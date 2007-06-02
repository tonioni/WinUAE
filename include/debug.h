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
#define MAX_LINEWIDTH 100

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

#define BREAKPOINT_TOTAL 8
struct breakpoint_node {
    uaecptr addr;
    int enabled;
};
extern struct breakpoint_node bpnodes[BREAKPOINT_TOTAL];

#define MEMWATCH_TOTAL 8
struct memwatch_node {
    uaecptr addr;
    int size;
    int rwi;
    uae_u32 val;
    int val_enabled;
    uae_u32 modval;
    int modval_written;
    int frozen;
};
extern struct memwatch_node mwnodes[MEMWATCH_TOTAL];

extern void memwatch_dump2 (char *buf, int bufsize, int num);

void debug_lgetpeek (uaecptr addr, uae_u32 v);
void debug_wgetpeek (uaecptr addr, uae_u32 v);
void debug_bgetpeek (uaecptr addr, uae_u32 v);
void debug_bputpeek(uaecptr addr, uae_u32 v);
void debug_wputpeek(uaecptr addr, uae_u32 v);
void debug_lputpeek(uaecptr addr, uae_u32 v);


#else

STATIC_INLINE void activate_debugger (void) { };

#endif
