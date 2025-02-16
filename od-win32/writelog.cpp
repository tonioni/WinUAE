#include "sysconfig.h"
#include "sysdeps.h"

#include <windows.h>
#include <sys/timeb.h>

#include "options.h"
#include "custom.h"
#include "events.h"
#include "debug.h"
#include "debug_win32.h"
#include "win32.h"
#include "registry.h"
#include "uae.h"

static void premsg (void)
{
#if 0
	static int done;
	char *as;
	char ast[32];
	TCHAR *ws;

	if (done)
		return;
	done = 1;

	ast[0] = 'A';
	ast[1] = 0x7f;
	ast[2] = 0x80;
	ast[3] = 0x81;
	ast[4] = 0x9f;
	ast[5] = 0;
	ws = au_fs (ast);

	MessageBoxA(NULL, "español", "ANSI", MB_OK);
	MessageBoxW(NULL, _T("español"), _T("UTF-16"), MB_OK);

	as = ua (_T("español"));
	MessageBoxA(NULL, as, "ANSI:2", MB_OK);
	ws = au (as);
	MessageBoxW(NULL, ws, _T("UTF-16:2"), MB_OK);
	xfree (ws);
	xfree (as);

	ws = au ("español");
	MessageBoxW(NULL, ws, _T("UTF-16:3"), MB_OK);
	as = ua (ws);
	MessageBoxA(NULL, as, "ANSI:3", MB_OK);
	xfree (ws);
	xfree (as);
#endif
}

#define SHOW_CONSOLE 0

static int nodatestamps = 0;

int consoleopen = 0;
static int realconsole;
static HANDLE stdinput,stdoutput;
static int bootlogmode;
static CRITICAL_SECTION cs;
static int cs_init;

FILE *debugfile = NULL;
int console_logging = 0;
static int debugger_type = -1;
extern BOOL debuggerinitializing;
extern bool lof_store;
static int console_input_linemode = -1;
int always_flush_log = 0;
TCHAR *conlogfile = NULL;
static FILE *conlogfilehandle;
static HWND previousactivewindow;

#define WRITE_LOG_BUF_SIZE 4096

/* console functions for debugger */

bool is_console_open(void)
{
	return consoleopen;
}

static HWND myGetConsoleWindow (void)
{
	return GetConsoleWindow ();
}

static void set_console_input_mode(int line)
{
	if (console_input_linemode < 0)
		return;
	if (line == console_input_linemode)
		return;
	SetConsoleMode (stdinput, ENABLE_PROCESSED_INPUT | ENABLE_PROCESSED_OUTPUT | (line ? (ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT) : 0));
	console_input_linemode = line;
}

static BOOL WINAPI ctrlchandler(DWORD ct)
{
	if (ct == CTRL_C_EVENT || ct == CTRL_CLOSE_EVENT) {
		systray(hHiddenWnd, TRUE);
	}
	return FALSE;
}

static void getconsole (void)
{
	CONSOLE_SCREEN_BUFFER_INFO csbi;

	stdinput = GetStdHandle (STD_INPUT_HANDLE);
	stdoutput = GetStdHandle (STD_OUTPUT_HANDLE);
	SetConsoleMode (stdinput, ENABLE_PROCESSED_INPUT|ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT|ENABLE_PROCESSED_OUTPUT);
	console_input_linemode = 1;
	SetConsoleCP (65001);
	SetConsoleOutputCP (65001);
	if (GetConsoleScreenBufferInfo (stdoutput, &csbi)) {
		if (csbi.dwMaximumWindowSize.Y < 5000) {
			csbi.dwMaximumWindowSize.Y = 5000;
			SetConsoleScreenBufferSize (stdoutput, csbi.dwMaximumWindowSize);
		}
	}
	SetConsoleCtrlHandler(ctrlchandler, TRUE);
	SetConsoleCtrlHandler(NULL, TRUE);
}

static void flushmsgpump(void)
{
	MSG msg;
	while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

void deactivate_console(void)
{
	if (previousactivewindow) {
		SetForegroundWindow(previousactivewindow);
		previousactivewindow = NULL;
	}
}

void activate_console(void)
{
	if (!consoleopen) {
		previousactivewindow = NULL;
		return;
	}
	HWND w = GetForegroundWindow();
	HWND cw = GetConsoleWindow();
	if (cw != w) {
		previousactivewindow = w;
	}
	SetForegroundWindow(cw);
}

static void open_console_window (void)
{
	if (!consoleopen) {
		previousactivewindow = GetForegroundWindow();
	}
	AllocConsole ();
	getconsole ();
	consoleopen = -1;
	reopen_console ();
}

static void openconsole (void)
{
	if (realconsole) {
		if (debugger_type == 2) {
			open_debug_window ();
			consoleopen = 1;
		} else {
			close_debug_window ();
			consoleopen = -1;
		}
		return;
	}
	if (debugger_active && (debugger_type < 0 || debugger_type == 2)) {
		if (consoleopen > 0 || debuggerinitializing)
			return;
		if (debugger_type < 0) {
			regqueryint (NULL, _T("DebuggerType"), &debugger_type);
			if (debugger_type <= 0)
				debugger_type = 1;
			openconsole();
			return;
		}
		close_console ();
		if (open_debug_window ()) {
			consoleopen = 1;
			return;
		}
		open_console_window ();
	} else {
		if (consoleopen < 0)
			return;
		close_console ();
		open_console_window ();
	}
}

void debugger_change (int mode)
{
	if (mode < 0)
		debugger_type = debugger_type == 2 ? 1 : 2;
	else
		debugger_type = mode;
	if (debugger_type != 1 && debugger_type != 2)
		debugger_type = 2;
	regsetint (NULL, _T("DebuggerType"), debugger_type);
	openconsole ();
}

void open_console(void)
{
	if (!consoleopen) {
		openconsole();
	}
}

void reopen_console (void)
{
	HWND hwnd;

	if (realconsole)
		return;
	if (consoleopen >= 0)
		return;
	hwnd = myGetConsoleWindow ();
	if (hwnd) {
		int newpos = 1;
		int x, y, w, h;
		if (!regqueryint (NULL, _T("LoggerPosX"), &x))
			newpos = 0;
		if (!regqueryint (NULL, _T("LoggerPosY"), &y))
			newpos = 0;
		if (!regqueryint (NULL, _T("LoggerPosW"), &w))
			newpos = 0;
		if (!regqueryint (NULL, _T("LoggerPosH"), &h))
			newpos = 0;
		if (newpos) {
			RECT rc;
			int dpi = getdpiforwindow(hwnd);
			x = x * dpi / 96;
			y = y * dpi / 96;
			w = w * dpi / 96;
			h = h * dpi / 96;
			rc.left = x;
			rc.top = y;
			rc.right = x + w;
			rc.bottom = y + h;
			if (MonitorFromRect (&rc, MONITOR_DEFAULTTONULL) != NULL) {
				SetForegroundWindow (hwnd);
				SetWindowPos (hwnd, HWND_TOP, x, y, w, h, SWP_NOACTIVATE);

			}
		}
	}
}

void close_console (void)
{
	if (realconsole)
		return;
	if (consoleopen > 0) {
		close_debug_window ();
	} else if (consoleopen < 0) {
		HWND hwnd = myGetConsoleWindow ();
		if (hwnd && !IsIconic (hwnd)) {
			RECT r;
			if (GetWindowRect (hwnd, &r)) {
				r.bottom -= r.top;
				r.right -= r.left;
				int dpi = getdpiforwindow(hwnd);
				r.left = r.left * 96 / dpi;
				r.right = r.right * 96 / dpi;
				r.top = r.top * 96 / dpi;
				r.bottom = r.bottom * 96 / dpi;
				regsetint (NULL, _T("LoggerPosX"), r.left);
				regsetint (NULL, _T("LoggerPosY"), r.top);
				regsetint (NULL, _T("LoggerPosW"), r.right);
				regsetint (NULL, _T("LoggerPosH"), r.bottom);
			}
		}
		FreeConsole ();
	}
	consoleopen = 0;
}

int read_log(void)
{
#if 0
	return -1;
#else
	if (consoleopen >= 0)
		return -1;
	set_console_input_mode(0);
	INPUT_RECORD irbuf;
	DWORD numread;
	for (;;) {
		if (!PeekConsoleInput(stdinput, &irbuf, 1, &numread))
			return -1;
		if (!numread)
			return -1;
		if (!ReadConsoleInput(stdinput, &irbuf, 1, &numread))
			return -1;
		if (irbuf.EventType != KEY_EVENT)
			continue;
		if (!irbuf.Event.KeyEvent.bKeyDown)
			continue;
		int ch = irbuf.Event.KeyEvent.uChar.AsciiChar;
		if (ch == 0)
			continue;
		return ch;
	}
#endif
}

static void writeconsole_2(const TCHAR *buffer)
{
	DWORD temp;

	if (!consoleopen)
		openconsole();

	if (consoleopen > 0) {
		WriteOutput(buffer, uaetcslen(buffer));
	} else if (realconsole) {
		fputws(buffer, stdout);
		fflush(stdout);
	} else if (consoleopen < 0) {
		WriteConsole(stdoutput, buffer, uaetcslen(buffer), &temp, 0);
	}
}

static void writeconsole (const TCHAR *buffer)
{
	if (_tcslen (buffer) > 256) {
		TCHAR *p = my_strdup (buffer);
		TCHAR *p2 = p;
		while (_tcslen (p) > 256) {
			TCHAR tmp = p[256];
			p[256] = 0;
			writeconsole_2 (p);
			p[256] = tmp;
			p += 256;
		}
		writeconsole_2 (p);
		xfree (p2);
	} else {
		writeconsole_2 (buffer);
	}
}

static void flushconsole (void)
{
	if (consoleopen > 0) {
		fflush (stdout);
	} else if (realconsole) {
		fflush (stdout);
	} else if (consoleopen < 0) {
		FlushFileBuffers  (stdoutput);
	}
}

static TCHAR *console_buffer;
static int console_buffer_size;

TCHAR *setconsolemode (TCHAR *buffer, int maxlen)
{
	TCHAR *ret = NULL;
	if (buffer) {
		console_buffer = buffer;
		console_buffer_size = maxlen;
	} else {
		ret = console_buffer;
		console_buffer = NULL;
	}
	return ret;
}

static void console_put (const TCHAR *buffer)
{
	if (console_buffer) {
		if (_tcslen (console_buffer) + _tcslen (buffer) < console_buffer_size)
			_tcscat (console_buffer, buffer);
	} else if (consoleopen) {
		openconsole ();
		writeconsole (buffer);
	}
	if (conlogfile) {
		if (!conlogfilehandle) {
			conlogfilehandle = _tfopen(conlogfile, _T("w"));
		}
		if (conlogfilehandle) {
			fputws(buffer, conlogfilehandle);
			fflush(conlogfilehandle);
		}
	}
}

static int console_buf_len = 100000;

void console_out_f (const TCHAR *format,...)
{
	int len;
	va_list parms;
	TCHAR *pbuf;
	TCHAR buffer[WRITE_LOG_BUF_SIZE];
	TCHAR *bigbuf = NULL;

	pbuf = buffer;
	va_start (parms, format);
	len = _vsntprintf (pbuf, WRITE_LOG_BUF_SIZE - 1, format, parms);
	if (!len)
		return;
	if (len < 0 || len >= WRITE_LOG_BUF_SIZE - 2) {
		int buflen = console_buf_len;
		for (;;) {
			bigbuf = xmalloc(TCHAR, buflen);
			if (!bigbuf)
				return;
			len = _vsntprintf(bigbuf, buflen - 1, format, parms);
			if (!len)
				return;
			if (len > 0 && len < buflen - 2)
				break;
			xfree(bigbuf);
			buflen += 100000;
			if (buflen > 10000000)
				return;
		}
		pbuf = bigbuf;
		console_buf_len = buflen;
	}
	va_end (parms);
	console_put (pbuf);
	if (bigbuf)
		xfree(bigbuf);
}
void console_out (const TCHAR *txt)
{
	console_put (txt);
}

bool console_isch (void)
{
	flushmsgpump();
	if (console_buffer) {
		return 0;
	} else if (realconsole) {
		return false;
	} else if (consoleopen < 0) {
		DWORD events = 0;
		GetNumberOfConsoleInputEvents (stdinput, &events);
		return events > 0;
	}
	return false;
}

TCHAR console_getch (void)
{
	flushmsgpump();
	if (console_buffer) {
		return 0;
	} else if (realconsole) {
		return getwc (stdin);
	} else if (consoleopen < 0) {
		DWORD len;
		TCHAR out[2];
		
		for (;;) {
			out[0] = 0;
			ReadConsole (stdinput, out, 1, &len, 0);
			if (len > 0)
				return out[0];
		}
	}
	return 0;
}

int console_get (TCHAR *out, int maxlen)
{
	*out = 0;

	flushmsgpump();
	set_console_input_mode(1);
	if (consoleopen > 0) {
		return console_get_gui (out, maxlen);
	} else if (realconsole) {
		DWORD totallen;

		*out = 0;
		totallen = 0;
		while (maxlen > 0) {
			*out = getwc (stdin);
			if (*out == 13)
				break;
			out++;
			maxlen--;
			totallen++;
		}
		*out = 0;
		return totallen;
	} else if (consoleopen < 0) {
		DWORD len, totallen;

		*out = 0;
		totallen = 0;
		while(maxlen > 0) {
			ReadConsole (stdinput, out, 1, &len, 0);
			if(*out == 13)
				break;
			out++;
			maxlen--;
			totallen++;
		}
		*out = 0;
		return totallen;
	}
	return 0;
}


void console_flush (void)
{
	flushconsole ();
}

static int lfdetected = 1;

TCHAR *write_log_get_ts(void)
{
	struct tm *t;
	struct _timeb tb;
	static TCHAR out[100];
	TCHAR *p;
	static TCHAR lastts[100];
	TCHAR curts[100];

	if (bootlogmode)
		return NULL;
	if (nodatestamps)
		return NULL;
	if (!vsync_counter)
		return NULL;
	_ftime (&tb);
	t = localtime (&tb.time);
	_tcsftime (curts, sizeof curts / sizeof (TCHAR), _T("%Y-%m-%d %H:%M:%S\n"), t);
	p = out;
	*p = 0;
	if (_tcsncmp (curts, lastts, _tcslen (curts) - 3)) { // "xx\n"
		_tcscat (p, curts);
		p += _tcslen (p);
		_tcscpy (lastts, curts);
	}
	_tcsftime (p, sizeof out / sizeof (TCHAR) - (p - out) , _T("%S-"), t);
	p += _tcslen (p);
	_stprintf (p, _T("%03d"), tb.millitm);
	p += _tcslen (p);
	if (vsync_counter != 0xffffffff)
		_stprintf (p, _T(" [%d %03d%s%03d/%03d]"), vsync_counter, current_hpos_safe(), lof_store ? _T("-") : _T("="), vpos, linear_vpos);
	_tcscat (p, _T(": "));
	return out;
}

void write_dlog (const TCHAR *format, ...)
{
	int count;
	TCHAR buffer[WRITE_LOG_BUF_SIZE];
	int bufsize = WRITE_LOG_BUF_SIZE;
	TCHAR *bufp;
	va_list parms;

	if (!cs_init)
		return;

	premsg ();

	EnterCriticalSection (&cs);
	va_start (parms, format);
	bufp = buffer;
	for (;;) {
		count = _vsntprintf (bufp, bufsize - 1, format, parms);
		if (count < 0) {
			bufsize *= 10;
			if (bufp != buffer)
				xfree (bufp);
			bufp = xmalloc (TCHAR, bufsize);
			continue;
		}
		break;
	}
	bufp[bufsize - 1] = 0;
	if (!_tcsncmp (bufp, _T("write "), 6))
		bufsize--;
	if (bufp[0] == '*')
		count++;
	if (SHOW_CONSOLE || console_logging) {
		writeconsole (bufp);
	}
	if (debugfile) {
		_ftprintf (debugfile, _T("%s"), bufp);
	}
	lfdetected = 0;
	if (bufp[0] != '\0' && bufp[_tcslen(bufp) - 1] == '\n')
		lfdetected = 1;
	va_end (parms);
	if (bufp != buffer)
		xfree (bufp);
	if (always_flush_log)
		flush_log ();
	LeaveCriticalSection (&cs);
}

void write_log (const char *format, ...)
{
	char buffer[WRITE_LOG_BUF_SIZE];
	va_list parms;
	TCHAR *b;

	va_start (parms, format);
	vsprintf (buffer, format, parms);
	b = au (buffer);
	write_log (b);
	xfree (b);
	va_end (parms);
}

void write_logx(const TCHAR *format, ...)
{
	int count;
	TCHAR buffer[WRITE_LOG_BUF_SIZE];
	int bufsize = WRITE_LOG_BUF_SIZE;
	TCHAR *bufp;
	va_list parms;

	if (!cs_init)
		return;

	EnterCriticalSection (&cs);
	va_start (parms, format);
	bufp = buffer;
	for (;;) {
		count = _vsntprintf (bufp, bufsize - 1, format, parms);
		if (count < 0) {
			bufsize *= 10;
			if (bufp != buffer)
				xfree (bufp);
			bufp = xmalloc (TCHAR, bufsize);
			continue;
		}
		break;
	}
	bufp[bufsize - 1] = 0;
	if (consoleopen) {
		writeconsole (bufp);
	}
	if (debugfile) {
		_ftprintf (debugfile, _T("%s"), bufp);
	}
	lfdetected = 0;
	if (bufp[0] != '\0' && bufp[_tcslen (bufp) - 1] == '\n')
		lfdetected = 1;
	va_end (parms);
	if (bufp != buffer)
		xfree (bufp);
	LeaveCriticalSection (&cs);
}

void write_log (const TCHAR *format, ...)
{
	int count;
	TCHAR buffer[WRITE_LOG_BUF_SIZE], *ts;
	int bufsize = WRITE_LOG_BUF_SIZE;
	TCHAR *bufp;
	va_list parms;

	if (!SHOW_CONSOLE && !console_logging && !debugfile)
		return;

	if (!cs_init)
		return;

	premsg ();

	if (!_tcsicmp (format, _T("*")))
		count = 0;

	EnterCriticalSection (&cs);
	va_start (parms, format);
	bufp = buffer;
	for (;;) {
		count = _vsntprintf (bufp, bufsize - 1, format, parms);
		if (count < 0) {
			bufsize *= 10;
			if (bufp != buffer)
				xfree (bufp);
			bufp = xmalloc (TCHAR, bufsize);
			continue;
		}
		break;
	}
	bufp[bufsize - 1] = 0;
	if (!_tcsncmp (bufp, _T("write "), 6))
		bufsize--;
	ts = write_log_get_ts();
	if (bufp[0] == '*')
		count++;
	if (SHOW_CONSOLE || console_logging) {
		if (lfdetected && ts)
			writeconsole (ts);
		writeconsole (bufp);
	}
	if (debugfile) {
		if (lfdetected && ts)
			_ftprintf (debugfile, _T("%s"), ts);
		_ftprintf (debugfile, _T("%s"), bufp);
	}

#if 0
	static int is_debugger_present = -1;
	if (is_debugger_present == -1) {
		is_debugger_present = IsDebuggerPresent();
	}
	if (is_debugger_present) {
		OutputDebugString(bufp);
	}
#endif

	lfdetected = 0;
	if (bufp[0] != '\0' && bufp[_tcslen (bufp) - 1] == '\n')
		lfdetected = 1;
	va_end (parms);
	if (bufp != buffer)
		xfree (bufp);
	if (always_flush_log)
		flush_log ();
	LeaveCriticalSection (&cs);
}

void flush_log (void)
{
	if (debugfile)
		fflush (debugfile);
	flushconsole ();
}

void f_out (void *f, const TCHAR *format, ...)
{
	int count;
	TCHAR buffer[WRITE_LOG_BUF_SIZE];
	va_list parms;
	va_start (parms, format);

	if (f == NULL || !consoleopen)
		return;
	count = _vsntprintf (buffer, WRITE_LOG_BUF_SIZE - 1, format, parms);
	openconsole ();
	writeconsole (buffer);
	va_end (parms);
}

TCHAR *buf_out(TCHAR *buffer, int *bufsize, const TCHAR *format, ...)
{
	int count;
	va_list parms;
	va_start (parms, format);

	if (buffer == NULL)
		return 0;
	count = _vsntprintf(buffer, (*bufsize) - 1, format, parms);
	va_end (parms);
	*bufsize -= uaetcslen(buffer);
	return buffer + uaetcslen(buffer);
}

FILE *log_open (const TCHAR *name, int append, int bootlog, TCHAR *outpath)
{
	FILE *f = NULL;

	if (!cs_init)
		InitializeCriticalSection (&cs);
	cs_init = 1;

	if (name != NULL) {
		if (bootlog >= 0) {
			_tcscpy (outpath, name);
			f = _tfopen (name, append ? _T("a, ccs=UTF-8") : _T("wt, ccs=UTF-8"));
			if (!f && bootlog) {
				TCHAR tmp[MAX_DPATH];
				tmp[0] = 0;
				if (GetTempPath (MAX_DPATH, tmp) > 0) {
					_tcscat (tmp, _T("winuaetemplog.txt"));
					_tcscpy (outpath, tmp);
					f = _tfopen (tmp, append ? _T("a, ccs=UTF-8") : _T("wt, ccs=UTF-8"));
				}
			}
		}
	} else if (1) {
		TCHAR *c = GetCommandLine ();
		if (_tcsstr (c, _T(" -console"))) {
			if (GetStdHandle (STD_INPUT_HANDLE) && GetStdHandle (STD_OUTPUT_HANDLE)) {
				consoleopen = -1;
				realconsole = 1;
				getconsole ();
				_setmode(_fileno (stdout), _O_U16TEXT);
				_setmode(_fileno (stdin), _O_U16TEXT);
			}
		}
	}
	bootlogmode = bootlog;
	return f;
}

void log_close (FILE *f)
{
	if (f)
		fclose (f);
}

void jit_abort (const TCHAR *format,...)
{
	static int happened;
	int count;
	TCHAR buffer[WRITE_LOG_BUF_SIZE];
	va_list parms;
	va_start (parms, format);

	count = _vsntprintf (buffer, WRITE_LOG_BUF_SIZE - 1, format, parms);
	writeconsole (buffer);
	va_end (parms);
	if (!happened)
		gui_message (_T("JIT: Serious error:\n%s"), buffer);
	happened = 1;
	uae_reset (1, 0);
}

void jit_abort(const char *format, ...)
{
	char buffer[WRITE_LOG_BUF_SIZE];
	va_list parms;
	TCHAR *b;

	va_start(parms, format);
	vsprintf(buffer, format, parms);
	b = au(buffer);
	jit_abort(_T("%s"), b);
	xfree(b);
	va_end(parms);
}
