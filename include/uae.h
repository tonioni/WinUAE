 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Prototypes for main.c
  *
  * Copyright 1996 Bernd Schmidt
  */

extern void do_start_program (void);
extern void do_leave_program (void);
extern void start_program (void);
extern void leave_program (void);
extern void real_main (int, TCHAR **);
extern void usage (void);
extern void parse_cmdline (int argc, TCHAR **argv);
extern void sleep_millis (int ms);
extern void sleep_millis_busy (int ms);
extern int sleep_resolution;

extern void uae_reset (int);
extern void uae_quit (void);
extern void uae_restart (int, TCHAR*);
extern void reset_all_systems (void);
extern void target_reset (void);
extern void target_addtorecent (const TCHAR*, int);
extern void target_run (void);
extern void target_quit (void);
extern bool get_plugin_path (TCHAR *out, int size, const TCHAR *path);
extern void stripslashes (TCHAR *p);
extern void fixtrailing (TCHAR *p);
extern void fullpath (TCHAR *path, int size);
extern void getpathpart (TCHAR *outpath, int size, const TCHAR *inpath);
extern void getfilepart (TCHAR *out, int size, const TCHAR *path);

extern int quit_program;
extern bool console_emulation;

extern TCHAR warning_buffer[256];
extern TCHAR start_path_data[];
extern TCHAR start_path_data_exe[];
extern TCHAR start_path_plugins[];

/* This structure is used to define menus. The val field can hold key
 * shortcuts, or one of these special codes:
 *   -4: deleted entry, not displayed, not selectable, but does count in
 *       select value
 *   -3: end of table
 *   -2: line that is displayed, but not selectable
 *   -1: line that is selectable, but has no keyboard shortcut
 *    0: Menu title
 */
struct bstring {
    const TCHAR *data;
    int val;
};

extern TCHAR *colormodes[];
extern void fetch_saveimagepath (TCHAR*, int, int);
extern void fetch_configurationpath (TCHAR *out, int size);
extern void fetch_screenshotpath (TCHAR *out, int size);
extern void fetch_ripperpath (TCHAR *out, int size);
extern void fetch_statefilepath (TCHAR *out, int size);
extern void fetch_inputfilepath (TCHAR *out, int size);
extern void fetch_datapath (TCHAR *out, int size);
extern uae_u32 uaerand (void);
extern uae_u32 uaesrand (uae_u32 seed);
extern uae_u32 uaerandgetseed (void);

