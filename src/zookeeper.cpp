#include <unistd.h>
#include <limits.h>
#include <time.h>

#include <cstring>
#include <mutex>

#include "mola/zookeeper.hpp"

#define FOR_EACH_ZNODE(parent, path, strs)  \
    for ((strs)->data += (strs)->count;     \
          (strs)->count-- ?                 \
                snprintf(path, sizeof(path), "%s/%s", parent, \
                    *--(strs)->data) : (free((strs)->data), 0);\
          free(*(strs)->data))


#define ZKID_TO_CLIENTID(id) ((id) + 0x10000)
#define CLIENTID_TO_ZKID(id) ((id) - 0x10000)

#define ZKID_TO_NODEID(id) ((id) + 1)
#define NODEID_TO_ZKID(id) ((id) - 1)

#define IS_CLIENT_ID(id) ((id) >= 0x10000)

using namespace mola;

void ZookeeperEngine::zk_watcher(::zhandle_t *zh, int type,
                                 int state, const char *path,
                                 void * watcher_ctx) {
  ZookeeperEngine * zk = 
    reinterpret_cast<ZookeeperEngine*>(watcher_ctx); 

  int ret;
  char str[MAX_NODE_STR_LEN], zone[MAX_NODE_STR_LEN];
  
  /* TODO : handle the session loss .. */
  if (type == ZOO_CHILD_EVENT) {
    if (!strcmp(path, zk->NODE_ZNODE)) {
      /* there is new node joined this cluster,
       * synchronize all nodes rigth now */
      MOLA_ALTER_TRACE("detect a new node join to this cluster!");
      zk->sync_all_nodes();
    }

  } else if (type == ZOO_DELETED_EVENT) {
    size_t n;  
    /* process distributed lock */
    LockID lock_id;
    
    n = strlen(zk->LOCK_ZNODE);

    if (!strncmp(path, zk->LOCK_ZNODE, n)) {
      MOLA_LOG_TRACE("unlock event for \"" << path << "\"");
      /* skip the top dir level */  
      const char *p = path + n;
      if (*p == '/') {
        const char *e = ::strchr(p+1, '/');

        if (e != nullptr) {
          strncpy(zone, p+1, e-p-1);
          ret = sscanf(e, "/%" __UINT64_FMTu__ "/%s", &lock_id, str);
          if (ret == 2) {
            zk->wakeup_internal(zone, lock_id);
            return;
          }
        }
      }
    }
    /* handle the node offline */
    Node::ID id;

    n = strlen(zk->NODE_ZNODE);
    if (!strncmp(path, zk->NODE_ZNODE, n) &&
         sscanf(path + n, "/%010d", &id) == 1) {

      MOLA_ERROR_TRACE("node offline, id = " << ZKID_TO_NODEID(id));

      zk->delete_node(ZKID_TO_NODEID(id));
      return;
    }
 
    /* handle the client offline */
    n = strlen(zk->CLIENT_ZNODE);
    if (!strncmp(path, zk->CLIENT_ZNODE, n) &&
         sscanf(path + n, "/%010d", &id) == 1) {

      MOLA_ERROR_TRACE("cline offline, id = " << ZKID_TO_CLIENTID(id));

      zk->delete_node(ZKID_TO_CLIENTID(id));
      return;
    }
    
    /* ---- TODO ---- */
    MOLA_ASSERT(0); 
  } else if (type == ZOO_CHANGED_EVENT) {
    int n = strlen(zk->EVENT_ZNODE);
    if (!strncmp(path, zk->EVENT_ZNODE, n)) {
      const char *event = path + n + 1;
      zk->on_event(event);
      // get for watching next time ..
      n = -1;
      ::zoo_get(zk->m_zhdl, path, 1, nullptr, &n, nullptr);
    }
  }
   
  return ;
}

int ZookeeperEngine::connect(const char *option, bool reset) {
  int zk_timeout = SESSION_TIMEOUT;
  const char * hosts = nullptr;
  char *timeout;

  std::unique_ptr<char[]> temp(new char[strlen(option)+1]); 
  char *option_ = temp.get();

  strcpy(option_, option);

  hosts = strtok((char*)option_, "=");
  if (timeout = strtok(NULL, "=")) {
    if (sscanf(timeout, "%" __UINT32_FMTu__, &zk_timeout) != 1) {
      MOLA_ERROR_TRACE( "failed to initialize the zookeeper server "
                      << option << "!");
      return -1;
    }
  }

  ::zoo_set_debug_level(ZOO_LOG_LEVEL_ERROR);
  m_zhdl = ::zookeeper_init(hosts, zk_watcher, zk_timeout,
                            nullptr, this, 0);
  if (!m_zhdl) {
    // report error
    return -1;
  }

  int interval = 100;
  int retry = 0, max_retry = zk_timeout / interval;

  while (::zoo_state(m_zhdl) != ZOO_CONNECTED_STATE) {
    ::usleep(interval * 1000);
    if (++retry >= max_retry) {
      return -1;
    }
  }

  int rc;

  // checking the option ..
  if (reset) {
    // recursive delete all temp nodes ..
    foreach_node(NODE_ZNODE, [&](char *p) {
      int rc = ::zoo_delete(m_zhdl, p, -1);
      if (rc != ZOK) {
        MOLA_ERROR_TRACE("cannot delete temp node " << p);
      }
    });
  } 

  // try to create the namespace ZNODE ..
  rc = ::zoo_create(m_zhdl, NM_ZNODE, "ICT", 3,
                        &ZOO_OPEN_ACL_UNSAFE, 0, nullptr, 0);
  if (rc != ZOK && rc != ZNODEEXISTS) {
    // report error
    return rc;
  }

  // I'm the first one to get here, create the global dirs
  if (rc == ZOK) {
    /* create the global node dir */
    int rc1 = ::zoo_create(m_zhdl, NODE_ZNODE, "0", 1,
                           &ZOO_OPEN_ACL_UNSAFE, 0, nullptr, 0);

    /* create the global module dir */
    int rc2 = ::zoo_create(m_zhdl, MODULE_ZNODE, "0", 1, 
                           &ZOO_OPEN_ACL_UNSAFE, 0, nullptr, 0);

    /* create the global lock dir (distributed locks) */
    int rc3 = ::zoo_create(m_zhdl, LOCK_ZNODE, "0", 1,
                           &ZOO_OPEN_ACL_UNSAFE, 0, nullptr, 0);

    /* create the global client dir */
    int rc4 = ::zoo_create(m_zhdl, CLIENT_ZNODE, "0", 1,
                           &ZOO_OPEN_ACL_UNSAFE, 0, nullptr, 0);

    /* create the global event dir */
    int rc5 = ::zoo_create(m_zhdl, EVENT_ZNODE, "0", 1,
                           &ZOO_OPEN_ACL_UNSAFE, 0, nullptr, 0);

    MOLA_ASSERT(rc1 == ZOK && rc2 == ZOK &&
              rc3 == ZOK && rc4 == ZOK && rc5 == ZOK);
  }

  return 0;
}

int ZookeeperEngine::disconnect() {
  if (m_zhdl == nullptr) {
    // report error
    return -1;
  }
  
  ::zookeeper_close(m_zhdl);
  m_zhdl = nullptr;
  return 0;
}

uint32_t ZookeeperEngine::get_and_inc_seq_num(const char* path) {
  MOLA_ASSERT(m_zhdl != nullptr);

  int rc, len = MAX_NODE_STR_LEN;
  uint32_t id;
  char seq[MAX_NODE_STR_LEN];

  rc = ::zoo_get(m_zhdl, path, 0, seq, &len, nullptr);
  MOLA_ASSERT(rc == ZOK && len > 0);
  
  seq[len] = 0;
  
  id = std::stoul(seq);

  sprintf(seq, "%" __UINT32_FMTu__, id+1);

  rc = ::zoo_set(m_zhdl, path, seq, strlen(seq), -1);
  MOLA_ASSERT(rc == ZOK);

  return id;
}

int ZookeeperEngine::join(void *opaque, size_t opaque_len) {
  if (m_zhdl == nullptr) return -1;
  
  int rc, id;
  char path[MAX_NODE_STR_LEN], my_path[MAX_NODE_STR_LEN];
  
  NodePtr node = local_node();

#if !defined(CLIENT_CONTEXT)
  /* delete the node with the same address. 
   * it must be a zombie node in zookeeper. */
  sync_all_nodes();
  {
    utils::LockGuard lk(m_lock);
    
    for (auto i : m_alive_nodes) {
      auto node_ = i.second;
      if (node->same_addr(node_.get())) {
        // delete the zk node ..
        sprintf(path, "%s/%010d", NODE_ZNODE, 
                NODEID_TO_ZKID( node_->get_id()) );
        ::zoo_delete(m_zhdl, path, -1);
      }
    }
  }

#endif 

  std::string info = Node::encode(node.get());

#if defined(CLIENT_CONTEXT)
  strcpy(path, CLIENT_ZNODE);
#else
  strcpy(path, NODE_ZNODE);
#endif
  strncat(path, "/", MAX_NODE_STR_LEN);

  /* create the temp directory for current Node */
  rc = ::zoo_create(m_zhdl, path, info.c_str(), info.length(),
                    &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL|ZOO_SEQUENCE, 
                    my_path, MAX_NODE_STR_LEN);

  MOLA_ASSERT(rc == ZOK);

  strncat(path, "%010d", MAX_NODE_STR_LEN);
  rc = sscanf(my_path, path, &id);
  MOLA_ASSERT(rc == 1);

#if defined(CLIENT_CONTEXT)
  node->set_id(ZKID_TO_CLIENTID(id));
#else
  /* set the unique node id, make sure the node id is
   * from 1 to 65534. 0 is LOCAL, 65535 is ANY */
  node->set_id(ZKID_TO_NODEID(id));
#endif

  MOLA_ALTER_TRACE("my node id is " << node->get_id()
		    << " {{ " << Node::encode(node.get()) << " }} ");

  update_cached_node(node);

  return 0;
}

int ZookeeperEngine::leave() {
  if (m_zhdl == nullptr) return -1;

  char path[MAX_NODE_STR_LEN];
#if defined(CLIENT_CONTEXT)
  sprintf(path, "%s/%010d", CLIENT_ZNODE, 
          CLIENTID_TO_ZKID(local_node()->get_id()) );
#else
  sprintf(path, "%s/%010d", NODE_ZNODE, 
          NODEID_TO_ZKID(local_node()->get_id()) );
#endif

  int rc = ::zoo_delete(m_zhdl, path, -1);
  MOLA_ASSERT(rc == ZOK);

  return 0;
}

void ZookeeperEngine::delete_node(Node::ID id) {
  decltype(m_alive_nodes)::size_type size = 0;
  {
    utils::LockGuard lk(m_lock);
    size = m_alive_nodes.erase(id);
  }

  if (size > 0) {
    /* invalid threads' node cache..
     * FIXME : how to invalid just one cache line ?? */
    invalid_thread_cache();
  }
}

Module::ID ZookeeperEngine::get_module_id(const char *name, bool create) {
  /* check my cache info firstly */
  auto i = m_module_cache.find(std::string(name));
  if (i != m_module_cache.end()) {
    return i->second;
  }

  Module::ID id(Module::invalid_id);
  std::string parent(MODULE_ZNODE);
  struct String_vector modules; // FIXME
  char *p, path[MAX_NODE_STR_LEN], mid[MAX_NODE_STR_LEN];
  int len = MAX_NODE_STR_LEN;
 
  int rc = ::zoo_get_children(m_zhdl, parent.c_str(), 1, &modules);
  
  FOR_EACH_ZNODE(parent.c_str(), path, &modules) {
    p = strrchr(path, '/');
    if (!strcmp(name, p+1)) {
      /* fetch the value of the current child node */
      rc = ::zoo_get(m_zhdl, path, 1, mid, &len, nullptr);
      MOLA_ASSERT(rc == ZOK && len > 0);
      
      mid[len] = 0; // FIXME : BUG of zookeeper C ??

      id = std::stoul(mid); 
    }
  }

  /* the module is first register, add name to the module dir */
  if (id == Module::invalid_id && create) {
    char *p, path[MAX_NODE_STR_LEN], mid[MAX_NODE_STR_LEN];
    
    id = get_and_inc_seq_num(MODULE_ZNODE);
    sprintf(mid, "%" __UINT32_FMTu__, id);

    sprintf(path, "%s/%s", MODULE_ZNODE, name);

    int rc = ::zoo_create(m_zhdl, path, mid, 
                      strlen(mid), &ZOO_OPEN_ACL_UNSAFE,
                      0, nullptr, 0);
    MOLA_ASSERT(rc == ZOK);

    m_module_cache.insert({std::string(name), id});
  }

  return id;
}

std::string ZookeeperEngine::get_module_name(Module::ID id) {
  for (auto i : m_module_cache) {
    if (i.second == id) return i.first;
  }

  int rc, len;
  char *p, path[MAX_NODE_STR_LEN], mid[MAX_NODE_STR_LEN];
  struct String_vector modules; //FIXME

  std::string parent(MODULE_ZNODE);
  std::string name;

  rc = ::zoo_get_children(m_zhdl, parent.c_str(), 1, &modules);
  
  FOR_EACH_ZNODE(parent.c_str(), path, &modules) {
    p = strrchr(path, '/');
    
    /* fetch the value of the current child node */
    len = MAX_NODE_STR_LEN;
    rc = ::zoo_get(m_zhdl, path, 0, mid, &len, nullptr);
    MOLA_ASSERT(rc == ZOK && len > 0);
    
    mid[len] = 0;
    
    Module::ID _id = std::stoul(mid);
    if (id == _id) {
      name = p+1;
    }
  }

  return name;
}

NodeManager::NodePtr ZookeeperEngine::_find_node(Node::ID nid) {

  MOLA_ASSERT(nid != ActorID::ANY && nid != ActorID::LOCAL);

  int rc, len = MAX_NODE_STR_LEN;
  char path[MAX_NODE_STR_LEN], info[MAX_NODE_STR_LEN];
  
  if (IS_CLIENT_ID(nid)) {
    sprintf(path, "%s/%010d", CLIENT_ZNODE, CLIENTID_TO_ZKID(nid));
  } else {
    sprintf(path, "%s/%010d", NODE_ZNODE, NODEID_TO_ZKID(nid));
  }

  rc = ::zoo_get(m_zhdl, path, 1, info, &len, nullptr);
  if (rc != ZOK) return NodePtr(nullptr);

  MOLA_ASSERT(len > 0);

  info[len] = 0;
  NodePtr node( Node::decode(nid, info, this) );
  return node;
}

void ZookeeperEngine::lock(const std::string& zone, LockID id, 
                           LockCallback callback) {
  LockPtr ilock;
  
  m_lock.lock();
  auto& lock_table = m_lock_zone[zone];
  m_lock.unlock();
  
  {
    utils::LockGuard lk(lock_table.get());
    
    auto itr = lock_table.find(id);
    if (itr == lock_table.end()) {
      ilock.reset(new Lock(zone, id));
      lock_table.insert({id, ilock});
    } else {
      ilock = itr->second;
    }
  }

  auto zk = this;
  auto ilock_ = ilock.get();
  ilock->acquire([=]() mutable {
    zk->zk_lock(ilock_, callback);
  }); 
}

void ZookeeperEngine::unlock(const std::string& zone, LockID id) {
  LockPtr ilock;

  m_lock.lock();
  auto& lock_table = m_lock_zone[zone];
  m_lock.unlock();

  {
    utils::LockGuard lk(lock_table.get());

    auto itr = lock_table.find(id);
    MOLA_ASSERT(itr != lock_table.end());

    ilock = itr->second;

    // try to delete the zookeeper temp lock seq node ..
    int rc = ::zoo_delete(m_zhdl, ilock->m_path.c_str(), -1);
    MOLA_ASSERT(rc == ZOK);
    ilock->m_path = ""; // clear the content
  }

  ilock->release();
}

void ZookeeperEngine::wakeup_internal(const std::string& zone, LockID id) {
  m_lock.lock();
  auto& lock_table = m_lock_zone[zone];
  m_lock.unlock();

  utils::LockGuard lk(lock_table.get());

  auto itr = lock_table.find(id);
  if (itr != lock_table.end()) {
    itr->second->notify();
  }
}


int ZookeeperEngine::get_least_seq(const char *parent, char *least_seq_path) {
  int least_seq = INT_MAX;

  foreach_node(parent, [&](char *path) mutable {
    char *p = strrchr(path, '/');
    int seq = std::stoul(++p);
    if (seq < least_seq)
      least_seq = seq;
  });

  sprintf(least_seq_path, "%s/%010d", parent, least_seq);
  
  // add to the watch list !!
  return ::zoo_exists(m_zhdl, least_seq_path, 1, nullptr);
}

void ZookeeperEngine::zk_lock(Lock *ilock, LockCallback callback) {
  std::string& zone = ilock->m_zone;
  LockID& id = ilock->m_id;

  int rc, len = MAX_NODE_STR_LEN;
  char parent[MAX_NODE_STR_LEN],
       parent_node[MAX_NODE_STR_LEN],
       my_path[MAX_NODE_STR_LEN],
       lowest_seq_path[MAX_NODE_STR_LEN];

  if (ilock->m_path.length() == 0) {
    // create the sub lock zone :
    sprintf(parent_node, "%s/%s", LOCK_ZNODE, zone.c_str());
    rc = ::zoo_create(m_zhdl, parent_node, nullptr, 0,
                      &ZOO_OPEN_ACL_UNSAFE, 0, nullptr, 0);
    MOLA_ASSERT(rc == ZOK || rc == ZNODEEXISTS);
  
    // create the lock id for this zone:
    sprintf(parent_node, "%s/%s/%" __UINT64_FMTu__, LOCK_ZNODE, zone.c_str(), id);
    rc = ::zoo_create(m_zhdl, parent_node, nullptr, 0,
                      &ZOO_OPEN_ACL_UNSAFE, 0, nullptr, 0);
    MOLA_ASSERT(rc == ZOK || rc == ZNODEEXISTS);

    sprintf(parent, "%s/%s/%" __UINT64_FMTu__ "/", LOCK_ZNODE, zone.c_str(), id);
    rc = ::zoo_create(m_zhdl, parent, nullptr, 0,
                      &ZOO_OPEN_ACL_UNSAFE, ZOO_SEQUENCE|ZOO_EPHEMERAL,
                      my_path, MAX_NODE_STR_LEN);
  
    MOLA_ASSERT(rc == ZOK);

    ilock->m_path = my_path;
  } else {
    sprintf(parent_node, "%s/%s/%" __UINT64_FMTu__, LOCK_ZNODE, zone.c_str(), id);
  }

  get_least_seq(parent_node, lowest_seq_path);
    
  if (ilock->m_path == lowest_seq_path) {
    /* I got the lock */
    return callback();
  }

  /* I failed to get the lock */
  auto zk = this;
  ilock->wait([=]() mutable { 
    zk->zk_lock(ilock, callback);
  });
}

int ZookeeperEngine::sync_all_nodes() {
  std::vector<Node::ID> ids;

  foreach_node(NODE_ZNODE, [&](char* path) {
    char * p = strrchr(path, '/');
    Node::ID id = ZKID_TO_NODEID( std::stoul(p+1) );

    if (id != NodeManager::local_node()->get_id()) {
      ids.push_back(id);
    }
  } );

  bool notify = false;

  for (auto i : ids) {
    auto node = _find_node(i);

    if (node.get() != nullptr && 
        update_cached_node(node))
      notify = true;
  }

  // if any node changes, we try to sending the 
  // messages in the delay queue !!
  if (notify) {
    invalid_thread_cache();
    trigger_delay_tasks();
  }

  return ids.size() + 1;
}

int ZookeeperEngine::foreach_node(const char *parent, 
                                  std::function<void(char*)> func) {
  char path[MAX_NODE_STR_LEN], *p;
  struct String_vector strs;

  int rc = ::zoo_get_children(m_zhdl, parent, 1, &strs);
  if (rc != ZOK) return rc;

  FOR_EACH_ZNODE(parent, path, &strs) {
     func(path);
  }
  return ZOK;
}


void ZookeeperEngine::publish_event(const std::string& event) {
  char path[MAX_NODE_STR_LEN], value[MAX_NODE_STR_LEN];
  sprintf(path, "%s/%s", EVENT_ZNODE, event.c_str());

  int rc = ::zoo_create(m_zhdl, path, nullptr, 0,
                        &ZOO_OPEN_ACL_UNSAFE, 0, nullptr, 0);
  if (rc == ZOK || rc == ZNODEEXISTS) {
    time_t curtime = ::time(nullptr);
    ::ctime_r(&curtime, value);

    ::zoo_set(m_zhdl, path, value, strlen(value), -1);
  }
  on_event(event);
}

void ZookeeperEngine::subscribe_event(const std::string& event,
                                      std::function<int()> handler) {
  char path[MAX_NODE_STR_LEN];

  NodeManager::subscribe_event(event, handler);

  sprintf(path, "%s/%s", EVENT_ZNODE, event.c_str());
  ::zoo_exists(m_zhdl, path, 1, nullptr); 
}
