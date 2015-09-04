 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Interface to the graphics system (X, SVGAlib)
  *
  * Copyright 1995-1997 Bernd Schmidt
  */

#ifndef UAE_XWIN_H
#define UAE_XWIN_H

#include "uae/types.h"
#include "machdep/rpt.h"

typedef uae_u32 xcolnr;

typedef int (*allocfunc_type)(int, int, int, xcolnr *);

extern xcolnr xcolors[4096];
extern xcolnr xcolors_16[4096];
extern xcolnr xcolors_32[4096];
extern uae_u32 p96_rgbx16[65536];

extern int graphics_setup (void);
extern int graphics_init (bool);
extern void graphics_leave(void);
extern void graphics_reset(bool);
extern bool handle_events (void);
extern int handle_msgpump (void);
extern void setup_brkhandler (void);
extern int isfullscreen (void);
extern void toggle_fullscreen (int);
extern bool toggle_rtg (int);

extern void toggle_mousegrab (void);
void setmouseactivexy (int x, int y, int dir);

extern void desktop_coords (int *dw, int *dh, int *x, int *y, int *w, int *h);
extern bool vsync_switchmode (int);
extern frame_time_t vsync_busywait_end (int*);
extern int vsync_busywait_do (int*, bool, bool);
extern void vsync_busywait_start (void);
extern double vblank_calibrate (double, bool);
extern bool vsync_isdone (void);
extern void doflashscreen (void);
extern int flashscreen;
extern void updatedisplayarea (void);
extern int isvsync_chipset (void);
extern int isvsync_rtg (void);
extern int isvsync (void);

extern void flush_line (struct vidbuffer*, int);
extern void flush_block (struct vidbuffer*, int, int);
extern void flush_screen (struct vidbuffer*, int, int);
extern void flush_clear_screen (struct vidbuffer*);
extern bool render_screen (bool);
extern void show_screen (int);
extern bool show_screen_maybe (bool);

extern int lockscr (struct vidbuffer*, bool);
extern void unlockscr (struct vidbuffer*);
extern bool target_graphics_buffer_update (void);

void getgfxoffset (float *dxp, float *dyp, float *mxp, float *myp);
double getcurrentvblankrate (void); /* todo: remove from od-win32/win32gfx.h */

extern int debuggable (void);
extern void LED (int);
extern void screenshot (int,int);
void refreshtitle (void);

extern int bits_in_mask (unsigned long mask);
extern int mask_shift (unsigned long mask);
extern unsigned int doMask (int p, int bits, int shift);
extern unsigned int doMask256 (int p, int bits, int shift);
extern void setup_maxcol (int);
extern void alloc_colors256 (int (*)(int, int, int, xcolnr *));
extern void alloc_colors64k (int, int, int, int, int, int, int, int, int, int);
extern void alloc_colors_rgb (int rw, int gw, int bw, int rs, int gs, int bs, int aw, int as, int alpha, int byte_swap,
			      uae_u32 *rc, uae_u32 *gc, uae_u32 *bc);
extern void alloc_colors_picasso (int rw, int gw, int bw, int rs, int gs, int bs, int rgbfmt);
extern void setup_greydither (int bits, allocfunc_type allocfunc);
extern void setup_greydither_maxcol (int maxcol, allocfunc_type allocfunc);
extern void setup_dither (int bits, allocfunc_type allocfunc);
extern void DitherLine (uae_u8 *l, uae_u16 *r4g4b4, int x, int y, uae_s16 len, int bits) ASM_SYM_FOR_FUNC("DitherLine");
extern double getvsyncrate (double hz, int *mult);

    /* The graphics code has a choice whether it wants to use a large buffer
     * for the whole display, or only a small buffer for a single line.
     * If you use a large buffer:
     *   - set bufmem to point at it
     *   - set linemem to 0
     *   - if memcpy within bufmem would be very slow, i.e. because bufmem is
     *     in graphics card memory, also set emergmem to point to a buffer
     *     that is large enough to hold a single line.
     *   - implement flush_line to be a no-op.
     * If you use a single line buffer:
     *   - set bufmem and emergmem to 0
     *   - set linemem to point at your buffer
     *   - implement flush_line to copy a single line to the screen
     */
struct vidbuffer
{
    /* Function implemented by graphics driver */
    void (*flush_line)         (struct vidbuf_description *gfxinfo, struct vidbuffer *vb, int line_no);
    void (*flush_block)        (struct vidbuf_description *gfxinfo, struct vidbuffer *vb, int first_line, int end_line);
    void (*flush_screen)       (struct vidbuf_description *gfxinfo, struct vidbuffer *vb, int first_line, int end_line);
    void (*flush_clear_screen) (struct vidbuf_description *gfxinfo, struct vidbuffer *vb);
    int  (*lockscr)            (struct vidbuf_description *gfxinfo, struct vidbuffer *vb);
    void (*unlockscr)          (struct vidbuf_description *gfxinfo, struct vidbuffer *vb);
    uae_u8 *linemem;
    uae_u8 *emergmem;

	uae_u8 *bufmem, *bufmemend;
    uae_u8 *realbufmem;
	uae_u8 *bufmem_allocated;
	bool bufmem_lockable;
    int rowbytes; /* Bytes per row in the memory pointed at by bufmem. */
    int pixbytes; /* Bytes per pixel. */
	/* size of this buffer */
	int width_allocated;
	int height_allocated;
	/* size of max visible image */
	int outwidth;
	int outheight;
	/* nominal size of image for centering */
	int inwidth;
	int inheight;
	/* same but doublescan multiplier included */
	int inwidth2;
	int inheight2;
	/* use drawbuffer instead */
	bool nativepositioning;
	/* tempbuffer in use */
	bool tempbufferinuse;
	/* extra width, chipset hpos extra in right border */
	int extrawidth;

	int xoffset; /* superhires pixels from left edge */
	int yoffset; /* lines from top edge */

	int inxoffset; /* positive if sync positioning */
	int inyoffset;
};

extern bool isnativevidbuf (void);
extern int max_uae_width, max_uae_height;

struct vidbuf_description
{

    int maxblocklines; /* Set to 0 if you want calls to flush_line after each drawn line, or the number of
			* lines that flush_block wants to/can handle (it isn't really useful to use another
			* value than maxline here). */

    struct vidbuffer drawbuffer;
	/* output buffer when using A2024 emulation */
	struct vidbuffer tempbuffer;

	struct vidbuffer *inbuffer;
	struct vidbuffer *outbuffer;

	int gfx_resolution_reserved; // reserved space for currprefs.gfx_resolution
	int gfx_vresolution_reserved; // reserved space for currprefs.gfx_resolution
	int xchange; /* how many superhires pixels in one pixel in buffer */
	int ychange; /* how many interlaced lines in one line in buffer */
};

extern struct vidbuf_description gfxvidinfo;

/* For ports using tui.c, this should be built by graphics_setup(). */
extern struct bstring *video_mode_menu;
extern void vidmode_menu_selected(int);

#endif /* UAE_XWIN_H */
