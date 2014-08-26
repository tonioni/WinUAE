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
#include "cd32_fmv.h"
#include "cdtv.h"
#include "savestate.h"
#include "filesys.h"
#include "parallel.h"
#include "a2091.h"
#include "a2065.h"
#include "ncr_scsi.h"
#include "ncr9x_scsi.h"
#include "scsi.h"
#include "sana2.h"
#include "blkdev.h"
#include "gfxfilter.h"
#include "uaeresource.h"
#include "dongle.h"
#include "sampler.h"
#include "consolehook.h"
#include "gayle.h"
#include "gfxboard.h"
#include "luascript.h"
#include "uaenative.h"
#include "tabletlibrary.h"
#include "cpuboard.h"
#include "uae/ppc.h"
#ifdef RETROPLATFORM
#include "rp.h"
#endif
#ifdef USE_SDL
#include "SDL.h"
#endif

long int version = 256 * 65536L * UAEMAJOR + 65536L * UAEMINOR + UAESUBREV;

struct uae_prefs currprefs, changed_prefs;
int config_changed;

bool no_gui = 0, quit_to_gui = 0;
bool cloanto_rom = 0;
bool kickstart_rom = 1;
bool console_emulation = 0;

struct gui_info gui_data;

TCHAR warning_buffer[256];

TCHAR optionsfile[256];

static uae_u32 randseed;
static int oldhcounter;

uae_u32 uaesrand (uae_u32 seed)
{
	oldhcounter = -1;
	randseed = seed;
	//randseed = 0x12345678;
	//write_log (_T("seed=%08x\n"), randseed);
	return randseed;
}
uae_u32 uaerand (void)
{
	if (oldhcounter != hsync_counter) {
		srand (hsync_counter ^ randseed);
		oldhcounter = hsync_counter;
	}
	uae_u32 r = rand ();
	//write_log (_T("rand=%08x\n"), r);
	return r;
}
uae_u32 uaerandgetseed (void)
{
	return randseed;
}

void my_trim (TCHAR *s)
{
	int len;
	while (_tcslen (s) > 0 && _tcscspn (s, _T("\t \r\n")) == 0)
		memmove (s, s + 1, (_tcslen (s + 1) + 1) * sizeof (TCHAR));
	len = _tcslen (s);
	while (len > 0 && _tcscspn (s + len - 1, _T("\t \r\n")) == 0)
		s[--len] = '\0';
}

TCHAR *my_strdup_trim (const TCHAR *s)
{
	TCHAR *out;
	int len;

	while (_tcscspn (s, _T("\t \r\n")) == 0)
		s++;
	len = _tcslen (s);
	while (len > 0 && _tcscspn (s + len - 1, _T("\t \r\n")) == 0)
		len--;
	out = xmalloc (TCHAR, len + 1);
	memcpy (out, s, len * sizeof (TCHAR));
	out[len] = 0;
	return out;
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
	if (wh->special)
		return;
	if (wh->width < 160) {
		error_log (_T("Width (%d) must be at least 160."), wh->width);
		wh->width = 160;
	}
	if (wh->height < 128) {
		error_log (_T("Height (%d) must be at least 128."), wh->height);
		wh->height = 128;
	}
	if (wh->width > max_uae_width) {
		error_log (_T("Width (%d) max is %d."), wh->width, max_uae_width);
		wh->width = max_uae_width;
	}
	if (wh->height > max_uae_height) {
		error_log (_T("Height (%d) max is %d."), wh->height, max_uae_height);
		wh->height = max_uae_height;
	}
}

void fixup_prefs_dimensions (struct uae_prefs *prefs)
{
	fixup_prefs_dim2 (&prefs->gfx_size_fs);
	fixup_prefs_dim2 (&prefs->gfx_size_win);
	if (prefs->gfx_apmode[1].gfx_vsync)
		prefs->gfx_apmode[1].gfx_vsyncmode = 1;

	for (int i = 0; i < 2; i++) {
		struct apmode *ap = &prefs->gfx_apmode[i];
		ap->gfx_vflip = 0;
		ap->gfx_strobo = false;
		if (ap->gfx_vsync) {
			if (ap->gfx_vsyncmode) {
				// low latency vsync: no flip only if no-buffer
				if (ap->gfx_backbuffers >= 1)
					ap->gfx_vflip = 1;
				if (!i && ap->gfx_backbuffers == 2)
					ap->gfx_vflip = 1;
				ap->gfx_strobo = prefs->lightboost_strobo;
			} else {
				// legacy vsync: always wait for flip
				ap->gfx_vflip = -1;
				if (prefs->gfx_api && ap->gfx_backbuffers < 1)
					ap->gfx_backbuffers = 1;
				if (ap->gfx_vflip)
					ap->gfx_strobo = prefs->lightboost_strobo;;
			}
		} else {
			// no vsync: wait if triple bufferirng
			if (ap->gfx_backbuffers >= 2)
				ap->gfx_vflip = -1;
		}
		if (prefs->gf[i].gfx_filter == 0 && ((prefs->gf[i].gfx_filter_autoscale && !prefs->gfx_api) || (prefs->gfx_apmode[APMODE_NATIVE].gfx_vsyncmode))) {
			prefs->gf[i].gfx_filter = 1;
		}
		if (i == 0) {
			if (prefs->gf[i].gfx_filter == 0 && prefs->monitoremu) {
				error_log(_T("A2024 and Graffiti require at least null filter enabled."));
				prefs->gf[i].gfx_filter = 1;
			}
			if (prefs->gf[i].gfx_filter == 0 && prefs->cs_cd32fmv) {
				error_log(_T("CD32 MPEG module overlay support require at least null filter enabled."));
				prefs->gf[i].gfx_filter = 1;
			}
		}
	}
}

void fixup_cpu (struct uae_prefs *p)
{
	if (p->cpu_frequency == 1000000)
		p->cpu_frequency = 0;

	if (p->cpu_model >= 68030 && p->address_space_24) {
		error_log (_T("24-bit address space is not supported in 68030/040/060 configurations."));
		p->address_space_24 = 0;
	}
	if (p->cpu_model < 68020 && p->fpu_model && (p->cpu_compatible || p->cpu_cycle_exact)) {
		error_log (_T("FPU is not supported in 68000/010 configurations."));
		p->fpu_model = 0;
	}

	switch (p->cpu_model)
	{
	case 68000:
		p->address_space_24 = 1;
		break;
	case 68010:
		p->address_space_24 = 1;
		break;
	case 68020:
		break;
	case 68030:
		break;
	case 68040:
		if (p->fpu_model)
			p->fpu_model = 68040;
		break;
	case 68060:
		if (p->fpu_model)
			p->fpu_model = 68060;
		break;
	}

	// 1 = "automatic" PPC config
	if (p->ppc_mode == 1) {
		p->cpuboard_type = BOARD_CSPPC;
		if (p->cs_compatible == CP_A1200) {
			p->cpuboard_type = BOARD_BLIZZARDPPC;
		} else if (p->cs_compatible != CP_A4000 && p->cs_compatible != CP_A4000T && p->cs_compatible != CP_A3000 && p->cs_compatible != CP_A3000T) {
			if ((p->cs_ide == IDE_A600A1200 || p->cs_pcmcia) && p->cs_mbdmac <= 0)
				p->cpuboard_type = BOARD_BLIZZARDPPC;
		}
		if (p->cpuboardmem1_size < 8 * 1024 * 1024)
			p->cpuboardmem1_size = 8 * 1024 * 1024;
	}

	if (p->cpu_model < 68020 && p->cachesize) {
		p->cachesize = 0;
		error_log (_T("JIT requires 68020 or better CPU."));
	}

	if (p->cpu_model >= 68040 && p->cachesize && p->cpu_compatible)
		p->cpu_compatible = false;

	if ((p->cpu_model < 68030 || p->cachesize) && p->mmu_model) {
		error_log (_T("MMU emulation requires 68030/040/060 and it is not JIT compatible."));
		p->mmu_model = 0;
	}

	if (p->cachesize && p->cpu_cycle_exact) {
		error_log (_T("JIT and cycle-exact can't be enabled simultaneously."));
		p->cachesize = 0;
	}
	if (p->cachesize && (p->fpu_no_unimplemented || p->int_no_unimplemented)) {
		error_log (_T("JIT is not compatible with unimplemented CPU/FPU instruction emulation."));
		p->fpu_no_unimplemented = p->int_no_unimplemented = false;
	}

#if 0
	if (p->cpu_cycle_exact && p->m68k_speed < 0 && currprefs.cpu_model <= 68020)
		p->m68k_speed = 0;
#endif

#if 0
	if (p->immediate_blits && p->blitter_cycle_exact) {
		error_log (_T("Cycle-exact and immediate blitter can't be enabled simultaneously.\n"));
		p->immediate_blits = false;
	}
#endif
	if (p->immediate_blits && p->waiting_blits) {
		error_log (_T("Immediate blitter and waiting blits can't be enabled simultaneously.\n"));
		p->waiting_blits = 0;
	}
	if (p->cpu_cycle_exact)
		p->cpu_compatible = true;

	if (p->cpuboard_type && !p->comptrustbyte) {
		error_log(_T("JIT direct is not compatible with emulated accelerator boards."));
		p->comptrustbyte = 1;
		p->comptrustlong = 1;
		p->comptrustlong = 1;
		p->comptrustnaddr = 1;
	}
	if (!p->jit_direct_compatible_memory && !p->comptrustbyte) {
		error_log(_T("JIT direct compatible memory option is disabled, disabling JIT direct."));
		p->comptrustbyte = 1;
		p->comptrustlong = 1;
		p->comptrustlong = 1;
		p->comptrustnaddr = 1;
	}
}

void fixup_prefs (struct uae_prefs *p)
{
	int err = 0;

	built_in_chipset_prefs (p);
	fixup_cpu (p);

	if (cpuboard_08000000(p))
		p->mbresmem_high_size = p->cpuboardmem1_size;

	if (((p->chipmem_size & (p->chipmem_size - 1)) != 0 && p->chipmem_size != 0x180000)
		|| p->chipmem_size < 0x20000
		|| p->chipmem_size > 0x800000)
	{
		error_log (_T("Unsupported chipmem size %d (0x%x)."), p->chipmem_size, p->chipmem_size);
		p->chipmem_size = 0x200000;
		err = 1;
	}

	if ((p->fastmem_size & (p->fastmem_size - 1)) != 0
		|| (p->fastmem_size != 0 && (p->fastmem_size < 0x10000 || p->fastmem_size > 0x800000)))
	{
		error_log (_T("Unsupported fastmem size %d (0x%x)."), p->fastmem_size, p->fastmem_size);
		p->fastmem_size = 0;
		err = 1;
	}
	if ((p->fastmem2_size & (p->fastmem2_size - 1)) != 0 || (p->fastmem_size + p->fastmem2_size) > 0x800000 + 262144
		|| (p->fastmem2_size != 0 && (p->fastmem2_size < 0x10000 || p->fastmem_size > 0x800000)))
	{
		error_log (_T("Unsupported fastmem2 size %d (0x%x)."), p->fastmem2_size, p->fastmem2_size);
		p->fastmem2_size = 0;
		err = 1;
	}
	if (p->fastmem2_size > p->fastmem_size) {
		error_log (_T("fastmem2 size can't be larger than fastmem1."));
		p->fastmem2_size = 0;
		err = 1;
	}

	if (p->rtgmem_size > max_z3fastmem && p->rtgmem_type == GFXBOARD_UAE_Z3) {
		error_log (_T("Graphics card memory size %d (0x%x) larger than maximum reserved %d (0x%x)."), p->rtgmem_size, p->rtgmem_size, max_z3fastmem, max_z3fastmem);
		p->rtgmem_size = max_z3fastmem;
		err = 1;
	}
	if ((p->rtgmem_size & (p->rtgmem_size - 1)) != 0 || (p->rtgmem_size != 0 && (p->rtgmem_size < 0x100000))) {
		error_log (_T("Unsupported graphics card memory size %d (0x%x)."), p->rtgmem_size, p->rtgmem_size);
		if (p->rtgmem_size > max_z3fastmem)
			p->rtgmem_size = max_z3fastmem;
		else
			p->rtgmem_size = 0;
		err = 1;
	}
	
	if (p->z3fastmem_size > max_z3fastmem) {
		error_log (_T("Zorro III fastmem size %d (0x%x) larger than max reserved %d (0x%x)."), p->z3fastmem_size, p->z3fastmem_size, max_z3fastmem, max_z3fastmem);
		p->z3fastmem_size = max_z3fastmem;
		err = 1;
	}
	if ((p->z3fastmem_size & (p->z3fastmem_size - 1)) != 0 || (p->z3fastmem_size != 0 && p->z3fastmem_size < 0x100000))
	{
		error_log (_T("Unsupported Zorro III fastmem size %d (0x%x)."), p->z3fastmem_size, p->z3fastmem_size);
		p->z3fastmem_size = 0;
		err = 1;
	}

	if (p->z3fastmem2_size > max_z3fastmem) {
		error_log (_T("Zorro III fastmem2 size %d (0x%x) larger than max reserved %d (0x%x)."), p->z3fastmem2_size, p->z3fastmem2_size, max_z3fastmem, max_z3fastmem);
		p->z3fastmem2_size = max_z3fastmem;
		err = 1;
	}
	if ((p->z3fastmem2_size & (p->z3fastmem2_size - 1)) != 0 || (p->z3fastmem2_size != 0 && p->z3fastmem2_size < 0x100000))
	{
		error_log (_T("Unsupported Zorro III fastmem2 size %x (%x)."), p->z3fastmem2_size, p->z3fastmem2_size);
		p->z3fastmem2_size = 0;
		err = 1;
	}

	p->z3fastmem_start &= ~0xffff;
	if (p->z3fastmem_start < 0x1000000)
		p->z3fastmem_start = 0x1000000;

	if (p->z3chipmem_size > max_z3fastmem) {
		error_log (_T("Zorro III fake chipmem size %d (0x%x) larger than max reserved %d (0x%x)."), p->z3chipmem_size, p->z3chipmem_size, max_z3fastmem, max_z3fastmem);
		p->z3chipmem_size = max_z3fastmem;
		err = 1;
	}
	if (((p->z3chipmem_size & (p->z3chipmem_size - 1)) != 0 &&  p->z3chipmem_size != 0x18000000 && p->z3chipmem_size != 0x30000000) || (p->z3chipmem_size != 0 && p->z3chipmem_size < 0x100000))
	{
		error_log (_T("Unsupported 32-bit chipmem size %d (0x%x)."), p->z3chipmem_size, p->z3chipmem_size);
		p->z3chipmem_size = 0;
		err = 1;
	}

	if (p->address_space_24 && (p->z3fastmem_size != 0 || p->z3fastmem2_size != 0 || p->z3chipmem_size != 0)) {
		p->z3fastmem_size = p->z3fastmem2_size = p->z3chipmem_size = 0;
		error_log (_T("Can't use a Z3 graphics card or 32-bit memory when using a 24 bit address space."));
	}

	if (p->bogomem_size != 0 && p->bogomem_size != 0x80000 && p->bogomem_size != 0x100000 && p->bogomem_size != 0x180000 && p->bogomem_size != 0x1c0000) {
		error_log (_T("Unsupported bogomem size %d (0x%x)"), p->bogomem_size, p->bogomem_size);
		p->bogomem_size = 0;
		err = 1;
	}

	if (p->bogomem_size > 0x180000 && (p->cs_fatgaryrev >= 0 || p->cs_ide || p->cs_ramseyrev >= 0)) {
		p->bogomem_size = 0x180000;
		error_log (_T("Possible Gayle bogomem conflict fixed."));
	}
	if (p->chipmem_size > 0x200000 && p->fastmem_size > 262144) {
		error_log(_T("You can't use fastmem and more than 2MB chip at the same time."));
		p->chipmem_size = 0x200000;
		err = 1;
	}
	if (p->chipmem_size > 0x200000 && p->rtgmem_size && !gfxboard_is_z3(p->rtgmem_type)) {
		error_log(_T("You can't use Zorro II RTG and more than 2MB chip at the same time."));
		p->chipmem_size = 0x200000;
		err = 1;
	}
	if (p->mbresmem_low_size > 0x04000000 || (p->mbresmem_low_size & 0xfffff)) {
		p->mbresmem_low_size = 0;
		error_log (_T("Unsupported Mainboard RAM size"));
	}
	if (p->mbresmem_high_size > 0x08000000 || (p->mbresmem_high_size & 0xfffff)) {
		p->mbresmem_high_size = 0;
		error_log (_T("Unsupported CPU Board RAM size."));
	}

	if (p->rtgmem_type >= GFXBOARD_HARDWARE) {
		if (p->rtgmem_size < gfxboard_get_vram_min (p->rtgmem_type))
			p->rtgmem_size = gfxboard_get_vram_min (p->rtgmem_type);
		if (p->address_space_24 && gfxboard_is_z3 (p->rtgmem_type)) {
			p->rtgmem_type = GFXBOARD_UAE_Z2;
			p->rtgmem_size = 0;
			error_log (_T("Z3 RTG and 24-bit address space are not compatible."));
		}
	}
	if (p->address_space_24 && p->rtgmem_size && p->rtgmem_type == GFXBOARD_UAE_Z3) {
		error_log (_T("Z3 RTG and 24bit address space are not compatible."));
		p->rtgmem_type = GFXBOARD_UAE_Z2;
	}
	if (p->rtgmem_type == GFXBOARD_UAE_Z2 && (p->chipmem_size > 2 * 1024 * 1024 || getz2size (p) > 8 * 1024 * 1024 || getz2size (p) < 0)) {
		p->rtgmem_size = 0;
		error_log (_T("Too large Z2 RTG memory size."));
	}

#if 0
	if (p->m68k_speed < -1 || p->m68k_speed > 20) {
		write_log (_T("Bad value for -w parameter: must be -1, 0, or within 1..20.\n"));
		p->m68k_speed = 4;
		err = 1;
	}
#endif

	if (p->produce_sound < 0 || p->produce_sound > 3) {
		error_log (_T("Bad value for -S parameter: enable value must be within 0..3."));
		p->produce_sound = 0;
		err = 1;
	}
	if (p->comptrustbyte < 0 || p->comptrustbyte > 3) {
		error_log (_T("Bad value for comptrustbyte parameter: value must be within 0..2."));
		p->comptrustbyte = 1;
		err = 1;
	}
	if (p->comptrustword < 0 || p->comptrustword > 3) {
		error_log (_T("Bad value for comptrustword parameter: value must be within 0..2."));
		p->comptrustword = 1;
		err = 1;
	}
	if (p->comptrustlong < 0 || p->comptrustlong > 3) {
		error_log (_T("Bad value for comptrustlong parameter: value must be within 0..2."));
		p->comptrustlong = 1;
		err = 1;
	}
	if (p->comptrustnaddr < 0 || p->comptrustnaddr > 3) {
		error_log (_T("Bad value for comptrustnaddr parameter: value must be within 0..2."));
		p->comptrustnaddr = 1;
		err = 1;
	}
	if (p->cachesize < 0 || p->cachesize > 16384) {
		error_log (_T("Bad value for cachesize parameter: value must be within 0..16384."));
		p->cachesize = 0;
		err = 1;
	}
	if ((p->z3fastmem_size || p->z3fastmem2_size || p->z3chipmem_size) && (p->address_space_24 || p->cpu_model < 68020)) {
		error_log (_T("Z3 fast memory can't be used with a 68000/68010 emulation. Turning off Z3 fast memory."));
		p->z3fastmem_size = 0;
		p->z3fastmem2_size = 0;
		p->z3chipmem_size = 0;
		err = 1;
	}
	if (p->rtgmem_size > 0 && p->rtgmem_type == GFXBOARD_UAE_Z3 && (p->cpu_model < 68020 || p->address_space_24)) {
		error_log (_T("UAEGFX RTG can't be used with a 68000/68010 or 68EC020 emulation. Turning off RTG."));
		p->rtgmem_size = 0;
		err = 1;
	}

#if !defined (BSDSOCKET)
	if (p->socket_emu) {
		write_log (_T("Compile-time option of BSDSOCKET_SUPPORTED was not enabled.  You can't use bsd-socket emulation.\n"));
		p->socket_emu = 0;
		err = 1;
	}
#endif

	if (p->nr_floppies < 0 || p->nr_floppies > 4) {
		error_log (_T("Invalid number of floppies.  Using 2."));
		p->nr_floppies = 2;
		p->floppyslots[0].dfxtype = 0;
		p->floppyslots[1].dfxtype = 0;
		p->floppyslots[2].dfxtype = -1;
		p->floppyslots[3].dfxtype = -1;
		err = 1;
	}
	if (p->floppy_speed > 0 && p->floppy_speed < 10) {
		error_log (_T("Invalid floppy speed."));
		p->floppy_speed = 100;
	}
	if (p->input_mouse_speed < 1 || p->input_mouse_speed > 1000) {
		error_log (_T("Invalid mouse speed."));
		p->input_mouse_speed = 100;
	}
	if (p->collision_level < 0 || p->collision_level > 3) {
		error_log (_T("Invalid collision support level.  Using 1."));
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
	} else if (p->cs_compatible == 0) {
		if (p->cs_ide == IDE_A4000) {
			if (p->cs_fatgaryrev < 0)
				p->cs_fatgaryrev = 0;
			if (p->cs_ramseyrev < 0)
				p->cs_ramseyrev = 0x0f;
		}
	}
	/* Can't fit genlock and A2024 or Graffiti at the same time,
	 * also Graffiti uses genlock audio bit as an enable signal
	 */
	if (p->genlock && p->monitoremu) {
		error_log (_T("Genlock and A2024 or Graffiti can't be active simultaneously."));
		p->genlock = false;
	}
	if (p->cs_hacks) {
		error_log (_T("chipset_hacks is nonzero (0x%04x)."), p->cs_hacks);
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
#if !defined (CPUEMU_11) && !defined (CPUEMU_13)
	p->cpu_compatible = 0;
	p->address_space_24 = 0;
#endif
#if !defined (CPUEMU_13)
	p->cpu_cycle_exact = p->blitter_cycle_exact = 0;
#endif
#ifndef AGA
	p->chipset_mask &= ~CSMASK_AGA;
#endif
#ifndef AUTOCONFIG
	p->z3fastmem_size = 0;
	p->fastmem_size = 0;
	p->rtgmem_size = 0;
#endif
#if !defined (BSDSOCKET)
	p->socket_emu = 0;
#endif
#if !defined (SCSIEMU)
	p->scsi = 0;
#ifdef _WIN32
	p->win32_aspi = 0;
#endif
#endif
#if !defined (SANA2)
	p->sana2 = 0;
#endif
#if !defined (UAESERIAL)
	p->uaeserial = 0;
#endif
#if defined (CPUEMU_13)
	if (p->cpu_cycle_exact) {
		if (p->gfx_framerate > 1) {
			error_log (_T("Cycle-exact requires disabled frameskip."));
			p->gfx_framerate = 1;
		}
		if (p->cachesize) {
			error_log (_T("Cycle-exact and JIT can't be active simultaneously."));
			p->cachesize = 0;
		}
#if 0
		if (p->m68k_speed) {
			error_log (_T("Adjustable CPU speed is not available in cycle-exact mode."));
			p->m68k_speed = 0;
		}
#endif
	}
#endif
	if (p->maprom && !p->address_space_24)
		p->maprom = 0x0f000000;
	if (((p->maprom & 0xff000000) && p->address_space_24) || (p->maprom && p->mbresmem_high_size == 0x08000000)) {
		p->maprom = 0x00e00000;
	}
	if (p->tod_hack && p->cs_ciaatod == 0)
		p->cs_ciaatod = p->ntscmode ? 2 : 1;

	built_in_chipset_prefs (p);
	blkdev_fix_prefs (p);
	target_fixup_options (p);
}

int quit_program = 0;
static int restart_program;
static TCHAR restart_config[MAX_DPATH];
static int default_config;

void uae_reset (int hardreset, int keyboardreset)
{
	if (debug_dma) {
		record_dma_reset ();
		record_dma_reset ();
	}
	currprefs.quitstatefile[0] = changed_prefs.quitstatefile[0] = 0;

	if (quit_program == 0) {
		quit_program = -UAE_RESET;
		if (keyboardreset)
			quit_program = -UAE_RESET_KEYBOARD;
		if (hardreset)
			quit_program = -UAE_RESET_HARD;
	}

}

void uae_quit (void)
{
	deactivate_debugger ();
	if (quit_program != -UAE_QUIT)
		quit_program = -UAE_QUIT;
	target_quit ();
}

/* 0 = normal, 1 = nogui, -1 = disable nogui */
void uae_restart (int opengui, const TCHAR *cfgfile)
{
	uae_quit ();
	restart_program = opengui > 0 ? 1 : (opengui == 0 ? 2 : 3);
	restart_config[0] = 0;
	default_config = 0;
	if (cfgfile)
		_tcscpy (restart_config, cfgfile);
	target_restart ();
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
		if (_tcsncmp (argv[i], _T("-cfgparam="), 10) == 0) {
			cfgfile_addcfgparam (argv[i] + 10);
		} else if (_tcscmp (argv[i], _T("-cfgparam")) == 0) {
			if (i + 1 == argc)
				write_log (_T("Missing argument for '-cfgparam' option.\n"));
			else
				cfgfile_addcfgparam (argv[++i]);
		}
	}
}

static int diskswapper_cb (struct zfile *f, void *vrsd)
{
	int *num = (int*)vrsd;
	if (*num >= MAX_SPARE_DRIVES)
		return 1;
	if (zfile_gettype (f) ==  ZFILE_DISKIMAGE) {
		_tcsncpy (currprefs.dfxlist[*num], zfile_getname (f), 255);
		(*num)++;
	}
	return 0;
}

static void parse_diskswapper (const TCHAR *s)
{
	TCHAR *tmp = my_strdup (s);
	const TCHAR *delim = _T(",");
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
		if (!zfile_zopen (p2, diskswapper_cb, &num)) {
			_tcsncpy (currprefs.dfxlist[num], p2, 255);
			num++;
		}
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
static TCHAR *parsetextpath (const TCHAR *s)
{
	TCHAR *s2 = parsetext (s);
	TCHAR *s3 = target_expand_environment (s2);
	xfree (s2);
	return s3;
}

static void parse_cmdline (int argc, TCHAR **argv)
{
	int i;
	bool firstconfig = true;

	for (i = 1; i < argc; i++) {
		if (!_tcsncmp (argv[i], _T("-diskswapper="), 13)) {
			TCHAR *txt = parsetextpath (argv[i] + 13);
			parse_diskswapper (txt);
			xfree (txt);
		} else if (_tcsncmp (argv[i], _T("-cfgparam="), 10) == 0) {
			;
		} else if (_tcscmp (argv[i], _T("-cfgparam")) == 0) {
			if (i + 1 < argc)
				i++;
		} else if (_tcsncmp (argv[i], _T("-config="), 8) == 0) {
			TCHAR *txt = parsetextpath (argv[i] + 8);
			currprefs.mountitems = 0;
			target_cfgfile_load (&currprefs, txt, firstconfig ? CONFIG_TYPE_ALL : CONFIG_TYPE_HARDWARE | CONFIG_TYPE_HOST | CONFIG_TYPE_NORESET, 0);
			xfree (txt);
			firstconfig = false;
		} else if (_tcsncmp (argv[i], _T("-statefile="), 11) == 0) {
			TCHAR *txt = parsetextpath (argv[i] + 11);
			savestate_state = STATE_DORESTORE;
			_tcscpy (savestate_fname, txt);
			xfree (txt);
		} else if (_tcscmp (argv[i], _T("-f")) == 0) {
			/* Check for new-style "-f xxx" argument, where xxx is config-file */
			if (i + 1 == argc) {
				write_log (_T("Missing argument for '-f' option.\n"));
			} else {
				TCHAR *txt = parsetextpath (argv[++i]);
				currprefs.mountitems = 0;
				target_cfgfile_load (&currprefs, txt, firstconfig ? CONFIG_TYPE_ALL : CONFIG_TYPE_HARDWARE | CONFIG_TYPE_HOST | CONFIG_TYPE_NORESET, 0);
				xfree (txt);
				firstconfig = false;
			}
		} else if (_tcscmp (argv[i], _T("-s")) == 0) {
			if (i + 1 == argc)
				write_log (_T("Missing argument for '-s' option.\n"));
			else
				cfgfile_parse_line (&currprefs, argv[++i], 0);
		} else if (_tcscmp (argv[i], _T("-h")) == 0 || _tcscmp (argv[i], _T("-help")) == 0) {
			usage ();
			exit (0);
		} else if (_tcsncmp (argv[i], _T("-cdimage="), 9) == 0) {
			TCHAR *txt = parsetextpath (argv[i] + 9);
			TCHAR *txt2 = xmalloc(TCHAR, _tcslen(txt) + 2);
			_tcscpy(txt2, txt);
			if (_tcsrchr(txt2, ',') != NULL)
				_tcscat(txt2, _T(","));
			cfgfile_parse_option (&currprefs, _T("cdimage0"), txt2, 0);
			xfree(txt2);
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

	_tcscpy (optionsfile, _T(""));

#ifdef OPTIONS_IN_HOME
	{
		TCHAR *home = getenv ("HOME");
		if (home != NULL && strlen (home) < 240)
		{
			_tcscpy (optionsfile, home);
			_tcscat (optionsfile, _T("/"));
		}
	}
#endif

	parse_cmdline_2 (argc, argv);

	_tcscat (optionsfile, restart_config);

	if (! target_cfgfile_load (&currprefs, optionsfile, CONFIG_TYPE_DEFAULT, default_config)) {
		write_log (_T("failed to load config '%s'\n"), optionsfile);
#ifdef OPTIONS_IN_HOME
		/* sam: if not found in $HOME then look in current directory */
		_tcscpy (optionsfile, restart_config);
		target_cfgfile_load (&currprefs, optionsfile, CONFIG_TYPE_DEFAULT, default_config);
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
	init_shm ();
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
#ifdef WITH_PPC
	uae_ppc_reset(false);
#endif
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

#ifdef _WIN32
#ifndef JIT
extern int DummyException (LPEXCEPTION_POINTERS blah, int n_except)
{
	return EXCEPTION_CONTINUE_SEARCH;
}
#endif
#endif

void do_start_program (void)
{
	if (quit_program == -UAE_QUIT)
		return;
	if (!canbang && candirect < 0)
		candirect = 0;
	if (canbang && candirect < 0)
		candirect = 1;
	/* Do a reset on startup. Whether this is elegant is debatable. */
	inputdevice_updateconfig (&changed_prefs, &currprefs);
	if (quit_program >= 0)
		quit_program = UAE_RESET;
#ifdef WITH_LUA
	uae_lua_loadall ();
#endif
#if (defined (_WIN32) || defined (_WIN64)) && !defined (NO_WIN32_EXCEPTION_HANDLER)
	extern int EvalException (LPEXCEPTION_POINTERS blah, int n_except);
	__try
#endif
	{
		m68k_go (1);
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
	a3000scsi_free ();
#endif
#ifdef NCR
	ncr710_free();
	ncr_free();
#endif
#ifdef NCR9X
	ncr9x_free();
#endif
#ifdef CD32
	akiko_free ();
	cd32_fmv_free();
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
	gayle_free ();
	device_func_reset ();
#ifdef WITH_LUA
	uae_lua_free ();
#endif
	savestate_free ();
	memory_cleanup ();
	free_shm ();
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


void virtualdevice_init (void)
{
#ifdef AUTOCONFIG
	rtarea_setup ();
#endif
#ifdef FILESYS
	rtarea_init ();
	uaeres_install ();
	hardfile_install ();
#endif
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
#ifdef AUTOCONFIG
	expansion_init ();
	emulib_install ();
	uaeexe_install ();
#endif
#ifdef FILESYS
	filesys_install ();
#endif
#if defined (BSDSOCKET)
	bsdlib_install ();
#endif
#ifdef WITH_UAENATIVE
	uaenative_install ();
#endif
#ifdef WITH_TABLETLIBRARY
	tabletlib_install ();
#endif
#ifdef NCR
	ncr710_init();
	ncr_init();
#endif
#ifdef NCR9X
	ncr9x_init();
#endif
}

static int real_main2 (int argc, TCHAR **argv)
{

#ifdef USE_SDL
	SDL_Init (SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_NOPARACHUTE);
#endif
	set_config_changed ();
	if (restart_config[0]) {
		default_prefs (&currprefs, 0);
		fixup_prefs (&currprefs);
	}

	if (! graphics_setup ()) {
		exit (1);
	}

#ifdef NATMEM_OFFSET
	//preinit_shm ();
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
		write_log (_T("Sound driver unavailable: Sound output disabled\n"));
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
		set_config_changed ();
		if (err == -1) {
			write_log (_T("Failed to initialize the GUI\n"));
			return -1;
		} else if (err == -2) {
			return 1;
		}
	}

	memset (&gui_data, 0, sizeof gui_data);
	gui_data.cd = -1;
	gui_data.hd = -1;
	gui_data.md = -1;
	logging_init (); /* Yes, we call this twice - the first case handles when the user has loaded
						 a config using the cmd-line.  This case handles loads through the GUI. */

#ifdef NATMEM_OFFSET
	init_shm ();
#endif
#ifdef WITH_LUA
	uae_lua_init ();
#endif
#ifdef PICASSO96
	picasso_reset ();
#endif

#if 0
#ifdef JIT
	if (!(currprefs.cpu_model >= 68020 && currprefs.address_space_24 == 0 && currprefs.cachesize))
		canbang = 0;
#endif
#endif

	fixup_prefs (&currprefs);
#ifdef RETROPLATFORM
	rp_fixup_options (&currprefs);
#endif
	changed_prefs = currprefs;
	target_run ();
	/* force sound settings change */
	currprefs.produce_sound = 0;

	savestate_init ();
	keybuf_init (); /* Must come after init_joystick */

	memory_hardreset (2);
	memory_reset ();

#ifdef AUTOCONFIG
	native2amiga_install ();
#endif
	custom_init (); /* Must come after memory_init */
#ifdef SERIAL_PORT
	serial_init ();
#endif
	DISK_init ();
#ifdef WITH_PPC
	uae_ppc_reset(true);
#endif

	reset_frame_rate_hack ();
	init_m68k (); /* must come after reset_frame_rate_hack (); */

	gui_update ();

	if (graphics_init (true)) {
		setup_brkhandler ();
		if (currprefs.start_debugger && debuggable ())
			activate_debugger ();

		if (!init_audio ()) {
			if (sound_available && currprefs.produce_sound > 1) {
				write_log (_T("Sound driver unavailable: Sound output disabled\n"));
			}
			currprefs.produce_sound = 0;
		}
		start_program ();
	}
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
