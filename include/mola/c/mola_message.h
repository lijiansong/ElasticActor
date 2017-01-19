#ifndef _MOLA_CRT_MESSAGE_H_
#define _MOLA_CRT_MESSAGE_H_

#include <stdint.h>
#include <stdlib.h>

#ifndef _BSD_SOURCE
#define _BSD_SOURCE 1
#endif // _BSD_SOURCE

#include <endian.h>

#include "mola/actor_id.hpp"
#include "mola/c/support.h"
#include "mola/c/mola_refcnt.h"

#ifdef __cplusplus
extern "C" {
#endif


/* define the meta type of the inner messages */
enum {
  MOLA_MESSAGE_TYPE_REQUSET = 1,
  MOLA_MESSAGE_TYPE_RESPONSE = 2,
  MOLA_MESSAGE_TYPE_SCATTER = 4,
  MOLA_MESSAGE_TYPE_GATHER = 8,

  MOLA_MESSAGE_TYPE_USERCREAT = 16,

  MOLA_MESSAGE_TYPE_ERROR = 0x8000,
};

/* message data type */
enum {
  TYPE_STREAM = 1,
  TYPE_USER_LITTLE = 2,
  TYPE_USER_NORMAL = 3,
};

/* user defined init, default is NULL */
typedef void (*mola_message_init_cb_t)(void*);
/* user defined get pack size callback */
typedef size_t (*mola_message_get_pack_size_cb_t)(const void*);
/* user defined pack callback */
typedef size_t (*mola_message_pack_cb_t)(const void*, void*);
/* user defined unpack callback */
typedef void* (*mola_message_unpack_cb_t)(void*, size_t, const void*);
/* user defined deallocator, to free the data generate by unpack */
typedef void* (*mola_message_free_unpacked_cb_t)(void*, void*);
/* user defined deallocator, default is NULL */
typedef void (*mola_message_dtor_cb_t)(void*);
/* user defined copy constructor */
typedef void (*mola_message_copy_cb_t)(void*, const void *);

typedef struct {
  uint32_t size;

  mola_message_init_cb_t init;
  mola_message_get_pack_size_cb_t pack_size;
  mola_message_pack_cb_t pack;
  mola_message_unpack_cb_t unpack;
  // mola_message_free_unpacked_cb_t free_unpacked;
  mola_message_dtor_cb_t free_unpacked;

  mola_message_copy_cb_t copy;
  mola_message_init_cb_t init_unpacked;

} mola_message_vtab_t;

/* the user allocated message wrapper */
typedef struct {
  mola_refcnt_t refcnt;
  mola_message_vtab_t* vtab;    // virtual callbacks' table
  mola_message_dtor_cb_t dtor; // destructor callback
  void *user_data[0];
} mola_message_t;

/* The metadata, ahead package for each transfer */
typedef struct {
  uint16_t type;    // the type of this message
  uint16_t data_type;  // user data or stream data
  uint32_t size;    // the serialized package size
  ActorID sender;  // the sender's instance id
  ActorID target;  // the target's instance id
  uint32_t reqid;   // the request id for keeping stream context
} mola_message_metadata_t; 

typedef union {
  char *stream;
  mola_message_t *msg;
} mola_message_data_t;


/** get the generatic message pointer from the specific one */
#define GEN_MESSAGE(user) \
    container_of(user, mola_message_t, user_data) 

/** copy this message on write, if the message is unique
 *  reference, no copy will happen !! */
mola_message_t* mola_message_cow(mola_message_t** message);

/** alloc a new raw inner message */
mola_message_t* mola_alloc_message(mola_message_vtab_t*, 
                                   mola_message_dtor_cb_t);

/** dec the refcnt of the message, if got zero, free the memory */
void mola_dealloc_message(mola_message_t*);

/** init the unpacked message */
void mola_init_unpacked_message(void*, mola_message_vtab_t*);

/** alloc refcnt memory */
void *mola_alloc(size_t, mola_message_dtor_cb_t); 

/** free refcnt memory */
void mola_free(void*);

// --- helper macros --- //

#define MOLA_IS_REQUEST(meta) \
    ((meta).type & MOLA_MESSAGE_TYPE_REQUSET)

#define MOLA_IS_RESPONSE(meta) \
    ((meta).type & MOLA_MESSAGE_TYPE_RESPONSE)

#define MOLA_IS_SCATTER(meta) \
    ((meta).type & MOLA_MESSAGE_TYPE_SCATTER)

#define MOLA_IS_GATHER(meta) \
    ((meta).type & MOLA_MESSAGE_TYPE_GATHER)

#define MOLA_IS_ONEWAY(meta) \
    (!MOLA_IS_REQUEST(meta) && !MOLA_IS_GATHER(meta)) 

#define MOLA_IS_USERCREAT(meta) \
    ((meta).type & MOLA_MESSAGE_TYPE_USERCREAT)

#define MOLA_IS_ERROR(meta) \
    ((meta).type & MOLA_MESSAGE_TYPE_ERROR)


#define MOLA_SET_METATYPE(meta, metatype) \
    (meta).type = (metatype)



#define MOLA_MESSAGE_SIZE(meta) \
    ((meta).size)

#define MOLA_MESSAGE_TARGET(meta) \
    ((meta).target)

#define MOLA_MESSAGE_SOURCE(meta) \
    ((meta).sender)

static void inline metatole(mola_message_metadata_t *meta) {
#ifndef __LITTLE_ENDIAN__
  mola_message_metadata_t meta_ = *meta;
  meta->type = htole16(meta_.type);
  meta->data_type = htole16(meta_.data_type);
  meta->size = htole32(meta_.size);
  meta->sender.dnode_id = htole32(meta_.sender.dnode_id);
  meta->sender.dmodule_id = htole16(meta_.sender.dmodule_id);
  meta->sender.dintf_id = htole16(meta_.sender.dintf_id);
  meta->sender.dinst_id = htole32(meta_.sender.dinst_id);
  meta->target.dnode_id = htole32(meta_.target.dnode_id);
  meta->target.dmodule_id = htole16(meta_.target.dmodule_id);
  meta->target.dintf_id = htole16(meta_.target.dintf_id);
  meta->target.dinst_id = htole32(meta_.target.dinst_id);
  meta->reqid = htole32(meta_.reqid);
#endif
  return;
}

static void inline metatobe(mola_message_metadata_t *meta) {
#ifdef __LITTLE_ENDIAN__
  mola_message_metadata_t meta_ = *meta;
  meta->type = htobe16(meta_.type);
  meta->data_type = htobe16(meta_.data_type);
  meta->size = htobe32(meta_.size);
  meta->sender.dnode_id = htobe32(meta_.sender.dnode_id);
  meta->sender.dmodule_id = htobe16(meta_.sender.dmodule_id);
  meta->sender.dintf_id = htobe16(meta_.sender.dintf_id);
  meta->sender.dinst_id = htobe32(meta_.sender.dinst_id);
  meta->target.dnode_id = htobe32(meta_.target.dnode_id);
  meta->target.dmodule_id = htobe16(meta_.target.dmodule_id);
  meta->target.dintf_id = htobe16(meta_.target.dintf_id);
  meta->target.dinst_id = htobe32(meta_.target.dinst_id);
  meta->reqid = htobe32(meta_.reqid);
#endif
  return;
}



/* put the declaration here, so both actor.cpp and framework.h
 * can include it !! */
typedef uint64_t INST_ID_T;
typedef uint32_t MODULE_ID_T;
typedef uint32_t INTF_ID_T;
typedef uint32_t REQ_ID_T;

typedef void* RID_T;
typedef void* EVENT_T;

#define GET_NODE_ID(id) (uint32_t)((id) >> 16)
#define GET_INST_ID(id) (uint32_t)((id) & 0x0000FFFFULL)
#define GET_REQ_ID(id)  (uint32_t)((id) >> 32)

#ifdef __cplusplus
}
#endif

#endif // _MOLA_CRT_MESSAGE_H_
