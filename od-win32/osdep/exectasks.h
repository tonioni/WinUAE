 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Stack magic definitions for autoconf.c
  *
  * Copyright 1997 Bernd Schmidt
  */

#include <setjmp.h>

#define CAN_DO_STACK_MAGIC

#ifdef CAN_DO_STACK_MAGIC
#ifdef __GNUC__
static inline void transfer_control(void *, int, void *, void *, int) __attribute__((noreturn));
static inline void transfer_control(void *s, int size, void *pc, void *f, int has_retval)
#else
__declspec(noreturn) static __forceinline void transfer_control(void *s, int size, void *pc, void *f, int has_retval)
#endif
{
    unsigned long *stacktop = (unsigned long *)((char *)s + size - 20);
    stacktop[0] = 0xC0DEDBAD; /* return address */
    stacktop[1] = (int)s; /* function arg 1: stack */
    stacktop[2] = (int)f; /* function arg 2: trap function */
    stacktop[3] = (int)stacktop; /* function arg 3: return value address */
    stacktop[4] = has_retval;
#ifdef __GNUC__
    __asm__ __volatile__ ("movl %0,%%esp\n\tpushl %1\n\tret\n" : : "r" (stacktop), "r" (pc) : "memory");
#elif defined( _MSC_VER ) /* VisualC++ */
    __asm
    {
        mov    esp, stacktop
        push   pc
        ret
    }
#endif
    /* Not reached. */
    abort();
}

static __inline__ uae_u32 get_retval_from_stack (void *s, int size)
{
    return *(uae_u32 *)((char *)s + size - 20);
}

static __inline__ int stack_has_retval (void *s, int size)
{
    return *(int *)((char *)s + size - 4);
}

#endif
