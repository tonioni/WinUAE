// Epson printer font rasterization and native page output through GDI.
#include "sysconfig.h"
#include "sysdeps.h"
#include <windows.h>
#include <winspool.h>
#include <shellapi.h>
#include "epsonprinter.h"
#include "printer_host.h"

static HFONT curFont;
static float curFontHorizPoints, curFontVertPoints;
static const TCHAR *curFontName;
static HDC memHDC, printerDC;
static LPOUTLINETEXTMETRIC otm;
static Bit16u dpiX, dpiY, multiPageCounter;
static DOCINFO docinfo;

static int selectfont(Bit16u style)
{
	static const TCHAR *thisFontName;
	static float thisFontHorizPoints;
	static float thisFontVertPoints;
	static Bit16u thisStyle;

	if (curFont) {
		for (;;) {
			if (thisFontName != curFontName)
				break;
			if (thisFontHorizPoints != curFontHorizPoints)
				break;
			if (thisFontVertPoints != curFontVertPoints)
				break;
			if ((thisStyle & (STYLE_ITALICS | STYLE_PROP)) != (style & (STYLE_ITALICS | STYLE_PROP)))
				break;
			// still using same font
			return 1;
		}
		DeleteObject (curFont);
		curFont = NULL;
		thisFontName = NULL;
		xfree (otm);
		otm = NULL;
	}
	thisFontHorizPoints = curFontHorizPoints;
	thisFontVertPoints = curFontVertPoints;
	thisStyle = style;
	thisFontName = curFontName;

	int ly = GetDeviceCaps (memHDC, LOGPIXELSY);
	int lx = GetDeviceCaps (memHDC, LOGPIXELSX);
	int rounds = 0;
	while (rounds < 2) {
		curFont = CreateFont ((int)(thisFontVertPoints * dpiY / ly + 0.5), (int)(thisFontHorizPoints * dpiX / lx + 0.5),
			0, 0,
			FW_NORMAL,
			(style & STYLE_ITALICS) ? TRUE : FALSE,
			FALSE,
			FALSE,
			DEFAULT_CHARSET,
			OUT_TT_PRECIS,
			CLIP_DEFAULT_PRECIS,
			PROOF_QUALITY,
			((style & STYLE_PROP) ? VARIABLE_PITCH : FIXED_PITCH) | FF_DONTCARE,
			thisFontName);
		if (curFont)
			break;
		rounds++;
		if (style & STYLE_PROP)
			thisFontName = curFontName = _T("Times New Roman");
		else
			thisFontName = curFontName = _T("Courier New");
	}
	if (curFont) {
		SelectObject (memHDC, curFont);
		int size = GetOutlineTextMetrics (memHDC, 0, NULL);
		if (size == 0) {
			DeleteObject(curFont);
			curFont = NULL;
		} else {
			otm = (LPOUTLINETEXTMETRIC)xmalloc (uae_u8, size);
			GetOutlineTextMetrics (memHDC, size, otm);
		}
	}
	return curFont ? 1 : 0;
}

void host_printer_reset_font()
{
	if (curFont) DeleteObject(curFont);
	curFont = NULL;
}

bool host_printer_init(const TCHAR *name, uae_u16 *dx, uae_u16 *dy,
	float *width, float *height)
{
	if (name) {
		printerDC = CreateDC(NULL, name, NULL, NULL);
		if (!printerDC) return false;
		*dx = (uae_u16)GetDeviceCaps(printerDC, LOGPIXELSX);
		*dy = (uae_u16)GetDeviceCaps(printerDC, LOGPIXELSY);
		*width = (float)GetDeviceCaps(printerDC, HORZRES) / *dx;
		*height = (float)GetDeviceCaps(printerDC, VERTRES) / *dy;
	}
	dpiX = *dx;
	dpiY = *dy;
	curFont = NULL;
	return true;
}

void host_printer_finish_init()
{
	memHDC = CreateCompatibleDC(NULL);
}

void host_printer_close()
{
	xfree(otm);
	otm = NULL;
	host_printer_reset_font();
}

void host_printer_set_font(const TCHAR *name, float horizontal, float vertical,
	uae_u16 dx, uae_u16 dy, bool)
{
	host_printer_reset_font();
	curFontName = name;
	curFontHorizPoints = horizontal;
	curFontVertPoints = vertical;
	dpiX = dx;
	dpiY = dy;
}

bool host_printer_render_glyph(uae_u16 character, uae_u16 style,
	host_printer_glyph *result)
{
	*result = {};
	int bitmap_left = 0, bitmap_top = 0, width = 0, rows = 0;
	int advancex = 0, height = 0, ascender = 0, pitch = 0;
	uae_u8 *gbitmap = NULL;
	if (!selectfont (style))
		return false;
	MAT2 m2 = {{0, 1}, {0, 0}, {0, 0}, {0, 1}};
	GLYPHMETRICS metrics;
	int bufsize = GetGlyphOutline (memHDC, character, GGO_GRAY8_BITMAP, &metrics, 0, NULL, &m2);
	if (bufsize >= 0) {
		if (bufsize == 0)
			bufsize = 4;
		gbitmap = xcalloc (uae_u8, bufsize);
		GetGlyphOutline (memHDC, character, GGO_GRAY8_BITMAP, &metrics, bufsize, gbitmap, &m2);

		bitmap_left = metrics.gmptGlyphOrigin.x;
		bitmap_top = metrics.gmptGlyphOrigin.y;
		width = metrics.gmBlackBoxX;
		rows = metrics.gmBlackBoxY;
		advancex = metrics.gmCellIncX;
		height = otm->otmTextMetrics.tmHeight;
		ascender = otm->otmAscent;
		pitch = (width + 3) & ~3;
	}
	result->bitmap = gbitmap;
	result->left = bitmap_left;
	result->top = bitmap_top;
	result->width = width;
	result->rows = rows;
	result->advance = advancex;
	result->ascender = ascender;
	result->pitch = pitch;
	result->overscore_offset = -otm->otmTextMetrics.tmDescent;
	result->overscore_double_offset = -5;
	return true;
}

void host_printer_release_glyph(host_printer_glyph *glyph)
{
	xfree(glyph->bitmap);
	glyph->bitmap = NULL;
}

bool host_printer_output_page(uae_u8 *Tpage, uae_u8 *Tcpage, int Tpage_w,
	int Tpage_h, int Tpage_pitch, bool TcolorPrinter, host_printer_color getcolor)
{
	Bit16u x, y;
	HDC TmemHDC = memHDC;
	if (!multiPageCounter) {
		docinfo.cbSize = sizeof(docinfo);
		docinfo.lpszDocName = _T("WinUAE Epson Printer");
		docinfo.lpszOutput = NULL;
		docinfo.lpszDatatype = NULL;
		docinfo.fwType = 0;

		StartDoc(printerDC, &docinfo);
		multiPageCounter = 1;
		write_log(_T("EPSONPRINTER new document\n"));
	} else {
		multiPageCounter++;
	}
	HDC TprinterDC = printerDC;

	if (TprinterDC)
	{
		int hz = GetDeviceCaps (TprinterDC, PHYSICALWIDTH);
		int vz = GetDeviceCaps (TprinterDC, PHYSICALHEIGHT);
		int topmargin = GetDeviceCaps (TprinterDC, PHYSICALOFFSETX);
		int leftmargin = GetDeviceCaps (TprinterDC, PHYSICALOFFSETY);
		HDC dc = NULL;

		write_log (_T("EPSONPRINTER: page=%d, HP=%d WP=%d TM=%d LM=%d W=%d H=%d\n"),
			multiPageCounter,
			hz, vz, topmargin, leftmargin, Tpage_w, Tpage_h);

		if (TcolorPrinter)
			dc = GetDC (NULL);
		HBITMAP bitmap = CreateCompatibleBitmap (dc ? dc : TmemHDC, Tpage_w, Tpage_h);
		HGDIOBJ hobj = SelectObject (TmemHDC, bitmap);
		BitBlt (TmemHDC, 0, 0, Tpage_w, Tpage_h, NULL, 0, 0, WHITENESS);

		StartPage (TprinterDC);

		// this really needs to use something else than SetPixel()..
		for (y=0; y<Tpage_h; y++)
		{
			for (x=0; x<Tpage_w; x++)
			{
				Bit8u r, g, b;
				getcolor (Tpage, Tcpage, x, y, Tpage_pitch, &r, &g, &b);
				if (r != 255 || g != 255 || b != 255)
					SetPixel (TmemHDC, x, y, RGB(r, g, b));
			}
		}

		BitBlt (TprinterDC, leftmargin, topmargin, Tpage_w, Tpage_h, TmemHDC, 0, 0, SRCCOPY);

		EndPage (TprinterDC);

		SelectObject(TmemHDC, hobj);

		DeleteObject (bitmap);
		if (dc)
			ReleaseDC (NULL, dc);

	}
	return TprinterDC != NULL;
}

void host_printer_end_document()
{
	write_log(_T("EPSONPRINTER: background thread end request %p %d\n"), printerDC, multiPageCounter);
	if (printerDC) {
		if (multiPageCounter > 0) {
			write_log(_T("EPSONPRINTER end document\n"));
			EndDoc(printerDC);
		}
		DeleteDC(printerDC);
	}
	printerDC = NULL;
	if (memHDC)
		DeleteDC(memHDC);
	memHDC = NULL;
	multiPageCounter = 0;
}

void host_printer_preview_file(const TCHAR *name)
{
	ShellExecute(NULL, _T("open"), name, NULL, NULL, SW_SHOWNORMAL);
}
