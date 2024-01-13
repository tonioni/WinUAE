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
void HtmlHelp(const TCHAR*);

#define MAX_GUIIDPARAMS 16
#define MAX_DLGID 100

#define xSendDlgItemMessage(a, b, c, d, e) (int)SendDlgItemMessage(a, b, c, (WPARAM)d, (LPARAM)e)

struct dlgstore
{
	RECT r;
	UINT wc;
	HWND h;
};

struct dlgcontext
{
	struct dlgstore dstore[MAX_DLGID];
	int dlgstorecnt;
};

/* Dialog info structure */
typedef struct
{
	HWND      hwndFocus;   /* Current control with focus */
	HFONT     hUserFont;   /* Dialog font */
	HMENU     hMenu;       /* Dialog menu */
	UINT      xBaseUnit;   /* Dialog units (depends on the font) */
	UINT      yBaseUnit;
	LONG_PTR  idResult;    /* EndDialog() result / default pushbutton ID */
	UINT      flags;       /* EndDialog() called for this dialog */
} DIALOGINFO;

/* Dialog template */
typedef struct
{
	DWORD      style;
	DWORD      exStyle;
	DWORD      helpId;
	WORD       nbItems;
	short      x;
	short      y;
	short      cx;
	short      cy;
	LPCWSTR    menuName;
	LPCWSTR    className;
	LPCWSTR    caption;
	WORD       pointSize;
	WORD       weight;
	BOOL       italic;
	LPCWSTR    faceName;
	BOOL       dialogEx;
} DLG_TEMPLATE;

struct newreswnd
{
	HWND hwnd, hwndx[5];
	LONG x, y, w, h;
	int style;
	int region;
	bool selectable;
	bool list, listn;
};

struct newresource
{
	HINSTANCE inst;
	LPCDLGTEMPLATEW sourceresource;
	int sourcesize;
	
	LPCDLGTEMPLATEW resource;
    int size;
    int tmpl;
    int x, y, width, height;
	int setparam_id[MAX_GUIIDPARAMS];
	struct newreswnd hwnds[MAX_DLGID];
	int hwndcnt;
	int listviewcnt;
	int setparamcnt;
	DIALOGINFO dinfo;
	DLG_TEMPLATE dtmpl;
	DLGPROC dlgproc;
	LPARAM param;
	HWND hwnd;
	struct newresource *parent, *child;
	int unitx, unity;
	bool fontchanged;
	int fontsize;
};

#define MIN_GUI_INTERNAL_WIDTH 512
#define MIN_GUI_INTERNAL_HEIGHT 400

#define GUI_INTERNAL_WIDTH_OLD 800
#define GUI_INTERNAL_HEIGHT_OLD 600
#define GUI_INTERNAL_FONT_OLD 8
#define GUI_INTERNAL_WIDTH_NEW 1280
#define GUI_INTERNAL_HEIGHT_NEW 960
#define GUI_INTERNAL_FONT_NEW 10

extern struct uae_prefs workprefs;
extern int dialog_inhibit;
extern int gui_control;
extern int externaldialogactive;

struct customdialogstate
{
	int active;
	int status;
	struct newresource *res;
	HWND hwnd;
	HWND parent;
};
extern struct customdialogstate cdstate;
#define SAVECDS \
	struct customdialogstate old_cds; \
	memcpy(&old_cds, &cdstate, sizeof(cdstate));
#define RESTORECDS \
	memcpy(&cdstate, &old_cds, sizeof(cdstate));

HWND x_CreateDialogIndirectParam(HINSTANCE hInstance, LPCDLGTEMPLATE lpTemplate, HWND hWndParent, DLGPROC lpDialogFunc, LPARAM lParamInit, struct newresource*);
void x_DestroyWindow(HWND, struct newresource*);
void getguipos(int *xp, int *yp);
extern int scaleresource(struct newresource*, struct dlgcontext *dctx, HWND, int, int, DWORD, int);
extern void rescaleresource(struct newresource*, bool);
extern void freescaleresource (struct newresource*);
extern void scaleresource_setsize (int w, int h, int fs);
extern HWND CustomCreateDialog(int templ, HWND hDlg, DLGPROC proc, struct customdialogstate *cds);
extern void CustomDialogClose(HWND, int);
extern INT_PTR CustomDialogBox (int templ, HWND hDlg, DLGPROC proc);
INT_PTR commonproc2(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam, bool *handled);
extern struct newresource *getresource (int tmpl);
extern void scaleresource_init(const TCHAR*, int);
extern int scaleresource_choosefont (HWND hDlg, int fonttype);
extern void scaleresource_setdefaults(HWND);
extern void scalaresource_listview_font_info(int*);
extern int getscaledfontsize(int size, HWND);
extern void scaleresource_modification(HWND);
extern bool show_box_art(const TCHAR*, const TCHAR*);
extern void move_box_art_window(void);
extern void close_box_art_window(void);
extern LRESULT CALLBACK BoxArtWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
extern int max_visible_boxart_images;
extern int stored_boxart_window_width;
extern int stored_boxart_window_width_fsgui;
extern int calculated_boxart_window_width;
void getextendedframebounds(HWND hwnd, RECT *r);
void reset_box_art_window(void);

void gui_cursor(HWND, struct newresource*, int, int, int);
void process_gui_control(HWND h, struct newresource *nres);

void darkmode_initdialog(HWND hDlg);
void darkmode_themechanged(HWND hDlg);
INT_PTR darkmode_ctlcolor(WPARAM wParam, bool *handled);

void regsetfont(UAEREG *reg, const TCHAR *prefix, const TCHAR *name, const TCHAR *fontname, int fontsize, int fontstyle, int fontweight);
bool regqueryfont(UAEREG *reg, const TCHAR *prefix, const TCHAR *name, TCHAR *fontname, int *pfontsize, int *pfontstyle, int *pfontweight);


#endif
