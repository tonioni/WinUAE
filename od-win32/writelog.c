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

#define SHOW_CONSOLE 0

int consoleopen = 0;
static HANDLE stdinput,stdoutput;
static int bootlogmode;
static CRITICAL_SECTION cs;
static int cs_init;

FILE *debugfile = NULL;
int console_logging = 0;
static LONG debugger_type = -1;
extern BOOL debuggerinitializing;

#define WRITE_LOG_BUF_SIZE 4096

/* console functions for debugger */

typedef HWND (CALLBACK* GETCONSOLEWINDOW)(void);

static HWND myGetConsoleWindow (void)
{
    GETCONSOLEWINDOW pGetConsoleWindow;
    /* Windows 2000 or newer only */
    pGetConsoleWindow = (GETCONSOLEWINDOW)GetProcAddress (
	GetModuleHandle ("kernel32.dll"), "GetConsoleWindow");
    if (pGetConsoleWindow)
	return pGetConsoleWindow ();
    return NULL;
}

static void open_console_window (void)
{
    AllocConsole();
    stdinput = GetStdHandle (STD_INPUT_HANDLE);
    stdoutput = GetStdHandle (STD_OUTPUT_HANDLE);
    SetConsoleMode (stdinput,ENABLE_PROCESSED_INPUT|ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT|ENABLE_PROCESSED_OUTPUT);
    consoleopen = -1;
    reopen_console ();
}

static void openconsole( void)
{
    if (debugger_active && (debugger_type < 0 || debugger_type == 2)) {
	if (consoleopen > 0 || debuggerinitializing)
	    return;
	if (debugger_type < 0) {
	    regqueryint (NULL, "DebuggerType", &debugger_type);
	    if (debugger_type <= 0)
		debugger_type = 2;
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
    regsetint (NULL, "DebuggerType", debugger_type);
    openconsole ();
}

void reopen_console (void)
{
    HWND hwnd;

    if (consoleopen >= 0)
	return;
    hwnd = myGetConsoleWindow ();
    if (hwnd) {
	int newpos = 1;
	LONG x, y, w, h;
	if (!regqueryint (NULL, "LoggerPosX", &x))
	    newpos = 0;
	if (!regqueryint (NULL, "LoggerPosY", &y))
	    newpos = 0;
	if (!regqueryint (NULL, "LoggerPosW", &w))
	    newpos = 0;
	if (!regqueryint (NULL, "LoggerPosH", &h))
	    newpos = 0;
	if (newpos) {
	    RECT rc;
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
    if (consoleopen > 0) {
	close_debug_window ();
    } else if (consoleopen < 0) {
	HWND hwnd = myGetConsoleWindow ();
	if (hwnd) {
	    RECT r;
	    if (GetWindowRect (hwnd, &r)) {
		r.bottom -= r.top;
		r.right -= r.left;
		regsetint (NULL, "LoggerPosX", r.left);
		regsetint (NULL, "LoggerPosY", r.top);
		regsetint (NULL, "LoggerPosW", r.right);
		regsetint (NULL, "LoggerPosH", r.bottom);
	    }
	}
	FreeConsole ();
    }
    consoleopen = 0;
}

static void writeconsole (const char *buffer)
{
    DWORD temp;
    if (!consoleopen)
	openconsole();
    if (consoleopen > 0)
	WriteOutput (buffer, strlen(buffer));
    else if (consoleopen < 0)
	WriteConsole (stdoutput, buffer, strlen (buffer), &temp,0);
}

void console_out_f (const char *format,...)
{
    va_list parms;
    char buffer[WRITE_LOG_BUF_SIZE];

    va_start (parms, format);
    _vsnprintf (buffer, WRITE_LOG_BUF_SIZE-1, format, parms);
    va_end (parms);
    openconsole ();
    writeconsole (buffer);
}
void console_out (const char *txt)
{
    openconsole ();
    writeconsole (txt);
}

int console_get (char *out, int maxlen)
{
    *out = 0;
    if (consoleopen > 0) {
	return console_get_gui (out, maxlen);
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
}

static int lfdetected = 1;

static char *writets (void)
{
    struct tm *t;
    struct _timeb tb;
    static char out[100];
    char *p;
    static char lastts[100];
    char curts[100];

    if (bootlogmode)
	return NULL;
    _ftime(&tb);
    t = localtime (&tb.time);
    strftime (curts, sizeof curts, "%Y-%m-%d %H:%M:%S\n", t);
    p = out;
    *p = 0;
    if (memcmp (curts, lastts, strlen (curts))) {
	strcat (p, curts);
	p += strlen (p);
	strcpy (lastts, curts);
    }
    strftime (p, sizeof out - (p - out) , "%S-", t);
    p += strlen (p);
    sprintf (p, "%03d", tb.millitm);
    p += strlen (p);
    if (timeframes || vpos > 0 && current_hpos () > 0)
	sprintf (p, " [%d %03dx%03d]", timeframes, current_hpos (), vpos);
    strcat (p, ": ");
    return out;
}


void write_dlog (const char *format, ...)
{
    int count;
    char buffer[WRITE_LOG_BUF_SIZE];
    char *ts;
    va_list parms;

    if (!SHOW_CONSOLE && !console_logging && !debugfile)
	return;

    EnterCriticalSection (&cs);
    va_start (parms, format);
    count = _vsnprintf(buffer, WRITE_LOG_BUF_SIZE-1, format, parms);
    ts = writets ();
    if (SHOW_CONSOLE || console_logging) {
	if (lfdetected && ts)
	    writeconsole (ts);
	writeconsole (buffer);
    }
    if (debugfile) {
	if (lfdetected && ts)
	    fprintf (debugfile, ts);
	fprintf (debugfile, buffer);
    }
    lfdetected = 0;
    if (strlen (buffer) > 0 && buffer[strlen(buffer) - 1] == '\n')
	lfdetected = 1;
    va_end (parms);
    LeaveCriticalSection (&cs);
}

void write_log (const char *format, ...)
{
    int count;
    char buffer[WRITE_LOG_BUF_SIZE], *ts;
    int bufsize = WRITE_LOG_BUF_SIZE;
    char *bufp;
    va_list parms;

    EnterCriticalSection(&cs);
    va_start(parms, format);
    bufp = buffer;
    for (;;) {
	count = _vsnprintf(bufp, bufsize - 1, format, parms);
	if (count < 0) {
	    bufsize *= 10;
	    if (bufp != buffer)
		xfree (bufp);
    	    bufp = xmalloc (bufsize);
	    continue;
	}
	break;
    }
    bufp[bufsize - 1] = 0;
    if (!memcmp (bufp, "write ",6))
	bufsize--;
    ts = writets ();
    if (bufp[0] == '*')
	count++;
    if (SHOW_CONSOLE || console_logging) {
	if (lfdetected && ts)
	    writeconsole (ts);
	writeconsole (bufp);
    }
    if (debugfile) {
	if (lfdetected && ts)
	    fprintf (debugfile, ts);
	fprintf (debugfile, bufp);
    }
    lfdetected = 0;
    if (strlen (bufp) > 0 && bufp[strlen(bufp) - 1] == '\n')
	lfdetected = 1;
    va_end (parms);
    if (bufp != buffer)
	xfree (bufp);
    LeaveCriticalSection(&cs);
}

void flush_log (void)
{
    if (debugfile)
	fflush (debugfile);
}

void f_out (void *f, const char *format, ...)
{
    int count;
    char buffer[WRITE_LOG_BUF_SIZE];
    va_list parms;
    va_start (parms, format);

    if (f == NULL)
	return;
    count = _vsnprintf (buffer, WRITE_LOG_BUF_SIZE - 1, format, parms);
    openconsole ();
    writeconsole (buffer);
    va_end (parms);
}

char* buf_out (char *buffer, int *bufsize, const char *format, ...)
{
    int count;
    va_list parms;
    va_start (parms, format);

    if (buffer == NULL)
	return 0;
    count = _vsnprintf (buffer, (*bufsize)-1, format, parms);
    va_end (parms);
    *bufsize -= strlen (buffer);
    return buffer + strlen (buffer);
}

void *log_open (const char *name, int append, int bootlog)
{
    FILE *f;

    f = fopen (name, append ? "a" : "wt");
    bootlogmode = bootlog;
    if (!cs_init)
	InitializeCriticalSection (&cs);
    cs_init = 1;
    return f;
}

void log_close (void *f)
{
    fclose (f);
}

void jit_abort (const char *format,...)
{
    static int happened;
    int count;
    char buffer[WRITE_LOG_BUF_SIZE];
    va_list parms;
    va_start (parms, format);

    count = _vsnprintf (buffer, WRITE_LOG_BUF_SIZE - 1, format, parms);
    writeconsole (buffer);
    va_end (parms);
    if (!happened)
	gui_message ("JIT: Serious error:\n%s", buffer);
    happened = 1;
    uae_reset (1);
}



