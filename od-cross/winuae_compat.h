#ifndef UAE_OD_FS_WINUAE_COMPAT_H
#define UAE_OD_FS_WINUAE_COMPAT_H

#ifdef WINDOWS

// Include windef.h now to get RECT and DWORD defined (and not collide with
// later includes of windows.h

#include "windef.h"
#include "windows.h"

#endif

// Use custom versions of these functions for platform-specific behaviour,
// for example uae_tfopen may perform charset conversion before opening the
// file.

#define _tfopen fopen
#define uae_tfopen fopen
#define _ftelli64 ftello64
#define _fseeki64 fseeko64

// convert windows libc names to standard libc function names, and also
// use char functions instead of wchar string functions.

#include "uae/string.h"

#define stricmp strcasecmp
#define strnicmp strncasecmp

#define _wunlink unlink

//#define _timezone timezone
//#define _daylight daylight
#ifdef WINDOWS

#else
extern int _timezone;
extern int _daylight;
#endif

#define _tzset tzset

#define _istalnum isalnum

// needed by e.g drawing.cpp

// #define NOINLINE

#ifndef WINDOWS

#define _ftime ftime
#define _timeb timeb

#define _cdecl

#ifndef ULONG
#define ULONG unsigned long
#endif

//typedef unsigned int UAE_DWORD;
typedef unsigned int DWORD;

typedef struct tagRECT {
    int left;
    int top;
    int right;
    int bottom;
} RECT, *PRECT, *PPRECT;

//#ifndef WINDOWS
//#define DWORD UAE_DWORD
//#define RECT UAE_RECT
//#endif
#endif

// #define STATIC_INLINE static inline

#endif // UAE_OD_FS_WINUAE_COMPAT_H
