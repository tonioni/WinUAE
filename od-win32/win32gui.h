#ifndef WIN32GUI_H
#define WIN32GUI_H

#define CFG_DESCRIPTION_LENGTH 128

#define CONFIG_SAVE   0
#define CONFIG_LOAD   1
#define CONFIG_SAVE_FULL 2
#define CONFIG_LOAD_FULL 3
#define CONFIG_DELETE 4

void WIN32GUI_LoadUIString (DWORD id, TCHAR *string, DWORD dwStringLen);
extern int DiskSelection (HWND hDlg, WPARAM wParam, int flag, struct uae_prefs *prefs, TCHAR *, TCHAR *);
void InitializeListView (HWND hDlg);
extern void pre_gui_message (const TCHAR*,...);
extern void gui_message_id (int id);
int dragdrop (HWND hDlg, HDROP hd, struct uae_prefs *prefs, int currentpage);
UAEREG *read_disk_history (int type);
void write_disk_history (void);

struct newresource
{
    LPCDLGTEMPLATEW resource;
    HINSTANCE inst;
    int size;
    int tmpl;
    int width, height;
};

#define GUI_INTERNAL_WIDTH 800
#define GUI_INTERNAL_HEIGHT 600
#define GUI_INTERNAL_FONT 8

extern struct uae_prefs workprefs;

extern struct newresource *scaleresource (struct newresource *res, HWND, int, int, DWORD, bool);
extern void freescaleresource (struct newresource*);
extern void scaleresource_setmult (HWND hDlg, int w, int h, int fs);
extern void scaleresource_getmult (int *mx, int *my);
extern HWND CustomCreateDialog (int templ, HWND hDlg, DLGPROC proc);
extern INT_PTR CustomDialogBox (int templ, HWND hDlg, DLGPROC proc);
extern struct newresource *getresource (int tmpl);
extern void scaleresource_init (const TCHAR*, int);
extern int scaleresource_choosefont (HWND hDlg, int fonttype);
extern void scaleresource_setdefaults (void);
extern void scaleresource_setfont (HWND hDlg);
extern void scaleresource_getdpimult (double*, double*, int*, int*);
extern void scalaresource_listview_font_info(int*);
extern int getscaledfontsize(int size);
extern bool show_box_art(const TCHAR*);
extern void move_box_art_window(void);
extern void close_box_art_window(void);
extern LRESULT CALLBACK BoxArtWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
extern int max_visible_boxart_images;
extern int stored_boxart_window_width;
#endif
