extern int decode_cloanto_rom_do (uae_u8 *mem, int size, int real_size);

#define ROMTYPE_SUB_MASK    0x000000ff
#define ROMTYPE_GROUP_MASK  0x00ffff00
#define ROMTYPE_MASK		0x00ffffff

#define ROMTYPE_KICK		0x00000100
#define ROMTYPE_KICKCD32	0x00000200
#define ROMTYPE_EXTCD32		0x00000400
#define ROMTYPE_EXTCDTV		0x00000800
#define ROMTYPE_KEY			0x00001000
#define ROMTYPE_ARCADIABIOS	0x00002000
#define ROMTYPE_ARCADIAGAME	0x00004000
#define ROMTYPE_CD32CART	0x00008000
#define ROMTYPE_SPECIALKICK	0x00010000
#define ROMTYPE_PIV			0x00020000
#define ROMTYPE_CPUBOARD	0x00040000

#define ROMTYPE_FREEZER		0x00080000
#define ROMTYPE_AR			0x00080001
#define ROMTYPE_AR2			0x00080002
#define ROMTYPE_HRTMON		0x00080003
#define ROMTYPE_NORDIC		0x00080004
#define ROMTYPE_XPOWER		0x00080005
#define ROMTYPE_SUPERIV		0x00080006

#define ROMTYPE_SCSI		0x00100000
#define ROMTYPE_A2091		0x00100001
#define ROMTYPE_A4091		0x00100002
#define ROMTYPE_CPUBOARDEXT	0x00100003
#define ROMTYPE_FASTLANE	0x00100004
#define ROMTYPE_OKTAGON		0x00100005
#define ROMTYPE_GVPS1		0x00100006
#define ROMTYPE_GVPS2		0x00100007
#define ROMTYPE_AMAX		0x00100008
#define ROMTYPE_ALFA		0x00100009
#define ROMTYPE_ALFAPLUS	0x0010000a
#define ROMTYPE_APOLLO		0x0010000b
#define ROMTYPE_MASOBOSHI	0x0010000c
#define ROMTYPE_SUPRA		0x0010000d

#define ROMTYPE_QUAD		0x01000000
#define ROMTYPE_EVEN		0x02000000
#define ROMTYPE_ODD			0x04000000
#define ROMTYPE_8BIT		0x08000000
#define ROMTYPE_BYTESWAP	0x10000000
#define ROMTYPE_CD32		0x20000000
#define ROMTYPE_SCRAMBLED	0x40000000
#define ROMTYPE_NONE		0x80000000

#define ROMTYPE_ALL_KICK (ROMTYPE_KICK | ROMTYPE_KICKCD32 | ROMTYPE_CD32)
#define ROMTYPE_ALL_EXT (ROMTYPE_EXTCD32 | ROMTYPE_EXTCDTV)
#define ROMTYPE_ALL_CART (ROMTYPE_AR | ROMTYPE_HRTMON | ROMTYPE_NORDIC | ROMTYPE_XPOWER | ROMTYPE_CD32CART)

struct romheader {
	TCHAR *name;
	int id;
};

struct romdata {
	TCHAR *name;
	int ver, rev;
	int subver, subrev;
	TCHAR *model;
	uae_u32 size;
	int id;
	int cpu;
	int cloanto;
	int type;
	int group;
	int title;
	TCHAR *partnumber;
	uae_u32 crc32;
	uae_u32 sha1[5];
	TCHAR *configname;
	TCHAR *defaultfilename;
};

struct romlist {
	TCHAR *path;
	struct romdata *rd;
};

extern struct romdata *getromdatabypath (const TCHAR *path);
extern struct romdata *getromdatabycrc (uae_u32 crc32);
extern struct romdata *getromdatabycrc (uae_u32 crc32, bool);
extern struct romdata *getromdatabydata (uae_u8 *rom, int size);
extern struct romdata *getromdatabyid (int id);
extern struct romdata *getromdatabyidgroup (int id, int group, int subitem);
extern struct romdata *getromdatabyzfile (struct zfile *f);
extern struct romdata *getfrombydefaultname(const TCHAR *name, int size);
extern struct romlist **getarcadiaroms (void);
extern struct romdata *getarcadiarombyname (const TCHAR *name);
extern struct romlist **getromlistbyident (int ver, int rev, int subver, int subrev, const TCHAR *model, int romflags, bool all);
extern void getromname (const struct romdata*, TCHAR*);
extern struct romdata *getromdatabyname (const TCHAR*);
extern struct romlist *getromlistbyids (const int *ids, const TCHAR *romname);
extern struct romdata *getromdatabyids (const int *ids);
extern void romwarning(const int *ids);
extern struct romlist *getromlistbyromdata (const struct romdata *rd);
extern void romlist_add (const TCHAR *path, struct romdata *rd);
extern TCHAR *romlist_get (const struct romdata *rd);
extern void romlist_clear (void);
extern struct zfile *read_rom (struct romdata *rd);
extern struct zfile *read_rom_name (const TCHAR *filename);

extern int load_keyring (struct uae_prefs *p, const TCHAR *path);
extern uae_u8 *target_load_keyfile (struct uae_prefs *p, const TCHAR *path, int *size, TCHAR *name);
extern void free_keyring (void);
extern int get_keyring (void);
extern void kickstart_fix_checksum (uae_u8 *mem, int size);
extern void descramble_nordicpro (uae_u8*, int, int);
extern int kickstart_checksum (uae_u8 *mem, int size);
extern int decode_rom (uae_u8 *mem, int size, int mode, int real_size);
extern struct zfile *rom_fopen (const TCHAR *name, const TCHAR *mode, int mask);
extern struct zfile *read_rom_name_guess (const TCHAR *filename);
extern void addkeydir (const TCHAR *path);
extern void addkeyfile (const TCHAR *path);
extern int romlist_count (void);
extern struct romlist *romlist_getit (void);
extern int configure_rom (struct uae_prefs *p, const int *rom, int msg);
int is_device_rom(struct uae_prefs *p, int devnum, int romtype);
struct zfile *read_device_rom(struct uae_prefs *p, int devnum, int romtype, int *roms);
struct romconfig *get_device_romconfig(struct uae_prefs *p, int devnum, int romtype);
struct boardromconfig *get_device_rom(struct uae_prefs *p, int romtype, int *index);
void set_device_rom(struct uae_prefs *p, const TCHAR *path, int romtype);
const struct expansionromtype *get_device_expansion_rom(int romtype);
const struct expansionromtype *get_unit_expansion_rom(int hdunit);
struct boardromconfig *get_device_rom_new(struct uae_prefs *p, int romtype, int *index);
void clear_device_rom(struct uae_prefs *p, int romtype);
