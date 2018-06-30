#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <commctrl.h>
#include <Dwmapi.h>

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "resource.h"
#include "registry.h"
#include "win32.h"
#include "win32gui.h"
#include "xwin.h"
#include "zfile.h"

#define MAX_GUI_FONTS 2
#define DEFAULT_FONTSIZE  8

static double multx, multy;
static int scaleresource_width, scaleresource_height;

static TCHAR fontname_gui[32], fontname_list[32];
static int fontsize_gui = DEFAULT_FONTSIZE;
static int fontsize_list = DEFAULT_FONTSIZE;
static int fontstyle_gui = 0;
static int fontstyle_list = 0;
static int fontweight_gui = FW_REGULAR;
static int fontweight_list = FW_REGULAR;

static int listviewcnt;
static int listviews_id[16];

static int setparamcnt;
static int setparam_id[16];

static HFONT listviewfont;
static TEXTMETRIC listview_tm;
static const TCHAR *fontprefix;

#define BASEMULT 1000
static int baseunitx, baseunity;
static RECT baserect, baseclientrect;
static int baseborderwidth, baseborderheight;
static int basewidth, baseheight;
static int baseclientwidth, baseclientheight;

#include <pshpack2.h>
typedef struct {
	WORD dlgVer;
	WORD signature;
	DWORD helpID;
	DWORD exStyle;
	DWORD style;
	WORD cDlgItems;
	short x;
	short y;
	short cx;
	short cy;
	/*
	sz_Or_Ord menu;
	sz_Or_Ord windowClass;
	WCHAR title[titleLen];
	*/
} DLGTEMPLATEEX;

typedef struct {
	WORD pointsize;
	WORD weight;
	BYTE italic;
	BYTE charset;
	WCHAR typeface[1];
} DLGTEMPLATEEX_END;

typedef struct {
	DWORD helpID;
	DWORD exStyle;
	DWORD style;
	short x;
	short y;
	short cx;
	short cy;
	WORD id;
	WORD reserved;
	WCHAR windowClass[1];
	/* variable data after this */
	/* sz_Or_Ord title; */
	/* WORD extraCount; */
} DLGITEMTEMPLATEEX;
#include <poppack.h>

static int font_vista_ok;
static const wchar_t wfont_vista[] = _T("Segoe UI");
static const wchar_t wfont_xp[] = _T("Tahoma");
static const wchar_t wfont_old[] = _T("MS Sans Serif");
static const TCHAR font_vista[] = _T("Segoe UI");
static const TCHAR font_xp[] = _T("Tahoma");

static int align (double f)
{
	int v = (int)(f + 0.5);
	return v;
}

static int mmx (int v)
{
	return align ((v * multx) / 100.0 + 0.5);
}
static int mmy (int v)
{
	return align ((v * multy) / 100.0 + 0.5);
}


static BYTE *skiptextone (BYTE *s)
{
	s -= sizeof (WCHAR);
	if (s[0] == 0xff && s[1] == 0xff) {
		s += 4;
		return s;
	}
	while (s[0] != 0 || s[1] != 0)
		s += 2;
	s += 2;
	return s;
}

static BYTE *skiptext (BYTE *s)
{
	if (s[0] == 0xff && s[1] == 0xff) {
		s += 4;
		return s;
	}
	while (s[0] != 0 || s[1] != 0)
		s += 2;
	s += 2;
	return s;
}

static BYTE *todword (BYTE *p)
{
	while ((LONG_PTR)p & 3)
		p++;
	return p;
}

static void modifytemplate (DLGTEMPLATEEX *d, DLGTEMPLATEEX_END *d2, int id, int fullscreen)
{
	if (fullscreen) {
		d->cx = scaleresource_width;
		d->cy = scaleresource_height;
	} else {
		d->cx = mmx (d->cx);
		d->cy = mmy (d->cy);
	}
}

static void modifytemplatefont (DLGTEMPLATEEX *d, DLGTEMPLATEEX_END *d2)
{
	if (!wcscmp (d2->typeface, wfont_old)) {
		wcscpy (d2->typeface, fontname_gui);
		d2->pointsize = fontsize_gui;
		d2->italic = (fontstyle_gui & ITALIC_FONTTYPE) != 0;
		d2->weight = fontweight_gui;
}
}

static void modifyitem (DLGTEMPLATEEX *d, DLGTEMPLATEEX_END *d2, DLGITEMTEMPLATEEX *dt, int id)
{
	bool noyscale = false;
	int wc = 0;

	if (dt->windowClass[0] == 0xffff)
		wc = dt->windowClass[1];

	if (multy >= 89 && multy <= 111) {

		if (wc == 0x0080 && dt->cy <= 20) { // button
			noyscale = true;
		}
		if (wc == 0x0085) {// combo box
			noyscale = false;
		}
		if (wc == 0x0081 && dt->cy <= 20) { // edit box
			noyscale = true;
		}
	}

	if (!noyscale)
		dt->cy = mmy (dt->cy);

	dt->cx = mmx (dt->cx);
	dt->y = mmy (dt->y);
	dt->x = mmx (dt->x);

	if (wc == 0x0085) {// combo box
		setparam_id[setparamcnt] = dt->id;
		setparamcnt++;
	}

	if (dt->windowClass[0] != 0xffff) {
		if (!_tcsicmp (dt->windowClass, WC_LISTVIEWW) || !_tcsicmp (dt->windowClass, WC_TREEVIEWW)) {
			listviews_id[listviewcnt] = dt->id;
			listviewcnt++;
		}
	}

}

static INT_PTR CALLBACK DummyProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch(msg)
	{
	case WM_DESTROY:
		PostQuitMessage (0);
		return TRUE;
	case WM_CLOSE:
		DestroyWindow(hDlg);
		return TRUE;
	case WM_INITDIALOG:
		return TRUE;
	}
	return FALSE;
}

extern int full_property_sheet;

static struct newresource *scaleresource2 (struct newresource *res, HWND parent, int resize, int fullscreen, DWORD exstyle, bool main)
{
	static int main_width, main_height;

	DLGTEMPLATEEX *d, *s;
	DLGTEMPLATEEX_END *d2, *s2;
	DLGITEMTEMPLATEEX *dt;
	BYTE *p, *p2, *ps, *ps2;
	int i;
	struct newresource *ns;

	listviewcnt = 0;
	setparamcnt = 0;

	d = (DLGTEMPLATEEX*)res->resource;
	d2 = (DLGTEMPLATEEX_END*)res->resource;

	if (d->dlgVer != 1 || d->signature != 0xffff)
		return 0;
	if (!(d->style & (DS_SETFONT | DS_SHELLFONT)))
		return 0;

	ns = xcalloc (struct newresource, 1);
	ns->inst = res->inst;
	ns->size = res->size;
	ns->tmpl = res->tmpl;
	ns->resource = (LPCDLGTEMPLATEW)xmalloc (uae_u8, ns->size + 32);
	memcpy ((void*)ns->resource, res->resource, ns->size);

	d = (DLGTEMPLATEEX*)ns->resource;
	s = (DLGTEMPLATEEX*)res->resource;

	int width = d->cx;
	int height = d->cy;

	if (resize > 0) {
		d->style &= ~DS_MODALFRAME;
		d->style |= WS_THICKFRAME;
	} else if (resize == 0) {
		d->style |= DS_MODALFRAME;
		d->style &= ~WS_THICKFRAME;
	}
	if (fullscreen > 0) {
		//d->style |= SW_MAXIMIZE;
		d->style |= WS_THICKFRAME;
	} else {
		d->style |= WS_MINIMIZEBOX;
	}
	d->exStyle |= exstyle;

	d2 = (DLGTEMPLATEEX_END*)ns->resource;
	p = (BYTE*)d + sizeof (DLGTEMPLATEEX);
	p = skiptext (p);
	p = skiptext (p);
	p = skiptext (p);

	s2 = (DLGTEMPLATEEX_END*)res->resource;
	ps = (BYTE*)s2 + sizeof (DLGTEMPLATEEX);
	ps = skiptext (ps);
	ps = skiptext (ps);
	ps = skiptext (ps);

	d2 = (DLGTEMPLATEEX_END*)p;
	p2 = p;
	p2 += sizeof (DLGTEMPLATEEX_END);
	p2 = skiptextone (p2);
	p2 = todword (p2);

	s2 = (DLGTEMPLATEEX_END*)ps;
	ps2 = ps;
	ps2 += sizeof (DLGTEMPLATEEX_END);
	ps2 = skiptextone (ps2);
	ps2 = todword (ps2);

	modifytemplatefont (d, d2);

	p += sizeof (DLGTEMPLATEEX_END);
	p = skiptextone (p);
	p = todword (p);

	memcpy (p, ps2, ns->size - (ps2 - (BYTE*)res->resource));

	modifytemplate(d, d2, ns->tmpl, fullscreen);

	for (i = 0; i < d->cDlgItems; i++) {
		dt = (DLGITEMTEMPLATEEX*)p;
		modifyitem (d, d2, dt, ns->tmpl);
		p += sizeof (DLGITEMTEMPLATEEX);
		p = skiptextone (p);
		p = skiptext (p);
		p += ((WORD*)p)[0];
		p += sizeof (WORD);
		p = todword (p);
	}

	ns->width = width;
	ns->height = height;
	return ns;
}

struct newresource *scaleresource (struct newresource *res, HWND parent, int resize, int fullscreen, DWORD exstyle, bool main)
{
	return scaleresource2(res, parent, resize, fullscreen, exstyle, main);
}

void freescaleresource (struct newresource *ns)
{
	xfree ((void*)ns->resource);
	xfree (ns);
}

int getscaledfontsize(int size)
{
	HDC hdc = GetDC(NULL);
	if (size <= 0)
		size = fontsize_gui;
	size = -MulDiv(size, GetDeviceCaps(hdc, LOGPIXELSY), 72);
	ReleaseDC(NULL, hdc);
	return size;
}

static void openfont (bool force)
{
	HDC hdc;
	int size;

	if (listviewfont && !force)
		return;
	if (listviewfont)
		DeleteObject (listviewfont);

	hdc = GetDC (NULL);

	size = -MulDiv (fontsize_list, GetDeviceCaps (hdc, LOGPIXELSY), 72);
	listviewfont = CreateFont (size, 0, 0, 0, fontweight_list, (fontstyle_list & ITALIC_FONTTYPE) != 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, fontname_list);
	HGDIOBJ o = SelectObject(hdc, listviewfont);
	memset(&listview_tm, 0, sizeof listview_tm);
	listview_tm.tmAveCharWidth = 8;
	GetTextMetrics(hdc, &listview_tm);
	SelectObject(hdc, o);

	ReleaseDC (NULL, hdc);
}

void scalaresource_listview_font_info(int *w)
{
	*w = listview_tm.tmAveCharWidth;
}

void scaleresource_setfont (HWND hDlg)
{
	if (listviewcnt) {
		if (!listviewfont) {
			openfont (false);
			if (!listviewfont)
				return;
		}
		for (int i = 0; i < listviewcnt; i++) {
			SendMessage (GetDlgItem (hDlg, listviews_id[i]), WM_SETFONT, WPARAM(listviewfont), FALSE);
		}
	}
	if (os_vista) {
		for (int i = 0; i < setparamcnt; i++) {
			int v = SendMessage (GetDlgItem (hDlg, setparam_id[i]), CB_GETITEMHEIGHT , -1, NULL);
			if (v > 0 && mmy(v) > v)
				SendMessage (GetDlgItem (hDlg, setparam_id[i]), CB_SETITEMHEIGHT , -1, mmy(v));
		}
	}
}

static void setdeffont (void)
{
	_tcscpy (fontname_gui, font_vista_ok ? wfont_vista : wfont_xp);
	fontsize_gui = DEFAULT_FONTSIZE;
	fontstyle_gui = 0;
	fontweight_gui = FW_REGULAR;
	_tcscpy (fontname_list, font_vista_ok ? wfont_vista : wfont_xp);
	fontsize_list = DEFAULT_FONTSIZE;
	fontstyle_list = 0;
	fontweight_list = FW_REGULAR;
}

static TCHAR *fontreg[2] = { _T("GUIFont"), _T("GUIListFont") };

static void regsetfont (UAEREG *reg, const TCHAR *prefix, const TCHAR *name, const TCHAR *fontname, int fontsize, int fontstyle, int fontweight)
{
	TCHAR tmp[256], tmp2[256];

	_stprintf (tmp, _T("%s:%d:%d:%d"), fontname, fontsize, fontstyle, fontweight);
	_stprintf (tmp2, _T("%s%s"), name, prefix);
	regsetstr (reg, tmp2, tmp);
}
static void regqueryfont (UAEREG *reg, const TCHAR *prefix, const TCHAR *name, TCHAR *fontname, int *pfontsize, int *pfontstyle, int *pfontweight)
{
	TCHAR tmp2[256], tmp[256], *p1, *p2, *p3, *p4;
	int size;
	int fontsize, fontstyle, fontweight;

	_tcscpy (tmp2, name);
	_tcscat (tmp2, prefix);
	size = sizeof tmp / sizeof (TCHAR);
	if (!regquerystr (reg, tmp2, tmp, &size))
		return;
	p1 = _tcschr (tmp, ':');
	if (!p1)
		return;
	*p1++ = 0;
	p2 = _tcschr (p1, ':');
	if (!p2)
		return;
	*p2++ = 0;
	p3 = _tcschr (p2, ':');
	if (!p3)
		return;
	*p3++ = 0;
	p4 = _tcschr (p3, ':');
	if (p4)
		*p4 = 0;

	_tcscpy (fontname, tmp);
	fontsize = _tstoi (p1);
	fontstyle = _tstoi (p2);
	fontweight = _tstoi (p3);

	if (fontsize == 0)
		fontsize = 8;
	if (fontsize < 5)
		fontsize = 5;
	if (fontsize > 20)
		fontsize = 20;
	*pfontsize = fontsize;

	*pfontstyle = fontstyle;

	*pfontweight = fontweight;
}

void scaleresource_setdefaults (void)
{
	setdeffont ();
	for (int i = 0; i < MAX_GUI_FONTS; i++) {
		TCHAR tmp[256];
		_stprintf (tmp, _T("%s%s"), fontreg[i], fontprefix);
		regdelete (NULL, tmp);
	}
	openfont (true);
}

static INT_PTR CALLBACK TestProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_INITDIALOG) {
		RECT r;
		// there really is no better way?
		r.left = 0;
		r.top = 0;
		r.bottom = BASEMULT;
		r.right = BASEMULT;
		MapDialogRect (hDlg, &r);
		baseunitx = r.right * 4 / BASEMULT;
		baseunity = r.bottom * 8 / BASEMULT;
		GetWindowRect (hDlg, &baserect);
		GetClientRect (hDlg, &baseclientrect);
	}
	return 0;
}

// horrible or what?
static void getbaseunits (int fullscreen)
{
	multx = multy = 100;
	struct newresource *nr, *nr2;
	HWND hwnd;
	nr = getresource (IDD_PANEL);
	if (!nr) {
		write_log (_T("getbaseunits fail!\n"));
		abort();
	}
	nr2 = scaleresource2(nr, NULL, -1, 0, 0, false);
	hwnd = CreateDialogIndirect (nr2->inst, nr2->resource, NULL, TestProc);
	if (hwnd) {
		DestroyWindow (hwnd);
	} else {
		baserect.left = baserect.top = 0;
		baserect.right = 800;
		baserect.bottom = 600;
		baseclientrect.left = baseclientrect.top = 0;
		baseclientrect.right = 800;
		baseclientrect.bottom = 600;
	}
	freescaleresource (nr2);
	freescaleresource (nr);
	basewidth = baserect.right - baserect.left;
	baseheight = baserect.bottom - baserect.top;
	baseclientwidth = baseclientrect.right - baseclientrect.left;
	baseclientheight = baseclientrect.bottom - baseclientrect.top;
	baseborderwidth = basewidth - baseclientwidth;
	baseborderheight = baseheight - baseclientheight;

	write_log (_T("GUIBase %dx%d (%dx%d)\n"), basewidth, baseheight, baseunitx, baseunity);
}

void scaleresource_init (const TCHAR *prefix, int fullscreen)
{
	if (os_vista)
		font_vista_ok = 1;

	fontprefix = prefix;

	setdeffont ();

	regqueryfont (NULL, fontprefix, fontreg[0], fontname_gui, &fontsize_gui, &fontstyle_gui, &fontweight_gui);
	regqueryfont (NULL, fontprefix, fontreg[1], fontname_list, &fontsize_list, &fontstyle_list, &fontweight_list);

	//write_log (_T("GUI font %s:%d:%d:%d\n"), fontname_gui, fontsize_gui, fontstyle_gui, fontweight_gui);
	//write_log (_T("List font %s:%d:%d:%d\n"), fontname_list, fontsize_list, fontstyle_list, fontweight_list);

	getbaseunits (fullscreen);

	openfont (true);
}

#if 0
static void sizefont (HWND hDlg, const TCHAR *name, int size, int style, int weight, int *width, int *height)
{
	/* ARGH!!! */

	HDC hdc = GetDC (hDlg);
	size = -MulDiv (size, lpy, 72);
	HFONT font = CreateFont (size, 0, 0, 0, weight,
		(style & ITALIC_FONTTYPE) != 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, name);
	if (!font) {
		*width = 8;
		*height = 8;
	} else {
		HFONT hFontOld = (HFONT)SelectObject (hdc, font);
		TEXTMETRIC tm;
		SIZE fsize;
		GetTextMetrics (hdc, &tm);
		GetTextExtentPoint32 (hdc, _T("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"), 52, &fsize);
		*width = (fsize.cx / 26 + 1) / 2;
		*height = tm.tmHeight;
		SelectObject (hdc, hFontOld);
		DeleteObject (font);
	}
	ReleaseDC (hDlg, hdc);
}
#endif


typedef enum MONITOR_DPI_TYPE {
	MDT_EFFECTIVE_DPI = 0,
	MDT_ANGULAR_DPI = 1,
	MDT_RAW_DPI = 2,
	MDT_DEFAULT = MDT_EFFECTIVE_DPI
} MONITOR_DPI_TYPE;
typedef HRESULT(CALLBACK* GETDPIFORMONITOR)(HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT*);

void scaleresource_getdpimult (double *dpixmp, double *dpiymp, int *dpixp, int *dpiyp)
{
	GETDPIFORMONITOR pGetDpiForMonitor;
	POINT pt = { 32000, 32000 };
	HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);

	*dpixmp = 1.0;
	*dpiymp = 1.0;
	*dpixp = 0;
	*dpiyp = 0;
	pGetDpiForMonitor = (GETDPIFORMONITOR)GetProcAddress(GetModuleHandle(_T("Shcore.dll")), "GetDpiForMonitor");
	if (pGetDpiForMonitor) {
		UINT dpix, dpiy;
		if (SUCCEEDED(pGetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpix, &dpiy))) {
			if (dpix > 96)
				*dpixmp = (double)dpix / 96.0;
			if (dpiy > 96)
				*dpiymp = (double)dpiy / 96.0;
			*dpixp = dpix;
			*dpiyp = dpiy;
		}
	}
}

void scaleresource_setmult (HWND hDlg, int w, int h, int fullscreen)
{
	if (w < 0) {
		multx = -w;
		multy = -h;
		return;
	}

	scaleresource_width = w;
	scaleresource_height = h;

	multx = w * 100.0 / basewidth;
	multy = h * 100.0 / baseheight;

	if (multx < 50)
		multx = 50;
	if (multy < 50)
		multy = 50;

	//write_log (_T("MX=%f MY=%f\n"), multx, multy);
}

void scaleresource_getmult (int *mx, int *my)
{
	if (mx)
		*mx = (int)(multx + 0.5);
	if (my)
		*my = (int)(multy + 0.5);
}


int scaleresource_choosefont (HWND hDlg, int fonttype)
{
	CHOOSEFONT cf = { 0 };
	LOGFONT lf = { 0 };
	HDC hdc;
	TCHAR *fontname[2];
	int *fontsize[2], *fontstyle[2], *fontweight[2];
	int lm;

	fontname[0] = fontname_gui;
	fontname[1] = fontname_list;
	fontsize[0] = &fontsize_gui;
	fontsize[1] = &fontsize_list;
	fontstyle[0] = &fontstyle_gui;
	fontstyle[1] = &fontstyle_list;
	fontweight[0] = &fontweight_gui;
	fontweight[1] = &fontweight_list;

	cf.lStructSize = sizeof cf;
	cf.hwndOwner = hDlg;
	cf.Flags = CF_FORCEFONTEXIST | CF_INITTOLOGFONTSTRUCT | CF_NOSCRIPTSEL | CF_SCREENFONTS;
	cf.lpLogFont = &lf;
	cf.nFontType = REGULAR_FONTTYPE;
	cf.iPointSize = *fontsize[fonttype];

	hdc = GetDC (NULL);
	lm = GetDeviceCaps (hdc, LOGPIXELSY);

	_tcscpy (lf.lfFaceName, fontname[fonttype]);
	lf.lfHeight = -MulDiv (*fontsize[fonttype], lm, 72);
	lf.lfWeight = *fontweight[fonttype];
	lf.lfItalic = (*fontstyle[fonttype] & ITALIC_FONTTYPE) != 0;

	if (!ChooseFont (&cf)) {
		ReleaseDC (NULL, hdc);
		return 0;
	}

	_tcscpy (fontname[fonttype], lf.lfFaceName);
	*fontsize[fonttype] = lf.lfHeight;
	*fontsize[fonttype] = -MulDiv (*fontsize[fonttype], 72, GetDeviceCaps (hdc, LOGPIXELSY));

	*fontstyle[fonttype] = lf.lfItalic ? ITALIC_FONTTYPE : 0;

	*fontweight[fonttype] = lf.lfWeight;

	ReleaseDC (NULL, hdc);

	regsetfont (NULL, fontprefix, fontreg[fonttype], fontname[fonttype], *fontsize[fonttype], *fontstyle[fonttype], *fontweight[fonttype]);

	openfont (true);

	return 1;
}

#include <gdiplus.h> 

#define MAX_BOX_ART_IMAGES 20
#define MAX_BOX_ART_TYPES 4
#define MAX_VISIBLE_IMAGES 2

static bool boxart_inited;
static ULONG_PTR gdiplusToken;
static HWND boxarthwnd;
static int boxart_window_width;
static int boxart_window_height;
static const int hgap = 8;
static const int wgap = 8;
static Gdiplus::Image *images[MAX_BOX_ART_IMAGES];
static int total_height;
static int max_width;
static int total_images;
static int imagemode;
static bool imagemodereset;
static int lastimage;
static TCHAR image_path[MAX_DPATH];
static int image_coords[MAX_VISIBLE_IMAGES + 1];

int max_visible_boxart_images = MAX_VISIBLE_IMAGES;
int stored_boxart_window_width = 400;

static void boxart_init(void)
{
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
}

static const TCHAR *boxartnames[MAX_BOX_ART_TYPES] = {
	_T("Boxart"),
	_T("Title"),
	_T("SShot"),
	_T("Misc"),
};

typedef HRESULT(CALLBACK* DWMGETWINDOWATTRIBUTE)(HWND hwnd, DWORD dwAttribute, PVOID pvAttribute, DWORD cbAttribute);
static DWMGETWINDOWATTRIBUTE pDwmGetWindowAttribute;
static HMODULE dwmapihandle;

static void getpos(RECT *r)
{
	RECT r1, r2;
	if (!pDwmGetWindowAttribute && !dwmapihandle && os_vista) {
		dwmapihandle = LoadLibrary(_T("dwmapi.dll"));
		if (dwmapihandle)
			pDwmGetWindowAttribute = (DWMGETWINDOWATTRIBUTE)GetProcAddress(dwmapihandle, "DwmGetWindowAttribute");
	}

	GetWindowRect(hGUIWnd, &r1);
	r2 = r1;

	if (pDwmGetWindowAttribute) {
		pDwmGetWindowAttribute(hGUIWnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r2, sizeof(r2));
	}

	r->left = r1.right - ((r2.left - r1.left) + (r1.right - r2.right));
	r->top = r1.top;
	r->bottom = r1.bottom;
	r->right = r->left + stored_boxart_window_width;
}

void move_box_art_window(void)
{
	RECT r;

	if (!hGUIWnd || !boxarthwnd)
		return;
	getpos(&r);
	SetWindowPos(boxarthwnd, HWND_TOPMOST, r.left, r.top, 0, 0, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOSIZE);
}

void close_box_art_window(void)
{
	for (int i = 0; i < MAX_BOX_ART_IMAGES; i++) {
		if (images[i]) {
			delete images[i];
			images[i] = NULL;
		}
	}
	if (!boxarthwnd)
		return;
	ShowWindow(boxarthwnd, SW_HIDE);
	DestroyWindow(boxarthwnd);
	boxarthwnd = NULL;
}

static bool open_box_art_window(void)
{
	RECT r;
	
	getpos(&r);
	if (!boxarthwnd) {
		DWORD exstyle = GetWindowLong(hGUIWnd, GWL_EXSTYLE);
		DWORD style = GetWindowLong(hGUIWnd, GWL_STYLE);

		style &= ~(WS_VISIBLE);

		boxarthwnd = CreateWindowEx(exstyle | WS_EX_NOACTIVATE,
			_T("BoxArt"), _T("WinUAE"),
			style,
			r.left, r.top,
			stored_boxart_window_width, r.bottom - r.top,
			hGUIWnd, NULL, hInst, NULL);
		if (boxarthwnd) {
			RECT r;
			GetClientRect(boxarthwnd, &r);
			boxart_window_width = r.right - r.left;
			boxart_window_height = r.bottom - r.top;

			HMENU menu = GetSystemMenu(boxarthwnd, FALSE);
			InsertMenu(menu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, _T(""));
			InsertMenu(menu, -1, MF_BYPOSITION, 1, _T("Open Game Folder"));
		}
	} else {
		move_box_art_window();
	}

	return boxarthwnd != 0;
}

static void boxartpaint(HDC hdc, HWND hwnd)
{
	int cnt;
	RECT r;

	r.left = 0;
	r.top = 0;
	r.right = boxart_window_width;
	r.bottom = boxart_window_height;
	FillRect(hdc, &r, (HBRUSH)(COLOR_BTNFACE + 1));

	int image_count = total_images;
	int image_total_height = total_height;

	if (imagemode) {
		int round = 0;
		if (imagemode > MAX_BOX_ART_IMAGES) {
			if (imagemodereset) {
				imagemode = 0;
			} else {
				imagemode = 1;
			}
		}
		while (imagemode) {
			if (images[imagemode - 1]) {
				Gdiplus::Image *img = images[imagemode - 1];
				image_count = 1;
				image_total_height = img->GetHeight();
				max_width = img->GetWidth();
				img = NULL;
				break;
			}
			imagemode++;
			if (imagemode > MAX_BOX_ART_IMAGES) {
				if (imagemodereset) {
					imagemode = 0;
				} else {
					imagemode = 1;
				}
				round++;
			}
			if (round > 1) {
				imagemode = 0;
			}
		}
	}

	int window_w = boxart_window_width - 2 * wgap;
	int window_h = boxart_window_height - (image_count + 1) * hgap;

	float scale;
	float scalex = (float)window_w / max_width;
	float scaley = (float)window_h / image_total_height;
	if (scalex > 1)
		scalex = 1;
	scale = scalex;
	if (scale > scaley)
		scale = scaley;

	Gdiplus::Graphics graphics(hdc);
	Gdiplus::Pen pen(Gdiplus::Color(170, 170, 0, 0), 1);

	float y = 0;
	cnt = 0;
	for (int i = 0; i < MAX_BOX_ART_IMAGES; i++) {
		if (!imagemode && cnt >= max_visible_boxart_images)
			break;
		if (images[i]) {
			Gdiplus::Image *img = images[i];
			int h = img->GetHeight();
			y += h;
			cnt++;
		}
	}

	y = hgap + (window_h - (y * scale)) / 2;

	cnt = 0;
	for (int i = 0; i < MAX_BOX_ART_IMAGES; i++) {
		if (imagemode && imagemode - 1 != i)
			continue;
		if (!imagemode && cnt >= max_visible_boxart_images)
			break;
		if (images[i]) {
			Gdiplus::Image *img = images[i];
			int w = img->GetWidth();
			int h = img->GetHeight();

			w = (int)(w * scale + 0.5);
			h = (int)(h * scale + 0.5);

			int x1 = wgap + (window_w - w) / 2;
			int x2 = w;
			int y1 = (int)(y + 0.5);
			int y2 = h;

			if (image_count == 1) {
				y1 = hgap + (window_h - h) / 2;
			}

			Gdiplus::Rect d(x1, y1, x2, y2);
			graphics.DrawImage(img, d);

			Gdiplus::Rect d2(x1 - 1, y1 - 1, x2 + 1, y2 + 1);
			graphics.DrawRectangle(&pen, d2);

			image_coords[cnt] = y1 + y2 + hgap / 2;

			y += h;
			y += hgap;
			cnt++;
		}
	}
}

bool show_box_art(const TCHAR *path)
{
	TCHAR tmp1[MAX_DPATH];

	for (int i = 0; i < MAX_BOX_ART_IMAGES; i++) {
		if (images[i]) {
			delete images[i];
			images[i] = NULL;
		}
	}

	if (!path) {
		close_box_art_window();
		return false;
	}
	if (!artcache) {
		return false;
	}
	if (!boxart_inited) {
		boxart_init();
		boxart_inited = true;
	}
	if ((!full_property_sheet && isfullscreen() > 0) || !hGUIWnd) {
		close_box_art_window();
		return false;
	}
	if (!open_box_art_window())
		return false;

	if (path != image_path)
		_tcscpy(image_path, path);

	if (max_visible_boxart_images < 1 || max_visible_boxart_images > 3)
		max_visible_boxart_images = 2;

	total_height = 0;
	max_width = 0;
	total_images = 0;
	lastimage = 0;

	write_log(_T("Box art path '%s'\n"), path);
	int cnt = 0;
	for (int arttype = 0; arttype < MAX_BOX_ART_TYPES; arttype++) {
		for (int j = 0; j < 10; j++) {

			if (total_images >= MAX_BOX_ART_IMAGES)
				break;
			images[cnt] = NULL;

			Gdiplus::Image *image;
			_tcscpy(tmp1, path);
			_tcscat(tmp1, _T("___"));
			_tcscat(tmp1, boxartnames[arttype]);
			if (j > 0)
				_stprintf(tmp1 + _tcslen(tmp1), _T("%d"), j + 1);
			_tcscat(tmp1, _T(".png"));

			image = Gdiplus::Image::FromFile(tmp1);
			// above returns out of memory if file does not exist!
			if (image->GetLastStatus() != Gdiplus::Ok) {
				_tcscpy(tmp1 + _tcslen(tmp1) - 3, _T("jpg"));
				image = Gdiplus::Image::FromFile(tmp1);
			}
			if (image->GetLastStatus() == Gdiplus::Ok) {
				int w = image->GetWidth();
				int h = image->GetHeight();
				write_log(_T("Image '%s' loaded %d*%d\n"), tmp1, w, h);
				images[cnt++] = image;
				if (total_images < max_visible_boxart_images) {
					if (w > max_width)
						max_width = w;
					total_height += h;
					total_images++;
				}
			} else {
				delete image;
				break;
			}
			image = NULL;
		}
	}
	images[cnt] = NULL;

	if (!total_images) {
		close_box_art_window();
		return false;
	}

	InvalidateRect(boxarthwnd, NULL, TRUE);
	ShowWindow(boxarthwnd, SW_SHOWNOACTIVATE);

	return true;
}

static void image_reload(int cnt)
{
	max_visible_boxart_images = cnt;
	regsetint(NULL, _T("ArtImageCount"), max_visible_boxart_images);
	show_box_art(image_path);
}

LRESULT CALLBACK BoxArtWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int oldmode = imagemode;
	switch (message)
	{
		case WM_LBUTTONDOWN:
			if (imagemode) {
				lastimage = imagemode - 1;
				imagemode = 0;
			} else {
				int y = (short)(lParam >> 16);
				imagemode = 0;
				for (int i = 1; i < max_visible_boxart_images; i++) {
					if (y >= image_coords[i - 1])
						imagemode = i;
				}
				while (imagemode > 0) {
					if (images[imagemode])
						break;
					imagemode--;
				}
				imagemode++;
			}
			lastimage = imagemode - 1;
			imagemodereset = true;
			if (oldmode != imagemode)
				InvalidateRect(hWnd, NULL, TRUE);
		break;
		case WM_RBUTTONDOWN:
			if (imagemode) {
				imagemode++;
				lastimage = imagemode - 1;
			} else {
				imagemode = lastimage + 1;
				if (imagemode <= 0)
					imagemode = 1;
			}
			imagemodereset = false;
			if (oldmode != imagemode)
				InvalidateRect(hWnd, NULL, TRUE);
		break;
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hDC = BeginPaint(hWnd, &ps);
			boxartpaint(hDC, hWnd);
			EndPaint(hWnd, &ps);
		}
		break;
		case WM_CLOSE:
			close_box_art_window();
		return 0;
		case WM_CHAR:
			switch (wParam)
			{
			case 27:
				DestroyWindow(hWnd);
				break;
			case ' ':
				imagemode++;
				imagemodereset = true;
				InvalidateRect(hWnd, NULL, TRUE);
				break;
			case '1':
				image_reload(1);
				break;
			case '2':
				image_reload(2);
				break;
			case '3':
				image_reload(3);
				break;
			}
			break;
		case WM_EXITSIZEMOVE:
			regsetint(NULL, _T("ArtImageWidth"), stored_boxart_window_width);
			image_reload(max_visible_boxart_images);
		break;
		case WM_SIZING:
		{
			RECT *r = (RECT*)lParam, r2;
			getpos(&r2);
			r->left = r2.left;
			r->top = r2.top;
			r->bottom = r2.bottom;
			boxart_window_width = stored_boxart_window_width = r->right - r->left;
		}
		return FALSE;
		case WM_MOVING:
		{
			RECT *r = (RECT*)lParam, r2;
			getpos(&r2);
			r->left = r2.left;
			r->top = r2.top;
			r->right = r2.right;
			r->bottom = r2.bottom;
		}
		return FALSE;
		case WM_SYSCOMMAND:
		if (wParam == 1) {
			ShellExecute(NULL, _T("explore"), image_path, NULL, NULL, SW_SHOWNORMAL);
			return FALSE;
		}
		break;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}
