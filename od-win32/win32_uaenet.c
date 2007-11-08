/*
 * UAE - The Un*x Amiga Emulator
 *
 * Win32 uaenet emulation
 *
 * Copyright 1997 Mathias Ortmann
 * Copyright 1998-1999 Brian King - added MIDI output support
 */

#include "sysconfig.h"
#include <windows.h>
#include <winspool.h>
#include <stdlib.h>
#include <stdarg.h>
#include <mmsystem.h>
#include <ddraw.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <io.h>

#include <setupapi.h>
#include <windows.h>

#include "sysdeps.h"
#include "options.h"

#include "threaddep/thread.h"
#include "win32_uaenet.h"

struct uaenetdatawin32
{
    HANDLE hCom;
    HANDLE evtr, evtw, evtt, evtwce;
    OVERLAPPED olr, olw, olwce;
    int writeactive;
    void *readdata, *writedata;
    volatile int threadactive;
    uae_thread_id tid;
    uae_sem_t change_sem, sync_sem;
    void *user;
};

int uaenet_getdatalenght (void)
{
    return sizeof (struct uaenetdatawin32);
}

static void uaeser_initdata (struct uaenetdatawin32 *sd, void *user)
{
    memset (sd, 0, sizeof (struct uaenetdatawin32));
    sd->hCom = INVALID_HANDLE_VALUE;
    sd->evtr = sd->evtw = sd->evtt = sd->evtwce = 0;
    sd->user = user;
}

static void *uaenet_trap_thread (void *arg)
{
    struct uaenetdatawin32 *sd = arg;
    HANDLE handles[4];
    int cnt, actual;
    DWORD evtmask;

    uae_set_thread_priority (2);
    sd->threadactive = 1;
    uae_sem_post (&sd->sync_sem);
    while (sd->threadactive == 1) {
	int sigmask = 0;
	uae_sem_wait (&sd->change_sem);
	if (WaitForSingleObject(sd->evtwce, 0) == WAIT_OBJECT_0) {
	    if (evtmask & EV_RXCHAR)
		sigmask |= 1;
	    if ((evtmask & EV_TXEMPTY) && !sd->writeactive)
		sigmask |= 2;
	    //startwce(sd, &evtmask);
	}
	cnt = 0;
	handles[cnt++] = sd->evtt;
	handles[cnt++] = sd->evtwce;
	if (sd->writeactive) {
	    if (GetOverlappedResult (sd->hCom, &sd->olw, &actual, FALSE)) {
		sd->writeactive = 0;
		sigmask |= 2;
	    } else {
		handles[cnt++] = sd->evtw;
	    }
	}
	if (!sd->writeactive)
	    sigmask |= 2;
	uaenet_signal (sd->user, sigmask | 1);
	uae_sem_post (&sd->change_sem);
	WaitForMultipleObjects(cnt, handles, FALSE, INFINITE);
    }
    sd->threadactive = 0;
    uae_sem_post (&sd->sync_sem);
    return 0;
}

void uaenet_trigger (struct uaenetdatawin32 *sd)
{
    SetEvent (sd->evtt);
}

int uaenet_write (struct uaenetdatawin32 *sd, uae_u8 *data, uae_u32 len)
{
    int ret = 1;
    if (!WriteFile (sd->hCom, data, len, NULL, &sd->olw)) {
	sd->writeactive = 1;
	if (GetLastError() != ERROR_IO_PENDING) {
	    ret = 0;
	    sd->writeactive = 0;
	}
    }
    SetEvent (sd->evtt);
    return ret;
}

int uaenet_read (struct uaenetdatawin32 *sd, uae_u8 *data, uae_u32 len)
{
    int ret = 1;
    DWORD err;

    if (!ReadFile (sd->hCom, data, len, NULL, &sd->olr)) {
	if (GetLastError() == ERROR_IO_PENDING)
	    WaitForSingleObject(sd->evtr, INFINITE);
	else
	    ret = 0;
    }
    SetEvent (sd->evtt);
    return ret;
}

int uaenet_open (struct uaenetdatawin32 *sd, void *user, int unit)
{
    char buf[256];

    sd->user = user;
    sprintf (buf, "\\.\\\\COM%d", unit);
    sd->evtr = CreateEvent (NULL, TRUE, FALSE, NULL);
    sd->evtw = CreateEvent (NULL, TRUE, FALSE, NULL);
    sd->evtt = CreateEvent (NULL, FALSE, FALSE, NULL);
    sd->evtwce = CreateEvent (NULL, TRUE, FALSE, NULL);
    if (!sd->evtt || !sd->evtw || !sd->evtt || !sd->evtwce)
	goto end;
    sd->olr.hEvent = sd->evtr;
    sd->olw.hEvent = sd->evtw;
    sd->olwce.hEvent = sd->evtwce;
    sd->hCom = CreateFile (buf, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
    if (sd->hCom == INVALID_HANDLE_VALUE) {
	write_log ("UAENET: '%s' failed to open, err=%d\n", buf, GetLastError());
	goto end;
    }
    uae_sem_init (&sd->sync_sem, 0, 0);
    uae_sem_init (&sd->change_sem, 0, 1);
    uae_start_thread ("uaenet_win32", uaenet_trap_thread, sd, &sd->tid);
    uae_sem_wait (&sd->sync_sem);

    return 1;

end:
    uaenet_close (sd);
    return 0;
}

void uaenet_close (struct uaenetdatawin32 *sd)
{
    if (sd->threadactive) {
	sd->threadactive = -1;
	SetEvent (sd->evtt);
	while (sd->threadactive)
	    Sleep(10);
	CloseHandle (sd->evtt);
    }
    if (sd->hCom != INVALID_HANDLE_VALUE)
	CloseHandle(sd->hCom);
    if (sd->evtr)
	CloseHandle(sd->evtr);
    if (sd->evtw)
	CloseHandle(sd->evtw);
    if (sd->evtwce)
	CloseHandle(sd->evtwce);
    uaeser_initdata (sd, sd->user);
}
