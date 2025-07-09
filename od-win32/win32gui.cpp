/*==========================================================================
*
*  Copyright (C) 1996 Brian King
*
*  File:       win32gui.c
*  Content:    Win32-specific gui features for UAE port.
*
***************************************************************************/

#define CONFIGCACHE 1
#define FRONTEND 0

#define _WIN32_WINNT 0x600

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <windowsx.h>
#include <winspool.h>
#include <winuser.h>
#include <mmsystem.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dlgs.h>
#include <prsht.h>
#include <shellapi.h>
#include <Shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <dbt.h>
#include <Cfgmgr32.h>
#include <dwmapi.h>

#include "resource.h"
#include "sysconfig.h"
#include "sysdeps.h"
#include "gui.h"
#include "options.h"
#include "memory.h"
#include "rommgr.h"
#include "custom.h"
#include "events.h"
#include "newcpu.h"
#include "traps.h"
#include "disk.h"
#include "uae.h"
#include "threaddep/thread.h"
#include "filesys.h"
#include "autoconf.h"
#include "inputdevice.h"
#include "inputrecord.h"
#include "xwin.h"
#include "keyboard.h"
#include "zfile.h"
#include "parallel.h"
#include "audio.h"
#include "arcadia.h"
#include "drawing.h"
#include "fsdb.h"
#include "blkdev.h"
#include "render.h"
#include "win32.h"
#include "registry.h"
#include "picasso96_win.h"
#include "win32gui.h"
#include "win32gfx.h"
#include "sounddep/sound.h"
#include "od-win32/parser.h"
#include "od-win32/ahidsound.h"
#include "target.h"
#include "savestate.h"
#include "avioutput.h"
#include "direct3d.h"
#include "akiko.h"
#include "cdtv.h"
#include "gfxfilter.h"
#include "driveclick.h"
#include "scsi.h"
#include "cpuboard.h"
#include "x86.h"
#include "sana2.h"
#ifdef PROWIZARD
#include "moduleripper.h"
#endif
#include "catweasel.h"
#include "lcd.h"
#include "uaeipc.h"
#include "crc32.h"
#include "rp.h"
#include "statusline.h"
#include "zarchive.h"
#include "gfxboard.h"
#include "win32_uaenet.h"
#include "uae/ppc.h"
#ifdef RETROPLATFORM
#include "rp.h"
#endif
#include "ini.h"
#include "specialmonitors.h"
#include "gayle.h"
#include "keybuf.h"
#ifdef FLOPPYBRIDGE
#include "floppybridge/floppybridge_abstract.h"
#include "floppybridge/floppybridge_lib.h"
#endif

#include <Vssym32.h>
#include "darkmode.h"

#define GUI_SCALE_DEFAULT 100


#define ARCHIVE_STRING _T("*.zip;*.7z;*.rar;*.lha;*.lzh;*.lzx")

#define DISK_FORMAT_STRING _T("(*.adf;*.adz;*.gz;*.dms;*.ipf;*.scp;*.fdi;*.exe)\0*.adf;*.adz;*.gz;*.dms;*.ipf;*.scp;*.fdi;*.exe;*.ima;*.wrp;*.dsq;*.st;*.raw;") ARCHIVE_STRING _T("\0")
#define ROM_FORMAT_STRING _T("(*.rom;*.roz;*.bin;*.a500;*.a600;*.a1200;*.a4000)\0*.rom;*.roz;*.bin;*.a500;*.a600;*.a1200;*.a4000;") ARCHIVE_STRING _T("\0")
#define USS_FORMAT_STRING_RESTORE _T("(*.uss)\0*.uss;*.gz;") ARCHIVE_STRING _T("\0")
#define USS_FORMAT_STRING_SAVE _T("(*.uss)\0*.uss\0")
#define HDF_FORMAT_STRING _T("(*.hdf;*.vhd;*.rdf;*.hdz;*.rdz;*.chd)\0*.hdf;*.vhd;*.rdf;*.hdz;*.rdz;*.chd\0")
#define INP_FORMAT_STRING _T("(*.inp)\0*.inp\0")
#define  CD_FORMAT_STRING _T("(*.cue;*.ccd;*.mds;*.iso;*.chd;*.nrg)\0*.cue;*.ccd;*.mds;*.iso;*.chd;*.nrg;") ARCHIVE_STRING _T("\0")
#define GEO_FORMAT_STRING _T("(*.geo)\0*.geo\0")
#define CONFIG_HOST _T("Host")
#define CONFIG_HARDWARE _T("Hardware")

#define SOUND_BUFFER_MULTIPLIER 1024

static wstring szNone;

static int allow_quit;
static int restart_requested;
int full_property_sheet = 1;
static struct uae_prefs *pguiprefs;
struct uae_prefs workprefs;
static int currentpage = -1;
static int qs_request_reset;
static int qs_override;
int gui_active, gui_left;
static struct newresource *panelresource;
int dialog_inhibit;
static HMODULE hHtmlHelp;
pathtype path_type;

int externaldialogactive;

struct customdialogstate cdstate;

static int CustomCreateDialogBox(int templ, HWND hDlg, DLGPROC proc);

void HtmlHelp(const TCHAR *panel)
{
	TCHAR help_file[MAX_DPATH];
	int found = -1;
	const TCHAR *ext[] = { _T("chm"), _T("pdf"), NULL };
	const TCHAR *chm = _T("WinUAE");
	for (int i = 0; ext[i] != NULL; i++) {
		_stprintf(help_file, _T("%s%s.%s"), start_path_data, chm, ext[i]);
		if (!zfile_exists(help_file)) {
			_stprintf(help_file, _T("%s%s.%s"), start_path_exe, chm, ext[i]);
		}
		if (zfile_exists(help_file)) {
			found = i;
			break;
		}
	}
	if (found == 0) {
		if (!hHtmlHelp) {
			hHtmlHelp = LoadLibrary(_T("HHCTRL.OCX"));
		}
		if (hHtmlHelp) {
			HWND(WINAPI * pHtmlHelp)(HWND, LPCWSTR, UINT, LPDWORD);
			pHtmlHelp = (HWND(WINAPI *)(HWND, LPCWSTR, UINT, LPDWORD))GetProcAddress(hHtmlHelp, "HtmlHelpW");
			if (pHtmlHelp) {
				pHtmlHelp(NULL, help_file, 0, (LPDWORD)panel);
				return;
			}
		}
	}
	if (found <= 0) {
		_tcscpy(help_file, _T("https://www.winuae.net/help/"));
		if (panel) {
			_tcscat(help_file, _T("#"));
			_tcscat(help_file, panel);
		}
	}
	HINSTANCE h = ShellExecute(NULL, _T("open"), help_file, NULL, NULL, SW_SHOWNORMAL);
	if ((INT_PTR)h <= 32) {
		TCHAR szMessage[MAX_DPATH];
		WIN32GUI_LoadUIString(IDS_NOHELP, szMessage, MAX_DPATH);
		gui_message(szMessage);
	}
}

extern TCHAR help_file[MAX_DPATH];

extern int mouseactive;

TCHAR config_filename[256] = _T("");
static TCHAR config_pathfilename[MAX_DPATH];
static TCHAR config_folder[MAX_DPATH];
static TCHAR config_search[MAX_DPATH];
static TCHAR stored_path[MAX_DPATH];
static int gui_size_changed;
static int filterstackpos = 2 * MAX_FILTERSHADERS;

extern std::vector<FloppyBridgeAPI::FloppyBridgeProfileInformation> bridgeprofiles;

bool isguiactive(void)
{
	return gui_active > 0;
}

static const int defaultaspectratios[] = {
		5, 4, 4, 3, 16, 10, 15, 9, 27, 16, 128, 75, 16, 9, 256, 135, 21, 9, 32, 9, 16, 3,
		-1
};
static int getaspectratioindex (int ar)
{
	for (int i = 0; defaultaspectratios[i] >= 0; i += 2) {
		if (ar == defaultaspectratios[i + 0] * ASPECTMULT + defaultaspectratios[i + 1])
			return i / 2;
	}
	return 0;
}
static int getaspectratio (int index)
{
	for (int i = 0; defaultaspectratios[i] >= 0; i += 2) {
		if (i == index * 2) {
			return defaultaspectratios[i + 0] * ASPECTMULT + defaultaspectratios[i + 1];
		}
	}
	return 0;
}
static void addaspectratios (HWND hDlg, int id)
{
	for (int i = 0; defaultaspectratios[i] >= 0; i += 2) {
		TCHAR tmp[100];
		_stprintf (tmp, _T("%d:%d (%.2f)"), defaultaspectratios[i + 0], defaultaspectratios[i + 1], (double)defaultaspectratios[i + 0] / defaultaspectratios[i + 1]);
		xSendDlgItemMessage(hDlg, id, CB_ADDSTRING, 0, tmp);
	}
}

int scsiromselected = 0;
static int scsiromselectednum = 0;
static int scsiromselectedcatnum = 0;

#define Error(x) MessageBox (NULL, (x), _T("WinUAE Error"), MB_OK)

wstring WIN32GUI_LoadUIString (DWORD id)
{
	wchar_t tmp[MAX_DPATH];
	tmp[0] = 0;
	if (LoadString (hUIDLL ? hUIDLL : hInst, id, tmp, MAX_DPATH) == 0)
		LoadString (hInst, id, tmp, MAX_DPATH);
	return wstring(tmp);
}

void WIN32GUI_LoadUIString (DWORD id, TCHAR *string, DWORD dwStringLen)
{
	if (LoadString (hUIDLL ? hUIDLL : hInst, id, string, dwStringLen) == 0)
		LoadString (hInst, id, string, dwStringLen);
}

static int quickstart_model = 0, quickstart_conf = 0, quickstart_compa = 0;
static int quickstart_model_confstore[16];
static int quickstart_floppy = 1, quickstart_cd = 0, quickstart_ntsc = 0;
static int quickstart_floppytype[2], quickstart_floppysubtype[2];
static TCHAR quickstart_floppysubtypeid[2][32];
static int quickstart_cdtype = 0;
static TCHAR quickstart_cddrive[16];
static int quickstart_ok, quickstart_ok_floppy;
// don't enable yet. issues with quickstart panel
static bool firstautoloadconfig = false;
static void addfloppytype (HWND hDlg, int n);
static void addfloppyhistory (HWND hDlg);
static void addhistorymenu (HWND hDlg, const TCHAR*, int f_text, int type, bool manglepath, int num);
static void addcdtype (HWND hDlg, int id);
static void getfloppyname (HWND hDlg, int n, int cd, int f_text);

static int C_PAGES;
#define MAX_C_PAGES 30
static int LOADSAVE_ID = -1, MEMORY_ID = -1, KICKSTART_ID = -1, CPU_ID = -1,
	DISPLAY_ID = -1, HW3D_ID = -1, CHIPSET_ID = -1, CHIPSET2_ID = -1, SOUND_ID = -1, FLOPPY_ID = -1, DISK_ID = -1,
	HARDDISK_ID = -1, IOPORTS_ID = -1, GAMEPORTS_ID = -1, INPUT_ID = -1, MISC1_ID = -1, MISC2_ID = -1,
	AVIOUTPUT_ID = -1, PATHS_ID = -1, QUICKSTART_ID = -1, ABOUT_ID = -1, EXPANSION_ID = -1, EXPANSION2_ID = -1,
	BOARD_ID = -1, FRONTEND_ID = -1;
static const int INPUTMAP_ID = MAX_C_PAGES - 1;
static HWND pages[MAX_C_PAGES];
#define MAX_IMAGETOOLTIPS 10
static HWND guiDlg, panelDlg, ToolTipHWND;
static struct dlgcontext  maindctx;
static HACCEL hAccelTable;
static HWND customDlg;
static int customDlgType;
struct ToolTipHWNDS {
	WNDPROC proc;
	HWND hwnd;
	int imageid;
};
static struct ToolTipHWNDS ToolTipHWNDS2[MAX_IMAGETOOLTIPS + 1];
struct GUIPAGE {
	DLGPROC dlgproc;
	LPCTSTR title;
	LPCTSTR icon;
	HTREEITEM tv;
	int himg;
	int idx;
	const TCHAR *help;
	HACCEL accel;
	int fullpanel;
	struct newresource *nres;
	int focusid;
};
static struct GUIPAGE ppage[MAX_C_PAGES];

static void CALLBACK gui_control_cb(HWND h, UINT v, UINT_PTR v3, DWORD v4)
{
	HWND ha = GetActiveWindow();
	if (externaldialogactive) {
		if (ha) {
			HWND p = GetParent(ha);
			HWND mainhwnd = AMonitors[0].hAmigaWnd;
			if (p && (p == cdstate.hwnd || p == guiDlg || p == mainhwnd)) {
				process_gui_control(ha, NULL);
			}
		}
	} else if (cdstate.active) {
		if (ha == cdstate.hwnd) {
			process_gui_control(cdstate.hwnd, cdstate.res);
		}
	} else {
		if (ha == h) {
			process_gui_control(h, panelresource);
		}
	}
}

static bool ischecked(HWND hDlg, DWORD id)
{
	return IsDlgButtonChecked(hDlg, id) == BST_CHECKED;
}
static void setchecked(HWND hDlg, DWORD id, bool checked)
{
	CheckDlgButton(hDlg, id, checked ? BST_CHECKED : BST_UNCHECKED);
}
static void setfocus(HWND hDlg, int id)
{
	SendMessage(hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(hDlg, id), TRUE);
}
static void ew(HWND hDlg, DWORD id, int enable)
{
	if (id == -1)
		return;
	HWND w = GetDlgItem(hDlg, id);
	if (!w)
		return;
	if (!enable && w == GetFocus())
		SendMessage(hDlg, WM_NEXTDLGCTL, 0, FALSE);
	EnableWindow(w, !!enable);
}
static void hide(HWND hDlg, DWORD id, int hide)
{
	HWND w;
	if (id == -1)
		return;
	w = GetDlgItem(hDlg, id);
	if (!w)
		return;
	if (hide && w == GetFocus())
		SendMessage(hDlg, WM_NEXTDLGCTL, 0, FALSE);
	ShowWindow(w, hide ? SW_HIDE : SW_SHOW);
}

static void parsefilepath(TCHAR *path, int maxlen)
{
	TCHAR *tmp = xmalloc(TCHAR, maxlen + 1);
	_tcscpy(tmp, path);
	TCHAR *p1 = _tcsstr(tmp, _T(" { "));
	TCHAR *p2 = _tcsstr(tmp, _T(" }"));
	if (p1 && p2 && p2 > p1) {
		*p1 = 0;
		memset(path, 0, maxlen * sizeof(TCHAR));
		memcpy(path, p1 + 3, (p2 - p1 - 3) * sizeof(TCHAR));
		_tcscat(path, tmp);
	}
	xfree(tmp);
}

static int scsiromselect_table[MAX_ROMMGR_ROMS];

static bool getcomboboxtext(HWND hDlg, int id, TCHAR *out, int maxlen)
{
	out[0] = 0;
	int posn = xSendDlgItemMessage(hDlg, id, CB_GETCURSEL, 0, 0L);
	if (posn == CB_ERR) {
		GetDlgItemText(hDlg, id, out, maxlen);
		return true;
	}
	int len = xSendDlgItemMessage(hDlg, id, CB_GETLBTEXTLEN, posn, 0);
	if (len < maxlen) {
		len = xSendDlgItemMessage(hDlg, id, CB_GETLBTEXT, posn, out);
	}
	return true;
}

static void gui_add_string(int *table, HWND hDlg, int item, int id, const TCHAR *str)
{
	while (*table >= 0)
		table++;
	*table++ = id;
	*table = -1;
	xSendDlgItemMessage(hDlg, item, CB_ADDSTRING, 0, str);
}
static void gui_set_string_cursor(int *table, HWND hDlg, int item, int id)
{
	int idx = 0;
	while (*table >= 0) {
		if (*table == id) {
			xSendDlgItemMessage(hDlg, item, CB_SETCURSEL, idx, 0);
			return;
		}
		idx++;
		table++;
	}
}
static int gui_get_string_cursor(int *table, HWND hDlg, int item)
{
	int posn = xSendDlgItemMessage (hDlg, item, CB_GETCURSEL, 0, 0);
	if (posn < 0)
		return CB_ERR;
	return table[posn];
}

INT_PTR commonproc2(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam, bool *handled)
{
	if (dialog_inhibit) {
		*handled = true;
		return FALSE;
	}

	if (msg == WM_INITDIALOG) {
		darkmode_initdialog(hDlg);
	} else if (msg == WM_CTLCOLORDLG || msg == WM_CTLCOLORSTATIC) {
		INT_PTR v = darkmode_ctlcolor(wParam, handled);
		if (*handled) {
			return v;
		}
	} else if (msg == WM_THEMECHANGED) {
		darkmode_themechanged(hDlg);
	}
	*handled = false;
	return 0;
}

static INT_PTR commonproc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam, bool *handled)
{
	*handled = false;

	if (dialog_inhibit) {
		*handled = true;
		return FALSE;
	}

	if (msg == WM_DPICHANGED) {
		RECT *const r = (RECT *)lParam;
		SetWindowPos(hDlg, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
		*handled = true;
	} else {
		INT_PTR v = commonproc2(hDlg, msg, wParam, lParam, handled);
		if (*handled) {
			return v;
		}
	}
	return 0;
}

static INT_PTR CALLBACK StringBoxDialogProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	bool handled;
	INT_PTR v = commonproc(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return v;
	}

	switch(msg)
	{
	case WM_DESTROY:
		PostQuitMessage (0);
		return TRUE;
	case WM_CLOSE:
		CustomDialogClose(hDlg, 0);
		DestroyWindow (hDlg);
		return TRUE;
	case WM_INITDIALOG:
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDOK:
			CustomDialogClose(hDlg, 1);
			return TRUE;
		case IDCANCEL:
			CustomDialogClose(hDlg, 0);
			return TRUE;
		}
		break;
	}
	return FALSE;
}

static int CALLBACK BrowseForFolderCallback (HWND hwnd, UINT uMsg, LPARAM lp, LPARAM pData)
{
	TCHAR szPath[MAX_DPATH];
	switch(uMsg)
	{
	case BFFM_INITIALIZED:
		SendMessage (hwnd, BFFM_SETSELECTION, TRUE, pData);
		break;
	case BFFM_SELCHANGED: 
		if (SHGetPathFromIDList ((LPITEMIDLIST)lp ,szPath))
			SendMessage(hwnd, BFFM_SETSTATUSTEXT, 0, (LPARAM)szPath);	
		break;
	}
	return 0;
}
static int DirectorySelection2 (OPENFILENAME *ofn)
{
	BROWSEINFO bi;
	LPITEMIDLIST pidlBrowse;
	TCHAR buf[MAX_DPATH], fullpath[MAX_DPATH];
	TCHAR *path = ofn->lpstrFile;
	int ret = 0;

	buf[0] = 0;
	memset (&bi, 0, sizeof bi);
	bi.hwndOwner = ofn->hwndOwner;
	bi.pidlRoot = NULL;
	bi.pszDisplayName = buf;
	bi.lpszTitle = NULL;
	bi.ulFlags = BIF_DONTGOBELOWDOMAIN | BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	if (path[0] && GetFullPathName (path, sizeof fullpath / sizeof (TCHAR), fullpath, NULL)) {
		bi.lpfn = BrowseForFolderCallback;
		bi.lParam = (LPARAM)fullpath;
	}
	// Browse for a folder and return its PIDL.
	pidlBrowse = SHBrowseForFolder (&bi);
	if (pidlBrowse != NULL) {
		if (SHGetPathFromIDList (pidlBrowse, buf)) {
			_tcscpy (path, buf);
			ret = 1;
		}
		CoTaskMemFree (pidlBrowse);
	}
	return ret;
}

static TCHAR *getfilepath (TCHAR *s)
{
	TCHAR *p = _tcsrchr (s, '\\');
	if (p)
		return p + 1;
	return NULL;
}

typedef HRESULT (CALLBACK* SHCREATEITEMFROMPARSINGNAME)
	(PCWSTR,IBindCtx*,REFIID,void**); // Vista+ only

// OPENFILENAME->IFileOpenDialog wrapper
static BOOL GetFileDialog (OPENFILENAME *opn, const GUID *guid, int mode)
{
	SHCREATEITEMFROMPARSINGNAME pSHCreateItemFromParsingName;
	HRESULT hr;
	IFileOpenDialog *pfd;
	FILEOPENDIALOGOPTIONS pfos;
	IShellItem *shellitem = NULL;
	int ret;
	COMDLG_FILTERSPEC *fs = NULL;
	int filtercnt = 0;

	static const GUID fsdialogguid = { 0xe768b477, 0x3684, 0x4128, { 0x91, 0x55, 0x8c, 0x8f, 0xd9, 0x2d, 0x16, 0x7b } };

	if (isfullscreen () > 0)
		guid = &fsdialogguid;

	hr = E_FAIL;
	ret = 0;
	pSHCreateItemFromParsingName = (SHCREATEITEMFROMPARSINGNAME)GetProcAddress (
		GetModuleHandle (_T("shell32.dll")), "SHCreateItemFromParsingName");
	if (pSHCreateItemFromParsingName)
		hr = CoCreateInstance (mode > 0 ? __uuidof(FileSaveDialog) : __uuidof(FileOpenDialog), 
		NULL, 
		CLSCTX_INPROC_SERVER, 
		mode > 0 ? IID_IFileSaveDialog : IID_IFileOpenDialog, (LPVOID*)&pfd);
	if (FAILED (hr)) {
		if (mode > 0)
			return GetSaveFileName (opn);
		else if (mode == 0)
			return GetOpenFileName (opn);
		else
			return DirectorySelection2 (opn);
	}
	pfd->GetOptions (&pfos);
	pfos |= FOS_FORCEFILESYSTEM;
	if (!(opn->Flags & OFN_FILEMUSTEXIST))
		pfos &= ~FOS_FILEMUSTEXIST;
	if (opn->Flags & OFN_ALLOWMULTISELECT)
		pfos |= FOS_ALLOWMULTISELECT;
	if (mode < 0)
		pfos |= FOS_PICKFOLDERS;
	pfd->SetOptions (pfos);
	opn->nFileOffset = 0;

	if (guid)
		pfd->SetClientGuid (*guid);

	if (opn->lpstrFilter) {
		const TCHAR *p = opn->lpstrFilter;
		int i;
		while (*p) {
			p += _tcslen (p) + 1;
			p += _tcslen (p) + 1;
			filtercnt++;
		}
		if (filtercnt) {
			fs = xmalloc (COMDLG_FILTERSPEC, filtercnt);
			p = opn->lpstrFilter;
			for (i = 0; i < filtercnt; i++) {
				fs[i].pszName = p;
				p += _tcslen (p) + 1;
				fs[i].pszSpec = p;
				p += _tcslen (p) + 1;
			}
			pfd->SetFileTypes (filtercnt, fs);
		}
		pfd->SetFileTypeIndex (opn->nFilterIndex);
	}

	if (mode >= 0 && opn->lpstrFile) {
		pfd->SetFileName(opn->lpstrFile);
	}
	if (opn->lpstrTitle) {
		pfd->SetTitle (opn->lpstrTitle);
	}
	if (opn->lpstrDefExt) {
		pfd->SetDefaultExtension (opn->lpstrDefExt);
	}
	if (opn->lpstrInitialDir) {
		TCHAR tmp[MAX_DPATH];
		const TCHAR *p = opn->lpstrInitialDir;
		if (GetFullPathName (p, sizeof tmp / sizeof (TCHAR), tmp, NULL)) 
			p = tmp;
		hr = pSHCreateItemFromParsingName (p, NULL, IID_IShellItem, (void**)&shellitem);
		if (SUCCEEDED (hr))
			pfd->SetFolder (shellitem);
	}

	externaldialogactive = 1;
	// GUI control without GUI
	if (gui_control && hGUIWnd == NULL) {
		HWND h = AMonitors[0].hAmigaWnd;
		if (h) {
			SetTimer(h, 8, 20, gui_control_cb);
		}
	}

	hr = pfd->Show(opn->hwndOwner);

	externaldialogactive = 0;
	if (gui_control && hGUIWnd == NULL) {
		HWND h = AMonitors[0].hAmigaWnd;
		if (h) {
			KillTimer(h, 8);
		}
	}

	if (SUCCEEDED (hr)) {
		UINT idx;
		IShellItemArray *pitema;
		opn->lpstrFile[0] = 0;
		opn->lpstrFile[1] = 0;
		if (opn->lpstrFileTitle)
			opn->lpstrFileTitle[0] = 0;
		if (mode > 0) {
			IShellItem *pitem;
			hr = pfd->GetResult (&pitem);
			if (SUCCEEDED (hr)) {
				WCHAR *path = NULL;
				hr = pitem->GetDisplayName (SIGDN_FILESYSPATH, &path);
				if (SUCCEEDED (hr)) {
					TCHAR *p = opn->lpstrFile;
					_tcscpy (p, path);
					p[_tcslen (p) + 1] = 0;
					p = getfilepath (opn->lpstrFile);
					if (p && opn->lpstrFileTitle)
						_tcscpy (opn->lpstrFileTitle, p);
				}
				pitem->Release ();
			}
		} else {
			hr = pfd->GetResults (&pitema);
			if (SUCCEEDED (hr)) {
				DWORD cnt;
				hr = pitema->GetCount (&cnt);
				if (SUCCEEDED (hr)) {
					int i, first = true;
					for (i = 0; i < cnt; i++) {
						IShellItem *pitem;
						hr = pitema->GetItemAt (i, &pitem);
						if (SUCCEEDED (hr)) {
							WCHAR *path = NULL;
							hr = pitem->GetDisplayName (SIGDN_FILESYSPATH, &path);
							if (SUCCEEDED (hr)) {
								TCHAR *ppath = path;
								TCHAR *p = opn->lpstrFile;
								while (*p)
									p += _tcslen (p) + 1;
								TCHAR *pathfilename = _tcsrchr (ppath, '\\');
								if (pathfilename)
									pathfilename++;
								if (first && cnt > 1) {
									opn->nFileOffset = (WORD)(pathfilename - ppath);
									_tcscpy (p, ppath);
									p[opn->nFileOffset - 1] = 0;
									p += _tcslen (p) + 1;
									*p = 0;
									ppath = pathfilename;
								} else if (cnt > 1) {
									ppath = pathfilename;
								} else {
									ppath = path;
								}
								if (!ppath)
									ppath = path;
								if (p - opn->lpstrFile + _tcslen (ppath) + 2 < opn->nMaxFile) {
									_tcscpy (p, ppath);
									p[_tcslen (p) + 1] = 0;
								}
								if (opn->lpstrFileTitle && !opn->lpstrFileTitle[0]) {
									p = getfilepath (opn->lpstrFile);
									if (p && opn->lpstrFileTitle)
										_tcscpy (opn->lpstrFileTitle, p);
								}
								first = false;
							}
							CoTaskMemFree (path);
						}
					}
				}
				pitema->Release ();
			}
		}
		hr = pfd->GetFileTypeIndex (&idx);
		if (SUCCEEDED (hr))
			opn->nFilterIndex = idx;
		ret = 1;
	}


	pfd->Release ();
	if (shellitem)
		shellitem->Release ();
	if (filtercnt) {
		xfree (fs);
	}
	return ret;
}

static BOOL GetOpenFileName_2 (HWND parent, OPENFILENAME *opn, const GUID *guid)
{
	BOOL val;
	val = GetFileDialog (opn, guid, 0);
	return val;
}
static BOOL GetSaveFileName_2 (HWND parent, OPENFILENAME *opn, const GUID *guid)
{
	BOOL val;
	val = GetFileDialog (opn, guid, 1);
	return val;
}
int DirectorySelection (HWND hDlg, const GUID *guid, TCHAR *path)
{
	int val;
	OPENFILENAME ofn = { 0 };
	ofn.hwndOwner = hDlg;
	ofn.lpstrFile = path;
	ofn.lpstrInitialDir = path;
	ofn.nMaxFile = MAX_DPATH;
	val = GetFileDialog (&ofn, NULL, -1);
	fullpath (path, MAX_DPATH);
	return val;
}

static const TCHAR *historytypes[] =
{
	_T("DiskImageMRUList"),
	_T("CDImageMRUList"),
	_T("DirFileSysMRUList"),
	_T("HardfileMRUList"),
	_T("FileSysMRUList"),
	_T("TapeImageMRUList"),
	_T("GenlockImageMRUList"),
	_T("GenlockVideoMRUList"),
	_T("GeometryMRUList"),
	_T("StatefileMRUList"),
	_T("ConfigfileMRUList")
};
static int regread;

static void write_disk_history2 (int type)
{
	int i, j;
	TCHAR tmp[16];
	UAEREG *fkey;

	if (!(regread & (1 << type)))
		return;
	fkey = regcreatetree (NULL, historytypes[type]);
	if (fkey == NULL)
		return;
	j = 1;
	for (i = 0; i <= MAX_PREVIOUS_IMAGES; i++) {
		TCHAR *s = DISK_history_get (i, type);
		if (s == 0 || _tcslen (s) == 0)
			continue;
		_stprintf (tmp, _T("Image%02d"), j);
		regsetstr (fkey, tmp, s);
		j++;
	}
	while (j <= MAX_PREVIOUS_IMAGES) {
		const TCHAR *s = _T("");
		_stprintf (tmp, _T("Image%02d"), j);
		regsetstr (fkey, tmp, s);
		j++;
	}
	regclosetree (fkey);
}
void write_disk_history (void)
{
	write_disk_history2(HISTORY_FLOPPY);
	write_disk_history2(HISTORY_CD);
	write_disk_history2(HISTORY_DIR);
	write_disk_history2(HISTORY_HDF);
	write_disk_history2(HISTORY_FS);
	write_disk_history2(HISTORY_TAPE);
	write_disk_history2(HISTORY_GENLOCK_IMAGE);
	write_disk_history2(HISTORY_GENLOCK_VIDEO);
	write_disk_history2(HISTORY_GEO);
	write_disk_history2(HISTORY_STATEFILE);
	write_disk_history2(HISTORY_CONFIGFILE);
}

void reset_disk_history (void)
{
	int i, rrold;

	for (i = 0; i < MAX_PREVIOUS_IMAGES; i++) {
		DISK_history_add(NULL, i, HISTORY_FLOPPY, 0);
		DISK_history_add(NULL, i, HISTORY_CD, 0);
		DISK_history_add(NULL, i, HISTORY_DIR, 0);
		DISK_history_add(NULL, i, HISTORY_HDF, 0);
		DISK_history_add(NULL, i, HISTORY_FS, 0);
		DISK_history_add(NULL, i, HISTORY_TAPE, 0);
		DISK_history_add(NULL, i, HISTORY_GENLOCK_IMAGE, 0);
		DISK_history_add(NULL, i, HISTORY_GENLOCK_VIDEO, 0);
		DISK_history_add(NULL, i, HISTORY_GEO, 0);
		DISK_history_add(NULL, i, HISTORY_STATEFILE, 0);
		DISK_history_add(NULL, i, HISTORY_CONFIGFILE, 0);
	}
	rrold = regread;
	regread = (1 << HISTORY_MAX) - 1;
	write_disk_history ();
	regread = rrold;
}

UAEREG *read_disk_history (int type)
{
	TCHAR tmp2[MAX_DPATH];
	int size, size2;
	int idx, idx2;
	UAEREG *fkey;
	TCHAR tmp[MAX_DPATH];

	fkey = regcreatetree (NULL, historytypes[type]);
	if (fkey == NULL || (regread & (1 << type)))
		return fkey;

	idx = 0;
	for (;;) {
		size = sizeof (tmp) / sizeof (TCHAR);
		size2 = sizeof (tmp2) / sizeof (TCHAR);
		if (!regenumstr (fkey, idx, tmp, &size, tmp2, &size2))
			break;
		if (_tcslen (tmp) == 7) {
			idx2 = _tstol (tmp + 5) - 1;
			if (idx2 >= 0)
				DISK_history_add (tmp2, idx2, type, 1);
		}
		idx++;
	}
	regread |= 1 << type;
	return fkey;
}

void exit_gui (int ok)
{
	if (!gui_active)
		return;
	if (guiDlg && hGUIWnd) {
		SendMessage (guiDlg, WM_COMMAND, ok ? IDOK : IDCANCEL, 0);
	}
}

static int getcbn (HWND hDlg, int v, TCHAR *out, int maxlen)
{
	LRESULT val = xSendDlgItemMessage (hDlg, v, CB_GETCURSEL, 0, 0L);
	out[0] = 0;
	if (val == CB_ERR) {
		xSendDlgItemMessage (hDlg, v, WM_GETTEXT, maxlen, out);
		return 1;
	} else {
		int len = xSendDlgItemMessage(hDlg, v, CB_GETLBTEXTLEN, val, 0);
		if (len < maxlen) {
			val = xSendDlgItemMessage(hDlg, v, CB_GETLBTEXT, val, out);
		}
		return 0;
	}
}


struct favitems
{
	TCHAR *value;
	TCHAR *path;
	int type;
};

#define MAXFAVORITES 30
#define MAXFAVORITESPACE 99
static void writefavoritepaths (int num, struct favitems *fitem)
{
	int i, idx;
	UAEREG *fkey;

	fkey = regcreatetree (NULL, _T("FavoritePaths"));
	if (fkey == NULL)
		return;
	idx = 0;
	for (i = 0; i < num; i++) {
		TCHAR str[MAX_DPATH];
		TCHAR key[100];
		if (fitem[i].type != 1)
			continue;
		if (!_tcscmp (fitem[i].value, fitem[i].path))
			_tcscpy (str, fitem[i].value);
		else
			_stprintf (str, _T("%s \"%s\""), fitem[i].value, fitem[i].path);
		_stprintf (key, _T("PATH_ALL_%02d"), idx + 1);
		idx++;
		regsetstr (fkey, key, str);
		xfree (fitem[i].value);
		xfree (fitem[i].path);
	}
	while (idx < MAXFAVORITES) {
		TCHAR key[100];
		_stprintf (key, _T("PATH_ALL_%02d"), idx + 1);
		regdelete (fkey, key);
		idx++;
	}
	regclosetree (fkey);
}


static int askinputcustom (HWND hDlg, TCHAR *custom, int maxlen, DWORD titleid);
static int addfavoritepath (HWND hDlg, int num, struct favitems *fitem)
{
	TCHAR name[MAX_DPATH];
	const GUID favoriteguid = 
	{ 0xed6e5ad9, 0xc0aa, 0x42fb, { 0x83, 0x3, 0x37, 0x41, 0x77, 0xb4, 0x6f, 0x18 } };

	if (num >= MAXFAVORITES)
		return 0;
	if (!stored_path[0])
		GetModuleFileName (NULL, stored_path, MAX_DPATH);
	while (stored_path[0]) {
		DWORD v = GetFileAttributes (stored_path);
		TCHAR *s;
		if (v == INVALID_FILE_ATTRIBUTES)
			break;
		if (v & FILE_ATTRIBUTE_DIRECTORY)
			break;
		s = _tcsrchr (stored_path, '\\');
		if (!s)
			s = _tcsrchr (stored_path, '/');
		if (!s) {
			stored_path[0] = 0;
			break;
		}
		s[0] = 0;
	}
	if (!DirectorySelection (hDlg, &favoriteguid, stored_path))
		return 0;
	_tcscpy (name, stored_path);
	if (askinputcustom (hDlg, name, sizeof name / sizeof (TCHAR), IDS_SB_FAVORITENAME)) {
		fitem[num].value = my_strdup (name);
		fitem[num].path = my_strdup (stored_path);
		fitem[num].type = 1;
		num++;
		writefavoritepaths (num, fitem);
	}
	return 1;
}
static void removefavoritepath (int idx, int num, struct favitems *fitem)
{
	int i;

	xfree (fitem[idx].value);
	xfree (fitem[idx].path);
	fitem[idx].value = fitem[idx].path = NULL;
	for (i = idx; i < num - 1; i++) {
		fitem[i].value = fitem[i + 1].value;
		fitem[i].path = fitem[i + 1].path;
	}
	num--;
	writefavoritepaths (num, fitem);
}

static void addeditmenu (HMENU menu, struct favitems *fitem)
{
	int i;
	HMENU emenu = CreatePopupMenu ();
	TCHAR newpath[MAX_DPATH];

	MENUITEMINFO mii = { 0 };
	mii.cbSize = sizeof mii;

	mii.fMask = MIIM_FTYPE;
	mii.fType = MFT_SEPARATOR;
	mii.fState = MFS_ENABLED;
	InsertMenuItem (menu, -1, TRUE, &mii);

	mii.fMask = MIIM_STRING | MIIM_ID;
	mii.fType = MFT_STRING;
	mii.fState = MFS_ENABLED;
	mii.dwTypeData = _T("Add New");
	mii.cch = uaetcslen(mii.dwTypeData);
	mii.wID = 1000;
	InsertMenuItem (emenu, -1, TRUE, &mii);
	i = 0;
	while (fitem[i].type) {
		if (fitem[i].type == 1) {
			mii.fMask = MIIM_STRING | MIIM_ID;
			mii.fType = MFT_STRING;
			mii.fState = MFS_ENABLED;
			mii.wID = 1001 + i;
			_stprintf (newpath, _T("Remove '%s'"), fitem[i].value);
			mii.dwTypeData = newpath;
			mii.cch = uaetcslen(mii.dwTypeData);
			InsertMenuItem (emenu, -1, TRUE, &mii);
		}
		i++;
	}

	mii.fMask = MIIM_STRING | MIIM_SUBMENU;
	mii.fType = MFT_STRING;
	mii.fState = MFS_ENABLED;
	mii.dwTypeData = _T("Edit");
	mii.cch = uaetcslen(mii.dwTypeData);
	mii.hSubMenu = emenu;
	InsertMenuItem (menu, -1, TRUE, &mii);
}

static int popupmenu (HWND hwnd, struct favitems *items, int morefiles)
{
	int i, item, got;
	HMENU menu;
	POINT pt;

	menu = CreatePopupMenu ();
	got = 0;
	i = 0;
	while (items[i].type) {
		if (items[i].type >= 2) {
			MENUITEMINFO mii = { 0 };
			mii.cbSize = sizeof mii;
			mii.fMask = MIIM_STRING | MIIM_ID;
			mii.fType = MFT_STRING;
			mii.fState = MFS_ENABLED;
			mii.wID = items[i].type == 2 ? 1 + i : 990 - 3 + items[i].type;
			mii.dwTypeData = items[i].value;
			mii.cch = uaetcslen(mii.dwTypeData);
			InsertMenuItem (menu, -1, TRUE, &mii);
			got = 1;
		}
		i++;
	}
	if (morefiles < 0) {
		MENUITEMINFO mii = { 0 };
		mii.cbSize = sizeof mii;
		mii.fMask = MIIM_STRING | MIIM_ID;
		mii.fType = MFT_STRING;
		mii.fState = MFS_ENABLED;
		mii.wID = 999;
		mii.dwTypeData = _T("[Directory scan]");
		mii.cch = uaetcslen(mii.dwTypeData);
		InsertMenuItem (menu, -1, TRUE, &mii);
		got = 1;
	}
	if (got) {
		MENUITEMINFO mii = { 0 };
		mii.cbSize = sizeof mii;
		mii.fMask = MIIM_FTYPE;
		mii.fType = MFT_SEPARATOR;
		mii.fState = MFS_ENABLED;
		InsertMenuItem (menu, -1, TRUE, &mii);
	}
	i = 0;
	while (items[i].type) {
		if (items[i].type == 1) {
			MENUITEMINFO mii = { 0 };
			mii.cbSize = sizeof mii;
			mii.fMask = MIIM_STRING | MIIM_ID;
			mii.fType = MFT_STRING;
			mii.fState = MFS_ENABLED;
			mii.wID = 1 + i;
			mii.dwTypeData = items[i].value;
			mii.cch = uaetcslen(mii.dwTypeData);
			InsertMenuItem (menu, -1, TRUE, &mii);
		}
		i++;
	}
	addeditmenu (menu, items);
	GetCursorPos (&pt);
	item = TrackPopupMenu (menu, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_RIGHTBUTTON | TPM_RETURNCMD,
		pt.x, pt.y, 0, hwnd, NULL);
	PostMessage (hwnd, WM_NULL, 0, 0);
	DestroyMenu (menu);
	return item;
}

static void favitemsort (struct favitems *fitem, int start, int end)
{
	for (int i = start; i < end; i++) {
		for (int j = i + 1; j < end; j++) {
			if (_tcscmp (fitem[i].value, fitem[j].value) > 0) {
				struct favitems tmp;
				memcpy (&tmp, &fitem[i], sizeof tmp);
				memcpy (&fitem[i], &fitem[j], sizeof tmp);
				memcpy (&fitem[j], &tmp, sizeof tmp);
			}
		}
	}
}

static int getdeepfavdiskimage (TCHAR *imgpath, struct favitems *fitem, int idx)
{
	TCHAR path[MAX_DPATH], mask[MAX_DPATH];
	TCHAR *p;
	struct my_opendir_s *myd = NULL;
	int previdx = idx;

	if (!imgpath[0])
		return idx;
	_tcscpy (path, imgpath);
	mask[0] = 0;
	for (;;) {
		p = _tcsrchr (path, '\\');
		if (!p)
			p = _tcsrchr (path, '/');
		if (!p)
			break;
		if (!mask[0])
			_tcscpy (mask, p + 1);
		p[0] = 0;
		if (my_existsdir (path))
			break;
	}
	static TCHAR notallowed[] = _T("[]()_-#!{}=.,");
	for (int i = 0; i < _tcslen (notallowed); i++) {
		for (;;) {
			p = _tcsrchr (mask, notallowed[i]);
			if (!p)
				break;
			if (p - mask < 6)
				break;
			p[0] = 0;
		}
	}
	while (mask[_tcslen (mask) - 1] == ' ')
		mask[_tcslen (mask) - 1] = 0;
	_tcscat (mask, _T("*.*"));
	myd = my_opendir (path, mask);
	int cnt = 0;
	while (myd && cnt < 30) {
		TCHAR tmp[MAX_DPATH], tmp2[MAX_DPATH];
		if (!my_readdir (myd, tmp))
			break;
		_tcscpy (tmp2, path);
		_tcscat (tmp2, _T("\\"));
		_tcscat (tmp2, tmp);
		fitem[idx].value = my_strdup (tmp2);
		fitem[idx].path = NULL;
		fitem[idx].type = 2;
		idx++;
		cnt++;
	}
	my_closedir (myd);
	favitemsort (fitem, previdx, idx);
	fitem[idx].type = 0;
	return idx;
}

static int getfavdiskimage (TCHAR *imgpath, struct favitems *fitem, int idx)
{
	int i;
	TCHAR name[MAX_DPATH];

	_tcscpy (name, imgpath);
	int previdx = idx;
	for (;;) {
		if (!disk_prevnext_name (name, 1))
			break;
		for (i = previdx; i < idx; i++) {
			if (!_tcsicmp (fitem[i].value, name))
				break;
		}
		if (i < idx)
			break;
		fitem[idx].value = my_strdup (name);
		fitem[idx].path = NULL;
		fitem[idx].type = 2;
		idx++;
		if (!_tcscmp (name, imgpath))
			break;
	}
	favitemsort (fitem, previdx, idx);
	fitem[idx].type = 0;
	return idx;
}

static TCHAR *favoritepopup (HWND hwnd, int drive)
{
	UAEREG *fkey;
	int idx, idx2;
	struct favitems fitem[MAXFAVORITESPACE + 1];
	int ret, i, num;
	int srcdrive, dstdrive;
	int morefiles = 0;

	srcdrive = dstdrive = drive;
	for (;;) {
		fkey = regcreatetree (NULL, _T("FavoritePaths"));
		if (fkey == NULL)
			return NULL;
		idx = 0;
		num = 0;
		for (;;) {
			TCHAR *p;
			int size, size2;
			TCHAR tmp[1000], tmp2[1000];
			size = sizeof (tmp) / sizeof (TCHAR);
			size2 = sizeof (tmp2) / sizeof (TCHAR);
			if (!regenumstr (fkey, idx, tmp, &size, tmp2, &size2))
				break;
			p = _tcsrchr (tmp, '_');
			if (p) {
				idx2 = _tstol (p + 1);
				if (idx2 > 0 && idx2 < MAXFAVORITES) {
					TCHAR *p2 = _tcschr (tmp2, '"');
					TCHAR *str, *fname;
					idx2--;
					if (p2) {
						fname = my_strdup (p2 + 1);
						do {
							*p2-- = 0;
						} while (p2 > tmp2 && *p2 == ' ');
						p2 = _tcschr (fname, '"');
						if (p2)
							*p2 = 0;
						str = my_strdup (tmp2);
					} else {
						str = my_strdup (tmp2);
						fname = my_strdup (tmp2);
					}
					fitem[idx2].path = fname;
					fitem[idx2].value = str;
					fitem[idx2].type = 1;
				}
			}
			idx++;
		}
		regclosetree (fkey);
		favitemsort (fitem, 0, idx);
		fitem[idx].type = 0;

		if (srcdrive >= 0 && srcdrive <= 4) {
			if (!morefiles) {
				for (i = 0; i < 4; i++) {
					if (workprefs.floppyslots[i].df[0] && srcdrive != i) {
						TCHAR tmp[100];
						_stprintf (tmp, _T("[DF%c:]"), i + '0');
						fitem[idx].value = my_strdup (tmp);
						fitem[idx].path = my_strdup (workprefs.floppyslots[i].df);
						fitem[idx].type = 3 + i;
						idx++;
						fitem[idx].type = 0;
					}
				}
			}
			if (workprefs.floppyslots[srcdrive].df[0]) {
				if (morefiles > 0) {
					idx = getdeepfavdiskimage (workprefs.floppyslots[srcdrive].df, fitem, idx);
				} else {
					idx = getfavdiskimage (workprefs.floppyslots[srcdrive].df, fitem, idx);
					morefiles = -1;
				}
			}
		}


		ret = popupmenu (hwnd, fitem, morefiles);
		if (ret == 0)
			break;
		if (ret <= idx) {
			if (fitem[ret - 1].type == 2) {
				_tcscpy (workprefs.floppyslots[dstdrive].df, fitem[ret - 1].value);
				disk_insert (dstdrive, workprefs.floppyslots[dstdrive].df);
				ret = 0;
			}
			break;
		}
		if (ret >= 990 && ret <= 993) {
			srcdrive = ret - 990;
		} else if (ret == 999) {
			morefiles = 1;
		} else if (ret == 1000) {
			if (!addfavoritepath (hwnd, idx, fitem)) {
				ret = 0;
				break;
			}
		} else if (ret > 1000) {
			removefavoritepath (ret - 1001, idx, fitem);
		}
	}
	for (i = 0; i < idx; i++) {
		xfree (fitem[i].value);
		if (i != ret - 1)
			xfree (fitem[i].path);
	}
	if (ret == 0)
		return NULL;
	return fitem[ret - 1].path;
}
static TCHAR *favoritepopup (HWND hwnd)
{
	return favoritepopup (hwnd, -1);
}

/* base Drag'n'Drop code borrowed from http://www.codeproject.com/listctrl/jianghong.asp */

// NOTE (TW):
// ListView_CreateDragImage has been broken at least since Windows Vista?

static int bDragging = 0;
static HIMAGELIST hDragImageList;
static int DragHeight;
static int drag_start (HWND hWnd, HWND hListView, LPARAM lParam)
{
	POINT pt;
	int bFirst, iPos;
	POINT offset;

	offset.x = 0;
	offset.y = 0;
	pt = ((NM_LISTVIEW*)((LPNMHDR)lParam))->ptAction;
	ClientToScreen(hListView, &pt);

	// Ok, now we create a drag-image for all selected items
	bFirst = TRUE;
	iPos = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
	while (iPos != -1) {
		if (bFirst) {
			int width, height;
			RECT rc2;
			GetClientRect(hListView, &rc2);

			// ListView_CreateDragImage replacement hack follows..
			RECT rc;
			// Get Rectangle of selected ListView Item
			ListView_GetItemRect(hListView, iPos, &rc, LVIR_BOUNDS);
			if (rc.left < 0)
				rc.left = 0;
			if (rc.bottom < 0)
				rc.bottom = 0;
			width = rc.right - rc.left;
			height = rc.bottom - rc.top;
			if (width <= 0 || height <= 0)
				return 0;
			// Image becomes blank bar if visible part
			// is smaller than complete width of item.
			if (width > rc2.right - rc2.left)
				width = rc2.right - rc2.left;
			if (height > rc2.bottom - rc2.top)
				height = rc2.bottom - rc2.top;

			// Create HBITMAP of selected ListView Item
			HDC hDC = GetDC(hListView);
			if (hDC) {
				HDC hMemDC = CreateCompatibleDC(hDC);
				if (hMemDC) {

					HBITMAP hBMP = CreateCompatibleBitmap(hDC, width, height);
					if (hBMP) {
						HGDIOBJ o = SelectObject(hMemDC, hBMP);
						BitBlt(hMemDC, 0, 0, width, height, hDC, rc.left, rc.top, SRCCOPY);
						SelectObject(hMemDC, o);
					}

					// Create ImageList, add HBITMAP to ImageList.
					hDragImageList = ImageList_Create(width, height, ILC_COLOR24, 1, 1);
					if (hBMP && hDragImageList) {
						ImageList_Add(hDragImageList, hBMP, NULL);
						DeleteObject(hBMP);
					}
					
					DeleteDC(hMemDC);
				}
				ReleaseDC(hListView, hDC);
			}

			offset.x = rc2.left;
			offset.y = rc2.top;
			ClientToScreen(hListView, &offset);
				
			offset.x = pt.x - offset.x;
			offset.y = height;
			DragHeight = height;

			bFirst = FALSE;

		} else {
#if 0
			IMAGEINFO imf;
			HIMAGELIST hOneImageList, hTempImageList;
			// For the rest selected items,
			// we create a single-line drag image, then
			// append it to the bottom of the complete drag image
			hOneImageList = ListView_CreateDragImage(hListView, iPos, &p);
			hTempImageList = ImageList_Merge(hDragImageList, 0, hOneImageList, 0, 0, iHeight);
			ImageList_Destroy(hDragImageList);
			ImageList_Destroy(hOneImageList);
			hDragImageList = hTempImageList;
			ImageList_GetImageInfo(hDragImageList, 0, &imf);
			iHeight = imf.rcImage.bottom;
#endif
		}
		iPos = ListView_GetNextItem(hListView, iPos, LVNI_SELECTED);
	}

	if (!hDragImageList)
		return 0;

	// Now we can initialize then start the drag action
	ImageList_BeginDrag(hDragImageList, 0, offset.x, offset.y);

	ImageList_DragEnter(NULL, pt.x, pt.y);

	bDragging = TRUE;

	// Don't forget to capture the mouse
	SetCapture (hWnd);

	return 1;
}

static int drag_end (HWND hWnd, HWND hListView, LPARAM lParam, int **draggeditems)
{
	int iPos, cnt;
	LVHITTESTINFO lvhti;
	LVITEM  lvi;

	*draggeditems = NULL;
	if (!bDragging)
		return -1;
	// End the drag-and-drop process
	bDragging = FALSE;
	ImageList_DragLeave(hListView);
	ImageList_EndDrag();
	ImageList_Destroy(hDragImageList);
	ReleaseCapture();

	// Determine the dropped item
	lvhti.pt.x = LOWORD(lParam);
	lvhti.pt.y = HIWORD(lParam);
	lvhti.pt.y -= DragHeight / 2;
	ClientToScreen(hWnd, &lvhti.pt);
	ScreenToClient(hListView, &lvhti.pt);
	ListView_HitTest(hListView, &lvhti);

	// Out of the ListView?
	if (lvhti.iItem == -1)
		return -1;
	// Not in an item?
	if ((lvhti.flags & LVHT_ONITEMLABEL) == 0 &&  (lvhti.flags & LVHT_ONITEMSTATEICON) == 0)
		return -1;
	// Dropped item is selected?
	lvi.iItem = lvhti.iItem;
	lvi.iSubItem = 0;
	lvi.mask = LVIF_STATE;
	lvi.stateMask = LVIS_SELECTED;
	ListView_GetItem(hListView, &lvi);
	if (lvi.state & LVIS_SELECTED)
		return -1;
	// Rearrange the items
	iPos = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
	cnt = 0;
	while (iPos != -1) {
		iPos = ListView_GetNextItem(hListView, iPos, LVNI_SELECTED);
		cnt++;
	}
	if (cnt == 0)
		return -1;
	*draggeditems = xmalloc (int, cnt + 1);
	iPos = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
	cnt = 0;
	while (iPos != -1) {
		(*draggeditems)[cnt++] = iPos;
		iPos = ListView_GetNextItem(hListView, iPos, LVNI_SELECTED);
	}
	(*draggeditems)[cnt] = -1;
	return lvhti.iItem;
}
static int drag_move (HWND hWnd, LPARAM lParam)
{
	POINT p;

	if (!bDragging)
		return 0;
	p.x = LOWORD(lParam);
	p.y = HIWORD(lParam);
	ClientToScreen(hWnd, &p);
	ImageList_DragMove(p.x, p.y);
	return 1;
}

static HWND cachedlist = NULL;

static const TCHAR *memsize_names[] = {
	/* 0 */ _T("none"),
	/* 1 */ _T("64 KB"),
	/* 2 */ _T("128 KB"),
	/* 3 */ _T("256 KB"),
	/* 4 */ _T("512 KB"),
	/* 5 */ _T("1 MB"),
	/* 6 */ _T("2 MB"),
	/* 7 */ _T("4 MB"),
	/* 8 */ _T("8 MB"),
	/* 9 */ _T("16 MB"),
	/* 10*/ _T("32 MB"),
	/* 11*/ _T("64 MB"),
	/* 12*/ _T("128 MB"),
	/* 13*/ _T("256 MB"),
	/* 14*/ _T("512 MB"),
	/* 15*/ _T("1 GB"),
	/* 16*/ _T("1.5MB"),
	/* 17*/ _T("1.8MB"),
	/* 18*/ _T("2 GB"),
	/* 19*/ _T("384 MB"),
	/* 20*/ _T("768 MB"),
	/* 21*/ _T("1.5 GB"),
	/* 22*/ _T("2.5 GB"),
	/* 23*/ _T("3 GB")
};

static const unsigned long memsizes[] = {
	/* 0 */ 0,
	/* 1 */ 0x00010000, /*  64K */
	/* 2 */ 0x00020000, /* 128K */
	/* 3 */ 0x00040000, /* 256K */
	/* 4 */ 0x00080000, /* 512K */
	/* 5 */ 0x00100000, /* 1M */
	/* 6 */ 0x00200000, /* 2M */
	/* 7 */ 0x00400000, /* 4M */
	/* 8 */ 0x00800000, /* 8M */
	/* 9 */ 0x01000000, /* 16M */
	/* 10*/ 0x02000000, /* 32M */
	/* 11*/ 0x04000000, /* 64M */
	/* 12*/ 0x08000000, //128M
	/* 13*/ 0x10000000, //256M
	/* 14*/ 0x20000000, //512M
	/* 15*/ 0x40000000, //1GB
	/* 16*/ 0x00180000, //1.5MB
	/* 17*/ 0x001C0000, //1.8MB
	/* 18*/ 0x80000000, //2GB
	/* 19*/ 0x18000000, //384M
	/* 20*/ 0x30000000, //768M
	/* 21*/ 0x60000000, //1.5GB
	/* 22*/ 0xA8000000, //2.5GB
	/* 23*/ 0xC0000000, //3GB
};

static const int msi_chip[] = { 3, 4, 5, 16, 6, 7, 8 };
static const int msi_bogo[] = { 0, 4, 5, 16, 17 };
static const int msi_fast[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
static const int msi_z3fast[] = { 0, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
static const int msi_z3chip[] = { 0, 9, 10, 11, 12, 13, 19, 14, 20, 15 };
static const int msi_gfx[] = { 0, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
static const int msi_cpuboard[] = { 0, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
static const int msi_mb[] = { 0, 5, 6, 7, 8, 9, 10, 11, 12 };

#define MIN_CHIP_MEM 0
#define MAX_CHIP_MEM 6
#define MIN_FAST_MEM 0
#define MAX_FAST_MEM 8
#define MIN_SLOW_MEM 0
#define MAX_SLOW_MEM 4
#define MIN_Z3_MEM 0
#define MAX_Z3_MEM 11
#define MAX_Z3_CHIPMEM 9
#define MIN_P96_MEM 0
#define MAX_P96_MEM_Z3 ((max_z3fastmem >> 20) < 512 ? 8 : ((max_z3fastmem >> 20) < 1024 ? 9 : ((max_z3fastmem >> 20) < 2048) ? 10 : 11))
#define MAX_P96_MEM_Z2 4
#define MIN_MB_MEM 0
#define MAX_MBL_MEM 7
#define MAX_MBH_MEM 8
#define MIN_CB_MEM 0
#define MAX_CB_MEM_Z2 4
#define MAX_CB_MEM_16M 5
#define MAX_CB_MEM_32M 6
#define MAX_CB_MEM_64M 7
#define MAX_CB_MEM_128M 8
#define MAX_CB_MEM_256M 9

#define MIN_M68K_PRIORITY 1
#define MAX_M68K_PRIORITY 16
#define MIN_CACHE_SIZE 0
#define MAX_CACHE_SIZE 5
#define MIN_REFRESH_RATE 1
#define MAX_REFRESH_RATE 10
#define MIN_SOUND_MEM 0
#define MAX_SOUND_MEM 10

struct romscandata {
	UAEREG *fkey;
	int got;
};

static void abspathtorelative (TCHAR *name)
{
	if (!_tcsncmp (start_path_exe, name, _tcslen (start_path_exe)))
		memmove (name, name + _tcslen (start_path_exe), (_tcslen (name) - _tcslen (start_path_exe) + 1) * sizeof (TCHAR));
}

static int extpri(const TCHAR *p, int size)
{
	const TCHAR *s = _tcsrchr(p, '.');
	if (s == NULL)
		return 80;
	// if archive: lowest priority
	if (!my_existsfile(p))
		return 100;
	int pri = 10;
	// prefer matching size
	struct mystat ms;
	if (my_stat(p, &ms)) {
		if (ms.size == size) {
			pri--;
		}
	}
	return pri;
}

static int addrom (UAEREG *fkey, struct romdata *rd, const TCHAR *name)
{
	TCHAR tmp1[MAX_DPATH], tmp2[MAX_DPATH], tmp3[MAX_DPATH];
	TCHAR pathname[MAX_DPATH];

	_stprintf (tmp1, _T("ROM_%03d"), rd->id);
	if (rd->group) {
		TCHAR *p = tmp1 + _tcslen (tmp1);
		_stprintf (p, _T("_%02d_%02d"), rd->group >> 16, rd->group & 65535);
	}
	getromname (rd, tmp2);
	pathname[0] = 0;
	if (name) {
		_tcscpy (pathname, name);
	}
	if (rd->crc32 == 0xffffffff) {
		if (rd->configname)
			_stprintf (tmp2, _T(":%s"), rd->configname);
		else
			_stprintf (tmp2, _T(":ROM_%03d"), rd->id);
	}

	int size = sizeof tmp3 / sizeof(TCHAR);
	if (regquerystr(fkey, tmp1, tmp3, &size)) {
		TCHAR *s = _tcschr(tmp3, '\"');
		if (s && _tcslen(s) > 1) {
			TCHAR *s2 = s + 1;
			s = _tcschr(s2, '\"');
			if (s)
				*s = 0;
			int pri1 = extpri(s2, rd->size);
			int pri2 = extpri(pathname, rd->size);
			if (pri2 >= pri1)
				return 1;
		}
	}
	fullpath(pathname, sizeof(pathname) / sizeof(TCHAR));
	if (pathname[0]) {
		_tcscat(tmp2, _T(" / \""));
		_tcscat(tmp2, pathname);
		_tcscat(tmp2, _T("\""));
	}
	if (!regsetstr (fkey, tmp1, tmp2))
		return 0;
	return 1;
}

static int isromext (const TCHAR *path, bool deepscan)
{
	const TCHAR *ext;
	int i;

	if (!path)
		return 0;
	ext = _tcsrchr (path, '.');
	if (!ext)
		return 0;
	ext++;

	if (!_tcsicmp (ext, _T("rom")) || !_tcsicmp (ext, _T("bin")) ||  !_tcsicmp (ext, _T("adf")) || !_tcsicmp (ext, _T("key"))
		|| !_tcsicmp (ext, _T("a500")) || !_tcsicmp (ext, _T("a1200")) || !_tcsicmp (ext, _T("a4000")) || !_tcsicmp (ext, _T("cd32")))
		return 1;
	if (_tcslen (ext) >= 2 && toupper(ext[0]) == 'U' && isdigit (ext[1]))
		return 1;
	if (!deepscan)
		return 0;
	for (i = 0; uae_archive_extensions[i]; i++) {
		if (!_tcsicmp (ext, uae_archive_extensions[i]))
			return 1;
	}
	return 0;
}

static INT_PTR CALLBACK InfoBoxDialogProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	bool handled;
	INT_PTR v = commonproc(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return v;
	}

	switch(msg)
	{
	case WM_DESTROY:
		PostQuitMessage (0);
		CustomDialogClose(hDlg, 1);
		return TRUE;
	case WM_CLOSE:
		CustomDialogClose(hDlg, 1);
	return TRUE;
	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDCANCEL:
			CustomDialogClose(hDlg, 1);
			return TRUE;
		}
		break;
	}
	return FALSE;
}
static bool scan_rom_hook (const TCHAR *name, int line)
{
	MSG msg;
	if (cdstate.status)
		return false;
	if (!cdstate.active)
		return true;
	if (name != NULL) {
		const TCHAR *s = NULL;
		if (line == 2) {
			s = _tcsrchr (name, '/');
			if (!s)
				s = _tcsrchr (name, '\\');
			if (s)
				s++;
		}
		SetWindowText (GetDlgItem (cdstate.hwnd, line == 1 ? IDC_INFOBOX_TEXT1 : (line == 2 ? IDC_INFOBOX_TEXT2 : IDC_INFOBOX_TEXT3)), s ? s : name);
	}
	while (PeekMessage (&msg, cdstate.hwnd, 0, 0, PM_REMOVE)) {
		if (!IsDialogMessage (cdstate.hwnd, &msg)) {
			TranslateMessage (&msg);
			DispatchMessage (&msg);
		}
	}
	return cdstate.active;
}

static int scan_rom_2 (struct zfile *f, void *vrsd)
{
	struct romscandata *rsd = (struct romscandata*)vrsd;
	const TCHAR *path = zfile_getname(f);
	const TCHAR *romkey = _T("rom.key");
	struct romdata *rd;

	scan_rom_hook (NULL, 0);
	if (!isromext (path, true))
		return 0;
	rd = scan_single_rom_file(f);
	if (rd) {
		TCHAR name[MAX_DPATH];
		getromname (rd, name);
		scan_rom_hook (name, 3);
		addrom (rsd->fkey, rd, path);
		if (rd->type & ROMTYPE_KEY)
			addkeyfile (path);
		rsd->got = 1;
	} else if (_tcslen (path) > _tcslen (romkey) && !_tcsicmp (path + _tcslen (path) - _tcslen (romkey), romkey)) {
		addkeyfile (path);
	}
	return 0;
}

static int scan_rom (const TCHAR *path, UAEREG *fkey, bool deepscan)
{
	struct romscandata rsd = { fkey, 0 };
	struct romdata *rd;
	int cnt = 0;

	if (!isromext (path, deepscan)) {
		//write_log("ROMSCAN: skipping file '%s', unknown extension\n", path);
		return 0;
	}
	scan_rom_hook (path, 2);
	for (;;) {
		TCHAR tmp[MAX_DPATH];
		_tcscpy (tmp, path);
		rd = scan_arcadia_rom (tmp, cnt++);
		if (rd) {
			if (!addrom (fkey, rd, tmp))
				return 1;
			continue;
		}
		break;
	}
	zfile_zopen (path, scan_rom_2, (void*)&rsd);
	return rsd.got;
}

static int listrom (const int *roms)
{
	int i;

	i = 0;
	while (roms[i] >= 0) {
		struct romdata *rd = getromdatabyid (roms[i]);
		if (rd && romlist_get (rd))
			return 1;
		i++;
	}
	return 0;
}

static void show_rom_list (void)
{
	TCHAR *p;
	TCHAR *p1, *p2;
	const int *rp;
	bool first = true;
	const int romtable[] = {
		5, 4, -1, -1, // A500 1.2
		6, 32, -1, -1, // A500 1.3
		7, -1, -1, // A500+
		8, 9, 10, -1, -1, // A600
		23, 24, -1, -1, // A1000
		11, 31, 15, -1, -1, // A1200
		59, 71, 61, -1, -1, // A3000
		16, 46, 31, 13, 12, -1, -1, // A4000
		17, -1, -1, // A4000T
		18, -1, 19, -1, -1, // CD32
		20, 21, 22, -1, 6, 32, -1, -1, // CDTV
		9, 10, -1, 107, 108, -1, -1, // CDTV-CR
		49, 50, 75, 51, 76, 77, -1, 5, 4, -1, -2, // ARCADIA

		18, -1, 19, -1, 74, 23, -1, -1,  // CD32 FMV

		69, 67, 70, 115, -1, -1, // nordic power
		65, 68, -1, -1, // x-power
		62, 60, -1, -1, // action cartridge
		116, -1, -1, // pro access
		52, 25, -1, -1, // ar 1
		26, 27, 28, -1, -1, // ar 2
		29, 30, -1, -1, // ar 3
		47, -1, -1, // action replay 1200

		0, 0, 0
	};

	p1 = _T("A500 Boot ROM 1.2\0A500 Boot ROM 1.3\0A500+\0A600\0A1000\0A1200\0A3000\0A4000\0A4000T\0")
		_T("CD32\0CDTV\0CDTV-CR\0Arcadia Multi Select\0")
		_T("CD32 Full Motion Video\0")
		_T("Nordic Power\0X-Power Professional 500\0Action Cartridge Super IV Professional\0")
		_T("Pro Access\0")
		_T("Action Replay MK I\0Action Replay MK II\0Action Replay MK III\0")
		_T("Action Replay 1200\0")
		_T("\0");

	p = xmalloc (TCHAR, 100000);
	if (!p)
		return;
	WIN32GUI_LoadUIString (IDS_ROMSCANEND, p, 100);
	_tcscat (p, _T("\n\n"));

	rp = romtable;
	while(rp[0]) {
		int ok = 1;
		p2 = p1 + _tcslen (p1) + 1;
		while (*rp >= 0) {
			if (ok) {
				ok = 0;
				if (listrom (rp))
					ok = 1;
			}
			while(*rp++ >= 0);
		}
		if (ok) {
			if (!first)
				_tcscat (p, _T(", "));
			first = false;
			_tcscat (p, p1);
		}
		if (*rp == -2) {
			_tcscat(p, _T("\n\n"));
			first = true;
		}
		rp++;
		p1 = p2;
	}

	pre_gui_message (p);
	free (p);
}

static int scan_roms_2 (UAEREG *fkey, const TCHAR *path, bool deepscan, int level)
{
	TCHAR buf[MAX_DPATH];
	WIN32_FIND_DATA find_data;
	HANDLE handle;
	int ret;

	if (!path)
		return 0;
	write_log (_T("ROM scan directory '%s'\n"), path);
	_tcscpy (buf, path);
	_tcscat (buf, _T("*.*"));
	ret = 0;
	handle = FindFirstFile (buf, &find_data);
	if (handle == INVALID_HANDLE_VALUE)
		return 0;
	scan_rom_hook (path, 1);
	for (;;) {
		TCHAR tmppath[MAX_DPATH];
		_tcscpy (tmppath, path);
		_tcscat (tmppath, find_data.cFileName);
		if (!(find_data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY |FILE_ATTRIBUTE_SYSTEM)) && find_data.nFileSizeLow < 10000000) {
			if (scan_rom (tmppath, fkey, deepscan))
				ret = 1;
		} else if (deepscan && (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			if (recursiveromscan < 0 || recursiveromscan > level) {
				if (find_data.cFileName[0] != '.') {
					_tcscat(tmppath, _T("\\"));
					scan_roms_2(fkey, tmppath, deepscan, level + 1);
				}
			}
		}
		if (!scan_rom_hook (NULL, 0) || FindNextFile (handle, &find_data) == 0) {
			FindClose (handle);
			break;
		}
	}
	return ret;
}

#define MAX_ROM_PATHS 10

static int scan_roms_3 (UAEREG *fkey, TCHAR **paths, const TCHAR *path)
{
	int i, ret;
	TCHAR pathp[MAX_DPATH];
	bool deepscan = true;

	ret = 0;
	scan_rom_hook (NULL, 0);
	pathp[0] = 0;
	GetFullPathName (path, MAX_DPATH, pathp, NULL);
	if (!pathp[0])
		return ret;
	if (_tcsicmp (pathp, start_path_exe) == 0)
		deepscan = false; // do not scan root dir archives
	for (i = 0; i < MAX_ROM_PATHS; i++) {
		if (paths[i] && !_tcsicmp (paths[i], pathp))
			return ret;
	}
	ret = scan_roms_2 (fkey, pathp, deepscan, 0);
	for (i = 0; i < MAX_ROM_PATHS; i++) {
		if (!paths[i]) {
			paths[i] = my_strdup(pathp);
			break;
		}
	}
	return ret;
}

extern int get_rom_path (TCHAR *out, pathtype mode);

int scan_roms (HWND hDlg, int show)
{
	TCHAR path[MAX_DPATH];
	static int recursive;
	int id, i, ret, keys, cnt;
	UAEREG *fkey, *fkey2;
	TCHAR *paths[MAX_ROM_PATHS];
	MSG msg;
	HWND hwnd = NULL;

	if (recursive)
		return 0;
	recursive++;

	ret = 0;

	SAVECDS;

	regdeletetree (NULL, _T("DetectedROMs"));
	fkey = regcreatetree (NULL, _T("DetectedROMs"));
	if (fkey == NULL)
		goto end;

	InitializeDarkMode();

	if (!rp_isactive ()) {
		hwnd = CustomCreateDialog(IDD_INFOBOX, hDlg, InfoBoxDialogProc, &cdstate);
		if (!hwnd)
			goto end;
	}

	cnt = 0;
	for (i = 0; i < MAX_ROM_PATHS; i++)
		paths[i] = NULL;
	scan_rom_hook (NULL, 0);
	while (scan_rom_hook (NULL, 0)) {
		keys = get_keyring ();
		fetch_path (_T("KickstartPath"), path, sizeof path / sizeof (TCHAR));
		cnt += scan_roms_3 (fkey, paths, path);
		if (1) {
			static pathtype pt[] = { PATH_TYPE_DEFAULT, PATH_TYPE_WINUAE, PATH_TYPE_NEWWINUAE, PATH_TYPE_NEWAF, PATH_TYPE_AMIGAFOREVERDATA, PATH_TYPE_END };
			for (i = 0; pt[i] != PATH_TYPE_END; i++) {
				ret = get_rom_path (path, pt[i]);
				if (ret < 0)
					break;
				cnt += scan_roms_3 (fkey, paths, path);
			}
			if (get_keyring() > keys) { /* more keys detected in previous scan? */
				write_log (_T("ROM scan: more keys found, restarting..\n"));
				for (i = 0; i < MAX_ROM_PATHS; i++) {
					xfree (paths[i]);
					paths[i] = NULL;
				}
				continue;
			}
		}
		break;
	}
	if (cnt == 0)
		scan_roms_3 (fkey, paths, workprefs.path_rom.path[0]);

	for (i = 0; i < MAX_ROM_PATHS; i++)
		xfree (paths[i]);

	fkey2 = regcreatetree (NULL, _T("DetectedROMS"));
	if (fkey2) {
		id = 1;
		for (;;) {
			struct romdata *rd = getromdatabyid (id);
			if (!rd)
				break;
			if (rd->crc32 == 0xffffffff)
				addrom (fkey, rd, NULL);
			id++;
		}
		regclosetree (fkey2);
	}

end:
	if (hwnd) {
		DestroyWindow (hwnd);
		while (PeekMessage (&msg, 0, 0, 0, PM_REMOVE)) {
			TranslateMessage (&msg);
			DispatchMessage (&msg);
		}
	}

	RESTORECDS;

	read_rom_list(false);
	if (show)
		show_rom_list ();

	regclosetree (fkey);
	recursive--;
	return ret;
}

static void box_art_check(struct uae_prefs *p, const TCHAR *config)
{
	TCHAR tmp1[MAX_DPATH];
	if (cfgfile_detect_art(p, tmp1)) {
		show_box_art(tmp1, config);
	} else {
		show_box_art(NULL, NULL);
	}
}

struct ConfigStruct {
	TCHAR Name[MAX_PATH];
	TCHAR Path[MAX_DPATH];
	TCHAR Fullpath[MAX_DPATH];
	TCHAR HostLink[MAX_DPATH];
	TCHAR HardwareLink[MAX_DPATH];
	TCHAR Description[CFG_DESCRIPTION_LENGTH];
	TCHAR Artpath[MAX_DPATH];
	TCHAR Category[CFG_DESCRIPTION_LENGTH];
	TCHAR Tags[CFG_DESCRIPTION_LENGTH];
	int Type, Directory;
	struct ConfigStruct *Parent, *Child;
	int host, hardware;
	bool expanded;
	HTREEITEM item;
	FILETIME t;
};
struct CategoryStruct
{
	TCHAR category[CFG_DESCRIPTION_LENGTH];
};

static const TCHAR *configreg[] = { _T("ConfigFile"), _T("ConfigFileHardware"), _T("ConfigFileHost") };
static const TCHAR *configregfolder[] = { _T("ConfigFileFolder"), _T("ConfigFileHardwareFolder"), _T("ConfigFileHostFolder") };
static const TCHAR *configregsearch[] = { _T("ConfigFileSearch"), _T("ConfigFileHardwareSearch"), _T("ConfigFileHostSearch") };
static const TCHAR *configreg2[] = { _T(""), _T("ConfigFileHardware_Auto"), _T("ConfigFileHost_Auto") };
static struct ConfigStruct **configstore;
static int configstoresize, configstoreallocated, configtype, configtypepanel;
static struct CategoryStruct **categorystore;
static int categorystoresize, categorystoreallocated;

static struct ConfigStruct *getconfigstorefrompath (TCHAR *path, TCHAR *out, int type)
{
	int i;
	for (i = 0; i < configstoresize; i++) {
		if (((configstore[i]->Type == 0 || configstore[i]->Type == 3) && type == 0) || (configstore[i]->Type == type)) {
			TCHAR path2[MAX_DPATH];
			_tcscpy (path2, configstore[i]->Path);
			_tcsncat (path2, configstore[i]->Name, MAX_DPATH - _tcslen(path2));
			if (!_tcscmp (path, path2)) {
				_tcscpy (out, configstore[i]->Fullpath);
				_tcsncat (out, configstore[i]->Name, MAX_DPATH - _tcslen(out));
				return configstore[i];
			}
		}
	}
	return 0;
}

void target_multipath_modified(struct uae_prefs *p)
{
	if (p != &workprefs)
		return;
	memcpy(&currprefs.path_hardfile, &p->path_hardfile, sizeof(struct multipath));
	memcpy(&currprefs.path_floppy, &p->path_floppy, sizeof(struct multipath));
	memcpy(&currprefs.path_cd, &p->path_cd, sizeof(struct multipath));
	memcpy(&currprefs.path_rom, &p->path_rom, sizeof(struct multipath));
}

static bool cfgfile_can_write(HWND hDlg, const TCHAR *path)
{
	for (;;) {
		int v = my_readonlyfile(path);
		if (v <= 0)
			return true;
		TCHAR szMessage[MAX_DPATH], msg[MAX_DPATH], szTitle[MAX_DPATH];
		WIN32GUI_LoadUIString(IDS_READONLYCONFIRMATION, szMessage, MAX_DPATH);
		_stprintf(msg, szMessage, path);
		WIN32GUI_LoadUIString(IDS_ERRORTITLE, szTitle, MAX_DPATH);
		if (MessageBox(hDlg, msg, szTitle, MB_YESNO | MB_ICONWARNING | MB_APPLMODAL | MB_SETFOREGROUND) == IDYES) {
			DWORD flags = GetFileAttributesSafe(path);
			if (!(flags & FILE_ATTRIBUTE_READONLY)) {
				return true;
			}
			flags &= ~FILE_ATTRIBUTE_READONLY;
			SetFileAttributesSafe(path, flags);
			continue;
		}
		break;

	}
	return false;
}

int target_cfgfile_load (struct uae_prefs *p, const TCHAR *filename, int type, int isdefault)
{
	int v, i, type2;
	int ct, ct2, size;
	TCHAR tmp1[MAX_DPATH], tmp2[MAX_DPATH];
	TCHAR fname[MAX_DPATH], cname[MAX_DPATH];

	if (isdefault) {
		path_statefile[0] = 0;
	}
	error_log(NULL);
	_tcscpy (fname, filename);
	cname[0] = 0;
	if (!zfile_exists (fname)) {
		fetch_configurationpath (fname, sizeof (fname) / sizeof (TCHAR));
		if (_tcsncmp (fname, filename, _tcslen (fname)))
			_tcscat (fname, filename);
		else
			_tcscpy (fname, filename);
	}
	target_setdefaultstatefilename(filename);

	if (!isdefault)
		qs_override = 1;
	if (type < 0) {
		type = 0;
		cfgfile_get_description(NULL, fname, NULL, NULL, NULL, NULL, NULL, &type);
		if (!isdefault) {
			const TCHAR *p = _tcsrchr(fname, '\\');
			if (!p)
				p = _tcsrchr(fname, '/');
			if (p)
				_tcscpy(cname, p + 1);
		}
	}
	if (type == 0 || type == 1) {
		discard_prefs (p, 0);
	}
	type2 = type;
	if (type == 0 || type == 3) {
		default_prefs (p, true, type);
		write_log(_T("config reset\n"));
#if 0
		if (isdefault == 0) {
			fetch_configurationpath (tmp1, sizeof (tmp1) / sizeof (TCHAR));
			_tcscat (tmp1, OPTIONSFILENAME);
			cfgfile_load (p, tmp1, NULL, 0, 0);
		}
#endif
	}
	ct2 = 0;
	regqueryint (NULL, _T("ConfigFile_NoAuto"), &ct2);
	v = cfgfile_load (p, fname, &type2, ct2, isdefault ? 0 : 1);
	if (!v)
		return v;
	if (type > 0)
		return v;
	if (cname[0])
		_tcscpy(config_filename, cname);
	box_art_check(p, fname);
	for (i = 1; i <= 2; i++) {
		if (type != i) {
			size = sizeof (ct);
			ct = 0;
			regqueryint (NULL, configreg2[i], &ct);
			if (ct && ((i == 1 && p->config_hardware_path[0] == 0) || (i == 2 && p->config_host_path[0] == 0) || ct2)) {
				size = sizeof (tmp1) / sizeof (TCHAR);
				regquerystr (NULL, configreg[i], tmp1, &size);
				fetch_path (_T("ConfigurationPath"), tmp2, sizeof (tmp2) / sizeof (TCHAR));
				_tcscat (tmp2, tmp1);
				v = i;
				cfgfile_load (p, tmp2, &v, 1, 0);
			}
		}
	}
	cfgfile_get_shader_config(p, 0);
	v = 1;
	return v;
}

static int gui_width, gui_height;
int gui_fullscreen, gui_darkmode;
static RECT gui_fullscreen_rect;
static bool gui_resize_enabled = true;
static bool gui_resize_allowed = true;

// Internal panel max size: 396, 318

static int mm = 0;
static void m(int monid)
{
	struct monconfig *gmw = &workprefs.gfx_monitor[monid];
	struct monconfig *gmc = &currprefs.gfx_monitor[monid];
	struct monconfig *gmh = &changed_prefs.gfx_monitor[monid];
	write_log (_T("%d:0: %dx%d %dx%d %dx%d\n"), mm, gmc->gfx_size.width, gmc->gfx_size.height,
		gmw->gfx_size.width, gmw->gfx_size.height, gmh->gfx_size.width, gmh->gfx_size.height);
	write_log (_T("%d:1: %dx%d %dx%d %dx%d\n"), mm, gmc->gfx_size_fs.width, gmc->gfx_size_fs.height,
		gmw->gfx_size_fs.width, gmw->gfx_size_fs.height, gmh->gfx_size_fs.width, gmh->gfx_size_fs.height);
	mm++;
}

static void flipgui(int opengui)
{
	end_draw_denise();
	D3D_guimode(0, opengui);
	if (full_property_sheet)
		return;
	if (opengui) {
		;
	} else {
		if (quit_program)
			return;
		full_redraw_all();
	}
}

static bool get_avioutput_file(HWND);
static int GetSettings (int all_options, HWND hwnd);
/* if drive is -1, show the full GUI, otherwise file-requester for DF[drive] */
void gui_display (int shortcut)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	struct picasso96_state_struct *state = &picasso96_state[0];
	struct monconfig *gm = &currprefs.gfx_monitor[0];
	static int here;
	int w, h;

	if (here)
		return;
	here++;
	gui_active++;

	if (isfullscreen() > 0 && currprefs.gfx_api != 1)
		screenshot_prepare(getfocusedmonitor());
	flipgui(1);

	if (setpaused (7)) {
		inputdevice_unacquire();
		rawinput_release();
		wait_keyrelease();
		clearallkeys();
		setmouseactive(0, 0);
	}

	w = h = -1;
	if (!WIN32GFX_IsPicassoScreen(mon) && currprefs.gfx_apmode[0].gfx_fullscreen && (gm->gfx_size.width < gui_width || gm->gfx_size.height < gui_height)) {
		w = gm->gfx_size.width;
		h = gm->gfx_size.height;
	}
	if (WIN32GFX_IsPicassoScreen(mon) && currprefs.gfx_apmode[1].gfx_fullscreen && (state->Width < gui_width || state->Height < gui_height)) {
		w = gm->gfx_size.width;
		h = gm->gfx_size.height;
	}
	mon->manual_painting_needed++; /* So that WM_PAINT will refresh the display */

	flush_log ();

	if (shortcut == -1) {
		int ret;
		ret = GetSettings(0, mon->hAmigaWnd);
		if (!ret) {
			savestate_state = 0;
		}
	} else if (shortcut >= 0 && shortcut < 4) {
		DiskSelection(mon->hAmigaWnd, IDC_DF0 + shortcut, 0, &changed_prefs, NULL, NULL);
	} else if (shortcut == 5) {
		if (DiskSelection(mon->hAmigaWnd, IDC_DOSAVESTATE, 9, &changed_prefs, NULL, NULL))
			save_state (savestate_fname, _T("Description!"));
	} else if (shortcut == 4) {
		if (DiskSelection(mon->hAmigaWnd, IDC_DOLOADSTATE, 10, &changed_prefs, NULL, NULL))
			savestate_state = STATE_DORESTORE;
	} else if (shortcut == 6) {
		DiskSelection(mon->hAmigaWnd, IDC_CD_SELECT, 17, &changed_prefs, NULL, NULL);
	} else if (shortcut == 7) {
		if (get_avioutput_file(mon->hAmigaWnd)) {
			_tcscpy(avioutput_filename_inuse, avioutput_filename_gui);
			AVIOutput_SetSettings();
			if (!avioutput_requested) {
				AVIOutput_Toggle(true, false);
				if (avioutput_audio != AVIAUDIO_WAV) {
					TCHAR tmp[MAX_DPATH];
					avioutput_audio = AVIOutput_GetAudioCodec(tmp, sizeof tmp / sizeof(TCHAR));
					avioutput_video = AVIOutput_GetVideoCodec(tmp, sizeof tmp / sizeof(TCHAR));
				} 
			} else {
				AVIOutput_Restart(false);
			}
		}
	}
	mon->manual_painting_needed--; /* So that WM_PAINT doesn't need to use custom refreshing */
	reset_sound();
	inputdevice_copyconfig(&changed_prefs, &currprefs);
	inputdevice_config_change_test();
	clearallkeys();
	flipgui(0);
	if (resumepaused (7)) {
		inputdevice_acquire(TRUE);
		setmouseactive(0, 1);
	}
	rawinput_alloc();
	fpscounter_reset();
	screenshot_free();
	write_disk_history();
	gui_active--;
	here--;
}

static void prefs_to_gui(struct uae_prefs *p)
{
	int st = savestate_state;
	default_prefs(&workprefs, false, 0);
	copy_prefs(p, &workprefs);
	/* filesys hack */
	workprefs.mountitems = currprefs.mountitems;
	memcpy (&workprefs.mountconfig, &currprefs.mountconfig, MOUNT_CONFIG_SIZE * sizeof (struct uaedev_config_info));
	updatewinfsmode(0, &workprefs);
	if (workprefs.statefile[0])
		savestate_state = st;
}

static void gui_to_prefs(void)
{
	// Always copy our prefs to changed_prefs
	copy_prefs(&workprefs, &changed_prefs);
	if (quit_program == -UAE_RESET_HARD) {
		// copy all if hard reset
		copy_prefs(&workprefs, &currprefs);
		memory_hardreset(2);
	}
	// filesys hack
	currprefs.mountitems = changed_prefs.mountitems;
	memcpy (&currprefs.mountconfig, &changed_prefs.mountconfig, MOUNT_CONFIG_SIZE * sizeof (struct uaedev_config_info));
	fixup_prefs (&changed_prefs, true);
	updatewinfsmode(0, &changed_prefs);
}

static int iscd (int n)
{
	if (quickstart_cd && n == 1 && currentpage == QUICKSTART_ID)
		return 1;
	return 0;
}

static const GUID diskselectionguids[] = {
	{ 0x4fa8fa15, 0xc209, 0x4112, { 0x94, 0x7b, 0xc6, 0x00, 0x8e, 0x1f, 0xa3, 0x29 } },
	{ 0x32073f09, 0x752d, 0x4783, { 0x84, 0x6c, 0xaa, 0x66, 0x48, 0x84, 0x14, 0x45 } },
	{ 0x8047f7ea, 0x8a42, 0x4695, { 0x94, 0x52, 0xf5, 0x0d, 0xb8, 0x43, 0x00, 0x58 } },
	{ 0x2412c4e7, 0xf608, 0x4333, { 0x83, 0xd2, 0xa1, 0x2f, 0xdf, 0x66, 0xac, 0xe5 } },
	{ 0xe3741dff, 0x11f2, 0x445f, { 0x94, 0xb0, 0xa3, 0xe7, 0x58, 0xe2, 0xcb, 0xb5 } },
	{ 0x2056d641, 0xba13, 0x4312, { 0xaa, 0x75, 0xc5, 0xeb, 0x52, 0xa8, 0x1c, 0xe3 } },
	{ 0x05aa5db2, 0x470b, 0x4725, { 0x96, 0x03, 0xee, 0x61, 0x30, 0xfc, 0x54, 0x99 } },
	{ 0x68366188, 0xa6d4, 0x4278, { 0xb7, 0x55, 0x6a, 0xb8, 0x17, 0xa6, 0x71, 0xd9 } },
	{ 0xe990bee1, 0xd7cc, 0x4768, { 0xaf, 0x34, 0xef, 0x39, 0x87, 0x48, 0x09, 0x50 } },
	{ 0x12c53317, 0xd99c, 0x4494, { 0x8d, 0x81, 0x00, 0x6d, 0x8c, 0x62, 0x7d, 0x83 } },
	{ 0x406859ac, 0x5283, 0x4f7e, { 0xb7, 0xee, 0x0c, 0x2b, 0x78, 0x3d, 0x1d, 0xcc } }
};

static void getfilter (int num, const TCHAR *name, int *filter, TCHAR *fname)
{
	_tcscpy (fname, name);
	_tcscat (fname, _T("_Filter"));
	regqueryint (NULL, fname, &filter[num]);
}
static void setfilter (int num, int *filter, const TCHAR *fname)
{
	if (fname == NULL || fname[0] == 0)
		return;
	regsetint (NULL, fname, filter[num]);
}

static UINT_PTR CALLBACK ofnhook (HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	HWND hWnd;
	RECT windowRect;
	int width, height, w2, h2, x, y;
	struct MultiDisplay *md;
	NMHDR *nmhdr;

	if (message == WM_NOTIFY) {
		nmhdr = (LPNMHDR)lParam;
		if (nmhdr->code == CDN_INITDONE) {
			write_log (_T("OFNHOOK CDN_INITDONE\n"));
			PostMessage (hDlg, WM_USER + 1, 0, 0);
			// OFN_ENABLESIZING enabled: SetWindowPos() only works once here...
		}
		return FALSE;
	} else if (message != WM_USER + 1) {
		return FALSE;
	}
	write_log (_T("OFNHOOK POST\n"));
	hWnd = GetParent (hDlg);
	md = getdisplay(&currprefs, mon->monitor_id);
	if (!md)
		return FALSE;
	w2 = WIN32GFX_GetWidth(mon);
	h2 = WIN32GFX_GetHeight(mon);
	write_log (_T("MOVEWINDOW %dx%d %dx%d (%dx%d)\n"), md->rect.left, md->rect.top, md->rect.right, md->rect.bottom, w2, h2);
	windowRect.left = windowRect.right = windowRect.top = windowRect.bottom = -1;
	GetWindowRect (hWnd, &windowRect);
	width = windowRect.right - windowRect.left;
	height = windowRect.bottom - windowRect.top;
	write_log (_T("%dx%d %dx%d\n"), windowRect.left, windowRect.top, windowRect.right, windowRect.bottom);
	if (width < 800)
		width = 800;
	if (height < 600)
		height = 600;
	if (width > w2)
		width = w2;
	if (height > h2)
		height = h2;
	x = md->rect.left + (w2 - width) / 2;
	y = md->rect.top  + (h2 - height) / 2;
	write_log (_T("X=%d Y=%d W=%d H=%d\n"), x, y, width, height);
	SetWindowPos (hWnd, NULL, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
	return FALSE;
}

static void eject_cd (void)
{
	workprefs.cdslots[0].name[0] = 0;
	if (full_property_sheet)
		workprefs.cdslots[0].type = SCSI_UNIT_DEFAULT;
	quickstart_cddrive[0] = 0;
	workprefs.cdslots[0].inuse = false;
	if (full_property_sheet) {
		quickstart_cdtype = 0;
	} else {
		if (quickstart_cdtype > 0) {
			quickstart_cdtype = 1;
			workprefs.cdslots[0].inuse = true;
		}
	}
}

void gui_infotextbox(HWND hDlg, const TCHAR *text)
{
	SAVECDS;
	HWND hwnd = CustomCreateDialog(IDD_DISKINFO, hDlg ? hDlg : hGUIWnd, StringBoxDialogProc, &cdstate);
	if (hwnd != NULL) {
		HFONT font = CreateFont (getscaledfontsize(-1, hDlg), 0, 0, 0, 0, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("Lucida Console"));
		if (font)
			SendMessage (GetDlgItem (hwnd, IDC_DISKINFOBOX), WM_SETFONT, WPARAM(font), FALSE);
		SendMessage (GetDlgItem (hwnd, IDC_DISKINFOBOX), WM_SETTEXT, 0, (LPARAM)text);
		while (cdstate.active) {
			MSG msg;
			int ret;
			WaitMessage();
			while ((ret = GetMessage(&msg, NULL, 0, 0))) {
				if (ret == -1)
					break;
				if (!IsWindow(hwnd) || !IsDialogMessage(hwnd, &msg)) {
					TranslateMessage(&msg);
					DispatchMessage(&msg);
				}
			}
		}
		if (font) {
			DeleteObject(font);
		}
	}
	RESTORECDS;
}

static void infofloppy (HWND hDlg, int n)
{
	struct diskinfo di;
	TCHAR tmp2[MAX_DPATH], tmp1[MAX_DPATH], tmp3[MAX_DPATH];
	TCHAR text[20000];

	DISK_examine_image (&workprefs, n, &di, true, tmp3);
	DISK_validate_filename(&workprefs, workprefs.floppyslots[n].df, n, tmp1, 0, NULL, NULL, NULL);

	text[0] = 0;
	if (tmp3[0]) {
		_tcscpy(text, tmp3);
		_tcscat(text, _T("\r\n\r\n"));
	}
	tmp2[0] = 0;
	if (tmp1[0]) {
		_stprintf(tmp2, _T("'%s'\r\n"), tmp1);
	}
	_stprintf (tmp2 + _tcslen(tmp2), _T("Disk readable: %s\r\n"), di.unreadable ? _T("No") : _T("Yes"));
	if (di.image_crc_value) {
		_stprintf(tmp2 + _tcslen(tmp2), _T("Disk CRC32: %08X\r\n"), di.imagecrc32);
	}
	_stprintf(tmp2 + _tcslen(tmp2),
		_T("Boot block CRC32: %08X\r\nBoot block checksum valid: %s\r\nBoot block type: %s\r\n"),
		di.bootblockcrc32,
		di.bb_crc_valid ? _T("Yes") : _T("No"),
		di.bootblocktype == 0 ? _T("Custom") : (di.bootblocktype == 1 ? _T("Standard 1.x") : _T("Standard 2.x+"))
	);

	_tcscat(text, tmp2);
	if (di.diskname[0]) {
		_stprintf (tmp2,
			_T("Label: '%s'\r\n"), di.diskname);
		_tcscat (text, tmp2);
	}
	_tcscat (text, _T("\r\n"));
	if (di.bootblockinfo[0]) {
		_tcscat(text, _T("Amiga Bootblock Reader database detected:\r\n"));
		_tcscat(text, _T("Name: '"));
		_tcscat(text, di.bootblockinfo);
		_tcscat(text, _T("'"));
		_tcscat(text, _T("\r\n"));
		if (di.bootblockclass[0]) {
			_tcscat(text, _T("Class: '"));
			_tcscat(text, di.bootblockclass);
			_tcscat(text, _T("'"));
			_tcscat(text, _T("\r\n"));
		}
		_tcscat(text, _T("\r\n"));
	}

	int w = 32;
	for (int i = 0; i < 1024; i += w) {
		for (int j = 0; j < w; j++) {
			uae_u8 b = di.bootblock[i + j];
			_stprintf (tmp2 + j * 2, _T("%02X"), b);
			if (b >= 32 && b < 127)
				tmp2[w * 2 + 1 + j] = (TCHAR)b;
			else
				tmp2[w * 2 + 1 + j] = '.';
		}
		tmp2[w * 2] = ' ';
		tmp2[w * 2 + 1 + w] = 0;
		_tcscat (text, tmp2);
		_tcscat (text, _T("\r\n"));
	}

	SAVECDS;
	HWND hwnd = CustomCreateDialog(IDD_DISKINFO, hDlg, StringBoxDialogProc, &cdstate);
	if (hwnd != NULL) {
		HFONT font = CreateFont (getscaledfontsize(-1, hDlg), 0, 0, 0, 0, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, _T("Lucida Console"));
		if (font)
			SendMessage (GetDlgItem (hwnd, IDC_DISKINFOBOX), WM_SETFONT, WPARAM(font), FALSE);
		SendMessage (GetDlgItem (hwnd, IDC_DISKINFOBOX), WM_SETTEXT, 0, (LPARAM)text);
		while (cdstate.active) {
			MSG msg;
			int ret;
			WaitMessage ();
			while ((ret = GetMessage (&msg, NULL, 0, 0))) {
				if (ret == -1)
					break;
				if (!IsWindow (hwnd) || !IsDialogMessage (hwnd, &msg)) {
					TranslateMessage (&msg);
					DispatchMessage (&msg);
				}
			}
		}
		if (font) {
			DeleteObject(font);
		}
	}
	RESTORECDS;
}

static void ejectfloppy (int n)
{
	if (iscd (n)) {
		eject_cd ();
	} else {
		workprefs.floppyslots[n].df[0] = 0;
		// no disk in drive when GUI was entered
		// make sure possibly disks inserted after GUI was entered
		// are removed.
		if (changed_prefs.floppyslots[n].df[0] == 0) {
			disk_insert(n, _T(""));
		} else {
			disk_eject(n);
		}
	}
}

static void selectcd (struct uae_prefs *prefs, HWND hDlg, int num, int id, const TCHAR *full_path)
{
	SetDlgItemText (hDlg, id, full_path);
	if (quickstart_cddrive[0])
		eject_cd ();
	_tcscpy (prefs->cdslots[0].name, full_path);
	fullpath (prefs->cdslots[0].name, sizeof prefs->cdslots[0].name / sizeof (TCHAR));
	DISK_history_add (prefs->cdslots[0].name, -1, HISTORY_CD, 0);
}

static void selectdisk (struct uae_prefs *prefs, HWND hDlg, int num, int id, const TCHAR *full_path)
{
	if (iscd (num)) {
		selectcd (prefs, hDlg, num, id, full_path);
		return;
	}
	SetDlgItemText (hDlg, id, full_path);
	_tcscpy(prefs->floppyslots[num].df, full_path);
	fullpath (prefs->floppyslots[num].df, sizeof prefs->floppyslots[num].df / sizeof (TCHAR));
	DISK_history_add (prefs->floppyslots[num].df, -1, HISTORY_FLOPPY, 0);
}

static void selectgenlock(struct uae_prefs *prefs, HWND hDlg, int id, const TCHAR *full_path)
{
	SetDlgItemText(hDlg, id, full_path);
	if (workprefs.genlock_image == 3) {
		_tcscpy(prefs->genlock_image_file, full_path);
		fullpath(prefs->genlock_image_file, sizeof prefs->genlock_image_file / sizeof(TCHAR));
		DISK_history_add(prefs->genlock_image_file, -1, HISTORY_GENLOCK_IMAGE, 0);
	} else if (workprefs.genlock_image == 4 || workprefs.genlock_image >= 6) {
		_tcscpy(prefs->genlock_video_file, full_path);
		fullpath(prefs->genlock_video_file, sizeof prefs->genlock_video_file / sizeof(TCHAR));
		DISK_history_add(prefs->genlock_video_file, -1, HISTORY_GENLOCK_VIDEO, 0);
	}
}

static void getcreatefloppytype(HWND hDlg, drive_type *atype, int *hd)
{
	*atype = DRV_NONE;
	*hd = -1;
	int type = xSendDlgItemMessage(hDlg, IDC_FLOPPYTYPE, CB_GETCURSEL, 0, 0L);
	switch (type)
	{
	case 0:
		*atype = DRV_35_DD;
		break;
	case 1:
		*atype = DRV_35_HD;
		break;
	case 2:
		*atype = DRV_PC_35_ONLY_80;
		break;
	case 3:
		*atype = DRV_PC_35_ONLY_80;
		*hd = 1;
		break;
	case 4:
		*atype = DRV_PC_525_ONLY_40;
		break;
	}
}

static void setdpath (const TCHAR *name, const TCHAR *path)
{
	TCHAR tmp[MAX_DPATH];
	_tcscpy (tmp, path);
	fullpath (tmp, sizeof tmp / sizeof (TCHAR));
	regsetstr (NULL, name, tmp);
}

// Common routine for popping up a file-requester
// flag - 0 for floppy loading, 1 for floppy creation, 2 for loading hdf, 3 for saving hdf
// flag - 4 for loading .uae config-files, 5 for saving .uae config-files
// flag = 6 for loading .rom files, 7 for loading .key files
// flag = 8 for loading configurations
// flag = 9 for saving snapshots
// flag = 10 for loading snapshots
// flag = 11 for selecting flash files
// flag = 12 for loading anything
// flag = 13 for selecting path
// flag = 14 for loading filesystem
// flag = 15 for loading input
// flag = 16 for recording input
// flag = 17 for CD image
// flag = 18 for Tape image
// flag = 20 for genlock image
// flag = 21 for genlock video
// flag = 22 for floppy replacement (missing statefile)
// fags = 23 for hdf geometry (load)
// fags = 24 for hdf geometry (save)

int DiskSelection_2 (HWND hDlg, WPARAM wParam, int flag, struct uae_prefs *prefs, TCHAR *infilename, TCHAR *path_out, int *multi)
{
	static int previousfilter[32];
	TCHAR filtername[MAX_DPATH] = _T("");
	OPENFILENAME openFileName;
	TCHAR full_path[MAX_DPATH] = _T("");
	TCHAR full_path2[MAX_DPATH];
	TCHAR file_name[MAX_DPATH] = _T("");
	TCHAR init_path[MAX_DPATH] = _T("");
	BOOL result = FALSE;
	TCHAR* amiga_path = NULL, * initialdir = NULL;
	const TCHAR *defext = NULL;
	TCHAR *p, *nextp;
	int all = 1;
	int next;
	int nosavepath = 0;
	const GUID *guid = NULL;

	TCHAR szTitle[MAX_DPATH] = { 0 };
	TCHAR szFormat[MAX_DPATH];
	TCHAR szFilter[MAX_DPATH] = { 0 };

	memset (&openFileName, 0, sizeof (OPENFILENAME));

	if (path_out && path_out[0]) {
		_tcscpy (init_path, path_out);
		nosavepath = 1;
	} else {
		_tcsncpy (init_path, start_path_data, MAX_DPATH);
		switch (flag)
		{
		case 0:
		case 1:
		case 22:
			getfilter (flag, _T("FloppyPath"), previousfilter, filtername);
			fetch_path (_T("FloppyPath"), init_path, sizeof (init_path) / sizeof (TCHAR));
			guid = &diskselectionguids[0];
			break;
		case 2:
		case 3:
		case 23:
		case 24:
			getfilter (flag, _T("hdfPath"), previousfilter, filtername);
			fetch_path (_T("hdfPath"), init_path, sizeof (init_path) / sizeof (TCHAR));
			guid = &diskselectionguids[1];
			break;
		case 6:
		case 7:
			getfilter (flag, _T("KickstartPath"), previousfilter, filtername);
			fetch_path (_T("KickstartPath"), init_path, sizeof (init_path) / sizeof (TCHAR));
			guid = &diskselectionguids[2];
			break;
		case 4:
		case 5:
		case 8:
			getfilter (flag, _T("ConfigurationPath"), previousfilter, filtername);
			fetch_path (_T("ConfigurationPath"), init_path, sizeof (init_path) / sizeof (TCHAR));
			guid = &diskselectionguids[3];
			break;
		case 9:
		case 10:
			{
				int ok = 0;
				if (savestate_fname[0]) {
					_tcscpy (init_path, savestate_fname);
					for (;;) {
						TCHAR *p;
						if (my_existsdir (init_path)) {
							ok = 1;
							break;
						}
						p = _tcsrchr (init_path, '\\');
						if (!p)
							p = _tcsrchr (init_path, '/');
						if (!p)
							break;
						*p = 0;
					}
				}
				if (!ok) {
					getfilter (flag, _T("StatefilePath"), previousfilter, filtername);
					fetch_path (_T("StatefilePath"), init_path, sizeof (init_path) / sizeof (TCHAR));
				}
				guid = &diskselectionguids[4];
			}
			break;
		case 11:
		case 19:
			getfilter(flag, _T("NVRAMPath"), previousfilter, filtername);
			fetch_path(_T("NVRAMPath"), init_path, sizeof(init_path) / sizeof(TCHAR));
			guid = &diskselectionguids[10];
			break;
		case 15:
		case 16:
			getfilter (flag, _T("InputPath"), previousfilter, filtername);
			fetch_path (_T("InputPath"), init_path, sizeof (init_path) / sizeof (TCHAR));
			guid = &diskselectionguids[5];
			break;
		case 17:
			getfilter (flag, _T("CDPath"), previousfilter, filtername);
			fetch_path (_T("CDPath"), init_path, sizeof (init_path) / sizeof (TCHAR));
			guid = &diskselectionguids[6];
			break;
		case 18:
			getfilter (flag, _T("TapePath"), previousfilter, filtername);
			fetch_path (_T("TapePath"), init_path, sizeof (init_path) / sizeof (TCHAR));
			guid = &diskselectionguids[7];
			break;
		case 20:
			fetch_path(_T("GenlockImagePath"), init_path, sizeof(init_path) / sizeof(TCHAR));
			guid = &diskselectionguids[8];
			break;
		case 21:
			fetch_path(_T("GenlockVideoPath"), init_path, sizeof(init_path) / sizeof(TCHAR));
			guid = &diskselectionguids[9];
			break;
		}
	}
	if (infilename)
		_tcscpy(full_path, infilename);

	szFilter[0] = 0;
	szFilter[1] = 0;
	switch (flag) {
	case 0:
		WIN32GUI_LoadUIString (IDS_SELECTADF, szTitle, MAX_DPATH);
		WIN32GUI_LoadUIString (IDS_ADF, szFormat, MAX_DPATH);
		_stprintf (szFilter, _T("%s "), szFormat);
		memcpy (szFilter + _tcslen (szFilter), DISK_FORMAT_STRING, sizeof (DISK_FORMAT_STRING));
		defext = _T("adf");
		break;
	case 22:
		_tcscpy(szTitle, prefs->floppyslots[wParam - IDC_DF0].df);
		WIN32GUI_LoadUIString (IDS_ADF, szFormat, MAX_DPATH);
		_stprintf (szFilter, _T("%s "), szFormat);
		memcpy (szFilter + _tcslen (szFilter), DISK_FORMAT_STRING, sizeof (DISK_FORMAT_STRING));
		defext = _T("adf");
		break;
	case 1:
		WIN32GUI_LoadUIString (IDS_CHOOSEBLANK, szTitle, MAX_DPATH);
		WIN32GUI_LoadUIString (IDS_ADF, szFormat, MAX_DPATH);
		_stprintf (szFilter, _T("%s "), szFormat);
		memcpy (szFilter + _tcslen (szFilter), _T("(*.adf)\0*.adf\0"), 15 * sizeof (TCHAR));
		defext = _T("adf");
		break;
	case 2:
	case 3:
		WIN32GUI_LoadUIString (IDS_SELECTHDF, szTitle, MAX_DPATH);
		WIN32GUI_LoadUIString (IDS_HDF, szFormat, MAX_DPATH);
		_stprintf (szFilter, _T("%s "), szFormat);
		memcpy (szFilter + _tcslen (szFilter),  HDF_FORMAT_STRING, sizeof (HDF_FORMAT_STRING));
		defext = _T("hdf");
		break;
	case 4:
	case 5:
		WIN32GUI_LoadUIString (IDS_SELECTUAE, szTitle, MAX_DPATH);
		WIN32GUI_LoadUIString (IDS_UAE, szFormat, MAX_DPATH );
		_stprintf (szFilter, _T("%s "), szFormat);
		memcpy (szFilter + _tcslen (szFilter), _T("(*.uae)\0*.uae\0"), 15 * sizeof (TCHAR));
		defext = _T("uae");
		break;
	case 6:
		WIN32GUI_LoadUIString (IDS_SELECTROM, szTitle, MAX_DPATH);
		WIN32GUI_LoadUIString (IDS_ROM, szFormat, MAX_DPATH);
		_stprintf (szFilter, _T("%s "), szFormat);
		memcpy (szFilter + _tcslen (szFilter), ROM_FORMAT_STRING, sizeof (ROM_FORMAT_STRING));
		defext = _T("rom");
		break;
	case 7:
		WIN32GUI_LoadUIString (IDS_SELECTKEY, szTitle, MAX_DPATH);
		WIN32GUI_LoadUIString (IDS_KEY, szFormat, MAX_DPATH);
		_stprintf (szFilter, _T("%s "), szFormat);
		memcpy (szFilter + _tcslen (szFilter), _T("(*.key)\0*.key\0"), 15 * sizeof (TCHAR));
		defext = _T("key");
		break;
	case 15:
	case 16:
		WIN32GUI_LoadUIString (flag == 15 ? IDS_RESTOREINP : IDS_SAVEINP, szTitle, MAX_DPATH);
		WIN32GUI_LoadUIString (IDS_INP, szFormat, MAX_DPATH);
		_stprintf (szFilter, _T("%s "), szFormat);
		memcpy (szFilter + _tcslen (szFilter), INP_FORMAT_STRING, sizeof (INP_FORMAT_STRING));
		defext = _T("inp");
		break;
	case 9:
	case 10:
		WIN32GUI_LoadUIString (flag == 10 ? IDS_RESTOREUSS : IDS_SAVEUSS, szTitle, MAX_DPATH);
		WIN32GUI_LoadUIString (IDS_USS, szFormat, MAX_DPATH);
		_stprintf (szFilter, _T("%s "), szFormat);
		if (flag == 10) {
			memcpy (szFilter + _tcslen (szFilter), USS_FORMAT_STRING_RESTORE, sizeof (USS_FORMAT_STRING_RESTORE));
			all = 1;
		} else {
			TCHAR tmp[MAX_DPATH];
			memcpy (szFilter + _tcslen (szFilter), USS_FORMAT_STRING_SAVE, sizeof (USS_FORMAT_STRING_SAVE));
			p = szFilter;
			while (p[0] != 0 || p[1] !=0 ) p++;
			p++;
			WIN32GUI_LoadUIString (IDS_STATEFILE_UNCOMPRESSED, tmp, sizeof (tmp) / sizeof (TCHAR));
			_tcscat (p, tmp);
			_tcscat (p, _T(" (*.uss)"));
			p += _tcslen (p) + 1;
			_tcscpy (p, _T("*.uss"));
			p += _tcslen (p) + 1;
			WIN32GUI_LoadUIString (IDS_STATEFILE_RAMDUMP, tmp, sizeof (tmp) / sizeof (TCHAR));
			_tcscat (p, tmp);
			_tcscat (p, _T(" (*.dat)"));
			p += _tcslen (p) + 1;
			_tcscpy (p, _T("*.dat"));
			p += _tcslen (p) + 1;
			WIN32GUI_LoadUIString (IDS_STATEFILE_WAVE, tmp, sizeof (tmp) / sizeof (TCHAR));
			_tcscat (p, tmp);
			_tcscat (p, _T(" (*.wav)"));
			p += _tcslen (p) + 1;
			_tcscpy (p, _T("*.wav"));
			p += _tcslen (p) + 1;
			*p = 0;
			all = 0;
		}
		defext = _T("uss");
		break;
	case 11:
	case 19:
		WIN32GUI_LoadUIString (IDS_SELECTFLASH, szTitle, MAX_DPATH);
		WIN32GUI_LoadUIString (IDS_FLASH, szFormat, MAX_DPATH);
		_stprintf (szFilter, _T("%s "), szFormat);
		memcpy (szFilter + _tcslen (szFilter), _T("(*.nvr)\0*.nvr\0"), 15 * sizeof (TCHAR));
		defext = _T("nvr");
		break;
	case 8:
	default:
		WIN32GUI_LoadUIString (IDS_SELECTINFO, szTitle, MAX_DPATH);
		break;
	case 12:
		WIN32GUI_LoadUIString (IDS_SELECTFS, szTitle, MAX_DPATH);
		initialdir = path_out;
		break;
	case 13:
		WIN32GUI_LoadUIString (IDS_SELECTINFO, szTitle, MAX_DPATH);
		initialdir = path_out;
		break;
	case 14:
		_tcscpy (szTitle, _T("Select supported archive file"));
		_stprintf (szFilter, _T("%s (%s)"), _T("Archive"), ARCHIVE_STRING);
		_tcscpy (szFilter + _tcslen (szFilter) + 1, ARCHIVE_STRING);
		initialdir = path_out;
		break;
	case 17:
		WIN32GUI_LoadUIString (IDS_SELECTCD, szTitle, MAX_DPATH);
		WIN32GUI_LoadUIString (IDS_CD, szFormat, MAX_DPATH);
		_stprintf (szFilter, _T("%s "), szFormat);
		memcpy (szFilter + _tcslen (szFilter), CD_FORMAT_STRING, sizeof (CD_FORMAT_STRING) + sizeof (TCHAR));
		defext = _T("cue");
		break;
	case 18:
		WIN32GUI_LoadUIString (IDS_SELECTTAPE, szTitle, MAX_DPATH);
		break;
	case 20:
		_tcscpy(szTitle, _T("Select genlock image"));
		break;
	case 21:
		_tcscpy(szTitle, _T("Select genlock video"));
		break;
	case 23:
	case 24:
		_tcscpy(szTitle, _T("Select geometry file"));
		_stprintf (szFilter, _T("%s "), _T("Geometry files"));
		memcpy (szFilter + _tcslen (szFilter),  GEO_FORMAT_STRING, sizeof (GEO_FORMAT_STRING) + sizeof (TCHAR));
		defext = _T("geo");
		break;
		break;
	}
	if (all) {
		p = szFilter;
		while (p[0] != 0 || p[1] !=0) p++;
		p++;
		_tcscpy (p, _T("All files (*.*)"));
		p += _tcslen (p) + 1;
		_tcscpy (p, _T("*.*"));
		p += _tcslen (p) + 1;
		*p = 0;
	}
	openFileName.lStructSize = sizeof (OPENFILENAME);
	openFileName.hwndOwner = hDlg;
	openFileName.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
		OFN_LONGNAMES | OFN_HIDEREADONLY | OFN_NOCHANGEDIR | OFN_ENABLESIZING | (isfullscreen () > 0 ? OFN_ENABLEHOOK : 0);
	openFileName.lpstrFilter = szFilter;
	openFileName.lpstrDefExt = defext;
	openFileName.nFilterIndex = previousfilter[flag];
	openFileName.lpstrFile = full_path;
	openFileName.nMaxFile = MAX_DPATH;
	openFileName.lpstrFileTitle = file_name;
	openFileName.nMaxFileTitle = MAX_DPATH;
	openFileName.lpfnHook = ofnhook;
	if (initialdir)
		openFileName.lpstrInitialDir = initialdir;
	else
		openFileName.lpstrInitialDir = init_path;
	openFileName.lpstrTitle = szTitle;

	if (multi)
		openFileName.Flags |= OFN_ALLOWMULTISELECT;
	if (flag == 1 || flag == 3 || flag == 5 || flag == 9 || flag == 16 || flag == 24) {
		openFileName.Flags &= ~OFN_FILEMUSTEXIST;
		if (!(result = GetSaveFileName_2 (hDlg, &openFileName, guid)))
			write_log (_T("GetSaveFileNameX() failed, err=%d.\n"), GetLastError ());
	} else {
		if (flag == 11 || flag == 19) {
			openFileName.Flags &= ~OFN_FILEMUSTEXIST;
		}
		if (!(result = GetOpenFileName_2 (hDlg, &openFileName, guid)))
			write_log (_T("GetOpenFileNameX() failed, err=%d.\n"), GetLastError ());
	}
	if (result) {
		previousfilter[flag] = openFileName.nFilterIndex;
		setfilter (flag, previousfilter, filtername);
	}

	memcpy (full_path2, full_path, sizeof full_path);
	memcpy (stored_path, full_path, sizeof stored_path);
	next = 0;
	nextp = full_path2 + openFileName.nFileOffset;
	if (path_out) {
		if (multi) {
			while (nextp[0])
				nextp += _tcslen (nextp) + 1;
			memcpy (path_out, full_path2, (nextp - full_path2 + 1) * sizeof (TCHAR));
		} else {
			_tcscpy (path_out, full_path2);
		}
	}
	nextp = full_path2 + openFileName.nFileOffset;
	if (nextp[_tcslen (nextp) + 1] == 0)
		multi = 0;
	while (result && next >= 0)
	{
		next = -1;
		if (multi) {
			if (nextp[0] == 0)
				break;
			_stprintf (full_path, _T("%s\\%s"), full_path2, nextp);
			nextp += _tcslen (nextp) + 1;
		}
		switch (wParam)
		{
		case IDC_PATH_NAME:
		case IDC_PATH_FILESYS:
			if (flag == 8) {
				if(_tcsstr (full_path, _T("Configurations\\"))) {
					_tcscpy (full_path, init_path);
					_tcscat (full_path, file_name);
				}
			}
			SetDlgItemText (hDlg, (int)wParam, full_path);
			break;
		case IDC_GENLOCKFILESELECT:
			selectgenlock(prefs, hDlg, IDC_GENLOCKFILE, full_path);
			break;
		case IDC_PATH_GEOMETRY:
			SetDlgItemText (hDlg, (int)wParam, full_path);
			break;
		case IDC_CD_SELECT:
			selectcd (prefs, hDlg, 0, IDC_CD_TEXT, full_path);
			break;
		case IDC_DF0:
		case IDC_DF0QQ:
			selectdisk (prefs, hDlg, 0, IDC_DF0TEXT, full_path);
			next = IDC_DF1;
			break;
		case IDC_DF1:
		case IDC_DF1QQ:
			selectdisk (prefs, hDlg, 1, IDC_DF1TEXT, full_path);
			next = IDC_DF2;
			break;
		case IDC_DF2:
			selectdisk (prefs, hDlg, 2, IDC_DF2TEXT, full_path);
			next = IDC_DF3;
			break;
		case IDC_DF3:
			selectdisk (prefs, hDlg, 3, IDC_DF3TEXT, full_path);
			break;
		case IDC_DOSAVESTATE:
			savestate_initsave (full_path, openFileName.nFilterIndex, FALSE, true);
			break;
		case IDC_DOLOADSTATE:
			savestate_initsave (full_path, openFileName.nFilterIndex, FALSE, false);
			break;
		case IDC_CREATE:
			{
				drive_type atype = DRV_NONE;
				int hd = -1;
				TCHAR disk_name[32];
				disk_name[0] = 0; disk_name[31] = 0;
				GetDlgItemText (hDlg, IDC_CREATE_NAME, disk_name, 30);
				getcreatefloppytype(hDlg, &atype, &hd);
				if (disk_creatediskfile (&workprefs, full_path, 0, atype, hd, disk_name, ischecked (hDlg, IDC_FLOPPY_FFS), ischecked (hDlg, IDC_FLOPPY_BOOTABLE), NULL)) {
					fullpath (full_path, sizeof full_path / sizeof (TCHAR));
					DISK_history_add (full_path, -1, HISTORY_FLOPPY, 0);
				}
			}
			break;
		case IDC_CREATE_RAW:
			{
				drive_type atype = DRV_NONE;
				int hd = -1;
				TCHAR disk_name[32];
				disk_name[0] = 0; disk_name[31] = 0;
				GetDlgItemText(hDlg, IDC_CREATE_NAME, disk_name, 30);
				getcreatefloppytype(hDlg, &atype, &hd);
				if (disk_creatediskfile(&workprefs, full_path, 1, atype, hd, disk_name, ischecked(hDlg, IDC_FLOPPY_FFS), ischecked(hDlg, IDC_FLOPPY_BOOTABLE), NULL)) {
					fullpath(full_path, sizeof full_path / sizeof(TCHAR));
					DISK_history_add(full_path, -1, HISTORY_FLOPPY, 0);
				}
			}
			break;
		case IDC_LOAD:
			if (target_cfgfile_load (&workprefs, full_path, CONFIG_TYPE_DEFAULT, 0) == 0) {
				TCHAR szMessage[MAX_DPATH];
				WIN32GUI_LoadUIString (IDS_COULDNOTLOADCONFIG, szMessage, MAX_DPATH);
				pre_gui_message (szMessage);
			} else {
				SetDlgItemText (hDlg, IDC_EDITDESCRIPTION, workprefs.description);
				SetDlgItemText (hDlg, IDC_EDITNAME, full_path);
			}
			break;
		case IDC_SAVE:
			if (cfgfile_can_write(hDlg, full_path)) {
				SetDlgItemText(hDlg, IDC_EDITNAME, full_path);
				cfgfile_save(&workprefs, full_path, 0);
			}
			break;
		case IDC_ROMFILE:
			_tcscpy (workprefs.romfile, full_path);
			fullpath (workprefs.romfile, MAX_DPATH);
			read_kickstart_version(&workprefs);
			break;
		case IDC_ROMFILE2:
			_tcscpy (workprefs.romextfile, full_path);
			fullpath (workprefs.romextfile, MAX_DPATH);
			break;
		case IDC_CUSTOMROMFILE:
		{
			int v = xSendDlgItemMessage(hDlg, IDC_CUSTOMROMSELECT, CB_GETCURSEL, 0, 0);
			if (v >= 0 && v < MAX_ROM_BOARDS) {
				struct romboard *rb = &workprefs.romboards[v];
				_tcscpy(rb->lf.loadfile, full_path);
				fullpath(rb->lf.loadfile, MAX_DPATH);
				if (rb->start_address) {
					struct zfile *zf = zfile_fopen(rb->lf.loadfile, _T("rb"));
					if (zf) {
						rb->end_address = rb->start_address + zfile_size32(zf);
						rb->end_address = ((rb->end_address + 65535) & ~65535) - 1;
						rb->size = rb->end_address - rb->start_address + 1;
						zfile_fclose(zf);
					}
				}
			}
			break;
		}
		case IDC_FLASHFILE:
			_tcscpy (workprefs.flashfile, full_path);
			fullpath(workprefs.flashfile, MAX_DPATH);
			break;
		case IDC_RTCFILE:
			_tcscpy (workprefs.rtcfile, full_path);
			fullpath(workprefs.rtcfile, MAX_DPATH);
			break;
		case IDC_CARTFILE:
			_tcscpy (workprefs.cartfile, full_path);
			fullpath (workprefs.cartfile, MAX_DPATH);
			break;
		case IDC_SCSIROMFILE:
		{
			int val = gui_get_string_cursor(scsiromselect_table, hDlg, IDC_SCSIROMSELECT);	
			if (val != CB_ERR) {
				int index;
				struct boardromconfig *brc;
				brc = get_device_rom_new(&workprefs, expansionroms[scsiromselected].romtype, scsiromselectednum, &index);
				_tcscpy (brc->roms[index].romfile, full_path);
				fullpath (brc->roms[index].romfile, MAX_DPATH);
			}
			break;
		}
		case IDC_CPUBOARDROMFILE:
		{
			int index;
			struct boardromconfig *brc = get_device_rom_new(&workprefs, ROMTYPE_CPUBOARD, 0, &index);
			_tcscpy(brc->roms[index].romfile, full_path);
			fullpath(brc->roms[index].romfile, MAX_DPATH);
			break;
		}
		case IDC_STATEREC_PLAY:
		case IDC_STATEREC_RECORD:
		case IDC_STATEREC_SAVE:
			_tcscpy (workprefs.inprecfile, full_path);
			_tcscpy (workprefs.inprecfile, full_path);
			break;
		}
		if (!nosavepath || 1) {
			if (flag == 0 || flag == 1 || flag == 22) {
				amiga_path = _tcsstr (openFileName.lpstrFile, openFileName.lpstrFileTitle);
				if (amiga_path && amiga_path != openFileName.lpstrFile) {
					*amiga_path = 0;
					setdpath (_T("FloppyPath"), openFileName.lpstrFile);
				}
			} else if (flag == 2 || flag == 3 || flag == 23) {
				amiga_path = _tcsstr (openFileName.lpstrFile, openFileName.lpstrFileTitle);
				if (amiga_path && amiga_path != openFileName.lpstrFile) {
					*amiga_path = 0;
					setdpath (_T("hdfPath"), openFileName.lpstrFile);
				}
			} else if (flag == 17) {
				amiga_path = _tcsstr (openFileName.lpstrFile, openFileName.lpstrFileTitle);
				if (amiga_path && amiga_path != openFileName.lpstrFile) {
					*amiga_path = 0;
					setdpath (_T("CDPath"), openFileName.lpstrFile);
				}
			} else if (flag == 18) {
				amiga_path = _tcsstr (openFileName.lpstrFile, openFileName.lpstrFileTitle);
				if (amiga_path && amiga_path != openFileName.lpstrFile) {
					*amiga_path = 0;
					setdpath (_T("TapePath"), openFileName.lpstrFile);
				}
			} else if (flag == 20) {
				amiga_path = _tcsstr(openFileName.lpstrFile, openFileName.lpstrFileTitle);
				if (amiga_path && amiga_path != openFileName.lpstrFile) {
					*amiga_path = 0;
					setdpath(_T("GenlockImagePath"), openFileName.lpstrFile);
				}
			} else if (flag == 21) {
				amiga_path = _tcsstr(openFileName.lpstrFile, openFileName.lpstrFileTitle);
				if (amiga_path && amiga_path != openFileName.lpstrFile) {
					*amiga_path = 0;
					setdpath(_T("GenlockVideoPath"), openFileName.lpstrFile);
				}
			}
		}
		if (!multi)
			next = -1;
		if (next >= 0)
			wParam = next;
	}
	return result;
}
int DiskSelection (HWND hDlg, WPARAM wParam, int flag, struct uae_prefs *prefs, TCHAR *file_in, TCHAR *path_out)
{
	return DiskSelection_2 (hDlg, wParam, flag, prefs, file_in, path_out, NULL);
}
int MultiDiskSelection (HWND hDlg, WPARAM wParam, int flag, struct uae_prefs *prefs, TCHAR *path_out)
{
	int multi = 0;
	return DiskSelection_2 (hDlg, wParam, flag, prefs, NULL, path_out, &multi);
}
static int loopmulti (const TCHAR *s, TCHAR *out)
{
	static int index;

	if (!out) {
		index = uaetcslen(s) + 1;
		return 1;
	}
	if (index < 0)
		return 0;
	if (!s[index]) {
		if (s[uaetcslen(s) + 1] == 0) {
			_tcscpy (out, s);
			index = -1;
			return 1;
		}
		return 0;
	}
	_stprintf (out, _T("%s\\%s"), s, s + index);
	index += uaetcslen(s + index) + 1;
	return 1;
}

static BOOL CreateHardFile (HWND hDlg, uae_s64 hfsize, const TCHAR *dostype, TCHAR *newpath, TCHAR *outpath)
{
	HANDLE hf;
	int i = 0;
	BOOL result = FALSE;
	LONG highword = 0;
	DWORD ret, written;
	TCHAR init_path[MAX_DPATH] = _T("");
	uae_u32 dt;
	uae_u8 b;
	int sparse, dynamic;

	outpath[0] = 0;
	sparse = 0;
	dynamic = 0;
	dt = 0;
	if (ischecked (hDlg, IDC_HF_SPARSE))
		sparse = 1;
	if (ischecked (hDlg, IDC_HF_DYNAMIC)) {
		dynamic = 1;
		sparse = 0;
	}
	if (!DiskSelection (hDlg, IDC_PATH_NAME, 3, &workprefs, NULL, newpath))
		return FALSE;
	GetDlgItemText (hDlg, IDC_PATH_NAME, init_path, MAX_DPATH);
	if (*init_path && hfsize) {
		if (dynamic) {
			if (!_stscanf (dostype, _T("%x"), &dt))
				dt = 0;
			if (_tcslen (init_path) > 4 && !_tcsicmp (init_path + _tcslen (init_path) - 4, _T(".hdf")))
				_tcscpy (init_path + _tcslen (init_path) - 4, _T(".vhd"));
			result = vhd_create (init_path, hfsize, dt);
		} else {
			SetCursor (LoadCursor (NULL, IDC_WAIT));
			if ((hf = CreateFile (init_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)) != INVALID_HANDLE_VALUE) {
				if (sparse) {
					DWORD ret;
					DeviceIoControl (hf, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &ret, NULL);
				}
				if (hfsize >= 0x80000000) {
					highword = (DWORD)(hfsize >> 32);
					ret = SetFilePointer (hf, (DWORD)hfsize, &highword, FILE_BEGIN);
				} else {
					ret = SetFilePointer (hf, (DWORD)hfsize, NULL, FILE_BEGIN);
				}
				if (ret == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
					write_log (_T("SetFilePointer() failure for %s to posn %ud\n"), init_path, hfsize);
				else
					result = SetEndOfFile (hf);
				SetFilePointer (hf, 0, NULL, FILE_BEGIN);
				b = 0;
				WriteFile (hf, &b, 1, &written, NULL);
				WriteFile (hf, &b, 1, &written, NULL);
				WriteFile (hf, &b, 1, &written, NULL);
				WriteFile (hf, &b, 1, &written, NULL);
				if (_stscanf (dostype, _T("%x"), &dt) > 0) {
					SetFilePointer (hf, 0, NULL, FILE_BEGIN);
					b = dt >> 24;
					WriteFile (hf, &b, 1, &written, NULL);
					b = dt >> 16;
					WriteFile (hf, &b, 1, &written, NULL);
					b = dt >> 8;
					WriteFile (hf, &b, 1, &written, NULL);
					b = dt >> 0;
					WriteFile (hf, &b, 1, &written, NULL);
				}
				CloseHandle (hf);
			} else {
				write_log (_T("CreateFile() failed to create %s\n"), init_path);
			}
			SetCursor (LoadCursor (NULL, IDC_ARROW));
		}
	}
	if (!result) {
		TCHAR szMessage[MAX_DPATH];
		TCHAR szTitle[MAX_DPATH];
		WIN32GUI_LoadUIString (IDS_FAILEDHARDFILECREATION, szMessage, MAX_DPATH);
		WIN32GUI_LoadUIString (IDS_CREATIONERROR, szTitle, MAX_DPATH);
		MessageBox (hDlg, szMessage, szTitle, MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
	} else {
		_tcscpy (outpath, init_path);
	}
	return result;
}

static uae_s64 CalculateHardfileSize(HWND hDlg)
{
	uae_s64 mbytes = 0;
	TCHAR tmp[100];

	tmp[0] = 0;
	GetDlgItemText(hDlg, IDC_HF_SIZE, tmp, sizeof tmp / sizeof(TCHAR));
	for (int i = 0; i < _tcslen(tmp); i++) {
		if (tmp[i] == ',')
			tmp[i] = '.';
	}
	double v = _tstof(tmp);
	mbytes = (uae_s64)(v * 1024 * 1024);
	mbytes &= ~511;
	if (mbytes <= 0)
		mbytes = 0;

	return mbytes;
}

static const TCHAR *nth[] = {
	_T(""), _T("second "), _T("third "), _T("fourth "), _T("fifth "), _T("sixth "), _T("seventh "), _T("eighth "), _T("ninth "), _T("tenth ")
};

static void setguititle (HWND phwnd)
{
	static TCHAR title[200];
	TCHAR title2[1000];
	TCHAR *name;
	static HWND hwnd;

	if (phwnd)
		hwnd = phwnd;
	if (hwnd && !title[0]) {
		GetWindowText (hwnd, title, sizeof title / sizeof (TCHAR));
		if (_tcslen (WINUAEBETA) > 0) {
			_tcscat (title, BetaStr);
			if (_tcslen (WINUAEEXTRA) > 0) {
				_tcscat (title, _T(" "));
				_tcscat (title, WINUAEEXTRA);
			}
		}
	}
	title2[0] = 0;
	name = workprefs.config_window_title;
	if (name && _tcslen (name) > 0) {
		_tcscat (title2, name);
		_tcscat (title2, _T(" - "));
	}
	if (!title2[0]) {
		name = config_filename;
		if (name && _tcslen (name) > 0) {
			_tcscat (title2, _T("["));
			_tcscat (title2, name);
			if (_tcslen(title2) > 4 && !_tcsicmp(title2 + _tcslen(title2) - 4, _T(".uae")))
				title2[_tcslen(title2) - 4] = 0;
			_tcscat (title2, _T("] - "));
		}
	}
	_tcscat (title2, title);
	SetWindowText (hwnd, title2);
}


static void GetConfigPath (TCHAR *path, struct ConfigStruct *parent, int noroot)
{
	if (parent == NULL) {
		path[0] = 0;
		if (!noroot) {
			fetch_path (_T("ConfigurationPath"), path, MAX_DPATH);
		}
		return;
	}
	if (parent) {
		GetConfigPath (path, parent->Parent, noroot);
		_tcsncat (path, parent->Name, MAX_DPATH - _tcslen(path));
		_tcsncat (path, _T("\\"), MAX_DPATH - _tcslen(path));
	}
}

void FreeConfigStruct (struct ConfigStruct *config)
{
	xfree (config);
}
struct ConfigStruct *AllocConfigStruct (void)
{
	struct ConfigStruct *config;

	config = xcalloc (struct ConfigStruct, 1);
	return config;
}

static void FreeConfigStore (void)
{
	for (int i = 0; i < configstoresize; i++) {
		FreeConfigStruct(configstore[i]);
	}
	xfree(configstore);
	configstoresize = configstoreallocated = 0;
	configstore = NULL;

	for (int i = 0; i < categorystoresize; i++) {
		xfree(categorystore[i]);
	}
	xfree(categorystore);
	categorystoresize = 0;
	categorystore = NULL;
}

static void sortcategories(void)
{
	for (int i = 0; i < categorystoresize; i++) {
		for (int j = i + 1; j < categorystoresize; j++) {
			struct CategoryStruct *s1 = categorystore[i];
			struct CategoryStruct *s2 = categorystore[j];
			if (_tcsicmp(s1->category, s2->category) > 0) {
				struct CategoryStruct *s = categorystore[i];
				categorystore[i] = categorystore[j];
				categorystore[j] = s;
			}
		}
	}
}

static void addtocategories(const TCHAR *category)
{
	if (!category[0])
		return;
	bool found = false;
	for (int j = 0; j < categorystoresize; j++) {
		struct CategoryStruct *s = categorystore[j];
		if (!_tcsicmp(category, s->category)) {
			found = true;
			break;
		}
	}
	if (!found) {
		if (categorystore == NULL || categorystoresize == categorystoreallocated) {
			categorystoreallocated += 100;
			categorystore = xrealloc(struct CategoryStruct*, categorystore, categorystoreallocated);
		}
		struct CategoryStruct *s = categorystore[categorystoresize++] = xcalloc(struct CategoryStruct, 1);
		_tcscpy(s->category, category);
	}
}

static void getconfigcache (TCHAR *dst, const TCHAR *path)
{
	_tcscpy (dst, path);
	_tcsncat (dst, _T("configuration.cache"), MAX_DPATH - _tcslen(dst));
}

static void deleteconfigcache(void)
{
	TCHAR path[MAX_DPATH], path2[MAX_DPATH];
	GetConfigPath(path, NULL, FALSE);
	if (!path[0])
		return;
	getconfigcache(path2, path);
	_wunlink(path2);
}

static TCHAR *fgetsx (TCHAR *dst, FILE *f)
{
	TCHAR *s2;
	dst[0] = 0;
	s2 = fgetws (dst, MAX_DPATH, f);
	if (!s2)
		return NULL;
	if (_tcslen (dst) == 0)
		return dst;
	if (dst[_tcslen (dst) - 1] == '\n')
		dst[_tcslen (dst) - 1] = 0;
	if (dst[_tcslen (dst) - 1] == '\r')
		dst[_tcslen (dst) - 1] = 0;
	return dst;
}

static const TCHAR configcachever[] = _T("WinUAE Configuration.Cache");

static void setconfighosthard (struct ConfigStruct *config)
{
	if (!config->Directory)
		return;
	if (!_tcsicmp (config->Name, CONFIG_HOST))
		config->host = 1;
	if (!_tcsicmp (config->Name, CONFIG_HARDWARE))
		config->hardware = 1;
}

static void flushconfigcache (const TCHAR *cachepath)
{
	FILE *zcache;
	zcache = _tfopen (cachepath, _T("r"));
	if (zcache == NULL)
		return;
	fclose (zcache);
	bool hidden = my_isfilehidden (cachepath);
	my_setfilehidden (cachepath, false);
	zcache = _tfopen (cachepath, _T("w+, ccs=UTF-8"));
	if (zcache)
		fclose (zcache);
	my_setfilehidden (cachepath, hidden);
	write_log (_T("'%s' flushed\n"), cachepath);
}

static struct ConfigStruct *readconfigcache (const TCHAR *path)
{
	FILE *zcache;
	TCHAR cachepath[MAX_DPATH];
	TCHAR buf[MAX_DPATH];
	TCHAR rootpath[MAX_DPATH];
	TCHAR path2[MAX_DPATH], tmp[MAX_DPATH];
	struct ConfigStruct *cs, *first;
	int err;
	int filelines, dirlines, headlines, dirmode, lines;
	TCHAR dirsep = '\\';
	FILETIME t;
	SYSTEMTIME st;
	ULARGE_INTEGER t1, stt, dirtt;
	HANDLE h;
	WIN32_FIND_DATA ffd;

#if CONFIGCACHE == 0
	return NULL;
#endif
	err = 0;
	first = NULL;
	getconfigcache (cachepath, path);
	zcache = my_opentext (cachepath);
	if (!zcache)
		return NULL;
	if (!configurationcache) {
		fclose (zcache);
		_wunlink (cachepath);
		return NULL;
	}
	fgetsx (buf, zcache);
	if (feof (zcache))
		goto end;
	if (_tcscmp (buf, configcachever))
		goto end;
	GetFullPathName (path, sizeof path2 / sizeof (TCHAR), path2, NULL);
	_tcscpy (rootpath, path2);
	if (path2[_tcslen (path2) - 1] == '\\' || path2[_tcslen (path2) -1] == '/')
		path2[_tcslen (path2) - 1] = 0;
	h = FindFirstFile (path2, &ffd);
	if (h == INVALID_HANDLE_VALUE)
		goto end;
	FindClose (h);
	memcpy (&dirtt, &ffd.ftLastWriteTime, sizeof (ULARGE_INTEGER));

	fgetsx (buf, zcache);
	headlines = _tstol (buf);
	fgetsx (buf, zcache);
	headlines--;
	dirlines = _tstol (buf);
	fgetsx (buf, zcache);
	headlines--;
	filelines = _tstol (buf);
	fgetsx (buf, zcache);
	t1.QuadPart = _tstoi64 (buf);
	headlines--;
	GetSystemTime (&st);
	SystemTimeToFileTime (&st, &t);
	memcpy (&stt, &t, sizeof (ULARGE_INTEGER));

	if (headlines < 0 || dirlines < 3 || filelines < 3 ||
		t1.QuadPart == 0 || t1.QuadPart > stt.QuadPart || dirtt.QuadPart > t1.QuadPart)
		goto end;

	while (headlines-- > 0)
		fgetsx (buf, zcache);
	fgetsx (buf, zcache);
	if (buf[0] != ';')
		goto end;

	while (fgetsx (buf, zcache)) {
		TCHAR c;
		TCHAR dirpath[MAX_DPATH];

		dirmode = 0;
		if (_tcslen (buf) > 0) {
			c = buf[_tcslen (buf) - 1];
			if (c == '/' || c == '\\') {
				dirmode = 1;
				dirsep = c;
			}
		}

		_tcscpy (dirpath, buf);
		if (dirmode) {
			lines = dirlines;
		} else {
			TCHAR *p;
			lines = filelines;
			p = _tcsrchr (dirpath, dirsep);
			if (p)
				p[0] = 0;
			else
				dirpath[0] = 0;
		}

		lines--;
		cs = AllocConfigStruct ();
		if (configstore == NULL || configstoreallocated == configstoresize) {
			configstoreallocated += 100;
			configstore = xrealloc(struct ConfigStruct*, configstore, configstoreallocated);
		}
		configstore[configstoresize++] = cs;
		if (!first)
			first = cs;

		cs->Directory = dirmode;
		_tcscpy (tmp, path);
		_tcscat (tmp, dirpath);
		_tcscpy (cs->Fullpath, tmp);
		_tcscpy (cs->Path, dirpath);

		fgetsx (tmp, zcache);
		lines--;
		t1.QuadPart = _tstoi64 (tmp);
		if (t1.QuadPart > stt.QuadPart)
			goto end;

		fgetsx (cs->Name, zcache);
		lines--;
		fgetsx (cs->Description, zcache);
		lines--;

		_tcscpy (tmp, cs->Path);
		if (_tcslen (tmp) > 0) {
			TCHAR *p = tmp;
			if (tmp[_tcslen (tmp) - 1] == dirsep) {
				tmp[_tcslen (tmp) - 1] = 0;
				p = _tcsrchr (tmp, dirsep);
				if (p)
					p[1] = 0;
			} else {
				tmp[_tcslen (tmp) + 1] = 0;
				tmp[_tcslen (tmp)] = dirsep;
			}
			if (p) {
				int i;
				for (i = 0; i < configstoresize; i++) {
					struct ConfigStruct *cs2 = configstore[i];
					if (cs2 != cs && !_tcscmp (cs2->Path, tmp) && cs2->Directory) {
						cs->Parent = cs2;
						if (!cs2->Child)
							cs2->Child = cs;
						cs->host = cs2->host;
						cs->hardware = cs2->hardware;
					}
				}
			}
		}

		if (_tcslen (cs->Path) > 0 && !dirmode) {
			_tcscat (cs->Path, _T("\\"));
			_tcscat (cs->Fullpath, _T("\\"));
		}

		if (!dirmode) {
			fgetsx (cs->HardwareLink, zcache);
			lines--;
			fgetsx (cs->HostLink, zcache);
			lines--;
			fgetsx (buf, zcache);
			lines--;
			cs->Type = _tstol (buf);
			if (lines > 0) {
				fgetsx(cs->Artpath, zcache);
				lines--;
			}
			if (lines > 0) {
				fgetsx(cs->Category, zcache);
				addtocategories(cs->Category);
				lines--;
			}
			if (lines > 0) {
				fgetsx(cs->Tags, zcache);
				lines--;
			}
		}

		setconfighosthard (cs);

		if (lines < 0)
			goto end;
		while (lines-- > 0)
			fgetsx (tmp, zcache);

		fgetsx (tmp, zcache);
		if (tmp[0] != ';')
			goto end;

	}

end:

	if (!feof (zcache))
		err = 1;
	fclose (zcache);
	if (err || first == NULL) {
		write_log (_T("'%s' load failed\n"), cachepath);
		flushconfigcache (cachepath);
		FreeConfigStore ();
		return NULL;
	} else {
		write_log (_T("'%s' loaded successfully\n"), cachepath);
	}
	return first;
}

static void writeconfigcacheentry (FILE *zcache, const TCHAR *relpath, struct ConfigStruct *cs)
{
	TCHAR path2[MAX_DPATH];
	TCHAR lf = 10;
	TCHAR el[] = _T(";\n");
	TCHAR *p;
	ULARGE_INTEGER li;

	GetFullPathName (cs->Fullpath, sizeof path2 / sizeof (TCHAR), path2, NULL);
	if (_tcslen (path2) < _tcslen (relpath))
		return;
	if (_tcsncmp (path2, relpath, _tcslen (relpath)))
		return;
	p = path2 + _tcslen (relpath);
	if (!cs->Directory)
		_tcscat (p, cs->Name);
	fwrite (p, _tcslen (p), sizeof (TCHAR), zcache);
	fwrite (&lf, 1, sizeof (TCHAR), zcache);

	memcpy (&li, &cs->t, sizeof (ULARGE_INTEGER));
	_stprintf (path2, _T("%I64u"), li.QuadPart);
	fwrite (path2, _tcslen (path2), sizeof (TCHAR), zcache);
	fwrite (&lf, 1, sizeof (TCHAR), zcache);

	fwrite (cs->Name, _tcslen (cs->Name), sizeof (TCHAR), zcache);
	fwrite (&lf, 1, sizeof (TCHAR), zcache);
	fwrite (cs->Description, _tcslen (cs->Description), sizeof (TCHAR), zcache);
	fwrite (&lf, 1, sizeof (TCHAR), zcache);

	if (!cs->Directory) {
		fwrite (cs->HardwareLink, _tcslen (cs->HardwareLink), sizeof (TCHAR), zcache);
		fwrite (&lf, 1, sizeof (TCHAR), zcache);
		fwrite (cs->HostLink, _tcslen (cs->HostLink), sizeof (TCHAR), zcache);
		fwrite (&lf, 1, sizeof (TCHAR), zcache);
		_stprintf (path2, _T("%d"), cs->Type);
		fwrite (path2, _tcslen (path2), sizeof (TCHAR), zcache);
		fwrite (&lf, 1, sizeof (TCHAR), zcache);
		fwrite(cs->Artpath, _tcslen(cs->Artpath), sizeof(TCHAR), zcache);
		fwrite(&lf, 1, sizeof(TCHAR), zcache);
		fwrite(cs->Category, _tcslen(cs->Category), sizeof(TCHAR), zcache);
		fwrite(&lf, 1, sizeof(TCHAR), zcache);
		fwrite(cs->Tags, _tcslen(cs->Tags), sizeof(TCHAR), zcache);
		fwrite(&lf, 1, sizeof(TCHAR), zcache);
	}

	fwrite (el, _tcslen (el), sizeof (TCHAR), zcache);
}

static void writeconfigcacherec (FILE *zcache, const TCHAR *relpath, struct ConfigStruct *cs)
{
	if (!cs->Directory)
		return;
	writeconfigcacheentry (zcache, relpath, cs);
	for (int i = 0; i < configstoresize; i++) {
		struct ConfigStruct *cs2 = configstore[i];
		if (cs2->Parent == cs)
			writeconfigcacherec (zcache, relpath, cs2);
	}
}

static void writeconfigcache (const TCHAR *path)
{
	TCHAR lf = 10;
	FILE *zcache;
	TCHAR cachepath[MAX_DPATH];
	TCHAR path2[MAX_DPATH];
	FILETIME t;
	ULARGE_INTEGER ul;
	SYSTEMTIME st;

	if (!configurationcache)
		return;
	getconfigcache (cachepath, path);
	bool hidden = my_isfilehidden (cachepath);
	my_setfilehidden (cachepath, false);
	zcache = _tfopen (cachepath, _T("w, ccs=UTF-8"));
	if (!zcache)
		return;
	t.dwHighDateTime = t.dwLowDateTime = 0;
	GetSystemTime (&st);
	SystemTimeToFileTime (&st, &t);
	fwrite (configcachever, _tcslen (configcachever), sizeof (TCHAR), zcache);
	fwrite (&lf, 1, sizeof (TCHAR), zcache);
	ul.HighPart = t.dwHighDateTime;
	ul.LowPart = t.dwLowDateTime;
	_stprintf (path2, _T("3\n4\n10\n%I64u\n;\n"), ul.QuadPart);
	fwrite (path2, _tcslen (path2), sizeof (TCHAR), zcache);
	GetFullPathName (path, sizeof path2 / sizeof (TCHAR), path2, NULL);
	for (int i = 0; i < configstoresize; i++) {
		struct ConfigStruct *cs = configstore[i];
		if (cs->Directory && cs->Parent == NULL)
			writeconfigcacherec (zcache, path2, cs);
	}
	for (int i = 0; i < configstoresize; i++) {
		struct ConfigStruct *cs = configstore[i];
		if (!cs->Directory)
			writeconfigcacheentry (zcache, path2, cs);
	}
	fclose (zcache);
	my_setfilehidden (cachepath, hidden);
	write_log (_T("'%s' created\n"), cachepath);
}

static struct ConfigStruct *GetConfigs (struct ConfigStruct *configparent, int usedirs, int *level, int flushcache)
{
	DWORD num_bytes = 0;
	TCHAR path[MAX_DPATH];
	TCHAR path2[MAX_DPATH];
	TCHAR shortpath[MAX_DPATH];
	WIN32_FIND_DATA find_data;
	struct ConfigStruct *config, *first;
	HANDLE handle;

	if (*level == 0)
		FreeConfigStore ();

	first = NULL;
	GetConfigPath (path, configparent, FALSE);
	GetConfigPath (shortpath, configparent, TRUE);
	_tcscpy (path2, path);
	_tcsncat (path2, _T("*.*"), MAX_DPATH - _tcslen(path2));

	if (*level == 0) {
		if (flushcache) {
			TCHAR cachepath[MAX_DPATH];
			getconfigcache (cachepath, path);
			flushconfigcache (cachepath);
		}
		first = readconfigcache (path);
		sortcategories();
		if (first)
			return first; 
	}

	handle = FindFirstFile (path2, &find_data);
	if (handle == INVALID_HANDLE_VALUE) {
#ifndef SINGLEFILE
		// Either the directory has no .CFG files, or doesn't exist.
		// Create the directory, even if it already exists.  No harm, and don't check return codes, because
		// we may be doing this on a read-only media like CD-ROM.
		if (configparent == NULL) {
			GetConfigPath (path, NULL, FALSE);
			CreateDirectory (path, NULL);
		}
#endif
		return NULL;
	}
	for (;;) {
		config = NULL;
		if (_tcscmp (find_data.cFileName, _T(".")) && _tcscmp (find_data.cFileName, _T(".."))) {
			int ok = 0;
			config = AllocConfigStruct ();
			_tcscpy (config->Path, shortpath);
			_tcscpy (config->Fullpath, path);
			memcpy (&config->t, &find_data.ftLastWriteTime, sizeof (FILETIME));
			if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && usedirs) {
				if ((*level) < 2) {
					struct ConfigStruct *child;
					_tcscpy (config->Name, find_data.cFileName);
					_tcscpy (config->Path, shortpath);
					_tcscat (config->Path, config->Name);
					_tcscat (config->Path, _T("\\"));
					_tcscpy (config->Fullpath, path);
					_tcscat (config->Fullpath, config->Name);
					_tcscat (config->Fullpath, _T("\\"));
					config->Directory = 1;
					(*level)++;
					config->Parent = configparent;
					setconfighosthard (config);
					child = GetConfigs (config, usedirs, level, FALSE);
					(*level)--;
					if (child)
						config->Child = child;
					ok = 1;
				}
			} else if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
				TCHAR path3[MAX_DPATH];
				if (_tcslen (find_data.cFileName) > 4 && !strcasecmp (find_data.cFileName + _tcslen (find_data.cFileName) - 4, _T(".uae"))) {
					_tcscpy (path3, path);
					_tcsncat (path3, find_data.cFileName, MAX_DPATH - _tcslen(path3));
					config->Artpath[0] = 0;
					struct uae_prefs *p = cfgfile_open(path3, &config->Type);
					if (p) {
						cfgfile_get_description(p, NULL, config->Description, config->Category, config->Tags, config->HostLink, config->HardwareLink, NULL);
						_tcscpy(config->Name, find_data.cFileName);
						if (artcache) {
							cfgfile_detect_art(p, config->Artpath);
						}
						addtocategories(config->Category);
						cfgfile_close(p);
						ok = 1;
					}
				}
			}
			if (!ok) {
				FreeConfigStruct (config);
				config = NULL;
			}
		}
		if (config) {
			if (configparent) {
				config->host = configparent->host;
				config->hardware = configparent->hardware;
			}
			config->Parent = configparent;
			if (configstore == NULL || configstoreallocated == configstoresize) {
				configstoreallocated += 100;
				configstore = xrealloc(struct ConfigStruct*, configstore, configstoreallocated);
			}
			configstore[configstoresize++] = config;
			if (first == NULL)
				first = config;
		}
		if (FindNextFile (handle, &find_data) == 0) {
			FindClose(handle);
			break;
		}
	}
	if (*level == 0 && CONFIGCACHE)
		writeconfigcache (path);

	sortcategories();
	return first;
}

static struct ConfigStruct *CreateConfigStore (struct ConfigStruct *oldconfig, int flushcache)
{
	int level;
	TCHAR path[MAX_DPATH], name[MAX_DPATH];
	struct ConfigStruct *cs;

	if (oldconfig) {
		_tcscpy (path, oldconfig->Path);
		_tcscpy (name, oldconfig->Name);
	}
	level = 0;
	GetConfigs (NULL, 1, &level, flushcache);
	if (oldconfig) {
		for (int i = 0; i < configstoresize; i++) {
			cs = configstore[i];
			if (!cs->Directory && !_tcscmp (path, cs->Path) && !_tcscmp (name, cs->Name))
				return cs;
		}
	}
	return 0;
}

static TCHAR *HandleConfiguration (HWND hDlg, int flag, struct ConfigStruct *config, TCHAR *newpath)
{
	TCHAR name[MAX_DPATH], desc[MAX_DPATH];
	TCHAR path[MAX_DPATH];
	static TCHAR full_path[MAX_DPATH];
	int ok = 1;
	bool absolutepath = false;

	full_path[0] = 0;
	name[0] = 0;
	desc[0] = 0;
	config_pathfilename[0] = 0;
	GetDlgItemText (hDlg, IDC_EDITNAME, name, MAX_DPATH);
	if ((name[0] == '\\' && name[1] == '\\') || (_istalpha(name[0]) && name[1] == ':'))
		absolutepath = true;
	_tcscpy (config_filename, name);
	if (flag == CONFIG_SAVE_FULL || flag == CONFIG_SAVE) {
		if (_tcslen (name) < 4 || strcasecmp (name + _tcslen (name) - 4, _T(".uae"))) {
			_tcscat (name, _T(".uae"));
			SetDlgItemText (hDlg, IDC_EDITNAME, name);
		}
		if (config && !absolutepath)
			_tcscpy (config->Name, name);
	}
	GetDlgItemText (hDlg, IDC_EDITDESCRIPTION, desc, MAX_DPATH);
	if (config) {
		_tcscpy (path, config->Fullpath);
		_tcscat(config_pathfilename, config->Path);
		_tcscat(config_pathfilename, config->Name);
		_tcsncat(path, name, MAX_DPATH - _tcslen(path));
	} else {
		if (absolutepath) {
			_tcscpy(path, name);
		} else {
			fetch_configurationpath(path, sizeof(path) / sizeof(TCHAR));
			_tcsncat(path, name, MAX_DPATH - _tcslen(path));
		}
		TCHAR fname[MAX_DPATH];
		getfilepart(fname, sizeof fname / sizeof(TCHAR), name);
		SetDlgItemText(hDlg, IDC_EDITNAME, fname);
	}
	_tcscpy (full_path, path);
	if (!config_pathfilename[0]) {
		_tcscat(config_pathfilename, full_path);
	}
	switch (flag)
	{
	case CONFIG_SAVE_FULL:
		ok = DiskSelection(hDlg, IDC_SAVE, 5, &workprefs, NULL, newpath);
		GetDlgItemText (hDlg, IDC_EDITNAME, name, MAX_DPATH);
		_tcscpy(config_filename, name);
		_tcscpy(config_pathfilename, name);
		break;

	case CONFIG_LOAD_FULL:
		if ((ok = DiskSelection(hDlg, IDC_LOAD, 4, &workprefs, NULL, newpath))) {
			EnableWindow(GetDlgItem (hDlg, IDC_VIEWINFO), workprefs.info[0]);
			GetDlgItemText (hDlg, IDC_EDITNAME, name, MAX_DPATH);
			_tcscpy(config_filename, name);
			_tcscpy(config_pathfilename, name);
		}
		break;

	case CONFIG_SAVE:
		if (_tcslen (name) == 0 || _tcscmp (name, _T(".uae")) == 0) {
			TCHAR szMessage[MAX_DPATH];
			WIN32GUI_LoadUIString(IDS_MUSTENTERNAME, szMessage, MAX_DPATH);
			pre_gui_message (szMessage);
		} else if (cfgfile_can_write(hDlg, path)) {
			_tcscpy (workprefs.description, desc);
			cfgfile_save (&workprefs, path, configtypepanel);
		}
		break;

	case CONFIG_LOAD:
		if (_tcslen (name) == 0) {
			TCHAR szMessage[MAX_DPATH];
			WIN32GUI_LoadUIString (IDS_MUSTSELECTCONFIG, szMessage, MAX_DPATH);
			pre_gui_message (szMessage);
			ok = 0;
		}
		if (ok && !zfile_exists(path)) {
			TCHAR fname[MAX_DPATH];
			fetch_configurationpath(fname, sizeof(fname) / sizeof(TCHAR));
			if (_tcsncmp(fname, path, _tcslen(fname)))
				_tcscat(fname, path);
			else
				_tcscpy(fname, path);
			if (!zfile_exists(fname)) {
				TCHAR szMessage[MAX_DPATH];
				WIN32GUI_LoadUIString(IDS_COULDNOTLOADCONFIG, szMessage, MAX_DPATH);
				pre_gui_message(szMessage);
				config_filename[0] = 0;
				ok = 0;
			}
		}
		if (ok) {
			if (target_cfgfile_load (&workprefs, path, configtypepanel, 0) == 0) {
				TCHAR szMessage[MAX_DPATH];
				WIN32GUI_LoadUIString (IDS_COULDNOTLOADCONFIG, szMessage, MAX_DPATH);
				pre_gui_message (szMessage);
				config_filename[0] = 0;
				ok = 0;
			} else {
				ew (hDlg, IDC_VIEWINFO, workprefs.info[0]);
			}
		}
		break;

	case CONFIG_DELETE:
		{
			TCHAR szMessage[MAX_DPATH];
			TCHAR szTitle[MAX_DPATH];
			TCHAR msg[MAX_DPATH];
			WIN32GUI_LoadUIString(IDS_DELETECONFIGTITLE, szTitle, MAX_DPATH);
			ok = 0;
			if (name[0] == 0) {
				if (config && config->Fullpath[0]) {
					// directory selected
					bool allowdelete = false;
					TCHAR fp[MAX_DPATH];
					_tcscpy(fp, config->Fullpath);
					_tcscat(fp, _T("*"));
					WIN32_FIND_DATA fd;
					HANDLE h = FindFirstFile(fp, &fd);
					if (h != INVALID_HANDLE_VALUE) {
						allowdelete = true;
						for (;;) {
							if (fd.cFileName[0] != '.' || !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
								allowdelete = false;
							}
							if (!FindNextFile(h, &fd))
								break;
						}
						FindClose(h);
					}
					if (allowdelete) {
						WIN32GUI_LoadUIString(IDS_DELETECONFIGDIRCONFIRMATION, szMessage, MAX_DPATH);
						TCHAR fp[MAX_DPATH];
						_tcscpy(fp, config->Fullpath);
						fp[_tcslen(fp) - 1] = 0;
						_stprintf(msg, szMessage, fp);
						if (MessageBox(hDlg, msg, szTitle,
							MB_YESNO | MB_ICONWARNING | MB_APPLMODAL | MB_SETFOREGROUND) == IDYES) {
							if (RemoveDirectory(fp)) {
								write_log(_T("deleted config directory '%s'\n"), fp);
								config_filename[0] = 0;
								ok = 1;
							}
						}
					} else {
						WIN32GUI_LoadUIString(IDS_DELETECONFIGDIRNOTEMPTY, szMessage, MAX_DPATH);
						MessageBox(hDlg, szMessage, szTitle, MB_OK | MB_ICONWARNING | MB_APPLMODAL | MB_SETFOREGROUND);
					}
				} else {
					TCHAR szMessage[MAX_DPATH];
					WIN32GUI_LoadUIString(IDS_MUSTSELECTCONFIGFORDELETE, szMessage, MAX_DPATH);
					pre_gui_message(szMessage);
				}
			} else {
				// config file selected
				WIN32GUI_LoadUIString(IDS_DELETECONFIGCONFIRMATION, szMessage, MAX_DPATH);
				_stprintf(msg, szMessage, name);
				if (MessageBox(hDlg, msg, szTitle,
					MB_YESNO | MB_ICONWARNING | MB_APPLMODAL | MB_SETFOREGROUND) == IDYES) {
					cfgfile_backup(path);
					if (DeleteFile(path)) {
						write_log(_T("deleted config '%s'\n"), path);
						config_filename[0] = 0;
						ok = 1;
					} else {
						write_log(_T("deleted config '%s' returned %x\n"), path, GetLastError());
						if (!my_existsfile(path)) {
							write_log(_T("deleted config '%s'\n"), path);
							config_filename[0] = 0;
							ok = 1;
						}
					}
				}
			}
			config_pathfilename[0] = 0;
		}
		break;
	}

	setguititle(NULL);
	return ok ? full_path : NULL;
}


static int disk_in_drive (int entry)
{
	int i;
	for (i = 0; i < 4; i++) {
		if (_tcslen (workprefs.dfxlist[entry]) > 0 && !_tcscmp (workprefs.dfxlist[entry], workprefs.floppyslots[i].df))
			return i;
	}
	return -1;
}

static int disk_swap (int entry, int mode)
{
	int drv, i, drvs[4] = { -1, -1, -1, -1 };

	for (i = 0; i < MAX_SPARE_DRIVES; i++) {
		drv = disk_in_drive (i);
		if (drv >= 0)
			drvs[drv] = i;
	}
	if ((drv = disk_in_drive (entry)) >= 0) {
		if (mode < 0) {
			workprefs.floppyslots[drv].df[0] = 0;
			return 1;
		}

		if (_tcscmp (workprefs.floppyslots[drv].df, currprefs.floppyslots[drv].df)) {
			_tcscpy (workprefs.floppyslots[drv].df, currprefs.floppyslots[drv].df);
			disk_insert (drv, workprefs.floppyslots[drv].df);
		} else {
			workprefs.floppyslots[drv].df[0] = 0;
		}
		if (drvs[0] < 0 || drvs[1] < 0 || drvs[2] < 0 || drvs[3] < 0) {
			drv++;
			while (drv < 4 && drvs[drv] >= 0)
				drv++;
			if (drv < 4 && workprefs.floppyslots[drv].dfxtype >= 0) {
				_tcscpy (workprefs.floppyslots[drv].df, workprefs.dfxlist[entry]);
				disk_insert (drv, workprefs.floppyslots[drv].df);
			}
		}
		return 1;
	}
	for (i = 0; i < 4; i++) {
		if (drvs[i] < 0 && workprefs.floppyslots[i].dfxtype >= 0) {
			_tcscpy (workprefs.floppyslots[i].df, workprefs.dfxlist[entry]);
			disk_insert (i, workprefs.floppyslots[i].df);
			return 1;
		}
	}
	_tcscpy (workprefs.floppyslots[0].df, workprefs.dfxlist[entry]);
	disk_insert (0, workprefs.floppyslots[0].df);
	return 1;
}

static int input_selected_device = -1;
static int input_selected_widget, input_total_devices;
static int input_selected_event, input_selected_sub_num;
static int input_copy_from;

struct remapcustoms_s
{
	uae_u64 flags;
	uae_u64 mask;
	const TCHAR *name;
};
static struct remapcustoms_s remapcustoms[] =
{
	{ 0, IDEV_MAPPED_AUTOFIRE_SET | IDEV_MAPPED_TOGGLE | IDEV_MAPPED_INVERTTOGGLE | IDEV_MAPPED_INVERT,
	NULL },
	{ IDEV_MAPPED_AUTOFIRE_SET, IDEV_MAPPED_AUTOFIRE_SET | IDEV_MAPPED_TOGGLE | IDEV_MAPPED_INVERTTOGGLE | IDEV_MAPPED_INVERT,
	NULL },
	{ IDEV_MAPPED_AUTOFIRE_SET | IDEV_MAPPED_TOGGLE, IDEV_MAPPED_AUTOFIRE_SET | IDEV_MAPPED_TOGGLE | IDEV_MAPPED_INVERTTOGGLE | IDEV_MAPPED_INVERT,
	NULL },
	{ IDEV_MAPPED_AUTOFIRE_SET | IDEV_MAPPED_INVERTTOGGLE, IDEV_MAPPED_AUTOFIRE_SET | IDEV_MAPPED_TOGGLE | IDEV_MAPPED_INVERTTOGGLE | IDEV_MAPPED_INVERT,
	NULL },
	{ IDEV_MAPPED_AUTOFIRE_SET | IDEV_MAPPED_INVERT | IDEV_MAPPED_TOGGLE, IDEV_MAPPED_AUTOFIRE_SET | IDEV_MAPPED_TOGGLE | IDEV_MAPPED_INVERTTOGGLE | IDEV_MAPPED_INVERT,
	NULL },
	{ NULL }
};

static void getqualifiername (TCHAR *p, uae_u64 mask)
{
	*p = 0;
	if (mask == IDEV_MAPPED_QUALIFIER_SPECIAL) {
		_tcscpy (p, _T("*"));
	} else if (mask == (IDEV_MAPPED_QUALIFIER_SPECIAL << 1)) {
		_tcscpy (p, _T("* [R]"));
	} else if (mask == IDEV_MAPPED_QUALIFIER_SHIFT) {
		_tcscpy (p, _T("Shift"));
	} else if (mask == (IDEV_MAPPED_QUALIFIER_SHIFT << 1)) {
		_tcscpy (p, _T("Shift [R]"));
	} else if (mask == IDEV_MAPPED_QUALIFIER_CONTROL) {
		_tcscpy (p, _T("Ctrl"));
	} else if (mask == (IDEV_MAPPED_QUALIFIER_CONTROL << 1)) {
		_tcscpy (p, _T("Ctrl [R]"));
	} else if (mask == IDEV_MAPPED_QUALIFIER_ALT) {
		_tcscpy (p, _T("Alt"));
	} else if (mask == (IDEV_MAPPED_QUALIFIER_ALT << 1)) {
		_tcscpy (p, _T("Alt [R]"));
	} else if (mask == IDEV_MAPPED_QUALIFIER_WIN) {
		_tcscpy (p, _T("Win"));
	} else if (mask == (IDEV_MAPPED_QUALIFIER_WIN << 1)) {
		_tcscpy (p, _T("Win [R]"));
	} else {
		int j;
		uae_u64 i;
		for (i = IDEV_MAPPED_QUALIFIER1, j = 0; i <= (IDEV_MAPPED_QUALIFIER8 << 1); i <<= 1, j++) {
			if (i == mask) {
				_stprintf (p, _T("%d%s"), j / 2 + 1, (j & 1) ? _T(" [R]") : _T(""));
			}
		}
	}
}

static int input_get_lv_index(HWND list, int index)
{
	LVFINDINFO plvfi = { 0 };
	plvfi.flags = LVFI_PARAM;
	plvfi.lParam = index;
	return ListView_FindItem(list, -1, &plvfi);
}

static void set_lventry_input (HWND list, int index)
{
	int i, sub, port;
	TCHAR name[256];
	TCHAR custom[MAX_DPATH];
	TCHAR af[32], toggle[32], invert[32];
	uae_u64 flags;
	int itemindex = input_get_lv_index(list, index);

	inputdevice_get_mapping (input_selected_device, index, &flags, &port, name, custom, input_selected_sub_num);
	if (flags & IDEV_MAPPED_AUTOFIRE_SET) {
		if (flags & IDEV_MAPPED_INVERTTOGGLE)
			WIN32GUI_LoadUIString (IDS_ON, af, sizeof af / sizeof (TCHAR));
		else 
			WIN32GUI_LoadUIString (IDS_YES, af, sizeof af / sizeof (TCHAR));
	} else if (flags & IDEV_MAPPED_AUTOFIRE_POSSIBLE) {
		WIN32GUI_LoadUIString (IDS_NO, af, sizeof af / sizeof (TCHAR));
	} else {
		_tcscpy (af, _T("-"));
	}
	if (flags & IDEV_MAPPED_TOGGLE) {
		WIN32GUI_LoadUIString (IDS_YES, toggle, sizeof toggle / sizeof (TCHAR));
	inputdevice_get_mapping(input_selected_device, index, &flags, &port, name, custom, input_selected_sub_num);
	} else if (flags & IDEV_MAPPED_AUTOFIRE_POSSIBLE)
		WIN32GUI_LoadUIString (IDS_NO, toggle, sizeof toggle / sizeof (TCHAR));
	else
		_tcscpy (toggle, _T("-"));
	if (port > 0) {
		TCHAR tmp[256];
		_tcscpy (tmp, name);
		_stprintf (name, _T("[PORT%d] %s"), port, tmp);
	}
	_tcscpy (invert, _T("-"));
	if (flags & IDEV_MAPPED_INVERT)
		WIN32GUI_LoadUIString (IDS_YES, invert, sizeof invert / sizeof (TCHAR));
	if (flags & IDEV_MAPPED_SET_ONOFF) {
		_tcscat (name, _T(" ("));
		TCHAR val[32];
		if ((flags & (IDEV_MAPPED_SET_ONOFF_VAL1 | IDEV_MAPPED_SET_ONOFF_VAL2)) == (IDEV_MAPPED_SET_ONOFF_VAL1 | IDEV_MAPPED_SET_ONOFF_VAL2)) {
			_tcscpy(val, _T("onoff"));
		} else if (flags & IDEV_MAPPED_SET_ONOFF_VAL2) {
			_tcscpy(val, _T("press"));
		} else if (flags & IDEV_MAPPED_SET_ONOFF_VAL1) {
			WIN32GUI_LoadUIString (IDS_ON, val, sizeof val / sizeof (TCHAR));
		} else {
			WIN32GUI_LoadUIString (IDS_OFF, val, sizeof val / sizeof (TCHAR));
		}
		_tcscat (name, val);
		_tcscat (name, _T(")"));
	}
	
	ListView_SetItemText (list, itemindex, 1, custom[0] ? custom : name);
	ListView_SetItemText (list, itemindex, 2, af);
	ListView_SetItemText (list, itemindex, 3, toggle);
	ListView_SetItemText (list, itemindex, 4, invert);
	_tcscpy (name, _T("-"));	
	if (flags & IDEV_MAPPED_QUALIFIER_MASK) {
		TCHAR *p;
		p = name;
		for (i = 0; i < MAX_INPUT_QUALIFIERS * 2; i++) {
			uae_u64 mask = IDEV_MAPPED_QUALIFIER1 << i;
			if (flags & mask) {
				if (p != name)
					*p++ = ',';
				getqualifiername (p, mask);
				p += _tcslen (p);
			}
		}
	}
	ListView_SetItemText (list, itemindex, 5, name);
	sub = 0;
	for (i = 0; i < MAX_INPUT_SUB_EVENT; i++) {
		if (inputdevice_get_mapping (input_selected_device, index, &flags, NULL, name, custom, i) || custom[0])
			sub++;
	}
	_stprintf (name, _T("%d"), sub);
	ListView_SetItemText (list, itemindex, 6, name);
}

static void update_listview_input (HWND hDlg)
{
	int i;
	if (!input_total_devices)
		return;
	for (i = 0; i < inputdevice_get_widget_num(input_selected_device); i++) {
		set_lventry_input(GetDlgItem(hDlg, IDC_INPUTLIST), i);
	}
}

static int inputmap_port = -1, inputmap_port_remap = -1, inputmap_port_sub = 0;
static int inputmap_groupindex[MAX_COMPA_INPUTLIST + 1];
static int inputmap_handle (HWND list, int currentdevnum, int currentwidgetnum,
	int *inputmap_portp, int *inputmap_indexp,
	int state, int *inputmap_itemindexp, int deleteindex, uae_u64 flags_or, uae_u64 flags_and, uae_u64 *inputmap_flagsp)
{
	int cntitem, cntgroup, portnum;
	int mode;
	const int *axistable;
	bool found2 = false;

	for (portnum = 0; portnum < 4; portnum++) {
		if (list)
			portnum = inputmap_port;
		cntitem = 1;
		cntgroup = 1;
		int events[MAX_COMPA_INPUTLIST];
		if (inputdevice_get_compatibility_input (&workprefs, portnum, &mode, events, &axistable) > 0) {
			int evtnum;
			for (int i = 0; (evtnum = events[i]) >= 0; i++) {
				const struct inputevent *evt = inputdevice_get_eventinfo (evtnum);
				LV_ITEM lvstruct = { 0 };
				int devnum;
				int status;
				TCHAR name[256];
				const int *atp = axistable;
				int atpidx;
				int item;
				bool found = false;
				uae_u64 flags;

				if (list) {
					LVGROUP group = { 0 };
					group.cbSize = sizeof (LVGROUP);
					group.mask = LVGF_HEADER | LVGF_GROUPID;
					group.pszHeader = (TCHAR*)evt->name;
					group.iGroupId = cntgroup;
					ListView_InsertGroup (list, -1, &group);

					lvstruct.mask     = LVIF_TEXT | LVIF_PARAM | LVIF_GROUPID;
					lvstruct.lParam   = 0;
					lvstruct.iSubItem = 0;
					lvstruct.iGroupId = cntgroup;
					if (inputmap_itemindexp) {
						inputmap_itemindexp[cntgroup - 1] = -1;
						inputmap_itemindexp[cntgroup - 1 + 1] = -1;
					}
				}

				atpidx = 0;
				while (*atp >= 0) {
					if (*atp == evtnum) {
						atp++;
						atpidx = 2;
						break;
					}
					if (atp[1] == evtnum || atp[2] == evtnum) {
						atpidx = 1;
						break;
					}
					atp += 3;
				}
				while (atpidx >= 0) {
					devnum = 0;
					while ((status = inputdevice_get_device_status (devnum)) >= 0) {
						if ((1 || status)) {
							for (int j = 0; j < inputdevice_get_widget_num (devnum); j++) {
								for (int sub = 0; sub < MAX_INPUT_SUB_EVENT; sub++) {
									int port;
									int evtnum2 = inputdevice_get_mapping (devnum, j, &flags, &port, name, NULL, sub);
									if (evtnum2 == evtnum) {
										if (port - 1 != portnum)
											continue;
										if (cntitem - 1 == deleteindex) {
											if (!flags_or && !flags_and && !inputmap_flagsp) {
												inputdevice_set_mapping (devnum, j, NULL, NULL, 0, 0, sub);
												deleteindex = -1;
												found = true;
												continue;
											} else {
												if (flags_or || flags_and) {
													flags &= ~flags_and;
													flags |= flags_or;
													inputdevice_set_mapping(devnum, j, name, NULL, flags, port, sub);
												} else {
													*inputmap_flagsp = flags;
												}
												found = true;
												found2 = true;
											}
										}
										if (list) {
											inputdevice_get_widget_type (devnum, j, name, false);
											TCHAR target[MAX_DPATH];
											_tcscpy (target, name);
											_tcscat (target, _T(", "));
											_tcscat (target, inputdevice_get_device_name2 (devnum));

											if (flags & IDEV_MAPPED_AUTOFIRE_SET) {
												_tcscat(target, _T(" ["));
												if (flags & IDEV_MAPPED_TOGGLE)
													_tcscat(target, remapcustoms[2].name);
												else if (flags & IDEV_MAPPED_INVERTTOGGLE)
													_tcscat(target, remapcustoms[3].name);
												else if (flags & IDEV_MAPPED_INVERT)
													_tcscat(target, remapcustoms[4].name);
												else
													_tcscat(target, remapcustoms[1].name);
												_tcscat(target, _T("]"));
											}

											lvstruct.pszText = target;
											lvstruct.iItem = cntgroup * 256 + cntitem;
											item = ListView_InsertItem(list, &lvstruct);

											if (inputmap_itemindexp && inputmap_itemindexp[cntgroup - 1] < 0)
												inputmap_itemindexp[cntgroup - 1] = item;
										} else if (currentdevnum == devnum && currentwidgetnum == j) {
											if (inputmap_portp)
												*inputmap_portp = portnum;
											if (inputmap_indexp)
												*inputmap_indexp = cntitem - 1;
											found2 = true;
											if (state < 0)
												return 1;
											state = -1;
										}
										cntitem++;
										found = true;
									}
								}
							}
						}
						devnum++;
					}
					evtnum = *atp++;
					atpidx--;
				}
				if (!found) {
					if (list) {
						lvstruct.pszText = _T("");
						lvstruct.iItem = cntgroup * 256 + cntitem;
						lvstruct.lParam = cntgroup;
						item = ListView_InsertItem (list, &lvstruct);
						if (inputmap_itemindexp && inputmap_itemindexp[cntgroup - 1] < 0)
							inputmap_itemindexp[cntgroup - 1] = item;
					}
					cntitem++;
				}
				cntgroup++;
			}
		}
		if (list)
			break;
	}
	if (found2)
		return 1;
	return 0;
}
static void update_listview_inputmap (HWND hDlg, int deleteindex)
{
	HWND list = GetDlgItem (hDlg, IDC_INPUTMAPLIST);

	ListView_EnableGroupView (list, TRUE);

	inputmap_handle (list, -1, -1, NULL, NULL, 0, inputmap_groupindex, deleteindex, 0, 0, NULL);
}

static int clicked_entry = -1;

#define LOADSAVE_COLUMNS 2
#define INPUT_COLUMNS 7
#define HARDDISK_COLUMNS 8
#define DISK_COLUMNS 3
#define MISC2_COLUMNS 2
#define INPUTMAP_COLUMNS 1
#define MISC1_COLUMNS 1
#define MAX_COLUMN_HEADING_WIDTH 20
#define CD_COLUMNS 3
#define BOARD_COLUMNS 6

#define LV_LOADSAVE 1
#define LV_HARDDISK 2
#define LV_INPUT 3
#define LV_DISK 4
#define LV_MISC2 5
#define LV_INPUTMAP 6
#define LV_MISC1 7
#define LV_CD 8
#define LV_BOARD 9
#define LV_MAX 10

static int lv_oldidx[LV_MAX];
static int lv_old_type = -1;

static int listview_num_columns;

struct miscentry
{
	int type;
	int canactive;
	const TCHAR *name;
	bool *b;
	int *i;
	int ival, imask;
};

static bool win32_middle_mouse_obsolete;

static const struct miscentry misclist[] = { 
	{ 0, 1, _T("Untrap = middle button"),  &win32_middle_mouse_obsolete },
	{ 0, 0, _T("Show GUI on startup"), &workprefs.start_gui },
	{ 0, 1, _T("Use CTRL-F11 to quit"), &workprefs.win32_ctrl_F11_is_quit },
	{ 0, 1, _T("Don't show taskbar button"), &workprefs.win32_notaskbarbutton },
	{ 0, 1, _T("Don't show notification icon"), &workprefs.win32_nonotificationicon },
	{ 0, 1, _T("Emulator window always on top"), &workprefs.win32_main_alwaysontop },
	{ 0, 1, _T("GUI window always on top"), &workprefs.win32_gui_alwaysontop },
	{ 0, 1, _T("Disable screensaver"), &workprefs.win32_powersavedisabled },
	{ 0, 0, _T("Synchronize clock"), &workprefs.tod_hack },
	{ 0, 1, _T("One second reboot pause"), &workprefs.reset_delay },
	{ 0, 1, _T("Faster RTG"), &workprefs.picasso96_nocustom },
	{ 0, 0, _T("Clipboard sharing"), &workprefs.clipboard_sharing },
	{ 0, 1, _T("Allow native code"), &workprefs.native_code },
	{ 0, 1, _T("Native on-screen display"), NULL, &workprefs.leds_on_screen, STATUSLINE_CHIPSET, STATUSLINE_CHIPSET },
	{ 0, 1, _T("RTG on-screen display"), NULL, &workprefs.leds_on_screen, STATUSLINE_RTG, STATUSLINE_RTG },
	{ 0, 0, _T("Create winuaelog.txt log"), &workprefs.win32_logfile },
	{ 0, 1, _T("Log illegal memory accesses"), &workprefs.illegal_mem },
	{ 0, 0, _T("Blank unused displays"), &workprefs.win32_blankmonitors },
	{ 0, 0, _T("Start mouse uncaptured"), &workprefs.win32_start_uncaptured  },
	{ 0, 0, _T("Start minimized"), &workprefs.win32_start_minimized  },
	{ 0, 1, _T("Minimize when focus is lost"), &workprefs.win32_minimize_inactive },
	{ 0, 1, _T("100/120Hz VSync black frame insertion"), &workprefs.lightboost_strobo },
	{ 0, 0, _T("Master floppy write protection"), &workprefs.floppy_read_only },
	{ 0, 0, _T("Master harddrive write protection"), &workprefs.harddrive_read_only },
	{ 0, 0, _T("Hide all UAE autoconfig boards"), &workprefs.uae_hide_autoconfig },
	{ 0, 1, _T("Right Control = Right Windows key"), &workprefs.right_control_is_right_win_key },
	{ 0, 0, _T("Windows shutdown/logoff notification"), &workprefs.win32_shutdown_notification },
	{ 0, 1, _T("Warn when attempting to close window"), &workprefs.win32_warn_exit },
	{ 0, 1, _T("Power led dims when audio filter is disabled"), NULL, &workprefs.power_led_dim, 128, 255 },
	{ 0, 1, _T("Automatically capture mouse when window is activated"), &workprefs.win32_capture_always },
	{ 0, 0, _T("Debug memory space"), &workprefs.debug_mem },
	{ 0, 1, _T("Force hard reset if CPU halted"), &workprefs.crash_auto_reset },
	{ 0, 0, _T("A600/A1200/A4000 IDE scsi.device disable"), &workprefs.scsidevicedisable },
	{ 0, 1, _T("Warp mode reset"), &workprefs.turbo_boot },
	{ 0, 1, _T("GUI game pad control"), &workprefs.win32_gui_control },
	{ 0, 1, _T("Default on screen keyboard (Pad button 4)"), &workprefs.input_default_onscreen_keyboard },
	{ 0, 0, NULL }
};

static void harddisktype (TCHAR *s, struct uaedev_config_info *ci)
{
	switch (ci->type)
	{
		case UAEDEV_CD:
		_tcscpy (s, _T("CD"));
		break;
		case UAEDEV_TAPE:
		_tcscpy (s, _T("TAPE"));
		break;
		case UAEDEV_HDF:
		_tcscpy (s, _T("HDF"));
		break;
		default:
		_tcscpy (s, _T("n/a"));
		break;
	}
}

#define MAX_LISTVIEW_COLUMNS 16
static int listview_id;
static int listview_type;
static int listview_columns;
static int listview_sortdir, listview_sortcolumn;
static int listview_column_widths[MAX_LISTVIEW_COLUMNS];

struct lvsort
{
	TCHAR **names;
	int max;
	int sortcolumn;
	int sortdir;
};

static int CALLBACK lvsortcompare(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
{
	struct lvsort *lvs = (struct lvsort*)lParamSort;
	if (lParam1 >= lvs->max || lParam2 >= lvs->max)
		return -1;
	TCHAR *s1, *s2;
	if (lvs->sortdir) {
		s2 = lvs->names[lParam1];
		s1 = lvs->names[lParam2];
	} else {
		s1 = lvs->names[lParam1];
		s2 = lvs->names[lParam2];

	}
	return _tcsicmp(s1, s2);
}

static void SortListView(HWND list, int sortcolumn, int dir)
{
	int cnt;
	struct lvsort lvs;
	TCHAR **names;
	TCHAR buf[256];

	cnt = ListView_GetItemCount(list);
	names = xmalloc(TCHAR*, cnt);
	for (int i = 0; i < cnt; i++) {
		LVITEM item = { 0 };
		item.iItem = i;
		item.iSubItem = sortcolumn;
		item.mask = LVIF_TEXT;
		item.pszText = buf;
		item.cchTextMax = sizeof(buf) / sizeof(TCHAR);
		ListView_GetItem(list, &item);
		names[i] = my_strdup(item.pszText);
	}
	lvs.sortcolumn = sortcolumn;
	lvs.max = cnt;
	lvs.sortdir = dir;
	lvs.names = names;
	ListView_SortItemsEx(list, lvsortcompare, &lvs);
	for (int i = 0; i < cnt; i++) {
		xfree(names[i]);
	}
	xfree(names);
	ListView_SetSelectedColumn(list, sortcolumn);
}

static void SaveListView(HWND hDlg, bool force)
{
	HWND list;
	TCHAR name[200];
	TCHAR data[256];
	int columns[100];
	int sortcolumn;
	UAEREG *fkey;

	if (listview_id <= 0)
		return;
	list = GetDlgItem(hDlg, listview_id);
	if (!list)
		return;
	fkey = regcreatetree(NULL, _T("ListViews"));
	if (!fkey)
		return;
	_stprintf(name, _T("LV_%d"), listview_type);
	if (!force) {
		bool modified = false;
		for (int i = 0; i < listview_num_columns; i++) {
			int w = ListView_GetColumnWidth(list, i);
			if (listview_column_widths[i] != w) {
				modified = true;
			}
		}
		if (!regexists(fkey, name) && !modified)
			return;
	}
	_tcscpy(data, _T("1,"));
	ListView_GetColumnOrderArray(list, listview_columns, columns);
	sortcolumn = ListView_GetSelectedColumn(list);
	for (int i = 0; i < listview_columns; i++) {
		int w = ListView_GetColumnWidth(list, i);
		if (i > 0)
			_tcscat(data, _T(","));
		_stprintf(data + _tcslen(data), _T("%d:%d"), columns[i], w);
		if (sortcolumn == columns[i]) {
			_tcscat(data, listview_sortdir ? _T(":D") : _T(":A"));
		}
	}
	regsetstr(fkey, name, data);

	regclosetree(fkey);
}

static bool LoadListView(HWND list)
{
	TCHAR name[200];
	TCHAR data[256];
	int columns[100], columnorders[100];
	int size;
	UAEREG *fkey;
	bool err = true;
	int sortindex = 0;

	listview_sortcolumn = -1;

	fkey = regcreatetree(NULL, _T("ListViews"));
	if (!fkey)
		return false;

	_stprintf(name, _T("LV_%d"), listview_type);
	size = sizeof(data) / sizeof(TCHAR) - 1;
	if (regquerystr(fkey, name, data, &size)) {
		_tcscat(data, _T(","));
		TCHAR *p1 = data;
		int idx = -1;
		for (;;) {
			TCHAR *p2 = _tcschr(p1, ',');
			if (!p2) {
				if (idx == listview_columns)
					err = false;
				break;
			}
			*p2++ = 0;
			int v = _tstol(p1);
			if (idx < 0) {
				if (v != 1) {
					break;
				}
			} else {
				TCHAR *p3 = _tcschr(p1, ':');
				if (!p3)
					break;
				*p3++ = 0;
				if (v < 0 || v >= listview_columns)
					break;
				int w = _tstol(p3);
				if (w < 1 || w >= 4096)
					break;
				columnorders[idx] = v;
				columns[idx] = w;
				p3 = _tcschr(p3, ':');
				if (p3) {
					p3++;
					if (*p3 == 'A') {
						sortindex = 1 + v;
					} else if (*p3 == 'D') {
						sortindex = -1 - v;
					}
				}
			}
			idx++;
			if (idx > listview_columns) {
				break;
			}
			p1 = p2;
		}
		if (!err) {
			for (int i = 0; i < listview_columns; i++) {
				ListView_SetColumnWidth(list, i, columns[i]);
			}
			if (columnorders) {
				ListView_SetColumnOrderArray(list, listview_columns, columnorders);
			}
			if (sortindex && listview_sortdir >= 0) {
				SortListView(list, abs(sortindex) - 1, sortindex < 0);
				listview_sortdir = sortindex < 0;
			}
		}
	}
	regclosetree(fkey);
	return !err;
}

static void ColumnClickListView(HWND hDlg, NM_LISTVIEW *lv)
{
	LPNMLISTVIEW pnmv = (LPNMLISTVIEW)lv;
	HWND list = pnmv->hdr.hwndFrom;
	int column = pnmv->iSubItem;
	int sortcolumn = ListView_GetSelectedColumn(list);

	if (sortcolumn < 0 || sortcolumn != listview_sortcolumn || listview_sortdir < 0) {
		listview_sortdir = 0;
	} else {
		listview_sortdir++;
	}
	listview_sortcolumn = column;
	if (listview_sortdir > 1) {
		listview_sortdir = -1;
		ListView_SetSelectedColumn(list, -1);
		InitializeListView(hDlg);
	} else {
		SortListView(list, column, listview_sortdir);
		SaveListView(hDlg, true);
	}
}

static void ResetListViews(void)
{
	UAEREG *fkey = regcreatetree(NULL, _T("ListViews"));
	if (!fkey)
		return;
	for (int i = 0; i < LV_MAX; i++) {
		TCHAR name[256];
		_stprintf(name, _T("LV_%d"), i);
		regdelete(fkey, name);
	}
	regclosetree(fkey);
}

static void InitializeListView (HWND hDlg)
{
	int lv_type;
	HWND list;
	LV_ITEM lvstruct;
	LV_COLUMN lvcolumn;
	RECT rect;
	TCHAR column_heading[HARDDISK_COLUMNS][MAX_COLUMN_HEADING_WIDTH];
	TCHAR blocksize_str[6] = _T("");
	TCHAR readwrite_str[10] = _T("");
	TCHAR size_str[32] = _T("");
	TCHAR volname_str[MAX_DPATH] = _T("");
	TCHAR devname_str[MAX_DPATH] = _T("");
	TCHAR bootpri_str[6] = _T("");
	int width = 0;
	int items = 0, result = 0, i, j, entry = 0, temp = 0;
	TCHAR tmp[10], tmp2[MAX_DPATH];
	int listview_column_width[HARDDISK_COLUMNS];
	DWORD extraflags = 0;
	int listpadding;
	int dpi = getdpiforwindow(hDlg);

	if (cachedlist) {
		if (lv_old_type >= 0) {
			lv_oldidx[lv_old_type] = ListView_GetTopIndex (cachedlist);
			lv_oldidx[lv_old_type] += ListView_GetCountPerPage (cachedlist) - 1;
		}
		cachedlist = NULL;
	}

	if (hDlg == pages[BOARD_ID]) {
		listview_id = IDC_BOARDLIST;
		listview_num_columns = BOARD_COLUMNS;;
		lv_type = LV_BOARD;
		WIN32GUI_LoadUIString(IDS_BOARDTYPE, column_heading[0], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString(IDS_BOARDNAME, column_heading[1], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString(IDS_BOARDSTART, column_heading[2], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString(IDS_BOARDEND, column_heading[3], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString(IDS_BOARDSIZE, column_heading[4], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString(IDS_BOARDID, column_heading[5], MAX_COLUMN_HEADING_WIDTH);

	} else if (hDlg == pages[HARDDISK_ID]) {

		listview_id = IDC_VOLUMELIST;
		listview_num_columns = HARDDISK_COLUMNS;
		lv_type = LV_HARDDISK;
		_tcscpy (column_heading[0], _T("*"));
		WIN32GUI_LoadUIString (IDS_DEVICE, column_heading[1], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString (IDS_VOLUME, column_heading[2], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString (IDS_PATH, column_heading[3], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString (IDS_RW, column_heading[4], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString (IDS_BLOCKSIZE, column_heading[5], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString (IDS_HFDSIZE, column_heading[6], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString (IDS_BOOTPRI, column_heading[7], MAX_COLUMN_HEADING_WIDTH);

	} else if (hDlg == pages[INPUT_ID]) {

		listview_id = IDC_INPUTLIST;
		listview_num_columns = INPUT_COLUMNS;
		lv_type = LV_INPUT;
		WIN32GUI_LoadUIString (IDS_INPUTHOSTWIDGET, column_heading[0], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString (IDS_INPUTAMIGAEVENT, column_heading[1], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString (IDS_INPUTAUTOFIRE, column_heading[2], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString(IDS_INPUTTOGGLE, column_heading[3], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString(IDS_INPUTINVERT, column_heading[4], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString (IDS_INPUTQUALIFIER, column_heading[5], MAX_COLUMN_HEADING_WIDTH);
		_tcscpy (column_heading[6], _T("#"));

	} else if (hDlg == pages[INPUTMAP_ID]) {

		listview_id = IDC_INPUTMAPLIST;
		listview_num_columns = INPUTMAP_COLUMNS;
		lv_type = LV_INPUTMAP;
		column_heading[0][0] = 0;

	} else if (hDlg == pages[MISC2_ID]) {

		listview_id = IDC_ASSOCIATELIST;
		listview_num_columns = MISC2_COLUMNS;
		lv_type = LV_MISC2;
		WIN32GUI_LoadUIString(IDS_ASSOCIATEEXTENSION, column_heading[0], MAX_COLUMN_HEADING_WIDTH);
		_tcscpy (column_heading[1], _T(""));

	} else if (hDlg == pages[MISC1_ID]) {

		listview_id = IDC_MISCLIST;
		listview_num_columns = MISC1_COLUMNS;
		lv_type = LV_MISC1;
		column_heading[0][0] = 0;
		extraflags = LVS_EX_CHECKBOXES;

	} else if (hDlg == pages[DISK_ID]) {

		listview_id = IDC_DISK;
		listview_num_columns = DISK_COLUMNS;
		lv_type = LV_DISK;
		_tcscpy (column_heading[0], _T("#"));
		WIN32GUI_LoadUIString (IDS_DISK_IMAGENAME, column_heading[1], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString (IDS_DISK_DRIVENAME, column_heading[2], MAX_COLUMN_HEADING_WIDTH);

	} else {
		// CD dialog
		listview_id = IDC_CDLIST;
		listview_num_columns = CD_COLUMNS;
		lv_type = LV_CD;
		_tcscpy (column_heading[0], _T("*"));
		WIN32GUI_LoadUIString (IDS_DEVICE, column_heading[1], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString (IDS_PATH, column_heading[2], MAX_COLUMN_HEADING_WIDTH);

	}

	list = GetDlgItem(hDlg, listview_id);
	SubclassListViewControl(list);
	listview_type = lv_type;
	listview_columns = listview_num_columns;

	SetWindowRedraw(list, FALSE);

	scalaresource_listview_font_info(&listpadding);
	listpadding *= 2;
	int flags = LVS_EX_DOUBLEBUFFER | extraflags | LVS_EX_HEADERDRAGDROP;
	if (lv_type != LV_MISC1)
		flags |= LVS_EX_ONECLICKACTIVATE | LVS_EX_UNDERLINEHOT | LVS_EX_FULLROWSELECT;
	ListView_SetExtendedListViewStyleEx (list, flags , flags);
	ListView_RemoveAllGroups (list);
	ListView_DeleteAllItems (list);

	cachedlist = list;

	for(i = 0; i < listview_num_columns; i++)
		listview_column_width[i] = MulDiv(ListView_GetStringWidth(list, column_heading[i]), dpi, 72) + listpadding;

	// If there are no columns, then insert some
	lvcolumn.mask = LVCF_WIDTH;
	if (ListView_GetColumn (list, 1, &lvcolumn) == FALSE) {
		for(i = 0; i < listview_num_columns; i++) {
			lvcolumn.mask     = LVCF_FMT | LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
			lvcolumn.iSubItem = i;
			lvcolumn.fmt      = LVCFMT_LEFT;
			lvcolumn.pszText  = column_heading[i];
			lvcolumn.cx       = listview_column_width[i];
			ListView_InsertColumn (list, i, &lvcolumn);
		}
	}

	if (lv_type == LV_BOARD) {

		listview_column_width[0] = MulDiv(40, dpi, 72);
		listview_column_width[1] = MulDiv(200, dpi, 72);
		listview_column_width[2] = MulDiv(90, dpi, 72);
		listview_column_width[3] = MulDiv(90, dpi, 72);
		listview_column_width[4] = MulDiv(90, dpi, 72);
		listview_column_width[5] = MulDiv(90, dpi, 72);
		i = 0;
		if (full_property_sheet)
			expansion_generate_autoconfig_info(&workprefs);
		uaecptr highest_expamem = 0;
		uaecptr addr = 0;
		for (;;) {
			TCHAR tmp[200];
			struct autoconfig_info *aci = NULL;
			if (full_property_sheet) {
				aci = expansion_get_autoconfig_data(&workprefs, i);
			} else {
				aci = expansion_get_bank_data(&currprefs, &addr);
			}
			if (aci) {
				if (aci->zorro == 3 && aci->size != 0 && aci->start + aci->size > highest_expamem)
					highest_expamem = aci->start + aci->size;
			}
			if (!aci && highest_expamem <= Z3BASE_UAE)
				break;
			if (aci && aci->zorro >= 1 && aci->zorro <= 3)
				_stprintf(tmp, _T("Z%d"), aci->zorro);
			else
				_tcscpy(tmp, _T("-"));
			lvstruct.mask = LVIF_TEXT | LVIF_PARAM;
			lvstruct.pszText = tmp;
			lvstruct.lParam = 0;
			if (aci) {
				// movable
				if (expansion_can_move(&workprefs, i))
					lvstruct.lParam |= 1;
				// outside or crosses 2G "border"
				if (aci->zorro == 3 && (aci->start + aci->size > 0x80000000 || aci->start + aci->size < aci->start))
					lvstruct.lParam |= 2;
				// outside or crosses 4G "border"
				if (aci->zorro == 3 && aci->start == 0xffffffff)
					lvstruct.lParam |= 4;
				if (!full_property_sheet && (aci->zorro == 2 || aci->zorro == 3) && aci->addrbank && (aci->addrbank->flags & ABFLAG_RAM) && aci->addrbank->reserved_size) {
					// failed to allocate
					if (aci->addrbank->allocated_size == 0)
						lvstruct.lParam |= 8;
					// outside of JIT direct range
					else if (canbang && (aci->addrbank->flags & ABFLAG_ALLOCINDIRECT))
						lvstruct.lParam |= 16;
				}
				if (aci->rc && aci->ert && (aci->ert->deviceflags & EXPANSIONTYPE_PCMCIA) && !aci->rc->inserted) {
					lvstruct.lParam |= 2;
				}
			}
			lvstruct.iItem = i;
			lvstruct.iSubItem = 0;
			result = ListView_InsertItem(list, &lvstruct);
			tmp[0] = 0;
			TCHAR *s = tmp;
			if (full_property_sheet) {
				if (aci && aci->parent_of_previous) {
					_tcscat(s, _T(" - "));
				}
				if (aci && (aci->parent_address_space || aci->parent_romtype) && !aci->parent_of_previous) {
					_tcscat(s, _T("? "));
				}
			}
			if (aci && aci->name) {
				_tcscat(s, aci->name);
			}
			ListView_SetItemText(list, result, 1, tmp);
			if (aci) {
				if (aci->start != 0xffffffff)
					_stprintf(tmp, _T("0x%08x"), aci->start);
				else
					_tcscpy(tmp, _T("-"));
				ListView_SetItemText(list, result, 2, tmp);
				if (aci->size != 0)
					_stprintf(tmp, _T("0x%08x"), aci->start + aci->size - 1);
				else
					_tcscpy(tmp, _T("-"));
				ListView_SetItemText(list, result, 3, tmp);
				if (aci->size != 0)
					_stprintf(tmp, _T("0x%08x"), aci->size);
				else
					_tcscpy(tmp, _T("-"));
				ListView_SetItemText(list, result, 4, tmp);
				if (aci->autoconfig_bytes[0] != 0xff)
					_stprintf(tmp, _T("0x%04x/0x%02x"),
					(aci->autoconfig_bytes[4] << 8) | aci->autoconfig_bytes[5], aci->autoconfig_bytes[1]);
				else
					_tcscpy(tmp, _T("-"));
				ListView_SetItemText(list, result, 5, tmp);
			} else if (full_property_sheet) {
				_stprintf(tmp, _T("0x%08x"), highest_expamem);
				ListView_SetItemText(list, result, 2, tmp);
			}
			i++;
			if (!aci)
				break;
		}


	} else if (lv_type == LV_MISC2) {

		listview_column_width[0] = MulDiv(180, dpi, 72);
		listview_column_width[1] = MulDiv(10, dpi, 72);
		for (i = 0; exts[i].ext; i++) {
			lvstruct.mask     = LVIF_TEXT | LVIF_PARAM;
			lvstruct.pszText  = exts[i].ext;
			lvstruct.lParam   = 0;
			lvstruct.iItem    = i;
			lvstruct.iSubItem = 0;
			result = ListView_InsertItem (list, &lvstruct);
			ListView_SetItemText (list, result, 1, exts[i].enabled ? _T("*") : _T(""));
		}

	} else if (lv_type == LV_INPUT) {

		for (i = 0; input_total_devices && i < inputdevice_get_widget_num (input_selected_device); i++) {
			TCHAR name[100];
			inputdevice_get_widget_type (input_selected_device, i, name, true);
			lvstruct.mask     = LVIF_TEXT | LVIF_PARAM;
			lvstruct.pszText  = name;
			lvstruct.lParam   = i;
			lvstruct.iItem    = i;
			lvstruct.iSubItem = 0;
			result = ListView_InsertItem (list, &lvstruct);
			width = MulDiv(ListView_GetStringWidth (list, lvstruct.pszText), dpi, 72) + listpadding;
			if (width > listview_column_width[0])
				listview_column_width[0] = width;
			entry++;
		}
		listview_column_width[1] = MulDiv(260, dpi, 72);
		listview_column_width[2] = MulDiv(65, dpi, 72);
		listview_column_width[3] = MulDiv(65, dpi, 72);
		listview_column_width[4] = MulDiv(65, dpi, 72);
		listview_column_width[5] = MulDiv(65, dpi, 72);
		listview_column_width[6] = MulDiv(30, dpi, 72);
		update_listview_input (hDlg);

	} else if (lv_type == LV_INPUTMAP) {

		listview_column_width[0] = MulDiv(400, dpi, 72);
		update_listview_inputmap (hDlg, -1);

	} else if (lv_type == LV_MISC1) {

		int itemids[] = { IDS_MISCLISTITEMS1, IDS_MISCLISTITEMS2, IDS_MISCLISTITEMS3, IDS_MISCLISTITEMS4, IDS_MISCLISTITEMS5, IDS_MISCLISTITEMS6, -1 };
		int itemoffset = 0;
		int itemcnt = 0;
		listview_column_width[0] = MulDiv(150, dpi, 72);
		for (i = 0; misclist[i].name; i++) {
			TCHAR tmpentry[MAX_DPATH], itemname[MAX_DPATH];
			const struct miscentry *me = &misclist[i];
			int type = me->type;
			bool checked = false;

			win32_middle_mouse_obsolete = (workprefs.input_mouse_untrap & MOUSEUNTRAP_MIDDLEBUTTON) != 0;

			if (me->b) {
				checked = *me->b;
			} else if (me->i) {
				checked = ((*me->i) & me->imask) != 0;
			}
			_tcscpy (itemname, me->name);

			for (;;) {
				if (itemids[itemcnt] < 0)
					break;
				WIN32GUI_LoadUIString (itemids[itemcnt], tmpentry, sizeof tmpentry / sizeof (TCHAR));
				TCHAR *p = tmpentry;
				for (int j = 0; j < itemoffset; j++) {
					p = _tcschr (p, '\n');
					if (!p || p[1] == 0) {
						p = NULL;
						itemoffset = 0;
						itemcnt++;
						break;
					}
					p++;
				}
				if (!p)
					continue;
				TCHAR *p2 = _tcschr (p, '\n');
				if (p2) {
					*p2 = 0;
					_tcscpy (itemname, p);
				}
				itemoffset++;
				break;
			}

			lvstruct.mask     = LVIF_TEXT | LVIF_PARAM;
			lvstruct.pszText  = itemname;
			lvstruct.lParam   = 0;
			lvstruct.iItem    = i;
			lvstruct.iSubItem = 0;
			result = ListView_InsertItem (list, &lvstruct);
			ListView_SetItemState (list, i, INDEXTOSTATEIMAGEMASK(type ? 0 : (checked ? 2 : 1)), LVIS_STATEIMAGEMASK);
			width = MulDiv(ListView_GetStringWidth(list, lvstruct.pszText), dpi, 72) + listpadding;
			if (width > listview_column_width[0])
				listview_column_width[0] = width;
			entry++;
		}

	} else if (lv_type == LV_DISK) {

		for (i = 0; i < MAX_SPARE_DRIVES; i++) {
			int drv;
			_stprintf (tmp, _T("%d"), i + 1);
			lvstruct.mask     = LVIF_TEXT | LVIF_PARAM;
			lvstruct.pszText  = tmp;
			lvstruct.lParam   = 0;
			lvstruct.iItem    = i;
			lvstruct.iSubItem = 0;
			result = ListView_InsertItem (list, &lvstruct);
			_tcscpy (tmp2, workprefs.dfxlist[i]);
			j = uaetcslen(tmp2) - 1;
			if (j < 0)
				j = 0;
			while (j > 0) {
				if ((tmp2[j - 1] == '\\' || tmp2[j - 1] == '/')) {
					if (!(j >= 5 && (tmp2[j - 5] == '.' || tmp2[j - 4] == '.')))
						break;
				}
				j--;
			}
			ListView_SetItemText (list, result, 1, tmp2 + j);
			drv = disk_in_drive (i);
			tmp[0] = 0;
			if (drv >= 0)
				_stprintf (tmp, _T("DF%d:"), drv);
			ListView_SetItemText (list, result, 2, tmp);
			width = MulDiv(ListView_GetStringWidth(list, lvstruct.pszText), dpi, 72) + listpadding;
			if (width > listview_column_width[0])
				listview_column_width[0] = width;
			entry++;
		}
		listview_column_width[0] = MulDiv(30, dpi, 72);
		listview_column_width[1] = MulDiv(336, dpi, 72);
		listview_column_width[2] = MulDiv(50, dpi, 72);

	} else if (lv_type == LV_CD) {

		listview_column_width[2] = MulDiv(450, dpi, 72);
		for (i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
			TCHAR tmp[10];
			struct device_info di = { 0 };
			struct cdslot *cds = &workprefs.cdslots[i];			
			
			if (cds->inuse)
				blkdev_get_info (&workprefs, i, &di);
			_stprintf (tmp, _T("%d"), i);
			lvstruct.mask     = LVIF_TEXT | LVIF_PARAM;
			lvstruct.pszText  = cds->inuse ? (di.media_inserted ? _T("*") : _T("E")) : _T("-");
			lvstruct.lParam   = 0;
			lvstruct.iItem    = i;
			lvstruct.iSubItem = 0;
			result = ListView_InsertItem (list, &lvstruct);
			ListView_SetItemText(list, result, 1, tmp);
			ListView_SetItemText(list, result, 2, cds->name);
			width = MulDiv(ListView_GetStringWidth(list, cds->name), dpi, 72) + listpadding;
			if (width > listview_column_width[2])
				listview_column_width[2] = width;
			break;
		}

	} else if (lv_type == LV_HARDDISK) {
#ifdef FILESYS
		listview_column_width[1] = MulDiv(80, dpi, 72);
		for (i = 0; i < workprefs.mountitems; i++)
		{
			struct uaedev_config_data *uci = &workprefs.mountconfig[i];
			struct uaedev_config_info *ci = &uci->ci;
			int nosize = 0, type, ctype;
			struct mountedinfo mi;
			TCHAR *rootdir, *rootdirp;

			type = get_filesys_unitconfig (&workprefs, i, &mi);
			if (type < 0) {
				type = ci->type == UAEDEV_HDF || ci->type == UAEDEV_CD || ci->type == UAEDEV_TAPE ? FILESYS_HARDFILE : FILESYS_VIRTUAL;
				nosize = 1;
			}
			if (mi.size < 0)
				nosize = 1;
			rootdir = my_strdup (mi.rootdir);
			rootdirp = rootdir;
			if (!_tcsncmp (rootdirp, _T("HD_"), 3))
				rootdirp += 3;
			if (rootdirp[0] == ':') {
				rootdirp++;
				TCHAR *p = _tcschr (rootdirp, ':');
				if (p)
					*p = 0;
			}

			if (nosize)
				_tcscpy (size_str, _T("n/a"));
			else if (mi.size >= 1024 * 1024 * 1024)
				_stprintf (size_str, _T("%.1fG"), ((double)(uae_u32)(mi.size / (1024 * 1024))) / 1024.0);
			else if (mi.size < 10 * 1024 * 1024)
				_stprintf (size_str, _T("%lldK"), mi.size / 1024);
			else
				_stprintf (size_str, _T("%.1fM"), ((double)(uae_u32)(mi.size / (1024))) / 1024.0);

			ctype = ci->controller_type;
			if (ctype >= HD_CONTROLLER_TYPE_IDE_FIRST && ctype <= HD_CONTROLLER_TYPE_IDE_LAST) {
				const struct expansionromtype *ert = get_unit_expansion_rom(ctype);
				const TCHAR *idedevs[] = {
					_T("IDE:%d"),
					_T("A600/A1200/A4000:%d"),
				};
				_stprintf (blocksize_str, _T("%d"), ci->blocksize);
				if (ert) {
					if (ci->controller_type_unit == 0)
						_stprintf (devname_str, _T("%s:%d"), ert->friendlyname, ci->controller_unit);
					else
						_stprintf (devname_str, _T("%s:%d/%d"), ert->friendlyname, ci->controller_unit, ci->controller_type_unit + 1);
				} else {
					_stprintf (devname_str, idedevs[ctype - HD_CONTROLLER_TYPE_IDE_FIRST], ci->controller_unit);
				}
				harddisktype (volname_str, ci);
				_tcscpy (bootpri_str, _T("n/a"));
			} else if (ctype >= HD_CONTROLLER_TYPE_SCSI_FIRST && ctype <= HD_CONTROLLER_TYPE_SCSI_LAST) {
				TCHAR sid[8];
				const struct expansionromtype *ert = get_unit_expansion_rom(ctype);
				const TCHAR *scsidevs[] = {
					_T("SCSI:%s"),
					_T("A3000:%s"),
					_T("A4000T:%s"),
					_T("CDTV:%s"),
				};
				if (ci->controller_unit == 8 && ert && !_tcscmp(ert->name, _T("a2091")))
					_tcscpy(sid, _T("XT"));
				else if (ci->controller_unit == 8 && ert && !_tcscmp(ert->name, _T("a2090a")))
					_tcscpy(sid, _T("ST-506"));
				else
					_stprintf(sid, _T("%d"), ci->controller_unit);
				_stprintf (blocksize_str, _T("%d"), ci->blocksize);
				if (ert) {
					if (ci->controller_type_unit == 0)
						_stprintf (devname_str, _T("%s:%s"), ert->friendlyname, sid);
					else
						_stprintf (devname_str, _T("%s:%s/%d"), ert->friendlyname, sid, ci->controller_type_unit + 1);
				} else {
					_stprintf (devname_str, scsidevs[ctype - HD_CONTROLLER_TYPE_SCSI_FIRST], sid);
				}
				harddisktype (volname_str, ci);
				_tcscpy (bootpri_str, _T("n/a"));
			} else if (ctype >= HD_CONTROLLER_TYPE_CUSTOM_FIRST && ctype <= HD_CONTROLLER_TYPE_CUSTOM_LAST) {
				TCHAR sid[8];
				const struct expansionromtype *ert = get_unit_expansion_rom(ctype);
				_stprintf(sid, _T("%d"), ci->controller_unit);
				if (ert) {
					if (ci->controller_type_unit == 0)
						_stprintf(devname_str, _T("%s:%s"), ert->friendlyname, sid);
					else
						_stprintf(devname_str, _T("%s:%s/%d"), ert->friendlyname, sid, ci->controller_type_unit + 1);
				} else {
					_stprintf(devname_str, _T("PCMCIA"));
				}
				harddisktype(volname_str, ci);
				_tcscpy(bootpri_str, _T("n/a"));
			} else if (type == FILESYS_HARDFILE) {
				_stprintf (blocksize_str, _T("%d"), ci->blocksize);
				_tcscpy (devname_str, ci->devname);
				_tcscpy (volname_str, _T("n/a"));
				_stprintf (bootpri_str, _T("%d"), ci->bootpri);
			} else if (type == FILESYS_HARDFILE_RDB || type == FILESYS_HARDDRIVE || ci->controller_type != HD_CONTROLLER_TYPE_UAE) {
				_stprintf (blocksize_str, _T("%d"), ci->blocksize);
				_stprintf (devname_str, _T("UAE:%d"), ci->controller_unit);
				_tcscpy (volname_str, _T("n/a"));
				_tcscpy (bootpri_str, _T("n/a"));
			} else if (type == FILESYS_TAPE) {
				_stprintf (blocksize_str, _T("%d"), ci->blocksize);
				_tcscpy (devname_str, _T("UAE"));
				harddisktype (volname_str, ci);
				_tcscpy (bootpri_str, _T("n/a"));
			} else {
				_tcscpy (blocksize_str, _T("n/a"));
				_tcscpy (devname_str, ci->devname);
				_tcscpy (volname_str, ci->volname);
				_tcscpy (size_str, _T("n/a"));
				_stprintf (bootpri_str, _T("%d"), ci->bootpri);
			}
			if (!mi.ismedia) {
				_tcscpy (blocksize_str, _T("n/a"));
				_tcscpy (size_str, _T("n/a"));
			}
			if (rootdirp[0] == 0) {
				xfree (rootdir);
				rootdir = my_strdup (_T("-"));
				rootdirp = rootdir;
			}
			WIN32GUI_LoadUIString (ci->readonly ? IDS_NO : IDS_YES, readwrite_str, sizeof (readwrite_str) / sizeof (TCHAR));

			lvstruct.mask     = LVIF_TEXT | LVIF_PARAM;
			lvstruct.pszText  = mi.ismedia == false ? _T("E") : (nosize && mi.size >= 0 ? _T("X") : (mi.ismounted ? _T("*") : _T(" ")));
			if (mi.error == -1)
				lvstruct.pszText = _T("?");
			else if (mi.error == -2)
				lvstruct.pszText = _T("!");
			if (ci->controller_type != HD_CONTROLLER_TYPE_UAE && mi.ismedia)
				lvstruct.pszText = _T(" ");
			lvstruct.lParam   = 0;
			lvstruct.iItem    = i;
			lvstruct.iSubItem = 0;
			result = ListView_InsertItem (list, &lvstruct);
			if (result != -1) {

				listview_column_width[0] = MulDiv(20, dpi, 72);

				ListView_SetItemText(list, result, 1, devname_str);
				width = MulDiv(ListView_GetStringWidth(list, devname_str), dpi, 72) + listpadding;
				if(width > listview_column_width[1])
					listview_column_width[1] = width;

				ListView_SetItemText(list, result, 2, volname_str);
				width = MulDiv(ListView_GetStringWidth(list, volname_str), dpi, 72) + listpadding;
				if(width > listview_column_width[2])
					listview_column_width[2] = width;

				listview_column_width[3] = 150;
				ListView_SetItemText(list, result, 3, rootdirp);
				width = MulDiv(ListView_GetStringWidth(list, rootdirp), dpi, 72) + listpadding;
				if(width > listview_column_width[3])
					listview_column_width[3] = width;

				ListView_SetItemText(list, result, 4, readwrite_str);
				width = MulDiv(ListView_GetStringWidth(list, readwrite_str), dpi, 72) + listpadding;
				if(width > listview_column_width[4])
					listview_column_width[4] = width;

				ListView_SetItemText(list, result, 5, blocksize_str);
				width = MulDiv(ListView_GetStringWidth(list, blocksize_str), dpi, 72) + listpadding;
				if(width > listview_column_width[5])
					listview_column_width[5] = width;

				ListView_SetItemText(list, result, 6, size_str);
				width = MulDiv(ListView_GetStringWidth(list, size_str), dpi, 72) + listpadding;
				if(width > listview_column_width[6])
					listview_column_width[6] = width;

				ListView_SetItemText(list, result, 7, bootpri_str);
				width = MulDiv(ListView_GetStringWidth(list, bootpri_str), dpi, 72) + listpadding;
				if(width > listview_column_width[7] )
					listview_column_width[7] = width;
			}
			xfree (rootdir);
		}
#endif
	}

	if (result != -1) {

		if (GetWindowRect(list, &rect)) {
			ScreenToClient(hDlg, (LPPOINT)&rect);
			ScreenToClient(hDlg, (LPPOINT)&rect.right);
			if (listview_num_columns == 2) {
				if ((temp = rect.right - rect.left - listview_column_width[0] - MulDiv(30, dpi, 72)) > listview_column_width[1])
					listview_column_width[1] = temp;
			} else if (listview_num_columns == 1) {
				listview_column_width[0] = rect.right - rect.left - MulDiv(30, dpi, 72);
			}
		}

		for (i = 0; i < listview_num_columns; i++) {
			listview_column_widths[i] = listview_column_width[i];
		}

		if (!LoadListView(list)) {
			// Adjust our column widths so that we can see the contents...
			for (i = 0; i < listview_num_columns; i++) {
				int w = ListView_GetColumnWidth(list, i);
				if (w < listview_column_width[i])
					ListView_SetColumnWidth(list, i, listview_column_width[i]);
			}
		}

		// Redraw the items in the list...
		items = ListView_GetItemCount (list);
		ListView_RedrawItems (list, 0, items);
	}
	if (lv_oldidx[lv_type] >= 0) {
		int idx = lv_oldidx[lv_type];
		if (idx >= ListView_GetItemCount (list))
			idx = ListView_GetItemCount (list) - 1;
		if (idx >= 0)
			ListView_EnsureVisible (list, idx, FALSE);
	}
	lv_old_type = lv_type;

	SetWindowRedraw(list, TRUE);
	RedrawWindow(list, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
}

static int listview_find_selected(HWND list, bool paramIndex)
{
	int i, items;
	items = ListView_GetItemCount (list);
	for (i = 0; i < items; i++) {
		if (ListView_GetItemState(list, i, LVIS_SELECTED) == LVIS_SELECTED) {
			if (paramIndex) {
				LVITEM pitem = { 0 };
				pitem.mask = LVIF_PARAM;
				pitem.iItem = i;
				ListView_GetItem(list, &pitem);
				return (int)pitem.lParam;
			}
			return i;
		}
	}
	return -1;
}

static int listview_entry_from_click (HWND list, int *column, bool paramIndex)
{
	POINT point;
	POINTS p;
	DWORD pos = GetMessagePos ();
	int items, entry;

	p = MAKEPOINTS (pos);
	point.x = p.x;
	point.y = p.y;
	ScreenToClient (list, &point);
	entry = ListView_GetTopIndex (list);
	items = entry + ListView_GetCountPerPage (list);
	if (items > ListView_GetItemCount (list))
		items = ListView_GetItemCount (list);

	while (entry <= items) {
		RECT rect;
		/* Get the bounding rectangle of an item. If the mouse
		* location is within the bounding rectangle of the item,
		* you know you have found the item that was being clicked.  */
		if (ListView_GetItemRect (list, entry, &rect, LVIR_BOUNDS)) {
			if (PtInRect (&rect, point)) {
				POINT ppt;
				int x;
				UINT flag = LVIS_SELECTED | LVIS_FOCUSED;

				ListView_GetItemPosition (list, entry, &ppt);
				x = ppt.x;
				ListView_SetItemState (list, entry, flag, flag);
				for (int i = 0; i < listview_num_columns && column; i++) {
					int cw = ListView_GetColumnWidth (list, i);
					if (x < point.x && x + cw > point.x) {
						*column = i;
						break;
					}
					x += cw;
				}
				if (paramIndex) {
					LVITEM pitem = { 0 };
					pitem.mask = LVIF_PARAM;
					pitem.iItem = entry;
					ListView_GetItem(list, &pitem);
					return (int)pitem.lParam;
				}
				return entry;
			}
		}
		entry++;
	}
	return -1;
}

static void getconfigfolderregistry(void)
{
	int cfsize;
	config_folder[0] = 0;
	cfsize = sizeof(config_folder) / sizeof(TCHAR);
	regquerystr(NULL, configregfolder[configtypepanel], config_folder, &cfsize);
	config_search[0] = 0;
	cfsize = sizeof(config_search) / sizeof(TCHAR);
	regquerystr(NULL, configregsearch[configtypepanel], config_search, &cfsize);
}

static void ConfigToRegistry(struct ConfigStruct *config, int type)
{
	if (config) {
		TCHAR path[MAX_DPATH];
		_tcscpy(path, config->Path);
		_tcsncat(path, config->Name, MAX_DPATH - _tcslen(path));
		regsetstr(NULL, configreg[type], path);
	}
	regsetstr(NULL, configregfolder[type], config_folder);
	regsetstr(NULL, configregsearch[type], config_search);
	int idx = 0;
	int exp = 1;
	UAEREG *fkey = NULL;
	while (idx < configstoresize) {
		config = configstore[idx];
		if (config->Directory && config->expanded) {
			if (!fkey) {
				fkey = regcreatetree(NULL, _T("ConfigurationListView"));
			}
			if (fkey) {
				TCHAR tmp[100];
				_stprintf(tmp, _T("Expanded%03d"), exp++);
				regsetstr(fkey, tmp, config->Path);
			}
		}
		idx++;
	}
	while (exp < 1000) {
		TCHAR tmp[100];
		_stprintf(tmp, _T("Expanded%03d"), exp++);
		if (!fkey) {
			fkey = regcreatetree(NULL, _T("ConfigurationListView"));
		}
		if (!fkey) {
			break;
		}
		if (!regexists(fkey, tmp)) {
			break;
		}
		regdelete(fkey, tmp);
	}
	regclosetree(fkey);
}
static void ConfigToRegistry2(DWORD ct, int type, DWORD noauto)
{
	if (type > 0)
		regsetint(NULL, configreg2[type], ct);
	if (noauto == 0 || noauto == 1)
		regsetint(NULL, _T("ConfigFile_NoAuto"), noauto);
}

static void checkautoload(HWND	hDlg, struct ConfigStruct *config)
{
	int ct = 0;

	if (configtypepanel > 0)
		regqueryint(NULL, configreg2[configtypepanel], &ct);
	if (!config || config->Directory) {
		ct = 0;
		ConfigToRegistry2(ct, configtypepanel, -1);
	}
	CheckDlgButton(hDlg, IDC_CONFIGAUTO, ct ? BST_CHECKED : BST_UNCHECKED);
	ew(hDlg, IDC_CONFIGAUTO, configtypepanel > 0 && config && !config->Directory ? TRUE : FALSE);
	regqueryint(NULL, _T("ConfigFile_NoAuto"), &ct);
	CheckDlgButton(hDlg, IDC_CONFIGNOLINK, ct ? BST_CHECKED : BST_UNCHECKED);
}

static struct ConfigStruct *InfoSettingsProcConfig;
static INT_PTR CALLBACK InfoSettingsProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;

	bool handled;
	INT_PTR v = commonproc(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return v;
	}

	switch (msg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return TRUE;
	case WM_CLOSE:
		CustomDialogClose(hDlg, 0);
		return TRUE;
	case WM_INITDIALOG:
	{
		recursive++;
		xSendDlgItemMessage(hDlg, IDC_CONFIGLINK, CB_RESETCONTENT, 0, 0L);
		xSendDlgItemMessage(hDlg, IDC_CONFIGLINK, CB_ADDSTRING, 0, (LPARAM)_T(""));
		int idx1 = 1;
		int idx2 = 0;
		for (int j = 0; j < 2; j++) {
			for (int i = 0; i < configstoresize; i++) {
				struct ConfigStruct *cs = configstore[i];
				if ((j == 0 && cs->Type == CONFIG_TYPE_HOST) || (j == 1 && cs->Type == CONFIG_TYPE_HARDWARE)) {
					TCHAR tmp2[MAX_DPATH];
					_tcscpy(tmp2, cs->Path);
					_tcsncat(tmp2, cs->Name, MAX_DPATH - _tcslen(tmp2));
					xSendDlgItemMessage(hDlg, IDC_CONFIGLINK, CB_ADDSTRING, 0, (LPARAM)tmp2);
					TCHAR *p1 = workprefs.config_host_path;
					if (_tcslen(p1) > _tcslen(tmp2)) {
						p1 += _tcslen(p1) - _tcslen(tmp2);
					}
					TCHAR *p2 = workprefs.config_host_path;
					if (_tcslen(p2) > _tcslen(tmp2)) {
						p2 += _tcslen(p2) - _tcslen(tmp2);
					}
					if (!_tcsicmp(tmp2, p1) || !_tcsicmp(tmp2, p2))
						idx2 = idx1;
					idx1++;
				}
			}
		}
		xSendDlgItemMessage(hDlg, IDC_CONFIGLINK, CB_SETCURSEL, idx2, 0);
		checkautoload(hDlg, InfoSettingsProcConfig);
		EnableWindow(GetDlgItem(hDlg, IDC_CONFIGAUTO), configtypepanel > 0);
		EnableWindow(GetDlgItem(hDlg, IDC_CONFIGLINK), configtypepanel == 0);
		EnableWindow(GetDlgItem(hDlg, IDC_CONFIGNOLINK), configtypepanel == 0);
		SetDlgItemText(hDlg, IDC_PATH_NAME, workprefs.info);
		SetDlgItemText(hDlg, IDC_CONFIGCATEGORY, workprefs.category);
		SetDlgItemText(hDlg, IDC_CONFIGTAGS, workprefs.tags);
		recursive--;
		return TRUE;
	}

	case WM_COMMAND:
		if (recursive)
			break;
		recursive++;

		switch (LOWORD(wParam))
		{
			case IDC_SELECTOR:
			DiskSelection(hDlg, IDC_PATH_NAME, 8, &workprefs, NULL, NULL);
			break;
			case IDOK:
			CustomDialogClose(hDlg, -1);
			recursive = 0;
			return TRUE;
			case IDCANCEL:
			CustomDialogClose(hDlg, 0);
			recursive = 0;
			return TRUE;
			case IDC_CONFIGAUTO:
			if (configtypepanel > 0) {
				int ct = ischecked(hDlg, IDC_CONFIGAUTO) ? 1 : 0;
				ConfigToRegistry2(ct, configtypepanel, -1);
			}
			break;
			case IDC_CONFIGNOLINK:
			if (configtypepanel == 0) {
				int ct = ischecked(hDlg, IDC_CONFIGNOLINK) ? 1 : 0;
				ConfigToRegistry2(-1, -1, ct);
			}
			break;
			case IDC_CONFIGLINK:
			if (HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == CBN_KILLFOCUS) {
				TCHAR tmp[MAX_DPATH];
				tmp[0] = 0;
				getcbn(hDlg, IDC_CONFIGLINK, tmp, sizeof(tmp) / sizeof(TCHAR));
				_tcscpy(workprefs.config_host_path, tmp);
			}
			break;
		}

		GetDlgItemText(hDlg, IDC_PATH_NAME, workprefs.info, sizeof workprefs.info / sizeof(TCHAR));
		GetDlgItemText(hDlg, IDC_CONFIGCATEGORY, workprefs.category, sizeof workprefs.category / sizeof(TCHAR));
		GetDlgItemText(hDlg, IDC_CONFIGTAGS, workprefs.tags, sizeof workprefs.tags / sizeof(TCHAR));
		recursive--;
		break;
	}
	return FALSE;
}

static int addConfigFolder(HWND hDlg, const TCHAR *s, bool directory)
{
	TCHAR tmp[MAX_DPATH];
	if (directory) {
		_tcscpy(tmp, s);
	} else {
		_stprintf(tmp, _T("[%s]"), s);
	}
	int idx = xSendDlgItemMessage(hDlg, IDC_CONFIGFOLDER, CB_ADDSTRING, 0, (LPARAM)tmp);
	if (!_tcscmp(tmp, config_folder))
		xSendDlgItemMessage(hDlg, IDC_CONFIGFOLDER, CB_SETCURSEL, idx, 0);
	return idx;
}

static HTREEITEM AddConfigNode(HWND hDlg, struct ConfigStruct *config, const TCHAR *name, const TCHAR *desc, const TCHAR *path, int isdir, int expand, HTREEITEM parent, TCHAR *file_name, TCHAR *file_path, HWND TVhDlg)
{
	TVINSERTSTRUCT is = { 0 };
	TCHAR s[MAX_DPATH] = _T("");

	is.hInsertAfter = isdir < 0 ? TVI_ROOT : TVI_SORT;
	is.hParent = parent;
	is.itemex.mask = TVIF_TEXT | TVIF_STATE | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM;
	if (name && path && !_tcscmp (file_name, name) && !_tcscmp (file_path, path)) {
		is.itemex.state |= TVIS_SELECTED;
		is.itemex.stateMask |= TVIS_SELECTED;
	}
	if (isdir) {
		_tcscat (s, _T(" "));
		is.itemex.state |= TVIS_BOLD;
		is.itemex.stateMask |= TVIS_BOLD;
	}
	if (expand) {
		is.itemex.state |= TVIS_EXPANDED;
		is.itemex.stateMask |= TVIS_EXPANDED;
	}
	_tcscat (s, name);
	if (_tcslen (s) > 4 && !_tcsicmp (s + _tcslen (s) - 4, _T(".uae")))
		s[_tcslen (s) - 4] = 0;
	if (desc && _tcslen (desc) > 0) {
		_tcscat (s, _T(" ("));
		_tcscat (s, desc);
		_tcscat (s, _T(")"));
	}
	is.itemex.pszText = s;
	is.itemex.iImage = is.itemex.iSelectedImage = isdir > 0 ? 0 : (isdir < 0) ? 2 : 1;
	is.itemex.lParam = (LPARAM)config;
	return TreeView_InsertItem (TVhDlg, &is);
}

static bool configsearchmatch(const TCHAR *str, int searchlen)
{
	if (!str[0])
		return false;
	int strlen = uaetcslen(str);
	if (strlen >= searchlen) {
		for (int i = 0; i <= strlen - searchlen; i++) {
			if (!_tcsnicmp(config_search, str + i, searchlen)) {
				return true;
			}
		}
	}
	return false;
}

static bool configsearch(struct ConfigStruct *config)
{
	int searchlen = uaetcslen(config_search);
	for (int j = 0; j < 3; j++) {
		TCHAR *str = NULL;
		switch (j)
		{
			case 0:
			str = config->Name;
			if (configsearchmatch(str, searchlen))
				return true;
			break;
			case 1:
			str = config->Description;
			if (configsearchmatch(str, searchlen))
				return true;
			break;
			case 2:
			{
				TCHAR tag[CFG_DESCRIPTION_LENGTH + 1] = { 0 };
				if (config->Tags[0]) {
					_tcscpy(tag, config->Tags);
					TCHAR *p = tag;
					for (;;) {
						TCHAR *p2 = p;
						while (*p2 != ',' && *p2 != ' ' && *p2 != 0)
							p2++;
						*p2++ = 0;
						if (configsearchmatch(p, searchlen))
							return true;
						if (*p2 == 0)
							break;
						p = p2;
					}
				}

			}
			break;
		}
	}
	return false;
}

static int LoadConfigTreeView (HWND hDlg, int idx, HTREEITEM parent)
{
	struct ConfigStruct *cparent, *config;
	int cnt = 0;

	if (configstoresize == 0)
		return cnt;
	if (idx < 0) {
		idx = 0;
		for (;;) {
			config = configstore[idx];
			if (config->Parent == NULL)
				break;
			idx++;
			if (idx >= configstoresize)
				return cnt;
		}
	}

	TCHAR file_name[MAX_DPATH] = _T(""), file_path[MAX_DPATH] = _T("");
	GetDlgItemText(hDlg, IDC_EDITNAME, file_name, MAX_DPATH);
	GetDlgItemText(hDlg, IDC_EDITPATH, file_path, MAX_DPATH);
	HWND TVhDlg = GetDlgItem(hDlg, IDC_CONFIGTREE);

	cparent = configstore[idx]->Parent;
	idx = 0;
	while (idx < configstoresize) {
		config = configstore[idx];
		if ((configtypepanel == 1 && !config->hardware) || (configtypepanel == 2 && !config->host) || (configtypepanel == 0 && (config->host || config->hardware))) {
			idx++;
			continue;
		}
		if (config->Parent == cparent) {
			bool visible = false;
			int cfgflen = uaetcslen(config_folder);
			if (config_folder[0] == 0) {
				visible = true;
			} else if (config_folder[0] == '[' && config_folder[cfgflen - 1] == ']') {
				visible = false;
				if (cfgflen - 2 == uaetcslen(config->Category) && !_tcsnicmp(config->Category, config_folder + 1, cfgflen - 2)) {
					visible = true;
				}
			} else if (!_tcsncmp(config_folder, config->Path, uaetcslen(config_folder))) {
				visible = true;
			}
			if (config_search[0] && visible && !config->Directory) {
				visible = configsearch(config);
			}
			if (visible) {
				if (config->Directory) {
					int stridx = -1;
					bool expand = config->hardware || config->host;
					if (config_folder[0]) {
						expand = true;
					}
					stridx = addConfigFolder(hDlg, config->Path, true);
					HTREEITEM par = AddConfigNode(hDlg, config, config->Name, NULL, config->Path, 1, expand, parent, file_name, file_path, TVhDlg);
					int idx2 = 0;
					for (;;) {
						if (configstore[idx2] == config->Child) {
							config->item = par;
							if (LoadConfigTreeView(hDlg, idx2, par) == 0) {
								if (!config->hardware && !config->host && !config->Directory) {
									TreeView_DeleteItem(GetDlgItem(hDlg, IDC_CONFIGTREE), par);
									if (stridx >= 0)
										xSendDlgItemMessage(hDlg, IDC_CONFIGFOLDER, CB_DELETESTRING, stridx, 0);
								}
							}
							break;
						}
						idx2++;
						if (idx2 >= configstoresize)
							break;
					}
				} else if (!config->Directory) {
					if (((config->Type == 0 || config->Type == 3) && configtype == 0) || (config->Type == configtype)) {
						config->item = AddConfigNode(hDlg, config, config->Name, config->Description, config->Path, 0, 0, parent, file_name, file_path, TVhDlg);
						cnt++;
					}
				}
			} else {
				if (config->Directory) {
					addConfigFolder(hDlg, config->Path, true);
					int idx2 = 0;
					for (;;) {
						if (configstore[idx2] == config->Child) {
							LoadConfigTreeView(hDlg, idx2, parent);
							break;
						}
						idx2++;
						if (idx2 >= configstoresize)
							break;
					}
				}
			}
		}
		idx++;
	}
	return cnt;
}

static void InitializeConfig (HWND hDlg, struct ConfigStruct *config)
{
	addhistorymenu(hDlg, config == NULL ? _T("") : config->Name, IDC_EDITNAME, HISTORY_CONFIGFILE, false, -1);
	if (config == NULL) {
		SetDlgItemText (hDlg, IDC_EDITDESCRIPTION, _T(""));
	} else {
		SetDlgItemText (hDlg, IDC_EDITDESCRIPTION, config->Description);
	}
	if (config && config->Artpath[0]) {
		show_box_art(config->Artpath, config->Name);
	} else {
		show_box_art(NULL, NULL);
	}
}

static void DeleteConfigTree (HWND hDlg)
{
	HWND TVhDlg = GetDlgItem(hDlg, IDC_CONFIGTREE);
	for (int i = 0; i < configstoresize; i++) {
		configstore[i]->item = NULL;
	}
	TreeView_DeleteAllItems (TVhDlg);
}

static HTREEITEM InitializeConfigTreeView (HWND hDlg)
{
	HIMAGELIST himl = ImageList_Create (16, 16, ILC_COLOR8 | ILC_MASK, 3, 0);
	HWND TVhDlg = GetDlgItem(hDlg, IDC_CONFIGTREE);
	SubclassTreeViewControl(TVhDlg);
	HTREEITEM parent;
	TCHAR path[MAX_DPATH];

	if (himl) {
		HICON icon;
		icon = LoadIcon (hInst, (LPCWSTR)MAKEINTRESOURCE(IDI_FOLDER));
		ImageList_AddIcon (himl, icon);
		icon = LoadIcon (hInst, (LPCWSTR)MAKEINTRESOURCE(IDI_FILE));
		ImageList_AddIcon (himl, icon);
		icon = LoadIcon (hInst, (LPCWSTR)MAKEINTRESOURCE(IDI_ROOT));
		ImageList_AddIcon (himl, icon);
		TreeView_SetImageList (TVhDlg, himl, TVSIL_NORMAL);
	}
	xSendDlgItemMessage(hDlg, IDC_CONFIGFOLDER, CB_RESETCONTENT, 0, 0L);
	xSendDlgItemMessage(hDlg, IDC_CONFIGFOLDER, CB_ADDSTRING, 0, (LPARAM)_T(""));
	for (int i = 0; i < categorystoresize; i++) {
		struct CategoryStruct *c = categorystore[i];
		addConfigFolder(hDlg, c->category, false);
	}
	DeleteConfigTree (hDlg);
	GetConfigPath (path, NULL, FALSE);
	parent = AddConfigNode (hDlg, NULL, path, NULL, NULL, 0, 1, NULL, NULL, NULL, TVhDlg);
	LoadConfigTreeView (hDlg, -1, parent);
	ew(hDlg, IDC_CONFIGFOLDER, xSendDlgItemMessage(hDlg, IDC_CONFIGFOLDER, CB_GETCOUNT, 0, 0L) > 1);
	return parent;
}

static struct ConfigStruct *fixloadconfig (HWND hDlg, struct ConfigStruct *config)
{
	int i;

	if (config && configtypepanel == 0 && (config->host || config->hardware))
		return NULL;
	if ((!config && configtypepanel) || (config && (configtypepanel == 2 && !config->host) || (configtypepanel == 1 && !config->hardware))) {
		for (i = 0; i < configstoresize; i++) {
			struct ConfigStruct *cs = configstore[i];
			if (cs->Directory && ((configtypepanel == 1 && cs->hardware) || (configtypepanel == 2 && cs->host))) {
				config = cs;
				SetDlgItemText (hDlg, IDC_EDITPATH, config->Path);
				break;
			}
		}
	}
	return config;
}

static struct ConfigStruct *initloadsave (HWND hDlg, struct ConfigStruct *config, bool init)
{
	HTREEITEM root;
	TCHAR name_buf[MAX_DPATH];
	int dwRFPsize = sizeof(name_buf) / sizeof(TCHAR);
	TCHAR path[MAX_DPATH];

	HWND lv = GetDlgItem(hDlg, IDC_CONFIGTREE);
	SetWindowRedraw(lv, FALSE);

	EnableWindow (GetDlgItem (hDlg, IDC_VIEWINFO), workprefs.info[0]);
	root = InitializeConfigTreeView (hDlg);
	if (regquerystr (NULL, configreg[configtypepanel], name_buf, &dwRFPsize)) {
		if (init) {
			if (_tcsicmp(name_buf, _T("default.uae")))
				target_cfgfile_load (&workprefs, name_buf, CONFIG_TYPE_DEFAULT, 0);
		}
		struct ConfigStruct *config2 = getconfigstorefrompath (name_buf, path, configtypepanel);
		if (config2)
			config = config2;
	}

	if (regexiststree(NULL, _T("ConfigurationListView"))) {
		UAEREG *fkey = regcreatetree(NULL, _T("ConfigurationListView"));
		if (fkey) {
			int exp = 1;
			while (exp < 1000) {
				TCHAR tmp[100];
				TCHAR name[MAX_DPATH];
				_stprintf(tmp, _T("Expanded%03d"), exp++);
				if (!regexists(fkey, tmp)) {
					break;
				}
				int size = sizeof(name) / sizeof(TCHAR);
				if (regquerystr(fkey, tmp, name, &size)) {
					int idx = 0;
					while (idx < configstoresize) {
						struct ConfigStruct *config2 = configstore[idx];
						if (config2->Directory) {
							if (!_tcscmp(name, config2->Path)) {
								TreeView_Expand(lv, config2->item, TVE_EXPAND);
								config2->expanded = true;
								break;
							}
						}
						idx++;
					}
				}
			}
			regclosetree(fkey);
		}
	}

	config = fixloadconfig (hDlg, config);

	SetWindowRedraw(lv, TRUE);
	RedrawWindow(lv, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);

	if (config && config->item)
		TreeView_SelectItem (GetDlgItem(hDlg, IDC_CONFIGTREE), config->item);
	else
		TreeView_SelectItem (GetDlgItem(hDlg, IDC_CONFIGTREE), root);

	return config;
}

static struct ConfigStruct *refreshconfiglist(HWND hDlg, struct ConfigStruct *config)
{
	struct ConfigStruct *cs = initloadsave(hDlg, config, false);
	return cs;
}

static void loadsavecommands (HWND hDlg, WPARAM wParam, struct ConfigStruct **configp, TCHAR **pcfgfile, TCHAR *newpath)
{
	struct ConfigStruct *config = *configp;

	if (HIWORD(wParam) == CBN_SELCHANGE) {
		switch (LOWORD(wParam))
		{
			case IDC_CONFIGFOLDER:
			{
				int idx = xSendDlgItemMessage(hDlg, IDC_CONFIGFOLDER, CB_GETCURSEL, 0, 0L);
				if (idx >= 0) {
					xSendDlgItemMessage(hDlg, IDC_CONFIGFOLDER, CB_GETLBTEXT, (WPARAM)idx, (LPARAM)config_folder);
					if (_tcslen(config_folder) > 0 && config_folder[_tcslen(config_folder) - 1] != ']' && config_folder[_tcslen(config_folder) - 1] != '\\')
						_tcscat(config_folder, _T("\\"));
					ConfigToRegistry(config, configtypepanel);
					config = refreshconfiglist(hDlg, config);
				}
				break;
			}
			case IDC_EDITNAME:
			{
				TCHAR cfg[MAX_DPATH];
				if (getcomboboxtext(hDlg, IDC_EDITNAME, cfg, sizeof cfg / sizeof(TCHAR))) {
					config = NULL;
					for (int i = 0; i < configstoresize; i++) {
						struct ConfigStruct *cs = configstore[i];
						TCHAR path[MAX_DPATH];
						_tcscpy(path, cs->Path);
						_tcscat(path, cs->Name);
						if (!_tcsicmp(path, cfg)) {
							config = cs;
							TreeView_SelectItem(GetDlgItem(hDlg, IDC_CONFIGTREE), cs->item);
							break;
						}
					}
				}
			}
			break;
		}
	}
	if (HIWORD(wParam) == EN_CHANGE) {
		switch (LOWORD(wParam))
		{
			case IDC_CONFIGSEARCH:
			{
				GetDlgItemText(hDlg, IDC_CONFIGSEARCH, config_search, MAX_DPATH);
				config = refreshconfiglist(hDlg, config);
				break;
			}
		}
	}

	switch (LOWORD (wParam))
	{
	case IDC_CONFIGSEARCHCLEAR:
		if (config_search[0]) {
			config_search[0] = 0;
			SetDlgItemText(hDlg, IDC_CONFIGSEARCH, _T(""));
			ConfigToRegistry(config, configtypepanel);
			config = refreshconfiglist(hDlg, config);
		}
		break;
	case IDC_SAVE:
		if (HandleConfiguration (hDlg, CONFIG_SAVE_FULL, config, newpath)) {
			DeleteConfigTree (hDlg);
			config = CreateConfigStore (config, TRUE);
			ConfigToRegistry (config, configtypepanel);
			config = initloadsave (hDlg, config, false);
			InitializeConfig (hDlg, config);
		}
		break;
	case IDC_QUICKSAVE:
		if (HandleConfiguration (hDlg, CONFIG_SAVE, config, NULL)) {
			DeleteConfigTree (hDlg);
			config = CreateConfigStore (config, TRUE);
			ConfigToRegistry (config, configtypepanel);
			config = initloadsave (hDlg, config, false);
			InitializeConfig (hDlg, config);
		}
		break;
	case IDC_QUICKLOAD:
		*pcfgfile = HandleConfiguration (hDlg, CONFIG_LOAD, config, NULL);
		if (*pcfgfile) {
			ConfigToRegistry (config, configtypepanel);
			InitializeConfig (hDlg, config);
			if (full_property_sheet) {
				inputdevice_updateconfig (NULL, &workprefs);
			} else {
				uae_restart(&workprefs, -1, *pcfgfile);
				exit_gui(1);
			}
		}
		break;
	case IDC_LOAD:
		*pcfgfile = HandleConfiguration (hDlg, CONFIG_LOAD_FULL, config, newpath);
		if (*pcfgfile) {
			ConfigToRegistry (config, configtypepanel);
			InitializeConfig (hDlg, config);
			if (full_property_sheet) {
				inputdevice_updateconfig (NULL, &workprefs);
			} else {
				uae_restart(&workprefs, -1, *pcfgfile);
				exit_gui(1);
			}
		}
		break;
	case IDC_DELETE:
		if (HandleConfiguration (hDlg, CONFIG_DELETE, config, NULL)) {
			DeleteConfigTree (hDlg);
			config = CreateConfigStore (config, TRUE);
			config = initloadsave (hDlg, config, false);
			InitializeConfig (hDlg, config);
		}
		break;
	case IDC_VIEWINFO:
		if (workprefs.info[0]) {
			TCHAR name_buf[MAX_DPATH];
			if (_tcsstr (workprefs.info, _T("Configurations\\")))
				_stprintf (name_buf, _T("%s\\%s"), start_path_data, workprefs.info);
			else
				_tcscpy (name_buf, workprefs.info);
			ShellExecute (NULL, NULL, name_buf, NULL, NULL, SW_SHOWNORMAL);
		}
		break;
	case IDC_SETINFO:
		InfoSettingsProcConfig = config;
		if (CustomCreateDialogBox(IDD_SETINFO, hDlg, InfoSettingsProc))
			EnableWindow( GetDlgItem( hDlg, IDC_VIEWINFO ), workprefs.info[0] );
		break;
	}
	*configp = config;
}

static INT_PTR CALLBACK LoadSaveDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	TCHAR *cfgfile = NULL;
	static int recursive;
	static struct ConfigStruct *config;

	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch (msg)
	{
	case WM_INITDIALOG:
	{
		recursive++;
		if (!configstore) {
			DeleteConfigTree(hDlg);
			CreateConfigStore(NULL, FALSE);
			config = NULL;
		}
		getconfigfolderregistry();
		pages[LOADSAVE_ID] = hDlg;
		currentpage = LOADSAVE_ID;
		SetDlgItemText(hDlg, IDC_EDITPATH, _T(""));
		SetDlgItemText(hDlg, IDC_EDITDESCRIPTION, workprefs.description);
		SetDlgItemText(hDlg, IDC_CONFIGSEARCH, config_search);
		config = initloadsave(hDlg, config, firstautoloadconfig);
		firstautoloadconfig = false;
		recursive--;
		return TRUE;
	}

	case WM_DESTROY:
	ConfigToRegistry(NULL, configtypepanel);
	break;

	case WM_USER + 1:
		if (config) {
			if (config->Artpath[0]) {
				show_box_art(config->Artpath, config->Name);
			} else {
				show_box_art(NULL, NULL);
			}
		}
		break;

	case WM_CONTEXTMENU:
		{
			int id = GetDlgCtrlID ((HWND)wParam);
			if (id == IDC_SAVE || id == IDC_LOAD) {
				TCHAR *s = favoritepopup (hDlg);
				if (s) {
					loadsavecommands (hDlg, id, &config, &cfgfile, s);
					xfree (s);
				}
			}
			break;
		}

	case WM_COMMAND:
		{
			recursive++;
			loadsavecommands (hDlg, wParam, &config, &cfgfile, NULL);
			recursive++;
			break;
		}

	case WM_NOTIFY:
		{
			LPNMHDR nm = (LPNMHDR)lParam;
			if (nm->hwndFrom == GetDlgItem (hDlg, IDC_CONFIGTREE)) {
				switch (nm->code)
				{
				case NM_DBLCLK:
					{
						HTREEITEM ht = TreeView_GetSelection (GetDlgItem (hDlg, IDC_CONFIGTREE));
						if (ht != NULL) {
							TVITEMEX pitem;
							memset (&pitem, 0, sizeof (pitem));
							pitem.mask = TVIF_HANDLE | TVIF_PARAM;
							pitem.hItem = ht;
							if (TreeView_GetItem (GetDlgItem(hDlg, IDC_CONFIGTREE), &pitem)) {
								struct ConfigStruct *config = (struct ConfigStruct*)pitem.lParam;
								if (config && !config->Directory) {
									cfgfile = HandleConfiguration (hDlg, CONFIG_LOAD, config, NULL);
									if (cfgfile) {
										ConfigToRegistry (config, configtypepanel);
										if (!full_property_sheet)
											uae_restart(&workprefs, 0, cfgfile);
										exit_gui (1);
									}
								}
							}
						}
						return TRUE;
					}
					break;
				case TVN_SELCHANGING:
					return FALSE;
				case TVN_SELCHANGED:
					{
						LPNMTREEVIEW tv = (LPNMTREEVIEW)lParam;
						struct ConfigStruct *c = (struct ConfigStruct*)tv->itemNew.lParam;
						if (c) {
							config = c;
							if (!config->Directory) {
								InitializeConfig (hDlg, config);
							} else {
								InitializeConfig (hDlg, NULL);
							}
							SetDlgItemText (hDlg, IDC_EDITPATH, config->Path);
						}
						if (configtypepanel > 0) {
							if (c && !c->Directory) {
								ConfigToRegistry (config, configtypepanel);
								InitializeConfig (hDlg, config);
							}
							checkautoload (hDlg, c);
						}
						return TRUE;
					}
					break;
				case TVN_ITEMEXPANDED:
					{
						LPNMTREEVIEW tv = (LPNMTREEVIEW)lParam;
						struct ConfigStruct *c = (struct ConfigStruct*)tv->itemNew.lParam;
						if (c) {
							if (tv->action == TVE_EXPAND) {
								c->expanded = true;
							} else if (tv->action == TVE_COLLAPSE) {
								c->expanded = false;
							}
						}
						break;
					}
				}
				break;
			}
		}
		break;
	}

	return FALSE;
}

#define MAX_CONTRIBUTORS_LENGTH 2048

static INT_PTR CALLBACK ErrorLogProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	TCHAR *err;

	bool handled;
	INT_PTR v = commonproc(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return v;
	}

	switch (msg) {
	case WM_COMMAND:
		if (wParam == IDOK) {
			CustomDialogClose(hDlg, -1);
			return TRUE;
		} else if (wParam == IDC_ERRORLOGCLEAR) {
			error_log (NULL);
			CustomDialogClose(hDlg, 0);
			return TRUE;
		}
		break;
	case WM_INITDIALOG:
		{
			err = get_error_log ();
			if (err == NULL)
				return FALSE;
			HWND list = GetDlgItem(hDlg, IDC_ERRORLOGMESSAGE);
			SubclassListViewControl(list);

			LV_COLUMN lvcolumn;
			RECT r;
			GetClientRect(list, &r);
			lvcolumn.mask = LVCF_WIDTH;
			lvcolumn.mask = LVCF_FMT | LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
			lvcolumn.iSubItem = 0;
			lvcolumn.fmt = LVCFMT_LEFT;
			lvcolumn.pszText = _T("");
			lvcolumn.cx = r.right - r.left;
			ListView_InsertColumn(list, 0, &lvcolumn);

			TCHAR *s = err;
			int cnt = 0;
			while (*s) {
				TCHAR *se = _tcschr(s, '\n');
				if (se) {
					*se = 0;
				}
				LV_ITEM lvstruct;
				lvstruct.mask = LVIF_TEXT | LVIF_PARAM;
				lvstruct.pszText = s;
				lvstruct.lParam = 0;
				lvstruct.iItem = cnt;
				lvstruct.iSubItem = 0;
				ListView_InsertItem(list, &lvstruct);
				if (!se) {
					break;
				}
				cnt++;
				s = se + 1;
			}
			xfree(err);
			return TRUE;
		}
	}
	return FALSE;
}

static TCHAR szContributors1[MAX_CONTRIBUTORS_LENGTH];
static TCHAR szContributors2[MAX_CONTRIBUTORS_LENGTH];
static TCHAR szContributors[MAX_CONTRIBUTORS_LENGTH * 2];

static INT_PTR CALLBACK ContributorsProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	bool handled;
	INT_PTR v = commonproc(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return v;
	}

	switch (msg) {
	case WM_DESTROY:
		PostQuitMessage(0);
		return TRUE;
	case WM_CLOSE:
		CustomDialogClose(hDlg, 0);
		return TRUE;
	case WM_COMMAND:
		if (wParam == ID_OK) {
			CustomDialogClose(hDlg, -1);
			return TRUE;
		}
		break;
	case WM_INITDIALOG:
		{
			HWND list = GetDlgItem(hDlg, IDC_CONTRIBUTORS);
			SubclassListViewControl(list);

			WIN32GUI_LoadUIString(IDS_CONTRIBUTORS1, szContributors1, MAX_CONTRIBUTORS_LENGTH);
			WIN32GUI_LoadUIString(IDS_CONTRIBUTORS2, szContributors2, MAX_CONTRIBUTORS_LENGTH);
			_stprintf (szContributors, _T("%s%s"), szContributors1, szContributors2);

			LV_COLUMN lvcolumn;
			RECT r;
			GetClientRect(list, &r);
			lvcolumn.mask = LVCF_WIDTH;
			lvcolumn.mask = LVCF_FMT | LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
			lvcolumn.iSubItem = 0;
			lvcolumn.fmt = LVCFMT_LEFT;
			lvcolumn.pszText = _T("");
			lvcolumn.cx = r.right - r.left;
			ListView_InsertColumn(list, 0, &lvcolumn);
						
			TCHAR *s = szContributors;
			int cnt = 0;
			while (*s) {
				TCHAR *se = _tcschr(s, '\n');
				if (se) {
					*se = 0;
				}
				LV_ITEM lvstruct;
				lvstruct.mask = LVIF_TEXT | LVIF_PARAM;
				lvstruct.pszText = s;
				lvstruct.lParam = 0;
				lvstruct.iItem = cnt;
				lvstruct.iSubItem = 0;
				ListView_InsertItem(list, &lvstruct);
				if (!se) {
					break;
				}
				cnt++;
				s = se + 1;
			}
			return TRUE;
		}
	}
	return FALSE;
}

static void DisplayContributors (HWND hDlg)
{
	CustomCreateDialogBox(IDD_CONTRIBUTORS, hDlg, ContributorsProc);
}

typedef struct url_info
{
	int   id;
	BOOL  state;
	const TCHAR *display;
	const TCHAR *url;
} urlinfo;

static urlinfo urls[] =
{
	{IDC_CLOANTOHOME, FALSE, _T("Cloanto's Amiga Forever"), _T("https://www.amigaforever.com/")},
	{IDC_AMIGAHOME, FALSE, _T("Amiga Corporation"), _T("https://amiga.com/")},
//	{IDC_PICASSOHOME, FALSE, _T("Picasso96 Home Page"), _T("http://www.picasso96.cogito.de/")},
//	{IDC_UAEHOME, FALSE, _T("UAE Home Page"), _T("http://www.amigaemulator.org/")},
	{IDC_WINUAEHOME, FALSE, _T("WinUAE Home Page"), _T("http://www.winuae.net/")},
//	{IDC_AIABHOME, FALSE, _T("AIAB"), _T("http://www.amigainabox.co.uk/")},
//	{IDC_THEROOTS, FALSE, _T("Back To The Roots"), _T("http://back2roots.abime.net/")},
	{IDC_ABIME, FALSE, _T("abime.net"), _T("http://www.abime.net/")},
	{IDC_CAPS, FALSE, _T("SPS"), _T("http://www.softpres.org/")},
//	{IDC_AMIGASYS, FALSE, _T("AmigaSYS"), _T("http://www.amigasys.com/")},
	{IDC_AMIKIT, FALSE, _T("AmiKit"), _T("http://amikit.amiga.sk/")},
	{ -1, FALSE, NULL, NULL }
};

static void url_handler (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	POINT point;
	point.x = LOWORD (lParam);
	point.y = HIWORD (lParam);

	for (int i = 0; urls[i].id >= 0; i++)
	{
		RECT rect;
		GetWindowRect (GetDlgItem(hDlg, urls[i].id), &rect);
		ScreenToClient (hDlg, (POINT*)&rect);
		ScreenToClient (hDlg, (POINT*)&rect.right);
		if (PtInRect (&rect, point))
		{
			if(msg == WM_LBUTTONDOWN)
			{
				ShellExecute (NULL, NULL, urls[i].url , NULL, NULL, SW_SHOWNORMAL);
				SetCursor(LoadCursor(NULL, IDC_ARROW));
			}
		}
	}
}

static void setac (HWND hDlg, int id)
{
	SHAutoComplete (GetDlgItem (hDlg, id), SHACF_FILESYSTEM | SHACF_AUTOAPPEND_FORCE_ON | SHACF_AUTOSUGGEST_FORCE_ON | SHACF_USETAB);
}
static void setautocomplete (HWND hDlg, int id)
{
	HWND item = FindWindowEx (GetDlgItem (hDlg, id), NULL, _T("Edit"), NULL);
	if (item)
		SHAutoComplete (item, SHACF_FILESYSTEM | SHACF_AUTOAPPEND_FORCE_ON | SHACF_AUTOSUGGEST_FORCE_ON | SHACF_USETAB);
}
static void setmultiautocomplete (HWND hDlg, int *ids)
{
	int i;
	for (i = 0; ids[i] >= 0; i++)
		setautocomplete (hDlg, ids[i]);
}

static void wsetpath (HWND hDlg, const TCHAR *name, DWORD d, const TCHAR *def)
{
	TCHAR tmp[MAX_DPATH];

	_tcscpy (tmp, def);
	fetch_path (name, tmp, sizeof (tmp) / sizeof (TCHAR));
	SetDlgItemText (hDlg, d, tmp);
}

static void values_to_pathsdialog (HWND hDlg)
{
	wsetpath(hDlg, _T("KickstartPath"), IDC_PATHS_ROM, _T("ROMs"));
	wsetpath(hDlg, _T("ConfigurationPath"), IDC_PATHS_CONFIG, _T("Configurations"));
	wsetpath(hDlg, _T("NVRAMPath"), IDC_PATHS_NVRAM, _T("NVRAMs"));
	wsetpath(hDlg, _T("ScreenshotPath"), IDC_PATHS_SCREENSHOT, _T("ScreenShots"));
	wsetpath(hDlg, _T("StatefilePath"), IDC_PATHS_SAVESTATE, _T("Savestates"));
	wsetpath(hDlg, _T("SaveimagePath"), IDC_PATHS_SAVEIMAGE, _T("SaveImages"));
	wsetpath(hDlg, _T("VideoPath"), IDC_PATHS_AVIOUTPUT, _T("Videos"));
	wsetpath(hDlg, _T("RipperPath"), IDC_PATHS_RIP, _T(".\\"));

	if (path_type == PATH_TYPE_CUSTOM) {
		SetDlgItemText(hDlg, IDC_CUSTOMDATAPATH, start_path_custom);
		ew(hDlg, IDC_PATHS_CUSTOMDATA, TRUE);
	} else {
		SetDlgItemText(hDlg, IDC_CUSTOMDATAPATH, start_path_data);
		ew(hDlg, IDC_PATHS_CUSTOMDATA, FALSE);
	}
	ew(hDlg, IDC_CUSTOMDATAPATH, FALSE);
}

static const TCHAR *pathnames[] = {
	_T("KickstartPath"),
	_T("ConfigurationPath"),
	_T("NVRAMPath"),
	_T("ScreenshotPath"),
	_T("StatefilePath"),
	_T("SaveimagePath"),
	_T("VideoPath"),
	_T("RipperPath"),
	_T("LuaPath"),
	_T("InputPath"),
	NULL
};

static void rewritepaths(void)
{
	for(int i = 0; pathnames[i]; i++) {
		TCHAR tmp[MAX_DPATH];
		fetch_path(pathnames[i], tmp, sizeof(tmp) / sizeof TCHAR);
		set_path(pathnames[i], tmp);
	}
}

static void resetregistry (void)
{
	regdeletetree(NULL, _T("DetectedROMs"));
	regdeletetree(NULL, _T("ConfigurationListView"));
	regdelete(NULL, _T("QuickStartMode"));
	regdelete(NULL, _T("ConfigFile"));
	regdelete(NULL, _T("ConfigFileHardware"));
	regdelete(NULL, _T("ConfigFileHost"));
	regdelete(NULL, _T("ConfigFileHardware_Auto"));
	regdelete(NULL, _T("ConfigFileHost_Auto"));
	regdelete(NULL, _T("ConfigurationPath"));
	regdelete(NULL, _T("NVRAMPath"));
	regdelete(NULL, _T("SaveimagePath"));
	regdelete(NULL, _T("ScreenshotPath"));
	regdelete(NULL, _T("StatefilePath"));
	regdelete(NULL, _T("VideoPath"));
	regdelete(NULL, _T("RipperPath"));
	regdelete(NULL, _T("QuickStartModel"));
	regdelete(NULL, _T("QuickStartConfiguration"));
	regdelete(NULL, _T("QuickStartCompatibility"));
	regdelete(NULL, _T("QuickStartHostConfig"));
	regdelete(NULL, _T("RecursiveROMScan"));
	regdelete(NULL, _T("ConfigurationCache"));
	regdelete(NULL, _T("ArtCache"));
	regdelete(NULL, _T("SaveImageOriginalPath"));
	regdelete(NULL, _T("RelativePaths"));
	regdelete(NULL, _T("DirectDraw_Secondary"));
	regdelete(NULL, _T("ShownsupportedModes"));
	regdelete(NULL, _T("ArtImageCount"));
	regdelete(NULL, _T("ArtImageWidth"));
	regdelete(NULL, _T("KeySwapBackslashF11"));
}

#include "zip.h"

static void copylog (const TCHAR *name, const TCHAR *path, FILE *f)
{
	FILE *s;

	s = my_opentext (path);
	if (s) {
		fputws (_T("\n"), f);
		fputws (name, f);
		fputws (_T(":\n"), f);
		fputws (_T("\n"), f);
		for (;;) {
			TCHAR buf[MAX_DPATH];
			if (!fgetws (buf, sizeof buf / sizeof (TCHAR), s))
				break;
			fputws (buf, f);
		}
		fclose (s);
	}
}
static void saveconfig (FILE *f)
{
	size_t len;
	uae_u8 *s;
	
	s = save_configuration (&len, true);
	if (!s)
		return;
	TCHAR *c = utf8u ((char*)s);
	fputws (c, f);
	xfree (c);
	xfree (s);
}

static void zipdate(zip_fileinfo *zi)
{
	SYSTEMTIME st;
	FILETIME ft;
	WORD dosdate, dostime;

	memset(zi, 0, sizeof zip_fileinfo);

	GetLocalTime (&st);
	SystemTimeToFileTime (&st, &ft);
	FileTimeToDosDateTime(&ft, &dosdate, &dostime);
	zi->dosDate = (dosdate << 16) | dostime;
}

static void ziplog(const char *name, const TCHAR *path, zipFile zf)
{
	zip_fileinfo zi;
	FILE *s;
	
	s = my_opentext (path);
	if (s) {
		zipdate(&zi);
		if (zipOpenNewFileInZip(zf, name, &zi, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_DEFAULT_COMPRESSION) == ZIP_OK) {
			for (;;) {
				TCHAR buf[MAX_DPATH];
				if (!fgetws (buf, sizeof buf / sizeof (TCHAR), s))
					break;
				zipWriteInFileInZip(zf, buf, uaetcslen(buf) * sizeof TCHAR);
			}
			zipCloseFileInZip(zf);
		}
		fclose(s);
	}
}
static void zipconfig(const char *name, zipFile zf)
{
	size_t len;
	uae_u8 *s;
	zip_fileinfo zi;
	
	s = save_configuration (&len, true);
	if (!s)
		return;
	zipdate(&zi);
	if (zipOpenNewFileInZip(zf, name, &zi, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_DEFAULT_COMPRESSION) == ZIP_OK) {
		zipWriteInFileInZip(zf, s, (unsigned int)len);
		zipCloseFileInZip(zf);
	}
	xfree(s);
}

static void savelog (HWND hDlg, int all)
{
	TCHAR tmp[MAX_DPATH], tmp2[MAX_DPATH];
	TCHAR name[MAX_DPATH];
	tmp[0] = 0;

	_stprintf(name, _T("winuae%s_%s_%d.%d.%d.%s"),
#ifdef _WIN64
		_T("64"),
#else
		_T(""),
#endif
		all ? _T("debug") : _T("config"),
		UAEMAJOR, UAEMINOR, UAESUBREV,
		all ? _T("zip") : _T("txt"));

	if (all) {
		OPENFILENAME openFileName = { 0 };

		flush_log ();
		_tcscpy(tmp, name);
		_tcscpy(tmp2, tmp);

		openFileName.lStructSize = sizeof (OPENFILENAME);
		openFileName.hwndOwner = hDlg;
		openFileName.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT |
			OFN_LONGNAMES | OFN_HIDEREADONLY | OFN_NOCHANGEDIR | OFN_ENABLESIZING;
		openFileName.lpstrFile = tmp;
		openFileName.lpstrFileTitle = tmp2;
		openFileName.nMaxFile = MAX_DPATH;
		openFileName.nMaxFileTitle = MAX_DPATH;
		openFileName.lpstrDefExt = _T("zip");

		// {8F1A2703-9FE0-468A-BBD7-D2336FD693E8}
		static const GUID logdialogguid = { 0x8f1a2703, 0x9fe0, 0x468a, { 0xbb, 0xd7, 0xd2, 0x33, 0x6f, 0xd6, 0x93, 0xe8 } };
		if (GetSaveFileName_2 (hDlg, &openFileName, &logdialogguid)) {
			zipFile zf = zipOpen(openFileName.lpstrFile, 0);
			if (zf) {
				ziplog("winuaebootlog.txt", bootlogpath, zf);
				ziplog("winuaelog.txt", logpath, zf);
				zipconfig("config.uae", zf);
			}
			zipClose(zf, NULL);
		}
	} else {
		if (GetTempPath (MAX_DPATH, tmp) <= 0)
			return;
		_tcscat(tmp, name);
		FILE *f = _tfopen (tmp, _T("wt, ccs=UTF-8"));
		saveconfig (f);
		fclose (f);
		ShellExecute (NULL, _T("open"), tmp, NULL, NULL, SW_SHOWNORMAL);
	}
#if 0		
	if (all) {
		f = _tfopen (tmp, _T("wt, ccs=UTF-8"));
		copylog (_T("winuaebootlog"), bootlogpath, f);
		copylog (_T("winuaelog"), logpath, f);
		fputws (_T("\n"), f);
		fputws (_T("configuration:\n"), f);
		fputws (_T("\n"), f);
		saveconfig (f);
		fclose (f);
	} else {
		_tcscat (tmp, _T("winuae_config.txt"));
		f = _tfopen (tmp, _T("wt, ccs=UTF-8"));
		saveconfig (f);
		fclose (f);
	}
#endif
}

static INT_PTR CALLBACK PathsDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	const GUID pathsguid = { 0x5674338c, 0x7a0b, 0x4565, { 0xbf, 0x75, 0x62, 0x8c, 0x80, 0x4a, 0xef, 0xf7 } };
	void create_afnewdir(int);
	static int recursive;
	static pathtype ptypes[10];
	static int numtypes;
	int val, selpath = 0;
	TCHAR tmp[MAX_DPATH];

	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch (msg)
	{
	case WM_INITDIALOG:
		recursive++;
		pages[PATHS_ID] = hDlg;
		setac(hDlg, IDC_PATHS_ROM);
		setac(hDlg, IDC_PATHS_CONFIG);
		setac(hDlg, IDC_PATHS_NVRAM);
		setac(hDlg, IDC_PATHS_SCREENSHOT);
		setac(hDlg, IDC_PATHS_SAVESTATE);
		setac(hDlg, IDC_PATHS_SAVEIMAGE);
		setac(hDlg, IDC_PATHS_AVIOUTPUT);
		setac(hDlg, IDC_PATHS_RIP);
		CheckDlgButton(hDlg, IDC_PATHS_RECURSIVEROMS, recursiveromscan);
		CheckDlgButton(hDlg, IDC_PATHS_CONFIGCACHE, configurationcache);
		CheckDlgButton(hDlg, IDC_PATHS_ARTCACHE, artcache);
		CheckDlgButton(hDlg, IDC_PATHS_SAVEIMAGEORIGINALPATH, saveimageoriginalpath);
		CheckDlgButton(hDlg, IDC_PATHS_RELATIVE, relativepaths);
		CheckDlgButton(hDlg, IDC_REGISTRYMODE, getregmode() != NULL);
		currentpage = PATHS_ID;
		ShowWindow (GetDlgItem (hDlg, IDC_RESETREGISTRY), FALSE);
		numtypes = 0;
		xSendDlgItemMessage (hDlg, IDC_PATHS_DEFAULTTYPE, CB_RESETCONTENT, 0, 0L);
		if (af_path_2005 & 1) {
			WIN32GUI_LoadUIString (IDS_DEFAULT_AF, tmp, sizeof tmp / sizeof (TCHAR));
			xSendDlgItemMessage (hDlg, IDC_PATHS_DEFAULTTYPE, CB_ADDSTRING, 0, (LPARAM)tmp);
			if (path_type == PATH_TYPE_NEWAF)
				selpath = numtypes;
			ptypes[numtypes++] = PATH_TYPE_NEWAF;
		}
		if (start_path_new1[0]) {
			WIN32GUI_LoadUIString (IDS_DEFAULT_NEWWINUAE, tmp, sizeof tmp / sizeof (TCHAR));
			xSendDlgItemMessage (hDlg, IDC_PATHS_DEFAULTTYPE, CB_ADDSTRING, 0, (LPARAM)tmp);
			if (path_type == PATH_TYPE_NEWWINUAE)
				selpath = numtypes;
			ptypes[numtypes++] = PATH_TYPE_NEWWINUAE;
		}
		if ((af_path_2005 & 3) == 2) {
			xSendDlgItemMessage (hDlg, IDC_PATHS_DEFAULTTYPE, CB_ADDSTRING, 0, (LPARAM)_T("AmigaForeverData"));
			if (path_type == PATH_TYPE_AMIGAFOREVERDATA)
				selpath = numtypes;
			ptypes[numtypes++] = PATH_TYPE_AMIGAFOREVERDATA;
		}
		WIN32GUI_LoadUIString (IDS_DEFAULT_WINUAE, tmp, sizeof tmp / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_PATHS_DEFAULTTYPE, CB_ADDSTRING, 0, (LPARAM)tmp);
		if (path_type == PATH_TYPE_WINUAE || path_type == PATH_TYPE_DEFAULT)
			selpath = numtypes;
		ptypes[numtypes++] = PATH_TYPE_WINUAE;
		WIN32GUI_LoadUIString(IDS_DEFAULT_WINUAECUSTOM, tmp, sizeof tmp / sizeof(TCHAR));
		xSendDlgItemMessage(hDlg, IDC_PATHS_DEFAULTTYPE, CB_ADDSTRING, 0, (LPARAM)tmp);
		if (path_type == PATH_TYPE_CUSTOM)
			selpath = numtypes;
		ptypes[numtypes++] = PATH_TYPE_CUSTOM;
		xSendDlgItemMessage (hDlg, IDC_PATHS_DEFAULTTYPE, CB_SETCURSEL, selpath, 0);
		EnableWindow (GetDlgItem (hDlg, IDC_PATHS_DEFAULTTYPE), numtypes > 0 ? TRUE : FALSE);
		SetWindowText (GetDlgItem (hDlg, IDC_LOGPATH), bootlogpath);
		xSendDlgItemMessage (hDlg, IDC_LOGSELECT, CB_RESETCONTENT, 0, 0);
		xSendDlgItemMessage (hDlg, IDC_LOGSELECT, CB_ADDSTRING, 0, (LPARAM)_T("winuaebootlog.txt"));
		xSendDlgItemMessage (hDlg, IDC_LOGSELECT, CB_ADDSTRING, 0, (LPARAM)_T("winuaelog.txt"));
		WIN32GUI_LoadUIString (IDS_CURRENT_CONFIGURATION, tmp, sizeof tmp / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_LOGSELECT, CB_ADDSTRING, 0, (LPARAM)tmp);
		xSendDlgItemMessage (hDlg, IDC_LOGSELECT, CB_SETCURSEL, 0, 0);
		CheckDlgButton (hDlg, IDC_LOGENABLE, winuaelog_temporary_enable || (full_property_sheet == 0 && currprefs.win32_logfile));
		ew (hDlg, IDC_LOGENABLE, winuaelog_temporary_enable == false && full_property_sheet);
		ew(hDlg, IDC_CUSTOMDATAPATH, selpath == PATH_TYPE_CUSTOM);
		extern int consoleopen;
		if (consoleopen || !full_property_sheet) {
			CheckDlgButton (hDlg, IDC_LOGENABLE2, consoleopen ? TRUE : FALSE);
			ew (hDlg, IDC_LOGENABLE2, FALSE);
		}
		values_to_pathsdialog (hDlg);
		recursive--;
		return TRUE;

	case WM_COMMAND:
		if (recursive > 0)
			break;
		recursive++;
		if (HIWORD (wParam) == CBN_SELCHANGE || HIWORD (wParam) == CBN_KILLFOCUS)  {
			switch (LOWORD (wParam))
			{
				case IDC_LOGSELECT:
					val = xSendDlgItemMessage (hDlg, IDC_LOGSELECT, CB_GETCURSEL, 0, 0L);
					if (val == 0) {
						SetWindowText (GetDlgItem (hDlg, IDC_LOGPATH), bootlogpath);
						ew (hDlg, IDC_LOGOPEN, bootlogpath[0]);
					} else if (val == 1) {
						SetWindowText (GetDlgItem (hDlg, IDC_LOGPATH), logpath);
						ew (hDlg, IDC_LOGOPEN, logpath[0]);
					} else if (val == 2) {
						SetWindowText (GetDlgItem (hDlg, IDC_LOGPATH), _T("Configuration"));
						ew (hDlg, IDC_LOGOPEN, TRUE);
					}
				break;
			}
		} else {

			switch (LOWORD (wParam))
			{
			case IDC_REGISTRYMODE:
				bool switchreginimode(void);
				switchreginimode();
				CheckDlgButton(hDlg, IDC_REGISTRYMODE, getregmode() != NULL);
				break;
			case IDC_LOGSAVE:
				savelog (hDlg, 1);
				break;
			case IDC_LOGENABLE:
				winuaelog_temporary_enable = ischecked (hDlg, IDC_LOGENABLE);
				break;
			case IDC_LOGENABLE2:
				extern int console_logging;
				console_logging = 1;
				break;
			case IDC_LOGOPEN:
				flush_log ();
				val = xSendDlgItemMessage (hDlg, IDC_LOGSELECT, CB_GETCURSEL, 0, 0L);
				if (val == 0) {
					if (bootlogpath[0])
						ShellExecute (NULL, _T("open"), bootlogpath, NULL, NULL, SW_SHOWNORMAL);
				} else if (val == 1) {
					if (logpath[0])
						ShellExecute (NULL, _T("open"), logpath, NULL, NULL, SW_SHOWNORMAL);
				} else if (val == 2) {
					savelog (hDlg, 0);
				}
				break;
			case IDC_PATHS_ROMS:
				fetch_path (_T("KickstartPath"), tmp, sizeof (tmp) / sizeof (TCHAR));
				if (DirectorySelection (hDlg, &pathsguid, tmp)) {
					load_keyring (&workprefs, NULL);
					set_path (_T("KickstartPath"), tmp);
					if (!scan_roms (hDlg, 1))
						gui_message_id (IDS_ROMSCANNOROMS);
					values_to_pathsdialog (hDlg);
				}
				break;
			case IDC_PATHS_ROM:
				GetWindowText (GetDlgItem (hDlg, IDC_PATHS_ROM), tmp, sizeof (tmp) / sizeof (TCHAR));
				set_path (_T("KickstartPath"), tmp);
				break;
			case IDC_PATHS_CONFIGS:
				fetch_path (_T("ConfigurationPath"), tmp, sizeof (tmp) / sizeof (TCHAR));
				if (DirectorySelection (hDlg, &pathsguid, tmp)) {
					set_path (_T("ConfigurationPath"), tmp);
					values_to_pathsdialog (hDlg);
					FreeConfigStore ();
				}
				break;
			case IDC_PATHS_CONFIG:
				GetWindowText (GetDlgItem (hDlg, IDC_PATHS_CONFIG), tmp, sizeof (tmp) / sizeof (TCHAR));
				set_path (_T("ConfigurationPath"), tmp);
				FreeConfigStore ();
				break;
			case IDC_PATHS_NVRAMS:
				fetch_path (_T("NVRAMPath"), tmp, sizeof (tmp) / sizeof (TCHAR));
				if (DirectorySelection (hDlg, &pathsguid, tmp)) {
					set_path (_T("NVRAMPath"), tmp);
					values_to_pathsdialog (hDlg);
				}
				break;
			case IDC_PATHS_NVRAM:
				GetWindowText(GetDlgItem(hDlg, IDC_PATHS_NVRAM), tmp, sizeof(tmp) / sizeof(TCHAR));
				set_path(_T("NVRAMPath"), tmp);
				break;
			case IDC_PATHS_SCREENSHOTS:
				fetch_path (_T("ScreenshotPath"), tmp, sizeof (tmp) / sizeof (TCHAR));
				if (DirectorySelection (hDlg, &pathsguid, tmp)) {
					set_path (_T("ScreenshotPath"), tmp);
					values_to_pathsdialog (hDlg);
				}
				break;
			case IDC_PATHS_SCREENSHOT:
				GetWindowText (GetDlgItem (hDlg, IDC_PATHS_SCREENSHOT), tmp, sizeof (tmp) / sizeof (TCHAR));
				set_path (_T("ScreenshotPath"), tmp);
				break;
			case IDC_PATHS_SAVESTATES:
				fetch_path (_T("StatefilePath"), tmp, sizeof (tmp) / sizeof (TCHAR));
				if (DirectorySelection (hDlg, &pathsguid, tmp)) {
					set_path (_T("StatefilePath"), tmp);
					values_to_pathsdialog (hDlg);
				}
				break;
			case IDC_PATHS_SAVESTATE:
				GetWindowText (GetDlgItem (hDlg, IDC_PATHS_SAVESTATE), tmp, sizeof (tmp) / sizeof (TCHAR));
				set_path (_T("StatefilePath"), tmp);
				break;
			case IDC_PATHS_SAVEIMAGES:
				fetch_path (_T("SaveimagePath"), tmp, sizeof (tmp) / sizeof (TCHAR));
				if (DirectorySelection (hDlg, &pathsguid, tmp)) {
					set_path (_T("SaveimagePath"), tmp);
					values_to_pathsdialog (hDlg);
				}
				break;
			case IDC_PATHS_SAVEIMAGE:
				GetWindowText (GetDlgItem (hDlg, IDC_PATHS_SAVEIMAGE), tmp, sizeof (tmp) / sizeof (TCHAR));
				set_path (_T("SaveimagePath"), tmp);
				break;
			case IDC_PATHS_AVIOUTPUTS:
				fetch_path (_T("VideoPath"), tmp, sizeof (tmp) / sizeof (TCHAR));
				if (DirectorySelection (hDlg, &pathsguid, tmp)) {
					set_path (_T("VideoPath"), tmp);
					values_to_pathsdialog (hDlg);
				}
				break;
			case IDC_PATHS_RIPS:
				fetch_path (_T("RipperPath"), tmp, sizeof (tmp) / sizeof (TCHAR));
				if (DirectorySelection (hDlg, &pathsguid, tmp)) {
					set_path (_T("RipperPath"), tmp);
					values_to_pathsdialog (hDlg);
				}
				break;
			case IDC_PATHS_AVIOUTPUT:
				GetWindowText (GetDlgItem (hDlg, IDC_PATHS_AVIOUTPUT), tmp, sizeof (tmp) / sizeof (TCHAR));
				set_path (_T("VideoPath"), tmp);
				break;
			case IDC_PATHS_RIP:
				GetWindowText (GetDlgItem (hDlg, IDC_PATHS_RIP), tmp, sizeof (tmp) / sizeof (TCHAR));
				set_path (_T("RipperPath"), tmp);
				break;
			case IDC_PATHS_CUSTOMDATA:
				_tcscpy(tmp, start_path_custom);
				if (DirectorySelection(hDlg, &pathsguid, tmp)) {
					fullpath(tmp, sizeof(tmp) / sizeof(TCHAR), false);
					fixtrailing(tmp);
					_tcscpy(start_path_custom, tmp);
					values_to_pathsdialog(hDlg);
				}
				break;
			case IDC_PATHS_DEFAULT:
				val = xSendDlgItemMessage (hDlg, IDC_PATHS_DEFAULTTYPE, CB_GETCURSEL, 0, 0L);
				if (val != CB_ERR && val >= 0 && val < numtypes) {
					val = ptypes[val];
					if (val == PATH_TYPE_WINUAE) {
						_tcscpy (start_path_data, start_path_exe);
						path_type = PATH_TYPE_WINUAE;
					} else if (val == PATH_TYPE_NEWWINUAE && start_path_new1[0]) {
						_tcscpy (start_path_data, start_path_new1);
						path_type = PATH_TYPE_NEWWINUAE;
						create_afnewdir(0);
					} else if (val == PATH_TYPE_NEWAF && start_path_new1[0]) {
						path_type = PATH_TYPE_NEWAF;
						create_afnewdir(0);
						_tcscpy (start_path_data, start_path_new1);
					} else if (val == PATH_TYPE_AMIGAFOREVERDATA && start_path_new2[0]) {
						path_type = PATH_TYPE_AMIGAFOREVERDATA;
						_tcscpy (start_path_data, start_path_new1);
					} else if (val == PATH_TYPE_CUSTOM) {
						path_type = PATH_TYPE_CUSTOM;
						if (!start_path_custom[0]) {
							_tcscpy(start_path_custom, start_path_exe);
						}
						_tcscpy(start_path_data, start_path_custom);
					}
					SetCurrentDirectory (start_path_data);
					setpathmode (path_type);
					set_path(_T("KickstartPath"), NULL, path_type);
					set_path(_T("ConfigurationPath"), NULL, path_type);
					set_path(_T("NVRAMPath"), NULL, path_type);
					set_path(_T("ScreenshotPath"), NULL, path_type);
					set_path(_T("StatefilePath"), NULL, path_type);
					set_path(_T("SaveimagePath"), NULL, path_type);
					set_path(_T("VideoPath"), NULL, path_type);
					set_path(_T("RipperPath"), NULL, path_type);
					set_path(_T("InputPath"), NULL, path_type);
					values_to_pathsdialog (hDlg);
					FreeConfigStore ();
				}
				break;
			case IDC_ROM_RESCAN:
				scan_roms (hDlg, 1);
				break;
			case IDC_RESETREGISTRY:
				resetregistry ();
				break;
			case IDC_RESETDISKHISTORY:
				reset_disk_history ();
				break;
			case IDC_PATHS_RECURSIVEROMS:
				recursiveromscan = ischecked (hDlg, IDC_PATHS_RECURSIVEROMS) ? 2 : 0;
				regsetint (NULL, _T("RecursiveROMScan"), recursiveromscan);
				break;
			case IDC_PATHS_CONFIGCACHE:
				configurationcache = ischecked(hDlg, IDC_PATHS_CONFIGCACHE) ? 1 : 0;
				regsetint(NULL, _T("ConfigurationCache"), configurationcache);
				deleteconfigcache();
				break;
			case IDC_PATHS_ARTCACHE:
				artcache = ischecked(hDlg, IDC_PATHS_ARTCACHE) ? 1 : 0;
				regsetint(NULL, _T("ArtCache"), artcache);
				deleteconfigcache();
				break;
			case IDC_PATHS_SAVEIMAGEORIGINALPATH:
				saveimageoriginalpath = ischecked (hDlg, IDC_PATHS_SAVEIMAGEORIGINALPATH) ? 1 : 0;
				regsetint (NULL, _T("SaveImageOriginalPath"), saveimageoriginalpath);
				break;
			case IDC_PATHS_RELATIVE:
				relativepaths = ischecked (hDlg, IDC_PATHS_RELATIVE) ? 1 : 0;
				regsetint (NULL, _T("RelativePaths"), relativepaths);
				rewritepaths();
				break;
			}
		}
		recursive--;
	}
	return FALSE;
}

struct amigamodels {
	int compalevels;
	int id;
	int resetlevels[6];
};
static struct amigamodels amodels[] = {
	{ 4, IDS_QS_MODEL_A500, { 0, 0, 0, 0, 0, 0 } }, // "Amiga 500"
	{ 4, IDS_QS_MODEL_A500P,  { 0, 0, 0, 0, 0, 0 } }, // "Amiga 500+"
	{ 4, IDS_QS_MODEL_A600,  { 0, 0, 1, 0, 0, 0 } }, // "Amiga 600"
	{ 4, IDS_QS_MODEL_A1000,  { 0, 0, 0, 0, 0, 0 } }, // "Amiga 1000"
	{ 5, IDS_QS_MODEL_A1200,  { 0, 1, 2, 3, 4, 5 } }, // "Amiga 1200"
	{ 2, IDS_QS_MODEL_A3000,  { 0, 0, 0, 0, 0, 0 } }, // "Amiga 3000"
	{ 1, IDS_QS_MODEL_A4000,  { 0, 0, 1, 0, 0, 0 } }, // "Amiga 4000"
	{ 0, }, //{ 1, IDS_QS_MODEL_A4000T }, // "Amiga 4000T"
	{ 4, IDS_QS_MODEL_CD32,  { 0, 0, 1, 0, 0, 0 } }, // "CD32"
	{ 4, IDS_QS_MODEL_CDTV,  { 0, 0, 1, 0, 0, 0 } }, // "CDTV"
	{ 4, IDS_QS_MODEL_ALG,  { 0, 0, 0, 0, 0, 0 } }, // "American Laser Games"
	{ 4, IDS_QS_MODEL_ARCADIA,  { 0, 0, 0, 0, 0, 0 } }, // "Arcadia"
	{ 1, IDS_QS_MODEL_MACROSYSTEM,  { 0, 0, 0, 0, 0, 0 } },
	{ 1, IDS_QS_MODEL_UAE,  { 0, 0, 0, 0, 0, 0 } }, // "Expanded UAE example configuration"
	{ -1 }
};

static void enable_for_quickstart (HWND hDlg)
{
	int v = quickstart_ok && quickstart_ok_floppy ? TRUE : FALSE;
	ew (guiDlg, IDC_RESETAMIGA, !full_property_sheet ? TRUE : FALSE);
	ShowWindow (GetDlgItem (hDlg, IDC_QUICKSTART_SETCONFIG), quickstart ? SW_HIDE : SW_SHOW);
}

static void load_quickstart (HWND hDlg, int romcheck)
{
	bool cdmodel = quickstart_model == 8 || quickstart_model == 9;
	bool pcmodel = quickstart_model == 12;
	ew (guiDlg, IDC_RESETAMIGA, FALSE);
	workprefs.nr_floppies = quickstart_floppy;
	workprefs.ntscmode = quickstart_ntsc != 0;
	quickstart_ok = built_in_prefs (&workprefs, quickstart_model, quickstart_conf, quickstart_compa, romcheck);
	quickstart_cd = workprefs.floppyslots[1].dfxtype == DRV_NONE && cdmodel;
	// DF0: HD->DD
	if (quickstart_model <= 4) {
		if (quickstart_floppytype[0] == 1) {
			quickstart_floppytype[0] = 0;
		}
	}
	for (int i = 0; i < 2; i++) {
		if (cdmodel || pcmodel) {
			quickstart_floppytype[i] = DRV_NONE;
			quickstart_floppysubtype[i] = 0;
			quickstart_floppysubtypeid[i][0] = 0;
		} else {
			if (quickstart_floppy < 1) {
				quickstart_floppy = 1;
			}
			if (i == 0 && quickstart_floppytype[i] != DRV_35_DD && quickstart_floppytype[i] != DRV_35_HD) {
				quickstart_floppytype[i] = DRV_35_DD;
				quickstart_floppysubtype[i] = 0;
				quickstart_floppysubtypeid[i][0] = 0;
			}
		}
		if (i < quickstart_floppy) {
			workprefs.floppyslots[i].dfxtype = quickstart_floppytype[i];
			workprefs.floppyslots[i].dfxsubtype = quickstart_floppysubtype[i];
			_tcscpy(workprefs.floppyslots[i].dfxsubtypeid, quickstart_floppysubtypeid[i]);
		} else {
			workprefs.floppyslots[i].dfxtype = DRV_NONE;
			workprefs.floppyslots[i].dfxsubtype = 0;
			workprefs.floppyslots[i].dfxsubtypeid[0] = 0;
		}
	}
	floppybridge_init(&workprefs);
	enable_for_quickstart (hDlg);
	addfloppytype (hDlg, 0);
	addfloppytype (hDlg, 1);
	addfloppyhistory (hDlg);
	config_filename[0] = 0;
	setguititle (NULL);
	target_setdefaultstatefilename(config_filename);
}

static void quickstarthost (HWND hDlg, TCHAR *name)
{
	int type = CONFIG_TYPE_HOST;
	TCHAR tmp[MAX_DPATH];

	if (getconfigstorefrompath (name, tmp, CONFIG_TYPE_HOST)) {
		if (cfgfile_load (&workprefs, tmp, &type, 1, 0))
			workprefs.start_gui = 1;
	}
}

static void init_quickstartdlg_tooltip (HWND hDlg, TCHAR *tt)
{
	TOOLINFO ti;

	ti.cbSize = sizeof (TOOLINFO);
	ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
	ti.hwnd = hDlg;
	ti.hinst = hInst;
	ti.uId = (UINT_PTR)GetDlgItem (hDlg, IDC_QUICKSTART_CONFIGURATION);
	ti.lpszText = tt;
	SendMessage (ToolTipHWND, TTM_DELTOOL, 0, (LPARAM) (LPTOOLINFO) &ti);
	if (!tt)
		return;
	SendMessage (ToolTipHWND, TTM_ADDTOOL, 0, (LPARAM) (LPTOOLINFO) &ti);
}

static void init_quickstartdlg(HWND hDlg, bool initial)
{
	static int firsttime;
	int i, j, idx, idx2, qssize, total;
	TCHAR tmp1[2 * MAX_DPATH], tmp2[MAX_DPATH], hostconf[MAX_DPATH];
	TCHAR *p1, *p2;

	firstautoloadconfig = false;
	qssize = sizeof (tmp1) / sizeof (TCHAR);
	regquerystr (NULL, _T("QuickStartHostConfig"), hostconf, &qssize);
	if (firsttime == 0 && workprefs.start_gui) {
		int size;
		regqueryint(NULL, _T("QuickStartModel"), &quickstart_model);
		regqueryint(NULL, _T("QuickStartConfiguration"), &quickstart_conf);
		quickstart_model_confstore[quickstart_model] = quickstart_conf;
		regqueryint(NULL, _T("QuickStartCompatibility"), &quickstart_compa);
		regqueryint(NULL, _T("QuickStartFloppies"), &quickstart_floppy);
		regqueryint(NULL, _T("QuickStartDF0Type"), &quickstart_floppytype[0]);
		regqueryint(NULL, _T("QuickStartDF1Type"), &quickstart_floppytype[1]);
		regqueryint(NULL, _T("QuickStartDF0SubType"), &quickstart_floppysubtype[0]);
		regqueryint(NULL, _T("QuickStartDF1SubType"), &quickstart_floppysubtype[1]);
		size = 30;
		regquerystr(NULL, _T("QuickStartDF0SubTypeID"), quickstart_floppysubtypeid[0], &size);
		size = 30;
		regquerystr(NULL, _T("QuickStartDF1SubTypeID"), quickstart_floppysubtypeid[1], &size);
		regqueryint(NULL, _T("QuickStartCDType"), &quickstart_cdtype);
		size = sizeof quickstart_cddrive / sizeof (TCHAR);
		regquerystr(NULL, _T("QuickStartCDDrive"), quickstart_cddrive, &size);
		regqueryint(NULL, _T("QuickStartNTSC"), &quickstart_ntsc);
		if (quickstart) {
			workprefs.floppyslots[0].df[0] = 0;
			workprefs.floppyslots[1].df[0] = 0;
			workprefs.floppyslots[2].df[0] = 0;
			workprefs.floppyslots[3].df[0] = 0;
			workprefs.cdslots[0].name[0] = 0;
			workprefs.cdslots[0].inuse = quickstart_cdtype > 0;
			load_quickstart (hDlg, 1);
			quickstarthost (hDlg, hostconf);
		}
	}

	CheckDlgButton (hDlg, IDC_QUICKSTARTMODE, quickstart);
	CheckDlgButton (hDlg, IDC_NTSC, quickstart_ntsc != 0);

	WIN32GUI_LoadUIString (IDS_QS_MODELS, tmp1, sizeof (tmp1) / sizeof (TCHAR));
	_tcscat (tmp1, _T("\n"));
	p1 = tmp1;
	xSendDlgItemMessage (hDlg, IDC_QUICKSTART_MODEL, CB_RESETCONTENT, 0, 0L);
	idx = idx2 = 0;
	i = 0;
	while (amodels[i].compalevels >= 0) {
		if (amodels[i].compalevels > 0) {
			p2 = _tcschr (p1, '\n');
			if (p2 && _tcslen (p2) > 0) {
				*p2++ = 0;
				xSendDlgItemMessage (hDlg, IDC_QUICKSTART_MODEL, CB_ADDSTRING, 0, (LPARAM)p1);
				p1 = p2;
			}
			if (i == quickstart_model)
				idx2 = idx;
			idx++;
		}
		i++;
	}
	xSendDlgItemMessage (hDlg, IDC_QUICKSTART_MODEL, CB_SETCURSEL, idx2, 0);

	total = 0;
	xSendDlgItemMessage (hDlg, IDC_QUICKSTART_CONFIGURATION, CB_RESETCONTENT, 0, 0L);
	if (amodels[quickstart_model].id == IDS_QS_MODEL_ARCADIA) {
		struct romlist **rl = getarcadiaroms(0);
		for (i = 0; rl[i]; i++) {
			xSendDlgItemMessage(hDlg, IDC_QUICKSTART_CONFIGURATION, CB_ADDSTRING, 0, (LPARAM)rl[i]->rd->name);
			total++;
		}
		xfree(rl);
	} else if (amodels[quickstart_model].id == IDS_QS_MODEL_ALG) {
		struct romlist **rl = getarcadiaroms(1);
		for (i = 0; rl[i]; i++) {
			xSendDlgItemMessage (hDlg, IDC_QUICKSTART_CONFIGURATION, CB_ADDSTRING, 0, (LPARAM)rl[i]->rd->name);
			total++;
		}
		xfree (rl);
	} else {
		WIN32GUI_LoadUIString (amodels[quickstart_model].id, tmp1, sizeof (tmp1) / sizeof (TCHAR));
		_tcscat (tmp1, _T("\n"));
		p1 = tmp1;
		init_quickstartdlg_tooltip (hDlg, 0);
		total = 0;
		for (;;) {
			p2 = _tcschr (p1, '\n');
			if (!p2)
				break;
			*p2++= 0;
			xSendDlgItemMessage (hDlg, IDC_QUICKSTART_CONFIGURATION, CB_ADDSTRING, 0, (LPARAM)p1);
			p1 = p2;
			p2 = _tcschr (p1, '\n');
			if (!p2)
				break;
			*p2++= 0;
			if (quickstart_conf == total && _tcslen (p1) > 0)
				init_quickstartdlg_tooltip (hDlg, p1);
			p1 = p2;
			total++;
		}
	}
	if (quickstart_conf >= total)
		quickstart_conf = 0;
	xSendDlgItemMessage (hDlg, IDC_QUICKSTART_CONFIGURATION, CB_SETCURSEL, quickstart_conf, 0);

	if (quickstart_compa >= amodels[quickstart_model].compalevels)
		quickstart_compa = 1;
	if (quickstart_compa >= amodels[quickstart_model].compalevels)
		quickstart_compa = 0;
	i = amodels[quickstart_model].compalevels;
	ew (hDlg, IDC_QUICKSTART_COMPATIBILITY, i > 1);
	xSendDlgItemMessage (hDlg, IDC_QUICKSTART_COMPATIBILITY, TBM_SETRANGE, TRUE, MAKELONG (0, i > 1 ? i - 1 : 1));
	xSendDlgItemMessage (hDlg, IDC_QUICKSTART_COMPATIBILITY, TBM_SETPAGESIZE, 0, 1);
	xSendDlgItemMessage (hDlg, IDC_QUICKSTART_COMPATIBILITY, TBM_SETPOS, TRUE, quickstart_compa);

	xSendDlgItemMessage (hDlg, IDC_QUICKSTART_HOSTCONFIG, CB_RESETCONTENT, 0, 0L);
	WIN32GUI_LoadUIString(IDS_CURRENT_HOST, tmp1, sizeof(tmp1) / sizeof(TCHAR));
	xSendDlgItemMessage(hDlg, IDC_QUICKSTART_HOSTCONFIG, CB_ADDSTRING, 0, (LPARAM)tmp1);
	WIN32GUI_LoadUIString(IDS_DEFAULT_HOST, tmp1, sizeof(tmp1) / sizeof(TCHAR));
	xSendDlgItemMessage(hDlg, IDC_QUICKSTART_HOSTCONFIG, CB_ADDSTRING, 0, (LPARAM)tmp1);
	idx = 0;
	j = 2;
	for (i = 0; i < configstoresize; i++) {
		if (configstore[i]->Type == CONFIG_TYPE_HOST) {
			_tcscpy (tmp2, configstore[i]->Path);
			_tcsncat (tmp2, configstore[i]->Name, MAX_DPATH - _tcslen(tmp2));
			if (!_tcscmp (tmp2, hostconf))
				idx = j;
			xSendDlgItemMessage (hDlg, IDC_QUICKSTART_HOSTCONFIG, CB_ADDSTRING, 0, (LPARAM)tmp2);
			j++;
		}
	}
	if (!_tcsicmp(hostconf, _T("Default Configuration"))) {
		idx = 0;
	}
	if (!_tcsicmp(hostconf, _T("Reset Configuration"))) {
		idx = 1;
	}
	xSendDlgItemMessage (hDlg, IDC_QUICKSTART_HOSTCONFIG, CB_SETCURSEL, idx, 0);
	regsetint (NULL, _T("QuickStartModel"), quickstart_model);
	regsetint (NULL, _T("QuickStartConfiguration"), quickstart_conf);
	regsetint (NULL, _T("QuickStartCompatibility"), quickstart_compa);

	if (quickstart && (initial || !firsttime)) {
		quickstarthost(hDlg, hostconf);
		if (idx == 0) {
			load_quickstart(hDlg, 0);
		} else if (idx == 1) {
			default_prefs(&workprefs, false, 0);
			load_quickstart(hDlg, 0);
		}
	}
	firsttime = 1;
}

static void floppytooltip (HWND hDlg, int num, uae_u32 crc32);
static void testimage (HWND hDlg, int num)
{
	int ret;
	int reload = 0;
	struct diskinfo di;
	int messageid = -1;
	TCHAR tmp[MAX_DPATH];

	floppytooltip (hDlg, num, 0);
	quickstart_ok_floppy = 0;
	if (workprefs.floppyslots[0].dfxtype < 0) {
		quickstart_ok_floppy = 1;
		return;
	}
	if (!workprefs.floppyslots[num].df[0])
		return;
	floppybridge_init(&workprefs);
	ret = DISK_examine_image (&workprefs, num, &di, false, NULL);
	if (!ret)
		return;
	floppytooltip (hDlg, num, di.imagecrc32);
	if (num > 0)
		return;
	if (!full_property_sheet)
		return;
	switch (ret)
	{
	case 10:
		quickstart_ok_floppy = 1;
		break;
	case 11:
		quickstart_ok_floppy = 1;
		if (quickstart_model != 1 && quickstart_model != 2 && quickstart_model != 4 &&
			quickstart_model != 5 && quickstart_model != 6 && quickstart_model != 8 && quickstart_model != 11) {
			quickstart_model = 4;
			messageid = IDS_IMGCHK_KS2;
			reload = 1;
		}
		break;
	case 12:
		quickstart_ok_floppy = 1;
		if (quickstart_model != 4 && quickstart_model != 8 && quickstart_model != 11) {
			quickstart_model = 4;
			messageid = IDS_IMGCHK_KS3;
			reload = 1;
		}
		break;
	case 4:
		messageid = IDS_IMGCHK_BOOTBLOCKNO;
		break;
	case 3:
		messageid = IDS_IMGCHK_BOOTBLOCKCRCERROR;
		break;
	case 2:
		messageid = IDS_IMGCHK_DAMAGED;
		break;
	}
	if (messageid > 0) {
		WIN32GUI_LoadUIString (messageid, tmp, sizeof (tmp) / sizeof (TCHAR));
		gui_message (tmp);
	}
	if (reload && quickstart) {
		load_quickstart(hDlg, 1);
		init_quickstartdlg(hDlg, false);
	}
}

static INT_PTR CALLBACK FloppyDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
static int diskselectmenu (HWND hDlg, WPARAM wParam);
static void addallfloppies (HWND hDlg);

#define BUTTONSPERFLOPPY 9
static const int floppybuttons[][BUTTONSPERFLOPPY] = {
	{ IDC_DF0TEXT,IDC_DF0,IDC_EJECT0,IDC_DF0TYPE,IDC_DF0WP,-1,IDC_SAVEIMAGE0,IDC_DF0ENABLE, IDC_INFO0 },
	{ IDC_DF1TEXT,IDC_DF1,IDC_EJECT1,IDC_DF1TYPE,IDC_DF1WP,-1,IDC_SAVEIMAGE1,IDC_DF1ENABLE, IDC_INFO1 },
	{ IDC_DF2TEXT,IDC_DF2,IDC_EJECT2,IDC_DF2TYPE,IDC_DF2WP,-1,IDC_SAVEIMAGE2,IDC_DF2ENABLE, IDC_INFO2 },
	{ IDC_DF3TEXT,IDC_DF3,IDC_EJECT3,IDC_DF3TYPE,IDC_DF3WP,-1,IDC_SAVEIMAGE3,IDC_DF3ENABLE, IDC_INFO3 }
};
static const int floppybuttonsq[][BUTTONSPERFLOPPY] = {
	{ IDC_DF0TEXTQ,IDC_DF0QQ,IDC_EJECT0Q,IDC_DF0TYPE,IDC_DF0WPQ,IDC_DF0WPTEXTQ,-1,IDC_DF0QENABLE, IDC_INFO0Q },
	{ IDC_DF1TEXTQ,IDC_DF1QQ,IDC_EJECT1Q,IDC_DF1TYPE,IDC_DF1WPQ,IDC_DF1WPTEXTQ,-1,IDC_DF1QENABLE, IDC_INFO1Q },
	{ -1,-1,-1,-1,-1,-1,-1,-1 },
	{ -1,-1,-1,-1,-1,-1,-1,-1 }
};

static int fromdfxtype(int num, int dfx, int subtype)
{
	if (currentpage == QUICKSTART_ID) {
		switch (dfx)
		{
		case DRV_35_DD:
			return 0;
		case DRV_35_HD:
			return 1;
		}
		if (dfx == DRV_FB) {
			return 2 + subtype;
		}
		return -1;
	}

	switch (dfx)
	{
	case DRV_35_DD:
		return 0;
	case DRV_35_HD:
		return 1;
	case DRV_525_SD:
		return 2;
	case DRV_525_DD:
		return 3;
	case DRV_35_DD_ESCOM:
		return 4;
	}
	if (num < 2) {
		if (dfx == DRV_FB) {
			return 5 + subtype;
		}
	} else {
		switch (dfx)
		{
		case DRV_PC_525_ONLY_40:
			return 5;
		case DRV_PC_525_40_80:
			return 6;
		case DRV_PC_35_ONLY_80:
			return 7;
		}
		if (dfx == DRV_FB) {
			return 8 + subtype;
		}
	}
	return -1;
}

static int todfxtype(int num, int dfx, int *subtype)
{
	*subtype = 0;
	if (currentpage == QUICKSTART_ID) {
		switch (dfx)
		{
		case 0:
			return DRV_35_DD;
		case 1:
			return DRV_35_HD;
		}
		if (dfx >= 2) {
			*subtype = dfx - 2;
			return DRV_FB;
		}
		return -1;
	}

	switch (dfx)
	{
	case 0:
		return DRV_35_DD;
	case 1:
		return DRV_35_HD;
	case 2:
		return DRV_525_SD;
	case 3:
		return DRV_525_DD;
	case 4:
		return DRV_35_DD_ESCOM;
	}
	if (num < 2) {
		if (dfx >= 5) {
			*subtype = dfx - 5;
			return DRV_FB;
		}
	} else {
		switch (dfx)
		{
		case 5:
			return DRV_PC_525_ONLY_40;
		case 6:
			return DRV_PC_525_40_80;
		case 7:
			return DRV_PC_35_ONLY_80;
		}
		if (dfx >= 8) {
			*subtype = dfx - 8;
			return DRV_FB;
		}
	}
	return -1;
}

static void setfloppytexts (HWND hDlg, int qs)
{
	SetDlgItemText (hDlg, IDC_DF0TEXT, workprefs.floppyslots[0].df);
	SetDlgItemText (hDlg, IDC_DF1TEXT, workprefs.floppyslots[1].df);
	SetDlgItemText (hDlg, IDC_DF2TEXT, workprefs.floppyslots[2].df);
	SetDlgItemText (hDlg, IDC_DF3TEXT, workprefs.floppyslots[3].df);
	SetDlgItemText (hDlg, IDC_DF0TEXTQ, workprefs.floppyslots[0].df);
	SetDlgItemText (hDlg, IDC_DF1TEXTQ, workprefs.floppyslots[1].df);
	if (!qs)
		addallfloppies (hDlg);
}

static void updatefloppytypes(HWND hDlg)
{
	TCHAR ft35dd[20], ft35hd[20], ft525sd[20], ft35ddescom[20];
	bool qs = currentpage == QUICKSTART_ID;

	WIN32GUI_LoadUIString(IDS_FLOPPYTYPE35DD, ft35dd, sizeof ft35dd / sizeof(TCHAR));
	WIN32GUI_LoadUIString(IDS_FLOPPYTYPE35HD, ft35hd, sizeof ft35hd / sizeof(TCHAR));
	WIN32GUI_LoadUIString(IDS_FLOPPYTYPE525SD, ft525sd, sizeof ft525sd / sizeof(TCHAR));
	WIN32GUI_LoadUIString(IDS_FLOPPYTYPE35DDESCOM, ft35ddescom, sizeof ft35ddescom / sizeof(TCHAR));

	for (int i = 0; i < (qs ? 2 : 4); i++) {
		int f_type;
		if (qs) {
			f_type = floppybuttonsq[i][3];
		} else {
			f_type = floppybuttons[i][3];
		}
		xSendDlgItemMessage(hDlg, f_type, CB_RESETCONTENT, 0, 0L);
		xSendDlgItemMessage(hDlg, f_type, CB_ADDSTRING, 0, (LPARAM)ft35dd);
		xSendDlgItemMessage(hDlg, f_type, CB_ADDSTRING, 0, (LPARAM)ft35hd);
		if (!qs) {
			xSendDlgItemMessage(hDlg, f_type, CB_ADDSTRING, 0, (LPARAM)ft525sd);
			xSendDlgItemMessage(hDlg, f_type, CB_ADDSTRING, 0, (LPARAM)_T("5.25\" (80)"));
			xSendDlgItemMessage(hDlg, f_type, CB_ADDSTRING, 0, (LPARAM)ft35ddescom);
			if (i >= 2) {
				xSendDlgItemMessage(hDlg, f_type, CB_ADDSTRING, 0, (LPARAM)_T("Bridgeboard 5.25\" 40"));
				xSendDlgItemMessage(hDlg, f_type, CB_ADDSTRING, 0, (LPARAM)_T("Bridgeboard 5.25\" 80"));
				xSendDlgItemMessage(hDlg, f_type, CB_ADDSTRING, 0, (LPARAM)_T("Bridgeboard 3.5\"  80"));
			}
		}
		if (floppybridge_available) {
			xSendDlgItemMessage(hDlg, f_type, CB_ADDSTRING, 0, (LPARAM)_T("Configure FloppyBridge"));
			for (int j = 0; j < bridgeprofiles.size(); j++) {
				FloppyBridgeAPI::FloppyBridgeProfileInformation fbpi = bridgeprofiles.at(j);
				TCHAR tmp[256];
				if (_tcslen(fbpi.name) < sizeof(tmp) - 10) {
					_stprintf(tmp, _T("FB: %s"), fbpi.name);
					xSendDlgItemMessage(hDlg, f_type, CB_ADDSTRING, 0, (LPARAM)tmp);
				}
			}
		}
		int nn;
		if (qs) {
			nn = fromdfxtype(i, quickstart_floppytype[i], quickstart_floppysubtype[i]);
		} else {
			nn = fromdfxtype(i, workprefs.floppyslots[i].dfxtype, workprefs.floppyslots[i].dfxsubtype);
		}
		xSendDlgItemMessage(hDlg, f_type, CB_SETCURSEL, nn, 0L);
	}
}


static INT_PTR CALLBACK QuickstartDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive;
	int ret = FALSE, i;
	TCHAR tmp[MAX_DPATH];
	static TCHAR df0[MAX_DPATH];
	static TCHAR df1[MAX_DPATH];
	static int dfxtype[2] = { -1, -1 };
	static int doinit;
	int val;

	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch(msg)
	{
	case WM_INITDIALOG:
		{
			int ids[] = { IDC_DF0TEXTQ, IDC_DF1TEXTQ, -1 };
			pages[QUICKSTART_ID] = hDlg;
			currentpage = QUICKSTART_ID;
			enable_for_quickstart (hDlg);
			setfloppytexts (hDlg, true);
			floppybridge_init(&workprefs);
			setmultiautocomplete (hDlg, ids);
			doinit = 1;
			break;
		}
	case WM_NULL:
		if (recursive > 0)
			break;
		recursive++;
		if (doinit) {
			addfloppytype(hDlg, 0);
			addfloppytype(hDlg, 1);
			addfloppyhistory(hDlg);
			init_quickstartdlg(hDlg, false);
			updatefloppytypes(hDlg);
		}
		doinit = 0;
		recursive--;
		break;

	case WM_CONTEXTMENU:
		if (recursive > 0)
			break;
		recursive++;
		diskselectmenu (hDlg, wParam);
		setfloppytexts (hDlg, true);
		recursive--;
		break;

	case WM_COMMAND:
		if (recursive > 0)
			break;
		recursive++;
		if (HIWORD (wParam) == CBN_SELCHANGE || HIWORD (wParam) == CBN_KILLFOCUS)  {
			switch (LOWORD (wParam))
			{
			case IDC_CD0Q_TYPE:
				val = xSendDlgItemMessage (hDlg, IDC_CD0Q_TYPE, CB_GETCURSEL, 0, 0);
				if (val != CB_ERR) {
					quickstart_cdtype = val;
					if (full_property_sheet)
						workprefs.cdslots[0].type = SCSI_UNIT_DEFAULT;
					if (quickstart_cdtype >= 2) {
						int len = sizeof quickstart_cddrive / sizeof (TCHAR);
						quickstart_cdtype = 2;
						xSendDlgItemMessage (hDlg, IDC_CD0Q_TYPE, WM_GETTEXT, (WPARAM)len, (LPARAM)quickstart_cddrive);
						_tcscpy (workprefs.cdslots[0].name, quickstart_cddrive);
					} else {
						eject_cd ();
						quickstart_cdtype = val;
					}
					workprefs.cdslots[0].inuse = quickstart_cdtype > 0;
					addfloppytype (hDlg, 1);
					addfloppyhistory (hDlg);
				}
			break;
			case IDC_QUICKSTART_MODEL:
				val = xSendDlgItemMessage (hDlg, IDC_QUICKSTART_MODEL, CB_GETCURSEL, 0, 0L);
				if (val != CB_ERR) {
					i = 0;
					while (amodels[i].compalevels >= 0) {
						if (amodels[i].compalevels > 0)
							val--;
						if (val < 0)
							break;
						i++;
					}
					if (i != quickstart_model) {
						quickstart_model = i;
						quickstart_conf = quickstart_model_confstore[quickstart_model];
						init_quickstartdlg(hDlg, true);
						if (quickstart)
							load_quickstart(hDlg, 1);
						if (quickstart && !full_property_sheet)
							qs_request_reset |= 4;
					}
				}
				break;
			case IDC_QUICKSTART_CONFIGURATION:
				val = xSendDlgItemMessage (hDlg, IDC_QUICKSTART_CONFIGURATION, CB_GETCURSEL, 0, 0L);
				if (val != CB_ERR && val != quickstart_conf) {
					int rslevel = amodels[quickstart_model].resetlevels[quickstart_conf];
					quickstart_conf = val;
					if (!full_property_sheet && amodels[quickstart_model].resetlevels[quickstart_conf] != rslevel) {
						qs_request_reset |= 4;
					}
					quickstart_model_confstore[quickstart_model] = quickstart_conf;
					init_quickstartdlg(hDlg, true);
					if (quickstart)
						load_quickstart(hDlg, 1);
					if (quickstart && !full_property_sheet)
						qs_request_reset |= 2;
				}
				break;
			case IDC_QUICKSTART_HOSTCONFIG:
				val = xSendDlgItemMessage (hDlg, IDC_QUICKSTART_HOSTCONFIG, CB_GETCURSEL, 0, 0);
				if (val != CB_ERR) {
					xSendDlgItemMessage (hDlg, IDC_QUICKSTART_HOSTCONFIG, CB_GETLBTEXT, (WPARAM)val, (LPARAM)tmp);
					regsetstr (NULL, _T("QuickStartHostConfig"), tmp);
					if (val == 0 || val == 1) {
						regsetstr(NULL, _T("QuickStartHostConfig"), val ? _T("Reset configuration") : _T("Default Configuration"));
					}
					quickstarthost (hDlg, tmp);
					if (val == 0 && quickstart) {
						if (HIWORD(wParam) != CBN_KILLFOCUS) {
							default_prefs(&workprefs, false, 0);
							target_cfgfile_load(&workprefs, _T("default.uae"), CONFIG_TYPE_DEFAULT, 0);
						}
						load_quickstart(hDlg, 0);
					} else if (val == 1 && quickstart) {
						if (HIWORD(wParam) != CBN_KILLFOCUS) {
							default_prefs(&workprefs, false, 0);
						}
						load_quickstart(hDlg, 0);
					}
				}
				break;
			}
		} else {
			switch (LOWORD (wParam))
			{
			case IDC_NTSC:
				quickstart_ntsc = ischecked(hDlg, IDC_NTSC);
				regsetint(NULL, _T("QuickStartNTSC"), quickstart_ntsc);
				if (quickstart) {
					init_quickstartdlg(hDlg, true);
					load_quickstart(hDlg, 0);
				}
				break;
			case IDC_QUICKSTARTMODE:
				quickstart = ischecked(hDlg, IDC_QUICKSTARTMODE);
				regsetint(NULL, _T("QuickStartMode"), quickstart);
				quickstart_cd = 0;
				if (quickstart) {
					init_quickstartdlg(hDlg, true);
					load_quickstart(hDlg, 0);
				}
				enable_for_quickstart(hDlg);
				break;
			}
		}
		switch (LOWORD (wParam))
		{
		case IDC_DF0TEXTQ:
		case IDC_DF0WPQ:
		case IDC_EJECT0Q:
		case IDC_DF0QQ:
		case IDC_DF1TEXTQ:
		case IDC_DF1WPQ:
		case IDC_EJECT1Q:
		case IDC_DF1QQ:
		case IDC_DF0QENABLE:
		case IDC_DF1QENABLE:
		case IDC_INFO0Q:
		case IDC_INFO1Q:
		case IDC_DF0TYPE:
		case IDC_DF1TYPE:
			if (currentpage == QUICKSTART_ID)
				ret = (int)FloppyDlgProc (hDlg, msg, wParam, lParam);
			break;
		case IDC_QUICKSTART_SETCONFIG:
		{
			val = xSendDlgItemMessage(hDlg, IDC_QUICKSTART_HOSTCONFIG, CB_GETCURSEL, 0, 0);
			if (val != CB_ERR) {
				xSendDlgItemMessage(hDlg, IDC_QUICKSTART_HOSTCONFIG, CB_GETLBTEXT, (WPARAM)val, (LPARAM)tmp);
				quickstarthost(hDlg, tmp);
				if (val == 1) {
					default_prefs(&workprefs, false, 0);
				}
			}
			load_quickstart (hDlg, 1);
			break;
		}
		}
		recursive--;
		break;
	case WM_HSCROLL:
		if (recursive > 0)
			break;
		recursive++;
		if ((HWND)lParam == GetDlgItem(hDlg, IDC_QUICKSTART_COMPATIBILITY)) {
			val = (int)SendMessage((HWND)lParam, TBM_GETPOS, 0, 0);
			if (val >= 0 && val != quickstart_compa) {
				quickstart_compa = val;
				init_quickstartdlg(hDlg, true);
				if (quickstart)
					load_quickstart(hDlg, 0);
			}
		}
		recursive--;
		break;
	}
	if (recursive == 0 && quickstart) {
		recursive++;
		if (_tcscmp (workprefs.floppyslots[0].df, df0) || workprefs.floppyslots[0].dfxtype != dfxtype[0]) {
			_tcscpy (df0, workprefs.floppyslots[0].df);
			dfxtype[0] = workprefs.floppyslots[0].dfxtype;
			testimage (hDlg, 0);
			enable_for_quickstart (hDlg);
		}
		if (_tcscmp (workprefs.floppyslots[1].df, df1) || workprefs.floppyslots[1].dfxtype != dfxtype[1]) {
			_tcscpy (df1, workprefs.floppyslots[1].df);
			dfxtype[1] = workprefs.floppyslots[1].dfxtype;
			testimage (hDlg, 1);
		}
		recursive--;
	}
	return ret;
}

static void init_aboutdlg (HWND hDlg)
{

}

static INT_PTR CALLBACK AboutDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static HFONT font1, font2;

	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch( msg )
	{
	case WM_INITDIALOG:
		{
			pages[ABOUT_ID] = hDlg;
			currentpage = ABOUT_ID;

			font1 = CreateFont(getscaledfontsize(-1, hDlg) * 3, 0, 0, 0, 0,
				0, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
				PROOF_QUALITY, FF_DONTCARE, _T("Segoe UI"));
			font2 = CreateFont(getscaledfontsize(-1, hDlg) * 2, 0, 0, 0, 0,
				0, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
				PROOF_QUALITY, FF_DONTCARE, _T("Segoe UI"));

			HWND hwnd = GetDlgItem(hDlg, IDC_RICHEDIT1);
			SendMessage(hwnd, WM_SETFONT, (WPARAM)font1, 0);
			SetWindowText(hwnd, _T("WinUAE"));
			hwnd = GetDlgItem(hDlg, IDC_RICHEDIT2);
			SendMessage(hwnd, WM_SETFONT, (WPARAM)font1, 0);
			SetWindowText(hwnd, VersionStr);

			for (int i = 0; urls[i].id >= 0; i++) {
				hwnd = GetDlgItem(hDlg, urls[i].id);
				SendMessage(hwnd, WM_SETFONT, (WPARAM)font2, 0);
				SetWindowText(hwnd, urls[i].display);
			}
			return TRUE;
		}
	case WM_COMMAND:
		if (wParam == IDC_CONTRIBUTORS)
			DisplayContributors (hDlg);
		break;
	case WM_SETCURSOR:
		return TRUE;
		break;
	case WM_LBUTTONDOWN:
	case WM_MOUSEMOVE:
		url_handler(hDlg, msg, wParam, lParam);
		break;
	case WM_DESTROY:
		DeleteObject(font1);
		font1 = NULL;
		DeleteObject(font2);
		font2 = NULL;
		break;
	}

	return FALSE;
}

static void enable_for_displaydlg (HWND hDlg)
{
	int rtg = ((!workprefs.address_space_24 || gfxboard_get_configtype(&workprefs.rtgboards[0]) == 2) && workprefs.rtgboards[0].rtgmem_size) || workprefs.rtgboards[0].rtgmem_type >= GFXBOARD_HARDWARE;
#ifndef PICASSO96
	rtg = FALSE;
#endif
	ew(hDlg, IDC_SCREENMODE_RTG, rtg);
	ew(hDlg, IDC_SCREENMODE_RTG2, rtg);
	ew(hDlg, IDC_XCENTER, TRUE);
	ew(hDlg, IDC_YCENTER, TRUE);
	ew(hDlg, IDC_FRAMERATE, !workprefs.cpu_memory_cycle_exact);
	ew(hDlg, IDC_LORES, !workprefs.gfx_autoresolution);
	ew(hDlg, IDC_OVERSCANMODE, TRUE);

	ew(hDlg, IDC_AUTORESOLUTIONVGA, workprefs.gfx_resolution >= RES_HIRES && workprefs.gfx_vresolution >= VRES_DOUBLE);
	if (workprefs.gfx_resolution < RES_HIRES || workprefs.gfx_vresolution < VRES_DOUBLE) {
		workprefs.gfx_autoresolution_vga = false;
		CheckDlgButton (hDlg, IDC_AUTORESOLUTIONVGA, workprefs.gfx_autoresolution_vga);
	}

	bool isdouble = workprefs.gfx_vresolution > 0;

	ew (hDlg, IDC_LM_NORMAL, !workprefs.gfx_autoresolution);
	ew (hDlg, IDC_LM_DOUBLED, !workprefs.gfx_autoresolution);
	ew (hDlg, IDC_LM_SCANLINES, !workprefs.gfx_autoresolution);
	ew (hDlg, IDC_LM_PDOUBLED2, !workprefs.gfx_autoresolution);
	ew (hDlg, IDC_LM_PDOUBLED3, !workprefs.gfx_autoresolution);

	ew (hDlg, IDC_LM_INORMAL, !workprefs.gfx_autoresolution && !isdouble);
	ew (hDlg, IDC_LM_IDOUBLED, !workprefs.gfx_autoresolution && isdouble);
	ew (hDlg, IDC_LM_IDOUBLED2, !workprefs.gfx_autoresolution && isdouble);
	ew (hDlg, IDC_LM_IDOUBLED3, !workprefs.gfx_autoresolution && isdouble);

	if (workprefs.gfx_apmode[0].gfx_vsyncmode == 1 || workprefs.gfx_apmode[0].gfx_vsyncmode == 2) {
		hide(hDlg, IDC_SCREENMODE_NATIVE3, FALSE);
		ew(hDlg, IDC_SCREENMODE_NATIVE3, TRUE);
	} else {
		ew(hDlg, IDC_SCREENMODE_NATIVE3, FALSE);
		hide(hDlg, IDC_SCREENMODE_NATIVE3, TRUE);
	}
#if WINUAEPUBLICBETA
	hide(hDlg, IDC_DISPLAY_VARSYNC, FALSE);
#endif
}

static void enable_for_chipsetdlg (HWND hDlg)
{
	int enable = workprefs.cpu_memory_cycle_exact ? FALSE : TRUE;
	int genlock = workprefs.genlock || workprefs.genlock_effects;

#if !defined (CPUEMU_13)
	ew (hDlg, IDC_CYCLEEXACT, FALSE);
#else
	ew (hDlg, IDC_CYCLEEXACTMEMORY, workprefs.cpu_model >= 68020);
#endif
	if ((workprefs.immediate_blits || (workprefs.cpu_memory_cycle_exact && workprefs.cpu_model <= 68010))) {
		workprefs.waiting_blits = 0;
		CheckDlgButton(hDlg, IDC_BLITWAIT, FALSE);
		ew(hDlg, IDC_BLITWAIT, false);
	} else {
		ew(hDlg, IDC_BLITWAIT, TRUE);
	}
	ew(hDlg, IDC_BLITIMM, !workprefs.cpu_cycle_exact);

	ew(hDlg, IDC_GENLOCKMODE, genlock ? TRUE : FALSE);
	ew(hDlg, IDC_GENLOCKMIX, genlock ? TRUE : FALSE);
	ew(hDlg, IDC_GENLOCK_ALPHA, genlock ? TRUE : FALSE);
	ew(hDlg, IDC_GENLOCK_KEEP_ASPECT, genlock ? TRUE : FALSE);
	ew(hDlg, IDC_GENLOCKFILE, genlock && (workprefs.genlock_image >= 6 || (workprefs.genlock_image >= 3 && workprefs.genlock_image < 5)) ? TRUE : FALSE);
	ew(hDlg, IDC_GENLOCKFILESELECT, genlock && (workprefs.genlock_image >= 6 || (workprefs.genlock_image >= 3 && workprefs.genlock_image < 5)) ? TRUE : FALSE);

	ew(hDlg, IDC_MONITOREMU_MON, workprefs.monitoremu != 0);

	if (workprefs.keyboard_mode == KB_UAE || workprefs.keyboard_mode == KB_A2000_8039) {
		if (!workprefs.keyboard_nkro) {
			workprefs.keyboard_nkro = true;
			CheckDlgButton(hDlg, IDC_KEYBOARDNKRO, TRUE);
		}
		ew(hDlg, IDC_KEYBOARDNKRO, FALSE);
	} else {
		ew(hDlg, IDC_KEYBOARDNKRO, TRUE);
	}

}

static const int fakerefreshrates[] = { 50, 60, 100, 120, 0 };
struct storedrefreshrate
{
	int rate, type;
};
static struct storedrefreshrate storedrefreshrates[MAX_REFRESH_RATES + 4 + 1];


static void init_frequency_combo (HWND hDlg, int dmode)
{
	int i, j, freq;
	TCHAR hz[20], hz2[20], txt[100];
	LRESULT index;
	struct MultiDisplay *md = getdisplay(&workprefs, 0);

	i = 0; index = 0;
	while (dmode >= 0 && (freq = md->DisplayModes[dmode].refresh[i]) > 0 && index < MAX_REFRESH_RATES) {
		storedrefreshrates[index].rate = freq;
		storedrefreshrates[index++].type = md->DisplayModes[dmode].refreshtype[i];
		i++;
	}
	if (workprefs.gfx_apmode[0].gfx_vsyncmode == 0 && workprefs.gfx_apmode[0].gfx_vsync) {
		i = 0;
		while ((freq = fakerefreshrates[i]) > 0 && index < MAX_REFRESH_RATES) {
			for (j = 0; j < index; j++) {
				if (storedrefreshrates[j].rate == freq)
					break;
			}
			if (j == index) {
				storedrefreshrates[index].rate = -freq;
				storedrefreshrates[index++].type = 0;
			}
			i++;
		}
	}
	storedrefreshrates[index].rate = 0;
	for (i = 0; i < index; i++) {
		for (j = i + 1; j < index; j++) {
			if (abs (storedrefreshrates[i].rate) >= abs (storedrefreshrates[j].rate)) {
				struct storedrefreshrate srr;
				memcpy (&srr, &storedrefreshrates[i], sizeof (struct storedrefreshrate));
				memcpy (&storedrefreshrates[i], &storedrefreshrates[j], sizeof (struct storedrefreshrate));
				memcpy (&storedrefreshrates[j], &srr, sizeof (struct storedrefreshrate));
			}
		}
	}

	hz[0] = hz2[0] = 0;
	xSendDlgItemMessage(hDlg, IDC_REFRESHRATE, CB_RESETCONTENT, 0, 0);
	WIN32GUI_LoadUIString (IDS_VSYNC_DEFAULT, txt, sizeof (txt) / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_REFRESHRATE, CB_ADDSTRING, 0, (LPARAM)txt);
	for (i = 0; i < index; i++) {
		bool lace = (storedrefreshrates[i].type & REFRESH_RATE_LACE) != 0;
		freq = storedrefreshrates[i].rate;
		if (freq < 0) {
			freq = -freq;
			_stprintf (hz, _T("(%dHz)"), freq);
		} else {
			_stprintf (hz, _T("%dHz"), freq);
		}
		if (freq == 50 || freq == 100 || (freq * 2 == 50 && lace))
			_tcscat (hz, _T(" PAL"));
		if (freq == 60 || freq == 120 || (freq * 2 == 60 && lace))
			_tcscat (hz, _T(" NTSC"));
		if (lace) {
			TCHAR tmp[10];
			_stprintf (tmp, _T(" (%di)"), freq * 2);
			_tcscat (hz, tmp);
		}
		if (storedrefreshrates[i].type & REFRESH_RATE_RAW)
			_tcscat (hz, _T(" (*)"));
		if (abs (workprefs.gfx_apmode[0].gfx_refreshrate) == freq)
			_tcscpy (hz2, hz);
		xSendDlgItemMessage (hDlg, IDC_REFRESHRATE, CB_ADDSTRING, 0, (LPARAM)hz);
	}
	index = CB_ERR;
	if (hz2[0] >= 0)
		index = xSendDlgItemMessage (hDlg, IDC_REFRESHRATE, CB_SELECTSTRING, 0, (LPARAM)hz2);
	if (index == CB_ERR) {
		WIN32GUI_LoadUIString (IDS_VSYNC_DEFAULT, txt, sizeof (txt) / sizeof (TCHAR));
		xSendDlgItemMessage(hDlg, IDC_REFRESHRATE, CB_SELECTSTRING, i, (LPARAM)txt);
		workprefs.gfx_apmode[0].gfx_refreshrate = 0;
	}
}

#define MAX_FRAMERATE_LENGTH 40
#define MAX_NTH_LENGTH 20

static int display_mode_index (uae_u32 x, uae_u32 y, uae_u32 d)
{
	int i, j;
	struct MultiDisplay *md = getdisplay(&workprefs, 0);

	j = 0;
	for (i = 0; md->DisplayModes[i].inuse; i++) {
		if (md->DisplayModes[i].res.width == x &&
			md->DisplayModes[i].res.height == y)
			break;
		j++;
	}
	if (x == 0 && y == 0) {
		j = 0;
		for (i = 0; md->DisplayModes[i].inuse; i++) {
			if (md->DisplayModes[i].res.width == md->rect.right - md->rect.left &&
				md->DisplayModes[i].res.height == md->rect.bottom - md->rect.top)
				break;
			j++;
		}
	}
	if(!md->DisplayModes[i].inuse)
		j = -1;
	return j;
}

static int da_mode_selected, da_mode_multiplier;

static int *getp_da (HWND hDlg)
{
	int vmin = -200;
	int vmax = 200;
	da_mode_multiplier = 10;
	int *p = 0;
	switch (da_mode_selected)
	{
	case 0:
		p = &workprefs.gfx_luminance;
		break;
	case 1:
		p = &workprefs.gfx_contrast;
		break;
	case 2:
		p = &workprefs.gfx_gamma;
		break;
	case 3:
		p = &workprefs.gfx_gamma_ch[0];
		break;
	case 4:
		p = &workprefs.gfx_gamma_ch[1];
		break;
	case 5:
		p = &workprefs.gfx_gamma_ch[2];
		break;
	case 6:
		p = &workprefs.gfx_threebitcolors;
		vmin = 0;
		vmax = 3;
		da_mode_multiplier = 1;
		break;
	}
	if (*p < vmin * da_mode_multiplier)
		*p = vmin * da_mode_multiplier;
	if (*p > vmax * da_mode_multiplier)
		*p = vmax * da_mode_multiplier;
	xSendDlgItemMessage(hDlg, IDC_DA_SLIDER, TBM_SETPAGESIZE, 0, 1);
	xSendDlgItemMessage(hDlg, IDC_DA_SLIDER, TBM_SETRANGE, TRUE, MAKELONG(vmin, vmax));
	return p;
}

static void set_da (HWND hDlg)
{
	int *p = getp_da (hDlg);
	if (!p)
		return;
	TCHAR buf[10];
	xSendDlgItemMessage (hDlg, IDC_DA_SLIDER, TBM_SETPOS, TRUE, (*p) / da_mode_multiplier);
	_stprintf(buf, _T("%.1f"), (double)((*p) / (double)da_mode_multiplier));
	SetDlgItemText (hDlg, IDC_DA_TEXT, buf);
}

static void update_da (HWND hDlg)
{
	currprefs.gfx_gamma = workprefs.gfx_gamma;
	currprefs.gfx_gamma_ch[0] = workprefs.gfx_gamma_ch[0];
	currprefs.gfx_gamma_ch[1] = workprefs.gfx_gamma_ch[1];
	currprefs.gfx_gamma_ch[2] = workprefs.gfx_gamma_ch[2];
	currprefs.gfx_luminance = workprefs.gfx_luminance;
	currprefs.gfx_contrast = workprefs.gfx_contrast;
	currprefs.gfx_threebitcolors = workprefs.gfx_threebitcolors;
	set_da (hDlg);
	init_colors(0);
	init_custom();
	updatedisplayarea(-1);
}

static void handle_da (HWND hDlg)
{
	int *p;
	int v;

	p = getp_da (hDlg);
	if (!p)
		return;
	v = xSendDlgItemMessage (hDlg, IDC_DA_SLIDER, TBM_GETPOS, 0, 0) * da_mode_multiplier;
	if (v == *p)
		return;
	*p = v;
	update_da (hDlg);
}

void init_da (HWND hDlg)
{
	int *p;
	TCHAR tmp[MAX_DPATH], *p1, *p2;

	WIN32GUI_LoadUIString(IDS_DISPLAY_ATTRIBUTES, tmp, sizeof tmp / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_DA_MODE, CB_RESETCONTENT, 0, 0);
	_tcscat (tmp, _T("\n"));
	p1 = tmp;
	for (;;) {
		p2 = _tcschr (p1, '\n');
		if (p2 && _tcslen (p2) > 0) {
			*p2++ = 0;
			xSendDlgItemMessage (hDlg, IDC_DA_MODE, CB_ADDSTRING, 0, (LPARAM)p1);
			p1 = p2;
		} else
			break;
	}
	if (da_mode_selected == CB_ERR)
		da_mode_selected = 0;
	xSendDlgItemMessage (hDlg, IDC_DA_MODE, CB_SETCURSEL, da_mode_selected, 0);
	p = getp_da (hDlg);
	if (p)
		set_da (hDlg);
}

static int gui_display_depths[3];
static void init_display_mode (HWND hDlg)
{
	int index;
	struct MultiDisplay *md = getdisplay(&workprefs, 0);
	struct monconfig *gm = &workprefs.gfx_monitor[0];

	if (gm->gfx_size_fs.special == WH_NATIVE) {
		int cnt = (int)xSendDlgItemMessage (hDlg, IDC_RESOLUTION, CB_GETCOUNT, 0, 0);
		xSendDlgItemMessage (hDlg, IDC_RESOLUTION, CB_SETCURSEL, cnt - 1, 0);
		index = display_mode_index (gm->gfx_size_fs.width, gm->gfx_size_fs.height, 4);
	} else {
		index = display_mode_index (gm->gfx_size_fs.width, gm->gfx_size_fs.height, 4);
		if (index >= 0)
			xSendDlgItemMessage (hDlg, IDC_RESOLUTION, CB_SETCURSEL, md->DisplayModes[index].residx, 0);
		gm->gfx_size_fs.special = 0;
	}
	init_frequency_combo (hDlg, index);

}

#if 0
static int display_toselect (int fs, int vsync, int p96)
{
	if (p96)
		return fs * 2 + (vsync ? 1 : 0);
	if (fs == 2)
		return 4;
	if (!vsync)
		return fs;
	if (fs == 1 && vsync == 1)
		return 2;
	if (fs == 1 && vsync == 2)
		return 3;
	return fs;
}
static void display_fromselect (int val, int *fs, int *vsync, int p96)
{
	int ofs = *fs;
	if (val == CB_ERR)
		return;
	*fs = 0;
	*vsync = 0;
	if (p96) {
		*fs = val / 2;
		*vsync = val & 1;
		if (*fs == 2 && *fs != ofs) {
			workprefs.win32_rtgscaleifsmall = 1;
			workprefs.win32_rtgmatchdepth = 0;
		}
		return;
	}
	switch (val)
	{
	case 0:
		*fs = 0;
		break;
	case 1:
		*fs = 1;
		break;
	case 2:
		*fs = 1;
		*vsync = 1;
		break;
	case 3:
		*fs = 1;
		*vsync = 2;
		break;
	case 4:
		*fs = 2;
		if (workprefs.gfx_filter == 0 && *fs != ofs && !workprefs.gfx_api) {
			workprefs.gfx_filter = 1;
			workprefs.gfx_filter_horiz_zoom = 0;
			workprefs.gfx_filter_vert_zoom = 0;
			workprefs.gfx_filter_horiz_zoom_mult = 0;
			workprefs.gfx_filter_vert_zoom_mult = 0;
			workprefs.gfx_filter_aspect = -1;
			workprefs.gfx_filter_horiz_offset = 0;
			workprefs.gfx_filter_vert_offset = 0;
			workprefs.gfx_filter_keep_aspect = 0;
		}
		break;
	}
}
#endif

#define MAX_GUI_DISPLAY_SECTIONS 30

static void values_to_displaydlg (HWND hDlg)
{
	TCHAR buffer[MAX_DPATH];
	int rates[MAX_CHIPSET_REFRESH_TOTAL];
	int v;
	double d;

	init_display_mode (hDlg);

	SetDlgItemInt (hDlg, IDC_XSIZE, workprefs.gfx_monitor[0].gfx_size_win.width, FALSE);
	SetDlgItemInt (hDlg, IDC_YSIZE, workprefs.gfx_monitor[0].gfx_size_win.height, FALSE);

	xSendDlgItemMessage(hDlg, IDC_RATE2BOX, CB_RESETCONTENT, 0, 0);
	v = 0;
	struct chipset_refresh *selectcr = full_property_sheet ? (workprefs.ntscmode ? &workprefs.cr[CHIPSET_REFRESH_NTSC] : &workprefs.cr[CHIPSET_REFRESH_PAL]) : get_chipset_refresh (&workprefs) ;
	for (int i = 0; i < MAX_CHIPSET_REFRESH_TOTAL; i++) {
		struct chipset_refresh *cr = &workprefs.cr[i];
		if (cr->rate > 0) {
			_tcscpy (buffer, cr->label);
			if (!buffer[0])
				_stprintf (buffer, _T(":%d"), i);
			xSendDlgItemMessage(hDlg, IDC_RATE2BOX, CB_ADDSTRING, 0, (LPARAM)buffer);
			d = workprefs.chipset_refreshrate;
			if (abs (d) < 1)
				d = currprefs.ntscmode ? 60.0 : 50.0;
			if (selectcr && selectcr->index == cr->index)
				workprefs.cr_selected = i;
			rates[i] = v;
			v++;
		}
	}


	if (workprefs.cr_selected < 0 || workprefs.cr[workprefs.cr_selected].rate <= 0)
		workprefs.cr_selected = CHIPSET_REFRESH_PAL;
	selectcr = &workprefs.cr[workprefs.cr_selected];
	xSendDlgItemMessage(hDlg, IDC_RATE2BOX, CB_SETCURSEL, rates[workprefs.cr_selected], 0);
	xSendDlgItemMessage (hDlg, IDC_FRAMERATE2, TBM_SETPOS, TRUE, (LPARAM)(selectcr->rate + 0.5));
	_stprintf (buffer, _T("%.6f"), selectcr->locked || full_property_sheet ? selectcr->rate : workprefs.chipset_refreshrate);
	SetDlgItemText (hDlg, IDC_RATE2TEXT, buffer);
	CheckDlgButton (hDlg, IDC_RATE2ENABLE, selectcr->locked);

	ew (hDlg, IDC_RATE2TEXT, selectcr->locked != 0);
	ew (hDlg, IDC_FRAMERATE2, selectcr->locked != 0);

	v = workprefs.cpu_memory_cycle_exact ? 1 : workprefs.gfx_framerate;
	xSendDlgItemMessage (hDlg, IDC_FRAMERATE, TBM_SETPOS, TRUE, (int)v);

	CheckRadioButton (hDlg, IDC_LM_NORMAL, IDC_LM_PDOUBLED3, IDC_LM_NORMAL + (workprefs.gfx_vresolution ? 1 : 0) + workprefs.gfx_pscanlines);
	CheckRadioButton (hDlg, IDC_LM_INORMAL, IDC_LM_IDOUBLED3, IDC_LM_INORMAL + (workprefs.gfx_iscanlines ? workprefs.gfx_iscanlines + 1 : (workprefs.gfx_vresolution ? 1 : 0)));

	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_NATIVE, CB_RESETCONTENT, 0, 0);
	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_NATIVE2, CB_RESETCONTENT, 0, 0);
	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_NATIVE3, CB_RESETCONTENT, 0, 0);

	WIN32GUI_LoadUIString(IDS_SCREEN_WINDOWED, buffer, sizeof buffer / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_NATIVE, CB_ADDSTRING, 0, (LPARAM)buffer);
	WIN32GUI_LoadUIString(IDS_SCREEN_FULLSCREEN, buffer, sizeof buffer / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_NATIVE, CB_ADDSTRING, 0, (LPARAM)buffer);
	WIN32GUI_LoadUIString(IDS_SCREEN_FULLWINDOW, buffer, sizeof buffer / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_NATIVE, CB_ADDSTRING, 0, (LPARAM)buffer);

	WIN32GUI_LoadUIString(IDS_SCREEN_VSYNC_NONE, buffer, sizeof buffer / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_NATIVE2, CB_ADDSTRING, 0, (LPARAM)buffer);
	WIN32GUI_LoadUIString(IDS_SCREEN_VSYNC2, buffer, sizeof buffer / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_NATIVE2, CB_ADDSTRING, 0, (LPARAM)buffer);
	WIN32GUI_LoadUIString(IDS_SCREEN_VSYNC2_AUTOSWITCH, buffer, sizeof buffer / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_NATIVE2, CB_ADDSTRING, 0, (LPARAM)buffer);
	WIN32GUI_LoadUIString(IDS_SCREEN_VSYNC, buffer, sizeof buffer / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_NATIVE2, CB_ADDSTRING, 0, (LPARAM)buffer);
	WIN32GUI_LoadUIString(IDS_SCREEN_VSYNC_AUTOSWITCH, buffer, sizeof buffer / sizeof(TCHAR));
	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_NATIVE2, CB_ADDSTRING, 0, (LPARAM)buffer);

	for (int i = 1; i < MAX_GUI_DISPLAY_SECTIONS; i++) {
		_stprintf(buffer, _T("%d"), i);
		xSendDlgItemMessage(hDlg, IDC_SCREENMODE_NATIVE3, CB_ADDSTRING, 0, (LPARAM)buffer);
	}

	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_NATIVE, CB_SETCURSEL, workprefs.gfx_apmode[0].gfx_fullscreen, 0);
	v = workprefs.gfx_apmode[0].gfx_vsync;
	if (v < 0)
		v = 5;
	else if (v > 0) {
		v = v + (workprefs.gfx_apmode[0].gfx_vsyncmode || !v ? 0 : 2);
	}

	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_NATIVE2, CB_SETCURSEL, v, 0);

	if (workprefs.gfx_display_sections - 1 < MAX_GUI_DISPLAY_SECTIONS)
		xSendDlgItemMessage(hDlg, IDC_SCREENMODE_NATIVE3, CB_SETCURSEL, workprefs.gfx_display_sections - 1, 0);


	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_RTG, CB_RESETCONTENT, 0, 0);
	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_RTG2, CB_RESETCONTENT, 0, 0);

	WIN32GUI_LoadUIString(IDS_SCREEN_WINDOWED, buffer, sizeof buffer / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_RTG, CB_ADDSTRING, 0, (LPARAM)buffer);
	WIN32GUI_LoadUIString(IDS_SCREEN_FULLSCREEN, buffer, sizeof buffer / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_RTG, CB_ADDSTRING, 0, (LPARAM)buffer);
	WIN32GUI_LoadUIString(IDS_SCREEN_FULLWINDOW, buffer, sizeof buffer / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_RTG, CB_ADDSTRING, 0, (LPARAM)buffer);

	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_RTG2, CB_ADDSTRING, 0, (LPARAM)_T("-"));
#if 0
	WIN32GUI_LoadUIString(IDS_SCREEN_VSYNC, buffer, sizeof buffer / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_RTG2, CB_ADDSTRING, 0, (LPARAM)buffer);
	WIN32GUI_LoadUIString(IDS_SCREEN_VSYNC_AUTOSWITCH, buffer, sizeof buffer / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_RTG2, CB_ADDSTRING, 0, (LPARAM)buffer);
#endif
	WIN32GUI_LoadUIString(IDS_SCREEN_VSYNC2, buffer, sizeof buffer / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_RTG2, CB_ADDSTRING, 0, (LPARAM)buffer);
#if 0
	WIN32GUI_LoadUIString(IDS_SCREEN_VSYNC2_AUTOSWITCH, buffer, sizeof buffer / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_RTG2, CB_ADDSTRING, 0, (LPARAM)buffer);
#endif
	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_RTG, CB_SETCURSEL,
		workprefs.gfx_apmode[1].gfx_fullscreen, 0);
	v = workprefs.gfx_apmode[1].gfx_vsync;
	if (v < 0)
		v = 2;
	else if (v > 0)
		v = 1;
	xSendDlgItemMessage(hDlg, IDC_SCREENMODE_RTG2, CB_SETCURSEL, v, 0);

	xSendDlgItemMessage(hDlg, IDC_LORES, CB_RESETCONTENT, 0, 0);
	WIN32GUI_LoadUIString(IDS_RES_LORES, buffer, sizeof buffer / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_LORES, CB_ADDSTRING, 0, (LPARAM)buffer);
	WIN32GUI_LoadUIString(IDS_RES_HIRES, buffer, sizeof buffer / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_LORES, CB_ADDSTRING, 0, (LPARAM)buffer);
	WIN32GUI_LoadUIString(IDS_RES_SUPERHIRES, buffer, sizeof buffer / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_LORES, CB_ADDSTRING, 0, (LPARAM)buffer);
	xSendDlgItemMessage (hDlg, IDC_LORES, CB_SETCURSEL, workprefs.gfx_resolution, 0);

	xSendDlgItemMessage(hDlg, IDC_OVERSCANMODE, CB_RESETCONTENT, 0, 0);
	xSendDlgItemMessage(hDlg, IDC_OVERSCANMODE, CB_ADDSTRING, 0, (LPARAM)_T("TV (narrow)"));
	xSendDlgItemMessage(hDlg, IDC_OVERSCANMODE, CB_ADDSTRING, 0, (LPARAM)_T("TV (standard)"));
	xSendDlgItemMessage(hDlg, IDC_OVERSCANMODE, CB_ADDSTRING, 0, (LPARAM)_T("TV (wide)"));
	xSendDlgItemMessage(hDlg, IDC_OVERSCANMODE, CB_ADDSTRING, 0, (LPARAM)_T("Overscan"));
	xSendDlgItemMessage(hDlg, IDC_OVERSCANMODE, CB_ADDSTRING, 0, (LPARAM)_T("Overscan+"));
	xSendDlgItemMessage(hDlg, IDC_OVERSCANMODE, CB_ADDSTRING, 0, (LPARAM)_T("Extreme"));
	xSendDlgItemMessage(hDlg, IDC_OVERSCANMODE, CB_ADDSTRING, 0, (LPARAM)_T("Ultra extreme debug"));
	xSendDlgItemMessage(hDlg, IDC_OVERSCANMODE, CB_ADDSTRING, 0, (LPARAM)_T("Ultra extreme debug (HV)"));
	xSendDlgItemMessage(hDlg, IDC_OVERSCANMODE, CB_ADDSTRING, 0, (LPARAM)_T("Ultra extreme debug (C)"));
	xSendDlgItemMessage(hDlg, IDC_OVERSCANMODE, CB_SETCURSEL, workprefs.gfx_overscanmode, 0);

	xSendDlgItemMessage(hDlg, IDC_AUTORESOLUTIONSELECT, CB_RESETCONTENT, 0, 0);
	WIN32GUI_LoadUIString(IDS_DISABLED, buffer, sizeof buffer / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_AUTORESOLUTIONSELECT, CB_ADDSTRING, 0, (LPARAM)buffer);
	WIN32GUI_LoadUIString(IDS_ALWAYS_ON, buffer, sizeof buffer / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_AUTORESOLUTIONSELECT, CB_ADDSTRING, 0, (LPARAM)buffer);
	xSendDlgItemMessage(hDlg, IDC_AUTORESOLUTIONSELECT, CB_ADDSTRING, 0, (LPARAM)_T("10%"));
	xSendDlgItemMessage(hDlg, IDC_AUTORESOLUTIONSELECT, CB_ADDSTRING, 0, (LPARAM)_T("33%"));
	xSendDlgItemMessage(hDlg, IDC_AUTORESOLUTIONSELECT, CB_ADDSTRING, 0, (LPARAM)_T("66%"));
	if (workprefs.gfx_autoresolution == 0)
		xSendDlgItemMessage (hDlg, IDC_AUTORESOLUTIONSELECT, CB_SETCURSEL, 0, 0);
	else if (workprefs.gfx_autoresolution == 1)
		xSendDlgItemMessage (hDlg, IDC_AUTORESOLUTIONSELECT, CB_SETCURSEL, 1, 0);
	else if (workprefs.gfx_autoresolution <= 10)
		xSendDlgItemMessage (hDlg, IDC_AUTORESOLUTIONSELECT, CB_SETCURSEL, 2, 0);
	else if (workprefs.gfx_autoresolution <= 33)
		xSendDlgItemMessage (hDlg, IDC_AUTORESOLUTIONSELECT, CB_SETCURSEL, 3, 0);
	else if (workprefs.gfx_autoresolution <= 99)
		xSendDlgItemMessage(hDlg, IDC_AUTORESOLUTIONSELECT, CB_SETCURSEL, 4, 0);
	else
		xSendDlgItemMessage(hDlg, IDC_AUTORESOLUTIONSELECT, CB_SETCURSEL, 5, 0);

	CheckDlgButton(hDlg, IDC_AUTORESOLUTIONVGA, workprefs.gfx_autoresolution_vga);
	CheckDlgButton(hDlg, IDC_BLACKER_THAN_BLACK, workprefs.gfx_blackerthanblack);
	CheckDlgButton(hDlg, IDC_LORES_SMOOTHED, workprefs.gfx_lores_mode);
	CheckDlgButton(hDlg, IDC_FLICKERFIXER, workprefs.gfx_scandoubler);
	CheckDlgButton(hDlg, IDC_GRAYSCALE, workprefs.gfx_grayscale);
	CheckDlgButton(hDlg, IDC_RESYNCBLANK, workprefs.gfx_monitorblankdelay > 0);

	CheckDlgButton (hDlg, IDC_XCENTER, workprefs.gfx_xcenter);
	CheckDlgButton (hDlg, IDC_YCENTER, workprefs.gfx_ycenter);

	xSendDlgItemMessage(hDlg, IDC_DISPLAY_BUFFERCNT, CB_RESETCONTENT, 0, 0);
#if 0
	WIN32GUI_LoadUIString(IDS_BUFFER_SINGLE, buffer, sizeof buffer / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_DISPLAY_BUFFERCNT, CB_ADDSTRING, 0, (LPARAM)buffer);
#endif
	WIN32GUI_LoadUIString(IDS_BUFFER_DOUBLE, buffer, sizeof buffer / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_DISPLAY_BUFFERCNT, CB_ADDSTRING, 0, (LPARAM)buffer);
	WIN32GUI_LoadUIString(IDS_BUFFER_TRIPLE, buffer, sizeof buffer / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_DISPLAY_BUFFERCNT, CB_ADDSTRING, 0, (LPARAM)buffer);
	xSendDlgItemMessage (hDlg, IDC_DISPLAY_BUFFERCNT, CB_SETCURSEL, workprefs.gfx_apmode[0].gfx_backbuffers - 1, 0);

	CheckDlgButton(hDlg, IDC_DISPLAY_VARSYNC, workprefs.gfx_variable_sync != 0);
	CheckDlgButton(hDlg, IDC_DISPLAY_RESIZE, workprefs.gfx_windowed_resize != 0);

	init_da (hDlg);
}

static void init_resolution_combo (HWND hDlg)
{
	int i, idx;
	TCHAR tmp[MAX_DPATH];
	struct MultiDisplay *md = getdisplay(&workprefs, 0);

	idx = -1;
	xSendDlgItemMessage(hDlg, IDC_RESOLUTION, CB_RESETCONTENT, 0, 0);
	for (i = 0; md->DisplayModes[i].inuse; i++) {
		if (md->DisplayModes[i].residx != idx) {
			_stprintf (tmp, _T("%dx%d%s"), md->DisplayModes[i].res.width, md->DisplayModes[i].res.height, md->DisplayModes[i].lace ? _T("i") : _T(""));
			if (md->DisplayModes[i].rawmode)
				_tcscat (tmp, _T(" (*)"));
			xSendDlgItemMessage(hDlg, IDC_RESOLUTION, CB_ADDSTRING, 0, (LPARAM)tmp);
			idx = md->DisplayModes[i].residx;
		}
	}
	WIN32GUI_LoadUIString (IDS_DISPLAYMODE_NATIVE, tmp, sizeof tmp / sizeof (TCHAR));
	xSendDlgItemMessage(hDlg, IDC_RESOLUTION, CB_ADDSTRING, 0, (LPARAM)tmp);
}

static void init_displays_combo (HWND hDlg, bool rtg)
{
	const TCHAR *adapter = _T("");
	struct MultiDisplay *md = Displays;
	int cnt = 0, cnt2 = 0;
	int displaynum;
	int idx = 0;
	int id = rtg ? IDC_RTG_DISPLAYSELECT : IDC_DISPLAYSELECT;

	displaynum = workprefs.gfx_apmode[rtg ? APMODE_RTG : APMODE_NATIVE].gfx_display - 1;
	xSendDlgItemMessage (hDlg, id, CB_RESETCONTENT, 0, 0);
	if (displaynum < 0)
		displaynum = 0;
	while (md->monitorname) {
		if (_tcscmp (md->adapterkey, adapter) != 0) {
			xSendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)md->adaptername);
			adapter = md->adapterkey;
			cnt++;
		}
		TCHAR buf[MAX_DPATH];
		_stprintf (buf, _T("  %s"), md->fullname);
		xSendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)buf);
		if (displaynum == cnt2)
			idx = cnt;
		md++;
		cnt2++;
		cnt++;
	}
	xSendDlgItemMessage (hDlg, id, CB_SETCURSEL, idx, 0);
}

static bool get_displays_combo (HWND hDlg, bool rtg)
{
	struct MultiDisplay *md = Displays;
	LRESULT posn;
	const TCHAR *adapter = _T("");
	int cnt = 0, cnt2 = 0;
	int displaynum;
	int id = rtg ? IDC_RTG_DISPLAYSELECT : IDC_DISPLAYSELECT;

	posn = xSendDlgItemMessage (hDlg, id, CB_GETCURSEL, 0, 0);
	if (posn == CB_ERR)
		return false;

	displaynum = workprefs.gfx_apmode[rtg ? APMODE_RTG : APMODE_NATIVE].gfx_display - 1;
	if (displaynum < 0)
		displaynum = 0;
	while (md->monitorname) {
		int foundnum = -1;
		if (_tcscmp (md->adapterkey, adapter) != 0) {
			adapter = md->adapterkey;
			if (posn == cnt)
				foundnum = cnt2;
			cnt++;
		}
		if (posn == cnt)
			foundnum = cnt2;
		if (foundnum >= 0) {
			if (foundnum == displaynum)
				return false;
			workprefs.gfx_apmode[rtg ? APMODE_RTG : APMODE_NATIVE].gfx_display = foundnum + 1;
			init_displays_combo (hDlg, rtg);
			return true;
		}
		cnt++;
		cnt2++;
		md++;
	}
	return false;
}

static void values_from_displaydlg (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	BOOL success = FALSE;
	int i;
	struct monconfig *gm = &workprefs.gfx_monitor[0];
	int gfx_width = gm->gfx_size_win.width;
	int gfx_height = gm->gfx_size_win.height;
	int posn;
	TCHAR tmp[200];

	workprefs.gfx_apmode[0].gfx_fullscreen = xSendDlgItemMessage (hDlg, IDC_SCREENMODE_NATIVE, CB_GETCURSEL, 0, 0);
	workprefs.gfx_lores_mode = ischecked (hDlg, IDC_LORES_SMOOTHED);
	workprefs.gfx_scandoubler = ischecked (hDlg, IDC_FLICKERFIXER);
	workprefs.gfx_blackerthanblack = ischecked (hDlg, IDC_BLACKER_THAN_BLACK);
	workprefs.gfx_autoresolution_vga = ischecked(hDlg, IDC_AUTORESOLUTIONVGA);
	workprefs.gfx_grayscale = ischecked(hDlg, IDC_GRAYSCALE);
	workprefs.gfx_monitorblankdelay = ischecked(hDlg, IDC_RESYNCBLANK) ? 1000 : 0;

	int vres = workprefs.gfx_vresolution;
	int viscan = workprefs.gfx_iscanlines;
	int vpscan = workprefs.gfx_pscanlines;
	
	workprefs.gfx_vresolution = (ischecked (hDlg, IDC_LM_DOUBLED) || ischecked (hDlg, IDC_LM_SCANLINES) || ischecked (hDlg, IDC_LM_PDOUBLED2) || ischecked (hDlg, IDC_LM_PDOUBLED3)) ? VRES_DOUBLE : VRES_NONDOUBLE;
	workprefs.gfx_iscanlines = 0;
	workprefs.gfx_pscanlines = 0;
	if (workprefs.gfx_vresolution >= VRES_DOUBLE) {
		if (ischecked (hDlg, IDC_LM_IDOUBLED2))
			workprefs.gfx_iscanlines = 1;
		if (ischecked (hDlg, IDC_LM_IDOUBLED3))
			workprefs.gfx_iscanlines = 2;
		if (ischecked (hDlg, IDC_LM_SCANLINES))
			workprefs.gfx_pscanlines = 1;
		if (ischecked (hDlg, IDC_LM_PDOUBLED2))
			workprefs.gfx_pscanlines = 2;
		if (ischecked (hDlg, IDC_LM_PDOUBLED3))
			workprefs.gfx_pscanlines = 3;
	}
	if (vres != workprefs.gfx_vresolution || viscan != workprefs.gfx_iscanlines || vpscan != workprefs.gfx_pscanlines) {
		CheckRadioButton (hDlg, IDC_LM_NORMAL, IDC_LM_PDOUBLED3, IDC_LM_NORMAL + (workprefs.gfx_vresolution ? 1 : 0) + workprefs.gfx_pscanlines);
		CheckRadioButton (hDlg, IDC_LM_INORMAL, IDC_LM_IDOUBLED3, IDC_LM_INORMAL + (workprefs.gfx_iscanlines ? workprefs.gfx_iscanlines + 1: (workprefs.gfx_vresolution ? 1 : 0)));
	}

	workprefs.gfx_apmode[0].gfx_backbuffers = xSendDlgItemMessage (hDlg, IDC_DISPLAY_BUFFERCNT, CB_GETCURSEL, 0, 0) + 1;
	workprefs.gfx_framerate = xSendDlgItemMessage (hDlg, IDC_FRAMERATE, TBM_GETPOS, 0, 0);

	i = xSendDlgItemMessage (hDlg, IDC_SCREENMODE_NATIVE2, CB_GETCURSEL, 0, 0);
	int oldvsmode = workprefs.gfx_apmode[0].gfx_vsyncmode;
	int oldvs = workprefs.gfx_apmode[0].gfx_vsync;
	workprefs.gfx_apmode[0].gfx_vsync = 0;
	workprefs.gfx_apmode[0].gfx_vsyncmode = 0;
	if (i == 1) {
		workprefs.gfx_apmode[0].gfx_vsync = 1;
		workprefs.gfx_apmode[0].gfx_vsyncmode = 1;
	} else if (i == 2) {
		workprefs.gfx_apmode[0].gfx_vsync = 2;
		workprefs.gfx_apmode[0].gfx_vsyncmode = 1;
	} else if (i == 3) {
		workprefs.gfx_apmode[0].gfx_vsync = 1;
		workprefs.gfx_apmode[0].gfx_vsyncmode = 0;
	} else if (i == 4) {
		workprefs.gfx_apmode[0].gfx_vsync = 2;
		workprefs.gfx_apmode[0].gfx_vsyncmode = 0;
	} else if (i == 5) {
		workprefs.gfx_apmode[0].gfx_vsync = -1;
		workprefs.gfx_apmode[0].gfx_vsyncmode = 0;
	}

	i = xSendDlgItemMessage(hDlg, IDC_SCREENMODE_NATIVE3, CB_GETCURSEL, 0, 0);
	if (i >= 0 && i < 100)
		workprefs.gfx_display_sections = i + 1;

	workprefs.gfx_apmode[1].gfx_fullscreen = xSendDlgItemMessage (hDlg, IDC_SCREENMODE_RTG, CB_GETCURSEL, 0, 0);
	i = xSendDlgItemMessage (hDlg, IDC_SCREENMODE_RTG2, CB_GETCURSEL, 0, 0);
	workprefs.gfx_apmode[1].gfx_vsync = 0;
	workprefs.gfx_apmode[1].gfx_vsyncmode = 0;
	if (i == 1) {
		workprefs.gfx_apmode[1].gfx_vsync = 1;
		workprefs.gfx_apmode[1].gfx_vsyncmode = 1;
	} else if (i == 2) {
		workprefs.gfx_apmode[1].gfx_vsync = -1;
		workprefs.gfx_apmode[1].gfx_vsyncmode = 0;
	}
	
	bool updaterate = false, updateslider = false;
	TCHAR label[16];
	label[0] = 0;
	xSendDlgItemMessage (hDlg, IDC_RATE2BOX, WM_GETTEXT, sizeof label / sizeof (TCHAR), (LPARAM)label);
	struct chipset_refresh *cr;
	for (i = 0; i < MAX_CHIPSET_REFRESH_TOTAL; i++) {
		cr = &workprefs.cr[i];
		if (!_tcscmp (label, cr->label) || (cr->label[0] == 0 && label[0] == ':' &&_tstol (label + 1) == i)) {
			if (workprefs.cr_selected != i) {
				workprefs.cr_selected = i;
				updaterate = true;
				updateslider = true;
				CheckDlgButton (hDlg, IDC_RATE2ENABLE, cr->locked);
				ew (hDlg, IDC_FRAMERATE2, cr->locked != 0);
				ew (hDlg, IDC_RATE2TEXT, cr->locked != 0);
			} else {
				cr->locked = ischecked (hDlg, IDC_RATE2ENABLE) != 0;
				if (cr->locked) {
					cr->inuse = true;
				} else {
					// deactivate if plain uncustomized PAL or NTSC
					if (!cr->commands[0] && !cr->filterprofile[0] && cr->resolution == 7 &&
						cr->horiz < 0 && cr->vert < 0 && cr->lace < 0 && cr->vsync < 0 && cr->framelength < 0 &&
						(cr == &workprefs.cr[CHIPSET_REFRESH_PAL] || cr == &workprefs.cr[CHIPSET_REFRESH_NTSC])) {
						cr->inuse = false;
					}
				}
			}
			break;
		}
	}
	if (cr->locked) {
		if (msg == WM_HSCROLL) {
			i = xSendDlgItemMessage (hDlg, IDC_FRAMERATE2, TBM_GETPOS, 0, 0);
			if (i != (int)cr->rate)
				cr->rate = (float)i;
			updaterate = true;
		} else if (LOWORD (wParam) == IDC_RATE2TEXT && HIWORD (wParam) == EN_KILLFOCUS) {
			if (GetDlgItemText(hDlg, IDC_RATE2TEXT, tmp, sizeof tmp / sizeof (TCHAR))) {
				cr->rate = (float)_tstof(tmp);
				updaterate = true;
				updateslider = true;
			}
		}
	} else if (i == CHIPSET_REFRESH_PAL) {
		cr->rate = 50.0f;
	} else if (i == CHIPSET_REFRESH_NTSC) {
		cr->rate = 60.0f;
	}
	if (cr->rate > 0 && cr->rate < 1) {
		cr->rate = currprefs.ntscmode ? 60.0f : 50.0f;
		updaterate = true;
	}
	if (cr->rate > 300) {
		cr->rate = currprefs.ntscmode ? 60.0f : 50.0f;
		updaterate = true;
	}
	if (updaterate) {
		TCHAR buffer[20];
		_stprintf (buffer, _T("%.6f"), cr->rate);
		SetDlgItemText (hDlg, IDC_RATE2TEXT, buffer);
	}
	if (updateslider) {
		xSendDlgItemMessage (hDlg, IDC_FRAMERATE2, TBM_SETPOS, TRUE, (LPARAM)cr->rate);
	}

	workprefs.gfx_xcenter = ischecked (hDlg, IDC_XCENTER) ? 2 : 0; /* Smart centering */
	workprefs.gfx_ycenter = ischecked (hDlg, IDC_YCENTER) ? 2 : 0; /* Smart centering */
	workprefs.gfx_variable_sync = ischecked(hDlg, IDC_DISPLAY_VARSYNC) ? 1 : 0;
	workprefs.gfx_windowed_resize = ischecked(hDlg, IDC_DISPLAY_RESIZE);

	int posn1 = xSendDlgItemMessage (hDlg, IDC_AUTORESOLUTIONSELECT, CB_GETCURSEL, 0, 0);
	if (posn1 != CB_ERR) {
		if (posn1 == 0)
			workprefs.gfx_autoresolution = 0;
		else if (posn1 == 1)
			workprefs.gfx_autoresolution = 1;
		else if (posn1 == 2)
			workprefs.gfx_autoresolution = 10;
		else if (posn1 == 3)
			workprefs.gfx_autoresolution = 33;
		else if (posn1 == 4)
			workprefs.gfx_autoresolution = 66;
		else
			workprefs.gfx_autoresolution = 100;
	}

	int dmode = -1;
	bool native = false;
	struct MultiDisplay *md = getdisplay(&workprefs, 0);
	posn1 = xSendDlgItemMessage (hDlg, IDC_RESOLUTION, CB_GETCURSEL, 0, 0);
	if (posn1 != CB_ERR) {
		workprefs.gfx_monitor[0].gfx_size_fs.special = 0;
		for (dmode = 0; md->DisplayModes[dmode].inuse; dmode++) {
			if (md->DisplayModes[dmode].residx == posn1)
				break;
		}
		if (!md->DisplayModes[dmode].inuse) {
			for (dmode = 0; md->DisplayModes[dmode].inuse; dmode++) {
				if (md->DisplayModes[dmode].res.width == md->rect.right - md->rect.left &&
					md->DisplayModes[dmode].res.height == md->rect.bottom - md->rect.top)
					{
						workprefs.gfx_monitor[0].gfx_size_fs.special = WH_NATIVE;
						break;
				}
			}
			if (!md->DisplayModes[dmode].inuse) {
				dmode = -1;
			}
		}
	}

	if (oldvsmode != workprefs.gfx_apmode[0].gfx_vsyncmode || oldvs != workprefs.gfx_apmode[0].gfx_vsync)
		init_frequency_combo (hDlg, dmode);

	if (msg == WM_COMMAND && HIWORD (wParam) == CBN_SELCHANGE)
	{
		if (LOWORD (wParam) == IDC_DISPLAYSELECT) {
			get_displays_combo (hDlg, false);
			init_resolution_combo (hDlg);
			init_display_mode (hDlg);
			return;
		} else if (LOWORD(wParam) == IDC_LORES) {
			posn = xSendDlgItemMessage(hDlg, IDC_LORES, CB_GETCURSEL, 0, 0);
			if (posn != CB_ERR)
				workprefs.gfx_resolution = posn;
		} else if (LOWORD(wParam) == IDC_OVERSCANMODE) {
			posn = xSendDlgItemMessage(hDlg, IDC_OVERSCANMODE, CB_GETCURSEL, 0, 0);
			if (posn != CB_ERR)
				workprefs.gfx_overscanmode = posn;
		} else if (LOWORD (wParam) == IDC_RESOLUTION && dmode >= 0) {
			workprefs.gfx_monitor[0].gfx_size_fs.width  = md->DisplayModes[dmode].res.width;
			workprefs.gfx_monitor[0].gfx_size_fs.height = md->DisplayModes[dmode].res.height;
			/* Set the Int boxes */
			SetDlgItemInt (hDlg, IDC_XSIZE, workprefs.gfx_monitor[0].gfx_size_win.width, FALSE);
			SetDlgItemInt (hDlg, IDC_YSIZE, workprefs.gfx_monitor[0].gfx_size_win.height, FALSE);
			init_display_mode (hDlg);
		} else if (LOWORD (wParam) == IDC_REFRESHRATE && dmode >= 0) {
			LRESULT posn1;
			posn1 = xSendDlgItemMessage (hDlg, IDC_REFRESHRATE, CB_GETCURSEL, 0, 0);
			if (posn1 == CB_ERR)
				return;
			if (posn1 == 0) {
				workprefs.gfx_apmode[APMODE_NATIVE].gfx_refreshrate = 0;
				workprefs.gfx_apmode[APMODE_NATIVE].gfx_interlaced = dmode >= 0 && md->DisplayModes[dmode].lace;
			} else {
				posn1--;
				workprefs.gfx_apmode[APMODE_NATIVE].gfx_refreshrate = storedrefreshrates[posn1].rate;
				workprefs.gfx_apmode[APMODE_NATIVE].gfx_interlaced = (storedrefreshrates[posn1].type & REFRESH_RATE_LACE) != 0;
			}
			values_to_displaydlg (hDlg);
		} else if (LOWORD (wParam) == IDC_DA_MODE) {
			da_mode_selected = xSendDlgItemMessage (hDlg, IDC_DA_MODE, CB_GETCURSEL, 0, 0);
			init_da (hDlg);
			handle_da (hDlg);
		}
	}

	updatewinfsmode(0, &workprefs);
}

static int hw3d_changed;

static INT_PTR CALLBACK DisplayDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;

	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch (msg)
	{
	case WM_INITDIALOG:
		pages[DISPLAY_ID] = hDlg;
		currentpage = DISPLAY_ID;
		xSendDlgItemMessage (hDlg, IDC_FRAMERATE, TBM_SETPAGESIZE, 0, 1);
		xSendDlgItemMessage (hDlg, IDC_FRAMERATE, TBM_SETRANGE, TRUE, MAKELONG (MIN_REFRESH_RATE, MAX_REFRESH_RATE));
		xSendDlgItemMessage (hDlg, IDC_FRAMERATE2, TBM_SETPAGESIZE, 0, 1);
		xSendDlgItemMessage (hDlg, IDC_FRAMERATE2, TBM_SETRANGE, TRUE, MAKELONG (1, 99));
		recursive++;
		init_displays_combo (hDlg, false);
		init_resolution_combo (hDlg);
		init_da (hDlg);
		recursive--;

	case WM_USER:
		recursive++;
		values_to_displaydlg (hDlg);
		enable_for_displaydlg (hDlg);
		recursive--;
		break;

	case WM_HSCROLL:
	case WM_COMMAND:
		if (recursive > 0)
			break;
		recursive++;
		switch(LOWORD(wParam))
		{
			case IDC_DA_RESET:
			{
				int *p;
				da_mode_selected = xSendDlgItemMessage(hDlg, IDC_DA_MODE, CB_GETCURSEL, 0, 0);
				p = getp_da(hDlg);
				if (p)
					*p = 0;
				init_da(hDlg);
				update_da(hDlg);
			}
			break;
			case IDC_XSIZE:
			{
				BOOL success;
				struct monconfig *gm = &workprefs.gfx_monitor[0];
				gm->gfx_size_win.width = GetDlgItemInt(hDlg, IDC_XSIZE, &success, FALSE);
				if (!success)
					gm->gfx_size_win.width = 800;
			}
			break;
			case IDC_YSIZE:
			{
				BOOL success;
				struct monconfig *gm = &workprefs.gfx_monitor[0];
				gm->gfx_size_win.height = GetDlgItemInt(hDlg, IDC_YSIZE, &success, FALSE);
				if (!success)
					gm->gfx_size_win.height = 600;
			}
			break;
			default:
			handle_da (hDlg);
			values_from_displaydlg (hDlg, msg, wParam, lParam);
			enable_for_displaydlg (hDlg);
			if (LOWORD (wParam) == IDC_RATE2ENABLE || LOWORD(wParam) == IDC_SCREENMODE_NATIVE3 || LOWORD(wParam) == IDC_SCREENMODE_NATIVE2 || LOWORD(wParam) == IDC_SCREENMODE_NATIVE) {
				values_to_displaydlg (hDlg);
			}
		}
		recursive--;
		break;

	}
	if (hw3d_changed && recursive == 0) {
		recursive++;
		enable_for_displaydlg (hDlg);
		values_to_displaydlg (hDlg);
		hw3d_changed = 0;
		recursive--;
	}
	return FALSE;
}

static void values_to_chipsetdlg (HWND hDlg)
{
	TCHAR Nth[MAX_NTH_LENGTH];
	TCHAR *blah[1] = { Nth };
	TCHAR *string = NULL;

	switch(workprefs.chipset_mask)
	{
	case CSMASK_A1000_NOEHB:
		CheckRadioButton(hDlg, IDC_OCS, IDC_OCSA1000NOEHB, IDC_OCS + 6);
		break;
	case CSMASK_A1000:
		CheckRadioButton(hDlg, IDC_OCS, IDC_OCSA1000, IDC_OCS + 5);
		break;
	case CSMASK_OCS:
		CheckRadioButton(hDlg, IDC_OCS, IDC_OCSA1000, IDC_OCS + 0);
		break;
	case CSMASK_ECS_AGNUS:
		CheckRadioButton(hDlg, IDC_OCS, IDC_OCSA1000, IDC_OCS + 1);
		break;
	case CSMASK_ECS_DENISE:
		CheckRadioButton(hDlg, IDC_OCS, IDC_OCSA1000, IDC_OCS + 2);
		break;
	case CSMASK_ECS_AGNUS | CSMASK_ECS_DENISE:
		CheckRadioButton(hDlg, IDC_OCS, IDC_OCSA1000, IDC_OCS + 3);
		break;
	case CSMASK_AGA:
	case CSMASK_ECS_AGNUS | CSMASK_ECS_DENISE | CSMASK_AGA:
		CheckRadioButton(hDlg, IDC_OCS, IDC_OCSA1000, IDC_OCS + 4);
		break;
	}
	CheckDlgButton(hDlg, IDC_NTSC, workprefs.ntscmode);
	CheckDlgButton(hDlg, IDC_GENLOCK, workprefs.genlock);
	CheckDlgButton(hDlg, IDC_BLITIMM, workprefs.immediate_blits);
	CheckDlgButton(hDlg, IDC_BLITWAIT, workprefs.waiting_blits);
	CheckDlgButton(hDlg, IDC_KEYBOARDNKRO, workprefs.keyboard_nkro);
	xSendDlgItemMessage(hDlg, IDC_KEYBOARDMODE, CB_SETCURSEL, workprefs.keyboard_mode + 1, 0);
	xSendDlgItemMessage(hDlg, IDC_CS_HVCSYNC, CB_SETCURSEL, workprefs.cs_hvcsync, 0);

	CheckRadioButton(hDlg, IDC_COLLISION0, IDC_COLLISION3, IDC_COLLISION0 + workprefs.collision_level);
	CheckDlgButton(hDlg, IDC_CYCLEEXACT, workprefs.cpu_cycle_exact);
	CheckDlgButton(hDlg, IDC_CYCLEEXACTMEMORY, workprefs.cpu_memory_cycle_exact);
	xSendDlgItemMessage(hDlg, IDC_CS_EXT, CB_SETCURSEL, workprefs.cs_compatible, 0);
	xSendDlgItemMessage(hDlg, IDC_DISPLAY_OPTIMIZATION, CB_SETCURSEL, workprefs.cs_optimizations, 0);
	xSendDlgItemMessage(hDlg, IDC_MONITOREMU, CB_SETCURSEL, workprefs.monitoremu, 0);
	xSendDlgItemMessage(hDlg, IDC_MONITOREMU_MON, CB_SETCURSEL, workprefs.monitoremu_mon, 0);
	xSendDlgItemMessage(hDlg, IDC_GENLOCKMODE, CB_SETCURSEL, workprefs.genlock_image, 0);
	xSendDlgItemMessage(hDlg, IDC_GENLOCKMIX, CB_SETCURSEL, workprefs.genlock_mix / 25, 0);
	CheckDlgButton(hDlg, IDC_GENLOCK_ALPHA, workprefs.genlock_alpha);
	CheckDlgButton(hDlg, IDC_GENLOCK_KEEP_ASPECT, workprefs.genlock_aspect);
}

static int cs_compatible = CP_GENERIC;

static void values_from_chipsetdlg (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	BOOL success = FALSE;
	int nn;
	bool n1, n2;
	int id = LOWORD(wParam);

	workprefs.genlock = ischecked (hDlg, IDC_GENLOCK);
	workprefs.genlock_alpha = ischecked(hDlg, IDC_GENLOCK_ALPHA);
	workprefs.genlock_aspect = ischecked(hDlg, IDC_GENLOCK_KEEP_ASPECT);

	workprefs.immediate_blits = ischecked (hDlg, IDC_BLITIMM);
	workprefs.waiting_blits = ischecked (hDlg, IDC_BLITWAIT) ? 1 : 0;
	workprefs.keyboard_nkro = ischecked(hDlg, IDC_KEYBOARDNKRO);
	nn = xSendDlgItemMessage(hDlg, IDC_KEYBOARDMODE, CB_GETCURSEL, 0, 0);
	if (nn != CB_ERR) {
		workprefs.keyboard_mode = nn - 1;
	}
	int val = xSendDlgItemMessage(hDlg, IDC_CS_HVCSYNC, CB_GETCURSEL, 0, 0L);
	if (val != CB_ERR)
		workprefs.cs_hvcsync = val;

	n2 = ischecked (hDlg, IDC_CYCLEEXACTMEMORY);
	n1 = ischecked (hDlg, IDC_CYCLEEXACT);
	if (workprefs.cpu_cycle_exact != n1 || workprefs.cpu_memory_cycle_exact != n2) {
		if (id == IDC_CYCLEEXACTMEMORY) {
			if (n2) {
				// n2: f -> t
				if (workprefs.cpu_model < 68020) {
					n1 = true;
					CheckDlgButton (hDlg, IDC_CYCLEEXACT, n1);
				}
			} else {
				// n2: t -> f
				n1 = false;
				CheckDlgButton (hDlg, IDC_CYCLEEXACT, n1);
			}
		} else if (id == IDC_CYCLEEXACT) {
			if (n1) {
				// n1: f -> t
				n2 = true;
				CheckDlgButton (hDlg, IDC_CYCLEEXACTMEMORY, n2);
			} else {
				// n1: t -> f
				if (workprefs.cpu_model < 68020) {
					n2 = false;
					CheckDlgButton (hDlg, IDC_CYCLEEXACTMEMORY, n2);
				}
			}
		}
		workprefs.cpu_cycle_exact = n1;
		workprefs.cpu_memory_cycle_exact = workprefs.blitter_cycle_exact = n2;
		if (n2) {
			if (workprefs.cpu_model <= 68030) {
				workprefs.m68k_speed = 0;
				workprefs.cpu_compatible = 1;
			}
			if (workprefs.immediate_blits) {
				workprefs.immediate_blits = false;
				CheckDlgButton (hDlg, IDC_BLITIMM, FALSE);
			}
			workprefs.gfx_framerate = 1;
			workprefs.cachesize = 0;
		}
	}

	workprefs.collision_level = ischecked(hDlg, IDC_COLLISION0) ? 0
		: ischecked(hDlg, IDC_COLLISION1) ? 1
		: ischecked(hDlg, IDC_COLLISION2) ? 2 : 3;
	workprefs.chipset_mask = ischecked(hDlg, IDC_OCS) ? CSMASK_OCS
		: ischecked(hDlg, IDC_OCSA1000NOEHB) ? CSMASK_A1000_NOEHB
		: ischecked(hDlg, IDC_OCSA1000) ? CSMASK_A1000
		: ischecked(hDlg, IDC_ECS_AGNUS) ? CSMASK_ECS_AGNUS
		: ischecked(hDlg, IDC_ECS_DENISE) ? CSMASK_ECS_DENISE
		: ischecked(hDlg, IDC_ECS) ? CSMASK_ECS_AGNUS | CSMASK_ECS_DENISE
		: CSMASK_AGA | CSMASK_ECS_AGNUS | CSMASK_ECS_DENISE;

	n1 = ischecked (hDlg, IDC_NTSC);
	if (workprefs.ntscmode != n1) {
		workprefs.ntscmode = n1;
	}
	nn = xSendDlgItemMessage (hDlg, IDC_CS_EXT, CB_GETCURSEL, 0, 0);
	if (nn != CB_ERR) {
		workprefs.cs_compatible = nn;
		cs_compatible = nn;
		built_in_chipset_prefs (&workprefs);
	}
	nn = xSendDlgItemMessage(hDlg, IDC_DISPLAY_OPTIMIZATION, CB_GETCURSEL, 0, 0);
	if (nn != CB_ERR) {
		workprefs.cs_optimizations = nn;
	}
	nn = xSendDlgItemMessage(hDlg, IDC_MONITOREMU, CB_GETCURSEL, 0, 0);
	if (nn != CB_ERR)
		workprefs.monitoremu = nn;
	nn = xSendDlgItemMessage(hDlg, IDC_MONITOREMU_MON, CB_GETCURSEL, 0, 0);
	if (nn != CB_ERR)
		workprefs.monitoremu_mon = nn;

	nn = xSendDlgItemMessage(hDlg, IDC_GENLOCKMODE, CB_GETCURSEL, 0, 0);
	if (nn != CB_ERR && nn != workprefs.genlock_image) {
		workprefs.genlock_image = nn;
		if (workprefs.genlock_image == 3) {
			xSendDlgItemMessage(hDlg, IDC_GENLOCKFILE, WM_SETTEXT, 0, (LPARAM)workprefs.genlock_image_file);
		} else if (workprefs.genlock_image == 4) {
			xSendDlgItemMessage(hDlg, IDC_GENLOCKFILE, WM_SETTEXT, 0, (LPARAM)workprefs.genlock_video_file);
		}
	}
	nn = xSendDlgItemMessage(hDlg, IDC_GENLOCKMIX, CB_GETCURSEL, 0, 0);
	if (nn != CB_ERR) {
		workprefs.genlock_mix = nn * 25;
		if (workprefs.genlock_mix >= 250)
			workprefs.genlock_mix = 255;
	}
}

static void setgenlock(HWND hDlg)
{
	setautocomplete(hDlg, IDC_GENLOCKFILE);
	if (workprefs.genlock_image == 3) {
		addhistorymenu(hDlg, workprefs.genlock_image_file, IDC_GENLOCKFILE, HISTORY_GENLOCK_IMAGE, true, -1);
	} else if (workprefs.genlock_image == 4 || workprefs.genlock_image >= 6) {
		addhistorymenu(hDlg, workprefs.genlock_video_file, IDC_GENLOCKFILE, HISTORY_GENLOCK_VIDEO, true, -1);
	}
}

static void appendkbmcurom(TCHAR *s, bool hasrom)
{
	if (!hasrom) {
		_tcscat(s, _T(" [ROM not found]"));
	}
}

static INT_PTR CALLBACK ChipsetDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;
	TCHAR buffer[MAX_DPATH], tmp[MAX_DPATH];

	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch (msg) {
	case WM_INITDIALOG:
	{
		pages[CHIPSET_ID] = hDlg;
		currentpage = CHIPSET_ID;
		int ids1[] = { 321, -1 };
		int ids2[] = { 322, -1 };
		int ids3[] = { 323, -1 };
		struct romlist *has65001 = NULL;
		struct romlist *has657036 = getromlistbyids(ids1, NULL);
		struct romlist *has6805 = getromlistbyids(ids2, NULL);
		struct romlist *has8039 = getromlistbyids(ids3, NULL);

		xSendDlgItemMessage(hDlg, IDC_KEYBOARDMODE, CB_RESETCONTENT, 0, 0);
		WIN32GUI_LoadUIString(IDS_KEYBOARD_DISCONNECTED, tmp, sizeof(tmp) / sizeof(TCHAR));
		xSendDlgItemMessage(hDlg, IDC_KEYBOARDMODE, CB_ADDSTRING, 0, (LPARAM)tmp);
		WIN32GUI_LoadUIString(IDS_KEYBOARD_HIGHLEVEL, tmp, sizeof(tmp) / sizeof(TCHAR));
		xSendDlgItemMessage(hDlg, IDC_KEYBOARDMODE, CB_ADDSTRING, 0, (LPARAM)tmp);
		_tcscpy(tmp, _T("A500 / A500+ (6570-036 MCU)"));
		appendkbmcurom(tmp, has657036);
		xSendDlgItemMessage(hDlg, IDC_KEYBOARDMODE, CB_ADDSTRING, 0, tmp);
		_tcscpy(tmp, _T("A600 (6570-036 MCU)"));
		appendkbmcurom(tmp, has657036);
		xSendDlgItemMessage(hDlg, IDC_KEYBOARDMODE, CB_ADDSTRING, 0, tmp);
		_tcscpy(tmp, _T("A1000 (6500-1 MCU. ROM not yet dumped)"));
		appendkbmcurom(tmp, has65001);
		xSendDlgItemMessage(hDlg, IDC_KEYBOARDMODE, CB_ADDSTRING, 0, tmp);
		_tcscpy(tmp, _T("A1000 (6570-036 MCU)"));
		appendkbmcurom(tmp, has657036);
		xSendDlgItemMessage(hDlg, IDC_KEYBOARDMODE, CB_ADDSTRING, 0, tmp);
		_tcscpy(tmp, _T("A1200 (68HC05C MCU)"));
		appendkbmcurom(tmp, has6805);
		xSendDlgItemMessage(hDlg, IDC_KEYBOARDMODE, CB_ADDSTRING, 0, tmp);
		_tcscpy(tmp, _T("A2000 (Cherry, 8039 MCU)"));
		appendkbmcurom(tmp, has8039);
		xSendDlgItemMessage(hDlg, IDC_KEYBOARDMODE, CB_ADDSTRING, 0, tmp);
		_tcscpy(tmp, _T("A2000/A3000/A4000 (6570-036 MCU)"));
		appendkbmcurom(tmp, has657036);
		xSendDlgItemMessage(hDlg, IDC_KEYBOARDMODE, CB_ADDSTRING, 0, tmp);

		xSendDlgItemMessage(hDlg, IDC_DISPLAY_OPTIMIZATION, CB_RESETCONTENT, 0, 0);
		WIN32GUI_LoadUIString(IDS_DISPLAY_OPTIMIZATION_FULL, tmp, sizeof(tmp) / sizeof(TCHAR));
		xSendDlgItemMessage(hDlg, IDC_DISPLAY_OPTIMIZATION, CB_ADDSTRING, 0, (LPARAM)tmp);
		WIN32GUI_LoadUIString(IDS_DISPLAY_OPTIMIZATION_PARTIAL, tmp, sizeof(tmp) / sizeof(TCHAR));
		xSendDlgItemMessage(hDlg, IDC_DISPLAY_OPTIMIZATION, CB_ADDSTRING, 0, (LPARAM)tmp);
		WIN32GUI_LoadUIString(IDS_DISPLAY_OPTIMIZATION_NONE, tmp, sizeof(tmp) / sizeof(TCHAR));
		xSendDlgItemMessage(hDlg, IDC_DISPLAY_OPTIMIZATION, CB_ADDSTRING, 0, (LPARAM)tmp);

		xSendDlgItemMessage(hDlg, IDC_CS_EXT, CB_RESETCONTENT, 0, 0);
		xSendDlgItemMessage(hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)_T("Custom"));
		WIN32GUI_LoadUIString(IDS_GENERIC, buffer, sizeof buffer / sizeof(TCHAR));
		xSendDlgItemMessage(hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)buffer);
		xSendDlgItemMessage(hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)_T("CDTV"));
		xSendDlgItemMessage(hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)_T("CDTV-CR"));
		xSendDlgItemMessage(hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)_T("CD32"));
		xSendDlgItemMessage(hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)_T("A500"));
		xSendDlgItemMessage(hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)_T("A500+"));
		xSendDlgItemMessage(hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)_T("A600"));
		xSendDlgItemMessage(hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)_T("A1000"));
		xSendDlgItemMessage(hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)_T("A1200"));
		xSendDlgItemMessage(hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)_T("A2000"));
		xSendDlgItemMessage(hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)_T("A3000"));
		xSendDlgItemMessage(hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)_T("A3000T"));
		xSendDlgItemMessage(hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)_T("A4000"));
		xSendDlgItemMessage(hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)_T("A4000T"));
		xSendDlgItemMessage(hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)_T("Velvet"));
		xSendDlgItemMessage(hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)_T("Casablanca"));
		xSendDlgItemMessage(hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)_T("DraCo"));

		xSendDlgItemMessage(hDlg, IDC_GENLOCKMODE, CB_RESETCONTENT, 0, 0);
		xSendDlgItemMessage(hDlg, IDC_GENLOCKMODE, CB_ADDSTRING, 0, (LPARAM)_T("-"));
		WIN32GUI_LoadUIString(IDS_GENLOCK_OPTIONS, tmp, sizeof tmp / sizeof(TCHAR));
		TCHAR *p1 = tmp;
		for (;;) {
			TCHAR *p2 = _tcschr(p1, '\n');
			if (p2 && _tcslen(p2) > 0) {
				*p2++ = 0;
				xSendDlgItemMessage(hDlg, IDC_GENLOCKMODE, CB_ADDSTRING, 0, (LPARAM)p1);
				p1 = p2;
			} else
				break;
		}
		xSendDlgItemMessage(hDlg, IDC_GENLOCKMIX, CB_RESETCONTENT, 0, 0);
		for (int i = 0; i <= 10; i++) {
			_stprintf(buffer, _T("%d%%"), (10 - i) * 10);
			xSendDlgItemMessage(hDlg, IDC_GENLOCKMIX, CB_ADDSTRING, 0, (LPARAM)buffer);
		}

		xSendDlgItemMessage(hDlg, IDC_MONITOREMU, CB_RESETCONTENT, 0, 0);
		xSendDlgItemMessage(hDlg, IDC_MONITOREMU, CB_ADDSTRING, 0, (LPARAM)_T("-"));
		WIN32GUI_LoadUIString(IDS_AUTODETECT, buffer, sizeof buffer / sizeof(TCHAR));
		xSendDlgItemMessage(hDlg, IDC_MONITOREMU, CB_ADDSTRING, 0, (LPARAM)buffer);
		for (int i = 0; specialmonitorfriendlynames[i]; i++) {
			_stprintf(buffer, _T("%s (%s)"), specialmonitorfriendlynames[i], specialmonitormanufacturernames[i]);
			xSendDlgItemMessage(hDlg, IDC_MONITOREMU, CB_ADDSTRING, 0, (LPARAM)buffer);
		}

		xSendDlgItemMessage(hDlg, IDC_MONITOREMU_MON, CB_RESETCONTENT, 0, 0);
		for (int i = 0; i < MAX_AMIGAMONITORS; i++) {
			_stprintf(buffer, _T("%d"), i + 1);
			xSendDlgItemMessage(hDlg, IDC_MONITOREMU_MON, CB_ADDSTRING, 0, (LPARAM)buffer);
		}

		xSendDlgItemMessage(hDlg, IDC_CS_HVCSYNC, CB_RESETCONTENT, 0, 0);
		WIN32GUI_LoadUIString(IDS_SYNCMODE_COMBINED, buffer, sizeof buffer / sizeof(TCHAR));
		xSendDlgItemMessage(hDlg, IDC_CS_HVCSYNC, CB_ADDSTRING, 0, (LPARAM)buffer);
		WIN32GUI_LoadUIString(IDS_SYNCMODE_CSYNC, buffer, sizeof buffer / sizeof(TCHAR));
		xSendDlgItemMessage(hDlg, IDC_CS_HVCSYNC, CB_ADDSTRING, 0, (LPARAM)buffer);
		WIN32GUI_LoadUIString(IDS_SYNCMODE_HVSYNC, buffer, sizeof buffer / sizeof(TCHAR));
		xSendDlgItemMessage(hDlg, IDC_CS_HVCSYNC, CB_ADDSTRING, 0, (LPARAM)buffer);

#ifndef	AGA
		ew(hDlg, IDC_AGA, FALSE);
#endif

		setgenlock(hDlg);
	}

	case WM_USER:
		recursive++;
		values_to_chipsetdlg (hDlg);
		enable_for_chipsetdlg (hDlg);
		recursive--;
		break;

	case WM_COMMAND:
		if (recursive > 0)
			break;
		recursive++;

		if (HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == CBN_KILLFOCUS) {
			switch (LOWORD(wParam))
			{
				case IDC_GENLOCKFILE:
				{
					TCHAR *p = workprefs.genlock_image == 3 ? workprefs.genlock_image_file : workprefs.genlock_video_file;
					getcomboboxtext(hDlg, IDC_GENLOCKFILE, p, MAX_DPATH);
					parsefilepath(p, MAX_DPATH);
					addhistorymenu(hDlg, p, IDC_GENLOCKFILE, workprefs.genlock_image == 3 ? HISTORY_GENLOCK_IMAGE : HISTORY_GENLOCK_VIDEO, true, -1);
					break;
				}
			}
		}
		switch (LOWORD(wParam))
		{
			case IDC_GENLOCKFILESELECT:
			{
				TCHAR path[MAX_DPATH];
				path[0] = 0;
				DiskSelection(hDlg, IDC_GENLOCKFILESELECT, workprefs.genlock_image == 3 ? 20 : 21, &workprefs, NULL, path);
				break;
			}
			case IDC_BLITWAIT:
			if (workprefs.immediate_blits) {
				workprefs.immediate_blits = false;
				CheckDlgButton (hDlg, IDC_BLITIMM, FALSE);
			}
			break;
			case IDC_BLITIMM:
			if (workprefs.waiting_blits) {
				workprefs.waiting_blits = false;
				CheckDlgButton (hDlg, IDC_BLITWAIT, FALSE);
			}
			break;
		}
		values_from_chipsetdlg(hDlg, msg, wParam, lParam);
		enable_for_chipsetdlg(hDlg);
		recursive--;
		break;
	case WM_HSCROLL:
		if (recursive > 0)
			break;
		recursive++;
		values_from_chipsetdlg (hDlg, msg, wParam, lParam);
		enable_for_chipsetdlg( hDlg );
		recursive--;
		break;
	}
	return FALSE;
}

static void values_to_chipsetdlg2 (HWND hDlg)
{
	TCHAR txt[32];
	uae_u32 rev;

	switch(workprefs.cs_ciaatod)
	{
	case 0:
		CheckRadioButton(hDlg, IDC_CS_CIAA_TOD1, IDC_CS_CIAA_TOD3, IDC_CS_CIAA_TOD1);
		break;
	case 1:
		CheckRadioButton(hDlg, IDC_CS_CIAA_TOD1, IDC_CS_CIAA_TOD3, IDC_CS_CIAA_TOD2);
		break;
	case 2:
		CheckRadioButton(hDlg, IDC_CS_CIAA_TOD1, IDC_CS_CIAA_TOD3, IDC_CS_CIAA_TOD3);
		break;
	}
	switch(workprefs.cs_rtc)
	{
	case 0:
		CheckRadioButton(hDlg, IDC_CS_RTC1, IDC_CS_RTC4, IDC_CS_RTC1);
		break;
	case 1:
		CheckRadioButton(hDlg, IDC_CS_RTC1, IDC_CS_RTC4, IDC_CS_RTC2);
		break;
	case 2:
		CheckRadioButton(hDlg, IDC_CS_RTC1, IDC_CS_RTC4, IDC_CS_RTC3);
		break;
	case 3:
		CheckRadioButton(hDlg, IDC_CS_RTC1, IDC_CS_RTC4, IDC_CS_RTC4);
		break;
	}
	CheckDlgButton(hDlg, IDC_CS_COMPATIBLE, workprefs.cs_compatible != 0);
	CheckDlgButton(hDlg, IDC_CS_RESETWARNING, workprefs.cs_resetwarning);
	CheckDlgButton(hDlg, IDC_CS_KSMIRROR_E0, workprefs.cs_ksmirror_e0);
	CheckDlgButton(hDlg, IDC_CS_KSMIRROR_A8, workprefs.cs_ksmirror_a8);
	CheckDlgButton(hDlg, IDC_CS_CIAOVERLAY, workprefs.cs_ciaoverlay);
	CheckDlgButton(hDlg, IDC_CS_DF0IDHW, workprefs.cs_df0idhw);
	CheckDlgButton(hDlg, IDC_CS_CD32CD, workprefs.cs_cd32cd);
	CheckDlgButton(hDlg, IDC_CS_CD32C2P, workprefs.cs_cd32c2p);
	CheckDlgButton(hDlg, IDC_CS_CD32NVRAM, workprefs.cs_cd32nvram);
	CheckDlgButton(hDlg, IDC_CS_CDTVCD, workprefs.cs_cdtvcd);
	CheckDlgButton(hDlg, IDC_CS_CDTVCR, workprefs.cs_cdtvcr);
	CheckDlgButton(hDlg, IDC_CS_CDTVRAM, workprefs.cs_cdtvram);
	CheckDlgButton(hDlg, IDC_CS_A1000RAM, workprefs.cs_a1000ram);
	CheckDlgButton(hDlg, IDC_CS_RAMSEY, workprefs.cs_ramseyrev >= 0);
	CheckDlgButton(hDlg, IDC_CS_FATGARY, workprefs.cs_fatgaryrev >= 0);
	CheckDlgButton(hDlg, IDC_CS_AGNUS, workprefs.cs_agnusrev >= 0);
	CheckDlgButton(hDlg, IDC_CS_DENISE, workprefs.cs_deniserev >= 0);
	CheckDlgButton(hDlg, IDC_CS_DMAC, workprefs.cs_mbdmac & 1);
	CheckDlgButton(hDlg, IDC_CS_DMAC2, workprefs.cs_mbdmac & 2);
	CheckDlgButton(hDlg, IDC_CS_PCMCIA, workprefs.cs_pcmcia);
	CheckDlgButton(hDlg, IDC_CS_CIATODBUG, workprefs.cs_ciatodbug);
	CheckDlgButton(hDlg, IDC_CS_Z3AUTOCONFIG, workprefs.cs_z3autoconfig);
	CheckDlgButton(hDlg, IDC_CS_IDE1, workprefs.cs_ide > 0 && (workprefs.cs_ide & 1));
	CheckDlgButton(hDlg, IDC_CS_IDE2, workprefs.cs_ide > 0 && (workprefs.cs_ide & 2));
	CheckDlgButton(hDlg, IDC_CS_1MCHIPJUMPER, workprefs.cs_1mchipjumper || workprefs.chipmem.size >= 0x100000);
	CheckDlgButton(hDlg, IDC_CS_BYTECUSTOMWRITEBUG, workprefs.cs_bytecustomwritebug);
	CheckDlgButton(hDlg, IDC_CS_COMPOSITECOLOR, workprefs.cs_color_burst);
	CheckDlgButton(hDlg, IDC_CS_TOSHIBAGARY, workprefs.cs_toshibagary);
	CheckDlgButton(hDlg, IDC_CS_ROMISSLOW, workprefs.cs_romisslow);
	CheckDlgButton(hDlg, IDC_CS_CIA, workprefs.cs_ciatype[0]);
	CheckDlgButton(hDlg, IDC_CS_MEMORYPATTERN, workprefs.cs_memorypatternfill);
	xSendDlgItemMessage(hDlg, IDC_CS_UNMAPPED, CB_SETCURSEL, workprefs.cs_unmapped_space, 0);
	xSendDlgItemMessage(hDlg, IDC_CS_CIASYNC, CB_SETCURSEL, workprefs.cs_eclocksync, 0);
	xSendDlgItemMessage(hDlg, IDC_CS_AGNUSMODEL, CB_SETCURSEL, workprefs.cs_agnusmodel, 0);
	xSendDlgItemMessage(hDlg, IDC_CS_AGNUSSIZE, CB_SETCURSEL, workprefs.cs_agnussize, 0);
	xSendDlgItemMessage(hDlg, IDC_CS_DENISEMODEL, CB_SETCURSEL, workprefs.cs_denisemodel, 0);
	txt[0] = 0;
	_stprintf (txt, _T("%d"), workprefs.cs_rtc_adjust);
	SetDlgItemText(hDlg, IDC_CS_RTCADJUST, txt);
	txt[0] = 0;
	if (workprefs.cs_fatgaryrev >= 0)
		_stprintf (txt, _T("%02X"), workprefs.cs_fatgaryrev);
	SetDlgItemText(hDlg, IDC_CS_FATGARYREV, txt);
	txt[0] = 0;
	if (workprefs.cs_ramseyrev >= 0)
		_stprintf (txt, _T("%02X"), workprefs.cs_ramseyrev);
	SetDlgItemText(hDlg, IDC_CS_RAMSEYREV, txt);
	txt[0] = 0;
	if (workprefs.cs_agnusrev >= 0) {
		rev = workprefs.cs_agnusrev;
		_stprintf (txt, _T("%02X"), rev);
	} else if (workprefs.cs_compatible) {
		rev = 0;
		if (workprefs.ntscmode)
			rev |= 0x10;
		rev |= (workprefs.chipset_mask & CSMASK_AGA) ? 0x23 : 0;
		rev |= (workprefs.chipset_mask & CSMASK_ECS_AGNUS) ? 0x20 : 0;
		_stprintf (txt, _T("%02X"), rev);
	}
	SetDlgItemText(hDlg, IDC_CS_AGNUSREV, txt);
	txt[0] = 0;
	if (workprefs.cs_deniserev >= 0) {
		rev = workprefs.cs_deniserev;
		_stprintf (txt, _T("%01.1X"), rev);
	} else if (workprefs.cs_compatible) {
		rev = 0xf;
		if (workprefs.chipset_mask & CSMASK_ECS_DENISE)
			rev = 0xc;
		if (workprefs.chipset_mask & CSMASK_AGA)
			rev = 0x8;
		_stprintf (txt, _T("%01.1X"), rev);
	}
	SetDlgItemText(hDlg, IDC_CS_DENISEREV, txt);
}

static void values_from_chipsetdlg2 (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	TCHAR txt[32], *p;
	int v;

	if (!!workprefs.cs_compatible != ischecked(hDlg, IDC_CS_COMPATIBLE)) {
		if (ischecked(hDlg, IDC_CS_COMPATIBLE)) {
			if (!cs_compatible)
				cs_compatible = CP_GENERIC;
			workprefs.cs_compatible = cs_compatible;
		} else {
			workprefs.cs_compatible = 0;
		}
		built_in_chipset_prefs(&workprefs);
		values_to_chipsetdlg2(hDlg);
	}
	workprefs.cs_resetwarning = ischecked (hDlg, IDC_CS_RESETWARNING);
	workprefs.cs_ciatodbug = ischecked (hDlg, IDC_CS_CIATODBUG);
	workprefs.cs_ksmirror_e0 = ischecked (hDlg, IDC_CS_KSMIRROR_E0);
	workprefs.cs_ksmirror_a8 = ischecked (hDlg, IDC_CS_KSMIRROR_A8);
	workprefs.cs_ciaoverlay = ischecked (hDlg, IDC_CS_CIAOVERLAY);
	workprefs.cs_df0idhw = ischecked (hDlg, IDC_CS_DF0IDHW);
	workprefs.cs_cd32cd = ischecked (hDlg, IDC_CS_CD32CD);
	workprefs.cs_cd32c2p = ischecked (hDlg, IDC_CS_CD32C2P);
	workprefs.cs_cd32nvram = ischecked (hDlg, IDC_CS_CD32NVRAM);
	workprefs.cs_cdtvcd = ischecked (hDlg, IDC_CS_CDTVCD);
	workprefs.cs_cdtvcr = ischecked (hDlg, IDC_CS_CDTVCR);
	workprefs.cs_cdtvram = ischecked (hDlg, IDC_CS_CDTVRAM);
	workprefs.cs_a1000ram = ischecked (hDlg, IDC_CS_A1000RAM);
	workprefs.cs_ramseyrev = ischecked (hDlg, IDC_CS_RAMSEY) ? 0x0f : -1;
	workprefs.cs_fatgaryrev = ischecked (hDlg, IDC_CS_FATGARY) ? 0x00 : -1;
	workprefs.cs_mbdmac = ischecked (hDlg, IDC_CS_DMAC) ? 1 : 0;
	workprefs.cs_mbdmac |= ischecked (hDlg, IDC_CS_DMAC2) ? 2 : 0;
	workprefs.cs_pcmcia = ischecked (hDlg, IDC_CS_PCMCIA) ? 1 : 0;
	workprefs.cs_z3autoconfig = ischecked (hDlg, IDC_CS_Z3AUTOCONFIG) ? 1 : 0;
	workprefs.cs_ide = ischecked (hDlg, IDC_CS_IDE1) ? 1 : (ischecked (hDlg, IDC_CS_IDE2) ? 2 : 0);
	workprefs.cs_ciaatod = ischecked (hDlg, IDC_CS_CIAA_TOD1) ? 0
		: (ischecked (hDlg, IDC_CS_CIAA_TOD2) ? 1 : 2);
	workprefs.cs_rtc = ischecked (hDlg, IDC_CS_RTC1) ? 0
		: ischecked (hDlg, IDC_CS_RTC2) ? 1 : ischecked (hDlg, IDC_CS_RTC3) ? 2 : 3;
	workprefs.cs_1mchipjumper = ischecked(hDlg, IDC_CS_1MCHIPJUMPER);
	workprefs.cs_bytecustomwritebug = ischecked(hDlg, IDC_CS_BYTECUSTOMWRITEBUG);
	workprefs.cs_color_burst = ischecked(hDlg, IDC_CS_COMPOSITECOLOR);
	workprefs.cs_toshibagary = ischecked(hDlg, IDC_CS_TOSHIBAGARY);
	workprefs.cs_romisslow = ischecked(hDlg, IDC_CS_ROMISSLOW);
	workprefs.cs_ciatype[0] = workprefs.cs_ciatype[1] = ischecked(hDlg, IDC_CS_CIA);
	workprefs.cs_memorypatternfill = ischecked(hDlg, IDC_CS_MEMORYPATTERN);

	int val = xSendDlgItemMessage(hDlg, IDC_CS_UNMAPPED, CB_GETCURSEL, 0, 0L);
	if (val != CB_ERR)
		workprefs.cs_unmapped_space = val;

	val = xSendDlgItemMessage(hDlg, IDC_CS_CIASYNC, CB_GETCURSEL, 0, 0L);
	if (val != CB_ERR)
		workprefs.cs_eclocksync = val;

	val = xSendDlgItemMessage(hDlg, IDC_CS_AGNUSMODEL, CB_GETCURSEL, 0, 0L);
	if (val != CB_ERR)
		workprefs.cs_agnusmodel = val;

	val = xSendDlgItemMessage(hDlg, IDC_CS_AGNUSSIZE, CB_GETCURSEL, 0, 0L);
	if (val != CB_ERR)
		workprefs.cs_agnussize = val;

	val = xSendDlgItemMessage(hDlg, IDC_CS_DENISEMODEL, CB_GETCURSEL, 0, 0L);
	if (val != CB_ERR)
		workprefs.cs_denisemodel = val;

	cfgfile_compatibility_romtype(&workprefs);

	if (workprefs.cs_rtc) {
		txt[0] = 0;
		xSendDlgItemMessage (hDlg, IDC_CS_RTCADJUST, WM_GETTEXT, (WPARAM)sizeof (txt) / sizeof (TCHAR), (LPARAM)txt);
		workprefs.cs_rtc_adjust = _tstol(txt);
	}
	if (workprefs.cs_fatgaryrev >= 0) {
		txt[0] = 0;
		xSendDlgItemMessage (hDlg, IDC_CS_FATGARYREV, WM_GETTEXT, (WPARAM)sizeof (txt) / sizeof (TCHAR), (LPARAM)txt);
		v = _tcstol (txt, &p, 16);
		if (v >= 0 && v <= 255)
			workprefs.cs_fatgaryrev = v;
	}
	if (workprefs.cs_ramseyrev >= 0) {
		txt[0] = 0;
		xSendDlgItemMessage (hDlg, IDC_CS_RAMSEYREV, WM_GETTEXT, (WPARAM)sizeof (txt) / sizeof (TCHAR), (LPARAM)txt);
		v = _tcstol (txt, &p, 16);
		if (v >= 0 && v <= 255)
			workprefs.cs_ramseyrev = v;
	}
	if (workprefs.cs_agnusrev >= 0) {
		txt[0] = 0;
		xSendDlgItemMessage (hDlg, IDC_CS_AGNUSREV, WM_GETTEXT, (WPARAM)sizeof (txt) / sizeof (TCHAR), (LPARAM)txt);
		v = _tcstol (txt, &p, 16);
		if (v >= 0 && v <= 255)
			workprefs.cs_agnusrev = v;
	}
	if (workprefs.cs_deniserev >= 0) {
		txt[0] = 0;
		xSendDlgItemMessage (hDlg, IDC_CS_DENISEREV, WM_GETTEXT, (WPARAM)sizeof (txt) / sizeof (TCHAR), (LPARAM)txt);
		v = _tcstol (txt, &p, 16);
		if (v >= 0 && v <= 15)
			workprefs.cs_deniserev = v;
	}

}

static void enable_for_chipsetdlg2 (HWND hDlg)
{
	int e = workprefs.cs_compatible ? FALSE : TRUE;

	ew(hDlg, IDC_CS_FATGARY, e);
	ew(hDlg, IDC_CS_RAMSEY, e);
	ew(hDlg, IDC_CS_AGNUS, e);
	ew(hDlg, IDC_CS_DENISE, e);
	ew(hDlg, IDC_CS_FATGARYREV, e);
	ew(hDlg, IDC_CS_RAMSEYREV, e);
	ew(hDlg, IDC_CS_AGNUSREV, e);
	ew(hDlg, IDC_CS_DENISEREV, e);
	ew(hDlg, IDC_CS_IDE1, e);
	ew(hDlg, IDC_CS_IDE2, e);
	ew(hDlg, IDC_CS_DMAC, e);
	ew(hDlg, IDC_CS_DMAC2, e);
	ew(hDlg, IDC_CS_PCMCIA, e);
	ew(hDlg, IDC_CS_CD32CD, e);
	ew(hDlg, IDC_CS_CD32NVRAM, e);
	ew(hDlg, IDC_CS_CD32C2P, e);
	ew(hDlg, IDC_CS_CDTVCD, e);
	ew(hDlg, IDC_CS_CDTVCR, e);
	ew(hDlg, IDC_CS_CDTVRAM, e);
	ew(hDlg, IDC_CS_RESETWARNING, e);
	ew(hDlg, IDC_CS_CIATODBUG, e);
	ew(hDlg, IDC_CS_Z3AUTOCONFIG, e);
	ew(hDlg, IDC_CS_KSMIRROR_E0, e);
	ew(hDlg, IDC_CS_KSMIRROR_A8, e);
	ew(hDlg, IDC_CS_CIAOVERLAY, e);
	ew(hDlg, IDC_CS_A1000RAM, e);
	ew(hDlg, IDC_CS_DF0IDHW, e);
	ew(hDlg, IDC_CS_CIAA_TOD1, e);
	ew(hDlg, IDC_CS_CIAA_TOD2, e);
	ew(hDlg, IDC_CS_CIAA_TOD3, e);
	ew(hDlg, IDC_CS_RTC1, e);
	ew(hDlg, IDC_CS_RTC2, e);
	ew(hDlg, IDC_CS_RTC3, e);
	ew(hDlg, IDC_CS_RTC4, e);
	ew(hDlg, IDC_CS_RTCADJUST, e);
	ew(hDlg, IDC_CS_1MCHIPJUMPER, e && workprefs.chipmem.size < 0x100000);
	ew(hDlg, IDC_CS_BYTECUSTOMWRITEBUG, e);
	ew(hDlg, IDC_CS_COMPOSITECOLOR, e);
	ew(hDlg, IDC_CS_TOSHIBAGARY, e);
	ew(hDlg, IDC_CS_ROMISSLOW, e);
	ew(hDlg, IDC_CS_UNMAPPED, e);
	ew(hDlg, IDC_CS_CIASYNC, e);
	ew(hDlg, IDC_CS_CIA, e);
	ew(hDlg, IDC_CS_MEMORYPATTERN, e);
	ew(hDlg, IDC_CS_AGNUSMODEL, e);
	ew(hDlg, IDC_CS_AGNUSSIZE, e);
	ew(hDlg, IDC_CS_DENISEMODEL, e);
}

static INT_PTR CALLBACK ChipsetDlgProc2 (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;
	TCHAR tmp[MAX_DPATH];

	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch (msg) {
	case WM_INITDIALOG:
	{
		pages[CHIPSET2_ID] = hDlg;
		currentpage = CHIPSET2_ID;
		cs_compatible = workprefs.cs_compatible;
		xSendDlgItemMessage(hDlg, IDC_CS_CIASYNC, CB_RESETCONTENT, 0, 0L);
		xSendDlgItemMessage(hDlg, IDC_CS_CIASYNC, CB_ADDSTRING, 0, (LPARAM)_T("Autoselect"));
		xSendDlgItemMessage(hDlg, IDC_CS_CIASYNC, CB_ADDSTRING, 0, (LPARAM)_T("68000"));
		xSendDlgItemMessage(hDlg, IDC_CS_CIASYNC, CB_ADDSTRING, 0, (LPARAM)_T("Gayle"));
		xSendDlgItemMessage(hDlg, IDC_CS_CIASYNC, CB_ADDSTRING, 0, (LPARAM)_T("68000 Alternate"));
		xSendDlgItemMessage(hDlg, IDC_CS_AGNUSMODEL, CB_RESETCONTENT, 0, 0L);
		xSendDlgItemMessage(hDlg, IDC_CS_AGNUSMODEL, CB_ADDSTRING, 0, (LPARAM)_T("Auto"));
		xSendDlgItemMessage(hDlg, IDC_CS_AGNUSMODEL, CB_ADDSTRING, 0, (LPARAM)_T("Velvet"));
		xSendDlgItemMessage(hDlg, IDC_CS_AGNUSMODEL, CB_ADDSTRING, 0, (LPARAM)_T("A1000"));
		xSendDlgItemMessage(hDlg, IDC_CS_AGNUSSIZE, CB_RESETCONTENT, 0, 0L);
		xSendDlgItemMessage(hDlg, IDC_CS_AGNUSSIZE, CB_ADDSTRING, 0, (LPARAM)_T("Auto"));
		xSendDlgItemMessage(hDlg, IDC_CS_AGNUSSIZE, CB_ADDSTRING, 0, (LPARAM)_T("512k"));
		xSendDlgItemMessage(hDlg, IDC_CS_AGNUSSIZE, CB_ADDSTRING, 0, (LPARAM)_T("1M"));
		xSendDlgItemMessage(hDlg, IDC_CS_AGNUSSIZE, CB_ADDSTRING, 0, (LPARAM)_T("2M"));
		xSendDlgItemMessage(hDlg, IDC_CS_DENISEMODEL, CB_RESETCONTENT, 0, 0L);
		xSendDlgItemMessage(hDlg, IDC_CS_DENISEMODEL, CB_ADDSTRING, 0, (LPARAM)_T("Auto"));
		xSendDlgItemMessage(hDlg, IDC_CS_DENISEMODEL, CB_ADDSTRING, 0, (LPARAM)_T("Velvet"));
		xSendDlgItemMessage(hDlg, IDC_CS_DENISEMODEL, CB_ADDSTRING, 0, (LPARAM)_T("A1000 No-EHB"));
		xSendDlgItemMessage(hDlg, IDC_CS_DENISEMODEL, CB_ADDSTRING, 0, (LPARAM)_T("A1000"));
		xSendDlgItemMessage(hDlg, IDC_CS_UNMAPPED, CB_RESETCONTENT, 0, 0L);
		WIN32GUI_LoadUIString(IDS_UNMAPPED_ADDRESS, tmp, sizeof tmp / sizeof(TCHAR));
		TCHAR *p1 = tmp;
		for (;;) {
			TCHAR *p2 = _tcschr(p1, '\n');
			if (p2 && _tcslen(p2) > 0) {
				*p2++ = 0;
				xSendDlgItemMessage(hDlg, IDC_CS_UNMAPPED, CB_ADDSTRING, 0, (LPARAM)p1);
				p1 = p2;
			} else
				break;
		}
	}
	case WM_USER:
		recursive++;
		values_to_chipsetdlg2 (hDlg);
		enable_for_chipsetdlg2 (hDlg);
		recursive--;
		break;
	case WM_HSCROLL:
	case WM_COMMAND:
		if (recursive > 0)
			break;
		recursive++;
		values_from_chipsetdlg2 (hDlg, msg, wParam, lParam);
		enable_for_chipsetdlg2 (hDlg);
		recursive--;
		break;
	}
	return FALSE;
}

static int fastram_select;
static uae_u32 *fastram_select_pointer;
static const int *fastram_select_msi;
static struct ramboard *fastram_select_ramboard;
#define MAX_STANDARD_RAM_BOARDS 2

static void enable_for_memorydlg (HWND hDlg)
{
	int z3 = workprefs.address_space_24 == false;
	bool ac = fastram_select_ramboard && fastram_select_ramboard->autoconfig_inuse;
	bool manual = fastram_select_ramboard && fastram_select_ramboard->manual_config;
	bool size = fastram_select_ramboard && fastram_select_ramboard->size != 0;

#ifndef AUTOCONFIG
	z3 = FALSE;
	fast = FALSE;
#endif
	ew (hDlg, IDC_Z3TEXT, z3);
	ew (hDlg, IDC_Z3FASTRAM, z3);
	ew (hDlg, IDC_Z3FASTMEM, z3);
	ew (hDlg, IDC_Z3CHIPRAM, z3);
	ew (hDlg, IDC_Z3CHIPMEM, z3);
	ew (hDlg, IDC_FASTMEM, true);
	ew (hDlg, IDC_FASTRAM, true);
	ew (hDlg, IDC_Z3MAPPING, z3);
	ew (hDlg, IDC_FASTTEXT, true);

	bool isfast = fastram_select >= MAX_STANDARD_RAM_BOARDS && fastram_select < MAX_STANDARD_RAM_BOARDS + 2 * MAX_RAM_BOARDS && fastram_select_ramboard && fastram_select_ramboard->size;
	ew(hDlg, IDC_AUTOCONFIG_MANUFACTURER, isfast && !manual);
	ew(hDlg, IDC_AUTOCONFIG_PRODUCT, isfast && !manual);
	ew(hDlg, IDC_MEMORYBOARDSELECT, isfast);
	ew(hDlg, IDC_AUTOCONFIG_DATA,  ac && size);
	ew(hDlg, IDC_FASTMEMAUTOCONFIGUSE, isfast);
	ew(hDlg, IDC_FASTMEMNOAUTOCONFIG, isfast);
	ew(hDlg, IDC_FASTMEMDMA, true);
	ew(hDlg, IDC_FASTMEMFORCE16, true);
	ew(hDlg, IDC_FASTMEMSLOW, fastram_select > 0);
	ew(hDlg, IDC_MEMORYRAM, true);
	ew(hDlg, IDC_MEMORYMEM, true);
	ew(hDlg, IDC_RAM_ADDRESS, manual && size);
	ew(hDlg, IDC_RAM_ADDRESS2, manual && size);
}

static void setfastram_ramboard(HWND hDlg, int zram)
{
	if (!fastram_select_ramboard)
		return;
	int idx = 1;
	for (int i = 0; memoryboards[i].name; i++) {
		const struct memoryboardtype *mbt = &memoryboards[i];
		if (mbt->z == zram) {
			if ((mbt->manufacturer == fastram_select_ramboard->manufacturer && mbt->product == fastram_select_ramboard->product) || (mbt->address && mbt->address == fastram_select_ramboard->start_address)) {
				xSendDlgItemMessage(hDlg, IDC_MEMORYBOARDSELECT, CB_SETCURSEL, idx, 0);
				break;
			}
			idx++;
		}
	}
}

static int getmemsize(uae_u32 size, const int *msi)
{
	int mem_size = 0;
	if (msi == msi_fast) {
		switch (size)
		{
		case 0x00000000: mem_size = 0; break;
		case 0x00010000: mem_size = 1; break;
		case 0x00020000: mem_size = 2; break;
		case 0x00040000: mem_size = 3; break;
		case 0x00080000: mem_size = 4; break;
		case 0x00100000: mem_size = 5; break;
		case 0x00200000: mem_size = 6; break;
		case 0x00400000: mem_size = 7; break;
		case 0x00800000: mem_size = 8; break;
		case 0x01000000: mem_size = 9; break;
		}
	} else if (msi == msi_chip) {
		switch (size)
		{
		case 0x00040000: mem_size = 0; break;
		case 0x00080000: mem_size = 1; break;
		case 0x00100000: mem_size = 2; break;
		case 0x00180000: mem_size = 3; break;
		case 0x00200000: mem_size = 4; break;
		case 0x00400000: mem_size = 5; break;
		case 0x00800000: mem_size = 6; break;
		}
	} else if (msi == msi_bogo) {
		switch (size)
		{
		case 0x00000000: mem_size = 0; break;
		case 0x00080000: mem_size = 1; break;
		case 0x00100000: mem_size = 2; break;
		case 0x00180000: mem_size = 3; break;
		case 0x001C0000: mem_size = 4; break;
		}
	} else if (msi == msi_z3chip) {
		switch (size)
		{
		case 0x00000000: mem_size = 0; break;
		case 0x01000000: mem_size = 1; break;
		case 0x02000000: mem_size = 2; break;
		case 0x04000000: mem_size = 3; break;
		case 0x08000000: mem_size = 4; break;
		case 0x10000000: mem_size = 5; break;
		case 0x18000000: mem_size = 6; break;
		case 0x20000000: mem_size = 7; break;
		case 0x30000000: mem_size = 8; break;
		case 0x40000000: mem_size = 9; break;
		}
	} else {
		if (size < 0x00100000)
			mem_size = 0;
		else if (size < 0x00200000)
			mem_size = 1;
		else if (size < 0x00400000)
			mem_size = 2;
		else if (size < 0x00800000)
			mem_size = 3;
		else if (size < 0x01000000)
			mem_size = 4;
		else if (size < 0x02000000)
			mem_size = 5;
		else if (size < 0x04000000)
			mem_size = 6;
		else if (size < 0x08000000)
			mem_size = 7;
		else if (size < 0x10000000)
			mem_size = 8;
		else if (size < 0x20000000)
			mem_size = 9;
		else if (size < 0x40000000)
			mem_size = 10;
		else // 1GB
			mem_size = 11;
	}
	return mem_size;
}

static void addadvancedram(HWND hDlg, struct ramboard *rb, const TCHAR *name, const int *msi)
{
	TCHAR tmp[200];
	_tcscpy(tmp, name);
	if (rb->size) {
		int mem_size = getmemsize(rb->size, msi);
		_tcscat(tmp, _T(" ("));
		_tcscat(tmp, memsize_names[msi[mem_size]]);
		_tcscat(tmp, _T(")"));
	}
	xSendDlgItemMessage(hDlg, IDC_MEMORYSELECT, CB_ADDSTRING, 0, (LPARAM)tmp);
}

static void setfastram_selectmenu(HWND hDlg, int mode)
{
	int min;
	int max;
	const int *msi;
	TCHAR tmp[200];
	int zram = 0;
	struct ramboard *fastram_select_ramboard_old = fastram_select_ramboard;

	fastram_select_ramboard = NULL;
	if (fastram_select == 0) {
		min = 0;
		max = MAX_CHIP_MEM;
		msi = msi_chip;
		fastram_select_pointer = &workprefs.chipmem.size;
		fastram_select_ramboard = &workprefs.chipmem;
	} else if (fastram_select == 1) {
		min = 0;
		max = MAX_SLOW_MEM;
		msi = msi_bogo;
		fastram_select_pointer = &workprefs.bogomem.size;
		fastram_select_ramboard = &workprefs.bogomem;
	} else if (fastram_select < MAX_STANDARD_RAM_BOARDS + MAX_RAM_BOARDS) {
		msi = msi_fast;
		min = MIN_FAST_MEM;
		max = MAX_FAST_MEM;
		fastram_select_ramboard = &workprefs.fastmem[fastram_select - MAX_STANDARD_RAM_BOARDS];
		fastram_select_pointer = &fastram_select_ramboard->size;
		zram = 2;
	} else if (fastram_select >= MAX_STANDARD_RAM_BOARDS + MAX_RAM_BOARDS && fastram_select < MAX_STANDARD_RAM_BOARDS + MAX_RAM_BOARDS * 2) {
		msi = msi_z3fast;
		min = MIN_Z3_MEM;
		max = MAX_Z3_MEM;
		fastram_select_ramboard = &workprefs.z3fastmem[fastram_select - (MAX_RAM_BOARDS + MAX_STANDARD_RAM_BOARDS)];
		fastram_select_pointer = &fastram_select_ramboard->size;
		zram = 3;
	} else if (fastram_select == MAX_STANDARD_RAM_BOARDS + 2 * MAX_RAM_BOARDS) {
		min = 0;
		max = MAX_CB_MEM_128M;
		msi = msi_cpuboard;
		fastram_select_pointer = &workprefs.mbresmem_high.size;
		fastram_select_ramboard = &workprefs.mbresmem_high;
	} else if (fastram_select == MAX_STANDARD_RAM_BOARDS + 2 * MAX_RAM_BOARDS + 1) {
		min = 0;
		max = MAX_CB_MEM_64M;
		msi = msi_cpuboard;
		fastram_select_pointer = &workprefs.mbresmem_low.size;
		fastram_select_ramboard = &workprefs.mbresmem_low;
	} else if (fastram_select == MAX_STANDARD_RAM_BOARDS + 2 * MAX_RAM_BOARDS + 2) {
		min = 0;
		max = MAX_Z3_CHIPMEM;
		msi = msi_z3chip;
		fastram_select_pointer = &workprefs.z3chipmem.size;
		fastram_select_ramboard = &workprefs.z3chipmem;
	} else {
		return;
	}

	fastram_select_msi = msi;
	uae_u32 v = *fastram_select_pointer;
	int mem_size = getmemsize(v, msi);

	xSendDlgItemMessage(hDlg, IDC_MEMORYMEM, TBM_SETRANGE, TRUE, MAKELONG(min, max));
	xSendDlgItemMessage(hDlg, IDC_MEMORYMEM, TBM_SETPOS, TRUE, mem_size);
	SetDlgItemText(hDlg, IDC_MEMORYRAM, memsize_names[msi[mem_size]]);

	expansion_generate_autoconfig_info(&workprefs);
	struct ramboard *rb = fastram_select_ramboard;
	setchecked(hDlg, IDC_FASTMEMAUTOCONFIGUSE, rb && rb->autoconfig_inuse);
	setchecked(hDlg, IDC_FASTMEMNOAUTOCONFIG, rb && rb->manual_config);
	setchecked(hDlg, IDC_FASTMEMDMA, rb && rb->nodma == 0);
	setchecked(hDlg, IDC_FASTMEMFORCE16, rb && rb->force16bit != 0);
	setchecked(hDlg, IDC_FASTMEMSLOW, rb && (rb->chipramtiming != 0 || rb == &workprefs.chipmem));
	if (rb) {
		if (rb->manual_config) {
			if (rb->end_address <= rb->start_address || rb->start_address + rb->size < rb->end_address)
				rb->end_address = rb->start_address + rb->size - 1;
		} else {
			rb->start_address = 0;
			rb->end_address = 0;
		}
		if (fastram_select_ramboard_old != fastram_select_ramboard || mode < 0) {
			if (zram) {
				ew(hDlg, IDC_MEMORYBOARDSELECT, TRUE);
				xSendDlgItemMessage(hDlg, IDC_MEMORYBOARDSELECT, CB_RESETCONTENT, 0, 0);
				xSendDlgItemMessage(hDlg, IDC_MEMORYBOARDSELECT, CB_ADDSTRING, 0, (LPARAM)_T("-"));
				for (int i = 0; memoryboards[i].name; i++) {
					const struct memoryboardtype *mbt = &memoryboards[i];
					if (mbt->z == zram) {
						_stprintf(tmp, _T("%s %s"), memoryboards[i].man, memoryboards[i].name);
						xSendDlgItemMessage(hDlg, IDC_MEMORYBOARDSELECT, CB_ADDSTRING, 0, (LPARAM)tmp);
					}
				}
			} else {
				xSendDlgItemMessage(hDlg, IDC_MEMORYBOARDSELECT, CB_RESETCONTENT, 0, 0);
				ew(hDlg, IDC_MEMORYBOARDSELECT, FALSE);
			}
		}
		struct autoconfig_info* aci = NULL;
		if (fastram_select >= MAX_STANDARD_RAM_BOARDS && fastram_select < MAX_STANDARD_RAM_BOARDS + 2 * MAX_RAM_BOARDS) {
			aci = expansion_get_autoconfig_info(&workprefs, fastram_select < MAX_STANDARD_RAM_BOARDS + MAX_RAM_BOARDS ? ROMTYPE_RAMZ2 : ROMTYPE_RAMZ3, (fastram_select - MAX_STANDARD_RAM_BOARDS) % MAX_RAM_BOARDS);
			if (!rb->autoconfig_inuse) {
				if (aci) {
					memcpy(rb->autoconfig, aci->autoconfig_bytes, sizeof rb->autoconfig);
				}
				if (rb->manufacturer) {
					rb->autoconfig[1] = rb->product;
					rb->autoconfig[4] = (rb->manufacturer >> 8) & 0xff;
					rb->autoconfig[5] = rb->manufacturer & 0xff;
				} else {
					memset(rb->autoconfig, 0, sizeof rb->autoconfig);
					aci = expansion_get_autoconfig_info(&workprefs, fastram_select < MAX_STANDARD_RAM_BOARDS + MAX_RAM_BOARDS ? ROMTYPE_RAMZ2 : ROMTYPE_RAMZ3, (fastram_select - MAX_STANDARD_RAM_BOARDS) % MAX_RAM_BOARDS);
					if (aci) {
						memcpy(rb->autoconfig, aci->autoconfig_bytes, sizeof rb->autoconfig);
					}
				}
			}
		}
		if (mode != 3) {
			if (aci && !rb->manual_config) {
				_stprintf(tmp, _T("%08x"), aci->start);
				SetDlgItemText(hDlg, IDC_RAM_ADDRESS, tmp);
				_stprintf(tmp, _T("%08x"), aci->start + aci->size - 1);
				SetDlgItemText(hDlg, IDC_RAM_ADDRESS2, tmp);
			} else if (rb->manual_config) {
				_stprintf(tmp, _T("%08x"), rb->start_address);
				SetDlgItemText(hDlg, IDC_RAM_ADDRESS, tmp);
				_stprintf(tmp, _T("%08x"), rb->end_address);
				SetDlgItemText(hDlg, IDC_RAM_ADDRESS2, tmp);
			} else {
				SetDlgItemText(hDlg, IDC_RAM_ADDRESS, _T(""));
				SetDlgItemText(hDlg, IDC_RAM_ADDRESS2, _T(""));
			}
		}

		if (mode == 1 && rb->autoconfig_inuse) {
			rb->autoconfig[1] = rb->product;
			rb->autoconfig[4] = (rb->manufacturer >> 8) & 0xff;
			rb->autoconfig[5] = rb->manufacturer & 0xff;
		}
		if (mode != 2) {
			tmp[0] = 0;
			TCHAR *p = tmp;
			for (int i = 0; i < 12; i++) {
				if (i > 0)
					_tcscat(p, _T("."));
				_stprintf(p + _tcslen(p), _T("%02x"), rb->autoconfig[i]);
			}
			SetDlgItemText(hDlg, IDC_AUTOCONFIG_DATA, tmp);
		}
		if (rb->autoconfig_inuse) {
			rb->product = rb->autoconfig[1];
			rb->manufacturer = (rb->autoconfig[4] << 8) | rb->autoconfig[5];
		}
		if (mode != 1) {
			if (rb->manufacturer) {
				_stprintf(tmp, _T("%d"), rb->manufacturer);
				SetDlgItemText(hDlg, IDC_AUTOCONFIG_MANUFACTURER, tmp);
				_stprintf(tmp, _T("%d"), rb->product);
				SetDlgItemText(hDlg, IDC_AUTOCONFIG_PRODUCT, tmp);
			} else {
				rb->product = 0;
				SetDlgItemText(hDlg, IDC_AUTOCONFIG_MANUFACTURER, _T(""));
				SetDlgItemText(hDlg, IDC_AUTOCONFIG_PRODUCT, _T(""));
			}
		}
		setfastram_ramboard(hDlg, zram);

	} else {
		SetDlgItemText(hDlg, IDC_AUTOCONFIG_MANUFACTURER, _T(""));
		SetDlgItemText(hDlg, IDC_AUTOCONFIG_PRODUCT, _T(""));
	}

	xSendDlgItemMessage(hDlg, IDC_MEMORYSELECT, CB_RESETCONTENT, 0, 0);
	addadvancedram(hDlg, &workprefs.chipmem, _T("Chip RAM"), msi_chip);
	addadvancedram(hDlg, &workprefs.bogomem, _T("Slow RAM"), msi_bogo);
	for (int i = 0; i < MAX_RAM_BOARDS; i++) {
		struct autoconfig_info *aci = expansion_get_autoconfig_info(&workprefs, ROMTYPE_RAMZ2, i);
		_stprintf(tmp, _T("Z2 Fast Ram #%d"), i + 1);
		if (workprefs.fastmem[i].size)
			_stprintf(tmp + _tcslen(tmp), _T(" [%dk]"), workprefs.fastmem[i].size / 1024);
		if (aci && aci->cst) {
			_stprintf(tmp + _tcslen(tmp), _T(" (%s)"), aci->cst->name);
		} else if (aci && aci->ert) {
			_stprintf(tmp + _tcslen(tmp), _T(" (%s)"), aci->ert->friendlyname);
		}
		xSendDlgItemMessage(hDlg, IDC_MEMORYSELECT, CB_ADDSTRING, 0, (LPARAM)tmp);
	}
	if (!workprefs.address_space_24) {
		for (int i = 0; i < MAX_RAM_BOARDS; i++) {
			struct autoconfig_info *aci = expansion_get_autoconfig_info(&workprefs, ROMTYPE_RAMZ3, i);
			_stprintf(tmp, _T("Z3 Fast Ram #%d"), i + 1);
			if (workprefs.z3fastmem[i].size)
				_stprintf(tmp + _tcslen(tmp), _T(" [%dM]"), workprefs.z3fastmem[i].size / (1024 * 1024));
			if (aci && aci->cst) {
				_stprintf(tmp + _tcslen(tmp), _T(" (%s)"), aci->cst->name);
			} else if (aci && aci->ert) {
				_stprintf(tmp + _tcslen(tmp), _T(" (%s)"), aci->ert->friendlyname);
			}
			xSendDlgItemMessage(hDlg, IDC_MEMORYSELECT, CB_ADDSTRING, 0, (LPARAM)tmp);
		}
		addadvancedram(hDlg, &workprefs.mbresmem_high, _T("Processor Slot Fast RAM"), msi_mb);
		addadvancedram(hDlg, &workprefs.mbresmem_low, _T("Motherboard Fast RAM"), msi_mb);
		addadvancedram(hDlg, &workprefs.z3chipmem, _T("32-bit Chip RAM"), msi_z3chip);
	} else {
		if (fastram_select >= MAX_STANDARD_RAM_BOARDS + MAX_RAM_BOARDS)
			fastram_select = 0;
	}
	xSendDlgItemMessage(hDlg, IDC_MEMORYSELECT, CB_SETCURSEL, fastram_select, 0);

	enable_for_memorydlg(hDlg);
}

extern uae_u32 natmem_reserved_size;
static void setmax32bitram (HWND hDlg)
{
	TCHAR tmp[256], tmp2[256];
	uae_u32 size32 = 0, z3size_uae = 0, z3size_real = 0;

	z3size_uae = natmem_reserved_size >= expamem_z3_pointer_uae ? natmem_reserved_size - expamem_z3_pointer_uae : 0;
	z3size_real = natmem_reserved_size >= expamem_z3_pointer_real ? natmem_reserved_size - expamem_z3_pointer_real : 0;

	uae_u32 first = 0;
	int idx = 0;
	for (;;) {
		struct autoconfig_info *aci = expansion_get_autoconfig_data(&workprefs, idx);
		if (!aci)
			break;
		if (aci->start >= 0x10000000 && !first)
			first = aci->start;
		if (aci->start >= 0x10000000 && aci->start + aci->size > size32)
			size32 = aci->start + aci->size;
		idx++;
	}
	if (size32 >= first)
		size32 -= first;

	WIN32GUI_LoadUIString(IDS_MEMINFO, tmp2, sizeof(tmp2) / sizeof(TCHAR));
	_stprintf (tmp, tmp2, size32 / (1024 * 1024), (natmem_reserved_size - 256 * 1024 * 1024) / (1024 * 1024), z3size_uae / (1024 * 1024), z3size_real / (1024 * 1024));
	SetDlgItemText (hDlg, IDC_MAX32RAM, tmp);
}

static void setcpuboardmemsize(HWND hDlg)
{
	if (workprefs.cpuboardmem1.size > cpuboard_maxmemory(&workprefs))
		workprefs.cpuboardmem1.size = cpuboard_maxmemory(&workprefs);

	if (cpuboard_memorytype(&workprefs) == BOARD_MEMORY_Z2) {
		workprefs.fastmem[0].size = workprefs.cpuboardmem1.size;
	}
	if (cpuboard_memorytype(&workprefs) == BOARD_MEMORY_25BITMEM) {
		workprefs.mem25bit.size = workprefs.cpuboardmem1.size;
	}
	if (workprefs.cpuboard_type == 0) {
		workprefs.mem25bit.size = 0;
	}

	if (cpuboard_memorytype(&workprefs) == BOARD_MEMORY_HIGHMEM)
		workprefs.mbresmem_high.size = workprefs.cpuboardmem1.size;

	int maxmem = cpuboard_maxmemory(&workprefs);
	if (workprefs.cpuboardmem1.size > maxmem) {
		workprefs.cpuboardmem1.size = maxmem;
	}
	if (maxmem <= 8 * 1024 * 1024)
		xSendDlgItemMessage (hDlg, IDC_CPUBOARDMEM, TBM_SETRANGE, TRUE, MAKELONG (MIN_CB_MEM, MAX_CB_MEM_Z2));
	else if (maxmem <= 16 * 1024 * 1024)
		xSendDlgItemMessage(hDlg, IDC_CPUBOARDMEM, TBM_SETRANGE, TRUE, MAKELONG(MIN_CB_MEM, MAX_CB_MEM_16M));
	else if (maxmem <= 32 * 1024 * 1024)
		xSendDlgItemMessage(hDlg, IDC_CPUBOARDMEM, TBM_SETRANGE, TRUE, MAKELONG(MIN_CB_MEM, MAX_CB_MEM_32M));
	else if (maxmem <= 64 * 1024 * 1024)
		xSendDlgItemMessage(hDlg, IDC_CPUBOARDMEM, TBM_SETRANGE, TRUE, MAKELONG(MIN_CB_MEM, MAX_CB_MEM_64M));
	else if (maxmem <= 128 * 1024 * 1024)
		xSendDlgItemMessage (hDlg, IDC_CPUBOARDMEM, TBM_SETRANGE, TRUE, MAKELONG (MIN_CB_MEM, MAX_CB_MEM_128M));
	else
		xSendDlgItemMessage (hDlg, IDC_CPUBOARDMEM, TBM_SETRANGE, TRUE, MAKELONG (MIN_CB_MEM, MAX_CB_MEM_256M));

	int mem_size = 0;
	switch (workprefs.cpuboardmem1.size) {
	case 0x00000000: mem_size = 0; break;
	case 0x00100000: mem_size = 1; break;
	case 0x00200000: mem_size = 2; break;
	case 0x00400000: mem_size = 3; break;
	case 0x00800000: mem_size = 4; break;
	case 0x01000000: mem_size = 5; break;
	case 0x02000000: mem_size = 6; break;
	case 0x04000000: mem_size = 7; break;
	case 0x08000000: mem_size = 8; break;
	case 0x10000000: mem_size = 9; break;
	}
	xSendDlgItemMessage (hDlg, IDC_CPUBOARDMEM, TBM_SETPOS, TRUE, mem_size);
	SetDlgItemText (hDlg, IDC_CPUBOARDRAM, memsize_names[msi_cpuboard[mem_size]]);
	xSendDlgItemMessage (hDlg, IDC_CPUBOARD_TYPE, CB_SETCURSEL, workprefs.cpuboard_type, 0);
	xSendDlgItemMessage (hDlg, IDC_CPUBOARD_SUBTYPE, CB_SETCURSEL, workprefs.cpuboard_subtype, 0);

//	for (int i = 0; cpuboard_settings_id[i] >= 0; i++) {
//		setchecked(hDlg, cpuboard_settings_id[i], (workprefs.cpuboard_settings & (1 << i)) != 0);
//	}
}

static int manybits (int v, int mask)
{
	int i, cnt;

	cnt = 0;
	for (i = 0; i < 32; i++) {
		if (((1 << i) & mask) & v)
			cnt++;
	}
	if (cnt > 1)
		return 1;
	return 0;
}

static void values_to_memorydlg (HWND hDlg)
{
	uae_u32 mem_size = 0;
	uae_u32 v;

	switch (workprefs.chipmem.size) {
	case 0x00040000: mem_size = 0; break;
	case 0x00080000: mem_size = 1; break;
	case 0x00100000: mem_size = 2; break;
	case 0x00180000: mem_size = 3; break;
	case 0x00200000: mem_size = 4; break;
	case 0x00400000: mem_size = 5; break;
	case 0x00800000: mem_size = 6; break;
	}
	xSendDlgItemMessage (hDlg, IDC_CHIPMEM, TBM_SETPOS, TRUE, mem_size);
	SetDlgItemText (hDlg, IDC_CHIPRAM, memsize_names[msi_chip[mem_size]]);


	mem_size = 0;
	switch (workprefs.fastmem[0].size) {
	case 0x00000000: mem_size = 0; break;
	case 0x00010000: mem_size = 1; break;
	case 0x00020000: mem_size = 2; break;
	case 0x00040000: mem_size = 3; break;
	case 0x00080000: mem_size = 4; break;
	case 0x00100000: mem_size = 5; break;
	case 0x00200000: mem_size = 6; break;
	case 0x00400000: mem_size = 7; break;
	case 0x00800000: mem_size = 8; break;
	case 0x01000000: mem_size = 9; break;
	}
	xSendDlgItemMessage (hDlg, IDC_FASTMEM, TBM_SETPOS, TRUE, mem_size);
	SetDlgItemText (hDlg, IDC_FASTRAM, memsize_names[msi_fast[mem_size]]);

	mem_size = 0;
	switch (workprefs.bogomem.size) {
	case 0x00000000: mem_size = 0; break;
	case 0x00080000: mem_size = 1; break;
	case 0x00100000: mem_size = 2; break;
	case 0x00180000: mem_size = 3; break;
	case 0x001C0000: mem_size = 4; break;
	}
	xSendDlgItemMessage (hDlg, IDC_SLOWMEM, TBM_SETPOS, TRUE, mem_size);
	SetDlgItemText (hDlg, IDC_SLOWRAM, memsize_names[msi_bogo[mem_size]]);

	mem_size = 0;
	v = workprefs.z3fastmem[0].size;
	if (v < 0x00100000)
		mem_size = 0;
	else if (v < 0x00200000)
		mem_size = 1;
	else if (v < 0x00400000)
		mem_size = 2;
	else if (v < 0x00800000)
		mem_size = 3;
	else if (v < 0x01000000)
		mem_size = 4;
	else if (v < 0x02000000)
		mem_size = 5;
	else if (v < 0x04000000)
		mem_size = 6;
	else if (v < 0x08000000)
		mem_size = 7;
	else if (v < 0x10000000)
		mem_size = 8;
	else if (v < 0x20000000)
		mem_size = 9;
	else if (v < 0x40000000)
		mem_size = 10;
	else // 1GB
		mem_size = 11;
	xSendDlgItemMessage (hDlg, IDC_Z3FASTMEM, TBM_SETPOS, TRUE, mem_size);
	SetDlgItemText (hDlg, IDC_Z3FASTRAM, memsize_names[msi_z3fast[mem_size]]);

	mem_size = 0;
	v = workprefs.z3chipmem.size;
	if (v < 0x01000000)
		mem_size = 0;
	else if (v < 0x02000000)
		mem_size = 1;
	else if (v < 0x04000000)
		mem_size = 2;
	else if (v < 0x08000000)
		mem_size = 3;
	else if (v < 0x10000000)
		mem_size = 4;
	else if (v < 0x18000000)
		mem_size = 5;
	else if (v < 0x20000000)
		mem_size = 6;
	else if (v < 0x30000000)
		mem_size = 7;
	else if (v < 0x40000000)
		mem_size = 8;
	else
		mem_size = 9;

	xSendDlgItemMessage (hDlg, IDC_Z3CHIPMEM, TBM_SETPOS, TRUE, mem_size);
	SetDlgItemText (hDlg, IDC_Z3CHIPRAM, memsize_names[msi_z3chip[mem_size]]);
#if 0
	mem_size = 0;
	switch (workprefs.mbresmem_low_size) {
	case 0x00000000: mem_size = 0; break;
	case 0x00100000: mem_size = 1; break;
	case 0x00200000: mem_size = 2; break;
	case 0x00400000: mem_size = 3; break;
	case 0x00800000: mem_size = 4; break;
	case 0x01000000: mem_size = 5; break;
	case 0x02000000: mem_size = 6; break;
	case 0x04000000: mem_size = 7; break;
	}
	xSendDlgItemMessage (hDlg, IDC_MBMEM1, TBM_SETPOS, TRUE, mem_size);
	SetDlgItemText (hDlg, IDC_MBRAM1, memsize_names[msi_gfx[mem_size]]);

	mem_size = 0;
	switch (workprefs.mbresmem_high_size) {
	case 0x00000000: mem_size = 0; break;
	case 0x00100000: mem_size = 1; break;
	case 0x00200000: mem_size = 2; break;
	case 0x00400000: mem_size = 3; break;
	case 0x00800000: mem_size = 4; break;
	case 0x01000000: mem_size = 5; break;
	case 0x02000000: mem_size = 6; break;
	case 0x04000000: mem_size = 7; break;
	case 0x08000000: mem_size = 8; break;
	}
	xSendDlgItemMessage (hDlg, IDC_MBMEM2, TBM_SETPOS, TRUE, mem_size);
	SetDlgItemText (hDlg, IDC_MBRAM2, memsize_names[msi_gfx[mem_size]]);
#endif
	setmax32bitram (hDlg);

}

static void fix_values_memorydlg (void)
{
	if (workprefs.chipmem.size <= 0x200000)
		return;
	int total = 0;
	for (int i = 0; i < MAX_RAM_BOARDS; i++) {
		if (workprefs.fastmem[i].size > 262144)
			workprefs.fastmem[i].size = 262144;
		total += workprefs.fastmem[i].size;
		if (total > 262144) {
			workprefs.fastmem[i].size = 0;
		}
	}
}

struct romdataentry
{
	TCHAR *name;
	int priority;
};

static void addromfiles(UAEREG *fkey, HWND hDlg, DWORD d, const TCHAR *path, int type1, int type2)
{
	int idx;
	TCHAR tmp[MAX_DPATH];
	TCHAR tmp2[MAX_DPATH];
	TCHAR seltmp[MAX_DPATH];
	struct romdata *rdx = NULL;
	struct romdataentry *rde = xcalloc(struct romdataentry, MAX_ROMMGR_ROMS);
	int ridx = 0;
	
	if (path)
		rdx = scan_single_rom(path);
	idx = 0;
	seltmp[0] = 0;
	for (; fkey;) {
		int size = sizeof(tmp) / sizeof(TCHAR);
		int size2 = sizeof(tmp2) / sizeof(TCHAR);
		if (!regenumstr(fkey, idx, tmp, &size, tmp2, &size2))
			break;
		if (_tcslen(tmp) == 7 || _tcslen(tmp) == 13) {
			int group = 0;
			int subitem = 0;
			int idx2 = _tstol(tmp + 4);
			if (_tcslen(tmp) == 13) {
				group = _tstol(tmp + 8);
				subitem = _tstol(tmp + 11);
			}
			if (idx2 >= 0) {
				struct romdata *rd = getromdatabyidgroup(idx2, group, subitem);
				for (int i = 0; i < 2; i++) {
					int type = i ? type2 : type1;
					if (type) {
						if (rd && ((((rd->type & ROMTYPE_GROUP_MASK) & (type & ROMTYPE_GROUP_MASK)) && ((rd->type & ROMTYPE_SUB_MASK) == (type & ROMTYPE_SUB_MASK) || !(type & ROMTYPE_SUB_MASK))) ||
								   (rd->type & type) == ROMTYPE_NONE || (rd->type & type) == ROMTYPE_NOT)) {
							getromname(rd, tmp);
							int j;
							for (j = 0; j < ridx; j++) {
								if (!_tcsicmp(rde[j].name, tmp)) {
									break;
								}
							}
							if (j >= ridx) {
								rde[ridx].name = my_strdup(tmp);
								rde[ridx].priority = rd->sortpriority;
								ridx++;
							}
							if (rd == rdx)
								_tcscpy(seltmp, tmp);
							break;
						}
					}
				}
			}
		}
		idx++;
	}

	for (int i = 0; i < ridx; i++) {
		for (int j = i + 1; j < ridx; j++) {
			int ipri = rde[i].priority;
			const TCHAR *iname = rde[i].name;
			int jpri = rde[j].priority;
			const TCHAR *jname = rde[j].name;
			if ((ipri > jpri) || (ipri == jpri && _tcsicmp(iname, jname) > 0)) {
				struct romdataentry rdet;
				memcpy(&rdet, &rde[i], sizeof(struct romdataentry));
				memcpy(&rde[i], &rde[j], sizeof(struct romdataentry));
				memcpy(&rde[j], &rdet, sizeof(struct romdataentry));
			}
		}
	}

	xSendDlgItemMessage(hDlg, d, CB_RESETCONTENT, 0, 0);
	xSendDlgItemMessage(hDlg, d, CB_ADDSTRING, 0, (LPARAM)_T(""));
	for (int i = 0; i < ridx; i++) {
		struct romdataentry *rdep = &rde[i];
		xSendDlgItemMessage(hDlg, d, CB_ADDSTRING, 0, (LPARAM)rdep->name);
		xfree(rdep->name);
	}
	if (seltmp[0])
		xSendDlgItemMessage(hDlg, d, CB_SELECTSTRING, (WPARAM) -1, (LPARAM) seltmp);
	else
		SetDlgItemText(hDlg, d, path);

	xfree(rde);
}

static void getromfile(HWND hDlg, DWORD d, TCHAR *path, int size)
{
	LRESULT val = xSendDlgItemMessage(hDlg, d, CB_GETCURSEL, 0, 0L);
	if (val == CB_ERR) {
		xSendDlgItemMessage(hDlg, d, WM_GETTEXT, (WPARAM) size, (LPARAM) path);
	}
	else {
		TCHAR tmp1[MAX_DPATH];
		struct romdata *rd;
		xSendDlgItemMessage(hDlg, d, CB_GETLBTEXT, (WPARAM) val, (LPARAM) tmp1);
		path[0] = 0;
		rd = getromdatabyname(tmp1);
		if (rd) {
			struct romlist *rl = getromlistbyromdata(rd);
			if (rd->configname)
				_stprintf(path, _T(":%s"), rd->configname);
			else if (rl)
				_tcsncpy(path, rl->path, size);
		}
	}
}

struct expansionrom_gui
{
	const struct expansionboardsettings *expansionrom_gui_ebs;
	int expansionrom_gui_item;
	DWORD expansionrom_gui_itemselector;
	DWORD expansionrom_gui_selector;
	DWORD expansionrom_gui_checkbox;
	DWORD expansionrom_gui_stringbox;
	int expansionrom_gui_settingsbits;
	int expansionrom_gui_settingsshift;
	int expansionrom_gui_settings;
	TCHAR expansionrom_gui_string[ROMCONFIG_CONFIGTEXT_LEN];
};
static struct expansionrom_gui expansion_gui_item;
static struct expansionrom_gui accelerator_gui_item;

static void reset_expansionrom_gui(HWND hDlg, struct expansionrom_gui *eg, DWORD itemselector, DWORD selector, DWORD checkbox, DWORD stringbox)
{
	eg->expansionrom_gui_settings = NULL;
	eg->expansionrom_gui_ebs = NULL;
	eg->expansionrom_gui_string[0] = 0;
	hide(hDlg, itemselector, 1);
	hide(hDlg, selector, 1);
	hide(hDlg, checkbox, 1);
	hide(hDlg, stringbox, 1);
}

static void create_expansionrom_gui(HWND hDlg, struct expansionrom_gui *eg, const struct expansionboardsettings *ebs,
	int settings, const TCHAR *settingsstring,
	DWORD itemselector, DWORD selector, DWORD checkbox, DWORD stringbox)
{
	bool reset = false;
	static int recursive;
	const struct expansionboardsettings *eb;
	if (eg->expansionrom_gui_ebs != ebs) {
		if (eg->expansionrom_gui_ebs)
			eg->expansionrom_gui_item = 0;
		reset = true;
	}
	eg->expansionrom_gui_ebs = ebs;
	eg->expansionrom_gui_itemselector = itemselector;
	eg->expansionrom_gui_selector = selector;
	eg->expansionrom_gui_checkbox = checkbox;
	eg->expansionrom_gui_stringbox = stringbox;
	eg->expansionrom_gui_settings = settings;
	if (settingsstring != eg->expansionrom_gui_string) {
		eg->expansionrom_gui_string[0] = 0;
		if (settingsstring)
			_tcscpy(eg->expansionrom_gui_string, settingsstring);
	}

	if (!ebs) {
		reset_expansionrom_gui(hDlg, eg, itemselector, selector, checkbox, stringbox);
		return;
	}
	if (recursive > 0)
		return;
	recursive++;

retry:
	int item = eg->expansionrom_gui_item;
	hide(hDlg, itemselector, 0);
	int bitcnt = 0;
	for (int i = 0; i < item; i++) {
		const struct expansionboardsettings *eb = &ebs[i];
		if (eb->name == NULL) {
			eg->expansionrom_gui_item = 0;
			goto retry;
		}
		if (eb->type == EXPANSIONBOARD_STRING) {
			;
		} else if (eb->type == EXPANSIONBOARD_MULTI) {
			const TCHAR *p = eb->configname;
			int itemcnt = -1;
			while (p[0]) {
				itemcnt++;
				p += _tcslen(p) + 1;
			}
			int bits = 1;
			for (int i = 0; i < 8; i++) {
				if ((1 << i) >= itemcnt) {
					bits = i;
					break;
				}
			}
			bitcnt += bits;
		} else {
			bitcnt++;
		}
		bitcnt += eb->bitshift;
	}
	if (reset) {
		xSendDlgItemMessage(hDlg, itemselector, CB_RESETCONTENT, 0, 0);
		for (int i = 0; ebs[i].name; i++) {
			const struct expansionboardsettings *eb = &ebs[i];
			xSendDlgItemMessage(hDlg, itemselector, CB_ADDSTRING, 0, (LPARAM)eb->name);
		}
		xSendDlgItemMessage(hDlg, itemselector, CB_SETCURSEL, item, 0);
	}
	eb = &ebs[item];
	bitcnt += eb->bitshift;
	if (eb->type == EXPANSIONBOARD_STRING) {
		hide(hDlg, stringbox, 0);
		hide(hDlg, selector, 1);
		hide(hDlg, checkbox, 1);
		eg->expansionrom_gui_settingsbits = 0;
		SetDlgItemText(hDlg, stringbox, eg->expansionrom_gui_string);
	} else if (eb->type == EXPANSIONBOARD_MULTI) {
		xSendDlgItemMessage(hDlg, selector, CB_RESETCONTENT, 0, 0);
		int itemcnt = -1;
		const TCHAR *p = eb->name;
		while (p[0]) {
			if (itemcnt >= 0) {
				xSendDlgItemMessage(hDlg, selector, CB_ADDSTRING, 0, (LPARAM)p);
			}
			itemcnt++;
			p += _tcslen(p) + 1;
		}
		int bits = 1;
		for (int i = 0; i < 8; i++) {
			if ((1 << i) >= itemcnt) {
				bits = i;
				break;
			}
		}
		int value = settings;
		if (eb->invert)
			value ^= 0x7fffffff;
		value >>= bitcnt;
		value &= (1 << bits) - 1;
		xSendDlgItemMessage(hDlg, selector, CB_SETCURSEL, value, 0);
		hide(hDlg, stringbox, 1);
		hide(hDlg, selector, 0);
		hide(hDlg, checkbox, 1);
		eg->expansionrom_gui_settingsbits = bits;
	} else {
		hide(hDlg, stringbox, 1);
		hide(hDlg, selector, 1);
		hide(hDlg, checkbox, 0);
		setchecked(hDlg, checkbox, ((settings >> bitcnt) ^ (eb->invert ? 1 : 0)) & 1);
		eg->expansionrom_gui_settingsbits = 1;
	}
	eg->expansionrom_gui_settingsshift = bitcnt;
	recursive--;
}

static void get_expansionrom_gui(HWND hDlg, struct expansionrom_gui *eg)
{
	if (!eg->expansionrom_gui_ebs)
		return;

	int val;
	int settings = eg->expansionrom_gui_settings;

	val = xSendDlgItemMessage(hDlg, eg->expansionrom_gui_itemselector, CB_GETCURSEL, 0, 0);
	if (val != CB_ERR && val != eg->expansionrom_gui_item) {
		eg->expansionrom_gui_item = val;
		create_expansionrom_gui(hDlg, eg, eg->expansionrom_gui_ebs, eg->expansionrom_gui_settings, eg->expansionrom_gui_string,
			eg->expansionrom_gui_itemselector, eg->expansionrom_gui_selector, eg->expansionrom_gui_checkbox, eg->expansionrom_gui_stringbox);
		return;
	}
	const struct expansionboardsettings *eb = &eg->expansionrom_gui_ebs[eg->expansionrom_gui_item];
	if (eb->type == EXPANSIONBOARD_STRING) {
		GetDlgItemText(hDlg, eg->expansionrom_gui_stringbox, eg->expansionrom_gui_string, sizeof(eg->expansionrom_gui_string) / sizeof(TCHAR));
	} else if (eb->type == EXPANSIONBOARD_MULTI) {
		val = xSendDlgItemMessage(hDlg, eg->expansionrom_gui_selector, CB_GETCURSEL, 0, 0);
		if (val != CB_ERR) {
			int mask = (1 << eg->expansionrom_gui_settingsbits) - 1;
			settings &= ~(mask << eg->expansionrom_gui_settingsshift);
			settings |= val << eg->expansionrom_gui_settingsshift;
			if (eb->invert)
				settings ^= mask << eg->expansionrom_gui_settingsshift;
		}
	} else {
		settings &= ~(1 << eg->expansionrom_gui_settingsshift);
		if (ischecked(hDlg, eg->expansionrom_gui_checkbox)) {
			settings |= 1 << eg->expansionrom_gui_settingsshift;
		}
		if (eb->invert)
			settings ^= 1 << eg->expansionrom_gui_settingsshift;
	}
	eg->expansionrom_gui_settings = settings;
}


static struct netdriverdata *ndd[MAX_TOTAL_NET_DEVICES + 1];
static int net_enumerated;

struct netdriverdata **target_ethernet_enumerate(void)
{
	if (net_enumerated)
		return ndd;
	ethernet_enumerate(ndd, 0);
	net_enumerated = 1;
	return ndd;
}

static const int scsiromselectedmask[] = {
	EXPANSIONTYPE_INTERNAL, EXPANSIONTYPE_SCSI, EXPANSIONTYPE_IDE, EXPANSIONTYPE_SASI, EXPANSIONTYPE_CUSTOM,
	EXPANSIONTYPE_PCI_BRIDGE, EXPANSIONTYPE_X86_BRIDGE, EXPANSIONTYPE_RTG,
	EXPANSIONTYPE_SOUND, EXPANSIONTYPE_NET, EXPANSIONTYPE_FLOPPY, EXPANSIONTYPE_X86_EXPANSION
};
static void init_expansion_scsi_id(HWND hDlg)
{
	int index;
	struct boardromconfig *brc = get_device_rom(&workprefs, expansionroms[scsiromselected].romtype, scsiromselectednum, &index);
	const struct expansionromtype *ert = &expansionroms[scsiromselected];
	if (brc && ert && ert->id_jumper) {
		if (SendDlgItemMessage(hDlg, IDC_SCSIROMID, CB_GETCOUNT, 0, 0) < 8) {
			xSendDlgItemMessage(hDlg, IDC_SCSIROMID, CB_RESETCONTENT, 0, 0);
			for (int i = 0; i < 8; i++) {
				TCHAR tmp[10];
				_stprintf(tmp, _T("%d"), i);
				xSendDlgItemMessage(hDlg, IDC_SCSIROMID, CB_ADDSTRING, 0, (LPARAM)tmp);
			}
		}
		xSendDlgItemMessage(hDlg, IDC_SCSIROMID, CB_SETCURSEL, brc->roms[index].device_id, 0);
		ew(hDlg, IDC_SCSIROMID, 1);
	} else {
		if (SendDlgItemMessage(hDlg, IDC_SCSIROMID, CB_GETCOUNT, 0, 0) != 1) {
			xSendDlgItemMessage(hDlg, IDC_SCSIROMID, CB_RESETCONTENT, 0, 0);
			xSendDlgItemMessage(hDlg, IDC_SCSIROMID, CB_ADDSTRING, 0, (LPARAM)_T("-"));
		}
		xSendDlgItemMessage(hDlg, IDC_SCSIROMID, CB_SETCURSEL, 0, 0);
		ew(hDlg, IDC_SCSIROMID, 0);
	}
}
static void init_expansion2(HWND hDlg, bool init)
{
	static int first = -1;
	bool last = false;

	for (;;) {
		bool matched = false;
		int *idtab;
		int total = 0;
		xSendDlgItemMessage(hDlg, IDC_SCSIROMSELECT, CB_RESETCONTENT, 0, 0);
		scsiromselect_table[0] = -1;
		for (int i = 0; expansionroms[i].name; i++) {
			total++;
		}
		idtab = xcalloc(int, total * 2);
		int idcnt = 0;
		for (int i = 0; expansionroms[i].name; i++) {
			if (expansionroms[i].romtype & ROMTYPE_CPUBOARD)
				continue;
			if (!(expansionroms[i].deviceflags & scsiromselectedmask[scsiromselectedcatnum]))
				continue;
			if (scsiromselectedcatnum == 0 && (expansionroms[i].deviceflags & (EXPANSIONTYPE_SASI | EXPANSIONTYPE_CUSTOM)))
				continue;
			if ((expansionroms[i].deviceflags & EXPANSIONTYPE_X86_EXPANSION) && scsiromselectedmask[scsiromselectedcatnum] != EXPANSIONTYPE_X86_EXPANSION)
				continue;
			int cnt = 0;
			for (int j = 0; j < MAX_DUPLICATE_EXPANSION_BOARDS; j++) {
				if (is_board_enabled(&workprefs, expansionroms[i].romtype, j)) {
					cnt++;
				}
			}
			if (i == scsiromselected)
				matched = true;
			if (cnt > 0) {
				if (first < 0)
					first = i;
			}
			idtab[idcnt++] = i;
			idtab[idcnt++] = cnt;
		}
		for (int j = 0; j < idcnt; j += 2) {
			TCHAR *nameval = NULL;
			TCHAR *cnameval = NULL;
			int ididx = -1;
			for (int i = 0; i < idcnt; i += 2) {
				TCHAR name[256], cname[256];
				int id = idtab[i];
				int cnt = idtab[i + 1];
				if (id < 0)
					continue;
				name[0] = 0;
				cname[0] = 0;
				if (cnt == 1)
					_tcscat(name, _T("* "));
				else if (cnt > 1)
					_stprintf(name + _tcslen(name), _T("[%d] "), cnt);
				_tcscat(name, expansionroms[id].friendlyname);
				_tcscat(cname, expansionroms[id].friendlyname);
				if (expansionroms[id].friendlymanufacturer) {
					_tcscat(name, _T(" ("));
					_tcscat(name, expansionroms[id].friendlymanufacturer);
					_tcscat(name, _T(")"));
				}
				if (!cnameval || _tcsicmp(cnameval, cname) > 0) {
					xfree(nameval);
					xfree(cnameval);
					nameval = my_strdup(name);
					cnameval = my_strdup(cname);
					ididx = i;
				}
			}
			gui_add_string(scsiromselect_table, hDlg, IDC_SCSIROMSELECT, idtab[ididx], nameval);
			idtab[ididx] = -1;
			xfree(nameval);
			xfree(cnameval);
		}
		xfree(idtab);

		if (scsiromselected > 0 && matched)
			break;
		int found = -1;
		for (int i = 0; expansionroms[i].name; i++) {
			int romtype = expansionroms[i].romtype;
			if (romtype & ROMTYPE_CPUBOARD)
				continue;
			if (!(expansionroms[i].deviceflags & scsiromselectedmask[scsiromselectedcatnum]))
				continue;
			if (scsiromselectedcatnum == 0 && (expansionroms[i].deviceflags & (EXPANSIONTYPE_SASI | EXPANSIONTYPE_CUSTOM)))
				continue;
			if (is_board_enabled(&workprefs, romtype, 0)) {
				if (found == -1)
					found = i;
				else
					found = -2;
			}
		}
		if (scsiromselected < 0 && found < 0)
			found = first;
		if (found > 0) {
			scsiromselected = found;
			break;
		}
		if (last || !init)
			break;
		scsiromselectedcatnum++;
		if (scsiromselectedcatnum > 5) {
			last = true;
			scsiromselectedcatnum = 0;
			scsiromselected = 0;
		}
	}

	if (scsiromselected > 0)
		gui_set_string_cursor(scsiromselect_table, hDlg, IDC_SCSIROMSELECT, scsiromselected);
	xSendDlgItemMessage(hDlg, IDC_SCSIROMSELECTCAT, CB_SETCURSEL, scsiromselectedcatnum, 0);
	init_expansion_scsi_id(hDlg);
}


static void values_to_expansion2dlg_sub(HWND hDlg)
{
	xSendDlgItemMessage(hDlg, IDC_CPUBOARDROMSUBSELECT, CB_RESETCONTENT, 0, 0);
	ew(hDlg, IDC_CPUBOARDROMSUBSELECT, false);

	xSendDlgItemMessage(hDlg, IDC_SCSIROMSUBSELECT, CB_RESETCONTENT, 0, 0);
	const struct expansionromtype *er = &expansionroms[scsiromselected];
	const struct expansionsubromtype *srt = er->subtypes;
	int deviceflags = er->deviceflags;
	ew(hDlg, IDC_SCSIROMSUBSELECT, srt != NULL);
	while (srt && srt->name) {
		xSendDlgItemMessage(hDlg, IDC_SCSIROMSUBSELECT, CB_ADDSTRING, 0, (LPARAM) srt->name);
		srt++;
	}
	int index;
	struct boardromconfig *brc = get_device_rom(&workprefs, expansionroms[scsiromselected].romtype, scsiromselectednum, &index);
	if (brc) {
		if (er->subtypes) {
			xSendDlgItemMessage(hDlg, IDC_SCSIROMSUBSELECT, CB_SETCURSEL, brc->roms[index].subtype, 0);
			deviceflags |= er->subtypes[brc->roms[index].subtype].deviceflags;
		}
	} else if (srt) {
		xSendDlgItemMessage(hDlg, IDC_SCSIROMSUBSELECT, CB_SETCURSEL, 0, 0);
	} else {
		ew(hDlg, IDC_SCSIROMID, FALSE);
	}
	init_expansion_scsi_id(hDlg);

	xSendDlgItemMessage(hDlg, IDC_SCSIROMSELECTNUM, CB_RESETCONTENT, 0, 0);
	if (deviceflags & EXPANSIONTYPE_CLOCKPORT) {
		xSendDlgItemMessage(hDlg, IDC_SCSIROMSELECTNUM, CB_ADDSTRING, 0, (LPARAM)_T("-"));
	}
	for (int i = 0; i < MAX_AVAILABLE_DUPLICATE_EXPANSION_BOARDS; i++) {
		TCHAR tmp[10];
		_stprintf(tmp, _T("%d"), i + 1);
		xSendDlgItemMessage(hDlg, IDC_SCSIROMSELECTNUM, CB_ADDSTRING, 0, (LPARAM)tmp);
	}
	xSendDlgItemMessage(hDlg, IDC_SCSIROMSELECTNUM, CB_SETCURSEL, scsiromselectednum, 0);
	if ((er->zorro < 2 || er->singleonly) && !(deviceflags & EXPANSIONTYPE_CLOCKPORT)) {
		scsiromselectednum = 0;
		xSendDlgItemMessage(hDlg, IDC_SCSIROMSELECTNUM, CB_SETCURSEL, 0, 0);
	}
	ew(hDlg, IDC_SCSIROMSELECTNUM, (er->zorro >= 2 && !er->singleonly) || (deviceflags & EXPANSIONTYPE_CLOCKPORT));
	hide(hDlg, IDC_SCSIROM24BITDMA, (deviceflags & EXPANSIONTYPE_DMA24) == 0);
	ew(hDlg, IDC_SCSIROM24BITDMA, (deviceflags & EXPANSIONTYPE_DMA24) != 0);
}

static void values_from_expansion2dlg(HWND hDlg)
{
	int index;
	struct boardromconfig *brc;
	TCHAR tmp[MAX_DPATH];
	bool changed = false;
	bool isnew = false;

	int checked = ischecked(hDlg, IDC_SCSIROMSELECTED);
	getromfile(hDlg, IDC_SCSIROMFILE, tmp, MAX_DPATH / sizeof(TCHAR));
	if (tmp[0] || checked) {
		const struct expansionromtype *ert = &expansionroms[scsiromselected];
		if (!get_device_rom(&workprefs, expansionroms[scsiromselected].romtype, scsiromselectednum, &index))
			isnew = true;
		brc = get_device_rom_new(&workprefs, expansionroms[scsiromselected].romtype, scsiromselectednum, &index);
		if (checked) {
			if (!brc->roms[index].romfile[0])
				changed = true;
			_tcscpy(brc->roms[index].romfile, _T(":ENABLED"));
		} else {
			changed = _tcscmp(tmp, brc->roms[index].romfile) != 0;
			getromfile(hDlg, IDC_SCSIROMFILE, brc->roms[index].romfile, MAX_DPATH / sizeof(TCHAR));
		}
		brc->roms[index].autoboot_disabled = ischecked(hDlg, IDC_SCSIROMFILEAUTOBOOT);
		brc->roms[index].inserted = ischecked(hDlg, IDC_SCSIROMFILEPCMCIA);
		brc->roms[index].dma24bit = ischecked(hDlg, IDC_SCSIROM24BITDMA);

		int v = xSendDlgItemMessage(hDlg, IDC_SCSIROMID, CB_GETCURSEL, 0, 0L);
		if (v != CB_ERR && !isnew)
			brc->roms[index].device_id = v;

		const struct expansionboardsettings *cbs = ert->settings;
		if (cbs) {
			brc->roms[index].device_settings = expansion_gui_item.expansionrom_gui_settings;
			_tcscpy(brc->roms[index].configtext, expansion_gui_item.expansionrom_gui_string);
		}
#if 0
			for (int i = 0; cbs[i].name; i++) {
				int id = expansion_settings_id[i];
				if (id < 0)
					break;
				brc->roms[index].device_settings &= ~(1 << i);
				if (ischecked(hDlg, id))
					brc->roms[index].device_settings |= 1 << i;
			}
		}
#endif
		v = xSendDlgItemMessage(hDlg, IDC_SCSIROMSUBSELECT, CB_GETCURSEL, 0, 0L);
		if (v != CB_ERR)
			brc->roms[index].subtype = v;

	} else {
		brc = get_device_rom(&workprefs, expansionroms[scsiromselected].romtype, scsiromselectednum, &index);
		if (brc && brc->roms[index].romfile[0])
			changed = true;
		clear_device_rom(&workprefs, expansionroms[scsiromselected].romtype, scsiromselectednum, true);
	}
	if (changed) {
		// singleonly check and removal
		if (expansionroms[scsiromselected].singleonly) {
			if (get_device_rom(&workprefs, expansionroms[scsiromselected].romtype, scsiromselectednum, &index)) {
				for (int i = 0; i < MAX_EXPANSION_BOARDS; i++) {
					if (i != scsiromselectednum) {
						clear_device_rom(&workprefs, expansionroms[scsiromselected].romtype, i, true);
					}
				}
			}
		}
		init_expansion2(hDlg, false);
		values_to_expansion2dlg_sub(hDlg);
	}

	workprefs.cpuboard_settings = accelerator_gui_item.expansionrom_gui_settings;
	getromfile(hDlg, IDC_CPUBOARDROMFILE, tmp, sizeof(brc->roms[index].romfile) / sizeof(TCHAR));
	if (tmp[0]) {
		brc = get_device_rom_new(&workprefs, ROMTYPE_CPUBOARD, 0, &index);
		getromfile(hDlg, IDC_CPUBOARDROMFILE, brc->roms[index].romfile, sizeof(brc->roms[index].romfile) / sizeof(TCHAR));
	} else {
		clear_device_rom(&workprefs, ROMTYPE_CPUBOARD, 0, true);
	}
}

static void values_to_expansion2_expansion_roms(HWND hDlg, UAEREG *fkey)
{
	int index;
	bool keyallocated = false;
	struct boardromconfig *brc;

	if (!fkey) {
		fkey = regcreatetree(NULL, _T("DetectedROMs"));
		keyallocated = true;
	}
	if (scsiromselected) {
		const struct expansionromtype *ert = &expansionroms[scsiromselected];
		int romtype = ert->romtype;
		int romtype_extra = ert->romtype_extra;
		int deviceflags = ert->deviceflags;

		brc = get_device_rom(&workprefs, romtype, scsiromselectednum, &index);
		if (brc && ert->subtypes) {
			const struct expansionsubromtype *esrt = &ert->subtypes[brc->roms[index].subtype];
			if (esrt->romtype) {
				romtype = esrt->romtype;
				romtype_extra = 0;
			}
			deviceflags |= esrt->deviceflags;
		}
		ew(hDlg, IDC_SCSIROMFILE, true);
		ew(hDlg, IDC_SCSIROMCHOOSER, true);
		hide(hDlg, IDC_SCSIROMFILEAUTOBOOT, !ert->autoboot_jumper);
		if (romtype & ROMTYPE_NOT) {
			hide(hDlg, IDC_SCSIROMCHOOSER, 1);
			hide(hDlg, IDC_SCSIROMFILE, 1);
			hide(hDlg, IDC_SCSIROMSELECTED, 0);
			setchecked(hDlg, IDC_SCSIROMSELECTED, brc && brc->roms[index].romfile[0] != 0);
		} else {
			hide(hDlg, IDC_SCSIROMCHOOSER, 0);
			hide(hDlg, IDC_SCSIROMFILE, 0);
			hide(hDlg, IDC_SCSIROMSELECTED, 1);
			setchecked(hDlg, IDC_SCSIROMSELECTED, false);
			addromfiles(fkey, hDlg, IDC_SCSIROMFILE, brc ? brc->roms[index].romfile : NULL, romtype, romtype_extra);
			setchecked(hDlg, IDC_SCSIROMFILEAUTOBOOT, brc && brc->roms[index].autoboot_disabled);
		}
		if (deviceflags & EXPANSIONTYPE_PCMCIA) {
			setchecked(hDlg, IDC_SCSIROMFILEPCMCIA, brc && brc->roms[index].inserted);
			hide(hDlg, IDC_SCSIROMFILEPCMCIA, 0);
		} else {
			hide(hDlg, IDC_SCSIROMFILEPCMCIA, 1);
			if (brc)
				brc->roms[index].inserted = false;
		}
		hide(hDlg, IDC_SCSIROM24BITDMA, (deviceflags & EXPANSIONTYPE_DMA24) == 0);
		ew(hDlg, IDC_SCSIROM24BITDMA, (deviceflags & EXPANSIONTYPE_DMA24) != 0);
		setchecked(hDlg, IDC_SCSIROM24BITDMA, brc && brc->roms[index].dma24bit);
	} else {
		hide(hDlg, IDC_SCSIROMCHOOSER, 0);
		hide(hDlg, IDC_SCSIROMFILE, 0);
		hide(hDlg, IDC_SCSIROMSELECTED, 1);
		hide(hDlg, IDC_SCSIROMFILEPCMCIA, 1);
		hide(hDlg, IDC_SCSIROMFILEAUTOBOOT, 1);
		setchecked(hDlg, IDC_SCSIROMSELECTED, false);
		setchecked(hDlg, IDC_SCSIROMFILEAUTOBOOT, false);
		setchecked(hDlg, IDC_SCSIROMFILEPCMCIA, false);
		xSendDlgItemMessage(hDlg, IDC_SCSIROMFILE, CB_RESETCONTENT, 0, 0);
		ew(hDlg, IDC_SCSIROMFILE, false);
		ew(hDlg, IDC_SCSIROMCHOOSER, false);		
		ew(hDlg, IDC_SCSIROM24BITDMA, 0);
		hide(hDlg, IDC_SCSIROM24BITDMA, 1);
	}
	if (keyallocated)
		regclosetree(fkey);
}

#if 0
static void createautoconfiginfo(struct autoconfig_info *aci, TCHAR *buf)
{
	TCHAR tmp2[200];
	if (aci->autoconfig[0] == 0xff) {
		buf[0] = 0;
		return;
	}
	if (aci->start != 0xffffffff)
		_stprintf(tmp2, _T("0x%08x-0x%08x"), aci->start, aci->start + aci->size - 1);
	else
		_tcscpy(tmp2, _T("--------"));
	_stprintf(buf, _T("%u/%u [0x%04x/0x%02x] %s "),
		(aci->autoconfig[4] << 8) | aci->autoconfig[5], aci->autoconfig[1],
		(aci->autoconfig[4] << 8) | aci->autoconfig[5], aci->autoconfig[1],
		tmp2
	);
	TCHAR *p = buf + _tcslen(buf);
	for (int i = 0; i < 12; i++) {
		if (i > 0)
			_tcscat(p, _T("."));
		_stprintf(p + _tcslen(p), _T("%02x"), aci->autoconfig[i]);
	}
}
#endif

static void values_to_expansion2_expansion_settings(HWND hDlg, int mode)
{
	int index;
	struct boardromconfig *brc;
	if (scsiromselected) {
		const struct expansionromtype *ert = &expansionroms[scsiromselected];
		brc = get_device_rom(&workprefs, expansionroms[scsiromselected].romtype, scsiromselectednum, &index);
		if (brc) {
			if (brc->roms[index].romfile[0])
				ew(hDlg, IDC_SCSIROMFILEAUTOBOOT, ert->autoboot_jumper);
		} else {
			ew(hDlg, IDC_SCSIROMFILEAUTOBOOT, FALSE);
			setchecked(hDlg, IDC_SCSIROMFILEAUTOBOOT, false);
		}
		ew(hDlg, IDC_SCSIROMID, ert->id_jumper);
		const struct expansionboardsettings *cbs = ert->settings;
		create_expansionrom_gui(hDlg, &expansion_gui_item, cbs,
			brc ? brc->roms[index].device_settings : 0,
			brc ? brc->roms[index].configtext : NULL,
			IDC_EXPANSIONBOARDITEMSELECTOR, IDC_EXPANSIONBOARDSELECTOR, IDC_EXPANSIONBOARDCHECKBOX, IDC_EXPANSIONBOARDSTRINGBOX);
	} else {
		reset_expansionrom_gui(hDlg, &expansion_gui_item,
			IDC_EXPANSIONBOARDITEMSELECTOR, IDC_EXPANSIONBOARDSELECTOR, IDC_EXPANSIONBOARDCHECKBOX, IDC_EXPANSIONBOARDSTRINGBOX);
	}


#if 0
	for (int i = 0; expansion_settings_id[i] >= 0; i++) {
		hide(hDlg, expansion_settings_id[i], !(cbs && cbs[i].name));
	}
	int i = 0;
	if (cbs) {
		for (i = 0; cbs[i].name; i++) {
			int id = expansion_settings_id[i];
			if (id < 0)
				break;
			SetWindowText(GetDlgItem(hDlg, id), cbs[i].name);
			setchecked(hDlg, id, (brc && (brc->roms[index].device_settings & (1 << i))));
		}
	}
	while (expansion_settings_id[i] >= 0) {
		int id = expansion_settings_id[i];
		SetWindowText(GetDlgItem(hDlg, id), _T("-"));
		hide(hDlg, id, true);
		i++;
	}
#endif
}


static void enable_for_expansion2dlg (HWND hDlg)
{
	int z3 = true;
	int cw, en;

	en = !!full_property_sheet;
	cw = catweasel_detect ();
	ew(hDlg, IDC_CATWEASEL, cw && en);
	ew(hDlg, IDC_SOCKETS, en);
	ew(hDlg, IDC_SCSIDEVICE, en);
	ew(hDlg, IDC_CATWEASEL, en);
	ew(hDlg, IDC_SANA2, en);

	ew(hDlg, IDC_CPUBOARDROMFILE, workprefs.cpuboard_type != 0);
	ew(hDlg, IDC_CPUBOARDROMCHOOSER, workprefs.cpuboard_type != 0);
	ew(hDlg, IDC_CPUBOARDMEM, workprefs.cpuboard_type > 0);
	ew(hDlg, IDC_CPUBOARDRAM, workprefs.cpuboard_type > 0);
	ew(hDlg, IDC_CPUBOARD_SUBTYPE, workprefs.cpuboard_type);

#if 0
	const struct expansionboardsettings *cbs = cpuboards[workprefs.cpuboard_type].subtypes[workprefs.cpuboard_subtype].settings;
	int i = 0;
	if (cbs) {
		while (cpuboard_settings_id[i] >= 0) {
			if (!cbs[i].name)
				break;
			hide(hDlg, cpuboard_settings_id[i], 0);
			i++;
		}
	}
	while (cpuboard_settings_id[i] >= 0) {
		hide(hDlg, cpuboard_settings_id[i], 1);
		i++;
	}
#endif
}

static void values_to_expansion2dlg (HWND hDlg, int mode)
{
	int cw;
	int index;
	struct boardromconfig *brc;

	CheckDlgButton(hDlg, IDC_SOCKETS, workprefs.socket_emu);
	CheckDlgButton(hDlg, IDC_CATWEASEL, workprefs.catweasel);
	CheckDlgButton(hDlg, IDC_SCSIDEVICE, workprefs.scsi == 1);
	CheckDlgButton(hDlg, IDC_SANA2, workprefs.sana2);
	cw = catweasel_detect ();
	ew (hDlg, IDC_CATWEASEL, cw);
	if (!cw && workprefs.catweasel < 100)
		workprefs.catweasel = 0;

	UAEREG *fkey = regcreatetree(NULL, _T("DetectedROMs"));
	load_keyring(&workprefs, NULL);

	values_to_expansion2_expansion_roms(hDlg, fkey);
	values_to_expansion2_expansion_settings(hDlg, mode);

	if (workprefs.cpuboard_type) {
		const struct cpuboardsubtype *cst = &cpuboards[workprefs.cpuboard_type].subtypes[workprefs.cpuboard_subtype];
		brc = get_device_rom(&workprefs, ROMTYPE_CPUBOARD, 0, &index);
		addromfiles(fkey, hDlg, IDC_CPUBOARDROMFILE, brc ? brc->roms[index].romfile : NULL,
			cst->romtype, cst->romtype_extra);
	} else {
		xSendDlgItemMessage(hDlg, IDC_CPUBOARDROMFILE, CB_RESETCONTENT, 0, 0);
	}

	regclosetree(fkey);

	gui_set_string_cursor(scsiromselect_table, hDlg, IDC_SCSIROMSELECT, scsiromselected);
	values_to_expansion2dlg_sub(hDlg);
}

static void updatecpuboardsubtypes(HWND hDlg)
{
	xSendDlgItemMessage(hDlg, IDC_CPUBOARD_SUBTYPE, CB_RESETCONTENT, 0, 0);
	for (int i = 0; cpuboards[workprefs.cpuboard_type].subtypes[i].name; i++) {
		xSendDlgItemMessage(hDlg, IDC_CPUBOARD_SUBTYPE, CB_ADDSTRING, 0, (LPARAM) cpuboards[workprefs.cpuboard_type].subtypes[i].name);
	}


	const struct expansionboardsettings *cbs = cpuboards[workprefs.cpuboard_type].subtypes[workprefs.cpuboard_subtype].settings;
	create_expansionrom_gui(hDlg, &accelerator_gui_item, cbs, workprefs.cpuboard_settings, NULL,
		IDC_ACCELERATORBOARDITEMSELECTOR, IDC_ACCELERATORBOARDSELECTOR, IDC_ACCELERATORBOARDCHECKBOX, -1);

#if 0
	int i = 0;
	if (cbs) {
		for (i = 0; cbs[i].name; i++) {
			int id = cpuboard_settings_id[i];
			if (id < 0)
				break;
			SetWindowText(GetDlgItem(hDlg, id), cbs[i].name);
		}
	}
	while (cpuboard_settings_id[i] >= 0) {
		int id = cpuboard_settings_id[i];
		SetWindowText(GetDlgItem(hDlg, id), _T("-"));
		i++;
	}
#endif
}

static int gui_rtg_index;

static void expansion2filebuttons(HWND hDlg, WPARAM wParam, TCHAR *path)
{
	switch (LOWORD(wParam))
	{
		case IDC_SCSIROMCHOOSER:
		DiskSelection(hDlg, IDC_SCSIROMFILE, 6, &workprefs, NULL, path);
		values_to_expansion2dlg(hDlg, 1);
		break;
		case IDC_CPUBOARDROMCHOOSER:
		DiskSelection(hDlg, IDC_CPUBOARDROMFILE, 6, &workprefs, NULL, path);
		values_to_expansion2dlg(hDlg, 2);
		break;
	}
}


static INT_PTR CALLBACK Expansion2DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	int v, val;
	TCHAR tmp[MAX_DPATH];
	static int recursive = 0;

	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch (msg)
	{
		case WM_INITDIALOG:
		{
			recursive++;
			pages[EXPANSION2_ID] = hDlg;
			currentpage = EXPANSION2_ID;
			int ids[] = { IDC_SCSIROMFILE, IDC_CPUBOARDROMFILE, -1 };
			setmultiautocomplete(hDlg, ids);

			if (!net_enumerated) {
				target_ethernet_enumerate();
				for (int i = 0; ndd[i]; i++) {
					struct netdriverdata *n = ndd[i];
					if (!n->active)
						continue;
					if (n->type == UAENET_SLIRP) {
						WIN32GUI_LoadUIString(IDS_SLIRP, tmp, sizeof tmp / sizeof(TCHAR));
						n->desc = my_strdup(tmp);
					}
					else if (n->type == UAENET_SLIRP_INBOUND) {
						WIN32GUI_LoadUIString(IDS_SLIRP_INBOUND, tmp, sizeof tmp / sizeof(TCHAR));
						n->desc = my_strdup(tmp);
					}
				}
				ethernet_updateselection();
				net_enumerated = 1;
			}

			xSendDlgItemMessage(hDlg, IDC_CPUBOARD_TYPE, CB_RESETCONTENT, 0, 0);
			for (int i = 0; cpuboards[i].name; i++) {
				xSendDlgItemMessage(hDlg, IDC_CPUBOARD_TYPE, CB_ADDSTRING, 0, (LPARAM) cpuboards[i].name);
			}

			WIN32GUI_LoadUIString(IDS_EXPANSION_CATEGORY, tmp, sizeof tmp / sizeof(TCHAR));
			xSendDlgItemMessage(hDlg, IDC_SCSIROMSELECTCAT, CB_RESETCONTENT, 0, 0);
			_tcscat(tmp, _T("\n"));
			TCHAR *p1 = tmp;
			for (;;) {
				TCHAR *p2 = _tcschr(p1, '\n');
				if (p2 && _tcslen(p2) > 0) {
					*p2++ = 0;
					xSendDlgItemMessage(hDlg, IDC_SCSIROMSELECTCAT, CB_ADDSTRING, 0, (LPARAM)p1);
					p1 = p2;
				} else
					break;
			}

			reset_expansionrom_gui(hDlg, &expansion_gui_item, IDC_EXPANSIONBOARDITEMSELECTOR, IDC_EXPANSIONBOARDSELECTOR, IDC_EXPANSIONBOARDCHECKBOX, IDC_EXPANSIONBOARDSTRINGBOX);
			reset_expansionrom_gui(hDlg, &accelerator_gui_item, IDC_ACCELERATORBOARDITEMSELECTOR, IDC_ACCELERATORBOARDSELECTOR, IDC_ACCELERATORBOARDCHECKBOX, -1);

			hide(hDlg, IDC_SCSIROMSELECTED, 1);
			init_expansion2(hDlg, true);
			updatecpuboardsubtypes(hDlg);
			setcpuboardmemsize(hDlg);

			recursive--;
		}

		case WM_USER:
		recursive++;
		values_to_expansion2dlg(hDlg, 0);
		enable_for_expansion2dlg(hDlg);
		recursive--;
		break;

		case WM_COMMAND:
		{
			if (recursive > 0)
				break;
			recursive++;
			switch (LOWORD(wParam))
			{
				case IDC_EXPANSIONBOARDSTRINGBOX:
				get_expansionrom_gui(hDlg, &expansion_gui_item);
				values_from_expansion2dlg(hDlg);
				break;
				case IDC_EXPANSIONBOARDCHECKBOX:
				get_expansionrom_gui(hDlg, &expansion_gui_item);
				values_from_expansion2dlg(hDlg);
				break;
				case IDC_ACCELERATORBOARDCHECKBOX:
				get_expansionrom_gui(hDlg, &accelerator_gui_item);
				values_from_expansion2dlg(hDlg);
				break;
				case IDC_SCSIROMFILEAUTOBOOT:
				case IDC_SCSIROMFILEPCMCIA:
				case IDC_SCSIROM24BITDMA:
				values_from_expansion2dlg(hDlg);
				break;
				case IDC_SOCKETS:
				workprefs.socket_emu = ischecked(hDlg, IDC_SOCKETS);
				break;
				case IDC_SCSIDEVICE:
				workprefs.scsi = ischecked(hDlg, IDC_SCSIDEVICE);
				enable_for_expansion2dlg(hDlg);
				break;
				case IDC_SANA2:
				workprefs.sana2 = ischecked(hDlg, IDC_SANA2);
				break;
				case IDC_CATWEASEL:
				workprefs.catweasel = ischecked(hDlg, IDC_CATWEASEL) ? -1 : 0;
				cfgfile_compatibility_romtype(&workprefs);
				break;
				break;
				case IDC_SCSIROMSELECTED:
				values_from_expansion2dlg(hDlg);
				values_to_expansion2_expansion_settings(hDlg, 1);
				break;
			}
			expansion2filebuttons(hDlg, wParam, NULL);
			if (HIWORD(wParam) == CBN_SELENDOK || HIWORD(wParam) == CBN_KILLFOCUS || HIWORD(wParam) == CBN_EDITCHANGE) {
				switch (LOWORD(wParam))
				{
					case IDC_EXPANSIONBOARDITEMSELECTOR:
					case IDC_EXPANSIONBOARDSELECTOR:
					get_expansionrom_gui(hDlg, &expansion_gui_item);
					values_from_expansion2dlg(hDlg);
					break;
					case IDC_ACCELERATORBOARDITEMSELECTOR:
					case IDC_ACCELERATORBOARDSELECTOR:
					get_expansionrom_gui(hDlg, &accelerator_gui_item);
					values_from_expansion2dlg(hDlg);
					break;
					case IDC_SCSIROMFILE:
					case IDC_SCSIROMID:
					values_from_expansion2dlg(hDlg);
					values_to_expansion2_expansion_settings(hDlg, 1);
					break;
					case IDC_CPUBOARDROMFILE:
					case IDC_CPUBOARDROMSUBSELECT:
					values_from_expansion2dlg(hDlg);
					values_to_expansion2_expansion_settings(hDlg, 2);
					break;
					case IDC_SCSIROMSUBSELECT:
					values_from_expansion2dlg(hDlg);
					values_to_expansion2_expansion_roms(hDlg, NULL);
					values_to_expansion2_expansion_settings(hDlg, 1);
					break;
					case IDC_SCSIROMSELECTCAT:
					val = xSendDlgItemMessage(hDlg, IDC_SCSIROMSELECTCAT, CB_GETCURSEL, 0, 0);
					if (val != CB_ERR && val != scsiromselectedcatnum) {
						scsiromselectedcatnum = val;
						scsiromselected = 0;
						init_expansion2(hDlg, false);
						values_to_expansion2_expansion_roms(hDlg, NULL);
						values_to_expansion2_expansion_settings(hDlg, 1);
						values_to_expansion2dlg_sub(hDlg);
					}
					break;
					case IDC_SCSIROMSELECTNUM:
					case IDC_SCSIROMSELECT:
					val = xSendDlgItemMessage(hDlg, IDC_SCSIROMSELECTNUM, CB_GETCURSEL, 0, 0);
					if (val != CB_ERR)
						scsiromselectednum = val;
					val = gui_get_string_cursor(scsiromselect_table, hDlg, IDC_SCSIROMSELECT);
					if (val != CB_ERR) {
						scsiromselected = val;
						values_to_expansion2_expansion_roms(hDlg, NULL);
						values_to_expansion2_expansion_settings(hDlg, 1);
						values_to_expansion2dlg_sub(hDlg);
					}
					break;
					case IDC_CPUBOARD_TYPE:
					v = xSendDlgItemMessage(hDlg, IDC_CPUBOARD_TYPE, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR && v != workprefs.cpuboard_type) {
						workprefs.cpuboard_type = v;
						workprefs.cpuboard_subtype = 0;
						workprefs.cpuboard_settings = 0;
						updatecpuboardsubtypes(hDlg);
						if (is_ppc_cpu(&workprefs)) {
							workprefs.ppc_mode = 2;
						} else if (workprefs.ppc_mode == 2) {
							workprefs.ppc_mode = 0;
						}
						cpuboard_set_cpu(&workprefs);
						setcpuboardmemsize(hDlg);
						enable_for_expansion2dlg(hDlg);
						values_to_expansion2dlg(hDlg, 2);
					}
					break;
					case IDC_CPUBOARD_SUBTYPE:
					v = xSendDlgItemMessage(hDlg, IDC_CPUBOARD_SUBTYPE, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR && v != workprefs.cpuboard_subtype) {
						workprefs.cpuboard_subtype = v;
						workprefs.cpuboard_settings = 0;
						updatecpuboardsubtypes(hDlg);
						if (is_ppc_cpu(&workprefs)) {
							workprefs.ppc_mode = 2;
						} else if (workprefs.ppc_mode == 2) {
							workprefs.ppc_mode = 0;
						}
						cpuboard_set_cpu(&workprefs);
						setcpuboardmemsize(hDlg);
						enable_for_expansion2dlg(hDlg);
						values_to_expansion2dlg(hDlg, 2);
					}
					break;
				}
			}
#if 0
			for (int i = 0; cpuboard_settings_id[i] >= 0; i++) {
				workprefs.cpuboard_settings &= ~(1 << i);
				if (ischecked(hDlg, cpuboard_settings_id[i]))
					workprefs.cpuboard_settings |= 1 << i;
			}
#endif
			recursive--;
		}
		break;
		case WM_HSCROLL:
		if (recursive > 0)
			break;
		recursive++;
		workprefs.cpuboardmem1.size = memsizes[msi_cpuboard[SendMessage(GetDlgItem(hDlg, IDC_CPUBOARDMEM), TBM_GETPOS, 0, 0)]];
		setcpuboardmemsize(hDlg);
		recursive--;
		break;
	}
	return FALSE;
}

static void enable_for_expansiondlg(HWND hDlg)
{
	struct rtgboardconfig *rbc = &workprefs.rtgboards[gui_rtg_index];
	int z3 = true;
	int en;

	en = !!full_property_sheet;

	int rtg = workprefs.rtgboards[gui_rtg_index].rtgmem_size && full_property_sheet && workprefs.rtgboards[gui_rtg_index].rtgmem_type < GFXBOARD_HARDWARE;
	int rtg2 = workprefs.rtgboards[gui_rtg_index].rtgmem_size || workprefs.rtgboards[gui_rtg_index].rtgmem_type >= GFXBOARD_HARDWARE;
	int rtg3 = workprefs.rtgboards[gui_rtg_index].rtgmem_size && workprefs.rtgboards[gui_rtg_index].rtgmem_type < GFXBOARD_HARDWARE;
	int rtg4 = workprefs.rtgboards[gui_rtg_index].rtgmem_type < GFXBOARD_HARDWARE;
	int rtg5 = workprefs.rtgboards[gui_rtg_index].rtgmem_size && full_property_sheet;

	int rtg0 = rtg2;
	if (gui_rtg_index > 0) {
		rtg = false;
		rtg2 = false;
		rtg3 = false;
	}

	ew(hDlg, IDC_P96RAM, rtg0);
	ew(hDlg, IDC_P96MEM, rtg0);
	ew(hDlg, IDC_RTG_Z2Z3, z3);
	ew(hDlg, IDC_MONITOREMU_MON, rtg5);
	ew(hDlg, IDC_RTG_8BIT, rtg);
	ew(hDlg, IDC_RTG_16BIT, rtg);
	ew(hDlg, IDC_RTG_24BIT, rtg);
	ew(hDlg, IDC_RTG_32BIT, rtg);
	ew(hDlg, IDC_RTG_SCALE, rtg2);
	ew(hDlg, IDC_RTG_CENTER, rtg2);
	ew(hDlg, IDC_RTG_INTEGERSCALE, rtg2);
	ew(hDlg, IDC_RTG_SCALE_ALLOW, rtg2);
	ew(hDlg, IDC_RTG_SCALE_ASPECTRATIO, rtg2);
	ew(hDlg, IDC_RTG_VBLANKRATE, rtg2);
	ew(hDlg, IDC_RTG_BUFFERCNT, rtg2);
	ew(hDlg, IDC_RTG_DISPLAYSELECT, rtg2);
	ew(hDlg, IDC_RTG_VBINTERRUPT, rtg3);
	ew(hDlg, IDC_RTG_THREAD, rtg3 && en);
	ew(hDlg, IDC_RTG_HWSPRITE, rtg3);

	ew(hDlg, IDC_RTG_SWITCHER, rbc->rtgmem_size > 0 && !gfxboard_get_switcher(rbc));
}

static void values_to_expansiondlg(HWND hDlg)
{
	xSendDlgItemMessage(hDlg, IDC_RTG_BUFFERCNT, CB_SETCURSEL, workprefs.gfx_apmode[1].gfx_backbuffers == 0 ? 0 : workprefs.gfx_apmode[1].gfx_backbuffers - 1, 0);

	int min_mem = MIN_P96_MEM;
	int max_mem = MAX_P96_MEM_Z3;
	struct rtgboardconfig *rbc = &workprefs.rtgboards[gui_rtg_index];
	if (gfxboard_get_configtype(rbc) == 2) {
		int v = rbc->rtgmem_size;
		max_mem = MAX_P96_MEM_Z2;
		rbc->rtgmem_size = 1024 * 1024;
		if (rbc->rtgmem_type >= GFXBOARD_HARDWARE && v > gfxboard_get_vram_max(rbc))
			v = gfxboard_get_vram_max(rbc);
		if (rbc->rtgmem_type >= GFXBOARD_HARDWARE && v < gfxboard_get_vram_min(rbc))
			v = gfxboard_get_vram_min(rbc);
		rbc->rtgmem_size = v;
	} else if (gfxboard_get_configtype(rbc) == 3) {
		int v = rbc->rtgmem_size;
		if (rbc->rtgmem_type >= GFXBOARD_HARDWARE && v > gfxboard_get_vram_max(rbc))
			v = gfxboard_get_vram_max(rbc);
		if (rbc->rtgmem_type >= GFXBOARD_HARDWARE && v < gfxboard_get_vram_min(rbc))
			v = gfxboard_get_vram_min(rbc);
		rbc->rtgmem_size = v;
	} else {
		int v = rbc->rtgmem_size;
		if (rbc->rtgmem_type >= GFXBOARD_HARDWARE && v > gfxboard_get_vram_max(rbc))
			v = gfxboard_get_vram_max(rbc);
		if (rbc->rtgmem_type >= GFXBOARD_HARDWARE && v < gfxboard_get_vram_min(rbc))
			v = gfxboard_get_vram_min(rbc);
		rbc->rtgmem_size = v;
	}
	if (rbc->rtgmem_type >= GFXBOARD_HARDWARE) {
		switch (gfxboard_get_vram_min(rbc)) {
			case 0x00100000: min_mem = 1; break;
			case 0x00200000: min_mem = 2; break;
			case 0x00400000: min_mem = 3; break;
		}
		switch (gfxboard_get_vram_max(rbc)) {
			case 0x00100000: max_mem = 1; break;
			case 0x00200000: max_mem = 2; break;
			case 0x00400000: max_mem = 3; break;
		}
	}
	xSendDlgItemMessage(hDlg, IDC_P96MEM, TBM_SETRANGE, TRUE, MAKELONG(min_mem, max_mem));
	int mem_size = 0;
	switch (rbc->rtgmem_size) {
		case 0x00000000: mem_size = 0; break;
		case 0x00100000: mem_size = 1; break;
		case 0x00200000: mem_size = 2; break;
		case 0x00400000: mem_size = 3; break;
		case 0x00800000: mem_size = 4; break;
		case 0x01000000: mem_size = 5; break;
		case 0x02000000: mem_size = 6; break;
		case 0x04000000: mem_size = 7; break;
		case 0x08000000: mem_size = 8; break;
		case 0x10000000: mem_size = 9; break;
		case 0x20000000: mem_size = 10; break;
		case 0x40000000: mem_size = 11; break;
	}
	xSendDlgItemMessage(hDlg, IDC_P96MEM, TBM_SETPOS, TRUE, mem_size);
	SetDlgItemText(hDlg, IDC_P96RAM, memsize_names[msi_gfx[mem_size]]);

	xSendDlgItemMessage(hDlg, IDC_RTG_Z2Z3, CB_SETCURSEL, rbc->rtgmem_size == 0 ? 0 : gfxboard_get_index_from_id(rbc->rtgmem_type) + 1, 0);
	xSendDlgItemMessage(hDlg, IDC_MONITOREMU_MON, CB_SETCURSEL, rbc->monitor_id, 0);
	xSendDlgItemMessage(hDlg, IDC_RTG_NUM, CB_SETCURSEL, gui_rtg_index, 0);
	xSendDlgItemMessage(hDlg, IDC_RTG_8BIT, CB_SETCURSEL, (workprefs.picasso96_modeflags & RGBFF_CLUT) ? 1 : 0, 0);
	xSendDlgItemMessage(hDlg, IDC_RTG_16BIT, CB_SETCURSEL,
					   (manybits(workprefs.picasso96_modeflags, RGBFF_R5G6B5PC | RGBFF_R5G6B5PC | RGBFF_R5G6B5 | RGBFF_R5G5B5 | RGBFF_B5G6R5PC | RGBFF_B5G5R5PC)) ? 1 :
					   (workprefs.picasso96_modeflags & RGBFF_R5G6B5PC) ? 2 :
					   (workprefs.picasso96_modeflags & RGBFF_R5G5B5PC) ? 3 :
					   (workprefs.picasso96_modeflags & RGBFF_R5G6B5) ? 4 :
					   (workprefs.picasso96_modeflags & RGBFF_R5G5B5) ? 5 :
					   (workprefs.picasso96_modeflags & RGBFF_B5G6R5PC) ? 6 :
					   (workprefs.picasso96_modeflags & RGBFF_B5G5R5PC) ? 7 : 0, 0);
	xSendDlgItemMessage(hDlg, IDC_RTG_24BIT, CB_SETCURSEL,
					   (manybits(workprefs.picasso96_modeflags, RGBFF_R8G8B8 | RGBFF_B8G8R8)) ? 1 :
					   (workprefs.picasso96_modeflags & RGBFF_R8G8B8) ? 2 :
					   (workprefs.picasso96_modeflags & RGBFF_B8G8R8) ? 3 : 0, 0);
	xSendDlgItemMessage(hDlg, IDC_RTG_32BIT, CB_SETCURSEL,
					   (manybits(workprefs.picasso96_modeflags, RGBFF_A8R8G8B8 | RGBFF_A8B8G8R8 | RGBFF_R8G8B8A8 | RGBFF_B8G8R8A8)) ? 1 :
					   (workprefs.picasso96_modeflags & RGBFF_A8R8G8B8) ? 2 :
					   (workprefs.picasso96_modeflags & RGBFF_A8B8G8R8) ? 3 :
					   (workprefs.picasso96_modeflags & RGBFF_R8G8B8A8) ? 4 :
					   (workprefs.picasso96_modeflags & RGBFF_B8G8R8A8) ? 5 : 0, 0);
	if (workprefs.win32_rtgvblankrate <= 0 ||
		workprefs.win32_rtgvblankrate == 50 ||
		workprefs.win32_rtgvblankrate == 60 ||
		workprefs.win32_rtgvblankrate == 70 ||
		workprefs.win32_rtgvblankrate == 75) {
		xSendDlgItemMessage(hDlg, IDC_RTG_VBLANKRATE, CB_SETCURSEL,
						   (workprefs.win32_rtgvblankrate == 0) ? 0 :
						   (workprefs.win32_rtgvblankrate == -1) ? 1 :
						   (workprefs.win32_rtgvblankrate == -2) ? 0 :
						   (workprefs.win32_rtgvblankrate == 50) ? 2 :
						   (workprefs.win32_rtgvblankrate == 60) ? 3 :
						   (workprefs.win32_rtgvblankrate == 70) ? 4 :
						   (workprefs.win32_rtgvblankrate == 75) ? 5 : 0, 0);
	} else {
		TCHAR tmp[10];
		_stprintf(tmp, _T("%d"), workprefs.win32_rtgvblankrate);
		xSendDlgItemMessage(hDlg, IDC_RTG_VBLANKRATE, WM_SETTEXT, 0, (LPARAM) tmp);
	}

	CheckDlgButton(hDlg, IDC_RTG_SCALE, workprefs.gf[1].gfx_filter_autoscale == RTG_MODE_SCALE);
	CheckDlgButton(hDlg, IDC_RTG_CENTER, workprefs.gf[1].gfx_filter_autoscale == RTG_MODE_CENTER);
	CheckDlgButton(hDlg, IDC_RTG_INTEGERSCALE, workprefs.gf[1].gfx_filter_autoscale == RTG_MODE_INTEGER_SCALE);
	CheckDlgButton(hDlg, IDC_RTG_SCALE_ALLOW, workprefs.win32_rtgallowscaling);
	CheckDlgButton(hDlg, IDC_RTG_VBINTERRUPT, workprefs.rtg_hardwareinterrupt);
	CheckDlgButton(hDlg, IDC_RTG_HWSPRITE, workprefs.rtg_hardwaresprite);
	CheckDlgButton(hDlg, IDC_RTG_THREAD, workprefs.rtg_multithread);
	CheckDlgButton(hDlg, IDC_RTG_SWITCHER, rbc->rtgmem_size > 0 && (rbc->autoswitch || gfxboard_get_switcher(rbc) || rbc->rtgmem_type < GFXBOARD_HARDWARE));

	xSendDlgItemMessage(hDlg, IDC_RTG_SCALE_ASPECTRATIO, CB_SETCURSEL,
					   (workprefs.win32_rtgscaleaspectratio == 0) ? 0 :
					   (workprefs.win32_rtgscaleaspectratio < 0) ? 1 :
					   getaspectratioindex(workprefs.win32_rtgscaleaspectratio) + 2, 0);
}

static INT_PTR CALLBACK ExpansionDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	int v;
	TCHAR tmp[256];
	static int recursive = 0;
	static int enumerated;

	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch (msg)
	{
	case WM_INITDIALOG:
		pages[EXPANSION_ID] = hDlg;
		currentpage = EXPANSION_ID;

		init_displays_combo (hDlg, true);
		
		xSendDlgItemMessage(hDlg, IDC_RTG_NUM, CB_RESETCONTENT, 0, 0);
		for (int i = 0; i < MAX_RTG_BOARDS; i++) {
			_stprintf(tmp, _T("%d"), i + 1);
			xSendDlgItemMessage(hDlg, IDC_RTG_NUM, CB_ADDSTRING, 0, (LPARAM)tmp);
		}

		xSendDlgItemMessage(hDlg, IDC_MONITOREMU_MON, CB_RESETCONTENT, 0, 0);
		for (int i = 0; i < MAX_AMIGAMONITORS; i++) {
			_stprintf(tmp, _T("%d"), i + 1);
			xSendDlgItemMessage(hDlg, IDC_MONITOREMU_MON, CB_ADDSTRING, 0, (LPARAM)tmp);
		}

		xSendDlgItemMessage (hDlg, IDC_RTG_Z2Z3, CB_RESETCONTENT, 0, 0);
		xSendDlgItemMessage (hDlg, IDC_RTG_Z2Z3, CB_ADDSTRING, 0, (LPARAM)_T("-"));
		v = 0;
		for (;;) {
			int index = gfxboard_get_id_from_index(v);
			if (index < 0)
				break;
			const TCHAR *n1 = gfxboard_get_name(index);
			const TCHAR *n2 = gfxboard_get_manufacturername(index);
			v++;
			_tcscpy(tmp, n1);
			if (n2) {
				_tcscat(tmp, _T(" ("));
				_tcscat(tmp, n2);
				_tcscat(tmp, _T(")"));
			}
			xSendDlgItemMessage (hDlg, IDC_RTG_Z2Z3, CB_ADDSTRING, 0, (LPARAM)tmp);
		}

		WIN32GUI_LoadUIString(IDS_ALL, tmp, sizeof tmp / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_RTG_8BIT, CB_RESETCONTENT, 0, 0);
		xSendDlgItemMessage (hDlg, IDC_RTG_8BIT, CB_ADDSTRING, 0, (LPARAM)_T("(8bit)"));
		xSendDlgItemMessage (hDlg, IDC_RTG_8BIT, CB_ADDSTRING, 0, (LPARAM)_T("8-bit (*)"));
		xSendDlgItemMessage (hDlg, IDC_RTG_16BIT, CB_RESETCONTENT, 0, 0);
		xSendDlgItemMessage (hDlg, IDC_RTG_16BIT, CB_ADDSTRING, 0, (LPARAM)_T("(15/16bit)"));
		xSendDlgItemMessage (hDlg, IDC_RTG_16BIT, CB_ADDSTRING, 0, (LPARAM)tmp);
		xSendDlgItemMessage (hDlg, IDC_RTG_16BIT, CB_ADDSTRING, 0, (LPARAM)_T("R5G6B5PC (*)"));
		xSendDlgItemMessage (hDlg, IDC_RTG_16BIT, CB_ADDSTRING, 0, (LPARAM)_T("R5G5B5PC"));
		xSendDlgItemMessage (hDlg, IDC_RTG_16BIT, CB_ADDSTRING, 0, (LPARAM)_T("R5G6B5"));
		xSendDlgItemMessage (hDlg, IDC_RTG_16BIT, CB_ADDSTRING, 0, (LPARAM)_T("R5G5B5"));
		xSendDlgItemMessage (hDlg, IDC_RTG_16BIT, CB_ADDSTRING, 0, (LPARAM)_T("B5G6R5PC"));
		xSendDlgItemMessage (hDlg, IDC_RTG_16BIT, CB_ADDSTRING, 0, (LPARAM)_T("B5G5R5PC"));
		xSendDlgItemMessage (hDlg, IDC_RTG_24BIT, CB_RESETCONTENT, 0, 0);
		xSendDlgItemMessage (hDlg, IDC_RTG_24BIT, CB_ADDSTRING, 0, (LPARAM)_T("(24bit)"));
		xSendDlgItemMessage (hDlg, IDC_RTG_24BIT, CB_ADDSTRING, 0, (LPARAM)tmp);
		xSendDlgItemMessage (hDlg, IDC_RTG_24BIT, CB_ADDSTRING, 0, (LPARAM)_T("R8G8B8"));
		xSendDlgItemMessage (hDlg, IDC_RTG_24BIT, CB_ADDSTRING, 0, (LPARAM)_T("B8G8R8"));
		xSendDlgItemMessage (hDlg, IDC_RTG_32BIT, CB_RESETCONTENT, 0, 0);
		xSendDlgItemMessage (hDlg, IDC_RTG_32BIT, CB_ADDSTRING, 0, (LPARAM)_T("(32bit)"));
		xSendDlgItemMessage (hDlg, IDC_RTG_32BIT, CB_ADDSTRING, 0, (LPARAM)tmp);
		xSendDlgItemMessage (hDlg, IDC_RTG_32BIT, CB_ADDSTRING, 0, (LPARAM)_T("A8R8G8B8"));
		xSendDlgItemMessage (hDlg, IDC_RTG_32BIT, CB_ADDSTRING, 0, (LPARAM)_T("A8B8G8R8"));
		xSendDlgItemMessage (hDlg, IDC_RTG_32BIT, CB_ADDSTRING, 0, (LPARAM)_T("R8G8B8A8"));
		xSendDlgItemMessage (hDlg, IDC_RTG_32BIT, CB_ADDSTRING, 0, (LPARAM)_T("B8G8R8A8 (*)"));
		xSendDlgItemMessage (hDlg, IDC_P96MEM, TBM_SETRANGE, TRUE, MAKELONG (MIN_P96_MEM, gfxboard_get_configtype(&workprefs.rtgboards[gui_rtg_index]) == 3 ? MAX_P96_MEM_Z3 : MAX_P96_MEM_Z2));
		xSendDlgItemMessage (hDlg, IDC_RTG_SCALE_ASPECTRATIO, CB_RESETCONTENT, 0, 0);
		WIN32GUI_LoadUIString (IDS_DISABLED, tmp, sizeof tmp / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_RTG_SCALE_ASPECTRATIO, CB_ADDSTRING, 0, (LPARAM)tmp);
		WIN32GUI_LoadUIString (IDS_AUTOMATIC, tmp, sizeof tmp / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_RTG_SCALE_ASPECTRATIO, CB_ADDSTRING, 0, (LPARAM)tmp);
		addaspectratios (hDlg, IDC_RTG_SCALE_ASPECTRATIO);
		xSendDlgItemMessage (hDlg, IDC_RTG_VBLANKRATE, CB_RESETCONTENT, 0, 0);
		xSendDlgItemMessage (hDlg, IDC_RTG_VBLANKRATE, CB_ADDSTRING, 0, (LPARAM)_T("Chipset"));
		xSendDlgItemMessage (hDlg, IDC_RTG_VBLANKRATE, CB_ADDSTRING, 0, (LPARAM)_T("Default"));
		xSendDlgItemMessage (hDlg, IDC_RTG_VBLANKRATE, CB_ADDSTRING, 0, (LPARAM)_T("50"));
		xSendDlgItemMessage (hDlg, IDC_RTG_VBLANKRATE, CB_ADDSTRING, 0, (LPARAM)_T("60"));
		xSendDlgItemMessage (hDlg, IDC_RTG_VBLANKRATE, CB_ADDSTRING, 0, (LPARAM)_T("70"));
		xSendDlgItemMessage (hDlg, IDC_RTG_VBLANKRATE, CB_ADDSTRING, 0, (LPARAM)_T("75"));
		xSendDlgItemMessage(hDlg, IDC_RTG_BUFFERCNT, CB_RESETCONTENT, 0, 0);
		WIN32GUI_LoadUIString(IDS_BUFFER_DOUBLE, tmp, sizeof tmp / sizeof (TCHAR));
		xSendDlgItemMessage(hDlg, IDC_RTG_BUFFERCNT, CB_ADDSTRING, 0, (LPARAM)tmp);
		WIN32GUI_LoadUIString(IDS_BUFFER_TRIPLE, tmp, sizeof tmp / sizeof (TCHAR));
		xSendDlgItemMessage(hDlg, IDC_RTG_BUFFERCNT, CB_ADDSTRING, 0, (LPARAM)tmp);

	case WM_USER:
		recursive++;
		values_to_expansiondlg (hDlg);
		enable_for_expansiondlg (hDlg);
		recursive--;
		break;

	case WM_HSCROLL:
		workprefs.rtgboards[gui_rtg_index].rtgmem_size = memsizes[msi_gfx[SendMessage (GetDlgItem (hDlg, IDC_P96MEM), TBM_GETPOS, 0, 0)]];
		values_to_expansiondlg(hDlg);
		enable_for_expansiondlg(hDlg);
		break;

	case WM_COMMAND:
		{
			if (recursive > 0)
				break;
			recursive++;
			switch (LOWORD (wParam))
			{
			case IDC_RTG_SCALE:
				workprefs.gf[1].gfx_filter_autoscale = ischecked(hDlg, IDC_RTG_SCALE) ? RTG_MODE_SCALE : 0;
				setchecked(hDlg, IDC_RTG_CENTER,  false);
				setchecked(hDlg, IDC_RTG_INTEGERSCALE, false);
				break;
			case IDC_RTG_CENTER:
				workprefs.gf[1].gfx_filter_autoscale = ischecked(hDlg, IDC_RTG_CENTER) ? RTG_MODE_CENTER : 0;
				setchecked(hDlg, IDC_RTG_SCALE, false);
				setchecked(hDlg, IDC_RTG_INTEGERSCALE, false);
				break;
			case IDC_RTG_INTEGERSCALE:
				workprefs.gf[1].gfx_filter_autoscale = ischecked(hDlg, IDC_RTG_INTEGERSCALE) ? RTG_MODE_INTEGER_SCALE : 0;
				setchecked(hDlg, IDC_RTG_SCALE, false);
				setchecked(hDlg, IDC_RTG_CENTER, false);
				break;
			case IDC_RTG_SCALE_ALLOW:
				workprefs.win32_rtgallowscaling = ischecked(hDlg, IDC_RTG_SCALE_ALLOW);
				break;
			case IDC_RTG_VBINTERRUPT:
				workprefs.rtg_hardwareinterrupt = ischecked(hDlg, IDC_RTG_VBINTERRUPT);
				break;
			case IDC_RTG_HWSPRITE:
				workprefs.rtg_hardwaresprite = ischecked(hDlg, IDC_RTG_HWSPRITE);
				break;
			case IDC_RTG_THREAD:
				workprefs.rtg_multithread = ischecked(hDlg, IDC_RTG_THREAD);
				break;
			case IDC_RTG_SWITCHER:
				{
					struct rtgboardconfig *rbc = &workprefs.rtgboards[gui_rtg_index];
					rbc->autoswitch = ischecked(hDlg, IDC_RTG_SWITCHER);
					break;
				}
			}
			if (HIWORD (wParam) == CBN_SELENDOK || HIWORD (wParam) == CBN_KILLFOCUS || HIWORD (wParam) == CBN_EDITCHANGE)  {
				uae_u32 mask = workprefs.picasso96_modeflags;
				switch (LOWORD (wParam))
				{
				case IDC_RTG_DISPLAYSELECT:
					get_displays_combo (hDlg, true);
					break;
				case  IDC_RTG_BUFFERCNT:
					v = xSendDlgItemMessage (hDlg, IDC_RTG_BUFFERCNT, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR) {
						v++;
						workprefs.gfx_apmode[1].gfx_backbuffers = v;
					}
					break;
				case IDC_RTG_SCALE_ASPECTRATIO:
					v = xSendDlgItemMessage (hDlg, IDC_RTG_SCALE_ASPECTRATIO, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR) {
						if (v == 0) {
							workprefs.win32_rtgscaleaspectratio = 0;
						} else if (v == 1) {
							workprefs.win32_rtgscaleaspectratio = -1;
						} else if (v >= 2) {
							workprefs.win32_rtgscaleaspectratio = getaspectratio (v - 2);
						}
						workprefs.gf[GF_RTG].gfx_filter_aspect = workprefs.win32_rtgscaleaspectratio;
					}
					break;
				case IDC_RTG_NUM:
					v = xSendDlgItemMessage(hDlg, IDC_RTG_NUM, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR) {
						gui_rtg_index = v;
						values_to_expansiondlg(hDlg);
						enable_for_expansiondlg(hDlg);
					}
					break;
				case IDC_MONITOREMU_MON:
					v = xSendDlgItemMessage(hDlg, IDC_MONITOREMU_MON, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR) {
						workprefs.rtgboards[gui_rtg_index].monitor_id = v;
						values_to_expansiondlg(hDlg);
					}
					break;
				case IDC_RTG_Z2Z3:
					v = xSendDlgItemMessage (hDlg, IDC_RTG_Z2Z3, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR) {
						if (v == 0) {
							workprefs.rtgboards[gui_rtg_index].rtgmem_type = 1;
							workprefs.rtgboards[gui_rtg_index].rtgmem_size = 0;
						} else {
							workprefs.rtgboards[gui_rtg_index].rtgmem_type = gfxboard_get_id_from_index(v - 1);
							if (workprefs.rtgboards[gui_rtg_index].rtgmem_size == 0)
								workprefs.rtgboards[gui_rtg_index].rtgmem_size = 4096 * 1024;
						}
						cfgfile_compatibility_rtg(&workprefs);
						enable_for_expansiondlg (hDlg);
					}
					break;
				case IDC_RTG_8BIT:
					v = xSendDlgItemMessage (hDlg, IDC_RTG_8BIT, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR) {
						mask &= ~RGBFF_CLUT;
						if (v == 1)
							mask |= RGBFF_CLUT;
					}
					break;
				case IDC_RTG_16BIT:
					v = xSendDlgItemMessage (hDlg, IDC_RTG_16BIT, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR) {
						mask &= ~(RGBFF_R5G6B5PC | RGBFF_R5G5B5PC | RGBFF_R5G6B5 | RGBFF_R5G5B5 | RGBFF_B5G6R5PC | RGBFF_B5G5R5PC);
						if (v == 1)
							mask |= RGBFF_R5G6B5PC | RGBFF_R5G6B5PC | RGBFF_R5G5B5PC | RGBFF_R5G6B5 | RGBFF_R5G5B5 | RGBFF_B5G6R5PC | RGBFF_B5G5R5PC;
						if (v == 2)
							mask |= RGBFF_R5G6B5PC;
						if (v == 3)
							mask |= RGBFF_R5G5B5PC;
						if (v == 4)
							mask |= RGBFF_R5G6B5;
						if (v == 5)
							mask |= RGBFF_R5G5B5;
						if (v == 6)
							mask |= RGBFF_B5G6R5PC;
						if (v == 7)
							mask |= RGBFF_B5G5R5PC;
					}
					break;
				case IDC_RTG_24BIT:
					v = xSendDlgItemMessage (hDlg, IDC_RTG_24BIT, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR) {
						mask &= ~(RGBFF_R8G8B8 | RGBFF_B8G8R8);
						if (v == 1)
							mask |= RGBFF_R8G8B8 | RGBFF_B8G8R8;
						if (v == 2)
							mask |= RGBFF_R8G8B8;
						if (v == 3)
							mask |= RGBFF_B8G8R8;
					}
					break;
				case IDC_RTG_32BIT:
					v = xSendDlgItemMessage (hDlg, IDC_RTG_32BIT, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR) {
						mask &= ~(RGBFF_A8R8G8B8 | RGBFF_A8B8G8R8 | RGBFF_R8G8B8A8 | RGBFF_B8G8R8A8);
						if (v == 1)
							mask |= RGBFF_A8R8G8B8 | RGBFF_A8B8G8R8 | RGBFF_R8G8B8A8 | RGBFF_B8G8R8A8;
						if (v == 2)
							mask |= RGBFF_A8R8G8B8;
						if (v == 3)
							mask |= RGBFF_A8B8G8R8;
						if (v == 4)
							mask |= RGBFF_R8G8B8A8;
						if (v == 5)
							mask |= RGBFF_B8G8R8A8;
					}
					break;
				case IDC_RTG_VBLANKRATE:
					tmp[0] = 0;
					v = xSendDlgItemMessage (hDlg, IDC_RTG_VBLANKRATE, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR) {
						if (v == 0) {
							workprefs.win32_rtgvblankrate = 0;
						} else if (v == 1) {
							workprefs.win32_rtgvblankrate = -1;
						} else {
							v = xSendDlgItemMessage (hDlg, IDC_RTG_VBLANKRATE, CB_GETLBTEXT, (WPARAM)v, (LPARAM)tmp);
						}
					} else {
						v = xSendDlgItemMessage (hDlg, IDC_RTG_VBLANKRATE, WM_GETTEXT, (WPARAM)sizeof tmp / sizeof (TCHAR), (LPARAM)tmp);
					}
					if (tmp[0])
						workprefs.win32_rtgvblankrate = _tstol (tmp);
					break;
				}
				workprefs.picasso96_modeflags = mask;
				values_to_expansiondlg (hDlg);
			}
			recursive--;
		}
		break;
	}
	return FALSE;
}


static LRESULT ProcesssBoardsDlgProcCustomDraw(LPARAM lParam)
{
}

static void BoardsEnable(HWND hDlg, int selected)
{
	bool move_up = expansion_can_move(&workprefs, selected);
	bool move_down = move_up;
	if (move_up) {
		if (expansion_autoconfig_move(&workprefs, selected, -1, true) < 0)
			move_up = false;
	}
	if (move_down) {
		if (expansion_autoconfig_move(&workprefs, selected, 1, true) < 0)
			move_down = false;
	}
	ew(hDlg, IDC_BOARDS_UP, move_up);
	ew(hDlg, IDC_BOARDS_DOWN, move_down);
}

static INT_PTR CALLBACK BoardsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;
	static int selected = -1;

	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch (msg)
	{
		case WM_INITDIALOG:
		recursive++;
		pages[BOARD_ID] = hDlg;
		currentpage = BOARD_ID;
		setchecked(hDlg, IDC_AUTOCONFIGCUSTOMSORT, workprefs.autoconfig_custom_sort);
		if (!g_darkModeEnabled) { // dark mode makes disabled list bright..
			ew(hDlg, IDC_BOARDLIST, workprefs.autoconfig_custom_sort != 0 && full_property_sheet);
		}
		ew(hDlg, IDC_BOARDS_UP, FALSE);
		ew(hDlg, IDC_BOARDS_DOWN, FALSE);
		ew(hDlg, IDC_AUTOCONFIGCUSTOMSORT, full_property_sheet);
		InitializeListView(hDlg);
		recursive--;
		break;

		case WM_COMMAND:
		if (!recursive) {
			recursive++;
			switch (LOWORD(wParam))
			{
				case IDC_AUTOCONFIGCUSTOMSORT:
				workprefs.autoconfig_custom_sort = ischecked(hDlg, IDC_AUTOCONFIGCUSTOMSORT);
				expansion_set_autoconfig_sort(&workprefs);
				if (!g_darkModeEnabled) { // dark mode makes disabled list bright..
					ew(hDlg, IDC_BOARDLIST, workprefs.autoconfig_custom_sort != 0);
				}
				InitializeListView(hDlg);
				break;
				case IDC_BOARDS_UP:
				case IDC_BOARDS_DOWN:
				if (selected >= 0) {
					int newpos = expansion_autoconfig_move(&workprefs, selected, LOWORD(wParam) == IDC_BOARDS_UP ? -1 : 1, false);
					if (newpos >= 0) {
						selected = newpos;
						BoardsEnable(hDlg, selected);
						InitializeListView(hDlg);
						ListView_SetItemState(cachedlist, selected, LVIS_SELECTED, LVIS_SELECTED);
					}
				}
				break;
			}
			recursive--;
		}
		break;

		case WM_NOTIFY:
		if (((LPNMHDR)lParam)->idFrom == IDC_BOARDLIST) {
			switch (((LPNMHDR)lParam)->code)
			{
				case NM_CUSTOMDRAW:
				{
					LPNMLVCUSTOMDRAW lpNMLVCD = (LPNMLVCUSTOMDRAW)lParam;
					switch (lpNMLVCD->nmcd.dwDrawStage)
					{
						case CDDS_PREPAINT:
						case CDDS_ITEMPREPAINT:
						SetWindowLongPtr(hDlg, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYSUBITEMDRAW);
						return TRUE;
						case CDDS_ITEMPREPAINT| CDDS_SUBITEM:
						{
							BOOL ret = FALSE;
							if (lpNMLVCD->nmcd.lItemlParam & 16) {
								lpNMLVCD->clrText = GetSysColor(COLOR_GRAYTEXT);
								SetWindowLongPtr(hDlg, DWLP_MSGRESULT, CDRF_NEWFONT);
								ret = TRUE;
							}
							if (lpNMLVCD->nmcd.lItemlParam & 8) {
								lpNMLVCD->clrTextBk = RGB(0xaa, 0x00, 0x00);
								SetWindowLongPtr(hDlg, DWLP_MSGRESULT, CDRF_NEWFONT);
								return TRUE;
							}
							if (lpNMLVCD->nmcd.lItemlParam & 4) {
								lpNMLVCD->clrTextBk = RGB(0xaa, 0xaa, 0x00);
								SetWindowLongPtr(hDlg, DWLP_MSGRESULT, CDRF_NEWFONT);
								return TRUE;
							}
							if (lpNMLVCD->nmcd.lItemlParam & 2) {
								lpNMLVCD->clrTextBk = GetSysColor(COLOR_INACTIVECAPTION);
								SetWindowLongPtr(hDlg, DWLP_MSGRESULT, CDRF_NEWFONT);
								return TRUE;
							}
							return ret;
						}
					}
				}
				return CDRF_DODEFAULT;
				case NM_CLICK:
				{
					int column;
					NM_LISTVIEW *nmlistview = (NM_LISTVIEW *)lParam;
					HWND list = nmlistview->hdr.hwndFrom;
					int entry = listview_entry_from_click(list, &column, false);
					if (entry >= 0) {
						selected = entry;
						BoardsEnable(hDlg, selected);
					}
				}
				break;
			}
		}
	}
	return FALSE;
}

static const struct memoryboardtype *getmemoryboardselect(HWND hDlg)
{
	int v = xSendDlgItemMessage(hDlg, IDC_MEMORYBOARDSELECT, CB_GETCURSEL, 0, 0L);
	if (v == CB_ERR)
		return NULL;
	int idx_z2 = 1;
	int idx_z3 = 1;
	for (int i = 0; memoryboards[i].name; i++) {
		const struct memoryboardtype *mbt = &memoryboards[i];
		if (fastram_select < MAX_STANDARD_RAM_BOARDS + MAX_RAM_BOARDS && mbt->z == 2) {
			if (idx_z2 == v) {
				return mbt;
			}
			idx_z2++;
		}
		if (fastram_select >= MAX_STANDARD_RAM_BOARDS + MAX_RAM_BOARDS && mbt->z == 3) {
			if (idx_z3 == v) {
				return mbt;
			}
			idx_z3++;
		}
	}
	return NULL;
}


static INT_PTR CALLBACK MemoryDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	TCHAR tmp[MAX_DPATH];
	static int recursive = 0;
	int v;

	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch (msg)
	{
	case WM_INITDIALOG:
		recursive++;
		pages[MEMORY_ID] = hDlg;
		currentpage = MEMORY_ID;
		xSendDlgItemMessage (hDlg, IDC_CHIPMEM, TBM_SETRANGE, TRUE, MAKELONG (MIN_CHIP_MEM, MAX_CHIP_MEM));
		xSendDlgItemMessage (hDlg, IDC_FASTMEM, TBM_SETRANGE, TRUE, MAKELONG (MIN_FAST_MEM, MAX_FAST_MEM));
		xSendDlgItemMessage (hDlg, IDC_SLOWMEM, TBM_SETRANGE, TRUE, MAKELONG (MIN_SLOW_MEM, MAX_SLOW_MEM));
		xSendDlgItemMessage (hDlg, IDC_Z3FASTMEM, TBM_SETRANGE, TRUE, MAKELONG (MIN_Z3_MEM, MAX_Z3_MEM));
		xSendDlgItemMessage (hDlg, IDC_Z3CHIPMEM, TBM_SETRANGE, TRUE, MAKELONG (MIN_Z3_MEM, MAX_Z3_CHIPMEM));
		xSendDlgItemMessage (hDlg, IDC_Z3MAPPING, CB_RESETCONTENT, 0, 0);
		WIN32GUI_LoadUIString (IDS_AUTOMATIC, tmp, sizeof tmp / sizeof (TCHAR));
		_tcscat(tmp, _T(" (*)"));
		xSendDlgItemMessage (hDlg, IDC_Z3MAPPING, CB_ADDSTRING, 0, (LPARAM)tmp);
		xSendDlgItemMessage(hDlg, IDC_Z3MAPPING, CB_ADDSTRING, 0, (LPARAM)_T("UAE (0x10000000)"));
		xSendDlgItemMessage(hDlg, IDC_Z3MAPPING, CB_ADDSTRING, 0, (LPARAM)_T("Real (0x40000000)"));
		xSendDlgItemMessage (hDlg, IDC_Z3MAPPING, CB_SETCURSEL, workprefs.z3_mapping_mode, 0);

		setfastram_selectmenu(hDlg, -1);

		recursive--;

	case WM_USER:
		recursive++;
		fix_values_memorydlg ();
		values_to_memorydlg (hDlg);
		enable_for_memorydlg (hDlg);
		recursive--;
		break;

	case WM_COMMAND:
		if (!recursive) {
			recursive++;
			switch (LOWORD(wParam))
			{
			case IDC_FASTMEMDMA:
				if (fastram_select_ramboard) {
					struct ramboard *rb = fastram_select_ramboard;
					rb->nodma = ischecked(hDlg, IDC_FASTMEMDMA) == 0;
					setfastram_selectmenu(hDlg, 0);
				}
				break;
			case IDC_FASTMEMFORCE16:
				if (fastram_select_ramboard) {
					struct ramboard *rb = fastram_select_ramboard;
					rb->force16bit = ischecked(hDlg, IDC_FASTMEMFORCE16) != 0;
					setfastram_selectmenu(hDlg, 0);
				}
				break;
			case IDC_FASTMEMSLOW:
				if (fastram_select_ramboard) {
					struct ramboard *rb = fastram_select_ramboard;
					rb->chipramtiming = ischecked(hDlg, IDC_FASTMEMSLOW) != 0;
					setfastram_selectmenu(hDlg, 0);
				}
				break;
			case IDC_FASTMEMAUTOCONFIGUSE:
				if (fastram_select_ramboard) {
					struct ramboard *rb = fastram_select_ramboard;
					rb->autoconfig_inuse = ischecked(hDlg, IDC_FASTMEMAUTOCONFIGUSE);
					rb->manual_config = false;
					setfastram_selectmenu(hDlg, 0);
				}
				break;
			case IDC_FASTMEMNOAUTOCONFIG:
				if (fastram_select_ramboard) {
					struct ramboard *rb = fastram_select_ramboard;
					rb->manual_config = ischecked(hDlg, IDC_FASTMEMNOAUTOCONFIG);
					rb->autoconfig_inuse = false;
					const struct memoryboardtype *mbt = getmemoryboardselect(hDlg);
					if (mbt && fastram_select_ramboard->manual_config && mbt->address) {
						fastram_select_ramboard->start_address = mbt->address;
						if (fastram_select_ramboard->end_address <= fastram_select_ramboard->start_address ||
							fastram_select_ramboard->end_address >= fastram_select_ramboard->start_address + fastram_select_ramboard->size)
						fastram_select_ramboard->end_address = mbt->address + fastram_select_ramboard->size - 1;
					}
					setfastram_selectmenu(hDlg, 0);
				}
				break;
			}
			if (HIWORD (wParam) == CBN_SELCHANGE || HIWORD (wParam) == CBN_KILLFOCUS)  {
				switch (LOWORD (wParam))
				{
					case IDC_Z3MAPPING:
					v = xSendDlgItemMessage (hDlg, IDC_Z3MAPPING, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR) {
						workprefs.z3_mapping_mode = v;
					}
					break;
					case IDC_MEMORYSELECT:
					v = xSendDlgItemMessage(hDlg, IDC_MEMORYSELECT, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR) {
						fastram_select = v;
						setfastram_selectmenu(hDlg, 0);
					}
					break;
					case IDC_MEMORYBOARDSELECT:
					if (fastram_select_ramboard) {
						const struct memoryboardtype *mbt = getmemoryboardselect(hDlg);

						// Various issues with RAM board selection since when
						// selecting a generic RAM board, mbt will be NULL.
						fastram_select_ramboard->manufacturer = 0;
						fastram_select_ramboard->product = 0;

						if (mbt) {
							if (mbt->manufacturer != 0xffff) {
								// Fix crash when changing between AutoConfig and manually
								// configurable boards.  WinUAE may have a buffer overflow
								// if custom AutoConfig conflicts with custom memory
								// range (these must be mutually exclusive).
								fastram_select_ramboard->manual_config = false;

								if (mbt->manufacturer) {
									fastram_select_ramboard->manufacturer = mbt->manufacturer;
									fastram_select_ramboard->product = mbt->product;
								}
							} else {
								fastram_select_ramboard->autoconfig_inuse = false;
								fastram_select_ramboard->manual_config = true;
							}
							if (fastram_select_ramboard->manual_config && mbt->address) {
								fastram_select_ramboard->start_address = mbt->address;
								fastram_select_ramboard->end_address = mbt->address + fastram_select_ramboard->size - 1;
							}
						} else {
							fastram_select_ramboard->manual_config = false;
						}
						setfastram_selectmenu(hDlg, 0);
					}
					break;
				}
			} else if (HIWORD(wParam) == EN_CHANGE || HIWORD(wParam) == EN_KILLFOCUS) {
				switch (LOWORD(wParam))
				{
					case IDC_AUTOCONFIG_MANUFACTURER:
					case IDC_AUTOCONFIG_PRODUCT:
					if (fastram_select_ramboard) {
						GetDlgItemText(hDlg, IDC_AUTOCONFIG_MANUFACTURER, tmp, sizeof tmp / sizeof(TCHAR));
						fastram_select_ramboard->manufacturer = (uae_u16)_tstol(tmp);
						GetDlgItemText(hDlg, IDC_AUTOCONFIG_PRODUCT, tmp, sizeof tmp / sizeof(TCHAR));
						fastram_select_ramboard->product = (uae_u8)_tstol(tmp);
						setfastram_selectmenu(hDlg, 1);
					}
					break;
					case IDC_RAM_ADDRESS:
					case IDC_RAM_ADDRESS2:
					if (fastram_select_ramboard) {
						TCHAR *endptr;
						GetDlgItemText(hDlg, IDC_RAM_ADDRESS, tmp, sizeof tmp / sizeof(TCHAR));
						fastram_select_ramboard->start_address = _tcstoul(tmp, &endptr, 16);
						GetDlgItemText(hDlg, IDC_RAM_ADDRESS2, tmp, sizeof tmp / sizeof(TCHAR));
						fastram_select_ramboard->end_address = _tcstoul(tmp, &endptr, 16);
						setfastram_selectmenu(hDlg, HIWORD(wParam) == EN_KILLFOCUS ? 0 : 3);
					}
					break;
					case IDC_AUTOCONFIG_DATA:
					if (fastram_select_ramboard && fastram_select_ramboard->autoconfig_inuse) {
						struct ramboard *rb = fastram_select_ramboard;
						GetDlgItemText(hDlg, IDC_AUTOCONFIG_DATA, tmp, sizeof tmp / sizeof(TCHAR));
						memset(rb->autoconfig, 0, sizeof rb->autoconfig);
						for (int i = 0; i < sizeof rb->autoconfig; i++) {
							TCHAR *s2 = &tmp[i * 3];
							if (i + 1 < 12 && s2[2] != '.')
								break;
							TCHAR *endptr;
							tmp[2] = 0;
							rb->autoconfig[i] = (uae_u8)_tcstol(s2, &endptr, 16);
						}
						setfastram_selectmenu(hDlg, 2);
					}
					break;
				}
			}
			recursive--;
		}
		break;

	case WM_HSCROLL:
	{
		recursive++;
		bool change1 = false;
		uae_u32 v;
		v = memsizes[msi_chip[SendMessage (GetDlgItem (hDlg, IDC_CHIPMEM), TBM_GETPOS, 0, 0)]];
		if (v != workprefs.chipmem.size) {
			change1 = true;
			workprefs.chipmem.size = v;
		}
		v = memsizes[msi_bogo[SendMessage (GetDlgItem (hDlg, IDC_SLOWMEM), TBM_GETPOS, 0, 0)]];
		if (v != workprefs.bogomem.size) {
			change1 = true;
			workprefs.bogomem.size = v;
		}
		v = memsizes[msi_fast[SendMessage (GetDlgItem (hDlg, IDC_FASTMEM), TBM_GETPOS, 0, 0)]];
		if (v != workprefs.fastmem[0].size) {
			change1 = true;
			workprefs.fastmem[0].size = v;
			fastram_select = MAX_STANDARD_RAM_BOARDS;
			setfastram_selectmenu(hDlg, 0);
		}
		v = memsizes[msi_z3fast[SendMessage (GetDlgItem (hDlg, IDC_Z3FASTMEM), TBM_GETPOS, 0, 0)]];
		if (v != workprefs.z3fastmem[0].size) {
			change1 = true;
			workprefs.z3fastmem[0].size = v;
			fastram_select = MAX_STANDARD_RAM_BOARDS + MAX_RAM_BOARDS;
			setfastram_selectmenu(hDlg, 0);
		}
		v = memsizes[msi_z3chip[SendMessage (GetDlgItem (hDlg, IDC_Z3CHIPMEM), TBM_GETPOS, 0, 0)]];
		if (v != workprefs.z3chipmem.size) {
			change1 = true;
			workprefs.z3chipmem.size = v;
		}
		if (!change1 && fastram_select_pointer) {
			v = memsizes[fastram_select_msi[SendMessage(GetDlgItem(hDlg, IDC_MEMORYMEM), TBM_GETPOS, 0, 0)]];
			if (*fastram_select_pointer != v) {
				*fastram_select_pointer = v;
				setfastram_selectmenu(hDlg, 0);
				values_to_memorydlg(hDlg);
			}
		}
		if (change1) {
			fix_values_memorydlg ();
			values_to_memorydlg (hDlg);
			setfastram_selectmenu(hDlg, 0);
			enable_for_memorydlg (hDlg);
		}
		recursive--;
		break;
	}

	}
	return FALSE;
}

static int customromselectnum;
static void values_to_kickstartdlg(HWND hDlg)
{
	UAEREG *fkey;

	fkey = regcreatetree(NULL, _T("DetectedROMs"));

	load_keyring(&workprefs, NULL);

	addromfiles(fkey, hDlg, IDC_ROMFILE, workprefs.romfile,
		ROMTYPE_KICK | ROMTYPE_KICKCD32, 0);
	addromfiles(fkey, hDlg, IDC_ROMFILE2, workprefs.romextfile,
		ROMTYPE_EXTCD32 | ROMTYPE_EXTCDTV | ROMTYPE_ARCADIABIOS | ROMTYPE_ALG, 0);
	addromfiles(fkey, hDlg, IDC_CARTFILE, workprefs.cartfile,
		ROMTYPE_FREEZER | ROMTYPE_ARCADIAGAME | ROMTYPE_CD32CART, 0);

	regclosetree(fkey);

	SetDlgItemText(hDlg, IDC_FLASHFILE, workprefs.flashfile);
	SetDlgItemText(hDlg, IDC_RTCFILE, workprefs.rtcfile);
	CheckDlgButton(hDlg, IDC_KICKSHIFTER, workprefs.kickshifter);
	CheckDlgButton(hDlg, IDC_MAPROM, workprefs.maprom);

	if (workprefs.boot_rom == 1) {
		xSendDlgItemMessage(hDlg, IDC_UAEBOARD_TYPE, CB_SETCURSEL, 0, 0);
	}
	else {
		xSendDlgItemMessage(hDlg, IDC_UAEBOARD_TYPE, CB_SETCURSEL, workprefs.uaeboard + 1, 0);
	}
}

static void values_to_kickstartdlg2(HWND hDlg)
{
	int v = xSendDlgItemMessage(hDlg, IDC_CUSTOMROMSELECT, CB_GETCURSEL, 0, 0);
	if (v >= 0 && v < MAX_ROM_BOARDS) {
		customromselectnum = v;
	}
	xSendDlgItemMessage(hDlg, IDC_CUSTOMROMSELECT, CB_RESETCONTENT, 0, 0);
	for (int i = 0; i < MAX_ROM_BOARDS; i++) {
		TCHAR tmp[MAX_DPATH];
		struct romboard *rb = &workprefs.romboards[i];
		_stprintf(tmp, _T("ROM #%d"), i + 1);
		if (rb->size)
			_stprintf(tmp + _tcslen(tmp), _T(" %08x - %08x"), rb->start_address, rb->end_address - 1);
		xSendDlgItemMessage(hDlg, IDC_CUSTOMROMSELECT, CB_ADDSTRING, 0, (LPARAM)tmp);
	}
	if (customromselectnum >= 0 && customromselectnum < MAX_ROM_BOARDS) {
		struct romboard *rb = &workprefs.romboards[customromselectnum];
		TCHAR tmp[100];
		_stprintf(tmp, _T("%08x"), rb->start_address);
		if (!rb->end_address && !rb->start_address) {
			tmp[0] = 0;
		}
		SetDlgItemText(hDlg, IDC_ROM_ADDRESS, tmp);
		_stprintf(tmp, _T("%08x"), rb->end_address);
		if (!rb->end_address && !rb->start_address) {
			tmp[0] = 0;
		}
		SetDlgItemText(hDlg, IDC_ROM_ADDRESS2, tmp);
		SetDlgItemText(hDlg, IDC_CUSTOMROMFILE, rb->lf.loadfile);
		xSendDlgItemMessage(hDlg, IDC_CUSTOMROMSELECT, CB_SETCURSEL, customromselectnum, 0);
	}
}

static void values_from_kickstartdlg(HWND hDlg)
{
	getromfile(hDlg, IDC_ROMFILE, workprefs.romfile, sizeof(workprefs.romfile) / sizeof(TCHAR));
	getromfile(hDlg, IDC_ROMFILE2, workprefs.romextfile, sizeof(workprefs.romextfile) / sizeof(TCHAR));
	getromfile(hDlg, IDC_CARTFILE, workprefs.cartfile, sizeof(workprefs.cartfile) / sizeof(TCHAR));

	read_kickstart_version(&workprefs);
	int v = xSendDlgItemMessage(hDlg, IDC_UAEBOARD_TYPE, CB_GETCURSEL, 0, 0);
	if (v > 0) {
		workprefs.uaeboard = v - 1;
		workprefs.boot_rom = 0;
	} else {
		workprefs.uaeboard = 0;
		workprefs.boot_rom = 1; // disabled
	}
}

static void values_from_kickstartdlg2(HWND hDlg)
{
	int v = xSendDlgItemMessage(hDlg, IDC_CUSTOMROMSELECT, CB_GETCURSEL, 0, 0);
	if (v >= 0 && v < MAX_ROM_BOARDS) {
		struct romboard *rb = &workprefs.romboards[v];
		TCHAR tmp[100];
		TCHAR *endptr;
		GetDlgItemText(hDlg, IDC_ROM_ADDRESS, tmp, sizeof tmp / sizeof(TCHAR));
		rb->start_address =_tcstoul(tmp, &endptr, 16);
		rb->start_address &= ~65535;
		GetDlgItemText(hDlg, IDC_ROM_ADDRESS2, tmp, sizeof tmp / sizeof(TCHAR));
		rb->end_address = _tcstoul(tmp, &endptr, 16);
		rb->end_address = ((rb->end_address - 1) & ~65535) | 0xffff;
		rb->size = 0;
		if (rb->end_address > rb->start_address) {
			rb->size = (rb->end_address - rb->start_address + 65535) & ~65535;
		}
	}
}

static void init_kickstart (HWND hDlg)
{
	ew (hDlg, IDC_MAPROM, workprefs.cpuboard_type == 0);
#if !defined (CDTV) && !defined (CD32)
	ew (hDlg, IDC_FLASHFILE), FALSE);
	ew (hDlg, IDC_ROMFILE2), FALSE);
#endif
#if !defined (ACTION_REPLAY)
	ew (hDlg, IDC_CARTFILE), FALSE);
#endif
#if defined (UAE_MINI)
	ew (hDlg, IDC_KICKSHIFTER), FALSE);
	ew (hDlg, IDC_ROMCHOOSER2), FALSE);
	ew (hDlg, IDC_CARTCHOOSER), FALSE);
	ew (hDlg, IDC_FLASHCHOOSER), FALSE);
#endif

	ew(hDlg, IDC_UAEBOARD_TYPE, full_property_sheet);

	xSendDlgItemMessage(hDlg, IDC_UAEBOARD_TYPE, CB_RESETCONTENT, 0, 0);
	xSendDlgItemMessage(hDlg, IDC_UAEBOARD_TYPE, CB_ADDSTRING, 0, (LPARAM)_T("ROM disabled"));
	xSendDlgItemMessage(hDlg, IDC_UAEBOARD_TYPE, CB_ADDSTRING, 0, (LPARAM)_T("Original UAE (FS + F0 ROM)"));
	xSendDlgItemMessage(hDlg, IDC_UAEBOARD_TYPE, CB_ADDSTRING, 0, (LPARAM)_T("New UAE (64k + F0 ROM)"));
	xSendDlgItemMessage(hDlg, IDC_UAEBOARD_TYPE, CB_ADDSTRING, 0, (LPARAM)_T("New UAE (128k, ROM, Direct)"));
	xSendDlgItemMessage(hDlg, IDC_UAEBOARD_TYPE, CB_ADDSTRING, 0, (LPARAM)_T("New UAE (128k, ROM, Indirect)"));

	if (!regexiststree(NULL, _T("DetectedROMs")))
		scan_roms (NULL, rp_isactive () ? 0 : 1);
}

static void kickstartfilebuttons (HWND hDlg, WPARAM wParam, TCHAR *path)
{
	switch (LOWORD(wParam))
	{
	case IDC_KICKCHOOSER:
		DiskSelection(hDlg, IDC_ROMFILE, 6, &workprefs, NULL, path);
		values_to_kickstartdlg(hDlg);
		break;
	case IDC_ROMCHOOSER2:
		DiskSelection(hDlg, IDC_ROMFILE2, 6, &workprefs, NULL, path);
		values_to_kickstartdlg(hDlg);
		break;
	case IDC_CUSTOMROMCHOOSER:
		DiskSelection(hDlg, IDC_CUSTOMROMFILE, 6, &workprefs, NULL, path);
		values_to_kickstartdlg2(hDlg);
		break;
	case IDC_FLASHCHOOSER:
		DiskSelection(hDlg, IDC_FLASHFILE, 11, &workprefs, NULL, path);
		values_to_kickstartdlg(hDlg);
		break;
	case IDC_RTCCHOOSER:
		DiskSelection(hDlg, IDC_RTCFILE, 19, &workprefs, NULL, path);
		values_to_kickstartdlg(hDlg);
		break;
	case IDC_CARTCHOOSER:
		DiskSelection(hDlg, IDC_CARTFILE, 6, &workprefs, NULL, path);
		values_to_kickstartdlg(hDlg);
		break;
	}
}

static INT_PTR CALLBACK KickstartDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive;
	TCHAR tmp[MAX_DPATH];

	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch (msg)
	{
	case WM_INITDIALOG:
		{
			int ids[] = { IDC_ROMFILE, IDC_ROMFILE2, IDC_CARTFILE, -1 };
			pages[KICKSTART_ID] = hDlg;
			currentpage = KICKSTART_ID;
			init_kickstart (hDlg);
			values_to_kickstartdlg(hDlg);
			values_to_kickstartdlg2(hDlg);
			setmultiautocomplete (hDlg, ids);
			setac (hDlg, IDC_FLASHFILE);
			setac (hDlg, IDC_RTCFILE);
			return TRUE;
		}

	case WM_CONTEXTMENU:
		{
			int id = GetDlgCtrlID((HWND)wParam);
			if (id == IDC_KICKCHOOSER || id == IDC_ROMCHOOSER2
				|| id == IDC_FLASHCHOOSER || id == IDC_CARTCHOOSER || id == IDC_RTCCHOOSER) {
					TCHAR *s = favoritepopup (hDlg);
					if (s) {
						TCHAR newfile[MAX_DPATH];
						_tcscpy (newfile, s);
						kickstartfilebuttons (hDlg, id, newfile);
						xfree (s);
					}
			}
			break;
		}

	case WM_COMMAND:
		if (recursive > 0)
			break;
		recursive++;
		if (HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == CBN_KILLFOCUS) {
			switch (LOWORD(wParam))
			{
			case IDC_ROMFILE:
			case IDC_ROMFILE2:
			case IDC_CARTFILE:
			case IDC_UAEBOARD_TYPE:
				values_from_kickstartdlg(hDlg);
				break;
			case IDC_CUSTOMROMSELECT:
				values_to_kickstartdlg2(hDlg);
				break;
			}
		} else if (HIWORD(wParam) == EN_CHANGE || HIWORD(wParam) == EN_KILLFOCUS) {
			switch (LOWORD(wParam))
			{
			case IDC_ROM_ADDRESS:
			case IDC_ROM_ADDRESS2:
				values_from_kickstartdlg2(hDlg);
				break;
			}
		}
		kickstartfilebuttons (hDlg, wParam, NULL);
		switch (LOWORD (wParam))
		{
		case IDC_FLASHFILE:
			GetWindowText (GetDlgItem (hDlg, IDC_FLASHFILE), tmp, sizeof (tmp) / sizeof (TCHAR));
			_tcscpy (workprefs.flashfile, tmp);
			break;
		case IDC_RTCFILE:
			GetWindowText (GetDlgItem (hDlg, IDC_RTCFILE), tmp, sizeof (tmp) / sizeof (TCHAR));
			_tcscpy (workprefs.rtcfile, tmp);
			break;

		case IDC_KICKSHIFTER:
			workprefs.kickshifter = ischecked (hDlg, IDC_KICKSHIFTER);
			break;

		case IDC_MAPROM:
			workprefs.maprom = ischecked (hDlg, IDC_MAPROM) ? 0x0f000000 : 0;
			break;
		}
		recursive--;
		break;
	}
	return FALSE;
}

static void enable_for_miscdlg (HWND hDlg)
{
	if (!full_property_sheet) {
		ew (hDlg, IDC_NOSPEED, TRUE);
		ew (hDlg, IDC_NOSPEEDPAUSE, TRUE);
		ew (hDlg, IDC_NOSOUND, TRUE);
		ew (hDlg, IDC_DOSAVESTATE, TRUE);
		ew (hDlg, IDC_SCSIMODE, FALSE);
	} else {
#if !defined (SCSIEMU)
		EnableWindow (GetDlgItem(hDlg, IDC_SCSIMODE), TRUE);
#endif
		ew (hDlg, IDC_DOSAVESTATE, FALSE);
	}

	ew (hDlg, IDC_ASSOCIATELIST, !rp_isactive ());
	ew (hDlg, IDC_ASSOCIATE_ON, !rp_isactive ());
	ew (hDlg, IDC_ASSOCIATE_OFF, !rp_isactive ());
	ew (hDlg, IDC_DXMODE_OPTIONS, workprefs.gfx_api == 2);

	bool paused = false;
	bool nosound = false;
	bool activenojoy = (workprefs.win32_active_input & 4) == 0;
	bool activenokeyboard = (workprefs.win32_active_input & 1) == 0;
	bool nojoy = (workprefs.win32_inactive_input & 4) == 0;
	ew(hDlg, IDC_ACTIVE_PAUSE, paused == false);
	ew(hDlg, IDC_ACTIVE_NOSOUND, nosound == false && paused == false);
	if (!paused) {
		paused = workprefs.win32_active_nocapture_pause;
		if (!nosound)
			nosound = workprefs.win32_active_nocapture_nosound;
		else
			workprefs.win32_active_nocapture_nosound = true;
	} else {
		workprefs.win32_active_nocapture_pause = workprefs.win32_active_nocapture_nosound = true;
		nosound = true;
		nojoy = true;
		workprefs.win32_active_input = 0;
	}
	ew(hDlg, IDC_ACTIVE_NOJOY, paused == false);
	ew(hDlg, IDC_ACTIVE_NOKEYBOARD, paused == false);
	if (paused)
		CheckDlgButton (hDlg, IDC_INACTIVE_PAUSE, TRUE);
	if (nosound || paused)
		CheckDlgButton(hDlg, IDC_INACTIVE_NOSOUND, TRUE);
	if (paused || nojoy)
		CheckDlgButton(hDlg, IDC_INACTIVE_NOJOY, TRUE);
	if (paused || activenojoy)
		CheckDlgButton(hDlg, IDC_ACTIVE_NOJOY, TRUE);
	if (paused || activenokeyboard)
		CheckDlgButton(hDlg, IDC_ACTIVE_NOKEYBOARD, TRUE);
	ew(hDlg, IDC_INACTIVE_PAUSE, paused == false);
	ew(hDlg, IDC_INACTIVE_NOSOUND, nosound == false && paused == false);
	ew(hDlg, IDC_INACTIVE_NOJOY, paused == false);
	if (!paused) {
		paused = workprefs.win32_inactive_pause;
		if (!nosound)
			nosound = workprefs.win32_inactive_nosound;
		else
			workprefs.win32_inactive_nosound = true;
		if (!nojoy)
			nojoy = (workprefs.win32_inactive_input & 4) == 0;
		else
			workprefs.win32_inactive_input = 0;
	} else {
		workprefs.win32_inactive_pause = workprefs.win32_inactive_nosound = true;
		workprefs.win32_inactive_input = workprefs.win32_inactive_input = 0;
		nosound = true;
		nojoy = true;
	}
	if (paused)
		CheckDlgButton (hDlg, IDC_MINIMIZED_PAUSE, TRUE);
	if (nosound || paused)
		CheckDlgButton (hDlg, IDC_MINIMIZED_NOSOUND, TRUE);
	if (paused || nojoy)
		CheckDlgButton(hDlg, IDC_MINIMIZED_NOJOY, TRUE);
	ew(hDlg, IDC_MINIMIZED_PAUSE, paused == false);
	ew(hDlg, IDC_MINIMIZED_NOSOUND, nosound == false && paused == false);
	ew(hDlg, IDC_MINIMIZED_NOJOY, nojoy == false && paused == false);
	if (!paused) {
		paused = workprefs.win32_iconified_pause;
		if (!nosound)
			nosound = workprefs.win32_iconified_nosound;
		else
			workprefs.win32_iconified_nosound = true;
	} else {
		workprefs.win32_iconified_pause = workprefs.win32_iconified_nosound = true;
		workprefs.win32_iconified_input = workprefs.win32_iconified_input = 0;
		nosound = true;
		nojoy = true;
	}
}

static void misc_kbled (HWND hDlg, int v, int nv)
{
	const TCHAR *defname = v == IDC_KBLED1 ? _T("(NumLock)") : v == IDC_KBLED2 ? _T("(CapsLock)") : _T("(ScrollLock)");
	xSendDlgItemMessage (hDlg, v, CB_RESETCONTENT, 0, 0L);
	xSendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)defname);
	xSendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)_T("POWER"));
	xSendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)_T("DF0"));
	xSendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)_T("DF1"));
	xSendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)_T("DF2"));
	xSendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)_T("DF3"));
	xSendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)_T("HD"));
	xSendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)_T("CD"));
	xSendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)_T("DF*"));
	xSendDlgItemMessage (hDlg, v, CB_SETCURSEL, nv, 0);
}

static void misc_getkbled (HWND hDlg, int v, int n)
{
	int nv = xSendDlgItemMessage (hDlg, v, CB_GETCURSEL, 0, 0L);
	if (nv != CB_ERR) {
		workprefs.keyboard_leds[n] = nv;
		misc_kbled (hDlg, v, nv);
	}
	workprefs.keyboard_leds_in_use = (workprefs.keyboard_leds[0] | workprefs.keyboard_leds[1] | workprefs.keyboard_leds[2]) != 0;
}

static void misc_getpri (HWND hDlg, int v, int *n)
{
	int nv = xSendDlgItemMessage (hDlg, v, CB_GETCURSEL, 0, 0L);
	if (nv != CB_ERR)
		*n = nv;
}

static void misc_addpri (HWND hDlg, int v, int pri)
{
	int i;

	DWORD opri = GetPriorityClass (GetCurrentProcess ());
	ew (hDlg, v, !(opri != IDLE_PRIORITY_CLASS && opri != NORMAL_PRIORITY_CLASS && opri != BELOW_NORMAL_PRIORITY_CLASS && opri != ABOVE_NORMAL_PRIORITY_CLASS));
	xSendDlgItemMessage (hDlg, v, CB_RESETCONTENT, 0, 0L);
	i = 0;
	while (priorities[i].name) {
		xSendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)priorities[i].name);
		i++;
	}
	xSendDlgItemMessage (hDlg, v, CB_SETCURSEL, pri, 0);
}

static void misc_scsi (HWND hDlg)
{
	TCHAR tmp[MAX_DPATH];

	xSendDlgItemMessage (hDlg, IDC_SCSIMODE, CB_RESETCONTENT, 0, 0);
	WIN32GUI_LoadUIString (IDS_SCSI_EMULATION, tmp, sizeof tmp / sizeof (TCHAR));
	xSendDlgItemMessage (hDlg, IDC_SCSIMODE, CB_ADDSTRING, 0, (LPARAM)tmp);
	xSendDlgItemMessage (hDlg, IDC_SCSIMODE, CB_ADDSTRING, 0, (LPARAM)_T("SPTI"));
	xSendDlgItemMessage (hDlg, IDC_SCSIMODE, CB_ADDSTRING, 0, (LPARAM)_T("SPTI + SCSI SCAN"));
	xSendDlgItemMessage (hDlg, IDC_SCSIMODE, CB_SETCURSEL, workprefs.win32_uaescsimode, 0);
}

static void misc_lang (HWND hDlg)
{
	int i, idx = 0, cnt = 0, lid;
	WORD langid = -1;
	TCHAR tmp[MAX_DPATH];

	if (regqueryint (NULL, _T("Language"), &lid))
		langid = (WORD)lid;
	WIN32GUI_LoadUIString (IDS_AUTODETECT, tmp, sizeof tmp / sizeof (TCHAR));
	xSendDlgItemMessage (hDlg, IDC_LANGUAGE, CB_RESETCONTENT, 0, 0);
	xSendDlgItemMessage (hDlg, IDC_LANGUAGE, CB_ADDSTRING, 0, (LPARAM)tmp);
	xSendDlgItemMessage (hDlg, IDC_LANGUAGE, CB_ADDSTRING, 0, (LPARAM)_T("English (built-in)"));
	if (langid == 0)
		idx = 1;
	cnt = 2;
	for (i = 0; langs[i].name; i++) {
		HMODULE hm = language_load (langs[i].id);
		if (hm) {
			FreeLibrary (hm);
			xSendDlgItemMessage (hDlg, IDC_LANGUAGE, CB_ADDSTRING, 0, (LPARAM)langs[i].name);
			if (langs[i].id == langid)
				idx = cnt;
			cnt++;
		}
	}
	xSendDlgItemMessage (hDlg, IDC_LANGUAGE, CB_SETCURSEL, idx, 0);

	xSendDlgItemMessage (hDlg, IDC_GUI_SIZE, CB_RESETCONTENT, 0, 0);
	WIN32GUI_LoadUIString (IDS_SELECT_MENU, tmp, sizeof tmp / sizeof (TCHAR));
	xSendDlgItemMessage (hDlg, IDC_GUI_SIZE, CB_ADDSTRING, 0, (LPARAM)tmp);
	xSendDlgItemMessage (hDlg, IDC_GUI_SIZE, CB_ADDSTRING, 0, (LPARAM)_T("200%"));
	xSendDlgItemMessage (hDlg, IDC_GUI_SIZE, CB_ADDSTRING, 0, (LPARAM)_T("190%"));
	xSendDlgItemMessage (hDlg, IDC_GUI_SIZE, CB_ADDSTRING, 0, (LPARAM)_T("180%"));
	xSendDlgItemMessage (hDlg, IDC_GUI_SIZE, CB_ADDSTRING, 0, (LPARAM)_T("170%"));
	xSendDlgItemMessage (hDlg, IDC_GUI_SIZE, CB_ADDSTRING, 0, (LPARAM)_T("160%"));
	xSendDlgItemMessage (hDlg, IDC_GUI_SIZE, CB_ADDSTRING, 0, (LPARAM)_T("150%"));
	xSendDlgItemMessage (hDlg, IDC_GUI_SIZE, CB_ADDSTRING, 0, (LPARAM)_T("140%"));
	xSendDlgItemMessage (hDlg, IDC_GUI_SIZE, CB_ADDSTRING, 0, (LPARAM)_T("130%"));
	xSendDlgItemMessage (hDlg, IDC_GUI_SIZE, CB_ADDSTRING, 0, (LPARAM)_T("120%"));
	xSendDlgItemMessage (hDlg, IDC_GUI_SIZE, CB_ADDSTRING, 0, (LPARAM)_T("110%"));
	xSendDlgItemMessage (hDlg, IDC_GUI_SIZE, CB_ADDSTRING, 0, (LPARAM)_T("100%"));
	xSendDlgItemMessage (hDlg, IDC_GUI_SIZE, CB_ADDSTRING, 0, (LPARAM)_T(" 90%"));
	xSendDlgItemMessage (hDlg, IDC_GUI_SIZE, CB_ADDSTRING, 0, (LPARAM)_T(" 80%"));
	xSendDlgItemMessage (hDlg, IDC_GUI_SIZE, CB_ADDSTRING, 0, (LPARAM)_T(" 70%"));
	xSendDlgItemMessage (hDlg, IDC_GUI_SIZE, CB_ADDSTRING, 0, (LPARAM)_T(" 60%"));
	xSendDlgItemMessage (hDlg, IDC_GUI_SIZE, CB_SETCURSEL, 0, 0);
}

static void misc_setlang (int v)
{
	int i;
	WORD langid = 0;
	v-=2;
	if (v >= 0) {
		for (i = 0; langs[i].name; i++) {
			HMODULE hm = language_load (langs[i].id);
			if (hm) {
				FreeLibrary(hm);
				if (v == 0) {
					langid = langs[i].id;
					break;
				}
				v--;
			}
		}
	}
	if (v == -2)
		langid = -1;
	regsetint (NULL, _T("Language"), langid);
	FreeLibrary(hUIDLL);
	hUIDLL = NULL;
	if (langid >= 0)
		hUIDLL = language_load(langid);
	restart_requested = 1;
	exit_gui(0);
}

static void misc_gui_font(HWND hDlg, int fonttype)
{
	if (scaleresource_choosefont(hDlg, fonttype)) {
		if (fonttype == 0) {
			gui_size_changed = 10;
		} else if (fonttype == 2) {
			if (!full_property_sheet && AMonitors[0].hAmigaWnd) {
				createstatusline(AMonitors[0].hAmigaWnd, 0);
				target_graphics_buffer_update(0, true);

			}
		}
	}
}

static void values_to_miscdlg_dx(HWND hDlg)
{
	xSendDlgItemMessage(hDlg, IDC_DXMODE_OPTIONS, CB_RESETCONTENT, 0, 0);
	if (workprefs.gfx_api >= 2) {
		xSendDlgItemMessage(hDlg, IDC_DXMODE_OPTIONS, CB_ADDSTRING, 0, (LPARAM)_T("Hardware D3D11"));
		xSendDlgItemMessage(hDlg, IDC_DXMODE_OPTIONS, CB_ADDSTRING, 0, (LPARAM)_T("Software D3D11"));
		xSendDlgItemMessage(hDlg, IDC_DXMODE_OPTIONS, CB_SETCURSEL, workprefs.gfx_api_options, 0);
	}
}

static void values_to_miscdlg (HWND hDlg)
{
	TCHAR tmp[MAX_DPATH];

	if (currentpage == MISC1_ID) {

		misc_kbled(hDlg, IDC_KBLED1, workprefs.keyboard_leds[0]);
		misc_kbled(hDlg, IDC_KBLED2, workprefs.keyboard_leds[1]);
		misc_kbled(hDlg, IDC_KBLED3, workprefs.keyboard_leds[2]);
		CheckDlgButton(hDlg, IDC_KBLED_USB, workprefs.win32_kbledmode);
		CheckDlgButton(hDlg, IDC_GUI_RESIZE, gui_resize_enabled);
		CheckDlgButton(hDlg, IDC_GUI_FULLSCREEN, gui_fullscreen > 0);
		ew(hDlg, IDC_GUI_DARKMODE, os_win10 && !rp_isactive() && !darkModeForced);
		if (!os_win10) {
			gui_darkmode = 0;
		}
		if (darkModeForced > 0 || gui_darkmode > 0) {
			CheckDlgButton(hDlg, IDC_GUI_DARKMODE, 1);
		} else if (darkModeForced < 0 || gui_darkmode == 0) {
			CheckDlgButton(hDlg, IDC_GUI_DARKMODE, 0);
		} else {
			CheckDlgButton(hDlg, IDC_GUI_DARKMODE, BST_INDETERMINATE);
		}
		ew(hDlg, IDC_GUI_RESIZE, gui_resize_allowed);

		misc_scsi (hDlg);
		misc_lang (hDlg);

		xSendDlgItemMessage(hDlg, IDC_DXMODE, CB_RESETCONTENT, 0, 0);
		xSendDlgItemMessage(hDlg, IDC_DXMODE, CB_ADDSTRING, 0, (LPARAM)_T("GDI"));
		xSendDlgItemMessage(hDlg, IDC_DXMODE, CB_ADDSTRING, 0, (LPARAM)_T("Direct3D 9"));
		xSendDlgItemMessage(hDlg, IDC_DXMODE, CB_ADDSTRING, 0, (LPARAM)_T("Direct3D 11"));
		xSendDlgItemMessage(hDlg, IDC_DXMODE, CB_ADDSTRING, 0, (LPARAM)_T("Direct3D 11 HDR (experimental)"));
		xSendDlgItemMessage(hDlg, IDC_DXMODE, CB_SETCURSEL, workprefs.gfx_api, 0);
		values_to_miscdlg_dx(hDlg);

		xSendDlgItemMessage (hDlg, IDC_WINDOWEDMODE, CB_RESETCONTENT, 0, 0);

		WIN32GUI_LoadUIString (IDS_WSTYLE_BORDERLESS, tmp, sizeof tmp / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_WINDOWEDMODE, CB_ADDSTRING, 0, (LPARAM)tmp);
		WIN32GUI_LoadUIString (IDS_WSTYLE_MINIMAL, tmp, sizeof tmp / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_WINDOWEDMODE, CB_ADDSTRING, 0, (LPARAM)tmp);
		WIN32GUI_LoadUIString (IDS_WSTYLE_STANDARD, tmp, sizeof tmp / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_WINDOWEDMODE, CB_ADDSTRING, 0, (LPARAM)tmp);
		WIN32GUI_LoadUIString (IDS_WSTYLE_EXTENDED, tmp, sizeof tmp / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_WINDOWEDMODE, CB_ADDSTRING, 0, (LPARAM)tmp);
		xSendDlgItemMessage (hDlg, IDC_WINDOWEDMODE, CB_SETCURSEL,
			workprefs.win32_borderless ? 0 : (workprefs.win32_statusbar + 1),
			0);

	} else if (currentpage == MISC2_ID) {

		CheckDlgButton(hDlg, IDC_ACTIVE_PAUSE, workprefs.win32_active_nocapture_pause);
		CheckDlgButton(hDlg, IDC_ACTIVE_NOSOUND, workprefs.win32_active_nocapture_nosound || workprefs.win32_active_nocapture_pause);
		CheckDlgButton(hDlg, IDC_ACTIVE_NOJOY, (workprefs.win32_active_input & 4) == 0 || workprefs.win32_active_nocapture_pause);
		CheckDlgButton(hDlg, IDC_ACTIVE_NOKEYBOARD, (workprefs.win32_active_input & 1) == 0 || workprefs.win32_active_nocapture_pause);
		CheckDlgButton(hDlg, IDC_INACTIVE_PAUSE, workprefs.win32_inactive_pause);
		CheckDlgButton(hDlg, IDC_INACTIVE_NOSOUND, workprefs.win32_inactive_nosound || workprefs.win32_inactive_pause);
		CheckDlgButton(hDlg, IDC_INACTIVE_NOJOY, (workprefs.win32_inactive_input & 4) == 0 || workprefs.win32_inactive_pause);
		CheckDlgButton(hDlg, IDC_MINIMIZED_PAUSE, workprefs.win32_iconified_pause);
		CheckDlgButton(hDlg, IDC_MINIMIZED_NOSOUND, workprefs.win32_iconified_nosound || workprefs.win32_iconified_pause);
		CheckDlgButton(hDlg, IDC_MINIMIZED_NOJOY, (workprefs.win32_iconified_input & 4) == 0 || workprefs.win32_iconified_pause);
		misc_addpri(hDlg, IDC_ACTIVE_PRIORITY, workprefs.win32_active_capture_priority);
		misc_addpri(hDlg, IDC_INACTIVE_PRIORITY, workprefs.win32_inactive_priority);
		misc_addpri(hDlg, IDC_MINIMIZED_PRIORITY, workprefs.win32_iconified_priority);

	}
}

static void addstatefilename(HWND hDlg)
{
	DISK_history_add(workprefs.statefile, -1, HISTORY_STATEFILE, 0);
	addhistorymenu(hDlg, workprefs.statefile, IDC_STATENAME, HISTORY_STATEFILE, true, -1);
	ew(hDlg, IDC_STATECLEAR, workprefs.statefile[0] != 0);
	setchecked(hDlg, IDC_STATECLEAR, workprefs.statefile[0] != 0);
}

static void getguidefaultsize(int *wp, int *hp)
{
	int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
	int dpi = getdpiformonitor(NULL);

	int ww = MulDiv(1600, dpi, 96);
	int wh = MulDiv(1024, dpi, 96);

	if (w >= ww && h >= wh) {
		*wp = GUI_INTERNAL_WIDTH_NEW;
		*hp = GUI_INTERNAL_HEIGHT_NEW;
	} else {
		*wp = GUI_INTERNAL_WIDTH_OLD;
		*hp = GUI_INTERNAL_HEIGHT_OLD;
	}
}

static void setdefaultguisize(int skipdpi)
{
	int dpi = skipdpi ? 96 : getdpiformonitor(NULL);
	int w, h;

	int dw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	int dh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

	getguidefaultsize(&w, &h);

	gui_width = MulDiv(w, dpi, 96);
	gui_height = MulDiv(h, dpi, 96);

	if ((dpi > 96) && (gui_width >= dw * 9 / 10 || gui_height > dh * 9 / 10)) {
		gui_width = w;
		gui_height = h;
	}
}

static void saveguisize(void)
{
	if (gui_fullscreen)
		return;
	if (full_property_sheet || isfullscreen() == 0) {
		regsetint(NULL, _T("GUISizeX"), gui_width);
		regsetint(NULL, _T("GUISizeY"), gui_height);
	} else if (isfullscreen() < 0) {
		regsetint(NULL, _T("GUISizeFWX"), gui_width);
		regsetint(NULL, _T("GUISizeFWY"), gui_height);
	} else if (isfullscreen() > 0) {
		regsetint(NULL, _T("GUISizeFSX"), gui_width);
		regsetint(NULL, _T("GUISizeFSY"), gui_height);
	}
}

static void getstoredguisize(void)
{
	if (full_property_sheet || isfullscreen () == 0) {
		regqueryint(NULL, _T("GUISizeX"), &gui_width);
		regqueryint(NULL, _T("GUISizeY"), &gui_height);
		scaleresource_init(gui_fullscreen ? _T("GFS") : _T(""), gui_fullscreen);
	} else if (isfullscreen () < 0) {
		regqueryint(NULL, _T("GUISizeFWX"), &gui_width);
		regqueryint(NULL, _T("GUISizeFWY"), &gui_height);
		scaleresource_init(gui_fullscreen ? _T("FW_GFS") : _T("FW"), gui_fullscreen);
	} else if (isfullscreen () > 0) {
		regqueryint(NULL, _T("GUISizeFSX"), &gui_width);
		regqueryint(NULL, _T("GUISizeFSY"), &gui_height);
		scaleresource_init(gui_fullscreen ? _T("FS_GFS") : _T("FS"), gui_fullscreen);
	}
}

static INT_PTR MiscDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	int v, i;
	static int recursive;
	TCHAR tmp[MAX_DPATH];

	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	if (recursive)
		return FALSE;
	recursive++;

	switch (msg)
	{
	case WM_INITDIALOG:
		pages[currentpage] = hDlg;
		InitializeListView (hDlg);
		values_to_miscdlg (hDlg);
		enable_for_miscdlg (hDlg);
		addstatefilename(hDlg);
		recursive--;
		return TRUE;

	case WM_USER:
		values_to_miscdlg (hDlg);
		enable_for_miscdlg (hDlg);
		recursive--;
		return TRUE;

	case WM_CONTEXTMENU:
		if (GetDlgCtrlID((HWND)wParam) == IDC_DOSAVESTATE) {
			TCHAR *s = favoritepopup (hDlg);
			if (s) {
				TCHAR path[MAX_DPATH];
				_tcscpy (path, s);
				xfree (s);
				if (DiskSelection(hDlg, wParam, 9, &workprefs, NULL, path))
					save_state (savestate_fname, _T("Description!"));
			}
		} else if (GetDlgCtrlID((HWND)wParam) == IDC_DOLOADSTATE) {
			TCHAR *s = favoritepopup (hDlg);
			if (s) {
				TCHAR path[MAX_DPATH];
				_tcscpy (path, s);
				xfree (s);
				if (DiskSelection(hDlg, wParam, 10, &workprefs, NULL, path))
					savestate_state = STATE_DORESTORE;
			}
		}
		break;

	case WM_NOTIFY:
		if (((LPNMHDR) lParam)->idFrom == IDC_ASSOCIATELIST) {
			int entry, col;
			HWND list;
			NM_LISTVIEW *nmlistview = (NM_LISTVIEW *)lParam;
			list = nmlistview->hdr.hwndFrom;
			if (nmlistview->hdr.code == NM_DBLCLK) {
				entry = listview_entry_from_click (list, &col, false);
				exts[entry].enabled = exts[entry].enabled ? 0 : 1;
				associate_file_extensions ();
				InitializeListView (hDlg);
			}
		} else if (((LPNMHDR) lParam)->idFrom == IDC_MISCLIST) {
			NM_LISTVIEW *nmlistview = (NM_LISTVIEW *)lParam;
			if (nmlistview->hdr.code == LVN_ITEMCHANGED) {
				int item = nmlistview->iItem;
				if (item >= 0) {
					const struct miscentry *me = &misclist[item];
					bool checked = (nmlistview->uNewState & LVIS_STATEIMAGEMASK) == 0x2000;
					if (me->b) {
						*me->b = checked;
					} else if (me->i) {
						*me->i &= ~me->imask;
						if (checked)
							*me->i |= me->ival & me->imask;
					}
					workprefs.input_mouse_untrap &= ~MOUSEUNTRAP_MIDDLEBUTTON;
					if (win32_middle_mouse_obsolete)
						workprefs.input_mouse_untrap |= MOUSEUNTRAP_MIDDLEBUTTON;
				}
			}
		}
		break;

	case WM_COMMAND:
		if (currentpage == MISC1_ID) {
			if (HIWORD (wParam) == CBN_SELENDOK || HIWORD (wParam) == CBN_KILLFOCUS || HIWORD (wParam) == CBN_EDITCHANGE)  {
				switch (LOWORD (wParam))
				{
				case IDC_STATENAME:
					if (HIWORD(wParam) != CBN_EDITCHANGE && getcomboboxtext(hDlg, IDC_STATENAME, tmp, sizeof tmp / sizeof(TCHAR))) {
						if (tmp[0]) {
							parsefilepath(tmp, sizeof tmp / sizeof(TCHAR));
							if (_tcscmp(tmp, savestate_fname)) {
								_tcscpy(savestate_fname, tmp);
								savestate_state = STATE_DORESTORE;
								if (!my_existsfile(savestate_fname)) {
									TCHAR t[MAX_DPATH];
									_tcscpy(t, savestate_fname);
									_tcscat(savestate_fname, _T(".uss"));
									if (!my_existsfile(savestate_fname)) {
										fetch_statefilepath(savestate_fname, sizeof(t) / sizeof(MAX_DPATH));
										_tcscat(savestate_fname, t);
										if (!my_existsfile(savestate_fname)) {
											_tcscat(savestate_fname, _T(".uss"));
											if (!my_existsfile(savestate_fname)) {
												_tcscpy(savestate_fname, t);
											}
										}
									}
								}
								_tcscpy(workprefs.statefile, savestate_fname);
								addstatefilename(hDlg);
							}
						}
					}
					break;
				case IDC_KBLED1:
					misc_getkbled (hDlg, IDC_KBLED1, 0);
					break;
				case IDC_KBLED2:
					misc_getkbled (hDlg, IDC_KBLED2, 1);
					break;
				case IDC_KBLED3:
					misc_getkbled (hDlg, IDC_KBLED3, 2);
					break;
				case IDC_LANGUAGE:
					if (HIWORD (wParam) == CBN_SELENDOK) {
						v = xSendDlgItemMessage (hDlg, IDC_LANGUAGE, CB_GETCURSEL, 0, 0L);
						if (v != CB_ERR)
							misc_setlang (v);
					}
					break;
				case IDC_DXMODE:
					v = xSendDlgItemMessage (hDlg, IDC_DXMODE, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR) {
						workprefs.gfx_api = v;
						if (full_property_sheet)
							d3d_select(&workprefs);
						enable_for_miscdlg (hDlg);
						values_to_miscdlg_dx(hDlg);
					}
				break;
				case IDC_WINDOWEDMODE:
					v = xSendDlgItemMessage (hDlg, IDC_WINDOWEDMODE, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR) {
						workprefs.win32_borderless = 0;
						workprefs.win32_statusbar = 0;
						if (v == 0)
							workprefs.win32_borderless = 1;
						if (v > 0)
							workprefs.win32_statusbar = v - 1;
					}
				break;
				case IDC_DXMODE_OPTIONS:
					v = xSendDlgItemMessage (hDlg, IDC_DXMODE_OPTIONS, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR) {
						if (workprefs.gfx_api >= 2) {
							workprefs.gfx_api_options = v;
						}
					}
					break;
				case IDC_SCSIMODE:
					v = xSendDlgItemMessage (hDlg, IDC_SCSIMODE, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR)
						workprefs.win32_uaescsimode = v;
					break;
				}
			}
		} else if (currentpage == MISC2_ID) {
			misc_getpri (hDlg, IDC_ACTIVE_PRIORITY, &workprefs.win32_active_capture_priority);
			misc_getpri (hDlg, IDC_INACTIVE_PRIORITY, &workprefs.win32_inactive_priority);
			misc_getpri (hDlg, IDC_MINIMIZED_PRIORITY, &workprefs.win32_iconified_priority);
		}

		switch(wParam)
		{
		case IDC_GUI_LVDEFAULT:
			ResetListViews();
			break;
		case IDC_GUI_DEFAULT:
			scaleresource_setdefaults(hDlg);
			v = xSendDlgItemMessage (hDlg, IDC_GUI_SIZE, CB_GETCURSEL, 0, 0L);
			if (v != CB_ERR) {
				if (v == 0) {
					v = GUI_SCALE_DEFAULT;
				} else {
					v--;
					v = 200 - v * 10;
				}
				int w, h;
				getguidefaultsize(&w, &h);
				gui_width = (int)(w * v / 100);
				gui_height = (int)(h * v / 100);
				int dpi = getdpiforwindow(hDlg);
				gui_width = MulDiv(gui_width, dpi, 96);
				gui_height = MulDiv(gui_height, dpi, 96);
				if (gui_width < MIN_GUI_INTERNAL_WIDTH || gui_height < MIN_GUI_INTERNAL_HEIGHT) {
					gui_width = MIN_GUI_INTERNAL_WIDTH;
					gui_height = MIN_GUI_INTERNAL_HEIGHT;
				}
				scaleresource_setsize(gui_width, gui_height, gui_fullscreen);
				gui_size_changed = 10;
			}
			break;
		case IDC_GUI_FONT:
			misc_gui_font(hDlg, 0);
			break;
		case IDC_OSD_FONT:
			misc_gui_font(hDlg, 2);
			break;
		case IDC_GUI_RESIZE:
			gui_resize_enabled = ischecked (hDlg, IDC_GUI_RESIZE);
			gui_fullscreen = -1;
			gui_size_changed = 10;
		break;
		case IDC_GUI_FULLSCREEN:
			gui_fullscreen = ischecked(hDlg, IDC_GUI_FULLSCREEN);
			if (!gui_fullscreen) {
				gui_fullscreen = -1;
				getstoredguisize();
			}
			gui_size_changed = 10;
			break;
		case IDC_GUI_DARKMODE:
			{
				int v = IsDlgButtonChecked(hDlg, IDC_GUI_DARKMODE);
				if (!rp_isactive() && !darkModeForced) {
					if (v == BST_INDETERMINATE) {
						gui_darkmode = -1;
					} else if (v) {
						gui_darkmode = 1;
					} else {
						gui_darkmode = 0;
					}
					regsetint(NULL, _T("GUIDarkMode"), gui_darkmode);
					gui_size_changed = 10;
					if (!full_property_sheet) {
						WIN32GFX_DisplayChangeRequested(4);
					}
				}
			}
			break;
		case IDC_ASSOCIATE_ON:
			for (i = 0; exts[i].ext; i++)
				exts[i].enabled = 1;
			associate_file_extensions ();
			InitializeListView (hDlg);
			break;
		case IDC_ASSOCIATE_OFF:
			for (i = 0; exts[i].ext; i++)
				exts[i].enabled = 0;
			associate_file_extensions ();
			InitializeListView (hDlg);
			break;
		case IDC_STATECLEAR:
			savestate_initsave (NULL, 0, 0, false);
			_tcscpy (workprefs.statefile, savestate_fname);
			addstatefilename (hDlg);
			break;
		case IDC_DOSAVESTATE:
			workprefs.statefile[0] = 0;
			if (DiskSelection(hDlg, wParam, 9, &workprefs, NULL, NULL)) {
				save_state (savestate_fname, _T("Description!"));
				_tcscpy (workprefs.statefile, savestate_fname);
			}
			addstatefilename (hDlg);
			break;
		case IDC_DOLOADSTATE:
			if (DiskSelection(hDlg, wParam, 10, &workprefs, NULL, NULL)) {
				savestate_state = STATE_DORESTORE;
				fullpath(savestate_fname, sizeof(savestate_fname) / sizeof(TCHAR));
				_tcscpy (workprefs.statefile, savestate_fname);
				addstatefilename (hDlg);
			}
			break;
		case IDC_INACTIVE_NOJOY:
			if (!ischecked(hDlg, IDC_INACTIVE_NOJOY))
				CheckDlgButton(hDlg, IDC_INACTIVE_PAUSE, BST_UNCHECKED);
		case IDC_INACTIVE_NOSOUND:
			if (!ischecked(hDlg, IDC_INACTIVE_NOSOUND))
				CheckDlgButton(hDlg, IDC_INACTIVE_PAUSE, BST_UNCHECKED);
		case IDC_INACTIVE_PAUSE:
			workprefs.win32_inactive_pause = ischecked (hDlg, IDC_INACTIVE_PAUSE);
			if (workprefs.win32_inactive_pause) {
				CheckDlgButton(hDlg, IDC_INACTIVE_NOSOUND, BST_CHECKED);
				CheckDlgButton(hDlg, IDC_INACTIVE_NOJOY, BST_CHECKED);
			}
			workprefs.win32_inactive_nosound = ischecked(hDlg, IDC_INACTIVE_NOSOUND);
			workprefs.win32_inactive_input = ischecked(hDlg, IDC_INACTIVE_NOJOY) ? 0 : 4;
			enable_for_miscdlg(hDlg);
			break;
		case IDC_ACTIVE_NOJOY:
			if (!ischecked(hDlg, IDC_ACTIVE_NOJOY))
				CheckDlgButton(hDlg, IDC_ACTIVE_NOJOY, BST_UNCHECKED);
		case IDC_ACTIVE_NOKEYBOARD:
			if (!ischecked(hDlg, IDC_ACTIVE_NOKEYBOARD))
				CheckDlgButton(hDlg, IDC_ACTIVE_NOKEYBOARD, BST_UNCHECKED);
		case IDC_ACTIVE_NOSOUND:
			if (!ischecked (hDlg, IDC_ACTIVE_NOSOUND))
				CheckDlgButton (hDlg, IDC_ACTIVE_PAUSE, BST_UNCHECKED);
		case IDC_ACTIVE_PAUSE:
			workprefs.win32_active_nocapture_pause = ischecked (hDlg, IDC_ACTIVE_PAUSE);
			if (workprefs.win32_active_nocapture_pause)
				CheckDlgButton (hDlg, IDC_ACTIVE_NOSOUND, BST_CHECKED);
			workprefs.win32_active_nocapture_nosound = ischecked (hDlg, IDC_ACTIVE_NOSOUND);
			workprefs.win32_active_input = ischecked(hDlg, IDC_ACTIVE_NOJOY) ? 0 : 4;
			workprefs.win32_active_input |= ischecked(hDlg, IDC_ACTIVE_NOKEYBOARD) ? 0 : 1;
			enable_for_miscdlg (hDlg);
			break;
		case IDC_MINIMIZED_NOJOY:
			if (!ischecked(hDlg, IDC_MINIMIZED_NOJOY))
				CheckDlgButton(hDlg, IDC_MINIMIZED_PAUSE, BST_UNCHECKED);
		case IDC_MINIMIZED_NOSOUND:
			if (!ischecked(hDlg, IDC_MINIMIZED_NOSOUND))
				CheckDlgButton(hDlg, IDC_MINIMIZED_PAUSE, BST_UNCHECKED);
		case IDC_MINIMIZED_PAUSE:
			workprefs.win32_iconified_pause = ischecked (hDlg, IDC_MINIMIZED_PAUSE);
			if (workprefs.win32_iconified_pause) {
				CheckDlgButton(hDlg, IDC_MINIMIZED_NOSOUND, BST_CHECKED);
				CheckDlgButton(hDlg, IDC_MINIMIZED_NOJOY, BST_CHECKED);
			}
			workprefs.win32_iconified_nosound = ischecked(hDlg, IDC_MINIMIZED_NOSOUND);
			workprefs.win32_iconified_input = ischecked(hDlg, IDC_MINIMIZED_NOJOY) ? 0 : 4;
			enable_for_miscdlg(hDlg);
			break;
		case IDC_KBLED_USB:
			workprefs.win32_kbledmode = ischecked (hDlg, IDC_KBLED_USB) ? 1 : 0;
			break;
		}
		recursive--;
		return TRUE;
	}
	recursive--;
	return FALSE;
}

static INT_PTR CALLBACK MiscDlgProc1 (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	currentpage = MISC1_ID;
	return MiscDlgProc (hDlg, msg, wParam, lParam);
}

static INT_PTR CALLBACK MiscDlgProc2 (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	currentpage = MISC2_ID;
	return MiscDlgProc (hDlg, msg, wParam, lParam);
}

static const int cpu_ids[]   = { IDC_CPU0, IDC_CPU1, IDC_CPU2, IDC_CPU3, IDC_CPU4, IDC_CPU5 };
static const int fpu_ids[]   = { IDC_FPU0, IDC_FPU1, IDC_FPU2, IDC_FPU3 };
static const int trust_ids[] = { IDC_TRUST0, IDC_TRUST1, IDC_TRUST1, IDC_TRUST1 };

static void cpudlg_slider_setup(HWND hDlg)
{
	xSendDlgItemMessage(hDlg, IDC_SPEED, TBM_SETRANGE, TRUE, workprefs.m68k_speed < 0 || (workprefs.cpu_memory_cycle_exact && !workprefs.cpu_cycle_exact) ? MAKELONG(-9, 0) : MAKELONG(-9, 50));
	xSendDlgItemMessage(hDlg, IDC_SPEED, TBM_SETPAGESIZE, 0, 1);
	xSendDlgItemMessage(hDlg, IDC_SPEED_x86, TBM_SETRANGE, TRUE, MAKELONG(0, 1000));
	xSendDlgItemMessage(hDlg, IDC_SPEED_x86, TBM_SETPAGESIZE, 0, 10);
}

static void enable_for_cpudlg (HWND hDlg)
{
	BOOL enable = FALSE, jitenable = FALSE;
	BOOL cpu_based_enable = FALSE;

	ew(hDlg, IDC_SPEED, !workprefs.cpu_cycle_exact);
	ew(hDlg, IDC_COMPATIBLE24, workprefs.cpu_model <= 68030);
	ew(hDlg, IDC_CPUIDLE, workprefs.m68k_speed != 0 ? TRUE : FALSE);
	ew(hDlg, IDC_PPC_CPUIDLE, workprefs.ppc_mode != 0);
	ew(hDlg, IDC_SPEED_x86, is_x86_cpu(&workprefs));
	ew(hDlg, IDC_CPUTEXT_x86, is_x86_cpu(&workprefs));
#if !defined(CPUEMU_0) || defined(CPUEMU_68000_ONLY)
	ew(hDlg, IDC_CPU1, FALSE);
	ew(hDlg, IDC_CPU2, FALSE);
	ew(hDlg, IDC_CPU3, FALSE);
	ew(hDlg, IDC_CPU4, FALSE);
	ew(hDlg, IDC_CPU5, FALSE);
#endif

	cpu_based_enable = workprefs.cpu_model >= 68020 && workprefs.address_space_24 == 0;

	jitenable = cpu_based_enable && !workprefs.mmu_model;
#ifndef JIT
	jitenable = FALSE;
#endif
	enable = jitenable && workprefs.cachesize;

	ew(hDlg, IDC_FPU_MODE, workprefs.fpu_model != 0);
	ew(hDlg, IDC_TRUST0, enable);
	ew(hDlg, IDC_TRUST1, enable);
	ew(hDlg, IDC_HARDFLUSH, enable);
	ew(hDlg, IDC_CONSTJUMP, enable);
	ew(hDlg, IDC_JITFPU, enable && workprefs.fpu_model > 0);
	ew(hDlg, IDC_JITCRASH, enable);
	ew(hDlg, IDC_NOFLAGS, enable);
	ew(hDlg, IDC_CS_CACHE_TEXT, enable);
	ew(hDlg, IDC_CACHE, enable);
	ew(hDlg, IDC_JITENABLE, jitenable);
	ew(hDlg, IDC_COMPATIBLE, !workprefs.cpu_memory_cycle_exact && !(workprefs.cachesize && workprefs.cpu_model >= 68040));
	ew(hDlg, IDC_COMPATIBLE_FPU, workprefs.fpu_model > 0);
	ew(hDlg, IDC_FPU_UNIMPLEMENTED, workprefs.fpu_model && !workprefs.cachesize);
	ew(hDlg, IDC_CPU_UNIMPLEMENTED, workprefs.cpu_model == 68060 && !workprefs.cachesize);
#if 0
	ew(hDlg, IDC_CPU_MULTIPLIER, workprefs.cpu_cycle_exact);
#endif
	ew(hDlg, IDC_CPU_FREQUENCY, (workprefs.cpu_cycle_exact || workprefs.cpu_compatible) && workprefs.m68k_speed >= 0);
	ew(hDlg, IDC_CPU_FREQUENCY2, workprefs.cpu_cycle_exact && !workprefs.cpu_clock_multiplier && workprefs.m68k_speed >= 0);

	ew(hDlg, IDC_FPU1, workprefs.cpu_model < 68040 && (workprefs.cpu_model >= 68020 || !workprefs.cpu_compatible));
	ew(hDlg, IDC_FPU2, workprefs.cpu_model < 68040 && (workprefs.cpu_model >= 68020 || !workprefs.cpu_compatible));
	ew(hDlg, IDC_FPU3, workprefs.cpu_model >= 68040);
	ew(hDlg, IDC_MMUENABLEEC, workprefs.cpu_model >= 68030 && workprefs.cachesize == 0);
	ew(hDlg, IDC_MMUENABLE, workprefs.cpu_model >= 68030 && workprefs.cachesize == 0);
	ew(hDlg, IDC_CPUDATACACHE, workprefs.cpu_model >= 68030 && workprefs.cachesize == 0 && workprefs.cpu_compatible);
	ew(hDlg, IDC_CPU_PPC, workprefs.cpu_model >= 68040 && (workprefs.ppc_mode == 1 || (workprefs.ppc_mode == 0 && !is_ppc_cpu(&workprefs))));
}

static float getcpufreq (int m)
{
	float f;

	f = workprefs.ntscmode ? 28636360.0f : 28375160.0f;
	return f * (m >> 8) / 8.0f;
}

static void values_to_cpudlg_sliders(HWND hDlg)
{
	TCHAR buffer[8] = _T("");
	_stprintf(buffer, _T("%+d%%"), (int)(workprefs.x86_speed_throttle / 10));
	SetDlgItemText(hDlg, IDC_CPUTEXT_x86, buffer);
	_stprintf(buffer, _T("%+d%%"), (int)(workprefs.m68k_speed_throttle / 10));
	SetDlgItemText(hDlg, IDC_CPUTEXT, buffer);
	_stprintf(buffer, _T("%d%%"), (workprefs.cpu_idle == 0 ? 0 : 12 - workprefs.cpu_idle / 15) * 10);
	SetDlgItemText(hDlg, IDC_CPUIDLETEXT, buffer);
	_stprintf (buffer, _T("%d MB"), workprefs.cachesize / 1024 );
	SetDlgItemText (hDlg, IDC_CACHETEXT, buffer);
}

static void values_to_cpudlg(HWND hDlg, WPARAM wParam)
{
	TCHAR buffer[8] = _T("");
	int cpu;

	xSendDlgItemMessage(hDlg, IDC_SPEED_x86, TBM_SETPOS, TRUE, (int)(workprefs.x86_speed_throttle / 100));
	xSendDlgItemMessage (hDlg, IDC_SPEED, TBM_SETPOS, TRUE, (int)(workprefs.m68k_speed_throttle / 100));
	values_to_cpudlg_sliders(hDlg);

	CheckDlgButton (hDlg, IDC_COMPATIBLE, workprefs.cpu_compatible);
	CheckDlgButton (hDlg, IDC_COMPATIBLE24, workprefs.address_space_24);
	CheckDlgButton (hDlg, IDC_CPUDATACACHE, workprefs.cpu_data_cache);
	CheckDlgButton (hDlg, IDC_COMPATIBLE_FPU, workprefs.fpu_strict);
	CheckDlgButton (hDlg, IDC_FPU_UNIMPLEMENTED, !workprefs.fpu_no_unimplemented || workprefs.cachesize);
	CheckDlgButton (hDlg, IDC_CPU_UNIMPLEMENTED, !workprefs.int_no_unimplemented || workprefs.cachesize);
	xSendDlgItemMessage (hDlg, IDC_CPUIDLE, TBM_SETPOS, TRUE, workprefs.cpu_idle == 0 ? 0 : 12 - workprefs.cpu_idle / 15);
	xSendDlgItemMessage (hDlg, IDC_PPC_CPUIDLE, TBM_SETPOS, TRUE, workprefs.ppc_cpu_idle);
	cpu = (workprefs.cpu_model - 68000) / 10;
	if (cpu >= 5)
		cpu--;
	CheckRadioButton (hDlg, IDC_CPU0, IDC_CPU5, cpu_ids[cpu]);
	CheckRadioButton (hDlg, IDC_FPU0, IDC_FPU3, fpu_ids[workprefs.fpu_model == 0 ? 0 : (workprefs.fpu_model == 68881 ? 1 : (workprefs.fpu_model == 68882 ? 2 : 3))]);

	if (workprefs.m68k_speed < 0)
		CheckRadioButton(hDlg, IDC_CS_HOST, IDC_CS_ADJUSTABLE, IDC_CS_HOST);
	else if (workprefs.m68k_speed >= 0)
		CheckRadioButton(hDlg, IDC_CS_HOST, IDC_CS_ADJUSTABLE, IDC_CS_68000);

	CheckRadioButton (hDlg, IDC_TRUST0, IDC_TRUST1, trust_ids[workprefs.comptrustbyte]);

	int idx = 0;
	for (int i = 0; i < MAX_CACHE_SIZE; i++) {
		if (workprefs.cachesize >= (1024 << i) && workprefs.cachesize < (1024 << i) * 2) {
			idx = i + 1;
			break;
		}
	}
	xSendDlgItemMessage (hDlg, IDC_CACHE, TBM_SETPOS, TRUE, idx);

	CheckDlgButton(hDlg, IDC_JITCRASH, workprefs.comp_catchfault);
	CheckDlgButton(hDlg, IDC_NOFLAGS, workprefs.compnf);
	CheckDlgButton(hDlg, IDC_JITFPU, workprefs.compfpu && workprefs.fpu_model > 0);
	CheckDlgButton(hDlg, IDC_HARDFLUSH, workprefs.comp_hardflush);
	CheckDlgButton(hDlg, IDC_CONSTJUMP, workprefs.comp_constjump);
	CheckDlgButton(hDlg, IDC_JITENABLE, workprefs.cachesize > 0);
	bool mmu = ((workprefs.cpu_model == 68060 && workprefs.mmu_model == 68060) ||
		(workprefs.cpu_model == 68040 && workprefs.mmu_model == 68040) ||
		(workprefs.cpu_model == 68030 && workprefs.mmu_model == 68030)) &&
		workprefs.cachesize == 0;
	CheckRadioButton (hDlg, IDC_MMUENABLEOFF, IDC_MMUENABLE, mmu == 0 ? IDC_MMUENABLEOFF : (mmu && workprefs.mmu_ec) ? IDC_MMUENABLEEC : IDC_MMUENABLE);
	CheckDlgButton(hDlg, IDC_CPU_PPC, workprefs.ppc_mode || is_ppc_cpu(&workprefs));

	idx = xSendDlgItemMessage(hDlg, IDC_CPU_FREQUENCY, CB_GETCURSEL, 0, 0);
	if (idx != CB_ERR) {
		int m = workprefs.cpu_clock_multiplier;
		workprefs.cpu_frequency = 0;
		workprefs.cpu_clock_multiplier = 0;
		if (idx < 5) {
			workprefs.cpu_clock_multiplier = (1 << 8) << idx;
			if (workprefs.cpu_cycle_exact || workprefs.cpu_compatible) {
				TCHAR txt[20];
				double f = getcpufreq(workprefs.cpu_clock_multiplier);
				_stprintf(txt, _T("%.6f"), f / 1000000.0);
				xSendDlgItemMessage(hDlg, IDC_CPU_FREQUENCY2, WM_SETTEXT, 0, (LPARAM)txt);
			} else {
				xSendDlgItemMessage(hDlg, IDC_CPU_FREQUENCY2, WM_SETTEXT, 0, (LPARAM)_T(""));
			}
		} else if (workprefs.cpu_cycle_exact) {
			TCHAR txt[20];
			txt[0] = 0;
			xSendDlgItemMessage(hDlg, IDC_CPU_FREQUENCY2, WM_GETTEXT, (WPARAM)sizeof(txt) / sizeof(TCHAR), (LPARAM)txt);
			workprefs.cpu_clock_multiplier = 0;
			workprefs.cpu_frequency = (int)(_tstof(txt) * 1000000.0);
			if (workprefs.cpu_frequency < 1 * 1000000)
				workprefs.cpu_frequency = 0;
			if (workprefs.cpu_frequency >= 99 * 1000000)
				workprefs.cpu_frequency = 0;
			if (!workprefs.cpu_frequency) {
				workprefs.cpu_frequency = (int)(getcpufreq(m) * 1000000.0);
			}
		}
	}
}

static void values_from_cpudlg(HWND hDlg, WPARAM wParam)
{
	int newcpu, oldcpu, newfpu, newtrust, idx;
	static int cachesize_prev, trust_prev;
#ifdef JIT
	int jitena, oldcache;
#endif

	workprefs.cpu_compatible = workprefs.cpu_memory_cycle_exact | (ischecked (hDlg, IDC_COMPATIBLE) ? 1 : 0);
	workprefs.fpu_strict = ischecked (hDlg, IDC_COMPATIBLE_FPU) ? 1 : 0;
	workprefs.fpu_no_unimplemented = ischecked (hDlg, IDC_FPU_UNIMPLEMENTED) ? 0 : 1;
	workprefs.int_no_unimplemented = ischecked (hDlg, IDC_CPU_UNIMPLEMENTED) ? 0 : 1;
	workprefs.address_space_24 = ischecked (hDlg, IDC_COMPATIBLE24) ? 1 : 0;
	workprefs.m68k_speed = ischecked (hDlg, IDC_CS_HOST) ? -1 : 0;
	workprefs.m68k_speed_throttle = (float)SendMessage (GetDlgItem (hDlg, IDC_SPEED), TBM_GETPOS, 0, 0) * 100;
	if (workprefs.m68k_speed_throttle > 0 && workprefs.m68k_speed < 0)
		workprefs.m68k_speed_throttle = 0;
	workprefs.x86_speed_throttle = (float)SendMessage(GetDlgItem(hDlg, IDC_SPEED_x86), TBM_GETPOS, 0, 0) * 100;
	idx = xSendDlgItemMessage(hDlg, IDC_FPU_MODE, CB_GETCURSEL, 0, 0);
	if (idx == 0)
		workprefs.fpu_mode = 0;
	if (idx == 1)
		workprefs.fpu_mode = -1;
	if (idx == 2)
		workprefs.fpu_mode = 1;

	newcpu = ischecked (hDlg, IDC_CPU0) ? 68000
		: ischecked (hDlg, IDC_CPU1) ? 68010
		: ischecked (hDlg, IDC_CPU2) ? 68020
		: ischecked (hDlg, IDC_CPU3) ? 68030
		: ischecked (hDlg, IDC_CPU4) ? 68040
		: ischecked (hDlg, IDC_CPU5) ? 68060 : 0;
	newfpu = ischecked (hDlg, IDC_FPU0) ? 0
		: ischecked (hDlg, IDC_FPU1) ? 1
		: ischecked (hDlg, IDC_FPU2) ? 2
		: ischecked (hDlg, IDC_FPU3) ? 3 : 0;

	/* When switching away from 68000, disable 24 bit addressing.  */
	oldcpu = workprefs.cpu_model;
	if (workprefs.cpu_model != newcpu && newcpu <= 68010)
		newfpu = 0;
	workprefs.cpu_model = newcpu;
	workprefs.mmu_model = 0;
	workprefs.mmu_ec = false;
	workprefs.cpu_data_cache = false;
	switch(newcpu)
	{
	case 68000:
	case 68010:
		workprefs.fpu_model = newfpu == 0 ? 0 : (newfpu == 2 ? 68882 : 68881);
		if (workprefs.cpu_compatible || workprefs.cpu_memory_cycle_exact)
			workprefs.fpu_model = 0;
		if (newcpu != oldcpu)
			workprefs.address_space_24 = 1;
		break;
	case 68020:
		workprefs.fpu_model = newfpu == 0 ? 0 : (newfpu == 2 ? 68882 : 68881);
		break;
	case 68030:
		if (newcpu != oldcpu)
			workprefs.address_space_24 = 0;
		workprefs.fpu_model = newfpu == 0 ? 0 : (newfpu == 2 ? 68882 : 68881);
		workprefs.mmu_ec = ischecked(hDlg, IDC_MMUENABLEEC);
		workprefs.mmu_model = workprefs.mmu_ec || ischecked (hDlg, IDC_MMUENABLE) ? 68030 : 0;
		if (workprefs.cpu_compatible)
			workprefs.cpu_data_cache = ischecked (hDlg, IDC_CPUDATACACHE);
		break;
	case 68040:
		workprefs.fpu_model = newfpu ? 68040 : 0;
		workprefs.address_space_24 = 0;
		if (workprefs.fpu_model)
			workprefs.fpu_model = 68040;
		workprefs.mmu_ec = ischecked(hDlg, IDC_MMUENABLEEC);
		workprefs.mmu_model = workprefs.mmu_ec || ischecked (hDlg, IDC_MMUENABLE) ? 68040 : 0;
		if (workprefs.cpu_compatible)
			workprefs.cpu_data_cache = ischecked (hDlg, IDC_CPUDATACACHE);
		break;
	case 68060:
		workprefs.fpu_model = newfpu ? 68060 : 0;
		workprefs.address_space_24 = 0;
		workprefs.mmu_ec = ischecked(hDlg, IDC_MMUENABLEEC);
		workprefs.mmu_model = workprefs.mmu_ec || ischecked (hDlg, IDC_MMUENABLE) ? 68060 : 0;
		if (workprefs.cpu_compatible)
			workprefs.cpu_data_cache = ischecked (hDlg, IDC_CPUDATACACHE);
		break;
	}

	if (newcpu != oldcpu && workprefs.cpu_compatible) {
		int idx = 0;
		if (newcpu <= 68010) {
			workprefs.cpu_clock_multiplier = 2 * 256;
			idx = 1;
		} else if (newcpu == 68020) {
			workprefs.cpu_clock_multiplier = 4 * 256;
			idx = 2;
		} else {
			workprefs.cpu_clock_multiplier = 8 * 256;
			idx = 3;
		}
		xSendDlgItemMessage(hDlg, IDC_CPU_FREQUENCY, CB_SETCURSEL, idx, 0);
	}

	newtrust = ischecked (hDlg, IDC_TRUST0) ? 0 : 1;
	workprefs.comptrustbyte = newtrust;
	workprefs.comptrustword = newtrust;
	workprefs.comptrustlong = newtrust;
	workprefs.comptrustnaddr= newtrust;

	workprefs.comp_catchfault   = ischecked(hDlg, IDC_JITCRASH);
	workprefs.compnf            = ischecked(hDlg, IDC_NOFLAGS);
	workprefs.compfpu           = ischecked(hDlg, IDC_JITFPU);
	workprefs.comp_hardflush    = ischecked(hDlg, IDC_HARDFLUSH);
	workprefs.comp_constjump    = ischecked(hDlg, IDC_CONSTJUMP);

#ifdef JIT
	oldcache = workprefs.cachesize;
	jitena = (ischecked (hDlg, IDC_JITENABLE) ? 1 : 0) && !workprefs.address_space_24 && workprefs.cpu_model >= 68020;
	idx = (int)SendMessage (GetDlgItem (hDlg, IDC_CACHE), TBM_GETPOS, 0, 0);
	workprefs.cachesize = 1024 << idx;
	if (workprefs.cachesize <= 1024)
		workprefs.cachesize = 0;
	else
		workprefs.cachesize /= 2;
	if (!jitena) {
		cachesize_prev = workprefs.cachesize;
		trust_prev = workprefs.comptrustbyte;
		workprefs.cachesize = 0;
	} else if (jitena && !oldcache) {
		workprefs.cachesize = MAX_JIT_CACHE;
		workprefs.cpu_cycle_exact = false;
		workprefs.blitter_cycle_exact = false;
		workprefs.cpu_memory_cycle_exact = false;
		if (!cachesize_prev)
			trust_prev = 0;
		if (cachesize_prev) {
			workprefs.cachesize = cachesize_prev;
		}
		workprefs.comptrustbyte = trust_prev;
		workprefs.comptrustword = trust_prev;
		workprefs.comptrustlong = trust_prev;
		workprefs.comptrustnaddr = trust_prev;
		if (workprefs.fpu_mode > 0 || workprefs.fpu_model == 0) {
			workprefs.compfpu = false;
			setchecked(hDlg, IDC_JITFPU, false);
		} else if (workprefs.fpu_model > 0) {
			workprefs.compfpu = true;
			setchecked(hDlg, IDC_JITFPU, true);
		}
	}
	if (!workprefs.cachesize) {
		setchecked (hDlg, IDC_JITENABLE, false);
		workprefs.compfpu = false;
		setchecked(hDlg, IDC_JITFPU, false);
	}
	if (workprefs.cachesize && workprefs.compfpu && workprefs.fpu_mode > 0) {
		workprefs.fpu_mode = 0;
		xSendDlgItemMessage(hDlg, IDC_FPU_MODE, CB_SETCURSEL, 0, 0);
	}
	if (oldcache == 0 && workprefs.cachesize > 0) {
		canbang = 1;
	}
	if (workprefs.cachesize && workprefs.cpu_model >= 68040) {
		workprefs.cpu_compatible = false;
	}
#endif
	if (ischecked(hDlg, IDC_CPU_PPC)) {
		if (workprefs.ppc_mode == 0)
			workprefs.ppc_mode = 1;
		if (workprefs.ppc_mode == 1 && workprefs.cpu_model < 68040)
			workprefs.ppc_mode = 0;
	} else if (!ischecked(hDlg, IDC_CPU_PPC) && workprefs.ppc_mode == 1) {
		workprefs.ppc_mode = 0;
	}

	workprefs.cpu_idle = (int)SendMessage (GetDlgItem (hDlg, IDC_CPUIDLE), TBM_GETPOS, 0, 0);
	if (workprefs.cpu_idle > 0)
		workprefs.cpu_idle = (12 - workprefs.cpu_idle) * 15;
	workprefs.ppc_cpu_idle = (int)SendMessage (GetDlgItem (hDlg, IDC_PPC_CPUIDLE), TBM_GETPOS, 0, 0);

	if (pages[KICKSTART_ID])
		SendMessage (pages[KICKSTART_ID], WM_USER, 0, 0);
	if (pages[DISPLAY_ID])
		SendMessage (pages[DISPLAY_ID], WM_USER, 0, 0);
	if (pages[MEMORY_ID])
		SendMessage (pages[MEMORY_ID], WM_USER, 0, 0);
}

static INT_PTR CALLBACK CPUDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;
	int idx;

	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch (msg) {
	case WM_INITDIALOG:
		recursive++;
		pages[CPU_ID] = hDlg;
		currentpage = CPU_ID;
		xSendDlgItemMessage(hDlg, IDC_CACHE, TBM_SETRANGE, TRUE, MAKELONG(MIN_CACHE_SIZE, MAX_CACHE_SIZE));
		xSendDlgItemMessage(hDlg, IDC_CACHE, TBM_SETPAGESIZE, 0, 1);
		xSendDlgItemMessage(hDlg, IDC_CPUIDLE, TBM_SETRANGE, TRUE, MAKELONG(0, 10));
		xSendDlgItemMessage(hDlg, IDC_CPUIDLE, TBM_SETPAGESIZE, 0, 1);
		xSendDlgItemMessage(hDlg, IDC_PPC_CPUIDLE, TBM_SETRANGE, TRUE, MAKELONG(0, 10));
		xSendDlgItemMessage(hDlg, IDC_PPC_CPUIDLE, TBM_SETPAGESIZE, 0, 1);

		xSendDlgItemMessage(hDlg, IDC_CPU_FREQUENCY, CB_RESETCONTENT, 0, 0);
		xSendDlgItemMessage(hDlg, IDC_CPU_FREQUENCY, CB_ADDSTRING, 0, (LPARAM)_T("1x"));
		xSendDlgItemMessage(hDlg, IDC_CPU_FREQUENCY, CB_ADDSTRING, 0, (LPARAM)_T("2x (A500)"));
		xSendDlgItemMessage(hDlg, IDC_CPU_FREQUENCY, CB_ADDSTRING, 0, (LPARAM)_T("4x (A1200)"));
		xSendDlgItemMessage(hDlg, IDC_CPU_FREQUENCY, CB_ADDSTRING, 0, (LPARAM)_T("8x"));
		xSendDlgItemMessage(hDlg, IDC_CPU_FREQUENCY, CB_ADDSTRING, 0, (LPARAM)_T("16x"));
		if (workprefs.cpu_cycle_exact) {
			xSendDlgItemMessage(hDlg, IDC_CPU_FREQUENCY, CB_ADDSTRING, 0, (LPARAM)_T("Custom"));
		}

		xSendDlgItemMessage(hDlg, IDC_FPU_MODE, CB_RESETCONTENT, 0, 0);
		xSendDlgItemMessage(hDlg, IDC_FPU_MODE, CB_ADDSTRING, 0, (LPARAM)_T("Host (64-bit)"));
		xSendDlgItemMessage(hDlg, IDC_FPU_MODE, CB_ADDSTRING, 0, (LPARAM)_T("Host (80-bit)"));
		xSendDlgItemMessage(hDlg, IDC_FPU_MODE, CB_ADDSTRING, 0, (LPARAM)_T("Softfloat (80-bit)"));
		idx = workprefs.fpu_mode < 0 ? 1 : (workprefs.fpu_mode > 0 ? 2 : 0);
		xSendDlgItemMessage(hDlg, IDC_FPU_MODE, CB_SETCURSEL, idx, 0);

		idx = 5;
		if (workprefs.cpu_clock_multiplier >= 1 << 8) {
			idx = 0;
			while (idx < 4) {
				if (workprefs.cpu_clock_multiplier <= (1 << 8) << idx)
					break;
				idx++;
			}
		} else if (workprefs.cpu_clock_multiplier == 0 && workprefs.cpu_frequency == 0 && workprefs.cpu_model <= 68010) {
			idx = 1; // A500
		} else if (workprefs.cpu_clock_multiplier == 0 && workprefs.cpu_frequency == 0 && workprefs.cpu_model >= 68020) {
			idx = 2; // A1200
		}
		if (!workprefs.cpu_cycle_exact) {
			workprefs.cpu_frequency = 0;
			if (!workprefs.cpu_clock_multiplier && (idx == 1 || idx == 2)) {
				workprefs.cpu_clock_multiplier = (1 << idx) << 8;
			}
		} else {
			if (!workprefs.cpu_frequency && (idx == 1 || idx == 2)) {
				workprefs.cpu_clock_multiplier = (1 << idx) << 8;
			}
		}
		xSendDlgItemMessage (hDlg, IDC_CPU_FREQUENCY, CB_SETCURSEL, idx, 0);
		if (!workprefs.cpu_clock_multiplier) {
			TCHAR txt[20];
			_stprintf (txt, _T("%.6f"), workprefs.cpu_frequency / 1000000.0);
			xSendDlgItemMessage (hDlg, IDC_CPU_FREQUENCY2, WM_SETTEXT, 0, (LPARAM)txt);
		} else {
			TCHAR txt[20];
			double f = getcpufreq(workprefs.cpu_clock_multiplier);
			_stprintf(txt, _T("%.6f"), f / 1000000.0);
			xSendDlgItemMessage(hDlg, IDC_CPU_FREQUENCY2, WM_SETTEXT, 0, (LPARAM)txt);
		}

		cpudlg_slider_setup(hDlg);
		recursive--;

	case WM_USER:
		recursive++;
		enable_for_cpudlg (hDlg);
		values_to_cpudlg (hDlg, wParam);
		recursive--;
		return TRUE;

	case WM_COMMAND:
		if (recursive > 0)
			break;
		if (currentpage == CPU_ID) {
			recursive++;
			values_from_cpudlg(hDlg, wParam);
			enable_for_cpudlg(hDlg);
			values_to_cpudlg(hDlg, wParam);
			cpudlg_slider_setup(hDlg);
			recursive--;
		}
		break;

	case WM_HSCROLL:
		if (currentpage == CPU_ID) {
			recursive++;
			values_from_cpudlg(hDlg, wParam);
			values_to_cpudlg_sliders(hDlg);
			enable_for_cpudlg(hDlg);
			recursive--;
		}
		break;
	}
	return FALSE;
}

static void enable_for_sounddlg (HWND hDlg)
{
	int numdevs;

	numdevs = enumerate_sound_devices ();
	if (numdevs == 0)
		ew (hDlg, IDC_SOUNDCARDLIST, FALSE);
	else
		ew (hDlg, IDC_SOUNDCARDLIST, workprefs.produce_sound);

	ew (hDlg, IDC_FREQUENCY, workprefs.produce_sound);
	ew (hDlg, IDC_SOUNDFREQ, workprefs.produce_sound ? TRUE : FALSE);
	ew (hDlg, IDC_SOUNDSTEREO, workprefs.produce_sound);
	ew (hDlg, IDC_SOUNDINTERPOLATION, workprefs.produce_sound);
	ew (hDlg, IDC_SOUNDVOLUME, workprefs.produce_sound);
	ew (hDlg, IDC_SOUNDVOLUME2, workprefs.produce_sound);
	ew (hDlg, IDC_SOUNDVOLUMEEXT, workprefs.produce_sound);
	ew (hDlg, IDC_SOUNDVOLUMEEXT2, workprefs.produce_sound);
	ew (hDlg, IDC_SOUNDSTEREOSEP, workprefs.sound_stereo > 0 && workprefs.produce_sound);
	ew (hDlg, IDC_SOUNDSTEREOMIX, workprefs.sound_stereo > 0 && workprefs.produce_sound);

	ew (hDlg, IDC_SOUNDBUFFERMEM, workprefs.produce_sound);
	ew (hDlg, IDC_SOUNDBUFFERRAM, workprefs.produce_sound);
	ew (hDlg, IDC_SOUNDADJUST, workprefs.produce_sound);
	ew (hDlg, IDC_SOUNDADJUSTNUM, workprefs.produce_sound);
	ew (hDlg, IDC_SOUNDBUFFERTEXT, workprefs.produce_sound);

	ew (hDlg, IDC_SOUNDDRIVE, workprefs.produce_sound);
	ew (hDlg, IDC_SOUNDDRIVESELECT, workprefs.produce_sound);
	ew (hDlg, IDC_SOUNDDRIVEVOLUME, workprefs.produce_sound);
	ew (hDlg, IDC_SOUNDDRIVEVOLUME2, workprefs.produce_sound);
	ew (hDlg, IDC_SOUNDDRIVEVOLUMEX, workprefs.produce_sound);
	ew (hDlg, IDC_SOUNDDRIVEVOLUMEX2, workprefs.produce_sound);
	ew (hDlg, IDC_AUDIOSYNC, workprefs.produce_sound);
	ew (hDlg, IDC_SOUNDFILTER, workprefs.produce_sound);
	ew (hDlg, IDC_SOUNDSWAP, workprefs.produce_sound);

	ew (hDlg, IDC_SOUNDCALIBRATE, workprefs.produce_sound && full_property_sheet);
}

static int exact_log2 (int v)
{
	int l = 0;
	while ((v >>= 1) != 0)
		l++;
	return l;
}

static TCHAR *drivesounds;

static void sound_loaddrivesamples (void)
{
	WIN32_FIND_DATA fd;
	HANDLE h;
	TCHAR *p;
	int len = 0;
	TCHAR dirname[1024];

	free (drivesounds);
	p = drivesounds = 0;

	get_plugin_path (dirname, sizeof dirname / sizeof (TCHAR), _T("floppysounds"));
	_tcscat (dirname, _T("*.wav"));
	h = FindFirstFile (dirname, &fd);
	if (h == INVALID_HANDLE_VALUE)
		return;
	for (;;) {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			TCHAR *name = fd.cFileName;
			if (_tcslen (name) > _tcslen (DS_NAME_CLICK) + 4 && !_tcsncmp (name, DS_NAME_CLICK, _tcslen (DS_NAME_CLICK))) {
				if (p - drivesounds < 1000) {
					TCHAR *oldp = p;
					len += 2000;
					drivesounds = p = xrealloc (TCHAR, drivesounds, len);
					if (oldp) {
						do {
							p = p + _tcslen (p) + 1;
						} while (p[0]);
					}
				}
				_tcscpy (p, name + _tcslen (DS_NAME_CLICK));
				p[_tcslen (name + _tcslen (DS_NAME_CLICK)) - 4] = 0;
				p += _tcslen (p);
				*p++ = 0;
				*p = 0;
			}
		}
		if (!FindNextFile (h, &fd))
			break;
	}
	FindClose (h);
}

extern int soundpercent;

static const int sndbufsizes[] = { 1024, 2048, 3072, 4096, 6144, 8192, 12288, 16384, 32768, 65536, -1 };
static int *volumeselection, volumeselectionindex;
static int sounddrivesel;

static int getsoundbufsizeindex (int size)
{
	int idx;
	if (size < sndbufsizes[0])
		return 0;
	for (idx = 0; sndbufsizes[idx] < size && sndbufsizes[idx + 1] >= 0 ; idx++);
	return idx + 1;
}

static void update_soundgui (HWND hDlg)
{
	int bufsize;
	TCHAR txt[20];

	bufsize = getsoundbufsizeindex (workprefs.sound_maxbsiz);
	if (bufsize <= 0) {
		_tcscpy(txt, _T("Min"));
	} else {
		_stprintf (txt, _T("%d"), bufsize);
	}
	SetDlgItemText (hDlg, IDC_SOUNDBUFFERMEM, txt);

	xSendDlgItemMessage (hDlg, IDC_SOUNDVOLUME, TBM_SETPOS, TRUE, 100 - workprefs.sound_volume_master);
	_stprintf (txt, _T("%d%%"), 100 - workprefs.sound_volume_master);
	SetDlgItemText (hDlg, IDC_SOUNDVOLUME2, txt);

	xSendDlgItemMessage (hDlg, IDC_SOUNDVOLUMEEXT, TBM_SETPOS, TRUE, 100 - (*volumeselection));
	_stprintf (txt, _T("%d%%"), 100 - (*volumeselection));
	SetDlgItemText (hDlg, IDC_SOUNDVOLUMEEXT2, txt);

	xSendDlgItemMessage (hDlg, IDC_SOUNDDRIVEVOLUME, TBM_SETPOS, TRUE, 100 - workprefs.dfxclickvolume_empty[sounddrivesel]);
	_stprintf (txt, _T("%d%%"), 100 - workprefs.dfxclickvolume_empty[sounddrivesel]);
	SetDlgItemText (hDlg, IDC_SOUNDDRIVEVOLUME2, txt);

	xSendDlgItemMessage (hDlg, IDC_SOUNDDRIVEVOLUMEX, TBM_SETPOS, TRUE, 100 - workprefs.dfxclickvolume_disk[sounddrivesel]);
	_stprintf (txt, _T("%d%%"), 100 - workprefs.dfxclickvolume_disk[sounddrivesel]);
	SetDlgItemText (hDlg, IDC_SOUNDDRIVEVOLUMEX2, txt);
}

static int soundfreqs[] = { 11025, 15000, 22050, 32000, 44100, 48000, 0 };
static int sounddrivers[] = { IDC_SOUND_DS, IDC_SOUND_WASAPI, IDC_SOUND_OPENAL, IDC_SOUND_PORTAUDIO, 0 };

static void values_to_sounddlg (HWND hDlg)
{
	int which_button;
	int sound_freq = workprefs.sound_freq;
	int produce_sound = workprefs.produce_sound;
	int stereo = workprefs.sound_stereo;
	TCHAR txt[100], txt2[100], *p;
	int i, selected;

	xSendDlgItemMessage (hDlg, IDC_SOUNDFILTER, CB_RESETCONTENT, 0, 0);
	WIN32GUI_LoadUIString (IDS_SOUND_FILTER_OFF, txt, sizeof (txt) / sizeof (TCHAR));
	xSendDlgItemMessage (hDlg, IDC_SOUNDFILTER, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_SOUND_FILTER_EMULATED, txt, sizeof (txt) / sizeof (TCHAR));
	xSendDlgItemMessage (hDlg, IDC_SOUNDFILTER, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_SOUND_FILTER_EMULATED_E, txt, sizeof (txt) / sizeof (TCHAR));
	xSendDlgItemMessage (hDlg, IDC_SOUNDFILTER, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_SOUND_FILTER_ON_AGA, txt, sizeof (txt) / sizeof (TCHAR));
	xSendDlgItemMessage (hDlg, IDC_SOUNDFILTER, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString(IDS_SOUND_FILTER_ON_A500, txt, sizeof(txt) / sizeof(TCHAR));
	xSendDlgItemMessage(hDlg, IDC_SOUNDFILTER, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString(IDS_SOUND_FILTER_ON_FIXEDONLY, txt, sizeof(txt) / sizeof(TCHAR));
	xSendDlgItemMessage(hDlg, IDC_SOUNDFILTER, CB_ADDSTRING, 0, (LPARAM)txt);
	i = 0;
	switch (workprefs.sound_filter)
	{
	case 0:
		i = 0;
		break;
	case 1:
		i = workprefs.sound_filter_type ? 2 : 1;
		break;
	case 2:
		i = workprefs.sound_filter_type == 2 ? 5 : (workprefs.sound_filter_type == 1 ? 4 : 3);
		break;
	}
	xSendDlgItemMessage (hDlg, IDC_SOUNDFILTER, CB_SETCURSEL, i, 0);

	xSendDlgItemMessage (hDlg, IDC_SOUNDSTEREO, CB_RESETCONTENT, 0, 0);
	WIN32GUI_LoadUIString (IDS_SOUND_MONO, txt, sizeof (txt) / sizeof (TCHAR));
	xSendDlgItemMessage (hDlg, IDC_SOUNDSTEREO, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_SOUND_STEREO, txt, sizeof (txt) / sizeof (TCHAR));
	xSendDlgItemMessage (hDlg, IDC_SOUNDSTEREO, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_SOUND_STEREO2, txt, sizeof (txt) / sizeof (TCHAR));
	xSendDlgItemMessage (hDlg, IDC_SOUNDSTEREO, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_SOUND_4CHANNEL, txt, sizeof (txt) / sizeof (TCHAR));
	xSendDlgItemMessage (hDlg, IDC_SOUNDSTEREO, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_SOUND_CLONED51, txt, sizeof (txt) / sizeof (TCHAR));
	xSendDlgItemMessage (hDlg, IDC_SOUNDSTEREO, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString(IDS_SOUND_51, txt, sizeof(txt) / sizeof(TCHAR));
	xSendDlgItemMessage(hDlg, IDC_SOUNDSTEREO, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString(IDS_SOUND_CLONED71, txt, sizeof(txt) / sizeof(TCHAR));
	xSendDlgItemMessage(hDlg, IDC_SOUNDSTEREO, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString(IDS_SOUND_71, txt, sizeof(txt) / sizeof(TCHAR));
	xSendDlgItemMessage(hDlg, IDC_SOUNDSTEREO, CB_ADDSTRING, 0, (LPARAM)txt);
	xSendDlgItemMessage(hDlg, IDC_SOUNDSTEREO, CB_SETCURSEL, workprefs.sound_stereo, 0);

	xSendDlgItemMessage (hDlg, IDC_SOUNDSWAP, CB_RESETCONTENT, 0, 0);
	xSendDlgItemMessage (hDlg, IDC_SOUNDSWAP, CB_ADDSTRING, 0, (LPARAM)_T("-"));
	WIN32GUI_LoadUIString (IDS_SOUND_SWAP_PAULA, txt, sizeof (txt) / sizeof (TCHAR));
	xSendDlgItemMessage (hDlg, IDC_SOUNDSWAP, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_SOUND_SWAP_AHI, txt, sizeof (txt) / sizeof (TCHAR));
	xSendDlgItemMessage (hDlg, IDC_SOUNDSWAP, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_SOUND_SWAP_BOTH, txt, sizeof (txt) / sizeof (TCHAR));
	xSendDlgItemMessage (hDlg, IDC_SOUNDSWAP, CB_ADDSTRING, 0, (LPARAM)txt);
	xSendDlgItemMessage (hDlg, IDC_SOUNDSWAP, CB_SETCURSEL,
		workprefs.sound_stereo_swap_paula + workprefs.sound_stereo_swap_ahi * 2, 0);

	xSendDlgItemMessage (hDlg, IDC_SOUNDSTEREOSEP, CB_RESETCONTENT, 0, 0);
	for (i = 10; i >= 0; i--) {
		_stprintf (txt, _T("%d%%"), i * 10);
		xSendDlgItemMessage (hDlg, IDC_SOUNDSTEREOSEP, CB_ADDSTRING, 0, (LPARAM)txt);
	}
	xSendDlgItemMessage (hDlg, IDC_SOUNDSTEREOSEP, CB_SETCURSEL, 10 - workprefs.sound_stereo_separation, 0);

	xSendDlgItemMessage (hDlg, IDC_SOUNDSTEREOMIX, CB_RESETCONTENT, 0, 0);
	xSendDlgItemMessage (hDlg, IDC_SOUNDSTEREOMIX, CB_ADDSTRING, 0, (LPARAM)_T("-"));
	for (i = 0; i < 10; i++) {
		_stprintf (txt, _T("%d"), i + 1);
		xSendDlgItemMessage (hDlg, IDC_SOUNDSTEREOMIX, CB_ADDSTRING, 0, (LPARAM)txt);
	}
	xSendDlgItemMessage (hDlg, IDC_SOUNDSTEREOMIX, CB_SETCURSEL,
		workprefs.sound_mixed_stereo_delay > 0 ? workprefs.sound_mixed_stereo_delay : 0, 0);

	xSendDlgItemMessage (hDlg, IDC_SOUNDINTERPOLATION, CB_RESETCONTENT, 0, 0);
	WIN32GUI_LoadUIString (IDS_DISABLED, txt, sizeof (txt) / sizeof (TCHAR));
	xSendDlgItemMessage (hDlg, IDC_SOUNDINTERPOLATION, CB_ADDSTRING, 0, (LPARAM)txt);
	xSendDlgItemMessage (hDlg, IDC_SOUNDINTERPOLATION, CB_ADDSTRING, 0, (LPARAM)_T("Anti"));
	xSendDlgItemMessage (hDlg, IDC_SOUNDINTERPOLATION, CB_ADDSTRING, 0, (LPARAM)_T("Sinc"));
	xSendDlgItemMessage (hDlg, IDC_SOUNDINTERPOLATION, CB_ADDSTRING, 0, (LPARAM)_T("RH"));
	xSendDlgItemMessage (hDlg, IDC_SOUNDINTERPOLATION, CB_ADDSTRING, 0, (LPARAM)_T("Crux"));
	xSendDlgItemMessage (hDlg, IDC_SOUNDINTERPOLATION, CB_SETCURSEL, workprefs.sound_interpol, 0);

	xSendDlgItemMessage (hDlg, IDC_SOUNDFREQ, CB_RESETCONTENT, 0, 0);
	i = 0;
	selected = -1;
	while (soundfreqs[i]) {
		_stprintf (txt, _T("%d"), soundfreqs[i]);
		xSendDlgItemMessage (hDlg, IDC_SOUNDFREQ, CB_ADDSTRING, 0, (LPARAM)txt);
		i++;
	}
	_stprintf (txt, _T("%d"), workprefs.sound_freq);
	xSendDlgItemMessage (hDlg, IDC_SOUNDFREQ, WM_SETTEXT, 0, (LPARAM)txt);

	switch (workprefs.produce_sound)
	{
	case 0: which_button = IDC_SOUND0; break;
	case 1: which_button = IDC_SOUND1; break;
	case 2: case 3: which_button = IDC_SOUND2; break;
	}
	CheckRadioButton (hDlg, IDC_SOUND0, IDC_SOUND2, which_button);

	CheckDlgButton (hDlg, IDC_SOUND_AUTO, workprefs.sound_auto);
	//CheckDlgButton(hDlg, IDC_SOUND_VOLCNT, workprefs.sound_volcnt);

	if (workprefs.sound_maxbsiz < SOUND_BUFFER_MULTIPLIER)
		workprefs.sound_maxbsiz = 0;
	xSendDlgItemMessage (hDlg, IDC_SOUNDBUFFERRAM, TBM_SETPOS, TRUE, getsoundbufsizeindex (workprefs.sound_maxbsiz));

	xSendDlgItemMessage (hDlg, IDC_SOUNDCARDLIST, CB_SETCURSEL, workprefs.win32_soundcard, 0);

	sounddrivesel = xSendDlgItemMessage (hDlg, IDC_SOUNDDRIVE, CB_GETCURSEL, 0, 0);
	if (sounddrivesel < 0)
		sounddrivesel = 0;
	xSendDlgItemMessage (hDlg, IDC_SOUNDDRIVE, CB_RESETCONTENT, 0, 0);
	for (i = 0; i < 4; i++) {
		_stprintf (txt, _T("DF%d:"), i);
		xSendDlgItemMessage (hDlg, IDC_SOUNDDRIVE, CB_ADDSTRING, 0, (LPARAM)txt);
	}
	xSendDlgItemMessage (hDlg, IDC_SOUNDDRIVE, CB_SETCURSEL, sounddrivesel, 0);
	xSendDlgItemMessage (hDlg, IDC_SOUNDDRIVESELECT, CB_RESETCONTENT, 0, 0);
	WIN32GUI_LoadUIString (IDS_DRIVESOUND_NONE, txt, sizeof (txt) / sizeof (TCHAR));
	xSendDlgItemMessage (hDlg, IDC_SOUNDDRIVESELECT, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_DRIVESOUND_DEFAULT_A500, txt, sizeof (txt) / sizeof (TCHAR));
	xSendDlgItemMessage (hDlg, IDC_SOUNDDRIVESELECT, CB_ADDSTRING, 0, (LPARAM)txt);
	driveclick_fdrawcmd_detect ();
	if (driveclick_pcdrivemask) {
		for (i = 0; i < 2; i++) {
			WIN32GUI_LoadUIString (IDS_DRIVESOUND_PC_FLOPPY, txt, sizeof (txt) / sizeof (TCHAR));
			_stprintf (txt2, txt, 'A' + i);
			xSendDlgItemMessage (hDlg, IDC_SOUNDDRIVESELECT, CB_ADDSTRING, 0, (LPARAM)txt2);
		}
	}
	xSendDlgItemMessage (hDlg, IDC_SOUNDDRIVESELECT, CB_SETCURSEL, 0, 0);
	p = drivesounds;
	if (p) {
		while (p[0]) {
			xSendDlgItemMessage (hDlg, IDC_SOUNDDRIVESELECT, CB_ADDSTRING, 0, (LPARAM)p);
			p += _tcslen (p) + 1;
		}
	}
	if (workprefs.floppyslots[sounddrivesel].dfxclick < 0) {
		p = drivesounds;
		i = DS_BUILD_IN_SOUNDS + (driveclick_pcdrivemask ? 2 : 0) + 1;
		while (p && p[0]) {
			if (!_tcsicmp (p, workprefs.floppyslots[sounddrivesel].dfxclickexternal)) {
				xSendDlgItemMessage (hDlg, IDC_SOUNDDRIVESELECT, CB_SETCURSEL, i, 0);
				break;
			}
			i++;
			p += _tcslen (p) + 1;
		}

	} else {
		xSendDlgItemMessage (hDlg, IDC_SOUNDDRIVESELECT, CB_SETCURSEL, workprefs.floppyslots[sounddrivesel].dfxclick, 0);
	}

	update_soundgui (hDlg);
}

static void values_from_sounddlg (HWND hDlg)
{
	TCHAR txt[10];
	int idx;
	int soundcard, i;

	idx = xSendDlgItemMessage (hDlg, IDC_SOUNDFREQ, CB_GETCURSEL, 0, 0);
	if (idx >= 0) {
		workprefs.sound_freq = soundfreqs[idx];
	} else {
		txt[0] = 0;
		xSendDlgItemMessage (hDlg, IDC_SOUNDFREQ, WM_GETTEXT, (WPARAM)sizeof (txt) / sizeof (TCHAR), (LPARAM)txt);
		workprefs.sound_freq = _tstol (txt);
	}
	if (workprefs.sound_freq < 8000)
		workprefs.sound_freq = 8000;
	if (workprefs.sound_freq > 768000)
		workprefs.sound_freq = 768000;

	workprefs.produce_sound = (ischecked (hDlg, IDC_SOUND0) ? 0
		: ischecked (hDlg, IDC_SOUND1) ? 1 : 3);

	workprefs.sound_auto = ischecked (hDlg, IDC_SOUND_AUTO);
	//workprefs.sound_volcnt = ischecked(hDlg, IDC_SOUND_VOLCNT);

	idx = xSendDlgItemMessage (hDlg, IDC_SOUNDSTEREO, CB_GETCURSEL, 0, 0);
	if (idx != CB_ERR)
		workprefs.sound_stereo = idx;
	workprefs.sound_stereo_separation = 0;
	workprefs.sound_mixed_stereo_delay = 0;
	if (workprefs.sound_stereo > 0) {
		idx = xSendDlgItemMessage (hDlg, IDC_SOUNDSTEREOSEP, CB_GETCURSEL, 0, 0);
		if (idx != CB_ERR) {
			if (idx > 0)
				workprefs.sound_mixed_stereo_delay = -1;
			workprefs.sound_stereo_separation = 10 - idx;
		}
		idx = xSendDlgItemMessage (hDlg, IDC_SOUNDSTEREOMIX, CB_GETCURSEL, 0, 0);
		if (idx != CB_ERR && idx > 0)
			workprefs.sound_mixed_stereo_delay = idx;
	}

	workprefs.sound_interpol = xSendDlgItemMessage (hDlg, IDC_SOUNDINTERPOLATION, CB_GETCURSEL, 0, 0);
	soundcard = xSendDlgItemMessage (hDlg, IDC_SOUNDCARDLIST, CB_GETCURSEL, 0, 0L);
	if (soundcard != workprefs.win32_soundcard && soundcard != CB_ERR) {
		workprefs.win32_soundcard = soundcard;
		update_soundgui (hDlg);
	}

	switch (xSendDlgItemMessage (hDlg, IDC_SOUNDFILTER, CB_GETCURSEL, 0, 0))
	{
	case 0:
		workprefs.sound_filter = FILTER_SOUND_OFF;
		break;
	case 1:
		workprefs.sound_filter = FILTER_SOUND_EMUL;
		workprefs.sound_filter_type = 0;
		break;
	case 2:
		workprefs.sound_filter = FILTER_SOUND_EMUL;
		workprefs.sound_filter_type = 1;
		break;
	case 3:
		workprefs.sound_filter = FILTER_SOUND_ON;
		workprefs.sound_filter_type = 0;
		break;
	case 4:
		workprefs.sound_filter = FILTER_SOUND_ON;
		workprefs.sound_filter_type = 1;
		break;
	case 5:
		workprefs.sound_filter = FILTER_SOUND_ON;
		workprefs.sound_filter_type = 2;
		break;
	}

	workprefs.sound_stereo_swap_paula = (xSendDlgItemMessage (hDlg, IDC_SOUNDSWAP, CB_GETCURSEL, 0, 0) & 1) ? 1 : 0;
	workprefs.sound_stereo_swap_ahi = (xSendDlgItemMessage (hDlg, IDC_SOUNDSWAP, CB_GETCURSEL, 0, 0) & 2) ? 1 : 0;

	idx = xSendDlgItemMessage (hDlg, IDC_SOUNDVOLUMESELECT, CB_GETCURSEL, 0, 0);
	if (idx != volumeselectionindex) {
		volumeselectionindex = idx;
		if (volumeselectionindex < 0 || volumeselectionindex > 4)
			volumeselectionindex = 0;
		if (volumeselectionindex == 1)
			volumeselection = &workprefs.sound_volume_cd;
		else if (volumeselectionindex == 2)
			volumeselection = &workprefs.sound_volume_board;
		else if (volumeselectionindex == 3)
			volumeselection = &workprefs.sound_volume_midi;
		else if (volumeselectionindex == 4)
			volumeselection = &workprefs.sound_volume_genlock;
		else
			volumeselection = &workprefs.sound_volume_paula;
		update_soundgui (hDlg);
	}

	for (i = 0; sounddrivers[i]; i++) {
		int old = sounddrivermask;
		sounddrivermask &= ~(1 << i);
		if (ischecked (hDlg, sounddrivers[i]))
			sounddrivermask |= 1 << i;
		if (old != sounddrivermask)
			regsetint (NULL, _T("SoundDriverMask"), sounddrivermask);
	}

	idx = xSendDlgItemMessage (hDlg, IDC_SOUNDDRIVE, CB_GETCURSEL, 0, 0);
	if (idx != CB_ERR && idx >= 0) {
		int res = xSendDlgItemMessage (hDlg, IDC_SOUNDDRIVESELECT, CB_GETCURSEL, 0, 0);
		if (res != CB_ERR && res >= 0) {
			int xtra = driveclick_pcdrivemask ? 2 : 0;
			if (res > DS_BUILD_IN_SOUNDS + xtra) {
				int j = res - (DS_BUILD_IN_SOUNDS + xtra + 1);
				TCHAR *p = drivesounds;
				while (j-- > 0)
					p += _tcslen (p) + 1;
				workprefs.floppyslots[idx].dfxclick = -1;
				_tcscpy (workprefs.floppyslots[idx].dfxclickexternal, p);
			} else {
				workprefs.floppyslots[idx].dfxclick = res;
				workprefs.floppyslots[idx].dfxclickexternal[0] = 0;
			}
		}
	}
}

static INT_PTR CALLBACK SoundDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;
	int numdevs;
	int card, i;

	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch (msg) {
	case WM_INITDIALOG:
		{
			recursive++;
			sound_loaddrivesamples ();
			xSendDlgItemMessage (hDlg, IDC_SOUNDBUFFERRAM, TBM_SETRANGE, TRUE, MAKELONG (MIN_SOUND_MEM, MAX_SOUND_MEM));
			xSendDlgItemMessage (hDlg, IDC_SOUNDBUFFERRAM, TBM_SETPAGESIZE, 0, 1);

			xSendDlgItemMessage (hDlg, IDC_SOUNDVOLUME, TBM_SETRANGE, TRUE, MAKELONG (0, 100));
			xSendDlgItemMessage (hDlg, IDC_SOUNDVOLUME, TBM_SETPAGESIZE, 0, 1);

			xSendDlgItemMessage (hDlg, IDC_SOUNDVOLUMEEXT, TBM_SETRANGE, TRUE, MAKELONG (0, 100));
			xSendDlgItemMessage (hDlg, IDC_SOUNDVOLUMEEXT, TBM_SETPAGESIZE, 0, 1);

			xSendDlgItemMessage (hDlg, IDC_SOUNDDRIVEVOLUME, TBM_SETRANGE, TRUE, MAKELONG (0, 100));
			xSendDlgItemMessage (hDlg, IDC_SOUNDDRIVEVOLUME, TBM_SETPAGESIZE, 0, 1);
			xSendDlgItemMessage (hDlg, IDC_SOUNDDRIVEVOLUMEX, TBM_SETRANGE, TRUE, MAKELONG (0, 100));
			xSendDlgItemMessage (hDlg, IDC_SOUNDDRIVEVOLUMEX, TBM_SETPAGESIZE, 0, 1);

			xSendDlgItemMessage (hDlg, IDC_SOUNDADJUST, TBM_SETRANGE, TRUE, MAKELONG (-100, +30));
			xSendDlgItemMessage (hDlg, IDC_SOUNDADJUST, TBM_SETPAGESIZE, 0, 1);

			for (i = 0; i < sounddrivers[i]; i++) {
				CheckDlgButton (hDlg, sounddrivers[i], (sounddrivermask & (1 << i)) ? TRUE : FALSE);
			}

			if (!volumeselection) {
				volumeselection = &workprefs.sound_volume_paula;
				volumeselectionindex = 0;
			}
			xSendDlgItemMessage (hDlg, IDC_SOUNDVOLUMESELECT, CB_RESETCONTENT, 0, 0L);
			xSendDlgItemMessage (hDlg, IDC_SOUNDVOLUMESELECT, CB_ADDSTRING, 0, (LPARAM)_T("Paula"));
			xSendDlgItemMessage (hDlg, IDC_SOUNDVOLUMESELECT, CB_ADDSTRING, 0, (LPARAM)_T("CD"));
			xSendDlgItemMessage (hDlg, IDC_SOUNDVOLUMESELECT, CB_ADDSTRING, 0, (LPARAM)_T("AHI"));
			xSendDlgItemMessage (hDlg, IDC_SOUNDVOLUMESELECT, CB_ADDSTRING, 0, (LPARAM)_T("MIDI"));
			xSendDlgItemMessage (hDlg, IDC_SOUNDVOLUMESELECT, CB_ADDSTRING, 0, (LPARAM)_T("Genlock"));
			xSendDlgItemMessage (hDlg, IDC_SOUNDVOLUMESELECT, CB_SETCURSEL, volumeselectionindex, 0);

			xSendDlgItemMessage (hDlg, IDC_SOUNDCARDLIST, CB_RESETCONTENT, 0, 0L);
			numdevs = enumerate_sound_devices ();
			for (card = 0; card < numdevs; card++) {
				TCHAR tmp[MAX_DPATH];
				int type = sound_devices[card]->type;
				_stprintf (tmp, _T("%s: %s"),
					type == SOUND_DEVICE_XAUDIO2 ? _T("XAudio2") : (type == SOUND_DEVICE_DS ? _T("DSOUND") : (type == SOUND_DEVICE_AL ? _T("OpenAL") : (type == SOUND_DEVICE_PA ? _T("PortAudio") : (type == SOUND_DEVICE_WASAPI ? _T("WASAPI") : _T("WASAPI EX"))))),
					sound_devices[card]->name);
				xSendDlgItemMessage (hDlg, IDC_SOUNDCARDLIST, CB_ADDSTRING, 0, (LPARAM)tmp);
			}
			if (numdevs == 0)
				workprefs.produce_sound = 0; /* No sound card in system, enable_for_sounddlg will accommodate this */

			pages[SOUND_ID] = hDlg;
			currentpage = SOUND_ID;
			update_soundgui (hDlg);
			values_to_sounddlg(hDlg);
			enable_for_sounddlg(hDlg);
			recursive--;
			return TRUE;
		}
	case WM_USER:
		recursive++;
		values_to_sounddlg (hDlg);
		enable_for_sounddlg (hDlg);
		recursive--;
		return TRUE;

	case WM_COMMAND:
		if (recursive > 0)
			break;
		recursive++;
		if (LOWORD(wParam) == IDC_SOUNDDRIVE) {
			if (HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == CBN_KILLFOCUS) {
				values_to_sounddlg(hDlg);
			}
		} else {
			values_from_sounddlg (hDlg);
		}
		enable_for_sounddlg(hDlg);
		recursive--;
		break;

	case WM_HSCROLL:
		if ((HWND)lParam == GetDlgItem (hDlg, IDC_SOUNDBUFFERRAM)) {
			int v = (int)SendMessage (GetDlgItem (hDlg, IDC_SOUNDBUFFERRAM), TBM_GETPOS, 0, 0);
			if (v >= 0) {
				if (v == 0)
					workprefs.sound_maxbsiz = 0;
				else
					workprefs.sound_maxbsiz = sndbufsizes[v - 1];
			}
		}
		workprefs.sound_volume_master = 100 - (int)SendMessage (GetDlgItem (hDlg, IDC_SOUNDVOLUME), TBM_GETPOS, 0, 0);
		(*volumeselection) = 100 - (int)SendMessage (GetDlgItem (hDlg, IDC_SOUNDVOLUMEEXT), TBM_GETPOS, 0, 0);
		workprefs.dfxclickvolume_empty[sounddrivesel] = 100 - (int)SendMessage (GetDlgItem (hDlg, IDC_SOUNDDRIVEVOLUME), TBM_GETPOS, 0, 0);
		workprefs.dfxclickvolume_disk[sounddrivesel] = 100 - (int)SendMessage (GetDlgItem (hDlg, IDC_SOUNDDRIVEVOLUMEX), TBM_GETPOS, 0, 0);
		update_soundgui (hDlg);
		break;
	}
	return FALSE;
}

#ifdef FILESYS

struct cddlg_vals
{
	struct uaedev_config_info ci;
};
struct tapedlg_vals
{
	struct uaedev_config_info ci;
};
struct fsvdlg_vals
{
	struct uaedev_config_info ci;
	int rdb;
};
struct hfdlg_vals
{
	struct uaedev_config_info ci;
	bool original;
	uae_u64 size;
	uae_u32 dostype;
	int forcedcylinders;
	bool rdb;
};

static struct cddlg_vals current_cddlg;
static struct tapedlg_vals current_tapedlg;
static struct fsvdlg_vals current_fsvdlg;
static struct hfdlg_vals current_hfdlg;
static int archivehd;

static void hardfile_testrdb (struct hfdlg_vals *hdf)
{
	uae_u8 id[512];
	int i;
	struct hardfiledata hfd;
	uae_u32 error = 0;

	memset (id, 0, sizeof id);
	memset (&hfd, 0, sizeof hfd);
	hfd.ci.readonly = true;
	hfd.ci.blocksize = 512;
	hdf->rdb = 0;
	if (hdf_open (&hfd, current_hfdlg.ci.rootdir) > 0) {
		for (i = 0; i < 16; i++) {
			hdf_read_rdb (&hfd, id, i * 512, 512, &error);
			if (!error && i == 0 && !memcmp (id + 2, "CIS", 3)) {
				hdf->ci.controller_type = HD_CONTROLLER_TYPE_CUSTOM_FIRST;
				hdf->ci.controller_type_unit = 0;
				break;
			}
			bool babe = id[0] == 0xBA && id[1] == 0xBE; // A2090
			if (!error && (!memcmp (id, "RDSK\0\0\0", 7) || !memcmp (id, "CDSK\0\0\0", 7) || !memcmp (id, "DRKS\0\0", 6) ||
				(id[0] == 0x53 && id[1] == 0x10 && id[2] == 0x9b && id[3] == 0x13 && id[4] == 0 && id[5] == 0) || babe)) {
				// RDSK or ADIDE "encoded" RDSK
				int blocksize = 512;
				if (!babe)
					blocksize = (id[16] << 24)  | (id[17] << 16) | (id[18] << 8) | (id[19] << 0);
				hdf->ci.cyls = hdf->ci.highcyl = hdf->forcedcylinders = 0;
				hdf->ci.sectors = 0;
				hdf->ci.surfaces = 0;
				hdf->ci.reserved = 0;
				hdf->ci.filesys[0] = 0;
				hdf->ci.bootpri = 0;
				hdf->ci.devname[0] = 0;
				if (blocksize >= 512)
					hdf->ci.blocksize = blocksize;
				hdf->rdb = 1;
				break;
			}
		}
		hdf_close (&hfd);
	}
}

static void default_fsvdlg (struct fsvdlg_vals *f)
{
	memset (f, 0, sizeof (struct fsvdlg_vals));
	f->ci.type = UAEDEV_DIR;
}
static void default_tapedlg (struct tapedlg_vals *f)
{
	memset (f, 0, sizeof (struct tapedlg_vals));
	f->ci.type = UAEDEV_TAPE;
}
static void default_hfdlg (struct hfdlg_vals *f, bool rdb)
{
	int ctrl = f->ci.controller_type;
	int unit = f->ci.controller_unit;
	memset (f, 0, sizeof (struct hfdlg_vals));
	uci_set_defaults (&f->ci, rdb);
	f->original = true;
	f->ci.type = UAEDEV_HDF;
	f->ci.controller_type = ctrl;
	f->ci.controller_unit = unit;
	f->ci.unit_feature_level = 1;
}
static void default_rdb_hfdlg (struct hfdlg_vals *f, const TCHAR *filename)
{
	default_hfdlg (f, true);
	_tcscpy (current_hfdlg.ci.rootdir, filename);
	hardfile_testrdb (f);
}

static void volumeselectfile (HWND hDlg, int setout)
{
	TCHAR directory_path[MAX_DPATH];
	_tcscpy (directory_path, current_fsvdlg.ci.rootdir);
	if (directory_path[0] == 0) {
		int out = sizeof directory_path / sizeof (TCHAR);
		regquerystr (NULL, _T("FilesystemFilePath"), directory_path, &out);
	}
	if (DiskSelection (hDlg, 0, 14, &workprefs, NULL, directory_path)) {
		TCHAR *s = filesys_createvolname (NULL, directory_path, NULL, _T("Harddrive"));
		SetDlgItemText (hDlg, IDC_PATH_NAME, directory_path);
		SetDlgItemText (hDlg, IDC_VOLUME_NAME, s);
		xfree (s);
		CheckDlgButton (hDlg, IDC_FS_RW, FALSE);
		ew (hDlg, IDC_FS_RW, FALSE);
		archivehd = 1;
		TCHAR *p = _tcsrchr (directory_path, '\\');
		if (p) {
			TCHAR t = p[1];
			p[1] = 0;
			regsetstr (NULL, _T("FilesystemFilePath"), directory_path);
			p[1] = t;
		}
		if (setout)
			_tcscpy (current_fsvdlg.ci.rootdir, directory_path);
	}
}
static void volumeselectdir (HWND hDlg, int newdir, int setout)
{
	const GUID volumeguid = { 0x1df05121, 0xcc08, 0x46ea, { 0x80, 0x3f, 0x98, 0x3c, 0x54, 0x88, 0x53, 0x76 } };
	TCHAR szTitle[MAX_DPATH];
	TCHAR directory_path[MAX_DPATH];

	_tcscpy (directory_path, current_fsvdlg.ci.rootdir);
	if (!newdir) {
		if (directory_path[0] == 0) {
			int out = sizeof directory_path / sizeof (TCHAR);
			regquerystr (NULL, _T("FilesystemDirectoryPath"), directory_path, &out);
		}
		WIN32GUI_LoadUIString (IDS_SELECTFILESYSROOT, szTitle, MAX_DPATH);
		if (DirectorySelection (hDlg, &volumeguid, directory_path)) {
			newdir = 1;
			DISK_history_add (directory_path, -1, HISTORY_DIR, 0);
			regsetstr (NULL, _T("FilesystemDirectoryPath"), directory_path);
		}
	}
	if (newdir) {
		SetDlgItemText (hDlg, IDC_PATH_NAME, directory_path);
		ew (hDlg, IDC_FS_RW, TRUE);
		archivehd = 0;
		if (setout)
			_tcscpy(current_fsvdlg.ci.rootdir, directory_path);
	}
}

static INT_PTR CALLBACK VolumeSettingsProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;

	bool handled;
	INT_PTR v = commonproc(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return v;
	}

	switch (msg) {
	case WM_DESTROY:
		PostQuitMessage(0);
		return TRUE;
	case WM_CLOSE:
		CustomDialogClose(hDlg, 0);
		return TRUE;
	case WM_INITDIALOG:
		{
			archivehd = -1;
			if (my_existsfile (current_fsvdlg.ci.rootdir))
				archivehd = 1;
			else if (my_existsdir (current_fsvdlg.ci.rootdir))
				archivehd = 0;
			recursive++;
			setautocomplete (hDlg, IDC_PATH_NAME);
			addhistorymenu(hDlg, current_fsvdlg.ci.rootdir, IDC_PATH_NAME, HISTORY_DIR, false, -1);
			SetDlgItemText (hDlg, IDC_VOLUME_NAME, current_fsvdlg.ci.volname);
			SetDlgItemText (hDlg, IDC_VOLUME_DEVICE, current_fsvdlg.ci.devname);
			SetDlgItemInt (hDlg, IDC_VOLUME_BOOTPRI, current_fsvdlg.ci.bootpri, TRUE);
			if (archivehd > 0)
				current_fsvdlg.ci.readonly = true;
			CheckDlgButton (hDlg, IDC_FS_RW, !current_fsvdlg.ci.readonly);
			CheckDlgButton (hDlg, IDC_FS_AUTOBOOT, ISAUTOBOOT(&current_fsvdlg.ci));
			ew (hDlg, IDC_FS_RW, archivehd <= 0);
			recursive--;
		}
		return TRUE;

	case WM_CONTEXTMENU:
		if (GetDlgCtrlID ((HWND)wParam) == IDC_FS_SELECT_FILE) {
			TCHAR *s = favoritepopup (hDlg);
			if (s) {
				_tcscpy (current_fsvdlg.ci.rootdir, s);
				xfree (s);
				volumeselectfile (hDlg,  0);
			}
		} else if (GetDlgCtrlID ((HWND)wParam) == IDC_FS_SELECT_DIR) {
			TCHAR *s = favoritepopup (hDlg);
			if (s) {
				_tcscpy (current_fsvdlg.ci.rootdir, s);
				xfree (s);
				volumeselectdir (hDlg, 1, 0);
			}
		}
		break;

	case WM_COMMAND:

		if (HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == CBN_KILLFOCUS) {
			switch (LOWORD(wParam))
			{
			case IDC_PATH_NAME:
			getcomboboxtext(hDlg, IDC_PATH_NAME, current_fsvdlg.ci.rootdir, sizeof current_fsvdlg.ci.rootdir / sizeof(TCHAR));
			break;
			}
		}
		if (HIWORD(wParam) == EN_UPDATE || HIWORD(wParam) == EN_KILLFOCUS) {
			switch (LOWORD(wParam))
			{
			case IDC_VOLUME_NAME:
			GetDlgItemText(hDlg, IDC_VOLUME_NAME, current_fsvdlg.ci.volname, sizeof current_fsvdlg.ci.volname / sizeof(TCHAR));
			break;
			case IDC_VOLUME_DEVICE:
			GetDlgItemText(hDlg, IDC_VOLUME_DEVICE, current_fsvdlg.ci.devname, sizeof current_fsvdlg.ci.devname / sizeof(TCHAR));
			break;
			}
		}
		if (recursive)
			break;
		recursive++;
		if (HIWORD (wParam) == BN_CLICKED) {
			switch (LOWORD (wParam))
			{
			case IDC_FS_SELECT_EJECT:
				SetDlgItemText (hDlg, IDC_PATH_NAME, _T(""));
				SetDlgItemText (hDlg, IDC_VOLUME_NAME, _T(""));
				current_fsvdlg.ci.rootdir[0] = 0;
				current_fsvdlg.ci.volname[0] = 0;
				CheckDlgButton (hDlg, IDC_FS_RW, TRUE);
				ew (hDlg, IDC_FS_RW, TRUE);
				archivehd = -1;
				break;
			case IDC_FS_SELECT_FILE:
				volumeselectfile (hDlg, 1);
				break;
			case IDC_FS_SELECT_DIR:
				volumeselectdir (hDlg, 0, 1);
				break;
			case IDOK:
				CustomDialogClose(hDlg, -1);
				recursive = 0;
				return TRUE;
			case IDCANCEL:
				CustomDialogClose(hDlg, 0);
				recursive = 0;
				return TRUE;
			}
		}
		current_fsvdlg.ci.readonly = !ischecked (hDlg, IDC_FS_RW);
		current_fsvdlg.ci.bootpri = GetDlgItemInt (hDlg, IDC_VOLUME_BOOTPRI, NULL, TRUE);
		if(LOWORD (wParam) == IDC_FS_AUTOBOOT) {
			if (!ischecked (hDlg, IDC_FS_AUTOBOOT)) {
				current_fsvdlg.ci.bootpri = BOOTPRI_NOAUTOBOOT;
			} else {
				current_fsvdlg.ci.bootpri = 0;
			}
			SetDlgItemInt (hDlg, IDC_VOLUME_BOOTPRI, current_fsvdlg.ci.bootpri, TRUE);
		}
		recursive--;
		break;
	}
	return FALSE;
}

STATIC_INLINE bool is_hdf_rdb (void)
{
	return current_hfdlg.ci.sectors == 0 && current_hfdlg.ci.surfaces == 0 && current_hfdlg.ci.reserved == 0;
}

static int hdmenutable[256];

static void sethardfilegeo(HWND hDlg)
{
	if (current_hfdlg.ci.geometry[0]) {
		current_hfdlg.ci.physical_geometry = true;
		setchecked(hDlg, IDC_HDF_PHYSGEOMETRY, TRUE);
		ew(hDlg, IDC_HDF_PHYSGEOMETRY, FALSE);
		get_hd_geometry(&current_hfdlg.ci);
	} else if (current_hfdlg.ci.chs) {
		current_hfdlg.ci.physical_geometry = true;
		setchecked(hDlg, IDC_HDF_PHYSGEOMETRY, TRUE);
		ew(hDlg, IDC_HDF_PHYSGEOMETRY, FALSE);
		ew(hDlg, IDC_SECTORS, FALSE);
		ew(hDlg, IDC_SECTORS, FALSE);
		ew(hDlg, IDC_RESERVED, FALSE);
		ew(hDlg, IDC_BLOCKSIZE, FALSE);
	} else {
		ew (hDlg, IDC_HDF_PHYSGEOMETRY, TRUE);
	}
}

static void sethardfiletypes(HWND hDlg)
{
	bool ide = current_hfdlg.ci.controller_type >= HD_CONTROLLER_TYPE_IDE_FIRST && current_hfdlg.ci.controller_type <= HD_CONTROLLER_TYPE_IDE_LAST;
	bool scsi = current_hfdlg.ci.controller_type >= HD_CONTROLLER_TYPE_SCSI_FIRST && current_hfdlg.ci.controller_type <= HD_CONTROLLER_TYPE_SCSI_LAST;
	ew(hDlg, IDC_HDF_CONTROLLER_TYPE, ide);
	ew(hDlg, IDC_HDF_FEATURE_LEVEL, ide || scsi);
	if (!ide) {
		current_hfdlg.ci.controller_media_type = 0;
	}
	if (current_hfdlg.ci.controller_media_type && current_hfdlg.ci.unit_feature_level == 0)
		current_hfdlg.ci.unit_feature_level = 1;
	xSendDlgItemMessage(hDlg, IDC_HDF_CONTROLLER_TYPE, CB_SETCURSEL, current_hfdlg.ci.controller_media_type, 0);
	xSendDlgItemMessage(hDlg, IDC_HDF_FEATURE_LEVEL, CB_SETCURSEL, current_hfdlg.ci.unit_feature_level, 0);
}

static void sethd(HWND hDlg)
{
	bool rdb = is_hdf_rdb ();
	bool physgeo = (rdb && ischecked(hDlg, IDC_HDF_PHYSGEOMETRY)) || current_hfdlg.ci.chs;
	bool enablegeo = (!rdb || (physgeo && current_hfdlg.ci.geometry[0] == 0)) && !current_hfdlg.ci.chs;
	const struct expansionromtype *ert = get_unit_expansion_rom(current_hfdlg.ci.controller_type);
	if (ert && current_hfdlg.ci.controller_unit >= 8) {
		if (!_tcscmp(ert->name, _T("a2091"))) {
			current_hfdlg.ci.unit_feature_level = HD_LEVEL_SASI_CHS;
		} else if (!_tcscmp(ert->name, _T("a2090a"))) {
			current_hfdlg.ci.unit_feature_level = HD_LEVEL_SCSI_1;
		}
	}
	if (!physgeo)
		current_hfdlg.ci.physical_geometry = false;
	ew(hDlg, IDC_SECTORS, enablegeo);
	ew(hDlg, IDC_HEADS, enablegeo);
	ew(hDlg, IDC_RESERVED, enablegeo);
	ew(hDlg, IDC_BLOCKSIZE, enablegeo);
}

static void setharddrive(HWND hDlg)
{
	sethardfilegeo(hDlg);
	sethd(hDlg);
	ew(hDlg,IDC_BLOCKSIZE, FALSE);
	SetDlgItemInt (hDlg, IDC_SECTORS, current_hfdlg.ci.psecs, FALSE);
	SetDlgItemInt (hDlg, IDC_HEADS, current_hfdlg.ci.pheads, FALSE);
	SetDlgItemInt (hDlg, IDC_RESERVED, current_hfdlg.ci.pcyls, FALSE);
	SetDlgItemInt (hDlg, IDC_BLOCKSIZE, current_hfdlg.ci.blocksize, FALSE);
	sethardfiletypes(hDlg);
}

static void sethardfile (HWND hDlg)
{
	sethardfilegeo(hDlg);

	bool ide = current_hfdlg.ci.controller_type >= HD_CONTROLLER_TYPE_IDE_FIRST && current_hfdlg.ci.controller_type <= HD_CONTROLLER_TYPE_IDE_LAST;
	bool scsi = current_hfdlg.ci.controller_type >= HD_CONTROLLER_TYPE_SCSI_FIRST && current_hfdlg.ci.controller_type <= HD_CONTROLLER_TYPE_SCSI_LAST;
	bool rdb = is_hdf_rdb ();
	bool physgeo = rdb && ischecked(hDlg, IDC_HDF_PHYSGEOMETRY);
	bool disables = !rdb || (rdb && current_hfdlg.ci.controller_type == HD_CONTROLLER_TYPE_UAE);
	bool rdsk = current_hfdlg.rdb;

	sethd(hDlg);
	if (!disables)
		current_hfdlg.ci.bootpri = 0;
	SetDlgItemText (hDlg, IDC_PATH_NAME, current_hfdlg.ci.rootdir);
	SetDlgItemText (hDlg, IDC_PATH_FILESYS, current_hfdlg.ci.filesys);
	SetDlgItemText (hDlg, IDC_HARDFILE_DEVICE, current_hfdlg.ci.devname);
	SetDlgItemInt (hDlg, IDC_SECTORS, rdb ? current_hfdlg.ci.psecs : current_hfdlg.ci.sectors, FALSE);
	SetDlgItemInt (hDlg, IDC_HEADS, rdb ? current_hfdlg.ci.pheads : current_hfdlg.ci.surfaces, FALSE);
	SetDlgItemInt (hDlg, IDC_RESERVED, rdb ? current_hfdlg.ci.pcyls : current_hfdlg.ci.reserved, FALSE);
	SetDlgItemInt (hDlg, IDC_BLOCKSIZE, current_hfdlg.ci.blocksize, FALSE);
	SetDlgItemInt (hDlg, IDC_HARDFILE_BOOTPRI, current_hfdlg.ci.bootpri, TRUE);
	CheckDlgButton (hDlg, IDC_HDF_RW, !current_hfdlg.ci.readonly);
	CheckDlgButton (hDlg, IDC_HDF_AUTOBOOT, ISAUTOBOOT(&current_hfdlg.ci));
	CheckDlgButton (hDlg, IDC_HDF_DONOTMOUNT, !ISAUTOMOUNT(&current_hfdlg.ci));
	ew (hDlg, IDC_HDF_AUTOBOOT, disables);
	ew (hDlg, IDC_HDF_DONOTMOUNT, disables);
	hide (hDlg, IDC_HDF_AUTOBOOT, !disables);
	hide (hDlg, IDC_HDF_DONOTMOUNT, !disables);
	hide (hDlg, IDC_HARDFILE_BOOTPRI, !disables);
	hide (hDlg, IDC_HARDFILE_BOOTPRI_TEXT, !disables);
	hide (hDlg, IDC_HDF_PHYSGEOMETRY, !rdb);
	if (rdb) {
		ew(hDlg, IDC_HDF_RDB, !rdsk);
		setchecked(hDlg, IDC_HDF_RDB, true);
	} else {
		ew(hDlg, IDC_HDF_RDB, TRUE);
		setchecked(hDlg, IDC_HDF_RDB, false);
	}
	if (!rdb) {
		setchecked(hDlg, IDC_HDF_PHYSGEOMETRY, false);
	}
	hide(hDlg, IDC_RESERVED_TEXT, rdb);
	hide(hDlg, IDC_CYLINDERS_TEXT, !rdb);
	gui_set_string_cursor(hdmenutable, hDlg, IDC_HDF_CONTROLLER, current_hfdlg.ci.controller_type +  current_hfdlg.ci.controller_type_unit * HD_CONTROLLER_NEXT_UNIT);
	xSendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER_UNIT, CB_SETCURSEL, current_hfdlg.ci.controller_unit, 0);
	sethardfiletypes(hDlg);
}

static void addhdcontroller(HWND hDlg, const struct expansionromtype *erc, int *hdmenutable, int firstid, int flags)
{
	TCHAR name[MAX_DPATH];
	name[0] = 0;
	if (erc->friendlymanufacturer && _tcsicmp(erc->friendlymanufacturer, erc->friendlyname)) {
		_tcscat(name, erc->friendlymanufacturer);
		_tcscat(name, _T(" "));
	}
	_tcscat(name, erc->friendlyname);
	if (workprefs.cpuboard_type && erc->romtype == ROMTYPE_CPUBOARD) {
		const struct cpuboardsubtype *cbt = &cpuboards[workprefs.cpuboard_type].subtypes[workprefs.cpuboard_subtype];
		if (!(cbt->deviceflags & flags))
			return;
		_tcscat(name, _T(" ("));
		_tcscat(name, cbt->name);
		_tcscat(name, _T(")"));
	}
	if (get_boardromconfig(&workprefs, erc->romtype, NULL) || get_boardromconfig(&workprefs, erc->romtype_extra, NULL)) {
		gui_add_string(hdmenutable, hDlg, IDC_HDF_CONTROLLER, firstid, name);
		for (int j = 1; j < MAX_DUPLICATE_EXPANSION_BOARDS; j++) {
			if (is_board_enabled(&workprefs, erc->romtype, j)) {
				TCHAR tmp[MAX_DPATH];
				_stprintf(tmp, _T("%s [%d]"), name, j + 1);
				gui_add_string(hdmenutable, hDlg, IDC_HDF_CONTROLLER, firstid + j * HD_CONTROLLER_NEXT_UNIT, tmp);
			}
		}
	}
}

static void inithdcontroller (HWND hDlg, int ctype, int ctype_unit, int devtype, bool media)
{
	hdmenutable[0] = -1;
	
	xSendDlgItemMessage(hDlg, IDC_HDF_CONTROLLER, CB_RESETCONTENT, 0, 0);

	gui_add_string(hdmenutable, hDlg, IDC_HDF_CONTROLLER, HD_CONTROLLER_TYPE_UAE, _T("UAE (uaehf.device)"));

	gui_add_string(hdmenutable, hDlg, IDC_HDF_CONTROLLER, 0, _T(""));
	gui_add_string(hdmenutable, hDlg, IDC_HDF_CONTROLLER, HD_CONTROLLER_TYPE_IDE_AUTO, _T("IDE (Auto)"));

	for (int i = 0; expansionroms[i].name; i++) {
		const struct expansionromtype *erc = &expansionroms[i];
		if (erc->deviceflags & EXPANSIONTYPE_IDE) {
			addhdcontroller(hDlg, erc, hdmenutable, HD_CONTROLLER_TYPE_IDE_EXPANSION_FIRST + i, EXPANSIONTYPE_IDE);
		}
	}

	gui_add_string(hdmenutable, hDlg, IDC_HDF_CONTROLLER, 0, _T(""));
	gui_add_string(hdmenutable, hDlg, IDC_HDF_CONTROLLER, HD_CONTROLLER_TYPE_SCSI_AUTO, _T("SCSI (Auto)"));

	for (int i = 0; expansionroms[i].name; i++) {
		const struct expansionromtype *erc = &expansionroms[i];
		if (erc->deviceflags & EXPANSIONTYPE_SCSI) {
			addhdcontroller(hDlg, erc, hdmenutable, HD_CONTROLLER_TYPE_SCSI_EXPANSION_FIRST + i, EXPANSIONTYPE_SCSI);
		}
	}

#if 0
	gui_add_string(hdmenutable, hDlg, IDC_HDF_CONTROLLER, 0, _T(""));
	for (int i = 0; expansionroms[i].name; i++) {
		const struct expansionromtype *erc = &expansionroms[i];
		if ((erc->romtype & ROMTYPE_MASK) == ROMTYPE_MB_PCMCIA) {
			addhdcontroller(hDlg, erc, hdmenutable, HD_CONTROLLER_TYPE_CUSTOM_FIRST + i, 0);
			ctype_unit = 0;
		}
	}
#endif

	gui_add_string(hdmenutable, hDlg, IDC_HDF_CONTROLLER, 0, _T(""));
	for (int i = 0; expansionroms[i].name; i++) {
		const struct expansionromtype *erc = &expansionroms[i];
		if (erc->deviceflags & EXPANSIONTYPE_CUSTOMDISK) {
			addhdcontroller(hDlg, erc, hdmenutable, HD_CONTROLLER_TYPE_CUSTOM_FIRST + i, EXPANSIONTYPE_CUSTOMDISK);
			break;
		}
	}

	gui_set_string_cursor(hdmenutable, hDlg, IDC_HDF_CONTROLLER, ctype + ctype_unit * HD_CONTROLLER_NEXT_UNIT);

	xSendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER_UNIT, CB_RESETCONTENT, 0, 0);
	if (ctype >= HD_CONTROLLER_TYPE_IDE_FIRST && ctype <= HD_CONTROLLER_TYPE_IDE_LAST) {
		const struct expansionromtype *ert = get_unit_expansion_rom(ctype);
		int ports = 2 + (ert ? ert->extrahdports : 0);
		for (int i = 0; i < ports; i += 2) {
			TCHAR tmp[100];
			_stprintf(tmp, _T("%d"), i + 0);
			xSendDlgItemMessage(hDlg, IDC_HDF_CONTROLLER_UNIT, CB_ADDSTRING, 0, (LPARAM)tmp);
			_stprintf(tmp, _T("%d"), i + 1);
			xSendDlgItemMessage(hDlg, IDC_HDF_CONTROLLER_UNIT, CB_ADDSTRING, 0, (LPARAM)tmp);
		}
		if (media)
			ew(hDlg, IDC_HDF_CONTROLLER_UNIT, TRUE);
	} else if (ctype >= HD_CONTROLLER_TYPE_SCSI_FIRST && ctype <= HD_CONTROLLER_TYPE_SCSI_LAST) {
		const struct expansionromtype *ert = get_unit_expansion_rom(ctype);
		xSendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER_UNIT, CB_ADDSTRING, 0, (LPARAM)_T("0"));
		xSendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER_UNIT, CB_ADDSTRING, 0, (LPARAM)_T("1"));
		if (!ert || !(ert->deviceflags & (EXPANSIONTYPE_SASI | EXPANSIONTYPE_CUSTOM)) ) {
			xSendDlgItemMessage(hDlg, IDC_HDF_CONTROLLER_UNIT, CB_ADDSTRING, 0, (LPARAM)_T("2"));
			xSendDlgItemMessage(hDlg, IDC_HDF_CONTROLLER_UNIT, CB_ADDSTRING, 0, (LPARAM)_T("3"));
			xSendDlgItemMessage(hDlg, IDC_HDF_CONTROLLER_UNIT, CB_ADDSTRING, 0, (LPARAM)_T("4"));
			xSendDlgItemMessage(hDlg, IDC_HDF_CONTROLLER_UNIT, CB_ADDSTRING, 0, (LPARAM)_T("5"));
			xSendDlgItemMessage(hDlg, IDC_HDF_CONTROLLER_UNIT, CB_ADDSTRING, 0, (LPARAM)_T("6"));
			xSendDlgItemMessage(hDlg, IDC_HDF_CONTROLLER_UNIT, CB_ADDSTRING, 0, (LPARAM)_T("7"));
			if (devtype == UAEDEV_HDF && ert && !_tcscmp(ert->name, _T("a2091")))
				xSendDlgItemMessage(hDlg, IDC_HDF_CONTROLLER_UNIT, CB_ADDSTRING, 0, (LPARAM)_T("XT"));
			if (devtype == UAEDEV_HDF && ert && !_tcscmp(ert->name, _T("a2090a"))) {
				xSendDlgItemMessage(hDlg, IDC_HDF_CONTROLLER_UNIT, CB_ADDSTRING, 0, (LPARAM)_T("ST-506 #1"));
				xSendDlgItemMessage(hDlg, IDC_HDF_CONTROLLER_UNIT, CB_ADDSTRING, 0, (LPARAM)_T("ST-506 #2"));
			}
		}
		if (media)
			ew(hDlg, IDC_HDF_CONTROLLER_UNIT, TRUE);
	} else if (ctype >= HD_CONTROLLER_TYPE_CUSTOM_FIRST && ctype <= HD_CONTROLLER_TYPE_CUSTOM_LAST) {
		ew(hDlg, IDC_HDF_CONTROLLER_UNIT, FALSE);
	} else if (ctype == HD_CONTROLLER_TYPE_UAE) {
		for (int i = 0; i < MAX_FILESYSTEM_UNITS; i++) {
			TCHAR tmp[100];
			_stprintf(tmp, _T("%d"), i);
			xSendDlgItemMessage(hDlg, IDC_HDF_CONTROLLER_UNIT, CB_ADDSTRING, 0, (LPARAM)tmp);
		}
		if (media)
			ew(hDlg, IDC_HDF_CONTROLLER_UNIT, TRUE);
	} else {
		ew(hDlg, IDC_HDF_CONTROLLER_UNIT, FALSE);
	}

	xSendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER_TYPE, CB_RESETCONTENT, 0, 0);
	xSendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER_TYPE, CB_ADDSTRING, 0, (LPARAM)_T("HD"));
	xSendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER_TYPE, CB_ADDSTRING, 0, (LPARAM)_T("CF"));

	xSendDlgItemMessage (hDlg, IDC_HDF_FEATURE_LEVEL, CB_RESETCONTENT, 0, 0);
	if (ctype >= HD_CONTROLLER_TYPE_IDE_FIRST && ctype <= HD_CONTROLLER_TYPE_IDE_LAST) {
		xSendDlgItemMessage (hDlg, IDC_HDF_FEATURE_LEVEL, CB_ADDSTRING, 0, (LPARAM)_T("ATA-1"));
		xSendDlgItemMessage (hDlg, IDC_HDF_FEATURE_LEVEL, CB_ADDSTRING, 0, (LPARAM)_T("ATA-2+"));
		xSendDlgItemMessage (hDlg, IDC_HDF_FEATURE_LEVEL, CB_ADDSTRING, 0, (LPARAM)_T("ATA-2+ Strict"));
	} else if (ctype >= HD_CONTROLLER_TYPE_SCSI_FIRST && ctype <= HD_CONTROLLER_TYPE_SCSI_LAST) {
		const struct expansionromtype *ert = get_unit_expansion_rom(ctype);
		xSendDlgItemMessage (hDlg, IDC_HDF_FEATURE_LEVEL, CB_ADDSTRING, 0, (LPARAM)_T("SCSI-1"));
		xSendDlgItemMessage(hDlg, IDC_HDF_FEATURE_LEVEL, CB_ADDSTRING, 0, (LPARAM)_T("SCSI-2"));
		if (ert && (ert->deviceflags & (EXPANSIONTYPE_CUSTOM | EXPANSIONTYPE_CUSTOM_SECONDARY | EXPANSIONTYPE_SASI))) {
			xSendDlgItemMessage(hDlg, IDC_HDF_FEATURE_LEVEL, CB_ADDSTRING, 0, (LPARAM)_T("SASI"));
			xSendDlgItemMessage(hDlg, IDC_HDF_FEATURE_LEVEL, CB_ADDSTRING, 0, (LPARAM)_T("SASI CHS"));
		}
	}
}

static void inithardfile (HWND hDlg, bool media)
{
	TCHAR tmp[MAX_DPATH];

	ew (hDlg, IDC_HF_DOSTYPE, FALSE);
	ew (hDlg, IDC_HF_CREATE, FALSE);
	inithdcontroller (hDlg, current_hfdlg.ci.controller_type, current_hfdlg.ci.controller_type_unit, UAEDEV_HDF, media);
	xSendDlgItemMessage (hDlg, IDC_HF_TYPE, CB_RESETCONTENT, 0, 0);
	WIN32GUI_LoadUIString (IDS_HF_FS_CUSTOM, tmp, sizeof (tmp) / sizeof (TCHAR));
	xSendDlgItemMessage (hDlg, IDC_HF_TYPE, CB_ADDSTRING, 0, (LPARAM)_T("RDB/OFS/FFS"));
	xSendDlgItemMessage (hDlg, IDC_HF_TYPE, CB_ADDSTRING, 0, (LPARAM)_T("PFS3"));
	xSendDlgItemMessage (hDlg, IDC_HF_TYPE, CB_ADDSTRING, 0, (LPARAM)_T("PDS3"));
	xSendDlgItemMessage (hDlg, IDC_HF_TYPE, CB_ADDSTRING, 0, (LPARAM)_T("SFS"));
	xSendDlgItemMessage (hDlg, IDC_HF_TYPE, CB_ADDSTRING, 0, (LPARAM)tmp);
	xSendDlgItemMessage (hDlg, IDC_HF_TYPE, CB_SETCURSEL, 0, 0);
}

static void sethfdostype (HWND hDlg, int idx)
{
	switch (idx)
	{
	case 1:
		SetDlgItemText (hDlg, IDC_HF_DOSTYPE, _T("0x50465300"));
	break;
	case 2:
		SetDlgItemText (hDlg, IDC_HF_DOSTYPE, _T("0x50445300"));
	break;
	case 3:
		SetDlgItemText (hDlg, IDC_HF_DOSTYPE, _T("0x53465300"));
	break;
	default:
		SetDlgItemText (hDlg, IDC_HF_DOSTYPE, _T(""));
	break;
	}
}

static void updatehdfinfo(HWND hDlg, bool force, bool defaults, bool realdrive)
{
	uae_u8 id[512] = { 0 };
	uae_u64 bsize;
	uae_u32 blocks, cyls, i;
	TCHAR tmp[200], tmp2[200];
	TCHAR idtmp[17];
	bool phys = is_hdf_rdb();
	uae_u32 error = 0;

	bsize = 0;
	if (force) {
		bool open = false;
		bool gotrdb = false;
		int blocksize = 512;
		struct hardfiledata hfd;
		memset (id, 0, sizeof id);
		memset (&hfd, 0, sizeof hfd);
		hfd.ci.readonly = true;
		hfd.ci.blocksize = blocksize;
		current_hfdlg.size = 0;
		current_hfdlg.dostype = 0;
		if (hdf_open (&hfd, current_hfdlg.ci.rootdir) > 0) {
			open = true;
			for (i = 0; i < 16; i++) {
				hdf_read (&hfd, id, i * 512, 512, &error);
				bsize = hfd.virtsize;
				current_hfdlg.size = hfd.virtsize;
				if (!memcmp (id, "RDSK", 4) || !memcmp (id, "CDSK", 4)) {
					blocksize = (id[16] << 24)  | (id[17] << 16) | (id[18] << 8) | (id[19] << 0);
					gotrdb = true;
					break;
				}
			}
			if (i == 16) {
				hdf_read (&hfd, id, 0, 512, &error);
				current_hfdlg.dostype = (id[0] << 24) | (id[1] << 16) | (id[2] << 8) | (id[3] << 0);
			}
		}
		if (defaults) {
			if (blocksize > 512) {
				hfd.ci.blocksize = blocksize;
			}
		}
		if (hfd.ci.chs) {
			current_hfdlg.ci.physical_geometry = true;
			current_hfdlg.ci.chs = true;
			current_hfdlg.ci.pcyls = hfd.ci.pcyls;
			current_hfdlg.ci.pheads = hfd.ci.pheads;
			current_hfdlg.ci.psecs = hfd.ci.psecs;
		}
		if (!current_hfdlg.ci.physical_geometry) {
			if (current_hfdlg.ci.controller_type >= HD_CONTROLLER_TYPE_IDE_FIRST && current_hfdlg.ci.controller_type <= HD_CONTROLLER_TYPE_IDE_LAST) {
				getchspgeometry (bsize, &current_hfdlg.ci.pcyls, &current_hfdlg.ci.pheads, &current_hfdlg.ci.psecs, true);
			} else {
				getchspgeometry (bsize, &current_hfdlg.ci.pcyls, &current_hfdlg.ci.pheads, &current_hfdlg.ci.psecs, false);
			}
			if (defaults && !gotrdb && !realdrive) {
				gethdfgeometry(bsize, &current_hfdlg.ci);
				phys = false;
			}
		} else {
			current_hfdlg.forcedcylinders = current_hfdlg.ci.pcyls;
		}
		if (hDlg && (hfd.identity[0] || hfd.identity[1])) {
			TCHAR ident[256];
			int i;
			for (i = 0; i < 11; i++) {
				_stprintf(ident + i * 5, _T("%02X%02X."), hfd.identity[i * 2 + 1], hfd.identity[i * 2 + 0]);
			}
			ident[i * 5 - 1] = 0;
			SetDlgItemText(hDlg, IDC_HDFINFO3, ident);
		}
		hdf_close (&hfd);
	}

	if (current_hfdlg.ci.controller_type >= HD_CONTROLLER_TYPE_IDE_FIRST && current_hfdlg.ci.controller_type <= HD_CONTROLLER_TYPE_IDE_LAST) {
		if (current_hfdlg.ci.unit_feature_level == HD_LEVEL_ATA_1 && bsize >= 4 * (uae_u64)0x40000000)
			current_hfdlg.ci.unit_feature_level = HD_LEVEL_ATA_2;
	}

	cyls = phys ? current_hfdlg.ci.pcyls : current_hfdlg.forcedcylinders;
	int heads = phys ? current_hfdlg.ci.pheads : current_hfdlg.ci.surfaces;
	int secs = phys ? current_hfdlg.ci.psecs : current_hfdlg.ci.sectors;
	if (!cyls && current_hfdlg.ci.blocksize && secs && heads) {
		cyls = (uae_u32)(bsize / ((uae_u64)current_hfdlg.ci.blocksize * secs * heads));
	}
	blocks = cyls * (secs * heads);
	if (!blocks && current_hfdlg.ci.blocksize)
		blocks = (uae_u32)(bsize / current_hfdlg.ci.blocksize);
	if (current_hfdlg.ci.max_lba)
		blocks = (uae_u32)current_hfdlg.ci.max_lba;

	for (i = 0; i < sizeof (idtmp) / sizeof (TCHAR) - 1; i++) {
		TCHAR c = id[i];
		if (c < 32 || c > 126)
			c = '.';
		idtmp[i] = c;
		idtmp[i + 1] = 0;
	}

	tmp[0] = 0;
	if (bsize) {
		_stprintf (tmp2, _T(" %s [%02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X]"), idtmp,
			id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7],
			id[8], id[9], id[10], id[11], id[12], id[13], id[14], id[15]);
		if (!blocks) {
			_stprintf (tmp, _T("%uMB"), (unsigned int)(bsize / (1024 * 1024)));
		} else if (blocks && !cyls) {
			_stprintf (tmp, _T("%u blocks, %.1fMB"),
				blocks,
				(double)bsize / (1024.0 * 1024.0));		
		} else {
			_stprintf (tmp, _T("%u/%u/%u, %u/%u blocks, %.1fMB/%.1fMB"),
				cyls, heads, secs,
				blocks, (int)(bsize / current_hfdlg.ci.blocksize),
				(double)blocks * 1.0 * current_hfdlg.ci.blocksize / (1024.0 * 1024.0),
				(double)bsize / (1024.0 * 1024.0));
			if ((uae_u64)cyls * heads * secs > bsize / current_hfdlg.ci.blocksize) {
				_tcscat (tmp2, _T(" [Geometry larger than drive!]"));
			} else if (cyls > 65535) {
				_tcscat (tmp2, _T(" [Too many cyls]"));
			}
		}
		if (hDlg != NULL) {
			SetDlgItemText(hDlg, IDC_HDFINFO, tmp);
			SetDlgItemText(hDlg, IDC_HDFINFO2, tmp2);
		}
	}
}

static void hardfileselecthdf (HWND hDlg, TCHAR *newpath, bool ask, bool newhd)
{
	if (ask) {
		DiskSelection (hDlg, IDC_PATH_NAME, 2, &workprefs, NULL, newpath);
		GetDlgItemText (hDlg, IDC_PATH_NAME, current_hfdlg.ci.rootdir, sizeof current_hfdlg.ci.rootdir / sizeof (TCHAR));
		DISK_history_add(current_hfdlg.ci.rootdir, -1, HISTORY_HDF, 0);
	}
	fullpath (current_hfdlg.ci.rootdir, sizeof current_hfdlg.ci.rootdir / sizeof (TCHAR));
	if (newhd) {
		// Set RDB mode if IDE or SCSI
		if (current_hfdlg.ci.controller_type > 0) {
			current_hfdlg.ci.sectors = current_hfdlg.ci.reserved = current_hfdlg.ci.surfaces = 0;
		}
	}
	inithardfile (hDlg, true);
	hardfile_testrdb (&current_hfdlg);
	updatehdfinfo (hDlg, true, true, false);
	get_hd_geometry (&current_hfdlg.ci);
	updatehdfinfo (hDlg, false, false, false);
	sethardfile (hDlg);
}

static void hardfilecreatehdf (HWND hDlg, TCHAR *newpath)
{
	TCHAR hdfpath[MAX_DPATH];
	LRESULT res;
	uae_s64 setting = CalculateHardfileSize (hDlg);
	TCHAR dostype[16];
	GetDlgItemText (hDlg, IDC_HF_DOSTYPE, dostype, sizeof (dostype) / sizeof (TCHAR));
	res = xSendDlgItemMessage (hDlg, IDC_HF_TYPE, CB_GETCURSEL, 0, 0);
	if (res == 0)
		dostype[0] = 0;
	if (CreateHardFile (hDlg, setting, dostype, newpath, hdfpath)) {
		if (!current_hfdlg.ci.rootdir[0]) {
			fullpath (hdfpath, sizeof hdfpath / sizeof (TCHAR));
			_tcscpy (current_hfdlg.ci.rootdir, hdfpath);
		}
	}
	sethardfile (hDlg);
}

static INT_PTR CALLBACK TapeDriveSettingsProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;
	int posn, readonly;
	TCHAR tmp[MAX_DPATH];

	bool handled;
	INT_PTR v = commonproc(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return v;
	}

	switch (msg) {

	case WM_DESTROY:
		PostQuitMessage(0);
		return TRUE;
	case WM_CLOSE:
		CustomDialogClose(hDlg, 0);
		return TRUE;
	case WM_INITDIALOG:
		recursive++;
		inithdcontroller(hDlg, current_tapedlg.ci.controller_type, current_tapedlg.ci.controller_type_unit, UAEDEV_TAPE, current_tapedlg.ci.rootdir[0] != 0);
		xSendDlgItemMessage(hDlg, IDC_HDF_CONTROLLER_UNIT, CB_SETCURSEL, current_tapedlg.ci.controller_unit, 0);
		setautocomplete (hDlg, IDC_PATH_NAME);
		addhistorymenu(hDlg, current_tapedlg.ci.rootdir, IDC_PATH_NAME, HISTORY_TAPE, false, -1);
		readonly = !tape_can_write(current_tapedlg.ci.rootdir);
		CheckDlgButton (hDlg, IDC_TAPE_RW, current_tapedlg.ci.readonly == 0 && !readonly);
		ew (hDlg, IDC_TAPE_RW, !readonly);
		recursive--;
		customDlgType = IDD_TAPEDRIVE;
		customDlg = hDlg;
		return TRUE;
	case WM_COMMAND:
		if (HIWORD (wParam) == CBN_SELCHANGE || HIWORD (wParam) == CBN_KILLFOCUS)  {
			switch (LOWORD (wParam))
			{
			case IDC_PATH_NAME:
				if (getcomboboxtext(hDlg, IDC_PATH_NAME, tmp, sizeof tmp / sizeof(TCHAR))) {
					if (_tcscmp (tmp, current_tapedlg.ci.rootdir)) {
						_tcscpy (current_tapedlg.ci.rootdir, tmp);
						readonly = !tape_can_write(current_tapedlg.ci.rootdir);
						ew (hDlg, IDC_TAPE_RW, !readonly);
						if (readonly)
							CheckDlgButton (hDlg, IDC_TAPE_RW, FALSE);
					}
				}
				break;
			case IDC_HDF_CONTROLLER:
				posn = gui_get_string_cursor(hdmenutable, hDlg, IDC_HDF_CONTROLLER);
				if (posn != CB_ERR) {
					current_tapedlg.ci.controller_type = posn % HD_CONTROLLER_NEXT_UNIT;
					current_tapedlg.ci.controller_type_unit = posn / HD_CONTROLLER_NEXT_UNIT;
					inithdcontroller(hDlg, current_tapedlg.ci.controller_type, current_tapedlg.ci.controller_type_unit, UAEDEV_TAPE, current_tapedlg.ci.rootdir);
					xSendDlgItemMessage(hDlg, IDC_HDF_CONTROLLER_UNIT, CB_SETCURSEL, current_tapedlg.ci.controller_unit, 0);
				}
				break;
			case IDC_HDF_CONTROLLER_UNIT:
				posn = xSendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER_UNIT, CB_GETCURSEL, 0, 0);
				if (posn != CB_ERR) {
					current_tapedlg.ci.controller_unit = posn;
				}
				break;
			}
		}
		if (recursive)
			break;
		recursive++;
		switch (LOWORD (wParam))
		{
		case IDC_TAPE_EJECT:
			current_tapedlg.ci.rootdir[0] = 0;
			SetDlgItemText(hDlg, IDC_PATH_NAME, current_tapedlg.ci.rootdir);
			break;
		case IDC_TAPE_SELECT_FILE:
			DiskSelection (hDlg, IDC_PATH_NAME, 18, &workprefs, NULL, NULL);
			GetDlgItemText (hDlg, IDC_PATH_NAME, current_tapedlg.ci.rootdir, sizeof current_tapedlg.ci.rootdir / sizeof (TCHAR));
			DISK_history_add(current_tapedlg.ci.rootdir, -1, HISTORY_TAPE, 0);
			fullpath (current_tapedlg.ci.rootdir, sizeof current_tapedlg.ci.rootdir / sizeof (TCHAR));
			readonly = !tape_can_write(current_tapedlg.ci.rootdir);
			ew (hDlg, IDC_TAPE_RW, !readonly);
			if (readonly)
				CheckDlgButton (hDlg, IDC_TAPE_RW, FALSE);
			break;
		case IDC_TAPE_SELECT_DIR:
		{
			const GUID volumeguid = { 0xb95772a5, 0x8444, 0x48d8, { 0xab, 0x9a, 0xef, 0x3c, 0x62, 0x11, 0x3a, 0x37 } };
			TCHAR directory_path[MAX_DPATH];
			_tcscpy (directory_path, current_tapedlg.ci.rootdir);
			if (directory_path[0] == 0) {
				int out = sizeof directory_path / sizeof (TCHAR);
				regquerystr (NULL, _T("TapeDirectoryPath"), directory_path, &out);
			}
			if (DirectorySelection (hDlg, &volumeguid, directory_path)) {
				regsetstr (NULL, _T("TapeDirectoryPath"), directory_path);
				SetDlgItemText (hDlg, IDC_PATH_NAME, directory_path);
			}
			_tcscpy (current_tapedlg.ci.rootdir, directory_path);
			DISK_history_add(current_tapedlg.ci.rootdir, -1, HISTORY_TAPE, 0);
			readonly = !tape_can_write(current_tapedlg.ci.rootdir);
			ew (hDlg, IDC_TAPE_RW, !readonly);
			if (readonly)
				CheckDlgButton (hDlg, IDC_TAPE_RW, FALSE);
			break;
		}
		case IDOK:
			CustomDialogClose(hDlg, -1);
			recursive = 0;
			return TRUE;
		case IDCANCEL:
			CustomDialogClose(hDlg, 0);
			recursive = 0;
			return TRUE;
		}
		current_tapedlg.ci.readonly = !ischecked (hDlg, IDC_TAPE_RW);
		recursive--;
		break;
	}
	return FALSE;
}

static INT_PTR CALLBACK CDDriveSettingsProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;
	int posn;

	bool handled;
	INT_PTR v = commonproc(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return v;
	}

	switch (msg) {
	case WM_DESTROY:
		PostQuitMessage(0);
		return TRUE;
	case WM_CLOSE:
		CustomDialogClose(hDlg, 0);
		return TRUE;

	case WM_INITDIALOG:
		recursive++;
		if (current_cddlg.ci.controller_type == HD_CONTROLLER_TYPE_UAE)
			current_cddlg.ci.controller_type = (is_board_enabled(&workprefs, ROMTYPE_A2091, 0) ||
				is_board_enabled(&workprefs, ROMTYPE_GVPS2, 0) || is_board_enabled(&workprefs, ROMTYPE_A4091, 0) ||
			(workprefs.cs_mbdmac & 3)) ? HD_CONTROLLER_TYPE_SCSI_AUTO : HD_CONTROLLER_TYPE_IDE_AUTO;
		inithdcontroller(hDlg, current_cddlg.ci.controller_type, current_cddlg.ci.controller_type_unit, UAEDEV_CD, current_cddlg.ci.rootdir[0] != 0);
		xSendDlgItemMessage(hDlg, IDC_HDF_CONTROLLER_UNIT, CB_SETCURSEL, current_cddlg.ci.controller_unit, 0);
		InitializeListView (hDlg);
		recursive--;
		customDlgType = IDD_CDDRIVE;
		customDlg = hDlg;
		return TRUE;
	case WM_NOTIFY:
		if (((LPNMHDR) lParam)->idFrom == IDC_CDLIST) {
			NM_LISTVIEW *nmlistview = (NM_LISTVIEW *)lParam;
			if (nmlistview->hdr.code == NM_DBLCLK) {
				CustomDialogClose(hDlg, -1);
				return TRUE;
			}
		}
		break;		
	case WM_COMMAND:
		if (recursive)
			break;
		recursive++;
		switch (LOWORD (wParam))
		{
		case IDOK:
			CustomDialogClose(hDlg, -1);
			recursive = 0;
			return TRUE;
		case IDCANCEL:
			CustomDialogClose(hDlg, 0);
			recursive = 0;
			return TRUE;
		case IDC_HDF_CONTROLLER:
			posn = gui_get_string_cursor(hdmenutable, hDlg, IDC_HDF_CONTROLLER);
			if (posn != CB_ERR) {
				current_cddlg.ci.controller_type = posn % HD_CONTROLLER_NEXT_UNIT;
				current_cddlg.ci.controller_type_unit = posn / HD_CONTROLLER_NEXT_UNIT;
				inithdcontroller(hDlg, current_cddlg.ci.controller_type, current_cddlg.ci.controller_type_unit, UAEDEV_CD, current_cddlg.ci.rootdir[0] != 0);
				xSendDlgItemMessage(hDlg, IDC_HDF_CONTROLLER_UNIT, CB_SETCURSEL, current_cddlg.ci.controller_unit, 0);
			}
			break;
		case IDC_HDF_CONTROLLER_UNIT:
			posn = xSendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER_UNIT, CB_GETCURSEL, 0, 0);
			if (posn != CB_ERR) {
				current_cddlg.ci.controller_unit = posn;
			}
			break;
		}
		recursive--;
		break;
	}
	return FALSE;
}

static void set_phys_cyls(HWND hDlg)
{
	if (ischecked(hDlg, IDC_HDF_PHYSGEOMETRY)) {
		int v = (current_hfdlg.ci.pheads * current_hfdlg.ci.psecs * current_hfdlg.ci.blocksize);
		current_hfdlg.ci.pcyls = (int)(v ? current_hfdlg.size / v : 0);
		current_hfdlg.ci.physical_geometry = true;
		SetDlgItemInt (hDlg, IDC_RESERVED, current_hfdlg.ci.pcyls, FALSE);
	}
}

static void restore_hd_geom(struct uaedev_config_info *dst, struct uaedev_config_info *src)
{
	_tcscpy(dst->filesys, src->filesys);
	_tcscpy(dst->devname, src->devname);
	dst->controller_type = src->controller_type;
	dst->controller_type_unit = src->controller_type_unit;
	dst->controller_unit = src->controller_unit;
	dst->controller_media_type = src->controller_media_type;
	dst->unit_feature_level = src->unit_feature_level;
	dst->bootpri = src->bootpri;
	dst->readonly = src->readonly;
	dst->physical_geometry = src->physical_geometry;
	if (src->physical_geometry) {
		dst->cyls = dst->sectors = dst->surfaces = dst->reserved = 0;
		dst->pcyls = src->pcyls;
		dst->pheads = src->pheads;
		dst->psecs = src->psecs;
	}
}

static INT_PTR CALLBACK HardfileSettingsProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;
	int res, posn;
	TCHAR tmp[MAX_DPATH];
	int v;
	int *p;

	bool handled;
	INT_PTR vv = commonproc(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch (msg) {
	case WM_DESTROY:
		PostQuitMessage(0);
		return TRUE;
	case WM_CLOSE:
		CustomDialogClose(hDlg, 0);
		return TRUE;
	case WM_DROPFILES:
		dragdrop (hDlg, (HDROP)wParam, &changed_prefs, -2);
		return FALSE;

	case WM_INITDIALOG:
		recursive++;
		setchecked(hDlg, IDC_HDF_PHYSGEOMETRY, current_hfdlg.ci.physical_geometry);
		setautocomplete (hDlg, IDC_PATH_NAME);
		setautocomplete (hDlg, IDC_PATH_FILESYS);
		setautocomplete (hDlg, IDC_PATH_GEOMETRY);
		addhistorymenu(hDlg, current_hfdlg.ci.geometry, IDC_PATH_GEOMETRY, HISTORY_GEO, false, -1);
		inithardfile (hDlg, current_hfdlg.ci.rootdir[0] != 0);
		addhistorymenu(hDlg, current_hfdlg.ci.rootdir, IDC_PATH_NAME, HISTORY_HDF, false, -1);
		addhistorymenu(hDlg, current_hfdlg.ci.filesys, IDC_PATH_FILESYS, HISTORY_FS, false, -1);
		updatehdfinfo (hDlg, true, false, false);
		sethardfile (hDlg);
		sethfdostype (hDlg, 0);
		setac (hDlg, IDC_PATH_NAME);
		recursive--;
		customDlgType = IDD_HARDFILE;
		customDlg = hDlg;
		return TRUE;

	case WM_CONTEXTMENU:
		if (GetDlgCtrlID ((HWND)wParam) == IDC_SELECTOR) {
			TCHAR *s = favoritepopup (hDlg);
			if (s) {
				TCHAR path[MAX_DPATH];
				_tcscpy (path, s);
				xfree (s);
				hardfileselecthdf (hDlg, path, true, false);
			}
		} else if (GetDlgCtrlID ((HWND)wParam) == IDC_FILESYS_SELECTOR) {
			TCHAR *s = favoritepopup (hDlg);
			if (s) {
				TCHAR path[MAX_DPATH];
				_tcscpy (path, s);
				xfree (s);
				DiskSelection (hDlg, IDC_PATH_FILESYS, 12, &workprefs, NULL, path);
			}
		} else if (GetDlgCtrlID ((HWND)wParam) == IDC_HF_CREATE) {
			TCHAR *s = favoritepopup (hDlg);
			if (s) {
				TCHAR path[MAX_DPATH];
				_tcscpy (path, s);
				xfree (s);
				hardfilecreatehdf (hDlg, path);
			}
		}
		break;

	case WM_COMMAND:

		if (HIWORD (wParam) == CBN_SELCHANGE || HIWORD (wParam) == CBN_KILLFOCUS)  {
			switch (LOWORD (wParam)) {
			case IDC_PATH_GEOMETRY:
				getcomboboxtext(hDlg, IDC_PATH_GEOMETRY, current_hfdlg.ci.geometry, sizeof  current_hfdlg.ci.geometry / sizeof(TCHAR));
				if (HIWORD (wParam) == CBN_KILLFOCUS) {
					addhistorymenu(hDlg, current_hfdlg.ci.geometry, IDC_PATH_GEOMETRY, HISTORY_GEO, false, -1);
					sethardfile(hDlg);
					updatehdfinfo (hDlg, true, false, false);
				}
				break;
			case IDC_PATH_NAME:
				if (getcomboboxtext(hDlg, IDC_PATH_NAME, tmp, sizeof tmp / sizeof(TCHAR))) {
					if (_tcscmp (tmp, current_hfdlg.ci.rootdir)) {
						_tcscpy (current_hfdlg.ci.rootdir, tmp);
						recursive++;
						hardfileselecthdf (hDlg, NULL, false, false); 
						recursive--;
					}
				}
				if (HIWORD (wParam) == CBN_KILLFOCUS) {
					addhistorymenu(hDlg, current_hfdlg.ci.rootdir, IDC_PATH_NAME, HISTORY_HDF, false, -1);
				}
				break;
			case IDC_PATH_FILESYS:
				getcomboboxtext(hDlg, IDC_PATH_FILESYS, current_hfdlg.ci.filesys, sizeof current_hfdlg.ci.filesys / sizeof(TCHAR));
				if (HIWORD(wParam) == CBN_KILLFOCUS) {
					addhistorymenu(hDlg, current_hfdlg.ci.filesys, IDC_PATH_FILESYS, HISTORY_FS, false, -1);
				}
				break;
			}
		}

		if (recursive)
			break;
		recursive++;

		if (HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == CBN_KILLFOCUS) {
			switch (LOWORD(wParam))
			{
			case IDC_HDF_CONTROLLER:
				posn = gui_get_string_cursor(hdmenutable, hDlg, IDC_HDF_CONTROLLER);
				if (posn != CB_ERR) {
					current_hfdlg.ci.controller_type = posn % HD_CONTROLLER_NEXT_UNIT;
					current_hfdlg.ci.controller_type_unit = posn / HD_CONTROLLER_NEXT_UNIT;
					inithdcontroller(hDlg, current_hfdlg.ci.controller_type, current_hfdlg.ci.controller_type_unit, UAEDEV_HDF, current_hfdlg.ci.rootdir[0] != 0);
					sethardfile(hDlg);
				}
				break;
			case IDC_HDF_CONTROLLER_UNIT:
				posn = xSendDlgItemMessage(hDlg, IDC_HDF_CONTROLLER_UNIT, CB_GETCURSEL, 0, 0);
				if (posn != CB_ERR) {
					current_hfdlg.ci.controller_unit = posn;
					sethardfile(hDlg);
				}
				break;
			case IDC_HDF_CONTROLLER_TYPE:
				posn = xSendDlgItemMessage(hDlg, IDC_HDF_CONTROLLER_TYPE, CB_GETCURSEL, 0, 0);
				if (posn != CB_ERR) {
					current_hfdlg.ci.controller_media_type = posn;
					sethardfile(hDlg);
				}
				break;
			case IDC_HDF_FEATURE_LEVEL:
				posn = xSendDlgItemMessage(hDlg, IDC_HDF_FEATURE_LEVEL, CB_GETCURSEL, 0, 0);
				if (posn != CB_ERR) {
					current_hfdlg.ci.unit_feature_level = posn;
					sethardfile(hDlg);
				}
				break;
			}
		}

		switch (LOWORD (wParam)) {
		case IDC_HF_SIZE:
			ew (hDlg, IDC_HF_CREATE, CalculateHardfileSize (hDlg) > 0);
			break;
		case IDC_HF_TYPE:
			res = xSendDlgItemMessage (hDlg, IDC_HF_TYPE, CB_GETCURSEL, 0, 0);
			sethfdostype (hDlg, (int)res);
			ew (hDlg, IDC_HF_DOSTYPE, res >= 4);
			break;
		case IDC_HF_CREATE:
			{
				struct uaedev_config_info citmp;
				memcpy(&citmp, &current_hfdlg.ci, sizeof citmp);
				default_hfdlg (&current_hfdlg, false);
				restore_hd_geom(&current_hfdlg.ci, &citmp);
				hardfilecreatehdf (hDlg, NULL);
			}
			break;
		case IDC_SELECTOR:
			{
				bool newhd = current_hfdlg.ci.rootdir[0] == 0;
				struct uaedev_config_info citmp;
				memcpy(&citmp, &current_hfdlg.ci, sizeof citmp);
				default_hfdlg (&current_hfdlg, false);
				restore_hd_geom(&current_hfdlg.ci, &citmp);
				hardfileselecthdf (hDlg, NULL, true, newhd);
			}
			break;
		case IDC_FILESYS_SELECTOR:
			DiskSelection (hDlg, IDC_PATH_FILESYS, 12, &workprefs, NULL, NULL);
			getcomboboxtext(hDlg, IDC_PATH_FILESYS, current_hfdlg.ci.filesys, sizeof  current_hfdlg.ci.filesys / sizeof(TCHAR));
			DISK_history_add(current_hfdlg.ci.filesys, -1, HISTORY_FS, 0);
			break;
		case IDOK:
			CustomDialogClose(hDlg, -1);
			recursive = 0;
			return TRUE;
		case IDCANCEL:
			CustomDialogClose(hDlg, 0);
			recursive = 0;
			return TRUE;
		case IDC_HDF_PHYSGEOMETRY:
			current_hfdlg.ci.physical_geometry = ischecked(hDlg, IDC_HDF_PHYSGEOMETRY);
			updatehdfinfo(hDlg, true, false, false);
			sethardfile(hDlg);
			break;
		case IDC_HDF_RW:
			current_hfdlg.ci.readonly = !ischecked (hDlg, IDC_HDF_RW);
			break;
		case IDC_HDF_AUTOBOOT:
			if (ischecked (hDlg, IDC_HDF_AUTOBOOT)) {
				current_hfdlg.ci.bootpri = 0;
				setchecked (hDlg, IDC_HDF_DONOTMOUNT, false);
			} else {
				current_hfdlg.ci.bootpri = BOOTPRI_NOAUTOBOOT;
			}
			SetDlgItemInt (hDlg, IDC_HARDFILE_BOOTPRI, current_hfdlg.ci.bootpri, TRUE);
			break;
		case IDC_HDF_DONOTMOUNT:
			if (ischecked (hDlg, IDC_HDF_DONOTMOUNT)) {
				current_hfdlg.ci.bootpri = BOOTPRI_NOAUTOMOUNT;
				setchecked (hDlg, IDC_HDF_AUTOBOOT, false);
			} else {
				current_hfdlg.ci.bootpri = BOOTPRI_NOAUTOBOOT;
				setchecked (hDlg, IDC_HDF_AUTOBOOT, true);
			}
			SetDlgItemInt (hDlg, IDC_HARDFILE_BOOTPRI, current_hfdlg.ci.bootpri, TRUE);
			break;
		case IDC_HDF_RDB:
			if (ischecked(hDlg, IDC_HDF_RDB)) {
				SetDlgItemText(hDlg, IDC_PATH_FILESYS, _T(""));
				SetDlgItemText(hDlg, IDC_HARDFILE_DEVICE, _T(""));
				current_hfdlg.ci.sectors = current_hfdlg.ci.reserved = current_hfdlg.ci.surfaces = 0;
				current_hfdlg.ci.bootpri = 0;
			} else {
				TCHAR tmp[MAX_DPATH];
				_tcscpy(tmp, current_hfdlg.ci.rootdir);
				default_hfdlg(&current_hfdlg, false);
				_tcscpy(current_hfdlg.ci.rootdir, tmp);
			}
			sethardfile(hDlg);
			break;
		case IDC_PATH_GEOMETRY_SELECTOR:
			if (DiskSelection (hDlg, IDC_PATH_GEOMETRY, 23, &workprefs, NULL, current_hfdlg.ci.geometry)) {
				DISK_history_add(current_hfdlg.ci.geometry, -1, HISTORY_GEO, 0);
				sethardfile(hDlg);
				updatehdfinfo (hDlg, true, false, false);
			}
			break;
		case IDC_SECTORS:
			p = ischecked(hDlg, IDC_HDF_PHYSGEOMETRY) ? &current_hfdlg.ci.psecs : &current_hfdlg.ci.sectors;
			v = *p;
			*p = GetDlgItemInt (hDlg, IDC_SECTORS, NULL, FALSE);
			if (v != *p) {
				set_phys_cyls(hDlg);
				updatehdfinfo (hDlg, true, false, false);
				setchecked(hDlg, IDC_HDF_RDB, !is_hdf_rdb());
			}
			break;
		case IDC_RESERVED:
			p = ischecked(hDlg, IDC_HDF_PHYSGEOMETRY) ? &current_hfdlg.ci.pcyls : &current_hfdlg.ci.reserved;
			v = *p;
			*p = GetDlgItemInt (hDlg, IDC_RESERVED, NULL, FALSE);
			if (v != *p) {
				if (ischecked(hDlg, IDC_HDF_PHYSGEOMETRY)) {
					current_hfdlg.ci.physical_geometry = true;
				}
				updatehdfinfo (hDlg, true, false, false);
				setchecked(hDlg, IDC_HDF_RDB, !is_hdf_rdb());
			}
			break;
		case IDC_HEADS:
			p = ischecked(hDlg, IDC_HDF_PHYSGEOMETRY) ? &current_hfdlg.ci.pheads : &current_hfdlg.ci.surfaces;
			v = *p;
			*p = GetDlgItemInt (hDlg, IDC_HEADS, NULL, FALSE);
			if (v != *p) {
				set_phys_cyls(hDlg);
				updatehdfinfo (hDlg, true, false, false);
				setchecked(hDlg, IDC_HDF_RDB, !is_hdf_rdb());
			}
			break;
		case IDC_BLOCKSIZE:
			v = current_hfdlg.ci.blocksize;
			current_hfdlg.ci.blocksize = GetDlgItemInt (hDlg, IDC_BLOCKSIZE, NULL, FALSE);
			if (v != current_hfdlg.ci.blocksize)
				updatehdfinfo (hDlg, true, false, false);
			break;
		case IDC_HARDFILE_BOOTPRI:
			current_hfdlg.ci.bootpri = GetDlgItemInt (hDlg, IDC_HARDFILE_BOOTPRI, NULL, TRUE);
			if (current_hfdlg.ci.bootpri < -127)
				current_hfdlg.ci.bootpri = -127;
			if (current_hfdlg.ci.bootpri > 127)
				current_hfdlg.ci.bootpri = 127;
			break;
		case IDC_HARDFILE_DEVICE:
			GetDlgItemText (hDlg, IDC_HARDFILE_DEVICE, current_hfdlg.ci.devname, sizeof current_hfdlg.ci.devname / sizeof (TCHAR));
			break;
		}
		recursive--;

		break;
	}
	return FALSE;
}

extern int harddrive_to_hdf (HWND, struct uae_prefs*, int);
static INT_PTR CALLBACK HarddriveSettingsProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;
	int v, i;
	int *p;
	int posn;
	static int oposn;

	bool handled;
	INT_PTR vv = commonproc(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch (msg) {
	case WM_DESTROY:
		PostQuitMessage(0);
		return TRUE;
	case WM_CLOSE:
		CustomDialogClose(hDlg, 0);
		return TRUE;
	case WM_INITDIALOG:
		{
			int index;
			oposn = -1;
			hdf_init_target ();
			recursive++;
			setautocomplete (hDlg, IDC_PATH_GEOMETRY);
			addhistorymenu(hDlg, current_hfdlg.ci.geometry, IDC_PATH_GEOMETRY, HISTORY_GEO, false, -1);
			setchecked(hDlg, IDC_HDF_PHYSGEOMETRY, current_hfdlg.ci.physical_geometry);
			setharddrive(hDlg);
			inithdcontroller(hDlg, current_hfdlg.ci.controller_type, current_hfdlg.ci.controller_type_unit, UAEDEV_HDF, current_hfdlg.ci.rootdir[0] != 0);
			CheckDlgButton (hDlg, IDC_HDF_RW, !current_hfdlg.ci.readonly);
			CheckDlgButton(hDlg, IDC_HDF_LOCK, current_hfdlg.ci.lock);
			CheckDlgButton(hDlg, IDC_HDF_IDENTITY, current_hfdlg.ci.loadidentity);
			xSendDlgItemMessage (hDlg, IDC_HARDDRIVE, CB_RESETCONTENT, 0, 0);
			ew (hDlg, IDC_HARDDRIVE_IMAGE, FALSE);
			index = -1;
			for (i = 0; i < hdf_getnumharddrives (); i++) {
				xSendDlgItemMessage (hDlg, IDC_HARDDRIVE, CB_ADDSTRING, 0, (LPARAM)hdf_getnameharddrive (i, 1, NULL, NULL, NULL));
				TCHAR *name1 = hdf_getnameharddrive (i, 4, NULL, NULL, NULL);
				TCHAR *name2 = hdf_getnameharddrive (i, 2, NULL, NULL, NULL);
				TCHAR *name3 = hdf_getnameharddrive (i, 0, NULL, NULL, NULL);
				if (!_tcscmp (current_hfdlg.ci.rootdir, name1) || !_tcscmp (current_hfdlg.ci.rootdir, name2) || !_tcscmp (current_hfdlg.ci.rootdir, name3))
					index = i;
			}
			if (index >= 0) {
				xSendDlgItemMessage (hDlg, IDC_HARDDRIVE, CB_SETCURSEL, index, 0);
				gui_set_string_cursor(hdmenutable, hDlg, IDC_HDF_CONTROLLER, current_hfdlg.ci.controller_type + current_hfdlg.ci.controller_type_unit * HD_CONTROLLER_NEXT_UNIT);
				xSendDlgItemMessage(hDlg, IDC_HDF_CONTROLLER_UNIT, CB_SETCURSEL, current_hfdlg.ci.controller_unit, 0);
				xSendDlgItemMessage(hDlg, IDC_HDF_CONTROLLER_TYPE, CB_SETCURSEL, current_hfdlg.ci.controller_media_type, 0);
				xSendDlgItemMessage(hDlg, IDC_HDF_FEATURE_LEVEL, CB_SETCURSEL, current_hfdlg.ci.unit_feature_level, 0);
			}
			recursive--;
			return TRUE;
		}
	case WM_COMMAND:
		if (recursive)
			break;
		recursive++;
		if (HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == CBN_KILLFOCUS) {
			switch (LOWORD(wParam)) {
				case IDC_PATH_GEOMETRY:
					getcomboboxtext(hDlg, IDC_PATH_GEOMETRY, current_hfdlg.ci.geometry, sizeof  current_hfdlg.ci.geometry / sizeof(TCHAR));
					if (HIWORD (wParam) == CBN_KILLFOCUS) {
						addhistorymenu(hDlg, current_hfdlg.ci.geometry, IDC_PATH_GEOMETRY, HISTORY_GEO, false, -1);
						setharddrive(hDlg);
						updatehdfinfo (hDlg, true, false, true);
					}
					break;
			}
		}

		if (HIWORD (wParam) == BN_CLICKED) {
			switch (LOWORD (wParam)) {
			case IDOK:
				CustomDialogClose(hDlg, -1);
				recursive = 0;
				return TRUE;
			case IDCANCEL:
				CustomDialogClose(hDlg, 0);
				recursive = 0;
				return TRUE;
			case IDC_HDF_PHYSGEOMETRY:
				current_hfdlg.ci.physical_geometry = ischecked(hDlg, IDC_HDF_PHYSGEOMETRY);
				updatehdfinfo(hDlg, true, false, true);
				setharddrive(hDlg);
				break;
			case IDC_HARDDRIVE_ID:
				if (oposn >= 0) {
					void hd_get_meta(HWND hDlg, int idx, TCHAR*);
					hd_get_meta(hDlg, oposn, current_hfdlg.ci.geometry);
					setharddrive(hDlg);
				}
				break;
			case IDC_HARDDRIVE_IMAGE:
				posn = xSendDlgItemMessage (hDlg, IDC_HARDDRIVE, CB_GETCURSEL, 0, 0);
				if (posn != CB_ERR)
					harddrive_to_hdf (hDlg, &workprefs, posn);
				break;
			case IDC_HDF_RW:
				posn = xSendDlgItemMessage (hDlg, IDC_HARDDRIVE, CB_GETCURSEL, 0, 0);
				if (posn != CB_ERR) {
					int dang = 1;
					hdf_getnameharddrive (posn, 1, NULL, &dang, NULL);
					current_hfdlg.ci.readonly = (ischecked (hDlg, IDC_HDF_RW) && !dang) ? false : true;
				}
				break;
			case IDC_HDF_LOCK:
				posn = xSendDlgItemMessage(hDlg, IDC_HARDDRIVE, CB_GETCURSEL, 0, 0);
				if (posn != CB_ERR) {
					int dang = 1;
					hdf_getnameharddrive(posn, 1, NULL, &dang, NULL);
					current_hfdlg.ci.lock = ischecked(hDlg, IDC_HDF_LOCK);
				}
				break;
			case IDC_HDF_IDENTITY:
				posn = xSendDlgItemMessage(hDlg, IDC_HDF_IDENTITY, CB_GETCURSEL, 0, 0);
				if (posn != CB_ERR) {
					current_hfdlg.ci.loadidentity = ischecked(hDlg, IDC_HDF_IDENTITY);
				}
				break;
			}
		}
		switch(LOWORD(wParam))
		{
			case IDC_HARDDRIVE:
				posn = xSendDlgItemMessage (hDlg, IDC_HARDDRIVE, CB_GETCURSEL, 0, 0);
				if (oposn != posn && posn != CB_ERR) {
					oposn = posn;
					if (posn >= 0) {
						BOOL ena;
						uae_u32 flags;
						int dang = 1;
						hdf_getnameharddrive (posn, 1, NULL, &dang, &flags);
						_tcscpy (current_hfdlg.ci.rootdir, hdf_getnameharddrive (posn, 4, NULL, &dang, NULL));
						ena = dang >= 0;
						ew(hDlg, IDC_HARDDRIVE_IMAGE, ena);
						ew(hDlg, IDC_HARDDRIVE_ID, ena);
						ew(hDlg, IDC_HDF_LOCK, ena);
						ew(hDlg, IDOK, ena);
						ew(hDlg, IDC_HDF_RW, !dang);
						ew(hDlg, IDC_HDF_FEATURE_LEVEL, ena);
						ew(hDlg, IDC_HDF_CONTROLLER, ena);
						ew(hDlg, IDC_HDF_CONTROLLER_UNIT, ena);
						ew(hDlg, IDC_HDF_CONTROLLER_TYPE, ena);
						ew(hDlg, IDC_PATH_GEOMETRY, ena);
						ew(hDlg, IDC_PATH_GEOMETRY_SELECTOR, ena);
						ew(hDlg, IDC_HDF_PHYSGEOMETRY, ena && current_hfdlg.ci.geometry[0] == 0);
						if (dang)
							current_hfdlg.ci.readonly = true;
						current_hfdlg.ci.blocksize = 512;
						current_hfdlg.forcedcylinders = 0;
						current_hfdlg.ci.cyls = current_hfdlg.ci.highcyl = current_hfdlg.ci.sectors = current_hfdlg.ci.surfaces = 0;
						SetDlgItemText(hDlg, IDC_HDFINFO, _T(""));
						SetDlgItemText(hDlg, IDC_HDFINFO2, _T(""));
						SetDlgItemText(hDlg, IDC_HDFINFO3, _T(""));
						updatehdfinfo (hDlg, true, current_hfdlg.ci.geometry[0] ? false : true, true);
						hdf_getnameharddrive(posn, 1, NULL, &dang, &flags);
						ew(hDlg, IDC_HDF_IDENTITY, ena && (flags & 1));
						if (!(flags & 1))
							current_hfdlg.ci.loadidentity = false;
						gui_set_string_cursor(hdmenutable, hDlg, IDC_HDF_CONTROLLER, current_hfdlg.ci.controller_type + current_hfdlg.ci.controller_type_unit * MAX_DUPLICATE_EXPANSION_BOARDS);
						xSendDlgItemMessage(hDlg, IDC_HDF_CONTROLLER_UNIT, CB_SETCURSEL, current_hfdlg.ci.controller_unit, 0);
						CheckDlgButton(hDlg, IDC_HDF_RW, !current_hfdlg.ci.readonly);
						_tcscpy (current_hfdlg.ci.rootdir, hdf_getnameharddrive ((int)posn, 4, &current_hfdlg.ci.blocksize, NULL, NULL));
						setharddrive(hDlg);
					}
				}
				break;
			case IDC_HDF_CONTROLLER:
				posn = gui_get_string_cursor(hdmenutable, hDlg, IDC_HDF_CONTROLLER);
				if (posn != CB_ERR && current_hfdlg.ci.controller_type != posn) {
					current_hfdlg.ci.controller_type = posn % HD_CONTROLLER_NEXT_UNIT;
					current_hfdlg.ci.controller_type_unit = posn / HD_CONTROLLER_NEXT_UNIT;
					current_hfdlg.forcedcylinders = 0;
					current_hfdlg.ci.cyls = current_hfdlg.ci.highcyl = current_hfdlg.ci.sectors = current_hfdlg.ci.surfaces = 0;
					SetDlgItemText(hDlg, IDC_HDFINFO, _T(""));
					SetDlgItemText(hDlg, IDC_HDFINFO2, _T(""));
					SetDlgItemText(hDlg, IDC_HDFINFO3, _T(""));
					updatehdfinfo (hDlg, true, true, true);
					inithdcontroller(hDlg, current_hfdlg.ci.controller_type, current_hfdlg.ci.controller_type_unit, UAEDEV_HDF, current_hfdlg.ci.rootdir[0] != 0);
					xSendDlgItemMessage(hDlg, IDC_HDF_CONTROLLER_UNIT, CB_SETCURSEL, current_hfdlg.ci.controller_unit, 0);
					sethardfiletypes(hDlg);
				}
				break;
			case IDC_HDF_CONTROLLER_UNIT:
				posn = xSendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER_UNIT, CB_GETCURSEL, 0, 0);
				if (posn != CB_ERR) {
					current_hfdlg.ci.controller_unit = posn;
				}
				break;
			case IDC_HDF_CONTROLLER_TYPE:
				posn = xSendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER_TYPE, CB_GETCURSEL, 0, 0);
				if (posn != CB_ERR) {
					current_hfdlg.ci.controller_media_type = posn;
				}
				break;
			case IDC_HDF_FEATURE_LEVEL:
				posn = xSendDlgItemMessage(hDlg, IDC_HDF_FEATURE_LEVEL, CB_GETCURSEL, 0, 0);
				if (posn != CB_ERR) {
					current_hfdlg.ci.unit_feature_level = posn;
				}
			break;
			case IDC_PATH_GEOMETRY_SELECTOR:
				if (DiskSelection (hDlg, IDC_PATH_GEOMETRY, 23, &workprefs, NULL, current_hfdlg.ci.geometry)) {
					getcomboboxtext(hDlg, IDC_PATH_GEOMETRY, current_hfdlg.ci.geometry, sizeof  current_hfdlg.ci.geometry / sizeof(TCHAR));
					DISK_history_add(current_hfdlg.ci.geometry, -1, HISTORY_GEO, 0);
					setharddrive(hDlg);
					updatehdfinfo (hDlg, true, false, true);
				}
				break;
			case IDC_SECTORS:
				p = &current_hfdlg.ci.psecs;
				v = *p;
				*p = GetDlgItemInt (hDlg, IDC_SECTORS, NULL, FALSE);
				if (v != *p) {
					set_phys_cyls(hDlg);
					updatehdfinfo (hDlg, true, false, true);
					setchecked(hDlg, IDC_HDF_RDB, !is_hdf_rdb());
				}
				break;
			case IDC_RESERVED:
				p = &current_hfdlg.ci.pcyls;
				v = *p;
				*p = GetDlgItemInt (hDlg, IDC_RESERVED, NULL, FALSE);
				if (v != *p) {
					if (ischecked(hDlg, IDC_HDF_PHYSGEOMETRY)) {
						current_hfdlg.ci.physical_geometry = true;
					}
					updatehdfinfo (hDlg, true, false, true);
					setchecked(hDlg, IDC_HDF_RDB, !is_hdf_rdb());
				}
				break;
			case IDC_HEADS:
				p = &current_hfdlg.ci.pheads;
				v = *p;
				*p = GetDlgItemInt (hDlg, IDC_HEADS, NULL, FALSE);
				if (v != *p) {
					set_phys_cyls(hDlg);
					updatehdfinfo (hDlg, true, false, true);
					setchecked(hDlg, IDC_HDF_RDB, !is_hdf_rdb());
				}
				break;
		}
		recursive--;
		break;
	}
	return FALSE;
}

static void new_filesys (HWND hDlg, int entry)
{
	struct uaedev_config_data *uci;
	struct uaedev_config_info ci;
	memcpy (&ci, &current_fsvdlg.ci, sizeof (struct uaedev_config_info));
	uci = add_filesys_config (&workprefs, entry, &ci);
	if (uci) {
		if (uci->ci.rootdir[0])
			filesys_media_change (uci->ci.rootdir, 1, uci);
		else if (uci->configoffset >= 0)
			filesys_eject (uci->configoffset);
	}
}

static void new_cddrive (HWND hDlg, int entry)
{
	struct uaedev_config_info ci = { 0 };
	ci.device_emu_unit = 0;
	ci.controller_type = current_cddlg.ci.controller_type;
	ci.controller_unit = current_cddlg.ci.controller_unit;
	ci.type = UAEDEV_CD;
	ci.readonly = true;
	ci.blocksize = 2048;
	add_filesys_config (&workprefs, entry, &ci);
}

static void new_tapedrive (HWND hDlg, int entry)
{
	struct uaedev_config_data *uci;
	struct uaedev_config_info ci = { 0 };
	ci.controller_type = current_tapedlg.ci.controller_type;
	ci.controller_unit = current_tapedlg.ci.controller_unit;
	ci.readonly = current_tapedlg.ci.readonly;
	_tcscpy (ci.rootdir, current_tapedlg.ci.rootdir);
	ci.type = UAEDEV_TAPE;
	ci.blocksize = 512;
	uci = add_filesys_config (&workprefs, entry, &ci);
	if (uci && uci->unitnum >= 0) {
		tape_media_change (uci->unitnum, &ci);
	}
}

static void new_hardfile (HWND hDlg, int entry)
{
	struct uaedev_config_data *uci;
	struct uaedev_config_info ci;
	memcpy (&ci, &current_hfdlg.ci, sizeof (struct uaedev_config_info));
	uci = add_filesys_config (&workprefs, entry, &ci);
	if (uci) {
		struct hardfiledata *hfd = get_hardfile_data (uci->configoffset);
		if (hfd)
			hardfile_media_change (hfd, &ci, true, false);
		pcmcia_disk_reinsert(&workprefs, &uci->ci, false);
	}
}

static void new_harddrive (HWND hDlg, int entry)
{
	struct uaedev_config_data *uci;

	uci = add_filesys_config (&workprefs, entry, &current_hfdlg.ci);
	if (uci) {
		struct hardfiledata *hfd = get_hardfile_data (uci->configoffset);
		if (hfd)
			hardfile_media_change (hfd, &current_hfdlg.ci, true, false);
		pcmcia_disk_reinsert(&workprefs, &uci->ci, false);
	}
}

static void harddisk_remove (HWND hDlg)
{
	int entry = listview_find_selected (GetDlgItem (hDlg, IDC_VOLUMELIST), false);
	if (entry < 0)
		return;
	kill_filesys_unitconfig (&workprefs, entry);
}

static void harddisk_move (HWND hDlg, int up)
{
	int entry = listview_find_selected (GetDlgItem (hDlg, IDC_VOLUMELIST), false);
	if (entry < 0)
		return;
	move_filesys_unitconfig (&workprefs, entry, up ? entry - 1 : entry + 1);
}

static void harddisk_edit (HWND hDlg)
{
	int entry = listview_find_selected (GetDlgItem (hDlg, IDC_VOLUMELIST), false);
	int type;
	struct uaedev_config_data *uci;
	struct mountedinfo mi;

	if (entry < 0 || entry >= workprefs.mountitems)
		return;
	uci = &workprefs.mountconfig[entry];

	type = get_filesys_unitconfig (&workprefs, entry, &mi);
	if (type < 0)
		type = uci->ci.type == UAEDEV_HDF ? FILESYS_HARDFILE : FILESYS_VIRTUAL;

	if (uci->ci.type == UAEDEV_CD) {
		memcpy (&current_cddlg.ci, uci, sizeof (struct uaedev_config_info));
		if (CustomCreateDialogBox(IDD_CDDRIVE, hDlg, CDDriveSettingsProc)) {
			new_cddrive (hDlg, entry);
		}
	} else if (uci->ci.type == UAEDEV_TAPE) {
		memcpy (&current_tapedlg.ci, uci, sizeof (struct uaedev_config_info));
		if (CustomCreateDialogBox(IDD_TAPEDRIVE, hDlg, TapeDriveSettingsProc)) {
			new_tapedrive (hDlg, entry);
		}
	}
	else if(type == FILESYS_HARDFILE || type == FILESYS_HARDFILE_RDB)
	{
		current_hfdlg.forcedcylinders = uci->ci.highcyl;
		memcpy (&current_hfdlg.ci, uci, sizeof (struct uaedev_config_info));
		if (CustomCreateDialogBox(IDD_HARDFILE, hDlg, HardfileSettingsProc)) {
			new_hardfile (hDlg, entry);
		}
	}
	else if (type == FILESYS_HARDDRIVE) /* harddisk */
	{
		memcpy (&current_hfdlg.ci, uci, sizeof (struct uaedev_config_info));
		if (CustomCreateDialogBox(IDD_HARDDRIVE, hDlg, HarddriveSettingsProc)) {
			new_harddrive (hDlg, entry);
		}
	}
	else /* Filesystem */
	{
		memcpy (&current_fsvdlg.ci, uci, sizeof (struct uaedev_config_info));
		archivehd = -1;
		if (CustomCreateDialogBox(IDD_FILESYS, hDlg, VolumeSettingsProc)) {
			new_filesys (hDlg, entry);
		}
	}
}

static ACCEL HarddiskAccel[] = {
	{ FVIRTKEY|FSHIFT, VK_UP, IDC_UP }, { FVIRTKEY|FSHIFT, VK_DOWN, IDC_DOWN },
	{ FVIRTKEY, VK_RETURN, IDC_EDIT }, { FVIRTKEY, VK_DELETE, IDC_REMOVE },
	{ 0, 0, 0 }
};

static void hilitehd (HWND hDlg)
{
	int total = ListView_GetItemCount (cachedlist);
	if (total <= 0) {
		ew (hDlg, IDC_EDIT, FALSE);
		ew (hDlg, IDC_REMOVE, FALSE);
		return;
	}
	if (clicked_entry < 0)
		clicked_entry = 0;
	if (clicked_entry >= total)
		clicked_entry = total;
	ListView_SetItemState (cachedlist, clicked_entry, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	ew (hDlg, IDC_EDIT, TRUE);
	ew (hDlg, IDC_REMOVE, TRUE);
}

static int harddiskdlg_button (HWND hDlg, WPARAM wParam)
{
	int button = LOWORD (wParam);
	switch (button) {
	case IDC_CD_SELECT:
		DiskSelection (hDlg, wParam, 17, &workprefs, NULL, NULL);
		quickstart_cdtype = 1;
		workprefs.cdslots[0].inuse = true;
		addcdtype (hDlg, IDC_CD_TYPE);
		InitializeListView (hDlg);
		hilitehd (hDlg);
		break;
	case IDC_CD_EJECT:
		eject_cd ();
		SetDlgItemText (hDlg, IDC_CD_TEXT, _T(""));
		addcdtype (hDlg, IDC_CD_TYPE);
		InitializeListView (hDlg);
		hilitehd (hDlg);
		break;
	case IDC_NEW_FS:
		default_fsvdlg (&current_fsvdlg);
		archivehd = 0;
		if (CustomCreateDialogBox(IDD_FILESYS, hDlg, VolumeSettingsProc))
			new_filesys (hDlg, -1);
		return 1;
	case IDC_NEW_FSARCH:
		archivehd = 1;
		default_fsvdlg (&current_fsvdlg);
		if (CustomCreateDialogBox(IDD_FILESYS, hDlg, VolumeSettingsProc))
			new_filesys (hDlg, -1);
		return 1;

	case IDC_NEW_HF:
		default_hfdlg (&current_hfdlg, false);
		if (CustomCreateDialogBox(IDD_HARDFILE, hDlg, HardfileSettingsProc))
			new_hardfile (hDlg, -1);
		return 1;

	case IDC_NEW_CD:
		if (CustomCreateDialogBox(IDD_CDDRIVE, hDlg, CDDriveSettingsProc))
			new_cddrive (hDlg, -1);
		return 1;

	case IDC_NEW_TAPE:
		default_tapedlg (&current_tapedlg);
		if (CustomCreateDialogBox(IDD_TAPEDRIVE, hDlg, TapeDriveSettingsProc))
			new_tapedrive (hDlg, -1);
		return 1;

	case IDC_NEW_HD:
		default_hfdlg (&current_hfdlg, true);
		current_hfdlg.ci.type = UAEDEV_HDF;
		if (hdf_init_target () == 0) {
			TCHAR tmp[MAX_DPATH];
			WIN32GUI_LoadUIString (IDS_NOHARDDRIVES, tmp, sizeof (tmp) / sizeof (TCHAR));
			gui_message (tmp);
		} else {
			if (CustomCreateDialogBox(IDD_HARDDRIVE, hDlg, HarddriveSettingsProc))
				new_harddrive (hDlg, -1);
		}
		return 1;

	case IDC_EDIT:
		harddisk_edit (hDlg);
		return 1;

	case IDC_REMOVE:
		harddisk_remove (hDlg);
		return 1;

	case IDC_UP:
		harddisk_move (hDlg, 1);
		clicked_entry--;
		return 1;

	case IDC_DOWN:
		harddisk_move (hDlg, 0);
		clicked_entry++;
		return 1;

	case IDC_MAPDRIVES_AUTO:
		workprefs.win32_automount_removable = ischecked (hDlg, IDC_MAPDRIVES_AUTO);
		break;

	case IDC_MAPDRIVES:
		workprefs.win32_automount_drives = ischecked (hDlg, IDC_MAPDRIVES);
		break;

	case IDC_MAPDRIVES_REMOVABLE:
		workprefs.win32_automount_removabledrives = ischecked (hDlg, IDC_MAPDRIVES_REMOVABLE);
		break;

	case IDC_MAPDRIVES_CD:
		workprefs.win32_automount_cddrives = ischecked (hDlg, IDC_MAPDRIVES_CD);
		break;

	case IDC_MAPDRIVES_LIMIT:
		workprefs.filesys_limit = ischecked (hDlg, IDC_MAPDRIVES_LIMIT) ? 950 * 1024 : 0;
		break;

	case IDC_MAPDRIVES_NET:
		workprefs.win32_automount_netdrives = ischecked (hDlg, IDC_MAPDRIVES_NET);
		break;

	case IDC_NOUAEFSDB:
		workprefs.filesys_no_uaefsdb = ischecked (hDlg, IDC_NOUAEFSDB);
		break;

	case IDC_NORECYCLEBIN:
		workprefs.win32_norecyclebin = ischecked (hDlg, IDC_NORECYCLEBIN);
		break;

	case IDC_CD_SPEED:
		workprefs.cd_speed = ischecked (hDlg, IDC_CD_SPEED) ? 0 : 100;
		break;
	}
	return 0;
}

static void harddiskdlg_volume_notify (HWND hDlg, NM_LISTVIEW *nmlistview)
{
	HWND list = nmlistview->hdr.hwndFrom;
	int dblclick = 0;
	int entry = 0;

	switch (nmlistview->hdr.code) {
	case LVN_BEGINDRAG:
		drag_start (hDlg, cachedlist, (LPARAM)nmlistview);
		break;
	case NM_DBLCLK:
		dblclick = 1;
		/* fall through */
	case NM_CLICK:
		entry = listview_entry_from_click (list, 0, false);
		if (entry >= 0)
		{
			if(dblclick)
				harddisk_edit (hDlg);
			InitializeListView (hDlg);
			clicked_entry = entry;
			// Hilite the current selected item
			ListView_SetItemState (cachedlist, clicked_entry, LVIS_SELECTED, LVIS_SELECTED);
		}
		break;
	case LVN_COLUMNCLICK:
		ColumnClickListView(hDlg, nmlistview);
		break;
	}
}

/* harddisk parent view */
static INT_PTR CALLBACK HarddiskDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch (msg) {
	case WM_INITDIALOG:
		clicked_entry = 0;
		pages[HARDDISK_ID] = hDlg;
		currentpage = HARDDISK_ID;
		Button_SetElevationRequiredState (GetDlgItem (hDlg, IDC_NEW_HD), TRUE);

	case WM_USER:
		CheckDlgButton (hDlg, IDC_MAPDRIVES_AUTO, workprefs.win32_automount_removable);
		CheckDlgButton (hDlg, IDC_MAPDRIVES, workprefs.win32_automount_drives);
		CheckDlgButton (hDlg, IDC_MAPDRIVES_CD, workprefs.win32_automount_cddrives);
		CheckDlgButton (hDlg, IDC_MAPDRIVES_NET, workprefs.win32_automount_netdrives);
		CheckDlgButton (hDlg, IDC_MAPDRIVES_REMOVABLE, workprefs.win32_automount_removabledrives);
		CheckDlgButton (hDlg, IDC_NOUAEFSDB, workprefs.filesys_no_uaefsdb);
		CheckDlgButton (hDlg, IDC_NORECYCLEBIN, workprefs.win32_norecyclebin);
		CheckDlgButton (hDlg, IDC_MAPDRIVES_LIMIT, workprefs.filesys_limit != 0);
		CheckDlgButton (hDlg, IDC_CD_SPEED, workprefs.cd_speed == 0);
		InitializeListView (hDlg);
		setautocomplete (hDlg, IDC_CD_TEXT);
		addhistorymenu (hDlg, workprefs.cdslots[0].name, IDC_CD_TEXT, HISTORY_CD, true, -1);
		addcdtype (hDlg, IDC_CD_TYPE);
		hilitehd (hDlg);
		break;

	case WM_MOUSEMOVE:
		if (drag_move (hDlg, lParam))
			return TRUE;
		break;
	case WM_LBUTTONUP:
		{
			int *draggeditems, item;
			if ((item = drag_end (hDlg, cachedlist, lParam, &draggeditems)) >= 0) {
				move_filesys_unitconfig (&workprefs, draggeditems[0], item);
				InitializeListView (hDlg);
				clicked_entry = item;
				hilitehd (hDlg);
			}
			xfree (draggeditems);
			break;
		}

	case WM_COMMAND:
		if (HIWORD (wParam) == CBN_SELCHANGE || HIWORD (wParam) == CBN_KILLFOCUS)  {
			switch (LOWORD (wParam))
			{
			case IDC_CD_TEXT:
			getfloppyname (hDlg, 0, 1, IDC_CD_TEXT);
			quickstart_cdtype = 1;
			workprefs.cdslots[0].inuse = true;
			if (full_property_sheet)
				workprefs.cdslots[0].type = SCSI_UNIT_DEFAULT;
			addcdtype (hDlg, IDC_CD_TYPE);
			addhistorymenu (hDlg, workprefs.cdslots[0].name, IDC_CD_TEXT, HISTORY_CD, true, -1);
			InitializeListView (hDlg);
			hilitehd (hDlg);
			break;
			case IDC_CD_TYPE:
			int val = xSendDlgItemMessage (hDlg, IDC_CD_TYPE, CB_GETCURSEL, 0, 0);
			if (val != CB_ERR) {
				quickstart_cdtype = val;
				if (full_property_sheet)
					workprefs.cdslots[0].type = SCSI_UNIT_DEFAULT;
				if (quickstart_cdtype >= 2) {
					int len = sizeof quickstart_cddrive / sizeof (TCHAR);
					quickstart_cdtype = 2;
					workprefs.cdslots[0].inuse = true;
					xSendDlgItemMessage (hDlg, IDC_CD_TYPE, WM_GETTEXT, (WPARAM)len, (LPARAM)quickstart_cddrive);
					_tcscpy (workprefs.cdslots[0].name, quickstart_cddrive);
				} else {
					eject_cd ();
					quickstart_cdtype = val;
					if (val > 0)
						workprefs.cdslots[0].inuse = true;

				}
				if (full_property_sheet) {
					for (int i = 1; i < MAX_TOTAL_SCSI_DEVICES; i++) {
						if (workprefs.cdslots[i].inuse == false)
							workprefs.cdslots[i].type = SCSI_UNIT_DISABLED;
					}
				}
				addcdtype (hDlg, IDC_CD_TYPE);
				addhistorymenu (hDlg, workprefs.cdslots[0].name, IDC_CD_TEXT, HISTORY_CD, true, -1);
				InitializeListView (hDlg);
				hilitehd (hDlg);
			}
			break;
			}
		}
		switch (LOWORD(wParam))
		{
		case 10001:
			clicked_entry--;
			hilitehd (hDlg);
			break;
		case 10002:
			clicked_entry++;
			hilitehd (hDlg);
			break;
		default:
			if (harddiskdlg_button (hDlg, wParam)) {
				InitializeListView (hDlg);
				hilitehd (hDlg);
			}
			break;
		}
		break;

	case WM_NOTIFY:
		if (((LPNMHDR) lParam)->idFrom == IDC_VOLUMELIST)
			harddiskdlg_volume_notify (hDlg, (NM_LISTVIEW *) lParam);
		return TRUE;
	default:
		return FALSE;
	}

	return FALSE;
}

#endif

static void out_floppyspeed (HWND hDlg)
{
	TCHAR txt[30];
	TCHAR tmp1[MAX_DPATH];
	TCHAR tmp2[MAX_DPATH];

	WIN32GUI_LoadUIString (IDS_FLOPPY_COMPATIBLE, tmp1, sizeof (tmp1) / sizeof (TCHAR));
	WIN32GUI_LoadUIString (IDS_FLOPPY_TURBO, tmp2, sizeof (tmp2) / sizeof (TCHAR));
	if (workprefs.floppy_speed)
		_stprintf (txt, _T("%d%%%s"), workprefs.floppy_speed, workprefs.floppy_speed == 100 ? tmp1 : _T(""));
	else
		_tcscpy (txt, tmp2);
	SetDlgItemText (hDlg, IDC_FLOPPYSPDTEXT, txt);
}

static int isfloppybridge(int type)
{
	if (type >= DRV_FB) {
		return type - DRV_FB;
	}
	return -1;
}

static void floppytooltip (HWND hDlg, int num, uae_u32 crc32)
{
	TOOLINFO ti;
	int id;
	TCHAR tmp[100];

	if (currentpage == QUICKSTART_ID)
		id = floppybuttonsq[num][0];
	else
		id = floppybuttons[num][0];
	if (id < 0)
		return;
	memset (&ti, 0, sizeof ti);
	ti.cbSize = sizeof (TOOLINFO);
	ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
	ti.hwnd = hDlg;
	ti.hinst = hInst;
	ti.uId = (UINT_PTR)GetDlgItem (hDlg, id);
	SendMessage (ToolTipHWND, TTM_DELTOOL, 0, (LPARAM) (LPTOOLINFO) &ti);
	if (crc32 == 0)
		return;
	_stprintf (tmp, _T("CRC=%08X"), crc32);
	ti.lpszText = tmp;
	SendMessage (ToolTipHWND, TTM_ADDTOOL, 0, (LPARAM) (LPTOOLINFO) &ti);
}

static void updatedfname(HWND hDlg, const TCHAR *text, int f_text, int type, int num)
{
	if (type == HISTORY_FLOPPY && DISK_isfloppybridge(&workprefs, num)) {
		TCHAR text2[MAX_DPATH];
		DISK_get_path_text(&workprefs, num, text2);
		xSendDlgItemMessage(hDlg, f_text, WM_SETTEXT, 0, (LPARAM)text2);
	} else {
		if (text)
			xSendDlgItemMessage(hDlg, f_text, WM_SETTEXT, 0, (LPARAM)text);
	}
}

static void addhistorymenu(HWND hDlg, const TCHAR *text, int f_text, int type, bool manglepath, int num)
{
	int i, j;
	TCHAR *s;
	UAEREG *fkey;
	int nn = 1;
	int curidx;

	if (f_text < 0)
		return;
	xSendDlgItemMessage(hDlg, f_text, CB_RESETCONTENT, 0, 0);
	fkey = read_disk_history (type);
	if (fkey == NULL)
		return;
	curidx = -1;
	i = 0;
	while (s = DISK_history_get (i, type)) {
		TCHAR tmpname[MAX_DPATH];
		if (_tcslen (s) == 0)
			break;
		i++;
		if (manglepath) {
			bool path = false;
			TCHAR tmppath[MAX_DPATH], *p, *p2;
			_tcscpy (tmppath, s);
			p = tmppath + _tcslen (tmppath) - 1;
			for (j = 0; uae_archive_extensions[j]; j++) {
				p2 = _tcsstr (tmppath, uae_archive_extensions[j]);
				if (p2) {
					p = p2;
					break;
				}
			}
			while (p > tmppath) {
				if (*p == '\\' || *p == '/') {
					path = true;
					break;
				}
				p--;
			}
			if (path) {
				_tcscpy (tmpname, p + 1);
			} else {
				_tcscpy(tmpname, p);
			}
			*++p = 0;
			if (tmppath[0] && path) {
				_tcscat (tmpname, _T(" { "));
				_tcscat (tmpname, tmppath);
				_tcscat (tmpname, _T(" }"));
			}
		} else {
			_tcscpy (tmpname, s);
		}
		if (f_text >= 0)
			xSendDlgItemMessage (hDlg, f_text, CB_ADDSTRING, 0, (LPARAM)tmpname);
		if (text && !_tcscmp (text, s))
			curidx = i - 1;
	}
	if (f_text >= 0) {
		if (curidx >= 0)
			xSendDlgItemMessage(hDlg, f_text, CB_SETCURSEL, curidx, 0);
		else 
			updatedfname(hDlg, text, f_text, type, num);
	}
	regclosetree (fkey);
}

static void addfloppyhistory (HWND hDlg)
{
	int f_text, max, n;

	if (currentpage == QUICKSTART_ID)
		max = 2;
	else if (currentpage == FLOPPY_ID)
		max = 4;
	else if (currentpage == DISK_ID)
		max = 1;
	else
		return;
	for (n = 0; n < max; n++) {
		if (currentpage == QUICKSTART_ID)
			f_text = floppybuttonsq[n][0];
		else if (currentpage == FLOPPY_ID)
			f_text = floppybuttons[n][0];
		else
			f_text = IDC_DISKTEXT;
		if (f_text >= 0) {
			TCHAR *name = workprefs.floppyslots[n].df;
			if (iscd(n))
				name = workprefs.cdslots[0].name;
			addhistorymenu(hDlg, name, f_text, iscd(n) ? HISTORY_CD : HISTORY_FLOPPY, true, n);
		}
	}
}

static void addcdtype (HWND hDlg, int id)
{
	TCHAR tmp[MAX_DPATH];
	xSendDlgItemMessage (hDlg, id, CB_RESETCONTENT, 0, 0L);
	WIN32GUI_LoadUIString (IDS_QS_CD_AUTO, tmp, sizeof tmp / sizeof (TCHAR));
	xSendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)tmp);
	WIN32GUI_LoadUIString (IDS_QS_CD_IMAGE, tmp, sizeof tmp / sizeof (TCHAR));
	xSendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)tmp);
	int cdtype = quickstart_cdtype;
	if (currentpage != QUICKSTART_ID) {
		if (full_property_sheet && !workprefs.cdslots[0].inuse && !workprefs.cdslots[0].name[0])
			cdtype = 0;
	}
	int cnt = 2;
	for (int drive = 'C'; drive <= 'Z'; ++drive) {
		TCHAR vol[100];
		_stprintf (vol, _T("%c:\\"), drive);
		int drivetype = GetDriveType (vol);
		if (drivetype == DRIVE_CDROM) {
			xSendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)vol);
			if (!_tcsicmp (vol, quickstart_cddrive)) {
				cdtype = quickstart_cdtype = cnt;
				_tcscpy (workprefs.cdslots[0].name, vol);
			}
			cnt++;
		}
	}
	xSendDlgItemMessage (hDlg, id, CB_SETCURSEL, cdtype, 0);
}

static void addfloppytype (HWND hDlg, int n)
{
	int state, chk;
	int nn;
	int fb = DISK_isfloppybridge(&workprefs, n);
	int showcd = 0;
	TCHAR *text;
	bool qs = currentpage == QUICKSTART_ID;

	int f_text = floppybuttons[n][0];
	int f_drive = floppybuttons[n][1];
	int f_eject = floppybuttons[n][2];
	int f_type = floppybuttons[n][3];
	int f_wp = floppybuttons[n][4];
	int f_wptext = floppybuttons[n][5];
	int f_si = floppybuttons[n][6];
	int f_enable = floppybuttons[n][7];
	int f_info = floppybuttons[n][8];

	text = workprefs.floppyslots[n].df;
	if (qs) {
		TCHAR tmp[MAX_DPATH];
		f_text = floppybuttonsq[n][0];
		f_drive = floppybuttonsq[n][1];
		f_type = floppybuttonsq[n][3];
		f_eject = floppybuttonsq[n][2];
		f_wp = floppybuttonsq[n][4];
		f_wptext = floppybuttonsq[n][5];
		f_si = -1;
		f_enable = floppybuttonsq[n][7];
		f_info = floppybuttonsq[n][8];
		if (iscd (n))
			showcd = 1;
		if (showcd) {
			nn = 1;
			hide(hDlg, f_wp, 1);
			hide(hDlg, f_wptext, 1);
			hide(hDlg, f_info, 1);
			hide(hDlg, f_type, 1);
			ew (hDlg, f_enable, FALSE);
			WIN32GUI_LoadUIString (IDS_QS_CD, tmp, sizeof tmp / sizeof (TCHAR));
			SetWindowText (GetDlgItem (hDlg, f_enable), tmp);
			addcdtype (hDlg, IDC_CD0Q_TYPE);
			hide(hDlg, IDC_CD0Q_TYPE, 0);
			text = workprefs.cdslots[0].name;
			regsetstr (NULL, _T("QuickStartCDDrive"), quickstart_cdtype >= 2 ? quickstart_cddrive : _T(""));
			regsetint (NULL, _T("QuickStartCDType"), quickstart_cdtype >= 2 ? 2 : quickstart_cdtype);
		} else {
			hide(hDlg, f_wp, 0);
			hide(hDlg, f_wptext, 0);
			hide(hDlg, f_info, 0);
			hide(hDlg, f_type, 0);
			if (n >= workprefs.nr_floppies) {
				nn = -1;
			} else {
				nn = fromdfxtype(n, quickstart_floppytype[n], quickstart_floppysubtype[n]);
			}
		}
	} else {
		nn = fromdfxtype(n, workprefs.floppyslots[n].dfxtype, workprefs.floppyslots[n].dfxsubtype);
	}
	if (!showcd && f_enable > 0 && n == 1 && qs) {
		static TCHAR drivedf1[MAX_DPATH];
		if (drivedf1[0] == 0)
			GetDlgItemText(hDlg, f_enable, drivedf1, sizeof drivedf1 / sizeof (TCHAR));
		ew (hDlg, f_enable, TRUE);
		SetWindowText (GetDlgItem (hDlg, f_enable), drivedf1);
		hide (hDlg, IDC_CD0Q_TYPE, 1);
	}

	if (nn < 0)
		state = FALSE;
	else
		state = TRUE;
	if (f_type >= 0 && nn >= 0) {
		xSendDlgItemMessage(hDlg, f_type, CB_SETCURSEL, nn, 0);
	}
	if (f_si >= 0) {
		TCHAR *path = DISK_get_saveimagepath(text, -2);
		ShowWindow (GetDlgItem(hDlg, f_si), !showcd && zfile_exists (path) ? SW_SHOW : SW_HIDE);
		xfree(path);
	}
	if (f_text >= 0)
		ew (hDlg, f_text, state && !fb);
	if (f_eject >= 0)
		ew (hDlg, f_eject, text[0] != 0 && !fb);
	if (f_drive >= 0)
		ew (hDlg, f_drive, state && !fb);
	if (f_enable >= 0) {
		if (qs) {
			if (quickstart_cd && n == 0 && currentpage == QUICKSTART_ID) {
				ew(hDlg, f_enable, TRUE);
			} else {
				ew(hDlg, f_enable, (n > 0 && workprefs.nr_floppies > 0) && !showcd);
			}
		} else {
			ew (hDlg, f_enable, TRUE);
		}
		CheckDlgButton (hDlg, f_enable, state ? BST_CHECKED : BST_UNCHECKED);
		TCHAR tmp[10];
		tmp[0] = 0;
		if (n < 2 || ((nn - 1 != 5) && (nn  - 1 != 6) && ( nn - 1 != 7))) {
			if (!showcd || n != 1)
				_stprintf(tmp, _T("DF%d:"), n);
		} else {
			int t = nn - 1 == 5 ? 40 : 80;
			_stprintf(tmp, _T("%c: (%d)"), n == 2 ? 'A' : 'B', t);
		}
		if (tmp[0])
			SetWindowText(GetDlgItem(hDlg, f_enable), tmp);
	}
	chk = !showcd && disk_getwriteprotect (&workprefs, text, n) && state == TRUE ? BST_CHECKED : 0;
	if (f_wp >= 0) {
		CheckDlgButton(hDlg, f_wp, chk);
	}
	if (f_info >= 0)
		ew (hDlg, f_info, (text[0] != 0 || fb) && nn >= 0);
	chk = !showcd && state && DISK_validate_filename (&workprefs, text, n, NULL, 0, NULL, NULL, NULL) ? TRUE : FALSE;
	if (f_wp >= 0) {
		ew (hDlg, f_wp, chk && !workprefs.floppy_read_only && !fb);
		if (f_wptext >= 0)
			ew (hDlg, f_wptext, chk);
	}
	if (f_type >= 0) {
		ew(hDlg, f_type, workprefs.floppyslots[n].dfxtype >= 0 && (!qs || n < workprefs.nr_floppies));
	}
}

static void floppyquickstartsave(void)
{
	bool qs = currentpage == QUICKSTART_ID;
	if (qs) {
		regsetint(NULL, _T("QuickStartFloppies"), quickstart_floppy);
		regsetint(NULL, _T("QuickStartDF0Type"), quickstart_floppytype[0]);
		regsetint(NULL, _T("QuickStartDF1Type"), quickstart_floppytype[1]);
		regsetint(NULL, _T("QuickStartDF0SubType"), quickstart_floppysubtype[0]);
		regsetint(NULL, _T("QuickStartDF1SubType"), quickstart_floppysubtype[1]);
		regsetstr(NULL, _T("QuickStartDF0SubTypeID"), quickstart_floppysubtypeid[0]);
		regsetstr(NULL, _T("QuickStartDF1SubTypeID"), quickstart_floppysubtypeid[1]);
	}
}

static void getfloppytype(HWND hDlg, int n, bool change)
{
	int f_text;
	int f_type;
	bool qs = currentpage == QUICKSTART_ID;

	if (qs) {
		f_text = floppybuttonsq[n][0];
		f_type = floppybuttonsq[n][3];
	} else {
		f_text = floppybuttons[n][0];
		f_type = floppybuttons[n][3];
	}
	int val = xSendDlgItemMessage(hDlg, f_type, CB_GETCURSEL, 0, 0L);
	int sub;
	
	int dfxtype = todfxtype(n, val, &sub);
	if (change && val != CB_ERR && (workprefs.floppyslots[n].dfxtype != dfxtype || workprefs.floppyslots[n].dfxsubtype != sub || (dfxtype == DRV_FB && sub == 0))) {
		workprefs.floppyslots[n].dfxtype = dfxtype;
		workprefs.floppyslots[n].dfxsubtype = sub;
		workprefs.floppyslots[n].dfxsubtypeid[0] = 0;
		if (workprefs.floppyslots[n].dfxtype == DRV_FB) {
			if (sub == 0) {
				FloppyBridgeAPI::showProfileConfigDialog(hDlg);
				floppybridge_reload_profiles();
				floppybridge_modified(-1);
				updatefloppytypes(hDlg);
				char *c = NULL;
				FloppyBridgeAPI::exportProfilesToString(&c);
				TCHAR *cc = au(c);
				regsetstr(NULL, _T("FloppyBridge"), cc);
				xfree(cc);
				workprefs.floppyslots[n].dfxtype = DRV_FB;
				sub = 1;
				workprefs.floppyslots[n].dfxsubtype = sub;
				if (bridgeprofiles.size() == 0) {
					workprefs.floppyslots[n].dfxtype = DRV_35_DD;
					workprefs.floppyslots[n].dfxsubtype = 0;
					sub = 0;
				}
				if (qs && quickstart_floppy > n) {
					quickstart_floppytype[n] = workprefs.floppyslots[n].dfxtype;
					quickstart_floppysubtype[n] = workprefs.floppyslots[n].dfxsubtype;
				}
			}
			if (sub > 0) {
				if (sub - 1 < bridgeprofiles.size()) {
					int nsub = sub - 1;
					TCHAR tmp[32];
					_stprintf(tmp, _T("%d:%s"), bridgeprofiles.at(nsub).profileID, bridgeprofiles.at(nsub).name);
					_tcscpy(workprefs.floppyslots[n].dfxsubtypeid, tmp);
					if (qs && quickstart_floppy > n) {
						_tcscpy(quickstart_floppysubtypeid[n], tmp);
					}
				}
			}
			if (qs && quickstart_floppy > n) {
				floppyquickstartsave();
			}
		}
		for (int i = 0; i < 4; i++) {
			if (i != n && workprefs.floppyslots[i].dfxtype == DRV_FB && sub == workprefs.floppyslots[i].dfxsubtype) {
				workprefs.floppyslots[n].dfxtype = DRV_35_DD;
				workprefs.floppyslots[n].dfxsubtype = 0;
			}
		}
		addfloppytype(hDlg, n);
		addhistorymenu(hDlg, NULL, f_text, HISTORY_FLOPPY, true, n);
		updatedfname(hDlg, workprefs.floppyslots[n].df, f_text, HISTORY_FLOPPY, n);
	}
}
static void getfloppytypeq(HWND hDlg, int n, bool type)
{
	bool qs = currentpage == QUICKSTART_ID;
	int f_enable = qs ? floppybuttonsq[n][7] : floppybuttons[n][7];
	int f_type = qs ? floppybuttonsq[n][3] : floppybuttons[n][3];
	int f_text = qs ? floppybuttonsq[n][0] : floppybuttons[n][0];
	struct floppyslot *fs = &workprefs.floppyslots[n];

	if (f_enable <= 0)
		return;
	if (iscd(n))
		return;
	int chk = ischecked(hDlg, f_enable) ? 0 : -1;
	int sub = qs ? quickstart_floppysubtype[n] : fs->dfxsubtype;
	if (!chk) {
		int res = xSendDlgItemMessage(hDlg, f_type, CB_GETCURSEL, 0, 0);
		if (res == CB_ERR) {
			if (qs) {
				res = quickstart_floppytype[n];
			} else {
				res = DRV_NONE;
			}
			if (res == DRV_NONE) {
				res = DRV_35_DD;
			}
			if (qs) {
				quickstart_floppytype[n] = DRV_NONE;
			}
		}
		chk = todfxtype(n, res, &sub);
	}
	if ((qs && (chk != quickstart_floppytype[n] || sub != quickstart_floppysubtype[n])) || (!qs && (chk != fs->dfxtype || sub != fs->dfxsubtype)) || type) {
		if (chk >= 0) {
			if (qs) {
				quickstart_floppytype[n] = chk;
				quickstart_floppysubtype[n] = sub;
			}
			fs->dfxtype = chk;
			fs->dfxsubtype = sub;
			if (chk == DRV_FB && sub - 1 < bridgeprofiles.size()) {
				TCHAR tmp[32];
				int nsub = sub - 1;
				_stprintf(tmp, _T("%d:%s"), bridgeprofiles.at(nsub).profileID, bridgeprofiles.at(nsub).name);
				if (qs) {
					_tcscpy(quickstart_floppysubtypeid[n], tmp);
				}
				_tcscpy(fs->dfxsubtypeid, tmp);
				floppybridge_modified(n);
			}
			if (qs) {
				if (n == 1) {
					quickstart_floppy = 2;
				} else {
					quickstart_floppy = 1;
				}
				workprefs.nr_floppies = quickstart_floppy;
			}
		} else {
			if (qs) {
				if (n == 1) {
					quickstart_floppy = 1;
				}
				if (quickstart_cd && n == 0) {
					quickstart_floppy = 0;
				}
				workprefs.nr_floppies = quickstart_floppy;
			}
			fs->dfxtype = DRV_NONE;
			fs->dfxsubtype = 0;
			fs->dfxsubtypeid[0] = 0;
		}

		floppybridge_init(&workprefs);
		addfloppytype(hDlg, n);
		updatedfname(hDlg, fs->df, f_text, HISTORY_FLOPPY, n);

		floppyquickstartsave();
	}
}

static int getfloppybox (HWND hDlg, int f_text, TCHAR *out, int maxlen, int type, int num)
{
	LRESULT val;
	TCHAR *p;
	int i;

	if (num >= 0 && DISK_isfloppybridge(&workprefs, num)) {

		out[0] = 0;

	} else {

		out[0] = 0;
		val = xSendDlgItemMessage(hDlg, f_text, CB_GETCURSEL, 0, 0L);
		if (val != CB_ERR) {
			int len = xSendDlgItemMessage(hDlg, f_text, CB_GETLBTEXTLEN, (WPARAM)val, 0);
			if (len < maxlen) {
				val = xSendDlgItemMessage(hDlg, f_text, CB_GETLBTEXT, (WPARAM)val, (LPARAM)out);
			}
		} else {
			xSendDlgItemMessage(hDlg, f_text, WM_GETTEXT, (WPARAM)maxlen, (LPARAM)out);
		}

		parsefilepath(out, maxlen);

		i = 0;
		while ((p = DISK_history_get(i, type))) {
			if (!_tcscmp(p, out)) {
				DISK_history_add(out, -1, type, 0);
				break;
			}
			i++;
		}

	}

	return out[0] ? 1 : 0;
}

bool gui_ask_disk(int drv, TCHAR *name)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	_tcscpy(changed_prefs.floppyslots[drv].df, name);
	DiskSelection(mon->hAmigaWnd, IDC_DF0 + drv, 22, &changed_prefs, NULL, NULL);
	_tcscpy(name, changed_prefs.floppyslots[drv].df);
	return true;
}

static void getfloppyname (HWND hDlg, int n, int cd, int f_text)
{
	TCHAR tmp[MAX_DPATH];

	if (getfloppybox (hDlg, f_text, tmp, sizeof (tmp) / sizeof (TCHAR), cd ? HISTORY_CD : HISTORY_FLOPPY, n)) {
		if (!cd) {
			disk_insert (n, tmp);
			_tcscpy (workprefs.floppyslots[n].df, tmp);
		} else {
			if (quickstart_cddrive[0])
				eject_cd ();
			_tcscpy (workprefs.cdslots[0].name, tmp);
		}
	}
}
static void getfloppyname (HWND hDlg, int n)
{
	int cd = iscd (n);
	int f_text = currentpage == QUICKSTART_ID ? floppybuttonsq[n][0] : floppybuttons[n][0];
	getfloppyname (hDlg, n, cd, f_text);
}

static void addallfloppies (HWND hDlg)
{
	int i;

	for (i = 0; i < 4; i++)
		addfloppytype (hDlg, i);
	addfloppyhistory (hDlg);
}

static void floppysetwriteprotect (HWND hDlg, int n, bool writeprotected)
{
	if (!iscd (n)) {
		if (disk_setwriteprotect (&workprefs, n, workprefs.floppyslots[n].df, writeprotected)) {
			if (!full_property_sheet)
				DISK_reinsert(n);
		}
		addfloppytype (hDlg, n);
	}
}

static void deletesaveimage (HWND hDlg, int num)
{
	TCHAR *p;
	if (iscd (num))
		return;
	p = DISK_get_saveimagepath(workprefs.floppyslots[num].df, -2);
	if (zfile_exists (p)) {
		if (!DeleteFile(p)) {
			struct mystat st;
			if (my_stat(p, &st)) {
				my_chmod(p, st.mode | FILEFLAG_WRITE);
				DeleteFile(p);
			}
		}
		if (!full_property_sheet)
			DISK_reinsert (num);
		addfloppytype (hDlg, num);
	}
	xfree(p);
}

static void diskselect (HWND hDlg, WPARAM wParam, struct uae_prefs *p, int drv, TCHAR *defaultpath)
{
	int cd = iscd (drv);
	MultiDiskSelection (hDlg, wParam, cd ? 17 : 0, &workprefs, defaultpath);
	if (!cd) {
		disk_insert (0, p->floppyslots[0].df);
		disk_insert (1, p->floppyslots[1].df);
		disk_insert (2, p->floppyslots[2].df);
		disk_insert (3, p->floppyslots[3].df);
	}
	addfloppytype (hDlg, drv);
	addfloppyhistory (hDlg);
}

static int diskselectmenu (HWND hDlg, WPARAM wParam)
{
	int id = GetDlgCtrlID ((HWND)wParam);
	int num = -1;
	switch (id)
	{
	case IDC_DF0:
	case IDC_DF0QQ:
		num = 0;
		break;
	case IDC_DF1:
	case IDC_DF1QQ:
		num = 1;
		break;
	case IDC_DF2:
		num = 2;
		break;
	case IDC_DF3:
		num = 3;
		break;
	}
	if (num >= 0) {
		TCHAR *s = favoritepopup (hDlg, iscd (num) ? -1 : num);
		if (s) {
			int num = id == IDC_DF0QQ ? 0 : 1;
			TCHAR tmp[MAX_DPATH];
			_tcscpy (tmp, s);
			xfree (s);
			diskselect (hDlg, id, &workprefs, num, tmp);
		}
		return 1;
	}
	return 0;
}


static INT_PTR CALLBACK FloppyDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;
	static TCHAR diskname[40] = { _T("") };
	static int dropopen;

	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch (msg)
	{
	case WM_INITDIALOG:
		{
			TCHAR ft35dd[20], ft35hd[20], ft35ddpc[20], ft35hdpc[20],  ft525sd[20];
			int df0texts[] = { IDC_DF0TEXT, IDC_DF1TEXT, IDC_DF2TEXT, IDC_DF3TEXT, -1 };

			WIN32GUI_LoadUIString (IDS_FLOPPYTYPE35DD, ft35dd, sizeof ft35dd / sizeof (TCHAR));
			WIN32GUI_LoadUIString (IDS_FLOPPYTYPE35HD, ft35hd, sizeof ft35hd / sizeof (TCHAR));
			WIN32GUI_LoadUIString (IDS_FLOPPYTYPE35DDPC, ft35ddpc, sizeof ft35ddpc / sizeof (TCHAR));
			WIN32GUI_LoadUIString (IDS_FLOPPYTYPE35HDPC, ft35hdpc, sizeof ft35hdpc / sizeof (TCHAR));
			WIN32GUI_LoadUIString (IDS_FLOPPYTYPE525SD, ft525sd, sizeof ft525sd / sizeof (TCHAR));
			pages[FLOPPY_ID] = hDlg;
			if (workprefs.floppy_speed > 0 && workprefs.floppy_speed < 10)
				workprefs.floppy_speed = 100;
			currentpage = FLOPPY_ID;
			xSendDlgItemMessage(hDlg, IDC_FLOPPYSPD, TBM_SETRANGE, TRUE, MAKELONG (0, 4));
			xSendDlgItemMessage(hDlg, IDC_FLOPPYSPD, TBM_SETPAGESIZE, 0, 1);
			xSendDlgItemMessage(hDlg, IDC_FLOPPYTYPE, CB_RESETCONTENT, 0, 0L);
			xSendDlgItemMessage(hDlg, IDC_FLOPPYTYPE, CB_ADDSTRING, 0, (LPARAM)ft35dd);
			xSendDlgItemMessage(hDlg, IDC_FLOPPYTYPE, CB_ADDSTRING, 0, (LPARAM)ft35hd);
			xSendDlgItemMessage(hDlg, IDC_FLOPPYTYPE, CB_ADDSTRING, 0, (LPARAM)ft35ddpc);
			xSendDlgItemMessage(hDlg, IDC_FLOPPYTYPE, CB_ADDSTRING, 0, (LPARAM)ft35hdpc);
			xSendDlgItemMessage(hDlg, IDC_FLOPPYTYPE, CB_ADDSTRING, 0, (LPARAM)ft525sd);
			xSendDlgItemMessage (hDlg, IDC_FLOPPYTYPE, CB_SETCURSEL, 0, 0);
			floppybridge_init(&workprefs);
			updatefloppytypes(hDlg);
			setmultiautocomplete (hDlg, df0texts);
			dropopen = 0;
		}
	case WM_USER:
		recursive++;
		setfloppytexts (hDlg, false);
		SetDlgItemText (hDlg, IDC_CREATE_NAME, diskname);
		xSendDlgItemMessage (hDlg, IDC_FLOPPYSPD, TBM_SETPOS, TRUE,
			workprefs.floppy_speed ? exact_log2 ((workprefs.floppy_speed) / 100) + 1 : 0);
		out_floppyspeed (hDlg);
		recursive--;
		break;

	case WM_CONTEXTMENU:
		recursive++;
		diskselectmenu (hDlg, wParam);
		setfloppytexts (hDlg, false);
		recursive--;
		break;

	case WM_COMMAND:
		if (recursive > 0)
			break;
		recursive++;
		if (HIWORD(wParam) == CBN_DROPDOWN)
			dropopen = 1;
		if (HIWORD(wParam) == CBN_CLOSEUP)
			dropopen = 0;
		if ((HIWORD(wParam) == CBN_SELCHANGE && !dropopen) || HIWORD(wParam) == CBN_KILLFOCUS || HIWORD(wParam) == CBN_CLOSEUP)  {
			bool upd = HIWORD(wParam) == CBN_KILLFOCUS || HIWORD(wParam) == CBN_CLOSEUP;
			switch (LOWORD (wParam))
			{
			case IDC_DF0TEXT:
			case IDC_DF0TEXTQ:
				getfloppyname (hDlg, 0);
				if (upd) {
					addfloppytype (hDlg, 0);
					addfloppyhistory (hDlg);
				}
				break;
			case IDC_DF1TEXT:
			case IDC_DF1TEXTQ:
				getfloppyname (hDlg, 1);
				if (upd) {
					addfloppytype (hDlg, 1);
					addfloppyhistory (hDlg);
				}
				break;
			case IDC_DF2TEXT:
				getfloppyname (hDlg, 2);
				if (upd) {
					addfloppytype (hDlg, 2);
					addfloppyhistory (hDlg);
				}
				break;
			case IDC_DF3TEXT:
				getfloppyname (hDlg, 3);
				if (upd) {
					addfloppytype (hDlg, 3);
					addfloppyhistory (hDlg);
				}
				break;
			case IDC_DF0TYPE:
				getfloppytype (hDlg, 0, HIWORD(wParam) == CBN_SELCHANGE);
				break;
			case IDC_DF1TYPE:
				getfloppytype (hDlg, 1, HIWORD(wParam) == CBN_SELCHANGE);
				break;
			case IDC_DF2TYPE:
				getfloppytype (hDlg, 2, HIWORD(wParam) == CBN_SELCHANGE);
				break;
			case IDC_DF3TYPE:
				getfloppytype (hDlg, 3, HIWORD(wParam) == CBN_SELCHANGE);
				break;
			case IDC_FLOPPYTYPE:
				int val = xSendDlgItemMessage (hDlg, IDC_FLOPPYTYPE, CB_GETCURSEL, 0, 0L);
				bool afloppy = val >= 0 && val <= 1;
				ew (hDlg, IDC_FLOPPY_FFS, afloppy);
				ew (hDlg, IDC_FLOPPY_BOOTABLE, afloppy);
				ew (hDlg, IDC_CREATE_NAME, afloppy);
				if (!afloppy) {
					setchecked (hDlg, IDC_FLOPPY_FFS, false);
					setchecked (hDlg, IDC_FLOPPY_BOOTABLE, false);
				}
				break;
			}
		}
		switch (LOWORD (wParam))
		{
		case IDC_DF0ENABLE:
		case IDC_DF0QENABLE:
			getfloppytypeq(hDlg, 0, true);
			break;
		case IDC_DF1ENABLE:
		case IDC_DF1QENABLE:
			getfloppytypeq(hDlg, 1, true);
			break;
		case IDC_DF0TYPE:
			getfloppytypeq(hDlg, 0, false);
			break;
		case IDC_DF1TYPE:
			getfloppytypeq(hDlg, 1, false);
			break;
		case IDC_DF2ENABLE:
			getfloppytypeq(hDlg, 2, true);
			break;
		case IDC_DF3ENABLE:
			getfloppytypeq(hDlg, 3, true);
			break;
		case IDC_DF0WP:
		case IDC_DF0WPQ:
			floppysetwriteprotect (hDlg, 0, currentpage == QUICKSTART_ID ? ischecked (hDlg, IDC_DF0WPQ) : ischecked (hDlg, IDC_DF0WP));
			break;
		case IDC_DF1WP:
		case IDC_DF1WPQ:
			floppysetwriteprotect (hDlg, 1, currentpage == QUICKSTART_ID ? ischecked (hDlg, IDC_DF1WPQ) : ischecked (hDlg, IDC_DF1WP));
			break;
		case IDC_DF2WP:
			floppysetwriteprotect (hDlg, 2, ischecked (hDlg, IDC_DF2WP));
			break;
		case IDC_DF3WP:
			floppysetwriteprotect (hDlg, 3, ischecked (hDlg, IDC_DF3WP));
			break;
		case IDC_DF0:
		case IDC_DF0QQ:
			diskselect (hDlg, wParam, &workprefs, 0, NULL);
			break;
		case IDC_DF1:
		case IDC_DF1QQ:
			diskselect (hDlg, wParam, &workprefs, 1, NULL);
			break;
		case IDC_DF2:
			diskselect (hDlg, wParam, &workprefs, 2, NULL);
			break;
		case IDC_DF3:
			diskselect (hDlg, wParam, &workprefs, 3, NULL);
			break;
		case IDC_INFO0:
		case IDC_INFO0Q:
			infofloppy (hDlg, 0);
			break;
		case IDC_INFO1:
		case IDC_INFO1Q:
			infofloppy (hDlg, 1);
			break;
		case IDC_INFO2:
			infofloppy (hDlg, 2);
			break;
		case IDC_INFO3:
			infofloppy (hDlg, 3);
			break;
		case IDC_EJECT0:
		case IDC_EJECT0Q:
			xSendDlgItemMessage (hDlg, IDC_DF0TEXT, CB_SETCURSEL, -1, 0);
			xSendDlgItemMessage (hDlg, IDC_DF0TEXTQ, CB_SETCURSEL, -1, 0);
			ejectfloppy (0);
			addfloppytype (hDlg, 0);
			break;
		case IDC_EJECT1:
		case IDC_EJECT1Q:
			xSendDlgItemMessage (hDlg, IDC_DF1TEXT, CB_SETCURSEL, -1, 0);
			xSendDlgItemMessage (hDlg, IDC_DF1TEXTQ, CB_SETCURSEL, -1, 0);
			ejectfloppy (1);
			addfloppytype (hDlg, 1);
			break;
		case IDC_EJECT2:
			xSendDlgItemMessage (hDlg, IDC_DF2TEXT, CB_SETCURSEL, -1, 0);
			ejectfloppy (2);
			addfloppytype (hDlg, 2);
			break;
		case IDC_EJECT3:
			xSendDlgItemMessage (hDlg, IDC_DF3TEXT, CB_SETCURSEL, -1, 0);
			ejectfloppy (3);
			addfloppytype (hDlg, 3);
			break;
		case IDC_SAVEIMAGE0:
			deletesaveimage (hDlg, 0);
			break;
		case IDC_SAVEIMAGE1:
			deletesaveimage (hDlg, 1);
			break;
		case IDC_SAVEIMAGE2:
			deletesaveimage (hDlg, 2);
			break;
		case IDC_SAVEIMAGE3:
			deletesaveimage (hDlg, 3);
			break;
		case IDC_CREATE:
			DiskSelection (hDlg, wParam, 1, &workprefs, NULL, NULL);
			addfloppyhistory (hDlg);
			break;
		case IDC_CREATE_RAW:
			DiskSelection (hDlg, wParam, 1, &workprefs, NULL, NULL);
			addfloppyhistory (hDlg);
			break;
		}
		GetDlgItemText (hDlg, IDC_CREATE_NAME, diskname, 30);
		recursive--;
		break;

	case WM_HSCROLL:
		workprefs.floppy_speed = (int)SendMessage (GetDlgItem (hDlg, IDC_FLOPPYSPD), TBM_GETPOS, 0, 0);
		if (workprefs.floppy_speed > 0) {
			workprefs.floppy_speed--;
			workprefs.floppy_speed = 1 << workprefs.floppy_speed;
			workprefs.floppy_speed *= 100;
		}
		out_floppyspeed (hDlg);
		break;
	}

	return FALSE;
}

static ACCEL SwapperAccel[] = {
	{ FALT|FVIRTKEY, '1', 10001 }, { FALT|FVIRTKEY, '2', 10002 }, { FALT|FVIRTKEY, '3', 10003 }, { FALT|FVIRTKEY, '4', 10004 }, { FALT|FVIRTKEY, '5', 10005 },
	{ FALT|FVIRTKEY, '6', 10006 }, { FALT|FVIRTKEY, '7', 10007 }, { FALT|FVIRTKEY, '8', 10008 }, { FALT|FVIRTKEY, '9', 10009 }, { FALT|FVIRTKEY, '0', 10010 },
	{ FALT|FSHIFT|FVIRTKEY, '1', 10011 }, { FALT|FSHIFT|FVIRTKEY, '2', 10012 }, { FALT|FSHIFT|FVIRTKEY, '3', 10013 }, { FALT|FSHIFT|FVIRTKEY, '4', 10014 }, { FALT|FSHIFT|FVIRTKEY, '5', 10015 },
	{ FALT|FSHIFT|FVIRTKEY, '6', 10016 }, { FALT|FSHIFT|FVIRTKEY, '7', 10017 }, { FALT|FSHIFT|FVIRTKEY, '8', 10018 }, { FALT|FSHIFT|FVIRTKEY, '9', 10019 }, { FALT|FSHIFT|FVIRTKEY, '0', 10020 },
	{ FVIRTKEY, VK_RIGHT, 10104 },
	{ FVIRTKEY|FSHIFT, VK_UP, IDC_UP }, { FVIRTKEY|FSHIFT, VK_DOWN, IDC_DOWN },
	{ FVIRTKEY|FCONTROL, '1', 10201 }, { FVIRTKEY|FCONTROL, '2', 10202 }, { FVIRTKEY|FCONTROL, '3', 10203 }, { FVIRTKEY|FCONTROL, '4', 10204 },
	{ FVIRTKEY|FCONTROL|FSHIFT, '1', 10205 }, { FVIRTKEY|FCONTROL|FSHIFT, '2', 10206 }, { FVIRTKEY|FCONTROL|FSHIFT, '3', 10207 }, { FVIRTKEY|FCONTROL|FSHIFT, '4', 10208 },
	{ FVIRTKEY, VK_RETURN, 10209 }, { FVIRTKEY, VK_DELETE, IDC_DISKLISTREMOVE },
	{ 0, 0, 0 }
};

static void swapperhili (HWND hDlg, int entry)
{
	ListView_SetItemState (cachedlist, entry,
		LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	SetDlgItemText (hDlg, IDC_DISKTEXT,  workprefs.dfxlist[entry]);
}

static void diskswapper_addfile2(struct uae_prefs *prefs, const TCHAR *file, int slot)
{
	while (slot < MAX_SPARE_DRIVES) {
		if (!prefs->dfxlist[slot][0]) {
			_tcscpy (prefs->dfxlist[slot], file);
			fullpath (prefs->dfxlist[slot], MAX_DPATH);
			break;
		}
		slot++;
	}
}

static int diskswapper_entry;

static void diskswapper_addfile(struct uae_prefs *prefs, const TCHAR *file, int slot)
{
	if (slot < 0) {
		slot = diskswapper_entry;
	}
	struct zdirectory *zd = zfile_opendir_archive (file, ZFD_ARCHIVE | ZFD_NORECURSE);
	if (zd && zfile_readdir_archive (zd, NULL, true) > 1) {
		TCHAR out[MAX_DPATH];
		while (zfile_readdir_archive (zd, out, true)) {
			struct zfile *zf = zfile_fopen (out, _T("rb"), ZFD_NORMAL);
			if (zf) {
				int type = zfile_gettype (zf);
				if (type == ZFILE_DISKIMAGE || type == ZFILE_EXECUTABLE) {
					diskswapper_addfile2(prefs, out, slot);
				}
				zfile_fclose (zf);
			}
		}
		zfile_closedir_archive (zd);
	} else {
		diskswapper_addfile2(prefs, file, slot);
	}
}

static int addswapperfile (HWND hDlg, int entry, TCHAR *newpath)
{
	TCHAR path[MAX_DPATH];
	int lastentry = entry;

	path[0] = 0;
	if (newpath)
		_tcscpy (path, newpath);
	if (MultiDiskSelection (hDlg, -1, 0, &changed_prefs, path)) {
		TCHAR dpath[MAX_DPATH];
		loopmulti (path, NULL);
		while (loopmulti (path, dpath) && entry < MAX_SPARE_DRIVES) {
			diskswapper_addfile(&workprefs, dpath, entry);
			lastentry = entry;
			entry++;
		}
		InitializeListView (hDlg);
		swapperhili (hDlg, lastentry);
	}
	return lastentry;
}

static INT_PTR CALLBACK SwapperDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;
	TCHAR tmp[MAX_DPATH];

	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch (msg)
	{
	case WM_INITDIALOG:
		pages[DISK_ID] = hDlg;
		currentpage = DISK_ID;
		InitializeListView (hDlg);
		addfloppyhistory (hDlg);
		diskswapper_entry = 0;
		swapperhili (hDlg, diskswapper_entry);
		setautocomplete (hDlg, IDC_DISKTEXT);
		break;
	case WM_LBUTTONUP:
		{
			int *draggeditems;
			int item = drag_end (hDlg, cachedlist, lParam, &draggeditems);
			if (item >= 0) {
				int i, item2;
				diskswapper_entry = item;
				for (i = 0; (item2 = draggeditems[i]) >= 0 && item2 < MAX_SPARE_DRIVES; i++, item++) {
					if (item != item2) {
						TCHAR tmp[1000];
						_tcscpy (tmp, workprefs.dfxlist[item]);
						_tcscpy (workprefs.dfxlist[item], workprefs.dfxlist[item2]);
						_tcscpy (workprefs.dfxlist[item2], tmp);
					}
				}
				InitializeListView(hDlg);
				swapperhili (hDlg, diskswapper_entry);
				return TRUE;
			}
			xfree (draggeditems);
		}
		break;
	case WM_MOUSEMOVE:
		if (drag_move (hDlg, lParam))
			return TRUE;
		break;
	case WM_CONTEXTMENU:
		if (GetDlgCtrlID ((HWND)wParam) == IDC_DISKLISTINSERT && diskswapper_entry >= 0) {
			TCHAR *s = favoritepopup (hDlg);
			if (s) {
				diskswapper_entry = addswapperfile (hDlg, diskswapper_entry, s);
				xfree (s);
			}
		}
		break;
	case WM_COMMAND:
		{
			switch (LOWORD (wParam))
			{
			case 10001:
			case 10002:
			case 10003:
			case 10004:
			case 10005:
			case 10006:
			case 10007:
			case 10008:
			case 10009:
			case 10010:
			case 10011:
			case 10012:
			case 10013:
			case 10014:
			case 10015:
			case 10016:
			case 10017:
			case 10018:
			case 10019:
			case 10020:
				diskswapper_entry = LOWORD (wParam) - 10001;
				swapperhili (hDlg, diskswapper_entry);
				break;
			case 10101:
				if (diskswapper_entry > 0) {
					diskswapper_entry--;
					swapperhili (hDlg, diskswapper_entry);
				}
				break;
			case 10102:
				if (diskswapper_entry >= 0 && diskswapper_entry < MAX_SPARE_DRIVES - 1) {
					diskswapper_entry++;
					swapperhili (hDlg, diskswapper_entry);
				}
				break;
			case 10103:
			case 10104:
				disk_swap (diskswapper_entry, 1);
				InitializeListView (hDlg);
				swapperhili (hDlg, diskswapper_entry);
				break;
			case 10201:
			case 10202:
			case 10203:
			case 10204:
				{
					int drv = LOWORD (wParam) - 10201;
					int i;
					if (workprefs.floppyslots[drv].dfxtype >= 0 && diskswapper_entry >= 0) {
						for (i = 0; i < 4; i++) {
							if (!_tcscmp (workprefs.floppyslots[i].df, workprefs.dfxlist[diskswapper_entry]))
								workprefs.floppyslots[i].df[0] = 0;
						}
						_tcscpy (workprefs.floppyslots[drv].df, workprefs.dfxlist[diskswapper_entry]);
						disk_insert (drv, workprefs.floppyslots[drv].df);
						InitializeListView (hDlg);
						swapperhili (hDlg, diskswapper_entry);
					}
				}
				break;
			case 10205:
			case 10206:
			case 10207:
			case 10208:
				{
					int drv = LOWORD (wParam) - 10201;
					workprefs.floppyslots[drv].df[0] = 0;
					InitializeListView (hDlg);
					swapperhili (hDlg, diskswapper_entry);
				}
				break;
			case 10209:
				{
				diskswapper_entry = addswapperfile (hDlg, diskswapper_entry, NULL);
				}
				break;

			case IDC_DISKLISTINSERT:
				if (diskswapper_entry >= 0) {
					if (getfloppybox (hDlg, IDC_DISKTEXT, tmp, sizeof (tmp) / sizeof (TCHAR), HISTORY_FLOPPY, -1)) {
						_tcscpy (workprefs.dfxlist[diskswapper_entry], tmp);
						addfloppyhistory (hDlg);
						InitializeListView (hDlg);
						swapperhili (hDlg, diskswapper_entry);
					} else {
						diskswapper_entry = addswapperfile (hDlg, diskswapper_entry, NULL);
					}
				}
				break;
			case IDC_DISKLISTREMOVE:
				if (diskswapper_entry >= 0) {
					workprefs.dfxlist[diskswapper_entry][0] = 0;
					InitializeListView (hDlg);
					swapperhili (hDlg, diskswapper_entry);
				}
				break;
			case IDC_DISKLISTREMOVEALL:
			{
				for (int i = 0; i < MAX_SPARE_DRIVES; i++) {
					workprefs.dfxlist[i][0] = 0;
				}
				InitializeListView (hDlg);
				swapperhili (hDlg, diskswapper_entry);
				break;
			}
			case IDC_UP:
				if (diskswapper_entry > 0) {
					_tcscpy (tmp, workprefs.dfxlist[diskswapper_entry - 1]);
					_tcscpy (workprefs.dfxlist[diskswapper_entry - 1], workprefs.dfxlist[diskswapper_entry]);
					_tcscpy (workprefs.dfxlist[diskswapper_entry], tmp);
					InitializeListView (hDlg);
					diskswapper_entry--;
					swapperhili (hDlg, diskswapper_entry);
				}
				break;
			case IDC_DOWN:
				if (diskswapper_entry >= 0 && diskswapper_entry < MAX_SPARE_DRIVES - 1) {
					_tcscpy (tmp, workprefs.dfxlist[diskswapper_entry + 1]);
					_tcscpy (workprefs.dfxlist[diskswapper_entry + 1], workprefs.dfxlist[diskswapper_entry]);
					_tcscpy (workprefs.dfxlist[diskswapper_entry], tmp);
					InitializeListView (hDlg);
					diskswapper_entry++;
					swapperhili (hDlg, diskswapper_entry);
				}
				break;
			}
			break;
		}
	case WM_NOTIFY:
		if (((LPNMHDR) lParam)->idFrom == IDC_DISKLIST)
		{
			int dblclick = 0, button = 0, col;
			HWND list;
			NM_LISTVIEW *nmlistview;
			nmlistview = (NM_LISTVIEW *) lParam;
			cachedlist = list = nmlistview->hdr.hwndFrom;
			switch (nmlistview->hdr.code)
			{
			case LVN_BEGINDRAG:
				drag_start (hDlg, cachedlist, lParam);
				break;
			case NM_RDBLCLK:
			case NM_DBLCLK:
				dblclick = 1;
				/* fall-through here too */
			case NM_RCLICK:
				if (nmlistview->hdr.code == NM_RCLICK || nmlistview->hdr.code == NM_RDBLCLK)
					button = 2;
			case NM_CLICK:
				diskswapper_entry = listview_entry_from_click (list, &col, false);
				if (diskswapper_entry >= 0) {
					if (col == 2) {
						if (button) {
							if (!dblclick) {
								if (disk_swap (diskswapper_entry, -1))
									InitializeListView (hDlg);
								swapperhili (hDlg, diskswapper_entry);
							}
						} else {
							if (!dblclick) {
								if (disk_swap (diskswapper_entry, 0))
									InitializeListView (hDlg);
								swapperhili (hDlg, diskswapper_entry);
							}
						}
					} else if (col == 1) {
						if (dblclick) {
							if (!button) {
								diskswapper_entry = addswapperfile (hDlg, diskswapper_entry, NULL);
							} else {
								workprefs.dfxlist[diskswapper_entry][0] = 0;
								InitializeListView (hDlg);
							}
						}
					}
					SetDlgItemText (hDlg, IDC_DISKTEXT,  workprefs.dfxlist[diskswapper_entry]);
				}
				break;
			}
		}
	}
	return FALSE;
}

static PRINTER_INFO_1 *pInfo = NULL;
static DWORD dwEnumeratedPrinters = 0;
struct serparportinfo *comports[MAX_SERPAR_PORTS];
struct midiportinfo *midiinportinfo[MAX_MIDI_PORTS];
struct midiportinfo *midioutportinfo[MAX_MIDI_PORTS];
static int ghostscript_available;

static int joyxprevious[4];
static BOOL bNoMidiIn = FALSE;

static void enable_for_gameportsdlg (HWND hDlg)
{
	int v = full_property_sheet;
	ew (hDlg, IDC_PORT_TABLET, v && workprefs.input_tablet != TABLET_REAL);
	ew (hDlg, IDC_PORT_TABLET_MODE, v && is_tablet());
	ew (hDlg, IDC_PORT_TABLET_LIBRARY, v && is_tablet());
	ew (hDlg, IDC_PORT_TABLET_CURSOR, v && workprefs.input_tablet > 0);
}

static void enable_for_portsdlg (HWND hDlg)
{
	int v;
	int isprinter, issampler;

	ew (hDlg, IDC_SWAP, TRUE);
#if !defined (SERIAL_PORT)
	ew(hDlg, IDC_MIDIOUTLIST, FALSE);
	ew(hDlg, IDC_MIDIINLIST, FALSE);
	ew(hDlg, IDC_SHARED, FALSE);
	ew(hDlg, IDC_SER_CTSRTS, FALSE);
	ew(hDlg, IDC_SER_RTSCTSDTRDTECD, FALSE);
	ew(hDlg, IDC_SER_RI, FALSE);
	ew(hDlg, IDC_SERIAL_DIRECT, FALSE);
	ew(hDlg, IDC_SERIAL, FALSE);
	ew(hDlg, IDC_UAESERIAL, FALSE);
#else
	v = workprefs.use_serial ? TRUE : FALSE;
	ew(hDlg, IDC_SER_SHARED, v);
	ew(hDlg, IDC_SER_CTSRTS, v);
	ew(hDlg, IDC_SER_RTSCTSDTRDTECD, v);
	ew(hDlg, IDC_SER_RI, v);
	ew(hDlg, IDC_SER_DIRECT, v);
	ew(hDlg, IDC_UAESERIAL, full_property_sheet);
#endif
	isprinter = true;
	issampler = true;
#if !defined (PARALLEL_PORT)
	isprinter = false;
	issampler = false;
#endif
	if (workprefs.prtname[0]) {
		issampler = false;
		workprefs.win32_samplersoundcard = -1;
	} else if (workprefs.win32_samplersoundcard >= 0) {
		isprinter = false;
	}
	ew (hDlg, IDC_PRINTERLIST, isprinter);
	ew (hDlg, IDC_SAMPLERLIST, issampler);
	ew (hDlg, IDC_SAMPLER_STEREO, issampler && workprefs.win32_samplersoundcard >= 0);
	ew (hDlg, IDC_PRINTERAUTOFLUSH, isprinter);
	ew (hDlg, IDC_PRINTERTYPELIST, isprinter);
	ew (hDlg, IDC_FLUSHPRINTER, isprinteropen () && isprinter ? TRUE : FALSE);
	ew (hDlg, IDC_PSPRINTER, full_property_sheet && ghostscript_available && isprinter ? TRUE : FALSE);
	ew (hDlg, IDC_PSPRINTERDETECT, full_property_sheet && isprinter ? TRUE : FALSE);
	ew (hDlg, IDC_PS_PARAMS, full_property_sheet && ghostscript_available && isprinter);
}

static const int joys[] = { IDC_PORT0_JOYS, IDC_PORT1_JOYS, IDC_PORT2_JOYS, IDC_PORT3_JOYS };
static const int joyssub[] = { IDC_PORT0_JOYSSUB, IDC_PORT1_JOYSSUB, IDC_PORT2_JOYSSUB, IDC_PORT3_JOYSSUB };
static const int joysm[] = { IDC_PORT0_JOYSMODE, IDC_PORT1_JOYSMODE, -1, -1 };
static const int joysaf[] = { IDC_PORT0_AF, IDC_PORT1_AF, -1, -1 };
static const int joyremap[] = { IDC_PORT0_REMAP, IDC_PORT1_REMAP, IDC_PORT2_REMAP, IDC_PORT3_REMAP };

#define MAX_PORTSUBMODES 16
static int portsubmodes[MAX_PORTSUBMODES];
static int portdevsub[MAX_JPORT_DEVS] = { -1, -1, -1, -1 };

static int get_jport_sub(HWND hDlg, int idx)
{
	int sub = 0;
	if (joyssub[idx] >= 0 && workprefs.input_advancedmultiinput) {
		sub = xSendDlgItemMessage(hDlg, joyssub[idx], CB_GETCURSEL, 0, 0L);
		if (sub < 0) {
			sub = 0;
		}
		portdevsub[idx] = sub;
	}
	return sub;
}

static void updatejoyport (HWND hDlg, int changedport)
{
	int i, j;
	TCHAR tmp[MAX_DPATH], tmp2[MAX_DPATH], tmp3[MAX_DPATH];

	SetDlgItemInt (hDlg, IDC_INPUTSPEEDM, workprefs.input_mouse_speed, FALSE);
	xSendDlgItemMessage (hDlg, IDC_PORT_TABLET_CURSOR, CB_SETCURSEL, workprefs.input_magic_mouse_cursor, 0);
	CheckDlgButton (hDlg, IDC_PORT_TABLET, workprefs.input_tablet > 0);
	xSendDlgItemMessage(hDlg, IDC_PORT_TABLET_MODE, CB_SETCURSEL, workprefs.input_tablet == TABLET_REAL ? 1 : 0, 0);
	if (!is_tablet())
		workprefs.tablet_library = false;
	CheckDlgButton (hDlg, IDC_PORT_TABLET_LIBRARY, workprefs.tablet_library);
	CheckDlgButton (hDlg, IDC_PORT_AUTOSWITCH, workprefs.input_autoswitch);

	if (joyxprevious[0] < 0)
		joyxprevious[0] = inputdevice_get_device_total (IDTYPE_JOYSTICK) + 1;
	if (joyxprevious[1] < 0)
		joyxprevious[1] = JSEM_LASTKBD + 1;

	for (i = 0; i < MAX_JPORTS; i++) {
		int total = 2;
		int idx = joyxprevious[i];
		int id = joys[i];
		int idm = joysm[i];
		int sub = get_jport_sub(hDlg, i);
		int v = workprefs.jports[i].jd[sub].id;
		int vm = workprefs.jports[i].jd[sub].mode + workprefs.jports[i].jd[sub].submode;
		TCHAR *p1, *p2;

		if (idm > 0)
			xSendDlgItemMessage (hDlg, idm, CB_SETCURSEL, vm, 0);

		xSendDlgItemMessage (hDlg, id, CB_RESETCONTENT, 0, 0L);
		xSendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)_T(""));
		WIN32GUI_LoadUIString (IDS_NONE, tmp, sizeof (tmp) / sizeof (TCHAR) - 3);
		_stprintf (tmp2, _T("<%s>"), tmp);
		xSendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)tmp2);
		WIN32GUI_LoadUIString (IDS_KEYJOY, tmp, sizeof (tmp) / sizeof (TCHAR));
		_tcscat (tmp, _T("\n"));
		p1 = tmp;
		for (;;) {
			p2 = _tcschr (p1, '\n');
			if (p2 && _tcslen (p2) > 0) {
				*p2++ = 0;
				xSendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)p1);
				total++;
				p1 = p2;
			} else
				break;
		}
		for (j = 0; j < inputdevice_get_device_total (IDTYPE_JOYSTICK); j++, total++)
			xSendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)inputdevice_get_device_name (IDTYPE_JOYSTICK, j));
		if (i < 2) {
			for (j = 0; j < inputdevice_get_device_total (IDTYPE_MOUSE); j++, total++)
				xSendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)inputdevice_get_device_name (IDTYPE_MOUSE, j));
		}
		WIN32GUI_LoadUIString(IDS_GAMEPORTS_CUSTOM, tmp3, sizeof(tmp3) / sizeof(TCHAR));
		for (j = 0; j < MAX_JPORTS_CUSTOM; j++, total++) {
			_stprintf(tmp2, _T("<%s>"), szNone.c_str());
			inputdevice_parse_jport_custom(&workprefs, j, i, tmp2);
			_stprintf(tmp, _T("%s %d: %s"), tmp3, j + 1, tmp2);
			xSendDlgItemMessage(hDlg, id, CB_ADDSTRING, 0, (LPARAM)tmp);
		}

		idx = inputdevice_getjoyportdevice (i, v);
		if (idx >= 0)
			idx += 2;
		else
			idx = 1;
		if (idx >= total)
			idx = 0;
		xSendDlgItemMessage (hDlg, id, CB_SETCURSEL, idx, 0);
		if (joysaf[i] >= 0)
			xSendDlgItemMessage (hDlg, joysaf[i], CB_SETCURSEL, workprefs.jports[i].jd[sub].autofire, 0);

		ew(hDlg, joyremap[i], idx >= 2);
		ew(hDlg, joysm[i], idx >= 2);
		ew(hDlg, joysaf[i], !JSEM_ISCUSTOM(i, sub, &workprefs) && idx >= 2);
	}
}

static void values_from_gameportsdlg (HWND hDlg, int d, int changedport)
{
	int i, success;
	int changed = 0;

	if (d) {
		i  = GetDlgItemInt (hDlg, IDC_INPUTSPEEDM, &success, FALSE);
		if (success)
			currprefs.input_mouse_speed = workprefs.input_mouse_speed = i;

		workprefs.input_mouse_untrap = xSendDlgItemMessage(hDlg, IDC_MOUSE_UNTRAPMODE, CB_GETCURSEL, 0, 0L);
		workprefs.input_magic_mouse_cursor = xSendDlgItemMessage (hDlg, IDC_PORT_TABLET_CURSOR, CB_GETCURSEL, 0, 0L);
		workprefs.input_autoswitch = ischecked (hDlg, IDC_PORT_AUTOSWITCH);
		workprefs.input_tablet = 0;
		i = xSendDlgItemMessage(hDlg, IDC_PORT_TABLET_MODE, CB_GETCURSEL, 0, 0L);
		if (ischecked (hDlg, IDC_PORT_TABLET) || i == 1) {
			if (!ischecked (hDlg, IDC_PORT_TABLET)) {
				setchecked(hDlg, IDC_PORT_TABLET, TRUE);
			}
			workprefs.input_tablet = TABLET_MOUSEHACK;
			if (i == 1)
				workprefs.input_tablet = TABLET_REAL;
		}
		workprefs.tablet_library = ischecked (hDlg, IDC_PORT_TABLET_LIBRARY);
		return;
	}

	for (i = 0; i < MAX_JPORTS; i++) {
		int idx = 0;
		int	sub = get_jport_sub(hDlg, i);
		int *port = &workprefs.jports[i].jd[sub].id;
		int *portm = &workprefs.jports[i].jd[sub].mode;
		int *portsm = &workprefs.jports[i].jd[sub].submode;
		int prevport = *port;
		int id = joys[i];
		int idm = joysm[i];
		int v = xSendDlgItemMessage (hDlg, id, CB_GETCURSEL, 0, 0L);
		if (v != CB_ERR && v > 0) {
			int max = JSEM_LASTKBD + inputdevice_get_device_total (IDTYPE_JOYSTICK);
			if (i < 2)
				max += inputdevice_get_device_total (IDTYPE_MOUSE);
			v -= 2;
			if (v < 0) {
				*port = JPORT_NONE;
			} else if (v >= max + MAX_JPORTS_CUSTOM) {
				*port = JPORT_NONE;
			} else if (v >= max) {
				*port = JSEM_CUSTOM + v - max;
			} else if (v < JSEM_LASTKBD) {
				*port = JSEM_KBDLAYOUT + (int)v;
			} else if (v >= JSEM_LASTKBD + inputdevice_get_device_total (IDTYPE_JOYSTICK)) {
				*port = JSEM_MICE + (int)v - inputdevice_get_device_total (IDTYPE_JOYSTICK) - JSEM_LASTKBD;
			} else {
				*port = JSEM_JOYS + (int)v - JSEM_LASTKBD;
			}
		}
		if (idm >= 0) {
			v = xSendDlgItemMessage (hDlg, idm, CB_GETCURSEL, 0, 0L);
			if (v != CB_ERR) {
				int vcnt = 0;
				*portsm = 0;
				for (int j = 0; j < MAX_PORTSUBMODES; j++) {
					if (v <= 0)
						break;
					if (portsubmodes[j] > 0) {
						if (v <= portsubmodes[j]) {
							*portsm = v;
						}
						v -= portsubmodes[j];
					} else {
						v--;
						vcnt++;
					}
				}
				*portm = vcnt;
			}
				
		}
		if (joysaf[i] >= 0) {
			int af = xSendDlgItemMessage (hDlg, joysaf[i], CB_GETCURSEL, 0, 0L);
			workprefs.jports[i].jd[sub].autofire = af;
		}
		if (*port != prevport)
			changed = 1;
	}
	if (changed)
		inputdevice_validate_jports (&workprefs, changedport, NULL);
}

static int midi2dev (struct midiportinfo **mid, int idx, int def)
{
	if (idx < 0)
		return def;
	if (mid[idx] == NULL)
		return def;
	return mid[idx]->devid;
}
static int midi2devidx (struct midiportinfo **mid, int devid)
{
	for (int i = 0; i < MAX_MIDI_PORTS; i++) {
		if (mid[i] != NULL && mid[i]->devid == devid)
			return i;
	}
	return -1;
}

static void values_from_portsdlg (HWND hDlg)
{
	int v;
	TCHAR tmp[256];
	BOOL success;
	int item;

	item = xSendDlgItemMessage (hDlg, IDC_SAMPLERLIST, CB_GETCURSEL, 0, 0L);
	if(item != CB_ERR) {
		workprefs.win32_samplersoundcard = item - 1;
		if (item > 0)
			workprefs.prtname[0] = 0;
	}
	workprefs.sampler_stereo = false;
	if (ischecked (hDlg, IDC_SAMPLER_STEREO))
		workprefs.sampler_stereo = true;

	item = xSendDlgItemMessage (hDlg, IDC_PRINTERLIST, CB_GETCURSEL, 0, 0L);
	if(item != CB_ERR) {
		int got = 0;
		_tcscpy (tmp, workprefs.prtname);
		if (item > 0) {
			workprefs.win32_samplersoundcard = -1;
			item--;
			if (item < dwEnumeratedPrinters) {
				_tcscpy (workprefs.prtname, pInfo[item].pName);
				got = 1;
			} else {
				int i;
				item -= dwEnumeratedPrinters;
				for (i = 0; i < 4; i++) {
					if ((paraport_mask & (1 << i)) && item == 0) {
						_stprintf (workprefs.prtname, _T("LPT%d"), i + 1);
						got = 1;
						break;
					}
					item--;
				}
			}
		}
		if (!got)
			workprefs.prtname[0] = 0;
#ifdef PARALLEL_PORT
		if (_tcscmp (workprefs.prtname, tmp))
			closeprinter ();
#endif
	}

	workprefs.win32_midioutdev = midi2dev (midioutportinfo, xSendDlgItemMessage (hDlg, IDC_MIDIOUTLIST, CB_GETCURSEL, 0, 0) - 1, -2);
	if (bNoMidiIn || workprefs.win32_midioutdev < -1) {
		workprefs.win32_midiindev = -1;
	} else {
		workprefs.win32_midiindev = midi2dev (midiinportinfo, xSendDlgItemMessage (hDlg, IDC_MIDIINLIST, CB_GETCURSEL, 0, 0) - 1, -1);
	}
	ew (hDlg, IDC_MIDIINLIST, workprefs.win32_midioutdev < -1 ? FALSE : TRUE);

	workprefs.win32_midirouter = ischecked (hDlg, IDC_MIDIROUTER);

	item = xSendDlgItemMessage (hDlg, IDC_SERIAL, CB_GETCURSEL, 0, 0L);
	if (item != CB_ERR && item > 0) {
		workprefs.use_serial = 1;
		_tcscpy (workprefs.sername, comports[item - 1]->dev);
	} else {
		workprefs.use_serial = 0;
		workprefs.sername[0] = 0;
	}
	workprefs.serial_demand = 0;
	if (ischecked(hDlg, IDC_SER_SHARED))
		workprefs.serial_demand = 1;
	workprefs.serial_hwctsrts = 0;
	if (ischecked(hDlg, IDC_SER_CTSRTS))
		workprefs.serial_hwctsrts = 1;
	workprefs.serial_rtsctsdtrdtecd = 0;
	if (ischecked(hDlg, IDC_SER_RTSCTSDTRDTECD))
		workprefs.serial_rtsctsdtrdtecd = 1;
	workprefs.serial_ri = 0;
	if (ischecked(hDlg, IDC_SER_RI))
		workprefs.serial_ri = 1;
	workprefs.serial_direct = 0;
	if (ischecked(hDlg, IDC_SER_DIRECT))
		workprefs.serial_direct = 1;

	workprefs.uaeserial = 0;
	if (ischecked (hDlg, IDC_UAESERIAL))
		workprefs.uaeserial = 1;

	GetDlgItemText (hDlg, IDC_PS_PARAMS, workprefs.ghostscript_parameters, sizeof workprefs.ghostscript_parameters / sizeof (TCHAR));
	v  = GetDlgItemInt (hDlg, IDC_PRINTERAUTOFLUSH, &success, FALSE);
	if (success)
		workprefs.parallel_autoflush_time = v;

	item = xSendDlgItemMessage (hDlg, IDC_DONGLELIST, CB_GETCURSEL, 0, 0L);
	if (item != CB_ERR)
		workprefs.dongle = item;

}

static void values_to_portsdlg (HWND hDlg)
{
	LRESULT result;
	int idx;

	xSendDlgItemMessage (hDlg, IDC_SAMPLERLIST, CB_SETCURSEL, workprefs.win32_samplersoundcard + 1, 0);
	CheckDlgButton (hDlg, IDC_SAMPLER_STEREO, workprefs.sampler_stereo);

	result = 0;
	if(workprefs.prtname[0]) {
		int i, got = 1;
		TCHAR tmp[10];
		result = xSendDlgItemMessage (hDlg, IDC_PRINTERLIST, CB_FINDSTRINGEXACT, -1, (LPARAM)workprefs.prtname);
		for (i = 0; i < 4; i++) {
			_stprintf (tmp, _T("LPT%d"), i + 1);
			if (!_tcscmp (tmp, workprefs.prtname)) {
				got = 0;
				if (paraport_mask & (1 << i))
					got = 1;
				break;
			}
		}
		if(result < 0 || got == 0) {
			// Warn the user that their printer-port selection is not valid on this machine
			TCHAR szMessage[MAX_DPATH];
			WIN32GUI_LoadUIString (IDS_INVALIDPRTPORT, szMessage, MAX_DPATH);
			pre_gui_message (szMessage);
			// Disable the invalid parallel-port selection
			workprefs.prtname[0] = 0;
			result = 0;
		}
	}
	SetDlgItemInt (hDlg, IDC_PRINTERAUTOFLUSH, workprefs.parallel_autoflush_time, FALSE);
	idx = workprefs.parallel_matrix_emulation;
	if (idx >= PARALLEL_MATRIX_EPSON24)
		idx = PARALLEL_MATRIX_EPSON24;
	if (workprefs.parallel_postscript_detection)
		idx = 4;
	if (workprefs.parallel_postscript_emulation)
		idx = 5;
	xSendDlgItemMessage (hDlg, IDC_PRINTERTYPELIST, CB_SETCURSEL, idx, 0);

	SetDlgItemText (hDlg, IDC_PS_PARAMS, workprefs.ghostscript_parameters);

	xSendDlgItemMessage (hDlg, IDC_PRINTERLIST, CB_SETCURSEL, result, 0);
	xSendDlgItemMessage (hDlg, IDC_MIDIOUTLIST, CB_SETCURSEL, midi2devidx (midioutportinfo, workprefs.win32_midioutdev) + 1, 0);
	if (workprefs.win32_midiindev >= 0)
		xSendDlgItemMessage (hDlg, IDC_MIDIINLIST, CB_SETCURSEL, midi2devidx (midiinportinfo, workprefs.win32_midiindev) + 1, 0);
	else
		xSendDlgItemMessage (hDlg, IDC_MIDIINLIST, CB_SETCURSEL, 0, 0);
	ew (hDlg, IDC_MIDIINLIST, workprefs.win32_midioutdev < -1 ? FALSE : TRUE);
	ew (hDlg, IDC_MIDIROUTER, workprefs.win32_midioutdev >= -1 && workprefs.win32_midiindev >= -1);
	CheckDlgButton (hDlg, IDC_MIDIROUTER, workprefs.win32_midirouter);

	CheckDlgButton(hDlg, IDC_UAESERIAL, workprefs.uaeserial);
	CheckDlgButton(hDlg, IDC_SER_SHARED, workprefs.serial_demand);
	CheckDlgButton(hDlg, IDC_SER_CTSRTS, workprefs.serial_hwctsrts);
	CheckDlgButton(hDlg, IDC_SER_RTSCTSDTRDTECD, workprefs.serial_rtsctsdtrdtecd);
	CheckDlgButton(hDlg, IDC_SER_RI, workprefs.serial_ri);
	CheckDlgButton(hDlg, IDC_SER_DIRECT, workprefs.serial_direct);

	if(!workprefs.sername[0])  {
		xSendDlgItemMessage (hDlg, IDC_SERIAL, CB_SETCURSEL, 0, 0L);
		workprefs.use_serial = 0;
	} else {
		int i;
		LRESULT result = -1;
		for (i = 0; i < MAX_SERPAR_PORTS && comports[i]; i++) {
			if (!_tcsncmp (workprefs.sername, _T("TCP:"), 4) && !_tcsncmp (comports[i]->dev, workprefs.sername, 4)) {
				const TCHAR *p1 = _tcschr(workprefs.sername + 4, ':');
				const TCHAR *p2 = _tcschr(comports[i]->dev + 4, ':');
				if (p1) {
					p1 = _tcschr(p1 + 1, '/');
				}
				if (p2) {
					p2 = _tcschr(p2 + 1, '/');
				}
				if ((p1 == NULL && p2 == NULL) || (p1 && p2 && !_tcsicmp(p1, p2))) {
					result = xSendDlgItemMessage (hDlg, IDC_SERIAL, CB_SETCURSEL, i + 1, 0L);
					break;
				}
			} else {
				if (!_tcscmp (comports[i]->dev, workprefs.sername) || (!_tcsncmp (workprefs.sername, _T("TCP:"), 4) && !_tcsncmp (comports[i]->dev, workprefs.sername, 4))) {
					result = xSendDlgItemMessage (hDlg, IDC_SERIAL, CB_SETCURSEL, i + 1, 0L);
					break;
				}
			}
		}
		if(result < 0 && workprefs.sername[0]) {
			// Warn the user that their COM-port selection is not valid on this machine
			TCHAR szMessage[MAX_DPATH];
			WIN32GUI_LoadUIString (IDS_INVALIDCOMPORT, szMessage, MAX_DPATH);
			pre_gui_message (szMessage);
			// Select "none" as the COM-port
			xSendDlgItemMessage (hDlg, IDC_SERIAL, CB_SETCURSEL, 0L, 0L);
			// Disable the chosen serial-port selection
			workprefs.sername[0] = 0;
			workprefs.use_serial = 0;
		} else {
			workprefs.use_serial = 1;
		}
	}
	xSendDlgItemMessage (hDlg, IDC_DONGLELIST, CB_SETCURSEL, workprefs.dongle, 0L);

}

static void init_portsdlg (HWND hDlg)
{
	static int first;
	int port;
	TCHAR tmp[MAX_DPATH];

	if (!first) {
		first = 1;
		if (load_ghostscript () > 0) {
			unload_ghostscript ();
			ghostscript_available = 1;
		}
	}
	if (!ghostscript_available) {
		workprefs.parallel_postscript_emulation = 0;
	}

	xSendDlgItemMessage(hDlg, IDC_DONGLELIST, CB_RESETCONTENT, 0, 0L);
	xSendDlgItemMessage(hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)szNone.c_str());
	xSendDlgItemMessage(hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)_T("RoboCop 3"));
	xSendDlgItemMessage(hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)_T("Leader Board"));
	xSendDlgItemMessage(hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)_T("B.A.T. II"));
	xSendDlgItemMessage(hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)_T("Italy '90 Soccer"));
	xSendDlgItemMessage(hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)_T("Dames Grand-Maitre"));
	xSendDlgItemMessage(hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)_T("Rugby Coach"));
	xSendDlgItemMessage(hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)_T("Cricket Captain"));
	xSendDlgItemMessage(hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)_T("Leviathan"));
	xSendDlgItemMessage(hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)_T("Music Master"));
	xSendDlgItemMessage(hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)_T("Logistics/SuperBase"));
	xSendDlgItemMessage(hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)_T("Scala MM (Red)"));
	xSendDlgItemMessage(hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)_T("Scala MM (Green)"));
	xSendDlgItemMessage(hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)_T("Striker Manager"));
	xSendDlgItemMessage(hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)_T("Multi-Player Soccer Manager"));
	xSendDlgItemMessage(hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)_T("Football Director 2"));

	xSendDlgItemMessage (hDlg, IDC_SERIAL, CB_RESETCONTENT, 0, 0L);
	xSendDlgItemMessage (hDlg, IDC_SERIAL, CB_ADDSTRING, 0, (LPARAM)szNone.c_str());
	for (port = 0; port < MAX_SERPAR_PORTS && comports[port]; port++) {
		if (!_tcsicmp(comports[port]->dev, SERIAL_INTERNAL)) {
			_tcscpy(tmp, comports[port]->name);
			if (!shmem_serial_state())
				shmem_serial_create();
			switch(shmem_serial_state())
			{
				case 1:
				_tcscat(tmp, _T(" [Master]"));
				break;
				case 2:
				_tcscat(tmp, _T(" [Slave]"));
				break;
			}
			xSendDlgItemMessage (hDlg, IDC_SERIAL, CB_ADDSTRING, 0, (LPARAM)tmp);
		} else {
			xSendDlgItemMessage (hDlg, IDC_SERIAL, CB_ADDSTRING, 0, (LPARAM)comports[port]->name);
		}
	}

	xSendDlgItemMessage (hDlg, IDC_PRINTERTYPELIST, CB_RESETCONTENT, 0, 0L);
	WIN32GUI_LoadUIString (IDS_PRINTER_PASSTHROUGH, tmp, MAX_DPATH);
	xSendDlgItemMessage (hDlg, IDC_PRINTERTYPELIST, CB_ADDSTRING, 0, (LPARAM)tmp);
	WIN32GUI_LoadUIString (IDS_PRINTER_ASCII, tmp, MAX_DPATH);
	xSendDlgItemMessage (hDlg, IDC_PRINTERTYPELIST, CB_ADDSTRING, 0, (LPARAM)tmp);
	WIN32GUI_LoadUIString (IDS_PRINTER_EPSON9, tmp, MAX_DPATH);
	xSendDlgItemMessage (hDlg, IDC_PRINTERTYPELIST, CB_ADDSTRING, 0, (LPARAM)tmp);
	WIN32GUI_LoadUIString (IDS_PRINTER_EPSON48, tmp, MAX_DPATH);
	xSendDlgItemMessage (hDlg, IDC_PRINTERTYPELIST, CB_ADDSTRING, 0, (LPARAM)tmp);
	WIN32GUI_LoadUIString (IDS_PRINTER_POSTSCRIPT_DETECTION, tmp, MAX_DPATH);
	xSendDlgItemMessage (hDlg, IDC_PRINTERTYPELIST, CB_ADDSTRING, 0, (LPARAM)tmp);
	WIN32GUI_LoadUIString (IDS_PRINTER_POSTSCRIPT_EMULATION, tmp, MAX_DPATH);
	xSendDlgItemMessage (hDlg, IDC_PRINTERTYPELIST, CB_ADDSTRING, 0, (LPARAM)tmp);

	xSendDlgItemMessage (hDlg, IDC_SAMPLERLIST, CB_RESETCONTENT, 0, 0L);
	xSendDlgItemMessage (hDlg, IDC_SAMPLERLIST, CB_ADDSTRING, 0, (LPARAM)szNone.c_str());
	enumerate_sound_devices ();
	for (int card = 0; card < MAX_SOUND_DEVICES && record_devices[card]; card++) {
		int type = record_devices[card]->type;
		TCHAR tmp[MAX_DPATH];
		_stprintf (tmp, _T("%s: %s"),
			type == SOUND_DEVICE_XAUDIO2 ? _T("XAudio2") : (type == SOUND_DEVICE_DS ? _T("DSOUND") : (type == SOUND_DEVICE_AL ? _T("OpenAL") : (type == SOUND_DEVICE_PA ? _T("PortAudio") : _T("WASAPI")))),
			record_devices[card]->name);
		if (type == SOUND_DEVICE_DS)
			xSendDlgItemMessage (hDlg, IDC_SAMPLERLIST, CB_ADDSTRING, 0, (LPARAM)tmp);
	}

	xSendDlgItemMessage (hDlg, IDC_PRINTERLIST, CB_RESETCONTENT, 0, 0L);
	xSendDlgItemMessage (hDlg, IDC_PRINTERLIST, CB_ADDSTRING, 0, (LPARAM)szNone.c_str());
	if(!pInfo) {
		int flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;
		DWORD needed = 0;
		EnumPrinters (flags, NULL, 1, (LPBYTE)pInfo, 0, &needed, &dwEnumeratedPrinters);
		if (needed > 0) {
			DWORD size = needed;
			pInfo = (PRINTER_INFO_1*)xcalloc (uae_u8, size);
			dwEnumeratedPrinters = 0;
			EnumPrinters (flags, NULL, 1, (LPBYTE)pInfo, size, &needed, &dwEnumeratedPrinters);
		}
		if (dwEnumeratedPrinters == 0) {
			xfree (pInfo);
			pInfo = 0;
		}
	}
	if (pInfo) {
		for(port = 0; port < (int)dwEnumeratedPrinters; port++)
			xSendDlgItemMessage (hDlg, IDC_PRINTERLIST, CB_ADDSTRING, 0, (LPARAM)pInfo[port].pName);
	} else {
		ew (hDlg, IDC_PRINTERLIST, FALSE);
	}
	if (paraport_mask) {
		int mask = paraport_mask;
		int i = 1;
		while (mask) {
			if (mask & 1) {
				TCHAR tmp[30];
				_stprintf (tmp, _T("LPT%d"), i);
				xSendDlgItemMessage (hDlg, IDC_PRINTERLIST, CB_ADDSTRING, 0, (LPARAM)tmp);
			}
			i++;
			mask >>= 1;
		}
	}

	xSendDlgItemMessage (hDlg, IDC_MIDIOUTLIST, CB_RESETCONTENT, 0, 0L);
	xSendDlgItemMessage (hDlg, IDC_MIDIOUTLIST, CB_ADDSTRING, 0, (LPARAM)szNone.c_str());
	for (port = 0; port < MAX_MIDI_PORTS && midioutportinfo[port]; port++) {
		TCHAR *n = midioutportinfo[port]->label ? midioutportinfo[port]->label : midioutportinfo[port]->name;
		xSendDlgItemMessage (hDlg, IDC_MIDIOUTLIST, CB_ADDSTRING, 0, (LPARAM)n);
	}
	ew (hDlg, IDC_MIDIOUTLIST, port > 0);

	xSendDlgItemMessage (hDlg, IDC_MIDIINLIST, CB_RESETCONTENT, 0, 0L);
	xSendDlgItemMessage (hDlg, IDC_MIDIINLIST, CB_ADDSTRING, 0, (LPARAM)szNone.c_str());
	for (port = 0; port < MAX_MIDI_PORTS && midiinportinfo[port]; port++) {
		TCHAR *n = midiinportinfo[port]->label ? midiinportinfo[port]->label : midiinportinfo[port]->name;
		xSendDlgItemMessage (hDlg, IDC_MIDIINLIST, CB_ADDSTRING, 0, (LPARAM)n);
	}
	bNoMidiIn = port == 0;
	ew (hDlg, IDC_MIDIINLIST, port > 0);

}

static void input_test (HWND hDlg, int);
static void ports_remap (HWND, int, int);

static void processport (HWND hDlg, bool reset, int port, int sub)
{
	if (reset)
		inputdevice_compa_clear (&workprefs, port, sub);
	values_from_gameportsdlg (hDlg, 0, port);
	enable_for_gameportsdlg (hDlg);
	updatejoyport (hDlg, port);
	inputdevice_updateconfig (NULL, &workprefs);
	inputdevice_config_change ();
}

/* Handle messages for the Joystick Settings page of our property-sheet */
static INT_PTR CALLBACK GamePortsDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	TCHAR tmp[MAX_DPATH];
	static int recursive = 0;
	static int first;
	int temp;

	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch (msg)
	{
		case WM_INITDIALOG:
		{
			recursive++;
			pages[GAMEPORTS_ID] = hDlg;
			currentpage = GAMEPORTS_ID;

			if (!first) {
				first = 1;
				joyxprevious[0] = -1;
				joyxprevious[1] = -1;
				joyxprevious[2] = -1;
				joyxprevious[3] = -1;
			}

			for (int i = 0; joysaf[i] >= 0; i++) {
				int id = joysaf[i];
				xSendDlgItemMessage (hDlg, id, CB_RESETCONTENT, 0, 0L);
				WIN32GUI_LoadUIString (IDS_PORT_AUTOFIRE_NO, tmp, MAX_DPATH);
				if (!remapcustoms[0].name)
					remapcustoms[0].name = my_strdup(tmp);
				xSendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)tmp);
				WIN32GUI_LoadUIString (IDS_PORT_AUTOFIRE, tmp, MAX_DPATH);
				if (!remapcustoms[1].name)
					remapcustoms[1].name = my_strdup(tmp);
				xSendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)tmp);
				WIN32GUI_LoadUIString (IDS_PORT_AUTOFIRE_TOGGLE, tmp, MAX_DPATH);
				if (!remapcustoms[2].name)
					remapcustoms[2].name = my_strdup(tmp);
				xSendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)tmp);
				WIN32GUI_LoadUIString (IDS_PORT_AUTOFIRE_ALWAYS, tmp, MAX_DPATH);
				if (!remapcustoms[3].name)
					remapcustoms[3].name = my_strdup(tmp);
				xSendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)tmp);
				WIN32GUI_LoadUIString(IDS_PORT_AUTOFIRE_TOGGLENOAF, tmp, MAX_DPATH);
				if (!remapcustoms[4].name)
					remapcustoms[4].name = my_strdup(tmp);
				xSendDlgItemMessage(hDlg, id, CB_ADDSTRING, 0, (LPARAM)tmp);
			}

			xSendDlgItemMessage (hDlg, IDC_PORT_TABLET_CURSOR, CB_RESETCONTENT, 0, 0L);
			WIN32GUI_LoadUIString (IDS_TABLET_BOTH_CURSORS, tmp, MAX_DPATH);
			xSendDlgItemMessage (hDlg, IDC_PORT_TABLET_CURSOR, CB_ADDSTRING, 0, (LPARAM)tmp);
			WIN32GUI_LoadUIString (IDS_TABLET_NATIVE_CURSOR, tmp, MAX_DPATH);
			xSendDlgItemMessage (hDlg, IDC_PORT_TABLET_CURSOR, CB_ADDSTRING, 0, (LPARAM)tmp);
			WIN32GUI_LoadUIString (IDS_TABLET_HOST_CURSOR, tmp, MAX_DPATH);
			xSendDlgItemMessage (hDlg, IDC_PORT_TABLET_CURSOR, CB_ADDSTRING, 0, (LPARAM)tmp);

			WIN32GUI_LoadUIString (IDS_MOUSE_UNTRAP_MODE, tmp, MAX_DPATH);
			xSendDlgItemMessage(hDlg, IDC_MOUSE_UNTRAPMODE, CB_RESETCONTENT, 0, 0L);
			TCHAR *p1 = tmp;
			for (;;) {
				TCHAR *p2 = _tcschr (p1, '\n');
				if (!p2)
					break;
				*p2++= 0;
				xSendDlgItemMessage (hDlg, IDC_MOUSE_UNTRAPMODE, CB_ADDSTRING, 0, (LPARAM)p1);
				p1 = p2;
			}
			xSendDlgItemMessage(hDlg, IDC_MOUSE_UNTRAPMODE, CB_SETCURSEL, workprefs.input_mouse_untrap, 0);

			WIN32GUI_LoadUIString (IDS_TABLET_MODE, tmp, MAX_DPATH);
			xSendDlgItemMessage(hDlg, IDC_PORT_TABLET_MODE, CB_RESETCONTENT, 0, 0L);
			p1 = tmp;
			for (;;) {
				TCHAR *p2 = _tcschr (p1, '\n');
				if (!p2)
					break;
				*p2++= 0;
				xSendDlgItemMessage (hDlg, IDC_PORT_TABLET_MODE, CB_ADDSTRING, 0, (LPARAM)p1);
				p1 = p2;
			}
			xSendDlgItemMessage(hDlg, IDC_PORT_TABLET_MODE, CB_SETCURSEL, workprefs.input_tablet == TABLET_REAL ? 1 : 0, 0);

			const int joys[] = { IDS_JOYMODE_DEFAULT, IDS_JOYMODE_WHEELMOUSE, IDS_JOYMODE_MOUSE,
				IDS_JOYMODE_JOYSTICK, IDS_JOYMODE_GAMEPAD, IDS_JOYMODE_JOYSTICKANALOG,
				IDS_JOYMODE_MOUSE_CDTV, IDS_JOYMODE_JOYSTICK_CD32,
				IDS_JOYMODE_LIGHTPEN,
				0 };

			for (int i = 0; i < 2; i++) {
				int id = i == 0 ? IDC_PORT0_JOYSMODE : IDC_PORT1_JOYSMODE;
				xSendDlgItemMessage(hDlg, id, CB_RESETCONTENT, 0, 0L);
				for (int j = 0; joys[j]; j++) {
					WIN32GUI_LoadUIString(joys[j], tmp, MAX_DPATH);
					p1 = tmp;
					_tcscat(p1, _T("\n"));
					for (;;) {
						TCHAR *p2 = _tcschr(p1, '\n');
						if (!p2)
							break;
						*p2++ = 0;
						xSendDlgItemMessage(hDlg, id, CB_ADDSTRING, 0, (LPARAM)p1);
						p1 = p2;
					}
				}
			}
			for (int i = 0; i < MAX_JPORTS; i++) {
				if (joyssub[i] >= 0) {
					xSendDlgItemMessage(hDlg, joyssub[i], CB_RESETCONTENT, 0, 0L);
					for (int j = 0; j < MAX_JPORT_DEVS; j++) {
						_stprintf(tmp, _T("%d"), j + 1);
						xSendDlgItemMessage(hDlg, joyssub[i], CB_ADDSTRING, 0, (LPARAM)tmp);
					}
					if (portdevsub[i] < 0) {
						portdevsub[i] = 0;
					}
					xSendDlgItemMessage(hDlg, joyssub[i], CB_SETCURSEL, portdevsub[i], 0);
					hide(hDlg, joyssub[i], workprefs.input_advancedmultiinput == 0);
				}
			}
			for (int i = 0; i < MAX_PORTSUBMODES; i++) {
				portsubmodes[i] = 0;
			}
			portsubmodes[8] = 2;
			portsubmodes[9] = -1;

			inputdevice_updateconfig (NULL, &workprefs);
			enable_for_gameportsdlg (hDlg);
			updatejoyport (hDlg, -1);
			recursive--;
		}
		break;
	case WM_USER:
		recursive++;
		enable_for_gameportsdlg (hDlg);
		updatejoyport (hDlg, -1);
		recursive--;
		return TRUE;

	case WM_COMMAND:
		if (recursive > 0)
			break;
		recursive++;
		if (LOWORD (wParam) == IDC_SWAP) {
			inputdevice_swap_compa_ports (&workprefs, 0);
			temp = joyxprevious[0];
			joyxprevious[0] = joyxprevious[1];
			joyxprevious[1] = temp;
			enable_for_gameportsdlg (hDlg);
			updatejoyport (hDlg, -1);
			inputdevice_forget_unplugged_device(0, -1);
			inputdevice_forget_unplugged_device(1, -1);
		} else if (LOWORD (wParam) == IDC_PORT0_REMAP) {
			ports_remap (hDlg, 0, portdevsub[0]);
			enable_for_gameportsdlg (hDlg);
			updatejoyport (hDlg, -1);
		} else if (LOWORD (wParam) == IDC_PORT1_REMAP) {
			ports_remap (hDlg, 1, portdevsub[1]);
			enable_for_gameportsdlg (hDlg);
			updatejoyport (hDlg, -1);
		} else if (LOWORD (wParam) == IDC_PORT2_REMAP) {
			ports_remap (hDlg, 2, portdevsub[2]);
			enable_for_gameportsdlg (hDlg);
			updatejoyport (hDlg, -1);
		} else if (LOWORD (wParam) == IDC_PORT3_REMAP) {
			ports_remap (hDlg, 3, portdevsub[3]);
			enable_for_gameportsdlg (hDlg);
			updatejoyport (hDlg, -1);
		} else if (HIWORD (wParam) == CBN_SELCHANGE) {
			switch (LOWORD (wParam))
			{
				case IDC_PORT0_AF:
					processport (hDlg, false, 0, portdevsub[0]);
				break;
				case IDC_PORT1_AF:
					processport (hDlg, false, 1, portdevsub[1]);
				break;
				case IDC_PORT0_JOYS:
				case IDC_PORT1_JOYS:
				case IDC_PORT2_JOYS:
				case IDC_PORT3_JOYS:
				case IDC_PORT0_JOYSMODE:
				case IDC_PORT1_JOYSMODE:
				{
					int port = -1;
					if (LOWORD (wParam) == IDC_PORT0_JOYS || LOWORD (wParam) == IDC_PORT0_JOYSMODE)
						port = 0;
					if (LOWORD (wParam) == IDC_PORT1_JOYS || LOWORD (wParam) == IDC_PORT1_JOYSMODE)
						port = 1;
					if (LOWORD (wParam) == IDC_PORT2_JOYS)
						port = 2;
					if (LOWORD (wParam) == IDC_PORT3_JOYS)
						port = 3;
					if (port >= 0) {
						processport (hDlg, true, port, portdevsub[port]);
						inputdevice_forget_unplugged_device(port, portdevsub[port]);
					}
					break;
				}
				case IDC_PORT0_JOYSSUB:
				case IDC_PORT1_JOYSSUB:
				case IDC_PORT2_JOYSSUB:
				case IDC_PORT3_JOYSSUB:
					enable_for_gameportsdlg(hDlg);
					updatejoyport(hDlg, -1);
					break;
			}
		} else {
			values_from_gameportsdlg (hDlg, 1, -1);
			enable_for_gameportsdlg (hDlg);
		}
		recursive--;
		break;
	}
	return FALSE;
}

/* Handle messages for the IO Settings page of our property-sheet */
static INT_PTR CALLBACK IOPortsDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;

	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch (msg)
	{
	case WM_INITDIALOG:
		recursive++;
		pages[IOPORTS_ID] = hDlg;
		currentpage = IOPORTS_ID;
		init_portsdlg (hDlg);
		enable_for_portsdlg (hDlg);
		values_to_portsdlg (hDlg);
		recursive--;
		break;
	case WM_USER:
		recursive++;
		enable_for_portsdlg (hDlg);
		recursive--;
		return TRUE;

	case WM_COMMAND:
		if (recursive > 0)
			break;
		recursive++;
		if (wParam == IDC_FLUSHPRINTER) {
			if (isprinter ()) {
				closeprinter ();
			}
		} else if (wParam == IDC_UAESERIAL || wParam == IDC_SER_SHARED || wParam == IDC_SER_DIRECT || wParam == IDC_SER_CTSRTS || wParam == IDC_SER_RTSCTSDTRDTECD || wParam == IDC_SER_RI ||
			wParam == IDC_PRINTERAUTOFLUSH || wParam == IDC_SAMPLER_STEREO || wParam == IDC_MIDIROUTER) {
			values_from_portsdlg (hDlg);
		} else {
			if (HIWORD (wParam) == CBN_SELCHANGE) {
				switch (LOWORD (wParam))
				{
				case IDC_SAMPLERLIST:
				case IDC_PRINTERLIST:
				case IDC_SERIAL:
				case IDC_MIDIOUTLIST:
				case IDC_MIDIINLIST:
				case IDC_DONGLELIST:
					values_from_portsdlg (hDlg);
					inputdevice_updateconfig (NULL, &workprefs);
					inputdevice_config_change ();
					enable_for_portsdlg (hDlg);
					break;
				case IDC_PRINTERTYPELIST:
					{
						int item = xSendDlgItemMessage (hDlg, IDC_PRINTERTYPELIST, CB_GETCURSEL, 0, 0L);
						workprefs.parallel_postscript_detection = workprefs.parallel_postscript_emulation = false;
						workprefs.parallel_matrix_emulation = 0;
						switch (item)
						{
						case 1:
							workprefs.parallel_matrix_emulation = PARALLEL_MATRIX_TEXT;
							break;
						case 2:
							workprefs.parallel_matrix_emulation = PARALLEL_MATRIX_EPSON9;
							break;
						case 3:
							workprefs.parallel_matrix_emulation = PARALLEL_MATRIX_EPSON48;
							break;
						case 4:
							workprefs.parallel_postscript_detection = true;
							break;
						case 5:
							workprefs.parallel_postscript_detection = true;
							workprefs.parallel_postscript_emulation = true;
							break;
						}
					}
				}
			}
		}
		recursive--;
		break;
	}
	return FALSE;
}

static TCHAR *eventnames[INPUTEVENT_END];

static void values_to_inputdlg (HWND hDlg)
{
	xSendDlgItemMessage (hDlg, IDC_INPUTTYPE, CB_SETCURSEL, workprefs.input_selected_setting, 0);
	xSendDlgItemMessage (hDlg, IDC_INPUTDEVICE, CB_SETCURSEL, input_selected_device, 0);
	SetDlgItemInt (hDlg, IDC_INPUTDEADZONE, workprefs.input_joystick_deadzone, FALSE);
	SetDlgItemInt (hDlg, IDC_INPUTAUTOFIRERATE, workprefs.input_autofire_linecnt, FALSE);
	SetDlgItemInt (hDlg, IDC_INPUTSPEEDD, workprefs.input_joymouse_speed, FALSE);
	SetDlgItemInt (hDlg, IDC_INPUTSPEEDA, workprefs.input_joymouse_multiplier, FALSE);
	CheckDlgButton (hDlg, IDC_INPUTDEVICEDISABLE, (!input_total_devices || inputdevice_get_device_status (input_selected_device)) ? BST_CHECKED : BST_UNCHECKED);
	if (key_swap_hack == 2) {
		CheckDlgButton(hDlg, IDC_KEYBOARD_SWAPHACK, BST_INDETERMINATE);
	} else {
		setchecked(hDlg, IDC_KEYBOARD_SWAPHACK, key_swap_hack);
	}
}

static int askinputcustom (HWND hDlg, TCHAR *custom, int maxlen, DWORD titleid)
{
	HWND hwnd;
	TCHAR txt[MAX_DPATH];

	SAVECDS;
	hwnd = CustomCreateDialog(IDD_STRINGBOX, hDlg, StringBoxDialogProc, &cdstate);
	if (hwnd != NULL) {
		if (titleid != 0) {
			LoadString (hUIDLL, titleid, txt, MAX_DPATH);
			SetWindowText (hwnd, txt);
		}
		txt[0] = 0;
		SendMessage (GetDlgItem (hwnd, IDC_STRINGBOXEDIT), WM_SETTEXT, 0, (LPARAM)custom);
		while (cdstate.active) {
			MSG msg;
			int ret;
			WaitMessage ();
			while ((ret = GetMessage (&msg, NULL, 0, 0))) {
				if (ret == -1)
					break;
				if (!IsWindow (hwnd) || !IsDialogMessage (hwnd, &msg)) {
					TranslateMessage (&msg);
					DispatchMessage (&msg);
				}
				SendMessage (GetDlgItem (hwnd, IDC_STRINGBOXEDIT), WM_GETTEXT, sizeof txt / sizeof (TCHAR), (LPARAM)txt);
			}
		}
		if (cdstate.status == 1) {
			_tcscpy(custom, txt);
			RESTORECDS;
			return 1;
		}
	}
	RESTORECDS;
	return 0;
}

static void init_inputdlg_2 (HWND hDlg)
{
	TCHAR name1[256], name2[256];
	TCHAR custom1[MAX_DPATH], tmp1[MAX_DPATH];
	int cnt, index, af, aftmp, port;

	xSendDlgItemMessage (hDlg, IDC_INPUTAMIGA, CB_RESETCONTENT, 0, 0L);
	xSendDlgItemMessage (hDlg, IDC_INPUTAMIGA, CB_ADDSTRING, 0, (LPARAM)szNone.c_str());
	WIN32GUI_LoadUIString (IDS_INPUT_CUSTOMEVENT, tmp1, MAX_DPATH);
	xSendDlgItemMessage (hDlg, IDC_INPUTAMIGA, CB_ADDSTRING, 0, (LPARAM)tmp1);
	index = 0; af = 0; port = 0;
	input_selected_event = -1;
	if (input_selected_widget >= 0) {
		inputdevice_get_mapping (input_selected_device, input_selected_widget, NULL, &port, name1, custom1, input_selected_sub_num);
		cnt = 2;
		while(inputdevice_iterate (input_selected_device, input_selected_widget, name2, &aftmp)) {
			xfree (eventnames[cnt]);
			eventnames[cnt] = my_strdup (name2);
			if (name1 && !_tcscmp (name1, name2)) {
				index = cnt;
				af = aftmp;
			}
			cnt++;
			xSendDlgItemMessage (hDlg, IDC_INPUTAMIGA, CB_ADDSTRING, 0, (LPARAM)name2);
		}
		if (_tcslen (custom1) > 0)
			index = 1;
		if (index >= 0) {
			xSendDlgItemMessage (hDlg, IDC_INPUTAMIGA, CB_SETCURSEL, index, 0);
			xSendDlgItemMessage (hDlg, IDC_INPUTAMIGACNT, CB_SETCURSEL, input_selected_sub_num, 0);
			input_selected_event = index;
		}
	}
	if (input_selected_widget < 0 || workprefs.input_selected_setting == GAMEPORT_INPUT_SETTINGS || port > 0) {
		EnableWindow (GetDlgItem (hDlg, IDC_INPUTAMIGA), FALSE);
	} else {
		EnableWindow (GetDlgItem (hDlg, IDC_INPUTAMIGA), TRUE);
	}
}

static void init_inputdlg (HWND hDlg)
{
	int i, num;
	TCHAR buf[100], txt[100];
	TCHAR input_selected_device_name[100];

	input_selected_device_name[0] = 0;
	if (input_selected_device < 0) {
		int size = sizeof input_selected_device_name / sizeof (TCHAR);
		regquerystr (NULL, _T("InputDeviceSelected"), input_selected_device_name, &size);
	}

	xSendDlgItemMessage (hDlg, IDC_INPUTTYPE, CB_RESETCONTENT, 0, 0L);
	for (i = 0; i < GAMEPORT_INPUT_SETTINGS; i++) {
		xSendDlgItemMessage (hDlg, IDC_INPUTTYPE, CB_ADDSTRING, 0, (LPARAM)workprefs.input_config_name[i]);
	}
	WIN32GUI_LoadUIString (IDS_INPUT_GAMEPORTS, buf, sizeof (buf) / sizeof (TCHAR));
	xSendDlgItemMessage (hDlg, IDC_INPUTTYPE, CB_ADDSTRING, 0, (LPARAM)buf);

	xSendDlgItemMessage (hDlg, IDC_INPUTCOPYFROM, CB_RESETCONTENT, 0, 0L);
	WIN32GUI_LoadUIString (IDS_INPUT_COPY_CUSTOM, buf, sizeof (buf) / sizeof (TCHAR));
	for (i = 0; i < MAX_INPUT_SETTINGS - 1; i++) {
		_stprintf (txt, buf, i + 1);
		xSendDlgItemMessage (hDlg, IDC_INPUTCOPYFROM, CB_ADDSTRING, 0, (LPARAM)txt);
	}
	WIN32GUI_LoadUIString (IDS_INPUT_COPY_DEFAULT, buf, sizeof (buf) / sizeof (TCHAR));
	xSendDlgItemMessage (hDlg, IDC_INPUTCOPYFROM, CB_ADDSTRING, 0, (LPARAM)buf);
	xSendDlgItemMessage (hDlg, IDC_INPUTCOPYFROM, CB_ADDSTRING, 0, (LPARAM)_T("Default"));
	xSendDlgItemMessage (hDlg, IDC_INPUTCOPYFROM, CB_ADDSTRING, 0, (LPARAM)_T("Default (PC KB)"));
	xSendDlgItemMessage (hDlg, IDC_INPUTCOPYFROM, CB_SETCURSEL, input_copy_from, 0);


	xSendDlgItemMessage (hDlg, IDC_INPUTAMIGACNT, CB_RESETCONTENT, 0, 0L);
	for (i = 0; i < MAX_INPUT_SUB_EVENT; i++) {
		_stprintf (buf, _T("%d"), i + 1);
		xSendDlgItemMessage (hDlg, IDC_INPUTAMIGACNT, CB_ADDSTRING, 0, (LPARAM)buf);
	}
	xSendDlgItemMessage (hDlg, IDC_INPUTAMIGACNT, CB_SETCURSEL, input_selected_sub_num, 0);

	num = 0;
	xSendDlgItemMessage (hDlg, IDC_INPUTDEVICE, CB_RESETCONTENT, 0, 0L);
	for (i = 0; i < inputdevice_get_device_total (IDTYPE_JOYSTICK); i++, num++) {
		TCHAR *name = inputdevice_get_device_name (IDTYPE_JOYSTICK, i);
		if (!_tcsicmp (name, input_selected_device_name))
			input_selected_device = num;
		xSendDlgItemMessage (hDlg, IDC_INPUTDEVICE, CB_ADDSTRING, 0, (LPARAM)name);
	}
	for (i = 0; i < inputdevice_get_device_total (IDTYPE_MOUSE); i++, num++) {
		TCHAR *name = inputdevice_get_device_name (IDTYPE_MOUSE, i);
		if (!_tcsicmp (name, input_selected_device_name))
			input_selected_device = num;
		xSendDlgItemMessage (hDlg, IDC_INPUTDEVICE, CB_ADDSTRING, 0, (LPARAM)name);
	}
	for (i = 0; i < inputdevice_get_device_total (IDTYPE_KEYBOARD); i++, num++) {
		TCHAR *name = inputdevice_get_device_name (IDTYPE_KEYBOARD, i);
		if (!_tcsicmp (name, input_selected_device_name))
			input_selected_device = num;
		xSendDlgItemMessage (hDlg, IDC_INPUTDEVICE, CB_ADDSTRING, 0, (LPARAM)name);
	}
	for (i = 0; i < inputdevice_get_device_total (IDTYPE_INTERNALEVENT); i++, num++) {
		TCHAR *name = inputdevice_get_device_name (IDTYPE_INTERNALEVENT, i);
		if (!_tcsicmp (name, input_selected_device_name))
			input_selected_device = num;
		xSendDlgItemMessage (hDlg, IDC_INPUTDEVICE, CB_ADDSTRING, 0, (LPARAM)name);
	}
	input_total_devices = inputdevice_get_device_total (IDTYPE_JOYSTICK) +
		inputdevice_get_device_total (IDTYPE_MOUSE) +
		inputdevice_get_device_total (IDTYPE_KEYBOARD) + 
		inputdevice_get_device_total (IDTYPE_INTERNALEVENT);
	if (input_selected_device >= input_total_devices || input_selected_device < 0)
		input_selected_device = 0;
	InitializeListView (hDlg);

	init_inputdlg_2 (hDlg);
	values_to_inputdlg (hDlg);
}

static void enable_for_inputdlg (HWND hDlg)
{
	bool v = workprefs.input_selected_setting != GAMEPORT_INPUT_SETTINGS;
	ew (hDlg, IDC_INPUTLIST, TRUE);
	ew (hDlg, IDC_INPUTAMIGA, v);
	ew (hDlg, IDC_INPUTAMIGACNT, TRUE);
	ew (hDlg, IDC_INPUTDEADZONE, TRUE);
	ew (hDlg, IDC_INPUTAUTOFIRERATE, TRUE);
	ew (hDlg, IDC_INPUTSPEEDA, TRUE);
	ew (hDlg, IDC_INPUTSPEEDD, TRUE);
	ew (hDlg, IDC_INPUTCOPY, v);
	ew (hDlg, IDC_INPUTCOPYFROM, v);
	ew (hDlg, IDC_INPUTSWAP, v);
	ew (hDlg, IDC_INPUTDEVICEDISABLE, v);
	ew (hDlg, IDC_INPUTREMAP, v);
}

static void clearinputlistview (HWND hDlg)
{
	ListView_DeleteAllItems (GetDlgItem (hDlg, IDC_INPUTLIST));
}

static void doinputcustom (HWND hDlg, int newcustom)
{
	TCHAR custom1[CONFIG_BLEN];
	uae_u64 flags;
	int port;

	custom1[0] = 0;
	inputdevice_get_mapping (input_selected_device, input_selected_widget,
		&flags, &port, NULL, custom1, input_selected_sub_num);
	if (_tcslen (custom1) > 0 || newcustom) {
		if (askinputcustom (hDlg, custom1, sizeof custom1 / sizeof (TCHAR), IDS_SB_CUSTOMEVENT)) {
			if (custom1[0]) {
				inputdevice_set_mapping (input_selected_device, input_selected_widget,
					NULL, custom1, flags, port, input_selected_sub_num);
			} else {
				inputdevice_set_mapping(input_selected_device, input_selected_widget,
					NULL, NULL, 0, -1, input_selected_sub_num);
			}
		}
	}
}

static void values_from_inputdlgbottom (HWND hDlg)
{
	int v;
	BOOL success;

	v  = GetDlgItemInt (hDlg, IDC_INPUTDEADZONE, &success, FALSE);
	if (success) {
		currprefs.input_joystick_deadzone = workprefs.input_joystick_deadzone = v;
		currprefs.input_joystick_deadzone = workprefs.input_joymouse_deadzone = v;
	}
	v  = GetDlgItemInt (hDlg, IDC_INPUTAUTOFIRERATE, &success, FALSE);
	if (success)
		currprefs.input_autofire_linecnt = workprefs.input_autofire_linecnt = v;
	v  = GetDlgItemInt (hDlg, IDC_INPUTSPEEDD, &success, FALSE);
	if (success)
		currprefs.input_joymouse_speed = workprefs.input_joymouse_speed = v;
	v  = GetDlgItemInt (hDlg, IDC_INPUTSPEEDA, &success, FALSE);
	if (success)
		currprefs.input_joymouse_multiplier = workprefs.input_joymouse_multiplier = v;
}

static void values_from_inputdlg (HWND hDlg, int inputchange)
{
	int doselect = 0;
	LRESULT item;
	bool iscustom = false;

	item = xSendDlgItemMessage (hDlg, IDC_INPUTAMIGACNT, CB_GETCURSEL, 0, 0L);
	if (item != CB_ERR && input_selected_sub_num != item) {
		input_selected_sub_num = (int)item;
		doselect = 0;
		init_inputdlg_2 (hDlg);
		update_listview_input (hDlg);
		return;
	}

	item = xSendDlgItemMessage (hDlg, IDC_INPUTTYPE, CB_GETCURSEL, 0, 0L);
	if (item != CB_ERR) {
		if (item != workprefs.input_selected_setting) {
			workprefs.input_selected_setting = (int)item;
			input_selected_widget = -1;
			inputdevice_updateconfig (NULL, &workprefs);
			enable_for_inputdlg (hDlg);
			InitializeListView (hDlg);
			values_to_inputdlg(hDlg);
			doselect = 1;
		}
	}
	item = xSendDlgItemMessage (hDlg, IDC_INPUTDEVICE, CB_GETCURSEL, 0, 0L);
	if (item != CB_ERR) {
		if (item != input_selected_device) {
			input_selected_device = (int)item;
			input_selected_widget = -1;
			input_selected_event = -1;
			InitializeListView (hDlg);
			init_inputdlg_2 (hDlg);
			values_to_inputdlg (hDlg);
			doselect = 1;
			regsetstr (NULL, _T("InputDeviceSelected"), inputdevice_get_device_name2 (input_selected_device));
		}
	}
	item = xSendDlgItemMessage (hDlg, IDC_INPUTAMIGA, CB_GETCURSEL, 0, 0L);
	if (item != CB_ERR) {
		if (item != input_selected_event) {
			uae_u64 flags;
			TCHAR custom[MAX_DPATH];
			input_selected_event = (int)item;
			doselect = 1;
			inputdevice_get_mapping (input_selected_device, input_selected_widget,
				&flags, NULL, 0, custom, input_selected_sub_num);
			if (item == 1 && custom[0] == 0) {
				doinputcustom (hDlg, 1);
				iscustom = true;
			}
		}
	}

	if (inputchange && doselect && input_selected_device >= 0 && input_selected_event >= 0) {
		uae_u64 flags;
		TCHAR custom[MAX_DPATH];

		if (!iscustom && eventnames[input_selected_event] && !_tcscmp (inputdevice_get_eventinfo (INPUTEVENT_SPC_CUSTOM_EVENT)->name, eventnames[input_selected_event])) {
			doinputcustom (hDlg, 1);
			iscustom = true;
		}
		inputdevice_get_mapping (input_selected_device, input_selected_widget,
			&flags, NULL, 0, custom, input_selected_sub_num);
		if (input_selected_event != 1 && !iscustom)
			custom[0] = 0;
		inputdevice_set_mapping (input_selected_device, input_selected_widget,
			eventnames[input_selected_event], _tcslen (custom) == 0 ? NULL : custom,
			flags, -1, input_selected_sub_num);
		update_listview_input (hDlg);
		inputdevice_updateconfig (NULL, &workprefs);
	}
}

static void input_swap (HWND hDlg)
{
	inputdevice_swap_ports (&workprefs, input_selected_device);
	init_inputdlg (hDlg);
}

static void showextramap (HWND hDlg)
{
	int evt;
	uae_u64 flags;
	TCHAR name[256];
	TCHAR custom[MAX_DPATH];
	TCHAR out[MAX_DPATH], out2[100];

	out[0] = 0;
	for (int i = 0; i < MAX_INPUT_SUB_EVENT; i++) {
		evt = inputdevice_get_mapping (input_selected_device, input_selected_widget,
			&flags, NULL, name, custom, i);
		if (evt <= 0 && !custom[0])
			continue;
		if (out[0])
			_tcscat (out, _T(" ; "));
		if (evt > 0) {
			_tcscat (out, name);
			const struct inputevent *ev = inputdevice_get_eventinfo(evt);
			if (ev->allow_mask == AM_K) {
				_stprintf(out + _tcslen(out), _T(" (0x%02x)"), ev->data);
			}
			if (flags & IDEV_MAPPED_AUTOFIRE_SET)
				_tcscat (out, _T(" (AF)"));
			if (flags & IDEV_MAPPED_TOGGLE)
				_tcscat (out, _T(" (T)"));
			if (flags & IDEV_MAPPED_INVERTTOGGLE)
				_tcscat (out, _T(" (IT)"));
			if (flags & IDEV_MAPPED_QUALIFIER_MASK) {
				bool gotone = false;
				_tcscat (out, _T(" Q("));
				for (int j = 0; j < MAX_INPUT_QUALIFIERS * 2; j++) {
					uae_u64 mask = IDEV_MAPPED_QUALIFIER1 << j;
					if (mask & flags) {
						if (gotone)
							_tcscat (out, _T(","));
						gotone = true;
						getqualifiername (out2, mask);
						_tcscat (out, out2);
					}
				}
				_tcscat (out, _T(")"));
			}
		}
		if (custom[0]) {
			_tcscat (out, _T("["));
			_tcscat (out, custom);
			_tcscat (out, _T("]"));
		}
	}
	SetWindowText (GetDlgItem (hDlg, IDC_INPUTMAPOUTM), out);
}

static void input_find (HWND hDlg, HWND mainDlg, int mode, int set, bool oneshot);
static int rawmode;
static int inputmap_remap_counter, inputmap_view_offset;
static int inputmap_remap_event;
static int inputmap_mode_cnt;
static int inputmap_selected;
static bool inputmap_oneshot;

#define INPUTMAP_F12 -1

#if 0
static void inputmap_next (HWND hDlg)
{
	HWND h = GetDlgItem (hDlg, IDC_INPUTMAPLIST);
	int inputmap = 1;
	if (inputmap == 1) {
		int mode, *events, *axistable;
		int max = inputdevice_get_compatibility_input (&workprefs, inputmap_port, &mode, &events, &axistable);
		inputmap_remap_counter++;
		if (inputmap_remap_counter >= max)
			inputmap_remap_counter = 0;
		int inputmap_index = inputmap_groupindex[inputmap_remap_counter];
		ListView_EnsureVisible (h, inputmap_index, FALSE);
		ListView_SetItemState (h, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
		ListView_SetItemState (h, inputmap_index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	} else if (inputmap == 2) {
		int itemcnt = ListView_GetItemCount (h);
		if (inputmap_view_offset >= itemcnt - 1 || inputmap_view_offset < 0) {
			inputmap_view_offset = 0;
		} else {
			inputmap_view_offset += ListView_GetCountPerPage (h);
			if (inputmap_view_offset >= itemcnt)
				inputmap_view_offset = itemcnt - 1;
		}
		ListView_EnsureVisible (h, inputmap_view_offset, FALSE);
	}
}
#endif

static void CALLBACK timerfunc (HWND hDlg, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
	int inputmap;
	WINDOWINFO pwi;
	HWND myDlg;

	if (idEvent != 1)
		return;

	if (pages[INPUTMAP_ID]) {
		inputmap = inputmap_remap_counter >= 0 ? 1 : (inputmap_remap_counter == -1 ? 2 : 3);
		setfocus (hDlg, IDC_INPUTMAPLIST);
		myDlg = hDlg;
	} else {
		inputmap = 0;
		setfocus (hDlg, IDC_INPUTLIST);
		myDlg = guiDlg;
	}

	pwi.cbSize = sizeof pwi;
	if (GetWindowInfo (myDlg, &pwi)) {
		// GUI inactive = disable capturing
		if (pwi.dwWindowStatus != WS_ACTIVECAPTION) {
			input_find (hDlg, myDlg, 0, false, false);
			return;
		}
	}

	int inputmap_index;
	int devnum, wtype, state;
	int cnt = inputdevice_testread_count ();
	if (cnt < 0) {
		input_find (hDlg, myDlg, 0, FALSE, false);
		return;
	}
	if (!cnt)
		return;
	int ret = inputdevice_testread (&devnum, &wtype, &state, true);
	if (ret > 0) {
		if (wtype == INPUTMAP_F12) {
			input_find (hDlg, myDlg, 0, FALSE, false);
			return;
		}
		if (input_selected_widget != devnum || input_selected_widget != wtype) {
			int od = input_selected_device;
			input_selected_device = devnum;
			input_selected_widget = wtype;
			int type = inputdevice_get_widget_type (input_selected_device, input_selected_widget, NULL, false);

			if (inputmap == 3) { // ports panel / add custom
				int mode;
				const int *axistable;
				int events[MAX_COMPA_INPUTLIST];

				int max = inputdevice_get_compatibility_input (&workprefs, inputmap_port, &mode, events, &axistable);
				if (max < MAX_COMPA_INPUTLIST - 1) {
					if (inputmap_remap_event > 0) {
						inputdevice_set_gameports_mapping (&workprefs, input_selected_device, input_selected_widget, inputmap_remap_event, 0, inputmap_port, workprefs.input_selected_setting);
					}
				}
				inputmap_remap_event = 0;
				inputdevice_generate_jport_custom(&workprefs, inputmap_port);
				InitializeListView (myDlg);
				input_find (hDlg, myDlg, 0, FALSE, false);
				return;

			} else if (inputmap == 1) { // ports panel / remap
				static int skipbuttonaxis;
				static int prevtype2, prevtype, prevwidget, prevevtnum, prevaxisevent, prevaxisstate;
				int widgets[10], widgetstate[10];
				int wcnt, found, axisevent, axisstate;

				HWND h = GetDlgItem (hDlg, IDC_INPUTMAPLIST);
				int mode;
				const int *axistable, *axistable2;
				int events[MAX_COMPA_INPUTLIST];
				
				int max = inputdevice_get_compatibility_input (&workprefs, inputmap_port, &mode, events, &axistable);
				if (inputmap_remap_counter >= max) {
					inputmap_remap_counter = 0;
				}
				int evtnum = events[inputmap_remap_counter];
				int type2 = intputdevice_compa_get_eventtype (evtnum, &axistable2);

				if (inputmap_remap_counter == 0) {
					prevtype = prevtype2 = prevwidget = prevevtnum = prevaxisevent = -1;
				}

				axisevent = -1;
				axisstate = 0;
				wcnt = 0;
				widgets[wcnt] = input_selected_widget;
				widgetstate[wcnt] = state;
				wcnt++;
				for (;;) {
					ret = inputdevice_testread (&devnum, &wtype, &state, false);
					if (ret <= 0 || wtype == -2)
						break;
					if (devnum != input_selected_device)
						continue;
					if (wcnt < 10) {
						widgets[wcnt] = wtype;
						widgetstate[wcnt] = state;
						wcnt++;
					}
				}
				
				found = 0;
				for (int i = 0; i < wcnt; i++) {
					input_selected_widget = widgets[i];
					type = inputdevice_get_widget_type (input_selected_device, input_selected_widget, NULL, false);
					if (type == IDEV_WIDGET_BUTTONAXIS) {
						found = 1;
						break;
					}
				}
				if (!found) {
					for (int i = 0; i < wcnt; i++) {
						input_selected_widget = widgets[i];
						type = inputdevice_get_widget_type (input_selected_device, input_selected_widget, NULL, false);
						if (type == IDEV_WIDGET_AXIS) {
							found = 2;
							break;
						}
					}
				}

				for (int i = 0; i < wcnt; i++) {
					int typex = inputdevice_get_widget_type (input_selected_device, widgets[i], NULL, false);
					if (typex == IDEV_WIDGET_AXIS) {
						if (!found) {
							found = 1;
							input_selected_widget = widgets[i];
						} else if (found == 1 && type == IDEV_WIDGET_BUTTONAXIS) {
							axisevent = widgets[i];
							axisstate = widgetstate[i];
						} else if (found == 2 && type == IDEV_WIDGET_AXIS) {
							axisevent = widgets[i];
							axisstate = widgetstate[i];
						}
						break;
					}
				}
				if (!found) {
					for (int i = 0; i < wcnt; i++) {
						input_selected_widget = widgets[i];
						type = inputdevice_get_widget_type (input_selected_device, input_selected_widget, NULL, false);
						if (type == IDEV_WIDGET_BUTTON || type == IDEV_WIDGET_KEY) {
							found = 1;
							break;
						}
					}
				}
				if (!found)
					return;
				
				//	if (inputmap_remap_counter > 4)
				//		write_log (_T("*"));

				//write_log (_T("%d %d %d %d %d\n"), input_selected_device, input_selected_widget, type, evtnum, type2);

				// if this and previous are same axis and they match (up/down or left/right)
				// and not oneshot mode
				if (!inputmap_oneshot && (inputmap_remap_counter & 1) == 1) {
					if (type2 == IDEV_WIDGET_BUTTONAXIS && prevtype2 == IDEV_WIDGET_BUTTONAXIS) {
						if (axisevent == prevaxisevent && (axisstate > 0 && prevaxisstate < 0)) {
							if ((type == IDEV_WIDGET_BUTTONAXIS && prevtype == IDEV_WIDGET_BUTTONAXIS) ||
								(type == IDEV_WIDGET_AXIS && prevtype == IDEV_WIDGET_AXIS)) {
								for (int i = 0; i < wcnt; i++) {
									wtype = widgets[i];
									if (inputdevice_get_widget_type (input_selected_device, wtype, NULL, false) == IDEV_WIDGET_AXIS) {
										inputdevice_set_gameports_mapping (&workprefs, input_selected_device, prevwidget, -1, 0, inputmap_port, workprefs.input_selected_setting);
										inputdevice_set_gameports_mapping (&workprefs, input_selected_device, wtype, axistable2[0], 0, inputmap_port, workprefs.input_selected_setting);
										evtnum = -1;
										break;
									}
								}
							}
						}
					}
				}

//				write_log (_T("%d %d %d\n"), input_selected_device, input_selected_widget, evtnum);
				if (evtnum >= 0)
					inputdevice_set_gameports_mapping (&workprefs, input_selected_device, input_selected_widget, evtnum, 0, inputmap_port, workprefs.input_selected_setting);

				inputdevice_generate_jport_custom(&workprefs, inputmap_port);

				InitializeListView (hDlg);
				inputmap_remap_counter++;
				if (inputmap_remap_counter >= max || inputmap_oneshot) {
					input_find (hDlg, myDlg, 0, FALSE, false);
					return;
				}
				
				inputmap_index = inputmap_groupindex[inputmap_remap_counter];
				ListView_EnsureVisible (h, inputmap_index, FALSE);
				ListView_SetItemState (h, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
				ListView_SetItemState (h, inputmap_index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
				TCHAR tmp[256];
				tmp[0] = 0;
				inputdevice_get_widget_type (input_selected_device, input_selected_widget, tmp, false);
				_tcscat (tmp, _T(", "));
				_tcscat (tmp, inputdevice_get_device_name2 (input_selected_device));
				SetWindowText (GetDlgItem (hDlg, IDC_INPUTMAPOUT), tmp);
				
				prevtype = type;
				prevtype2 = type2;
				prevwidget = input_selected_widget;
				prevevtnum = evtnum;
				prevaxisevent = axisevent;
				prevaxisstate = axisstate;

			} else if (inputmap == 2) { // ports panel / test

				static int toggle, skipactive;
				if (cnt == 2 && !skipactive) {
					toggle ^= 1;
					skipactive = 2;
				}
				if (skipactive) {
					skipactive--;
					if (cnt == 2 && toggle)
						return;
					if (cnt == 1 && !toggle)
						return;
				}

				bool found = false;
				HWND h = GetDlgItem (hDlg, IDC_INPUTMAPLIST);
				int op = inputmap_port;
				if (inputmap_handle (NULL, input_selected_device, input_selected_widget, &op, &inputmap_index, state, NULL, -1, 0, 0, NULL)) {
					if (op == inputmap_port) {
						ListView_EnsureVisible (h, 1, FALSE);
						ListView_EnsureVisible (h, inputmap_index, FALSE);
						ListView_SetItemState (h, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
						ListView_SetItemState (h, inputmap_index, LVIS_SELECTED , LVIS_SELECTED);
						ListView_SetItemState (h, inputmap_index, LVIS_FOCUSED, LVIS_FOCUSED);
						inputmap_view_offset = inputmap_index;
						found = true;
					}
				}
				if (!found) {
					ListView_SetItemState (h, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
				}
				TCHAR tmp[256];
				tmp[0] = 0;
				inputdevice_get_widget_type (input_selected_device, input_selected_widget, tmp, false);
				_tcscat (tmp, _T(", "));
				_tcscat (tmp, inputdevice_get_device_name2 (input_selected_device));
				SetWindowText (GetDlgItem (hDlg, IDC_INPUTMAPOUT), tmp);
				showextramap (hDlg);

			} else {
				// input panel

				if (od != devnum)
					init_inputdlg (hDlg);
				else
					init_inputdlg_2 (hDlg);

				static int toggle, skipactive;
				if (cnt == 2 && !skipactive) {
					toggle ^= 1;
					skipactive = 2;
				}
				if (skipactive) {
					skipactive--;
					if (cnt == 2 && toggle)
						return;
					if (cnt == 1 && !toggle)
						return;
				}
				if (rawmode == 1) {
					inputdevice_set_device_status (devnum, TRUE);
					values_to_inputdlg (hDlg);
				}
				HWND list = GetDlgItem(hDlg, IDC_INPUTLIST);
				int itemindex = input_get_lv_index(list, input_selected_widget);
				ListView_EnsureVisible (list, itemindex, FALSE);
				ListView_SetItemState (list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
				ListView_SetItemState (list, itemindex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
				if (rawmode == 1) {
					input_find (hDlg, myDlg, 0, FALSE, false);
					if (IsWindowEnabled (GetDlgItem (hDlg, IDC_INPUTAMIGA))) {
						setfocus (hDlg, IDC_INPUTAMIGA);
						xSendDlgItemMessage (hDlg, IDC_INPUTAMIGA, CB_SHOWDROPDOWN , TRUE, 0L);
					}
				}
			}
		}
	}
}

static HWND updatePanel (int id, UINT action);

static int rawdisable[] = {
	IDC_INPUTTYPE, 0, 0, IDC_INPUTDEVICE, 0, 0, IDC_INPUTDEVICEDISABLE, 0, 0,
	IDC_INPUTAMIGACNT, 0, 0, IDC_INPUTAMIGA, 0, 0, IDC_INPUTTEST, 0, 0, IDC_INPUTREMAP, 0, 0,
	IDC_INPUTCOPY, 0, 0, IDC_INPUTCOPYFROM, 0, 0, IDC_INPUTSWAP, 0, 0,
	IDC_INPUTDEADZONE, 0, 0, IDC_INPUTSPEEDD, 0, 0, IDC_INPUTAUTOFIRERATE, 0, 0, IDC_INPUTSPEEDA, 0, 0,
	IDC_PANELTREE, 1, 0, IDC_RESETAMIGA, 1, 0, IDC_QUITEMU, 1, 0, IDC_RESTARTEMU, 1, 0, IDOK, 1, 0, IDCANCEL, 1, 0, IDHELP, 1, 0,
	-1
};
static int rawdisable2[] = {
	IDC_INPUTMAP_DELETE, 0, 0, IDC_INPUTMAP_CAPTURE, 0, 0, IDC_INPUTMAP_CUSTOM, 0, 0,
	IDC_INPUTMAP_TEST, 0, 0, IDC_INPUTMAP_DELETEALL, 0, 0, IDC_INPUTMAP_EXIT, 0, 0,
	-1
};

static void inputmap_disable (HWND hDlg, bool disable)
{
	int *p = pages[INPUTMAP_ID] ? rawdisable2 : rawdisable;
	for (int i = 0; p[i] >= 0; i += 3) {
		HWND w = GetDlgItem (p[i + 1] ? guiDlg : hDlg, p[i]);
		if (w) {
			if (disable) {
				p[i + 2] = IsWindowEnabled (w);
				EnableWindow (w, FALSE);
			} else {
				EnableWindow (w, p[i + 2]);
			}
		}
	}
}

static void input_find (HWND hDlg, HWND mainDlg, int mode, int set, bool oneshot)
{
	static TCHAR tmp[200];
	if (set && !rawmode) {
		rawmode = mode ? 2 : 1;
		inputmap_oneshot = oneshot;
		inputmap_disable (hDlg, true);
		inputdevice_settest (TRUE);
		inputdevice_acquire (mode ? -1 : -2);
		TCHAR tmp2[MAX_DPATH];
		GetWindowText (guiDlg, tmp, sizeof tmp / sizeof (TCHAR));
		WIN32GUI_LoadUIString (IDS_REMAPTITLE, tmp2, sizeof tmp2 / sizeof (TCHAR));
		SetWindowText (mainDlg, tmp2);
		SetTimer (hDlg, 1, 30, timerfunc);
		ShowCursor (FALSE);
		SetCapture (mainDlg);
		RECT r;
		GetWindowRect (mainDlg, &r);
		ClipCursor (&r);
	} else if (rawmode) {
		KillTimer (hDlg, 1);
		ClipCursor (NULL);
		ReleaseCapture ();
		ShowCursor (TRUE);
		wait_keyrelease ();
		inputdevice_unacquire ();
		rawinput_release();
		inputmap_disable (hDlg, false);
		inputdevice_settest (FALSE);
		SetWindowText (mainDlg, tmp);
		SetFocus (hDlg);
		rawmode = FALSE;
		SetWindowText (GetDlgItem (hDlg, IDC_INPUTMAPOUT), _T(""));
		SetWindowText (GetDlgItem (hDlg, IDC_INPUTMAPOUTM), _T(""));
	}
}

#if 0
static void input_test (HWND hDlg, int port)
{
	inputmap_port_remap = -1;
	inputmap_port =-1;
	updatePanel (INPUTMAP_ID);
	for (int idx = 0; idx < MAX_JPORTS; idx++) {
		int mode, **events, *axistable;
		write_log (_T("Port: %d\n"), idx);
		if (inputdevice_get_compatibility_input (&workprefs, idx, &mode, &events, &axistable) > 0) {
			for (int k = 0; k < 2 && events[k]; k++) {
				int *p = events[k];
				int evtnum;
				for (int i = 0; (evtnum = p[i]) >= 0; i++) {
					int devnum;
					struct inputevent *evt = inputdevice_get_eventinfo (evtnum);
					int flags, status;
					TCHAR name[256];
					bool found = false;
					int *atp = axistable;
					int atpidx;

					write_log (_T("- %d: %s = "), i, evt->name);
					atpidx = 0;
					while (*atp >= 0) {
						if (*atp == evtnum) {
							atp++;
							atpidx = 2;
							break;
						}
						if (atp[1] == evtnum || atp[2] == evtnum) {
							atpidx = 1;
							break;
						}
						atp += 3;
					}
					while (atpidx >= 0) {
						devnum = 0;
						while ((status = inputdevice_get_device_status (devnum)) >= 0) {
							if (status) {
								for (int j = 0; j < inputdevice_get_widget_num (devnum); j++) {
									if (inputdevice_get_mapped_name (devnum, j, &flags, NULL, NULL, 0) == evtnum) {
										inputdevice_get_widget_type (devnum, j, name);
										write_log (_T("[%s, %s]"), inputdevice_get_device_name2 (devnum), name);
										found = true;
									}
								}
							}
							devnum++;
						}
						evtnum = *atp++;
						atpidx--;
					}
					if (!found)
						write_log (_T("<none>"));
					write_log (_T("\n"));
				}
			}
		} else {
			write_log (_T("<none>"));
		}
		write_log (_T("\n"));
	}

}
#endif

static void handleXbutton (WPARAM wParam, int updown)
{
	int b = GET_XBUTTON_WPARAM (wParam);
	int num = (b & XBUTTON1) ? 3 : (b & XBUTTON2) ? 4 : -1;
	if (num >= 0)
		setmousebuttonstate (dinput_winmouse (), num, updown);
}

static void handlerawinput (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_INPUT) {
		handle_rawinput (lParam);
		DefWindowProc(hDlg, msg, wParam, lParam);
	}
}

static int getremapcounter(int item)
{
	for (int i = 0; inputmap_groupindex[i] >= 0; i++) {
		if (item < inputmap_groupindex[i + 1] || inputmap_groupindex[i + 1] < 0)
			return i;
	}
	return 0;
}

static void fillinputmapadd (HWND hDlg)
{
	const int *axistable;
	int inputlist[MAX_COMPA_INPUTLIST];
	inputdevice_get_compatibility_input (&workprefs, inputmap_port, NULL, inputlist, &axistable);
	xSendDlgItemMessage (hDlg, IDC_INPUTMAPADD, CB_RESETCONTENT, 0, 0L);
	int evt = 1;
	for (;;) {
		bool ignore = false;
		const struct inputevent *ie = inputdevice_get_eventinfo (evt);
		if (!ie)
			break;
		if (_tcslen (ie->name) == 0) {
			evt++;
			continue;
		}
		for (int k = 0; axistable[k] >= 0; k += 3) {
			if (evt == axistable[k] || evt == axistable[k + 1] || evt == axistable[k + 2]) {
				for (int l = 0; inputlist[l] >= 0; l++) {
					if (inputlist[l] == axistable[k] || inputlist[l] == axistable[k + 1] || inputlist[l] == axistable[k + 2]) {
						ignore = true;
					}
				}
			}
		}
		if (!ignore)
			xSendDlgItemMessage (hDlg, IDC_INPUTMAPADD, CB_ADDSTRING, 0, (LPARAM)ie->name);
		evt++;
	}
}

static void remapspeciallistview(HWND list)
{
	uae_u64 flags = 0;
	inputmap_handle(NULL, -1, -1, NULL, NULL, -1, NULL, inputmap_selected, 0, 0, &flags);

	ListView_DeleteAllItems(list);

	for (int i = 0; remapcustoms[i].name; i++) {
		const struct remapcustoms_s *rc = &remapcustoms[i];
		TCHAR tmp[MAX_DPATH];
		_tcscpy(tmp, rc->name);
		LV_ITEM lvi = { 0 };
		lvi.mask = LVIF_TEXT | LVIF_PARAM;
		lvi.pszText = tmp;
		lvi.lParam = 0;
		lvi.iItem = i;
		lvi.iSubItem = 0;
		ListView_InsertItem(list, &lvi);

		tmp[0] = 0;
		if ((flags & rc->mask) == rc->flags) {
			_tcscpy(tmp, _T("*"));
		}

		ListView_SetItemText(list, i, 1, tmp);

	}
}

static INT_PTR CALLBACK RemapSpecialsProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;

	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	HWND list = GetDlgItem(hDlg, IDC_LISTDIALOG_LIST);

	switch (msg)
	{
	case WM_INITDIALOG:
	{
		recursive++;

		int lvflags = LVS_EX_DOUBLEBUFFER | LVS_EX_ONECLICKACTIVATE | LVS_EX_UNDERLINEHOT | LVS_EX_FULLROWSELECT;
		ListView_SetExtendedListViewStyleEx(list, lvflags, lvflags);

		LV_COLUMN lvc = { 0 };

		lvc.mask = LVCF_FMT | LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
		lvc.iSubItem = 0;
		lvc.fmt = LVCFMT_LEFT;
		lvc.pszText = _T("Option");
		lvc.cx = 150;
		ListView_InsertColumn(list, 0, &lvc);
		lvc.mask = LVCF_FMT | LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
		lvc.iSubItem = 1;
		lvc.fmt = LVCFMT_LEFT;
		lvc.pszText = _T("Selection");
		lvc.cx = 150;
		ListView_InsertColumn(list, 1, &lvc);


		remapspeciallistview(list);

		recursive--;
	}
	return TRUE;

	case WM_NOTIFY:
	if (((LPNMHDR)lParam)->idFrom == IDC_LISTDIALOG_LIST)
	{
		int column, entry;
		NM_LISTVIEW *nmlistview = (NM_LISTVIEW *)lParam;
		list = nmlistview->hdr.hwndFrom;
		switch (nmlistview->hdr.code)
		{
		case NM_RCLICK:
		case NM_CLICK:
		{
			entry = listview_entry_from_click(list, &column, false);
			if (entry >= 0 && inputmap_selected >= 0) {
				if (inputmap_handle(NULL, -1, -1, NULL, NULL, -1, NULL, inputmap_selected,
					remapcustoms[entry].flags, IDEV_MAPPED_AUTOFIRE_SET | IDEV_MAPPED_TOGGLE | IDEV_MAPPED_INVERTTOGGLE | IDEV_MAPPED_INVERT, NULL)) {
					inputdevice_generate_jport_custom(&workprefs, inputmap_port);
					CustomDialogClose(hDlg, -1);
					return TRUE;
				}
			}
		}
		break;
		}
	}
	break;
	case WM_COMMAND:
	if (recursive)
		break;
	recursive++;

	switch (wParam)
	{
	case IDC_LISTDIALOG_CLEAR:
	{
		break;
	}
	case IDOK:
	CustomDialogClose(hDlg, -1);
	recursive = 0;
	return TRUE;
	case IDCANCEL:
	CustomDialogClose(hDlg, 0);
	recursive = 0;
	return TRUE;
	}
	recursive--;
	break;
	}
	return FALSE;
}

static void input_remapspecials(HWND hDlg)
{
	CustomCreateDialogBox(IDD_LIST, hDlg, RemapSpecialsProc);
}

static INT_PTR CALLBACK InputMapDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	bool handled;
	INT_PTR v = commonproc(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return v;
	}

	static int recursive;
	HWND h = GetDlgItem (hDlg, IDC_INPUTMAPLIST);
	TCHAR tmp[256];
	int i;
	int inputmapselected_old = inputmap_selected;

	switch (msg)
	{
	case WM_CLOSE:
		CustomDialogClose(hDlg, 1);
		return TRUE;
	case WM_INITDIALOG:
	{
		inputmap_port_remap = -1;
		inputmap_remap_counter = -1;
		inputmap_view_offset = 0;
		inputmap_selected = -1;
		pages[INPUTMAP_ID] = hDlg;
		fillinputmapadd (hDlg);
		inputdevice_updateconfig (NULL, &workprefs);
		InitializeListView (hDlg);
		ew(hDlg, IDC_INPUTMAP_SPECIALS, inputmap_selected >= 0);
		if (!JSEM_ISCUSTOM(inputmap_port, 0, &workprefs)) {
			ew(hDlg, IDC_INPUTMAP_CAPTURE, FALSE);
			ew(hDlg, IDC_INPUTMAP_DELETE, FALSE);
			ew(hDlg, IDC_INPUTMAP_DELETEALL, FALSE);
			ew(hDlg, IDC_INPUTMAP_CUSTOM, FALSE);
			ew(hDlg, IDC_INPUTMAP_SPECIALS, FALSE);
			ew(hDlg, IDC_INPUTMAPADD, FALSE);
		}
		break;
	}
	case WM_DESTROY:
		input_find (hDlg, hDlg, 0, false, false);
		pages[INPUTMAP_ID] =  NULL;
		inputmap_port_remap = -1;
		inputmap_remap_counter = -1;
		inputmap_view_offset = 0;
		PostQuitMessage (0);
		return TRUE;
	break;
	case WM_NOTIFY:
		if (((LPNMHDR) lParam)->idFrom == IDC_INPUTMAPLIST) {
			NM_LISTVIEW *lv = (NM_LISTVIEW*)lParam;
			switch (lv->hdr.code)
			{
				case NM_DBLCLK:
					if (lv->iItem >= 0) {
						inputmap_selected = lv->iItem;
						inputmap_remap_counter = getremapcounter (lv->iItem);
						if (JSEM_ISCUSTOM(inputmap_port, 0, &workprefs)) {
							input_find (hDlg, hDlg, 1, true, true);
						}
						if (inputmapselected_old < 0)
							ew(hDlg, IDC_INPUTMAP_SPECIALS, TRUE);
					}
				return TRUE;

				case NM_CLICK:
					if (lv->iItem >= 0) {
						inputmap_selected = lv->iItem;
						inputmap_remap_counter = getremapcounter (lv->iItem);
						if (inputmapselected_old < 0)
							ew(hDlg, IDC_INPUTMAP_SPECIALS, TRUE);
					}
				return TRUE;
			}
		}
	break;
	case WM_COMMAND:
	{
		if (recursive)
			break;
		recursive++;
		switch(wParam)
		{
			case IDCANCEL:
			case IDOK:
			case IDC_INPUTMAP_EXIT:
			pages[INPUTMAP_ID] =  NULL;
			CustomDialogClose(hDlg, 1);
			recursive--;
			return TRUE;
			case IDC_INPUTMAP_TEST:
			inputmap_port_remap = -1;
			inputmap_remap_counter = -1;
			inputmap_view_offset = 0;
			input_find (hDlg, hDlg, 0, true, false);
			break;
			case IDC_INPUTMAP_CAPTURE:
			if (inputmap_remap_counter < 0)
				inputmap_remap_counter = 0;
			inputmap_port_remap = inputmap_port;
			inputdevice_compa_prepare_custom (&workprefs, inputmap_port, inputmap_port_sub, -1, false);
			inputdevice_updateconfig (NULL, &workprefs);
			InitializeListView (hDlg);
			ListView_EnsureVisible (h, inputmap_remap_counter, FALSE);
			ListView_SetItemState (h, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
			ListView_SetItemState (h, inputmap_remap_counter, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			input_find (hDlg, hDlg, 1, true, false);
			break;
			case IDC_INPUTMAP_SPECIALS:
			input_remapspecials(hDlg);
			InitializeListView(hDlg);
			break;
			case IDC_INPUTMAP_CUSTOM:
			tmp[0] = 0;
			xSendDlgItemMessage (hDlg, IDC_INPUTMAPADD, WM_GETTEXT, (WPARAM)sizeof tmp / sizeof (TCHAR), (LPARAM)tmp);
			i = 1;
			for (;;) {
				const struct inputevent *ie = inputdevice_get_eventinfo (i);
				if (!ie)
					break;
				if (_tcslen (ie->name) > 0 && !_tcsicmp (tmp, ie->name)) {
					inputmap_remap_counter = -2;
					inputmap_remap_event = i;
					inputmap_port_remap = inputmap_port;
					input_find (hDlg, hDlg, 1, true, false);
					break;
				}
				i++;
			}
			break;
			case IDC_INPUTMAP_DELETE:
			if (JSEM_ISCUSTOM(inputmap_port, 0, &workprefs)) {
				update_listview_inputmap (hDlg, inputmap_selected);
				inputdevice_generate_jport_custom(&workprefs, inputmap_port);
				InitializeListView (hDlg);
			}
			break;
			case IDC_INPUTMAP_DELETEALL:
			inputmap_remap_counter = 0;
			inputmap_port_remap = inputmap_port;
			inputdevice_compa_prepare_custom (&workprefs, inputmap_port, inputmap_port_sub, -1, true);
			inputdevice_updateconfig (NULL, &workprefs);
			fillinputmapadd (hDlg);
			InitializeListView (hDlg);
			ListView_EnsureVisible (h, inputmap_remap_counter, FALSE);
			ListView_SetItemState (h, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
			ListView_SetItemState (h, inputmap_remap_counter, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			ew (hDlg, IDC_INPUTMAP_CAPTURE, TRUE);
			ew (hDlg, IDC_INPUTMAP_DELETE, TRUE);
			ew (hDlg, IDC_INPUTMAP_CUSTOM, TRUE);
			break;
		}
		recursive--;
		break;
	}
	break;
	}
	handlerawinput (hDlg, msg, wParam, lParam);
	return FALSE;
}

static void ports_remap (HWND hDlg, int port, int sub)
{
	SAVECDS;
	inputmap_port = port;
	if (sub < 0) {
		sub = 0;
	}
	inputmap_port_sub = sub;
	HWND dlg = CustomCreateDialog(IDD_INPUTMAP, hDlg, InputMapDlgProc, &cdstate);
	if (dlg != NULL) {
		MSG msg;
		while(cdstate.active) {
			DWORD ret = GetMessage (&msg, NULL, 0, 0);
			if (ret == -1 || ret == 0)
				break;
			if (rawmode) {
				if (msg.message == WM_INPUT) {
					handlerawinput (msg.hwnd, msg.message, msg.wParam, msg.lParam);
					continue;
				}
				// eat all accelerators
				if (msg.message == WM_KEYDOWN || msg.message == WM_MOUSEMOVE || msg.message == WM_MOUSEWHEEL
					|| msg.message == WM_MOUSEHWHEEL || msg.message == WM_LBUTTONDOWN)
					continue;
			}
			// IsDialogMessage() eats WM_INPUT messages?!?!
			if (!rawmode && IsDialogMessage (dlg, &msg))
				continue;
			TranslateMessage (&msg);
			DispatchMessage (&msg);
		}
	}
	RESTORECDS;
}

static void input_togglesetmode (void)
{
	int evtnum;
	uae_u64 flags;
	TCHAR name[256];
	TCHAR custom[MAX_DPATH];

	if (input_selected_device < 0 || input_selected_widget < 0)
		return;
	evtnum = inputdevice_get_mapping (input_selected_device, input_selected_widget,
		&flags, NULL, name, custom, input_selected_sub_num);
	if (evtnum) {
		const struct inputevent *evt = inputdevice_get_eventinfo (evtnum);
		if (evt && (evt->allow_mask & AM_SETTOGGLE)) {
			if ((flags & (IDEV_MAPPED_SET_ONOFF | IDEV_MAPPED_SET_ONOFF_VAL1 | IDEV_MAPPED_SET_ONOFF_VAL2)) == (IDEV_MAPPED_SET_ONOFF | IDEV_MAPPED_SET_ONOFF_VAL1 | IDEV_MAPPED_SET_ONOFF_VAL2)) {
				flags &= ~(IDEV_MAPPED_SET_ONOFF | IDEV_MAPPED_SET_ONOFF_VAL1 | IDEV_MAPPED_SET_ONOFF_VAL1);
			} else if ((flags & (IDEV_MAPPED_SET_ONOFF | IDEV_MAPPED_SET_ONOFF_VAL2)) == (IDEV_MAPPED_SET_ONOFF | IDEV_MAPPED_SET_ONOFF_VAL2)) {
				flags |= IDEV_MAPPED_SET_ONOFF_VAL1;
			} else if ((flags & (IDEV_MAPPED_SET_ONOFF | IDEV_MAPPED_SET_ONOFF_VAL1)) == (IDEV_MAPPED_SET_ONOFF | IDEV_MAPPED_SET_ONOFF_VAL1)) {
				flags &= ~IDEV_MAPPED_SET_ONOFF_VAL1;
				flags |= IDEV_MAPPED_SET_ONOFF_VAL2;
			} else if (flags & IDEV_MAPPED_SET_ONOFF) {
				flags |= IDEV_MAPPED_SET_ONOFF_VAL1;
			} else {
				flags |= IDEV_MAPPED_SET_ONOFF;
				flags &= ~(IDEV_MAPPED_SET_ONOFF_VAL1 | IDEV_MAPPED_SET_ONOFF_VAL2);
			}
			inputdevice_set_mapping (input_selected_device, input_selected_widget,
				name, custom, flags, -1, input_selected_sub_num);
		}
	}
}

static void input_toggleautofire (void)
{
	int evt;
	uae_u64 flags;
	TCHAR name[256];
	TCHAR custom[MAX_DPATH];

	if (input_selected_device < 0 || input_selected_widget < 0)
		return;
	evt = inputdevice_get_mapping (input_selected_device, input_selected_widget,
		&flags, NULL, name, custom, input_selected_sub_num);
	if ((flags & (IDEV_MAPPED_INVERTTOGGLE | IDEV_MAPPED_AUTOFIRE_SET)) == (IDEV_MAPPED_INVERTTOGGLE | IDEV_MAPPED_AUTOFIRE_SET))
		flags &= ~(IDEV_MAPPED_INVERTTOGGLE | IDEV_MAPPED_AUTOFIRE_SET);
	else if (flags & IDEV_MAPPED_AUTOFIRE_SET) {
		flags |= IDEV_MAPPED_INVERTTOGGLE;
		flags &= ~IDEV_MAPPED_TOGGLE;
	} else if (!(flags & (IDEV_MAPPED_INVERTTOGGLE | IDEV_MAPPED_AUTOFIRE_SET)))
		flags |= IDEV_MAPPED_AUTOFIRE_SET;
	else
		flags &= ~(IDEV_MAPPED_INVERTTOGGLE | IDEV_MAPPED_AUTOFIRE_SET);
	inputdevice_set_mapping (input_selected_device, input_selected_widget,
		name, custom, flags, -1, input_selected_sub_num);
}

static int genericpopupmenu (HWND hwnd, TCHAR **items, int *flags, int num)
{
	int i, item;
	HMENU menu;
	POINT pt;

	menu = CreatePopupMenu ();
	for (i = 0; i < num; i++) {
		MENUITEMINFO mii = { 0 };
		mii.cbSize = sizeof mii;
		mii.fMask = MIIM_STRING | MIIM_ID | MIIM_STATE;
		mii.fType = MFT_STRING;
		mii.fState = MFS_ENABLED;
		if (flags[i])
			mii.fState |= MFS_CHECKED;
		mii.wID = i + 1;
		mii.dwTypeData = items[i];
		mii.cch = uaetcslen(mii.dwTypeData);
		InsertMenuItem (menu, -1, TRUE, &mii);
	}
	GetCursorPos (&pt);
	item = TrackPopupMenu (menu, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_RIGHTBUTTON | TPM_RETURNCMD,
		pt.x, pt.y, 0, hwnd, NULL);
	PostMessage (hwnd, WM_NULL, 0, 0);
	DestroyMenu (menu);
	return item - 1;
}

static void qualifierlistview(HWND list)
{
	uae_u64 flags;
	int evt;
	TCHAR name[256];
	TCHAR custom[MAX_DPATH];

	evt = inputdevice_get_mapping(input_selected_device, input_selected_widget,
		&flags, NULL, name, custom, input_selected_sub_num);

	ListView_DeleteAllItems(list);

	for (int i = 0; i < MAX_INPUT_QUALIFIERS; i++) {
		TCHAR tmp[MAX_DPATH];
		getqualifiername(tmp, IDEV_MAPPED_QUALIFIER1 << (i * 2));

		LV_ITEM lvi = { 0 };
		lvi.mask     = LVIF_TEXT | LVIF_PARAM;
		lvi.pszText  = tmp;
		lvi.lParam   = 0;
		lvi.iItem    = i;
		lvi.iSubItem = 0;
		ListView_InsertItem(list, &lvi);

		_tcscpy(tmp, _T("-"));
		if ((flags & (IDEV_MAPPED_QUALIFIER1 << (i * 2))) && (flags & (IDEV_MAPPED_QUALIFIER1 << (i * 2 + 1))))
			_tcscpy(tmp, _T("X"));
		if (flags & (IDEV_MAPPED_QUALIFIER1 << (i * 2)))
			_tcscpy(tmp, _T("*"));
		else if (flags & (IDEV_MAPPED_QUALIFIER1 << (i * 2 + 1)))
			_tcscpy(tmp, _T("R"));

		ListView_SetItemText(list, i, 1, tmp);
	}
}

static INT_PTR CALLBACK QualifierProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	bool handled;
	INT_PTR v = commonproc(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return v;
	}

	static int recursive = 0;
	HWND list = GetDlgItem (hDlg, IDC_LISTDIALOG_LIST);

	switch (msg)
	{
	case WM_INITDIALOG:
		{
			recursive++;

			int lvflags = LVS_EX_DOUBLEBUFFER | LVS_EX_ONECLICKACTIVATE | LVS_EX_UNDERLINEHOT | LVS_EX_FULLROWSELECT;
			ListView_SetExtendedListViewStyleEx (list, lvflags , lvflags);

			LV_COLUMN lvc = { 0 };

			lvc.mask     = LVCF_FMT | LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
			lvc.iSubItem = 0;
			lvc.fmt      = LVCFMT_LEFT;
			lvc.pszText  = _T("Qualifier");
			lvc.cx       = 150;
			ListView_InsertColumn (list, 0, &lvc);
			lvc.mask     = LVCF_FMT | LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
			lvc.iSubItem = 1;
			lvc.fmt      = LVCFMT_LEFT;
			lvc.pszText  = _T("Selection");
			lvc.cx       = 150;
			ListView_InsertColumn (list, 1, &lvc);


			qualifierlistview (list);

			recursive--;
		}
		return TRUE;

	case WM_NOTIFY:
		if (((LPNMHDR) lParam)->idFrom == IDC_LISTDIALOG_LIST)
		{
			uae_u64 flags;
			int evt;
			TCHAR name[256];
			TCHAR custom[MAX_DPATH];
			int column, entry;
			NM_LISTVIEW *nmlistview = (NM_LISTVIEW *) lParam;
			list = nmlistview->hdr.hwndFrom;
			switch (nmlistview->hdr.code)
			{
			case NM_RCLICK:
			case NM_CLICK:
				entry = listview_entry_from_click (list, &column, false);
				if (entry >= 0) {
					uae_u64 mask = IDEV_MAPPED_QUALIFIER1 << (entry * 2);
					evt = inputdevice_get_mapping (input_selected_device, input_selected_widget,
						&flags, NULL, name, custom, input_selected_sub_num);
					if (evt <= 0)
						name[0] = 0;
					if (flags & mask) {
						flags &= ~(mask | (mask << 1));
						flags |= mask << 1;
					} else if (flags & (mask << 1)) {
						flags &= ~(mask | (mask << 1));
					} else {
						flags &= ~(mask | (mask << 1));
						flags |= mask;
					}
					inputdevice_set_mapping (input_selected_device, input_selected_widget,
						name, custom, flags, -1, input_selected_sub_num);
					qualifierlistview (list);
				}
			}
		}
		break;
		case WM_COMMAND:
		if (recursive)
			break;
		recursive++;

		switch(wParam)
		{
		case IDC_LISTDIALOG_CLEAR:
			{
				uae_u64 flags;
				int evt;
				TCHAR name[256];
				TCHAR custom[MAX_DPATH];
				evt = inputdevice_get_mapping (input_selected_device, input_selected_widget,
					&flags, NULL, name, custom, input_selected_sub_num);
				flags &= ~IDEV_MAPPED_QUALIFIER_MASK;
				inputdevice_set_mapping (input_selected_device, input_selected_widget,
					name, custom, flags, -1, input_selected_sub_num);
				qualifierlistview (list);
				break;
			}
		case IDOK:
			CustomDialogClose(hDlg, -1);
			recursive = 0;
			return TRUE;
		case IDCANCEL:
			CustomDialogClose(hDlg, 0);
			recursive = 0;
			return TRUE;
		}
		recursive--;
		break;
	}
	return FALSE;
}

static void input_qualifiers (HWND hDlg)
{
	uae_u64 flags;
	int evt;
	TCHAR name[256];
	TCHAR custom[MAX_DPATH];
	
	if (input_selected_device < 0 || input_selected_widget < 0)
		return;
	evt = inputdevice_get_mapping (input_selected_device, input_selected_widget,
		&flags, NULL, name, custom, input_selected_sub_num);
	if (evt <= 0)
		name[0] = 0;
	
	CustomCreateDialogBox(IDD_LIST, hDlg, QualifierProc);
#if 0
	int item = genericpopupmenu (hDlg, names, mflags, MAX_INPUT_QUALIFIERS * 2);
	if (item >= 0)
		flags ^= IDEV_MAPPED_QUALIFIER1 << item;

	inputdevice_set_mapping (input_selected_device, input_selected_widget,
		name, custom, flags, -1, input_selected_sub_num);
#endif
}

static void input_invert (void)
{
	int evt;
	uae_u64 flags;
	TCHAR name[256];
	TCHAR custom[MAX_DPATH];

	if (input_selected_device < 0 || input_selected_widget < 0)
		return;
	evt = inputdevice_get_mapping (input_selected_device, input_selected_widget,
		&flags, NULL, name, custom, input_selected_sub_num);
	if (evt <= 0)
		return;
	flags ^= IDEV_MAPPED_INVERT;
	inputdevice_set_mapping (input_selected_device, input_selected_widget,
		name, custom, flags, -1, input_selected_sub_num);
}

static void input_toggletoggle (void)
{
	int evt;
	uae_u64 flags;
	TCHAR name[256];
	TCHAR custom[MAX_DPATH];

	if (input_selected_device < 0 || input_selected_widget < 0)
		return;
	evt = inputdevice_get_mapping (input_selected_device, input_selected_widget,
		&flags, NULL, name, custom, input_selected_sub_num);
	if (evt <= 0)
		return;
	flags ^= IDEV_MAPPED_TOGGLE;
	inputdevice_set_mapping (input_selected_device, input_selected_widget,
		name, custom, flags, -1, input_selected_sub_num);
}

static void input_copy (HWND hDlg)
{
	int dst = workprefs.input_selected_setting;
	int src = xSendDlgItemMessage (hDlg, IDC_INPUTCOPYFROM, CB_GETCURSEL, 0, 0L);
	if (src == CB_ERR)
		return;
	input_copy_from = src;
	inputdevice_copy_single_config (&workprefs, (int)src, workprefs.input_selected_setting, input_selected_device, -1);
	init_inputdlg (hDlg);
}

static void input_restoredefault (void)
{
	inputdevice_copy_single_config (&workprefs, GAMEPORT_INPUT_SETTINGS, workprefs.input_selected_setting, input_selected_device, input_selected_widget);
}

static INT_PTR CALLBACK InputDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	TCHAR name_buf[MAX_DPATH] = _T(""), desc_buf[128] = _T("");
	TCHAR *posn = NULL;
	HWND list;
	int dblclick = 0;
	NM_LISTVIEW *nmlistview;
	int items = 0, entry = 0;
	static int recursive;

	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch (msg)
	{
	case WM_INITDIALOG:
		recursive++;
		pages[INPUT_ID] = hDlg;
		currentpage = INPUT_ID;
		inputdevice_updateconfig (NULL, &workprefs);
		inputdevice_config_change ();
		input_selected_widget = -1;
		init_inputdlg (hDlg);
		recursive--;

	case WM_USER:
		recursive++;
		enable_for_inputdlg (hDlg);
		values_to_inputdlg (hDlg);
		recursive--;
		return TRUE;
	case WM_DESTROY:
		input_find (hDlg, guiDlg, 0, false, false);
		break;
	case WM_COMMAND:
		if (recursive)
			break;
		recursive++;
		switch (wParam)
		{
		case IDC_INPUTREMAP:
			input_selected_event = -1;
			input_find (hDlg, guiDlg, 0, true, false);
			break;
		case IDC_INPUTTEST:
			input_find (hDlg, guiDlg, 1, true, false);
			break;
		case IDC_INPUTCOPY:
			input_copy (hDlg);
			break;
		case IDC_INPUTSWAP:
			input_swap (hDlg);
			break;
		case IDC_INPUTDEVICEDISABLE:
			inputdevice_set_device_status (input_selected_device, ischecked (hDlg, IDC_INPUTDEVICEDISABLE));
			break;
		case IDC_KEYBOARD_SWAPHACK:
			{
				int v = IsDlgButtonChecked(hDlg, IDC_KEYBOARD_SWAPHACK);
				key_swap_hack = v == BST_INDETERMINATE ? 2 : v > 0;
				key_swap_hack++;
				if (key_swap_hack > 2) {
					key_swap_hack = 0;
				}
				regsetint(NULL, _T("KeySwapBackslashF11"), key_swap_hack);
				values_to_inputdlg(hDlg);
			}
			break;
		default:
			switch (LOWORD (wParam))
			{
			case IDC_INPUTDEADZONE:
			case IDC_INPUTAUTOFIRERATE:
			case IDC_INPUTSPEEDD:
			case IDC_INPUTSPEEDA:
				values_from_inputdlgbottom (hDlg);
				break;
			}
			if (HIWORD (wParam) == CBN_SELCHANGE || HIWORD (wParam) == CBN_KILLFOCUS)  {
				switch (LOWORD (wParam))
				{
				case IDC_INPUTAMIGA:
					values_from_inputdlg (hDlg, 1);
					break;
				case IDC_INPUTAMIGACNT:
				case IDC_INPUTTYPE:
				case IDC_INPUTDEVICE:
					values_from_inputdlg (hDlg, 0);
					break;
				}
			}
			break;
		}
		enable_for_portsdlg (hDlg);
		inputdevice_config_change ();
		recursive--;
		break;
	case WM_NOTIFY:
		if (((LPNMHDR) lParam)->idFrom == IDC_INPUTLIST)
		{
			int column;
			nmlistview = (NM_LISTVIEW *) lParam;
			list = nmlistview->hdr.hwndFrom;
			switch (nmlistview->hdr.code)
			{
			case NM_RCLICK:
			case NM_RDBLCLK:
				input_selected_widget = -1;
				ListView_SetItemState (list, -1, 0, LVIS_SELECTED);
				update_listview_input (hDlg);
				init_inputdlg_2 (hDlg);
				break;
			case NM_DBLCLK:
				dblclick = 1;
				/* fall-through */
			case NM_CLICK:
				entry = listview_entry_from_click (list, &column, true);
				if (entry >= 0) {
					int oldentry = input_selected_widget;
					input_selected_widget = entry;
					if (column == 0 && entry == oldentry && dblclick) {
						input_restoredefault ();
					} else if (column == 1 && entry == oldentry) {
						input_togglesetmode ();
					} else if (column == 2 && entry == oldentry) {
						input_toggleautofire ();
					} else if (column == 3 && entry == oldentry) {
						input_toggletoggle ();
					} else if (column == 4 && entry == oldentry) {
						input_invert ();
					} else if (column == 5 && entry == oldentry) {
						input_qualifiers (hDlg);
					} else if (column == 6) {
						input_selected_sub_num++;
						if (input_selected_sub_num >= MAX_INPUT_SUB_EVENT)
							input_selected_sub_num = 0;
						xSendDlgItemMessage (hDlg, IDC_INPUTAMIGACNT, CB_SETCURSEL, input_selected_sub_num, 0);
					}
				} else {
					input_selected_widget = -1;
				}
				if (dblclick && column == 1)
					doinputcustom (hDlg, 0);
				update_listview_input (hDlg);
				init_inputdlg_2 (hDlg);
				break;
			case LVN_COLUMNCLICK:
				ColumnClickListView(hDlg, nmlistview);
				break;
			}
		}
	}
	handlerawinput (hDlg, msg, wParam, lParam);
	return FALSE;
}

#ifdef GFXFILTER

static int scanlineratios[] = { 1,1,1,2,1,3, 2,1,2,2,2,3, 3,1,3,2,3,3, 0,0 };
static int scanlineindexes[100];
static int filterpreset_selected = -1, filterpreset_builtin = -1;
static int filter_nativertg;

static void enable_for_hw3ddlg (HWND hDlg)
{
	struct gfx_filterdata *gf = &workprefs.gf[filter_nativertg];
	int v = gf->gfx_filter ? TRUE : FALSE;
	int scalemode = workprefs.gf[filter_nativertg].gfx_filter_autoscale;
	int vv = FALSE, vv2 = FALSE, vv3 = FALSE, vv4 = FALSE;
	int as = FALSE;

	v = vv = vv2 = vv3 = vv4 = TRUE;
	if (filter_nativertg == 1) {
		vv4 = FALSE;
	}
	if (scalemode == AUTOSCALE_STATIC_AUTO || scalemode == AUTOSCALE_STATIC_NOMINAL || scalemode == AUTOSCALE_STATIC_MAX)
		as = TRUE;

	if (filter_nativertg == 2) {
		ew(hDlg, IDC_FILTERENABLE, TRUE);
		setchecked(hDlg, IDC_FILTERENABLE, gf->enable);
	} else {
		ew(hDlg, IDC_FILTERENABLE, FALSE);
		setchecked(hDlg, IDC_FILTERENABLE, TRUE);
	}

	ew(hDlg, IDC_FILTERHZMULT, v && !as);
	ew(hDlg, IDC_FILTERVZMULT, v && !as);
	ew(hDlg, IDC_FILTERHZ, v && vv4);
	ew(hDlg, IDC_FILTERVZ, v && vv4);
	ew(hDlg, IDC_FILTERHO, v && vv4);
	ew(hDlg, IDC_FILTERVO, v && vv4);
	ew(hDlg, IDC_FILTERSLR, vv3);
	ew(hDlg, IDC_FILTERXL, vv2);
	ew(hDlg, IDC_FILTERXLV, vv2);
	ew(hDlg, IDC_FILTERXTRA, vv2);
	ew(hDlg, IDC_FILTERFILTERH, TRUE);
	ew(hDlg, IDC_FILTERFILTERV, TRUE);
	ew(hDlg, IDC_FILTERSTACK, workprefs.gfx_api);
	ew(hDlg, IDC_FILTERKEEPASPECT, v && scalemode != AUTOSCALE_STATIC_AUTO);
	ew(hDlg, IDC_FILTERASPECT, v && scalemode != AUTOSCALE_STATIC_AUTO);
	ew(hDlg, IDC_FILTERASPECT2, v && workprefs.gf[filter_nativertg].gfx_filter_keep_aspect && scalemode != AUTOSCALE_STATIC_AUTO);
	ew(hDlg, IDC_FILTERKEEPAUTOSCALEASPECT, scalemode == AUTOSCALE_NORMAL || scalemode == AUTOSCALE_INTEGER_AUTOSCALE);
	ew(hDlg, IDC_FILTEROVERLAY, workprefs.gfx_api);
	ew(hDlg, IDC_FILTEROVERLAYTYPE, workprefs.gfx_api);

	ew(hDlg, IDC_FILTERPRESETSAVE, filterpreset_builtin < 0);
	ew(hDlg, IDC_FILTERPRESETLOAD, filterpreset_selected > 0);
	ew(hDlg, IDC_FILTERPRESETDELETE, filterpreset_selected > 0 && filterpreset_builtin < 0);

	ew(hDlg, IDC_FILTERINTEGER, scalemode == AUTOSCALE_INTEGER || scalemode == AUTOSCALE_INTEGER_AUTOSCALE);
}

static const TCHAR *filtermultnames[] = {
	_T("FS"), _T("1/4x"), _T("1/2x"), _T("1x"), _T("1.5x"), _T("2x"), _T("2.5x"), _T("3x"), _T("3.5x"), _T("4x"), _T("6x"), _T("8x"), NULL
};
static const float filtermults[] = { 0, 0.25f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 6.0f, 8.0f };
struct filterxtra {
	const TCHAR *label;
	int *varw[3], *varc[3];
	int min, max, step;
};
static struct filterxtra *filter_extra[4], *filter_selected;
static int filter_selected_num;

static struct filterxtra filter_pal_extra[] =
{
	_T("Brightness"),
		&workprefs.gf[0].gfx_filter_luminance, NULL, &workprefs.gf[2].gfx_filter_luminance,
		&currprefs.gf[0].gfx_filter_luminance, NULL, &currprefs.gf[2].gfx_filter_luminance,
		-1000, 1000, 10,
	_T("Contrast"),
		&workprefs.gf[0].gfx_filter_contrast, NULL, &workprefs.gf[2].gfx_filter_contrast,
		&currprefs.gf[0].gfx_filter_contrast, NULL, &currprefs.gf[2].gfx_filter_contrast,
		-1000, 1000, 10,
	_T("Saturation"),
		&workprefs.gf[0].gfx_filter_saturation, NULL, &workprefs.gf[2].gfx_filter_saturation,
		&currprefs.gf[0].gfx_filter_saturation, NULL, &currprefs.gf[2].gfx_filter_saturation,
		-1000, 1000, 10,
	_T("Gamma"),
		&workprefs.gfx_gamma, NULL, &workprefs.gfx_gamma,
		&currprefs.gfx_gamma, NULL, &currprefs.gfx_gamma,
		-1000, 1000, 10,
	_T("Blurriness"),
		&workprefs.gf[0].gfx_filter_blur, NULL, &workprefs.gf[2].gfx_filter_blur,
		&currprefs.gf[0].gfx_filter_blur, NULL, &currprefs.gf[2].gfx_filter_blur,
		0, 2000, 10,
	_T("Noise"),
		&workprefs.gf[0].gfx_filter_noise, NULL, &workprefs.gf[2].gfx_filter_noise,
		&currprefs.gf[0].gfx_filter_noise, NULL, &currprefs.gf[2].gfx_filter_noise,
		0, 100, 10,
	NULL
};
static struct filterxtra filter_3d_extra[] =
{
	_T("Point/Bilinear"),
		&workprefs.gf[0].gfx_filter_bilinear, &workprefs.gf[1].gfx_filter_bilinear, &workprefs.gf[2].gfx_filter_bilinear,
		&currprefs.gf[0].gfx_filter_bilinear, &currprefs.gf[1].gfx_filter_bilinear, &currprefs.gf[2].gfx_filter_bilinear,
		0, 1, 1,
	_T("Scanline opacity"),
		&workprefs.gf[0].gfx_filter_scanlines, &workprefs.gf[1].gfx_filter_scanlines, &workprefs.gf[2].gfx_filter_scanlines,
		&currprefs.gf[0].gfx_filter_scanlines, &currprefs.gf[1].gfx_filter_scanlines, &currprefs.gf[2].gfx_filter_scanlines,
		0, 100, 10,
	_T("Scanline level"),
		&workprefs.gf[0].gfx_filter_scanlinelevel, &workprefs.gf[1].gfx_filter_scanlinelevel, &workprefs.gf[2].gfx_filter_scanlinelevel,
		&currprefs.gf[0].gfx_filter_scanlinelevel, &currprefs.gf[1].gfx_filter_scanlinelevel, &currprefs.gf[2].gfx_filter_scanlinelevel,
		0, 100, 10,
	_T("Scanline offset"),
		&workprefs.gf[0].gfx_filter_scanlineoffset, &workprefs.gf[1].gfx_filter_scanlineoffset, &workprefs.gf[2].gfx_filter_scanlineoffset,
		&currprefs.gf[0].gfx_filter_scanlineoffset, &currprefs.gf[1].gfx_filter_scanlineoffset, &currprefs.gf[2].gfx_filter_scanlineoffset,
		0, 3, 1,
	NULL
};
static int dummy_in, dummy_out;
static const int filtertypes[] = {
	0, 0,
	1, 1,
	1, 1,
	1, 1,
	0, 0, 0,
	0, 0, 0,
	0, 0,
	0, 0, 0,
	0, 0, 0,
	0, 0,
	0, 0,
	0,
	0,
	0, 0, 0, 0,
	-1
};	
static void *filtervars_wp[] = {
	&workprefs.gf[0].gfx_filter, &workprefs.gf[0].gfx_filter_filtermodeh,
	&workprefs.gf[0].gfx_filter_vert_zoom, &workprefs.gf[0].gfx_filter_horiz_zoom,
	&workprefs.gf[0].gfx_filter_vert_zoom_mult, &workprefs.gf[0].gfx_filter_horiz_zoom_mult,
	&workprefs.gf[0].gfx_filter_vert_offset, &workprefs.gf[0].gfx_filter_horiz_offset,
	&workprefs.gf[0].gfx_filter_scanlines, &workprefs.gf[0].gfx_filter_scanlinelevel, &workprefs.gf[0].gfx_filter_scanlineratio,
	&workprefs.gfx_resolution, &workprefs.gfx_vresolution, &workprefs.gfx_iscanlines,
	&workprefs.gfx_xcenter, &workprefs.gfx_ycenter,
	&workprefs.gf[0].gfx_filter_luminance, &workprefs.gf[0].gfx_filter_contrast, &workprefs.gf[0].gfx_filter_saturation,
	&workprefs.gf[0].gfx_filter_gamma, &workprefs.gf[0].gfx_filter_blur, &workprefs.gf[0].gfx_filter_noise,
	&workprefs.gf[0].gfx_filter_keep_aspect, &workprefs.gf[0].gfx_filter_aspect,
	&workprefs.gf[0].gfx_filter_autoscale, &workprefs.gf[0].gfx_filter_bilinear,
	&workprefs.gf[0].gfx_filter_keep_autoscale_aspect,
	&workprefs.gf[0].gfx_filter_integerscalelimit,
	&workprefs.gf[0].gfx_filter_left_border, &workprefs.gf[0].gfx_filter_right_border, &workprefs.gf[0].gfx_filter_top_border, &workprefs.gf[0].gfx_filter_bottom_border,
	NULL
};
static void *filtervars_cp[] = {
	NULL, &currprefs.gf[0].gfx_filter_filtermodeh,
	&currprefs.gf[0].gfx_filter_vert_zoom, &currprefs.gf[0].gfx_filter_horiz_zoom,
	&currprefs.gf[0].gfx_filter_vert_zoom_mult, &currprefs.gf[0].gfx_filter_horiz_zoom_mult,
	&currprefs.gf[0].gfx_filter_vert_offset, &currprefs.gf[0].gfx_filter_horiz_offset,
	&currprefs.gf[0].gfx_filter_scanlines, &currprefs.gf[0].gfx_filter_scanlinelevel, &currprefs.gf[0].gfx_filter_scanlineratio,
	&currprefs.gfx_resolution, &currprefs.gfx_vresolution, &currprefs.gfx_iscanlines,
	&currprefs.gfx_xcenter, &currprefs.gfx_ycenter,
	&currprefs.gf[0].gfx_filter_luminance, &currprefs.gf[0].gfx_filter_contrast, &currprefs.gf[0].gfx_filter_saturation,
	&currprefs.gf[0].gfx_filter_gamma, &currprefs.gf[0].gfx_filter_blur, &currprefs.gf[0].gfx_filter_noise,
	&currprefs.gf[0].gfx_filter_keep_aspect, &currprefs.gf[0].gfx_filter_aspect,
	&currprefs.gf[0].gfx_filter_autoscale, &currprefs.gf[0].gfx_filter_bilinear,
	&currprefs.gf[0].gfx_filter_keep_autoscale_aspect,
	&currprefs.gf[0].gfx_filter_integerscalelimit,
	&currprefs.gf[0].gfx_filter_left_border, &currprefs.gf[0].gfx_filter_right_border, &currprefs.gf[0].gfx_filter_top_border, &currprefs.gf[0].gfx_filter_bottom_border,
	NULL
};

struct filterpreset {
	const TCHAR *name;
	int conf[27];
};
static const struct filterpreset filterpresets[] =
{
	{ _T("D3D Autoscale"),		0,	0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 1, 1, 0, 0, 0,  0, 0, 0, 0,   0,  0, 0, -1, 4, 0, 0 },
	{ _T("D3D Full Scaling"),	0,	0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 1, 1, 0, 0, 0,  0, 0, 0, 0,   0,  0, 0, -1, 0, 0, 0 },
	{ NULL }
};

static float getfiltermult (HWND hDlg, DWORD dlg)
{
	TCHAR tmp[100];
	LRESULT v = xSendDlgItemMessage (hDlg, dlg, CB_GETCURSEL, 0, 0L);
	float f;

	if (v != CB_ERR)
		return filtermults[v];
	tmp[0] = 0;
	xSendDlgItemMessage (hDlg, dlg, WM_GETTEXT, (WPARAM)sizeof tmp / sizeof (TCHAR), (LPARAM)tmp);
	if (!_tcsicmp (tmp, _T("FS")))
		return 0.0f;
	f = (float)_tstof (tmp);
	if (f < 0.0f)
		f = 0.0f;
	if (f > 9.9f)
		f = 9.9f;
	return f;
}

static void setfiltermult2 (HWND hDlg, int id, float val)
{
	int i, got;

	got = 0;
	for (i = 0; filtermultnames[i]; i++) {
		if (filtermults[i] == val) {
			xSendDlgItemMessage (hDlg, id, CB_SETCURSEL, i, 0);
			got = 1;
		}
	}
	if (!got) {
		TCHAR tmp[100];
		tmp[0] = 0;
		if (val > 0)
			_stprintf (tmp, _T("%.2f"), val);
		xSendDlgItemMessage (hDlg, id, CB_SETCURSEL, 0, 0);
		SetDlgItemText (hDlg, id, tmp);
	}
}

static void setfiltermult (HWND hDlg)
{
	setfiltermult2 (hDlg, IDC_FILTERHZMULT, workprefs.gf[filter_nativertg].gfx_filter_horiz_zoom_mult);
	setfiltermult2 (hDlg, IDC_FILTERVZMULT, workprefs.gf[filter_nativertg].gfx_filter_vert_zoom_mult);
}

static void values_to_hw3ddlg (HWND hDlg, bool initdialog)
{
	TCHAR txt[200], tmp[200];
	int i, j, fltnum;
	int fxidx, fxcnt;
	UAEREG *fkey;

	xSendDlgItemMessage (hDlg, IDC_FILTERASPECT, CB_SETCURSEL,
		(workprefs.gf[filter_nativertg].gfx_filter_aspect == 0) ? 0 :
		(workprefs.gf[filter_nativertg].gfx_filter_aspect < 0) ? 1 :
		getaspectratioindex (workprefs.gf[filter_nativertg].gfx_filter_aspect) + 2, 0);

	CheckDlgButton (hDlg, IDC_FILTERKEEPASPECT, workprefs.gf[filter_nativertg].gfx_filter_keep_aspect);
	CheckDlgButton (hDlg, IDC_FILTERKEEPAUTOSCALEASPECT, workprefs.gf[filter_nativertg].gfx_filter_keep_autoscale_aspect != 0);

	xSendDlgItemMessage (hDlg, IDC_FILTERASPECT2, CB_SETCURSEL,
		workprefs.gf[filter_nativertg].gfx_filter_keep_aspect, 0);

	xSendDlgItemMessage (hDlg, IDC_FILTERINTEGER, CB_RESETCONTENT, 0, 0L);
	xSendDlgItemMessage (hDlg, IDC_FILTERINTEGER, CB_ADDSTRING, 0, (LPARAM)_T("1/1"));
	xSendDlgItemMessage (hDlg, IDC_FILTERINTEGER, CB_ADDSTRING, 0, (LPARAM)_T("1/2"));
	xSendDlgItemMessage (hDlg, IDC_FILTERINTEGER, CB_ADDSTRING, 0, (LPARAM)_T("1/4"));
	xSendDlgItemMessage (hDlg, IDC_FILTERINTEGER, CB_ADDSTRING, 0, (LPARAM)_T("1/8"));
	
	xSendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_RESETCONTENT, 0, 0L);
	WIN32GUI_LoadUIString (IDS_AUTOSCALE_DISABLED, txt, sizeof (txt) / sizeof (TCHAR));
	xSendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_ADDSTRING, 0, (LPARAM)txt);
	if (filter_nativertg != 1) {
		WIN32GUI_LoadUIString (IDS_AUTOSCALE_DEFAULT, txt, sizeof (txt) / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_ADDSTRING, 0, (LPARAM)txt);
		WIN32GUI_LoadUIString (IDS_AUTOSCALE_TV, txt, sizeof (txt) / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_ADDSTRING, 0, (LPARAM)txt);
		WIN32GUI_LoadUIString (IDS_AUTOSCALE_MAX, txt, sizeof (txt) / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_ADDSTRING, 0, (LPARAM)txt);
		WIN32GUI_LoadUIString (IDS_AUTOSCALE_SCALING, txt, sizeof (txt) / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_ADDSTRING, 0, (LPARAM)txt);
		WIN32GUI_LoadUIString (IDS_AUTOSCALE_RESIZE, txt, sizeof (txt) / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_ADDSTRING, 0, (LPARAM)txt);
		WIN32GUI_LoadUIString (IDS_AUTOSCALE_CENTER, txt, sizeof (txt) / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_ADDSTRING, 0, (LPARAM)txt);
		WIN32GUI_LoadUIString (IDS_AUTOSCALE_MANUAL, txt, sizeof (txt) / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_ADDSTRING, 0, (LPARAM)txt);
		WIN32GUI_LoadUIString (IDS_AUTOSCALE_INTEGER, txt, sizeof (txt) / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_ADDSTRING, 0, (LPARAM)txt);
		WIN32GUI_LoadUIString (IDS_AUTOSCALE_INTEGER_AUTOSCALE, txt, sizeof (txt) / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_ADDSTRING, 0, (LPARAM)txt);

		xSendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_ADDSTRING, 0, (LPARAM)_T("-"));
		WIN32GUI_LoadUIString (IDS_AUTOSCALE_OVERSCAN_BLANK, txt, sizeof (txt) / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_ADDSTRING, 0, (LPARAM)txt);
	} else {
		WIN32GUI_LoadUIString(IDS_AUTOSCALE_DEFAULT, txt, sizeof (txt) / sizeof (TCHAR));
		xSendDlgItemMessage(hDlg, IDC_FILTERAUTOSCALE, CB_ADDSTRING, 0, (LPARAM)txt);
		WIN32GUI_LoadUIString(IDS_AUTOSCALE_CENTER, txt, sizeof (txt) / sizeof (TCHAR));
		xSendDlgItemMessage(hDlg, IDC_FILTERAUTOSCALE, CB_ADDSTRING, 0, (LPARAM)txt);
		WIN32GUI_LoadUIString(IDS_AUTOSCALE_INTEGER, txt, sizeof (txt) / sizeof (TCHAR));
		xSendDlgItemMessage(hDlg, IDC_FILTERAUTOSCALE, CB_ADDSTRING, 0, (LPARAM)txt);
	}
	xSendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_SETCURSEL, workprefs.gf[filter_nativertg].gfx_filter_autoscale, 0);
	xSendDlgItemMessage (hDlg, IDC_FILTERINTEGER, CB_SETCURSEL, workprefs.gf[filter_nativertg].gfx_filter_integerscalelimit, 0);

	xSendDlgItemMessage (hDlg, IDC_FILTERSTACK, CB_RESETCONTENT, 0, 0);
	for (i = -MAX_FILTERSHADERS; i < MAX_FILTERSHADERS; i++) {
		j = i < 0 ? i : i + 1;
		if (i == 0) {
			_stprintf (tmp, _T("%d%s"), 0, workprefs.gf[filter_nativertg].gfx_filtershader[2 * MAX_FILTERSHADERS][0] ? _T(" *") : _T(""));
			xSendDlgItemMessage (hDlg, IDC_FILTERSTACK, CB_ADDSTRING, 0, (LPARAM)tmp);
		}
		_stprintf (tmp, _T("%d%s"), j, workprefs.gf[filter_nativertg].gfx_filtershader[i + 4][0] ? _T(" *") : _T(""));
		xSendDlgItemMessage (hDlg, IDC_FILTERSTACK, CB_ADDSTRING, 0, (LPARAM)tmp);
	}

	i = filterstackpos;
	if (i == 2 * MAX_FILTERSHADERS)
		i = MAX_FILTERSHADERS;
	else if (i >= MAX_FILTERSHADERS)
		i++;
	xSendDlgItemMessage (hDlg, IDC_FILTERSTACK, CB_SETCURSEL, i, 0);

	int xrange1, xrange2;
	int yrange1, yrange2;
	
	if (workprefs.gf[filter_nativertg].gfx_filter_autoscale == AUTOSCALE_MANUAL) {
		xrange1 = -1;
		xrange2 = 1900;
		yrange1 = xrange1;
		yrange2 = xrange2;
	} else if (workprefs.gf[filter_nativertg].gfx_filter_autoscale == AUTOSCALE_OVERSCAN_BLANK) {
		xrange1 = 0;
		xrange2 = 1900;
		yrange1 = 0;
		yrange2 = 700;
	} else if (workprefs.gf[filter_nativertg].gfx_filter_autoscale == AUTOSCALE_INTEGER ||
			   workprefs.gf[filter_nativertg].gfx_filter_autoscale == AUTOSCALE_INTEGER_AUTOSCALE) {
		xrange1 = -99;
		xrange2 = 99;
		yrange1 = xrange1;
		yrange2 = xrange2;
	} else {
		xrange1 = -9999;
		xrange2 = 9999;
		yrange1 = xrange1;
		yrange2 = xrange2;
	}

	xSendDlgItemMessage (hDlg, IDC_FILTERHZ, TBM_SETRANGE, TRUE, MAKELONG (xrange1, xrange2));
	xSendDlgItemMessage (hDlg, IDC_FILTERHZ, TBM_SETPAGESIZE, 0, 1);
	xSendDlgItemMessage (hDlg, IDC_FILTERVZ, TBM_SETRANGE, TRUE, MAKELONG (xrange1, xrange2));
	xSendDlgItemMessage (hDlg, IDC_FILTERVZ, TBM_SETPAGESIZE, 0, 1);
	xSendDlgItemMessage (hDlg, IDC_FILTERHO, TBM_SETRANGE, TRUE, MAKELONG (yrange1, yrange2));
	xSendDlgItemMessage (hDlg, IDC_FILTERHO, TBM_SETPAGESIZE, 0, 1);
	xSendDlgItemMessage (hDlg, IDC_FILTERVO, TBM_SETRANGE, TRUE, MAKELONG (yrange1, yrange2));
	xSendDlgItemMessage (hDlg, IDC_FILTERVO, TBM_SETPAGESIZE, 0, 1);

	int v = 0;
	if (filter_nativertg == 0) {
		v = 0;
	} else if (filter_nativertg == 1) {
		v = 2;
	} else {
		v = 1;
	}
	xSendDlgItemMessage (hDlg, IDC_FILTER_NATIVERTG, CB_SETCURSEL, v, 0);

	xSendDlgItemMessage (hDlg, IDC_FILTERMODE, CB_RESETCONTENT, 0, 0L);
	WIN32GUI_LoadUIString (IDS_NONE, tmp, sizeof(tmp) / sizeof(TCHAR));

	fltnum = 0;
	xSendDlgItemMessage(hDlg, IDC_FILTERMODE, CB_ADDSTRING, 0, (LPARAM)tmp);
	j = 1;
	if (workprefs.gfx_api && D3D_canshaders ()) {
		bool gotit = false;
		HANDLE h;
		TCHAR tmp[MAX_DPATH];
		for (int fx = 0; fx < 2; fx++) {
			get_plugin_path (tmp, sizeof tmp / sizeof (TCHAR), _T("filtershaders\\direct3d"));
			if (fx) {
				_tcscat (tmp, _T("*.fx"));
			} else {
				_tcscat(tmp, _T("*.hlsl"));
			}
			WIN32_FIND_DATA wfd;
			h = FindFirstFile (tmp, &wfd);
			while (h != INVALID_HANDLE_VALUE) {
				if (wfd.cFileName[0] != '_') {
					TCHAR tmp2[MAX_DPATH];
					_stprintf (tmp2, _T("D3D: %s"), wfd.cFileName);
#if 0
					if (!fx) {
						tmp2[_tcslen(tmp2) - 5] = 0;
					} else {
						tmp2[_tcslen(tmp2) - 3] = 0;
					}
#endif
					xSendDlgItemMessage (hDlg, IDC_FILTERMODE, CB_ADDSTRING, 0, (LPARAM)tmp2);
					if (workprefs.gfx_api && !_tcscmp (workprefs.gf[filter_nativertg].gfx_filtershader[filterstackpos], wfd.cFileName)) {
						fltnum = j;
						gotit = true;
					}
					j++;
				}
				if (!FindNextFile (h, &wfd)) {
					FindClose (h);
					h = INVALID_HANDLE_VALUE;
				}
			}
		}
	}
	if (workprefs.gfx_api) {
		static int old_overlaytype = -1;
		int overlaytype = xSendDlgItemMessage(hDlg, IDC_FILTEROVERLAYTYPE, CB_GETCURSEL, 0, 0L);
		if (overlaytype != old_overlaytype || initdialog) {
			WIN32GUI_LoadUIString(IDS_NONE, tmp, MAX_DPATH);
			xSendDlgItemMessage(hDlg, IDC_FILTEROVERLAY, CB_RESETCONTENT, 0, 0L);
			xSendDlgItemMessage(hDlg, IDC_FILTEROVERLAY, CB_ADDSTRING, 0, (LPARAM)tmp);
			xSendDlgItemMessage(hDlg, IDC_FILTEROVERLAY, CB_SETCURSEL, 0, 0);
			HANDLE h;
			WIN32_FIND_DATA wfd;
			TCHAR tmp[MAX_DPATH];
			get_plugin_path(tmp, sizeof tmp / sizeof(TCHAR), overlaytype == 0 ? _T("overlays") : _T("masks"));
			_tcscat(tmp, _T("*.*"));
			h = FindFirstFile(tmp, &wfd);
			i = 0; j = 1;
			while (h != INVALID_HANDLE_VALUE) {
				TCHAR *fname = wfd.cFileName;
				TCHAR *ext = _tcsrchr(fname, '.');
				if (ext && (
					!_tcsicmp(ext, _T(".png")) ||
					(!_tcsicmp(ext, _T(".bmp")) && workprefs.gfx_api < 2)))
				{
					for (;;) {
						if (!overlaytype && _tcslen(fname) > 4 + 1 + 3 && !_tcsnicmp(fname + _tcslen(fname) - (4 + 1 + 3), _T("_led"), 4))
							break;
						xSendDlgItemMessage(hDlg, IDC_FILTEROVERLAY, CB_ADDSTRING, 0, (LPARAM)fname);
						if (!_tcsicmp(wfd.cFileName, overlaytype == 0 ? workprefs.gf[filter_nativertg].gfx_filteroverlay : workprefs.gf[filter_nativertg].gfx_filtermask[filterstackpos]))
							xSendDlgItemMessage(hDlg, IDC_FILTEROVERLAY, CB_SETCURSEL, j, 0);
						j++;
						break;
					}

				}
				if (!FindNextFile(h, &wfd)) {
					FindClose(h);
					h = INVALID_HANDLE_VALUE;
				}
			}
			if (D3D_goodenough() < 1 && overlaytype) {
				xSendDlgItemMessage(hDlg, IDC_FILTEROVERLAY, CB_SETCURSEL, 0, 0);
				ew(hDlg, IDC_FILTEROVERLAY, FALSE);
			} else {
				ew(hDlg, IDC_FILTEROVERLAY, TRUE);
			}
			old_overlaytype = overlaytype;
		}
	} else {
		xSendDlgItemMessage(hDlg, IDC_FILTEROVERLAY, CB_RESETCONTENT, 0, 0L);
		WIN32GUI_LoadUIString (IDS_FILTER_NOOVERLAYS, tmp, MAX_DPATH);
		xSendDlgItemMessage (hDlg, IDC_FILTEROVERLAY, CB_ADDSTRING, 0, (LPARAM)tmp);
		xSendDlgItemMessage (hDlg, IDC_FILTEROVERLAY, CB_SETCURSEL, 0, 0);
		ew(hDlg, IDC_FILTEROVERLAY, TRUE);
	}
	xSendDlgItemMessage (hDlg, IDC_FILTERMODE, CB_SETCURSEL, fltnum, 0);

	fxidx = 0;
	filter_extra[fxidx] = NULL;
	if (workprefs.gfx_api) {
		filter_extra[fxidx++] = filter_3d_extra;
		filter_extra[fxidx] = NULL;
	}
	int filtermodenum = 0;
	xSendDlgItemMessage(hDlg, IDC_FILTERFILTERH, CB_RESETCONTENT, 0, 0L);
	xSendDlgItemMessage(hDlg, IDC_FILTERFILTERV, CB_RESETCONTENT, 0, 0L);
	xSendDlgItemMessage(hDlg, IDC_FILTERFILTERV, CB_ADDSTRING, 0, (LPARAM)_T("-"));
	if (workprefs.gfx_api) {
		for (i = 0; i < 4; i++) {
			TCHAR tmp[100];
			_stprintf (tmp, _T("%dx"), i + 1);
			xSendDlgItemMessage(hDlg, IDC_FILTERFILTERH, CB_ADDSTRING, 0, (LPARAM)tmp);
			xSendDlgItemMessage(hDlg, IDC_FILTERFILTERV, CB_ADDSTRING, 0, (LPARAM)tmp);
			filtermodenum++;
		}
	}
	xSendDlgItemMessage (hDlg, IDC_FILTERXL, TBM_SETRANGE, TRUE, MAKELONG (   0, +1000));
	xSendDlgItemMessage (hDlg, IDC_FILTERXL, TBM_SETPAGESIZE, 0, 1);
	if (filter_extra[0]) {
		struct filterxtra *prev = filter_selected;
		int idx2 = -1;
		int idx = xSendDlgItemMessage (hDlg, IDC_FILTERXTRA, CB_GETCURSEL, 0, 0L);
		if (idx == CB_ERR)
			idx = -1;
		fxcnt = 0;
		filter_selected = &filter_extra[0][0];
		xSendDlgItemMessage (hDlg, IDC_FILTERXTRA, CB_RESETCONTENT, 0, 0L);
		for (j = 0; filter_extra[j]; j++) {
			struct filterxtra *fx = filter_extra[j];
			for (i = 0; fx[i].label; i++) {
				if (prev == &fx[i] && idx < 0) {
					idx2 = i;
					filter_selected = &fx[idx2];
					filter_selected_num = fxcnt;
				}
				xSendDlgItemMessage (hDlg, IDC_FILTERXTRA, CB_ADDSTRING, 0, (LPARAM)fx[i].label);
				if (idx == 0) {
					filter_selected = &fx[i];
					filter_selected_num = fxcnt;
					prev = NULL;
				}
				fxcnt++;
				idx--;
			}
		}
		xSendDlgItemMessage (hDlg, IDC_FILTERXTRA, CB_SETCURSEL, filter_selected_num, 0);
		xSendDlgItemMessage (hDlg, IDC_FILTERXL, TBM_SETRANGE, TRUE, MAKELONG (filter_selected->min, filter_selected->max));
		xSendDlgItemMessage (hDlg, IDC_FILTERXL, TBM_SETPAGESIZE, 0, filter_selected->step);
		if (filter_selected->varw[filter_nativertg]) {
			xSendDlgItemMessage (hDlg, IDC_FILTERXL, TBM_SETPOS, TRUE, *(filter_selected->varw[filter_nativertg]));
			SetDlgItemInt (hDlg, IDC_FILTERXLV, *(filter_selected->varw[filter_nativertg]), TRUE);
		}
	}
	if (workprefs.gf[filter_nativertg].gfx_filter_filtermodeh >= filtermodenum)
		workprefs.gf[filter_nativertg].gfx_filter_filtermodeh = 0;
	if (workprefs.gf[filter_nativertg].gfx_filter_filtermodev >= filtermodenum + 1)
		workprefs.gf[filter_nativertg].gfx_filter_filtermodev = 0;
	xSendDlgItemMessage(hDlg, IDC_FILTERFILTERH, CB_SETCURSEL, workprefs.gf[filter_nativertg].gfx_filter_filtermodeh, 0);
	xSendDlgItemMessage(hDlg, IDC_FILTERFILTERV, CB_SETCURSEL, workprefs.gf[filter_nativertg].gfx_filter_filtermodev, 0);
	setfiltermult (hDlg);

	xSendDlgItemMessage (hDlg, IDC_FILTERSLR, CB_RESETCONTENT, 0, 0L);
	i = j = 0;
	while (scanlineratios[i * 2]) {
		int sl = scanlineratios[i * 2] * 16 + scanlineratios[i * 2 + 1];
		_stprintf (txt, _T("%d:%d"), scanlineratios[i * 2], scanlineratios[i * 2 + 1]);
		if (workprefs.gf[filter_nativertg].gfx_filter_scanlineratio == sl)
			j = i;
		xSendDlgItemMessage (hDlg, IDC_FILTERSLR, CB_ADDSTRING, 0, (LPARAM)txt);
		scanlineindexes[i] = sl;
		i++;
	}
	xSendDlgItemMessage (hDlg, IDC_FILTERSLR, CB_SETCURSEL, j, 0);

	j = 0;
	xSendDlgItemMessage (hDlg, IDC_FILTERPRESETS, CB_RESETCONTENT, 0, 0L);
	fkey = regcreatetree (NULL, _T("FilterPresets"));
	if (fkey) {
		int idx = 0;
		TCHAR tmp[MAX_DPATH], tmp2[MAX_DPATH];
		int size, size2;

		for (;;) {
			size = sizeof (tmp) / sizeof (TCHAR);
			size2 = sizeof (tmp2) / sizeof (TCHAR);
			if (!regenumstr (fkey, idx, tmp, &size, tmp2, &size2))
				break;
			xSendDlgItemMessage (hDlg, IDC_FILTERPRESETS, CB_ADDSTRING, 0, (LPARAM)tmp);
			idx++;
		}
		regclosetree (fkey);
	}
	for (i = 0; filterpresets[i].name; i++) {
		TCHAR tmp[MAX_DPATH];
		_stprintf (tmp, _T("* %s"), filterpresets[i].name);
		xSendDlgItemMessage (hDlg, IDC_FILTERPRESETS, CB_INSERTSTRING, i, (LPARAM)tmp);
	}
	xSendDlgItemMessage (hDlg, IDC_FILTERPRESETS, CB_INSERTSTRING, 0, (LPARAM)_T(""));
	xSendDlgItemMessage (hDlg, IDC_FILTERPRESETS, CB_SETCURSEL, filterpreset_selected, 0);

	float ho, vo, hz, vz;
	if (workprefs.gf[filter_nativertg].gfx_filter_autoscale == AUTOSCALE_MANUAL) {
		hz = (float)workprefs.gfx_xcenter_size;
		vz = (float)workprefs.gfx_ycenter_size;
		ho = (float)workprefs.gfx_xcenter_pos;
		vo = (float)workprefs.gfx_ycenter_pos;
	} else if (workprefs.gf[filter_nativertg].gfx_filter_autoscale == AUTOSCALE_OVERSCAN_BLANK) {
		hz = (float)workprefs.gf[filter_nativertg].gfx_filter_left_border;
		vz = (float)workprefs.gf[filter_nativertg].gfx_filter_right_border;
		ho = (float)workprefs.gf[filter_nativertg].gfx_filter_top_border;
		vo = (float)workprefs.gf[filter_nativertg].gfx_filter_bottom_border;
	} else {
		hz = (float)workprefs.gf[filter_nativertg].gfx_filter_horiz_zoom;
		vz = (float)workprefs.gf[filter_nativertg].gfx_filter_vert_zoom;
		ho = (float)workprefs.gf[filter_nativertg].gfx_filter_horiz_offset;
		vo = (float)workprefs.gf[filter_nativertg].gfx_filter_vert_offset;
	}

	xSendDlgItemMessage (hDlg, IDC_FILTERHZ, TBM_SETPOS, TRUE, (int)hz);
	xSendDlgItemMessage (hDlg, IDC_FILTERVZ, TBM_SETPOS, TRUE, (int)vz);
	xSendDlgItemMessage (hDlg, IDC_FILTERHO, TBM_SETPOS, TRUE, (int)ho);
	xSendDlgItemMessage (hDlg, IDC_FILTERVO, TBM_SETPOS, TRUE, (int)vo);
	SetDlgItemInt (hDlg, IDC_FILTERHZV, (int)hz, TRUE);
	SetDlgItemInt (hDlg, IDC_FILTERVZV, (int)vz, TRUE);
	SetDlgItemInt (hDlg, IDC_FILTERHOV, (int)ho, TRUE);
	SetDlgItemInt (hDlg, IDC_FILTERVOV, (int)vo, TRUE);
}

static void values_from_hw3ddlg (HWND hDlg)
{
}

static void filter_preset (HWND hDlg, WPARAM wParam)
{
	int ok, load, i, builtin, userfilter;
	TCHAR tmp1[MAX_DPATH], tmp2[MAX_DPATH];
	int outsize;
	UAEREG *fkey;
	LRESULT item;

	load = 0;
	ok = 0;
	for (builtin = 0; filterpresets[builtin].name; builtin++);
	fkey = regcreatetree (NULL, _T("FilterPresets"));
	item = xSendDlgItemMessage (hDlg, IDC_FILTERPRESETS, CB_GETCURSEL, 0, 0);
	tmp1[0] = 0;
	if (item != CB_ERR) {
		filterpreset_selected = (int)item;
		xSendDlgItemMessage (hDlg, IDC_FILTERPRESETS, CB_GETLBTEXT, (WPARAM)item, (LPARAM)tmp1);
	} else {
		filterpreset_selected = -1;
		xSendDlgItemMessage (hDlg, IDC_FILTERPRESETS, WM_GETTEXT, (WPARAM)sizeof tmp1 / sizeof (TCHAR), (LPARAM)tmp1);
	}
	userfilter = 0;
	filterpreset_builtin = -1;
	if (filterpreset_selected < 0 || filterpreset_selected == 0) {
		userfilter = -1;
	} else if (filterpreset_selected > builtin) {
		userfilter = filterpreset_selected - builtin;
	} else {
		filterpreset_builtin = filterpreset_selected - 1;
	}

	if (filterpreset_builtin < 0) {
		outsize = sizeof (tmp2) / sizeof (TCHAR);
		if (tmp1[0] && regquerystr (fkey, tmp1, tmp2, &outsize))
			ok = 1;
	} else {
		TCHAR *p = tmp2;
		for (i = 0; filtervars_wp[i]; i++) {
			if (i > 0) {
				_tcscat (p, _T(","));
				p++;
			}
			_stprintf (p, _T("%d"), filterpresets[filterpreset_builtin].conf[i]);
			p += _tcslen (p);
		}
		ok = 1;
	}

	if (wParam == IDC_FILTERPRESETSAVE && userfilter && fkey) {
		TCHAR *p = tmp2;
		for (i = 0; filtervars_wp[i]; i++) {
			if (i > 0) {
				_tcscat (p, _T(","));
				p++;
			}
			if (filtertypes[i])
				_stprintf (p, _T("%f"), *((float*)filtervars_wp[i]));
			else
				_stprintf (p, _T("%d"), *((int*)filtervars_wp[i]));
			p += _tcslen (p);
		}
		if (ok == 0) {
			tmp1[0] = 0;
			xSendDlgItemMessage (hDlg, IDC_FILTERPRESETS, WM_GETTEXT, (WPARAM)sizeof (tmp1) / sizeof (TCHAR), (LPARAM)tmp1);
			if (tmp1[0] == 0)
				goto end;
		}
		regsetstr (fkey, tmp1, tmp2);
		values_to_hw3ddlg (hDlg, false);
	}
	if (ok) {
		if (wParam == IDC_FILTERPRESETDELETE && userfilter) {
			regdelete (fkey, tmp1);
			values_to_hw3ddlg (hDlg, false);
		} else if (wParam == IDC_FILTERPRESETLOAD) {
			TCHAR *s = tmp2;
			TCHAR *t;

			load = 1;
			_tcscat (s, _T(","));
			t = _tcschr (s, ',');
			*t++ = 0;
			for (i = 0; filtervars_wp[i]; i++) {
				if (filtertypes[i])
					*((float*)filtervars_wp[i]) = (float)_tstof (s);
				else
					*((int*)filtervars_wp[i]) = _tstol (s);
				if (filtervars_cp[i]) {
					if (filtertypes[i])
						*((float*)filtervars_cp[i]) = *((float*)filtervars_wp[i]);
					else
						*((int*)filtervars_cp[i]) = *((int*)filtervars_wp[i]);
				}
				s = t;
				t = _tcschr (s, ',');
				if (!t)
					break;
				*t++ = 0;
			}
			set_config_changed(4);
		}
	}
end:
	regclosetree (fkey);
	enable_for_hw3ddlg (hDlg);
	if (load) {
		values_to_hw3ddlg (hDlg, false);
		SendMessage (hDlg, WM_HSCROLL, 0, 0);
	}
}

static void filter_handle (HWND hDlg)
{
	LRESULT item = xSendDlgItemMessage (hDlg, IDC_FILTERMODE, CB_GETCURSEL, 0, 0L);
	if (item != CB_ERR) {
		TCHAR tmp[MAX_DPATH], oldsh[MAX_DPATH];
		int of = workprefs.gf[filter_nativertg].gfx_filter;
		int offh = workprefs.gf[filter_nativertg].gfx_filter_filtermodeh;
		int offv = workprefs.gf[filter_nativertg].gfx_filter_filtermodev;
		tmp[0] = 0;
		_tcscpy (oldsh, workprefs.gf[filter_nativertg].gfx_filtershader[filterstackpos]);
		xSendDlgItemMessage (hDlg, IDC_FILTERMODE, CB_GETLBTEXT, (WPARAM)item, (LPARAM)tmp);
		workprefs.gf[filter_nativertg].gfx_filtershader[filterstackpos][0] = 0;
		workprefs.gf[filter_nativertg].gfx_filter = 0;
		workprefs.gf[filter_nativertg].gfx_filter_filtermodeh = 0;
		workprefs.gf[filter_nativertg].gfx_filter_filtermodev = 0;
		LRESULT item2 = xSendDlgItemMessage(hDlg, IDC_FILTERFILTERH, CB_GETCURSEL, 0, 0L);
		if (item2 != CB_ERR)
			workprefs.gf[filter_nativertg].gfx_filter_filtermodeh = (int)item2;
		item2 = xSendDlgItemMessage(hDlg, IDC_FILTERFILTERV, CB_GETCURSEL, 0, 0L);
		if (item2 != CB_ERR)
			workprefs.gf[filter_nativertg].gfx_filter_filtermodev = (int)item2;
		if (item > 0) {
			_stprintf (workprefs.gf[filter_nativertg].gfx_filtershader[filterstackpos], _T("%s"), tmp + 5);
			cfgfile_get_shader_config(&workprefs, full_property_sheet ? 0 : filter_nativertg);
			if (of != workprefs.gf[filter_nativertg].gfx_filter ||
				offh != workprefs.gf[filter_nativertg].gfx_filter_filtermodeh ||
				offv != workprefs.gf[filter_nativertg].gfx_filter_filtermodev) {
				values_to_hw3ddlg (hDlg, false);
				hw3d_changed = 1;
			}
		}
		for (int i = 1; i < MAX_FILTERSHADERS; i++) {
			if (workprefs.gf[filter_nativertg].gfx_filtershader[i][0])
				workprefs.gf[filter_nativertg].gfx_filter = 0;
		}
	}

	int overlaytype = xSendDlgItemMessage (hDlg, IDC_FILTEROVERLAYTYPE, CB_GETCURSEL, 0, 0L);
	TCHAR *filterptr = overlaytype == 0 ? workprefs.gf[filter_nativertg].gfx_filteroverlay : workprefs.gf[filter_nativertg].gfx_filtermask[filterstackpos];
	item = xSendDlgItemMessage (hDlg, IDC_FILTEROVERLAY, CB_GETCURSEL, 0, 0L);
	if (item != CB_ERR) {
		TCHAR tmp[MAX_DPATH];
		tmp[0] = 0;
		if (item > 0) {
			xSendDlgItemMessage (hDlg, IDC_FILTEROVERLAY, CB_GETLBTEXT, (WPARAM)item, (LPARAM)tmp);
			if (_tcsicmp (filterptr, tmp)) {
				_tcscpy (filterptr, tmp);
			}
		} else {
			filterptr[0] = 0;
		}
	}
	enable_for_hw3ddlg (hDlg);
	updatedisplayarea(-1);
}

static INT_PTR CALLBACK hw3dDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive;
	int item;
	TCHAR tmp[MAX_DPATH];
	int i;
	static int filteroverlaypos = -1;
	static bool firstinit;
	
	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch (msg)
	{
	case WM_INITDIALOG:
		if (!firstinit) {
			WIN32GUI_LoadUIString (IDS_FILTER_PAL_EXTRA, tmp, sizeof tmp / sizeof (TCHAR));
			TCHAR *p1 = tmp;
			for (i = 0; filter_pal_extra[i].label; i++) {
				TCHAR *p2 = _tcschr (p1, '\n');
				if (!p2 || *p2 == 0)
					break;
				*p2++ = 0;
				filter_pal_extra[i].label = my_strdup(p1);
				p1 = p2;
			}
			WIN32GUI_LoadUIString (IDS_FILTER_3D_EXTRA, tmp, sizeof tmp / sizeof (TCHAR));
			p1 = tmp;
			for (i = 0; filter_3d_extra[i].label; i++) {
				TCHAR *p2 = _tcschr (p1, '\n');
				if (!p2 || *p2 == 0)
					break;
				*p2++ = 0;
				filter_3d_extra[i].label = my_strdup(p1);
				p1 = p2;
			}
			firstinit = true;
		}
		pages[HW3D_ID] = hDlg;
		currentpage = HW3D_ID;
		xSendDlgItemMessage (hDlg, IDC_FILTERASPECT, CB_RESETCONTENT, 0, 0);
		WIN32GUI_LoadUIString (IDS_DISABLED, tmp, sizeof tmp / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_FILTERASPECT, CB_ADDSTRING, 0, (LPARAM)tmp);
		WIN32GUI_LoadUIString (IDS_AUTOMATIC, tmp, sizeof tmp / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_FILTERASPECT, CB_ADDSTRING, 0, (LPARAM)tmp);
		addaspectratios (hDlg, IDC_FILTERASPECT);

		xSendDlgItemMessage (hDlg, IDC_FILTERASPECT2, CB_RESETCONTENT, 0, 0);
		WIN32GUI_LoadUIString (IDS_DISABLED, tmp, sizeof tmp / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_FILTERASPECT2, CB_ADDSTRING, 0, (LPARAM)tmp);
		xSendDlgItemMessage (hDlg, IDC_FILTERASPECT2, CB_ADDSTRING, 0, (LPARAM)_T("VGA"));
		xSendDlgItemMessage (hDlg, IDC_FILTERASPECT2, CB_ADDSTRING, 0, (LPARAM)_T("TV"));

		xSendDlgItemMessage (hDlg, IDC_FILTER_NATIVERTG, CB_RESETCONTENT, 0, 0L);
		WIN32GUI_LoadUIString (IDS_SCREEN_NATIVE, tmp, sizeof tmp / sizeof (TCHAR));
		xSendDlgItemMessage(hDlg, IDC_FILTER_NATIVERTG, CB_ADDSTRING, 0, (LPARAM)tmp);
		WIN32GUI_LoadUIString(IDS_SCREEN_NATIVELACE, tmp, sizeof tmp / sizeof(TCHAR));
		xSendDlgItemMessage(hDlg, IDC_FILTER_NATIVERTG, CB_ADDSTRING, 0, (LPARAM)tmp);
		WIN32GUI_LoadUIString (IDS_SCREEN_RTG, tmp, sizeof tmp / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_FILTER_NATIVERTG, CB_ADDSTRING, 0, (LPARAM)tmp);

		xSendDlgItemMessage (hDlg, IDC_FILTERHZMULT, CB_RESETCONTENT, 0, 0L);
		xSendDlgItemMessage (hDlg, IDC_FILTERVZMULT, CB_RESETCONTENT, 0, 0L);
		for (i = 0; filtermultnames[i]; i++) {
			xSendDlgItemMessage (hDlg, IDC_FILTERHZMULT, CB_ADDSTRING, 0, (LPARAM)filtermultnames[i]);
			xSendDlgItemMessage (hDlg, IDC_FILTERVZMULT, CB_ADDSTRING, 0, (LPARAM)filtermultnames[i]);
		}

		xSendDlgItemMessage (hDlg, IDC_FILTEROVERLAYTYPE, CB_RESETCONTENT, 0, 0L);
		WIN32GUI_LoadUIString (IDS_FILTEROVERLAYTYPE_OVERLAYS, tmp, sizeof tmp / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_FILTEROVERLAYTYPE, CB_ADDSTRING, 0, (LPARAM)tmp);
		WIN32GUI_LoadUIString (IDS_FILTEROVERLAYTYPE_MASKS, tmp, sizeof tmp / sizeof (TCHAR));
		xSendDlgItemMessage (hDlg, IDC_FILTEROVERLAYTYPE, CB_ADDSTRING, 0, (LPARAM)tmp);
		if (filteroverlaypos < 0) {
			if (!workprefs.gf[filter_nativertg].gfx_filteroverlay[0])
				filteroverlaypos = 1;
			else
				filteroverlaypos = 0;
		}
		xSendDlgItemMessage (hDlg, IDC_FILTEROVERLAYTYPE, CB_SETCURSEL, filteroverlaypos, 0);

		recursive++;
		enable_for_hw3ddlg (hDlg);
		values_to_hw3ddlg(hDlg, true);
		recursive--;
		break;

	case WM_USER:
		if(recursive > 0)
			break;
		recursive++;
		enable_for_hw3ddlg (hDlg);
		values_to_hw3ddlg (hDlg, false);
		recursive--;
		return TRUE;
	case WM_COMMAND:
		if(recursive > 0)
			break;
		recursive++;
		switch (wParam)
		{
		case IDC_FILTERDEFAULT:
		{
			struct gfx_filterdata *fd = &currprefs.gf[filter_nativertg];
			struct gfx_filterdata *fdw = &workprefs.gf[filter_nativertg];
			fd->gfx_filter_horiz_zoom = fdw->gfx_filter_horiz_zoom = 0;
			fd->gfx_filter_vert_zoom = fdw->gfx_filter_vert_zoom = 0;
			fd->gfx_filter_horiz_offset = fdw->gfx_filter_horiz_offset = 0;
			fd->gfx_filter_vert_offset = fdw->gfx_filter_vert_offset = 0;
			fd->gfx_filter_horiz_zoom_mult = fdw->gfx_filter_horiz_zoom_mult = 1.0;
			fd->gfx_filter_vert_zoom_mult = fdw->gfx_filter_vert_zoom_mult = 1.0;
			fd->gfx_filter_left_border = fdw->gfx_filter_left_border = -1;
			fd->gfx_filter_top_border = fdw->gfx_filter_top_border = -1;
			fd->gfx_filter_right_border = fdw->gfx_filter_right_border = 0;
			fd->gfx_filter_bottom_border = fdw->gfx_filter_bottom_border = 0;
			currprefs.gfx_xcenter_pos = -1;
			workprefs.gfx_xcenter_pos = -1;
			currprefs.gfx_ycenter_pos = -1;
			workprefs.gfx_ycenter_pos = -1;
			currprefs.gfx_xcenter_size = -1;
			workprefs.gfx_xcenter_size = -1;
			currprefs.gfx_ycenter_size = -1;
			workprefs.gfx_ycenter_size = -1;
			values_to_hw3ddlg(hDlg, false);
			updatedisplayarea(-1);
			break;
		}
		case IDC_FILTERPRESETLOAD:
		case IDC_FILTERPRESETSAVE:
		case IDC_FILTERPRESETDELETE:
			recursive--;
			filter_preset (hDlg, wParam);
			recursive++;
			break;
		case IDC_FILTERKEEPASPECT:
			{
				if (ischecked (hDlg, IDC_FILTERKEEPASPECT))
					currprefs.gf[filter_nativertg].gfx_filter_keep_aspect = workprefs.gf[filter_nativertg].gfx_filter_keep_aspect = 1;
				else
					currprefs.gf[filter_nativertg].gfx_filter_keep_aspect = workprefs.gf[filter_nativertg].gfx_filter_keep_aspect = 0;
				enable_for_hw3ddlg (hDlg);
				values_to_hw3ddlg (hDlg, false);
				updatedisplayarea(-1);
				break;
			}
		case IDC_FILTERKEEPAUTOSCALEASPECT:
			{
				workprefs.gf[filter_nativertg].gfx_filter_keep_autoscale_aspect = currprefs.gf[filter_nativertg].gfx_filter_keep_autoscale_aspect = ischecked (hDlg, IDC_FILTERKEEPAUTOSCALEASPECT) ? 1 : 0;
				enable_for_hw3ddlg (hDlg);
				values_to_hw3ddlg (hDlg, false);
				updatedisplayarea(-1);
			}
			break;
		case IDC_FILTERENABLE:
			if (filter_nativertg == 2) {
				workprefs.gf[filter_nativertg].enable = ischecked(hDlg, IDC_FILTERENABLE);
			}
			break;
		default:
			if (HIWORD (wParam) == CBN_SELCHANGE || HIWORD (wParam) == CBN_KILLFOCUS)  {
				switch (LOWORD (wParam))
				{
				case IDC_FILTER_NATIVERTG:
					item = xSendDlgItemMessage (hDlg, IDC_FILTER_NATIVERTG, CB_GETCURSEL, 0, 0L);
					if (item != CB_ERR) {
						if (item == 0) {
							filter_nativertg = 0;
						} else if (item == 1) {
							filter_nativertg = 2;
						} else {
							filter_nativertg = 1;
						}
						values_to_hw3ddlg (hDlg, false);
						enable_for_hw3ddlg (hDlg);
					}
					break;
				case IDC_FILTERSTACK:
					item = xSendDlgItemMessage (hDlg, IDC_FILTERSTACK, CB_GETCURSEL, 0, 0L);
					if (item != CB_ERR) {
						if (item < MAX_FILTERSHADERS)
							filterstackpos = item;
						else if (item == MAX_FILTERSHADERS)
							filterstackpos = 2 * MAX_FILTERSHADERS;
						else
							filterstackpos = item - 1;
						values_to_hw3ddlg (hDlg, true);
						enable_for_hw3ddlg (hDlg);
					}
					break;
				case IDC_FILTERINTEGER:
					item = xSendDlgItemMessage (hDlg, IDC_FILTERINTEGER, CB_GETCURSEL, 0, 0L);
					if (item != CB_ERR) {
						workprefs.gf[filter_nativertg].gfx_filter_integerscalelimit = item;
						values_to_hw3ddlg (hDlg, false);
						enable_for_hw3ddlg (hDlg);
					}
					break;
				case IDC_FILTERAUTOSCALE:
					item = xSendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_GETCURSEL, 0, 0L);
					if (item != CB_ERR) {
						if (item == AUTOSCALE_SEPARATOR)
							item++;
						workprefs.gf[filter_nativertg].gfx_filter_autoscale = item;
						values_to_hw3ddlg (hDlg, false);
						enable_for_hw3ddlg (hDlg);
					}
					break;
				case IDC_FILTERXTRA:
					values_to_hw3ddlg (hDlg, false);
					break;
				case IDC_FILTERPRESETS:
					filter_preset (hDlg, LOWORD (wParam));
					break;
				case IDC_FILTERSLR:
					item = xSendDlgItemMessage (hDlg, IDC_FILTERSLR, CB_GETCURSEL, 0, 0L);
					if (item != CB_ERR) {
						currprefs.gf[filter_nativertg].gfx_filter_scanlineratio = workprefs.gf[filter_nativertg].gfx_filter_scanlineratio = scanlineindexes[item];
						updatedisplayarea(-1);
					}
					break;
				case IDC_FILTEROVERLAYTYPE:
					item = xSendDlgItemMessage (hDlg, IDC_FILTEROVERLAYTYPE, CB_GETCURSEL, 0, 0L);
					if (item != CB_ERR) {
						filteroverlaypos = item;
						values_to_hw3ddlg (hDlg, true);
					}
					break;
				case IDC_FILTERMODE:
				case IDC_FILTERFILTERH:
				case IDC_FILTERFILTERV:
				case IDC_FILTEROVERLAY:
					filter_handle (hDlg);
					values_to_hw3ddlg (hDlg, false);
					break;
				case IDC_FILTERHZMULT:
					currprefs.gf[filter_nativertg].gfx_filter_horiz_zoom_mult = workprefs.gf[filter_nativertg].gfx_filter_horiz_zoom_mult = getfiltermult (hDlg, IDC_FILTERHZMULT);
					workprefs.gf[filter_nativertg].changed = true;
					updatedisplayarea(-1);
					break;
				case IDC_FILTERVZMULT:
					currprefs.gf[filter_nativertg].gfx_filter_vert_zoom_mult = workprefs.gf[filter_nativertg].gfx_filter_vert_zoom_mult = getfiltermult (hDlg, IDC_FILTERVZMULT);
					workprefs.gf[filter_nativertg].changed = true;
					updatedisplayarea(-1);
					break;
				case IDC_FILTERASPECT:
					{
						int v = xSendDlgItemMessage (hDlg, IDC_FILTERASPECT, CB_GETCURSEL, 0, 0L);
						int v2 = 0;
						if (v != CB_ERR) {
							if (v == 0)
								v2 = 0;
							else if (v == 1)
								v2 = -1;
							else if (v >= 2)
								v2 = getaspectratio (v - 2);
						}
						currprefs.gf[filter_nativertg].gfx_filter_aspect = workprefs.gf[filter_nativertg].gfx_filter_aspect = v2;
						if (filter_nativertg == GF_RTG) {
							currprefs.win32_rtgscaleaspectratio = workprefs.win32_rtgscaleaspectratio = v2;
						}
						workprefs.gf[filter_nativertg].changed = true;
						updatedisplayarea(-1);
					}
					break;
				case IDC_FILTERASPECT2:
					{
						int v = xSendDlgItemMessage (hDlg, IDC_FILTERASPECT2, CB_GETCURSEL, 0, 0L);
						if (v != CB_ERR)
							currprefs.gf[filter_nativertg].gfx_filter_keep_aspect = workprefs.gf[filter_nativertg].gfx_filter_keep_aspect = v;
						updatedisplayarea(-1);
					}
					break;

				}
			}
			break;
		}
		recursive--;
		break;
	case WM_HSCROLL:
		{
			HWND hz = GetDlgItem (hDlg, IDC_FILTERHZ);
			HWND vz = GetDlgItem (hDlg, IDC_FILTERVZ);
			HWND ho = GetDlgItem (hDlg, IDC_FILTERHO);
			HWND vo = GetDlgItem (hDlg, IDC_FILTERVO);
			HWND h = (HWND)lParam;
			struct gfx_filterdata *fd = &currprefs.gf[filter_nativertg];
			struct gfx_filterdata *fdwp = &workprefs.gf[filter_nativertg];

			if (recursive)
				break;
			recursive++;
			if (fdwp->gfx_filter_autoscale == AUTOSCALE_MANUAL) {
				currprefs.gfx_xcenter_size = workprefs.gfx_xcenter_size = (int)SendMessage (hz, TBM_GETPOS, 0, 0);
				currprefs.gfx_ycenter_size = workprefs.gfx_ycenter_size = (int)SendMessage (vz, TBM_GETPOS, 0, 0);
				currprefs.gfx_xcenter_pos = workprefs.gfx_xcenter_pos = (int)SendMessage (ho, TBM_GETPOS, 0, 0);
				currprefs.gfx_ycenter_pos = workprefs.gfx_ycenter_pos = (int)SendMessage (vo, TBM_GETPOS, 0, 0);
				SetDlgItemInt (hDlg, IDC_FILTERHZV, workprefs.gfx_xcenter_size, TRUE);
				SetDlgItemInt (hDlg, IDC_FILTERVZV, workprefs.gfx_ycenter_size, TRUE);
				SetDlgItemInt (hDlg, IDC_FILTERHOV, workprefs.gfx_xcenter_pos, TRUE);
				SetDlgItemInt (hDlg, IDC_FILTERVOV, workprefs.gfx_ycenter_pos, TRUE);
			} else if (fdwp->gfx_filter_autoscale == AUTOSCALE_OVERSCAN_BLANK) {
				fd->gfx_filter_left_border = fdwp->gfx_filter_left_border = (int)SendMessage (hz, TBM_GETPOS, 0, 0);
				fd->gfx_filter_right_border = fdwp->gfx_filter_right_border = (int)SendMessage (vz, TBM_GETPOS, 0, 0);
				fd->gfx_filter_top_border = fdwp->gfx_filter_top_border = (int)SendMessage (ho, TBM_GETPOS, 0, 0);
				fd->gfx_filter_bottom_border = fdwp->gfx_filter_bottom_border = (int)SendMessage (vo, TBM_GETPOS, 0, 0);
				SetDlgItemInt (hDlg, IDC_FILTERHZV, fdwp->gfx_filter_left_border, TRUE);
				SetDlgItemInt (hDlg, IDC_FILTERVZV, fdwp->gfx_filter_right_border, TRUE);
				SetDlgItemInt (hDlg, IDC_FILTERHOV, fdwp->gfx_filter_top_border, TRUE);
				SetDlgItemInt (hDlg, IDC_FILTERVOV, fdwp->gfx_filter_bottom_border, TRUE);
			} else {
				if (h == hz) {
					fd->gfx_filter_horiz_zoom = (float)SendMessage (hz, TBM_GETPOS, 0, 0);
					if (fdwp->gfx_filter_keep_aspect) {
						fd->gfx_filter_vert_zoom = currprefs.gf[filter_nativertg].gfx_filter_horiz_zoom;
						xSendDlgItemMessage (hDlg, IDC_FILTERVZ, TBM_SETPOS, TRUE, (int)fdwp->gfx_filter_vert_zoom);
					}
				} else if (h == vz) {
					fd->gfx_filter_vert_zoom = (float)SendMessage (vz, TBM_GETPOS, 0, 0);
					if (fdwp->gfx_filter_keep_aspect) {
						fd->gfx_filter_horiz_zoom = currprefs.gf[filter_nativertg].gfx_filter_vert_zoom;
						xSendDlgItemMessage (hDlg, IDC_FILTERHZ, TBM_SETPOS, TRUE, (int)fdwp->gfx_filter_horiz_zoom);
					}
				}
				fdwp->gfx_filter_horiz_zoom = fd->gfx_filter_horiz_zoom;
				fdwp->gfx_filter_vert_zoom = fd->gfx_filter_vert_zoom;
				fdwp->gfx_filter_horiz_offset = (float)SendMessage (GetDlgItem (hDlg, IDC_FILTERHO), TBM_GETPOS, 0, 0);
				fdwp->gfx_filter_vert_offset = (float)SendMessage(GetDlgItem(hDlg, IDC_FILTERVO), TBM_GETPOS, 0, 0);
				fd->gfx_filter_horiz_offset = fdwp->gfx_filter_horiz_offset;
				fd->gfx_filter_vert_offset = fdwp->gfx_filter_vert_offset;
				SetDlgItemInt (hDlg, IDC_FILTERHOV, (int)workprefs.gf[filter_nativertg].gfx_filter_horiz_offset, TRUE);
				SetDlgItemInt (hDlg, IDC_FILTERVOV, (int)workprefs.gf[filter_nativertg].gfx_filter_vert_offset, TRUE);
				SetDlgItemInt (hDlg, IDC_FILTERHZV, (int)workprefs.gf[filter_nativertg].gfx_filter_horiz_zoom, TRUE);
				SetDlgItemInt (hDlg, IDC_FILTERVZV, (int)workprefs.gf[filter_nativertg].gfx_filter_vert_zoom, TRUE);
			}
			if (filter_selected && filter_selected->varw[filter_nativertg]) {
				int *pw = filter_selected->varw[filter_nativertg];
				int *pc = filter_selected->varc[filter_nativertg];
				int v = (int)SendMessage (GetDlgItem(hDlg, IDC_FILTERXL), TBM_GETPOS, 0, 0);
				if (v < filter_selected->min)
					v = filter_selected->min;
				if (v > filter_selected->max)
					v = filter_selected->max;
				*pw = v;
				*pc = v;
				SetDlgItemInt (hDlg, IDC_FILTERXLV, v, TRUE);
			}
			if (!full_property_sheet) {
				init_colors(0);
				notice_new_xcolors ();
			}
			updatedisplayarea(-1);
			recursive--;
			break;
		}
	}
	return FALSE;
}
#endif

#ifdef AVIOUTPUT
static void values_to_avioutputdlg (HWND hDlg)
{

	updatewinfsmode(0, &workprefs);
	SetDlgItemText(hDlg, IDC_AVIOUTPUT_FILETEXT, avioutput_filename_gui);
	CheckDlgButton(hDlg, IDC_AVIOUTPUT_FRAMELIMITER, avioutput_framelimiter ? FALSE : TRUE);
	CheckDlgButton(hDlg, IDC_AVIOUTPUT_NOSOUNDOUTPUT, avioutput_nosoundoutput ? TRUE : FALSE);
	CheckDlgButton(hDlg, IDC_AVIOUTPUT_NOSOUNDSYNC, avioutput_nosoundsync ? TRUE : FALSE);
	CheckDlgButton(hDlg, IDC_AVIOUTPUT_ORIGINALSIZE, avioutput_originalsize ? TRUE : FALSE);
	CheckDlgButton(hDlg, IDC_AVIOUTPUT_ACTIVATED, avioutput_requested ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_SCREENSHOT_ORIGINALSIZE, screenshot_originalsize ? TRUE : FALSE);
	CheckDlgButton(hDlg, IDC_SCREENSHOT_PALETTED, screenshot_paletteindexed ? TRUE : FALSE);
	CheckDlgButton(hDlg, IDC_SCREENSHOT_CLIP, screenshot_clipmode ? TRUE : FALSE);
	CheckDlgButton(hDlg, IDC_SCREENSHOT_AUTO, screenshot_multi != 0 ? TRUE : FALSE);
	CheckDlgButton(hDlg, IDC_SAMPLERIPPER_ACTIVATED, sampleripper_enabled ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_STATEREC_RECORD, input_record ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_STATEREC_PLAY, input_play ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hDlg, IDC_STATEREC_AUTOPLAY, workprefs.inprec_autoplay ? BST_CHECKED : BST_UNCHECKED);
}

static void values_from_avioutputdlg (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
}

static void enable_for_avioutputdlg (HWND hDlg)
{
	TCHAR tmp[1000];

#if defined (PROWIZARD)
	ew (hDlg, IDC_PROWIZARD, TRUE);
	if (full_property_sheet)
		ew (hDlg, IDC_PROWIZARD, FALSE);
#endif

	ew (hDlg, IDC_SCREENSHOT, full_property_sheet ? FALSE : TRUE);

	ew (hDlg, IDC_AVIOUTPUT_FILE, TRUE);

	if (!screenshot_originalsize) {
		CheckDlgButton(hDlg, IDC_SCREENSHOT_CLIP, BST_UNCHECKED);
		screenshot_clipmode = 0;
	}
	ew (hDlg, IDC_SCREENSHOT_CLIP, screenshot_originalsize != 0);

	if(workprefs.produce_sound < 2) {
		ew (hDlg, IDC_AVIOUTPUT_AUDIO, FALSE);
		ew (hDlg, IDC_AVIOUTPUT_AUDIO_STATIC, FALSE);
		avioutput_audio = 0;
	} else {
		ew (hDlg, IDC_AVIOUTPUT_AUDIO, TRUE);
		ew (hDlg, IDC_AVIOUTPUT_AUDIO_STATIC, TRUE);
	}

	ew (hDlg, IDC_STATEREC_RATE, !input_record && full_property_sheet ? TRUE : FALSE);
	ew (hDlg, IDC_STATEREC_BUFFERSIZE, !input_record && full_property_sheet ? TRUE : FALSE);

	if (avioutput_audio == AVIAUDIO_WAV) {
		_tcscpy (tmp, _T("Wave (internal)"));
	} else {
		avioutput_audio = AVIOutput_GetAudioCodec (tmp, sizeof tmp / sizeof (TCHAR));
	}
	if(!avioutput_audio) {
		CheckDlgButton (hDlg, IDC_AVIOUTPUT_AUDIO, BST_UNCHECKED);
		WIN32GUI_LoadUIString (IDS_AVIOUTPUT_NOCODEC, tmp, sizeof tmp / sizeof (TCHAR));
	}
	SetWindowText (GetDlgItem (hDlg, IDC_AVIOUTPUT_AUDIO_STATIC), tmp);

	if (avioutput_audio != AVIAUDIO_WAV)
		avioutput_video = AVIOutput_GetVideoCodec (tmp, sizeof tmp / sizeof (TCHAR));
	if(!avioutput_video) {
		CheckDlgButton (hDlg, IDC_AVIOUTPUT_VIDEO, BST_UNCHECKED);
		WIN32GUI_LoadUIString (IDS_AVIOUTPUT_NOCODEC, tmp, sizeof tmp / sizeof (TCHAR));
	}
	SetWindowText (GetDlgItem (hDlg, IDC_AVIOUTPUT_VIDEO_STATIC), tmp);

	ew (hDlg, IDC_AVIOUTPUT_NOSOUNDOUTPUT, avioutput_framelimiter ? TRUE : FALSE);

	if (!avioutput_framelimiter)
		avioutput_nosoundoutput = 1;
	CheckDlgButton(hDlg, IDC_AVIOUTPUT_FRAMELIMITER, avioutput_framelimiter ? FALSE : TRUE);
	CheckDlgButton(hDlg, IDC_AVIOUTPUT_NOSOUNDOUTPUT, avioutput_nosoundoutput ? TRUE : FALSE);
	CheckDlgButton(hDlg, IDC_AVIOUTPUT_NOSOUNDSYNC, avioutput_nosoundsync ? TRUE : FALSE);

	ew (hDlg, IDC_AVIOUTPUT_ACTIVATED, (!avioutput_audio && !avioutput_video) ? FALSE : TRUE);

	CheckDlgButton (hDlg, IDC_STATEREC_RECORD, input_record ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton (hDlg, IDC_STATEREC_PLAY, input_play ? BST_CHECKED : BST_UNCHECKED);
	ew (hDlg, IDC_STATEREC_RECORD, !(input_play == INPREC_PLAY_NORMAL && full_property_sheet));
	ew (hDlg, IDC_STATEREC_SAVE, input_record == INPREC_RECORD_RERECORD || input_record == INPREC_RECORD_PLAYING);
	ew (hDlg, IDC_STATEREC_PLAY, input_record != INPREC_RECORD_RERECORD);
}

static bool get_avioutput_file(HWND hDlg)
{
	OPENFILENAME ofn;

	ZeroMemory(&ofn, sizeof(OPENFILENAME));
	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hwndOwner = hDlg;
	ofn.hInstance = hInst;
	ofn.Flags = OFN_EXTENSIONDIFFERENT | OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
	ofn.lpstrCustomFilter = NULL;
	ofn.nMaxCustFilter = 0;
	ofn.nFilterIndex = avioutput_audio == AVIAUDIO_WAV ? 2 : 0;
	ofn.lpstrFile = avioutput_filename_gui;
	ofn.nMaxFile = MAX_DPATH;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.lpfnHook = NULL;
	ofn.lpTemplateName = NULL;
	ofn.lCustData = 0;
	ofn.lpstrFilter = _T("Video Clip (*.avi)\0*.avi\0Wave Sound (*.wav)\0");

	if (!GetSaveFileName(&ofn))
		return false;

	if (ofn.nFilterIndex == 2) {
		avioutput_audio = AVIAUDIO_WAV;
		avioutput_video = 0;
		if (_tcslen(avioutput_filename_gui) > 4 && !_tcsicmp(avioutput_filename_gui + _tcslen(avioutput_filename_gui) - 4, _T(".avi")))
			_tcscpy(avioutput_filename_gui + _tcslen(avioutput_filename_gui) - 4, _T(".wav"));
		_tcscpy(avioutput_filename_auto, avioutput_filename_gui);
	} else if (avioutput_audio == AVIAUDIO_WAV) {
		avioutput_audio = 0;
		avioutput_video = 0;
		if (_tcslen(avioutput_filename_gui) > 4 && !_tcsicmp(avioutput_filename_gui + _tcslen(avioutput_filename_gui) - 4, _T(".wav")))
			_tcscpy(avioutput_filename_gui + _tcslen(avioutput_filename_gui) - 4, _T(".avi"));
		_tcscpy(avioutput_filename_auto, avioutput_filename_gui);
	}
	return true;
}

static INT_PTR CALLBACK AVIOutputDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;
	TCHAR tmp[1000];

	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch(msg)
	{
	case WM_INITDIALOG:
		pages[AVIOUTPUT_ID] = hDlg;
		currentpage = AVIOUTPUT_ID;
		AVIOutput_GetSettings ();
		regqueryint(NULL, _T("Screenshot_Original"), &screenshot_originalsize);
		regqueryint(NULL, _T("Screenshot_PaletteIndexed"), &screenshot_paletteindexed);
		regqueryint(NULL, _T("Screenshot_ClipMode"), &screenshot_clipmode);
		enable_for_avioutputdlg (hDlg);
		if (!avioutput_filename_gui[0]) {
			fetch_path (_T("VideoPath"), avioutput_filename_gui, sizeof (avioutput_filename_gui) / sizeof (TCHAR));
			_tcscat (avioutput_filename_gui, _T("output.avi"));
		}
		_tcscpy (avioutput_filename_auto, avioutput_filename_gui);
		xSendDlgItemMessage (hDlg, IDC_STATEREC_RATE, CB_RESETCONTENT, 0, 0);
		xSendDlgItemMessage (hDlg, IDC_STATEREC_RATE, CB_ADDSTRING, 0, (LPARAM)_T("-"));
		xSendDlgItemMessage (hDlg, IDC_STATEREC_RATE, CB_ADDSTRING, 0, (LPARAM)_T("1"));
		xSendDlgItemMessage (hDlg, IDC_STATEREC_RATE, CB_ADDSTRING, 0, (LPARAM)_T("2"));
		xSendDlgItemMessage (hDlg, IDC_STATEREC_RATE, CB_ADDSTRING, 0, (LPARAM)_T("5"));
		xSendDlgItemMessage (hDlg, IDC_STATEREC_RATE, CB_ADDSTRING, 0, (LPARAM)_T("10"));
		xSendDlgItemMessage (hDlg, IDC_STATEREC_RATE, CB_ADDSTRING, 0, (LPARAM)_T("20"));
		xSendDlgItemMessage (hDlg, IDC_STATEREC_RATE, CB_ADDSTRING, 0, (LPARAM)_T("30"));
		xSendDlgItemMessage (hDlg, IDC_STATEREC_RATE, CB_ADDSTRING, 0, (LPARAM)_T("60"));
		xSendDlgItemMessage (hDlg, IDC_STATEREC_RATE, CB_SETCURSEL, 0, 0);
		if (workprefs.statecapturerate > 0) {
			_stprintf (tmp, _T("%d"), workprefs.statecapturerate / 50);
			xSendDlgItemMessage( hDlg, IDC_STATEREC_RATE, WM_SETTEXT, 0, (LPARAM)tmp);
		}

		xSendDlgItemMessage (hDlg, IDC_STATEREC_BUFFERSIZE, CB_RESETCONTENT, 0, 0);
		xSendDlgItemMessage (hDlg, IDC_STATEREC_BUFFERSIZE, CB_ADDSTRING, 0, (LPARAM)_T("50"));
		xSendDlgItemMessage (hDlg, IDC_STATEREC_BUFFERSIZE, CB_ADDSTRING, 0, (LPARAM)_T("100"));
		xSendDlgItemMessage (hDlg, IDC_STATEREC_BUFFERSIZE, CB_ADDSTRING, 0, (LPARAM)_T("500"));
		xSendDlgItemMessage (hDlg, IDC_STATEREC_BUFFERSIZE, CB_ADDSTRING, 0, (LPARAM)_T("1000"));
		xSendDlgItemMessage (hDlg, IDC_STATEREC_BUFFERSIZE, CB_ADDSTRING, 0, (LPARAM)_T("10000"));
		_stprintf (tmp, _T("%d"), workprefs.statecapturebuffersize);
		xSendDlgItemMessage( hDlg, IDC_STATEREC_BUFFERSIZE, WM_SETTEXT, 0, (LPARAM)tmp);
		

	case WM_USER:
		recursive++;
		values_to_avioutputdlg (hDlg);
		enable_for_avioutputdlg (hDlg);
		recursive--;
		return TRUE;

	case WM_HSCROLL:
		{
			recursive++;
			values_from_avioutputdlg (hDlg, msg, wParam, lParam);
			values_to_avioutputdlg (hDlg);
			enable_for_avioutputdlg (hDlg);
			recursive--;
			return TRUE;
		}

	case WM_COMMAND:

		if(recursive > 0)
			break;
		recursive++;
		switch(wParam)
		{
		case IDC_AVIOUTPUT_FRAMELIMITER:
			avioutput_framelimiter = ischecked (hDlg, IDC_AVIOUTPUT_FRAMELIMITER) ? 0 : 1;
			AVIOutput_SetSettings ();
			break;
		case IDC_AVIOUTPUT_NOSOUNDOUTPUT:
			avioutput_nosoundoutput = ischecked (hDlg, IDC_AVIOUTPUT_NOSOUNDOUTPUT) ? 1 : 0;
			AVIOutput_SetSettings ();
			break;
		case IDC_AVIOUTPUT_NOSOUNDSYNC:
			avioutput_nosoundsync = ischecked (hDlg, IDC_AVIOUTPUT_NOSOUNDSYNC) ? 1 : 0;
			AVIOutput_SetSettings ();
			break;
		case IDC_AVIOUTPUT_ORIGINALSIZE:
			avioutput_originalsize = ischecked (hDlg, IDC_AVIOUTPUT_ORIGINALSIZE) ? 1 : 0;
			AVIOutput_SetSettings ();
			break;
		case IDC_SCREENSHOT_ORIGINALSIZE:
			screenshot_originalsize = ischecked(hDlg, IDC_SCREENSHOT_ORIGINALSIZE) ? 1 : 0;
			regsetint(NULL, _T("Screenshot_Original"), screenshot_originalsize);
			screenshot_reset();
			break;
		case IDC_SCREENSHOT_PALETTED:
			screenshot_paletteindexed = ischecked(hDlg, IDC_SCREENSHOT_PALETTED) ? 1 : 0;
			regsetint(NULL, _T("Screenshot_PaletteIndexed"), screenshot_paletteindexed);
			screenshot_reset();
			break;
		case IDC_SCREENSHOT_CLIP:
			screenshot_clipmode = ischecked(hDlg, IDC_SCREENSHOT_CLIP) ? 1 : 0;
			regsetint(NULL, _T("Screenshot_ClipMode"), screenshot_clipmode);
			screenshot_reset();
			break;
		case IDC_SCREENSHOT_AUTO:
			screenshot_multi = ischecked(hDlg, IDC_SCREENSHOT_AUTO) ? -1 : 0;
			screenshot_reset();
			if (screenshot_multi) {
				screenshot(-1, 3, 0);
			}
			break;
		case IDC_STATEREC_SAVE:
			if (input_record > INPREC_RECORD_NORMAL) {
				if (DiskSelection (hDlg, wParam, 16, &workprefs, NULL, NULL)) {
					TCHAR tmp[MAX_DPATH];
					_tcscpy (tmp, workprefs.inprecfile);
					_tcscat (tmp, _T(".uss"));
					inprec_save (workprefs.inprecfile, tmp);
					statefile_save_recording (tmp);
					workprefs.inprecfile[0] = 0;
				}
			}
			break;
		case IDC_STATEREC_RECORD:
		{
			if (input_play) {
				inprec_playtorecord ();
			} else if (input_record) {
				inprec_close (true);
			} else {
				input_record = INPREC_RECORD_START;
				set_special (SPCFLAG_MODE_CHANGE);
			}
			break;
		}
		case IDC_STATEREC_AUTOPLAY:
			workprefs.inprec_autoplay = ischecked (hDlg, IDC_STATEREC_AUTOPLAY) ? 1 : 0;
			break;
		case IDC_STATEREC_PLAY:
			if (input_record)
				inprec_close (true);
			if (input_play) {
				inprec_close (true);
			} else {
				inprec_close (true);
				if (DiskSelection (hDlg, wParam, 15, &workprefs, NULL, NULL)) {
					input_play = INPREC_PLAY_NORMAL;
					_tcscpy (currprefs.inprecfile, workprefs.inprecfile);
					set_special (SPCFLAG_MODE_CHANGE);
				}
			}
			break;
#ifdef PROWIZARD
		case IDC_PROWIZARD:
			moduleripper ();
			break;
#endif
		case IDC_SAMPLERIPPER_ACTIVATED:
			if (ischecked(hDlg, IDC_SAMPLERIPPER_ACTIVATED) != (sampleripper_enabled != 0)) {
				sampleripper_enabled = !sampleripper_enabled;
				audio_sampleripper (-1);
			}
			break;

		case IDC_AVIOUTPUT_ACTIVATED:
			if (ischecked(hDlg, IDC_AVIOUTPUT_ACTIVATED) != (avioutput_requested != 0)) {
				AVIOutput_Toggle (!avioutput_requested, false);
			}
			break;
		case IDC_SCREENSHOT:
			screenshot(-1, 1, 0);
			break;
		case IDC_AVIOUTPUT_AUDIO:
			{
				if (avioutput_enabled)
					AVIOutput_End ();
				if(ischecked (hDlg, IDC_AVIOUTPUT_AUDIO)) {
					avioutput_audio = AVIOutput_ChooseAudioCodec (hDlg, tmp, sizeof tmp / sizeof (TCHAR));
					enable_for_avioutputdlg (hDlg);
				} else {
					avioutput_audio = 0;
				}
				break;
			}
		case IDC_AVIOUTPUT_VIDEO:
			{
				if (avioutput_enabled)
					AVIOutput_End ();
				if(ischecked (hDlg, IDC_AVIOUTPUT_VIDEO)) {
					avioutput_video = AVIOutput_ChooseVideoCodec (hDlg, tmp, sizeof tmp / sizeof (TCHAR));
					if (avioutput_audio == AVIAUDIO_WAV)
						avioutput_audio = 0;
					enable_for_avioutputdlg (hDlg);
				} else {
					avioutput_video = 0;
				}
				enable_for_avioutputdlg (hDlg);
				break;
			}
		case IDC_AVIOUTPUT_FILE:
			{
				if (get_avioutput_file(hDlg)) {
					AVIOutput_SetSettings();
				}
				break;
			}
		}
		if (HIWORD (wParam) == CBN_SELENDOK || HIWORD (wParam) == CBN_KILLFOCUS || HIWORD (wParam) == CBN_EDITCHANGE)  {
			switch (LOWORD (wParam))
			{
				case IDC_STATEREC_RATE:
					getcbn (hDlg, IDC_STATEREC_RATE, tmp, sizeof tmp / sizeof (TCHAR));
					workprefs.statecapturerate = _tstol (tmp) * 50;
					if (workprefs.statecapturerate <= 0)
						workprefs.statecapturerate = -1;
					break;
				case IDC_STATEREC_BUFFERSIZE:
					getcbn (hDlg, IDC_STATEREC_BUFFERSIZE, tmp, sizeof tmp / sizeof (TCHAR));
					workprefs.statecapturebuffersize = _tstol (tmp);
					if (workprefs.statecapturebuffersize <= 0)
						workprefs.statecapturebuffersize = 100;
					break;
			}
		}

		values_from_avioutputdlg (hDlg, msg, wParam, lParam);
		values_to_avioutputdlg (hDlg);
		enable_for_avioutputdlg (hDlg);

		recursive--;

		return TRUE;
	}
	return FALSE;
}
#endif

static int GetPanelRect (HWND hDlg, RECT *r)
{
	RECT rect;
	if (!GetWindowRect (guiDlg, &rect))
		return 0;
	if (!GetWindowRect (hDlg, r))
		return 0;
	r->top -= rect.top;
	r->left -= rect.left;
	r->right -= rect.left;
	r->bottom -= rect.top;
	return 1;
}

static int ToolTipImageIDs[] = { 1, IDB_XARCADE, -1 };

static WNDPROC ToolTipWndProcOld;
static LRESULT FAR PASCAL ToolTipWndProc (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	RECT r1;
	POINT p1;
	PAINTSTRUCT ps;
	HBITMAP bm;
	BITMAP binfo;
	HDC memdc;
	int w, h, i;

	for (i = 0; ToolTipHWNDS2[i].hwnd; i++) {
		if (hwnd == ToolTipHWNDS2[i].hwnd)
			break;
	}
	if (!ToolTipHWNDS2[i].hwnd)
		return CallWindowProc (ToolTipHWNDS2[i].proc, hwnd, message, wParam, lParam);

	switch (message)
	{
	case WM_WINDOWPOSCHANGED:
		bm = LoadBitmap (hInst, MAKEINTRESOURCE (ToolTipHWNDS2[i].imageid));
		GetObject (bm, sizeof (binfo), &binfo);
		w = binfo.bmWidth;
		h = binfo.bmHeight;
		GetWindowRect (hwnd, &r1);
		GetCursorPos (&p1);
		r1.right = r1.left + w;
		r1.bottom = r1.top + h;
		MoveWindow (hwnd, r1.left, r1.top, r1.right - r1.left, r1.bottom - r1.top, TRUE);
		DeleteObject (bm);
		return 0;

	case WM_PAINT:
		bm = LoadBitmap (hInst, MAKEINTRESOURCE (ToolTipHWNDS2[i].imageid));
		GetObject (bm, sizeof (binfo), &binfo);
		w = binfo.bmWidth;
		h = binfo.bmHeight;
		GetWindowRect (hwnd, &r1);
		GetCursorPos (&p1);
		r1.right = r1.left + w;
		r1.bottom = r1.top + h;
		BeginPaint (hwnd, &ps);
		memdc = CreateCompatibleDC (ps.hdc);
		SelectObject (memdc, bm);
		ShowWindow (hwnd, SW_SHOWNA);
		MoveWindow (hwnd, r1.left, r1.top, r1.right - r1.left, r1.bottom - r1.top, TRUE);
		SetBkMode (ps.hdc, TRANSPARENT);
		BitBlt (ps.hdc, 0, 0, w, h, memdc, 0, 0, SRCCOPY);
		DeleteObject (bm);
		EndPaint (hwnd, &ps);
		DeleteDC (memdc);
		return FALSE;
	case WM_PRINT:
		PostMessage (hwnd, WM_PAINT, 0, 0);
		return TRUE;
	}
	return CallWindowProc (ToolTipHWNDS2[i].proc, hwnd, message, wParam, lParam);
}

static const int ignorewindows[] = {
	IDD_FLOPPY, IDC_DF0TEXT, IDC_DF1TEXT, IDC_DF2TEXT, IDC_DF3TEXT, IDC_CREATE_NAME,
	-1,
	IDD_QUICKSTART, IDC_DF0TEXTQ, IDC_DF1TEXTQ, IDC_QUICKSTART_HOSTCONFIG,
	-1,
	IDD_AVIOUTPUT, IDC_AVIOUTPUT_FILETEXT, IDC_AVIOUTPUT_VIDEO_STATIC, IDC_AVIOUTPUT_AUDIO_STATIC,
	-1,
	IDD_DISK, IDC_DISKTEXT, IDC_DISKLIST,
	-1,
	IDD_DISPLAY, IDC_DISPLAYSELECT,
	-1,
	IDD_FILTER, IDC_FILTERMODE, IDC_FILTEROVERLAY, IDC_FILTERPRESETS,
	-1,
	IDD_HARDDISK, IDC_VOLUMELIST, IDC_CD_TEXT,
	-1,
	IDD_INPUT, IDC_INPUTDEVICE, IDC_INPUTLIST, IDC_INPUTAMIGA,
	-1,
	IDD_KICKSTART, IDC_ROMFILE, IDC_ROMFILE2, IDC_CARTFILE, IDC_FLASHFILE, IDC_RTCFILE, IDC_SCSIROMSELECT, IDC_SCSIROMFILE, IDC_CPUBOARDROMFILE,
	-1,
	IDD_LOADSAVE, IDC_CONFIGTREE, IDC_EDITNAME, IDC_EDITDESCRIPTION, IDC_CONFIGLINK, IDC_EDITPATH,
	-1,
	IDD_MISC1, IDC_LANGUAGE, IDC_STATENAME,
	-1,
	IDD_PATHS, IDC_PATHS_ROM, IDC_PATHS_CONFIG, IDC_PATHS_NVRAM, IDC_PATHS_SCREENSHOT, IDC_PATHS_SAVESTATE, IDC_PATHS_AVIOUTPUT, IDC_PATHS_SAVEIMAGE, IDC_PATHS_RIP, IDC_LOGPATH,
	-1,
	IDD_IOPORTS, IDC_PRINTERLIST, IDC_SAMPLERLIST, IDC_PS_PARAMS, IDC_SERIAL, IDC_MIDIOUTLIST, IDC_MIDIINLIST, IDC_DONGLELIST,
	-1,
	IDD_SOUND, IDC_SOUNDCARDLIST, IDC_SOUNDDRIVESELECT,
	-1,
	IDD_EXPANSION, IDC_RTG_DISPLAYSELECT,
	-1,
	IDD_GAMEPORTS, IDC_PORT0_JOYS, IDC_PORT1_JOYS, IDC_PORT2_JOYS, IDC_PORT3_JOYS,
	-1,
	IDD_BOARDS, IDC_BOARDLIST,
	-1,
	0
};

static BOOL CALLBACK childenumproc (HWND hwnd, LPARAM lParam)
{
	int i;
	TOOLINFO ti;
	TCHAR tmp[MAX_DPATH];
	TCHAR *p;
	LRESULT v;

	if (GetParent (hwnd) != panelDlg)
		return 1;
	i = 0;
	while (ignorewindows[i]) {
		if (ignorewindows[i] == (int)lParam) {
			int dlgid = GetDlgCtrlID (hwnd);
			i++;
			while (ignorewindows[i] > 0) {
				if (dlgid == ignorewindows[i])
					return 1;
				i++;
			}
			break;
		} else {
			while (ignorewindows[i++] > 0);
		}
	}
	tmp[0] = 0;
	SendMessage (hwnd, WM_GETTEXT, (WPARAM)sizeof (tmp) / sizeof (TCHAR), (LPARAM)tmp);
	p = _tcschr (tmp, '[');
	if (_tcslen (tmp) > 0 && p && _tcslen (p) > 2 && p[1] == ']') {
		int imageid = 0;
		*p++ = 0;
		*p++ = 0;
		if (p[0] == ' ')
			*p++ = 0;
		if (p[0] == '#')
			imageid = _tstol (p + 1);
		tmp[_tcslen (tmp) - 1] = 0;
		ti.cbSize = sizeof (TOOLINFO);
		ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
		ti.hwnd = GetParent (hwnd);
		ti.hinst = hInst;
		ti.uId = (UINT_PTR)hwnd;
		ti.lpszText = p;
		if (imageid > 0) {
			int idx, i;

			idx = 0;
			while (ToolTipHWNDS2[idx].hwnd)
				idx++;
			for (i = 0; ToolTipImageIDs[i] >= 0; i += 2) {
				if (ToolTipImageIDs[i] == imageid)
					break;
			}
			if (ToolTipImageIDs[i] >= 0 && idx < MAX_IMAGETOOLTIPS) {
				HWND ToolTipHWND2 = CreateWindowEx (WS_EX_TOPMOST,
					TOOLTIPS_CLASS, NULL,
					WS_POPUP | TTS_NOPREFIX,
					CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
					panelDlg, NULL, hInst, NULL);
				ToolTipHWNDS2[idx].hwnd = ToolTipHWND2;
				ToolTipHWNDS2[idx+1].hwnd = NULL;
				ToolTipHWNDS2[idx].proc = (WNDPROC)GetWindowLongPtr (ToolTipHWND2, GWLP_WNDPROC);
				ToolTipHWNDS2[idx].imageid = ToolTipImageIDs[i + 1];
				SetWindowLongPtr (ToolTipHWND2, GWLP_WNDPROC, (LONG_PTR)ToolTipWndProc);
				SetWindowPos (ToolTipHWND2, HWND_TOPMOST, 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
				SendMessage (ToolTipHWND2, TTM_SETDELAYTIME, (WPARAM)TTDT_AUTOPOP, (LPARAM)MAKELONG(20000, 0));
				SendMessage (ToolTipHWND2, TTM_SETMAXTIPWIDTH, 0, 400);
				SendMessage (hwnd, WM_SETTEXT, 0, (LPARAM)tmp);
				v = SendMessage (ToolTipHWND2, TTM_ADDTOOL, 0, (LPARAM) (LPTOOLINFO) &ti);
			}
		} else {
			v = SendMessage (hwnd, WM_SETTEXT, 0, (LPARAM)tmp);
			v = SendMessage (ToolTipHWND, TTM_ADDTOOL, 0, (LPARAM) (LPTOOLINFO) &ti);
		}
		return 1;
	}
	p = _tcschr (tmp, ']');
	if (_tcslen (tmp) > 0 && p && _tcslen (p) > 2 && p[1] == '[') {
		RECT r;
		*p++ = 0;
		*p++ = 0;
		if (p[0] == ' ')
			*p++ = 0;
		tmp[_tcslen (tmp) - 1] = 0;
		SendMessage (hwnd, WM_SETTEXT, 0, (LPARAM)tmp);
		ti.cbSize = sizeof (TOOLINFO);
		ti.uFlags = TTF_SUBCLASS;
		ti.hwnd = GetParent (hwnd);
		ti.hinst = hInst;
		ti.uId = (UINT_PTR)hwnd;
		ti.lpszText = p;
		GetWindowRect (GetParent (hwnd), &r);
		GetWindowRect (hwnd, &ti.rect);
		ti.rect.top -= r.top;
		ti.rect.left -= r.left;
		ti.rect.right -= r.left;
		ti.rect.bottom -= r.top;
		SendMessage (ToolTipHWND, TTM_ADDTOOL, 0, (LPARAM) (LPTOOLINFO) &ti);
	}
	return 1;
}

static void getguisize (HWND hwnd, int *width, int *height)
{
	RECT r;

	if (!GetWindowRect(hwnd, &r)) {
		write_log("getguisize failed! %d\n", GetLastError());
		return;
	}
	write_log("getguisize got %dx%d - %dx%d = %dx%d\n", r.left, r.top, r.right, r.bottom, r.right - r.left, r.bottom - r.top);
	*width = (r.right - r.left);
	*height = (r.bottom - r.top);
}

static HWND updatePanel (int id, UINT action)
{
	HWND hDlg = guiDlg;
	static HWND hwndTT;
	static bool first = true;
	int fullpanel;
	HWND focus = GetFocus();

	SaveListView(panelDlg, false);
	listview_id = 0;

	if (!hDlg)
		return NULL;

	if (first) {
		first = false;
		getguisize (hDlg, &gui_width, &gui_height);
	}

	if (cachedlist) {
		if (lv_old_type >= 0)
			lv_oldidx[lv_old_type] = ListView_GetTopIndex (cachedlist);
			lv_oldidx[lv_old_type] += ListView_GetCountPerPage (cachedlist) - 1;
		cachedlist = NULL;
	}

	if (id >= 0)
		currentpage = id;
	ew (guiDlg, IDC_RESETAMIGA, full_property_sheet ? FALSE : TRUE);
	ew (guiDlg, IDOK, TRUE);
	if (panelDlg != NULL) {
		ShowWindow (panelDlg, FALSE);
		x_DestroyWindow(panelDlg, panelresource);
		panelDlg = NULL;
	}
	if (ToolTipHWND != NULL) {
		DestroyWindow (ToolTipHWND);
		ToolTipHWND = NULL;
	}
	for (int i = 0; ToolTipHWNDS2[i].hwnd; i++) {
		DestroyWindow (ToolTipHWNDS2[i].hwnd);
		ToolTipHWNDS2[i].hwnd = NULL;
	}
	hAccelTable = NULL;
	if (id < 0) {
		RECT r;
		if (!gui_fullscreen && IsWindowVisible(hDlg) && !IsIconic(hDlg) && GetWindowRect(hDlg, &r)) {
			int left, top;
			left = r.left;
			top = r.top;
			if (full_property_sheet || isfullscreen () == 0) {
				regsetint (NULL, _T("GUIPosX"), left);
				regsetint (NULL, _T("GUIPosY"), top);
			} else if (isfullscreen () < 0) {
				regsetint (NULL, _T("GUIPosFWX"), left);
				regsetint (NULL, _T("GUIPosFWY"), top);
			} else if (isfullscreen () > 0) {
				regsetint (NULL, _T("GUIPosFSX"), left);
				regsetint (NULL, _T("GUIPosFSY"), top);
			}
		}
		ew (hDlg, IDHELP, FALSE);
		return NULL;
	}

	fullpanel = ppage[id].fullpanel;
	struct newresource *res = ppage[id].nres;
	scaleresource (res, &maindctx, hDlg, -1, 0, 0, id + 1);
	res->parent = panelresource;
	panelresource->child = res;
	panelDlg = x_CreateDialogIndirectParam(res->inst, res->resource, hDlg, ppage[id].dlgproc, id, res);

	rescaleresource(panelresource, false);

	freescaleresource(res);

	ShowWindow (GetDlgItem (hDlg, IDC_PANEL_FRAME), SW_HIDE);
	ShowWindow (GetDlgItem (hDlg, IDC_PANEL_FRAME_OUTER), !fullpanel ? SW_SHOW : SW_HIDE);
	ShowWindow (GetDlgItem (hDlg, IDC_PANELTREE), !fullpanel ? SW_SHOW : SW_HIDE);
	ShowWindow (panelDlg, SW_SHOW);
	ew (hDlg, IDHELP, TRUE);

	ToolTipHWND = CreateWindowEx (WS_EX_TOPMOST,
		TOOLTIPS_CLASS, NULL,
		WS_POPUP | TTS_NOPREFIX | TTS_BALLOON,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		panelDlg, NULL, hInst, NULL);
	SetWindowPos (ToolTipHWND, HWND_TOPMOST, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

#if 0
	SendMessage(ToolTipHWND, TTM_SETDELAYTIME, (WPARAM)TTDT_INITIAL, (LPARAM)MAKELONG(100, 0));
	SendMessage(ToolTipHWND, TTM_SETDELAYTIME, (WPARAM)TTDT_RESHOW, (LPARAM)MAKELONG(0, 0));
#endif
	SendMessage (ToolTipHWND, TTM_SETDELAYTIME, (WPARAM)TTDT_AUTOPOP, (LPARAM)MAKELONG(20000, 0));
	SendMessage (ToolTipHWND, TTM_SETMAXTIPWIDTH, 0, 400);

	EnumChildWindows (panelDlg, &childenumproc, (LPARAM)ppage[currentpage].nres->tmpl);
	SendMessage (panelDlg, WM_NULL, 0, 0);

	hAccelTable = ppage[currentpage].accel;

#if 0
	if (ppage[id].focusid > 0 && action != TVC_BYKEYBOARD) {
		setfocus (panelDlg, ppage[id].focusid);
	}
#endif

	if (focus) {
		SetFocus(focus);
	}

	return panelDlg;
}

static bool panel_done, panel_active_done;

static void checkpagelabel (int id, int sub, const TCHAR *label)
{
	if (full_property_sheet) {
		if (panel_done)
			return;
		if (!label || _tcsicmp (label, currprefs.win32_guipage) != 0)
			return;
		panel_done = true;
	} else {
		if (panel_active_done)
			return;
		if (!label || _tcsicmp (label, currprefs.win32_guiactivepage) != 0)
			return;
		panel_active_done = true;
	}
	currentpage = id;
	configtypepanel = configtype = sub;
}

void gui_restart (void)
{
	panel_done = panel_active_done = false;
}


static HTREEITEM CreateFolderNode (HWND TVhDlg, int nameid, HTREEITEM parent, int nodeid, int sub, const TCHAR *label)
{
	TVINSERTSTRUCT is;
	TCHAR txt[100];

	memset (&is, 0, sizeof (is));
	is.hInsertAfter = TVI_LAST;
	is.hParent = parent;
	is.itemex.mask = TVIF_TEXT | TVIF_PARAM | TVIF_STATE | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
	WIN32GUI_LoadUIString (nameid, txt, sizeof (txt) / sizeof (TCHAR));
	is.itemex.pszText = txt;
	is.itemex.lParam = (LPARAM)(nodeid | (sub << 16));
	is.itemex.iImage = C_PAGES;
	is.itemex.iSelectedImage = C_PAGES;
	is.itemex.state = TVIS_BOLD | TVIS_EXPANDED;
	is.itemex.stateMask = TVIS_BOLD | TVIS_EXPANDED;
	checkpagelabel (nodeid, sub, label);
	return TreeView_InsertItem (TVhDlg, &is);
}

static void CreateNode (HWND TVhDlg, int page, HTREEITEM parent, const TCHAR *label)
{
	TVINSERTSTRUCT is;
	struct GUIPAGE *p;

	if (page < 0)
		return;
	p = &ppage[page];
	memset (&is, 0, sizeof (is));
	is.hInsertAfter = TVI_LAST;
	is.hParent = parent;
	is.itemex.mask = TVIF_TEXT | TVIF_PARAM | TVIF_STATE | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
	is.itemex.pszText = (TCHAR*)p->title;
	is.itemex.lParam = (LPARAM)p->idx;
	is.itemex.iImage = p->himg;
	is.itemex.iSelectedImage = is.itemex.iImage;
	p->tv = TreeView_InsertItem (TVhDlg, &is);
	checkpagelabel (page, 0, label);
}
#define CN(page, label) CreateNode(TVhDlg, page, p, label);

static void createTreeView (HWND hDlg)
{
	HWND TVhDlg;
	int i;
	HIMAGELIST himl;
	HTREEITEM p, root, p1, p2;

	himl = ImageList_Create (16, 16, ILC_COLOR8 | ILC_MASK, C_PAGES + 1, 0);
	if (himl) {
		HICON icon;
		for (i = 0; i < C_PAGES; i++) {
			icon = LoadIcon (hInst, (LPCWSTR)ppage[i].icon);
			ppage[i].himg = ImageList_AddIcon (himl, icon);
		}
		icon = LoadIcon (hInst, MAKEINTRESOURCE (IDI_ROOT));
		ImageList_AddIcon (himl, icon);
	}

	TVhDlg = GetDlgItem (hDlg, IDC_PANELTREE);
	SubclassTreeViewControl(TVhDlg);
	SetWindowRedraw(TVhDlg, FALSE);
	TreeView_SetImageList (TVhDlg, himl, TVSIL_NORMAL);

	p = root = CreateFolderNode (TVhDlg, IDS_TREEVIEW_SETTINGS, NULL, ABOUT_ID, 0, NULL);
	CN (ABOUT_ID, _T("about"));
	CN (PATHS_ID, _T("path"));
	CN (QUICKSTART_ID, _T("quickstart"));
	CN (LOADSAVE_ID, _T("configuration"));
#if FRONTEND == 1
	CN (FRONTEND_ID, _T("frontend"));
#endif

	p1 = p = CreateFolderNode (TVhDlg, IDS_TREEVIEW_HARDWARE, root, LOADSAVE_ID, CONFIG_TYPE_HARDWARE, _T("configuration_hardware"));
	CN (CPU_ID, _T("cpu"));
	CN (CHIPSET_ID, _T("chipset"));
	CN (CHIPSET2_ID, _T("chipset2"));
	CN (KICKSTART_ID, _T("rom"));
	CN (MEMORY_ID, _T("ram"));
	CN (FLOPPY_ID, _T("floppy"));
	CN (HARDDISK_ID, _T("harddisk"));
	CN(EXPANSION2_ID, _T("expansion2"));
	CN(EXPANSION_ID, _T("expansion"));
	CN(BOARD_ID, _T("board"));

	p2 = p = CreateFolderNode (TVhDlg, IDS_TREEVIEW_HOST, root, LOADSAVE_ID, CONFIG_TYPE_HOST, _T("configuration_host"));
	CN (DISPLAY_ID, _T("display"));
	CN (SOUND_ID, _T("sound"));
	CN (GAMEPORTS_ID, _T("gameport"));
	CN (IOPORTS_ID, _T("ioport"));
	CN (INPUT_ID, _T("input"));
	CN (AVIOUTPUT_ID, _T("output"));
	CN (HW3D_ID, _T("filter"));
	CN (DISK_ID, _T("swapper"));
	CN (MISC1_ID, _T("misc"));
	CN (MISC2_ID, _T("misc2"));

	SetWindowRedraw(TVhDlg, TRUE);
	RedrawWindow(TVhDlg, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);

	if (configtypepanel == 1)
		TreeView_SelectItem (TVhDlg, p1);
	else if (configtypepanel == 2)
		TreeView_SelectItem (TVhDlg, p2);
	else
		TreeView_SelectItem (TVhDlg, ppage[currentpage].tv);

}

static int dialog_x_offset, dialog_y_offset;

static bool dodialogmousemove(void)
{
	int monid = 0;
	if (full_property_sheet || isfullscreen () <= 0)
		return false;
	if (isfullscreen () > 0 && currprefs.gfx_monitor[monid].gfx_size_fs.width > gui_width && currprefs.gfx_monitor[monid].gfx_size.height > gui_height)
		return false;
	if (currprefs.gfx_api >= 2)
		return false;
	struct MultiDisplay *mdc = getdisplay(&currprefs, monid);
	for (int i = 0; Displays[i].monitorid; i++) {
		struct MultiDisplay *md = &Displays[i];
		if (md->rect.right - md->rect.left >= 800 && md->rect.bottom - md->rect.top >= 600 && md != mdc)
			return false;
	}
	return true;
}

void getguipos(int *xp, int *yp)
{
	int x = 10, y = 10;
	if (gui_fullscreen) {
		x = gui_fullscreen_rect.left;
		y = gui_fullscreen_rect.top;
	} else {
		if (full_property_sheet || isfullscreen() == 0) {
			regqueryint(NULL, _T("GUIPosX"), &x);
			regqueryint(NULL, _T("GUIPosY"), &y);
		} else if (isfullscreen() < 0) {
			regqueryint(NULL, _T("GUIPosFWX"), &x);
			regqueryint(NULL, _T("GUIPosFWY"), &y);
		} else if (isfullscreen() > 0) {
			regqueryint(NULL, _T("GUIPosFSX"), &x);
			regqueryint(NULL, _T("GUIPosFSY"), &y);
			if (dodialogmousemove()) {
				struct MultiDisplay *mdc = getdisplay(&currprefs, 0);
				x = mdc->rect.left;
				y = mdc->rect.top;
			}
		}
	}
	*xp = x;
	*yp = y;
}

static void centerWindow (HWND hDlg)
{
	RECT rc, rcDlg, rcOwner;
	int x = 0, y = 0;
	POINT pt1, pt2;
	struct MultiDisplay *mdc = getdisplay(&currprefs, 0);

	HWND owner = GetParent (hDlg);
	if (owner == NULL)
		owner = GetDesktopWindow ();

	getguipos(&x, &y);
	pt1.x = x + 100;
	pt1.y = y + (GetSystemMetrics (SM_CYMENU) + GetSystemMetrics (SM_CYBORDER)) / 2;
	pt2.x = x + gui_width - 100;
	pt2.y = pt1.y;
	if (MonitorFromPoint (pt1, MONITOR_DEFAULTTONULL) == NULL && MonitorFromPoint (pt2, MONITOR_DEFAULTTONULL) == NULL) {
		if (isfullscreen () > 0) {
			GetWindowRect (owner, &rcOwner);
			GetWindowRect (hDlg, &rcDlg);
			CopyRect (&rc, &rcOwner);
			OffsetRect (&rcDlg, -rcDlg.left, -rcDlg.top);
			OffsetRect (&rc, -rc.left, -rc.top);
			OffsetRect (&rc, -rcDlg.right, -rcDlg.bottom);
			x = rcOwner.left + (rc.right / 2);
			y = rcOwner.top + (rc.bottom / 2);
			pt1.x = x;
			pt1.y = y;
			pt2.x = x + 16;
			pt2.y = y + GetSystemMetrics (SM_CYMENU) + GetSystemMetrics (SM_CYBORDER);
			if (MonitorFromPoint (pt1, MONITOR_DEFAULTTONULL) == NULL && MonitorFromPoint (pt2, MONITOR_DEFAULTTONULL) == NULL) {
				x = mdc->rect.left;
				y = mdc->rect.top;
			}
		} else {
			x = mdc->rect.left + 16;
			y = mdc->rect.top + 16;
		}
	}
	//write_log (_T("centerwindow %dx%d\n"), x, y);
	dialog_x_offset = x;
	dialog_y_offset = y;
	SetWindowPos (hDlg,  HWND_TOP, x, y, 0, 0, SWP_NOSIZE);
}

static int floppyslot_addfile2 (struct uae_prefs *prefs, const TCHAR *file, int drv, int firstdrv, int maxdrv)
{
	_tcscpy (workprefs.floppyslots[drv].df, file);
	disk_insert (drv, workprefs.floppyslots[drv].df);
	drv++;
	if (drv >= (currentpage == QUICKSTART_ID ? 2 : 4))
		drv = 0;
	if (workprefs.floppyslots[drv].dfxtype < 0)
		drv = 0;
	if (drv == firstdrv)
		return -1;
	return drv;
}
static int floppyslot_addfile (struct uae_prefs *prefs, const TCHAR *filepath, const TCHAR *file, int drv, int firstdrv, int maxdrv)
{
	if (!filepath[0]) {
		struct zdirectory *zd = zfile_opendir_archive (file, ZFD_ARCHIVE | ZFD_NORECURSE);
		if (zd && zfile_readdir_archive (zd, NULL, true) > 1) {
			TCHAR out[MAX_DPATH];
			while (zfile_readdir_archive (zd, out, true)) {
				struct zfile *zf = zfile_fopen (out, _T("rb"), ZFD_NORMAL);
				if (zf) {
					int type = zfile_gettype (zf);
					if (type == ZFILE_DISKIMAGE || type == ZFILE_EXECUTABLE) {
						drv = floppyslot_addfile2 (prefs, out, drv, firstdrv, maxdrv);
						if (drv < 0)
							break;
					}
				}
			}
			zfile_closedir_archive (zd);
		} else {
			drv = floppyslot_addfile2 (prefs, file, drv, firstdrv, maxdrv);
		}
	} else {
		drv = floppyslot_addfile2(prefs, filepath, drv, firstdrv, maxdrv);
	}
	return drv;
}

static int do_filesys_insert (const TCHAR *root, int total)
{
	if (total <= 1) {
		if (filesys_insert(-1, NULL, root, 0, 0))
			return 1;
		return filesys_media_change(root, 2, NULL);
	}
	filesys_media_change_queue(root, total);
	return 1;
}

static bool draghit(DWORD id, POINT pt)
{
	RECT r;
	if (GetPanelRect(GetDlgItem(panelDlg, id), &r)) {
		int extra = r.bottom - r.top;
		r.top -= extra;
		r.bottom += extra;
		return PtInRect(&r, pt) != 0;
	}
	return false;
}

int dragdrop (HWND hDlg, HDROP hd, struct uae_prefs *prefs, int	currentpage)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	int cnt, i, drv, harddrive, drvdrag, firstdrv;
	TCHAR file[MAX_DPATH];
	TCHAR *filepart = NULL;
	const int dfxtext[] = { IDC_DF0TEXT, IDC_DF0TEXTQ, IDC_DF1TEXT, IDC_DF1TEXTQ, IDC_DF2TEXT, -1, IDC_DF3TEXT, -1 };
	POINT pt;
	RECT r, r2;
	int ret = 0;
	DWORD flags;
	TCHAR *dragrompath = NULL;
	int corner = -1;

	filesys_media_change_queue(NULL, -1);
	DragQueryPoint (hd, &pt);
	pt.y += GetSystemMetrics (SM_CYMENU) + GetSystemMetrics (SM_CYBORDER);
	cnt = DragQueryFile (hd, 0xffffffff, NULL, 0);
	if (!cnt)
		return 0;
	drv = harddrive = 0;
	drvdrag = 0;
	if (currentpage < 0) {
		GetClientRect(mon->hMainWnd, &r2);
		if (mon->hStatusWnd) {
			GetClientRect(mon->hStatusWnd, &r);
			if (pt.y >= r2.bottom && pt.y < r2.bottom + r.bottom) {
				if (pt.x >= window_led_drives && pt.x < window_led_drives_end && window_led_drives > 0) {
					drv = pt.x - window_led_drives;
					drv /= (window_led_drives_end - window_led_drives) / 4;
					drvdrag = 1;
					if (drv < 0 || drv >= currprefs.nr_floppies)
						drv = 0;
					corner = -2;
				}
				if (pt.x >= window_led_hd && pt.x < window_led_hd_end && window_led_hd > 0) {
					harddrive = 1;
					corner = -2;
				}
			}
		}
		if (corner == -1) {
			int div = 10;
			if (pt.y < r2.bottom / div && pt.x < r2.right / div)
				corner = 0;
			if (pt.y < r2.bottom / div && pt.x >= r2.right - r2.right / div)
				corner = 1;
			if (pt.y >= r2.bottom - r2.bottom / div && pt.x < r2.right / div)
				corner = 2;
			if (pt.y >= r2.bottom - r2.bottom / div && pt.x >= r2.right - r2.right / div)
				corner = 3;
			if (corner >= 0)
				drv = corner;
		}
	} else if (currentpage == FLOPPY_ID || currentpage == QUICKSTART_ID) {
		for (i = 0; i < 4; i++) {
			int id = dfxtext[i * 2 + (currentpage == QUICKSTART_ID ? 1 : 0)];
			if (workprefs.floppyslots[i].dfxtype >= 0 && id >= 0) {
				if (draghit(id, pt)) {
					drv = i;
					break;
				}
			}
		}
	} else if (currentpage == KICKSTART_ID) {
		if (draghit(IDC_ROMFILE, pt)) {
			dragrompath = prefs->romfile;
		} else if (draghit(IDC_ROMFILE2, pt)) {
			dragrompath = prefs->romextfile;
		} else if (draghit(IDC_CARTFILE, pt)) {
			dragrompath = prefs->cartfile;
		} else if (draghit(IDC_FLASHFILE, pt)) {
			dragrompath = prefs->flashfile;
		} else if (draghit(IDC_RTCFILE, pt)) {
			dragrompath = prefs->rtcfile;
		}
	} else if (currentpage == HARDDISK_ID) {
		if (draghit(IDC_CD_TEXT, pt)) {
			dragrompath = prefs->cdslots[0].name;
		}
	}
	firstdrv = drv;
	for (i = 0; i < cnt; i++) {
		struct romdata *rd = NULL;
		struct zfile *z;
		int type = -1, zip = 0;
		int mask;
		TCHAR filepath[MAX_DPATH];

		DragQueryFile (hd, i, file, sizeof (file) / sizeof (TCHAR));
		my_resolvesoftlink (file, sizeof file / sizeof (TCHAR), true);
		filepart = _tcsrchr (file, '/');
		if (!filepart)
			filepart = _tcsrchr (file, '\\');
		if (filepart)
			filepart++;
		else
			filepart = file;
		flags = GetFileAttributes (file);
		if (flags & FILE_ATTRIBUTE_DIRECTORY)
			type = ZFILE_HDF;
		if (harddrive)
			mask = ZFD_ALL;
		else
			mask = ZFD_NORMAL;
		filepath[0] = 0;
		if (type < 0) {
			if (currentpage < 0) {
				z = zfile_fopen (file, _T("rb"), 0);
				if (z) {
					zip = iszip (z);
					zfile_fclose (z);
				}
			}
			if (!zip) {
				z = zfile_fopen (file, _T("rb"), mask);
				if (z) {
					if (currentpage < 0 && iszip (z)) {
						zip = 1;
					} else {
						type = zfile_gettype (z);
						if (type == ZFILE_ROM) {
							rd = getromdatabyzfile (z);
						} else if (currentpage == QUICKSTART_ID || currentpage == LOADSAVE_ID) {
							if (type == ZFILE_UNKNOWN && iszip(z)) {
								type = ZFILE_HDF;
							}
						}						
						// replace with decrunched path but only if
						// floppy insert and decrunched path is deeper (longer)
						if (type > 0 && _tcslen(z->name) > _tcslen(file) && drv >= 0) {
							_tcscpy(filepath, z->name);
						}
					}
					zfile_fclose (z);
					z = NULL;
				}
			}
		}

		if (customDlgType == IDD_HARDFILE) {
			_tcscpy (current_hfdlg.ci.rootdir, file);
			SetDlgItemText (hDlg, IDC_PATH_NAME, current_hfdlg.ci.rootdir);
			updatehdfinfo (customDlg, true, true, false);
			sethardfile (customDlg);
			continue;
		}

		if (drvdrag) {
			type = ZFILE_DISKIMAGE;
		} else if ((zip || harddrive) && type != ZFILE_DISKIMAGE && type != ZFILE_EXECUTABLE) {
			if (do_filesys_insert (file, cnt))
				continue;
			if (zip) {
				struct zfile *z2 = zfile_fopen (file, _T("rb"), mask);
				if (z2) {
					type = zfile_gettype (z2);
					zfile_fclose (z2);
				}
			}
		}
		if (dragrompath && !(flags & FILE_ATTRIBUTE_DIRECTORY)) {
			_tcscpy(dragrompath, file);
			type = -2;
		}

		switch (type)
		{
		case ZFILE_DISKIMAGE:
		case ZFILE_EXECUTABLE:
			if (currentpage == DISK_ID) {
				diskswapper_addfile(prefs, file, -1);
			} else if (currentpage == HARDDISK_ID) {
				default_fsvdlg (&current_fsvdlg);
				_tcscpy (current_fsvdlg.ci.rootdir, file);
				add_filesys_config (&workprefs, -1, &current_fsvdlg.ci);
			} else if (harddrive) {
				do_filesys_insert (file, cnt);
			} else {
				drv = floppyslot_addfile (prefs, filepath, file, drv, firstdrv, i);
				if (drv < 0)
					i = cnt;
			}
			break;
		case ZFILE_ROM:
			if (rd) {
				if (rd->type == ROMTYPE_KICK || rd->type == ROMTYPE_KICKCD32)
					_tcscpy (prefs->romfile, file);
				if (rd->type == ROMTYPE_EXTCD32 || rd->type == ROMTYPE_EXTCDTV)
					_tcscpy (prefs->romextfile, file);
				if (rd->type & ROMTYPE_FREEZER)
					_tcscpy (prefs->cartfile, file);
			} else {
				_tcscpy (prefs->romfile, file);
			}
			break;
		case ZFILE_HDF:
			if (flags & FILE_ATTRIBUTE_DIRECTORY) {
				if (!full_property_sheet && currentpage < 0) {
					do_filesys_insert (file, cnt);
				} else {
					default_fsvdlg (&current_fsvdlg);
					_tcscpy (current_fsvdlg.ci.rootdir, file);
					add_filesys_config (&workprefs, -1, &current_fsvdlg.ci);
				}
			} else {
				default_hfdlg (&current_hfdlg, false);
				_tcscpy (current_hfdlg.ci.rootdir, file);
				updatehdfinfo (NULL, true, true, false);
				add_filesys_config (&workprefs, -1, &current_hfdlg.ci);
			}
			break;
		case ZFILE_HDFRDB:
			default_rdb_hfdlg (&current_hfdlg, file);
			add_filesys_config (&workprefs, -1, &current_hfdlg.ci);
			break;
		case ZFILE_NVR:
			_tcscpy (prefs->flashfile, file);
			break;
		case ZFILE_CONFIGURATION:
			if (target_cfgfile_load (&workprefs, file, CONFIG_TYPE_DEFAULT, 0)) {
				if (full_property_sheet) {
					inputdevice_updateconfig (NULL, &workprefs);
					if (!workprefs.start_gui)
						ret = 1;
				} else {
					uae_restart(&workprefs, workprefs.start_gui, file);
					ret = 1;
				}
			}
			break;
		case ZFILE_STATEFILE:
			savestate_state = STATE_DORESTORE;
			_tcscpy (savestate_fname, file);
			ret = 1;
			break;
		case ZFILE_CDIMAGE:
			_tcscpy (prefs->cdslots[0].name, file);
			break;
		case -2:
			break;
		default:
			if (currentpage < 0 && !full_property_sheet) {
				do_filesys_insert (file, cnt);
			} else if (currentpage == HARDDISK_ID) {
				default_fsvdlg (&current_fsvdlg);
				_tcscpy (current_fsvdlg.ci.rootdir, file);
				_tcscpy (current_fsvdlg.ci.volname, filepart);
				add_filesys_config (&workprefs, -1, &current_fsvdlg.ci);
				if (!full_property_sheet)
					do_filesys_insert (file, cnt);
			} else {
				rd = scan_arcadia_rom (file, 0);
				if (rd) {
					if (rd->type == ROMTYPE_ARCADIABIOS || rd->type == ROMTYPE_ALG)
						_tcscpy (prefs->romextfile, file);
					else if (rd->type == ROMTYPE_ARCADIAGAME)
						_tcscpy (prefs->cartfile, file);
				}
			}
			break;
		}
	}
	DragFinish (hd);
	filesys_media_change_queue(NULL, 0);
	set_config_changed ();
	return ret;
}

static int HitTestToSizingEdge(WPARAM wHitTest)
{
	switch (wHitTest)
	{
		case HTLEFT:        return WMSZ_LEFT;
		case HTRIGHT:       return WMSZ_RIGHT;
		case HTTOP:         return WMSZ_TOP;
		case HTTOPLEFT:     return WMSZ_TOPLEFT;
		case HTTOPRIGHT:    return WMSZ_TOPRIGHT;
		case HTBOTTOM:      return WMSZ_BOTTOM;
		case HTBOTTOMLEFT:  return WMSZ_BOTTOMLEFT;
		case HTBOTTOMRIGHT: return WMSZ_BOTTOMRIGHT;
	}
	return 0;
}


static bool gui_resizing;
static int nSizingEdge;
static POINT ptResizePos;
static RECT rcResizeStartWindowRect;

static void StartCustomResize(HWND hWindow, int nEdge, int x, int y)
{
	gui_resizing = TRUE;
	SetCapture(hWindow);
	nSizingEdge = nEdge;
	ptResizePos.x = x;
	ptResizePos.y = y;
	GetWindowRect(hWindow, &rcResizeStartWindowRect);
}

static void CustomResizeMouseMove(HWND hWindow)
{
	POINT pt;
	GetCursorPos(&pt);
	if (pt.x != ptResizePos.x || pt.y != ptResizePos.y) {
		int x = rcResizeStartWindowRect.left;
		int y = rcResizeStartWindowRect.top;
		int w, h;
		int dx = pt.x - ptResizePos.x;
		int dy = pt.y - ptResizePos.y;
		getguisize(hWindow, &w, &h);
		switch (nSizingEdge)
		{
			case WMSZ_TOP:
				y = pt.y;
				h -= dy;
				gui_size_changed = -1;
				break;
			case WMSZ_BOTTOM:
				h += dy;
				gui_size_changed = -1;
				break;
			case WMSZ_LEFT:
				x = pt.x;
				w -= dx;
				gui_size_changed = -1;
				break;
			case WMSZ_RIGHT:
				w += dx;
				gui_size_changed = -1;
				break;
			case WMSZ_TOPLEFT:
				x = pt.x;
				w -= dx;
				y = pt.y;
				h -= dy;
				gui_size_changed = -1;
				break;
			case WMSZ_TOPRIGHT:
				w += dx;
				y = pt.y;
				h -= dy;
				gui_size_changed = -1;
				break;
			case WMSZ_BOTTOMLEFT:
				x = pt.x;
				w -= dx;
				h += dy;
				gui_size_changed = -1;
				break;
			case WMSZ_BOTTOMRIGHT:
				w += dx;
				h += dy;
				gui_size_changed = -1;
				break;
		}
		if (gui_size_changed < 0) {
			gui_width = w;
			gui_height = h;
			SetWindowPos(hWindow, NULL, x, y,w, h, 0);
		}
		ptResizePos.x = pt.x;
		ptResizePos.y = pt.y;
	}
}

static void EndCustomResize(HWND hWindow, BOOL bCanceled)
{
	gui_resizing = false;
	ReleaseCapture();
	if (bCanceled) {
		SetWindowPos(hWindow, NULL, rcResizeStartWindowRect.left, rcResizeStartWindowRect.top,
			rcResizeStartWindowRect.right - rcResizeStartWindowRect.left, rcResizeStartWindowRect.bottom - rcResizeStartWindowRect.top,
			SWP_NOZORDER | SWP_NOACTIVATE);
	}
}

static int dialogreturn;
static int devicechangetimer = -1;
static INT_PTR CALLBACK DialogProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;
	static int oldwidth, oldheight;

	bool handled;
	INT_PTR vv = commonproc2(hDlg, msg, wParam, lParam, &handled);
	if (handled) {
		return vv;
	}

	switch (msg)
	{
	case  WM_DPICHANGED:
	{
		if (gui_size_changed <= 1 && hGUIWnd) {
			int dx = LOWORD(wParam);
			int dy = HIWORD(wParam);
			RECT *const r = (RECT*)lParam;
			gui_size_changed = 0;
			gui_width = (r->right - r->left);
			gui_height = (r->bottom - r->top);
			oldwidth = gui_width;
			oldheight = gui_height;
			saveguisize();
			SetWindowPos(hDlg, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
			scaleresource_setsize(gui_width, gui_height, 0);
		}
	}
	break;
	case WM_MOVING:
		move_box_art_window();
		return FALSE;
	case WM_MOVE:
		move_box_art_window();
		return TRUE;
	case WM_SIZE:
		if (!gui_size_changed && hGUIWnd && (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED)) {
			getguisize(hDlg, &gui_width, &gui_height);
			oldwidth = gui_width;
			oldheight = gui_height;
			saveguisize();
			gui_size_changed = 1;
			return 0;
		}
		break;
	case WM_SIZING:
	{
		if (!recursive && gui_resize_enabled) {
			RECT *r = (RECT*)lParam;
			if (r->right - r->left < MIN_GUI_INTERNAL_WIDTH)
				r->right = r->left + MIN_GUI_INTERNAL_WIDTH;
			if (r->bottom - r->top < MIN_GUI_INTERNAL_HEIGHT)
				r->bottom = r->top + MIN_GUI_INTERNAL_HEIGHT;
			return FALSE;
		}
		break;
	}
	case WM_ENTERSIZEMOVE:
		if (!recursive && gui_resize_enabled) {
			getguisize (hDlg, &oldwidth, &oldheight);
			return FALSE;
		}
		break;
	case WM_EXITSIZEMOVE:
		if (!recursive && gui_resize_enabled) {
			int w, h;
			getguisize (hDlg, &w, &h);
			if (w != oldwidth || h != oldheight) {
				gui_width = w;
				gui_height = h;
				gui_size_changed = 1;
			}
			return FALSE;
		}
		break;

	case WM_NCLBUTTONDOWN:
		switch (wParam)
		{
			case HTLEFT:
			case HTRIGHT:
			case HTTOP:
			case HTTOPLEFT:
			case HTTOPRIGHT:
			case HTBOTTOM:
			case HTBOTTOMLEFT:
			case HTBOTTOMRIGHT:
				if (gui_resize_enabled)
				{
					SetForegroundWindow(hDlg);
					StartCustomResize(hDlg, HitTestToSizingEdge(wParam), GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
					return 0;
				}
				break;
		}
		break;

	case WM_MOUSEMOVE:
		if (gui_resizing)
		{
			CustomResizeMouseMove(hDlg);
		}
		break;
	case WM_LBUTTONUP:
		if (gui_resizing) {
			EndCustomResize(hDlg, FALSE);
			return 0;
		}
		break;
	case WM_CANCELMODE:
		if (gui_resizing) {
			EndCustomResize(hDlg, FALSE);
		}
		break;
	case WM_KEYDOWN:
		if (gui_resizing && wParam == VK_ESCAPE)
		{
			EndCustomResize(hDlg, TRUE);
			return 0;
		}
		break;
	case WM_DEVICECHANGE:
		{
			DEV_BROADCAST_HDR *pBHdr = (DEV_BROADCAST_HDR *)lParam;
			int doit = 0;
			if (wParam == DBT_DEVNODES_CHANGED && lParam == 0) {
				doit = 1;
			} else if (pBHdr && pBHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
				DEV_BROADCAST_DEVICEINTERFACE *dbd = (DEV_BROADCAST_DEVICEINTERFACE*)lParam;
				write_log (_T("%s: %s\n"), wParam == DBT_DEVICEREMOVECOMPLETE ? _T("Removed") : _T("Inserted"),
					dbd->dbcc_name);
				if (wParam == DBT_DEVICEREMOVECOMPLETE)
					doit = 1;
				else if (wParam == DBT_DEVICEARRIVAL)
					doit = 1;
			}
			if (doit) {
				if (devicechangetimer < 0)
					return TRUE;
				if (devicechangetimer)
					KillTimer(hDlg, 3);
				devicechangetimer = 1;
				SetTimer(hDlg, 3, 2000, NULL);
			}
		}
		return TRUE;
	case WM_TIMER:
		if (wParam == 3) {
			KillTimer(hDlg, 3);
			devicechangetimer = 0;
			if (inputdevice_devicechange (&workprefs))
				updatePanel (currentpage, 0);
		}
		if (wParam == 4) {
			KillTimer(hDlg, 4);
			if (devicechangetimer < 0)
				devicechangetimer = 0;
		}
		break;

	case WM_DESTROY:
		PostQuitMessage (0);
		return TRUE;
	case WM_CLOSE:
		if (devicechangetimer)
			KillTimer(hDlg, 3);
		devicechangetimer = 0;
		addnotifications (hDlg, TRUE, TRUE);
		updatePanel (-1, 0);
		show_box_art(NULL, NULL);
		DestroyWindow(hDlg);
		if (dialogreturn < 0) {
			dialogreturn = 0;
			if (allow_quit) {
				quit_program = UAE_QUIT;
				regs.spcflags |= SPCFLAG_MODE_CHANGE;
			}
		}
		return TRUE;
	case WM_INITDIALOG:
		guiDlg = hDlg;
		SendMessage (hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE(IDI_APPICON)));
		if (full_property_sheet) {
			TCHAR tmp[100];
			WIN32GUI_LoadUIString (IDS_STARTEMULATION, tmp, sizeof (tmp) / sizeof (TCHAR));
			SetWindowText (GetDlgItem (guiDlg, IDOK), tmp);
		}
		ShowWindow (GetDlgItem (guiDlg, IDC_RESTARTEMU), full_property_sheet ? SW_HIDE : SW_SHOW);
		ShowWindow (GetDlgItem (guiDlg, IDC_ERRORLOG), is_error_log () ? SW_SHOW : SW_HIDE);
		centerWindow (hDlg);
		createTreeView (hDlg);
		updatePanel (currentpage, 0);
		addnotifications (hDlg, FALSE, TRUE);
		return TRUE;
	case WM_DROPFILES:
		if (dragdrop (hDlg, (HDROP)wParam, (gui_active || full_property_sheet) ? &workprefs : &changed_prefs, currentpage))
			SendMessage (hDlg, WM_COMMAND, IDOK, 0);
		updatePanel (currentpage, 0);
		return FALSE;
	case WM_NOTIFY:
		{
			switch (((LPNMHDR)lParam)->code)
			{
			case TVN_SELCHANGING:
				return FALSE;
			case TVN_SELCHANGED:
				{
					int cp, cf;
					LPNMTREEVIEW tv = (LPNMTREEVIEW)lParam;
					cp = (int)(tv->itemNew.lParam & 0xffff);
					cf = (int)(tv->itemNew.lParam >> 16);
					if (cp != currentpage || cf != configtype) {
						configtypepanel = configtype = cf;
						getconfigfolderregistry();
						updatePanel (cp, tv->action);
					}
					return TRUE;
				}
				break;
			}
			break;
		}
	case WM_COMMAND:
		{
			switch (LOWORD(wParam))
			{
			case IDC_ERRORLOG:
				{
					CustomCreateDialogBox(IDD_ERRORLOG, hDlg, ErrorLogProc);
					ShowWindow (GetDlgItem (guiDlg, IDC_ERRORLOG), is_error_log () ? SW_SHOW : SW_HIDE);
				}
				break;
			case IDC_RESETAMIGA:
				uae_reset(1, 1);
				SendMessage (hDlg, WM_COMMAND, IDOK, 0);
				return TRUE;
			case IDC_QUITEMU:
				uae_quit ();
				SendMessage (hDlg, WM_COMMAND, IDCANCEL, 0);
				return TRUE;
			case IDC_RESTARTEMU:
				uae_restart(&workprefs, -1, NULL);
				exit_gui (1);
				return TRUE;
			case IDHELP:
				HtmlHelp(ppage[currentpage].help);
				return TRUE;
			case IDOK:
				updatePanel (-1, 0);
				dialogreturn = 1;
				DestroyWindow (hDlg);
				gui_to_prefs ();
				guiDlg = NULL;
				return TRUE;
			case IDCANCEL:
				updatePanel (-1, 0);
				dialogreturn = 0;
				DestroyWindow (hDlg);
				if (allow_quit) {
					quit_program = UAE_QUIT;
					regs.spcflags |= SPCFLAG_MODE_CHANGE;
				}
				guiDlg = NULL;
				return TRUE;
			}
			break;
		}

	case WM_DWMCOMPOSITIONCHANGED:
		gui_size_changed = 1;
		return 0;
	}

	handlerawinput (hDlg, msg, wParam, lParam);
	return FALSE;
}

struct newresource *getresource (int tmpl)
{
	TCHAR rid[20];
	HRSRC hrsrc;
	HGLOBAL res;
	HINSTANCE inst = hUIDLL ? hUIDLL : hInst;
	void *resdata;
	LPCDLGTEMPLATEW newres;
	struct newresource *nr;
	int size;

	_stprintf (rid, _T("#%d"), tmpl);
	hrsrc = FindResource (inst, rid, RT_DIALOG);
	if (!hrsrc) {
		inst = hInst;
		hrsrc = FindResource (inst, rid, RT_DIALOG);
	}
	if (!hrsrc)
		return NULL;
	res = LoadResource (inst, hrsrc);
	if (!res)
		return NULL;
	resdata = LockResource (res);
	if (!resdata)
		return NULL;
	size = SizeofResource (inst, hrsrc);
	if (!size)
		return NULL;
	nr = xcalloc (struct newresource, 1);
	if (!nr)
		return NULL;
	newres = (LPCDLGTEMPLATEW)xmalloc (uae_u8, size);
	if (!newres) {
		xfree(nr);
		return NULL;
	}
	memcpy ((void*)newres, resdata, size);
	nr->sourceresource = newres;
	nr->sourcesize = size;
	nr->tmpl = tmpl;
	nr->inst = inst;
	return nr;
}

void CustomDialogClose(HWND hDlg, int status)
{
	if (cdstate.parent) {
		EnableWindow(cdstate.parent, TRUE);
		cdstate.parent = NULL;
	}
	cdstate.status = status;
	cdstate.active = 0;
	cdstate.hwnd = NULL;
	freescaleresource(cdstate.res);
	cdstate.res = NULL;
	DestroyWindow(hDlg);
}

INT_PTR CustomDialogBox(int templ, HWND hDlg, DLGPROC proc)
{
	struct newresource *res;
	struct dlgcontext dctx;
	INT_PTR h = -1;

	res = getresource (templ);
	if (!res)
		return h;
	if (scaleresource (res, &dctx, hDlg, -1, 0, 0, -1)) {
		res->parent = panelresource;
		h = DialogBoxIndirect (res->inst, res->resource, hDlg, proc);
	}
	customDlgType = 0;
	customDlg = NULL;
	freescaleresource (res);
	return h;
}

static HWND getparent(HWND owner)
{
	for (;;) {
		if (GetWindowLongW(owner, GWL_STYLE) & WS_POPUP) {
			return owner;
		}
		owner = GetParent(owner);
		if (!owner || owner == GetDesktopWindow())
			break;
	}
	return NULL;
}

HWND CustomCreateDialog(int templ, HWND hDlg, DLGPROC proc, struct customdialogstate *cds)
{
	struct newresource *res;
	struct dlgcontext dctx;
	HWND h = NULL;

	memset(cds, 0, sizeof(customdialogstate));
	res = getresource(templ);
	if (!res)
		return h;
	HWND parent = getparent(hDlg);
	if (scaleresource(res, &dctx, hDlg, -1, 0, 0, -1)) {
		res->parent = panelresource;
		cds->parent = parent;
		if (parent) {
			EnableWindow(parent, FALSE);
		}
		h = x_CreateDialogIndirectParam(res->inst, res->resource, hDlg, proc, NULL, res);
		if (!h) {
			if (parent) {
				EnableWindow(parent, TRUE);
			}
			freescaleresource(res);
			res = NULL;
		}
	}
	if (h) {
		cds->res = res;
		cds->active = 1;
		cds->hwnd = h;
	}
	return h;
}

static int CustomCreateDialogBox(int templ, HWND hDlg, DLGPROC proc)
{
	struct customdialogstate prevstate;
	memcpy(&prevstate, &cdstate, sizeof(customdialogstate));

	memset(&cdstate, 0, sizeof(customdialogstate));
	cdstate.active = 1;
	HWND hwnd = CustomCreateDialog(templ, hDlg, proc, &cdstate);
	if (!hwnd) {
		return 0;
	}
	if (!IsWindowVisible(hwnd)) {
		ShowWindow(hwnd, SW_SHOW);
	}
	while (cdstate.active) {
		MSG msg;
		int ret;
		WaitMessage();
		while ((ret = GetMessage(&msg, NULL, 0, 0))) {
			if (ret == -1)
				break;
			if (!IsWindow(hwnd)) {
				break;
			}
			if (!IsDialogMessage(hwnd, &msg)) {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
			if (!IsWindow(hwnd)) {
				break;
			}
		}
	}
	int ret = cdstate.status;
	memcpy(&cdstate, &prevstate, sizeof(customdialogstate));
	return ret;
}

static int init_page (int tmpl, int icon, int title,
	INT_PTR (CALLBACK FAR *func) (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam), ACCEL *accels, const TCHAR *help, int focusid)
{
	LPTSTR lpstrTitle;
	static int id = 0;
	struct newresource *res;

	res = getresource (tmpl);
	if (!res) {
		write_log (_T("init_page(%d) failed\n"), tmpl);
		return -1;
	}
	ppage[id].nres = res;
	ppage[id].icon = MAKEINTRESOURCE (icon);
	if (title >= 0) {
		lpstrTitle = xcalloc (TCHAR, MAX_DPATH);
		LoadString (hUIDLL, title, lpstrTitle, MAX_DPATH);
		ppage[id].title = lpstrTitle;
	}
	ppage[id].dlgproc = func;
	ppage[id].help = help;
	ppage[id].idx = id;
	ppage[id].accel = NULL;
	ppage[id].focusid = focusid;
	if (accels) {
		int i = -1;
		while (accels[++i].key);
		ppage[id].accel = CreateAcceleratorTable (accels, i);
	}
	if (tmpl == IDD_FRONTEND)
		ppage[id].fullpanel = TRUE;
	id++;
	return id - 1;
}

static RECT dialog_rect;

static void dialogmousemove (HWND hDlg)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	static int newmx, newmy;
	RECT rc;
	POINT pt;
	static POINT pt2;
	int dx, dy;
	int sw, sh;
	int xstart, ystart;
	MONITORINFOEX pmi;

	if (!dodialogmousemove ())
		return;
	pmi.cbSize = sizeof (pmi);
	GetMonitorInfo (MonitorFromWindow (mon->hAmigaWnd, MONITOR_DEFAULTTOPRIMARY), (LPMONITORINFO)&pmi);
	xstart = pmi.rcMonitor.left;
	ystart = pmi.rcMonitor.top;
	GetCursorPos (&pt);
	pt.x -= xstart;
	pt.y -= ystart;
	if (pt.x == pt2.x && pt.y == pt2.y)
		return;
	sw = pmi.rcMonitor.right - pmi.rcMonitor.left;
	sh = pmi.rcMonitor.bottom - pmi.rcMonitor.top;
	dx = dialog_x_offset;
	dy = dialog_y_offset;
	GetWindowRect (hDlg, &rc);
	rc.right -= rc.left;
	rc.bottom -= rc.top;
	rc.left = 0;
	rc.top = 0;

	//write_log (_T("SW=%d SH=%d %dx%d\n"), sw, sh, rc.right, rc.bottom);

	if (rc.right <= sw && rc.bottom <= sh)
		return;
	pt2.x = pt.x;
	pt2.y = pt.y;

	newmx = pt.x;
	newmy = pt.y;

	if (newmx >= sw - 1 && rc.right > sw)
		dx = sw - rc.right;
	if (newmx <= 1)
		dx = 0;
	if (newmy >= sh - 1 && rc.bottom > sh)
		dy = sh - rc.bottom;
	if (newmy <= 1)
		dy = 0;

	if (dx != dialog_x_offset || dy != dialog_y_offset) {
		dialog_x_offset = dx;
		dialog_y_offset = dy;
		dx += xstart;
		dy += ystart;
		SetWindowPos (hDlg, 0, dx, dy, 0, 0,
			SWP_NOOWNERZORDER | SWP_NOSIZE | SWP_NOACTIVATE | SWP_DEFERERASE);
	}
}

#if 0
static void blah(void)
{
	char *str1 = "äöåõãñëÿüïñç";
	TCHAR *str2 = _T("äöåõãñëÿüïñç");
	TCHAR *s1;
	char *s2;
	MessageBoxA(NULL, str1, "Test1 ANSI", MB_OK);
	s1 = au (str1);
	MessageBoxW(NULL, s1, _T("Test1 UNICODE"), MB_OK);

	MessageBoxW(NULL, str2, _T("Test2 UNICODE"), MB_OK);
	s2 = ua (str2);
	MessageBoxA(NULL, s2, "Test2 ANSI", MB_OK);
}
#endif

static int GetSettings (int all_options, HWND hwnd)
{
	static int init_called = 0;
	static int start_gui_width = -1;
	static int start_gui_height = -1;
	int psresult;
	HWND dhwnd;
	int first = 0;
	bool closed = false;

	gui_active++;
	timeend();

	full_property_sheet = all_options;
	allow_quit = all_options;
	pguiprefs = &currprefs;
	memset (&workprefs, 0, sizeof (struct uae_prefs));

	szNone = WIN32GUI_LoadUIString (IDS_NONE);
	memsize_names[0] = szNone.c_str();
	prefs_to_gui (&changed_prefs);

	if (!init_called) {
		first = 1;
		panelresource = getresource (IDD_PANEL);
		LOADSAVE_ID = init_page (IDD_LOADSAVE, IDI_FILE, IDS_LOADSAVE, LoadSaveDlgProc, NULL, _T("gui/configurations.htm"), IDC_CONFIGTREE);
		MEMORY_ID = init_page (IDD_MEMORY, IDI_MEMORY, IDS_MEMORY, MemoryDlgProc, NULL, _T("gui/ram.htm"), 0);
		EXPANSION_ID = init_page(IDD_EXPANSION, IDI_EXPANSION, IDS_EXPANSION, ExpansionDlgProc, NULL, _T("gui/rtgboard.htm"), 0);
		EXPANSION2_ID = init_page(IDD_EXPANSION2, IDI_EXPANSION, IDS_EXPANSION2, Expansion2DlgProc, NULL, _T("gui/expansions.htm"), 0);
		KICKSTART_ID = init_page (IDD_KICKSTART, IDI_MEMORY, IDS_KICKSTART, KickstartDlgProc, NULL, _T("gui/rom.htm"), 0);
		CPU_ID = init_page (IDD_CPU, IDI_CPU, IDS_CPU, CPUDlgProc, NULL, _T("gui/cpu.htm"), 0);
		DISPLAY_ID = init_page (IDD_DISPLAY, IDI_DISPLAY, IDS_DISPLAY, DisplayDlgProc, NULL, _T("gui/display.htm"), 0);
#if defined (GFXFILTER)
		HW3D_ID = init_page (IDD_FILTER, IDI_DISPLAY, IDS_FILTER, hw3dDlgProc, NULL, _T("gui/filter.htm"), 0);
#endif
		CHIPSET_ID = init_page (IDD_CHIPSET, IDI_CPU, IDS_CHIPSET, ChipsetDlgProc, NULL, _T("gui/chipset.htm"), 0);
		CHIPSET2_ID = init_page (IDD_CHIPSET2, IDI_CPU, IDS_CHIPSET2, ChipsetDlgProc2, NULL, _T("gui/advchipset.htm"), 0);
		SOUND_ID = init_page (IDD_SOUND, IDI_SOUND, IDS_SOUND, SoundDlgProc, NULL, _T("gui/sound.htm"), 0);
		FLOPPY_ID = init_page (IDD_FLOPPY, IDI_FLOPPY, IDS_FLOPPY, FloppyDlgProc, NULL, _T("gui/floppies.htm"), 0);
		DISK_ID = init_page (IDD_DISK, IDI_FLOPPY, IDS_DISK, SwapperDlgProc, SwapperAccel, _T("gui/diskswapper.htm"), IDC_DISKLIST);
#ifdef FILESYS
		HARDDISK_ID = init_page (IDD_HARDDISK, IDI_HARDDISK, IDS_HARDDISK, HarddiskDlgProc, HarddiskAccel, _T("gui/harddrives.htm"), 0);
#endif
		GAMEPORTS_ID = init_page (IDD_GAMEPORTS, IDI_GAMEPORTS, IDS_GAMEPORTS, GamePortsDlgProc, NULL, _T("gui/gameports.htm"), 0);
		IOPORTS_ID = init_page (IDD_IOPORTS, IDI_PORTS, IDS_IOPORTS, IOPortsDlgProc, NULL, _T("gui/ioports.htm"), 0);
		INPUT_ID = init_page (IDD_INPUT, IDI_INPUT, IDS_INPUT, InputDlgProc, NULL, _T("gui/input.htm"), IDC_INPUTLIST);
		MISC1_ID = init_page (IDD_MISC1, IDI_MISC1, IDS_MISC1, MiscDlgProc1, NULL, _T("gui/misc.htm"), 0);
		MISC2_ID = init_page (IDD_MISC2, IDI_MISC2, IDS_MISC2, MiscDlgProc2, NULL, _T("gui/extensions.htm"), 0);
#ifdef AVIOUTPUT
		AVIOUTPUT_ID = init_page (IDD_AVIOUTPUT, IDI_AVIOUTPUT, IDS_AVIOUTPUT, AVIOutputDlgProc, NULL, _T("gui/output.htm"), 0);
#endif
		PATHS_ID = init_page (IDD_PATHS, IDI_PATHS, IDS_PATHS, PathsDlgProc, NULL, _T("gui/paths.htm"), 0);
		QUICKSTART_ID = init_page (IDD_QUICKSTART, IDI_QUICKSTART, IDS_QUICKSTART, QuickstartDlgProc, NULL, _T("gui/quickstart.htm"), 0);
		ABOUT_ID = init_page (IDD_ABOUT, IDI_ABOUT, IDS_ABOUT, AboutDlgProc, NULL, NULL, 0);
		FRONTEND_ID = init_page (IDD_FRONTEND, IDI_QUICKSTART, IDS_FRONTEND, AboutDlgProc, NULL, NULL, 0);
		BOARD_ID = init_page(IDD_BOARDS, IDI_EXPANSION, IDS_BOARD, BoardsDlgProc, NULL, _T("gui/hardwareinfo.htm"), 0);
		C_PAGES = BOARD_ID + 1;

		init_called = 1;
		if (quickstart && !qs_override)
			currentpage = QUICKSTART_ID;
		else
			currentpage = LOADSAVE_ID;
	}

	int fmultx = 0, fmulty = 0;
	int resetcount = 0;
	bool boxart_reopen = false;
	int use_gui_control = gui_control > 0 ? 2 : -1;
	if (osk_status()) {
		use_gui_control = 2;
	}
	setdefaultguisize(0);
	getstoredguisize();
	scaleresource_setsize(-1, -1, -1);
	for (;;) {
		int v = 0;
		int regexists;
		regexists = regqueryint (NULL, _T("GUIResize"), &v);
		gui_fullscreen = 0;
		gui_resize_allowed = true;
		v = -1;
		regqueryint(NULL, _T("GUIDarkMode"), &v);
		gui_darkmode = v;
		InitializeDarkMode();
		v = 0;
		regqueryint(NULL, _T("GUIFullscreen"), &v);
		if (v) {
			gui_resize_allowed = false;
			gui_resize_enabled = true;
			gui_fullscreen = 1;
		}
		if (!gui_fullscreen && !gui_size_changed) {
			setdefaultguisize(0);
			getstoredguisize();
		}
		gui_size_changed = 0;
		if (!regexists) {
			scaleresource_setdefaults(hwnd);
			fmultx = 0;
			write_log(_T("GUI default size\n"));
			regsetint(NULL, _T("GUIResize"), 0);
			regsetint(NULL, _T("GUIFullscreen"), 0);
		} else {
			if (gui_width < MIN_GUI_INTERNAL_WIDTH || gui_width > 8192 || gui_height < MIN_GUI_INTERNAL_HEIGHT || gui_height > 8192) {
				scaleresource_setdefaults(hwnd);
				setdefaultguisize(resetcount > 0);
				resetcount++;
				fmultx = 0;
				write_log (_T("GUI size reset\n"));
			}
		}

		if (all_options || !configstore)
			CreateConfigStore (NULL, FALSE);

		dialogreturn = -1;
		hAccelTable = NULL;
		if (first)
			write_log (_T("Entering GUI idle loop\n"));

		memset(&gui_fullscreen_rect, 0, sizeof(RECT));
		if (gui_fullscreen) {
			gui_width = GetSystemMetrics(SM_CXSCREEN);
			gui_height = GetSystemMetrics(SM_CYSCREEN);
			if (isfullscreen() > 0 && currprefs.gfx_api < 2) {
				struct MultiDisplay *md = getdisplay(&currprefs, 0);
				int w = md->rect.right - md->rect.left;
				int h = md->rect.bottom - md->rect.top;
				write_log(_T("GUI Fullscreen, screen size %dx%d (%dx%d)\n"), w, h, start_gui_width, start_gui_height);
				if (w < (start_gui_width * 9 / 10) || h < (start_gui_height * 9 / 10)) {
					gui_width = start_gui_width;
					gui_height = start_gui_height;
					write_log(_T("GUI Fullscreen %dx%d, closing fullscreen.\n"), gui_width, gui_height);
					hwnd = currprefs.win32_notaskbarbutton ? hHiddenWnd : NULL;
					closed = true;
					close_windows(&AMonitors[0]);
				} else {
					gui_width = w;
					gui_height = h;
					write_log(_T("GUI Fullscreen %dx%d\n"), gui_width, gui_height);
				}
			} else {
				int x = 0, y = 0, w = 0, h = 0;
				regqueryint(NULL, _T("GUIPosX"), &x);
				regqueryint(NULL, _T("GUIPosY"), &y);
				regqueryint(NULL, _T("GUISizeX"), &w);
				regqueryint(NULL, _T("GUISizeY"), &h);
				POINT pt;
				pt.x = x + w / 2;
				pt.y = y + h / 2;
				HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
				MONITORINFO mi;
				mi.cbSize = sizeof(mi);
				if (GetMonitorInfo(mon, &mi)) {
					RECT *r = &mi.rcWork;
					gui_fullscreen_rect = *r;
					gui_width = r->right - r->left;
					gui_height = r->bottom - r->top;
				}
			}
			scaleresource_setsize(gui_width, gui_height, 1);
			int gw = gui_width;
			int gh = gui_height;
			getstoredguisize();
			gui_width = gw;
			gui_height = gh;
			scaleresource_setsize(gui_width, gui_height, 1);
		} else {
			scaleresource_setsize(gui_width, gui_height, 0);
		}

		if (hwnd != NULL)
			DragAcceptFiles(hwnd, TRUE);

		fmultx = 0;
		write_log (_T("Requested GUI size = %dx%d (%dx%d)\n"), gui_width, gui_height, workprefs.gfx_monitor[0].gfx_size.width, workprefs.gfx_monitor[0].gfx_size.height);
		if (dodialogmousemove () && isfullscreen() > 0) {
			if (gui_width >= workprefs.gfx_monitor[0].gfx_size.width || gui_height >= workprefs.gfx_monitor[0].gfx_size.height) {
				write_log (_T("GUI larger than screen, resize disabled\n"));
				gui_resize_allowed = false;
			}
		}

		panelresource->width = gui_width;
		panelresource->height = gui_height;
		freescaleresource(panelresource);
		scaleresource (panelresource, &maindctx, hwnd, gui_resize_enabled && gui_resize_allowed, gui_fullscreen, workprefs.win32_gui_alwaysontop || workprefs.win32_main_alwaysontop ? WS_EX_TOPMOST : 0, 0);
		HWND phwnd = hwnd;
		hGUIWnd = NULL;
		if (isfullscreen() == 0)
			phwnd = 0;
		if (isfullscreen() > 0 && currprefs.gfx_api > 1)
			phwnd = 0;
		dhwnd = x_CreateDialogIndirectParam(panelresource->inst, panelresource->resource, phwnd, DialogProc, NULL, panelresource);
		dialog_rect.top = dialog_rect.left = 0;
		dialog_rect.right = panelresource->width;
		dialog_rect.bottom = panelresource->height;
		psresult = 0;
		if (dhwnd != NULL) {
			int dw = GetSystemMetrics(SM_CXSCREEN);
			int dh = GetSystemMetrics(SM_CYSCREEN);
			MSG msg;
			DWORD v;
			int w = 0, h = 0;

			getguisize (dhwnd, &w, &h);
			write_log (_T("Got GUI size = %dx%d\n"), w, h);
			if (w < 100 || h < 100 || (w > 8192 && w > dw + 500) || (h > 8192 && h > dh + 500)) {
				write_log (_T("GUI size (%dx%d) out of range!\n"), w, h);
				scaleresource_setdefaults(hwnd);
				setdefaultguisize(resetcount > 0);
				SendMessage (dhwnd, WM_COMMAND, IDCANCEL, 0);
				fmultx = fmulty = 0;
				gui_size_changed = 10;
				resetcount++;
				goto gui_exit;
			}

			if (start_gui_width < 0) {
				start_gui_width = w;
				start_gui_height = h;
			}

			if (g_darkModeSupported && g_darkModeEnabled) {
				BOOL value = TRUE;
				DwmSetWindowAttribute(dhwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
			}
			setguititle (dhwnd);
			RedrawWindow(dhwnd, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
			ShowWindow (dhwnd, SW_SHOW);
			MapDialogRect (dhwnd, &dialog_rect);

			hGUIWnd = dhwnd;
			if (currentpage == LOADSAVE_ID) {
				// update boxart
				SendMessage(pages[LOADSAVE_ID], WM_USER + 1, 0, 0);
				boxart_reopen = false;
			}
			if (boxart_reopen) {
				reset_box_art_window();
				boxart_reopen = false;
			}
			if (devicechangetimer < 0) {
				SetTimer(dhwnd, 4, 2000, NULL);
			}
			if (use_gui_control > 1) {
				SetTimer(dhwnd, 8, 20, gui_control_cb);
			}
			
			for (;;) {
				if (!PeekMessage(&msg, NULL, 0, 0, 0)) {
					HANDLE IPChandle;
					IPChandle = geteventhandleIPC(globalipc);
					if (globalipc && IPChandle != INVALID_HANDLE_VALUE) {
						MsgWaitForMultipleObjects(1, &IPChandle, FALSE, INFINITE, QS_ALLINPUT);
						while (checkIPC(globalipc, &workprefs));
						if (quit_program == -UAE_QUIT)
							break;
					} else {
						WaitMessage();
					}
				}
				dialogmousemove(dhwnd);

				while ((v = PeekMessage (&msg, NULL, 0, 0, PM_REMOVE))) {
					if (dialogreturn >= 0)
						break;
					if (v == -1)
						continue;
					if (!IsWindow (dhwnd))
						continue;
					if (hAccelTable && panelDlg && !rawmode) {
						if (TranslateAccelerator (panelDlg, hAccelTable, &msg))
							continue;
					}
					if (rawmode) {
						if (msg.message == WM_INPUT) {
							handlerawinput (msg.hwnd, msg.message, msg.wParam, msg.lParam);
							continue;
						}
						// eat all accelerators
						if (msg.message == WM_KEYDOWN || msg.message == WM_MOUSEMOVE || msg.message == WM_MOUSEWHEEL
							|| msg.message == WM_MOUSEHWHEEL || msg.message == WM_LBUTTONDOWN)
							continue;
					}
					// IsDialogMessage() eats WM_INPUT messages?!?!
					if (!rawmode && IsDialogMessage (dhwnd, &msg))
						continue;
					TranslateMessage (&msg);
					DispatchMessage (&msg);
				}
				if (dialogreturn >= 0)
					break;
				if (gui_size_changed) {
					saveguisize();
					regsetint(NULL, _T("GUIResize"), gui_resize_enabled ? 1 : 0);
					regsetint(NULL, _T("GUIFullscreen"), gui_fullscreen > 0 ? 1 : 0);
					if (gui_size_changed < 10) {
						scaleresource_setsize(gui_width, gui_height, 0);
						rescaleresource(panelresource, true);
						gui_size_changed = 0;
						reset_box_art_window();
					} else {
						close_box_art_window();
						boxart_reopen = true;
					}
				}
				if (gui_size_changed >= 10) {
					SendMessage(dhwnd, WM_COMMAND, IDCANCEL, 0);
					scaleresource_setsize(-1, -1, -1);
				}
				if (gui_fullscreen < 0) {
					// reset after IDCANCEL which would save coordinates
					gui_fullscreen = 0;
				}
				if ((workprefs.win32_gui_control ? 1 : 0) != use_gui_control && use_gui_control < 2) {
					use_gui_control = workprefs.win32_gui_control;
					if (use_gui_control) {
						SetTimer(dhwnd, 8, 30, gui_control_cb);
					} else {
						KillTimer(dhwnd, 8);
					}
				}
			}
			psresult = dialogreturn;
		} else {
			static int count;
			count++;
			if (count > 4) {
				pre_gui_message (_T("GUI failed to open"));
				abort ();
			}
			setdefaultguisize(count >= 2);
			scaleresource_setdefaults(hwnd);
			gui_size_changed = 10;
		}
gui_exit:;
		dhwnd = NULL;
		hGUIWnd = NULL;
		freescaleresource(panelresource);
		if (!gui_size_changed)
			break;
		quit_program = 0;
	}

	if (use_gui_control > 0 && dhwnd) {
		KillTimer(dhwnd, 8);
	}

	hGUIWnd = NULL;
	if (quit_program) {
		psresult = -2;
	} else if (qs_request_reset && quickstart) {
		if (qs_request_reset & 4) {
			copy_prefs(&changed_prefs, &currprefs);
			uae_restart(&workprefs, -2, NULL);
		} else {
			uae_reset((qs_request_reset & 2) ? 1 : 0, 1);
		}
	}
	if (psresult > 0 && config_pathfilename[0]) {
		DISK_history_add(config_pathfilename, -1, HISTORY_CONFIGFILE, 0);
	}

	if (closed) {
		graphics_init(false);
	}

	qs_request_reset = 0;
	full_property_sheet = 0;
	gui_active--;
	timebegin();
	return psresult;
}

int gui_init (void)
{
	int ret;

	read_rom_list(false);
	prefs_to_gui(&changed_prefs);
	inputdevice_updateconfig(NULL, &workprefs);
	for (;;) {
		ret = GetSettings (1, currprefs.win32_notaskbarbutton ? hHiddenWnd : NULL);
		if (!restart_requested)
			break;
		restart_requested = 0;
	}
	return ret;
}

int gui_update (void)
{
	return 1;
}

void gui_exit (void)
{
	int i;

	for (i = 0; i < C_PAGES; i++) {
		if (ppage[i].accel)
			DestroyAcceleratorTable (ppage[i].accel);
		ppage[i].accel = NULL;
	}
	FreeConfigStore ();
#ifdef PARALLEL_PORT
	closeprinter (); // Bernd Roesch
#endif
}

void check_prefs_changed_gui (void)
{
}

void gui_disk_image_change (int unitnum, const TCHAR *name, bool writeprotected)
{
#ifdef RETROPLATFORM
	rp_disk_image_change (unitnum, name, writeprotected);
#endif
}

static void gui_flicker_led2 (int led, int unitnum, int status)
{
	static int resetcounter[LED_MAX];
	uae_s8 old;
	uae_s8 *p;

	if (led == LED_HD)
		p = &gui_data.hd;
	else if (led == LED_CD)
		p = &gui_data.cd;
	else if (led == LED_MD)
		p = &gui_data.md;
	else if (led == LED_NET)
		p = &gui_data.net;
	else
		return;
	old = *p;
	if (status < 0) {
		if (old < 0) {
			gui_led (led, -1, -1);
		} else {
			gui_led (led, 0, -1);
		}
		return;
	}
	if (status == 0 && old < 0) {
		*p = 0;
		resetcounter[led] = 0;
		gui_led (led, 0, -1);
		return;
	}
	if (status == 0) {
		resetcounter[led]--;
		if (resetcounter[led] > 0)
			return;
	}
#ifdef RETROPLATFORM
	if (unitnum >= 0) {
		if (led == LED_HD) {
			rp_hd_activity(unitnum, status ? 1 : 0, status == 2 ? 1 : 0);
		} else if (led == LED_CD) {
			rp_cd_activity(unitnum, status);
		}
	}
#endif
	*p = status;
	resetcounter[led] = 15;
	if (old != *p)
		gui_led (led, *p, -1);
}

void gui_flicker_led (int led, int unitnum, int status)
{
	if (led < 0) {
		gui_flicker_led2(LED_HD, 0, 0);
		gui_flicker_led2(LED_CD, 0, 0);
		if (gui_data.net >= 0)
			gui_flicker_led2(LED_NET, 0, 0);
		if (gui_data.md >= 0)
			gui_flicker_led2(LED_MD, 0, 0);
	} else {
		gui_flicker_led2(led, unitnum, status);
	}
}

void gui_fps (int fps, int lines, bool lace, int idle, int color)
{
	gui_data.fps = fps;
	gui_data.lines = lines;
	gui_data.lace = lace;
	gui_data.idle = idle;
	gui_data.fps_color = color;
	gui_led(LED_FPS, 0, -1);
	gui_led(LED_LINES, 0, -1);
	gui_led(LED_CPU, 0, -1);
	gui_led(LED_SND, (gui_data.sndbuf_status > 1 || gui_data.sndbuf_status < 0) ? 0 : 1, -1);
}

#define LED_STRING_WIDTH 40
void gui_led (int led, int on, int brightness)
{
	int monid = 0;
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct amigadisplay *ad = &adisplays[monid];
	WORD type;
	static TCHAR drive_text[NUM_LEDS * LED_STRING_WIDTH];
	static TCHAR dfx[4][300];
	TCHAR *ptr, *tt, *p;
	int pos = -1, j;
	int writing = 0, playing = 0, active2 = 0;
	int writeprotected = 0;
	int center = 0;

	indicator_leds (led, on);
#ifdef LOGITECHLCD
	lcd_update (led, on);
#endif
	if (D3D_led)
		D3D_led(led, on, brightness);
#ifdef RETROPLATFORM
	if (led >= LED_DF0 && led <= LED_DF3 && !gui_data.drives[led - LED_DF0].drive_disabled) {
		rp_floppy_track(led - LED_DF0, gui_data.drives[led - LED_DF0].drive_track);
		writing = gui_data.drives[led - LED_DF0].drive_writing;
	}
	rp_update_leds (led, on, brightness, writing);
#endif
	if (!mon->hStatusWnd)
		return;
	tt = NULL;
	if (led >= LED_DF0 && led <= LED_DF3) {
		pos = 9 + (led - LED_DF0);
		ptr = drive_text + pos * LED_STRING_WIDTH;
		if (gui_data.drives[led - 1].drive_disabled)
			_tcscpy (ptr, _T(""));
		else
			_stprintf (ptr , _T("%02d"), gui_data.drives[led - 1].drive_track);
		p = gui_data.drives[led - 1].df;
		j = uaetcslen(p) - 1;
		if (j < 0)
			j = 0;
		while (j > 0) {
			if (p[j - 1] == '\\' || p[j - 1] == '/')
				break;
			j--;
		}
		tt = dfx[led - 1];
		tt[0] = 0;
		if (uaetcslen(p + j) > 0)
			_stprintf (tt, _T("%s [CRC=%08X]"), p + j, gui_data.drives[led - 1].crc32);
		center = 1;
		if (gui_data.drives[led - 1].drive_writing)
			writing = 1;
		if (gui_data.drives[led - 1].floppy_protected)
			writeprotected = 1;
	} else if (led == LED_POWER) {
		pos = 4;
		ptr = _tcscpy(drive_text + pos * LED_STRING_WIDTH, _T("Power"));
		center = 1;
	} else if (led == LED_CAPS) {
		pos = 5;
		ptr = _tcscpy(drive_text + pos * LED_STRING_WIDTH, _T("Caps"));
		center = 1;
	} else if (led == LED_HD) {
		pos = 7;
		ptr = _tcscpy(drive_text + pos * LED_STRING_WIDTH, _T("HD"));
		center = 1;
		if (on > 1)
			writing = 1;
	} else if (led == LED_CD) {
		pos = 6;
		ptr = _tcscpy(drive_text + pos * LED_STRING_WIDTH, _T("CD"));
		center = 1;
		if (on >= 0) {
			if (on & LED_CD_AUDIO)
				playing = 1;
			else if (on & LED_CD_ACTIVE2)
				active2 = 1;
			on &= 1;
		}
	} else if (led == LED_NET) {
		pos = 8;
		ptr = _tcscpy(drive_text + pos * LED_STRING_WIDTH, _T("N"));
		center = 1;
		if (on > 1)
			writing = 1;
	} else if (led == LED_LINES) {
		pos = 3;
		ptr = drive_text + pos * LED_STRING_WIDTH;
		if (gui_data.fps_color < 2) {
			_stprintf(ptr, _T("%3d%c"), gui_data.lines, gui_data.lace ? 'i' : 'p');
		} else {
			_tcscpy(ptr, _T("----"));
		}
		on = 1;
	} else if (led == LED_FPS) {
		float fps = gui_data.fps / 10.0f;
		extern float p96vblank;
		pos = 2;
		ptr = drive_text + pos * LED_STRING_WIDTH;
		if (fps > 9999.9)
			fps = 9999.9;
		const TCHAR *rec = avioutput_requested ? _T("R") : _T("");
		if (gui_data.fps_color == 2) {
			_stprintf(ptr, _T("No Sync%s"), rec);
		} else if (gui_data.fps_color == 3) {
			_stprintf(ptr, _T("FPS: ---%s"), rec);
		} else if (fps < 1000) {
			if (ad->picasso_on)
				_stprintf (ptr, _T("%.1f%s[%.1f]"), p96vblank, rec, fps);
			else
				_stprintf (ptr, _T("FPS: %.1f%s"), fps, rec);
		} else {
			if (ad->picasso_on)
				_stprintf(ptr, _T("%.0f%s[%.0f]"), p96vblank, rec, fps);
			else
				_stprintf(ptr, _T("FPS: %.0f%s"), fps, rec);
		}
		if (gui_data.cpu_halted > 0) {
			_stprintf (ptr, _T("HALT%d%s"), gui_data.cpu_halted, rec);
			center = 1;
		}
		if (pause_emulation) {
			_tcscpy (ptr, _T("PAUSED"));
			center = 1;
		}
		on = 1;
	} else if (led == LED_CPU) {
		pos = 1;
		ptr = drive_text + pos * LED_STRING_WIDTH;
		if (pause_emulation)
			on = 0;
		else
			on = 1;
		bool m68klabelchange = false;
		const TCHAR *m68label = _T("CPU");
		ptr[0] = 0;
		p = ptr;
		if (is_ppc_cpu(&currprefs)) {
			_tcscat(ptr, _T("PPC: "));
			if (ppc_state == PPC_STATE_ACTIVE)
				_tcscat(ptr, _T("RUN"));
			else if (ppc_state == PPC_STATE_CRASH)
				_tcscat(ptr, _T("CRASH"));
			else if (ppc_state == PPC_STATE_SLEEP)
				_tcscat(ptr, _T("SLEEP"));
			else
				_tcscat(ptr, _T("STOP"));
			_tcscat(ptr, _T(" "));
			p = ptr + _tcslen(ptr);
			m68label = _T("68k");
			m68klabelchange = true;
		}
		int state = is_x86_cpu(&currprefs);
		if (state > 0) {
			_tcscat(ptr, _T("x86: "));
			if (state == X86_STATE_ACTIVE)
				_tcscat(ptr, _T("RUN"));
			else
				_tcscat(ptr, _T("STOP"));
			_tcscat(ptr, _T(" "));
			p = ptr + _tcslen(ptr);
			m68label = _T("68k");
			m68klabelchange = true;
		}
		if (gui_data.cpu_halted < 0) {
			if (!m68klabelchange)
				_tcscpy(p, _T("STOP"));
			else
				_tcscat(p, _T(" 68k: STOP"));
		} else {
			_stprintf(p, _T("%s: %.0f%%"), m68label, (double)((gui_data.idle) / 10.0));
		}
	} else if (led == LED_SND && gui_data.sndbuf_avail) {
		pos = 0;
		ptr = drive_text + pos * LED_STRING_WIDTH;
		if (gui_data.sndbuf_status < 3 && !pause_emulation && !sound_paused()) {
			_stprintf (ptr, _T("SND: %+.0f%%"), (double)((gui_data.sndbuf) / 10.0));
		} else {
			_tcscpy (ptr, _T("SND: -"));
			center = 1;
			on = 0;
		}
	} else if (led == LED_MD) {
		pos = 9 + 3;
		ptr = _tcscpy(drive_text + pos * LED_STRING_WIDTH, _T("NV"));
	}

	if (on < 0)
		return;

	type = SBT_OWNERDRAW;
	if (pos >= 0) {
		ptr[_tcslen(ptr) + 1] = 0;
		if (center)
			ptr[_tcslen(ptr) + 1] |= 1;
		if (on) {
			ptr[_tcslen(ptr) + 1] |= 2;
			type |= SBT_POPOUT;
		}
		if (writing)
			ptr[_tcslen(ptr) + 1] |= 4;
		if (playing)
			ptr[_tcslen(ptr) + 1] |= 8;
		if (active2)
			ptr[_tcslen(ptr) + 1] |= 16;
		if (writeprotected)
			ptr[_tcslen(ptr) + 1] |= 32;
		pos += window_led_joy_start;
		PostMessage(mon->hStatusWnd, SB_SETTEXT, (WPARAM)((pos + 1) | type), (LPARAM)ptr);
		if (tt != NULL)
			PostMessage(mon->hStatusWnd, SB_SETTIPTEXT, (WPARAM)(pos + 1), (LPARAM)tt);
	}
}

void gui_filename (int num, const TCHAR *name)
{
}

static int fsdialog (HWND *hwnd, DWORD *flags)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	if (gui_active) {
		*hwnd = guiDlg;
		*flags |= MB_SETFOREGROUND;
		return 0;
	}
	*hwnd = mon->hMainWnd;
	if (isfullscreen () <= 0)
		return 0;
	*hwnd = mon->hAmigaWnd;
	flipgui (true);
	*flags |= MB_SETFOREGROUND;
	*flags |= MB_TOPMOST;
	return 0;
}

int gui_message_multibutton (int flags, const TCHAR *format,...)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	TCHAR msg[2048];
	TCHAR szTitle[MAX_DPATH];
	va_list parms;
	int flipflop = 0;
	int fullscreen = 0;
	int focuso = isfocus();
	int ret;
	DWORD mbflags;
	HWND hwnd;

	mbflags = MB_ICONWARNING | MB_TASKMODAL;
	if (flags == 0)
		mbflags |= MB_OK;
	else if (flags == 1)
		mbflags |= MB_YESNO;
	else if (flags == 2)
		mbflags |= MB_YESNOCANCEL;

	flipflop = fsdialog (&hwnd, &mbflags);
	if (!gui_active) {
		pause_sound ();
		if (flipflop)
			ShowWindow(mon->hAmigaWnd, SW_MINIMIZE);
	}

	va_start (parms, format);
	_vsntprintf (msg, sizeof msg / sizeof (TCHAR), format, parms);
	va_end (parms);
	write_log (msg);
	if (msg[_tcslen (msg) - 1]!='\n')
		write_log (_T("\n"));

	WIN32GUI_LoadUIString (IDS_ERRORTITLE, szTitle, MAX_DPATH);

	ret = MessageBox (hwnd, msg, szTitle, mbflags);

	if (!gui_active) {
		flipgui (false);
		if (flipflop)
			ShowWindow(mon->hAmigaWnd, SW_RESTORE);
		reset_sound ();
		resume_sound ();
		setmouseactive(0, focuso > 0 ? 1 : 0);
	}
	if (ret == IDOK)
		return 0;
	if (ret == IDYES)
		return 1;
	if (ret == IDNO)
		return 2;
	if (ret == IDCANCEL)
		return -1;
	return 0;
}

void gui_message (const TCHAR *format,...)
{
	struct AmigaMonitor *mon = &AMonitors[0];
	TCHAR msg[2048];
	TCHAR szTitle[MAX_DPATH];
	va_list parms;
	int flipflop = 0;
	int fullscreen = 0;
	int focuso = isfocus();
	DWORD flags = MB_OK;
	HWND hwnd;

	va_start (parms, format);
	_vsntprintf (msg, sizeof msg / sizeof (TCHAR), format, parms);
	va_end (parms);

	if (full_property_sheet || rp_isactive()) {
		pre_gui_message (msg);
		return;
	}

	flipflop = fsdialog (&hwnd, &flags);
	if (!gui_active) {
		pause_sound ();
		if (flipflop)
			ShowWindow(mon->hAmigaWnd, SW_MINIMIZE);
		rawinput_release();
	}
	if (hwnd == NULL)
		flags |= MB_TASKMODAL;

	write_log (msg);
	if (msg[_tcslen (msg) - 1] != '\n')
		write_log (_T("\n"));

	WIN32GUI_LoadUIString (IDS_ERRORTITLE, szTitle, MAX_DPATH);

	if (!MessageBox (hwnd, msg, szTitle, flags))
		write_log (_T("MessageBox(%s) failed, err=%d\n"), msg, GetLastError ());

	if (!gui_active) {
		flipgui (false);
		if (flipflop)
			ShowWindow(mon->hAmigaWnd, SW_RESTORE);
		reset_sound ();
		resume_sound ();
		setmouseactive(0, focuso > 0 ? 1 : 0);
		rawinput_alloc();
	}
}

void gui_message_id (int id)
{
	TCHAR msg[MAX_DPATH];
	WIN32GUI_LoadUIString (id, msg, sizeof (msg) / sizeof (TCHAR));
	gui_message (msg);
}

void pre_gui_message (const TCHAR *format,...)
{
	TCHAR msg[2048];
	TCHAR szTitle[MAX_DPATH];
	va_list parms;

	va_start (parms, format);
	_vsntprintf (msg, sizeof msg / sizeof (TCHAR), format, parms);
	va_end (parms);
	write_log (msg);
	if (msg[_tcslen (msg) - 1] != '\n')
		write_log (_T("\n"));

	if (!rp_isactive()) {
		WIN32GUI_LoadUIString (IDS_ERRORTITLE, szTitle, MAX_DPATH);
		_tcscat (szTitle, BetaStr);
		MessageBox (guiDlg, msg, szTitle, MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);
	}

}

static const int transla[] = {
	NUMSG_NEEDEXT2, IDS_NUMSG_NEEDEXT2,
	NUMSG_NOROMKEY,IDS_NUMSG_NOROMKEY,
	NUMSG_NOROM,IDS_NUMSG_NOROM,
	NUMSG_KSROMCRCERROR,IDS_NUMSG_KSROMCRCERROR,
	NUMSG_KSROMREADERROR,IDS_NUMSG_KSROMREADERROR,
	NUMSG_NOEXTROM,IDS_NUMSG_NOEXTROM,
	NUMSG_MODRIP_NOTFOUND,IDS_NUMSG_MODRIP_NOTFOUND,
	NUMSG_MODRIP_FINISHED,IDS_NUMSG_MODRIP_FINISHED,
	NUMSG_MODRIP_SAVE,IDS_NUMSG_MODRIP_SAVE,
	NUMSG_KS68EC020,IDS_NUMSG_KS68EC020,
	NUMSG_KS68020,IDS_NUMSG_KS68020,
	NUMSG_KS68030,IDS_NUMSG_KS68030,
	NUMSG_ROMNEED,IDS_NUMSG_ROMNEED,
	NUMSG_EXPROMNEED,IDS_NUMSG_EXPROMNEED,
	NUMSG_NOZLIB,IDS_NUMSG_NOZLIB,
	NUMSG_STATEHD,IDS_NUMSG_STATEHD,
	NUMSG_OLDCAPS, IDS_NUMSG_OLDCAPS,
	NUMSG_NOCAPS, IDS_NUMSG_NOCAPS,
	NUMSG_KICKREP, IDS_NUMSG_KICKREP,
	NUMSG_KICKREPNO, IDS_NUMSG_KICKREPNO,
	NUMSG_KS68030PLUS, IDS_NUMSG_KS68030PLUS,
	NUMSG_NO_PPC, IDS_NUMSG_NO_PPC,
	NUMSG_UAEBOOTROM_PPC, IDS_NUMSG_UAEBOOTROM_PCC,
	NUMSG_NOMEMORY, IDS_NUMSG_NOMEMORY,
	NUMSG_INPUT_NONE, IDS_NONE2,
	-1
};

static int gettranslation (int msg)
{
	int i;

	i = 0;
	while (transla[i] >= 0) {
		if (transla[i] == msg)
			return transla[i + 1];
		i += 2;
	}
	return -1;
}

void notify_user (int msg)
{
	TCHAR tmp[MAX_DPATH];
	int c = 0;

	c = gettranslation (msg);
	if (c < 0)
		return;
	WIN32GUI_LoadUIString (c, tmp, MAX_DPATH);
	gui_message (tmp);
}

void notify_user_parms (int msg, const TCHAR *parms, ...)
{
	TCHAR msgtxt[MAX_DPATH];
	TCHAR tmp[MAX_DPATH];
	int c = 0;
	va_list parms2;

	c = gettranslation (msg);
	if (c < 0)
		return;
	WIN32GUI_LoadUIString (c, tmp, MAX_DPATH);
	va_start (parms2, parms);
	_vsntprintf (msgtxt, sizeof msgtxt / sizeof (TCHAR), tmp, parms2);
	gui_message (msgtxt);
	va_end (parms2);
}


int translate_message (int msg,	TCHAR *out)
{
	msg = gettranslation (msg);
	out[0] = 0;
	if (msg < 0)
		return 0;
	WIN32GUI_LoadUIString (msg, out, MAX_DPATH);
	return 1;
}

void gui_lock (void)
{
}

void gui_unlock (void)
{
}
