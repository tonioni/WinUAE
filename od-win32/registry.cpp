
#include "sysconfig.h"
#include "sysdeps.h"

#include <windows.h>
#include <shlwapi.h>
#include "win32.h"
#include "registry.h"
#include "crc32.h"

static int inimode = 0;
static TCHAR *inipath;

#define ROOT_TREE _T("WinUAE")

static struct ini_line **inidata;
static int inilines;

struct ini_line
{
	int section_order;
	TCHAR *section;
	TCHAR *key;
	TCHAR *value;
};

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

static void ini_free(void)
{
	for(int c = 0; c < inilines; c++) {
		struct ini_line *il = inidata[c];
		xfree(il->section);
		xfree(il->key);
		xfree(il->value);
		xfree(il);
		inidata[c] = NULL;
	}
}

static void ini_sort(void)
{
	for(int c1 = 0; c1 < inilines; c1++) {
		struct ini_line *il1 = inidata[c1];
		if (il1 == NULL)
			continue;
		for (int c2 = c1 + 1; c2 < inilines; c2++) {
			struct ini_line *il2 = inidata[c2];
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

static void ini_addnewstring(const TCHAR *section, const TCHAR *key, const TCHAR *val)
{
	struct ini_line *il = xcalloc(struct ini_line, 1);
	il->section = my_strdup(section);
	if (!_tcsicmp(section, ROOT_TREE))
		il->section_order = 1;
	il->key = my_strdup(key);
	il->value = my_strdup(val);
	int cnt = 0;
	while (cnt < inilines && inidata[cnt])
		cnt++;
	if (cnt == inilines) {
		inilines += 10;
		inidata = xrealloc(struct ini_line*, inidata, inilines);
		int cnt2 = cnt;
		while (cnt2 < inilines) {
			inidata[cnt2++] = NULL;
		}
	}
	inidata[cnt] = il;
}

static const uae_u8 bom[3] = { 0xef, 0xbb, 0xbf };

static void ini_load(const TCHAR *path)
{
	bool utf8 = false;
	TCHAR section[MAX_DPATH];
	uae_u8 tmp[3];

	ini_free();

	FILE *f = _tfopen(path, _T("rb"));
	if (!f)
		return;
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
			ini_addnewstring(section, s2, s3);
		}
	}
	fclose(f);
	ini_sort();
}

static void ini_save(const TCHAR *path)
{
	TCHAR section[MAX_DPATH];
	TCHAR sep[2] = { '=', 0 };
	TCHAR lf[2] = {  10, 0 };
	TCHAR left[2] = { '[', 0 };
	TCHAR right[2] = { ']', 0 };

	ini_sort();
	FILE *f = _tfopen(path, _T("wt, ccs=UTF-8"));
	if (!f)
		return;
	section[0] = 0;
	for (int c = 0; c < inilines; c++) {
		TCHAR out[MAX_DPATH];
		struct ini_line *il = inidata[c];
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
		_tcscpy(out, il->key);
		_tcscat(out, sep);
		_tcscat(out, il->value);
		_tcscat(out, lf);
		fputws(out, f);
	}
	fclose(f);
}

static bool ini_getstring(const TCHAR *section, const TCHAR *key, TCHAR *out, int *max)
{
	for (int c = 0; c < inilines; c++) {
		struct ini_line *il = inidata[c];
		if (il && !_tcscmp(section, il->section) && (key == NULL || !_tcscmp(key, il->key))) {
			if (out) {
				_tcsncpy(out, il->value, *max);
				out[*max - 1] = 0;
				*max = _tcslen(out);
			}
			return true;
		}
	}
	return false;
}

static bool ini_getsectionstring(const TCHAR *section, int idx, TCHAR *keyout, int *keysize, TCHAR *valout, int *valsize)
{
	for (int c = 0; c < inilines; c++) {
		struct ini_line *il = inidata[c];
		if (il && !_tcscmp(section, il->section)) {
			if (idx == 0) {
				if (keyout) {
					_tcsncpy(keyout, il->key, *keysize);
					keyout[*keysize - 1] = 0;
					*keysize = _tcslen(keyout);
				}
				if (valout) {
					_tcsncpy(valout, il->value, *valsize);
					valout[*valsize - 1] = 0;
					*valsize = _tcslen(valout);
				}
				return true;
			}
			idx--;
		}
	}
	return false;
}

static bool ini_addstring(const TCHAR *section, const TCHAR *key, const TCHAR *val)
{
	for (int c = 0; c < inilines; c++) {
		struct ini_line *il = inidata[c];
		if (il && !_tcscmp(section, il->section)) {
			if (!_tcscmp(key, il->key)) {
				xfree(il->value);
				il->value = my_strdup(val);
				return true;
			}
		}
	}
	ini_addnewstring(section, key, val);
	return true;
}

static void ini_delete(const TCHAR *section, const TCHAR *key)
{
	for (int c = 0; c < inilines; c++) {
		struct ini_line *il = inidata[c];
		if (il && !_tcscmp(section, il->section) && (key == NULL || !_tcscmp(key, il->key))) {
			xfree(il->section);
			xfree(il->key);
			xfree(il->value);
			xfree(il);
			inidata[c] = NULL;
		}
	}
}


static HKEY gr (UAEREG *root)
{
	if (!root)
		return hWinUAEKey;
	return root->fkey;
}
static TCHAR *gs (UAEREG *root)
{
	if (!root)
		return ROOT_TREE;
	return root->inipath;
}
static TCHAR *gsn (UAEREG *root, const TCHAR *name)
{
	TCHAR *r, *s;
	if (!root)
		return my_strdup (name);
	r = gs (root);
	s = xmalloc (TCHAR, _tcslen (r) + 1 + _tcslen (name) + 1);
	_stprintf (s, _T("%s/%s"), r, name);
	return s;
}

int regsetstr (UAEREG *root, const TCHAR *name, const TCHAR *str)
{
	if (inimode) {
		int ret = ini_addstring(gs(root), name, str);
		return ret;
	} else {
		HKEY rk = gr (root);
		if (!rk)
			return 0;
		return RegSetValueEx (rk, name, 0, REG_SZ, (CONST BYTE *)str, (_tcslen (str) + 1) * sizeof (TCHAR)) == ERROR_SUCCESS;
	}
}

int regsetint (UAEREG *root, const TCHAR *name, int val)
{
	if (inimode) {
		int ret;
		TCHAR tmp[100];
		_stprintf (tmp, _T("%d"), val);
		ret = ini_addstring(gs(root), name, tmp);
		return ret;
	} else {
		DWORD v = val;
		HKEY rk = gr (root);
		if (!rk)
			return 0;
		return RegSetValueEx(rk, name, 0, REG_DWORD, (CONST BYTE*)&v, sizeof (DWORD)) == ERROR_SUCCESS;
	}
}

int regqueryint (UAEREG *root, const TCHAR *name, int *val)
{
	if (inimode) {
		int ret = 0;
		TCHAR tmp[100];
		int size = sizeof tmp / sizeof(TCHAR);
		if (ini_getstring(gs(root), name, tmp, &size)) {
			*val = _tstol (tmp);
			ret = 1;
		}
		return ret;
	} else {
		DWORD dwType = REG_DWORD;
		DWORD size = sizeof (int);
		HKEY rk = gr (root);
		if (!rk)
			return 0;
		return RegQueryValueEx (rk, name, 0, &dwType, (LPBYTE)val, &size) == ERROR_SUCCESS;
	}
}

int regsetlonglong (UAEREG *root, const TCHAR *name, ULONGLONG val)
{
	if (inimode) {
		int ret;
		TCHAR tmp[100];
		_stprintf (tmp, _T("%I64d"), val);
		ret = ini_addstring(gs(root), name, tmp);
		return ret;
	} else {
		ULONGLONG v = val;
		HKEY rk = gr (root);
		if (!rk)
			return 0;
		return RegSetValueEx(rk, name, 0, REG_QWORD, (CONST BYTE*)&v, sizeof (ULONGLONG)) == ERROR_SUCCESS;
	}
}

int regquerylonglong (UAEREG *root, const TCHAR *name, ULONGLONG *val)
{
	*val = 0;
	if (inimode) {
		int ret = 0;
		TCHAR tmp[100];
		int size = sizeof tmp / sizeof(TCHAR);
		if (ini_getstring(gs(root), name, tmp, &size)) {
			*val = _tstoi64 (tmp);
			ret = 1;
		}
		return ret;
	} else {
		DWORD dwType = REG_QWORD;
		DWORD size = sizeof (ULONGLONG);
		HKEY rk = gr (root);
		if (!rk)
			return 0;
		return RegQueryValueEx (rk, name, 0, &dwType, (LPBYTE)val, &size) == ERROR_SUCCESS;
	}
}

int regquerystr (UAEREG *root, const TCHAR *name, TCHAR *str, int *size)
{
	if (inimode) {
		int ret = 0;
		TCHAR *tmp = xmalloc (TCHAR, (*size) + 1);
		if (ini_getstring(gs(root), name, tmp, size)) {
			_tcscpy (str, tmp);
			ret = 1;
		}
		xfree (tmp);
		return ret;
	} else {
		DWORD size2 = *size * sizeof (TCHAR);
		HKEY rk = gr (root);
		if (!rk)
			return 0;
		int v = RegQueryValueEx (rk, name, 0, NULL, (LPBYTE)str, &size2) == ERROR_SUCCESS;
		*size = size2 / sizeof (TCHAR);
		return v;
	}
}

int regenumstr (UAEREG *root, int idx, TCHAR *name, int *nsize, TCHAR *str, int *size)
{
	name[0] = 0;
	str[0] = 0;
	if (inimode) {
		int ret = ini_getsectionstring(gs(root), idx, name, nsize, str, size);
		return ret;
	} else {
		DWORD nsize2 = *nsize;
		DWORD size2 = *size;
		HKEY rk = gr (root);
		if (!rk)
			return 0;
		int v = RegEnumValue (rk, idx, name, &nsize2, NULL, NULL, (LPBYTE)str, &size2) == ERROR_SUCCESS;
		*nsize = nsize2;
		*size = size2;
		return v;
	}
}

int regquerydatasize (UAEREG *root, const TCHAR *name, int *size)
{
	if (inimode) {
		int ret = 0;
		int csize = 65536;
		TCHAR *tmp = xmalloc (TCHAR, csize);
		if (regquerystr (root, name, tmp, &csize)) {
			*size = _tcslen (tmp) / 2;
			ret = 1;
		}
		xfree (tmp);
		return ret;
	} else {
		HKEY rk = gr (root);
		if (!rk)
			return 0;
		DWORD size2 = *size;
		int v = RegQueryValueEx(rk, name, 0, NULL, NULL, &size2) == ERROR_SUCCESS;
		*size = size2;
		return v;
	}
}

int regsetdata (UAEREG *root, const TCHAR *name, const void *str, int size)
{
	if (inimode) {
		uae_u8 *in = (uae_u8*)str;
		int ret;
		TCHAR *tmp = xmalloc (TCHAR, size * 2 + 1);
		for (int i = 0; i < size; i++)
			_stprintf (tmp + i * 2, _T("%02X"), in[i]); 
		ret = ini_addstring(gs(root), name, tmp);
		xfree (tmp);
		return ret;
	} else {
		HKEY rk = gr (root);
		if (!rk)
			return 0;
		return RegSetValueEx(rk, name, 0, REG_BINARY, (BYTE*)str, size) == ERROR_SUCCESS;
	}
}
int regquerydata (UAEREG *root, const TCHAR *name, void *str, int *size)
{
	if (inimode) {
		int csize = (*size) * 2 + 1;
		int i, j;
		int ret = 0;
		TCHAR *tmp = xmalloc (TCHAR, csize);
		uae_u8 *out = (uae_u8*)str;

		if (!regquerystr (root, name, tmp, &csize))
			goto err;
		j = 0;
		for (i = 0; i < _tcslen (tmp); i += 2) {
			TCHAR c1 = toupper(tmp[i + 0]);
			TCHAR c2 = toupper(tmp[i + 1]);
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
			out[j++] = c1 * 16 + c2;
		}
		ret = 1;
err:
		xfree (tmp);
		return ret;
	} else {
		HKEY rk = gr (root);
		if (!rk)
			return 0;
		DWORD size2 = *size;
		int v = RegQueryValueEx(rk, name, 0, NULL, (LPBYTE)str, &size2) == ERROR_SUCCESS;
		*size = size2;
		return v;
	}
}

int regdelete (UAEREG *root, const TCHAR *name)
{
	if (inimode) {
		ini_delete(gs(root), name);
		return 1;
	} else {
		HKEY rk = gr (root);
		if (!rk)
			return 0;
		return RegDeleteValue (rk, name) == ERROR_SUCCESS;
	}
}

int regexists (UAEREG *root, const TCHAR *name)
{
	if (inimode) {
		if (!inilines)
			return 0;
		int ret = ini_getstring(gs(root), name, NULL, NULL);
		return ret;
	} else {
		HKEY rk = gr (root);
		if (!rk)
			return 0;
		return RegQueryValueEx(rk, name, 0, NULL, NULL, NULL) == ERROR_SUCCESS;
	}
}

void regdeletetree (UAEREG *root, const TCHAR *name)
{
	if (inimode) {
		TCHAR *s = gsn (root, name);
		if (!s)
			return;
		ini_delete(s, NULL);
		xfree (s);
	} else {
		HKEY rk = gr (root);
		if (!rk)
			return;
		SHDeleteKey (rk, name);
	}
}

int regexiststree (UAEREG *root, const TCHAR *name)
{
	if (inimode) {
		int ret = 0;
		TCHAR *s = gsn (root, name);
		if (!s)
			return 0;
		ret = ini_getstring(s, NULL, NULL, 0);
		xfree (s);
		return ret;
	} else {
		int ret = 0;
		HKEY k = NULL;
		HKEY rk = gr (root);
		if (!rk)
			return 0;
		if (RegOpenKeyEx (rk , name, 0, KEY_READ, &k) == ERROR_SUCCESS)
			ret = 1;
		if (k)
			RegCloseKey (k);
		return ret;
	}
}

UAEREG *regcreatetree (UAEREG *root, const TCHAR *name)
{
	UAEREG *fkey;
	HKEY rkey;

	if (inimode) {
		TCHAR *ininame;
		if (!root) {
			if (!name)
				ininame = my_strdup (gs (NULL));
			else
				ininame = my_strdup (name);
		} else {
			ininame = xmalloc (TCHAR, _tcslen (root->inipath) + 1 + _tcslen (name) + 1);
			_stprintf (ininame, _T("%s/%s"), root->inipath, name);
		}
		fkey = xcalloc (UAEREG, 1);
		fkey->inipath = ininame;
	} else {
		DWORD err;
		HKEY rk = gr (root);
		if (!rk) {
			rk = HKEY_CURRENT_USER;
			name = _T("Software\\Arabuusimiehet\\WinUAE");
		} else if (!name) {
			name = _T("");
		}
		err = RegCreateKeyEx (rk, name, 0, NULL, REG_OPTION_NON_VOLATILE,
			KEY_READ | KEY_WRITE, NULL, &rkey, NULL);
		if (err != ERROR_SUCCESS)
			return 0;
		fkey = xcalloc (UAEREG, 1);
		fkey->fkey = rkey;
	}
	return fkey;
}

void regclosetree (UAEREG *key)
{
	if (!key)
		return;
	if (inimode)
		ini_save(inipath);
	if (key->fkey)
		RegCloseKey (key->fkey);
	xfree (key->inipath);
	xfree (key);
}

int reginitializeinit (TCHAR **pppath)
{
	UAEREG *r = NULL;
	TCHAR path[MAX_DPATH], fpath[MAX_DPATH];
	FILE *f;
	TCHAR *ppath = *pppath;

	inimode = 0;
	if (!ppath) {
		int ok = 0;
		TCHAR *posn;
		path[0] = 0;
		GetFullPathName (executable_path, sizeof path / sizeof (TCHAR), path, NULL);
		if (_tcslen (path) > 4 && !_tcsicmp (path + _tcslen (path) - 4, _T(".exe"))) {
			_tcscpy (path + _tcslen (path) - 3, _T("ini"));
			if (GetFileAttributes (path) != INVALID_FILE_ATTRIBUTES)
				ok = 1;
		}
		if (!ok) {
			path[0] = 0;
			GetFullPathName (executable_path, sizeof path / sizeof (TCHAR), path, NULL);
			if((posn = _tcsrchr (path, '\\')))
				posn[1] = 0;
			_tcscat (path, _T("winuae.ini"));
		}
		if (GetFileAttributes (path) == INVALID_FILE_ATTRIBUTES)
			return 0;
	} else {
		_tcscpy (path, ppath);
	}

	fpath[0] = 0;
	GetFullPathName (path, sizeof fpath / sizeof (TCHAR), fpath, NULL);
	if (_tcslen (fpath) < 5 || _tcsicmp (fpath + _tcslen (fpath) - 4, _T(".ini")))
		return 0;

	inimode = 1;
	inipath = my_strdup (fpath);
	ini_load(inipath);
	if (!regexists (NULL, _T("Version")))
		goto fail;
	return 1;
fail:
	regclosetree (r);
	if (GetFileAttributes (path) != INVALID_FILE_ATTRIBUTES)
		DeleteFile (path);
	if (GetFileAttributes (path) != INVALID_FILE_ATTRIBUTES)
		goto end;
	f = _tfopen (path, _T("wb"));
	if (f) {
		uae_u8 bom[3] = { 0xef, 0xbb, 0xbf };
		fwrite (bom, sizeof (bom), 1, f);
		fclose (f);
	}
	if (*pppath == NULL)
		*pppath = my_strdup (path);
	return 1;
end:
	inimode = 0;
	xfree (inipath);
	return 0;
}

void regstatus (void)
{
	if (inimode)
		write_log (_T("'%s' enabled\n"), inipath);
}

const TCHAR *getregmode (void)
{
	if (!inimode)
		return NULL;
	return inipath;
}
