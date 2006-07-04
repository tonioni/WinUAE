
#include "config.h"
#include "sysconfig.h"

#include <stdlib.h>
#include <stdarg.h>

#include "sysdeps.h"
#include "uaeipc.h"
#include "options.h"
#include "zfile.h"
#include "inputdevice.h"

#include <windows.h>

static HANDLE *hipc = INVALID_HANDLE_VALUE, *olevent = INVALID_HANDLE_VALUE;
static OVERLAPPED ol;
#define IPC_BUFFER_SIZE 16384
static uae_u8 buffer[IPC_BUFFER_SIZE], outbuf[IPC_BUFFER_SIZE];
static int connected, readpending, writepending;

static void parsemessage(char *in, struct uae_prefs *p, char *out, int outsize)
{
    out[0] = 0;
    if (!memcmp(in, "CFG ", 4) || !memcmp(in, "EVT ", 4)) {
	char tmpout[256];
	int index = -1;
	int cnt = 0;
	in += 4;
	for (;;) {
	    int ret;
	    tmpout[0] = 0;
	    ret = cfgfile_modify (index, in, strlen (in), tmpout, sizeof (tmpout));
	    index++;
	    if (strlen(tmpout) > 0) {
		if (strlen(out) == 0)
		    strcat (out, "200 ");
		strncat (out, "\n", outsize);
		strncat (out, tmpout, outsize);
	    }
	    cnt++;
	    if (ret >= 0)
		break;
	}
	if (strlen (out) == 0)
	    strcat (out, "404");
    } else {
	strcpy (out, "501");
    }
}

static int listenIPC(void)
{
    DWORD err;

    memset(&ol, 0, sizeof ol);
    ol.hEvent = olevent;
    if (ConnectNamedPipe(hipc, &ol)) {
	write_log ("IPC: ConnectNamedPipe init failed, err=%d\n", GetLastError());
	closeIPC();
	return 0;
    }
    err = GetLastError();
    if (err == ERROR_PIPE_CONNECTED) {
	if (SetEvent(olevent)) {
	    write_log ("IPC: ConnectNamedPipe SetEvent failed, err=%d\n", GetLastError());
	    closeIPC();
	    return 0;
	}
    } else if (err != ERROR_IO_PENDING) {
        write_log ("IPC: ConnectNamedPipe failed, err=%d\n", err);
	closeIPC();
	return 0;
    }
    write_log("IPC: waiting for connections\n");
    return 1;
}

static void disconnectIPC(void)
{
    readpending = writepending = FALSE;
    if (connected) {
	if (!DisconnectNamedPipe(hipc))
	    write_log ("IPC: DisconnectNamedPipe failed, err=%d\n", GetLastError());
	connected = FALSE;
    }
}

static void resetIPC(void)
{
    disconnectIPC();
    listenIPC();
}

void closeIPC(void)
{
    disconnectIPC();
    if (hipc == INVALID_HANDLE_VALUE)
	return;
    CloseHandle(hipc);
    hipc = INVALID_HANDLE_VALUE;
    if (olevent != INVALID_HANDLE_VALUE)
	CloseHandle (olevent);
    olevent = INVALID_HANDLE_VALUE;

}

int createIPC(void)
{
    char tmpname[100];
    int cnt = 0;

    connected = FALSE;
    readpending = FALSE;
    writepending = FALSE;
    olevent = INVALID_HANDLE_VALUE;
    while (cnt < 10) {
	strcpy (tmpname, "\\\\.\\pipe\\WinUAE");
	if (cnt > 0) {
	    char *p = tmpname + strlen (tmpname);
	    sprintf(p, "_%d", cnt);
	}
	hipc = CreateNamedPipe(tmpname,
	    PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
	    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE,
	    1, IPC_BUFFER_SIZE, IPC_BUFFER_SIZE,
	    NMPWAIT_USE_DEFAULT_WAIT, NULL);
	if (hipc == INVALID_HANDLE_VALUE) {
	    DWORD err = GetLastError();
	    if (err == ERROR_ALREADY_EXISTS || err == ERROR_PIPE_BUSY) {
		cnt++;
		continue;
	    }
	    return 0;
	}
	break;
    }
    write_log ("IPC: Named Pipe '%s' open\n", tmpname);
    olevent = CreateEvent(NULL, TRUE, TRUE, NULL);
    return listenIPC();
}

void *geteventhandleIPC(void)
{
    return olevent;
}

static int isout;
static char outmsg[IPC_BUFFER_SIZE];

int sendIPC(char *msg)
{
    if (hipc == INVALID_HANDLE_VALUE)
	return 0;
    if (isout)
	return 0;
    strcpy (outmsg, msg);
    if (!readpending && !writepending)
	SetEvent (olevent);
    return 1;
}

int checkIPC(struct uae_prefs *p)
{
    BOOL ok;
    DWORD ret, err;

    if (hipc == INVALID_HANDLE_VALUE)
	return 0;
    if (WaitForSingleObject(olevent, 0) != WAIT_OBJECT_0)
	return 0;
    if (!readpending && !writepending && isout) {
	isout = 0;
	memset (&ol, 0, sizeof ol);
	ol.hEvent = olevent;
	ok = WriteFile(hipc, outmsg, strlen (outmsg) + 1, &ret, &ol);
	err = GetLastError();
	if (!ok && err != ERROR_IO_PENDING) {
	    write_log ("IPC: WriteFile() err=%d\n", err);
	    resetIPC();
	    return 0;
	}
	writepending = TRUE;
	return 1;
    }
    if (readpending || writepending) {
	ok = GetOverlappedResult(hipc, &ol, &ret, FALSE);
	if (!ok) {
	    err = GetLastError();
	    if (err == ERROR_IO_INCOMPLETE)
		return 0;
	    write_log ("IPC: GetOverlappedResult error %d\n", err);
	    resetIPC();
	    return 0;
	}
	if (!connected) {
	    write_log ("IPC: Pipe connected\n");
	    connected = TRUE;
	    return 0;
	}
        if (writepending) {
	    writepending = FALSE;
	    SetEvent (ol.hEvent);
	    memset (&ol, 0, sizeof ol);
	    ol.hEvent = olevent;
	    return 0;
        }
    }
    if (!readpending) {
	ok = ReadFile(hipc, buffer, IPC_BUFFER_SIZE, &ret, &ol);
	err = GetLastError();
	if (!ok) {
	    if (err == ERROR_IO_PENDING) {
		readpending = TRUE;
		return 0;
	    } else if (err == ERROR_BROKEN_PIPE) {
		write_log ("IPC: IPC client disconnected\n");
	    } else {
		write_log ("IPC: ReadFile() err=%d\n", err);
	    }
	    resetIPC();
	    return 0;
	}
    }
    readpending = FALSE;
    write_log("IPC: got message '%s'\n", buffer);
    parsemessage(buffer, p, outbuf, sizeof outbuf);
    memset (&ol, 0, sizeof ol);
    ol.hEvent = olevent;
    ok = WriteFile(hipc, outbuf, strlen (outbuf) + 1, &ret, &ol);
    err = GetLastError();
    if (!ok && err != ERROR_IO_PENDING) {
	write_log ("IPC: WriteFile() err=%d\n", err);
        resetIPC();
	return 0;
    }
    writepending = TRUE;
    return 1;
}

