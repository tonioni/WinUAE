#include "sysconfig.h"
#include "sysdeps.h"

#include <sys/stat.h>
#include <unistd.h>
#if defined(UAE_HOST_DARWIN)
#include <mach-o/dyld.h>
#endif

#include "registry.h"
#include "ini.h"
#include "od-generic/registry_ini.h"
#include "uae/string.h"

/* Unix port of the od-win32 registry abstraction. Only the ini-file mode
 * exists here; the ini format is shared with Windows through
 * od-generic/registry_ini.cpp. This file only picks the ini file and
 * loads and saves it. */

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
    return root ? root->inipath : NULL;
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
    return regini_setstr(inidata, gs(root), name, str);
}

int regsetint (UAEREG *root, const TCHAR *name, int val)
{
    registry_init();
    return regini_setint(inidata, gs(root), name, val);
}

int regqueryint (UAEREG *root, const TCHAR *name, int *val)
{
    registry_init();
    return regini_queryint(inidata, gs(root), name, val);
}

int regsetlonglong (UAEREG *root, const TCHAR *name, unsigned long long val)
{
    registry_init();
    return regini_setlonglong(inidata, gs(root), name, val);
}

int regquerylonglong (UAEREG *root, const TCHAR *name, unsigned long long *val)
{
    registry_init();
    return regini_querylonglong(inidata, gs(root), name, val);
}

int regquerystr (UAEREG *root, const TCHAR *name, TCHAR *str, int *size)
{
    registry_init();
    return regini_querystr(inidata, gs(root), name, str, size);
}

int regenumstr (UAEREG *root, int idx, TCHAR *name, int *nsize, TCHAR *str, int *size)
{
    registry_init();
    return regini_enumstr(inidata, gs(root), idx, name, nsize, str, size);
}

int regquerydatasize (UAEREG *root, const TCHAR *name, int *size)
{
    registry_init();
    return regini_querydatasize(inidata, gs(root), name, size);
}

int regsetdata (UAEREG *root, const TCHAR *name, const void *str, int size)
{
    registry_init();
    return regini_setdata(inidata, gs(root), name, str, size);
}

int regquerydata (UAEREG *root, const TCHAR *name, void *str, int *size)
{
    registry_init();
    return regini_querydata(inidata, gs(root), name, str, size);
}

int regdelete (UAEREG *root, const TCHAR *name)
{
    registry_init();
    return regini_delete(inidata, gs(root), name);
}

int regexists (UAEREG *root, const TCHAR *name)
{
    registry_init();
    return regini_exists(inidata, gs(root), name);
}

void regdeletetree (UAEREG *root, const TCHAR *name)
{
    registry_init();
    regini_deletetree(inidata, gs(root), name);
}

int regexiststree (UAEREG *root, const TCHAR *name)
{
    registry_init();
    return regini_existstree(inidata, gs(root), name);
}

UAEREG *regcreatetree (UAEREG *root, const TCHAR *name)
{
    registry_init();
    UAEREG *fkey = xcalloc (UAEREG, 1);
    fkey->inipath = regini_treepath(gs(root), name);
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
