/*
* UAE - The Un*x Amiga Emulator
*
* UAE Native Interface (UNI)
*
* Copyright 2013-2014 Frode Solheim
*/

#ifndef _UAE_UNI_COMMON_H_
#define _UAE_UNI_COMMON_H_

#define UNI_VERSION 1

#ifndef UNI_MIN_VERSION
// MIN_UNI_VERSION decides which callback functions are declared available.
// The default, unless overriden is to required the current UNI version.
#define UNI_MIN_VERSION UNI_VERSION
#endif

#define UNI_ERROR_NOT_ENABLED            0x70000001
#define UNI_ERROR_INVALID_LIBRARY        0x70000002
#define UNI_ERROR_INVALID_FUNCTION       0x70000002
#define UNI_ERROR_FUNCTION_NOT_FOUND     0x70000003
#define UNI_ERROR_LIBRARY_NOT_FOUND      0x70000004
#define UNI_ERROR_INVALID_FLAGS          0x70000005
#define UNI_ERROR_COULD_NOT_OPEN_LIBRARY 0x70000006
#define UNI_ERROR_ILLEGAL_LIBRARY_NAME   0x70000007
#define UNI_ERROR_LIBRARY_TOO_OLD        0x70000008
#define UNI_ERROR_INIT_FAILED            0x70000009
#define UNI_ERROR_INTERFACE_TOO_OLD      0x7000000a

// these errors are only return from the Amiga-side uni wrappers
#define UNI_ERROR_AMIGALIB_NOT_OPEN      0x71000000

// On all current platforms, char, short and int short be 1, 2 and 4 bytes
// respectively, so we just use these simple types.

typedef char           uni_char;
typedef unsigned char  uni_uchar;
typedef short          uni_short;
typedef unsigned short uni_ushort;
typedef int            uni_long;
typedef unsigned int   uni_ulong;

typedef int (UNICALL *uni_version_function)();
typedef void * (UNICALL *uni_resolve_function)(uni_ulong ptr);
typedef const uni_char * (UNICALL *uni_uae_version_function)(void);

struct uni {
    uni_long d1;
    uni_long d2;
    uni_long d3;
    uni_long d4;
    uni_long d5;
    uni_long d6;
    uni_long d7;
    uni_long a1;
    uni_long a2;
    uni_long a3;
    uni_long a4;
    uni_long a5;
    uni_long a7;

#if UNI_VERSION >= 2
    // add new struct entries for version 2 here
#endif

#ifdef CPUEMU_0  // UAE
    uni_long result;
    uni_long error;
    uni_ulong function;
    uni_ulong library;
    void *native_function;
    void *uaevar_compat;
    int flags;
    uaecptr task;
#endif
};

#endif // _UAE_UNI_COMMON_H_
