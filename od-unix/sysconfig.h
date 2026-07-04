#ifndef WINUAE_OD_UNIX_SYSCONFIG_H
#define WINUAE_OD_UNIX_SYSCONFIG_H

#define UAE_TARGET_UNIX 1

#if defined(__APPLE__)
#define UAE_HOST_DARWIN 1
#define MACOSX 1
#elif defined(__linux__)
#define UAE_HOST_LINUX 1
#endif

#if defined(__linux__) && (defined(__x86_64__) || defined(__i386__))
#define HAVE_STRUCT_UCONTEXT_UC_MCONTEXT_GREGS 1
#endif

#if !defined(ARM64) && !defined(_M_ARM64) && !defined(__aarch64__) && \
    (defined(__x86_64__) || defined(__amd64__) || defined(_M_X64) || \
     defined(_M_IX86) || defined(i386) || defined(__i386) || \
     defined(__i386__) || defined(_X86_))
#define PCEM_VOODOO_CODEGEN 1
#endif

#define SUPPORT_THREADS 1
#define WITH_THREADED_CPU 1
#define MAX_DPATH 1000
#define MAX_AMIGAMONITORS 4
#define MAX_AMIGADISPLAYS 4
#define NATMEM_OFFSET natmem_offset
#define PACKAGE_STRING "WinUAE Unix"
#if defined(UAE_HOST_DARWIN)
#define WINUAE_UNIX_WINDOW_TITLE "WinUAE for macOS"
#elif defined(UAE_HOST_LINUX)
#define WINUAE_UNIX_WINDOW_TITLE "WinUAE for Linux"
#else
#define WINUAE_UNIX_WINDOW_TITLE "WinUAE for Unix"
#endif

#define GFXFILTER 1
#define FILESYS 1
#define UAE_FILESYS_THREADS 1
#define AUTOCONFIG 1
#define DEBUGGER 1
#define ECS_DENISE 1
#define AGA 1
#define CD32 1
#define CDTV 1
#define FPUEMU 1
#define FPU_UAE 1
#define MMUEMU 1
#define FULLMMU 1
#define CPUEMU_0 1
#define CPUEMU_11 1
#define CPUEMU_13 1
#define CPUEMU_20 1
#define CPUEMU_21 1
#define CPUEMU_22 1
#define CPUEMU_23 1
#define CPUEMU_24 1
#define CPUEMU_31 1
#define CPUEMU_32 1
#define CPUEMU_33 1
#define CPUEMU_34 1
#define CPUEMU_35 1
#define CPUEMU_40 1
#define CPUEMU_50 1
#define ACTION_REPLAY 1
#define SAVESTATE 1
#define A2091 1
#define PICASSO96_SUPPORTED 1
#define PICASSO96 1
#define SCP 1
#define FDI2RAW 1
#define ARCADIA 1
#define AMAX 1
#define WITH_SOFTFLOAT 1
#define DRIVESOUND 1
#define PARALLEL_PORT 1
#define SERIAL_PORT 1

#define CAN_PRINTF_LONG_LONG 1
#define RETSIGTYPE void
#define TIME_WITH_SYS_TIME 1

#define HAVE_DIRENT_H 1
#define HAVE_FCNTL_H 1
#define HAVE_GETCWD 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_INET_ATON 1
#define HAVE_MKDIR 1
#define HAVE_RMDIR 1
#define HAVE_SELECT 1
#define HAVE_STRDUP 1
#define HAVE_STRERROR 1
#define HAVE_STDINT_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_SYS_MMAN_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define HAVE_UTIME_H 1
#define HAVE_VFPRINTF 1
#define HAVE_VPRINTF 1
#define HAVE_VSPRINTF 1

#define SIZEOF_CHAR 1
#define SIZEOF_SHORT 2
#define SIZEOF_INT 4
#if defined(__LP64__)
#define SIZEOF_LONG 8
#define SIZEOF_VOID_P 8
#define CPU_64_BIT 1
#else
#define SIZEOF_LONG 4
#define SIZEOF_VOID_P 4
#endif
#define SIZEOF_LONG_LONG 8
#define SIZEOF_FLOAT 4
#define SIZEOF_DOUBLE 8

#define LT_MODULE_EXT _T(".dylib")
#if defined(UAE_HOST_LINUX)
#undef LT_MODULE_EXT
#define LT_MODULE_EXT _T(".so")
#endif

#define FSDB_DIR_SEPARATOR '/'
#define FSDB_DIR_SEPARATOR_S _T("/")

#ifndef PATH_MAX
#define PATH_MAX MAX_DPATH
#endif
#ifndef MAX_PATH
#define MAX_PATH MAX_DPATH
#endif
#ifndef MAXINT
#define MAXINT INT_MAX
#endif

#define UAE_RAND_MAX 2147483647
#define ZEXPORT
#ifndef __cdecl
#define __cdecl
#endif

#include <stdint.h>
#include <limits.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <time.h>

typedef long uae_atomic;
typedef int boolean;

#ifdef GFXBOARD
typedef int8_t INT8;
typedef uint8_t UINT8;
typedef int16_t INT16;
typedef uint16_t UINT16;
typedef int32_t INT32;
typedef uint32_t UINT32;
typedef signed long long INT64;
typedef unsigned long long UINT64;
#endif

#define BYTE int8_t
#define WORD int16_t
#define UBYTE uint8_t
#define UWORD uint16_t
#define USHORT uint16_t
#define ULONG uint32_t
#define DWORD uint32_t
#define UINT uint32_t
#define BOOL int
#define HANDLE void*
#define HWND void*
#define HINSTANCE void*
#define HMODULE void*
#define WPARAM uintptr_t
#define LPARAM intptr_t
#define LRESULT intptr_t
#define INT_PTR intptr_t
#define CALLBACK
#define WINAPI
#define PASCAL
#define _cdecl
#define _stdcall
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)

#ifdef WINUAE_UNIX_WITH_PCEM
#ifndef INFINITE
#define INFINITE 0xffffffffU
#endif
#ifndef WAIT_OBJECT_0
#define WAIT_OBJECT_0 0
#endif
#ifndef WAIT_TIMEOUT
#define WAIT_TIMEOUT 258
#endif
HANDLE CreateSemaphore(void*, int, int, const char*);
DWORD WaitForSingleObject(HANDLE, DWORD);
BOOL ReleaseSemaphore(HANDLE, int, void*);
BOOL CloseHandle(HANDLE);
#endif

#define Sleep sleep_millis
#define stricmp strcasecmp
#define strnicmp strncasecmp
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define _tzset tzset
#define _ftime ftime
#define _timeb timeb
#define _istalnum isalnum
#define _daylight daylight

static inline long uae_unix_timezone_offset(void)
{
    time_t now = time(NULL);
    struct tm local_tm;
    struct tm gm_tm;
    localtime_r(&now, &local_tm);
    gmtime_r(&now, &gm_tm);
    return (long)difftime(mktime(&gm_tm), mktime(&local_tm));
}

#define _timezone uae_unix_timezone_offset()

#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef O_NDELAY
#define O_NDELAY O_NONBLOCK
#endif

#define FILEFLAG_WRITE S_IWUSR
#define FILEFLAG_READ S_IRUSR
#define FILEFLAG_EXECUTE S_IXUSR
#define FILEFLAG_DIR S_IFDIR

#define UAESCSI_CDEMU 0
#define UAESCSI_SPTI 1
#define UAESCSI_SPTISCAN 2
#define UAESCSI_LAST 2
#define UAESCSI_ASPI_FIRST 3
#define UAESCSI_ADAPTECASPI 3
#define UAESCSI_NEROASPI 4
#define UAESCSI_FROGASPI 5

#define DRIVE_CDROM 5

#endif /* WINUAE_OD_UNIX_SYSCONFIG_H */
