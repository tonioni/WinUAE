#include "sysconfig.h"
#include "sysdeps.h"

#include "uae/dlopen.h"
#ifdef _WIN32

#else
#include <dlfcn.h>
#endif

UAE_DLHANDLE uae_dlopen(const TCHAR *path)
{
	UAE_DLHANDLE result;
#ifdef WINUAE
	result = uae_dlopen_plugin(path);
#elif _WIN32
	result = LoadLibrary(path);
#else
	result = dlopen(path, RTLD_NOW);
	const char *error = dlerror();
	if (error != NULL)  {
		write_log("uae_dlopen failed: %s\n", error);
	}
#endif
	if (result)
		uae_dlopen_patch_common(result);
	return result;
}

void *uae_dlsym(UAE_DLHANDLE handle, const char *name)
{
#if 0
	if (handle == NULL) {
		return NULL;
	}
#endif
#ifdef _WIN32
	return (void *) GetProcAddress(handle, name);
#else
	return dlsym(handle, name);
#endif
}

void uae_dlclose(UAE_DLHANDLE handle)
{
#ifdef _WIN32
	FreeLibrary (handle);
#else
	dlclose(handle);
#endif
}

#include "uae/log.h"

void uae_dlopen_patch_common(UAE_DLHANDLE handle)
{
	write_log(_T("DLOPEN: Patching common functions\n"));
	void *ptr;
	ptr = uae_dlsym(handle, "uae_log");
	if (ptr) *((uae_log_function *) ptr) = &uae_log;
}
