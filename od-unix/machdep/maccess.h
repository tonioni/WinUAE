#ifndef WINUAE_OD_UNIX_MACHDEP_MACCESS_H
#define WINUAE_OD_UNIX_MACHDEP_MACCESS_H

#include "uae/types.h"

static inline uae_u16 uae_bswap16(uae_u16 v)
{
    return __builtin_bswap16(v);
}

static inline uae_u32 uae_bswap32(uae_u32 v)
{
    return __builtin_bswap32(v);
}

static inline uae_u64 uae_bswap64(uae_u64 v)
{
    return __builtin_bswap64(v);
}

static inline uae_u8 do_get_mem_byte(uae_u8 *a)
{
    return *a;
}

static inline uae_u16 do_get_mem_word(uae_u16 *a)
{
    uae_u16 v;
    __builtin_memcpy(&v, a, sizeof(v));
    return uae_bswap16(v);
}

static inline uae_u32 do_get_mem_long(uae_u32 *a)
{
    uae_u32 v;
    __builtin_memcpy(&v, a, sizeof(v));
    return uae_bswap32(v);
}

static inline uae_u64 do_get_mem_quad(uae_u64 *a)
{
    uae_u64 v;
    __builtin_memcpy(&v, a, sizeof(v));
    return uae_bswap64(v);
}

static inline void do_put_mem_byte(uae_u8 *a, uae_u8 v)
{
    *a = v;
}

static inline void do_put_mem_word(uae_u16 *a, uae_u16 v)
{
    v = uae_bswap16(v);
    __builtin_memcpy(a, &v, sizeof(v));
}

static inline void do_put_mem_long(uae_u32 *a, uae_u32 v)
{
    v = uae_bswap32(v);
    __builtin_memcpy(a, &v, sizeof(v));
}

static inline void do_put_mem_quad(uae_u64 *a, uae_u64 v)
{
    v = uae_bswap64(v);
    __builtin_memcpy(a, &v, sizeof(v));
}

static inline uae_u16 do_byteswap_16(uae_u16 v) { return uae_bswap16(v); }
static inline uae_u32 do_byteswap_32(uae_u32 v) { return uae_bswap32(v); }
static inline uae_u64 do_byteswap_64(uae_u64 v) { return uae_bswap64(v); }

static inline uae_u32 do_get_mem_word_unswapped(uae_u16 *a)
{
    return *a;
}

#define ALIGN_POINTER_TO32(p) ((~(uintptr_t)(p)) & 3)
#define call_mem_get_func(func, addr) ((*func)(addr))
#define call_mem_put_func(func, addr, v) ((*func)(addr, v))

#endif /* WINUAE_OD_UNIX_MACHDEP_MACCESS_H */
