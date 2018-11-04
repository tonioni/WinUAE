
extern HRESULT rp_init (void);
extern void rp_free (void);
extern int rp_close (void);
extern void rp_fixup_options (struct uae_prefs*);
extern void rp_pause (int paused);
extern void rp_activate (int, LPARAM);
extern void rp_mouse_capture (int);
extern void rp_mouse_magic (int);
extern void rp_turbo_cpu (int);
extern void rp_turbo_floppy (int);
extern void rp_set_hwnd (HWND);
extern void rp_set_hwnd_delayed (void);
extern void rp_set_enabledisable (int);
extern int rp_checkesc (int, int, int);
extern int rp_isactive (void);
extern void rp_vsync (void);
extern HWND rp_getparent (void);
extern void rp_rtg_switch (void);
extern void rp_screenmode_changed (void);
extern void rp_keymap(TrapContext*, uaecptr, uae_u32);
extern USHORT rp_rawbuttons(LPARAM lParam, USHORT usButtonFlags);
extern bool rp_mouseevent(int x, int y, int buttons, int buttonmask);
extern bool rp_ismouseevent(void);

extern TCHAR *rp_param;
extern int rp_rpescapekey;
extern int rp_rpescapeholdtime;
extern int rp_screenmode;
extern int rp_inputmode;
extern int log_rp;

extern void rp_input_change (int num);
extern void rp_disk_image_change (int num, const TCHAR *name, bool writeprotected);
extern void rp_harddrive_image_change (int num, bool readonly, const TCHAR *name);
extern void rp_cd_image_change (int num, const TCHAR *name);

extern void rp_update_gameport (int port, int mask, int onoff);
extern void rp_update_volume (struct uae_prefs*);
extern void rp_update_leds (int, int, int, int);
extern void rp_floppy_track (int floppy, int track);
extern void rp_hd_activity (int, int, int);
extern void rp_cd_activity (int, int);

void rp_floppy_device_enable (int num, bool enabled);
void rp_hd_device_enable (int num, bool enabled);
void rp_cd_device_enable (int num, bool enabled);
void rp_enumdevices (void);

