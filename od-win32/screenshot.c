
#define PNG_SCREENSHOTS 1

#include <windows.h>
#include <ddraw.h>

#include <stdio.h>

#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "custom.h"
#include "picasso96.h"
#include "dxwrap.h"
#include "win32.h"
#include "win32gfx.h"
#include "direct3d.h"
#include "opengl.h"

#include "png.h"

int screenshotmode = PNG_SCREENSHOTS;

static void namesplit (char *s)
{
    int l;
    
    l = strlen (s) - 1;
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
	memmove (s, s + l, strlen (s + l) + 1);
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
        dib = GlobalLock (hg);
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
static BITMAPINFO bi; // bitmap info
static LPVOID lpvBits = NULL; // pointer to bitmap bits array
static HBITMAP offscreen_bitmap;
static int screenshot_prepared;

void screenshot_free(void)
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


int screenshot_prepare(void)
{
    unsigned int width, height;
    HGDIOBJ hgdiobj;

    screenshot_free();

    width = WIN32GFX_GetWidth();
    height = WIN32GFX_GetHeight();

    surface_dc = gethdc ();
    if (surface_dc == NULL)
	goto oops;

    // need a HBITMAP to convert it to a DIB
    if((offscreen_bitmap = CreateCompatibleBitmap(surface_dc, width, height)) == NULL)
    	goto oops; // error
	
    // The bitmap is empty, so let's copy the contents of the surface to it.
    // For that we need to select it into a device context.
    if((offscreen_dc = CreateCompatibleDC(surface_dc)) == NULL)
    	goto oops; // error

    // select offscreen_bitmap into offscreen_dc
    hgdiobj = SelectObject(offscreen_dc, offscreen_bitmap);

    // now we can copy the contents of the surface to the offscreen bitmap
    BitBlt(offscreen_dc, 0, 0, width, height, surface_dc, 0, 0, SRCCOPY);

    // de-select offscreen_bitmap
    SelectObject(offscreen_dc, hgdiobj);
	
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;
    bi.bmiHeader.biSizeImage = (((bi.bmiHeader.biWidth * bi.bmiHeader.biBitCount + 31) & ~31) / 8) * bi.bmiHeader.biHeight;
    bi.bmiHeader.biXPelsPerMeter = 0;
    bi.bmiHeader.biYPelsPerMeter = 0;
    bi.bmiHeader.biClrUsed = 0;
    bi.bmiHeader.biClrImportant = 0;

    // Reserve memory for bitmap bits
    if(!(lpvBits = malloc(bi.bmiHeader.biSizeImage)))
	goto oops; // out of memory

    // Have GetDIBits convert offscreen_bitmap to a DIB (device-independent bitmap):
    if(!GetDIBits(offscreen_dc, offscreen_bitmap, 0, bi.bmiHeader.biHeight, lpvBits, &bi, DIB_RGB_COLORS))
	goto oops; // GetDIBits FAILED

    releasehdc (surface_dc);
    surface_dc = NULL;
    screenshot_prepared = TRUE;
    return 1;

oops:
    screenshot_free();
    return 0;
}

#if PNG_SCREENSHOTS > 0

static void pngtest_blah(png_structp png_ptr, png_const_charp message)
{
   char *name = "unknown";
   if (png_ptr != NULL && png_ptr->error_ptr != NULL)
      name = png_ptr->error_ptr;
   write_log ("%s: libpng warning: %s\n", name, message);
}

static int savepng(FILE *fp)
{
    png_structp png_ptr;
    png_infop info_ptr;
    png_bytep *row_pointers;
    int h = bi.bmiHeader.biHeight;
    int w = bi.bmiHeader.biWidth;
    int i;

    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, pngtest_blah, pngtest_blah, pngtest_blah);
    if (!png_ptr)
       return 0;
    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
       png_destroy_write_struct(&png_ptr, NULL);
       return 0;
    }
    if (setjmp(png_jmpbuf(png_ptr))) {
       png_destroy_write_struct(&png_ptr, &info_ptr);
       return 0;
    }

    png_init_io(png_ptr, fp);
    png_set_filter(png_ptr, 0, PNG_FILTER_NONE); 
    png_set_IHDR(png_ptr, info_ptr,
	w, h, 8, PNG_COLOR_TYPE_RGB,
	PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    row_pointers = xmalloc (h * sizeof(png_bytep*));
    for (i = 0; i < h; i++) {
	int j = h - i - 1;
	row_pointers[i] = (uae_u8*)lpvBits + j * 3 * ((w + 3) & ~3);
    }
    png_set_rows(png_ptr, info_ptr, row_pointers);
    png_write_png(png_ptr,info_ptr, PNG_TRANSFORM_BGR, NULL);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    return 1;
}
#endif

static int savebmp(FILE *fp)
{
    BITMAPFILEHEADER bfh;
    // write the file header, bitmap information and pixel data
    bfh.bfType = 19778;
    bfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + bi.bmiHeader.biSizeImage;
    bfh.bfReserved1 = 0;
    bfh.bfReserved2 = 0;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    if(fwrite(&bfh, 1, sizeof(BITMAPFILEHEADER), fp) < sizeof(BITMAPFILEHEADER))
	return 0; // failed to write bitmap file header
    if(fwrite(&bi, 1, sizeof(BITMAPINFOHEADER), fp) < sizeof(BITMAPINFOHEADER))
	return 0; // failed to write bitmap infomation header
    if(fwrite(lpvBits, 1, bi.bmiHeader.biSizeImage, fp) < bi.bmiHeader.biSizeImage)
	return 0; // failed to write the bitmap
    return 1;
}

/*
Captures the Amiga display (DirectDraw, D3D or OpenGL) surface and saves it to file as a 24bit bitmap.
*/
void screenshot(int mode, int doprepare)
{
    static int recursive;
    FILE *fp = NULL;
	
    HBITMAP offscreen_bitmap = NULL; // bitmap that is converted to a DIB
    HDC offscreen_dc = NULL; // offscreen DC that we can select offscreen bitmap into

    if(recursive)
    	return;
	
    recursive++;

    if (!screenshot_prepared || doprepare) {
        if (!screenshot_prepare())
	    goto oops;
    }

    if (mode == 0) {
        toclipboard (&bi, lpvBits);
    } else {
        char filename[MAX_DPATH];
        char path[MAX_DPATH];
        char name[MAX_DPATH];
        char underline[] = "_";
        int number = 0;
		
    	fetch_path ("ScreenshotPath", path, sizeof (path));
	CreateDirectory (path, NULL);
	name[0] = 0;
	if (currprefs.dfxtype[0] >= 0)
	    strcpy (name, currprefs.df[0]);
	if (!name[0])
	    underline[0] = 0;
	namesplit (name);
		
	while(++number < 1000) // limit 999 iterations / screenshots
	{
	    sprintf(filename, "%s%s%s%03d.%s", path, name, underline, number, screenshotmode ? "png" : "bmp");
	    if((fp = fopen(filename, "rb")) == NULL) // does file not exist?
	    {
		int ok = 0;
		if((fp = fopen(filename, "wb")) == NULL)
		    goto oops; // error
#if PNG_SCREENSHOTS > 0
		if (screenshotmode)
		    ok = savepng (fp);
		else
#endif
		    ok = savebmp (fp);
		fclose(fp);
		fp = NULL;
		if (!ok)
		    goto oops;
		write_log("Screenshot saved as \"%s\"\n", filename);
		break;
	    }
	    fclose (fp);
	    fp = NULL;
	}
    }
	
oops:
    if(fp)
        fclose(fp);
	
    if (doprepare)
        screenshot_free();

    recursive--;
}

