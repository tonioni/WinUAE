
#include <windows.h>
#include "resource.h"

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "xwin.h"
#include "custom.h"
#include "drawing.h"
#include "render.h"
#include "win32.h"
#include "win32gfx.h"
#include "statusline.h"
#include "uae.h"
#include "direct3d.h"

struct gdibm
{
	bool active;
	int x, y;
	int width, height, depth;
	HDC thdc;
	HBITMAP hbm;
	HGDIOBJ oldbm;
	void *bits;
	int pitch;
};

struct gdistruct
{
	int enabled;
	int num;
	int width, height, depth;
	int wwidth, wheight;
	HWND hwnd;
	HDC hdc;
	HDC thdc;
	HBITMAP hbm;
	HGDIOBJ oldbm;
	void *bits;
	int pitch;
	int statusbar_hx, statusbar_vx;
	int ledwidth, ledheight;
	struct gdibm osd;
};

static struct gdistruct gdidata[MAX_AMIGAMONITORS];

static void gdi_refresh(int monid)
{
	struct gdistruct *gdi = &gdidata[monid];
}

static void gdi_restore(int monid, bool checkonly)
{
	struct gdistruct *gdi = &gdidata[monid];
}

static void freetexture(int monid)
{
	struct gdistruct *gdi = &gdidata[monid];
	if (gdi->hdc) {
		if (gdi->thdc) {
			if (gdi->hbm) {
				if (gdi->oldbm) {
					SelectObject(gdi->thdc, gdi->oldbm);
				}
				DeleteObject(gdi->hbm);
			}
			gdi->oldbm = NULL;
			gdi->hbm = NULL;
			gdi->bits = NULL;
			DeleteDC(gdi->thdc);
			gdi->thdc = NULL;
		}
		ReleaseDC(gdi->hwnd, gdi->hdc);
		gdi->hdc = NULL;
	}
}

static void freesprite(struct gdistruct *gdi, struct gdibm *bm)
{
	if (bm->thdc) {
		if (bm->hbm) {
			if (bm->oldbm) {
				SelectObject(bm->thdc, bm->oldbm);
			}
			DeleteObject(bm->hbm);
		}
		bm->oldbm = NULL;
		bm->hbm = NULL;
		bm->bits = NULL;
		DeleteDC(bm->thdc);
		bm->thdc = NULL;
	}
}

static bool allocsprite(struct gdistruct *gdi, struct gdibm *bm, int w, int h)
{
	bm->thdc = CreateCompatibleDC(gdi->hdc);
	if (bm->thdc) {
		BITMAPV4HEADER bmi = { 0 };

		bmi.bV4Size = sizeof(BITMAPINFOHEADER);
		bmi.bV4Width = w;
		bmi.bV4Height = -h;
		bmi.bV4Planes = 1;
		bmi.bV4V4Compression = BI_RGB;
		bmi.bV4BitCount = gdi->depth;
		bm->width = w;
		bm->height = h;
		bm->depth = gdi->depth;
		bm->pitch = ((w * bmi.bV4BitCount + 31) / 32) * 4;
		bmi.bV4SizeImage = gdi->pitch * h;
		bm->hbm = CreateDIBSection(gdi->hdc, (const BITMAPINFO*)&bmi, DIB_RGB_COLORS, &bm->bits, NULL, 0);
		if (bm->hbm) {
			bm->oldbm = SelectObject(bm->thdc, bm->hbm);
			return true;
		}
	}
	return false;
}

static bool gdi_alloctexture(int monid, int w, int h)
{
	struct gdistruct *gdi = &gdidata[monid];

	freetexture(monid);

	gdi->hdc = GetDC(gdi->hwnd);
	if (gdi->hdc) {
		gdi->thdc = CreateCompatibleDC(gdi->hdc);
		if (gdi->thdc) {
			BITMAPV4HEADER bmi = { 0 };

			bmi.bV4Size = sizeof(BITMAPINFOHEADER);
			bmi.bV4Width = w;
			bmi.bV4Height = -h;
			bmi.bV4Planes = 1;
			bmi.bV4V4Compression = BI_RGB;
			bmi.bV4BitCount = gdi->depth;
			gdi->width = w;
			gdi->height = h;
			gdi->pitch = ((w * bmi.bV4BitCount + 31) / 32) * 4;
			bmi.bV4SizeImage = gdi->pitch * h;
			gdi->hbm = CreateDIBSection(gdi->hdc, (const BITMAPINFO*)&bmi, DIB_RGB_COLORS, &gdi->bits, NULL, 0);
			if (gdi->hbm) {
				gdi->oldbm = SelectObject(gdi->thdc, gdi->hbm);
				return true;
			}
		}
	}
	freetexture(monid);
	return false;
}

static void updateleds(struct gdistruct *gdi)
{
	static uae_u32 rc[256], gc[256], bc[256], a[256];
	static int done;
	int osdx, osdy;

	if (!done) {
		for (int i = 0; i < 256; i++) {
			rc[i] = i << 16;
			gc[i] = i << 8;
			bc[i] = i << 0;
			a[i] = i << 24;
		}
		done = 1;
	}


	if (gdi->osd.bits == NULL || gdi != gdidata)
		return;

	statusline_getpos(gdi->num, &osdx, &osdy, gdi->wwidth, gdi->wheight);
	gdi->osd.x = osdx;
	gdi->osd.y = osdy;

	for (int y = 0; y < gdi->osd.height; y++) {
		uae_u8 *buf = (uae_u8*)gdi->osd.bits + y * gdi->osd.pitch;
		statusline_single_erase(gdi->num, buf, gdi->osd.depth / 8, y, gdi->ledwidth);
	}
	statusline_render(gdi->num, (uae_u8*)gdi->osd.bits, gdi->osd.depth / 8, gdi->osd.pitch, gdi->ledwidth, gdi->ledheight, rc, gc, bc, a);

	for (int y = 0; y < gdi->osd.height; y++) {
		uae_u8 *buf = (uae_u8*)gdi->osd.bits + y * gdi->osd.pitch;
		draw_status_line_single(gdi->num, buf, gdi->osd.depth / 8, y, gdi->ledwidth, rc, gc, bc, a);
	}
}

static void gdi_guimode(int monid, int guion)
{
}

static uae_u8 *gdi_locktexture(int monid, int *pitch, int *height, int fullupdate)
{
	struct gdistruct *gdi = &gdidata[monid];
	if (gdi->bits) {
		*pitch = gdi->pitch;
		if (height)
			*height = gdi->height;
		return (uae_u8*)gdi->bits;
	}
	return NULL;
}

static void gdi_unlocktexture(int monid, int y_start, int y_end)
{
	struct gdistruct *gdi = &gdidata[monid];

	struct AmigaMonitor *mon = &AMonitors[monid];
	bool rtg = WIN32GFX_IsPicassoScreen(mon);
	if (((currprefs.leds_on_screen & STATUSLINE_CHIPSET) && !rtg) || ((currprefs.leds_on_screen & STATUSLINE_RTG) && rtg)) {
		updateleds(gdi);
		gdi->osd.active = true;
	} else {
		gdi->osd.active = false;
	}
}

static void gdi_flushtexture(int monid, int miny, int maxy)
{
}

static bool gdi_renderframe(int monid, int mode, bool immediate)
{
	struct gdistruct *gdi = &gdidata[monid];

	return gdi->hbm != NULL;
}

static void gdi_showframe(int monid)
{
	struct gdistruct *gdi = &gdidata[monid];

	if (gdi->hbm) {
		StretchBlt(gdi->hdc, 0, 0, gdi->wwidth, gdi->wheight, gdi->thdc, 0, 0, gdi->width, gdi->height, SRCCOPY);
	}
	if (gdi->osd.active && gdi->osd.hbm) {
		TransparentBlt(gdi->hdc, gdi->osd.x, gdi->osd.y, gdi->ledwidth, gdi->ledheight, gdi->osd.thdc, 0, 0, gdi->ledwidth, gdi->ledheight, 0x000000);
	}
}

void gdi_free(int monid, bool immediate)
{
	struct gdistruct *gdi = &gdidata[monid];
	gdi->enabled = 0;
	freetexture(monid);
}

static const TCHAR *gdi_init(HWND ahwnd, int monid, int w_w, int w_h, int depth, int *freq, int mmulth, int mmultv)
{
	struct gdistruct *gdi = &gdidata[monid];

	if (isfullscreen() > 0) {
		return _T("GDI fullscreen not supported");
	}

	gdi->hwnd = ahwnd;
	gdi->depth = depth;
	gdi->wwidth = w_w;
	gdi->wheight = w_h;

	gdi->statusbar_hx = gdi->statusbar_vx = statusline_set_multiplier(monid, gdi->wwidth, gdi->wheight) / 100;
	gdi->ledwidth = gdi->wwidth;
	gdi->ledheight = TD_TOTAL_HEIGHT * gdi->statusbar_vx;
	allocsprite(gdi, &gdi->osd, gdi->ledwidth, gdi->ledheight);

	gdi->enabled = 1;

	return NULL;
}

static HDC gdi_getDC(int monid, HDC hdc)
{
	struct gdistruct *gdi = &gdidata[monid];

	if (!hdc) {
		return gdi->hdc;
	}
	return NULL;
}

static int gdi_isenabled(int monid)
{
	struct gdistruct *gdi = &gdidata[monid];
	return gdi->enabled ? -1 : 0;
}

static void gdi_clear(int monid)
{
	struct gdistruct *gdi = &gdidata[monid];
}

void gdi_select(void)
{
	for (int i = 0; i < MAX_AMIGAMONITORS; i++) {
		gdidata[i].num = i;
	}

	D3D_free = gdi_free;
	D3D_init = gdi_init;

	D3D_renderframe = gdi_renderframe;
	D3D_alloctexture = gdi_alloctexture;
	D3D_refresh = gdi_refresh;
	D3D_restore = gdi_restore;

	D3D_locktexture = gdi_locktexture;
	D3D_unlocktexture = gdi_unlocktexture;
	D3D_flushtexture = gdi_flushtexture;

	D3D_showframe = gdi_showframe;
	D3D_showframe_special = NULL;
	D3D_guimode = gdi_guimode;
	D3D_getDC = gdi_getDC;
	D3D_isenabled = gdi_isenabled;
	D3D_clear = gdi_clear;
	D3D_canshaders = NULL;
	D3D_goodenough = NULL;
	D3D_setcursor = NULL;
	D3D_setcursorsurface = NULL;
	D3D_getrefreshrate = NULL;
	D3D_resize = NULL;
	D3D_change = NULL;
	D3D_getscalerect = NULL;
	D3D_run = NULL;
	D3D_debug = NULL;
	D3D_led = NULL;
	D3D_getscanline = NULL;
	D3D_extoverlay = NULL;
}
