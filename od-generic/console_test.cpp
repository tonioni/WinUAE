#include "sysconfig.h"
#include "sysdeps.h"
#include "console.h"
#include <string>
#include <stdio.h>

static std::basic_string<TCHAR> mirrored;
static bool captured;
void target_console_out(const TCHAR *text, bool buffered)
{
	mirrored = text;
	captured = buffered;
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main()
{
	TCHAR buffer[8] = _T("a");
	CHECK(setconsolemode(buffer, 8) == NULL);
	console_out(_T("bc"));
	CHECK(!_tcscmp(buffer, _T("abc")) && captured);
	console_out(_T("defg"));
	CHECK(!_tcscmp(buffer, _T("abcdefg")));
	console_out(_T("X"));
	CHECK(!_tcscmp(buffer, _T("abcdefg")) && mirrored == _T("X"));
	CHECK(setconsolemode(NULL, 0) == buffer && !console_is_buffered());
	console_out_f(_T("%s:%d:%08x"), _T("plain"), -17, 0xabcd);
	CHECK(mirrored == _T("plain:-17:0000abcd") && !captured);
	std::basic_string<TCHAR> longtext(250000, _T('x'));
	console_out_f(_T("[%s] %d %s"), longtext.c_str(), 42, _T("tail"));
	CHECK(mirrored == _T("[") + longtext + _T("] 42 tail"));
	// Rejected captures still reach the host for its own logfile policy.
	TCHAR guard[3] = { _T('a'), _T('b'), _T('c') };
	setconsolemode(guard, 2);
	console_out(_T("replacement"));
	CHECK(guard[0] == _T('a') && guard[1] == _T('b') && guard[2] == _T('c') && captured);
	setconsolemode(guard, -1);
	console_out(_T("overflow"));
	CHECK(guard[0] == _T('a') && guard[2] == _T('c'));
	setconsolemode(NULL, 0);
	console_out(NULL);
	return 0;
}
