#ifndef UAE_GFXBOARD_H
#define UAE_GFXBOARD_H

extern addrbank *gfxboard_init_memory (int devnum);
extern addrbank *gfxboard_init_memory_p4_z2(int devnum);
extern addrbank *gfxboard_init_registers(int devnum);
extern void gfxboard_free (void);
extern void gfxboard_reset (void);
extern void gfxboard_vsync_handler (void);
extern void gfxboard_hsync_handler(void);
extern int gfxboard_get_configtype (int);
extern bool gfxboard_is_registers (int);
extern int gfxboard_get_vram_min (int);
extern int gfxboard_get_vram_max (int);
extern bool gfxboard_need_byteswap (int type);
extern int gfxboard_get_autoconfig_size(int type);
extern double gfxboard_get_vsync (void);
extern void gfxboard_refresh (void);
extern bool gfxboard_toggle (int mode);
extern int gfxboard_num_boards (int type);
extern uae_u32 gfxboard_get_romtype(int type);
extern const TCHAR *gfxboard_get_name(int);
extern const TCHAR *gfxboard_get_manufacturername(int); 
extern const TCHAR *gfxboard_get_configname(int);

extern addrbank *tms_init(int devnum);
extern void tms_free(void);
extern void tms_reset(void);
extern void tms_hsync_handler(void);
extern void tms_vsync_handler(void);
extern bool tms_toggle(int);

extern void vga_io_put(int portnum, uae_u8 v);
extern uae_u8 vga_io_get(int portnum);
extern void vga_ram_put(int offset, uae_u8 v);
extern uae_u8 vga_ram_get(int offset);

#define GFXBOARD_UAE_Z2 0
#define GFXBOARD_UAE_Z3 1
#define GFXBOARD_HARDWARE 2

#define GFXBOARD_PICASSO2 2
#define GFXBOARD_PICASSO2PLUS 3
#define GFXBOARD_PICCOLO_Z2 4
#define GFXBOARD_PICCOLO_Z3 5
#define GFXBOARD_SD64_Z2 6
#define GFXBOARD_SD64_Z3 7
#define GFXBOARD_SPECTRUM_Z2 8
#define GFXBOARD_SPECTRUM_Z3 9
#define GFXBOARD_PICASSO4_Z2 10
#define GFXBOARD_PICASSO4_Z3 11
#define GFXBOARD_A2410 12
#define GFXBOARD_VGA 13

#endif /* UAE_GFXBOARD_H */
