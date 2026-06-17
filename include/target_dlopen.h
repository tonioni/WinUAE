#ifndef UAE_TARGET_DLOPEN_H
#define UAE_TARGET_DLOPEN_H

static inline bool target_dlopen_plugin(const TCHAR *, TCHAR *, size_t,
	UAE_DLHANDLE *)
{
	return false;
}

#endif /* UAE_TARGET_DLOPEN_H */
