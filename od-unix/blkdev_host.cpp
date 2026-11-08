#include "sysconfig.h"
#include "sysdeps.h"
#include "traps.h"
#include "blkdev.h"

bool host_cd_is_device(const TCHAR *name)
{
	return name && !_tcsncmp(name, _T("/dev/"), 5);
}

#ifndef WITH_SCSI_IOCTL
int host_cd_next_device(int cursor, TCHAR *name, int name_size)
{
	if (name && name_size > 0) {
		name[0] = 0;
	}
	return 0;
}
#endif
