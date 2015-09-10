#include "sysconfig.h"
#include "sysdeps.h"

#include "devices.h"

#include "options.h"
#include "threaddep/thread.h"
#include "traps.h"
#include "memory.h"
#include "audio.h"
#include "a2091.h"
#include "a2065.h"
#include "gfxboard.h"
#include "ncr_scsi.h"
#include "ncr9x_scsi.h"
#include "scsi.h"
#include "scsidev.h"
#include "sana2.h"
#include "clipboard.h"
#include "cpuboard.h"
#include "sndboard.h"
#include "statusline.h"
#include "uae/ppc.h"
#include "cd32_fmv.h"
#include "cdtv.h"
#include "cdtvcr.h"
#include "akiko.h"
#include "gayle.h"
#include "idecontrollers.h"
#include "disk.h"
#include "cia.h"
#include "inputdevice.h"
#include "picasso96.h"
#include "blkdev.h"
#include "parallel.h"
#include "picasso96.h"
#include "autoconf.h"
#include "sampler.h"
#include "newcpu.h"
#include "blitter.h"
#include "xwin.h"
#include "custom.h"
#include "serial.h"
#include "bsdsocket.h"
#include "uaeserial.h"
#include "uaeresource.h"
#include "native2amiga.h"
#include "dongle.h"
#include "gensound.h"
#include "gui.h"
#include "savestate.h"
#include "uaeexe.h"
#include "uaenative.h"
#include "tabletlibrary.h"
#include "luascript.h"
#include "driveclick.h"
#include "pci.h"
#include "pci_hw.h"
#include "x86.h"
#include "ethernet.h"
#ifdef RETROPLATFORM
#include "rp.h"
#endif

void devices_reset(int hardreset)
{
	gayle_reset (hardreset);
	idecontroller_reset();
	a1000_reset ();
	DISK_reset ();
	CIA_reset ();
	gayle_reset (0);
	soft_scsi_reset();
#ifdef A2091
	a2091_reset ();
	gvp_reset ();
#endif
#ifdef GFXBOARD
	gfxboard_reset ();
#endif
#ifdef WITH_TOCCATA
	sndboard_reset();
#endif
#ifdef NCR
	ncr_reset();
#endif
#ifdef NCR9X
	ncr9x_reset();
#endif
#ifdef WITH_PCI
	pci_reset();
#endif
#ifdef WITH_X86
	x86_bridge_reset();
#endif
#ifdef JIT
	compemu_reset ();
#endif
#ifdef AUTOCONFIG
	expamem_reset ();
#endif
}


void devices_vsync_pre(void)
{
	audio_vsync ();
	blkdev_vsync ();
	CIA_vsync_prehandler ();
	inputdevice_vsync ();
	filesys_vsync ();
	sampler_vsync ();
	clipboard_vsync ();
#ifdef RETROPLATFORM
	rp_vsync ();
#endif
#ifdef CD32
	cd32_fmv_vsync_handler();
#endif
	cpuboard_vsync();
	statusline_vsync();
#ifdef WITH_X86
	x86_bridge_vsync();
#endif
}

void devices_vsync_post(void)
{
#ifdef GFXBOARD
	if (!picasso_on)
		gfxboard_vsync_handler ();
#endif
#ifdef WITH_TOCCATA
	sndboard_vsync();
#endif
}

void devices_hsync(void)
{
#ifdef GFXBOARD
	gfxboard_hsync_handler();
#endif
#ifdef A2065
	a2065_hsync_handler ();
#endif
#ifdef CD32
	AKIKO_hsync_handler ();
	cd32_fmv_hsync_handler();
#endif
#ifdef CDTV
	CDTV_hsync_handler ();
	CDTVCR_hsync_handler ();
#endif
	decide_blitter (-1);

#ifdef PICASSO96
	picasso_handle_hsync ();
#endif
#ifdef AHI
	{
		void ahi_hsync (void);
		ahi_hsync ();
	}
#endif
#ifdef WITH_PPC
	uae_ppc_hsync_handler();
	cpuboard_hsync();
#endif
#ifdef WITH_PCI
	pci_hsync();
#endif
#ifdef WITH_X86
	x86_bridge_hsync();
#endif
#ifdef WITH_TOCCATA
	sndboard_hsync();
#endif
	DISK_hsync ();
	audio_hsync ();
	CIA_hsync_prehandler ();
	serial_hsynchandler ();
	gayle_hsync ();
	idecontroller_hsync();
#ifdef A2091
	scsi_hsync ();
#endif
}

void devices_rethink(void)
{
	rethink_cias ();
#ifdef A2065
	rethink_a2065 ();
#endif
#ifdef A2091
	rethink_a2091 ();
#endif
#ifdef CDTV
	rethink_cdtv();
	rethink_cdtvcr();
#endif
#ifdef CD32
	rethink_akiko ();
	rethink_cd32fmv();
#endif
#ifdef NCR
	ncr_rethink();
#endif
#ifdef NCR9X
	ncr9x_rethink();
#endif
	ncr80_rethink();
#ifdef WITH_PCI
	pci_rethink();
#endif
#ifdef WITH_X86
	x86_bridge_rethink();
#endif
#ifdef WITH_TOCCATA
	sndboard_rethink();
#endif
	rethink_gayle ();
	idecontroller_rethink();
	/* cpuboard_rethink must be last */
	cpuboard_rethink();
}

void devices_update_sound(double clk, double syncadjust)
{
	update_sound (clk);
	update_sndboard_sound (clk / syncadjust);
	update_cda_sound(clk / syncadjust);
}

void devices_update_sync(double svpos, double syncadjust)
{
	cd32_fmv_set_sync(svpos, syncadjust);
}

void reset_all_systems (void)
{
	init_eventtab ();

#ifdef WITH_PPC
	uae_ppc_reset(is_hardreset());
#endif
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
#ifdef WITH_PCI
	pci_reset();
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
	cdtv_free();
	cdtvcr_free();
#endif
#ifdef A2091
	a2091_free ();
	gvp_free ();
	a3000scsi_free ();
#endif
	soft_scsi_free();
#ifdef NCR
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
#ifdef WITH_PCI
	pci_free();
#endif
#ifdef WITH_X86
	x86_bridge_free();
#endif
#ifdef FILESYS
	filesys_cleanup ();
#endif
#ifdef BSDSOCKET
	bsdlib_reset ();
#endif
	gayle_free ();
	idecontroller_free();
	device_func_reset ();
#ifdef WITH_LUA
	uae_lua_free ();
#endif
#ifdef WITH_PPC
	uae_ppc_free();
#endif
#ifdef WITH_TOCCATA
	sndboard_free();
#endif
	gfxboard_free();
	savestate_free ();
	memory_cleanup ();
	free_shm ();
	cfgfile_addcfgparam (0);
	machdep_free ();
	driveclick_free();
	ethernet_enumerate_free();
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
	ncr_init();
#endif
#ifdef NCR9X
	ncr9x_init();
#endif
#ifdef CDTV
	cdtvcr_reset();
#endif
}

void devices_restore_start(void)
{
	restore_cia_start();
	restore_blkdev_start();
	changed_prefs.bogomem_size = 0;
	changed_prefs.chipmem_size = 0;
	changed_prefs.fastmem_size = 0;
	changed_prefs.z3fastmem_size = 0;
	changed_prefs.z3fastmem2_size = 0;
	changed_prefs.mbresmem_low_size = 0;
	changed_prefs.mbresmem_high_size = 0;
}

void devices_syncchange(void)
{
	x86_bridge_sync_change();
}