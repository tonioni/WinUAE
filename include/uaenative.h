/*
* UAE - The Un*x Amiga Emulator
*
* UAE Native Interface (UNI)
*
* Copyright 2013-2014 Frode Solheim
*/

#ifndef _UAE_UAENATIVE_H_
#define _UAE_UAENATIVE_H_

#ifdef WITH_UAENATIVE

#if defined(_WIN32) || defined(WINDOWS)

    #define UNIAPI __declspec(dllimport)
    #define UNICALL __cdecl

#else // _WIN32 not defined

    #define UNIAPI
    #define UNICALL

#endif

#include "uni_common.h"

#define UNI_FLAG_ASYNCHRONOUS 1
#define UNI_FLAG_COMPAT 2
#define UNI_FLAG_NAMED_FUNCTION 4

uae_u32 uaenative_open_library(TrapContext *context, int flags);
uae_u32 uaenative_get_function(TrapContext *context, int flags);
uae_u32 uaenative_call_function(TrapContext *context, int flags);
uae_u32 uaenative_close_library(TrapContext *context, int flags);

void *uaenative_get_uaevar(void);

void uaenative_install ();
uaecptr uaenative_startup (uaecptr resaddr);

/* This function must return a list of directories to look for native
 * libraries in. The returned list must be NULL-terminated, and must not
 * be de-allocated. */
const TCHAR **uaenative_get_library_dirs(void);

#endif // WITH_UAENATIVE

#endif // _UAE_UAENATIVE_H_
