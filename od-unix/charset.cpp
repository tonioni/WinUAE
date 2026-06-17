#include "sysconfig.h"
#include "sysdeps.h"

#include "uae/string.h"

static TCHAR *dup_tchar(const TCHAR *s)
{
    if (!s) {
        s = _T("");
    }
    size_t len = _tcslen(s) + 1;
    TCHAR *out = xmalloc(TCHAR, len);
    memcpy(out, s, len * sizeof(TCHAR));
    return out;
}

TCHAR *my_strdup_ansi(const char *s)
{
    return dup_tchar(s ? s : "");
}

TCHAR *au(const char *s) { return dup_tchar(s); }
char *ua(const TCHAR *s) { return dup_tchar(s); }
TCHAR *aucp(const char *s, unsigned int) { return au(s); }
char *uacp(const TCHAR *s, unsigned int) { return ua(s); }
TCHAR *au_fs(const char *s) { return au(s); }
char *ua_fs(const TCHAR *s, int) { return ua(s); }
char *uutf8(const TCHAR *s) { return ua(s); }
TCHAR *utf8u(const char *s) { return au(s); }

char *ua_copy(char *dst, int maxlen, const TCHAR *src)
{
    uae_tcslcpy(dst, src ? src : "", maxlen);
    return dst;
}

TCHAR *au_copy(TCHAR *dst, int maxlen, const char *src)
{
    uae_tcslcpy(dst, src ? src : "", maxlen);
    return dst;
}

char *ua_fs_copy(char *dst, int maxlen, const TCHAR *src, int)
{
    return ua_copy(dst, maxlen, src);
}

TCHAR *au_fs_copy(TCHAR *dst, int maxlen, const char *src)
{
    return au_copy(dst, maxlen, src);
}

void unicode_init(void)
{
}

void to_lower(TCHAR *s, int len)
{
    for (int i = 0; s && s[i] && i < len; i++) {
        s[i] = (TCHAR)_totlower((unsigned char)s[i]);
    }
}

void to_upper(TCHAR *s, int len)
{
    for (int i = 0; s && s[i] && i < len; i++) {
        s[i] = (TCHAR)_totupper((unsigned char)s[i]);
    }
}

int uaestrlen(const char *s)
{
    return s ? (int)strlen(s) : 0;
}

int uaetcslen(const TCHAR *s)
{
    return s ? (int)_tcslen(s) : 0;
}
