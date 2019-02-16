#ifndef __DXWRAP_H__
#define __DXWRAP_H__

#include "rtgmodes.h"
#include <ddraw.h>
#include <d3d9.h>
#include <D3dkmthk.h>

#define MAX_DISPLAYS 10

extern int ddforceram;
extern int useoverlay;

struct ddstuff
{
	int ddinit;
	int ddzeroguid;
	GUID ddguid;
	LPDIRECTDRAW7 maindd;
	LPDIRECTDRAWCLIPPER dclip;
	LPDIRECTDRAWSURFACE7 primary, secondary, flipping[2];
	DDOVERLAYFX overlayfx;
	DWORD overlayflags;
	int fsmodeset, backbuffers;
	int width, height, depth, freq;
	int vblank_skip, vblank_skip_cnt;
	int swidth, sheight;
	DDSURFACEDESC2 native;
	DDSURFACEDESC2 locksurface;
	int lockcnt;
	DWORD pitch;
	HWND hwnd;
	uae_u32 colorkey;
	int islost, isoverlay;

	LPDIRECTDRAWSURFACE7 statussurface;
};
struct ddcaps
{
	int maxwidth, maxheight;
	int cancolorkey;
	int cannonlocalvidmem;
};
extern struct ddstuff dxdata;
extern struct ddcaps dxcaps;

struct ScreenResolution
{
	uae_u32 width;  /* in pixels */
	uae_u32 height; /* in pixels */
};

#define MAX_PICASSO_MODES 300
#define MAX_REFRESH_RATES 100

#define REFRESH_RATE_RAW 1
#define REFRESH_RATE_LACE 2

struct PicassoResolution
{
	struct ScreenResolution res;
	int depth;   /* depth in bytes-per-pixel */
	int residx;
	int refresh[MAX_REFRESH_RATES]; /* refresh-rates in Hz */
	int refreshtype[MAX_REFRESH_RATES]; /* 0=normal,1=raw,2=lace */
	TCHAR name[25];
	/* Bit mask of RGBFF_xxx values.  */
	uae_u32 colormodes;
	int rawmode;
	bool lace; // all modes lace
};

struct MultiDisplay {
	bool primary;
	GUID ddguid;
	TCHAR *adaptername, *adapterid, *adapterkey;
	TCHAR *monitorname, *monitorid;
	TCHAR *fullname;
	struct PicassoResolution *DisplayModes;
	RECT rect;
	RECT workrect;
	LUID AdapterLuid;
	UINT VidPnSourceId;
	UINT AdapterHandle;
	bool HasAdapterData;
};
extern struct MultiDisplay Displays[MAX_DISPLAYS + 1];

extern int amigamonid;

struct winuae_currentmode {
	unsigned int flags;
	int native_width, native_height, native_depth, pitch;
	int current_width, current_height, current_depth;
	int amiga_width, amiga_height;
	int initdone;
	int fullfill;
	int vsync;
	int freq;
};

#define MAX_AMIGAMONITORS 4
struct AmigaMonitor {
	int monitor_id;
	HWND hAmigaWnd;
	HWND hMainWnd;
	struct MultiDisplay *md;

	RECT amigawin_rect, mainwin_rect;
	RECT amigawinclip_rect;
	int window_extra_width, window_extra_height;
	int win_x_diff, win_y_diff;
	int setcursoroffset_x, setcursoroffset_y;
	int mouseposx, mouseposy;
	int windowmouse_max_w;
	int windowmouse_max_h;
	int ratio_width, ratio_height;
	int ratio_adjust_x, ratio_adjust_y;
	bool ratio_sizing;
	int prevsbheight;
	bool render_ok, wait_render;

	int in_sizemove;
	int manual_painting_needed;
	int minimized;
	int screen_is_picasso;
	int screen_is_initialized;
	int scalepicasso;
	bool rtg_locked;
	int p96_double_buffer_firstx, p96_double_buffer_lastx;
	int p96_double_buffer_first, p96_double_buffer_last;
	int p96_double_buffer_needs_flushing;

	HWND hStatusWnd;
	HBRUSH hStatusBkgB;

	struct winuae_currentmode currentmode;
	struct uae_filter *usedfilter;
};
extern struct AmigaMonitor *amon;
extern struct AmigaMonitor AMonitors[MAX_AMIGAMONITORS];

typedef enum
{
	red_mask,
	green_mask,
	blue_mask
} DirectDraw_Mask_e;

extern const TCHAR *DXError (HRESULT hr);
extern TCHAR *outGUID (const GUID *guid);

HRESULT DirectDraw_GetDisplayMode (void);
void DirectDraw_Release(void);
int DirectDraw_Start(void);
void DirectDraw_get_GUIDs (void);
void DirectDraw_ClearSurface (LPDIRECTDRAWSURFACE7 surf);
int DirectDraw_LockSurface (LPDIRECTDRAWSURFACE7 surf, LPDDSURFACEDESC2 desc);
void DirectDraw_UnlockSurface (LPDIRECTDRAWSURFACE7 surf);
LPDIRECTDRAWSURFACE7 allocsurface (int width, int height);
LPDIRECTDRAWSURFACE7 allocsystemsurface (int width, int height);
LPDIRECTDRAWSURFACE7 createsurface (uae_u8 *ptr, int pitch, int width, int height);
void freesurface (LPDIRECTDRAWSURFACE7 surf);
void DirectDraw_FreeMainSurface (void);
HRESULT DirectDraw_CreateMainSurface (int width, int height);
HRESULT DirectDraw_SetDisplayMode(int width, int height, int bits, int freq);
HRESULT DirectDraw_SetCooperativeLevel (HWND window, int fullscreen, int doset);
HRESULT DirectDraw_CreateClipper (void);
HRESULT DirectDraw_SetClipper(HWND hWnd);
RGBFTYPE DirectDraw_GetSurfacePixelFormat(LPDDSURFACEDESC2 surface);
DWORD DirectDraw_CurrentWidth (void);
DWORD DirectDraw_CurrentHeight (void);
DWORD DirectDraw_GetCurrentDepth (void);
int DirectDraw_SurfaceLock (void);
void DirectDraw_SurfaceUnlock (void);
uae_u8 *DirectDraw_GetSurfacePointer (void);
DWORD DirectDraw_GetSurfacePitch (void);
int DirectDraw_IsLocked (void);
DWORD DirectDraw_GetPixelFormatBitMask (DirectDraw_Mask_e mask);
RGBFTYPE DirectDraw_GetPixelFormat (void);
DWORD DirectDraw_GetBytesPerPixel (void);
HRESULT DirectDraw_GetDC (HDC *hdc);
HRESULT DirectDraw_ReleaseDC (HDC hdc);
int DirectDraw_GetVerticalBlankStatus (void);
DWORD DirectDraw_CurrentRefreshRate (void);
void DirectDraw_GetPrimaryPixelFormat (DDSURFACEDESC2 *desc);
HRESULT DirectDraw_FlipToGDISurface (void);
int DirectDraw_Flip (int doflip);
int DirectDraw_BlitToPrimary (RECT *rect);
int DirectDraw_BlitToPrimaryScale (RECT *dstrect, RECT *srcrect);
int DirectDraw_Blit (LPDIRECTDRAWSURFACE7 dst, LPDIRECTDRAWSURFACE7 src);
int DirectDraw_BlitRect (LPDIRECTDRAWSURFACE7 dst, RECT *dstrect, LPDIRECTDRAWSURFACE7 src, RECT *scrrect);
void DirectDraw_Fill (RECT *rect, uae_u32 color);
void DirectDraw_FillPrimary (void);
bool DD_getvblankpos (int *vpos);
void DD_vblank_reset (double freq);

void dx_check (void);
int dx_islost (void);

#define DDFORCED_NONLOCAL 0
#define DDFORCED_DEFAULT 1
#define DDFORCED_VIDMEM 2
#define DDFORCED_SYSMEM 3

#endif
