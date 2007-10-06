#include "sysconfig.h"
#include "sysdeps.h"

#include <windows.h>
#include <sys/timeb.h>

#include "custom.h"
#include "events.h"
#include "debug.h"
#include "debug_win32.h"
#include "win32.h"

#define SHOW_CONSOLE 0

int consoleopen = 0;
static HANDLE stdinput,stdoutput;
static int bootlogmode;
static CRITICAL_SECTION cs;
static int cs_init;

FILE *debugfile = NULL;
int console_logging;
static LONG debugger_type = -1;

#define WRITE_LOG_BUF_SIZE 4096

/* console functions for debugger */

typedef HWND (CALLBACK* GETCONSOLEWINDOW)(void);

static HWND myGetConsoleWindow(void)
{
    GETCONSOLEWINDOW pGetConsoleWindow;
    /* Windows 2000 or newer only */
    pGetConsoleWindow = (GETCONSOLEWINDOW)GetProcAddress(
	GetModuleHandle("kernel32.dll"), "GetConsoleWindow");
    if (pGetConsoleWindow)
	return pGetConsoleWindow();
    return NULL;
}

static void open_console_window(void)
{
    AllocConsole();
    stdinput = GetStdHandle(STD_INPUT_HANDLE);
    stdoutput = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleMode(stdinput,ENABLE_PROCESSED_INPUT|ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT|ENABLE_PROCESSED_OUTPUT);
    consoleopen = -1;
    reopen_console();
}

static void openconsole(void)
{
    if (debugger_active && (debugger_type < 0 || debugger_type == 2)) {
	if (consoleopen > 0)
	    return;
	if (debugger_type < 0) {
	    DWORD regkeytype;
	    DWORD regkeysize = sizeof(LONG);
	    if (hWinUAEKey)
		RegQueryValueEx (hWinUAEKey, "DebuggerType", 0, &regkeytype, (LPBYTE)&debugger_type, &regkeysize);
	    if (debugger_type <= 0)
		debugger_type = 2;
	    openconsole();
	    return;
	}
	close_console();
	if (open_debug_window()) {
	    consoleopen = 1;
	    return;
	}
	open_console_window();
    } else {
	if (consoleopen < 0)
	    return;
	close_console();
	open_console_window();
    }
}

void debugger_change(int mode)
{
    if (mode < 0)
	debugger_type = debugger_type == 2 ? 1 : 2;
    else
	debugger_type = mode;
    if (debugger_type != 1 && debugger_type != 2)
	debugger_type = 2;
    if (hWinUAEKey)
	RegSetValueEx (hWinUAEKey, "DebuggerType", 0, REG_DWORD, (LPBYTE)&debugger_type, sizeof(LONG));
    openconsole();
}

void reopen_console(void)
{
    HWND hwnd;

    if (consoleopen >= 0)
	return;
    hwnd = myGetConsoleWindow();
    if (hwnd && hWinUAEKey) {
	int newpos = 1;
	LONG x, y, w, h;
	DWORD regkeytype;
	DWORD regkeysize = sizeof(LONG);
	if (RegQueryValueEx (hWinUAEKey, "LoggerPosX", 0, &regkeytype, (LPBYTE)&x, &regkeysize) != ERROR_SUCCESS)
	    newpos = 0;
	if (RegQueryValueEx (hWinUAEKey, "LoggerPosY", 0, &regkeytype, (LPBYTE)&y, &regkeysize) != ERROR_SUCCESS)
	    newpos = 0;
	if (RegQueryValueEx (hWinUAEKey, "LoggerPosW", 0, &regkeytype, (LPBYTE)&w, &regkeysize) != ERROR_SUCCESS)
	    newpos = 0;
	if (RegQueryValueEx (hWinUAEKey, "LoggerPosH", 0, &regkeytype, (LPBYTE)&h, &regkeysize) != ERROR_SUCCESS)
	    newpos = 0;
	if (newpos) {
	    RECT rc;
	    rc.left = x;
	    rc.top = y;
	    rc.right = x + w;
	    rc.bottom = y + h;
	    if (MonitorFromRect (&rc, MONITOR_DEFAULTTONULL) != NULL) {
		SetForegroundWindow(hwnd);
		SetWindowPos(hwnd, HWND_TOP, x, y, w, h, SWP_NOACTIVATE);

	    }
	}
    }
}

void close_console(void)
{
    if (consoleopen > 0) {
	close_debug_window();
    } else if (consoleopen < 0) {
	HWND hwnd = myGetConsoleWindow();
	if (hwnd && hWinUAEKey) {
	    RECT r;
	    if (GetWindowRect (hwnd, &r)) {
		r.bottom -= r.top;
		r.right -= r.left;
		RegSetValueEx (hWinUAEKey, "LoggerPosX", 0, REG_DWORD, (LPBYTE)&r.left, sizeof(LONG));
		RegSetValueEx (hWinUAEKey, "LoggerPosY", 0, REG_DWORD, (LPBYTE)&r.top, sizeof(LONG));
		RegSetValueEx (hWinUAEKey, "LoggerPosW", 0, REG_DWORD, (LPBYTE)&r.right, sizeof(LONG));
		RegSetValueEx (hWinUAEKey, "LoggerPosH", 0, REG_DWORD, (LPBYTE)&r.bottom, sizeof(LONG));
	    }
	}
	FreeConsole();
    }
    consoleopen = 0;
}

static void writeconsole(char *buffer)
{
    DWORD temp;
    if (!consoleopen)
	openconsole();
    if (consoleopen > 0)
	WriteOutput(buffer, strlen(buffer));
    else if (consoleopen < 0)
	WriteConsole(stdoutput, buffer, strlen(buffer), &temp,0);
}

void console_out (const char *format,...)
{
    va_list parms;
    char buffer[WRITE_LOG_BUF_SIZE];

    va_start (parms, format);
    _vsnprintf (buffer, WRITE_LOG_BUF_SIZE-1, format, parms);
    va_end (parms);
    openconsole();
    writeconsole(buffer);
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
	    ReadConsole(stdinput, out, 1, &len, 0);
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

static char *writets(void)
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
    t = localtime(&tb.time);
    strftime(curts, sizeof curts, "%Y-%m-%d %H:%M:%S\n", t);
    p = out;
    *p = 0;
    if (memcmp (curts, lastts, strlen (curts))) {
	strcat (p, curts);
	p += strlen (p);
	strcpy (lastts, curts);
    }
    strftime(p, sizeof out - (p - out) , "%S-", t);
    p += strlen(p);
    sprintf(p, "%03d", tb.millitm);
    p += strlen(p);
    if (timeframes || vpos > 0 && current_hpos() > 0)
	sprintf (p, " [%d %03dx%03d]", timeframes, current_hpos(), vpos);
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

    EnterCriticalSection(&cs);
    va_start (parms, format);
    count = _vsnprintf(buffer, WRITE_LOG_BUF_SIZE-1, format, parms);
    ts = writets();
    if (SHOW_CONSOLE || console_logging) {
	if (lfdetected && ts)
	    writeconsole(ts);
	writeconsole(buffer);
    }
    if (debugfile) {
	if (lfdetected && ts)
	    fprintf(debugfile, ts);
	fprintf(debugfile, buffer);
	fflush(debugfile);
    }
    lfdetected = 0;
    if (strlen(buffer) > 0 && buffer[strlen(buffer) - 1] == '\n')
	lfdetected = 1;
    va_end (parms);
    LeaveCriticalSection(&cs);
}

void write_log (const char *format, ...)
{
    int count;
    char buffer[WRITE_LOG_BUF_SIZE], *ts;

    va_list parms;
    va_start(parms, format);
    count = _vsnprintf(buffer, WRITE_LOG_BUF_SIZE - 1, format, parms);
    ts = writets();
    if (buffer[0] == '*')
	count++;
    if (SHOW_CONSOLE || console_logging) {
	if (lfdetected && ts)
	    writeconsole(ts);
	writeconsole(buffer);
    }
    if (debugfile) {
	if (lfdetected && ts)
	    fprintf(debugfile, ts);
	fprintf(debugfile, buffer);
	fflush(debugfile);
    }
    lfdetected = 0;
    if (strlen(buffer) > 0 && buffer[strlen(buffer) - 1] == '\n')
	lfdetected = 1;
    va_end (parms);
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
    writeconsole(buffer);
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
    *bufsize -= strlen(buffer);
    return buffer + strlen(buffer);
}

void *log_open(const char *name, int append, int bootlog)
{
    FILE *f;

    f = fopen(name, append ? "a" : "wt");
    bootlogmode = bootlog;
    if (!cs_init)
	InitializeCriticalSection(&cs);
    cs_init = 1;
    return f;
}

void log_close(void *f)
{
    fclose(f);
}
