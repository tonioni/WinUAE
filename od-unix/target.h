#ifndef WINUAE_OD_UNIX_TARGET_H
#define WINUAE_OD_UNIX_TARGET_H

#define TARGET_NAME _T("unix")
// Also read the settings that WinUAE for Windows saves as win32.*.
#define TARGET_NAME_COMPAT _T("win32")
// True when a win32.* option means the same on Unix (without the prefix).
bool unix_config_is_compat_option(const TCHAR *option);
#define OPTIONSFILENAME _T("default.uae")

#include "winuae_version.h"

#define TARGET_ROM_PATH _T("~/")
#define TARGET_FLOPPY_PATH _T("~/")
#define TARGET_HARDFILE_PATH _T("~/")
#define TARGET_SAVESTATE_PATH _T("~/")

#define DEFPRTNAME _T("lpr")
#define DEFSERNAME _T("/dev/ttyS0")

static inline int uae_deterministic_mode(void)
{
    return 0;
}

#endif /* WINUAE_OD_UNIX_TARGET_H */
