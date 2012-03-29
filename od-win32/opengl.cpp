/*
 * UAE - The Un*x Amiga Emulator
 *
 * OpenGL renderer
 *
 * Copyright 2002 Toni Wilen
 */

#include <windows.h>
#include "sysconfig.h"
#include "sysdeps.h"

#if defined (OPENGL) && defined (GFXFILTER)

#include "options.h"
#include "xwin.h"
#include "dxwrap.h"
#include "opengl.h"
#include "custom.h"
#include "win32.h"
#include "win32gfx.h"
#include "gfxfilter.h"

//#define FSAA

#include <gl\gl.h>
#include <gl\glu.h>
#include <gl\wglext.h>

typedef BOOL (WINAPI * PFNWGLSWAPINTERVALEXTPROC) (int interval);
typedef int (WINAPI * PFNWGLGETSWAPINTERVALEXTPROC) (void);
typedef BOOL (WINAPI * PFNWGLCHOOSEPIXELFORMATARBPROC) (HDC, const int *, const FLOAT *, UINT, int *, UINT *);

/* not defined in MSVC's opengl.h */
#ifndef GL_UNSIGNED_SHORT_5_5_5_1_EXT
#define GL_UNSIGNED_SHORT_5_5_5_1_EXT       0x8034
#endif
#ifndef GL_UNSIGNED_SHORT_4_4_4_4_EXT
#define GL_UNSIGNED_SHORT_4_4_4_4_EXT       0x8033
#endif
#define GL_MULTISAMPLE_ARB 0x809D

static GLint max_texture_size;
static GLint tex[4];
static GLint scanlinetex;
static int total_textures;
static int required_texture_size;
static int required_sl_texture_size;
static GLint ti2d_internalformat, ti2d_format, ti2d_type;
static GLint sl_ti2d_internalformat, sl_ti2d_format, sl_ti2d_type;
static int w_width, w_height, t_width, t_height, t_depth;
static int packed_pixels;
static int doublevsync;
static int ogl_enabled;

static HDC openglhdc;
static HGLRC hrc;
static HWND hwnd;
static PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT;
static PFNWGLGETSWAPINTERVALEXTPROC wglGetSwapIntervalEXT;

static PIXELFORMATDESCRIPTOR pfd;

static void testerror (TCHAR *s)
{
    for (;;) {
	GLint err = glGetError();
	if (err == 0)
	    return;
	write_log (_T("OpenGL error %d (%s)\n"), err, s);
    }
}

static int exact_log2 (int v)
{
    int l = 0;
    while ((v >>= 1) != 0)
	l++;
    return l;
}

static int arbMultisampleSupported;
static int arbMultisampleFormat;

#ifdef FSAA

// WGLisExtensionSupported: This Is A Form Of The Extension For WGL
static int WGLisExtensionSupported(const TCHAR *extension)
{
	const size_t extlen = strlen(extension);
	const TCHAR *supported = NULL;
	const TCHAR *p;

	// Try To Use wglGetExtensionStringARB On Current DC, If Possible
	PROC wglGetExtString = wglGetProcAddress("wglGetExtensionsStringARB");

	if (wglGetExtString)
		supported = ((char*(__stdcall*)(HDC))wglGetExtString)(wglGetCurrentDC());

	// If That Failed, Try Standard Opengl Extensions String
	if (supported == NULL)
		supported = (char*)glGetString(GL_EXTENSIONS);

	// If That Failed Too, Must Be No Extensions Supported
	if (supported == NULL)
		return 0;

	// Begin Examination At Start Of String, Increment By 1 On False Match
	for (p = supported; ; p++)
	{
		// Advance p Up To The Next Possible Match
		p = strstr(p, extension);

		if (p == NULL)
			return 0;															// No Match

		// Make Sure That Match Is At The Start Of The String Or That
		// The Previous Char Is A Space, Or Else We Could Accidentally
		// Match "wglFunkywglExtension" With "wglExtension"

		// Also, Make Sure That The Following Character Is Space Or NULL
		// Or Else "wglExtensionTwo" Might Match "wglExtension"
		if ((p==supported || p[-1]==' ') && (p[extlen]=='\0' || p[extlen]==' '))
			return 1;															// Match
	}
	return 0;
}

// InitMultisample: Used To Query The Multisample Frequencies
static int InitMultisample(HDC hDC, PIXELFORMATDESCRIPTOR *pfd)
{
	PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB;
	int pixelFormat;
	int valid, i;
	UINT numFormats;
	float fAttributes[] = {0,0};
	// These Attributes Are The Bits We Want To Test For In Our Sample
	// Everything Is Pretty Standard, The Only One We Want To
	// Really Focus On Is The SAMPLE BUFFERS ARB And WGL SAMPLES
	// These Two Are Going To Do The Main Testing For Whether Or Not
	// We Support Multisampling On This Hardware.
	int iAttributes[] = {
		WGL_DRAW_TO_WINDOW_ARB,GL_TRUE,
		WGL_SUPPORT_OPENGL_ARB,GL_TRUE,
		WGL_ACCELERATION_ARB,WGL_FULL_ACCELERATION_ARB,
		WGL_COLOR_BITS_ARB, pfd->cDepthBits,
		WGL_ALPHA_BITS_ARB,0,
		WGL_DEPTH_BITS_ARB,0,
		WGL_STENCIL_BITS_ARB,0,
		WGL_DOUBLE_BUFFER_ARB,GL_TRUE,
		WGL_SAMPLE_BUFFERS_ARB,GL_TRUE,
		WGL_SAMPLES_ARB,0,
		0,0
	};

	 // See If The String Exists In WGL!
	if (!WGLisExtensionSupported("WGL_ARB_multisample"))
		return 0;

	// Get Our Pixel Format
	wglChoosePixelFormatARB = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
	if (!wglChoosePixelFormatARB)
		return 0;

	for (i = 8; i >= 2; i -= 2) {
	    iAttributes[19] = i;
	    valid = wglChoosePixelFormatARB(hDC,iAttributes,fAttributes,1,&pixelFormat,&numFormats);
	    // If We Returned True, And Our Format Count Is Greater Than 1
	    if (valid && numFormats >= 1) {
		arbMultisampleSupported = i;
		arbMultisampleFormat = pixelFormat;
		write_log (_T("OPENGL: max FSAA = %d\n"), i);
		return arbMultisampleSupported;
	    }
	}
	// Return The Valid Format
	write_log (_T("OPENGL: no FSAA support detected\n"));
	return  arbMultisampleSupported;
}
#endif

const TCHAR *OGL_init (HWND ahwnd, int w_w, int w_h, int t_w, int t_h, int depth)
{
    int PixelFormat;
    const char *ext1;
    static TCHAR errmsg[100] = { 0 };
    static int init;

    ogl_enabled = 0;
    if (currprefs.gfx_filter != UAE_FILTER_OPENGL) {
	_tcscpy (errmsg, _T("OPENGL: not enabled"));
	return errmsg;
    }

    w_width = w_w;
    w_height = w_h;
    t_width = t_w;
    t_height = t_h;
    t_depth = depth;

    hwnd = ahwnd;
    total_textures = 2;

    if (isfullscreen() > 0 && WIN32GFX_GetDepth (TRUE) < 15) {
	_tcscpy (errmsg, _T("OPENGL: display depth must be at least 15 bit"));
	return errmsg;
    }

    for (;;) {

	memset (&pfd, 0, sizeof (pfd));
	pfd.nSize = sizeof (PIXELFORMATDESCRIPTOR);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER | PFD_TYPE_RGBA;
	pfd.cColorBits = depth;
	pfd.iLayerType = PFD_MAIN_PLANE;

	openglhdc = GetDC (hwnd);

	if (!arbMultisampleSupported) {
	    PixelFormat = ChoosePixelFormat (openglhdc, &pfd);	// Find A Compatible Pixel Format
	    if (PixelFormat == 0) {				// Did We Find A Compatible Format?
		_tcscpy (errmsg, _T("OPENGL: can't find suitable pixelformat"));
		return errmsg;
	    }
	} else {
	    PixelFormat = arbMultisampleFormat;
	}

	if (!SetPixelFormat (openglhdc, PixelFormat, &pfd)) {
	    _stprintf (errmsg, _T("OPENGL: can't set pixelformat %x"), PixelFormat);
	    return errmsg;
	}

	if (!(hrc = wglCreateContext (openglhdc))) {
	    _tcscpy (errmsg, _T("OPENGL: can't create gl rendering context"));
	    return errmsg;
	}

	if (!wglMakeCurrent (openglhdc, hrc)) {
	    _tcscpy (errmsg, _T("OPENGL: can't activate gl rendering context"));
	    return errmsg;
	}
#ifdef FSAA
	if(!arbMultisampleSupported) {
	    if(InitMultisample(openglhdc, &pfd)) {
		OGL_free ();
		_tcscpy (errmsg, "*");
		return errmsg;
	    }
	}
#endif
	break;
    }

    glGetIntegerv (GL_MAX_TEXTURE_SIZE, &max_texture_size);
    required_texture_size = 2 << exact_log2 (t_width > t_height ? t_width : t_height);
    if (max_texture_size < t_width || max_texture_size < t_height) {
	_stprintf (errmsg, _T("OPENGL: %d * %d or bigger texture support required\nYour gfx card's maximum texture size is only %d * %d"),
	    required_texture_size, required_texture_size, max_texture_size, max_texture_size);
	return errmsg;
    }
    required_sl_texture_size = 2 << exact_log2 (w_width > w_height ? w_width : w_height);
    if (currprefs.gfx_filter_scanlines > 0 && (max_texture_size < w_width || max_texture_size < w_height)) {
	gui_message (_T("OPENGL: %d * %d or bigger texture support required for scanlines (max is only %d * %d)\n")
	    _T("Scanlines disabled."),
	    required_sl_texture_size, required_sl_texture_size, max_texture_size, max_texture_size);
	changed_prefs.gfx_filter_scanlines = currprefs.gfx_filter_scanlines = 0;
    }

    ext1 = glGetString (GL_EXTENSIONS);
    if (!init)
	write_log (_T("OpenGL extensions: %s\n"), ext1);
    if (strstr (ext1, "EXT_packed_pixels"))
	packed_pixels = 1;
    if (strstr (ext1, "WGL_EXT_swap_control")) {
	wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress ("wglSwapIntervalEXT");
	wglGetSwapIntervalEXT = (PFNWGLGETSWAPINTERVALEXTPROC)wglGetProcAddress ("wglGetSwapIntervalEXT");
	if (!wglGetSwapIntervalEXT || !wglSwapIntervalEXT) {
	    write_log (_T("OPENGL: WGL_EXT_swap_control extension found but no wglGetSwapIntervalEXT or wglSwapIntervalEXT found!?\n"));
	    wglSwapIntervalEXT = 0;
	    wglGetSwapIntervalEXT = 0;
	}

    }

    sl_ti2d_internalformat = GL_RGBA4;
    sl_ti2d_format = GL_RGBA;
    sl_ti2d_type = GL_UNSIGNED_SHORT_4_4_4_4_EXT;
    ti2d_type = -1;
    if (depth == 15 || depth == 16) {
	if (!packed_pixels) {
	    _stprintf (errmsg, _T("OPENGL: can't use 15/16 bit screen depths because\n")
		_T("EXT_packed_pixels extension was not found."));
	    OGL_free ();
	    return errmsg;
	}
	ti2d_internalformat = GL_RGB5_A1;
	ti2d_format = GL_RGBA;
	ti2d_type = GL_UNSIGNED_SHORT_5_5_5_1_EXT;
    }
    if (depth == 32) {
	ti2d_internalformat = GL_RGBA;
	ti2d_format = GL_RGBA;
	ti2d_type = GL_UNSIGNED_BYTE;
	if (!packed_pixels) {
	    sl_ti2d_internalformat = GL_RGBA;
	    sl_ti2d_format = GL_RGBA;
	    sl_ti2d_type = GL_UNSIGNED_BYTE;
	}
    }
    if (ti2d_type < 0) {
	_stprintf (errmsg, _T("OPENGL: Only 15, 16 or 32 bit screen depths supported (was %d)"), depth);
	OGL_free ();
	return errmsg;
    }

    glGenTextures (total_textures, tex);

    /* "bitplane" texture */
    glBindTexture (GL_TEXTURE_2D, tex [0]);
    glTexImage2D (GL_TEXTURE_2D, 0, ti2d_internalformat,
	required_texture_size, required_texture_size,0,  ti2d_format, ti2d_type, 0);

    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    glClearColor (0.0, 0.0, 0.0, 0.0);
    glShadeModel (GL_FLAT);
    glDisable (GL_DEPTH_TEST);
    glEnable (GL_TEXTURE_2D);
    glDisable (GL_LIGHTING);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    ogl_enabled = 1;
    OGL_resize (w_width, w_height);
    OGL_refresh ();
    init = 1;

    write_log (_T("OPENGL: using texture depth %d texture size %d * %d scanline texture size %d * %d\n"),
	depth, required_texture_size, required_texture_size, required_sl_texture_size, required_sl_texture_size);
    return 0;
}

static void createscanlines (int force)
{
    int x, y, yy;
    uae_u8 *sld, *p;
    int sl4, sl8, sl42, sl82;
    int l1, l2;
    static int osl1, osl2, osl3;

    if (osl1 == currprefs.gfx_filter_scanlines && osl3 == currprefs.gfx_filter_scanlinelevel && osl2 == currprefs.gfx_filter_scanlineratio && !force)
	return;
    osl1 = currprefs.gfx_filter_scanlines;
    osl3 = currprefs.gfx_filter_scanlinelevel;
    osl2 = currprefs.gfx_filter_scanlineratio;
    if (!currprefs.gfx_filter_scanlines) {
	glDisable (GL_BLEND);
	return;
    }

    glEnable (GL_BLEND);
    scanlinetex = tex[total_textures - 1];
    glBindTexture (GL_TEXTURE_2D, scanlinetex);
    glTexImage2D (GL_TEXTURE_2D, 0, sl_ti2d_internalformat,
	required_sl_texture_size, required_sl_texture_size, 0, sl_ti2d_format, sl_ti2d_type, 0);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    sl4 = currprefs.gfx_filter_scanlines * 16 / 100;
    sl8 = currprefs.gfx_filter_scanlines * 256 / 100;
    sl42 = currprefs.gfx_filter_scanlinelevel * 16 / 100;
    sl82 = currprefs.gfx_filter_scanlinelevel * 256 / 100;
    if (sl4 > 15)
	sl4 = 15;
    if (sl8 > 255)
	sl8 = 255;
    if (sl42 > 15)
	sl42 = 15;
    if (sl82 > 255)
	sl82 = 255;
    sld = xcalloc (w_width * w_height * 4, 1);
    l1 = currprefs.gfx_filter_scanlineratio & 15;
    l2 = currprefs.gfx_filter_scanlineratio >> 4;
    if (!l1) l1 = 1;
    if (!l2) l2 = 1;
    for (y = 1; y < w_height; y += l1 + l2) {
	for (yy = 0; yy < l2 && y + yy < w_height; yy++) {
	    for (x = 0; x < w_width; x++) {
		if (packed_pixels) {
		    /* 16-bit, R4G4B4A4 */
		    uae_u8 sll = sl42;
		    p = &sld[((y + yy) * w_width + x) * 2];
		    p[0] = sl4 | (sll << 4);
		    p[1] = (sll << 4) | (sll << 0);
		} else {
		    /* 32-bit, R8G8B8A8 */
		    p = &sld[((y + yy) * w_width + x) * 4];
		    p[0] = p[1] = p[2] = sl82;
		    p[3] = sl8;
		}
	    }
	}
    }
    glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, w_width, w_height, sl_ti2d_format, sl_ti2d_type, sld);
    xfree (sld);
}

static void setfilter (void)
{
    int filtering;
    switch (currprefs.gfx_filter_filtermode & 1)
    {
	case 0:
	filtering = GL_NEAREST;
	break;
	case 1:
	default:
	filtering = GL_LINEAR;
	break;
    }
    if (currprefs.gfx_filter_scanlines > 0) {
	glBindTexture (GL_TEXTURE_2D, scanlinetex);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filtering);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filtering);
    }
    glBindTexture (GL_TEXTURE_2D, tex[0]);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filtering);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filtering);
}

static void OGL_swapinterval (void)
{
    doublevsync = 0;
    if (wglSwapIntervalEXT) {
	int i1, i2;
	i1 = (currprefs.gfx_avsync > 0 && isfullscreen() > 0) ? (abs (currprefs.gfx_refreshrate) > 85 ? 2 : 1) : 0;
	if (currprefs.turbo_emulation)
	    i1 = 0;
	wglSwapIntervalEXT (i1);
	i2 = wglGetSwapIntervalEXT ();
	if (i1 == 2 && i2 < i1) /* did display driver support SwapInterval == 2 ? */
	    doublevsync = 1; /* nope, we must wait for vblank twice */
    }
}

void OGL_resize (int width, int height)
{
    if (!ogl_enabled)
	return;
    w_width = width;
    w_height = height;
    glViewport (0, 0, w_width, w_height);
    glMatrixMode (GL_PROJECTION);
    glLoadIdentity ();
    glOrtho (0.0f, w_width, w_height, 0, -1.0f, 1.0f);
    createscanlines (1);
    setfilter ();
    OGL_swapinterval ();
}

static void OGL_dorender (int newtex)
{
    uae_u8 *data = gfxvidinfo.bufmem;
    float x1, y1, x2, y2, multx, multy;

#if 1
    RECT sr, dr, zr;
    float w, h;
    float dw, dh;

    getfilterrect2 (&dr, &sr, &zr, w_width, w_height, t_width, t_height, 1, t_width, t_height);
//    write_log (_T("(%d %d %d %d) - (%d %d %d %d) (%d %d)\n"),
//	dr.left, dr.top, dr.right, dr.bottom, sr.left, sr.top, sr.right, sr.bottom, zr.left, zr.top);
    dw = dr.right - dr.left;
    dh = dr.bottom - dr.top;
    w = sr.right - sr.left;
    h = sr.bottom - sr.top;

    multx = dw * t_width / w_width;
    multy = dh * t_height / w_height;
//    write_log (_T("%fx%f\n"), multx, multy);

    x1 = -0.5f + dw * t_width / w_width / 2 - zr.left - sr.left;
    y1 =  0.5f + dh * t_height / w_height / 2 - zr.top - (t_height - 2 * zr.top - h) + sr.top;
    x1 -= (w * t_width / w_width) / 2;
    y1 -= (h * t_height / w_height) / 2;

    x2 = x1 + dw * t_width / w_width;
    y2 = y1 + dh * t_height / w_height;

    x1 *= (float)required_texture_size / t_width;
    y1 *= (float)required_texture_size / t_height;
    x2 *= (float)required_texture_size / t_width;
    y2 *= (float)required_texture_size / t_height;
#else
    double fx, fy, xm, ym;
    float multx, multy;

    multx = (currprefs.gfx_filter_horiz_zoom + 1000.0) / 1000.;
    if (currprefs.gfx_filter_horiz_zoom_mult)
	multx *= 1000.0 / currprefs.gfx_filter_horiz_zoom_mult;
    multy = (currprefs.gfx_filter_vert_zoom + 1000.0) / 1000.;
    if (currprefs.gfx_filter_vert_zoom_mult)
	multy *= 1000.0 / currprefs.gfx_filter_vert_zoom_mult;

    xm = 2 >> currprefs.gfx_resolution;
    ym = currprefs.gfx_linedbl ? 1 : 2;
    if (w_width >= 1024)
	xm *= 2;
    else if (w_width < 500)
	xm /= 2;
    if (w_height >= 960)
	ym *= 2;
    else if (w_height < 350)
	ym /= 2;
    if (xm < 1)
	xm = 1;
    if (ym < 1)
	ym = 1;
    fx = (t_width * xm - w_width) / 2;
    fy = (t_height * ym - w_height) / 2;

    x1 = (float)(w_width * currprefs.gfx_filter_horiz_offset / 1000.0);
    y1 = (float)(w_height * currprefs.gfx_filter_vert_offset / 1000.0);
    x2 = x1 + (float)((required_texture_size * w_width / t_width) * multx);
    y2 = y1 + (float)((required_texture_size * w_height / t_height) * multy);
    x1 -= fx; y1 -= fy;
    x2 += 2 * fx; y2 += 2 * fy;

#endif

#ifdef FSAA
    glEnable (GL_MULTISAMPLE_ARB);
#endif
    glClear (GL_COLOR_BUFFER_BIT);
    glMatrixMode (GL_MODELVIEW);
    glLoadIdentity ();

    glBindTexture (GL_TEXTURE_2D, tex[0]);
    if (newtex && data + t_width * t_height * t_depth / 8 <= gfxvidinfo.bufmemend)
	glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, t_width, t_height, ti2d_format, ti2d_type, data);

    glBegin (GL_QUADS);
    glTexCoord2f (0, -1.0f); glVertex2f (x1, y1);
    glTexCoord2f (0, 0); glVertex2f (x1, y2);
    glTexCoord2f (1.0f, 0); glVertex2f (x2, y2);
    glTexCoord2f (1.0f, -1.0f); glVertex2f (x2, y1);
    glEnd();

    if (currprefs.gfx_filter_scanlines > 0) {
	float v = (float)required_sl_texture_size;
	glBindTexture (GL_TEXTURE_2D, scanlinetex);
	glBegin (GL_QUADS);
	glTexCoord2f (0, -1.0f); glVertex2f (0, 0);
	glTexCoord2f (0, 0); glVertex2f (0, v);
	glTexCoord2f (1.0f, 0); glVertex2f (v, v);
	glTexCoord2f (1.0f, -1.0f); glVertex2f (v, 0);
	glEnd();
    }
    glFlush ();
#ifdef FSAA
    glDisable (GL_MULTISAMPLE_ARB);
#endif
}

void OGL_render (void)
{
    if (!ogl_enabled)
	return;
    OGL_dorender (1);
    SwapBuffers (openglhdc);
    if (doublevsync) {
	OGL_dorender (0);
	SwapBuffers (openglhdc);
    }
}

void OGL_refresh (void)
{
    if (!ogl_enabled)
	return;
    createscanlines (0);
    setfilter ();
    OGL_swapinterval ();
    OGL_render ();
 }

void OGL_getpixelformat (int depth,int *rb, int *gb, int *bb, int *rs, int *gs, int *bs, int *ab, int *as, int *a)
{
    switch (depth)
    {
	case 32:
	*rb = 8;
	*gb = 8;
	*bb = 8;
	*ab = 8;
	*rs = 0;
	*gs = 8;
	*bs = 16;
	*as = 24;
	*a = 255;
	break;
	case 15:
	case 16:
	*rb = 5;
	*gb = 5;
	*bb = 5;
	*ab = 1;
	*rs = 11;
	*gs = 6;
	*bs = 1;
	*as = 0;
	*a = 1;
	break;
    }
}

void OGL_free (void)
{
    if (hrc) {
	wglMakeCurrent (NULL, NULL);
	wglDeleteContext (hrc);
	hrc = 0;
    }
    if (openglhdc) {
	ReleaseDC (hwnd, openglhdc);
	openglhdc = 0;
    }
    ogl_enabled = 0;
}

HDC OGL_getDC (HDC hdc)
{
    return openglhdc;
}

int OGL_isenabled (void)
{
    return ogl_enabled;
}

#endif
