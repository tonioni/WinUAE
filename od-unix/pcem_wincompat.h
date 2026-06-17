#ifndef WINUAE_OD_UNIX_PCEM_WINCOMPAT_H
#define WINUAE_OD_UNIX_PCEM_WINCOMPAT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
#include <cstdlib>
#endif

#ifndef UAE
#define UAE
#endif

#ifndef __cdecl
#define __cdecl
#endif

#ifndef container_of
#define container_of(address, type, field) \
	((type *)((char *)(address) - offsetof(type, field)))
#endif

#if !defined(_MSC_VER)
static inline uint16_t _byteswap_ushort(uint16_t v)
{
	return __builtin_bswap16(v);
}

static inline uint32_t _byteswap_ulong(uint32_t v)
{
	return __builtin_bswap32(v);
}
#endif

#endif
