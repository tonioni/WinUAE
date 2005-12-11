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
extern int debug_copper;
extern int debug_sprite_mask;

extern void debug(void);
extern void activate_debugger(void);
extern int notinrom (void);
extern const char *debuginfo(int);
extern void record_copper (uaecptr addr, int hpos, int vpos);
extern void record_copper_reset(void);

#else

STATIC_INLINE void activate_debugger (void) { };

#endif
