#ifndef __DEBUG_WIN32_H__
#define __DEBUG_WIN32_H__

extern int open_debug_window(void);
extern void close_debug_window(void);
extern void WriteOutput(const char *out, int len);
extern int GetInput (char *out, int maxlen);
extern int console_get_gui (char *out, int maxlen);

#endif
