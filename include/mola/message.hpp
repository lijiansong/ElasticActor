/*
 Project MOLA Runtime System version 0.1

 Copyrigth (c) Institute of Computing Technology Chinese Academy of Sciences.

 All rights reserved.

 */

#ifndef _MOLA_MESSAGE_H_
#define _MOLA_MESSAGE_H_

#ifndef _BSD_SOURCE
# define _BSD_SOURCE 1
#endif // _BSD_SOURCE

#include <endian.h>

#include <vector>
#include <atomic>
#include <functional>

#include "c/mola_message.h"
#include "mola/debug.hpp"
#include "mola/actor_id.hpp"

namespace mola {

class WorkItem;
class MessageSafePtr;

/*
 * The real inner message type should be defined in the 
 * C context for C program to handle the refcnt directly.
 * So this is only a simple wrapper for C++ code to reference
 * the inner data in the C type by some member methods.
 *
 * Since the "MessageSafePtr" holds this wrapper, we can use "->"
 * to access those member methods in our C++ runtime like a normal
 * pointer to the raw message.
 */
class MessageWrapper {
  friend class MessageSafePtr;
  friend class Node;

public:
  /* This type can only be created by inside the "MessageSafePtr" class. */
  MessageWrapper(mola_message_t* msg) 
    : m_refcnt(1), m_metadata{0} {
    m_data.msg = msg;
    m_metadata.data_type =
        msg->vtab->unpack ? TYPE_USER_NORMAL : TYPE_USER_LITTLE;
    m_metadata.size = msg->vtab->size + sizeof(mola_message_t);
  }

  MessageWrapper(char *stream, size_t size)
    : m_refcnt(1), m_metadata{0} {
    m_data.stream = stream;
    m_metadata.data_type = TYPE_STREAM;
    m_metadata.size = size;
  }
 
  MessageWrapper(MessageWrapper&& ) = delete;

  MessageWrapper(const MessageWrapper& wrapper)
    : m_refcnt(1)
    , m_data(wrapper.m_data)
    , m_metadata(wrapper.m_metadata) {
    if (m_metadata.data_type != TYPE_STREAM) {
      mola_refcnt_get(& m_data.msg->refcnt);
    }
  }

private:
  virtual ~MessageWrapper() {
    if (m_metadata.data_type == TYPE_STREAM &&
        m_data.stream != nullptr) {
      free(m_data.stream);
    } else if (m_data.msg != nullptr) {
      mola_dealloc_message(m_data.msg);
    }
  }

  /* The methods only be invoked by the "MessageSafePtr" class : */
  MessageWrapper* inc_ref() { 
    m_refcnt++;
    return this;
  }

  void dec_ref() {
    if (--m_refcnt == 0) {
      delete this;
    }
  }

  MessageWrapper& operator=(MessageWrapper&&) = delete;
  MessageWrapper& operator=(MessageWrapper&) = delete;
  MessageWrapper& operator=(const MessageWrapper&) = delete;

public:

  /* The wrapper methods for the C type : */
  void set_request() { 
    MOLA_SET_METATYPE(m_metadata, MOLA_MESSAGE_TYPE_REQUSET); 
  }
  void set_response() { 
    MOLA_SET_METATYPE(m_metadata, MOLA_MESSAGE_TYPE_RESPONSE); 
  }

  void set_scatter() {
    MOLA_SET_METATYPE(m_metadata, MOLA_MESSAGE_TYPE_SCATTER); 
  }
  void set_gather() { 
    MOLA_SET_METATYPE(m_metadata, MOLA_MESSAGE_TYPE_GATHER); 
  }
  void set_error() {
    MOLA_SET_METATYPE(m_metadata, MOLA_MESSAGE_TYPE_ERROR);
  }

  void set_reqid(uint32_t reqid) {
    m_metadata.reqid = reqid;
  }
  uint32_t get_reqid() const { return m_metadata.reqid; }

  bool is_request() const { return MOLA_IS_REQUEST(m_metadata); }
  bool is_response() const { return MOLA_IS_RESPONSE(m_metadata); }

  bool is_scatter() const { return MOLA_IS_SCATTER(m_metadata); }
  bool is_gather() const { return MOLA_IS_GATHER(m_metadata); }

  bool is_error() const { return MOLA_IS_ERROR(m_metadata); }

  bool is_oneway() const { return MOLA_IS_ONEWAY(m_metadata); }

  void set_sender(const WorkItem* sender);

  void set_sender(const ActorID& id) {
    m_metadata.sender = id.u64();
  }

  void set_sender(uint32_t node_id, uint32_t module_id,
                  uint32_t intf_id, uint32_t inst_id) {
    get_sender().set_node_id(node_id);
    get_sender().set_module_id(module_id);
    get_sender().set_intf_id(intf_id);
    get_sender().set_inst_id(inst_id);
  }

  const ActorID& get_sender() const { 
    return *(ActorID::convert_from_u64(&(m_metadata.sender)));
  }
  
  ActorID& get_sender() {
    return *(ActorID::convert_from_u64(&(m_metadata.sender)));
  }
  
  void set_target(const WorkItem* target);

  void set_target(const ActorID& id) { 
    m_metadata.target = id.u64();
  }

  void set_target(uint32_t node_id, uint32_t module_id,
                  uint32_t intf_id, uint32_t inst_id) {
    get_target().set_node_id(node_id);
    get_target().set_module_id(module_id);
    get_target().set_intf_id(intf_id);
    get_target().set_inst_id(inst_id);
  }

  const ActorID& get_target() const { 
    return *(ActorID::convert_from_u64(&(m_metadata.target)));
  }

  ActorID& get_target() {
    return *(ActorID::convert_from_u64(&(m_metadata.target)));
  }

  mola_message_metadata_t& get_metadata() {
    return m_metadata;
  }

  const mola_message_metadata_t& get_metadata() const {
    return m_metadata;
  }

  void set_stream_data(char *stream) {
    m_metadata.data_type = TYPE_STREAM;
    m_data.stream = stream;
  }

  char *get_stream_data() const {
    if (m_metadata.data_type == TYPE_STREAM)
      return m_data.stream;
    return nullptr;
  }

  void copy_on_write() {
    if (m_metadata.data_type != TYPE_STREAM) {
      mola_message_cow(& m_data.msg);
    }
  }

  bool serialize(char*& buf, size_t& size, bool be = false);
  void* deserialize(mola_message_vtab_t *vtab);

private:
  void drop_() {
    if (m_metadata.data_type != TYPE_STREAM &&
        m_data.msg != nullptr) {
      mola_refcnt_put(& m_data.msg->refcnt);
      m_data.msg = nullptr;
    }
  }

  mola_message_t* pass_() {
    if (m_metadata.data_type != TYPE_STREAM) {
      auto ret = m_data.msg;
      m_data.msg = nullptr;
      return ret;
    }
    return nullptr;
  }

  std::atomic<uint32_t> m_refcnt;
  mola_message_metadata_t m_metadata;
  mola_message_data_t m_data;
};

class MessageSafePtr {
public:
  MessageSafePtr() : m_wrapper(nullptr) {}

  MessageSafePtr(MessageSafePtr& _ptr)
    : m_wrapper(_ptr.m_wrapper ? _ptr.m_wrapper->inc_ref() : nullptr) {}

  MessageSafePtr(MessageWrapper *_raw)
    : m_wrapper(_raw) {}

  MessageSafePtr(mola_message_t *_msg)
    : m_wrapper(_msg ? new MessageWrapper(_msg) : nullptr) {}

  MessageSafePtr(MessageSafePtr&& _ptr) 
    : m_wrapper(_ptr.m_wrapper) {
    _ptr.m_wrapper = nullptr;
  }

  ~MessageSafePtr() { drop(); }

  bool is_nil() { return m_wrapper == nullptr; }
  
  MessageWrapper* get() const { 
    return m_wrapper;
  }

  /** pass the pointer to the C context 
   *  and discard this reference. */
  MessageWrapper* pass() {
    auto raw_ptr = m_wrapper;
    m_wrapper = nullptr;
    return raw_ptr;
  }

  void drop() {
    if (m_wrapper != nullptr) {
      m_wrapper->dec_ref();
      m_wrapper = nullptr;
    }
  }

  MessageSafePtr& operator = (MessageSafePtr& rhs) {
    drop();
    if (rhs.m_wrapper != nullptr)
      m_wrapper = rhs.m_wrapper->inc_ref();
    return *this;
  }

 void operator = (MessageSafePtr&& rhs) {
    drop();
    m_wrapper = rhs.pass();
  }

  MessageSafePtr& operator = (mola_message_t *raw) {
    drop();
    m_wrapper = new MessageWrapper(raw);
    return *this;
  }

  // To access the public member methods of "MessageWrapper"
  MessageWrapper* operator -> () {
    return m_wrapper;
  }

  const MessageWrapper* operator -> () const {
    return m_wrapper;
  }

  // To access the object "MessageWrapper"
  MessageWrapper& operator * () {
    return *m_wrapper;
  }

  const MessageWrapper& operator * () const {
    return *m_wrapper;
  }

private:
  MessageWrapper* m_wrapper;
};

typedef std::vector<MessageSafePtr> MessageGroup;

class RequestContext {
public:
  RequestContext(uint32_t reqid, MessageSafePtr& initiator, void *ctx)
    : m_reqid(reqid), m_initiator(initiator), m_ctx(ctx) {
    // FIXME : if request context is created during the init callback
    //         the `m_initiator' is unset, we can ignore this now ..
    /* MOLA_ASSERT(! m_initiator.is_nil()); */
  }
  virtual ~RequestContext() {}

  uint32_t get_reqid() const { return m_reqid; }
  void set_reqid(uint32_t reqid) { m_reqid = reqid; }

  MessageSafePtr& get_initiator() { return m_initiator; }
  /* 
  void set_initiator(MessageSafePtr& initiator) {
    m_initiator = initiator;
  } 
  */

  void *get_ctx() const { return m_ctx; }
  /* void set_ctx(void *ctx) { m_ctx = ctx; } */

  virtual bool done() const { return true; }

private:
  uint32_t m_reqid;
  MessageSafePtr m_initiator;
  void *m_ctx;
};

class Actor;
/*
 * The message group using by Scatter-Gather.
 */
class ScatterGroup : public MessageGroup {
public:
  ScatterGroup(size_t size, void *mg[], void *ctx);
  
  virtual ~ScatterGroup();

  void failure();

  uint32_t get_reqid() const { return m_reqid; }
  void set_reqid(uint32_t reqid) { m_reqid = reqid; }

  void foreach(std::function<void(MessageSafePtr&)> func) {
    for (int i = 0; i < size(); ++i) func((*this)[i]);
  } 

private:
  Actor *m_current;
  uint32_t m_reqid;
  void *m_ctx;
  // ------------- TODO -------------- //
};

class GatherGroup : public RequestContext, 
                    public MessageGroup {
public:
  explicit GatherGroup(uint32_t reqid, uint32_t size,
                       MessageSafePtr& initiator, void *ctx) 
    : RequestContext(reqid, initiator, ctx)
    , m_done(false), m_size(size) { }

  GatherGroup(GatherGroup&& gg) = default;

  virtual ~GatherGroup() {}

  uint32_t get_size() const { return m_size; }

  bool ready() const { 
    return MessageGroup::size() == m_size;
  }
  
  bool done() const override {
    return m_done;
  }

  void set_done(bool done = true) {
    m_done = done;
  }

private:
  bool m_done;
  uint32_t m_size;  // total messages to gather
};

} // namespace mola

extern "C" {
  void* ConvertRawToType_(void *raw, mola_message_vtab_t *vtab, bool release);
}

#endif // _MOLA_MESSAGE_H_
