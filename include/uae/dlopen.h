/*
 * UAE shared-library loading interface (declarations only).
 *
 * The implementation lives in dlopen.cpp, with the Unix plugin search
 * (target_dlopen_plugin) in od-unix/dlopen.cpp. All functions keep C++
 * linkage. floppybridge/floppybridge_lib.cpp includes this header only on
 * non-Windows builds.
 *
 * On Windows the caller must have <windows.h> visible already, which is
 * where HINSTANCE comes from; sysdeps.h provides it via <winsock.h>. This
 * header does not include <windows.h> itself.
 *
 * Copyright (C) 2026 Dimitris Panokostas
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Clean-room implementation written from a functional description of the
 * UAE portability interface.
 */

#ifndef UAE_DLOPEN_H
#define UAE_DLOPEN_H

#include "uae/types.h"

#ifdef _WIN32
#define UAE_DLHANDLE HINSTANCE
#else
#define UAE_DLHANDLE void *
#endif

/* Load a shared library from `path`. Null or empty path fails. */
UAE_DLHANDLE uae_dlopen(const TCHAR *path);

/* Look up `symbol` (always a narrow string) in a loaded library. */
void *uae_dlsym(UAE_DLHANDLE handle, const char *symbol);

/* Close a library obtained from uae_dlopen/uae_dlopen_plugin. */
void uae_dlclose(UAE_DLHANDLE handle);

/* Search the plugin directories for `name` (no extension), append the
 * platform suffix (LT_MODULE_EXT) and load it. */
UAE_DLHANDLE uae_dlopen_plugin(const TCHAR *name);

/* Patch a freshly loaded plugin: point its exported `uae_log` variable
 * at the host's uae_log. */
void uae_dlopen_patch_common(UAE_DLHANDLE handle);

#endif /* UAE_DLOPEN_H */
