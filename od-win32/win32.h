/*
  * UAE - The Un*x Amiga Emulator
  *
  * Win32-specific header file
  *
  * (c) 1997-1999 Mathias Ortmann
  * (c) 1998-2001 Brian King
  */

#ifndef __WIN32_H__
#define __WIN32_H__

#define MAKEBD(x,y,z) ((((x) - 2000) * 10000 + (y)) * 100 + (z))
#define GETBDY(x) ((x) / 1000000 + 2000)
#define GETBDM(x) (((x) - ((x / 10000) * 10000)) / 100)
#define GETBDD(x) ((x) % 100)

#define WINUAEPUBLICBETA 0
#define LANG_DLL 1
#define LANG_DLL_FULL_VERSION_MATCH 1

#if WINUAEPUBLICBETA
#define WINUAEBETA _T("20")
#else
#define WINUAEBETA _T("")
#endif

#define WINUAEDATE MAKEBD(2015, 11, 11)

//#define WINUAEEXTRA _T("AmiKit Preview")
//#define WINUAEEXTRA _T("Amiga Forever Edition")

#ifndef WINUAEEXTRA
#define WINUAEEXTRA _T("")
#endif
#ifndef WINUAEREV
#define WINUAEREV _T("")
#endif

#define IHF_WINDOWHIDDEN 6
#define WINUAEAPPNAME _T("Arabuusimiehet.WinUAE")
extern HMODULE hUIDLL;
extern HWND hAmigaWnd, hMainWnd, hHiddenWnd, hGUIWnd;
extern RECT amigawin_rect, mainwin_rect;
extern int in_sizemove;
extern int manual_painting_needed;
extern int mouseactive;
extern int minimized;
extern int monitor_off;
extern void *globalipc, *serialipc;

extern TCHAR start_path_exe[MAX_DPATH];
extern TCHAR start_path_data[MAX_DPATH];
extern TCHAR start_path_plugins[MAX_DPATH];

extern void my_kbd_handler (int, int, int);
extern void clearallkeys (void);
extern int getcapslock (void);

void releasecapture (void);
int WIN32_RegisterClasses (void);
int WIN32_InitHtmlHelp (void);
int WIN32_InitLibraries (void);
int WIN32_CleanupLibraries (void);
void WIN32_HandleRegistryStuff (void);
extern void setup_brkhandler (void);
extern void remove_brkhandler (void);
extern void disablecapture (void);
extern void fullscreentoggle (void);
extern int isfocus (void);
extern void gui_restart (void);
int timebegin (void);
int timeend (void);

extern void setmouseactive (int active);
extern void minimizewindow (void);
extern uae_u32 OSDEP_minimize_uae (void);
extern void updatemouseclip (void);
extern void updatewinrect (bool);

extern bool resumepaused (int priority);
extern bool setpaused (int priority);
extern void unsetminimized (void);
extern void setminimized (void);

void finishjob (void);
void init_colors (void);

extern int pause_emulation;
extern int sound_available;
extern int framecnt;
extern TCHAR VersionStr[256];
extern TCHAR BetaStr[64];
extern int os_admin, os_64bit, os_vista, os_win7, cpu_number, os_touch;
extern OSVERSIONINFO osVersion;
extern int paraport_mask;
extern int gui_active;
extern int quickstart, configurationcache, saveimageoriginalpath, relativepaths, recursiveromscan;

extern HKEY hWinUAEKey;
extern int screen_is_picasso;
extern HINSTANCE hInst;
extern int win_x_diff, win_y_diff;
extern int window_extra_width, window_extra_height;
extern int af_path_2005;
extern TCHAR start_path_new1[MAX_DPATH], start_path_new2[MAX_DPATH];
extern TCHAR bootlogpath[MAX_DPATH];
extern TCHAR logpath[MAX_DPATH];
extern bool winuaelog_temporary_enable;
enum pathtype { PATH_TYPE_DEFAULT, PATH_TYPE_WINUAE, PATH_TYPE_NEWWINUAE, PATH_TYPE_NEWAF, PATH_TYPE_AMIGAFOREVERDATA, PATH_TYPE_END };
void setpathmode (pathtype pt);

extern void sleep_millis (int ms);
extern void sleep_millis_main (int ms);
extern void sleep_millis_amiga (int ms);
extern void wait_keyrelease (void);
extern void keyboard_settrans (void);

extern void handle_rawinput (LPARAM lParam);

#define DEFAULT_PRIORITY 2
struct threadpriorities {
    TCHAR *name;
    int value;
    int classvalue;
    int id;
};
extern struct threadpriorities priorities[];
extern void setpriority (struct threadpriorities *pri);

extern int dinput_wmkey (uae_u32 key);
extern int dinput_winmouse (void);
extern int dinput_wheelbuttonstart (void);
extern int dinput_winmousemode (void);
extern void dinput_window (void);
extern void *open_tablet (HWND hwnd);
extern int close_tablet (void*);
extern void send_tablet (int x, int y, int z, int pres, uae_u32 buttons, int flags, int ax, int ay, int az, int rx, int ry, int rz, RECT *r);
extern void send_tablet_proximity (int);

void addnotifications (HWND hwnd, int remove, int isgui);
void registertouch(HWND hwnd);
int win32_hardfile_media_change (const TCHAR *drvname, int inserted);
extern int CheckRM (const TCHAR *DriveName);
void systray (HWND hwnd, int remove);
void systraymenu (HWND hwnd);
void exit_gui (int);
void fetch_path (const TCHAR *name, TCHAR *out, int size);
void set_path (const TCHAR *name, TCHAR *path);
void set_path (const TCHAR *name, TCHAR *path, pathtype);
void read_rom_list (void);
void associate_file_extensions (void);

#define WIN32_PLUGINDIR _T("plugins\\")
HMODULE WIN32_LoadLibrary (const TCHAR *);
HMODULE WIN32_LoadLibrary2 (const TCHAR *);
int isdllversion (const TCHAR *name, int version, int revision, int subver, int subrev);

extern int screenshot_prepare (void);
extern int screenshot_prepare (int);
extern void screenshot_free (void);

struct winuae_lang
{
    WORD id;
    TCHAR *name;
};
extern struct winuae_lang langs[];
extern HMODULE language_load (WORD language);
extern unsigned int fpucontrol;
extern void fpux_save (int *v);
extern void fpux_restore (int *v);

extern void logging_open (int,int);
extern void logging_cleanup (void);

extern LONG WINAPI WIN32_ExceptionFilter (struct _EXCEPTION_POINTERS *pExceptionPointers, DWORD ec);

#define MAX_SOUND_DEVICES 100
#define SOUND_DEVICE_DS 1
#define SOUND_DEVICE_AL 2
#define SOUND_DEVICE_PA 3
#define SOUND_DEVICE_WASAPI 4
#define SOUND_DEVICE_WASAPI_EXCLUSIVE 5
#define SOUND_DEVICE_XAUDIO2 6

struct sound_device
{
    GUID guid;
    TCHAR *name;
    TCHAR *alname;
    TCHAR *cfgname;
    int panum;
    int type;
};
extern struct sound_device *sound_devices[MAX_SOUND_DEVICES];
extern struct sound_device *record_devices[MAX_SOUND_DEVICES];

struct contextcommand
{
	TCHAR *shellcommand;
	TCHAR *command;
	int icon;
};
struct assext {
    TCHAR *ext;
    TCHAR *cmd;
    TCHAR *desc;
    int icon;
	struct contextcommand *cc;
    int enabled;
};
struct assext exts[];
void associate_file_extensions (void);

#define PATHPREFIX _T("\\\\?\\")
DWORD GetFileAttributesSafe (const TCHAR *name);
BOOL SetFileAttributesSafe (const TCHAR *name, DWORD attr);

#endif
