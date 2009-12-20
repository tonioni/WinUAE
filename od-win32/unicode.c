
#include <windows.h>

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"

#define DWFLAGS (0)
#define FS_TEST 0

static WCHAR aufstable[256];

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
	len = WideCharToMultiByte (cp, 0, s, -1, NULL, 0, 0, FALSE);
	if (!len) {
		err (__FUNCTION__, s, NULL, cp);
		return strdup ("");
	}
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

TCHAR *my_strdup_ansi (const char *src)
{
	return au (src);
}

char *ua_fs (const WCHAR *s)
{
	char *d;
	int len, i;
	BOOL dc;
	char def = 0;

	if (s == NULL)
		return NULL;
	dc = FALSE;
	len = WideCharToMultiByte (28605, DWFLAGS | WC_NO_BEST_FIT_CHARS, s, -1, NULL, 0, &def, &dc);
	if (!len) {
		err (__FUNCTION__, s, NULL, currprefs.win32_fscodepage);
		return strdup ("");
	}
	d = xmalloc (len + 1);
	dc = FALSE;
	WideCharToMultiByte (28605, DWFLAGS | WC_NO_BEST_FIT_CHARS, s, -1, d, len, &def, &dc);
	if (dc) {
		for (i = 0; i < len; i++) {
			if (d[i] == 0 || (d[i] < 32 || (d[i] >= 0x7f && d[i] <= 0x9f))) {
				WCHAR s2[2];
				char d2[2];
				s2[0] = s[i];
				s2[1] = 0;
				d2[0] = (char)s[i];
				WideCharToMultiByte (0, DWFLAGS, s2, -1, d2, 1, 0, FALSE);
				d[i] = d2[0];
			}
		}
	}
	return d;
}

char *ua_fs_copy (char *dst, int maxlen, const TCHAR *src)
{
	int len, i;
	BOOL dc;
	char def = 0;

	if (src == NULL)
		return NULL;
	dc = FALSE;
	len = WideCharToMultiByte (28605, DWFLAGS | WC_NO_BEST_FIT_CHARS, src, -1, dst, maxlen, &def, &dc);
	if (dc) {
		for (i = 0; i < len; i++) {
			if (dst[i] == 0) {
				WCHAR s2[2];
				char d2[2];
				s2[0] = src[i];
				s2[1] = 0;
				d2[0] = (char)src[i];
				WideCharToMultiByte (0, DWFLAGS, s2, -1, d2, 1, 0, FALSE);
				dst[i] = d2[0];
			}
		}
	}
	return dst;
}

WCHAR *au_fs (const char *s)
{
	int i, len;
	WCHAR *d;
	
	len = strlen (s);
	d = xmalloc ((len + 1) * sizeof (WCHAR));
	for (i = 0; i < len; i++)
		d[i] = aufstable[(uae_u8)s[i]];
	d[len] = 0;
	return d;
}
WCHAR *au_fs_copy (TCHAR *dst, int maxlen, const char *src)
{
	int i;

	for (i = 0; src[i] && i < maxlen - 1; i++)
		dst[i] = aufstable[(uae_u8)src[i]];
	dst[i] = 0;
	return dst;
}

void unicode_init (void)
{
	int i;

	write_log (L"Filesystem charset conversion table:\n");
	for (i = 0; i < 256; i++) {
		TCHAR dst1[2], dst2[2];
		char src[2];

		src[0] = i;
		src[1] = 0;
		dst1[0] = 0;
		dst1[1] = 0;
		dst2[0] = 0;
		dst2[1] = 0;
		aufstable[i] = 0;
		aucp_copy (dst1, 1, src, 0);
		aucp_copy (dst2, 1, src, 28605); // iso-8859-15
		if (dst2[0] != dst1[0])
			write_log (L" %02X: %04X (%04X)", i, dst1[0], dst2[0]);
		else
			write_log (L" %02X: %04X       ", i, dst1[0]);
		if ((i & 3) == 3)
			write_log (L"\n");
		if (i < 32 || (i >= 0x7f && i <= 0x9f))
			aufstable[i] = dst1[0];
		else
			aufstable[i] = dst2[0];
		if (aufstable[i] == 0)
			aufstable[i] = (unsigned char)i;
	}		
	write_log (L"End\n");
}
