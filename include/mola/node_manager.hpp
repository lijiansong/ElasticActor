#ifndef _MOLA_NODE_MANAGER_H_
#define _MOLA_NODE_MANAGER_H_

#include <map>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "mola/message.hpp"
#include "mola/module.hpp"

extern "C" {
  uint32_t GetModuleID(const char* mname);

  void Publish(const char* event);
}

namespace mola {

class BaseClient;
class NodeManager;

class Node {
  friend class NodeManager;

public:
  enum {
    PROTO_TCP = 0,
    PROTO_UDP = 1,
    // TODO
  };

  enum {
    LITTLE = 0,
    BIG = 1,
  };

  typedef std::tuple<std::string, Module::ID, bool> ModuleInfo;
  typedef std::vector<ModuleInfo> ModuleInfoList;

  typedef uint32_t ID;

  static const ID invalid_id = 0xffffffffUL;

  static Node * decode(ID id, std::string& info, NodeManager*);
  static Node * decode(ID id, const char *info, NodeManager* nmgr) {
    std::string s(info);
    return decode(id, s, nmgr);
  }

  static std::string encode(Node * node);

  const ID& local_node_id();

  void set_id(ID id) { m_id = id; }
  ID get_id() const { return m_id; }

  bool invalid() const { return m_id == invalid_id; }
 
  bool has_module(const std::string& name);
  bool has_module(uint32_t id);
  void add_module(uint32_t id, const char* name = nullptr, bool pub = true);

  void set_addr(std::string& ip, uint16_t port, 
                unsigned proto = PROTO_TCP) {
    m_ip = ip;
    m_port = port;
    m_proto = proto;
  }

  void get_addr(std::string& ip, uint16_t &port, unsigned &proto) {
    ip = m_ip;
    port = m_port;
    proto = m_proto;
  }

  void set_ip(std::string& ip) { m_ip = ip; }
  std::string get_ip() const { return m_ip; }

  void set_port(uint16_t port) { m_port = port; }
  uint16_t get_port() const { return m_port; }

  void set_proto(unsigned proto) { m_proto = proto; }
  unsigned get_proto() const { return m_proto; }

  void set_endian(unsigned endian) { m_endian = endian; }
  unsigned get_endian() const { return m_endian; }

  void set_client(BaseClient *client);
  BaseClient* get_client(bool create = false);

  int deliver_message(MessageSafePtr& message);
 
  virtual ~Node();

  bool same_addr(const Node* node) {
    return (m_ip == node->m_ip &&
            m_port == node->m_port &&
            m_proto == node->m_proto);
  }

  bool same_endian(const Node* node) {
    return m_endian == node->m_endian;
  }

protected:
  Node(NodeManager* nmgr, ID id = invalid_id);

  Node(NodeManager* nmgr, ID id, 
       std::string& ip, uint16_t port, 
       unsigned proto = PROTO_TCP,
#ifdef __LITTLE_ENDIAN__
       unsigned endian = LITTLE
#else
       unsigned endian = BIG
#endif // __LITTLE_ENDIAN__
       );


  bool equals(const Node* node) {
    return (m_id == node->m_id &&
            m_ip == node->m_ip &&
            m_port == node->m_port &&
            m_proto == node->m_proto &&
            m_endian == node->m_endian);
  }


private:
  ID m_id;
  unsigned m_endian;
  unsigned m_proto;
  uint16_t m_port;
  std::string m_ip;

  ModuleInfoList m_module_list;
  utils::SpinLock m_lock;
  BaseClient* m_client;

  NodeManager *m_nmgr;
};

class NodeManager;
class DelayMessageWorker;

class DelayMessagePool {
  friend class NodeManager;
  friend class DelayMessageWorker;

  typedef std::mutex LockType;
  typedef std::deque<std::function<int()>> TaskQue;
  typedef std::map<std::string, TaskQue> TaskQueMap;
  typedef std::deque<std::string> EventQue;
  typedef std::vector<std::shared_ptr<Node>> NodeSet;

public:
  virtual ~DelayMessagePool();

protected:
  DelayMessagePool(NodeManager* mgr, uint32_t duration = 60);
    
  void add(const std::string& event, std::function<int()> task) {
    std::unique_lock<LockType> lock(m_mutex);
    m_tasks[event].emplace_back(task);
  }

  void notify(const std::string& event) {
    std::unique_lock<LockType> lock(m_mutex);

    auto i = m_tasks.find(event);
    if (i != m_tasks.end() && i->second.size() > 0) {
      m_event_que.push_back(event);
      m_condvar.notify_one();
    }
  }
  
  void kill() {
    std::unique_lock<LockType> lock(m_mutex);
    m_stop = true;
    m_condvar.notify_one();
  }

  void loop();

private:

  NodeManager* m_mgr;
  uint32_t m_duration;
  bool m_stop;
  std::condition_variable m_condvar;
  TaskQueMap m_tasks;
  EventQue m_event_que;
  LockType m_mutex;
#if defined(CLIENT_CONTEXT)
  std::thread m_this_thread;
#endif // CLIENT_CONTEXT
};


class NodeManager {
private:
  static std::unique_ptr<NodeManager> node_manager;

public:
  typedef uint64_t LockID;

  typedef std::shared_ptr<Node> NodePtr;
  typedef std::vector<NodePtr> NodeSet;
  typedef std::map<Node::ID, NodePtr> NodeMap;

  typedef std::function<void()> LockCallback;

  enum InnerEngineType {
    NM_SINGLETON = 0,
    NM_ZOOKEEPER = 1,
    // TODO
  };

  static NodeManager* create(InnerEngineType, const char*); 

  static NodeManager* initialize(InnerEngineType type,
                                 const char *ns = nullptr); 

  static NodeManager* instance() { return node_manager.get(); }

public:
  virtual int connect(const char *option, bool reset = false) = 0;
  virtual int disconnect() = 0;

  virtual int join(void *opaque, size_t opaque_len) = 0;
  virtual int leave() = 0;

  /** get or allocate a module id by given name.
   *  it will send request to the cluster manager to get 
   *  the unique id among the cluster env. */
  virtual Module::ID get_module_id(const char* name, bool create = false) = 0;

  /** get the module name with given unique id */
  virtual std::string get_module_name(Module::ID id) = 0;

  virtual void lock(const std::string&, LockID, LockCallback) = 0;
  virtual void unlock(const std::string&, LockID) = 0;

  void register_module(Module::ID id, const char *name, bool pub) {
    // will publish this module to the node manager,
    // so this module deplyment will be seen by all other nodes.
    local_node()->add_module(id, name, pub);
  }

  Module::ID register_module(const char *name, bool pub = true) {
    auto id = get_module_id(name, true);
    register_module(id, name, pub);
    return id;
  }

  /** synchronized lock */
  void lock(const std::string& zone, LockID id) {
    std::mutex mutex;
    std::condition_variable cond;
    bool acquired = false;

    lock(zone, id, [&]() { 
      std::unique_lock<std::mutex> lk(mutex);
      acquired = true;
      cond.notify_one();
    });

    {
      std::unique_lock<std::mutex> lk(mutex);
      if (!acquired)
        cond.wait(lk);
    }
  }

  void lock(LockID id) { lock("__GLOBAL__", id); }
  void unlock(LockID id) { unlock("__GLOBAL__", id); }
 
  /** find the Node with given node id */
  NodePtr find_node(Node::ID id);

  /** delete the unreachable node by its id */
  virtual void delete_node(Node::ID id) {
    MOLA_ASSERT(0 && "undefined 'delete_node' methord");
  }

  /** get the Node set which have the module with the given ID */
  NodeSet query_node_with_module(Module::ID mid);

  NodeSet query_node_with_module(const std::string& name) {
    auto mid = get_module_id(name.c_str());
    return query_node_with_module(mid);
  }
  
  void create_local_node(const char* host = nullptr, uint16_t port = 0);
  const NodePtr& local_node() const { return m_local_node; }
  NodePtr local_node() { return m_local_node; }

  int add_delay_task(std::function<int()> task) {
    if (m_delay_pool.get() != nullptr) {
      m_delay_pool->add("NODE_CHANGE", task);
      return 0;
    }
    return -1;
  }

  void trigger_delay_tasks() {
    if (m_delay_pool.get() != nullptr) {
      m_delay_pool->notify("NODE_CHANGE");
    }
  }

  void on_event(const std::string& event) {
    if (m_delay_pool.get() != nullptr) {
      m_delay_pool->notify(event);
    }
  }

  virtual void subscribe_event(const std::string& event, 
                               std::function<int()> handler) {
    if (m_delay_pool.get() != nullptr) {
      m_delay_pool->add(event, handler);
    }
  }

  virtual void publish_event(const std::string& event) = 0;

  virtual ~NodeManager() {
    m_local_node.reset();
    m_alive_nodes.clear();
  }

  NodeManager(bool single_node = false) 
    : m_single_node(single_node)
    , m_need_sync(true)
    , m_delay_pool(nullptr) {

    if (!single_node) {
      m_delay_pool.reset(new DelayMessagePool(this));
    }
  }

public:
  int get_node_count();
  int get_node_ids(Node::ID*);

protected:
  NodePtr find_cached_node(Node::ID id);
  bool delete_cached_node(Node::ID id);
  bool update_cached_node(NodePtr& node);

  virtual NodePtr _find_node(Node::ID id) = 0;

  virtual int sync_all_nodes() = 0;

  void sync_all_nodes_later();
  
  void invalid_thread_cache();

  utils::SpinLock m_lock;
  NodeMap m_alive_nodes;

  NodePtr m_local_node;

private:
  std::unique_ptr<DelayMessagePool> m_delay_pool;
  std::atomic<bool> m_need_sync;
  bool m_single_node;
};

} // namespace mola

extern "C" {
    int GetNodeCount();
    int GetNodeIDs(uint32_t*);
}

#endif // _MOLA_NODE_MANAGER_H_
