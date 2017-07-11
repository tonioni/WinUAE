
#include "sysconfig.h"
#include "sysdeps.h"

#include "ini.h"

static TCHAR *initrim(TCHAR *s)
{
	while (*s != 0 && *s <= 32)
		s++;
	TCHAR *s2 = s;
	while (*s2)
		s2++;
	while (s2 > s) {
		s2--;
		if (*s2 > 32)
			break;
		*s2 = 0;
	}
	return s;
}

void ini_free(struct ini_data *ini)
{
	for(int c = 0; c < ini->inilines; c++) {
		struct ini_line *il = ini->inidata[c];
		if (il) {
			xfree(il->section);
			xfree(il->key);
			xfree(il->value);
			xfree(il);
		}
		ini->inidata[c] = NULL;
	}
	xfree(ini);
}

static void ini_sort(struct ini_data *ini)
{
	for(int c1 = 0; c1 < ini->inilines; c1++) {
		struct ini_line *il1 = ini->inidata[c1];
		if (il1 == NULL)
			continue;
		for (int c2 = c1 + 1; c2 < ini->inilines; c2++) {
			struct ini_line *il2 = ini->inidata[c2];
			if (il2 == NULL)
				continue;
			int order = 0;
			int sec = _tcsicmp(il1->section, il2->section);
			if (sec) {
				if (!il1->section_order && !il2->section_order)
					order = sec;
				else
					order = il2->section_order - il1->section_order;
			} else {
				order = _tcsicmp(il1->key, il2->key);
			}
			if (order > 0) {
				struct ini_line il;
				memcpy(&il, il1, sizeof(struct ini_line));
				memcpy(il1, il2, sizeof(struct ini_line));
				memcpy(il2, &il, sizeof(struct ini_line));
			}
		}
	}
#if 0
	for(int c1 = 0; c1 < inilines; c1++) {
		struct ini_line *il1 = inidata[c1];
		if (il1)
			write_log(_T("[%s] %s %s\n"), il1->section, il1->key, il1->value);
	}
	write_log(_T("\n"));
#endif
}

void ini_addnewcomment(struct ini_data *ini, const TCHAR *section, const TCHAR *val)
{
	ini_addnewstring(ini, section, _T(""), val);
}

void ini_addnewstring(struct ini_data *ini, const TCHAR *section, const TCHAR *key, const TCHAR *val)
{
	struct ini_line *il = xcalloc(struct ini_line, 1);
	il->section = my_strdup(section);
	if (!_tcsicmp(section, _T("WinUAE")))
		il->section_order = 1;
	il->key = my_strdup(key);
	il->value = my_strdup(val);
	int cnt = 0;
	while (cnt < ini->inilines && ini->inidata[cnt])
		cnt++;
	if (cnt == ini->inilines) {
		ini->inilines += 10;
		ini->inidata = xrealloc(struct ini_line*, ini->inidata, ini->inilines);
		int cnt2 = cnt;
		while (cnt2 < ini->inilines) {
			ini->inidata[cnt2++] = NULL;
		}
	}
	ini->inidata[cnt] = il;
}

void ini_addnewdata(struct ini_data *ini, const TCHAR *section, const TCHAR *key, const uae_u8 *data, int len)
{
	TCHAR *s = xcalloc(TCHAR, len * 3);
	_tcscpy(s, _T("\\\n"));
	int w = 32;
	for (int i = 0; i < len; i += w) {
		if (i > 0)
			_tcscat(s, _T(" \\\n"));
		TCHAR *p = s + _tcslen(s);
		for (int j = 0; j < w && j + i < len; j++) {
			_stprintf (p, _T("%02X"), data[i + j]);
			p += 2;
		}
		*p = 0;
	}
	ini_addnewstring(ini, section, key, s);
	xfree(s);
}

static const uae_u8 bom[3] = { 0xef, 0xbb, 0xbf };

struct ini_data *ini_new(void)
{
	struct ini_data *iniout = xcalloc(ini_data, 1);
	return iniout;
}

struct ini_data *ini_load(const TCHAR *path)
{
	bool utf8 = false;
	TCHAR section[MAX_DPATH];
	uae_u8 tmp[3];
	struct ini_data ini = { 0 };

	FILE *f = _tfopen(path, _T("rb"));
	if (!f)
		return NULL;
	int v = fread(tmp, 1, sizeof tmp, f);
	fclose (f);
	if (v == 3 && tmp[0] == 0xef && tmp[1] == 0xbb && tmp[2] == 0xbf) {
		f = _tfopen (path, _T("rt, ccs=UTF-8"));
	} else {
		f = _tfopen (path, _T("rt"));
	}
	section[0] = 0;
	for (;;) {
		TCHAR tbuffer[MAX_DPATH];
		tbuffer[0] = 0;
		if (!fgetws(tbuffer, MAX_DPATH, f))
			break;
		TCHAR *s = initrim(tbuffer);
		if (_tcslen(s) < 3)
			continue;
		if (s[0] == ';')
			continue;
		if (s[0] == '[' && s[_tcslen(s) - 1] == ']') {
			s[_tcslen(s) - 1] = 0;
			_tcscpy(section, s + 1);
			continue;
		}
		if (section[0] == 0)
			continue;
		TCHAR *s1 = _tcschr(s, '=');
		if (s1) {
			*s1++ = 0;
			TCHAR *s2 = initrim(tbuffer);
			TCHAR *s3 = initrim(s1);
			if (s3[0] == '\\' && s3[1] == 0) {
				// multiline
				xfree(s3);
				s3 = NULL;
				int len = MAX_DPATH;
				TCHAR *otxt = xcalloc(TCHAR, len);
				for (;;) {
					tbuffer[0] = 0;
					if (!fgetws(tbuffer, MAX_DPATH, f))
						break;
					s3 = initrim(tbuffer);
					if (s3[0] == 0)
						break;
					if (_tcslen(otxt) + _tcslen(s3) + 1 >= len) {
						len += MAX_DPATH;
						otxt = xrealloc(TCHAR, otxt, len);
					}
					_tcscat(otxt, s3);
					xfree(s3);
					s3 = NULL;
				}
				xfree(s3);
				ini_addnewstring(&ini, section, s2, otxt);
			} else {
				ini_addnewstring(&ini, section, s2, s3);
			}
		}
	}
	fclose(f);
	ini_sort(&ini);
	struct ini_data *iniout = xcalloc(ini_data, 1);
	memcpy(iniout, &ini, sizeof(struct ini_data));
	return iniout;
}

bool ini_save(struct ini_data *ini, const TCHAR *path)
{
	TCHAR section[MAX_DPATH];
	TCHAR sep[2] = { '=', 0 };
	TCHAR com[3] = { ';', ' ', 0 };
	TCHAR lf[2] = {  10, 0 };
	TCHAR left[2] = { '[', 0 };
	TCHAR right[2] = { ']', 0 };

	if (!ini)
		return false;
	ini_sort(ini);
	FILE *f = _tfopen(path, _T("wt, ccs=UTF-8"));
	if (!f)
		return false;
	section[0] = 0;
	for (int c = 0; c < ini->inilines; c++) {
		TCHAR out[MAX_DPATH];
		struct ini_line *il = ini->inidata[c];
		if (!il)
			continue;
		if (_tcscmp(il->section, section)) {
			_tcscpy(out, lf);
			_tcscat(out, left);
			_tcscat(out, il->section);
			_tcscat(out, right);
			_tcscat(out, lf);
			fputws(out, f);
			_tcscpy(section, il->section);
		}
		if (il->key[0] == 0) {
			fputws(com, f);
		} else {
			fputws(il->key, f);
			fputws(sep, f);
		}
		fputws(il->value, f);
		fputws(lf, f);
	}
	fclose(f);
	return true;
}

bool ini_getstring(struct ini_data *ini, const TCHAR *section, const TCHAR *key, TCHAR **out)
{
	for (int c = 0; c < ini->inilines; c++) {
		struct ini_line *il = ini->inidata[c];
		if (il && !_tcscmp(section, il->section) && (key == NULL || !_tcscmp(key, il->key))) {
			if (out) {
				*out = my_strdup(il->value);
			}
			return true;
		}
	}
	return false;
}

bool ini_getdata(struct ini_data *ini, const TCHAR *section, const TCHAR *key, uae_u8 **out, int *size)
{
	TCHAR *out2 = NULL;
	uae_u8 *outp = NULL;
	int len;
	bool quoted = false;

	if (!ini_getstring(ini, section, key, &out2))
		return false;

	len = _tcslen(out2);
	outp = xcalloc(uae_u8, len);

	int j = 0;
	for (int i = 0; i < len; ) {
		TCHAR c1 = _totupper(out2[i + 0]);
		if (c1 == '\"') {
			quoted = !quoted;
			i++;
		} else {
			if (quoted) {
				outp[j++] = (uae_u8)c1;
				i++;
			} else {
				if (c1 == ' ') {
					i++;
					continue;
				}
				if (i + 1 >= len)
					goto err;
				TCHAR c2 = _totupper(out2[i + 1]);
				if (c1 >= 'A')
					c1 -= 'A' - 10;
				else if (c1 >= '0')
					c1 -= '0';
				if (c1 > 15)
					goto err;
				if (c2 >= 'A')
					c2 -= 'A' - 10;
				else if (c2 >= '0')
					c2 -= '0';
				if (c2 > 15)
					goto err;
				outp[j++] = c1 * 16 + c2;
				i += 2;
			}
		}
	}
	if (quoted)
		goto err;
	*out = outp;
	*size = j;
	return true;
err:
	xfree(out2);
	xfree(outp);
	return false;
}

bool ini_getsectionstring(struct ini_data *ini, const TCHAR *section, int idx, TCHAR **keyout, TCHAR **valout)
{
	for (int c = 0; c < ini->inilines; c++) {
		struct ini_line *il = ini->inidata[c];
		if (il && !_tcscmp(section, il->section)) {
			if (idx == 0) {
				if (keyout) {
					*keyout = my_strdup(il->key);
				}
				if (valout) {
					*valout = my_strdup(il->value);
				}
				return true;
			}
			idx--;
		}
	}
	return false;
}

bool ini_addstring(struct ini_data *ini, const TCHAR *section, const TCHAR *key, const TCHAR *val)
{
	for (int c = 0; c < ini->inilines; c++) {
		struct ini_line *il = ini->inidata[c];
		if (il && !_tcscmp(section, il->section)) {
			if (!_tcscmp(key, il->key)) {
				xfree(il->value);
				il->value = my_strdup(val);
				return true;
			}
		}
	}
	ini_addnewstring(ini, section, key, val);
	return true;
}

bool ini_delete(struct ini_data *ini, const TCHAR *section, const TCHAR *key)
{
	for (int c = 0; c < ini->inilines; c++) {
		struct ini_line *il = ini->inidata[c];
		if (il && !_tcscmp(section, il->section) && (key == NULL || !_tcscmp(key, il->key))) {
			xfree(il->section);
			xfree(il->key);
			xfree(il->value);
			xfree(il);
			ini->inidata[c] = NULL;
			return true;
		}
	}
	return false;
}

