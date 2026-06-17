#include "sysconfig.h"
#include "sysdeps.h"

#include "uae/dlopen.h"
#include "uae/log.h"
#include "target_dlopen.h"

#include <dlfcn.h>
#if defined(UAE_HOST_DARWIN)
#include <mach-o/dyld.h>
#elif defined(UAE_HOST_LINUX)
#include <unistd.h>
#endif

static bool get_executable_dir(TCHAR *out, size_t out_size)
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
	TCHAR *slash = _tcsrchr(out, FSDB_DIR_SEPARATOR);
	if (!slash) {
		return false;
	}
	*slash = 0;
	return true;
}

static bool append_plugin_path(TCHAR *path, size_t path_size,
	const TCHAR *prefix, const TCHAR *name, const TCHAR *suffix)
{
	if (_tcslen(prefix) + _tcslen(name) + _tcslen(suffix) >= path_size) {
		return false;
	}
	_tcscpy(path, prefix);
	_tcscat(path, name);
	_tcscat(path, suffix);
	return true;
}

static UAE_DLHANDLE try_unix_plugin_path(const TCHAR *path,
	const char **last_error)
{
	dlerror();
	UAE_DLHANDLE handle = dlopen(path, RTLD_NOW);
	if (!handle && last_error) {
		*last_error = dlerror();
	}
	return handle;
}

bool target_dlopen_plugin(const TCHAR *name, TCHAR *loaded_path,
	size_t loaded_path_size, UAE_DLHANDLE *handlep)
{
	const char *last_error = NULL;
	TCHAR executable_dir[MAX_DPATH] = { 0 };
	bool have_executable_dir = get_executable_dir(executable_dir,
		MAX_DPATH);

	*handlep = NULL;

#if defined(UAE_HOST_DARWIN)
	const TCHAR *suffixes[] = { _T(".so"), LT_MODULE_EXT, NULL };
	const TCHAR *prefixes[] = {
		_T("@executable_path/../PlugIns/"),
		_T("@executable_path/"),
		_T("./plugins/"),
		_T("./"),
		_T(""),
		NULL
	};
#else
	const TCHAR *suffixes[] = { LT_MODULE_EXT, NULL };
	const TCHAR *prefixes[] = {
		_T("./plugins/"),
		_T("./"),
		_T(""),
		NULL
	};
#endif

	for (int s = 0; suffixes[s]; s++) {
		for (int p = 0; prefixes[p]; p++) {
			TCHAR path[MAX_DPATH];
			if (!append_plugin_path(path, MAX_DPATH, prefixes[p], name,
				suffixes[s])) {
				continue;
			}
			UAE_DLHANDLE handle = try_unix_plugin_path(path, &last_error);
			if (handle) {
				_tcsncpy(loaded_path, path, loaded_path_size - 1);
				loaded_path[loaded_path_size - 1] = 0;
				*handlep = handle;
				return true;
			}
		}
		if (have_executable_dir) {
			TCHAR prefix[MAX_DPATH];
			if (_tcslen(executable_dir) + 2 < MAX_DPATH) {
				_tcscpy(prefix, executable_dir);
				_tcscat(prefix, _T("/"));
				TCHAR path[MAX_DPATH];
				if (append_plugin_path(path, MAX_DPATH, prefix, name,
					suffixes[s])) {
					UAE_DLHANDLE handle = try_unix_plugin_path(path,
						&last_error);
					if (handle) {
						_tcsncpy(loaded_path, path,
							loaded_path_size - 1);
						loaded_path[loaded_path_size - 1] = 0;
						*handlep = handle;
						return true;
					}
				}
			}
			if (_tcslen(executable_dir) + 10 < MAX_DPATH) {
				_tcscpy(prefix, executable_dir);
				_tcscat(prefix, _T("/plugins/"));
				TCHAR path[MAX_DPATH];
				if (append_plugin_path(path, MAX_DPATH, prefix, name,
					suffixes[s])) {
					UAE_DLHANDLE handle = try_unix_plugin_path(path,
						&last_error);
					if (handle) {
						_tcsncpy(loaded_path, path,
							loaded_path_size - 1);
						loaded_path[loaded_path_size - 1] = 0;
						*handlep = handle;
						return true;
					}
				}
			}
#ifdef WINUAE_UNIX_INSTALL_PLUGINDIR_RELATIVE
			if (_tcslen(executable_dir) + 4 +
				_tcslen(WINUAE_UNIX_INSTALL_PLUGINDIR_RELATIVE) + 2 <
				MAX_DPATH) {
				_tcscpy(prefix, executable_dir);
				_tcscat(prefix, _T("/../"));
				_tcscat(prefix, WINUAE_UNIX_INSTALL_PLUGINDIR_RELATIVE);
				_tcscat(prefix, _T("/"));
				TCHAR path[MAX_DPATH];
				if (append_plugin_path(path, MAX_DPATH, prefix, name,
					suffixes[s])) {
					UAE_DLHANDLE handle = try_unix_plugin_path(path,
						&last_error);
					if (handle) {
						_tcsncpy(loaded_path, path,
							loaded_path_size - 1);
						loaded_path[loaded_path_size - 1] = 0;
						*handlep = handle;
						return true;
					}
				}
			}
#endif
		}
	}

	if (last_error) {
		write_log("DLOPEN: %s\n", last_error);
	}
	write_log(_T("DLOPEN: Could not find plugin \"%s\"\n"), name);
	return true;
}
