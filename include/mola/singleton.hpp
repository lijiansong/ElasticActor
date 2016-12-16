#ifndef _MOLA_SINGLETON_H_
#define _MOLA_SINGLETON_H_

#include <map>
#include <vector>
#include <memory>

#include "mola/debug.hpp"
#include "mola/spinlock.hpp"
#include "mola/node_manager.hpp"

namespace mola {

class SingletonEngine : public NodeManager {

  typedef std::unique_ptr<utils::AsyncLock> LockType;
  typedef std::map<LockID, LockType> LockMap_;
  typedef utils::ThreadSafeWrapper<LockMap_> LockMap;
  typedef std::map<std::string, LockMap> LockZone;
  typedef std::vector<std::string> ModuleSet;
  
public:
  /** single node version, no need connect/disconnect/join/leave
   *  to any server, so ignore those virtual functions .. */
  int connect(const char *option, bool reset = false) override { return 0; }
  int disconnect() override { return 0; }

  int join(void *opaque, size_t opaque_len) override { return 0; }
  int leave() override { return 0; }

  Module::ID get_module_id(const char* name, bool create) override {
    for (unsigned i = 0; i < m_modules.size(); ++i) {
      if (m_modules[i] == name) return i;
    }

    if (create) {
      m_modules.push_back(name);
      return m_modules.size() - 1;
    }

    return Module::invalid_id;
  }

  std::string get_module_name(Module::ID id) override {
    static std::string nil;
    if (id < m_modules.size()) return m_modules[id];
    return nil;
  }

  void lock(const std::string& zone, LockID id,
            LockCallback callback) override {
    m_lock.lock();
    auto& lm = m_lock_zone[zone];
    m_lock.unlock();

    lm.lock();

    auto i = lm.find(id);
    if (i == lm.end()) {
      LockType lock(new utils::AsyncLock);
      lm.emplace(id, std::move(lock));
      i = lm.find(id);
    }

    lm.unlock(); // to avoid deadlock !!

    i->second->acquire(callback);
  }

  void unlock(const std::string& zone, LockID id) override {
    m_lock.lock();
    auto& lm = m_lock_zone[zone];
    m_lock.unlock();

    lm.lock();

    auto i = lm.find(id);
    MOLA_ASSERT(i != lm.end());

    lm.unlock();
    i->second->release(); 
    // TODO : if no pending thread, 
    // just release this lock !!
  }

  int sync_all_nodes() override {
    if (m_local_node.get() != nullptr) return 1;
    return 0;
  }

  void publish_event(const std::string& event) override {
    on_event(event);
  }

  SingletonEngine() : NodeManager(true) {}

  virtual ~SingletonEngine() {}

protected:
  NodePtr _find_node(Node::ID id) override { return NodePtr(nullptr); }

private:
  LockZone m_lock_zone;
  ModuleSet m_modules;
};

} // namespace mola
#endif // _MOLA_SINGLETON_H_
