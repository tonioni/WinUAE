/*
 * UAE core integer and character types.
 *
 * Every translation unit in the emulator gets these, usually after
 * sysdeps.h, sometimes before it (mame/mameglue.h includes this header
 * first). The underlying types are fixed and must match the typedefs that
 * sysdeps.h makes for the 8, 16 and 32-bit names. sysdeps.h also defines
 * uae_s64/uae_u64 as macros expanding to long long / unsigned long long,
 * and owns VAL64/UVAL64, so this header only typedefs the 64-bit names
 * when no such macro exists and never defines VAL64/UVAL64.
 *
 * This header is self-contained: it must compile without sysconfig.h,
 * without UAE defined and with none of the SIZEOF_* macros present, so it
 * never tests SIZEOF_* and never includes emulator headers.
 *
 * Copyright (C) 2026 Dimitris Panokostas
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Clean-room implementation written from a functional description of the
 * UAE portability interface.
 */

#ifndef UAE_TYPES_H
#define UAE_TYPES_H

#include <cstdint>

/* TCHAR model. On Windows <tchar.h> provides TCHAR, which is wchar_t in
 * UNICODE builds (WinUAE) and char otherwise. Elsewhere TCHAR is char,
 * as in od-unix/tchar.h. */
#ifdef _WIN32
#include <tchar.h>
#ifdef UNICODE
#define SIZEOF_TCHAR 2
#else
#define SIZEOF_TCHAR 1
#endif
#else
typedef char TCHAR;
#define SIZEOF_TCHAR 1
#endif

/* _T(x) normally comes from <tchar.h> or od-unix/tchar.h; define it only
 * if it is still missing, in the same form those headers use. */
#ifndef _T
#if SIZEOF_TCHAR == 1
#define _T(x) x
#else
#define _T(x) L##x
#endif
#endif

/* Fixed-width emulator types. short/int/long long are 16/32/64 bits on
 * every supported target (ILP32, LP64 and LLP64 alike). */
typedef signed char uae_s8;
typedef unsigned char uae_u8;
typedef signed short uae_s16;
typedef unsigned short uae_u16;
typedef signed int uae_s32;
typedef unsigned int uae_u32;
/* sysdeps.h may already have defined these as macros of the same types. */
#ifndef uae_s64
typedef long long uae_s64;
#endif
#ifndef uae_u64
typedef unsigned long long uae_u64;
#endif

typedef char uae_char;

/* Amiga address (32-bit address space, even on 64-bit hosts) */
typedef uae_u32 uaecptr;

/* Emulator cycle counter (events.h) */
typedef signed long long evt_t;

/* Booleans. FALSE must be exactly 0 because mameglue.h redefines it
 * unguarded. */
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

/* Win32-style sized integers used by the MAME-derived sources. Windows
 * provides this group via <basetsd.h> (pulled in by <windows.h>); if that
 * header comes later it repeats the same types, which is valid. */
#ifndef _BASETSD_H_
typedef signed char INT8;
typedef signed short INT16;
typedef signed int INT32;
typedef signed long long INT64;
typedef unsigned char UINT8;
typedef unsigned short UINT16;
typedef unsigned int UINT32;
typedef unsigned long long UINT64;
#endif

#ifdef __cplusplus
static_assert(sizeof(uae_s8) == 1 && sizeof(uae_u8) == 1, "uae_s8/uae_u8 must be 8 bit");
static_assert(sizeof(uae_s16) == 2 && sizeof(uae_u16) == 2, "uae_s16/uae_u16 must be 16 bit");
static_assert(sizeof(uae_s32) == 4 && sizeof(uae_u32) == 4, "uae_s32/uae_u32 must be 32 bit");
static_assert(sizeof(uae_s64) == 8 && sizeof(uae_u64) == 8, "uae_s64/uae_u64 must be 64 bit");
static_assert(sizeof(evt_t) == 8, "evt_t must be 64 bit");
#endif

#endif /* UAE_TYPES_H */
