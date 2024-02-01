#include "sysconfig.h"
#include "sysdeps.h"

int relativepaths = 0;

// convert path to absolute or relative
void fullpath(TCHAR *path, int size, bool userelative)
{
	// FIXME: forward/backslash fix needed
	if (path[0] == 0 || (path[0] == '\\' && path[1] == '\\') ||
	    path[0] == ':') {
		return;
	}
	/* <drive letter>: is supposed to mean same as <drive letter>:\ */
#if 0
	if (path[0] == 0 || (path[0] == '\\' && path[1] == '\\') || path[0] == ':')
		return;
	// has one or more environment variables? do nothing.
	if (_tcschr(path, '%'))
		return;
	if (_tcslen(path) >= 2 && path[_tcslen(path) - 1] == '.')
		return;
	/* <drive letter>: is supposed to mean same as <drive letter>:\ */
	if (_istalpha (path[0]) && path[1] == ':' && path[2] == 0)
		_tcscat (path, _T("\\"));
	if (userelative) {
		TCHAR tmp1[MAX_DPATH], tmp2[MAX_DPATH];
		tmp1[0] = 0;
		GetCurrentDirectory (sizeof tmp1 / sizeof (TCHAR), tmp1);
		fixtrailing (tmp1);
		tmp2[0] = 0;
		int ret = GetFullPathName (path, sizeof tmp2 / sizeof (TCHAR), tmp2, NULL);
		if (ret == 0 || ret >= sizeof tmp2 / sizeof (TCHAR))
			return;
		if (_tcslen(tmp1) > 2 && _tcsnicmp(tmp1, tmp2, 3) == 0 && tmp1[1] == ':' && tmp1[2] == '\\') {
			// same drive letter
			path[0] = 0;
			if (PathRelativePathTo(path, tmp1, FILE_ATTRIBUTE_DIRECTORY, tmp2, tmp2[_tcslen(tmp2) - 1] == '\\' ? FILE_ATTRIBUTE_DIRECTORY : 0)) {
				if (path[0]) {
					if (path[0] == '.' && path[1] == 0) {
						_tcscpy(path, _T(".\\"));
					} else if (path[0] == '\\') {
						_tcscpy(tmp1, path + 1);
						_stprintf(path, _T(".\\%s"), tmp1);
					} else if (path[0] != '.') {
						_tcscpy(tmp1, path);
						_stprintf(path, _T(".\\%s"), tmp1);
					}
				} else {
					_tcscpy (path, tmp2);
				}
				goto done;
			}
		}
		if (_tcsnicmp (tmp1, tmp2, _tcslen (tmp1)) == 0) {
			// tmp2 is inside tmp1
			_tcscpy (path, _T(".\\"));
			_tcscat (path, tmp2 + _tcslen (tmp1));
		} else {
			_tcscpy (path, tmp2);
		}
done:;
	} else {
		TCHAR tmp[MAX_DPATH];
		_tcscpy(tmp, path);
		DWORD err = GetFullPathName (tmp, size, path, NULL);
	}
#endif
}

// convert path to absolute or relative
void fullpath (TCHAR *path, int size)
{
	fullpath(path, size, relativepaths);
}

