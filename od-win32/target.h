 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Target specific stuff, Win32 version
  *
  * Copyright 1997 Mathias Ortmann
  */

#ifdef _WIN32_WCE
#define TARGET_NAME "WinCE"
#define DONT_PARSE_CMDLINE
#else
#define TARGET_NAME _T("win32")
#endif

#define NO_MAIN_IN_MAIN_C

#define OPTIONSFILENAME _T("default.uae")
