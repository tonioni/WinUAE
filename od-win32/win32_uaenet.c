/*
 * UAE - The Un*x Amiga Emulator
 *
 * Win32 uaenet emulation
 *
 * Copyright 2007 Toni Wilen
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
#include "tun_uae.h"

struct uaenetdatawin32
{
    HANDLE hCom;
    HANDLE evtr, evtw, evtt;
    OVERLAPPED olr, olw;
    int writeactive;
    void *readdata, *writedata;
    volatile int threadactive;
    uae_thread_id tid;
    uae_sem_t change_sem, sync_sem;
    void *user;
    struct tapdata *tc;
    uae_u8 *readbuffer;
    uae_u8 *writebuffer;
    int mtu;
};

int uaenet_getdatalenght (void)
{
    return sizeof (struct uaenetdatawin32);
}

static void uaeser_initdata (struct uaenetdatawin32 *sd, void *user)
{
    memset (sd, 0, sizeof (struct uaenetdatawin32));
    sd->hCom = INVALID_HANDLE_VALUE;
    sd->evtr = sd->evtw = sd->evtt = 0;
    sd->user = user;
}

static void *uaenet_trap_thread (void *arg)
{
    struct uaenetdatawin32 *sd = arg;
    HANDLE handles[4];
    int cnt, towrite;
    int readactive, writeactive;
    DWORD actual;

    uae_set_thread_priority (2);
    sd->threadactive = 1;
    uae_sem_post (&sd->sync_sem);
    readactive = 0;
    writeactive = 0;
    while (sd->threadactive == 1) {
	int donotwait = 0;

	uae_sem_wait (&sd->change_sem);

	if (readactive) {
	    if (GetOverlappedResult (sd->hCom, &sd->olr, &actual, FALSE)) {
		readactive = 0;
		uaenet_gotdata (sd->user, sd->readbuffer, actual);
		donotwait = 1;
	    }
	}
	if (writeactive) {
	    if (GetOverlappedResult (sd->hCom, &sd->olw, &actual, FALSE)) {
		writeactive = 0;
		donotwait = 1;
	    }
	}

	if (!readactive) {
	    if (!ReadFile (sd->hCom, sd->readbuffer, sd->mtu, &actual, &sd->olr)) {
		DWORD err = GetLastError();
		if (err == ERROR_IO_PENDING)
		    readactive = 1;
	    } else {
		uaenet_gotdata (sd->user, sd->readbuffer, actual);
		donotwait = 1;
	    }
	}

	towrite = 0;
	if (!writeactive && uaenet_getdata (sd->user, sd->writebuffer, &towrite)) {
	    donotwait = 1;
	    if (!WriteFile (sd->hCom, sd->writebuffer, towrite, &actual, &sd->olw)) {
		DWORD err = GetLastError();
		if (err == ERROR_IO_PENDING)
		    writeactive = 1;
	    }
	}

	uae_sem_post (&sd->change_sem);

	if (!donotwait) {
	    cnt = 0;
	    handles[cnt++] = sd->evtt;
	    if (readactive)
		handles[cnt++] = sd->olr.hEvent;
	    if (writeactive)
		handles[cnt++] = sd->olw.hEvent;
	    WaitForMultipleObjects(cnt, handles, FALSE, INFINITE);
	}


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

int uaenet_open (struct uaenetdatawin32 *sd, struct tapdata *tc, void *user, int unit)
{
    sd->tc = tc;
    sd->user = user;
    sd->evtr = CreateEvent (NULL, TRUE, FALSE, NULL);
    sd->evtw = CreateEvent (NULL, TRUE, FALSE, NULL);
    sd->evtt = CreateEvent (NULL, FALSE, FALSE, NULL);
    if (!sd->evtt || !sd->evtw || !sd->evtt)
	goto end;
    sd->olr.hEvent = sd->evtr;
    sd->olw.hEvent = sd->evtw;
    sd->hCom = tc->h;
    sd->mtu = tc->mtu;
    sd->readbuffer = xmalloc (sd->mtu);
    sd->writebuffer = xmalloc (sd->mtu);
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
    if (sd->evtr)
	CloseHandle(sd->evtr);
    if (sd->evtw)
	CloseHandle(sd->evtw);
    xfree (sd->readbuffer);
    xfree (sd->writebuffer);
    uaeser_initdata (sd, sd->user);
}
