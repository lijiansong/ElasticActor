#ifndef _MOLA_ZOOKEEPER_H_
#define _MOLA_ZOOKEEPER_H_

#include <zookeeper/zookeeper.h>

#include <cstring>
#include <thread>
#include <deque>
#include <mutex>
#include <condition_variable>

#include "mola/node_manager.hpp"

#define NM_ZNODE_ "/MOLA"
#define NODE_ZNODE_ "/NODES"
#define MODULE_ZNODE_ "/MODULES"
#define LOCK_ZNODE_ "/LOCKS"
#define CLIENT_ZNODE_ "/CLIENTS"
#define EVENT_ZNODE_ "/EVENTS"

namespace mola {

class ZookeeperEngine : public NodeManager {

  static const int MAX_NODE_STR_LEN = 1024;

  // not static since the ZookeeperEngine is singletion
  char NM_ZNODE[MAX_NODE_STR_LEN];
  char NODE_ZNODE[MAX_NODE_STR_LEN];
  char MODULE_ZNODE[MAX_NODE_STR_LEN];
  char LOCK_ZNODE[MAX_NODE_STR_LEN];
  char CLIENT_ZNODE[MAX_NODE_STR_LEN];
  char EVENT_ZNODE[MAX_NODE_STR_LEN];

  typedef uint64_t LockID;

  struct Lock : public utils::AsyncLock {
    Lock(const std::string& zone, LockID id)
      : m_zone(zone), m_id(id) { }
    
    void wait(LockCallback callback) {
      m_wait_que.emplace_front(callback);
    }

    void notify() {
      utils::LockGuard lk(m_lock);
      if (m_wait_que.size() > 0) {
        auto callback = m_wait_que.front();
        m_wait_que.pop_front();
        lk.unlock(); // TO AVOID DEADLOCK!!
        return callback();
      }
    }

    std::string m_zone;
    LockID m_id;
    std::string m_path;
  };
  
  typedef std::shared_ptr<Lock> LockPtr;
  typedef utils::ThreadSafeWrapper<std::map<LockID, LockPtr>> LockHashTable;
  typedef std::map<std::string, LockHashTable> LockHashTableZone;
  
  const long SESSION_TIMEOUT = 30000; 

  static void zk_watcher(zhandle_t *zh, int type,
                         int state, const char* path,
                         void * watcher_ctx);
public:
  int connect(const char *option, bool reset = false) override;
  int disconnect() override;

  int join(void *opaque, size_t opaque_len) override;
  int leave() override;

  Module::ID get_module_id(const char *name, bool create) override;
  std::string get_module_name(Module::ID id) override;

  void lock(const std::string&, LockID, LockCallback) override;
  void unlock(const std::string&, LockID) override;

  void wakeup_internal(const std::string& zone, LockID id); 

  void zk_lock(Lock* ilock, LockCallback callback);

  void delete_node(Node::ID id) override;

  ZookeeperEngine(const char *ns = nullptr) 
    : NodeManager(false)
    , m_zhdl(nullptr) {
    if (ns == nullptr || strlen(ns) == 0) {
      strcpy(NM_ZNODE, NM_ZNODE_);
    } else {
      sprintf(NM_ZNODE, "/%s", ns);
    }

    sprintf(NODE_ZNODE, "%s" NODE_ZNODE_, NM_ZNODE);
    sprintf(MODULE_ZNODE, "%s" MODULE_ZNODE_, NM_ZNODE);
    sprintf(LOCK_ZNODE, "%s" LOCK_ZNODE_, NM_ZNODE);
    sprintf(CLIENT_ZNODE, "%s" CLIENT_ZNODE_, NM_ZNODE);
    sprintf(EVENT_ZNODE, "%s" EVENT_ZNODE_, NM_ZNODE);
  }
  
  void subscribe_event(const std::string& event,
                       std::function<int()> handler) override;

  void publish_event(const std::string& event) override;

  virtual ~ZookeeperEngine() {}

protected:
  NodePtr _find_node(Node::ID id) override;
  int sync_all_nodes() override;

private:
  int foreach_node(const char* parent, std::function<void(char*)>);

  uint32_t get_and_inc_seq_num(const char* path);

  int get_least_seq(const char *parent, char *least_seq_path);

  ::zhandle_t *m_zhdl; 

  LockHashTableZone m_lock_zone;

  typedef std::map<std::string, Module::ID> ModuleInfoCache;
  ModuleInfoCache m_module_cache;
};

}


#endif // _MOLA_ZOOKEEPER_H_
