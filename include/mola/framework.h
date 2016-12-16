#ifndef _MODULES_FRAMEWORK_H_
#define _MODULES_FRAMEWORK_H_

#ifdef __MOCC__
#pragma GCC system_header
#endif 

#ifndef __MOLA__
# define __MOLA__ 1
#endif // __MOLA__

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include "mola/runtime.h"
#include "mola/c/mola_network.h"
#include "mola/c/mola_future.h"

/* the global thread-local data pointer */
extern __thread void *__rt_gen_data;

/* for network api */
typedef void* Socket;

/* for future api */
typedef mola_future_t FUTURE_T;

#define RT_FUTURE_RV MOLA_FUTURE_TYPE_RETURN_VALUE
#define RT_FUTURE_RC MOLA_FUTURE_TYPE_RETURN_CALLBACK

#define DoFuture mola_future_do
#define FreeFuture(F)\
  do {\
    if ((F) != NULL)\
      mola_refcnt_put(&((F)->refcnt));\
  } while (0)

/* MOLA Runtime APIs */
extern int SendMsgToIntf(MODULE_ID_T, INTF_ID_T, INST_ID_T, 
                         void* msg, INTF_ID_T, void* ctx);

extern int SendMsgToIntfAndRecv(MODULE_ID_T, INTF_ID_T, INST_ID_T,
                                void *, void **);

extern int ScatterToIntf(MODULE_ID_T, INTF_ID_T, size_t,
                         INST_ID_T dest[], void* mg[], 
                         INTF_ID_T, void *ctx);

extern void SwitchToGatherCtx(void*, mola_gather_cb_t);
extern void InsertToGatherCtx(void*, void*);

extern INST_ID_T GetMyInstID();
extern INST_ID_T GetSenderInstID();

extern int GetNodeCount();
extern int GetNodeIDs(uint32_t*);

extern MODULE_ID_T GetModuleID();

extern void* GetSavedCtx();

extern EVENT_T CreateEvent();
extern void* WaitForNotify();
extern int NotifyEvent(EVENT_T event, void *data);

#define ANY_INST (0xffffffffU)
#define ANY_INTF (0x0000ffffU)

#define INVALID_ID (0x0000ffffU)

#define __MOCC_SINGLE_SECTION_BEGIN__ \
    if (GET_INST_ID( GetMyInstID() ) == 1) {

#define __MOCC_SINGLE_SECTION_END__ }

#define SendRecvMsgAndConvertType(mid, intf, inst, req, res, type) \
({\
    void *_res = NULL;\
    int ret = SendMsgToIntfAndRecv(mid, intf, inst, req, &_res);\
    if (ret == 0 && _res != NULL) {\
      extern vtable_t type##__vtable;\
      *(res) = (type*)ConvertRawToType_(_res, &(type##__vtable), 0);\
    }\
    ret;\
})

typedef struct raw_package_ {
  int size;
  char *buffer;
  Socket client;
} Package;

typedef struct mola_proto Proto;

extern mola_message_vtab_t raw_package_vtab;

#define Package__vtable raw_package_vtab

#if defined(__MOCC__) || defined(__SMFG__)

/* dummy declarations for mocc source-to-source compiler */

extern int SendMsg(const char*, const void*);
extern int SendMsgWithCtx(const char*, const void*, void*);

extern int SendRecvMsg(const char*, const void*, void*);

extern int Scatter(const char*, size_t, void* mg);
extern int ScatterWithCtx(const char*, size_t, void* mg, void* ctx);

extern int SendMsgToInst(const char*, INST_ID_T, const void*);
extern int SendMsgToInstWithCtx(const char*, INST_ID_T, const void*, void*);

extern int SendRecvMsgToInst(const char*, INST_ID_T, const void*, void*);

extern int ScatterToInsts(const char*, size_t, INST_ID_T*, void* mg);
extern int ScatterToInstsWithCtx(const char*, size_t, INST_ID_T*, void* mg, void* ctx);

extern void Lock(const char*, uint64_t, const void *);
extern void Subscribe(const char*, const void *);

extern Socket ConnectToServer(const char*, uint16_t, Proto*);
extern Socket StartServer(const char*, uint16_t, Proto*);

#define SaveReturnStateWithType(type) ({\
  type *T = NULL;\
  SaveReturnStateWithType_((void*)T); })

extern RID_T SaveReturnStateWithType_(void*);
extern RID_T SaveReturnState();

extern FUTURE_T CreateReturnFuture(void*);

#else // defined(__MOCC__) || defined(__SMFG__)

extern void Lock(const char*, uint64_t, void *, INTF_ID_T);
extern void Subscribe(const char*, void *, INTF_ID_T);

extern Socket ConnectToServer(const char*, uint16_t, Proto*, INTF_ID_T);
extern Socket StartServer(const char*, uint16_t, Proto*, INTF_ID_T);

#define SaveReturnStateWithType(type) ({\
    extern vtable_t type##__vtable;\
    SaveReturnState_(&(type##__vtable)); })

#define SaveReturnState() SaveReturnState_(NULL)

extern RID_T SaveReturnState_(void*);

#endif // defined(__MOCC__) || defined(__SMFG__)

extern void UnLock(const char*, uint64_t);

extern int SendToSocket(Socket, int, char*);
extern void CloseSocket(Socket);

extern void DisableClient(Socket);
extern void EnableClient(Socket);

extern int ReplyMsg(RID_T rid, void *msg);




/* ignore the mocc import semantic after source-to-source compiling */
#ifndef __MOCC_IMPORT__
# define __MOCC_IMPORT__(F)
#endif // __MOCC_IMPORT__

#ifndef __MOCC_IMPLEMENT__
# define __MOCC_IMPLEMENT__(F)
#endif // __MOCC_IMPLEMENT__

#ifndef __MOCC_IGNORE_CHECK_BEGIN__
# define __MOCC_IGNORE_CHECK_BEGIN__
#endif // __MOCC_IGNORE_CHECK_BEGIN__

#ifndef __MOCC_IGNORE_CHECK_END__
# define __MOCC_IGNORE_CHECK_END__
#endif // __MOCC_IGNORE_CHECK_END__

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // _MODULES_FRAMEWORK_H_
