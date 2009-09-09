 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Debugger
  *
  * (c) 1995 Bernd Schmidt
  *
  */

#ifdef DEBUGGER

#define	MAX_HIST 500
#define MAX_LINEWIDTH 100

extern int debugging;
extern int exception_debugging;
extern int debug_copper;
extern int debug_dma;
extern int debug_sprite_mask;
extern int debug_bpl_mask, debug_bpl_mask_one;
extern int debugger_active;

extern void debug (void);
extern void debugger_change (int mode);
extern void activate_debugger (void);
extern void deactivate_debugger (void);
extern int notinrom (void);
extern const TCHAR *debuginfo (int);
extern void record_copper (uaecptr addr, int hpos, int vpos);
extern void record_copper_reset (void);
extern int mmu_init (int, uaecptr,uaecptr);
extern void mmu_do_hit (void);
extern void dump_aga_custom (void);
extern void memory_map_dump (void);
extern void debug_help (void);
extern uaecptr dumpmem2 (uaecptr addr, TCHAR *out, int osize);
extern void update_debug_info (void);
extern int instruction_breakpoint (TCHAR **c);
extern int debug_bankchange (int);

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
    uae_u32 val, valmask;
    int mustchange;
    int val_enabled;
    uae_u32 modval;
    int modval_written;
    int frozen;
    uaecptr pc;
};
extern struct memwatch_node mwnodes[MEMWATCH_TOTAL];

extern void memwatch_dump2 (TCHAR *buf, int bufsize, int num);

void debug_lgetpeek (uaecptr addr, uae_u32 v);
void debug_wgetpeek (uaecptr addr, uae_u32 v);
void debug_bgetpeek (uaecptr addr, uae_u32 v);
void debug_bputpeek(uaecptr addr, uae_u32 v);
void debug_wputpeek(uaecptr addr, uae_u32 v);
void debug_lputpeek(uaecptr addr, uae_u32 v);

enum debugtest_item { DEBUGTEST_BLITTER, DEBUGTEST_KEYBOARD, DEBUGTEST_FLOPPY, DEBUGTEST_MAX };
void debugtest (enum debugtest_item, const TCHAR *, ...);

struct dma_rec
{
    uae_u16 reg;
    uae_u16 dat;
    uae_u32 addr;
    uae_u16 evt;
};

#define DMA_EVENT_BLITIRQ 1
#define DMA_EVENT_BLITNASTY 2
#define DMA_EVENT_BLITFINISHED 4
#define DMA_EVENT_BPLFETCHUPDATE 8
#define DMA_EVENT_COPPERWAKE 16

extern struct dma_rec *record_dma (uae_u16 reg, uae_u16 dat, uae_u32 addr, int hpos, int vpos);
extern void record_dma_reset (void);
extern void record_dma_event (int evt, int hpos, int vpos);

#else

STATIC_INLINE void activate_debugger (void) { };

#endif
