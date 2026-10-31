
#include "sysconfig.h"
#include "sysdeps.h"

#include <windows.h>
#include <shlwapi.h>
#include "win32.h"
#include "registry.h"
#include "ini.h"
#include "od-generic/registry_ini.h"

static int inimode = 0;
static TCHAR *inipath;

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
		return NULL;
	return root->inipath;
}

int regsetstr (UAEREG *root, const TCHAR *name, const TCHAR *str)
{
	if (inimode) {
		return regini_setstr(inidata, gs(root), name, str);
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
		return regini_setint(inidata, gs(root), name, val);
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
		return regini_queryint(inidata, gs(root), name, val);
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
		return regini_setlonglong(inidata, gs(root), name, val);
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
		return regini_querylonglong(inidata, gs(root), name, val);
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
		return regini_querystr(inidata, gs(root), name, str, size);
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
		return regini_enumstr(inidata, gs(root), idx, name, nsize, str, size);
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
		return regini_querydatasize(inidata, gs(root), name, size);
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
		return regini_setdata(inidata, gs(root), name, str, size);
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
		return regini_querydata(inidata, gs(root), name, str, size);
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
		return regini_delete(inidata, gs(root), name);
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
		return regini_exists(inidata, gs(root), name);
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
		regini_deletetree(inidata, gs(root), name);
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
		return regini_existstree(inidata, gs(root), name);
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
		fkey = xcalloc (UAEREG, 1);
		fkey->inipath = regini_treepath(gs(root), name);
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
