/*
* UAE - The Un*x Amiga Emulator
*
* Not a parser, but parallel and serial emulation for Win32
*
* Copyright 1997 Mathias Ortmann
* Copyright 1998-1999 Brian King - added MIDI output support
*/

#include "sysconfig.h"

#undef SERIAL_ENET

#include <Ws2tcpip.h>

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
#include <Ntddpar.h>

#include "sysdeps.h"
#include "options.h"
#include "gensound.h"
#include "events.h"
#include "uae.h"
#include "include/memory.h"
#include "custom.h"
#include "autoconf.h"
#include "newcpu.h"
#include "traps.h"
#include "registry.h"
#include "od-win32/win32gui.h"
#include "od-win32/parser.h"
#include "od-win32/midi.h"
#include "od-win32/ahidsound.h"
#include "picasso96_win.h"
#include "win32gfx.h"
#include "win32.h"
#include "ioport.h"
#include "parallel.h"
#include "zfile.h"
#include "threaddep/thread.h"
#include "serial.h"
#include "savestate.h"
#include "ahidsound_new.h"
#include "uaeipc.h"
#include "xwin.h"
#include "drawing.h"

#define GSDLLEXPORT __declspec(dllimport)

#include <Ghostscript/iapi.h>
#include <Ghostscript/ierrors.h>

#define MIN_PRTBYTES 10

static uae_char prtbuf[PRTBUFSIZE];
static int prtbufbytes,wantwrite;
static HANDLE hPrt = INVALID_HANDLE_VALUE;
static DWORD  dwJob;
static int prtopen;
void DoSomeWeirdPrintingStuff(uae_char val);
static int uartbreak;
static int parflush;

static volatile int prt_running;
static volatile int prt_started;
static smp_comm_pipe prt_requests;

int postscript_print_debugging = 0;
static struct zfile *prtdump;

static int psmode = 0;
static HMODULE gsdll;
static void *gsinstance;
static int gs_exitcode;

typedef int (GSDLLAPI* GSAPI_REVISION)(gsapi_revision_t *pr, int len);
static GSAPI_REVISION ptr_gsapi_revision;
typedef int (GSDLLAPI* GSAPI_NEW_INSTANCE)(void **pinstance, void *caller_handle);
static GSAPI_NEW_INSTANCE ptr_gsapi_new_instance;
typedef void (GSDLLAPI* GSAPI_DELETE_INSTANCE)(void *instance);
static GSAPI_DELETE_INSTANCE ptr_gsapi_delete_instance;
typedef int (GSDLLAPI* GSAPI_SET_STDIO)(void *instance,
	int (GSDLLCALLPTR stdin_fn)(void *caller_handle, char *buf, int len),
	int (GSDLLCALLPTR stdout_fn)(void *caller_handle, const char *str, int len),
	int (GSDLLCALLPTR stderr_fn)(void *caller_handle, const char *str, int len));
static GSAPI_SET_STDIO ptr_gsapi_set_stdio;
typedef int (GSDLLAPI* GSAPI_INIT_WITH_ARGS)(void *instance, int argc, char **argv);
static GSAPI_INIT_WITH_ARGS ptr_gsapi_init_with_args;
typedef int (GSDLLAPI* GSAPI_SET_ARG_ENCODING)(void *instance, int encoding);
static GSAPI_SET_ARG_ENCODING ptr_gsapi_set_arg_encoding;
typedef int (GSDLLAPI* GSAPI_EXIT)(void *instance);
static GSAPI_EXIT ptr_gsapi_exit;
typedef int (GSDLLAPI* GSAPI_RUN_STRING_BEGIN)(void *instance, int user_errors, int *pexit_code);
static GSAPI_RUN_STRING_BEGIN ptr_gsapi_run_string_begin;
typedef int (GSDLLAPI* GSAPI_RUN_STRING_CONTINUE)(void *instance, const char *str, unsigned int length, int user_errors, int *pexit_code);
static GSAPI_RUN_STRING_CONTINUE ptr_gsapi_run_string_continue;
typedef int (GSDLLAPI* GSAPI_RUN_STRING_END)(void *instance, int user_errors, int *pexit_code);
static GSAPI_RUN_STRING_END ptr_gsapi_run_string_end;

static uae_u8 **psbuffer;
static int psbuffers;

static LONG WINAPI ExceptionFilter (struct _EXCEPTION_POINTERS * pExceptionPointers, DWORD ec)
{
	return EXCEPTION_EXECUTE_HANDLER;
}

static void freepsbuffers (void)
{
	int i;
	for (i = 0; i < psbuffers; i++)
		free (psbuffer[i]);
	free (psbuffer);
	psbuffer = NULL;
	psbuffers = 0;
}

static int openprinter_ps (void)
{
	const TCHAR *gsargv[] = {
		_T("-dNOPAUSE"), _T("-dBATCH"), _T("-dNOPAGEPROMPT"), _T("-dNOPROMPT"), _T("-dQUIET"), _T("-dNoCancel"),
		_T("-sDEVICE=mswinpr2"), NULL
	};
	int gsargc, gsargc2, i;
	TCHAR *tmpparms[100];
	TCHAR tmp[MAX_DPATH];
	char *gsparms[100];

	if (ptr_gsapi_new_instance (&gsinstance, NULL) < 0)
		return 0;
	ptr_gsapi_set_arg_encoding(gsinstance, GS_ARG_ENCODING_UTF8);
	cmdlineparser (currprefs.ghostscript_parameters, tmpparms, 100 - 10);

	gsargc2 = 0;
	gsparms[gsargc2++] = uutf8(_T("WinUAE"));
	for (gsargc = 0; gsargv[gsargc]; gsargc++) {
		gsparms[gsargc2++] = uutf8(gsargv[gsargc]);
	}
	for (i = 0; tmpparms[i]; i++)
		gsparms[gsargc2++] = uutf8(tmpparms[i]);
	if (currprefs.prtname[0]) {
		_stprintf (tmp, _T("-sOutputFile=%%printer%%%s"), currprefs.prtname);
		gsparms[gsargc2++] = uutf8(tmp);
	}
	if (postscript_print_debugging) {
		for (i = 0; i < gsargc2; i++) {
			TCHAR *parm = utf8u(gsparms[i]);
			write_log (_T("GSPARM%d: '%s'\n"), i, parm);
			xfree (parm);
			xfree(gsparms[i]);
		}
	}
	__try {
		int rc = ptr_gsapi_init_with_args (gsinstance, gsargc2, gsparms);
		for (i = 0; i < gsargc2; i++) {
			xfree (gsparms[i]);
		}
		if (rc != 0) {
			write_log (_T("GS failed, returncode %d\n"), rc);
			return 0;
		}
		ptr_gsapi_run_string_begin (gsinstance, 0, &gs_exitcode);
	} __except (ExceptionFilter (GetExceptionInformation (), GetExceptionCode ())) {
		write_log (_T("GS crashed\n"));
		return 0;
	}
	psmode = 1;
	return 1;
}

static void *prt_thread (void *p)
{
	uae_u8 **buffers = (uae_u8**)p;
	int err, cnt, ok;

	ok = 1;
	prt_running++;
	prt_started = 1;
	SetThreadPriority (GetCurrentThread (), THREAD_PRIORITY_BELOW_NORMAL);
	if (load_ghostscript ()) {
		if (openprinter_ps ()) {
			write_log (_T("PostScript printing emulation started..\n"));
			cnt = 0;
			while (buffers[cnt]) {
				uae_u8 *p = buffers[cnt];
				err = ptr_gsapi_run_string_continue (gsinstance, (char*)p + 2, (p[0] << 8) | p[1], 0, &gs_exitcode);
				if (err != e_NeedInput && err <= e_Fatal) {
					ptr_gsapi_exit (gsinstance);
					write_log (_T("PostScript parsing failed.\n"));
					ok = 0;
					break;
				}
				cnt++;
			}
			cnt = 0;
			while (buffers[cnt]) {
				xfree (buffers[cnt]);
				cnt++;
			}
			xfree (buffers);
			if (ok) {
				write_log (_T("PostScript printing emulation finished..\n"));
				ptr_gsapi_run_string_end (gsinstance, 0, &gs_exitcode);
			}
		} else {
			write_log (_T("gsdllxx.dll failed to initialize\n"));
		}
	} else {
		write_log (_T("gsdllxx.dll failed to load\n"));
	}
	unload_ghostscript ();
	prt_running--;
	return 0;
}

static int doflushprinter (void)
{
	if (prtopen == 0 && prtbufbytes < MIN_PRTBYTES) {
		if (prtbufbytes > 0)
			write_log (_T("PRINTER: %d bytes received, less than %d bytes, not printing.\n"), prtbufbytes, MIN_PRTBYTES);
		prtbufbytes = 0;
		return 0;
	}
	return 1;
}

static void openprinter (void);
static void flushprtbuf (void)
{
	DWORD written = 0;

	if (!prtbufbytes)
		return;

	if (postscript_print_debugging && prtdump)
		zfile_fwrite (prtbuf, prtbufbytes, 1, prtdump);

	if (currprefs.parallel_postscript_emulation) {

		if (psmode) {
			uae_u8 *p;
			psbuffer = xrealloc (uae_u8*, psbuffer, (psbuffers + 2));
			p = xmalloc (uae_u8, prtbufbytes + 2);
			p[0] = prtbufbytes >> 8;
			p[1] = prtbufbytes;
			memcpy (p + 2, prtbuf, prtbufbytes);
			psbuffer[psbuffers++] = p;
			psbuffer[psbuffers] = NULL;
		}
		prtbufbytes = 0;
		return;

	} else if (prtbufbytes > 0) {
		int pbyt = prtbufbytes;

		if (currprefs.parallel_matrix_emulation >= PARALLEL_MATRIX_EPSON) {
			int i;
			if (!prtopen) {
				if (!doflushprinter ())
					return;
				if (epson_init (currprefs.prtname, currprefs.parallel_matrix_emulation))
					prtopen = 1;
			}
			for (i = 0; i < prtbufbytes; i++)
				epson_printchar (prtbuf[i]);
		} else {
			if (hPrt == INVALID_HANDLE_VALUE) {
				if (!doflushprinter ())
					return;
				openprinter ();
			}
			if (hPrt != INVALID_HANDLE_VALUE) {
				if (WritePrinter (hPrt, prtbuf, pbyt, &written)) {
					if (written != pbyt)
						write_log (_T("PRINTER: Only wrote %d of %d bytes!\n"), written, pbyt);
				} else {
					write_log (_T("PRINTER: Couldn't write data!\n"));
				}
			}
		}

	}
	prtbufbytes = 0;
}

void finishjob (void)
{
	flushprtbuf ();
}

static void DoSomeWeirdPrintingStuff (uae_char val)
{
	static uae_char prev[5];

	memmove (prev, prev + 1, 3);
	prev[3] = val;
	prev[4] = 0;
	if (currprefs.parallel_postscript_detection) {
		if (psmode && val == 4) {
			flushprtbuf ();
			*prtbuf = val;
			prtbufbytes = 1;
			flushprtbuf ();
			write_log (_T("PostScript end detected..\n"));

			if (postscript_print_debugging) {
				zfile_fclose (prtdump);
				prtdump = NULL;
			}

			if (currprefs.parallel_postscript_emulation) {
				prt_started = 0;
				if (uae_start_thread (_T("postscript"), prt_thread, psbuffer, NULL)) {
					while (!prt_started)
						Sleep (5);
					psbuffers = 0;
					psbuffer = NULL;
				}
			} else {
				closeprinter ();
			}
			freepsbuffers ();
			return;

		} else if (!psmode && !stricmp (prev, "%!PS")) {

			if (postscript_print_debugging)
				prtdump = zfile_fopen (_T("psdump.dat"), _T("wb"), 0);

			psmode = 1;
			psbuffer = xmalloc (uae_u8*, 1);
			psbuffer[0] = 0;
			psbuffers = 0;
			strcpy (prtbuf, "%!PS");
			prtbufbytes = strlen (prtbuf);
			flushprtbuf ();
			write_log (_T("PostScript start detected..\n"));
			return;
		}
	}
	if (prtbufbytes < PRTBUFSIZE) {
		prtbuf[prtbufbytes++] = val;
	} else {
		flushprtbuf ();
		*prtbuf = val;
		prtbufbytes = 1;
	}
}

int isprinter (void)
{
	if (!currprefs.prtname[0])
		return 0;
	if (!_tcsncmp (currprefs.prtname, _T("LPT"), 3)) {
		paraport_open (currprefs.prtname);
		return -1;
	}
	return 1;
}

int isprinteropen (void)
{
	if (prtopen || prtbufbytes > 0)
		return 1;
	return 0;
}

int load_ghostscript (void)
{
	struct gsapi_revision_s r;
	TCHAR path[MAX_DPATH];
	TCHAR *s;

	if (gsdll)
		return 1;
	_tcscpy (path, _T("gsdll32.dll"));
	gsdll = WIN32_LoadLibrary (path);
	if (!gsdll) {
		if (GetEnvironmentVariable (_T("GS_DLL"), path, sizeof (path) / sizeof (TCHAR)))
			gsdll = LoadLibrary (path);
	}
	if (!gsdll) {
		HKEY key;
		DWORD ret = RegOpenKeyEx (HKEY_LOCAL_MACHINE, _T("SOFTWARE\\AFPL Ghostscript"), 0, KEY_READ, &key);
		if (ret != ERROR_SUCCESS)
			ret = RegOpenKeyEx (HKEY_LOCAL_MACHINE, _T("SOFTWARE\\GPL Ghostscript"), 0, KEY_READ, &key);
		if (ret == ERROR_SUCCESS) {
			int idx = 0, cnt = 20;
			TCHAR tmp1[MAX_DPATH];
			while (cnt-- > 0) {
				DWORD size1 = sizeof (tmp1) / sizeof (TCHAR);
				FILETIME ft;
				if (RegEnumKeyEx (key, idx, tmp1, &size1, NULL, NULL, NULL, &ft) == ERROR_SUCCESS) {
					HKEY key2;
					if (RegOpenKeyEx (key, tmp1, 0, KEY_READ, &key2) == ERROR_SUCCESS) {
						DWORD type = REG_SZ;
						DWORD size = sizeof (path) / sizeof (TCHAR);
						if (RegQueryValueEx (key2, _T("GS_DLL"), 0, &type, (LPBYTE)path, &size) == ERROR_SUCCESS) {
							gsdll = LoadLibrary (path);
						}
						RegCloseKey (key2);
						if (gsdll)
							break;
					}
				}
				idx++;
			}
			RegCloseKey (key);
		}
	}
	if (!gsdll)
		return 0;
	ptr_gsapi_revision = (GSAPI_REVISION)GetProcAddress (gsdll, "gsapi_revision");
	if (!ptr_gsapi_revision) {
		unload_ghostscript ();
		write_log (_T("incompatible %s! (1)\n"), path);
		return -1;
	}
	if (ptr_gsapi_revision(&r, sizeof(r))) {
		unload_ghostscript ();
		write_log (_T("incompatible %s! (2)\n"), path);
		return -2;
	}
	ptr_gsapi_new_instance = (GSAPI_NEW_INSTANCE)GetProcAddress (gsdll, "gsapi_new_instance");
	ptr_gsapi_delete_instance = (GSAPI_DELETE_INSTANCE)GetProcAddress (gsdll, "gsapi_delete_instance");
	ptr_gsapi_set_stdio = (GSAPI_SET_STDIO)GetProcAddress (gsdll, "gsapi_set_stdio");
	ptr_gsapi_exit = (GSAPI_EXIT)GetProcAddress (gsdll, "gsapi_exit");
	ptr_gsapi_run_string_begin = (GSAPI_RUN_STRING_BEGIN)GetProcAddress (gsdll, "gsapi_run_string_begin");
	ptr_gsapi_run_string_continue = (GSAPI_RUN_STRING_CONTINUE)GetProcAddress (gsdll, "gsapi_run_string_continue");
	ptr_gsapi_run_string_end = (GSAPI_RUN_STRING_END)GetProcAddress (gsdll, "gsapi_run_string_end");
	ptr_gsapi_set_arg_encoding = (GSAPI_SET_ARG_ENCODING)GetProcAddress(gsdll, "gsapi_set_arg_encoding");
	ptr_gsapi_init_with_args = (GSAPI_INIT_WITH_ARGS)GetProcAddress(gsdll, "gsapi_init_with_args");

	if (!ptr_gsapi_new_instance || !ptr_gsapi_delete_instance || !ptr_gsapi_exit ||
		!ptr_gsapi_run_string_begin || !ptr_gsapi_run_string_continue || !ptr_gsapi_run_string_end ||
		!ptr_gsapi_set_arg_encoding || !ptr_gsapi_init_with_args) {
			unload_ghostscript ();
			write_log (_T("incompatible %s! (3)\n"), path);
			return -3;
	}
	s = au (r.product);
	write_log (_T("%s: %s rev %d initialized\n"), path, s, r.revision);
	xfree (s);
	return 1;
}

void unload_ghostscript (void)
{
	if (gsinstance) {
		ptr_gsapi_exit (gsinstance);
		ptr_gsapi_delete_instance (gsinstance);
	}
	gsinstance = NULL;
	if (gsdll)
		FreeLibrary (gsdll);
	gsdll = NULL;
	psmode = 0;
}

static void openprinter (void)
{
	DOC_INFO_1 DocInfo;
	static int first;

	closeprinter ();
	if (!currprefs.prtname[0])
		return;

	if (currprefs.parallel_postscript_emulation) {
		prtopen = 1;
		return;
	} else if (currprefs.parallel_matrix_emulation >= PARALLEL_MATRIX_EPSON) {
		epson_init (currprefs.prtname, currprefs.parallel_matrix_emulation);
	} else if (hPrt == INVALID_HANDLE_VALUE) {
		flushprtbuf ();
		if (OpenPrinter (currprefs.prtname, &hPrt, NULL)) {
			// Fill in the structure with info about this "document."
			DocInfo.pDocName = _T("WinUAE Document");
			DocInfo.pOutputFile = NULL;
			DocInfo.pDatatype = (currprefs.parallel_matrix_emulation || currprefs.parallel_postscript_detection) ? _T("TEXT") : _T("RAW");
			// Inform the spooler the document is beginning.
			if ((dwJob = StartDocPrinter (hPrt, 1, (LPBYTE)&DocInfo)) == 0) {
				ClosePrinter (hPrt );
				hPrt = INVALID_HANDLE_VALUE;
			} else if (StartPagePrinter (hPrt)) {
				prtopen = 1;
			}
		} else {
			hPrt = INVALID_HANDLE_VALUE; // Stupid bug in Win32, where OpenPrinter fails, but hPrt ends up being zero
		}
	}
	if (hPrt != INVALID_HANDLE_VALUE) {
		write_log (_T("PRINTER: Opening printer \"%s\" with handle 0x%x.\n"), currprefs.prtname, hPrt);
	} else if (*currprefs.prtname) {
		write_log (_T("PRINTER: ERROR - Couldn't open printer \"%s\" for output.\n"), currprefs.prtname);
	}
}

void flushprinter (void)
{
	if (!doflushprinter ())
		return;
	flushprtbuf ();
	closeprinter ();
}

void closeprinter (void)
{
#ifdef PRINT_DUMP
	zfile_fclose (prtdump);
#endif
	parflush = 0;
	psmode = 0;
	if (hPrt != INVALID_HANDLE_VALUE) {
		EndPagePrinter (hPrt);
		EndDocPrinter (hPrt);
		ClosePrinter (hPrt);
		hPrt = INVALID_HANDLE_VALUE;
		write_log (_T("PRINTER: Closing printer.\n"));
	}
	if (currprefs.parallel_postscript_emulation)
		prtopen = 1;
	else
		prtopen = 0;
	if (prt_running) {
		write_log (_T("waiting for printing to finish...\n"));
		while (prt_running)
			Sleep (10);
	}
	freepsbuffers ();
	epson_close ();
	prtbufbytes = 0;
}

void doprinter (uae_u8 val)
{
	parflush = 0;
	DoSomeWeirdPrintingStuff (val);
}

struct uaeserialdatawin32
{
	HANDLE hCom;
	HANDLE evtr, evtw, evtt, evtwce;
	OVERLAPPED olr, olw, olwce;
	int writeactive;
	void *readdata, *writedata;
	volatile int threadactive;
	uae_sem_t change_sem, sync_sem;
	void *user;
};

int uaeser_getdatalength (void)
{
	return sizeof (struct uaeserialdatawin32);
}

static void uaeser_initdata (void *vsd, void *user)
{
	struct uaeserialdatawin32 *sd = (struct uaeserialdatawin32*)vsd;
	memset (sd, 0, sizeof (struct uaeserialdatawin32));
	sd->hCom = INVALID_HANDLE_VALUE;
	sd->evtr = sd->evtw = sd->evtt = sd->evtwce = 0;
	sd->user = user;
}

int uaeser_query (void *vsd, uae_u16 *status, uae_u32 *pending)
{
	struct uaeserialdatawin32 *sd = (struct uaeserialdatawin32*)vsd;
	DWORD err, modem;
	COMSTAT ComStat;
	uae_u16 s = 0;

	if (!ClearCommError (sd->hCom, &err, &ComStat))
		return 0;
	*pending = ComStat.cbInQue;
	if (status) {
		s |= (err & CE_BREAK) ? (1 << 10) : 0;
		s |= (err & CE_RXOVER) ? (1 << 8) : 0;
		if (GetCommModemStatus (sd->hCom, &modem)) {
			s |= (modem & MS_CTS_ON) ? 0 : (1 << 4);
			s |= (modem & MS_DSR_ON) ? 0 : (1 << 7);
			s |= (modem & MS_RING_ON) ? (1 << 2) : 0;
		}
		*status = s;
	}
	return 1;
}

int uaeser_break (void *vsd, int brklen)
{
	struct uaeserialdatawin32 *sd = (struct uaeserialdatawin32*)vsd;
	if (!SetCommBreak (sd->hCom))
		return 0;
	Sleep (brklen / 1000);
	ClearCommBreak (sd->hCom);
	return 1;
}

int uaeser_setparams (void *vsd, int baud, int rbuffer, int bits, int sbits, int rtscts, int parity, uae_u32 xonxoff)
{
	struct uaeserialdatawin32 *sd = (struct uaeserialdatawin32*)vsd;
	DCB dcb;

	memset (&dcb, 0, sizeof (dcb));
	dcb.DCBlength = sizeof (DCB);
	if (!GetCommState (sd->hCom, &dcb))
		return 5;

	dcb.fBinary = TRUE;
	dcb.BaudRate = baud;
	dcb.ByteSize = bits;
	dcb.Parity = parity == 0 ? NOPARITY : (parity == 1 ? ODDPARITY : EVENPARITY);
	dcb.fParity = FALSE;
	dcb.StopBits = sbits == 1 ? ONESTOPBIT : TWOSTOPBITS;

	dcb.fDsrSensitivity = FALSE;
	dcb.fOutxDsrFlow = FALSE;
	dcb.fDtrControl = DTR_CONTROL_ENABLE;

	if (rtscts) {
		dcb.fOutxCtsFlow = TRUE;
		dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
	} else {
		dcb.fRtsControl = RTS_CONTROL_ENABLE;
		dcb.fOutxCtsFlow = FALSE;
	}

	dcb.fTXContinueOnXoff = FALSE;
	if (xonxoff & 1) {
		dcb.fOutX = TRUE;
		dcb.fInX = TRUE;
		dcb.XonChar = (xonxoff >> 8) & 0xff;
		dcb.XoffChar = (xonxoff >> 16) & 0xff;
	} else {
		dcb.fOutX = FALSE;
		dcb.fInX = FALSE;
	}

	dcb.fErrorChar = FALSE;
	dcb.fNull = FALSE;
	dcb.fAbortOnError = FALSE;

	if (!SetCommState (sd->hCom, &dcb)) {
		write_log (_T("uaeserial: SetCommState() failed %d\n"), GetLastError());
		return 5;
	}
	SetupComm (sd->hCom, rbuffer, rbuffer);
	return 0;
}

static void startwce(struct uaeserialdatawin32 *sd, DWORD *evtmask)
{
	SetEvent(sd->evtwce);
	WaitCommEvent(sd->hCom, evtmask, &sd->olwce);
}

static void *uaeser_trap_thread (void *arg)
{
	struct uaeserialdatawin32 *sd = (struct uaeserialdatawin32*)arg;
	HANDLE handles[4];
	int cnt;
	DWORD evtmask, actual;

	uae_set_thread_priority (NULL, 1);
	sd->threadactive = 1;
	uae_sem_post (&sd->sync_sem);
	startwce(sd, &evtmask);
	while (sd->threadactive == 1) {
		int sigmask = 0;
		uae_sem_wait (&sd->change_sem);
		if (WaitForSingleObject(sd->evtwce, 0) == WAIT_OBJECT_0) {
			if (evtmask & EV_RXCHAR)
				sigmask |= 1;
			if ((evtmask & EV_TXEMPTY) && !sd->writeactive)
				sigmask |= 2;
			startwce(sd, &evtmask);
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
		uaeser_signal (sd->user, sigmask | 1);
		uae_sem_post (&sd->change_sem);
		WaitForMultipleObjects(cnt, handles, FALSE, INFINITE);
	}
	sd->threadactive = 0;
	uae_sem_post (&sd->sync_sem);
	return 0;
}

void uaeser_trigger (void *vsd)
{
	struct uaeserialdatawin32 *sd = (struct uaeserialdatawin32*)vsd;
	SetEvent (sd->evtt);
}

int uaeser_write (void *vsd, uae_u8 *data, uae_u32 len)
{
	struct uaeserialdatawin32 *sd = (struct uaeserialdatawin32*)vsd;
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

int uaeser_read (void *vsd, uae_u8 *data, uae_u32 len)
{
	struct uaeserialdatawin32 *sd = (struct uaeserialdatawin32*)vsd;
	int ret = 1;
	DWORD err;
	COMSTAT ComStat;

	if (!ClearCommError (sd->hCom, &err, &ComStat))
		return 0;
	if (len > ComStat.cbInQue)
		return 0;
	if (!ReadFile (sd->hCom, data, len, NULL, &sd->olr)) {
		if (GetLastError() == ERROR_IO_PENDING)
			WaitForSingleObject(sd->evtr, INFINITE);
		else
			ret = 0;
	}
	SetEvent (sd->evtt);
	return ret;
}

void uaeser_clearbuffers (void *vsd)
{
	struct uaeserialdatawin32 *sd = (struct uaeserialdatawin32*)vsd;
	PurgeComm (sd->hCom, PURGE_TXCLEAR | PURGE_RXCLEAR);
}

int uaeser_open (void *vsd, void *user, int unit)
{
	struct uaeserialdatawin32 *sd = (struct uaeserialdatawin32*)vsd;
	TCHAR buf[256];
	COMMTIMEOUTS CommTimeOuts;

	sd->user = user;
	_stprintf (buf, _T("\\\\.\\COM%d"), unit);
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
		FILE_FLAG_OVERLAPPED, NULL);
	if (sd->hCom == INVALID_HANDLE_VALUE) {
		_stprintf (buf, _T("\\.\\\\COM%d"), unit);
		sd->hCom = CreateFile (buf, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
			FILE_FLAG_OVERLAPPED, NULL);
		if (sd->hCom == INVALID_HANDLE_VALUE) {
			write_log (_T("UAESER: '%s' failed to open, err=%d\n"), buf, GetLastError());
			goto end;
		}
	}
	uae_sem_init (&sd->sync_sem, 0, 0);
	uae_sem_init (&sd->change_sem, 0, 1);
	uae_start_thread (_T("uaeserial_win32"), uaeser_trap_thread, sd, NULL);
	uae_sem_wait (&sd->sync_sem);

	CommTimeOuts.ReadIntervalTimeout = 0;
	CommTimeOuts.ReadTotalTimeoutMultiplier = 0;
	CommTimeOuts.ReadTotalTimeoutConstant = 0;
	CommTimeOuts.WriteTotalTimeoutMultiplier = 0;
	CommTimeOuts.WriteTotalTimeoutConstant = 0;
	SetCommTimeouts (sd->hCom, &CommTimeOuts);
	SetCommMask (sd->hCom, EV_RXCHAR | EV_TXEMPTY | EV_BREAK);

	return 1;

end:
	uaeser_close (sd);
	return 0;
}

void uaeser_close (void *vsd)
{
	struct uaeserialdatawin32 *sd = (struct uaeserialdatawin32*)vsd;
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

static HANDLE hCom = INVALID_HANDLE_VALUE;
static DCB dcb;
static DWORD fDtrControl = DTR_CONTROL_DISABLE, fRtsControl = RTS_CONTROL_DISABLE;
static HANDLE writeevent, readevent;
#define SERIAL_WRITE_BUFFER 100
#define SERIAL_READ_BUFFER 100
static uae_u8 outputbuffer[SERIAL_WRITE_BUFFER];
static uae_u8 outputbufferout[SERIAL_WRITE_BUFFER];
static uae_u8 inputbuffer[SERIAL_READ_BUFFER];
static int datainoutput;
static int dataininput, dataininputcnt;
static OVERLAPPED writeol, readol;
static int writepending;

static WSADATA wsadata;
static SOCKET serialsocket = INVALID_SOCKET;
static SOCKET serialconn = INVALID_SOCKET;
static PADDRINFOW socketinfo;
static char socketaddr[sizeof SOCKADDR_INET];
static BOOL tcpserial;

static bool tcp_is_connected (void)
{
	socklen_t sa_len = sizeof SOCKADDR_INET;
	if (serialsocket == INVALID_SOCKET)
		return false;
	if (serialconn == INVALID_SOCKET) {
		struct timeval tv;
		fd_set fd;
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		fd.fd_array[0] = serialsocket;
		fd.fd_count = 1;
		if (select (1, &fd, NULL, NULL, &tv)) {
			serialconn = accept (serialsocket, (struct sockaddr*)socketaddr, &sa_len);
			if (serialconn != INVALID_SOCKET)
				write_log (_T("SERIAL_TCP: connection accepted\n"));
		}
	}
	return serialconn != INVALID_SOCKET;
}

static void tcp_disconnect (void)
{
	if (serialconn == INVALID_SOCKET)
		return;
	closesocket (serialconn);
	serialconn = INVALID_SOCKET;
	write_log (_T("SERIAL_TCP: disconnect\n"));
}

static void closetcp (void)
{
	if (serialconn != INVALID_SOCKET)
		closesocket (serialconn);
	serialconn = INVALID_SOCKET;
	if (serialsocket != INVALID_SOCKET)
		closesocket (serialsocket);
	serialsocket = INVALID_SOCKET;
	if (socketinfo)
		FreeAddrInfoW (socketinfo);
	socketinfo = NULL;
	WSACleanup ();
}

static int opentcp (const TCHAR *sername)
{
	int err;
	TCHAR *port, *name;
	const TCHAR *p;
	bool waitmode = false;
	const int one = 1;
	const struct linger linger_1s = { 1, 1 };

	if (WSAStartup (MAKEWORD (2, 2), &wsadata)) {
		DWORD lasterror = WSAGetLastError ();
		write_log (_T("SERIAL_TCP: can't open '%s', error %d\n"), sername, lasterror);
		return 0;
	}
	name = my_strdup (sername);
	port = NULL;
	p = _tcschr (sername, ':');
	if (p) {
		name[p - sername] = 0;
		port = my_strdup (p + 1);
		const TCHAR *p2 = _tcschr (port, '/');
		if (p2) {
			port[p2 - port] = 0;
			if (!_tcsicmp (p2 + 1, _T("wait")))
				waitmode = true;
		}
	}
	if (port && port[0] == 0) {
		xfree (port);
		port = NULL;
	}
	if (!port)
		port = 	my_strdup (_T("1234"));

	err = GetAddrInfoW (name, port, NULL, &socketinfo);
	if (err < 0) {
		write_log (_T("SERIAL_TCP: GetAddrInfoW() failed, %s:%s: %d\n"), name, port, WSAGetLastError ());
		goto end;
	}
	serialsocket = socket (socketinfo->ai_family, socketinfo->ai_socktype, socketinfo->ai_protocol);
	if (serialsocket == INVALID_SOCKET) {
		write_log(_T("SERIAL_TCP: socket() failed, %s:%s: %d\n"), name, port, WSAGetLastError ());
		goto end;
	}
	err = bind (serialsocket, socketinfo->ai_addr, socketinfo->ai_addrlen);
	if (err < 0) {
		write_log(_T("SERIAL_TCP: bind() failed, %s:%s: %d\n"), name, port, WSAGetLastError ());
		goto end;
	}
	err = listen (serialsocket, 1);
	if (err < 0) {
		write_log(_T("SERIAL_TCP: listen() failed, %s:%s: %d\n"), name, port, WSAGetLastError ());
		goto end;
	}
	err = setsockopt (serialsocket, SOL_SOCKET, SO_LINGER, (char*)&linger_1s, sizeof linger_1s);
	if (err < 0) {
		write_log(_T("SERIAL_TCP: setsockopt(SO_LINGER) failed, %s:%s: %d\n"), name, port, WSAGetLastError ());
		goto end;
	}
	err = setsockopt (serialsocket, SOL_SOCKET, SO_REUSEADDR, (char*)&one, sizeof one);
	if (err < 0) {
		write_log(_T("SERIAL_TCP: setsockopt(SO_REUSEADDR) failed, %s:%s: %d\n"), name, port, WSAGetLastError ());
		goto end;
	}

	if (waitmode) {
		while (tcp_is_connected () == false) {
			Sleep (1000);
			write_log (_T("SERIAL_TCP: waiting for connect...\n"));
		}
	}

	xfree (port);
	xfree (name);
	tcpserial = TRUE;
	return 1;
end:
	xfree (port);
	xfree (name);
	closetcp ();
	return 0;
}

int openser (const TCHAR *sername)
{
	COMMTIMEOUTS CommTimeOuts;

	if (!_tcsnicmp (sername, _T("TCP://"), 6)) {
		return opentcp (sername + 6);
	}
	if (!_tcsnicmp (sername, _T("TCP:"), 4)) {
		return opentcp (sername + 4);
	}

	if (!(readevent = CreateEvent (NULL, TRUE, FALSE, NULL))) {
		write_log (_T("SERIAL: Failed to create r event!\n"));
		return 0;
	}
	readol.hEvent = readevent;

	if (!(writeevent = CreateEvent (NULL, TRUE, FALSE, NULL))) {
		write_log (_T("SERIAL: Failed to create w event!\n"));
		return 0;
	}
	SetEvent (writeevent);
	writeol.hEvent = writeevent;

	uartbreak = 0;

	hCom = CreateFile (sername, GENERIC_READ | GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_OVERLAPPED,
		NULL);
	if (hCom == INVALID_HANDLE_VALUE) {
		write_log (_T("SERIAL: failed to open '%s' err=%d\n"), sername, GetLastError());
		closeser ();
		return 0;
	}

	SetCommMask (hCom, EV_RXFLAG);
	SetupComm (hCom, 65536, 128);
	PurgeComm (hCom, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);
	CommTimeOuts.ReadIntervalTimeout = 0xFFFFFFFF;
	CommTimeOuts.ReadTotalTimeoutMultiplier = 0;
	CommTimeOuts.ReadTotalTimeoutConstant = 0;
	CommTimeOuts.WriteTotalTimeoutMultiplier = 0;
	CommTimeOuts.WriteTotalTimeoutConstant = 0;
	SetCommTimeouts (hCom, &CommTimeOuts);

	dcb.DCBlength = sizeof (DCB);
	GetCommState (hCom, &dcb);

	dcb.fBinary = TRUE;
	dcb.BaudRate = 9600;
	dcb.ByteSize = 8;
	dcb.Parity = NOPARITY;
	dcb.fParity = FALSE;
	dcb.StopBits = currprefs.serial_stopbits;

	dcb.fDsrSensitivity = FALSE;
	dcb.fOutxDsrFlow = FALSE;
	dcb.fDtrControl = fDtrControl;

	if (currprefs.serial_hwctsrts) {
		dcb.fOutxCtsFlow = TRUE;
		dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
	} else {
		dcb.fRtsControl = fRtsControl;
		dcb.fOutxCtsFlow = FALSE;
	}

	dcb.fTXContinueOnXoff = FALSE;
	dcb.fOutX = FALSE;
	dcb.fInX = FALSE;

	dcb.fErrorChar = FALSE;
	dcb.fNull = FALSE;
	dcb.fAbortOnError = FALSE;

	if (SetCommState (hCom, &dcb)) {
		write_log (_T("SERIAL: Using %s CTS/RTS=%d\n"), sername, currprefs.serial_hwctsrts);
		return 1;
	}

	write_log (_T("SERIAL: serial driver didn't accept new parameters\n"));
	closeser();
	return 0;
}

void closeser (void)
{
	if (tcpserial) {
		closetcp ();
		tcpserial = FALSE;
	}
	if (hCom != INVALID_HANDLE_VALUE)  {
		CloseHandle (hCom);
		hCom = INVALID_HANDLE_VALUE;
	}
	if (midi_ready) {
		extern uae_u16 serper;
		Midi_Close ();
		//need for camd Midi Stuff(it close midi and reopen it but serial.c think the baudrate
		//is the same and do not open midi), so setting serper to different value helps
		serper = 0x30;
	}
	if(writeevent)
		CloseHandle(writeevent);
	writeevent = 0;
	if(readevent)
		CloseHandle(readevent);
	readevent = 0;
	uartbreak = 0;
}

static void outser (void)
{
	if (datainoutput <= 0)
		return;
	DWORD v = WaitForSingleObject (writeevent, 0);
	if (v == WAIT_OBJECT_0) {
		DWORD actual;
		memcpy (outputbufferout, outputbuffer, datainoutput);
		WriteFile (hCom, outputbufferout, datainoutput, &actual, &writeol);
		datainoutput = 0;
	}
}

void writeser_flush(void)
{
	outser();
}

void writeser (int c)
{
#if 0
	write_log(_T("writeser %04X (buf=%d)\n"), c, datainoutput);
#endif
	if (tcpserial) {
		if (tcp_is_connected ()) {
			char buf[1];
			buf[0] = (char)c;
			if (send (serialconn, buf, 1, 0) != 1) {
				tcp_disconnect ();
			}
		}
	} else if (midi_ready) {
		BYTE outchar = (BYTE)c;
		Midi_Parse (midi_output, &outchar);
	} else {
		if (hCom == INVALID_HANDLE_VALUE || !currprefs.use_serial)
			return;
		if (datainoutput + 1 < sizeof (outputbuffer)) {
			outputbuffer[datainoutput++] = c;
		} else {
			write_log (_T("serial output buffer overflow, data will be lost\n"));
			datainoutput = 0;
		}
		outser ();
	}
}

int checkserwrite (int spaceneeded)
{
	if (hCom == INVALID_HANDLE_VALUE || !currprefs.use_serial)
		return 1;
	if (midi_ready) {
		return 1;
	} else {
		outser ();
		if (datainoutput + spaceneeded >= sizeof (outputbuffer))
			return 0;
	}
	return 1;
}

int readseravail (void)
{
	COMSTAT ComStat;
	DWORD dwErrorFlags;

	if (tcpserial) {
		if (tcp_is_connected ()) {
			struct timeval tv;
			fd_set fd;
			tv.tv_sec = 0;
			tv.tv_usec = 0;
			fd.fd_array[0] = serialconn;
			fd.fd_count = 1;
			int err = select (1, &fd, NULL, NULL, &tv);
			if (err == SOCKET_ERROR) {
				tcp_disconnect ();
				return 0;
			}
			if (err > 0)
				return 1;
		}
		return 0;
	} else if (midi_ready) {
		if (ismidibyte ())
			return 1;
	} else {
		if (!currprefs.use_serial)
			return 0;
		if (dataininput > dataininputcnt)
			return 1;
		if (hCom != INVALID_HANDLE_VALUE)  {
			ClearCommError (hCom, &dwErrorFlags, &ComStat);
			if (ComStat.cbInQue > 0)
				return 1;
		}
	}
	return 0;
}

int readser (int *buffer)
{
	COMSTAT ComStat;
	DWORD dwErrorFlags;
	DWORD actual;

	if (tcpserial) {
		if (tcp_is_connected ()) {
			char buf[1];
			buf[0] = 0;
			int err = recv (serialconn, buf, 1, 0);
			if (err == 1) {
				*buffer = buf[0];
				//write_log(_T(" %02X "), buf[0]);
				return 1;
			} else {
				tcp_disconnect ();
			}
		}
		return 0;
	} else if (midi_ready) {
		*buffer = getmidibyte ();
		if (*buffer < 0)
			return 0;
		return 1;
	} else {
		if (!currprefs.use_serial)
			return 0;
		if (dataininput > dataininputcnt) {
			*buffer = inputbuffer[dataininputcnt++];
			return 1;
		}
		dataininput = 0;
		dataininputcnt = 0;
		if (hCom != INVALID_HANDLE_VALUE)  {
			/* only try to read number of bytes in queue */
			ClearCommError (hCom, &dwErrorFlags, &ComStat);
			if (ComStat.cbInQue)  {
				int len = ComStat.cbInQue;
				if (len > sizeof (inputbuffer))
					len = sizeof (inputbuffer);
				if (!ReadFile (hCom, inputbuffer, len, &actual, &readol))  {
					if (GetLastError() == ERROR_IO_PENDING)
						WaitForSingleObject (&readol, INFINITE);
					else
						return 0;
				}
				dataininput = actual;
				dataininputcnt = 0;
				if (actual == 0)
					return 0;
				return readser (buffer);
			}
		}
	}
	return 0;
}

void serialuartbreak (int v)
{
	if (hCom == INVALID_HANDLE_VALUE || !currprefs.use_serial)
		return;

	if (v)
		EscapeCommFunction (hCom, SETBREAK);
	else
		EscapeCommFunction (hCom, CLRBREAK);
}

void getserstat (int *pstatus)
{
	DWORD stat;
	int status = 0;

	*pstatus = 0;
	if (hCom == INVALID_HANDLE_VALUE || !currprefs.use_serial)
		return;

	GetCommModemStatus (hCom, &stat);
	if (stat & MS_CTS_ON)
		status |= TIOCM_CTS;
	if (stat & MS_RLSD_ON)
		status |= TIOCM_CAR;
	if (stat & MS_DSR_ON)
		status |= TIOCM_DSR;
	if (stat & MS_RING_ON)
		status |= TIOCM_RI;
	*pstatus = status;
}


void setserstat (int mask, int onoff)
{
	if (mask & TIOCM_DTR) {
		if (currprefs.use_serial && hCom != INVALID_HANDLE_VALUE) {
			EscapeCommFunction(hCom, onoff ? SETDTR : CLRDTR);
		}
		fDtrControl = onoff ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
	}
	if (!currprefs.serial_hwctsrts) {
		if (mask & TIOCM_RTS) {
			if (currprefs.use_serial && hCom != INVALID_HANDLE_VALUE) {
				EscapeCommFunction(hCom, onoff ? SETRTS : CLRRTS);
			}
			fRtsControl = onoff ? RTS_CONTROL_ENABLE : RTS_CONTROL_DISABLE;
		}
	}
}

int setbaud (long baud)
{
	if(baud == 31400 && currprefs.win32_midioutdev >= -1) {
		/* MIDI baud-rate */
		if (!midi_ready) {
			if (Midi_Open())
				write_log (_T("Midi enabled\n"));
		}
		return 1;
	} else {
		if (midi_ready) {
			Midi_Close();
		}
		if (!currprefs.use_serial)
			return 1;
		if (hCom != INVALID_HANDLE_VALUE)  {
			dcb.BaudRate = baud;
			if (!currprefs.serial_hwctsrts) {
				dcb.fRtsControl = fRtsControl;
			} else {
				dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
			}
			dcb.fDtrControl = fDtrControl;
			write_log(_T("SERIAL: baud rate %d. DTR=%d RTS=%d\n"), baud, dcb.fDtrControl, dcb.fRtsControl);
			if (!SetCommState (hCom, &dcb)) {
				write_log (_T("SERIAL: Error setting baud rate %d!\n"), baud);
				return 0;
			}
		}
	}
	return 1;
}

void initparallel (void)
{
	if (uae_boot_rom_type) {
		uaecptr a = here (); //this install the ahisound
		org (rtarea_base + 0xFFC0);
		calltrap (deftrapres (ahi_demux, 0, _T("ahi_winuae")));
		dw (RTS);
		org (a);
		init_ahi_v2 ();
	}
}

int flashscreen;

void doflashscreen (void)
{
	flashscreen = 10;
	init_colors(0);
	picasso_refresh(0);
	reset_drawing ();
	//flush_screen (gfxvidinfo.outbuffer, 0, 0);
}

void hsyncstuff (void)
	//only generate Interrupts when
	//writebuffer is complete flushed
	//check state of lwin rwin
{
	static int keycheck = 0;

#ifdef AHI
	{ //begin ahi_sound
		static int count;
		if (ahi_on) {
			count++;
			//15625/count freebuffer check
			if(count > ahi_pollrate) {
				ahi_updatesound (1);
				count = 0;
			}
		}
	} //end ahi_sound
#endif
#ifdef PARALLEL_PORT
	keycheck++;
	if(keycheck >= 1000)
	{
		if (prtopen)
			flushprtbuf ();
		{
			if (flashscreen > 0) {
				flashscreen--;
				if (flashscreen == 0) {
					init_colors(0);
					reset_drawing ();
					picasso_refresh(0);
					//flush_screen (gfxvidinfo.outbuffer, 0, 0);
				}
			}
		}
		keycheck = 0;
	}
	if (currprefs.parallel_autoflush_time && !currprefs.parallel_postscript_detection) {
		parflush++;
		if (parflush / ((currprefs.ntscmode ? MAXVPOS_NTSC : MAXVPOS_PAL) * MAXHPOS_PAL / maxhpos) >= currprefs.parallel_autoflush_time * 50) {
			flushprinter ();
			parflush = 0;
		}
	}
#endif
}

const static GUID GUID_DEVINTERFACE_PARALLEL = {0x97F76EF0,0xF883,0x11D0,
{0xAF,0x1F,0x00,0x00,0xF8,0x00,0x84,0x5C}};

static const GUID serportsguids[] =
{
	GUID_DEVINTERFACE_COMPORT,
	// GUID_DEVINTERFACE_MODEM
	{ 0x2C7089AA, 0x2E0E, 0x11D1, { 0xB1, 0x14, 0x00, 0xC0, 0x4F, 0xC2, 0xAA, 0xE4} }
};
static const GUID parportsguids[] =
{
	GUID_DEVINTERFACE_PARALLEL
};

static int enumports_2 (struct serparportinfo **pi, int cnt, bool parport)
{
	// Create a device information set that will be the container for
	// the device interfaces.
	HDEVINFO hDevInfo = INVALID_HANDLE_VALUE;
	SP_DEVICE_INTERFACE_DETAIL_DATA *pDetData = NULL;
	SP_DEVICE_INTERFACE_DATA ifcData;
	DWORD dwDetDataSize = sizeof (SP_DEVICE_INTERFACE_DETAIL_DATA) + 256 * sizeof (TCHAR);
	const GUID *guids = parport ? parportsguids : serportsguids;
	int guidcnt = parport ? sizeof(parportsguids)/sizeof(parportsguids[0]) : sizeof(serportsguids)/sizeof(serportsguids[0]);

	for (int guididx = 0; guididx < guidcnt; guididx++) {
		hDevInfo = SetupDiGetClassDevs (&guids[guididx], NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
		if(hDevInfo == INVALID_HANDLE_VALUE)
			continue;
		// Enumerate the serial ports
		pDetData = (SP_DEVICE_INTERFACE_DETAIL_DATA*)xmalloc (uae_u8, dwDetDataSize);
		// This is required, according to the documentation. Yes,
		// it's weird.
		ifcData.cbSize = sizeof (SP_DEVICE_INTERFACE_DATA);
		pDetData->cbSize = sizeof (SP_DEVICE_INTERFACE_DETAIL_DATA);
		BOOL bOk = TRUE;
		for (int ii = 0; bOk; ii++) {
			bOk = SetupDiEnumDeviceInterfaces (hDevInfo, NULL, &guids[guididx], ii, &ifcData);
			if (bOk) {
				// Got a device. Get the details.
				SP_DEVINFO_DATA devdata = { sizeof (SP_DEVINFO_DATA)};
				bOk = SetupDiGetDeviceInterfaceDetail (hDevInfo,
					&ifcData, pDetData, dwDetDataSize, NULL, &devdata);
				if (bOk) {
					// Got a path to the device. Try to get some more info.
					TCHAR fname[256];
					TCHAR desc[256];
					BOOL bSuccess = SetupDiGetDeviceRegistryProperty (
						hDevInfo, &devdata, SPDRP_FRIENDLYNAME, NULL,
						(PBYTE)fname, sizeof (fname), NULL);
					bSuccess = bSuccess && SetupDiGetDeviceRegistryProperty (
						hDevInfo, &devdata, SPDRP_DEVICEDESC, NULL,
						(PBYTE)desc, sizeof (desc), NULL);
					if (bSuccess && cnt < MAX_SERPAR_PORTS) {
						TCHAR *p;
						pi[cnt] = xcalloc (struct serparportinfo, 1);
						pi[cnt]->dev = my_strdup (pDetData->DevicePath);
						pi[cnt]->name = my_strdup (fname);
						p = _tcsstr (fname, parport ? _T("(LPT") : _T("(COM"));
						if (p && (p[5] == ')' || p[6] == ')')) {
							pi[cnt]->cfgname = xmalloc (TCHAR, 100);
							if (isdigit(p[5]))
								_stprintf (pi[cnt]->cfgname, parport ? _T("LPT%c%c") : _T("COM%c%c"), p[4], p[5]);
							else
								_stprintf (pi[cnt]->cfgname, parport ? _T("LPT%c") : _T("COM%c"), p[4]);
						} else {
							pi[cnt]->cfgname = my_strdup (pDetData->DevicePath);
						}
						write_log (_T("%s: '%s' = '%s' = '%s'\n"), parport ? _T("PARPORT") : _T("SERPORT"), pi[cnt]->name, pi[cnt]->cfgname, pi[cnt]->dev);
						cnt++;
					}
				} else {
					write_log (_T("SetupDiGetDeviceInterfaceDetail failed, err=%d"), GetLastError ());
					break;
				}
			} else {
				DWORD err = GetLastError ();
				if (err != ERROR_NO_MORE_ITEMS) {
					write_log (_T("SetupDiEnumDeviceInterfaces failed, err=%d"), err);
					break;
				}
			}
		}
		xfree(pDetData);
		if (hDevInfo != INVALID_HANDLE_VALUE)
			SetupDiDestroyDeviceInfoList (hDevInfo);
	}
	return cnt;
}

static struct serparportinfo *parports[MAX_SERPAR_PORTS];

int enumserialports (void)
{
	int cnt, i, j;
	TCHAR name[256];
	DWORD size = sizeof (COMMCONFIG);
	TCHAR devname[1000];

	write_log (_T("Serial port enumeration..\n"));
	cnt = 0;

#ifdef SERIAL_ENET
	comports[cnt].dev = my_strdup (_T("ENET:H"));
	comports[cnt].cfgname = my_strdup (comports[0].dev);
	comports[cnt].name = my_strdup (_T("NET (host)"));
	cnt++;
	comports[cnt].dev = my_strdup (_T("ENET:L"));
	comports[cnt].cfgname = my_strdup (comports[1].dev);
	comports[cnt].name = my_strdup (_T("NET (client)"));
	cnt++;
#endif

	cnt = enumports_2 (comports, cnt, false);
	j = 0;
	for (i = 0; i < 10; i++) {
		_stprintf (name, _T("COM%d"), i);
		if (!QueryDosDevice (name, devname, sizeof devname / sizeof (TCHAR)))
			continue;
		for(j = 0; j < cnt; j++) {
			if (!_tcscmp (comports[j]->cfgname, name))
				break;
		}
		if (j == cnt) {
			if (cnt >= MAX_SERPAR_PORTS)
				break;
			comports[j] = xcalloc(struct serparportinfo, 1);
			comports[j]->dev = xmalloc (TCHAR, 100);
			_stprintf (comports[cnt]->dev, _T("\\.\\\\%s"), name);
			comports[j]->cfgname = my_strdup (name);
			comports[j]->name = my_strdup (name);
			write_log (_T("SERPORT: %d:'%s' = '%s' (%s)\n"), cnt, comports[j]->name, comports[j]->dev, devname);
			cnt++;
			j++;
		}
	}

	for (i = 0; i < cnt; i++) {
		for (j = i + 1; j < cnt; j++) {
			if (_tcsicmp (comports[i]->name, comports[j]->name) > 0) {
				struct serparportinfo *spi;
				spi = comports[i];
				comports[i] = comports[j];
				comports[j] = spi;
			}
		}
	}


	if (cnt < MAX_SERPAR_PORTS) {
		comports[cnt] = xcalloc(struct serparportinfo, 1);
		comports[cnt]->dev = my_strdup (SERIAL_INTERNAL);
		comports[cnt]->cfgname = my_strdup (comports[cnt]->dev);
		comports[cnt]->name = my_strdup (_T("WinUAE inter-process serial port"));
		cnt++;
	}

	if (cnt < MAX_SERPAR_PORTS) {
		comports[cnt] = xcalloc(struct serparportinfo, 1);
		comports[cnt]->dev = my_strdup (_T("TCP://0.0.0.0:1234"));
		comports[cnt]->cfgname = my_strdup (comports[cnt]->dev);
		comports[cnt]->name = my_strdup (comports[cnt]->dev);
		cnt++;
	}
	if (cnt < MAX_SERPAR_PORTS) {
		comports[cnt] = xcalloc(struct serparportinfo, 1);
		comports[cnt]->dev = my_strdup (_T("TCP://0.0.0.0:1234/wait"));
		comports[cnt]->cfgname = my_strdup (comports[cnt]->dev);
		comports[cnt]->name = my_strdup (comports[cnt]->dev);
		cnt++;
	}

	write_log (_T("Parallel port enumeration..\n"));
	enumports_2 (parports, 0, true);
	write_log (_T("Port enumeration end\n"));

	return cnt;
}

int enummidiports (void)
{
	MIDIOUTCAPS midiOutCaps;
	MIDIINCAPS midiInCaps;
	int i, j, num, total;
	int innum, outnum;
	
	outnum = midiOutGetNumDevs();
	innum = midiInGetNumDevs();
	write_log (_T("MIDI port enumeration.. IN=%d OUT=%d\n"), innum, outnum);

	num = outnum;
	for (i = 0; i < num + 1 && i < MAX_MIDI_PORTS - 1; i++) {
		MMRESULT r = midiOutGetDevCaps ((UINT)(i - 1), &midiOutCaps, sizeof (midiOutCaps));
		if (r != MMSYSERR_NOERROR) {
			num = i;
			break;
		}
		midioutportinfo[i] = xcalloc (struct midiportinfo, 1);
		midioutportinfo[i]->name = my_strdup (midiOutCaps.szPname);
		midioutportinfo[i]->devid = i - 1;
		write_log (_T("MIDI OUT: %d:'%s' (%d/%d)\n"), midioutportinfo[i]->devid, midioutportinfo[i]->name, midiOutCaps.wMid, midiOutCaps.wPid);
	}
	total = num + 1;
	for (i = 1; i < num + 1; i++) {
		for (j = i + 1; j < num + 1; j++) {
			if (_tcsicmp (midioutportinfo[i]->name, midioutportinfo[j]->name) > 0) {
				struct midiportinfo *mi;
				mi = midioutportinfo[i];
				midioutportinfo[i] = midioutportinfo[j];
				midioutportinfo[j] = mi;
			}
		}
	}
	num = innum;
	for (i = 0; i < num && i < MAX_MIDI_PORTS - 1; i++) {
		if (midiInGetDevCaps (i, &midiInCaps, sizeof (midiInCaps)) != MMSYSERR_NOERROR) {
			num = i;
			break;
		}
		midiinportinfo[i] = xcalloc (struct midiportinfo, 1);
		midiinportinfo[i]->name = my_strdup (midiInCaps.szPname);
		midiinportinfo[i]->devid = i;
		write_log (_T("MIDI IN: %d:'%s' (%d/%d)\n"), midiinportinfo[i]->devid, midiinportinfo[i]->name, midiInCaps.wMid, midiInCaps.wPid);
	}
	total += num;
	for (i = 0; i < num; i++) {
		for (j = i + 1; j < num; j++) {
			if (_tcsicmp (midiinportinfo[i]->name, midiinportinfo[j]->name) > 0) {
				struct midiportinfo *mi;
				mi = midiinportinfo[i];
				midiinportinfo[i] = midiinportinfo[j];
				midiinportinfo[j] = mi;
			}
		}
	}

	write_log (_T("MIDI port enumeration end\n"));

	return total;
}


void sernametodev (TCHAR *sername)
{
	int i;

	for (i = 0; i < MAX_SERPAR_PORTS && comports[i]; i++) {
		if (!_tcscmp (sername, comports[i]->cfgname)) {
			_tcscpy (sername, comports[i]->dev);
			return;
		}
	}
	if (!_tcsncmp (sername, _T("TCP:"), 4))
		return;
	sername[0] = 0;
}

void serdevtoname (TCHAR *sername)
{
	int i;
	if (!_tcsncmp (sername, _T("TCP:"), 4))
		return;
	for (i = 0; i < MAX_SERPAR_PORTS && comports[i]; i++) {
		if (!_tcscmp (sername, comports[i]->dev)) {
			_tcscpy (sername, comports[i]->cfgname);
			return;
		}
	}
	sername[0] = 0;
}
