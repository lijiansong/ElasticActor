#ifndef _MOLA_MODULE_H_
#define _MOLA_MODULE_H_ 

#include <map>
#include <vector>
#include <atomic>
#include <climits>

#include "c/mola_module.h"
#include "mola/message.hpp"
#include "mola/spinlock.hpp"
#include "mola/queue.hpp"
#include "mola/node_manager.hpp"

namespace mola {

class Actor;
class StrongActor;
class AbsolateWorker;

class Module {
  friend class BuiltinClient;

  // FIXME : Is it thread-safe for concurrent reading??
  static std::map<uint16_t, Module*> g_module_index;

  static uint32_t parse_number(const std::string& n) {
    if ("MAX" == n) return UINT_MAX;
    return std::stoul(n);
  }

public:
  enum {
    MODULE_OK = 0,
    MODULE_DISPACH_ERR = -1,
    MODULE_SYNCWAIT_ERR = -2,
    MODULE_GATHER_ERR = -3,
  };

  typedef uint16_t ID;
  
  static const ID invalid_id = 0xffff;

  /** loading a module, the filename is name of the .so file */
  static Module *dynload_module(const char* filename, 
                                std::vector<std::string>& argv);

  /** unloading a module */
  static void unload_module(Module *module);

  /** find a local module by its id */
  static Module* find_module(ID id) {
    auto itr = g_module_index.find(id);
    if (itr != g_module_index.end())
      return itr->second;
    return nullptr;
  }

  ActorAddr* search_location_cache(ActorID id);
  ActorAddr* search_location(ActorID id);
  void insert_location_cache(ActorID id, ActorAddr* addr);
  // ------------------------------------------------------- //

  /* Belowing two functions are called by Dispatcher when
   * `SendMessage`, `SendRecvMessage` or `SendMessageToCluster` are invoked.
   * The caller of the Send* APIs only know the name of the module but 
   * has NO idea of the its location. 
   */ 
  int dispatch(uint32_t inst_id, uint32_t intf_id,
               MessageSafePtr& message,
               AbsolateWorker* worker = nullptr);

  int dispatch(MessageSafePtr& message, AbsolateWorker* worker = nullptr);

  /* Called by the active instance on receiving a message. */
  int invoke_message(Actor* job, 
                     AbsolateWorker* worker,
                     MessageSafePtr message) throw (int);
  
  /* Check whether the module on this node is overloaded */
  bool is_overload() const;

  void on_add_instance(Actor* inst);
  void on_remove_instance(Actor* inst);

  ID get_id() const { return m_id; }
  
  const char* get_name() const {
    return m_module_spec->name;
  }

  ::mola_module_type_t get_type() const {
    return m_module_spec->type;
  }

  bool is_strong() const {
    return m_module_spec->type & MOLA_MODULE_TYPE_STRONG;
  } 

  bool is_singleton() const {
    return m_singleton;
  }

  void set_singleton(bool singleton = true) {
    m_singleton = singleton;
    if (singleton) {
      m_module_spec->max_instances = 1; // !!
    }
  }

  bool is_autostart() const {
    return m_autostart;
  }

  void set_autostart(bool autostart = true) {
    m_autostart = autostart;
  }

  bool is_private() const {
    return m_private;
  }

  void set_private(bool prv = true) {
    m_private = prv;
  }

  uint32_t get_max_instances() const {
    return m_module_spec->max_instances;
  }

  void set_max_instances(uint32_t max) {
    m_module_spec->max_instances = max;
  }

  size_t get_data_size() const {
    return m_module_spec->size;
  }

  size_t get_intf_num() const {
    return m_module_spec->intf_num;
  }

  const mola_intf_t* get_intf_tbl() const {
    return m_module_spec->intf_tbl;
  }

  mola_init_cb_t get_init() const {
    return m_module_spec->init;
  }

  mola_fini_cb_t get_fini() const {
    return m_module_spec->fini;
  }

  bool become_idle(Station *actor) {
    // TODO : If there are too many idle instances, 
    //        donot enqueue and just return false.
    // m_idle_que.enqueue(actor);
    return true;
  }

  uint32_t get_threshold() const { return m_threshold; }
  void set_threshold(uint32_t threshold) {
    m_threshold = threshold;
  }
  
  void init_instance(Station*) const throw();

protected:
  // Fetch all avaliable instances, may only be used by network APIs
  std::vector<Station*> get_all_instances();

private:
  /* The Module can only be created on dynamic loading 
   * by calling the function `Module::dynload_module(const char* name)` */
  Module(ID id,
         mola_module_handle_t handle,
         mola_module_spec_t spec)
    : m_id(id)
    , m_module_hd(handle)
    , m_module_spec(spec)
    , m_threshold(1)
    , m_private(spec->type & MOLA_MODULE_TYPE_PRIVATE)
    , m_autostart(spec->type & MOLA_MODULE_TYPE_AUTOSTART)
    , m_singleton(spec->type & MOLA_MODULE_TYPE_SINGLETON)
    , m_id_generator(0)
    , m_instance(nullptr) {
    auto nmgr = NodeManager::instance();
    node_id = nmgr->local_node()->get_id();
  }

  ~Module() {
    unload_module(this);
  }

  Station* create_instance();
  Station* create_instance(uint32_t);

  /* select an instance to handle the incoming messages,
   * if there're no thus one or all active ones are busy,
   * try to create a new one to handle this message. */ 
  Actor* select_or_create_actor_instance(uint32_t inst_id);
  
  /* Module id on current node */
  ID m_id;

  uint32_t node_id;
  /* runtime module specific */
  mola_module_spec_t m_module_spec;
  /* module handler for libdl */
  mola_module_handle_t m_module_hd;
  /* max number of messages to process when instance is running */
  uint32_t m_threshold;
  size_t default_actor_num = 1024;

  /* configuable params */
  bool m_private:1;
  bool m_autostart:1;
  bool m_singleton:1;

  std::atomic<uint32_t> m_id_generator;
  std::map<uint32_t, Station*> m_inst_map;
  int location_cache_size = 1024;
  std::map<ActorID, ActorAddr*> m_location_cache;
  std::map<ActorID, ActorAddr> m_location;

  Station* m_instance;

  mutable utils::SpinLock m_lock;
  utils::ThreadSafeQueue<Station> m_idle_que;
};

}

#endif // _MOLA_MODULE_H_
