
extern HRESULT rp_init (void);
extern void rp_free (void);
extern void rp_fixup_options (struct uae_prefs*);
extern void rp_update_volume (struct uae_prefs*);
extern void rp_update_leds (int, int);
extern void rp_floppy_track (int floppy, int track);
extern void rp_floppydrive_change (int num, int removed);
extern void rp_hd_activity (int, int);
extern void rp_hd_change (int, int);
extern void rp_cd_activity (int, int);
extern void rp_cd_change (int, int);
extern void rp_activate (int, LPARAM);
extern void rp_mouse_capture (int);
extern void rp_mouse_magic (int);
extern void rp_turbo (int);
extern void rp_set_hwnd (HWND);
extern void rp_set_enabledisable (int);
extern int rp_checkesc (int, uae_u8*, int, int);
extern int rp_isactive (void);
extern void rp_vsync (void);
extern HWND rp_getparent (void);
extern void rp_rtg_switch (void);

extern char *rp_param;
extern int rp_rpescapekey;
extern int rp_rpescapeholdtime;
extern int rp_screenmode;
extern int rp_inputmode;

extern void rp_input_change (int num);
extern void rp_disk_image_change (int num, const char *name);
extern void rp_harddrive_image_change (int num, const char *name);
