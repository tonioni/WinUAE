#include "sysconfig.h"
#include "sysdeps.h"
#include "traps.h"
#include "blkdev.h"
#include <windows.h>

bool host_cd_is_device(const TCHAR *name)
{
	return name && _tcslen(name) == 3 && name[1] == ':' && name[2] == '\\';
}

int host_cd_next_device(int cursor, TCHAR *name, int name_size)
{
	if (!name || name_size <= 0) {
		return 0;
	}
	name[0] = 0;
	if (cursor < 0 || name_size < 4) {
		return 0;
	}
	for (int index = cursor; index <= 'Z' - 'C'; index++) {
		TCHAR device[] = _T("C:\\");
		device[0] = (TCHAR)('C' + index);
		if (GetDriveType(device) == DRIVE_CDROM) {
			_tcscpy(name, device);
			return index + 1;
		}
	}
	return 0;
}
