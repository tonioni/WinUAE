#include <stdio.h>
#include <tchar.h>

#include "sysconfig.h"
#include "sysdeps.h"

#include "win32.h"

static void LLError(const TCHAR *s)
{
    DWORD err = GetLastError ();

    if (err == ERROR_MOD_NOT_FOUND || err == ERROR_DLL_NOT_FOUND)
	return;
    write_log (_T("%s failed to open %d\n"), s, err);
}

void notify_user (int n)
{
}

HMODULE WIN32_LoadLibrary (const TCHAR *name)
{
    HMODULE m = NULL;
    TCHAR *newname;
    DWORD err = -1;
#ifdef CPU_64_BIT
    TCHAR *p;
#endif
    int round;

    newname = xmalloc (TCHAR, _tcslen (name) + 1 + 10);
    if (!newname)
	return NULL;
    for (round = 0; round < 4; round++) {
	TCHAR *s;
	_tcscpy (newname, name);
#ifdef CPU_64_BIT
	switch(round)
	{
	    case 0:
	    p = strstr (newname,"32");
	    if (p) {
		p[0] = '6';
		p[1] = '4';
	    }
	    break;
	    case 1:
	    p = strchr (newname,'.');
	    _tcscpy(p,"_64");
	    _tcscat(p, strchr (name,'.'));
	    break;
	    case 2:
	    p = strchr (newname,'.');
	    _tcscpy (p,"64");
	    _tcscat (p, strchr (name,'.'));
	    break;
	}
#endif
	s = xmalloc (TCHAR, _tcslen (start_path_exe) + _tcslen (WIN32_PLUGINDIR) + _tcslen (newname) + 1);
	if (s) {
	    _stprintf (s, _T("%s%s%s"), start_path_exe, WIN32_PLUGINDIR, newname);
	    m = LoadLibrary (s);
	    if (m)
		goto end;
	    _stprintf (s, _T("%s%s"), start_path_exe, newname);
	    m = LoadLibrary (s);
	    if (m)
		goto end;
	    _stprintf (s, _T("%s%s%s"), start_path_exe, WIN32_PLUGINDIR, newname);
	    LLError(s);
	    xfree (s);
	}
	m = LoadLibrary (newname);
	if (m)
	    goto end;
	LLError (newname);
#ifndef CPU_64_BIT
	break;
#endif
    }
end:
    xfree (newname);
    return m;
}
