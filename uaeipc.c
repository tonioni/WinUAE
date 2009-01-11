
#include "sysconfig.h"

#include <stdlib.h>
#include <stdarg.h>

#include "sysdeps.h"
#include "uaeipc.h"
#include "options.h"
#include "zfile.h"
#include "inputdevice.h"

#include <windows.h>

#define IPC_BUFFER_SIZE 16384
#define MAX_OUTMESSAGES 30
#define MAX_BINMESSAGE 32

struct uaeipc
{
    HANDLE hipc, olevent;
    OVERLAPPED ol;
    uae_u8 buffer[IPC_BUFFER_SIZE], outbuf[IPC_BUFFER_SIZE];
    int connected, readpending, writepending;
    int binary;
    char *outmsg[MAX_OUTMESSAGES];
    int outmessages;
    uae_u8 outbin[MAX_OUTMESSAGES][MAX_BINMESSAGE];
    int outbinlen[MAX_OUTMESSAGES];
};

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

static int listenIPC(struct uaeipc *ipc)
{
    DWORD err;

    memset(&ipc->ol, 0, sizeof (OVERLAPPED));
    ipc->ol.hEvent = ipc->olevent;
    if (ConnectNamedPipe(ipc->hipc, &ipc->ol)) {
	write_log ("IPC: ConnectNamedPipe init failed, err=%d\n", GetLastError());
	closeIPC(ipc);
	return 0;
    }
    err = GetLastError();
    if (err == ERROR_PIPE_CONNECTED) {
	if (SetEvent(ipc->olevent)) {
	    write_log ("IPC: ConnectNamedPipe SetEvent failed, err=%d\n", GetLastError());
	    closeIPC(ipc);
	    return 0;
	}
    } else if (err != ERROR_IO_PENDING) {
	write_log ("IPC: ConnectNamedPipe failed, err=%d\n", err);
	closeIPC(ipc);
	return 0;
    }
    return 1;
}

static void disconnectIPC(struct uaeipc *ipc)
{
    ipc->readpending = ipc->writepending = FALSE;
    if (ipc->connected) {
	if (!DisconnectNamedPipe(ipc->hipc))
	    write_log ("IPC: DisconnectNamedPipe failed, err=%d\n", GetLastError());
	ipc->connected = FALSE;
    }
}

static void resetIPC(struct uaeipc *ipc)
{
    disconnectIPC(ipc);
    listenIPC(ipc);
}

void closeIPC(struct uaeipc *ipc)
{
    if (!ipc)
	return;
    disconnectIPC(ipc);
    if (ipc->hipc == INVALID_HANDLE_VALUE)
	return;
    CloseHandle(ipc->hipc);
    ipc->hipc = INVALID_HANDLE_VALUE;
    if (ipc->olevent != INVALID_HANDLE_VALUE)
	CloseHandle (ipc->olevent);
    ipc->olevent = INVALID_HANDLE_VALUE;
    xfree (ipc);
}

void *createIPC(const char *name, int binary)
{
    char tmpname[100];
    int cnt = 0;
    struct uaeipc *ipc;

    ipc = xcalloc (sizeof (struct uaeipc), 1);
    ipc->connected = FALSE;
    ipc->readpending = FALSE;
    ipc->writepending = FALSE;
    ipc->olevent = INVALID_HANDLE_VALUE;
    ipc->binary = 1;
    while (cnt < 10) {
	sprintf (tmpname, "\\\\.\\pipe\\%s", name);
	if (cnt > 0) {
	    char *p = tmpname + strlen (tmpname);
	    sprintf(p, "_%d", cnt);
	}
	ipc->hipc = CreateNamedPipe(tmpname,
	    PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
	    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE,
	    1, IPC_BUFFER_SIZE, IPC_BUFFER_SIZE,
	    NMPWAIT_USE_DEFAULT_WAIT, NULL);
	if (ipc->hipc == INVALID_HANDLE_VALUE) {
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
    ipc->olevent = CreateEvent(NULL, TRUE, TRUE, NULL);
    if (listenIPC(ipc))
	return ipc;
    closeIPC(ipc);
    return NULL;
}

void *geteventhandleIPC(struct uaeipc *ipc)
{
    return ipc->olevent;
}

int sendIPC(struct uaeipc *ipc, char *msg)
{
    if (ipc->hipc == INVALID_HANDLE_VALUE)
	return 0;
    if (ipc->outmessages >= MAX_OUTMESSAGES)
	return 0;
    ipc->outmsg[ipc->outmessages++] = my_strdup (msg);
    if (!ipc->readpending && !ipc->writepending)
	SetEvent (ipc->olevent);
    return 1;
}
int sendBinIPC(struct uaeipc *ipc, uae_u8 *msg, int len)
{
    if (ipc->hipc == INVALID_HANDLE_VALUE)
	return 0;
    if (ipc->outmessages >= MAX_OUTMESSAGES)
	return 0;
    ipc->outbinlen[ipc->outmessages] = len;
    memcpy (&ipc->outbin[ipc->outmessages++][0], msg, len);
    if (!ipc->readpending && !ipc->writepending)
	SetEvent (ipc->olevent);
    return 1;
}

int checkIPC(struct uaeipc *ipc, struct uae_prefs *p)
{
    BOOL ok;
    DWORD ret, err;

    if (ipc->hipc == INVALID_HANDLE_VALUE)
	return 0;
    if (WaitForSingleObject(ipc->olevent, 0) != WAIT_OBJECT_0)
	return 0;
    if (!ipc->readpending && !ipc->writepending && ipc->outmessages > 0) {
	memset (&ipc->ol, 0, sizeof (OVERLAPPED));
	ipc->ol.hEvent = ipc->olevent;
	if (ipc->binary) {
	    ok = WriteFile(ipc->hipc, &ipc->outbin[ipc->outmessages][0], ipc->outbinlen[ipc->outmessages], &ret, &ipc->ol);
	} else {
	    ok = WriteFile(ipc->hipc, ipc->outmsg[ipc->outmessages], strlen (ipc->outmsg[ipc->outmessages]) + 1, &ret, &ipc->ol);
	}
	xfree(ipc->outmsg[ipc->outmessages--]);
	err = GetLastError();
	if (!ok && err != ERROR_IO_PENDING) {
	    write_log ("IPC: WriteFile() err=%d\n", err);
	    resetIPC(ipc);
	    return 0;
	}
	ipc->writepending = TRUE;
	return 1;
    }
    if (ipc->readpending || ipc->writepending) {
	ok = GetOverlappedResult(ipc->hipc, &ipc->ol, &ret, FALSE);
	if (!ok) {
	    err = GetLastError();
	    if (err == ERROR_IO_INCOMPLETE)
		return 0;
	    write_log ("IPC: GetOverlappedResult error %d\n", err);
	    resetIPC(ipc);
	    return 0;
	}
	if (!ipc->connected) {
	    write_log ("IPC: Pipe connected\n");
	    ipc->connected = TRUE;
	    return 0;
	}
	if (ipc->writepending) {
	    ipc->writepending = FALSE;
	    SetEvent (ipc->ol.hEvent);
	    memset (&ipc->ol, 0, sizeof (OVERLAPPED));
	    ipc->ol.hEvent = ipc->olevent;
	    return 0;
	}
    }
    if (!ipc->readpending) {
	ok = ReadFile(ipc->hipc, ipc->buffer, IPC_BUFFER_SIZE, &ret, &ipc->ol);
	err = GetLastError();
	if (!ok) {
	    if (err == ERROR_IO_PENDING) {
		ipc->readpending = TRUE;
		return 0;
	    } else if (err == ERROR_BROKEN_PIPE) {
		write_log ("IPC: IPC client disconnected\n");
	    } else {
		write_log ("IPC: ReadFile() err=%d\n", err);
	    }
	    resetIPC(ipc);
	    return 0;
	}
    }
    ipc->readpending = FALSE;
    if (ipc->binary) {

    } else {
	write_log ("IPC: got message '%s'\n", ipc->buffer);
	parsemessage((char*)ipc->buffer, p, (char*)ipc->outbuf, sizeof ipc->outbuf);
	memset (&ipc->ol, 0, sizeof (OVERLAPPED));
	ipc->ol.hEvent = ipc->olevent;
	ok = WriteFile(ipc->hipc, ipc->outbuf, strlen ((char*)ipc->outbuf) + 1, &ret, &ipc->ol);
	err = GetLastError();
	if (!ok && err != ERROR_IO_PENDING) {
	    write_log ("IPC: WriteFile() err=%d\n", err);
	    resetIPC(ipc);
	    return 0;
	}
	ipc->writepending = TRUE;
    }
    return 1;
}

int isIPC (const char *pipename)
{
    HANDLE *p;

    p = CreateFile(
	pipename,
	GENERIC_READ | GENERIC_WRITE,
	0,
	NULL,
	OPEN_EXISTING,
	0,
	NULL);
    if (p == INVALID_HANDLE_VALUE)
	return 0;
    CloseHandle (p);
    return 1;
}
