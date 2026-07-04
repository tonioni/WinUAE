#include "sysconfig.h"
#include "sysdeps.h"

#include <sys/stat.h>
#include <unistd.h>
#if defined(UAE_HOST_DARWIN)
#include <mach-o/dyld.h>
#endif

#include "registry.h"
#include "ini.h"
#include "uae/string.h"

/* Unix port of the od-win32 registry abstraction. Only the ini-file mode
 * exists here; trees map to slash-joined ini section names below the
 * WinUAE root section, exactly like winuae.ini on Windows. */

#define ROOT_TREE _T("WinUAE")

static int initialized;
static TCHAR inipath[MAX_DPATH];
static TCHAR explicit_inipath[MAX_DPATH];
static struct ini_data *inidata;

static bool registry_executable_dir(TCHAR *out, size_t out_size)
{
#if defined(UAE_HOST_DARWIN)
    uint32_t size = out_size;
    if (_NSGetExecutablePath(out, &size) != 0) {
        return false;
    }
#elif defined(UAE_HOST_LINUX)
    ssize_t len = readlink("/proc/self/exe", out, out_size - 1);
    if (len <= 0 || (size_t)len >= out_size) {
        return false;
    }
    out[len] = 0;
#else
    return false;
#endif
    TCHAR *slash = _tcsrchr(out, '/');
    if (!slash) {
        return false;
    }
    *slash = 0;
    return true;
}

static void registry_user_ini_path(TCHAR *out, size_t out_size)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) {
        home = "/tmp";
    }
#if defined(UAE_HOST_DARWIN)
    snprintf(out, out_size, "%s/Library/Application Support/WinUAE/winuae.ini", home);
#else
    const char *config_home = getenv("XDG_CONFIG_HOME");
    if (config_home && config_home[0]) {
        snprintf(out, out_size, "%s/winuae/winuae.ini", config_home);
    } else {
        snprintf(out, out_size, "%s/.config/winuae/winuae.ini", home);
    }
#endif
}

static void registry_resolve_path(void)
{
    if (explicit_inipath[0]) {
        uae_tcslcpy(inipath, explicit_inipath, MAX_DPATH);
        return;
    }
    const char *env = getenv("WINUAE_INI");
    if (env && env[0]) {
        uae_tcslcpy(inipath, env, MAX_DPATH);
        return;
    }
    TCHAR exedir[MAX_DPATH];
    if (registry_executable_dir(exedir, MAX_DPATH)
        && _tcslen(exedir) + 12 < MAX_DPATH) {
        _stprintf(inipath, _T("%s/winuae.ini"), exedir);
        if (access(inipath, F_OK) == 0) {
            /* Portable mode, matching winuae.ini next to winuae.exe. */
            return;
        }
    }
    registry_user_ini_path(inipath, MAX_DPATH);
}

static void registry_mkdirs(const TCHAR *path)
{
    TCHAR tmp[MAX_DPATH];
    uae_tcslcpy(tmp, path, MAX_DPATH);
    TCHAR *slash = _tcsrchr(tmp, '/');
    if (!slash || slash == tmp) {
        return;
    }
    *slash = 0;
    for (TCHAR *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0700);
            *p = '/';
        }
    }
    mkdir(tmp, 0700);
}

static void registry_init(void)
{
    if (initialized) {
        return;
    }
    initialized = 1;
    registry_resolve_path();
    inidata = ini_load(inipath, true);
    if (!inidata) {
        inidata = ini_new();
    }
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

void registry_set_ini_path (const TCHAR *path)
{
    if (path && path[0]) {
        uae_tcslcpy(explicit_inipath, path, MAX_DPATH);
    } else {
        explicit_inipath[0] = 0;
    }
    if (initialized) {
        registry_flush();
        ini_free(inidata);
        inidata = NULL;
        initialized = 0;
    }
}

void registry_flush (void)
{
    if (!initialized || !inidata || !inidata->modified) {
        return;
    }
    registry_mkdirs(inipath);
    if (ini_save(inidata, inipath)) {
        inidata->modified = false;
    } else {
        write_log(_T("Failed to save host settings to '%s'\n"), inipath);
    }
}

int regsetstr (UAEREG *root, const TCHAR *name, const TCHAR *str)
{
    registry_init();
    return ini_addstring(inidata, gs(root), name, str);
}

int regsetint (UAEREG *root, const TCHAR *name, int val)
{
    TCHAR tmp[100];
    _stprintf (tmp, _T("%d"), val);
    return regsetstr(root, name, tmp);
}

int regqueryint (UAEREG *root, const TCHAR *name, int *val)
{
    int ret = 0;
    TCHAR *tmp = NULL;
    registry_init();
    if (ini_getstring(inidata, gs(root), name, &tmp)) {
        *val = _tstol (tmp);
        ret = 1;
    }
    xfree(tmp);
    return ret;
}

int regsetlonglong (UAEREG *root, const TCHAR *name, unsigned long long val)
{
    TCHAR tmp[100];
    _stprintf (tmp, _T("%lld"), (long long)val);
    return regsetstr(root, name, tmp);
}

int regquerylonglong (UAEREG *root, const TCHAR *name, unsigned long long *val)
{
    int ret = 0;
    TCHAR *tmp = NULL;
    *val = 0;
    registry_init();
    if (ini_getstring(inidata, gs(root), name, &tmp)) {
        *val = strtoll (tmp, NULL, 10);
        ret = 1;
    }
    xfree(tmp);
    return ret;
}

int regquerystr (UAEREG *root, const TCHAR *name, TCHAR *str, int *size)
{
    int ret = 0;
    TCHAR *tmp = NULL;
    registry_init();
    if (ini_getstring(inidata, gs(root), name, &tmp)) {
        if (_tcslen(tmp) >= (size_t)*size)
            tmp[(*size) - 1] = 0;
        _tcscpy (str, tmp);
        *size = (int)_tcslen(str);
        ret = 1;
    }
    xfree (tmp);
    return ret;
}

int regenumstr (UAEREG *root, int idx, TCHAR *name, int *nsize, TCHAR *str, int *size)
{
    TCHAR *name2 = NULL;
    TCHAR *str2 = NULL;
    name[0] = 0;
    str[0] = 0;
    registry_init();
    int ret = ini_getsectionstring(inidata, gs(root), idx, &name2, &str2);
    if (ret) {
        if (_tcslen(name2) >= (size_t)*nsize) {
            name2[(*nsize) - 1] = 0;
        }
        if (_tcslen(str2) >= (size_t)*size) {
            str2[(*size) - 1] = 0;
        }
        _tcscpy(name, name2);
        _tcscpy(str, str2);
    }
    xfree(str2);
    xfree(name2);
    return ret;
}

int regquerydatasize (UAEREG *root, const TCHAR *name, int *size)
{
    int ret = 0;
    int csize = 65536;
    TCHAR *tmp = xmalloc (TCHAR, csize);
    if (regquerystr (root, name, tmp, &csize)) {
        *size = (int)_tcslen(tmp) / 2;
        ret = 1;
    }
    xfree (tmp);
    return ret;
}

int regsetdata (UAEREG *root, const TCHAR *name, const void *str, int size)
{
    uae_u8 *in = (uae_u8*)str;
    int ret;
    TCHAR *tmp = xmalloc (TCHAR, size * 2 + 1);
    for (int i = 0; i < size; i++)
        _stprintf (tmp + i * 2, _T("%02X"), in[i]);
    registry_init();
    ret = ini_addstring(inidata, gs(root), name, tmp);
    xfree (tmp);
    return ret;
}

int regquerydata (UAEREG *root, const TCHAR *name, void *str, int *size)
{
    int csize = (*size) * 2 + 1;
    int i, j;
    int ret = 0;
    TCHAR *tmp = xmalloc (TCHAR, csize);
    uae_u8 *out = (uae_u8*)str;

    if (!regquerystr (root, name, tmp, &csize))
        goto err;
    j = 0;
    for (i = 0; i < (int)_tcslen (tmp); i += 2) {
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
}

int regdelete (UAEREG *root, const TCHAR *name)
{
    registry_init();
    ini_delete(inidata, gs(root), name);
    return 1;
}

int regexists (UAEREG *root, const TCHAR *name)
{
    registry_init();
    if (!inidata)
        return 0;
    return ini_getstring(inidata, gs(root), name, NULL);
}

void regdeletetree (UAEREG *root, const TCHAR *name)
{
    TCHAR *s = gsn (root, name);
    if (!s)
        return;
    registry_init();
    ini_delete(inidata, s, NULL);
    xfree (s);
}

int regexiststree (UAEREG *root, const TCHAR *name)
{
    int ret = 0;
    TCHAR *s = gsn (root, name);
    if (!s)
        return 0;
    registry_init();
    ret = ini_getstring(inidata, s, NULL, NULL);
    xfree (s);
    return ret;
}

UAEREG *regcreatetree (UAEREG *root, const TCHAR *name)
{
    UAEREG *fkey;
    TCHAR *ininame;

    registry_init();
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
    return fkey;
}

void regclosetree (UAEREG *key)
{
    registry_flush();
    if (!key)
        return;
    xfree (key->inipath);
    xfree (key);
}

int reginitializeinit (TCHAR **pppath)
{
    if (pppath && *pppath) {
        registry_set_ini_path(*pppath);
    }
    registry_init();
    return 1;
}

void regstatus (void)
{
    registry_init();
    write_log (_T("Host settings: '%s'\n"), inipath);
}

const TCHAR *getregmode (void)
{
    registry_init();
    return inipath;
}
