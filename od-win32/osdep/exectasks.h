 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Stack magic definitions for autoconf.c
  *
  * Copyright 1997 Bernd Schmidt
  */

#include <setjmp.h>

#ifdef CAN_DO_STACK_MAGIC

#ifdef CPU_64_BIT

 /*
  * UAE - The Un*x Amiga Emulator
  *
  * AMD64/GCC stack magic definitions for autoconf.c
  *
  * Copyright 2005 Richard Drummond
  */

#include <setjmp.h>

struct stack_frame
{
    uae_u64 back_chain;
   
    /* Local area */
    uae_u32 local_has_retval;
    uae_u32 local_retval;
    uae_u64 dummy; /* keep 16-byte aligned */
   
    /* Previous frame */
    uae_u64 end_back_chain;
};

typedef void stupidfunc(void*,void*,uae_u32*,void*,void*);

extern uae_u8 *stack_magic_amd64_asm_executable;

__declspec(noreturn) static __forceinline void transfer_control (void *s, int size, void *pc, void *f, int has_retval)
{   
    struct stack_frame *stacktop = (struct stack_frame *)((char *)s + size - sizeof (struct stack_frame));

    stacktop->end_back_chain   = 0xC0DEDBAD;
    stacktop->local_retval     = 0;
    stacktop->local_has_retval = has_retval;
    stacktop->back_chain       = (uae_u64) &stacktop->end_back_chain;

    ((stupidfunc*)stack_magic_amd64_asm_executable)(s, f, &(stacktop->local_retval), ((uae_u8*)stacktop) - 4 * 8, pc);
    /* Not reached. */
    abort ();
}

STATIC_INLINE uae_u32 get_retval_from_stack (void *s, int size)
{
    return ((struct stack_frame *)((char *)s + size - sizeof(struct stack_frame)))->local_retval;
}

STATIC_INLINE int stack_has_retval (void *s, int size)
{
    return ((struct stack_frame *)((char *)s + size - sizeof(struct stack_frame)))->local_has_retval;
}

#else

__declspec(noreturn) static __forceinline void transfer_control(void *s, int size, void *pc, void *f, int has_retval)
{
    unsigned long *stacktop = (unsigned long *)((char *)s + size - 5 * 4);
    stacktop[0] = 0xC0DEDBAD; /* return address */
    stacktop[1] = (int)s; /* function arg 1: stack */
    stacktop[2] = (int)f; /* function arg 2: trap function */
    stacktop[3] = (int)stacktop; /* function arg 3: return value address */
    stacktop[4] = has_retval;

    __asm
    {
        mov    esp, stacktop
        push   pc
        ret
    }
    /* Not reached. */
    abort();
}

static __inline__ uae_u32 get_retval_from_stack (void *s, int size)
{
    return *(uae_u32 *)((char *)s + size - 5 * 4);
}

static __inline__ int stack_has_retval (void *s, int size)
{
    return *(int *)((char *)s + size - 4);
}

#endif

#endif
