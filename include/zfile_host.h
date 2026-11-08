#ifndef UAE_ZFILE_HOST_H
#define UAE_ZFILE_HOST_H

#include "uae/types.h"

// Zero means a local filename, positive means HTTP(S), negative means FTP.
// Targets without URL loading report all names as local paths.
int host_internet_file_type(const TCHAR *name);
// On success the caller owns the allocated data. Check-only success has
// no data and size zero. On failure both outputs are cleared.
bool host_read_internet_file(const TCHAR *name, bool check_only,
	uae_u8 **data, int *size);

#endif /* UAE_ZFILE_HOST_H */
