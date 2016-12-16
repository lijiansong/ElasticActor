#ifndef _MOLA_CRT_SUPPORT_H_
#define _MOLA_CRT_SUPPORT_H_

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(offsetof)
#define offsetof(type, member) ((size_t) &((type*)0)->member)
#endif // !defined(offsetof)

#if !defined(container_of)
#define container_of(ptr, type, member)  ({\
    const void *__mptr = (ptr);\
    (type *)( (char *)__mptr - offsetof(type, member) );})
#endif // !defined(container_of)    

/* for builtin atomic operations */
#define MOLA_ATOMIC_INC(n) __atomic_add_fetch(&(n), 1, __ATOMIC_SEQ_CST)
#define MOLA_ATOMIC_DEC(n) __atomic_sub_fetch(&(n), 1, __ATOMIC_SEQ_CST)
 
#define MOLA_ATOMIC_READ(n) __atomic_load_n(&(n), __ATOMIC_SEQ_CST)
#define MOLA_ATOMIC_WRITE(n, v) __atomic_store_n(&(n), __ATOMIC_SEQ_CST)
#define MOLA_XCHG(pval, new) __atomic_exchange_n(pval, new, __ATOMIC_SEQ_CST)

// #define MOLA_CAS(pval, old, new) __sync_bool_compare_and_swap(pval, old, new)
#define MOLA_CAS(pval, old, new) \
    __atomic_compare_exchange_n(pval, &(old), new, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)

#define MOLA_SYNC_ALL() __sync_synchronize()



/* for coroutine implement */

#if 0
#if !defined(__x86_64__) && !defined(USE_UCONTEXT)
# define USE_UCONTEXT 1
#endif // !defined(__x86_64__) && !defined(USE_UCONTEXT)
#endif

/* ucontext api */
#include <ucontext.h>

#define MOLA_UCTX_T ucontext_t

#define MOLA_MAKE_UCTX(...) makecontext(__VA_ARGS__)
#define MOLA_SET_UCTX(uctx) setcontext(uctx)
#define MOLA_GET_UCTX(uctx) getcontext(uctx)
#define MOLA_SWITCH_UCTX(ouctx, uctx) swapcontext(ouctx, uctx)

#define MOLA_UCTX_SET_STACK(uctx, new_sp, size) {\
    (uctx)->uc_stack.ss_sp = new_sp;\
    (uctx)->uc_stack.ss_size = size; }

/* setjmp/longjmp */
#include "mola/c/mola_fiber.h"

#define MOLA_JMPBUF __rt_jmpbuf_t

#define MOLA_SETJMP(env)  __rt_setjmp(&(env))
#define MOLA_LONGJUMP(env, ret) __rt_longjmp(&(env), ret)

#define MOLA_JB_SET_SP  JB_FIX_SP



// for printf format string ..
#ifndef __UINT64_FMTu__
# ifdef __x86_64__
#  define __UINT64_FMTu__ "lu"
# else
#  define __UINT64_FMTu__ "llu"
# endif
#endif

#ifndef __UINT32_FMTu__
# define __UINT32_FMTu__ "u"
#endif

#ifdef __cplusplus
}
#endif

#endif // _MOLA_CRT_SUPPORT_H_
