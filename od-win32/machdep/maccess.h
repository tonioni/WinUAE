#ifndef __MACCESS_H__
#define __MACCESS_H__
 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * Memory access functions
  *
  * Copyright 1996 Bernd Schmidt
  */

#if defined( __GNUC__ ) && defined( X86_ASSEMBLY )

#define X86_PPRO_OPT

static __inline__ uae_u32 do_get_mem_long (uae_u32 *a)
{
    uae_u32 retval;

    __asm__ ("bswap %0" : "=r" (retval) : "0" (*a) : "cc");
    return retval;
}

static __inline__ uae_u32 do_get_mem_word (uae_u16 *a)
{
    uae_u32 retval;

#ifdef X86_PPRO_OPT
    __asm__ ("movzwl %w1,%k0\n\tshll $16,%k0\n\tbswap %k0\n" : "=&r" (retval) : "m" (*a) : "cc");
#else
    __asm__ ("xorl %k0,%k0\n\tmovw %w1,%w0\n\trolw $8,%w0" : "=&r" (retval) : "m" (*a) : "cc");
#endif
    return retval;
}

#define do_get_mem_byte(a) ((uae_u32)*(uae_u8 *)(a))

static __inline__ void do_put_mem_long (uae_u32 *a, uae_u32 v)
{
    __asm__ ("bswap %0" : "=r" (v) : "0" (v) : "cc");
    *a = v;
}

static __inline__ void do_put_mem_word (uae_u16 *a, uae_u32 v)
{
#ifdef X86_PPRO_OPT
    __asm__ ("bswap %0" : "=&r" (v) : "0" (v << 16) : "cc");
#else
    __asm__ ("rolw $8,%w0" : "=r" (v) : "0" (v) : "cc");
#endif
    *a = v;
}

#define do_put_mem_byte(a,v) (*(uae_u8 *)(a) = (v))

#if 0
static __inline__ uae_u32 call_mem_get_func(mem_get_func func, uae_cptr addr)
{
    uae_u32 result;
    __asm__("call %1"
	    : "=a" (result) : "r" (func), "a" (addr) : "cc", "edx", "ecx");
    return result;
}

static __inline__ void call_mem_put_func(mem_put_func func, uae_cptr addr, uae_u32 v)
{
    __asm__("call %2"
	    : : "a" (addr), "d" (v), "r" (func) : "cc", "eax", "edx", "ecx", "memory");
}
#else

#define call_mem_get_func(func,addr) ((*func)(addr))
#define call_mem_put_func(func,addr,v) ((*func)(addr,v))

#endif

#undef NO_INLINE_MEMORY_ACCESS
#undef MD_HAVE_MEM_1_FUNCS

#ifdef MD_HAVE_MEM_1_FUNCS
static __inline__ uae_u32 longget_1 (uae_cptr addr)
{
    uae_u32 result;

    __asm__ ("andl $0x00FFFFFF,%1\n"
	     "\tcmpb $0,(%1,%3)\n"
	     "\tleal 1f,%%ecx\n"
	     "\tje longget_stub\n"
	     "\taddl address_space,%1\n"
	     "\tmovl (%1),%0\n"
	     "\tbswap %0\n"
	     "\t1:"
	     : "=c" (result), "=d" (addr) : "1" (addr), "r" (good_address_map) : "cc");
    return result;
}
static __inline__ uae_u32 wordget_1 (uae_cptr addr)
{
    uae_u32 result;

    __asm__ ("andl $0x00FFFFFF,%1\n"
	     "\tcmpb $0,(%1,%3)\n"
	     "\tleal 1f,%%ecx\n"
	     "\tje wordget_stub\n"
	     "\taddl address_space,%1\n"
	     "\tmovzwl (%1),%0\n"
	     "\trolw $8,%w0\n"
	     "\t1:"
	     : "=c" (result), "=d" (addr) : "1" (addr), "r" (good_address_map) : "cc");
    return result;
}
static __inline__ uae_u32 byteget_1 (uae_cptr addr) 
{
    uae_u32 result;

    __asm__ ("andl $0x00FFFFFF,%1\n"
	     "\tcmpb $0,(%1,%3)\n"
	     "\tleal 1f,%%ecx\n"
	     "\tje byteget_stub\n"
	     "\taddl address_space,%1\n"
	     "\tmovzbl (%1),%0\n"
	     "\t1:"
	     : "=c" (result), "=d" (addr) : "1" (addr), "r" (good_address_map) : "cc");
    return result;
}
static __inline__ void longput_1 (uae_cptr addr, uae_u32 l)
{
    __asm__ __volatile__("andl $0x00FFFFFF,%0\n"
	     "\tcmpb $0,(%0,%3)\n"
	     "\tleal 1f,%%ecx\n"
	     "\tje longput_stub\n"
	     "\taddl address_space,%0\n"
	     "\tbswap %1\n"
	     "\tmovl %1,(%0)\n"
	     "\t1:"
	     : "=d" (addr), "=b" (l) : "0" (addr), "r" (good_address_map), "1" (l) : "cc", "memory", "ecx");
}
static __inline__ void wordput_1 (uae_cptr addr, uae_u32 w)
{
    __asm__ __volatile__("andl $0x00FFFFFF,%0\n"
	     "\tcmpb $0,(%0,%3)\n"
	     "\tleal 1f,%%ecx\n"
	     "\tje wordput_stub\n"
	     "\taddl address_space,%0\n"
	     "\trolw $8,%1\n"
	     "\tmovw %w1,(%0)\n"
	     "\t1:"
	     : "=d" (addr), "=b" (w) : "0" (addr), "r" (good_address_map), "1" (w) : "cc", "memory", "ecx");
}
static __inline__ void byteput_1 (uae_cptr addr, uae_u32 b)
{
    __asm__ __volatile__("andl $0x00FFFFFF,%0\n"
	     "\tcmpb $0,(%0,%3)\n"
	     "\tleal 1f,%%ecx\n"
	     "\tje byteput_stub\n"
	     "\taddl address_space,%0\n"
	     "\tmovb %b1,(%0)\n"
	     "\t1:"
	     : "=d" (addr), "=b" (b) : "0" (addr), "r" (good_address_map), "1" (b) : "cc", "memory", "ecx");
}

#endif

#define ALIGN_POINTER_TO32(p) ((~(unsigned long)(p)) & 3)

#else

static __inline__ uae_u32 do_get_mem_long(uae_u32 *a)
{
#ifndef _MSC_VER
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

static __inline__ uae_u16 do_get_mem_word(uae_u16 *a)
{
    uae_u8 *b = (uae_u8 *)a;
    
    return (*b << 8) | (*(b+1));
}

#define do_get_mem_byte(a) ((uae_u32)*(uae_u8 *)(a))

static __inline__ void do_put_mem_long(uae_u32 *a, uae_u32 v)
{
#ifndef _MSC_VER
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

static __inline__ void do_put_mem_word(uae_u16 *a, uae_u16 v)
{
    uae_u8 *b = (uae_u8 *)a;
    
    *b = v >> 8;
    *(b+1) = (uae_u8)v;
}

static __inline__ void do_put_mem_byte(uae_u8 *a, uae_u8 v)
{
    *a = v;
}

#define call_mem_get_func(func, addr) ((*func)(addr))
#define call_mem_put_func(func, addr, v) ((*func)(addr, v))

#undef NO_INLINE_MEMORY_ACCESS
#undef MD_HAVE_MEM_1_FUNCS

#endif

#endif