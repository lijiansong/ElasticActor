#include "c/mola_fiber.h"

#if defined(__arm__)
#define ENTRY(name) \
  .globl name;  \
  .type name, %function;\
  .align 4;    \
  name:
#else
#define ENTRY(name) \
  .globl name;      \
  .type name,@function;\
  .align 16;        \
  name:
#endif // defined(__arm__)

#define END(name)   \
  .size name, .-name

#if defined(__x86_64__)

ENTRY (__rt_setjmp)
  /* save current bp register */
  movq %rbp, (JB_BP*8)(%rdi)
  
  /* save the sp before calling this */
  lea  8(%rsp), %rdx
  movq %rdx, (JB_SP*8)(%rdi)

  /* save the return pc */
  mov  (%rsp), %rax
  movq %rax, (JB_PC*8)(%rdi)

  /* return zero for save context */
  xorl %eax, %eax
  retq

END (__rt_setjmp)


ENTRY (__rt_longjmp)
  /* resume the registers for return */
  movq (JB_SP*8)(%rdi), %rsp
  movq (JB_BP*8)(%rdi), %rbp
  movq (JB_PC*8)(%rdi),  %rdx

  /* set return value for setjmp */
  mov  %esi, %eax
  jmpq *%rdx

END (__rt_longjmp)

#elif defined(__i386__)

ENTRY (__rt_setjmp)
  movl %ebp, (JB_BP*4)(%edi)

  lea  4(%esp), %edx
  movl %edx, (JB_SP*4)(%edi)

  mov  (%esp), %eax
  movl %eax, (JB_PC*4)(%edi)

  xorl %eax, %eax
  ret

END (__rt_setjmp)

ENTRY (__rt_longjmp)
  /* resume the registers for return */
  movl (JB_SP*8)(%edi), %esp
  movl (JB_BP*8)(%edi), %ebp
  movl (JB_PC*8)(%edi), %edx

  /* set return value for setjmp */
  mov  %esi, %eax
  jmp  *%edx

END (__rt_longjmp)

#elif defined(__arm__)

/* for arm architectures */
# if (__ARM_ARCH == 7)

ENTRY (__rt_setjmp)
  str r7, [r0], #4
  str sp, [r0], #4
  str r14, [r0], #4

  mov r0, #0
  mov pc, r14

END (__rt_setjmp)


ENTRY (__rt_longjmp)
  ldr r7, [r0], #4
  ldr sp, [r0], #4
  ldr r14, [r0], #4

  mov r0, r1
  mov pc, r14
END (__rt_longjmp)

# endif // __ARM_ARCH == 7

#endif
