
#include <windows.h>

#include "sysconfig.h"
#include "sysdeps.h"

#define DWFLAGS (WC_COMPOSITECHECK | WC_DISCARDNS)

static void err (const char *func, const WCHAR *w, const char *c, UINT cp)
{
#if 0
	FILE *f;
	uae_u8 zero[2] = { 0 };
	DWORD err = GetLastError ();

	f = fopen ("output.dat", "a");
	fwrite (func, strlen (func) + 1, 1, f);
	if (w)
		fwrite (w, wcslen (w) + 1, 2, f);
	else
		fwrite (zero, 1, 2, f);
	if (c)
		fwrite (c, strlen (c) + 1, 1, f);
	else
		fwrite (zero, 1, 1, f);
	fwrite (&err, 4, 1, f);
	fclose (f);
	write_log (L"CP=%d,ERR=%d\n", cp, err);
#endif
}

char *uutf8 (const WCHAR *s)
{
	char *d;
	int len;

	if (s == NULL)
		return NULL;
	len = WideCharToMultiByte (CP_UTF8, 0, s, -1, NULL, 0, 0, FALSE);
	if (!len) {
		err (__FUNCTION__, s, NULL, CP_UTF8);
		return strdup ("");
	}
	d = xmalloc (len + 1);
	WideCharToMultiByte (CP_UTF8, 0, s, -1, d, len, 0, FALSE);
	return d;
}
WCHAR *utf8u (const char *s)
{
	WCHAR *d;
	int len;

	if (s == NULL)
		return NULL;
	len = MultiByteToWideChar (CP_UTF8, 0, s, -1, NULL, 0);
	if (!len) {
		err (__FUNCTION__, NULL, s, CP_UTF8);
		return xcalloc (2, 1);
	}
	d = xmalloc ((len + 1) * sizeof (WCHAR));
	MultiByteToWideChar (CP_UTF8, 0, s, -1, d, len);
	return d;
}

static char *ua_2 (const WCHAR *s, UINT cp)
{
	char *d;
	int len;

	if (s == NULL)
		return NULL;
	len = WideCharToMultiByte (cp, DWFLAGS, s, -1, NULL, 0, 0, FALSE);
	if (!len) {
		err (__FUNCTION__, s, NULL, cp);
		return strdup ("");
	}
	d = xmalloc (len + 1);
	WideCharToMultiByte (cp, DWFLAGS, s, -1, d, len, 0, FALSE);
	return d;
}

char *ua (const WCHAR *s)
{
	return ua_2 (s, CP_ACP);
}
char *uacp (const WCHAR *s, UINT cp)
{
	return ua_2 (s, cp);
}

static WCHAR *au_2 (const char *s, UINT cp)
{
	WCHAR *d;
	int len;

	if (s == NULL)
		return NULL;
	len = MultiByteToWideChar (cp, 0, s, -1, NULL, 0);
	if (!len) {
		err (__FUNCTION__, NULL, s, cp);
		return xcalloc (2, 1);
	}
	d = xmalloc ((len + 1) * sizeof (WCHAR));
	MultiByteToWideChar (cp, 0, s, -1, d, len);
	return d;
}

WCHAR *au (const char *s)
{
	return au_2 (s, CP_ACP);
}
WCHAR *aucp (const char *s, UINT cp)
{
	return au_2 (s, cp);
}

WCHAR *au_copy (TCHAR *dst, int maxlen, const char *src)
{
	MultiByteToWideChar (CP_ACP, 0, src, -1, dst, maxlen);
	return dst;
}
WCHAR *aucp_copy (TCHAR *dst, int maxlen, const char *src, UINT cp)
{
	MultiByteToWideChar (cp, 0, src, -1, dst, maxlen);
	return dst;
}

char *ua_copy (char *dst, int maxlen, const TCHAR *src)
{
	WideCharToMultiByte (CP_ACP, DWFLAGS, src, -1, dst, maxlen, 0, FALSE);
	return dst;
}
char *uacp_copy (char *dst, int maxlen, const TCHAR *src, UINT cp)
{
	WideCharToMultiByte (cp, DWFLAGS, src, -1, dst, maxlen, 0, FALSE);
	return dst;
}

int charset_test (const TCHAR *s, UINT cp)
{
	static char s1[MAX_DPATH];
	static WCHAR s2[MAX_DPATH];
	s1[0] = 0;
	s2[0] = 0;
	uacp_copy (s1, MAX_DPATH, s, cp);
	aucp_copy (s2, MAX_DPATH, s1, cp);
	return !_tcscmp (s2, s);
}

TCHAR *my_strdup_ansi (const char *src)
{
	return au (src);
}
