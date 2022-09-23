
#include "sysconfig.h"

#include <stdlib.h>
#include <stdarg.h>

#include "sysdeps.h"
#include "uaeipc.h"
#include "options.h"
#include "zfile.h"
#include "inputdevice.h"
#include "debug.h"
#include "uae.h"

#include <windows.h>

#define IPC_BUFFER_SIZE 16384
#define MAX_OUTMESSAGES 30
#define MAX_BINMESSAGE 32

static int ipcmode;

struct uaeipc
{
	HANDLE hipc, olevent;
	OVERLAPPED ol;
	uae_u8 buffer[IPC_BUFFER_SIZE], outbuf[IPC_BUFFER_SIZE];
	int connected, readpending, writepending;
	int binary;
	TCHAR *outmsg[MAX_OUTMESSAGES];
	int outmessages;
	uae_u8 outbin[MAX_OUTMESSAGES][MAX_BINMESSAGE];
	int outbinlen[MAX_OUTMESSAGES];
};

static void parsemessage(TCHAR *in, struct uae_prefs *p, TCHAR *out, int outsize)
{
	int mode;

	out[0] = 0;

	my_trim (in);
	if (!_tcsicmp (in, _T("IPC_QUIT"))) {
		uae_quit ();
		return;
	}
	if (!_tcsicmp (in, _T("ipc_config"))) {
		ipcmode = 1;
		_tcscat (out, _T("200\n"));
		return;
	} else if (!_tcsicmp (in, _T("ipc_event"))) {
		ipcmode = 2;
		_tcscat (out, _T("200\n"));
		return;
	} else if (!_tcsicmp (in, _T("ipc_debug"))) {
		ipcmode = 3;
		_tcscat (out, _T("200\n"));
		return;
	} else if (!_tcsicmp (in, _T("ipc_restore"))) {
		ipcmode = 0;
		_tcscat (out, _T("200\n"));
		return;
	}

	mode = 0;
	if (ipcmode == 1) {
		mode = 1;
	} else if (ipcmode == 2) {
		mode = 1;
	} else if (ipcmode == 3) {
		mode = 2;
	} else if (!_tcsnicmp (in, _T("CFG "), 4) || !_tcsnicmp (in, _T("EVT "), 4)) {
		mode = 1;
		in += 4;
	} else if (!_tcsnicmp (in, _T("DBG "), 4)) {
		mode = 2;
		in += 4;
	}

	if (mode == 1) {
		TCHAR tmpout[256];
		int index = -1;
		int cnt = 0;
		for (;;) {
			int ret;
			tmpout[0] = 0;
			ret = cfgfile_modify (index, in, uaetcslen(in), tmpout, sizeof (tmpout) / sizeof (TCHAR));
			index++;
			if (uaetcslen(tmpout) > 0) {
				if (uaetcslen(out) == 0)
					_tcscat (out, _T("200 "));
				_tcsncat (out, _T("\n"), outsize);
				_tcsncat (out, tmpout, outsize);
			}
			cnt++;
			if (ret >= 0)
				break;
		}
		if (out[0] == 0)
			_tcscat (out, _T("404"));
	} else if (mode == 2) {
		debug_parser (in, out, outsize);
		if (!out[0])
			_tcscpy (out, _T("404"));
	} else {
		_tcscpy (out, _T("501"));
	}
}

static int listenIPC (void *vipc)
{
	struct uaeipc *ipc = (struct uaeipc*)vipc;
	DWORD err;

	memset(&ipc->ol, 0, sizeof (OVERLAPPED));
	ipc->ol.hEvent = ipc->olevent;
	if (ConnectNamedPipe(ipc->hipc, &ipc->ol)) {
		write_log (_T("IPC: ConnectNamedPipe init failed, err=%d\n"), GetLastError());
		closeIPC(ipc);
		return 0;
	}
	err = GetLastError();
	if (err == ERROR_PIPE_CONNECTED) {
		if (SetEvent(ipc->olevent)) {
			write_log (_T("IPC: ConnectNamedPipe SetEvent failed, err=%d\n"), GetLastError());
			closeIPC(ipc);
			return 0;
		}
	} else if (err != ERROR_IO_PENDING) {
		write_log (_T("IPC: ConnectNamedPipe failed, err=%d\n"), err);
		closeIPC(ipc);
		return 0;
	}
	return 1;
}

static void disconnectIPC (void *vipc)
{
	struct uaeipc *ipc = (struct uaeipc*)vipc;
	ipc->readpending = ipc->writepending = FALSE;
	if (ipc->connected) {
		if (!DisconnectNamedPipe(ipc->hipc))
			write_log (_T("IPC: DisconnectNamedPipe failed, err=%d\n"), GetLastError());
		ipc->connected = FALSE;
	}
}

static void resetIPC (void *vipc)
{
	struct uaeipc *ipc = (struct uaeipc*)vipc;
	disconnectIPC (ipc);
	listenIPC (ipc);
}

void closeIPC (void *vipc)
{
	struct uaeipc *ipc = (struct uaeipc*)vipc;
	if (!ipc)
		return;
	disconnectIPC (ipc);
	if (ipc->hipc == INVALID_HANDLE_VALUE)
		return;
	CloseHandle (ipc->hipc);
	ipc->hipc = INVALID_HANDLE_VALUE;
	if (ipc->olevent != INVALID_HANDLE_VALUE)
		CloseHandle (ipc->olevent);
	ipc->olevent = INVALID_HANDLE_VALUE;
	xfree (ipc);
}

void *createIPC (const TCHAR *name, int binary)
{
	TCHAR tmpname[100];
	int cnt = 0;
	struct uaeipc *ipc;

	ipc = xcalloc (struct uaeipc, 1);
	ipc->connected = FALSE;
	ipc->readpending = FALSE;
	ipc->writepending = FALSE;
	ipc->olevent = INVALID_HANDLE_VALUE;
	ipc->binary = 0;
	while (cnt < 10) {
		_stprintf (tmpname, _T("\\\\.\\pipe\\%s"), name);
		if (cnt > 0) {
			TCHAR *p = tmpname + _tcslen (tmpname);
			_stprintf (p, _T("_%d"), cnt);
		}
		ipc->hipc = CreateNamedPipe (tmpname,
			PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
			PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE,
			1, IPC_BUFFER_SIZE, IPC_BUFFER_SIZE,
			NMPWAIT_USE_DEFAULT_WAIT, NULL);
		if (ipc->hipc == INVALID_HANDLE_VALUE) {
			DWORD err = GetLastError ();
			if (err == ERROR_ALREADY_EXISTS || err == ERROR_PIPE_BUSY) {
				cnt++;
				continue;
			}
			xfree(ipc);
			return 0;
		}
		break;
	}
	write_log (_T("IPC: Named Pipe '%s' open\n"), tmpname);
	ipc->olevent = CreateEvent(NULL, TRUE, TRUE, NULL);
	if (listenIPC(ipc))
		return ipc;
	closeIPC(ipc);
	return NULL;
}

void *geteventhandleIPC (void *vipc)
{
	struct uaeipc *ipc = (struct uaeipc*)vipc;
	if (!ipc)
		return INVALID_HANDLE_VALUE;
	return ipc->olevent;
}

int sendIPC (void *vipc, TCHAR *msg)
{
	struct uaeipc *ipc = (struct uaeipc*)vipc;
	if (ipc->hipc == INVALID_HANDLE_VALUE)
		return 0;
	if (ipc->outmessages >= MAX_OUTMESSAGES)
		return 0;
	ipc->outmsg[ipc->outmessages++] = my_strdup (msg);
	if (!ipc->readpending && !ipc->writepending)
		SetEvent (ipc->olevent);
	return 1;
}
int sendBinIPC (void *vipc, uae_u8 *msg, int len)
{
	struct uaeipc *ipc = (struct uaeipc*)vipc;
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

int checkIPC (void *vipc, struct uae_prefs *p)
{
	struct uaeipc *ipc = (struct uaeipc*)vipc;
	BOOL ok;
	DWORD ret, err;

	if (!ipc)
		return 0;
	if (ipc->hipc == INVALID_HANDLE_VALUE)
		return 0;
	if (WaitForSingleObject(ipc->olevent, 0) != WAIT_OBJECT_0)
		return 0;
	if (!ipc->readpending && !ipc->writepending && ipc->outmessages > 0) {
		memset (&ipc->ol, 0, sizeof (OVERLAPPED));
		ipc->ol.hEvent = ipc->olevent;
		if (ipc->binary) {
			ok = WriteFile (ipc->hipc, &ipc->outbin[ipc->outmessages][0], ipc->outbinlen[ipc->outmessages], &ret, &ipc->ol);
		} else {
			ok = WriteFile (ipc->hipc, ipc->outmsg[ipc->outmessages], (uaetcslen(ipc->outmsg[ipc->outmessages]) + 1) * sizeof (TCHAR), &ret, &ipc->ol);
		}
		xfree (ipc->outmsg[ipc->outmessages--]);
		err = GetLastError ();
		if (!ok && err != ERROR_IO_PENDING) {
			write_log (_T("IPC: WriteFile() err=%d\n"), err);
			resetIPC (ipc);
			return 0;
		}
		ipc->writepending = TRUE;
		return 1;
	}
	if (ipc->readpending || ipc->writepending) {
		ok = GetOverlappedResult (ipc->hipc, &ipc->ol, &ret, FALSE);
		if (!ok) {
			err = GetLastError ();
			if (err == ERROR_IO_INCOMPLETE)
				return 0;
			write_log (_T("IPC: GetOverlappedResult error %d\n"), err);
			ipc->connected = TRUE;
			resetIPC (ipc);
			return 0;
		}
		if (!ipc->connected) {
			write_log (_T("IPC: Pipe connected\n"));
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
		ipc->buffer[0] = ipc->buffer[1] = 0;
		ipc->buffer[2] = ipc->buffer[3] = 0;
		ok = ReadFile (ipc->hipc, ipc->buffer, IPC_BUFFER_SIZE, &ret, &ipc->ol);
		err = GetLastError ();
		if (!ok) {
			if (err == ERROR_IO_PENDING) {
				ipc->readpending = TRUE;
				return 0;
			} else if (err == ERROR_BROKEN_PIPE) {
				write_log (_T("IPC: IPC client disconnected\n"));
			} else {
				write_log (_T("IPC: ReadFile() err=%d\n"), err);
			}
			resetIPC (ipc);
			return 0;
		}
	}
	ipc->readpending = FALSE;
	if (ipc->binary) {

	} else {
		TCHAR out[IPC_BUFFER_SIZE];
		int outlen;
		TCHAR *msg;
		bool freeit = false;
		int type = 0;
		if (ipc->buffer[0] == 0xef && ipc->buffer[1] == 0xbb && ipc->buffer[2] == 0xbf) {
			msg = utf8u ((char*)ipc->buffer + 3);
			type = 1;
		} else if (ipc->buffer[0] == 0xff && ipc->buffer[1] == 0xfe) {
			msg = my_strdup ((TCHAR*)(ipc->buffer + 2));
			type = 2;
		} else {
			msg = au ((uae_char*)ipc->buffer);
		}
		parsemessage (msg, p, out, sizeof out / sizeof (TCHAR));
		xfree (msg);
		if (type == 1) {
			char *outp = uutf8 (out);
			strcpy ((char*)ipc->outbuf, outp);
			outlen = uaestrlen((char*)ipc->outbuf) + sizeof (char);
			xfree (outp);
		} else if (type == 2) {
			if (_tcslen (out) >= IPC_BUFFER_SIZE)
				out[IPC_BUFFER_SIZE - 1] = 0;
			_tcscpy ((TCHAR*)ipc->outbuf, out);
			outlen = (uaetcslen((TCHAR*)ipc->outbuf) + 1) * sizeof (TCHAR);
		} else {
			ua_copy ((uae_char*)ipc->outbuf, sizeof ipc->outbuf, out);
			outlen = uaestrlen((char*)ipc->outbuf) + sizeof (char);
		}
		memset (&ipc->ol, 0, sizeof (OVERLAPPED));
		ipc->ol.hEvent = ipc->olevent;
		ok = WriteFile (ipc->hipc, ipc->outbuf, outlen, &ret, &ipc->ol);
		err = GetLastError ();
		if (!ok && err != ERROR_IO_PENDING) {
			write_log (_T("IPC: WriteFile() err=%d\n"), err);
			resetIPC (ipc);
			return 0;
		}
		ipc->writepending = TRUE;
	}
	return 1;
}

int isIPC (const TCHAR *pipename)
{
	HANDLE p;

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
