#include "sysconfig.h"
#include "sysdeps.h"
#include "uae/dlopen.h"

UAE_DLHANDLE uae_dlopen(const TCHAR *path)
{
	if (!path || !path[0]) {
		write_log(_T("DLOPEN: No path given\n"));
		return NULL;
	}
	UAE_DLHANDLE result = LoadLibrary(path);

	if (!result) {
		write_log(_T("DLOPEN: Failed to open %s\n"), path);
	}
	return result;
}

void *uae_dlsym(UAE_DLHANDLE handle, const char *name)
{
	return (void *)GetProcAddress(handle, name);
}

void uae_dlclose(UAE_DLHANDLE handle)
{
	FreeLibrary(handle);
}

bool uae_dlpath(UAE_DLHANDLE handle, TCHAR *path, int size)
{
	if (!path || size <= 0) {
		return false;
	}
	DWORD len = GetModuleFileName(handle, path, size);
	if (len > 0 && len < (DWORD)size) {
		return true;
	}
	path[0] = 0;
	return false;
}
