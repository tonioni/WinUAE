#include "sysconfig.h"
#include "sysdeps.h"

#include <stdarg.h>
#include <stdio.h>
#include <string>

#include "options.h"
#include "traps.h"
#include "memory.h"
#include "autoconf.h"
#include "audio.h"
#include "cpuboard.h"
#include "debug.h"
#include "filesys.h"
#include "fsdb.h"
#include "gfxboard.h"
#include "inputdevice.h"
#include "keyboard.h"
#include "newcpu.h"
#include "fpp.h"
#include "readcpu.h"
#include "rommgr.h"
#include "savestate.h"
#include "sampler.h"
#include "sana2.h"
#include "scsidev.h"
#include "statusline.h"
#include "ethernet.h"
#include "uae/string.h"
#include "videograb.h"
#include "zfile.h"
#include "zarchive.h"

int consoleopen;
int log_scsi;
int log_net;
int log_vsync;
int debug_vsync_min_delay;
int debug_vsync_forced_delay;
int uaelib_debug;
int pissoff_value = 15000 * CYCLE_UNIT;
int multithread_enabled = 1;
int p96syncrate = 312;
int p96refresh_active;
int max_uae_width = 8192;
int max_uae_height = 8192;
int pissoff_nojit_value = 160 * CYCLE_UNIT;
#ifndef BSDSOCKET
volatile int bsd_int_requested;
#endif

void machdep_free(void) {}
#ifndef WINUAE_UNIX_WITH_VIDEOGRAB
void pausevideograb(int) {}
bool getpausevideograb(void) { return false; }
uae_s64 getsetpositionvideograb(uae_s64) { return -1; }
#endif
#ifndef WINUAE_UNIX_WITH_SAMPLER
int sampler_init(void) { return 0; }
void sampler_free(void) {}
void sampler_vsync(void) {}
uae_u8 sampler_getsample(int) { return 0; }
float sampler_evtime;
int unix_sampler_device_count(void) { return 0; }
const TCHAR *unix_sampler_device_name(int) { return _T(""); }
const TCHAR *unix_sampler_device_config_name(int) { return _T(""); }
int unix_sampler_device_index_from_config_name(const TCHAR *) { return -1; }
#endif
int audio_is_pull(void) { return 0; }
bool audio_is_pull_event(void) { return false; }
int audio_pull_buffer(void) { return 0; }
bool audio_finish_pull(void) { return false; }
void save_log_open(void) {}
void statusline_updated(int) {}
#ifndef BSDSOCKET
void bsdsock_fake_int_handler(void) { bsd_int_requested = 0; }
#endif

#ifdef DEBUGGER
extern void unix_gui_debugger_update_info(const TCHAR *text);

static void append_debugger_info(std::string &info, const char *format, ...)
{
	char buffer[4096];
	va_list ap;

	va_start(ap, format);
	const int len = vsnprintf(buffer, sizeof buffer, format, ap);
	va_end(ap);
	if (len <= 0) {
		return;
	}
	info.append(buffer, len < int(sizeof buffer) ? len : int(sizeof buffer) - 1);
}

static const TCHAR *debugger_opcode_name(uae_u16 opcode)
{
	if (!table68k) {
		return _T("?");
	}
	const struct instr *dp = table68k + opcode;
	for (struct mnemolookup *lookup = lookuptab; lookup->name; lookup++) {
		if (lookup->mnemo == dp->mnemo) {
			return lookup->name;
		}
	}
	return _T("?");
}

static void append_debugger_areg(std::string &info, int reg)
{
	TCHAR mem[MAX_LINEWIDTH + 1];

	mem[0] = 0;
	dumpmem2(m68k_areg(regs, reg), mem, sizeof mem / sizeof mem[0]);
	const TCHAR *memtext = mem;
	if (_tcslen(memtext) > 9) {
		memtext += 9;
	}
	append_debugger_info(
		info,
		"A%d: %08X  %s\n",
		reg,
		m68k_areg(regs, reg),
		memtext);
}

void update_debug_info(void)
{
	if (!debugger_active) {
		return;
	}

	std::string info;
	info.reserve(8192);

	append_debugger_info(info, "CPU\n");
	append_debugger_info(info, "PC:  %08X\n", m68k_getpc());
	append_debugger_info(info, "USP: %08X\n", regs.usp);
	append_debugger_info(info, "ISP: %08X\n", regs.isp);
	append_debugger_info(info, "SR:  %04X\n\n", regs.sr);

	append_debugger_info(info, "Data registers\n");
	for (int i = 0; i < 8; i++) {
		append_debugger_info(info, "D%d: %08X\n", i, m68k_dreg(regs, i));
	}

	append_debugger_info(info, "\nAddress registers\n");
	for (int i = 0; i < 8; i++) {
		append_debugger_areg(info, i);
	}

	append_debugger_info(info, "\nCCR\n");
	append_debugger_info(info, "X: %d  N: %d  Z: %d  V: %d  C: %d\n",
		GET_XFLG() ? 1 : 0,
		GET_NFLG() ? 1 : 0,
		GET_ZFLG() ? 1 : 0,
		GET_VFLG() ? 1 : 0,
		GET_CFLG() ? 1 : 0);

	append_debugger_info(info, "\nStatus\n");
	append_debugger_info(info, "T:     %d%d\n", regs.t1, regs.t0);
	append_debugger_info(info, "S:     %d\n", regs.s);
	append_debugger_info(info, "M:     %d\n", regs.m);
	append_debugger_info(info, "IMASK: %d\n", regs.intmask);
	append_debugger_info(info, "STP:   %d\n", regs.stopped);

	append_debugger_info(info, "\nPrefetch\n");
	append_debugger_info(
		info,
		"%04X (%s)  %04X (%s)\n",
		regs.irc,
		debugger_opcode_name(regs.irc),
		regs.ir,
		debugger_opcode_name(regs.ir));

	append_debugger_info(info, "\nControl registers\n");
	for (int i = 0; m2cregs[i].regno >= 0; i++) {
		if (!movec_illg(m2cregs[i].regno)) {
			append_debugger_info(
				info,
				"%-4s %08X\n",
				m2cregs[i].regname,
				val_move2c(m2cregs[i].regno));
		}
	}

	append_debugger_info(info, "\nFPU\n");
	for (int i = 0; i < 8; i++) {
		const TCHAR *fp = fpp_print ? fpp_print(&regs.fp[i], 0) : _T("-");
		append_debugger_info(info, "FP%d: %s\n", i, fp ? fp : _T("-"));
	}

	const uae_u32 fpsr = fpp_get_fpsr();
	append_debugger_info(info, "\nFPSR\n");
	append_debugger_info(info, "N:   %d\n", (fpsr & 0x08000000) != 0 ? 1 : 0);
	append_debugger_info(info, "Z:   %d\n", (fpsr & 0x04000000) != 0 ? 1 : 0);
	append_debugger_info(info, "I:   %d\n", (fpsr & 0x02000000) != 0 ? 1 : 0);
	append_debugger_info(info, "NAN: %d\n", (fpsr & 0x01000000) != 0 ? 1 : 0);

	unix_gui_debugger_update_info(info.c_str());
}
#else
void update_debug_info(void) {}
#endif

#ifndef GFXBOARD
void gfxboard_vsync_handler(bool, bool) {}
#endif

#ifndef WITH_CPUBOARD
bool cpuboard_autoconfig_init(struct autoconfig_info *) { return false; }
bool cpuboard_maprom(void) { return false; }
void cpuboard_map(void) {}
void cpuboard_reset(int) {}
void cpuboard_rethink(void) {}
void cpuboard_cleanup(void) {}
void cpuboard_init(void) {}
void cpuboard_clear(void) {}
int cpuboard_memorytype(struct uae_prefs *) { return 0; }
int cpuboard_maxmemory(struct uae_prefs *) { return 0; }
bool cpuboard_32bit(struct uae_prefs *) { return false; }
bool cpuboard_io_special(int, uae_u32 *, int, bool) { return false; }
void cpuboard_overlay_override(void) {}
uaecptr cpuboard_get_reset_pc(uaecptr *) { return 0; }
void cpuboard_set_flash_unlocked(bool) {}
bool cpuboard_forced_hardreset(void) { return false; }
bool cpuboard_fc_check(uaecptr, uae_u32 *, int, bool) { return false; }
void cpuboard_gvpmaprom(int) {}
#endif
#ifndef WITH_CPUBOARD
void cyberstorm_scsi_ram_put(uaecptr, uae_u32) {}
uae_u32 cyberstorm_scsi_ram_get(uaecptr) { return 0; }
int REGPARAM2 cyberstorm_scsi_ram_check(uaecptr, uae_u32) { return 0; }
uae_u8 *REGPARAM2 cyberstorm_scsi_ram_xlate(uaecptr) { return NULL; }
void cyberstorm_mk3_ppc_irq(int, int) {}
void blizzardppc_irq(int, int) {}
void cyberstorm_mk3_ppc_irq_setonly(int, int) {}
#endif
#ifndef WITH_PCI
void wildfire_ncr815_irq(int, int) {}
#endif

struct netdriverdata **target_ethernet_enumerate(void)
{
#if defined(WITH_SLIRP) || defined(WITH_UAENET_PCAP)
    static netdriverdata *drivers[MAX_TOTAL_NET_DEVICES];
    memset(drivers, 0, sizeof drivers);
    ethernet_enumerate(drivers, 0);
    return drivers;
#else
    static netdriverdata *none[1] = { NULL };
    return none;
#endif
}

#ifndef WITH_UAENET_PCAP
void ethernet_pause(int) {}
void ethernet_reset(void) {}
#endif

#ifndef WITH_PCI
bool ariadne2_init(struct autoconfig_info *) { return false; }
bool hydra_init(struct autoconfig_info *) { return false; }
bool lanrover_init(struct autoconfig_info *) { return false; }
bool xsurf_init(struct autoconfig_info *) { return false; }
bool xsurf100_init(struct autoconfig_info *) { return false; }
#endif

#ifndef WINUAE_UNIX_WITH_ARCHIVES
struct zvolume *archive_directory_plain(struct zfile *) { return NULL; }
struct zvolume *archive_directory_lha(struct zfile *) { return NULL; }
struct zvolume *archive_directory_zip(struct zfile *) { return NULL; }
struct zvolume *archive_directory_7z(struct zfile *) { return NULL; }
struct zfile *archive_access_7z(struct znode *) { return NULL; }
struct zvolume *archive_directory_rar(struct zfile *) { return NULL; }
struct zfile *archive_access_rar(struct znode *) { return NULL; }
struct zvolume *archive_directory_lzx(struct zfile *) { return NULL; }
struct zfile *archive_access_lzx(struct znode *) { return NULL; }
struct zvolume *archive_directory_arcacc(struct zfile *, unsigned int) { return NULL; }
struct zvolume *archive_directory_adf(struct znode *, struct zfile *) { return NULL; }
struct zvolume *archive_directory_rdb(struct zfile *) { return NULL; }
struct zvolume *archive_directory_fat(struct zfile *) { return NULL; }
struct zvolume *archive_directory_tar(struct zfile *) { return NULL; }
struct zfile *archive_access_select(struct znode *, struct zfile *, unsigned int, int, int *retcode, int)
{
    if (retcode) {
        *retcode = 0;
    }
    return NULL;
}
void archive_access_scan(struct zfile *, zfile_callback, void *, unsigned int) {}
void archive_access_close(void *, unsigned int) {}
struct zfile *archive_unpackzfile(struct zfile *zf) { return zf; }
struct zfile *archive_access_lha(struct znode *) { return NULL; }
struct zfile *archive_getzfile(struct znode *, unsigned int, int) { return NULL; }
int isfat(uae_u8 *) { return 0; }
#endif

#ifndef SCSIEMU
int scsi_do_disk_change(int, int, int *pollmode) { if (pollmode) *pollmode = 0; return 0; }
uae_u32 scsi_get_cd_drive_mask(void) { return 0; }
uae_u32 scsi_get_cd_drive_media_mask(void) { return 0; }
int scsi_add_tape(struct uaedev_config_info *) { return -1; }
uae_u8 *save_scsidev(int, size_t *len, uae_u8 *dst) { if (len) *len = 0; return dst; }
uae_u8 *restore_scsidev(uae_u8 *src) { return src; }
#endif

a_inode *custom_fsdb_lookup_aino_aname(a_inode *, const TCHAR *) { return NULL; }
a_inode *custom_fsdb_lookup_aino_nname(a_inode *, const TCHAR *) { return NULL; }
int custom_fsdb_used_as_nname(a_inode *, const TCHAR *) { return 0; }

void filesys_addexternals(void) {}
int target_get_volume_name(struct uaedev_mount_info *, struct uaedev_config_info *, bool, bool, int) { return 0; }
uae_u8 *target_load_keyfile(struct uae_prefs *, const TCHAR *, int *size, TCHAR *)
{
    if (size) {
        *size = 0;
    }
    return NULL;
}
uae_u32 emulib_target_getcpurate(uae_u32, uae_u32 *low)
{
    if (low) {
        *low = 0;
    }
    return 0;
}
int is_touch_lightpen(void) { return 0; }
