#ifndef _MOLA_CRT_FUTURE_H_
#define _MOLA_CRT_FUTURE_H_

#include <stdlib.h>
#include "mola/c/mola_refcnt.h"

#ifdef __cplusplus
extern "C" {
#endif 

/// Define the type of future
enum {
  MOLA_FUTURE_TYPE_NORMAL = 0,
  MOLA_FUTURE_TYPE_RETURN_VALUE,
  MOLA_FUTURE_TYPE_RETURN_CALLBACK,
  /* --- TODO --- */
};

/// Define the status of future
enum {
  MOLA_FUTURE_OK = 0,
  MOLA_FUTURE_DONE,
  MOLA_FUTURE_ERROR,
};

typedef uint32_t mola_future_type_t;
typedef uint32_t mola_future_status_t;

struct mola_future;

/// General future callback type
typedef int (*mola_future_callback_t)(struct mola_future*, void*);

/// General future struct type
typedef struct mola_future {
  mola_refcnt_t          refcnt;
  mola_future_type_t     type;
  mola_future_status_t   status;
  mola_future_callback_t callback;
} *mola_future_t;


/// Do the future now
static int mola_future_do(mola_future_t future, void* arg) {
  if (future->status != MOLA_FUTURE_OK)
    return -11;

  int rc = future->callback(future, arg);
  if (rc == 0) {
    future->status = MOLA_FUTURE_DONE;
    return 0;
  }
  
  future->status = MOLA_FUTURE_ERROR;
  return rc;
}

/// Destructor of future
static void mola_future_release(mola_future_t future) {
  // If not done, just do it !!
  if (future->status == MOLA_FUTURE_OK) {
    mola_future_do(future, NULL);
  }
}

/// Constructor of future
static void mola_future_init(mola_future_t future,
                             mola_future_type_t type,
                             mola_future_callback_t callback) {
  mola_refcnt_init(& future->refcnt, 
                   (release_handler_t)mola_future_release);
  future->status = MOLA_FUTURE_OK;
  future->type = type;
  future->callback = callback;
}

/// Extension future for delay return senmantic
typedef struct mola_return_future {
  struct mola_future base;
  void*              state;  // RID_T, saved return state
  union {
    void* (*fp)(void*);
    void* rv;
  } u;
} *mola_return_future_t;


extern int ReplyMsg(void*, void*);
extern void* SaveReturnState_(void*);

/// Callback for return future, this is a builtin one.
static int mola_return_future_callback(mola_future_t base, void* arg) {
  if (base->status != MOLA_FUTURE_OK)
    return -10;

  void *rt = NULL;
  mola_return_future_t future = 
    (mola_return_future_t)base;

  if (base->type == MOLA_FUTURE_TYPE_RETURN_VALUE) {
    rt = future->u.rv;
  } else if (base->type == MOLA_FUTURE_TYPE_RETURN_CALLBACK) {
    rt = future->u.fp(arg);
  } else {
    return -11;
  }
  
  return ReplyMsg(future->state, rt);
}

/// Create and init the return future.
static mola_future_t
mola_return_future_create(uint32_t type, void *rv) {
  assert((type == MOLA_FUTURE_TYPE_RETURN_VALUE ||
          type == MOLA_FUTURE_TYPE_RETURN_CALLBACK) && 
         rv != NULL);

  mola_return_future_t future = 
    malloc(sizeof(struct mola_return_future));
  
  mola_future_init(&future->base, type, 
                   mola_return_future_callback);

  future->state = SaveReturnState_(NULL);
  future->u.rv = rv;

  return &(future->base);
}

#ifdef __cplusplus
}
#endif

#endif // _MOLA_CRT_FUTURE_H_
