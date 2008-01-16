
extern HRESULT rp_init (void);
extern void rp_free (void);
extern void rp_fixup_options (struct uae_prefs*);
extern void rp_update_status (struct uae_prefs*);
extern void rp_update_leds (int, int);
extern void rp_hd_activity (int, int);
extern void rp_hd_change (int, int);
extern void rp_cd_activity (int, int);
extern void rp_cd_change (int, int);
extern void rp_activate (int, LPARAM);
extern void rp_minimize (int);
extern void rp_mousecapture (int);
extern void rp_turbo (int);
extern void rp_set_hwnd (HWND);
extern void rp_moved (int);
extern int rp_checkesc (int, uae_u8*, int, int);
extern int rp_isactive (void);
extern void rp_hsync (void);

extern char *rp_param;
extern int rp_rpescapekey;
extern int rp_rpescapeholdtime;
extern int rp_screenmode;
extern int rp_inputmode;
