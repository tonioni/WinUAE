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
#include "traps.h"
#include "osemu.h"
#include "picasso96.h"
#include "bsdsocket.h"
#include "uaeexe.h"
#include "native2amiga.h"
#include "scsidev.h"
#include "uaeserial.h"
#include "akiko.h"
#include "cdtv.h"
#include "savestate.h"
#include "filesys.h"
#include "parallel.h"
#include "a2091.h"
#include "a2065.h"
#include "ncr_scsi.h"
#include "scsi.h"
#include "sana2.h"
#include "blkdev.h"
#include "gfxfilter.h"
#include "uaeresource.h"
#include "dongle.h"
#include "sampler.h"
#include "consolehook.h"

#ifdef USE_SDL
#include "SDL.h"
#endif

long int version = 256 * 65536L * UAEMAJOR + 65536L * UAEMINOR + UAESUBREV;

struct uae_prefs currprefs, changed_prefs;
int config_changed;

int no_gui = 0, quit_to_gui = 0;
int cloanto_rom = 0;
int kickstart_rom = 1;
int console_emulation = 0;

struct gui_info gui_data;

TCHAR warning_buffer[256];

TCHAR optionsfile[256];

int uaerand (void)
{
	return rand ();
}

void discard_prefs (struct uae_prefs *p, int type)
{
	struct strlist **ps = &p->all_lines;
	while (*ps) {
		struct strlist *s = *ps;
		*ps = s->next;
		xfree (s->value);
		xfree (s->option);
		xfree (s);
	}
#ifdef FILESYS
	filesys_cleanup ();
#endif
}

static void fixup_prefs_dim2 (struct wh *wh)
{
	if (wh->width < 160)
		wh->width = 160;
	if (wh->height < 128)
		wh->height = 128;
	if (wh->width > 3072)
		wh->width = 3072;
	if (wh->height > 2048)
		wh->height = 2048;
}

void fixup_prefs_dimensions (struct uae_prefs *prefs)
{
	fixup_prefs_dim2 (&prefs->gfx_size_fs);
	fixup_prefs_dim2 (&prefs->gfx_size_win);
	if (prefs->gfx_filter == 0 && prefs->gfx_filter_autoscale)
		prefs->gfx_filter = 1;
	if (prefs->gfx_filter_autoscale) {
		prefs->gfx_filter_horiz_zoom_mult = 0;
		prefs->gfx_filter_vert_zoom_mult = 0;
	}
}

void fixup_cpu (struct uae_prefs *p)
{
	if (p->cpu_frequency == 1000000)
		p->cpu_frequency = 0;
	switch (p->cpu_model)
	{
	case 68000:
		p->address_space_24 = 1;
		if (p->cpu_compatible || p->cpu_cycle_exact)
			p->fpu_model = 0;
		break;
	case 68010:
		p->address_space_24 = 1;
		if (p->cpu_compatible || p->cpu_cycle_exact)
			p->fpu_model = 0;
		break;
	case 68020:
		break;
	case 68030:
		p->address_space_24 = 0;
		break;
	case 68040:
		p->address_space_24 = 0;
		if (p->fpu_model)
			p->fpu_model = 68040;
		break;
	case 68060:
		p->address_space_24 = 0;
		if (p->fpu_model)
			p->fpu_model = 68060;
		break;
	}
	if (p->cpu_model != 68040)
		p->mmu_model = 0;
}


void fixup_prefs (struct uae_prefs *p)
{
	int err = 0;

	built_in_chipset_prefs (p);
	fixup_cpu (p);

	if (((p->chipmem_size & (p->chipmem_size - 1)) != 0 && p->chipmem_size != 0x180000)
		|| p->chipmem_size < 0x20000
		|| p->chipmem_size > 0x800000)
	{
		write_log (L"Unsupported chipmem size %x!\n", p->chipmem_size);
		p->chipmem_size = 0x200000;
		err = 1;
	}
	if ((p->fastmem_size & (p->fastmem_size - 1)) != 0
		|| (p->fastmem_size != 0 && (p->fastmem_size < 0x100000 || p->fastmem_size > 0x800000)))
	{
		write_log (L"Unsupported fastmem size %x!\n", p->fastmem_size);
		err = 1;
	}
	if ((p->gfxmem_size & (p->gfxmem_size - 1)) != 0
		|| (p->gfxmem_size != 0 && (p->gfxmem_size < 0x100000 || p->gfxmem_size > max_z3fastmem / 2)))
	{
		write_log (L"Unsupported graphics card memory size %x (%x)!\n", p->gfxmem_size, max_z3fastmem / 2);
		if (p->gfxmem_size > max_z3fastmem / 2)
			p->gfxmem_size = max_z3fastmem / 2;
		else
			p->gfxmem_size = 0;
		err = 1;
	}
	if ((p->z3fastmem_size & (p->z3fastmem_size - 1)) != 0
		|| (p->z3fastmem_size != 0 && (p->z3fastmem_size < 0x100000 || p->z3fastmem_size > max_z3fastmem)))
	{
		write_log (L"Unsupported Zorro III fastmem size %x (%x)!\n", p->z3fastmem_size, max_z3fastmem);
		if (p->z3fastmem_size > max_z3fastmem)
			p->z3fastmem_size = max_z3fastmem;
		else
			p->z3fastmem_size = 0;
		err = 1;
	}
	if ((p->z3fastmem2_size & (p->z3fastmem2_size - 1)) != 0
		|| (p->z3fastmem2_size != 0 && (p->z3fastmem2_size < 0x100000 || p->z3fastmem2_size > max_z3fastmem)))
	{
		write_log (L"Unsupported Zorro III fastmem size %x (%x)!\n", p->z3fastmem2_size, max_z3fastmem);
		if (p->z3fastmem2_size > max_z3fastmem)
			p->z3fastmem2_size = max_z3fastmem;
		else
			p->z3fastmem2_size = 0;
		err = 1;
	}
	p->z3fastmem_start &= ~0xffff;
	if (p->z3fastmem_start < 0x1000000)
		p->z3fastmem_start = 0x1000000;

	if (p->address_space_24 && (p->gfxmem_size != 0 || p->z3fastmem_size != 0)) {
		p->z3fastmem_size = p->gfxmem_size = 0;
		write_log (L"Can't use a graphics card or Zorro III fastmem when using a 24 bit\n"
			L"address space - sorry.\n");
	}
	if (p->bogomem_size != 0 && p->bogomem_size != 0x80000 && p->bogomem_size != 0x100000 && p->bogomem_size != 0x180000 && p->bogomem_size != 0x1c0000) {
		p->bogomem_size = 0;
		write_log (L"Unsupported bogomem size!\n");
		err = 1;
	}
	if (p->bogomem_size > 0x100000 && (p->cs_fatgaryrev >= 0 || p->cs_ide || p->cs_ramseyrev >= 0)) {
		p->bogomem_size = 0x100000;
		write_log (L"Possible Gayle bogomem conflict fixed\n");
	}
	if (p->chipmem_size > 0x200000 && p->fastmem_size != 0) {
		write_log (L"You can't use fastmem and more than 2MB chip at the same time!\n");
		p->fastmem_size = 0;
		err = 1;
	}
	if (p->mbresmem_low_size > 0x04000000 || (p->mbresmem_low_size & 0xfffff)) {
		p->mbresmem_low_size = 0;
		write_log (L"Unsupported A3000 MB RAM size\n");
	}
	if (p->mbresmem_high_size > 0x04000000 || (p->mbresmem_high_size & 0xfffff)) {
		p->mbresmem_high_size = 0;
		write_log (L"Unsupported Motherboard RAM size\n");
	}

#if 0
	if (p->m68k_speed < -1 || p->m68k_speed > 20) {
		write_log (L"Bad value for -w parameter: must be -1, 0, or within 1..20.\n");
		p->m68k_speed = 4;
		err = 1;
	}
#endif

	if (p->produce_sound < 0 || p->produce_sound > 3) {
		write_log (L"Bad value for -S parameter: enable value must be within 0..3\n");
		p->produce_sound = 0;
		err = 1;
	}
	if (p->comptrustbyte < 0 || p->comptrustbyte > 3) {
		write_log (L"Bad value for comptrustbyte parameter: value must be within 0..2\n");
		p->comptrustbyte = 1;
		err = 1;
	}
	if (p->comptrustword < 0 || p->comptrustword > 3) {
		write_log (L"Bad value for comptrustword parameter: value must be within 0..2\n");
		p->comptrustword = 1;
		err = 1;
	}
	if (p->comptrustlong < 0 || p->comptrustlong > 3) {
		write_log (L"Bad value for comptrustlong parameter: value must be within 0..2\n");
		p->comptrustlong = 1;
		err = 1;
	}
	if (p->comptrustnaddr < 0 || p->comptrustnaddr > 3) {
		write_log (L"Bad value for comptrustnaddr parameter: value must be within 0..2\n");
		p->comptrustnaddr = 1;
		err = 1;
	}
	if (p->compnf < 0 || p->compnf > 1) {
		write_log (L"Bad value for compnf parameter: value must be within 0..1\n");
		p->compnf = 1;
		err = 1;
	}
	if (p->comp_hardflush < 0 || p->comp_hardflush > 1) {
		write_log (L"Bad value for comp_hardflush parameter: value must be within 0..1\n");
		p->comp_hardflush = 1;
		err = 1;
	}
	if (p->comp_constjump < 0 || p->comp_constjump > 1) {
		write_log (L"Bad value for comp_constjump parameter: value must be within 0..1\n");
		p->comp_constjump = 1;
		err = 1;
	}
	if (p->comp_oldsegv < 0 || p->comp_oldsegv > 1) {
		write_log (L"Bad value for comp_oldsegv parameter: value must be within 0..1\n");
		p->comp_oldsegv = 1;
		err = 1;
	}
	if (p->cachesize < 0 || p->cachesize > 16384) {
		write_log (L"Bad value for cachesize parameter: value must be within 0..16384\n");
		p->cachesize = 0;
		err = 1;
	}
	if (p->z3fastmem_size > 0 && (p->address_space_24 || p->cpu_model < 68020)) {
		write_log (L"Z3 fast memory can't be used with a 68000/68010 emulation. It\n"
			L"requires a 68020 emulation. Turning off Z3 fast memory.\n");
		p->z3fastmem_size = 0;
		err = 1;
	}
	if (p->gfxmem_size > 0 && (p->cpu_model < 68020 || p->address_space_24)) {
		write_log (L"Picasso96 can't be used with a 68000/68010 or 68EC020 emulation. It\n"
			L"requires a 68020 emulation. Turning off Picasso96.\n");
		p->gfxmem_size = 0;
		err = 1;
	}
#if !defined (BSDSOCKET)
	if (p->socket_emu) {
		write_log (L"Compile-time option of BSDSOCKET_SUPPORTED was not enabled.  You can't use bsd-socket emulation.\n");
		p->socket_emu = 0;
		err = 1;
	}
#endif

	if (p->nr_floppies < 0 || p->nr_floppies > 4) {
		write_log (L"Invalid number of floppies.  Using 4.\n");
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
	if (p->collision_level < 0 || p->collision_level > 3) {
		write_log (L"Invalid collision support level.  Using 1.\n");
		p->collision_level = 1;
		err = 1;
	}
	if (p->parallel_postscript_emulation)
		p->parallel_postscript_detection = 1;
	if (p->cs_compatible == 1) {
		p->cs_fatgaryrev = p->cs_ramseyrev = p->cs_mbdmac = -1;
		p->cs_ide = 0;
		if (p->cpu_model >= 68020) {
			p->cs_fatgaryrev = 0;
			p->cs_ide = -1;
			p->cs_ramseyrev = 0x0f;
			p->cs_mbdmac = 0;
		}
	}
	fixup_prefs_dimensions (p);

#if !defined (JIT)
	p->cachesize = 0;
#endif
#ifdef CPU_68000_ONLY
	p->cpu_model = 68000;
	p->fpu_model = 0;
#endif
#ifndef CPUEMU_0
	p->cpu_compatible = 1;
	p->address_space_24 = 1;
#endif
#if !defined (CPUEMU_11) && !defined (CPUEMU_12)
	p->cpu_compatible = 0;
	p->address_space_24 = 0;
#endif
#if !defined (CPUEMU_12)
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
#if !defined (SANA2)
	p->sana2 = 0;
#endif
#if !defined (UAESERIAL)
	p->uaeserial = 0;
#endif
#if defined (CPUEMU_12)
	if (p->cpu_cycle_exact) {
		p->gfx_framerate = 1;
		p->cachesize = 0;
		p->m68k_speed = 0;
	}
#endif
	if (p->maprom && !p->address_space_24)
		p->maprom = 0x0f000000;
	if (p->tod_hack && p->cs_ciaatod == 0)
		p->cs_ciaatod = p->ntscmode ? 2 : 1;
	target_fixup_options (p);
}

int quit_program = 0;
static int restart_program;
static TCHAR restart_config[MAX_DPATH];
static int default_config;

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
	deactivate_debugger ();
	if (quit_program != -1)
		quit_program = -1;
	target_quit ();
}

/* 0 = normal, 1 = nogui, -1 = disable nogui */
void uae_restart (int opengui, TCHAR *cfgfile)
{
	uae_quit ();
	restart_program = opengui > 0 ? 1 : (opengui == 0 ? 2 : 3);
	restart_config[0] = 0;
	default_config = 0;
	if (cfgfile)
		_tcscpy (restart_config, cfgfile);
}

#ifndef DONT_PARSE_CMDLINE

void usage (void)
{
}
static void parse_cmdline_2 (int argc, TCHAR **argv)
{
	int i;

	cfgfile_addcfgparam (0);
	for (i = 1; i < argc; i++) {
		if (_tcsncmp (argv[i], L"-cfgparam=", 10) == 0) {
			cfgfile_addcfgparam (argv[i] + 10);
		} else if (_tcscmp (argv[i], L"-cfgparam") == 0) {
			if (i + 1 == argc)
				write_log (L"Missing argument for '-cfgparam' option.\n");
			else
				cfgfile_addcfgparam (argv[++i]);
		}
	}
}

static void parse_diskswapper (TCHAR *s)
{
	TCHAR *tmp = my_strdup (s);
	TCHAR *delim = L",";
	TCHAR *p1, *p2;
	int num = 0;

	p1 = tmp;
	for (;;) {
		p2 = _tcstok (p1, delim);
		if (!p2)
			break;
		p1 = NULL;
		if (num >= MAX_SPARE_DRIVES)
			break;
		_tcsncpy (currprefs.dfxlist[num], p2, 255);
		num++;
	}
	free (tmp);
}

static TCHAR *parsetext (const TCHAR *s)
{
	if (*s == '"' || *s == '\'') {
		TCHAR *d;
		TCHAR c = *s++;
		int i;
		d = my_strdup (s);
		for (i = 0; i < _tcslen (d); i++) {
			if (d[i] == c) {
				d[i] = 0;
				break;
			}
		}
		return d;
	} else {
		return my_strdup (s);
	}
}

static void parse_cmdline (int argc, TCHAR **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		if (!_tcsncmp (argv[i], L"-diskswapper=", 13)) {
			TCHAR *txt = parsetext (argv[i] + 13);
			parse_diskswapper (txt);
			xfree (txt);
		} else if (_tcscmp (argv[i], L"-cfgparam") == 0) {
			if (i + 1 < argc)
				i++;
		} else if (_tcsncmp (argv[i], L"-config=", 8) == 0) {
			TCHAR *txt = parsetext (argv[i] + 8);
			currprefs.mountitems = 0;
			target_cfgfile_load (&currprefs, txt, -1, 0);
			xfree (txt);
		} else if (_tcsncmp (argv[i], L"-statefile=", 11) == 0) {
			TCHAR *txt = parsetext (argv[i] + 11);
			savestate_state = STATE_DORESTORE;
			_tcscpy (savestate_fname, txt);
			xfree (txt);
		} else if (_tcscmp (argv[i], L"-f") == 0) {
			/* Check for new-style "-f xxx" argument, where xxx is config-file */
			if (i + 1 == argc) {
				write_log (L"Missing argument for '-f' option.\n");
			} else {
				currprefs.mountitems = 0;
				target_cfgfile_load (&currprefs, argv[++i], -1, 0);
			}
		} else if (_tcscmp (argv[i], L"-s") == 0) {
			if (i + 1 == argc)
				write_log (L"Missing argument for '-s' option.\n");
			else
				cfgfile_parse_line (&currprefs, argv[++i], 0);
		} else if (_tcscmp (argv[i], L"-h") == 0 || _tcscmp (argv[i], L"-help") == 0) {
			usage ();
			exit (0);
		} else if (_tcsncmp (argv[i], L"-cdimage=", 9) == 0) {
			TCHAR *txt = parsetext (argv[i] + 9);
			cfgfile_parse_option (&currprefs, L"cdimage0", txt, 0);
			xfree (txt);
		} else {
			if (argv[i][0] == '-' && argv[i][1] != '\0') {
				const TCHAR *arg = argv[i] + 2;
				int extra_arg = *arg == '\0';
				if (extra_arg)
					arg = i + 1 < argc ? argv[i + 1] : 0;
				if (parse_cmdline_option (&currprefs, argv[i][1], arg) && extra_arg)
					i++;
			}
		}
	}
}
#endif

static void parse_cmdline_and_init_file (int argc, TCHAR **argv)
{

	_tcscpy (optionsfile, L"");

#ifdef OPTIONS_IN_HOME
	{
		TCHAR *home = getenv ("HOME");
		if (home != NULL && strlen (home) < 240)
		{
			_tcscpy (optionsfile, home);
			_tcscat (optionsfile, L"/");
		}
	}
#endif

	parse_cmdline_2 (argc, argv);

	_tcscat (optionsfile, restart_config);

	if (! target_cfgfile_load (&currprefs, optionsfile, 0, default_config)) {
		write_log (L"failed to load config '%s'\n", optionsfile);
#ifdef OPTIONS_IN_HOME
		/* sam: if not found in $HOME then look in current directory */
		_tcscpy (optionsfile, restart_config);
		target_cfgfile_load (&currprefs, optionsfile, 0);
#endif
	}
	fixup_prefs (&currprefs);

	parse_cmdline (argc, argv);
}

void reset_all_systems (void)
{
	init_eventtab ();

#ifdef PICASSO96
	picasso_reset ();
#endif
#ifdef SCSIEMU
	scsi_reset ();
	scsidev_reset ();
	scsidev_start_threads ();
#endif
#ifdef A2065
	a2065_reset ();
#endif
#ifdef SANA2
	netdev_reset ();
	netdev_start_threads ();
#endif
#ifdef FILESYS
	filesys_prepare_reset ();
	filesys_reset ();
#endif
	memory_reset ();
#if defined (BSDSOCKET)
	bsdlib_reset ();
#endif
#ifdef FILESYS
	filesys_start_threads ();
	hardfile_reset ();
#endif
#ifdef UAESERIAL
	uaeserialdev_reset ();
	uaeserialdev_start_threads ();
#endif
#if defined (PARALLEL_PORT)
	initparallel ();
#endif
	native2amiga_reset ();
	dongle_reset ();
	sampler_init ();
}

/* Okay, this stuff looks strange, but it is here to encourage people who
* port UAE to re-use as much of this code as possible. Functions that you
* should be using are do_start_program () and do_leave_program (), as well
* as real_main (). Some OSes don't call main () (which is braindamaged IMHO,
* but unfortunately very common), so you need to call real_main () from
* whatever entry point you have. You may want to write your own versions
* of start_program () and leave_program () if you need to do anything special.
* Add #ifdefs around these as appropriate.
*/

void do_start_program (void)
{
	if (quit_program == -1)
		return;
	if (!canbang && candirect < 0)
		candirect = 0;
	if (canbang && candirect < 0)
		candirect = 1;
	/* Do a reset on startup. Whether this is elegant is debatable. */
	inputdevice_updateconfig (&currprefs);
	if (quit_program >= 0)
		quit_program = 2;
	m68k_go (1);
}

void do_leave_program (void)
{
	sampler_free ();
	graphics_leave ();
	inputdevice_close ();
	DISK_free ();
	close_sound ();
	dump_counts ();
#ifdef SERIAL_PORT
	serial_exit ();
#endif
#ifdef CDTV
	cdtv_free ();
#endif
#ifdef A2091
	a2091_free ();
#endif
#ifdef NCR
	ncr_free ();
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
#ifdef BSDSOCKET
	bsdlib_reset ();
#endif
	device_func_reset ();
	savestate_free ();
	memory_cleanup ();
	cfgfile_addcfgparam (0);
	machdep_free ();
}

void start_program (void)
{
	do_start_program ();
}

void leave_program (void)
{
	do_leave_program ();
}

#ifndef JIT
extern int DummyException (LPEXCEPTION_POINTERS blah, int n_except)
{
	return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static int real_main2 (int argc, TCHAR **argv)
{
#if (defined (_WIN32) || defined (_WIN64)) && !defined (NO_WIN32_EXCEPTION_HANDLER)
	extern int EvalException (LPEXCEPTION_POINTERS blah, int n_except);
	__try
#endif
	{

#ifdef USE_SDL
		SDL_Init (SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_NOPARACHUTE);
#endif
		config_changed = 1;
		if (restart_config[0]) {
			default_prefs (&currprefs, 0);
			fixup_prefs (&currprefs);
		}

		if (! graphics_setup ()) {
			exit (1);
		}

#ifdef NATMEM_OFFSET
		preinit_shm ();
#endif

		if (restart_config[0])
			parse_cmdline_and_init_file (argc, argv);
		else
			currprefs = changed_prefs;

		if (!machdep_init ()) {
			restart_program = 0;
			return -1;
		}

		if (console_emulation) {
			consolehook_config (&currprefs);
			fixup_prefs (&currprefs);
		}

		if (! setup_sound ()) {
			write_log (L"Sound driver unavailable: Sound output disabled\n");
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
			currprefs = changed_prefs;
			config_changed = 1;
			if (err == -1) {
				write_log (L"Failed to initialize the GUI\n");
				return -1;
			} else if (err == -2) {
				return 1;
			}
		}

		logging_init (); /* Yes, we call this twice - the first case handles when the user has loaded
						 a config using the cmd-line.  This case handles loads through the GUI. */

#ifdef NATMEM_OFFSET
		init_shm ();
#endif

#ifdef JIT
		if (!(currprefs.cpu_model >= 68020 && currprefs.address_space_24 == 0 && currprefs.cachesize))
			canbang = 0;
#endif

		fixup_prefs (&currprefs);
		changed_prefs = currprefs;
		target_run ();
		/* force sound settings change */
		currprefs.produce_sound = 0;

#ifdef AUTOCONFIG
		rtarea_setup ();
#endif
#ifdef FILESYS
		rtarea_init ();
		uaeres_install ();
		hardfile_install ();
#endif
		savestate_init ();
#ifdef SCSIEMU
		scsi_reset ();
		scsidev_install ();
#endif
#ifdef SANA2
		netdev_install ();
#endif
#ifdef UAESERIAL
		uaeserialdev_install ();
#endif
		keybuf_init (); /* Must come after init_joystick */

#ifdef AUTOCONFIG
		expansion_init ();
#endif
#ifdef FILESYS
		filesys_install ();
#endif
		memory_init ();
		memory_reset ();

#ifdef AUTOCONFIG
#if defined (BSDSOCKET)
		bsdlib_install ();
#endif
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
		init_m68k (); /* must come after reset_frame_rate_hack (); */

		gui_update ();

		if (graphics_init ()) {
			setup_brkhandler ();
			if (currprefs.start_debugger && debuggable ())
				activate_debugger ();

			if (!init_audio ()) {
				if (sound_available && currprefs.produce_sound > 1) {
					write_log (L"Sound driver unavailable: Sound output disabled\n");
				}
				currprefs.produce_sound = 0;
			}

			start_program ();
		}

	}
#if (defined (_WIN32) || defined (_WIN64)) && !defined (NO_WIN32_EXCEPTION_HANDLER)
#ifdef JIT
	__except (EvalException (GetExceptionInformation (), GetExceptionCode ()))
#else
	__except (DummyException (GetExceptionInformation (), GetExceptionCode ()))
#endif
	{
		// EvalException does the good stuff...
	}
#endif
	return 0;
}

void real_main (int argc, TCHAR **argv)
{
	restart_program = 1;

	fetch_configurationpath (restart_config, sizeof (restart_config) / sizeof (TCHAR));
	_tcscat (restart_config, OPTIONSFILENAME);
	default_config = 1;

	while (restart_program) {
		int ret;
		changed_prefs = currprefs;
		ret = real_main2 (argc, argv);
		if (ret == 0 && quit_to_gui)
			restart_program = 1;
		leave_program ();
		quit_program = 0;
	}
	zfile_exit ();
}

#ifndef NO_MAIN_IN_MAIN_C
int main (int argc, TCHAR **argv)
{
	real_main (argc, argv);
	return 0;
}
#endif

#ifdef SINGLEFILE
uae_u8 singlefile_config[50000] = { "_CONFIG_STARTS_HERE" };
uae_u8 singlefile_data[1500000] = { "_DATA_STARTS_HERE" };
#endif
