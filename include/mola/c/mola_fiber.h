#ifndef _MOLA_FIBER_H_
#define _MOLA_FIBER_H_

#ifdef __cplusplus
extern "C" {
#endif

#define JB_BP 0
#define JB_SP 1
#define JB_PC 2

#define JB_FIX_BP(env, new_bp)\
    (env).__buf[JB_BP] = (void*)(new_bp)

#define JB_FIX_SP(env, new_sp)\
    (env).__buf[JB_SP] = (void*)(new_sp)

#define JB_FIX_PC(env, new_pc)\
    (env).__buf[JB_PC] = (void*)(new_pc)

#if !defined(__ASSEMBLER__)

typedef struct __rt_jmpbuf {
  void * __buf[3];
} __rt_jmpbuf_t;

extern int __rt_setjmp(__rt_jmpbuf_t* env);
extern void __rt_longjmp(__rt_jmpbuf_t* env, int) __attribute__((noreturn));

#endif // !defined(__ASSEMBLER__)

#ifdef __cplusplus
}
#endif

#endif // _MOLA_FIBER_H_
