 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Main program
  *
  * Copyright 1995 Ed Hanway
  * Copyright 1995, 1996, 1997 Bernd Schmidt
  */
#include "sysconfig.h"
#include "sysdeps.h"
#include <assert.h>

#include "config.h"
#include "options.h"
#include "threaddep/thread.h"
#include "uae.h"
#include "gensound.h"
#include "audio.h"
#include "sounddep/sound.h"
#include "events.h"
#include "memory.h"
#include "custom.h"
#include "serial.h"
#include "newcpu.h"
#include "disk.h"
#include "debug.h"
#include "xwin.h"
#include "inputdevice.h"
#include "keybuf.h"
#include "gui.h"
#include "zfile.h"
#include "autoconf.h"
#include "osemu.h"
#include "osdep/exectasks.h"
#include "picasso96.h"
#include "bsdsocket.h"
#include "uaeexe.h"
#include "native2amiga.h"
#include "scsidev.h"
#include "akiko.h"
#include "savestate.h"

#ifdef USE_SDL
#include "SDL.h"
#endif

long int version = 256*65536L*UAEMAJOR + 65536L*UAEMINOR + UAESUBREV;

struct uae_prefs currprefs, changed_prefs;

int no_gui = 0;
int joystickpresent = 0;
int cloanto_rom = 0;

struct gui_info gui_data;

char warning_buffer[256];

char optionsfile[256];

/* If you want to pipe printer output to a file, put something like
 * "cat >>printerfile.tmp" above.
 * The printer support was only tested with the driver "PostScript" on
 * Amiga side, using apsfilter for linux to print ps-data.
 *
 * Under DOS it ought to be -p LPT1: or -p PRN: but you'll need a
 * PostScript printer or ghostscript -=SR=-
 */

/* Slightly stupid place for this... */
/* ncurses.c might use quite a few of those. */
char *colormodes[] = { "256 colors", "32768 colors", "65536 colors",
    "256 colors dithered", "16 colors dithered", "16 million colors",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""
};

void discard_prefs (struct uae_prefs *p, int type)
{
    struct strlist **ps = &p->all_lines;
    while (*ps) {
	struct strlist *s = *ps;
	*ps = s->next;
	free (s->value);
	free (s->option);
	free (s);
    }
#ifdef FILESYS
    filesys_cleanup ();
    free_mountinfo (p->mountinfo);
    p->mountinfo = alloc_mountinfo ();
#endif
}

void fixup_prefs_dimensions (struct uae_prefs *prefs)
{
    if (prefs->gfx_width_fs < 320)
	prefs->gfx_width_fs = 320;
    if (prefs->gfx_height_fs < 200)
	prefs->gfx_height_fs = 200;
    if (prefs->gfx_height_fs > 1280)
	prefs->gfx_height_fs = 1280;
    prefs->gfx_width_fs += 7; /* X86.S wants multiples of 4 bytes, might be 8 in the future. */
    prefs->gfx_width_fs &= ~7;
    if (prefs->gfx_width_win < 320)
	prefs->gfx_width_win = 320;
    if (prefs->gfx_height_win < 200)
	prefs->gfx_height_win = 200;
    if (prefs->gfx_height_win > 1280)
	prefs->gfx_height_win = 1280;
    prefs->gfx_width_win += 7; /* X86.S wants multiples of 4 bytes, might be 8 in the future. */
    prefs->gfx_width_win &= ~7;
}

void fixup_prefs (struct uae_prefs *p)
{
    int err = 0;

    if ((p->chipmem_size & (p->chipmem_size - 1)) != 0
	|| p->chipmem_size < 0x40000
	|| p->chipmem_size > 0x800000)
    {
	p->chipmem_size = 0x200000;
	write_log ("Unsupported chipmem size!\n");
	err = 1;
    }
    if (p->chipmem_size > 0x80000)
	p->chipset_mask |= CSMASK_ECS_AGNUS;

    if ((p->fastmem_size & (p->fastmem_size - 1)) != 0
	|| (p->fastmem_size != 0 && (p->fastmem_size < 0x100000 || p->fastmem_size > 0x800000)))
    {
	p->fastmem_size = 0;
	write_log ("Unsupported fastmem size!\n");
	err = 1;
    }
    if ((p->gfxmem_size & (p->gfxmem_size - 1)) != 0
	|| (p->gfxmem_size != 0 && (p->gfxmem_size < 0x100000 || p->gfxmem_size > 0x2000000)))
    {
	write_log ("Unsupported graphics card memory size %lx!\n", p->gfxmem_size);
	p->gfxmem_size = 0;
	err = 1;
    }
    if ((p->z3fastmem_size & (p->z3fastmem_size - 1)) != 0
	|| (p->z3fastmem_size != 0 && (p->z3fastmem_size < 0x100000 || p->z3fastmem_size > 0x20000000)))
    {
	p->z3fastmem_size = 0;
	write_log ("Unsupported Zorro III fastmem size!\n");
	err = 1;
    }
    if (p->address_space_24 && (p->gfxmem_size != 0 || p->z3fastmem_size != 0)) {
	p->z3fastmem_size = p->gfxmem_size = 0;
	write_log ("Can't use a graphics card or Zorro III fastmem when using a 24 bit\n"
		 "address space - sorry.\n");
    }
    if (p->bogomem_size != 0 && p->bogomem_size != 0x80000 && p->bogomem_size != 0x100000 && p->bogomem_size != 0x180000 && p->bogomem_size != 0x1c0000)
    {
	p->bogomem_size = 0;
	write_log ("Unsupported bogomem size!\n");
	err = 1;
    }

    if (p->chipmem_size > 0x200000 && p->fastmem_size != 0) {
	write_log ("You can't use fastmem and more than 2MB chip at the same time!\n");
	p->fastmem_size = 0;
	err = 1;
    }
#if 0
    if (p->m68k_speed < -1 || p->m68k_speed > 20) {
	write_log ("Bad value for -w parameter: must be -1, 0, or within 1..20.\n");
	p->m68k_speed = 4;
	err = 1;
    }
#endif
  
    if (p->produce_sound < 0 || p->produce_sound > 3) {
	write_log ("Bad value for -S parameter: enable value must be within 0..3\n");
	p->produce_sound = 0;
	err = 1;
    }
    if (p->comptrustbyte < 0 || p->comptrustbyte > 3) {
	write_log ("Bad value for comptrustbyte parameter: value must be within 0..2\n");
	p->comptrustbyte = 1;
	err = 1;
    }
    if (p->comptrustword < 0 || p->comptrustword > 3) {
	write_log ("Bad value for comptrustword parameter: value must be within 0..2\n");
	p->comptrustword = 1;
	err = 1;
    }
    if (p->comptrustlong < 0 || p->comptrustlong > 3) {
	write_log ("Bad value for comptrustlong parameter: value must be within 0..2\n");
	p->comptrustlong = 1;
	err = 1;
    }
    if (p->comptrustnaddr < 0 || p->comptrustnaddr > 3) {
	write_log ("Bad value for comptrustnaddr parameter: value must be within 0..2\n");
	p->comptrustnaddr = 1;
	err = 1;
    }
    if (p->compnf < 0 || p->compnf > 1) {
	write_log ("Bad value for compnf parameter: value must be within 0..1\n");
	p->compnf = 1;
	err = 1;
    }
    if (p->comp_hardflush < 0 || p->comp_hardflush > 1) {
	write_log ("Bad value for comp_hardflush parameter: value must be within 0..1\n");
	p->comp_hardflush = 1;
	err = 1;
    }
    if (p->comp_constjump < 0 || p->comp_constjump > 1) {
	write_log ("Bad value for comp_constjump parameter: value must be within 0..1\n");
	p->comp_constjump = 1;
	err = 1;
    }
    if (p->comp_oldsegv < 0 || p->comp_oldsegv > 1) {
	write_log ("Bad value for comp_oldsegv parameter: value must be within 0..1\n");
	p->comp_oldsegv = 1;
	err = 1;
    }
    if (p->cachesize < 0 || p->cachesize > 16384) {
	write_log ("Bad value for cachesize parameter: value must be within 0..16384\n");
	p->cachesize = 0;
	err = 1;
    }

    if (p->cpu_level < 2 && p->z3fastmem_size > 0) {
	write_log ("Z3 fast memory can't be used with a 68000/68010 emulation. It\n"
		 "requires a 68020 emulation. Turning off Z3 fast memory.\n");
	p->z3fastmem_size = 0;
	err = 1;
    }
    if (p->gfxmem_size > 0 && (p->cpu_level < 2 || p->address_space_24)) {
	write_log ("Picasso96 can't be used with a 68000/68010 or 68EC020 emulation. It\n"
		 "requires a 68020 emulation. Turning off Picasso96.\n");
	p->gfxmem_size = 0;
	err = 1;
    }
#ifndef BSDSOCKET
    if (p->socket_emu) {
	write_log ("Compile-time option of BSDSOCKET_SUPPORTED was not enabled.  You can't use bsd-socket emulation.\n");
	p->socket_emu = 0;
	err = 1;
    }
#endif

    if (p->nr_floppies < 0 || p->nr_floppies > 4) {
	write_log ("Invalid number of floppies.  Using 4.\n");
	p->nr_floppies = 4;
	p->dfxtype[0] = 0;
	p->dfxtype[1] = 0;
	p->dfxtype[2] = 0;
	p->dfxtype[3] = 0;
	err = 1;
    }

    if (p->floppy_speed > 0 && p->floppy_speed < 10) {
	p->floppy_speed = 100;
    }
    if (p->input_mouse_speed < 1 || p->input_mouse_speed > 1000) {
	p->input_mouse_speed = 100;
    }
    if (p->cpu_cycle_exact || p->blitter_cycle_exact)
	p->fast_copper = 0;

    if (p->collision_level < 0 || p->collision_level > 3) {
	write_log ("Invalid collision support level.  Using 1.\n");
	p->collision_level = 1;
	err = 1;
    }

    if (p->parallel_postscript_emulation)
	p->parallel_postscript_detection = 1;

    fixup_prefs_dimensions (p);

#ifdef CPU_68000_ONLY
    p->cpu_level = 0;
#endif
#ifndef CPUEMU_0
    p->cpu_compatible = 1;
    p->address_space_24 = 1;
#endif
#if !defined(CPUEMU_5) && !defined (CPUEMU_6)
    p->cpu_compatible = 0;
    p->address_space_24 = 0;
#endif
#if !defined (CPUEMU_6)
    p->cpu_cycle_exact = p->blitter_cycle_exact = 0;
#endif
#ifndef AGA
    p->chipset_mask &= ~CSMASK_AGA;
#endif
#ifndef AUTOCONFIG
    p->z3fastmem_size = 0;
    p->fastmem_size = 0;
    p->gfxmem_size = 0;
#endif
#if !defined (BSDSOCKET)
    p->socket_emu = 0;
#endif
#if !defined (SCSIEMU)
    p->scsi = 0;
    p->win32_aspi = 0;
#endif


    if (err)
	write_log ("Please use \"uae -h\" to get usage information.\n");
}

int quit_program = 0;
static int restart_program;
static char restart_config[256];

void uae_reset (int hardreset)
{
    if (quit_program == 0) {
	quit_program = -2;
	if (hardreset)
	    quit_program = -3;
    }
    
}

void uae_quit (void)
{
    if (quit_program != -1)
	quit_program = -1;
}

/* 0 = normal, 1 = nogui, -1 = disable nogui */
void uae_restart (int opengui, char *cfgfile)
{
    uae_quit ();
    restart_program = opengui > 0 ? 1 : (opengui == 0 ? 2 : 3);
    restart_config[0] = 0;
    if (cfgfile)
	strcpy (restart_config, cfgfile);
}

#ifndef DONT_PARSE_CMDLINE

void usage (void)
{
}
static void parse_cmdline_2 (int argc, char **argv)
{
    int i;

    cfgfile_addcfgparam (0);
    for (i = 1; i < argc; i++) {
	if (strcmp (argv[i], "-cfgparam") == 0) {
	    if (i + 1 == argc)
		write_log ("Missing argument for '-cfgparam' option.\n");
	    else
		cfgfile_addcfgparam (argv[++i]);
	}
    }
}

static void parse_diskswapper (char *s)
{
    char *tmp = my_strdup (s);
    char *delim = ",";
    char *p1, *p2;
    int num = 0;

    p1 = tmp;
    for(;;) {
        p2 = strtok (p1, delim);
        if (!p2)
	    break;
	p1 = NULL;
	if (num >= MAX_SPARE_DRIVES)
	    break;
	strncpy (currprefs.dfxlist[num], p2, 255);
	num++;
    }
    free (tmp);
}

static void parse_cmdline (int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
	if (!strncmp (argv[i], "-diskswapper=", 13)) {
	    parse_diskswapper (argv[i] + 13);
	} else if (strcmp (argv[i], "-cfgparam") == 0) {
	    if (i + 1 < argc)
		i++;
	} else if (strncmp (argv[i], "-config=", 8) == 0) {
	    target_cfgfile_load (&currprefs, argv[i] + 8, 0, 1);
	}
	/* Check for new-style "-f xxx" argument, where xxx is config-file */
	else if (strcmp (argv[i], "-f") == 0) {
	    if (i + 1 == argc) {
		write_log ("Missing argument for '-f' option.\n");
	    } else {
#ifdef FILESYS
                free_mountinfo (currprefs.mountinfo);
	        currprefs.mountinfo = alloc_mountinfo ();
#endif
		target_cfgfile_load (&currprefs, argv[++i], 0, 1);
	    }
	} else if (strcmp (argv[i], "-s") == 0) {
	    if (i + 1 == argc)
		write_log ("Missing argument for '-s' option.\n");
	    else
		cfgfile_parse_line (&currprefs, argv[++i], 0);
	} else if (strcmp (argv[i], "-h") == 0 || strcmp (argv[i], "-help") == 0) {
	    usage ();
	    exit (0);
	} else {
	    if (argv[i][0] == '-' && argv[i][1] != '\0') {
		const char *arg = argv[i] + 2;
		int extra_arg = *arg == '\0';
		if (extra_arg)
		    arg = i + 1 < argc ? argv[i + 1] : 0;
		if (parse_cmdline_option (&currprefs, argv[i][1], (char*)arg) && extra_arg)
		    i++;
	    }
	}
    }
}
#endif

static void parse_cmdline_and_init_file (int argc, char **argv)
{

    strcpy (optionsfile, "");

#ifdef OPTIONS_IN_HOME
    {
	char *home = getenv ("HOME");
	if (home != NULL && strlen (home) < 240)
	{
	    strcpy (optionsfile, home);
	    strcat (optionsfile, "/");
	}
    }
#endif

    parse_cmdline_2 (argc, argv);

    strcat (optionsfile, restart_config);

    if (! target_cfgfile_load (&currprefs, optionsfile, 0, 0)) {
	write_log ("failed to load config '%s'\n", optionsfile);
#ifdef OPTIONS_IN_HOME
	/* sam: if not found in $HOME then look in current directory */
	strcpy (optionsfile, restart_config);
	target_cfgfile_load (&currprefs, optionsfile, 0);
#endif
    }
    fixup_prefs (&currprefs);

    parse_cmdline (argc, argv);
}

void reset_all_systems (void)
{
    init_eventtab ();

    memory_reset ();
#ifdef BSDSOCKET
    bsdlib_reset ();
#endif
#ifdef FILESYS
    filesys_reset ();
    filesys_start_threads ();
    hardfile_reset ();
#endif
#ifdef SCSIEMU
    scsidev_reset ();
    scsidev_start_threads ();
#endif
}

/* Okay, this stuff looks strange, but it is here to encourage people who
 * port UAE to re-use as much of this code as possible. Functions that you
 * should be using are do_start_program() and do_leave_program(), as well
 * as real_main(). Some OSes don't call main() (which is braindamaged IMHO,
 * but unfortunately very common), so you need to call real_main() from
 * whatever entry point you have. You may want to write your own versions
 * of start_program() and leave_program() if you need to do anything special.
 * Add #ifdefs around these as appropriate.
 */

void do_start_program (void)
{
    if (quit_program == -1)
	return;
    /* Do a reset on startup. Whether this is elegant is debatable. */
    inputdevice_updateconfig (&currprefs);
    if (quit_program >= 0)
	quit_program = 2;
    m68k_go (1);
}

void do_leave_program (void)
{
    graphics_leave ();
    inputdevice_close ();
    DISK_free ();
    close_sound ();
    dump_counts ();
#ifdef SERIAL_PORT
    serial_exit ();
#endif
#ifdef CD32
    akiko_free ();
#endif
    if (! no_gui)
	gui_exit ();
#ifdef USE_SDL
    SDL_Quit ();
#endif
#ifdef AUTOCONFIG
    expansion_cleanup ();
#endif
#ifdef FILESYS
    filesys_cleanup ();
#endif
    savestate_free ();
    memory_cleanup ();
    cfgfile_addcfgparam (0);
}

void start_program (void)
{
    do_start_program ();
}

void leave_program (void)
{
    do_leave_program ();
}

static void real_main2 (int argc, char **argv)
{
#if defined (NATMEM_OFFSET) && defined( _WIN32 ) && !defined( NO_WIN32_EXCEPTION_HANDLER )
    extern int EvalException ( LPEXCEPTION_POINTERS blah, int n_except );
    __try
#endif
    {

#ifdef USE_SDL
    SDL_Init (SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_NOPARACHUTE);
#endif

    if (restart_config[0]) {
#ifdef FILESYS
	free_mountinfo (currprefs.mountinfo);
        currprefs.mountinfo = alloc_mountinfo ();
#endif
	default_prefs (&currprefs, 0);
	fixup_prefs (&currprefs);
    }

    if (! graphics_setup ()) {
	exit (1);
    }

#ifdef JIT
    init_shm();
#endif

#ifdef FILESYS
    rtarea_init ();
    hardfile_install ();
#endif

    if (restart_config[0])
        parse_cmdline_and_init_file (argc, argv);
    else
	currprefs = changed_prefs;

    machdep_init ();

    if (! setup_sound ()) {
	write_log ("Sound driver unavailable: Sound output disabled\n");
	currprefs.produce_sound = 0;
    }
    inputdevice_init ();

    changed_prefs = currprefs;
    no_gui = ! currprefs.start_gui;
    if (restart_program == 2)
	no_gui = 1;
    else if (restart_program == 3)
	no_gui = 0;
    restart_program = 0;
    if (! no_gui) {
	int err = gui_init ();
	struct uaedev_mount_info *mi = currprefs.mountinfo;
	currprefs = changed_prefs;
	currprefs.mountinfo = mi;
	if (err == -1) {
	    write_log ("Failed to initialize the GUI\n");
	} else if (err == -2) {
	    return;
	}
    }

#ifdef JIT
    if (!(( currprefs.cpu_level >= 2 ) && ( currprefs.address_space_24 == 0 ) && ( currprefs.cachesize )))
	canbang = 0;
#endif

    logging_init(); /* Yes, we call this twice - the first case handles when the user has loaded
		       a config using the cmd-line.  This case handles loads through the GUI. */
    fixup_prefs (&currprefs);
    changed_prefs = currprefs;

    savestate_init ();
#ifdef SCSIEMU
    scsidev_install ();
#endif
#ifdef AUTOCONFIG
    /* Install resident module to get 8MB chipmem, if requested */
    rtarea_setup ();
#endif

    keybuf_init (); /* Must come after init_joystick */

#ifdef AUTOCONFIG
    expansion_init ();
#endif
    memory_init ();
    memory_reset ();

#ifdef FILESYS
    filesys_install ();
#endif
#ifdef AUTOCONFIG
    gfxlib_install ();
    bsdlib_install ();
    emulib_install ();
    uaeexe_install ();
    native2amiga_install ();
#endif

    custom_init (); /* Must come after memory_init */
#ifdef SERIAL_PORT
    serial_init ();
#endif
    DISK_init ();

    reset_frame_rate_hack ();
    init_m68k(); /* must come after reset_frame_rate_hack (); */

    gui_update ();

    if (graphics_init ()) {
	setup_brkhandler ();
	if (currprefs.start_debugger && debuggable ())
	    activate_debugger ();

#ifdef WIN32
#ifdef FILESYS
	filesys_init (); /* New function, to do 'add_filesys_unit()' calls at start-up */
#endif
#endif
	if (sound_available && currprefs.produce_sound > 1 && ! init_audio ()) {
	    write_log ("Sound driver unavailable: Sound output disabled\n");
	    currprefs.produce_sound = 0;
	}

	start_program ();
    }

    }
#if defined (NATMEM_OFFSET) && defined( _WIN32 ) && !defined( NO_WIN32_EXCEPTION_HANDLER )
    __except( EvalException( GetExceptionInformation(), GetExceptionCode() ) )
    {
	// EvalException does the good stuff...
    }
#endif
}

void real_main (int argc, char **argv)
{
    restart_program = 1;
    fetch_configurationpath (restart_config, sizeof (restart_config));
    strcat (restart_config, OPTIONSFILENAME);
    while (restart_program) {
	changed_prefs = currprefs;
	real_main2 (argc, argv);
        leave_program ();
	quit_program = 0;
    }
    zfile_exit ();
}

#ifndef NO_MAIN_IN_MAIN_C
int main (int argc, char **argv)
{
    real_main (argc, argv);
    return 0;
}
#endif

#ifdef SINGLEFILE
uae_u8 singlefile_config[50000] = { "_CONFIG_STARTS_HERE" };
uae_u8 singlefile_data[1500000] = { "_DATA_STARTS_HERE" };
#endif
