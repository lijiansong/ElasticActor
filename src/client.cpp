#include "mola/debug.hpp"
#include "mola/client.hpp"

using namespace mola;
using namespace mola::client;

// FIXME : noused but included by some modules ..
thread_local uint32_t AbsolateWorker::seed;

std::unique_ptr<LoadBalanceStrategy> ClientConn::lbs;

bool ClientConn::initialized = false;

std::unique_ptr<ClientConn::AsyncThread> ClientConn::aio_thread;

std::map<uint64_t, ClientConn*> ClientConn::client_map;
std::mutex ClientConn::lock;

ClientConn::ClientConn(int nmgr_type, const char* ns)
  : m_cid((uint64_t)-1) {

  NodeManager* nmgr = 
    NodeManager::create(
        static_cast<NodeManager::InnerEngineType>(nmgr_type), ns);
  MOLA_ASSERT(nmgr != nullptr);

  m_node_mgr.reset(nmgr);
  m_node_mgr->create_local_node("<CLIENT>", 0);
  
}

ClientConn::~ClientConn() {}

int ClientConn::connect(const char *cluster_addr) {
  ClientConn::initialize();

  int ret = m_node_mgr->connect(cluster_addr);
  MOLA_ASSERT(ret == 0);
  ret = m_node_mgr->join(NULL, 0);
  MOLA_ASSERT(ret == 0);

  auto node = m_node_mgr->local_node();
  m_cid.set_node_id(node->get_id());
  
  register_client(this);

  return ret;
}

int ClientConn::disconnect() {
  m_node_mgr->leave();
  m_node_mgr->disconnect();
  return 0;
}

int ClientConn::send_message(const std::string& mod, unsigned intf,
                             MessageSafePtr& message, ReqCallback callback) {
  uint32_t reqid;
  do {
    reqid = static_cast<uint32_t>(::rand());
  } while (reqid == 0);

  message->set_reqid(reqid);
  {
    std::unique_lock<std::mutex> lock(m_lock);
    m_req_states.emplace(reqid, callback); 
  }
  return send_message(mod, intf, message);
}

int ClientConn::send_message(const std::string& mod, unsigned intf, 
                             MessageSafePtr& message) {
  // set the message metadata:
  message->set_target(ActorID::ANY, ActorID::ANY, intf, ActorID::ANY);
  message->set_sender(m_cid);
  message->set_request(); // !!

  auto callback = [=]() mutable -> int {
    auto mid = m_node_mgr->get_module_id(mod.c_str());
    if (mid == Module::invalid_id) return -1;

    auto candidates = m_node_mgr->query_node_with_module(mid);
    if (candidates.size() == 0) return -1;
    
    message->get_target().set_module_id(mid);

    auto node = lbs->select_one_node(candidates);
    MOLA_ASSERT(node->has_module(mid));

    return send_message(node, message);  
  };

  if (callback() != 0)
    m_node_mgr->add_delay_task(callback);
  
  return 0;
}

int ClientConn::send_message(NodeManager::NodePtr& node,
                             MessageSafePtr& message) {
  if (node->get_client() == nullptr) {
    auto client = new ConnGatewayClient(this);
    int ret = client->connect(node->get_ip().c_str(), node->get_port());
    MOLA_ASSERT(ret > 0);

    auto nid = ::htonl(m_cid.node_id());
    client->async_write(reinterpret_cast<char*>(& nid), sizeof(nid));
    node->set_client(client);
  }

  return node->deliver_message(message);
}

int ClientConn::recv_message(const std::string& mod, 
                             MessageSafePtr& message,
                             char ** recv_module) {
  std::unique_lock<std::mutex> lock(m_lock);
  bool match_any = (mod == "*");

  for (;;) {
    auto count = ( match_any
                     ? m_msg_cache.size()
                     : m_msg_cache.count(mod) );
    if (count == 0) {
      m_pending[mod]++;
      m_condvar.wait(lock);
      m_pending[mod]--;
      continue;
    }

    auto itr = ( match_any 
                   ? m_msg_cache.begin()
                   : m_msg_cache.find(mod) );
    message = itr->second;

    if (recv_module != nullptr) {
      // this may only useful for the case using
      // the wildcard "*" for module name
      *recv_module = ::strdup(itr->first.c_str());
    }

    m_msg_cache.erase(itr);
    break;
  }
  return 0;
}

int ClientConn::deliver_message(MessageSafePtr& message) {
  if (message.is_nil()) return -1;

  auto target = message->get_target();
  MOLA_ASSERT(target == m_cid);

  auto reqid = message->get_reqid();

  auto mid = message->get_sender().module_id();
  
  std::unique_lock<std::mutex> lock(m_lock);
  // find the req states map ..
  auto i = m_req_states.find(reqid);
  if (i != m_req_states.end()) {
    (i->second)(message);
    m_req_states.erase(i);
  } else {
    auto mod = m_node_mgr->get_module_name(mid);
    m_msg_cache.emplace(mod, message.pass());
    if (m_pending[mod] > 0 || m_pending["*"] > 0)
      m_condvar.notify_one();
  }

  return 0;
}

void ClientConn::subscribe(const std::string& event,
                           EventCallback callback, void * ctx) {
  m_node_mgr->subscribe_event(event, [=]() -> int {
    ::Connection conn;
    conn.conn_ctx = this;
    return callback(conn, ctx);
  });
}



//---------- The C APIs for client connection --------- //
::Connection ConnectToCluster(ClusterAddr addr) {
  ::Connection conn;

  ClientConn *client = nullptr;
  const char * endp = strchr(addr, '@');
  
  if (endp != nullptr) {
    std::string ns(addr, endp - addr);
    client = new ClientConn(NodeManager::NM_ZOOKEEPER, ns.c_str());
    client->connect(endp + 1);
  } else {
    client = new ClientConn(NodeManager::NM_ZOOKEEPER);
    client->connect(addr);
  }

  conn.conn_ctx = client;
  return conn;
}

void DisconnectFromCluster(::Connection conn) {
  MOLA_ASSERT(conn.conn_ctx != nullptr);
  auto client = reinterpret_cast<ClientConn*>(conn.conn_ctx);
  client->disconnect();
  delete client;
}

int SendMsgToCluster(::Connection conn, const char* module,
                     unsigned intf, void *message) {
  MOLA_ASSERT(conn.conn_ctx != nullptr);
  auto client = reinterpret_cast<ClientConn*>(conn.conn_ctx);
 
  MessageSafePtr rt_msg(GEN_MESSAGE(message));
  auto ret = client->send_message(module, intf, rt_msg);
  rt_msg.pass();
  return ret;
}

int AsyncSendMsgToCluster(::Connection conn, const char *module,
                          unsigned intf, void *message,
                          RecvCallback callback, void *ctx) {
  MOLA_ASSERT(conn.conn_ctx && callback);
  auto client = reinterpret_cast<ClientConn*>(conn.conn_ctx);

  MessageSafePtr rt_msg(GEN_MESSAGE(message));
  auto ret = client->send_message(module, intf, rt_msg,
                       [=](MessageSafePtr& msg) {
                         callback(conn, msg.pass(), ctx);
                       } );
  rt_msg.pass();
  return ret;
}

int RecvMsgFromCluster(::Connection conn, const char* module,
                       void **message) {
  MOLA_ASSERT(conn.conn_ctx != nullptr);
  auto client = reinterpret_cast<ClientConn*>(conn.conn_ctx);
   
  MessageSafePtr response;
  if (module != nullptr && strlen(module) > 0)
    client->recv_message(module, response);
  else
    client->recv_message("*", response);
  
  if (response.is_nil()) return -1;
  
  *message = response.pass();
  return 0;
}

int RecvMsgWithMoFromCluster(::Connection conn, 
                             void **message,
                             char ** module) {
  MOLA_ASSERT(conn.conn_ctx != nullptr);
  auto client = reinterpret_cast<ClientConn*>(conn.conn_ctx);
   
  MessageSafePtr response;
  client->recv_message("*", response, module);
  
  if (response.is_nil()) return -1;
  
  *message = response.pass();
  return 0;
}

void Subscribe(::Connection conn,
               const char* event,
               EventCallback callback, void * ctx) {
  MOLA_ASSERT(conn.conn_ctx != nullptr);
  auto client = reinterpret_cast<ClientConn*>(conn.conn_ctx);
  client->subscribe(event, callback, ctx);
}
