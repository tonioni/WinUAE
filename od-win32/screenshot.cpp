
#define PNG_SCREENSHOTS 1

#include <windows.h>
#include <ddraw.h>

#include <stdio.h>

#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "custom.h"
#include "dxwrap.h"
#include "win32.h"
#include "win32gfx.h"
#include "direct3d.h"
#include "opengl.h"
#include "registry.h"
#include "gfxfilter.h"
#include "xwin.h"

#include "png.h"

int screenshotmode = PNG_SCREENSHOTS;
int screenshot_originalsize = 0;

static void namesplit (TCHAR *s)
{
	int l;

	l = _tcslen (s) - 1;
	while (l >= 0) {
		if (s[l] == '.')
			s[l] = 0;
		if (s[l] == '\\' || s[l] == '/' || s[l] == ':' || s[l] == '?') {
			l++;
			break;
		}
		l--;
	}
	if (l > 0)
		memmove (s, s + l, (_tcslen (s + l) + 1) * sizeof (TCHAR));
}

static int toclipboard (BITMAPINFO *bi, void *bmp)
{
	int v = 0;
	uae_u8 *dib = 0;
	HANDLE hg;

	if (!OpenClipboard (hMainWnd))
		return v;
	EmptyClipboard ();
	hg = GlobalAlloc (GMEM_MOVEABLE | GMEM_DDESHARE, bi->bmiHeader.biSize + bi->bmiHeader.biSizeImage);
	if (hg) {
		dib = (uae_u8*)GlobalLock (hg);
		if (dib) {
			memcpy (dib, &bi->bmiHeader, bi->bmiHeader.biSize);
			memcpy (dib + bi->bmiHeader.biSize, bmp, bi->bmiHeader.biSizeImage);
		}
		GlobalUnlock (hg);
		if (SetClipboardData (CF_DIB, hg))
			v = 1;
	}
	CloseClipboard ();
	if (!v)
		GlobalFree (hg);
	return v;
}

static HDC surface_dc, offscreen_dc;
static BITMAPINFO *bi; // bitmap info
static LPVOID lpvBits = NULL; // pointer to bitmap bits array
static HBITMAP offscreen_bitmap;
static int screenshot_prepared;

void screenshot_free (void)
{
	if (surface_dc)
		releasehdc (surface_dc);
	surface_dc = NULL;
	if(offscreen_dc)
		DeleteDC(offscreen_dc);
	offscreen_dc = NULL;
	if(offscreen_bitmap)
		DeleteObject(offscreen_bitmap);
	offscreen_bitmap = NULL;
	if(lpvBits)
		free(lpvBits);
	lpvBits = NULL;
	screenshot_prepared = FALSE;
}

static int rgb_rb, rgb_gb, rgb_bb, rgb_rs, rgb_gs, rgb_bs;

static int screenshot_prepare (int imagemode, struct vidbuffer *vb)
{
	int width, height;
	HGDIOBJ hgdiobj;
	int bits;

	screenshot_free ();

	regqueryint (NULL, _T("Screenshot_Original"), &screenshot_originalsize);
	if (imagemode < 0)
		imagemode = screenshot_originalsize;

	if (imagemode) {
		int spitch, dpitch, x, y;
		uae_u8 *src, *dst, *mem;
		bool needfree = false;
		uae_u8 *palette = NULL;
		int rgb_bb2, rgb_gb2, rgb_rb2;
		int rgb_bs2, rgb_gs2, rgb_rs2;
		uae_u8 pal[256 * 3];
		
		if (WIN32GFX_IsPicassoScreen ()) {
			src = mem = getrtgbuffer (&width, &height, &spitch, &bits, pal);
			needfree = true;
			rgb_bb2 = 8;
			rgb_gb2 = 8;
			rgb_rb2 = 8;
			rgb_bs2 = 0;
			rgb_gs2 = 8;
			rgb_rs2 = 16;
		} else if (vb) {
			width = vb->outwidth;
			height = vb->outheight;
			spitch = vb->rowbytes;
			bits = vb->pixbytes * 8;
			src = mem = vb->bufmem;
			rgb_bb2 = rgb_bb;
			rgb_gb2 = rgb_gb;
			rgb_rb2 = rgb_rb;
			rgb_bs2 = rgb_bs;
			rgb_gs2 = rgb_gs;
			rgb_rs2 = rgb_rs;
		} else {
			src = mem = getfilterbuffer (&width, &height, &spitch, &bits);
			needfree = true;
			rgb_bb2 = rgb_bb;
			rgb_gb2 = rgb_gb;
			rgb_rb2 = rgb_rb;
			rgb_bs2 = rgb_bs;
			rgb_gs2 = rgb_gs;
			rgb_rs2 = rgb_rs;
		}
		if (src == NULL)
			goto donormal;
		if (width == 0 || height == 0) {
			if (needfree) {
				if (WIN32GFX_IsPicassoScreen())
					freertgbuffer(mem);
				else
					freefilterbuffer(mem);
			}
			goto donormal;
		}
		ZeroMemory (bi, sizeof(bi));
		bi->bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
		bi->bmiHeader.biWidth = width;
		bi->bmiHeader.biHeight = height;
		bi->bmiHeader.biPlanes = 1;
		bi->bmiHeader.biBitCount = bits <= 8 ? 8 : 24;
		bi->bmiHeader.biCompression = BI_RGB;
		dpitch = ((bi->bmiHeader.biWidth * bi->bmiHeader.biBitCount + 31) & ~31) / 8;
		bi->bmiHeader.biSizeImage = dpitch * bi->bmiHeader.biHeight;
		bi->bmiHeader.biXPelsPerMeter = 0;
		bi->bmiHeader.biYPelsPerMeter = 0;
		bi->bmiHeader.biClrUsed = bits <= 8 ? (1 << bits) : 0;
		bi->bmiHeader.biClrImportant = 0;
		if (bits <= 8) {
			for (int i = 0; i < bi->bmiHeader.biClrUsed; i++) {
				bi->bmiColors[i].rgbRed = pal[i * 3  + 0];
				bi->bmiColors[i].rgbGreen = pal[i * 3  + 1];
				bi->bmiColors[i].rgbBlue = pal[i * 3  + 2];
			}
		}
		if (!(lpvBits = xmalloc(uae_u8, bi->bmiHeader.biSizeImage))) {
			if (needfree) {
				if (WIN32GFX_IsPicassoScreen())
					freertgbuffer(mem);
				else
					freefilterbuffer(mem);
			}
			goto oops;
		}
		dst = (uae_u8*)lpvBits + (height - 1) * dpitch;
		if (bits <=8) {
			for (y = 0; y < height; y++) {
				memcpy (dst, src, width);
				src += spitch;
				dst -= dpitch;
			}
		} else {
			for (y = 0; y < height; y++) {
				for (x = 0; x < width; x++) {
					int shift;
					uae_u32 v = 0;
					uae_u32 v2;

					if (bits == 16)
						v = ((uae_u16*)src)[x];
					else if (bits == 32)
						v = ((uae_u32*)src)[x];

					shift = 8 - rgb_bb2;
					v2 = (v >> rgb_bs2) & ((1 << rgb_bb2) - 1);
					v2 <<= shift;
					if (rgb_bb2 < 8)
						v2 |= (v2 >> shift) & ((1 < shift) - 1);
					dst[x * 3 + 0] = v2;

					shift = 8 - rgb_gb2;
					v2 = (v >> rgb_gs2) & ((1 << rgb_gb2) - 1);
					v2 <<= (8 - rgb_gb2);
					if (rgb_gb < 8)
						v2 |= (v2 >> shift) & ((1 < shift) - 1);
					dst[x * 3 + 1] = v2;

					shift = 8 - rgb_rb2;
					v2 = (v >> rgb_rs2) & ((1 << rgb_rb2) - 1);
					v2 <<= (8 - rgb_rb2);
					if (rgb_rb < 8)
						v2 |= (v2 >> shift) & ((1 < shift) - 1);
					dst[x * 3 + 2] = v2;

				}
				src += spitch;
				dst -= dpitch;
			}
		}
		if (needfree) {
			if (WIN32GFX_IsPicassoScreen())
				freertgbuffer(mem);
			else
				freefilterbuffer(mem);
		}

	} else {
donormal:
		width = WIN32GFX_GetWidth ();
		height = WIN32GFX_GetHeight ();

		surface_dc = gethdc ();
		if (surface_dc == NULL)
			goto oops;

		// need a HBITMAP to convert it to a DIB
		if ((offscreen_bitmap = CreateCompatibleBitmap (surface_dc, width, height)) == NULL)
			goto oops; // error

		// The bitmap is empty, so let's copy the contents of the surface to it.
		// For that we need to select it into a device context.
		if ((offscreen_dc = CreateCompatibleDC (surface_dc)) == NULL)
			goto oops; // error

		// select offscreen_bitmap into offscreen_dc
		hgdiobj = SelectObject (offscreen_dc, offscreen_bitmap);

		// now we can copy the contents of the surface to the offscreen bitmap
		BitBlt (offscreen_dc, 0, 0, width, height, surface_dc, 0, 0, SRCCOPY);

		// de-select offscreen_bitmap
		SelectObject (offscreen_dc, hgdiobj);

		ZeroMemory (bi, sizeof(bi));
		bi->bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
		bi->bmiHeader.biWidth = width;
		bi->bmiHeader.biHeight = height;
		bi->bmiHeader.biPlanes = 1;
		bi->bmiHeader.biBitCount = 24;
		bi->bmiHeader.biCompression = BI_RGB;
		bi->bmiHeader.biSizeImage = (((bi->bmiHeader.biWidth * bi->bmiHeader.biBitCount + 31) & ~31) / 8) * bi->bmiHeader.biHeight;
		bi->bmiHeader.biXPelsPerMeter = 0;
		bi->bmiHeader.biYPelsPerMeter = 0;
		bi->bmiHeader.biClrUsed = 0;
		bi->bmiHeader.biClrImportant = 0;

		// Reserve memory for bitmap bits
		if (!(lpvBits = xmalloc (uae_u8, bi->bmiHeader.biSizeImage)))
			goto oops; // out of memory

		// Have GetDIBits convert offscreen_bitmap to a DIB (device-independent bitmap):
		if (!GetDIBits (offscreen_dc, offscreen_bitmap, 0, bi->bmiHeader.biHeight, lpvBits, bi, DIB_RGB_COLORS))
			goto oops; // GetDIBits FAILED

		releasehdc (surface_dc);
		surface_dc = NULL;
	}
	screenshot_prepared = TRUE;
	return 1;

oops:
	screenshot_free ();
	return 0;
}

int screenshot_prepare (struct vidbuffer *vb)
{
	return screenshot_prepare (1, vb);
}
int screenshot_prepare (int imagemode)
{
	return screenshot_prepare (imagemode, NULL);
}
int screenshot_prepare (void)
{
	return screenshot_prepare (-1);
}

void Screenshot_RGBinfo (int rb, int gb, int bb, int rs, int gs, int bs)
{
	if (!bi)
		bi = xcalloc (BITMAPINFO, sizeof(BITMAPINFO) + 256 * sizeof(RGBQUAD));
	rgb_rb = rb;
	rgb_gb = gb;
	rgb_bb = rb;
	rgb_rs = rs;
	rgb_gs = gs;
	rgb_bs = bs;
}

#if PNG_SCREENSHOTS > 0

static void _cdecl pngtest_blah (png_structp png_ptr, png_const_charp message)
{
#if 1
	if (message) {
		TCHAR *msg = au(message);
		write_log (_T("libpng warning: '%s'\n"), msg);
		xfree (msg);
	}
#endif
}

static int savepng (FILE *fp)
{
	png_structp png_ptr;
	png_infop info_ptr;
	png_bytep *row_pointers;
	int h = bi->bmiHeader.biHeight;
	int w = bi->bmiHeader.biWidth;
	int d = bi->bmiHeader.biBitCount;
	png_color pngpal[256];
	int i;

	png_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING, pngtest_blah, pngtest_blah, pngtest_blah);
	if (!png_ptr)
		return 1;
	info_ptr = png_create_info_struct (png_ptr);
	if (!info_ptr) {
		png_destroy_write_struct (&png_ptr, NULL);
		return 2;
	}
	if (setjmp(png_jmpbuf (png_ptr))) {
		png_destroy_write_struct (&png_ptr, &info_ptr);
		return 3;
	}

	png_init_io (png_ptr, fp);
	png_set_filter (png_ptr, 0, PNG_FILTER_NONE);
	png_set_IHDR (png_ptr, info_ptr,
		w, h, 8, d <= 8 ? PNG_COLOR_TYPE_PALETTE : PNG_COLOR_TYPE_RGB,
		PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	if (d <= 8) {
		for (i = 0; i < (1 << d); i++) {
			pngpal[i].red = bi->bmiColors[i].rgbRed;
			pngpal[i].green = bi->bmiColors[i].rgbGreen;
			pngpal[i].blue = bi->bmiColors[i].rgbBlue;
		}
		png_set_PLTE (png_ptr, info_ptr, pngpal, 1 << d);
	}
	row_pointers = xmalloc (png_bytep, h);
	for (i = 0; i < h; i++) {
		int j = h - i - 1;
		row_pointers[i] = (uae_u8*)lpvBits + j * (((w * (d <= 8 ? 8 : 24) + 31) & ~31) / 8);
	}
	png_set_rows (png_ptr, info_ptr, row_pointers);
	png_write_png (png_ptr,info_ptr, PNG_TRANSFORM_BGR, NULL);
	png_destroy_write_struct (&png_ptr, &info_ptr);
	xfree (row_pointers);
	return 0;
}
#endif

static int savebmp (FILE *fp)
{
	BITMAPFILEHEADER bfh;
	// write the file header, bitmap information and pixel data
	bfh.bfType = 19778;
	bfh.bfSize = sizeof (BITMAPFILEHEADER) + sizeof (BITMAPINFOHEADER) + (bi->bmiHeader.biClrUsed * sizeof(RGBQUAD)) + bi->bmiHeader.biSizeImage;
	bfh.bfReserved1 = 0;
	bfh.bfReserved2 = 0;
	bfh.bfOffBits = sizeof (BITMAPFILEHEADER) + sizeof (BITMAPINFOHEADER) + bi->bmiHeader.biClrUsed * sizeof(RGBQUAD);
	if (fwrite (&bfh, 1, sizeof (BITMAPFILEHEADER), fp) < sizeof (BITMAPFILEHEADER))
		return 1; // failed to write bitmap file header
	if (fwrite (bi, 1, sizeof (BITMAPINFOHEADER), fp) < sizeof (BITMAPINFOHEADER))
		return 2; // failed to write bitmap infomation header
	if (bi->bmiHeader.biClrUsed) {
		if (fwrite (bi->bmiColors, 1, bi->bmiHeader.biClrUsed * sizeof(RGBQUAD), fp) < bi->bmiHeader.biClrUsed * sizeof(RGBQUAD))
			return 3; // failed to write bitmap file header
	}
	if (fwrite (lpvBits, 1, bi->bmiHeader.biSizeImage, fp) < bi->bmiHeader.biSizeImage)
		return 4; // failed to write the bitmap
	return 0;
}

/*
Captures the Amiga display (DirectDraw, D3D or OpenGL) surface and saves it to file as a 24bit bitmap.
*/
int screenshotf (const TCHAR *spath, int mode, int doprepare, int imagemode, struct vidbuffer *vb)
{
	static int recursive;
	FILE *fp = NULL;
	int failed = 0;

	HBITMAP offscreen_bitmap = NULL; // bitmap that is converted to a DIB
	HDC offscreen_dc = NULL; // offscreen DC that we can select offscreen bitmap into

	if(recursive)
		return 0;

	recursive++;

	if (vb) {
		if (!screenshot_prepare (vb))
			goto oops;
	} else if (!screenshot_prepared || doprepare) {	
		if (!screenshot_prepare (imagemode))
			goto oops;
	}

	if (mode == 0) {
		toclipboard (bi, lpvBits);
	} else {
		TCHAR filename[MAX_DPATH];
		TCHAR path[MAX_DPATH];
		TCHAR name[MAX_DPATH];
		TCHAR underline[] = _T("_");
		int number = 0;

		if (spath) {
			fp = _tfopen (spath, _T("wb"));
			if (fp) {
#if PNG_SCREENSHOTS > 0
				if (screenshotmode)
					failed = savepng (fp);
				else
#endif
					failed = savebmp (fp);
				fclose (fp);
				fp = NULL;
				goto oops;
			}
		}
		fetch_path (_T("ScreenshotPath"), path, sizeof (path) / sizeof (TCHAR));
		CreateDirectory (path, NULL);
		name[0] = 0;
		if (currprefs.floppyslots[0].dfxtype >= 0)
			_tcscpy (name, currprefs.floppyslots[0].df);
		else if (currprefs.cdslots[0].inuse)
			_tcscpy (name, currprefs.cdslots[0].name);
		if (!name[0])
			underline[0] = 0;
		namesplit (name);

		while(++number < 1000) // limit 999 iterations / screenshots
		{
			_stprintf (filename, _T("%s%s%s%03d.%s"), path, name, underline, number, screenshotmode ? _T("png") : _T("bmp"));
			if ((fp = _tfopen (filename, _T("rb"))) == NULL) // does file not exist?
			{
				int nok = 0;
				if ((fp = _tfopen (filename, _T("wb"))) == NULL) {
					write_log(_T("Screenshot error, can't open \"%s\" err=%d\n"), filename, GetLastError());
					goto oops; // error
				}
#if PNG_SCREENSHOTS > 0
				if (screenshotmode)
					nok = savepng (fp);
				else
#endif
					nok = savebmp (fp);
				fclose(fp);
				if (nok && fp) {
					_tunlink(filename);
				}
				fp = NULL;
				if (nok) {
					write_log(_T("Screenshot error %d ('%s')\n"), nok, filename);
					goto oops;
				}
				write_log (_T("Screenshot saved as \"%s\"\n"), filename);
				failed = 0;
				break;
			}
			fclose (fp);
			fp = NULL;
		}
	}

oops:
	if(fp)
		fclose (fp);

	if (doprepare)
		screenshot_free ();

	recursive--;

	return failed == 0;
}

#include "drawing.h"

void screenshot (int mode, int doprepare)
{
#if 1
	screenshotf (NULL, mode, doprepare, -1, NULL);
#else
	struct vidbuffer vb;
	int w = gfxvidinfo.drawbuffer.inwidth;
	int h = gfxvidinfo.drawbuffer.inheight;
	if (!programmedmode) {
		h = (maxvpos + lof_store - minfirstline) << currprefs.gfx_vresolution;
	}
	if (interlace_seen && currprefs.gfx_vresolution > 0)
		h -= 1 << (currprefs.gfx_vresolution - 1);
	allocvidbuffer (&vb, w, h, gfxvidinfo.drawbuffer.pixbytes * 8);
	set_custom_limits (-1, -1, -1, -1);
	draw_frame (&vb);
	screenshotmode = 0;
	screenshotf (_T("c:\\temp\\1.bmp"), 1, 1, 1, &vb);
	freevidbuffer (&vb);
#endif
}