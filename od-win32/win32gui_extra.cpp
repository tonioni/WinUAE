#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <Wingdi.h>
#include <winspool.h>
#include <winuser.h>
#include <mmsystem.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dlgs.h>
#include <process.h>
#include <prsht.h>
#include <richedit.h>
#include <shellapi.h>
#include <Shlobj.h>
#include <shlwapi.h>
#include <ddraw.h>
#include <shobjidl.h>

#include "sysconfig.h"
#include "sysdeps.h"

#include "resource"
#include "registry.h"
#include "win32.h"
#include "win32gui.h"

static int max_w = 800, max_h = 600, mult = 100, pointsize;

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
static wchar_t wfont_vista[] = _T("Segoe UI");
static wchar_t wfont_xp[] = _T("Tahoma");
static wchar_t wfont_old[] = _T("MS Sans Serif");
static TCHAR font_vista[] = _T("Segoe UI");
static TCHAR font_xp[] = _T("Tahoma");

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

static void modifytemplate (DLGTEMPLATEEX *d, DLGTEMPLATEEX_END *d2, int id, int mult)
{

	d->cx = d->cx * mult / 100;
	d->cy = d->cy * mult / 100;
}

static void modifytemplatefont (DLGTEMPLATEEX *d, DLGTEMPLATEEX_END *d2)
{
	wchar_t *p = NULL;

	if (font_vista_ok)
		p = wfont_vista;
	else
		p = wfont_xp;
	if (p && !wcscmp (d2->typeface, wfont_old))
		wcscpy (d2->typeface, p);
}

static void modifyitem (DLGTEMPLATEEX *d, DLGTEMPLATEEX_END *d2, DLGITEMTEMPLATEEX *dt, int id, int mult)
{
	dt->cy = dt->cy * mult / 100;
	dt->cx = dt->cx * mult / 100;
	dt->y = dt->y * mult / 100;
	dt->x = dt->x * mult / 100;
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

struct newresource *scaleresource (struct newresource *res, HWND parent)
{
	DLGTEMPLATEEX *d;
	DLGTEMPLATEEX_END *d2;
	DLGITEMTEMPLATEEX *dt;
	BYTE *p, *p2;
	int i;
	struct newresource *ns;

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
	ns->resource = (LPCDLGTEMPLATEW)xmalloc (uae_u8, ns->size);
	memcpy ((void*)ns->resource, res->resource, ns->size);

	d = (DLGTEMPLATEEX*)ns->resource;
	d2 = (DLGTEMPLATEEX_END*)ns->resource;
	p = (BYTE*)d + sizeof (DLGTEMPLATEEX);
	p = skiptext (p);
	p = skiptext (p);
	p = skiptext (p);
	d2 = (DLGTEMPLATEEX_END*)p;
	p2 = p;
	p2 += sizeof (DLGTEMPLATEEX_END);
	p2 = skiptextone (p2);
	p2 = todword (p2);

	modifytemplatefont (d, d2);

	p += sizeof (DLGTEMPLATEEX_END);
	p = skiptextone (p);
	p = todword (p);

	if (p != p2)
		memmove (p, p2, ns->size - (p2 - (BYTE*)ns->resource));

	modifytemplate(d, d2, ns->tmpl, mult);

	for (i = 0; i < d->cDlgItems; i++) {
		dt = (DLGITEMTEMPLATEEX*)p;
		modifyitem (d, d2, dt, ns->tmpl, mult);
		p += sizeof (DLGITEMTEMPLATEEX);
		p = skiptextone (p);
		p = skiptext (p);
		p += ((WORD*)p)[0];
		p += sizeof (WORD);
		p = todword (p);
	}

	ns->width = d->cx;
	ns->height = d->cy;
	return ns;
}

void freescaleresource (struct newresource *ns)
{
	xfree ((void*)ns->resource);
	xfree (ns);
}

void scaleresource_setmaxsize (int w, int h)
{
	if (os_vista)
		font_vista_ok = 1;
	max_w = w;
	max_h = h;
	mult = 100;
}
