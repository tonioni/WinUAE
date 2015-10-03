 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Autoconfig device support
  *
  * (c) 1996 Ed Hanway
  */

#ifndef UAE_AUTOCONF_H
#define UAE_AUTOCONF_H

#include "uae/types.h"

#define RTAREA_DEFAULT 0xf00000
#define RTAREA_BACKUP  0xef0000
#define RTAREA_BACKUP_2 0xdb0000
#define RTAREA_SIZE 0x10000
#define RTAREA_TRAPS 0x2000
#define RTAREA_RTG 0x3000
#define RTAREA_FSBOARD 0xFFEC
#define RTAREA_INT 0xFFEB

extern uae_u32 addr (int);
extern void db (uae_u8);
extern void dw (uae_u16);
extern void dl (uae_u32);
extern uae_u32 ds_ansi (const uae_char*);
extern uae_u32 ds (const TCHAR*);
extern uae_u32 ds_bstr_ansi (const uae_char*);
extern uae_u8 dbg (uaecptr);
extern void calltrap (uae_u32);
extern void org (uae_u32);
extern uae_u32 here (void);
extern uaecptr makedatatable (uaecptr resid, uaecptr resname, uae_u8 type, uae_s8 priority, uae_u16 ver, uae_u16 rev);

extern void align (int);

extern volatile int uae_int_requested, uaenet_int_requested;
extern volatile int uaenet_vsync_requested;
extern void set_uae_int_flag (void);

#define RTS 0x4e75
#define RTE 0x4e73

extern uaecptr EXPANSION_explibname, EXPANSION_doslibname, EXPANSION_uaeversion;
extern uaecptr EXPANSION_explibbase, EXPANSION_uaedevname, EXPANSION_haveV36;
extern uaecptr EXPANSION_bootcode, EXPANSION_nullfunc;

extern uaecptr ROM_filesys_resname, ROM_filesys_resid;
extern uaecptr ROM_filesys_diagentry;
extern uaecptr ROM_hardfile_resname, ROM_hardfile_resid;
extern uaecptr ROM_hardfile_init;
extern uaecptr filesys_initcode, filesys_initcode_ptr;

extern int is_hardfile (int unit_no);
extern int nr_units (void);
extern int nr_directory_units (struct uae_prefs*);
extern uaecptr need_uae_boot_rom (void);

struct mountedinfo
{
    uae_s64 size;
    bool ismounted;
    bool ismedia;
	int error;
    int nrcyls;
	TCHAR rootdir[MAX_DPATH];
};

extern int add_filesys_unitconfig (struct uae_prefs *p, int index, TCHAR *error);
extern int get_filesys_unitconfig (struct uae_prefs *p, int index, struct mountedinfo*);
extern int kill_filesys_unitconfig (struct uae_prefs *p, int nr);
extern int move_filesys_unitconfig (struct uae_prefs *p, int nr, int to);
extern TCHAR *validatedevicename (TCHAR *s, const TCHAR *def);
extern TCHAR *validatevolumename (TCHAR *s, const TCHAR *def);

int filesys_insert (int nr, const TCHAR *volume, const TCHAR *rootdir, bool readonly, int flags);
int filesys_eject (int nr);
int filesys_media_change (const TCHAR *rootdir, int inserted, struct uaedev_config_data *uci);

extern TCHAR *filesys_createvolname (const TCHAR *volname, const TCHAR *rootdir, struct zvolume *zv, const TCHAR *def);
extern int target_get_volume_name (struct uaedev_mount_info *mtinf, const TCHAR *volumepath, TCHAR *volumename, int size, bool inserted, bool fullcheck);

extern int sprintf_filesys_unit (TCHAR *buffer, int num);

extern void filesys_reset (void);
extern void filesys_cleanup (void);
extern void filesys_prepare_reset (void);
extern void filesys_start_threads (void);
extern void filesys_flush_cache (void);
extern void filesys_free_handles (void);
extern void filesys_vsync (void);

extern void filesys_install (void);
extern void filesys_install_code (void);
extern void filesys_store_devinfo (uae_u8 *);
extern void hardfile_install (void);
extern void hardfile_reset (void);
extern void emulib_install (void);
extern void expansion_init (void);
extern void expansion_cleanup (void);
extern void expansion_clear (void);
extern void expansion_autoconfig_put(int, uae_u8);
extern uaecptr expansion_startaddress(uaecptr addr, uae_u32 size);
extern bool expansion_is_next_board_fastram(void);

extern void uaegfx_install_code (uaecptr);

extern uae_u32 emulib_target_getcpurate (uae_u32, uae_u32*);

typedef addrbank*(*DEVICE_INIT)(struct romconfig*);
typedef void(*DEVICE_ADD)(int, struct uaedev_config_info*, struct romconfig*);
typedef bool(*E8ACCESS)(int, uae_u32*, int, bool);
typedef void(*DEVICE_MEMORY_CALLBACK)(struct romconfig*, uae_u8*, int);
#define EXPANSIONTYPE_SCSI 1
#define EXPANSIONTYPE_IDE 2
#define EXPANSIONTYPE_24BIT 4
#define EXPANSIONTYPE_IDE_PORT_DOUBLED 8
#define EXPANSIONTYPE_SASI 16
#define EXPANSIONTYPE_CUSTOM 32
#define EXPANSIONTYPE_PCI_BRIDGE 64
#define EXPANSIONTYPE_PARALLEL_ADAPTER 128
#define EXPANSIONTYPE_X86_BRIDGE 256
#define EXPANSIONTYPE_CUSTOM_SECONDARY 512
#define EXPANSIONTYPE_RTG 1024
struct expansionboardsettings
{
	const TCHAR *name;
	const TCHAR *configname;
	bool multiselect;
	bool invert;
	int bitshift;
};
struct expansionsubromtype
{
	const TCHAR *name;
	const TCHAR *configname;
	uae_u32 romtype;
	int memory_mid, memory_pid;
	uae_u32 memory_serial;
	bool memory_after;
	uae_u8 autoconfig[16];
};
struct expansionromtype
{
	const TCHAR *name;
	const TCHAR *friendlyname;
	const TCHAR *friendlymanufacturer;
	DEVICE_INIT init, init2;
	DEVICE_ADD add;
	uae_u32 romtype;
	uae_u32 romtype_extra;
	uae_u32 parentromtype;
	int zorro;
	bool singleonly;
	const struct expansionsubromtype *subtypes;
	int defaultsubtype;
	bool autoboot_jumper;
	int deviceflags;
	int memory_mid, memory_pid;
	uae_u32 memory_serial;
	bool memory_after;
	DEVICE_MEMORY_CALLBACK memory_callback;
	bool id_jumper;
	const struct expansionboardsettings *settings;
	uae_u8 autoconfig[16];
};
extern const struct expansionromtype expansionroms[];
struct cpuboardsubtype
{
	const TCHAR *name;
	const TCHAR *configname;
	int romtype, romtype_extra;
	DEVICE_ADD add;
	int deviceflags;
	int memorytype;
	int maxmemory;
	int z3extra;
	DEVICE_INIT init, init2;
	int initzorro;
	int initflag;
	const struct expansionboardsettings *settings;
	E8ACCESS e8;
	// if includes Z2 or Z3 RAM
	int memory_mid, memory_pid;
	uae_u32 memory_serial;
	bool memory_after;
	uae_u8 autoconfig[16];
};
struct cpuboardtype
{
	int id;
	const TCHAR *name;
	const struct cpuboardsubtype *subtypes;
	int defaultsubtype;
};
extern const struct cpuboardtype cpuboards[];

#endif /* UAE_AUTOCONF_H */
