#ifndef _MODULES_COMM_H_
#define _MODULES_COMM_H_

#ifdef __MOCC__
#pragma GCC system_header
#endif // __MOCC__

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include "mola/c/mola_message.h"
#include "mola/c/mola_module.h"

// -- runtime name alias for modules --
typedef struct mola_module_spec rt_module_spec_t;
typedef mola_intf_t rt_intf_t;
typedef mola_init_cb_t rt_init_t;
typedef mola_fini_cb_t rt_fini_t;

#define RT_INTF_INVALID     MOLA_INTF_TYPE_INVALID
#define RT_INTF_NORMAL      MOLA_INTF_TYPE_NORMAL
#define RT_INTF_SYNC        MOLA_INTF_TYPE_SYNC          

#define RT_MODULE_NORMAL    MOLA_MODULE_TYPE_NORMAL
#define RT_MODULE_SINGLETON MOLA_MODULE_TYPE_SINGLETON
#define RT_MODULE_AUTOSTART MOLA_MODULE_TYPE_AUTOSTART
#define RT_MODULE_STATELESS MOLA_MODULE_TYPE_STATELESS
#define RT_MODULE_STRONG    MOLA_MODULE_TYPE_STRONG

// -- runtime name alias for messages --
typedef mola_message_vtab_t vtable_t;

typedef mola_message_init_cb_t          rt_msg_init_t;
typedef mola_message_get_pack_size_cb_t rt_msg_encoded_size_t;
typedef mola_message_pack_cb_t          rt_msg_encode_t;
typedef mola_message_unpack_cb_t        rt_msg_decode_t;
//typedef mola_message_free_unpacked_cb_t rt_msg_fini_t;
typedef mola_message_copy_cb_t          rt_msg_copy_t;
typedef mola_message_dtor_cb_t          rt_msg_fini_t;

extern void* ConvertRawToType_(void*, mola_message_vtab_t*, bool);

/* this simple macro may be used both by client and mocc code */

#define DECLARE_VTABLE_FOR_PIGEON_FULL(T, DTOR, COPY, FIX) \
vtable_t T##__vtable = {\
  .size = sizeof(T),\
  .init = (rt_msg_init_t) T##__init,\
  .pack_size = (rt_msg_encoded_size_t) pigeon_c_message_get_packed_size,\
  .pack = (rt_msg_encode_t) pigeon_c_message_pack,\
  .unpack = (rt_msg_decode_t) T##__unpack, \
  .free_unpacked = (rt_msg_fini_t) DTOR, \
  .copy = (rt_msg_copy_t) COPY, \
  .init_unpacked = (rt_msg_init_t) FIX, \
};

#define DECLARE_VTABLE_FOR_PIGEON(T) \
  DECLARE_VTABLE_FOR_PIGEON_FULL(T, NULL, NULL, NULL)

#define DECLARE_INIT_FINI_CALLBACK(_funcname_) \
extern void _funcname_()

#define DECLARE_INTF_WRAPPER(_funcname_) \
extern void* _funcname_(void*, void*)

#if defined(__MOCC__)

extern void* NewMsgWithDtor_(void*, mola_message_dtor_cb_t);
extern void* NewMsg_(void*);
extern void* CopyMsg_(void*);
extern void  FreeMsg_(void*);

#undef  NewMsg
#undef  NewMsgWithDtor
#undef  FreeMsg
#undef  CopyMsg

#define NewMsg(T) ((T*)NewMsg_(((T*)0)))
#define NewMsgWithDtor(T, dtor) ((T*)NewMsgWithDtor_(((T*)0), dtor))
#define FreeMsg(M) FreeMsg_((void*)(M))

#define CopyMsg(M) ((typeof(M)) CopyMsg_((void*)(M)))

#else

#define GenMsgOrNull(msg) \
    (((msg) != NULL) ? GEN_MESSAGE(msg) : NULL)

#define GetMsgFromRaw(raw, type) \
    (type*)(((mola_message_t*)(raw))->user_data)

#define AllocMsg(type, dtor) ({\
    extern vtable_t type##__vtable;\
    mola_message_t* message = \
        mola_alloc_message(& type##__vtable, dtor); \
    (type*)(message->user_data);})

#define NewMsgWithDtor(type, dtor) ({\
    type *msg = AllocMsg(type, dtor);\
    if (msg != NULL) type##__init(msg);\
    msg; })

#define NewMsg(type) NewMsgWithDtor(type, NULL)

#define FreeMsg(msg)\
  do {\
    if (msg != NULL)\
      mola_dealloc_message(GEN_MESSAGE(msg));\
  } while (0)


#define ConvertRawToType(msg, type) ({\
    extern vtable_t type##__vtable;\
    type * ret = (type*)ConvertRawToType_(msg, &(type##__vtable), 0);\
    ret; })

#define ConvertMsgToType(msg, type) \
    ConvertRawToType(GEN_MESSAGE(msg), type)

#define CopyMsgOnWrite(msg) ({\
    mola_message_t *raw = GEN_MESSAGE(msg);\
    mola_message_cow(&raw);\
    msg = (typeof (msg))(raw->user_data);\
    msg; })

#define CopyMsg(msg) ({\
    if (msg != NULL)\
      mola_refcnt_get(& (GEN_MESSAGE(msg)->refcnt) );\
    msg; })

#define CreateReturnFuture mola_return_future_create

#endif //  defined(__MOCC__)

// New API for auto refcnt memory alloc/free/copy
#define MolaAlloc(size) mola_alloc(size, NULL)
#define MolaAllocWithDtor(size, dtor) mola_alloc(size, dtor)

#define MolaFree(pointer)\
  do {\
    if (pointer != NULL)\
      mola_free(pointer);\
  } while (0)

#define MolaRefCpy(pointer) ({\
    if (pointer != NULL)\
      mola_refcnt_get(& (GEN_MESSAGE(pointer)->refcnt));\
    pointer; })

// for subscribe / publish API
extern void Publish(const char *);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _MODULES_COMM_H_
