#ifndef WIN32GUI_H
#define WIN32GUI_H

#define MAXJOYSTICKS 2

#define MAX_BUFFER_SIZE    256
#define NUM_DRIVES         16

typedef struct
{
    char path[256];
    char name[256];
    uae_u16 rw;
    uae_u8  sectors;
    uae_u8  surfaces;
    uae_u8  reserved;
    uae_u8  hardfile;
    uae_u16 spare;
} drive_specs;

extern drive_specs drives[NUM_DRIVES];

#define CFG_DESCRIPTION_LENGTH 128
#define CFG_SER_LENGTH 256
#define CFG_ROM_LENGTH 256
#define CFG_PAR_LENGTH 256
#define CFG_KEY_LENGTH 256

#define CONFIG_VERSION_MAJOR 1
#define CONFIG_VERSION_MINOR 2

#define DIRECT_SOUND_ENABLED 0x01
#define AHI_ENABLED          0x02

#define CONFIG_SAVE   0
#define CONFIG_LOAD   1
#define CONFIG_SAVE_FULL 2
#define CONFIG_LOAD_FULL 3
#define CONFIG_DELETE 4

void WIN32GUI_LoadUIString( DWORD id, char *string, DWORD dwStringLen );
extern int GetSettings (int all_options, HWND);
extern int DiskSelection( HWND hDlg, WPARAM wParam, int flag, struct uae_prefs *prefs, char *);
void InitializeListView( HWND hDlg );
extern void pre_gui_message (const char*,...);
int dragdrop (HWND hDlg, HDROP hd, struct uae_prefs *prefs, int currentpage);
HKEY read_disk_history (void);

#endif
