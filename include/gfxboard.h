
extern addrbank gfxboard_bank_memory;
extern addrbank gfxboard_bank_registers;

extern void gfxboard_init_memory (void);
extern void gfxboard_init_memory_p4_z2 (void);
extern void gfxboard_init_registers (void);
extern void gfxboard_free (void);
extern void gfxboard_reset (void);
extern void gfxboard_vsync_handler (void);
extern bool gfxboard_is_z3 (int);
extern bool gfxboard_is_registers (int);
extern int gfxboard_get_vram_min (int);
extern int gfxboard_get_vram_max (int);
extern bool gfxboard_need_byteswap (int type);
extern double gfxboard_get_vsync (void);
extern void gfxboard_refresh (void);
extern bool gfxboard_toggle (int mode);
extern int gfxboard_num_boards (int type);

#define GFXBOARD_UAE_Z2 0
#define GFXBOARD_UAE_Z3 1
#define GFXBOARD_HARDWARE 2
