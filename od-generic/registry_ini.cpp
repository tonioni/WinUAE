#include "sysconfig.h"
#include "sysdeps.h"

#include "ini.h"
#include "od-generic/registry_ini.h"

#define ROOT_TREE _T("WinUAE")

static const TCHAR *gs(const TCHAR *root)
{
	if (!root) {
		return ROOT_TREE;
	}
	return root;
}

static TCHAR *gsn(const TCHAR *root, const TCHAR *name)
{
	TCHAR *s;
	if (!root) {
		return my_strdup(name);
	}
	s = xmalloc(TCHAR, _tcslen(root) + 1 + _tcslen(name) + 1);
	_stprintf(s, _T("%s/%s"), root, name);
	return s;
}

int regini_setstr(struct ini_data *ini, const TCHAR *root, const TCHAR *name, const TCHAR *str)
{
	return ini_addstring(ini, gs(root), name, str);
}

int regini_setint(struct ini_data *ini, const TCHAR *root, const TCHAR *name, int val)
{
	TCHAR tmp[100];
	_stprintf(tmp, _T("%d"), val);
	return ini_addstring(ini, gs(root), name, tmp);
}

int regini_queryint(struct ini_data *ini, const TCHAR *root, const TCHAR *name, int *val)
{
	int ret = 0;
	TCHAR *tmp = NULL;
	if (ini_getstring(ini, gs(root), name, &tmp)) {
		*val = _tstol(tmp);
		ret = 1;
	}
	xfree(tmp);
	return ret;
}

int regini_setlonglong(struct ini_data *ini, const TCHAR *root, const TCHAR *name, unsigned long long val)
{
	TCHAR tmp[100];
	_stprintf(tmp, _T("%lld"), (long long)val);
	return ini_addstring(ini, gs(root), name, tmp);
}

int regini_querylonglong(struct ini_data *ini, const TCHAR *root, const TCHAR *name, unsigned long long *val)
{
	int ret = 0;
	TCHAR *tmp = NULL;
	*val = 0;
	if (ini_getstring(ini, gs(root), name, &tmp)) {
		*val = _tstoi64(tmp);
		ret = 1;
	}
	xfree(tmp);
	return ret;
}

int regini_querystr(struct ini_data *ini, const TCHAR *root, const TCHAR *name, TCHAR *str, int *size)
{
	int ret = 0;
	TCHAR *tmp = NULL;
	if (ini_getstring(ini, gs(root), name, &tmp)) {
		if (uaetcslen(tmp) >= *size) {
			tmp[(*size) - 1] = 0;
		}
		_tcscpy(str, tmp);
		*size = uaetcslen(str);
		ret = 1;
	}
	xfree(tmp);
	return ret;
}

int regini_enumstr(struct ini_data *ini, const TCHAR *root, int idx, TCHAR *name, int *nsize, TCHAR *str, int *size)
{
	TCHAR *name2 = NULL;
	TCHAR *str2 = NULL;
	name[0] = 0;
	str[0] = 0;
	int ret = ini_getsectionstring(ini, gs(root), idx, &name2, &str2);
	if (ret) {
		if (uaetcslen(name2) >= *nsize) {
			name2[(*nsize) - 1] = 0;
		}
		if (uaetcslen(str2) >= *size) {
			str2[(*size) - 1] = 0;
		}
		_tcscpy(name, name2);
		_tcscpy(str, str2);
	}
	xfree(str2);
	xfree(name2);
	return ret;
}

int regini_querydatasize(struct ini_data *ini, const TCHAR *root, const TCHAR *name, int *size)
{
	int ret = 0;
	int csize = 65536;
	TCHAR *tmp = xmalloc(TCHAR, csize);
	if (regini_querystr(ini, root, name, tmp, &csize)) {
		*size = uaetcslen(tmp) / 2;
		ret = 1;
	}
	xfree(tmp);
	return ret;
}

int regini_setdata(struct ini_data *ini, const TCHAR *root, const TCHAR *name, const void *str, int size)
{
	const uae_u8 *in = (const uae_u8*)str;
	int ret;
	TCHAR *tmp = xmalloc(TCHAR, size * 2 + 1);
	for (int i = 0; i < size; i++) {
		_stprintf(tmp + i * 2, _T("%02X"), in[i]);
	}
	ret = ini_addstring(ini, gs(root), name, tmp);
	xfree(tmp);
	return ret;
}

int regini_querydata(struct ini_data *ini, const TCHAR *root, const TCHAR *name, void *str, int *size)
{
	int csize = (*size) * 2 + 1;
	int i, j, len;
	int ret = 0;
	TCHAR *tmp = xmalloc(TCHAR, csize);
	uae_u8 *out = (uae_u8*)str;

	if (!regini_querystr(ini, root, name, tmp, &csize)) {
		goto err;
	}
	j = 0;
	len = uaetcslen(tmp);
	for (i = 0; i < len; i += 2) {
		TCHAR c1 = _totupper(tmp[i + 0]);
		TCHAR c2 = _totupper(tmp[i + 1]);
		if (c1 >= 'A') {
			c1 -= 'A' - 10;
		} else if (c1 >= '0') {
			c1 -= '0';
		}
		if (c1 > 15) {
			goto err;
		}
		if (c2 >= 'A') {
			c2 -= 'A' - 10;
		} else if (c2 >= '0') {
			c2 -= '0';
		}
		if (c2 > 15) {
			goto err;
		}
		out[j++] = c1 * 16 + c2;
	}
	ret = 1;
err:
	xfree(tmp);
	return ret;
}

int regini_delete(struct ini_data *ini, const TCHAR *root, const TCHAR *name)
{
	ini_delete(ini, gs(root), name);
	return 1;
}

int regini_exists(struct ini_data *ini, const TCHAR *root, const TCHAR *name)
{
	if (!ini) {
		return 0;
	}
	return ini_getstring(ini, gs(root), name, NULL);
}

void regini_deletetree(struct ini_data *ini, const TCHAR *root, const TCHAR *name)
{
	TCHAR *s = gsn(root, name);
	if (!s) {
		return;
	}
	ini_delete(ini, s, NULL);
	xfree(s);
}

int regini_existstree(struct ini_data *ini, const TCHAR *root, const TCHAR *name)
{
	int ret = 0;
	TCHAR *s = gsn(root, name);
	if (!s) {
		return 0;
	}
	ret = ini_getstring(ini, s, NULL, NULL);
	xfree(s);
	return ret;
}

TCHAR *regini_treepath(const TCHAR *root, const TCHAR *name)
{
	if (!root) {
		if (!name) {
			return my_strdup(gs(NULL));
		}
		return my_strdup(name);
	}
	return gsn(root, name);
}
