 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Autoconfig device support
  *
  * (c) 1996 Ed Hanway
  */

extern uae_u32 addr (int);
extern void db (uae_u8);
extern void dw (uae_u16);
extern void dl (uae_u32);
extern uae_u32 ds (const char *);
extern void calltrap (uae_u32);
extern void org (uae_u32);
extern uae_u32 here (void);

#define deftrap(f) define_trap((f), 0, "")
#define deftrap2(f, mode, str) define_trap((f), (mode), (str))

extern void align (int);

extern volatile int uae_int_requested;
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
extern uaecptr filesys_initcode;

extern int is_hardfile (int unit_no);
extern int nr_units (void);

struct mountedinfo
{
    uae_u64 size;
    int ismounted;
    int nrcyls;
};

extern int add_filesys_unitconfig (struct uae_prefs *p, int index, char *error);
extern int get_filesys_unitconfig (struct uae_prefs *p, int index, struct mountedinfo*);
extern int kill_filesys_unitconfig (struct uae_prefs *p, int nr);
extern int move_filesys_unitconfig (struct uae_prefs *p, int nr, int to);

extern int sprintf_filesys_unit (char *buffer, int num);

extern void filesys_reset (void);
extern void filesys_cleanup (void);
extern void filesys_prepare_reset (void);
extern void filesys_start_threads (void);
extern void filesys_flush_cache (void);

extern void filesys_install (void);
extern void filesys_install_code (void);
extern void filesys_store_devinfo (uae_u8 *);
extern void hardfile_install (void);
extern void hardfile_reset (void);
extern void emulib_install (void);
extern void expansion_init (void);
extern void expansion_cleanup (void);
extern void expansion_clear (void);

#define TRAPFLAG_NO_REGSAVE 1
#define TRAPFLAG_NO_RETVAL 2
#define TRAPFLAG_EXTRA_STACK 4
#define TRAPFLAG_DORET 8

#define RTAREA_BASE 0xF00000
