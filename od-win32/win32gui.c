/*==========================================================================
 *
 *  Copyright (C) 1996 Brian King
 *
 *  File:       win32gui.c
 *  Content:    Win32-specific gui features for UAE port.
 *
 ***************************************************************************/

#define FRONTEND 0

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

#include "config.h"
#include "resource.h"
#include "sysconfig.h"
#include "sysdeps.h"
#include "gui.h"
#include "options.h"
#include "memory.h"
#include "custom.h"
#include "events.h"
#include "newcpu.h"
#include "disk.h"
#include "uae.h"
#include "threaddep/thread.h"
#include "filesys.h"
#include "autoconf.h"
#include "inputdevice.h"
#include "xwin.h"
#include "keyboard.h"
#include "zfile.h"
#include "parallel.h"
#include "audio.h"

#include "dxwrap.h"
#include "win32.h"
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
#include "gfxfilter.h"
#include "driveclick.h"
#ifdef PROWIZARD
#include "moduleripper.h"
#endif
#include "catweasel.h"
#include "lcd.h"
#include "uaeipc.h"

#define DISK_FORMAT_STRING "(*.adf;*.adz;*.gz;*.dms;*.fdi;*.ipf;*.exe)\0*.adf;*.adz;*.gz;*.dms;*.fdi;*.ipf;*.zip;*.rar;*.7z;*.exe;*.ima\0"
#define ROM_FORMAT_STRING "(*.rom;*.roz)\0*.rom;*.zip;*.rar;*.7z;*.roz\0"
#define USS_FORMAT_STRING_RESTORE "(*.uss)\0*.uss;*.gz;*.zip;*.rar;*.7z\0"
#define USS_FORMAT_STRING_SAVE "(*.uss)\0*.uss\0"
#define HDF_FORMAT_STRING "(*.hdf;*.rdf;*.hdz;*.rdz)\0*.hdf;*.rdf;*.hdz;*.rdz\0"
#define INP_FORMAT_STRING "(*.inp)\0*.inp\0"
#define CONFIG_HOST "Host"
#define CONFIG_HARDWARE "Hardware"

static int allow_quit;
static int restart_requested;
static int full_property_sheet = 1;
static struct uae_prefs *pguiprefs;
struct uae_prefs workprefs;
static int currentpage;
static int qs_request_reset;
static int qs_override;
int gui_active;

extern HWND (WINAPI *pHtmlHelp)(HWND, LPCSTR, UINT, LPDWORD );

#undef HtmlHelp
#ifndef HH_DISPLAY_TOPIC
#define HH_DISPLAY_TOPIC 0
#endif
#define HtmlHelp(a,b,c,d) if( pHtmlHelp ) (*pHtmlHelp)(a,b,c,(LPDWORD)d); else \
{ char szMessage[MAX_DPATH]; WIN32GUI_LoadUIString( IDS_NOHELP, szMessage, MAX_DPATH ); gui_message( szMessage ); }

extern HWND hAmigaWnd;
extern char help_file[MAX_DPATH];

extern int mouseactive;

static char config_filename[ MAX_DPATH ] = "";

#define Error(x) MessageBox( NULL, (x), "WinUAE Error", MB_OK )

void WIN32GUI_LoadUIString( DWORD id, char *string, DWORD dwStringLen )
{
    if( LoadString( hUIDLL ? hUIDLL : hInst, id, string, dwStringLen ) == 0 )
	LoadString( hInst, id, string, dwStringLen );
}

static int C_PAGES;
#define MAX_C_PAGES 30
static int LOADSAVE_ID = -1, MEMORY_ID = -1, KICKSTART_ID = -1, CPU_ID = -1,
    DISPLAY_ID = -1, HW3D_ID = -1, CHIPSET_ID = -1, SOUND_ID = -1, FLOPPY_ID = -1, DISK_ID = -1,
    HARDDISK_ID = -1, PORTS_ID = -1, INPUT_ID = -1, MISC1_ID = -1, MISC2_ID = -1, AVIOUTPUT_ID = -1,
    PATHS_ID = -1, QUICKSTART_ID = -1, ABOUT_ID = -1, FRONTEND_ID = -1;
static HWND pages[MAX_C_PAGES];
#define MAX_IMAGETOOLTIPS 10
static HWND guiDlg, panelDlg, ToolTipHWND;
static HACCEL hAccelTable;
struct ToolTipHWNDS {
    WNDPROC proc;
    HWND hwnd;
    int imageid;
};
static struct ToolTipHWNDS ToolTipHWNDS2[MAX_IMAGETOOLTIPS + 1];

void write_disk_history (void)
{
    int i, j;
    char tmp[16];
    HKEY fkey;

    if (!hWinUAEKey)
	return;
    RegCreateKeyEx(hWinUAEKey , "DiskImageMRUList", 0, NULL, REG_OPTION_NON_VOLATILE,
	KEY_READ | KEY_WRITE, NULL, &fkey, NULL);
    if (fkey == NULL)
	return;
    j = 1;
    for (i = 0; i <= MAX_PREVIOUS_FLOPPIES; i++) {
	char *s = DISK_history_get(i);
	if (s == 0 || strlen(s) == 0)
	    continue;
	sprintf (tmp, "Image%02d", j);
	RegSetValueEx (fkey, tmp, 0, REG_SZ, (CONST BYTE *)s, strlen(s) + 1);
	j++;
   }
    while (j <= MAX_PREVIOUS_FLOPPIES) {
	char *s = "";
	sprintf (tmp, "Image%02d", j);
	RegSetValueEx (fkey, tmp, 0, REG_SZ, (CONST BYTE *)s, strlen(s) + 1);
	j++;
    }
    RegCloseKey(fkey);
}

void reset_disk_history (void)
{
    int i;

    for (i = 0; i < MAX_PREVIOUS_FLOPPIES; i++)
	DISK_history_add (NULL, i);
    write_disk_history();
}

HKEY read_disk_history (void)
{
    static int regread;
    char tmp2[1000];
    DWORD size2;
    int idx, idx2;
    HKEY fkey;
    char tmp[1000];
    DWORD size;

    if (!hWinUAEKey)
	return NULL;
    RegCreateKeyEx(hWinUAEKey , "DiskImageMRUList", 0, NULL, REG_OPTION_NON_VOLATILE,
	KEY_READ | KEY_WRITE, NULL, &fkey, NULL);
    if (fkey == NULL || regread)
	return fkey;

    idx = 0;
    for (;;) {
	int err;
	size = sizeof (tmp);
	size2 = sizeof (tmp2);
	err = RegEnumValue (fkey, idx, tmp, &size, NULL, NULL, tmp2, &size2);
	if (err != ERROR_SUCCESS)
	    break;
	if (strlen (tmp) == 7) {
	    idx2 = atol (tmp + 5) - 1;
	    if (idx2 >= 0)
		DISK_history_add (tmp2, idx2);
	}
	idx++;
    }
    regread = 1;
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

static int getcbn (HWND hDlg, int v, char *out, int len)
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
	    hTempImageList = ImageList_Merge(hDragImageList, 
			     0, hOneImageList, 0, 0, iHeight);
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

    ImageList_DragEnter(GetDesktopWindow(), pt.x, pt.y);

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
    *draggeditems = xmalloc (sizeof (int) * (cnt + 1));
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
#define MAX_CHIP_MEM 5
#define MIN_FAST_MEM 0
#define MAX_FAST_MEM 4
#define MIN_SLOW_MEM 0
#define MAX_SLOW_MEM 4
#define MIN_Z3_MEM 0
#if defined(WIN64)
#define MAX_Z3_MEM 12
#else
#define MAX_Z3_MEM 10
#endif
#define MIN_P96_MEM 0
#define MAX_P96_MEM 6
#define MIN_M68K_PRIORITY 1
#define MAX_M68K_PRIORITY 16
#define MIN_CACHE_SIZE 0
#define MAX_CACHE_SIZE 8
#define MIN_REFRESH_RATE 1
#define MAX_REFRESH_RATE 10
#define MIN_SOUND_MEM 0
#define MAX_SOUND_MEM 6

static char szNone[ MAX_DPATH ] = "None";

struct romscandata {
    uae_u8 *keybuf;
    int keysize;
    HKEY fkey;
    int got;
};

static struct romdata *scan_single_rom_2 (struct zfile *f, uae_u8 *keybuf, int keysize)
{
    uae_u8 buffer[20] = { 0 };
    uae_u8 *rombuf;
    int cl = 0, size;
    struct romdata *rd = 0;

    zfile_fseek (f, 0, SEEK_END);
    size = zfile_ftell (f);
    zfile_fseek (f, 0, SEEK_SET);
    if (size > 600000)
	return 0;
    zfile_fread (buffer, 1, 11, f);
    if (!memcmp (buffer, "AMIROMTYPE1", 11)) {
	cl = 1;
	if (keybuf == 0)
	    cl = -1;
	size -= 11;
    } else {
	zfile_fseek (f, 0, SEEK_SET);
    }
    rombuf = xcalloc (size, 1);
    if (!rombuf)
	return 0;
    zfile_fread (rombuf, 1, size, f);
    if (cl > 0) {
	decode_cloanto_rom_do (rombuf, size, size, keybuf, keysize);
	cl = 0;
    }
    if (!cl)
	rd = getromdatabydata (rombuf, size);
    free (rombuf);
    return rd;
}

static struct romdata *scan_single_rom (char *path, uae_u8 *keybuf, int keysize)
{
    struct zfile *z = zfile_fopen (path, "rb");
    if (!z)
	return 0;
    return scan_single_rom_2 (z, keybuf, keysize);
}

static void addrom (HKEY fkey, struct romdata *rd, char *name)
{
    char tmp[MAX_DPATH];
    sprintf (tmp, "ROM%02d", rd->id);
    RegSetValueEx (fkey, tmp, 0, REG_SZ, (CONST BYTE *)name, strlen (name) + 1);
}

static int scan_rom_2 (struct zfile *f, struct romscandata *rsd)
{
    struct romdata *rd = scan_single_rom_2 (f, rsd->keybuf, rsd->keysize);
    if (rd) {
	addrom (rsd->fkey, rd, zfile_getname (f));
	rsd->got = 1;
    }
    return 1;
}

static int scan_rom (char *path, HKEY fkey, uae_u8 *keybuf, int keysize)
{
    struct romscandata rsd = { keybuf, keysize, fkey, 0 };
    struct romdata *rd;

    rd = getarcadiarombyname (path);
    if (rd) {
	addrom (fkey, rd, path);
	return 1;
    }

    zfile_zopen (path, scan_rom_2, &rsd);
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
    char *p;
    int roms[6], ok;
    char unavail[MAX_DPATH], avail[MAX_DPATH], tmp1[MAX_DPATH];
    char *p1, *p2;
    
    WIN32GUI_LoadUIString (IDS_ROM_AVAILABLE, avail, sizeof (avail));
    WIN32GUI_LoadUIString (IDS_ROM_UNAVAILABLE, unavail, sizeof (avail));
    strcat (avail, "\n");
    strcat (unavail, "\n");
    WIN32GUI_LoadUIString (IDS_QS_MODELS, tmp1, sizeof (tmp1));
    p1 = tmp1;
    
    p = malloc (100000);
    if (!p)
	return;
    WIN32GUI_LoadUIString (IDS_ROMSCANEND, p, 100);
    strcat (p, "\n\n");

    /* A500 */
    p2 = strchr (p1, '\n');
    if (!p2) goto end;
    *p2++= 0; strcat (p, p1); strcat (p, " Boot ROM v1.2:");
    roms[0] = 5; roms[1] = 4; roms[2] = -1;
    if (listrom (roms)) strcat (p, avail); else strcat (p, unavail);
    strcat (p, p1); strcat (p, " Boot ROM v1.3:");
    roms[0] = 6; roms[1] = 32; roms[2] = -1;
    if (listrom (roms)) strcat (p, avail); else strcat (p, unavail);
    p1 = p2;
   
    /* A500+ */
    p2 = strchr (p1, '\n');
    if (!p2) goto end;
    *p2++= 0; strcat (p, p1); strcat (p, ": ");
    roms[0] = 7; roms[1] = -1;
    if (listrom (roms)) strcat (p, avail); else strcat (p, unavail);
    p1 = p2;

    /* A600 */
    p2 = strchr (p1, '\n');
    if (!p2) goto end;
    *p2++= 0; strcat (p, p1); strcat (p, ": ");
    roms[0] = 8; roms[1] = 9; roms[2] = 10; roms[3] = -1;
    if (listrom (roms)) strcat (p, avail); else strcat (p, unavail);
    p1 = p2;

    /* A1000 */
    p2 = strchr (p1, '\n');
    if (!p2) goto end;
    *p2++= 0; strcat (p, p1); strcat (p, ": ");
    roms[0] = 23; roms[1] = 24; roms[2] = -1;
    if (listrom (roms)) strcat (p, avail); else strcat (p, unavail);
    p1 = p2;

    /* A1200 */
    p2 = strchr (p1, '\n');
    if (!p2) goto end;
    *p2++= 0; strcat (p, p1); strcat (p, ": ");
    roms[0] = 11; roms[1] = 31; roms[2] = 15; roms[3] = -1;
    if (listrom (roms)) strcat (p, avail); else strcat (p, unavail);
    p1 = p2;

    /* CD32 */
    ok = 0;
    p2 = strchr (p1, '\n');
    if (!p2) goto end;
    *p2++= 0; strcat (p, p1); strcat (p, ": ");
    roms[0] = 18; roms[1] = -1;
    if (listrom (roms)) {
	roms[0] = 19;
	if (listrom (roms))
	    ok = 1;
    }
    if (ok) strcat (p, avail); else strcat (p, unavail);
    p1 = p2;

    /* CDTV */
    ok = 0;
    p2 = strchr (p1, '\n');
    if (!p2) goto end;
    *p2++= 0; strcat (p, p1); strcat (p, ": ");
    roms[0] = 20; roms[1] = 21; roms[2] = 22; roms[3] = -1;
    if (listrom (roms)) {
	roms[0] = 6; roms[1] = 32; roms[2] = -1;
	if (listrom (roms))
	    ok = 1;
    }
    if (ok) strcat (p, avail); else strcat (p, unavail);
    p1 = p2;

#if 0
    /* Arcadia */
    p2 = strchr (p1, '\n');
    if (!p2) goto end;
    *p2++= 0; strcat (p, p1); strcat (p, ": ");
    roms[0] = 5; roms[1] = -1;
    if (listrom (roms)) strcat (p, avail); else strcat (p, unavail);
    p1 = p2;
#endif

    pre_gui_message (p);
end:
    free (p);
}

static int scan_roms_2 (char *pathp)
{
    HKEY fkey = NULL;
    char buf[MAX_DPATH], path[MAX_DPATH];
    WIN32_FIND_DATA find_data;
    HANDLE handle;
    uae_u8 *keybuf;
    int keysize;
    int ret;
    
    if (!pathp)
	fetch_path ("KickstartPath", path, sizeof (path));
    else
	strcpy (path, pathp);
    keybuf = load_keyfile (&workprefs, path, &keysize);
    strcpy (buf, path);
    strcat (buf, "*.*");
    if (!hWinUAEKey)
	goto end;
    SHDeleteKey (hWinUAEKey, "DetectedROMs");
    RegCreateKeyEx(hWinUAEKey , "DetectedROMs", 0, NULL, REG_OPTION_NON_VOLATILE,
	KEY_READ | KEY_WRITE, NULL, &fkey, NULL);
    if (fkey == NULL)
	goto end;
    ret = 0;
    for (;;) {
	handle = FindFirstFile (buf, &find_data);
	if (handle == INVALID_HANDLE_VALUE)
	    goto end;
	for (;;) {
	    char tmppath[MAX_DPATH];
	    strcpy (tmppath, path);
	    strcat (tmppath, find_data.cFileName);
	    if (find_data.nFileSizeLow < 10000000 && scan_rom (tmppath, fkey, keybuf, keysize))
		ret = 1;
	    if (FindNextFile (handle, &find_data) == 0) {
		FindClose (handle);
		break;
	    }
	}
	if (!keybuf && ret) { /* did previous scan detect keyfile? */
	    keybuf = load_keyfile (&workprefs, path, &keysize);
	    if (keybuf) /* ok, maybe we now find more roms.. */
		continue;
	}
	break;
    }
end:
    if (fkey)
	RegCloseKey (fkey);
    free_keyfile (keybuf);
    return ret;
}

int scan_roms (char *pathp)
{
    char path[MAX_DPATH];
    int ret;
    static int recursive;

    if (recursive)
	return 0;
    recursive++;
    ret = scan_roms_2 (pathp);
    sprintf (path, "%s..\\shared\\rom\\", start_path_data);
    if (!ret && pathp == NULL) {
	ret = scan_roms_2 (path);
	if (ret)
	    set_path ("KickstartPath", path);
    }
    read_rom_list ();
    show_rom_list ();
    recursive--;
    return ret;
}

struct ConfigStruct {
    char Name[MAX_DPATH];
    char Path[MAX_DPATH];
    char Fullpath[MAX_DPATH];
    char HostLink[MAX_DPATH];
    char HardwareLink[MAX_DPATH];
    char Description[CFG_DESCRIPTION_LENGTH];
    int Type, Directory;
    struct ConfigStruct *Parent, *Child;
    int host, hardware;
    HTREEITEM item;
};

static char *configreg[] = { "ConfigFile", "ConfigFileHardware", "ConfigFileHost" };
static char *configreg2[] = { "", "ConfigFileHardware_Auto", "ConfigFileHost_Auto" };
static struct ConfigStruct **configstore;
static int configstoresize, configstoreallocated, configtype, configtypepanel;

static struct ConfigStruct *getconfigstorefrompath (char *path, char *out, int type)
{
    int i;
    for (i = 0; i < configstoresize; i++) {
	if (((configstore[i]->Type == 0 || configstore[i]->Type == 3) && type == 0) || (configstore[i]->Type == type)) {
	    char path2[MAX_DPATH];
	    strcpy (path2, configstore[i]->Path);
	    strncat (path2, configstore[i]->Name, MAX_DPATH);
	    if (!strcmp (path, path2)) {
		strcpy (out, configstore[i]->Fullpath);
		strncat (out, configstore[i]->Name, MAX_DPATH);
		return configstore[i];
	    }
	}
    }
    return 0;
}

int target_cfgfile_load (struct uae_prefs *p, char *filename, int type, int isdefault)
{
    int v, i, type2;
    DWORD ct, ct2, size;
    char tmp1[MAX_DPATH], tmp2[MAX_DPATH];
    char fname[MAX_DPATH];
    
    strcpy (fname, filename);
    if (!zfile_exists (fname)) {
	fetch_configurationpath (fname, sizeof (fname));
	strcat (fname, filename);
    }

    if (isdefault)
	qs_override = 1;
    if (type < 0) {
	type = 0;
	cfgfile_get_description (fname, NULL, NULL, NULL, &type);
    }
    if (type == 0 || type == 1) {
	discard_prefs (p, 0);
#ifdef FILESYS
	free_mountinfo (currprefs.mountinfo);
#endif
    }
    type2 = type;
    if (type == 0)
	default_prefs (p, type);
    RegQueryValueEx (hWinUAEKey, "ConfigFile_NoAuto", 0, NULL, (LPBYTE)&ct2, &size);
    v = cfgfile_load (p, fname, &type2, ct2);
    if (!v)
	return v;
    if (type > 0)
	return v;
    for (i = 1; i <= 2; i++) {
	if (type != i) {
	    size = sizeof (ct);
	    ct = 0;
	    RegQueryValueEx (hWinUAEKey, configreg2[i], 0, NULL, (LPBYTE)&ct, &size);
	    if (ct && ((i == 1 && p->config_hardware_path[0] == 0) || (i == 2 && p->config_host_path[0] == 0) || ct2)) {
		size = sizeof (tmp1);
		RegQueryValueEx (hWinUAEKey, configreg[i], 0, NULL, (LPBYTE)tmp1, &size);
		fetch_path ("ConfigurationPath", tmp2, sizeof (tmp2));
		strcat (tmp2, tmp1);
		v = i;
		cfgfile_load (p, tmp2, &v, 1);
	    }
	}
    }
    v = 1;
    return v;
}

static int gui_width = 640, gui_height = 480;

static int mm = 0;
static void m(void)
{
    write_log ("%d:0: %dx%d %dx%d %dx%d\n", mm, currprefs.gfx_size.width, currprefs.gfx_size.height,
	workprefs.gfx_size.width, workprefs.gfx_size.height, changed_prefs.gfx_size.width, changed_prefs.gfx_size.height);
    write_log ("%d:1: %dx%d %dx%d %dx%d\n", mm, currprefs.gfx_size_fs.width, currprefs.gfx_size_fs.height,
	workprefs.gfx_size_fs.width, workprefs.gfx_size_fs.height, changed_prefs.gfx_size_fs.width, changed_prefs.gfx_size_fs.height);
    mm++;
}

/* if drive is -1, show the full GUI, otherwise file-requester for DF[drive] */
void gui_display(int shortcut)
{
    static int here;
    int flipflop = 0;
    HRESULT hr;

    if (here)
	return;
    here++;
    screenshot_prepare();
#ifdef D3D
    D3D_guimode (TRUE);
#endif
#ifdef CD32
    akiko_entergui ();
#endif
    inputdevice_unacquire ();
    clearallkeys ();
#ifdef AHI
    ahi_close_sound ();
#endif
    pause_sound ();
    setmouseactive (0);

    if ((!WIN32GFX_IsPicassoScreen() && currprefs.gfx_afullscreen && (currprefs.gfx_size.width < gui_width || currprefs.gfx_size.height < gui_height))
#ifdef PICASSO96
	|| (WIN32GFX_IsPicassoScreen() && currprefs.gfx_pfullscreen && (picasso96_state.Width < gui_width || picasso96_state.Height < gui_height))
#endif
    ) {
	flipflop = 1;
    }

    WIN32GFX_ClearPalette();
    manual_painting_needed++; /* So that WM_PAINT will refresh the display */

    if (isfullscreen ()) {
	hr = DirectDraw_FlipToGDISurface();
	if (FAILED(hr))
	    write_log ("FlipToGDISurface failed, %s\n", DXError (hr));
    }

    if (shortcut == -1) {
	int ret;
	if (flipflop)
	    ShowWindow (hAmigaWnd, SW_MINIMIZE);
	ret = GetSettings (0, flipflop ? (currprefs.win32_notaskbarbutton ? hHiddenWnd : GetDesktopWindow()) : hAmigaWnd);
	if (flipflop > 0)
	    ShowWindow (hAmigaWnd, SW_RESTORE);
	if (!ret) {
	    savestate_state = 0;
	}
    } else if (shortcut >= 0 && shortcut < 4) {
	DiskSelection(hAmigaWnd, IDC_DF0+shortcut, 0, &changed_prefs, 0);
    } else if (shortcut == 5) {
	if (DiskSelection(hAmigaWnd, IDC_DOSAVESTATE, 9, &changed_prefs, 0))
	    save_state (savestate_fname, "Description!");
    } else if (shortcut == 4) {
	if (DiskSelection(hAmigaWnd, IDC_DOLOADSTATE, 10, &changed_prefs, 0))
	    savestate_state = STATE_DORESTORE;
    }
    manual_painting_needed--; /* So that WM_PAINT doesn't need to use custom refreshing */
    manual_palette_refresh_needed = 1;
    resume_sound ();
#ifdef AHI
    ahi_open_sound ();
#endif
    inputdevice_copyconfig (&changed_prefs, &currprefs);
    inputdevice_config_change_test ();
    clearallkeys ();
    inputdevice_acquire ();
#ifdef CD32
    akiko_exitgui ();
#endif
    if (flipflop >= 0)
	setmouseactive (1);
#ifdef D3D
    D3D_guimode (FALSE);
#endif
#ifdef AVIOUTPUT
    AVIOutput_Begin ();
#endif
    fpscounter_reset ();
    WIN32GFX_SetPalette();
#ifdef PICASSO96
    DX_SetPalette (0, 256);
#endif
    screenshot_free();
    write_disk_history();
    here--;
}

static void prefs_to_gui (struct uae_prefs *p)
{
    workprefs = *p;
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
    updatewinfsmode (&changed_prefs);
}

int DirectorySelection(HWND hDlg, int flag, char *path)
{
    BROWSEINFO bi;
    LPITEMIDLIST pidlBrowse;
    char buf[MAX_DPATH];

    buf[0] = 0;
    bi.hwndOwner = hDlg;
    bi.pidlRoot = NULL; 
    bi.pszDisplayName = buf;
    bi.lpszTitle = "Select folder"; 
    bi.ulFlags = 0; 
    bi.lpfn = NULL; 
    bi.lParam = 0; 
 
    // Browse for a folder and return its PIDL. 
    pidlBrowse = SHBrowseForFolder(&bi); 
    if (pidlBrowse != NULL) { 
	if (SHGetPathFromIDList(pidlBrowse, buf)) {
	    strcpy (path, buf);
	    return 1;
	}
    }
    return 0;
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
int DiskSelection_2 (HWND hDlg, WPARAM wParam, int flag, struct uae_prefs *prefs, char *path_out, int *multi)
{
    static int statefile_previousfilter;
    OPENFILENAME openFileName;
    char full_path[MAX_DPATH] = "";
    char full_path2[MAX_DPATH];
    char file_name[MAX_DPATH] = "";
    char init_path[MAX_DPATH] = "";
    BOOL result = FALSE;
    char *amiga_path = NULL;
    char description[CFG_DESCRIPTION_LENGTH] = "";
    char *p, *nextp;
    int all = 1;
    int next;
    int filterindex = 0;

    char szTitle[MAX_DPATH];
    char szFormat[MAX_DPATH];
    char szFilter[MAX_DPATH] = { 0 };
    
    memset (&openFileName, 0, sizeof (OPENFILENAME));
    
    strncpy (init_path, start_path_data, MAX_DPATH);
    switch (flag)
    {
	case 0:
	case 1:
	    fetch_path ("FloppyPath", init_path, sizeof (init_path));
	break;
	case 2:
	case 3:
	    fetch_path ("hdfPath", init_path, sizeof (init_path));
	break;
	case 6:
	case 7:
	case 11:
	    fetch_path ("KickstartPath", init_path, sizeof (init_path));
	break;
	case 4:
	case 5:
	case 8:
	    fetch_path ("ConfigurationPath", init_path, sizeof (init_path));
	break;
	case 9:
	case 10:
	    fetch_path ("StatefilePath", init_path, sizeof (init_path));
	break;
	case 15:
	case 16:
	    fetch_path ("InputPath", init_path, sizeof (init_path));
	break;
	
    }

    openFileName.lStructSize = sizeof (OPENFILENAME);
    openFileName.hwndOwner = hDlg;
    openFileName.hInstance = hInst;
    
    switch (flag) {
    case 0:
	WIN32GUI_LoadUIString( IDS_SELECTADF, szTitle, MAX_DPATH );
	WIN32GUI_LoadUIString( IDS_ADF, szFormat, MAX_DPATH );
	sprintf( szFilter, "%s ", szFormat );
	memcpy( szFilter + strlen( szFilter ), DISK_FORMAT_STRING, sizeof(DISK_FORMAT_STRING) + 1);

	openFileName.lpstrTitle = szTitle;
	openFileName.lpstrDefExt = "ADF";
	openFileName.lpstrFilter = szFilter;
	break;
    case 1:
	WIN32GUI_LoadUIString( IDS_CHOOSEBLANK, szTitle, MAX_DPATH );
	WIN32GUI_LoadUIString( IDS_ADF, szFormat, MAX_DPATH );
	sprintf( szFilter, "%s ", szFormat );
	memcpy( szFilter + strlen( szFilter ), "(*.adf)\0*.adf\0", 15 );

	openFileName.lpstrTitle = szTitle;
	openFileName.lpstrDefExt = "ADF";
	openFileName.lpstrFilter = szFilter;
	break;
    case 2:
    case 3:
	WIN32GUI_LoadUIString( IDS_SELECTHDF, szTitle, MAX_DPATH );
	WIN32GUI_LoadUIString( IDS_HDF, szFormat, MAX_DPATH );
	sprintf( szFilter, "%s ", szFormat );
	memcpy( szFilter + strlen( szFilter ),  HDF_FORMAT_STRING, sizeof (HDF_FORMAT_STRING) + 1);

	openFileName.lpstrTitle = szTitle;
	openFileName.lpstrDefExt = "HDF";
	openFileName.lpstrFilter = szFilter;
	break;
    case 4:
    case 5:
	WIN32GUI_LoadUIString( IDS_SELECTUAE, szTitle, MAX_DPATH );
	WIN32GUI_LoadUIString( IDS_UAE, szFormat, MAX_DPATH );
	sprintf( szFilter, "%s ", szFormat );
	memcpy( szFilter + strlen( szFilter ), "(*.uae)\0*.uae\0", 15 );

	openFileName.lpstrTitle  = szTitle;
	openFileName.lpstrDefExt = "UAE";
	openFileName.lpstrFilter = szFilter;
	break;
    case 6:
	WIN32GUI_LoadUIString( IDS_SELECTROM, szTitle, MAX_DPATH );
	WIN32GUI_LoadUIString( IDS_ROM, szFormat, MAX_DPATH );
	sprintf( szFilter, "%s ", szFormat );
	memcpy( szFilter + strlen( szFilter ), ROM_FORMAT_STRING, sizeof (ROM_FORMAT_STRING) + 1);

	openFileName.lpstrTitle = szTitle;
	openFileName.lpstrDefExt = "ROM";
	openFileName.lpstrFilter = szFilter;
	break;
    case 7:
	WIN32GUI_LoadUIString( IDS_SELECTKEY, szTitle, MAX_DPATH );
	WIN32GUI_LoadUIString( IDS_KEY, szFormat, MAX_DPATH );
	sprintf( szFilter, "%s ", szFormat );
	memcpy( szFilter + strlen( szFilter ), "(*.key)\0*.key\0", 15 );

	openFileName.lpstrTitle = szTitle;
	openFileName.lpstrDefExt = "KEY";
	openFileName.lpstrFilter = szFilter;
	break;
    case 15:
    case 16:
	WIN32GUI_LoadUIString(flag == 15 ? IDS_RESTOREINP : IDS_SAVEINP, szTitle, MAX_DPATH);
	WIN32GUI_LoadUIString(IDS_INP, szFormat, MAX_DPATH);
	sprintf( szFilter, "%s ", szFormat );
	memcpy( szFilter + strlen( szFilter ), INP_FORMAT_STRING, sizeof (INP_FORMAT_STRING) + 1);

	openFileName.lpstrTitle = szTitle;
	openFileName.lpstrDefExt = "INP";
	openFileName.lpstrFilter = szFilter;
	break;
    case 9:
    case 10:
	WIN32GUI_LoadUIString(flag == 10 ? IDS_RESTOREUSS : IDS_SAVEUSS, szTitle, MAX_DPATH);
	WIN32GUI_LoadUIString(IDS_USS, szFormat, MAX_DPATH);
	sprintf( szFilter, "%s ", szFormat );
	if (flag == 10) {
	    memcpy(szFilter + strlen(szFilter), USS_FORMAT_STRING_RESTORE, sizeof (USS_FORMAT_STRING_RESTORE) + 1);
	    all = 1;
	} else {
	    char tmp[MAX_DPATH];
	    memcpy(szFilter + strlen(szFilter), USS_FORMAT_STRING_SAVE, sizeof (USS_FORMAT_STRING_SAVE) + 1);
	    p = szFilter;
	    while (p[0] != 0 || p[1] !=0 ) p++;
	    p++;
	    WIN32GUI_LoadUIString(IDS_STATEFILE_UNCOMPRESSED, tmp, sizeof(tmp));
	    strcat(p, tmp);
	    strcat(p, " (*.uss");
	    p += strlen(p) + 1;
	    strcpy(p, "*.uss");
	    p += strlen(p) + 1;
	    WIN32GUI_LoadUIString(IDS_STATEFILE_RAMDUMP, tmp, sizeof(tmp));
	    strcat(p, tmp);
	    strcat(p, " (*.dat)");
	    p += strlen(p) + 1;
	    strcpy (p, "*.dat");
	    p += strlen(p) + 1;
	    WIN32GUI_LoadUIString(IDS_STATEFILE_WAVE, tmp, sizeof(tmp));
	    strcat(p, tmp);
	    strcat(p, " (*.wav)");
	    p += strlen(p) + 1;
	    strcpy (p, "*.wav");
	    p += strlen(p) + 1;
	    *p = 0;
	    all = 0;
	    filterindex = statefile_previousfilter;
	}
	openFileName.lpstrTitle  = szTitle;
	openFileName.lpstrDefExt = "USS";
	openFileName.lpstrFilter = szFilter;
	break;
    case 11:
	WIN32GUI_LoadUIString( IDS_SELECTFLASH, szTitle, MAX_DPATH );
	WIN32GUI_LoadUIString( IDS_FLASH, szFormat, MAX_DPATH );
	sprintf( szFilter, "%s ", szFormat );
	memcpy( szFilter + strlen( szFilter ), "(*.nvr)\0*.nvr\0", 15 );

	openFileName.lpstrTitle  = szTitle;
	openFileName.lpstrDefExt = "NVR";
	openFileName.lpstrFilter = szFilter;
	break;
    case 8:
    default:
	WIN32GUI_LoadUIString( IDS_SELECTINFO, szTitle, MAX_DPATH );

	openFileName.lpstrTitle = szTitle;
	openFileName.lpstrFilter = NULL;
	openFileName.lpstrDefExt = NULL;
	break;
    case 12:
	WIN32GUI_LoadUIString( IDS_SELECTFS, szTitle, MAX_DPATH );

	openFileName.lpstrTitle = szTitle;
	openFileName.lpstrFilter = NULL;
	openFileName.lpstrDefExt = NULL;
	openFileName.lpstrInitialDir = path_out;
	break;
    case 13:
	WIN32GUI_LoadUIString( IDS_SELECTINFO, szTitle, MAX_DPATH );

	openFileName.lpstrTitle = szTitle;
	openFileName.lpstrFilter = NULL;
	openFileName.lpstrDefExt = NULL;
	openFileName.lpstrInitialDir = path_out;
	break;
    }
    if (all) {
	p = szFilter;
	while (p[0] != 0 || p[1] !=0 ) p++;
	p++;
	strcpy (p, "All files (*.*)");
	p += strlen(p) + 1;
	strcpy (p, "*.*");
	p += strlen(p) + 1;
	*p = 0;
    }
    openFileName.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
	OFN_LONGNAMES | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    openFileName.lpstrCustomFilter = NULL;
    openFileName.nMaxCustFilter = 0;
    openFileName.nFilterIndex = filterindex;
    openFileName.lpstrFile = full_path;
    openFileName.nMaxFile = MAX_DPATH;
    openFileName.lpstrFileTitle = file_name;
    openFileName.nMaxFileTitle = MAX_DPATH;
    openFileName.lpstrInitialDir = init_path;
    openFileName.lpfnHook = NULL;
    openFileName.lpTemplateName = NULL;
    openFileName.lCustData = 0;
    if (multi)
	openFileName.Flags |= OFN_ALLOWMULTISELECT;
    if (flag == 1 || flag == 3 || flag == 5 || flag == 9 || flag == 11 || flag == 16) {
	if (!(result = GetSaveFileName (&openFileName)))
	    write_log ("GetSaveFileName() failed.\n");
    } else {
	if (!(result = GetOpenFileName (&openFileName)))
	    write_log ("GetOpenFileName() failed.\n");
    }
    memcpy (full_path2, full_path, sizeof (full_path));
    next = 0;
    nextp = full_path2 + openFileName.nFileOffset;
    if (path_out) {
	if (multi) {
	    while (nextp[0])
		nextp += strlen (nextp) + 1;
	    memcpy (path_out, full_path2, nextp - full_path2 + 1);
	} else {
	    strcpy (path_out, full_path2);
	}
    }
    nextp = full_path2 + openFileName.nFileOffset;
    if (nextp[strlen(nextp) + 1] == 0)
	multi = 0;
    while (result && next >= 0)
    {
	next = -1;
	if (multi) {
	    if (nextp[0] == 0)
		break;
	    sprintf (full_path, "%s\\%s", full_path2, nextp);
	    nextp += strlen (nextp) + 1;
	}
	switch (wParam) 
	{
	case IDC_PATH_NAME:
	case IDC_PATH_FILESYS:
	    if (flag == 8) {
		if(strstr(full_path, "Configurations\\")) {
		    strcpy(full_path, init_path);
		    strcat(full_path, file_name);
		}
	    }
	    SetDlgItemText (hDlg, wParam, full_path);
	    break;
	case IDC_DF0:
	case IDC_DF0QQ:
	    SetDlgItemText (hDlg, IDC_DF0TEXT, full_path);
	    strcpy(prefs->df[0], full_path);
	    DISK_history_add (full_path, -1);
	    next = IDC_DF1;
	    break;
	case IDC_DF1:
	case IDC_DF1QQ:
	    SetDlgItemText (hDlg, IDC_DF1TEXT, full_path);
	    strcpy(prefs->df[1], full_path);
	    DISK_history_add (full_path, -1);
	    next = IDC_DF2;
	    break;
	case IDC_DF2:
	    SetDlgItemText (hDlg, IDC_DF2TEXT, full_path);
	    strcpy(prefs->df[2], full_path);
	    DISK_history_add (full_path, -1);
	    next = IDC_DF3;
	    break;
	case IDC_DF3:
	    SetDlgItemText (hDlg, IDC_DF3TEXT, full_path);
	    strcpy(prefs->df[3], full_path);
	    DISK_history_add (full_path, -1);
	    break;
	case IDC_DOSAVESTATE:
	case IDC_DOLOADSTATE:
	    statefile_previousfilter = openFileName.nFilterIndex;
	    savestate_initsave (full_path, openFileName.nFilterIndex);
	    break;
	case IDC_CREATE:
	    {
		char disk_name[32];
		disk_name[0] = 0; disk_name[31] = 0;
		GetDlgItemText(hDlg, IDC_CREATE_NAME, disk_name, 30);
		disk_creatediskfile(full_path, 0, SendDlgItemMessage(hDlg, IDC_FLOPPYTYPE, CB_GETCURSEL, 0, 0L), disk_name);
	    }
	    break;
	case IDC_CREATE_RAW:
	    disk_creatediskfile(full_path, 1, SendDlgItemMessage(hDlg, IDC_FLOPPYTYPE, CB_GETCURSEL, 0, 0L), NULL);
	    break;
	case IDC_LOAD:
	    if (target_cfgfile_load(&workprefs, full_path, 0, 0) == 0) {
		char szMessage[MAX_DPATH];
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
	    strcpy (workprefs.romfile, full_path);
	    break;
	case IDC_ROMFILE2:
	    strcpy (workprefs.romextfile, full_path);
	    break;
	case IDC_FLASHFILE:
	    strcpy (workprefs.flashfile, full_path);
	    break;
	case IDC_CARTFILE:
	    strcpy (workprefs.cartfile, full_path);
	    break;
	case IDC_INPREC_PLAY:
	    inprec_open(full_path, IsDlgButtonChecked(hDlg, IDC_AVIOUTPUT_AUDIO) == BST_CHECKED ? -2 : -1);
	    break;
	case IDC_INPREC_RECORD:
	    inprec_open(full_path, 1);
	    break;
	}
	if (flag == 0 || flag == 1) {
	    amiga_path = strstr (openFileName.lpstrFile, openFileName.lpstrFileTitle);
	    if (amiga_path && amiga_path != openFileName.lpstrFile) {
		*amiga_path = 0;
		if (hWinUAEKey)
		    RegSetValueEx (hWinUAEKey, "FloppyPath", 0, REG_SZ, (CONST BYTE *)openFileName.lpstrFile, strlen(openFileName.lpstrFile) + 1);
	    }
	} else if (flag == 2 || flag == 3) {
	    amiga_path = strstr (openFileName.lpstrFile, openFileName.lpstrFileTitle);
	    if (amiga_path && amiga_path != openFileName.lpstrFile) {
		*amiga_path = 0;
		if(hWinUAEKey)
		    RegSetValueEx(hWinUAEKey, "hdfPath", 0, REG_SZ, (CONST BYTE *)openFileName.lpstrFile, strlen(openFileName.lpstrFile) + 1);
	    }
	}
	if (!multi)
	    next = -1;
	if (next >= 0)
	    wParam = next;
    }
    return result;
}
int DiskSelection(HWND hDlg, WPARAM wParam, int flag, struct uae_prefs *prefs, char *path_out)
{
    return DiskSelection_2 (hDlg, wParam, flag, prefs, path_out, NULL);
}
int MultiDiskSelection(HWND hDlg, WPARAM wParam, int flag, struct uae_prefs *prefs, char *path_out)
{
    int multi = 0;
    return DiskSelection_2 (hDlg, wParam, flag, prefs, path_out, &multi);
}
static int loopmulti (char *s, char *out)
{
    static int index;

    if (!out) {
	index = strlen (s) + 1;
	return 1;
    }
    if (index < 0)
	return 0;
    if (!s[index]) {
	if (s[strlen (s) + 1] == 0) {
	    strcpy (out, s);
	    index = -1;
	    return 1;
	}
	return 0;
    }
    sprintf (out, "%s\\%s", s, s + index);
    index += strlen (s + index) + 1;
    return 1;
}

static BOOL CreateHardFile (HWND hDlg, UINT hfsizem, char *dostype)
{
    HANDLE hf;
    int i = 0;
    BOOL result = FALSE;
    LONG highword = 0;
    DWORD ret, written;
    char init_path[MAX_DPATH] = "";
    uae_u64 hfsize;
    uae_u32 dt;
    uae_u8 b;

    hfsize = (uae_u64)hfsizem * 1024 * 1024;
    DiskSelection (hDlg, IDC_PATH_NAME, 3, &workprefs, 0);
    GetDlgItemText (hDlg, IDC_PATH_NAME, init_path, MAX_DPATH);
    if (*init_path && hfsize) {
	SetCursor (LoadCursor(NULL, IDC_WAIT));
	if ((hf = CreateFile (init_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL) ) != INVALID_HANDLE_VALUE) {
	    if (hfsize >= 0x80000000) {
		highword = (DWORD)(hfsize >> 32);
		ret = SetFilePointer (hf, (DWORD)hfsize, &highword, FILE_BEGIN);
	    } else {
		ret = SetFilePointer (hf, (DWORD)hfsize, NULL, FILE_BEGIN);
	    }
	    if (ret == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
		write_log ("SetFilePointer() failure for %s to posn %ud\n", init_path, hfsize);
	    else
		result = SetEndOfFile (hf);
	    SetFilePointer (hf, 0, NULL, FILE_BEGIN);
	    b = 0;
	    WriteFile (hf, &b, 1, &written, NULL);
	    WriteFile (hf, &b, 1, &written, NULL);
	    WriteFile (hf, &b, 1, &written, NULL);
	    WriteFile (hf, &b, 1, &written, NULL);
	    if (sscanf (dostype, "%x", &dt) > 0) {
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
	    write_log ("CreateFile() failed to create %s\n", init_path);
	}
	SetCursor (LoadCursor (NULL, IDC_ARROW));
    }
    return result;
}

static const char *memsize_names[] = {
/* 0 */ szNone,
/* 1 */ "256 K",
/* 2 */ "512 K",
/* 3 */ "1 MB",
/* 4 */ "2 MB",
/* 5 */ "4 MB",
/* 6 */ "8 MB",
/* 7 */ "16 MB",
/* 8 */ "32 MB",
/* 9 */ "64 MB",
/* 10*/ "128 MB",
/* 11*/ "256 MB",
/* 12*/ "512 MB",
/* 13*/ "1 GB",
/* 14*/ "1.5MB",
/* 15*/ "1.8MB",
/* 16*/ "2 GB",
};

static unsigned long memsizes[] = {
/* 0 */ 0,
/* 1 */ 0x00040000, /* 256-K */
/* 2 */ 0x00080000, /* 512-K */
/* 3 */ 0x00100000, /* 1-meg */
/* 4 */ 0x00200000, /* 2-meg */
/* 5 */ 0x00400000, /* 4-meg */
/* 6 */ 0x00800000, /* 8-meg */
/* 7 */ 0x01000000, /* 16-meg */
/* 8 */ 0x02000000, /* 32-meg */
/* 9 */ 0x04000000, /* 64-meg */
/* 10*/ 0x08000000, //128 Meg
/* 11*/ 0x10000000, //256 Meg 
/* 12*/ 0x20000000, //512 Meg The correct size is set in mman.c
/* 13*/ 0x40000000, //1GB
/* 14*/ 0x00180000, //1.5MB
/* 15*/ 0x001C0000, //1.8MB
/* 16*/ 0x80000000, //2GB
};

static int msi_chip[] = { 1, 2, 3, 4, 5, 6 };
static int msi_bogo[] = { 0, 2, 3, 14, 15 };
static int msi_fast[] = { 0, 3, 4, 5, 6 };
static int msi_z3fast[] = { 0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 16 };
static int msi_gfx[] = { 0, 3, 4, 5, 6,7,8};

static int CalculateHardfileSize (HWND hDlg)
{
    BOOL Translated = FALSE;
    UINT mbytes = 0;

    mbytes = GetDlgItemInt( hDlg, IDC_HF_SIZE, &Translated, FALSE );
    if (mbytes <= 0)
	mbytes = 0;
    if( !Translated )
	mbytes = 0;
    return mbytes;
}

static const char *nth[] = {
    "", "second ", "third ", "fourth ", "fifth ", "sixth ", "seventh ", "eighth ", "ninth ", "tenth "
};

static void GetConfigPath (char *path, struct ConfigStruct *parent, int noroot)
{
    if (parent == 0) {
	path[0] = 0;
	if (!noroot) {
	    fetch_path ("ConfigurationPath", path, MAX_DPATH);
	}
	return;
    }
    if (parent) {
	GetConfigPath (path, parent->Parent, noroot);
	strncat (path, parent->Name, MAX_DPATH);
	strncat (path, "\\", MAX_DPATH);
    }
}

void FreeConfigStruct (struct ConfigStruct *config)
{
    free (config);
}
struct ConfigStruct *AllocConfigStruct (void)
{
    struct ConfigStruct *config;

    config = xcalloc (sizeof (struct ConfigStruct), 1);
    return config;
}

static struct ConfigStruct *GetConfigs (struct ConfigStruct *configparent, int usedirs, int *level)
{
    DWORD num_bytes = 0;
    char path[MAX_DPATH];
    char path2[MAX_DPATH];
    char shortpath[MAX_DPATH];
    WIN32_FIND_DATA find_data;
    struct ConfigStruct *config, *first;
    HANDLE handle;

    first = NULL;
    GetConfigPath (path, configparent, FALSE);
    GetConfigPath (shortpath, configparent, TRUE);
    strcpy (path2, path);
    strncat (path2, "*.*", MAX_DPATH);
    handle = FindFirstFile(path2, &find_data );
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
	if (strcmp (find_data.cFileName, ".") && strcmp (find_data.cFileName, "..")) {
	    int ok = 0;
	    config = AllocConfigStruct ();
	    strcpy (config->Path, shortpath);
	    strcpy (config->Fullpath, path);
	    if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && usedirs) {
		if ((*level) < 2) {
		    struct ConfigStruct *child;
		    strcpy (config->Name, find_data.cFileName);
		    strcpy (config->Path, shortpath);
		    strcat (config->Path, config->Name);
		    strcat (config->Path, "\\");
		    strcpy (config->Fullpath, path);
		    strcat (config->Fullpath, config->Name);
		    strcat (config->Fullpath, "\\");
		    config->Directory = 1;
		    (*level)++;
		    config->Parent = configparent;
		    if (!stricmp (config->Name, CONFIG_HOST))
			config->host = 1;
		    if (!stricmp (config->Name, CONFIG_HARDWARE))
			config->hardware = 1;
		    child = GetConfigs (config, usedirs, level);
		    (*level)--;
		    if (child)
			config->Child = child;
		    ok = 1;
		}
	    } else if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
		char path3[MAX_DPATH];
		if (strlen (find_data.cFileName) > 4 && !strcasecmp (find_data.cFileName + strlen (find_data.cFileName) - 4, ".uae")) {
		    strcpy (path3, path);
		    strncat (path3, find_data.cFileName, MAX_DPATH);
		    if (cfgfile_get_description (path3, config->Description, config->HostLink, config->HardwareLink, &config->Type)) {
			strcpy (config->Name, find_data.cFileName);
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
		configstore = realloc (configstore, sizeof (struct ConfigStruct*) * configstoreallocated);
	    }
	    configstore[configstoresize++] = config;
	    if (first == NULL)
		first = config;
	}
	if(FindNextFile (handle, &find_data) == 0) {
	    FindClose(handle);
	    break;
	}
    }
    return first;
}

static void FreeConfigStore (void)
{
    int i;
    for (i = 0; i < configstoresize; i++)
	FreeConfigStruct (configstore[i]);
    free (configstore);
    configstore = 0;
    configstoresize = configstoreallocated = 0;
}
static struct ConfigStruct *CreateConfigStore (struct ConfigStruct *oldconfig)
{
    int level = 0, i;
    char path[MAX_DPATH], name[MAX_DPATH];
    struct ConfigStruct *cs;
    
    if (oldconfig) {
	strcpy (path, oldconfig->Path);
	strcpy (name, oldconfig->Name);
    }
    FreeConfigStore ();
    GetConfigs (NULL, 1, &level);
    if (oldconfig) {
	for (i = 0; i < configstoresize; i++) {
	    cs = configstore[i];
	    if (!cs->Directory && !strcmp (path, cs->Path) && !strcmp (name, cs->Name))
		return cs;
	}
    }
    return 0;
}

static char *HandleConfiguration (HWND hDlg, int flag, struct ConfigStruct *config)
{
    char name[MAX_DPATH], desc[MAX_DPATH];
    char path[MAX_DPATH];
    static char full_path[MAX_DPATH];

    full_path[0] = 0;
    GetDlgItemText (hDlg, IDC_EDITNAME, name, MAX_DPATH);
    if (flag == CONFIG_SAVE_FULL || flag == CONFIG_SAVE) {
	if (strlen (name) < 4 || strcasecmp (name + strlen (name) - 4, ".uae")) {
	    strcat (name, ".uae");
	    SetDlgItemText (hDlg, IDC_EDITNAME, name);
	}
	if (config)
	    strcpy (config->Name, name);
    }
    GetDlgItemText (hDlg, IDC_EDITDESCRIPTION, desc, MAX_DPATH);
    if (config) {
	strcpy (path, config->Fullpath);
    } else {
	fetch_configurationpath (path, sizeof (path));
    }
    strncat (path, name, MAX_DPATH);
    strcpy (full_path, path);
    switch (flag)
    {
	case CONFIG_SAVE_FULL:
	    DiskSelection(hDlg, IDC_SAVE, 5, &workprefs, 0);
	break;

	case CONFIG_LOAD_FULL:
	    DiskSelection(hDlg, IDC_LOAD, 4, &workprefs, 0);
	    EnableWindow(GetDlgItem (hDlg, IDC_VIEWINFO), workprefs.info[0]);
	break;
		
	case CONFIG_SAVE:
	    if (strlen (name) == 0 || strcmp (name, ".uae") == 0) {
		char szMessage[ MAX_DPATH ];
		WIN32GUI_LoadUIString( IDS_MUSTENTERNAME, szMessage, MAX_DPATH );
		pre_gui_message (szMessage);
	    } else {
		strcpy (workprefs.description, desc);
		cfgfile_save (&workprefs, path, configtypepanel);
	    }
	    break;
	
	case CONFIG_LOAD:
	    if (strlen (name) == 0) {
		char szMessage[ MAX_DPATH ];
		WIN32GUI_LoadUIString( IDS_MUSTSELECTCONFIG, szMessage, MAX_DPATH );
		pre_gui_message (szMessage);
	    } else {
		if (target_cfgfile_load (&workprefs, path, configtypepanel, 0) == 0) {
		    char szMessage[ MAX_DPATH ];
		    WIN32GUI_LoadUIString( IDS_COULDNOTLOADCONFIG, szMessage, MAX_DPATH );
		    pre_gui_message (szMessage);
		} else {
		    EnableWindow (GetDlgItem (hDlg, IDC_VIEWINFO), workprefs.info[0]);
		}
	    break;

	    case CONFIG_DELETE:
		if (strlen (name) == 0) {
		    char szMessage[ MAX_DPATH ];
		    WIN32GUI_LoadUIString( IDS_MUSTSELECTCONFIGFORDELETE, szMessage, MAX_DPATH );
		    pre_gui_message (szMessage);
		} else {
		    char szMessage[ MAX_DPATH ];
		    char szTitle[ MAX_DPATH ];
		    WIN32GUI_LoadUIString( IDS_DELETECONFIGCONFIRMATION, szMessage, MAX_DPATH );
		    WIN32GUI_LoadUIString( IDS_DELETECONFIGTITLE, szTitle, MAX_DPATH );
		    if( MessageBox( hDlg, szMessage, szTitle,
			MB_YESNO | MB_ICONWARNING | MB_APPLMODAL | MB_SETFOREGROUND ) == IDYES ) {
			cfgfile_backup (path);
			DeleteFile (path);
			write_log ("deleted config '%s'\n", path);
		    }
		}
	    break;
	}
    }
    return full_path;
}


static int disk_in_drive (int entry)
{
    int i;
    for (i = 0; i < 4; i++) {
	if (strlen (workprefs.dfxlist[entry]) > 0 && !strcmp (workprefs.dfxlist[entry], workprefs.df[i]))
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
	    workprefs.df[drv][0] = 0;
	    disk_eject (drv);
	    return 1;
	}

	if (strcmp (workprefs.df[drv], currprefs.df[drv])) {
	    strcpy (workprefs.df[drv], currprefs.df[drv]);
	    disk_insert (drv, workprefs.df[drv]);
	} else {
	    workprefs.df[drv][0] = 0;
	    disk_eject (drv);
	}
	if (drvs[0] < 0 || drvs[1] < 0 || drvs[2] < 0 || drvs[3] < 0) {
	    drv++;
	    while (drv < 4 && drvs[drv] >= 0)
		drv++;
	    if (drv < 4 && workprefs.dfxtype[drv] >= 0) {
		strcpy (workprefs.df[drv], workprefs.dfxlist[entry]);
		disk_insert (drv, workprefs.df[drv]);
	    }
	}
	return 1;
    }
    for (i = 0; i < 4; i++) {
	if (drvs[i] < 0 && workprefs.dfxtype[i] >= 0) {
	    strcpy (workprefs.df[i], workprefs.dfxlist[entry]);
	    disk_insert (i, workprefs.df[i]);
	    return 1;
	}
    }
    strcpy (workprefs.df[0], workprefs.dfxlist[entry]);
    disk_insert (0, workprefs.df[0]);
    return 1;
}

static int input_selected_device, input_selected_widget, input_total_devices;
static int input_selected_event, input_selected_sub_num;

static void set_lventry_input (HWND list, int index)
{
    int flags, i, sub;
    char name[256];
    char custom[MAX_DPATH];
    char af[10];

    inputdevice_get_mapped_name (input_selected_device, index, &flags, name, custom, input_selected_sub_num);
    if (flags & IDEV_MAPPED_AUTOFIRE_SET)
	WIN32GUI_LoadUIString (IDS_YES, af, sizeof (af));
    else if (flags & IDEV_MAPPED_AUTOFIRE_POSSIBLE)
	WIN32GUI_LoadUIString (IDS_NO, af, sizeof (af));
    else
	strcpy (af,"-");
    ListView_SetItemText(list, index, 1, custom[0] ? custom : name);
    ListView_SetItemText(list, index, 2, af);
    sub = 0;
    for (i = 0; i < MAX_INPUT_SUB_EVENT; i++) {
	if (inputdevice_get_mapped_name (input_selected_device, index, &flags, name, custom, i) || custom[0])
	    sub++;
    }
    sprintf (name, "%d", sub);
    ListView_SetItemText(list, index, 3, name);
}

static void update_listview_input (HWND hDlg)
{
    int i;
    if (!input_total_devices)
	return;
    for (i = 0; i < inputdevice_get_widget_num (input_selected_device); i++)
	set_lventry_input (GetDlgItem (hDlg, IDC_INPUTLIST), i);
}

static int clicked_entry = -1;

#define LOADSAVE_COLUMNS 2
#define INPUT_COLUMNS 4
#define HARDDISK_COLUMNS 7
#define DISK_COLUMNS 3
#define MAX_COLUMN_HEADING_WIDTH 20

#define LV_LOADSAVE 1
#define LV_HARDDISK 2
#define LV_INPUT 3
#define LV_DISK 4

static int listview_num_columns;
static int listview_column_width[HARDDISK_COLUMNS];

void InitializeListView (HWND hDlg)
{
    int lv_type;
    HWND list;
    LV_ITEM lvstruct;
    LV_COLUMN lvcolumn;
    RECT rect;
    char column_heading[ HARDDISK_COLUMNS ][ MAX_COLUMN_HEADING_WIDTH ];
    char blocksize_str[6] = "";
    char readwrite_str[10] = "";
    char size_str[32] = "";
    char volname_str[ MAX_DPATH ] = "";
    char devname_str[ MAX_DPATH ] = "";
    char bootpri_str[6] = "";
    int width = 0;
    int items = 0, result = 0, i, j, entry = 0, temp = 0;
    char tmp[10], tmp2[MAX_DPATH];

    if (hDlg == pages[HARDDISK_ID]) {
	listview_num_columns = HARDDISK_COLUMNS;
	lv_type = LV_HARDDISK;
	WIN32GUI_LoadUIString( IDS_DEVICE, column_heading[0], MAX_COLUMN_HEADING_WIDTH );
	WIN32GUI_LoadUIString( IDS_VOLUME, column_heading[1], MAX_COLUMN_HEADING_WIDTH );
	WIN32GUI_LoadUIString( IDS_PATH, column_heading[2], MAX_COLUMN_HEADING_WIDTH );
	WIN32GUI_LoadUIString( IDS_RW, column_heading[3], MAX_COLUMN_HEADING_WIDTH );
	WIN32GUI_LoadUIString( IDS_BLOCKSIZE, column_heading[4], MAX_COLUMN_HEADING_WIDTH );
	WIN32GUI_LoadUIString( IDS_HFDSIZE, column_heading[5], MAX_COLUMN_HEADING_WIDTH );
	WIN32GUI_LoadUIString( IDS_BOOTPRI, column_heading[6], MAX_COLUMN_HEADING_WIDTH );
	list = GetDlgItem( hDlg, IDC_VOLUMELIST );
    } else if (hDlg == pages[INPUT_ID]) {
	listview_num_columns = INPUT_COLUMNS;
	lv_type = LV_INPUT;
	WIN32GUI_LoadUIString( IDS_INPUTHOSTWIDGET, column_heading[0], MAX_COLUMN_HEADING_WIDTH );
	WIN32GUI_LoadUIString( IDS_INPUTAMIGAEVENT, column_heading[1], MAX_COLUMN_HEADING_WIDTH );
	WIN32GUI_LoadUIString( IDS_INPUTAUTOFIRE, column_heading[2], MAX_COLUMN_HEADING_WIDTH );
	strcpy (column_heading[3], "#");
	list = GetDlgItem( hDlg, IDC_INPUTLIST );
    } else {
	listview_num_columns = DISK_COLUMNS;
	lv_type = LV_DISK;
	strcpy (column_heading[0], "#");
	WIN32GUI_LoadUIString( IDS_DISK_IMAGENAME, column_heading[1], MAX_COLUMN_HEADING_WIDTH );
	WIN32GUI_LoadUIString( IDS_DISK_DRIVENAME, column_heading[2], MAX_COLUMN_HEADING_WIDTH );
	list = GetDlgItem (hDlg, IDC_DISK);
    }
    cachedlist = list;

    ListView_DeleteAllItems( list );

    for( i = 0; i < listview_num_columns; i++ )
	listview_column_width[i] = ListView_GetStringWidth( list, column_heading[i] ) + 15;

    // If there are no columns, then insert some
    lvcolumn.mask = LVCF_WIDTH;
    if( ListView_GetColumn( list, 1, &lvcolumn ) == FALSE )
    {
	for( i = 0; i < listview_num_columns; i++ )
	{
	    lvcolumn.mask     = LVCF_FMT | LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
	    lvcolumn.iSubItem = i;
	    lvcolumn.fmt      = LVCFMT_LEFT;
	    lvcolumn.pszText  = column_heading[i];
	    lvcolumn.cx       = listview_column_width[i];
	    ListView_InsertColumn( list, i, &lvcolumn );
	}
    }
    if (lv_type == LV_INPUT)
    {
	for (i = 0; input_total_devices && i < inputdevice_get_widget_num (input_selected_device); i++) {
	    char name[100];
	    inputdevice_get_widget_type (input_selected_device, i, name);
	    lvstruct.mask     = LVIF_TEXT | LVIF_PARAM;
	    lvstruct.pszText  = name;
	    lvstruct.lParam   = 0;
	    lvstruct.iItem    = i;
	    lvstruct.iSubItem = 0;
	    result = ListView_InsertItem( list, &lvstruct );
	    width = ListView_GetStringWidth( list, lvstruct.pszText ) + 15;
	    if( width > listview_column_width[ 0 ] )
		listview_column_width[ 0 ] = width;
	    entry++;
	}
	listview_column_width [1] = 260;
	listview_column_width [2] = 65;
	listview_column_width [3] = 30;
	update_listview_input (hDlg);
    }
    else if (lv_type == LV_DISK)
    {
	for (i = 0; i < MAX_SPARE_DRIVES; i++) {
	    int drv;
	    sprintf (tmp, "%d", i + 1);
	    lvstruct.mask     = LVIF_TEXT | LVIF_PARAM;
	    lvstruct.pszText  = tmp;
	    lvstruct.lParam   = 0;
	    lvstruct.iItem    = i;
	    lvstruct.iSubItem = 0;
	    result = ListView_InsertItem (list, &lvstruct);
	    strcpy (tmp2, workprefs.dfxlist[i]);
	    j = strlen (tmp2) - 1;
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
		sprintf (tmp, "DF%d:", drv);
	    ListView_SetItemText (list, result, 2, tmp);
	    width = ListView_GetStringWidth( list, lvstruct.pszText ) + 15;
	    if (width > listview_column_width[0])
		listview_column_width[ 0 ] = width;
	    entry++;
	}
	listview_column_width[0] = 30;
	listview_column_width[1] = 354;
	listview_column_width[2] = 50;

    }
    else if (lv_type == LV_HARDDISK)
    {
#ifdef FILESYS
	for( i = 0; i < nr_units( currprefs.mountinfo ); i++ )
	{
	    int secspertrack, surfaces, reserved, blocksize, bootpri;
	    uae_u64 size;
	    int cylinders, readonly, type;
	    char *volname, *devname, *rootdir;
	    char *failure;

	    failure = get_filesys_unit (currprefs.mountinfo, i,
					&devname, &volname, &rootdir, &readonly,
					&secspertrack, &surfaces, &reserved,
					&cylinders, &size, &blocksize, &bootpri, 0, 0);
	    type = is_hardfile (currprefs.mountinfo, i);
	    
	    if (size >= 1024 * 1024 * 1024)
		sprintf (size_str, "%.1fG", ((double)(uae_u32)(size / (1024 * 1024))) / 1024.0);
	    else
		sprintf (size_str, "%.1fM", ((double)(uae_u32)(size / (1024))) / 1024.0);
	    if (type == FILESYS_HARDFILE) {
		sprintf (blocksize_str, "%d", blocksize);
		strcpy (devname_str, devname);
		strcpy (volname_str, "n/a");
		sprintf (bootpri_str, "%d", bootpri);
	    } else if (type == FILESYS_HARDFILE_RDB || type == FILESYS_HARDDRIVE) {
		sprintf (blocksize_str, "%d", blocksize);
		strcpy (devname_str, "n/a");
		strcpy (volname_str, "n/a");
		strcpy (bootpri_str, "n/a");
	    } else {
		strcpy (blocksize_str, "n/a");
		strcpy (devname_str, devname);
		strcpy (volname_str, volname);
		strcpy (size_str, "n/a");
		sprintf (bootpri_str, "%d", bootpri);
	    }
	    if (readonly)
		WIN32GUI_LoadUIString (IDS_NO, readwrite_str, sizeof (readwrite_str));
	    else
		WIN32GUI_LoadUIString (IDS_YES, readwrite_str, sizeof (readwrite_str));

	    lvstruct.mask     = LVIF_TEXT | LVIF_PARAM;
	    lvstruct.pszText  = devname_str;
	    lvstruct.lParam   = 0;
	    lvstruct.iItem    = i;
	    lvstruct.iSubItem = 0;
	    result = ListView_InsertItem (list, &lvstruct);
	    if (result != -1) {
		width = ListView_GetStringWidth( list, devname_str) + 15;
		if( width > listview_column_width[0] )
		    listview_column_width[0] = width;

		ListView_SetItemText( list, result, 1, volname_str );
		width = ListView_GetStringWidth( list, volname_str ) + 15;
		if( width > listview_column_width[ 1 ] )
		    listview_column_width[ 1 ] = width;

		listview_column_width [ 2 ] = 150;
		ListView_SetItemText( list, result, 2, rootdir );
		width = ListView_GetStringWidth( list, rootdir ) + 15;
		if( width > listview_column_width[ 2 ] )
		    listview_column_width[ 2 ] = width;

		ListView_SetItemText( list, result, 3, readwrite_str );
		width = ListView_GetStringWidth( list, readwrite_str ) + 15;
		if( width > listview_column_width[ 3 ] )
		    listview_column_width[ 3 ] = width;

		ListView_SetItemText( list, result, 4, blocksize_str );
		width = ListView_GetStringWidth( list, blocksize_str ) + 15;
		if( width > listview_column_width[ 4 ] )
		    listview_column_width[ 4 ] = width;

		ListView_SetItemText( list, result, 5, size_str );
		width = ListView_GetStringWidth( list, size_str ) + 15;
		if( width > listview_column_width[ 5 ] )
		    listview_column_width[ 5 ] = width;

		ListView_SetItemText( list, result, 6, bootpri_str );
		width = ListView_GetStringWidth( list, bootpri_str ) + 15;
		if( width > listview_column_width[ 6 ] )
		    listview_column_width[ 6 ] = width;
	    }
	}
#endif
    }

    if( result != -1 )
    {
	if( GetWindowRect( list, &rect ) )
	{
	    ScreenToClient( hDlg, (LPPOINT)&rect );
	    ScreenToClient( hDlg, (LPPOINT)&rect.right );
	    if( listview_num_columns == 2 )
	    {
		if( ( temp = rect.right - rect.left - listview_column_width[ 0 ] - 4 ) > listview_column_width[1] )
		    listview_column_width[1] = temp;
	    }
	}

	// Adjust our column widths so that we can see the contents...
	for( i = 0; i < listview_num_columns; i++ )
	{
	    ListView_SetColumnWidth( list, i, listview_column_width[i] );
	}

	// Turn on full-row-select option
	ListView_SetExtendedListViewStyle( list, LVS_EX_FULLROWSELECT );

	// Redraw the items in the list...
	items = ListView_GetItemCount( list );
	ListView_RedrawItems( list, 0, items );
    }
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
    DWORD pos = GetMessagePos ();
    int items, entry;

    point.x = LOWORD (pos);
    point.y = HIWORD (pos);
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
	ListView_GetItemRect (list, entry, &rect, LVIR_BOUNDS);
	if (PtInRect (&rect, point)) {
	    int i, x = 0;
	    UINT flag = LVIS_SELECTED | LVIS_FOCUSED;
	    ListView_SetItemState (list, entry, flag, flag);
	    for (i = 0; i < listview_num_columns && column; i++) {
		if (x < point.x && x + listview_column_width[i] > point.x) {
		    *column = i;
		    break;
		}
		x += listview_column_width[i];
	    }
	    return entry;
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
	
	    switch( wParam ) 
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
	
	    GetDlgItemText( hDlg, IDC_PATH_NAME, workprefs.info, sizeof workprefs.info );
	    recursive--;
	break;
    }
    return FALSE;
}

static HTREEITEM AddConfigNode (HWND hDlg, struct ConfigStruct *config, char *name, char *desc, char *path, int isdir, int expand, HTREEITEM parent)
{
    TVINSERTSTRUCT is;
    HWND TVhDlg;
    char s[MAX_DPATH] = "";
    char file_name[MAX_DPATH], file_path[MAX_DPATH];

    GetDlgItemText (hDlg, IDC_EDITNAME, file_name, MAX_DPATH);
    GetDlgItemText (hDlg, IDC_EDITPATH, file_path, MAX_DPATH);
    TVhDlg = GetDlgItem(hDlg, IDC_CONFIGTREE);
    memset (&is, 0, sizeof (is));
    is.hInsertAfter = isdir < 0 ? TVI_ROOT : TVI_SORT;
    is.hParent = parent;
    is.itemex.mask = TVIF_TEXT | TVIF_STATE | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM;
    if (!strcmp (file_name, name) && !strcmp (file_path, path)) {
	is.itemex.state |= TVIS_SELECTED;
	is.itemex.stateMask |= TVIS_SELECTED;
    }
    if (isdir) {
	strcat (s, " ");
	is.itemex.state |= TVIS_BOLD;
	is.itemex.stateMask |= TVIS_BOLD;
    }
    if (expand) {
	is.itemex.state |= TVIS_EXPANDED;
	is.itemex.stateMask |= TVIS_EXPANDED;
    }
    strcat (s, name);
    if (strlen (s) > 4 && !stricmp (s + strlen (s) - 4, ".uae"))
	s[strlen(s) - 4] = 0;
    if (desc && strlen(desc) > 0) {
	strcat (s, " (");
	strcat (s, desc);
	strcat (s, ")");
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
			    if (!config->hardware && !config->host)
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
	SetDlgItemText (hDlg, IDC_EDITNAME, "");
	SetDlgItemText (hDlg, IDC_EDITDESCRIPTION, "");
    } else {
	SetDlgItemText (hDlg, IDC_EDITNAME, config->Name);
	SetDlgItemText (hDlg, IDC_EDITDESCRIPTION, config->Description);
    }
    SendDlgItemMessage (hDlg, IDC_CONFIGLINK, CB_RESETCONTENT, 0, 0L);
    SendDlgItemMessage (hDlg, IDC_CONFIGLINK, CB_ADDSTRING, 0, (LPARAM)"");
    idx1 = 1;
    idx2 = 0;
    for (j = 0; j < 2; j++) {
	for (i = 0; i < configstoresize; i++) {
	    struct ConfigStruct *cs = configstore[i];
	    if ((j == 0 && cs->Type == CONFIG_TYPE_HOST) || (j == 1 && cs->Type == CONFIG_TYPE_HARDWARE)) {
		char tmp2[MAX_DPATH];
		strcpy (tmp2, configstore[i]->Path);
		strncat (tmp2, configstore[i]->Name, MAX_DPATH);
		SendDlgItemMessage (hDlg, IDC_CONFIGLINK, CB_ADDSTRING, 0, (LPARAM)tmp2);
		if (config && (!strcmpi (tmp2, config->HardwareLink) || !strcmpi (tmp2, config->HostLink)))
		    idx2 = idx1;
		idx1++;
	    }
	}
    }
    SendDlgItemMessage (hDlg, IDC_CONFIGLINK, CB_SETCURSEL, idx2, 0);
}

static HTREEITEM InitializeConfigTreeView (HWND hDlg)
{
    HIMAGELIST himl = ImageList_Create (16, 16, ILC_COLOR8 | ILC_MASK, 3, 0);
    HWND TVhDlg = GetDlgItem(hDlg, IDC_CONFIGTREE);
    HTREEITEM parent;
    char path[MAX_DPATH];
    int i;

    if (himl) {
	HICON icon;
	icon = LoadIcon (hInst, (LPCSTR)MAKEINTRESOURCE(IDI_FOLDER));
	ImageList_AddIcon (himl, icon);
	icon = LoadIcon (hInst, (LPCSTR)MAKEINTRESOURCE(IDI_CONFIGFILE));
	ImageList_AddIcon (himl, icon);
	icon = LoadIcon (hInst, (LPCSTR)MAKEINTRESOURCE(IDI_ROOT));
	ImageList_AddIcon (himl, icon);
	TreeView_SetImageList (TVhDlg, himl, TVSIL_NORMAL);
    }
    for (i = 0; i < configstoresize; i++)
	configstore[i]->item = NULL;
    TreeView_DeleteAllItems (TVhDlg);
    GetConfigPath (path, NULL, FALSE);
    parent = AddConfigNode (hDlg, NULL, path, NULL, NULL, 0, 1, NULL);
    LoadConfigTreeView (hDlg, -1, parent);
    return parent;
}

static void ConfigToRegistry (struct ConfigStruct *config, int type)
{
    if (hWinUAEKey && config) {
	char path[MAX_DPATH];
	strcpy (path, config->Path);
	strncat (path, config->Name, MAX_DPATH);
	RegSetValueEx (hWinUAEKey, configreg[type], 0, REG_SZ, (CONST BYTE *)path, strlen(path) + 1);
    }
}
static void ConfigToRegistry2 (DWORD ct, int type, DWORD noauto)
{
    if (!hWinUAEKey)
	return;
    if (type > 0)
	RegSetValueEx (hWinUAEKey, configreg2[type], 0, REG_DWORD, (CONST BYTE *)&ct, sizeof (ct));
    if (noauto == 0 || noauto == 1)
	RegSetValueEx (hWinUAEKey, "ConfigFile_NoAuto", 0, REG_DWORD, (CONST BYTE *)&noauto, sizeof (noauto));
}

static void checkautoload (HWND	hDlg, struct ConfigStruct *config)
{
    int ct = 0;
    DWORD dwType = REG_DWORD;
    DWORD dwRFPsize = sizeof (ct);

    if (configtypepanel > 0)
	RegQueryValueEx (hWinUAEKey, configreg2[configtypepanel], 0, &dwType, (LPBYTE)&ct, &dwRFPsize);
    if (!config || config->Directory) {
	ct = 0;
	ConfigToRegistry2 (ct, configtypepanel, -1);
    }
    CheckDlgButton(hDlg, IDC_CONFIGAUTO, ct ? BST_CHECKED : BST_UNCHECKED);
    EnableWindow (GetDlgItem (hDlg, IDC_CONFIGAUTO), configtypepanel > 0 && config && !config->Directory ? TRUE : FALSE);
    RegQueryValueEx (hWinUAEKey, "ConfigFile_NoAuto", 0, &dwType, (LPBYTE)&ct, &dwRFPsize);
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
    char name_buf[MAX_DPATH];

    EnableWindow (GetDlgItem( hDlg, IDC_VIEWINFO ), workprefs.info[0]);
    SetDlgItemText (hDlg, IDC_EDITNAME, config_filename);
    SetDlgItemText (hDlg, IDC_EDITPATH, "");
    SetDlgItemText (hDlg, IDC_EDITDESCRIPTION, workprefs.description);
    root = InitializeConfigTreeView (hDlg);
    if (hWinUAEKey) {
	DWORD dwType = REG_SZ;
	DWORD dwRFPsize = sizeof (name_buf);
	char path[MAX_DPATH];
	if (RegQueryValueEx (hWinUAEKey, configreg[configtypepanel], 0, &dwType, (LPBYTE)name_buf, &dwRFPsize) == ERROR_SUCCESS) {
	    struct ConfigStruct *config2 = getconfigstorefrompath (name_buf, path, configtypepanel);
	    if (config2)
		config = config2;
	}
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

static INT_PTR CALLBACK LoadSaveDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char *cfgfile;
    static int recursive;
    static struct ConfigStruct *config;

    switch (msg)
    {
    case WM_INITDIALOG:
	recursive++;
	if (!configstore) {
	    CreateConfigStore (NULL);
	    config = NULL;
	}
	pages[LOADSAVE_ID] = hDlg;
	currentpage = LOADSAVE_ID;
	config = initloadsave (hDlg, config);
	recursive--;
	return TRUE;

    case WM_USER:
	break;
	
    case WM_COMMAND:
    {
	recursive++;
	switch (LOWORD (wParam))
	{
	    case IDC_SAVE:
		HandleConfiguration (hDlg, CONFIG_SAVE_FULL, config);
		config = CreateConfigStore (config);
		config = fixloadconfig (hDlg, config);
		ConfigToRegistry (config, configtypepanel);
		InitializeConfigTreeView (hDlg);
		InitializeConfig (hDlg, config);
	    break;
	    case IDC_QUICKSAVE:
		HandleConfiguration (hDlg, CONFIG_SAVE, config);
		config = CreateConfigStore (config);
		config = fixloadconfig (hDlg, config);
		ConfigToRegistry (config, configtypepanel);
		InitializeConfigTreeView (hDlg);
		InitializeConfig (hDlg, config);
	    break;
	    case IDC_QUICKLOAD:
		cfgfile = HandleConfiguration (hDlg, CONFIG_LOAD, config);
		ConfigToRegistry (config, configtypepanel);
		InitializeConfig (hDlg, config);
		if (full_property_sheet) {
		    inputdevice_updateconfig (&workprefs);
		} else {
		    uae_restart (-1, cfgfile);
		    exit_gui(1);
		}
	    break;
	    case IDC_LOAD:
		cfgfile = HandleConfiguration (hDlg, CONFIG_LOAD_FULL, config);
		ConfigToRegistry (config, configtypepanel);
		InitializeConfig (hDlg, config);
		if (full_property_sheet) {
		    inputdevice_updateconfig (&workprefs);
		} else {
		    uae_restart (-1, cfgfile);
		    exit_gui(1);
		}
	    break;
	    case IDC_DELETE:
		HandleConfiguration (hDlg, CONFIG_DELETE, config);
		config = CreateConfigStore (config);
		config = fixloadconfig (hDlg, config);
		InitializeConfigTreeView (hDlg);
	    break;
	    case IDC_VIEWINFO:
		if (workprefs.info[0]) {
		    char name_buf[MAX_DPATH];
		    if (strstr (workprefs.info, "Configurations\\"))
			sprintf (name_buf, "%s\\%s", start_path_data, workprefs.info);
		    else
			strcpy (name_buf, workprefs.info);
		    ShellExecute (NULL, NULL, name_buf, NULL, NULL, SW_SHOWNORMAL);
		}
	    break;
	    case IDC_SETINFO:
		if (DialogBox(hUIDLL ? hUIDLL : hInst, MAKEINTRESOURCE (IDD_SETINFO), hDlg, InfoSettingsProc))
		    EnableWindow( GetDlgItem( hDlg, IDC_VIEWINFO ), workprefs.info[0] );
	    break;
	    case IDC_CONFIGAUTO:
	    if (configtypepanel > 0) {
		int ct = IsDlgButtonChecked (hDlg, IDC_CONFIGAUTO) == BST_CHECKED ? 1 : 0;
		ConfigToRegistry2 (ct, configtypepanel, -1);
	    }
	    break;
	    case IDC_CONFIGNOLINK:
	    if (configtypepanel == 0) {
		int ct = IsDlgButtonChecked (hDlg, IDC_CONFIGNOLINK) == BST_CHECKED ? 1 : 0;
		ConfigToRegistry2 (-1, -1, ct);
	    }
	    break;
	    case IDC_CONFIGLINK:
	    if (HIWORD (wParam) == CBN_SELCHANGE || HIWORD (wParam) == CBN_KILLFOCUS)  {
		LRESULT val;
		char tmp[MAX_DPATH];
		tmp[0] = 0;
		val = SendDlgItemMessage (hDlg, IDC_CONFIGLINK, CB_GETCURSEL, 0, 0L);
		if (val == CB_ERR)
		    SendDlgItemMessage (hDlg, IDC_CONFIGLINK, WM_GETTEXT, (WPARAM)sizeof(tmp), (LPARAM)tmp);
		else
		    SendDlgItemMessage (hDlg, IDC_CONFIGLINK, CB_GETLBTEXT, (WPARAM)val, (LPARAM)tmp);
		strcpy (workprefs.config_host_path, tmp);
	    }
	    break;
	}
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
				cfgfile = HandleConfiguration (hDlg, CONFIG_LOAD, config);
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
    char szContributors1[ MAX_CONTRIBUTORS_LENGTH ];
    char szContributors2[ MAX_CONTRIBUTORS_LENGTH ];
    char szContributors[ MAX_CONTRIBUTORS_LENGTH*2 ];

    switch (msg) {
     case WM_COMMAND:
	if (wParam == ID_OK) {
	    EndDialog (hDlg, 1);
	    return TRUE;
	}
	break;
     case WM_INITDIALOG:
	CharFormat.cbSize = sizeof (CharFormat);

	WIN32GUI_LoadUIString( IDS_CONTRIBUTORS1, szContributors1, MAX_CONTRIBUTORS_LENGTH );
	WIN32GUI_LoadUIString( IDS_CONTRIBUTORS2, szContributors2, MAX_CONTRIBUTORS_LENGTH );
	sprintf( szContributors, "%s%s", szContributors1, szContributors2 );

	SetDlgItemText (hDlg, IDC_CONTRIBUTORS, szContributors );
	SendDlgItemMessage (hDlg, IDC_CONTRIBUTORS, EM_GETCHARFORMAT, 0, (LPARAM) & CharFormat);
	CharFormat.dwMask |= CFM_SIZE | CFM_FACE;
	CharFormat.yHeight = 10 * 20; /* height in twips, where a twip is 1/20th of a point - for a pt.size of 18 */

	strcpy (CharFormat.szFaceName, "Times New Roman");
	SendDlgItemMessage (hDlg, IDC_CONTRIBUTORS, EM_SETCHARFORMAT, SCF_ALL, (LPARAM) & CharFormat);
	/* SendDlgItemMessage(hDlg, IDC_CONTRIBUTORS, EM_SETBKGNDCOLOR,0,GetSysColor( COLOR_3DFACE ) ); */

	return TRUE;
    }
    return FALSE;
}

static void DisplayContributors (HWND hDlg)
{
    DialogBox( hUIDLL ? hUIDLL : hInst, MAKEINTRESOURCE (IDD_CONTRIBUTORS), hDlg, ContributorsProc);
}

typedef struct url_info
{
    int   id;
    BOOL  state;
    char *display;
    char *url;
} urlinfo;

static urlinfo urls[] = 
{
    {IDC_CLOANTOHOME, FALSE, "Cloanto's Amiga Forever", "http://www.amigaforever.com/"},
    {IDC_AMIGAHOME, FALSE, "Amiga Inc.", "http://www.amiga.com"},
    {IDC_PICASSOHOME, FALSE, "Picasso96 Home Page", "http://www.picasso96.cogito.de/"}, 
    {IDC_UAEHOME, FALSE, "UAE Home Page", "http://uae.coresystems.de/"},
    {IDC_WINUAEHOME, FALSE, "WinUAE Home Page", "http://www.winuae.net/"},
    {IDC_AIABHOME, FALSE, "AIAB", "http://www.amigainabox.co.uk/"},
    {IDC_THEROOTS, FALSE, "Back To The Roots", "http://www.back2roots.org/"},
    {IDC_ABIME, FALSE, "abime.net", "http://www.abime.net/"},
    {IDC_CAPS, FALSE, "SPS", "http://www.softpres.org/"},
    {IDC_AMIGASYS, FALSE, "AmigaSYS", "http://amigasys.fw.hu/"},
    {IDC_AMIKIT, FALSE, "AmiKit", "http://amikit.amiga.sk/"},
    { -1, FALSE, NULL, NULL }
};

static void SetupRichText( HWND hDlg, urlinfo *url )
{
    CHARFORMAT CharFormat;
    CharFormat.cbSize = sizeof (CharFormat);

    SetDlgItemText( hDlg, url->id, url->display );
    SendDlgItemMessage( hDlg, url->id, EM_GETCHARFORMAT, 0, (LPARAM)&CharFormat );
    CharFormat.dwMask   |= CFM_UNDERLINE | CFM_SIZE | CFM_FACE | CFM_COLOR;
    CharFormat.dwEffects = url->state ? CFE_UNDERLINE : 0;
    CharFormat.yHeight = 10 * 20; /* height in twips, where a twip is 1/20th of a point - for a pt.size of 18 */

    CharFormat.crTextColor = GetSysColor( COLOR_ACTIVECAPTION );
    strcpy( CharFormat.szFaceName, "Tahoma" );
    SendDlgItemMessage( hDlg, url->id, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&CharFormat );
    SendDlgItemMessage( hDlg, url->id, EM_SETBKGNDCOLOR, 0, GetSysColor( COLOR_3DFACE ) );
}

static void url_handler(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam )
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
	GetWindowRect( GetDlgItem( hDlg, urls[i].id), &rect );
	ScreenToClient( hDlg, (POINT *) &rect );
	ScreenToClient( hDlg, (POINT *) &(rect.right) );
	if( PtInRect( &rect, point ) ) 
	{
	    if( msg == WM_LBUTTONDOWN )
	    {
		ShellExecute (NULL, NULL, urls[i].url , NULL, NULL, SW_SHOWNORMAL);
		SetCursor( LoadCursor( NULL, MAKEINTRESOURCE(IDC_ARROW) ) );
	    }
	    else
	    {
		if( ( i != last_rectangle ) )
		{
		    // try and load the system hand (Win2000+)
		    m_hCursor = LoadCursor(NULL, MAKEINTRESOURCE(IDC_HAND) );
		    if (!m_hCursor)
		    {
			// retry with our fallback hand
			m_hCursor = LoadCursor(hInst, MAKEINTRESOURCE(IDC_MYHAND) );
		    }
		    SetCursor( m_hCursor );
		    urls[i].state = TRUE;
		    SetupRichText( hDlg, &urls[i] );

		    if( last_rectangle != -1 )
		    {
			urls[last_rectangle].state = FALSE;
			SetupRichText( hDlg, &urls[last_rectangle] );
		    }
		}
	    }
	    last_rectangle = i;
	    found = TRUE;
	    break;
	}
    }

    if( !found && last_rectangle >= 0 )
    {
	SetCursor( LoadCursor( NULL, MAKEINTRESOURCE(IDC_ARROW) ) );
	urls[last_rectangle].state = FALSE;
	SetupRichText( hDlg, &urls[last_rectangle] );
	last_rectangle = -1;
    }
}

static void setpath (HWND hDlg, char *name, DWORD d, char *def)
{
    char tmp[MAX_DPATH];

    strcpy (tmp, def);
    fetch_path (name, tmp, sizeof (tmp));
    SetDlgItemText (hDlg, d, tmp);
}

static void values_to_pathsdialog (HWND hDlg)
{
    setpath (hDlg, "KickstartPath", IDC_PATHS_ROM, "Roms");
    setpath (hDlg, "ConfigurationPath", IDC_PATHS_CONFIG, "Configurations");
    setpath (hDlg, "ScreenshotPath", IDC_PATHS_SCREENSHOT, "ScreenShots");
    setpath (hDlg, "StatefilePath", IDC_PATHS_SAVESTATE, "Savestates");
    setpath (hDlg, "SaveimagePath", IDC_PATHS_SAVEIMAGE, "SaveImages");
    setpath (hDlg, "VideoPath", IDC_PATHS_AVIOUTPUT, "Videos");
}

static void resetregistry (void)
{
    if (!hWinUAEKey)
	return;
    SHDeleteKey (hWinUAEKey, "DetectedROMs");
    RegDeleteValue (hWinUAEKey, "QuickStartMode");
    RegDeleteValue (hWinUAEKey, "ConfigFile");
    RegDeleteValue (hWinUAEKey, "ConfigFileHardware");
    RegDeleteValue (hWinUAEKey, "ConfigFileHost");
    RegDeleteValue (hWinUAEKey, "ConfigFileHardware_Auto");
    RegDeleteValue (hWinUAEKey, "ConfigFileHost_Auto");
    RegDeleteValue (hWinUAEKey, "ConfigurationPath");
    RegDeleteValue (hWinUAEKey, "SaveimagePath");
    RegDeleteValue (hWinUAEKey, "ScreenshotPath");
    RegDeleteValue (hWinUAEKey, "StatefilePath");
    RegDeleteValue (hWinUAEKey, "VideoPath");
    RegDeleteValue (hWinUAEKey, "QuickStartModel");
    RegDeleteValue (hWinUAEKey, "QuickStartConfiguration");
    RegDeleteValue (hWinUAEKey, "QuickStartCompatibility");
    RegDeleteValue (hWinUAEKey, "QuickStartHostConfig");
}

int path_type;
static INT_PTR CALLBACK PathsDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int recursive;
    static int ptypes[3], numtypes;
    int val, selpath = 0;
    char tmp[MAX_DPATH], pathmode[32];

    switch (msg)
    {
	case WM_INITDIALOG:
	recursive++;
	pages[PATHS_ID] = hDlg;
	currentpage = PATHS_ID;
#if !WINUAEBETA
	ShowWindow (GetDlgItem (hDlg, IDC_RESETREGISTRY), FALSE);
#endif
	numtypes = 0;
        SendDlgItemMessage (hDlg, IDC_PATHS_DEFAULTTYPE, CB_RESETCONTENT, 0, 0L);
	if (af_path_2005) {
	    WIN32GUI_LoadUIString(IDS_DEFAULT_AF2005, tmp, sizeof tmp);
	    SendDlgItemMessage (hDlg, IDC_PATHS_DEFAULTTYPE, CB_ADDSTRING, 0, (LPARAM)tmp);
	    if (path_type == 2005)
		selpath = numtypes;
	    ptypes[numtypes++] = 2005;
	}
	if (af_path_old) {
	    WIN32GUI_LoadUIString(IDS_DEFAULT_AF, tmp, sizeof tmp);
	    SendDlgItemMessage (hDlg, IDC_PATHS_DEFAULTTYPE, CB_ADDSTRING, 0, (LPARAM)tmp);
	    if (path_type == 1)
		selpath = numtypes;
	    ptypes[numtypes++] = 1;
	}
	if (winuae_path || numtypes == 0) {
	    WIN32GUI_LoadUIString(IDS_DEFAULT_WINUAE, tmp, sizeof tmp);
	    SendDlgItemMessage (hDlg, IDC_PATHS_DEFAULTTYPE, CB_ADDSTRING, 0, (LPARAM)tmp);
	    if (path_type == 0)
		selpath = numtypes;
	    ptypes[numtypes++] = 0;
	}
        SendDlgItemMessage (hDlg, IDC_PATHS_DEFAULTTYPE, CB_SETCURSEL, selpath, 0);
	if (numtypes > 1) {
	    EnableWindow(GetDlgItem (hDlg, IDC_PATHS_DEFAULTTYPE), TRUE);
	    ShowWindow(GetDlgItem (hDlg, IDC_PATHS_DEFAULTTYPE), TRUE);
	} else {
	    EnableWindow(GetDlgItem (hDlg, IDC_PATHS_DEFAULTTYPE), FALSE);
	    ShowWindow(GetDlgItem (hDlg, IDC_PATHS_DEFAULTTYPE), FALSE);
	}
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
	    fetch_path ("KickstartPath", tmp, sizeof (tmp));
	    if (DirectorySelection (hDlg, 0, tmp)) {
		if (tmp[strlen (tmp) - 1] != '\\')
		    strcat (tmp, "\\");
		if (!scan_roms (tmp)) 
		    gui_message_id (IDS_ROMSCANNOROMS);
		set_path ("KickstartPath", tmp);
		values_to_pathsdialog (hDlg);
	    }
	    break;
	    case IDC_PATHS_ROM:
	    GetWindowText (GetDlgItem (hDlg, IDC_PATHS_ROM), tmp, sizeof (tmp));
	    set_path ("KickstartPath", tmp);
	    break;
	    case IDC_PATHS_CONFIGS:
	    fetch_path ("ConfigurationPath", tmp, sizeof (tmp));
	    if (DirectorySelection (hDlg, 0, tmp)) {
		set_path ("ConfigurationPath", tmp);
		values_to_pathsdialog (hDlg);
		FreeConfigStore ();
	    }
	    break;
	    case IDC_PATHS_CONFIG:
	    GetWindowText (GetDlgItem (hDlg, IDC_PATHS_CONFIG), tmp, sizeof (tmp));
	    set_path ("ConfigurationPath", tmp);
	    FreeConfigStore ();
	    break;
	    case IDC_PATHS_SCREENSHOTS:
	    fetch_path ("ScreenshotPath", tmp, sizeof (tmp));
	    if (DirectorySelection (hDlg, 0, tmp)) {
		set_path ("ScreenshotPath", tmp);
		values_to_pathsdialog (hDlg);
	    }
	    break;
	    case IDC_PATHS_SCREENSHOT:
	    GetWindowText (GetDlgItem (hDlg, IDC_PATHS_SCREENSHOT), tmp, sizeof (tmp));
	    set_path ("ScreenshotPath", tmp);
	    break;
	    case IDC_PATHS_SAVESTATES:
	    fetch_path ("StatefilePath", tmp, sizeof (tmp));
	    if (DirectorySelection (hDlg, 0, tmp)) {
		set_path ("StatefilePath", tmp);
		values_to_pathsdialog (hDlg);
	    }
	    break;
	    case IDC_PATHS_SAVESTATE:
	    GetWindowText (GetDlgItem (hDlg, IDC_PATHS_SAVESTATE), tmp, sizeof (tmp));
	    set_path ("StatefilePath", tmp);
	    break;
	    case IDC_PATHS_SAVEIMAGES:
	    fetch_path ("SaveimagePath", tmp, sizeof (tmp));
	    if (DirectorySelection (hDlg, 0, tmp)) {
		set_path ("SaveimagePath", tmp);
		values_to_pathsdialog (hDlg);
	    }
	    break;
	    case IDC_PATHS_SAVEIMAGE:
	    GetWindowText (GetDlgItem (hDlg, IDC_PATHS_SAVEIMAGE), tmp, sizeof (tmp));
	    set_path ("SaveimagePath", tmp);
	    break;
	    case IDC_PATHS_AVIOUTPUTS:
	    fetch_path ("VideoPath", tmp, sizeof (tmp));
	    if (DirectorySelection (hDlg, 0, tmp)) {
		set_path ("VideoPath", tmp);
		values_to_pathsdialog (hDlg);
	    }
	    break;
	    case IDC_PATHS_AVIOUTPUT:
	    GetWindowText (GetDlgItem (hDlg, IDC_PATHS_AVIOUTPUT), tmp, sizeof (tmp));
	    set_path ("VideoPath", tmp);
	    break;
	    case IDC_PATHS_DEFAULT:	
	    val = SendDlgItemMessage (hDlg, IDC_PATHS_DEFAULTTYPE, CB_GETCURSEL, 0, 0L);
	    if (val != CB_ERR && val >= 0 && val < numtypes) {
		char *p = my_strdup (start_path_data);
		int pp1 = af_path_2005, pp2 = af_path_old;
		val = ptypes[val];
		if (val == 0) {
		    strcpy (start_path_data, start_path_exe);
		    af_path_2005 = af_path_old = 0;
		    path_type = 0;
		    strcpy (pathmode, "WinUAE");
		} else if (val == 1) {
		    strcpy (start_path_data, start_path_exe);
		    af_path_2005 = 0;
		    strcpy (pathmode, "AF");
		    path_type = 1;
		} else {
		    strcpy (pathmode, "AF2005");
		    path_type = 2005;
		    strcpy (start_path_data, start_path_af);
		}
		if (hWinUAEKey)
		    RegSetValueEx (hWinUAEKey, "PathMode", 0, REG_SZ, (CONST BYTE *)pathmode, strlen(pathmode) + 1);
		set_path ("KickstartPath", NULL);
		if (path_type == 2005)
		    strcat (start_path_data, "WinUAE\\");
		set_path ("ConfigurationPath", NULL);
		set_path ("ScreenshotPath", NULL);
		set_path ("StatefilePath", NULL);
		set_path ("SaveimagePath", NULL);
		set_path ("VideoPath", NULL);
		values_to_pathsdialog (hDlg);
		FreeConfigStore ();
    		strcpy (start_path_data, p);
		xfree (p);
		af_path_2005 = pp1;
		af_path_old = pp2;
	    }
	    break;
	    case IDC_ROM_RESCAN:
	    scan_roms (NULL);
	    break;
	    case IDC_RESETREGISTRY:
	    resetregistry ();
	    break;
	    case IDC_RESETDISKHISTORY:
	    reset_disk_history ();
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
    { 3, IDS_QS_MODEL_CD32 }, // "CD32"
    { 4, IDS_QS_MODEL_CDTV }, // "CDTV"
    { 4, IDS_QS_MODEL_ARCADIA }, // "Arcadia"
    { 0, 0 },
    { 0, 0 },
    { 1, IDS_QS_MODEL_UAE }, // "Expanded UAE example configuration"
    { -1 }
};

static DWORD quickstart_model = 0, quickstart_conf = 0, quickstart_compa = 1;
static int quickstart_ok, quickstart_ok_floppy;
static void addfloppytype (HWND hDlg, int n);

static void enable_for_quickstart (HWND hDlg)
{
    int v = quickstart_ok && quickstart_ok_floppy ? TRUE : FALSE;
    EnableWindow (GetDlgItem (guiDlg, IDC_RESETAMIGA), !full_property_sheet ? TRUE : FALSE);
    ShowWindow (GetDlgItem (hDlg, IDC_QUICKSTART_SETCONFIG), quickstart ? SW_HIDE : SW_SHOW);
}

static void load_quickstart (HWND hDlg, int romcheck)
{
    EnableWindow (GetDlgItem (guiDlg, IDC_RESETAMIGA), FALSE);
    quickstart_ok = build_in_prefs (&workprefs, quickstart_model, quickstart_conf, quickstart_compa, romcheck);
    enable_for_quickstart (hDlg);
    addfloppytype (hDlg, 0);
    addfloppytype (hDlg, 1);
}

static void quickstarthost (HWND hDlg, char *name)
{
    int type = CONFIG_TYPE_HOST;
    char tmp[MAX_DPATH];

    if (getconfigstorefrompath (name, tmp, CONFIG_TYPE_HOST)) {
	if (cfgfile_load (&workprefs, tmp, &type, 1))
	    workprefs.start_gui = 1;
    }
}

static void init_quickstartdlg_tooltip (HWND hDlg, char *tt)
{
    TOOLINFO ti;

    ti.cbSize = sizeof(TOOLINFO);
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
    int i, j, idx, idx2;
    DWORD dwType, qssize;
    char tmp1[2 * MAX_DPATH], tmp2[MAX_DPATH], hostconf[MAX_DPATH];
    char *p1, *p2;

    qssize = sizeof (tmp1);
    RegQueryValueEx (hWinUAEKey, "QuickStartHostConfig", 0, &dwType, (LPBYTE)hostconf, &qssize);
    if (firsttime == 0) {
	if (hWinUAEKey) {
	    qssize = sizeof (quickstart_model);
	    RegQueryValueEx (hWinUAEKey, "QuickStartModel", 0, &dwType, (LPBYTE)&quickstart_model, &qssize);
	    qssize = sizeof (quickstart_conf);
	    RegQueryValueEx (hWinUAEKey, "QuickStartConfiguration", 0, &dwType, (LPBYTE)&quickstart_conf, &qssize);
	    qssize = sizeof (quickstart_compa);
	    RegQueryValueEx (hWinUAEKey, "QuickStartCompatibility", 0, &dwType, (LPBYTE)&quickstart_compa, &qssize);
	}
	if (quickstart) {
	    workprefs.df[0][0] = 0;
	    workprefs.df[1][0] = 0;
	    workprefs.df[2][0] = 0;
	    workprefs.df[3][0] = 0;
	    load_quickstart (hDlg, 1);
	    quickstarthost (hDlg, hostconf);
	}
       firsttime = 1;
    }

    CheckDlgButton (hDlg, IDC_QUICKSTARTMODE, quickstart);

    WIN32GUI_LoadUIString (IDS_QS_MODELS, tmp1, sizeof (tmp1));
    strcat (tmp1, "\n");
    p1 = tmp1;
    SendDlgItemMessage (hDlg, IDC_QUICKSTART_MODEL, CB_RESETCONTENT, 0, 0L);
    idx = idx2 = 0;
    i = 0;
    while (amodels[i].compalevels >= 0) {
	if (amodels[i].compalevels > 0) {
	    p2 = strchr (p1, '\n');
	    if (p2 && strlen (p2) > 0) {
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

    WIN32GUI_LoadUIString (amodels[quickstart_model].id, tmp1, sizeof (tmp1));
    strcat (tmp1, "\n");
    p1 = tmp1;
    init_quickstartdlg_tooltip (hDlg, 0);
    SendDlgItemMessage (hDlg, IDC_QUICKSTART_CONFIGURATION, CB_RESETCONTENT, 0, 0L);
    i = 0;
    for (;;) {
	p2 = strchr (p1, '\n');
	if (!p2)
	    break;
	*p2++= 0;
	SendDlgItemMessage (hDlg, IDC_QUICKSTART_CONFIGURATION, CB_ADDSTRING, 0, (LPARAM)p1);
	p1 = p2;
	p2 = strchr (p1, '\n');
	if (!p2)
	    break;
	*p2++= 0;
	if (quickstart_conf == i && strlen (p1) > 0)
	    init_quickstartdlg_tooltip (hDlg, p1);
	p1 = p2;
	i++;
    }
    if (quickstart_conf >= i)
	quickstart_conf = 0;
    SendDlgItemMessage (hDlg, IDC_QUICKSTART_CONFIGURATION, CB_SETCURSEL, quickstart_conf, 0);
    
    if (quickstart_compa >= amodels[quickstart_model].compalevels)
	quickstart_compa = 1;
    if (quickstart_compa >= amodels[quickstart_model].compalevels)
	quickstart_compa = 0;
    i = amodels[quickstart_model].compalevels;
    EnableWindow (GetDlgItem (hDlg, IDC_QUICKSTART_COMPATIBILITY), i > 1);
    SendDlgItemMessage (hDlg, IDC_QUICKSTART_COMPATIBILITY, TBM_SETRANGE, TRUE, MAKELONG (0, i > 1 ? i - 1 : 1));
    SendDlgItemMessage (hDlg, IDC_QUICKSTART_COMPATIBILITY, TBM_SETPAGESIZE, 0, 1);
    SendDlgItemMessage( hDlg, IDC_QUICKSTART_COMPATIBILITY, TBM_SETPOS, TRUE, quickstart_compa);

    SendDlgItemMessage (hDlg, IDC_QUICKSTART_HOSTCONFIG, CB_RESETCONTENT, 0, 0L);
    WIN32GUI_LoadUIString (IDS_DEFAULT_HOST, tmp1, sizeof (tmp1));
    SendDlgItemMessage (hDlg, IDC_QUICKSTART_HOSTCONFIG, CB_ADDSTRING, 0, (LPARAM)tmp1);
    idx = 0;
    j = 1;
    for (i = 0; i < configstoresize; i++) {
	if (configstore[i]->Type == CONFIG_TYPE_HOST) {
	    strcpy (tmp2, configstore[i]->Path);
	    strncat (tmp2, configstore[i]->Name, MAX_DPATH);
	    if (!strcmp (tmp2, hostconf))
		idx = j;
	    SendDlgItemMessage (hDlg, IDC_QUICKSTART_HOSTCONFIG, CB_ADDSTRING, 0, (LPARAM)tmp2);
	    j++;
	}
    }
    SendDlgItemMessage (hDlg, IDC_QUICKSTART_HOSTCONFIG, CB_SETCURSEL, idx, 0);

    if (hWinUAEKey) {
	RegSetValueEx (hWinUAEKey, "QuickStartModel", 0, REG_DWORD, (CONST BYTE *)&quickstart_model, sizeof(quickstart_model));
	RegSetValueEx (hWinUAEKey, "QuickStartConfiguration", 0, REG_DWORD, (CONST BYTE *)&quickstart_conf, sizeof(quickstart_conf));
	RegSetValueEx (hWinUAEKey, "QuickStartCompatibility", 0, REG_DWORD, (CONST BYTE *)&quickstart_compa, sizeof(quickstart_compa));
    }
}

static void floppytooltip (HWND hDlg, int num, uae_u32 crc32);
static void testimage (HWND hDlg, int num)
{
    int ret;
    int reload = 0;
    uae_u32 crc32;
    int messageid = -1;
    char tmp[MAX_DPATH];

    floppytooltip (hDlg, num, 0);
    quickstart_ok_floppy = 0;
    if (workprefs.dfxtype[0] < 0) {
	quickstart_ok_floppy = 1;
	return;
    }
    if (!workprefs.df[num][0])
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
	if (quickstart_model != 1 && quickstart_model != 2 && quickstart_model != 4) {
	    quickstart_model = 4;
	    messageid = IDS_IMGCHK_KS2;
	    reload = 1;
	}
	break;
	case 12:
	quickstart_ok_floppy = 1;
	if (quickstart_model != 4) {
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
	WIN32GUI_LoadUIString (messageid, tmp, sizeof (tmp));
	gui_message (tmp);
    }
    if (reload && quickstart) {
	load_quickstart (hDlg, 1);
	init_quickstartdlg (hDlg);
    }
}

static INT_PTR CALLBACK FloppyDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

static INT_PTR CALLBACK QuickstartDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int recursive;
    int ret = FALSE, i;
    char tmp[MAX_DPATH];
    static char df0[MAX_DPATH];
    static char df1[MAX_DPATH];
    static int dfxtype[2] = { -1, -1 };
    static int doinit;
    LRESULT val;

    switch( msg )
    {
	case WM_INITDIALOG:
	    pages[QUICKSTART_ID] = hDlg;
	    currentpage = QUICKSTART_ID;
	    enable_for_quickstart (hDlg);
	    strcpy (df0, workprefs.df[0]);
	    strcpy (df1, workprefs.df[1]);
	    doinit = 1;
	    break;
	case WM_NULL:
	    if (recursive > 0)
		break;
	    recursive++;
	    if (doinit) {
		addfloppytype (hDlg, 0);
		addfloppytype (hDlg, 1);
		init_quickstartdlg (hDlg);
	    }
	    doinit = 0;
	    recursive--;
	break;

	case WM_COMMAND:
	if (recursive > 0)
	    break;
	recursive++;
	if (HIWORD (wParam) == CBN_SELCHANGE || HIWORD (wParam) == CBN_KILLFOCUS)  {
	    switch (LOWORD (wParam))
	    {
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
		    if (hWinUAEKey)
			RegSetValueEx (hWinUAEKey, "QuickStartHostConfig", 0, REG_SZ, (CONST BYTE *)&tmp, strlen (tmp) + 1);
		    quickstarthost (hDlg, tmp);
		    if (val == 0 && quickstart)
			load_quickstart (hDlg, 0);
		}
		break;
	    }
	} else {
	    switch (LOWORD (wParam))
	    {
		case IDC_QUICKSTARTMODE:
		quickstart = IsDlgButtonChecked (hDlg, IDC_QUICKSTARTMODE);
		if (hWinUAEKey)
		    RegSetValueEx( hWinUAEKey, "QuickStartMode", 0, REG_DWORD, (CONST BYTE *)&quickstart, sizeof(quickstart));
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
	if (strcmp (workprefs.df[0], df0) || workprefs.dfxtype[0] != dfxtype[0]) {
	    strcpy (df0, workprefs.df[0]);
	    dfxtype[0] = workprefs.dfxtype[0];
	    testimage (hDlg, 0);
	    enable_for_quickstart (hDlg);
	}
	if (strcmp (workprefs.df[1], df1) || workprefs.dfxtype[1] != dfxtype[1]) {
	    strcpy (df1, workprefs.df[1]);
	    dfxtype[1] = workprefs.dfxtype[1];
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

    SetDlgItemText (hDlg, IDC_RICHEDIT1, "WinUAE");
    SendDlgItemMessage (hDlg, IDC_RICHEDIT1, EM_GETCHARFORMAT, 0, (LPARAM) & CharFormat);
    CharFormat.dwMask |= CFM_BOLD | CFM_SIZE | CFM_FACE;
    CharFormat.dwEffects = CFE_BOLD;
    CharFormat.yHeight = 18 * 20; /* height in twips, where a twip is 1/20th of a point - for a pt.size of 18 */

    strcpy (CharFormat.szFaceName, "Times New Roman");
    SendDlgItemMessage (hDlg, IDC_RICHEDIT1, EM_SETCHARFORMAT, SCF_ALL, (LPARAM) & CharFormat);
    SendDlgItemMessage (hDlg, IDC_RICHEDIT1, EM_SETBKGNDCOLOR, 0, GetSysColor (COLOR_3DFACE));

    SetDlgItemText (hDlg, IDC_RICHEDIT2, VersionStr );
    SendDlgItemMessage (hDlg, IDC_RICHEDIT2, EM_GETCHARFORMAT, 0, (LPARAM) & CharFormat);
    CharFormat.dwMask |= CFM_SIZE | CFM_FACE;
    CharFormat.yHeight = 10 * 20;
    strcpy (CharFormat.szFaceName, "Times New Roman");
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
    EnableWindow (GetDlgItem (hDlg, IDC_PFULLSCREEN), rtg);
    if (!full_property_sheet)  {
	/* Disable certain controls which are only to be set once at start-up... */
	EnableWindow (GetDlgItem (hDlg, IDC_TEST16BIT), FALSE);
    } else {
	CheckDlgButton( hDlg, IDC_VSYNC, workprefs.gfx_vsync);
	EnableWindow (GetDlgItem (hDlg, IDC_XCENTER), TRUE);
	EnableWindow (GetDlgItem (hDlg, IDC_YCENTER), TRUE);
	EnableWindow (GetDlgItem (hDlg, IDC_LM_SCANLINES), TRUE);
    }
    EnableWindow (GetDlgItem (hDlg, IDC_FRAMERATE2), !workprefs.gfx_vsync);
    EnableWindow (GetDlgItem (hDlg, IDC_FRAMERATE), !workprefs.cpu_cycle_exact);
    EnableWindow (GetDlgItem (hDlg, IDC_LORES), !workprefs.gfx_autoresolution);
    EnableWindow (GetDlgItem (hDlg, IDC_LM_NORMAL), !workprefs.gfx_autoresolution);
    EnableWindow (GetDlgItem (hDlg, IDC_LM_DOUBLED), !workprefs.gfx_autoresolution);
    EnableWindow (GetDlgItem (hDlg, IDC_LM_SCANLINES), !workprefs.gfx_autoresolution);
}

static void enable_for_chipsetdlg (HWND hDlg)
{
    int enable = workprefs.cpu_cycle_exact ? FALSE : TRUE;

#if !defined (CPUEMU_6)
    EnableWindow (GetDlgItem (hDlg, IDC_CYCLEEXACT), FALSE);
#endif
    EnableWindow (GetDlgItem (hDlg, IDC_FASTCOPPER), enable);
    EnableWindow (GetDlgItem (hDlg, IDC_GENLOCK), full_property_sheet);
    EnableWindow (GetDlgItem (hDlg, IDC_BLITIMM), enable);
    if (enable == FALSE) {
	workprefs.immediate_blits = 0;
	CheckDlgButton (hDlg, IDC_FASTCOPPER, FALSE);
	CheckDlgButton (hDlg, IDC_BLITIMM, FALSE);
    }
}

static void LoadNthString( DWORD value, char *nth, DWORD dwNthMax )
{
    switch( value )
    {
	case 1:
	    WIN32GUI_LoadUIString(IDS_SECOND, nth, dwNthMax);
	break;

	case 2:
	    WIN32GUI_LoadUIString(IDS_THIRD, nth, dwNthMax);	
	break;
	
	case 3:
	    WIN32GUI_LoadUIString(IDS_FOURTH, nth, dwNthMax);	
	break;
	
	case 4:
	    WIN32GUI_LoadUIString(IDS_FIFTH, nth, dwNthMax);	
	break;
	
	case 5:
	    WIN32GUI_LoadUIString(IDS_SIXTH, nth, dwNthMax);	
	break;
	
	case 6:
	    WIN32GUI_LoadUIString(IDS_SEVENTH, nth, dwNthMax);	
	break;
	
	case 7:
	    WIN32GUI_LoadUIString(IDS_EIGHTH, nth, dwNthMax);	
	break;
	
	case 8:
	    WIN32GUI_LoadUIString(IDS_NINTH, nth, dwNthMax);	
	break;
	
	case 9:
	    WIN32GUI_LoadUIString(IDS_TENTH, nth, dwNthMax);	
	break;
	
	default:
	    strcpy(nth, "");
    }
}

static int fakerefreshrates[] = { 50, 60, 100, 120, 0 };
static int storedrefreshrates[MAX_REFRESH_RATES + 1];

static void init_frequency_combo (HWND hDlg, int dmode)
{
    int i, j, freq, tmp;
    char hz[20], hz2[20], txt[100];
    LRESULT index;
    
    i = 0; index = 0;
    while ((freq = DisplayModes[dmode].refresh[i]) > 0 && index < MAX_REFRESH_RATES) {
	storedrefreshrates[index++] = freq;
	i++;
    }
    i = 0;
    while ((freq = fakerefreshrates[i]) > 0 && index < MAX_REFRESH_RATES) {
	for (j = 0; j < index; j++) {
	    if (storedrefreshrates[j] == freq) break;
	}
	if (j == index)
	    storedrefreshrates[index++] = -freq;
	i++;
    }
    storedrefreshrates[index] = 0;
    for (i = 0; i < index; i++) {
	for (j = i + 1; j < index; j++) {
	    if (abs(storedrefreshrates[i]) >= abs(storedrefreshrates[j])) {
		tmp = storedrefreshrates[i];
		storedrefreshrates[i] = storedrefreshrates[j];
		storedrefreshrates[j] = tmp;
	    }
	}
    }

    hz[0] = hz2[0] = 0;
    SendDlgItemMessage(hDlg, IDC_REFRESHRATE, CB_RESETCONTENT, 0, 0);
    WIN32GUI_LoadUIString (IDS_VSYNC_DEFAULT, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_REFRESHRATE, CB_ADDSTRING, 0, (LPARAM)txt);
    for (i = 0; i < index; i++) {
	freq = storedrefreshrates[i];
	if (freq < 0) {
	    freq = -freq;
	    sprintf (hz, "(%dHz)", freq);
	} else {
	    sprintf (hz, "%dHz", freq);
	}
	if (freq == 50 || freq == 100)
	    strcat (hz, " PAL");
	if (freq == 60 || freq == 120)
	    strcat (hz, " NTSC");
	if (abs(workprefs.gfx_refreshrate) == freq)
	    strcpy (hz2, hz);
	SendDlgItemMessage (hDlg, IDC_REFRESHRATE, CB_ADDSTRING, 0, (LPARAM)hz);
    }
    index = CB_ERR;
    if (hz2[0] >= 0)
	index = SendDlgItemMessage (hDlg, IDC_REFRESHRATE, CB_SELECTSTRING, 0, (LPARAM)hz2);
    if (index == CB_ERR) {
	WIN32GUI_LoadUIString (IDS_VSYNC_DEFAULT, txt, sizeof (txt));
	SendDlgItemMessage(hDlg, IDC_REFRESHRATE, CB_SELECTSTRING, i, (LPARAM)txt);
	workprefs.gfx_refreshrate = 0;
    }
}

#define MAX_FRAMERATE_LENGTH 40
#define MAX_NTH_LENGTH 20

static int display_mode_index(uae_u32 x, uae_u32 y, uae_u32 d)
{
    int i;

    i = 0;
    while (DisplayModes[i].depth >= 0) {
	if (DisplayModes[i].res.width == x &&
	    DisplayModes[i].res.height == y &&
	    DisplayModes[i].depth == d)
	    break;
	i++;
    }
    if(DisplayModes[i].depth < 0)
	i = -1;
    return i;
}

#if 0
static int da_mode_selected;

static int *getp_da (void)
{
    int *p = 0;
    switch (da_mode_selected)
    {
	case 3:
	p = &workprefs.gfx_hue;
	break;
	case 4:
	p = &workprefs.gfx_saturation;
	break;
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
#if 0
    currprefs.gfx_hue = workprefs.gfx_hue;
    currprefs.gfx_saturation = workprefs.gfx_saturation;
    currprefs.gfx_gamma = workprefs.gfx_gamma;
#endif
    currprefs.gfx_luminance = workprefs.gfx_luminance;
    currprefs.gfx_contrast = workprefs.gfx_contrast;
    init_colors ();
    updatedisplayarea ();
    WIN32GFX_WindowMove ();
    init_custom();
}

void init_da (HWND hDlg)
{
    int *p;
    SendDlgItemMessage(hDlg, IDC_DA_MODE, CB_RESETCONTENT, 0, 0);
    SendDlgItemMessage(hDlg, IDC_DA_MODE, CB_ADDSTRING, 0, (LPARAM)"Luminance");
    SendDlgItemMessage(hDlg, IDC_DA_MODE, CB_ADDSTRING, 0, (LPARAM)"Contrast");
#if 0
    SendDlgItemMessage(hDlg, IDC_DA_MODE, CB_ADDSTRING, 0, (LPARAM)"Gamma");
    SendDlgItemMessage(hDlg, IDC_DA_MODE, CB_ADDSTRING, 0, (LPARAM)"Hue");
    SendDlgItemMessage(hDlg, IDC_DA_MODE, CB_ADDSTRING, 0, (LPARAM)"Saturation");
#endif
    if (da_mode_selected == CB_ERR)
	da_mode_selected = 0;
    SendDlgItemMessage (hDlg, IDC_DA_MODE, CB_SETCURSEL, da_mode_selected, 0);
    SendDlgItemMessage (hDlg, IDC_DA_SLIDER, TBM_SETPAGESIZE, 0, 1);
    SendDlgItemMessage (hDlg, IDC_DA_SLIDER, TBM_SETRANGE, TRUE, MAKELONG (-99, 99));
    p = getp_da ();
    if (p)
	SendDlgItemMessage (hDlg, IDC_DA_SLIDER, TBM_SETPOS, TRUE, (*p) / 10);
}
#endif

static int gui_display_depths[3];
static void init_display_mode (HWND hDlg)
{
   int d, d2, index;

   switch (workprefs.color_mode)
    {
    case 2:
	d = 16;
	break;
    case 5:
	d = 32;
	break;
    default:
	d = 8;
	break;
    }

    if (workprefs.gfx_afullscreen)
    {
	d2 = d;
	if ((index = WIN32GFX_AdjustScreenmode(&workprefs.gfx_size_fs.width, &workprefs.gfx_size_fs.height, &d2)) >= 0)
	{
	    switch (d2)
	    {
	    case 15:
		workprefs.color_mode = 1;
		d = 2;
		break;
	    case 16:
		workprefs.color_mode = 2;
		d = 2;
		break;
	    case 32:
		workprefs.color_mode = 5;
		d = 4;
		break;
	    default:
		workprefs.color_mode = 0;
		d = 1;
		break;
	    }
	}
    }
    else
    {
	d = d / 8;
    }

    if ((index = display_mode_index (workprefs.gfx_size_fs.width, workprefs.gfx_size_fs.height, d)) >= 0) {
	int i, cnt;
	SendDlgItemMessage (hDlg, IDC_RESOLUTION, CB_SETCURSEL, DisplayModes[index].residx, 0);
	SendDlgItemMessage(hDlg, IDC_RESOLUTIONDEPTH, CB_RESETCONTENT, 0, 0);
	cnt = 0;
	gui_display_depths[0] = gui_display_depths[1] = gui_display_depths[2] = -1;
	for (i = 0; DisplayModes[i].depth >= 0; i++) {
	    if (DisplayModes[i].residx == DisplayModes[index].residx) {
		char tmp[64];
		sprintf (tmp, "%d", DisplayModes[i].depth * 8);
		SendDlgItemMessage(hDlg, IDC_RESOLUTIONDEPTH, CB_ADDSTRING, 0, (LPARAM)tmp);
		if (DisplayModes[i].depth == d)
		    SendDlgItemMessage (hDlg, IDC_RESOLUTIONDEPTH, CB_SETCURSEL, cnt, 0);
		gui_display_depths[cnt] = DisplayModes[i].depth;
		cnt++;
	    }
	}
	init_frequency_combo (hDlg, index);
    }
}

static void values_to_displaydlg (HWND hDlg)
{
    char buffer[ MAX_FRAMERATE_LENGTH + MAX_NTH_LENGTH ];
    char Nth[ MAX_NTH_LENGTH ];
    LPSTR blah[1] = { Nth };
    LPTSTR string = NULL;
    int v;

    init_display_mode (hDlg);

    SetDlgItemInt (hDlg, IDC_XSIZE, workprefs.gfx_size_win.width, FALSE);
    SetDlgItemInt (hDlg, IDC_YSIZE, workprefs.gfx_size_win.height, FALSE);

    v = workprefs.chipset_refreshrate;
    if (v == 0)
	v = currprefs.ntscmode ? 60 : 50;
    SendDlgItemMessage (hDlg, IDC_FRAMERATE2, TBM_SETPOS, TRUE, v);
    sprintf (buffer, "%d", v);
    SetDlgItemText (hDlg, IDC_RATE2TEXT, buffer);

    v = workprefs.cpu_cycle_exact ? 1 : workprefs.gfx_framerate;
    SendDlgItemMessage (hDlg, IDC_FRAMERATE, TBM_SETPOS, TRUE, v);
    WIN32GUI_LoadUIString( IDS_FRAMERATE, buffer, MAX_FRAMERATE_LENGTH );
    LoadNthString (v - 1, Nth, MAX_NTH_LENGTH);
    if( FormatMessage( FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY | FORMAT_MESSAGE_ALLOCATE_BUFFER,
		       buffer, 0, 0, (LPTSTR)&string, MAX_FRAMERATE_LENGTH + MAX_NTH_LENGTH, (va_list *)blah ) == 0 )
    {
	DWORD dwLastError = GetLastError();
	sprintf (buffer, "Every %s Frame", nth[v - 1]);
	SetDlgItemText( hDlg, IDC_RATETEXT, buffer );
    }
    else
    {
	SetDlgItemText( hDlg, IDC_RATETEXT, string );
	LocalFree( string );
    }

    CheckRadioButton( hDlg, IDC_LM_NORMAL, IDC_LM_SCANLINES, IDC_LM_NORMAL + workprefs.gfx_linedbl );
    CheckDlgButton (hDlg, IDC_AFULLSCREEN, workprefs.gfx_afullscreen);
    CheckDlgButton (hDlg, IDC_PFULLSCREEN, workprefs.gfx_pfullscreen);
    CheckDlgButton (hDlg, IDC_ASPECT, workprefs.gfx_correct_aspect);
    CheckDlgButton (hDlg, IDC_LORES, workprefs.gfx_lores);
    CheckDlgButton (hDlg, IDC_LORES_SMOOTHED, workprefs.gfx_lores_mode);
    CheckDlgButton (hDlg, IDC_VSYNC, workprefs.gfx_vsync);

    CheckDlgButton (hDlg, IDC_XCENTER, workprefs.gfx_xcenter);
    CheckDlgButton (hDlg, IDC_YCENTER, workprefs.gfx_ycenter);

#if 0
    init_da (hDlg);
#endif
}

static void init_resolution_combo (HWND hDlg)
{
    int i = 0, idx = -1;
    char tmp[64];

    SendDlgItemMessage(hDlg, IDC_RESOLUTION, CB_RESETCONTENT, 0, 0);
    while (DisplayModes[i].depth >= 0) {
	if (DisplayModes[i].residx != idx) {
	    sprintf (tmp, "%dx%d", DisplayModes[i].res.width, DisplayModes[i].res.height);
	    SendDlgItemMessage( hDlg, IDC_RESOLUTION, CB_ADDSTRING, 0, (LPARAM)tmp);
	    idx = DisplayModes[i].residx;
	}
	i++;
    }
}
static void init_displays_combo (HWND hDlg)
{
    int i = 0;
    SendDlgItemMessage(hDlg, IDC_DISPLAYSELECT, CB_RESETCONTENT, 0, 0);
    while (Displays[i].name) {
	SendDlgItemMessage(hDlg, IDC_DISPLAYSELECT, CB_ADDSTRING, 0, (LPARAM)Displays[i].name);
	i++;
    }
    if (workprefs.gfx_display >= i)
	workprefs.gfx_display = 0;
    SendDlgItemMessage(hDlg, IDC_DISPLAYSELECT, CB_SETCURSEL, workprefs.gfx_display, 0);
}

static void values_from_displaydlg (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    BOOL success = FALSE;
    int i, j;
    int gfx_width = workprefs.gfx_size_win.width;
    int gfx_height = workprefs.gfx_size_win.height;

    workprefs.gfx_pfullscreen    = IsDlgButtonChecked (hDlg, IDC_PFULLSCREEN);
    workprefs.gfx_afullscreen    = IsDlgButtonChecked (hDlg, IDC_AFULLSCREEN);

    workprefs.gfx_lores          = IsDlgButtonChecked (hDlg, IDC_LORES);
    workprefs.gfx_lores_mode     = IsDlgButtonChecked (hDlg, IDC_LORES_SMOOTHED);
    workprefs.gfx_correct_aspect = IsDlgButtonChecked (hDlg, IDC_ASPECT);
    workprefs.gfx_linedbl = ( IsDlgButtonChecked( hDlg, IDC_LM_SCANLINES ) ? 2 :
			      IsDlgButtonChecked( hDlg, IDC_LM_DOUBLED ) ? 1 : 0 );

    workprefs.gfx_framerate = SendDlgItemMessage (hDlg, IDC_FRAMERATE, TBM_GETPOS, 0, 0);
    workprefs.chipset_refreshrate = SendDlgItemMessage (hDlg, IDC_FRAMERATE2, TBM_GETPOS, 0, 0);
    workprefs.gfx_vsync = IsDlgButtonChecked (hDlg, IDC_VSYNC);

    {
	char buffer[ MAX_FRAMERATE_LENGTH ];
	char Nth[ MAX_NTH_LENGTH ];
	LPSTR blah[1] = { Nth };
	LPTSTR string = NULL;

	WIN32GUI_LoadUIString( IDS_FRAMERATE, buffer, MAX_FRAMERATE_LENGTH );
	LoadNthString( workprefs.gfx_framerate - 1, Nth, MAX_NTH_LENGTH );
	if( FormatMessage( FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY | FORMAT_MESSAGE_ALLOCATE_BUFFER,
			   buffer, 0, 0, (LPTSTR)&string, MAX_FRAMERATE_LENGTH + MAX_NTH_LENGTH, (va_list *)blah ) == 0 )
	{
	    DWORD dwLastError = GetLastError();
	    sprintf (buffer, "Every %s Frame", nth[workprefs.gfx_framerate - 1]);
	    SetDlgItemText( hDlg, IDC_RATETEXT, buffer );
	}
	else
	{
	    SetDlgItemText( hDlg, IDC_RATETEXT, string );
	    LocalFree( string );
	}
	sprintf (buffer, "%d", workprefs.chipset_refreshrate);
	SetDlgItemText (hDlg, IDC_RATE2TEXT, buffer);
	workprefs.gfx_size_win.width  = GetDlgItemInt( hDlg, IDC_XSIZE, &success, FALSE );
	if(!success)
	    workprefs.gfx_size_win.width = 800;
	workprefs.gfx_size_win.height = GetDlgItemInt( hDlg, IDC_YSIZE, &success, FALSE );
	if(!success)
	    workprefs.gfx_size_win.height = 600;
    }
    if (workprefs.chipset_refreshrate == (currprefs.ntscmode ? 60 : 50))
	workprefs.chipset_refreshrate = 0;
    workprefs.gfx_xcenter = (IsDlgButtonChecked (hDlg, IDC_XCENTER) ? 2 : 0 ); /* Smart centering */
    workprefs.gfx_ycenter = (IsDlgButtonChecked (hDlg, IDC_YCENTER) ? 2 : 0 ); /* Smart centering */

    if (msg == WM_COMMAND && HIWORD (wParam) == CBN_SELCHANGE) 
    {
	if (LOWORD (wParam) == IDC_DISPLAYSELECT) {
	    LRESULT posn = SendDlgItemMessage (hDlg, IDC_DISPLAYSELECT, CB_GETCURSEL, 0, 0);
	    if (posn != CB_ERR && posn != workprefs.gfx_display) {
		if (Displays[posn].disabled)
		    posn = 0;
		workprefs.gfx_display = posn;
		DisplayModes = Displays[workprefs.gfx_display].DisplayModes;
		init_resolution_combo (hDlg);
		init_display_mode (hDlg);
	    }
	    return;
	} else if (LOWORD (wParam) == IDC_RESOLUTION || LOWORD(wParam) == IDC_RESOLUTIONDEPTH) {
	    LRESULT posn1, posn2;
	    posn1 = SendDlgItemMessage (hDlg, IDC_RESOLUTION, CB_GETCURSEL, 0, 0);
	    if (posn1 == CB_ERR)
		return;
	    posn2 = SendDlgItemMessage (hDlg, IDC_RESOLUTIONDEPTH, CB_GETCURSEL, 0, 0);
	    if (posn2 == CB_ERR)
		return;
	    for (i = 0; DisplayModes[i].depth >= 0; i++) {
		if (DisplayModes[i].residx == posn1)
		    break;
	    }
	    if (DisplayModes[i].depth < 0)
		return;
	    j = i;
	    while (DisplayModes[i].residx == posn1) {
		if (DisplayModes[i].depth == gui_display_depths[posn2])
		    break;
		i++;
	    }
	    if (DisplayModes[i].residx != posn1)
		i = j;
	    workprefs.gfx_size_fs.width  = DisplayModes[i].res.width;
	    workprefs.gfx_size_fs.height = DisplayModes[i].res.height;
	    switch(DisplayModes[i].depth)
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
		workprefs.gfx_refreshrate = storedrefreshrates[posn1];
	    }
	    values_to_displaydlg (hDlg);
#if 0
	} else if (LOWORD (wParam) == IDC_DA_MODE) {
	    da_mode_selected = SendDlgItemMessage (hDlg, IDC_DA_MODE, CB_GETCURSEL, 0, 0);
	    init_da (hDlg);
	    handle_da (hDlg);
#endif
	}
    }

    updatewinfsmode (&workprefs);
}

static int hw3d_changed;

static INT_PTR CALLBACK DisplayDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int recursive = 0;
    HKEY hPixelFormatKey;
    RGBFTYPE colortype      = RGBFB_NONE;
    DWORD dwType            = REG_DWORD;
    DWORD dwDisplayInfoSize = sizeof( colortype );

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
#if 0
	init_da (hDlg);
#endif

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
	if( ( wParam == IDC_TEST16BIT ) && DirectDraw_Start(NULL) )
	{
	    if( RegOpenKeyEx( HKEY_CURRENT_USER, "Software\\Arabuusimiehet\\WinUAE", 0, KEY_WRITE | KEY_READ, &hPixelFormatKey ) == ERROR_SUCCESS )
	    {
		char szMessage[ 4096 ];
		char szTitle[ MAX_DPATH ];
		WIN32GUI_LoadUIString( IDS_GFXCARDCHECK, szMessage, 4096 );
		WIN32GUI_LoadUIString( IDS_GFXCARDTITLE, szTitle, MAX_DPATH );
		    
		if( MessageBox( NULL, szMessage, szTitle, 
				MB_YESNO | MB_ICONWARNING | MB_TASKMODAL | MB_SETFOREGROUND ) == IDYES )
		{
		    colortype = WIN32GFX_FigurePixelFormats(0);
		    RegSetValueEx( hPixelFormatKey, "DisplayInfo", 0, REG_DWORD, (CONST BYTE *)&colortype, sizeof( colortype ) );
		}
		RegCloseKey( hPixelFormatKey );
	    }
	    DirectDraw_Release();
	}
	else
	{
#if 0
	    handle_da (hDlg);
#endif
	    values_from_displaydlg (hDlg, msg, wParam, lParam);
	    enable_for_displaydlg( hDlg );
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
    char Nth[ MAX_NTH_LENGTH ];
    LPSTR blah[1] = { Nth };
    LPTSTR string = NULL;
    int which_button;

    switch(workprefs.chipset_mask)
    {
    case 0:
	CheckRadioButton( hDlg, IDC_OCS, IDC_AGA, IDC_OCS+0 );
	break;
    case CSMASK_ECS_AGNUS:
	CheckRadioButton( hDlg, IDC_OCS, IDC_AGA, IDC_OCS+1 );
	break;
#if 0
    case CSMASK_ECS_DENISE:
	CheckRadioButton( hDlg, IDC_OCS, IDC_AGA, IDC_OCS+2 );
	break;
#endif
    case CSMASK_ECS_AGNUS | CSMASK_ECS_DENISE:
	CheckRadioButton( hDlg, IDC_OCS, IDC_AGA, IDC_OCS+3 );
	break;
    case CSMASK_AGA:
    case CSMASK_ECS_AGNUS | CSMASK_ECS_DENISE | CSMASK_AGA:
	CheckRadioButton( hDlg, IDC_OCS, IDC_AGA, IDC_OCS+4 );
	break;
    }
    CheckDlgButton (hDlg, IDC_NTSC, workprefs.ntscmode);
    CheckDlgButton (hDlg, IDC_GENLOCK, workprefs.genlock);
    CheckDlgButton (hDlg, IDC_BLITIMM, workprefs.immediate_blits);
    CheckRadioButton (hDlg, IDC_COLLISION0, IDC_COLLISION3, IDC_COLLISION0 + workprefs.collision_level);
    CheckDlgButton (hDlg, IDC_CYCLEEXACT, workprefs.cpu_cycle_exact);
    switch (workprefs.produce_sound) {
     case 0: which_button = IDC_CS_SOUND0; break;
     case 1: case 2: which_button = IDC_CS_SOUND1; break;
     default: which_button = IDC_CS_SOUND2; break;
    }
    CheckRadioButton( hDlg, IDC_CS_SOUND0, IDC_CS_SOUND2, which_button );
}

static void values_from_chipsetdlg (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    BOOL success = FALSE;
    int n, snd;

    workprefs.genlock = IsDlgButtonChecked (hDlg, IDC_FASTCOPPER);
    workprefs.immediate_blits = IsDlgButtonChecked (hDlg, IDC_BLITIMM);
    n = IsDlgButtonChecked (hDlg, IDC_CYCLEEXACT) ? 1 : 0;
    if (workprefs.cpu_cycle_exact != n) {
	workprefs.cpu_cycle_exact = workprefs.blitter_cycle_exact = n;
	if (n) {
	    if (workprefs.cpu_level == 0) {
		workprefs.cpu_compatible = 1;
		workprefs.m68k_speed = 0;
	    }
	    workprefs.immediate_blits = 0;
	    workprefs.gfx_framerate = 1;
	}
    }
    workprefs.collision_level = IsDlgButtonChecked (hDlg, IDC_COLLISION0) ? 0
				 : IsDlgButtonChecked (hDlg, IDC_COLLISION1) ? 1
				 : IsDlgButtonChecked (hDlg, IDC_COLLISION2) ? 2 : 3;
    workprefs.chipset_mask = IsDlgButtonChecked( hDlg, IDC_OCS ) ? 0
			      : IsDlgButtonChecked( hDlg, IDC_ECS_AGNUS ) ? CSMASK_ECS_AGNUS
#if 0
			      : IsDlgButtonChecked( hDlg, IDC_ECS_DENISE ) ? CSMASK_ECS_DENISE
#endif
			      : IsDlgButtonChecked( hDlg, IDC_ECS ) ? CSMASK_ECS_AGNUS | CSMASK_ECS_DENISE
			      : CSMASK_AGA | CSMASK_ECS_AGNUS | CSMASK_ECS_DENISE;
    n = IsDlgButtonChecked (hDlg, IDC_NTSC) ? 1 : 0;
    if (workprefs.ntscmode != n) {
	workprefs.ntscmode = n;
#ifdef AVIOUTPUT
	avioutput_fps = n ? VBLANK_HZ_NTSC : VBLANK_HZ_PAL;
#endif
    }
    snd = IsDlgButtonChecked (hDlg, IDC_CS_SOUND0) ? 0
	: IsDlgButtonChecked (hDlg, IDC_CS_SOUND1) ? 2 : 3;
    if (snd == 0 || snd == 3) {
	workprefs.produce_sound = snd;
    } else if (snd == 2) {
	if (workprefs.produce_sound == 0)
	    workprefs.produce_sound = 2;
	else if (workprefs.produce_sound >= 2)
	    workprefs.produce_sound = 2;
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
	EnableWindow (GetDlgItem (hDlg, IDC_AGA), FALSE);
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

static void enable_for_memorydlg (HWND hDlg)
{
    int z3 = ! workprefs.address_space_24;
    int fast = workprefs.chipmem_size <= 0x200000;

#ifndef AUTOCONFIG
    z3 = FALSE;
    fast = FALSE;
#endif
    EnableWindow (GetDlgItem (hDlg, IDC_Z3TEXT), z3);
    EnableWindow (GetDlgItem (hDlg, IDC_Z3FASTRAM), z3);
    EnableWindow (GetDlgItem (hDlg, IDC_Z3FASTMEM), z3);
    EnableWindow (GetDlgItem (hDlg, IDC_FASTMEM), fast);
    EnableWindow (GetDlgItem (hDlg, IDC_FASTRAM), fast);
    EnableWindow (GetDlgItem (hDlg, IDC_FASTTEXT), fast);
    EnableWindow (GetDlgItem (hDlg, IDC_GFXCARDTEXT), z3);
    EnableWindow (GetDlgItem (hDlg, IDC_P96RAM), z3);
    EnableWindow (GetDlgItem (hDlg, IDC_P96MEM), z3);
}

static void values_to_memorydlg (HWND hDlg)
{
    uae_u32 mem_size = 0;

    switch (workprefs.chipmem_size) {
     case 0x00040000: mem_size = 0; break;
     case 0x00080000: mem_size = 1; break;
     case 0x00100000: mem_size = 2; break;
     case 0x00200000: mem_size = 3; break;
     case 0x00400000: mem_size = 4; break;
     case 0x00800000: mem_size = 5; break;
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
    switch (workprefs.z3fastmem_size) {
     case 0x00000000: mem_size = 0; break; /*   0-megs */
     case 0x00100000: mem_size = 1; break; /*   1-megs */
     case 0x00200000: mem_size = 2; break; /*   2-megs */
     case 0x00400000: mem_size = 3; break; /*   4-megs */
     case 0x00800000: mem_size = 4; break; /*   8-megs */
     case 0x01000000: mem_size = 5; break; /*  16-megs */
     case 0x02000000: mem_size = 6; break; /*  32-megs */
     case 0x04000000: mem_size = 7; break; /*  64-megs */
     case 0x08000000: mem_size = 8; break; /* 128-megs */
     case 0x10000000: mem_size = 9; break; /* 256-megs */
     case 0x20000000: mem_size = 10; break; /* 512-megs */
     case 0x40000000: mem_size = 11; break; /* 1 GB */
     case 0x80000000: mem_size = 12; break; /* 2 GB */

    }
    SendDlgItemMessage (hDlg, IDC_Z3FASTMEM, TBM_SETPOS, TRUE, mem_size);
    SetDlgItemText (hDlg, IDC_Z3FASTRAM, memsize_names[msi_z3fast[mem_size]]);

    mem_size = 0;
    switch (workprefs.gfxmem_size) {
     case 0x00000000: mem_size = 0; break;
     case 0x00100000: mem_size = 1; break;
     case 0x00200000: mem_size = 2; break;
     case 0x00400000: mem_size = 3; break;
     case 0x00800000: mem_size = 4; break;
     case 0x01000000: mem_size = 5; break;
     case 0x02000000: mem_size = 6; break;
    }
    SendDlgItemMessage (hDlg, IDC_P96MEM, TBM_SETPOS, TRUE, mem_size);
    SetDlgItemText (hDlg, IDC_P96RAM, memsize_names[msi_gfx[mem_size]]);
}

static void fix_values_memorydlg (void)
{
    if (workprefs.chipmem_size > 0x200000)
	workprefs.fastmem_size = 0;
    if (workprefs.chipmem_size > 0x80000)
	workprefs.chipset_mask |= CSMASK_ECS_AGNUS;
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
	SendDlgItemMessage (hDlg, IDC_P96MEM, TBM_SETRANGE, TRUE, MAKELONG (MIN_P96_MEM, MAX_P96_MEM));

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
	workprefs.gfxmem_size = memsizes[msi_gfx[SendMessage (GetDlgItem (hDlg, IDC_P96MEM), TBM_GETPOS, 0, 0)]];
	fix_values_memorydlg ();
	values_to_memorydlg (hDlg);
	enable_for_memorydlg (hDlg);
	break;

    case WM_COMMAND:
	if (recursive > 0)
	    break;
	recursive++;
	values_to_memorydlg (hDlg);
	recursive--;
	break;

    }
    return FALSE;
}

static void addromfiles (HKEY fkey, HWND hDlg, DWORD d, char *path, uae_u8 *keybuf, int keysize, int type)
{
    int idx, idx2;
    char tmp[MAX_DPATH];
    char tmp2[MAX_DPATH];
    char seltmp[MAX_DPATH];
    struct romdata *rdx;

    rdx = scan_single_rom (path, keybuf, keysize);
    SendDlgItemMessage(hDlg, d, CB_RESETCONTENT, 0, 0);
    SendDlgItemMessage(hDlg, d, CB_ADDSTRING, 0, (LPARAM)"");
    idx = 0;
    seltmp[0] = 0;
    for (;fkey;) {
	DWORD size = sizeof (tmp);
	DWORD size2 = sizeof (tmp2);
	int err = RegEnumValue(fkey, idx, tmp, &size, NULL, NULL, tmp2, &size2);
	if (err != ERROR_SUCCESS)
	    break;
	if (strlen (tmp) == 5) {
	    idx2 = atol (tmp + 3);
	    if (idx2 >= 0) {
		struct romdata *rd = getromdatabyid (idx2);
		if (rd && (rd->type & type)) {
		    getromname (rd, tmp);
		    SendDlgItemMessage(hDlg, d, CB_ADDSTRING, 0, (LPARAM)tmp);
		    if (rd == rdx)
			strcpy (seltmp, tmp);
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

static void getromfile (HWND hDlg, DWORD d, char *path, int size)
{
    LRESULT val = SendDlgItemMessage (hDlg, d, CB_GETCURSEL, 0, 0L);
    if (val == CB_ERR) {
	SendDlgItemMessage (hDlg, d, WM_GETTEXT, (WPARAM)size, (LPARAM)path);
    } else {
	char tmp1[MAX_DPATH], tmp2[MAX_DPATH];
	struct romdata *rd;
	SendDlgItemMessage (hDlg, d, CB_GETLBTEXT, (WPARAM)val, (LPARAM)tmp1);
	path[0] = 0;
	rd = getromdatabyname (tmp1);
	if (rd && hWinUAEKey) {
	    HKEY fkey;
	    RegCreateKeyEx(hWinUAEKey , "DetectedROMs", 0, NULL, REG_OPTION_NON_VOLATILE,
		KEY_READ | KEY_WRITE, NULL, &fkey, NULL);
	    if (fkey) {
		DWORD outsize = size;
		sprintf (tmp1, "ROM%02d", rd->id);
		tmp2[0] = 0;
		RegQueryValueEx (fkey, tmp1, NULL, NULL, tmp2, &outsize);
		RegCloseKey (fkey);
		if (tmp2[0])
		    strncpy (path, tmp2, size);
	    }
	}
    }
}

static void values_from_kickstartdlg (HWND hDlg)
{
    getromfile (hDlg, IDC_ROMFILE, workprefs.romfile, sizeof (workprefs.romfile));
    getromfile (hDlg, IDC_ROMFILE2, workprefs.romextfile, sizeof (workprefs.romextfile));
    getromfile (hDlg, IDC_CARTFILE, workprefs.cartfile, sizeof (workprefs.cartfile));
    if (workprefs.cartfile[0])
	workprefs.cart_internal = 0;
    EnableWindow(GetDlgItem(hDlg, IDC_HRTMON), workprefs.cartfile[0] ? FALSE : TRUE);
    CheckDlgButton(hDlg, IDC_HRTMON, workprefs.cart_internal ? TRUE : FALSE);
}

static void values_to_kickstartdlg (HWND hDlg)
{
    HKEY fkey;
    uae_u8 *keybuf;
    int keysize;

    if (hWinUAEKey) {
	RegCreateKeyEx(hWinUAEKey , "DetectedROMs", 0, NULL, REG_OPTION_NON_VOLATILE,
	    KEY_READ | KEY_WRITE, NULL, &fkey, NULL);
	keybuf = load_keyfile (&workprefs, NULL, &keysize);
	addromfiles (fkey, hDlg, IDC_ROMFILE, workprefs.romfile, keybuf, keysize, ROMTYPE_KICK | ROMTYPE_KICKCD32);
	addromfiles (fkey, hDlg, IDC_ROMFILE2, workprefs.romextfile, keybuf, keysize, ROMTYPE_EXTCD32 | ROMTYPE_EXTCDTV);
	addromfiles (fkey, hDlg, IDC_CARTFILE, workprefs.cartfile, keybuf, keysize, ROMTYPE_AR | ROMTYPE_ARCADIA);
	free_keyfile (keybuf);
	if (fkey)
	    RegCloseKey (fkey);
    }

    SetDlgItemText(hDlg, IDC_FLASHFILE, workprefs.flashfile);
    CheckDlgButton(hDlg, IDC_KICKSHIFTER, workprefs.kickshifter);
    CheckDlgButton(hDlg, IDC_MAPROM, workprefs.maprom);
    CheckDlgButton(hDlg, IDC_HRTMON, workprefs.cart_internal);
}

static void init_kickstart (HWND hDlg)
{
    HKEY fkey;

#if !defined(AUTOCONFIG)
    EnableWindow(GetDlgItem(hDlg, IDC_MAPROM), FALSE);
#endif
#if !defined (CDTV) && !defined (CD32)
    EnableWindow(GetDlgItem(hDlg, IDC_FLASHFILE), FALSE);
    EnableWindow(GetDlgItem(hDlg, IDC_ROMFILE2), FALSE);
#endif
#if !defined (ACTION_REPLAY)
    EnableWindow(GetDlgItem(hDlg, IDC_CARTFILE), FALSE);
#endif
#if defined (UAE_MINI)
    EnableWindow(GetDlgItem(hDlg, IDC_KICKSHIFTER), FALSE);
    EnableWindow(GetDlgItem(hDlg, IDC_ROMCHOOSER2), FALSE);
    EnableWindow(GetDlgItem(hDlg, IDC_CARTCHOOSER), FALSE);
    EnableWindow(GetDlgItem(hDlg, IDC_FLASHCHOOSER), FALSE);
#endif
    if (RegOpenKeyEx (hWinUAEKey , "DetectedROMs", 0, KEY_READ, &fkey) != ERROR_SUCCESS)
	scan_roms (workprefs.path_rom);
    if (fkey)
	RegCloseKey (fkey);
    EnableWindow(GetDlgItem(hDlg, IDC_HRTMON), full_property_sheet);

}

static INT_PTR CALLBACK KickstartDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int recursive;
    char tmp[MAX_DPATH];

    switch( msg ) 
    {
    case WM_INITDIALOG:
	pages[KICKSTART_ID] = hDlg;
	currentpage = KICKSTART_ID;
	init_kickstart (hDlg);
	values_to_kickstartdlg (hDlg);
	return TRUE;

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
	switch (LOWORD (wParam))
	{
	case IDC_KICKCHOOSER:
	    DiskSelection(hDlg, IDC_ROMFILE, 6, &workprefs, 0);
	    values_to_kickstartdlg (hDlg);
	    break;

	case IDC_ROMCHOOSER2:
	    DiskSelection(hDlg, IDC_ROMFILE2, 6, &workprefs, 0);
	    values_to_kickstartdlg (hDlg);
	    break;
		    
	case IDC_FLASHCHOOSER:
	    DiskSelection(hDlg, IDC_FLASHFILE, 11, &workprefs, 0);
	    values_to_kickstartdlg (hDlg);
	    break;
	case IDC_FLASHFILE:
	    GetWindowText (GetDlgItem (hDlg, IDC_FLASHFILE), tmp, sizeof (tmp));
	    strcpy (workprefs.flashfile, tmp);
	    break;

	case IDC_CARTCHOOSER:
	    DiskSelection(hDlg, IDC_CARTFILE, 6, &workprefs, 0);
	    values_to_kickstartdlg (hDlg);
	    break;

	case IDC_KICKSHIFTER:
	    workprefs.kickshifter = IsDlgButtonChecked(hDlg, IDC_KICKSHIFTER);
	    break;

	case IDC_MAPROM:
	    workprefs.maprom = IsDlgButtonChecked(hDlg, IDC_MAPROM) ? 0xe00000 : 0;
	    break;

	case IDC_HRTMON:
	    workprefs.cart_internal = IsDlgButtonChecked(hDlg, IDC_HRTMON) ? 1 : 0;
	    EnableWindow(GetDlgItem(hDlg, IDC_CARTFILE), workprefs.cart_internal ? FALSE : TRUE);
	    break;
	
	}
	recursive--;
	break;
    }
    return FALSE;
}

static void enable_for_miscdlg (HWND hDlg)
{
    if(!full_property_sheet)
    {
	EnableWindow (GetDlgItem (hDlg, IDC_JULIAN), TRUE);
	EnableWindow (GetDlgItem (hDlg, IDC_CTRLF11), TRUE);
	EnableWindow (GetDlgItem (hDlg, IDC_SOCKETS), FALSE);
	EnableWindow (GetDlgItem (hDlg, IDC_SHOWGUI), FALSE);
	EnableWindow (GetDlgItem (hDlg, IDC_NOSPEED), TRUE);
	EnableWindow (GetDlgItem (hDlg, IDC_NOSPEEDPAUSE), TRUE);
	EnableWindow (GetDlgItem (hDlg, IDC_NOSOUND), TRUE);
	EnableWindow (GetDlgItem (hDlg, IDC_NOOVERLAY), TRUE);
	EnableWindow (GetDlgItem (hDlg, IDC_DOSAVESTATE), TRUE);
	EnableWindow (GetDlgItem (hDlg, IDC_SCSIMODE), FALSE);
	EnableWindow (GetDlgItem (hDlg, IDC_SCSIDEVICE), FALSE);
	EnableWindow (GetDlgItem (hDlg, IDC_CLOCKSYNC), FALSE);
	EnableWindow (GetDlgItem (hDlg, IDC_CATWEASEL), FALSE);
	EnableWindow (GetDlgItem (hDlg, IDC_STATE_CAPTURE), FALSE);
	EnableWindow (GetDlgItem (hDlg, IDC_STATE_RATE), FALSE);
	EnableWindow (GetDlgItem (hDlg, IDC_STATE_BUFFERSIZE), FALSE);
	EnableWindow (GetDlgItem (hDlg, IDC_LANGUAGE), FALSE);
    } else {
#if !defined (BSDSOCKET)
	EnableWindow (GetDlgItem(hDlg, IDC_SOCKETS), FALSE);
#endif
#if !defined (SCSIEMU)
	EnableWindow (GetDlgItem(hDlg, IDC_SCSIDEVICE), FALSE);
	EnableWindow (GetDlgItem(hDlg, IDC_SCSIMODE), TRUE);
#endif
	EnableWindow (GetDlgItem (hDlg, IDC_DOSAVESTATE), FALSE);
	EnableWindow (GetDlgItem (hDlg, IDC_STATE_RATE), workprefs.statecapture ? TRUE : FALSE);
	EnableWindow (GetDlgItem (hDlg, IDC_STATE_BUFFERSIZE), workprefs.statecapture ? TRUE : FALSE);
    }
}

static void misc_kbled (HWND hDlg, int v, int nv)
{
    char *defname = v == IDC_KBLED1 ? "(NumLock)" : v == IDC_KBLED2 ? "(CapsLock)" : "(ScrollLock)";
    SendDlgItemMessage (hDlg, v, CB_RESETCONTENT, 0, 0L);
    SendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)defname);
    SendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)"POWER");
    SendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)"DF0");
    SendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)"DF1");
    SendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)"DF2");
    SendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)"DF3");
    SendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)"HD");
    SendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)"CD");
    SendDlgItemMessage (hDlg, v, CB_SETCURSEL, nv, 0);
}

static void misc_getkbled (HWND hDlg, int v, int n)
{
    LRESULT nv = SendDlgItemMessage(hDlg, v, CB_GETCURSEL, 0, 0L);
    if (nv != CB_ERR) {
	workprefs.keyboard_leds[n] = nv;
	misc_kbled (hDlg, v, nv);
    }
    workprefs.keyboard_leds_in_use = workprefs.keyboard_leds[0] | workprefs.keyboard_leds[1] | workprefs.keyboard_leds[2];
}

static void misc_getpri (HWND hDlg, int v, int *n)
{
    LRESULT nv = SendDlgItemMessage(hDlg, v, CB_GETCURSEL, 0, 0L);
    if (nv != CB_ERR)
	*n = nv;
}

static void misc_addpri (HWND hDlg, int v, int pri)
{
    int i;
    SendDlgItemMessage (hDlg, v, CB_RESETCONTENT, 0, 0L);
    i = 0;
    while (priorities[i].name) {
	SendDlgItemMessage (hDlg, v, CB_ADDSTRING, 0, (LPARAM)priorities[i].name);
	i++;
    }
    SendDlgItemMessage (hDlg, v, CB_SETCURSEL, pri, 0);
}

extern char *get_aspi_path(int);

static void misc_scsi(HWND hDlg)
{
    SendDlgItemMessage (hDlg, IDC_SCSIMODE, CB_RESETCONTENT, 0, 0);
    SendDlgItemMessage (hDlg, IDC_SCSIMODE, CB_ADDSTRING, 0, (LPARAM)((os_winnt && os_winnt_admin) ? "SPTI" : "(SPTI)"));
    SendDlgItemMessage (hDlg, IDC_SCSIMODE, CB_ADDSTRING, 0, (LPARAM)((os_winnt && os_winnt_admin) ? "SPTI + SCSI SCAN" : "(SPTI + SCSI SCAN)"));
    SendDlgItemMessage (hDlg, IDC_SCSIMODE, CB_ADDSTRING, 0, (LPARAM)((get_aspi_path(0)) ? "AdaptecASPI" : "(AdaptecASPI)"));
    SendDlgItemMessage (hDlg, IDC_SCSIMODE, CB_ADDSTRING, 0, (LPARAM)((get_aspi_path(1)) ? "NeroASPI" : "(NeroASPI)"));
    SendDlgItemMessage (hDlg, IDC_SCSIMODE, CB_ADDSTRING, 0, (LPARAM)((get_aspi_path(2)) ? "FrogASPI" : "(FrogASPI)"));
    SendDlgItemMessage (hDlg, IDC_SCSIMODE, CB_SETCURSEL, workprefs.win32_uaescsimode, 0);
}

static void misc_lang(HWND hDlg)
{
    int i, idx = 0, cnt = 0;
    WORD langid = -1;

    if (hWinUAEKey) {
	DWORD regkeytype;
	DWORD regkeysize = sizeof(langid);
        RegQueryValueEx (hWinUAEKey, "Language", 0, &regkeytype, (LPBYTE)&langid, &regkeysize);
    }
    SendDlgItemMessage (hDlg, IDC_LANGUAGE, CB_RESETCONTENT, 0, 0);
    SendDlgItemMessage (hDlg, IDC_LANGUAGE, CB_ADDSTRING, 0, (LPARAM)"Autodetect");
    SendDlgItemMessage (hDlg, IDC_LANGUAGE, CB_ADDSTRING, 0, (LPARAM)"English (build-in)");
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
static void misc_setlang(int v)
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
    if (hWinUAEKey)
	RegSetValueEx (hWinUAEKey, "Language", 0, REG_DWORD, (CONST BYTE *)&langid, sizeof(langid));
    FreeLibrary(hUIDLL);
    hUIDLL = NULL;
    if (langid >= 0)
	hUIDLL = language_load(langid);
    restart_requested = 1;
    exit_gui(0);
}

static void values_to_miscdlg (HWND hDlg)
{
    char txt[100];
    int cw;

    if (currentpage == MISC1_ID) {

	CheckDlgButton (hDlg, IDC_SOCKETS, workprefs.socket_emu);
	CheckDlgButton (hDlg, IDC_ILLEGAL, workprefs.illegal_mem);
	CheckDlgButton (hDlg, IDC_SHOWGUI, workprefs.start_gui);
	CheckDlgButton (hDlg, IDC_JULIAN, workprefs.win32_middle_mouse);
	CheckDlgButton (hDlg, IDC_CREATELOGFILE, workprefs.win32_logfile);
	CheckDlgButton (hDlg, IDC_CTRLF11, workprefs.win32_ctrl_F11_is_quit);
	CheckDlgButton (hDlg, IDC_NOOVERLAY, workprefs.win32_no_overlay);
	CheckDlgButton (hDlg, IDC_SHOWLEDS, workprefs.leds_on_screen);
	CheckDlgButton (hDlg, IDC_SCSIDEVICE, workprefs.scsi);
	CheckDlgButton (hDlg, IDC_NOTASKBARBUTTON, workprefs.win32_notaskbarbutton);
	CheckDlgButton (hDlg, IDC_ALWAYSONTOP, workprefs.win32_alwaysontop);
	CheckDlgButton (hDlg, IDC_CLOCKSYNC, workprefs.tod_hack);
	cw = catweasel_detect();
	EnableWindow (GetDlgItem (hDlg, IDC_CATWEASEL), cw);
	if (!cw && workprefs.catweasel < 100)
	    workprefs.catweasel = 0;
	CheckDlgButton (hDlg, IDC_CATWEASEL, workprefs.catweasel);
	CheckDlgButton (hDlg, IDC_STATE_CAPTURE, workprefs.statecapture);

	misc_kbled (hDlg, IDC_KBLED1, workprefs.keyboard_leds[0]);
	misc_kbled (hDlg, IDC_KBLED2, workprefs.keyboard_leds[1]);
	misc_kbled (hDlg, IDC_KBLED3, workprefs.keyboard_leds[2]);

	SendDlgItemMessage (hDlg, IDC_STATE_RATE, CB_RESETCONTENT, 0, 0);
	SendDlgItemMessage (hDlg, IDC_STATE_RATE, CB_ADDSTRING, 0, (LPARAM)"1");
	SendDlgItemMessage (hDlg, IDC_STATE_RATE, CB_ADDSTRING, 0, (LPARAM)"5");
	SendDlgItemMessage (hDlg, IDC_STATE_RATE, CB_ADDSTRING, 0, (LPARAM)"10");
	SendDlgItemMessage (hDlg, IDC_STATE_RATE, CB_ADDSTRING, 0, (LPARAM)"20");
	SendDlgItemMessage (hDlg, IDC_STATE_RATE, CB_ADDSTRING, 0, (LPARAM)"30");
	sprintf (txt, "%d", workprefs.statecapturerate / 50);
	SendDlgItemMessage( hDlg, IDC_STATE_RATE, WM_SETTEXT, 0, (LPARAM)txt); 

	SendDlgItemMessage (hDlg, IDC_STATE_BUFFERSIZE, CB_RESETCONTENT, 0, 0);
	SendDlgItemMessage (hDlg, IDC_STATE_BUFFERSIZE, CB_ADDSTRING, 0, (LPARAM)"5");
	SendDlgItemMessage (hDlg, IDC_STATE_BUFFERSIZE, CB_ADDSTRING, 0, (LPARAM)"10");
	SendDlgItemMessage (hDlg, IDC_STATE_BUFFERSIZE, CB_ADDSTRING, 0, (LPARAM)"20");
	SendDlgItemMessage (hDlg, IDC_STATE_BUFFERSIZE, CB_ADDSTRING, 0, (LPARAM)"50");
	SendDlgItemMessage (hDlg, IDC_STATE_BUFFERSIZE, CB_ADDSTRING, 0, (LPARAM)"100");
	sprintf (txt, "%d", workprefs.statecapturebuffersize / (1024 * 1024));
	SendDlgItemMessage( hDlg, IDC_STATE_BUFFERSIZE, WM_SETTEXT, 0, (LPARAM)txt); 

	misc_scsi(hDlg);
	misc_lang(hDlg);

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
    char txt[100];
    int v;
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
		    case IDC_STATE_RATE:
		    getcbn (hDlg, IDC_STATE_RATE, txt, sizeof (txt));
		    workprefs.statecapturerate = atol (txt) * 50;
		    break;
		    case IDC_STATE_BUFFERSIZE:
		    getcbn (hDlg, IDC_STATE_BUFFERSIZE, txt, sizeof (txt));
		    break;
		    case IDC_SCSIMODE:
		    v = SendDlgItemMessage(hDlg, IDC_SCSIMODE, CB_GETCURSEL, 0, 0L);
		    if (v != CB_ERR)
			workprefs.win32_uaescsimode = v;
		    break;
		    case IDC_LANGUAGE:
		    if (HIWORD (wParam) == CBN_SELENDOK) {
			v = SendDlgItemMessage(hDlg, IDC_LANGUAGE, CB_GETCURSEL, 0, 0L);
			if (v != CB_ERR)
			    misc_setlang(v);
		    }
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
	case IDC_DOSAVESTATE:
	    if (DiskSelection(hDlg, wParam, 9, &workprefs, 0)) 
		save_state (savestate_fname, "Description!");
	    break;
	case IDC_DOLOADSTATE:
	    if (DiskSelection(hDlg, wParam, 10, &workprefs, 0))
		savestate_state = STATE_DORESTORE;
	    break;
	case IDC_STATE_CAPTURE:
	    workprefs.statecapture = IsDlgButtonChecked (hDlg, IDC_STATE_CAPTURE);
	    enable_for_miscdlg (hDlg);
	    break;
	case IDC_SOCKETS:
	    workprefs.socket_emu = IsDlgButtonChecked (hDlg, IDC_SOCKETS);
	    break;
	case IDC_ILLEGAL:
	    workprefs.illegal_mem = IsDlgButtonChecked (hDlg, IDC_ILLEGAL);
	    break;
	case IDC_JULIAN:
	    workprefs.win32_middle_mouse = IsDlgButtonChecked(hDlg, IDC_JULIAN);
	    break;
	case IDC_NOOVERLAY:
	    workprefs.win32_no_overlay = IsDlgButtonChecked(hDlg, IDC_NOOVERLAY);
	    break;
	case IDC_SHOWLEDS:
	    workprefs.leds_on_screen = IsDlgButtonChecked(hDlg, IDC_SHOWLEDS);
	    break;
	case IDC_SHOWGUI:
	    workprefs.start_gui = IsDlgButtonChecked (hDlg, IDC_SHOWGUI);
	    break;
	case IDC_CREATELOGFILE:
	    workprefs.win32_logfile = IsDlgButtonChecked (hDlg, IDC_CREATELOGFILE);
	    enable_for_miscdlg(hDlg);
	    break;
	case IDC_INACTIVE_NOSOUND:
	    if (!IsDlgButtonChecked (hDlg, IDC_INACTIVE_NOSOUND))
		CheckDlgButton (hDlg, IDC_INACTIVE_PAUSE, BST_UNCHECKED);
	case IDC_INACTIVE_PAUSE:
	    workprefs.win32_inactive_pause = IsDlgButtonChecked (hDlg, IDC_INACTIVE_PAUSE);
	    if (workprefs.win32_inactive_pause)
		CheckDlgButton (hDlg, IDC_INACTIVE_NOSOUND, BST_CHECKED);
	    workprefs.win32_inactive_nosound = IsDlgButtonChecked (hDlg, IDC_INACTIVE_NOSOUND);
	    break;
	case IDC_MINIMIZED_NOSOUND:
	    if (!IsDlgButtonChecked (hDlg, IDC_MINIMIZED_NOSOUND))
		CheckDlgButton (hDlg, IDC_MINIMIZED_PAUSE, BST_UNCHECKED);
	case IDC_MINIMIZED_PAUSE:
	    workprefs.win32_iconified_pause = IsDlgButtonChecked (hDlg, IDC_MINIMIZED_PAUSE);
	    if (workprefs.win32_iconified_pause)
		CheckDlgButton (hDlg, IDC_MINIMIZED_NOSOUND, BST_CHECKED);
	    workprefs.win32_iconified_nosound = IsDlgButtonChecked (hDlg, IDC_MINIMIZED_NOSOUND);
	    break;
	case IDC_CTRLF11:
	    workprefs.win32_ctrl_F11_is_quit = IsDlgButtonChecked (hDlg, IDC_CTRLF11);
	    break;
	case IDC_SCSIDEVICE:
	    workprefs.scsi = IsDlgButtonChecked (hDlg, IDC_SCSIDEVICE);
	    enable_for_miscdlg (hDlg);
	    break;
	case IDC_CLOCKSYNC:
	    workprefs.tod_hack = IsDlgButtonChecked (hDlg, IDC_CLOCKSYNC);
	    break;
	case IDC_CATWEASEL:
	    workprefs.catweasel = IsDlgButtonChecked (hDlg, IDC_CATWEASEL) ? -1 : 0;
	    break;
	case IDC_NOTASKBARBUTTON:
	    workprefs.win32_notaskbarbutton = IsDlgButtonChecked (hDlg, IDC_NOTASKBARBUTTON);
	    break;
	case IDC_ALWAYSONTOP:
	    workprefs.win32_alwaysontop = IsDlgButtonChecked (hDlg, IDC_ALWAYSONTOP);
	    break;
	case IDC_KBLED_USB:
	    workprefs.win32_kbledmode = IsDlgButtonChecked (hDlg, IDC_KBLED_USB) ? 1 : 0;
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
	values_to_miscdlg (hDlg);
	enable_for_miscdlg (hDlg);
	return TRUE;
    }
    return MiscDlgProc (hDlg, msg, wParam, lParam);
}

static int cpu_ids[]   = { IDC_CPU0, IDC_CPU0, IDC_CPU1, IDC_CPU1, IDC_CPU2, IDC_CPU4, IDC_CPU3, IDC_CPU5, IDC_CPU6, IDC_CPU6 };
static int trust_ids[] = { IDC_TRUST0, IDC_TRUST1, IDC_TRUST1, IDC_TRUST2 };

static void enable_for_cpudlg (HWND hDlg)
{
    BOOL enable = FALSE, enable2 = FALSE;
    BOOL cpu_based_enable = FALSE;

    /* These four items only get enabled when adjustable CPU style is enabled */
    EnableWindow (GetDlgItem (hDlg, IDC_SPEED), workprefs.m68k_speed > 0);
    EnableWindow (GetDlgItem (hDlg, IDC_CS_CPU_TEXT), (!workprefs.cpu_cycle_exact || workprefs.cpu_level > 0) && workprefs.m68k_speed > 0);
    EnableWindow (GetDlgItem (hDlg, IDC_CS_CHIPSET_TEXT), (!workprefs.cpu_cycle_exact || workprefs.cpu_level > 0) && workprefs.m68k_speed > 0);
    EnableWindow (GetDlgItem (hDlg, IDC_CS_HOST), !workprefs.cpu_cycle_exact || workprefs.cpu_level > 0);
    EnableWindow (GetDlgItem (hDlg, IDC_CS_68000), !workprefs.cpu_cycle_exact || workprefs.cpu_level > 0);
    EnableWindow (GetDlgItem (hDlg, IDC_CS_ADJUSTABLE), !workprefs.cpu_cycle_exact || workprefs.cpu_level > 0);
    EnableWindow (GetDlgItem (hDlg, IDC_CPUTEXT), workprefs.m68k_speed > 0 );
    EnableWindow (GetDlgItem (hDlg, IDC_CPUIDLE), workprefs.m68k_speed != 0 ? TRUE : FALSE);
#if !defined(CPUEMU_0) || defined(CPUEMU_68000_ONLY)
    EnableWindow (GetDlgItem (hDlg, IDC_CPU1), FALSE);
    EnableWindow (GetDlgItem (hDlg, IDC_CPU2), FALSE);
    EnableWindow (GetDlgItem (hDlg, IDC_CPU3), FALSE);
    EnableWindow (GetDlgItem (hDlg, IDC_CPU4), FALSE);
    EnableWindow (GetDlgItem (hDlg, IDC_CPU5), FALSE);
    EnableWindow (GetDlgItem (hDlg, IDC_CPU6), FALSE);
#endif

    cpu_based_enable = workprefs.cpu_level >= 2 &&
		       workprefs.address_space_24 == 0;

    enable = cpu_based_enable && workprefs.cachesize;
#ifndef JIT
    enable = FALSE;
#endif
    enable2 = enable && workprefs.compforcesettings;

    EnableWindow (GetDlgItem (hDlg, IDC_TRUST0), enable2);
    EnableWindow (GetDlgItem (hDlg, IDC_TRUST1), enable2);
    EnableWindow (GetDlgItem (hDlg, IDC_TRUST2), enable2);
    EnableWindow (GetDlgItem (hDlg, IDC_HARDFLUSH), enable2);
    EnableWindow (GetDlgItem (hDlg, IDC_CONSTJUMP), enable2);
    EnableWindow (GetDlgItem (hDlg, IDC_JITFPU), enable2);
    EnableWindow (GetDlgItem (hDlg, IDC_NOFLAGS), enable2);
    EnableWindow (GetDlgItem (hDlg, IDC_CS_CACHE_TEXT), cpu_based_enable && workprefs.cachesize);
    EnableWindow (GetDlgItem (hDlg, IDC_CACHE), cpu_based_enable && workprefs.cachesize);
    EnableWindow (GetDlgItem (hDlg, IDC_CACHETEXT), cpu_based_enable && workprefs.cachesize);
    EnableWindow (GetDlgItem (hDlg, IDC_FORCE), enable);
    EnableWindow (GetDlgItem (hDlg, IDC_JITENABLE), cpu_based_enable);
    EnableWindow (GetDlgItem (hDlg, IDC_COMPATIBLE), !workprefs.cpu_cycle_exact && !workprefs.cachesize);
    EnableWindow (GetDlgItem (hDlg, IDC_COMPATIBLE_FPU), workprefs.cpu_level >= 3);

#ifdef JIT
    if( enable )
    {
	if(!canbang)
	{
	    workprefs.compforcesettings = TRUE;
	    workprefs.comptrustbyte = 1;
	    workprefs.comptrustword = 1;
	    workprefs.comptrustlong = 1;
	    workprefs.comptrustnaddr= 1;
	}
    }
    else
    {
	workprefs.cachesize = 0; // Disable JIT
    }
#endif
}

static void values_to_cpudlg (HWND hDlg)
{
    char cache[ 8 ] = "";
    BOOL enable = FALSE;
    BOOL cpu_based_enable = FALSE;

    SendDlgItemMessage (hDlg, IDC_SPEED, TBM_SETPOS, TRUE, workprefs.m68k_speed <= 0 ? 1 : workprefs.m68k_speed / CYCLE_UNIT );
    SetDlgItemInt( hDlg, IDC_CPUTEXT, workprefs.m68k_speed <= 0 ? 1 : workprefs.m68k_speed / CYCLE_UNIT, FALSE );
    CheckDlgButton (hDlg, IDC_COMPATIBLE, workprefs.cpu_compatible);
    CheckDlgButton (hDlg, IDC_COMPATIBLE_FPU, workprefs.fpu_strict);
    SendDlgItemMessage (hDlg, IDC_CPUIDLE, TBM_SETPOS, TRUE, workprefs.cpu_idle == 0 ? 0 : 12 - workprefs.cpu_idle / 15);
    CheckRadioButton (hDlg, IDC_CPU0, IDC_CPU6, cpu_ids[workprefs.cpu_level * 2 + !workprefs.address_space_24]);

    if (workprefs.m68k_speed == -1)
	CheckRadioButton( hDlg, IDC_CS_HOST, IDC_CS_ADJUSTABLE, IDC_CS_HOST );
    else if (workprefs.m68k_speed == 0)
	CheckRadioButton( hDlg, IDC_CS_HOST, IDC_CS_ADJUSTABLE, IDC_CS_68000 );
    else
	CheckRadioButton( hDlg, IDC_CS_HOST, IDC_CS_ADJUSTABLE, IDC_CS_ADJUSTABLE );

    cpu_based_enable = ( workprefs.cpu_level >= 2 ) &&
		       ( workprefs.address_space_24 == 0 );

    enable = cpu_based_enable && ( workprefs.cachesize );

#ifdef JIT
    if( enable ) {
	if( !canbang ) {
	    workprefs.compforcesettings = TRUE;
	    workprefs.comptrustbyte = 1;
	    workprefs.comptrustword = 1;
	    workprefs.comptrustlong = 1;
	    workprefs.comptrustnaddr= 1;
	}
    } else {
#endif
	workprefs.cachesize = 0; // Disable JIT
#ifdef JIT
    }
#endif

    if( !workprefs.compforcesettings ) {
	workprefs.comptrustbyte = 0;
	workprefs.comptrustword = 0;
	workprefs.comptrustlong = 0;
	workprefs.comptrustnaddr= 0;
    }

    CheckRadioButton( hDlg, IDC_TRUST0, IDC_TRUST2, trust_ids[ workprefs.comptrustbyte ] );

    SendDlgItemMessage( hDlg, IDC_CACHE, TBM_SETPOS, TRUE, workprefs.cachesize / 1024 );
    sprintf( cache, "%d MB", workprefs.cachesize / 1024 );
    SetDlgItemText( hDlg, IDC_CACHETEXT, cache );

    CheckDlgButton( hDlg, IDC_FORCE, workprefs.compforcesettings );
    CheckDlgButton( hDlg, IDC_NOFLAGS, workprefs.compnf );
    CheckDlgButton( hDlg, IDC_JITFPU, workprefs.compfpu );
    CheckDlgButton( hDlg, IDC_HARDFLUSH, workprefs.comp_hardflush );
    CheckDlgButton( hDlg, IDC_CONSTJUMP, workprefs.comp_constjump );
    CheckDlgButton( hDlg, IDC_JITENABLE, workprefs.cachesize > 0);
}

static void values_from_cpudlg (HWND hDlg)
{
    int newcpu, newtrust, oldcache, jitena;
    static int cachesize_prev;
    
    workprefs.cpu_compatible = workprefs.cpu_cycle_exact | (IsDlgButtonChecked (hDlg, IDC_COMPATIBLE) ? 1 : 0);
    workprefs.fpu_strict = IsDlgButtonChecked (hDlg, IDC_COMPATIBLE_FPU) ? 1 : 0;
    workprefs.m68k_speed = IsDlgButtonChecked (hDlg, IDC_CS_HOST) ? -1
	: IsDlgButtonChecked (hDlg, IDC_CS_68000) ? 0
	: SendMessage (GetDlgItem (hDlg, IDC_SPEED), TBM_GETPOS, 0, 0) * CYCLE_UNIT;
    
    newcpu = (IsDlgButtonChecked (hDlg, IDC_CPU0) ? 0
	: IsDlgButtonChecked (hDlg, IDC_CPU1) ? 1
	: IsDlgButtonChecked (hDlg, IDC_CPU2) ? 2
	: IsDlgButtonChecked (hDlg, IDC_CPU3) ? 3
	: IsDlgButtonChecked (hDlg, IDC_CPU4) ? 4
	: IsDlgButtonChecked (hDlg, IDC_CPU5) ? 5 : 6);
    /* When switching away from 68000, disable 24 bit addressing.  */
    switch( newcpu )
    {
	case 0: // 68000
	case 1: // 68010
	case 2: // 68EC020
	case 3: // 68EC020+FPU
	    workprefs.address_space_24 = 1;
	    workprefs.cpu_level = newcpu;
	    if (newcpu == 0 && workprefs.cpu_cycle_exact)
		workprefs.m68k_speed = 0;
	break;

	case 4: // 68020
	case 5: // 68020+FPU
	case 6: // 68040
	    workprefs.address_space_24 = 0;
	    workprefs.cpu_level = newcpu - 2;
	break;
    }
    newtrust = (IsDlgButtonChecked( hDlg, IDC_TRUST0 ) ? 0
	: IsDlgButtonChecked( hDlg, IDC_TRUST1 ) ? 1 : 3 );
    workprefs.comptrustbyte = newtrust;
    workprefs.comptrustword = newtrust;
    workprefs.comptrustlong = newtrust;
    workprefs.comptrustnaddr= newtrust;

    workprefs.compforcesettings = IsDlgButtonChecked( hDlg, IDC_FORCE );
    workprefs.compnf            = IsDlgButtonChecked( hDlg, IDC_NOFLAGS );
    workprefs.compfpu           = IsDlgButtonChecked( hDlg, IDC_JITFPU );
    workprefs.comp_hardflush    = IsDlgButtonChecked( hDlg, IDC_HARDFLUSH );
    workprefs.comp_constjump    = IsDlgButtonChecked( hDlg, IDC_CONSTJUMP );

#ifdef JIT
    oldcache = workprefs.cachesize;
    jitena = IsDlgButtonChecked (hDlg, IDC_JITENABLE) ? 1 : 0;
    workprefs.cachesize = SendMessage(GetDlgItem(hDlg, IDC_CACHE), TBM_GETPOS, 0, 0) * 1024;
    if (!jitena) {
        cachesize_prev = workprefs.cachesize;
	workprefs.cachesize = 0;
    } else if (jitena && !oldcache) {
	workprefs.cachesize = 8192;
	if (cachesize_prev)
	    workprefs.cachesize = cachesize_prev;
    }
    if (oldcache == 0 && workprefs.cachesize > 0)
	canbang = 1;
#endif
    workprefs.cpu_idle = SendMessage(GetDlgItem(hDlg, IDC_CPUIDLE), TBM_GETPOS, 0, 0);
    if (workprefs.cpu_idle > 0)
	workprefs.cpu_idle = (12 - workprefs.cpu_idle) * 15;

    if (workprefs.cachesize > 0)
	workprefs.cpu_compatible = 0;

    if (pages[KICKSTART_ID])
	SendMessage(pages[KICKSTART_ID], WM_USER, 0, 0 );
    if (pages[DISPLAY_ID])
	SendMessage(pages[DISPLAY_ID], WM_USER, 0, 0 );
    if (pages[MEMORY_ID])
	SendMessage(pages[MEMORY_ID], WM_USER, 0, 0 );
}

static INT_PTR CALLBACK CPUDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int recursive = 0;

    switch (msg) {
    case WM_INITDIALOG:
	pages[CPU_ID] = hDlg;
	currentpage = CPU_ID;
	SendDlgItemMessage (hDlg, IDC_SPEED, TBM_SETRANGE, TRUE, MAKELONG (MIN_M68K_PRIORITY, MAX_M68K_PRIORITY));
	SendDlgItemMessage (hDlg, IDC_SPEED, TBM_SETPAGESIZE, 0, 1);
	SendDlgItemMessage (hDlg, IDC_CACHE, TBM_SETRANGE, TRUE, MAKELONG (MIN_CACHE_SIZE, MAX_CACHE_SIZE));
	SendDlgItemMessage (hDlg, IDC_CACHE, TBM_SETPAGESIZE, 0, 1);
	SendDlgItemMessage (hDlg, IDC_CPUIDLE, TBM_SETRANGE, TRUE, MAKELONG (0, 10));
	SendDlgItemMessage (hDlg, IDC_CPUIDLE, TBM_SETPAGESIZE, 0, 1);

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

    enumerate_sound_devices (&numdevs);
    if( numdevs == 0 )
	EnableWindow (GetDlgItem (hDlg, IDC_SOUNDCARDLIST), FALSE);
    else
	EnableWindow (GetDlgItem (hDlg, IDC_SOUNDCARDLIST), workprefs.produce_sound);

    EnableWindow (GetDlgItem (hDlg, IDC_FREQUENCY), workprefs.produce_sound);
    EnableWindow (GetDlgItem (hDlg, IDC_SOUNDFREQ), workprefs.produce_sound ? TRUE : FALSE);
    EnableWindow (GetDlgItem (hDlg, IDC_SOUNDSTEREO), workprefs.produce_sound);
    EnableWindow (GetDlgItem (hDlg, IDC_SOUNDINTERPOLATION), workprefs.sound_stereo < 3 && workprefs.produce_sound);
    EnableWindow (GetDlgItem (hDlg, IDC_SOUNDVOLUME), workprefs.produce_sound);
    EnableWindow (GetDlgItem (hDlg, IDC_SOUNDVOLUME2), workprefs.produce_sound);
    EnableWindow (GetDlgItem (hDlg, IDC_SOUNDSTEREOSEP), ISSTEREO(workprefs) && workprefs.produce_sound);
    EnableWindow (GetDlgItem (hDlg, IDC_SOUNDSTEREOMIX), ISSTEREO(workprefs) && workprefs.produce_sound);

    EnableWindow (GetDlgItem (hDlg, IDC_SOUNDBUFFERMEM), workprefs.produce_sound);
    EnableWindow (GetDlgItem (hDlg, IDC_SOUNDBUFFERRAM), workprefs.produce_sound);
    EnableWindow (GetDlgItem (hDlg, IDC_SOUNDADJUST), workprefs.produce_sound);
    EnableWindow (GetDlgItem (hDlg, IDC_SOUNDADJUSTNUM), workprefs.produce_sound);
    EnableWindow (GetDlgItem (hDlg, IDC_SOUNDBUFFERTEXT), workprefs.produce_sound);

    EnableWindow (GetDlgItem (hDlg, IDC_SOUNDDRIVE), workprefs.produce_sound);
    EnableWindow (GetDlgItem (hDlg, IDC_SOUNDDRIVESELECT), workprefs.produce_sound);
    EnableWindow (GetDlgItem (hDlg, IDC_SOUNDDRIVEVOLUME), workprefs.produce_sound);
    EnableWindow (GetDlgItem (hDlg, IDC_SOUNDDRIVEVOLUME2), workprefs.produce_sound);

    EnableWindow (GetDlgItem (hDlg, IDC_AUDIOSYNC), workprefs.produce_sound);
 
    EnableWindow (GetDlgItem (hDlg, IDC_SOUNDFILTER), workprefs.produce_sound);

    EnableWindow (GetDlgItem (hDlg, IDC_SOUNDCALIBRATE), workprefs.produce_sound && full_property_sheet);
}

static int exact_log2 (int v)
{
    int l = 0;
    while ((v >>= 1) != 0)
	l++;
    return l;
}

static char *drivesounds;

static void sound_loaddrivesamples (void)
{
    WIN32_FIND_DATA fd;
    HANDLE h;
    char *p;
    int len = 0;
    char dirname[1024];

    free (drivesounds);
    p = drivesounds = 0;
    sprintf (dirname, "%s\\uae_data\\*.wav", start_path_data);
    h = FindFirstFile (dirname, &fd);
    if (h == INVALID_HANDLE_VALUE)
	return;
    for (;;) {
	if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
	    char *name = fd.cFileName;
	    if (strlen (name) > strlen (DS_NAME_CLICK) + 4 && !strncmp (name, DS_NAME_CLICK, strlen (DS_NAME_CLICK))) {
		if (p - drivesounds < 1000) {
		    char *oldp = p;
		    len += 2000;
		    drivesounds = p = realloc (drivesounds, len);
		    if (oldp) {
			do {
			    p = p + strlen (p) + 1;
			} while (p[0]);
		    }
		}
		strcpy (p, name + strlen (DS_NAME_CLICK));
		p[strlen(name + strlen (DS_NAME_CLICK)) - 4] = 0;
		p += strlen (p);
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
    int bufsize;
    char txt[20];

    bufsize = exact_log2 (workprefs.sound_maxbsiz / 1024);
    sprintf (txt, "%d (%dms)", bufsize, 1000 * (workprefs.sound_maxbsiz >> 1) / workprefs.sound_freq );
    SetDlgItemText (hDlg, IDC_SOUNDBUFFERMEM, txt);

    if (workprefs.sound_adjust < -100)
	workprefs.sound_adjust = -100;
    if (workprefs.sound_adjust > 30)
	workprefs.sound_adjust = 30;
    SendDlgItemMessage( hDlg, IDC_SOUNDADJUST, TBM_SETPOS, TRUE, workprefs.sound_adjust );
   
    sprintf (txt, "%.1f%%", workprefs.sound_adjust / 10.0);
    SetDlgItemText (hDlg, IDC_SOUNDADJUSTNUM, txt);

    SendDlgItemMessage( hDlg, IDC_SOUNDVOLUME, TBM_SETPOS, TRUE, 100 - workprefs.sound_volume );
    sprintf (txt, "%d%%", 100 - workprefs.sound_volume);
    SetDlgItemText (hDlg, IDC_SOUNDVOLUME2, txt);

    SendDlgItemMessage( hDlg, IDC_SOUNDDRIVEVOLUME, TBM_SETPOS, TRUE, 100 - workprefs.dfxclickvolume );
    sprintf (txt, "%d%%", 100 - workprefs.dfxclickvolume);
    SetDlgItemText (hDlg, IDC_SOUNDDRIVEVOLUME2, txt);
}

static int soundfreqs[] = { 11025, 15000, 22050, 32000, 44100, 48000, 0 };

static void values_to_sounddlg (HWND hDlg)
{
    int which_button;
    int sound_freq = workprefs.sound_freq;
    int produce_sound = workprefs.produce_sound;
    int stereo = workprefs.sound_stereo;
    char txt[100], txt2[100], *p;
    int i, selected;
    LRESULT idx;

    if (workprefs.sound_maxbsiz & (workprefs.sound_maxbsiz - 1))
	workprefs.sound_maxbsiz = DEFAULT_SOUND_MAXB;

    SendDlgItemMessage(hDlg, IDC_SOUNDFILTER, CB_RESETCONTENT, 0, 0);
    WIN32GUI_LoadUIString (IDS_SOUND_FILTER_OFF, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_SOUNDFILTER, CB_ADDSTRING, 0, (LPARAM)txt);
    WIN32GUI_LoadUIString (IDS_SOUND_FILTER_EMULATED, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_SOUNDFILTER, CB_ADDSTRING, 0, (LPARAM)txt);
    WIN32GUI_LoadUIString (IDS_SOUND_FILTER_EMULATED_E, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_SOUNDFILTER, CB_ADDSTRING, 0, (LPARAM)txt);
    WIN32GUI_LoadUIString (IDS_SOUND_FILTER_ON_AGA, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_SOUNDFILTER, CB_ADDSTRING, 0, (LPARAM)txt);
    WIN32GUI_LoadUIString (IDS_SOUND_FILTER_ON_A500, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_SOUNDFILTER, CB_ADDSTRING, 0, (LPARAM)txt);
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
    SendDlgItemMessage(hDlg, IDC_SOUNDFILTER, CB_SETCURSEL, i, 0 );

    SendDlgItemMessage(hDlg, IDC_SOUNDSTEREO, CB_RESETCONTENT, 0, 0);
    WIN32GUI_LoadUIString (IDS_SOUND_MONO, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_SOUNDSTEREO, CB_ADDSTRING, 0, (LPARAM)txt);
    WIN32GUI_LoadUIString (IDS_SOUND_STEREO, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_SOUNDSTEREO, CB_ADDSTRING, 0, (LPARAM)txt);
    WIN32GUI_LoadUIString (IDS_SOUND_STEREO2, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_SOUNDSTEREO, CB_ADDSTRING, 0, (LPARAM)txt);
    WIN32GUI_LoadUIString (IDS_SOUND_4CHANNEL, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_SOUNDSTEREO, CB_ADDSTRING, 0, (LPARAM)txt);
    SendDlgItemMessage (hDlg, IDC_SOUNDSTEREO, CB_SETCURSEL, workprefs.sound_stereo, 0);

    SendDlgItemMessage(hDlg, IDC_SOUNDSWAP, CB_RESETCONTENT, 0, 0);
    SendDlgItemMessage(hDlg, IDC_SOUNDSWAP, CB_ADDSTRING, 0, (LPARAM)"-");
    WIN32GUI_LoadUIString (IDS_SOUND_SWAP_PAULA, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_SOUNDSWAP, CB_ADDSTRING, 0, (LPARAM)txt);
    WIN32GUI_LoadUIString (IDS_SOUND_SWAP_AHI, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_SOUNDSWAP, CB_ADDSTRING, 0, (LPARAM)txt);
    WIN32GUI_LoadUIString (IDS_SOUND_SWAP_BOTH, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_SOUNDSWAP, CB_ADDSTRING, 0, (LPARAM)txt);
    SendDlgItemMessage(hDlg, IDC_SOUNDSWAP, CB_SETCURSEL, workprefs.sound_stereo_swap_paula + workprefs.sound_stereo_swap_ahi * 2, 0);

    SendDlgItemMessage(hDlg, IDC_SOUNDSTEREOSEP, CB_RESETCONTENT, 0, 0);
    for (i = 10; i >= 0; i--) {
	sprintf (txt, "%d%%", i * 10);
	SendDlgItemMessage(hDlg, IDC_SOUNDSTEREOSEP, CB_ADDSTRING, 0, (LPARAM)txt);
    }
    SendDlgItemMessage (hDlg, IDC_SOUNDSTEREOSEP, CB_SETCURSEL, 10 - workprefs.sound_stereo_separation, 0);

    SendDlgItemMessage(hDlg, IDC_SOUNDSTEREOMIX, CB_RESETCONTENT, 0, 0);
    SendDlgItemMessage(hDlg, IDC_SOUNDSTEREOMIX, CB_ADDSTRING, 0, (LPARAM)"-");
    for (i = 0; i < 10; i++) {
	sprintf (txt, "%d", i + 1);
	SendDlgItemMessage(hDlg, IDC_SOUNDSTEREOMIX, CB_ADDSTRING, 0, (LPARAM)txt);
    }
    SendDlgItemMessage (hDlg, IDC_SOUNDSTEREOMIX, CB_SETCURSEL, workprefs.sound_mixed_stereo > 0 ? workprefs.sound_mixed_stereo : 0, 0);
    
    SendDlgItemMessage(hDlg, IDC_SOUNDINTERPOLATION, CB_RESETCONTENT, 0, 0);
    WIN32GUI_LoadUIString (IDS_SOUND_INTERPOL_DISABLED, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_SOUNDINTERPOLATION, CB_ADDSTRING, 0, (LPARAM)txt);
    SendDlgItemMessage(hDlg, IDC_SOUNDINTERPOLATION, CB_ADDSTRING, 0, (LPARAM)"Anti");
    SendDlgItemMessage(hDlg, IDC_SOUNDINTERPOLATION, CB_ADDSTRING, 0, (LPARAM)"Sinc");
    WIN32GUI_LoadUIString (IDS_SOUND_INTERPOL_RH, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_SOUNDINTERPOLATION, CB_ADDSTRING, 0, (LPARAM)txt);
    WIN32GUI_LoadUIString (IDS_SOUND_INTERPOL_CRUX, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_SOUNDINTERPOLATION, CB_ADDSTRING, 0, (LPARAM)txt);
    SendDlgItemMessage (hDlg, IDC_SOUNDINTERPOLATION, CB_SETCURSEL, workprefs.sound_interpol, 0);

    SendDlgItemMessage(hDlg, IDC_SOUNDFREQ, CB_RESETCONTENT, 0, 0);
    i = 0;
    selected = -1;
    while (soundfreqs[i]) {
	sprintf (txt, "%d", soundfreqs[i]);
	SendDlgItemMessage( hDlg, IDC_SOUNDFREQ, CB_ADDSTRING, 0, (LPARAM)txt);
	i++;
    }
    sprintf (txt, "%d", workprefs.sound_freq);
    SendDlgItemMessage( hDlg, IDC_SOUNDFREQ, WM_SETTEXT, 0, (LPARAM)txt); 

    switch (workprefs.produce_sound) {
     case 0: which_button = IDC_SOUND0; break;
     case 1: which_button = IDC_SOUND1; break;
     case 2: which_button = IDC_SOUND2; break;
     case 3: which_button = IDC_SOUND3; break;
    }
    CheckRadioButton( hDlg, IDC_SOUND0, IDC_SOUND3, which_button );

    workprefs.sound_maxbsiz = 1 << exact_log2 (workprefs.sound_maxbsiz);
    if (workprefs.sound_maxbsiz < 2048)
	workprefs.sound_maxbsiz = 2048;
    SendDlgItemMessage( hDlg, IDC_SOUNDBUFFERRAM, TBM_SETPOS, TRUE, exact_log2 (workprefs.sound_maxbsiz / 2048));

    SendDlgItemMessage( hDlg, IDC_SOUNDVOLUME, TBM_SETPOS, TRUE, 0);
    SendDlgItemMessage( hDlg, IDC_SOUNDDRIVEVOLUME, TBM_SETPOS, TRUE, 0);

    SendDlgItemMessage( hDlg, IDC_SOUNDCARDLIST, CB_SETCURSEL, workprefs.win32_soundcard, 0 );

    idx = SendDlgItemMessage (hDlg, IDC_SOUNDDRIVE, CB_GETCURSEL, 0, 0);
    if (idx < 0)
	idx = 0;
    SendDlgItemMessage(hDlg, IDC_SOUNDDRIVE, CB_RESETCONTENT, 0, 0);
    for (i = 0; i < 4; i++) {
	sprintf (txt, "DF%d:", i);
	SendDlgItemMessage( hDlg, IDC_SOUNDDRIVE, CB_ADDSTRING, 0, (LPARAM)txt);
    }
    SendDlgItemMessage (hDlg, IDC_SOUNDDRIVE, CB_SETCURSEL, idx, 0);
    SendDlgItemMessage(hDlg, IDC_SOUNDDRIVESELECT, CB_RESETCONTENT, 0, 0);
    WIN32GUI_LoadUIString (IDS_DRIVESOUND_NONE, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_SOUNDDRIVESELECT, CB_ADDSTRING, 0, (LPARAM)txt);
    WIN32GUI_LoadUIString (IDS_DRIVESOUND_DEFAULT_A500, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_SOUNDDRIVESELECT, CB_ADDSTRING, 0, (LPARAM)txt);
    driveclick_fdrawcmd_detect();
    if (driveclick_pcdrivemask) {
	for (i = 0; i < 2; i++) {
	    WIN32GUI_LoadUIString (IDS_DRIVESOUND_PC_FLOPPY, txt, sizeof (txt));
	    sprintf (txt2, txt, 'A' + i);
	    SendDlgItemMessage(hDlg, IDC_SOUNDDRIVESELECT, CB_ADDSTRING, 0, (LPARAM)txt2);
	}
    }
    SendDlgItemMessage (hDlg, IDC_SOUNDDRIVESELECT, CB_SETCURSEL, 0, 0);
    p = drivesounds;
    if (p) {
	while (p[0]) {
	    SendDlgItemMessage(hDlg, IDC_SOUNDDRIVESELECT, CB_ADDSTRING, 0, (LPARAM)p);
	    p += strlen (p) + 1;
	}
    }
    if (workprefs.dfxclick[idx] < 0) {
	p = drivesounds;
	i = DS_BUILD_IN_SOUNDS + driveclick_pcdrivenum + 1;
	while (p && p[0]) {
	    if (!strcmpi (p, workprefs.dfxclickexternal[idx])) {
		SendDlgItemMessage (hDlg, IDC_SOUNDDRIVESELECT, CB_SETCURSEL, i, 0);
		break;
	    }
	    i++;
	    p += strlen (p) + 1;
	}

    } else {
	SendDlgItemMessage (hDlg, IDC_SOUNDDRIVESELECT, CB_SETCURSEL, workprefs.dfxclick[idx], 0);
    }

    update_soundgui (hDlg);
}

static void values_from_sounddlg (HWND hDlg)
{
    char txt[10];
    LRESULT idx;

    idx = SendDlgItemMessage (hDlg, IDC_SOUNDFREQ, CB_GETCURSEL, 0, 0);
    if (idx >= 0) {
	workprefs.sound_freq = soundfreqs[idx];
    } else {
	SendDlgItemMessage (hDlg, IDC_SOUNDFREQ, WM_GETTEXT, (WPARAM)sizeof (txt), (LPARAM)txt);
	workprefs.sound_freq = atol (txt);
    }
    if (workprefs.sound_freq < 8000)
	workprefs.sound_freq = 8000;
    if (workprefs.sound_freq > 96000)
	workprefs.sound_freq = 96000;

    workprefs.produce_sound = (IsDlgButtonChecked (hDlg, IDC_SOUND0) ? 0
			       : IsDlgButtonChecked (hDlg, IDC_SOUND1) ? 1
			       : IsDlgButtonChecked (hDlg, IDC_SOUND2) ? 2 : 3);
    idx = SendDlgItemMessage (hDlg, IDC_SOUNDSTEREO, CB_GETCURSEL, 0, 0);
    if (idx != CB_ERR)
	workprefs.sound_stereo = idx;
    workprefs.sound_stereo_separation = 0;
    workprefs.sound_mixed_stereo = 0;
    if (workprefs.sound_stereo > 0) {
	idx = SendDlgItemMessage (hDlg, IDC_SOUNDSTEREOSEP, CB_GETCURSEL, 0, 0);
	if (idx >= 0) {
	    if (idx > 0)
		workprefs.sound_mixed_stereo = -1;
	    workprefs.sound_stereo_separation = 10 - idx;
	}
	idx = SendDlgItemMessage (hDlg, IDC_SOUNDSTEREOMIX, CB_GETCURSEL, 0, 0);
	if (idx > 0)
	    workprefs.sound_mixed_stereo = idx;
    }

    workprefs.sound_interpol = SendDlgItemMessage (hDlg, IDC_SOUNDINTERPOLATION, CB_GETCURSEL, 0, 0);
    workprefs.win32_soundcard = SendDlgItemMessage (hDlg, IDC_SOUNDCARDLIST, CB_GETCURSEL, 0, 0L);
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

    idx = SendDlgItemMessage (hDlg, IDC_SOUNDDRIVE, CB_GETCURSEL, 0, 0);
    if (idx >= 0) {
	LRESULT res = SendDlgItemMessage (hDlg, IDC_SOUNDDRIVESELECT, CB_GETCURSEL, 0, 0);
	if (res >= 0) {
	    if (res > DS_BUILD_IN_SOUNDS + driveclick_pcdrivenum) {
		int j = res - (DS_BUILD_IN_SOUNDS + driveclick_pcdrivenum + 1);
		char *p = drivesounds;
		while (j-- > 0)
		    p += strlen (p) + 1;
		workprefs.dfxclick[idx] = -1;
		strcpy (workprefs.dfxclickexternal[idx], p);
	    } else {
		workprefs.dfxclick[idx] = res;
		workprefs.dfxclickexternal[idx][0] = 0;
	    }
	}
    }
}

extern int sound_calibrate (HWND, struct uae_prefs*);

static INT_PTR CALLBACK SoundDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int recursive = 0;
    int numdevs;
    int card;
    char **sounddevs;

    switch (msg) {
    case WM_INITDIALOG:
	sound_loaddrivesamples ();
	SendDlgItemMessage (hDlg, IDC_SOUNDBUFFERRAM, TBM_SETRANGE, TRUE, MAKELONG (MIN_SOUND_MEM, MAX_SOUND_MEM));
	SendDlgItemMessage (hDlg, IDC_SOUNDBUFFERRAM, TBM_SETPAGESIZE, 0, 1);

	SendDlgItemMessage (hDlg, IDC_SOUNDVOLUME, TBM_SETRANGE, TRUE, MAKELONG (0, 100));
	SendDlgItemMessage (hDlg, IDC_SOUNDVOLUME, TBM_SETPAGESIZE, 0, 1);

	SendDlgItemMessage (hDlg, IDC_SOUNDDRIVEVOLUME, TBM_SETRANGE, TRUE, MAKELONG (0, 100));
	SendDlgItemMessage (hDlg, IDC_SOUNDDRIVEVOLUME, TBM_SETPAGESIZE, 0, 1);

	SendDlgItemMessage( hDlg, IDC_SOUNDADJUST, TBM_SETRANGE, TRUE, MAKELONG (-100, +30) );
	SendDlgItemMessage( hDlg, IDC_SOUNDADJUST, TBM_SETPAGESIZE, 0, 1 );

	SendDlgItemMessage( hDlg, IDC_SOUNDCARDLIST, CB_RESETCONTENT, 0, 0L );
	sounddevs = enumerate_sound_devices (&numdevs);
	for (card = 0; card < numdevs; card++)
	    SendDlgItemMessage (hDlg, IDC_SOUNDCARDLIST, CB_ADDSTRING, 0, (LPARAM)sounddevs[card]);
	if (numdevs == 0)
	    workprefs.produce_sound = 0; /* No sound card in system, enable_for_sounddlg will accomodate this */

	pages[SOUND_ID] = hDlg;
	currentpage = SOUND_ID;

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
	if (LOWORD (wParam) == IDC_SOUNDCALIBRATE) {
	    int pct = sound_calibrate (hDlg, &workprefs);
	    workprefs.sound_adjust = (pct - 1000);
	    update_soundgui (hDlg);
	} else if(LOWORD (wParam) == IDC_SOUNDDRIVE) {
	    values_to_sounddlg (hDlg);
	}
	values_from_sounddlg (hDlg);
	enable_for_sounddlg (hDlg);
	recursive--;
	break;

     case WM_HSCROLL:
	workprefs.sound_maxbsiz = 2048 << SendMessage(GetDlgItem(hDlg, IDC_SOUNDBUFFERRAM), TBM_GETPOS, 0, 0);
	workprefs.sound_adjust = SendMessage(GetDlgItem(hDlg, IDC_SOUNDADJUST), TBM_GETPOS, 0, 0);
	workprefs.sound_volume = 100 - SendMessage(GetDlgItem(hDlg, IDC_SOUNDVOLUME), TBM_GETPOS, 0, 0);
	workprefs.dfxclickvolume = 100 - SendMessage(GetDlgItem(hDlg, IDC_SOUNDDRIVEVOLUME), TBM_GETPOS, 0, 0);
	update_soundgui (hDlg);
	break;
    }
    return FALSE;
}

#ifdef FILESYS

struct fsvdlg_vals
{
    char volume[4096];
    char device[4096];
    char rootdir[4096];
    int bootpri;
    int rw;
    int rdb;
};

static struct fsvdlg_vals empty_fsvdlg = { "", "", "", 0, 1, 0 };
static struct fsvdlg_vals current_fsvdlg;

struct hfdlg_vals
{
    char volumename[4096];
    char devicename[4096];
    char filename[4096];
    char fsfilename[4096];
    int sectors;
    int reserved;
    int surfaces;
    int cylinders;
    int blocksize;
    int rw;
    int rdb;
    int bootpri;
};

static struct hfdlg_vals empty_hfdlg = { "", "", "", "", 32, 2, 1, 0, 512, 1, 0, 0 };
static struct hfdlg_vals current_hfdlg;

static INT_PTR CALLBACK VolumeSettingsProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int recursive = 0;
    BROWSEINFO browse_info;
    char directory_path[MAX_DPATH] = "";
    LPITEMIDLIST browse;
    char szTitle[ MAX_DPATH ];

    WIN32GUI_LoadUIString( IDS_SELECTFILESYSROOT, szTitle, MAX_DPATH );

    browse_info.hwndOwner = hDlg;
    browse_info.pidlRoot = NULL;
    browse_info.pszDisplayName = directory_path;
    browse_info.lpszTitle = "";
    browse_info.ulFlags = BIF_DONTGOBELOWDOMAIN | BIF_RETURNONLYFSDIRS;
    browse_info.lpfn = NULL;
    browse_info.iImage = 0;

    switch (msg) {
     case WM_INITDIALOG:
	recursive++;
	SetDlgItemText (hDlg, IDC_VOLUME_NAME, current_fsvdlg.volume);
	SetDlgItemText (hDlg, IDC_VOLUME_DEVICE, current_fsvdlg.device);
	SetDlgItemText (hDlg, IDC_PATH_NAME, current_fsvdlg.rootdir);
	SetDlgItemInt (hDlg, IDC_VOLUME_BOOTPRI, current_fsvdlg.bootpri, TRUE);
	CheckDlgButton (hDlg, IDC_RW, current_fsvdlg.rw);
	recursive--;
	return TRUE;

     case WM_COMMAND:
	if (recursive)
	    break;
	recursive++;
	if (HIWORD (wParam) == BN_CLICKED) {
	    switch (LOWORD (wParam)) {
	     case IDC_SELECTOR:
		if ((browse = SHBrowseForFolder (&browse_info)) != NULL) {
		    SHGetPathFromIDList (browse, directory_path);
		    SetDlgItemText (hDlg, IDC_PATH_NAME, directory_path);
		}
		break;
	     case IDOK:
		    if( strlen( current_fsvdlg.rootdir ) == 0 ) 
		    {
			char szMessage[ MAX_DPATH ];
			char szTitle[ MAX_DPATH ];
			WIN32GUI_LoadUIString( IDS_MUSTSELECTPATH, szMessage, MAX_DPATH );
			WIN32GUI_LoadUIString( IDS_SETTINGSERROR, szTitle, MAX_DPATH );

			MessageBox( hDlg, szMessage, szTitle,
				MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
			break;
		    }
		    if( strlen( current_fsvdlg.volume ) == 0 )
		    {
			char szMessage[ MAX_DPATH ];
			char szTitle[ MAX_DPATH ];
			WIN32GUI_LoadUIString( IDS_MUSTSELECTNAME, szMessage, MAX_DPATH );
			WIN32GUI_LoadUIString( IDS_SETTINGSERROR, szTitle, MAX_DPATH );

			MessageBox( hDlg, szMessage, szTitle,
				MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
			break;
		    }
		EndDialog (hDlg, 1);

		break;
	     case IDCANCEL:
		EndDialog (hDlg, 0);
		break;
	    }
	}
	GetDlgItemText (hDlg, IDC_PATH_NAME, current_fsvdlg.rootdir, sizeof current_fsvdlg.rootdir);
	GetDlgItemText (hDlg, IDC_VOLUME_NAME, current_fsvdlg.volume, sizeof current_fsvdlg.volume);
	GetDlgItemText (hDlg, IDC_VOLUME_DEVICE, current_fsvdlg.device, sizeof current_fsvdlg.device);
	current_fsvdlg.rw = IsDlgButtonChecked (hDlg, IDC_RW);
	current_fsvdlg.bootpri = GetDlgItemInt( hDlg, IDC_VOLUME_BOOTPRI, NULL, TRUE );
	recursive--;
	break;
    }
    return FALSE;
}

static void sethardfile (HWND hDlg)
{
    SetDlgItemText (hDlg, IDC_PATH_NAME, current_hfdlg.filename);
    SetDlgItemText (hDlg, IDC_PATH_FILESYS, current_hfdlg.fsfilename);
    SetDlgItemText (hDlg, IDC_HARDFILE_DEVICE, current_hfdlg.devicename);
    SetDlgItemInt( hDlg, IDC_SECTORS, current_hfdlg.sectors, FALSE);
    SetDlgItemInt( hDlg, IDC_HEADS, current_hfdlg.surfaces, FALSE);
    SetDlgItemInt( hDlg, IDC_RESERVED, current_hfdlg.reserved, FALSE);
    SetDlgItemInt( hDlg, IDC_BLOCKSIZE, current_hfdlg.blocksize, FALSE);
    SetDlgItemInt( hDlg, IDC_HARDFILE_BOOTPRI, current_hfdlg.bootpri, TRUE);
    CheckDlgButton (hDlg, IDC_RW, current_hfdlg.rw);
    EnableWindow (GetDlgItem (hDlg, IDC_HDF_RDB),
	!(current_hfdlg.sectors == 0 && current_hfdlg.surfaces == 0 && current_hfdlg.reserved == 0));
}

static void inithardfile (HWND hDlg)
{
    char tmp[MAX_DPATH];

    EnableWindow (GetDlgItem (hDlg, IDC_HF_DOSTYPE), FALSE);
    EnableWindow (GetDlgItem (hDlg, IDC_HF_CREATE), FALSE);
    SendDlgItemMessage(hDlg, IDC_HF_TYPE, CB_RESETCONTENT, 0, 0);
    WIN32GUI_LoadUIString (IDS_HF_FS_CUSTOM, tmp, sizeof (tmp));
    SendDlgItemMessage (hDlg, IDC_HF_TYPE, CB_ADDSTRING, 0, (LPARAM)"OFS/FFS/RDB");
    SendDlgItemMessage (hDlg, IDC_HF_TYPE, CB_ADDSTRING, 0, (LPARAM)"SFS");
    SendDlgItemMessage (hDlg, IDC_HF_TYPE, CB_ADDSTRING, 0, (LPARAM)tmp);
    SendDlgItemMessage (hDlg, IDC_HF_TYPE, CB_SETCURSEL, 0, 0);
}

static void sethfdostype (HWND hDlg, int idx)
{
    if (idx == 1)
	SetDlgItemText (hDlg, IDC_HF_DOSTYPE, "0x53465300");
    else
	SetDlgItemText (hDlg, IDC_HF_DOSTYPE, "");
}

static void hardfile_testrdb (HWND hDlg)
{
    void *f = zfile_fopen (current_hfdlg.filename, "rb");
    char tmp[8] = { 0 };
    if (!f)
	return;
    zfile_fread (tmp, 1, sizeof (tmp), f);
    zfile_fclose (f);
    if (memcmp (tmp, "RDSK\0\0\0", 7))
	return;
    current_hfdlg.sectors = 0;
    current_hfdlg.surfaces = 0;
    current_hfdlg.reserved = 0;
    current_hfdlg.fsfilename[0] = 0;
    current_hfdlg.bootpri = 0;
    current_hfdlg.devicename[0] = 0;
    sethardfile (hDlg);
}

static INT_PTR CALLBACK HardfileSettingsProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int recursive = 0;
    LRESULT res;

    switch (msg) {
    case WM_INITDIALOG:
	recursive++;
	inithardfile (hDlg);
	sethardfile (hDlg);
	sethfdostype (hDlg, 0);
	recursive--;
	return TRUE;

    case WM_COMMAND:
	if (recursive)
	    break;
	recursive++;

	switch (LOWORD (wParam)) {
	    case IDC_HF_SIZE:
		EnableWindow (GetDlgItem (hDlg, IDC_HF_CREATE), CalculateHardfileSize (hDlg) > 0);
	    break;
	    case IDC_HF_TYPE:
		res = SendDlgItemMessage (hDlg, IDC_HF_TYPE, CB_GETCURSEL, 0, 0);
		sethfdostype (hDlg, (int)res);
		EnableWindow (GetDlgItem (hDlg, IDC_HF_DOSTYPE), res >= 2);
	    break;
	    case IDC_HF_CREATE:
	    {
		UINT setting = CalculateHardfileSize (hDlg);
		char dostype[16];
		GetDlgItemText (hDlg, IDC_HF_DOSTYPE, dostype, sizeof (dostype));
		res = SendDlgItemMessage (hDlg, IDC_HF_TYPE, CB_GETCURSEL, 0, 0);
		if (res == 0)
		    dostype[0] = 0;
		if (!CreateHardFile(hDlg, setting, dostype)) {
		    char szMessage[MAX_DPATH];
		    char szTitle[MAX_DPATH];
		    WIN32GUI_LoadUIString (IDS_FAILEDHARDFILECREATION, szMessage, MAX_DPATH);
		    WIN32GUI_LoadUIString (IDS_CREATIONERROR, szTitle, MAX_DPATH);
		    MessageBox(hDlg, szMessage, szTitle,
				MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
		}
	    }
	    break;
	    case IDC_SELECTOR:
		DiskSelection (hDlg, IDC_PATH_NAME, 2, &workprefs, 0);
		GetDlgItemText (hDlg, IDC_PATH_NAME, current_hfdlg.filename, sizeof current_hfdlg.filename);
		hardfile_testrdb (hDlg);
		break;
	    case IDC_FILESYS_SELECTOR:
		DiskSelection (hDlg, IDC_PATH_FILESYS, 12, &workprefs, 0);
		break;
	    case IDOK:
		if( strlen(current_hfdlg.filename) == 0 ) 
		{
		    char szMessage[MAX_DPATH];
		    char szTitle[MAX_DPATH];
		    WIN32GUI_LoadUIString( IDS_MUSTSELECTFILE, szMessage, MAX_DPATH );
		    WIN32GUI_LoadUIString( IDS_SETTINGSERROR, szTitle, MAX_DPATH );

		    MessageBox( hDlg, szMessage, szTitle,
				MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
		    break;
		}
		EndDialog (hDlg, 1);
		break;
	    case IDCANCEL:
		EndDialog (hDlg, 0);
		break;
	    case IDC_RW:
		current_hfdlg.rw = IsDlgButtonChecked (hDlg, IDC_RW);
		break;
	    case IDC_HDF_RDB:
		SetDlgItemInt (hDlg, IDC_SECTORS, 0, FALSE);
		SetDlgItemInt (hDlg, IDC_RESERVED, 0, FALSE);
		SetDlgItemInt (hDlg, IDC_HEADS, 0, FALSE);
		SetDlgItemText (hDlg, IDC_PATH_FILESYS, "");
		SetDlgItemText (hDlg, IDC_HARDFILE_DEVICE, "");
		current_hfdlg.sectors = current_hfdlg.reserved = current_hfdlg.surfaces = 0;
		sethardfile (hDlg);
		break;
	}

	GetDlgItemText (hDlg, IDC_PATH_NAME, current_hfdlg.filename, sizeof current_hfdlg.filename);
	GetDlgItemText (hDlg, IDC_PATH_FILESYS, current_hfdlg.fsfilename, sizeof current_hfdlg.fsfilename);
	GetDlgItemText (hDlg, IDC_HARDFILE_DEVICE, current_hfdlg.devicename, sizeof current_hfdlg.devicename);
	current_hfdlg.sectors   = GetDlgItemInt( hDlg, IDC_SECTORS, NULL, FALSE );
	current_hfdlg.reserved  = GetDlgItemInt( hDlg, IDC_RESERVED, NULL, FALSE );
	current_hfdlg.surfaces  = GetDlgItemInt( hDlg, IDC_HEADS, NULL, FALSE );
	current_hfdlg.blocksize = GetDlgItemInt( hDlg, IDC_BLOCKSIZE, NULL, FALSE );
	current_hfdlg.bootpri = GetDlgItemInt( hDlg, IDC_HARDFILE_BOOTPRI, NULL, TRUE );
	recursive--;

	break;
    }
    return FALSE;
}

extern int harddrive_to_hdf(HWND, struct uae_prefs*, int);
static INT_PTR CALLBACK HarddriveSettingsProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int recursive = 0;
    int i, index;
    LRESULT posn;
    static int oposn;

    switch (msg) {
    case WM_INITDIALOG:
	oposn = -1;
	hdf_init ();
	recursive++;
	CheckDlgButton (hDlg, IDC_RW, current_hfdlg.rw);
	SendDlgItemMessage(hDlg, IDC_HARDDRIVE, CB_RESETCONTENT, 0, 0);
	EnableWindow(GetDlgItem(hDlg, IDC_HARDDRIVE_IMAGE), FALSE);
	EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
	EnableWindow(GetDlgItem(hDlg, IDC_RW), FALSE);
	index = -1;
	for (i = 0; i < hdf_getnumharddrives(); i++) {
	    SendDlgItemMessage( hDlg, IDC_HARDDRIVE, CB_ADDSTRING, 0, (LPARAM)hdf_getnameharddrive(i, 1));
	    if (!strcmp (current_hfdlg.filename, hdf_getnameharddrive (i, 0))) index = i;
	}
	if (index >= 0)
	    SendDlgItemMessage (hDlg, IDC_HARDDRIVE, CB_SETCURSEL, index, 0);
	recursive--;
	return TRUE;

    case WM_COMMAND:
	if (recursive)
	    break;
	recursive++;
	posn = SendDlgItemMessage (hDlg, IDC_HARDDRIVE, CB_GETCURSEL, 0, 0);
	if (oposn != posn) {
	    oposn = posn;
	    if (posn >= 0) {
		EnableWindow(GetDlgItem(hDlg, IDC_HARDDRIVE_IMAGE), TRUE);
		EnableWindow(GetDlgItem(hDlg, IDOK), TRUE);
		EnableWindow(GetDlgItem(hDlg, IDC_RW), TRUE);
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
		    harddrive_to_hdf(hDlg, &workprefs, posn);
		break;
	    }
	}
	if (posn != CB_ERR)
	    strcpy (current_hfdlg.filename, hdf_getnameharddrive ((int)posn, 0));
	current_hfdlg.rw = IsDlgButtonChecked (hDlg, IDC_RW);
	recursive--;
	break;
    }
    return FALSE;
}

static void new_filesys (HWND hDlg)
{
    const char *result;

    result = add_filesys_unit (currprefs.mountinfo, current_fsvdlg.device, current_fsvdlg.volume,
		    current_fsvdlg.rootdir, ! current_fsvdlg.rw, 0, 0, 0, 0, current_fsvdlg.bootpri, 0, 0);
    if (result)
	MessageBox (hDlg, result, result,
		    MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
}

static void new_hardfile (HWND hDlg)
{
    const char *result;

    result = add_filesys_unit (currprefs.mountinfo, current_hfdlg.devicename, 0,
				current_hfdlg.filename, ! current_hfdlg.rw,
				current_hfdlg.sectors, current_hfdlg.surfaces,
				current_hfdlg.reserved, current_hfdlg.blocksize,
				current_hfdlg.bootpri, current_hfdlg.fsfilename, 0);
    if (result)
	MessageBox (hDlg, result, result,
		    MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
}

static void new_harddrive (HWND hDlg)
{
    const char *result;

    result = add_filesys_unit (currprefs.mountinfo, 0, 0,
				current_hfdlg.filename, ! current_hfdlg.rw, 0, 0,
				0, current_hfdlg.blocksize, 0, 0, 0);
    if (result)
	MessageBox (hDlg, result, result,
		    MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
}

static void harddisk_remove (HWND hDlg)
{
    int entry = listview_find_selected (GetDlgItem (hDlg, IDC_VOLUMELIST));
    if (entry < 0)
	return;
    kill_filesys_unit (currprefs.mountinfo, entry);
}

static void harddisk_move (HWND hDlg, int up)
{
    int entry = listview_find_selected (GetDlgItem (hDlg, IDC_VOLUMELIST));
    if (entry < 0)
	return;
    move_filesys_unit (currprefs.mountinfo, entry, up ? entry - 1 : entry + 1);
}

static void harddisk_edit (HWND hDlg)
{
    int entry = listview_find_selected (GetDlgItem (hDlg, IDC_VOLUMELIST));
    char *volname, *devname, *rootdir, *filesys;
    int secspertrack, surfaces, cylinders, reserved, blocksize, readonly, type, bootpri;
    uae_u64 size;
    const char *failure;

    if (entry < 0)
	return;
    
    failure = get_filesys_unit (currprefs.mountinfo, entry, &devname, &volname, &rootdir, &readonly,
			    &secspertrack, &surfaces, &reserved, &cylinders, &size,
			    &blocksize, &bootpri, &filesys, 0);

    type = is_hardfile( currprefs.mountinfo, entry );
    if( type == FILESYS_HARDFILE || type == FILESYS_HARDFILE_RDB )
    {
	current_hfdlg.sectors = secspertrack;
	current_hfdlg.surfaces = surfaces;
	current_hfdlg.reserved = reserved;
	current_hfdlg.cylinders = cylinders;
	current_hfdlg.blocksize = blocksize;

	strncpy (current_hfdlg.filename, rootdir, (sizeof current_hfdlg.filename) - 1);
	current_hfdlg.filename[(sizeof current_hfdlg.filename) - 1] = '\0';
	current_hfdlg.fsfilename[0] = 0;
	if (filesys) {
	    strncpy (current_hfdlg.fsfilename, filesys, (sizeof current_hfdlg.fsfilename) - 1);
	    current_hfdlg.fsfilename[(sizeof current_hfdlg.fsfilename) - 1] = '\0';
	}
	current_fsvdlg.device[0] = 0;
	if (devname) {
	    strncpy (current_hfdlg.devicename, devname, (sizeof current_hfdlg.devicename) - 1);
	    current_hfdlg.devicename[(sizeof current_hfdlg.devicename) - 1] = '\0';
	}
	current_hfdlg.rw = !readonly;
	current_hfdlg.bootpri = bootpri;
	if (DialogBox( hUIDLL ? hUIDLL : hInst, MAKEINTRESOURCE (IDD_HARDFILE), hDlg, HardfileSettingsProc)) 
	{
	    const char *result;
	    result = set_filesys_unit (currprefs.mountinfo, entry, current_hfdlg.devicename, 0, current_hfdlg.filename,
					! current_hfdlg.rw, current_hfdlg.sectors, current_hfdlg.surfaces,
					current_hfdlg.reserved, current_hfdlg.blocksize, current_hfdlg.bootpri, current_hfdlg.fsfilename, 0);
	    if (result)
		MessageBox (hDlg, result, result,
		MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
	}
    }
    else if (type == FILESYS_HARDDRIVE) /* harddisk */
    {
	current_hfdlg.rw = !readonly;
	strncpy (current_hfdlg.filename, rootdir, (sizeof current_hfdlg.filename) - 1);
	current_hfdlg.filename[(sizeof current_hfdlg.filename) - 1] = '\0';
	if (DialogBox( hUIDLL ? hUIDLL : hInst, MAKEINTRESOURCE (IDD_HARDDRIVE), hDlg, HarddriveSettingsProc)) 
	{
	    const char *result;
	    result = set_filesys_unit (currprefs.mountinfo, entry, 0, 0, current_hfdlg.filename,
					! current_hfdlg.rw, 0, 0,
					0, current_hfdlg.blocksize, current_hfdlg.bootpri, 0, 0);
	    if (result)
		MessageBox (hDlg, result, result,
		MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
	}
    }
    else /* Filesystem */
    {
	strncpy (current_fsvdlg.rootdir, rootdir, (sizeof current_fsvdlg.rootdir) - 1);
	current_fsvdlg.rootdir[(sizeof current_fsvdlg.rootdir) - 1] = '\0';
	strncpy (current_fsvdlg.volume, volname, (sizeof current_fsvdlg.volume) - 1);
	current_fsvdlg.volume[(sizeof current_fsvdlg.volume) - 1] = '\0';
	current_fsvdlg.device[0] = 0;
	if (devname) {
	    strncpy (current_fsvdlg.device, devname, (sizeof current_fsvdlg.device) - 1);
	    current_fsvdlg.device[(sizeof current_fsvdlg.device) - 1] = '\0';
	}
	current_fsvdlg.rw = !readonly;
	current_fsvdlg.bootpri = bootpri;
	if (DialogBox( hUIDLL ? hUIDLL : hInst, MAKEINTRESOURCE (IDD_FILESYS), hDlg, VolumeSettingsProc)) {
	    const char *result;
	    result = set_filesys_unit (currprefs.mountinfo, entry, current_fsvdlg.device, current_fsvdlg.volume,
					current_fsvdlg.rootdir, ! current_fsvdlg.rw, 0, 0, 0, 0, current_fsvdlg.bootpri, 0, 0);
	    if (result)
		MessageBox (hDlg, result, result,
		MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
	}
    }
}

static ACCEL HarddiskAccel[] = {
    { FVIRTKEY, VK_UP, 10001 }, { FVIRTKEY, VK_DOWN, 10002 },
    { FVIRTKEY|FSHIFT, VK_UP, IDC_UP }, { FVIRTKEY|FSHIFT, VK_DOWN, IDC_DOWN },
    { FVIRTKEY, VK_RETURN, IDC_EDIT }, { FVIRTKEY, VK_DELETE, IDC_REMOVE },
    { 0, 0, 0 }
};

static void harddiskdlg_button (HWND hDlg, int button)
{
    switch (button) {
     case IDC_NEW_FS:
	current_fsvdlg = empty_fsvdlg;
	if (DialogBox (hUIDLL ? hUIDLL : hInst, MAKEINTRESOURCE (IDD_FILESYS), hDlg, VolumeSettingsProc))
	    new_filesys (hDlg);
	break;

     case IDC_NEW_HF:
	current_hfdlg = empty_hfdlg;
	if (DialogBox (hUIDLL ? hUIDLL : hInst, MAKEINTRESOURCE (IDD_HARDFILE), hDlg, HardfileSettingsProc))
	    new_hardfile (hDlg);
	break;

     case IDC_NEW_HD:
	memset (&current_hfdlg, 0, sizeof (current_hfdlg));
	if (hdf_init() == 0) {
	    char tmp[MAX_DPATH];
	    WIN32GUI_LoadUIString (IDS_NOHARDDRIVES, tmp, sizeof (tmp));
	    gui_message (tmp);
	} else {
	    if (DialogBox (hUIDLL ? hUIDLL : hInst, MAKEINTRESOURCE (IDD_HARDDRIVE), hDlg, HarddriveSettingsProc))
		new_harddrive (hDlg);
	}
	break;

     case IDC_EDIT:
	harddisk_edit (hDlg);
	break;

     case IDC_REMOVE:
	harddisk_remove (hDlg);
	break;

     case IDC_UP:
	harddisk_move (hDlg, 1);
	clicked_entry--;
	break;

     case IDC_DOWN:
	harddisk_move (hDlg, 0);
	clicked_entry++;
	break;

     case IDC_MAPDRIVES:
	workprefs.win32_automount_drives = IsDlgButtonChecked(hDlg, IDC_MAPDRIVES);
	break;

     case IDC_MAPDRIVES_NET:
	workprefs.win32_automount_netdrives = IsDlgButtonChecked(hDlg, IDC_MAPDRIVES_NET);
	break;

    case IDC_NOUAEFSDB:
	workprefs.filesys_no_uaefsdb = IsDlgButtonChecked(hDlg, IDC_NOUAEFSDB);
	break;

    case IDC_NORECYCLEBIN:
	workprefs.win32_norecyclebin = IsDlgButtonChecked(hDlg, IDC_NORECYCLEBIN);
	break;

    }
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
    ListView_SetItemState( cachedlist, clicked_entry, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED );
}

static INT_PTR CALLBACK HarddiskDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{

    switch (msg) {
    case WM_INITDIALOG:
	clicked_entry = 0;
	pages[HARDDISK_ID] = hDlg;
	currentpage = HARDDISK_ID;
	EnableWindow (GetDlgItem(hDlg, IDC_NEW_HD), os_winnt && os_winnt_admin ? TRUE : FALSE);
	
    case WM_USER:
	CheckDlgButton (hDlg, IDC_MAPDRIVES, workprefs.win32_automount_drives);
	CheckDlgButton (hDlg, IDC_MAPDRIVES_NET, workprefs.win32_automount_netdrives);
	CheckDlgButton (hDlg, IDC_NOUAEFSDB, workprefs.filesys_no_uaefsdb);
	CheckDlgButton (hDlg, IDC_NORECYCLEBIN, workprefs.win32_norecyclebin);
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
	    move_filesys_unit (currprefs.mountinfo, draggeditems[0], item);
	    InitializeListView (hDlg);
	    clicked_entry = item;
	    hilitehd ();
	}
	xfree (draggeditems);
	break;
    }

    case WM_COMMAND:
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
	    harddiskdlg_button (hDlg, LOWORD (wParam));
	    InitializeListView (hDlg);
	    hilitehd ();
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
    char txt[30];
    char tmp1[MAX_DPATH];
    char tmp2[MAX_DPATH];

    WIN32GUI_LoadUIString (IDS_FLOPPY_COMPATIBLE, tmp1, sizeof (tmp1));
    WIN32GUI_LoadUIString (IDS_FLOPPY_TURBO, tmp2, sizeof (tmp2));
    if (workprefs.floppy_speed)
	sprintf (txt, "%d%%%s", workprefs.floppy_speed, workprefs.floppy_speed == 100 ? tmp1 : "");
    else
	strcpy (txt, tmp2);
    SetDlgItemText (hDlg, IDC_FLOPPYSPDTEXT, txt);
}

#define BUTTONSPERFLOPPY 6
static int floppybuttons[][BUTTONSPERFLOPPY] = {
    { IDC_DF0TEXT,IDC_DF0,IDC_EJECT0,IDC_DF0TYPE,IDC_DF0WP,IDC_SAVEIMAGE0 },
    { IDC_DF1TEXT,IDC_DF1,IDC_EJECT1,IDC_DF1TYPE,IDC_DF1WP,IDC_SAVEIMAGE1 },
    { IDC_DF2TEXT,IDC_DF2,IDC_EJECT2,IDC_DF2TYPE,IDC_DF2WP,IDC_SAVEIMAGE2 },
    { IDC_DF3TEXT,IDC_DF3,IDC_EJECT3,IDC_DF3TYPE,IDC_DF3WP,IDC_SAVEIMAGE3 }
};
static int floppybuttonsq[][BUTTONSPERFLOPPY] = {
    { IDC_DF0TEXTQ,IDC_DF0QQ,IDC_EJECT0Q,-1,IDC_DF0WPQ,-1 },
    { IDC_DF1TEXTQ,IDC_DF1QQ,IDC_EJECT1Q,-1,IDC_DF1WPQ,-1 },
    { -1,-1,-1,-1,-1,-1 },
    { -1,-1,-1,-1,-1,-1 }
};

static void floppytooltip (HWND hDlg, int num, uae_u32 crc32)
{
    TOOLINFO ti;
    int id;
    char tmp[100];
    
    if (currentpage == QUICKSTART_ID)
	id = floppybuttonsq[num][0];
    else
	id = floppybuttons[num][0];
    ti.cbSize = sizeof(TOOLINFO);
    ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
    ti.hwnd = hDlg;
    ti.hinst = hInst;
    ti.uId = (UINT_PTR)GetDlgItem (hDlg, id);
    SendMessage (ToolTipHWND, TTM_DELTOOL, 0, (LPARAM) (LPTOOLINFO) &ti);
    if (crc32 == 0)
	return;
    sprintf (tmp, "CRC=%08.8X", crc32);
    ti.lpszText = tmp;
    SendMessage (ToolTipHWND, TTM_ADDTOOL, 0, (LPARAM) (LPTOOLINFO) &ti);	
}

static void addfloppyhistory (HWND hDlg, HKEY fkey, int n, int f_text)
{
    int i, j;
    char *s;
    int nn = workprefs.dfxtype[n] + 1;

    if (f_text < 0)
	return;
    SendDlgItemMessage(hDlg, f_text, CB_RESETCONTENT, 0, 0);
    SendDlgItemMessage(hDlg, f_text, WM_SETTEXT, 0, (LPARAM)workprefs.df[n]);
    i = 0;
    while (s = DISK_history_get (i)) {
	char tmpname[MAX_DPATH], tmppath[MAX_DPATH], *p, *p2;
	if (strlen(s) == 0)
	    break;
	i++;
	strcpy (tmppath, s);
        p = tmppath + strlen(tmppath) - 1;
	for (j = 0; archive_extensions[j]; j++) {
	    p2 = strstr (tmppath, archive_extensions[j]);
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
	strcpy (tmpname, p + 1);
	*++p = 0;
	if (tmppath[0]) {
	    strcat (tmpname, " { ");
	    strcat (tmpname, tmppath);
	    strcat (tmpname, " }");
	}
	if (f_text >= 0)
	    SendDlgItemMessage (hDlg, f_text, CB_ADDSTRING, 0, (LPARAM)tmpname);
	if (!strcmp (workprefs.df[n], s)) {
	    if (f_text >= 0)
		SendDlgItemMessage (hDlg, f_text, CB_SETCURSEL, i - 1, 0);
	}
	if (nn <= 0)
	    break;
    }
}

static void addfloppytype (HWND hDlg, int n)
{
    int state, chk;
    HKEY fkey;
    int nn = workprefs.dfxtype[n] + 1;

    int f_text = floppybuttons[n][0];
    int f_drive = floppybuttons[n][1];
    int f_eject = floppybuttons[n][2];
    int f_type = floppybuttons[n][3];
    int f_wp = floppybuttons[n][4];
    int f_si = floppybuttons[n][5];

    if (currentpage == QUICKSTART_ID) {
	f_text = floppybuttonsq[n][0];
	f_drive = floppybuttonsq[n][1];
	f_type = -1;
	f_eject = floppybuttonsq[n][2];
	f_wp = floppybuttonsq[n][4];
	f_si = -1;
    }

    if (nn <= 0)
	state = FALSE;
    else
	state = TRUE;
    if (f_type >= 0)
	SendDlgItemMessage (hDlg, f_type, CB_SETCURSEL, nn, 0);
    if (f_si >= 0)
	ShowWindow (GetDlgItem(hDlg, f_si), zfile_exists (DISK_get_saveimagepath (workprefs.df[n])) ? SW_SHOW : SW_HIDE);

    if (f_text >= 0)
	EnableWindow(GetDlgItem(hDlg, f_text), state);
    if (f_eject >= 0)
	EnableWindow(GetDlgItem(hDlg, f_eject), TRUE);
    if (f_drive >= 0)
	EnableWindow(GetDlgItem(hDlg, f_drive), state);
    chk = disk_getwriteprotect (workprefs.df[n]) && state == TRUE ? BST_CHECKED : 0;
    if (f_wp >= 0)
	CheckDlgButton(hDlg, f_wp, chk);
    chk = state && DISK_validate_filename (workprefs.df[n], 0, NULL, NULL) ? TRUE : FALSE;
    if (f_wp >= 0)
	EnableWindow(GetDlgItem(hDlg, f_wp), chk);
 
    fkey = read_disk_history ();

    addfloppyhistory (hDlg, fkey, n, f_text);

    if (fkey)
	RegCloseKey (fkey);
}

static void getfloppytype (HWND hDlg, int n)
{
    int f_type = floppybuttons[n][3];
    LRESULT val = SendDlgItemMessage (hDlg, f_type, CB_GETCURSEL, 0, 0L);
    
    if (val != CB_ERR && workprefs.dfxtype[n] != val - 1) {
	workprefs.dfxtype[n] = (int)val - 1;
	addfloppytype (hDlg, n);
    }
}

static int getfloppybox (HWND hDlg, int f_text, char *out, int maxlen)
{
    LRESULT val;

    out[0] = 0;
    val = SendDlgItemMessage (hDlg, f_text, CB_GETCURSEL, 0, 0L);
    if (val == CB_ERR) {
	char *p1, *p2;
        char *tmp;
	SendDlgItemMessage (hDlg, f_text, WM_GETTEXT, (WPARAM)maxlen, (LPARAM)out);
	tmp = xmalloc (maxlen + 1);
        strcpy (tmp, out);
	p1 = strstr(tmp, " { ");
	p2 = strstr(tmp, " }");
	if (p1 && p2 && p2 > p1) {
	    *p1 = 0;
	    memset (out, 0, maxlen);
	    memcpy (out, p1 + 3, p2 - p1 - 3);
	    strcat (out, tmp);
	}
	xfree (tmp);
    } else {
	char *p = DISK_history_get (val);
#if 0
	val = SendDlgItemMessage (hDlg, f_text, CB_GETLBTEXT, (WPARAM)val, (LPARAM)out);
	if (val != CB_ERR && val > 0) {
#endif
	if (p) {
	    strcpy (out, p);
	    if (out[0]) {
		/* add to top of list */
		DISK_history_add (out, -1);
	    }
	} else {
	    out[0] = 0;
	}
    }
    return out[0] ? 1 : 0;
}

static void getfloppyname (HWND hDlg, int n)
{
    int f_text = currentpage == QUICKSTART_ID ? floppybuttonsq[n][0] : floppybuttons[n][0];
    char tmp[MAX_DPATH];

    if (getfloppybox (hDlg, f_text, tmp, sizeof (tmp))) {
	disk_insert (n, tmp);
	strcpy (workprefs.df[n], tmp);
    }
}

static void addallfloppies (HWND hDlg)
{
    int i;
    
    for (i = 0; i < 4; i++)
	addfloppytype (hDlg, i);
}

static void floppysetwriteprotect (HWND hDlg, int n, int protect)
{
    disk_setwriteprotect (n, workprefs.df[n], protect);
    addfloppytype (hDlg, n);
}

static void deletesaveimage (HWND hDlg, int num)
{
    char *p = DISK_get_saveimagepath (workprefs.df[num]);
    if (zfile_exists (p)) {
	DeleteFile (p);
	DISK_reinsert (num);
	addfloppytype (hDlg, num);
    }
}

static void diskselect (HWND hDlg, WPARAM wParam, struct uae_prefs *p, int drv)
{
    MultiDiskSelection (hDlg, wParam, 0, &workprefs, NULL);
    disk_insert (0, p->df[0]);
    disk_insert (1, p->df[1]);
    disk_insert (2, p->df[2]);
    disk_insert (3, p->df[3]);
    addfloppytype (hDlg, drv);
}

static INT_PTR CALLBACK FloppyDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int recursive = 0;
    int i;
    static char diskname[40] = { "empty" };

    switch (msg) 
    {
    case WM_INITDIALOG:
    {
	char ft35dd[20], ft35hd[20], ft525sd[20], ftdis[20], ft35ddescom[20];
	WIN32GUI_LoadUIString(IDS_FLOPPYTYPE35DD, ft35dd, sizeof ft35dd);
	WIN32GUI_LoadUIString(IDS_FLOPPYTYPE35HD, ft35hd, sizeof ft35hd);
	WIN32GUI_LoadUIString(IDS_FLOPPYTYPE525SD, ft525sd, sizeof ft525sd);
	WIN32GUI_LoadUIString(IDS_FLOPPYTYPE35DDESCOM, ft35ddescom, sizeof ft35ddescom);
	WIN32GUI_LoadUIString(IDS_FLOPPYTYPEDISABLED, ftdis, sizeof ftdis);
	pages[FLOPPY_ID] = hDlg;
	if (workprefs.floppy_speed > 0 && workprefs.floppy_speed < 10)
	    workprefs.floppy_speed = 100;
	currentpage = FLOPPY_ID;
	SendDlgItemMessage (hDlg, IDC_FLOPPYSPD, TBM_SETRANGE, TRUE, MAKELONG (0, 4));
	SendDlgItemMessage (hDlg, IDC_FLOPPYSPD, TBM_SETPAGESIZE, 0, 1);
	SendDlgItemMessage (hDlg, IDC_FLOPPYTYPE, CB_RESETCONTENT, 0, 0L);
	SendDlgItemMessage (hDlg, IDC_FLOPPYTYPE, CB_ADDSTRING, 0, (LPARAM)ft35dd);
	SendDlgItemMessage (hDlg, IDC_FLOPPYTYPE, CB_ADDSTRING, 0, (LPARAM)ft35hd);
	SendDlgItemMessage (hDlg, IDC_FLOPPYTYPE, CB_ADDSTRING, 0, (LPARAM)ft525sd);
	SendDlgItemMessage (hDlg, IDC_FLOPPYTYPE, CB_ADDSTRING, 0, (LPARAM)ft35ddescom);
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
    }
    case WM_USER:
	recursive++;
	SetDlgItemText (hDlg, IDC_DF0TEXT, workprefs.df[0]);
	SetDlgItemText (hDlg, IDC_DF1TEXT, workprefs.df[1]);
	SetDlgItemText (hDlg, IDC_DF2TEXT, workprefs.df[2]);
	SetDlgItemText (hDlg, IDC_DF3TEXT, workprefs.df[3]);
	SetDlgItemText (hDlg, IDC_DF0TEXTQ, workprefs.df[0]);
	SetDlgItemText (hDlg, IDC_DF1TEXTQ, workprefs.df[1]);
	SetDlgItemText (hDlg, IDC_CREATE_NAME, diskname);
	SendDlgItemMessage (hDlg, IDC_FLOPPYSPD, TBM_SETPOS, TRUE,
	    workprefs.floppy_speed ? exact_log2 ((workprefs.floppy_speed) / 100) + 1 : 0);
	out_floppyspeed (hDlg);
	addallfloppies (hDlg);
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
		break;
		case IDC_DF1TEXT:
		case IDC_DF1TEXTQ:
		getfloppyname (hDlg, 1);
		addfloppytype (hDlg, 1);
		break;
		case IDC_DF2TEXT:
		getfloppyname (hDlg, 2);
		addfloppytype (hDlg, 2);
		break;
		case IDC_DF3TEXT:
		getfloppyname (hDlg, 3);
		addfloppytype (hDlg, 3);
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
	    }
	}
	switch (LOWORD (wParam))
	{
	case IDC_DF0WP:
	case IDC_DF0WPQ:
	    floppysetwriteprotect (hDlg, 0, currentpage == QUICKSTART_ID ? IsDlgButtonChecked (hDlg, IDC_DF0WPQ) : IsDlgButtonChecked (hDlg, IDC_DF0WP));
	    break;
	case IDC_DF1WP:
	case IDC_DF1WPQ:
	    floppysetwriteprotect (hDlg, 1, currentpage == QUICKSTART_ID ? IsDlgButtonChecked (hDlg, IDC_DF1WPQ) : IsDlgButtonChecked (hDlg, IDC_DF1WP));
	    break;
	case IDC_DF2WP:
	    floppysetwriteprotect (hDlg, 2, IsDlgButtonChecked (hDlg, IDC_DF2WP));
	    break;
	case IDC_DF3WP:
	    floppysetwriteprotect (hDlg, 3, IsDlgButtonChecked (hDlg, IDC_DF3WP));
	    break;
	case IDC_DF0:
	case IDC_DF0QQ:
	    diskselect (hDlg, wParam, &workprefs, 0);
	    break;
	case IDC_DF1:
	case IDC_DF1QQ:
	    diskselect (hDlg, wParam, &workprefs, 1);
	    break;
	case IDC_DF2:
	    diskselect (hDlg, wParam, &workprefs, 2);
	    break;
	case IDC_DF3:
	    diskselect (hDlg, wParam, &workprefs, 3);
	    break;
	case IDC_EJECT0:
	case IDC_EJECT0Q:
	    disk_eject(0);
	    SetDlgItemText (hDlg, IDC_DF0TEXT, "");
	    SetDlgItemText (hDlg, IDC_DF0TEXTQ, "");
	    workprefs.df[0][0] = 0;
	    addfloppytype (hDlg, 0);
	    break;
	case IDC_EJECT1:
	case IDC_EJECT1Q:
	    disk_eject(1);
	    SetDlgItemText (hDlg, IDC_DF1TEXT, "");
	    SetDlgItemText (hDlg, IDC_DF1TEXTQ, "");
	    workprefs.df[1][0] = 0;
	    addfloppytype (hDlg, 1);
	    break;
	case IDC_EJECT2:
	    disk_eject(2);
	    SetDlgItemText (hDlg, IDC_DF2TEXT, "");
	    workprefs.df[2][0] = 0;
	    addfloppytype (hDlg, 2);
	    break;
	case IDC_EJECT3:
	    disk_eject(3);
	    SetDlgItemText (hDlg, IDC_DF3TEXT, "");
	    workprefs.df[3][0] = 0;
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
	    DiskSelection(hDlg, wParam, 1, &workprefs, 0);
	    break;
	case IDC_CREATE_RAW:
	    DiskSelection(hDlg, wParam, 1, &workprefs, 0);
	    break;
	}
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

    strcpy (changed_prefs.df[0], workprefs.df[0]);
    strcpy (changed_prefs.df[1], workprefs.df[1]);
    strcpy (changed_prefs.df[2], workprefs.df[2]);
    strcpy (changed_prefs.df[3], workprefs.df[3]);
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

static void addswapperfile (HWND hDlg, int entry)
{
    char path[MAX_DPATH];
    int lastentry = entry;

    if (MultiDiskSelection (hDlg, -1, 0, &changed_prefs, path)) {
	char dpath[MAX_DPATH];
	loopmulti (path, NULL);
	while (loopmulti(path, dpath) && entry < MAX_SPARE_DRIVES) {
	    strcpy (workprefs.dfxlist[entry], dpath);
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
    char tmp[MAX_DPATH];
    HKEY fkey;

    switch (msg) 
    {
    case WM_INITDIALOG:
	pages[DISK_ID] = hDlg;
	currentpage = DISK_ID;
	InitializeListView(hDlg);
	fkey = read_disk_history ();
	addfloppyhistory (hDlg, fkey, 0, IDC_DISKTEXT);
	if (fkey)
	    RegCloseKey (fkey);
	entry = 0;
	swapperhili (hDlg, entry);
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
		    char tmp[1000];
		    strcpy (tmp, workprefs.dfxlist[item]);
		    strcpy (workprefs.dfxlist[item], workprefs.dfxlist[item2]);
		    strcpy (workprefs.dfxlist[item2], tmp);
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
    case WM_COMMAND:
    {
	switch (LOWORD(wParam))
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
		if (workprefs.dfxtype[drv] >= 0 && entry >= 0) {
		    for (i = 0; i < 4; i++) {
			if (!strcmp (workprefs.df[i], workprefs.dfxlist[entry])) {
			    workprefs.df[i][0] = 0;
			    disk_eject (i);
			}
		    }
		    strcpy (workprefs.df[drv], workprefs.dfxlist[entry]);
		    disk_insert (drv, workprefs.df[drv]);
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
		disk_eject (drv);
		InitializeListView (hDlg);
		swapperhili (hDlg, entry);
	    }
	    break;
	    case 10209:
	    {
		addswapperfile (hDlg, entry);
	    }
	    break;
	
	    case IDC_DISKLISTINSERT:
		if (entry >= 0) {
		    if (getfloppybox (hDlg, IDC_DISKTEXT, tmp, sizeof (tmp))) {
			strcpy (workprefs.dfxlist[entry], tmp);
			InitializeListView (hDlg);
			swapperhili (hDlg, entry);
		    } else {
			addswapperfile (hDlg, entry);
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
		    strcpy (tmp, workprefs.dfxlist[entry - 1]);
		    strcpy (workprefs.dfxlist[entry - 1], workprefs.dfxlist[entry]);
		    strcpy (workprefs.dfxlist[entry], tmp);
		    InitializeListView (hDlg);
		    entry--;
		    swapperhili (hDlg, entry);
		}
		break;
	    case IDC_DOWN:
		if (entry >= 0 && entry < MAX_SPARE_DRIVES - 1) {
		    strcpy (tmp, workprefs.dfxlist[entry + 1]);
		    strcpy (workprefs.dfxlist[entry + 1], workprefs.dfxlist[entry]);
		    strcpy (workprefs.dfxlist[entry], tmp);
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
				addswapperfile (hDlg, entry);
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
#define MAX_SERIALS 8
static char comports[MAX_SERIALS][8];
static int ghostscript_available;

static int joy0previous, joy1previous;
static BOOL bNoMidiIn = FALSE;

static void enable_for_portsdlg( HWND hDlg )
{
    int v;

    v = workprefs.input_selected_setting > 0 ? FALSE : TRUE;
    EnableWindow (GetDlgItem (hDlg, IDC_SWAP), v);
#if !defined (SERIAL_PORT)
    EnableWindow( GetDlgItem( hDlg, IDC_MIDIOUTLIST), FALSE );
    EnableWindow( GetDlgItem( hDlg, IDC_MIDIINLIST), FALSE );
    EnableWindow( GetDlgItem( hDlg, IDC_SHARED), FALSE );
    EnableWindow( GetDlgItem( hDlg, IDC_SER_CTSRTS), FALSE );
    EnableWindow( GetDlgItem( hDlg, IDC_SERIAL_DIRECT), FALSE );
    EnableWindow( GetDlgItem( hDlg, IDC_SERIAL), FALSE );
#else
    v = workprefs.use_serial ? TRUE : FALSE;
    EnableWindow( GetDlgItem( hDlg, IDC_SHARED), v);
    EnableWindow( GetDlgItem( hDlg, IDC_SER_CTSRTS), v);
    EnableWindow( GetDlgItem( hDlg, IDC_SERIAL_DIRECT), v);
#endif
#if !defined (PARALLEL_PORT)
    EnableWindow( GetDlgItem( hDlg, IDC_PRINTERLIST), FALSE );
    EnableWindow( GetDlgItem( hDlg, IDC_FLUSHPRINTER), FALSE );
    EnableWindow( GetDlgItem( hDlg, IDC_PSPRINTER), FALSE );
    EnableWindow( GetDlgItem( hDlg, IDC_PS_PARAMS), FALSE );
    EnableWindow( GetDlgItem( hDlg, IDC_PRINTER_AUTOFLUSH), FALSE );
#else
    EnableWindow( GetDlgItem( hDlg, IDC_FLUSHPRINTER), isprinteropen () ? TRUE : FALSE );
    EnableWindow( GetDlgItem( hDlg, IDC_PSPRINTER), full_property_sheet && ghostscript_available ? TRUE : FALSE );
    EnableWindow( GetDlgItem( hDlg, IDC_PSPRINTERDETECT), full_property_sheet ? TRUE : FALSE );
    EnableWindow( GetDlgItem( hDlg, IDC_PS_PARAMS), full_property_sheet && ghostscript_available );
#endif
}

static void updatejoyport (HWND hDlg)
{
    int i, j;
    char tmp[MAX_DPATH], tmp2[MAX_DPATH];
 
    enable_for_portsdlg (hDlg);
    if (joy0previous < 0)
	joy0previous = inputdevice_get_device_total (IDTYPE_JOYSTICK) + 1;
    if (joy1previous < 0)
	joy1previous = JSEM_LASTKBD + 1;
    for (i = 0; i < 2; i++) {
	int total = 2;
	int idx = i == 0 ? joy0previous : joy1previous;
	int id = i == 0 ? IDC_PORT0_JOYS : IDC_PORT1_JOYS;
	int v = i == 0 ? workprefs.jport0 : workprefs.jport1;
	char *p1, *p2;

	SendDlgItemMessage (hDlg, id, CB_RESETCONTENT, 0, 0L);
	SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)"");
	WIN32GUI_LoadUIString (IDS_NONE, tmp, sizeof (tmp) - 3);
	sprintf (tmp2, "<%s>", tmp);
	SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)tmp2);
	WIN32GUI_LoadUIString (IDS_KEYJOY, tmp, sizeof (tmp));
	strcat (tmp, "\n");
	p1 = tmp;
	for (;;) {
	    p2 = strchr (p1, '\n');
	    if (p2 && strlen (p2) > 0) {
		*p2++ = 0;
		SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)p1);
		total++;
		p1 = p2;
	    } else break;
	}
	for (j = 0; j < inputdevice_get_device_total (IDTYPE_JOYSTICK); j++, total++)
	    SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)inputdevice_get_device_name(IDTYPE_JOYSTICK, j));
	for (j = 0; j < inputdevice_get_device_total (IDTYPE_MOUSE); j++, total++)
	    SendDlgItemMessage (hDlg, id, CB_ADDSTRING, 0, (LPARAM)inputdevice_get_device_name(IDTYPE_MOUSE, j));
	if (v < 0) {
	    idx = -1;
	} else if (v >= JSEM_MICE) {
	    idx = v - JSEM_MICE;
	    if (idx >= inputdevice_get_device_total (IDTYPE_MOUSE))
		idx = 0;
	    else
		idx += inputdevice_get_device_total (IDTYPE_JOYSTICK);
	    idx += JSEM_LASTKBD;
	} else if (v >= JSEM_JOYS) {
	    idx = v - JSEM_JOYS;
	    if (idx >= inputdevice_get_device_total (IDTYPE_JOYSTICK))
		idx = 0;
	    idx += JSEM_LASTKBD;
	} else {
	    idx = v - JSEM_KBDLAYOUT;
	}
	idx+=2;
	if (idx >= total)
	    idx = 0;
	SendDlgItemMessage (hDlg, id, CB_SETCURSEL, idx, 0);
    }
}

static void fixjport (int *port, int v)
{
    int vv = *port;
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
    *port = vv;
}	

static void values_from_portsdlg (HWND hDlg)
{
    int i, lastside = 0, changed = 0, v;
    char tmp[256];
    BOOL success;
    LRESULT item;

    for (i = 0; i < 2; i++) {
	int idx = 0;
	int *port = i == 0 ? &workprefs.jport0 : &workprefs.jport1;
	int prevport = *port;
	int id = i == 0 ? IDC_PORT0_JOYS : IDC_PORT1_JOYS;
	LRESULT v = SendDlgItemMessage (hDlg, id, CB_GETCURSEL, 0, 0L);
	if (v != CB_ERR && v > 0) {
	    v-=2;
	    if (v < 0)
		*port = -1;
	    else if (v < JSEM_LASTKBD)
		*port = JSEM_KBDLAYOUT + (int)v;
	    else if (v >= JSEM_LASTKBD + inputdevice_get_device_total (IDTYPE_JOYSTICK))
		*port = JSEM_MICE + (int)v - inputdevice_get_device_total (IDTYPE_JOYSTICK) - JSEM_LASTKBD;
	    else
		*port = JSEM_JOYS + (int)v - JSEM_LASTKBD;
	}
	if (*port != prevport) {
	    lastside = i;
	    changed = 1;
	}
    }
    if (changed) {
	if (lastside)
	    fixjport (&workprefs.jport0, workprefs.jport1);
	else
	    fixjport (&workprefs.jport1, workprefs.jport0);
    }

    item = SendDlgItemMessage (hDlg, IDC_PRINTERLIST, CB_GETCURSEL, 0, 0L);
    if(item != CB_ERR)
    {
	int got = 0;
	strcpy (tmp, workprefs.prtname);
	if (item > 0) {
	    item--;
	    if (item < dwEnumeratedPrinters) {
		strcpy (workprefs.prtname, pInfo[item].pName);
		got = 1;
	    } else {
		int i;
		item -= dwEnumeratedPrinters;
		for (i = 0; i < 4; i++) {
		    if ((paraport_mask & (1 << i)) && item == 0) {
			sprintf (workprefs.prtname, "LPT%d", i + 1);
			got = 1;
			break;
		    }
		    item--;
		}
	    }
	}
	if (!got)
	    strcpy( workprefs.prtname, "none" );
#ifdef PARALLEL_PORT
	if (strcmp (workprefs.prtname, tmp))
	    closeprinter ();
#endif
    }

    workprefs.win32_midioutdev = SendDlgItemMessage(hDlg, IDC_MIDIOUTLIST, CB_GETCURSEL, 0, 0);
    workprefs.win32_midioutdev -= 2;

    if( bNoMidiIn )
    {
	workprefs.win32_midiindev = -1;
    }
    else
    {
	workprefs.win32_midiindev = SendDlgItemMessage(hDlg, IDC_MIDIINLIST, CB_GETCURSEL, 0, 0);
    }
    EnableWindow(GetDlgItem( hDlg, IDC_MIDIINLIST ), workprefs.win32_midioutdev < -1 ? FALSE : TRUE);

    item = SendDlgItemMessage (hDlg, IDC_SERIAL, CB_GETCURSEL, 0, 0L);
    switch( item ) 
    {
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	    workprefs.use_serial = 1;
	    strcpy (workprefs.sername, comports[item - 1]);
	break;

	default:
	    workprefs.use_serial = 0;
	    strcpy(workprefs.sername, "none");
	break;
    }
    workprefs.serial_demand = 0;
    if (IsDlgButtonChecked (hDlg, IDC_SHARED))
	workprefs.serial_demand = 1;
    workprefs.serial_hwctsrts = 0;
    if (IsDlgButtonChecked (hDlg, IDC_SER_CTSRTS))
	workprefs.serial_hwctsrts = 1;
    workprefs.serial_direct = 0;
    if (IsDlgButtonChecked (hDlg, IDC_SERIAL_DIRECT))
	workprefs.serial_direct = 1;
    GetDlgItemText (hDlg, IDC_PS_PARAMS, workprefs.ghostscript_parameters, sizeof workprefs.ghostscript_parameters);
    v  = GetDlgItemInt (hDlg, IDC_PRINTERAUTOFLUSH, &success, FALSE);
    if (success)
	workprefs.parallel_autoflush_time = v;

}

static void values_to_portsdlg (HWND hDlg)
{
    LRESULT result = 0;

    if( strcmp (workprefs.prtname, "none"))
    {
	int i, got = 1;
	char tmp[10];
	result = SendDlgItemMessage( hDlg, IDC_PRINTERLIST, CB_FINDSTRINGEXACT, -1, (LPARAM)workprefs.prtname );
	for (i = 0; i < 4; i++) {
	    sprintf (tmp, "LPT%d", i + 1);
	    if (!strcmp (tmp, workprefs.prtname)) {
		got = 0;
		if (paraport_mask & (1 << i))
		    got = 1;
		break;
	    }
	}
	if( result < 0 || got == 0)
	{
	    // Warn the user that their printer-port selection is not valid on this machine
	    char szMessage[ MAX_DPATH ];
	    WIN32GUI_LoadUIString( IDS_INVALIDPRTPORT, szMessage, MAX_DPATH );
	    pre_gui_message (szMessage);
	    
	    // Disable the invalid parallel-port selection
	    strcpy( workprefs.prtname, "none" );

	    result = 0;
	}
    }
    SetDlgItemInt( hDlg, IDC_PRINTERAUTOFLUSH, workprefs.parallel_autoflush_time, FALSE );
    CheckDlgButton( hDlg, IDC_PSPRINTER, workprefs.parallel_postscript_emulation );
    CheckDlgButton( hDlg, IDC_PSPRINTERDETECT, workprefs.parallel_postscript_detection );
    SetDlgItemText (hDlg, IDC_PS_PARAMS, workprefs.ghostscript_parameters);
    
    SendDlgItemMessage( hDlg, IDC_PRINTERLIST, CB_SETCURSEL, result, 0 );
    SendDlgItemMessage( hDlg, IDC_MIDIOUTLIST, CB_SETCURSEL, workprefs.win32_midioutdev + 2, 0 );
    if (!bNoMidiIn && workprefs.win32_midiindev >= 0)
	SendDlgItemMessage( hDlg, IDC_MIDIINLIST, CB_SETCURSEL, workprefs.win32_midiindev, 0 );
    else
	SendDlgItemMessage( hDlg, IDC_MIDIINLIST, CB_SETCURSEL, 0, 0 );
    EnableWindow( GetDlgItem( hDlg, IDC_MIDIINLIST ), workprefs.win32_midioutdev < -1 ? FALSE : TRUE);
    
    CheckDlgButton( hDlg, IDC_SHARED, workprefs.serial_demand );
    CheckDlgButton( hDlg, IDC_SER_CTSRTS, workprefs.serial_hwctsrts );
    CheckDlgButton( hDlg, IDC_SERIAL_DIRECT, workprefs.serial_direct );
    
    if( strcasecmp( workprefs.sername, "none") == 0 ) 
    {
	SendDlgItemMessage (hDlg, IDC_SERIAL, CB_SETCURSEL, 0, 0L);
	workprefs.use_serial = 0;
    }
    else
    {
	int t = (workprefs.sername[0] == '\0' ? 0 : workprefs.sername[3] - '0');
	int i;
	LRESULT result = -1;
	for (i = 0; i < MAX_SERIALS; i++) {
	    if (!strcmp (comports[i], workprefs.sername)) {
		result = SendDlgItemMessage(hDlg, IDC_SERIAL, CB_SETCURSEL, i + 1, 0L);
		break;
	    }
	}
	if( result < 0 )
	{
	    if (t > 0) {
		// Warn the user that their COM-port selection is not valid on this machine
		char szMessage[ MAX_DPATH ];
		WIN32GUI_LoadUIString( IDS_INVALIDCOMPORT, szMessage, MAX_DPATH );
		pre_gui_message (szMessage);

		// Select "none" as the COM-port
		SendDlgItemMessage( hDlg, IDC_SERIAL, CB_SETCURSEL, 0L, 0L );		
	    }
	    // Disable the chosen serial-port selection
	    strcpy( workprefs.sername, "none" );
	    workprefs.use_serial = 0;
	}
	else
	{
	    workprefs.use_serial = 1;
	}
    }
}

static void init_portsdlg( HWND hDlg )
{
    static int first;
    int port, portcnt, numdevs;
    COMMCONFIG cc;
    DWORD size = sizeof(COMMCONFIG);
    MIDIOUTCAPS midiOutCaps;
    MIDIINCAPS midiInCaps;

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

    joy0previous = joy1previous = -1;
    SendDlgItemMessage (hDlg, IDC_SERIAL, CB_RESETCONTENT, 0, 0L);
    SendDlgItemMessage (hDlg, IDC_SERIAL, CB_ADDSTRING, 0, (LPARAM)szNone );
    portcnt = 0;
    for( port = 0; port < MAX_SERIALS; port++ )
    {
	sprintf( comports[portcnt], "COM%d", port );
	if( GetDefaultCommConfig( comports[portcnt], &cc, &size ) )
	{
	    SendDlgItemMessage( hDlg, IDC_SERIAL, CB_ADDSTRING, 0, (LPARAM)comports[portcnt++] );
	}
    }

    SendDlgItemMessage (hDlg, IDC_PRINTERLIST, CB_RESETCONTENT, 0, 0L);
    SendDlgItemMessage (hDlg, IDC_PRINTERLIST, CB_ADDSTRING, 0, (LPARAM)szNone );
    if( !pInfo ) {
	int flags = PRINTER_ENUM_LOCAL | (os_winnt ? PRINTER_ENUM_CONNECTIONS : 0);
	DWORD needed = 0;
	EnumPrinters( flags, NULL, 1, (LPBYTE)pInfo, 0, &needed, &dwEnumeratedPrinters );
	if (needed > 0) {
	    DWORD size = needed;
	    pInfo = calloc(1, size);
	    dwEnumeratedPrinters = 0;
	    EnumPrinters( flags, NULL, 1, (LPBYTE)pInfo, size, &needed, &dwEnumeratedPrinters );
	}
	if (dwEnumeratedPrinters == 0) {
	    free (pInfo);
	    pInfo = 0;
	}
    }
    if (pInfo) {
	for( port = 0; port < (int)dwEnumeratedPrinters; port++ )
	    SendDlgItemMessage( hDlg, IDC_PRINTERLIST, CB_ADDSTRING, 0, (LPARAM)pInfo[port].pName );
    } else {
	EnableWindow( GetDlgItem( hDlg, IDC_PRINTERLIST ), FALSE );
    }
    if (paraport_mask) {
	int mask = paraport_mask;
	int i = 1;
	while (mask) {
	    if (mask & 1) {
		char tmp[30];
		sprintf (tmp, "LPT%d", i);
		SendDlgItemMessage (hDlg, IDC_PRINTERLIST, CB_ADDSTRING, 0, (LPARAM)tmp);
	    }
	    i++;
	    mask >>= 1;
	}
    }

    SendDlgItemMessage( hDlg, IDC_MIDIOUTLIST, CB_RESETCONTENT, 0, 0L );
    SendDlgItemMessage (hDlg, IDC_MIDIOUTLIST, CB_ADDSTRING, 0, (LPARAM)szNone );
    if( ( numdevs = midiOutGetNumDevs() ) == 0 )
    {
	EnableWindow( GetDlgItem( hDlg, IDC_MIDIOUTLIST ), FALSE );
    }
    else
    {
	char szMidiOut[ MAX_DPATH ];
	WIN32GUI_LoadUIString( IDS_DEFAULTMIDIOUT, szMidiOut, MAX_DPATH );
	SendDlgItemMessage( hDlg, IDC_MIDIOUTLIST, CB_ADDSTRING, 0, (LPARAM)szMidiOut );

	for( port = 0; port < numdevs; port++ )
	{
	    if( midiOutGetDevCaps( port, &midiOutCaps, sizeof( midiOutCaps ) ) == MMSYSERR_NOERROR )
	    {
		SendDlgItemMessage( hDlg, IDC_MIDIOUTLIST, CB_ADDSTRING, 0, (LPARAM)midiOutCaps.szPname );
	    }
	}
	EnableWindow( GetDlgItem( hDlg, IDC_MIDIOUTLIST ), TRUE );
    }

    SendDlgItemMessage( hDlg, IDC_MIDIINLIST, CB_RESETCONTENT, 0, 0L );
    if( ( numdevs = midiInGetNumDevs() ) == 0 )
    {
	EnableWindow( GetDlgItem( hDlg, IDC_MIDIINLIST ), FALSE );
	bNoMidiIn = TRUE;
    }
    else
    {
	for( port = 0; port < numdevs; port++ )
	{
	    if( midiInGetDevCaps( port, &midiInCaps, sizeof( midiInCaps ) ) == MMSYSERR_NOERROR )
	    {
		SendDlgItemMessage( hDlg, IDC_MIDIINLIST, CB_ADDSTRING, 0, (LPARAM)midiInCaps.szPname );
	    }
	}
    }
}

/* Handle messages for the Joystick Settings page of our property-sheet */
static INT_PTR CALLBACK PortsDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int recursive = 0;
    int temp;

    switch (msg) 
    {
    case WM_INITDIALOG:
	recursive++;
	pages[PORTS_ID] = hDlg;
	currentpage = PORTS_ID;
	init_portsdlg(hDlg);
	enable_for_portsdlg(hDlg);
	values_to_portsdlg(hDlg);
	updatejoyport(hDlg);
	recursive--;
	break;	    
    case WM_USER:
	recursive++;
	enable_for_portsdlg (hDlg);
	updatejoyport (hDlg);
	recursive--;
	return TRUE;

    case WM_COMMAND:
	if (recursive > 0)
	    break;
	recursive++;
	if (wParam == IDC_SWAP) {
	    temp = workprefs.jport0;
	    workprefs.jport0 = workprefs.jport1;
	    workprefs.jport1 = temp;
	    temp = joy0previous;
	    joy0previous = joy1previous;
	    joy1previous = temp;
	    updatejoyport (hDlg);
	} else if (wParam == IDC_FLUSHPRINTER) {
	    if (isprinter ()) {
		closeprinter ();
	    }
	} else if (wParam == IDC_PSPRINTER) {
	    workprefs.parallel_postscript_emulation = IsDlgButtonChecked (hDlg, IDC_PSPRINTER) ? 1 : 0;
	    if (workprefs.parallel_postscript_emulation)
		CheckDlgButton (hDlg, IDC_PSPRINTERDETECT, 1);
	} else if (wParam == IDC_PSPRINTERDETECT) {
	    workprefs.parallel_postscript_detection = IsDlgButtonChecked (hDlg, IDC_PSPRINTERDETECT) ? 1 : 0;
	    if (!workprefs.parallel_postscript_detection)
		CheckDlgButton (hDlg, IDC_PSPRINTER, 0);
	} else   {
	    values_from_portsdlg (hDlg);
	    if (HIWORD (wParam) == CBN_SELCHANGE || HIWORD (wParam) == CBN_KILLFOCUS)
		updatejoyport (hDlg);
	}
	inputdevice_updateconfig (&workprefs);
	inputdevice_config_change ();
	recursive--;
	break;
    }
    return FALSE;
}

static char *eventnames[INPUTEVENT_END];

static void values_to_inputdlg (HWND hDlg)
{
    SendDlgItemMessage(hDlg, IDC_INPUTTYPE, CB_SETCURSEL, workprefs.input_selected_setting, 0);
    SendDlgItemMessage(hDlg, IDC_INPUTDEVICE, CB_SETCURSEL, input_selected_device, 0);
    SetDlgItemInt(hDlg, IDC_INPUTDEADZONE, workprefs.input_joystick_deadzone, FALSE);
    SetDlgItemInt(hDlg, IDC_INPUTAUTOFIRERATE, workprefs.input_autofire_framecnt, FALSE);
    SetDlgItemInt(hDlg, IDC_INPUTSPEEDD, workprefs.input_joymouse_speed, FALSE);
    SetDlgItemInt(hDlg, IDC_INPUTSPEEDA, workprefs.input_joymouse_multiplier, FALSE);
    SetDlgItemInt(hDlg, IDC_INPUTSPEEDM, workprefs.input_mouse_speed, FALSE);
    CheckDlgButton (hDlg, IDC_INPUTDEVICEDISABLE, (!input_total_devices || inputdevice_get_device_status (input_selected_device)) ? BST_CHECKED : BST_UNCHECKED);
}

static void init_inputdlg_2( HWND hDlg )
{
    char name1[256], name2[256];
    char custom1[MAX_DPATH];
    int cnt, index, af, aftmp;

    if (input_selected_widget < 0) {
	EnableWindow( GetDlgItem( hDlg, IDC_INPUTAMIGA), FALSE );
    } else {
	EnableWindow( GetDlgItem( hDlg, IDC_INPUTAMIGA), TRUE );
    }
    SendDlgItemMessage (hDlg, IDC_INPUTAMIGA, CB_RESETCONTENT, 0, 0L);
    SendDlgItemMessage (hDlg, IDC_INPUTAMIGA, CB_ADDSTRING, 0, (LPARAM)szNone);
    index = -1; af = 0;
    if (input_selected_widget >= 0) {
	inputdevice_get_mapped_name (input_selected_device, input_selected_widget, 0, name1, custom1, input_selected_sub_num);
	cnt = 1;
	while(inputdevice_iterate (input_selected_device, input_selected_widget, name2, &aftmp)) {
	    free (eventnames[cnt]);
	    eventnames[cnt] = strdup (name2);
	    if (name1 && !strcmp (name1, name2)) {
		index = cnt;
		af = aftmp;
	    }
	    cnt++;
	    SendDlgItemMessage (hDlg, IDC_INPUTAMIGA, CB_ADDSTRING, 0, (LPARAM)name2);
	}
	if (index >= 0) {
	    SendDlgItemMessage( hDlg, IDC_INPUTAMIGA, CB_SETCURSEL, index, 0 );
	    SendDlgItemMessage( hDlg, IDC_INPUTAMIGACNT, CB_SETCURSEL, input_selected_sub_num, 0 );
	}
    }
}

static void init_inputdlg( HWND hDlg )
{
    int i;
    DWORD size = sizeof(COMMCONFIG);
    char buf[100], txt[100];

    SendDlgItemMessage (hDlg, IDC_INPUTTYPE, CB_RESETCONTENT, 0, 0L);
    WIN32GUI_LoadUIString (IDS_INPUT_COMPATIBILITY, buf, sizeof (buf));
    SendDlgItemMessage (hDlg, IDC_INPUTTYPE, CB_ADDSTRING, 0, (LPARAM)buf);
    WIN32GUI_LoadUIString (IDS_INPUT_CUSTOM, buf, sizeof (buf));
    for (i = 0; i < 4; i++) {
	sprintf (txt, buf, i + 1);
	SendDlgItemMessage (hDlg, IDC_INPUTTYPE, CB_ADDSTRING, 0, (LPARAM)txt);
    }

    SendDlgItemMessage (hDlg, IDC_INPUTCOPYFROM, CB_RESETCONTENT, 0, 0L);
    WIN32GUI_LoadUIString (IDS_INPUT_COPY_DEFAULT, buf, sizeof (buf));
    SendDlgItemMessage (hDlg, IDC_INPUTCOPYFROM, CB_ADDSTRING, 0, (LPARAM)buf);
    WIN32GUI_LoadUIString (IDS_INPUT_COPY_CUSTOM, buf, sizeof (buf));
    for (i = 0; i < 4; i++) {
	sprintf (txt, buf, i + 1);
	SendDlgItemMessage (hDlg, IDC_INPUTCOPYFROM, CB_ADDSTRING, 0, (LPARAM)txt);
    }

    SendDlgItemMessage (hDlg, IDC_INPUTAMIGACNT, CB_RESETCONTENT, 0, 0L);
    for (i = 0; i < MAX_INPUT_SUB_EVENT; i++) {
	sprintf (buf, "%d", i + 1);
	SendDlgItemMessage (hDlg, IDC_INPUTAMIGACNT, CB_ADDSTRING, 0, (LPARAM)buf);
    }
    SendDlgItemMessage( hDlg, IDC_INPUTAMIGACNT, CB_SETCURSEL, input_selected_sub_num, 0 );

    SendDlgItemMessage (hDlg, IDC_INPUTDEVICE, CB_RESETCONTENT, 0, 0L);
    for (i = 0; i < inputdevice_get_device_total (IDTYPE_JOYSTICK); i++) {
	SendDlgItemMessage (hDlg, IDC_INPUTDEVICE, CB_ADDSTRING, 0, (LPARAM)inputdevice_get_device_name(IDTYPE_JOYSTICK, i));
    }
    for (i = 0; i < inputdevice_get_device_total (IDTYPE_MOUSE); i++) {
	SendDlgItemMessage (hDlg, IDC_INPUTDEVICE, CB_ADDSTRING, 0, (LPARAM)inputdevice_get_device_name(IDTYPE_MOUSE, i));
    }
    for (i = 0; i < inputdevice_get_device_total (IDTYPE_KEYBOARD); i++) {
	SendDlgItemMessage (hDlg, IDC_INPUTDEVICE, CB_ADDSTRING, 0, (LPARAM)inputdevice_get_device_name(IDTYPE_KEYBOARD, i));
    }
    input_total_devices = inputdevice_get_device_total (IDTYPE_JOYSTICK) +
	inputdevice_get_device_total (IDTYPE_MOUSE) +
	inputdevice_get_device_total (IDTYPE_KEYBOARD);
    InitializeListView(hDlg);
    init_inputdlg_2 (hDlg);
    values_to_inputdlg (hDlg);
}

static void enable_for_inputdlg (HWND hDlg)
{
    int v = workprefs.input_selected_setting == 0 ? FALSE : TRUE;
    EnableWindow (GetDlgItem (hDlg, IDC_INPUTLIST), v);
    EnableWindow (GetDlgItem (hDlg, IDC_INPUTAMIGA), v);
    EnableWindow (GetDlgItem (hDlg, IDC_INPUTAMIGACNT), v);
    EnableWindow (GetDlgItem (hDlg, IDC_INPUTDEADZONE), TRUE);
    EnableWindow (GetDlgItem (hDlg, IDC_INPUTAUTOFIRERATE), v);
    EnableWindow (GetDlgItem (hDlg, IDC_INPUTSPEEDA), v);
    EnableWindow (GetDlgItem (hDlg, IDC_INPUTSPEEDD), v);
    EnableWindow (GetDlgItem (hDlg, IDC_INPUTSPEEDM), TRUE);
    EnableWindow (GetDlgItem (hDlg, IDC_INPUTCOPY), v);
    EnableWindow (GetDlgItem (hDlg, IDC_INPUTCOPYFROM), v);
    EnableWindow (GetDlgItem (hDlg, IDC_INPUTSWAP), TRUE);
    EnableWindow (GetDlgItem (hDlg, IDC_INPUTDEVICEDISABLE), workprefs.input_selected_setting == 0 ? FALSE : TRUE);
}

static void clearinputlistview (HWND hDlg)
{
    ListView_DeleteAllItems( GetDlgItem( hDlg, IDC_INPUTLIST ) );
}

static void values_from_inputdlg (HWND hDlg)
{
    int doselect = 0, v;
    BOOL success;
    LRESULT item;

    v  = GetDlgItemInt( hDlg, IDC_INPUTDEADZONE, &success, FALSE );
    if (success) {
	currprefs.input_joystick_deadzone = workprefs.input_joystick_deadzone = v;
	currprefs.input_joystick_deadzone = workprefs.input_joymouse_deadzone = v;
    }
    v  = GetDlgItemInt( hDlg, IDC_INPUTAUTOFIRERATE, &success, FALSE );
    if (success)
	currprefs.input_autofire_framecnt = workprefs.input_autofire_framecnt = v;
    v  = GetDlgItemInt( hDlg, IDC_INPUTSPEEDD, &success, FALSE );
    if (success)
	currprefs.input_joymouse_speed = workprefs.input_joymouse_speed = v;
    v  = GetDlgItemInt( hDlg, IDC_INPUTSPEEDA, &success, FALSE );
    if (success)
	currprefs.input_joymouse_multiplier = workprefs.input_joymouse_multiplier = v;
    v  = GetDlgItemInt( hDlg, IDC_INPUTSPEEDM, &success, FALSE );
    if (success)
	currprefs.input_mouse_speed = workprefs.input_mouse_speed = v;

    item = SendDlgItemMessage( hDlg, IDC_INPUTAMIGACNT, CB_GETCURSEL, 0, 0L );
    if (item != CB_ERR && input_selected_sub_num != item) {
	input_selected_sub_num = (int)item;
	doselect = 0;
	init_inputdlg_2 (hDlg);
	update_listview_input (hDlg);
	return;
    }

    item = SendDlgItemMessage( hDlg, IDC_INPUTTYPE, CB_GETCURSEL, 0, 0L );
    if(item != CB_ERR) {
	if (item != workprefs.input_selected_setting) {
	    workprefs.input_selected_setting = (int)item;
	    input_selected_widget = -1;
	    inputdevice_updateconfig (&workprefs);
	    enable_for_inputdlg( hDlg );
	    InitializeListView (hDlg);
	    doselect = 1;
	}
    }
    item = SendDlgItemMessage( hDlg, IDC_INPUTDEVICE, CB_GETCURSEL, 0, 0L );
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
    item = SendDlgItemMessage( hDlg, IDC_INPUTAMIGA, CB_GETCURSEL, 0, 0L );
    if(item != CB_ERR) {
	input_selected_event = (int)item;
	doselect = 1;
    }

    if (doselect && input_selected_device >= 0 && input_selected_event >= 0) {
	int flags;
	inputdevice_get_mapped_name (input_selected_device, input_selected_widget,
	    &flags, 0, NULL, input_selected_sub_num);
	inputdevice_set_mapping (input_selected_device, input_selected_widget,
	    eventnames[input_selected_event], NULL, (flags & IDEV_MAPPED_AUTOFIRE_SET) ? 1 : 0,
	    input_selected_sub_num);
	update_listview_input (hDlg);
	inputdevice_updateconfig (&workprefs);
    }
}

static void input_swap (HWND hDlg)
{
    inputdevice_swap_ports (&workprefs, input_selected_device);
    init_inputdlg (hDlg);
}

static void input_copy (HWND hDlg)
{
    int dst = workprefs.input_selected_setting;
    LRESULT src = SendDlgItemMessage( hDlg, IDC_INPUTCOPYFROM, CB_GETCURSEL, 0, 0L );
    if (src == CB_ERR)
	return;
    inputdevice_copy_single_config (&workprefs, (int)src, workprefs.input_selected_setting, input_selected_device);
    init_inputdlg (hDlg);
}

static void input_toggleautofire (void)
{
    int af, flags, event;
    char name[256];
    char custom[MAX_DPATH];
    if (input_selected_device < 0 || input_selected_widget < 0)
	return;
    event = inputdevice_get_mapped_name (input_selected_device, input_selected_widget,
	&flags, name, custom, input_selected_sub_num);
    if (event <= 0)
	return;
    af = (flags & IDEV_MAPPED_AUTOFIRE_SET) ? 0 : 1;
    inputdevice_set_mapping (input_selected_device, input_selected_widget,
	name, custom, af, input_selected_sub_num);
}

static INT_PTR CALLBACK InputDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char name_buf[MAX_DPATH] = "", desc_buf[128] = "";
    char *posn = NULL;
    HWND list;
    int dblclick = 0;
    NM_LISTVIEW *nmlistview;
    int items = 0, entry = 0;
    static int recursive;

    switch (msg) 
    {
    case WM_INITDIALOG:
	pages[INPUT_ID] = hDlg;
	currentpage = INPUT_ID;
	inputdevice_updateconfig (&workprefs);
	inputdevice_config_change ();
	input_selected_widget = -1;
	init_inputdlg(hDlg);
	    
    case WM_USER:
	recursive++;
	enable_for_inputdlg (hDlg);
	values_to_inputdlg (hDlg);
	recursive--;
	return TRUE;
    case WM_COMMAND:
	if (recursive)
	    break;
	recursive++;
	switch (wParam)
	{
	    case IDC_INPUTCOPY:
	    input_copy (hDlg);
	    break;
	    case IDC_INPUTSWAP:
	    input_swap (hDlg);
	    break;
	    case IDC_INPUTDEVICEDISABLE:
	    inputdevice_set_device_status (input_selected_device, IsDlgButtonChecked( hDlg, IDC_INPUTDEVICEDISABLE) ? 1 : 0);
	    break;
	    default:
	    values_from_inputdlg (hDlg);
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
		    if (row == 3) {
			input_selected_sub_num++;
			if (input_selected_sub_num >= MAX_INPUT_SUB_EVENT)
			    input_selected_sub_num = 0;
			SendDlgItemMessage( hDlg, IDC_INPUTAMIGACNT, CB_SETCURSEL, input_selected_sub_num, 0);
		    }
		} else {
		    input_selected_widget = -1;
		}
		update_listview_input (hDlg);
		init_inputdlg_2 (hDlg);
	    }
	}
    }
    return FALSE;
}

#ifdef GFXFILTER

static int scanlineratios[] = { 1,1,1,2,1,3, 2,1,2,2,2,3, 3,1,3,2,3,3, 0,0 };
static int scanlineindexes[100];
static int filterpreset = 0;

static void enable_for_hw3ddlg (HWND hDlg)
{
    int v = workprefs.gfx_filter ? TRUE : FALSE;
    int vv = FALSE, vv2 = FALSE;
    struct uae_filter *uf;
    int i;

    uf = &uaefilters[0];
    i = 0;
    while (uaefilters[i].name) {
	if (workprefs.gfx_filter == uaefilters[i].type) {
	    uf = &uaefilters[i];
	    break;
	}
	i++;
    }
    if (v && (uf->x[0] || uf->x[1] || uf->x[2] || uf->x[3] || uf->x[4]))
	vv = TRUE;
    if (v && uf->x[0])
	vv2 = TRUE;
    EnableWindow (GetDlgItem (hDlg, IDC_FILTERENABLE), TRUE);
    EnableWindow (GetDlgItem (hDlg, IDC_FILTERMODE), v);
    CheckDlgButton( hDlg, IDC_FILTERENABLE, v );
    EnableWindow (GetDlgItem (hDlg, IDC_FILTERHZ), v);
    EnableWindow (GetDlgItem (hDlg, IDC_FILTERVZ), v);
    EnableWindow (GetDlgItem (hDlg, IDC_FILTERHZMULT), vv && !vv2);
    EnableWindow (GetDlgItem (hDlg, IDC_FILTERVZMULT), vv && !vv2);
    EnableWindow (GetDlgItem (hDlg, IDC_FILTERHO), v);
    EnableWindow (GetDlgItem (hDlg, IDC_FILTERVO), v);
    EnableWindow (GetDlgItem (hDlg, IDC_FILTERSLR), vv2);
    EnableWindow (GetDlgItem (hDlg, IDC_FILTERSL), vv2);
    EnableWindow (GetDlgItem (hDlg, IDC_FILTERSL2), vv2);
    EnableWindow (GetDlgItem (hDlg, IDC_FILTERDEFAULT), v);
    EnableWindow (GetDlgItem (hDlg, IDC_FILTERFILTER), vv);
    EnableWindow (GetDlgItem (hDlg, IDC_FILTERAUTORES), vv && !vv2);

    EnableWindow (GetDlgItem (hDlg, IDC_FILTERPRESETLOAD), filterpreset > 0);
    EnableWindow (GetDlgItem (hDlg, IDC_FILTERPRESETDELETE), filterpreset > 0);
}

static void makefilter(char *s, int x, int flags)
{
    sprintf (s, "%dx", x);
    if ((flags & (UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_32_32)) == (UAE_FILTER_MODE_16_16 | UAE_FILTER_MODE_32_32)) {
	strcat (s, " (16bit/32bit)");
	return;
    }
    if (flags & UAE_FILTER_MODE_16)
	strcat (s, " (16bit)");
    if (flags & UAE_FILTER_MODE_32)
	strcat (s, " (32bit)");
}

static char *filtermultnames[] = { "1x", "2x", "4x", "6x", "8x", NULL };
static int filtermults[] = { 1000, 500, 250, 167, 125 };
static void values_to_hw3ddlg (HWND hDlg)
{
    char txt[100], tmp[100];
    int i, j, nofilter, fltnum, modenum;
    struct uae_filter *uf;
    HKEY fkey;

    SendDlgItemMessage(hDlg, IDC_FILTERHZ, TBM_SETRANGE, TRUE, MAKELONG (-999, +999));
    SendDlgItemMessage(hDlg, IDC_FILTERHZ, TBM_SETPAGESIZE, 0, 10);
    SendDlgItemMessage(hDlg, IDC_FILTERVZ, TBM_SETRANGE, TRUE, MAKELONG (-999, +999));
    SendDlgItemMessage(hDlg, IDC_FILTERVZ, TBM_SETPAGESIZE, 0, 10);
    SendDlgItemMessage(hDlg, IDC_FILTERHO, TBM_SETRANGE, TRUE, MAKELONG (-999, +999));
    SendDlgItemMessage(hDlg, IDC_FILTERHO, TBM_SETPAGESIZE, 0, 10);
    SendDlgItemMessage(hDlg, IDC_FILTERVO, TBM_SETRANGE, TRUE, MAKELONG (-999, +999));
    SendDlgItemMessage(hDlg, IDC_FILTERVO, TBM_SETPAGESIZE, 0, 10);
    SendDlgItemMessage(hDlg, IDC_FILTERSL, TBM_SETRANGE, TRUE, MAKELONG (   0, +1000));
    SendDlgItemMessage(hDlg, IDC_FILTERSL, TBM_SETPAGESIZE, 0, 10);
    SendDlgItemMessage(hDlg, IDC_FILTERSL2, TBM_SETRANGE, TRUE, MAKELONG (   0, +1000));
    SendDlgItemMessage(hDlg, IDC_FILTERSL2, TBM_SETPAGESIZE, 0, 10);

    SendDlgItemMessage (hDlg, IDC_FILTERMODE, CB_RESETCONTENT, 0, 0L);
    uf = &uaefilters[0];
    nofilter = 0; fltnum = 0;
    i = 0; j = 0;
    while (uaefilters[i].name) {
	switch (uaefilters[i].type)
	{
#ifndef D3D
	    case UAE_FILTER_DIRECT3D:
	    nofilter = 1;
	    break;
#endif
#ifndef OPENGL
	    case UAE_FILTER_OPENGL:
	    nofilter = 1;
	    break;
#endif
	    default:
	    nofilter = 0;
	    break;
	}
	if (nofilter == 0) {
	    SendDlgItemMessage (hDlg, IDC_FILTERMODE, CB_ADDSTRING, 0, (LPARAM)uaefilters[i].name);
	    if (uaefilters[i].type == workprefs.gfx_filter) {
		uf = &uaefilters[i];
		fltnum = j;
	    }
	    j++;
	}
	i++;
    }
    SendDlgItemMessage (hDlg, IDC_FILTERMODE, CB_SETCURSEL, fltnum, 0);

    SendDlgItemMessage (hDlg, IDC_FILTERFILTER, CB_RESETCONTENT, 0, 0L);
    if (uf->x[0]) {
	WIN32GUI_LoadUIString (IDS_3D_NO_FILTER, txt, sizeof (txt));
	sprintf (tmp, txt, 16);
	SendDlgItemMessage (hDlg, IDC_FILTERFILTER, CB_ADDSTRING, 0, (LPARAM)tmp);
	WIN32GUI_LoadUIString (IDS_3D_BILINEAR, txt, sizeof (txt));
	sprintf (tmp, txt, 16);
	SendDlgItemMessage (hDlg, IDC_FILTERFILTER, CB_ADDSTRING, 0, (LPARAM)tmp);
	WIN32GUI_LoadUIString (IDS_3D_NO_FILTER, txt, sizeof (txt));
	sprintf (tmp, txt, 32);
	SendDlgItemMessage (hDlg, IDC_FILTERFILTER, CB_ADDSTRING, 0, (LPARAM)tmp);
	WIN32GUI_LoadUIString (IDS_3D_BILINEAR, txt, sizeof (txt));
	sprintf (tmp, txt, 32);
	SendDlgItemMessage (hDlg, IDC_FILTERFILTER, CB_ADDSTRING, 0, (LPARAM)tmp);
	modenum = 4;
    } else {
	modenum = 0;
	for (i = 1; i <= 4; i++) {
	    if (uf->x[i]) {
		makefilter (tmp, i, uf->x[i]);
		SendDlgItemMessage (hDlg, IDC_FILTERFILTER, CB_ADDSTRING, 0, (LPARAM)tmp);
		modenum++;
	    }
	}
    }
    if (workprefs.gfx_filter_filtermode >= modenum)
	workprefs.gfx_filter_filtermode = 0;
    SendDlgItemMessage (hDlg, IDC_FILTERFILTER, CB_SETCURSEL, workprefs.gfx_filter_filtermode, 0);

    SendDlgItemMessage (hDlg, IDC_FILTERHZMULT, CB_RESETCONTENT, 0, 0L);
    SendDlgItemMessage (hDlg, IDC_FILTERVZMULT, CB_RESETCONTENT, 0, 0L);
    for (i = 0; filtermultnames[i]; i++) {
        SendDlgItemMessage (hDlg, IDC_FILTERHZMULT, CB_ADDSTRING, 0, (LPARAM)filtermultnames[i]);
        SendDlgItemMessage (hDlg, IDC_FILTERVZMULT, CB_ADDSTRING, 0, (LPARAM)filtermultnames[i]);
    }
    SendDlgItemMessage (hDlg, IDC_FILTERHZMULT, CB_SETCURSEL, 0, 0);
    SendDlgItemMessage (hDlg, IDC_FILTERVZMULT, CB_SETCURSEL, 0, 0);
    for (i = 0; filtermultnames[i]; i++) {
	if (filtermults[i] == workprefs.gfx_filter_horiz_zoom_mult)
            SendDlgItemMessage (hDlg, IDC_FILTERHZMULT, CB_SETCURSEL, i, 0);
	if (filtermults[i] == workprefs.gfx_filter_vert_zoom_mult)
            SendDlgItemMessage (hDlg, IDC_FILTERVZMULT, CB_SETCURSEL, i, 0);
    }

    SendDlgItemMessage (hDlg, IDC_FILTERSLR, CB_RESETCONTENT, 0, 0L);
    i = j = 0;
    while (scanlineratios[i * 2]) {
	int sl = scanlineratios[i * 2] * 16 + scanlineratios[i * 2 + 1];
	sprintf (txt, "%d:%d", scanlineratios[i * 2], scanlineratios[i * 2 + 1]);
	if (workprefs.gfx_filter_scanlineratio == sl)
	    j = i;
	SendDlgItemMessage (hDlg, IDC_FILTERSLR, CB_ADDSTRING, 0, (LPARAM)txt);
	scanlineindexes[i] = sl;
	i++;
    }
    SendDlgItemMessage (hDlg, IDC_FILTERSLR, CB_SETCURSEL, j, 0);
    
    j = 0;
    SendDlgItemMessage (hDlg, IDC_FILTERPRESETS, CB_RESETCONTENT, 0, 0L);
    SendDlgItemMessage (hDlg, IDC_FILTERPRESETS, CB_ADDSTRING, 0, (LPARAM)"");
    if (hWinUAEKey) {
	RegCreateKeyEx(hWinUAEKey , "FilterPresets", 0, NULL, REG_OPTION_NON_VOLATILE,
	    KEY_READ, NULL, &fkey, NULL);
	if (fkey) {
	    int idx = 0;
	    char tmp[MAX_DPATH], tmp2[MAX_DPATH];
	    DWORD size, size2;

	    for (;;) {
		int err;
		size = sizeof (tmp);
		size2 = sizeof (tmp2);
		err = RegEnumValue(fkey, idx, tmp, &size, NULL, NULL, tmp2, &size2);
		if (err != ERROR_SUCCESS)
		    break;
		SendDlgItemMessage (hDlg, IDC_FILTERPRESETS, CB_ADDSTRING, 0, (LPARAM)tmp);
		idx++;
	    }
	    SendDlgItemMessage (hDlg, IDC_FILTERPRESETS, CB_SETCURSEL, filterpreset, 0);
	    RegCloseKey (fkey);
	}
    }
    
    SendDlgItemMessage (hDlg, IDC_FILTERHZ, TBM_SETPOS, TRUE, workprefs.gfx_filter_horiz_zoom);
    SendDlgItemMessage (hDlg, IDC_FILTERVZ, TBM_SETPOS, TRUE, workprefs.gfx_filter_vert_zoom);
    SendDlgItemMessage (hDlg, IDC_FILTERHO, TBM_SETPOS, TRUE, workprefs.gfx_filter_horiz_offset);
    SendDlgItemMessage (hDlg, IDC_FILTERVO, TBM_SETPOS, TRUE, workprefs.gfx_filter_vert_offset);
    SendDlgItemMessage (hDlg, IDC_FILTERSL, TBM_SETPOS, TRUE, workprefs.gfx_filter_scanlines);
    SendDlgItemMessage (hDlg, IDC_FILTERSL2, TBM_SETPOS, TRUE, workprefs.gfx_filter_scanlinelevel);
    SetDlgItemInt (hDlg, IDC_FILTERHZV, workprefs.gfx_filter_horiz_zoom, TRUE);
    SetDlgItemInt (hDlg, IDC_FILTERVZV, workprefs.gfx_filter_vert_zoom, TRUE);
    SetDlgItemInt (hDlg, IDC_FILTERHOV, workprefs.gfx_filter_horiz_offset, TRUE);
    SetDlgItemInt (hDlg, IDC_FILTERVOV, workprefs.gfx_filter_vert_offset, TRUE);
    SetDlgItemInt (hDlg, IDC_FILTERSLV, workprefs.gfx_filter_scanlines, TRUE);
    SetDlgItemInt (hDlg, IDC_FILTERSL2V, workprefs.gfx_filter_scanlinelevel, TRUE);
}

static void values_from_hw3ddlg (HWND hDlg)
{
}

static void filter_preset (HWND hDlg, WPARAM wParam)
{
    int ok, err, load;
    char tmp1[MAX_DPATH], tmp2[MAX_DPATH];
    DWORD outsize;
    HKEY fkey;
    struct uae_prefs *p = &workprefs;
    LRESULT item;

    load = 0;
    ok = 0;
    if (!hWinUAEKey)
	return;
    RegCreateKeyEx(hWinUAEKey , "FilterPresets", 0, NULL, REG_OPTION_NON_VOLATILE,
	KEY_READ | KEY_WRITE, NULL, &fkey, NULL);
    if (!fkey)
	return;
    item = SendDlgItemMessage (hDlg, IDC_FILTERPRESETS, CB_GETCURSEL, 0, 0);
    tmp1[0] = 0;
    if (item != CB_ERR) {
	filterpreset = (int)item;
	SendDlgItemMessage (hDlg, IDC_FILTERPRESETS, CB_GETLBTEXT, (WPARAM)item, (LPARAM)tmp1);
    } else {
	SendDlgItemMessage (hDlg, IDC_FILTERPRESETS, WM_GETTEXT, (WPARAM)item, (LPARAM)tmp1);
    }
    outsize = sizeof (tmp2);
    if (tmp1[0] && RegQueryValueEx (fkey, tmp1, NULL, NULL, tmp2, &outsize) == ERROR_SUCCESS)
	ok = 1;
    
    if (wParam == IDC_FILTERPRESETSAVE) {
	sprintf (tmp2, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
	    p->gfx_filter, p->gfx_filter_filtermode,
	    p->gfx_filter_vert_zoom, p->gfx_filter_horiz_zoom,
	    p->gfx_filter_vert_zoom_mult, p->gfx_filter_horiz_zoom_mult,
	    p->gfx_filter_vert_offset, p->gfx_filter_horiz_offset,
	    p->gfx_filter_scanlines, p->gfx_filter_scanlinelevel, p->gfx_filter_scanlineratio,
	    p->gfx_lores, p->gfx_linedbl, p->gfx_correct_aspect,
	    p->gfx_xcenter, p->gfx_ycenter);
	if (ok == 0) {
	    tmp1[0] = 0;
	    SendDlgItemMessage (hDlg, IDC_FILTERPRESETS, WM_GETTEXT, (WPARAM)sizeof (tmp1), (LPARAM)tmp1);
	    if (tmp1[0] == 0)
		goto end;
	}
	RegSetValueEx (fkey, tmp1, 0, REG_SZ, (CONST BYTE *)&tmp2, strlen (tmp2) + 1);
	values_to_hw3ddlg (hDlg);
    }
    if (ok) {
	if (wParam == IDC_FILTERPRESETDELETE) {
	    err = RegDeleteValue (fkey, tmp1);
	    values_to_hw3ddlg (hDlg);
	} else if (wParam == IDC_FILTERPRESETLOAD) {
	    char *s = tmp2;
	    char *t;
	    
	    load = 1;
	    strcat (s, ",");
	    t = strchr (s, ',');
	    *t++ = 0;
	    p->gfx_filter = atol (s);
	    s = t; t = strchr (s, ','); if (!t) goto end; *t++ = 0;
	    p->gfx_filter_filtermode = atol (s);
	    s = t; t = strchr (s, ','); if (!t) goto end; *t++ = 0;
	    p->gfx_filter_vert_zoom = atol (s);
	    s = t; t = strchr (s, ','); if (!t) goto end; *t++ = 0;
	    p->gfx_filter_horiz_zoom = atol (s);
	    s = t; t = strchr (s, ','); if (!t) goto end; *t++ = 0;
	    p->gfx_filter_vert_zoom_mult = atol (s);
	    s = t; t = strchr (s, ','); if (!t) goto end; *t++ = 0;
	    p->gfx_filter_horiz_zoom_mult = atol (s);
	    s = t; t = strchr (s, ','); if (!t) goto end; *t++ = 0;
	    p->gfx_filter_vert_offset = atol (s);
	    s = t; t = strchr (s, ','); if (!t) goto end; *t++ = 0;
	    p->gfx_filter_horiz_offset = atol (s);
	    s = t; t = strchr (s, ','); if (!t) goto end; *t++ = 0;
	    p->gfx_filter_scanlines = atol (s);
	    s = t; t = strchr (s, ','); if (!t) goto end; *t++ = 0;
	    p->gfx_filter_scanlinelevel = atol (s);
	    s = t; t = strchr (s, ','); if (!t) goto end; *t++ = 0;
	    p->gfx_filter_scanlineratio = atol (s);
	    s = t; t = strchr (s, ','); if (!t) goto end; *t++ = 0;
	    p->gfx_lores = atol (s);
	    s = t; t = strchr (s, ','); if (!t) goto end; *t++ = 0;
	    p->gfx_linedbl = atol (s);
	    s = t; t = strchr (s, ','); if (!t) goto end; *t++ = 0;
	    p->gfx_correct_aspect = atol (s);
	    s = t; t = strchr (s, ','); if (!t) goto end; *t++ = 0;
	    p->gfx_xcenter = atol (s);
	    s = t; t = strchr (s, ','); if (!t) goto end; *t++ = 0;
	    p->gfx_ycenter = atol (s);
	}
    }
end:
    RegCloseKey (fkey);
    if (load) {
	values_to_hw3ddlg (hDlg);
	SendMessage (hDlg, WM_HSCROLL, 0, 0);
    }
    enable_for_hw3ddlg (hDlg);
}

static void filter_handle (HWND hDlg)
{
    LRESULT item = SendDlgItemMessage (hDlg, IDC_FILTERMODE, CB_GETCURSEL, 0, 0L);
    if (item != CB_ERR) {
	int of = workprefs.gfx_filter;
	int off = workprefs.gfx_filter_filtermode;
	workprefs.gfx_filter = 0;
	if (IsDlgButtonChecked (hDlg, IDC_FILTERENABLE)) {
	    workprefs.gfx_filter = uaefilters[item].type;
	    item = SendDlgItemMessage (hDlg, IDC_FILTERFILTER, CB_GETCURSEL, 0, 0L);
	    if (item != CB_ERR)
		workprefs.gfx_filter_filtermode = (int)item;
	    if (of != workprefs.gfx_filter || off != workprefs.gfx_filter_filtermode) {
		values_to_hw3ddlg (hDlg);
		hw3d_changed = 1;
	    }
	}
    }
    enable_for_hw3ddlg (hDlg);
    updatedisplayarea ();
}

static int getfiltermult(HWND hDlg, DWORD dlg)
{
    int v = SendDlgItemMessage (hDlg, dlg, CB_GETCURSEL, 0, 0L);
    if (v == CB_ERR)
	return 1000;
    return filtermults[v];
}

static INT_PTR CALLBACK hw3dDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int recursive;
    LRESULT item;

    switch (msg) 
    {
    case WM_INITDIALOG:
	pages[HW3D_ID] = hDlg;
	currentpage = HW3D_ID;
	enable_for_hw3ddlg (hDlg);
	    
    case WM_USER:
	if(recursive > 0)
	    break;
	recursive++;
	enable_for_hw3ddlg( hDlg );
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
	    break;
	    case IDC_FILTERPRESETLOAD:
	    case IDC_FILTERPRESETSAVE:
	    case IDC_FILTERPRESETDELETE:
	    filter_preset (hDlg, wParam);
	    break;
	    case IDC_FILTERENABLE:
	    filter_handle (hDlg);
	    break;
	    case IDC_FILTERAUTORES:
	    workprefs.gfx_autoresolution = IsDlgButtonChecked (hDlg, IDC_FILTERAUTORES);
	    break;
	    default:
	    if (HIWORD (wParam) == CBN_SELCHANGE || HIWORD (wParam) == CBN_KILLFOCUS)  {
		switch (LOWORD (wParam))
		{
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
		    case IDC_FILTERMODE:
		    case IDC_FILTERFILTER:
		    filter_handle (hDlg);
		    break;
		    case IDC_FILTERHZMULT:
		    case IDC_FILTERVZMULT:
		    currprefs.gfx_filter_horiz_zoom_mult = workprefs.gfx_filter_horiz_zoom_mult = getfiltermult(hDlg, IDC_FILTERHZMULT);
		    currprefs.gfx_filter_vert_zoom_mult = workprefs.gfx_filter_vert_zoom_mult = getfiltermult(hDlg, IDC_FILTERVZMULT);
		    updatedisplayarea ();
		    WIN32GFX_WindowMove ();
		    break;
		}
	    }
	    break;
	}
	recursive--;
	break;
    case WM_HSCROLL:
	currprefs.gfx_filter_horiz_zoom = workprefs.gfx_filter_horiz_zoom = (int)SendMessage(GetDlgItem(hDlg, IDC_FILTERHZ), TBM_GETPOS, 0, 0 );
	currprefs.gfx_filter_vert_zoom = workprefs.gfx_filter_vert_zoom = (int)SendMessage(GetDlgItem(hDlg, IDC_FILTERVZ), TBM_GETPOS, 0, 0 );
	currprefs.gfx_filter_horiz_offset = workprefs.gfx_filter_horiz_offset = (int)SendMessage(GetDlgItem(hDlg, IDC_FILTERHO), TBM_GETPOS, 0, 0);
	currprefs.gfx_filter_vert_offset = workprefs.gfx_filter_vert_offset = (int)SendMessage(GetDlgItem(hDlg, IDC_FILTERVO), TBM_GETPOS, 0, 0);
	currprefs.gfx_filter_scanlines = workprefs.gfx_filter_scanlines = (int)SendMessage(GetDlgItem(hDlg, IDC_FILTERSL), TBM_GETPOS, 0, 0);
	currprefs.gfx_filter_scanlinelevel = workprefs.gfx_filter_scanlinelevel = (int)SendMessage(GetDlgItem(hDlg, IDC_FILTERSL2), TBM_GETPOS, 0, 0);
	SetDlgItemInt (hDlg, IDC_FILTERHZV, workprefs.gfx_filter_horiz_zoom, TRUE);
	SetDlgItemInt (hDlg, IDC_FILTERVZV, workprefs.gfx_filter_vert_zoom, TRUE);
	SetDlgItemInt (hDlg, IDC_FILTERHOV, workprefs.gfx_filter_horiz_offset, TRUE);
	SetDlgItemInt (hDlg, IDC_FILTERVOV, workprefs.gfx_filter_vert_offset, TRUE);
	SetDlgItemInt (hDlg, IDC_FILTERSLV, workprefs.gfx_filter_scanlines, TRUE);
	SetDlgItemInt (hDlg, IDC_FILTERSL2V, workprefs.gfx_filter_scanlinelevel, TRUE);
	updatedisplayarea ();
	WIN32GFX_WindowMove ();
	break;
    }
    return FALSE;
}
#endif

#ifdef AVIOUTPUT
static void values_to_avioutputdlg(HWND hDlg)
{
    char tmpstr[256];
	
    updatewinfsmode (&workprefs);
    SetDlgItemText(hDlg, IDC_AVIOUTPUT_FILETEXT, avioutput_filename);
	
    sprintf(tmpstr, "%d fps", avioutput_fps);
    SendMessage(GetDlgItem(hDlg, IDC_AVIOUTPUT_FPS_STATIC), WM_SETTEXT, (WPARAM) 0, (LPARAM) tmpstr);
	
    sprintf(tmpstr, "Actual: %d x %d", workprefs.gfx_size.width, workprefs.gfx_size.height);
    SendMessage(GetDlgItem(hDlg, IDC_AVIOUTPUT_DIMENSIONS_STATIC), WM_SETTEXT, (WPARAM) 0, (LPARAM) tmpstr);
	
    switch(avioutput_fps)
    {
	case VBLANK_HZ_PAL:
	    CheckRadioButton(hDlg, IDC_AVIOUTPUT_PAL, IDC_AVIOUTPUT_NTSC, IDC_AVIOUTPUT_PAL);
	break;
		
	case VBLANK_HZ_NTSC:
	    CheckRadioButton(hDlg, IDC_AVIOUTPUT_PAL, IDC_AVIOUTPUT_NTSC, IDC_AVIOUTPUT_NTSC);
	break;
		
	default:
	    CheckDlgButton(hDlg, IDC_AVIOUTPUT_PAL, BST_UNCHECKED);
	    CheckDlgButton(hDlg, IDC_AVIOUTPUT_NTSC, BST_UNCHECKED);
	break;
    }
	
    CheckDlgButton (hDlg, IDC_AVIOUTPUT_FRAMELIMITER, avioutput_framelimiter ? FALSE : TRUE);
    CheckDlgButton (hDlg, IDC_AVIOUTPUT_ACTIVATED, avioutput_requested ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton (hDlg, IDC_SAMPLERIPPER_ACTIVATED, sampleripper_enabled ? BST_CHECKED : BST_UNCHECKED);
}

static void values_from_avioutputdlg(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	LRESULT tmp;

	updatewinfsmode (&workprefs);
	tmp = SendMessage(GetDlgItem(hDlg, IDC_AVIOUTPUT_FPS), TBM_GETPOS, 0, 0);
	if (tmp < 1)
	    tmp = 1;
	if (tmp != avioutput_fps) {
	    avioutput_fps = (int)tmp;
	    AVIOutput_Restart ();
	}
}

static void enable_for_avioutputdlg(HWND hDlg)
{
    char tmp[1000];
#if defined (PROWIZARD)
    EnableWindow(GetDlgItem(hDlg, IDC_PROWIZARD), TRUE);
    if (full_property_sheet)
        EnableWindow(GetDlgItem(hDlg, IDC_PROWIZARD), FALSE);
#endif

    EnableWindow(GetDlgItem(hDlg, IDC_SCREENSHOT), full_property_sheet ? FALSE : TRUE);

    EnableWindow(GetDlgItem(hDlg, IDC_AVIOUTPUT_PAL), TRUE);
    EnableWindow(GetDlgItem(hDlg, IDC_AVIOUTPUT_NTSC), TRUE);
    EnableWindow(GetDlgItem(hDlg, IDC_AVIOUTPUT_FPS), TRUE);
    EnableWindow(GetDlgItem(hDlg, IDC_AVIOUTPUT_FILE), TRUE);
		
    if(workprefs.produce_sound < 2) {
	EnableWindow(GetDlgItem(hDlg, IDC_AVIOUTPUT_AUDIO), FALSE);
	EnableWindow(GetDlgItem(hDlg, IDC_AVIOUTPUT_AUDIO_STATIC), FALSE);
	avioutput_audio = 0;
    } else {
	EnableWindow(GetDlgItem(hDlg, IDC_AVIOUTPUT_AUDIO), TRUE);
	EnableWindow(GetDlgItem(hDlg, IDC_AVIOUTPUT_AUDIO_STATIC), TRUE);
    }

    if (avioutput_audio == AVIAUDIO_WAV) {
       strcpy (tmp, "Wave (internal)");
    } else {
	avioutput_audio = AVIOutput_GetAudioCodec(tmp, sizeof tmp);
    }
    if(!avioutput_audio) {
	CheckDlgButton(hDlg, IDC_AVIOUTPUT_AUDIO, BST_UNCHECKED);
	WIN32GUI_LoadUIString (IDS_AVIOUTPUT_NOCODEC, tmp, sizeof tmp);
    }
    SetWindowText(GetDlgItem(hDlg, IDC_AVIOUTPUT_AUDIO_STATIC), tmp);
	
    if (avioutput_audio != AVIAUDIO_WAV)
	avioutput_video = AVIOutput_GetVideoCodec(tmp, sizeof tmp);
    if(!avioutput_video) {
	CheckDlgButton(hDlg, IDC_AVIOUTPUT_VIDEO, BST_UNCHECKED);
	WIN32GUI_LoadUIString (IDS_AVIOUTPUT_NOCODEC, tmp, sizeof tmp);
    }
    SetWindowText(GetDlgItem(hDlg, IDC_AVIOUTPUT_VIDEO_STATIC), tmp);

    CheckDlgButton (hDlg, IDC_AVIOUTPUT_FRAMELIMITER, avioutput_framelimiter ? FALSE : TRUE);

    EnableWindow(GetDlgItem(hDlg, IDC_AVIOUTPUT_ACTIVATED), (!avioutput_audio && !avioutput_video) ? FALSE : TRUE);

    EnableWindow(GetDlgItem(hDlg, IDC_INPREC_RECORD), input_recording >= 0);
    CheckDlgButton(hDlg, IDC_INPREC_RECORD, input_recording > 0 ? BST_CHECKED : BST_UNCHECKED);
    EnableWindow(GetDlgItem(hDlg, IDC_INPREC_PLAY), input_recording <= 0);
    CheckDlgButton(hDlg, IDC_INPREC_PLAY, input_recording < 0 ? BST_CHECKED : BST_UNCHECKED);
    EnableWindow(GetDlgItem(hDlg, IDC_INPREC_PLAYMODE), input_recording == 0);
}

static INT_PTR CALLBACK AVIOutputDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int recursive = 0;
    char tmp[1000];
	
    switch(msg)
    {
	case WM_INITDIALOG:
	    pages[AVIOUTPUT_ID] = hDlg;
	    currentpage = AVIOUTPUT_ID;
	    AVIOutput_GetSettings();
	    enable_for_avioutputdlg(hDlg);
	    SendDlgItemMessage(hDlg, IDC_AVIOUTPUT_FPS, TBM_SETRANGE, TRUE, MAKELONG(1, VBLANK_HZ_NTSC));
	    SendDlgItemMessage(hDlg, IDC_AVIOUTPUT_FPS, TBM_SETPOS, TRUE, avioutput_fps);
	    SendMessage(hDlg, WM_HSCROLL, (WPARAM) NULL, (LPARAM) NULL);
	    if (!avioutput_filename[0]) {
	        fetch_path ("VideoPath", avioutput_filename, sizeof (avioutput_filename));
	        strcat (avioutput_filename, "output.avi");
	    }
		
	case WM_USER:
	    recursive++;
	    values_to_avioutputdlg(hDlg);
	    enable_for_avioutputdlg(hDlg);
	    recursive--;
	return TRUE;
		
	case WM_HSCROLL:
	{
	    recursive++;
	    values_from_avioutputdlg(hDlg, msg, wParam, lParam);
	    values_to_avioutputdlg(hDlg);
	    enable_for_avioutputdlg(hDlg);
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
		    avioutput_framelimiter = IsDlgButtonChecked (hDlg, IDC_AVIOUTPUT_FRAMELIMITER) ? 0 : 1;
		    AVIOutput_SetSettings();
		break;

		case IDC_INPREC_PLAYMODE:
		break;
		case IDC_INPREC_RECORD:
		    if (input_recording)
			inprec_close ();
		    else
			DiskSelection(hDlg, wParam, 16, &workprefs, NULL);
		break;
		case IDC_INPREC_PLAY:
		    if (input_recording)
			inprec_close ();
		    else
			DiskSelection(hDlg, wParam, 15, &workprefs, NULL);
		break;

#ifdef PROWIZARD
		case IDC_PROWIZARD:
		    moduleripper ();
		break;
#endif
		case IDC_SAMPLERIPPER_ACTIVATED:
		    sampleripper_enabled = !sampleripper_enabled;
		    audio_sampleripper(-1);
		break;

		case IDC_AVIOUTPUT_ACTIVATED:
		    avioutput_requested = !avioutput_requested;
		    SendMessage(hDlg, WM_HSCROLL, (WPARAM) NULL, (LPARAM) NULL);
		    if (!avioutput_requested)
			AVIOutput_End ();
		break;

		case IDC_SCREENSHOT:
		    screenshot(1, 0);
		break;
			
		case IDC_AVIOUTPUT_PAL:
		    SendDlgItemMessage(hDlg, IDC_AVIOUTPUT_FPS, TBM_SETPOS, TRUE, VBLANK_HZ_PAL);
		    SendMessage(hDlg, WM_HSCROLL, (WPARAM) NULL, (LPARAM) NULL);
		break;
			
		case IDC_AVIOUTPUT_NTSC:
		    SendDlgItemMessage(hDlg, IDC_AVIOUTPUT_FPS, TBM_SETPOS, TRUE, VBLANK_HZ_NTSC);
		    SendMessage(hDlg, WM_HSCROLL, (WPARAM) NULL, (LPARAM) NULL);
		break;
			
		case IDC_AVIOUTPUT_AUDIO:
		{
		    if (avioutput_enabled)
		        AVIOutput_End ();
		    if(IsDlgButtonChecked(hDlg, IDC_AVIOUTPUT_AUDIO) == BST_CHECKED) {
			avioutput_audio = AVIOutput_ChooseAudioCodec(hDlg, tmp, sizeof tmp);
			enable_for_avioutputdlg(hDlg);
		    } else {
			avioutput_audio = 0;
		    }	
		    break;
		}
			
		case IDC_AVIOUTPUT_VIDEO:
		{
		    if (avioutput_enabled)
		        AVIOutput_End ();
		    if(IsDlgButtonChecked(hDlg, IDC_AVIOUTPUT_VIDEO) == BST_CHECKED) {
			avioutput_video = AVIOutput_ChooseVideoCodec(hDlg, tmp, sizeof tmp);
		        if (avioutput_audio = AVIAUDIO_WAV)
			    avioutput_audio = 0;
			enable_for_avioutputdlg(hDlg);
		    } else {
			avioutput_video = 0;
		    }
		    enable_for_avioutputdlg(hDlg);
		    break;
		}
			
		case IDC_AVIOUTPUT_FILE:
		{
		    OPENFILENAME ofn;
				
		    ZeroMemory(&ofn, sizeof(OPENFILENAME));
		    ofn.lStructSize = sizeof(OPENFILENAME);
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
		    ofn.lpstrFilter = "Video Clip (*.avi)\0*.avi\0Wave Sound (*.wav)\0";
				
		    if(!GetSaveFileName(&ofn))
			break;
		    if (ofn.nFilterIndex == 2) {
		        avioutput_audio = AVIAUDIO_WAV;
		        avioutput_video = 0;
		        if (strlen (avioutput_filename) > 4 && !stricmp (avioutput_filename + strlen (avioutput_filename) - 4, ".avi"))
			    strcpy (avioutput_filename + strlen (avioutput_filename) - 4, ".wav");
		    }
		    break;
		}
		
	    }
	
	    values_from_avioutputdlg(hDlg, msg, wParam, lParam);
	    values_to_avioutputdlg(hDlg);
	    enable_for_avioutputdlg(hDlg);
		
	    recursive--;
	
	    return TRUE;
	}
	return FALSE;
}
#endif

struct GUIPAGE {
    PROPSHEETPAGE pp;
    HTREEITEM tv;
    int himg;
    int idx;
    const char *help;
    HACCEL accel;
    int fullpanel;
    int tmpl;
};

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
    HDC hdc, memdc;
    int w, h, i;

    for (i = 0; ToolTipHWNDS2[i].hwnd; i++) {
	if (hwnd == ToolTipHWNDS2[i].hwnd)
	    break;
    }
    if (!ToolTipHWNDS2[i].hwnd)
	return CallWindowProc (ToolTipHWNDS2[i].proc, hwnd, message, wParam, lParam);

    switch (message)
    {
	case WM_PAINT:
	bm = LoadBitmap (hInst, MAKEINTRESOURCE (ToolTipHWNDS2[i].imageid));
	GetObject (bm, sizeof (binfo), &binfo);
	w = binfo.bmWidth;
	h = binfo.bmHeight;
	GetWindowRect (hwnd, &r1);
	GetCursorPos (&p1);
	r1.right = r1.left + w;
	r1.bottom = r1.top + h;
	ShowWindow (hwnd, SW_SHOWNA);
	MoveWindow (hwnd, r1.left, r1.top, r1.right - r1.left, r1.bottom - r1.top, TRUE);
	hdc = GetDC (hwnd);
	memdc = CreateCompatibleDC (hdc);
	SelectObject (memdc, bm);
	BeginPaint (hwnd, &ps);
	SetBkMode (ps.hdc, TRANSPARENT);
	BitBlt (hdc, 0, 0, w, h, memdc, 0, 0, SRCCOPY);
	DeleteObject (bm);
	EndPaint (hwnd, &ps);
	DeleteDC (memdc);
	ReleaseDC (hwnd, hdc);
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
    IDD_PATHS, IDC_PATHS_ROM, IDC_PATHS_CONFIG, IDC_PATHS_SCREENSHOT, IDC_PATHS_SAVESTATE, IDC_PATHS_AVIOUTPUT, IDC_PATHS_SAVEIMAGE,
    -1,
    IDD_PORTS, IDC_PRINTERLIST, IDC_PS_PARAMS, IDC_SERIAL, IDC_MIDIOUTLIST, IDC_MIDIINLIST,
    -1,
    IDD_SOUND, IDC_SOUNDCARDLIST, IDC_SOUNDDRIVESELECT,
    -1,
    0
};

static BOOL CALLBACK childenumproc (HWND hwnd, LPARAM lParam)
{
    int i;
    TOOLINFO ti;
    char tmp[MAX_DPATH];
    char *p;
    LRESULT v;

    if (GetParent(hwnd) != panelDlg)
	return 1;
    i = 0;
    while (ignorewindows[i]) {
	if (ignorewindows[i] == (int)lParam) {
	    int dlgid = GetDlgCtrlID(hwnd);
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
    SendMessage (hwnd, WM_GETTEXT, (WPARAM)sizeof (tmp), (LPARAM)tmp);
    p = strchr (tmp, '[');
    if (strlen (tmp) > 0 && p && strlen(p) > 2 && p[1] == ']') {
	int imageid = 0;
	*p++ = 0;
	*p++ = 0;
	if (p[0] == ' ')
	    *p++;
	if (p[0] == '#')
	    imageid = atol (p + 1);
	tmp[strlen(tmp) - 1] = 0;
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
		SendMessage(ToolTipHWND2, TTM_SETDELAYTIME, (WPARAM)TTDT_AUTOPOP, (LPARAM)MAKELONG(20000, 0));
		SendMessage(ToolTipHWND2, TTM_SETMAXTIPWIDTH, 0, 400);
		SendMessage(hwnd, WM_SETTEXT, 0, (LPARAM)tmp);
		v = SendMessage(ToolTipHWND2, TTM_ADDTOOL, 0, (LPARAM) (LPTOOLINFO) &ti);
	    }
	} else {
	    v = SendMessage(hwnd, WM_SETTEXT, 0, (LPARAM)tmp);
	    v = SendMessage(ToolTipHWND, TTM_ADDTOOL, 0, (LPARAM) (LPTOOLINFO) &ti);
	}
	return 1;
    }
    p = strchr (tmp, ']');
    if (strlen (tmp) > 0 && p && strlen(p) > 2 && p[1] == '[') {
	RECT r;
	*p++ = 0;
	*p++ = 0;
	if (p[0] == ' ')
	    *p++;
	tmp[strlen(tmp) - 1] = 0;
	SendMessage(hwnd, WM_SETTEXT, 0, (LPARAM)tmp);
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
	SendMessage(ToolTipHWND, TTM_ADDTOOL, 0, (LPARAM) (LPTOOLINFO) &ti);
    }
    return 1;
}

static struct GUIPAGE ppage[MAX_C_PAGES];
#define PANEL_X 174
#define PANEL_Y 12
#define PANEL_WIDTH 456
#define PANEL_HEIGHT 396

static HWND updatePanel (HWND hDlg, int id)
{
    static HWND hwndTT;
    RECT r1c, r1w, r2c, r2w, r3c, r3w;
    int w, h, pw, ph, x , y, i;
    int fullpanel;

    EnableWindow (GetDlgItem (guiDlg, IDC_RESETAMIGA), full_property_sheet ? FALSE : TRUE);
    EnableWindow (GetDlgItem (guiDlg, IDOK), TRUE);
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
	if (!isfullscreen ()) {
	    RECT r;
	    LONG left, top;
	    GetWindowRect (hDlg, &r);
	    left = r.left;
	    top = r.top;
	    if (hWinUAEKey) {
		RegSetValueEx (hWinUAEKey, "xPosGUI", 0, REG_DWORD, (LPBYTE)&left, sizeof(LONG));
		RegSetValueEx (hWinUAEKey, "yPosGUI", 0, REG_DWORD, (LPBYTE)&top, sizeof(LONG));
	    }
	}
	EnableWindow (GetDlgItem (hDlg, IDHELP), FALSE);
	return NULL;
    }

    fullpanel = ppage[id].fullpanel;
    GetWindowRect (GetDlgItem (hDlg, IDC_PANEL_FRAME), &r1w);
    GetClientRect (GetDlgItem (hDlg, IDC_PANEL_FRAME), &r1c);
    GetWindowRect (hDlg, &r2w);
    GetClientRect (hDlg, &r2c);
    gui_width = r2c.right;
    gui_height = r2c.bottom;
    panelDlg = CreateDialogParam (hUIDLL ? hUIDLL : hInst, ppage[id].pp.pszTemplate, hDlg, ppage[id].pp.pfnDlgProc, id);
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
    ShowWindow(GetDlgItem(hDlg, IDC_PANEL_FRAME), FALSE);
    ShowWindow(GetDlgItem(hDlg, IDC_PANEL_FRAME_OUTER), !fullpanel);
    ShowWindow(GetDlgItem(hDlg, IDC_PANELTREE), !fullpanel);
    ShowWindow (panelDlg, TRUE);
    EnableWindow (GetDlgItem (hDlg, IDHELP), pHtmlHelp && ppage[currentpage].help ? TRUE : FALSE);

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
    SendMessage(ToolTipHWND, TTM_SETDELAYTIME, (WPARAM)TTDT_AUTOPOP, (LPARAM)MAKELONG(20000, 0));
    SendMessage(ToolTipHWND, TTM_SETMAXTIPWIDTH, 0, 400);

    EnumChildWindows (panelDlg, &childenumproc, (LPARAM)ppage[currentpage].tmpl);
    SendMessage (panelDlg, WM_NULL, 0, 0);

    hAccelTable = ppage[currentpage].accel;

    return panelDlg;
}

static HTREEITEM CreateFolderNode (HWND TVhDlg, int nameid, HTREEITEM parent, int nodeid, int sub)
{
    TVINSERTSTRUCT is;
    char txt[100];

    memset (&is, 0, sizeof (is));
    is.hInsertAfter = TVI_LAST;
    is.hParent = parent;
    is.itemex.mask = TVIF_TEXT | TVIF_PARAM | TVIF_STATE | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    WIN32GUI_LoadUIString (nameid, txt, sizeof (txt));
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
    is.itemex.pszText = (char*)p->pp.pszTitle;
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
	    icon = LoadIcon (hInst, (LPCSTR)ppage[i].pp.pszIcon);
	    ppage[i].himg = ImageList_AddIcon (himl, icon);
	}
	icon = LoadIcon (hInst, MAKEINTRESOURCE (IDI_ROOT));
	ImageList_AddIcon (himl, icon);
    }
    TVhDlg = GetDlgItem(hDlg, IDC_PANELTREE);
    TreeView_SetImageList (TVhDlg, himl, TVSIL_NORMAL);

    p = root = CreateFolderNode (TVhDlg, IDS_TREEVIEW_SETTINGS, NULL, ABOUT_ID, 0);
    CN(ABOUT_ID);
    CN(PATHS_ID);
    CN(QUICKSTART_ID);
    CN(LOADSAVE_ID);
#if FRONTEND == 1
    CN(FRONTEND_ID);
#endif

    p = CreateFolderNode (TVhDlg, IDS_TREEVIEW_HARDWARE, root, LOADSAVE_ID, CONFIG_TYPE_HARDWARE);
    CN(CPU_ID);
    CN(CHIPSET_ID);
    CN(KICKSTART_ID);
    CN(MEMORY_ID);
    CN(FLOPPY_ID);
    CN(HARDDISK_ID);

    p = CreateFolderNode (TVhDlg, IDS_TREEVIEW_HOST, root, LOADSAVE_ID, CONFIG_TYPE_HOST);
    CN(DISPLAY_ID);
    CN(SOUND_ID);
    CN(PORTS_ID);
    CN(INPUT_ID);
    CN(AVIOUTPUT_ID);
    CN(HW3D_ID);
    CN(DISK_ID);
    CN(MISC1_ID);
    CN(MISC2_ID);

    TreeView_SelectItem (TVhDlg, ppage[currentpage].tv);
}

static void centerWindow (HWND hDlg)
{
    RECT rc, rcDlg, rcOwner;
    HWND owner = GetParent(hDlg);
    LONG x = 0, y = 0;
    POINT pt1, pt2;

    if (owner == NULL)
	owner = GetDesktopWindow();
    if (!isfullscreen ()) {
	DWORD regkeytype;
	DWORD regkeysize = sizeof(LONG);
	if (hWinUAEKey) {
	    if (RegQueryValueEx (hWinUAEKey, "xPosGUI", 0, &regkeytype, (LPBYTE)&x, &regkeysize) != ERROR_SUCCESS)
		x = 0;
	    if (RegQueryValueEx (hWinUAEKey, "yPosGUI", 0, &regkeytype, (LPBYTE)&y, &regkeysize) != ERROR_SUCCESS)
		y = 0;
	} else {
	    x = y = 0;
	}
    } else {
	GetWindowRect (owner, &rcOwner);
	GetWindowRect (hDlg, &rcDlg);
	CopyRect (&rc, &rcOwner);
	OffsetRect(&rcDlg, -rcDlg.left, -rcDlg.top); 
	OffsetRect(&rc, -rc.left, -rc.top); 
	OffsetRect(&rc, -rcDlg.right, -rcDlg.bottom);
	x = rcOwner.left + (rc.right / 2);
	y = rcOwner.top + (rc.bottom / 2);
    }
    SetForegroundWindow (hDlg);
    pt1.x = x;
    pt1.y = y;
    pt2.x = x + 16;
    pt2.y = y + GetSystemMetrics (SM_CYMENU) + GetSystemMetrics (SM_CYBORDER);
    if (MonitorFromPoint (pt1, MONITOR_DEFAULTTONULL) == NULL || MonitorFromPoint (pt2, MONITOR_DEFAULTTONULL) == NULL) {
	x = 16;
	y = 16;
    }
    SetWindowPos (hDlg,  HWND_TOP, x, y, 0, 0, SWP_NOSIZE);
}

int dragdrop (HWND hDlg, HDROP hd, struct uae_prefs *prefs, int	currentpage)
{
    int cnt, i, drv, firstdrv, list;
    char file[MAX_DPATH];
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
    drv = 0;
    if (currentpage < 0) {
	GetClientRect(hMainWnd, &r2);
	GetClientRect(hStatusWnd, &r);
	if (pt.y >= r2.bottom && pt.y < r2.bottom + r.bottom) {
	    if (pt.x >= window_led_drives && pt.x < window_led_drives_end && window_led_drives > 0) {
		drv = pt.x - window_led_drives;
		drv /= (window_led_drives_end - window_led_drives) / 4;
		if (drv < 0 || drv > 3)
		    drv = 0;
	    }
	}
    } else if (currentpage == FLOPPY_ID || currentpage == QUICKSTART_ID) {
	for (i = 0; i < 4; i++) {
	    int id = dfxtext[i * 2 + (currentpage == QUICKSTART_ID ? 1 : 0)];
	    if (workprefs.dfxtype[i] >= 0 && id >= 0) {
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
	struct zfile *z;
	DragQueryFile (hd, i, file, sizeof (file));
        flags = GetFileAttributes(file);
	z = zfile_fopen (file, "rb");
	if (z) {
	    int type = zfile_gettype (z);
	    struct romdata *rd = getromdatabyzfile (z);
	    zfile_fclose (z);
	    switch (type)
	    {
		case  ZFILE_DISKIMAGE:
		    if (currentpage == DISK_ID) {
			list = 0;
			while (list < MAX_SPARE_DRIVES) {
			    if (!strcasecmp (prefs->dfxlist[list], file))
				break;
			    list++;
			}
			if (list == MAX_SPARE_DRIVES) {
			    list = 0;
			    while (list < MAX_SPARE_DRIVES) {
				if (!prefs->dfxlist[list][0]) {
				    strcpy (prefs->dfxlist[list], file);
				    break;
				}
				list++;
			    }
			}
		    } else {
			DISK_history_add (file, -1);
			strcpy (workprefs.df[drv], file);
		        strcpy (changed_prefs.df[drv], workprefs.df[drv]);
			drv++;
			if (drv >= (currentpage == QUICKSTART_ID ? 2 : 4))
			    drv = 0;
			if (workprefs.dfxtype[drv] < 0)
			    drv = 0;
			if (drv == firstdrv)
			    i = cnt;
		    }
		break;
		case ZFILE_ROM:
		    if (rd) {
			if (rd->type == ROMTYPE_KICK || rd->type == ROMTYPE_KICKCD32)
			    strcpy (prefs->romfile, file);
			if (rd->type == ROMTYPE_EXTCD32 || rd->type == ROMTYPE_EXTCDTV)
			    strcpy (prefs->romextfile, file);
			if (rd->type == ROMTYPE_AR)
			    strcpy (prefs->cartfile, file);
		    } else {
			strcpy (prefs->romfile, file);
		    }
		break;
		case ZFILE_HDF:
		{
		    char *result;
		    if (currentpage == HARDDISK_ID) {
			if (flags & FILE_ATTRIBUTE_DIRECTORY) {
			    result = add_filesys_unit (currprefs.mountinfo, NULL, "XXX", file, 0,
			        0, 0, 0, 0, 0, NULL, 0);
			} else {
			    result = add_filesys_unit (currprefs.mountinfo, NULL, NULL, file, 0,
			        32, 1, 2, 512, 0, NULL, 0);
			}
		    }
		}
		break;
		case ZFILE_NVR:
		    strcpy (prefs->flashfile, file);
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
		    strcpy (savestate_fname, file);
		    ret = 1;
		break;
	    }
	}
    }
    DragFinish (hd);
    return ret;
}

static int dialogreturn;
static INT_PTR CALLBACK DialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int recursive = 0;

    switch(msg)
    {
	case WM_DESTROY:
	PostQuitMessage (0);
	return TRUE;
	case WM_CLOSE:
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
	    guiDlg = hDlg;
	    SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE(IDI_APPICON)));
	    if (full_property_sheet) {
		char tmp[100];
		WIN32GUI_LoadUIString (IDS_STARTEMULATION, tmp, sizeof (tmp));
		SetWindowText (GetDlgItem (guiDlg, IDOK), tmp);
	    }
	    centerWindow (hDlg);
	    createTreeView (hDlg, currentpage);
	    updatePanel (hDlg, currentpage);
	    return TRUE;
	case WM_DROPFILES:
	    if (dragdrop (hDlg, (HDROP)wParam, (gui_active || full_property_sheet) ? &workprefs : &changed_prefs, currentpage))
		SendMessage (hDlg, WM_COMMAND, IDOK, 0);
	    updatePanel (hDlg, currentpage);
	    return FALSE;
	case WM_NOTIFY:
	    switch (((LPNMHDR)lParam)->code)
	    {
		case TVN_SELCHANGING:
		return FALSE;
		case TVN_SELCHANGED:
		{
		    LPNMTREEVIEW tv = (LPNMTREEVIEW)lParam;
		    currentpage = (int)(tv->itemNew.lParam & 0xffff);
		    configtypepanel = configtype = (int)(tv->itemNew.lParam >> 16);
		    updatePanel (hDlg, currentpage);
		    return TRUE;
		}
		break;
	    }
	    break;
	case WM_COMMAND:
	    switch (LOWORD(wParam))
	    {
		case IDC_RESETAMIGA:
		    uae_reset (0);
		    SendMessage (hDlg, WM_COMMAND, IDOK, 0);
		    return TRUE;
		case IDC_QUITEMU:
		    uae_quit ();
		    SendMessage (hDlg, WM_COMMAND, IDCANCEL, 0);
		    return TRUE;
		case IDHELP:
		    if (pHtmlHelp && ppage[currentpage].help)
			HtmlHelp (NULL, help_file, HH_DISPLAY_TOPIC, ppage[currentpage].help);
		    return TRUE;
		case IDOK:
		    updatePanel (hDlg, -1);
		    dialogreturn = 1;
		    DestroyWindow (hDlg);
		    gui_to_prefs ();
		    guiDlg = NULL;
		    return TRUE;
		case IDCANCEL:
		    updatePanel (hDlg, -1);
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
    return FALSE;
}

static ACCEL EmptyAccel[] = {
    { FVIRTKEY, VK_UP, 20001 }, { FVIRTKEY, VK_DOWN, 20002 },
    { 0, 0, 0 }
};

static int init_page (int tmpl, int icon, int title,
    INT_PTR (CALLBACK FAR *func) (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam), ACCEL *accels, char *help)
{
    static id = 0;
    int i = -1;

    LPTSTR lpstrTitle;
    ppage[id].pp.pszTemplate = MAKEINTRESOURCE (tmpl);
    ppage[id].pp.pszIcon = MAKEINTRESOURCE (icon);
    lpstrTitle = calloc (1, MAX_DPATH);
    LoadString (hUIDLL, title, lpstrTitle, MAX_DPATH);
    ppage[id].pp.pszTitle = lpstrTitle;
    ppage[id].pp.pfnDlgProc = func;
    ppage[id].help = help;
    ppage[id].idx = id;
    ppage[id].accel = NULL;
    ppage[id].tmpl = tmpl;
    if (!accels)
	accels = EmptyAccel;
    while (accels[++i].key);
    ppage[id].accel = CreateAcceleratorTable (accels, i);
    if (tmpl == IDD_FRONTEND)
	ppage[id].fullpanel = TRUE;
    id++;
    return id - 1;
}

static int GetSettings (int all_options, HWND hwnd)
{
    static int init_called = 0;
    int psresult;
    HWND dhwnd;

    gui_active = 1;

    full_property_sheet = all_options;
    allow_quit = all_options;
    pguiprefs = &currprefs;
    default_prefs (&workprefs, 0);

    WIN32GUI_LoadUIString (IDS_NONE, szNone, MAX_DPATH);

    prefs_to_gui (&changed_prefs);

    if (!init_called) {
	LOADSAVE_ID = init_page (IDD_LOADSAVE, IDI_CONFIGFILE, IDS_LOADSAVE, LoadSaveDlgProc, NULL, "gui/configurations.htm");
	MEMORY_ID = init_page (IDD_MEMORY, IDI_MEMORY, IDS_MEMORY, MemoryDlgProc, NULL, "gui/ram.htm");
	KICKSTART_ID = init_page (IDD_KICKSTART, IDI_MEMORY, IDS_KICKSTART, KickstartDlgProc, NULL, "gui/rom.htm");
	CPU_ID = init_page (IDD_CPU, IDI_CPU, IDS_CPU, CPUDlgProc, NULL, "gui/cpu.htm");
	DISPLAY_ID = init_page (IDD_DISPLAY, IDI_DISPLAY, IDS_DISPLAY, DisplayDlgProc, NULL, "gui/display.htm");
#if defined (GFXFILTER)
	HW3D_ID = init_page (IDD_FILTER, IDI_DISPLAY, IDS_FILTER, hw3dDlgProc, NULL, "gui/filter.htm");
#endif
	CHIPSET_ID = init_page (IDD_CHIPSET, IDI_CPU, IDS_CHIPSET, ChipsetDlgProc, NULL, "gui/chipset.htm");
	SOUND_ID = init_page (IDD_SOUND, IDI_SOUND, IDS_SOUND, SoundDlgProc, NULL, "gui/sound.htm");
	FLOPPY_ID = init_page (IDD_FLOPPY, IDI_FLOPPY, IDS_FLOPPY, FloppyDlgProc, NULL, "gui/floppies.htm");
	DISK_ID = init_page (IDD_DISK, IDI_FLOPPY, IDS_DISK, SwapperDlgProc, SwapperAccel, "gui/disk.htm");
#ifdef FILESYS
	HARDDISK_ID = init_page (IDD_HARDDISK, IDI_HARDDISK, IDS_HARDDISK, HarddiskDlgProc, HarddiskAccel, "gui/hard-drives.htm");
#endif
	PORTS_ID = init_page (IDD_PORTS, IDI_PORTS, IDS_PORTS, PortsDlgProc, NULL, "gui/ports.htm");
	INPUT_ID = init_page (IDD_INPUT, IDI_INPUT, IDS_INPUT, InputDlgProc, NULL, "gui/input.htm");
	MISC1_ID = init_page (IDD_MISC1, IDI_MISC1, IDS_MISC1, MiscDlgProc1, NULL, "gui/misc.htm");
	MISC2_ID = init_page (IDD_MISC2, IDI_MISC2, IDS_MISC2, MiscDlgProc2, NULL, "gui/misc2.htm");
#ifdef AVIOUTPUT
	AVIOUTPUT_ID = init_page (IDD_AVIOUTPUT, IDI_AVIOUTPUT, IDS_AVIOUTPUT, AVIOutputDlgProc, NULL, "gui/output.htm");
#endif
	PATHS_ID = init_page (IDD_PATHS, IDI_PATHS, IDS_PATHS, PathsDlgProc, NULL, "gui/paths.htm");
	QUICKSTART_ID = init_page (IDD_QUICKSTART, IDI_QUICKSTART, IDS_QUICKSTART, QuickstartDlgProc, NULL, "gui/quickstart.htm");
	ABOUT_ID = init_page (IDD_ABOUT, IDI_ABOUT, IDS_ABOUT, AboutDlgProc, NULL, NULL);
	FRONTEND_ID = init_page (IDD_FRONTEND, IDI_QUICKSTART, IDS_FRONTEND, AboutDlgProc, NULL, NULL);
	C_PAGES = FRONTEND_ID + 1;
	init_called = 1;
	if (quickstart && !qs_override)
	    currentpage = QUICKSTART_ID;
	else
	    currentpage = LOADSAVE_ID;
    }

    if (all_options || !configstore)
	CreateConfigStore (NULL);

    dialogreturn = -1;
    hAccelTable = NULL;
    dhwnd = CreateDialog (hUIDLL ? hUIDLL : hInst, MAKEINTRESOURCE (IDD_PANEL), hwnd, DialogProc);
    psresult = 0;
    if (dhwnd != NULL) {
	MSG msg;
	DWORD v;
	ShowWindow (dhwnd, SW_SHOW);
	for (;;) {
	    HANDLE IPChandle;
	    IPChandle = geteventhandleIPC();
	    if (IPChandle != INVALID_HANDLE_VALUE) {
		MsgWaitForMultipleObjects (1, &IPChandle, FALSE, INFINITE, QS_ALLINPUT);
		while (checkIPC(&workprefs));
	    }
	    while ((v = PeekMessage (&msg, NULL, 0, 0, PM_REMOVE))) {
		if (dialogreturn >= 0)
		    break;
		if (v == -1)
		    continue;
		if (!IsWindow (dhwnd))
		    continue;
		if (hAccelTable && panelDlg) {
		    if (TranslateAccelerator (panelDlg, hAccelTable, &msg))
			continue;
		}
		if (IsDialogMessage (dhwnd, &msg))
		    continue;
		TranslateMessage (&msg);
		DispatchMessage (&msg);
	    }
	    if (dialogreturn >= 0)
	        break;
	}
	psresult = dialogreturn;
    }

    if (quit_program)
	psresult = -2;
    else if (qs_request_reset && quickstart)
	uae_reset (qs_request_reset == 2 ? 1 : 0);

    qs_request_reset = 0;
    full_property_sheet = 0;
    gui_active = 0;
    return psresult;
}

int gui_init (void)
{
    int ret;
    
    read_rom_list();
    for (;;) {
	ret = GetSettings(1, currprefs.win32_notaskbarbutton ? hHiddenWnd : GetDesktopWindow());
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
    closeprinter(); // Bernd Roesch
#endif
}

extern HWND hStatusWnd;
struct gui_info gui_data;

void check_prefs_changed_gui( void )
{
}

void gui_hd_led (int led)
{
    static int resetcounter;

    int old = gui_data.hd;
    if (led == 0) {
	resetcounter--;
	if (resetcounter > 0)
	    return;
    }
    gui_data.hd = led;
    resetcounter = 6;
    if (old != gui_data.hd)
	gui_led (5, gui_data.hd);
}

void gui_cd_led (int led)
{
    static int resetcounter;

    int old = gui_data.cd;
    if (led == 0) {
	resetcounter--;
	if (resetcounter > 0)
	    return;
    }
    gui_data.cd = led;
    resetcounter = 6;
    if (old != gui_data.cd)
	gui_led (6, gui_data.cd);
}

void gui_fps (int fps, int idle)
{
    gui_data.fps = fps;
    gui_data.idle = idle;
    gui_led (7, 0);
    gui_led (8, 0);
}

void gui_led (int led, int on)
{
    WORD type;
    static char drive_text[NUM_LEDS * 16];
    static char dfx[4][300];
    char *ptr, *tt, *p;
    int pos = -1, j;

    indicator_leds (led, on);
#ifdef LOGITECHLCD
    lcd_update (led, on);
#endif
    if (!hStatusWnd)
	return;
    if (on)
	type = SBT_POPOUT;
    else
	type = 0;
    tt = NULL;
    if (led >= 1 && led <= 4) {
	pos = 5 + (led - 1);
	ptr = drive_text + pos * 16;
	if (gui_data.drive_disabled[led - 1])
	    strcpy (ptr, "");
	else
	    sprintf (ptr , "%02d  .", gui_data.drive_track[led - 1]);
	p = gui_data.df[led - 1];
	j = strlen (p) - 1;
	if (j < 0)
	    j = 0;
	while (j > 0) {
	    if (p[j - 1] == '\\' || p[j - 1] == '/')
		break;
	    j--;
	}
	tt = dfx[led - 1]; 
	tt[0] = 0;
	if (strlen (p + j) > 0)
	    sprintf (tt, "%s (CRC=%08.8X)", p + j, gui_data.crc32[led - 1]);
    } else if (led == 0) {
	pos = 2;
	ptr = strcpy (drive_text + pos * 16, "Power");
    } else if (led == 5) {
	pos = 3;
	ptr = strcpy (drive_text + pos * 16, "HD");
    } else if (led == 6) {
	pos = 4;
	ptr = strcpy (drive_text + pos * 16, "CD");
    } else if (led == 7) {
	pos = 1;
	ptr = drive_text + pos * 16;
	sprintf(ptr, "FPS: %.1f", (double)(gui_data.fps  / 10.0));
    } else if (led == 8) {
	pos = 0;
	ptr = drive_text + pos * 16;
	sprintf(ptr, "CPU: %.0f%%", (double)((gui_data.idle) / 10.0));
    }
    if (pos >= 0) {
	PostMessage (hStatusWnd, SB_SETTEXT, (WPARAM) ((pos + 1) | type), (LPARAM) ptr);
	if (tt != NULL)
	    PostMessage (hStatusWnd, SB_SETTIPTEXT, (WPARAM) (pos + 1), (LPARAM) tt);
    }
}

void gui_filename (int num, const char *name)
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
    if (!isfullscreen ())
	return 0;
    hr = DirectDraw_FlipToGDISurface();
    if (FAILED(hr))
	write_log ("FlipToGDISurface failed, %s\n", DXError (hr));
    *flags &= ~MB_SETFOREGROUND;
    return 0;
/*
    HRESULT hr;
    hr = DirectDraw_FlipToGDISurface();
    if (FAILED(hr)) {
	write_log ("FlipToGDISurface failed, %s\n", DXError (hr));
	return 0;
    }
    *hwnd = NULL;
    return 1;
*/
}

int gui_message_multibutton (int flags, const char *format,...)
{
    char msg[2048];
    char szTitle[ MAX_DPATH ];
    va_list parms;
    int flipflop = 0;
    int fullscreen = 0;
    int focuso = focus;
    int mbflags, ret;
    HWND hwnd;

    mbflags = MB_ICONWARNING | MB_TASKMODAL;
    if (flags == 0)
	mbflags |= MB_OK;
    else if (flags == 1)
	mbflags |= MB_YESNO;
    else if (flags == 2)
	mbflags |= MB_YESNOCANCEL;

    pause_sound ();
    flipflop = fsdialog (&hwnd, &mbflags);
    if (flipflop)
	ShowWindow (hAmigaWnd, SW_MINIMIZE);

    va_start (parms, format);
    vsprintf( msg, format, parms );
    va_end (parms);
    write_log( msg );
    if (msg[strlen(msg)-1]!='\n')
	write_log("\n");

    WIN32GUI_LoadUIString (IDS_ERRORTITLE, szTitle, MAX_DPATH);

    ret = MessageBox (hwnd, msg, szTitle, mbflags);

    if (flipflop)
	ShowWindow (hAmigaWnd, SW_RESTORE);

    resume_sound ();
    setmouseactive (focuso);
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

void gui_message (const char *format,...)
{
    char msg[2048];
    char szTitle[ MAX_DPATH ];
    va_list parms;
    int flipflop = 0;
    int fullscreen = 0;
    int focuso = focus;
    DWORD flags = MB_OK | MB_TASKMODAL;
    HWND hwnd;

    va_start (parms, format);
    vsprintf( msg, format, parms );
    va_end (parms);
    if (full_property_sheet) {
	pre_gui_message (msg);
	return;
    }
    pause_sound ();
    flipflop = fsdialog (&hwnd, &flags);
    if (flipflop)
	ShowWindow (hAmigaWnd, SW_MINIMIZE);

    write_log( msg );
    if (msg[strlen(msg)-1]!='\n')
	write_log("\n");

    WIN32GUI_LoadUIString (IDS_ERRORTITLE, szTitle, MAX_DPATH);

    MessageBox (hwnd, msg, szTitle, flags);

    if (flipflop)
	ShowWindow (hAmigaWnd, SW_RESTORE);
    resume_sound();
    setmouseactive (focuso);
}

void gui_message_id (int id)
{
    char msg[MAX_DPATH];
    WIN32GUI_LoadUIString (id, msg, sizeof (msg));
    gui_message (msg);
}

void pre_gui_message (const char *format,...)
{
    char msg[2048];
    char szTitle[MAX_DPATH];
    va_list parms;

    va_start (parms, format);
    vsprintf( msg, format, parms );
    va_end (parms);
    write_log( msg );
    if (msg[strlen(msg)-1]!='\n')
	write_log("\n");

    WIN32GUI_LoadUIString (IDS_ERRORTITLE, szTitle, MAX_DPATH);
    MessageBox (guiDlg, msg, szTitle, MB_OK | MB_TASKMODAL | MB_SETFOREGROUND );

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
    NUMSG_ROMNEED,IDS_NUMSG_ROMNEED,
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
    char tmp[MAX_DPATH];
    int c = 0;

    c = gettranslation (msg);
    if (c < 0)
	return;
    WIN32GUI_LoadUIString (c, tmp, MAX_DPATH);
    gui_message (tmp);
}

void notify_user_parms (int msg, const char *parms, ...)
{
    char msgtxt[MAX_DPATH];
    char tmp[MAX_DPATH];
    int c = 0;
    va_list parms2;

    c = gettranslation (msg);
    if (c < 0)
	return;
    WIN32GUI_LoadUIString (c, tmp, MAX_DPATH);
    va_start (parms2, parms);
    vsprintf (msgtxt, tmp, parms2);
    gui_message (msgtxt);
    va_end (parms);
}


int translate_message (int msg,	char *out)
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
