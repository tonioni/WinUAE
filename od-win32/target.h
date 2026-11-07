 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Target specific stuff, Win32 version
  *
  * Copyright 1997 Mathias Ortmann
  */

#ifdef _WIN32_WCE
#define TARGET_NAME_COMPAT _T("unix")
#define TARGET_NAME "WinCE"
#define DONT_PARSE_CMDLINE
#else
#define TARGET_NAME _T("win32")
#endif

#define WINUAEAPPNAME _T("Arabuusimiehet.WinUAE")
#define TARGET_HAS_DLOPEN_PLUGIN

#define NO_MAIN_IN_MAIN_C

#define OPTIONSFILENAME _T("default.uae")

// Implemented by the Windows backend instead of the generic defaults.
#define TARGET_HAS_MACHDEP_FREE
#define TARGET_HAS_AUDIO_PULL
#define TARGET_HAS_STATUSLINE_UPDATED
#define TARGET_HAS_CUSTOM_FSDB
#define TARGET_HAS_EXTERNAL_FILESYSTEM
#define TARGET_HAS_KEYFILE
#define TARGET_HAS_CPU_RATE
#define TARGET_HAS_TOUCH_LIGHTPEN
#define TARGET_HAS_DEBUG_INFO
