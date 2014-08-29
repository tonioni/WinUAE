#include "sysconfig.h"
#include "sysdeps.h"

#include "uae/dlopen.h"
#ifdef _WIN32

#else
#include <dlfcn.h>
#endif

UAE_DLHANDLE uae_dlopen(const TCHAR *path) {
#ifdef _WIN32
	UAE_DLHANDLE result = LoadLibrary(path);
#else
	UAE_DLHANDLE result = dlopen(path, RTLD_NOW);
	const char *error = dlerror();
	if (error != NULL)  {
		write_log("uae_dlopen failed: %s\n", error);
	}
#endif
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

void uae_patch_library_common(UAE_DLHANDLE handle)
{
	void *ptr;

	ptr = uae_dlsym(handle, "uae_log");
	if (ptr) *((uae_log_function *) ptr) = &uae_log;
}
