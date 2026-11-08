#ifndef UAE_CONSOLE_H
#define UAE_CONSOLE_H

// True while console output is redirected by setconsolemode().
bool console_is_buffered(void);

// The shared console has already attempted buffer capture. Hosts retain
// their native console and logfile mirroring policy, including whether
// captured output is also written to a log.
void target_console_out(const TCHAR *text, bool buffered);

#endif
