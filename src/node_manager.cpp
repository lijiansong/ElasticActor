
#include <sstream>

#include "mola/node_manager.hpp"
#include "mola/singleton.hpp"
#include "mola/zookeeper.hpp"
#include "mola/gateway.hpp"

#if defined(CLIENT_CONTEXT)
#include "mola/client.hpp"
#endif

using namespace mola;

Node* Node::decode(Node::ID id, std::string& info, 
                   NodeManager *nmgr) {
  unsigned proto, endian;
  std::string ip;
  uint16_t port;

  std::istringstream ss(info);
  ss >> endian >> proto >> ip >> port;
  
  Node* node = new Node(nmgr, id, ip, port, proto, endian);
  
  unsigned mid;
  while (ss >> mid) {
    node->add_module(mid);
  }
 
  return node;
}

std::string Node::encode(Node* node) {
  std::stringstream ss;
  ss << node->get_endian() << " "
     << node->get_proto() << " "
     << node->get_ip() << " "
     << node->get_port() << " ";

  auto list = node->m_module_list;
  for (auto i : list) {
    if (std::get<2>(i))
      ss << std::get<1>(i) << " ";
  }

  return ss.str();
}

const Node::ID& Node::local_node_id() {
  static Node::ID nid = 
    m_nmgr->local_node()->get_id();
  return nid;
}

Node::Node(NodeManager* nmgr, ID id) 
  : m_nmgr(nmgr), m_id(id), m_client(nullptr) {}

Node::Node(NodeManager* nmgr, 
           ID id, std::string& ip, 
           uint16_t port, unsigned proto, unsigned endian)
  : m_nmgr(nmgr), m_id(id), m_ip(ip), m_port(port)
  , m_proto(proto), m_endian(endian), m_client(nullptr) {}

Node::~Node() {
  if (m_client != nullptr) {
    m_client->stop();
    delete m_client;
  }
}

void Node::add_module(uint32_t id, const char *name, bool pub) {
  if (name == nullptr) {
    std::string str = m_nmgr->get_module_name(id);
    m_module_list.emplace_back(str, id, pub);
    return;
  }

  m_module_list.emplace_back(std::string(name), id, pub);
}

bool Node::has_module(uint32_t id) {
  for (auto i : m_module_list) {
    if (std::get<1>(i) == id) return true;
  }
  return false;
}

bool Node::has_module(const std::string& name) {
  for (auto i : m_module_list) {
    if (std::get<0>(i) == name) return true;
  }

  auto id = m_nmgr->get_module_id(name.c_str());

  return has_module(id);
}

void Node::set_client(BaseClient* client) {
  utils::LockGuard lk(m_lock);
  m_client = client;
}

BaseClient* Node::get_client(bool create) {
  utils::LockGuard lk(m_lock);

  auto client = m_client;
  if (client != nullptr || !create) return client;

  GatewayClient * new_client = new GatewayClient(m_id);
  auto ret = new_client->connect(m_ip, m_port);

  MOLA_LOG_TRACE("try to connect another node (" 
                << m_id << ":" << m_ip << ":" << m_port <<")");

  if (ret < 0) {
    MOLA_LOG_TRACE("connect error, ret code is " << ret); 
    delete new_client;
    return nullptr; // TODO : global errno!!
  }
  
  /* sending the handshake signal to the peer */
  MOLA_LOG_TRACE("sending my node id " << local_node_id() << " to the peer");
  auto nid = htonl(local_node_id());
  new_client->async_write(reinterpret_cast<char*>(& nid), sizeof(nid));

  m_client = new_client;
  return new_client;
}

// TODO : using a new class to handle the gateway outbound,
//        since we need decouple the transfer mechnism from the Node class!!
int Node::deliver_message(MessageSafePtr& message) {
#if !defined(CLIENT_CONTEXT)
  if (this == m_nmgr->local_node().get()) {
    auto module = Module::find_module(message->get_target().module_id());
    MOLA_ASSERT(module != nullptr && ! module->is_overload());
    return module->dispatch(message);
  }
#else
  MOLA_ASSERT(this != m_nmgr->local_node().get());
#endif

  auto client = get_client(true);

  if (client == nullptr) {
    return -1;
  }

#if 0
  /* FIXME : make a private copy before doing the serialization !! */
  message->copy_on_write();
#endif

  /* Fix ID of current node */
  message->get_sender().set_node_id(local_node_id());

  /* set the target's nid as my node id */
  message->get_target().set_node_id(m_id);

#if defined(CLIENT_CONTEXT)
  MOLA_ASSERT(has_module(message->get_target().module_id()));
#endif

  /* try to dump the message into a buffer */
  char *pbuf = nullptr;
  size_t size = 0;

  bool need_free = message->serialize(pbuf, size, m_endian == BIG);
  auto raw = message.pass();

  MOLA_LOG_TRACE("deliver form " << std::hex 
                << MOLA_MESSAGE_SOURCE(raw->get_metadata()) << " to " 
                << MOLA_MESSAGE_TARGET(raw->get_metadata()) << std::dec);

  std::vector<StreamBuffer*> ss;

  ss.push_back(new StreamBuffer(sizeof(mola_message_metadata_t),
                                reinterpret_cast<char*>(& (raw->get_metadata())) )); 
  // FIXME : since the sending is asynchronized, 
  //         we must keep the message before all data transfer is done!!
  ss.push_back(new StreamBuffer(size, pbuf, need_free,
                                [raw]() { 
                                  MOLA_LOG_TRACE("release message!!");
                                  raw->dec_ref(); 
                                } ));

  client->async_write(ss);
  
  return 0;
}


DelayMessagePool::DelayMessagePool(NodeManager* mgr, uint32_t duration)
  : m_mgr(mgr), m_duration(duration), m_stop(false) {
#if defined(CLIENT_CONTEXT)
  auto self = this;
  m_this_thread = std::thread{
                    [self]() { self->loop(); } 
                  };
#else
  auto thread = new DelayMessageWorker(this);
  thread->start();
#endif //CLIENT_CONTEXT
}

DelayMessagePool::~DelayMessagePool() {
  // TODO : kill m_this_thread
  kill();
#if defined(CLIENT_CONTEXT)
  m_this_thread.join();
#endif // CLIENT_CONTEXT
}

void DelayMessagePool::loop() {
  AbsolateWorker::srand(::random());

  std::unique_lock<LockType> lock(m_mutex);
  
  while (! m_stop) {
    if (m_tasks.size() == 0 || m_event_que.size() == 0) {
      // if there is no tasks, just clear the events
      if (m_tasks.size() == 0)
        m_event_que.clear();
      if ( std::cv_status::timeout == 
            m_condvar.wait_for(lock, std::chrono::seconds(m_duration)) ) {
        // TODO : check the timeout messages, delete them !!
      }
      continue;
    }

    // for each active event, we find the tasks who subscribe it and trigger
    while (m_event_que.size() > 0) {
      auto ev = m_event_que.front();
      m_event_que.pop_front();

      auto i = m_tasks.find(ev);
      if (i == m_tasks.end()) 
        continue;
      
      TaskQue& q = i->second;
      while (q.size() > 0) {
        auto task = q.front();
        q.pop_front();

        if (task() != 0) 
          q.emplace_back(task);
      }
      // q is empty, just delete it from the task map !!
      m_tasks.erase(i);
    }

  }
  return;
}



std::unique_ptr<NodeManager> NodeManager::node_manager = nullptr;

NodeManager* NodeManager::initialize(InnerEngineType type, 
                                     const char* ns) {
  node_manager.reset(create(type, ns));
  return node_manager.get();
}

NodeManager* NodeManager::create(InnerEngineType type,
                                 const char* ns) {
  switch (type) {
  case NM_SINGLETON:
   {
     return new SingletonEngine;
   }
  case NM_ZOOKEEPER:
   {
     return new ZookeeperEngine(ns);
   }
  // TODO ..
  }
  return nullptr;
}

void NodeManager::sync_all_nodes_later() {
  m_need_sync.store(true);
  invalid_thread_cache();
}

void NodeManager::invalid_thread_cache() {
#if !defined(CLIENT_CONTEXT)
  auto &workers = AbsolateWorker::get_workers();
  for (auto &w : workers) {
    w->invalid_cached_node();
    w->invalid_cached_ns();
  }
#endif
}

NodeManager::NodePtr NodeManager::find_cached_node(Node::ID id) {
  auto i = m_alive_nodes.find(id);
  if (i != m_alive_nodes.end())
    return i->second;
  return nullptr;
}

bool NodeManager::delete_cached_node(Node::ID id) {
  auto itr = m_alive_nodes.find(id);
  if (itr != m_alive_nodes.end()) {
    m_alive_nodes.erase(itr);
    return true;
  }
  return false;
}

bool NodeManager::update_cached_node(NodePtr& node) {
  auto orig_node = find_cached_node(node->get_id());
  if (orig_node == nullptr ||
      ! orig_node->equals(node.get())) {
    m_alive_nodes.insert(std::make_pair(node->get_id(), node));
    return true;
  }
  return false;
}

NodeManager::NodePtr NodeManager::find_node(Node::ID id) {
#if defined(CLIENT_CONTEXT)
  NodePtr node;
#else
  auto worker = AbsolateWorker::get_current_worker<AbsolateWorker>();
  MOLA_ASSERT(worker != nullptr);

  NodePtr node = worker->find_cached_node(id);
  if (node.get() != nullptr) return node;
#endif

  utils::LockGuard lk(m_lock);

  if (m_need_sync.load()) {
    sync_all_nodes();
    m_need_sync.store(false);
  }

  node = find_cached_node(id);
  if (node.get() == nullptr) {
    node = _find_node(id);
    if (node.get() != nullptr) 
      update_cached_node(node);
  }

#if !defined(CLIENT_CONTEXT)
  // update the per-thread cache for this node
  worker->cache_a_node(id, node);
#endif

  return node;
}

NodeManager::NodeSet 
NodeManager::query_node_with_module(Module::ID mid) {
#if defined(CLIENT_CONTEXT)
  NodeSet set;
#else
  auto worker = AbsolateWorker::get_current_worker<AbsolateWorker>();
  MOLA_ASSERT(worker != nullptr);

  NodeSet set = worker->find_cached_ns(mid);
  if (set.size() > 0) return set;
#endif

  utils::LockGuard lk(m_lock);
 
  if (m_need_sync.load()) {
    sync_all_nodes();
    m_need_sync.store(false);
  }

  for (auto itr : m_alive_nodes) {
    auto node = itr.second;
    if (node->has_module(mid))
      set.push_back(node);
  }

#if !defined(CLIENT_CONTEXT)
  worker->cache_ns(mid, set);
#endif

  return set;
}

void NodeManager::create_local_node(const char* host, uint16_t port) {
  if (m_local_node.get() == nullptr) {
    std::string hostname;
    if (host != nullptr) hostname = host;
    if (hostname == "*") {
      if (Connection::get_primary_ip(hostname)) {
        MOLA_LOG_TRACE("using ipv4 address : " << hostname);
      } else {
        MOLA_ERROR_TRACE("cannot get the primary ipv4 address");
      }
    }

    m_local_node.reset( 
      new Node(this, Node::invalid_id, hostname, port) );

#if !defined(CLIENT_CONTEXT)   
    /* initialize the local gateway server */
    if (! m_single_node) {
      GatewayServer::initialize();
    }
#endif
  }
}
int NodeManager::get_node_count() {
  return m_alive_nodes.size();
}
int NodeManager::get_node_ids(Node::ID* ids) {
  assert(ids);
  int i = 0;
  for (auto itr : m_alive_nodes) {
    ids[i++] = itr.first;
  }
  return i;
}



uint32_t GetModuleID(const char* mname) {
  auto mgr = mola::NodeManager::instance();
  return (uint32_t)( mgr->get_module_id(mname, /*create = */true) );
}


void Publish(const char* event) {
  auto mgr = mola::NodeManager::instance();
  mgr->publish_event(event);
}

int GetNodeCount() {
  auto mgr = mola::NodeManager::instance();
  return mgr->get_node_count();
}
int GetNodeIDs(uint32_t* ids) {
  auto mgr = mola::NodeManager::instance();
  return mgr->get_node_ids(ids);
}
