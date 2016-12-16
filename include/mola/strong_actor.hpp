/*
 Project MOLA Runtime System version 0.1

 Copyrigth (c) Institute of Computing Technology Chinese Academy of Sciences.

 All rights reserved.

 */

#ifndef _MOLA_STRONG_ACTOR_H_
#define _MOLA_STRONG_ACTOR_H_

#include <cstring>
#include <deque>

#include "c/support.h"
#include "mola/spinlock.hpp"
#include "mola/actor.hpp"
#include "mola/node_manager.hpp"

namespace mola {

class StrongActor : public Actor {
 public:
  class ThreadState {
   public: 
    ThreadState() {
      ::memset(&m_context, 0, sizeof(m_context));
    }
    
    MOLA_UCTX_T* get_context() {
      return &m_context;
    }

    const MOLA_UCTX_T* get_context() const {
      return &m_context;
    }

   protected:
    MOLA_UCTX_T m_context;
  };
  
  class Fiber : public ThreadState {
    constexpr static uint32_t STACK_SIZE = 16 << 10; // 16KB

    typedef void (*fiber_entry_t)();

    static uint32_t HIGH(void *p) {
      return (uint32_t)(((uintptr_t)p) >> 32);
    }
    
    static uint32_t LOW(void *p) {
      return (uint32_t)(((uintptr_t)p) & 0x0ffffffffULL);
    }

    static void* MERGE(uint32_t low, uint32_t high) {
      uintptr_t m = high;
      m = (m << 32) | low;
      return (void*)m;
    }

    static void spawn(int low, int high) {
      auto fiber = reinterpret_cast<Fiber*>(MERGE(low, high));
      MOLA_ASSERT(fiber != nullptr);

      fiber->set_state(RUNNING);
      fiber->m_retval = fiber->m_entry(fiber->m_input, fiber->m_ctx);
      fiber->m_status = DEAD;

      auto actor = Actor::current<StrongActor>();
      MOLA_ASSERT(actor != nullptr);

      auto ts = actor->get_thread_state();
      MOLA_SET_UCTX(ts.get_context());
    }

   public:
    enum State {
      INIT = 0,
      RUNNING,
      SUSPEND,
      DEAD,
      EXIT,
    };
    
    Fiber(uint32_t id, size_t size = STACK_SIZE)
      : m_id(id), m_ssize(size), m_stack(nullptr)
      , m_retval(nullptr), m_status(INIT) {

      MOLA_ASSERT(size > 0);
      m_stack = new uint64_t[size >> 3];
    }

    virtual ~Fiber() { 
      if (m_stack != nullptr)
        delete [] m_stack; 
    }

    void init(mola_intf_cb_t entry, void *input, void *ctx) {
      m_entry = entry;
      m_input = input;
      m_ctx = ctx;

      MOLA_GET_UCTX(&m_context);
      MOLA_UCTX_SET_STACK(&m_context, m_stack, m_ssize);
      MOLA_MAKE_UCTX(&m_context, (fiber_entry_t)Fiber::spawn, 
                   2, LOW(this), HIGH(this));
    }

    void* get_retval() const { return m_retval; }
    void set_retval(void *rv) { m_retval = rv; }

    uint32_t get_id() const { return m_id; }

    bool is_init() const {
      return (m_status == INIT);
    }

    bool is_running() const { 
      return (m_status == RUNNING); 
    }

    bool is_suspend() const {
      return (m_status == SUSPEND);
    }

    bool is_dead() const {
      return (m_status == DEAD);
    }

    bool is_exit() const {
      return (m_status == EXIT);
    }

    void set_state(State state) {
      m_status = state;
    }

    State get_state() const {
      return m_status;
    }

   private:
    uint32_t m_id;
    size_t m_ssize;
    uint64_t *m_stack;
    mola_intf_cb_t m_entry;
    void *m_input;
    void *m_ctx;
    void *m_retval;
    State m_status;
  };

  class FiberReqCtx : public RequestContext {
   public:
    FiberReqCtx(uint32_t reqid, MessageSafePtr& initiator,
                void *ctx, Fiber *fiber) 
      : RequestContext(reqid, initiator, ctx), m_fiber(fiber) {}
    
    Fiber* get_fiber() const { return m_fiber.get(); }
    void set_fiber(Fiber* fiber) { m_fiber.reset(fiber); }

    bool done() const override {
      MOLA_ASSERT(m_fiber.get() != nullptr);
      return m_fiber->is_dead();
    }
   
   private:
    std::unique_ptr<Fiber> m_fiber;
  };

  static void *call_intf_on_fiber(mola_intf_cb_t intf,
                                  void *input, void *ctx) throw (int) {
    auto actor = Actor::current<StrongActor>();
    MOLA_ASSERT(actor != nullptr);

    auto fiber = actor->get_or_create_fiber(intf, input, ctx);
    MOLA_ASSERT(fiber != nullptr && "Canrot get or create fiber context!");

    MOLA_GET_UCTX(actor->m_thread_state.get_context());

    if (fiber->is_init() || fiber->is_suspend()) {
      actor->m_current_fiber = fiber;
      MOLA_SET_UCTX(fiber->get_context());

      MOLA_FATAL_TRACE("cannot return here, fiber state is " << fiber->get_state());
    }  

    // return from the fiber context ..
    if (fiber->is_dead()) {
      void *ret = fiber->get_retval();
      
      // if the fiber's request context is not current one,
      // we need delete it here !!
      if (fiber->get_id() != actor->get_current_reqid()) {
        actor->delete_req_ctx(fiber->get_id());
      }
      
      return ret;
    }

    // if user call the InstanceExit() ..
    if (fiber->is_exit()) {
      int rc = *( reinterpret_cast<int*>(fiber->get_retval()) );
      actor->Actor::exit(rc);

      MOLA_ASSERT(0 && "cannot get here after calling the exit!");
    }

    fiber->set_state(Fiber::SUSPEND);
    return nullptr;
  }
 
  StrongActor(ActorID id, Module *module) 
    : Actor(id, module)
    , m_current_fiber(nullptr) {
  }

  virtual ~StrongActor() {}
 
  Fiber* get_or_create_fiber(mola_intf_cb_t entry, void *input, void *ctx) {
    auto rc = get_current_ctx();
    auto frc = dynamic_cast<FiberReqCtx*>(rc);
    Fiber *fiber = nullptr;

    if (frc == nullptr) {
      uint32_t reqid = new_reqid();
      fiber = new Fiber(reqid);
      
      MessageSafePtr initiator = get_current_message();
      void *context = nullptr;
      if (rc != nullptr) {
        initiator = rc->get_initiator();
        context = rc->get_ctx();
      }
      
      frc = new FiberReqCtx(reqid, initiator, ctx, fiber);
      insert_req_ctx(frc);

      fiber->init(entry, input, ctx);
    } else {
      fiber = frc->get_fiber();
      MOLA_ASSERT(fiber != nullptr);
      
      fiber->set_retval(input);
    }

    return fiber;
  }

  uint32_t get_fiber_reqid() const {
    if (m_current_fiber != nullptr)
      return m_current_fiber->get_id();
    return 0;
  }

  Fiber* get_current_fiber() const {
    return m_current_fiber;
  }

  void set_current_fiber(Fiber *fiber) {
    m_current_fiber = fiber;
  }

  ThreadState& get_thread_state() {
    return m_thread_state;
  }

  const ThreadState& get_thread_state() const {
    return m_thread_state;
  }

  void* wait_response() {
    MOLA_ASSERT(m_current_fiber != nullptr);

    MOLA_GET_UCTX(m_current_fiber->get_context());

    if (! m_current_fiber->is_suspend()) {
      m_current_fiber = nullptr;
      MOLA_SET_UCTX(m_thread_state.get_context());
      
      MOLA_FATAL_TRACE("cannot return here !!");
    }

    //  resumed by runtime 
    MOLA_ASSERT(m_current_fiber != nullptr);
    m_current_fiber->set_state(Fiber::RUNNING);

    void* res = m_current_fiber->get_retval();
    m_current_fiber->set_retval(nullptr);
    
    return res; 
  }

  virtual void exit(int rc) throw (int) {
    if (m_current_fiber != nullptr) {
      // FIXME : is it safe to leave rc on fiber's stackframe ?
      m_current_fiber->set_retval(& rc);
      m_current_fiber = nullptr;
      MOLA_SET_UCTX(m_thread_state.get_context());

      MOLA_FATAL_TRACE("cannot return here !!");
    }
    
    Actor::exit(rc);
  }

 private:
  Fiber* m_current_fiber;
  ThreadState m_thread_state; 
};

}

#endif // _MOLA_STRONG_ACTOR_H_
