#ifndef _MOLA_CRT_REFCNT_H_
#define _MOLA_CRT_REFCNT_H_

#include <stdint.h>
#include <assert.h>
#include <stdbool.h>

#include "mola/c/support.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*release_handler_t)(void *);

typedef struct {
  uint32_t count;
  release_handler_t dtor;
} mola_refcnt_t;

#if defined(__MOCC__)

extern void 
mola_refcnt_init(mola_refcnt_t*, release_handler_t);

extern mola_refcnt_t* mola_refcnt_get(mola_refcnt_t*);

extern bool mola_refcnt_put(mola_refcnt_t*);

extern bool mola_refcnt_unique(mola_refcnt_t*);

#else // !defined(__MOCC__)
static inline void mola_refcnt_init(mola_refcnt_t* ref,
                                    release_handler_t dtor) {
  ref->count = 1;
  ref->dtor = dtor;
}

static inline mola_refcnt_t* mola_refcnt_get(mola_refcnt_t* ref) {
  if (ref == NULL) return NULL;
  assert(ref->count > 0);
  MOLA_ATOMIC_INC(ref->count);
  return ref;
}

static inline bool mola_refcnt_put(mola_refcnt_t* ref) {
  if (ref == NULL) return false; // FIXME

  assert(ref->count > 0);
  if (ref != NULL && MOLA_ATOMIC_DEC(ref->count) == 0) {
    if (ref->dtor) 
      ref->dtor(ref);
    
    free(ref);
    return true;
  }
  return false;
}

static inline bool mola_refcnt_unique(mola_refcnt_t* ref) {
  if (ref == NULL) return false; // FIXME
  assert(ref->count > 0);
  return ref->count == 1;
}
#endif // defined(__MOCC__)

#ifdef __cplusplus
}
#endif

#endif // _MOLA_CRT_REFCNT_H_
