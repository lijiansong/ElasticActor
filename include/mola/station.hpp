#ifndef _MOLA_STATION_H_
#define _MOLA_STATION_H_

#include "mola/mailbox.hpp"
#include "mola/message.hpp"
#include "mola/module.hpp"
#include "mola/scheduler.hpp"

namespace mola{

class Station : public WorkItem {
  friend class Module;

protected:
  Station(StationID id, Module * module)
    : WorkItem(id), m_module(module)
    , m_status(STATION_STARTUP)
    , m_data_ptr(nullptr)
    , m_reqid_generator(0)
    , m_current_ctx(nullptr) {
    m_module->on_add_instance(this);
    }
  virtual ~Station() {
    for (auto &item : m_req_ctx_map) {
      delete item.second;
    }
    m_module->on_remove_instance(this);
  } 

private:
  void set_data_ptr(void *data_ptr) { m_data_ptr = data_ptr; }

  struct MsgLinkNode {
    MessageSafePtr value;
    MsgLinkNode* next;

    MsgLinkNode(MessageSafePtr& _v, MsgLinkNode *_n = nullptr)
      : value(_v), next(_n) {}
  };

  struct GetMsgPriority {
    int operator () (const MsgLinkNode* node) {
      if (node->value->is_response() ||
          node->value->is_gather()) return 0;
      return 1;
    }
  };

public:
  static const uint32_t INVALID_REQID = 0;

  typedef std::pair<ExecStatus, int> ExitStatus;

  /* get the current actor, by scheduler-context only */
  template <class T = Actor>
  static T* current() {
    auto worker = AbsolateWorker::get_worker();
    if (worker == nullptr) 
      return nullptr;
    return dynamic_cast<T*>(worker->get_current());
  }

  /* exit call for actor context, maybe just for debug .. */
  virtual void exit(int rc) throw (int) {
    throw rc;
  }

  virtual ExecStatus execute(Worker* worker) { 
    // before resuming this actor,
    // try to set the private data pointer before processing.
    worker->before_resume(m_data_ptr);

    ExecStatus status = RESUMABLE;
    
    try {
      /* call the actor 's init callback for first schedule */
      if (m_status == ACTOR_STARTUP) {
        m_module->init_instance(this);
        m_status = ACTOR_RUNNING;
      }

      auto threshold = m_module->get_threshold();

      for (unsigned i = 1; i <= threshold; ++i) {
        get_message(m_current_message);

        if (m_current_message.is_nil()) break;
        
        // set the requset context for current callback!!
        uint32_t reqid = 0;
        set_current_ctx();
        
        if ( m_current_message->is_response() ||
             m_current_message->is_gather() ) {
          --i; // let the responses run as many as possible !!
          if (reqid = m_current_message->get_reqid())
            set_current_ctx(reqid);   
        } 

        // try to invoke this message, since the interfaces
        // to processing the messages is belong to a module,
        // so the InvokeMessage is implemented as a methord
        // call of the Module class !!
        m_module->invoke_message(this, worker, m_current_message);
        // reset the temp message ref ..
        m_current_message.drop();
        
        // delete the request context if need
        if (m_current_ctx != nullptr) {
          delete_req_ctx(m_current_ctx);
        }
      }

      // for auto-started actors, the mailbox must be blocked
      // when it got here first time, so we must test if the 
      // mailbox is blocked here !!
      if (m_mbox.is_blocked() || m_mbox.try_block()) {
        status = BLOCKED;
        m_module->become_idle(this);
      }

    } catch (int rc) {
      // TODO : how to use `rc' ?
      status = EXIT;
    }

    if (status == EXIT) {
      m_mbox.close();
      return EXIT; // cannot got here !!
    }
    
    return status;
  }


  Module *get_module() const { return m_module; }
  void *get_data_ptr() const { return m_data_ptr; }

  uint32_t new_reqid() {
    if (m_reqid_recycle.size() > 0) {
      auto reqid = m_reqid_recycle.back();
      m_reqid_recycle.pop_back();
      return reqid;
    }
    return ++m_reqid_generator;
  }

  void release_reqid(uint32_t reqid) { 
    m_reqid_recycle.push_back(reqid);
  }

  RequestContext* find_req_ctx(uint32_t reqid) const {
    auto i = m_req_ctx_map.find(reqid);
    if (i != m_req_ctx_map.end()) return i->second;
    return nullptr;
  }

  void insert_req_ctx(RequestContext* ctx) {
    uint32_t reqid = ctx->get_reqid();
    MOLA_ASSERT(nullptr == find_req_ctx(reqid));
    m_req_ctx_map.insert({reqid, ctx});
  }

  void delete_req_ctx(RequestContext* ctx) {
    if (!ctx->done()) 
      return; 

    release_reqid(ctx->get_reqid());
    m_req_ctx_map.erase(ctx->get_reqid());
    delete ctx;
  }

  void delete_req_ctx(uint32_t reqid) {
    if (auto req_ctx = find_req_ctx(reqid))
      delete_req_ctx(req_ctx);
  }

  /* called by Dispatcher when Scatter operation done successfully */
  void on_scatter_done(ScatterGroup* mg, void* ctx = nullptr) {
    // FIXME : keep the whole message is not necessary,
    //         since only the metadata will be used later.
    //         maybe we can copy the meta and create a message
    //         reference without user-data...
    GatherGroup *group;

    if (m_current_ctx != nullptr)
      group = new GatherGroup(mg->get_reqid(), mg->size(),
                              m_current_ctx->get_initiator(), ctx);
    else
      group = new GatherGroup(mg->get_reqid(), mg->size(), 
                              m_current_message, ctx);
    
    insert_req_ctx(group);
  }

  void on_async_send_done(MessageSafePtr& message, void *ctx = nullptr) {
    RequestContext *rc;
    if (m_current_ctx != nullptr)
      rc = new RequestContext(message->get_reqid(),
                              m_current_ctx->get_initiator(), ctx);
    else
      rc = new RequestContext(message->get_reqid(),
                              m_current_message, ctx); 

    insert_req_ctx(rc);
  }

  RequestContext* save_return_state(void *ctx = nullptr) {
    RequestContext *rc;
    if (m_current_ctx != nullptr)
      rc = new RequestContext(0, m_current_ctx->get_initiator(), ctx);
    else
      rc = new RequestContext(0, m_current_message, ctx);
    return rc;
  }

  MessageSafePtr& get_current_message() { 
    return m_current_message; 
  }

  const MessageSafePtr& get_current_message() const {
    return m_current_message;
  }

  void set_current_message(MessageSafePtr& message) {
    m_current_message = message;
  }

  RequestContext* get_current_ctx() {
    return m_current_ctx;
  }

  void set_current_ctx(RequestContext* ctx = nullptr) {
    m_current_ctx = ctx;
  }

  void set_current_ctx(uint32_t reqid) {
    return set_current_ctx(find_req_ctx(reqid));
  }

  /* send a message to an actor.
   * this must be called by methords of Module class !! */ 
  bool deliver_message(MessageSafePtr& message,
                       AbsolateWorker* worker = nullptr) {
    MsgLinkNode* item = new MsgLinkNode(message);
    auto ret = m_mbox.enqueue(item);
    
    if (ret == utils::MAILBOX_FAILURE) {
      // TODO : handle the error case
      MOLA_FATAL_TRACE("cannot push message into the mailbox of actor "
                     << std::hex << get_id().u64() << std::dec);
      return false;
    }

    // try to resume this actor instance!!
    if (ret == utils::MAILBOX_UNBLOCKED) {
      WorkerGroup::instance()->enqueue(worker, this);
    }

    return true;
  }

  Actor* find_cached_actor(uint32_t key) const {
    auto itr = m_actor_map.find(key);
    if (itr != m_actor_map.end()) return itr->second;
    return nullptr;
  }

  Actor* find_cached_actor(const ActorID& id) const {
    return find_cached_actor(id.hash_key());
  }

  void cache_an_actor(Actor *actor) {
    m_actor_map.emplace(actor->get_id().hash_key(), actor);
  }

  void cache_an_actor(uint32_t key, Actor *actor) {
    m_actor_map.emplace(key, actor);
  }

  uint32_t get_current_reqid() const {
    if (m_current_ctx != nullptr) 
      return m_current_ctx->get_reqid();
    return 0;
  }
 
protected:
  enum State {
    ACTOR_STARTUP = 0,
    ACTOR_RUNNING = 1,
    ACTOR_SUSPEND = 2,
    // TODO ..
  };
  
  virtual void get_message(MessageSafePtr& message) {
    std::unique_ptr<MsgLinkNode> item( m_mbox.dequeue() );
    if (item != nullptr) {
      message = std::move(item->value);
    }
  }

  /* the status may be accessed by child classes */
  State m_status;

  utils::Mailbox<MsgLinkNode, 2, GetMsgPriority> m_mbox;
 
private:
  Module *m_module;
  void *m_data_ptr;
  
  // ---- for saved states ----
  RequestContext *m_current_ctx;
  std::map<uint32_t, RequestContext*> m_req_ctx_map;

  // ---- for actor local reqid allocation ----
  std::vector<uint32_t> m_reqid_recycle;
  uint32_t m_reqid_generator;

  // ---- temp ref of current processing message --
  MessageSafePtr m_current_message;
  
  // ---- cache the instances of each module ---- 
  std::map<uint32_t, Actor*> m_actor_map;
};

};

} // namespace mola
