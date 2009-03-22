
#include <windows.h>

#include "sysconfig.h"
#include "sysdeps.h"

char *uutf8 (const WCHAR *s)
{
    char *d;
    int len;

    if (s == NULL)
	return NULL;
    len = WideCharToMultiByte (CP_UTF8, 0, s, -1, NULL, 0, 0, FALSE);
    if (!len)
	return strdup ("");
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
    if (!len)
	return xcalloc (2, 1);
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
    len = WideCharToMultiByte (cp, 0, s, -1, NULL, 0, 0, FALSE);
    if (!len)
	return strdup ("");
    d = xmalloc (len + 1);
    WideCharToMultiByte (cp, 0, s, -1, d, len, 0, FALSE);
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
    len = MultiByteToWideChar (cp, MB_PRECOMPOSED, s, -1, NULL, 0);
    if (!len)
	return xcalloc (2, 1);
    d = xmalloc ((len + 1) * sizeof (WCHAR));
    MultiByteToWideChar (cp, MB_PRECOMPOSED, s, -1, d, len);
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
    MultiByteToWideChar (CP_ACP, MB_PRECOMPOSED, src, -1, dst, maxlen);
    return dst;
}
WCHAR *aucp_copy (TCHAR *dst, int maxlen, const char *src, UINT cp)
{
    MultiByteToWideChar (cp, MB_PRECOMPOSED, src, -1, dst, maxlen);
    return dst;
}

char *ua_copy (char *dst, int maxlen, const TCHAR *src)
{
    WideCharToMultiByte (CP_ACP, 0, src, -1, dst, maxlen, 0, FALSE);
    return dst;
}
char *uacp_copy (char *dst, int maxlen, const TCHAR *src, UINT cp)
{
    WideCharToMultiByte (cp, 0, src, -1, dst, maxlen, 0, FALSE);
    return dst;
}

TCHAR *my_strdup_ansi (const char *src)
{
    return au (src);
}
