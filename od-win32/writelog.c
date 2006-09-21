
#include "sysconfig.h"
#include "sysdeps.h"

#include <windows.h>
#include <sys/timeb.h>

#include "custom.h"
#include "events.h"

#define SHOW_CONSOLE 0

static int consoleopen = 0;
static HANDLE stdinput,stdoutput;

FILE *debugfile = NULL;
int console_logging;

#define WRITE_LOG_BUF_SIZE 4096

/* console functions for debugger */

static void openconsole(void)
{
    if(consoleopen) return;
    AllocConsole();
    stdinput = GetStdHandle(STD_INPUT_HANDLE);
    stdoutput = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleMode(stdinput,ENABLE_PROCESSED_INPUT|ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT|ENABLE_PROCESSED_OUTPUT);
    consoleopen = 1;
}

void console_out (const char *format,...)
{
    va_list parms;
    char buffer[WRITE_LOG_BUF_SIZE];
    DWORD temp, tmp;

    va_start (parms, format);
    _vsnprintf (buffer, WRITE_LOG_BUF_SIZE-1, format, parms);
    va_end (parms);
    openconsole();
    tmp = WriteConsole(stdoutput, buffer, strlen(buffer), &temp,0);
}

int console_get (char *out, int maxlen)
{
    DWORD len,totallen;

    *out = 0;
    totallen=0;
    while(maxlen>0) 
    {
	ReadConsole(stdinput,out,1,&len,0);
	if(*out == 13)
	    break;
	out++;
	maxlen--;
	totallen++;
    }
    *out=0;
    return totallen;
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
    DWORD numwritten;
    char buffer[WRITE_LOG_BUF_SIZE];
    char *ts;

    va_list parms;
    va_start (parms, format);
    count = _vsnprintf(buffer, WRITE_LOG_BUF_SIZE-1, format, parms);
    ts = writets();
    if (SHOW_CONSOLE || console_logging) {
	openconsole();
	if (lfdetected)
	    WriteConsole(stdoutput, ts, strlen(ts), &numwritten,0);
	WriteConsole(stdoutput, buffer, strlen(buffer), &numwritten,0);
    }
    if (debugfile) {
	if (lfdetected)
	    fprintf(debugfile, ts);
	fprintf(debugfile, buffer);
	fflush(debugfile);
    }
    lfdetected = 0;
    if (strlen(buffer) > 0 && buffer[strlen(buffer) - 1] == '\n')
	lfdetected = 1;
    va_end (parms);
}
void write_log (const char *format, ...)
{
    int count, tmp;
    DWORD numwritten;
    char buffer[WRITE_LOG_BUF_SIZE], *ts;

    va_list parms;
    va_start(parms, format);
    count = _vsnprintf(buffer, WRITE_LOG_BUF_SIZE-1, format, parms);
    ts = writets();
    if (SHOW_CONSOLE || console_logging) {
	openconsole();
	if (lfdetected)
	    WriteConsole(stdoutput, ts, strlen(ts), &numwritten,0);
	tmp = WriteConsole(stdoutput, buffer, strlen(buffer), &numwritten, 0);
	if (!tmp)
	    tmp = GetLastError();
    }
    if (debugfile) {
	if (lfdetected)
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
    DWORD numwritten;
    char buffer[WRITE_LOG_BUF_SIZE];
    va_list parms;
    va_start (parms, format);

    if (f == NULL)
	return;
    count = _vsnprintf (buffer, WRITE_LOG_BUF_SIZE-1, format, parms);
    openconsole ();
    WriteConsole(stdoutput, buffer, strlen(buffer), &numwritten,0);
    va_end (parms);
}
