#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <stdlib.h>

struct addrbank;

#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "inputdevice.h"
#include "autoconf.h"
#include "fsdb.h"
#include "uae/slirp.h"
#include "driveclick.h"
#include "pci_hw.h"
#include "blkdev.h"

struct PPCD_CB;

int avioutput_enabled = 0;
bool beamracer_debug = false;
int bsd_int_requested = 0;
int busywait = 0; 
int key_swap_hack = 0;
int seriallog = 0;
int log_vsync, debug_vsync_min_delay, debug_vsync_forced_delay;
bool is_dsp_installed = false;
int tablet_log = 0;
int log_scsi = 0;
int uaelib_debug;

uae_u8* start_pc_p = nullptr;
uae_u32 start_pc = 0;
uae_u8 *cubo_nvram = nullptr;

int dos_errno(void) {
    return errno;
}

void pausevideograb(int) { 
    UNIMPLEMENTED();
}

void show_screen(int monid, int mode) {
    TRACE();
}

// from fs-uae
void vsync_clear() {
    UNIMPLEMENTED();
}

int vsync_isdone(long* dt) {
    TRACE();
    return 1;
}

bool target_osd_keyboard(int show) {
    UNIMPLEMENTED();
    return false;
}

bool specialmonitor_need_genlock() {
    return false;
}

void setmouseactive(int, int) {
    UNIMPLEMENTED();
}

void screenshot(int monid, int,int) {
    UNIMPLEMENTED();
}

int same_aname(const TCHAR* an1, const TCHAR* an2) {
    UNIMPLEMENTED();
    return 0;
}

int input_get_default_keyboard(int i) {
    TRACE();
    //UNIMPLEMENTED();
    return 0;
}

uae_s64 getsetpositionvideograb(uae_s64 framepos) {
    UNIMPLEMENTED();
    return 0;
}

// Dummy initialization function
static int dummy_init(void) {
    return 1;
    //*((volatile int*)0) = 0;
    //UNIMPLEMENTED();
    //return 0; // Return 0 for success, -1 for failure
}

// Dummy closing function
static void dummy_close(void) {
    UNIMPLEMENTED();
}

// Dummy function to acquire an input device
static int dummy_acquire(int device_id, int exclusive) {
    UNIMPLEMENTED();
    return 0; // Return 0 for success, -1 for failure
}

// Dummy function to release/unacquire an input device
static void dummy_unacquire(int device_id) {
    printf("Unacquiring input device %d\n", device_id);
}

// Dummy function to read input from the device
static void dummy_read(void) {
    //printf("Reading input from device\n");
}

// Dummy function to get the number of input devices
static int dummy_get_num(void) {
    //TRACE();
    return 0;
}

// Dummy function to get the friendly name of an input device
static TCHAR* dummy_get_friendlyname(int device_id) {
    UNIMPLEMENTED();
    return nullptr;
}

// Dummy function to get the unique name of an input device
static TCHAR* dummy_get_uniquename(int device_id) {
    UNIMPLEMENTED();
    return nullptr;
}

// Dummy function to get the number of widgets (input elements) in an input device
static int dummy_get_widget_num(int device_id) {
    UNIMPLEMENTED();
    return 4; // Return the number of widgets
}

// Dummy function to get the type and name of a widget
static int dummy_get_widget_type(int device_id, int widget_id, TCHAR* widget_name, uae_u32* widget_type) {
    UNIMPLEMENTED();
    return 0; // Return 0 for success, -1 for failure
}

// Dummy function to get the first widget (input element) in an input device
static int dummy_get_widget_first(int device_id, int widget_type) {
    UNIMPLEMENTED();
    return 0;
}

// Dummy function to get the flags of an input device
int dummy_get_flags(int device_id) {
    return 0; // Return flags (if any) for the input device
}

struct inputdevice_functions inputdevicefunc_mouse = {
    dummy_init, 
    dummy_close, 
    dummy_acquire, 
    dummy_unacquire, 
    dummy_read, 
    dummy_get_num, 
    dummy_get_friendlyname, 
    dummy_get_uniquename, 
    dummy_get_widget_num, 
    dummy_get_widget_type, 
    dummy_get_widget_first, 
    dummy_get_flags
};

struct inputdevice_functions inputdevicefunc_keyboard = {
    dummy_init, 
    dummy_close, 
    dummy_acquire, 
    dummy_unacquire, 
    dummy_read, 
    dummy_get_num, 
    dummy_get_friendlyname, 
    dummy_get_uniquename, 
    dummy_get_widget_num, 
    dummy_get_widget_type, 
    dummy_get_widget_first, 
    dummy_get_flags
};

struct inputdevice_functions inputdevicefunc_joystick = {
    dummy_init, 
    dummy_close, 
    dummy_acquire, 
    dummy_unacquire, 
    dummy_read, 
    dummy_get_num, 
    dummy_get_friendlyname, 
    dummy_get_uniquename, 
    dummy_get_widget_num, 
    dummy_get_widget_type, 
    dummy_get_widget_first, 
    dummy_get_flags
};

const TCHAR* my_getfilepart(const TCHAR* filename) {
    UNIMPLEMENTED();
    return nullptr;
}

void fetch_statefilepath(TCHAR* out, int size) {
    UNIMPLEMENTED();
}

uae_u32 cpuboard_ncr9x_scsi_get(uaecptr addr) {
    UNIMPLEMENTED();
    return 0;
}

void cpuboard_ncr9x_scsi_put(uaecptr addr, uae_u32 v) {
    UNIMPLEMENTED();
}

void getfilepart(TCHAR* out, int size, const TCHAR* path) {
    UNIMPLEMENTED();
}

void toggle_fullscreen(int monid, int) {
    UNIMPLEMENTED();
}

const TCHAR* target_get_display_name(int, bool) {
    TRACE();
    return "Amiga";
    //UNIMPLEMENTED();
    //return nullptr;
}

extern int target_get_display(const TCHAR*) {
    UNIMPLEMENTED();
    return 0;
}

int target_cfgfile_load(struct uae_prefs* p, const TCHAR* filename, int type, int isdefault) {
    TRACE();
    return 1;
}

void target_addtorecent(const TCHAR* name, int t) {
    UNIMPLEMENTED();
}

int my_truncate(const TCHAR* name, uae_u64 len) {
    UNIMPLEMENTED();
    return 0;
}

bool my_issamepath(const TCHAR* path1, const TCHAR *path2) {
    UNIMPLEMENTED();
    return false;
}

int input_get_default_joystick (struct uae_input_device *uid, int i, int port, int af, int mode, bool gp, bool joymouseswap) {
    UNIMPLEMENTED();
    return 0;
}

bool get_plugin_path (TCHAR *out, int len, const TCHAR *path) {
    UNIMPLEMENTED();
    return false;
}

void getgfxoffset(int monid, float* dxp, float* dyp, float* mxp, float* myp) {
    UNIMPLEMENTED();
}

void fixtrailing(TCHAR* p) {
    UNIMPLEMENTED();
}

void unlockscr(struct vidbuffer *vb, int y_start, int y_end) {
    UNIMPLEMENTED();
}

int uae_slirp_redir(int is_udp, int host_port, struct in_addr guest_addr, int guest_port) {
    UNIMPLEMENTED();
    return 0;
}

int translate_message(int msg,	TCHAR* out) {
    UNIMPLEMENTED();
    return 0;
}

bool toggle_rtg(int monid, int mode) {
    UNIMPLEMENTED();
    return false;
}

struct netdriverdata** target_ethernet_enumerate() {
    UNIMPLEMENTED();
    return nullptr;
}

void refreshtitle() {
    UNIMPLEMENTED();
}

bool render_screen(int, int, bool) {
    TRACE();
    return true;
}

bool my_utime(const TCHAR* name, struct mytimeval* tv) {
    UNIMPLEMENTED();
    return false;
}

bool my_resolvesoftlink(char*, int, bool) {
    TRACE();
    return true;
}

int my_rename(char const*, char const*) {
    UNIMPLEMENTED();
    return 0;
}

void masoboshi_ncr9x_scsi_put(unsigned int, unsigned int, int) {
    UNIMPLEMENTED();
}

struct autoconfig_info;
      
bool isa_expansion_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return false;
}

int gfxboard_get_romtype(rtgboardconfig*) {
    TRACE();
    //UNIMPLEMENTED();
    return 0;
}
      
void getpathpart(char*, int, char const*) {
    UNIMPLEMENTED();
}

uae_u8* save_log(int, unsigned long*) {
    UNIMPLEMENTED();
    return nullptr;
}

int my_unlink (const TCHAR* name, bool dontrecycle) {
    UNIMPLEMENTED();
    return 0;
}

struct fs_usage;
      
int get_fs_usage(char const*, char const*, fs_usage*) {
    UNIMPLEMENTED();
    return 0;
}

uae_u32 cpuboard_ncr710_io_bget(unsigned int) {
    UNIMPLEMENTED();
    return 0;
}

void cpuboard_ncr710_io_bput(unsigned int, unsigned int) {
    UNIMPLEMENTED();
}

void cpuboard_ncr720_io_bget(unsigned int) {
    UNIMPLEMENTED();
}

void cpuboard_ncr720_io_bput(unsigned int, unsigned int) {
    UNIMPLEMENTED();
}

void cpuboard_setboard(struct uae_prefs *p, int type, int subtype) {
    UNIMPLEMENTED();
}

int cpuboard_memorytype(struct uae_prefs *p) {
    TRACE();
    //UNIMPLEMENTED();
    return 0;
}

bool cpuboard_fc_check(uaecptr addr, uae_u32 *v, int size, bool write) {
    UNIMPLEMENTED();
    return false;
}

int fsdb_name_invalid_dir (a_inode *, const TCHAR *n) {
    UNIMPLEMENTED();
    return 0;
}

int fsdb_mode_supported (const a_inode *) {
    UNIMPLEMENTED();
    return 0;
}

int a1060_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}
int a2088t_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}
int a2088xt_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}
int a2286_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}
int a2386_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}
int a4000t_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}
int a4000t_scsi_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}
int a4091_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}
void activate_console() {
    UNIMPLEMENTED();
}

void ahi_close_sound() {
    TRACE();
}

int alf3_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}
int amiga_clipboard_die(TrapContext*) {
    UNIMPLEMENTED();
    return 0;
}
int amiga_clipboard_got_data(TrapContext*, unsigned int, unsigned int, unsigned int) {
    UNIMPLEMENTED();
    return 0;
}
int amiga_clipboard_init(TrapContext*) {
    UNIMPLEMENTED();
    return 0;
}
int amiga_clipboard_proc_start(TrapContext*) {
    UNIMPLEMENTED();
    return 0;
}
int amiga_clipboard_task_start(TrapContext*, unsigned int) {
    UNIMPLEMENTED();
    return 0;
}
int amiga_clipboard_want_data(TrapContext*) {
    UNIMPLEMENTED();
    return 0;
}
int ariadne2_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

bool audio_is_pull_event() {
    TRACE();
    return false;
}

int AVIOutput_Restart(bool) {
    UNIMPLEMENTED();
    return 0;
}
int AVIOutput_Toggle(int, bool) {
    UNIMPLEMENTED();
    return 0;
}

int blizzardppc_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}

int bsdlib_install() {
    UNIMPLEMENTED();
    return 0;
}

int bsdlib_startup(TrapContext*, unsigned int) {
    UNIMPLEMENTED();
    return 0;
}

int bsdsock_fake_int_handler() {
    UNIMPLEMENTED();
    return 0;
}

int casablanca_map_overlay() {
    UNIMPLEMENTED();
    return 0;
}

int cd32_fmv_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

void cd32_fmv_set_sync(float, float) {
    TRACE();
    //return 0;
}

int cdtv_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}

int cdtv_battram_read(int) {
    UNIMPLEMENTED();
    return 0;
}

int cdtv_battram_write(int, int) {
    UNIMPLEMENTED();
    return 0;
}

int cdtv_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

int cdtvscsi_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

int cdtvsram_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

int check_for_cache_miss() {
    UNIMPLEMENTED();
    return 0;
}

int check_prefs_changed_gfx() {
    TRACE();
    return 0;
}

int clipboard_unsafeperiod() {
    UNIMPLEMENTED();
    return 0;
}

int clipboard_vsync() {
    TRACE();
    return 0;
}

void close_console() {
    UNIMPLEMENTED();
}

int compemu_reset() {
    UNIMPLEMENTED();
    return 0;
}

struct cpu_history;

int compile_block(cpu_history*, int, int) {
    UNIMPLEMENTED();
    return 0;
}

int compiler_init() {
    UNIMPLEMENTED();
    return 0;
}

void console_flush() {
    UNIMPLEMENTED();
}

int console_get(char*, int) {
    UNIMPLEMENTED();
    return 0;
}

bool console_isch() {
    UNIMPLEMENTED();
    return false;
}

int cpuboard_32bit(uae_prefs*) {
    UNIMPLEMENTED();
    return 0;
}

int cpuboard_autoconfig_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

int cpuboard_cleanup() {
    UNIMPLEMENTED();
    return 0;
}

int cpuboard_clear() {
    TRACE();
    return 0;
}

int cpuboard_dkb_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}

int cpuboard_forced_hardreset() {
    UNIMPLEMENTED();
    return 0;
}

int cpuboard_get_reset_pc(unsigned int*) {
    UNIMPLEMENTED();
    return 0;
}
int cpuboard_init() {
    TRACE();
    return 0;
}
int cpuboard_maprom() {
    UNIMPLEMENTED();
    return 0;
}

void cpuboard_overlay_override() {
    TRACE();
}

void cpuboard_rethink() {
    TRACE();
}

a_inode* custom_fsdb_lookup_aino_aname(a_inode_struct*, char const*) {
    UNIMPLEMENTED();
    nullptr;
}

a_inode* custom_fsdb_lookup_aino_nname(a_inode_struct*, char const*) {
    UNIMPLEMENTED();
    return nullptr;
}

int custom_fsdb_used_as_nname(a_inode_struct*, char const*) {
    UNIMPLEMENTED();
    return 0;
}

int debuggable() {
    UNIMPLEMENTED();
    return 0;
}

int debugger_change(int) {
    UNIMPLEMENTED();
    return 0;
}

int desktop_coords(int, int*, int*, int*, int*, int*, int*) {
    UNIMPLEMENTED();
    return 0;
}

int doflashscreen() {
    UNIMPLEMENTED();
    return 0;
}

int doprinter(unsigned char) {
    UNIMPLEMENTED();
    return 0;
}

int draco_mouse(int, int, int, int, int) {
    TRACE();
    return 0;
}

void driveclick_fdrawcmd_detect() {
    UNIMPLEMENTED();
}

void driveclick_fdrawcmd_motor(int, int) {
    UNIMPLEMENTED();
}

int driveclick_fdrawcmd_open(int) {
    UNIMPLEMENTED();
    return 0;
}

void driveclick_fdrawcmd_seek(int, int) {
    UNIMPLEMENTED();
}

void driveclick_fdrawcmd_vsync() {
    TRACE();
    //UNIMPLEMENTED();
}

int driveclick_loadresource(drvsample*, int) {
    UNIMPLEMENTED();
    return 0;
}

int dsp_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

int dsp_read() {
    UNIMPLEMENTED();
    return 0;
}

int dsp_write(unsigned char) {
    UNIMPLEMENTED();
    return 0;
}

int ematrix_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}

int emulate_genlock(vidbuffer*, vidbuffer*, bool) {
    UNIMPLEMENTED();
    return 0;
}

int emulate_grayscale(vidbuffer*, vidbuffer*) {
    UNIMPLEMENTED();
    return 0;
}

int emulate_specialmonitors(vidbuffer*, vidbuffer*) {
    UNIMPLEMENTED();
    return 0;
}

uae_u32 emulib_target_getcpurate(unsigned int, unsigned int*) {
    UNIMPLEMENTED();
    return 0;
}

int ethernet_reset() {
    TRACE();
    //UNIMPLEMENTED();
    return 0;
}

int fastlane_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}

int fetch_inputfilepath(char*, int) {
    UNIMPLEMENTED();
    return 0;
}

int fetch_ripperpath(char*, int) {
    UNIMPLEMENTED();
    return 0;
}

int fetch_rompath(char*, int) {
    UNIMPLEMENTED();
    return 0;
}

int fetch_saveimagepath(char*, int, int) {
    UNIMPLEMENTED();
    return 0;
}

int fetch_videopath(char*, int) {
    UNIMPLEMENTED();
    return 0;
}

int filesys_addexternals() {
    //UNIMPLEMENTED();
    return 0;
}

int finish_sound_buffer() {
    UNIMPLEMENTED();
    return 0;
}

void flush_log() {
    UNIMPLEMENTED();
}

void fpux_restore(int*) {
    TRACE();
}

int frame_drawn(int) {
    UNIMPLEMENTED();
    return 0;
}

void free_ahi_v2() {
    TRACE();
}

TCHAR* fsdb_create_unique_nname(a_inode_struct*, char const*) {
    UNIMPLEMENTED();
    return nullptr;
}

int fsdb_mode_representable_p(a_inode_struct const*, int) {
    UNIMPLEMENTED();
    return 0;
}

int fsdb_name_invalid(a_inode_struct*, char const*) {
    UNIMPLEMENTED();
    return 0;
}

TCHAR* fsdb_search_dir(char const*, char*, char**) {
    UNIMPLEMENTED();
    return nullptr;
}

int getcapslockstate() {
    UNIMPLEMENTED();
    return 0;
}

int get_guid_target(unsigned char*) {
    UNIMPLEMENTED();
    return 0;
}

int gfxboard_free() {
    UNIMPLEMENTED();
    return 0;
}

int gfxboard_init_memory(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

int gfxboard_refresh(int) {
    UNIMPLEMENTED();
    return 0;
}

int golemfast_ncr9x_scsi_get(unsigned int, int) {
    UNIMPLEMENTED();
    return 0;
}

int golemfast_ncr9x_scsi_put(unsigned int, unsigned int, int) {
    UNIMPLEMENTED();
    return 0;
}

int graphics_init(bool) {
    TRACE();
    return 1;
}

int graphics_leave() {
    UNIMPLEMENTED();
    return 0;
}

int graphics_reset(bool) {
    UNIMPLEMENTED();
    return 0;
}

int graphics_setup() {
    TRACE();
    //UNIMPLEMENTED();
    return 1;
}

int gui_ask_disk(int, char*) {
    UNIMPLEMENTED();
    return 0;
}

int handle_events() {
    TRACE();
    return 0;
}

int hydra_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

void init_fpucw_x87_80() {
    TRACE();
    //UNIMPLEMENTED();
}

int initparallel() {
    //UNIMPLEMENTED();
    return 0;
}

int init_sound() {
    TRACE();
    return 0;
}

int isguiactive() {
    UNIMPLEMENTED();
    return 0;
}

bool is_mainthread() {
    TRACE();
    return true;
}

bool ismouseactive() {
    UNIMPLEMENTED();
    return false;
}

int is_tablet() {
    UNIMPLEMENTED();
    return 0;
}

int is_touch_lightpen() {
    UNIMPLEMENTED();
    return 0;
}

int lanrover_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

void logging_init() {
    TRACE();
    //UNIMPLEMENTED();
    return;
}

void machdep_free() {
    UNIMPLEMENTED();
}

int machdep_init() {
    TRACE();
    //UNIMPLEMENTED();
    return 1;
}

int magnum40_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}

int mtecmastercard_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}

int multievolution_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}

void my_close(my_openfile_s*) {
    UNIMPLEMENTED();
}

bool my_createshortcut(char const*, char const*, char const*) {
    UNIMPLEMENTED();
    return false;
}

uae_s64 my_fsize(my_openfile_s*) {
    UNIMPLEMENTED();
    return 0;
}

bool my_isfilehidden(char const*) {
    UNIMPLEMENTED();
    return false;
}

int my_issamevolume(char const*, char const*, char*) {
    UNIMPLEMENTED();
    return 0;
}

uae_s64 my_lseek(my_openfile_s*, long, int) {
    UNIMPLEMENTED();
    return 0;
}

int my_mkdir(char const*) {
    UNIMPLEMENTED();
    return 0;
}

my_openfile_s* my_open(char const*, int) {
    UNIMPLEMENTED();
    return nullptr;
}

FILE* my_opentext(char const*) {
    UNIMPLEMENTED();
    return nullptr;
}

unsigned int my_read(my_openfile_s*, void*, unsigned int) {
    UNIMPLEMENTED();
    return 0;
}

void my_setfilehidden(char const*, bool) {
    UNIMPLEMENTED();
}

unsigned int my_write(my_openfile_s*, void*, unsigned int) {
    UNIMPLEMENTED();
    return 0;
}

int ncr710_a4091_autoconfig_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

int ncr710_draco_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

int ncr710_magnum40_autoconfig_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

int ncr710_warpengine_autoconfig_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

int ncr710_zeus040_autoconfig_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

int ncr_alf3_autoconfig_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

int ncr_dkb_autoconfig_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

int ncr_ematrix_autoconfig_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

int ncr_fastlane_autoconfig_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

int ncr_golemfast_autoconfig_init(romconfig*, unsigned int) {
    UNIMPLEMENTED();
    return 0;
}

int ncr_masoboshi_autoconfig_init(romconfig*, unsigned int) {
    UNIMPLEMENTED();
    return 0;
}

int ncr_mtecmastercard_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

int ncr_multievolution_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

int ncr_oktagon_autoconfig_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

int ncr_rapidfire_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

int ncr_scram5394_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

int ncr_squirrel_init(romconfig*, unsigned int) {
    UNIMPLEMENTED();
    return 0;
}

int ncr_trifecta_autoconfig_init(romconfig*, unsigned int) {
    UNIMPLEMENTED();
    return 0;
}

int notify_user_parms(int, char const*, ...) {
    UNIMPLEMENTED();
    return 0;
}

int oktagon_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}

int parallel_direct_read_data(unsigned char*) {
    UNIMPLEMENTED();
    return 0;
}

int parallel_direct_read_status(unsigned char*) {
    UNIMPLEMENTED();
    return 0;
}

int parallel_direct_write_data(unsigned char, unsigned char) {
    UNIMPLEMENTED();
    return 0;
}

int parallel_direct_write_status(unsigned char, unsigned char) {
    UNIMPLEMENTED();
    return 0;
}

int pause_sound_buffer() {
    UNIMPLEMENTED();
    return 0;
}

void pci_read_dma(pci_board_state*, unsigned int, unsigned char*, int) {
    UNIMPLEMENTED();
}

int PPCDisasm(PPCD_CB*) {
    UNIMPLEMENTED();
    return 0;
}

int quikpak_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}

int rapidfire_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}

void release_keys() {
    UNIMPLEMENTED();
}

void reset_sound() {
    TRACE();
}

void restart_sound_buffer() {
    TRACE();
}

int restore_cdtv_final() {
    UNIMPLEMENTED();
    return 0;
}
int restore_cdtv_finish() {
    UNIMPLEMENTED();
    return 0;
}
int samepath(char const*, char const*) {
    UNIMPLEMENTED();
    return 0;
}
int sampler_free() {
    UNIMPLEMENTED();
    return 0;
}
int sampler_getsample(int) {
    UNIMPLEMENTED();
    return 0;
}
int sampler_init() {
    TRACE();
    return 0;
}
int sampler_vsync() {
    TRACE();
    return 0;
}
int save_screenshot(int, unsigned long*) {
    UNIMPLEMENTED();
    return 0;
}
int scram5394_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}
int SERDATR() {
    UNIMPLEMENTED();
    return 0;
}
int SERDAT(unsigned short) {
    UNIMPLEMENTED();
    return 0;
}
int serial_dtr_off() {
    UNIMPLEMENTED();
    return 0;
}
int serial_exit() {
    UNIMPLEMENTED();
    return 0;
}
int serial_hsynchandler() {
    UNIMPLEMENTED();
    return 0;
}
int serial_init() {
    UNIMPLEMENTED();
    return 0;
}
int serial_rbf_clear() {
    UNIMPLEMENTED();
    return 0;
}
int serial_readstatus(unsigned char, unsigned char) {
    UNIMPLEMENTED();
    return 0;
}
int serial_rethink() {
    UNIMPLEMENTED();
    return 0;
}

void setup_brkhandler() {
    TRACE();
}

int setup_sound() {
    TRACE();
    //UNIMPLEMENTED();
    return 0;
}
int show_screen_maybe(int, bool) {
    UNIMPLEMENTED();
    return 0;
}
int sleep_millis_main(int) {
    UNIMPLEMENTED();
    return 0;
}

void sndboard_free_capture() {
    UNIMPLEMENTED();
}

int sndboard_get_buffer(int*) {
    UNIMPLEMENTED();
    return 0;
}

/*
bool sndboard_init_capture(int) {
    UNIMPLEMENTED();
    return false;
}
*/

int sndboard_release_buffer(unsigned char*, int) {
    UNIMPLEMENTED();
    return 0;
}

int sound_mute(int) {
    UNIMPLEMENTED();
    return 0;
}

int specialmonitor_autoconfig_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

void specialmonitor_reset() {
    TRACE();
}

int specialmonitor_store_fmode(int, int, unsigned short) {
    UNIMPLEMENTED();
    return 0;
}
int specialmonitor_uses_control_lines() {
    UNIMPLEMENTED();
    return 0;
}
int squirrel_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}

void statusline_render(int, unsigned char*, int, int, int, int, unsigned int*, unsigned int*, unsigned int*, unsigned int*) {
    TRACE();
}

void statusline_updated(int) {
    TRACE();
}

void sub_to_deinterleaved(unsigned char const*, unsigned char*) {
    UNIMPLEMENTED();
}

void sub_to_interleaved(unsigned char const*, unsigned char*) {
    UNIMPLEMENTED();
}

float target_adjust_vblank_hz(int, float hz) {
    TRACE();
    return hz;
}

bool target_can_autoswitchdevice() {
    UNIMPLEMENTED();
    return false;
}

int target_checkcapslock(int, int*) {
    UNIMPLEMENTED();
    return 0;
}

void target_fixup_options(uae_prefs*) {
    TRACE();
    //UNIMPLEMENTED();
}

int target_getcurrentvblankrate(int) {
    UNIMPLEMENTED();
    return 0;
}

int target_getdate(int*, int*, int*) {
    TRACE();
    //UNIMPLEMENTED();
    return 0;
}

int target_get_volume_name(uaedev_mount_info*, uaedev_config_info*, bool, bool, int) {
    UNIMPLEMENTED();
    return 0;
}

int target_inputdevice_acquire() {
    UNIMPLEMENTED();
    return 0;
}

int target_inputdevice_unacquire() {
    UNIMPLEMENTED();
    return 0;
}

int target_isrelativemode() {
    UNIMPLEMENTED();
    return 0;
}

int target_load_keyfile(uae_prefs*, char const*, int*, char*) {
    UNIMPLEMENTED();
    return 0;
}

void target_multipath_modified(uae_prefs*) {
    UNIMPLEMENTED();
}

int target_osk_control(int, int, int, int) {
    UNIMPLEMENTED();
    return 0;
}

int target_paste_to_keyboard() {
    UNIMPLEMENTED();
    return 0;
}

int target_quit() {
    UNIMPLEMENTED();
    return 0;
}

void target_reset() {
    TRACE();
}

int target_restart() {
    UNIMPLEMENTED();
    return 0;
}

int target_run() {
    TRACE();
    return 0;
}

void target_save_options(zfile*, uae_prefs*) {
    TRACE();
    //UNIMPLEMENTED();
}

int tekmagic_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}

int toggle_mousegrab() {
    UNIMPLEMENTED();
    return 0;
}

void to_upper(char*, int) {
    UNIMPLEMENTED();
}

int trifecta_ncr9x_scsi_get(unsigned int, int) {
    UNIMPLEMENTED();
    return 0;
}

int typhoon2scsi_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}

int typhoon2scsi_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

int uaenative_get_library_dirs() {
    UNIMPLEMENTED();
    return 0;
}

int uaenative_get_uaevar() {
    UNIMPLEMENTED();
    return 0;
}

int uaeser_clearbuffers(void*) {
    UNIMPLEMENTED();
    return 0;
}

int uaeser_getdatalength() {
    UNIMPLEMENTED();
    return 0;
}

int uaeser_open(void*, void*, int) {
    UNIMPLEMENTED();
    return 0;
}

int uaeser_query(void*, unsigned short*, unsigned int*) {
    UNIMPLEMENTED();
    return 0;
}

int uaeser_read(void*, unsigned char*, unsigned int) {
    UNIMPLEMENTED();
    return 0;
}

int uaeser_setparams(void*, int, int, int, int, int, int, unsigned int) {
    UNIMPLEMENTED();
    return 0;
}

int uaeser_trigger(void*) {
    UNIMPLEMENTED();
    return 0;
}

void uae_slirp_cleanup() {
    UNIMPLEMENTED();
}

void uae_slirp_end() {
    UNIMPLEMENTED();
}

int uae_slirp_init() {
    UNIMPLEMENTED();
    return 0;
}

void uae_slirp_input(unsigned char const*, int) {
    UNIMPLEMENTED();
}

bool uae_slirp_start() {
    UNIMPLEMENTED();
    return false;
}

int update_debug_info() {
    UNIMPLEMENTED();
    return 0;
}

void updatedisplayarea(int) {
    TRACE();
}

int update_sound(float) {
    UNIMPLEMENTED();
    return 0;
}

int vsync_switchmode(int, int) {
    UNIMPLEMENTED();
    return 0;
}

int warpengine_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}

void x86_bridge_sync_change() {
    TRACE();
    //return 0;
}

int x86_doirq(unsigned char) {
    UNIMPLEMENTED();
    return 0;
}

int x86_mouse(int, int, int, int, int) {
    TRACE();
    return 0;
}

int x86_rt1000_add_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}

int x86_rt1000_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

int x86_update_sound(float) {
    UNIMPLEMENTED();
    return 0;
}

int x86_xt_ide_bios(zfile*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}

int xsurf_init(autoconfig_info*) {
    UNIMPLEMENTED();
    return 0;
}

int zeus040_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}

int audio_pull_buffer() {
    UNIMPLEMENTED();
    return 0;
}

int bsdlib_reset() {
    UNIMPLEMENTED();
    return 0;
}

int build_comp() {
    UNIMPLEMENTED();
    return 0;
}

int close_sound() {
    UNIMPLEMENTED();
    return 0;
}

TCHAR console_getch() {
    UNIMPLEMENTED();
    return 0;
}

int cpuboard_io_special(int, unsigned int*, int, bool) {
    UNIMPLEMENTED();
    return 0;
}

int cpuboard_map() {
    TRACE();
    return 0;
}

int cpuboard_maxmemory(uae_prefs*) {
    UNIMPLEMENTED();
    return 0;
}

int cpuboard_reset(int) {
    TRACE();
    return 0;
}

int cyberstorm_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}

int draco_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}

int draco_ext_interrupt(bool) {
    UNIMPLEMENTED();
    return 0;
}

void driveclick_fdrawcmd_close(int) {
    UNIMPLEMENTED();
}

int dsp_pause(int) {
    UNIMPLEMENTED();
    return 0;
}

int ethernet_pause(int) {
    UNIMPLEMENTED();
    return 0;
}

int fp_init_native_80() {
    UNIMPLEMENTED();
    return 0;
}

int fsdb_exists(char const*) {
    UNIMPLEMENTED();
    return 0;
}

int fsdb_fill_file_attrs(a_inode_struct*, a_inode_struct*) {
    UNIMPLEMENTED();
    return 0;
}

int getlocaltime() {
    TRACE();
    //UNIMPLEMENTED();
    return 0;
}

int getvsyncrate(int, float, int*) {
    UNIMPLEMENTED();
    return 0;
}

int gfxboard_get_configname(int) {
    UNIMPLEMENTED();
    return 0;
}

int gfxboard_get_configtype(rtgboardconfig*) {
    TRACE();
    //UNIMPLEMENTED();
    return 0;
}

int gfxboard_set(int, bool) {
    UNIMPLEMENTED();
    return 0;
}

int golemfast_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}

int handle_msgpump(bool) {
    TRACE();
    return 0;
}

int input_get_default_joystick_analog(uae_input_device*, int, int, int, bool, bool) {
    UNIMPLEMENTED();
    return 0;
}

int input_get_default_lightpen(uae_input_device*, int, int, int, bool, bool, int) {
    UNIMPLEMENTED();
    return 0;
}

int input_get_default_mouse(uae_input_device*, int, int, int, bool, bool, bool) {
    UNIMPLEMENTED();
    return 0;
}

int isfullscreen() {
    UNIMPLEMENTED();
    return 0;
}

int masoboshi_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}

int masoboshi_ncr9x_scsi_get(unsigned int, int) {
    UNIMPLEMENTED();
    return 0;
}

bool my_chmod(char const*, unsigned int) {
    UNIMPLEMENTED();
    return false;
}

int my_getvolumeinfo(char const*) {
    UNIMPLEMENTED();
    return 0;
}

int my_rmdir(char const*) {
    UNIMPLEMENTED();
    return 0;
}

int pause_sound() {
    UNIMPLEMENTED();
    return 0;
}

int restore_cdtv_dmac(unsigned char*) {
    UNIMPLEMENTED();
    return 0;
}

int restore_cdtv(unsigned char*) {
    UNIMPLEMENTED();
    return 0;
}

int resume_sound() {
    UNIMPLEMENTED();
    return 0;
}

int save_cdtv_dmac(unsigned long*, unsigned char*) {
    UNIMPLEMENTED();
    return 0;
}

int save_cdtv(unsigned long*, unsigned char*) {
    UNIMPLEMENTED();
    return 0;
}

int serial_uartbreak(int) {
    UNIMPLEMENTED();
    return 0;
}

int serial_writestatus(unsigned char, unsigned char) {
    UNIMPLEMENTED();
    return 0;
}

int SERPER(unsigned short) {
    UNIMPLEMENTED();
    return 0;
}

int set_cache_state(int) {
    UNIMPLEMENTED();
    return 0;
}

int setcapslockstate(int) {
    UNIMPLEMENTED();
    return 0;
}

int setmouseactivexy(int, int, int, int) {
    UNIMPLEMENTED();
    return 0;
}

int squirrel_ncr9x_scsi_get(unsigned int, int) {
    UNIMPLEMENTED();
    return 0;
}

int squirrel_ncr9x_scsi_put(unsigned int, unsigned int, int) {
    UNIMPLEMENTED();
    return 0;
}

int target_cpu_speed() {
    //UNIMPLEMENTED();
    return 0;
}

void target_default_options(uae_prefs*, int) {
    TRACE();
    //UNIMPLEMENTED();
}

bool target_graphics_buffer_update(int) {
    TRACE();
    return true;
}

int target_parse_option(uae_prefs*, char const*, char const*, int) {
    UNIMPLEMENTED();
    return 0;
}

int target_sleep_nanos(int) {
    UNIMPLEMENTED();
    return 0;
}

int trifecta_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}

int trifecta_ncr9x_scsi_put(unsigned int, unsigned int, int) {
    UNIMPLEMENTED();
    return 0;
}

int uaeser_break(void*, int) {
    UNIMPLEMENTED();
    return 0;
}

int uaeser_close(void*) {
    UNIMPLEMENTED();
    return 0;
}

int uaeser_write(void*, unsigned char*, unsigned int) {
    UNIMPLEMENTED();
    return 0;
}

int audio_is_pull() {
    TRACE();
    return 0;
}

int cdtv_front_panel(int) {
    UNIMPLEMENTED();
    return 0;
}

/*
int check_prefs_changed_comp(bool) {
    UNIMPLEMENTED();
    return 0;
}
*/

int cpuboard_ncr9x_add_scsi_unit(int, uaedev_config_info*, romconfig*) {
    UNIMPLEMENTED();
    return 0;
}

void f_out(void*, char const*, ...) {
    UNIMPLEMENTED();
}


/*
      2  undefined reference to `debug_vsync_forced_delay'
      2  undefined reference to `avioutput_filename_gui'
      1  undefined reference to `seriallog'
      2  undefined reference to `is_dsp_installed'
      2  undefined reference to `specialmonitorconfignames'
      2  undefined reference to `FloppyBridgeAPI::getBridgeDriverInformation(bool, FloppyBridgeAPI::BridgeInformation&)'
      2  undefined reference to `chd_file::close()'
      2  undefined reference to `chd_file::open(zfile&, bool, chd_file*)'
      2  undefined reference to `chd_file::read_units(unsigned long long, void*, unsigned int)'
      2  undefined reference to `chd_file::write_units(unsigned long long, void const*, unsigned int)'
      2  undefined reference to `uaelib_debug'
      1  undefined reference to `_daylight'
      1  undefined reference to `prop'
      1  undefined reference to `ne2000_pci_board_pcmcia'
      1  undefined reference to `busywait'
      1  undefined reference to `bsd_int_requested'
      1  undefined reference to `beamracer_debug'
      1  undefined reference to `avioutput_enabled'
      1  undefined reference to `HARD_DISK_METADATA_FORMAT'
      1  undefined reference to `picasso96_state'
      2  undefined reference to `tablet_log'
      2  undefined reference to `_timezone'
      3  undefined reference to `cubo_nvram'
      3  undefined reference to `debug_vsync_min_delay'
      3  undefined reference to `FloppyBridgeAPI::createDriverFromProfileID(unsigned int)'
      1  undefined reference to `astring::init()'
        int pushall_call_handler {
      1  undefined reference to `devicefunc_cdimage'
      1  undefined reference to `devicefunc_scsi_ioctl'
      1  undefined reference to `devicefunc_scsi_spti'
      1  undefined reference to `key_swap_hack'
      1  undefined reference to `log_vsync'
      1  undefined reference to `start_pc'
      1  undefined reference to `start_pc_p'
*/



static int dummy_open_bus_func(int flags) {
    printf("Dummy open_bus_func called with flags: %d\n", flags);
    return 0;
}

static void dummy_close_bus_func(void) {
    printf("Dummy close_bus_func called\n");
}

static int dummy_open_device_func(int deviceID, const TCHAR* deviceName, int flags) {
    printf("Dummy open_device_func called with deviceID: %d, deviceName: %s, flags: %d\n", deviceID, deviceName, flags);
    return 0;
}

static void dummy_close_device_func(int deviceID) {
    printf("Dummy close_device_func called with deviceID: %d\n", deviceID);
}

static struct device_info* dummy_info_device_func(int deviceID, struct device_info* info, int size, int flags) {
    printf("Dummy info_device_func called with deviceID: %d, size: %d, flags: %d\n", deviceID, size, flags);
    return NULL;
}

static uae_u8* dummy_execscsicmd_out_func(int deviceID, uae_u8* cmd, int size) {
    printf("Dummy execscsicmd_out_func called with deviceID: %d, size: %d\n", deviceID, size);
    return NULL;
}

static uae_u8* dummy_execscsicmd_in_func(int deviceID, uae_u8* cmd, int size, int* result) {
    printf("Dummy execscsicmd_in_func called with deviceID: %d, size: %d\n", deviceID, size);
    *result = 0;
    return NULL;
}

static int dummy_execscsicmd_direct_func(int deviceID, struct amigascsi* cmd) {
    printf("Dummy execscsicmd_direct_func called with deviceID: %d\n", deviceID);
    return 0;
}

static void dummy_play_subchannel_callback(uae_u8* data, int size) {
    printf("Dummy play_subchannel_callback called with size: %d\n", size);
}

static int dummy_play_status_callback(int status, int subcode) {
    printf("Dummy play_status_callback called with status: %d, subcode: %d\n", status, subcode);
    return 0;
}

static int dummy_pause_func(int deviceID, int flags) {
    printf("Dummy pause_func called with deviceID: %d, flags: %d\n", deviceID, flags);
    return 0;
}

static int dummy_stop_func(int deviceID) {
    printf("Dummy stop_func called with deviceID: %d\n", deviceID);
    return 0;
}

static int dummy_play_func(int deviceID, int track, int index, int flags, play_status_callback status_callback, play_subchannel_callback subchannel_callback) {
    printf("Dummy play_func called with deviceID: %d, track: %d, index: %d, flags: %d\n", deviceID, track, index, flags);
    return 0;
}

static uae_u32 dummy_volume_func(int deviceID, uae_u16 left, uae_u16 right) {
    printf("Dummy volume_func called with deviceID: %d, left: %u, right: %u\n", deviceID, left, right);
    return 0;
}

static int dummy_qcode_func(int deviceID, uae_u8* qcode, int size, bool msf) {
    printf("Dummy qcode_func called with deviceID: %d, size: %d, msf: %d\n", deviceID, size, msf);
    return 0;
}

static int dummy_toc_func(int deviceID, struct cd_toc_head* toc) {
    printf("Dummy toc_func called with deviceID: %d\n", deviceID);
    return 0;
}

static int dummy_read_func(int deviceID, uae_u8* buffer, int size, int flags) {
    printf("Dummy read_func called with deviceID: %d, size: %d, flags: %d\n", deviceID, size, flags);
    return 0;
}

static int dummy_rawread_func(int deviceID, uae_u8* buffer, int size, int subcode, int flags, uae_u32 offset) {
    printf("Dummy rawread_func called with deviceID: %d, size: %d, subcode: %d, flags: %d, offset: %u\n", deviceID, size, subcode, flags, offset);
    return 0;
}

static int dummy_write_func(int deviceID, uae_u8* buffer, int size, int flags) {
    printf("Dummy write_func called with deviceID: %d, size: %d, flags: %d\n", deviceID, size, flags);
    return 0;
}

int dummy_isatapi_func(int deviceID) {
    printf("Dummy isatapi_func called with deviceID: %d\n", deviceID);
    return 0;
}

int dummy_ismedia_func(int deviceID, int flags) {
    printf("Dummy ismedia_func called with deviceID: %d, flags: %d\n", deviceID, flags);
    return 0;
}

int dummy_scsiemu_func(int deviceID, uae_u8* data) {
    printf("Dummy scsiemu_func called with deviceID: %d\n", deviceID);
    return 0;
}

struct device_functions devicefunc_cdimage = {
    _T("DummyDevice"),
    dummy_open_bus_func, dummy_close_bus_func, dummy_open_device_func, dummy_close_device_func, dummy_info_device_func,
    dummy_execscsicmd_out_func, dummy_execscsicmd_in_func, dummy_execscsicmd_direct_func, dummy_pause_func, dummy_stop_func,
    dummy_play_func, dummy_volume_func, dummy_qcode_func, dummy_toc_func, dummy_read_func,
    dummy_rawread_func, dummy_write_func, dummy_isatapi_func, dummy_ismedia_func, dummy_scsiemu_func
};

struct device_functions devicefunc_scsi_ioctl = {
    _T("IOCTL"),
    dummy_open_bus_func, dummy_close_bus_func, dummy_open_device_func, dummy_close_device_func, dummy_info_device_func,
    dummy_execscsicmd_out_func, dummy_execscsicmd_in_func, dummy_execscsicmd_direct_func, dummy_pause_func, dummy_stop_func,
    dummy_play_func, dummy_volume_func, dummy_qcode_func, dummy_toc_func, dummy_read_func,
    dummy_rawread_func, dummy_write_func, dummy_isatapi_func, dummy_ismedia_func, dummy_scsiemu_func
};

struct device_functions devicefunc_scsi_spti = {
    _T("IOCTL"),
    dummy_open_bus_func, dummy_close_bus_func, dummy_open_device_func, dummy_close_device_func, dummy_info_device_func,
    dummy_execscsicmd_out_func, dummy_execscsicmd_in_func, dummy_execscsicmd_direct_func, dummy_pause_func, dummy_stop_func,
    dummy_play_func, dummy_volume_func, dummy_qcode_func, dummy_toc_func, dummy_read_func,
    dummy_rawread_func, dummy_write_func, dummy_isatapi_func, dummy_ismedia_func, dummy_scsiemu_func
};

const TCHAR* specialmonitorconfignames[] = {
	_T("none"),
	NULL
};

TCHAR avioutput_filename_gui[MAX_DPATH];
void* pushall_call_handler = nullptr;

