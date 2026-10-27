
#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "events.h"
#include "custom.h"
#include "uae/log.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <sys/timeb.h>

void UAECALL uae_log(const char *format, ...)
{
	/* Redirect UAE_LOG_VA_ARGS_FULL to use write_log instead */
#define uae_log write_log
	UAE_LOG_VA_ARGS_FULL(format);
#undef uae_log
}

int log_nodatestamps;

TCHAR *write_log_get_ts(void)
{
	struct tm *t;
	struct _timeb tb;
	static TCHAR out[100];
	TCHAR *p;
	static TCHAR lastts[100];
	TCHAR curts[100];

	if (log_nodatestamps) {
		return NULL;
	}
	if (!vsync_counter) {
		return NULL;
	}
	_ftime(&tb);
	t = localtime(&tb.time);
	_tcsftime(curts, sizeof curts / sizeof(TCHAR), _T("%Y-%m-%d %H:%M:%S\n"), t);
	p = out;
	*p = 0;
	if (_tcsncmp(curts, lastts, _tcslen(curts) - 3)) { // "xx\n"
		_tcscat(p, curts);
		p += _tcslen(p);
		_tcscpy(lastts, curts);
	}
	_tcsftime(p, sizeof out / sizeof(TCHAR) - (p - out), _T("%S-"), t);
	p += _tcslen(p);
	_stprintf(p, _T("%03d"), tb.millitm);
	p += _tcslen(p);
	if (vsync_counter != 0xffffffff) {
		_stprintf(p, _T(" [%d %03d%s%03d/%03d]"), vsync_counter, current_hpos_safe(), lof_store ? _T("-") : _T("="), vpos, linear_vpos_start);
	}
	_tcscat(p, _T(": "));
	return out;
}
