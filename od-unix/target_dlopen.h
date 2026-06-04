#pragma once

bool target_dlopen_plugin(const TCHAR *name, TCHAR *loaded_path,
	size_t loaded_path_size, UAE_DLHANDLE *handlep);
