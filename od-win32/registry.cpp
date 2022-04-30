
#include "sysconfig.h"
#include "sysdeps.h"

#include <windows.h>
#include <shlwapi.h>
#include "win32.h"
#include "registry.h"
#include "ini.h"

static int inimode = 0;
static TCHAR *inipath;

#define ROOT_TREE _T("WinUAE")

static struct ini_data *inidata;

static HKEY gr (UAEREG *root)
{
	if (!root)
		return hWinUAEKey;
	return root->fkey;
}
static const TCHAR *gs (UAEREG *root)
{
	if (!root)
		return ROOT_TREE;
	return root->inipath;
}
static TCHAR *gsn (UAEREG *root, const TCHAR *name)
{
	const TCHAR *r;
	TCHAR *s;
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
		int ret = ini_addstring(inidata, gs(root), name, str);
		return ret;
	} else {
		HKEY rk = gr (root);
		if (!rk)
			return 0;
		return RegSetValueEx (rk, name, 0, REG_SZ, (CONST BYTE *)str, (uaetcslen(str) + 1) * sizeof (TCHAR)) == ERROR_SUCCESS;
	}
}

int regsetint (UAEREG *root, const TCHAR *name, int val)
{
	if (inimode) {
		int ret;
		TCHAR tmp[100];
		_stprintf (tmp, _T("%d"), val);
		ret = ini_addstring(inidata, gs(root), name, tmp);
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
		TCHAR *tmp = NULL;
		if (ini_getstring(inidata, gs(root), name, &tmp)) {
			*val = _tstol (tmp);
			ret = 1;
		}
		xfree(tmp);
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
		ret = ini_addstring(inidata, gs(root), name, tmp);
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
		TCHAR *tmp = NULL;
		if (ini_getstring(inidata, gs(root), name, &tmp)) {
			*val = _tstoi64 (tmp);
			ret = 1;
		}
		xfree(tmp);
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
		TCHAR *tmp = NULL;
		if (ini_getstring(inidata, gs(root), name, &tmp)) {
			if (_tcslen(tmp) >= *size)
				tmp[(*size) - 1] = 0;
			_tcscpy (str, tmp);
			*size = uaetcslen(str);
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
		TCHAR *name2 = NULL;
		TCHAR *str2 = NULL;
		int ret = ini_getsectionstring(inidata, gs(root), idx, &name2, &str2);
		if (ret) {
			if (_tcslen(name2) >= *nsize) {
				name2[(*nsize) - 1] = 0;
			}
			if (_tcslen(str2) >= *size) {
				str2[(*size) - 1] = 0;
			}
			_tcscpy(name, name2);
			_tcscpy(str, str2);
		}
		xfree(str2);
		xfree(name2);
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
			*size = uaetcslen (tmp) / 2;
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
		ret = ini_addstring(inidata, gs(root), name, tmp);
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
			TCHAR c1 = _totupper(tmp[i + 0]);
			TCHAR c2 = _totupper(tmp[i + 1]);
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
		ini_delete(inidata, gs(root), name);
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
		if (!inidata)
			return 0;
		int ret = ini_getstring(inidata, gs(root), name, NULL);
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
		ini_delete(inidata, s, NULL);
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
		ret = ini_getstring(inidata, s, NULL, NULL);
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
	if (inimode) {
		if (inidata->modified) {
			ini_save(inidata, inipath);
		}
	}
	if (!key)
		return;
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
	inidata = ini_load(inipath, true);
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
