#ifndef WINUAE_OD_UNIX_TARGET_H
#define WINUAE_OD_UNIX_TARGET_H

#define TARGET_NAME _T("unix")
#define OPTIONSFILENAME _T("default.uae")

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
