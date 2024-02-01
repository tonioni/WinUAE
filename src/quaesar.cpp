#include <stdio.h>
#include <stdarg.h>

#include "sysconfig.h"
#include "sysdeps.h"
#include "uae/time.h"

extern void real_main(int argc, TCHAR** argv);
extern void keyboard_settrans();

// dummy main
int main(int argc, TCHAR** argv) {
    syncbase = 1000000;

    keyboard_settrans();
    real_main(argc, argv);
	return 0;
}

// dummy write_log
void write_log (const char *format, ...)
{
	va_list parms;

	va_start (parms, format);
	vprintf (format, parms);
	va_end (parms);
}

// dummy write_log
void write_dlog(const char* format, ...) {
	va_list parms;

	va_start (parms, format);
	vprintf (format, parms);
	va_end (parms);
}

void console_out_f (const TCHAR * format, ...)
{
	va_list parms;

	va_start (parms, format);
	vprintf (format, parms);
	va_end (parms);
}

void console_out (const TCHAR *txt)
{
	console_out_f("%s", txt);
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

static TCHAR *console_buffer;
static int console_buffer_size;

TCHAR* setconsolemode(TCHAR* buffer, int maxlen) {
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

// dummy win support for blkdev.cpp
int GetDriveType(TCHAR* vol) { return 0;}
