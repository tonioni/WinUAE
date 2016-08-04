#ifndef UAE_GFXBOARD_H
#define UAE_GFXBOARD_H

extern bool gfxboard_init_memory (struct autoconfig_info*);
extern bool gfxboard_init_memory_p4_z2(struct autoconfig_info*);
extern bool gfxboard_init_registers(struct autoconfig_info*);
extern void gfxboard_free (void);
extern void gfxboard_reset (void);
extern bool gfxboard_vsync_handler (void);
extern void gfxboard_hsync_handler(void);
extern int gfxboard_get_configtype (struct rtgboardconfig*);
extern bool gfxboard_is_registers (struct rtgboardconfig*);
extern int gfxboard_get_vram_min (struct rtgboardconfig*);
extern int gfxboard_get_vram_max (struct rtgboardconfig*);
extern bool gfxboard_need_byteswap (struct rtgboardconfig*);
extern int gfxboard_get_autoconfig_size(struct rtgboardconfig*);
extern double gfxboard_get_vsync (void);
extern void gfxboard_refresh (void);
extern int gfxboard_toggle (int mode, int msg);
extern int gfxboard_num_boards (struct rtgboardconfig*);
extern uae_u32 gfxboard_get_romtype(struct rtgboardconfig*);
extern const TCHAR *gfxboard_get_name(int);
extern const TCHAR *gfxboard_get_manufacturername(int);
extern const TCHAR *gfxboard_get_configname(int);
extern bool gfxboard_allocate_slot(int, int);
extern void gfxboard_free_slot(int);
extern bool gfxboard_rtg_enable_initial(int);
extern void gfxboard_rtg_disable(int);

extern bool tms_init(struct autoconfig_info *aci);
extern void tms_free(void);
extern void tms_reset(void);
extern void tms_hsync_handler(void);
extern bool tms_vsync_handler(void);
extern bool tms_toggle(int);

extern void vga_io_put(int board, int portnum, uae_u8 v);
extern uae_u8 vga_io_get(int board, int portnum);
extern void vga_ram_put(int board, int offset, uae_u8 v);
extern uae_u8 vga_ram_get(int board, int offset);

void gfxboard_get_a8_vram(int index);
void gfxboard_free_vram(int index);

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
