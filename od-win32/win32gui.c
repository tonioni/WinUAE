/*==========================================================================
 *
 *  Copyright (C) 1996 Brian King
 *
 *  File:       win32gui.c
 *  Content:    Win32-specific gui features for UAE port.
 *
 ***************************************************************************/

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
#include <ddraw.h>

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

#define DISK_FORMAT_STRING "(*.adf;*.adz;*.gz;*.dms;*.fdi;*.ipf;*.zip;*.exe)\0*.adf;*.adz;*.gz;*.dms;*.fdi;*.ipf;*.zip;*.exe\0"
#define ROM_FORMAT_STRING "(*.rom;*.zip;*.roz)\0*.rom;*.zip;*.roz\0"
#define USS_FORMAT_STRING_RESTORE "(*.uss;*.gz;*.zip)\0*.uss;*.gz;*.zip\0"
#define USS_FORMAT_STRING_SAVE "(*.uss)\0*.uss\0"

static int allow_quit;
static int full_property_sheet = 1;
static struct uae_prefs *pguiprefs;
struct uae_prefs workprefs;
static int currentpage, currentpage_sub;
int gui_active;

extern HWND (WINAPI *pHtmlHelp)(HWND, LPCSTR, UINT, LPDWORD );
#undef HtmlHelp
#ifndef HH_DISPLAY_TOPIC
#define HH_DISPLAY_TOPIC 0
#endif
#define HtmlHelp(a,b,c,d) if( pHtmlHelp ) (*pHtmlHelp)(a,b,c,(LPDWORD)d); else \
{ char szMessage[MAX_DPATH]; WIN32GUI_LoadUIString( IDS_NOHELP, szMessage, MAX_DPATH ); gui_message( szMessage ); }

extern HWND hAmigaWnd;
extern char help_file[ MAX_DPATH ];

extern int mouseactive;
extern char *start_path;

static char config_filename[ MAX_DPATH ] = "";

static drive_specs blankdrive =
{"", "", 1, 32, 1, 2, 0, 0};

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
    ABOUT_ID = -1;
static HWND pages[MAX_C_PAGES];
static HWND guiDlg;

void exit_gui (int ok)
{
    if (guiDlg == NULL)
	return;
    SendMessage (guiDlg, WM_COMMAND, ok ? IDOK : IDCANCEL, 0);
}

static HICON hMoveUp = NULL, hMoveDown = NULL;
static HWND cachedlist = NULL;

#define MIN_CHIP_MEM 0
#define MAX_CHIP_MEM 5
#define MIN_FAST_MEM 0
#define MAX_FAST_MEM 4
#define MIN_SLOW_MEM 0
#define MAX_SLOW_MEM 3
#define MIN_Z3_MEM 0
#define MAX_Z3_MEM 10
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

static int cfgfile_doload (struct uae_prefs *p, const char *filename, int type)
{
    if (type == 0) {
	if (p->mountinfo == currprefs.mountinfo)
	    currprefs.mountinfo = 0;
	discard_prefs (p, 0);
#ifdef FILESYS
	free_mountinfo (currprefs.mountinfo);
	currprefs.mountinfo = alloc_mountinfo ();
#endif
    }
    default_prefs (p, type);
    return cfgfile_load (p, filename, 0);
}

static int gui_width = 640, gui_height = 480;

/* if drive is -1, show the full GUI, otherwise file-requester for DF[drive] */
void gui_display( int shortcut )
{
    int flipflop = 0;
    HRESULT hr;

    gui_active = 1;
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

    if( ( !WIN32GFX_IsPicassoScreen() && currprefs.gfx_afullscreen && ( currprefs.gfx_width < gui_width || currprefs.gfx_height < gui_height ) )
#ifdef PICASSO96
        || ( WIN32GFX_IsPicassoScreen() && currprefs.gfx_pfullscreen && ( picasso96_state.Width < gui_width || picasso96_state.Height < gui_height ) )
#endif
    ) {
        flipflop = 1;
    }
    WIN32GFX_ClearPalette();
    manual_painting_needed++; /* So that WM_PAINT will refresh the display */

    hr = DirectDraw_FlipToGDISurface();
    if (hr != DD_OK) {
	write_log ("FlipToGDISurface failed, %s\n", DXError (hr));
    }

    if( shortcut == -1 ) {
	int ret;
        if( flipflop )
            ShowWindow( hAmigaWnd, SW_MINIMIZE );
	ret = GetSettings (0, flipflop ? GetDesktopWindow () : hAmigaWnd);
        if( flipflop )
            ShowWindow( hAmigaWnd, SW_RESTORE );
	if (!ret) {
	    savestate_state = 0;
	}
    } else if (shortcut >= 0 && shortcut < 4) {
	write_log("1\n");
        DiskSelection( hAmigaWnd, IDC_DF0+shortcut, 0, &changed_prefs, 0 );
	write_log("2\n");
    } else if (shortcut == 5) {
        if (DiskSelection( hAmigaWnd, IDC_DOSAVESTATE, 9, &changed_prefs, 0 ))
	    save_state (savestate_fname, "Description!");
    } else if (shortcut == 4) {
        if (DiskSelection( hAmigaWnd, IDC_DOLOADSTATE, 10, &changed_prefs, 0 ))
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
    inputdevice_acquire (mouseactive);
#ifdef CD32
    akiko_exitgui ();
#endif
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
    gui_active = 0;

}

static void prefs_to_gui (struct uae_prefs *p)
{
    workprefs = *p;
    updatewinfsmode (&workprefs);
    /* Could also duplicate unknown lines, but no need - we never
       modify those.  */
#if 0
#ifdef _DEBUG
    if (workprefs.gfx_framerate < 5)
	workprefs.gfx_framerate = 5;
#endif
#endif
}

static void gui_to_prefs (void)
{
    struct uaedev_mount_info *mi = currprefs.mountinfo;
    /* Always copy our prefs to changed_prefs, ... */
    //free_mountinfo (workprefs.mountinfo);
    changed_prefs = workprefs;
    updatewinfsmode (&changed_prefs);
    currprefs.mountinfo = mi;
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
int DiskSelection( HWND hDlg, WPARAM wParam, int flag, struct uae_prefs *prefs, char *path_out)
{
    OPENFILENAME openFileName;
    char regfloppypath[MAX_DPATH] = "";
    char regrompath[MAX_DPATH] = "";
    char reghdfpath[MAX_DPATH] = "";
    DWORD dwType = REG_SZ;
    DWORD dwRFPsize = MAX_DPATH;
    DWORD dwRRPsize = MAX_DPATH;
    DWORD dwRHPsize = MAX_DPATH;
    
    char full_path[MAX_DPATH] = "";
    char file_name[MAX_DPATH] = "";
    char init_path[MAX_DPATH] = "";
    BOOL result = FALSE;
    char *amiga_path = NULL;
    char description[ CFG_DESCRIPTION_LENGTH ] = "";
    char *p;
    int all = 1;

    char szTitle[ MAX_DPATH ];
    char szFormat[ MAX_DPATH ];
    char szFilter[ MAX_DPATH ] = { 0 };
    
    memset (&openFileName, 0, sizeof (OPENFILENAME));
    if( hWinUAEKey )
    {
        RegQueryValueEx( hWinUAEKey, "FloppyPath", 0, &dwType, (LPBYTE)regfloppypath, &dwRFPsize );
        RegQueryValueEx( hWinUAEKey, "KickstartPath", 0, &dwType, (LPBYTE)regrompath, &dwRRPsize );
        RegQueryValueEx( hWinUAEKey, "hdfPath", 0, &dwType, (LPBYTE)reghdfpath, &dwRHPsize );
    }
    
    strncpy( init_path, start_path, MAX_DPATH );
    switch( flag )
    {
	case 0:
	case 1:
	    if( regfloppypath[0] )
		strncpy( init_path, regfloppypath, MAX_DPATH );
	    else
		strncat( init_path, "..\\shared\\adf\\", MAX_DPATH );
	break;
	case 2:
	case 3:
	    if( reghdfpath[0] )
		strncpy( init_path, reghdfpath, MAX_DPATH );
	    else
		strncat( init_path, "..\\shared\\hdf\\", MAX_DPATH );
	break;
	case 6:
	case 7:
	case 11:
	    if( regrompath[0] )
		strncpy( init_path, regrompath, MAX_DPATH );
	    else
		strncat( init_path, "..\\shared\\rom\\", MAX_DPATH );
	break;
	case 4:
	case 5:
	case 8:
	    strncat( init_path, "Configurations\\", MAX_DPATH );
	break;
	case 9:
	case 10:
	    strncat( init_path, "SaveStates\\", MAX_DPATH );
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
	memcpy( szFilter + strlen( szFilter ), DISK_FORMAT_STRING, sizeof( DISK_FORMAT_STRING ) + 1 );

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
	memcpy( szFilter + strlen( szFilter ), "(*.hdf;*.rdf)\0*.hdf;*.rdf\0", 26 );

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
    case 9:
    case 10:
	WIN32GUI_LoadUIString( flag == 10 ? IDS_RESTOREUSS : IDS_SAVEUSS, szTitle, MAX_DPATH );
	WIN32GUI_LoadUIString( IDS_USS, szFormat, MAX_DPATH );
	sprintf( szFilter, "%s ", szFormat );
	if (flag == 10) {
	    memcpy( szFilter + strlen( szFilter ), USS_FORMAT_STRING_RESTORE, sizeof (USS_FORMAT_STRING_RESTORE) + 1);
	    all = 1;
	} else {
	    memcpy( szFilter + strlen( szFilter ), USS_FORMAT_STRING_SAVE, sizeof (USS_FORMAT_STRING_SAVE) + 1);
	    p = szFilter;
	    while (p[0] != 0 || p[1] !=0 ) p++;
	    p++;
	    strcpy (p, "Uncompressed (*.uss)");
	    p += strlen(p) + 1;
	    strcpy (p, "*.uss");
	    p += strlen(p) + 1;
	    strcpy (p, "RAM dump (*.dat)");
	    p += strlen(p) + 1;
	    strcpy (p, "*.dat");
	    p += strlen(p) + 1;
	    *p = 0;
	    all = 0;
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
    openFileName.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_LONGNAMES | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    openFileName.lpstrCustomFilter = NULL;
    openFileName.nMaxCustFilter = 0;
    openFileName.nFilterIndex = 0;
    openFileName.lpstrFile = full_path;
    openFileName.nMaxFile = MAX_DPATH;
    openFileName.lpstrFileTitle = file_name;
    openFileName.nMaxFileTitle = MAX_DPATH;
    openFileName.lpstrInitialDir = init_path;
    openFileName.lpfnHook = NULL;
    openFileName.lpTemplateName = NULL;
    openFileName.lCustData = 0;
    if (flag == 1 || flag == 3 || flag == 5 || flag == 9 || flag == 11)
    {
	if( !(result = GetSaveFileName (&openFileName)) )
	    write_log ("GetSaveFileName() failed.\n");
    }
    else
    {
	if( !(result = GetOpenFileName (&openFileName)) )
	    write_log ("GetOpenFileName() failed.\n");
    }
    write_log("result=%d\n", result); // xxx
    if (result)
    {
	switch (wParam) 
        {
	case IDC_PATH_NAME:
	case IDC_PATH_FILESYS:
	    if( flag == 8 )
	    {
		if( strstr( full_path, "Configurations\\" ) )
		{
		    strcpy( full_path, init_path );
		    strcat( full_path, file_name );
		}
	    }
	    SetDlgItemText (hDlg, wParam, full_path);
            break;
	case IDC_DF0:
	    write_log("x1\n");
	    SetDlgItemText (hDlg, IDC_DF0TEXT, full_path);
	    write_log("x2\n");
	    strcpy( prefs->df[0], full_path );
	    write_log("x3\n");
	    DISK_history_add (full_path, -1);
	    write_log("x4\n");
            break;
	case IDC_DF1:
	    SetDlgItemText (hDlg, IDC_DF1TEXT, full_path);
	    strcpy( prefs->df[1], full_path );
	    DISK_history_add (full_path, -1);
            break;
	case IDC_DF2:
	    SetDlgItemText (hDlg, IDC_DF2TEXT, full_path);
	    strcpy( prefs->df[2], full_path );
	    DISK_history_add (full_path, -1);
            break;
	case IDC_DF3:
	    SetDlgItemText (hDlg, IDC_DF3TEXT, full_path);
	    strcpy( prefs->df[3], full_path );
	    DISK_history_add (full_path, -1);
            break;
	case IDC_DOSAVESTATE:
	case IDC_DOLOADSTATE:
	    savestate_initsave (full_path, openFileName.nFilterIndex);
	    break;
	case IDC_CREATE:
	    disk_creatediskfile( full_path, 0, SendDlgItemMessage( hDlg, IDC_FLOPPYTYPE, CB_GETCURSEL, 0, 0L ));
            break;
	case IDC_CREATE_RAW:
	    disk_creatediskfile( full_path, 1, SendDlgItemMessage( hDlg, IDC_FLOPPYTYPE, CB_GETCURSEL, 0, 0L ));
	    break;
	case IDC_LOAD:
	    if (cfgfile_doload(&workprefs, full_path, 0) == 0)
	    {
		char szMessage[MAX_DPATH];
		WIN32GUI_LoadUIString (IDS_COULDNOTLOADCONFIG, szMessage, MAX_DPATH);
		gui_message (szMessage);
	    }
	    else
	    {
		int type;
		cfgfile_get_description (full_path, description, &type);
		SetDlgItemText (hDlg, IDC_EDITDESCRIPTION, description);
		SetDlgItemText (hDlg, IDC_EDITNAME, full_path);
	    }
            break;
	case IDC_SAVE:
	    SetDlgItemText( hDlg, IDC_EDITNAME, full_path );
            cfgfile_save (&workprefs, full_path, 0);
            break;
	case IDC_ROMFILE:
	    SetDlgItemText( hDlg, IDC_ROMFILE, full_path );
	    strcpy( workprefs.romfile, full_path );
	    break;
	case IDC_ROMFILE2:
	    SetDlgItemText( hDlg, IDC_ROMFILE2, full_path );
	    strcpy( workprefs.romextfile, full_path );
	    break;
	case IDC_KEYFILE:
	    SetDlgItemText( hDlg, IDC_KEYFILE, full_path );
	    strcpy( workprefs.keyfile, full_path );
	    break;
	case IDC_FLASHFILE:
	    SetDlgItemText( hDlg, IDC_FLASHFILE, full_path );
	    strcpy( workprefs.flashfile, full_path );
	    break;
	case IDC_CARTFILE:
	    SetDlgItemText( hDlg, IDC_CARTFILE, full_path );
	    strcpy( workprefs.cartfile, full_path );
	    break;
        }
        if (path_out)
	    strcpy (path_out, full_path);
        if( flag == 0 || flag == 1 )
        {
            amiga_path = strstr( openFileName.lpstrFile, openFileName.lpstrFileTitle );
            if( amiga_path && amiga_path != openFileName.lpstrFile )
            {
                *amiga_path = 0;
                if( hWinUAEKey )
                    RegSetValueEx( hWinUAEKey, "FloppyPath", 0, REG_SZ, (CONST BYTE *)openFileName.lpstrFile, strlen( openFileName.lpstrFile ) );
            }
        }
        else if( flag == 2 || flag == 3 )
        {
            amiga_path = strstr( openFileName.lpstrFile, openFileName.lpstrFileTitle );
            if( amiga_path && amiga_path != openFileName.lpstrFile )
            {
                *amiga_path = 0;
                if( hWinUAEKey )
                    RegSetValueEx( hWinUAEKey, "hdfPath", 0, REG_SZ, (CONST BYTE *)openFileName.lpstrFile, strlen( openFileName.lpstrFile ) );
            }
        }
        else if( flag == 6 || flag == 7 )
        {
            amiga_path = strstr( openFileName.lpstrFile, openFileName.lpstrFileTitle );
            if( amiga_path && amiga_path != openFileName.lpstrFile )
            {
                *amiga_path = 0;
                if( hWinUAEKey )
                    RegSetValueEx( hWinUAEKey, "KickstartPath", 0, REG_SZ, (CONST BYTE *)openFileName.lpstrFile, strlen( openFileName.lpstrFile ) );
            }
        }
    }
    write_log("return=%d\n", result);
    return result;
}

static BOOL CreateHardFile (HWND hDlg, UINT hfsizem)
{
    HANDLE hf;
    int i = 0;
    BOOL result = FALSE;
    LONG highword = 0;
    DWORD ret;
    char init_path[MAX_DPATH] = "";
    uae_u64 hfsize;

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
};

static unsigned long memsizes[] = {
/* 0 */ 0,
/* 1 */ 0x00040000, /*  256-K */
/* 2 */ 0x00080000, /*  512-K */
/* 3 */ 0x00100000, /*  1-meg */
/* 4 */ 0x00200000, /*  2-meg */
/* 5 */ 0x00400000, /*  4-meg */
/* 6 */ 0x00800000, /*  8-meg */
/* 7 */ 0x01000000, /* 16-meg */
/* 8 */ 0x02000000, /* 32-meg */
/* 9 */ 0x04000000, /* 64-meg */
/* 10*/ 0x08000000, //128 Meg
/* 11*/ 0x10000000, //256 Meg 
/* 12*/ 0x20000000, //512 Meg The correct size is set in mman.c
/* 13*/ 0x40000000, //1GB
/* 14*/ 0x00180000, //1.5MB
};

static int msi_chip[] = { 1, 2, 3, 4, 5, 6 };
static int msi_bogo[] = { 0, 2, 3, 14, 4 };
static int msi_fast[] = { 0, 3, 4, 5, 6 };
static int msi_z3fast[] = { 0, 3, 4, 5, 6, 7, 8, 9, 10,11,12,13 };
static int msi_gfx[] = { 0, 3, 4, 5, 6,7,8};

static int CalculateHardfileSize (HWND hDlg)
{
    BOOL Translated = FALSE;
    UINT mbytes = 0;

    mbytes = GetDlgItemInt( hDlg, IDC_HFSIZE, &Translated, FALSE );
    if (mbytes <= 0)
	mbytes = 0;
    if( !Translated )
        mbytes = 0;
    return mbytes;
}

static const char *nth[] = {
    "", "second ", "third ", "fourth ", "fifth ", "sixth ", "seventh ", "eighth ", "ninth ", "tenth "
};

struct ConfigStruct {
    char Name[MAX_DPATH];
    char Path[MAX_DPATH];
    char Fullpath[MAX_DPATH];
    char Description[CFG_DESCRIPTION_LENGTH];
    int Type, Directory;
    struct ConfigStruct *Parent, *Child;
    HTREEITEM item;
};

static void GetConfigPath (char *path, struct ConfigStruct *parent, int noroot)
{
    if (parent == 0) {
	path[0] = 0;
	if (!noroot) {
	    if (start_path)
		strcpy (path, start_path);
	    strcat (path, "Configurations\\");
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

static struct ConfigStruct **configstore;
static int configstoresize, configstoreallocated;

static struct ConfigStruct *GetConfigs (struct ConfigStruct *configparent, int usedirs)
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
	    config = AllocConfigStruct ();
	    if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && usedirs) {
		struct ConfigStruct *child;
		strcpy (config->Name, find_data.cFileName);
		config->Directory = 1;
		child = GetConfigs (config, usedirs);
		if (child)
		    config->Child = child;
	    } else if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
		char path3[MAX_DPATH];
		int ok = 0;
		if (strlen (find_data.cFileName) > 4 && !strcasecmp (find_data.cFileName + strlen (find_data.cFileName) - 4, ".uae")) {
    		    strcpy (path3, path);
		    strncat (path3, find_data.cFileName, MAX_DPATH);
		    if (cfgfile_get_description (path3, config->Description, &config->Type)) {
			strcpy (config->Name, find_data.cFileName);
			ok = 1;
		    }
		}
		if (!ok) {
		    FreeConfigStruct (config);
		    config = NULL;
		}
	    }
	}
	if (config) {
	    strcpy (config->Path, shortpath);
	    strcpy (config->Fullpath, path);
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
static void CreateConfigStore (void)
{
    FreeConfigStore ();
    GetConfigs (NULL, 1);
}

static char *HandleConfiguration (HWND hDlg, int flag, struct ConfigStruct *config)
{
    char name[MAX_DPATH], desc[MAX_DPATH];
    char path[MAX_DPATH];
    static char full_path[MAX_DPATH];
    int type;

    type = SendDlgItemMessage (hDlg, IDC_CONFIGTYPE, CB_GETCURSEL, 0, 0);
    if (type == CB_ERR)
	type = 0;
    full_path[0] = 0;
    GetDlgItemText (hDlg, IDC_EDITNAME, name, MAX_DPATH);
    if (flag == CONFIG_SAVE_FULL || flag == CONFIG_SAVE) {
	if (strlen (name) < 4 || strcasecmp (name + strlen (name) - 4, ".uae")) {
	    strcat (name, ".uae");
	    SetDlgItemText (hDlg, IDC_EDITNAME, name);
	}
    }
    GetDlgItemText (hDlg, IDC_EDITDESCRIPTION, desc, MAX_DPATH);
    if (config) {
        strcpy (path, config->Fullpath);
    } else {
        strncpy (path, start_path, MAX_DPATH);
        strncat (path, "Configurations\\", MAX_DPATH);
    }
    strncat (path, name, MAX_DPATH);
    strcpy (full_path, path);
    switch (flag)
    {
	case CONFIG_SAVE_FULL:
	    DiskSelection( hDlg, IDC_SAVE, 5, &workprefs, 0);
	break;

	case CONFIG_LOAD_FULL:
	    DiskSelection (hDlg, IDC_LOAD, 4, &workprefs, 0);
	    EnableWindow (GetDlgItem (hDlg, IDC_VIEWINFO), workprefs.info[0]);
	break;
		
	case CONFIG_SAVE:
	    if (strlen (name) == 0) {
		char szMessage[ MAX_DPATH ];
		WIN32GUI_LoadUIString( IDS_MUSTENTERNAME, szMessage, MAX_DPATH );
		gui_message( szMessage );
	    } else {
		strcpy (workprefs.description, desc);
		cfgfile_save (&workprefs, path, type);
	    }
	    break;
        
	case CONFIG_LOAD:
	    if (strlen (name) == 0) {
		char szMessage[ MAX_DPATH ];
		WIN32GUI_LoadUIString( IDS_MUSTSELECTCONFIG, szMessage, MAX_DPATH );
		gui_message( szMessage );
	    } else {
		if (cfgfile_doload (&workprefs, path, type) == 0) {
		    char szMessage[ MAX_DPATH ];
		    WIN32GUI_LoadUIString( IDS_COULDNOTLOADCONFIG, szMessage, MAX_DPATH );
		    gui_message( szMessage );
		} else {
		    EnableWindow (GetDlgItem (hDlg, IDC_VIEWINFO), workprefs.info[0]);
                }
            break;

	    case CONFIG_DELETE:
                if (strlen (name) == 0) {
		    char szMessage[ MAX_DPATH ];
		    WIN32GUI_LoadUIString( IDS_MUSTSELECTCONFIGFORDELETE, szMessage, MAX_DPATH );
		    gui_message( szMessage );
                } else {
		    char szMessage[ MAX_DPATH ];
		    char szTitle[ MAX_DPATH ];
		    WIN32GUI_LoadUIString( IDS_DELETECONFIGCONFIRMATION, szMessage, MAX_DPATH );
		    WIN32GUI_LoadUIString( IDS_DELETECONFIGTITLE, szTitle, MAX_DPATH );
                    if( MessageBox( hDlg, szMessage, szTitle,
			MB_YESNO | MB_ICONWARNING | MB_APPLMODAL | MB_SETFOREGROUND ) == IDYES ) {
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

static int disk_swap (int entry, int col)
{
    int drv, i, drvs[4] = { -1, -1, -1, -1 };

    for (i = 0; i < MAX_SPARE_DRIVES; i++) {
	drv = disk_in_drive (i);
	if (drv >= 0)
	    drvs[drv] = i;
    }
    if ((drv = disk_in_drive (entry)) >= 0) {
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

static int input_selected_device, input_selected_widget;
static int input_selected_event, input_selected_sub_num;

static void set_lventry_input (HWND list, int index)
{
    int flags, i, sub;
    char name[256];
    char af[10];

    inputdevice_get_mapped_name (input_selected_device, index, &flags, name, input_selected_sub_num);
    if (flags & IDEV_MAPPED_AUTOFIRE_SET)
	strcpy (af, "yes");
    else if (flags & IDEV_MAPPED_AUTOFIRE_POSSIBLE)
	strcpy (af, "no");
    else
	strcpy (af,"-");
    ListView_SetItemText(list, index, 1, name);
    ListView_SetItemText(list, index, 2, af);
    sub = 0;
    for (i = 0; i < MAX_INPUT_SUB_EVENT; i++) {
	if (inputdevice_get_mapped_name (input_selected_device, index, &flags, name, i)) sub++;
    }
    sprintf (name, "%d", sub);
    ListView_SetItemText(list, index, 3, name);
}

static void update_listview_input (HWND hDlg)
{
    int i;
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

void InitializeListView( HWND hDlg )
{
    int lv_type;
    HWND list;
    LV_ITEM lvstruct;
    LV_COLUMN lvcolumn;
    RECT rect;
    char column_heading[ HARDDISK_COLUMNS ][ MAX_COLUMN_HEADING_WIDTH ];
    char blocksize_str[6] = "";
    char readwrite_str[4] = "";
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
	for (i = 0; i < inputdevice_get_widget_num (input_selected_device); i++) {
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
	listview_column_width [2] = 50;
	listview_column_width [3] = 20;
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
		if (tmp2[j - 1] == '\\' || tmp2[j - 1] == '/')
		    break;
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
	listview_column_width[1] = 308;
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
					&cylinders, &size, &blocksize, &bootpri, 0);
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
            sprintf( readwrite_str, "%s", readonly ? "no" : "yes" );

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

static int CALLBACK InfoSettingsProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
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

static HTREEITEM AddConfigNode (HWND hDlg, struct ConfigStruct *config, char *name, char *desc, char *path, int isdir, HTREEITEM parent)
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
    if (isdir < 0) {
	is.itemex.state |= TVIS_EXPANDED;
	is.itemex.stateMask |= TVIS_EXPANDED;
    }
    strcat (s, name);
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

static void LoadConfigTreeView (HWND hDlg, int idx, HTREEITEM parent)
{
    struct ConfigStruct *cparent, *config;

    if (configstoresize == 0)
	return;
    if (idx < 0) {
	idx = 0;
	for (;;) {
	    config = configstore[idx];
	    if (config->Parent == NULL)
		break;
	    idx++;
	    if (idx >= configstoresize)
		return;
	}
    }
    cparent = configstore[idx]->Parent;
    idx = 0;
    for (;;) {
        config = configstore[idx];
	if (config->Parent == cparent) {
	    if (config->Directory && config->Child) {
		HTREEITEM par = AddConfigNode (hDlg, config, config->Name, NULL, config->Path, 1, parent);
		int idx2 = 0;
		for (;;) {
		    if (configstore[idx2] == config->Child) {
			config->item = par;
			LoadConfigTreeView (hDlg, idx2, par);
			break;
		    }
		    idx2++;
		    if (idx2 >= configstoresize)
			break;
		}
	    } else if (!config->Directory) {
		config->item = AddConfigNode (hDlg, config, config->Name, config->Description, config->Path, 0, parent);
	    }
	}
	idx++;
	if (idx >= configstoresize)
	    break;
    }
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
    parent = AddConfigNode (hDlg, NULL, path, NULL, NULL, -1, NULL);
    LoadConfigTreeView (hDlg, -1, parent);
    return parent;
}

static void ConfigToRegistry (struct ConfigStruct *config)
{
    if (hWinUAEKey && config) {
	char path[MAX_DPATH];
	strcpy (path, config->Path);
	strncat (path, config->Name, MAX_DPATH);
	RegSetValueEx (hWinUAEKey, "ConfigFile", 0, REG_SZ, (CONST BYTE *)path, strlen(path));
    }
}

static BOOL CALLBACK LoadSaveDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char name_buf[MAX_DPATH];
    char *cfgfile;
    static int recursive;
    HTREEITEM root;
    static struct ConfigStruct *config;
    int i;
    
    switch (msg)
    {
    case WM_INITDIALOG:
	recursive++;
	pages[LOADSAVE_ID] = hDlg;
	currentpage = LOADSAVE_ID;
	EnableWindow (GetDlgItem( hDlg, IDC_VIEWINFO ), workprefs.info[0]);
	SetDlgItemText (hDlg, IDC_EDITNAME, config_filename);
	SetDlgItemText (hDlg, IDC_EDITPATH, "");
	SetDlgItemText (hDlg, IDC_EDITDESCRIPTION, workprefs.description);
	root = InitializeConfigTreeView (hDlg);
	if (hWinUAEKey) {
	    DWORD dwType = REG_SZ;
	    DWORD dwRFPsize = sizeof (name_buf);
	    char path[MAX_DPATH];
	    if (RegQueryValueEx (hWinUAEKey, "ConfigFile", 0, &dwType, (LPBYTE)name_buf, &dwRFPsize) == ERROR_SUCCESS) {
		for (i = 0; i < configstoresize; i++) {
		    strcpy (path, configstore[i]->Path);
		    strncat (path, configstore[i]->Name, MAX_DPATH);
		    if (!strcmp (name_buf, path)) {
			config = configstore[i];
			break;
		    }
		}
	    }
	}
	if (config && config->item)
	    TreeView_SelectItem (GetDlgItem(hDlg, IDC_CONFIGTREE), config->item);
	else
	    TreeView_SelectItem (GetDlgItem(hDlg, IDC_CONFIGTREE), root);
/*
	SendDlgItemMessage(hDlg, IDC_CONFIGTYPE, CB_RESETCONTENT, 0, 0);
	SendDlgItemMessage(hDlg, IDC_CONFIGTYPE, CB_ADDSTRING, 0, (LPARAM)"Complete configuration file");
	SendDlgItemMessage(hDlg, IDC_CONFIGTYPE, CB_ADDSTRING, 0, (LPARAM)"Hardware only configuration file");
	SendDlgItemMessage(hDlg, IDC_CONFIGTYPE, CB_ADDSTRING, 0, (LPARAM)"Host only configuration file");
	SendDlgItemMessage(hDlg, IDC_CONFIGTYPE, CB_SETCURSEL, currentpage_sub, 0);
*/
	recursive--;
	return TRUE;

    case WM_USER:
	break;
	
    case WM_COMMAND:
        switch (LOWORD (wParam))
        {
	    case IDC_CONFIGTYPE:
		if (HIWORD (wParam) == CBN_SELCHANGE) {
		    currentpage_sub = SendDlgItemMessage (hDlg, IDC_CONFIGTYPE, CB_GETCURSEL, 0, 0);
		    InitializeConfigTreeView (hDlg);
		}
	    break;
	    case IDC_SAVE:
		HandleConfiguration (hDlg, CONFIG_SAVE_FULL, config);
	    	recursive++;
		CreateConfigStore ();
		InitializeConfigTreeView (hDlg);
		recursive--;
	    break;
	    case IDC_QUICKSAVE:
		HandleConfiguration (hDlg, CONFIG_SAVE, config);
	    	recursive++;
		CreateConfigStore ();
		InitializeConfigTreeView (hDlg);
		recursive--;
	    break;
	    case IDC_QUICKLOAD:
	        cfgfile = HandleConfiguration (hDlg, CONFIG_LOAD, config);
		ConfigToRegistry (config);
		if (full_property_sheet) {
		    inputdevice_updateconfig (&workprefs);
		} else {
		    uae_restart (-1, cfgfile);
		    exit_gui(1);
		}
	    break;
	    case IDC_LOAD:
		cfgfile = HandleConfiguration (hDlg, CONFIG_LOAD_FULL, config);
		ConfigToRegistry (config);
		if (full_property_sheet) {
		    inputdevice_updateconfig (&workprefs);
		} else {
		    uae_restart (-1, cfgfile);
		    exit_gui(1);
		}
	    break;
	    case IDC_DELETE:
		HandleConfiguration (hDlg, CONFIG_DELETE, config);
	    	recursive++;
		CreateConfigStore ();
		InitializeConfigTreeView (hDlg);
		recursive--;
	    break;
	    case IDC_VIEWINFO:
		if (workprefs.info[0])
		{
		    if (strstr( workprefs.info, "Configurations\\"))
			sprintf( name_buf, "%s\\%s", start_path, workprefs.info );
		    else
			strcpy( name_buf, workprefs.info );
		    ShellExecute( NULL, NULL, name_buf, NULL, NULL, SW_SHOWNORMAL );
		}
	    break;
	    case IDC_SETINFO:
		if (DialogBox(hUIDLL ? hUIDLL : hInst, MAKEINTRESOURCE (IDD_SETINFO), hDlg, InfoSettingsProc))
		    EnableWindow( GetDlgItem( hDlg, IDC_VIEWINFO ), workprefs.info[0] );
	    break;
	}
	break;
	
	case WM_NOTIFY:
	{
	    LPNMHDR nm = (LPNMHDR)lParam;
	    if (nm->hwndFrom == GetDlgItem( hDlg, IDC_CONFIGTREE)) {
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
				    ConfigToRegistry (config);
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
			config = (struct ConfigStruct*)tv->itemNew.lParam;
			if (config) {
			    if (!config->Directory) {
				SetDlgItemText (hDlg, IDC_EDITNAME, config->Name);
				SetDlgItemText (hDlg, IDC_EDITDESCRIPTION, config->Description);
			    }
			    SetDlgItemText (hDlg, IDC_EDITPATH, config->Path);
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

static int CALLBACK ContributorsProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
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
	CharFormat.yHeight = 10 * 20;	/* height in twips, where a twip is 1/20th of a point - for a pt.size of 18 */

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

#define NUM_URLS 8
typedef struct url_info
{
    int   id;
    BOOL  state;
    char *display;
    char *url;
} urlinfo;

static urlinfo urls[NUM_URLS] = 
{
    {IDC_CLOANTOHOME, FALSE, "Cloanto's Amiga Forever", "http://www.amigaforever.com/"},
    {IDC_AMIGAHOME, FALSE, "Amiga Inc.", "http://www.amiga.com"},
    {IDC_PICASSOHOME, FALSE, "Picasso96 Home Page", "http://www.picasso96.cogito.de/"}, 
    {IDC_UAEHOME, FALSE, "UAE Home Page", "http://www.freiburg.linux.de/~uae/"},
    {IDC_WINUAEHOME, FALSE, "WinUAE Home Page", "http://www.winuae.net/"},
    {IDC_AIABHOME, FALSE, "AIAB", "http://aiab.emuunlim.com/"},
    {IDC_THEROOTS, FALSE, "Back To The Roots", "http://back2roots.emuunlim.com/"},
    {IDC_CAPS, FALSE, "CAPS", "http://caps-project.org/"}
};

static void SetupRichText( HWND hDlg, urlinfo *url )
{
    CHARFORMAT CharFormat;
    CharFormat.cbSize = sizeof (CharFormat);

    SetDlgItemText( hDlg, url->id, url->display );
    SendDlgItemMessage( hDlg, url->id, EM_GETCHARFORMAT, 0, (LPARAM)&CharFormat );
    CharFormat.dwMask   |= CFM_UNDERLINE | CFM_SIZE | CFM_FACE | CFM_COLOR;
    CharFormat.dwEffects = url->state ? CFE_UNDERLINE : 0;
    CharFormat.yHeight = 10 * 20;	/* height in twips, where a twip is 1/20th of a point - for a pt.size of 18 */

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
    
    for (i = 0; i < NUM_URLS; i++) 
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

static void init_aboutdlg (HWND hDlg)
{
    CHARFORMAT CharFormat;
    int i;

    CharFormat.cbSize = sizeof (CharFormat);

    SetDlgItemText (hDlg, IDC_RICHEDIT1, "WinUAE");
    SendDlgItemMessage (hDlg, IDC_RICHEDIT1, EM_GETCHARFORMAT, 0, (LPARAM) & CharFormat);
    CharFormat.dwMask |= CFM_BOLD | CFM_SIZE | CFM_FACE;
    CharFormat.dwEffects = CFE_BOLD;
    CharFormat.yHeight = 18 * 20;	/* height in twips, where a twip is 1/20th of a point - for a pt.size of 18 */

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

    for( i = 0; i < NUM_URLS; i++ )
    {
        SetupRichText( hDlg, &urls[i] );
    }
}

static BOOL CALLBACK AboutDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
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
	    {
		DisplayContributors (hDlg);
	    }
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
    EnableWindow( GetDlgItem( hDlg, IDC_PFULLSCREEN ), rtg);
    if (! full_property_sheet) 
    {
	/* Disable certain controls which are only to be set once at start-up... */
        EnableWindow (GetDlgItem (hDlg, IDC_TEST16BIT), FALSE);
    }
    else
    {
        CheckDlgButton( hDlg, IDC_VSYNC, workprefs.gfx_vsync);
        if (workprefs.gfx_filter) {
	    EnableWindow (GetDlgItem (hDlg, IDC_XCENTER), FALSE);
	    EnableWindow (GetDlgItem (hDlg, IDC_YCENTER), FALSE);
	    EnableWindow (GetDlgItem (hDlg, IDC_LM_SCANLINES), FALSE);
	    if (workprefs.gfx_linedbl == 2)
		workprefs.gfx_linedbl = 1;
	    workprefs.gfx_xcenter = workprefs.gfx_ycenter = 0;
	} else {
	    EnableWindow (GetDlgItem (hDlg, IDC_XCENTER), TRUE);
	    EnableWindow (GetDlgItem (hDlg, IDC_YCENTER), TRUE);
	    EnableWindow (GetDlgItem (hDlg, IDC_LM_SCANLINES), TRUE);
	}
    }
}

static void enable_for_chipsetdlg (HWND hDlg)
{
    int enable = workprefs.cpu_cycle_exact ? FALSE : TRUE;

#if !defined (CPUEMU_6)
    EnableWindow (GetDlgItem (hDlg, IDC_CYCLEEXACT), FALSE);
#endif
    EnableWindow (GetDlgItem (hDlg, IDC_FASTCOPPER), enable);
    EnableWindow (GetDlgItem (hDlg, IDC_BLITIMM), enable);
    if (enable == FALSE) {
	workprefs.fast_copper = 0;
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
	    WIN32GUI_LoadUIString( IDS_SECOND, nth, dwNthMax );
	break;

	case 2:
	    WIN32GUI_LoadUIString( IDS_THIRD, nth, dwNthMax );	
	break;
	
	case 3:
	    WIN32GUI_LoadUIString( IDS_FOURTH, nth, dwNthMax );	
	break;
	
	case 4:
	    WIN32GUI_LoadUIString( IDS_FIFTH, nth, dwNthMax );	
	break;
	
	case 5:
	    WIN32GUI_LoadUIString( IDS_SIXTH, nth, dwNthMax );	
	break;
	
	case 6:
	    WIN32GUI_LoadUIString( IDS_SEVENTH, nth, dwNthMax );	
	break;
	
	case 7:
	    WIN32GUI_LoadUIString( IDS_EIGHTH, nth, dwNthMax );	
	break;
	
	case 8:
	    WIN32GUI_LoadUIString( IDS_NINTH, nth, dwNthMax );	
	break;
	
	case 9:
	    WIN32GUI_LoadUIString( IDS_TENTH, nth, dwNthMax );	
	break;
	
	default:
	    strcpy( nth, "" );
    }
}

static int fakerefreshrates[] = { 50, 60, 100, 120, 0 };
static int storedrefreshrates[MAX_REFRESH_RATES + 1];

static void init_frequency_combo (HWND hDlg, int dmode)
{
    int i, j, freq, index, tmp;
    char hz[20], hz2[20], txt[100];
    
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
    SendDlgItemMessage( hDlg, IDC_REFRESHRATE, CB_ADDSTRING, 0, (LPARAM)txt);
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
	SendDlgItemMessage( hDlg, IDC_REFRESHRATE, CB_ADDSTRING, 0, (LPARAM)hz);
    }
    index = CB_ERR;
    if (hz2[0] >= 0)
    	index = SendDlgItemMessage( hDlg, IDC_REFRESHRATE, CB_SELECTSTRING, 0, (LPARAM)hz2 );
    if (index == CB_ERR) {
        WIN32GUI_LoadUIString (IDS_VSYNC_DEFAULT, txt, sizeof (txt));
	SendDlgItemMessage( hDlg, IDC_REFRESHRATE, CB_SELECTSTRING, i, (LPARAM)txt);
	workprefs.gfx_refreshrate = 0;
    }
}

#define MAX_FRAMERATE_LENGTH 40
#define MAX_NTH_LENGTH 20

static int display_mode_index( uae_u32 x, uae_u32 y, uae_u32 d )
{
    int i;
    i = 0;
    while (DisplayModes[i].depth >= 0) {
        if( DisplayModes[i].res.width == x &&
            DisplayModes[i].res.height == y &&
            DisplayModes[i].depth == d )
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
	case 0:
	p = &workprefs.gfx_hue;
	break;
	case 1:
	p = &workprefs.gfx_saturation;
	break;
	case 2:
	p = &workprefs.gfx_luminance;
	break;
	case 3:
	p = &workprefs.gfx_contrast;
	break;
	case 4:
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
    currprefs.gfx_hue = workprefs.gfx_hue;
    currprefs.gfx_saturation = workprefs.gfx_saturation;
    currprefs.gfx_luminance = workprefs.gfx_luminance;
    currprefs.gfx_contrast = workprefs.gfx_contrast;
    currprefs.gfx_gamma = workprefs.gfx_gamma;
    init_colors ();
    reset_drawing ();
    redraw_frame ();
    updatedisplayarea ();
}

void init_da (HWND hDlg)
{
    int *p;
    SendDlgItemMessage(hDlg, IDC_DA_MODE, CB_RESETCONTENT, 0, 0);
    SendDlgItemMessage(hDlg, IDC_DA_MODE, CB_ADDSTRING, 0, (LPARAM)"Hue");
    SendDlgItemMessage(hDlg, IDC_DA_MODE, CB_ADDSTRING, 0, (LPARAM)"Saturation");
    SendDlgItemMessage(hDlg, IDC_DA_MODE, CB_ADDSTRING, 0, (LPARAM)"Luminance");
    SendDlgItemMessage(hDlg, IDC_DA_MODE, CB_ADDSTRING, 0, (LPARAM)"Contrast");
    SendDlgItemMessage(hDlg, IDC_DA_MODE, CB_ADDSTRING, 0, (LPARAM)"Gamma");
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

static void init_display_mode (HWND hDlg)
{
   int d, d2, index;

   switch( workprefs.color_mode )
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

    if( workprefs.gfx_afullscreen )
    {
        d2 = d;
        if( ( index = WIN32GFX_AdjustScreenmode( &workprefs.gfx_width_fs, &workprefs.gfx_height_fs, &d2 ) ) >= 0 )
        {
            switch( d2 )
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

    if ((index = display_mode_index (workprefs.gfx_width_fs, workprefs.gfx_height_fs, d)) >= 0) {
        SendDlgItemMessage( hDlg, IDC_RESOLUTION, CB_SETCURSEL, index, 0 );
        init_frequency_combo (hDlg, index);
    }
}

static void values_to_displaydlg (HWND hDlg)
{
    char buffer[ MAX_FRAMERATE_LENGTH + MAX_NTH_LENGTH ];
    char Nth[ MAX_NTH_LENGTH ];
    LPSTR blah[1] = { Nth };
    LPTSTR string = NULL;

    init_display_mode (hDlg);

    SetDlgItemInt( hDlg, IDC_XSIZE, workprefs.gfx_width_win, FALSE );
    SetDlgItemInt( hDlg, IDC_YSIZE, workprefs.gfx_height_win, FALSE );

    SendDlgItemMessage (hDlg, IDC_FRAMERATE, TBM_SETPOS, TRUE, workprefs.gfx_framerate);

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

    CheckRadioButton( hDlg, IDC_LM_NORMAL, IDC_LM_SCANLINES, IDC_LM_NORMAL + workprefs.gfx_linedbl );
    CheckDlgButton (hDlg, IDC_AFULLSCREEN, workprefs.gfx_afullscreen);
    CheckDlgButton (hDlg, IDC_PFULLSCREEN, workprefs.gfx_pfullscreen);
    CheckDlgButton (hDlg, IDC_ASPECT, workprefs.gfx_correct_aspect);
    CheckDlgButton (hDlg, IDC_LORES, workprefs.gfx_linedbl ? FALSE : TRUE);
    CheckDlgButton (hDlg, IDC_LORES, workprefs.gfx_lores);
    CheckDlgButton (hDlg, IDC_VSYNC, workprefs.gfx_vsync);
    
    CheckDlgButton (hDlg, IDC_XCENTER, workprefs.gfx_xcenter);
    CheckDlgButton (hDlg, IDC_YCENTER, workprefs.gfx_ycenter);

#if 0
    init_da (hDlg);
#endif
}

static void init_resolution_combo (HWND hDlg)
{
    int i = 0;
    SendDlgItemMessage(hDlg, IDC_RESOLUTION, CB_RESETCONTENT, 0, 0);
    while (DisplayModes[i].depth >= 0) {
        SendDlgItemMessage( hDlg, IDC_RESOLUTION, CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)DisplayModes[i].name);
	i++;
    }
}
static void init_displays_combo (HWND hDlg)
{
    int i = 0;
    SendDlgItemMessage(hDlg, IDC_DISPLAYSELECT, CB_RESETCONTENT, 0, 0);
    while (Displays[i].name) {
	SendDlgItemMessage( hDlg, IDC_DISPLAYSELECT, CB_ADDSTRING, 0, (LPARAM)Displays[i].name);
	i++;
    }
    if (workprefs.gfx_display >= i)
	workprefs.gfx_display = 0;
    SendDlgItemMessage( hDlg, IDC_DISPLAYSELECT, CB_SETCURSEL, workprefs.gfx_display, 0);
}

static void values_from_displaydlg (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    BOOL success = FALSE;
    int gfx_width = workprefs.gfx_width_win;
    int gfx_height = workprefs.gfx_height_win;

    workprefs.gfx_pfullscreen    = IsDlgButtonChecked (hDlg, IDC_PFULLSCREEN);
    workprefs.gfx_afullscreen    = IsDlgButtonChecked (hDlg, IDC_AFULLSCREEN);

    workprefs.gfx_lores          = IsDlgButtonChecked (hDlg, IDC_LORES);
    workprefs.gfx_correct_aspect = IsDlgButtonChecked (hDlg, IDC_ASPECT);
    workprefs.gfx_linedbl = ( IsDlgButtonChecked( hDlg, IDC_LM_SCANLINES ) ? 2 :
                              IsDlgButtonChecked( hDlg, IDC_LM_DOUBLED ) ? 1 : 0 );

    workprefs.gfx_framerate = SendDlgItemMessage (hDlg, IDC_FRAMERATE, TBM_GETPOS, 0, 0);
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
	workprefs.gfx_width_win  = GetDlgItemInt( hDlg, IDC_XSIZE, &success, FALSE );
        if( !success )
            workprefs.gfx_width_win = 800;
	workprefs.gfx_height_win = GetDlgItemInt( hDlg, IDC_YSIZE, &success, FALSE );
        if( !success )
            workprefs.gfx_height_win = 600;
    }
    workprefs.gfx_xcenter = (IsDlgButtonChecked (hDlg, IDC_XCENTER) ? 2 : 0 ); /* Smart centering */
    workprefs.gfx_ycenter = (IsDlgButtonChecked (hDlg, IDC_YCENTER) ? 2 : 0 ); /* Smart centering */

    if (msg == WM_COMMAND && HIWORD (wParam) == CBN_SELCHANGE) 
    {
	if (LOWORD (wParam) == IDC_DISPLAYSELECT) {
	    LONG posn;
	    posn = SendDlgItemMessage (hDlg, IDC_DISPLAYSELECT, CB_GETCURSEL, 0, 0);
	    if (posn != CB_ERR && posn != workprefs.gfx_display) {
		if (Displays[posn].disabled)
		    posn = 0;
		workprefs.gfx_display = posn;
	        DisplayModes = Displays[workprefs.gfx_display].DisplayModes;
		init_resolution_combo (hDlg);
		init_display_mode (hDlg);
	    }
	    return;
	} else if (LOWORD (wParam) == IDC_RESOLUTION) {
	    LONG posn = SendDlgItemMessage (hDlg, IDC_RESOLUTION, CB_GETCURSEL, 0, 0);
	    if (posn == CB_ERR)
		return;
	    workprefs.gfx_width_fs  = DisplayModes[posn].res.width;
	    workprefs.gfx_height_fs = DisplayModes[posn].res.height;
	    switch( DisplayModes[posn].depth )
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
	    SetDlgItemInt( hDlg, IDC_XSIZE, workprefs.gfx_width_win, FALSE );
	    SetDlgItemInt( hDlg, IDC_YSIZE, workprefs.gfx_height_win, FALSE );
	    init_frequency_combo (hDlg, posn);
	} else if (LOWORD (wParam) == IDC_REFRESHRATE) {
	    LONG posn1, posn2;
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

static BOOL CALLBACK DisplayDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
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
	init_displays_combo( hDlg );
	init_resolution_combo( hDlg );
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
	    if( RegOpenKeyEx( HKEY_CURRENT_USER, "Software\\Arabuusimiehet\\WinUAE", 0, KEY_ALL_ACCESS, &hPixelFormatKey ) == ERROR_SUCCESS )
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

    switch( workprefs.chipset_mask )
    {
    case 0:
	CheckRadioButton( hDlg, IDC_OCS, IDC_AGA, IDC_OCS+0 );
	break;
    case CSMASK_ECS_AGNUS:
	CheckRadioButton( hDlg, IDC_OCS, IDC_AGA, IDC_OCS+1 );
	break;
    case CSMASK_ECS_DENISE:
	CheckRadioButton( hDlg, IDC_OCS, IDC_AGA, IDC_OCS+2 );
	break;
    case CSMASK_ECS_AGNUS | CSMASK_ECS_DENISE:
	CheckRadioButton( hDlg, IDC_OCS, IDC_AGA, IDC_OCS+3 );
	break;
    case CSMASK_AGA:
    case CSMASK_ECS_AGNUS | CSMASK_ECS_DENISE | CSMASK_AGA:
	CheckRadioButton( hDlg, IDC_OCS, IDC_AGA, IDC_OCS+4 );
	break;
    }
    CheckDlgButton (hDlg, IDC_NTSC, workprefs.ntscmode);
    CheckDlgButton (hDlg, IDC_FASTCOPPER, workprefs.fast_copper);
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

    workprefs.fast_copper = IsDlgButtonChecked (hDlg, IDC_FASTCOPPER);
    workprefs.immediate_blits = IsDlgButtonChecked (hDlg, IDC_BLITIMM);
    n = IsDlgButtonChecked (hDlg, IDC_CYCLEEXACT) ? 1 : 0;
    if (workprefs.cpu_cycle_exact != n) {
	workprefs.cpu_cycle_exact = workprefs.blitter_cycle_exact = n;
	if (n) {
	    if (workprefs.cpu_level == 0)
		workprefs.cpu_compatible = 1;
	    workprefs.immediate_blits = 0;
	    workprefs.fast_copper = 0;
	}
    }
    workprefs.collision_level = IsDlgButtonChecked (hDlg, IDC_COLLISION0) ? 0
				 : IsDlgButtonChecked (hDlg, IDC_COLLISION1) ? 1
				 : IsDlgButtonChecked (hDlg, IDC_COLLISION2) ? 2 : 3;
    workprefs.chipset_mask = IsDlgButtonChecked( hDlg, IDC_OCS ) ? 0
			      : IsDlgButtonChecked( hDlg, IDC_ECS_AGNUS ) ? CSMASK_ECS_AGNUS
			      : IsDlgButtonChecked( hDlg, IDC_ECS_DENISE ) ? CSMASK_ECS_DENISE
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

static BOOL CALLBACK ChipsetDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int recursive = 0;
    RGBFTYPE colortype      = RGBFB_NONE;
    DWORD dwType            = REG_DWORD;
    DWORD dwDisplayInfoSize = sizeof( colortype );

    switch (msg) {
    case WM_INITDIALOG:
	pages[CHIPSET_ID] = hDlg;
	currentpage = CHIPSET_ID;
#ifndef AGA
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
     case 0x00200000: mem_size = 4; break;
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

static BOOL CALLBACK MemoryDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int recursive = 0;
    unsigned int max_z3_mem = MAX_Z3_MEM;
    MEMORYSTATUS memstats;

    switch (msg)
    {
    case WM_INITDIALOG:
	pages[MEMORY_ID] = hDlg;
	currentpage = MEMORY_ID;

	memstats.dwLength = sizeof( memstats );
	GlobalMemoryStatus( &memstats );
	while( ( memstats.dwAvailPageFile + memstats.dwAvailPhys - 32000000) < (DWORD)( 1 << (max_z3_mem + 19) ) )
	    max_z3_mem--;

	SendDlgItemMessage (hDlg, IDC_CHIPMEM, TBM_SETRANGE, TRUE, MAKELONG (MIN_CHIP_MEM, MAX_CHIP_MEM));
	SendDlgItemMessage (hDlg, IDC_FASTMEM, TBM_SETRANGE, TRUE, MAKELONG (MIN_FAST_MEM, MAX_FAST_MEM));
	SendDlgItemMessage (hDlg, IDC_SLOWMEM, TBM_SETRANGE, TRUE, MAKELONG (MIN_SLOW_MEM, MAX_SLOW_MEM));
	SendDlgItemMessage (hDlg, IDC_Z3FASTMEM, TBM_SETRANGE, TRUE, MAKELONG (MIN_Z3_MEM, max_z3_mem));
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

static void values_to_kickstartdlg (HWND hDlg)
{
    SetDlgItemText( hDlg, IDC_ROMFILE, workprefs.romfile );
    SetDlgItemText( hDlg, IDC_ROMFILE2, workprefs.romextfile );
    SetDlgItemText( hDlg, IDC_KEYFILE, workprefs.keyfile );
    SetDlgItemText( hDlg, IDC_FLASHFILE, workprefs.flashfile );
    SetDlgItemText( hDlg, IDC_CARTFILE, workprefs.cartfile );
    CheckDlgButton( hDlg, IDC_KICKSHIFTER, workprefs.kickshifter );
    CheckDlgButton( hDlg, IDC_MAPROM, workprefs.maprom );
}

static BOOL CALLBACK KickstartDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch( msg ) 
    {
    case WM_INITDIALOG:
	pages[KICKSTART_ID] = hDlg;
	currentpage = KICKSTART_ID;
#if !defined(AUTOCONFIG)
        EnableWindow( GetDlgItem( hDlg, IDC_MAPROM), FALSE );
#endif
#if !defined (CDTV) && !defined (CD32)
        EnableWindow( GetDlgItem( hDlg, IDC_FLASHFILE), FALSE );
        EnableWindow( GetDlgItem( hDlg, IDC_ROMFILE2), FALSE );
#endif
#if !defined (ACTION_REPLAY)
        EnableWindow( GetDlgItem( hDlg, IDC_CARTFILE), FALSE );
#endif
#if defined (UAE_MINI)
        EnableWindow( GetDlgItem( hDlg, IDC_KICKSHIFTER), FALSE );
        EnableWindow( GetDlgItem( hDlg, IDC_ROMCHOOSER2), FALSE );
        EnableWindow( GetDlgItem( hDlg, IDC_CARTCHOOSER), FALSE );
        EnableWindow( GetDlgItem( hDlg, IDC_FLASHCHOOSER), FALSE );
#endif	
    case WM_USER:
	values_to_kickstartdlg (hDlg);
	return TRUE;

    case WM_COMMAND:
	switch( wParam ) 
	{
	case IDC_KICKCHOOSER:
	    DiskSelection( hDlg, IDC_ROMFILE, 6, &workprefs, 0);
	    break;

	case IDC_ROMCHOOSER2:
	    DiskSelection( hDlg, IDC_ROMFILE2, 6, &workprefs, 0);
	    break;
	            
	case IDC_KEYCHOOSER:
	    DiskSelection( hDlg, IDC_KEYFILE, 7, &workprefs, 0);
	    break;
                
	case IDC_FLASHCHOOSER:
	    DiskSelection( hDlg, IDC_FLASHFILE, 11, &workprefs, 0);
	    break;

	case IDC_CARTCHOOSER:
	    DiskSelection( hDlg, IDC_CARTFILE, 6, &workprefs, 0);
	    break;

	case IDC_KICKSHIFTER:
	    workprefs.kickshifter = IsDlgButtonChecked( hDlg, IDC_KICKSHIFTER );
	    break;

	case IDC_MAPROM:
	    workprefs.maprom = IsDlgButtonChecked( hDlg, IDC_MAPROM ) ? 0xe00000 : 0;
	    break;

	default:
	    if( SendMessage( GetDlgItem( hDlg, IDC_ROMFILE ), EM_GETMODIFY, 0, 0 ) )
	    {
		GetDlgItemText( hDlg, IDC_ROMFILE, workprefs.romfile, CFG_ROM_LENGTH);
		SendMessage( GetDlgItem( hDlg, IDC_ROMFILE ), EM_SETMODIFY, 0, 0 );
	    }
	    if( SendMessage( GetDlgItem( hDlg, IDC_ROMFILE2 ), EM_GETMODIFY, 0, 0 ) )
	    {
		GetDlgItemText( hDlg, IDC_ROMFILE2, workprefs.romextfile, CFG_ROM_LENGTH);
		SendMessage( GetDlgItem( hDlg, IDC_ROMFILE2 ), EM_SETMODIFY, 0, 0 );
	    }
	    if( SendMessage( GetDlgItem( hDlg, IDC_KEYFILE ), EM_GETMODIFY, 0, 0 ) )
	    {
		GetDlgItemText( hDlg, IDC_KEYFILE, workprefs.keyfile, CFG_KEY_LENGTH);
		SendMessage( GetDlgItem( hDlg, IDC_KEYFILE ), EM_SETMODIFY, 0, 0 );
	    }
	    if( SendMessage( GetDlgItem( hDlg, IDC_FLASHFILE ), EM_GETMODIFY, 0, 0 ) )
	    {
		GetDlgItemText( hDlg, IDC_FLASHFILE, workprefs.flashfile, CFG_ROM_LENGTH);
		SendMessage( GetDlgItem( hDlg, IDC_FLASHFILE ), EM_SETMODIFY, 0, 0 );
	    }
	    if( SendMessage( GetDlgItem( hDlg, IDC_CARTFILE ), EM_GETMODIFY, 0, 0 ) )
	    {
		GetDlgItemText( hDlg, IDC_CARTFILE, workprefs.cartfile, CFG_ROM_LENGTH);
		SendMessage( GetDlgItem( hDlg, IDC_CARTFILE ), EM_SETMODIFY, 0, 0 );
	    }
	    break;
	}
    	break;
    }
    return FALSE;
}

static void enable_for_miscdlg (HWND hDlg)
{
    if( !full_property_sheet )
    {
        EnableWindow( GetDlgItem( hDlg, IDC_JULIAN), TRUE);
        EnableWindow( GetDlgItem( hDlg, IDC_CTRLF11), TRUE);
        EnableWindow( GetDlgItem( hDlg, IDC_SOCKETS), FALSE);
        EnableWindow( GetDlgItem( hDlg, IDC_SHOWGUI ), FALSE );
        EnableWindow( GetDlgItem( hDlg, IDC_CREATELOGFILE ), FALSE );
        EnableWindow( GetDlgItem( hDlg, IDC_ILLEGAL ), FALSE );
        EnableWindow( GetDlgItem( hDlg, IDC_NOSPEED ), TRUE );
        EnableWindow( GetDlgItem( hDlg, IDC_NOSPEEDPAUSE ), TRUE );
        EnableWindow( GetDlgItem( hDlg, IDC_NOSOUND ), TRUE );
	EnableWindow( GetDlgItem( hDlg, IDC_NOOVERLAY ), FALSE );
        EnableWindow( GetDlgItem( hDlg, IDC_DOSAVESTATE ), TRUE );
        EnableWindow( GetDlgItem( hDlg, IDC_ASPI ), FALSE );
        EnableWindow( GetDlgItem( hDlg, IDC_SCSIDEVICE ), FALSE );
        EnableWindow( GetDlgItem( hDlg, IDC_NOUAEFSDB ), FALSE );
        EnableWindow( GetDlgItem( hDlg, IDC_CLOCKSYNC ), FALSE );
        EnableWindow( GetDlgItem( hDlg, IDC_STATE_CAPTURE ), FALSE );
        EnableWindow( GetDlgItem( hDlg, IDC_STATE_RATE ), FALSE );
        EnableWindow( GetDlgItem( hDlg, IDC_STATE_BUFFERSIZE ), FALSE );
    } else {
#if !defined (FILESYS)
        EnableWindow( GetDlgItem( hDlg, IDC_CLOCKSYNC ), FALSE );
#endif
#if !defined (BSDSOCKET)
        EnableWindow( GetDlgItem( hDlg, IDC_SOCKETS), FALSE);
#endif
#if !defined (SCSIEMU)
        EnableWindow( GetDlgItem( hDlg, IDC_SCSIDEVICE), FALSE);
        EnableWindow( GetDlgItem( hDlg, IDC_ASPI ), FALSE );
#endif
        if( workprefs.win32_logfile )
	    EnableWindow( GetDlgItem( hDlg, IDC_ILLEGAL ), TRUE );
        else
	    EnableWindow( GetDlgItem( hDlg, IDC_ILLEGAL ), FALSE );
        EnableWindow( GetDlgItem( hDlg, IDC_DOSAVESTATE ), FALSE );
	EnableWindow( GetDlgItem( hDlg, IDC_STATE_RATE ), workprefs.statecapture ? TRUE : FALSE );
	EnableWindow( GetDlgItem( hDlg, IDC_STATE_BUFFERSIZE ), workprefs.statecapture ? TRUE : FALSE );
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
    int nv = SendDlgItemMessage(hDlg, v, CB_GETCURSEL, 0, 0L);
    if (nv != CB_ERR) {
	workprefs.keyboard_leds[n] = nv;
	misc_kbled (hDlg, v, nv);
    }
    workprefs.keyboard_leds_in_use = workprefs.keyboard_leds[0] | workprefs.keyboard_leds[1] | workprefs.keyboard_leds[2];
}

static void misc_getpri (HWND hDlg, int v, int *n)
{
    int nv = SendDlgItemMessage(hDlg, v, CB_GETCURSEL, 0, 0L);
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

static void values_to_miscdlg (HWND hDlg)
{
    char txt[100];

    CheckDlgButton( hDlg, IDC_SOCKETS, workprefs.socket_emu );
    CheckDlgButton( hDlg, IDC_ILLEGAL, workprefs.illegal_mem);
    CheckDlgButton( hDlg, IDC_SHOWGUI, workprefs.start_gui);
    CheckDlgButton( hDlg, IDC_JULIAN, workprefs.win32_middle_mouse );
    CheckDlgButton( hDlg, IDC_CREATELOGFILE, workprefs.win32_logfile );
    CheckDlgButton( hDlg, IDC_INACTIVE_PAUSE, workprefs.win32_inactive_pause );
    CheckDlgButton( hDlg, IDC_INACTIVE_NOSOUND, workprefs.win32_inactive_nosound );
    CheckDlgButton( hDlg, IDC_MINIMIZED_PAUSE, workprefs.win32_iconified_pause );
    CheckDlgButton( hDlg, IDC_MINIMIZED_NOSOUND, workprefs.win32_iconified_nosound );
    CheckDlgButton( hDlg, IDC_CTRLF11, workprefs.win32_ctrl_F11_is_quit );
    CheckDlgButton( hDlg, IDC_NOOVERLAY, workprefs.win32_no_overlay );
    CheckDlgButton( hDlg, IDC_SHOWLEDS, workprefs.leds_on_screen );
    CheckDlgButton( hDlg, IDC_SCSIDEVICE, workprefs.scsi );
    CheckDlgButton( hDlg, IDC_NOUAEFSDB, workprefs.filesys_no_uaefsdb );
    CheckDlgButton( hDlg, IDC_ASPI, workprefs.win32_aspi );
    CheckDlgButton( hDlg, IDC_STATE_CAPTURE, workprefs.tod_hack );

    if (!os_winnt) {
	EnableWindow( GetDlgItem( hDlg, IDC_ASPI), FALSE );
	CheckDlgButton( hDlg, IDC_ASPI, BST_CHECKED );
    }

    misc_kbled (hDlg, IDC_KBLED1, workprefs.keyboard_leds[0]);
    misc_kbled (hDlg, IDC_KBLED2, workprefs.keyboard_leds[1]);
    misc_kbled (hDlg, IDC_KBLED3, workprefs.keyboard_leds[2]);

    misc_addpri (hDlg, IDC_ACTIVE_PRIORITY, workprefs.win32_active_priority);
    misc_addpri (hDlg, IDC_INACTIVE_PRIORITY, workprefs.win32_inactive_priority);
    misc_addpri (hDlg, IDC_MINIMIZED_PRIORITY, workprefs.win32_iconified_priority);

    CheckDlgButton (hDlg, IDC_STATE_CAPTURE, workprefs.statecapture);

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
}

static BOOL MiscDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    char txt[100];

    switch (msg) 
    {

    case WM_USER:
	values_to_miscdlg (hDlg);
	enable_for_miscdlg (hDlg);
	return TRUE;

    case WM_COMMAND:

	if (currentpage == MISC1_ID) {
	    misc_getkbled (hDlg, IDC_KBLED1, 0);
	    misc_getkbled (hDlg, IDC_KBLED2, 1);
	    misc_getkbled (hDlg, IDC_KBLED3, 2);
	    SendDlgItemMessage (hDlg, IDC_STATE_RATE, WM_GETTEXT, (WPARAM)sizeof (txt), (LPARAM)txt);
	    workprefs.statecapturerate = atol (txt) * 50;
	    SendDlgItemMessage (hDlg, IDC_STATE_BUFFERSIZE, WM_GETTEXT, (WPARAM)sizeof (txt), (LPARAM)txt);
	    workprefs.statecapturebuffersize = atol (txt) * 1024 * 1024;
	} else {
	    misc_getpri (hDlg, IDC_ACTIVE_PRIORITY, &workprefs.win32_active_priority);
	    misc_getpri (hDlg, IDC_INACTIVE_PRIORITY, &workprefs.win32_inactive_priority);
	    misc_getpri (hDlg, IDC_MINIMIZED_PRIORITY, &workprefs.win32_iconified_priority);
	}

	switch( wParam )
	{
	case IDC_DOSAVESTATE:
	    if (DiskSelection( hDlg, wParam, 9, &workprefs, 0)) 
	        save_state (savestate_fname, "Description!");
	    break;
	case IDC_DOLOADSTATE:
	    if (DiskSelection( hDlg, wParam, 10, &workprefs, 0))
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
	    workprefs.win32_middle_mouse = IsDlgButtonChecked( hDlg, IDC_JULIAN );
	    break;
	case IDC_NOOVERLAY:
	    workprefs.win32_no_overlay = IsDlgButtonChecked( hDlg, IDC_NOOVERLAY );
	    break;
	case IDC_SHOWLEDS:
	    workprefs.leds_on_screen = IsDlgButtonChecked( hDlg, IDC_SHOWLEDS );
	    break;
	case IDC_SHOWGUI:
	    workprefs.start_gui = IsDlgButtonChecked (hDlg, IDC_SHOWGUI);
	    break;
	case IDC_CREATELOGFILE:
	    workprefs.win32_logfile = IsDlgButtonChecked( hDlg, IDC_CREATELOGFILE );
	    enable_for_miscdlg( hDlg );
	    break;
	case IDC_INACTIVE_PAUSE:
	    workprefs.win32_inactive_pause = IsDlgButtonChecked( hDlg, IDC_INACTIVE_PAUSE );
	    break;
	case IDC_INACTIVE_NOSOUND:
	    workprefs.win32_inactive_nosound = IsDlgButtonChecked( hDlg, IDC_INACTIVE_NOSOUND );
	    break;
	case IDC_MINIMIZED_PAUSE:
	    workprefs.win32_iconified_pause = IsDlgButtonChecked( hDlg, IDC_MINIMIZED_PAUSE );
	    break;
	case IDC_MINIMIZED_NOSOUND:
	    workprefs.win32_iconified_nosound = IsDlgButtonChecked( hDlg, IDC_MINIMIZED_NOSOUND );
	    break;
	case IDC_CTRLF11:
	    workprefs.win32_ctrl_F11_is_quit = IsDlgButtonChecked( hDlg, IDC_CTRLF11 );
	    break;
	case IDC_SCSIDEVICE:
	    workprefs.scsi = IsDlgButtonChecked( hDlg, IDC_SCSIDEVICE );
	    break;
	case IDC_NOUAEFSDB:
	    workprefs.filesys_no_uaefsdb = IsDlgButtonChecked( hDlg, IDC_NOUAEFSDB );
	    break;
	case IDC_ASPI:
	    workprefs.win32_aspi = IsDlgButtonChecked( hDlg, IDC_ASPI );
	    break;
	}
	return TRUE;
    }
    return FALSE;
}

static BOOL CALLBACK MiscDlgProc1 (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_INITDIALOG) {
	pages[MISC1_ID] = hDlg;
	currentpage = MISC1_ID;
	values_to_miscdlg (hDlg);
	enable_for_miscdlg (hDlg);
	return TRUE;
    }
    return MiscDlgProc (hDlg, msg, wParam, lParam);
}

static BOOL CALLBACK MiscDlgProc2 (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_INITDIALOG) {
	pages[MISC2_ID] = hDlg;
	currentpage = MISC2_ID;
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

    EnableWindow (GetDlgItem (hDlg, IDC_COMPATIBLE), !workprefs.cpu_cycle_exact && !workprefs.cachesize);
    /* These four items only get enabled when adjustable CPU style is enabled */
    EnableWindow (GetDlgItem (hDlg, IDC_SPEED), workprefs.m68k_speed > 0);
    EnableWindow (GetDlgItem (hDlg, IDC_CS_CPU_TEXT), workprefs.m68k_speed > 0);
    EnableWindow (GetDlgItem (hDlg, IDC_CS_CHIPSET_TEXT), workprefs.m68k_speed > 0);
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

    cpu_based_enable = ( workprefs.cpu_level >= 2 ) &&
		       ( workprefs.address_space_24 == 0 );

    enable = cpu_based_enable && ( workprefs.cachesize );
#ifndef JIT
    enable = FALSE;
#endif
    enable2 = enable && workprefs.compforcesettings;

    EnableWindow( GetDlgItem( hDlg, IDC_TRUST0 ), enable2 );
    EnableWindow( GetDlgItem( hDlg, IDC_TRUST1 ), enable2 );
    EnableWindow( GetDlgItem( hDlg, IDC_TRUST2 ), enable2 );
    EnableWindow( GetDlgItem( hDlg, IDC_HARDFLUSH ), enable2 );
    EnableWindow( GetDlgItem( hDlg, IDC_CONSTJUMP ), enable2 );
    EnableWindow( GetDlgItem( hDlg, IDC_JITFPU ), enable2 );
    EnableWindow( GetDlgItem( hDlg, IDC_NOFLAGS ), enable2 );
    EnableWindow( GetDlgItem( hDlg, IDC_CS_CACHE_TEXT ), cpu_based_enable );
    EnableWindow( GetDlgItem( hDlg, IDC_CACHE ), cpu_based_enable );
    EnableWindow( GetDlgItem( hDlg, IDC_CACHETEXT ), cpu_based_enable );
    EnableWindow( GetDlgItem( hDlg, IDC_FORCE ), enable );

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
}

static void values_from_cpudlg (HWND hDlg)
{
    int newcpu, newtrust, oldcache;
    
    workprefs.cpu_compatible = workprefs.cpu_cycle_exact | (IsDlgButtonChecked (hDlg, IDC_COMPATIBLE) ? 1 : 0);
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

    oldcache = workprefs.cachesize;
    workprefs.cachesize = SendMessage(GetDlgItem(hDlg, IDC_CACHE), TBM_GETPOS, 0, 0) * 1024;
#ifdef JIT
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

static BOOL CALLBACK CPUDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
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
	EnableWindow( GetDlgItem( hDlg, IDC_SOUNDCARDLIST ), FALSE );
    else
	EnableWindow( GetDlgItem( hDlg, IDC_SOUNDCARDLIST ), workprefs.produce_sound );

    EnableWindow( GetDlgItem( hDlg, IDC_FREQUENCY ), workprefs.produce_sound );
    EnableWindow( GetDlgItem( hDlg, IDC_SOUNDFREQ ), workprefs.produce_sound ? TRUE : FALSE );
    EnableWindow( GetDlgItem( hDlg, IDC_SOUNDSTEREO ), workprefs.produce_sound );
    EnableWindow( GetDlgItem( hDlg, IDC_SOUNDINTERPOLATION ), workprefs.produce_sound );
    EnableWindow( GetDlgItem( hDlg, IDC_SOUNDVOLUME ), workprefs.produce_sound );

    EnableWindow( GetDlgItem( hDlg, IDC_SOUNDBUFFERMEM ), workprefs.produce_sound );
    EnableWindow( GetDlgItem( hDlg, IDC_SOUNDBUFFERRAM ), workprefs.produce_sound );
    EnableWindow( GetDlgItem( hDlg, IDC_SOUNDADJUST ), workprefs.produce_sound );
    EnableWindow( GetDlgItem( hDlg, IDC_SOUNDADJUSTNUM ), workprefs.produce_sound );
    EnableWindow( GetDlgItem( hDlg, IDC_SOUNDBUFFERTEXT ), workprefs.produce_sound );

    EnableWindow( GetDlgItem( hDlg, IDC_SOUNDDRIVE ), workprefs.produce_sound );
    EnableWindow( GetDlgItem( hDlg, IDC_SOUNDDRIVESELECT ), workprefs.produce_sound );
    EnableWindow( GetDlgItem( hDlg, IDC_SOUNDDRIVEVOLUME ), workprefs.produce_sound );

    EnableWindow( GetDlgItem( hDlg, IDC_AUDIOSYNC ), workprefs.produce_sound );
 
    EnableWindow( GetDlgItem( hDlg, IDC_SOUNDFILTER ), workprefs.produce_sound );

    EnableWindow( GetDlgItem( hDlg, IDC_SOUNDCALIBRATE ), workprefs.produce_sound && full_property_sheet);
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
    sprintf (dirname, "%s\\uae_data\\*.wav", start_path);
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
		    if (oldp)
			p = p + strlen (p) + 1;
		}
		strcpy (p, name + strlen (DS_NAME_CLICK));
		p[strlen(name) - 4] = 0;
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
    SendDlgItemMessage( hDlg, IDC_SOUNDDRIVEVOLUME, TBM_SETPOS, TRUE, 100 - workprefs.dfxclickvolume );
}

static int soundfreqs[] = { 11025, 15000, 22050, 32000, 44100, 48000, 0 };

static void values_to_sounddlg (HWND hDlg)
{
    int which_button;
    int sound_freq = workprefs.sound_freq;
    int produce_sound = workprefs.produce_sound;
    int stereo = workprefs.stereo;
    char txt[100], *p;
    int i, idx, selected;

    if (workprefs.sound_maxbsiz & (workprefs.sound_maxbsiz - 1))
	workprefs.sound_maxbsiz = DEFAULT_SOUND_MAXB;

    SendDlgItemMessage(hDlg, IDC_SOUNDFILTER, CB_RESETCONTENT, 0, 0);
    WIN32GUI_LoadUIString (IDS_SOUND_FILTER_OFF, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_SOUNDFILTER, CB_ADDSTRING, 0, (LPARAM)txt);
    WIN32GUI_LoadUIString (IDS_SOUND_FILTER_EMULATED, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_SOUNDFILTER, CB_ADDSTRING, 0, (LPARAM)txt);
    WIN32GUI_LoadUIString (IDS_SOUND_FILTER_ON, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_SOUNDFILTER, CB_ADDSTRING, 0, (LPARAM)txt);
    SendDlgItemMessage(hDlg, IDC_SOUNDFILTER, CB_SETCURSEL, workprefs.sound_filter, 0 );

    SendDlgItemMessage(hDlg, IDC_SOUNDSTEREO, CB_RESETCONTENT, 0, 0);
    WIN32GUI_LoadUIString (IDS_SOUND_MONO, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_SOUNDSTEREO, CB_ADDSTRING, 0, (LPARAM)txt);
    WIN32GUI_LoadUIString (IDS_SOUND_MIXED, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_SOUNDSTEREO, CB_ADDSTRING, 0, (LPARAM)txt);
    WIN32GUI_LoadUIString (IDS_SOUND_STEREO, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_SOUNDSTEREO, CB_ADDSTRING, 0, (LPARAM)txt);
    which_button = 0;
    if (workprefs.stereo) {
	if (workprefs.mixed_stereo)
	    which_button = 1;
	else
	    which_button = 2;
    }
    SendDlgItemMessage (hDlg, IDC_SOUNDSTEREO, CB_SETCURSEL, which_button, 0);

    SendDlgItemMessage(hDlg, IDC_SOUNDINTERPOLATION, CB_RESETCONTENT, 0, 0);
    WIN32GUI_LoadUIString (IDS_SOUND_INTERPOL_DISABLED, txt, sizeof (txt));
    SendDlgItemMessage(hDlg, IDC_SOUNDINTERPOLATION, CB_ADDSTRING, 0, (LPARAM)txt);
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
    p = drivesounds;
    if (p) {
	while (p[0]) {
	    SendDlgItemMessage(hDlg, IDC_SOUNDDRIVESELECT, CB_ADDSTRING, 0, (LPARAM)p);
	    p += strlen (p) + 1;
	}
    }
    if (workprefs.dfxclick[idx] && workprefs.dfxclickexternal[idx][0]) {
	p = drivesounds;
	i = DS_BUILD_IN_SOUNDS + 1;
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
    int idx, i;
    char txt[6];

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
    workprefs.mixed_stereo = 0;
    workprefs.stereo = 0;
    if (idx > 0) {
	workprefs.stereo = 1;
	if (idx == 1)
	    workprefs.mixed_stereo = 1;
    }
    workprefs.sound_interpol = SendDlgItemMessage (hDlg, IDC_SOUNDINTERPOLATION, CB_GETCURSEL, 0, 0);
    workprefs.win32_soundcard = SendDlgItemMessage (hDlg, IDC_SOUNDCARDLIST, CB_GETCURSEL, 0, 0L);
    workprefs.sound_filter = SendDlgItemMessage (hDlg, IDC_SOUNDFILTER, CB_GETCURSEL, 0, 0);

    idx = SendDlgItemMessage (hDlg, IDC_SOUNDDRIVE, CB_GETCURSEL, 0, 0);
    if (idx >= 0) {
	i = SendDlgItemMessage (hDlg, IDC_SOUNDDRIVESELECT, CB_GETCURSEL, 0, 0);
	if (i >= 0) {
	    if (i > DS_BUILD_IN_SOUNDS) {
		int j = i - (DS_BUILD_IN_SOUNDS + 1);
		char *p = drivesounds;
		while (j-- > 0)
		    p += strlen (p) + 1;
		workprefs.dfxclick[idx] = -1;
		strcpy (workprefs.dfxclickexternal[idx], p);
	    } else {
		workprefs.dfxclick[idx] = i;
		workprefs.dfxclickexternal[idx][0] = 0;
	    }
	}
    }
}

extern int sound_calibrate (HWND, struct uae_prefs*);

static BOOL CALLBACK SoundDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
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
	if ((wParam & 0xffff) == IDC_SOUNDCALIBRATE) {
	    int pct = sound_calibrate (hDlg, &workprefs);
	    workprefs.sound_adjust = (pct - 1000);
	    update_soundgui (hDlg);
	} else if((wParam & 0xffff) == IDC_SOUNDDRIVE) {
	    int idx = SendDlgItemMessage (hDlg, IDC_SOUNDDRIVE, CB_GETCURSEL, 0, 0);;
	    if (idx >= 0)
		SendDlgItemMessage (hDlg, IDC_SOUNDDRIVESELECT, CB_SETCURSEL, workprefs.dfxclick[idx], 0);
	}
	values_from_sounddlg (hDlg);
	enable_for_sounddlg (hDlg);
	recursive--;
	break;

     case WM_HSCROLL:
	workprefs.sound_maxbsiz = 2048 << SendMessage( GetDlgItem( hDlg, IDC_SOUNDBUFFERRAM ), TBM_GETPOS, 0, 0 );
	workprefs.sound_adjust = SendMessage( GetDlgItem( hDlg, IDC_SOUNDADJUST ), TBM_GETPOS, 0, 0 );
	workprefs.sound_volume = 100 - SendMessage( GetDlgItem( hDlg, IDC_SOUNDVOLUME ), TBM_GETPOS, 0, 0 );
	workprefs.dfxclickvolume = 100 - SendMessage( GetDlgItem( hDlg, IDC_SOUNDDRIVEVOLUME ), TBM_GETPOS, 0, 0 );
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

static int CALLBACK VolumeSettingsProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
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

static int CALLBACK HardfileSettingsProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int recursive = 0;
    UINT setting;

    switch (msg) {
    case WM_INITDIALOG:
	recursive++;
	sethardfile (hDlg);
	recursive--;
	return TRUE;

    case WM_COMMAND:
	if (recursive)
	    break;
	recursive++;

	if (HIWORD (wParam) == BN_CLICKED) {
	    switch (LOWORD (wParam)) {
	    case IDC_CREATEHF:
		setting = CalculateHardfileSize (hDlg);
		if( !CreateHardFile(hDlg, setting) )
		{
		    char szMessage[ MAX_DPATH ];
		    char szTitle[ MAX_DPATH ];
		    WIN32GUI_LoadUIString( IDS_FAILEDHARDFILECREATION, szMessage, MAX_DPATH );
		    WIN32GUI_LoadUIString( IDS_CREATIONERROR, szTitle, MAX_DPATH );

		    MessageBox( hDlg, szMessage, szTitle,
				MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
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
		if( strlen( current_hfdlg.filename ) == 0 ) 
		{
		    char szMessage[ MAX_DPATH ];
		    char szTitle[ MAX_DPATH ];
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
	    }
	}

	GetDlgItemText (hDlg, IDC_PATH_NAME, current_hfdlg.filename, sizeof current_hfdlg.filename);
	GetDlgItemText (hDlg, IDC_PATH_FILESYS, current_hfdlg.fsfilename, sizeof current_hfdlg.fsfilename);
	GetDlgItemText (hDlg, IDC_HARDFILE_DEVICE, current_hfdlg.devicename, sizeof current_hfdlg.devicename);
	current_hfdlg.sectors   = GetDlgItemInt( hDlg, IDC_SECTORS, NULL, FALSE );
	current_hfdlg.reserved  = GetDlgItemInt( hDlg, IDC_RESERVED, NULL, FALSE );
	current_hfdlg.surfaces  = GetDlgItemInt( hDlg, IDC_HEADS, NULL, FALSE );
	current_hfdlg.blocksize = GetDlgItemInt( hDlg, IDC_BLOCKSIZE, NULL, FALSE );
	current_hfdlg.bootpri = GetDlgItemInt( hDlg, IDC_HARDFILE_BOOTPRI, NULL, TRUE );
	current_hfdlg.rw = IsDlgButtonChecked (hDlg, IDC_RW);
	recursive--;

	break;
    }
    return FALSE;
}

static int CALLBACK HarddriveSettingsProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int recursive = 0;
    int i, posn, index;

    switch (msg) {
    case WM_INITDIALOG:
	hdf_init ();
	recursive++;
	CheckDlgButton (hDlg, IDC_RW, current_hfdlg.rw);
	SendDlgItemMessage(hDlg, IDC_HARDDRIVE, CB_RESETCONTENT, 0, 0);
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

	if (HIWORD (wParam) == BN_CLICKED) {
	    switch (LOWORD (wParam)) {
	    case IDOK:
		EndDialog (hDlg, 1);
		break;
	    case IDCANCEL:
		EndDialog (hDlg, 0);
		break;
	    }
	}

        posn = SendDlgItemMessage (hDlg, IDC_HARDDRIVE, CB_GETCURSEL, 0, 0);
	if (posn != CB_ERR)
	    strcpy (current_hfdlg.filename, hdf_getnameharddrive (posn, 0));
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
		    current_fsvdlg.rootdir, ! current_fsvdlg.rw, 0, 0, 0, 0, current_fsvdlg.bootpri, 0);
    if (result)
	MessageBox (hDlg, result, "Bad directory",
		    MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
}

static void new_hardfile (HWND hDlg)
{
    const char *result;

    result = add_filesys_unit (currprefs.mountinfo, current_hfdlg.devicename, 0,
				current_hfdlg.filename, ! current_hfdlg.rw,
				current_hfdlg.sectors, current_hfdlg.surfaces,
			       current_hfdlg.reserved, current_hfdlg.blocksize,
			       current_hfdlg.bootpri, current_hfdlg.fsfilename);
    if (result)
	MessageBox (hDlg, result, "Bad hardfile",
		    MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
}

static void new_harddrive (HWND hDlg)
{
    const char *result;

    result = add_filesys_unit (currprefs.mountinfo, 0, 0,
				current_hfdlg.filename, ! current_hfdlg.rw, 0, 0,
			       0, current_hfdlg.blocksize, 0, 0);
    if (result)
	MessageBox (hDlg, result, "Bad harddrive",
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
			    &blocksize, &bootpri, &filesys);

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
				       current_hfdlg.reserved, current_hfdlg.blocksize, current_hfdlg.bootpri, current_hfdlg.fsfilename);
	    if (result)
		MessageBox (hDlg, result, "Bad hardfile",
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
				       0, current_hfdlg.blocksize, current_hfdlg.bootpri, 0);
	    if (result)
		MessageBox (hDlg, result, "Bad harddrive",
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
				       current_fsvdlg.rootdir, ! current_fsvdlg.rw, 0, 0, 0, 0, current_fsvdlg.bootpri, 0);
	    if (result)
		MessageBox (hDlg, result, "Bad hardfile",
		MB_OK | MB_ICONERROR | MB_APPLMODAL | MB_SETFOREGROUND);
	}
    }
}

static void harddiskdlg_button (HWND hDlg, int button)
{
    switch (button) {
     case IDC_NEW_FS:
	current_fsvdlg = empty_fsvdlg;
	if (DialogBox( hUIDLL ? hUIDLL : hInst, MAKEINTRESOURCE (IDD_FILESYS), hDlg, VolumeSettingsProc))
	    new_filesys (hDlg);
	break;

     case IDC_NEW_HF:
	current_hfdlg = empty_hfdlg;
	if (DialogBox( hUIDLL ? hUIDLL : hInst, MAKEINTRESOURCE (IDD_HARDFILE), hDlg, HardfileSettingsProc))
	    new_hardfile (hDlg);
	break;

     case IDC_NEW_HD:
	memset (&current_hfdlg, 0, sizeof (current_hfdlg));
	if (DialogBox( hUIDLL ? hUIDLL : hInst, MAKEINTRESOURCE (IDD_HARDDRIVE), hDlg, HarddriveSettingsProc))
	    new_harddrive (hDlg);
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
        workprefs.win32_automount_drives = IsDlgButtonChecked( hDlg, button );
        break;
    }
}

static void harddiskdlg_volume_notify (HWND hDlg, NM_LISTVIEW *nmlistview)
{
    HWND list = nmlistview->hdr.hwndFrom;
    int dblclick = 0;
    int entry = 0;

    switch (nmlistview->hdr.code) {
     case NM_DBLCLK:
	dblclick = 1;
	/* fall through */
     case NM_CLICK:
	entry = listview_entry_from_click (list, 0);
	if (entry >= 0)
	{
	    if(dblclick)
		harddisk_edit (hDlg);
	    InitializeListView( hDlg );
	    clicked_entry = entry;
	    cachedlist = list;
	    // Hilite the current selected item
	    ListView_SetItemState( cachedlist, clicked_entry, LVIS_SELECTED, LVIS_SELECTED );
	}
	break;
    }
}

static BOOL CALLBACK HarddiskDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{

    switch (msg) {
    case WM_INITDIALOG:
	clicked_entry = 0;
	pages[HARDDISK_ID] = hDlg;
	currentpage = HARDDISK_ID;
	SendMessage( GetDlgItem( hDlg, IDC_UP ), BM_SETIMAGE, (WPARAM)IMAGE_ICON, (LPARAM)hMoveUp );
	SendMessage( GetDlgItem( hDlg, IDC_DOWN ), BM_SETIMAGE, (WPARAM)IMAGE_ICON, (LPARAM)hMoveDown );
	EnableWindow (GetDlgItem(hDlg, IDC_NEW_HD), os_winnt ? TRUE : FALSE);
	
    case WM_USER:
        CheckDlgButton( hDlg, IDC_MAPDRIVES, workprefs.win32_automount_drives );
        InitializeListView( hDlg );
	break;
	
    case WM_COMMAND:
	if (HIWORD (wParam) == BN_CLICKED)
	{
	    harddiskdlg_button (hDlg, LOWORD (wParam));
	    InitializeListView( hDlg );

	    if( clicked_entry < 0 )
		clicked_entry = 0;
	    if( clicked_entry >= ListView_GetItemCount( cachedlist ) )
		clicked_entry = ListView_GetItemCount( cachedlist ) - 1;

	    if( cachedlist && clicked_entry >= 0 )
	    {
    		// Hilite the current selected item
		ListView_SetItemState( cachedlist, clicked_entry, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED );
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

HKEY read_disk_history (void)
{
    static int regread;
    char tmp2[1000];
    DWORD size2;
    int idx, idx2;
    HKEY fkey;
    char tmp[1000];
    DWORD size;

    RegCreateKeyEx(hWinUAEKey , "DiskImageMRUList", 0, NULL, REG_OPTION_NON_VOLATILE,
	KEY_ALL_ACCESS, NULL, &fkey, NULL);
    if (fkey == NULL || regread)
	return fkey;

    idx = 0;
    for (;;) {
        int err;
        size = sizeof (tmp);
        size2 = sizeof (tmp2);
        err = RegEnumValue(fkey, idx, tmp, &size, NULL, NULL, tmp2, &size2);
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

static void out_floppyspeed (HWND hDlg)
{
    char txt[30];
    if (workprefs.floppy_speed)
	sprintf (txt, "%d%%%s", workprefs.floppy_speed, workprefs.floppy_speed == 100 ? " (compatible)" : "");
    else
        strcpy (txt, "Turbo");
    SetDlgItemText (hDlg, IDC_FLOPPYSPDTEXT, txt);
}

#define BUTTONSPERFLOPPY 5
static int floppybuttons[][BUTTONSPERFLOPPY] = {
    { IDC_DF0TEXT,IDC_DF0,IDC_EJECT0,IDC_DF0TYPE,IDC_DF0WP },
    { IDC_DF1TEXT,IDC_DF1,IDC_EJECT1,IDC_DF1TYPE,IDC_DF1WP },
    { IDC_DF2TEXT,IDC_DF2,IDC_EJECT2,IDC_DF2TYPE,IDC_DF2WP },
    { IDC_DF3TEXT,IDC_DF3,IDC_EJECT3,IDC_DF3TYPE,IDC_DF3WP }
};
    
static void addfloppytype (HWND hDlg, int n)
{
    int f_text = floppybuttons[n][0];
    int f_drive = floppybuttons[n][1];
    int f_eject = floppybuttons[n][2];
    int f_type = floppybuttons[n][3];
    int f_wp = floppybuttons[n][4];
    int nn = workprefs.dfxtype[n] + 1;
    int state, i;
    char *s;
    HKEY fkey;
    char tmp[1000];

    if (nn <= 0)
	state = FALSE;
    else
	state = TRUE;
    SendDlgItemMessage (hDlg, f_type, CB_SETCURSEL, nn, 0);

    EnableWindow(GetDlgItem(hDlg, f_text), state);
    EnableWindow(GetDlgItem(hDlg, f_eject), state);
    EnableWindow(GetDlgItem(hDlg, f_drive), state);
    CheckDlgButton(hDlg, f_wp, disk_getwriteprotect (workprefs.df[n]) && state == TRUE ? BST_CHECKED : 0);
    EnableWindow(GetDlgItem(hDlg, f_wp), state && DISK_validate_filename (workprefs.df[n], 0, 0) ? TRUE : FALSE);
 
    fkey = read_disk_history ();

    SendDlgItemMessage(hDlg, f_text, CB_RESETCONTENT, 0, 0);
    SendDlgItemMessage(hDlg, f_text, WM_SETTEXT, 0, (LPARAM)workprefs.df[n]); 
    i = 0;
    while (s = DISK_history_get (i)) {
	i++;
	SendDlgItemMessage(hDlg, f_text, CB_ADDSTRING, 0, (LPARAM)s);
	if (fkey) {
	    sprintf (tmp, "Image%02d", i);
	    RegSetValueEx(fkey, tmp, 0, REG_SZ, (CONST BYTE *)s, strlen(s));
	}
	if (!strcmp (workprefs.df[n], s))
	    SendDlgItemMessage (hDlg, f_text, CB_SETCURSEL, i - 1, 0);
    }
    if (fkey)
	RegCloseKey (fkey);
}

static void getfloppytype (HWND hDlg, int n)
{
    int f_type = floppybuttons[n][3];
    int val = SendDlgItemMessage (hDlg, f_type, CB_GETCURSEL, 0, 0L);
    
    if (val != CB_ERR && workprefs.dfxtype[n] != val - 1) {
	workprefs.dfxtype[n] = val - 1;
	addfloppytype (hDlg, n);
    }
}

static void getfloppyname (HWND hDlg, int n)
{
    int val;
    int f_text = floppybuttons[n][0];
    char tmp[1000];

    tmp[0] = 0;
    val = SendDlgItemMessage (hDlg, f_text, CB_GETCURSEL, 0, 0L);
    if (val == CB_ERR) {
	SendDlgItemMessage (hDlg, f_text, WM_GETTEXT, (WPARAM)sizeof (tmp), (LPARAM)tmp);
    } else {
	char *s = DISK_history_get (val);
	if (s) {
	    strcpy (tmp, s);
	    /* add to top of list */
	    DISK_history_add (tmp, -1);
	}
    }
    disk_insert (n, tmp);
    strcpy (workprefs.df[n], tmp);
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

static BOOL CALLBACK FloppyDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int recursive = 0;
    int i;
    switch (msg) 
    {
    case WM_INITDIALOG:
    {
	char ft35dd[100];
	char ft35hd[100];
	char ft525sd[100];
	char ftdis[100];
	WIN32GUI_LoadUIString(IDS_FLOPPYTYPE35DD, ft35dd, sizeof (ft35dd));
	WIN32GUI_LoadUIString(IDS_FLOPPYTYPE35HD, ft35hd, sizeof (ft35hd));
	WIN32GUI_LoadUIString(IDS_FLOPPYTYPE525SD, ft525sd, sizeof (ft525sd));
	WIN32GUI_LoadUIString(IDS_FLOPPYTYPEDISABLED, ftdis, sizeof (ftdis));
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
        SendDlgItemMessage (hDlg, IDC_FLOPPYTYPE, CB_SETCURSEL, 0, 0);
	for (i = 0; i < 4; i++) {
	    int f_type = floppybuttons[i][3];
	    SendDlgItemMessage (hDlg, f_type, CB_RESETCONTENT, 0, 0L);
	    SendDlgItemMessage (hDlg, f_type, CB_ADDSTRING, 0, (LPARAM)ftdis);
	    SendDlgItemMessage (hDlg, f_type, CB_ADDSTRING, 0, (LPARAM)ft35dd);
	    SendDlgItemMessage (hDlg, f_type, CB_ADDSTRING, 0, (LPARAM)ft35hd);
	    SendDlgItemMessage (hDlg, f_type, CB_ADDSTRING, 0, (LPARAM)ft525sd);
	}
    }
    case WM_USER:
	recursive++;
	SetDlgItemText (hDlg, IDC_DF0TEXT, workprefs.df[0]);
	SetDlgItemText (hDlg, IDC_DF1TEXT, workprefs.df[1]);
	SetDlgItemText (hDlg, IDC_DF2TEXT, workprefs.df[2]);
	SetDlgItemText (hDlg, IDC_DF3TEXT, workprefs.df[3]);
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
	if (HIWORD (wParam) == CBN_SELCHANGE)  {
	    switch (LOWORD (wParam))
	    {
		case IDC_DF0TEXT:
		getfloppyname (hDlg, 0);
		addfloppytype (hDlg, 0);
		break;
		case IDC_DF1TEXT:
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
	    floppysetwriteprotect (hDlg, 0, IsDlgButtonChecked (hDlg, IDC_DF0WP));
	    break;
	case IDC_DF1WP:
	    floppysetwriteprotect (hDlg, 1, IsDlgButtonChecked (hDlg, IDC_DF1WP));
	    break;
	case IDC_DF2WP:
	    floppysetwriteprotect (hDlg, 2, IsDlgButtonChecked (hDlg, IDC_DF2WP));
	    break;
	case IDC_DF3WP:
	    floppysetwriteprotect (hDlg, 3, IsDlgButtonChecked (hDlg, IDC_DF3WP));
	    break;
	case IDC_DF0:
	    DiskSelection (hDlg, wParam, 0, &workprefs, 0);
	    disk_insert (0, workprefs.df[0]);
	    addfloppytype (hDlg, 0);
	    break;
	case IDC_DF1:
	    DiskSelection (hDlg, wParam, 0, &workprefs, 0);
	    disk_insert (1, workprefs.df[1]);
	    addfloppytype (hDlg, 1);
	    break;
	case IDC_DF2:
	    DiskSelection (hDlg, wParam, 0, &workprefs, 0);
	    disk_insert (2, workprefs.df[2]);
	    addfloppytype (hDlg, 2);
	    break;
	case IDC_DF3:
	    DiskSelection (hDlg, wParam, 0, &workprefs, 0);
	    disk_insert (3, workprefs.df[3]);
	    addfloppytype (hDlg, 3);
	    break;
	case IDC_EJECT0:
	    disk_eject(0);
	    SetDlgItemText (hDlg, IDC_DF0TEXT, "");
	    workprefs.df[0][0] = 0;
	    addfloppytype (hDlg, 0);
	    break;
	case IDC_EJECT1:
	    disk_eject(1);
	    SetDlgItemText (hDlg, IDC_DF1TEXT, "");
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
	case IDC_CREATE:
	    DiskSelection (hDlg, wParam, 1, &workprefs, 0);
	    break;
	case IDC_CREATE_RAW:
	    DiskSelection( hDlg, wParam, 1, &workprefs, 0);
	    break;
	}
	recursive--;
	break;

    case WM_HSCROLL:
	workprefs.floppy_speed = SendMessage( GetDlgItem( hDlg, IDC_FLOPPYSPD ), TBM_GETPOS, 0, 0 );
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

static BOOL CALLBACK DiskDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int recursive = 0;
    static int entry;
    char tmp[MAX_DPATH];

    switch (msg) 
    {
    case WM_INITDIALOG:
	pages[DISK_ID] = hDlg;
	currentpage = DISK_ID;
	InitializeListView(hDlg);
	SendMessage( GetDlgItem( hDlg, IDC_UP ), BM_SETIMAGE, (WPARAM)IMAGE_ICON, (LPARAM)hMoveUp );
	SendMessage( GetDlgItem( hDlg, IDC_DOWN ), BM_SETIMAGE, (WPARAM)IMAGE_ICON, (LPARAM)hMoveDown );
	entry = -1;
    break;
    case WM_COMMAND:
    {
	switch (wParam)
	{
	    case IDC_DISKLISTREMOVE:
		if (entry >= 0) {
		    workprefs.dfxlist[entry][0] = 0;
		    InitializeListView (hDlg);
		}
		break;
	    case IDC_UP:
		if (entry > 0) {
		    strcpy (tmp, workprefs.dfxlist[entry - 1]);
		    strcpy (workprefs.dfxlist[entry - 1], workprefs.dfxlist[entry]);
		    strcpy (workprefs.dfxlist[entry], tmp);
		    InitializeListView (hDlg);
		    entry--;
		    ListView_SetItemState (cachedlist, entry,
			LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		}
		break;
	    case IDC_DOWN:
		if (entry >= 0 && entry < MAX_SPARE_DRIVES - 1) {
		    strcpy (tmp, workprefs.dfxlist[entry + 1]);
		    strcpy (workprefs.dfxlist[entry + 1], workprefs.dfxlist[entry]);
		    strcpy (workprefs.dfxlist[entry], tmp);
		    InitializeListView (hDlg);
		    entry++;
		    ListView_SetItemState (cachedlist, entry,
			LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		}
	        break;
	}
	break;
    }
    case WM_NOTIFY:
        if (((LPNMHDR) lParam)->idFrom == IDC_DISKLIST) 
        {
	    int dblclick = 0, col;
	    HWND list;
	    NM_LISTVIEW *nmlistview;
	    nmlistview = (NM_LISTVIEW *) lParam;
	    cachedlist = list = nmlistview->hdr.hwndFrom;
	    switch (nmlistview->hdr.code) 
	    {
		case NM_DBLCLK:
		dblclick = 1;
		/* fall-through */
		case NM_CLICK:
		entry = listview_entry_from_click (list, &col);
		if (entry >= 0) {
		    if (col == 2) {
			if (disk_swap (entry, col))
			    InitializeListView (hDlg);
		    } else if (col == 1) {
			char path[MAX_DPATH];
			if (dblclick && DiskSelection (hDlg, -1, 0, &changed_prefs, path)) {
			    strcpy (workprefs.dfxlist[entry], path);
			    InitializeListView (hDlg);
			}
		    }
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

static int joy0idc[] = {
    IDC_PORT0_JOY0, IDC_PORT0_JOY1, IDC_PORT0_MOUSE, IDC_PORT0_KBDA, IDC_PORT0_KBDB, IDC_PORT0_KBDC
};

static int joy1idc[] = {
    IDC_PORT1_JOY0, IDC_PORT1_JOY1, IDC_PORT1_MOUSE, IDC_PORT1_KBDA, IDC_PORT1_KBDB, IDC_PORT1_KBDC
};

static BOOL bNoMidiIn = FALSE;

static void enable_for_portsdlg( HWND hDlg )
{
    int i, v;

    v = workprefs.input_selected_setting > 0 ? FALSE : TRUE;
    for( i = 0; i < 6; i++ )
    {
        EnableWindow( GetDlgItem( hDlg, joy0idc[i] ), v );
        EnableWindow( GetDlgItem( hDlg, joy1idc[i] ), v );
    }
    EnableWindow (GetDlgItem (hDlg, IDC_SWAP), v);
#if !defined (SERIAL_PORT)
    EnableWindow( GetDlgItem( hDlg, IDC_MIDIOUTLIST), FALSE );
    EnableWindow( GetDlgItem( hDlg, IDC_MIDIINLIST), FALSE );
    EnableWindow( GetDlgItem( hDlg, IDC_SHARED), FALSE );
    EnableWindow( GetDlgItem( hDlg, IDC_SER_CTSRTS), FALSE );
    EnableWindow( GetDlgItem( hDlg, IDC_SERIAL_DIRECT), FALSE );
    EnableWindow( GetDlgItem( hDlg, IDC_SERIAL), FALSE );
#endif
#if !defined (PARALLEL_PORT)
    EnableWindow( GetDlgItem( hDlg, IDC_PRINTERLIST), FALSE );
#endif
}

static void UpdatePortRadioButtons( HWND hDlg )
{
    int which_button1, which_button2;

    enable_for_portsdlg( hDlg );
	which_button1 = joy0idc[workprefs.jport0];
	if (CheckRadioButton (hDlg, IDC_PORT0_JOY0, IDC_PORT0_KBDC, which_button1) == 0)
	    which_button1 = 0;
    else
    {
        EnableWindow( GetDlgItem( hDlg, joy1idc[workprefs.jport0] ), FALSE );
    }
	which_button2 = joy1idc[workprefs.jport1];
    if( workprefs.jport1 == workprefs.jport0 )
    {
        if( which_button2 == IDC_PORT1_KBDC )
            which_button2 = IDC_PORT1_KBDB;
        else
            which_button2++;
    }
	if (CheckRadioButton (hDlg, IDC_PORT1_JOY0, IDC_PORT1_KBDC, which_button2) == 0)
	    which_button2 = 0;
    else
    {
        EnableWindow( GetDlgItem( hDlg, joy0idc[ workprefs.jport1 ] ), FALSE );
    }
}

static void values_from_portsdlg (HWND hDlg)
{
    int item;
    char tmp[256];
    /* 0 - joystick 0
     * 1 - joystick 1
     * 2 - mouse
     * 3 - numpad
     * 4 - cursor keys
     * 5 - elsewhere
     */
    if (IsDlgButtonChecked (hDlg, IDC_PORT0_JOY0)) {
	    workprefs.jport0 = 0;
    }
    if (IsDlgButtonChecked (hDlg, IDC_PORT0_JOY1)) {
	    workprefs.jport0 = 1;
    }
    if (IsDlgButtonChecked (hDlg, IDC_PORT0_MOUSE))
	    workprefs.jport0 = 2;
    if (IsDlgButtonChecked (hDlg, IDC_PORT0_KBDA))
	    workprefs.jport0 = 3;
    if (IsDlgButtonChecked (hDlg, IDC_PORT0_KBDB))
	    workprefs.jport0 = 4;
    if (IsDlgButtonChecked (hDlg, IDC_PORT0_KBDC))
	    workprefs.jport0 = 5;

    if (IsDlgButtonChecked (hDlg, IDC_PORT1_JOY0)) {
	    workprefs.jport1 = 0;
    }
    if (IsDlgButtonChecked (hDlg, IDC_PORT1_JOY1)) {
	    workprefs.jport1 = 1;
    }
    if (IsDlgButtonChecked (hDlg, IDC_PORT1_MOUSE))
	    workprefs.jport1 = 2;
    if (IsDlgButtonChecked (hDlg, IDC_PORT1_KBDA))
	    workprefs.jport1 = 3;
    if (IsDlgButtonChecked (hDlg, IDC_PORT1_KBDB))
	    workprefs.jport1 = 4;
    if (IsDlgButtonChecked (hDlg, IDC_PORT1_KBDC))
	    workprefs.jport1 = 5;

    item = SendDlgItemMessage( hDlg, IDC_PRINTERLIST, CB_GETCURSEL, 0, 0L );
    if( item != CB_ERR )
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

    workprefs.win32_midioutdev = SendDlgItemMessage( hDlg, IDC_MIDIOUTLIST, CB_GETCURSEL, 0, 0 );
    workprefs.win32_midioutdev--; /* selection zero is always 'default midi device', so we make it -1 */

    if( bNoMidiIn )
    {
	workprefs.win32_midiindev = -1;
    }
    else
    {
	workprefs.win32_midiindev = SendDlgItemMessage( hDlg, IDC_MIDIINLIST, CB_GETCURSEL, 0, 0 );
    }

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
	    EnableWindow( GetDlgItem( hDlg, IDC_MIDIOUTLIST ), TRUE );
	    EnableWindow( GetDlgItem( hDlg, IDC_MIDIINLIST ), TRUE );
	break;

	default:
	    workprefs.use_serial = 0;
	    strcpy( workprefs.sername, "none" );
	    EnableWindow( GetDlgItem( hDlg, IDC_MIDIOUTLIST ), FALSE );
	    EnableWindow( GetDlgItem( hDlg, IDC_MIDIINLIST ), FALSE );
	break;
    }
    workprefs.serial_demand = 0;
    if( IsDlgButtonChecked( hDlg, IDC_SHARED ) )
        workprefs.serial_demand = 1;
    workprefs.serial_hwctsrts = 0;
    if( IsDlgButtonChecked( hDlg, IDC_SER_CTSRTS ) )
        workprefs.serial_hwctsrts = 1;
    workprefs.serial_direct = 0;
    if( IsDlgButtonChecked( hDlg, IDC_SERIAL_DIRECT ) )
        workprefs.serial_direct = 1;
}

static void values_to_portsdlg (HWND hDlg)
{
    LONG item_height, result = 0;
    RECT rect;

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
	    gui_message( szMessage );
	    
	    // Disable the invalid parallel-port selection
	    strcpy( workprefs.prtname, "none" );

	    result = 0;
	}
    }
    SendDlgItemMessage( hDlg, IDC_PRINTERLIST, CB_SETCURSEL, result, 0 );
    SendDlgItemMessage( hDlg, IDC_MIDIOUTLIST, CB_SETCURSEL, workprefs.win32_midioutdev + 1, 0 ); /* we +1 here because 1st entry is 'default' */
    if( !bNoMidiIn && ( workprefs.win32_midiindev >= 0 ) )
	SendDlgItemMessage( hDlg, IDC_MIDIINLIST, CB_SETCURSEL, workprefs.win32_midiindev, 0 );
    else
	SendDlgItemMessage( hDlg, IDC_MIDIINLIST, CB_SETCURSEL, 0, 0 );
    
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
	int i, result = -1;
	for (i = 0; i < MAX_SERIALS; i++) {
	    if (!strcmp (comports[i], workprefs.sername)) {
	        result = SendDlgItemMessage( hDlg, IDC_SERIAL, CB_SETCURSEL, i + 1, 0L );
		break;
	    }
	}
	if( result < 0 )
	{
	    if (t > 0) {
		// Warn the user that their COM-port selection is not valid on this machine
		char szMessage[ MAX_DPATH ];
		WIN32GUI_LoadUIString( IDS_INVALIDCOMPORT, szMessage, MAX_DPATH );
		gui_message( szMessage );

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

    if( workprefs.use_serial )
    {
	EnableWindow( GetDlgItem( hDlg, IDC_MIDIOUTLIST ), TRUE );
	if( !bNoMidiIn )
	    EnableWindow( GetDlgItem( hDlg, IDC_MIDIINLIST ), TRUE );
    }
    else
    {
	EnableWindow( GetDlgItem( hDlg, IDC_MIDIOUTLIST ), FALSE );
	EnableWindow( GetDlgItem( hDlg, IDC_MIDIINLIST ), FALSE );
    }
    /* Retrieve the height, in pixels, of a list item. */
    item_height = SendDlgItemMessage (hDlg, IDC_SERIAL, CB_GETITEMHEIGHT, 0, 0L);
    if (item_height != CB_ERR) {
	/* Get actual box position and size. */
	GetWindowRect (GetDlgItem (hDlg, IDC_SERIAL), &rect);
	rect.bottom = (rect.top + item_height * 5
	    + SendDlgItemMessage (hDlg, IDC_SERIAL, CB_GETITEMHEIGHT, (WPARAM) - 1, 0L)
	    + item_height);
	SetWindowPos (GetDlgItem (hDlg, IDC_SERIAL), 0, 0, 0, rect.right - rect.left,
	    rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
    }
}

static void init_portsdlg( HWND hDlg )
{
    int port, portcnt, numdevs;
    COMMCONFIG cc;
    DWORD size = sizeof(COMMCONFIG);

    MIDIOUTCAPS midiOutCaps;
    MIDIINCAPS midiInCaps;

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

    if( ( numdevs = midiOutGetNumDevs() ) == 0 )
    {
	EnableWindow( GetDlgItem( hDlg, IDC_MIDIOUTLIST ), FALSE );
    }
    else
    {
	char szMidiOut[ MAX_DPATH ];
	WIN32GUI_LoadUIString( IDS_DEFAULTMIDIOUT, szMidiOut, MAX_DPATH );
        SendDlgItemMessage( hDlg, IDC_MIDIOUTLIST, CB_RESETCONTENT, 0, 0L );
        SendDlgItemMessage( hDlg, IDC_MIDIOUTLIST, CB_ADDSTRING, 0, (LPARAM)szMidiOut );

        for( port = 0; port < numdevs; port++ )
        {
            if( midiOutGetDevCaps( port, &midiOutCaps, sizeof( midiOutCaps ) ) == MMSYSERR_NOERROR )
            {
                SendDlgItemMessage( hDlg, IDC_MIDIOUTLIST, CB_ADDSTRING, 0, (LPARAM)midiOutCaps.szPname );
            }
        }
    }

    if( ( numdevs = midiInGetNumDevs() ) == 0 )
    {
	EnableWindow( GetDlgItem( hDlg, IDC_MIDIINLIST ), FALSE );
	bNoMidiIn = TRUE;
    }
    else
    {
        SendDlgItemMessage( hDlg, IDC_MIDIINLIST, CB_RESETCONTENT, 0, 0L );

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
static BOOL CALLBACK PortsDlgProc (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int recursive = 0;
    int temp;

    switch (msg) 
    {
    case WM_INITDIALOG:
	pages[PORTS_ID] = hDlg;
	currentpage = PORTS_ID;
	init_portsdlg( hDlg );
	enable_for_portsdlg( hDlg );
	values_to_portsdlg ( hDlg);
	UpdatePortRadioButtons( hDlg );
	break;	    
    case WM_USER:
	recursive++;
	enable_for_portsdlg( hDlg );
	UpdatePortRadioButtons( hDlg );
	recursive--;
	return TRUE;

    case WM_COMMAND:
        if (recursive > 0)
	    break;
	recursive++;
	if( wParam == IDC_SWAP )
	{
	    temp = workprefs.jport0;
	    workprefs.jport0 = workprefs.jport1;
	    workprefs.jport1 = temp;
	    UpdatePortRadioButtons( hDlg );
	}
	else
	{
	    values_from_portsdlg (hDlg);
	    UpdatePortRadioButtons( hDlg );
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
    SendDlgItemMessage( hDlg, IDC_INPUTTYPE, CB_SETCURSEL, workprefs.input_selected_setting, 0 );
    SendDlgItemMessage( hDlg, IDC_INPUTDEVICE, CB_SETCURSEL, input_selected_device, 0 );
    SetDlgItemInt( hDlg, IDC_INPUTDEADZONE, workprefs.input_joystick_deadzone, FALSE );
    SetDlgItemInt( hDlg, IDC_INPUTAUTOFIRERATE, workprefs.input_autofire_framecnt, FALSE );
    SetDlgItemInt( hDlg, IDC_INPUTSPEEDD, workprefs.input_joymouse_speed, FALSE );
    SetDlgItemInt( hDlg, IDC_INPUTSPEEDA, workprefs.input_joymouse_multiplier, FALSE );
    SetDlgItemInt( hDlg, IDC_INPUTSPEEDM, workprefs.input_mouse_speed, FALSE );
    CheckDlgButton ( hDlg, IDC_INPUTDEVICEDISABLE, inputdevice_get_device_status (input_selected_device) ? BST_CHECKED : BST_UNCHECKED);
}

static void init_inputdlg_2( HWND hDlg )
{
    char name1[256], name2[256];
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
	inputdevice_get_mapped_name (input_selected_device, input_selected_widget, 0, name1, input_selected_sub_num);
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
    int item, doselect = 0, v;
    BOOL success;

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
	input_selected_sub_num = item;
	doselect = 0;
        init_inputdlg_2 (hDlg);
	update_listview_input (hDlg);
	return;
    }

    item = SendDlgItemMessage( hDlg, IDC_INPUTTYPE, CB_GETCURSEL, 0, 0L );
    if( item != CB_ERR ) {
	if (item != workprefs.input_selected_setting) {
	    workprefs.input_selected_setting = item;
	    input_selected_widget = -1;
	    inputdevice_updateconfig (&workprefs);
	    enable_for_inputdlg( hDlg );
	    InitializeListView (hDlg);
	    doselect = 1;
	}
    }
    item = SendDlgItemMessage( hDlg, IDC_INPUTDEVICE, CB_GETCURSEL, 0, 0L );
    if( item != CB_ERR ) {
	if (item != input_selected_device) {
	    input_selected_device = item;
	    input_selected_widget = -1;
	    input_selected_event = -1;
	    InitializeListView (hDlg);
	    init_inputdlg_2 (hDlg);
	    values_to_inputdlg (hDlg);
	    doselect = 1;
	}
    }
    item = SendDlgItemMessage( hDlg, IDC_INPUTAMIGA, CB_GETCURSEL, 0, 0L );
    if( item != CB_ERR) {
	input_selected_event = item;
	doselect = 1;
    }

    if (doselect && input_selected_device >= 0 && input_selected_event >= 0) {
	int flags;
	inputdevice_get_mapped_name (input_selected_device, input_selected_widget,
	    &flags, 0, input_selected_sub_num);
	inputdevice_set_mapping (input_selected_device, input_selected_widget,
	    eventnames[input_selected_event], (flags & IDEV_MAPPED_AUTOFIRE_SET) ? 1 : 0,
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
    int src = SendDlgItemMessage( hDlg, IDC_INPUTCOPYFROM, CB_GETCURSEL, 0, 0L );
    if (src == CB_ERR)
	return;
    inputdevice_copy_single_config (&workprefs, src, workprefs.input_selected_setting, input_selected_device);
    init_inputdlg (hDlg);
}

static void input_toggleautofire (void)
{
    int af, flags, event;
    char name[256];
    if (input_selected_device < 0 || input_selected_widget < 0)
	return;
    event = inputdevice_get_mapped_name (input_selected_device, input_selected_widget,
	&flags, name, input_selected_sub_num);
    if (event <= 0)
	return;
    af = (flags & IDEV_MAPPED_AUTOFIRE_SET) ? 0 : 1;
    inputdevice_set_mapping (input_selected_device, input_selected_widget,
	name, af, input_selected_sub_num);
}

static BOOL CALLBACK InputDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
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
	init_inputdlg( hDlg );
	    
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

static int scanlineratios[] = { 1,1,1,2,1,3, 2,1,2,2,2,3, 3,1,3,2,3,3, 0,0 };
static int scanlineindexes[100];

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
    EnableWindow (GetDlgItem (hDlg, IDC_OPENGLENABLE), TRUE);
    EnableWindow (GetDlgItem (hDlg, IDC_OPENGLBITS), v);
    CheckDlgButton( hDlg, IDC_OPENGLENABLE, v );
    EnableWindow (GetDlgItem (hDlg, IDC_OPENGLHZ), vv2);
    EnableWindow (GetDlgItem (hDlg, IDC_OPENGLVZ), vv2);
    EnableWindow (GetDlgItem (hDlg, IDC_OPENGLHO), v);
    EnableWindow (GetDlgItem (hDlg, IDC_OPENGLVO), v);
    EnableWindow (GetDlgItem (hDlg, IDC_OPENGLSLR), vv2);
    EnableWindow (GetDlgItem (hDlg, IDC_OPENGLSL), vv2);
    EnableWindow (GetDlgItem (hDlg, IDC_OPENGLSL2), vv2);
    EnableWindow (GetDlgItem (hDlg, IDC_OPENGLDEFAULT), v);
    EnableWindow (GetDlgItem (hDlg, IDC_OPENGLFILTER), vv);
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

static void values_to_hw3ddlg (HWND hDlg)
{
    char txt[100], tmp[100];
    int i, j, nofilter, fltnum, modenum;
    struct uae_filter *uf;

    SendDlgItemMessage( hDlg, IDC_OPENGLHZ, TBM_SETRANGE, TRUE, MAKELONG (-99, +99) );
    SendDlgItemMessage( hDlg, IDC_OPENGLHZ, TBM_SETPAGESIZE, 0, 1 );
    SendDlgItemMessage( hDlg, IDC_OPENGLVZ, TBM_SETRANGE, TRUE, MAKELONG (-99, +99) );
    SendDlgItemMessage( hDlg, IDC_OPENGLVZ, TBM_SETPAGESIZE, 0, 1 );
    SendDlgItemMessage( hDlg, IDC_OPENGLHO, TBM_SETRANGE, TRUE, MAKELONG (-99, +99) );
    SendDlgItemMessage( hDlg, IDC_OPENGLHO, TBM_SETPAGESIZE, 0, 1 );
    SendDlgItemMessage( hDlg, IDC_OPENGLVO, TBM_SETRANGE, TRUE, MAKELONG (-50, +50) );
    SendDlgItemMessage( hDlg, IDC_OPENGLVO, TBM_SETPAGESIZE, 0, 1 );
    SendDlgItemMessage( hDlg, IDC_OPENGLSL, TBM_SETRANGE, TRUE, MAKELONG (   0, +100) );
    SendDlgItemMessage( hDlg, IDC_OPENGLSL, TBM_SETPAGESIZE, 0, 10 );
    SendDlgItemMessage( hDlg, IDC_OPENGLSL2, TBM_SETRANGE, TRUE, MAKELONG (   0, +100) );
    SendDlgItemMessage( hDlg, IDC_OPENGLSL2, TBM_SETPAGESIZE, 0, 10 );

    SendDlgItemMessage (hDlg, IDC_OPENGLBITS, CB_RESETCONTENT, 0, 0L);

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
    	    SendDlgItemMessage (hDlg, IDC_OPENGLBITS, CB_ADDSTRING, 0, (LPARAM)uaefilters[i].name);
	    if (uaefilters[i].type == workprefs.gfx_filter) {
		uf = &uaefilters[i];
		fltnum = j;
	    }
	    j++;
	}
	i++;
    }
    SendDlgItemMessage( hDlg, IDC_OPENGLBITS, CB_SETCURSEL, fltnum, 0 );

    SendDlgItemMessage (hDlg, IDC_OPENGLFILTER, CB_RESETCONTENT, 0, 0L);
    if (uf->x[0]) {
	WIN32GUI_LoadUIString (IDS_3D_NO_FILTER, txt, sizeof (txt));
	sprintf (tmp, txt, 16);
        SendDlgItemMessage (hDlg, IDC_OPENGLFILTER, CB_ADDSTRING, 0, (LPARAM)tmp);
	WIN32GUI_LoadUIString (IDS_3D_BILINEAR, txt, sizeof (txt));
	sprintf (tmp, txt, 16);
	SendDlgItemMessage (hDlg, IDC_OPENGLFILTER, CB_ADDSTRING, 0, (LPARAM)tmp);
	WIN32GUI_LoadUIString (IDS_3D_NO_FILTER, txt, sizeof (txt));
	sprintf (tmp, txt, 32);
        SendDlgItemMessage (hDlg, IDC_OPENGLFILTER, CB_ADDSTRING, 0, (LPARAM)tmp);
	WIN32GUI_LoadUIString (IDS_3D_BILINEAR, txt, sizeof (txt));
	sprintf (tmp, txt, 32);
	SendDlgItemMessage (hDlg, IDC_OPENGLFILTER, CB_ADDSTRING, 0, (LPARAM)tmp);
	modenum = 4;
    } else {
	workprefs.gfx_filter_horiz_zoom = 0;
	workprefs.gfx_filter_vert_zoom = 0;
	modenum = 0;
	for (i = 1; i <= 4; i++) {
	    if (uf->x[i]) {
		makefilter (tmp, i, uf->x[i]);
		SendDlgItemMessage (hDlg, IDC_OPENGLFILTER, CB_ADDSTRING, 0, (LPARAM)tmp);
		modenum++;
	    }
	}
    }
    if (workprefs.gfx_filter_filtermode >= modenum)
	workprefs.gfx_filter_filtermode = 0;
    SendDlgItemMessage (hDlg, IDC_OPENGLFILTER, CB_SETCURSEL, workprefs.gfx_filter_filtermode, 0);

    SendDlgItemMessage (hDlg, IDC_OPENGLSLR, CB_RESETCONTENT, 0, 0L);
    i = j = 0;
    while (scanlineratios[i * 2]) {
	int sl = scanlineratios[i * 2] * 16 + scanlineratios[i * 2 + 1];
	sprintf (txt, "%d:%d", scanlineratios[i * 2], scanlineratios[i * 2 + 1]);
	if (workprefs.gfx_filter_scanlineratio == sl)
	    j = i;
        SendDlgItemMessage (hDlg, IDC_OPENGLSLR, CB_ADDSTRING, 0, (LPARAM)txt);
	scanlineindexes[i] = sl;
	i++;
    }
    SendDlgItemMessage( hDlg, IDC_OPENGLSLR, CB_SETCURSEL, j, 0 );

    SendDlgItemMessage( hDlg, IDC_OPENGLHZ, TBM_SETPOS, TRUE, workprefs.gfx_filter_horiz_zoom);
    SendDlgItemMessage( hDlg, IDC_OPENGLVZ, TBM_SETPOS, TRUE, workprefs.gfx_filter_vert_zoom);
    SendDlgItemMessage( hDlg, IDC_OPENGLHO, TBM_SETPOS, TRUE, workprefs.gfx_filter_horiz_offset);
    SendDlgItemMessage( hDlg, IDC_OPENGLVO, TBM_SETPOS, TRUE, workprefs.gfx_filter_vert_offset);
    SendDlgItemMessage( hDlg, IDC_OPENGLSL, TBM_SETPOS, TRUE, workprefs.gfx_filter_scanlines);
    SendDlgItemMessage( hDlg, IDC_OPENGLSL2, TBM_SETPOS, TRUE, workprefs.gfx_filter_scanlinelevel);
    SetDlgItemInt( hDlg, IDC_OPENGLHZV, workprefs.gfx_filter_horiz_zoom, TRUE );
    SetDlgItemInt( hDlg, IDC_OPENGLVZV, workprefs.gfx_filter_vert_zoom, TRUE );
    SetDlgItemInt( hDlg, IDC_OPENGLHOV, workprefs.gfx_filter_horiz_offset, TRUE );
    SetDlgItemInt( hDlg, IDC_OPENGLVOV, workprefs.gfx_filter_vert_offset, TRUE );
    SetDlgItemInt( hDlg, IDC_OPENGLSLV, workprefs.gfx_filter_scanlines, TRUE );
    SetDlgItemInt( hDlg, IDC_OPENGLSL2V, workprefs.gfx_filter_scanlinelevel, TRUE );
}

static void values_from_hw3ddlg (HWND hDlg)
{
}

static BOOL CALLBACK hw3dDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int recursive;
    int item;

    switch (msg) 
    {
    case WM_INITDIALOG:
	pages[HW3D_ID] = hDlg;
	currentpage = HW3D_ID;
	enable_for_hw3ddlg (hDlg);
	    
    case WM_USER:
	recursive++;
	enable_for_hw3ddlg( hDlg );
	values_to_hw3ddlg (hDlg);
	recursive--;
	return TRUE;
    case WM_COMMAND:
	if (wParam == IDC_OPENGLDEFAULT) {
	    currprefs.gfx_filter_horiz_zoom = workprefs.gfx_filter_horiz_zoom = 0;
	    currprefs.gfx_filter_vert_zoom = workprefs.gfx_filter_vert_zoom = 0;
	    currprefs.gfx_filter_horiz_offset = workprefs.gfx_filter_horiz_offset = 0;
	    currprefs.gfx_filter_vert_offset = workprefs.gfx_filter_vert_offset = 0;
	    values_to_hw3ddlg (hDlg);
	}
	item = SendDlgItemMessage( hDlg, IDC_OPENGLSLR, CB_GETCURSEL, 0, 0L );
	if (item != CB_ERR)
	    currprefs.gfx_filter_scanlineratio = workprefs.gfx_filter_scanlineratio = scanlineindexes[item];
	item = SendDlgItemMessage( hDlg, IDC_OPENGLMODE, CB_GETCURSEL, 0, 0L );
	if (item != CB_ERR) {
	    int of = workprefs.gfx_filter;
	    int off = workprefs.gfx_filter_filtermode;
	    workprefs.gfx_filter = 0;
	    if (IsDlgButtonChecked( hDlg, IDC_OPENGLENABLE)) {
		workprefs.gfx_filter = uaefilters[item].type;
	        item = SendDlgItemMessage( hDlg, IDC_OPENGLFILTER, CB_GETCURSEL, 0, 0L );
	        if (item != CB_ERR)
		    workprefs.gfx_filter_filtermode = item;
		if (of != workprefs.gfx_filter || off != workprefs.gfx_filter_filtermode) {
		    values_to_hw3ddlg (hDlg);
		    enable_for_hw3ddlg (hDlg);
		    hw3d_changed = 1;
		}
	    } else {
		enable_for_hw3ddlg (hDlg);
	    }
	}
	updatedisplayarea ();
	break;
    case WM_HSCROLL:
	currprefs.gfx_filter_horiz_zoom = workprefs.gfx_filter_horiz_zoom = SendMessage( GetDlgItem( hDlg, IDC_OPENGLHZ ), TBM_GETPOS, 0, 0 );
	currprefs.gfx_filter_vert_zoom = workprefs.gfx_filter_vert_zoom = SendMessage( GetDlgItem( hDlg, IDC_OPENGLVZ ), TBM_GETPOS, 0, 0 );
	currprefs.gfx_filter_horiz_offset = workprefs.gfx_filter_horiz_offset = SendMessage( GetDlgItem( hDlg, IDC_OPENGLHO ), TBM_GETPOS, 0, 0 );
	currprefs.gfx_filter_vert_offset = workprefs.gfx_filter_vert_offset = SendMessage( GetDlgItem( hDlg, IDC_OPENGLVO ), TBM_GETPOS, 0, 0 );
	currprefs.gfx_filter_scanlines = workprefs.gfx_filter_scanlines = SendMessage( GetDlgItem( hDlg, IDC_OPENGLSL ), TBM_GETPOS, 0, 0 );
	currprefs.gfx_filter_scanlinelevel = workprefs.gfx_filter_scanlinelevel = SendMessage( GetDlgItem( hDlg, IDC_OPENGLSL2 ), TBM_GETPOS, 0, 0 );
	SetDlgItemInt( hDlg, IDC_OPENGLHZV, workprefs.gfx_filter_horiz_zoom, TRUE );
	SetDlgItemInt( hDlg, IDC_OPENGLVZV, workprefs.gfx_filter_vert_zoom, TRUE );
	SetDlgItemInt( hDlg, IDC_OPENGLHOV, workprefs.gfx_filter_horiz_offset, TRUE );
        SetDlgItemInt( hDlg, IDC_OPENGLVOV, workprefs.gfx_filter_vert_offset, TRUE );
        SetDlgItemInt( hDlg, IDC_OPENGLSLV, workprefs.gfx_filter_scanlines, TRUE );
	SetDlgItemInt( hDlg, IDC_OPENGLSL2V, workprefs.gfx_filter_scanlinelevel, TRUE );
	updatedisplayarea ();
	WIN32GFX_WindowMove ();
	break;
    }
    return FALSE;
}

#ifdef AVIOUTPUT
static void values_to_avioutputdlg(HWND hDlg)
{
	char tmpstr[256];
	
        updatewinfsmode (&workprefs);
	SetDlgItemText(hDlg, IDC_AVIOUTPUT_FILETEXT, avioutput_filename);
	
	sprintf(tmpstr, "%d fps", avioutput_fps);
	SendMessage(GetDlgItem(hDlg, IDC_AVIOUTPUT_FPS_STATIC), WM_SETTEXT, (WPARAM) 0, (LPARAM) tmpstr);
	
	sprintf(tmpstr, "Actual: %d x %d", workprefs.gfx_width, workprefs.gfx_height);
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
}

static void values_from_avioutputdlg(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	int tmp;

        updatewinfsmode (&workprefs);
	tmp = SendMessage(GetDlgItem(hDlg, IDC_AVIOUTPUT_FPS), TBM_GETPOS, 0, 0);
	if (tmp < 1)
	    tmp = 1;
	if (tmp != avioutput_fps) {
	    avioutput_fps = tmp;
	    AVIOutput_Restart ();
	}
	avioutput_framelimiter = IsDlgButtonChecked (hDlg, IDC_AVIOUTPUT_FRAMELIMITER) ? 0 : 1;
}

static char aviout_videoc[200], aviout_audioc[200];

static void enable_for_avioutputdlg(HWND hDlg)
{
#if defined (PROWIZARD)
	EnableWindow( GetDlgItem( hDlg, IDC_PROWIZARD ), TRUE );
#endif
	if (full_property_sheet)
	    EnableWindow( GetDlgItem( hDlg, IDC_PROWIZARD ), FALSE );

	EnableWindow(GetDlgItem(hDlg, IDC_SCREENSHOT), full_property_sheet ? FALSE : TRUE);

	EnableWindow(GetDlgItem(hDlg, IDC_AVIOUTPUT_PAL), TRUE);
	EnableWindow(GetDlgItem(hDlg, IDC_AVIOUTPUT_NTSC), TRUE);
	EnableWindow(GetDlgItem(hDlg, IDC_AVIOUTPUT_FPS), TRUE);
	EnableWindow(GetDlgItem(hDlg, IDC_AVIOUTPUT_FILE), TRUE);
        CheckDlgButton (hDlg, IDC_AVIOUTPUT_FRAMELIMITER, avioutput_framelimiter ? FALSE : TRUE);
		
	if(workprefs.produce_sound < 2)
	{
		EnableWindow(GetDlgItem(hDlg, IDC_AVIOUTPUT_AUDIO), FALSE);
		EnableWindow(GetDlgItem(hDlg, IDC_AVIOUTPUT_AUDIO_STATIC), FALSE);
		avioutput_audio = 0;
	}
	else
	{
		EnableWindow(GetDlgItem(hDlg, IDC_AVIOUTPUT_AUDIO), TRUE);
		EnableWindow(GetDlgItem(hDlg, IDC_AVIOUTPUT_AUDIO_STATIC), TRUE);
		
	}
	
	if(!avioutput_audio)
	{
		CheckDlgButton(hDlg, IDC_AVIOUTPUT_AUDIO, BST_UNCHECKED);
		WIN32GUI_LoadUIString (IDS_AVIOUTPUT_NOCODEC, aviout_audioc, sizeof (aviout_audioc));
	}
	SetWindowText(GetDlgItem(hDlg, IDC_AVIOUTPUT_AUDIO_STATIC), aviout_audioc);
	
	if(!avioutput_video)
	{
		CheckDlgButton(hDlg, IDC_AVIOUTPUT_VIDEO, BST_UNCHECKED);
		WIN32GUI_LoadUIString (IDS_AVIOUTPUT_NOCODEC, aviout_videoc, sizeof (aviout_videoc));
	}
	SetWindowText(GetDlgItem(hDlg, IDC_AVIOUTPUT_VIDEO_STATIC), aviout_videoc);
}

static BOOL CALLBACK AVIOutputDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static int recursive = 0;
	
	switch(msg)
	{
	case WM_INITDIALOG:
		pages[AVIOUTPUT_ID] = hDlg;
		currentpage = AVIOUTPUT_ID;
		enable_for_avioutputdlg(hDlg);
		SendDlgItemMessage(hDlg, IDC_AVIOUTPUT_FPS, TBM_SETRANGE, TRUE, MAKELONG(1, VBLANK_HZ_NTSC));
		SendDlgItemMessage(hDlg, IDC_AVIOUTPUT_FPS, TBM_SETPOS, TRUE, VBLANK_HZ_PAL);
		SendMessage(hDlg, WM_HSCROLL, (WPARAM) NULL, (LPARAM) NULL);
		
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
#ifdef PROWIZARD
		case IDC_PROWIZARD:
		    moduleripper ();
		break;
#endif
		case IDC_AVIOUTPUT_ACTIVATED:
		    avioutput_requested = !avioutput_requested;
		    SendMessage(hDlg, WM_HSCROLL, (WPARAM) NULL, (LPARAM) NULL);
		    if (!avioutput_requested)
			AVIOutput_End ();
		    break;

		case IDC_SCREENSHOT:
			screenshot(1);
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
				if(IsDlgButtonChecked(hDlg, IDC_AVIOUTPUT_AUDIO) == BST_CHECKED)
				{
					LPSTR string;
					
					aviout_audioc[0] = 0;
					if(string = AVIOutput_ChooseAudioCodec(hDlg))
					{
						avioutput_audio = AVIAUDIO_AVI;
						strcpy (aviout_audioc, string);
						SetWindowText(GetDlgItem(hDlg, IDC_AVIOUTPUT_AUDIO_STATIC), string);
					}
					else
						avioutput_audio = 0;
				}
				else
					avioutput_audio = 0;
				
				break;
			}
			
		case IDC_AVIOUTPUT_VIDEO:
			{
				if (avioutput_enabled)
				    AVIOutput_End ();
				if(IsDlgButtonChecked(hDlg, IDC_AVIOUTPUT_VIDEO) == BST_CHECKED)
				{
					LPSTR string;
					aviout_videoc[0] = 0;
					if(string = AVIOutput_ChooseVideoCodec(hDlg))
					{
						avioutput_video = 1;
						strcpy (aviout_videoc, string);
						SetWindowText(GetDlgItem(hDlg, IDC_AVIOUTPUT_VIDEO_STATIC), string);
					}
					else
						avioutput_video = 0;
				}
				else
					avioutput_video = 0;
				
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
				    SetWindowText(GetDlgItem(hDlg, IDC_AVIOUTPUT_AUDIO_STATIC), "Wave (internal)");
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
    char *help;
};

static struct GUIPAGE ppage[MAX_C_PAGES];
#define PANEL_X 174
#define PANEL_Y 12
#define PANEL_WIDTH 456
#define PANEL_HEIGHT 396

static HWND updatePanel (HWND hDlg, int id)
{
    static HWND chwnd;
    RECT r1c, r1w, r2c, r2w, r3c, r3w;
    int w, h, pw, ph, x , y;

    if (chwnd != NULL) {
	ShowWindow (chwnd, FALSE);
	DestroyWindow (chwnd);
    }
    if (id < 0) {
	if (!isfullscreen ()) {
	    RECT r;
	    LONG left, top;
	    GetWindowRect (hDlg, &r);
	    left = r.left;
	    top = r.top;
	    RegSetValueEx (hWinUAEKey, "xPosGUI", 0, REG_DWORD, (LPBYTE)&left, sizeof(LONG));
	    RegSetValueEx (hWinUAEKey, "yPosGUI", 0, REG_DWORD, (LPBYTE)&top, sizeof(LONG));
	}
	EnableWindow (GetDlgItem (hDlg, IDHELP), FALSE);
	return NULL;
    }
    GetWindowRect (GetDlgItem (hDlg, IDC_PANEL_FRAME), &r1w);
    GetClientRect (GetDlgItem (hDlg, IDC_PANEL_FRAME), &r1c);
    GetWindowRect (hDlg, &r2w);
    GetClientRect (hDlg, &r2c);
    gui_width = r2c.right;
    gui_height = r2c.bottom;
    chwnd = CreateDialogParam (hUIDLL ? hUIDLL : hInst, ppage[id].pp.pszTemplate, hDlg, ppage[id].pp.pfnDlgProc, id);
    GetWindowRect (hDlg, &r3w);
    GetClientRect (chwnd, &r3c);
    x = r1w.left - r2w.left;
    y = r1w.top - r2w.top;
    w = r3c.right - r3c.left + 1;
    h = r3c.bottom - r3c.top + 1;
    pw = r1w.right - r1w.left + 1;
    ph = r1w.bottom - r1w.top + 1;
    SetWindowPos (chwnd, HWND_TOP, 0, 0, 0, 0,
	SWP_NOSIZE | SWP_NOOWNERZORDER);
    GetWindowRect (chwnd, &r3w);
    GetClientRect (chwnd, &r3c);
    x -= r3w.left - r2w.left - 1;
    y -= r3w.top - r2w.top - 1;
    SetWindowPos (chwnd, HWND_TOP, x + (pw - w) / 2, y + (ph - h) / 2, 0, 0,
	SWP_NOSIZE | SWP_NOOWNERZORDER);
    ShowWindow (chwnd, TRUE);
    EnableWindow (GetDlgItem (hDlg, IDHELP), pHtmlHelp && ppage[currentpage].help ? TRUE : FALSE);
    return chwnd;
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
    is.itemex.pszText = p->pp.pszTitle;
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
    CN(LOADSAVE_ID);

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

    if (owner == NULL)
	owner = GetDesktopWindow();
    if (!isfullscreen ()) {
        DWORD regkeytype;
	DWORD regkeysize = sizeof(LONG);
        if (RegQueryValueEx (hWinUAEKey, "xPosGUI", 0, &regkeytype, (LPBYTE)&x, &regkeysize) != ERROR_SUCCESS)
	    x = 0;
        if (RegQueryValueEx (hWinUAEKey, "yPosGUI", 0, &regkeytype, (LPBYTE)&y, &regkeysize) != ERROR_SUCCESS)
	    y = 0;
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
    SetWindowPos (hDlg,  HWND_TOP, x, y, 0, 0, SWP_NOSIZE);
}

int dragdrop (HWND hDlg, HDROP hd, struct uae_prefs *prefs, int currentpage)
{
    int cnt, i, drv, list;
    char file[MAX_DPATH];
    POINT pt;
    int ret = 0;

    DragQueryPoint (hd, &pt);
    cnt = DragQueryFile (hd, 0xffffffff, NULL, 0);
    if (!cnt)
	return 0;
    drv = 0;
    for (i = 0; i < cnt; i++) {
	struct zfile *f;
	DragQueryFile (hd, i, file, sizeof (file));
	f = zfile_fopen (file, "rb");
	if (f) {
	    int type = zfile_gettype (f);
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
			strcpy (workprefs.df[drv], file);
			disk_insert (drv, file);
			if (drv < 3 && workprefs.dfxtype[drv + 1] >= 0)
			    drv++;
		    }
		break;
		case ZFILE_ROM:
		    strcpy (prefs->romfile, file);
		break;
		case ZFILE_KEY:
		    strcpy (prefs->keyfile, file);
		break;
		case ZFILE_NVR:
		    strcpy (prefs->flashfile, file);
		break;
		case ZFILE_CONFIGURATION:
		    if (cfgfile_doload (&workprefs, file, 0)) {
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

static BOOL CALLBACK DialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static int recursive = 0;
	
    switch(msg)
    {
	case WM_INITDIALOG:
	    guiDlg = hDlg;
	    SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIcon (GetModuleHandle (NULL), MAKEINTRESOURCE(IDI_APPICON)));
	    centerWindow (hDlg);
	    createTreeView (hDlg, currentpage);
	    updatePanel (hDlg, currentpage);
	    EnableWindow( GetDlgItem( hDlg, IDC_RESETAMIGA ), full_property_sheet ? FALSE : TRUE);
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
		    currentpage = tv->itemNew.lParam & 0xffff;
		    currentpage_sub = tv->itemNew.lParam >> 16;
		    updatePanel (hDlg, currentpage);
		    return TRUE;
		}
		break;
	    }
	    break;
	case WM_COMMAND:
	    switch (wParam) 
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
		    EndDialog (hDlg, 1);
		    gui_to_prefs ();
		    guiDlg = NULL;
		    return TRUE;
		case IDCANCEL:
		    updatePanel (hDlg, -1);
		    EndDialog (hDlg, 0);
		    if (allow_quit) 
		    {
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


static int init_page (int tmpl, int icon, int title,
               BOOL (CALLBACK FAR *func) (HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam), char *help)
{
    static id = 0;
    LPTSTR lpstrTitle;
    ppage[id].pp.pszTemplate = MAKEINTRESOURCE (tmpl);
    ppage[id].pp.pszIcon = MAKEINTRESOURCE (icon);
    lpstrTitle = calloc (1, MAX_DPATH);
    LoadString( hUIDLL, title, lpstrTitle, MAX_DPATH );
    ppage[id].pp.pszTitle = lpstrTitle;
    ppage[id].pp.pfnDlgProc = func;
    ppage[id].help = help;
    ppage[id].idx = id;
    id++;
    return id - 1;
}

static int GetSettings (int all_options, HWND hwnd)
{
    static int init_called = 0;
    int psresult;

    full_property_sheet = all_options;
    allow_quit = all_options;
    pguiprefs = &currprefs;
    default_prefs (&workprefs, 0);

    WIN32GUI_LoadUIString( IDS_NONE, szNone, MAX_DPATH );

    prefs_to_gui (&changed_prefs);

    if( !init_called )
    {
	LOADSAVE_ID = init_page (IDD_LOADSAVE, IDI_CONFIGFILE, IDS_LOADSAVE, LoadSaveDlgProc, "gui/configurations.htm");
	MEMORY_ID = init_page (IDD_MEMORY, IDI_MEMORY, IDS_MEMORY, MemoryDlgProc, "gui/ram.htm");
	KICKSTART_ID = init_page (IDD_KICKSTART, IDI_MEMORY, IDS_KICKSTART, KickstartDlgProc, "gui/rom.htm");
	CPU_ID = init_page (IDD_CPU, IDI_CPU, IDS_CPU, CPUDlgProc, "gui/cpu.htm");
	DISPLAY_ID = init_page (IDD_DISPLAY, IDI_DISPLAY, IDS_DISPLAY, DisplayDlgProc, "gui/display.htm");
#if defined(OPENGL) || defined (D3D)
	HW3D_ID = init_page (IDD_OPENGL, IDI_DISPLAY, IDS_OPENGL, hw3dDlgProc, "gui/opengl.htm");
#endif
	CHIPSET_ID = init_page (IDD_CHIPSET, IDI_CPU, IDS_CHIPSET, ChipsetDlgProc, "gui/chipset.htm");
	SOUND_ID = init_page (IDD_SOUND, IDI_SOUND, IDS_SOUND, SoundDlgProc, "gui/sound.htm");
	FLOPPY_ID = init_page (IDD_FLOPPY, IDI_FLOPPY, IDS_FLOPPY, FloppyDlgProc, "gui/floppies.htm");
	DISK_ID = init_page (IDD_DISK, IDI_FLOPPY, IDS_DISK, DiskDlgProc, "gui/disk.htm");
#ifdef FILESYS
	HARDDISK_ID = init_page (IDD_HARDDISK, IDI_HARDDISK, IDS_HARDDISK, HarddiskDlgProc, "gui/hard-drives.htm");
#endif
	PORTS_ID = init_page (IDD_PORTS, IDI_PORTS, IDS_PORTS, PortsDlgProc, "gui/ports.htm");
	INPUT_ID = init_page (IDD_INPUT, IDI_INPUT, IDS_INPUT, InputDlgProc, "gui/input.htm");
	MISC1_ID = init_page (IDD_MISC1, IDI_MISC1, IDS_MISC1, MiscDlgProc1, "gui/misc.htm");
	MISC2_ID = init_page (IDD_MISC2, IDI_MISC2, IDS_MISC2, MiscDlgProc2, "gui/misc.htm");
#ifdef AVIOUTPUT
	AVIOUTPUT_ID = init_page (IDD_AVIOUTPUT, IDI_AVIOUTPUT, IDS_AVIOUTPUT, AVIOutputDlgProc, "gui/output.htm");
#endif
	ABOUT_ID = init_page (IDD_ABOUT, IDI_ABOUT, IDS_ABOUT, AboutDlgProc, NULL);
	C_PAGES = ABOUT_ID + 1;
	init_called = 1;
	currentpage = LOADSAVE_ID;
	hMoveUp = (HICON)LoadImage( hInst, MAKEINTRESOURCE( IDI_MOVE_UP ), IMAGE_ICON, 16, 16, LR_LOADMAP3DCOLORS );
	hMoveDown = (HICON)LoadImage( hInst, MAKEINTRESOURCE( IDI_MOVE_DOWN ), IMAGE_ICON, 16, 16, LR_LOADMAP3DCOLORS );
    }

    if (all_options || !configstore)
	CreateConfigStore ();
    psresult = DialogBox (hUIDLL ? hUIDLL : hInst, MAKEINTRESOURCE (IDD_PANEL), hwnd, DialogProc);

    if (quit_program)
        psresult = -2;

    full_property_sheet = 0;
    return psresult;
}

int gui_init (void)
{
    int ret;
    
    ret = GetSettings(1, GetDesktopWindow ());
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
    char *ptr;
    int pos = -1;

    indicator_leds (led, on);
    if( hStatusWnd )
    {
        if (on)
	    type = SBT_POPOUT;
	else
	    type = 0;
	if (led >= 1 && led <= 4) {
	    pos = 5 + (led - 1);
	    ptr = drive_text + pos * 16;
	    if (gui_data.drive_disabled[led - 1])
		strcpy (ptr, "");
	    else
		sprintf (ptr , "%02d", gui_data.drive_track[led - 1]);
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
	    sprintf(ptr, "FPS: %.1f", (double)((gui_data.fps + 5) / 10.0));
	} else if (led == 8) {
	    pos = 0;
	    ptr = drive_text + pos * 16;
	    sprintf(ptr, "CPU: %.0f%%", (double)((gui_data.idle) / 10.0));
	}
	if (pos >= 0)
	    PostMessage (hStatusWnd, SB_SETTEXT, (WPARAM) ((pos + 1) | type), (LPARAM) ptr);
    }
}

void gui_filename (int num, const char *name)
{
}


static int fsdialog (HWND *hwnd)
{
    HRESULT hr;
    if (gui_active || !isfullscreen ())
	return 0;
    hr = DirectDraw_FlipToGDISurface();
    if (hr != DD_OK) {
        write_log ("FlipToGDISurface failed, %s\n", DXError (hr));
	return 0;
    }
    *hwnd = NULL;
    return 1;
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
    HWND hwnd = guiDlg;

    pause_sound ();
    flipflop = fsdialog (&hwnd);
    if( flipflop )
        ShowWindow( hAmigaWnd, SW_MINIMIZE );

    va_start (parms, format);
    vsprintf( msg, format, parms );
    va_end (parms);
    write_log( msg );
    if (msg[strlen(msg)-1]!='\n')
	write_log("\n");

    WIN32GUI_LoadUIString( IDS_ERRORTITLE, szTitle, MAX_DPATH );

    mbflags = MB_ICONWARNING | MB_TASKMODAL | MB_SETFOREGROUND;
    if (flags == 0)
	mbflags |= MB_OK;
    else if (flags == 1)
	mbflags |= MB_YESNO;
    else if (flags == 2)
	mbflags |= MB_YESNOCANCEL;
    ret = MessageBox (hwnd, msg, szTitle, mbflags);

    if( flipflop )
        ShowWindow( hAmigaWnd, SW_RESTORE );

    resume_sound();
    setmouseactive( focuso );
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
    HWND hwnd = guiDlg;

    pause_sound ();
    flipflop = fsdialog (&hwnd);
    if( flipflop )
        ShowWindow( hAmigaWnd, SW_MINIMIZE );

    va_start (parms, format);
    vsprintf( msg, format, parms );
    va_end (parms);
    write_log( msg );
    if (msg[strlen(msg)-1]!='\n')
	write_log("\n");

    WIN32GUI_LoadUIString( IDS_ERRORTITLE, szTitle, MAX_DPATH );

    MessageBox (hwnd, msg, szTitle, MB_OK | MB_ICONWARNING | MB_TASKMODAL | MB_SETFOREGROUND);

    if( flipflop )
        ShowWindow( hAmigaWnd, SW_RESTORE );
    resume_sound();
    setmouseactive( focuso );
}

void pre_gui_message (const char *format,...)
{
    char msg[2048];
    char szTitle[ MAX_DPATH ];
    va_list parms;

    va_start (parms, format);
    vsprintf( msg, format, parms );
    va_end (parms);
    write_log( msg );

    WIN32GUI_LoadUIString( IDS_ERRORTITLE, szTitle, MAX_DPATH );

    MessageBox( NULL, msg, szTitle, MB_OK | MB_ICONWARNING | MB_TASKMODAL | MB_SETFOREGROUND );

}

void gui_lock (void)
{
}

void gui_unlock (void)
{
}
