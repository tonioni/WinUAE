 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Text-based user interface
  * Sie haben es sich verdient!
  *
  * Copyright 1996 Tim Gunn, Bernd Schmidt
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include <stdio.h>
#include <ctype.h>

#include "config.h"
#include "options.h"
#include "threaddep/thread.h"
#include "uae.h"
#include "gensound.h"
#include "joystick.h"
#include "keybuf.h"
#include "autoconf.h"
#include "xwin.h"
#include "tui.h"
#include "gui.h"
#include "memory.h"

#define MAX_MENU_HEIGHT 15
#define OPTION_COLUMN 3
#define MENU_COL_OFFSET -2

int mountok=0;

void gui_led (int led, int on)
{
}
void gui_filename (int num, const char *name)
{
}
static void getline (char *p)
{
}
void gui_handle_events (void)
{
}
void gui_fps (int x)
{
}
static void save_settings (void)
{
    FILE *f;
    tui_backup_optionsfile ();
    f = fopen (optionsfile, "w");
    if (f == NULL) {
	write_log ("Error saving options file!\n");
	return;
    }
    save_options (f, &currprefs);
    fclose (f);
}

void gui_exit()
{
}

static struct bstring mainmenu[] = {
    { "UAE configuration", 0 },
    { "_Disk settings", 'D' },
    { "_Video settings", 'V' },
    { "_Memory settings", 'M' },
    { "_CPU settings", 'C' },
    { "_Hard disk settings", 'H' },
    { "_Sound settings", 'S' },
    { "_Other settings", 'O' },
    { "S_ave settings", 'A' },
    { "_Run UAE", 'R' },
    { NULL, -3 }
};

static struct bstring mainmenu2[] = {
    { "UAE configuration", 0 },
    { "_Disk settings", 'D' },
/*    { "_Video settings", 'V' },
    { "_Memory settings", 'M' },
    { "_Hard disk settings", 'H' },
    { "_Sound settings", 'S' }, */
    { "_Other settings", 'O' },
    { "S_ave settings", 'A' },
    { "R_eset UAE", 'E' },
    { "_Quit UAE", 'Q' },
    { "_Run UAE", 'R' },
    { NULL, -3 }
};

static struct bstring diskmenu[] = {
    { "Floppy disk settings", 0 },
    { "Change DF_0:", '0' },
    { "Change DF_1:", '1' },
    { "Change DF_2:", '2' },
    { "Change DF_3:", '3' },
    { NULL, -3 }
};

static struct bstring videomenu[] = {
    { "Video settings", 0 },
    { "Change _width", 'W' },
    { "Change _height", 'H' },
    { "Change _color mode", 'C' },
    { "Select predefined _mode", 'M' },
    { "Toggle _low resolution", 'L' },
    { "Change _X centering", 'X' },
    { "Change _Y centering", 'Y' },
    { "Toggle line _doubling", 'D' },
    { "Toggle _aspect _correction", 'A' },
    { "Change _framerate", 'F' },
    { "_Graphics card memory", 'P'},
    { NULL, -3 }
};

static struct bstring memorymenu[] = {
    { "Memory settings", 0 },
    { "Change _fastmem size", 'F' },
    { "Change _chipmem size", 'C' },
    { "Change _slowmem size", 'S' },
    { "Select ROM _image", 'I' },
    { NULL, -3 }
};

static struct bstring cpumenu[] = {
    { "Change _CPU", 'C' },
    { NULL, -3 }
};

static struct bstring soundmenu[] = {
    { "Sound settings", 0 },
    { "Change _sound emulation accuracy", 'S' },
    { "Change m_inimum sound buffer size", 'I' },
    { "Change m_aximum sound buffer size", 'A' },
    { "Change number of _bits", 'B' },
    { "Change output _frequency", 'F' },
    { "Change s_tereo", 'T' },
    { NULL , -3 }
};

static struct bstring miscmenu[] = {
    { "Miscellaneous settings", 0 },
    { "Toggle joystick port _0 emulation", '0' },
    { "Toggle joystick port _1 emulation", '1' },
    { "Set _CPU emulation speed", 'C' },
    { NULL, -3 }
};

static struct bstring hdmenu[] = {
/*    { "Harddisk/CDROM emulation settings", 0 },*/
    { "_Add a mounted volume", 'A' },
    { "Add a mounted _volume r/o", 'V' },
    { "Add a hard_file", 'F' },
    { "_Delete a mounted volume", 'D' },
    { NULL, -3 }
};

static int makemenu (const char **menu, int x, int y)
{
    const char **m = menu;
    int maxlen = 0, count = 0;
    int w;

    while (*m != NULL) {
	int l = strlen (*m);
	if (l > maxlen)
	    maxlen = l;
	m++; count++;
    }
    w = tui_dlog (x, y, x + maxlen + 2, y + count + 1);
    tui_drawbox (w);
    tui_selwin (w);
    y = 2;
    while (*menu != NULL) {
	tui_gotoxy (2, y++);
	tui_puts (*menu++);
    }
    tui_selwin (0);
    return w;
}

static char tmpbuf[256];

static char *trimfilename(char *s, size_t n)
{
    size_t i;
    if (n > 250)
	n = 250;
    if (strlen (s) == 0)
	strcpy (tmpbuf, "none");
    else if (strlen (s) < n)
	strcpy (tmpbuf, s);
    else {
	tmpbuf[0] = '^';
	strcpy (tmpbuf + 1, s + strlen (s) - n + 2);
    }
    for (i = strlen(tmpbuf); i < n; i++)
	tmpbuf[i] = ' ';
    tmpbuf[i] = 0;
    return tmpbuf;
}

static void print_configuration (void)
{
    char tmp[256];
    int y = 5;
    int i;

    tui_clrwin (0);

    tui_drawbox (0);
    tui_hline (2, 3, tui_cols () - 1);
    sprintf (tmp, "UAE %d.%d.%d: The Un*x Amiga Emulator", UAEMAJOR, UAEMINOR, UAESUBREV);
    tui_gotoxy ((tui_cols () - strlen(tmp))/2, 2); tui_puts (tmp);
    strcpy(tmp, "Press RETURN/ENTER to run UAE, ESC to exit");
    tui_gotoxy ((tui_cols () - strlen(tmp))/2, tui_lines ()); tui_puts (tmp);

    tui_gotoxy (OPTION_COLUMN, y++); sprintf (tmp, "Disk file DF0: %s", trimfilename (currprefs.df[0], tui_cols () - 20)); tui_puts (tmp);
    tui_gotoxy (OPTION_COLUMN, y++); sprintf (tmp, "Disk file DF1: %s", trimfilename (currprefs.df[1], tui_cols () - 20)); tui_puts (tmp);
    tui_gotoxy (OPTION_COLUMN, y++); sprintf (tmp, "Disk file DF2: %s", trimfilename (currprefs.df[2], tui_cols () - 20)); tui_puts (tmp);
    tui_gotoxy (OPTION_COLUMN, y++); sprintf (tmp, "Disk file DF3: %s", trimfilename (currprefs.df[3], tui_cols () - 20)); tui_puts (tmp);
    y++;
    tui_gotoxy (OPTION_COLUMN, y++);
    sprintf (tmp, "VIDEO: %d:%d%s %s", currprefs.gfx_width, currprefs.gfx_height,
	    currprefs.gfx_lores ? " (lores)" : "", colormodes[currprefs.color_mode]);
    tui_puts (tmp);

    tui_gotoxy (OPTION_COLUMN+7, y++);
    if (currprefs.gfx_linedbl)
	tui_puts ("Doubling lines, ");
    if (currprefs.gfx_correct_aspect)
	tui_puts ("Aspect corrected");
    else
	tui_puts ("Not aspect corrected");
    tui_gotoxy (OPTION_COLUMN+7, y++);
    if (currprefs.gfx_xcenter)
	tui_puts ("X centered");
    if (currprefs.gfx_xcenter == 2)
	tui_puts (" (clever)");
    if (currprefs.gfx_ycenter && currprefs.gfx_xcenter)
	tui_puts (", ");
    if (currprefs.gfx_ycenter)
	tui_puts ("Y centered ");
    if (currprefs.gfx_ycenter == 2)
	tui_puts (" (clever)");
    tui_gotoxy (OPTION_COLUMN+7, y++);
    tui_puts ("drawing every ");
    switch (currprefs.gfx_framerate) {
     case 1: break;
     case 2: tui_puts ("2nd "); break;
     case 3: tui_puts ("3rd "); break;
     default: sprintf (tmp, "%dth ",currprefs.gfx_framerate); tui_puts (tmp); break;
    }
    tui_puts ("frame.    ");

    tui_gotoxy (OPTION_COLUMN+7, y++);
    if (currprefs.gfxmem_size) {
	sprintf (tmp, "Picasso 96 %d MB", currprefs.gfxmem_size / 0x100000);
	tui_puts(tmp);
    } else
 	tui_puts ("Picasso 96 Off");
    y++;

    tui_gotoxy (OPTION_COLUMN, y++);
    tui_puts ("CPU: ");
    switch (currprefs.cpu_level) {
     case 0: tui_puts ("68000"); break;
     case 1: tui_puts ("68010"); break;
     case 2: tui_puts ("68020"); break;
     case 3: tui_puts ("68020/68881"); break;
    }
    if (currprefs.address_space_24 && currprefs.cpu_level > 1)
	tui_puts (" (24 bit addressing)");
    if (currprefs.cpu_compatible)
	tui_puts (" (slow but compatible)");
    tui_gotoxy (OPTION_COLUMN, y++);
    sprintf (tmp, "MEMORY: %4dK chip; %4dK fast; %4dK slow",
	     currprefs.chipmem_size/1024,
	     currprefs.fastmem_size/1024,
	     currprefs.bogomem_size/1024);
    tui_puts (tmp);

    tui_gotoxy (OPTION_COLUMN, y++);
    sprintf (tmp, "ROM IMAGE: %s", trimfilename (currprefs.romfile, tui_cols () - 50));
    tui_puts (tmp);
    tui_gotoxy (OPTION_COLUMN, y++);
    if (!sound_available)
	tui_puts ("SOUND: Not available");
    else {
	switch (currprefs.produce_sound) {
	 case 0: tui_puts ("SOUND: 0 (Off)"); break;
	 case 1: tui_puts ("SOUND: 1 (Off, but emulated)"); break;
	 case 2: tui_puts ("SOUND: 2 (On)"); break;
	 case 3: tui_puts ("SOUND: 3 (On, emulated perfectly)"); break;
	}
	tui_gotoxy (OPTION_COLUMN + 7, y++);
	sprintf (tmp, "%d bits at %d Hz", currprefs.sound_bits, currprefs.sound_freq);
	tui_puts (tmp);
	tui_gotoxy (OPTION_COLUMN + 7, y++);
	sprintf (tmp, "Minimum buffer size %d bytes, maximum %d bytes", currprefs.sound_minbsiz, currprefs.sound_maxbsiz);
	tui_puts (tmp);
    }

    tui_gotoxy (OPTION_COLUMN,y++);
    tui_puts ("GAME PORT 1: "); tui_puts (gameport_state (0));
    tui_gotoxy (OPTION_COLUMN,y++);
    tui_puts ("GAME PORT 2: "); tui_puts (gameport_state (1));

    for (i = 0;; i++) {
	char buf[256];

	tui_gotoxy (OPTION_COLUMN+1,y++);
	if (sprintf_filesys_unit (currprefs.mountinfo, buf, i) == -1)
	    break;
	tui_puts (buf);
    }
}

static void HDOptions (void)
{
    char *buff;
    char tmp[256];
    char mountvol[256];
    char mountdir[256];
    int c = 0;

    for (;;){

	tui_selwin(0);
	print_configuration();

	c = tui_menubrowse (hdmenu, MENU_COL_OFFSET, 5, c, MAX_MENU_HEIGHT);
	if (c == -1)
	    break;
	else switch (c) {
	 case 0:
	    tui_wgets (mountvol, "Enter mounted volume name", 10);
	    if (strlen (mountvol) == 0)
		break;
	    if (mountvol[strlen(mountvol)-1]==':')
		mountvol[strlen(mountvol)-1] = 0;
	    tui_wgets (mountdir, "Enter mounted volume path", 78);
	    add_filesys_unit (currprefs.mountinfo, mountvol, mountdir, 0, 0, 0, 0, 0);
	    break;
	 case 1:
	    tui_wgets (mountvol, "Enter mounted volume name", 10);
	    if (strlen (mountvol) == 0)
		break;
	    if (mountvol[strlen (mountvol)-1]==':')
		mountvol[strlen (mountvol)-1] = 0;
	    tui_wgets (mountdir, "Enter mounted volume path", 78);
	    add_filesys_unit (currprefs.mountinfo, mountvol, mountdir, 1, 0, 0, 0, 0);
	    break;
	 case 2:
	    buff = tui_filereq("*", "", "Select the hardfile to be mounted");
	    if (buff == NULL)
		break;
	    strcpy (mountvol, buff);
	    tui_wgets (mountdir, "Enter number of sectors per track", 4);
	    tui_wgets (mountdir + 10, "Enter number of heads", 4);
	    tui_wgets (mountdir + 20, "Enter number of reserved blocks", 3);
	    tui_wgets (mountdir + 30, "Enter block size", 4);
	    buff = add_filesys_unit (currprefs.mountinfo, 0, mountvol, 1,
				     atoi (mountdir), atoi (mountdir + 10),
				     atoi (mountdir + 20), atoi (mountdir + 30));
	    if (buff)
		tui_errorbox (buff);
	    break;
	 case 3:
	    tui_wgets (mountvol, "Enter number of volume to be removed (0 for UAE0:, etc.)", 2);
	    if (kill_filesys_unit (currprefs.mountinfo, atoi (mountvol)) == -1)
		tui_errorbox ("Volume does not exist");
	    break;
	}
    }
}

static void DiskOptions (void)
{
    char tmp[256];
    int c = 0;

    for (;;) {
	char *sel;

	tui_selwin(0);
	print_configuration();

	c = tui_menubrowse (diskmenu, MENU_COL_OFFSET, 5, c, MAX_MENU_HEIGHT);
	if (c == -1)
	    break;
	else switch (c) {
	 case 0:
	 case 1:
	 case 2:
	 case 3:
	    sprintf (tmp, "Select a diskfile for DF%d:", c);
	    sel = tui_filereq("*.adf", currprefs.df[c], tmp);
	    if (sel == NULL)
		break;
	    strcpy (currprefs.df[c], sel);
	    break;
	}
    }
}

static void VideoOptions (void)
{
    char tmp[256];
    int c = 0;

    for (c = 0; c < 10; c++)
	if (videomenu[c].val == 'M') {
	    if (video_mode_menu == NULL)
		videomenu[c].val = -4;
	    break;
	}

    c = 0;
    for (;;) {

	tui_selwin(0);
	print_configuration();

	c = tui_menubrowse (videomenu, MENU_COL_OFFSET, 5, c, MAX_MENU_HEIGHT);
	if (c == -1)
	    break;
	else switch (c) {
	 case 0:
	    tui_wgets (tmp, "Enter new video mode width", 4);
	    if (atoi (tmp) < 320 || atoi (tmp) > 1600 /* maybe we'll implement SHires */)
		tui_errorbox ("Insane value for video mode width");
	    else
		currprefs.gfx_width = atoi (tmp);
	    break;
	 case 1:
	    tui_wgets (tmp, "Enter new video mode height", 4);
	    if (atoi (tmp) < 200 || atoi (tmp) > 800 /* whatever */)
		tui_errorbox ("Insane value for video mode height");
	    else
		currprefs.gfx_height = atoi (tmp);
	    break;
	 case 2:
	    currprefs.color_mode++;
	    if (currprefs.color_mode > MAX_COLOR_MODES)
		currprefs.color_mode=0;
	    break;
	 case 3:
	    c = tui_menubrowse (video_mode_menu, 4, 6, 0, 15);
	    if (c != -1)
		vidmode_menu_selected(c);
	    c = 3;
	    break;
	 case 4:
	    currprefs.gfx_lores = !currprefs.gfx_lores;
	    break;
	 case 5:
	    currprefs.gfx_xcenter = (currprefs.gfx_xcenter + 1) % 3;
	    break;
	 case 6:
	    currprefs.gfx_ycenter = (currprefs.gfx_ycenter + 1) % 3;
	    break;
	 case 7:
	    currprefs.gfx_linedbl = !currprefs.gfx_linedbl;
	    break;
	 case 8:
	    currprefs.gfx_correct_aspect = !currprefs.gfx_correct_aspect;
	    break;
	 case 9:
	    currprefs.gfx_framerate++;
	    if (currprefs.gfx_framerate > 9)
		currprefs.gfx_framerate=1;
	    break;
	 case 10:
	    currprefs.gfxmem_size += 0x100000;
	    if (currprefs.gfxmem_size > 0x800000)
		currprefs.gfxmem_size = 0;
	    break;
	}
    }
}

static void MemoryOptions (void)
{
    char *tmp;
    int c = 0;
    for (;;) {

	tui_selwin(0);
	print_configuration ();

	c = tui_menubrowse (memorymenu, MENU_COL_OFFSET, 5, c, MAX_MENU_HEIGHT);
	if (c == -1)
	    break;
	else switch (c) {
	 case 0:
	    if (currprefs.fastmem_size == 0)
		currprefs.fastmem_size = 0x200000;
	    else if (currprefs.fastmem_size == 0x800000)
		currprefs.fastmem_size = 0;
	    else
		currprefs.fastmem_size <<= 1;
	    break;
	 case 1:
	    if (currprefs.chipmem_size == 0x800000)
		currprefs.chipmem_size = 0x80000;
	    else
		currprefs.chipmem_size <<= 1;
	    if (currprefs.chipmem_size > 0x200000)
		currprefs.fastmem_size = 0;
	    break;
	 case 2:
	    if (currprefs.bogomem_size == 0)
		currprefs.bogomem_size = 0x40000;
	    else if (currprefs.bogomem_size == 0x100000)
		currprefs.bogomem_size = 0;
	    else
		currprefs.bogomem_size <<= 1;
	    break;
	 case 3:
	    tmp = tui_filereq ("*.rom", currprefs.romfile, "Select a ROM image");
	    if (tmp != NULL)
		strcpy (currprefs.romfile, tmp);
	    break;
	}
    }
}

static void CPUOptions (void)
{
    char *tmp;
    int c = 0;
    for (;;) {
	tui_selwin(0);
	print_configuration ();

	c = tui_menubrowse (cpumenu, MENU_COL_OFFSET, 5, c, MAX_MENU_HEIGHT);
	if (c == -1)
	    break;
	else switch (c) {
	 case 0:
	    currprefs.cpu_level++;
	    if (currprefs.cpu_level > 3)
		currprefs.cpu_level = 0;
	    /* Default to 32 bit addressing when switching from a 68000/68010 to a 32 bit CPU */
	    if (currprefs.cpu_level == 2)
		currprefs.address_space_24 = 0;
	    if (currprefs.cpu_level != 0)
		currprefs.cpu_compatible = 0;
	    break;
	}
    }
}

static void SoundOptions (void)
{
    char tmp[256];
    int c = 0;
    for (;;) {
	tui_selwin(0);
	print_configuration ();
	c = tui_menubrowse (soundmenu, MENU_COL_OFFSET, 5, c, MAX_MENU_HEIGHT);
	if (c == -1)
	    break;
	else switch (c) {
	 case 0:
	    currprefs.produce_sound++;
	    if (currprefs.produce_sound > 3)
		currprefs.produce_sound = 0;
	    break;

	 case 1:
	    tui_wgets (tmp, "Enter new minimum sound buffer size in bytes", 6);
	    if (atoi (tmp) < 128 || atoi (tmp) > 65536 || atoi (tmp) > currprefs.sound_maxbsiz)
		tui_errorbox ("Insane value for minimum sound buffer size");
	    else
		currprefs.sound_minbsiz = atoi (tmp);
	    break;

	 case 2:
	    tui_wgets (tmp, "Enter new maximum sound buffer size in bytes", 6);
	    if (atoi (tmp) < 128 || atoi (tmp) > 65536 || atoi (tmp) < currprefs.sound_minbsiz)
		tui_errorbox ("Insane value for maximum sound buffer size");
	    else
		currprefs.sound_maxbsiz = atoi (tmp);
	    break;

	 case 3:
	    tui_wgets (tmp, "Enter new number of bits", 3);
	    if (atoi (tmp)!= 8 && atoi (tmp) != 16)
		tui_errorbox ("Unsupported number of bits");
	    else
		currprefs.sound_bits = atoi (tmp);
	    break;

	 case 4:
	    tui_wgets (tmp, "Enter new sound output frequency", 6);
	    if (atoi (tmp) < 11025 || atoi (tmp) > 44100)
		tui_errorbox ("Unsupported frequency");
	    else
		currprefs.sound_freq = atoi (tmp);
	    break;
	 case 5:
	    currprefs.stereo = (currprefs.stereo + 1) % 3;
	    break;
	}
    }
}

static void OtherOptions (void)
{
    char tmp[256];
    int c = 0;

    for (;;) {
	tui_selwin (0);
	print_configuration ();
	c = tui_menubrowse (miscmenu, MENU_COL_OFFSET, 5, c, MAX_MENU_HEIGHT);
	if (c == -1) {
	    break;
	} else switch (c) {
	 case 0:
	    currprefs.jport0 = (currprefs.jport0 + 1) % 6;
	    if (currprefs.jport0 == currprefs.jport1)
	      currprefs.jport1 = (currprefs.jport1 + 5) % 6;	      
	    break;
	 case 1:
	    currprefs.jport1 = (currprefs.jport1 + 1) % 6;
	    if (currprefs.jport0 == currprefs.jport1)
	      currprefs.jport0 = (currprefs.jport0 + 5) % 6;
	    break;
	 case 2:
	    tui_wgets (tmp, "Enter new CPU emulation speed", 6);
	    if (atoi (tmp) < 1 || atoi (tmp) > 20)
		tui_errorbox ("Unsupported CPU emulation speed");
	    else
		currprefs.m68k_speed = atoi (tmp);
	    break;
	}
    }
}

static int do_gui (int mode)
{
    char cwd[1024];

    if (getcwd (cwd, 1024) == NULL)
	return 0;

    tui_setup ();

    for (;;) {
	int c;

	tui_selwin (0);
	print_configuration ();
	c = tui_menubrowse (mode == 0 ? mainmenu2 : mainmenu, MENU_COL_OFFSET, 4, 0, MAX_MENU_HEIGHT);
	if (c == -1) {
	    tui_shutdown ();
	    return -2;
	}
	if (mode == 1) {
	    if (c == 8)
		break;
	    switch (c) {
	     case 0: DiskOptions (); break;
	     case 1: VideoOptions (); break;
	     case 2: MemoryOptions (); break;
	     case 3: CPUOptions (); break;
	     case 4: HDOptions (); break;
	     case 5: SoundOptions (); break;
	     case 6: OtherOptions (); break;
	     case 7: save_settings (); break;
	    }
	} else {
	    if (c == 5)
		break;
	    switch (c) {
	     case 0: DiskOptions (); break;
	     case 1: OtherOptions (); break;
	     case 2: save_settings (); break;
	     case 3: uae_reset (); break;
	     case 4: uae_quit (); break;
	    }
	}
    }
    tui_shutdown ();

    chdir (cwd);
    return 0;
}

int gui_init (void)
{
    return do_gui (1);
}

void gui_changesettings (void)
{
    struct uae_prefs oldprefs;
    oldprefs = currprefs;

    if (do_gui(0) == -2)
	uae_quit ();
    else {
	changed_prefs = currprefs;
	currprefs = oldprefs;
	currprefs.jport0 = changed_prefs.jport0;
	currprefs.jport1 = changed_prefs.jport1;
	joystick_setting_changed ();
    }
}

int gui_update (void)
{
    return 0;
}

void gui_lock (void)
{
}

void gui_unlock (void)
{
}
