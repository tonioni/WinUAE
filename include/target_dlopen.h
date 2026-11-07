#ifndef UAE_TARGET_DLOPEN_H
#define UAE_TARGET_DLOPEN_H

#ifdef TARGET_HAS_DLOPEN_PLUGIN
bool target_dlopen_plugin(const TCHAR *name, TCHAR *loaded_path,
	size_t loaded_path_size, UAE_DLHANDLE *handlep);
#else
static inline bool target_dlopen_plugin(const TCHAR *, TCHAR *, size_t,
	UAE_DLHANDLE *)
{
	return false;
}

#endif

#endif /* UAE_TARGET_DLOPEN_H */
