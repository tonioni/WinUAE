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

#include <Avrt.h>

extern HANDLE AVTask;

uae_u32 getlocaltime (void)
{
	SYSTEMTIME st;
	FILETIME ft;
	ULARGE_INTEGER t;

	GetLocalTime (&st);
	SystemTimeToFileTime (&st, &ft);
	t.LowPart = ft.dwLowDateTime;
	t.HighPart = ft.dwHighDateTime;
	t.QuadPart -= 11644473600000 * 10000;
	return (uae_u32)(t.QuadPart / 10000000);
}

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

#if 0
static DWORD getattr (const TCHAR *name, LPFILETIME lpft, uae_s64 *size)
{
	HANDLE hFind;
	WIN32_FIND_DATA fd;

	if ((hFind = FindFirstFile (name, &fd)) == INVALID_HANDLE_VALUE) {
		fd.dwFileAttributes = GetFileAttributes (name);
		return fd.dwFileAttributes;
	}
	FindClose (hFind);

	if (lpft)
		*lpft = fd.ftLastWriteTime;
	if (size)
		*size = (((uae_u64)fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;

	return fd.dwFileAttributes;
}
#endif

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

int uae_sem_trywait_delay(uae_sem_t * event, int millis)
{
	int v = WaitForSingleObject(*event, millis);
	if (v == WAIT_OBJECT_0)
		return 0;
	if (v == WAIT_ABANDONED)
		return -2;
	return -1;
}

int uae_sem_trywait (uae_sem_t * event)
{
	return uae_sem_trywait_delay(event, 0);
}

void uae_sem_destroy (uae_sem_t * event)
{
	if (*event) {
		CloseHandle (*event);
		*event = NULL;
	}
}

uae_thread_id uae_thread_get_id(void)
{
	return (uae_thread_id)GetCurrentThreadId();
}

#ifndef _CONSOLE

typedef unsigned (__stdcall *BEGINTHREADEX_FUNCPTR)(void *);

struct thparms
{
	void *(*f)(void*);
	void *arg;
};

static unsigned __stdcall thread_init (void *f)
{
	struct thparms *thp = (struct thparms*)f;
	void *(*fp)(void*) = thp->f;
	void *arg = thp->arg;

	xfree (f);

#ifndef _CONSOLE
	__try {
		fp (arg);
#endif
#ifndef _CONSOLE
	} __except (WIN32_ExceptionFilter (GetExceptionInformation (), GetExceptionCode ())) {
	}
#endif
	return 0;
}

void uae_end_thread (uae_thread_id *tid)
{
	if (tid) {
		CloseHandle (*tid);
		*tid = NULL;
	}
}

typedef BOOL(WINAPI* AVSETMMTHREADPRIORITY)(HANDLE, AVRT_PRIORITY);
static AVSETMMTHREADPRIORITY pAvSetMmThreadPriority;

int uae_start_thread (const TCHAR *name, void *(*f)(void *), void *arg, uae_thread_id *tid)
{
	HANDLE hThread;
	int result = 1;
	unsigned foo;
	struct thparms *thp;

	if (!pAvSetMmThreadPriority && AVTask) {
		pAvSetMmThreadPriority = (AVSETMMTHREADPRIORITY)GetProcAddress(GetModuleHandle(_T("Avrt.dll")), "AvSetMmThreadPriority");
	}

	thp = xmalloc (struct thparms, 1);
	thp->f = f;
	thp->arg = arg;
	hThread = (HANDLE)_beginthreadex (NULL, 0, thread_init, thp, 0, &foo);
	if (hThread) {
		if (name) {
			//write_log (_T("Thread '%s' started (%d)\n"), name, hThread);
			if (!AVTask) {
				SetThreadPriority (hThread, THREAD_PRIORITY_HIGHEST);
			} else if (pAvSetMmThreadPriority) {
				pAvSetMmThreadPriority(AVTask, AVRT_PRIORITY_HIGH);
			}
		}
	} else {
		result = 0;
		write_log (_T("Thread '%s' failed to start!?\n"), name ? name : _T("<unknown>"));
	}
	if (tid)
		*tid = hThread;
	else
		CloseHandle (hThread);
	return result;
}

int uae_start_thread_fast (void *(*f)(void *), void *arg, uae_thread_id *tid)
{
	int v = uae_start_thread (NULL, f, arg, tid);
	if (*tid) {
		if (!AVTask) {
			SetThreadPriority (*tid, THREAD_PRIORITY_HIGHEST);
		} else if (pAvSetMmThreadPriority) {
			pAvSetMmThreadPriority(AVTask, AVRT_PRIORITY_HIGH);
		}
	}
	return v;
}

DWORD_PTR cpu_affinity = 1, cpu_paffinity = 1;

void uae_set_thread_priority (uae_thread_id *tid, int pri)
{
#if 0
	int pri2;
	HANDLE th;

	if (tid)
		th = *tid;
	else
		th = GetCurrentThread ();
	pri2 = GetThreadPriority (th);
	if (pri2 == THREAD_PRIORITY_ERROR_RETURN)
		pri2 = 0;
	if (pri > 0)
		pri2 = THREAD_PRIORITY_HIGHEST;
	else
		pri2 = THREAD_PRIORITY_ABOVE_NORMAL;
	pri2 += pri;
	if (pri2 > 1)
		pri2 = 1;
	if (pri2 < -1)
		pri2 = -1;
	SetThreadPriority (th, pri2);
#endif
	if (!AVTask) {
		if (!SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_HIGHEST))
			SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
	} else if (pAvSetMmThreadPriority) {
		pAvSetMmThreadPriority(AVTask, AVRT_PRIORITY_HIGH);
	}
}

uae_atomic atomic_and(volatile uae_atomic *p, uae_u32 v)
{
	return _InterlockedAnd(p, v);
}
uae_atomic atomic_or(volatile uae_atomic *p, uae_u32 v)
{
	return _InterlockedOr(p, v);
}
void atomic_set(volatile uae_atomic *p, uae_u32 v)
{
}
uae_atomic atomic_inc(volatile uae_atomic *p)
{
	return _InterlockedIncrement(p);
}
uae_atomic atomic_dec(volatile uae_atomic *p)
{
	return _InterlockedDecrement(p);
}

uae_u32 atomic_bit_test_and_reset(volatile uae_atomic *p, uae_u32 v)
{
	return _interlockedbittestandreset(p, v);
}
#endif

