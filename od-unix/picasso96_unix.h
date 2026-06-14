#ifndef UAE_PICASSO96_UNIX_H
#define UAE_PICASSO96_UNIX_H

struct picasso96_state_struct
{
    RGBFTYPE            RGBFormat;   /* true-colour, CLUT, hi-colour, etc. */
    struct MyCLUTEntry  CLUT[2 * 256];   /* Duh! */
    uaecptr             Address;     /* Active screen address (Amiga-side) */
    uaecptr             Extent;      /* End address of screen (Amiga-side) */
    uae_u16             Width;       /* Active display width  (From SetGC) */
    uae_u16             VirtualWidth;/* Total screen width (From SetPanning) */
    uae_u16             BytesPerRow; /* Total screen width in bytes (From SetGC) */
    uae_u16             Height;      /* Active display height (From SetGC) */
    uae_u16             VirtualHeight; /* Total screen height */
    uae_u8              GC_Depth;    /* From SetGC() */
    uae_u8              GC_Flags;    /* From SetGC() */
    int                 XOffset;     /* From SetPanning() */
    int                 YOffset;     /* From SetPanning() */
    uae_u8              SwitchState; /* From SetSwitch() - 0 is Amiga, 1 is Picasso */
    uae_u8              BytesPerPixel;
    uae_u8              CardFound;
    uae_u8              BigAssBitmap;
    unsigned int        Version;
    uae_u8              *HostAddress;
    int                 XYOffset;
    bool                dualclut, advDragging;
    int                 HLineDBL, VLineDBL;
    bool                ModeChanged;
};

extern void InitPicasso96 (int monid);

extern struct picasso96_state_struct picasso96_state[MAX_AMIGAMONITORS];

extern void picasso_enablescreen (int monid, int on);
extern void picasso_refresh (int monid);
extern void picasso_handle_vsync (void);
extern void picasso_allocatewritewatch (int index, int gfxmemsize);
extern int picasso_getwritewatch (int index, int offset, uae_u8 ***gwwbufp, uae_u8 **startp);
extern bool picasso_is_vram_dirty (int index, uaecptr addr, int size);
extern void picasso_invalidate (int monid, int x, int y, int w, int h);
extern void init_hz_p96 (int monid);

/* This structure describes the UAE-side framebuffer for the Picasso
 * screen.  */
struct picasso_vidbuf_description {
    int width, height, depth;
    int rowbytes, pixbytes, offset;
    int maxwidth, maxheight;
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

extern void gfx_set_picasso_modeinfo (int monid, RGBFTYPE rgbfmt);
extern void gfx_set_picasso_colors (int monid, RGBFTYPE rgbfmt);
extern void gfx_set_picasso_baseaddr (uaecptr);
extern void gfx_set_picasso_state (int monid, int on);
extern uae_u8 *gfx_lock_picasso (int monid, bool);
extern void gfx_unlock_picasso (int monid, bool);
extern void fb_copyrow (int monid, uae_u8 *src, uae_u8 *dst, int x, int y, int width, int srcpixbytes, int dy);

extern int p96refresh_active;

#endif /* UAE_PICASSO96_UNIX_H */
