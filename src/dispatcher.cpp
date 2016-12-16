#include <iostream>
#include "mola/dispatcher.hpp"
#include "mola/scheduler.hpp"
#include "mola/module.hpp"
#include "mola/node_manager.hpp"
#include "mola/load_balancer.hpp"
#include "mola/actor.hpp"
#include "mola/strong_actor.hpp"

using namespace mola;

std::unique_ptr<LoadBalanceStrategy> Dispatcher::lbs;

void Dispatcher::initialize(int type) {
  LoadBalanceStrategy *ss;

  switch (type) {
  case RANDOM_BALANCER:
    ss = new RandomStrategy();
    break;
  /* TODO */
  default:
    MOLA_ASSERT(0);
    break;
  }

  lbs.reset( ss );
}

int Dispatcher::dispatch(MessageSafePtr& message) {
  auto worker = AbsolateWorker::get_current_worker();
  auto mgr = NodeManager::instance();
  auto& target = message->get_target();

  if (target.is_local()) {
    auto& sender = message->get_sender();

    // checking if it is an error message 
    if (message->is_error()) {
      auto node = mgr->find_node(sender.node_id());
      MOLA_ASSERT(node != nullptr);

      MOLA_ERROR_TRACE("got an error message from node {{ "
                        << Node::encode(node.get()) << " }}, "
                        << " from " << sender.to_string()
                        << " to " << target.to_string() ); 
      // invalid this node now !!
      mgr->delete_node(sender.node_id());
      return 0;
    }

    auto module = Module::find_module(target.module_id());
    if (module != nullptr) {
      return module->dispatch(message, worker);
    }

    // no deplyment info for this module
    MOLA_ERROR_TRACE("dispatch a message to module with id "
                      << (target.module_id()) << " is null ("
                      << " from " << sender.to_string() 
                      << " to " << target.to_string() << " )");

    // sending an error message back
    message->set_error();
 
    target.set_node_id(sender.node_id());
    sender.set_node_id(ActorID::LOCAL);
  }

  /* FIXME: if the dest is not current node, the sender of the message
   * must be current node, so it must be called on user-context!! */

  auto node = mgr->find_node(target.node_id());
  if (node == nullptr) {
    if (AbsolateWorker::get_worker() != nullptr) {
      // before add to the delay tasks pool
      mgr->add_delay_task([=]() mutable {
        auto ret = Dispatcher::dispatch(message);
        return ret == -1 ? 0 : ret;
      });
      return 0;
    } 
    // called by delay tasks pool
    return -2;
  }

  if (node->invalid())
    return /*DISPATCHER_UNREACH*/-1;

  MOLA_ASSERT(Actor::current() == nullptr || 
              Actor::current()->get_id().module_id()
                == message->get_sender().module_id());
  
  // TODO : If the message is not oneway, which means
  //        the sender will be blocked to wait for the
  //        reply message, then the destination node
  //        should record those messages. When this node
  //        is dead, we could wakeup all those blocked 
  //        actors by sending an error message as response.
  node->deliver_message(message);

  return 0;
}

// ------ for request message -------
int Dispatcher::dispatch(ActorID& target,
                         MessageSafePtr& message,
                         uint32_t return_intf, void *ctx) {
  int ret = 0;
  bool new_reqid = false;
  auto current = Actor::current();
  MOLA_ASSERT(current != nullptr);

  message->set_target(target);
  message->set_sender(current->get_id());
  //std::cout << "set sender :" << current->get_id().to_string() << std::endl;

  message->set_request();

  if (return_intf != ActorID::ANY) {
    // set the return intf id so the receiver can reply
    // message back to the specific interface id !!
    message->get_sender().set_intf_id(return_intf);
    // record the context for this request
    if (message->get_reqid() == 0) {
      message->set_reqid(current->new_reqid());  
      new_reqid = true;
    }
  }

  if (target.node_id() != ActorID::ANY) {
    ret = dispatch(message);
  } else {
    auto module = Module::find_module(target.module_id());
    if (module != nullptr && module->is_private()) {
      // we prefer to use the local module instances if the module is privete!!
      ret = module->dispatch(message, AbsolateWorker::get_current_worker());
    } else { 
      auto mgr = NodeManager::instance();

      auto callback = [=]() mutable {
        auto candidates = 
          mgr->query_node_with_module(target.module_id());
        if (candidates.size() == 0)
          return -2; // no depoyment info

        auto node = lbs->select_one_node(candidates);
        return node->deliver_message(message);
      };

      if (callback() != 0)
        ret = mgr->add_delay_task(callback);
    }
  }

  if (new_reqid) {
    if (ret == 0) {
      // save the request context for async return
      current->on_async_send_done(message, ctx);
    } else {
      // release the reqid 
      current->release_reqid(message->get_reqid());
    }
  }
  return ret;
}

// ------ for message group ---------
int Dispatcher::dispatch(ScatterGroup& mg,
                         uint32_t gather_intf,
                         bool directional) {

  auto mgr = NodeManager::instance();
  MOLA_ASSERT(mgr != nullptr);

  auto current = Actor::current();
  MOLA_ASSERT(current != nullptr);

  int ret = 0;
  bool local = false;

  // target of each message is unset (ANY)
  auto mid = mg[0]->get_target().module_id();

  // FIXME: local only ?? speed up for single node benchmark!!
  if (!directional) {
    auto module = Module::find_module(mid);
    if (module != nullptr && module->is_private()) {
      directional = true;
      local = true;
    }
  }

  // reset the sender's intf id so the scatter tasks can
  // easly find where to return the responses !!
  ActorID sender = current->get_id();
  sender.set_intf_id(gather_intf);
  // set the sender & type info
  mg.foreach([&](MessageSafePtr& msg){
    msg->set_scatter();
    msg->set_reqid(mg.get_reqid());
    msg->set_sender(sender);
    if (local) msg->get_target().set_node_id(ActorID::LOCAL);
  });

  // target of each message is set
  if (directional) {
    mg.foreach([&](MessageSafePtr& msg) {
      if (dispatch(msg) == 0) ret++;
    });
    ;
    return (ret == mg.size()) ? 0 : -4;
  }


  auto candidates = mgr->query_node_with_module(mid);

  if (candidates.size() > 0) {
    ret = lbs->scatter_to_nodes( candidates, mg,
              [&](NodeManager::NodePtr& node, MessageSafePtr& msg) {
                msg->get_target().set_node_id(node->get_id()); // set node id
                return dispatch(msg);
              } );
    return (ret == mg.size()) ? 0 : -4;
  } 

  // add to the delay queue and send them later ..
  mg.foreach([&](MessageSafePtr& msg) {
    mgr->add_delay_task([=]() mutable {
      auto candidates = mgr->query_node_with_module(mid);
      auto size = candidates.size();
      if (size == 0) return -2;
      auto node = lbs->select_one_node(candidates, size);
      return node->deliver_message(msg);
    });
  });
  
  return 0;
}

static inline 
MessageSafePtr beforeAsyncCall(void *msg, uint32_t ret_intf,
                               mola::Actor* actor) {

  MOLA_ASSERT(actor != nullptr);
 
  MessageSafePtr rt_msg( GEN_MESSAGE(msg) );
  
  rt_msg->set_response();
  rt_msg->set_reqid(actor->new_reqid());
  rt_msg->set_target(actor->get_id());
  rt_msg->get_target().set_intf_id(ret_intf);

  actor->on_async_send_done(rt_msg);
  
  return rt_msg;
}

/** these will be called in the C context */
void Lock(const char* zone, uint64_t id,
          void* msg, uint32_t ret_intf) {
  auto mgr = mola::NodeManager::instance();
  auto actor = mola::Actor::current();

  auto rt_msg = beforeAsyncCall(msg, ret_intf, actor);

  mgr->lock(zone, id, [=]() mutable {
    actor->deliver_message(rt_msg); 
  });
}

void UnLock(const char* zone, uint64_t id) {
  auto mgr = NodeManager::instance();
  mgr->unlock(zone, id);
}

void Subscribe(const char* event, void* msg, uint32_t ret_intf) {
  auto mgr = NodeManager::instance();
  auto actor = mola::Actor::current();

  auto rt_msg = beforeAsyncCall(msg, ret_intf, actor);

  mgr->subscribe_event(event, [=]() mutable -> int {
    actor->deliver_message(rt_msg);
    return 0; // TODO !!
  });
}

// --- asynchronized send operation --- //
int SendMsgToIntf(MODULE_ID_T mid, INTF_ID_T intf, INST_ID_T inst,
                  void* msg, INTF_ID_T ret_intf, void* ctx) {

  MessageSafePtr rt_msg( GEN_MESSAGE(msg) );
  ActorID target(GET_NODE_ID(inst), mid, intf, GET_INST_ID(inst));

  /* if the reqid is not zero, we just do an asynchronized return */
  auto reqid = GET_REQ_ID(inst);
  if (reqid != 0) {
    rt_msg->set_reqid(reqid);
    rt_msg->set_response();
    rt_msg->set_target(target);

    // FIXME : merge those code ..
    auto current = mola::Actor::current();
    MOLA_ASSERT(current != nullptr);

    rt_msg->set_sender(current->get_id());

    return mola::Dispatcher::dispatch(rt_msg);
  }

  return mola::Dispatcher::dispatch(target, rt_msg, ret_intf, ctx);
 
}

// --- synchronized send and recv operation --- //
int SendMsgToIntfAndRecv(MODULE_ID_T mid, INTF_ID_T intf, 
                         INST_ID_T inst, void* req, void **res) {

  MOLA_ASSERT(GET_REQ_ID(inst) == 0);

  auto actor = Actor::current<StrongActor>();
  MOLA_ASSERT(actor != nullptr);

  auto reqid = actor->get_fiber_reqid();

  ActorID target(GET_NODE_ID(inst), mid, intf, GET_INST_ID(inst));

  MessageSafePtr rt_msg( GEN_MESSAGE(req) );
  rt_msg->set_reqid(reqid);

  int rt_intf = actor->get_current_message()->get_target().intf_id();

  int ret = mola::Dispatcher::dispatch(target, rt_msg, rt_intf);
  if (ret != 0) return ret;
  
  *res = actor->wait_response();
  if (*res == nullptr) return -1;
  return 0;
}

// --- asynchronized scatter a group of messages --- //
int ScatterToIntf(MODULE_ID_T mid, INTF_ID_T intf, 
                  size_t size, INST_ID_T dest[], void* mg[],
                  INTF_ID_T gather_intf, void *ctx) {
  if (size == 0 || mg == nullptr) 
    return -3; // NO_MSG_TO_SCATTER

  mola::ScatterGroup group(size, mg, ctx);
  
  int i = 0;
  group.foreach([&](MessageSafePtr& msg) mutable {
    if (dest != nullptr) {
      MOLA_ASSERT(GET_REQ_ID(dest[i]) == 0);
      msg->set_target(GET_NODE_ID(dest[i]), mid,
                      intf, GET_INST_ID(dest[i]));
      i++;
    } else {
      msg->set_target(ActorID::ANY, mid,
                      intf, ActorID::ANY);
    }
  });

  if (size != group.size() || /* some messages in mg are NULL !! */
      mola::Dispatcher::dispatch(group, gather_intf, dest != nullptr) != 0) {
    group.failure(); 
    return -4; // SCATTER_ERROR
  }
  return 0;
}

// --- resume the gather context and invoke the callback --- //
// NOTE: this will be called only by the wrapper functions gen by mocc!
void SwitchToGatherCtx(void * ctx, 
                       mola_gather_cb_t gather_callback) {

  MOLA_ASSERT(ctx != nullptr);
  auto mg = reinterpret_cast<mola::GatherGroup*>(ctx);
  
  if (!mg->ready()) return;

  auto initiator = mg->get_initiator();
  auto size = mg->size();

  void ** group = (void**) alloca(sizeof(void*) * size);
  for (int i = 0; i < size; ++i) {
    group[i] = (*mg)[i].get();
  }

  auto actor = mola::Actor::current();

  auto ret = gather_callback(size, group);
  mola::MessageSafePtr response(
    reinterpret_cast<mola_message_t*>(ret));

  if (! response.is_nil()) {
    // return to the initiator if not invalid
    MOLA_ASSERT(! initiator.is_nil());

    if (initiator->is_scatter()) {
      response->set_gather();
    } else {
      response->set_response();
    }

    response->set_reqid( initiator->get_reqid() );
    
    // reply the nested scatter-gather call
    response->set_sender(actor);
    response->set_target(initiator->get_sender());

    mola::Dispatcher::dispatch(response);
  }

  mg->set_done();

  return;
}

void InsertToGatherCtx(void *ctx, void *message) {
  MOLA_ASSERT(ctx != nullptr);
  auto mg = reinterpret_cast<mola::GatherGroup*>(ctx);
  mg->emplace_back(GEN_MESSAGE(message));
}

/* FIXME: You can use 'SaveReturnState/ReplyMsg' to return a
 *        message in an interface without a return statement,
 *        or the return type is not match the message you want 
 *        to return. 
 *        Note if you use those API, the mocc compiler cannot 
 *        make the static checking for you, so it may cause an 
 *        error in runtime !! */

RID_T SaveReturnState_(void* vtab) {
  Actor *actor = Actor::current();
  MOLA_ASSERT(actor != nullptr);

  return static_cast<RID_T>( actor->save_return_state(vtab) );
}

int ReplyMsg(RID_T rid, void *msg) {
  Actor *actor = Actor::current();
  MOLA_ASSERT(actor != nullptr);

  auto rc = reinterpret_cast<RequestContext*>(rid);
  if (rc == nullptr) 
    return -4; /* return state not found */
  
  MOLA_ASSERT(msg != nullptr);
  auto msg_ = GEN_MESSAGE(msg);
  auto vtab = reinterpret_cast<mola_message_vtab_t*>(rc->get_ctx());
  if (vtab != nullptr && vtab != msg_->vtab)
    return -5; /* message type not match */

  MessageSafePtr response(msg_);
  auto initiator = rc->get_initiator();

  response->set_reqid(initiator->get_reqid());
  if (initiator->is_request()) {
    response->set_response();
  } else if (initiator->is_scatter()) {
    response->set_gather();
  } else {
    MOLA_ASSERT(0);
  }

  response->set_target(initiator->get_sender());
  response->set_sender(actor->get_id());
  
  delete rc;
  
  return Dispatcher::dispatch(response);
}

// --- For event and notify operation --- //
EVENT_T CreateEvent() {
  auto actor = Actor::current<StrongActor>();
  MOLA_ASSERT(actor != nullptr);

  MessageSafePtr ev( new MessageWrapper(nullptr, 0) );

  // Target is current actor and current interface.
  ActorID target = actor->get_id();
  target.set_intf_id(
    actor->get_current_message()->get_target().intf_id() );

  ev->set_target(target);
  ev->set_reqid(actor->get_fiber_reqid());
  ev->set_response();
  
  return static_cast<EVENT_T>(ev.pass());
}

void* WaitForNotify() {
  auto actor = Actor::current<StrongActor>();
  MOLA_ASSERT(actor != nullptr);
  // waiting for other to send an empty message
  // to wake me up !!
  void *ret = actor->wait_response();
  if (ret != nullptr) {
    // fetch the date inside the wrapper
    auto rt_msg = reinterpret_cast<MessageWrapper*>(ret);
    ret = rt_msg->get_stream_data();
    // set null to avoid the date be released later
    rt_msg->set_stream_data(nullptr);
  }
  return ret;
}

int NotifyEvent(EVENT_T event, void *data) {
  MOLA_ASSERT(event != nullptr);

  MessageSafePtr ev(
    reinterpret_cast<MessageWrapper*>(event) );
  
  // set current actor as the sender
  ev->set_sender(Actor::current()->get_id());
  // set the data passed to the sleeper
  ev->set_stream_data(reinterpret_cast<char*>(data));

  return Dispatcher::dispatch(ev); 
}
