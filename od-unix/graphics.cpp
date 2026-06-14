#include "sysconfig.h"
#include "sysdeps.h"

#include "custom.h"
#ifdef AVIOUTPUT
#include "avioutput.h"
#endif
#include "xwin.h"
#include "drawing.h"
#include "options.h"
#include "memory.h"
#include "picasso96.h"
#include "uae.h"
#include "video.h"
#include "host.h"
#include "devices.h"
#include "gfxboard.h"

#include <condition_variable>
#include <mutex>
#include <stdlib.h>
#include <thread>
#include <utility>
#include <vector>

extern int pause_emulation;
extern void picasso_trigger_vblank(void);
extern void unix_rtg_overlay_sprite(int monid, uae_u32 *dst, int width, int height, int rowpixels);

uae_u32 p96_rgbx16[65536];
bool gfx_hdr;
int flashscreen;
struct picasso96_state_struct picasso96_state[MAX_AMIGAMONITORS];
struct picasso_vidbuf_description picasso_vidinfo[MAX_AMIGAMONITORS];

static bool unix_graphics_initialized;
static bool unix_video_debug;
static bool unix_rtg_render_has_output[MAX_AMIGAMONITORS];
static void unix_rtg_stop_render_thread(void);

enum {
    UNIX_PICASSO_STATE_SETDISPLAY = 1,
    UNIX_PICASSO_STATE_SETPANNING = 2,
    UNIX_PICASSO_STATE_SETGC = 4,
    UNIX_PICASSO_STATE_SETDAC = 8,
    UNIX_PICASSO_STATE_SETSWITCH = 16,
    UNIX_PICASSO_STATE_SPRITE = 32
};

static int unix_picasso_bytes_per_pixel(RGBFTYPE rgbfmt)
{
    switch (rgbfmt) {
    case RGBFB_CLUT:
    case RGBFB_Y4U1V1:
        return 1;
    case RGBFB_R5G6B5:
    case RGBFB_R5G5B5:
    case RGBFB_R5G6B5PC:
    case RGBFB_R5G5B5PC:
    case RGBFB_B5G6R5PC:
    case RGBFB_B5G5R5PC:
    case RGBFB_Y4U2V2:
        return 2;
    case RGBFB_R8G8B8:
    case RGBFB_B8G8R8:
        return 3;
    case RGBFB_A8R8G8B8:
    case RGBFB_A8B8G8R8:
    case RGBFB_R8G8B8A8:
    case RGBFB_B8G8R8A8:
        return 4;
    default:
        return 0;
    }
}

static uae_u16 unix_picasso_load_host_u16(const uae_u8 *src)
{
    uae_u16 value;
    memcpy(&value, src, sizeof value);
    return value;
}

static void unix_init_colors(void)
{
    alloc_colors64k(0, 8, 8, 8, 16, 8, 0, 8, 24, 1, 0);
    notice_new_xcolors();
    alloc_colors_picasso(8, 8, 8, 16, 8, 0, RGBFB_R8G8B8A8, p96_rgbx16);
}

static void unix_alloc_buffer(int monid, struct vidbuffer *buffer, int width, int height)
{
    if (buffer->realbufmem && buffer->width_allocated >= width && buffer->height_allocated >= height &&
        buffer->pixbytes == 4) {
        return;
    }

    freevidbuffer(monid, buffer);
    allocvidbuffer(monid, buffer, width, height, 32);
    buffer->initialized = true;
}

static void unix_init_display_buffers(void)
{
    struct vidbuf_description *vidinfo = &adisplays[0].gfxvidinfo;

    vidinfo->gfx_resolution_reserved = RES_MAX;
    vidinfo->gfx_vresolution_reserved = VRES_MAX;
    vidinfo->xchange = 1;
    vidinfo->ychange = 1;

    unix_alloc_buffer(0, &vidinfo->drawbuffer, 1920, 1280);
    unix_alloc_buffer(0, &vidinfo->tempbuffer, 2048, 2048);

    vidinfo->drawbuffer.monitor_id = 0;
    vidinfo->tempbuffer.monitor_id = 0;
    vidinfo->outbuffer = &vidinfo->drawbuffer;
    vidinfo->inbuffer = &vidinfo->drawbuffer;
}

static int unix_apmode_index(int monid)
{
    if (monid >= 0 && monid < MAX_AMIGADISPLAYS &&
        (adisplays[monid].picasso_on || picasso_vidinfo[monid].picasso_active)) {
        return APMODE_RTG;
    }
    return APMODE_NATIVE;
}

static int unix_fullscreen_state(int fullscreen)
{
    if (fullscreen == GFX_FULLSCREEN) {
        return 1;
    }
    if (fullscreen == GFX_FULLWINDOW) {
        return -1;
    }
    return 0;
}

static enum unix_video_window_mode unix_video_mode_from_prefs(int fullscreen)
{
    if (fullscreen == GFX_FULLSCREEN) {
        return UNIX_VIDEO_FULLSCREEN;
    }
    if (fullscreen == GFX_FULLWINDOW) {
        return UNIX_VIDEO_FULLWINDOW;
    }
    return UNIX_VIDEO_WINDOWED;
}

static void unix_apply_video_mode_from_prefs(struct uae_prefs *prefs, int monid)
{
    if (monid < 0 || monid >= MAX_AMIGADISPLAYS) {
        return;
    }

    fixup_prefs_dimensions(prefs);
    int idx = unix_apmode_index(monid);
    struct apmode *ap = &prefs->gfx_apmode[idx];
    struct wh *size = ap->gfx_fullscreen == GFX_WINDOW
        ? &prefs->gfx_monitor[monid].gfx_size_win
        : &prefs->gfx_monitor[monid].gfx_size_fs;

    prefs->gfx_monitor[monid].gfx_size = *size;
    int width = size->special == WH_NATIVE ? 0 : size->width;
    int height = size->special == WH_NATIVE ? 0 : size->height;
    unix_video_set_window_mode(unix_video_mode_from_prefs(ap->gfx_fullscreen),
        ap->gfx_display, width, height, ap->gfx_refreshrate);
}

static bool unix_runtime_graphics_prefs_changed(int monid)
{
    int idx = unix_apmode_index(monid);

    return currprefs.gfx_apmode[APMODE_NATIVE].gfx_fullscreen != changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_fullscreen ||
        currprefs.gfx_apmode[APMODE_RTG].gfx_fullscreen != changed_prefs.gfx_apmode[APMODE_RTG].gfx_fullscreen ||
        currprefs.gfx_apmode[APMODE_NATIVE].gfx_display != changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_display ||
        currprefs.gfx_apmode[APMODE_RTG].gfx_display != changed_prefs.gfx_apmode[APMODE_RTG].gfx_display ||
        currprefs.gfx_apmode[APMODE_NATIVE].gfx_backbuffers != changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_backbuffers ||
        currprefs.gfx_apmode[APMODE_RTG].gfx_backbuffers != changed_prefs.gfx_apmode[APMODE_RTG].gfx_backbuffers ||
        currprefs.gfx_apmode[idx].gfx_refreshrate != changed_prefs.gfx_apmode[idx].gfx_refreshrate ||
        currprefs.gfx_monitor[monid].gfx_size_fs.width != changed_prefs.gfx_monitor[monid].gfx_size_fs.width ||
        currprefs.gfx_monitor[monid].gfx_size_fs.height != changed_prefs.gfx_monitor[monid].gfx_size_fs.height ||
        currprefs.gfx_monitor[monid].gfx_size_fs.special != changed_prefs.gfx_monitor[monid].gfx_size_fs.special ||
        currprefs.gfx_monitor[monid].gfx_size_win.width != changed_prefs.gfx_monitor[monid].gfx_size_win.width ||
        currprefs.gfx_monitor[monid].gfx_size_win.height != changed_prefs.gfx_monitor[monid].gfx_size_win.height ||
        currprefs.gfx_monitor[monid].gfx_size_win.special != changed_prefs.gfx_monitor[monid].gfx_size_win.special;
}

static void unix_copy_runtime_graphics_prefs(int monid)
{
    currprefs.gfx_apmode[APMODE_NATIVE].gfx_fullscreen = changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_fullscreen;
    currprefs.gfx_apmode[APMODE_RTG].gfx_fullscreen = changed_prefs.gfx_apmode[APMODE_RTG].gfx_fullscreen;
    currprefs.gfx_apmode[APMODE_NATIVE].gfx_display = changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_display;
    currprefs.gfx_apmode[APMODE_RTG].gfx_display = changed_prefs.gfx_apmode[APMODE_RTG].gfx_display;
    currprefs.gfx_apmode[APMODE_NATIVE].gfx_backbuffers = changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_backbuffers;
    currprefs.gfx_apmode[APMODE_RTG].gfx_backbuffers = changed_prefs.gfx_apmode[APMODE_RTG].gfx_backbuffers;
    currprefs.gfx_apmode[APMODE_NATIVE].gfx_refreshrate = changed_prefs.gfx_apmode[APMODE_NATIVE].gfx_refreshrate;
    currprefs.gfx_apmode[APMODE_RTG].gfx_refreshrate = changed_prefs.gfx_apmode[APMODE_RTG].gfx_refreshrate;
    currprefs.gfx_monitor[monid].gfx_size_fs = changed_prefs.gfx_monitor[monid].gfx_size_fs;
    currprefs.gfx_monitor[monid].gfx_size_win = changed_prefs.gfx_monitor[monid].gfx_size_win;
}

int graphics_setup(void)
{
    unix_video_debug = getenv("WINUAE_UNIX_VIDEO_DEBUG") != NULL;
    unix_init_colors();
    if (unix_video_debug) {
        write_log(_T("Unix video colors: direct_rgb=%d black=%08x white=%08x r255=%08x g255=%08x b255=%08x\n"),
            direct_rgb ? 1 : 0, xcolors[0], xcolors[0xfff], xredcolors[255], xgreencolors[255], xbluecolors[255]);
    }
    InitPicasso96(0);
    return 1;
}

int graphics_init(bool)
{
    unix_init_display_buffers();
    struct vidbuffer *vb = &adisplays[0].gfxvidinfo.drawbuffer;
    if (!unix_video_init(vb->outwidth, vb->outheight, vb->pixbytes)) {
        write_log(_T("Unix video: no window presenter available, continuing headless\n"));
    }
    unix_apply_video_mode_from_prefs(&currprefs, 0);
    unix_graphics_initialized = true;
    return 1;
}

void graphics_leave(void)
{
#ifdef AVIOUTPUT
    AVIOutput_Release();
#endif
    unix_rtg_stop_render_thread();
    struct vidbuf_description *vidinfo = &adisplays[0].gfxvidinfo;
    freevidbuffer(0, &vidinfo->drawbuffer);
    freevidbuffer(0, &vidinfo->tempbuffer);
    unix_video_shutdown();
    unix_graphics_initialized = false;
}

void graphics_reset(bool) {}

bool handle_events(void)
{
    handle_msgpump(false);
    return pause_emulation != 0;
}

int handle_msgpump(bool)
{
    unix_host_check_quit();
    bool quit_requested = false;
    int got = unix_video_poll(&quit_requested);
    if (quit_requested) {
        uae_quit();
    }
    unix_host_check_quit();
    return got;
}

int check_prefs_changed_gfx(void)
{
    int flags = config_changed_flags;
    bool changed = unix_runtime_graphics_prefs_changed(0);

    if (!unix_graphics_initialized || (!changed && !flags)) {
        return 0;
    }

    config_changed_flags = 0;
    if (changed) {
        unix_copy_runtime_graphics_prefs(0);
        unix_apply_video_mode_from_prefs(&currprefs, 0);
    }
    return 1;
}

int isfullscreen(void)
{
    int idx = unix_apmode_index(0);
    return unix_fullscreen_state(currprefs.gfx_apmode[idx].gfx_fullscreen);
}

void toggle_fullscreen(int monid, int mode)
{
    if (monid < 0 || monid >= MAX_AMIGADISPLAYS) {
        return;
    }

    int idx = unix_apmode_index(monid);
    int v = changed_prefs.gfx_apmode[idx].gfx_fullscreen;
    static int wasfs[2];

    if (mode < 0) {
        if (v == GFX_FULLWINDOW) {
            wasfs[idx] = -1;
            v = GFX_WINDOW;
        } else if (v == GFX_WINDOW) {
            v = wasfs[idx] >= 0 ? GFX_FULLSCREEN : GFX_FULLWINDOW;
        } else if (v == GFX_FULLSCREEN) {
            wasfs[idx] = 1;
            v = GFX_WINDOW;
        }
    } else if (mode == 0) {
        v = v == GFX_FULLSCREEN ? GFX_WINDOW : GFX_FULLSCREEN;
    } else if (mode == 1) {
        v = v == GFX_FULLSCREEN ? GFX_FULLWINDOW : GFX_FULLSCREEN;
    } else if (mode == 2) {
        v = v == GFX_FULLWINDOW ? GFX_WINDOW : GFX_FULLWINDOW;
    } else if (mode == 10) {
        v = GFX_WINDOW;
    }

    changed_prefs.gfx_apmode[idx].gfx_fullscreen = v;
    devices_unsafeperiod();
    set_config_changed();
}
bool toggle_rtg(int monid, int)
{
    return monid >= 0 && monid < MAX_AMIGAMONITORS && currprefs.rtgboards[0].rtgmem_size > 0;
}
void close_rtg(int, bool) {}
void toggle_mousegrab(void) { unix_video_toggle_mouse_grab(); }
void setmouseactivexy(int, int, int, int) {}

void desktop_coords(int, int *dw, int *dh, int *x, int *y, int *w, int *h)
{
    unix_video_get_desktop(dw, dh, x, y, w, h);
}

bool vsync_switchmode(int, int) { return false; }
void vsync_clear(void) {}
int vsync_isdone(frame_time_t*) { return 1; }
void doflashscreen(void) {}
void updatedisplayarea(int) {}
void flush_line(struct vidbuffer*, int) {}
void flush_block(struct vidbuffer*, int, int) {}
void flush_screen(struct vidbuffer*, int, int) {}
void flush_clear_screen(struct vidbuffer*) {}
bool render_screen(int, int, bool)
{
    set_custom_limits(-1, -1, -1, -1, false);
    return true;
}

static void unix_log_video_frame(const struct vidbuffer *vb)
{
    static int frames;

    if (!unix_video_debug || !vb || !vb->bufmem || vb->pixbytes != 4) {
        return;
    }

    frames++;
    if (frames > 1 && (frames % 50) != 0) {
        return;
    }

    int nonblack = 0;
    int firstx = -1;
    int firsty = -1;
    int lastx = -1;
    int lasty = -1;
    uae_u32 first = 0;
    uae_u32 last = 0;

    int scan_width = vb->width_allocated > 0 ? vb->width_allocated : vb->outwidth;
    int scan_height = vb->height_allocated > 0 ? vb->height_allocated : vb->outheight;
    for (int y = 0; y < scan_height; y++) {
        const uae_u32 *row = (const uae_u32 *)(vb->bufmem + y * vb->rowbytes);
        for (int x = 0; x < scan_width; x++) {
            uae_u32 pixel = row[x];
            if ((pixel & 0x00ffffff) != 0) {
                if (!nonblack) {
                    firstx = x;
                    firsty = y;
                    first = pixel;
                }
                lastx = x;
                lasty = y;
                last = pixel;
                nonblack++;
            }
        }
    }

    write_log(_T("Unix video frame %d: out=%dx%d alloc=%dx%d pitch=%d xoff=%d yoff=%d nonblack=%d first=%d,%d:%08x last=%d,%d:%08x\n"),
        frames, vb->outwidth, vb->outheight, vb->width_allocated, vb->height_allocated, vb->rowbytes, vb->xoffset, vb->yoffset,
        nonblack, firstx, firsty, first, lastx, lasty, last);
}

void show_screen(int monid, int)
{
    if (!unix_graphics_initialized || monid < 0 || monid >= MAX_AMIGADISPLAYS) {
        return;
    }

    struct vidbuf_description *vidinfo = &adisplays[monid].gfxvidinfo;
    struct vidbuffer *vb = vidinfo->inbuffer ? vidinfo->inbuffer : &vidinfo->drawbuffer;
    if (!vb->bufmem || vb->outwidth <= 0 || vb->outheight <= 0) {
        return;
    }

    struct unix_video_frame frame;
    frame.pixels = vb->bufmem;
    frame.width = vb->outwidth;
    frame.height = vb->outheight;
    frame.rowbytes = vb->rowbytes;
    frame.pixbytes = vb->pixbytes;
    frame.filter_index = adisplays[monid].gf_index;
    frame.monitor_id = monid;
    frame.backbuffers = currprefs.gfx_apmode[unix_apmode_index(monid)].gfx_backbuffers;
    unix_log_video_frame(vb);
    unix_video_present(&frame);
}

bool show_screen_maybe(int monid, bool)
{
    show_screen(monid, 0);
    return true;
}

int lockscr(struct vidbuffer *vb, bool, bool)
{
    if (!vb || !vb->bufmem) {
        return 0;
    }
    vb->locked = true;
    return 1;
}

void unlockscr(struct vidbuffer *vb, int, int)
{
    if (vb) {
        vb->locked = false;
    }
}

bool target_graphics_buffer_update(int, bool) { return true; }
float target_adjust_vblank_hz(int, float hz) { return hz; }
int target_get_display_scanline(int) { return -1; }
void target_spin(int)
{
    static int spin_counter;
    if ((spin_counter++ & 31) == 0 || unix_host_quit_requested()) {
        handle_msgpump(false);
    }
}

void getgfxoffset(int, float *dxp, float *dyp, float *mxp, float *myp)
{
    if (dxp) *dxp = 0;
    if (dyp) *dyp = 0;
    if (mxp) *mxp = 1;
    if (myp) *myp = 1;
}

float target_getcurrentvblankrate(int monid)
{
    int idx = unix_apmode_index(monid);
    float rate = unix_video_get_display_refresh_rate(currprefs.gfx_apmode[idx].gfx_display);
    return rate > 0.0f ? rate : 60.0f;
}
int debuggable(void) { return 0; }

void refreshtitle(void)
{
    unix_video_set_title(_T(WINUAE_UNIX_WINDOW_TITLE));
}

void InitPicasso96(int monid)
{
    if (monid < 0 || monid >= MAX_AMIGAMONITORS) {
        return;
    }

    memset(&picasso96_state[monid], 0, sizeof picasso96_state[monid]);
    memset(&picasso_vidinfo[monid], 0, sizeof picasso_vidinfo[monid]);
    unix_rtg_render_has_output[monid] = false;
    picasso_vidinfo[monid].rgbformat = RGBFB_B8G8R8A8;
    picasso_vidinfo[monid].selected_rgbformat = RGBFB_B8G8R8A8;
    picasso_vidinfo[monid].host_mode = RGBFB_B8G8R8A8;
    picasso_vidinfo[monid].pixbytes = 4;
    for (int i = 0; i < 256; i++) {
        picasso96_state[monid].CLUT[i].Red = i;
        picasso96_state[monid].CLUT[i].Green = i;
        picasso96_state[monid].CLUT[i].Blue = i;
        picasso_vidinfo[monid].clut[i] = 0xff000000 | (i << 16) | (i << 8) | i;
    }
}

static bool unix_picasso_ensure_buffer(int monid)
{
    struct picasso96_state_struct *state = &picasso96_state[monid];
    struct picasso_vidbuf_description *pvidinfo = &picasso_vidinfo[monid];
    struct vidbuf_description *vidinfo = &adisplays[monid].gfxvidinfo;
    int width = state->Width > 0 ? state->Width : 640;
    int height = state->Height > 0 ? state->Height : 480;

    unix_alloc_buffer(monid, &vidinfo->drawbuffer, width, height);
    vidinfo->drawbuffer.outwidth = width;
    vidinfo->drawbuffer.outheight = height;
    vidinfo->drawbuffer.pixbytes = 4;
    vidinfo->drawbuffer.monitor_id = monid;
    vidinfo->inbuffer = &vidinfo->drawbuffer;
    vidinfo->outbuffer = &vidinfo->drawbuffer;

    pvidinfo->width = width;
    pvidinfo->height = height;
    pvidinfo->depth = state->GC_Depth ? (state->GC_Depth + 7) / 8 : 0;
    pvidinfo->pixbytes = 4;
    pvidinfo->rowbytes = vidinfo->drawbuffer.rowbytes;
    pvidinfo->maxwidth = vidinfo->drawbuffer.width_allocated;
    pvidinfo->maxheight = vidinfo->drawbuffer.height_allocated;
    pvidinfo->rgbformat = state->RGBFormat;
    pvidinfo->selected_rgbformat = state->RGBFormat;
    pvidinfo->host_mode = RGBFB_B8G8R8A8;

    return vidinfo->drawbuffer.bufmem != NULL;
}

static uae_u32 unix_picasso_convert_pixel(const uae_u8 *src, RGBFTYPE fmt,
    const uae_u32 *clut, const uae_u32 *rgbx16 = p96_rgbx16)
{
    switch (fmt) {
    case RGBFB_CLUT:
        return clut[src[0]];
    case RGBFB_R8G8B8:
        return 0xff000000 | ((uae_u32)src[0] << 16) | ((uae_u32)src[1] << 8) | src[2];
    case RGBFB_B8G8R8:
        return 0xff000000 | ((uae_u32)src[2] << 16) | ((uae_u32)src[1] << 8) | src[0];
    case RGBFB_R8G8B8A8:
        return ((uae_u32)src[3] << 24) | ((uae_u32)src[0] << 16) | ((uae_u32)src[1] << 8) | src[2];
    case RGBFB_B8G8R8A8:
        return ((uae_u32)src[3] << 24) | ((uae_u32)src[2] << 16) | ((uae_u32)src[1] << 8) | src[0];
    case RGBFB_A8R8G8B8:
        return ((uae_u32)src[0] << 24) | ((uae_u32)src[1] << 16) | ((uae_u32)src[2] << 8) | src[3];
    case RGBFB_A8B8G8R8:
        return ((uae_u32)src[0] << 24) | ((uae_u32)src[3] << 16) | ((uae_u32)src[2] << 8) | src[1];
    case RGBFB_R5G6B5:
    case RGBFB_R5G5B5:
    case RGBFB_R5G6B5PC:
    case RGBFB_R5G5B5PC:
    case RGBFB_B5G6R5PC:
    case RGBFB_B5G5R5PC:
        return rgbx16[unix_picasso_load_host_u16(src)];
    default:
        return 0xff000000;
    }
}

enum {
    RGBFB_A8R8G8B8_32 = 1,
    RGBFB_A8B8G8R8_32,
    RGBFB_R8G8B8A8_32,
    RGBFB_B8G8R8A8_32,
    RGBFB_R8G8B8_32,
    RGBFB_B8G8R8_32,
    RGBFB_R5G6B5PC_32,
    RGBFB_R5G5B5PC_32,
    RGBFB_R5G6B5_32,
    RGBFB_R5G5B5_32,
    RGBFB_B5G6R5PC_32,
    RGBFB_B5G5R5PC_32,
    RGBFB_CLUT_RGBFB_32,
    RGBFB_Y4U2V2_32,
    RGBFB_Y4U1V1_32,
};

static void unix_picasso_render_pixels(const uae_u8 *srcbase, int width, int height,
    int srcrowbytes, int srcpixbytes, RGBFTYPE rgbfmt, const uae_u32 *clut,
    uae_u8 *dstbase, int dstrowbytes, const uae_u32 *rgbx16 = p96_rgbx16);

int getconvert(int rgbformat)
{
    switch (rgbformat) {
    case RGBFB_CLUT:
        return RGBFB_CLUT_RGBFB_32;
    case RGBFB_B5G6R5PC:
        return RGBFB_B5G6R5PC_32;
    case RGBFB_R5G6B5PC:
        return RGBFB_R5G6B5PC_32;
    case RGBFB_R5G5B5PC:
        return RGBFB_R5G5B5PC_32;
    case RGBFB_R5G6B5:
        return RGBFB_R5G6B5_32;
    case RGBFB_R5G5B5:
        return RGBFB_R5G5B5_32;
    case RGBFB_B5G5R5PC:
        return RGBFB_B5G5R5PC_32;
    case RGBFB_A8R8G8B8:
        return RGBFB_A8R8G8B8_32;
    case RGBFB_R8G8B8:
        return RGBFB_R8G8B8_32;
    case RGBFB_B8G8R8:
        return RGBFB_B8G8R8_32;
    case RGBFB_A8B8G8R8:
        return RGBFB_A8B8G8R8_32;
    case RGBFB_B8G8R8A8:
        return RGBFB_B8G8R8A8_32;
    case RGBFB_R8G8B8A8:
        return RGBFB_R8G8B8A8_32;
    case RGBFB_Y4U2V2:
        return RGBFB_Y4U2V2_32;
    case RGBFB_Y4U1V1:
        return RGBFB_Y4U1V1_32;
    default:
        return 0;
    }
}

static uae_u16 unix_yuv_to_rgb16(uae_u8 yx, uae_u8 ux, uae_u8 vx)
{
    int y = yx - 16;
    int u = ux - 128;
    int v = vx - 128;
    int r = (298 * y + 409 * v + 128) >> (8 + 3);
    int g = (298 * y - 100 * u - 208 * v + 128) >> (8 + 3);
    int b = (298 * y + 516 * u + 128) >> (8 + 3);
    if (r < 0) {
        r = 0;
    } else if (r > 31) {
        r = 31;
    }
    if (g < 0) {
        g = 0;
    } else if (g > 31) {
        g = 31;
    }
    if (b < 0) {
        b = 0;
    } else if (b > 31) {
        b = 31;
    }
    return (r << 10) | (g << 5) | b;
}

static bool unix_picasso_colorkey_matches(const uae_u8 *screen, int dx, int screenpixbytes,
    uae_u32 colorkey)
{
    if (!screen || dx < 0 || screenpixbytes <= 0) {
        return false;
    }
    switch (screenpixbytes) {
    case 1:
        return screen[dx] == (uae_u8)colorkey;
    case 2:
        return do_get_mem_word((uae_u16 *)(screen + dx * 2)) == (uae_u16)colorkey;
    case 3:
        return screen[dx * 3 + 0] == (uae_u8)(colorkey >> 16) &&
            screen[dx * 3 + 1] == (uae_u8)(colorkey >> 8) &&
            screen[dx * 3 + 2] == (uae_u8)colorkey;
    case 4:
        return do_get_mem_long((uae_u32 *)(screen + dx * 4)) == colorkey;
    default:
        return false;
    }
}

static void unix_picasso_store_scaled_pixel(uae_u8 *dst, int dx, int dstpixbytes, uae_u32 color)
{
    switch (dstpixbytes) {
    case 1:
        dst[dx] = (uae_u8)color;
        break;
    case 2:
        do_put_mem_word((uae_u16 *)(dst + dx * 2), (uae_u16)color);
        break;
    case 3:
        dst[dx * 3 + 0] = (uae_u8)(color >> 16);
        dst[dx * 3 + 1] = (uae_u8)(color >> 8);
        dst[dx * 3 + 2] = (uae_u8)color;
        break;
    case 4:
        do_put_mem_long((uae_u32 *)(dst + dx * 4), color);
        break;
    }
}

static uae_u32 unix_picasso_convert_scaled_pixel(const uae_u8 *src, int x, int sxfrac,
    int convert_mode, const uae_u32 *rgbx16, const uae_u32 *clut, bool yuv_swap)
{
    switch (convert_mode) {
    case RGBFB_R8G8B8_32:
        return 0xff000000 | ((uae_u32)src[x * 3 + 0] << 16) |
            ((uae_u32)src[x * 3 + 1] << 8) | src[x * 3 + 2];
    case RGBFB_B8G8R8_32:
        return 0xff000000 | ((uae_u32)src[x * 3 + 2] << 16) |
            ((uae_u32)src[x * 3 + 1] << 8) | src[x * 3 + 0];
    case RGBFB_R8G8B8A8_32:
        return 0xff000000 | ((uae_u32)src[x * 4 + 0] << 16) |
            ((uae_u32)src[x * 4 + 1] << 8) | src[x * 4 + 2];
    case RGBFB_A8R8G8B8_32:
        return ((uae_u32)src[x * 4 + 0] << 24) | ((uae_u32)src[x * 4 + 1] << 16) |
            ((uae_u32)src[x * 4 + 2] << 8) | src[x * 4 + 3];
    case RGBFB_A8B8G8R8_32:
        return ((uae_u32)src[x * 4 + 0] << 24) | ((uae_u32)src[x * 4 + 3] << 16) |
            ((uae_u32)src[x * 4 + 2] << 8) | src[x * 4 + 1];
    case RGBFB_B8G8R8A8_32:
        return ((uae_u32)src[x * 4 + 3] << 24) | ((uae_u32)src[x * 4 + 2] << 16) |
            ((uae_u32)src[x * 4 + 1] << 8) | src[x * 4 + 0];
    case RGBFB_R5G6B5PC_32:
    case RGBFB_R5G5B5PC_32:
    case RGBFB_R5G6B5_32:
    case RGBFB_R5G5B5_32:
    case RGBFB_B5G6R5PC_32:
    case RGBFB_B5G5R5PC_32:
        return rgbx16 ? rgbx16[unix_picasso_load_host_u16(src + x * 2)] : 0xff000000;
    case RGBFB_CLUT_RGBFB_32:
        return clut ? clut[src[x]] : (0xff000000 | src[x] | ((uae_u32)src[x] << 8) |
            ((uae_u32)src[x] << 16));
    case RGBFB_Y4U2V2_32:
    {
        uae_u32 val = do_get_mem_long((uae_u32 *)(src + x * 4));
        if (yuv_swap) {
            val = ((val & 0xff00ff00) >> 8) | ((val & 0x00ff00ff) << 8);
        }
        uae_u8 y = (sxfrac & 0x80) ? (uae_u8)(val >> 24) : (uae_u8)(val >> 8);
        uae_u8 u = (uae_u8)val;
        uae_u8 v = (uae_u8)(val >> 16);
        uae_u16 rgb = unix_yuv_to_rgb16(y, u, v);
        return rgbx16 ? rgbx16[rgb] : 0xff000000;
    }
    case RGBFB_Y4U1V1_32:
    {
        uae_u32 val = do_get_mem_long((uae_u32 *)(src + x * 4));
        if (yuv_swap) {
            val = ((val & 0xff00ff00) >> 8) | ((val & 0x00ff00ff) << 8);
        }
        uae_u8 y[4] = {
            (uae_u8)(val >> 24), (uae_u8)(val >> 18),
            (uae_u8)(val >> 12), (uae_u8)(val >> 6)
        };
        uae_u8 u = (uae_u8)((val >> 3) & 7);
        uae_u8 v = (uae_u8)(val & 7);
        uae_u16 rgb = unix_yuv_to_rgb16(y[(sxfrac >> 6) & 3], u << 5, v << 5);
        return rgbx16 ? rgbx16[rgb] : 0xff000000;
    }
    default:
        return 0xff000000;
    }
}

void copyrow_scale(int, uae_u8 *src, uae_u8 *src_screen, uae_u8 *dst,
    int sx, int sy, int sxadd, int width, int srcbytesperrow, int,
    int screenbytesperrow, int screenpixbytes,
    int dx, int dy, int dstwidth, int dstheight, int dstbytesperrow, int dstpixbytes,
    bool ck, uae_u32 colorkey, int convert_mode, uae_u32 *p96_rgbx16p,
    uae_u32 *clut, bool yuv_swap)
{
    if (!src || !dst || sxadd <= 0 || width <= 0 || dx < 0 || dy < 0 ||
        dx >= dstwidth || dy >= dstheight || dstpixbytes <= 0) {
        return;
    }

    uae_u8 *srcrow = src + sy * srcbytesperrow;
    uae_u8 *dstrow = dst + dy * dstbytesperrow;
    uae_u8 *screenrow = src_screen ? src_screen + dy * screenbytesperrow : NULL;
    int endx = (sx + width) << 8;

    if (convert_mode == RGBFB_Y4U2V2_32) {
        endx /= 2;
        sxadd /= 2;
    } else if (convert_mode == RGBFB_Y4U1V1_32) {
        endx /= 4;
        sxadd /= 4;
    }

    while (sx < endx && dx < dstwidth) {
        int x = sx >> 8;
        if (!ck || unix_picasso_colorkey_matches(screenrow, dx, screenpixbytes, colorkey)) {
            uae_u32 color = unix_picasso_convert_scaled_pixel(srcrow, x, sx & 255,
                convert_mode, p96_rgbx16p, clut, yuv_swap);
            unix_picasso_store_scaled_pixel(dstrow, dx, dstpixbytes, color);
        }
        sx += sxadd;
        dx++;
    }
}

uae_u8 *uaegfx_getrtgbuffer(int monid, int *widthp, int *heightp, int *pitch, int *depth,
    uae_u8 *palette)
{
    if (monid < 0 || monid >= MAX_AMIGAMONITORS) {
        monid = 0;
    }
    int bankid = monid < MAX_RTG_BOARDS && gfxmem_banks[monid] ? monid : 0;
    addrbank *bank = gfxmem_banks[bankid];
    struct picasso96_state_struct *state = &picasso96_state[monid];
    struct picasso_vidbuf_description *pvidinfo = &picasso_vidinfo[monid];
    int width = state->VirtualWidth ? state->VirtualWidth : state->Width;
    int height = state->VirtualHeight ? state->VirtualHeight : state->Height;
    int srcpixbytes = unix_picasso_bytes_per_pixel((RGBFTYPE)state->RGBFormat);
    int dstpixbytes = state->BytesPerPixel == 1 && palette ? 1 : 4;

    if (!bank || !bank->baseaddr || width <= 0 || height <= 0 || srcpixbytes <= 0 ||
        state->BytesPerRow <= 0 || dstpixbytes <= 0 || (uae_u32)state->XYOffset < bank->start) {
        return NULL;
    }
    uae_u32 offset = (uae_u32)state->XYOffset - bank->start;
    uae_u32 needed = offset + (height - 1) * state->BytesPerRow + width * srcpixbytes;
    if (needed > bank->allocated_size) {
        return NULL;
    }

    uae_u8 *dst = xmalloc(uae_u8, (size_t)width * (size_t)height * (size_t)dstpixbytes);
    if (!dst) {
        return NULL;
    }
    if (dstpixbytes == 1) {
        for (int y = 0; y < height; y++) {
            memcpy(dst + (size_t)y * (size_t)width,
                bank->baseaddr + offset + (size_t)y * (size_t)state->BytesPerRow,
                (size_t)width);
        }
        for (int i = 0; i < 256; i++) {
            palette[i * 3 + 0] = state->CLUT[i].Red;
            palette[i * 3 + 1] = state->CLUT[i].Green;
            palette[i * 3 + 2] = state->CLUT[i].Blue;
        }
    } else {
        alloc_colors_picasso(8, 8, 8, 16, 8, 0, (RGBFTYPE)state->RGBFormat, p96_rgbx16);
        unix_picasso_render_pixels(bank->baseaddr + offset, width, height, state->BytesPerRow,
            srcpixbytes, (RGBFTYPE)state->RGBFormat, pvidinfo->clut, dst, width * dstpixbytes);
    }

    *widthp = width;
    *heightp = height;
    *pitch = width * dstpixbytes;
    *depth = dstpixbytes * 8;
    return dst;
}

void uaegfx_freertgbuffer(int, uae_u8 *dst)
{
    xfree(dst);
}

static void unix_picasso_render_pixels(const uae_u8 *srcbase, int width, int height,
    int srcrowbytes, int srcpixbytes, RGBFTYPE rgbfmt, const uae_u32 *clut,
    uae_u8 *dstbase, int dstrowbytes, const uae_u32 *rgbx16)
{
    for (int y = 0; y < height; y++) {
        const uae_u8 *src = srcbase + y * srcrowbytes;
        uae_u32 *dst = (uae_u32 *)(dstbase + y * dstrowbytes);
        for (int x = 0; x < width; x++) {
            dst[x] = unix_picasso_convert_pixel(src + x * srcpixbytes, rgbfmt, clut, rgbx16);
        }
    }
}

struct unix_rtg_render_job
{
    int monid;
    int width;
    int height;
    int srcrowbytes;
    int srcpixbytes;
    RGBFTYPE rgbfmt;
    uae_u32 clut[256];
    std::vector<uae_u8> src;
    std::vector<uae_u32> rgbx16;
};

struct unix_rtg_render_result
{
    int monid;
    int width;
    int height;
    std::vector<uae_u8> pixels;
};

static std::thread unix_rtg_render_thread;
static std::mutex unix_rtg_render_mutex;
static std::condition_variable unix_rtg_render_cv;
static bool unix_rtg_render_stop;
static bool unix_rtg_render_has_job;
static bool unix_rtg_render_busy;
static bool unix_rtg_render_ready;
static unix_rtg_render_job unix_rtg_pending_job;
static unix_rtg_render_result unix_rtg_ready_result;

static void unix_rtg_render_worker(void)
{
    for (;;) {
        unix_rtg_render_job job;
        {
            std::unique_lock<std::mutex> lock(unix_rtg_render_mutex);
            unix_rtg_render_cv.wait(lock, [] {
                return unix_rtg_render_stop || unix_rtg_render_has_job;
            });
            if (unix_rtg_render_stop && !unix_rtg_render_has_job) {
                return;
            }
            job = std::move(unix_rtg_pending_job);
            unix_rtg_render_has_job = false;
            unix_rtg_render_busy = true;
        }

        unix_rtg_render_result result;
        result.monid = job.monid;
        result.width = job.width;
        result.height = job.height;
        result.pixels.assign((size_t)job.width * (size_t)job.height * sizeof(uae_u32), 0);
        unix_picasso_render_pixels(job.src.data(), job.width, job.height, job.srcrowbytes,
            job.srcpixbytes, job.rgbfmt, job.clut, result.pixels.data(),
            job.width * (int)sizeof(uae_u32),
            job.rgbx16.empty() ? p96_rgbx16 : job.rgbx16.data());

        {
            std::lock_guard<std::mutex> lock(unix_rtg_render_mutex);
            unix_rtg_ready_result = std::move(result);
            unix_rtg_render_ready = true;
            unix_rtg_render_busy = false;
        }
    }
}

static bool unix_rtg_start_render_thread_locked(void)
{
    if (unix_rtg_render_thread.joinable()) {
        return true;
    }
    try {
        unix_rtg_render_stop = false;
        unix_rtg_render_thread = std::thread(unix_rtg_render_worker);
    } catch (...) {
        return false;
    }
    return true;
}

static void unix_rtg_stop_render_thread(void)
{
    {
        std::lock_guard<std::mutex> lock(unix_rtg_render_mutex);
        unix_rtg_render_stop = true;
        unix_rtg_render_has_job = false;
    }
    unix_rtg_render_cv.notify_all();
    if (unix_rtg_render_thread.joinable()) {
        unix_rtg_render_thread.join();
    }
    {
        std::lock_guard<std::mutex> lock(unix_rtg_render_mutex);
        unix_rtg_pending_job = unix_rtg_render_job();
        unix_rtg_ready_result = unix_rtg_render_result();
        unix_rtg_render_ready = false;
        unix_rtg_render_busy = false;
    }
    memset(unix_rtg_render_has_output, 0, sizeof unix_rtg_render_has_output);
}

static bool unix_rtg_collect_render_result(int monid, struct vidbuffer *vb)
{
    unix_rtg_render_result result;
    {
        std::lock_guard<std::mutex> lock(unix_rtg_render_mutex);
        if (!unix_rtg_render_ready || unix_rtg_ready_result.monid != monid) {
            return false;
        }
        result = std::move(unix_rtg_ready_result);
        unix_rtg_render_ready = false;
    }
    if (!vb || !vb->bufmem || result.width != vb->outwidth || result.height != vb->outheight) {
        return false;
    }
    for (int y = 0; y < result.height; y++) {
        memcpy(vb->bufmem + y * vb->rowbytes,
            result.pixels.data() + (size_t)y * (size_t)result.width * sizeof(uae_u32),
            (size_t)result.width * sizeof(uae_u32));
    }
    unix_rtg_overlay_sprite(monid, (uae_u32 *)vb->bufmem, result.width, result.height,
        vb->rowbytes / sizeof(uae_u32));
    unix_rtg_render_has_output[monid] = true;
    return true;
}

static bool unix_rtg_submit_render_job(int monid, const uae_u8 *srcbase, int width, int height,
    int srcrowbytes, int srcpixbytes, RGBFTYPE rgbfmt, const uae_u32 *clut)
{
    if (!srcbase || width <= 0 || height <= 0 || srcrowbytes <= 0 || srcpixbytes <= 0) {
        return false;
    }

    unix_rtg_render_job job;
    job.monid = monid;
    job.width = width;
    job.height = height;
    job.srcrowbytes = width * srcpixbytes;
    job.srcpixbytes = srcpixbytes;
    job.rgbfmt = rgbfmt;
    memcpy(job.clut, clut, sizeof job.clut);
    if (srcpixbytes == 2) {
        job.rgbx16.assign(p96_rgbx16, p96_rgbx16 + 65536);
    }
    job.src.assign((size_t)job.srcrowbytes * (size_t)height, 0);
    for (int y = 0; y < height; y++) {
        memcpy(job.src.data() + (size_t)y * (size_t)job.srcrowbytes,
            srcbase + (size_t)y * (size_t)srcrowbytes, (size_t)job.srcrowbytes);
    }

    {
        std::lock_guard<std::mutex> lock(unix_rtg_render_mutex);
        if (!unix_rtg_start_render_thread_locked() || unix_rtg_render_has_job ||
            unix_rtg_render_busy || unix_rtg_render_ready) {
            return false;
        }
        unix_rtg_pending_job = std::move(job);
        unix_rtg_render_has_job = true;
    }
    unix_rtg_render_cv.notify_one();
    return true;
}

static void unix_picasso_render(int monid)
{
    if (monid < 0 || monid >= MAX_AMIGAMONITORS || !unix_picasso_ensure_buffer(monid)) {
        return;
    }

    struct picasso96_state_struct *state = &picasso96_state[monid];
    struct picasso_vidbuf_description *pvidinfo = &picasso_vidinfo[monid];
    struct vidbuffer *vb = &adisplays[monid].gfxvidinfo.drawbuffer;
    addrbank *bank = gfxmem_banks[0];
    int srcpixbytes = unix_picasso_bytes_per_pixel((RGBFTYPE)state->RGBFormat);

    if (!bank || !bank->baseaddr || !srcpixbytes || !state->Address || !state->BytesPerRow ||
        !state->Width || !state->Height) {
        return;
    }
    if ((uae_u32)state->XYOffset < bank->start) {
        return;
    }

    uae_u32 offset = (uae_u32)state->XYOffset - bank->start;
    uae_u32 needed = offset + (state->Height - 1) * state->BytesPerRow + state->Width * srcpixbytes;
    if (needed > bank->allocated_size) {
        return;
    }

    alloc_colors_picasso(8, 8, 8, 16, 8, 0, (RGBFTYPE)state->RGBFormat, p96_rgbx16);
    const uae_u8 *srcbase = bank->baseaddr + offset;
    if (currprefs.rtg_multithread) {
        bool collected = unix_rtg_collect_render_result(monid, vb);
        bool submitted = unix_rtg_submit_render_job(monid, srcbase, state->Width, state->Height,
            state->BytesPerRow, srcpixbytes, (RGBFTYPE)state->RGBFormat, pvidinfo->clut);
        if (collected || submitted) {
            if (unix_rtg_render_has_output[monid]) {
                return;
            }
        }
    }
    unix_picasso_render_pixels(srcbase, state->Width, state->Height, state->BytesPerRow,
        srcpixbytes, (RGBFTYPE)state->RGBFormat, pvidinfo->clut, vb->bufmem, vb->rowbytes);
    unix_rtg_overlay_sprite(monid, (uae_u32 *)vb->bufmem, state->Width, state->Height, vb->rowbytes / sizeof(uae_u32));
    unix_rtg_render_has_output[monid] = true;
}

void picasso_enablescreen(int monid, int on)
{
    if (monid < 0 || monid >= MAX_AMIGAMONITORS) {
        return;
    }
    picasso_vidinfo[monid].picasso_active = on != 0;
    unix_apply_video_mode_from_prefs(&currprefs, monid);
    if (on) {
        picasso_refresh(monid);
    }
}

void picasso_refresh(int monid)
{
    if (monid < 0 || monid >= MAX_AMIGAMONITORS) {
        return;
    }
    if (currprefs.rtgboards[0].rtgmem_type >= GFXBOARD_HARDWARE) {
        gfxboard_refresh(monid);
        return;
    }
    unix_picasso_render(monid);
    if (adisplays[monid].picasso_on || picasso_vidinfo[monid].picasso_active) {
#ifdef AVIOUTPUT
        frame_drawn(monid);
#endif
        show_screen(monid, 0);
    }
}
void init_hz_p96(int) {}

void gfx_set_picasso_modeinfo(int monid, RGBFTYPE rgbfmt)
{
    if (monid < 0 || monid >= MAX_AMIGAMONITORS) {
        return;
    }
    struct picasso96_state_struct *state = &picasso96_state[monid];
    picasso_vidinfo[monid].rgbformat = rgbfmt;
    picasso_vidinfo[monid].selected_rgbformat = rgbfmt;
    picasso_vidinfo[monid].depth = state->GC_Depth ? (state->GC_Depth + 7) / 8 : 0;
    picasso_vidinfo[monid].picasso_changed = true;
    unix_picasso_ensure_buffer(monid);
}

void gfx_set_picasso_colors(int monid, RGBFTYPE rgbfmt)
{
    if (monid >= 0 && monid < MAX_AMIGAMONITORS) {
        picasso_vidinfo[monid].rgbformat = rgbfmt;
    }
}

void gfx_set_picasso_state(int monid, int on)
{
    if (monid >= 0 && monid < MAX_AMIGAMONITORS) {
        picasso_vidinfo[monid].picasso_active = on != 0;
    }
}

uae_u8 *gfx_lock_picasso(int monid, bool)
{
    if (monid < 0 || monid >= MAX_AMIGAMONITORS || !unix_picasso_ensure_buffer(monid)) {
        return NULL;
    }
    struct vidbuffer *vb = &adisplays[monid].gfxvidinfo.drawbuffer;
    vb->locked = true;
    return vb->bufmem;
}

void gfx_unlock_picasso(int monid, bool dorender)
{
    if (monid < 0 || monid >= MAX_AMIGAMONITORS) {
        return;
    }
    struct vidbuffer *vb = &adisplays[monid].gfxvidinfo.drawbuffer;
    vb->locked = false;
    if (dorender) {
        show_screen(monid, 0);
    }
}
void picasso_invalidate(int, int, int, int, int) {}

void picasso_handle_vsync(void)
{
    for (int monid = 0; monid < MAX_AMIGAMONITORS; monid++) {
        struct amigadisplay *ad = &adisplays[monid];
        struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[monid];
        int state = vidinfo->picasso_state_change;
        bool uaegfx = currprefs.rtgboards[0].rtgmem_type < GFXBOARD_HARDWARE;

        if (state) {
            atomic_and(&vidinfo->picasso_state_change, ~state);
            if (state & UNIX_PICASSO_STATE_SETGC) {
                gfx_set_picasso_modeinfo(monid, (RGBFTYPE)picasso96_state[monid].RGBFormat);
            }
            if (state & UNIX_PICASSO_STATE_SETSWITCH) {
                vidinfo->picasso_active = ad->picasso_requested_on;
            }
            if (state & (UNIX_PICASSO_STATE_SETGC | UNIX_PICASSO_STATE_SETPANNING |
                UNIX_PICASSO_STATE_SETDAC | UNIX_PICASSO_STATE_SETDISPLAY)) {
                vidinfo->full_refresh = 1;
            }
        }
        if (!uaegfx) {
            continue;
        }

        if (ad->picasso_on || vidinfo->picasso_active) {
            picasso_trigger_vblank();
            picasso_refresh(monid);
        }
    }
    gfxboard_vsync_handler(false, true);
}

void fb_copyrow(int monid, uae_u8 *src, uae_u8 *dst, int x, int, int width, int srcpixbytes, int dy)
{
    if (!src || !dst || monid < 0 || monid >= MAX_AMIGAMONITORS) {
        return;
    }
    int rowbytes = picasso_vidinfo[monid].rowbytes;
    int pixbytes = picasso_vidinfo[monid].pixbytes ? picasso_vidinfo[monid].pixbytes : srcpixbytes;
    if (rowbytes <= 0 || pixbytes <= 0 || width <= 0) {
        return;
    }
    memcpy(dst + dy * rowbytes + x * pixbytes, src, (size_t)width * (size_t)srcpixbytes);
}
