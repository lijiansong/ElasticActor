#include "mola/actor.hpp"
#include "mola/strong_actor.hpp"
#include "mola/node_manager.hpp"

static inline INST_ID_T
TO_INST_ID(const mola::ActorID& aid, uint32_t reqid = 0) {
  auto nid = aid.node_id();
  if (aid.node_id() == mola::ActorID::LOCAL)
    nid = mola::NodeManager::instance()->local_node()->get_id();
  
  uint64_t id = reqid;
  return (id << 32) | (nid << 16) | aid.instance_id() ;
}

void InstanceExit(int ret) {
  auto current = mola::Actor::current();
  if (current != nullptr) {
    current->exit(ret);
  }
  MOLA_ASSERT(0 && "cannot return here!");
}

INST_ID_T GetMyInstID() {
  auto current = mola::Actor::current();
  MOLA_ASSERT(current != nullptr);
  return TO_INST_ID(current->get_id());
}

INST_ID_T GetSenderInstID() {
  auto current = mola::Actor::current();
  MOLA_ASSERT(current != nullptr);
  auto message = current->get_current_message();
  MOLA_ASSERT(!message.is_nil());
  return TO_INST_ID(message->get_sender(), 
                    message->get_reqid());
}

void* GetSavedCtx() {
  auto current = mola::Actor::current();
  MOLA_ASSERT(current != nullptr);
  auto req_ctx = current->get_current_ctx();
  if (req_ctx != nullptr) 
    return req_ctx->get_ctx();
  return nullptr;
}
