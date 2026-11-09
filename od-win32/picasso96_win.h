/*
 * UAE - The U*nix Amiga Emulator
 *
 * Picasso96 Support Module Header
 *
 * Copyright 1997 Brian King <Brian_King@Mitel.com, Brian_King@Cloanto.com>
 */
#ifndef __PICASSO96_H__
#define __PICASSO96_H__

#include "picasso96.h"

#ifdef PICASSO96

#include "render.h"

/************************************************************************/
struct picasso96_state_struct
{
    RGBFTYPE            RGBFormat;   /* true-colour, CLUT, hi-colour, etc.*/
    struct MyCLUTEntry  CLUT[2 * 256];   /* Duh! */
    uaecptr             Address;     /* Active screen address (Amiga-side)*/
    uaecptr             Extent;      /* End address of screen (Amiga-side)*/
    uae_u16             Width;       /* Active display width  (From SetGC)*/
    uae_u16             VirtualWidth;/* Total screen width (From SetPanning)*/
    uae_u16             BytesPerRow; /* Total screen width in bytes (FromSetGC) */
    uae_u16             Height;      /* Active display height (From SetGC)*/
    uae_u16             VirtualHeight; /* Total screen height */
    uae_u8              GC_Depth;    /* From SetGC() */
    uae_u8              GC_Flags;    /* From SetGC() */
    int                 XOffset;     /* From SetPanning() */
    int                 YOffset;     /* From SetPanning() */
    uae_u8              SwitchState; /* From SetSwitch() - 0 is Amiga, 1 isPicasso */
    uae_u8              BytesPerPixel;
    uae_u8              CardFound;
    //here follow winuae additional entrys
    uae_u8		BigAssBitmap; /* Set to 1 when our Amiga screen is bigger than the displayable area */
    unsigned int	Version;
    uae_u8		*HostAddress; /* Active screen address (PC-side) */
    // host address is need because Windows
    // support NO direct access all the time to gfx Card
    // every time windows can remove your surface from card so the mainrender place
    // must be in memory
    int         XYOffset;
    bool        dualclut, advDragging;
    int         HLineDBL, VLineDBL;
    bool        ModeChanged;
};

extern void InitPicasso96(int monid);

extern int uaegfx_card_found;

extern struct picasso96_state_struct picasso96_state[MAX_AMIGAMONITORS];

extern void picasso_enablescreen(int monid, int on);
extern void picasso_refresh(int monid);
extern void init_hz_p96(int monid);
extern void picasso_handle_vsync(void);
extern void picasso_trigger_vblank(void);
extern int picasso_get_splitypos(int monid);
extern bool picasso_is_active(int monid);
extern int picasso_setwincursor(int monid);
extern int picasso_palette(struct MyCLUTEntry *MCLUT, uae_u32 *clut);
extern void picasso_allocatewritewatch (int index, int gfxmemsize);
extern int picasso_getwritewatch(int index, int offset, uae_u8 ***gwwbufp, uae_u8 **startp);
extern bool picasso_is_vram_dirty (int index, uaecptr addr, int size);
extern void picasso_statusline (int monid, uae_u8 *dst);
extern void picasso_invalidate(int monid, int x, int y, int w, int h);

/* This structure describes the UAE-side framebuffer for the Picasso
 * screen.  */
struct picasso_vidbuf_description {
	int width, height, depth;
	int rowbytes, pixbytes, offset;
    int maxwidth, maxheight; // allocated surface/texture size
	int extra_mem; /* nonzero if there's a second buffer that must be updated */
	uae_u32 rgbformat;
	uae_u32 selected_rgbformat;
	uae_u32 clut[256 * 2];
	int picasso_convert[2], host_mode;
	int ohost_mode, orgbformat;
	int full_refresh;
	int set_panning_called;
	int rtg_clear_flag;
	bool picasso_active;
	bool picasso_changed;
    uae_s16 splitypos;
	uae_atomic picasso_state_change;
    uae_u32 dacrgbformat[2];
};

extern struct picasso_vidbuf_description picasso_vidinfo[MAX_AMIGAMONITORS];

extern void gfx_set_picasso_modeinfo(int monid, RGBFTYPE rgbfmt);
extern void gfx_set_picasso_colors(int monid, RGBFTYPE rgbfmt);
extern void gfx_set_picasso_state(int monid,int on);
extern uae_u8 *gfx_lock_picasso(int monid, bool);
extern void gfx_unlock_picasso(int monid, bool);

void lockrtg(void);
void unlockrtg(void);

void fb_copyrow(int monid, uae_u8 *src, uae_u8 *dst, int x, int y, int width, int srcpixbytes, int dy);

extern int p96refresh_active;

#endif

#endif
