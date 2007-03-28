#ifndef __DEBUG_WIN32_H__
#define __DEBUG_WIN32_H__

extern int open_debug_window(void);
extern void close_debug_window(void);
extern void WriteOutput(const char *out, int len);
extern int GetInput (char *out, int maxlen);
extern int console_get_gui (char *out, int maxlen);

#include <pshpack2.h>
typedef struct {
    WORD dlgVer;
    WORD signature;
    DWORD helpID;
    DWORD exStyle;
    DWORD style;
    WORD cDlgItems;
    short x;
    short y;
    short cx;
    short cy;
} DLGTEMPLATEEX;

typedef struct {
    WORD pointsize;
    WORD weight;
    BYTE italic;
    BYTE charset;
    WCHAR typeface[0];
} DLGTEMPLATEEX_END;

typedef struct {
    DWORD helpID;
    DWORD exStyle;
    DWORD style;
    short x;
    short y;
    short cx;
    short cy;
    DWORD id;
} DLGITEMTEMPLATEEX;
#include <poppack.h>

#endif
