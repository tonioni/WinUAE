 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Target specific stuff, Win32 version
  *
  * Copyright 1997 Mathias Ortmann
  */

#ifdef _WIN32_WCE
#define TARGET_NAME "WinCE"
#define TARGET_NO_AUTOCONF
#define TARGET_NO_ZFILE
#define DONT_PARSE_CMDLINE
#else
#define TARGET_NAME _T("win32")
#endif
#define TARGET_PROVIDES_DEFAULT_PREFS
#define TARGET_NO_DITHER

#define NO_MAIN_IN_MAIN_C

#define OPTIONSFILENAME _T("default.uae")

