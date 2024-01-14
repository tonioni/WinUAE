#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <Dwmapi.h>
#include <shellscalingapi.h>
#include <shlwapi.h>
#include <uiautomation.h>
#include "Xinput.h"

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "resource.h"
#include "registry.h"
#include "win32.h"
#include "win32gui.h"
#include "xwin.h"
#include "zfile.h"

#include "darkmode.h"

#define MAX_GUI_FONTS 3
#define DEFAULT_FONTSIZE_OLD 8
#define DEFAULT_FONTSIZE_NEW 9

static float multx, multy;
static int scaleresource_width, scaleresource_height;
static int scaleresource_reset;
static int dux, duy;

static TCHAR fontname_gui[32], fontname_list[32], fontname_osd[32];
static int fontsize_default = DEFAULT_FONTSIZE_OLD;
static int fontsize_gui = DEFAULT_FONTSIZE_OLD;
static int fontsize_list = DEFAULT_FONTSIZE_OLD;
static int fontsize_osd = 6;
static int fontstyle_gui = 0;
static int fontstyle_list = 0;
static int fontstyle_osd = 0;
static int fontweight_gui = FW_REGULAR;
static int fontweight_list = FW_REGULAR;
static int fontweight_osd = FW_REGULAR;

static TEXTMETRIC listview_tm;
static const TCHAR *fontprefix;

static IUIAutomation *Automation;

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

static const wchar_t wfont_vista[] = _T("Segoe UI");
static const wchar_t wfont_old[] = _T("MS Sans Serif");
static const TCHAR font_vista[] = _T("Segoe UI");

#define WNDS_DIALOGWINDOW 0X00010000
#define CW_USEDEFAULT16 ((short)0x8000)

/* Dialog control information */
typedef struct
{
	DWORD      style;
	DWORD      exStyle;
	DWORD      helpId;
	short      x;
	short      y;
	short      cx;
	short      cy;
	UINT       id;
	LPCWSTR    className;
	LPCWSTR    windowName;
	BOOL       windowNameFree; // ReactOS
	LPCVOID    data;
} DLG_CONTROL_INFO;


/* MACROS/DEFINITIONS ********************************************************/

#define DF_END  0x0001
#define DF_DIALOGACTIVE 0x4000 // ReactOS
#define GETDLGINFO(res) DIALOG_get_info(res, FALSE)
#define GET_WORD(ptr)  (*(WORD *)(ptr))
#define GET_DWORD(ptr) (*(DWORD *)(ptr))
#define GET_LONG(ptr) (*(const LONG *)(ptr))
#define DLG_ISANSI 2

/***********************************************************************
*               DIALOG_get_info
*
* Get the DIALOGINFO structure of a window, allocating it if needed
* and 'create' is TRUE.
*
* ReactOS
*/
static DIALOGINFO *DIALOG_get_info(struct newresource *res, BOOL create)
{
	DIALOGINFO *dlgInfo;

	dlgInfo = (DIALOGINFO *)&res->dinfo;
	dlgInfo->idResult = IDOK;
	return dlgInfo;
}
static LONG GdiGetCharDimensions(HDC hdc, LPTEXTMETRICW lptm, LONG *height)
{
	SIZE sz;
	TEXTMETRICW tm;
	static const WCHAR alphabet[] =
	{
		'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q',
		'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
		'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 0
	};

	if (!GetTextMetricsW(hdc, &tm)) return 0;

	if (!GetTextExtentPointW(hdc, alphabet, 52, &sz)) return 0;

	if (lptm) *lptm = tm;
	if (height) *height = tm.tmHeight;

	return (sz.cx / 26 + 1) / 2;
}

/***********************************************************************
 *           DIALOG_GetControl32
 *
 * Return the class and text of the control pointed to by ptr,
 * fill the header structure and return a pointer to the next control.
 */
static const WORD *DIALOG_GetControl32(const WORD *p, DLG_CONTROL_INFO *info,
	BOOL dialogEx)
{
	if (dialogEx)
	{
		info->helpId = GET_DWORD(p); p += 2;
		info->exStyle = GET_DWORD(p); p += 2;
		info->style = GET_DWORD(p); p += 2;
	} else
	{
		info->helpId = 0;
		info->style = GET_DWORD(p); p += 2;
		info->exStyle = GET_DWORD(p); p += 2;
	}
	info->x = GET_WORD(p); p++;
	info->y = GET_WORD(p); p++;
	info->cx = GET_WORD(p); p++;
	info->cy = GET_WORD(p); p++;

	if (dialogEx)
	{
		/* id is 4 bytes for DIALOGEX */
		info->id = GET_LONG(p);
		p += 2;
	} else
	{
		info->id = GET_WORD(p);
		p++;
	}

	if (GET_WORD(p) == 0xffff)
	{
		static const WCHAR class_names[6][10] =
		{
			{ 'B', 'u', 't', 't', 'o', 'n', },             /* 0x80 */
			{ 'E', 'd', 'i', 't', },                     /* 0x81 */
			{ 'S', 't', 'a', 't', 'i', 'c', },             /* 0x82 */
			{ 'L', 'i', 's', 't', 'B', 'o', 'x', },         /* 0x83 */
			{ 'S', 'c', 'r', 'o', 'l', 'l', 'B', 'a', 'r', }, /* 0x84 */
			{ 'C', 'o', 'm', 'b', 'o', 'B', 'o', 'x', }      /* 0x85 */
		};
		WORD id = GET_WORD(p + 1);
		/* Windows treats dialog control class ids 0-5 same way as 0x80-0x85 */
		if ((id >= 0x80) && (id <= 0x85)) id -= 0x80;
		if (id <= 5)
		{
			info->className = class_names[id];
		} else
		{
			info->className = NULL;
			/* FIXME: load other classes here? */
			write_log(_T("Unknown built-in class id %04x\n"), id);
		}
		p += 2;
	} else
	{
		info->className = (LPCWSTR)p;
		p += _tcslen(info->className) + 1;
	}

	if (GET_WORD(p) == 0xffff)  /* Is it an integer id? */
	{
		//// ReactOS Rev 6478
		info->windowName = (LPCWSTR)HeapAlloc(GetProcessHeap(), 0, sizeof(L"#65535"));
		if (info->windowName != NULL)
		{
			wsprintf((LPWSTR)info->windowName, L"#%u", GET_WORD(p + 1));
			info->windowNameFree = TRUE;
		} else
		{
			info->windowNameFree = FALSE;
		}
		p += 2;
	} else
	{
		info->windowName = (LPCWSTR)p;
		info->windowNameFree = FALSE;
		p += _tcslen(info->windowName) + 1;
	}
#if 0
	write_log(_T("    %s %s %ld, %d, %d, %d, %d, %08x, %08x, %08x\n"),
		info->className, info->windowName,
		info->id, info->x, info->y, info->cx, info->cy,
		info->style, info->exStyle, info->helpId);
#endif
	if (GET_WORD(p))
	{
		info->data = p;
		p += GET_WORD(p) / sizeof(WORD);
	} else info->data = NULL;
	p++;

	/* Next control is on dword boundary */
	return (const WORD *)(((UINT_PTR)p + 3) & ~3);
}

static BOOL DIALOG_CreateControls32(HWND hwnd, LPCSTR tmpl, const DLG_TEMPLATE *dlgTemplate,
	HINSTANCE hInst, struct newresource *res)
{
	DIALOGINFO *dlgInfo;
	DLG_CONTROL_INFO info;
	HWND hwndCtrl, hwndDefButton = 0;
	INT items = dlgTemplate->nbItems;

	if (!(dlgInfo = GETDLGINFO(res)))
		return FALSE;

	while (items--)
	{
		tmpl = (LPCSTR)DIALOG_GetControl32((const WORD *)tmpl, &info, dlgTemplate->dialogEx);
		info.style &= ~WS_POPUP;
		info.style |= WS_CHILD;

		if (info.style & WS_BORDER)
		{
			info.style &= ~WS_BORDER;
			info.exStyle |= WS_EX_CLIENTEDGE;
		}

		int x = MulDiv(info.x, dlgInfo->xBaseUnit, 4);
		int y = MulDiv(info.y, dlgInfo->yBaseUnit, 8);
		int w = MulDiv(info.cx, dlgInfo->xBaseUnit, 4);
		int h = MulDiv(info.cy, dlgInfo->yBaseUnit, 8);

		hwndCtrl = CreateWindowEx(info.exStyle | WS_EX_NOPARENTNOTIFY,
			info.className, info.windowName,
			info.style | WS_CHILD,
			x, y, w, h,
			hwnd, (HMENU)(ULONG_PTR)info.id,
			hInst, (LPVOID)info.data);

		if (info.windowNameFree)
		{
			HeapFree(GetProcessHeap(), 0, (LPVOID)info.windowName);
		}

		if (!hwndCtrl)
		{
			write_log(_T("control %s %s creation failed\n"), info.className, info.windowName);
			if (dlgTemplate->style & DS_NOFAILCREATE)
				continue;
			return FALSE;
		}

		struct newreswnd *nrw = &res->hwnds[res->hwndcnt++];
		nrw->hwnd = hwndCtrl;
		nrw->x = x;
		nrw->y = y;
		nrw->w = w;
		nrw->h = h;
		if (info.id != IDC_STATIC) {
			int style = info.style & BS_TYPEMASK;
			if (!_tcsicmp(info.className, _T("Button")) ||
				!_tcsicmp(info.className, _T("ComboBox")) ||
				!_tcsicmp(info.className, _T("SysTreeView32")) ||
				!_tcsicmp(info.className, _T("SysListView32")) ||
				!_tcsicmp(info.className, _T("msctls_trackbar32")) ||
				!_tcsicmp(info.className, _T("Edit"))) {		
				if (style == BS_GROUPBOX) {
					nrw->selectable = false;
				} else {
					nrw->selectable = true;
				}
				if (!_tcsicmp(info.className, _T("Edit"))) {
					if (info.style & ES_READONLY)  {
						nrw->selectable = false;
					}
				}
				if (!_tcsicmp(info.className, _T("ComboBox"))) {
					style |= 0x100;
				}
				if (!_tcsicmp(info.className, _T("msctls_trackbar32"))) {
					style |= 0x200;
				}
				if (!_tcsicmp(info.className, _T("SysTreeView32"))) {
					nrw->list = true;
					nrw->region = 3;
					style |= 0x400;
				}
				if (!_tcsicmp(info.className, _T("SysListView32"))) {
					nrw->listn = true;
					nrw->region = 3;
					style |= 0x400;
				}
				nrw->style = style;
				int i = 0;
				nrw->hwndx[i++] = nrw->hwnd;
				if (!_tcsicmp(info.className, _T("ComboBox"))) {
					COMBOBOXINFO pcbi = {};
					pcbi.cbSize = sizeof(COMBOBOXINFO);
					GetComboBoxInfo(nrw->hwnd, &pcbi);
					if (pcbi.hwndItem && pcbi.hwndItem != nrw->hwnd) {
						nrw->hwndx[i++] = pcbi.hwndItem;
					}
					if (pcbi.hwndCombo && pcbi.hwndCombo != nrw->hwnd) {
						nrw->hwndx[i++] = pcbi.hwndCombo;
					}
					if (pcbi.hwndList && pcbi.hwndList != nrw->hwnd) {
						nrw->hwndx[i++] = pcbi.hwndList;
					}
				}
				switch(info.id)
				{
					case IDC_PANELTREE:
					nrw->region = 2;
					break;
					case IDC_RESETAMIGA:
					case IDC_QUITEMU:
					case IDC_RESTARTEMU:
					case IDC_ERRORLOG:
					case IDOK:
					case IDCANCEL:
					case IDHELP:
					nrw->region = 1;
					break;
				}
			}
			//write_log(_T("%p %s %d %08x\n"), hwndCtrl, info.className, info.id, info.style);
		}

		/* Send initialisation messages to the control */
		if (dlgInfo->hUserFont) SendMessage(hwndCtrl, WM_SETFONT,
			(WPARAM)dlgInfo->hUserFont, 0);
		if (SendMessage(hwndCtrl, WM_GETDLGCODE, 0, 0) & DLGC_DEFPUSHBUTTON)
		{
			/* If there's already a default push-button, set it back */
			/* to normal and use this one instead. */
			if (hwndDefButton)
				SendMessage(hwndDefButton, BM_SETSTYLE, BS_PUSHBUTTON, FALSE);
			hwndDefButton = hwndCtrl;
			dlgInfo->idResult = GetWindowLongPtrA(hwndCtrl, GWLP_ID);
		}
		if (g_darkModeSupported) {
			_AllowDarkModeForWindow(hwndCtrl, g_darkModeEnabled);
			if (g_darkModeEnabled) {
				//write_log(_T("%s\n"), info.className);
				if (!_tcsicmp(info.className, _T("Button"))) {
					SubclassButtonControl(hwndCtrl);
				} else if (!_tcsicmp(info.className, _T("ComboBox")) || !_tcsicmp(info.className, _T("Edit"))) {
					SetWindowTheme(hwndCtrl, L"CFD", nullptr);
				} else if (!_tcsicmp(info.className, _T("SysListView32")) || !_tcsicmp(info.className, _T("SysTreeView32"))) {
					//
				} else {
					SetWindowTheme(hwndCtrl, L"Explorer", nullptr);
				}
				SendMessageW(hwndCtrl, WM_THEMECHANGED, 0, 0);
			}
		}
	}
	return TRUE;
}

/***********************************************************************
*           DIALOG_ParseTemplate32
*
* Fill a DLG_TEMPLATE structure from the dialog template, and return
* a pointer to the first control.
*/
static LPCSTR DIALOG_ParseTemplate32(LPCSTR tmpl, DLG_TEMPLATE *result)
{
	const WORD *p = (const WORD *)tmpl;
	WORD signature;
	WORD dlgver;

	dlgver = GET_WORD(p); p++;
	signature = GET_WORD(p); p++;

	if (dlgver == 1 && signature == 0xffff)  /* DIALOGEX resource */
	{
		result->dialogEx = TRUE;
		result->helpId = GET_DWORD(p); p += 2;
		result->exStyle = GET_DWORD(p); p += 2;
		result->style = GET_DWORD(p); p += 2;
	} else
	{
		result->style = GET_DWORD(p - 2);
		result->dialogEx = FALSE;
		result->helpId = 0;
		result->exStyle = GET_DWORD(p); p += 2;
	}
	result->nbItems = GET_WORD(p); p++;
	result->x = GET_WORD(p); p++;
	result->y = GET_WORD(p); p++;
	result->cx = GET_WORD(p); p++;
	result->cy = GET_WORD(p); p++;

	/* Get the menu name */

	switch (GET_WORD(p))
	{
	case 0x0000:
		result->menuName = NULL;
		p++;
		break;
	case 0xffff:
		result->menuName = (LPCWSTR)(UINT_PTR)GET_WORD(p + 1);
		p += 2;
		break;
	default:
		result->menuName = (LPCWSTR)p;
		p += _tcslen(result->menuName) + 1;
		break;
	}

	/* Get the class name */

	switch (GET_WORD(p))
	{
	case 0x0000:
		result->className = WC_DIALOG;
		p++;
		break;
	case 0xffff:
		result->className = (LPCWSTR)(UINT_PTR)GET_WORD(p + 1);
		p += 2;
		break;
	default:
		result->className = (LPCWSTR)p;
		p += _tcslen(result->className) + 1;
		break;
	}

	/* Get the window caption */

	result->caption = (LPCWSTR)p;
	p += _tcslen(result->caption) + 1;

	/* Get the font name */

	result->pointSize = 0;
	result->faceName = NULL;
	result->weight = FW_DONTCARE;
	result->italic = FALSE;

	if (result->style & DS_SETFONT)
	{
		result->pointSize = GET_WORD(p);
		p++;

		/* If pointSize is 0x7fff, it means that we need to use the font
		 * in NONCLIENTMETRICSW.lfMessageFont, and NOT read the weight,
		 * italic, and facename from the dialog template.
		 */
		if (result->pointSize == 0x7fff)
		{
			/* We could call SystemParametersInfo here, but then we'd have
			 * to convert from pixel size to point size (which can be
			 * imprecise).
			 */
		} else
		{
			if (result->dialogEx)
			{
				result->weight = GET_WORD(p); p++;
				result->italic = LOBYTE(GET_WORD(p)); p++;
			}
			result->faceName = (LPCWSTR)p;
			p += _tcslen(result->faceName) + 1;
		}
	}

	/* First control is on dword boundary */
	return (LPCSTR)((((UINT_PTR)p) + 3) & ~3);
}

static int createcontrols(HWND hwnd, struct newresource *res)
{
	LPCVOID dlgTemplate = DIALOG_ParseTemplate32((LPCSTR)res->resource, &res->dtmpl);
	DLG_TEMPLATE *tmpl = &res->dtmpl;

	if (DIALOG_CreateControls32(hwnd, (LPCSTR)dlgTemplate, &res->dtmpl, res->inst, res))
	{
		/* Send initialisation messages and set focus */

		if (res->dlgproc)
		{
			HWND focus = GetNextDlgTabItem(hwnd, 0, FALSE);
			if (!focus)
				focus = GetNextDlgGroupItem(hwnd, 0, FALSE);
			if (SendMessage(hwnd, WM_INITDIALOG, (WPARAM)focus, res->param) && IsWindow(hwnd) &&
				((~tmpl->style & DS_CONTROL) || (tmpl->style & WS_VISIBLE)))
			{
				/* By returning TRUE, app has requested a default focus assignment.
				 * WM_INITDIALOG may have changed the tab order, so find the first
				 * tabstop control again. */
				focus = GetNextDlgTabItem(hwnd, 0, FALSE);
				if (!focus)
					focus = GetNextDlgGroupItem(hwnd, 0, FALSE);
				if (focus)
				{
					if (SendMessage(focus, WM_GETDLGCODE, 0, 0) & DLGC_HASSETSEL)
						SendMessage(focus, EM_SETSEL, 0, MAXLONG);
					SetFocus(focus);
				} else
				{
					if (!(tmpl->style & WS_CHILD))
						SetFocus(hwnd);
				}
			}
			//// ReactOS see 43396, Fixes setting focus on Open and Close dialogs to the FileName edit control in OpenOffice.
			//// This now breaks test_SaveRestoreFocus.
						//DEFDLG_SaveFocus( hwnd );
			////
		}
		//// ReactOS Rev 30613 & 30644
		if (!(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD))
			SendMessage(hwnd, WM_CHANGEUISTATE, MAKEWPARAM(UIS_INITIALIZE, 0), 0);
		////
		if (tmpl->style & WS_VISIBLE && !(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_VISIBLE))
		{
			ShowWindow(hwnd, SW_SHOWNORMAL);   /* SW_SHOW doesn't always work */
			UpdateWindow(hwnd);
		}
		return 1;
	}
	return 0;
}


/***********************************************************************
 *           DIALOG_CreateIndirect
 *       Creates a dialog box window
 *
 *       modal = TRUE if we are called from a modal dialog box.
 *       (it's more compatible to do it here, as under Windows the owner
 *       is never disabled if the dialog fails because of an invalid template)
 */
static HWND DIALOG_CreateIndirect(HINSTANCE hInst, LPCVOID dlgTemplate,
	HWND owner, DLGPROC dlgProc, LPARAM param,
	HWND *modal_owner, struct newresource *res)
{
	HWND hwnd;
	RECT rect;
	POINT pos;
	SIZE size;
	DLG_TEMPLATE *tmpl = &res->dtmpl;
	DIALOGINFO *dlgInfo = NULL;
	DWORD units = GetDialogBaseUnits();
	HWND disabled_owner = NULL;
	HMENU hMenu = 0;
	HFONT hUserFont = 0;
	UINT flags = 0;
	UINT xBaseUnit = LOWORD(units);
	UINT yBaseUnit = HIWORD(units);
	int fontpixels = 8;

	/* Parse dialog template */

	if (!dlgTemplate)
		return 0;
	dlgTemplate = DIALOG_ParseTemplate32((LPCSTR)dlgTemplate, tmpl);

	res->dlgproc = dlgProc;
	res->param = param;

	/* Load menu */

	if (tmpl->menuName)
		hMenu = LoadMenu(hInst, tmpl->menuName);

	/* Create custom font if needed */

	if (tmpl->style & DS_SETFONT)
	{
		HDC dc = GetDC(0);

		if (tmpl->pointSize == 0x7fff)
		{
			/* We get the message font from the non-client metrics */
			NONCLIENTMETRICSW ncMetrics;

			ncMetrics.cbSize = sizeof(NONCLIENTMETRICSW);
			if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS,
				sizeof(NONCLIENTMETRICSW), &ncMetrics, 0))
			{
				hUserFont = CreateFontIndirect(&ncMetrics.lfMessageFont);
			}
		} else
		{
			int xx, yy;
			if (res->parent) {
				xx = res->parent->x + res->parent->width / 2;
				yy = res->parent->y + res->parent->height / 2;
			} else {
				getguipos(&xx, &yy);
				xx += 128;
				yy += 128;
			}
			POINT pt;
			pt.x = xx;
			pt.y = yy;
			HMONITOR m = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
			int dpi = getdpiformonitor(m);
			/* We convert the size to pixels and then make it -ve.  This works
			 * for both +ve and -ve template.pointSize */
			fontpixels = MulDiv(tmpl->pointSize, dpi, 72);
			hUserFont = CreateFont(-fontpixels, 0, 0, 0, tmpl->weight,
				tmpl->italic, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
				PROOF_QUALITY, FF_DONTCARE,
				tmpl->faceName);
			res->fontsize = fontpixels;
		}

		if (hUserFont)
		{
			SIZE charSize;
			HFONT hOldFont = (HFONT)SelectObject(dc, hUserFont);
			charSize.cx = GdiGetCharDimensions(dc, NULL, &charSize.cy);
			if (charSize.cx)
			{
				xBaseUnit = charSize.cx;
				yBaseUnit = charSize.cy;
			}
			SelectObject(dc, hOldFont);
		}
		ReleaseDC(0, dc);
	}

	/* Create dialog main window */

	SetRect(&rect, 0, 0, MulDiv(tmpl->cx, xBaseUnit, 4), MulDiv(tmpl->cy, yBaseUnit, 8));
	if (tmpl->style & DS_CONTROL)
		tmpl->style &= ~(WS_CAPTION | WS_SYSMENU);
	tmpl->style |= DS_3DLOOK;
	if (tmpl->style & DS_MODALFRAME)
		tmpl->exStyle |= WS_EX_DLGMODALFRAME;
	if ((tmpl->style & DS_CONTROL) || !(tmpl->style & WS_CHILD))
		tmpl->exStyle |= WS_EX_CONTROLPARENT;
	AdjustWindowRectEx(&rect, tmpl->style, (hMenu != 0), tmpl->exStyle);
	pos.x = rect.left;
	pos.y = rect.top;
	size.cx = rect.right - rect.left;
	size.cy = rect.bottom - rect.top;

	if (!res->parent && res->width > 0 && res->height > 0) {
		size.cx = res->width;
		size.cy = res->height;
	}

	HMONITOR monitor = 0;
	MONITORINFO mon_info;

	if (tmpl->x == CW_USEDEFAULT16)
	{
		pos.x = pos.y = CW_USEDEFAULT;
	}
	else
	{
		mon_info.cbSize = sizeof(mon_info);
		if (tmpl->style & DS_CENTER)
		{
			if (!owner) {
				monitor = MonitorFromWindow(owner ? owner : GetActiveWindow(), MONITOR_DEFAULTTOPRIMARY);
				GetMonitorInfoW(monitor, &mon_info);
				pos.x = (mon_info.rcWork.left + mon_info.rcWork.right - size.cx) / 2;
				pos.y = (mon_info.rcWork.top + mon_info.rcWork.bottom - size.cy) / 2;
			} else {
				RECT r;
				GetWindowRect(owner, &r);
				pos.x = (r.left + r.right - size.cx) / 2;
				pos.y = (r.top + r.bottom - size.cy) / 2;
			}
		}
		else if (tmpl->style & DS_CENTERMOUSE)
		{
			GetCursorPos(&pos);
			monitor = MonitorFromPoint(pos, MONITOR_DEFAULTTOPRIMARY);
			GetMonitorInfoW(monitor, &mon_info);
		}
		else
		{
			pos.x += MulDiv(tmpl->x, xBaseUnit, 4);
			pos.y += MulDiv(tmpl->y, yBaseUnit, 8);
#if 0
			//
			// REACTOS : Need an owner to be passed!!!
			//
			if (!(tmpl->style & (WS_CHILD | DS_ABSALIGN)) && owner)
				ClientToScreen(owner, &pos);
#endif
		}
	}

	if (tmpl->style & (DS_CENTER | DS_CENTERMOUSE)) {
		POINT pos2 = pos;
		pos2.x += size.cx / 2;
		pos2.y += size.cy / 2;
		monitor = MonitorFromPoint(pos, MONITOR_DEFAULTTOPRIMARY);
		GetMonitorInfoW(monitor, &mon_info);
		if (pos.x + size.cx > mon_info.rcWork.right) {
			pos.x = mon_info.rcWork.right - size.cx;
		}
		if (pos.y + size.cy > mon_info.rcWork.bottom) {
			pos.y = mon_info.rcWork.bottom - size.cy;
		}
		if (pos.x < mon_info.rcWork.left) {
			pos.x = mon_info.rcWork.left;
		}
		if (pos.y < mon_info.rcWork.top) {
			pos.y = mon_info.rcWork.top;
		}
	}

	res->unitx = MulDiv(8, xBaseUnit, 4);
	res->unity = MulDiv(8, yBaseUnit, 8);

	if (!res->parent) {
		int xx, yy;
		getguipos(&xx, &yy);
		pos.x += xx;
		pos.y += yy;
	}

	res->width = size.cx;
	res->height = size.cy;
	res->x = pos.x;
	res->y = pos.y;

	hwnd = CreateWindowEx(tmpl->exStyle, tmpl->className, tmpl->caption,
		tmpl->style & ~WS_VISIBLE, pos.x, pos.y, size.cx, size.cy,
		owner, hMenu, hInst, NULL);

	res->hwnd = hwnd;

	if (!hwnd)
	{
		if (hUserFont)
			DeleteObject(hUserFont);
		if (hMenu)
			DestroyMenu(hMenu);
		if (disabled_owner)
			EnableWindow(disabled_owner, TRUE);
		return 0;
	}

	if (res->parent) {
		struct newreswnd *nrh = &res->parent->hwnds[res->parent->hwndcnt++];
		nrh->hwnd = hwnd;
		nrh->x = pos.x;
		nrh->y = pos.y;
		nrh->w = size.cx;
		nrh->h = size.cy;
	}

	/* moved this from the top of the method to here as DIALOGINFO structure
	will be valid only after WM_CREATE message has been handled in DefDlgProc
	All the members of the structure get filled here using temp variables */
	dlgInfo = DIALOG_get_info(res, TRUE);
	// ReactOS
	if (dlgInfo == NULL)
	{
		if (hUserFont)
			DeleteObject(hUserFont);
		if (hMenu)
			DestroyMenu(hMenu);
		if (disabled_owner)
			EnableWindow(disabled_owner, TRUE);
		return 0;
	}
	//
	dlgInfo->hwndFocus = 0;
	dlgInfo->hUserFont = hUserFont;
	dlgInfo->hMenu = hMenu;
	dlgInfo->xBaseUnit = xBaseUnit;
	dlgInfo->yBaseUnit = yBaseUnit;
	dlgInfo->flags = flags;

	if (tmpl->helpId)
		SetWindowContextHelpId(hwnd, tmpl->helpId);

	SetWindowLongPtrW(hwnd, DWLP_DLGPROC, (ULONG_PTR)dlgProc);

	if (dlgProc && dlgInfo->hUserFont)
		SendMessage(hwnd, WM_SETFONT, (WPARAM)dlgInfo->hUserFont, 0);

	if (g_darkModeSupported) {
		_AllowDarkModeForWindow(hwnd, g_darkModeEnabled);
	}

	/* Create controls */
	if (createcontrols(hwnd, res))
		return hwnd;

	if (disabled_owner) EnableWindow(disabled_owner, TRUE);
	if (IsWindow(hwnd))
	{
		DestroyWindow(hwnd);
	}
	return 0;
}

HWND x_CreateDialogIndirectParam(
	HINSTANCE hInstance,
	LPCDLGTEMPLATE lpTemplate,
	HWND hWndParent,
	DLGPROC lpDialogFunc,
	LPARAM lParamInit,
	struct newresource *res)
{
	return DIALOG_CreateIndirect(hInstance, lpTemplate, hWndParent, lpDialogFunc, lParamInit, NULL, res);
}

void x_DestroyWindow(HWND hwnd, struct newresource *res)
{
	for (int i = 0; res->hwndcnt; i++) {
		struct newreswnd *pr = &res->hwnds[i];
		if (res->hwnds[i].hwnd == hwnd) {
			while (i + 1 < res->hwndcnt) {
				res->hwnds[i] = res->hwnds[i + 1];
				i++;
			}
			res->hwndcnt--;
			res->hwnds[res->hwndcnt].hwnd = NULL;
			res->hwnds[res->hwndcnt].x = res->hwnds[res->hwndcnt].y = res->hwnds[res->hwndcnt].w = res->hwnds[res->hwndcnt].h = 0;
			break;
		}
	}
	DestroyWindow(hwnd);
	if (res->child) {
		struct newresource *cres = res->child;
		if (cres->dinfo.hUserFont) {
			DeleteObject(cres->dinfo.hUserFont);
			cres->dinfo.hUserFont = NULL;
		}
		if (cres->dinfo.hMenu) {
			DeleteObject(cres->dinfo.hMenu);
			cres->dinfo.hMenu = NULL;
		}
	}
}

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

static void modifytemplatefont (DLGTEMPLATEEX *d, DLGTEMPLATEEX_END *d2)
{
	if (!wcscmp (d2->typeface, wfont_old)) {
		wcscpy (d2->typeface, fontname_gui);
		d2->pointsize = fontsize_gui;
		d2->italic = (fontstyle_gui & ITALIC_FONTTYPE) != 0;
		d2->weight = fontweight_gui;
}
}

static void scalechildwindows(struct newresource *nr)
{
	if (!nr)
		return;

	if (nr->fontchanged) {
		SendMessage(nr->hwnd, WM_SETFONT, (WPARAM)nr->dinfo.hUserFont, 0);
	}

	for (int i = 0; i < nr->hwndcnt; i++) {
		struct newreswnd *nw = &nr->hwnds[i];

		int x = (int)(nw->x * multx / 100);

		int y = (int)(nw->y * multy / 100);

		int w = (int)(nw->w * multx / 100);

		int h = (int)(nw->h * multy / 100);

		if (nr->fontchanged) {
			SendMessage(nw->hwnd, WM_SETFONT, (WPARAM)nr->dinfo.hUserFont, 0);
		}
		bool disable = false;
		if (!IsWindowEnabled(nw->hwnd)) {
			EnableWindow(nw->hwnd, TRUE);
			disable = true;
		}
		SetFocus(nw->hwnd);
		if (!SetWindowPos(nw->hwnd, HWND_TOP, x, y, w, h, SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOCOPYBITS | SWP_DEFERERASE)) {
			write_log("scalechildwindows %d/%d: ERROR %d\n", i, nr->hwndcnt, GetLastError());
		}
		if (disable) {
			EnableWindow(nw->hwnd, FALSE);
		}
	}

	HWND hwnd = nr->hwnd;
	HWND focus = GetNextDlgTabItem(hwnd, 0, FALSE);
	if (!focus)
		focus = GetNextDlgGroupItem(hwnd, 0, FALSE);
	if (focus)
	{
		if (SendMessage(focus, WM_GETDLGCODE, 0, 0) & DLGC_HASSETSEL)
			SendMessage(focus, EM_SETSEL, 0, MAXLONG);
		SetFocus(focus);
	} else
	{
		if (!(nr->dtmpl.style & WS_CHILD))
			SetFocus(hwnd);
	}
}

static void scaleresource_setfont(struct newresource *nr, HWND hDlg)
{
	if (!nr)
		return;
	for (int i = 0; i < nr->setparamcnt; i++) {
		HWND hwnd = GetDlgItem(hDlg, nr->setparam_id[i]);
		if (hwnd) {
			int v = (int)SendMessage(hwnd, CB_GETITEMHEIGHT, -1, NULL);
			if (v > 0 && mmy(v) > v)
				SendMessage(hwnd, CB_SETITEMHEIGHT, -1, mmy(v));
		}
	}
}

void rescaleresource(struct newresource *nr, bool full)
{
	if (full) {
		SetWindowRedraw(nr->hwnd, FALSE);
	}

	TITLEBARINFO tbi = { 0 };
	tbi.cbSize = sizeof(TITLEBARINFO);
	GetTitleBarInfo(nr->hwnd, &tbi);

	int height = tbi.rcTitleBar.bottom - tbi.rcTitleBar.top;

	WINDOWINFO pwi = { 0 };
	pwi.cbSize = sizeof(WINDOWINFO);
	GetWindowInfo(nr->hwnd, &pwi);

	float neww = (float)scaleresource_width - pwi.cxWindowBorders * 2;
	float oldw = (530.0f * nr->unitx) / 8.0f;
	multx = neww * 100.0f / oldw;

	float newh = (float)scaleresource_height - height - pwi.cyWindowBorders * 2;
	float oldh = (345.0f * nr->unity) / 8.0f;
	multy = newh * 100.0f / oldh;

	HMONITOR m = MonitorFromWindow(nr->hwnd, MONITOR_DEFAULTTOPRIMARY);
	int dpi = getdpiformonitor(m);
	DLG_TEMPLATE *tmpl = &nr->dtmpl;
	int pixels = MulDiv(tmpl->pointSize, dpi, 72);
	if (pixels != nr->fontsize) {
		nr->fontchanged = true;
		if (nr->dinfo.hUserFont)
			DeleteObject(nr->dinfo.hUserFont);
		nr->dinfo.hUserFont = CreateFont(-pixels, 0, 0, 0, tmpl->weight,
			tmpl->italic, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
			PROOF_QUALITY, FF_DONTCARE, tmpl->faceName);
	}

	dialog_inhibit = 1;
	scalechildwindows(nr);
	if (nr->child) {
		if (nr->fontchanged) {
			if (nr->child->dinfo.hUserFont)
				DeleteObject(nr->child->dinfo.hUserFont);
			nr->child->dinfo.hUserFont = CreateFont(-pixels, 0, 0, 0, tmpl->weight,
				tmpl->italic, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
				PROOF_QUALITY, FF_DONTCARE, tmpl->faceName);
		}
		scalechildwindows(nr->child);

		RECT rf, rpf;
		HWND pf = GetDlgItem(nr->hwnd, IDC_PANEL_FRAME);
		GetClientRect(nr->child->hwnd, &rpf);
		GetClientRect(pf, &rf);
		MapWindowPoints(pf, nr->hwnd, (LPPOINT)&rf, 1);
		SetWindowPos(nr->child->hwnd, HWND_TOP, rf.left + (rf.right - rpf.right) / 2, rf.top + (rf.bottom - rpf.bottom) / 2, 0, 0, SWP_NOSIZE | SWP_NOOWNERZORDER);

		SendMessage(nr->child->hwnd, WM_NEXTDLGCTL, (WPARAM)nr->child->hwnds[0].hwnd, TRUE);

		GetWindowRect(pf, &rf);
		InvalidateRect(nr->hwnd, &rf, TRUE);
	}

	nr->fontsize = pixels;
	nr->fontchanged = false;
	dialog_inhibit = 0;

	if (full) {
		SetWindowRedraw(nr->hwnd, TRUE);
		RedrawWindow(nr->hwnd, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
	}
}

void scaleresource_setsize(int w, int h, int fullscreen)
{
	if (w < 0 || h < 0) {
		return;
	}
	scaleresource_width = w;
	scaleresource_height = h;
}

static int scaleresource2 (struct newresource *res, HWND parent, int resize, int fullscreen, DWORD exstyle, int dlgid)
{
	static int main_width, main_height;

	DLGTEMPLATEEX *d, *s;
	DLGTEMPLATEEX_END *d2, *s2;
	DLGITEMTEMPLATEEX *dt;
	BYTE *p, *p2, *ps, *ps2;

	res->listviewcnt = 0;
	res->setparamcnt = 0;
	res->hwndcnt = 0;

	s = (DLGTEMPLATEEX*)res->sourceresource;

	if (s->dlgVer != 1 || s->signature != 0xffff)
		return 0;
	if (!(s->style & (DS_SETFONT | DS_SHELLFONT)))
		return 0;

	res->size = res->sourcesize + 32;
	res->resource = (LPCDLGTEMPLATEW)xmalloc (uae_u8, res->size);
	memcpy ((void*)res->resource, res->sourceresource, res->sourcesize);

	d = (DLGTEMPLATEEX*)res->resource;
	s = (DLGTEMPLATEEX*)res->sourceresource;

	int width = d->cx;
	int height = d->cy;

	if (resize > 0) {
		d->style &= ~DS_MODALFRAME;
		d->style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
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

	d2 = (DLGTEMPLATEEX_END*)res->resource;
	p = (BYTE*)d + sizeof (DLGTEMPLATEEX);
	p = skiptext (p);
	p = skiptext (p);
	p = skiptext (p);

	s2 = (DLGTEMPLATEEX_END*)res->sourceresource;
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

	size_t remain = ps2 - (BYTE*)res->sourceresource;
	memcpy (p, ps2, res->sourcesize - remain);

	int id2 = 0;
	for (int i = 0; i < d->cDlgItems; i++) {
		dt = (DLGITEMTEMPLATEEX*)p;
		p += sizeof (DLGITEMTEMPLATEEX);
		p = skiptextone (p);
		p = skiptext (p);
		p += ((WORD*)p)[0];
		p += sizeof (WORD);
		p = todword (p);
	}
	return 1;
}

int scaleresource (struct newresource *res, struct dlgcontext *dctx, HWND parent, int resize, int fullscreen, DWORD exstyle, int dlgid)
{
	dctx->dlgstorecnt = 0;
	return scaleresource2(res, parent, resize, fullscreen, exstyle, dlgid);
}

void freescaleresource (struct newresource *ns)
{
	if (!ns)
		return;
	xfree((void*)ns->resource);
	ns->resource = NULL;
	ns->size = 0;
}

int getscaledfontsize(int size, HWND hwnd)
{
	int lm = 72;

	if (size <= 0)
		size = fontsize_gui;

	if (!dpi_aware_v2) {
		HDC hdc = GetDC(hwnd);
		lm = GetDeviceCaps(hdc, LOGPIXELSY);
		ReleaseDC(hwnd, hdc);
	} else if (hwnd) {
		HMONITOR m = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
		lm = getdpiformonitor(m);
	}
	size = -MulDiv(size, lm, 72);
	return size;
}

void scalaresource_listview_font_info(int *w)
{
	*w = listview_tm.tmAveCharWidth;
}

static void setdeffont (void)
{
	int fs = DEFAULT_FONTSIZE_OLD;
	int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

	if (w >= 1600 && h >= 1024) {
		fs = DEFAULT_FONTSIZE_NEW;
	}
	fontsize_default = fs;

	_tcscpy(fontname_gui, wfont_vista);
	fontsize_gui = fontsize_default;
	fontstyle_gui = 0;
	fontweight_gui = FW_REGULAR;
	_tcscpy(fontname_list, wfont_vista);
	fontsize_list = fontsize_default;
	fontstyle_list = 0;
	fontweight_list = FW_REGULAR;
	_tcscpy(fontname_osd, _T("Lucida Console"));
	fontsize_osd = 8;
	fontstyle_osd = 0;
	fontweight_osd = FW_REGULAR;
}

static TCHAR *fontreg[3] = { _T("GUIFont"), _T("GUIListFont"), _T("OSDFont") };

void regsetfont(UAEREG *reg, const TCHAR *prefix, const TCHAR *name, const TCHAR *fontname, int fontsize, int fontstyle, int fontweight)
{
	TCHAR tmp[256], tmp2[256];

	_stprintf(tmp, _T("%s:%d:%d:%d"), fontname, fontsize, fontstyle, fontweight);
	_stprintf(tmp2, _T("%s%s"), name, prefix ? prefix : _T(""));
	regsetstr(reg, tmp2, tmp);
}
bool regqueryfont(UAEREG *reg, const TCHAR *prefix, const TCHAR *name, TCHAR *fontname, int *pfontsize, int *pfontstyle, int *pfontweight)
{
	TCHAR tmp2[256], tmp[256], *p1, *p2, *p3, *p4;
	int size;
	int fontsize, fontstyle, fontweight;

	_tcscpy (tmp2, name);
	if (prefix) {
		_tcscat (tmp2, prefix);
	}
	size = sizeof tmp / sizeof (TCHAR);
	if (!regquerystr (reg, tmp2, tmp, &size))
		return false;
	p1 = _tcschr (tmp, ':');
	if (!p1)
		return false;
	*p1++ = 0;
	p2 = _tcschr (p1, ':');
	if (!p2)
		return false;
	*p2++ = 0;
	p3 = _tcschr (p2, ':');
	if (!p3)
		return false;
	*p3++ = 0;
	p4 = _tcschr (p3, ':');
	if (p4)
		*p4 = 0;

	_tcscpy (fontname, tmp);
	fontsize = _tstoi (p1);
	fontstyle = _tstoi (p2);
	fontweight = _tstoi (p3);

	if (fontsize == 0)
		fontsize = fontsize_default;
	if (fontsize < 5)
		fontsize = 5;
	if (fontsize > 30)
		fontsize = 30;
	*pfontsize = fontsize;

	*pfontstyle = fontstyle;

	*pfontweight = fontweight;

	return true;
}

void scaleresource_setdefaults(HWND hwnd)
{
	setdeffont ();
	for (int i = 0; i < MAX_GUI_FONTS; i++) {
		TCHAR tmp[256];
		_stprintf (tmp, _T("%s%s"), fontreg[i], fontprefix);
		regdelete (NULL, tmp);
	}
}

void scaleresource_modification(HWND hwnd)
{
}

void scaleresource_init(const TCHAR *prefix, int fullscreen)
{
	fontprefix = prefix;

	setdeffont();

	if (fontprefix) {
		regqueryfont(NULL, fontprefix, fontreg[0], fontname_gui, &fontsize_gui, &fontstyle_gui, &fontweight_gui);
		regqueryfont(NULL, fontprefix, fontreg[1], fontname_list, &fontsize_list, &fontstyle_list, &fontweight_list);
	}

	//write_log (_T("GUI font %s:%d:%d:%d\n"), fontname_gui, fontsize_gui, fontstyle_gui, fontweight_gui);
	//write_log (_T("List font %s:%d:%d:%d\n"), fontname_list, fontsize_list, fontstyle_list, fontweight_list);
}

int scaleresource_choosefont(HWND hDlg, int fonttype)
{
	CHOOSEFONT cf = { 0 };
	LOGFONT lf = { 0 };
	TCHAR *fontname[3];
	int *fontsize[3], *fontstyle[3], *fontweight[3];
	int lm = 72;
	const TCHAR *prefix = fontprefix;

	if (fonttype == 2) {
		regqueryfont(NULL, NULL, fontreg[2], fontname_osd, &fontsize_osd, &fontstyle_osd, &fontweight_osd);
		prefix = NULL;
	}

	fontname[0] = fontname_gui;
	fontname[1] = fontname_list;
	fontname[2] = fontname_osd;
	fontsize[0] = &fontsize_gui;
	fontsize[1] = &fontsize_list;
	fontsize[2] = &fontsize_osd;
	fontstyle[0] = &fontstyle_gui;
	fontstyle[1] = &fontstyle_list;
	fontstyle[2] = &fontstyle_osd;
	fontweight[0] = &fontweight_gui;
	fontweight[1] = &fontweight_list;
	fontweight[2] = &fontweight_osd;

	cf.lStructSize = sizeof cf;
	cf.hwndOwner = hDlg;
	cf.Flags = CF_FORCEFONTEXIST | CF_INITTOLOGFONTSTRUCT | CF_NOSCRIPTSEL | CF_SCREENFONTS;
	cf.lpLogFont = &lf;
	cf.nFontType = REGULAR_FONTTYPE;

	HDC hdc = GetDC(NULL);
	lm = GetDeviceCaps(hdc, LOGPIXELSY);
	ReleaseDC(NULL, hdc);

	_tcscpy (lf.lfFaceName, fontname[fonttype]);
	lf.lfHeight = -MulDiv(*fontsize[fonttype], lm, 72);
	lf.lfWeight = *fontweight[fonttype];
	lf.lfItalic = (*fontstyle[fonttype] & ITALIC_FONTTYPE) != 0;

	if (!ChooseFont (&cf)) {
		return 0;
	}

	_tcscpy (fontname[fonttype], lf.lfFaceName);
	*fontsize[fonttype] = lf.lfHeight;
	*fontsize[fonttype] = -MulDiv (*fontsize[fonttype], 72, lm);

	*fontstyle[fonttype] = lf.lfItalic ? ITALIC_FONTTYPE : 0;

	*fontweight[fonttype] = lf.lfWeight;

	regsetfont(NULL, prefix, fontreg[fonttype], fontname[fonttype], *fontsize[fonttype], *fontstyle[fonttype], *fontweight[fonttype]);

	return 1;
}

void darkmode_initdialog(HWND hDlg)
{
	if (g_darkModeSupported)
	{
		SendMessageW(hDlg, WM_THEMECHANGED, 0, 0);
	}
}
void darkmode_themechanged(HWND hDlg)
{
	if (g_darkModeSupported)
	{
		RefreshTitleBarThemeColor(hDlg);
		UpdateWindow(hDlg);
	}
}
INT_PTR darkmode_ctlcolor(WPARAM wParam, bool *handled)
{
	constexpr COLORREF darkBkColor = 0x383838;
	constexpr COLORREF darkTextColor = 0xFFFFFF;
	static HBRUSH hbrBkgnd = nullptr;

	if (g_darkModeSupported && g_darkModeEnabled)
	{
		HDC hdc = reinterpret_cast<HDC>(wParam);
		SetTextColor(hdc, darkTextColor);
		SetBkColor(hdc, darkBkColor);
		if (!hbrBkgnd)
			hbrBkgnd = CreateSolidBrush(darkBkColor);
		*handled = true;
		return reinterpret_cast<INT_PTR>(hbrBkgnd);
	}
	*handled = false;
	return 0;
}

#include <gdiplus.h> 

#define MAX_BOX_ART_IMAGES 20
#define MAX_BOX_ART_TYPES 5
#define MAX_VISIBLE_IMAGES 10

struct imagedata
{
	Gdiplus::Image *image;
	IStream *stream;
	TCHAR *metafile;
};

static bool boxart_inited;
static ULONG_PTR gdiplusToken;
static HWND boxarthwnd;
static int boxart_window_width;
static int boxart_window_height;
static const int hgap = 8;
static const int wgap = 8;
static struct imagedata *images[MAX_BOX_ART_IMAGES];
static int total_height;
static int max_width;
static int total_images;
static int imagemode;
static bool imagemodereset;
static int lastimage;
static TCHAR image_path[MAX_DPATH], config_path[MAX_DPATH];
static int image_coords[MAX_VISIBLE_IMAGES + 1];

int max_visible_boxart_images = 2;
int stored_boxart_window_width = 400;
int stored_boxart_window_width_fsgui = 33;
int calculated_boxart_window_width;
static int stored_boxart_window_height;

static void boxart_init(void)
{
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
}

static const TCHAR *boxartnames[MAX_BOX_ART_TYPES] = {
	_T("Title"),
	_T("SShot"),
	_T("Boxart"),
	_T("Misc"),
	_T("Data"),
};

typedef HRESULT(CALLBACK* DWMGETWINDOWATTRIBUTE)(HWND hwnd, DWORD dwAttribute, PVOID pvAttribute, DWORD cbAttribute);
static DWMGETWINDOWATTRIBUTE pDwmGetWindowAttribute;
static HMODULE dwmapihandle;

extern int gui_fullscreen;

void getextendedframebounds(HWND hwnd, RECT *r)
{
	if (!pDwmGetWindowAttribute && !dwmapihandle) {
		dwmapihandle = LoadLibrary(_T("dwmapi.dll"));
		if (dwmapihandle)
			pDwmGetWindowAttribute = (DWMGETWINDOWATTRIBUTE)GetProcAddress(dwmapihandle, "DwmGetWindowAttribute");
	}
	if (pDwmGetWindowAttribute) {
		pDwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, r, sizeof(RECT));
	}
}

static void getpos(RECT *r)
{
	RECT r1, r2;

	GetWindowRect(hGUIWnd, &r1);

	calculated_boxart_window_width = stored_boxart_window_width;
	if (gui_fullscreen && stored_boxart_window_width_fsgui >= 10 && stored_boxart_window_width_fsgui <= 90) {
		calculated_boxart_window_width = (r1.right - r1.left) * stored_boxart_window_width_fsgui / 100;
	}

	if (gui_fullscreen && (r1.right - r1.left) - calculated_boxart_window_width >= MIN_GUI_INTERNAL_WIDTH) {
		HMONITOR mon = MonitorFromRect(&r1, MONITOR_DEFAULTTOPRIMARY);
		MONITORINFO mi;
		mi.cbSize = sizeof(mi);
		if (GetMonitorInfo(mon, &mi)) {
			RECT r = mi.rcWork;
			if (r1.right + calculated_boxart_window_width > r.right) {
				r1.right -= calculated_boxart_window_width - (r.right - r1.right);
				SetWindowPos(hGUIWnd, NULL, r1.left, r1.top, r1.right - r1.left, r1.bottom - r1.top, SWP_NOZORDER | SWP_NOACTIVATE);
			}
		}
	}

	r2 = r1;
	getextendedframebounds(hGUIWnd, &r2);

	r->left = r1.right - ((r2.left - r1.left) + (r1.right - r2.right));
	r->top = r1.top;
	r->bottom = r1.bottom;
	r->right = r->left + calculated_boxart_window_width;
}

void move_box_art_window(void)
{
	RECT r;

	if (!hGUIWnd || !boxarthwnd)
		return;
	getpos(&r);
	SetWindowPos(boxarthwnd, HWND_TOPMOST, r.left, r.top, 0, 0, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOSIZE);
}

static void freeimages(void)
{
	for (int i = 0; i < MAX_BOX_ART_IMAGES; i++) {
		struct imagedata *im = images[i];
		if (im) {
			if (im->image) {
				delete im->image;
			}
			if (im->stream) {
				im->stream->Release();
			}
			xfree(im->metafile);
			xfree(im);
			images[i] = NULL;
		}
	}
}

void close_box_art_window(void)
{
	freeimages();
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

		style &= ~(WS_VISIBLE | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);

		stored_boxart_window_height = r.bottom - r.top;
		boxarthwnd = CreateWindowEx(exstyle | WS_EX_NOACTIVATE,
			_T("BoxArt"), _T("WinUAE"),
			style,
			r.left, r.top,
			calculated_boxart_window_width, r.bottom - r.top,
			hGUIWnd, NULL, hInst, NULL);
		if (boxarthwnd) {

			if (g_darkModeSupported) {
				_AllowDarkModeForWindow(boxarthwnd, g_darkModeEnabled);
				SendMessageW(boxarthwnd, WM_THEMECHANGED, 0, 0);
			}

			RECT r;
			GetClientRect(boxarthwnd, &r);
			boxart_window_width = r.right - r.left;
			boxart_window_height = r.bottom - r.top;

			HMENU menu = GetSystemMenu(boxarthwnd, FALSE);
			InsertMenu(menu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, _T(""));
			InsertMenu(menu, -1, MF_BYPOSITION, 1, _T("Open Game Folder"));
		}
	} else {
		GetWindowRect(hGUIWnd, &r);
		if (stored_boxart_window_height != r.bottom - r.top) {
			stored_boxart_window_height = r.bottom - r.top;
			SetWindowPos(boxarthwnd, HWND_TOPMOST, 0, 0, calculated_boxart_window_width, stored_boxart_window_height, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOMOVE);
			GetClientRect(boxarthwnd, &r);
			boxart_window_width = r.right - r.left;
			boxart_window_height = r.bottom - r.top;
		}
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
	if (g_darkModeEnabled) {
		HBRUSH hbrBkgnd = CreateSolidBrush(dark_bg_color);
		FillRect(hdc, &r, hbrBkgnd);
		DeleteObject(hbrBkgnd);
	} else {
		FillRect(hdc, &r, (HBRUSH)(COLOR_BTNFACE + 1));
	}

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
				struct imagedata *im = images[imagemode - 1];
				Gdiplus::Image *img = im->image;
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
	Gdiplus::Pen pen1(Gdiplus::Color(170, 170, 0, 0), 2);
	Gdiplus::Pen pen2(Gdiplus::Color(170, 0, 0, 170), 2);

	float y = 0;
	cnt = 0;
	for (int i = 0; i < MAX_BOX_ART_IMAGES; i++) {
		if (!imagemode && cnt >= max_visible_boxart_images)
			break;
		if (images[i]) {
			struct imagedata *im = images[i];
			Gdiplus::Image *img = im->image;
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
			struct imagedata *im = images[i];
			Gdiplus::Image *img = im->image;
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

			Gdiplus::Rect d2(x1 - 1, y1 - 1, x2 + 2, y2 + 2);
			if (im->metafile) {
				graphics.DrawRectangle(&pen2, d2);
			} else {
				graphics.DrawRectangle(&pen1, d2);
			}

			image_coords[cnt] = y1 + y2 + hgap / 2;

			y += h;
			y += hgap;
			cnt++;
		}
	}
}

extern int full_property_sheet;  

bool show_box_art(const TCHAR *path, const TCHAR *configpath)
{
	TCHAR tmp1[MAX_DPATH];

	freeimages();

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

	if (path != image_path) {
		_tcscpy(image_path, path);
		_tcscpy(config_path, configpath);
	}

	int len = uaetcslen(config_path);
	if (len > 4 && !_tcsicmp(config_path + len - 4, _T(".uae")))
		config_path[len - 4] = 0;
	if (uaetcslen(config_path) > 0)
		SetWindowText(boxarthwnd, config_path);

	if (max_visible_boxart_images < 1) {
		max_visible_boxart_images = 1;
	}
	if (max_visible_boxart_images > MAX_VISIBLE_IMAGES) {
		max_visible_boxart_images = MAX_VISIBLE_IMAGES;
	}

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
			TCHAR metafile[MAX_DPATH];
			metafile[0] = 0;

			if (arttype == MAX_BOX_ART_TYPES - 1) {
				WIN32_FIND_DATA ffd;
				if (j > 0) {
					_stprintf(tmp1, _T("%s___%s%d_*"), path, boxartnames[arttype], j + 1);
				} else {
					_stprintf(tmp1, _T("%s___%s_*"), path, boxartnames[arttype]);
				}
				HANDLE ffHandle = FindFirstFile(tmp1, &ffd);
				if (ffHandle == INVALID_HANDLE_VALUE) {
					break;
				}
				int found = 0;
				for (;;) {
					if (_tcslen(ffd.cFileName) >= 4 && !_tcsicmp(ffd.cFileName + _tcslen(ffd.cFileName) - 4, _T(".png"))) {
						found |= 1;
						_tcscpy(tmp1, path);
						_tcscat(tmp1, ffd.cFileName);
					} else if (!(found & 2)) {
						_tcscpy(metafile, path);
						_tcscat(metafile, ffd.cFileName);
						found |= 2;
					}
					if (!FindNextFile(ffHandle, &ffd))
						break;
				}
				FindClose(ffHandle);
				if (!(found & 1))
					break;
				if (!(found & 2)) {
					_tcscpy(metafile, path);
					_tcscat(metafile, tmp1 + _tcslen(path) + 3 + _tcslen(boxartnames[arttype]) + (j > 0) + 1);
					if (_tcslen(metafile) > 4) {
						metafile[_tcslen(metafile) - 4] = 0;
					}
					if (!zfile_exists(metafile))
						break;
				}
			} else {
				_tcscpy(tmp1, path);
				_tcscat(tmp1, _T("___"));
				_tcscat(tmp1, boxartnames[arttype]);
				if (j > 0) {
					_stprintf(tmp1 + _tcslen(tmp1), _T("%d"), j + 1);
				}
				_tcscat(tmp1, _T(".png"));
			}

			Gdiplus::Image *image = NULL;
			IStream *stream = NULL;
			for (int imgtype = 0; imgtype < 2 && !image; imgtype++) {
				if (imgtype == 1 && _tcslen(tmp1) > 3) {
					_tcscpy(tmp1 + _tcslen(tmp1) - 3, _T("jpg"));
				}
				int outlen = 0;
				uae_u8 *filedata = zfile_load_file(tmp1, &outlen);
				if (filedata) {
					stream = SHCreateMemStream(filedata, outlen);
					xfree(filedata);
					if (stream) {
						image = Gdiplus::Image::FromStream(stream);
						if (image && image->GetLastStatus() == Gdiplus::Ok) {
							break;
						}
						if (image) {
							delete image;
							image = NULL;
						}
						stream->Release();
						stream = NULL;
					}
				}
			}
			if (image && stream) {
				int w = image->GetWidth();
				int h = image->GetHeight();
				write_log(_T("Image '%s' loaded %d*%d\n"), tmp1, w, h);
				struct imagedata *img = xcalloc(struct imagedata, 1);
				if (img) {
					img->image = image;
					img->stream = stream;
					if (metafile[0]) {
						img->metafile = my_strdup(metafile);
					}
					images[cnt++] = img;
					if (total_images < max_visible_boxart_images) {
						if (w > max_width)
							max_width = w;
						total_height += h;
						total_images++;
					}
					image = NULL;
					stream = NULL;
				}
				if (image || stream) {
					if (image) {
						delete image;
					}
					if (stream) {
						stream->Release();
					}
					break;
				}
			}
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
	show_box_art(image_path, config_path);
}

void reset_box_art_window(void)
{
	if (!image_path[0])
		return;
	show_box_art(image_path, config_path);
}

LRESULT CALLBACK BoxArtWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int oldmode = imagemode;
	bool handled;

	switch (message)
	{
		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
		{
			bool exec = false;
			if (imagemode) {
				lastimage = imagemode - 1;
				struct imagedata *im = images[lastimage];
				if (im->metafile) {
					ShellExecute(NULL, _T("open"), im->metafile, NULL, NULL, SW_SHOWNORMAL);
					exec = true;
				} else {
					imagemode = 0;
				}
			} else {
				int y = (short)(lParam >> 16);
				int imagenum = 0;
				for (int i = 1; i < max_visible_boxart_images; i++) {
					if (y >= image_coords[i - 1])
						imagenum = i;
				}
				while (imagenum > 0) {
					if (images[imagenum])
						break;
					imagenum--;
				}
				imagenum++;
				struct imagedata *im = images[imagenum - 1];
				if (im->metafile) {
					ShellExecute(NULL, _T("open"), im->metafile, NULL, NULL, SW_SHOWNORMAL);
					exec = true;
				} else {
					imagemode = imagenum;
				}
			}
			if (!exec) {
				lastimage = imagemode - 1;
				imagemodereset = true;
				if (oldmode != imagemode) {
					InvalidateRect(hWnd, NULL, TRUE);
				}
			}
		}
		break;
		case WM_RBUTTONDOWN:
		case WM_RBUTTONDBLCLK:
			if (imagemode) {
				imagemode++;
				lastimage = imagemode - 1;
			} else {
				imagemode = lastimage + 1;
				if (imagemode <= 0)
					imagemode = 1;
			}
			imagemodereset = false;
			if (oldmode != imagemode) {
				InvalidateRect(hWnd, NULL, TRUE);
			}
		break;
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hDC = BeginPaint(hWnd, &ps);
			boxartpaint(hDC, hWnd);
			EndPaint(hWnd, &ps);
			return 0;
		}
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
		case  WM_DPICHANGED:
		{
			RECT *const r = (RECT *)lParam;
			SetWindowPos(hWnd, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
			return FALSE;
		}
		case WM_THEMECHANGED:
			darkmode_themechanged(hWnd);
		break;
		case WM_CTLCOLORDLG:
		case WM_CTLCOLORSTATIC:
			INT_PTR v = darkmode_ctlcolor(wParam, &handled);
			if (handled) {
				return v;
			}
			break;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}


static HRESULT InitializeUIAutomation(IUIAutomation **ppAutomation)
{
	return CoCreateInstance(CLSID_CUIAutomation, NULL,
		CLSCTX_INPROC_SERVER, IID_IUIAutomation,
		reinterpret_cast<void **>(ppAutomation));
}

struct elxy
{
	int sx, sy;
	int ex, ey;
	int region;
	bool selectable;
	struct newreswnd *nres;
};

static struct elxy *findclosestelement(struct elxy *xy, int total, struct elxy *curxy, int x, int y, int region, int newdist, int xydist)
{
	struct elxy *closest = NULL;
	int olddist = 10000;

	int cx = curxy->sx;
	int cy = curxy->sy;
	int cxe = curxy->ex;
	int cye = curxy->ey;

	if (x > 0) {
		cxe = cx = curxy->ex + 1;
	} else if (x < 0) {
		cxe = cx = curxy->sx - 1;
	}
	if (y > 0) {
		cye = cy = curxy->ey + 1;
	} else if (y < 0) {
		cye = cy = curxy->sy - 1;
	}

	cx += x * newdist;
	cy += y * newdist;
	cxe += x * newdist;
	cye += y * newdist;

	int cxs = cx + (cxe - cx) / 2;
	int cys = cy + (cye - cy) / 2;

	for (int i = 0; i < total; i++) {
		struct elxy *txy = &xy[i];
		int dist = -1;
		int sx = txy->sx;
		int sy = txy->sy;
		int ex = txy->ex;
		int ey = txy->ey;

		if (!txy->selectable) {
			continue;
		}

		if (txy->region != region) {
			continue;
		}

		if (x) {
			ey += xydist;
			sy -= xydist;
		}
		if (y) {
			ex += xydist;
			sx -= xydist;
		}

		if ((sx <= cxs && ex >= cxs) && y) {
			if ((y < 0 && txy->ey < curxy->sy) || (y > 0 && txy->sy > curxy->ey)) {
				dist = abs(txy->sy + (txy->ey - txy->sy) / 2 - cys);
			}
		} else if ((sy <= cys && ey >= cys) && x) {
			if ((x < 0 && txy->ex < curxy->sx) || (x > 0 && txy->sx > curxy->ex)) {
				dist = abs(txy->sx + (txy->ex - txy->sx) / 2 - cxs);
			}
		}
		if (dist > 0 && dist < olddist) {
			closest = txy;
			olddist = dist;
		}
	}
	return closest;
}

static void UIA_Invoke(HWND hwnd)
{
	IUIAutomationElement *el;
	HRESULT hr = Automation->GetFocusedElement(&el);
	if (SUCCEEDED(hr)) {
		IUIAutomationInvokePattern *InvokePattern;
		hr = el->GetCurrentPattern(UIA_InvokePatternId, (IUnknown **)&InvokePattern);
		if (SUCCEEDED(hr) && InvokePattern) {
			InvokePattern->Invoke();
			InvokePattern->Release();
		}
		el->Release();
	}
}

static void treeview_cursor(HWND h, HTREEITEM next)
{
	TreeView_SelectItem(h, next);
	RECT r;
	r.left = -1;
	r.top = -1;
	TreeView_GetItemRect(h, next, &r, TRUE);
	if (r.left != -1 && r.top != -1) {
		RECT r2;
		GetWindowRect(h, &r2);
		r.left += r2.left;
		r.right += r2.left;
		r.top += r2.top;
		r.bottom += r2.top;
		SetCursorPos(r.left + (r.right - r.left) / 2, r.top + (r.bottom - r.top) / 2);
	}
}

void gui_cursor(HWND hwnd, struct newresource *ns, int x, int y, int click)
{
	struct elxy *found = NULL;
	struct elxy *currres;
	struct elxy xy[MAX_DLGID];
	int total = 0;
	int maxdist = 0;
	int mindist = 32000;

	if (!Automation) {
		InitializeUIAutomation(&Automation);
	}
	if (!Automation) {
		return;
	}

	for (int i = 0; ns->hwnds[i].hwnd; i++) {
		struct newreswnd *nrw = &ns->hwnds[i];
		if (nrw->selectable && IsWindowVisible(nrw->hwnd) && IsWindowEnabled(nrw->hwnd)) {
			RECT r;
			GetWindowRect(nrw->hwnd, &r);
			struct elxy *txy = &xy[total];
			txy->sx = r.left;
			txy->sy = r.top;
			txy->ex = r.right;
			txy->ey = r.bottom;
			txy->region = nrw->region;
			txy->selectable = nrw->selectable;
			txy->nres = nrw;
			total++;
		}
	}
	ns = ns->child;
	if (ns) {
		for (int i = 0; ns->hwnds[i].hwnd; i++) {
			struct newreswnd *nrw = &ns->hwnds[i];
			if (IsWindowVisible(nrw->hwnd) && IsWindowEnabled(nrw->hwnd)) {
				RECT r;
				GetWindowRect(nrw->hwnd, &r);
				struct elxy *txy = &xy[total];
				txy->sx = r.left;
				txy->sy = r.top;
				txy->ex = r.right;
				txy->ey = r.bottom;
				txy->region = nrw->region;
				txy->selectable = nrw->selectable;
				txy->nres = nrw;
				total++;
			}
		}
	}

	for (int i = 0; i < total; i++) {
		struct elxy *txy = &xy[i];
		struct newreswnd *nrw = txy->nres;
		if (nrw->x + nrw->w > maxdist) {
			maxdist = nrw->x + nrw->x;
		}
		if (nrw->y + nrw->h > maxdist) {
			maxdist = nrw->y + nrw->h;
		}
		if (nrw->x < mindist) {
			mindist = nrw->x;
		}
		if (nrw->y < mindist) {
			mindist = nrw->y;
		}
	}

	currres = NULL;
	HWND focus = GetFocus();
	for (int i = 0; i < total; i++) {
		struct elxy *txy = &xy[i];
		for (int j = 0; txy->nres->hwndx[j]; j++) {
			HWND h = txy->nres->hwndx[j];
			if (h == focus && txy->selectable) {
				RECT r;
				GetWindowRect(h, &r);
				currres = txy;
				//write_log("%p %d\n", currres->nres->hwnd, currres->region);
				break;
			}
		}
	}

	if (click == 8) {
		bool regfound = false;
		int region = 0;
		if (currres) {
			region = currres->region;
		}
		while (!regfound) {
			region++;
			if (region >= 4) {
				region = 0;
			}
			for (int i = 0; i < total; i++) {
				struct elxy *txy = &xy[i];
				if (txy->region == region) {
					if (txy->selectable) {
						found = txy;
						goto end;
					}
					regfound = true;
				}
			}
		}
		return;
	}


	if (currres && click == 1 && x && !y) {
		int style = currres->nres->style;
		// msctls_trackbar32
		if (style & 0x200) {
			HWND h = currres->nres->hwnd;
			LRESULT v = SendMessage(h, TBM_GETPOS, 0, 0);
			LRESULT min = SendMessage(h, TBM_GETRANGEMIN, 0, 0);
			LRESULT max = SendMessage(h, TBM_GETRANGEMAX, 0, 0);
			LRESULT change = SendMessage(h, TBM_GETLINESIZE, 0, 0);
			v += change * x;
			if (v < min) {
				v = min;
			}
			if (v > max) {
				v = max;
			}
			SendMessage(h, TBM_SETPOSNOTIFY, 0, v);
		}
		return;
	}

	if (currres && click == 1 && !x && !y) {
		HWND h = currres->nres->hwnd;
		int style = currres->nres->style;
		int style2 = style & BS_TYPEMASK;
		// ComboBox expand/collapse
		if (style & 0x100) {
			IUIAutomationElement *el;
			HRESULT hr = Automation->ElementFromHandle(currres->nres->hwndx[0], &el);
			if (SUCCEEDED(hr)) {
				IUIAutomationExpandCollapsePattern *ExpandCollapsePattern;
				hr = el->GetCurrentPattern(UIA_ExpandCollapsePatternId, (IUnknown **)&ExpandCollapsePattern);
				if (SUCCEEDED(hr) && ExpandCollapsePattern) {
					ExpandCollapseState ecs;
					hr = ExpandCollapsePattern->get_CurrentExpandCollapseState(&ecs);
					if (SUCCEEDED(hr)) {
						if (ecs == ExpandCollapseState_Collapsed) {
							ExpandCollapsePattern->Expand();
						} else {
							ExpandCollapsePattern->Collapse();
						}
					}
					ExpandCollapsePattern->Release();
				}
				el->Release();
			}
		} else if (currres->nres->listn) {
			// ListView
			int c = ListView_GetHotItem(h);
			UINT state = ListView_GetItemState(h, c, LVIS_STATEIMAGEMASK);
			// checkbox toggle
			if (state & 0x3000) {
				if ((state & 0x3000) == 0x1000) {
					state &= ~0x1000;
					state |= 0x2000;
				} else {
					state &= ~0x2000;
					state |= 0x1000;
				}
				ListView_SetItemState(h, c, state, LVIS_STATEIMAGEMASK);
			}
			return;
		} else if (style2 == BS_PUSHBUTTON ||
			style2 == BS_DEFPUSHBUTTON ||
			style2 == BS_CHECKBOX ||
			style2 == BS_AUTOCHECKBOX ||
			style2 == BS_RADIOBUTTON ||
			style2 == BS_AUTORADIOBUTTON ||
			style2 == BS_3STATE ||
			style2 == BS_AUTO3STATE)
		{
			// button, checkbox etc click
			UIA_Invoke(h);
			return;
		}
		return;
	}
	
	if (currres) {
		int style = currres->nres->style;
		HWND h = currres->nres->hwnd;
		// TreeView up/down
		if (currres->nres->list) {
			if (x || y) {
				HTREEITEM c = TreeView_GetSelection(h);
				HTREEITEM next;
				if (x < 0) {
					next = TreeView_GetNextItem(h, c, TVGN_PARENT);
				} else if (x > 0) {
					next = TreeView_GetNextItem(h, c, TVGN_CHILD);
					if (!next) {
						HTREEITEM next2 = c;
						for (;;) {
							next2 = TreeView_GetNextItem(h, next2, TVGN_NEXT);
							if (!next2) {
								next = c;
								break;
							}
							next = TreeView_GetNextItem(h, next2, TVGN_CHILD);
							if (next) {
								break;
							}
						}
					}
				} else if (y < 0) {
					next = TreeView_GetNextItem(h, c, TVGN_PREVIOUS);
					if (!next) {
						next = c;
						for (;;) {
							HTREEITEM next2 = TreeView_GetNextItem(h, next, TVGN_NEXT);
							if (!next2) {
								break;
							}
							next = next2;
						}
					}
				} else if (y > 0) {
					next = TreeView_GetNextItem(h, c, TVGN_NEXT);
					if (!next) {
						next = TreeView_GetNextItem(h, c, TVGN_PARENT);
						if (next) {
							next = TreeView_GetNextItem(h, next, TVGN_CHILD);
						} else {
							next = TreeView_GetNextItem(h, c, TVGN_CHILD);
						}
					}
				}
				if (!next) {
					next = TreeView_GetNextItem(h, next, TVGN_ROOT);
				}
				if (next) {
					treeview_cursor(h, next);
				}
				return;
			}
		} else if (currres->nres->listn) {
			// ListView
			if (!x && y) {
				int c = ListView_GetHotItem(h);
				int total = ListView_GetItemCount(h);
				c += y;
				if (c < 0) {
					c = total - 1;
					if (c < 0) {
						c = 0;
					}
				}
				if (c >= total) {
					c = 0;
				}
				ListView_SetHotItem(h, c);
				ListView_EnsureVisible(h, c, FALSE);
				RECT r;
				r.left = -1;
				r.top = -1;
				ListView_GetItemRect(h, c, &r, LVIR_BOUNDS);
				if (r.left != -1 && r.top != -1) {
					RECT r2;
					GetWindowRect(h, &r2);
					r.left += r2.left;
					r.right += r2.left;
					r.top += r2.top;
					r.bottom += r2.top;
					SetCursorPos(r.left + (r.right - r.left) / 3, r.top + (r.bottom - r.top) / 2);
				}
			}
			return;
		} else if (style & 0x100) {
			// ComboBox expanded up/down
			IUIAutomationElement *el;
			//HRESULT hr = Automation->GetFocusedElement(&el);
			HRESULT hr = Automation->ElementFromHandle(currres->nres->hwndx[0], &el);
			if (SUCCEEDED(hr)) {
				IUIAutomationExpandCollapsePattern *ExpandCollapsePattern;
				hr = el->GetCurrentPattern(UIA_ExpandCollapsePatternId, (IUnknown **)&ExpandCollapsePattern);
				if (SUCCEEDED(hr) && ExpandCollapsePattern) {
					ExpandCollapseState ecs;
					hr = ExpandCollapsePattern->get_CurrentExpandCollapseState(&ecs);
					if (SUCCEEDED(hr)) {
						if (ecs != ExpandCollapseState_Collapsed) {
							ExpandCollapsePattern->Release();
							el->Release();
							INPUT inp[2] = {};
							inp[0].type = INPUT_KEYBOARD;
							inp[0].ki.wVk = y < 0 ? VK_UP : VK_DOWN;
							inp[1].type = INPUT_KEYBOARD;
							inp[1].ki.wVk = inp[0].ki.wVk;
							inp[1].ki.dwFlags = KEYEVENTF_KEYUP;
							SendInput(ARRAYSIZE(inp), inp, sizeof(INPUT));
							return;
						}
					}
					ExpandCollapsePattern->Release();
				}
				el->Release();
			}
		}
	}

	if (currres && (x || y)) {
		int dist_min = 10;
		int dist_max = maxdist - mindist;
		int dist_step = 10;

		int region = currres->region;
		for(int j = 0; j < 5 && !found; j++) {
			for (int i = dist_min; i < dist_max && !found; i += dist_step) {
				struct elxy *n = findclosestelement(xy, total, currres, x, y, region, i, dist_max * j / 10);
				if (n && n != currres) {
					found = n;
				}
			}
		}
	}
end:
	if (found) {
		HWND h = found->nres->hwnd;
		if (found->nres->list) {
			HTREEITEM c = TreeView_GetSelection(h);
			treeview_cursor(h, c);
		} else {
			RECT r;
			GetWindowRect(h, &r);
			SetCursorPos(r.left + (r.right - r.left) / 3, r.top + (r.bottom - r.top) / 2);
			//SetCursorPos(r.left, r.top);
		}
		HWND parent = GetParent(h);
		SetFocus(parent);
		SendMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)h, TRUE);

		//write_log("%p -> %p\n", focus, h);
	}

}

#define MAX_PADS 4

int gui_control = 0;

void process_gui_control(HWND h, struct newresource *nres)
{
	static int clicked[MAX_PADS];
	static int lastbuttons[MAX_PADS];
	static int mousemove;

	if (h == NULL || nres == NULL) {
		mousemove = 1;
	}

	for (int i = 0; i < MAX_PADS; i++) {
		XINPUT_STATE xstate;
		DWORD state = XInputGetState(i, &xstate);
		if (state == ERROR_SUCCESS) {
			int buttons = xstate.Gamepad.wButtons;
			int oldclick = clicked[i];
			bool clickchanged = false;
			if (buttons & XINPUT_GAMEPAD_A) {
				clicked[i] |= 1;
			} else {
				clicked[i] &= ~1;
			}
			if (buttons & XINPUT_GAMEPAD_B) {
				clicked[i] |= 2;
			} else {
				clicked[i] &= ~2;
			}
			if (buttons & XINPUT_GAMEPAD_X) {
				clicked[i] |= 4;
			} else {
				clicked[i] &= ~4;
			}
			if (buttons & XINPUT_GAMEPAD_Y) {
				clicked[i] |= 8;
			} else {
				clicked[i] &= ~8;
			}
			if (clicked[i] != oldclick) {
				clickchanged = true;
			}

			int newbuttons = buttons;
			if (lastbuttons[i] & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT)) {
				if (buttons & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT)) {
					buttons &= ~(XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT);
				}
			}

			int x = 0, y = 0;
			if (buttons & XINPUT_GAMEPAD_DPAD_UP) {
				y = -1;
				mousemove = 0;
			}
			if (buttons & XINPUT_GAMEPAD_DPAD_DOWN) {
				y = 1;
				mousemove = 0;
			}
			if (buttons & XINPUT_GAMEPAD_DPAD_LEFT) {
				x = -1;
				mousemove = 0;
			}
			if (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) {
				x = 1;
				mousemove = 0;
			}

			if (mousemove && clicked[i] == 1 && clickchanged) {

				INPUT inputs[2] = { 0 };
				inputs[0].type = INPUT_MOUSE;
				inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
				inputs[1].type = INPUT_MOUSE;
				inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
				SendInput(sizeof(inputs) / sizeof(INPUT), inputs, sizeof(INPUT));

				lastbuttons[i] = newbuttons;

			} else if (mousemove && clicked[i] == 2 && clickchanged) {

				INPUT inputs[2] = { 0 };
				inputs[0].type = INPUT_MOUSE;
				inputs[0].mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
				inputs[1].type = INPUT_MOUSE;
				inputs[1].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
				SendInput(sizeof(inputs) / sizeof(INPUT), inputs, sizeof(INPUT));

				lastbuttons[i] = newbuttons;

			} else if (mousemove && clicked[i] == 8 && clickchanged) {

				INPUT inputs[2] = { 0 };
				inputs[0].type = INPUT_KEYBOARD;
				inputs[0].ki.wVk = VK_TAB;
				inputs[1].type = INPUT_KEYBOARD;
				inputs[1].ki.wVk = VK_TAB;
				inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
				SendInput(sizeof(inputs) / sizeof(INPUT), inputs, sizeof(INPUT));

				lastbuttons[i] = newbuttons;

			} else {

				if (nres && h && (x || y || (clickchanged && clicked[i]))) {
					gui_cursor(h, nres, x, y, clicked[i]);
				}
				lastbuttons[i] = newbuttons;

				int min = 4096;
				int max = 30;
				int xdiff = 0, ydiff = 0;
				if (xstate.Gamepad.sThumbLX < -min || xstate.Gamepad.sThumbLX > min) {
					xdiff = abs(xstate.Gamepad.sThumbLX) - min;
					if (xstate.Gamepad.sThumbLX < 0) {
						xdiff = -xdiff;
					}
				}
				if (xstate.Gamepad.sThumbLY < -min || xstate.Gamepad.sThumbLY > min) {
					ydiff = abs(xstate.Gamepad.sThumbLY) - min;
					if (xstate.Gamepad.sThumbLY < 0) {
						ydiff = -ydiff;
					}
				}
				if (xdiff || ydiff) {
					xdiff /= 1024;
					ydiff /= 1024;
					if (xdiff < -max) {
						xdiff = -max;
					}
					if (xdiff > max) {
						xdiff = max;
					}
					if (ydiff < -max) {
						ydiff = -max;
					}
					if (ydiff > max) {
						ydiff = max;
					}
					POINT pt;
					if ((xdiff || ydiff) && GetCursorPos(&pt)) {
						POINT ptx = pt;
						pt.x += xdiff;
						pt.y += -ydiff;
						if (h) {
							RECT r;
							WINDOWINFO wi = { 0 };
							wi.cbSize = sizeof(wi);
							GetWindowInfo(h, &wi);
							r = wi.rcClient;
							if (pt.x < r.left) {
								if (xdiff < 0) {
									pt.x = r.right;
								} else {
									pt.x = r.left;
								}
							}
							if (pt.x > r.right) {
								if (xdiff > 0) {
									pt.x = r.left;
								} else {
									pt.x = r.right;
								}
							}
							if (pt.y < r.top) {
								if (ydiff > 0) {
									pt.y = r.bottom;
								} else {
									pt.y = r.top;
								}
							}
							if (pt.y > r.bottom) {
								if (ydiff < 0) {
									pt.y = r.top;
								} else {
									pt.y = r.bottom;
								}
							}
						}
						if (ptx.x != pt.x || ptx.y != pt.y) {
							mousemove = 1;
							SetCursorPos(pt.x, pt.y);
						}
					}
				}
			}

		}
	}
}
