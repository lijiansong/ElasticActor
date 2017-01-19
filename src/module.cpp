#include <dlfcn.h>
#include <stdlib.h>

#include "mola/actor.hpp"
#include "mola/strong_actor.hpp"
#include "mola/module.hpp"
#include "mola/node_manager.hpp"
#include "mola/scheduler.hpp"
#include "mola/dispatcher.hpp"

using namespace mola;

// define the static data of Module class
std::map<uint16_t, Module*> Module::g_module_index; 

Module* Module::dynload_module(const char* filename,
                               std::vector<std::string>& argv) {
  ::mola_module_handle_t hd = ::dlopen(filename, RTLD_LAZY);
  if (hd == nullptr) {
    // TODO: handle the error case !!
    MOLA_ERROR_TRACE("cannnot load the module file, " << ::dlerror());
    return nullptr;
  }

  ::dlerror(); // clean all errors 

  mola_module_spec_t spec = 
    reinterpret_cast<mola_module_spec_t>(::dlsym(hd, "__module_spec"));

  if (::dlerror() != nullptr) {
    // TODO: handle the error case !!
    MOLA_ERROR_TRACE("error when loading module file, " << ::dlerror());
    ::dlclose(hd);
    return nullptr;
  }

  auto mgr = NodeManager::instance();
  // Get or create a module id by name and version 
  auto id = mgr->get_module_id(spec->name, true);

  MOLA_ALTER_TRACE("Get module \"" << spec->name << "\" with id " << id);

  // FIXME : ugly code, find a better way to replace it !! 
  Module *module = new Module(id, hd, spec);
  g_module_index.emplace(id, module);
  
  // set the default values 
  if (module->get_max_instances() == 0)
    module->set_max_instances(2 * WorkerGroup::instance()->get_workers_num());
  if (module->get_threshold() == 0)
    module->set_threshold(32);

  // check the user arguments and fix the module spec
  for (int i = 0; i < argv.size(); ++i) {
    if (argv[i] == "autostart") {
      module->set_autostart();
    } else if (argv[i] == "singleton") {
      module->set_singleton();
    } else if (argv[i] == "private") {
      module->set_private();
    } else if (argv[i] == "public") {
      module->set_private(false);
    } else if (argv[i] == "max") {
      MOLA_ASSERT(i < argv.size()-1);
      uint32_t max = parse_number(argv[++i]);
      MOLA_ASSERT(max > 0 && max < 0x100000UL);
      
      if (max == 1)
        module->set_singleton();
      else
        module->set_max_instances(max);
    } else if (argv[i] == "threshold") {
      MOLA_ASSERT(i < argv.size()-1);
      uint32_t threshold = parse_number(argv[++i]);
      MOLA_ASSERT(threshold > 0);

      module->set_threshold(threshold);
    } else {
      MOLA_FATAL_TRACE("unknown argument \"" << argv[i] 
                      << "\" for \"Module::dynload_module\" .. ");
    }
  }
  
  // publish this module to global scope
  mgr->register_module(id, spec->name, !module->is_private());

  // if the module is auto started,
  // create a new instance and add it to ready tasks queue!!
  if (module->is_autostart()) {
    WorkerGroup *wg = WorkerGroup::instance();
    // start all instances of this module after loading ..
    for (int i = 0; i < module->get_max_instances(); ++i) {
      auto actor = module->create_instance();
      MOLA_ASSERT(actor != nullptr);
      /* add the new instance to the global task queue,
       * note the schduler will start after all modules loaded!! */
      wg->enqueue(actor);
    }
  }

  return module;
}

void Module::unload_module(Module *module) {
  MOLA_ASSERT(module != nullptr && module->m_module_hd != nullptr);
  ::dlclose(module->m_module_hd);
  module->m_module_hd = nullptr;
  module->m_module_spec = nullptr;
}


int Module::dispatch(MessageSafePtr& message, 
                     AbsolateWorker* worker) {
  MOLA_ASSERT(!message.is_nil());
  return dispatch(message->get_target().instance_id(),
                  message->get_target().intf_id(), 
                  message, worker);
}

int Module::dispatch(uint32_t inst_id,
                     uint32_t intf_id,
                     MessageSafePtr& message,
                     AbsolateWorker* worker) {
  //MOLA_ASSERT(inst_id != 0 && !message.is_nil());
  MOLA_ASSERT(!message.is_nil());

  Actor *actor = nullptr;

  // changed by ZHAO Peng, don't bind actors
  
  // find from the per-thread cache first, no lock required
  //if (worker != nullptr) {
    //uint32_t key = (m_id << 16) | inst_id;
    //actor = worker->find_cached_actor(key); 
    //if (actor == nullptr) {
      // find or create a actor instance to handle this message
      //actor = select_or_create_instance(inst_id);
      //MOLA_ASSERT(actor != nullptr);
      //worker->cache_an_actor(key, actor);
      //}
    //} else {
    // find or create a actor instance to handle this message
  actor = select_or_create_instance(inst_id);
  MOLA_ASSERT(actor != nullptr);
    //}

  MOLA_LOG_TRACE("deliver a message to actor "
               << actor->get_id().to_string());

  ActorID dest = actor->get_id();
  dest.set_intf_id(intf_id);
  message->set_target(dest);

  if (!actor->deliver_message(message, worker)) {
    MOLA_ERROR_TRACE("deliver error for module " << get_name());
    return MODULE_DISPACH_ERR;
  }
  return MODULE_OK;
}

int Module::invoke_message(Actor *actor,
                           AbsolateWorker *worker,
                           MessageSafePtr message) throw(int) {

  auto id = message->get_target();
  auto intf_id = id.intf_id();
  
  MOLA_ASSERT(intf_id < get_intf_num());

  auto callback = get_intf_tbl()[intf_id].callback;
  auto intf_type = get_intf_tbl()[intf_id].type;

  MOLA_ASSERT (callback != NULL && intf_type != MOLA_INTF_TYPE_INVALID);
  
  // the request context, not null if this message 
  // is a response or a gather message
  auto req_ctx = actor->get_current_ctx();
 
  // check if the current req ctx is gather ctx
  auto gather_group = dynamic_cast<GatherGroup*>(req_ctx);
  
  if (gather_group != nullptr) {
    if (message->is_response()) {
      // if the message is returned by an asynchronized way,
      // using 'SendMsgToInst' with a reqid, the reply's type
      // may be wrong, we fix it here, since it is easy to implement.
      message->set_gather();
    }
    MOLA_ASSERT(message->is_gather());
  } else {
    MOLA_ASSERT(!message->is_gather());
  }

  // call the module interface implement callback
  void *ret = nullptr;
  if (intf_type == MOLA_INTF_TYPE_SYNC &&
      Actor::current<StrongActor>() != nullptr) {
    // NOTE: we call the interface on a fiber context,
    //       ONLY IF the actor is strong one AND this interface is sync one!!
    ret = StrongActor::call_intf_on_fiber(
            callback, message.get(), reinterpret_cast<void*>(gather_group));
  } else {
    ret = callback(message.get(), reinterpret_cast<void*>(gather_group));
  }

  MessageSafePtr response(reinterpret_cast<mola_message_t*>(ret));

  // reply the response to the sender
  if (!response.is_nil()) {
    // if this is a response message, we should got the request context
    // and return the message back to the initiator !!
    if (message->is_response()) {
      MOLA_ASSERT(req_ctx != nullptr);
      message = req_ctx->get_initiator();
    }
    
    if (message->is_scatter()) {
      // the response of a scatter must be a gather !!
      response->set_gather();
    } else {
      // FIXME : is it ok if this is a gather message ??
      response->set_response(); // set as a response message!!
    }

    response->set_reqid(message->get_reqid());
    
    response->set_sender(actor);
    response->set_target(message->get_sender());

    Dispatcher::dispatch(response);
  }
  
  return MODULE_OK;
}

ActorAddr* Module::search_location_cache(ActorID id) {
  auto loc = m_location_cache.find(id);	
  if (loc == m_location_cache.end())
    return nullptr;
  return loc.second;
}

ActorAddr* Module::search_location(ActorID id) {
  auto loc = m_location.find(id);
  if (loc == m_location.end())
    return nullptr;
  insert_location_cache(id, &(loc.second));
  return &(loc.second);
}

void insert_location_cache(ActorID id, ActorAddr* addr) {
  m_location_cache.emplcae(id, addr);
  // replacement alg
  if (m_location_cache.size() > location_cache_size) { 
   //TODO 
  }
}

Station* Module::create_instance() {
  // Can not create more ..
  if (m_id_generator.load() >= get_max_instances()) {
/*    MOLA_ERROR_TRACE("module \"" << get_name()
                    << "\" cannot create instances more than "
                    << get_max_instances() );*/
    return nullptr;
  }
  // Since the stateless worker is created in the dispatch stage,
  // the requeset here must be a instance with its state !!
  ActorID new_id(m_id, ActorID::ANY, m_id_generator++);
  Actor *infant = is_strong()
                    ? new StrongActor(new_id, this)
		    : new Actor(new_id, this);

  m_inst_map.emplace(new_id.instance_id(), infant);
  
  // Allocate the private data for this actor
  if (get_data_size() > 0) {
    void* data_ptr = ::calloc(sizeof(char) * get_data_size());
    infant->set_data_ptr(data_ptr);
  }

  if (is_singleton()) {
    MOLA_ASSERT(m_id_generator.load() == 1);
    m_instance = infant;
  }

  return infant;
}
Actor* Module::create_instance(uint32_t inst_id) {
  // Can not create more ..
  // Since the stateless worker is created in the dispatch stage,
  // the requeset here must be a instance with its state !!
  ActorID new_id(node_id, m_id, ActorID::ANY, inst_id);
  Actor *infant = is_strong()
                    ? new StrongActor(new_id, this)
                    : new Actor(new_id, this);

  m_inst_map.emplace(new_id.instance_id(), infant);
  
  // Allocate the private data for this actor
  if (get_data_size() > 0) {
    void* data_ptr = ::calloc(sizeof(char) * get_data_size(), 1);
    infant->set_data_ptr(data_ptr);
  }

  return infant;
}

Actor* Module::select_or_create_actor_instance(uint32_t inst_id) {
  
  utils::LockGuard lock(m_lock);
  Actor *actor = nullptr;
  /*
  if (is_singleton()) {
    // TODO: find the single instance and return ..
    if (is_singleton()) {
      if (m_instance == nullptr)
        m_instance = create_instance();
      return m_instance;
    }
*/
    auto ans = m_inst_map.find(inst_id);
    if (ans != m_inst_map.end()) {
      return ans->second;
    } else {
      // don't report error and create a new instance because we enable users manipulate actor id directly
      //TODO : report the error, since the state may loss
      //       because the given actor instance is reclaimed..
      // MOLA_ASSERT(0);
      actor = create_instance(inst_id);
      assert(actor);
      return actor;
    }
}

std::vector<Station*> Module::get_all_instances() {
  utils::LockGuard lock(m_lock);
  
  // FIXME : create all instances if the module is not auto-started.
  while (create_instance() != nullptr);

  std::vector<Station*> instances;
  for (auto itr = m_inst_map.begin(), end = m_inst_map.end();
       itr != end; ++itr) {
    instances.emplace_back(itr->second);
  }
  return instances;
}

void Module::on_add_instance(Actor* inst) {
}

void Module::on_remove_instance(Actor* inst) {
  utils::LockGuard lock(m_lock);
  ActorID id = inst->get_id();
  m_inst_map.erase(id.instance_id());
}

bool Module::is_overload() const {
  return false;
#if 0
  utils::LockGuard lock(m_lock);
  // FIXME : how to decide the module is overload?
  return !is_singleton() && m_idle_que.empty() && 
          (m_inst_map.size() > get_max_instances());
#endif
}

void Module::init_instance(Actor * actor) const throw() {
  if (m_module_spec->init != nullptr) {
    m_module_spec->init();
  }
}
