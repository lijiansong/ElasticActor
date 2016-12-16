extern "C" {

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "c/mola_message.h"

static inline void
__init_message(mola_message_t *message, 
               mola_message_vtab_t *vtab,
               mola_message_dtor_cb_t dtor,
               release_handler_t release_handler) {
  mola_refcnt_init(& message->refcnt, release_handler);
  message->vtab = vtab;
  message->dtor = dtor;
}


/* adapte for pigeon's decoder callback */
struct mola_message_allocator {
  void *(*alloc)(void *, size_t size);
  void (*free)(void *, void *pointer);
  void *user_data;
};

static void* __allocate(void * /*dummy*/, size_t size) {
  mola_message_t* message = 
    (mola_message_t*)malloc(sizeof(mola_message_t) + size);

  mola_refcnt_init(& message->refcnt, nullptr);
  return & message->user_data[0];
}

static void __deallocate(void * /*dummy*/, void *pointer) {
  mola_message_t* message = GEN_MESSAGE(pointer);
  // FIXME: since pigeon will call the `do_free' inside the 
  //        `free_unpacked' callback, when the refcnt already
  //        reach to zero, so we do this trick here to make sure
  //        the pigeon allocated messages can be released correctly.
  if (MOLA_ATOMIC_READ(message->refcnt.count) > 0) {
    mola_refcnt_put(& message->refcnt);
  }
}

static struct mola_message_allocator __allocator = {
  .alloc = __allocate,
  .free  = __deallocate,
  .user_data = nullptr,
};

static void  __dealloc_unpacked_message(void *data) {
  mola_message_t *message = (mola_message_t*)data;
  assert(message->vtab);
  if (message->dtor != nullptr)
    message->dtor(& message->user_data[0]);
  
  if (message->vtab->free_unpacked)
    message->vtab->free_unpacked(& message->user_data[0]/*, &__allocator*/);
}

mola_message_t* mola_alloc_message(mola_message_vtab_t *vtab,
                                   mola_message_dtor_cb_t dtor) {
  assert(vtab != nullptr);
  mola_message_t *message = 
    (mola_message_t*)malloc(sizeof(mola_message_t) + vtab->size);
  __init_message(message, vtab, dtor, 
                 (release_handler_t)__dealloc_unpacked_message);
  return message;
}

void mola_dealloc_message(mola_message_t* message) {
  __deallocate(nullptr, & message->user_data[0]);
}

void *mola_alloc(size_t size, mola_message_dtor_cb_t dtor) {
  assert(size > 0);
  mola_message_t *message =
    (mola_message_t*)malloc(sizeof(mola_message_t) + size);
  __init_message(message, nullptr, dtor, nullptr);
  return & message->user_data[0];
}

void mola_free(void *pointer) {
  assert(pointer != nullptr);
  __deallocate(nullptr, pointer);
}

mola_message_t* mola_message_cow(mola_message_t** pmsg) {
  mola_message_t *message = *pmsg;

  if (mola_refcnt_unique(& message->refcnt))  
    return message;
  
  mola_message_t* copy = mola_alloc_message(message->vtab, message->dtor);
  if (message->vtab->copy) {
    message->vtab->copy(copy->user_data, message->user_data);
  } else {
    memcpy(copy->user_data, message->user_data, message->vtab->size);
  }
  
  mola_refcnt_put(& message->refcnt);
  return (*pmsg = copy);
}

void mola_init_unpacked_message(void* msg, mola_message_vtab_t *vtab) {
  if (msg == nullptr) 
    return;
  
  assert(vtab != nullptr);
  __init_message(GEN_MESSAGE(msg), vtab, nullptr,
                 (release_handler_t)__dealloc_unpacked_message);
  /* recurrent init nested messages */
  if (vtab->init_unpacked != nullptr)
    vtab->init_unpacked(msg);
}

} // extern "C"



#include "mola/scheduler.hpp"
#include "mola/message.hpp"
#include "mola/actor.hpp"

using namespace mola;

void MessageWrapper::set_sender(const WorkItem* sender) {
  MOLA_ASSERT(sender != nullptr);
  set_sender(sender->get_id());
}

void MessageWrapper::set_target(const WorkItem* target) {
  MOLA_ASSERT(target != nullptr);
  set_target(target->get_id());
}

bool MessageWrapper::serialize(char*& buf, size_t& size, bool be) {
  bool ret = true;

  // FIXME : make sure this message is unique before
  //         doing the seralization !!
  if (m_metadata.data_type == TYPE_USER_NORMAL) {
    mola_message_t* msg = m_data.msg;

    // pack func ptr is not NULL, we should pack this message
    // into a new allocated buffer ..
    size = msg->vtab->pack_size(msg->user_data);
    m_metadata.size = size;
    buf = (char*)malloc(size * sizeof(char));
    auto pack_size = msg->vtab->pack(msg->user_data, buf);
    MOLA_ASSERT(pack_size <= size);
    
    // FIXME: drop the user message after serialization !!
    drop_();
    
  } else {
    // little-message, means we should sending the raw_content
    // directly to another cluster node ..
    size = m_metadata.size;
    buf = m_data.stream;
    ret = false;
  } 

  // if the target's byte order is different from the host's
  // we need to change the order of metadata !!
  // NOTE: we only change the byte order before sending !!
  if (be) 
    metatobe(& m_metadata);
  else
    metatole(& m_metadata);
  return ret;
}


void* MessageWrapper::deserialize(mola_message_vtab_t *vtab) {
  MOLA_ASSERT(vtab != nullptr);

  if (m_metadata.data_type != TYPE_STREAM) {
    auto msg = pass_();
    return msg->user_data;
  }

  if (vtab->unpack == nullptr) {
    // LITTLE MESSAGE :
    m_metadata.data_type = TYPE_USER_LITTLE;
    __init_message(m_data.msg, vtab, nullptr, nullptr);
    auto msg = pass_();
    return msg->user_data;
  }

  void *msg = vtab->unpack(&__allocator,
                           m_metadata.size,
                           m_data.stream);
  MOLA_ASSERT(msg != nullptr);

  mola_init_unpacked_message(msg, vtab);

  free(m_data.stream);
  m_metadata.data_type = TYPE_USER_NORMAL;

  m_data.msg = nullptr;

  return msg;
}

void* ConvertRawToType_(void *raw, mola_message_vtab_t *vtab, bool release) {
  MessageSafePtr raw_( reinterpret_cast<MessageWrapper*>(raw) );
  auto ret = raw_->deserialize(vtab);
  if (!release) raw_.pass();

  MOLA_ASSERT(ret != nullptr);
  return ret;
}



#if !defined(CLIENT_CONTEXT)

ScatterGroup::ScatterGroup(size_t size, void *mg[], void *ctx) : m_ctx(ctx) { 
  for (int i = 0; i < size; ++i) {
    if (mg[i] == nullptr) continue;

    MessageSafePtr message( GEN_MESSAGE(mg[i]) );
    // NOTE : Since we must increase the refcnt number to keep 
    //        this message from auto deleting, however, 
    //        the constructor `MessageSafePtr(mola_message_t*)'
    //        not do that, so we must use the `push_back' instead of
    //        `emplace_back'. So we can make the refcnt plus one
    //        during the copy constructor `MessageSafePtr(MessageSafePtr&)' 
    MessageGroup::push_back( std::move(message) );
  }

  m_current = Actor::current();
  MOLA_ASSERT(m_current != nullptr);

  m_reqid = m_current->new_reqid();
}

ScatterGroup::~ScatterGroup() {
  MOLA_ASSERT(m_current != nullptr && 
            m_current == Actor::current());

  if (m_reqid != Actor::INVALID_REQID)
    m_current->on_scatter_done(this, m_ctx);
}

// when scatter operation failed, we shoud call this to
// release the reqid for current operation !!
void ScatterGroup::failure() {
  // FIXME : Reclaim the `reqid' is unsafe,
  //         since some messages of the group may
  //         have been sent to other nodes already,
  //         so the reply message with the given `reqid'
  //         may received later, and we'd better not
  //         using this id until all sent messages are back.
  //         But how to handle that status, since those messages
  //         may return or not return because of errors. So we
  //         must alway keep this `reqid' even the scatter was failed!!
#if 0
  m_current->release_reqid(m_reqid);
#endif 
  m_reqid = Actor::INVALID_REQID;
}





#endif // !defined(CLIENT_CONTEXT)
