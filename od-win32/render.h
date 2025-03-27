#ifndef __DXWRAP_H__
#define __DXWRAP_H__

#include "rtgmodes.h"
#include <d3d9.h>
#include <D3dkmthk.h>

#define MAX_DISPLAYS 10

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
	bool inuse;
	struct ScreenResolution res;
	int residx;
	int refresh[MAX_REFRESH_RATES]; /* refresh-rates in Hz */
	int refreshtype[MAX_REFRESH_RATES]; /* 0=normal,1=raw,2=lace */
	TCHAR name[25];
	int rawmode;
	bool lace; // all modes lace
};

struct MultiDisplay {
	bool primary;
	GUID ddguid;
	HMONITOR monitor;
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
	int native_width, native_height;
	int current_width, current_height;
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
	int window_extra_height_bar;
	int win_x_diff, win_y_diff;
	int setcursoroffset_x, setcursoroffset_y;
	int mouseposx, mouseposy;
	int windowmouse_max_w;
	int windowmouse_max_h;
	int ratio_width, ratio_height;
	int ratio_adjust_x, ratio_adjust_y;
	bool ratio_sizing;
	bool render_ok, wait_render;
	int dpi;

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
};
extern struct AmigaMonitor *amon;
extern struct AmigaMonitor AMonitors[MAX_AMIGAMONITORS];

typedef enum
{
	red_mask,
	green_mask,
	blue_mask
} DirectDraw_Mask_e;

extern const TCHAR *DXError(HRESULT hr);
extern TCHAR *outGUID (const GUID *guid);

#endif
