/*
 Project MOLA Runtime System version 0.1

 Copyrigth (c) Institute of Computing Technology Chinese Academy of Sciences.

 All rights reserved.

 */

#ifndef _MOLA_DISPATCHER_H_
#define _MOLA_DISPATCHER_H_

#include "c/mola_message.h"
#include "c/mola_module.h"

#include "mola/message.hpp"
#include "mola/load_balancer.hpp"

extern "C" {
  int SendMsgToIntf(MODULE_ID_T, INTF_ID_T, INST_ID_T, 
                    void* msg, INTF_ID_T, void* ctx);

  int SendMsgToIntfAndRecv(MODULE_ID_T, INTF_ID_T, INST_ID_T,
                           void *, void **);

  int ScatterToIntf(MODULE_ID_T, INTF_ID_T, size_t,
                    INST_ID_T dest[], void* mg[], 
                    INTF_ID_T, void *ctx);

  void SwitchToGatherCtx(void*, mola_gather_cb_t);
  void InsertToGatherCtx(void*, void*);

  void Lock(const char*, uint64_t, void*, uint32_t);
  void UnLock(const char*, uint64_t);

  RID_T SaveReturnState_(void*);
  int ReplyMsg(RID_T rid, void *msg);

  EVENT_T CreateEvent();
  void* WaitForNotify();
  int NotifyEvent(EVENT_T event, void *data);

#if !defined(CLIENT_CONTEXT)
  void Subscribe(const char*, void*, uint32_t);
#endif // CLIENT_CONTEXT
}

namespace mola {

class Dispatcher {
  using task_t = LoadBalanceStrategy::task_t;

  static std::unique_ptr<LoadBalanceStrategy> lbs;
  
public:
  enum {
    RANDOM_BALANCER = 0,
    /* TODO */
  };

  /* using the system pre-defined strategy */
  static void initialize(int type = RANDOM_BALANCER);

  /* using the user defined strategy */
  static void initialize(LoadBalanceStrategy* strategy) {
    lbs.reset(strategy);
  }

  static LoadBalanceStrategy* load_balance_strategy() {
    return lbs.get();
  }

  enum {
    DISPATCHER_OK = 0,
    DISPATCHER_EBUSY,
    // TODO 
  };

  static int dispatch(MessageSafePtr& message); 

  static int dispatch(ActorAddr& target,
                      MessageSafePtr& message,
                      uint32_t return_intf = ActorID::ANY,
                      void *ctx = nullptr);

  static int dispatch(ScatterGroup& mg,
                      uint32_t gather_intf = ActorID::ANY,
                      bool directional = false);
};

}

#endif // _MOLA_DISPATCHER_H_

