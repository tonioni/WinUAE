#ifndef __MACCESS_H__
#define __MACCESS_H__

#include <stdlib.h>

 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Memory access functions
  *
  * Copyright 1996 Bernd Schmidt
  */

#ifdef CPU_64_BIT
#define ALIGN_POINTER_TO32(p) ((~(uae_u64)(p)) & 3)
#else
#define ALIGN_POINTER_TO32(p) ((~(uae_u32)(p)) & 3)
#endif

#define do_get_mem_byte(a) ((uae_u32)*(uae_u8 *)(a))
STATIC_INLINE void do_put_mem_byte(uae_u8 *a, uae_u8 v)
{
	*a = v;
}

#ifdef HAVE_MOVBE

#include <immintrin.h>

STATIC_INLINE uae_u64 do_get_mem_quad(uae_u64 *a)
{
	return _load_be_u64(a);
}

STATIC_INLINE uae_u32 do_get_mem_long(uae_u32 *a)
{
	return _load_be_u32(a);
}

STATIC_INLINE uae_u16 do_get_mem_word(uae_u16 *a)
{
	return _load_be_u16(a);
}

STATIC_INLINE void do_put_mem_quad(uae_u64 *a, uae_u64 v)
{
	_store_be_u64(a, v);
}

STATIC_INLINE void do_put_mem_long(uae_u32 *a, uae_u32 v)
{
	_store_be_u32(a, v);
}

STATIC_INLINE void do_put_mem_word(uae_u16 *a, uae_u16 v)
{
	_store_be_u16(a, v);
}


#else

STATIC_INLINE uae_u64 do_get_mem_quad(uae_u64 *a)
{
	return _byteswap_uint64(*a);
}

STATIC_INLINE uae_u32 do_get_mem_long(uae_u32 *a)
{
	return _byteswap_ulong(*a);
}

STATIC_INLINE uae_u16 do_get_mem_word(uae_u16 *a)
{
	return _byteswap_ushort(*a);
}


STATIC_INLINE void do_put_mem_quad(uae_u64 *a, uae_u64 v)
{
	*a = _byteswap_uint64(v);
}

STATIC_INLINE void do_put_mem_long(uae_u32 *a, uae_u32 v)
{
	*a = _byteswap_ulong(v);
}

STATIC_INLINE void do_put_mem_word(uae_u16 *a, uae_u16 v)
{
	*a = _byteswap_ushort(v);
}


#endif

STATIC_INLINE uae_u64 do_byteswap_64(uae_u64 v)
{
	return _byteswap_uint64(v);
}

STATIC_INLINE uae_u32 do_byteswap_32(uae_u32 v)
{
	return _byteswap_ulong(v);
}

STATIC_INLINE uae_u16 do_byteswap_16(uae_u16 v)
{
	return _byteswap_ushort(v);
}

STATIC_INLINE uae_u32 do_get_mem_word_unswapped(uae_u16 *a)
{
	return *a;
}

#define call_mem_get_func(func, addr) ((*func)(addr))
#define call_mem_put_func(func, addr, v) ((*func)(addr, v))

#endif
