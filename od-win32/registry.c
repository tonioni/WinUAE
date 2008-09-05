
#include "sysconfig.h"
#include "sysdeps.h"

#include <windows.h>
#include "win32.h"
#include <shlwapi.h>
#include "registry.h"
#include "crc32.h"

static int inimode = 0;
static char *inipath;
#define PUPPA "eitätäoo"

static HKEY gr (UAEREG *root)
{
    if (!root)
	return hWinUAEKey;
    return root->fkey;
}
static char *gs (UAEREG *root)
{
    if (!root)
	return "WinUAE";
    return root->inipath;
}
static char *gsn (UAEREG *root, const char *name)
{
    char *r, *s;
    if (!root)
	return my_strdup (name);
    r = gs (root);
    s = xmalloc (strlen (r) + 1 + strlen (name) + 1);
    sprintf (s, "%s/%s", r, name);
    return s;
}

int regsetstr (UAEREG *root, const char *name, const char *str)
{
    if (inimode) {
	DWORD ret;
	ret = WritePrivateProfileString (gs (root), name, str, inipath);
	return ret;
    } else {
	HKEY rk = gr (root);
	if (!rk)
	    return 0;
	return RegSetValueEx (rk, name, 0, REG_SZ, (CONST BYTE *)str, strlen (str) + 1) == ERROR_SUCCESS;
    }
}

int regsetint (UAEREG *root, const char *name, int val)
{
    if (inimode) {
	DWORD ret;
	char tmp[100];
	sprintf (tmp, "%d", val);
	ret = WritePrivateProfileString (gs (root), name, tmp, inipath);
	return ret;
    } else {
	DWORD v = val;
	HKEY rk = gr (root);
	if (!rk)
	    return 0;
	return RegSetValueEx(rk, name, 0, REG_DWORD, (CONST BYTE*)&v, sizeof (DWORD)) == ERROR_SUCCESS;
    }
}

int regqueryint (UAEREG *root, const char *name, int *val)
{
    if (inimode) {
	int ret = 0;
	char tmp[100];
	GetPrivateProfileString (gs (root), name, PUPPA, tmp, sizeof (tmp), inipath);
	if (strcmp (tmp, PUPPA)) {
	    *val = atol (tmp);
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

int regquerystr (UAEREG *root, const char *name, char *str, int *size)
{
    if (inimode) {
	int ret = 0;
	char *tmp = xmalloc ((*size) + 1);
	GetPrivateProfileString (gs (root), name, PUPPA, tmp, *size, inipath);
	if (strcmp (tmp, PUPPA)) {
	    strcpy (str, tmp);
	    ret = 1;
	}
	xfree (tmp);
	return ret;
    } else {
	HKEY rk = gr (root);
	if (!rk)
	    return 0;
	return RegQueryValueEx (rk, name, 0, NULL, str, size) == ERROR_SUCCESS;
    }
}

int regenumstr (UAEREG *root, int idx, char *name, int *nsize, char *str, int *size)
{
    if (inimode) {
	int ret = 0;
	int tmpsize = 65536;
	char *tmp = xmalloc (tmpsize);
	if (GetPrivateProfileSection (gs (root), tmp, tmpsize, inipath) > 0) {
	    int i;
	    char *p = tmp, *p2;
	    for (i = 0; i < idx; i++) {
		if (p[0] == 0)
		    break;
		p += strlen (p) + 1;
	    }
	    if (p[0]) {
		p2 = strchr (p, '=');
		*p2++ = 0;
		strcpy_s (name, *nsize, p);
		strcpy_s (str, *size, p2);
		ret = 1;
	    }
	}
	xfree (tmp);
	return ret;
    } else {
	HKEY rk = gr (root);
	if (!rk)
	    return 0;
	return RegEnumValue (rk, idx, name, nsize, NULL, NULL, str, size) == ERROR_SUCCESS;
    }
}

int regquerydatasize (UAEREG *root, const char *name, int *size)
{
    if (inimode) {
	int ret = 0;
	int csize = 65536;
	char *tmp = xmalloc (csize);
	if (regquerystr (root, name, tmp, &csize)) {
	    *size = strlen (tmp) / 2;
	    ret = 1;
	}
	xfree (tmp);
        return ret;
    } else {
	HKEY rk = gr (root);
	if (!rk)
	    return 0;
	return RegQueryValueEx(rk, name, 0, NULL, NULL, size) == ERROR_SUCCESS;
    }
}

int regsetdata (UAEREG *root, const char *name, void *str, int size)
{
    if (inimode) {
	uae_u8 *in = str;
	DWORD ret;
	int i;
	char *tmp = xmalloc (size * 2 + 1);
	for (i = 0; i < size; i++)
	    sprintf (tmp + i * 2, "%02X", in[i]); 
	ret = WritePrivateProfileString (gs (root), name, tmp, inipath);
	xfree (tmp);
	return ret;
    } else {
	HKEY rk = gr (root);
	if (!rk)
	    return 0;
	return RegSetValueEx(rk, name, 0, REG_BINARY, (BYTE*)str, size) == ERROR_SUCCESS;
    }
}
int regquerydata (UAEREG *root, const char *name, void *str, int *size)
{
    if (inimode) {
	int csize = (*size) * 2 + 1;
	int i, j;
	int ret = 0;
	char *tmp = xmalloc (csize);
	uae_u8 *out = str;

	if (!regquerystr (root, name, tmp, &csize))
	    goto err;
	j = 0;
	for (i = 0; i < strlen (tmp); i += 2) {
	    char c1 = toupper(tmp[i + 0]);
	    char c2 = toupper(tmp[i + 1]);
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
	return RegQueryValueEx(rk, name, 0, NULL, str, size) == ERROR_SUCCESS;
    }
}

int regdelete (UAEREG *root, const char *name)
{
    if (inimode) {
	WritePrivateProfileString (gs (root), name, NULL, inipath);
	return 1;
    } else {
	HKEY rk = gr (root);
	if (!rk)
	    return 0;
	return RegDeleteValue (rk, name) == ERROR_SUCCESS;
    }
}

int regexists (UAEREG *root, const char *name)
{
    if (inimode) {
	int ret = 1;
	char *tmp = xmalloc (strlen (PUPPA) + 1);
	int size = strlen (PUPPA) + 1;
	GetPrivateProfileString (gs (root), name, PUPPA, tmp, size, inipath);
	if (!strcmp (tmp, PUPPA))
	    ret = 0;
	xfree (tmp);
	return ret;
    } else {
	HKEY rk = gr (root);
	if (!rk)
	    return 0;
	return RegQueryValueEx(rk, name, 0, NULL, NULL, NULL) == ERROR_SUCCESS;
    }
}

void regdeletetree (UAEREG *root, const char *name)
{
    if (inimode) {
	char *s = gsn (root, name);
	if (!s)
	    return;
	WritePrivateProfileSection (s, "", inipath);
	xfree (s);
    } else {
	HKEY rk = gr (root);
	if (!rk)
	    return;
	SHDeleteKey (rk, name);
    }
}

int regexiststree (UAEREG *root, const char *name)
{
    if (inimode) {
	int ret = 0;
	int tmpsize = 65536;
	char *p, *tmp;
	char *s = gsn (root, name);
	if (!s)
	    return 0;
	tmp = xmalloc (tmpsize);
	tmp[0] = 0;
	GetPrivateProfileSectionNames (tmp, tmpsize, inipath);
	p = tmp;
	while (p[0]) {
	    if (!strcmp (p, name)) {
		ret = 1;
		break;
	    }
	    p += strlen (p) + 1;
	}
	xfree (tmp);
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


UAEREG *regcreatetree (UAEREG *root, const char *name)
{
    UAEREG *fkey;
    HKEY rkey;

    if (inimode) {
	char *ininame;
	if (!root) {
	    if (!name)
		ininame = my_strdup (gs (NULL));
	    else
		ininame = my_strdup (name);
	} else {
	    ininame = xmalloc (strlen (root->inipath) + 1 + strlen (name) + 1);
	    sprintf (ininame, "%s/%s", root->inipath, name);
	}
	fkey = xcalloc (sizeof (UAEREG), 1);
	fkey->inipath = ininame;
    } else {
	HKEY rk = gr (root);
	if (!rk) {
	    rk = HKEY_CURRENT_USER;
	    name = "Software\\Arabuusimiehet\\WinUAE";
	}
	if (RegCreateKeyEx(rk, name, 0, NULL, REG_OPTION_NON_VOLATILE,
	    KEY_READ | KEY_WRITE, NULL, &rkey, NULL) != ERROR_SUCCESS)
	    return 0;
	fkey = xcalloc (sizeof (UAEREG), 1);
	fkey->fkey = rkey;
    }
    return fkey;
}

void regclosetree (UAEREG *key)
{
    if (!key)
	return;
    if (key->fkey)
	RegCloseKey (key->fkey);
    xfree (key->inipath);
    xfree (key);
}

static uae_u8 crcok[20] = { 0xD3,0x34,0xDE,0x75,0x31,0x2B,0x44,0x51,0xA2,0xB8,0x8D,0xC3,0x52,0xFB,0x65,0x8F,0x95,0xCB,0x0C,0xF2 };

int reginitializeinit (const char *ppath)
{
    UAEREG *r = NULL;
    char tmp1[1000];
    uae_u8 crc[20];
    int s, v1, v2, v3;
    char path[MAX_DPATH], fpath[MAX_PATH];

    if (!ppath) {
	int ok = 0;
	char *posn;
	strcpy (path, _pgmptr);
	if (strlen (path) > 4 && !stricmp (path + strlen (path) - 4, ".exe")) {
	    strcpy (path + strlen (path) - 3, "ini");
	    if (GetFileAttributes (path) != INVALID_FILE_ATTRIBUTES)
		ok = 1;
	}
	if (!ok) {
	    strcpy (path, _pgmptr);
	    if((posn = strrchr (path, '\\')))
		posn[1] = 0;
	    strcat (path, "winuae.ini");
	}
	if (GetFileAttributes (path) == INVALID_FILE_ATTRIBUTES)
	    return 0;
    } else {
	strcpy (path, ppath);
    }

    fpath[0] = 0;
    GetFullPathName (path, sizeof fpath, fpath, NULL);
    if (strlen (fpath) < 5 || stricmp (fpath + strlen (fpath) - 4, ".ini"))
	return 0;

    inimode = 1;
    inipath = my_strdup (fpath);
    if (!regexists (NULL, "Version"))
	goto fail;
    r = regcreatetree (NULL, "Warning");
    if (!r)
	goto fail;
    memset (tmp1, 0, sizeof tmp1);
    s = 200;
    if (!regquerystr (r, "info1", tmp1, &s))
	goto fail;
    if (!regquerystr (r, "info2", tmp1 + 200, &s))
	goto fail;
    get_sha1 (tmp1, sizeof tmp1, crc);
    if (memcmp (crc, crcok, sizeof crcok))
	goto fail;
    v1 = v2 = -1;
    regsetint (r, "check", 1);
    regqueryint (r, "check", &v1);
    regsetint (r, "check", 3);
    regqueryint (r, "check", &v2);
    regdelete (r, "check");
    if (regqueryint (r, "check", &v3))
	goto fail;
    if (v1 != 1 || v2 != 3)
	goto fail;
    regclosetree (r);
    return 1;
fail:
    regclosetree (r);
    if (GetFileAttributes (path) != INVALID_FILE_ATTRIBUTES)
	DeleteFile (path);
    if (GetFileAttributes (path) != INVALID_FILE_ATTRIBUTES)
	goto end;
    r = regcreatetree (NULL, "Warning");
    if (!r)
	goto end;
    regsetstr (r, "info1", "This is unsupported file. Compatibility between versions is not guaranteed.");
    regsetstr (r, "info2", "Incompatible ini-files may be re-created from scratch!");
    regclosetree (r);
    return 1;
end:
    inimode = 0;
    xfree (inipath);
    return 0;
}

void regstatus (void)
{
    if (inimode)
	write_log ("WARNING: Unsupported '%s' enabled\n", inipath);
}

int getregmode (void)
{
    return inimode;
}