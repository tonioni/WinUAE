#include "sysconfig.h"
#include "sysdeps.h"
#include "uae/api.h"
#include "uae/dlopen.h"
#include "uae/log.h"
#include "target_dlopen.h"

UAE_DLHANDLE uae_dlopen_plugin(const TCHAR *name)
{
#if defined(FSUAE)
	const TCHAR *path = NULL;
	if (plugin_lookup) {
		path = plugin_lookup(name);
	}
	if (path == NULL or path[0] == _T('\0')) {
		write_log(_T("DLOPEN: Could not find plugin \"%s\"\n"), name);
		return NULL;
	}
	UAE_DLHANDLE handle = uae_dlopen(path);
#else
	TCHAR path[MAX_DPATH] = { 0 };
	UAE_DLHANDLE handle;
	if (!target_dlopen_plugin(name, path, MAX_DPATH, &handle)) {
		_tcscpy(path, name);
		_tcscat(path, TARGET_MODULE_SUFFIX);
		_tcscat(path, LT_MODULE_EXT);
		handle = uae_dlopen(path);
	}
#endif
	if (handle) {
		write_log(_T("DLOPEN: Loaded plugin %s\n"), path);
		uae_dlopen_patch_common(handle);
	}
	return handle;
}

void uae_dlopen_patch_common(UAE_DLHANDLE handle)
{
	void *ptr;
	ptr = uae_dlsym(handle, "uae_log");
	if (ptr) {
		write_log(_T("DLOPEN: Patching common functions\n"));
		*((uae_log_function *)ptr) = &uae_log;
	}
}
