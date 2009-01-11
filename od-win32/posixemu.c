/*
 * UAE - The Un*x Amiga Emulator
 *
 * Win32 interface
 *
 * Copyright 1997 Mathias Ortmann
 */

#include "sysconfig.h"
#include "sysdeps.h"

#include <windows.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ddraw.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <io.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <sys/utime.h>
#include <process.h>

#include "options.h"
#include "posixemu.h"
#include "threaddep/thread.h"
#include "filesys.h"
#include "win32.h"

/* Our Win32 implementation of this function */
void gettimeofday (struct timeval *tv, void *blah)
{
#if 1
    struct timeb time;

    ftime (&time);

    tv->tv_sec = time.time;
    tv->tv_usec = time.millitm * 1000;
#else
    SYSTEMTIME st;
    FILETIME ft;
    uae_u64 v, sec;
    GetSystemTime (&st);
    SystemTimeToFileTime (&st, &ft);
    v = (ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    v /= 10;
    sec = v / 1000000;
    tv->tv_usec = (unsigned long)(v - (sec * 1000000));
    tv->tv_sec = (unsigned long)(sec - 11644463600);
#endif
}

/* convert time_t to/from AmigaDOS time */
#define secs_per_day (24 * 60 * 60)
#define diff ((8 * 365 + 2) * secs_per_day)

static void get_time(time_t t, long *days, long *mins, long *ticks)
{
    /* time_t is secs since 1-1-1970 */
    /* days since 1-1-1978 */
    /* mins since midnight */
    /* ticks past minute @ 50Hz */

    t -= diff;
    *days = t / secs_per_day;
    t -= *days * secs_per_day;
    *mins = t / 60;
    t -= *mins * 60;
    *ticks = t * 50;
}

static DWORD getattr(const char *name, LPFILETIME lpft, size_t *size)
{
    HANDLE hFind;
    WIN32_FIND_DATA fd;

    if ((hFind = FindFirstFile (name,&fd)) == INVALID_HANDLE_VALUE) {
	fd.dwFileAttributes = GetFileAttributes (name);
	return fd.dwFileAttributes;
    }
    FindClose (hFind);

    if (lpft)
	*lpft = fd.ftLastWriteTime;
    if (size)
	*size = fd.nFileSizeLow;

    return fd.dwFileAttributes;
}

int posixemu_stat(const char *name, struct stat *statbuf)
{
    DWORD attr;
    FILETIME ft, lft;

    if ((attr = getattr (name,&ft,(size_t*)&statbuf->st_size)) == (DWORD)~0) {
	return -1;
    } else {
	statbuf->st_mode = (attr & FILE_ATTRIBUTE_READONLY) ? FILEFLAG_READ : FILEFLAG_READ | FILEFLAG_WRITE;
	if (attr & FILE_ATTRIBUTE_ARCHIVE)
	    statbuf->st_mode |= FILEFLAG_ARCHIVE;
	if (attr & FILE_ATTRIBUTE_DIRECTORY)
	    statbuf->st_mode |= FILEFLAG_DIR;
	FileTimeToLocalFileTime (&ft,&lft);
	statbuf->st_mtime = (long)((*(__int64 *)&lft-((__int64)(369*365+89)*(__int64)(24*60*60)*(__int64)10000000))/(__int64)10000000);
    }
    return 0;
}

int posixemu_chmod(const char *name, int mode)
{
    DWORD attr = FILE_ATTRIBUTE_NORMAL;
    if (!(mode & FILEFLAG_WRITE))
	attr |= FILE_ATTRIBUTE_READONLY;
    if (mode & FILEFLAG_ARCHIVE)
	attr |= FILE_ATTRIBUTE_ARCHIVE;
    if (SetFileAttributes(name,attr))
	return 1;
    return -1;
}

static void tmToSystemTime(struct tm *tmtime, LPSYSTEMTIME systime)
{
    if (tmtime == NULL) {
	GetSystemTime (systime);
    } else {
	systime->wDay       = tmtime->tm_mday;
	systime->wDayOfWeek = tmtime->tm_wday;
	systime->wMonth     = tmtime->tm_mon + 1;
	systime->wYear      = tmtime->tm_year + 1900;
	systime->wHour      = tmtime->tm_hour;
	systime->wMinute    = tmtime->tm_min;
	systime->wSecond    = tmtime->tm_sec;
	systime->wMilliseconds = 0;
    }
}

static int setfiletime (const char *name, unsigned int days, int minute, int tick, int tolocal)
{
    FILETIME LocalFileTime, FileTime;
    HANDLE hFile;
    int success;
    if ((hFile = CreateFile (name, GENERIC_WRITE,FILE_SHARE_READ | FILE_SHARE_WRITE,NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, NULL)) == INVALID_HANDLE_VALUE)
	return 0;

    *(__int64 *)&LocalFileTime = (((__int64)(377*365+91+days)*(__int64)1440+(__int64)minute)*(__int64)(60*50)+(__int64)tick)*(__int64)200000;

    if (tolocal) {
	if (!LocalFileTimeToFileTime (&LocalFileTime,&FileTime))
	    FileTime = LocalFileTime;
    } else {
	FileTime = LocalFileTime;
    }

    success = SetFileTime (hFile,&FileTime,&FileTime,&FileTime);
    CloseHandle (hFile);

    return success;
}

int posixemu_utime (const char *name, struct utimbuf *ttime)
{
    int result = -1, tolocal;
    long days, mins, ticks;
    time_t actime;

    if (!ttime) {
	actime = time (NULL);
	tolocal = 0;
    } else {
	tolocal = 1;
	actime = ttime->actime;
    }
    get_time (actime, &days, &mins, &ticks);

    if (setfiletime (name, days, mins, ticks, tolocal))
	result = 0;

	return result;
}

void uae_sem_init (uae_sem_t * event, int manual_reset, int initial_state)
{
    if(*event) {
	if (initial_state)
	    SetEvent (*event);
	else
	    ResetEvent (*event);
    } else {
	*event = CreateEvent (NULL, manual_reset, initial_state, NULL);
    }
}

void uae_sem_wait (uae_sem_t * event)
{
    WaitForSingleObject (*event, INFINITE);
}

void uae_sem_post (uae_sem_t * event)
{
    SetEvent (*event);
}

int uae_sem_trywait (uae_sem_t * event)
{
    return WaitForSingleObject (*event, 0) == WAIT_OBJECT_0 ? 0 : -1;
}

void uae_sem_destroy (uae_sem_t * event)
{
    if (*event) {
	CloseHandle (*event);
	*event = NULL;
    }
}

typedef unsigned (__stdcall *BEGINTHREADEX_FUNCPTR)(void *);

struct thparms
{
    void *(*f)(void*);
    void *arg;
};

static unsigned __stdcall thread_init (void *f)
{
    struct thparms *thp = f;
    void *(*fp)(void*) = thp->f;
    void *arg = thp->arg;

    xfree (f);
    __try {
	fp (arg);
    } __except (WIN32_ExceptionFilter (GetExceptionInformation (), GetExceptionCode ())) {
    }
    return 0;
}

void uae_end_thread (uae_thread_id *tid)
{
    if (tid) {
	CloseHandle (*tid);
	*tid = NULL;
    }
}

int uae_start_thread (char *name, void *(*f)(void *), void *arg, uae_thread_id *tid)
{
    HANDLE hThread;
    int result = 1;
    unsigned foo;
    struct thparms *thp;

    thp = xmalloc (sizeof (struct thparms));
    thp->f = f;
    thp->arg = arg;
    hThread = (HANDLE)_beginthreadex (NULL, 0, thread_init, thp, 0, &foo);
    if (hThread) {
	SetThreadPriority (hThread, THREAD_PRIORITY_ABOVE_NORMAL);
	if (name)
	    write_log ("Thread '%s' started (%d)\n", name, hThread);
    } else {
	result = 0;
	write_log ("Thread '%s' failed to start!?\n", name ? name : "<unknown>");
    }
    if (tid)
	*tid = hThread;
    else
	CloseHandle (hThread);
    return result;
}

int uae_start_thread_fast (void *(*f)(void *), void *arg, uae_thread_id *tid)
{
    return uae_start_thread (NULL, f, arg, tid);
}

DWORD_PTR cpu_affinity = 1, cpu_paffinity = 1;

void uae_set_thread_priority (int pri)
{
}

