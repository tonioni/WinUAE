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
#include <dbt.h>

#include "resource"
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

#include "dxwrap.h"
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
#include "opengl.h"
#include "direct3d.h"
#include "akiko.h"
#include "cdtv.h"
#include "gfxfilter.h"
#include "driveclick.h"
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
#include "win32_uaenet.h"
#ifdef RETROPLATFORM
#include "rp.h"
#endif

#define ARCHIVE_STRING L"*.zip;*.7z;*.rar;*.lha;*.lzh;*.lzx"

#define DISK_FORMAT_STRING L"(*.adf;*.adz;*.gz;*.dms;*.fdi;*.ipf;*.exe)\0*.adf;*.adz;*.gz;*.dms;*.fdi;*.ipf;*.exe;*.ima;*.wrp;*.dsq;*.st;" ARCHIVE_STRING L"\0"
#define ROM_FORMAT_STRING L"(*.rom;*.roz)\0*.rom;*.roz;" ARCHIVE_STRING L"\0"
#define USS_FORMAT_STRING_RESTORE L"(*.uss)\0*.uss;*.gz;"  ARCHIVE_STRING L"\0"
#define USS_FORMAT_STRING_SAVE L"(*.uss)\0*.uss\0"
#define HDF_FORMAT_STRING L"(*.hdf;*.vhd;*.rdf;*.hdz;*.rdz)\0*.hdf;*.vhd;*.rdf;*.hdz;*.rdz\0"
#define INP_FORMAT_STRING L"(*.inp)\0*.inp\0"
#define  CD_FORMAT_STRING L"(*.cue;*.ccd;*.mds;*.iso)\0*.cue;*.ccd;*.mds;*.iso;" ARCHIVE_STRING L"\0"
#define CONFIG_HOST L"Host"
#define CONFIG_HARDWARE L"Hardware"

static wstring szNone;

static int allow_quit;
static int restart_requested;
static int full_property_sheet = 1;
static struct uae_prefs *pguiprefs;
struct uae_prefs workprefs;
static int currentpage;
static int qs_request_reset;
static int qs_override;
int gui_active;

extern HWND (WINAPI *pHtmlHelp)(HWND, LPCWSTR, UINT, LPDWORD);

#undef HtmlHelp
#ifndef HH_DISPLAY_TOPIC
#define HH_DISPLAY_TOPIC 0
#endif
#define HtmlHelp(a,b,c,d) if(pHtmlHelp) (*pHtmlHelp)(a,b,c,(LPDWORD)d); else \
{ TCHAR szMessage[MAX_DPATH]; WIN32GUI_LoadUIString (IDS_NOHELP, szMessage, MAX_DPATH); gui_message (szMessage); }

extern TCHAR help_file[MAX_DPATH];

extern int mouseactive;

TCHAR config_filename[MAX_DPATH] = L"";
static TCHAR stored_path[MAX_DPATH];

#define Error(x) MessageBox (NULL, (x), L"WinUAE Error", MB_OK)

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

static int quickstart_model = 0, quickstart_conf = 0, quickstart_compa = 1;
static int quickstart_floppy = 1, quickstart_cd = 0, quickstart_ntsc = 0;
static int quickstart_cdtype = 0;
static TCHAR quickstart_cddrive[16];
static int quickstart_ok, quickstart_ok_floppy;
static void addfloppytype (HWND hDlg, int n);
static void addfloppyhistory (HWND hDlg);
static void addfloppyhistory_2 (HWND hDlg, int n, int f_text, int type);
static void addcdtype (HWND hDlg, int id);
static void getfloppyname (HWND hDlg, int n, int cd, int f_text);

static int C_PAGES;
#define MAX_C_PAGES 30
static int LOADSAVE_ID = -1, MEMORY_ID = -1, KICKSTART_ID = -1, CPU_ID = -1,
	DISPLAY_ID = -1, HW3D_ID = -1, CHIPSET_ID = -1, CHIPSET2_ID = -1, SOUND_ID = -1, FLOPPY_ID = -1, DISK_ID = -1,
	HARDDISK_ID = -1, IOPORTS_ID = -1, GAMEPORTS_ID = -1, INPUT_ID = -1, INPUTMAP_ID = -1, MISC1_ID = -1, MISC2_ID = -1,
	AVIOUTPUT_ID = -1, PATHS_ID = -1, QUICKSTART_ID = -1, ABOUT_ID = -1, EXPANSION_ID = -1, FRONTEND_ID = -1;
static HWND pages[MAX_C_PAGES];
#define MAX_IMAGETOOLTIPS 10
static HWND guiDlg, panelDlg, ToolTipHWND;
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

static bool ischecked (HWND hDlg, DWORD id)
{
	return IsDlgButtonChecked (hDlg, id) == BST_CHECKED;
}
static void setchecked (HWND hDlg, DWORD id, bool checked)
{
	CheckDlgButton (hDlg, id, checked ? BST_CHECKED : BST_UNCHECKED);
}
static void setfocus (HWND hDlg, int id)
{
	SendMessage (hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem (hDlg, id), TRUE);
}
static void ew (HWND hDlg, DWORD id, int enable)
{
	HWND w = GetDlgItem (hDlg, id);
	if (!w)
		return;
	if (!enable && w == GetFocus ())
		SendMessage (hDlg, WM_NEXTDLGCTL, 0, FALSE);
	EnableWindow (w, !!enable);
}
static void hide (HWND hDlg, DWORD id, int hide)
{
	HWND w;
	if (id < 0)
		return;
	w = GetDlgItem (hDlg, id);
	if (!w)
		return;
	if (hide && w == GetFocus ())
		SendMessage (hDlg, WM_NEXTDLGCTL, 0, FALSE);
	ShowWindow (w, hide ? SW_HIDE : SW_SHOW);
}


static int CALLBACK BrowseForFolderCallback (HWND hwnd, UINT uMsg, LPARAM lp, LPARAM pData)
{
	TCHAR szPath[MAX_PATH];
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

	hr = -1;
	ret = 0;
	pSHCreateItemFromParsingName = (SHCREATEITEMFROMPARSINGNAME)GetProcAddress (
		GetModuleHandle (L"shell32.dll"), "SHCreateItemFromParsingName");
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

	hr = pfd->Show (opn->hwndOwner);
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
									opn->nFileOffset = pathfilename - ppath;
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

static void write_disk_history2 (int type)
{
	int i, j;
	TCHAR tmp[16];
	UAEREG *fkey;

	fkey = regcreatetree (NULL, type ? L"CDImageMRUList" : L"DiskImageMRUList");
	if (fkey == NULL)
		return;
	j = 1;
	for (i = 0; i <= MAX_PREVIOUS_FLOPPIES; i++) {
		TCHAR *s = DISK_history_get (i, type);
		if (s == 0 || _tcslen (s) == 0)
			continue;
		_stprintf (tmp, L"Image%02d", j);
		regsetstr (fkey, tmp, s);
		j++;
	}
	while (j <= MAX_PREVIOUS_FLOPPIES) {
		TCHAR *s = L"";
		_stprintf (tmp, L"Image%02d", j);
		regsetstr (fkey, tmp, s);
		j++;
	}
	regclosetree (fkey);
}
void write_disk_history (void)
{
	write_disk_history2 (HISTORY_FLOPPY);
	write_disk_history2 (HISTORY_CD);
}

void reset_disk_history (void)
{
	int i;

	for (i = 0; i < MAX_PREVIOUS_FLOPPIES; i++) {
		DISK_history_add (NULL, i, HISTORY_FLOPPY, 0);
		DISK_history_add (NULL, i, HISTORY_CD, 0);
	}
	write_disk_history ();
}

UAEREG *read_disk_history (int type)
{
	static int regread;
	TCHAR tmp2[1000];
	int size, size2;
	int idx, idx2;
	UAEREG *fkey;
	TCHAR tmp[1000];

	fkey = regcreatetree (NULL, type ? L"CDImageMRUList" : L"DiskImageMRUList");
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
				DISK_history_add (tmp2, idx2, type, TRUE);
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
	if (guiDlg == NULL)
		return;
	SendMessage (guiDlg, WM_COMMAND, ok ? IDOK : IDCANCEL, 0);
}

static int getcbn (HWND hDlg, int v, TCHAR *out, int len)
{
	LRESULT val = SendDlgItemMessage (hDlg, v, CB_GETCURSEL, 0, 0L);
	out[0] = 0;
	if (val == CB_ERR) {
		SendDlgItemMessage (hDlg, v, WM_GETTEXT, (WPARAM)len, (LPARAM)out);
		return 1;
	} else {
		val = SendDlgItemMessage (hDlg, v, CB_GETLBTEXT, (WPARAM)val, (LPARAM)out);
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

	fkey = regcreatetree (NULL, L"FavoritePaths");
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
			_stprintf (str, L"%s \"%s\"", fitem[i].value, fitem[i].path);
		_stprintf (key, L"PATH_ALL_%02d", idx + 1);
		idx++;
		regsetstr (fkey, key, str);
		xfree (fitem[i].value);
		xfree (fitem[i].path);
	}
	while (idx < MAXFAVORITES) {
		TCHAR key[100];
		_stprintf (key, L"PATH_ALL_%02d", idx + 1);
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
	mii.dwTypeData = L"Add New";
	mii.cch = _tcslen (mii.dwTypeData);
	mii.wID = 1000;
	InsertMenuItem (emenu, -1, TRUE, &mii);
	i = 0;
	while (fitem[i].type) {
		if (fitem[i].type == 1) {
			mii.fMask = MIIM_STRING | MIIM_ID;
			mii.fType = MFT_STRING;
			mii.fState = MFS_ENABLED;
			mii.wID = 1001 + i;
			_stprintf (newpath, L"Remove '%s'", fitem[i].value);
			mii.dwTypeData = newpath;
			mii.cch = _tcslen (mii.dwTypeData);
			InsertMenuItem (emenu, -1, TRUE, &mii);
		}
		i++;
	}

	mii.fMask = MIIM_STRING | MIIM_SUBMENU;
	mii.fType = MFT_STRING;
	mii.fState = MFS_ENABLED;
	mii.dwTypeData = L"Edit";
	mii.cch = _tcslen (mii.dwTypeData);
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
			mii.cch = _tcslen (mii.dwTypeData);
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
		mii.dwTypeData = L"[Directory scan]";
		mii.cch = _tcslen (mii.dwTypeData);
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
			mii.cch = _tcslen (mii.dwTypeData);
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
	static TCHAR notallowed[] = L"[]()_-#!{}=.,";
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
	_tcscat (mask, L"*.*");
	myd = my_opendir (path, mask);
	int cnt = 0;
	while (cnt < 30) {
		TCHAR tmp[MAX_DPATH], tmp2[MAX_DPATH];
		if (!my_readdir (myd, tmp))
			break;
		_tcscpy (tmp2, path);
		_tcscat (tmp2, L"\\");
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
		fkey = regcreatetree (NULL, L"FavoritePaths");
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
						_stprintf (tmp, L"[DF%c:]", i + '0');
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

static int bDragging = 0;
static HIMAGELIST hDragImageList;
static int drag_start (HWND hWnd, HWND hListView, LPARAM lParam)
{
	POINT p, pt;
	int bFirst, iPos, iHeight;
	HIMAGELIST hOneImageList, hTempImageList;
	IMAGEINFO imf;

	// You can set your customized cursor here
	p.x = 8;
	p.y = 8;
	// Ok, now we create a drag-image for all selected items
	bFirst = TRUE;
	iPos = ListView_GetNextItem(hListView, -1, LVNI_SELECTED);
	while (iPos != -1) {
		if (bFirst) {
			// For the first selected item,
			// we simply create a single-line drag image
			hDragImageList = ListView_CreateDragImage(hListView, iPos, &p);
			ImageList_GetImageInfo(hDragImageList, 0, &imf);
			iHeight = imf.rcImage.bottom;
			bFirst = FALSE;
		} else {
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
		}
		iPos = ListView_GetNextItem(hListView, iPos, LVNI_SELECTED);
	}

	// Now we can initialize then start the drag action
	ImageList_BeginDrag(hDragImageList, 0, 0, 0);

	pt = ((NM_LISTVIEW*) ((LPNMHDR)lParam))->ptAction;
	ClientToScreen(hListView, &pt);

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

#define MIN_CHIP_MEM 0
#define MAX_CHIP_MEM 6
#define MIN_FAST_MEM 0
#define MAX_FAST_MEM 4
#define MIN_SLOW_MEM 0
#define MAX_SLOW_MEM 4
#define MIN_Z3_MEM 0
#define MAX_Z3_MEM ((max_z3fastmem >> 20) < 512 ? 12 : ((max_z3fastmem >> 20) < 1024 ? 13 : ((max_z3fastmem >> 20) < 2048) ? 14 : ((max_z3fastmem >> 20) < 2560) ? 15 : ((max_z3fastmem >> 20) < 3072) ? 16 : 17))
#define MAX_Z3_CHIPMEM 7
#define MIN_P96_MEM 0
#define MAX_P96_MEM ((max_z3fastmem >> 20) < 512 ? 8 : ((max_z3fastmem >> 20) < 1024 ? 9 : ((max_z3fastmem >> 20) < 2048) ? 10 : 11))
#define MIN_MB_MEM 0
#define MAX_MB_MEM 7

#define MIN_M68K_PRIORITY 1
#define MAX_M68K_PRIORITY 16
#define MIN_CACHE_SIZE 0
#define MAX_CACHE_SIZE 8
#define MIN_REFRESH_RATE 1
#define MAX_REFRESH_RATE 10
#define MIN_SOUND_MEM 0
#define MAX_SOUND_MEM 6

struct romscandata {
	UAEREG *fkey;
	int got;
};

static struct romdata *scan_single_rom_2 (struct zfile *f)
{
	uae_u8 buffer[20] = { 0 };
	uae_u8 *rombuf;
	int cl = 0, size;
	struct romdata *rd = 0;

	zfile_fseek (f, 0, SEEK_END);
	size = zfile_ftell (f);
	zfile_fseek (f, 0, SEEK_SET);
	if (size > 524288 * 2)  {/* don't skip KICK disks or 1M ROMs */
		write_log (L"'%s': too big %d, ignored\n", zfile_getname(f), size);
		return 0;
	}
	zfile_fread (buffer, 1, 11, f);
	if (!memcmp (buffer, "KICK", 4)) {
		zfile_fseek (f, 512, SEEK_SET);
		if (size > 262144)
			size = 262144;
	} else if (!memcmp (buffer, "AMIROMTYPE1", 11)) {
		cl = 1;
		size -= 11;
	} else {
		zfile_fseek (f, 0, SEEK_SET);
	}
	rombuf = xcalloc (uae_u8, size);
	if (!rombuf)
		return 0;
	zfile_fread (rombuf, 1, size, f);
	if (cl > 0) {
		decode_cloanto_rom_do (rombuf, size, size);
		cl = 0;
	}
	if (!cl) {
		rd = getromdatabydata (rombuf, size);
		if (!rd && (size & 65535) == 0) {
			/* check byteswap */
			int i;
			for (i = 0; i < size; i+=2) {
				uae_u8 b = rombuf[i];
				rombuf[i] = rombuf[i + 1];
				rombuf[i + 1] = b;
			}
			rd = getromdatabydata (rombuf, size);
		}
	}
	if (!rd) {
		write_log (L"!: Name='%s':%d\nCRC32=%08X SHA1=%s\n",
			zfile_getname (f), size, get_crc32 (rombuf, size), get_sha1_txt (rombuf, size));
	} else {
		TCHAR tmp[MAX_DPATH];
		getromname (rd, tmp);
		write_log (L"*: %s:%d = %s\nCRC32=%08X SHA1=%s\n",
			zfile_getname (f), size, tmp, get_crc32 (rombuf, size), get_sha1_txt (rombuf, size));
	}
	xfree (rombuf);
	return rd;
}

static struct romdata *scan_single_rom (const TCHAR *path)
{
	struct zfile *z;
	TCHAR tmp[MAX_DPATH];
	struct romdata *rd;

	_tcscpy (tmp, path);
	rd = scan_arcadia_rom (tmp, 0);
	if (rd)
		return rd;
	rd = getromdatabypath (path);
	if (rd && rd->crc32 == 0xffffffff)
		return rd;
	z = zfile_fopen (path, L"rb", ZFD_NORMAL);
	if (!z)
		return 0;
	return scan_single_rom_2 (z);
}

static void abspathtorelative (TCHAR *name)
{
	if (!_tcsncmp (start_path_exe, name, _tcslen (start_path_exe)))
		memmove (name, name + _tcslen (start_path_exe), (_tcslen (name) - _tcslen (start_path_exe) + 1) * sizeof (TCHAR));
}

static int addrom (UAEREG *fkey, struct romdata *rd, const TCHAR *name)
{
	TCHAR tmp1[MAX_DPATH], tmp2[MAX_DPATH];

	_stprintf (tmp1, L"ROM_%03d", rd->id);
	if (rd->group) {
		TCHAR *p = tmp1 + _tcslen (tmp1);
		_stprintf (p, L"_%02d_%02d", rd->group >> 16, rd->group & 65535);
	}
	if (regexists (fkey, tmp1))
		return 0;
	getromname (rd, tmp2);
	if (name) {
		TCHAR name2[MAX_DPATH];
		_tcscpy (name2, name);
		_tcscat (tmp2, L" / \"");
		if (getregmode ())
			abspathtorelative (name2);
		_tcscat (tmp2, name2);
		_tcscat (tmp2, L"\"");
	}
	if (rd->crc32 == 0xffffffff) {
		if (rd->configname)
			_stprintf (tmp2, L":%s", rd->configname);
		else
			_stprintf (tmp2, L":ROM_%03d", rd->id);
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

	if (!_tcsicmp (ext, L"rom") ||  !_tcsicmp (ext, L"adf") || !_tcsicmp (ext, L"key")
		|| !_tcsicmp (ext, L"a500") || !_tcsicmp (ext, L"a1200") || !_tcsicmp (ext, L"a4000") || !_tcsicmp (ext, L"cd32"))
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

static bool infoboxdialogstate;
static HWND infoboxhwnd;
static INT_PTR CALLBACK InfoBoxDialogProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch(msg)
	{
	case WM_DESTROY:
		PostQuitMessage (0);
		infoboxdialogstate = false;
		return TRUE;
	case WM_CLOSE:
		DestroyWindow (hDlg);
		infoboxdialogstate = false;
	return TRUE;
	case WM_INITDIALOG:
	{
		HWND owner = GetParent (hDlg);
		if (!owner) {
			owner = GetDesktopWindow ();
			RECT ownerrc, merc;
			GetWindowRect (owner, &ownerrc);
			GetWindowRect (hDlg, &merc);
			SetWindowPos (hDlg, NULL,
				ownerrc.left + ((ownerrc.right - ownerrc.left) - (merc.right - merc.left)) /2,
				ownerrc.top + ((ownerrc.bottom - ownerrc.top) - (merc.bottom - merc.top)) / 2,
				0, 0,
				SWP_NOSIZE);
		}
		return TRUE;
	}
	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDCANCEL:
			infoboxdialogstate = false;
			DestroyWindow (hDlg);
			return TRUE;
		}
		break;
	}
	return FALSE;
}
static bool scan_rom_hook (const TCHAR *name, int line)
{
	MSG msg;
	if (!infoboxhwnd)
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
		SetWindowText (GetDlgItem (infoboxhwnd, line == 1 ? IDC_INFOBOX_TEXT1 : (line == 2 ? IDC_INFOBOX_TEXT2 : IDC_INFOBOX_TEXT3)), s ? s : name);
	}
	while (PeekMessage (&msg, infoboxhwnd, 0, 0, PM_REMOVE)) {
		if (!IsDialogMessage (infoboxhwnd, &msg)) {
			TranslateMessage (&msg);
			DispatchMessage (&msg);
		}
	}
	return infoboxdialogstate;
}

static int scan_rom_2 (struct zfile *f, void *vrsd)
{
	struct romscandata *rsd = (struct romscandata*)vrsd;
	const TCHAR *path = zfile_getname(f);
	const TCHAR *romkey = L"rom.key";
	struct romdata *rd;

	scan_rom_hook (NULL, 0);
	if (!isromext (path, true))
		return 0;
	rd = scan_single_rom_2 (f);
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

static int listrom (int *roms)
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
	TCHAR unavail[MAX_DPATH], avail[MAX_DPATH];
	TCHAR *p1, *p2;
	int *rp;
	int romtable[] = {
		5, 4, -1, -1, // A500 1.2
		6, 32, -1, -1, // A500 1.3
		7, -1, -1, // A500+
		8, 9, 10, -1, -1, // A600
		23, 24, -1, -1, // A1000
		11, 31, 15, -1, -1, // A1200
		59, 71, 61, -1, -1, // A3000
		16, 46, 31, 13, 12, -1, -1, // A4000
		18, -1, 19, -1, -1, // CD32
		20, 21, 22, -1, 6, 32, -1, -1, // CDTV
		49, 50, 51, -1, 5, 4, -1, -1, // ARCADIA
		46, 16, 17, 31, 13, 12, -1, -1, // highend, any 3.x A4000
		53, 54, 55, -1, -1, // A590/A2091
		//56, 57, -1, -1, // A4091
		0, 0, 0
	};

	WIN32GUI_LoadUIString (IDS_ROM_AVAILABLE, avail, sizeof (avail) / sizeof (TCHAR));
	WIN32GUI_LoadUIString (IDS_ROM_UNAVAILABLE, unavail, sizeof (avail) / sizeof (TCHAR));
	_tcscat (avail, L"\n");
	_tcscat (unavail, L"\n");
	p1 = L"A500 Boot ROM 1.2\0A500 Boot ROM 1.3\0A500+\0A600\0A1000\0A1200\0A3000\0A4000\0\nCD32\0CDTV\0Arcadia Multi Select\0High end WinUAE\0\nA590/A2091 SCSI Boot ROM\0\0";

	p = xmalloc (TCHAR, 100000);
	if (!p)
		return;
	WIN32GUI_LoadUIString (IDS_ROMSCANEND, p, 100);
	_tcscat (p, L"\n\n");

	rp = romtable;
	while(rp[0]) {
		int ok = 0;
		p2 = p1 + _tcslen (p1) + 1;
		_tcscat (p, L" ");
		_tcscat (p, p1); _tcscat (p, L": ");
		if (listrom (rp))
			ok = 1;
		while(*rp++ != -1);
		if (*rp != -1) {
			if (ok) {
				ok = 0;
				if (listrom (rp))
					ok = 1;
			}
			while(*rp++ != -1);
		}
		rp++;
		if (ok)
			_tcscat (p, avail); else _tcscat (p, unavail);
		p1 = p2;
	}

	pre_gui_message (p);
	free (p);
}

static int scan_roms_2 (UAEREG *fkey, const TCHAR *path, bool deepscan)
{
	TCHAR buf[MAX_DPATH];
	WIN32_FIND_DATA find_data;
	HANDLE handle;
	int ret;

	if (!path)
		return 0;
	write_log (L"ROM scan directory '%s'\n", path);
	_tcscpy (buf, path);
	_tcscat (buf, L"*.*");
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
	ret = scan_roms_2 (fkey, pathp, deepscan);
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

	if (recursive)
		return 0;
	recursive++;

	regdeletetree (NULL, L"DetectedROMs");
	fkey = regcreatetree (NULL, L"DetectedROMs");
	if (fkey == NULL)
		goto end;

	infoboxdialogstate = true;
	infoboxhwnd = NULL;
	if (!rp_isactive ()) {
		HWND hwnd = CreateDialog (hUIDLL ? hUIDLL : hInst, MAKEINTRESOURCE (IDD_INFOBOX), hDlg, InfoBoxDialogProc);
		if (!hwnd)
			goto end;
		infoboxhwnd = hwnd;
	}

	cnt = 0;
	ret = 0;
	for (i = 0; i < MAX_ROM_PATHS; i++)
		paths[i] = NULL;
	scan_rom_hook (NULL, 0);
	while (scan_rom_hook (NULL, 0)) {
		keys = get_keyring ();
		fetch_path (L"KickstartPath", path, sizeof path / sizeof (TCHAR));
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
				write_log (L"ROM scan: more keys found, restarting..\n");
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

	fkey2 = regcreatetree (NULL, L"DetectedROMS");
	if (fkey2) {
		id = 1;
		for (;;) {
			struct romdata *rd = getromdatabyid (id);
			if (!rd)
				break;
			if (rd->crc32 == 0xffffffff)
				addrom(fkey, rd, NULL);
			id++;
		}
		regclosetree (fkey2);
	}

end:
	if (infoboxhwnd) {
		HWND hwnd = infoboxhwnd;
		infoboxhwnd = NULL;
		DestroyWindow (hwnd);
		while (PeekMessage (&msg, 0, 0, 0, PM_REMOVE)) {
			TranslateMessage (&msg);
			DispatchMessage (&msg);
		}
	}
	read_rom_list ();
	if (show)
		show_rom_list ();

	regclosetree (fkey);
	recursive--;
	return ret;
}

struct ConfigStruct {
	TCHAR Name[MAX_DPATH];
	TCHAR Path[MAX_DPATH];
	TCHAR Fullpath[MAX_DPATH];
	TCHAR HostLink[MAX_DPATH];
	TCHAR HardwareLink[MAX_DPATH];
	TCHAR Description[CFG_DESCRIPTION_LENGTH];
	int Type, Directory;
	struct ConfigStruct *Parent, *Child;
	int host, hardware;
	HTREEITEM item;
	FILETIME t;
};

static TCHAR *configreg[] = { L"ConfigFile", L"ConfigFileHardware", L"ConfigFileHost" };
static TCHAR *configreg2[] = { L"", L"ConfigFileHardware_Auto", L"ConfigFileHost_Auto" };
static struct ConfigStruct **configstore;
static int configstoresize, configstoreallocated, configtype, configtypepanel;

static struct ConfigStruct *getconfigstorefrompath (TCHAR *path, TCHAR *out, int type)
{
	int i;
	for (i = 0; i < configstoresize; i++) {
		if (((configstore[i]->Type == 0 || configstore[i]->Type == 3) && type == 0) || (configstore[i]->Type == type)) {
			TCHAR path2[MAX_DPATH];
			_tcscpy (path2, configstore[i]->Path);
			_tcsncat (path2, configstore[i]->Name, MAX_DPATH);
			if (!_tcscmp (path, path2)) {
				_tcscpy (out, configstore[i]->Fullpath);
				_tcsncat (out, configstore[i]->Name, MAX_DPATH);
				return configstore[i];
			}
		}
	}
	return 0;
}

int target_cfgfile_load (struct uae_prefs *p, const TCHAR *filename, int type, int isdefault)
{
	int v, i, type2;
	int ct, ct2, size;
	TCHAR tmp1[MAX_DPATH], tmp2[MAX_DPATH];
	TCHAR fname[MAX_DPATH];

	_tcscpy (fname, filename);
	if (!zfile_exists (fname)) {
		fetch_configurationpath (fname, sizeof (fname) / sizeof (TCHAR));
		if (_tcsncmp (fname, filename, _tcslen (fname)))
			_tcscat (fname, filename);
		else
			_tcscpy (fname, filename);
	}

	if (!isdefault)
		qs_override = 1;
	if (type < 0) {
		type = 0;
		cfgfile_get_description (fname, NULL, NULL, NULL, &type);
	}
	if (type == 0 || type == 1) {
		discard_prefs (p, 0);
	}
	type2 = type;
	if (type == 0) {
		default_prefs (p, type);
#if 0
		if (isdefault == 0) {
			fetch_configurationpath (tmp1, sizeof (tmp1) / sizeof (TCHAR));
			_tcscat (tmp1, OPTIONSFILENAME);
			cfgfile_load (p, tmp1, NULL, 0, 0);
		}
#endif
	}
		
	regqueryint (NULL, L"ConfigFile_NoAuto", &ct2);
	v = cfgfile_load (p, fname, &type2, ct2, isdefault ? 0 : 1);
	if (!v)
		return v;
	if (type > 0)
		return v;
	for (i = 1; i <= 2; i++) {
		if (type != i) {
			size = sizeof (ct);
			ct = 0;
			regqueryint (NULL, configreg2[i], &ct);
			if (ct && ((i == 1 && p->config_hardware_path[0] == 0) || (i == 2 && p->config_host_path[0] == 0) || ct2)) {
				size = sizeof (tmp1) / sizeof (TCHAR);
				regquerystr (NULL, configreg[i], tmp1, &size);
				fetch_path (L"ConfigurationPath", tmp2, sizeof (tmp2) / sizeof (TCHAR));
				_tcscat (tmp2, tmp1);
				v = i;
				cfgfile_load (p, tmp2, &v, 1, 0);
			}
		}
	}
	v = 1;
	return v;
}

static int gui_width = 640, gui_height = 480;

static int mm = 0;
static void m (void)
{
	write_log (L"%d:0: %dx%d %dx%d %dx%d\n", mm, currprefs.gfx_size.width, currprefs.gfx_size.height,
		workprefs.gfx_size.width, workprefs.gfx_size.height, changed_prefs.gfx_size.width, changed_prefs.gfx_size.height);
	write_log (L"%d:1: %dx%d %dx%d %dx%d\n", mm, currprefs.gfx_size_fs.width, currprefs.gfx_size_fs.height,
		workprefs.gfx_size_fs.width, workprefs.gfx_size_fs.height, changed_prefs.gfx_size_fs.width, changed_prefs.gfx_size_fs.height);
	mm++;
}

static int GetSettings (int all_options, HWND hwnd);
/* if drive is -1, show the full GUI, otherwise file-requester for DF[drive] */
void gui_display (int shortcut)
{
	static int here;
	HRESULT hr;
	int w, h;

	if (here)
		return;
	here++;
	gui_active++;
	setpaused (9);
	screenshot_prepare ();
#ifdef D3D
	D3D_guimode (TRUE);
#endif
	inputdevice_unacquire ();
	clearallkeys ();
	setmouseactive (0);

	w = h = -1;
	if (!WIN32GFX_IsPicassoScreen () && currprefs.gfx_afullscreen && (currprefs.gfx_size.width < gui_width || currprefs.gfx_size.height < gui_height)) {
		w = currprefs.gfx_size.width;
		h = currprefs.gfx_size.height;
	}
	if (WIN32GFX_IsPicassoScreen () && currprefs.gfx_pfullscreen && (picasso96_state.Width < gui_width || picasso96_state.Height < gui_height)) {
		w = currprefs.gfx_size.width;
		h = currprefs.gfx_size.height;
	}
	scaleresource_setmaxsize (-1, -1);
	if (w > 0 && h > 0)
		scaleresource_setmaxsize (w, h);
	manual_painting_needed++; /* So that WM_PAINT will refresh the display */

	if (isfullscreen () > 0) {
		hr = DirectDraw_FlipToGDISurface ();
		if (FAILED (hr))
			write_log (L"FlipToGDISurface failed, %s\n", DXError (hr));
	}

	flush_log ();

	if (shortcut == -1) {
		int ret;
		ret = GetSettings (0, hAmigaWnd);
		if (!ret) {
			savestate_state = 0;
		}
	} else if (shortcut >= 0 && shortcut < 4) {
		DiskSelection (hAmigaWnd, IDC_DF0 + shortcut, 0, &changed_prefs, 0);
	} else if (shortcut == 5) {
		if (DiskSelection (hAmigaWnd, IDC_DOSAVESTATE, 9, &changed_prefs, 0))
			save_state (savestate_fname, L"Description!");
	} else if (shortcut == 4) {
		if (DiskSelection (hAmigaWnd, IDC_DOLOADSTATE, 10, &changed_prefs, 0))
			savestate_state = STATE_DORESTORE;
	}
	manual_painting_needed--; /* So that WM_PAINT doesn't need to use custom refreshing */
	resumepaused (9);
	inputdevice_copyconfig (&changed_prefs, &currprefs);
	inputdevice_config_change_test ();
	clearallkeys ();
	inputdevice_acquire (TRUE);
	setmouseactive (1);
#ifdef D3D
	D3D_guimode (FALSE);
#endif
#ifdef AVIOUTPUT
	AVIOutput_Begin ();
#endif
	fpscounter_reset ();
	screenshot_free ();
	write_disk_history ();
	gui_active--;
	here--;
}

static void prefs_to_gui (struct uae_prefs *p)
{
	workprefs = *p;
	/* filesys hack */
	workprefs.mountitems = currprefs.mountitems;
	memcpy (&workprefs.mountconfig, &currprefs.mountconfig, MOUNT_CONFIG_SIZE * sizeof (struct uaedev_config_info));

	updatewinfsmode (&workprefs);
#if 0
#ifdef _DEBUG
	if (workprefs.gfx_framerate < 5)
		workprefs.gfx_framerate = 5;
#endif
#endif
}

static void gui_to_prefs (void)
{
	/* Always copy our prefs to changed_prefs, ... */
	changed_prefs = workprefs;
	/* filesys hack */
	currprefs.mountitems = changed_prefs.mountitems;
	memcpy (&currprefs.mountconfig, &changed_prefs.mountconfig, MOUNT_CONFIG_SIZE * sizeof (struct uaedev_config_info));
	fixup_prefs (&changed_prefs);
	updatewinfsmode (&changed_prefs);
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
	{ 0x05aa5db2, 0x470b, 0x4725, { 0x96, 0x03, 0xee, 0x61, 0x30, 0xfc, 0x54, 0x99 } }
};

static void getfilter (int num, TCHAR *name, int *filter, TCHAR *fname)
{
	_tcscpy (fname, name);
	_tcscat (fname, L"_Filter");
	regqueryint (NULL, fname, &filter[num]);
}
static void setfilter (int num, int *filter, TCHAR *fname)
{
	regsetint (NULL, fname, filter[num]);
}

static UINT_PTR CALLBACK ofnhook (HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HWND hWnd;
	RECT windowRect;
	int width, height, w2, h2, x, y;
	struct MultiDisplay *md;
	NMHDR *nmhdr;

	if (message == WM_NOTIFY) {
		nmhdr = (LPNMHDR)lParam;
		if (nmhdr->code == CDN_INITDONE) {
			write_log (L"OFNHOOK CDN_INITDONE\n");
			PostMessage (hDlg, WM_USER + 1, 0, 0);
			// OFN_ENABLESIZING enabled: SetWindowPos() only works once here...
		}
		return FALSE;
	} else if (message != WM_USER + 1) {
		return FALSE;
	}
	write_log (L"OFNHOOK POST\n");
	hWnd = GetParent (hDlg);
	md = getdisplay (&currprefs);
	if (!md)
		return FALSE;
	w2 = WIN32GFX_GetWidth ();
	h2 = WIN32GFX_GetHeight ();
	write_log (L"MOVEWINDOW %dx%d %dx%d (%dx%d)\n", md->rect.left, md->rect.top, md->rect.right, md->rect.bottom, w2, h2);
	windowRect.left = windowRect.right = windowRect.top = windowRect.bottom = -1;
	GetWindowRect (hWnd, &windowRect);
	width = windowRect.right - windowRect.left;
	height = windowRect.bottom - windowRect.top;
	write_log (L"%dx%d %dx%d\n", windowRect.left, windowRect.top, windowRect.right, windowRect.bottom);
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
	write_log (L"X=%d Y=%d W=%d H=%d\n", x, y, width, height);
	SetWindowPos (hWnd, NULL, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
	return FALSE;
}

static void eject_cd (void)
{
	workprefs.cdslots[0].name[0] = 0;
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

static void ejectfloppy (int n)
{
	if (iscd (n)) {
		eject_cd ();
	} else {
		workprefs.floppyslots[n].df[0] = 0;
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
int DiskSelection_2 (HWND hDlg, WPARAM wParam, int flag, struct uae_prefs *prefs, TCHAR *path_out, int *multi)
{
	static int previousfilter[20];
	TCHAR filtername[MAX_DPATH] = L"";
	OPENFILENAME openFileName;
	TCHAR full_path[MAX_DPATH] = L"";
	TCHAR full_path2[MAX_DPATH];
	TCHAR file_name[MAX_DPATH] = L"";
	TCHAR init_path[MAX_DPATH] = L"";
	BOOL result = FALSE;
	TCHAR *amiga_path = NULL, *initialdir = NULL, *defext = NULL;
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
			getfilter (flag, L"FloppyPath", previousfilter, filtername);
			fetch_path (L"FloppyPath", init_path, sizeof (init_path) / sizeof (TCHAR));
			guid = &diskselectionguids[0];
			break;
		case 2:
		case 3:
			getfilter (flag, L"hdfPath", previousfilter, filtername);
			fetch_path (L"hdfPath", init_path, sizeof (init_path) / sizeof (TCHAR));
			guid = &diskselectionguids[1];
			break;
		case 6:
		case 7:
		case 11:
			getfilter (flag, L"KickstartPath", previousfilter, filtername);
			fetch_path (L"KickstartPath", init_path, sizeof (init_path) / sizeof (TCHAR));
			guid = &diskselectionguids[2];
			break;
		case 4:
		case 5:
		case 8:
			getfilter (flag, L"ConfigurationPath", previousfilter, filtername);
			fetch_path (L"ConfigurationPath", init_path, sizeof (init_path) / sizeof (TCHAR));
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
					getfilter(flag, L"StatefilePath", previousfilter, filtername);
					fetch_path (L"StatefilePath", init_path, sizeof (init_path) / sizeof (TCHAR));
				}
				guid = &diskselectionguids[4];
			}
			break;
		case 15:
		case 16:
			getfilter (flag, L"InputPath", previousfilter, filtername);
			fetch_path (L"InputPath", init_path, sizeof (init_path) / sizeof (TCHAR));
			guid = &diskselectionguids[5];
			break;
		case 17:
			getfilter (flag, L"CDPath", previousfilter, filtername);
			fetch_path (L"CDPath", init_path, sizeof (init_path) / sizeof (TCHAR));
			guid = &diskselectionguids[6];
			break;
		}
	}

	szFilter[0] = 0;
	szFilter[1] = 0;
	switch (flag) {
	case 0:
		WIN32GUI_LoadUIString (IDS_SELECTADF, szTitle, MAX_DPATH);
		WIN32GUI_LoadUIString (IDS_ADF, szFormat, MAX_DPATH);
		_stprintf (szFilter, L"%s ", szFormat);
		memcpy (szFilter + _tcslen (szFilter), DISK_FORMAT_STRING, sizeof (DISK_FORMAT_STRING) + sizeof (TCHAR));
		defext = L"adf";
		break;
	case 1:
		WIN32GUI_LoadUIString (IDS_CHOOSEBLANK, szTitle, MAX_DPATH);
		WIN32GUI_LoadUIString (IDS_ADF, szFormat, MAX_DPATH);
		_stprintf (szFilter, L"%s ", szFormat);
		memcpy (szFilter + _tcslen (szFilter), L"(*.adf)\0*.adf\0", 15 * sizeof (TCHAR));
		defext = L"adf";
		break;
	case 2:
	case 3:
		WIN32GUI_LoadUIString (IDS_SELECTHDF, szTitle, MAX_DPATH);
		WIN32GUI_LoadUIString (IDS_HDF, szFormat, MAX_DPATH);
		_stprintf (szFilter, L"%s ", szFormat);
		memcpy (szFilter + _tcslen (szFilter),  HDF_FORMAT_STRING, sizeof (HDF_FORMAT_STRING) + sizeof (TCHAR));
		defext = L"hdf";
		break;
	case 4:
	case 5:
		WIN32GUI_LoadUIString (IDS_SELECTUAE, szTitle, MAX_DPATH);
		WIN32GUI_LoadUIString (IDS_UAE, szFormat, MAX_DPATH );
		_stprintf (szFilter, L"%s ", szFormat);
		memcpy (szFilter + _tcslen (szFilter), L"(*.uae)\0*.uae\0", 15 * sizeof (TCHAR));
		defext = L"uae";
		break;
	case 6:
		WIN32GUI_LoadUIString (IDS_SELECTROM, szTitle, MAX_DPATH);
		WIN32GUI_LoadUIString (IDS_ROM, szFormat, MAX_DPATH);
		_stprintf (szFilter, L"%s ", szFormat);
		memcpy (szFilter + _tcslen (szFilter), ROM_FORMAT_STRING, sizeof (ROM_FORMAT_STRING) + sizeof (TCHAR));
		defext = L"rom";
		break;
	case 7:
		WIN32GUI_LoadUIString (IDS_SELECTKEY, szTitle, MAX_DPATH);
		WIN32GUI_LoadUIString (IDS_KEY, szFormat, MAX_DPATH);
		_stprintf (szFilter, L"%s ", szFormat);
		memcpy (szFilter + _tcslen (szFilter), L"(*.key)\0*.key\0", 15 * sizeof (TCHAR));
		defext = L"key";
		break;
	case 15:
	case 16:
		WIN32GUI_LoadUIString (flag == 15 ? IDS_RESTOREINP : IDS_SAVEINP, szTitle, MAX_DPATH);
		WIN32GUI_LoadUIString (IDS_INP, szFormat, MAX_DPATH);
		_stprintf (szFilter, L"%s ", szFormat);
		memcpy (szFilter + _tcslen (szFilter), INP_FORMAT_STRING, sizeof (INP_FORMAT_STRING) + sizeof (TCHAR));
		defext = L"inp";
		break;
	case 9:
	case 10:
		WIN32GUI_LoadUIString (flag == 10 ? IDS_RESTOREUSS : IDS_SAVEUSS, szTitle, MAX_DPATH);
		WIN32GUI_LoadUIString (IDS_USS, szFormat, MAX_DPATH);
		_stprintf (szFilter, L"%s ", szFormat);
		if (flag == 10) {
			memcpy (szFilter + _tcslen (szFilter), USS_FORMAT_STRING_RESTORE, sizeof (USS_FORMAT_STRING_RESTORE) + sizeof (TCHAR));
			all = 1;
		} else {
			TCHAR tmp[MAX_DPATH];
			memcpy (szFilter + _tcslen (szFilter), USS_FORMAT_STRING_SAVE, sizeof (USS_FORMAT_STRING_SAVE) + sizeof (TCHAR));
			p = szFilter;
			while (p[0] != 0 || p[1] !=0 ) p++;
			p++;
			WIN32GUI_LoadUIString (IDS_STATEFILE_UNCOMPRESSED, tmp, sizeof (tmp) / sizeof (TCHAR));
			_tcscat (p, tmp);
			_tcscat (p, L" (*.uss)");
			p += _tcslen (p) + 1;
			_tcscpy (p, L"*.uss");
			p += _tcslen (p) + 1;
			WIN32GUI_LoadUIString (IDS_STATEFILE_RAMDUMP, tmp, sizeof (tmp) / sizeof (TCHAR));
			_tcscat (p, tmp);
			_tcscat (p, L" (*.dat)");
			p += _tcslen (p) + 1;
			_tcscpy (p, L"*.dat");
			p += _tcslen (p) + 1;
			WIN32GUI_LoadUIString (IDS_STATEFILE_WAVE, tmp, sizeof (tmp) / sizeof (TCHAR));
			_tcscat (p, tmp);
			_tcscat (p, L" (*.wav)");
			p += _tcslen (p) + 1;
			_tcscpy (p, L"*.wav");
			p += _tcslen (p) + 1;
			*p = 0;
			all = 0;
		}
		defext = L"uss";
		break;
	case 11:
		WIN32GUI_LoadUIString (IDS_SELECTFLASH, szTitle, MAX_DPATH);
		WIN32GUI_LoadUIString (IDS_FLASH, szFormat, MAX_DPATH);
		_stprintf (szFilter, L"%s ", szFormat);
		memcpy (szFilter + _tcslen (szFilter), L"(*.nvr)\0*.nvr\0", 15 * sizeof (TCHAR));
		defext = L"nvr";
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
		_tcscpy (szTitle, L"Select supported archive file");
		_stprintf (szFilter, L"%s (%s)", L"Archive", ARCHIVE_STRING);
		_tcscpy (szFilter + _tcslen (szFilter) + 1, ARCHIVE_STRING);
		initialdir = path_out;
		break;
	case 17:
		WIN32GUI_LoadUIString (IDS_SELECTCD, szTitle, MAX_DPATH);
		WIN32GUI_LoadUIString (IDS_CD, szFormat, MAX_DPATH);
		_stprintf (szFilter, L"%s ", szFormat);
		memcpy (szFilter + _tcslen (szFilter), CD_FORMAT_STRING, sizeof (CD_FORMAT_STRING) + sizeof (TCHAR));
		defext = L"cue";
		break;
	}
	if (all) {
		p = szFilter;
		while (p[0] != 0 || p[1] !=0) p++;
		p++;
		_tcscpy (p, L"All files (*.*)");
		p += _tcslen (p) + 1;
		_tcscpy (p, L"*.*");
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
	if (flag == 1 || flag == 3 || flag == 5 || flag == 9 || flag == 16) {
		if (!(result = GetSaveFileName_2 (hDlg, &openFileName, guid)))
			write_log (L"GetSaveFileNameX() failed, err=%d.\n", GetLastError ());
	} else {
		if (!(result = GetOpenFileName_2 (hDlg, &openFileName, guid)))
			write_log (L"GetOpenFileNameX() failed, err=%d.\n", GetLastError ());
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
			_stprintf (full_path, L"%s\\%s", full_path2, nextp);
			nextp += _tcslen (nextp) + 1;
		}
		switch (wParam)
		{
		case IDC_PATH_NAME:
		case IDC_PATH_FILESYS:
			if (flag == 8) {
				if(_tcsstr (full_path, L"Configurations\\")) {
					_tcscpy (full_path, init_path);
					_tcscat (full_path, file_name);
				}
			}
			SetDlgItemText (hDlg, wParam, full_path);
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
				TCHAR disk_name[32];
				disk_name[0] = 0; disk_name[31] = 0;
				GetDlgItemText (hDlg, IDC_CREATE_NAME, disk_name, 30);
				disk_creatediskfile (full_path, 0, (drive_type)SendDlgItemMessage (hDlg, IDC_FLOPPYTYPE, CB_GETCURSEL, 0, 0L), disk_name, ischecked (hDlg, IDC_FLOPPY_FFS), ischecked (hDlg, IDC_FLOPPY_BOOTABLE));
			}
			break;
		case IDC_CREATE_RAW:
			TCHAR disk_name[32];
			disk_name[0] = 0; disk_name[31] = 0;
			GetDlgItemText (hDlg, IDC_CREATE_NAME, disk_name, 30);
			disk_creatediskfile (full_path, 1, (drive_type)SendDlgItemMessage (hDlg, IDC_FLOPPYTYPE, CB_GETCURSEL, 0, 0L), disk_name, ischecked (hDlg, IDC_FLOPPY_FFS), ischecked (hDlg, IDC_FLOPPY_BOOTABLE));
			break;
		case IDC_LOAD:
			if (target_cfgfile_load (&workprefs, full_path, 0, 0) == 0) {
				TCHAR szMessage[MAX_DPATH];
				WIN32GUI_LoadUIString (IDS_COULDNOTLOADCONFIG, szMessage, MAX_DPATH);
				pre_gui_message (szMessage);
			} else {
				SetDlgItemText (hDlg, IDC_EDITDESCRIPTION, workprefs.description);
				SetDlgItemText (hDlg, IDC_EDITNAME, full_path);
				SetDlgItemText (hDlg, IDC_CONFIGLINK, workprefs.config_host_path);
			}
			break;
		case IDC_SAVE:
			SetDlgItemText (hDlg, IDC_EDITNAME, full_path);
			cfgfile_save (&workprefs, full_path, 0);
			break;
		case IDC_ROMFILE:
			_tcscpy (workprefs.romfile, full_path);
			fullpath (workprefs.romfile, MAX_DPATH);
			break;
		case IDC_ROMFILE2:
			_tcscpy (workprefs.romextfile, full_path);
			fullpath (workprefs.romextfile, MAX_DPATH);
			break;
		case IDC_FLASHFILE:
			_tcscpy (workprefs.flashfile, full_path);
			fullpath (workprefs.flashfile, MAX_DPATH);
			break;
		case IDC_CARTFILE:
			_tcscpy (workprefs.cartfile, full_path);
			fullpath (workprefs.cartfile, MAX_DPATH);
			break;
		case IDC_STATEREC_PLAY:
		case IDC_STATEREC_RECORD:
		case IDC_STATEREC_SAVE:
			_tcscpy (workprefs.inprecfile, full_path);
			_tcscpy (workprefs.inprecfile, full_path);
			break;
		}
		if (!nosavepath || 1) {
			if (flag == 0 || flag == 1) {
				amiga_path = _tcsstr (openFileName.lpstrFile, openFileName.lpstrFileTitle);
				if (amiga_path && amiga_path != openFileName.lpstrFile) {
					*amiga_path = 0;
					setdpath (L"FloppyPath", openFileName.lpstrFile);
				}
			} else if (flag == 2 || flag == 3) {
				amiga_path = _tcsstr (openFileName.lpstrFile, openFileName.lpstrFileTitle);
				if (amiga_path && amiga_path != openFileName.lpstrFile) {
					*amiga_path = 0;
					setdpath (L"hdfPath", openFileName.lpstrFile);
				}
			} else if (flag == 17) {
				amiga_path = _tcsstr (openFileName.lpstrFile, openFileName.lpstrFileTitle);
				if (amiga_path && amiga_path != openFileName.lpstrFile) {
					*amiga_path = 0;
					setdpath (L"CDPath", openFileName.lpstrFile);
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
int DiskSelection (HWND hDlg, WPARAM wParam, int flag, struct uae_prefs *prefs, TCHAR *path_out)
{
	return DiskSelection_2 (hDlg, wParam, flag, prefs, path_out, NULL);
}
int MultiDiskSelection (HWND hDlg, WPARAM wParam, int flag, struct uae_prefs *prefs, TCHAR *path_out)
{
	int multi = 0;
	return DiskSelection_2 (hDlg, wParam, flag, prefs, path_out, &multi);
}
static int loopmulti (TCHAR *s, TCHAR *out)
{
	static int index;

	if (!out) {
		index = _tcslen (s) + 1;
		return 1;
	}
	if (index < 0)
		return 0;
	if (!s[index]) {
		if (s[_tcslen (s) + 1] == 0) {
			_tcscpy (out, s);
			index = -1;
			return 1;
		}
		return 0;
	}
	_stprintf (out, L"%s\\%s", s, s + index);
	index += _tcslen (s + index) + 1;
	return 1;
}

static BOOL CreateHardFile (HWND hDlg, UINT hfsizem, TCHAR *dostype, TCHAR *newpath, TCHAR *outpath)
{
	HANDLE hf;
	int i = 0;
	BOOL result = FALSE;
	LONG highword = 0;
	DWORD ret, written;
	TCHAR init_path[MAX_DPATH] = L"";
	uae_u64 hfsize;
	uae_u32 dt;
	uae_u8 b;
	int sparse, dynamic;

	outpath[0] = 0;
	sparse = 0;
	dynamic = 0;
	dt = 0;
	hfsize = (uae_u64)hfsizem * 1024 * 1024;
	if (ischecked (hDlg, IDC_HF_SPARSE))
		sparse = 1;
	if (ischecked (hDlg, IDC_HF_DYNAMIC)) {
		dynamic = 1;
		sparse = 0;
	}
	if (!DiskSelection (hDlg, IDC_PATH_NAME, 3, &workprefs, newpath))
		return FALSE;
	GetDlgItemText (hDlg, IDC_PATH_NAME, init_path, MAX_DPATH);
	if (*init_path && hfsize) {
		if (dynamic) {
			if (!_stscanf (dostype, L"%x", &dt))
				dt = 0;
			if (_tcslen (init_path) > 4 && !_tcsicmp (init_path + _tcslen (init_path) - 4, L".hdf"))
				_tcscpy (init_path + _tcslen (init_path) - 4, L".vhd");
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
					write_log (L"SetFilePointer() failure for %s to posn %ud\n", init_path, hfsize);
				else
					result = SetEndOfFile (hf);
				SetFilePointer (hf, 0, NULL, FILE_BEGIN);
				b = 0;
				WriteFile (hf, &b, 1, &written, NULL);
				WriteFile (hf, &b, 1, &written, NULL);
				WriteFile (hf, &b, 1, &written, NULL);
				WriteFile (hf, &b, 1, &written, NULL);
				if (_stscanf (dostype, L"%x", &dt) > 0) {
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
				write_log (L"CreateFile() failed to create %s\n", init_path);
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

static const TCHAR *memsize_names[] = {
	/* 0 */ szNone.c_str(),
	/* 1 */ L"256 K",
	/* 2 */ L"512 K",
	/* 3 */ L"1 MB",
	/* 4 */ L"2 MB",
	/* 5 */ L"4 MB",
	/* 6 */ L"8 MB",
	/* 7 */ L"16 MB",
	/* 8 */ L"32 MB",
	/* 9 */ L"64 MB",
	/* 10*/ L"128 MB",
	/* 11*/ L"256 MB",
	/* 12*/ L"512 MB",
	/* 13*/ L"1 GB",
	/* 14*/ L"1.5MB",
	/* 15*/ L"1.8MB",
	/* 16*/ L"2 GB",
	/* 17*/ L"384 MB",
	/* 18*/ L"768 MB",
	/* 19*/ L"1.5 GB",
	/* 20*/ L"2.5 GB",
	/* 21*/ L"3 GB"
};

static unsigned long memsizes[] = {
	/* 0 */ 0,
	/* 1 */ 0x00040000, /* 256K */
	/* 2 */ 0x00080000, /* 512K */
	/* 3 */ 0x00100000, /* 1M */
	/* 4 */ 0x00200000, /* 2M */
	/* 5 */ 0x00400000, /* 4M */
	/* 6 */ 0x00800000, /* 8M */
	/* 7 */ 0x01000000, /* 16M */
	/* 8 */ 0x02000000, /* 32M */
	/* 9 */ 0x04000000, /* 64M */
	/* 10*/ 0x08000000, //128M
	/* 11*/ 0x10000000, //256M
	/* 12*/ 0x20000000, //512M
	/* 13*/ 0x40000000, //1GB
	/* 14*/ 0x00180000, //1.5MB
	/* 15*/ 0x001C0000, //1.8MB
	/* 16*/ 0x80000000, //2GB
	/* 17*/ 0x18000000, //384M
	/* 18*/ 0x30000000, //768M
	/* 19*/ 0x60000000, //1.5GB
	/* 20*/ 0xA8000000, //2.5GB
	/* 21*/ 0xC0000000, //3GB
};

static int msi_chip[] = { 1, 2, 3, 14, 4, 5, 6 };
static int msi_bogo[] = { 0, 2, 3, 14, 15 };
static int msi_fast[] = { 0, 3, 4, 5, 6 };
static int msi_z3fast[] = { 0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 17, 12, 18, 13, 19, 16, 20, 21 };
static int msi_z3chip[] = { 0, 7, 8, 9, 10, 11, 12, 13 };
static int msi_gfx[] = { 0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };

static int CalculateHardfileSize (HWND hDlg)
{
	BOOL Translated = FALSE;
	UINT mbytes = 0;

	mbytes = GetDlgItemInt(hDlg, IDC_HF_SIZE, &Translated, FALSE);
	if (mbytes <= 0)
		mbytes = 0;
	if( !Translated )
		mbytes = 0;
	return mbytes;
}

static const TCHAR *nth[] = {
	L"", L"second ", L"third ", L"fourth ", L"fifth ", L"sixth ", L"seventh ", L"eighth ", L"ninth ", L"tenth "
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
		GetWindowText (hwnd, title, sizeof (title) / sizeof (TCHAR));
		if (_tcslen (WINUAEBETA) > 0) {
			_tcscat (title, BetaStr);
			if (_tcslen (WINUAEEXTRA) > 0) {
				_tcscat (title, L" ");
				_tcscat (title, WINUAEEXTRA);
			}
		}
	}
	title2[0] = 0;
	name = config_filename;
	if (name && _tcslen (name) > 0) {
		_tcscat (title2, L"[");
		_tcscat (title2, name);
		_tcscat (title2, L"] - ");
	}
	_tcscat (title2, title);
	SetWindowText (hwnd, title2);
}


static void GetConfigPath (TCHAR *path, struct ConfigStruct *parent, int noroot)
{
	if (parent == NULL) {
		path[0] = 0;
		if (!noroot) {
			fetch_path (L"ConfigurationPath", path, MAX_DPATH);
		}
		return;
	}
	if (parent) {
		GetConfigPath (path, parent->Parent, noroot);
		_tcsncat (path, parent->Name, MAX_DPATH);
		_tcsncat (path, L"\\", MAX_DPATH);
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
	int i;
	for (i = 0; i < configstoresize; i++)
		FreeConfigStruct (configstore[i]);
	xfree (configstore);
	configstore = 0;
	configstoresize = configstoreallocated = 0;
}

static void getconfigcache (TCHAR *dst, const TCHAR *path)
{
	_tcscpy (dst, path);
	_tcsncat (dst, L"configuration.cache", MAX_DPATH);
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

static TCHAR configcachever[] = L"WinUAE Configuration.Cache";

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
	zcache = _tfopen (cachepath, L"r");
	if (zcache == NULL)
		return;
	fclose (zcache);
	bool hidden = my_isfilehidden (cachepath);
	my_setfilehidden (cachepath, false);
	zcache = _tfopen (cachepath, L"w+, ccs=UTF-8");
	if (zcache)
		fclose (zcache);
	my_setfilehidden (cachepath, hidden);
	write_log (L"'%s' flushed\n", cachepath);
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
			configstore = xrealloc (struct ConfigStruct*, configstore, configstoreallocated);
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
			_tcscat (cs->Path, L"\\");
			_tcscat (cs->Fullpath, L"\\");
		}

		if (!dirmode) {
			fgetsx (cs->HardwareLink, zcache);
			lines--;
			fgetsx (cs->HostLink, zcache);
			lines--;
			fgetsx (buf, zcache);
			lines--;
			cs->Type = _tstol (buf);
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
		write_log (L"'%s' load failed\n", cachepath);
		flushconfigcache (cachepath);
		FreeConfigStore ();
		return NULL;
	} else {
		write_log (L"'%s' loaded successfully\n", cachepath);
	}
	return first;
}

static void writeconfigcacheentry (FILE *zcache, const TCHAR *relpath, struct ConfigStruct *cs)
{
	TCHAR path2[MAX_DPATH];
	TCHAR lf = 10;
	TCHAR el[] = L";\n";
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
	_stprintf (path2, L"%I64u", li.QuadPart);
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
		_stprintf (path2, L"%d", cs->Type);
		fwrite (path2, _tcslen (path2), sizeof (TCHAR), zcache);
		fwrite (&lf, 1, sizeof (TCHAR), zcache);
	}

	fwrite (el, _tcslen (el), sizeof (TCHAR), zcache);
}

static void writeconfigcacherec (FILE *zcache, const TCHAR *relpath, struct ConfigStruct *cs)
{
	int i;

	if (!cs->Directory)
		return;
	writeconfigcacheentry (zcache, relpath, cs);
	for (i = 0; i < configstoresize; i++) {
		struct ConfigStruct *cs2 = configstore[i];
		if (cs2->Parent == cs)
			writeconfigcacherec (zcache, relpath, cs2);
	}
}

static void writeconfigcache (const TCHAR *path)
{
	int i;
	TCHAR lf = 10;
	FILE *zcache;
	TCHAR cachepath[MAX_DPATH];
	TCHAR path2[MAX_DPATH];
	FILETIME t;
	SYSTEMTIME st;

	if (!configurationcache)
		return;
	getconfigcache (cachepath, path);
	bool hidden = my_isfilehidden (cachepath);
	my_setfilehidden (cachepath, false);
	zcache = _tfopen (cachepath, L"w, ccs=UTF-8");
	if (!zcache)
		return;
	t.dwHighDateTime = t.dwLowDateTime = 0;
	GetSystemTime (&st);
	SystemTimeToFileTime (&st, &t);
	fwrite (configcachever, _tcslen (configcachever), sizeof (TCHAR), zcache);
	fwrite (&lf, 1, sizeof (TCHAR), zcache);
	_stprintf (path2, L"3\n4\n7\n%I64u\n;\n", t);
	fwrite (path2, _tcslen (path2), sizeof (TCHAR), zcache);
	GetFullPathName (path, sizeof path2 / sizeof (TCHAR), path2, NULL);
	for (i = 0; i < configstoresize; i++) {
		struct ConfigStruct *cs = configstore[i];
		if (cs->Directory && cs->Parent == NULL)
			writeconfigcacherec (zcache, path2, cs);
	}
	for (i = 0; i < configstoresize; i++) {
		struct ConfigStruct *cs = configstore[i];
		if (!cs->Directory)
			writeconfigcacheentry (zcache, path2, cs);
	}
	fclose (zcache);
	my_setfilehidden (cachepath, hidden);
	write_log (L"'%s' created\n", cachepath);
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
	_tcsncat (path2, L"*.*", MAX_DPATH);

	if (*level == 0) {
		if (flushcache) {
			TCHAR cachepath[MAX_DPATH];
			getconfigcache (cachepath, path);
			flushconfigcache (cachepath);
		}
		first = readconfigcache (path);
		if (first)
			return first; 
	}

	handle = FindFirstFile (path2, &find_data );
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
		if (_tcscmp (find_data.cFileName, L".") && _tcscmp (find_data.cFileName, L"..")) {
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
					_tcscat (config->Path, L"\\");
					_tcscpy (config->Fullpath, path);
					_tcscat (config->Fullpath, config->Name);
					_tcscat (config->Fullpath, L"\\");
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
				if (_tcslen (find_data.cFileName) > 4 && !strcasecmp (find_data.cFileName + _tcslen (find_data.cFileName) - 4, L".uae")) {
					_tcscpy (path3, path);
					_tcsncat (path3, find_data.cFileName, MAX_DPATH);
					if (cfgfile_get_description (path3, config->Description, config->HostLink, config->HardwareLink, &config->Type)) {
						_tcscpy (config->Name, find_data.cFileName);
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
				configstore = xrealloc (struct ConfigStruct*, configstore, configstoreallocated);
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
	return first;
}

static struct ConfigStruct *CreateConfigStore (struct ConfigStruct *oldconfig, int flushcache)
{
	int level, i;
	TCHAR path[MAX_DPATH], name[MAX_DPATH];
	struct ConfigStruct *cs;

	if (oldconfig) {
		_tcscpy (path, oldconfig->Path);
		_tcscpy (name, oldconfig->Name);
	}
	level = 0;
	GetConfigs (NULL, 1, &level, flushcache);
	if (oldconfig) {
		for (i = 0; i < configstoresize; i++) {
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

	full_path[0] = 0;
	name[0] = 0;
	desc[0] = 0;
	GetDlgItemText (hDlg, IDC_EDITNAME, name, MAX_DPATH);
	_tcscpy (config_filename, name);
	if (flag == CONFIG_SAVE_FULL || flag == CONFIG_SAVE) {
		if (_tcslen (name) < 4 || strcasecmp (name + _tcslen (name) - 4, L".uae")) {
			_tcscat (name, L".uae");
			SetDlgItemText (hDlg, IDC_EDITNAME, name);
		}
		if (config)
			_tcscpy (config->Name, name);
	}
	GetDlgItemText (hDlg, IDC_EDITDESCRIPTION, desc, MAX_DPATH);
	if (config) {
		_tcscpy (path, config->Fullpath);
	} else {
		fetch_configurationpath (path, sizeof (path) / sizeof (TCHAR));
	}
	_tcsncat (path, name, MAX_DPATH);
	_tcscpy (full_path, path);
	switch (flag)
	{
	case CONFIG_SAVE_FULL:
		DiskSelection(hDlg, IDC_SAVE, 5, &workprefs, newpath);
		break;

	case CONFIG_LOAD_FULL:
		DiskSelection(hDlg, IDC_LOAD, 4, &workprefs, newpath);
		EnableWindow(GetDlgItem (hDlg, IDC_VIEWINFO), workprefs.info[0]);
		break;

	case CONFIG_SAVE:
		if (_tcslen (name) == 0 || _tcscmp (name, L".uae") == 0) {
			TCHAR szMessage[MAX_DPATH];
			WIN32GUI_LoadUIString(IDS_MUSTENTERNAME, szMessage, MAX_DPATH);
			pre_gui_message (szMessage);
		} else {
			_tcscpy (workprefs.description, desc);
			cfgfile_save (&workprefs, path, configtypepanel);
		}
		break;

	case CONFIG_LOAD:
		if (_tcslen (name) == 0) {
			TCHAR szMessage[MAX_DPATH];
			WIN32GUI_LoadUIString (IDS_MUSTSELECTCONFIG, szMessage, MAX_DPATH);
			pre_gui_message (szMessage);
		} else {
			if (target_cfgfile_load (&workprefs, path, configtypepanel, 0) == 0) {
				TCHAR szMessage[MAX_DPATH];
				WIN32GUI_LoadUIString (IDS_COULDNOTLOADCONFIG, szMessage, MAX_DPATH);
				pre_gui_message (szMessage);
				config_filename[0] = 0;
			} else {
				ew (hDlg, IDC_VIEWINFO, workprefs.info[0]);
			}
		}
		break;

	case CONFIG_DELETE:
		if (_tcslen (name) == 0) {
			TCHAR szMessage[MAX_DPATH];
			WIN32GUI_LoadUIString (IDS_MUSTSELECTCONFIGFORDELETE, szMessage, MAX_DPATH);
			pre_gui_message (szMessage);
		} else {
			TCHAR szMessage[MAX_DPATH];
			TCHAR szTitle[MAX_DPATH];
			WIN32GUI_LoadUIString (IDS_DELETECONFIGCONFIRMATION, szMessage, MAX_DPATH);
			WIN32GUI_LoadUIString (IDS_DELETECONFIGTITLE, szTitle, MAX_DPATH );
			if (MessageBox (hDlg, szMessage, szTitle,
				MB_YESNO | MB_ICONWARNING | MB_APPLMODAL | MB_SETFOREGROUND) == IDYES) {
					cfgfile_backup (path);
					DeleteFile (path);
					write_log (L"deleted config '%s'\n", path);
					config_filename[0] = 0;
			}
		}
		break;
	}

	setguititle (NULL);
	return full_path;
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

static int input_selected_device, input_selected_widget, input_total_devices;
static int input_selected_event, input_selected_sub_num;

static void set_lventry_input (HWND list, int index)
{
	int flags, i, sub, port;
	TCHAR name[256];
	TCHAR custom[MAX_DPATH];
	TCHAR af[32], toggle[32];

	inputdevice_get_mapping (input_selected_device, index, &flags, &port, name, custom, input_selected_sub_num);
	if (flags & IDEV_MAPPED_AUTOFIRE_SET)
		WIN32GUI_LoadUIString (IDS_YES, af, sizeof af / sizeof (TCHAR));
	else if (flags & IDEV_MAPPED_AUTOFIRE_POSSIBLE)
		WIN32GUI_LoadUIString (IDS_NO, af, sizeof af / sizeof (TCHAR));
	else
		_tcscpy (af, L"-");
	if (flags & IDEV_MAPPED_TOGGLE)
		WIN32GUI_LoadUIString (IDS_YES, toggle, sizeof toggle / sizeof (TCHAR));
	else if (flags & IDEV_MAPPED_AUTOFIRE_POSSIBLE)
		WIN32GUI_LoadUIString (IDS_NO, toggle, sizeof toggle / sizeof (TCHAR));
	else
		_tcscpy (toggle, L"-");
	if (port > 0) {
		TCHAR tmp[256];
		_tcscpy (tmp, name);
		_stprintf (name, L"[PORT%d] %s", port, tmp);
	}
	ListView_SetItemText (list, index, 1, custom[0] ? custom : name);
	ListView_SetItemText (list, index, 2, af);
	ListView_SetItemText (list, index, 3, toggle);
	sub = 0;
	for (i = 0; i < MAX_INPUT_SUB_EVENT; i++) {
		if (inputdevice_get_mapping (input_selected_device, index, &flags, NULL, name, custom, i) || custom[0])
			sub++;
	}
	_stprintf (name, L"%d", sub);
	ListView_SetItemText (list, index, 4, name);
}

static void update_listview_input (HWND hDlg)
{
	int i;
	if (!input_total_devices)
		return;
	for (i = 0; i < inputdevice_get_widget_num (input_selected_device); i++)
		set_lventry_input (GetDlgItem (hDlg, IDC_INPUTLIST), i);
}

static int inputmap_port = -1, inputmap_port_remap = -1;
static int inputmap_handle (HWND list, int currentdevnum, int currentwidgetnum, int *inputmap_portp, int *inputmap_indexp, int state)
{
	int cntitem, cntgroup, portnum;
	int mode, *events, *axistable;
	bool found2 = false;

	for (portnum = 0; portnum < 4; portnum++) {
		if (list)
			portnum = inputmap_port;
		cntitem = 1;
		cntgroup = 1;
		if (inputdevice_get_compatibility_input (&workprefs, portnum, &mode, &events, &axistable)) {
			int evtnum;
			for (int i = 0; (evtnum = events[i]) >= 0; i++) {
				struct inputevent *evt = inputdevice_get_eventinfo (evtnum);
				LV_ITEM lvstruct;
				int devnum;
				int flags, status;
				TCHAR name[256];
				int *atp = axistable;
				int atpidx;
				int item;
				bool found = false;

				if (list) {
					LVGROUP group;
					group.cbSize = sizeof (LVGROUP);
					group.mask = LVGF_HEADER | LVGF_GROUPID;
					group.pszHeader = (TCHAR*)evt->name;
					group.iGroupId = cntgroup;
					ListView_InsertGroup (list, -1, &group);

					lvstruct.mask     = LVIF_TEXT | LVIF_PARAM | LVIF_GROUPID;
					lvstruct.lParam   = 0;
					lvstruct.iSubItem = 0;
					lvstruct.iGroupId = cntgroup;
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
									if (inputdevice_get_mapping (devnum, j, &flags, &port, NULL, NULL, sub) == evtnum) {
										if (!port)
											continue;
										inputdevice_get_widget_type (devnum, j, name);
										if (list) {
											TCHAR target[MAX_DPATH];
											_tcscpy (target, name);
											_tcscat (target, L", ");
											_tcscat (target, inputdevice_get_device_name2 (devnum));
											lvstruct.pszText = target;
											lvstruct.iItem = cntgroup * 256 + cntitem;
											item = ListView_InsertItem (list, &lvstruct);
										} else if (currentdevnum == devnum) {
											if (currentwidgetnum == j) {
												*inputmap_portp = portnum;
												*inputmap_indexp = cntitem - 1;
												found2 = true;
												if (state < 0)
													return 1;
												state = -1;
											}
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
						lvstruct.pszText = L"";
						lvstruct.iItem = cntgroup * 256 + cntitem;
						item = ListView_InsertItem (list, &lvstruct);
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
static void update_listview_inputmap (HWND hDlg)
{
	HWND list = GetDlgItem (hDlg, IDC_INPUTMAPLIST);

	ListView_EnableGroupView (list, TRUE);

	inputmap_handle (list, -1, -1, NULL, NULL, 0);
}

static int clicked_entry = -1;

#define LOADSAVE_COLUMNS 2
#define INPUT_COLUMNS 5
#define HARDDISK_COLUMNS 8
#define DISK_COLUMNS 3
#define MISC2_COLUMNS 2
#define INPUTMAP_COLUMNS 1
#define MAX_COLUMN_HEADING_WIDTH 20

#define LV_LOADSAVE 1
#define LV_HARDDISK 2
#define LV_INPUT 3
#define LV_DISK 4
#define LV_MISC2 5
#define LV_INPUTMAP 6
#define LV_MAX 7

static int lv_oldidx[LV_MAX];
static int lv_old_type = -1;

static int listview_num_columns;

void InitializeListView (HWND hDlg)
{
	int lv_type;
	HWND list;
	LV_ITEM lvstruct;
	LV_COLUMN lvcolumn;
	RECT rect;
	TCHAR column_heading[HARDDISK_COLUMNS][MAX_COLUMN_HEADING_WIDTH];
	TCHAR blocksize_str[6] = L"";
	TCHAR readwrite_str[10] = L"";
	TCHAR size_str[32] = L"";
	TCHAR volname_str[MAX_DPATH] = L"";
	TCHAR devname_str[MAX_DPATH] = L"";
	TCHAR bootpri_str[6] = L"";
	int width = 0;
	int items = 0, result = 0, i, j, entry = 0, temp = 0;
	TCHAR tmp[10], tmp2[MAX_DPATH];
	int listview_column_width[HARDDISK_COLUMNS];

	if (cachedlist) {
		if (lv_old_type >= 0)
			lv_oldidx[lv_old_type] = ListView_GetTopIndex (cachedlist);
			lv_oldidx[lv_old_type] += ListView_GetCountPerPage (cachedlist) - 1;
		cachedlist = NULL;
	}

	if (hDlg == pages[HARDDISK_ID]) {

		listview_num_columns = HARDDISK_COLUMNS;
		lv_type = LV_HARDDISK;
		_tcscpy (column_heading[0], L"*");
		WIN32GUI_LoadUIString (IDS_DEVICE, column_heading[1], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString (IDS_VOLUME, column_heading[2], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString (IDS_PATH, column_heading[3], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString (IDS_RW, column_heading[4], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString (IDS_BLOCKSIZE, column_heading[5], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString (IDS_HFDSIZE, column_heading[6], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString (IDS_BOOTPRI, column_heading[7], MAX_COLUMN_HEADING_WIDTH);
		list = GetDlgItem (hDlg, IDC_VOLUMELIST);

	} else if (hDlg == pages[INPUT_ID]) {

		listview_num_columns = INPUT_COLUMNS;
		lv_type = LV_INPUT;
		WIN32GUI_LoadUIString (IDS_INPUTHOSTWIDGET, column_heading[0], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString (IDS_INPUTAMIGAEVENT, column_heading[1], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString (IDS_INPUTAUTOFIRE, column_heading[2], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString (IDS_INPUTTOGGLE, column_heading[3], MAX_COLUMN_HEADING_WIDTH);
		_tcscpy (column_heading[4], L"#");
		list = GetDlgItem (hDlg, IDC_INPUTLIST);

	} else if (hDlg == pages[INPUTMAP_ID]) {

		listview_num_columns = INPUTMAP_COLUMNS;
		lv_type = LV_INPUTMAP;
		column_heading[0][0] = NULL;
		list = GetDlgItem (hDlg, IDC_INPUTMAPLIST);

	} else if (hDlg == pages[MISC2_ID]) {

		listview_num_columns = MISC2_COLUMNS;
		lv_type = LV_MISC2;
		_tcscpy (column_heading[0], L"Extension");
		_tcscpy (column_heading[1], L"");
		list = GetDlgItem (hDlg, IDC_ASSOCIATELIST);

	} else {

		listview_num_columns = DISK_COLUMNS;
		lv_type = LV_DISK;
		_tcscpy (column_heading[0], L"#");
		WIN32GUI_LoadUIString (IDS_DISK_IMAGENAME, column_heading[1], MAX_COLUMN_HEADING_WIDTH);
		WIN32GUI_LoadUIString (IDS_DISK_DRIVENAME, column_heading[2], MAX_COLUMN_HEADING_WIDTH);
		list = GetDlgItem (hDlg, IDC_DISK);

	}

	int flags = LVS_EX_ONECLICKACTIVATE | LVS_EX_UNDERLINEHOT | LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT;
	ListView_SetExtendedListViewStyleEx (list, flags , flags);
	ListView_DeleteAllItems (list);

	cachedlist = list;

	for(i = 0; i < listview_num_columns; i++)
		listview_column_width[i] = ListView_GetStringWidth (list, column_heading[i]) + 15;

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

	if (lv_type == LV_MISC2) {

		listview_column_width[0] = 180;
		listview_column_width[1] = 10;
		for (i = 0; exts[i].ext; i++) {
			lvstruct.mask     = LVIF_TEXT | LVIF_PARAM;
			lvstruct.pszText  = exts[i].ext;
			lvstruct.lParam   = 0;
			lvstruct.iItem    = i;
			lvstruct.iSubItem = 0;
			result = ListView_InsertItem (list, &lvstruct);
			ListView_SetItemText (list, result, 1, exts[i].enabled ? L"*" : L"");
		}
	} else if (lv_type == LV_INPUT) {

		for (i = 0; input_total_devices && i < inputdevice_get_widget_num (input_selected_device); i++) {
			TCHAR name[100];
			inputdevice_get_widget_type (input_selected_device, i, name);
			lvstruct.mask     = LVIF_TEXT | LVIF_PARAM;
			lvstruct.pszText  = name;
			lvstruct.lParam   = 0;
			lvstruct.iItem    = i;
			lvstruct.iSubItem = 0;
			result = ListView_InsertItem (list, &lvstruct);
			width = ListView_GetStringWidth (list, lvstruct.pszText) + 15;
			if (width > listview_column_width[0])
				listview_column_width[0] = width;
			entry++;
		}
		listview_column_width[1] = 260;
		listview_column_width[2] = 65;
		listview_column_width[3] = 65;
		listview_column_width[4] = 30;
		update_listview_input (hDlg);

	} else if (lv_type == LV_INPUTMAP) {

		listview_column_width[0] = 400;
		update_listview_inputmap (hDlg);

	} else if (lv_type == LV_DISK) {

		for (i = 0; i < MAX_SPARE_DRIVES; i++) {
			int drv;
			_stprintf (tmp, L"%d", i + 1);
			lvstruct.mask     = LVIF_TEXT | LVIF_PARAM;
			lvstruct.pszText  = tmp;
			lvstruct.lParam   = 0;
			lvstruct.iItem    = i;
			lvstruct.iSubItem = 0;
			result = ListView_InsertItem (list, &lvstruct);
			_tcscpy (tmp2, workprefs.dfxlist[i]);
			j = _tcslen (tmp2) - 1;
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
				_stprintf (tmp, L"DF%d:", drv);
			ListView_SetItemText (list, result, 2, tmp);
			width = ListView_GetStringWidth (list, lvstruct.pszText) + 15;
			if (width > listview_column_width[0])
				listview_column_width[0] = width;
			entry++;
		}
		listview_column_width[0] = 30;
		listview_column_width[1] = 336;
		listview_column_width[2] = 50;

	} else if (lv_type == LV_HARDDISK) {
#ifdef FILESYS
		for(i = 0; i < workprefs.mountitems; i++)
		{
			struct uaedev_config_info *uci = &workprefs.mountconfig[i];
			int nosize = 0, type;
			struct mountedinfo mi;
			TCHAR *rootdir = uci->rootdir;

			type = get_filesys_unitconfig (&workprefs, i, &mi);
			if (type < 0) {
				type = uci->ishdf ? FILESYS_HARDFILE : FILESYS_VIRTUAL;
				nosize = 1;
			}

			if (nosize)
				_tcscpy (size_str, L"n/a");
			else if (mi.size >= 1024 * 1024 * 1024)
				_stprintf (size_str, L"%.1fG", ((double)(uae_u32)(mi.size / (1024 * 1024))) / 1024.0);
			else if (mi.size < 10 * 1024 * 1024)
				_stprintf (size_str, L"%dK", mi.size / 1024);
			else
				_stprintf (size_str, L"%.1fM", ((double)(uae_u32)(mi.size / (1024))) / 1024.0);

			if (uci->controller >= HD_CONTROLLER_IDE0 && uci->controller <= HD_CONTROLLER_IDE3) {
				_stprintf (blocksize_str, L"%d", uci->blocksize);
				_stprintf (devname_str, L"*IDE%d*", uci->controller - HD_CONTROLLER_IDE0);
				_tcscpy (volname_str, L"n/a");
				_tcscpy (bootpri_str, L"n/a");
			} else if (uci->controller >= HD_CONTROLLER_SCSI0 && uci->controller <= HD_CONTROLLER_SCSI6) {
				_stprintf (blocksize_str, L"%d", uci->blocksize);
				_stprintf (devname_str, L"*SCSI%d*", uci->controller - HD_CONTROLLER_SCSI0);
				_tcscpy (volname_str, L"n/a");
				_tcscpy (bootpri_str, L"n/a");
			} else if (uci->controller == HD_CONTROLLER_PCMCIA_SRAM) {
				_tcscpy (blocksize_str, L"n/a");
				_tcscpy(devname_str, L"*SCSRAM*");
				_tcscpy (volname_str, L"n/a");
				_tcscpy (bootpri_str, L"n/a");
			} else if (type == FILESYS_HARDFILE) {
				_stprintf (blocksize_str, L"%d", uci->blocksize);
				_tcscpy (devname_str, uci->devname);
				_tcscpy (volname_str, L"n/a");
				_stprintf (bootpri_str, L"%d", uci->bootpri);
			} else if (type == FILESYS_HARDFILE_RDB || type == FILESYS_HARDDRIVE || uci->controller) {
				_stprintf (blocksize_str, L"%d", uci->blocksize);
				_tcscpy (devname_str, L"n/a");
				_tcscpy (volname_str, L"n/a");
				_tcscpy (bootpri_str, L"n/a");
				if (!_tcsncmp (rootdir, L"HD_", 3))
					rootdir += 3;
			} else {
				_tcscpy (blocksize_str, L"n/a");
				_tcscpy (devname_str, uci->devname);
				_tcscpy (volname_str, uci->volname);
				_tcscpy (size_str, L"n/a");
				_stprintf (bootpri_str, L"%d", uci->bootpri);
			}
			if (!mi.ismedia) {
				_tcscpy (blocksize_str, L"n/a");
				_tcscpy (size_str, L"n/a");
			}
			WIN32GUI_LoadUIString (uci->readonly ? IDS_NO : IDS_YES, readwrite_str, sizeof (readwrite_str) / sizeof (TCHAR));

			lvstruct.mask     = LVIF_TEXT | LVIF_PARAM;
			lvstruct.pszText  = mi.ismedia == 0 ? L"E" : (nosize ? L"X" : (mi.ismounted ? L"*" : L" "));
			if (uci->controller)
				lvstruct.pszText = L" ";
			lvstruct.lParam   = 0;
			lvstruct.iItem    = i;
			lvstruct.iSubItem = 0;
			result = ListView_InsertItem (list, &lvstruct);
			if (result != -1) {

				listview_column_width[0] = 15;

				ListView_SetItemText(list, result, 1, devname_str);
				width = ListView_GetStringWidth(list, devname_str) + 10;
				if(width > listview_column_width[1])
					listview_column_width[1] = width;

				ListView_SetItemText(list, result, 2, volname_str);
				width = ListView_GetStringWidth(list, volname_str) + 10;
				if(width > listview_column_width[2])
					listview_column_width[2] = width;

				listview_column_width[3] = 150;
				ListView_SetItemText(list, result, 3, rootdir);
				width = ListView_GetStringWidth(list, rootdir) + 10;
				if(width > listview_column_width[3])
					listview_column_width[3] = width;

				ListView_SetItemText(list, result, 4, readwrite_str);
				width = ListView_GetStringWidth(list, readwrite_str) + 10;
				if(width > listview_column_width[4])
					listview_column_width[4] = width;

				ListView_SetItemText(list, result, 5, blocksize_str);
				width = ListView_GetStringWidth(list, blocksize_str) + 10;
				if(width > listview_column_width[5])
					listview_column_width[5] = width;

				ListView_SetItemText(list, result, 6, size_str);
				width = ListView_GetStringWidth(list, size_str) + 10;
				if(width > listview_column_width[6])
					listview_column_width[6] = width;

				ListView_SetItemText(list, result, 7, bootpri_str);
				width = ListView_GetStringWidth(list, bootpri_str) + 10;
				if(width > listview_column_width[7] )
					listview_column_width[7] = width;
			}
		}
#endif
	}
	if (result != -1) {
		if (GetWindowRect (list, &rect)) {
			ScreenToClient (hDlg, (LPPOINT)&rect);
			ScreenToClient (hDlg, (LPPOINT)&rect.right);
			if (listview_num_columns == 2) {
				if ((temp = rect.right - rect.left - listview_column_width[0] - 30) > listview_column_width[1])
					listview_column_width[1] = temp;
			}
		}
		// Adjust our column widths so that we can see the contents...
		for(i = 0; i < listview_num_columns; i++)
			ListView_SetColumnWidth (list, i, listview_column_width[i]);
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

}

static int listview_find_selected (HWND list)
{
	int i, items;
	items = ListView_GetItemCount (list);
	for (i = 0; i < items; i++) {
		if (ListView_GetItemState (list, i, LVIS_SELECTED) == LVIS_SELECTED)
			return i;
	}
	return -1;
}

static int listview_entry_from_click (HWND list, int *column)
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
				int i, x;
				UINT flag = LVIS_SELECTED | LVIS_FOCUSED;

				ListView_GetItemPosition (list, entry, &ppt);
				x = ppt.x;
				ListView_SetItemState (list, entry, flag, flag);
				for (i = 0; i < listview_num_columns && column; i++) {
					int cw = ListView_GetColumnWidth (list, i);
					if (x < point.x && x + cw > point.x) {
						*column = i;
						break;
					}
					x += cw;
				}
				return entry;
			}
		}
		entry++;
	}
	return -1;
}

static INT_PTR CALLBACK InfoSettingsProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;

	switch (msg)
	{
	case WM_INITDIALOG:
		recursive++;
		SetDlgItemText (hDlg, IDC_PATH_NAME, workprefs.info);
		recursive--;
		return TRUE;

	case WM_COMMAND:
		if (recursive)
			break;
		recursive++;

		switch(wParam)
		{
		case IDC_SELECTOR:
			DiskSelection (hDlg, IDC_PATH_NAME, 8, &workprefs, 0);
			break;
		case IDOK:
			EndDialog (hDlg, 1);
			break;
		case IDCANCEL:
			EndDialog (hDlg, 0);
			break;
		}

		GetDlgItemText(hDlg, IDC_PATH_NAME, workprefs.info, sizeof workprefs.info);
		recursive--;
		break;
	}
	return FALSE;
}

static HTREEITEM AddConfigNode (HWND hDlg, struct ConfigStruct *config, const TCHAR *name, const TCHAR *desc, const TCHAR *path, int isdir, int expand, HTREEITEM parent)
{
	TVINSERTSTRUCT is;
	HWND TVhDlg;
	TCHAR s[MAX_DPATH] = L"";
	TCHAR file_name[MAX_DPATH] = L"", file_path[MAX_DPATH] = L"";

	GetDlgItemText (hDlg, IDC_EDITNAME, file_name, MAX_DPATH);
	GetDlgItemText (hDlg, IDC_EDITPATH, file_path, MAX_DPATH);
	TVhDlg = GetDlgItem (hDlg, IDC_CONFIGTREE);
	memset (&is, 0, sizeof (is));
	is.hInsertAfter = isdir < 0 ? TVI_ROOT : TVI_SORT;
	is.hParent = parent;
	is.itemex.mask = TVIF_TEXT | TVIF_STATE | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM;
	if (name && path && !_tcscmp (file_name, name) && !_tcscmp (file_path, path)) {
		is.itemex.state |= TVIS_SELECTED;
		is.itemex.stateMask |= TVIS_SELECTED;
	}
	if (isdir) {
		_tcscat (s, L" ");
		is.itemex.state |= TVIS_BOLD;
		is.itemex.stateMask |= TVIS_BOLD;
	}
	if (expand) {
		is.itemex.state |= TVIS_EXPANDED;
		is.itemex.stateMask |= TVIS_EXPANDED;
	}
	_tcscat (s, name);
	if (_tcslen (s) > 4 && !_tcsicmp (s + _tcslen (s) - 4, L".uae"))
		s[_tcslen (s) - 4] = 0;
	if (desc && _tcslen (desc) > 0) {
		_tcscat (s, L" (");
		_tcscat (s, desc);
		_tcscat (s, L")");
	}
	is.itemex.pszText = s;
	is.itemex.iImage = is.itemex.iSelectedImage = isdir > 0 ? 0 : (isdir < 0) ? 2 : 1;
	is.itemex.lParam = (LPARAM)config;
	return TreeView_InsertItem (TVhDlg, &is);
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
	cparent = configstore[idx]->Parent;
	idx = 0;
	while (idx < configstoresize) {
		config = configstore[idx];
		if ((configtypepanel == 1 && !config->hardware) || (configtypepanel == 2 && !config->host) || (configtypepanel == 0 && (config->host || config->hardware))) {
			idx++;
			continue;
		}
		if (config->Parent == cparent) {
			if (config->Directory) {
				HTREEITEM par = AddConfigNode (hDlg, config, config->Name, NULL, config->Path, 1, config->hardware || config->host, parent);
				int idx2 = 0;
				for (;;) {
					if (configstore[idx2] == config->Child) {
						config->item = par;
						if (LoadConfigTreeView (hDlg, idx2, par) == 0) {
							if (!config->hardware && !config->host && !config->Directory)
								TreeView_DeleteItem (GetDlgItem(hDlg, IDC_CONFIGTREE), par);
						}
						break;
					}
					idx2++;
					if (idx2 >= configstoresize)
						break;
				}
			} else if (!config->Directory) {
				if (((config->Type == 0 || config->Type == 3) && configtype == 0) || (config->Type == configtype)) {
					config->item = AddConfigNode (hDlg, config, config->Name, config->Description, config->Path, 0, 0, parent);
					cnt++;
				}
			}
		}
		idx++;
	}
	return cnt;
}

static void InitializeConfig (HWND hDlg, struct ConfigStruct *config)
{
	int i, j, idx1, idx2;

	if (config == NULL) {
		SetDlgItemText (hDlg, IDC_EDITNAME, L"");
		SetDlgItemText (hDlg, IDC_EDITDESCRIPTION, L"");
	} else {
		SetDlgItemText (hDlg, IDC_EDITNAME, config->Name);
		SetDlgItemText (hDlg, IDC_EDITDESCRIPTION, config->Description);
	}
	SendDlgItemMessage (hDlg, IDC_CONFIGLINK, CB_RESETCONTENT, 0, 0L);
	SendDlgItemMessage (hDlg, IDC_CONFIGLINK, CB_ADDSTRING, 0, (LPARAM)L"");
	idx1 = 1;
	idx2 = 0;
	for (j = 0; j < 2; j++) {
		for (i = 0; i < configstoresize; i++) {
			struct ConfigStruct *cs = configstore[i];
			if ((j == 0 && cs->Type == CONFIG_TYPE_HOST) || (j == 1 && cs->Type == CONFIG_TYPE_HARDWARE)) {
				TCHAR tmp2[MAX_DPATH];
				_tcscpy (tmp2, configstore[i]->Path);
				_tcsncat (tmp2, configstore[i]->Name, MAX_DPATH);
				SendDlgItemMessage (hDlg, IDC_CONFIGLINK, CB_ADDSTRING, 0, (LPARAM)tmp2);
				if (config && (!_tcsicmp (tmp2, config->HardwareLink) || !_tcsicmp (tmp2, config->HostLink)))
					idx2 = idx1;
				idx1++;
			}
		}
	}
	SendDlgItemMessage (hDlg, IDC_CONFIGLINK, CB_SETCURSEL, idx2, 0);
}

static void DeleteConfigTree (HWND hDlg)
{
	int i;
	HWND TVhDlg = GetDlgItem(hDlg, IDC_CONFIGTREE);
	for (i = 0; i < configstoresize; i++)
		configstore[i]->item = NULL;
	TreeView_DeleteAllItems (TVhDlg);
}

static HTREEITEM InitializeConfigTreeView (HWND hDlg)
{
	HIMAGELIST himl = ImageList_Create (16, 16, ILC_COLOR8 | ILC_MASK, 3, 0);
	HWND TVhDlg = GetDlgItem(hDlg, IDC_CONFIGTREE);
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
	DeleteConfigTree (hDlg);
	GetConfigPath (path, NULL, FALSE);
	parent = AddConfigNode (hDlg, NULL, path, NULL, NULL, 0, 1, NULL);
	LoadConfigTreeView (hDlg, -1, parent);
	return parent;
}

static void ConfigToRegistry (struct ConfigStruct *config, int type)
{
	if (config) {
		TCHAR path[MAX_DPATH];
		_tcscpy (path, config->Path);
		_tcsncat (path, config->Name, MAX_DPATH);
		regsetstr (NULL, configreg[type], path);
	}
}
static void ConfigToRegistry2 (DWORD ct, int type, DWORD noauto)
{
	if (type > 0)
		regsetint (NULL, configreg2[type], ct);
	if (noauto == 0 || noauto == 1)
		regsetint (NULL, L"ConfigFile_NoAuto", noauto);
}

static void checkautoload (HWND	hDlg, struct ConfigStruct *config)
{
	int ct = 0;

	if (configtypepanel > 0)
		regqueryint (NULL, configreg2[configtypepanel], &ct);
	if (!config || config->Directory) {
		ct = 0;
		ConfigToRegistry2 (ct, configtypepanel, -1);
	}
	CheckDlgButton (hDlg, IDC_CONFIGAUTO, ct ? BST_CHECKED : BST_UNCHECKED);
	ew (hDlg, IDC_CONFIGAUTO, configtypepanel > 0 && config && !config->Directory ? TRUE : FALSE);
	regqueryint (NULL, L"ConfigFile_NoAuto", &ct);
	CheckDlgButton(hDlg, IDC_CONFIGNOLINK, ct ? BST_CHECKED : BST_UNCHECKED);
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

static struct ConfigStruct *initloadsave (HWND hDlg, struct ConfigStruct *config)
{
	HTREEITEM root;
	TCHAR name_buf[MAX_DPATH];
	int dwRFPsize = sizeof (name_buf) / sizeof (TCHAR);
	TCHAR path[MAX_DPATH];

	EnableWindow (GetDlgItem (hDlg, IDC_VIEWINFO), workprefs.info[0]);
	SetDlgItemText (hDlg, IDC_EDITPATH, L"");
	SetDlgItemText (hDlg, IDC_EDITDESCRIPTION, workprefs.description);
	root = InitializeConfigTreeView (hDlg);
	if (regquerystr (NULL, configreg[configtypepanel], name_buf, &dwRFPsize)) {
		struct ConfigStruct *config2 = getconfigstorefrompath (name_buf, path, configtypepanel);
		if (config2)
			config = config2;
		checkautoload (hDlg, config);
	}
	config = fixloadconfig (hDlg, config);
	if (config && config->item)
		TreeView_SelectItem (GetDlgItem(hDlg, IDC_CONFIGTREE), config->item);
	else
		TreeView_SelectItem (GetDlgItem(hDlg, IDC_CONFIGTREE), root);
	EnableWindow (GetDlgItem(hDlg, IDC_CONFIGAUTO), configtypepanel > 0);
	EnableWindow (GetDlgItem(hDlg, IDC_CONFIGLINK), configtypepanel == 0);
	EnableWindow (GetDlgItem(hDlg, IDC_CONFIGNOLINK), configtypepanel == 0);
	return config;
}

static void loadsavecommands (HWND hDlg, WPARAM wParam, struct ConfigStruct **configp, TCHAR **pcfgfile, TCHAR *newpath)
{
	struct ConfigStruct *config = *configp;
	switch (LOWORD (wParam))
	{
	case IDC_SAVE:
		HandleConfiguration (hDlg, CONFIG_SAVE_FULL, config, newpath);
		DeleteConfigTree (hDlg);
		config = CreateConfigStore (config, TRUE);
		ConfigToRegistry (config, configtypepanel);
		config = initloadsave (hDlg, config);
		InitializeConfig (hDlg, config);
		break;
	case IDC_QUICKSAVE:
		HandleConfiguration (hDlg, CONFIG_SAVE, config, NULL);
		DeleteConfigTree (hDlg);
		config = CreateConfigStore (config, TRUE);
		ConfigToRegistry (config, configtypepanel);
		config = initloadsave (hDlg, config);
		InitializeConfig (hDlg, config);
		break;
	case IDC_QUICKLOAD:
		*pcfgfile = HandleConfiguration (hDlg, CONFIG_LOAD, config, NULL);
		ConfigToRegistry (config, configtypepanel);
		InitializeConfig (hDlg, config);
		if (full_property_sheet) {
			inputdevice_updateconfig (&workprefs);
		} else {
			uae_restart (-1, *pcfgfile);
			exit_gui(1);
		}
		break;
	case IDC_LOAD:
		*pcfgfile = HandleConfiguration (hDlg, CONFIG_LOAD_FULL, config, newpath);
		ConfigToRegistry (config, configtypepanel);
		InitializeConfig (hDlg, config);
		if (full_property_sheet) {
			inputdevice_updateconfig (&workprefs);
		} else {
			uae_restart (-1, *pcfgfile);
			exit_gui(1);
		}
		break;
	case IDC_DELETE:
		HandleConfiguration (hDlg, CONFIG_DELETE, config, NULL);
		DeleteConfigTree (hDlg);
		config = CreateConfigStore (config, TRUE);
		config = initloadsave (hDlg, config);
		InitializeConfig (hDlg, config);
		break;
	case IDC_VIEWINFO:
		if (workprefs.info[0]) {
			TCHAR name_buf[MAX_DPATH];
			if (_tcsstr (workprefs.info, L"Configurations\\"))
				_stprintf (name_buf, L"%s\\%s", start_path_data, workprefs.info);
			else
				_tcscpy (name_buf, workprefs.info);
			ShellExecute (NULL, NULL, name_buf, NULL, NULL, SW_SHOWNORMAL);
		}
		break;
	case IDC_SETINFO:
		if (CustomDialogBox(IDD_SETINFO, hDlg, InfoSettingsProc))
			EnableWindow( GetDlgItem( hDlg, IDC_VIEWINFO ), workprefs.info[0] );
		break;
	case IDC_CONFIGAUTO:
		if (configtypepanel > 0) {
			int ct = ischecked (hDlg, IDC_CONFIGAUTO) ? 1 : 0;
			ConfigToRegistry2 (ct, configtypepanel, -1);
		}
		break;
	case IDC_CONFIGNOLINK:
		if (configtypepanel == 0) {
			int ct = ischecked (hDlg, IDC_CONFIGNOLINK) ? 1 : 0;
			ConfigToRegistry2 (-1, -1, ct);
		}
		break;
	case IDC_CONFIGLINK:
		if (HIWORD (wParam) == CBN_SELCHANGE || HIWORD (wParam) == CBN_KILLFOCUS)  {
			LRESULT val;
			TCHAR tmp[MAX_DPATH];
			tmp[0] = 0;
			val = SendDlgItemMessage (hDlg, IDC_CONFIGLINK, CB_GETCURSEL, 0, 0L);
			if (val == CB_ERR)
				SendDlgItemMessage (hDlg, IDC_CONFIGLINK, WM_GETTEXT, (WPARAM)sizeof(tmp) / sizeof (TCHAR), (LPARAM)tmp);
			else
				SendDlgItemMessage (hDlg, IDC_CONFIGLINK, CB_GETLBTEXT, (WPARAM)val, (LPARAM)tmp);
			_tcscpy (workprefs.config_host_path, tmp);
		}
		break;
	}
	*configp = config;
}

static INT_PTR CALLBACK LoadSaveDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	TCHAR *cfgfile = NULL;
	static int recursive;
	static struct ConfigStruct *config;

	switch (msg)
	{
	case WM_INITDIALOG:
		recursive++;
		if (!configstore) {
			DeleteConfigTree (hDlg);
			CreateConfigStore (NULL, FALSE);
			config = NULL;
		}
		pages[LOADSAVE_ID] = hDlg;
		currentpage = LOADSAVE_ID;
		config = initloadsave (hDlg, config);
		recursive--;
		return TRUE;

	case WM_USER:
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
						HTREEITEM ht = TreeView_GetSelection (GetDlgItem(hDlg, IDC_CONFIGTREE));
						if (ht != NULL) {
							TVITEMEX pitem;
							memset (&pitem, 0, sizeof (pitem));
							pitem.mask = TVIF_HANDLE | TVIF_PARAM;
							pitem.hItem = ht;
							if (TreeView_GetItem (GetDlgItem(hDlg, IDC_CONFIGTREE), &pitem)) {
								struct ConfigStruct *config = (struct ConfigStruct*)pitem.lParam;
								if (config && !config->Directory) {
									cfgfile = HandleConfiguration (hDlg, CONFIG_LOAD, config, NULL);
									ConfigToRegistry (config, configtypepanel);
									if (!full_property_sheet)
										uae_restart (0, cfgfile);
									exit_gui (1);
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
				}
			}
			break;
		}
	}

	return FALSE;
}

#define MAX_CONTRIBUTORS_LENGTH 2048

static INT_PTR CALLBACK ContributorsProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	CHARFORMAT CharFormat;
	TCHAR szContributors1[MAX_CONTRIBUTORS_LENGTH];
	TCHAR szContributors2[MAX_CONTRIBUTORS_LENGTH];
	TCHAR szContributors[MAX_CONTRIBUTORS_LENGTH * 2];

	switch (msg) {
	case WM_COMMAND:
		if (wParam == ID_OK) {
			EndDialog (hDlg, 1);
			return TRUE;
		}
		break;
	case WM_INITDIALOG:
		CharFormat.cbSize = sizeof (CharFormat);

		WIN32GUI_LoadUIString(IDS_CONTRIBUTORS1, szContributors1, MAX_CONTRIBUTORS_LENGTH);
		WIN32GUI_LoadUIString(IDS_CONTRIBUTORS2, szContributors2, MAX_CONTRIBUTORS_LENGTH);
		_stprintf (szContributors, L"%s%s", szContributors1, szContributors2);

		SetDlgItemText (hDlg, IDC_CONTRIBUTORS, szContributors );
		SendDlgItemMessage (hDlg, IDC_CONTRIBUTORS, EM_GETCHARFORMAT, 0, (LPARAM) & CharFormat);
		CharFormat.dwMask |= CFM_SIZE | CFM_FACE;
		CharFormat.yHeight = 8 * 20; /* height in twips, where a twip is 1/20th of a point - for a pt.size of 18 */

		_tcscpy (CharFormat.szFaceName, os_vista ? L"Segoe UI" : L"Tahoma");
		SendDlgItemMessage (hDlg, IDC_CONTRIBUTORS, EM_SETCHARFORMAT, SCF_ALL, (LPARAM) & CharFormat);
		return TRUE;
	}
	return FALSE;
}

static void DisplayContributors (HWND hDlg)
{
	CustomDialogBox (IDD_CONTRIBUTORS, hDlg, ContributorsProc);
}

typedef struct url_info
{
	int   id;
	BOOL  state;
	TCHAR *display;
	TCHAR *url;
} urlinfo;

static urlinfo urls[] =
{
	{IDC_CLOANTOHOME, FALSE, L"Cloanto's Amiga Forever", L"http://www.amigaforever.com/"},
	{IDC_AMIGAHOME, FALSE, L"Amiga Inc.", L"http://www.amiga.com"},
//	{IDC_PICASSOHOME, FALSE, L"Picasso96 Home Page", L"http://www.picasso96.cogito.de/"},
	{IDC_UAEHOME, FALSE, L"UAE Home Page", L"http://www.amigaemulator.org/"},
	{IDC_WINUAEHOME, FALSE, L"WinUAE Home Page", L"http://www.winuae.net/"},
//	{IDC_AIABHOME, FALSE, L"AIAB", L"http://www.amigainabox.co.uk/"},
	{IDC_THEROOTS, FALSE, L"Back To The Roots", L"http://www.back2roots.org/"},
	{IDC_ABIME, FALSE, L"abime.net", L"http://www.abime.net/"},
	{IDC_CAPS, FALSE, L"SPS", L"http://www.softpres.org/"},
	{IDC_AMIGASYS, FALSE, L"AmigaSYS", L"http://www.amigasys.com/"},
	{IDC_AMIKIT, FALSE, L"AmiKit", L"http://amikit.amiga.sk/"},
	{ -1, FALSE, NULL, NULL }
};

static void SetupRichText(HWND hDlg, urlinfo *url)
{
	CHARFORMAT CharFormat;
	CharFormat.cbSize = sizeof (CharFormat);

	SetDlgItemText(hDlg, url->id, url->display);
	SendDlgItemMessage(hDlg, url->id, EM_GETCHARFORMAT, 0, (LPARAM)&CharFormat);
	CharFormat.dwMask   |= CFM_UNDERLINE | CFM_SIZE | CFM_FACE | CFM_COLOR;
	CharFormat.dwEffects = url->state ? CFE_UNDERLINE : 0;
	CharFormat.yHeight = 10 * 20; /* height in twips, where a twip is 1/20th of a point - for a pt.size of 18 */

	CharFormat.crTextColor = GetSysColor(COLOR_ACTIVECAPTION);
	_tcscpy (CharFormat.szFaceName, os_vista ? L"Segoe UI" : L"Tahoma");
	SendDlgItemMessage(hDlg, url->id, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&CharFormat);
	SendDlgItemMessage(hDlg, url->id, EM_SETBKGNDCOLOR, 0, GetSysColor(COLOR_3DFACE));
}

static void url_handler(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int last_rectangle = -1;
	int i;
	BOOL found = FALSE;
	HCURSOR m_hCursor = NULL;
	POINT point;
	point.x = LOWORD (lParam);
	point.y = HIWORD (lParam);

	for (i = 0; urls[i].id >= 0; i++)
	{
		RECT rect;
		GetWindowRect(GetDlgItem(hDlg, urls[i].id), &rect);
		ScreenToClient(hDlg, (POINT*)&rect);
		ScreenToClient(hDlg, (POINT*)&rect.right);
		if(PtInRect(&rect, point))
		{
			if(msg == WM_LBUTTONDOWN)
			{
				ShellExecute (NULL, NULL, urls[i].url , NULL, NULL, SW_SHOWNORMAL);
				SetCursor(LoadCursor(NULL, MAKEINTRESOURCE(IDC_ARROW)));
			}
			else
			{
				if(i != last_rectangle)
				{
					// try and load the system hand (Win2000+)
					m_hCursor = LoadCursor(NULL, MAKEINTRESOURCE(IDC_HAND) );
					if (!m_hCursor)
					{
						// retry with our fallback hand
						m_hCursor = LoadCursor(hInst, MAKEINTRESOURCE(IDC_MYHAND) );
					}
					SetCursor(m_hCursor);
					urls[i].state = TRUE;
					SetupRichText(hDlg, &urls[i]);

					if(last_rectangle != -1)
					{
						urls[last_rectangle].state = FALSE;
						SetupRichText(hDlg, &urls[last_rectangle]);
					}
				}
			}
			last_rectangle = i;
			found = TRUE;
			break;
		}
	}

	if(!found && last_rectangle >= 0)
	{
		SetCursor(LoadCursor(NULL, MAKEINTRESOURCE(IDC_ARROW)));
		urls[last_rectangle].state = FALSE;
		SetupRichText(hDlg, &urls[last_rectangle]);
		last_rectangle = -1;
	}
}

static void setac (HWND hDlg, int id)
{
	SHAutoComplete (GetDlgItem (hDlg, id), SHACF_FILESYSTEM | SHACF_AUTOAPPEND_FORCE_ON | SHACF_AUTOSUGGEST_FORCE_ON | SHACF_USETAB);
}
static void setautocomplete (HWND hDlg, int id)
{
	HWND item = FindWindowEx (GetDlgItem (hDlg, id), NULL, L"Edit", NULL);
	if (item)
		SHAutoComplete (item, SHACF_FILESYSTEM | SHACF_AUTOAPPEND_FORCE_ON | SHACF_AUTOSUGGEST_FORCE_ON | SHACF_USETAB);
}
static void setmultiautocomplete (HWND hDlg, int *ids)
{
	int i;
	for (i = 0; ids[i] >= 0; i++)
		setautocomplete (hDlg, ids[i]);
}

static void setpath (HWND hDlg, TCHAR *name, DWORD d, TCHAR *def)
{
	TCHAR tmp[MAX_DPATH];

	_tcscpy (tmp, def);
	fetch_path (name, tmp, sizeof (tmp) / sizeof (TCHAR));
	SetDlgItemText (hDlg, d, tmp);
}

static void values_to_pathsdialog (HWND hDlg)
{
	setpath (hDlg, L"KickstartPath", IDC_PATHS_ROM, L"Roms");
	setpath (hDlg, L"ConfigurationPath", IDC_PATHS_CONFIG, L"Configurations");
	setpath (hDlg, L"ScreenshotPath", IDC_PATHS_SCREENSHOT, L"ScreenShots");
	setpath (hDlg, L"StatefilePath", IDC_PATHS_SAVESTATE, L"Savestates");
	setpath (hDlg, L"SaveimagePath", IDC_PATHS_SAVEIMAGE, L"SaveImages");
	setpath (hDlg, L"VideoPath", IDC_PATHS_AVIOUTPUT, L"Videos");
	setpath (hDlg, L"RipperPath", IDC_PATHS_RIP, L".\\");
}

static void resetregistry (void)
{
	regdeletetree (NULL, L"DetectedROMs");
	regdelete (NULL, L"QuickStartMode");
	regdelete (NULL, L"ConfigFile");
	regdelete (NULL, L"ConfigFileHardware");
	regdelete (NULL, L"ConfigFileHost");
	regdelete (NULL, L"ConfigFileHardware_Auto");
	regdelete (NULL, L"ConfigFileHost_Auto");
	regdelete (NULL, L"ConfigurationPath");
	regdelete (NULL, L"SaveimagePath");
	regdelete (NULL, L"ScreenshotPath");
	regdelete (NULL, L"StatefilePath");
	regdelete (NULL, L"VideoPath");
	regdelete (NULL, L"RipperPath");
	regdelete (NULL, L"QuickStartModel");
	regdelete (NULL, L"QuickStartConfiguration");
	regdelete (NULL, L"QuickStartCompatibility");
	regdelete (NULL, L"QuickStartHostConfig");
	regdelete (NULL, L"ConfigurationCache");
	regdelete (NULL, L"RelativePaths");
	regdelete (NULL, L"DirectDraw_Secondary");
	regdelete (NULL, L"ShownsupportedModes");
}

pathtype path_type;
static INT_PTR CALLBACK PathsDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	const GUID pathsguid = { 0x5674338c, 0x7a0b, 0x4565, { 0xbf, 0x75, 0x62, 0x8c, 0x80, 0x4a, 0xef, 0xf7 } };
	void create_afnewdir(int);
	static int recursive;
	static pathtype ptypes[10];
	static int numtypes;
	int val, selpath = 0;
	TCHAR tmp[MAX_DPATH];

	switch (msg)
	{
	case WM_INITDIALOG:
		recursive++;
		pages[PATHS_ID] = hDlg;
		setac (hDlg, IDC_PATHS_ROM);
		setac (hDlg, IDC_PATHS_CONFIG);
		setac (hDlg, IDC_PATHS_SCREENSHOT);
		setac (hDlg, IDC_PATHS_SAVESTATE);
		setac (hDlg, IDC_PATHS_SAVEIMAGE);
		setac (hDlg, IDC_PATHS_AVIOUTPUT);
		setac (hDlg, IDC_PATHS_RIP);
		CheckDlgButton(hDlg, IDC_PATHS_CONFIGCACHE, configurationcache);
		CheckDlgButton(hDlg, IDC_PATHS_RELATIVE, relativepaths);
		currentpage = PATHS_ID;
		ShowWindow (GetDlgItem (hDlg, IDC_RESETREGISTRY), FALSE);
		numtypes = 0;
		SendDlgItemMessage (hDlg, IDC_PATHS_DEFAULTTYPE, CB_RESETCONTENT, 0, 0L);
		if (af_path_2005 & 1) {
			WIN32GUI_LoadUIString (IDS_DEFAULT_AF, tmp, sizeof tmp / sizeof (TCHAR));
			SendDlgItemMessage (hDlg, IDC_PATHS_DEFAULTTYPE, CB_ADDSTRING, 0, (LPARAM)tmp);
			if (path_type == PATH_TYPE_NEWAF)
				selpath = numtypes;
			ptypes[numtypes++] = PATH_TYPE_NEWAF;
		}
		if (start_path_new1[0]) {
			WIN32GUI_LoadUIString (IDS_DEFAULT_NEWWINUAE, tmp, sizeof tmp / sizeof (TCHAR));
			SendDlgItemMessage (hDlg, IDC_PATHS_DEFAULTTYPE, CB_ADDSTRING, 0, (LPARAM)tmp);
			if (path_type == PATH_TYPE_NEWWINUAE)
				selpath = numtypes;
			ptypes[numtypes++] = PATH_TYPE_NEWWINUAE;
		}
		if ((af_path_2005 & 3) == 2) {
			SendDlgItemMessage (hDlg, IDC_PATHS_DEFAULTTYPE, CB_ADDSTRING, 0, (LPARAM)L"AmigaForeverData");
			if (path_type == PATH_TYPE_AMIGAFOREVERDATA)
				selpath = numtypes;
			ptypes[numtypes++] = PATH_TYPE_AMIGAFOREVERDATA;
		}
		WIN32GUI_LoadUIString (IDS_DEFAULT_WINUAE, tmp, sizeof tmp / sizeof (TCHAR));
		SendDlgItemMessage (hDlg, IDC_PATHS_DEFAULTTYPE, CB_ADDSTRING, 0, (LPARAM)tmp);
		if (path_type == PATH_TYPE_WINUAE || path_type == PATH_TYPE_DEFAULT)
			selpath = numtypes;
		ptypes[numtypes++] = PATH_TYPE_WINUAE;
		SendDlgItemMessage (hDlg, IDC_PATHS_DEFAULTTYPE, CB_SETCURSEL, selpath, 0);
		EnableWindow (GetDlgItem (hDlg, IDC_PATHS_DEFAULTTYPE), numtypes > 0 ? TRUE : FALSE);
		values_to_pathsdialog (hDlg);
		recursive--;
		return TRUE;

	case WM_COMMAND:
		if (recursive > 0)
			break;
		recursive++;
		switch (LOWORD (wParam))
		{
		case IDC_PATHS_ROMS:
			fetch_path (L"KickstartPath", tmp, sizeof (tmp) / sizeof (TCHAR));
			if (DirectorySelection (hDlg, &pathsguid, tmp)) {
				load_keyring (&workprefs, NULL);
				set_path (L"KickstartPath", tmp);
				if (!scan_roms (hDlg, 1))
					gui_message_id (IDS_ROMSCANNOROMS);
				values_to_pathsdialog (hDlg);
			}
			break;
		case IDC_PATHS_ROM:
			GetWindowText (GetDlgItem (hDlg, IDC_PATHS_ROM), tmp, sizeof (tmp) / sizeof (TCHAR));
			set_path (L"KickstartPath", tmp);
			break;
		case IDC_PATHS_CONFIGS:
			fetch_path (L"ConfigurationPath", tmp, sizeof (tmp) / sizeof (TCHAR));
			if (DirectorySelection (hDlg, &pathsguid, tmp)) {
				set_path (L"ConfigurationPath", tmp);
				values_to_pathsdialog (hDlg);
				FreeConfigStore ();
			}
			break;
		case IDC_PATHS_CONFIG:
			GetWindowText (GetDlgItem (hDlg, IDC_PATHS_CONFIG), tmp, sizeof (tmp) / sizeof (TCHAR));
			set_path (L"ConfigurationPath", tmp);
			FreeConfigStore ();
			break;
		case IDC_PATHS_SCREENSHOTS:
			fetch_path (L"ScreenshotPath", tmp, sizeof (tmp) / sizeof (TCHAR));
			if (DirectorySelection (hDlg, &pathsguid, tmp)) {
				set_path (L"ScreenshotPath", tmp);
				values_to_pathsdialog (hDlg);
			}
			break;
		case IDC_PATHS_SCREENSHOT:
			GetWindowText (GetDlgItem (hDlg, IDC_PATHS_SCREENSHOT), tmp, sizeof (tmp) / sizeof (TCHAR));
			set_path (L"ScreenshotPath", tmp);
			break;
		case IDC_PATHS_SAVESTATES:
			fetch_path (L"StatefilePath", tmp, sizeof (tmp) / sizeof (TCHAR));
			if (DirectorySelection (hDlg, &pathsguid, tmp)) {
				set_path (L"StatefilePath", tmp);
				values_to_pathsdialog (hDlg);
			}
			break;
		case IDC_PATHS_SAVESTATE:
			GetWindowText (GetDlgItem (hDlg, IDC_PATHS_SAVESTATE), tmp, sizeof (tmp) / sizeof (TCHAR));
			set_path (L"StatefilePath", tmp);
			break;
		case IDC_PATHS_SAVEIMAGES:
			fetch_path (L"SaveimagePath", tmp, sizeof (tmp) / sizeof (TCHAR));
			if (DirectorySelection (hDlg, &pathsguid, tmp)) {
				set_path (L"SaveimagePath", tmp);
				values_to_pathsdialog (hDlg);
			}
			break;
		case IDC_PATHS_SAVEIMAGE:
			GetWindowText (GetDlgItem (hDlg, IDC_PATHS_SAVEIMAGE), tmp, sizeof (tmp) / sizeof (TCHAR));
			set_path (L"SaveimagePath", tmp);
			break;
		case IDC_PATHS_AVIOUTPUTS:
			fetch_path (L"VideoPath", tmp, sizeof (tmp) / sizeof (TCHAR));
			if (DirectorySelection (hDlg, &pathsguid, tmp)) {
				set_path (L"VideoPath", tmp);
				values_to_pathsdialog (hDlg);
			}
			break;
		case IDC_PATHS_RIPS:
			fetch_path (L"RipperPath", tmp, sizeof (tmp) / sizeof (TCHAR));
			if (DirectorySelection (hDlg, &pathsguid, tmp)) {
				set_path (L"RipperPath", tmp);
				values_to_pathsdialog (hDlg);
			}
			break;
		case IDC_PATHS_AVIOUTPUT:
			GetWindowText (GetDlgItem (hDlg, IDC_PATHS_AVIOUTPUT), tmp, sizeof (tmp) / sizeof (TCHAR));
			set_path (L"VideoPath", tmp);
			break;
		case IDC_PATHS_RIP:
			GetWindowText (GetDlgItem (hDlg, IDC_PATHS_RIP), tmp, sizeof (tmp) / sizeof (TCHAR));
			set_path (L"RipperPath", tmp);
			break;
		case IDC_PATHS_DEFAULT:
			val = SendDlgItemMessage (hDlg, IDC_PATHS_DEFAULTTYPE, CB_GETCURSEL, 0, 0L);
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
				}
				SetCurrentDirectory (start_path_data);
				setpathmode (path_type);
				set_path (L"KickstartPath", NULL, path_type);
				set_path (L"ConfigurationPath", NULL, path_type);
				set_path (L"ScreenshotPath", NULL, path_type);
				set_path (L"StatefilePath", NULL, path_type);
				set_path (L"SaveimagePath", NULL, path_type);
				set_path (L"VideoPath", NULL, path_type);
				set_path (L"RipperPath", NULL, path_type);
				set_path (L"InputPath", NULL, path_type);
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
		case IDC_PATHS_CONFIGCACHE:
			configurationcache = ischecked (hDlg, IDC_PATHS_CONFIGCACHE) ? 1 : 0;
			regsetint (NULL, L"ConfigurationCache", configurationcache);
			break;
		case IDC_PATHS_RELATIVE:
			relativepaths = ischecked (hDlg, IDC_PATHS_RELATIVE) ? 1 : 0;
			regsetint (NULL, L"RelativePaths", relativepaths);
			break;

		}
		recursive--;
	}
	return FALSE;
}

struct amigamodels {
	int compalevels;
	int id;
};
static struct amigamodels amodels[] = {
	{ 4, IDS_QS_MODEL_A500 }, // "Amiga 500"
	{ 4, IDS_QS_MODEL_A500P }, // "Amiga 500+"
	{ 4, IDS_QS_MODEL_A600 }, // "Amiga 600"
	{ 4, IDS_QS_MODEL_A1000 }, // "Amiga 1000"
	{ 3, IDS_QS_MODEL_A1200 }, // "Amiga 1200"
	{ 1, IDS_QS_MODEL_A3000 }, // "Amiga 3000"
	{ 1, IDS_QS_MODEL_A4000 }, // "Amiga 4000"
	{ 0, }, //{ 1, IDS_QS_MODEL_A4000T }, // "Amiga 4000T"
	{ 3, IDS_QS_MODEL_CD32 }, // "CD32"
	{ 4, IDS_QS_MODEL_CDTV }, // "CDTV"
	{ 4, IDS_QS_MODEL_ARCADIA }, // "Arcadia"
	{ 1, IDS_QS_MODEL_UAE }, // "Expanded UAE example configuration"
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
	ew (guiDlg, IDC_RESETAMIGA, FALSE);
	workprefs.nr_floppies = quickstart_floppy;
	quickstart_ok = built_in_prefs (&workprefs, quickstart_model, quickstart_conf, quickstart_compa, romcheck);
	workprefs.ntscmode = quickstart_ntsc != 0;
	quickstart_cd = workprefs.floppyslots[1].dfxtype == DRV_NONE && (quickstart_model == 8 || quickstart_model == 9);
	enable_for_quickstart (hDlg);
	addfloppytype (hDlg, 0);
	addfloppytype (hDlg, 1);
	addfloppyhistory (hDlg);
	config_filename[0] = 0;
	setguititle (NULL);
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

static void init_quickstartdlg (HWND hDlg)
{
	static int firsttime;
	int i, j, idx, idx2, qssize, total;
	TCHAR tmp1[2 * MAX_DPATH], tmp2[MAX_DPATH], hostconf[MAX_DPATH];
	TCHAR *p1, *p2;

	qssize = sizeof (tmp1) / sizeof (TCHAR);
	regquerystr (NULL, L"QuickStartHostConfig", hostconf, &qssize);
	if (firsttime == 0 && workprefs.start_gui) {
		regqueryint (NULL, L"QuickStartModel", &quickstart_model);
		regqueryint (NULL, L"QuickStartConfiguration", &quickstart_conf);
		regqueryint (NULL, L"QuickStartCompatibility", &quickstart_compa);
		regqueryint (NULL, L"QuickStartFloppies", &quickstart_floppy);
		regqueryint (NULL, L"QuickStartCDType", &quickstart_cdtype);
		int size = sizeof quickstart_cddrive / sizeof (TCHAR);
		regquerystr (NULL, L"QuickStartCDDrive", quickstart_cddrive, &size);
		regqueryint (NULL, L"QuickStartNTSC", &quickstart_ntsc);
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
	firsttime = 1;

	CheckDlgButton (hDlg, IDC_QUICKSTARTMODE, quickstart);
	CheckDlgButton (hDlg, IDC_NTSC, quickstart_ntsc != 0);

	WIN32GUI_LoadUIString (IDS_QS_MODELS, tmp1, sizeof (tmp1) / sizeof (TCHAR));
	_tcscat (tmp1, L"\n");
	p1 = tmp1;
	SendDlgItemMessage (hDlg, IDC_QUICKSTART_MODEL, CB_RESETCONTENT, 0, 0L);
	idx = idx2 = 0;
	i = 0;
	while (amodels[i].compalevels >= 0) {
		if (amodels[i].compalevels > 0) {
			p2 = _tcschr (p1, '\n');
			if (p2 && _tcslen (p2) > 0) {
				*p2++ = 0;
				SendDlgItemMessage (hDlg, IDC_QUICKSTART_MODEL, CB_ADDSTRING, 0, (LPARAM)p1);
				p1 = p2;
			}
			if (i == quickstart_model)
				idx2 = idx;
			idx++;
		}
		i++;
	}
	SendDlgItemMessage (hDlg, IDC_QUICKSTART_MODEL, CB_SETCURSEL, idx2, 0);

	total = 0;
	SendDlgItemMessage (hDlg, IDC_QUICKSTART_CONFIGURATION, CB_RESETCONTENT, 0, 0L);
	if (amodels[quickstart_model].id == IDS_QS_MODEL_ARCADIA) {
		struct romlist **rl = getarcadiaroms ();
		for (i = 0; rl[i]; i++) {
			SendDlgItemMessage (hDlg, IDC_QUICKSTART_CONFIGURATION, CB_ADDSTRING, 0, (LPARAM)rl[i]->rd->name);
			total++;
		}
		xfree (rl);
	} else {
		WIN32GUI_LoadUIString (amodels[quickstart_model].id, tmp1, sizeof (tmp1) / sizeof (TCHAR));
		_tcscat (tmp1, L"\n");
		p1 = tmp1;
		init_quickstartdlg_tooltip (hDlg, 0);
		total = 0;
		for (;;) {
			p2 = _tcschr (p1, '\n');
			if (!p2)
				break;
			*p2++= 0;
			SendDlgItemMessage (hDlg, IDC_QUICKSTART_CONFIGURATION, CB_ADDSTRING, 0, (LPARAM)p1);
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
	SendDlgItemMessage (hDlg, IDC_QUICKSTART_CONFIGURATION, CB_SETCURSEL, quickstart_conf, 0);

	if (quickstart_compa >= amodels[quickstart_model].compalevels)
		quickstart_compa = 1;
	if (quickstart_compa >= amodels[quickstart_model].compalevels)
		quickstart_compa = 0;
	i = amodels[quickstart_model].compalevels;
	ew (hDlg, IDC_QUICKSTART_COMPATIBILITY, i > 1);
	SendDlgItemMessage (hDlg, IDC_QUICKSTART_COMPATIBILITY, TBM_SETRANGE, TRUE, MAKELONG (0, i > 1 ? i - 1 : 1));
	SendDlgItemMessage (hDlg, IDC_QUICKSTART_COMPATIBILITY, TBM_SETPAGESIZE, 0, 1);
	SendDlgItemMessage (hDlg, IDC_QUICKSTART_COMPATIBILITY, TBM_SETPOS, TRUE, quickstart_compa);

	SendDlgItemMessage (hDlg, IDC_QUICKSTART_HOSTCONFIG, CB_RESETCONTENT, 0, 0L);
	WIN32GUI_LoadUIString (IDS_DEFAULT_HOST, tmp1, sizeof (tmp1) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_QUICKSTART_HOSTCONFIG, CB_ADDSTRING, 0, (LPARAM)tmp1);
	idx = 0;
	j = 1;
	for (i = 0; i < configstoresize; i++) {
		if (configstore[i]->Type == CONFIG_TYPE_HOST) {
			_tcscpy (tmp2, configstore[i]->Path);
			_tcsncat (tmp2, configstore[i]->Name, MAX_DPATH);
			if (!_tcscmp (tmp2, hostconf))
				idx = j;
			SendDlgItemMessage (hDlg, IDC_QUICKSTART_HOSTCONFIG, CB_ADDSTRING, 0, (LPARAM)tmp2);
			j++;
		}
	}
	SendDlgItemMessage (hDlg, IDC_QUICKSTART_HOSTCONFIG, CB_SETCURSEL, idx, 0);
	regsetint (NULL, L"QuickStartModel", quickstart_model);
	regsetint (NULL, L"QuickStartConfiguration", quickstart_conf);
	regsetint (NULL, L"QuickStartCompatibility", quickstart_compa);
}

static void floppytooltip (HWND hDlg, int num, uae_u32 crc32);
static void testimage (HWND hDlg, int num)
{
	int ret;
	int reload = 0;
	uae_u32 crc32;
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
	ret = DISK_examine_image (&workprefs, num, &crc32);
	if (!ret)
		return;
	floppytooltip (hDlg, num, crc32);
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
		load_quickstart (hDlg, 1);
		init_quickstartdlg (hDlg);
	}
}

static INT_PTR CALLBACK FloppyDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
static int diskselectmenu (HWND hDlg, WPARAM wParam);
static void addallfloppies (HWND hDlg);

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

static INT_PTR CALLBACK QuickstartDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive;
	int ret = FALSE, i;
	TCHAR tmp[MAX_DPATH];
	static TCHAR df0[MAX_DPATH];
	static TCHAR df1[MAX_DPATH];
	static int dfxtype[2] = { -1, -1 };
	static int doinit;
	LRESULT val;

	switch(msg)
	{
	case WM_INITDIALOG:
		{
			int ids[] = { IDC_DF0TEXTQ, IDC_DF1TEXTQ, -1 };
			pages[QUICKSTART_ID] = hDlg;
			currentpage = QUICKSTART_ID;
			enable_for_quickstart (hDlg);
			setfloppytexts (hDlg, true);
			setmultiautocomplete (hDlg, ids);
			doinit = 1;
			break;
		}
	case WM_NULL:
		if (recursive > 0)
			break;
		recursive++;
		if (doinit) {
			addfloppytype (hDlg, 0);
			addfloppytype (hDlg, 1);
			addfloppyhistory (hDlg);
			init_quickstartdlg (hDlg);
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
				val = SendDlgItemMessage (hDlg, IDC_CD0Q_TYPE, CB_GETCURSEL, 0, 0);
				if (val != CB_ERR) {
					quickstart_cdtype = val;
					if (quickstart_cdtype >= 2) {
						int len = sizeof quickstart_cddrive / sizeof (TCHAR);
						quickstart_cdtype = 2;
						SendDlgItemMessage (hDlg, IDC_CD0Q_TYPE, WM_GETTEXT, (WPARAM)len, (LPARAM)quickstart_cddrive);
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
				val = SendDlgItemMessage (hDlg, IDC_QUICKSTART_MODEL, CB_GETCURSEL, 0, 0L);
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
						init_quickstartdlg (hDlg);
						if (quickstart)
							load_quickstart (hDlg, 1);
						if (quickstart && !full_property_sheet)
							qs_request_reset = 2;
					}
				}
				break;
			case IDC_QUICKSTART_CONFIGURATION:
				val = SendDlgItemMessage (hDlg, IDC_QUICKSTART_CONFIGURATION, CB_GETCURSEL, 0, 0L);
				if (val != CB_ERR && val != quickstart_conf) {
					quickstart_conf = val;
					init_quickstartdlg (hDlg);
					if (quickstart)
						load_quickstart (hDlg, 1);
					if (quickstart && !full_property_sheet)
						qs_request_reset = 2;
				}
				break;
			case IDC_QUICKSTART_HOSTCONFIG:
				val = SendDlgItemMessage (hDlg, IDC_QUICKSTART_HOSTCONFIG, CB_GETCURSEL, 0, 0);
				if (val != CB_ERR) {
					SendDlgItemMessage (hDlg, IDC_QUICKSTART_HOSTCONFIG, CB_GETLBTEXT, (WPARAM)val, (LPARAM)tmp);
					regsetstr (NULL, L"QuickStartHostConfig", tmp);
					quickstarthost (hDlg, tmp);
					if (val == 0 && quickstart)
						load_quickstart (hDlg, 0);
				}
				break;
			}
		} else {
			switch (LOWORD (wParam))
			{
			case IDC_NTSC:
				quickstart_ntsc = ischecked (hDlg, IDC_NTSC);
				regsetint (NULL, L"QuickStartNTSC", quickstart_ntsc);
				if (quickstart) {
					init_quickstartdlg (hDlg);
					load_quickstart (hDlg, 0);
				}
				break;
			case IDC_QUICKSTARTMODE:
				quickstart = ischecked (hDlg, IDC_QUICKSTARTMODE);
				regsetint (NULL, L"QuickStartMode", quickstart);
				quickstart_cd = 0;
				if (quickstart) {
					init_quickstartdlg (hDlg);
					load_quickstart (hDlg, 0);
				}
				enable_for_quickstart (hDlg);
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
			if (currentpage == QUICKSTART_ID)
				ret = FloppyDlgProc (hDlg, msg, wParam, lParam);
			break;
		case IDC_QUICKSTART_SETCONFIG:
			load_quickstart (hDlg, 1);
			break;
		}
		recursive--;
	case WM_HSCROLL:
		if (recursive > 0)
			break;
		recursive++;
		if ((HWND)lParam == GetDlgItem (hDlg, IDC_QUICKSTART_COMPATIBILITY)) {
			val = SendMessage ((HWND)lParam, TBM_GETPOS, 0, 0);
			if (val >= 0 && val != quickstart_compa) {
				quickstart_compa = val;
				init_quickstartdlg (hDlg);
				if (quickstart)
					load_quickstart (hDlg, 0);
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
	CHARFORMAT CharFormat;
	int i;

	CharFormat.cbSize = sizeof (CharFormat);

	SetDlgItemText (hDlg, IDC_RICHEDIT1, L"WinUAE");
	SendDlgItemMessage (hDlg, IDC_RICHEDIT1, EM_GETCHARFORMAT, 0, (LPARAM) & CharFormat);
	CharFormat.dwMask |= CFM_BOLD | CFM_SIZE | CFM_FACE;
	CharFormat.dwEffects = CFE_BOLD;
	CharFormat.yHeight = 18 * 20; /* height in twips, where a twip is 1/20th of a point - for a pt.size of 18 */

	_tcscpy (CharFormat.szFaceName, L"Times New Roman");
	SendDlgItemMessage (hDlg, IDC_RICHEDIT1, EM_SETCHARFORMAT, SCF_ALL, (LPARAM) & CharFormat);
	SendDlgItemMessage (hDlg, IDC_RICHEDIT1, EM_SETBKGNDCOLOR, 0, GetSysColor (COLOR_3DFACE));

	SetDlgItemText (hDlg, IDC_RICHEDIT2, VersionStr );
	SendDlgItemMessage (hDlg, IDC_RICHEDIT2, EM_GETCHARFORMAT, 0, (LPARAM) & CharFormat);
	CharFormat.dwMask |= CFM_SIZE | CFM_FACE;
	CharFormat.yHeight = 10 * 20;
	_tcscpy (CharFormat.szFaceName, L"Times New Roman");
	SendDlgItemMessage (hDlg, IDC_RICHEDIT2, EM_SETCHARFORMAT, SCF_ALL, (LPARAM) & CharFormat);
	SendDlgItemMessage (hDlg, IDC_RICHEDIT2, EM_SETBKGNDCOLOR, 0, GetSysColor (COLOR_3DFACE));

	for(i = 0; urls[i].id >= 0; i++)
		SetupRichText(hDlg, &urls[i]);
}

static INT_PTR CALLBACK AboutDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch( msg )
	{
	case WM_INITDIALOG:
		pages[ABOUT_ID] = hDlg;
		currentpage = ABOUT_ID;
		init_aboutdlg (hDlg);
		break;

	case WM_COMMAND:
		if (wParam == IDC_CONTRIBUTORS)
			DisplayContributors (hDlg);
		break;
	case WM_SETCURSOR:
		return TRUE;
		break;
	case WM_LBUTTONDOWN:
	case WM_MOUSEMOVE:
		url_handler( hDlg, msg, wParam, lParam );
		break;
	}

	return FALSE;
}

static void enable_for_displaydlg (HWND hDlg)
{
	int rtg = ! workprefs.address_space_24;
#ifndef PICASSO96
	rtg = FALSE;
#endif
	ew (hDlg, IDC_SCREENMODE_RTG, rtg);
	ew (hDlg, IDC_XCENTER, TRUE);
	ew (hDlg, IDC_YCENTER, TRUE);
	ew (hDlg, IDC_LM_SCANLINES, TRUE);
	ew (hDlg, IDC_FRAMERATE2, !workprefs.gfx_avsync);
	ew (hDlg, IDC_FRAMERATE, !workprefs.cpu_cycle_exact);
	ew (hDlg, IDC_LORES, !workprefs.gfx_autoresolution);
	ew (hDlg, IDC_LM_NORMAL, !workprefs.gfx_autoresolution);
	ew (hDlg, IDC_LM_DOUBLED, !workprefs.gfx_autoresolution);
	ew (hDlg, IDC_LM_SCANLINES, !workprefs.gfx_autoresolution);
}

static void enable_for_chipsetdlg (HWND hDlg)
{
	int enable = workprefs.cpu_cycle_exact ? FALSE : TRUE;

#if !defined (CPUEMU_12)
	ew (hDlg, IDC_CYCLEEXACT, FALSE);
#endif
	ew (hDlg, IDC_FASTCOPPER, enable);
	ew (hDlg, IDC_GENLOCK, full_property_sheet);
	ew (hDlg, IDC_BLITIMM, enable);
	if (enable == FALSE) {
		workprefs.immediate_blits = 0;
		CheckDlgButton (hDlg, IDC_FASTCOPPER, FALSE);
		CheckDlgButton (hDlg, IDC_BLITIMM, FALSE);
	}
	ew (hDlg, IDC_CS_EXT, workprefs.cs_compatible ? TRUE : FALSE);
}

static DWORD idnth[] = { IDS_SECOND, IDS_THIRD, IDS_FOURTH, IDS_FIFTH, IDS_SIXTH, IDS_SEVENTH, IDS_EIGHTH, IDS_NINTH, IDS_TENTH, -1 };
static void LoadNthString( DWORD value, TCHAR *nth, DWORD dwNthMax )
{
	nth[0] = 0;
	if (value >= 1 && value <= 9)
		WIN32GUI_LoadUIString(idnth[value - 1], nth, dwNthMax);
}

static int fakerefreshrates[] = { 50, 60, 100, 120, 0 };
struct storedrefreshrate
{
	int rate, type;
};
static struct storedrefreshrate storedrefreshrates[MAX_REFRESH_RATES + 1];

static void init_frequency_combo (HWND hDlg, int dmode)
{
	int i, j, freq;
	TCHAR hz[20], hz2[20], txt[100];
	LRESULT index;
	struct MultiDisplay *md = getdisplay (&workprefs);

	i = 0; index = 0;
	while (dmode >= 0 && (freq = md->DisplayModes[dmode].refresh[i]) > 0 && index < MAX_REFRESH_RATES) {
		storedrefreshrates[index].rate = freq;
		storedrefreshrates[index++].type = md->DisplayModes[dmode].refreshtype[i];
		i++;
	}
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
	SendDlgItemMessage(hDlg, IDC_REFRESHRATE, CB_RESETCONTENT, 0, 0);
	WIN32GUI_LoadUIString (IDS_VSYNC_DEFAULT, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage(hDlg, IDC_REFRESHRATE, CB_ADDSTRING, 0, (LPARAM)txt);
	for (i = 0; i < index; i++) {
		freq = storedrefreshrates[i].rate;
		if (freq < 0) {
			freq = -freq;
			_stprintf (hz, L"(%dHz)", freq);
		} else {
			_stprintf (hz, L"%dHz", freq);
		}
		if (freq == 50 || freq == 100)
			_tcscat (hz, L" PAL");
		if (freq == 60 || freq == 120)
			_tcscat (hz, L" NTSC");
		if (storedrefreshrates[i].type)
			_tcscat (hz, L" (*)");
		if (abs (workprefs.gfx_refreshrate) == freq)
			_tcscpy (hz2, hz);
		SendDlgItemMessage (hDlg, IDC_REFRESHRATE, CB_ADDSTRING, 0, (LPARAM)hz);
	}
	index = CB_ERR;
	if (hz2[0] >= 0)
		index = SendDlgItemMessage (hDlg, IDC_REFRESHRATE, CB_SELECTSTRING, 0, (LPARAM)hz2);
	if (index == CB_ERR) {
		WIN32GUI_LoadUIString (IDS_VSYNC_DEFAULT, txt, sizeof (txt) / sizeof (TCHAR));
		SendDlgItemMessage(hDlg, IDC_REFRESHRATE, CB_SELECTSTRING, i, (LPARAM)txt);
		workprefs.gfx_refreshrate = 0;
	}
}

#define MAX_FRAMERATE_LENGTH 40
#define MAX_NTH_LENGTH 20

static int display_mode_index (uae_u32 x, uae_u32 y, uae_u32 d)
{
	int i, j;
	struct MultiDisplay *md = getdisplay (&workprefs);

	j = 0;
	for (i = 0; md->DisplayModes[i].depth >= 0; i++) {
		if (md->DisplayModes[i].res.width == x &&
			md->DisplayModes[i].res.height == y &&
			md->DisplayModes[i].depth == d)
			break;
		j++;
	}
	if(md->DisplayModes[i].depth < 0)
		j = -1;
	return j;
}

static int da_mode_selected;

static int *getp_da (void)
{
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
	}
	return p;
}

static void update_da(void)
{
	currprefs.gfx_gamma = workprefs.gfx_gamma;
	currprefs.gfx_luminance = workprefs.gfx_luminance;
	currprefs.gfx_contrast = workprefs.gfx_contrast;
	init_colors ();
	init_custom();
	updatedisplayarea ();
	WIN32GFX_WindowMove ();
}
static void handle_da (HWND hDlg)
{
	int *p;
	int v;

	p = getp_da ();
	if (!p)
		return;
	v = SendDlgItemMessage (hDlg, IDC_DA_SLIDER, TBM_GETPOS, 0, 0) * 10;
	if (v == *p)
		return;
	*p = v;
	update_da();
}

void init_da (HWND hDlg)
{
	int *p;
	SendDlgItemMessage(hDlg, IDC_DA_MODE, CB_RESETCONTENT, 0, 0);
	SendDlgItemMessage(hDlg, IDC_DA_MODE, CB_ADDSTRING, 0, (LPARAM)L"Brightness");
	SendDlgItemMessage(hDlg, IDC_DA_MODE, CB_ADDSTRING, 0, (LPARAM)L"Contrast");
	SendDlgItemMessage(hDlg, IDC_DA_MODE, CB_ADDSTRING, 0, (LPARAM)L"Gamma");
	if (da_mode_selected == CB_ERR)
		da_mode_selected = 0;
	SendDlgItemMessage (hDlg, IDC_DA_MODE, CB_SETCURSEL, da_mode_selected, 0);
	SendDlgItemMessage (hDlg, IDC_DA_SLIDER, TBM_SETPAGESIZE, 0, 1);
	SendDlgItemMessage (hDlg, IDC_DA_SLIDER, TBM_SETRANGE, TRUE, MAKELONG (-99, 99));
	p = getp_da ();
	if (p)
		SendDlgItemMessage (hDlg, IDC_DA_SLIDER, TBM_SETPOS, TRUE, (*p) / 10);
}

static int gui_display_depths[3];
static void init_display_mode (HWND hDlg)
{
	int d, d2, index;
	int i, cnt;
	struct MultiDisplay *md = getdisplay (&workprefs);

	switch (workprefs.color_mode)
	{
	case 2:
		d = 16;
		break;
	case 5:
	default:
		d = 32;
		break;
	}

	if (workprefs.gfx_afullscreen) {
		d2 = d;
		if ((index = WIN32GFX_AdjustScreenmode (md, &workprefs.gfx_size_fs.width, &workprefs.gfx_size_fs.height, &d2)) >= 0) {
			switch (d2)
			{
			case 15:
			case 16:
				workprefs.color_mode = 2;
				d = 2;
				break;
			case 32:
			default:
				workprefs.color_mode = 5;
				d = 4;
				break;
			}
		}
	} else {
		d = d / 8;
	}

	index = display_mode_index (workprefs.gfx_size_fs.width, workprefs.gfx_size_fs.height, d);
	if (index >= 0)
		SendDlgItemMessage (hDlg, IDC_RESOLUTION, CB_SETCURSEL, md->DisplayModes[index].residx, 0);
	else
		index = 0;
	SendDlgItemMessage(hDlg, IDC_RESOLUTIONDEPTH, CB_RESETCONTENT, 0, 0);
	cnt = 0;
	gui_display_depths[0] = gui_display_depths[1] = gui_display_depths[2] = -1;
	for (i = 0; md->DisplayModes[i].depth >= 0; i++) {
		if (md->DisplayModes[i].depth > 1 && md->DisplayModes[i].residx == md->DisplayModes[index].residx) {
			TCHAR tmp[64];
			_stprintf (tmp, L"%d", md->DisplayModes[i].depth * 8);
			SendDlgItemMessage(hDlg, IDC_RESOLUTIONDEPTH, CB_ADDSTRING, 0, (LPARAM)tmp);
			if (md->DisplayModes[i].depth == d)
				SendDlgItemMessage (hDlg, IDC_RESOLUTIONDEPTH, CB_SETCURSEL, cnt, 0);
			gui_display_depths[cnt] = md->DisplayModes[i].depth;
			cnt++;
		}
	}
	init_frequency_combo (hDlg, index);

}

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

static void values_to_displaydlg (HWND hDlg)
{
	TCHAR buffer[MAX_DPATH], buffer2[MAX_DPATH];
	TCHAR Nth[MAX_NTH_LENGTH];
	TCHAR *blah[1] = { Nth };
	TCHAR *string = NULL;
	int v;

	init_display_mode (hDlg);

	SetDlgItemInt (hDlg, IDC_XSIZE, workprefs.gfx_size_win.width, FALSE);
	SetDlgItemInt (hDlg, IDC_YSIZE, workprefs.gfx_size_win.height, FALSE);

	v = workprefs.chipset_refreshrate;
	if (v == 0)
		v = currprefs.ntscmode ? 60 : 50;
	SendDlgItemMessage (hDlg, IDC_FRAMERATE2, TBM_SETPOS, TRUE, v);
	_stprintf (buffer, L"%d", v);
	SetDlgItemText (hDlg, IDC_RATE2TEXT, buffer);

	v = workprefs.cpu_cycle_exact ? 1 : workprefs.gfx_framerate;
	SendDlgItemMessage (hDlg, IDC_FRAMERATE, TBM_SETPOS, TRUE, v);
	WIN32GUI_LoadUIString(IDS_FRAMERATE, buffer, sizeof buffer / sizeof (TCHAR));
	LoadNthString (v - 1, Nth, MAX_NTH_LENGTH);
	if (FormatMessage (FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY | FORMAT_MESSAGE_ALLOCATE_BUFFER,
		buffer, 0, 0, (LPTSTR)&string, MAX_FRAMERATE_LENGTH + MAX_NTH_LENGTH, (va_list *)blah ) == 0)
	{
		DWORD dwLastError = GetLastError();
		_stprintf (buffer, L"Every %s Frame", nth[v - 1]);
		SetDlgItemText(hDlg, IDC_RATETEXT, buffer);
	} else {
		SetDlgItemText( hDlg, IDC_RATETEXT, string);
		LocalFree(string);
	}

	CheckRadioButton (hDlg, IDC_LM_NORMAL, IDC_LM_SCANLINES, IDC_LM_NORMAL + workprefs.gfx_vresolution + (workprefs.gfx_scanlines ? 1 : 0));

	SendDlgItemMessage(hDlg, IDC_SCREENMODE_NATIVE, CB_RESETCONTENT, 0, 0);

	WIN32GUI_LoadUIString(IDS_SCREEN_WINDOWED, buffer, sizeof buffer / sizeof (TCHAR));
	SendDlgItemMessage(hDlg, IDC_SCREENMODE_NATIVE, CB_ADDSTRING, 0, (LPARAM)buffer);

	WIN32GUI_LoadUIString(IDS_SCREEN_FULLSCREEN, buffer, sizeof buffer / sizeof (TCHAR));
	SendDlgItemMessage(hDlg, IDC_SCREENMODE_NATIVE, CB_ADDSTRING, 0, (LPARAM)buffer);

	WIN32GUI_LoadUIString(IDS_SCREEN_VSYNC, buffer2, sizeof buffer2 / sizeof (TCHAR));
	_stprintf (buffer + _tcslen (buffer), L" + %s", buffer2);
	SendDlgItemMessage(hDlg, IDC_SCREENMODE_NATIVE, CB_ADDSTRING, 0, (LPARAM)buffer);

	WIN32GUI_LoadUIString(IDS_SCREEN_FULLSCREEN, buffer, sizeof buffer / sizeof (TCHAR));
	WIN32GUI_LoadUIString(IDS_SCREEN_VSYNC_AUTOSWITCH, buffer2, sizeof buffer2 / sizeof (TCHAR));
	_stprintf (buffer + _tcslen (buffer), L" + %s", buffer2);
	SendDlgItemMessage(hDlg, IDC_SCREENMODE_NATIVE, CB_ADDSTRING, 0, (LPARAM)buffer);


	WIN32GUI_LoadUIString(IDS_SCREEN_FULLWINDOW, buffer, sizeof buffer / sizeof (TCHAR));
	SendDlgItemMessage(hDlg, IDC_SCREENMODE_NATIVE, CB_ADDSTRING, 0, (LPARAM)buffer);

	SendDlgItemMessage(hDlg, IDC_SCREENMODE_NATIVE, CB_SETCURSEL,
		display_toselect (workprefs.gfx_afullscreen, workprefs.gfx_avsync, 0), 0);


	SendDlgItemMessage(hDlg, IDC_SCREENMODE_RTG, CB_RESETCONTENT, 0, 0);
	WIN32GUI_LoadUIString(IDS_SCREEN_WINDOWED, buffer, sizeof buffer / sizeof (TCHAR));
	SendDlgItemMessage(hDlg, IDC_SCREENMODE_RTG, CB_ADDSTRING, 0, (LPARAM)buffer);
	_stprintf (buffer + _tcslen (buffer), L" + %s", buffer2);
	SendDlgItemMessage(hDlg, IDC_SCREENMODE_RTG, CB_ADDSTRING, 0, (LPARAM)buffer);
	WIN32GUI_LoadUIString(IDS_SCREEN_FULLSCREEN, buffer, sizeof buffer / sizeof (TCHAR));
	SendDlgItemMessage(hDlg, IDC_SCREENMODE_RTG, CB_ADDSTRING, 0, (LPARAM)buffer);
	WIN32GUI_LoadUIString(IDS_SCREEN_VSYNC, buffer2, sizeof buffer2 / sizeof (TCHAR));
	_stprintf (buffer + _tcslen (buffer), L" + %s", buffer2);
	SendDlgItemMessage(hDlg, IDC_SCREENMODE_RTG, CB_ADDSTRING, 0, (LPARAM)buffer);
	WIN32GUI_LoadUIString(IDS_SCREEN_FULLWINDOW, buffer, sizeof buffer / sizeof (TCHAR));
	SendDlgItemMessage(hDlg, IDC_SCREENMODE_RTG, CB_ADDSTRING, 0, (LPARAM)buffer);
	_stprintf (buffer + _tcslen (buffer), L" + %s", buffer2);
	SendDlgItemMessage(hDlg, IDC_SCREENMODE_RTG, CB_ADDSTRING, 0, (LPARAM)buffer);
	SendDlgItemMessage(hDlg, IDC_SCREENMODE_RTG, CB_SETCURSEL,
		display_toselect (workprefs.gfx_pfullscreen, workprefs.gfx_pvsync, 1), 0);

	SendDlgItemMessage(hDlg, IDC_LORES, CB_RESETCONTENT, 0, 0);
	WIN32GUI_LoadUIString(IDS_RES_LORES, buffer, sizeof buffer / sizeof (TCHAR));
	SendDlgItemMessage(hDlg, IDC_LORES, CB_ADDSTRING, 0, (LPARAM)buffer);
	WIN32GUI_LoadUIString(IDS_RES_HIRES, buffer, sizeof buffer / sizeof (TCHAR));
	SendDlgItemMessage(hDlg, IDC_LORES, CB_ADDSTRING, 0, (LPARAM)buffer);
	WIN32GUI_LoadUIString(IDS_RES_SUPERHIRES, buffer, sizeof buffer / sizeof (TCHAR));
	SendDlgItemMessage(hDlg, IDC_LORES, CB_ADDSTRING, 0, (LPARAM)buffer);
	SendDlgItemMessage (hDlg, IDC_LORES, CB_SETCURSEL, workprefs.gfx_resolution, 0);

	CheckDlgButton (hDlg, IDC_BLACKER_THAN_BLACK, workprefs.gfx_blackerthanblack);
	CheckDlgButton (hDlg, IDC_LORES_SMOOTHED, workprefs.gfx_lores_mode);
	CheckDlgButton (hDlg, IDC_FLICKERFIXER, workprefs.gfx_scandoubler);

	CheckDlgButton (hDlg, IDC_XCENTER, workprefs.gfx_xcenter);
	CheckDlgButton (hDlg, IDC_YCENTER, workprefs.gfx_ycenter);

	CheckDlgButton (hDlg, IDC_AUTORESOLUTION, workprefs.gfx_autoresolution);

	SendDlgItemMessage(hDlg, IDC_DISPLAY_BUFFERCNT, CB_RESETCONTENT, 0, 0);
	WIN32GUI_LoadUIString(IDS_BUFFER_SINGLE, buffer, sizeof buffer / sizeof (TCHAR));
	SendDlgItemMessage(hDlg, IDC_DISPLAY_BUFFERCNT, CB_ADDSTRING, 0, (LPARAM)buffer);
	WIN32GUI_LoadUIString(IDS_BUFFER_DOUBLE, buffer, sizeof buffer / sizeof (TCHAR));
	SendDlgItemMessage(hDlg, IDC_DISPLAY_BUFFERCNT, CB_ADDSTRING, 0, (LPARAM)buffer);
	WIN32GUI_LoadUIString(IDS_BUFFER_TRIPLE, buffer, sizeof buffer / sizeof (TCHAR));
	SendDlgItemMessage(hDlg, IDC_DISPLAY_BUFFERCNT, CB_ADDSTRING, 0, (LPARAM)buffer);
	SendDlgItemMessage (hDlg, IDC_DISPLAY_BUFFERCNT, CB_SETCURSEL, workprefs.gfx_backbuffers, 0);

	init_da (hDlg);
}

static void init_resolution_combo (HWND hDlg)
{
	int i, idx;
	TCHAR tmp[64];
	struct MultiDisplay *md = getdisplay (&workprefs);

	idx = -1;
	SendDlgItemMessage(hDlg, IDC_RESOLUTION, CB_RESETCONTENT, 0, 0);
	for (i = 0; md->DisplayModes[i].depth >= 0; i++) {
		if (md->DisplayModes[i].depth > 1 && md->DisplayModes[i].residx != idx) {
			_stprintf (tmp, L"%dx%d", md->DisplayModes[i].res.width, md->DisplayModes[i].res.height);
			if (md->DisplayModes[i].nondx)
				_tcscat (tmp, L" (*)");
			SendDlgItemMessage(hDlg, IDC_RESOLUTION, CB_ADDSTRING, 0, (LPARAM)tmp);
			idx = md->DisplayModes[i].residx;
		}
	}
}
static void init_displays_combo (HWND hDlg)
{
	int i = 0;
	SendDlgItemMessage (hDlg, IDC_DISPLAYSELECT, CB_RESETCONTENT, 0, 0);
	while (Displays[i].name) {
		SendDlgItemMessage (hDlg, IDC_DISPLAYSELECT, CB_ADDSTRING, 0, (LPARAM)Displays[i].name);
		i++;
	}
	if (workprefs.gfx_display >= i)
		workprefs.gfx_display = 0;
	SendDlgItemMessage (hDlg, IDC_DISPLAYSELECT, CB_SETCURSEL, workprefs.gfx_display, 0);
}

static void values_from_displaydlg (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	BOOL success = FALSE;
	int i, j;
	int gfx_width = workprefs.gfx_size_win.width;
	int gfx_height = workprefs.gfx_size_win.height;
	LRESULT posn;

	display_fromselect (SendDlgItemMessage (hDlg, IDC_SCREENMODE_NATIVE, CB_GETCURSEL, 0, 0),
		&workprefs.gfx_afullscreen, &workprefs.gfx_avsync, 0);
	display_fromselect (SendDlgItemMessage (hDlg, IDC_SCREENMODE_RTG, CB_GETCURSEL, 0, 0),
		&workprefs.gfx_pfullscreen, &workprefs.gfx_pvsync, 1);

	workprefs.gfx_lores_mode     = ischecked (hDlg, IDC_LORES_SMOOTHED);
	workprefs.gfx_scandoubler     = ischecked (hDlg, IDC_FLICKERFIXER);
	workprefs.gfx_blackerthanblack = ischecked (hDlg, IDC_BLACKER_THAN_BLACK);
	workprefs.gfx_vresolution = (ischecked (hDlg, IDC_LM_DOUBLED) || ischecked (hDlg, IDC_LM_SCANLINES)) ? VRES_DOUBLE : VRES_NONDOUBLE;
	workprefs.gfx_scanlines = ischecked (hDlg, IDC_LM_SCANLINES);	
	workprefs.gfx_backbuffers = SendDlgItemMessage (hDlg, IDC_DISPLAY_BUFFERCNT, CB_GETCURSEL, 0, 0);
	workprefs.gfx_framerate = SendDlgItemMessage (hDlg, IDC_FRAMERATE, TBM_GETPOS, 0, 0);
	workprefs.chipset_refreshrate = SendDlgItemMessage (hDlg, IDC_FRAMERATE2, TBM_GETPOS, 0, 0);

	{
		TCHAR buffer[MAX_FRAMERATE_LENGTH];
		TCHAR Nth[MAX_NTH_LENGTH];
		TCHAR *blah[1] = { Nth };
		TCHAR *string = NULL;

		WIN32GUI_LoadUIString(IDS_FRAMERATE, buffer, MAX_FRAMERATE_LENGTH);
		LoadNthString(workprefs.gfx_framerate - 1, Nth, MAX_NTH_LENGTH);
		if(FormatMessage(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY | FORMAT_MESSAGE_ALLOCATE_BUFFER,
			buffer, 0, 0, (LPTSTR)&string, MAX_FRAMERATE_LENGTH + MAX_NTH_LENGTH, (va_list *)blah ) == 0)
		{
			DWORD dwLastError = GetLastError();
			_stprintf (buffer, L"Every %s Frame", nth[workprefs.gfx_framerate - 1]);
			SetDlgItemText(hDlg, IDC_RATETEXT, buffer);
		}
		else
		{
			SetDlgItemText(hDlg, IDC_RATETEXT, string);
			LocalFree(string);
		}
		_stprintf (buffer, L"%d", workprefs.chipset_refreshrate);
		SetDlgItemText (hDlg, IDC_RATE2TEXT, buffer);
		workprefs.gfx_size_win.width  = GetDlgItemInt(hDlg, IDC_XSIZE, &success, FALSE);
		if(!success)
			workprefs.gfx_size_win.width = 800;
		workprefs.gfx_size_win.height = GetDlgItemInt(hDlg, IDC_YSIZE, &success, FALSE);
		if(!success)
			workprefs.gfx_size_win.height = 600;
	}
	if (workprefs.chipset_refreshrate == (currprefs.ntscmode ? 60 : 50))
		workprefs.chipset_refreshrate = 0;
	workprefs.gfx_xcenter = ischecked (hDlg, IDC_XCENTER) ? 2 : 0; /* Smart centering */
	workprefs.gfx_ycenter = ischecked (hDlg, IDC_YCENTER) ? 2 : 0; /* Smart centering */
	workprefs.gfx_autoresolution = ischecked (hDlg, IDC_AUTORESOLUTION);

	if (msg == WM_COMMAND && HIWORD (wParam) == CBN_SELCHANGE)
	{
		if (LOWORD (wParam) == IDC_DISPLAYSELECT) {
			posn = SendDlgItemMessage (hDlg, IDC_DISPLAYSELECT, CB_GETCURSEL, 0, 0);
			if (posn != CB_ERR && posn != workprefs.gfx_display) {
				if (Displays[posn].disabled)
					posn = 0;
				workprefs.gfx_display = posn;
				init_resolution_combo (hDlg);
				init_display_mode (hDlg);
			}
			return;
		} else if (LOWORD (wParam) == IDC_LORES) {
			posn = SendDlgItemMessage (hDlg, IDC_LORES, CB_GETCURSEL, 0, 0);
			if (posn != CB_ERR)
				workprefs.gfx_resolution = posn;
		} else if (LOWORD (wParam) == IDC_RESOLUTION || LOWORD(wParam) == IDC_RESOLUTIONDEPTH) {
			struct MultiDisplay *md = getdisplay (&workprefs);
			LRESULT posn1, posn2;
			posn1 = SendDlgItemMessage (hDlg, IDC_RESOLUTION, CB_GETCURSEL, 0, 0);
			if (posn1 == CB_ERR)
				return;
			posn2 = SendDlgItemMessage (hDlg, IDC_RESOLUTIONDEPTH, CB_GETCURSEL, 0, 0);
			if (posn2 == CB_ERR)
				return;
			for (i = 0; md->DisplayModes[i].depth >= 0; i++) {
				if (md->DisplayModes[i].residx == posn1)
					break;
			}
			if (md->DisplayModes[i].depth < 0)
				return;
			j = i;
			while (md->DisplayModes[i].residx == posn1) {
				if (md->DisplayModes[i].depth == gui_display_depths[posn2])
					break;
				i++;
			}
			if (md->DisplayModes[i].residx != posn1)
				i = j;
			workprefs.gfx_size_fs.width  = md->DisplayModes[i].res.width;
			workprefs.gfx_size_fs.height = md->DisplayModes[i].res.height;
			switch(md->DisplayModes[i].depth)
			{
			case 2:
				workprefs.color_mode = 2;
				break;
			case 3:
			case 4:
				workprefs.color_mode = 5;
				break;
			default:
				workprefs.color_mode = 0;
				break;
			}
			/* Set the Int boxes */
			SetDlgItemInt (hDlg, IDC_XSIZE, workprefs.gfx_size_win.width, FALSE);
			SetDlgItemInt (hDlg, IDC_YSIZE, workprefs.gfx_size_win.height, FALSE);
			init_frequency_combo (hDlg, i);
		} else if (LOWORD (wParam) == IDC_REFRESHRATE) {
			LRESULT posn1, posn2;
			posn1 = SendDlgItemMessage (hDlg, IDC_REFRESHRATE, CB_GETCURSEL, 0, 0);
			if (posn1 == CB_ERR)
				return;
			posn2 = SendDlgItemMessage (hDlg, IDC_RESOLUTION, CB_GETCURSEL, 0, 0);
			if (posn2 == CB_ERR)
				return;
			if (posn1 == 0) {
				workprefs.gfx_refreshrate = 0;
			} else {
				posn1--;
				workprefs.gfx_refreshrate = storedrefreshrates[posn1].rate;
			}
			values_to_displaydlg (hDlg);
		} else if (LOWORD (wParam) == IDC_DA_MODE) {
			da_mode_selected = SendDlgItemMessage (hDlg, IDC_DA_MODE, CB_GETCURSEL, 0, 0);
			init_da (hDlg);
			handle_da (hDlg);
		}
	}

	updatewinfsmode (&workprefs);
}

static int hw3d_changed;

static INT_PTR CALLBACK DisplayDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;

	switch (msg)
	{
	case WM_INITDIALOG:
		pages[DISPLAY_ID] = hDlg;
		currentpage = DISPLAY_ID;
		SendDlgItemMessage (hDlg, IDC_FRAMERATE, TBM_SETPAGESIZE, 0, 1);
		SendDlgItemMessage (hDlg, IDC_FRAMERATE, TBM_SETRANGE, TRUE, MAKELONG (MIN_REFRESH_RATE, MAX_REFRESH_RATE));
		SendDlgItemMessage (hDlg, IDC_FRAMERATE2, TBM_SETPAGESIZE, 0, 1);
		SendDlgItemMessage (hDlg, IDC_FRAMERATE2, TBM_SETRANGE, TRUE, MAKELONG (1, 99));
		init_displays_combo (hDlg);
		init_resolution_combo (hDlg);
		init_da (hDlg);

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
		if (LOWORD (wParam) == IDC_DA_RESET) {
			int *p;
			da_mode_selected = SendDlgItemMessage (hDlg, IDC_DA_MODE, CB_GETCURSEL, 0, 0);
			p = getp_da();
			if (p)
				*p = 0;
			init_da(hDlg);
			update_da();
		}
		handle_da (hDlg);
		values_from_displaydlg (hDlg, msg, wParam, lParam);
		enable_for_displaydlg (hDlg);
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
	case 0:
		CheckRadioButton(hDlg, IDC_OCS, IDC_AGA, IDC_OCS + 0);
		break;
	case CSMASK_ECS_AGNUS:
		CheckRadioButton(hDlg, IDC_OCS, IDC_AGA, IDC_OCS + 1);
		break;
	case CSMASK_ECS_DENISE:
		CheckRadioButton(hDlg, IDC_OCS, IDC_AGA, IDC_OCS + 2);
		break;
	case CSMASK_ECS_AGNUS | CSMASK_ECS_DENISE:
		CheckRadioButton(hDlg, IDC_OCS, IDC_AGA, IDC_OCS + 3);
		break;
	case CSMASK_AGA:
	case CSMASK_ECS_AGNUS | CSMASK_ECS_DENISE | CSMASK_AGA:
		CheckRadioButton(hDlg, IDC_OCS, IDC_AGA, IDC_OCS + 4);
		break;
	}
	CheckDlgButton (hDlg, IDC_NTSC, workprefs.ntscmode);
	CheckDlgButton (hDlg, IDC_GENLOCK, workprefs.genlock);
	CheckDlgButton (hDlg, IDC_BLITIMM, workprefs.immediate_blits);
	CheckRadioButton (hDlg, IDC_COLLISION0, IDC_COLLISION3, IDC_COLLISION0 + workprefs.collision_level);
	CheckDlgButton (hDlg, IDC_CYCLEEXACT, workprefs.cpu_cycle_exact);
	SendDlgItemMessage (hDlg, IDC_CS_EXT, CB_RESETCONTENT, 0, 0);
	SendDlgItemMessage (hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)L"");
	SendDlgItemMessage (hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)L"Generic");
	SendDlgItemMessage (hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)L"CDTV");
	SendDlgItemMessage (hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)L"CD32");
	SendDlgItemMessage (hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)L"A500");
	SendDlgItemMessage (hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)L"A500+");
	SendDlgItemMessage (hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)L"A600");
	SendDlgItemMessage (hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)L"A1000");
	SendDlgItemMessage (hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)L"A1200");
	SendDlgItemMessage (hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)L"A2000");
	SendDlgItemMessage (hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)L"A3000");
	SendDlgItemMessage (hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)L"A3000T");
	SendDlgItemMessage (hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)L"A4000");
	SendDlgItemMessage (hDlg, IDC_CS_EXT, CB_ADDSTRING, 0, (LPARAM)L"A4000T");
	SendDlgItemMessage (hDlg, IDC_CS_EXT, CB_SETCURSEL, workprefs.cs_compatible, 0);
}

static void values_from_chipsetdlg (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	BOOL success = FALSE;
	int nn;
	bool n;

	workprefs.genlock = ischecked (hDlg, IDC_GENLOCK);
	workprefs.immediate_blits = ischecked (hDlg, IDC_BLITIMM);
	n = ischecked (hDlg, IDC_CYCLEEXACT);
	if (workprefs.cpu_cycle_exact != n) {
		workprefs.cpu_cycle_exact = workprefs.blitter_cycle_exact = n;
		if (n) {
			if (workprefs.cpu_model == 68000)
				workprefs.cpu_compatible = 1;
			if (workprefs.cpu_model <= 68020)
				workprefs.m68k_speed = 0;
			workprefs.immediate_blits = 0;
			workprefs.gfx_framerate = 1;
			workprefs.cachesize = 0;
		}
	}
	workprefs.collision_level = ischecked (hDlg, IDC_COLLISION0) ? 0
		: ischecked (hDlg, IDC_COLLISION1) ? 1
		: ischecked (hDlg, IDC_COLLISION2) ? 2 : 3;
	workprefs.chipset_mask = ischecked (hDlg, IDC_OCS) ? 0
		: ischecked (hDlg, IDC_ECS_AGNUS) ? CSMASK_ECS_AGNUS
		: ischecked (hDlg, IDC_ECS_DENISE) ? CSMASK_ECS_DENISE
		: ischecked (hDlg, IDC_ECS) ? CSMASK_ECS_AGNUS | CSMASK_ECS_DENISE
		: CSMASK_AGA | CSMASK_ECS_AGNUS | CSMASK_ECS_DENISE;
	n = ischecked (hDlg, IDC_NTSC);
	if (workprefs.ntscmode != n) {
		workprefs.ntscmode = n;
	}
	nn = SendDlgItemMessage (hDlg, IDC_CS_EXT, CB_GETCURSEL, 0, 0);
	if (nn != CB_ERR) {
		workprefs.cs_compatible = nn;
		built_in_chipset_prefs (&workprefs);
	}
}

static INT_PTR CALLBACK ChipsetDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;

	switch (msg) {
	case WM_INITDIALOG:
		pages[CHIPSET_ID] = hDlg;
		currentpage = CHIPSET_ID;
#ifndef	AGA
		ew (hDlg, IDC_AGA, FALSE);
#endif

	case WM_USER:
		recursive++;
		values_to_chipsetdlg (hDlg);
		enable_for_chipsetdlg (hDlg);
		recursive--;
		break;

	case WM_HSCROLL:
	case WM_COMMAND:
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
		CheckRadioButton(hDlg, IDC_CS_RTC1, IDC_CS_RTC3, IDC_CS_RTC1);
		break;
	case 1:
		CheckRadioButton(hDlg, IDC_CS_RTC1, IDC_CS_RTC3, IDC_CS_RTC2);
		break;
	case 2:
		CheckRadioButton(hDlg, IDC_CS_RTC1, IDC_CS_RTC3, IDC_CS_RTC3);
		break;
	}
	CheckDlgButton (hDlg, IDC_CS_COMPATIBLE, workprefs.cs_compatible);
	CheckDlgButton (hDlg, IDC_CS_RESETWARNING, workprefs.cs_resetwarning);
	CheckDlgButton (hDlg, IDC_CS_NOEHB, workprefs.cs_denisenoehb);
	CheckDlgButton (hDlg, IDC_CS_DIPAGNUS, workprefs.cs_dipagnus);
	CheckDlgButton (hDlg, IDC_CS_KSMIRROR_E0, workprefs.cs_ksmirror_e0);
	CheckDlgButton (hDlg, IDC_CS_KSMIRROR_A8, workprefs.cs_ksmirror_a8);
	CheckDlgButton (hDlg, IDC_CS_CIAOVERLAY, workprefs.cs_ciaoverlay);
	CheckDlgButton (hDlg, IDC_CS_DF0IDHW, workprefs.cs_df0idhw);
	CheckDlgButton (hDlg, IDC_CS_CD32CD, workprefs.cs_cd32cd);
	CheckDlgButton (hDlg, IDC_CS_CD32C2P, workprefs.cs_cd32c2p);
	CheckDlgButton (hDlg, IDC_CS_CD32NVRAM, workprefs.cs_cd32nvram);
	CheckDlgButton (hDlg, IDC_CS_CDTVCD, workprefs.cs_cdtvcd);
	CheckDlgButton (hDlg, IDC_CS_CDTVRAM, workprefs.cs_cdtvram);
	CheckDlgButton (hDlg, IDC_CS_CDTVRAMEXP, workprefs.cs_cdtvcard);
	CheckDlgButton (hDlg, IDC_CS_A1000RAM, workprefs.cs_a1000ram);
	CheckDlgButton (hDlg, IDC_CS_RAMSEY, workprefs.cs_ramseyrev >= 0);
	CheckDlgButton (hDlg, IDC_CS_FATGARY, workprefs.cs_fatgaryrev >= 0);
	CheckDlgButton (hDlg, IDC_CS_AGNUS, workprefs.cs_agnusrev >= 0);
	CheckDlgButton (hDlg, IDC_CS_DENISE, workprefs.cs_deniserev >= 0);
	CheckDlgButton (hDlg, IDC_CS_DMAC, workprefs.cs_mbdmac == 1);
	CheckDlgButton (hDlg, IDC_CS_DMAC2, workprefs.cs_mbdmac == 2);
	CheckDlgButton (hDlg, IDC_CS_A2091, workprefs.cs_a2091);
	CheckDlgButton (hDlg, IDC_CS_A4091, workprefs.cs_a4091);
	CheckDlgButton (hDlg, IDC_CS_CDTVSCSI, workprefs.cs_cdtvscsi);
	CheckDlgButton (hDlg, IDC_CS_SCSIMODE, workprefs.scsi == 2);
	CheckDlgButton (hDlg, IDC_CS_PCMCIA, workprefs.cs_pcmcia);
	CheckDlgButton (hDlg, IDC_CS_SLOWISFAST, workprefs.cs_slowmemisfast);
	CheckDlgButton (hDlg, IDC_CS_IDE1, workprefs.cs_ide > 0 && (workprefs.cs_ide & 1));
	CheckDlgButton (hDlg, IDC_CS_IDE2, workprefs.cs_ide > 0 && (workprefs.cs_ide & 2));
	txt[0] = 0;
	_stprintf (txt, L"%d", workprefs.cs_rtc_adjust);
	SetDlgItemText(hDlg, IDC_CS_RTCADJUST, txt);
	txt[0] = 0;
	if (workprefs.cs_fatgaryrev >= 0)
		_stprintf (txt, L"%02X", workprefs.cs_fatgaryrev);
	SetDlgItemText(hDlg, IDC_CS_FATGARYREV, txt);
	txt[0] = 0;
	if (workprefs.cs_ramseyrev >= 0)
		_stprintf (txt, L"%02X", workprefs.cs_ramseyrev);
	SetDlgItemText(hDlg, IDC_CS_RAMSEYREV, txt);
	txt[0] = 0;
	if (workprefs.cs_agnusrev >= 0) {
		rev = workprefs.cs_agnusrev;
		_stprintf (txt, L"%02X", rev);
	} else if (workprefs.cs_compatible) {
		rev = 0;
		if (workprefs.ntscmode)
			rev |= 0x10;
		rev |= (workprefs.chipset_mask & CSMASK_AGA) ? 0x23 : 0;
		rev |= (currprefs.chipset_mask & CSMASK_ECS_AGNUS) ? 0x20 : 0;
		if (workprefs.chipmem_size > 1024 * 1024 && (workprefs.chipset_mask & CSMASK_ECS_AGNUS))
			rev |= 0x21;
		_stprintf (txt, L"%02X", rev);
	}
	SetDlgItemText(hDlg, IDC_CS_AGNUSREV, txt);
	txt[0] = 0;
	if (workprefs.cs_deniserev >= 0) {
		rev = workprefs.cs_deniserev;
		_stprintf (txt, L"%01.1X", rev);
	} else if (workprefs.cs_compatible) {
		rev = 0xf;
		if (workprefs.chipset_mask & CSMASK_ECS_DENISE)
			rev = 0xc;
		if (workprefs.chipset_mask & CSMASK_AGA)
			rev = 0x8;
		_stprintf (txt, L"%01.1X", rev);
	}
	SetDlgItemText(hDlg, IDC_CS_DENISEREV, txt);
}

static void values_from_chipsetdlg2 (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	TCHAR txt[32], *p;
	int v;

	workprefs.cs_compatible = ischecked (hDlg, IDC_CS_COMPATIBLE);
	workprefs.cs_resetwarning = ischecked (hDlg, IDC_CS_RESETWARNING);
	workprefs.cs_denisenoehb = ischecked (hDlg, IDC_CS_NOEHB);
	workprefs.cs_dipagnus = ischecked (hDlg, IDC_CS_DIPAGNUS);
	workprefs.cs_agnusbltbusybug = workprefs.cs_dipagnus;
	workprefs.cs_ksmirror_e0 = ischecked (hDlg, IDC_CS_KSMIRROR_E0);
	workprefs.cs_ksmirror_a8 = ischecked (hDlg, IDC_CS_KSMIRROR_A8);
	workprefs.cs_ciaoverlay = ischecked (hDlg, IDC_CS_CIAOVERLAY);
	workprefs.cs_df0idhw = ischecked (hDlg, IDC_CS_DF0IDHW);
	workprefs.cs_cd32cd = ischecked (hDlg, IDC_CS_CD32CD);
	workprefs.cs_cd32c2p = ischecked (hDlg, IDC_CS_CD32C2P);
	workprefs.cs_cd32nvram = ischecked (hDlg, IDC_CS_CD32NVRAM);
	workprefs.cs_cdtvcd = ischecked (hDlg, IDC_CS_CDTVCD);
	workprefs.cs_cdtvram = ischecked (hDlg, IDC_CS_CDTVRAM);
	workprefs.cs_cdtvcard = ischecked (hDlg, IDC_CS_CDTVRAMEXP) ? 64 : 0;
	workprefs.cs_a1000ram = ischecked (hDlg, IDC_CS_A1000RAM);
	workprefs.cs_ramseyrev = ischecked (hDlg, IDC_CS_RAMSEY) ? 0x0f : -1;
	workprefs.cs_fatgaryrev = ischecked (hDlg, IDC_CS_FATGARY) ? 0x00 : -1;
	workprefs.cs_mbdmac = ischecked (hDlg, IDC_CS_DMAC) ? 1 : 0;
	if (workprefs.cs_mbdmac == 0)
		workprefs.cs_mbdmac = ischecked (hDlg, IDC_CS_DMAC2) ? 2 : 0;
	workprefs.cs_a2091 = ischecked (hDlg, IDC_CS_A2091) ? 1 : 0;
	workprefs.cs_a4091 = ischecked (hDlg, IDC_CS_A4091) ? 1 : 0;
#if 0
	if (msg == WM_COMMAND && LOWORD(wParam) == IDC_CS_SCSIMODE)
		workprefs.scsi = ischecked (hDlg, IDC_CS_SCSIMODE) ? 2 : 0;
#endif
	workprefs.cs_cdtvscsi = ischecked (hDlg, IDC_CS_CDTVSCSI) ? 1 : 0;
	workprefs.cs_pcmcia = ischecked (hDlg, IDC_CS_PCMCIA) ? 1 : 0;
	workprefs.cs_slowmemisfast = ischecked (hDlg, IDC_CS_SLOWISFAST) ? 1 : 0;
	workprefs.cs_ide = ischecked (hDlg, IDC_CS_IDE1) ? 1 : (ischecked (hDlg, IDC_CS_IDE2) ? 2 : 0);
	workprefs.cs_ciaatod = ischecked (hDlg, IDC_CS_CIAA_TOD1) ? 0
		: (ischecked (hDlg, IDC_CS_CIAA_TOD2) ? 1 : 2);
	workprefs.cs_rtc = ischecked (hDlg, IDC_CS_RTC1) ? 0
		: (ischecked (hDlg, IDC_CS_RTC2) ? 1 : 2);

	if (workprefs.cs_rtc) {
		txt[0] = 0;
		SendDlgItemMessage (hDlg, IDC_CS_RTCADJUST, WM_GETTEXT, (WPARAM)sizeof (txt) / sizeof (TCHAR), (LPARAM)txt);
		workprefs.cs_rtc_adjust = _tstol(txt);
	}
	if (workprefs.cs_fatgaryrev >= 0) {
		txt[0] = 0;
		SendDlgItemMessage (hDlg, IDC_CS_FATGARYREV, WM_GETTEXT, (WPARAM)sizeof (txt) / sizeof (TCHAR), (LPARAM)txt);
		v = _tcstol (txt, &p, 16);
		if (v >= 0 && v <= 255)
			workprefs.cs_fatgaryrev = v;
	}
	if (workprefs.cs_ramseyrev >= 0) {
		txt[0] = 0;
		SendDlgItemMessage (hDlg, IDC_CS_RAMSEYREV, WM_GETTEXT, (WPARAM)sizeof (txt) / sizeof (TCHAR), (LPARAM)txt);
		v = _tcstol (txt, &p, 16);
		if (v >= 0 && v <= 255)
			workprefs.cs_ramseyrev = v;
	}
	if (workprefs.cs_agnusrev >= 0) {
		txt[0] = 0;
		SendDlgItemMessage (hDlg, IDC_CS_AGNUSREV, WM_GETTEXT, (WPARAM)sizeof (txt) / sizeof (TCHAR), (LPARAM)txt);
		v = _tcstol (txt, &p, 16);
		if (v >= 0 && v <= 255)
			workprefs.cs_agnusrev = v;
	}
	if (workprefs.cs_deniserev >= 0) {
		txt[0] = 0;
		SendDlgItemMessage (hDlg, IDC_CS_DENISEREV, WM_GETTEXT, (WPARAM)sizeof (txt) / sizeof (TCHAR), (LPARAM)txt);
		v = _tcstol (txt, &p, 16);
		if (v >= 0 && v <= 15)
			workprefs.cs_deniserev = v;
	}

}

static void enable_for_chipsetdlg2 (HWND hDlg)
{
	int e = workprefs.cs_compatible ? FALSE : TRUE;

	ew (hDlg, IDC_CS_FATGARY, e);
	ew (hDlg, IDC_CS_RAMSEY, e);
	ew (hDlg, IDC_CS_AGNUS, e);
	ew (hDlg, IDC_CS_DENISE, e);
	ew (hDlg, IDC_CS_FATGARYREV, e);
	ew (hDlg, IDC_CS_RAMSEYREV, e);
	ew (hDlg, IDC_CS_AGNUSREV, e);
	ew (hDlg, IDC_CS_DENISEREV, e);
	ew (hDlg, IDC_CS_IDE1, e);
	ew (hDlg, IDC_CS_IDE2, e);
	ew (hDlg, IDC_CS_DMAC, e);
	ew (hDlg, IDC_CS_DMAC2, e);
	ew (hDlg, IDC_CS_A2091, e);
	ew (hDlg, IDC_CS_A4091, e);
	ShowWindow (GetDlgItem(hDlg, IDC_CS_SCSIMODE), SW_HIDE);
	ew (hDlg, IDC_CS_SCSIMODE, FALSE);
	ew (hDlg, IDC_CS_CDTVSCSI, e);
	ew (hDlg, IDC_CS_PCMCIA, e);
	ew (hDlg, IDC_CS_SLOWISFAST, e);
	ew (hDlg, IDC_CS_CD32CD, e);
	ew (hDlg, IDC_CS_CD32NVRAM, e);
	ew (hDlg, IDC_CS_CD32C2P, e);
	ew (hDlg, IDC_CS_CDTVCD, e);
	ew (hDlg, IDC_CS_CDTVRAM, e);
	ew (hDlg, IDC_CS_CDTVRAMEXP, e);
	ew (hDlg, IDC_CS_RESETWARNING, e);
	ew (hDlg, IDC_CS_NOEHB, e);
	ew (hDlg, IDC_CS_DIPAGNUS, e);
	ew (hDlg, IDC_CS_KSMIRROR_E0, e);
	ew (hDlg, IDC_CS_KSMIRROR_A8, e);
	ew (hDlg, IDC_CS_CIAOVERLAY, e);
	ew (hDlg, IDC_CS_A1000RAM, e);
	ew (hDlg, IDC_CS_DF0IDHW, e);
	ew (hDlg, IDC_CS_CIAA_TOD1, e);
	ew (hDlg, IDC_CS_CIAA_TOD2, e);
	ew (hDlg, IDC_CS_CIAA_TOD3, e);
	ew (hDlg, IDC_CS_RTC1, e);
	ew (hDlg, IDC_CS_RTC2, e);
	ew (hDlg, IDC_CS_RTC3, e);
	ew (hDlg, IDC_CS_RTCADJUST, e);
}

static INT_PTR CALLBACK ChipsetDlgProc2 (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;

	switch (msg) {
	case WM_INITDIALOG:
		pages[CHIPSET2_ID] = hDlg;
		currentpage = CHIPSET2_ID;
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

static void enable_for_memorydlg (HWND hDlg)
{
	int z3 = ! workprefs.address_space_24;
	int fast = workprefs.chipmem_size <= 0x200000;
	int rtg = workprefs.gfxmem_size && full_property_sheet;
	int rtg2 = workprefs.gfxmem_size;

#ifndef AUTOCONFIG
	z3 = FALSE;
	fast = FALSE;
#endif
	ew (hDlg, IDC_Z3TEXT, z3);
	ew (hDlg, IDC_Z3FASTRAM, z3);
	ew (hDlg, IDC_Z3FASTMEM, z3);
	ew (hDlg, IDC_Z3CHIPRAM, z3);
	ew (hDlg, IDC_Z3CHIPMEM, z3);
	ew (hDlg, IDC_FASTMEM, fast);
	ew (hDlg, IDC_FASTRAM, fast);
	ew (hDlg, IDC_FASTTEXT, fast);
	ew (hDlg, IDC_GFXCARDTEXT, z3);
	ew (hDlg, IDC_P96RAM, z3);
	ew (hDlg, IDC_P96MEM, z3);
	ew (hDlg, IDC_MBRAM1, z3);
	ew (hDlg, IDC_MBMEM1, z3);
	ew (hDlg, IDC_MBRAM2, z3);
	ew (hDlg, IDC_MBMEM2, z3);

	ew (hDlg, IDC_RTG_8BIT, rtg);
	ew (hDlg, IDC_RTG_16BIT, rtg);
	ew (hDlg, IDC_RTG_24BIT, rtg);
	ew (hDlg, IDC_RTG_32BIT, rtg);
	ew (hDlg, IDC_RTG_MATCH_DEPTH, rtg2);
	ew (hDlg, IDC_RTG_SCALE, rtg2);
	ew (hDlg, IDC_RTG_SCALE_ALLOW, rtg2);
	ew (hDlg, IDC_RTG_SCALE_ASPECTRATIO, rtg2);
	ew (hDlg, IDC_RTG_VBLANKRATE, rtg2);
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

	switch (workprefs.chipmem_size) {
	case 0x00040000: mem_size = 0; break;
	case 0x00080000: mem_size = 1; break;
	case 0x00100000: mem_size = 2; break;
	case 0x00180000: mem_size = 3; break;
	case 0x00200000: mem_size = 4; break;
	case 0x00400000: mem_size = 5; break;
	case 0x00800000: mem_size = 6; break;
	}
	SendDlgItemMessage (hDlg, IDC_CHIPMEM, TBM_SETPOS, TRUE, mem_size);
	SetDlgItemText (hDlg, IDC_CHIPRAM, memsize_names[msi_chip[mem_size]]);

	mem_size = 0;
	switch (workprefs.fastmem_size) {
	case 0x00000000: mem_size = 0; break;
	case 0x00100000: mem_size = 1; break;
	case 0x00200000: mem_size = 2; break;
	case 0x00400000: mem_size = 3; break;
	case 0x00800000: mem_size = 4; break;
	case 0x01000000: mem_size = 5; break;
	}
	SendDlgItemMessage (hDlg, IDC_FASTMEM, TBM_SETPOS, TRUE, mem_size);
	SetDlgItemText (hDlg, IDC_FASTRAM, memsize_names[msi_fast[mem_size]]);

	mem_size = 0;
	switch (workprefs.bogomem_size) {
	case 0x00000000: mem_size = 0; break;
	case 0x00080000: mem_size = 1; break;
	case 0x00100000: mem_size = 2; break;
	case 0x00180000: mem_size = 3; break;
	case 0x001C0000: mem_size = 4; break;
	}
	SendDlgItemMessage (hDlg, IDC_SLOWMEM, TBM_SETPOS, TRUE, mem_size);
	SetDlgItemText (hDlg, IDC_SLOWRAM, memsize_names[msi_bogo[mem_size]]);

	mem_size = 0;
	v = workprefs.z3fastmem_size + workprefs.z3fastmem2_size;
	if      (v < 0x00100000)
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
	else if (v < 0x18000000)
		mem_size = 9;
	else if (v < 0x20000000)
		mem_size = 10;
	else if (v < 0x30000000)
		mem_size = 11;
	else if (v < 0x40000000) // 1GB
		mem_size = 12;
	else if (v < 0x60000000) // 1.5GB
		mem_size = 13;
	else if (v < 0x80000000) // 2GB
		mem_size = 14;
	else if (v < 0xA8000000) // 2.5GB
		mem_size = 15;
	else if (v < 0xC0000000) // 3GB
		mem_size = 16;
	else
		mem_size = 17;
	SendDlgItemMessage (hDlg, IDC_Z3FASTMEM, TBM_SETPOS, TRUE, mem_size);
	SetDlgItemText (hDlg, IDC_Z3FASTRAM, memsize_names[msi_z3fast[mem_size]]);

	mem_size = 0;
	v = workprefs.z3chipmem_size;
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
	else if (v < 0x20000000)
		mem_size = 5;
	else if (v < 0x40000000)
		mem_size = 6;
	else
		mem_size = 7;
	SendDlgItemMessage (hDlg, IDC_Z3CHIPMEM, TBM_SETPOS, TRUE, mem_size);
	SetDlgItemText (hDlg, IDC_Z3CHIPRAM, memsize_names[msi_z3chip[mem_size]]);

	mem_size = 0;
	switch (workprefs.gfxmem_size) {
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
	SendDlgItemMessage (hDlg, IDC_P96MEM, TBM_SETPOS, TRUE, mem_size);
	SetDlgItemText (hDlg, IDC_P96RAM, memsize_names[msi_gfx[mem_size]]);
	SendDlgItemMessage (hDlg, IDC_RTG_8BIT, CB_SETCURSEL, (workprefs.picasso96_modeflags & RGBFF_CLUT) ? 1 : 0, 0);
	SendDlgItemMessage (hDlg, IDC_RTG_16BIT, CB_SETCURSEL,
		(manybits (workprefs.picasso96_modeflags, RGBFF_R5G6B5PC | RGBFF_R5G6B5PC | RGBFF_R5G6B5 | RGBFF_R5G5B5 | RGBFF_B5G6R5PC | RGBFF_B5G5R5PC)) ? 1 :
		(workprefs.picasso96_modeflags & RGBFF_R5G6B5PC) ? 2 :
		(workprefs.picasso96_modeflags & RGBFF_R5G5B5PC) ? 3 :
		(workprefs.picasso96_modeflags & RGBFF_R5G6B5) ? 4 :
		(workprefs.picasso96_modeflags & RGBFF_R5G5B5) ? 5 :
		(workprefs.picasso96_modeflags & RGBFF_B5G6R5PC) ? 6 :
		(workprefs.picasso96_modeflags & RGBFF_B5G5R5PC) ? 7 : 0, 0);
	SendDlgItemMessage (hDlg, IDC_RTG_24BIT, CB_SETCURSEL,
		(manybits (workprefs.picasso96_modeflags, RGBFF_R8G8B8 | RGBFF_B8G8R8)) ? 1 :
		(workprefs.picasso96_modeflags & RGBFF_R8G8B8) ? 2 :
		(workprefs.picasso96_modeflags & RGBFF_B8G8R8) ? 3 : 0, 0);
	SendDlgItemMessage (hDlg, IDC_RTG_32BIT, CB_SETCURSEL,
		(manybits (workprefs.picasso96_modeflags, RGBFF_A8R8G8B8 | RGBFF_A8B8G8R8 | RGBFF_R8G8B8A8 | RGBFF_B8G8R8A8)) ? 1 :
		(workprefs.picasso96_modeflags & RGBFF_A8R8G8B8) ? 2 :
		(workprefs.picasso96_modeflags & RGBFF_A8B8G8R8) ? 3 :
		(workprefs.picasso96_modeflags & RGBFF_R8G8B8A8) ? 4 :
		(workprefs.picasso96_modeflags & RGBFF_B8G8R8A8) ? 5 : 0, 0);
	if (workprefs.win32_rtgvblankrate <= 0 ||
		workprefs.win32_rtgvblankrate == 50 ||
		workprefs.win32_rtgvblankrate == 60 ||
		workprefs.win32_rtgvblankrate == 70 ||
		workprefs.win32_rtgvblankrate == 75) {
			SendDlgItemMessage (hDlg, IDC_RTG_VBLANKRATE, CB_SETCURSEL,
				(workprefs.win32_rtgvblankrate == 0) ? 1 :
				(workprefs.win32_rtgvblankrate == -1) ? 2 :
				(workprefs.win32_rtgvblankrate == -2) ? 0 :
				(workprefs.win32_rtgvblankrate == 50) ? 3 :
				(workprefs.win32_rtgvblankrate == 60) ? 4 :
				(workprefs.win32_rtgvblankrate == 70) ? 5 :
				(workprefs.win32_rtgvblankrate == 75) ? 6 : 0, 0);
	} else {
		TCHAR tmp[10];
		_stprintf (tmp, L"%d", workprefs.win32_rtgvblankrate);
		SendDlgItemMessage (hDlg, IDC_RTG_VBLANKRATE, WM_SETTEXT, 0, (LPARAM)tmp);
	}


	CheckDlgButton (hDlg, IDC_RTG_SCALE, workprefs.win32_rtgscaleifsmall);
	CheckDlgButton (hDlg, IDC_RTG_SCALE_ALLOW, workprefs.win32_rtgallowscaling);
	CheckDlgButton (hDlg, IDC_RTG_MATCH_DEPTH, workprefs.win32_rtgmatchdepth);

	SendDlgItemMessage (hDlg, IDC_RTG_SCALE_ASPECTRATIO, CB_SETCURSEL,
		(workprefs.win32_rtgscaleaspectratio == 0) ? 0 :
		(workprefs.win32_rtgscaleaspectratio == 4 * 256 + 3) ? 2 :
		(workprefs.win32_rtgscaleaspectratio == 5 * 256 + 4) ? 3 :
		(workprefs.win32_rtgscaleaspectratio == 15 * 256 + 9) ? 4 :
		(workprefs.win32_rtgscaleaspectratio == 16 * 256 + 9) ? 5 :
		(workprefs.win32_rtgscaleaspectratio == 16 * 256 + 10) ? 6 : 1, 0);

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
	SendDlgItemMessage (hDlg, IDC_MBMEM1, TBM_SETPOS, TRUE, mem_size);
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
	}
	SendDlgItemMessage (hDlg, IDC_MBMEM2, TBM_SETPOS, TRUE, mem_size);
	SetDlgItemText (hDlg, IDC_MBRAM2, memsize_names[msi_gfx[mem_size]]);
}

static void fix_values_memorydlg (void)
{
	if (workprefs.chipmem_size > 0x200000)
		workprefs.fastmem_size = 0;
}
static void updatez3 (uae_u32 *size1p, uae_u32 *size2p)
{
	int i;
	uae_u32 s1, s2;

	// no 2GB Z3 size so we need 2x1G
	if (*size1p >= 0x80000000) {
		*size2p = *size1p - 0x40000000;
		*size1p = 0x40000000;
		return;
	}
	s1 = *size1p;
	*size1p = 0;
	*size2p = 0;
	s2 = 0;
	for (i = 32; i >= 0; i--) {
		if (s1 & (1 << i))
			break;
	}
	if (i < 20)
		return;
	if (s1 == (1 << i)) {
		*size1p = s1;
		return;
	}
	s2 = s1 & ((1 << i) - 1);
	s1 = 1 << i;
	i--;
	while (i >= 0) {
		if (s2 & (1 << i)) {
			s2 = 1 << i;
			break;
		}
		i--;
	}
	if (i < 19)
		s2 = 0;
	*size1p = s1;
	*size2p = s2;
}

static struct netdriverdata *ndd;

static void expansion_net (HWND hDlg)
{
	int i, cnt;
	TCHAR tmp[MAX_DPATH];

	SendDlgItemMessage (hDlg, IDC_NETDEVICE, CB_RESETCONTENT, 0, 0);
	WIN32GUI_LoadUIString (IDS_NETDISCONNECTED, tmp, sizeof tmp / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_NETDEVICE, CB_ADDSTRING, 0, (LPARAM)tmp);
	if (!_tcscmp (workprefs.a2065name, L"none"))
		SendDlgItemMessage (hDlg, IDC_NETDEVICE, CB_SETCURSEL, 0, 0);
	cnt = 1;
	for (i = 0; ndd && i < MAX_TOTAL_NET_DEVICES; i++) {
		if (ndd[i].active) {
			TCHAR mac[20];
			_stprintf (mac, L"%02X:%02X:%02X:%02X:%02X:%02X",
				ndd[i].mac[0], ndd[i].mac[1], ndd[i].mac[2], ndd[i].mac[3], ndd[i].mac[4], ndd[i].mac[5]);
			_stprintf (tmp, L"%s %s", mac, ndd[i].desc);
			SendDlgItemMessage (hDlg, IDC_NETDEVICE, CB_ADDSTRING, 0, (LPARAM)tmp);
			if (!_tcsicmp (workprefs.a2065name, mac) || !_tcsicmp (workprefs.a2065name, ndd[i].name))
				SendDlgItemMessage (hDlg, IDC_NETDEVICE, CB_SETCURSEL, cnt, 0);
			cnt++;
		}
	}
}

static void enable_for_expansiondlg (HWND hDlg)
{
	int cw, en;

	en = !!full_property_sheet;
	cw = catweasel_detect ();
	ew (hDlg, IDC_CATWEASEL, cw && en);
	ew (hDlg, IDC_SOCKETS, en);
	ew (hDlg, IDC_SCSIDEVICE, en);
	ew (hDlg, IDC_CATWEASEL, en);
	ew (hDlg, IDC_NETDEVICE, en);
	ew (hDlg, IDC_SANA2, en);
	ew (hDlg, IDC_A2065, en);
	ew (hDlg, IDC_NETDEVICE, en && workprefs.a2065name[0]);
}

static void values_to_expansiondlg (HWND hDlg)
{
	int cw;

	CheckDlgButton (hDlg, IDC_SOCKETS, workprefs.socket_emu);
	CheckDlgButton (hDlg, IDC_CATWEASEL, workprefs.catweasel);
	CheckDlgButton (hDlg, IDC_SCSIDEVICE, workprefs.scsi == 1);
	CheckDlgButton (hDlg, IDC_SANA2, workprefs.sana2);
	CheckDlgButton (hDlg, IDC_A2065, workprefs.a2065name[0] ? 1 : 0);
	cw = catweasel_detect ();
	ew (hDlg, IDC_CATWEASEL, cw);
	if (!cw && workprefs.catweasel < 100)
		workprefs.catweasel = 0;
}

static INT_PTR CALLBACK ExpansionDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	int v;
	TCHAR tmp[100];
	static int recursive = 0;
	static int enumerated;

	switch (msg)
	{
	case WM_INITDIALOG:
		pages[EXPANSION_ID] = hDlg;
		currentpage = EXPANSION_ID;

		if (!enumerated) {
			uaenet_enumerate (&ndd, NULL);
			enumerated = 1;
		}
		expansion_net (hDlg);
		WIN32GUI_LoadUIString(IDS_ALL, tmp, sizeof tmp / sizeof (TCHAR));
		SendDlgItemMessage (hDlg, IDC_RTG_8BIT, CB_RESETCONTENT, 0, 0);
		SendDlgItemMessage (hDlg, IDC_RTG_8BIT, CB_ADDSTRING, 0, (LPARAM)L"(8bit)");
		SendDlgItemMessage (hDlg, IDC_RTG_8BIT, CB_ADDSTRING, 0, (LPARAM)L"8-bit (*)");
		SendDlgItemMessage (hDlg, IDC_RTG_16BIT, CB_RESETCONTENT, 0, 0);
		SendDlgItemMessage (hDlg, IDC_RTG_16BIT, CB_ADDSTRING, 0, (LPARAM)L"(15/16bit)");
		SendDlgItemMessage (hDlg, IDC_RTG_16BIT, CB_ADDSTRING, 0, (LPARAM)tmp);
		SendDlgItemMessage (hDlg, IDC_RTG_16BIT, CB_ADDSTRING, 0, (LPARAM)L"R5G6B5PC (*)");
		SendDlgItemMessage (hDlg, IDC_RTG_16BIT, CB_ADDSTRING, 0, (LPARAM)L"R5G5B5PC");
		SendDlgItemMessage (hDlg, IDC_RTG_16BIT, CB_ADDSTRING, 0, (LPARAM)L"R5G6B5");
		SendDlgItemMessage (hDlg, IDC_RTG_16BIT, CB_ADDSTRING, 0, (LPARAM)L"R5G5B5");
		SendDlgItemMessage (hDlg, IDC_RTG_16BIT, CB_ADDSTRING, 0, (LPARAM)L"B5G6R5PC");
		SendDlgItemMessage (hDlg, IDC_RTG_16BIT, CB_ADDSTRING, 0, (LPARAM)L"B5G5R5PC");
		SendDlgItemMessage (hDlg, IDC_RTG_24BIT, CB_RESETCONTENT, 0, 0);
		SendDlgItemMessage (hDlg, IDC_RTG_24BIT, CB_ADDSTRING, 0, (LPARAM)L"(24bit)");
		SendDlgItemMessage (hDlg, IDC_RTG_24BIT, CB_ADDSTRING, 0, (LPARAM)tmp);
		SendDlgItemMessage (hDlg, IDC_RTG_24BIT, CB_ADDSTRING, 0, (LPARAM)L"R8G8B8");
		SendDlgItemMessage (hDlg, IDC_RTG_24BIT, CB_ADDSTRING, 0, (LPARAM)L"B8G8R8");
		SendDlgItemMessage (hDlg, IDC_RTG_32BIT, CB_RESETCONTENT, 0, 0);
		SendDlgItemMessage (hDlg, IDC_RTG_32BIT, CB_ADDSTRING, 0, (LPARAM)L"(32bit)");
		SendDlgItemMessage (hDlg, IDC_RTG_32BIT, CB_ADDSTRING, 0, (LPARAM)tmp);
		SendDlgItemMessage (hDlg, IDC_RTG_32BIT, CB_ADDSTRING, 0, (LPARAM)L"A8R8G8B8");
		SendDlgItemMessage (hDlg, IDC_RTG_32BIT, CB_ADDSTRING, 0, (LPARAM)L"A8B8G8R8");
		SendDlgItemMessage (hDlg, IDC_RTG_32BIT, CB_ADDSTRING, 0, (LPARAM)L"R8G8B8A8");
		SendDlgItemMessage (hDlg, IDC_RTG_32BIT, CB_ADDSTRING, 0, (LPARAM)L"B8G8R8A8 (*)");
		SendDlgItemMessage (hDlg, IDC_P96MEM, TBM_SETRANGE, TRUE, MAKELONG (MIN_P96_MEM, MAX_P96_MEM));
		SendDlgItemMessage (hDlg, IDC_RTG_SCALE_ASPECTRATIO, CB_RESETCONTENT, 0, 0);
		WIN32GUI_LoadUIString (IDS_DISABLED, tmp, sizeof tmp / sizeof (TCHAR));
		SendDlgItemMessage (hDlg, IDC_RTG_SCALE_ASPECTRATIO, CB_ADDSTRING, 0, (LPARAM)tmp);
		WIN32GUI_LoadUIString (IDS_AUTOMATIC, tmp, sizeof tmp / sizeof (TCHAR));
		SendDlgItemMessage (hDlg, IDC_RTG_SCALE_ASPECTRATIO, CB_ADDSTRING, 0, (LPARAM)tmp);
		SendDlgItemMessage (hDlg, IDC_RTG_SCALE_ASPECTRATIO, CB_ADDSTRING, 0, (LPARAM)L"4:3");
		SendDlgItemMessage (hDlg, IDC_RTG_SCALE_ASPECTRATIO, CB_ADDSTRING, 0, (LPARAM)L"5:4");
		SendDlgItemMessage (hDlg, IDC_RTG_SCALE_ASPECTRATIO, CB_ADDSTRING, 0, (LPARAM)L"15:9");
		SendDlgItemMessage (hDlg, IDC_RTG_SCALE_ASPECTRATIO, CB_ADDSTRING, 0, (LPARAM)L"16:9");
		SendDlgItemMessage (hDlg, IDC_RTG_SCALE_ASPECTRATIO, CB_ADDSTRING, 0, (LPARAM)L"16:10");
		SendDlgItemMessage (hDlg, IDC_RTG_VBLANKRATE, CB_RESETCONTENT, 0, 0);
		SendDlgItemMessage (hDlg, IDC_RTG_VBLANKRATE, CB_ADDSTRING, 0, (LPARAM)L"Disabled");
		SendDlgItemMessage (hDlg, IDC_RTG_VBLANKRATE, CB_ADDSTRING, 0, (LPARAM)L"Chipset");
		SendDlgItemMessage (hDlg, IDC_RTG_VBLANKRATE, CB_ADDSTRING, 0, (LPARAM)L"Real");
		SendDlgItemMessage (hDlg, IDC_RTG_VBLANKRATE, CB_ADDSTRING, 0, (LPARAM)L"50");
		SendDlgItemMessage (hDlg, IDC_RTG_VBLANKRATE, CB_ADDSTRING, 0, (LPARAM)L"60");
		SendDlgItemMessage (hDlg, IDC_RTG_VBLANKRATE, CB_ADDSTRING, 0, (LPARAM)L"70");
		SendDlgItemMessage (hDlg, IDC_RTG_VBLANKRATE, CB_ADDSTRING, 0, (LPARAM)L"75");

	case WM_USER:
		recursive++;
		values_to_expansiondlg (hDlg);
		enable_for_expansiondlg (hDlg);
		values_to_memorydlg (hDlg);
		enable_for_memorydlg (hDlg);
		recursive--;
		break;

	case WM_HSCROLL:
		workprefs.gfxmem_size = memsizes[msi_gfx[SendMessage (GetDlgItem (hDlg, IDC_P96MEM), TBM_GETPOS, 0, 0)]];
		values_to_memorydlg (hDlg);
		enable_for_memorydlg (hDlg);
		break;

	case WM_COMMAND:
		{
			if (recursive > 0)
				break;
			recursive++;
			switch (LOWORD (wParam))
			{
			case IDC_RTG_MATCH_DEPTH:
				workprefs.win32_rtgmatchdepth = ischecked (hDlg, IDC_RTG_MATCH_DEPTH);
				break;
			case IDC_RTG_SCALE:
				workprefs.win32_rtgscaleifsmall = ischecked (hDlg, IDC_RTG_SCALE);
				break;
			case IDC_RTG_SCALE_ALLOW:
				workprefs.win32_rtgallowscaling = ischecked (hDlg, IDC_RTG_SCALE_ALLOW);
				break;
			case IDC_SOCKETS:
				workprefs.socket_emu = ischecked (hDlg, IDC_SOCKETS);
				break;
			case IDC_SCSIDEVICE:
				workprefs.scsi = ischecked (hDlg, IDC_SCSIDEVICE);
				enable_for_expansiondlg (hDlg);
				break;
			case IDC_SANA2:
				workprefs.sana2 = ischecked (hDlg, IDC_SANA2);
				break;
			case IDC_A2065:
				if (ischecked (hDlg, IDC_A2065)) {
					_tcscpy (workprefs.a2065name, L"none");
					expansion_net (hDlg);
					enable_for_expansiondlg (hDlg);
				} else {
					ew (hDlg, IDC_NETDEVICE, FALSE);
					workprefs.a2065name[0] = 0;
					enable_for_expansiondlg (hDlg);
				}
				break;
			case IDC_CATWEASEL:
				workprefs.catweasel = ischecked (hDlg, IDC_CATWEASEL) ? -1 : 0;
				break;
			}
			if (HIWORD (wParam) == CBN_SELENDOK || HIWORD (wParam) == CBN_KILLFOCUS || HIWORD (wParam) == CBN_EDITCHANGE)  {
				uae_u32 mask = workprefs.picasso96_modeflags;
				switch (LOWORD (wParam))
				{
				case IDC_RTG_SCALE_ASPECTRATIO:
					v = SendDlgItemMessage (hDlg, IDC_RTG_SCALE_ASPECTRATIO, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR) {
						if (v == 0)
							workprefs.win32_rtgscaleaspectratio = 0;
						if (v == 1)
							workprefs.win32_rtgscaleaspectratio = -1;
						if (v == 2)
							workprefs.win32_rtgscaleaspectratio = 4 * 256 + 3;
						if (v == 3)
							workprefs.win32_rtgscaleaspectratio = 5 * 256 + 4;
						if (v == 4)
							workprefs.win32_rtgscaleaspectratio = 15 * 256 + 9;
						if (v == 5)
							workprefs.win32_rtgscaleaspectratio = 16 * 256 + 9;
						if (v == 6)
							workprefs.win32_rtgscaleaspectratio = 16 * 256 + 10;
					}
					break;
				case IDC_RTG_8BIT:
					v = SendDlgItemMessage (hDlg, IDC_RTG_8BIT, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR) {
						mask &= ~RGBFF_CLUT;
						if (v == 1)
							mask |= RGBFF_CLUT;
					}
					break;
				case IDC_RTG_16BIT:
					v = SendDlgItemMessage (hDlg, IDC_RTG_16BIT, CB_GETCURSEL, 0, 0L);
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
					v = SendDlgItemMessage (hDlg, IDC_RTG_24BIT, CB_GETCURSEL, 0, 0L);
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
					v = SendDlgItemMessage (hDlg, IDC_RTG_32BIT, CB_GETCURSEL, 0, 0L);
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
					v = SendDlgItemMessage (hDlg, IDC_RTG_VBLANKRATE, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR) {
						if (v == 0) {
							workprefs.win32_rtgvblankrate = -2;
						} else if (v == 1) {
							workprefs.win32_rtgvblankrate = 0;
						} else if (v == 2) {
							workprefs.win32_rtgvblankrate = -1;
						} else {
							v = SendDlgItemMessage (hDlg, IDC_RTG_VBLANKRATE, CB_GETLBTEXT, (WPARAM)v, (LPARAM)tmp);
						}
					} else {
						v = SendDlgItemMessage (hDlg, IDC_RTG_VBLANKRATE, WM_GETTEXT, (WPARAM)sizeof tmp / sizeof (TCHAR), (LPARAM)tmp);
					}
					if (tmp[0])
						workprefs.win32_rtgvblankrate = _tstol (tmp);
					break;
				case IDC_NETDEVICE:
					v = SendDlgItemMessage (hDlg, IDC_NETDEVICE, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR) {
						if (v == 0) {
							_tcscpy (workprefs.a2065name, L"none");
						} else if (ndd) {
							v--;
							_tcscpy (workprefs.a2065name, ndd[v].name);
						}
					}
					break;

				}
				workprefs.picasso96_modeflags = mask;
				values_to_memorydlg (hDlg);
			}
			recursive--;
		}
		break;
	}
	return FALSE;
}


static INT_PTR CALLBACK MemoryDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;

	switch (msg)
	{
	case WM_INITDIALOG:
		pages[MEMORY_ID] = hDlg;
		currentpage = MEMORY_ID;
		SendDlgItemMessage (hDlg, IDC_CHIPMEM, TBM_SETRANGE, TRUE, MAKELONG (MIN_CHIP_MEM, MAX_CHIP_MEM));
		SendDlgItemMessage (hDlg, IDC_FASTMEM, TBM_SETRANGE, TRUE, MAKELONG (MIN_FAST_MEM, MAX_FAST_MEM));
		SendDlgItemMessage (hDlg, IDC_SLOWMEM, TBM_SETRANGE, TRUE, MAKELONG (MIN_SLOW_MEM, MAX_SLOW_MEM));
		SendDlgItemMessage (hDlg, IDC_Z3FASTMEM, TBM_SETRANGE, TRUE, MAKELONG (MIN_Z3_MEM, MAX_Z3_MEM));
		SendDlgItemMessage (hDlg, IDC_Z3CHIPMEM, TBM_SETRANGE, TRUE, MAKELONG (MIN_Z3_MEM, MAX_Z3_CHIPMEM));
		SendDlgItemMessage (hDlg, IDC_MBMEM1, TBM_SETRANGE, TRUE, MAKELONG (MIN_MB_MEM, MAX_MB_MEM));
		SendDlgItemMessage (hDlg, IDC_MBMEM2, TBM_SETRANGE, TRUE, MAKELONG (MIN_MB_MEM, MAX_MB_MEM));

	case WM_USER:
		recursive++;
		fix_values_memorydlg ();
		values_to_memorydlg (hDlg);
		enable_for_memorydlg (hDlg);
		recursive--;
		break;

	case WM_HSCROLL:
		workprefs.chipmem_size = memsizes[msi_chip[SendMessage (GetDlgItem (hDlg, IDC_CHIPMEM), TBM_GETPOS, 0, 0)]];
		workprefs.bogomem_size = memsizes[msi_bogo[SendMessage (GetDlgItem (hDlg, IDC_SLOWMEM), TBM_GETPOS, 0, 0)]];
		workprefs.fastmem_size = memsizes[msi_fast[SendMessage (GetDlgItem (hDlg, IDC_FASTMEM), TBM_GETPOS, 0, 0)]];
		workprefs.z3fastmem_size = memsizes[msi_z3fast[SendMessage (GetDlgItem (hDlg, IDC_Z3FASTMEM), TBM_GETPOS, 0, 0)]];
		updatez3 (&workprefs.z3fastmem_size, &workprefs.z3fastmem2_size);
		workprefs.z3chipmem_size = memsizes[msi_z3chip[SendMessage (GetDlgItem (hDlg, IDC_Z3CHIPMEM), TBM_GETPOS, 0, 0)]];
		workprefs.mbresmem_low_size = memsizes[msi_gfx[SendMessage (GetDlgItem (hDlg, IDC_MBMEM1), TBM_GETPOS, 0, 0)]];
		workprefs.mbresmem_high_size = memsizes[msi_gfx[SendMessage (GetDlgItem (hDlg, IDC_MBMEM2), TBM_GETPOS, 0, 0)]];
		fix_values_memorydlg ();
		values_to_memorydlg (hDlg);
		enable_for_memorydlg (hDlg);
		break;

	}
	return FALSE;
}

static void addromfiles (UAEREG *fkey, HWND hDlg, DWORD d, TCHAR *path, int type)
{
	int idx;
	TCHAR tmp[MAX_DPATH];
	TCHAR tmp2[MAX_DPATH];
	TCHAR seltmp[MAX_DPATH];
	struct romdata *rdx;

	rdx = scan_single_rom (path);
	SendDlgItemMessage(hDlg, d, CB_RESETCONTENT, 0, 0);
	SendDlgItemMessage(hDlg, d, CB_ADDSTRING, 0, (LPARAM)L"");
	idx = 0;
	seltmp[0] = 0;
	for (;fkey;) {
		int size = sizeof (tmp) / sizeof (TCHAR);
		int size2 = sizeof (tmp2) / sizeof (TCHAR);
		if (!regenumstr (fkey, idx, tmp, &size, tmp2, &size2))
			break;
		if (_tcslen (tmp) == 7 || _tcslen (tmp) == 13) {
			int group = 0;
			int subitem = 0;
			int idx2 = _tstol (tmp + 4);
			if (_tcslen (tmp) == 13) {
				group = _tstol (tmp + 8);
				subitem = _tstol (tmp + 11);
			}
			if (idx2 >= 0) {
				struct romdata *rd = getromdatabyidgroup (idx2, group, subitem);
				if (rd && (rd->type & type)) {
					getromname (rd, tmp);
					if (SendDlgItemMessage (hDlg, d, CB_FINDSTRING, (WPARAM)-1, (LPARAM)tmp) < 0)
						SendDlgItemMessage(hDlg, d, CB_ADDSTRING, 0, (LPARAM)tmp);
					if (rd == rdx)
						_tcscpy (seltmp, tmp);
				}
			}
		}
		idx++;
	}
	if (seltmp[0])
		SendDlgItemMessage (hDlg, d, CB_SELECTSTRING, (WPARAM)-1, (LPARAM)seltmp);
	else
		SetDlgItemText(hDlg, d, path);
}

static void getromfile (HWND hDlg, DWORD d, TCHAR *path, int size)
{
	LRESULT val = SendDlgItemMessage (hDlg, d, CB_GETCURSEL, 0, 0L);
	if (val == CB_ERR) {
		SendDlgItemMessage (hDlg, d, WM_GETTEXT, (WPARAM)size, (LPARAM)path);
	} else {
		TCHAR tmp1[MAX_DPATH];
		struct romdata *rd;
		SendDlgItemMessage (hDlg, d, CB_GETLBTEXT, (WPARAM)val, (LPARAM)tmp1);
		path[0] = 0;
		rd = getromdatabyname (tmp1);
		if (rd) {
			struct romlist *rl = getromlistbyromdata(rd);
			if (rd->configname)
				_stprintf (path, L":%s", rd->configname);
			else if (rl)
				_tcsncpy (path, rl->path, size);
		}
	}
}

static void values_from_kickstartdlg (HWND hDlg)
{
	getromfile (hDlg, IDC_ROMFILE, workprefs.romfile, sizeof (workprefs.romfile) / sizeof (TCHAR));
	getromfile (hDlg, IDC_ROMFILE2, workprefs.romextfile, sizeof (workprefs.romextfile) / sizeof (TCHAR));
	getromfile (hDlg, IDC_CARTFILE, workprefs.cartfile, sizeof (workprefs.cartfile) / sizeof (TCHAR));
}

static void values_to_kickstartdlg (HWND hDlg)
{
	UAEREG *fkey;

	fkey = regcreatetree (NULL, L"DetectedROMs");
	load_keyring(&workprefs, NULL);
	addromfiles (fkey, hDlg, IDC_ROMFILE, workprefs.romfile,
		ROMTYPE_KICK | ROMTYPE_KICKCD32);
	addromfiles (fkey, hDlg, IDC_ROMFILE2, workprefs.romextfile,
		ROMTYPE_EXTCD32 | ROMTYPE_EXTCDTV | ROMTYPE_ARCADIABIOS);
	addromfiles (fkey, hDlg, IDC_CARTFILE, workprefs.cartfile,
		ROMTYPE_AR | ROMTYPE_SUPERIV | ROMTYPE_NORDIC | ROMTYPE_XPOWER | ROMTYPE_ARCADIAGAME | ROMTYPE_HRTMON | ROMTYPE_CD32CART);
	regclosetree (fkey);

	SetDlgItemText(hDlg, IDC_FLASHFILE, workprefs.flashfile);
	CheckDlgButton(hDlg, IDC_KICKSHIFTER, workprefs.kickshifter);
	CheckDlgButton(hDlg, IDC_MAPROM, workprefs.maprom);
}

static void init_kickstart (HWND hDlg)
{
#if !defined(AUTOCONFIG)
	ew (hDlg, IDC_MAPROM), FALSE);
#endif
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
	if (!regexiststree (NULL , L"DetectedROMs"))
		scan_roms (NULL, rp_isactive () ? 0 : 1);
}

static void kickstartfilebuttons (HWND hDlg, WPARAM wParam, TCHAR *path)
{
	switch (LOWORD(wParam))
	{
	case IDC_KICKCHOOSER:
		DiskSelection(hDlg, IDC_ROMFILE, 6, &workprefs, path);
		values_to_kickstartdlg (hDlg);
		break;
	case IDC_ROMCHOOSER2:
		DiskSelection(hDlg, IDC_ROMFILE2, 6, &workprefs, path);
		values_to_kickstartdlg (hDlg);
		break;
	case IDC_FLASHCHOOSER:
		DiskSelection(hDlg, IDC_FLASHFILE, 11, &workprefs, path);
		values_to_kickstartdlg (hDlg);
		break;
	case IDC_CARTCHOOSER:
		DiskSelection(hDlg, IDC_CARTFILE, 6, &workprefs, path);
		values_to_kickstartdlg (hDlg);
		break;
	}
}

static INT_PTR CALLBACK KickstartDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive;
	TCHAR tmp[MAX_DPATH];

	switch (msg)
	{
	case WM_INITDIALOG:
		{
			int ids[] = { IDC_ROMFILE, IDC_ROMFILE2, IDC_CARTFILE, -1 };
			pages[KICKSTART_ID] = hDlg;
			currentpage = KICKSTART_ID;
			init_kickstart (hDlg);
			values_to_kickstartdlg (hDlg);
			setmultiautocomplete (hDlg, ids);
			setac (hDlg, IDC_FLASHFILE);
			return TRUE;
		}

	case WM_CONTEXTMENU:
		{
			int id = GetDlgCtrlID((HWND)wParam);
			if (id == IDC_KICKCHOOSER || id == IDC_ROMCHOOSER2
				|| id == IDC_FLASHCHOOSER || id == IDC_CARTCHOOSER) {
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
		if (HIWORD (wParam) == CBN_SELCHANGE || HIWORD (wParam) == CBN_KILLFOCUS)  {
			switch (LOWORD (wParam))
			{
			case IDC_ROMFILE:
			case IDC_ROMFILE2:
			case IDC_CARTFILE:
				values_from_kickstartdlg (hDlg);
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
		ew (hDlg, IDC_JULIAN, TRUE);
		ew (hDlg, IDC_CTRLF11, TRUE);
		ew (hDlg, IDC_SHOWGUI, FALSE);
		ew (hDlg, IDC_NOSPEED, TRUE);
		ew (hDlg, IDC_NOSPEEDPAUSE, TRUE);
		ew (hDlg, IDC_NOSOUND, TRUE);
		ew (hDlg, IDC_DOSAVESTATE, TRUE);
		ew (hDlg, IDC_SCSIMODE, FALSE);
		ew (hDlg, IDC_CLOCKSYNC, FALSE);
		ew (hDlg, IDC_CLIPBOARDSHARE, FALSE);
	} else {
#if !defined (SCSIEMU)
		EnableWindow (GetDlgItem(hDlg, IDC_SCSIMODE), TRUE);
#endif
		ew (hDlg, IDC_DOSAVESTATE, FALSE);
	}
	ew (hDlg, IDC_ASSOCIATELIST, !rp_isactive ());
	ew (hDlg, IDC_ASSOCIATE_ON, !rp_isactive ());
	ew (hDlg, IDC_ASSOCIATE_OFF, !rp_isactive ());
	ew (hDlg, IDC_DD_SURFACETYPE, full_property_sheet && workprefs.gfx_api == 0);
}

static void misc_kbled (HWND hDlg, int v, int nv)
{
	TCHAR *defname = v == IDC_KBLED1 ? L"(NumLock)" : v == IDC_KBLED2 ? L"(CapsLock)" : L"(ScrollLock)";
	SendDlgItemMessage (hDlg, v, CB_RESETCONTENT, 0, 0L);
	SendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)defname);
	SendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)L"POWER");
	SendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)L"DF0");
	SendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)L"DF1");
	SendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)L"DF2");
	SendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)L"DF3");
	SendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)L"HD");
	SendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)L"CD");
	SendDlgItemMessage (hDlg, v, CB_SETCURSEL, nv, 0);
}

static void misc_getkbled (HWND hDlg, int v, int n)
{
	LRESULT nv = SendDlgItemMessage (hDlg, v, CB_GETCURSEL, 0, 0L);
	if (nv != CB_ERR) {
		workprefs.keyboard_leds[n] = nv;
		misc_kbled (hDlg, v, nv);
	}
	workprefs.keyboard_leds_in_use = (workprefs.keyboard_leds[0] | workprefs.keyboard_leds[1] | workprefs.keyboard_leds[2]) != 0;
}

static void misc_getpri (HWND hDlg, int v, int *n)
{
	LRESULT nv = SendDlgItemMessage (hDlg, v, CB_GETCURSEL, 0, 0L);
	if (nv != CB_ERR)
		*n = nv;
}

static void misc_addpri (HWND hDlg, int v, int pri)
{
	int i;

	DWORD opri = GetPriorityClass (GetCurrentProcess ());
	ew (hDlg, v, !(opri != IDLE_PRIORITY_CLASS && opri != NORMAL_PRIORITY_CLASS && opri != BELOW_NORMAL_PRIORITY_CLASS && opri != ABOVE_NORMAL_PRIORITY_CLASS));
	SendDlgItemMessage (hDlg, v, CB_RESETCONTENT, 0, 0L);
	i = 0;
	while (priorities[i].name) {
		SendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)priorities[i].name);
		i++;
	}
	SendDlgItemMessage (hDlg, v, CB_SETCURSEL, pri, 0);


}

extern const TCHAR *get_aspi_path (int);

static void misc_scsi (HWND hDlg)
{
	SendDlgItemMessage (hDlg, IDC_SCSIMODE, CB_RESETCONTENT, 0, 0);
	SendDlgItemMessage (hDlg, IDC_SCSIMODE, CB_ADDSTRING, 0, (LPARAM)L"SCSI Emulation *");
	SendDlgItemMessage (hDlg, IDC_SCSIMODE, CB_ADDSTRING, 0, (LPARAM)L"SPTI");
	SendDlgItemMessage (hDlg, IDC_SCSIMODE, CB_ADDSTRING, 0, (LPARAM)L"SPTI + SCSI SCAN");
	SendDlgItemMessage (hDlg, IDC_SCSIMODE, CB_ADDSTRING, 0, (LPARAM)((get_aspi_path (0)) ? L"AdaptecASPI" : L"(AdaptecASPI)"));
	SendDlgItemMessage (hDlg, IDC_SCSIMODE, CB_ADDSTRING, 0, (LPARAM)((get_aspi_path (1)) ? L"NeroASPI" : L"(NeroASPI)"));
	SendDlgItemMessage (hDlg, IDC_SCSIMODE, CB_ADDSTRING, 0, (LPARAM)((get_aspi_path (2)) ? L"FrogASPI" : L"(FrogASPI)"));
	SendDlgItemMessage (hDlg, IDC_SCSIMODE, CB_SETCURSEL, workprefs.win32_uaescsimode, 0);
}

static void misc_lang (HWND hDlg)
{
	int i, idx = 0, cnt = 0, lid;
	WORD langid = -1;

	if (regqueryint (NULL, L"Language", &lid))
		langid = (WORD)lid;
	SendDlgItemMessage (hDlg, IDC_LANGUAGE, CB_RESETCONTENT, 0, 0);
	SendDlgItemMessage (hDlg, IDC_LANGUAGE, CB_ADDSTRING, 0, (LPARAM)L"Autodetect");
	SendDlgItemMessage (hDlg, IDC_LANGUAGE, CB_ADDSTRING, 0, (LPARAM)L"English (built-in)");
	if (langid == 0)
		idx = 1;
	cnt = 2;
	for (i = 0; langs[i].name; i++) {
		HMODULE hm = language_load (langs[i].id);
		if (hm) {
			FreeLibrary (hm);
			SendDlgItemMessage (hDlg, IDC_LANGUAGE, CB_ADDSTRING, 0, (LPARAM)langs[i].name);
			if (langs[i].id == langid)
				idx = cnt;
			cnt++;
		}
	}
	SendDlgItemMessage (hDlg, IDC_LANGUAGE, CB_SETCURSEL, idx, 0);
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
	regsetint (NULL, L"Language", langid);
	FreeLibrary(hUIDLL);
	hUIDLL = NULL;
	if (langid >= 0)
		hUIDLL = language_load(langid);
	restart_requested = 1;
	exit_gui(0);
}

static void values_to_miscdlg (HWND hDlg)
{
	if (currentpage == MISC1_ID) {

		CheckDlgButton (hDlg, IDC_FOCUSMINIMIZE, workprefs.win32_minimize_inactive);
		CheckDlgButton (hDlg, IDC_ILLEGAL, workprefs.illegal_mem);
		CheckDlgButton (hDlg, IDC_SHOWGUI, workprefs.start_gui);
		CheckDlgButton (hDlg, IDC_JULIAN, workprefs.win32_middle_mouse);
		CheckDlgButton (hDlg, IDC_CREATELOGFILE, workprefs.win32_logfile);
		CheckDlgButton (hDlg, IDC_CTRLF11, workprefs.win32_ctrl_F11_is_quit);
		CheckDlgButton (hDlg, IDC_SHOWLEDS, (workprefs.leds_on_screen & STATUSLINE_CHIPSET) ? 1 : 0);
		CheckDlgButton (hDlg, IDC_SHOWLEDSRTG, (workprefs.leds_on_screen & STATUSLINE_RTG) ? 1 : 0);
		CheckDlgButton (hDlg, IDC_NOTASKBARBUTTON, workprefs.win32_notaskbarbutton);
		CheckDlgButton (hDlg, IDC_ALWAYSONTOP, workprefs.win32_alwaysontop);
		CheckDlgButton (hDlg, IDC_CLOCKSYNC, workprefs.tod_hack);
		CheckDlgButton (hDlg, IDC_CLIPBOARDSHARE, workprefs.clipboard_sharing);
		CheckDlgButton (hDlg, IDC_POWERSAVE, workprefs.win32_powersavedisabled);
		CheckDlgButton (hDlg, IDC_FASTERRTG, workprefs.picasso96_nocustom);

		misc_kbled (hDlg, IDC_KBLED1, workprefs.keyboard_leds[0]);
		misc_kbled (hDlg, IDC_KBLED2, workprefs.keyboard_leds[1]);
		misc_kbled (hDlg, IDC_KBLED3, workprefs.keyboard_leds[2]);
		CheckDlgButton (hDlg, IDC_KBLED_USB, workprefs.win32_kbledmode);

		misc_scsi (hDlg);
		misc_lang (hDlg);

		SendDlgItemMessage (hDlg, IDC_DXMODE, CB_RESETCONTENT, 0, 0);
		SendDlgItemMessage (hDlg, IDC_DXMODE, CB_ADDSTRING, 0, (LPARAM)L"DirectDraw");
		SendDlgItemMessage (hDlg, IDC_DXMODE, CB_ADDSTRING, 0, (LPARAM)L"Direct3D");
		SendDlgItemMessage (hDlg, IDC_DXMODE, CB_SETCURSEL, workprefs.gfx_api, 0);

		SendDlgItemMessage (hDlg, IDC_DD_SURFACETYPE, CB_RESETCONTENT, 0, 0);
		SendDlgItemMessage (hDlg, IDC_DD_SURFACETYPE, CB_ADDSTRING, 0, (LPARAM)L"NonLocalVRAM");
		SendDlgItemMessage (hDlg, IDC_DD_SURFACETYPE, CB_ADDSTRING, 0, (LPARAM)L"DefaultRAM *");
		SendDlgItemMessage (hDlg, IDC_DD_SURFACETYPE, CB_ADDSTRING, 0, (LPARAM)L"LocalVRAM");
		SendDlgItemMessage (hDlg, IDC_DD_SURFACETYPE, CB_ADDSTRING, 0, (LPARAM)L"SystemRAM");
		SendDlgItemMessage (hDlg, IDC_DD_SURFACETYPE, CB_SETCURSEL, ddforceram, 0);

		SendDlgItemMessage (hDlg, IDC_WINDOWEDMODE, CB_RESETCONTENT, 0, 0);
		SendDlgItemMessage (hDlg, IDC_WINDOWEDMODE, CB_ADDSTRING, 0, (LPARAM)L"Borderless");
		SendDlgItemMessage (hDlg, IDC_WINDOWEDMODE, CB_ADDSTRING, 0, (LPARAM)L"Minimal");
		SendDlgItemMessage (hDlg, IDC_WINDOWEDMODE, CB_ADDSTRING, 0, (LPARAM)L"Standard");
		SendDlgItemMessage (hDlg, IDC_WINDOWEDMODE, CB_ADDSTRING, 0, (LPARAM)L"Extended");
		SendDlgItemMessage (hDlg, IDC_WINDOWEDMODE, CB_SETCURSEL,
			workprefs.win32_borderless ? 0 : (workprefs.win32_statusbar + 1),
			0);

	} else if (currentpage == MISC2_ID) {

		CheckDlgButton (hDlg, IDC_INACTIVE_PAUSE, workprefs.win32_inactive_pause);
		CheckDlgButton (hDlg, IDC_INACTIVE_NOSOUND, workprefs.win32_inactive_nosound || workprefs.win32_inactive_pause);
		CheckDlgButton (hDlg, IDC_MINIMIZED_PAUSE, workprefs.win32_iconified_pause);
		CheckDlgButton (hDlg, IDC_MINIMIZED_NOSOUND, workprefs.win32_iconified_nosound || workprefs.win32_iconified_pause);
		misc_addpri (hDlg, IDC_ACTIVE_PRIORITY, workprefs.win32_active_priority);
		misc_addpri (hDlg, IDC_INACTIVE_PRIORITY, workprefs.win32_inactive_priority);
		misc_addpri (hDlg, IDC_MINIMIZED_PRIORITY, workprefs.win32_iconified_priority);

	}
}

static INT_PTR MiscDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	int v, i;
	static int recursive;

	if (recursive)
		return FALSE;
	recursive++;

	switch (msg)
	{

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
				if (DiskSelection(hDlg, wParam, 9, &workprefs, path))
					save_state (savestate_fname, L"Description!");
			}
		} else if (GetDlgCtrlID((HWND)wParam) == IDC_DOLOADSTATE) {
			TCHAR *s = favoritepopup (hDlg);
			if (s) {
				TCHAR path[MAX_DPATH];
				_tcscpy (path, s);
				xfree (s);
				if (DiskSelection(hDlg, wParam, 10, &workprefs, path))
					savestate_state = STATE_DORESTORE;
			}
		}
		break;

	case WM_NOTIFY:
		if (((LPNMHDR) lParam)->idFrom == IDC_ASSOCIATELIST) {
			int entry, col;
			HWND list;
			NM_LISTVIEW *nmlistview;
			nmlistview = (NM_LISTVIEW *) lParam;
			list = nmlistview->hdr.hwndFrom;
			if (nmlistview->hdr.code == NM_DBLCLK) {
				entry = listview_entry_from_click (list, &col);
				exts[entry].enabled = exts[entry].enabled ? 0 : 1;
				associate_file_extensions ();
				InitializeListView (hDlg);
			}
		}
		break;

	case WM_COMMAND:
		if (currentpage == MISC1_ID) {
			if (HIWORD (wParam) == CBN_SELENDOK || HIWORD (wParam) == CBN_KILLFOCUS || HIWORD (wParam) == CBN_EDITCHANGE)  {
				switch (LOWORD (wParam))
				{
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
						v = SendDlgItemMessage (hDlg, IDC_LANGUAGE, CB_GETCURSEL, 0, 0L);
						if (v != CB_ERR)
							misc_setlang(v);
					}
					break;
				case IDC_DXMODE:
					v = SendDlgItemMessage (hDlg, IDC_DXMODE, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR) {
						workprefs.gfx_api = v;
						enable_for_miscdlg (hDlg);
					}
				break;
				case IDC_WINDOWEDMODE:
					v = SendDlgItemMessage (hDlg, IDC_WINDOWEDMODE, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR) {
						workprefs.win32_borderless = 0;
						workprefs.win32_statusbar = 0;
						if (v == 0)
							workprefs.win32_borderless = 1;
						if (v > 0)
							workprefs.win32_statusbar = v - 1;
					}
				break;
				case IDC_DD_SURFACETYPE:
					v = SendDlgItemMessage (hDlg, IDC_DD_SURFACETYPE, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR) {
						ddforceram = v;
						regsetint (NULL, L"DirectDraw_Secondary", ddforceram);
					}
					break;
				case IDC_SCSIMODE:
					v = SendDlgItemMessage (hDlg, IDC_SCSIMODE, CB_GETCURSEL, 0, 0L);
					if (v != CB_ERR)
						workprefs.win32_uaescsimode = v;
					break;
				}
			}
		} else if (currentpage == MISC2_ID) {
			misc_getpri (hDlg, IDC_ACTIVE_PRIORITY, &workprefs.win32_active_priority);
			misc_getpri (hDlg, IDC_INACTIVE_PRIORITY, &workprefs.win32_inactive_priority);
			misc_getpri (hDlg, IDC_MINIMIZED_PRIORITY, &workprefs.win32_iconified_priority);
		}

		switch(wParam)
		{
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
		case IDC_DOSAVESTATE:
			if (DiskSelection(hDlg, wParam, 9, &workprefs, 0))
				save_state (savestate_fname, L"Description!");
			break;
		case IDC_DOLOADSTATE:
			if (DiskSelection(hDlg, wParam, 10, &workprefs, 0))
				savestate_state = STATE_DORESTORE;
			break;
		case IDC_ILLEGAL:
			workprefs.illegal_mem = ischecked (hDlg, IDC_ILLEGAL);
			break;
		case IDC_JULIAN:
			workprefs.win32_middle_mouse = ischecked (hDlg, IDC_JULIAN);
			break;
		case IDC_FOCUSMINIMIZE:
			workprefs.win32_minimize_inactive = ischecked (hDlg, IDC_FOCUSMINIMIZE);
			break;
		case IDC_SHOWLEDS:
			workprefs.leds_on_screen &= ~STATUSLINE_CHIPSET;
			if (ischecked (hDlg, IDC_SHOWLEDS))
				workprefs.leds_on_screen |= STATUSLINE_CHIPSET;
			break;
		case IDC_SHOWLEDSRTG:
			workprefs.leds_on_screen &= ~STATUSLINE_RTG;
			if (ischecked (hDlg, IDC_SHOWLEDSRTG))
				workprefs.leds_on_screen |= STATUSLINE_RTG;
			break;
		case IDC_SHOWGUI:
			workprefs.start_gui = ischecked (hDlg, IDC_SHOWGUI);
			break;
		case IDC_CREATELOGFILE:
			workprefs.win32_logfile = ischecked (hDlg, IDC_CREATELOGFILE);
			enable_for_miscdlg(hDlg);
			break;
		case IDC_POWERSAVE:
			workprefs.win32_powersavedisabled = ischecked (hDlg, IDC_POWERSAVE);
			break;
		case IDC_INACTIVE_NOSOUND:
			if (!ischecked (hDlg, IDC_INACTIVE_NOSOUND))
				CheckDlgButton (hDlg, IDC_INACTIVE_PAUSE, BST_UNCHECKED);
		case IDC_INACTIVE_PAUSE:
			workprefs.win32_inactive_pause = ischecked (hDlg, IDC_INACTIVE_PAUSE);
			if (workprefs.win32_inactive_pause)
				CheckDlgButton (hDlg, IDC_INACTIVE_NOSOUND, BST_CHECKED);
			workprefs.win32_inactive_nosound = ischecked (hDlg, IDC_INACTIVE_NOSOUND);
			break;
		case IDC_MINIMIZED_NOSOUND:
			if (!ischecked (hDlg, IDC_MINIMIZED_NOSOUND))
				CheckDlgButton (hDlg, IDC_MINIMIZED_PAUSE, BST_UNCHECKED);
		case IDC_MINIMIZED_PAUSE:
			workprefs.win32_iconified_pause = ischecked (hDlg, IDC_MINIMIZED_PAUSE);
			if (workprefs.win32_iconified_pause)
				CheckDlgButton (hDlg, IDC_MINIMIZED_NOSOUND, BST_CHECKED);
			workprefs.win32_iconified_nosound = ischecked (hDlg, IDC_MINIMIZED_NOSOUND);
			break;
		case IDC_CTRLF11:
			workprefs.win32_ctrl_F11_is_quit = ischecked (hDlg, IDC_CTRLF11);
			break;
		case IDC_CLOCKSYNC:
			workprefs.tod_hack = ischecked (hDlg, IDC_CLOCKSYNC);
			break;
		case IDC_CLIPBOARDSHARE:
			workprefs.clipboard_sharing = ischecked (hDlg, IDC_CLIPBOARDSHARE);
			break;
		case IDC_NOTASKBARBUTTON:
			workprefs.win32_notaskbarbutton = ischecked (hDlg, IDC_NOTASKBARBUTTON);
			break;
		case IDC_ALWAYSONTOP:
			workprefs.win32_alwaysontop = ischecked (hDlg, IDC_ALWAYSONTOP);
			break;
		case IDC_KBLED_USB:
			workprefs.win32_kbledmode = ischecked (hDlg, IDC_KBLED_USB) ? 1 : 0;
			break;
		case IDC_FASTERRTG:
			workprefs.picasso96_nocustom = ischecked (hDlg, IDC_FASTERRTG);
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
	if (msg == WM_INITDIALOG) {
		pages[MISC1_ID] = hDlg;
		values_to_miscdlg (hDlg);
		enable_for_miscdlg (hDlg);
		return TRUE;
	}
	return MiscDlgProc (hDlg, msg, wParam, lParam);
}

static INT_PTR CALLBACK MiscDlgProc2 (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	currentpage = MISC2_ID;
	if (msg == WM_INITDIALOG) {
		pages[MISC2_ID] = hDlg;
		InitializeListView (hDlg);
		values_to_miscdlg (hDlg);
		enable_for_miscdlg (hDlg);
		return TRUE;
	}
	return MiscDlgProc (hDlg, msg, wParam, lParam);
}

static int cpu_ids[]   = { IDC_CPU0, IDC_CPU1, IDC_CPU2, IDC_CPU3, IDC_CPU4, IDC_CPU5 };
static int fpu_ids[]   = { IDC_FPU0, IDC_FPU1, IDC_FPU2, IDC_FPU3 };
static int trust_ids[] = { IDC_TRUST0, IDC_TRUST1, IDC_TRUST1, IDC_TRUST1 };

static void enable_for_cpudlg (HWND hDlg)
{
	BOOL enable = FALSE, jitenable = FALSE;
	BOOL cpu_based_enable = FALSE;
	BOOL fpu;

	/* These four items only get enabled when adjustable CPU style is enabled */
	ew (hDlg, IDC_SPEED, workprefs.m68k_speed > 0);
	ew (hDlg, IDC_COMPATIBLE24, workprefs.cpu_model == 68020);
	ew (hDlg, IDC_CS_HOST, !workprefs.cpu_cycle_exact);
	ew (hDlg, IDC_CS_68000, !workprefs.cpu_cycle_exact);
	ew (hDlg, IDC_CS_ADJUSTABLE, !workprefs.cpu_cycle_exact);
	ew (hDlg, IDC_CPUIDLE, workprefs.m68k_speed != 0 ? TRUE : FALSE);
#if !defined(CPUEMU_0) || defined(CPUEMU_68000_ONLY)
	ew (hDlg, IDC_CPU1, FALSE);
	ew (hDlg, IDC_CPU2, FALSE);
	ew (hDlg, IDC_CPU3, FALSE);
	ew (hDlg, IDC_CPU4, FALSE);
	ew (hDlg, IDC_CPU5, FALSE);
#endif

	cpu_based_enable = workprefs.cpu_model >= 68020 && workprefs.address_space_24 == 0;

	jitenable = cpu_based_enable;
#ifndef JIT
	jitenable = FALSE;
#endif
	enable = jitenable && workprefs.cachesize;

	ew (hDlg, IDC_TRUST0, enable);
	ew (hDlg, IDC_TRUST1, enable);
	ew (hDlg, IDC_HARDFLUSH, enable);
	ew (hDlg, IDC_CONSTJUMP, enable);
	ew (hDlg, IDC_JITFPU, enable);
	ew (hDlg, IDC_NOFLAGS, enable);
	ew (hDlg, IDC_CS_CACHE_TEXT, enable);
	ew (hDlg, IDC_CACHE, enable);
	ew (hDlg, IDC_JITENABLE, jitenable);
	ew (hDlg, IDC_COMPATIBLE, !workprefs.cpu_cycle_exact && !workprefs.cachesize);
	ew (hDlg, IDC_COMPATIBLE_FPU, workprefs.fpu_model > 0);
#if 0
	ew (hDlg, IDC_CPU_MULTIPLIER, workprefs.cpu_cycle_exact);
#endif
	ew (hDlg, IDC_CPU_FREQUENCY, workprefs.cpu_cycle_exact);
	ew (hDlg, IDC_CPU_FREQUENCY2, workprefs.cpu_cycle_exact && !workprefs.cpu_clock_multiplier);

	fpu = TRUE;
	if (workprefs.cpu_model > 68030 || workprefs.cpu_compatible || workprefs.cpu_cycle_exact)
		fpu = FALSE;
	ew (hDlg, IDC_FPU1, fpu);
	ew (hDlg, IDC_FPU2, fpu);
	ew (hDlg, IDC_FPU3, workprefs.cpu_model >= 68040);
	ew (hDlg, IDC_MMUENABLE, workprefs.cpu_model == 68040 && workprefs.cachesize == 0);
}

static int getcpufreq (int m)
{
	int f;

	f = workprefs.ntscmode ? 28636360.0 : 28375160.0;
	return f * (m >> 8) / 8;
}

static void values_to_cpudlg (HWND hDlg)
{
	TCHAR cache[8] = L"";
	int cpu;

	SendDlgItemMessage (hDlg, IDC_SPEED, TBM_SETPOS, TRUE, workprefs.m68k_speed <= 0 ? 1 : workprefs.m68k_speed / CYCLE_UNIT );
	SetDlgItemInt( hDlg, IDC_CPUTEXT, workprefs.m68k_speed <= 0 ? 1 : workprefs.m68k_speed / CYCLE_UNIT, FALSE );
	CheckDlgButton (hDlg, IDC_COMPATIBLE, workprefs.cpu_compatible);
	CheckDlgButton (hDlg, IDC_COMPATIBLE24, workprefs.address_space_24);
	CheckDlgButton (hDlg, IDC_COMPATIBLE_FPU, workprefs.fpu_strict);
	SendDlgItemMessage (hDlg, IDC_CPUIDLE, TBM_SETPOS, TRUE, workprefs.cpu_idle == 0 ? 0 : 12 - workprefs.cpu_idle / 15);
	cpu = (workprefs.cpu_model - 68000) / 10;
	if (cpu >= 5)
		cpu--;
	CheckRadioButton (hDlg, IDC_CPU0, IDC_CPU5, cpu_ids[cpu]);
	CheckRadioButton (hDlg, IDC_FPU0, IDC_FPU3, fpu_ids[workprefs.fpu_model == 0 ? 0 : (workprefs.fpu_model == 68881 ? 1 : (workprefs.fpu_model == 68882 ? 2 : 3))]);

	if (workprefs.m68k_speed == -1)
		CheckRadioButton(hDlg, IDC_CS_HOST, IDC_CS_ADJUSTABLE, IDC_CS_HOST);
	else if (workprefs.m68k_speed == 0)
		CheckRadioButton(hDlg, IDC_CS_HOST, IDC_CS_ADJUSTABLE, IDC_CS_68000);
	else
		CheckRadioButton(hDlg, IDC_CS_HOST, IDC_CS_ADJUSTABLE, IDC_CS_ADJUSTABLE);

	CheckRadioButton (hDlg, IDC_TRUST0, IDC_TRUST1, trust_ids[workprefs.comptrustbyte]);

	SendDlgItemMessage (hDlg, IDC_CACHE, TBM_SETPOS, TRUE, workprefs.cachesize / 1024);
	_stprintf (cache, L"%d MB", workprefs.cachesize / 1024 );
	SetDlgItemText (hDlg, IDC_CACHETEXT, cache);

	CheckDlgButton (hDlg, IDC_NOFLAGS, workprefs.compnf);
	CheckDlgButton (hDlg, IDC_JITFPU, workprefs.compfpu);
	CheckDlgButton (hDlg, IDC_HARDFLUSH, workprefs.comp_hardflush);
	CheckDlgButton (hDlg, IDC_CONSTJUMP, workprefs.comp_constjump);
	CheckDlgButton (hDlg, IDC_JITENABLE, workprefs.cachesize > 0);
	CheckDlgButton (hDlg, IDC_MMUENABLE, workprefs.cpu_model == 68040 && workprefs.cachesize == 0 && workprefs.mmu_model == 68040);

	if (workprefs.cpu_cycle_exact) {
		if (workprefs.cpu_clock_multiplier) {
			TCHAR txt[20];
			double f = getcpufreq (workprefs.cpu_clock_multiplier);
			_stprintf (txt, L"%.6f", f / 1000000.0);
			SendDlgItemMessage (hDlg, IDC_CPU_FREQUENCY2, WM_SETTEXT, 0, (LPARAM)txt);
		}
	} else {
		SendDlgItemMessage (hDlg, IDC_CPU_FREQUENCY2, WM_SETTEXT, 0, (LPARAM)L"");
	}
}

static void values_from_cpudlg (HWND hDlg)
{
	int newcpu, newfpu, newtrust, oldcache, jitena, idx;
	static int cachesize_prev, trust_prev;

	workprefs.cpu_compatible = workprefs.cpu_cycle_exact | (ischecked (hDlg, IDC_COMPATIBLE) ? 1 : 0);
	workprefs.fpu_strict = ischecked (hDlg, IDC_COMPATIBLE_FPU) ? 1 : 0;
	workprefs.address_space_24 = ischecked (hDlg, IDC_COMPATIBLE24) ? 1 : 0;
	workprefs.m68k_speed = ischecked (hDlg, IDC_CS_HOST) ? -1
		: ischecked (hDlg, IDC_CS_68000) ? 0
		: SendMessage (GetDlgItem (hDlg, IDC_SPEED), TBM_GETPOS, 0, 0) * CYCLE_UNIT;
	workprefs.mmu_model = ischecked (hDlg, IDC_MMUENABLE) ? 68040 : 0;

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
	workprefs.cpu_model = newcpu;
	switch(newcpu)
	{
	case 68000:
	case 68010:
		workprefs.fpu_model = newfpu == 0 ? 0 : (newfpu == 2 ? 68882 : 68881);
		if (workprefs.cpu_compatible || workprefs.cpu_cycle_exact)
			workprefs.fpu_model = 0;
		workprefs.address_space_24 = 1;
		if (newcpu == 0 && workprefs.cpu_cycle_exact)
			workprefs.m68k_speed = 0;
		break;
	case 68020:
		workprefs.fpu_model = newfpu == 0 ? 0 : (newfpu == 2 ? 68882 : 68881);
		break;
	case 68030:
		workprefs.address_space_24 = 0;
		workprefs.fpu_model = newfpu == 0 ? 0 : (newfpu == 2 ? 68882 : 68881);
		break;
	case 68040:
		workprefs.fpu_model = newfpu ? 68040 : 0;
		workprefs.address_space_24 = 0;
		if (workprefs.fpu_model)
			workprefs.fpu_model = 68040;
		break;
	case 68060:
		workprefs.fpu_model = newfpu ? 68060 : 0;
		workprefs.address_space_24 = 0;
		break;
	}

	newtrust = ischecked (hDlg, IDC_TRUST0) ? 0 : 1;
	workprefs.comptrustbyte = newtrust;
	workprefs.comptrustword = newtrust;
	workprefs.comptrustlong = newtrust;
	workprefs.comptrustnaddr= newtrust;

	workprefs.compnf            = ischecked (hDlg, IDC_NOFLAGS);
	workprefs.compfpu           = ischecked (hDlg, IDC_JITFPU);
	workprefs.comp_hardflush    = ischecked (hDlg, IDC_HARDFLUSH);
	workprefs.comp_constjump    = ischecked (hDlg, IDC_CONSTJUMP);

#ifdef JIT
	oldcache = workprefs.cachesize;
	jitena = ischecked (hDlg, IDC_JITENABLE) ? 1 : 0;
	workprefs.cachesize = SendMessage (GetDlgItem (hDlg, IDC_CACHE), TBM_GETPOS, 0, 0) * 1024;
	if (!jitena) {
		cachesize_prev = workprefs.cachesize;
		trust_prev = workprefs.comptrustbyte;
		workprefs.cachesize = 0;
	} else if (jitena && !oldcache) {
		workprefs.cachesize = 8192;
		if (cachesize_prev) {
			workprefs.cachesize = cachesize_prev;
			workprefs.comptrustbyte = trust_prev;
			workprefs.comptrustword = trust_prev;
			workprefs.comptrustlong = trust_prev;
			workprefs.comptrustnaddr = trust_prev;
		}
	}
	if (oldcache == 0 && candirect && workprefs.cachesize > 0)
		canbang = 1;
#endif
	workprefs.cpu_idle = SendMessage (GetDlgItem (hDlg, IDC_CPUIDLE), TBM_GETPOS, 0, 0);
	if (workprefs.cpu_idle > 0)
		workprefs.cpu_idle = (12 - workprefs.cpu_idle) * 15;

	if (workprefs.cachesize > 0)
		workprefs.cpu_compatible = 0;
	if (pages[KICKSTART_ID])
		SendMessage (pages[KICKSTART_ID], WM_USER, 0, 0);
	if (pages[DISPLAY_ID])
		SendMessage (pages[DISPLAY_ID], WM_USER, 0, 0);
	if (pages[MEMORY_ID])
		SendMessage (pages[MEMORY_ID], WM_USER, 0, 0);

	idx = SendDlgItemMessage (hDlg, IDC_CPU_FREQUENCY, CB_GETCURSEL, 0, 0);
	if (idx != CB_ERR) {
		int m = workprefs.cpu_clock_multiplier;
		workprefs.cpu_frequency = 0;
		workprefs.cpu_clock_multiplier = 0;
		if (idx == 0)
			workprefs.cpu_clock_multiplier = 2 << 8;
		if (idx == 1)
			workprefs.cpu_clock_multiplier = 4 << 8;
		if (idx == 2)
			workprefs.cpu_clock_multiplier = 8 << 8;
		if (idx == 3) {
			TCHAR txt[20];
			SendDlgItemMessage (hDlg, IDC_CPU_FREQUENCY2, WM_GETTEXT, (WPARAM)sizeof (txt) / sizeof (TCHAR), (LPARAM)txt);
			workprefs.cpu_clock_multiplier = 0;
			workprefs.cpu_frequency = _tstof (txt) * 1000000.0;
			if (workprefs.cpu_frequency < 1 * 1000000)
				workprefs.cpu_frequency = 0;
			if (workprefs.cpu_frequency >= 99 * 1000000)
				workprefs.cpu_frequency = 0;
		}
	}
}

static INT_PTR CALLBACK CPUDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;
	int idx;

	switch (msg) {
	case WM_INITDIALOG:
		recursive++;
		pages[CPU_ID] = hDlg;
		currentpage = CPU_ID;
		SendDlgItemMessage (hDlg, IDC_SPEED, TBM_SETRANGE, TRUE, MAKELONG (MIN_M68K_PRIORITY, MAX_M68K_PRIORITY));
		SendDlgItemMessage (hDlg, IDC_SPEED, TBM_SETPAGESIZE, 0, 1);
		SendDlgItemMessage (hDlg, IDC_CACHE, TBM_SETRANGE, TRUE, MAKELONG (MIN_CACHE_SIZE, MAX_CACHE_SIZE));
		SendDlgItemMessage (hDlg, IDC_CACHE, TBM_SETPAGESIZE, 0, 1);
		SendDlgItemMessage (hDlg, IDC_CPUIDLE, TBM_SETRANGE, TRUE, MAKELONG (0, 10));
		SendDlgItemMessage (hDlg, IDC_CPUIDLE, TBM_SETPAGESIZE, 0, 1);

		SendDlgItemMessage (hDlg, IDC_CPU_FREQUENCY, CB_RESETCONTENT, 0, 0);
		SendDlgItemMessage (hDlg, IDC_CPU_FREQUENCY, CB_ADDSTRING, 0, (LPARAM)L"A500");
		SendDlgItemMessage (hDlg, IDC_CPU_FREQUENCY, CB_ADDSTRING, 0, (LPARAM)L"A1200");
		SendDlgItemMessage (hDlg, IDC_CPU_FREQUENCY, CB_ADDSTRING, 0, (LPARAM)L"2x");
		SendDlgItemMessage (hDlg, IDC_CPU_FREQUENCY, CB_ADDSTRING, 0, (LPARAM)L"Custom");

		idx = 3;
		if (workprefs.cpu_clock_multiplier == 2 << 8 || (workprefs.cpu_clock_multiplier == 0 && workprefs.cpu_frequency == 0 && workprefs.cpu_model <= 68010)) {
			idx = 0;
			workprefs.cpu_clock_multiplier = 2 << 8;
		}
		if (workprefs.cpu_clock_multiplier == 4 << 8 || (workprefs.cpu_clock_multiplier == 0 && workprefs.cpu_frequency == 0 && workprefs.cpu_model >= 68020)) {
			idx = 1;
			workprefs.cpu_clock_multiplier = 4 << 8;
		}
		if (workprefs.cpu_clock_multiplier == 8 << 8) {
			idx = 2;
		}
		if (!workprefs.cpu_cycle_exact) {
			idx = 3;
			workprefs.cpu_clock_multiplier = 0;
			workprefs.cpu_frequency = 0;
		}
		SendDlgItemMessage (hDlg, IDC_CPU_FREQUENCY, CB_SETCURSEL, idx, 0);
		if (!workprefs.cpu_clock_multiplier) {
			TCHAR txt[20];
			_stprintf (txt, L"%.6f", workprefs.cpu_frequency / 1000000.0);
			SendDlgItemMessage (hDlg, IDC_CPU_FREQUENCY2, WM_SETTEXT, 0, (LPARAM)txt);
		}
		recursive--;

	case WM_USER:
		recursive++;
		values_to_cpudlg (hDlg);
		enable_for_cpudlg (hDlg);
		recursive--;
		return TRUE;

	case WM_COMMAND:
		if (recursive > 0)
			break;
		recursive++;
		values_from_cpudlg (hDlg);
		values_to_cpudlg (hDlg);
		enable_for_cpudlg (hDlg);
		recursive--;
		break;

	case WM_HSCROLL:
		recursive++;
		values_from_cpudlg( hDlg );
		values_to_cpudlg( hDlg );
		enable_for_cpudlg( hDlg );
		recursive--;
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

	get_plugin_path (dirname, sizeof dirname / sizeof (TCHAR), L"floppysounds");
	_tcscat (dirname, L"*.wav");
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

static void update_soundgui (HWND hDlg)
{
	int bufsize, canexc;
	TCHAR txt[20];

	bufsize = exact_log2 (workprefs.sound_maxbsiz / 1024);
	_stprintf (txt, L"%d", bufsize);
	SetDlgItemText (hDlg, IDC_SOUNDBUFFERMEM, txt);

	SendDlgItemMessage (hDlg, IDC_SOUNDVOLUME, TBM_SETPOS, TRUE, 100 - workprefs.sound_volume);
	_stprintf (txt, L"%d%%", 100 - workprefs.sound_volume);
	SetDlgItemText (hDlg, IDC_SOUNDVOLUME2, txt);

	SendDlgItemMessage (hDlg, IDC_SOUNDDRIVEVOLUME, TBM_SETPOS, TRUE, 100 - workprefs.dfxclickvolume);
	_stprintf (txt, L"%d%%", 100 - workprefs.dfxclickvolume);
	SetDlgItemText (hDlg, IDC_SOUNDDRIVEVOLUME2, txt);

	canexc = sound_devices[workprefs.win32_soundcard].type == SOUND_DEVICE_WASAPI;
	ew (hDlg, IDC_SOUND_EXCLUSIVE, canexc);
	if (!canexc)
		workprefs.win32_soundexclusive = 0;
	CheckDlgButton (hDlg, IDC_SOUND_EXCLUSIVE, workprefs.win32_soundexclusive);

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
	LRESULT idx;

	if (workprefs.sound_maxbsiz & (workprefs.sound_maxbsiz - 1))
		workprefs.sound_maxbsiz = DEFAULT_SOUND_MAXB;

	SendDlgItemMessage (hDlg, IDC_SOUNDFILTER, CB_RESETCONTENT, 0, 0);
	WIN32GUI_LoadUIString (IDS_SOUND_FILTER_OFF, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_SOUNDFILTER, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_SOUND_FILTER_EMULATED, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_SOUNDFILTER, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_SOUND_FILTER_EMULATED_E, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_SOUNDFILTER, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_SOUND_FILTER_ON_AGA, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_SOUNDFILTER, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_SOUND_FILTER_ON_A500, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_SOUNDFILTER, CB_ADDSTRING, 0, (LPARAM)txt);
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
		i = workprefs.sound_filter_type ? 4 : 3;
		break;
	}
	SendDlgItemMessage (hDlg, IDC_SOUNDFILTER, CB_SETCURSEL, i, 0);

	SendDlgItemMessage (hDlg, IDC_SOUNDSTEREO, CB_RESETCONTENT, 0, 0);
	WIN32GUI_LoadUIString (IDS_SOUND_MONO, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_SOUNDSTEREO, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_SOUND_STEREO, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_SOUNDSTEREO, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_SOUND_STEREO2, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_SOUNDSTEREO, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_SOUND_4CHANNEL, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_SOUNDSTEREO, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_SOUND_CLONED51, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_SOUNDSTEREO, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_SOUND_51, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_SOUNDSTEREO, CB_ADDSTRING, 0, (LPARAM)txt);
	SendDlgItemMessage (hDlg, IDC_SOUNDSTEREO, CB_SETCURSEL, workprefs.sound_stereo, 0);

	SendDlgItemMessage (hDlg, IDC_SOUNDSWAP, CB_RESETCONTENT, 0, 0);
	SendDlgItemMessage (hDlg, IDC_SOUNDSWAP, CB_ADDSTRING, 0, (LPARAM)L"-");
	WIN32GUI_LoadUIString (IDS_SOUND_SWAP_PAULA, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_SOUNDSWAP, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_SOUND_SWAP_AHI, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_SOUNDSWAP, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_SOUND_SWAP_BOTH, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_SOUNDSWAP, CB_ADDSTRING, 0, (LPARAM)txt);
	SendDlgItemMessage (hDlg, IDC_SOUNDSWAP, CB_SETCURSEL,
		workprefs.sound_stereo_swap_paula + workprefs.sound_stereo_swap_ahi * 2, 0);

	SendDlgItemMessage (hDlg, IDC_SOUNDSTEREOSEP, CB_RESETCONTENT, 0, 0);
	for (i = 10; i >= 0; i--) {
		_stprintf (txt, L"%d%%", i * 10);
		SendDlgItemMessage (hDlg, IDC_SOUNDSTEREOSEP, CB_ADDSTRING, 0, (LPARAM)txt);
	}
	SendDlgItemMessage (hDlg, IDC_SOUNDSTEREOSEP, CB_SETCURSEL, 10 - workprefs.sound_stereo_separation, 0);

	SendDlgItemMessage (hDlg, IDC_SOUNDSTEREOMIX, CB_RESETCONTENT, 0, 0);
	SendDlgItemMessage (hDlg, IDC_SOUNDSTEREOMIX, CB_ADDSTRING, 0, (LPARAM)L"-");
	for (i = 0; i < 10; i++) {
		_stprintf (txt, L"%d", i + 1);
		SendDlgItemMessage (hDlg, IDC_SOUNDSTEREOMIX, CB_ADDSTRING, 0, (LPARAM)txt);
	}
	SendDlgItemMessage (hDlg, IDC_SOUNDSTEREOMIX, CB_SETCURSEL,
		workprefs.sound_mixed_stereo_delay > 0 ? workprefs.sound_mixed_stereo_delay : 0, 0);

	SendDlgItemMessage (hDlg, IDC_SOUNDINTERPOLATION, CB_RESETCONTENT, 0, 0);
	WIN32GUI_LoadUIString (IDS_DISABLED, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_SOUNDINTERPOLATION, CB_ADDSTRING, 0, (LPARAM)txt);
	SendDlgItemMessage (hDlg, IDC_SOUNDINTERPOLATION, CB_ADDSTRING, 0, (LPARAM)L"Anti");
	SendDlgItemMessage (hDlg, IDC_SOUNDINTERPOLATION, CB_ADDSTRING, 0, (LPARAM)L"Sinc");
	SendDlgItemMessage (hDlg, IDC_SOUNDINTERPOLATION, CB_ADDSTRING, 0, (LPARAM)L"RH");
	SendDlgItemMessage (hDlg, IDC_SOUNDINTERPOLATION, CB_ADDSTRING, 0, (LPARAM)L"Crux");
	SendDlgItemMessage (hDlg, IDC_SOUNDINTERPOLATION, CB_SETCURSEL, workprefs.sound_interpol, 0);

	SendDlgItemMessage (hDlg, IDC_SOUNDFREQ, CB_RESETCONTENT, 0, 0);
	i = 0;
	selected = -1;
	while (soundfreqs[i]) {
		_stprintf (txt, L"%d", soundfreqs[i]);
		SendDlgItemMessage (hDlg, IDC_SOUNDFREQ, CB_ADDSTRING, 0, (LPARAM)txt);
		i++;
	}
	_stprintf (txt, L"%d", workprefs.sound_freq);
	SendDlgItemMessage (hDlg, IDC_SOUNDFREQ, WM_SETTEXT, 0, (LPARAM)txt);

	switch (workprefs.produce_sound)
	{
	case 0: which_button = IDC_SOUND0; break;
	case 1: which_button = IDC_SOUND1; break;
	case 2: case 3: which_button = IDC_SOUND2; break;
	}
	CheckRadioButton (hDlg, IDC_SOUND0, IDC_SOUND2, which_button);

	CheckDlgButton (hDlg, IDC_SOUND_AUTO, workprefs.sound_auto);

	workprefs.sound_maxbsiz = 1 << exact_log2 (workprefs.sound_maxbsiz);
	if (workprefs.sound_maxbsiz < 2048)
		workprefs.sound_maxbsiz = 2048;
	SendDlgItemMessage (hDlg, IDC_SOUNDBUFFERRAM, TBM_SETPOS, TRUE, exact_log2 (workprefs.sound_maxbsiz / 2048));

	SendDlgItemMessage (hDlg, IDC_SOUNDVOLUME, TBM_SETPOS, TRUE, 0);
	SendDlgItemMessage (hDlg, IDC_SOUNDDRIVEVOLUME, TBM_SETPOS, TRUE, 0);

	SendDlgItemMessage (hDlg, IDC_SOUNDCARDLIST, CB_SETCURSEL, workprefs.win32_soundcard, 0);

	idx = SendDlgItemMessage (hDlg, IDC_SOUNDDRIVE, CB_GETCURSEL, 0, 0);
	if (idx < 0)
		idx = 0;
	SendDlgItemMessage (hDlg, IDC_SOUNDDRIVE, CB_RESETCONTENT, 0, 0);
	for (i = 0; i < 4; i++) {
		_stprintf (txt, L"DF%d:", i);
		SendDlgItemMessage (hDlg, IDC_SOUNDDRIVE, CB_ADDSTRING, 0, (LPARAM)txt);
	}
	SendDlgItemMessage (hDlg, IDC_SOUNDDRIVE, CB_SETCURSEL, idx, 0);
	SendDlgItemMessage (hDlg, IDC_SOUNDDRIVESELECT, CB_RESETCONTENT, 0, 0);
	WIN32GUI_LoadUIString (IDS_DRIVESOUND_NONE, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_SOUNDDRIVESELECT, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_DRIVESOUND_DEFAULT_A500, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_SOUNDDRIVESELECT, CB_ADDSTRING, 0, (LPARAM)txt);
	driveclick_fdrawcmd_detect ();
	if (driveclick_pcdrivemask) {
		for (i = 0; i < 2; i++) {
			WIN32GUI_LoadUIString (IDS_DRIVESOUND_PC_FLOPPY, txt, sizeof (txt) / sizeof (TCHAR));
			_stprintf (txt2, txt, 'A' + i);
			SendDlgItemMessage (hDlg, IDC_SOUNDDRIVESELECT, CB_ADDSTRING, 0, (LPARAM)txt2);
		}
	}
	SendDlgItemMessage (hDlg, IDC_SOUNDDRIVESELECT, CB_SETCURSEL, 0, 0);
	p = drivesounds;
	if (p) {
		while (p[0]) {
			SendDlgItemMessage (hDlg, IDC_SOUNDDRIVESELECT, CB_ADDSTRING, 0, (LPARAM)p);
			p += _tcslen (p) + 1;
		}
	}
	if (workprefs.floppyslots[idx].dfxclick < 0) {
		p = drivesounds;
		i = DS_BUILD_IN_SOUNDS + (driveclick_pcdrivemask ? 2 : 0) + 1;
		while (p && p[0]) {
			if (!_tcsicmp (p, workprefs.floppyslots[idx].dfxclickexternal)) {
				SendDlgItemMessage (hDlg, IDC_SOUNDDRIVESELECT, CB_SETCURSEL, i, 0);
				break;
			}
			i++;
			p += _tcslen (p) + 1;
		}

	} else {
		SendDlgItemMessage (hDlg, IDC_SOUNDDRIVESELECT, CB_SETCURSEL, workprefs.floppyslots[idx].dfxclick, 0);
	}

	update_soundgui (hDlg);
}

static void values_from_sounddlg (HWND hDlg)
{
	TCHAR txt[10];
	LRESULT idx;
	int soundcard, i;

	idx = SendDlgItemMessage (hDlg, IDC_SOUNDFREQ, CB_GETCURSEL, 0, 0);
	if (idx >= 0) {
		workprefs.sound_freq = soundfreqs[idx];
	} else {
		SendDlgItemMessage (hDlg, IDC_SOUNDFREQ, WM_GETTEXT, (WPARAM)sizeof (txt) / sizeof (TCHAR), (LPARAM)txt);
		workprefs.sound_freq = _tstol (txt);
	}
	if (workprefs.sound_freq < 8000)
		workprefs.sound_freq = 8000;
	if (workprefs.sound_freq > 96000)
		workprefs.sound_freq = 96000;

	workprefs.produce_sound = (ischecked (hDlg, IDC_SOUND0) ? 0
		: ischecked (hDlg, IDC_SOUND1) ? 1 : 3);

	workprefs.sound_auto = ischecked (hDlg, IDC_SOUND_AUTO);

	idx = SendDlgItemMessage (hDlg, IDC_SOUNDSTEREO, CB_GETCURSEL, 0, 0);
	if (idx != CB_ERR)
		workprefs.sound_stereo = idx;
	workprefs.sound_stereo_separation = 0;
	workprefs.sound_mixed_stereo_delay = 0;
	if (workprefs.sound_stereo > 0) {
		idx = SendDlgItemMessage (hDlg, IDC_SOUNDSTEREOSEP, CB_GETCURSEL, 0, 0);
		if (idx != CB_ERR) {
			if (idx > 0)
				workprefs.sound_mixed_stereo_delay = -1;
			workprefs.sound_stereo_separation = 10 - idx;
		}
		idx = SendDlgItemMessage (hDlg, IDC_SOUNDSTEREOMIX, CB_GETCURSEL, 0, 0);
		if (idx != CB_ERR && idx > 0)
			workprefs.sound_mixed_stereo_delay = idx;
	}

	workprefs.sound_interpol = SendDlgItemMessage (hDlg, IDC_SOUNDINTERPOLATION, CB_GETCURSEL, 0, 0);
	workprefs.win32_soundexclusive = ischecked (hDlg, IDC_SOUND_EXCLUSIVE);
	soundcard = SendDlgItemMessage (hDlg, IDC_SOUNDCARDLIST, CB_GETCURSEL, 0, 0L);
	if (soundcard != workprefs.win32_soundcard && soundcard != CB_ERR) {
		workprefs.win32_soundcard = soundcard;
		update_soundgui (hDlg);
	}

	switch (SendDlgItemMessage (hDlg, IDC_SOUNDFILTER, CB_GETCURSEL, 0, 0))
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
	}

	workprefs.sound_stereo_swap_paula = (SendDlgItemMessage (hDlg, IDC_SOUNDSWAP, CB_GETCURSEL, 0, 0) & 1) ? 1 : 0;
	workprefs.sound_stereo_swap_ahi = (SendDlgItemMessage (hDlg, IDC_SOUNDSWAP, CB_GETCURSEL, 0, 0) & 2) ? 1 : 0;

	for (i = 0; sounddrivers[i]; i++) {
		int old = sounddrivermask;
		sounddrivermask &= ~(1 << i);
		if (ischecked (hDlg, sounddrivers[i]))
			sounddrivermask |= 1 << i;
		if (old != sounddrivermask)
			regsetint (NULL, L"SoundDriverMask", sounddrivermask);
	}

	idx = SendDlgItemMessage (hDlg, IDC_SOUNDDRIVE, CB_GETCURSEL, 0, 0);
	if (idx != CB_ERR && idx >= 0) {
		LRESULT res = SendDlgItemMessage (hDlg, IDC_SOUNDDRIVESELECT, CB_GETCURSEL, 0, 0);
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

	switch (msg) {
	case WM_INITDIALOG:
		{
			recursive++;
			sound_loaddrivesamples ();
			SendDlgItemMessage (hDlg, IDC_SOUNDBUFFERRAM, TBM_SETRANGE, TRUE, MAKELONG (MIN_SOUND_MEM, MAX_SOUND_MEM));
			SendDlgItemMessage (hDlg, IDC_SOUNDBUFFERRAM, TBM_SETPAGESIZE, 0, 1);

			SendDlgItemMessage (hDlg, IDC_SOUNDVOLUME, TBM_SETRANGE, TRUE, MAKELONG (0, 100));
			SendDlgItemMessage (hDlg, IDC_SOUNDVOLUME, TBM_SETPAGESIZE, 0, 1);

			SendDlgItemMessage (hDlg, IDC_SOUNDDRIVEVOLUME, TBM_SETRANGE, TRUE, MAKELONG (0, 100));
			SendDlgItemMessage (hDlg, IDC_SOUNDDRIVEVOLUME, TBM_SETPAGESIZE, 0, 1);

			SendDlgItemMessage (hDlg, IDC_SOUNDADJUST, TBM_SETRANGE, TRUE, MAKELONG (-100, +30));
			SendDlgItemMessage (hDlg, IDC_SOUNDADJUST, TBM_SETPAGESIZE, 0, 1);

			for (i = 0; i < sounddrivers[i]; i++) {
				CheckDlgButton (hDlg, sounddrivers[i], (sounddrivermask & (1 << i)) ? TRUE : FALSE);
			}

			SendDlgItemMessage (hDlg, IDC_SOUNDCARDLIST, CB_RESETCONTENT, 0, 0L);
			numdevs = enumerate_sound_devices ();
			for (card = 0; card < numdevs; card++) {
				TCHAR tmp[MAX_DPATH];
				int type = sound_devices[card].type;
				_stprintf (tmp, L"%s: %s",
					type == SOUND_DEVICE_DS ? L"DSOUND" : (type == SOUND_DEVICE_AL ? L"OpenAL" : (type == SOUND_DEVICE_PA ? L"PortAudio" : L"WASAPI")),
					sound_devices[card].name);
				SendDlgItemMessage (hDlg, IDC_SOUNDCARDLIST, CB_ADDSTRING, 0, (LPARAM)tmp);
			}
			if (numdevs == 0)
				workprefs.produce_sound = 0; /* No sound card in system, enable_for_sounddlg will accomodate this */

			pages[SOUND_ID] = hDlg;
			currentpage = SOUND_ID;
			update_soundgui (hDlg);
			recursive--;
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
		if(LOWORD (wParam) == IDC_SOUNDDRIVE) {
			values_to_sounddlg (hDlg);
		}
		values_from_sounddlg (hDlg);
		enable_for_sounddlg (hDlg);
		recursive--;
		break;

	case WM_HSCROLL:
		workprefs.sound_maxbsiz = 2048 << SendMessage (GetDlgItem (hDlg, IDC_SOUNDBUFFERRAM), TBM_GETPOS, 0, 0);
		workprefs.sound_volume = 100 - SendMessage (GetDlgItem (hDlg, IDC_SOUNDVOLUME), TBM_GETPOS, 0, 0);
		workprefs.dfxclickvolume = 100 - SendMessage (GetDlgItem (hDlg, IDC_SOUNDDRIVEVOLUME), TBM_GETPOS, 0, 0);
		update_soundgui (hDlg);
		break;
	}
	return FALSE;
}

#ifdef FILESYS

struct fsvdlg_vals
{
	TCHAR volume[MAX_DPATH];
	TCHAR device[MAX_DPATH];
	TCHAR rootdir[MAX_DPATH];
	int bootpri;
	int autoboot;
	int donotmount;
	int rw;
	int rdb;
};

static struct fsvdlg_vals empty_fsvdlg = { L"", L"", L"", 0, 1, 1, 1, 0 };
static struct fsvdlg_vals current_fsvdlg;

struct hfdlg_vals
{
	TCHAR devicename[MAX_DPATH];
	TCHAR filename[MAX_DPATH];
	TCHAR fsfilename[MAX_DPATH];
	int sectors;
	int reserved;
	int surfaces;
	int cylinders;
	int blocksize;
	bool rw;
	bool rdb;
	int bootpri;
	bool donotmount;
	bool autoboot;
	int controller;
	bool original;
};

static struct hfdlg_vals empty_hfdlg = { L"", L"", L"", 32, 2, 1, 0, 512, 1, 0, 0, 0, 1, 0, 1 };
static struct hfdlg_vals current_hfdlg;
static int archivehd;

static void volumeselectfile (HWND hDlg)
{
	TCHAR directory_path[MAX_DPATH];
	_tcscpy (directory_path, current_fsvdlg.rootdir);
	if (directory_path[0] == 0) {
		int out = sizeof directory_path / sizeof (TCHAR);
		regquerystr (NULL, L"FilesystemFilePath", directory_path, &out);
	}
	if (DiskSelection (hDlg, 0, 14, &workprefs, directory_path)) {
		TCHAR *s = filesys_createvolname (NULL, directory_path, L"Harddrive");
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
			regsetstr (NULL, L"FilesystemFilePath", directory_path);
			p[1] = t;
		}
	}
}
static void volumeselectdir (HWND hDlg, int newdir)
{
	const GUID volumeguid = { 0x1df05121, 0xcc08, 0x46ea, { 0x80, 0x3f, 0x98, 0x3c, 0x54, 0x88, 0x53, 0x76 } };
	TCHAR szTitle[MAX_DPATH];
	TCHAR directory_path[MAX_DPATH];

	_tcscpy (directory_path, current_fsvdlg.rootdir);
	if (!newdir) {
		if (directory_path[0] == 0) {
			int out = sizeof directory_path / sizeof (TCHAR);
			regquerystr (NULL, L"FilesystemDirectoryPath", directory_path, &out);
		}
		WIN32GUI_LoadUIString (IDS_SELECTFILESYSROOT, szTitle, MAX_DPATH);
		if (DirectorySelection (hDlg, &volumeguid, directory_path)) {
			newdir = 1;
			regsetstr (NULL, L"FilesystemDirectoryPath", directory_path);
		}
	}
	if (newdir) {
		SetDlgItemText (hDlg, IDC_PATH_NAME, directory_path);
		ew (hDlg, IDC_FS_RW, TRUE);
		archivehd = 0;
	}
}

static INT_PTR CALLBACK VolumeSettingsProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;

	switch (msg) {
	case WM_INITDIALOG:
		{
			archivehd = -1;
			if (my_existsfile (current_fsvdlg.rootdir))
				archivehd = 1;
			else if (my_existsdir (current_fsvdlg.rootdir))
				archivehd = 0;
			recursive++;
			setac (hDlg, IDC_PATH_NAME);
			SetDlgItemText (hDlg, IDC_VOLUME_NAME, current_fsvdlg.volume);
			SetDlgItemText (hDlg, IDC_VOLUME_DEVICE, current_fsvdlg.device);
			SetDlgItemText (hDlg, IDC_PATH_NAME, current_fsvdlg.rootdir);
			SetDlgItemInt (hDlg, IDC_VOLUME_BOOTPRI, current_fsvdlg.bootpri >= -127 ? current_fsvdlg.bootpri : -127, TRUE);
			if (archivehd > 0)
				current_fsvdlg.rw = 0;
			CheckDlgButton (hDlg, IDC_FS_RW, current_fsvdlg.rw);
			CheckDlgButton (hDlg, IDC_FS_AUTOBOOT, current_fsvdlg.autoboot);
			current_fsvdlg.donotmount = 0;
			ew (hDlg, IDC_FS_RW, archivehd <= 0);
			recursive--;
		}
		return TRUE;

	case WM_CONTEXTMENU:
		if (GetDlgCtrlID ((HWND)wParam) == IDC_FS_SELECT_FILE) {
			TCHAR *s = favoritepopup (hDlg);
			if (s) {
				_tcscpy (current_fsvdlg.rootdir, s);
				xfree (s);
				volumeselectfile (hDlg);
			}
		} else if (GetDlgCtrlID ((HWND)wParam) == IDC_FS_SELECT_DIR) {
			TCHAR *s = favoritepopup (hDlg);
			if (s) {
				_tcscpy (current_fsvdlg.rootdir, s);
				xfree (s);
				volumeselectdir (hDlg, 1);
			}
		}
		break;

	case WM_COMMAND:
		if (recursive)
			break;
		recursive++;
		if (HIWORD (wParam) == BN_CLICKED) {
			switch (LOWORD (wParam))
			{
			case IDC_FS_SELECT_EJECT:
				SetDlgItemText (hDlg, IDC_PATH_NAME, L"");
				SetDlgItemText (hDlg, IDC_VOLUME_NAME, L"");
				CheckDlgButton (hDlg, IDC_FS_RW, TRUE);
				ew (hDlg, IDC_FS_RW, TRUE);
				archivehd = -1;
				break;
			case IDC_FS_SELECT_FILE:
				volumeselectfile (hDlg);
				break;
			case IDC_FS_SELECT_DIR:
				volumeselectdir (hDlg, 0);
				break;
			case IDOK:
				EndDialog (hDlg, 1);
				break;
			case IDCANCEL:
				EndDialog (hDlg, 0);
				break;
			}
		}
		GetDlgItemText (hDlg, IDC_PATH_NAME, current_fsvdlg.rootdir, sizeof current_fsvdlg.rootdir / sizeof (TCHAR));
		GetDlgItemText (hDlg, IDC_VOLUME_NAME, current_fsvdlg.volume, sizeof current_fsvdlg.volume / sizeof (TCHAR));
		GetDlgItemText (hDlg, IDC_VOLUME_DEVICE, current_fsvdlg.device, sizeof current_fsvdlg.device / sizeof (TCHAR));
		current_fsvdlg.rw = ischecked (hDlg, IDC_FS_RW);
		current_fsvdlg.bootpri = GetDlgItemInt (hDlg, IDC_VOLUME_BOOTPRI, NULL, TRUE);
		current_fsvdlg.autoboot = ischecked (hDlg, IDC_FS_AUTOBOOT);
		recursive--;
		break;
	}
	return FALSE;
}

STATIC_INLINE int is_hdf_rdb (void)
{
	return current_hfdlg.sectors == 0 && current_hfdlg.surfaces == 0 && current_hfdlg.reserved == 0;
}

static void sethardfile (HWND hDlg)
{
	int rdb = is_hdf_rdb ();

	SetDlgItemText (hDlg, IDC_PATH_NAME, current_hfdlg.filename);
	SetDlgItemText (hDlg, IDC_PATH_FILESYS, current_hfdlg.fsfilename);
	SetDlgItemText (hDlg, IDC_HARDFILE_DEVICE, current_hfdlg.devicename);
	SetDlgItemInt (hDlg, IDC_SECTORS, current_hfdlg.sectors, FALSE);
	SetDlgItemInt (hDlg, IDC_HEADS, current_hfdlg.surfaces, FALSE);
	SetDlgItemInt (hDlg, IDC_RESERVED, current_hfdlg.reserved, FALSE);
	SetDlgItemInt (hDlg, IDC_BLOCKSIZE, current_hfdlg.blocksize, FALSE);
	SetDlgItemInt (hDlg, IDC_HARDFILE_BOOTPRI, current_hfdlg.bootpri >= -127 ? current_hfdlg.bootpri : -127, TRUE);
	CheckDlgButton (hDlg, IDC_HDF_RW, current_hfdlg.rw);
	CheckDlgButton (hDlg, IDC_HDF_AUTOBOOT, current_hfdlg.autoboot);
	CheckDlgButton (hDlg, IDC_HDF_DONOTMOUNT, current_hfdlg.donotmount);
	ew (hDlg, IDC_HDF_RDB, !rdb);
	ew (hDlg, IDC_HDF_AUTOBOOT, !rdb);
	ew (hDlg, IDC_HDF_DONOTMOUNT, !rdb);
	SendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER, CB_SETCURSEL, current_hfdlg.controller, 0);
}

static void inithdcontroller (HWND hDlg)
{
	SendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER, CB_RESETCONTENT, 0, 0);
	SendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER, CB_ADDSTRING, 0, (LPARAM)L"UAE");
	SendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER, CB_ADDSTRING, 0, (LPARAM)L"IDE0");
	SendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER, CB_ADDSTRING, 0, (LPARAM)L"IDE1");
	SendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER, CB_ADDSTRING, 0, (LPARAM)L"IDE2");
	SendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER, CB_ADDSTRING, 0, (LPARAM)L"IDE3");
	SendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER, CB_ADDSTRING, 0, (LPARAM)L"SCSI0");
	SendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER, CB_ADDSTRING, 0, (LPARAM)L"SCSI1");
	SendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER, CB_ADDSTRING, 0, (LPARAM)L"SCSI2");
	SendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER, CB_ADDSTRING, 0, (LPARAM)L"SCSI3");
	SendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER, CB_ADDSTRING, 0, (LPARAM)L"SCSI4");
	SendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER, CB_ADDSTRING, 0, (LPARAM)L"SCSI5");
	SendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER, CB_ADDSTRING, 0, (LPARAM)L"SCSI6");
	SendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER, CB_ADDSTRING, 0, (LPARAM)L"SCSRAM");
	SendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER, CB_SETCURSEL, 0, 0);
}

static void inithardfile (HWND hDlg)
{
	TCHAR tmp[MAX_DPATH];

	ew (hDlg, IDC_HF_DOSTYPE, FALSE);
	ew (hDlg, IDC_HF_CREATE, FALSE);
	inithdcontroller (hDlg);
	SendDlgItemMessage (hDlg, IDC_HF_TYPE, CB_RESETCONTENT, 0, 0);
	WIN32GUI_LoadUIString (IDS_HF_FS_CUSTOM, tmp, sizeof (tmp) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_HF_TYPE, CB_ADDSTRING, 0, (LPARAM)L"OFS/FFS/RDB");
	SendDlgItemMessage (hDlg, IDC_HF_TYPE, CB_ADDSTRING, 0, (LPARAM)L"SFS");
	SendDlgItemMessage (hDlg, IDC_HF_TYPE, CB_ADDSTRING, 0, (LPARAM)tmp);
	SendDlgItemMessage (hDlg, IDC_HF_TYPE, CB_SETCURSEL, 0, 0);
}

static void sethfdostype (HWND hDlg, int idx)
{
	if (idx == 1)
		SetDlgItemText (hDlg, IDC_HF_DOSTYPE, L"0x53465300");
	else
		SetDlgItemText (hDlg, IDC_HF_DOSTYPE, L"");
}

static void hardfile_testrdb (HWND hDlg, struct hfdlg_vals *hdf)
{
	struct zfile *f = zfile_fopen (hdf->filename, L"rb", ZFD_NORMAL);
	uae_u8 tmp[8];
	int i;

	if (!f)
		return;
	for (i = 0; i < 16; i++) {
		zfile_fseek (f, i * 512, SEEK_SET);
		memset (tmp, 0, sizeof tmp);
		zfile_fread (tmp, 1, sizeof tmp, f);
		if (i == 0 && !memcmp (tmp + 2, "CIS", 3)) {
			hdf->controller = HD_CONTROLLER_PCMCIA_SRAM;
			break;
		}
		if (!memcmp (tmp, "RDSK\0\0\0", 7) || !memcmp (tmp, "DRKS\0\0", 6) || (tmp[0] == 0x53 && tmp[1] == 0x10 && tmp[2] == 0x9b && tmp[3] == 0x13 && tmp[4] == 0 && tmp[5] == 0)) {
			// RDSK or ADIDE "encoded" RDSK
			hdf->sectors = 0;
			hdf->surfaces = 0;
			hdf->reserved = 0;
			hdf->fsfilename[0] = 0;
			hdf->bootpri = 0;
			hdf->autoboot = 1;
			hdf->donotmount = 0;
			hdf->devicename[0] = 0;
			break;
		}
	}
	zfile_fclose (f);
	sethardfile (hDlg);
}

static void updatehdfinfo (HWND hDlg, int force)
{
	static uae_u64 bsize;
	static uae_u8 id[512];
	int blocks, cyls, i;
	TCHAR tmp[200], tmp2[200];
	TCHAR idtmp[9];

	if (force) {
		struct hardfiledata hfd;
		memset (id, 0, sizeof id);
		memset (&hfd, 0, sizeof hfd);
		hfd.readonly = 1;
		hfd.blocksize = 512;
		if (hdf_open (&hfd, current_hfdlg.filename)) {
			for (i = 0; i < 16; i++) {
				hdf_read (&hfd, id, i * 512, 512);
				bsize = hfd.virtsize;
				if (!memcmp (id, "RDSK", 4))
					break;
			}
			if (i == 16)
				hdf_read (&hfd, id, 0, 512);
			hdf_close (&hfd);
		}
	}

	cyls = 0;
	if (current_hfdlg.blocksize * current_hfdlg.sectors * current_hfdlg.surfaces) {
		if (bsize >= 512 * 1024 * 1024 && current_hfdlg.original) {
			getchsgeometry (bsize, &current_hfdlg.cylinders, &current_hfdlg.surfaces, &current_hfdlg.sectors);
			current_hfdlg.original = 0;
		}
		cyls = bsize / (current_hfdlg.blocksize * current_hfdlg.sectors * current_hfdlg.surfaces);
	}
	blocks = cyls * (current_hfdlg.sectors * current_hfdlg.surfaces);
	for (i = 0; i < sizeof (idtmp) / sizeof (TCHAR) - 1; i++) {
		TCHAR c = id[i];
		if (c < 32 || c > 126)
			c = '.';
		idtmp[i] = c;
		idtmp[i + 1] = 0;
	}

	tmp[0] = 0;
	if (bsize) {
		_stprintf (tmp2, L" %s [%02X%02X%02X%02X%02X%02X%02X%02X]", idtmp, id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7]);
		if (!cyls || !blocks) {
			_stprintf (tmp, L"%dMB", bsize / (1024 * 1024));
		} else {
			_stprintf (tmp, L"%u cyls, %u blocks, %.1fMB/%.1fMB",
				cyls, blocks,
				(double)blocks * 1.0 * current_hfdlg.blocksize / (1024.0 * 1024.0),
				(double)bsize / (1024.0 * 1024.0));
			if (cyls > 65535) {
				_stprintf (tmp2, L" %4.4s [%02X%02X%02X%02X]", idtmp, id[0], id[1], id[2], id[3]);
				_tcscat (tmp, L" [Too many cyls]");
			}
		}
		_tcscat (tmp, tmp2);
	}
	SetDlgItemText (hDlg, IDC_HDFINFO, tmp);
}

static void hardfileselecthdf (HWND hDlg, TCHAR *newpath)
{
	DiskSelection (hDlg, IDC_PATH_NAME, 2, &workprefs, newpath);
	GetDlgItemText (hDlg, IDC_PATH_NAME, current_hfdlg.filename, sizeof current_hfdlg.filename / sizeof (TCHAR));
	fullpath (current_hfdlg.filename, sizeof current_hfdlg.filename / sizeof (TCHAR));
	inithardfile (hDlg);
	hardfile_testrdb (hDlg, &current_hfdlg);
	updatehdfinfo (hDlg, 1);
	sethardfile (hDlg);
}

static void hardfilecreatehdf (HWND hDlg, TCHAR *newpath)
{
	TCHAR hdfpath[MAX_DPATH];
	LRESULT res;
	UINT setting = CalculateHardfileSize (hDlg);
	TCHAR dostype[16];
	GetDlgItemText (hDlg, IDC_HF_DOSTYPE, dostype, sizeof (dostype) / sizeof (TCHAR));
	res = SendDlgItemMessage (hDlg, IDC_HF_TYPE, CB_GETCURSEL, 0, 0);
	if (res == 0)
		dostype[0] = 0;
	if (CreateHardFile (hDlg, setting, dostype, newpath, hdfpath)) {
		if (!current_hfdlg.filename[0])
			_tcscpy (current_hfdlg.filename, hdfpath);
	}
	sethardfile (hDlg);
}

static INT_PTR CALLBACK HardfileSettingsProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;
	LRESULT res, posn;
	TCHAR tmp[MAX_DPATH];

	switch (msg) {
	case WM_DROPFILES:
		dragdrop (hDlg, (HDROP)wParam, &changed_prefs, -1);
		return FALSE;

	case WM_INITDIALOG:
		recursive++;
		inithardfile (hDlg);
		sethardfile (hDlg);
		sethfdostype (hDlg, 0);
		updatehdfinfo (hDlg, 1);
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
				hardfileselecthdf (hDlg, path);
			}
		} else if (GetDlgCtrlID ((HWND)wParam) == IDC_FILESYS_SELECTOR) {
			TCHAR *s = favoritepopup (hDlg);
			if (s) {
				TCHAR path[MAX_DPATH];
				_tcscpy (path, s);
				xfree (s);
				DiskSelection (hDlg, IDC_PATH_FILESYS, 12, &workprefs, path);
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
		if (recursive)
			break;
		recursive++;

		switch (LOWORD (wParam)) {
		case IDC_HF_SIZE:
			ew (hDlg, IDC_HF_CREATE, CalculateHardfileSize (hDlg) > 0);
			break;
		case IDC_HF_TYPE:
			res = SendDlgItemMessage (hDlg, IDC_HF_TYPE, CB_GETCURSEL, 0, 0);
			sethfdostype (hDlg, (int)res);
			ew (hDlg, IDC_HF_DOSTYPE, res >= 2);
			break;
		case IDC_HF_CREATE:
			current_hfdlg = empty_hfdlg;
			hardfilecreatehdf (hDlg, NULL);
			break;
		case IDC_SELECTOR:
			current_hfdlg = empty_hfdlg;
			hardfileselecthdf (hDlg, NULL);
			break;
		case IDC_FILESYS_SELECTOR:
			DiskSelection (hDlg, IDC_PATH_FILESYS, 12, &workprefs, 0);
			break;
		case IDOK:
			if (_tcslen (current_hfdlg.filename) == 0) {
				TCHAR szMessage[MAX_DPATH];
				TCHAR szTitle[MAX_DPATH];
				WIN32GUI_LoadUIString (IDS_MUSTSELECTFILE, szMessage, MAX_DPATH);
				WIN32GUI_LoadUIString (IDS_SETTINGSERROR, szTitle, MAX_DPATH);
				MessageBox (hDlg, szMessage, szTitle, MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
				break;
			}
			EndDialog (hDlg, 1);
			break;
		case IDCANCEL:
			EndDialog (hDlg, 0);
			break;
		case IDC_HDF_RW:
			current_hfdlg.rw = ischecked (hDlg, IDC_HDF_RW);
			break;
		case IDC_HDF_AUTOBOOT:
			current_hfdlg.autoboot = ischecked (hDlg, IDC_HDF_AUTOBOOT);
			break;
		case IDC_HDF_DONOTMOUNT:
			current_hfdlg.donotmount = ischecked (hDlg, IDC_HDF_DONOTMOUNT);
			break;
		case IDC_HDF_RDB:
			SetDlgItemInt (hDlg, IDC_SECTORS, 0, FALSE);
			SetDlgItemInt (hDlg, IDC_RESERVED, 0, FALSE);
			SetDlgItemInt (hDlg, IDC_HEADS, 0, FALSE);
			SetDlgItemText (hDlg, IDC_PATH_FILESYS, L"");
			SetDlgItemText (hDlg, IDC_HARDFILE_DEVICE, L"");
			current_hfdlg.sectors = current_hfdlg.reserved = current_hfdlg.surfaces = 0;
			current_hfdlg.bootpri = 0;
			current_hfdlg.autoboot = 1;
			current_hfdlg.donotmount = 0;
			sethardfile (hDlg);
			break;
		}

		current_hfdlg.sectors   = GetDlgItemInt (hDlg, IDC_SECTORS, NULL, FALSE);
		current_hfdlg.reserved  = GetDlgItemInt (hDlg, IDC_RESERVED, NULL, FALSE);
		current_hfdlg.surfaces  = GetDlgItemInt (hDlg, IDC_HEADS, NULL, FALSE);
		current_hfdlg.blocksize = GetDlgItemInt (hDlg, IDC_BLOCKSIZE, NULL, FALSE);
		current_hfdlg.bootpri = GetDlgItemInt (hDlg, IDC_HARDFILE_BOOTPRI, NULL, TRUE);
		GetDlgItemText (hDlg, IDC_PATH_NAME, tmp, sizeof tmp / sizeof (TCHAR));
		if (_tcscmp (tmp, current_hfdlg.filename)) {
			_tcscpy (current_hfdlg.filename, tmp);
			updatehdfinfo (hDlg, 1);
		}
		GetDlgItemText (hDlg, IDC_PATH_FILESYS, current_hfdlg.fsfilename, sizeof current_hfdlg.fsfilename / sizeof (TCHAR));
		GetDlgItemText (hDlg, IDC_HARDFILE_DEVICE, current_hfdlg.devicename, sizeof current_hfdlg.devicename / sizeof (TCHAR));
		posn = SendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER, CB_GETCURSEL, 0, 0);
		if (posn != CB_ERR)
			current_hfdlg.controller = posn;
		updatehdfinfo (hDlg, 0);
		recursive--;

		break;
	}
	return FALSE;
}

extern int harddrive_to_hdf (HWND, struct uae_prefs*, int);
static INT_PTR CALLBACK HarddriveSettingsProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;
	int i;
	LRESULT posn;
	static int oposn;

	switch (msg) {
	case WM_INITDIALOG:
		{
			int index;
			oposn = -1;
			hdf_init_target ();
			recursive++;
			inithdcontroller (hDlg);
			CheckDlgButton (hDlg, IDC_HDF_RW, current_hfdlg.rw);
			SendDlgItemMessage (hDlg, IDC_HARDDRIVE, CB_RESETCONTENT, 0, 0);
			ew (hDlg, IDC_HARDDRIVE_IMAGE, FALSE);
			ew (hDlg, IDOK, FALSE);
			ew (hDlg, IDC_HDF_RW, FALSE);
			ew (hDlg, IDC_HDF_CONTROLLER, FALSE);
			index = -1;
			for (i = 0; i < hdf_getnumharddrives (); i++) {
				SendDlgItemMessage (hDlg, IDC_HARDDRIVE, CB_ADDSTRING, 0, (LPARAM)hdf_getnameharddrive (i, 1, NULL, NULL));
				if (!_tcscmp (current_hfdlg.filename, hdf_getnameharddrive (i, 0, NULL, NULL)))
					index = i;
			}
			if (index >= 0) {
				SendDlgItemMessage (hDlg, IDC_HARDDRIVE, CB_SETCURSEL, index, 0);
				SendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER, CB_SETCURSEL, current_hfdlg.controller, 0);
			}
			recursive--;
			return TRUE;
		}
	case WM_COMMAND:
		if (recursive)
			break;
		recursive++;
		posn = SendDlgItemMessage (hDlg, IDC_HARDDRIVE, CB_GETCURSEL, 0, 0);
		if (oposn != posn && posn != CB_ERR) {
			oposn = posn;
			if (posn >= 0) {
				int dang = 1;
				hdf_getnameharddrive (posn, 1, NULL, &dang);
				ew (hDlg, IDC_HARDDRIVE_IMAGE, TRUE);
				ew (hDlg, IDOK, TRUE);
				ew (hDlg, IDC_HDF_RW, !dang);
				if (dang)
					current_hfdlg.rw = FALSE;
				ew (hDlg, IDC_HDF_CONTROLLER, TRUE);
				hardfile_testrdb (hDlg, &current_hfdlg);
				SendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER, CB_SETCURSEL, current_hfdlg.controller, 0);
				CheckDlgButton(hDlg, IDC_HDF_RW, current_hfdlg.rw);
			}
		}
		if (HIWORD (wParam) == BN_CLICKED) {
			switch (LOWORD (wParam)) {
			case IDOK:
				EndDialog (hDlg, 1);
				break;
			case IDCANCEL:
				EndDialog (hDlg, 0);
				break;
			case IDC_HARDDRIVE_IMAGE:
				if (posn != CB_ERR)
					harddrive_to_hdf (hDlg, &workprefs, posn);
				break;
			}
		}
		if (posn != CB_ERR)
			_tcscpy (current_hfdlg.filename, hdf_getnameharddrive ((int)posn, 0, &current_hfdlg.blocksize, NULL));
		current_hfdlg.rw = ischecked (hDlg, IDC_HDF_RW);
		posn = SendDlgItemMessage (hDlg, IDC_HDF_CONTROLLER, CB_GETCURSEL, 0, 0);
		if (posn != CB_ERR)
			current_hfdlg.controller = posn;
		recursive--;
		break;
	}
	return FALSE;
}

static int tweakbootpri (int bp, int ab, int dnm)
{
	if (dnm)
		return -129;
	if (!ab)
		return -128;
	if (bp < -127)
		bp = -127;
	return bp;
}

static void new_filesys (HWND hDlg, int entry)
{
	struct uaedev_config_info *uci;
	int bp = tweakbootpri (current_fsvdlg.bootpri, current_fsvdlg.autoboot, current_fsvdlg.donotmount);

	uci = add_filesys_config (&workprefs, entry, current_fsvdlg.device, current_fsvdlg.volume,
		current_fsvdlg.rootdir, ! current_fsvdlg.rw, 0, 0, 0, 0, bp, 0, 0, 0);
	if (uci) {
		if (uci->rootdir[0])
			filesys_media_change (uci->rootdir, 1, uci);
		else
			filesys_eject (uci->configoffset);
	}
}

static void new_hardfile (HWND hDlg, int entry)
{
	struct uaedev_config_info *uci;
	int bp = tweakbootpri (current_hfdlg.bootpri, current_hfdlg.autoboot, current_hfdlg.donotmount);

	uci = add_filesys_config (&workprefs, entry, current_hfdlg.devicename, 0,
		current_hfdlg.filename, ! current_hfdlg.rw,
		current_hfdlg.sectors, current_hfdlg.surfaces,
		current_hfdlg.reserved, current_hfdlg.blocksize,
		bp, current_hfdlg.fsfilename,
		current_hfdlg.controller, 0);
	if (uci)
		hardfile_do_disk_change (uci, 1);
}

static void new_harddrive (HWND hDlg, int entry)
{
	struct uaedev_config_info *uci;

	uci = add_filesys_config (&workprefs, entry, 0, 0,
		current_hfdlg.filename, ! current_hfdlg.rw, 0, 0,
		0, current_hfdlg.blocksize, 0, 0, current_hfdlg.controller, 0);
	if (uci)
		hardfile_do_disk_change (uci, 1);
}

static void harddisk_remove (HWND hDlg)
{
	int entry = listview_find_selected (GetDlgItem (hDlg, IDC_VOLUMELIST));
	if (entry < 0)
		return;
	kill_filesys_unitconfig (&workprefs, entry);
}

static void harddisk_move (HWND hDlg, int up)
{
	int entry = listview_find_selected (GetDlgItem (hDlg, IDC_VOLUMELIST));
	if (entry < 0)
		return;
	move_filesys_unitconfig (&workprefs, entry, up ? entry - 1 : entry + 1);
}

static void harddisk_edit (HWND hDlg)
{
	int entry = listview_find_selected (GetDlgItem (hDlg, IDC_VOLUMELIST));
	int type;
	struct uaedev_config_info *uci;
	struct mountedinfo mi;

	if (entry < 0 || entry >= workprefs.mountitems)
		return;
	uci = &workprefs.mountconfig[entry];

	type = get_filesys_unitconfig (&workprefs, entry, &mi);
	if (type < 0)
		type = uci->ishdf ? FILESYS_HARDFILE : FILESYS_VIRTUAL;

	if(type == FILESYS_HARDFILE || type == FILESYS_HARDFILE_RDB)
	{
		current_hfdlg.sectors = uci->sectors;
		current_hfdlg.surfaces = uci->surfaces;
		current_hfdlg.reserved = uci->reserved;
		current_hfdlg.cylinders = mi.nrcyls;
		current_hfdlg.blocksize = uci->blocksize;
		current_hfdlg.controller = uci->controller;

		_tcsncpy (current_hfdlg.filename, uci->rootdir, (sizeof current_hfdlg.filename  / sizeof (TCHAR)) - 1);
		current_hfdlg.filename[(sizeof current_hfdlg.filename / sizeof (TCHAR)) - 1] = '\0';
		current_hfdlg.fsfilename[0] = 0;
		if (uci->filesys) {
			_tcsncpy (current_hfdlg.fsfilename, uci->filesys, (sizeof current_hfdlg.fsfilename / sizeof (TCHAR)) - 1);
			current_hfdlg.fsfilename[(sizeof current_hfdlg.fsfilename / sizeof (TCHAR)) - 1] = '\0';
		}
		current_fsvdlg.device[0] = 0;
		if (uci->devname) {
			_tcsncpy (current_hfdlg.devicename, uci->devname, (sizeof current_hfdlg.devicename / sizeof (TCHAR)) - 1);
			current_hfdlg.devicename[(sizeof current_hfdlg.devicename / sizeof (TCHAR)) - 1] = '\0';
		}
		current_hfdlg.rw = !uci->readonly;
		current_hfdlg.bootpri = uci->bootpri;
		current_hfdlg.autoboot = uci->autoboot;
		current_hfdlg.donotmount = uci->donotmount;
		if (CustomDialogBox (IDD_HARDFILE, hDlg, HardfileSettingsProc)) {
			new_hardfile (hDlg, entry);
		}
	}
	else if (type == FILESYS_HARDDRIVE) /* harddisk */
	{
		current_hfdlg.controller = uci->controller;
		current_hfdlg.rw = !uci->readonly;
		_tcsncpy (current_hfdlg.filename, uci->rootdir, (sizeof current_hfdlg.filename) / sizeof (TCHAR) - 1);
		current_hfdlg.filename[(sizeof current_hfdlg.filename) / sizeof (TCHAR) - 1] = '\0';
		if (CustomDialogBox (IDD_HARDDRIVE, hDlg, HarddriveSettingsProc)) {
			new_harddrive (hDlg, entry);
		}
	}
	else /* Filesystem */
	{
		_tcsncpy (current_fsvdlg.rootdir, uci->rootdir, (sizeof current_fsvdlg.rootdir / sizeof (TCHAR)) - 1);
		current_fsvdlg.rootdir[sizeof (current_fsvdlg.rootdir) / sizeof (TCHAR) - 1] = '\0';
		_tcsncpy (current_fsvdlg.volume, uci->volname, (sizeof current_fsvdlg.volume / sizeof (TCHAR)) - 1);
		current_fsvdlg.volume[sizeof (current_fsvdlg.volume) / sizeof (TCHAR) - 1] = '\0';
		current_fsvdlg.device[0] = 0;
		if (uci->devname) {
			_tcsncpy (current_fsvdlg.device, uci->devname, (sizeof current_fsvdlg.device) / sizeof (TCHAR) - 1);
			current_fsvdlg.device[sizeof (current_fsvdlg.device) / sizeof (TCHAR) - 1] = '\0';
		}
		current_fsvdlg.rw = !uci->readonly;
		current_fsvdlg.bootpri = uci->bootpri;
		current_fsvdlg.autoboot = uci->autoboot;
		current_fsvdlg.donotmount = uci->donotmount;
		archivehd = -1;
		if (CustomDialogBox (IDD_FILESYS, hDlg, VolumeSettingsProc)) {
			new_filesys (hDlg, entry);
		}
	}
}

static ACCEL HarddiskAccel[] = {
	{ FVIRTKEY, VK_UP, 10001 }, { FVIRTKEY, VK_DOWN, 10002 },
	{ FVIRTKEY|FSHIFT, VK_UP, IDC_UP }, { FVIRTKEY|FSHIFT, VK_DOWN, IDC_DOWN },
	{ FVIRTKEY, VK_RETURN, IDC_EDIT }, { FVIRTKEY, VK_DELETE, IDC_REMOVE },
	{ 0, 0, 0 }
};

static int harddiskdlg_button (HWND hDlg, WPARAM wParam)
{
	int button = LOWORD (wParam);
	switch (button) {
	case IDC_CD_SELECT:
		DiskSelection (hDlg, wParam, 17, &workprefs, NULL);
		quickstart_cdtype = 1;
		workprefs.cdslots[0].inuse = true;
		addcdtype (hDlg, IDC_CD_TYPE);
		break;
	case IDC_CD_EJECT:
		eject_cd ();
		SetDlgItemText (hDlg, IDC_CD_TEXT, L"");
		addcdtype (hDlg, IDC_CD_TYPE);
		break;
	case IDC_NEW_FS:
		current_fsvdlg = empty_fsvdlg;
		archivehd = 0;
		if (CustomDialogBox (IDD_FILESYS, hDlg, VolumeSettingsProc))
			new_filesys (hDlg, -1);
		return 1;
	case IDC_NEW_FSARCH:
		archivehd = 1;
		current_fsvdlg = empty_fsvdlg;
		if (CustomDialogBox (IDD_FILESYS, hDlg, VolumeSettingsProc))
			new_filesys (hDlg, -1);
		return 1;

	case IDC_NEW_HF:
		current_hfdlg = empty_hfdlg;
		if (CustomDialogBox (IDD_HARDFILE, hDlg, HardfileSettingsProc))
			new_hardfile (hDlg, -1);
		return 1;

	case IDC_NEW_HD:
		memset (&current_hfdlg, 0, sizeof (current_hfdlg));
		if (hdf_init_target () == 0) {
			TCHAR tmp[MAX_DPATH];
			WIN32GUI_LoadUIString (IDS_NOHARDDRIVES, tmp, sizeof (tmp) / sizeof (TCHAR));
			gui_message (tmp);
		} else {
			if (CustomDialogBox (IDD_HARDDRIVE, hDlg, HarddriveSettingsProc))
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

	case IDC_MAPDRIVES_NET:
		workprefs.win32_automount_netdrives = ischecked (hDlg, IDC_MAPDRIVES_NET);
		break;

	case IDC_NOUAEFSDB:
		workprefs.filesys_no_uaefsdb = ischecked (hDlg, IDC_NOUAEFSDB);
		break;

	case IDC_NORECYCLEBIN:
		workprefs.win32_norecyclebin = ischecked (hDlg, IDC_NORECYCLEBIN);
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
		entry = listview_entry_from_click (list, 0);
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
	}
}

static void hilitehd (void)
{
	int total = ListView_GetItemCount (cachedlist);
	if (total <= 0)
		return;
	if (clicked_entry < 0)
		clicked_entry = 0;
	if (clicked_entry >= total)
		clicked_entry = total;
	ListView_SetItemState (cachedlist, clicked_entry, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

static INT_PTR CALLBACK HarddiskDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_INITDIALOG:
		clicked_entry = 0;
		pages[HARDDISK_ID] = hDlg;
		currentpage = HARDDISK_ID;
		Button_SetElevationRequiredState (GetDlgItem (hDlg, IDC_NEW_HD), TRUE);
		EnableWindow (GetDlgItem (hDlg, IDC_NEW_HD), os_winnt_admin > 1 ? TRUE : FALSE);

	case WM_USER:
		CheckDlgButton (hDlg, IDC_MAPDRIVES_AUTO, workprefs.win32_automount_removable);
		CheckDlgButton (hDlg, IDC_MAPDRIVES, workprefs.win32_automount_drives);
		CheckDlgButton (hDlg, IDC_MAPDRIVES_CD, workprefs.win32_automount_cddrives);
		CheckDlgButton (hDlg, IDC_MAPDRIVES_NET, workprefs.win32_automount_netdrives);
		CheckDlgButton (hDlg, IDC_MAPDRIVES_REMOVABLE, workprefs.win32_automount_removabledrives);
		CheckDlgButton (hDlg, IDC_NOUAEFSDB, workprefs.filesys_no_uaefsdb);
		CheckDlgButton (hDlg, IDC_NORECYCLEBIN, workprefs.win32_norecyclebin);
		addfloppyhistory_2 (hDlg, 0, IDC_CD_TEXT, HISTORY_CD);
		addcdtype (hDlg, IDC_CD_TYPE);
		InitializeListView (hDlg);
		hilitehd ();
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
				hilitehd ();
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
			addcdtype (hDlg, IDC_CD_TYPE);
			addfloppyhistory_2 (hDlg, 0, IDC_CD_TEXT, HISTORY_CD);
			break;
			case IDC_CD_TYPE:
			int val = SendDlgItemMessage (hDlg, IDC_CD_TYPE, CB_GETCURSEL, 0, 0);
			if (val != CB_ERR) {
				quickstart_cdtype = val;
				if (quickstart_cdtype >= 2) {
					int len = sizeof quickstart_cddrive / sizeof (TCHAR);
					quickstart_cdtype = 2;
					workprefs.cdslots[0].inuse = true;
					SendDlgItemMessage (hDlg, IDC_CD_TYPE, WM_GETTEXT, (WPARAM)len, (LPARAM)quickstart_cddrive);
					_tcscpy (workprefs.cdslots[0].name, quickstart_cddrive);
				} else {
					eject_cd ();
					quickstart_cdtype = val;
					if (val > 0)
						workprefs.cdslots[0].inuse = true;

				}
				addcdtype (hDlg, IDC_CD_TYPE);
				addfloppyhistory_2 (hDlg, 0, IDC_CD_TEXT, HISTORY_CD);
			}
			break;
			}
		} else {
			switch (LOWORD(wParam))
			{
			case 10001:
				clicked_entry--;
				hilitehd ();
				break;
			case 10002:
				clicked_entry++;
				hilitehd ();
				break;
			default:
				if (harddiskdlg_button (hDlg, wParam)) {
					InitializeListView (hDlg);
					hilitehd ();
				}
				break;
			}
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
		_stprintf (txt, L"%d%%%s", workprefs.floppy_speed, workprefs.floppy_speed == 100 ? tmp1 : L"");
	else
		_tcscpy (txt, tmp2);
	SetDlgItemText (hDlg, IDC_FLOPPYSPDTEXT, txt);
}

#define BUTTONSPERFLOPPY 8
static int floppybuttons[][BUTTONSPERFLOPPY] = {
	{ IDC_DF0TEXT,IDC_DF0,IDC_EJECT0,IDC_DF0TYPE,IDC_DF0WP,-1,IDC_SAVEIMAGE0,IDC_DF0ENABLE },
	{ IDC_DF1TEXT,IDC_DF1,IDC_EJECT1,IDC_DF1TYPE,IDC_DF1WP,-1,IDC_SAVEIMAGE1,IDC_DF1ENABLE },
	{ IDC_DF2TEXT,IDC_DF2,IDC_EJECT2,IDC_DF2TYPE,IDC_DF2WP,-1,IDC_SAVEIMAGE2,IDC_DF2ENABLE },
	{ IDC_DF3TEXT,IDC_DF3,IDC_EJECT3,IDC_DF3TYPE,IDC_DF3WP,-1,IDC_SAVEIMAGE3,IDC_DF3ENABLE }
};
static int floppybuttonsq[][BUTTONSPERFLOPPY] = {
	{ IDC_DF0TEXTQ,IDC_DF0QQ,IDC_EJECT0Q,-1,IDC_DF0WPQ,IDC_DF0WPTEXTQ,-1,IDC_DF0QENABLE },
	{ IDC_DF1TEXTQ,IDC_DF1QQ,IDC_EJECT1Q,-1,IDC_DF1WPQ,IDC_DF1WPTEXTQ,-1,IDC_DF1QENABLE },
	{ -1,-1,-1,-1,-1,-1,-1 },
	{ -1,-1,-1,-1,-1,-1,-1 }
};

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
	ti.cbSize = sizeof (TOOLINFO);
	ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
	ti.hwnd = hDlg;
	ti.hinst = hInst;
	ti.uId = (UINT_PTR)GetDlgItem (hDlg, id);
	SendMessage (ToolTipHWND, TTM_DELTOOL, 0, (LPARAM) (LPTOOLINFO) &ti);
	if (crc32 == 0)
		return;
	_stprintf (tmp, L"CRC=%08X", crc32);
	ti.lpszText = tmp;
	SendMessage (ToolTipHWND, TTM_ADDTOOL, 0, (LPARAM) (LPTOOLINFO) &ti);
}

static void addfloppyhistory_2 (HWND hDlg, int n, int f_text, int type)
{
	int i, j;
	TCHAR *s, *text;
	UAEREG *fkey;
	int nn, curidx;

	if (f_text < 0)
		return;
	SendDlgItemMessage (hDlg, f_text, CB_RESETCONTENT, 0, 0);
	if (type == HISTORY_CD) {
		nn = 1;
		text = workprefs.cdslots[0].name;
	} else {
		nn = workprefs.floppyslots[n].dfxtype + 1;
		text = workprefs.floppyslots[n].df;
	}
	SendDlgItemMessage (hDlg, f_text, WM_SETTEXT, 0, (LPARAM)text);
	fkey = read_disk_history (type);
	if (fkey == NULL)
		return;
	curidx = -1;
	i = 0;
	while (s = DISK_history_get (i, type)) {
		TCHAR tmpname[MAX_DPATH], tmppath[MAX_DPATH], *p, *p2;
		if (_tcslen (s) == 0)
			break;
		i++;
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
			if (*p == '\\' || *p == '/')
				break;
			p--;
		}
		_tcscpy (tmpname, p + 1);
		*++p = 0;
		if (tmppath[0]) {
			_tcscat (tmpname, L" { ");
			_tcscat (tmpname, tmppath);
			_tcscat (tmpname, L" }");
		}
		if (f_text >= 0)
			SendDlgItemMessage (hDlg, f_text, CB_ADDSTRING, 0, (LPARAM)tmpname);
		if (!_tcscmp (text, s))
			curidx = i - 1;
		if (nn <= 0)
			break;
	}
	if (f_text >= 0 && curidx >= 0)
		SendDlgItemMessage (hDlg, f_text, CB_SETCURSEL, curidx, 0);
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
		if (f_text >= 0)
			addfloppyhistory_2 (hDlg, n, f_text, iscd (n) ? HISTORY_CD : HISTORY_FLOPPY);
	}
}

static void addcdtype (HWND hDlg, int id)
{
	TCHAR tmp[MAX_DPATH];
	SendDlgItemMessage (hDlg, id, CB_RESETCONTENT, 0, 0L);
	WIN32GUI_LoadUIString (IDS_QS_CD_AUTO, tmp, sizeof tmp / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)tmp);
	WIN32GUI_LoadUIString (IDS_QS_CD_IMAGE, tmp, sizeof tmp / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)tmp);
	int cdtype = quickstart_cdtype;
	if (currentpage != QUICKSTART_ID) {
		if (full_property_sheet && !workprefs.cdslots[0].inuse && !workprefs.cdslots[0].name[0])
			cdtype = 0;
	}
	int cnt = 2;
	for (int drive = 'C'; drive <= 'Z'; ++drive) {
		TCHAR vol[100];
		_stprintf (vol, L"%c:\\", drive);
		int drivetype = GetDriveType (vol);
		if (drivetype == DRIVE_CDROM) {
			SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)vol);
			if (!_tcsicmp (vol, quickstart_cddrive)) {
				cdtype = quickstart_cdtype = cnt;
				_tcscpy (workprefs.cdslots[0].name, vol);
			}
			cnt++;
		}
	}
	SendDlgItemMessage (hDlg, id, CB_SETCURSEL, cdtype, 0);
}

static void addfloppytype (HWND hDlg, int n)
{
	int state, chk;
	int nn = workprefs.floppyslots[n].dfxtype + 1;
	int showcd = 0;
	TCHAR *text;

	int f_text = floppybuttons[n][0];
	int f_drive = floppybuttons[n][1];
	int f_eject = floppybuttons[n][2];
	int f_type = floppybuttons[n][3];
	int f_wp = floppybuttons[n][4];
	int f_wptext = floppybuttons[n][5];
	int f_si = floppybuttons[n][6];
	int f_enable = floppybuttons[n][7];

	text = workprefs.floppyslots[n].df;
	if (currentpage == QUICKSTART_ID) {
		TCHAR tmp[MAX_DPATH];
		f_text = floppybuttonsq[n][0];
		f_drive = floppybuttonsq[n][1];
		f_type = -1;
		f_eject = floppybuttonsq[n][2];
		f_wp = floppybuttonsq[n][4];
		f_wptext = floppybuttonsq[n][5];
		f_si = -1;
		f_enable = floppybuttonsq[n][7];
		if (iscd (n))
			showcd = 1;
		if (showcd) {
			nn = 1;
			hide (hDlg, f_wp, 1);
			hide (hDlg, f_wptext, 1);
			ew (hDlg, f_enable, FALSE);
			WIN32GUI_LoadUIString (IDS_QS_CD, tmp, sizeof tmp / sizeof (TCHAR));
			SetWindowText (GetDlgItem (hDlg, f_enable), tmp);
			addcdtype (hDlg, IDC_CD0Q_TYPE);
			hide (hDlg, IDC_CD0Q_TYPE, 0);
			text = workprefs.cdslots[0].name;
			regsetstr (NULL, L"QuickStartCDDrive", quickstart_cdtype >= 2 ? quickstart_cddrive : L"");
			regsetint (NULL, L"QuickStartCDType", quickstart_cdtype >= 2 ? 2 : quickstart_cdtype);
		} else {
			hide (hDlg, f_wp, 0);
			hide (hDlg, f_wptext, 0);
		}
	}
	if (!showcd && f_enable > 0 && n == 1 && currentpage == QUICKSTART_ID) {
		static TCHAR drivedf1[MAX_DPATH];
		if (drivedf1[0] == 0)
			GetDlgItemText(hDlg, f_enable, drivedf1, sizeof drivedf1 / sizeof (TCHAR));
		ew (hDlg, f_enable, TRUE);
		SetWindowText (GetDlgItem (hDlg, f_enable), drivedf1);
		hide (hDlg, IDC_CD0Q_TYPE, 1);
	}

	if (nn <= 0)
		state = FALSE;
	else
		state = TRUE;
	if (f_type >= 0)
		SendDlgItemMessage (hDlg, f_type, CB_SETCURSEL, nn, 0);
	if (f_si >= 0)
		ShowWindow (GetDlgItem(hDlg, f_si), !showcd && zfile_exists (DISK_get_saveimagepath (text)) ? SW_SHOW : SW_HIDE);

	if (f_text >= 0)
		ew (hDlg, f_text, state);
	if (f_eject >= 0)
		ew (hDlg, f_eject, TRUE);
	if (f_drive >= 0)
		ew (hDlg, f_drive, state);
	if (f_enable >= 0) {
		if (currentpage == QUICKSTART_ID) {
			ew (hDlg, f_enable, (n > 0 && workprefs.nr_floppies > 0) && !showcd);
		} else {
			ew (hDlg, f_enable, TRUE);
		}
		CheckDlgButton (hDlg, f_enable, state ? BST_CHECKED : BST_UNCHECKED);
	}
	chk = !showcd && disk_getwriteprotect (text) && state == TRUE ? BST_CHECKED : 0;
	if (f_wp >= 0)
		CheckDlgButton (hDlg, f_wp, chk);
	chk = !showcd && state && DISK_validate_filename (text, 0, NULL, NULL, NULL) ? TRUE : FALSE;
	if (f_wp >= 0) {
		ew (hDlg, f_wp, chk);
		if (f_wptext >= 0)
			ew (hDlg, f_wptext, chk);
	}
}

static void getfloppytype (HWND hDlg, int n)
{
	int f_type = floppybuttons[n][3];
	LRESULT val = SendDlgItemMessage (hDlg, f_type, CB_GETCURSEL, 0, 0L);

	if (val != CB_ERR && workprefs.floppyslots[n].dfxtype != val - 1) {
		workprefs.floppyslots[n].dfxtype = (int)val - 1;
		addfloppytype (hDlg, n);
	}
}
static void getfloppytypeq (HWND hDlg, int n)
{
	int f_enable = currentpage == QUICKSTART_ID ? floppybuttonsq[n][7] : floppybuttons[n][7];
	int chk;

	if (f_enable <= 0 || (n == 0 && currentpage == QUICKSTART_ID))
		return;
	if (iscd (n))
		return;
	chk = ischecked (hDlg, f_enable) ? 0 : -1;
	if (chk != workprefs.floppyslots[n].dfxtype) {
		workprefs.floppyslots[n].dfxtype = chk;
		addfloppytype (hDlg, n);
	}
	if (currentpage == QUICKSTART_ID) {
		if (chk == 0)
			quickstart_floppy = 2;
		else
			quickstart_floppy = 1;
		regsetint (NULL, L"QuickStartFloppies", quickstart_floppy);
	}
}

static int getfloppybox (HWND hDlg, int f_text, TCHAR *out, int maxlen, int type)
{
	LRESULT val;
	TCHAR *p1, *p2, *p;
	TCHAR *tmp;
	int i;

	out[0] = 0;
	val = SendDlgItemMessage (hDlg, f_text, CB_GETCURSEL, 0, 0L);
	if (val != CB_ERR)
		val = SendDlgItemMessage (hDlg, f_text, CB_GETLBTEXT, (WPARAM)val, (LPARAM)out);
	else
		SendDlgItemMessage (hDlg, f_text, WM_GETTEXT, (WPARAM)maxlen, (LPARAM)out);

	tmp = xmalloc (TCHAR, maxlen + 1);
	_tcscpy (tmp, out);
	p1 = _tcsstr(tmp, L" { ");
	p2 = _tcsstr(tmp, L" }");
	if (p1 && p2 && p2 > p1) {
		*p1 = 0;
		memset (out, 0, maxlen * sizeof (TCHAR));
		memcpy (out, p1 + 3, (p2 - p1 - 3) * sizeof (TCHAR));
		_tcscat (out, tmp);
	}
	xfree (tmp);
	i = 0;
	while ((p = DISK_history_get (i, type))) {
		if (!_tcscmp (p, out)) {
			DISK_history_add (out, -1, type, 0);
			break;
		}
		i++;
	}
	return out[0] ? 1 : 0;
}

static void getfloppyname (HWND hDlg, int n, int cd, int f_text)
{
	TCHAR tmp[MAX_DPATH];

	if (getfloppybox (hDlg, f_text, tmp, sizeof (tmp) / sizeof (TCHAR), cd ? HISTORY_CD : HISTORY_FLOPPY)) {
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
		disk_setwriteprotect (n, workprefs.floppyslots[n].df, writeprotected);
		addfloppytype (hDlg, n);
	}
}

static void deletesaveimage (HWND hDlg, int num)
{
	TCHAR *p;
	if (iscd (num))
		return;
	p = DISK_get_saveimagepath (workprefs.floppyslots[num].df);
	if (zfile_exists (p)) {
		DeleteFile (p);
		DISK_reinsert (num);
		addfloppytype (hDlg, num);
	}
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
	int i;
	static TCHAR diskname[40] = { L"" };

	switch (msg)
	{
	case WM_INITDIALOG:
		{
			TCHAR ft35dd[20], ft35hd[20], ft35ddpc[20], ft35hdpc[20],  ft525sd[20], ftdis[20], ft35ddescom[20];
			int df0texts[] = { IDC_DF0TEXT, IDC_DF1TEXT, IDC_DF2TEXT, IDC_DF3TEXT, -1 };

			WIN32GUI_LoadUIString (IDS_FLOPPYTYPE35DD, ft35dd, sizeof ft35dd / sizeof (TCHAR));
			WIN32GUI_LoadUIString (IDS_FLOPPYTYPE35HD, ft35hd, sizeof ft35hd / sizeof (TCHAR));
			WIN32GUI_LoadUIString (IDS_FLOPPYTYPE35DDPC, ft35ddpc, sizeof ft35ddpc / sizeof (TCHAR));
			WIN32GUI_LoadUIString (IDS_FLOPPYTYPE35HDPC, ft35hdpc, sizeof ft35hdpc / sizeof (TCHAR));
			WIN32GUI_LoadUIString (IDS_FLOPPYTYPE525SD, ft525sd, sizeof ft525sd / sizeof (TCHAR));
			WIN32GUI_LoadUIString (IDS_FLOPPYTYPE35DDESCOM, ft35ddescom, sizeof ft35ddescom / sizeof (TCHAR));
			WIN32GUI_LoadUIString (IDS_FLOPPYTYPEDISABLED, ftdis, sizeof ftdis / sizeof (TCHAR));
			pages[FLOPPY_ID] = hDlg;
			if (workprefs.floppy_speed > 0 && workprefs.floppy_speed < 10)
				workprefs.floppy_speed = 100;
			currentpage = FLOPPY_ID;
			SendDlgItemMessage (hDlg, IDC_FLOPPYSPD, TBM_SETRANGE, TRUE, MAKELONG (0, 4));
			SendDlgItemMessage (hDlg, IDC_FLOPPYSPD, TBM_SETPAGESIZE, 0, 1);
			SendDlgItemMessage (hDlg, IDC_FLOPPYTYPE, CB_RESETCONTENT, 0, 0L);
			SendDlgItemMessage (hDlg, IDC_FLOPPYTYPE, CB_ADDSTRING, 0, (LPARAM)ft35dd);
			SendDlgItemMessage (hDlg, IDC_FLOPPYTYPE, CB_ADDSTRING, 0, (LPARAM)ft35hd);
			SendDlgItemMessage (hDlg, IDC_FLOPPYTYPE, CB_ADDSTRING, 0, (LPARAM)ft35ddpc);
			SendDlgItemMessage (hDlg, IDC_FLOPPYTYPE, CB_ADDSTRING, 0, (LPARAM)ft35hdpc);
			SendDlgItemMessage (hDlg, IDC_FLOPPYTYPE, CB_ADDSTRING, 0, (LPARAM)ft525sd);
			SendDlgItemMessage (hDlg, IDC_FLOPPYTYPE, CB_SETCURSEL, 0, 0);
			for (i = 0; i < 4; i++) {
				int f_type = floppybuttons[i][3];
				SendDlgItemMessage (hDlg, f_type, CB_RESETCONTENT, 0, 0L);
				SendDlgItemMessage (hDlg, f_type, CB_ADDSTRING, 0, (LPARAM)ftdis);
				SendDlgItemMessage (hDlg, f_type, CB_ADDSTRING, 0, (LPARAM)ft35dd);
				SendDlgItemMessage (hDlg, f_type, CB_ADDSTRING, 0, (LPARAM)ft35hd);
				SendDlgItemMessage (hDlg, f_type, CB_ADDSTRING, 0, (LPARAM)ft525sd);
				SendDlgItemMessage (hDlg, f_type, CB_ADDSTRING, 0, (LPARAM)ft35ddescom);
			}
			setmultiautocomplete (hDlg, df0texts);
		}
	case WM_USER:
		recursive++;
		setfloppytexts (hDlg, false);
		SetDlgItemText (hDlg, IDC_CREATE_NAME, diskname);
		SendDlgItemMessage (hDlg, IDC_FLOPPYSPD, TBM_SETPOS, TRUE,
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
		if (HIWORD (wParam) == CBN_SELCHANGE || HIWORD (wParam) == CBN_KILLFOCUS)  {
			switch (LOWORD (wParam))
			{
			case IDC_DF0TEXT:
			case IDC_DF0TEXTQ:
				getfloppyname (hDlg, 0);
				addfloppytype (hDlg, 0);
				addfloppyhistory (hDlg);
				break;
			case IDC_DF1TEXT:
			case IDC_DF1TEXTQ:
				getfloppyname (hDlg, 1);
				addfloppytype (hDlg, 1);
				addfloppyhistory (hDlg);
				break;
			case IDC_DF2TEXT:
				getfloppyname (hDlg, 2);
				addfloppytype (hDlg, 2);
				addfloppyhistory (hDlg);
				break;
			case IDC_DF3TEXT:
				getfloppyname (hDlg, 3);
				addfloppytype (hDlg, 3);
				addfloppyhistory (hDlg);
				break;
			case IDC_DF0TYPE:
				getfloppytype (hDlg, 0);
				break;
			case IDC_DF1TYPE:
				getfloppytype (hDlg, 1);
				break;
			case IDC_DF2TYPE:
				getfloppytype (hDlg, 2);
				break;
			case IDC_DF3TYPE:
				getfloppytype (hDlg, 3);
				break;
			case IDC_FLOPPYTYPE:
				int val = SendDlgItemMessage (hDlg, IDC_FLOPPYTYPE, CB_GETCURSEL, 0, 0L);
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
			getfloppytypeq (hDlg, 0);
			break;
		case IDC_DF1ENABLE:
		case IDC_DF1QENABLE:
			getfloppytypeq (hDlg, 1);
			break;
		case IDC_DF2ENABLE:
			getfloppytypeq (hDlg, 2);
			break;
		case IDC_DF3ENABLE:
			getfloppytypeq (hDlg, 3);
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
		case IDC_EJECT0:
		case IDC_EJECT0Q:
			SetDlgItemText (hDlg, IDC_DF0TEXT, L"");
			SetDlgItemText (hDlg, IDC_DF0TEXTQ, L"");
			ejectfloppy (0);
			addfloppytype (hDlg, 0);
			break;
		case IDC_EJECT1:
		case IDC_EJECT1Q:
			SetDlgItemText (hDlg, IDC_DF1TEXT, L"");
			SetDlgItemText (hDlg, IDC_DF1TEXTQ, L"");
			ejectfloppy (1);
			addfloppytype (hDlg, 1);
			break;
		case IDC_EJECT2:
			SetDlgItemText (hDlg, IDC_DF2TEXT, L"");
			ejectfloppy (2);
			addfloppytype (hDlg, 2);
			break;
		case IDC_EJECT3:
			SetDlgItemText (hDlg, IDC_DF3TEXT, L"");
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
			DiskSelection (hDlg, wParam, 1, &workprefs, 0);
			addfloppyhistory (hDlg);
			break;
		case IDC_CREATE_RAW:
			DiskSelection (hDlg, wParam, 1, &workprefs, 0);
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
	{ FVIRTKEY, VK_UP, 10101 }, { FVIRTKEY, VK_DOWN, 10102 }, { FVIRTKEY, VK_RIGHT, 10104 },
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

static void diskswapper_addfile2 (struct uae_prefs *prefs, const TCHAR *file)
{
	int list = 0;
	while (list < MAX_SPARE_DRIVES) {
		if (!strcasecmp (prefs->dfxlist[list], file))
			break;
		list++;
	}
	if (list == MAX_SPARE_DRIVES) {
		list = 0;
		while (list < MAX_SPARE_DRIVES) {
			if (!prefs->dfxlist[list][0]) {
				_tcscpy (prefs->dfxlist[list], file);
				fullpath (prefs->dfxlist[list], MAX_DPATH);
				break;
			}
			list++;
		}
	}
}

static void diskswapper_addfile (struct uae_prefs *prefs, const TCHAR *file)
{
	struct zdirectory *zd = zfile_opendir_archive (file, ZFD_ARCHIVE | ZFD_NORECURSE);
	if (zd && zfile_readdir_archive (zd, NULL, true) > 1) {
		TCHAR out[MAX_DPATH];
		while (zfile_readdir_archive (zd, out, true)) {
			struct zfile *zf = zfile_fopen (out, L"rb", ZFD_NORMAL);
			if (zf) {
				int type = zfile_gettype (zf);
				if (type == ZFILE_DISKIMAGE)
					diskswapper_addfile2 (prefs, out);
				zfile_fclose (zf);
			}
		}
		zfile_closedir_archive (zd);
	} else {
		diskswapper_addfile2 (prefs, file);
	}
}

static void addswapperfile (HWND hDlg, int entry, TCHAR *newpath)
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
			diskswapper_addfile (&workprefs, path);
			lastentry = entry;
			entry++;
		}
		InitializeListView (hDlg);
		swapperhili (hDlg, lastentry);
	}
}

static INT_PTR CALLBACK SwapperDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;
	static int entry;
	TCHAR tmp[MAX_DPATH];

	switch (msg)
	{
	case WM_INITDIALOG:
		pages[DISK_ID] = hDlg;
		currentpage = DISK_ID;
		InitializeListView (hDlg);
		addfloppyhistory (hDlg);
		entry = 0;
		swapperhili (hDlg, entry);
		setautocomplete (hDlg, IDC_DISKTEXT);
		break;
	case WM_LBUTTONUP:
		{
			int *draggeditems;
			int item = drag_end (hDlg, cachedlist, lParam, &draggeditems);
			if (item >= 0) {
				int i, item2;
				entry = item;
				for (i = 0; (item2 = draggeditems[i]) >= 0 && item2 < MAX_SPARE_DRIVES; i++, item++) {
					if (item != item2) {
						TCHAR tmp[1000];
						_tcscpy (tmp, workprefs.dfxlist[item]);
						_tcscpy (workprefs.dfxlist[item], workprefs.dfxlist[item2]);
						_tcscpy (workprefs.dfxlist[item2], tmp);
					}
				}
				InitializeListView(hDlg);
				swapperhili (hDlg, entry);
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
		if (GetDlgCtrlID ((HWND)wParam) == IDC_DISKLISTINSERT && entry >= 0) {
			TCHAR *s = favoritepopup (hDlg);
			if (s) {
				addswapperfile (hDlg, entry, s);
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
				entry = LOWORD (wParam) - 10001;
				swapperhili (hDlg, entry);
				break;
			case 10101:
				if (entry > 0) {
					entry--;
					swapperhili (hDlg, entry);
				}
				break;
			case 10102:
				if (entry >= 0 && entry < MAX_SPARE_DRIVES - 1) {
					entry++;
					swapperhili (hDlg, entry);
				}
				break;
			case 10103:
			case 10104:
				disk_swap (entry, 1);
				InitializeListView (hDlg);
				swapperhili (hDlg, entry);
				break;
			case 10201:
			case 10202:
			case 10203:
			case 10204:
				{
					int drv = LOWORD (wParam) - 10201;
					int i;
					if (workprefs.floppyslots[drv].dfxtype >= 0 && entry >= 0) {
						for (i = 0; i < 4; i++) {
							if (!_tcscmp (workprefs.floppyslots[i].df, workprefs.dfxlist[entry]))
								workprefs.floppyslots[i].df[0] = 0;
						}
						_tcscpy (workprefs.floppyslots[drv].df, workprefs.dfxlist[entry]);
						disk_insert (drv, workprefs.floppyslots[drv].df);
						InitializeListView (hDlg);
						swapperhili (hDlg, entry);
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
					swapperhili (hDlg, entry);
				}
				break;
			case 10209:
				{
					addswapperfile (hDlg, entry, NULL);
				}
				break;

			case IDC_DISKLISTINSERT:
				if (entry >= 0) {
					if (getfloppybox (hDlg, IDC_DISKTEXT, tmp, sizeof (tmp) / sizeof (TCHAR), HISTORY_FLOPPY)) {
						_tcscpy (workprefs.dfxlist[entry], tmp);
						addfloppyhistory (hDlg);
						InitializeListView (hDlg);
						swapperhili (hDlg, entry);
					} else {
						addswapperfile (hDlg, entry, NULL);
					}
				}
				break;

			case IDC_DISKLISTREMOVE:
				if (entry >= 0) {
					workprefs.dfxlist[entry][0] = 0;
					InitializeListView (hDlg);
					swapperhili (hDlg, entry);
				}
				break;
			case IDC_UP:
				if (entry > 0) {
					_tcscpy (tmp, workprefs.dfxlist[entry - 1]);
					_tcscpy (workprefs.dfxlist[entry - 1], workprefs.dfxlist[entry]);
					_tcscpy (workprefs.dfxlist[entry], tmp);
					InitializeListView (hDlg);
					entry--;
					swapperhili (hDlg, entry);
				}
				break;
			case IDC_DOWN:
				if (entry >= 0 && entry < MAX_SPARE_DRIVES - 1) {
					_tcscpy (tmp, workprefs.dfxlist[entry + 1]);
					_tcscpy (workprefs.dfxlist[entry + 1], workprefs.dfxlist[entry]);
					_tcscpy (workprefs.dfxlist[entry], tmp);
					InitializeListView (hDlg);
					entry++;
					swapperhili (hDlg, entry);
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
				entry = listview_entry_from_click (list, &col);
				if (entry >= 0) {
					if (col == 2) {
						if (button) {
							if (!dblclick) {
								if (disk_swap (entry, -1))
									InitializeListView (hDlg);
								swapperhili (hDlg, entry);
							}
						} else {
							if (!dblclick) {
								if (disk_swap (entry, 0))
									InitializeListView (hDlg);
								swapperhili (hDlg, entry);
							}
						}
					} else if (col == 1) {
						if (dblclick) {
							if (!button) {
								addswapperfile (hDlg, entry, NULL);
							} else {
								workprefs.dfxlist[entry][0] = 0;
								InitializeListView (hDlg);
							}
						}
					}
					SetDlgItemText (hDlg, IDC_DISKTEXT,  workprefs.dfxlist[entry]);
				}
				break;
			}
		}
	}
	return FALSE;
}

static PRINTER_INFO_1 *pInfo = NULL;
static DWORD dwEnumeratedPrinters = 0;
#define MAX_PRINTERS 10
struct serparportinfo comports[MAX_SERPAR_PORTS];
struct midiportinfo midiinportinfo[MAX_MIDI_PORTS];
struct midiportinfo midioutportinfo[MAX_MIDI_PORTS];
static int ghostscript_available;

static int joyxprevious[4];
static BOOL bNoMidiIn = FALSE;

static void enable_for_gameportsdlg (HWND hDlg)
{
	int v = full_property_sheet;
	ew (hDlg, IDC_PORT_TABLET_FULL, v && is_tablet () && workprefs.input_tablet > 0);
	ew (hDlg, IDC_PORT_TABLET_CURSOR, v && workprefs.input_tablet > 0);
	ew (hDlg, IDC_PORT_TABLET, v);
}

static void enable_for_portsdlg (HWND hDlg)
{
	int v;
	int isprinter, issampler;

	ew (hDlg, IDC_SWAP, TRUE);
#if !defined (SERIAL_PORT)
	ew (hDlg, IDC_MIDIOUTLIST, FALSE);
	ew (hDlg, IDC_MIDIINLIST, FALSE);
	ew (hDlg, IDC_SHARED, FALSE);
	ew (hDlg, IDC_SER_CTSRTS, FALSE);
	ew (hDlg, IDC_SERIAL_DIRECT, FALSE);
	ew (hDlg, IDC_SERIAL, FALSE);
	ew (hDlg, IDC_UAESERIAL, FALSE);
#else
	v = workprefs.use_serial ? TRUE : FALSE;
	ew (hDlg, IDC_SER_SHARED, v);
	ew (hDlg, IDC_SER_CTSRTS, v);
	ew (hDlg, IDC_SER_DIRECT, v);
	ew (hDlg, IDC_UAESERIAL, full_property_sheet);
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
	ew (hDlg, IDC_PRINTERAUTOFLUSH, isprinter);
	ew (hDlg, IDC_PRINTERTYPELIST, isprinter);
	ew (hDlg, IDC_FLUSHPRINTER, isprinteropen () && isprinter ? TRUE : FALSE);
	ew (hDlg, IDC_PSPRINTER, full_property_sheet && ghostscript_available && isprinter ? TRUE : FALSE);
	ew (hDlg, IDC_PSPRINTERDETECT, full_property_sheet && isprinter ? TRUE : FALSE);
	ew (hDlg, IDC_PS_PARAMS, full_property_sheet && ghostscript_available && isprinter);
}

static int joys[] = { IDC_PORT0_JOYS, IDC_PORT1_JOYS, IDC_PORT2_JOYS, IDC_PORT3_JOYS };
static int joysm[] = { IDC_PORT0_JOYSMODE, IDC_PORT1_JOYSMODE, -1, -1 };
static int joysaf[] = { IDC_PORT0_AF, IDC_PORT1_AF, -1, -1 };

static void updatejoyport (HWND hDlg)
{
	int i, j;
	TCHAR tmp[MAX_DPATH], tmp2[MAX_DPATH];

	SetDlgItemInt (hDlg, IDC_INPUTSPEEDM, workprefs.input_mouse_speed, FALSE);
	CheckDlgButton (hDlg, IDC_PORT_MOUSETRICK, workprefs.input_magic_mouse);
	SendDlgItemMessage (hDlg, IDC_PORT_TABLET_CURSOR, CB_SETCURSEL, workprefs.input_magic_mouse_cursor, 0);
	CheckDlgButton (hDlg, IDC_PORT_TABLET, workprefs.input_tablet > 0);
	CheckDlgButton (hDlg, IDC_PORT_TABLET_FULL, workprefs.input_tablet == TABLET_REAL);

	if (joyxprevious[0] < 0)
		joyxprevious[0] = inputdevice_get_device_total (IDTYPE_JOYSTICK) + 1;
	if (joyxprevious[1] < 0)
		joyxprevious[1] = JSEM_LASTKBD + 1;

	for (i = 0; i < MAX_JPORTS; i++) {
		int total = 2;
		int idx = joyxprevious[i];
		int id = joys[i];
		int idm = joysm[i];
		int v = workprefs.jports[i].id;
		int vm = workprefs.jports[i].mode;
		TCHAR *p1, *p2;

		if (idm > 0)
			SendDlgItemMessage (hDlg, idm, CB_SETCURSEL, vm, 0);

		SendDlgItemMessage (hDlg, id, CB_RESETCONTENT, 0, 0L);
		SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)L"");
		WIN32GUI_LoadUIString (IDS_NONE, tmp, sizeof (tmp) / sizeof (TCHAR) - 3);
		_stprintf (tmp2, L"<%s>", tmp);
		SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)tmp2);
		WIN32GUI_LoadUIString (IDS_KEYJOY, tmp, sizeof (tmp) / sizeof (TCHAR));
		_tcscat (tmp, L"\n");
		p1 = tmp;
		for (;;) {
			p2 = _tcschr (p1, '\n');
			if (p2 && _tcslen (p2) > 0) {
				*p2++ = 0;
				SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)p1);
				total++;
				p1 = p2;
			} else
				break;
		}
		for (j = 0; j < inputdevice_get_device_total (IDTYPE_JOYSTICK); j++, total++)
			SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)inputdevice_get_device_name(IDTYPE_JOYSTICK, j));
		if (i < 2) {
			for (j = 0; j < inputdevice_get_device_total (IDTYPE_MOUSE); j++, total++)
				SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)inputdevice_get_device_name(IDTYPE_MOUSE, j));
		}
		if (v == JPORT_CUSTOM) {
			SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)L"<Custom mapping>");
			total++;
		}

		idx = inputdevice_getjoyportdevice (i, v);
		if (idx >= 0)
			idx += 2;
		else
			idx = 1;
		if (idx >= total)
			idx = 0;
		SendDlgItemMessage (hDlg, id, CB_SETCURSEL, idx, 0);
		if (joysaf[i] >= 0)
			SendDlgItemMessage (hDlg, joysaf[i], CB_SETCURSEL, workprefs.jports[i].autofire, 0);
	}
}

static void fixjport (struct jport *port, int v)
{
	int vv = port->id;
	if (vv == JPORT_CUSTOM || vv == JPORT_NONE)
		return;
	if (vv != v)
		return;
	if (vv >= JSEM_JOYS && vv < JSEM_MICE) {
		vv -= JSEM_JOYS;
		vv++;
		if (vv >= inputdevice_get_device_total (IDTYPE_JOYSTICK))
			vv = 0;
		vv += JSEM_JOYS;
	}
	if (vv >= JSEM_MICE && vv < JSEM_END) {
		vv -= JSEM_MICE;
		vv++;
		if (vv >= inputdevice_get_device_total (IDTYPE_MOUSE))
			vv = 0;
		vv += JSEM_MICE;
	}
	if (vv >= JSEM_KBDLAYOUT && vv < JSEM_LASTKBD) {
		vv -= JSEM_KBDLAYOUT;
		vv++;
		if (vv >= JSEM_LASTKBD)
			vv = 0;
		vv += JSEM_KBDLAYOUT;
	}
	port->id = vv;
}

static void values_from_gameportsdlg (HWND hDlg, int d)
{
	int i, j, success;
	int changed = 0;

	if (d) {
		i  = GetDlgItemInt (hDlg, IDC_INPUTSPEEDM, &success, FALSE);
		if (success)
			currprefs.input_mouse_speed = workprefs.input_mouse_speed = i;

		currprefs.input_magic_mouse = workprefs.input_magic_mouse = ischecked (hDlg, IDC_PORT_MOUSETRICK);
		workprefs.input_magic_mouse_cursor = SendDlgItemMessage (hDlg, IDC_PORT_TABLET_CURSOR, CB_GETCURSEL, 0, 0L);
		workprefs.input_tablet = 0;
		if (ischecked (hDlg, IDC_PORT_TABLET)) {
			workprefs.input_tablet = TABLET_MOUSEHACK;
			if (ischecked (hDlg, IDC_PORT_TABLET_FULL))
				workprefs.input_tablet = TABLET_REAL;
		}
		return;
	}

	for (i = 0; i < MAX_JPORTS; i++) {
		int idx = 0;
		int *port = &workprefs.jports[i].id;
		int *portm = &workprefs.jports[i].mode;
		int prevport = *port;
		int id = joys[i];
		int idm = joysm[i];
		LRESULT v = SendDlgItemMessage (hDlg, id, CB_GETCURSEL, 0, 0L);
		if (v != CB_ERR && v > 0) {
			int max = JSEM_LASTKBD + inputdevice_get_device_total (IDTYPE_JOYSTICK);
			if (i < 2)
				max += inputdevice_get_device_total (IDTYPE_MOUSE);
			v -= 2;
			if (v < 0)
				*port = JPORT_NONE;
			else if (v >= max && prevport == JPORT_CUSTOM)
				*port = JPORT_CUSTOM;
			else if (v >= max)
				*port = JPORT_NONE;
			else if (v < JSEM_LASTKBD)
				*port = JSEM_KBDLAYOUT + (int)v;
			else if (v >= JSEM_LASTKBD + inputdevice_get_device_total (IDTYPE_JOYSTICK))
				*port = JSEM_MICE + (int)v - inputdevice_get_device_total (IDTYPE_JOYSTICK) - JSEM_LASTKBD;
			else
				*port = JSEM_JOYS + (int)v - JSEM_LASTKBD;
		}
		if (idm >= 0) {
			v = SendDlgItemMessage (hDlg, idm, CB_GETCURSEL, 0, 0L);
			if (v != CB_ERR) {
				*portm = v;
			}
				
		}
		if (joysaf[i] >= 0) {
			int af = SendDlgItemMessage (hDlg, joysaf[i], CB_GETCURSEL, 0, 0L);
			workprefs.jports[i].autofire = af;
		}
		if (*port != prevport)
			changed = 1;
	}
	if (changed) {
		for (i = 0; i < MAX_JPORTS; i++) {
			for (j = 0; j < MAX_JPORTS; j++) {
				if (j != i)
					fixjport (&workprefs.jports[i], workprefs.jports[j].id);
			}
		}
	}

}

static void values_from_portsdlg (HWND hDlg)
{
	int v;
	TCHAR tmp[256];
	BOOL success;
	LRESULT item;

	item = SendDlgItemMessage (hDlg, IDC_SAMPLERLIST, CB_GETCURSEL, 0, 0L);
	if(item != CB_ERR) {
		workprefs.win32_samplersoundcard = item - 1;
		if (item > 0)
			workprefs.prtname[0] = 0;
	}

	item = SendDlgItemMessage (hDlg, IDC_PRINTERLIST, CB_GETCURSEL, 0, 0L);
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
						_stprintf (workprefs.prtname, L"LPT%d", i + 1);
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

	workprefs.win32_midioutdev = SendDlgItemMessage (hDlg, IDC_MIDIOUTLIST, CB_GETCURSEL, 0, 0) - 2;
	if (bNoMidiIn || workprefs.win32_midioutdev < -1) {
		workprefs.win32_midiindev = -1;
	} else {
		workprefs.win32_midiindev = SendDlgItemMessage (hDlg, IDC_MIDIINLIST, CB_GETCURSEL, 0, 0) - 1;
	}
	ew (hDlg, IDC_MIDIINLIST, workprefs.win32_midioutdev < -1 ? FALSE : TRUE);

	item = SendDlgItemMessage (hDlg, IDC_SERIAL, CB_GETCURSEL, 0, 0L);
	if (item != CB_ERR && item > 0) {
		workprefs.use_serial = 1;
		_tcscpy (workprefs.sername, comports[item - 1].dev);
	} else {
		workprefs.use_serial = 0;
		workprefs.sername[0] = 0;
	}
	workprefs.serial_demand = 0;
	if (ischecked (hDlg, IDC_SER_SHARED))
		workprefs.serial_demand = 1;
	workprefs.serial_hwctsrts = 0;
	if (ischecked (hDlg, IDC_SER_CTSRTS))
		workprefs.serial_hwctsrts = 1;
	workprefs.serial_direct = 0;
	if (ischecked (hDlg, IDC_SER_DIRECT))
		workprefs.serial_direct = 1;

	workprefs.uaeserial = 0;
	if (ischecked (hDlg, IDC_UAESERIAL))
		workprefs.uaeserial = 1;

	GetDlgItemText (hDlg, IDC_PS_PARAMS, workprefs.ghostscript_parameters, sizeof workprefs.ghostscript_parameters / sizeof (TCHAR));
	v  = GetDlgItemInt (hDlg, IDC_PRINTERAUTOFLUSH, &success, FALSE);
	if (success)
		workprefs.parallel_autoflush_time = v;

	item = SendDlgItemMessage (hDlg, IDC_DONGLELIST, CB_GETCURSEL, 0, 0L);
	if (item != CB_ERR)
		workprefs.dongle = item;

}

static void values_to_portsdlg (HWND hDlg)
{
	LRESULT result;
	int idx;

	SendDlgItemMessage (hDlg, IDC_SAMPLERLIST, CB_SETCURSEL, workprefs.win32_samplersoundcard + 1, 0);

	result = 0;
	if(workprefs.prtname[0]) {
		int i, got = 1;
		TCHAR tmp[10];
		result = SendDlgItemMessage (hDlg, IDC_PRINTERLIST, CB_FINDSTRINGEXACT, -1, (LPARAM)workprefs.prtname);
		for (i = 0; i < 4; i++) {
			_stprintf (tmp, L"LPT%d", i + 1);
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
	SendDlgItemMessage (hDlg, IDC_PRINTERTYPELIST, CB_SETCURSEL, idx, 0);

	SetDlgItemText (hDlg, IDC_PS_PARAMS, workprefs.ghostscript_parameters);

	SendDlgItemMessage (hDlg, IDC_PRINTERLIST, CB_SETCURSEL, result, 0);
	SendDlgItemMessage (hDlg, IDC_MIDIOUTLIST, CB_SETCURSEL, workprefs.win32_midioutdev + 2, 0);
	if (workprefs.win32_midiindev >= 0)
		SendDlgItemMessage (hDlg, IDC_MIDIINLIST, CB_SETCURSEL, workprefs.win32_midiindev + 1, 0);
	else
		SendDlgItemMessage (hDlg, IDC_MIDIINLIST, CB_SETCURSEL, 0, 0);
	ew (hDlg, IDC_MIDIINLIST, workprefs.win32_midioutdev < -1 ? FALSE : TRUE);

	CheckDlgButton (hDlg, IDC_UAESERIAL, workprefs.uaeserial);
	CheckDlgButton (hDlg, IDC_SER_SHARED, workprefs.serial_demand);
	CheckDlgButton (hDlg, IDC_SER_CTSRTS, workprefs.serial_hwctsrts);
	CheckDlgButton (hDlg, IDC_SER_DIRECT, workprefs.serial_direct);

	if(!workprefs.sername[0])  {
		SendDlgItemMessage (hDlg, IDC_SERIAL, CB_SETCURSEL, 0, 0L);
		workprefs.use_serial = 0;
	} else {
		int i;
		LRESULT result = -1;
		for (i = 0; i < MAX_SERPAR_PORTS && comports[i].name; i++) {
			if (!_tcscmp (comports[i].dev, workprefs.sername)) {
				result = SendDlgItemMessage (hDlg, IDC_SERIAL, CB_SETCURSEL, i + 1, 0L);
				break;
			}
		}
		if(result < 0 && workprefs.sername[0]) {
			// Warn the user that their COM-port selection is not valid on this machine
			TCHAR szMessage[MAX_DPATH];
			WIN32GUI_LoadUIString (IDS_INVALIDCOMPORT, szMessage, MAX_DPATH);
			pre_gui_message (szMessage);
			// Select "none" as the COM-port
			SendDlgItemMessage (hDlg, IDC_SERIAL, CB_SETCURSEL, 0L, 0L);
			// Disable the chosen serial-port selection
			workprefs.sername[0] = 0;
			workprefs.use_serial = 0;
		} else {
			workprefs.use_serial = 1;
		}
	}
	SendDlgItemMessage (hDlg, IDC_DONGLELIST, CB_SETCURSEL, workprefs.dongle, 0L);

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

	SendDlgItemMessage (hDlg, IDC_DONGLELIST, CB_RESETCONTENT, 0, 0L);
	SendDlgItemMessage (hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)szNone.c_str());
	SendDlgItemMessage (hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)L"Robocop 3");
	SendDlgItemMessage (hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)L"Leaderboard");
	SendDlgItemMessage (hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)L"B.A.T. II");
	SendDlgItemMessage (hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)L"Italy'90 Soccer");
	SendDlgItemMessage (hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)L"Dames Grand Maitre");
	SendDlgItemMessage (hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)L"Rugby Coach");
	SendDlgItemMessage (hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)L"Cricket Captain");
	SendDlgItemMessage (hDlg, IDC_DONGLELIST, CB_ADDSTRING, 0, (LPARAM)L"Leviathan");

	SendDlgItemMessage (hDlg, IDC_SERIAL, CB_RESETCONTENT, 0, 0L);
	SendDlgItemMessage (hDlg, IDC_SERIAL, CB_ADDSTRING, 0, (LPARAM)szNone.c_str());
	for (port = 0; port < MAX_SERPAR_PORTS && comports[port].name; port++) {
		SendDlgItemMessage (hDlg, IDC_SERIAL, CB_ADDSTRING, 0, (LPARAM)comports[port].name);
	}

	SendDlgItemMessage (hDlg, IDC_PRINTERTYPELIST, CB_RESETCONTENT, 0, 0L);
	WIN32GUI_LoadUIString (IDS_PRINTER_PASSTHROUGH, tmp, MAX_DPATH);
	SendDlgItemMessage (hDlg, IDC_PRINTERTYPELIST, CB_ADDSTRING, 0, (LPARAM)tmp);
	WIN32GUI_LoadUIString (IDS_PRINTER_ASCII, tmp, MAX_DPATH);
	SendDlgItemMessage (hDlg, IDC_PRINTERTYPELIST, CB_ADDSTRING, 0, (LPARAM)tmp);
	WIN32GUI_LoadUIString (IDS_PRINTER_EPSON9, tmp, MAX_DPATH);
	SendDlgItemMessage (hDlg, IDC_PRINTERTYPELIST, CB_ADDSTRING, 0, (LPARAM)tmp);
	WIN32GUI_LoadUIString (IDS_PRINTER_EPSON48, tmp, MAX_DPATH);
	SendDlgItemMessage (hDlg, IDC_PRINTERTYPELIST, CB_ADDSTRING, 0, (LPARAM)tmp);
	WIN32GUI_LoadUIString (IDS_PRINTER_POSTSCRIPT_DETECTION, tmp, MAX_DPATH);
	SendDlgItemMessage (hDlg, IDC_PRINTERTYPELIST, CB_ADDSTRING, 0, (LPARAM)tmp);
	WIN32GUI_LoadUIString (IDS_PRINTER_POSTSCRIPT_EMULATION, tmp, MAX_DPATH);
	SendDlgItemMessage (hDlg, IDC_PRINTERTYPELIST, CB_ADDSTRING, 0, (LPARAM)tmp);

	SendDlgItemMessage (hDlg, IDC_SAMPLERLIST, CB_RESETCONTENT, 0, 0L);
	SendDlgItemMessage (hDlg, IDC_SAMPLERLIST, CB_ADDSTRING, 0, (LPARAM)szNone.c_str());
	enumerate_sound_devices ();
	for (int card = 0; record_devices[card].type; card++) {
		int type = record_devices[card].type;
		TCHAR tmp[MAX_DPATH];
		_stprintf (tmp, L"%s: %s",
			type == SOUND_DEVICE_DS ? L"DSOUND" : (type == SOUND_DEVICE_AL ? L"OpenAL" : (type == SOUND_DEVICE_PA ? L"PortAudio" : L"WASAPI")),
			record_devices[card].name);
		if (type == SOUND_DEVICE_DS)
			SendDlgItemMessage (hDlg, IDC_SAMPLERLIST, CB_ADDSTRING, 0, (LPARAM)tmp);
	}

	SendDlgItemMessage (hDlg, IDC_PRINTERLIST, CB_RESETCONTENT, 0, 0L);
	SendDlgItemMessage (hDlg, IDC_PRINTERLIST, CB_ADDSTRING, 0, (LPARAM)szNone.c_str());
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
			SendDlgItemMessage (hDlg, IDC_PRINTERLIST, CB_ADDSTRING, 0, (LPARAM)pInfo[port].pName);
	} else {
		ew (hDlg, IDC_PRINTERLIST, FALSE);
	}
	if (paraport_mask) {
		int mask = paraport_mask;
		int i = 1;
		while (mask) {
			if (mask & 1) {
				TCHAR tmp[30];
				_stprintf (tmp, L"LPT%d", i);
				SendDlgItemMessage (hDlg, IDC_PRINTERLIST, CB_ADDSTRING, 0, (LPARAM)tmp);
			}
			i++;
			mask >>= 1;
		}
	}

	SendDlgItemMessage (hDlg, IDC_MIDIOUTLIST, CB_RESETCONTENT, 0, 0L);
	SendDlgItemMessage (hDlg, IDC_MIDIOUTLIST, CB_ADDSTRING, 0, (LPARAM)szNone.c_str());
	for (port = 0; midioutportinfo[port].name; port++) {
		SendDlgItemMessage (hDlg, IDC_MIDIOUTLIST, CB_ADDSTRING, 0, (LPARAM)midioutportinfo[port].name);
	}
	ew (hDlg, IDC_MIDIOUTLIST, port > 0);

	SendDlgItemMessage (hDlg, IDC_MIDIINLIST, CB_RESETCONTENT, 0, 0L);
	SendDlgItemMessage (hDlg, IDC_MIDIINLIST, CB_ADDSTRING, 0, (LPARAM)szNone.c_str());
	for (port = 0; midiinportinfo[port].name; port++) {
		SendDlgItemMessage (hDlg, IDC_MIDIINLIST, CB_ADDSTRING, 0, (LPARAM)midiinportinfo[port].name);
	}
	bNoMidiIn = port == 0;
	ew (hDlg, IDC_MIDIINLIST, port > 0);

}

static void input_test (HWND hDlg, int);
static void ports_remap (HWND, int);

static void processport (HWND hDlg, bool reset, int port)
{
	if (reset)
		inputdevice_compa_clear (&workprefs, port);
	values_from_gameportsdlg (hDlg, 0);
	enable_for_gameportsdlg (hDlg);
	updatejoyport (hDlg);
	inputdevice_updateconfig (&workprefs);
	inputdevice_config_change ();
}

/* Handle messages for the Joystick Settings page of our property-sheet */
static INT_PTR CALLBACK GamePortsDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	TCHAR tmp[MAX_DPATH];
	static int recursive = 0;
	static int first;
	int temp, i;

	switch (msg)
	{
	case WM_INITDIALOG:
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

		for (i = 0; joysaf[i] >= 0; i++) {
			int id = joysaf[i];
			SendDlgItemMessage (hDlg, id, CB_RESETCONTENT, 0, 0L);
			SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)L"No autofire");
			SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)L"Autofire");
			SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)L"Autofire (toggle)");
		}

		SendDlgItemMessage (hDlg, IDC_PORT_TABLET_CURSOR, CB_RESETCONTENT, 0, 0L);
		WIN32GUI_LoadUIString (IDS_TABLET_BOTH_CURSORS, tmp, MAX_DPATH);
		SendDlgItemMessage (hDlg, IDC_PORT_TABLET_CURSOR, CB_ADDSTRING, 0, (LPARAM)tmp);
		WIN32GUI_LoadUIString (IDS_TABLET_NATIVE_CURSOR, tmp, MAX_DPATH);
		SendDlgItemMessage (hDlg, IDC_PORT_TABLET_CURSOR, CB_ADDSTRING, 0, (LPARAM)tmp);
		WIN32GUI_LoadUIString (IDS_TABLET_HOST_CURSOR, tmp, MAX_DPATH);
		SendDlgItemMessage (hDlg, IDC_PORT_TABLET_CURSOR, CB_ADDSTRING, 0, (LPARAM)tmp);

		for (i = 0; i < 2; i++) {
			int id = i == 0 ? IDC_PORT0_JOYSMODE : IDC_PORT1_JOYSMODE;
			SendDlgItemMessage (hDlg, id, CB_RESETCONTENT, 0, 0L);
			WIN32GUI_LoadUIString (IDS_JOYMODE_DEFAULT, tmp, MAX_DPATH);
			SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)tmp);
			WIN32GUI_LoadUIString (IDS_JOYMODE_MOUSE, tmp, MAX_DPATH);
			SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)tmp);
			WIN32GUI_LoadUIString (IDS_JOYMODE_JOYSTICK, tmp, MAX_DPATH);
			SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)tmp);
			WIN32GUI_LoadUIString (IDS_JOYMODE_GAMEPAD, tmp, MAX_DPATH);
			SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)tmp);
			WIN32GUI_LoadUIString (IDS_JOYMODE_JOYSTICKANALOG, tmp, MAX_DPATH);
			SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)tmp);
			WIN32GUI_LoadUIString (IDS_JOYMODE_MOUSE_CDTV, tmp, MAX_DPATH);
			SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)tmp);
			WIN32GUI_LoadUIString (IDS_JOYMODE_JOYSTICK_CD32, tmp, MAX_DPATH);
			SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)tmp);
			WIN32GUI_LoadUIString (IDS_JOYMODE_LIGHTPEN, tmp, MAX_DPATH);
			SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)tmp);
		}

		inputdevice_updateconfig (&workprefs);
		enable_for_gameportsdlg (hDlg);
		updatejoyport (hDlg);
		recursive--;
		break;
	case WM_USER:
		recursive++;
		enable_for_gameportsdlg (hDlg);
		updatejoyport (hDlg);
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
			updatejoyport (hDlg);
		} else if (LOWORD (wParam) == IDC_PORT0_REMAP) {
			ports_remap (hDlg, 0);
		} else if (LOWORD (wParam) == IDC_PORT1_REMAP) {
			ports_remap (hDlg, 1);
		} else if (LOWORD (wParam) == IDC_PORT2_REMAP) {
			ports_remap (hDlg, 2);
		} else if (LOWORD (wParam) == IDC_PORT3_REMAP) {
			ports_remap (hDlg, 3);
		} else if (LOWORD (wParam) == IDC_PORT0_TEST) {
			input_test (hDlg, 0);
		} else if (LOWORD (wParam) == IDC_PORT1_TEST) {
			input_test (hDlg, 1);
		} else if (LOWORD (wParam) == IDC_PORT2_TEST) {
			input_test (hDlg, 2);
		} else if (LOWORD (wParam) == IDC_PORT3_TEST) {
			input_test (hDlg, 3);
		} else if (HIWORD (wParam) == CBN_SELCHANGE) {
			switch (LOWORD (wParam))
			{
				case IDC_PORT0_AF:
					processport (hDlg, false, 0);
				break;
				case IDC_PORT1_AF:
					processport (hDlg, false, 1);
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
					if (port >= 0)
						processport (hDlg, true, port);
					break;
				}
			}
		} else {
			values_from_gameportsdlg (hDlg, 1);
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

	switch (msg)
	{
	case WM_INITDIALOG:
		recursive++;
		pages[IOPORTS_ID] = hDlg;
		currentpage = IOPORTS_ID;
		init_portsdlg (hDlg);
		inputdevice_updateconfig (&workprefs);
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
		} else if (wParam == IDC_UAESERIAL || wParam == IDC_SER_SHARED || wParam == IDC_SER_DIRECT || wParam == IDC_SER_CTSRTS || wParam == IDC_PRINTERAUTOFLUSH) {
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
					inputdevice_updateconfig (&workprefs);
					inputdevice_config_change ();
					enable_for_portsdlg (hDlg);
					break;
				case IDC_PRINTERTYPELIST:
					{
						int item = SendDlgItemMessage (hDlg, IDC_PRINTERTYPELIST, CB_GETCURSEL, 0, 0L);
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
	SendDlgItemMessage (hDlg, IDC_INPUTTYPE, CB_SETCURSEL, workprefs.input_selected_setting, 0);
	SendDlgItemMessage (hDlg, IDC_INPUTDEVICE, CB_SETCURSEL, input_selected_device, 0);
	SetDlgItemInt (hDlg, IDC_INPUTDEADZONE, workprefs.input_joystick_deadzone, FALSE);
	SetDlgItemInt (hDlg, IDC_INPUTAUTOFIRERATE, workprefs.input_autofire_linecnt, FALSE);
	SetDlgItemInt (hDlg, IDC_INPUTSPEEDD, workprefs.input_joymouse_speed, FALSE);
	SetDlgItemInt (hDlg, IDC_INPUTSPEEDA, workprefs.input_joymouse_multiplier, FALSE);
	CheckDlgButton (hDlg, IDC_INPUTDEVICEDISABLE, (!input_total_devices || inputdevice_get_device_status (input_selected_device)) ? BST_CHECKED : BST_UNCHECKED);
}

static int stringboxdialogactive;
static INT_PTR CALLBACK StringBoxDialogProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch(msg)
	{
	case WM_DESTROY:
		PostQuitMessage (0);
		return TRUE;
	case WM_CLOSE:
		stringboxdialogactive = 0;
		DestroyWindow (hDlg);
		return TRUE;
	case WM_INITDIALOG:
		return TRUE;
	case WM_COMMAND:
		switch (LOWORD (wParam))
		{
		case IDOK:
			stringboxdialogactive = -1;
			DestroyWindow (hDlg);
			return TRUE;
		case IDCANCEL:
			stringboxdialogactive = 0;
			DestroyWindow (hDlg);
			return TRUE;
		}
		break;
	}
	return FALSE;
}

static int askinputcustom (HWND hDlg, TCHAR *custom, int maxlen, DWORD titleid)
{
	HWND hwnd;
	TCHAR txt[MAX_DPATH];

	stringboxdialogactive = 1;
	hwnd = CustomCreateDialog (IDD_STRINGBOX, hDlg, StringBoxDialogProc);
	if (hwnd == NULL)
		return 0;
	if (titleid != 0) {
		LoadString (hUIDLL, titleid, txt, MAX_DPATH);
		SetWindowText (hwnd, txt);
	}
	txt[0] = 0;
	SendMessage (GetDlgItem (hwnd, IDC_STRINGBOXEDIT), WM_SETTEXT, 0, (LPARAM)custom);
	while (stringboxdialogactive == 1) {
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
		if (stringboxdialogactive == -1) {
			_tcscpy (custom, txt);
			return 1;
		}
	}
	return 0;
}

static void init_inputdlg_2 (HWND hDlg)
{
	TCHAR name1[256], name2[256];
	TCHAR custom1[MAX_DPATH], tmp1[MAX_DPATH];
	int cnt, index, af, aftmp, port;

	SendDlgItemMessage (hDlg, IDC_INPUTAMIGA, CB_RESETCONTENT, 0, 0L);
	SendDlgItemMessage (hDlg, IDC_INPUTAMIGA, CB_ADDSTRING, 0, (LPARAM)szNone.c_str());
	WIN32GUI_LoadUIString (IDS_INPUT_CUSTOMEVENT, tmp1, MAX_DPATH);
	SendDlgItemMessage (hDlg, IDC_INPUTAMIGA, CB_ADDSTRING, 0, (LPARAM)tmp1);
	index = 0; af = 0; port = 0;
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
			SendDlgItemMessage (hDlg, IDC_INPUTAMIGA, CB_ADDSTRING, 0, (LPARAM)name2);
		}
		if (_tcslen (custom1) > 0)
			index = 1;
		if (index >= 0) {
			SendDlgItemMessage (hDlg, IDC_INPUTAMIGA, CB_SETCURSEL, index, 0);
			SendDlgItemMessage (hDlg, IDC_INPUTAMIGACNT, CB_SETCURSEL, input_selected_sub_num, 0);
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
	int i;
	TCHAR buf[100], txt[100];

	SendDlgItemMessage (hDlg, IDC_INPUTTYPE, CB_RESETCONTENT, 0, 0L);
	WIN32GUI_LoadUIString (IDS_INPUT_CUSTOM, buf, sizeof (buf) / sizeof (TCHAR));
	for (i = 0; i < GAMEPORT_INPUT_SETTINGS; i++) {
		_stprintf (txt, buf, i + 1);
		SendDlgItemMessage (hDlg, IDC_INPUTTYPE, CB_ADDSTRING, 0, (LPARAM)txt);
	}
	WIN32GUI_LoadUIString (IDS_INPUT_GAMEPORTS, buf, sizeof (buf) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_INPUTTYPE, CB_ADDSTRING, 0, (LPARAM)buf);

	SendDlgItemMessage (hDlg, IDC_INPUTCOPYFROM, CB_RESETCONTENT, 0, 0L);
	WIN32GUI_LoadUIString (IDS_INPUT_COPY_CUSTOM, buf, sizeof (buf) / sizeof (TCHAR));
	for (i = 0; i < MAX_INPUT_SETTINGS - 1; i++) {
		_stprintf (txt, buf, i + 1);
		SendDlgItemMessage (hDlg, IDC_INPUTCOPYFROM, CB_ADDSTRING, 0, (LPARAM)txt);
	}
	WIN32GUI_LoadUIString (IDS_INPUT_COPY_DEFAULT, buf, sizeof (buf) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_INPUTCOPYFROM, CB_ADDSTRING, 0, (LPARAM)buf);
	SendDlgItemMessage (hDlg, IDC_INPUTCOPYFROM, CB_SETCURSEL, 0, 0);


	SendDlgItemMessage (hDlg, IDC_INPUTAMIGACNT, CB_RESETCONTENT, 0, 0L);
	for (i = 0; i < MAX_INPUT_SUB_EVENT; i++) {
		_stprintf (buf, L"%d", i + 1);
		SendDlgItemMessage (hDlg, IDC_INPUTAMIGACNT, CB_ADDSTRING, 0, (LPARAM)buf);
	}
	SendDlgItemMessage (hDlg, IDC_INPUTAMIGACNT, CB_SETCURSEL, input_selected_sub_num, 0);

	SendDlgItemMessage (hDlg, IDC_INPUTDEVICE, CB_RESETCONTENT, 0, 0L);
	for (i = 0; i < inputdevice_get_device_total (IDTYPE_JOYSTICK); i++) {
		SendDlgItemMessage (hDlg, IDC_INPUTDEVICE, CB_ADDSTRING, 0, (LPARAM)inputdevice_get_device_name (IDTYPE_JOYSTICK, i));
	}
	for (i = 0; i < inputdevice_get_device_total (IDTYPE_MOUSE); i++) {
		SendDlgItemMessage (hDlg, IDC_INPUTDEVICE, CB_ADDSTRING, 0, (LPARAM)inputdevice_get_device_name (IDTYPE_MOUSE, i));
	}
	for (i = 0; i < inputdevice_get_device_total (IDTYPE_KEYBOARD); i++) {
		SendDlgItemMessage (hDlg, IDC_INPUTDEVICE, CB_ADDSTRING, 0, (LPARAM)inputdevice_get_device_name (IDTYPE_KEYBOARD, i));
	}
	input_total_devices = inputdevice_get_device_total (IDTYPE_JOYSTICK) +
		inputdevice_get_device_total (IDTYPE_MOUSE) +
		inputdevice_get_device_total (IDTYPE_KEYBOARD);
	if (input_selected_device >= input_total_devices)
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
	TCHAR custom1[MAX_DPATH];
	int flags;
	custom1[0] = 0;
	inputdevice_get_mapping (input_selected_device, input_selected_widget,
		&flags, NULL, NULL, custom1, input_selected_sub_num);
	if (_tcslen (custom1) > 0 || newcustom) {
		if (askinputcustom (hDlg, custom1, sizeof custom1 / sizeof (TCHAR), IDS_SB_CUSTOMEVENT)) {
			inputdevice_set_mapping (input_selected_device, input_selected_widget,
				NULL, custom1, flags, -1, input_selected_sub_num);
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

	item = SendDlgItemMessage (hDlg, IDC_INPUTAMIGACNT, CB_GETCURSEL, 0, 0L);
	if (item != CB_ERR && input_selected_sub_num != item) {
		input_selected_sub_num = (int)item;
		doselect = 0;
		init_inputdlg_2 (hDlg);
		update_listview_input (hDlg);
		return;
	}

	item = SendDlgItemMessage (hDlg, IDC_INPUTTYPE, CB_GETCURSEL, 0, 0L);
	if(item != CB_ERR) {
		if (item != workprefs.input_selected_setting) {
			workprefs.input_selected_setting = (int)item;
			input_selected_widget = -1;
			inputdevice_updateconfig (&workprefs);
			enable_for_inputdlg (hDlg);
			InitializeListView (hDlg);
			doselect = 1;
		}
	}
	item = SendDlgItemMessage (hDlg, IDC_INPUTDEVICE, CB_GETCURSEL, 0, 0L);
	if(item != CB_ERR) {
		if (item != input_selected_device) {
			input_selected_device = (int)item;
			input_selected_widget = -1;
			input_selected_event = -1;
			InitializeListView (hDlg);
			init_inputdlg_2 (hDlg);
			values_to_inputdlg (hDlg);
			doselect = 1;
		}
	}
	item = SendDlgItemMessage (hDlg, IDC_INPUTAMIGA, CB_GETCURSEL, 0, 0L);
	if(item != CB_ERR) {
		if (item == 1) {
			if (item != input_selected_event)
				doinputcustom (hDlg, 1);
		}
		input_selected_event = (int)item;
		doselect = 1;
	}

	if (inputchange && doselect && input_selected_device >= 0 && input_selected_event >= 0) {
		int flags;
		TCHAR custom[MAX_DPATH];
		custom[0] = 0;
		inputdevice_get_mapping (input_selected_device, input_selected_widget,
			&flags, NULL, 0, custom, input_selected_sub_num);
		if (input_selected_event != 1)
			custom[0] = 0;
		inputdevice_set_mapping (input_selected_device, input_selected_widget,
			eventnames[input_selected_event], _tcslen (custom) == 0 ? NULL : custom,
			flags, -1, input_selected_sub_num);
		update_listview_input (hDlg);
		inputdevice_updateconfig (&workprefs);
	}
}

static void input_swap (HWND hDlg)
{
	inputdevice_swap_ports (&workprefs, input_selected_device);
	init_inputdlg (hDlg);
}

static void input_find (HWND hDlg, int mode, int set);
static int rawmode;
static int inputmap_remap_counter;

static void CALLBACK timerfunc (HWND hDlg, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
	if (idEvent != 1)
		return;
	int inputmap;
	WINDOWINFO pwi;
	pwi.cbSize = sizeof pwi;
	if (GetWindowInfo (guiDlg, &pwi)) {
		// GUI inactive = disable capturing
		if (pwi.dwWindowStatus != WS_ACTIVECAPTION) {
			input_find (hDlg, 0, false);
			return;
		}
	}
	if (currentpage == INPUTMAP_ID) {
		inputmap = inputmap_remap_counter >= 0 ? 1 : 2;
		setfocus (hDlg, IDC_INPUTMAPLIST);
	} else {
		inputmap = 0;
		setfocus (hDlg, IDC_INPUTLIST);
	}
	int inputmap_index;
	int devnum, wtype, state;
	int cnt = inputdevice_testread_count ();
	if (cnt < 0) {
		input_find (hDlg, 0, FALSE);
		return;
	}
	if (!cnt)
		return;
	int ret = inputdevice_testread (&devnum, &wtype, &state);
	if (ret > 0) {
		if (wtype < 0) {
			if (!state)
				return;
			if (inputmap == 1) {
				int mode, *events, *axistable;
				HWND h = GetDlgItem (hDlg, IDC_INPUTMAPLIST);
				inputmap_remap_counter++;
				ListView_EnsureVisible (h, inputmap_remap_counter, FALSE);
				ListView_SetItemState (h, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
				ListView_SetItemState (h, inputmap_remap_counter, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
				int max = inputdevice_get_compatibility_input (&workprefs, inputmap_port, &mode, &events, &axistable);
				if (inputmap_remap_counter >= max)
					inputmap_remap_counter = -1;
			}
			return;
		}
		if (input_selected_widget != devnum || input_selected_widget != wtype) {
			int od = input_selected_device;
			input_selected_device = devnum;
			input_selected_widget = wtype;
			int type = inputdevice_get_widget_type (input_selected_device, input_selected_widget, NULL);

			if (inputmap == 1) { // ports panel / remap
				static int skipbuttonaxis;

				HWND h = GetDlgItem (hDlg, IDC_INPUTMAPLIST);
				int mode, *events, *axistable, *axistable2;
				int cntadd = 1;

				int max = inputdevice_get_compatibility_input (&workprefs, inputmap_port, &mode, &events, &axistable);
				int evtnum = events[inputmap_remap_counter];
				int type2 = intputdevice_compa_get_eventtype (evtnum, &axistable2);

				if (type == IDEV_WIDGET_BUTTONAXIS) {
					if (skipbuttonaxis) {
						skipbuttonaxis = false;
						return;
					}
					if (type2 != IDEV_WIDGET_BUTTON && type2 != IDEV_WIDGET_KEY)
						return;
				}
				if (type == IDEV_WIDGET_AXIS) {
					skipbuttonaxis = false;
					if (type2 == IDEV_WIDGET_BUTTON || type2 == IDEV_WIDGET_KEY)
						return;
					if (type2 == IDEV_WIDGET_BUTTONAXIS) {
						if (inputmap_remap_counter & 1)
							return;
						// use VERT or HORIZ events, not UP/DOWN/LEFT/RIGHT
						evtnum = axistable2[0];
						cntadd = 2;
						skipbuttonaxis = true;
					}
				}

				struct inputevent *ie = inputdevice_get_eventinfo (evtnum);
				TCHAR name[256];
				inputdevice_get_eventname (ie, name);
				//write_log (L"%d %d %d %s\n", input_selected_device, input_selected_widget, evtnum, name);
				inputdevice_set_gameports_mapping (&workprefs, input_selected_device, input_selected_widget, name, inputmap_port);
				InitializeListView (hDlg);
				inputmap_remap_counter += cntadd;
				ListView_EnsureVisible (h, inputmap_remap_counter, FALSE);
				ListView_SetItemState (h, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
				ListView_SetItemState (h, inputmap_remap_counter, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
				if (inputmap_remap_counter >= max)
					inputmap_remap_counter = -1;
				TCHAR tmp[256];
				tmp[0] = 0;
				inputdevice_get_widget_type (input_selected_device, input_selected_widget, tmp);
				_tcscat (tmp, L", ");
				_tcscat (tmp, inputdevice_get_device_name2 (input_selected_device));
				SetWindowText (GetDlgItem (hDlg, IDC_INPUTMAPOUT), tmp);

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
				if (inputmap_handle (NULL, input_selected_device, input_selected_widget, &op, &inputmap_index, state)) {
					if (op == inputmap_port) {
						ListView_EnsureVisible (h, 1, FALSE);
						ListView_EnsureVisible (h, inputmap_index, FALSE);
						ListView_SetItemState (h, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
						ListView_SetItemState (h, inputmap_index, LVIS_SELECTED , LVIS_SELECTED);
						ListView_SetItemState (h, inputmap_index, LVIS_FOCUSED, LVIS_FOCUSED);
						found = true;
					}
				}
				if (!found) {
					ListView_SetItemState (h, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
				}
				TCHAR tmp[256];
				tmp[0] = 0;
				inputdevice_get_widget_type (input_selected_device, input_selected_widget, tmp);
				_tcscat (tmp, L", ");
				_tcscat (tmp, inputdevice_get_device_name2 (input_selected_device));
				SetWindowText (GetDlgItem (hDlg, IDC_INPUTMAPOUT), tmp);

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
				ListView_EnsureVisible (GetDlgItem (hDlg, IDC_INPUTLIST), input_selected_widget, FALSE);
				ListView_SetItemState (GetDlgItem (hDlg, IDC_INPUTLIST), -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
				ListView_SetItemState (GetDlgItem (hDlg, IDC_INPUTLIST), input_selected_widget, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
				if (rawmode == 1) {
					input_find (hDlg, 0, FALSE);
					if (IsWindowEnabled (GetDlgItem (hDlg, IDC_INPUTAMIGA))) {
						setfocus (hDlg, IDC_INPUTAMIGA);
						SendDlgItemMessage (hDlg, IDC_INPUTAMIGA, CB_SHOWDROPDOWN , TRUE, 0L);
					}
				}
			}
		}
	}
}

static HWND updatePanel (int id);

static int rawdisable[] = {
	IDC_INPUTTYPE, 0, 0, IDC_INPUTDEVICE, 0, 0, IDC_INPUTDEVICEDISABLE, 0, 0,
	IDC_INPUTAMIGACNT, 0, 0, IDC_INPUTAMIGA, 0, 0, IDC_INPUTTEST, 0, 0, IDC_INPUTREMAP, 0, 0,
	IDC_INPUTCOPY, 0, 0, IDC_INPUTCOPYFROM, 0, 0, IDC_INPUTSWAP, 0, 0,
	IDC_INPUTDEADZONE, 0, 0, IDC_INPUTSPEEDD, 0, 0, IDC_INPUTAUTOFIRERATE, 0, 0, IDC_INPUTSPEEDA, 0, 0,
	IDC_PANELTREE, 1, 0, IDC_RESETAMIGA, 1, 0, IDC_QUITEMU, 1, 0, IDC_RESTARTEMU, 1, 0, IDOK, 1, 0, IDCANCEL, 1, 0, IDHELP, 1, 0,
	-1
};


static void input_find (HWND hDlg, int mode, int set)
{
	static TCHAR tmp[200];
	if (set && !rawmode) {
		rawmode = mode ? 2 : 1;
		inputdevice_settest (TRUE);
		inputdevice_acquire (-1);
		if (rawmode == 2) {
			TCHAR tmp2[MAX_DPATH];
			GetWindowText (guiDlg, tmp, sizeof tmp / sizeof (TCHAR));
			WIN32GUI_LoadUIString (IDS_REMAPTITLE, tmp2, sizeof tmp2 / sizeof (TCHAR));
			SetWindowText (guiDlg, tmp2);
		}
		SetTimer (hDlg, 1, 30, timerfunc);
		for (int i = 0; rawdisable[i] >= 0; i += 3) {
			HWND w = GetDlgItem (rawdisable[i + 1] ? guiDlg : hDlg, rawdisable[i]);
			if (w) {
				rawdisable[i + 2] = IsWindowEnabled (w);
				EnableWindow (w, FALSE);
			}
		}
		ShowCursor (FALSE);
		SetCapture (guiDlg);
		RECT r;
		GetWindowRect (guiDlg, &r);
		ClipCursor (&r);
	} else if (rawmode) {
		KillTimer (hDlg, 1);
		ClipCursor (NULL);
		ReleaseCapture ();
		ShowCursor (TRUE);
		inputdevice_unacquire ();
		for (int i = 0; rawdisable[i] >= 0; i += 3) {
			HWND w = GetDlgItem (rawdisable[i + 1] ? guiDlg : hDlg, rawdisable[i]);
			if (w)
				EnableWindow (w, rawdisable[i + 2]);
		}
		inputdevice_settest (FALSE);
		if (rawmode == 2) {
			SetWindowText (guiDlg, tmp);
			SetFocus (hDlg);
		}
		rawmode = FALSE;
		if (currentpage == INPUTMAP_ID)
			updatePanel (GAMEPORTS_ID);
	}
}

static void ports_remap (HWND hDlg, int port)
{
	inputmap_port_remap = port;
	inputmap_port = port;
	inputmap_remap_counter = 0;
	updatePanel (INPUTMAP_ID);
}
static void input_test (HWND hDlg, int port)
{
	inputmap_port_remap = -1;
	inputmap_port = port;
	inputmap_remap_counter = -1;
	updatePanel (INPUTMAP_ID);
}

#if 0
static void input_test (HWND hDlg, int port)
{
	inputmap_port_remap = -1;
	inputmap_port =-1;
	updatePanel (INPUTMAP_ID);
	for (int idx = 0; idx < MAX_JPORTS; idx++) {
		int mode, **events, *axistable;
		write_log (L"Port: %d\n", idx);
		if (inputdevice_get_compatibility_input (&workprefs, idx, &mode, &events, &axistable)) {
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

					write_log (L"- %d: %s = ", i, evt->name);
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
										write_log (L"[%s, %s]", inputdevice_get_device_name2 (devnum), name);
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
						write_log (L"<none>");
					write_log (L"\n");
				}
			}
		} else {
			write_log (L"<none>");
		}
		write_log (L"\n");
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

static INT_PTR CALLBACK InputMapDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive;

	switch (msg)
	{
	case WM_INITDIALOG:
	{
		pages[INPUTMAP_ID] = hDlg;
		currentpage = INPUTMAP_ID;
		inputdevice_updateconfig (&workprefs);
		if (inputmap_remap_counter == 0) {
			inputdevice_compa_prepare_custom (&workprefs, inputmap_port);
			inputdevice_updateconfig (&workprefs);
		}
		InitializeListView (hDlg);
		HWND h = GetDlgItem (hDlg, IDC_INPUTMAPLIST);
		if (inputmap_remap_counter == 0) {
			ListView_EnsureVisible (h, inputmap_remap_counter, FALSE);
			ListView_SetItemState (h, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
			ListView_SetItemState (h, inputmap_remap_counter, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		}
		input_find (hDlg, 1, true);
	}
	case WM_USER:
		recursive++;
		recursive--;
		return TRUE;
	case WM_DESTROY:
		input_find (hDlg, 0, false);
	break;
#if 0
	case WM_MOUSEMOVE:
	{
		int wm = dinput_winmouse ();
		int mx = (signed short) LOWORD (lParam);
		int my = (signed short) HIWORD (lParam);
		setmousestate (dinput_winmouse (), 0, mx, 0);
		setmousestate (dinput_winmouse (), 1, my, 0);
		break;
	}
	case WM_LBUTTONUP:
		setmousebuttonstate (dinput_winmouse (), 0, 0);
		return 0;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONDBLCLK:
		setmousebuttonstate (dinput_winmouse (), 0, 1);
		return 0;
	case WM_RBUTTONUP:
		setmousebuttonstate (dinput_winmouse (), 1, 0);
		return 0;
	case WM_RBUTTONDOWN:
	case WM_RBUTTONDBLCLK:
		setmousebuttonstate (dinput_winmouse (), 1, 1);
		return 0;
	case WM_MBUTTONUP:
		setmousebuttonstate (dinput_winmouse (), 2, 0);
		return 0;
	case WM_MBUTTONDOWN:
	case WM_MBUTTONDBLCLK:
		setmousebuttonstate (dinput_winmouse (), 2, 1);
		return 0;
	case WM_XBUTTONUP:
		handleXbutton (wParam, 0);
		return TRUE;
	case WM_XBUTTONDOWN:
	case WM_XBUTTONDBLCLK:
		handleXbutton (wParam, 1);
		return TRUE;
	case WM_MOUSEWHEEL:
		{
			int val = ((short)HIWORD (wParam));
			setmousestate (dinput_winmouse (), 2, val, 0);
			if (val < 0)
				setmousebuttonstate (dinput_winmouse (), dinput_wheelbuttonstart () + 0, -1);
			else if (val > 0)
				setmousebuttonstate (dinput_winmouse (), dinput_wheelbuttonstart () + 1, -1);
			return TRUE;
		}
	case WM_MOUSEHWHEEL:
		{
			int val = ((short)HIWORD (wParam));
			setmousestate (dinput_winmouse (), 3, val, 0);
			if (val < 0)
				setmousebuttonstate (dinput_winmouse (), dinput_wheelbuttonstart () + 2, -1);
			else if (val > 0)
				setmousebuttonstate (dinput_winmouse (), dinput_wheelbuttonstart () + 3, -1);
			return TRUE;
		}
#endif
	}
	return 0;
}

static void handlerawinput (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_INPUT) {
		handle_rawinput (lParam);
		DefWindowProc (hDlg, msg, wParam, lParam);
	}
}

static void input_copy (HWND hDlg)
{
	int dst = workprefs.input_selected_setting;
	LRESULT src = SendDlgItemMessage (hDlg, IDC_INPUTCOPYFROM, CB_GETCURSEL, 0, 0L);
	if (src == CB_ERR)
		return;
	int v = (int)src;
	if (v >= 0 && v <= 3) {
		inputdevice_copy_single_config (&workprefs, v, workprefs.input_selected_setting, input_selected_device);
	}
	init_inputdlg (hDlg);
}

static void input_toggleautofire (void)
{
	int flags, evt;
	TCHAR name[256];
	TCHAR custom[MAX_DPATH];
	if (input_selected_device < 0 || input_selected_widget < 0)
		return;
	evt = inputdevice_get_mapping (input_selected_device, input_selected_widget,
		&flags, NULL, name, custom, input_selected_sub_num);
	if (evt <= 0)
		return;
	inputdevice_set_mapping (input_selected_device, input_selected_widget,
		name, custom, flags ^ IDEV_MAPPED_AUTOFIRE_SET, -1, input_selected_sub_num);
}
static void input_toggletoggle (void)
{
	int flags, evt;
	TCHAR name[256];
	TCHAR custom[MAX_DPATH];
	if (input_selected_device < 0 || input_selected_widget < 0)
		return;
	evt = inputdevice_get_mapping (input_selected_device, input_selected_widget,
		&flags, NULL, name, custom, input_selected_sub_num);
	if (evt <= 0)
		return;
	inputdevice_set_mapping (input_selected_device, input_selected_widget,
		name, custom, flags ^ IDEV_MAPPED_TOGGLE, -1, input_selected_sub_num);
}

static INT_PTR CALLBACK InputDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	TCHAR name_buf[MAX_DPATH] = L"", desc_buf[128] = L"";
	TCHAR *posn = NULL;
	HWND list;
	int dblclick = 0;
	NM_LISTVIEW *nmlistview;
	int items = 0, entry = 0;
	static int recursive;

	switch (msg)
	{
	case WM_INITDIALOG:
		recursive++;
		pages[INPUT_ID] = hDlg;
		currentpage = INPUT_ID;
		inputdevice_updateconfig (&workprefs);
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
		input_find (hDlg, 0, false);
		break;
	case WM_COMMAND:
		if (recursive)
			break;
		recursive++;
		switch (wParam)
		{
		case IDC_INPUTREMAP:
			input_find (hDlg, 0, true);
			break;
		case IDC_INPUTTEST:
			input_find (hDlg, 1, true);
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
			int row;
			nmlistview = (NM_LISTVIEW *) lParam;
			list = nmlistview->hdr.hwndFrom;
			switch (nmlistview->hdr.code)
			{
			case NM_DBLCLK:
				dblclick = 1;
				/* fall-through */
			case NM_CLICK:
				entry = listview_entry_from_click (list, &row);
				if (entry >= 0) {
					int oldentry = input_selected_widget;
					input_selected_widget = entry;
					if (row == 2 && entry == oldentry)
						input_toggleautofire ();
					if (row == 3 && entry == oldentry)
						input_toggletoggle ();
					if (row == 4) {
						input_selected_sub_num++;
						if (input_selected_sub_num >= MAX_INPUT_SUB_EVENT)
							input_selected_sub_num = 0;
						SendDlgItemMessage (hDlg, IDC_INPUTAMIGACNT, CB_SETCURSEL, input_selected_sub_num, 0);
					}
				} else {
					input_selected_widget = -1;
				}
				if (dblclick)
					doinputcustom (hDlg, 0);
				update_listview_input (hDlg);
				init_inputdlg_2 (hDlg);
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

static void enable_for_hw3ddlg (HWND hDlg)
{
	int v = workprefs.gfx_filter ? TRUE : FALSE;
	int vv = FALSE, vv2 = FALSE, vv3 = FALSE;
	int as = FALSE;
	struct uae_filter *uf;
	int i, isfilter;

	isfilter = 0;
	uf = &uaefilters[0];
	i = 0;
	while (uaefilters[i].name) {
		if (workprefs.gfx_filter == uaefilters[i].type) {
			uf = &uaefilters[i];
			isfilter = 1;
			break;
		}
		i++;
	}
	if (v && uf->intmul)
		vv = TRUE;
	if (v && uf->yuv)
		vv2 = TRUE;
	if (workprefs.gfx_api)
		v = vv = vv2 = vv3 = TRUE;

	ew (hDlg, IDC_FILTERHZ, v);
	ew (hDlg, IDC_FILTERVZ, v);
	ew (hDlg, IDC_FILTERHZMULT, v && !as);
	ew (hDlg, IDC_FILTERVZMULT, v && !as);
	ew (hDlg, IDC_FILTERHO, v && !as);
	ew (hDlg, IDC_FILTERVO, v && !as);
	ew (hDlg, IDC_FILTERSLR, vv3);
	ew (hDlg, IDC_FILTERXL, vv2);
	ew (hDlg, IDC_FILTERXLV, vv2);
	ew (hDlg, IDC_FILTERXTRA, vv2);
	ew (hDlg, IDC_FILTERDEFAULT, v);
	ew (hDlg, IDC_FILTERFILTER, workprefs.gfx_api);
	ew (hDlg, IDC_FILTERKEEPASPECT, v);
	ew (hDlg, IDC_FILTERASPECT, v);
	ew (hDlg, IDC_FILTERASPECT2, v && workprefs.gfx_filter_keep_aspect);
	ew (hDlg, IDC_FILTEROVERLAY, workprefs.gfx_api);
	ew (hDlg, IDC_FILTEROVERLAYTYPE, workprefs.gfx_api);

	ew (hDlg, IDC_FILTERPRESETSAVE, filterpreset_builtin < 0);
	ew (hDlg, IDC_FILTERPRESETLOAD, filterpreset_selected > 0);
	ew (hDlg, IDC_FILTERPRESETDELETE, filterpreset_selected > 0 && filterpreset_builtin < 0);
}

static TCHAR *filtermultnames[] = { L"FS", L"1/2x", L"1x", L"2x", L"4x", L"6x", L"8x", NULL };
static int filtermults[] = { 0, 2000, 1000, 500, 250, 167, 125 };
struct filterxtra {
	TCHAR *label;
	int *varw, *varc;
	int min, max, step;
};
static struct filterxtra *filter_extra[4], *filter_selected;
static int filter_selected_num;

static struct filterxtra filter_pal_extra[] =
{
	L"Brightness", &workprefs.gfx_filter_luminance, &currprefs.gfx_filter_luminance, -1000, 1000, 10,
	L"Contrast", &workprefs.gfx_filter_contrast, &currprefs.gfx_filter_contrast, -1000, 1000, 10,
	L"Saturation", &workprefs.gfx_filter_saturation, &currprefs.gfx_filter_saturation, -1000, 1000, 10,
	L"Gamma", &workprefs.gfx_gamma, &currprefs.gfx_gamma, -1000, 1000, 10,
	L"Scanlines", &workprefs.gfx_filter_scanlines, &currprefs.gfx_filter_scanlines, 0, 100, 1,
	L"Blurriness", &workprefs.gfx_filter_blur, &currprefs.gfx_filter_blur,0, 2000, 10,
	L"Noise", &workprefs.gfx_filter_noise, &currprefs.gfx_filter_noise,0, 100, 10,
	NULL
};
static struct filterxtra filter_3d_extra[] =
{
	L"Point/Bilinear", &workprefs.gfx_filter_bilinear, &currprefs.gfx_filter_bilinear, 0, 1, 1,
	L"Scanline transparency", &workprefs.gfx_filter_scanlines, &currprefs.gfx_filter_scanlines, 0, 100, 10,
	L"Scanline level", &workprefs.gfx_filter_scanlinelevel, &currprefs.gfx_filter_scanlinelevel, 0, 100, 10,
	NULL
};
static int dummy_in, dummy_out;
static int *filtervars[] = {
	&workprefs.gfx_filter, &workprefs.gfx_filter_filtermode,
	&workprefs.gfx_filter_vert_zoom, &workprefs.gfx_filter_horiz_zoom,
	&workprefs.gfx_filter_vert_zoom_mult, &workprefs.gfx_filter_horiz_zoom_mult,
	&workprefs.gfx_filter_vert_offset, &workprefs.gfx_filter_horiz_offset,
	&workprefs.gfx_filter_scanlines, &workprefs.gfx_filter_scanlinelevel, &workprefs.gfx_filter_scanlineratio,
	&workprefs.gfx_resolution, &workprefs.gfx_vresolution, &workprefs.gfx_scanlines,
	&workprefs.gfx_xcenter, &workprefs.gfx_ycenter,
	&workprefs.gfx_filter_luminance, &workprefs.gfx_filter_contrast, &workprefs.gfx_filter_saturation,
	&workprefs.gfx_filter_gamma, &workprefs.gfx_filter_blur, &workprefs.gfx_filter_noise,
	&workprefs.gfx_filter_keep_aspect, &workprefs.gfx_filter_aspect,
	&workprefs.gfx_filter_autoscale, &workprefs.gfx_filter_bilinear,
	NULL
};
static int *filtervars2[] = {
	NULL, &currprefs.gfx_filter_filtermode,
	&currprefs.gfx_filter_vert_zoom, &currprefs.gfx_filter_horiz_zoom,
	&currprefs.gfx_filter_vert_zoom_mult, &currprefs.gfx_filter_horiz_zoom_mult,
	&currprefs.gfx_filter_vert_offset, &currprefs.gfx_filter_horiz_offset,
	&currprefs.gfx_filter_scanlines, &currprefs.gfx_filter_scanlinelevel, &currprefs.gfx_filter_scanlineratio,
	&currprefs.gfx_resolution, &currprefs.gfx_vresolution, &currprefs.gfx_scanlines,
	&currprefs.gfx_xcenter, &currprefs.gfx_ycenter,
	&currprefs.gfx_filter_luminance, &currprefs.gfx_filter_contrast, &currprefs.gfx_filter_saturation,
	&currprefs.gfx_filter_gamma, &currprefs.gfx_filter_blur, &currprefs.gfx_filter_noise,
	&currprefs.gfx_filter_keep_aspect, &currprefs.gfx_filter_aspect,
	&currprefs.gfx_filter_autoscale, &currprefs.gfx_filter_bilinear,
	NULL
};

struct filterpreset {
	TCHAR *name;
	int conf[25];
};
static struct filterpreset filterpresets[] =
{
	{ L"PAL",				UAE_FILTER_PAL,		0, 0, 0, 0, 0, 0, 0, 50, 0, 0, 1, 1, 0, 0, 0, 10, 0, 0, 0, 300, 30, 0,  0, 0 },
	{ L"D3D Autoscale",		UAE_FILTER_NULL,	2, 0, 0, 0, 0, 0, 0,  0, 0, 0, 1, 1, 0, 0, 0,  0, 0, 0, 0,   0,  0, 0, -1, 1 },
	{ L"D3D Full Scaling",	UAE_FILTER_NULL,	2, 0, 0, 0, 0, 0, 0,  0, 0, 0, 1, 1, 0, 0, 0,  0, 0, 0, 0,   0,  0, 0, -1, 0 },
	{ NULL }
};

static int getfiltermult (HWND hDlg, DWORD dlg)
{
	TCHAR tmp[100];
	LRESULT v = SendDlgItemMessage (hDlg, dlg, CB_GETCURSEL, 0, 0L);
	float f;

	if (v != CB_ERR)
		return filtermults[v];
	SendDlgItemMessage (hDlg, dlg, WM_GETTEXT, (WPARAM)sizeof tmp / sizeof (TCHAR), (LPARAM)tmp);
	if (!_tcsicmp (tmp, L"FS"))
		return 0;
	f = (float)_tstof (tmp);
	if (f < 0)
		f = 0;
	if (f > 9)
		f = 9;
	return (int)(1000.0 / f);
}

static void setfiltermult2 (HWND hDlg, int id, int val)
{
	int i, got;

	got = 0;
	for (i = 0; filtermultnames[i]; i++) {
		if (filtermults[i] == val) {
			SendDlgItemMessage (hDlg, id, CB_SETCURSEL, i, 0);
			got = 1;
		}
	}
	if (!got) {
		TCHAR tmp[100];
		tmp[0] = 0;
		if (val > 0)
			_stprintf (tmp, L"%.2f", 1000.0 / val);
		SendDlgItemMessage (hDlg, id, CB_SETCURSEL, 0, 0);
		SetDlgItemText (hDlg, id, tmp);
	}
}

static void setfiltermult (HWND hDlg)
{
	setfiltermult2 (hDlg, IDC_FILTERHZMULT, workprefs.gfx_filter_horiz_zoom_mult);
	setfiltermult2 (hDlg, IDC_FILTERVZMULT, workprefs.gfx_filter_vert_zoom_mult);
}

static void values_to_hw3ddlg (HWND hDlg)
{
	TCHAR txt[100], tmp[100];
	int i, j, fltnum;
	struct uae_filter *uf;
	int fxidx, fxcnt;
	UAEREG *fkey;

	SendDlgItemMessage (hDlg, IDC_FILTERASPECT, CB_SETCURSEL,
		(workprefs.gfx_filter_aspect == 0) ? 0 :
		(workprefs.gfx_filter_aspect < 0) ? 1 :
		(workprefs.gfx_filter_aspect == 4 * 256 + 3) ? 2 :
		(workprefs.gfx_filter_aspect == 5 * 256 + 4) ? 3 :
		(workprefs.gfx_filter_aspect == 15 * 256 + 9) ? 4 :
		(workprefs.gfx_filter_aspect == 16 * 256 + 9) ? 5 :
		(workprefs.gfx_filter_aspect == 16 * 256 + 10) ? 6 : 0, 0);

	CheckDlgButton (hDlg, IDC_FILTERKEEPASPECT, workprefs.gfx_filter_keep_aspect);

	SendDlgItemMessage (hDlg, IDC_FILTERASPECT2, CB_SETCURSEL,
		workprefs.gfx_filter_keep_aspect, 0);

	SendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_RESETCONTENT, 0, 0L);
	WIN32GUI_LoadUIString (IDS_AUTOSCALE_DISABLED, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_AUTOSCALE_DEFAULT, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_AUTOSCALE_TV, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_AUTOSCALE_MAX, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_AUTOSCALE_SCALING, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_AUTOSCALE_RESIZE, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_AUTOSCALE_CENTER, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_ADDSTRING, 0, (LPARAM)txt);
	WIN32GUI_LoadUIString (IDS_AUTOSCALE_MANUAL, txt, sizeof (txt) / sizeof (TCHAR));
	SendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_ADDSTRING, 0, (LPARAM)txt);
	SendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_SETCURSEL, workprefs.gfx_filter_autoscale, 0);

	int range1 = workprefs.gfx_filter_autoscale == AUTOSCALE_MANUAL ? -1 : -999;
	int range2 = workprefs.gfx_filter_autoscale == AUTOSCALE_MANUAL ? 1800 : 999;

	SendDlgItemMessage (hDlg, IDC_FILTERHZ, TBM_SETRANGE, TRUE, MAKELONG (range1, range2));
	SendDlgItemMessage (hDlg, IDC_FILTERHZ, TBM_SETPAGESIZE, 0, 1);
	SendDlgItemMessage (hDlg, IDC_FILTERVZ, TBM_SETRANGE, TRUE, MAKELONG (range1, range2));
	SendDlgItemMessage (hDlg, IDC_FILTERVZ, TBM_SETPAGESIZE, 0, 1);
	SendDlgItemMessage (hDlg, IDC_FILTERHO, TBM_SETRANGE, TRUE, MAKELONG (range1, range2));
	SendDlgItemMessage (hDlg, IDC_FILTERHO, TBM_SETPAGESIZE, 0, 1);
	SendDlgItemMessage (hDlg, IDC_FILTERVO, TBM_SETRANGE, TRUE, MAKELONG (range1, range2));
	SendDlgItemMessage (hDlg, IDC_FILTERVO, TBM_SETPAGESIZE, 0, 1);

	SendDlgItemMessage (hDlg, IDC_FILTEROVERLAY, CB_RESETCONTENT, 0, 0L);
	SendDlgItemMessage (hDlg, IDC_FILTERMODE, CB_RESETCONTENT, 0, 0L);
	WIN32GUI_LoadUIString (IDS_NONE, tmp, MAX_DPATH);
	SendDlgItemMessage (hDlg, IDC_FILTERMODE, CB_ADDSTRING, 0, (LPARAM)tmp);
	uf = NULL;
	fltnum = 0;
	i = 0; j = 1;
	while (uaefilters[i].name) {
		SendDlgItemMessage (hDlg, IDC_FILTERMODE, CB_ADDSTRING, 0, (LPARAM)uaefilters[i].name);
		if (uaefilters[i].type == workprefs.gfx_filter) {
			uf = &uaefilters[i];
			fltnum = j;
		}
		j++;
		i++;
	}
	if (workprefs.gfx_api && D3D_canshaders ()) {
		HANDLE h;
		WIN32_FIND_DATA wfd;
		TCHAR tmp[MAX_DPATH];
		get_plugin_path (tmp, sizeof tmp / sizeof (TCHAR), L"filtershaders\\direct3d");
		_tcscat (tmp, L"*.fx");
		h = FindFirstFile (tmp, &wfd);
		while (h != INVALID_HANDLE_VALUE) {
			if (wfd.cFileName[0] != '_') {
				TCHAR tmp2[100];
				_stprintf (tmp2, L"D3D: %s", wfd.cFileName);
				tmp2[_tcslen (tmp2) - 3] = 0;
				SendDlgItemMessage (hDlg, IDC_FILTERMODE, CB_ADDSTRING, 0, (LPARAM)tmp2);
				if (workprefs.gfx_api && !_tcscmp (workprefs.gfx_filtershader, wfd.cFileName))
					fltnum = j;
				j++;
			}
			if (!FindNextFile (h, &wfd)) {
				FindClose (h);
				h = INVALID_HANDLE_VALUE;
			}
		}
	}
	int overlaytype = SendDlgItemMessage (hDlg, IDC_FILTEROVERLAYTYPE, CB_GETCURSEL, 0, 0L);
	if (workprefs.gfx_api && D3D_goodenough ()) {
		WIN32GUI_LoadUIString (IDS_NONE, tmp, MAX_DPATH);
		SendDlgItemMessage (hDlg, IDC_FILTEROVERLAY, CB_ADDSTRING, 0, (LPARAM)tmp);
		SendDlgItemMessage (hDlg, IDC_FILTEROVERLAY, CB_SETCURSEL, 0, 0);
		HANDLE h;
		WIN32_FIND_DATA wfd;
		TCHAR tmp[MAX_DPATH];
		get_plugin_path (tmp, sizeof tmp / sizeof (TCHAR), overlaytype == 0 ? L"overlays" : L"masks");
		_tcscat (tmp, L"*.*");
		h = FindFirstFile (tmp, &wfd);
		i = 0; j = 1;
		while (h != INVALID_HANDLE_VALUE) {
			TCHAR *ext = _tcsrchr (wfd.cFileName, '.');
			if (ext && (
				!_tcsicmp (ext, L".png") ||
				!_tcsicmp (ext, L".bmp")))
			{
				SendDlgItemMessage (hDlg, IDC_FILTEROVERLAY, CB_ADDSTRING, 0, (LPARAM)wfd.cFileName);
				if (!_tcsicmp (wfd.cFileName, overlaytype == 0 ? workprefs.gfx_filteroverlay : workprefs.gfx_filtermask))
					SendDlgItemMessage (hDlg, IDC_FILTEROVERLAY, CB_SETCURSEL, j, 0);
				j++;

			}
			if (!FindNextFile (h, &wfd)) {
				FindClose (h);
				h = INVALID_HANDLE_VALUE;
			}
		}
	} else {
		WIN32GUI_LoadUIString (IDS_FILTER_NOOVERLAYS, tmp, MAX_DPATH);
		SendDlgItemMessage (hDlg, IDC_FILTEROVERLAY, CB_ADDSTRING, 0, (LPARAM)tmp);
		SendDlgItemMessage (hDlg, IDC_FILTEROVERLAY, CB_SETCURSEL, 0, 0);
	}
	SendDlgItemMessage (hDlg, IDC_FILTERMODE, CB_SETCURSEL, fltnum, 0);

	fxidx = 0;
	filter_extra[fxidx] = NULL;
	if (workprefs.gfx_api) {
		filter_extra[fxidx++] = filter_3d_extra;
		filter_extra[fxidx] = NULL;
	}
	int filtermodenum = 0;
	SendDlgItemMessage (hDlg, IDC_FILTERFILTER, CB_RESETCONTENT, 0, 0L);
	if (workprefs.gfx_api) {
		for (i = 0; i < 4; i++) {
			TCHAR tmp[100];
			_stprintf (tmp, L"%dx", i + 1);
			SendDlgItemMessage (hDlg, IDC_FILTERFILTER, CB_ADDSTRING, 0, (LPARAM)tmp);
			filtermodenum++;
		}
	}
	if (uf && uf->yuv) {
		filter_extra[fxidx++] = filter_pal_extra;
		filter_extra[fxidx] = NULL;
	}
	SendDlgItemMessage (hDlg, IDC_FILTERXL, TBM_SETRANGE, TRUE, MAKELONG (   0, +1000));
	SendDlgItemMessage (hDlg, IDC_FILTERXL, TBM_SETPAGESIZE, 0, 1);
	if (filter_extra[0]) {
		struct filterxtra *prev = filter_selected;
		int idx2 = -1;
		int idx = SendDlgItemMessage (hDlg, IDC_FILTERXTRA, CB_GETCURSEL, 0, 0L);
		if (idx == CB_ERR)
			idx = -1;
		fxcnt = 0;
		filter_selected = &filter_extra[0][0];
		SendDlgItemMessage (hDlg, IDC_FILTERXTRA, CB_RESETCONTENT, 0, 0L);
		for (j = 0; filter_extra[j]; j++) {
			struct filterxtra *fx = filter_extra[j];
			for (i = 0; fx[i].label; i++) {
				if (prev == &fx[i] && idx < 0) {
					idx2 = i;
					filter_selected = &fx[idx2];
					filter_selected_num = fxcnt;
				}
				SendDlgItemMessage (hDlg, IDC_FILTERXTRA, CB_ADDSTRING, 0, (LPARAM)fx[i].label);
				if (idx == 0) {
					filter_selected = &fx[i];
					filter_selected_num = fxcnt;
					prev = NULL;
				}
				fxcnt++;
				idx--;
			}
		}
		SendDlgItemMessage (hDlg, IDC_FILTERXTRA, CB_SETCURSEL, filter_selected_num, 0);
		SendDlgItemMessage (hDlg, IDC_FILTERXL, TBM_SETRANGE, TRUE, MAKELONG (filter_selected->min, filter_selected->max));
		SendDlgItemMessage (hDlg, IDC_FILTERXL, TBM_SETPAGESIZE, 0, filter_selected->step);
		SendDlgItemMessage (hDlg, IDC_FILTERXL, TBM_SETPOS, TRUE, *(filter_selected->varw));
		SetDlgItemInt (hDlg, IDC_FILTERXLV, *(filter_selected->varw), TRUE);
	}
	if (workprefs.gfx_filter_filtermode >= filtermodenum)
		workprefs.gfx_filter_filtermode = 0;
	SendDlgItemMessage (hDlg, IDC_FILTERFILTER, CB_SETCURSEL, workprefs.gfx_filter_filtermode, 0);
	setfiltermult (hDlg);

	SendDlgItemMessage (hDlg, IDC_FILTERSLR, CB_RESETCONTENT, 0, 0L);
	i = j = 0;
	while (scanlineratios[i * 2]) {
		int sl = scanlineratios[i * 2] * 16 + scanlineratios[i * 2 + 1];
		_stprintf (txt, L"%d:%d", scanlineratios[i * 2], scanlineratios[i * 2 + 1]);
		if (workprefs.gfx_filter_scanlineratio == sl)
			j = i;
		SendDlgItemMessage (hDlg, IDC_FILTERSLR, CB_ADDSTRING, 0, (LPARAM)txt);
		scanlineindexes[i] = sl;
		i++;
	}
	SendDlgItemMessage (hDlg, IDC_FILTERSLR, CB_SETCURSEL, j, 0);

	j = 0;
	SendDlgItemMessage (hDlg, IDC_FILTERPRESETS, CB_RESETCONTENT, 0, 0L);
	SendDlgItemMessage (hDlg, IDC_FILTERPRESETS, CB_ADDSTRING, 0, (LPARAM)L"");
	for (i = 0; filterpresets[i].name; i++) {
		TCHAR tmp[MAX_DPATH];
		_stprintf (tmp, L"* %s", filterpresets[i].name);
		SendDlgItemMessage (hDlg, IDC_FILTERPRESETS, CB_ADDSTRING, 0, (LPARAM)tmp);
	}
	fkey = regcreatetree (NULL, L"FilterPresets");
	if (fkey) {
		int idx = 0;
		TCHAR tmp[MAX_DPATH], tmp2[MAX_DPATH];
		int size, size2;

		for (;;) {
			size = sizeof (tmp) / sizeof (TCHAR);
			size2 = sizeof (tmp2) / sizeof (TCHAR);
			if (!regenumstr (fkey, idx, tmp, &size, tmp2, &size2))
				break;
			SendDlgItemMessage (hDlg, IDC_FILTERPRESETS, CB_ADDSTRING, 0, (LPARAM)tmp);
			idx++;
		}
		SendDlgItemMessage (hDlg, IDC_FILTERPRESETS, CB_SETCURSEL, filterpreset_selected, 0);
		regclosetree (fkey);
	}
	int ho, vo, hz, vz;
	if (workprefs.gfx_filter_autoscale == AUTOSCALE_MANUAL) {
		hz = workprefs.gfx_xcenter_size;
		vz = workprefs.gfx_ycenter_size;
		ho = workprefs.gfx_xcenter_pos;
		vo = workprefs.gfx_ycenter_pos;
	} else {
		hz = workprefs.gfx_filter_horiz_zoom;
		vz = workprefs.gfx_filter_vert_zoom;
		ho = workprefs.gfx_filter_horiz_offset;
		vo = workprefs.gfx_filter_vert_offset;
	}

	SendDlgItemMessage (hDlg, IDC_FILTERHZ, TBM_SETPOS, TRUE, hz);
	SendDlgItemMessage (hDlg, IDC_FILTERVZ, TBM_SETPOS, TRUE, vz);
	SendDlgItemMessage (hDlg, IDC_FILTERHO, TBM_SETPOS, TRUE, ho);
	SendDlgItemMessage (hDlg, IDC_FILTERVO, TBM_SETPOS, TRUE, vo);
	SetDlgItemInt (hDlg, IDC_FILTERHZV, hz, TRUE);
	SetDlgItemInt (hDlg, IDC_FILTERVZV, vo, TRUE);
	SetDlgItemInt (hDlg, IDC_FILTERHOV, ho, TRUE);
	SetDlgItemInt (hDlg, IDC_FILTERVOV, vo, TRUE);
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
	fkey = regcreatetree (NULL, L"FilterPresets");
	item = SendDlgItemMessage (hDlg, IDC_FILTERPRESETS, CB_GETCURSEL, 0, 0);
	tmp1[0] = 0;
	if (item != CB_ERR) {
		filterpreset_selected = (int)item;
		SendDlgItemMessage (hDlg, IDC_FILTERPRESETS, CB_GETLBTEXT, (WPARAM)item, (LPARAM)tmp1);
	} else {
		filterpreset_selected = -1;
		SendDlgItemMessage (hDlg, IDC_FILTERPRESETS, WM_GETTEXT, (WPARAM)sizeof tmp1 / sizeof (TCHAR), (LPARAM)tmp1);
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
		for (i = 0; filtervars[i]; i++) {
			if (i > 0) {
				_tcscat (p, L",");
				p++;
			}
			_stprintf (p, L"%d", filterpresets[filterpreset_builtin].conf[i]);
			p += _tcslen (p);
		}
		ok = 1;
	}

	if (wParam == IDC_FILTERPRESETSAVE && userfilter && fkey) {
		TCHAR *p = tmp2;
		for (i = 0; filtervars[i]; i++) {
			if (i > 0) {
				_tcscat (p, L",");
				p++;
			}
			_stprintf (p, L"%d", *(filtervars[i]));
			p += _tcslen (p);
		}
		if (ok == 0) {
			tmp1[0] = 0;
			SendDlgItemMessage (hDlg, IDC_FILTERPRESETS, WM_GETTEXT, (WPARAM)sizeof (tmp1) / sizeof (TCHAR), (LPARAM)tmp1);
			if (tmp1[0] == 0)
				goto end;
		}
		regsetstr (fkey, tmp1, tmp2);
		values_to_hw3ddlg (hDlg);
	}
	if (ok) {
		if (wParam == IDC_FILTERPRESETDELETE && userfilter) {
			regdelete (fkey, tmp1);
			values_to_hw3ddlg (hDlg);
		} else if (wParam == IDC_FILTERPRESETLOAD) {
			TCHAR *s = tmp2;
			TCHAR *t;

			load = 1;
			_tcscat (s, L",");
			t = _tcschr (s, ',');
			*t++ = 0;
			for (i = 0; filtervars[i]; i++) {
				*(filtervars[i]) = _tstol(s);
				if (filtervars2[i])
					*(filtervars2[i]) = *(filtervars[i]);
				s = t;
				t = _tcschr (s, ',');
				if (!t)
					break;
				*t++ = 0;
			}
		}
	}
end:
	regclosetree (fkey);
	enable_for_hw3ddlg (hDlg);
	if (load) {
		values_to_hw3ddlg (hDlg);
		SendMessage (hDlg, WM_HSCROLL, 0, 0);
	}
}

static void filter_handle (HWND hDlg)
{
	LRESULT item = SendDlgItemMessage (hDlg, IDC_FILTERMODE, CB_GETCURSEL, 0, 0L);
	if (item != CB_ERR) {
		TCHAR tmp[MAX_DPATH], oldsh[MAX_DPATH];
		int of = workprefs.gfx_filter;
		int off = workprefs.gfx_filter_filtermode;
		tmp[0] = 0;
		_tcscpy (oldsh, workprefs.gfx_filtershader);
		SendDlgItemMessage (hDlg, IDC_FILTERMODE, CB_GETLBTEXT, (WPARAM)item, (LPARAM)tmp);
		workprefs.gfx_filtershader[0] = 0;
		workprefs.gfx_filter = 0;
		workprefs.gfx_filter_filtermode = 0;
		if (workprefs.gfx_api) {
			LRESULT item2 = SendDlgItemMessage (hDlg, IDC_FILTERFILTER, CB_GETCURSEL, 0, 0L);
			if (item2 != CB_ERR)
				workprefs.gfx_filter_filtermode = (int)item2;
		}
		if (item > 0) {
			if (item > UAE_FILTER_LAST) {
				_stprintf (workprefs.gfx_filtershader, L"%s.fx", tmp + 5);
			} else {
				item--;
				workprefs.gfx_filter = uaefilters[item].type;
			}
			if (of != workprefs.gfx_filter || off != workprefs.gfx_filter_filtermode) {
				values_to_hw3ddlg (hDlg);
				hw3d_changed = 1;
			}
		}
		if (workprefs.gfx_filter == 0 && !workprefs.gfx_api)
			workprefs.gfx_filter_autoscale = 0;
	}

	int overlaytype = SendDlgItemMessage (hDlg, IDC_FILTEROVERLAYTYPE, CB_GETCURSEL, 0, 0L);
	TCHAR *filterptr = overlaytype == 0 ? workprefs.gfx_filteroverlay : workprefs.gfx_filtermask;
	item = SendDlgItemMessage (hDlg, IDC_FILTEROVERLAY, CB_GETCURSEL, 0, 0L);
	if (item != CB_ERR) {
		TCHAR tmp[MAX_DPATH];
		tmp[0] = 0;
		if (item > 0) {
			SendDlgItemMessage (hDlg, IDC_FILTEROVERLAY, CB_GETLBTEXT, (WPARAM)item, (LPARAM)tmp);
			if (_tcsicmp (filterptr, tmp)) {
				_tcscpy (filterptr, tmp);
			}
		} else {
			filterptr[0] = 0;
		}
	}
	enable_for_hw3ddlg (hDlg);
	updatedisplayarea ();
}

static INT_PTR CALLBACK hw3dDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive;
	LRESULT item;
	TCHAR tmp[100];
	int i;
	static int filteroverlaypos;

	switch (msg)
	{
	case WM_INITDIALOG:
		pages[HW3D_ID] = hDlg;
		currentpage = HW3D_ID;
		SendDlgItemMessage (hDlg, IDC_FILTERASPECT, CB_RESETCONTENT, 0, 0);
		WIN32GUI_LoadUIString (IDS_DISABLED, tmp, sizeof tmp / sizeof (TCHAR));
		SendDlgItemMessage (hDlg, IDC_FILTERASPECT, CB_ADDSTRING, 0, (LPARAM)tmp);
		WIN32GUI_LoadUIString (IDS_AUTOMATIC, tmp, sizeof tmp / sizeof (TCHAR));
		SendDlgItemMessage (hDlg, IDC_FILTERASPECT, CB_ADDSTRING, 0, (LPARAM)tmp);
		SendDlgItemMessage (hDlg, IDC_FILTERASPECT, CB_ADDSTRING, 0, (LPARAM)L"4:3");
		SendDlgItemMessage (hDlg, IDC_FILTERASPECT, CB_ADDSTRING, 0, (LPARAM)L"5:4");
		SendDlgItemMessage (hDlg, IDC_FILTERASPECT, CB_ADDSTRING, 0, (LPARAM)L"15:9");
		SendDlgItemMessage (hDlg, IDC_FILTERASPECT, CB_ADDSTRING, 0, (LPARAM)L"16:9");
		SendDlgItemMessage (hDlg, IDC_FILTERASPECT, CB_ADDSTRING, 0, (LPARAM)L"16:10");

		SendDlgItemMessage (hDlg, IDC_FILTERASPECT2, CB_RESETCONTENT, 0, 0);
		WIN32GUI_LoadUIString (IDS_DISABLED, tmp, sizeof tmp / sizeof (TCHAR));
		SendDlgItemMessage (hDlg, IDC_FILTERASPECT2, CB_ADDSTRING, 0, (LPARAM)tmp);
		SendDlgItemMessage (hDlg, IDC_FILTERASPECT2, CB_ADDSTRING, 0, (LPARAM)L"VGA");
		SendDlgItemMessage (hDlg, IDC_FILTERASPECT2, CB_ADDSTRING, 0, (LPARAM)L"TV");

		SendDlgItemMessage (hDlg, IDC_FILTERHZMULT, CB_RESETCONTENT, 0, 0L);
		SendDlgItemMessage (hDlg, IDC_FILTERVZMULT, CB_RESETCONTENT, 0, 0L);
		for (i = 0; filtermultnames[i]; i++) {
			SendDlgItemMessage (hDlg, IDC_FILTERHZMULT, CB_ADDSTRING, 0, (LPARAM)filtermultnames[i]);
			SendDlgItemMessage (hDlg, IDC_FILTERVZMULT, CB_ADDSTRING, 0, (LPARAM)filtermultnames[i]);
		}

		SendDlgItemMessage (hDlg, IDC_FILTEROVERLAYTYPE, CB_RESETCONTENT, 0, 0L);
		SendDlgItemMessage (hDlg, IDC_FILTEROVERLAYTYPE, CB_ADDSTRING, 0, (LPARAM)L"Overlays");
		SendDlgItemMessage (hDlg, IDC_FILTEROVERLAYTYPE, CB_ADDSTRING, 0, (LPARAM)L"Masks");
		SendDlgItemMessage (hDlg, IDC_FILTEROVERLAYTYPE, CB_SETCURSEL, filteroverlaypos, 0);

		enable_for_hw3ddlg (hDlg);

	case WM_USER:
		if(recursive > 0)
			break;
		recursive++;
		enable_for_hw3ddlg (hDlg);
		values_to_hw3ddlg (hDlg);
		recursive--;
		return TRUE;
	case WM_COMMAND:
		if(recursive > 0)
			break;
		recursive++;
		switch (wParam)
		{
		case IDC_FILTERDEFAULT:
			currprefs.gfx_filter_horiz_zoom = workprefs.gfx_filter_horiz_zoom = 0;
			currprefs.gfx_filter_vert_zoom = workprefs.gfx_filter_vert_zoom = 0;
			currprefs.gfx_filter_horiz_offset = workprefs.gfx_filter_horiz_offset = 0;
			currprefs.gfx_filter_vert_offset = workprefs.gfx_filter_vert_offset = 0;
			values_to_hw3ddlg (hDlg);
			updatedisplayarea ();
			WIN32GFX_WindowMove ();
			break;
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
					currprefs.gfx_filter_keep_aspect = workprefs.gfx_filter_keep_aspect = 1;
				else
					currprefs.gfx_filter_keep_aspect = workprefs.gfx_filter_keep_aspect = 0;
				enable_for_hw3ddlg (hDlg);
				values_to_hw3ddlg (hDlg);
				updatedisplayarea ();
				WIN32GFX_WindowMove ();
			}
			break;
		default:
			if (HIWORD (wParam) == CBN_SELCHANGE || HIWORD (wParam) == CBN_KILLFOCUS)  {
				switch (LOWORD (wParam))
				{
				case IDC_FILTERAUTOSCALE:
					item = SendDlgItemMessage (hDlg, IDC_FILTERAUTOSCALE, CB_GETCURSEL, 0, 0L);
					if (item != CB_ERR) {
						workprefs.gfx_filter_autoscale = item;
						if (workprefs.gfx_filter_autoscale && workprefs.gfx_filter == 0 && !workprefs.gfx_api)
							workprefs.gfx_filter = 1; // NULL
						values_to_hw3ddlg (hDlg);
						enable_for_hw3ddlg (hDlg);
					}
					break;
				case IDC_FILTERXTRA:
					values_to_hw3ddlg (hDlg);
					break;
				case IDC_FILTERPRESETS:
					filter_preset (hDlg, LOWORD (wParam));
					break;
				case IDC_FILTERSLR:
					item = SendDlgItemMessage (hDlg, IDC_FILTERSLR, CB_GETCURSEL, 0, 0L);
					if (item != CB_ERR) {
						currprefs.gfx_filter_scanlineratio = workprefs.gfx_filter_scanlineratio = scanlineindexes[item];
						updatedisplayarea ();
					}
					break;
				case IDC_FILTEROVERLAYTYPE:
					item = SendDlgItemMessage (hDlg, IDC_FILTEROVERLAYTYPE, CB_GETCURSEL, 0, 0L);
					if (item != CB_ERR) {
						filteroverlaypos = item;
						values_to_hw3ddlg (hDlg);
					}
					break;
				case IDC_FILTERMODE:
				case IDC_FILTERFILTER:
				case IDC_FILTEROVERLAY:
					filter_handle (hDlg);
					values_to_hw3ddlg (hDlg);
					break;
				case IDC_FILTERHZMULT:
					currprefs.gfx_filter_horiz_zoom_mult = workprefs.gfx_filter_horiz_zoom_mult = getfiltermult (hDlg, IDC_FILTERHZMULT);
					updatedisplayarea ();
					WIN32GFX_WindowMove ();
					break;
				case IDC_FILTERVZMULT:
					currprefs.gfx_filter_vert_zoom_mult = workprefs.gfx_filter_vert_zoom_mult = getfiltermult (hDlg, IDC_FILTERVZMULT);
					updatedisplayarea ();
					WIN32GFX_WindowMove ();
					break;
				case IDC_FILTERASPECT:
					{
						int v = SendDlgItemMessage (hDlg, IDC_FILTERASPECT, CB_GETCURSEL, 0, 0L);
						int v2 = 0;
						if (v != CB_ERR) {
							if (v == 0)
								v2 = 0;
							if (v == 1)
								v2 = -1;
							if (v == 2)
								v2 = 4 * 256 + 3;
							if (v == 3)
								v2 = 5 * 256 + 4;
							if (v == 4)
								v2 = 15 * 256 + 9;
							if (v == 5)
								v2 = 16 * 256 + 9;
							if (v == 6)
								v2 = 16 * 256 + 10;
						}
						currprefs.gfx_filter_aspect = workprefs.gfx_filter_aspect = v2;
						updatedisplayarea ();
						WIN32GFX_WindowMove ();
					}
					break;
				case IDC_FILTERASPECT2:
					{
						int v = SendDlgItemMessage (hDlg, IDC_FILTERASPECT2, CB_GETCURSEL, 0, 0L);
						if (v != CB_ERR)
							currprefs.gfx_filter_keep_aspect = workprefs.gfx_filter_keep_aspect = v;
						updatedisplayarea ();
						WIN32GFX_WindowMove ();
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
			HWND h = (HWND)lParam;

			if (recursive)
				break;
			recursive++;
			if (currprefs.gfx_filter_autoscale == AUTOSCALE_MANUAL) {
				currprefs.gfx_xcenter_size = workprefs.gfx_xcenter_size = (int)SendMessage (hz, TBM_GETPOS, 0, 0);
				currprefs.gfx_ycenter_size = workprefs.gfx_ycenter_size = (int)SendMessage (vz, TBM_GETPOS, 0, 0);
				currprefs.gfx_xcenter_pos = workprefs.gfx_xcenter_pos = (int)SendMessage (GetDlgItem (hDlg, IDC_FILTERHO), TBM_GETPOS, 0, 0);
				currprefs.gfx_ycenter_pos = workprefs.gfx_ycenter_pos = (int)SendMessage (GetDlgItem (hDlg, IDC_FILTERVO), TBM_GETPOS, 0, 0);
				SetDlgItemInt (hDlg, IDC_FILTERHOV, workprefs.gfx_xcenter_pos, TRUE);
				SetDlgItemInt (hDlg, IDC_FILTERVOV, workprefs.gfx_ycenter_pos, TRUE);
				SetDlgItemInt (hDlg, IDC_FILTERHZV, workprefs.gfx_xcenter_size, TRUE);
				SetDlgItemInt (hDlg, IDC_FILTERVZV, workprefs.gfx_ycenter_size, TRUE);
			} else {
				if (h == hz) {
					currprefs.gfx_filter_horiz_zoom = workprefs.gfx_filter_horiz_zoom = (int)SendMessage (hz, TBM_GETPOS, 0, 0);
					if (workprefs.gfx_filter_keep_aspect) {
						currprefs.gfx_filter_vert_zoom = workprefs.gfx_filter_vert_zoom = currprefs.gfx_filter_horiz_zoom;
						SendDlgItemMessage (hDlg, IDC_FILTERVZ, TBM_SETPOS, TRUE, workprefs.gfx_filter_vert_zoom);
					}
				} else if (h == vz) {
					currprefs.gfx_filter_vert_zoom = workprefs.gfx_filter_vert_zoom = (int)SendMessage (vz, TBM_GETPOS, 0, 0);
					if (workprefs.gfx_filter_keep_aspect) {
						currprefs.gfx_filter_horiz_zoom = workprefs.gfx_filter_horiz_zoom = currprefs.gfx_filter_vert_zoom;
						SendDlgItemMessage (hDlg, IDC_FILTERHZ, TBM_SETPOS, TRUE, workprefs.gfx_filter_horiz_zoom);
					}
				}
				currprefs.gfx_filter_horiz_offset = workprefs.gfx_filter_horiz_offset = (int)SendMessage (GetDlgItem (hDlg, IDC_FILTERHO), TBM_GETPOS, 0, 0);
					currprefs.gfx_filter_vert_offset = workprefs.gfx_filter_vert_offset = (int)SendMessage (GetDlgItem (hDlg, IDC_FILTERVO), TBM_GETPOS, 0, 0);
					SetDlgItemInt (hDlg, IDC_FILTERHOV, workprefs.gfx_filter_horiz_offset, TRUE);
					SetDlgItemInt (hDlg, IDC_FILTERVOV, workprefs.gfx_filter_vert_offset, TRUE);
				SetDlgItemInt (hDlg, IDC_FILTERHZV, workprefs.gfx_filter_horiz_zoom, TRUE);
				SetDlgItemInt (hDlg, IDC_FILTERVZV, workprefs.gfx_filter_vert_zoom, TRUE);
			}
			if (filter_selected) {
				int *pw = filter_selected->varw;
				int *pc = filter_selected->varc;
				int v = (int)SendMessage (GetDlgItem(hDlg, IDC_FILTERXL), TBM_GETPOS, 0, 0);
				if (v < filter_selected->min)
					v = filter_selected->min;
				if (v > filter_selected->max)
					v = filter_selected->max;
				*pw = v;
				*pc = v;
				SetDlgItemInt (hDlg, IDC_FILTERXLV, v, TRUE);
			}
			init_colors ();
			notice_new_xcolors ();
			reset_drawing ();
			updatedisplayarea ();
			WIN32GFX_WindowMove ();
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

	updatewinfsmode (&workprefs);
	SetDlgItemText (hDlg, IDC_AVIOUTPUT_FILETEXT, avioutput_filename);
	CheckDlgButton (hDlg, IDC_AVIOUTPUT_FRAMELIMITER, avioutput_framelimiter ? FALSE : TRUE);
	CheckDlgButton (hDlg, IDC_AVIOUTPUT_NOSOUNDOUTPUT, avioutput_nosoundoutput ? TRUE : FALSE);
	CheckDlgButton (hDlg, IDC_AVIOUTPUT_NOSOUNDSYNC, avioutput_nosoundsync ? TRUE : FALSE);
	CheckDlgButton (hDlg, IDC_AVIOUTPUT_ORIGINALSIZE, avioutput_originalsize ? TRUE : FALSE);
	CheckDlgButton (hDlg, IDC_AVIOUTPUT_ACTIVATED, avioutput_requested ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton (hDlg, IDC_SCREENSHOT_ORIGINALSIZE, screenshot_originalsize ? TRUE : FALSE);
	CheckDlgButton (hDlg, IDC_SAMPLERIPPER_ACTIVATED, sampleripper_enabled ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton (hDlg, IDC_STATEREC_RECORD, input_record ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton (hDlg, IDC_STATEREC_PLAY, input_play ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton (hDlg, IDC_STATEREC_AUTOPLAY, workprefs.inprec_autoplay ? BST_CHECKED : BST_UNCHECKED);
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
	ew (hDlg, IDC_SAMPLERIPPER_ACTIVATED, full_property_sheet ? FALSE : TRUE);

	ew (hDlg, IDC_AVIOUTPUT_FILE, TRUE);

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
		_tcscpy (tmp, L"Wave (internal)");
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
	CheckDlgButton (hDlg, IDC_AVIOUTPUT_FRAMELIMITER, avioutput_framelimiter ? FALSE : TRUE);
	CheckDlgButton (hDlg, IDC_AVIOUTPUT_NOSOUNDOUTPUT, avioutput_nosoundoutput ? TRUE : FALSE);
	CheckDlgButton (hDlg, IDC_AVIOUTPUT_NOSOUNDSYNC, avioutput_nosoundsync ? TRUE : FALSE);

	ew (hDlg, IDC_AVIOUTPUT_ACTIVATED, (!avioutput_audio && !avioutput_video) ? FALSE : TRUE);

	CheckDlgButton (hDlg, IDC_STATEREC_RECORD, input_record ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton (hDlg, IDC_STATEREC_PLAY, input_play ? BST_CHECKED : BST_UNCHECKED);
	ew (hDlg, IDC_STATEREC_RECORD, !(input_play == INPREC_PLAY_NORMAL && full_property_sheet));
	ew (hDlg, IDC_STATEREC_SAVE, input_record == INPREC_RECORD_RERECORD || input_record == INPREC_RECORD_PLAYING);
	ew (hDlg, IDC_STATEREC_PLAY, input_record != INPREC_RECORD_RERECORD);
}

static INT_PTR CALLBACK AVIOutputDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;
	TCHAR tmp[1000];

	switch(msg)
	{
	case WM_INITDIALOG:
		pages[AVIOUTPUT_ID] = hDlg;
		currentpage = AVIOUTPUT_ID;
		AVIOutput_GetSettings ();
		regqueryint (NULL, L"Screenshot_Original", &screenshot_originalsize);
		enable_for_avioutputdlg (hDlg);
		if (!avioutput_filename[0]) {
			fetch_path (L"VideoPath", avioutput_filename, sizeof (avioutput_filename) / sizeof (TCHAR));
			_tcscat (avioutput_filename, L"output.avi");
		}
		SendDlgItemMessage (hDlg, IDC_STATEREC_RATE, CB_RESETCONTENT, 0, 0);
		SendDlgItemMessage (hDlg, IDC_STATEREC_RATE, CB_ADDSTRING, 0, (LPARAM)L"-");
		SendDlgItemMessage (hDlg, IDC_STATEREC_RATE, CB_ADDSTRING, 0, (LPARAM)L"1");
		SendDlgItemMessage (hDlg, IDC_STATEREC_RATE, CB_ADDSTRING, 0, (LPARAM)L"2");
		SendDlgItemMessage (hDlg, IDC_STATEREC_RATE, CB_ADDSTRING, 0, (LPARAM)L"5");
		SendDlgItemMessage (hDlg, IDC_STATEREC_RATE, CB_ADDSTRING, 0, (LPARAM)L"10");
		SendDlgItemMessage (hDlg, IDC_STATEREC_RATE, CB_ADDSTRING, 0, (LPARAM)L"20");
		SendDlgItemMessage (hDlg, IDC_STATEREC_RATE, CB_ADDSTRING, 0, (LPARAM)L"30");
		SendDlgItemMessage (hDlg, IDC_STATEREC_RATE, CB_ADDSTRING, 0, (LPARAM)L"60");
		SendDlgItemMessage (hDlg, IDC_STATEREC_RATE, CB_SETCURSEL, 0, 0);
		if (workprefs.statecapturerate > 0) {
			_stprintf (tmp, L"%d", workprefs.statecapturerate / 50);
			SendDlgItemMessage( hDlg, IDC_STATEREC_RATE, WM_SETTEXT, 0, (LPARAM)tmp);
		}

		SendDlgItemMessage (hDlg, IDC_STATEREC_BUFFERSIZE, CB_RESETCONTENT, 0, 0);
		SendDlgItemMessage (hDlg, IDC_STATEREC_BUFFERSIZE, CB_ADDSTRING, 0, (LPARAM)L"50");
		SendDlgItemMessage (hDlg, IDC_STATEREC_BUFFERSIZE, CB_ADDSTRING, 0, (LPARAM)L"100");
		SendDlgItemMessage (hDlg, IDC_STATEREC_BUFFERSIZE, CB_ADDSTRING, 0, (LPARAM)L"500");
		SendDlgItemMessage (hDlg, IDC_STATEREC_BUFFERSIZE, CB_ADDSTRING, 0, (LPARAM)L"1000");
		SendDlgItemMessage (hDlg, IDC_STATEREC_BUFFERSIZE, CB_ADDSTRING, 0, (LPARAM)L"10000");
		_stprintf (tmp, L"%d", workprefs.statecapturebuffersize);
		SendDlgItemMessage( hDlg, IDC_STATEREC_BUFFERSIZE, WM_SETTEXT, 0, (LPARAM)tmp);
		

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
			screenshot_originalsize = ischecked (hDlg, IDC_SCREENSHOT_ORIGINALSIZE) ? 1 : 0;
			regsetint (NULL, L"Screenshot_Original", screenshot_originalsize);
			break;
		case IDC_STATEREC_SAVE:
			if (input_record > INPREC_RECORD_NORMAL) {
				if (DiskSelection (hDlg, wParam, 16, &workprefs, NULL)) {
					TCHAR tmp[MAX_DPATH];
					_tcscpy (tmp, workprefs.inprecfile);
					_tcscat (tmp, L".uss");
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
				if (DiskSelection (hDlg, wParam, 15, &workprefs, NULL)) {
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
			sampleripper_enabled = !sampleripper_enabled;
			audio_sampleripper (-1);
			break;

		case IDC_AVIOUTPUT_ACTIVATED:
			avioutput_requested = !avioutput_requested;
			if (!avioutput_requested)
				AVIOutput_End ();
			break;
		case IDC_SCREENSHOT:
			screenshot(1, 0);
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
					if (avioutput_audio = AVIAUDIO_WAV)
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
				OPENFILENAME ofn;

				ZeroMemory (&ofn, sizeof (OPENFILENAME));
				ofn.lStructSize = sizeof (OPENFILENAME);
				ofn.hwndOwner = hDlg;
				ofn.hInstance = hInst;
				ofn.Flags = OFN_EXTENSIONDIFFERENT | OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
				ofn.lpstrCustomFilter = NULL;
				ofn.nMaxCustFilter = 0;
				ofn.nFilterIndex = 0;
				ofn.lpstrFile = avioutput_filename;
				ofn.nMaxFile = MAX_DPATH;
				ofn.lpstrFileTitle = NULL;
				ofn.nMaxFileTitle = 0;
				ofn.lpstrInitialDir = NULL;
				ofn.lpfnHook = NULL;
				ofn.lpTemplateName = NULL;
				ofn.lCustData = 0;
				ofn.lpstrFilter = L"Video Clip (*.avi)\0*.avi\0Wave Sound (*.wav)\0";

				if(!GetSaveFileName (&ofn))
					break;
				if (ofn.nFilterIndex == 2) {
					avioutput_audio = AVIAUDIO_WAV;
					avioutput_video = 0;
					if (_tcslen (avioutput_filename) > 4 && !_tcsicmp (avioutput_filename + _tcslen (avioutput_filename) - 4, L".avi"))
						_tcscpy (avioutput_filename + _tcslen (avioutput_filename) - 4, L".wav");
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

static int ignorewindows[] = {
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
	IDD_FILTER, IDC_FILTERPRESETS,
	-1,
	IDD_HARDDISK, IDC_VOLUMELIST,
	-1,
	IDD_INPUT, IDC_INPUTDEVICE, IDC_INPUTLIST, IDC_INPUTAMIGA,
	-1,
	IDD_KICKSTART, IDC_ROMFILE, IDC_ROMFILE2, IDC_CARTFILE, IDC_FLASHFILE,
	-1,
	IDD_LOADSAVE, IDC_CONFIGTREE, IDC_EDITNAME, IDC_EDITDESCRIPTION, IDC_CONFIGLINK, IDC_EDITPATH,
	-1,
	IDD_MISC1, IDC_LANGUAGE,
	-1,
	IDD_PATHS, IDC_PATHS_ROM, IDC_PATHS_CONFIG, IDC_PATHS_SCREENSHOT, IDC_PATHS_SAVESTATE, IDC_PATHS_AVIOUTPUT, IDC_PATHS_SAVEIMAGE, IDC_PATHS_RIP,
	-1,
	IDD_IOPORTS, IDC_PRINTERLIST, IDC_SAMPLERLIST, IDC_PS_PARAMS, IDC_SERIAL, IDC_MIDIOUTLIST, IDC_MIDIINLIST,
	-1,
	IDD_SOUND, IDC_SOUNDCARDLIST, IDC_SOUNDDRIVESELECT,
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
			*p++;
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
			*p++;
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

#define PANEL_X 174
#define PANEL_Y 12
#define PANEL_WIDTH 456
#define PANEL_HEIGHT 396

static HWND updatePanel (int id)
{
	HWND hDlg = guiDlg;
	static HWND hwndTT;
	RECT r1c, r1w, r2c, r2w, r3c, r3w;
	int w, h, pw, ph, x , y, i;
	int fullpanel;
	struct newresource *tres;

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
		DestroyWindow (panelDlg);
		panelDlg = NULL;
	}
	if (ToolTipHWND != NULL) {
		DestroyWindow (ToolTipHWND);
		ToolTipHWND = NULL;
	}
	for (i = 0; ToolTipHWNDS2[i].hwnd; i++) {
		DestroyWindow (ToolTipHWNDS2[i].hwnd);
		ToolTipHWNDS2[i].hwnd = NULL;
	}
	hAccelTable = NULL;
	if (id < 0) {
		if (isfullscreen () <= 0) {
			RECT r;
			if (GetWindowRect (hDlg, &r)) {
				LONG left, top;
				left = r.left;
				top = r.top;
				regsetint (NULL, L"GUIPosX", left);
				regsetint (NULL, L"GUIPosY", top);
			}
		}
		ew (hDlg, IDHELP, FALSE);
		return NULL;
	}

	fullpanel = ppage[id].fullpanel;
	GetWindowRect (GetDlgItem (hDlg, IDC_PANEL_FRAME), &r1w);
	GetClientRect (GetDlgItem (hDlg, IDC_PANEL_FRAME), &r1c);
	GetWindowRect (hDlg, &r2w);
	GetClientRect (hDlg, &r2c);
	gui_width = r2c.right;
	gui_height = r2c.bottom;
	tres = scaleresource (ppage[id].nres, hDlg);
	panelDlg = CreateDialogIndirectParam (tres->inst, tres->resource, hDlg, ppage[id].dlgproc, id);
	freescaleresource(tres);
	GetWindowRect (hDlg, &r3w);
	GetClientRect (panelDlg, &r3c);
	x = r1w.left - r2w.left;
	y = r1w.top - r2w.top;
	w = r3c.right - r3c.left + 1;
	h = r3c.bottom - r3c.top + 1;
	pw = r1w.right - r1w.left + 1;
	ph = r1w.bottom - r1w.top + 1;
	SetWindowPos (panelDlg, HWND_TOP, 0, 0, 0, 0,
		SWP_NOSIZE | SWP_NOOWNERZORDER);
	GetWindowRect (panelDlg, &r3w);
	GetClientRect (panelDlg, &r3c);
	x -= r3w.left - r2w.left - 1;
	y -= r3w.top - r2w.top - 1;
	if (!fullpanel) {
		SetWindowPos (panelDlg, HWND_TOP, x + (pw - w) / 2, y + (ph - h) / 2, 0, 0,
			SWP_NOSIZE | SWP_NOOWNERZORDER);
	}
	ShowWindow (GetDlgItem (hDlg, IDC_PANEL_FRAME), SW_HIDE);
	ShowWindow (GetDlgItem (hDlg, IDC_PANEL_FRAME_OUTER), !fullpanel ? SW_SHOW : SW_HIDE);
	ShowWindow (GetDlgItem (hDlg, IDC_PANELTREE), !fullpanel ? SW_SHOW : SW_HIDE);
	ShowWindow (panelDlg, SW_SHOW);
	ew (hDlg, IDHELP, pHtmlHelp && ppage[currentpage].help ? TRUE : FALSE);

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

	if (ppage[id].focusid > 0) {
		setfocus (panelDlg, ppage[id].focusid);
	}

	return panelDlg;
}

static HTREEITEM CreateFolderNode (HWND TVhDlg, int nameid, HTREEITEM parent, int nodeid, int sub)
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
	return TreeView_InsertItem (TVhDlg, &is);
}

static void CreateNode (HWND TVhDlg, int page, HTREEITEM parent)
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
}
#define CN(page) CreateNode(TVhDlg, page, p);

static void createTreeView (HWND hDlg, int currentpage)
{
	HWND TVhDlg;
	int i;
	HIMAGELIST himl;
	HTREEITEM p, root;

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
	TreeView_SetImageList (TVhDlg, himl, TVSIL_NORMAL);

	p = root = CreateFolderNode (TVhDlg, IDS_TREEVIEW_SETTINGS, NULL, ABOUT_ID, 0);
	CN (ABOUT_ID);
	CN (PATHS_ID);
	CN (QUICKSTART_ID);
	CN (LOADSAVE_ID);
#if FRONTEND == 1
	CN (FRONTEND_ID);
#endif

	p = CreateFolderNode (TVhDlg, IDS_TREEVIEW_HARDWARE, root, LOADSAVE_ID, CONFIG_TYPE_HARDWARE);
	CN (CPU_ID);
	CN (CHIPSET_ID);
	CN (CHIPSET2_ID);
	CN (KICKSTART_ID);
	CN (MEMORY_ID);
	CN (FLOPPY_ID);
	CN (HARDDISK_ID);
	CN (EXPANSION_ID);

	p = CreateFolderNode (TVhDlg, IDS_TREEVIEW_HOST, root, LOADSAVE_ID, CONFIG_TYPE_HOST);
	CN (DISPLAY_ID);
	CN (SOUND_ID);
	CN (GAMEPORTS_ID);
	CN (IOPORTS_ID);
	CN (INPUT_ID);
	CN (AVIOUTPUT_ID);
	CN (HW3D_ID);
	CN (DISK_ID);
	CN (MISC1_ID);
	CN (MISC2_ID);

	TreeView_SelectItem (TVhDlg, ppage[currentpage].tv);
}

static int dialog_x_offset, dialog_y_offset;

static void centerWindow (HWND hDlg)
{
	RECT rc, rcDlg, rcOwner;
	HWND owner = GetParent(hDlg);
	int x = 0, y = 0;
	POINT pt1, pt2;

	if (owner == NULL)
		owner = GetDesktopWindow ();
	if (isfullscreen () <= 0) {
		regqueryint (NULL, L"GUIPosX", &x);
		regqueryint (NULL, L"GUIPosY", &y);
	} else {
		GetWindowRect (owner, &rcOwner);
		GetWindowRect (hDlg, &rcDlg);
		CopyRect (&rc, &rcOwner);
		OffsetRect (&rcDlg, -rcDlg.left, -rcDlg.top);
		OffsetRect (&rc, -rc.left, -rc.top);
		OffsetRect (&rc, -rcDlg.right, -rcDlg.bottom);
		x = rcOwner.left + (rc.right / 2);
		y = rcOwner.top + (rc.bottom / 2);
	}
	SetForegroundWindow (hDlg);
	pt1.x = x;
	pt1.y = y;
	pt2.x = x + 16;
	pt2.y = y + GetSystemMetrics (SM_CYMENU) + GetSystemMetrics (SM_CYBORDER);
	if (MonitorFromPoint (pt1, MONITOR_DEFAULTTONULL) == NULL || MonitorFromPoint (pt2, MONITOR_DEFAULTTONULL) == NULL) {
		if (isfullscreen () > 0) {
			x = 0;
			y = 0;
		} else {
			x = 16;
			y = 16;
		}
	}
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
static int floppyslot_addfile (struct uae_prefs *prefs, const TCHAR *file, int drv, int firstdrv, int maxdrv)
{
	struct zdirectory *zd = zfile_opendir_archive (file, ZFD_ARCHIVE | ZFD_NORECURSE);
	if (zd && zfile_readdir_archive (zd, NULL, true) > 1) {
		TCHAR out[MAX_DPATH];
		while (zfile_readdir_archive (zd, out, true)) {
			struct zfile *zf = zfile_fopen (out, L"rb", ZFD_NORMAL);
			if (zf) {
				int type = zfile_gettype (zf);
				if (type == ZFILE_DISKIMAGE) {
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
	return drv;
}

static int do_filesys_insert (const TCHAR *root)
{
	if (filesys_insert (-1, NULL, root, 0, 0) == 0)
		return filesys_media_change (root, 2, NULL);
	return 1;
}

int dragdrop (HWND hDlg, HDROP hd, struct uae_prefs *prefs, int	currentpage)
{
	int cnt, i, drv, harddrive, drvdrag, firstdrv;
	TCHAR file[MAX_DPATH];
	int dfxtext[] = { IDC_DF0TEXT, IDC_DF0TEXTQ, IDC_DF1TEXT, IDC_DF1TEXTQ, IDC_DF2TEXT, -1, IDC_DF3TEXT, -1 };
	POINT pt;
	RECT r, r2;
	int ret = 0;
	DWORD flags;

	DragQueryPoint (hd, &pt);
	pt.y += GetSystemMetrics (SM_CYMENU) + GetSystemMetrics (SM_CYBORDER);
	cnt = DragQueryFile (hd, 0xffffffff, NULL, 0);
	if (!cnt)
		return 0;
	drv = harddrive = 0;
	drvdrag = 0;
	if (currentpage < 0) {
		GetClientRect (hMainWnd, &r2);
		if (hStatusWnd) {
			GetClientRect (hStatusWnd, &r);
			if (pt.y >= r2.bottom && pt.y < r2.bottom + r.bottom) {
				if (pt.x >= window_led_drives && pt.x < window_led_drives_end && window_led_drives > 0) {
					drv = pt.x - window_led_drives;
					drv /= (window_led_drives_end - window_led_drives) / 4;
					drvdrag = 1;
					if (drv < 0 || drv > 3)
						drv = 0;
				}
				if (pt.x >= window_led_hd && pt.x < window_led_hd_end && window_led_hd > 0) {
					harddrive = 1;
				}
			}
		}
	} else if (currentpage == FLOPPY_ID || currentpage == QUICKSTART_ID) {
		for (i = 0; i < 4; i++) {
			int id = dfxtext[i * 2 + (currentpage == QUICKSTART_ID ? 1 : 0)];
			if (workprefs.floppyslots[i].dfxtype >= 0 && id >= 0) {
				if (GetPanelRect (GetDlgItem (panelDlg, id), &r)) {
					if (PtInRect (&r, pt)) {
						drv = i;
						break;
					}
				}
			}
		}
	}
	firstdrv = drv;
	for (i = 0; i < cnt; i++) {
		struct romdata *rd = NULL;
		struct zfile *z;
		int type = -1, zip = 0;
		int mask;

		DragQueryFile (hd, i, file, sizeof (file) / sizeof (TCHAR));
		flags = GetFileAttributes (file);
		if (flags & FILE_ATTRIBUTE_DIRECTORY)
			type = ZFILE_HDF;
		if (harddrive)
			mask = ZFD_ALL;
		else
			mask = ZFD_NORMAL;
		if (type < 0) {
			if (currentpage < 0) {
				z = zfile_fopen (file, L"rb", 0);
				if (z) {
					zip = iszip (z);
					zfile_fclose (z);
				}
			}
			if (!zip) {
				z = zfile_fopen (file, L"rb", mask);
				if (z) {
					if (currentpage < 0 && iszip (z)) {
						zip = 1;
					} else {
						type = zfile_gettype (z);
						if (type == ZFILE_ROM)
							rd = getromdatabyzfile (z);
					}
					zfile_fclose (z);
					z = NULL;
				}
			}
		}

		if (customDlgType == IDD_HARDFILE) {
			_tcscpy (current_hfdlg.filename, file);
			SetDlgItemText (hDlg, IDC_PATH_NAME, current_hfdlg.filename);
			updatehdfinfo (customDlg, 1);
			continue;
		}

		if (drvdrag) {
			type = ZFILE_DISKIMAGE;
		} else if (zip || harddrive) {
			do_filesys_insert (file);
			continue;
		}

		switch (type)
		{
		case ZFILE_DISKIMAGE:
			if (currentpage == DISK_ID) {
				diskswapper_addfile (prefs, file);
			} else if (currentpage == HARDDISK_ID) {
				add_filesys_config (&workprefs, -1, NULL, L"", file, 0,
					0, 0, 0, 0, 0, NULL, 0, 0);
			} else {
				drv = floppyslot_addfile (prefs, file, drv, firstdrv, i);
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
				if (rd->type == ROMTYPE_AR)
					_tcscpy (prefs->cartfile, file);
			} else {
				_tcscpy (prefs->romfile, file);
			}
			break;
		case ZFILE_HDF:
			if (flags & FILE_ATTRIBUTE_DIRECTORY) {
				if (!full_property_sheet && currentpage < 0)
					do_filesys_insert (file);
				else
					add_filesys_config (&workprefs, -1, NULL, L"", file, 0,
					0, 0, 0, 0, 0, NULL, 0, 0);
			} else {
				add_filesys_config (&workprefs, -1, NULL, NULL, file, 0,
					32, 1, 2, 512, 0, NULL, 0, 0);
			}
			break;
		case ZFILE_HDFRDB:
			add_filesys_config (&workprefs, -1, NULL, NULL, file, 0,
				0, 0, 0, 512, 0, NULL, 0, 0);
			break;
		case ZFILE_NVR:
			_tcscpy (prefs->flashfile, file);
			break;
		case ZFILE_CONFIGURATION:
			if (target_cfgfile_load (&workprefs, file, 0, 0)) {
				if (full_property_sheet) {
					inputdevice_updateconfig (&workprefs);
					if (!workprefs.start_gui)
						ret = 1;
				} else {
					uae_restart (workprefs.start_gui, file);
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
			_tcscpy (workprefs.cdslots[0].name, file);
			break;
		default:
			if (currentpage < 0 && !full_property_sheet) {
				do_filesys_insert (file);
			} else if (currentpage == HARDDISK_ID) {
				add_filesys_config (&workprefs, -1, NULL, L"", file, 0,
					0, 0, 0, 0, 0, NULL, 0, 0);
				if (!full_property_sheet)
					do_filesys_insert (file);
			} else {
				rd = scan_arcadia_rom (file, 0);
				if (rd) {
					if (rd->type == ROMTYPE_ARCADIABIOS)
						_tcscpy (prefs->romextfile, file);
					else if (rd->type == ROMTYPE_ARCADIAGAME)
						_tcscpy (prefs->cartfile, file);
				}
			}
			break;
		}
	}
	DragFinish (hd);
	config_changed = 1;
	return ret;
}

static int dialogreturn;
static INT_PTR CALLBACK DialogProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;
	static int waitfornext;

	switch (msg)
	{
	case WM_DEVICECHANGE:
		{
			DEV_BROADCAST_HDR *pBHdr = (DEV_BROADCAST_HDR *)lParam;
			int doit = 0;
			if (wParam == DBT_DEVNODES_CHANGED && lParam == 0) {
				if (waitfornext)
					doit = 1;
			} else if (pBHdr && pBHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
				DEV_BROADCAST_DEVICEINTERFACE *dbd = (DEV_BROADCAST_DEVICEINTERFACE*)lParam;
				write_log (L"%s: %s\n", wParam == DBT_DEVICEREMOVECOMPLETE ? L"Removed" : L"Inserted",
					dbd->dbcc_name);
				if (wParam == DBT_DEVICEREMOVECOMPLETE)
					doit = 1;
				else if (wParam == DBT_DEVICEARRIVAL)
					waitfornext = 1; /* DirectInput enumeration does not yet show the new device.. */
			}
			if (doit) {
				inputdevice_devicechange (&workprefs);
				updatePanel (currentpage);
				waitfornext = 0;
			}
		}
		return TRUE;
	case WM_DESTROY:
		PostQuitMessage (0);
		return TRUE;
	case WM_CLOSE:
		addnotifications (hDlg, TRUE, TRUE);
		DestroyWindow(hDlg);
		if (dialogreturn < 0) {
			dialogreturn = 0;
			if (allow_quit) {
				quit_program = 1;
				regs.spcflags |= SPCFLAG_BRK;
			}
		}
		return TRUE;
	case WM_INITDIALOG:
		waitfornext = 0;
		guiDlg = hDlg;
		SendMessage (hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE(IDI_APPICON)));
		if (full_property_sheet) {
			TCHAR tmp[100];
			WIN32GUI_LoadUIString (IDS_STARTEMULATION, tmp, sizeof (tmp) / sizeof (TCHAR));
			SetWindowText (GetDlgItem (guiDlg, IDOK), tmp);
		}
		ShowWindow (GetDlgItem (guiDlg, IDC_RESTARTEMU), full_property_sheet ? SW_HIDE : SW_SHOW);
		centerWindow (hDlg);
		createTreeView (hDlg, currentpage);
		updatePanel (currentpage);
		addnotifications (hDlg, FALSE, TRUE);
		return TRUE;
	case WM_DROPFILES:
		if (dragdrop (hDlg, (HDROP)wParam, (gui_active || full_property_sheet) ? &workprefs : &changed_prefs, currentpage))
			SendMessage (hDlg, WM_COMMAND, IDOK, 0);
		updatePanel (currentpage);
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
						updatePanel (cp);
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
			case IDC_RESETAMIGA:
				uae_reset (1);
				SendMessage (hDlg, WM_COMMAND, IDOK, 0);
				return TRUE;
			case IDC_QUITEMU:
				uae_quit ();
				SendMessage (hDlg, WM_COMMAND, IDCANCEL, 0);
				return TRUE;
			case IDC_RESTARTEMU:
				uae_restart (-1, NULL);
				exit_gui (1);
				return TRUE;
			case IDHELP:
				if (pHtmlHelp && ppage[currentpage].help)
					HtmlHelp (NULL, help_file, HH_DISPLAY_TOPIC, ppage[currentpage].help);
				return TRUE;
			case IDOK:
				updatePanel (-1);
				dialogreturn = 1;
				DestroyWindow (hDlg);
				gui_to_prefs ();
				guiDlg = NULL;
				return TRUE;
			case IDCANCEL:
				updatePanel (-1);
				dialogreturn = 0;
				DestroyWindow (hDlg);
				if (allow_quit) {
					quit_program = 1;
					regs.spcflags |= SPCFLAG_BRK;
				}
				guiDlg = NULL;
				return TRUE;
			}
			break;
		}
	}
	handlerawinput (hDlg, msg, wParam, lParam);
	return FALSE;
}

static ACCEL EmptyAccel[] = {
	{ FVIRTKEY, VK_UP, 20001 }, { FVIRTKEY, VK_DOWN, 20002 },
	{ 0, 0, 0 }
};

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

	_stprintf (rid, L"#%d", tmpl);
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
	newres = (LPCDLGTEMPLATEW)xmalloc (uae_u8, size);
	if (!newres)
		return NULL;
	memcpy ((void*)newres, resdata, size);
	nr->resource = newres;
	nr->size = size;
	nr->tmpl = tmpl;
	nr->inst = inst;
	return nr;
}

INT_PTR CustomDialogBox (int templ, HWND hDlg, DLGPROC proc)
{
	struct newresource *res, *r;
	INT_PTR h = -1;

	res = getresource (templ);
	if (!res)
		return h;
	r = scaleresource (res, hDlg);
	if (r) {
		h = DialogBoxIndirect (r->inst, r->resource, hDlg, proc);
		freescaleresource (r);
	}
	customDlgType = 0;
	customDlg = NULL;
	freescaleresource (res);
	return h;
}

HWND CustomCreateDialog (int templ, HWND hDlg, DLGPROC proc)
{
	struct newresource *res, *r;
	HWND h = NULL;

	res = getresource (templ);
	if (!res)
		return h;
	r = scaleresource (res, hDlg);
	if (r) {
		h = CreateDialogIndirect (r->inst, r->resource, hDlg, proc);
		freescaleresource (r);
	}
	freescaleresource (res);
	return h;
}

static int init_page (int tmpl, int icon, int title,
	INT_PTR (CALLBACK FAR *func) (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam), ACCEL *accels, const TCHAR *help, int focusid)
{
	LPTSTR lpstrTitle;
	static int id = 0;
	int i = -1;
	struct newresource *res;

	res = getresource (tmpl);
	if (!res) {
		write_log (L"init_page(%d) failed\n", tmpl);
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
	if (!accels)
		accels = EmptyAccel;
	while (accels[++i].key);
	ppage[id].accel = CreateAcceleratorTable (accels, i);
	if (tmpl == IDD_FRONTEND)
		ppage[id].fullpanel = TRUE;
	id++;
	return id - 1;
}

static RECT dialog_rect;

static void dialogmousemove (HWND hDlg)
{
	static int newmx, newmy;
	RECT rc;
	POINT pt;
	static POINT pt2;
	int dx, dy;
	int sw, sh;
	int xstart, ystart;
	MONITORINFOEX pmi;

	if (full_property_sheet || isfullscreen () <= 0)
		return;
	pmi.cbSize = sizeof (pmi);
	GetMonitorInfo (MonitorFromWindow (hAmigaWnd, MONITOR_DEFAULTTOPRIMARY), (LPMONITORINFO)&pmi);
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
	char *str1 = "äöå€õãñëÿüïñç";
	TCHAR *str2 = L"äöå€õãñëÿüïñç";
	TCHAR *s1;
	char *s2;
	MessageBoxA(NULL, str1, "Test1 ANSI", MB_OK);
	s1 = au (str1);
	MessageBoxW(NULL, s1, L"Test1 UNICODE", MB_OK);

	MessageBoxW(NULL, str2, L"Test2 UNICODE", MB_OK);
	s2 = ua (str2);
	MessageBoxA(NULL, s2, "Test2 ANSI", MB_OK);
}
#endif

static int GetSettings (int all_options, HWND hwnd)
{
	static int init_called = 0;
	int psresult;
	HWND dhwnd;
	int first = 0;
	static struct newresource *panelresource;
	struct newresource *tres;

	gui_active++;

	full_property_sheet = all_options;
	allow_quit = all_options;
	pguiprefs = &currprefs;
	memset (&workprefs, 0, sizeof (struct uae_prefs));
	default_prefs (&workprefs, 0);

	szNone = WIN32GUI_LoadUIString (IDS_NONE);
	prefs_to_gui (&changed_prefs);

	if (!init_called) {
		first = 1;
		panelresource = getresource (IDD_PANEL);
		LOADSAVE_ID = init_page (IDD_LOADSAVE, IDI_FILE, IDS_LOADSAVE, LoadSaveDlgProc, NULL, L"gui/configurations.htm", IDC_CONFIGTREE);
		MEMORY_ID = init_page (IDD_MEMORY, IDI_MEMORY, IDS_MEMORY, MemoryDlgProc, NULL, L"gui/ram.htm", 0);
		EXPANSION_ID = init_page (IDD_EXPANSION, IDI_EXPANSION, IDS_EXPANSION, ExpansionDlgProc, NULL, L"gui/expansion.htm", 0);
		KICKSTART_ID = init_page (IDD_KICKSTART, IDI_MEMORY, IDS_KICKSTART, KickstartDlgProc, NULL, L"gui/rom.htm", 0);
		CPU_ID = init_page (IDD_CPU, IDI_CPU, IDS_CPU, CPUDlgProc, NULL, L"gui/cpu.htm", 0);
		DISPLAY_ID = init_page (IDD_DISPLAY, IDI_DISPLAY, IDS_DISPLAY, DisplayDlgProc, NULL, L"gui/display.htm", 0);
#if defined (GFXFILTER)
		HW3D_ID = init_page (IDD_FILTER, IDI_DISPLAY, IDS_FILTER, hw3dDlgProc, NULL, L"gui/filter.htm", 0);
#endif
		CHIPSET_ID = init_page (IDD_CHIPSET, IDI_CPU, IDS_CHIPSET, ChipsetDlgProc, NULL, L"gui/chipset.htm", 0);
		CHIPSET2_ID = init_page (IDD_CHIPSET2, IDI_CPU, IDS_CHIPSET2, ChipsetDlgProc2, NULL, L"gui/chipset.htm", 0);
		SOUND_ID = init_page (IDD_SOUND, IDI_SOUND, IDS_SOUND, SoundDlgProc, NULL, L"gui/sound.htm", 0);
		FLOPPY_ID = init_page (IDD_FLOPPY, IDI_FLOPPY, IDS_FLOPPY, FloppyDlgProc, NULL, L"gui/floppies.htm", 0);
		DISK_ID = init_page (IDD_DISK, IDI_FLOPPY, IDS_DISK, SwapperDlgProc, SwapperAccel, L"gui/disk.htm", IDC_DISKLIST);
#ifdef FILESYS
		HARDDISK_ID = init_page (IDD_HARDDISK, IDI_HARDDISK, IDS_HARDDISK, HarddiskDlgProc, HarddiskAccel, L"gui/hard-drives.htm", 0);
#endif
		GAMEPORTS_ID = init_page (IDD_GAMEPORTS, IDI_GAMEPORTS, IDS_GAMEPORTS, GamePortsDlgProc, NULL, L"gui/gameports.htm", 0);
		INPUTMAP_ID = init_page (IDD_INPUTMAP, IDI_GAMEPORTS, IDS_GAMEPORTS, InputMapDlgProc, NULL, NULL, IDC_INPUTMAPLIST);
		IOPORTS_ID = init_page (IDD_IOPORTS, IDI_PORTS, IDS_IOPORTS, IOPortsDlgProc, NULL, L"gui/ioports.htm", 0);
		INPUT_ID = init_page (IDD_INPUT, IDI_INPUT, IDS_INPUT, InputDlgProc, NULL, L"gui/input.htm", IDC_INPUTLIST);
		MISC1_ID = init_page (IDD_MISC1, IDI_MISC1, IDS_MISC1, MiscDlgProc1, NULL, L"gui/misc.htm", 0);
		MISC2_ID = init_page (IDD_MISC2, IDI_MISC2, IDS_MISC2, MiscDlgProc2, NULL, L"gui/misc2.htm", 0);
#ifdef AVIOUTPUT
		AVIOUTPUT_ID = init_page (IDD_AVIOUTPUT, IDI_AVIOUTPUT, IDS_AVIOUTPUT, AVIOutputDlgProc, NULL, L"gui/output.htm", 0);
#endif
		PATHS_ID = init_page (IDD_PATHS, IDI_PATHS, IDS_PATHS, PathsDlgProc, NULL, L"gui/paths.htm", 0);
		QUICKSTART_ID = init_page (IDD_QUICKSTART, IDI_QUICKSTART, IDS_QUICKSTART, QuickstartDlgProc, NULL, L"gui/quickstart.htm", 0);
		ABOUT_ID = init_page (IDD_ABOUT, IDI_ABOUT, IDS_ABOUT, AboutDlgProc, NULL, NULL, 0);
		FRONTEND_ID = init_page (IDD_FRONTEND, IDI_QUICKSTART, IDS_FRONTEND, AboutDlgProc, NULL, NULL, 0);
		C_PAGES = FRONTEND_ID + 1;
		init_called = 1;
		if (quickstart && !qs_override)
			currentpage = QUICKSTART_ID;
		else
			currentpage = LOADSAVE_ID;
	}

	if (all_options || !configstore)
		CreateConfigStore (NULL, FALSE);

	dialogreturn = -1;
	hAccelTable = NULL;
	DragAcceptFiles (hwnd, TRUE);
	if (first)
		write_log (L"Entering GUI idle loop\n");

	scaleresource_setmaxsize (800, 600);
	tres = scaleresource (panelresource, hwnd);
	dhwnd = CreateDialogIndirect (tres->inst, tres->resource, hwnd, DialogProc);
	dialog_rect.top = dialog_rect.left = 0;
	dialog_rect.right = tres->width;
	dialog_rect.bottom = tres->height;
	freescaleresource(tres);
	psresult = 0;
	if (dhwnd != NULL) {
		MSG msg;
		DWORD v;

		setguititle (dhwnd);
		ShowWindow (dhwnd, SW_SHOW);
		MapDialogRect (dhwnd, &dialog_rect);

		hGUIWnd = dhwnd;

		for (;;) {
			HANDLE IPChandle;
			IPChandle = geteventhandleIPC (globalipc);
			if (globalipc && IPChandle != INVALID_HANDLE_VALUE) {
				MsgWaitForMultipleObjects (1, &IPChandle, FALSE, INFINITE, QS_ALLINPUT);
				while (checkIPC (globalipc, &workprefs));
			} else {
				WaitMessage();
			}
			dialogmousemove (dhwnd);
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
		}
		psresult = dialogreturn;
	}

	hGUIWnd = NULL;
	if (quit_program)
		psresult = -2;
	else if (qs_request_reset && quickstart)
		uae_reset (qs_request_reset == 2 ? 1 : 0);

	qs_request_reset = 0;
	full_property_sheet = 0;
	gui_active--;
	return psresult;
}

int gui_init (void)
{
	int ret;

	read_rom_list ();
	inputdevice_updateconfig (&workprefs);
	for (;;) {
		ret = GetSettings (1, currprefs.win32_notaskbarbutton ? hHiddenWnd : NULL);
		if (!restart_requested)
			break;
		restart_requested = 0;
	}
	if (ret > 0) {
#ifdef AVIOUTPUT
		AVIOutput_Begin ();
#endif
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

extern HWND hStatusWnd;

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
	uae_u8 old;
	uae_u8 *p;

	if (led == LED_HD)
		p = &gui_data.hd;
	else if (led == LED_CD)
		p = &gui_data.cd;
	else if (led == LED_MD)
		p = &gui_data.md;
	else
		return;
	old = *p;
	if (status == 0) {
		resetcounter[led]--;
		if (resetcounter[led] > 0)
			return;
	}
#ifdef RETROPLATFORM
	if (led == LED_HD)
		rp_hd_activity (unitnum, status ? 1 : 0, status == 2 ? 1 : 0);
	else if (led == LED_CD)
		rp_cd_activity (unitnum, status);
#endif
	*p = status;
	resetcounter[led] = 6;
	if (old != *p)
		gui_led (led, *p);
}

void gui_flicker_led (int led, int unitnum, int status)
{
	if (led < 0) {
		gui_flicker_led2 (LED_HD, 0, 0);
		gui_flicker_led2 (LED_CD, 0, 0);
		gui_flicker_led2 (LED_MD, 0, 0);
	} else {
		gui_flicker_led2 (led, unitnum, status);
	}
}

void gui_fps (int fps, int idle)
{
	gui_data.fps = fps;
	gui_data.idle = idle;
	gui_led (LED_FPS, 0);
	gui_led (LED_CPU, 0);
	gui_led (LED_SND, (gui_data.sndbuf_status > 1 || gui_data.sndbuf_status < 0) ? 0 : 1);
}

void gui_led (int led, int on)
{
	WORD type;
	static TCHAR drive_text[NUM_LEDS * 16];
	static TCHAR dfx[4][300];
	TCHAR *ptr, *tt, *p;
	int pos = -1, j;
	int writing = 0, playing = 0, active2 = 0;
	int center = 0;

	indicator_leds (led, on);
#ifdef LOGITECHLCD
	lcd_update (led, on);
#endif
#ifdef RETROPLATFORM
	if (led >= LED_DF0 && led <= LED_DF3 && !gui_data.drive_disabled[led - LED_DF0]) {
		rp_floppy_track (led - LED_DF0, gui_data.drive_track[led - LED_DF0]);
		writing = gui_data.drive_writing[led - LED_DF0];
	}
	rp_update_leds (led, on, writing);
#endif
	if (!hStatusWnd)
		return;
	tt = NULL;
	if (led >= LED_DF0 && led <= LED_DF3) {
		pos = 6 + (led - LED_DF0);
		ptr = drive_text + pos * 16;
		if (gui_data.drive_disabled[led - 1])
			_tcscpy (ptr, L"");
		else
			_stprintf (ptr , L"%02d", gui_data.drive_track[led - 1]);
		p = gui_data.df[led - 1];
		j = _tcslen (p) - 1;
		if (j < 0)
			j = 0;
		while (j > 0) {
			if (p[j - 1] == '\\' || p[j - 1] == '/')
				break;
			j--;
		}
		tt = dfx[led - 1];
		tt[0] = 0;
		if (_tcslen (p + j) > 0)
			_stprintf (tt, L"%s [CRC=%08X]", p + j, gui_data.crc32[led - 1]);
		center = 1;
		if (gui_data.drive_writing[led - 1])
			writing = 1;
	} else if (led == LED_POWER) {
		pos = 3;
		ptr = _tcscpy (drive_text + pos * 16, L"Power");
		center = 1;
	} else if (led == LED_HD) {
		pos = 4;
		ptr = _tcscpy (drive_text + pos * 16, L"HD");
		center = 1;
		if (on > 1)
			writing = 1;
	} else if (led == LED_CD) {
		pos = 5;
		ptr = _tcscpy (drive_text + pos * 16, L"CD");
		center = 1;
		if (on & LED_CD_AUDIO)
			playing = 1;
		else if (on & LED_CD_ACTIVE2)
			active2 = 1;
		on &= 1;
	} else if (led == LED_FPS) {
		double fps = (double)gui_data.fps / 10.0;
		extern int p96vblank;
		pos = 2;
		ptr = drive_text + pos * 16;
		if (fps > 999.9)
			fps = 999.9;
		if (picasso_on)
			_stprintf (ptr, L"%d [%.1f]", p96vblank, fps);
		else
			_stprintf (ptr, L"FPS: %.1f", fps);
		if (pause_emulation) {
			_tcscpy (ptr, L"PAUSED");
			center = 1;
		}
		on = 1;
	} else if (led == LED_CPU) {
		pos = 1;
		ptr = drive_text + pos * 16;
		_stprintf (ptr, L"CPU: %.0f%%", (double)((gui_data.idle) / 10.0));
		if (pause_emulation)
			on = 0;
		else
			on = 1;
	} else if (led == LED_SND && gui_data.drive_disabled[3]) {
		pos = 0;
		ptr = drive_text + pos * 16;
		if (gui_data.sndbuf_status < 3 && !pause_emulation) {
			_stprintf (ptr, L"SND: %+.0f%%", (double)((gui_data.sndbuf) / 10.0));
		} else {
			_tcscpy (ptr, L"SND: -");
			center = 1;
			on = 0;
		}
	} else if (led == LED_MD) {
		pos = 6 + 3;
		ptr = _tcscpy (drive_text + pos * 16, L"NV");
	}

	type = SBT_OWNERDRAW;
	if (pos >= 0) {
		ptr[_tcslen (ptr) + 1] = 0;
		if (center)
			ptr[_tcslen (ptr) + 1] |= 1;
		if (on) {
			ptr[_tcslen (ptr) + 1] |= 2;
			type |= SBT_POPOUT;
		}
		if (writing)
			ptr[_tcslen (ptr) + 1] |= 4;
		if (playing)
			ptr[_tcslen (ptr) + 1] |= 8;
		if (active2)
			ptr[_tcslen (ptr) + 1] |= 16;
		pos += window_led_joy_start;
		PostMessage (hStatusWnd, SB_SETTEXT, (WPARAM)((pos + 1) | type), (LPARAM)ptr);
		if (tt != NULL)
			PostMessage (hStatusWnd, SB_SETTIPTEXT, (WPARAM)(pos + 1), (LPARAM)tt);
	}
}

void gui_filename (int num, const TCHAR *name)
{
}


static int fsdialog (HWND *hwnd, DWORD *flags)
{
	HRESULT hr;

	if (gui_active) {
		*hwnd = guiDlg;
		*flags |= MB_SETFOREGROUND;
		return 0;
	}
	*hwnd = hAmigaWnd;
	if (isfullscreen () <= 0)
		return 0;
	hr = DirectDraw_FlipToGDISurface ();
	if (FAILED (hr))
		write_log (L"FlipToGDISurface failed, %s\n", DXError (hr));
	*flags |= MB_SETFOREGROUND;
	*flags |= MB_TOPMOST;
	return 0;
	/*
	HRESULT hr;
	hr = DirectDraw_FlipToGDISurface();
	if (FAILED(hr)) {
	write_log (L"FlipToGDISurface failed, %s\n", DXError (hr));
	return 0;
	}
	*hwnd = NULL;
	return 1;
	*/
}

int gui_message_multibutton (int flags, const TCHAR *format,...)
{
	TCHAR msg[2048];
	TCHAR szTitle[MAX_DPATH];
	va_list parms;
	int flipflop = 0;
	int fullscreen = 0;
	int focuso = isfocus ();
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
			ShowWindow (hAmigaWnd, SW_MINIMIZE);
	}

	va_start (parms, format);
	_vsntprintf (msg, sizeof msg / sizeof (TCHAR), format, parms);
	va_end (parms);
	write_log (msg);
	if (msg[_tcslen (msg) - 1]!='\n')
		write_log (L"\n");

	WIN32GUI_LoadUIString (IDS_ERRORTITLE, szTitle, MAX_DPATH);

	ret = MessageBox (hwnd, msg, szTitle, mbflags);

	if (!gui_active) {
		if (flipflop)
			ShowWindow (hAmigaWnd, SW_RESTORE);
		resume_sound ();
		setmouseactive (focuso > 0 ? 1 : 0);
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
	TCHAR msg[2048];
	TCHAR szTitle[MAX_DPATH];
	va_list parms;
	int flipflop = 0;
	int fullscreen = 0;
	int focuso = isfocus ();
	DWORD flags = MB_OK | MB_TASKMODAL;
	HWND hwnd;

	va_start (parms, format);
	_vsntprintf (msg, sizeof msg / sizeof (TCHAR), format, parms);
	va_end (parms);

	if (full_property_sheet) {
		pre_gui_message (msg);
		return;
	}

	flipflop = fsdialog (&hwnd, &flags);
	if (!gui_active) {
		pause_sound ();
		if (flipflop)
			ShowWindow (hAmigaWnd, SW_MINIMIZE);
	}

	write_log (msg);
	if (msg[_tcslen (msg) - 1] != '\n')
		write_log (L"\n");

	WIN32GUI_LoadUIString (IDS_ERRORTITLE, szTitle, MAX_DPATH);

	if (!MessageBox (hwnd, msg, szTitle, flags))
		write_log (L"MessageBox(%s) failed, err=%d\n", msg, GetLastError ());

	if (!gui_active) {
		if (flipflop)
			ShowWindow (hAmigaWnd, SW_RESTORE);
		resume_sound ();
		setmouseactive (focuso > 0 ? 1 : 0);
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
		write_log (L"\n");

	WIN32GUI_LoadUIString (IDS_ERRORTITLE, szTitle, MAX_DPATH);
	_tcscat (szTitle, BetaStr);
	MessageBox (guiDlg, msg, szTitle, MB_OK | MB_TASKMODAL | MB_SETFOREGROUND);

}

static int transla[] = {
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
