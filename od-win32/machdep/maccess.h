#ifndef __MACCESS_H__
#define __MACCESS_H__
 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Memory access functions
  *
  * Copyright 1996 Bernd Schmidt
  */

#define ALIGN_POINTER_TO32(p) ((~(unsigned long)(p)) & 3)

STATIC_INLINE uae_u32 do_get_mem_long(uae_u32 *a)
{
#if !defined(X86_MSVC_ASSEMBLY_MEMACCESS)
    uae_u8 *b = (uae_u8 *)a;
    return (*b << 24) | (*(b+1) << 16) | (*(b+2) << 8) | (*(b+3));
#else
    uae_u32 retval;
    __asm
    {
	mov	eax, a
	mov	ebx, [eax]
	bswap	ebx
	mov	retval, ebx
    }
    return retval;
#endif
}

STATIC_INLINE uae_u16 do_get_mem_word(uae_u16 *a)
{
    uae_u8 *b = (uae_u8 *)a;

    return (*b << 8) | (*(b+1));
}

#define do_get_mem_byte(a) ((uae_u32)*(uae_u8 *)(a))

STATIC_INLINE void do_put_mem_long(uae_u32 *a, uae_u32 v)
{
#if !defined(X86_MSVC_ASSEMBLY_MEMACCESS)
    uae_u8 *b = (uae_u8 *)a;

    *b = v >> 24;
    *(b+1) = v >> 16;
    *(b+2) = v >> 8;
    *(b+3) = v;
#else
    __asm
    {
	mov	eax, a
	mov	ebx, v
	bswap	ebx
	mov	[eax], ebx
    }
#endif
}

STATIC_INLINE void do_put_mem_word(uae_u16 *a, uae_u16 v)
{
    uae_u8 *b = (uae_u8 *)a;

    *b = v >> 8;
    *(b+1) = (uae_u8)v;
}

STATIC_INLINE void do_put_mem_byte(uae_u8 *a, uae_u8 v)
{
    *a = v;
}

#define call_mem_get_func(func, addr) ((*func)(addr))
#define call_mem_put_func(func, addr, v) ((*func)(addr, v))

#endif
