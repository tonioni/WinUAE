 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Debugger
  *
  * (c) 1995 Bernd Schmidt
  *
  */

#ifdef DEBUGGER

#define	MAX_HIST 100

extern int debugging;
extern int exception_debugging;

extern void debug(void);
extern void activate_debugger(void);
extern int notinrom (void);
extern const char *debuginfo(int);

#else

STATIC_INLINE void activate_debugger (void) { };

#endif
