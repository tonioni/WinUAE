
extern HRESULT rp_init (void);
extern void rp_free (void);
extern void rp_fixup_options (struct uae_prefs*);
extern void rp_update_status (struct uae_prefs*);
extern void rp_update_leds (int, int);
extern void rp_activate (int, LPARAM);
extern void rp_minimize (int);
extern void rp_mousecapture (int);
extern void rp_turbo (int);
extern void rp_set_hwnd (void);
extern void rp_moved (int);
extern int rp_checkesc (int, uae_u8*, int, int);
extern int rp_isactive (void);
extern void rp_hsync (void);

extern char *rp_param;
extern int rp_rmousevkey;
extern int rp_rmouseholdtime;
extern int rp_screenmode;
extern int rp_inputmode;
