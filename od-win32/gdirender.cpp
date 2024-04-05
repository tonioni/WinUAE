
/* GDI graphics renderer */

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
#include "gfxfilter.h"

struct gdibm
{
	bool active;
	int x, y;
	int width, height, depth;
	int maxw, maxh;
	HDC thdc;
	HBITMAP hbm;
	HGDIOBJ oldbm;
	void *bits;
	int pitch;
};

struct gdioverlay
{
	struct gdioverlay *next;
	int id;
	int x, y;
	struct gdibm tex;
};

struct gdistruct
{
	int enabled;
	int num;
	int wwidth, wheight;
	int depth;
	HWND hwnd;
	HDC hdc;
	HGDIOBJ oldbm;
	int statusbar_hx, statusbar_vx;
	int ledwidth, ledheight;
	struct gdibm bm;
	struct gdibm buf;
	struct gdibm osd;
	struct gdibm cursor;
	struct gdioverlay *extoverlays;

	float cursor_x, cursor_y;
	float cursor_mx, cursor_my;
	bool cursor_v, cursor_scale;

	RECT sr2, dr2, zr2;
	int dmult, dmultxh, dmultxv, dmode;
	int xoffset, yoffset;
	float xmult, ymult;
	int cursor_offset_x, cursor_offset_y;

	int bmxoffset, bmyoffset;
	int bmwidth, bmheight;
	bool refreshneeded;
	bool eraseneeded;
};

static struct gdistruct gdidata[MAX_AMIGAMONITORS];

static void gdi_restore(int monid, bool checkonly)
{
	struct gdistruct *gdi = &gdidata[monid];
}

static void setupscenecoords(struct gdistruct *gdi, int monid)
{
	RECT sr, dr, zr;
	static RECT sr2[MAX_AMIGAMONITORS], dr2[MAX_AMIGAMONITORS], zr2[MAX_AMIGAMONITORS];

	getfilterrect2(gdi->num, &dr, &sr, &zr, gdi->wwidth, gdi->wheight, gdi->bm.width / gdi->dmult, gdi->bm.height / gdi->dmult, gdi->dmult, &gdi->dmode, gdi->bm.width, gdi->bm.height);

	if (memcmp(&sr, &sr2[monid], sizeof RECT) || memcmp(&dr, &dr2[monid], sizeof RECT) || memcmp(&zr, &zr2[monid], sizeof RECT)) {
		write_log(_T("POS (%d %d %d %d) - (%d %d %d %d)[%d,%d] (%d %d) S=%d*%d B=%d*%d\n"),
			dr.left, dr.top, dr.right, dr.bottom,
			sr.left, sr.top, sr.right, sr.bottom,
			sr.right - sr.left, sr.bottom - sr.top,
			zr.left, zr.top,
			gdi->wwidth, gdi->wheight,
			gdi->bm.width, gdi->bm.height);
		sr2[monid] = sr;
		dr2[monid] = dr;
		zr2[monid] = zr;
	}

	gdi->sr2 = sr;
	gdi->dr2 = dr;
	gdi->zr2 = zr;

	float dw = (float)dr.right - dr.left;
	float dh = (float)dr.bottom - dr.top;
	float w = (float)sr.right - sr.left;
	float h = (float)sr.bottom - sr.top;

	int tx = ((dr.right - dr.left) * gdi->bm.width) / (gdi->wwidth * 2);
	int ty = ((dr.bottom - dr.top) * gdi->bm.height) / (gdi->wheight * 2);

	float sw = dw / gdi->wwidth;
	float sh = dh / gdi->wheight;

	int xshift = -zr.left - sr.left;
	int yshift = -zr.top - sr.top;

	xshift -= ((sr.right - sr.left) - gdi->wwidth) / 2;
	yshift -= ((sr.bottom - sr.top) - gdi->wheight) / 2;

	gdi->xoffset = tx + xshift - gdi->wwidth / 2;
	gdi->yoffset = ty + yshift - gdi->wheight / 2;

	gdi->xmult = filterrectmult(gdi->wwidth, w, gdi->dmode);
	gdi->ymult = filterrectmult(gdi->wheight, h, gdi->dmode);

	gdi->cursor_offset_x = -zr.left;
	gdi->cursor_offset_y = -zr.top;

	sw *= gdi->wwidth;
	sh *= gdi->wheight;

	gdi->bmwidth = (int)(gdi->bm.width * gdi->xmult);
	gdi->bmheight = (int)(gdi->bm.height * gdi->ymult);

	int positionX, positionY;
	int bw2 = gdi->bm.width;
	int bh2 = gdi->bm.height;
	int sw2 = gdi->wwidth;
	int sh2 = gdi->wheight;

	positionX = (sw2 - bw2) / 2 + gdi->xoffset;
	positionY = (sh2 - bh2) / 2 + gdi->yoffset;

	float left = sw2 / -2.0f;
	left += positionX;

	float top = sh2 / -2.0f;
	top += positionY;

	left *= gdi->xmult;
	top *= gdi->ymult;

	left += gdi->wwidth / 2.0f;
	top += gdi->wheight / 2.0f;

	gdi->bmxoffset = (int)left;
	gdi->bmyoffset = (int)top;

	gdi->eraseneeded = true;
}

static void gdi_clear(int monid)
{
	struct gdistruct *gdi = &gdidata[monid];

	if (gdi->hdc) {
		Rectangle(gdi->hdc, 0, 0, gdi->wwidth, gdi->wheight);
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
	bm->active = false;
}

static void freetexture(int monid)
{
	struct gdistruct *gdi = &gdidata[monid];
	freesprite(gdi, &gdi->bm);
	if (gdi->hdc) {
		ReleaseDC(gdi->hwnd, gdi->hdc);
		gdi->hdc = NULL;
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
		bmi.bV4SizeImage = bm->pitch * h;
		bm->hbm = CreateDIBSection(gdi->hdc, (const BITMAPINFO*)&bmi, DIB_RGB_COLORS, &bm->bits, NULL, 0);
		if (bm->hbm) {
			bm->oldbm = SelectObject(bm->thdc, bm->hbm);
			SelectObject(bm->thdc, GetStockObject(DC_PEN));
			SelectObject(bm->thdc, GetStockObject(DC_BRUSH));
			SetDCPenColor(bm->thdc, RGB(0, 0, 0));
			SetDCBrushColor(bm->thdc, RGB(0, 0, 0));
			return true;
		}
	}
	return false;
}

static bool gdi_alloctexture(int monid, int w, int h)
{
	struct gdistruct *gdi = &gdidata[monid];

	if (w < 0 || h < 0) { 
		if (gdi->bm.width == -w && gdi->bm.height == -h && gdi->hdc) {
			return true;
		}
		return false;
	}

	freetexture(monid);

	gdi->hdc = GetDC(gdi->hwnd);
	if (gdi->hdc) {
		SelectObject(gdi->hdc, GetStockObject(DC_PEN));
		SelectObject(gdi->hdc, GetStockObject(DC_BRUSH));
		SetDCPenColor(gdi->hdc, RGB(0, 0, 0));
		SetDCBrushColor(gdi->hdc, RGB(0, 0, 0));
		if (allocsprite(gdi, &gdi->bm, w, h)) {
			gdi->dmult = S2X_getmult(monid);
			setupscenecoords(gdi, monid);
			return true;
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

static uae_u8 *gdi_locktexture(int monid, int *pitch, int *width, int *height, int fullupdate)
{
	struct gdistruct *gdi = &gdidata[monid];
	if (gdi->bm.bits) {
		*pitch = gdi->bm.pitch;
		if (height)
			*height = gdi->bm.height;
		if (width)
			*width = gdi->bm.width;
		return (uae_u8*)gdi->bm.bits;
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

	if (gdi->bm.hbm) {
		setupscenecoords(gdi, monid);
	}
	return gdi->bm.hbm != NULL;
}

static void gdi_paint(void)
{
	for (int monid = 0; monid < MAX_AMIGAMONITORS; monid++) {
		struct gdistruct *gdi = &gdidata[monid];
		if (!gdi->refreshneeded) {
			continue;
		}
		gdi->refreshneeded = false;
		
		if (gdi->eraseneeded) {
			Rectangle(gdi->buf.thdc, 0, 0, gdi->buf.width, gdi->buf.height);
			gdi->eraseneeded = false;
		}

		if (gdi->bm.hbm) {
			StretchBlt(gdi->buf.thdc, gdi->bmxoffset, gdi->bmyoffset, gdi->bmwidth, gdi->bmheight, gdi->bm.thdc, 0, 0, gdi->bm.width, gdi->bm.height, SRCCOPY);
		}
		if (gdi->cursor.active && gdi->cursor.hbm) {
			int bx = gdi->bmxoffset;
			int by = gdi->bmyoffset;
			int x = gdi->cursor.x;
			int y = gdi->cursor.y;
			int cx = 0;
			int cy = 0;
			int cw = CURSORMAXWIDTH;
			int ch = CURSORMAXHEIGHT;
			if (gdi->cursor.x < bx) {
				int d = bx - gdi->cursor.x;
				cx += d;
				cw -= d;
				x += d;
			}
			if (gdi->cursor.y < by) {
				int d = by - gdi->cursor.y;
				cy += d;
				ch -= d;
				y += d;
			}
			if (x + cw > gdi->cursor.maxw) {
				cw -= (x + cw) - gdi->cursor.maxw;
			}
			if (y + ch > gdi->cursor.maxh) {
				ch -= (y + ch) - gdi->cursor.maxh;
			}
			if (cw > 0 && ch > 0) {
				TransparentBlt(gdi->buf.thdc, x, y, (int)(cw * gdi->xmult), (int)(ch * gdi->ymult), gdi->cursor.thdc, cx, cy, cw, ch, 0xfe00fe);
			}
		}
		if (gdi->osd.active && gdi->osd.hbm) {
			TransparentBlt(gdi->buf.thdc, gdi->osd.x, gdi->osd.y, gdi->ledwidth, gdi->ledheight, gdi->osd.thdc, 0, 0, gdi->ledwidth, gdi->ledheight, 0x000000);
		}
		struct gdioverlay *ov = gdi->extoverlays;
		while (ov) {
			if (ov->tex.bits) {
				TransparentBlt(gdi->buf.thdc, ov->x, ov->y, ov->tex.width, ov->tex.height, ov->tex.thdc, 0, 0, ov->tex.width, ov->tex.height, 0xfe00fe);
			}
			ov = ov->next;
		}

		BitBlt(gdi->hdc, 0, 0, gdi->wwidth, gdi->wheight, gdi->buf.thdc, 0, 0, SRCCOPY);
	}
}

static void gdi_showframe(int monid)
{
	struct gdistruct *gdi = &gdidata[monid];
	gdi->refreshneeded = true;
	RECT r;
	r.left = 0;
	r.top = 0;
	r.right = gdi->wwidth;
	r.bottom = gdi->wheight;
	InvalidateRect(gdi->hwnd, &r, FALSE);
}

static void gdi_refresh(int monid)
{
	gdi_clear(monid);
	gdi_showframe(monid);
}

void gdi_free(int monid, bool immediate)
{
	struct gdistruct *gdi = &gdidata[monid];
	gdi->enabled = 0;
	freetexture(monid);
	freesprite(gdi, &gdi->osd);
	freesprite(gdi, &gdi->cursor);
	struct gdioverlay *ov = gdi->extoverlays;
	while (ov) {
		struct gdioverlay *next = ov->next;
		if (ov->tex.bits) {
			freesprite(gdi, &ov->tex);
		}
		xfree(ov);
		ov = next;
	}
	gdi->extoverlays = NULL;
}

static const TCHAR *gdi_init(HWND ahwnd, int monid, int w_w, int w_h, int depth, int *freq, int mmulth, int mmultv, int *errp)
{
	struct gdistruct *gdi = &gdidata[monid];

	if (isfullscreen() > 0) {
		*errp = 2;
		return _T("GDI fullscreen not supported");
	}

	gdi->hwnd = ahwnd;
	gdi->depth = depth;
	gdi->wwidth = w_w;
	gdi->wheight = w_h;
	if (allocsprite(gdi, &gdi->buf, gdi->wwidth, gdi->wheight)) {
		gdi->statusbar_hx = gdi->statusbar_vx = statusline_set_multiplier(monid, gdi->wwidth, gdi->wheight) / 100;
		gdi->ledwidth = gdi->wwidth;
		gdi->ledheight = TD_TOTAL_HEIGHT * gdi->statusbar_vx;
		allocsprite(gdi, &gdi->osd, gdi->ledwidth, gdi->ledheight);
		allocsprite(gdi, &gdi->cursor, CURSORMAXWIDTH, CURSORMAXHEIGHT);
		gdi->enabled = 1;
		write_log(_T("GDI mode initialized %d*%d*%d\n"), w_w, w_h, depth);
		return NULL;
	}

	*errp = 1;
	return _T("failed to allocate buffer");
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

static bool gdi_setcursor(int monid, int x, int y, int width, int height, float mx, float my, bool visible, bool noscale)
{
	struct gdistruct *gdi = &gdidata[monid];
	int cx, cy;

	if (gdi->depth < 32) {
		return false;
	}

	if (width < 0 || height < 0) {
		return true;
	}

	if (width && height) {
		cx = (int)((float)x * mx * gdi->xmult + gdi->cursor_offset_x * gdi->ymult + 0.5f);
		cy = (int)((float)y * my * gdi->ymult + gdi->cursor_offset_y * gdi->xmult + 0.5f);
	} else {
		cx = cy = 0;
	}
	if (cx < 0) {
		cx = 0;
	}
	if (cy < 0) {
		cy = 0;
	}
	gdi->cursor.x = cx;
	gdi->cursor.y = cy;
	gdi->cursor.maxw = (int)((float)gdi->bmwidth * mx * gdi->xmult + gdi->cursor_offset_x * gdi->ymult + 0.5f);
	gdi->cursor.maxh = (int)((float)gdi->bmheight * my * gdi->ymult + gdi->cursor_offset_y * gdi->xmult + 0.5f);
	gdi->cursor_scale = !noscale;
	gdi->cursor.active = visible;
	return true;
}

static uae_u8 *gdi_setcursorsurface(int monid, bool query, int *pitch)
{
	struct gdistruct* gdi = &gdidata[monid];

	if (query) {
		return (uae_u8 *)gdi->cursor.bits;
	}

	if (gdi->depth < 32) {
		return NULL;
	}

	if (pitch) {
		*pitch = gdi->cursor.pitch;
		return (uae_u8*)gdi->cursor.bits;
	}
	for (int y = 0; y < CURSORMAXHEIGHT; y++) {
		for (int x = 0; x < CURSORMAXWIDTH; x++) {
			uae_u32 *p = (uae_u32*)((uae_u8*)gdi->cursor.bits + gdi->cursor.pitch * y + x * 4);
			uae_u32 v = *p;
			if ((v & 0xff000000) == 0x00000000) {
				*p = 0xfe00fe;
			} else {
				*p = v & 0xffffff;
			}
		}
	}

	return NULL;
}

static bool gdi_extoverlay(struct extoverlay *ext, int monid)
{
	struct gdistruct *gdi = &gdidata[monid];
	struct gdioverlay *ov, *ovprev, *ov2;
	struct gdibm *s = NULL;

	if (gdi->depth < 32) {
		return false;
	}

	gdi->eraseneeded = true;

	ov = gdi->extoverlays;
	ovprev = NULL;
	while (ov) {
		if (ov->id == ext->idx) {
			s = &ov->tex;
			break;
		}
		ovprev = ov;
		ov = ov->next;
	}

	if (!s && (ext->width <= 0 || ext->height <= 0))
		return false;

	if (!ext->data && s && (ext->width == 0 || ext->height == 0)) {
		ov->x = ext->xpos;
		ov->y = ext->ypos;
		return true;
	}

	if (ov && s) {
		if (ovprev) {
			ovprev->next = ov->next;
		} else {
			gdi->extoverlays = ov->next;
		}
		freesprite(gdi, s);
		xfree(ov);
		if (ext->width <= 0 || ext->height <= 0)
			return true;
	}

	if (ext->width <= 0 || ext->height <= 0 || !ext->data || !gdi->hdc) {
		return false;
	}

	ov = xcalloc(gdioverlay, 1);
	if (!ov) {
		return false;
	}
	if (!allocsprite(gdi, &ov->tex, ext->width, ext->height)) {
		xfree(ov);
		return false;
	}

	s = &ov->tex;
	ov->id = ext->idx;

	ov2 = gdi->extoverlays;
	ovprev = NULL;
	for (;;) {
		if (ov2 == NULL || ov2->id >= ov->id) {
			if (ov2 == gdi->extoverlays) {
				gdi->extoverlays = ov;
				ov->next = ov2;
			} else {
				ov->next = ovprev->next;
				ovprev->next = ov;
			}
			break;
		}
		ovprev = ov2;
		ov2 = ov2->next;
	}

	ov->x = ext->xpos;
	ov->y = ext->ypos;

	for (int y = 0; y < ext->height; y++) {
		for (int x = 0; x < ext->width; x++) {
			uae_u32 *sp = (uae_u32*)(ext->data + ext->width * y * 4 + x * 4);
			uae_u32 *p = (uae_u32*)((uae_u8*)s->bits + s->pitch * y + x * 4);
			uae_u32 v = *sp;
			if ((v & 0xff000000) == 0x00000000) {
				*p = 0xfe00fe;
			} else {
				*p = v & 0xffffff;
			}
		}
	}

	return true;
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
	D3D_setcursor = gdi_setcursor;
	D3D_setcursorsurface = gdi_setcursorsurface;
	D3D_getrefreshrate = NULL;
	D3D_resize = NULL;
	D3D_change = NULL;
	D3D_getscalerect = NULL;
	D3D_run = NULL;
	D3D_debug = NULL;
	D3D_led = NULL;
	D3D_getscanline = NULL;
	D3D_extoverlay = gdi_extoverlay;
	D3D_paint = gdi_paint;
}
