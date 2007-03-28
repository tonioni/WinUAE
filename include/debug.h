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
#define MAX_LINEWIDTH 90

extern int debugging;
extern int exception_debugging;
extern int debug_copper;
extern int debug_sprite_mask;
extern int debugger_active;

extern void debug(void);
extern void debugger_change(int mode);
extern void activate_debugger(void);
extern void deactivate_debugger (void);
extern int notinrom (void);
extern const char *debuginfo(int);
extern void record_copper (uaecptr addr, int hpos, int vpos);
extern void record_copper_reset(void);
extern int mmu_init(int,uaecptr,uaecptr);
extern void mmu_do_hit(void);
extern void dump_aga_custom (void);
extern void memory_map_dump (void);
extern void debug_help (void);
extern uaecptr dumpmem2 (uaecptr addr, char *out, int osize);
extern void update_debug_info (void);
#else

STATIC_INLINE void activate_debugger (void) { };

#endif
