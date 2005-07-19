
#include "sysconfig.h"
#include "sysdeps.h"

#include <windows.h>

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
    stdinput=GetStdHandle(STD_INPUT_HANDLE);
    stdoutput=GetStdHandle(STD_OUTPUT_HANDLE);
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

void write_dlog (const char *format, ...)
{
    int count;
    DWORD numwritten;
    char buffer[WRITE_LOG_BUF_SIZE];

    va_list parms;
    va_start (parms, format);
    count = _vsnprintf( buffer, WRITE_LOG_BUF_SIZE-1, format, parms );
    if (SHOW_CONSOLE || console_logging) {
	openconsole();
	WriteConsole(stdoutput,buffer,strlen(buffer),&numwritten,0);
    }
    if (debugfile) {
	fprintf( debugfile, buffer );
	fflush (debugfile);
    }
    va_end (parms);
}
void write_log (const char *format, ...)
{
    int count, tmp;
    DWORD numwritten;
    char buffer[WRITE_LOG_BUF_SIZE];

    va_list parms;
    va_start (parms, format);
    count = _vsnprintf( buffer, WRITE_LOG_BUF_SIZE-1, format, parms );
    if (SHOW_CONSOLE || console_logging) {
	openconsole();
	tmp = WriteConsole(stdoutput,buffer,strlen(buffer),&numwritten,0);
	if (!tmp)
	    tmp = GetLastError();
    }
    if (debugfile) {
	fprintf (debugfile, buffer);
	fflush (debugfile);
    }
    va_end (parms);
}

void f_out (void *f, const char *format, ...)
{
    int count;
    DWORD numwritten;
    char buffer[ WRITE_LOG_BUF_SIZE ];

    va_list parms;
    va_start (parms, format);
    count = _vsnprintf( buffer, WRITE_LOG_BUF_SIZE-1, format, parms );
    if (f == 0) {
	write_log (buffer);
    } else {
	openconsole();
	WriteConsole(stdoutput,buffer,strlen(buffer),&numwritten,0);
	va_end (parms);
    }
}
