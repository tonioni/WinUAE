 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Stuff
  *
  * Copyright 1995, 1996 Ed Hanway
  * Copyright 1995-2001 Bernd Schmidt
  */

#define UAEMAJOR 1
#define UAEMINOR 1
#define UAESUBREV 0

typedef enum { KBD_LANG_US, KBD_LANG_DK, KBD_LANG_DE, KBD_LANG_SE, KBD_LANG_FR, KBD_LANG_IT, KBD_LANG_ES } KbdLang;

extern long int version;

struct uaedev_mount_info;

struct strlist {
    struct strlist *next;
    char *option, *value;
    int unknown;
};

/* maximum number native input devices supported (single type) */
#define MAX_INPUT_DEVICES 8
/* maximum number of native input device's buttons and axles supported */
#define MAX_INPUT_DEVICE_EVENTS 256
/* 4 different customization settings */
#define MAX_INPUT_SETTINGS 4
#define MAX_INPUT_SUB_EVENT 4
#define MAX_INPUT_SIMULTANEOUS_KEYS 4

struct uae_input_device {
    char *name;
    uae_s16 eventid[MAX_INPUT_DEVICE_EVENTS][MAX_INPUT_SUB_EVENT];
    char *custom[MAX_INPUT_DEVICE_EVENTS][MAX_INPUT_SUB_EVENT];
    uae_u16 flags[MAX_INPUT_DEVICE_EVENTS][MAX_INPUT_SUB_EVENT];
    uae_s16 extra[MAX_INPUT_DEVICE_EVENTS][MAX_INPUT_SIMULTANEOUS_KEYS];
    uae_u8 enabled;
};

#define MAX_SPARE_DRIVES 20

#define CONFIG_TYPE_HARDWARE 1
#define CONFIG_TYPE_HOST 2

struct uae_prefs {

    struct strlist *all_lines;

    char description[256];
    char info[256];
    int config_version;
    char config_hardware_path[MAX_DPATH];
    char config_host_path[MAX_DPATH];

    int illegal_mem;
    int no_xhair;
    int use_serial;
    int serial_demand;
    int serial_hwctsrts;
    int serial_direct;
    int parallel_demand;
    int parallel_postscript_emulation;
    int parallel_postscript_detection;
    int parallel_autoflush_time;
    char ghostscript_parameters[256];
    int use_gfxlib;
    int socket_emu;

    int start_debugger;
    int start_gui;

    KbdLang keyboard_lang;
    int test_drawing_speed;

    int produce_sound;
    int sound_stereo;
    int sound_stereo_separation;
    int sound_mixed_stereo;
    int sound_bits;
    int sound_freq;
    int sound_maxbsiz;
    int sound_interpol;
    int sound_adjust;
    int sound_filter;
    int sound_volume;
    int sound_stereo_swap_paula;
    int sound_stereo_swap_ahi;

    int comptrustbyte;
    int comptrustword;
    int comptrustlong;
    int comptrustnaddr;
    int compnf;
    int compforcesettings;
    int compfpu;
    int comp_midopt;
    int comp_lowopt;

    int comp_hardflush;
    int comp_constjump;
    int comp_oldsegv;

    int cachesize;
    int optcount[10];

    int avoid_cmov;
    int avoid_dga;
    int avoid_vid;
    uae_u32 override_dga_address;

    int gfx_display;
    int gfx_framerate;
    int gfx_width_win, gfx_height_win;
    int gfx_width_fs, gfx_height_fs;
    int gfx_width, gfx_height;
    int gfx_refreshrate;
    int gfx_vsync;
    int gfx_lores;
    int gfx_linedbl;
    int gfx_correct_aspect;
    int gfx_afullscreen;
    int gfx_pfullscreen;
    int gfx_xcenter;
    int gfx_ycenter;
    int gfx_hue, gfx_saturation, gfx_luminance, gfx_contrast, gfx_gamma;

    int gfx_filter;
    int gfx_filter_scanlines;
    int gfx_filter_scanlineratio;
    int gfx_filter_scanlinelevel;
    int gfx_filter_horiz_zoom, gfx_filter_vert_zoom;
    int gfx_filter_horiz_offset, gfx_filter_vert_offset;
    int gfx_filter_filtermode;
    int color_mode;

    int immediate_blits;
    unsigned int chipset_mask;
    int ntscmode;
    int genlock;
    int chipset_refreshrate;
    int collision_level;
    int leds_on_screen;
    int keyboard_leds[3];
    int keyboard_leds_in_use;
    int fast_copper;
    int scsi;
    int uaeserial;
    int catweasel;
    int cpu_idle;
    int cpu_cycle_exact;
    int blitter_cycle_exact;
    int floppy_speed;
    int tod_hack;
    uae_u32 maprom;

    char df[4][MAX_DPATH];
    char dfxlist[MAX_SPARE_DRIVES][MAX_DPATH];
    char romfile[MAX_DPATH];
    char romextfile[MAX_DPATH];
    char flashfile[MAX_DPATH];
    char cartfile[MAX_DPATH];
    char pci_devices[256];
    char prtname[256];
    char sername[256];

    char path_floppy[256];
    char path_hardfile[256];
    char path_rom[256];

    int m68k_speed;
    int cpu_level;
    int cpu_compatible;
    int address_space_24;

    uae_u32 z3fastmem_size;
    uae_u32 fastmem_size;
    uae_u32 chipmem_size;
    uae_u32 bogomem_size;
    uae_u32 a3000mem_size;
    uae_u32 gfxmem_size;

    int kickshifter;
    int filesys_no_uaefsdb;
    int filesys_custom_uaefsdb;
    int mmkeyboard;

    struct uaedev_mount_info *mountinfo;

    int nr_floppies;
    int dfxtype[4];
    int dfxclick[4];
    char dfxclickexternal[4][256];
    int dfxclickvolume;

    /* Target specific options */
    int x11_use_low_bandwidth;
    int x11_use_mitshm;
    int x11_use_dgamode;
    int x11_hide_cursor;
    int svga_no_linear;

    int win32_middle_mouse;
    int win32_logfile;
    int win32_notaskbarbutton;
    int win32_alwaysontop;

    int win32_active_priority;
    int win32_inactive_priority;
    int win32_inactive_pause;
    int win32_inactive_nosound;
    int win32_iconified_priority;
    int win32_iconified_pause;
    int win32_iconified_nosound;

    int win32_no_overlay; /* If this is set, we won't try and use any RGB overlays */
    int win32_ctrl_F11_is_quit;
    int win32_automount_drives;
    int win32_automount_netdrives;
    int win32_midioutdev;
    int win32_midiindev;
    int win32_uaescsimode;
    int win32_soundcard;
    int win32_norecyclebin;
    int win32_specialkey;
    int win32_kbledmode;

    int curses_reverse_video;

    int statecapture;
    int statecapturerate, statecapturebuffersize;

    /* input */

    int jport0;
    int jport1;
    int input_selected_setting;
    int input_joymouse_multiplier;
    int input_joymouse_deadzone;
    int input_joystick_deadzone;
    int input_joymouse_speed;
    int input_autofire_framecnt;
    int input_mouse_speed;
    struct uae_input_device joystick_settings[MAX_INPUT_SETTINGS + 1][MAX_INPUT_DEVICES];
    struct uae_input_device mouse_settings[MAX_INPUT_SETTINGS + 1][MAX_INPUT_DEVICES];
    struct uae_input_device keyboard_settings[MAX_INPUT_SETTINGS + 1][MAX_INPUT_DEVICES];
};

/* Contains the filename of .uaerc */
extern char optionsfile[];
extern void save_options (struct zfile *, struct uae_prefs *, int);
extern void cfgfile_write (struct zfile *, char *format,...);
extern void cfgfile_target_write (struct zfile *, char *format,...);
extern void cfgfile_backup (const char *path);

extern void default_prefs (struct uae_prefs *, int);
extern void discard_prefs (struct uae_prefs *, int);

int parse_cmdline_option (struct uae_prefs *, char, char *);

extern int cfgfile_yesno (char *option, char *value, char *name, int *location);
extern int cfgfile_intval (char *option, char *value, char *name, int *location, int scale);
extern int cfgfile_strval (char *option, char *value, char *name, int *location, const char *table[], int more);
extern int cfgfile_string (char *option, char *value, char *name, char *location, int maxsz);
extern char *cfgfile_subst_path (const char *path, const char *subst, const char *file);

extern int target_parse_option (struct uae_prefs *, char *option, char *value);
extern void target_save_options (struct zfile*, struct uae_prefs *);
extern void target_default_options (struct uae_prefs *, int type);
extern int target_cfgfile_load (struct uae_prefs *, char *filename, int type, int isdefault);

extern int cfgfile_load (struct uae_prefs *p, const char *filename, int *type, int ignorelink);
extern int cfgfile_save (struct uae_prefs *, const char *filename, int);
extern void cfgfile_parse_line (struct uae_prefs *p, char *, int);
extern int cfgfile_parse_option (struct uae_prefs *p, char *option, char *value, int);
extern int cfgfile_get_description (const char *filename, char *description, char *hostlink, char *hardwarelink, int *type);
extern void cfgfile_show_usage (void);
extern uae_u32 cfgfile_uaelib (int mode, uae_u32 name, uae_u32 dst, uae_u32 maxlen);
extern uae_u32 cfgfile_uaelib_modify (uae_u32 mode, uae_u32 parms, uae_u32 size, uae_u32 out, uae_u32 outsize);
extern void cfgfile_addcfgparam (char *);
extern int build_in_prefs (struct uae_prefs *p, int model, int config, int compa, int romcheck);
extern int cmdlineparser (char *s, char *outp[], int max);
extern int cfgfile_handle_custom_event (char *custom, int mode);

extern void fixup_prefs_dimensions (struct uae_prefs *prefs);
extern void fixup_prefs (struct uae_prefs *prefs);

extern void check_prefs_changed_custom (void);
extern void check_prefs_changed_cpu (void);
extern void check_prefs_changed_audio (void);
extern int check_prefs_changed_gfx (void);

extern struct uae_prefs currprefs, changed_prefs;

extern void machdep_init (void);

/* AIX doesn't think it is Unix. Neither do I. */
#if defined(_ALL_SOURCE) || defined(_AIX)
#undef __unix
#define __unix
#endif

#define MAX_COLOR_MODES 5

/* #define NEED_TO_DEBUG_BADLY */

#if !defined(USER_PROGRAMS_BEHAVE)
#define USER_PROGRAMS_BEHAVE 0
#endif

/* Some memsets which know that they can safely overwrite some more memory
 * at both ends and use that knowledge to align the pointers. */

#define QUADRUPLIFY(c) (((c) | ((c) << 8)) | (((c) | ((c) << 8)) << 16))

/* When you call this routine, bear in mind that it rounds the bounds and
 * may need some padding for the array. */

#define fuzzy_memset(p, c, o, l) fuzzy_memset_1 ((p), QUADRUPLIFY (c), (o) & ~3, ((l) + 4) >> 2)
STATIC_INLINE void fuzzy_memset_1 (void *p, uae_u32 c, int offset, int len)
{
    uae_u32 *p2 = (uae_u32 *)((char *)p + offset);
    int a = len & 7;
    len >>= 3;
    switch (a) {
     case 7: p2--; goto l1;
     case 6: p2-=2; goto l2;
     case 5: p2-=3; goto l3;
     case 4: p2-=4; goto l4;
     case 3: p2-=5; goto l5;
     case 2: p2-=6; goto l6;
     case 1: p2-=7; goto l7;
     case 0: if (!--len) return; break;
    }

    for (;;) {
	p2[0] = c;
	l1:
	p2[1] = c;
	l2:
	p2[2] = c;
	l3:
	p2[3] = c;
	l4:
	p2[4] = c;
	l5:
	p2[5] = c;
	l6:
	p2[6] = c;
	l7:
	p2[7] = c;

	if (!len)
	    break;
	len--;
	p2 += 8;
    }
}

/* This one knows it will never be asked to clear more than 32 bytes.  Make sure you call this with a
   constant for the length.  */
#define fuzzy_memset_le32(p, c, o, l) fuzzy_memset_le32_1 ((p), QUADRUPLIFY (c), (o) & ~3, ((l) + 7) >> 2)
STATIC_INLINE void fuzzy_memset_le32_1 (void *p, uae_u32 c, int offset, int len)
{
    uae_u32 *p2 = (uae_u32 *)((char *)p + offset);

    switch (len) {
     case 9: p2[0] = c; p2[1] = c; p2[2] = c; p2[3] = c; p2[4] = c; p2[5] = c; p2[6] = c; p2[7] = c; p2[8] = c; break;
     case 8: p2[0] = c; p2[1] = c; p2[2] = c; p2[3] = c; p2[4] = c; p2[5] = c; p2[6] = c; p2[7] = c; break;
     case 7: p2[0] = c; p2[1] = c; p2[2] = c; p2[3] = c; p2[4] = c; p2[5] = c; p2[6] = c; break;
     case 6: p2[0] = c; p2[1] = c; p2[2] = c; p2[3] = c; p2[4] = c; p2[5] = c; break;
     case 5: p2[0] = c; p2[1] = c; p2[2] = c; p2[3] = c; p2[4] = c; break;
     case 4: p2[0] = c; p2[1] = c; p2[2] = c; p2[3] = c; break;
     case 3: p2[0] = c; p2[1] = c; p2[2] = c; break;
     case 2: p2[0] = c; p2[1] = c; break;
     case 1: p2[0] = c; break;
     case 0: break;
     default: printf("Hit the programmer.\n"); break;
    }
}

#if defined(AMIGA) && defined(__GNUC__)
#include "od-amiga/amiga-kludges.h"
#endif
