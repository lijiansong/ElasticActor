#ifndef _MOLA_CLIENT_API_H_
#define _MOLA_CLIENT_API_H_

#include <pthread.h>

#include <map>
#include <mutex>
#include <condition_variable>

#include "c/mola_client.h"

#include "mola/gateway.hpp"
#include "mola/multiplexer.hpp"
#include "mola/load_balancer.hpp"

extern "C" {
  Connection ConnectToCluster(ClusterAddr addr);
  void DisconnectFromCluster(Connection conn);
  int SendMsgToCluster(Connection conn, const char* module,
                       unsigned intf, void * message);
  int RecvMsgFromCluster(Connection conn, const char* module,
                         void ** message);

  int RecvMsgWithMoFromCluster(Connection conn, void ** message,
                                char ** module);

  int AsyncSendMsgToCluster(Connection conn, const char* module,
                            unsigned intf, void * message,
                            RecvCallback callback, void * ctx);

  typedef int (*EventCallback) (Connection, void *);

  void Subscribe(Connection conn, const char* event, 
                 EventCallback callback, void * ctx);
}

namespace mola {

class NodeManager;

namespace client {
class ClientConn;

class ConnGatewayClient : public GatewayClient {
public:
  ConnGatewayClient(ClientConn * conn)
    : m_conn(conn) {}
  virtual ~ConnGatewayClient() {}

  ClientConn* get_conn() const { return m_conn; }
  void set_conn(ClientConn* conn) { m_conn = conn; }

private: 
  ClientConn* m_conn;
};

class ClientConn {
  typedef std::multimap<std::string, MessageWrapper*> MessageCache;
  typedef std::map<std::string, int> PendingCounter;
  typedef std::function<void(MessageSafePtr&)> ReqCallback;
  typedef std::map<uint32_t, ReqCallback> ReqStates;
  
  friend class mola::GatewayReceiver;
  friend class mola::DelayMessagePool;

  static bool initialized;
  static std::unique_ptr<LoadBalanceStrategy> lbs;
  static std::map<uint64_t, ClientConn*> client_map;
  static std::mutex lock;

  struct AsyncThread {
    ~AsyncThread() {
      auto handle = m_thread.native_handle();
      ::pthread_cancel((pthread_t)handle);
      m_thread.join();
    }

    void start() {
      m_thread = std::thread{ [] {
        auto poller = Multiplexer::instance();
        poller->poll();
      }};
    }

    std::thread m_thread;
  };

  static std::unique_ptr<AsyncThread> aio_thread;

public:
  
  static int initialize(LoadBalanceStrategy *strategy = nullptr) {
    std::unique_lock<std::mutex> lk(lock);

    if (!initialized) {
      // -- init the multiplexer --
      Multiplexer::initialize();

      aio_thread.reset( new AsyncThread );
      aio_thread->start();
      
      // -- init the load balancer --
      if (strategy == nullptr)
        strategy = new RandomStrategy;
      lbs.reset(strategy);

      initialized = true;
    }
    
    return 0;
  }

  static ClientConn* find_client(const ActorID& cid) {
    return client_map[cid.u64()];
  }

  static void register_client(ClientConn* client) {
    client_map.emplace(client->m_cid.u64(), client);
  }
  
  ClientConn(int nmgr_type, const char *ns = nullptr);
  ~ClientConn();

  int connect(const char *cluster_addr);
  int disconnect();

  int send_message(const std::string& mod, unsigned intf,
                   MessageSafePtr& message);

  int send_message(const std::string& mod, unsigned intf,
                   MessageSafePtr& message, ReqCallback callback);

  int recv_message(const std::string& mod, MessageSafePtr& message,
                   char ** recv_module = nullptr);

  void subscribe(const std::string& event, EventCallback, void*);

protected:
  int send_message(NodeManager::NodePtr& node,
                   MessageSafePtr& message);

  int deliver_message(MessageSafePtr& message);

private:
  ActorID m_cid;
  std::mutex m_lock;
  std::condition_variable m_condvar;
  std::unique_ptr<NodeManager> m_node_mgr;

  MessageCache m_msg_cache;
  PendingCounter m_pending;
  ReqStates m_req_states;
};

}
}

#endif // _MOLA_CLIENT_API_H_
