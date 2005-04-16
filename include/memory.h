 /*
  * UAE - The Un*x Amiga Emulator
  *
  * memory management
  *
  * Copyright 1995 Bernd Schmidt
  */

extern void memory_reset (void);
extern void a1000_reset (void);

#ifdef JIT
extern int special_mem;
#define S_READ 1
#define S_WRITE 2
extern void *cache_alloc (int);
extern void cache_free (void*);
#endif

#ifdef ADDRESS_SPACE_24BIT
#define MEMORY_BANKS 256
#else
#define MEMORY_BANKS 65536
#endif

typedef uae_u32 (*mem_get_func)(uaecptr) REGPARAM;
typedef void (*mem_put_func)(uaecptr, uae_u32) REGPARAM;
typedef uae_u8 *(*xlate_func)(uaecptr) REGPARAM;
typedef int (*check_func)(uaecptr, uae_u32) REGPARAM;

extern char *address_space, *good_address_map;
extern uae_u8 *chipmemory;

extern uae_u32 allocated_chipmem;
extern uae_u32 allocated_fastmem;
extern uae_u32 allocated_bogomem;
extern uae_u32 allocated_gfxmem;
extern uae_u32 allocated_z3fastmem, max_z3fastmem;
extern uae_u32 allocated_a3000mem;

extern uae_u32 wait_cpu_cycle_read (uaecptr addr, int mode);
extern uae_u32 wait_cpu_cycle_read_cycles (uaecptr addr, int mode, int *cycles);
extern void wait_cpu_cycle_write (uaecptr addr, int mode, uae_u32 v);

#undef DIRECT_MEMFUNCS_SUCCESSFUL
#include "machdep/maccess.h"

#define chipmem_start 0x00000000
#define bogomem_start 0x00C00000
#define a3000mem_start 0x07000000
#define kickmem_start 0x00F80000

extern int ersatzkickfile;
extern int cloanto_rom;
extern uae_u16 kickstart_version;

extern uae_u8* baseaddr[];

typedef struct {
    /* These ones should be self-explanatory... */
    mem_get_func lget, wget, bget;
    mem_put_func lput, wput, bput;
    /* Use xlateaddr to translate an Amiga address to a uae_u8 * that can
     * be used to address memory without calling the wget/wput functions.
     * This doesn't work for all memory banks, so this function may call
     * abort(). */
    xlate_func xlateaddr;
    /* To prevent calls to abort(), use check before calling xlateaddr.
     * It checks not only that the memory bank can do xlateaddr, but also
     * that the pointer points to an area of at least the specified size.
     * This is used for example to translate bitplane pointers in custom.c */
    check_func check;
    /* For those banks that refer to real memory, we can save the whole trouble
       of going through function calls, and instead simply grab the memory
       ourselves. This holds the memory address where the start of memory is
       for this particular bank. */
    uae_u8 *baseaddr;
} addrbank;

extern uae_u8 *filesysory;
extern uae_u8 *rtarea;

extern addrbank chipmem_bank;
extern addrbank chipmem_agnus_bank;
extern addrbank chipmem_bank_ce2;
extern addrbank kickmem_bank;
extern addrbank custom_bank;
extern addrbank clock_bank;
extern addrbank cia_bank;
extern addrbank rtarea_bank;
extern addrbank expamem_bank;
extern addrbank fastmem_bank;
extern addrbank gfxmem_bank;

extern void rtarea_init (void);
extern void rtarea_setup (void);
extern void expamem_init (void);
extern void expamem_reset (void);

extern uae_u32 gfxmem_start;
extern uae_u8 *gfxmemory;
extern uae_u32 gfxmem_mask;
extern int address_space_24;

/* Default memory access functions */

extern int default_check(uaecptr addr, uae_u32 size) REGPARAM;
extern uae_u8 *default_xlate(uaecptr addr) REGPARAM;

#define bankindex(addr) (((uaecptr)(addr)) >> 16)

extern addrbank *mem_banks[MEMORY_BANKS];
extern uae_u8 *baseaddr[MEMORY_BANKS];
#define get_mem_bank(addr) (*mem_banks[bankindex(addr)])
#define put_mem_bank(addr, b, realstart) do { \
    (mem_banks[bankindex(addr)] = (b)); \
    if ((b)->baseaddr) \
        baseaddr[bankindex(addr)] = (b)->baseaddr - (realstart); \
    else \
        baseaddr[bankindex(addr)] = (uae_u8*)(((long)b)+1); \
} while (0)

extern void memory_init (void);
extern void memory_cleanup (void);
extern void map_banks (addrbank *bank, int first, int count, int realsize);
extern void map_overlay (int chip);

#ifndef NO_INLINE_MEMORY_ACCESS

#define longget(addr) (call_mem_get_func(get_mem_bank(addr).lget, addr))
#define wordget(addr) (call_mem_get_func(get_mem_bank(addr).wget, addr))
#define byteget(addr) (call_mem_get_func(get_mem_bank(addr).bget, addr))
#define longput(addr,l) (call_mem_put_func(get_mem_bank(addr).lput, addr, l))
#define wordput(addr,w) (call_mem_put_func(get_mem_bank(addr).wput, addr, w))
#define byteput(addr,b) (call_mem_put_func(get_mem_bank(addr).bput, addr, b))

#else

extern uae_u32 alongget(uaecptr addr);
extern uae_u32 awordget(uaecptr addr);
extern uae_u32 longget(uaecptr addr);
extern uae_u32 wordget(uaecptr addr);
extern uae_u32 byteget(uaecptr addr);
extern void longput(uaecptr addr, uae_u32 l);
extern void wordput(uaecptr addr, uae_u32 w);
extern void byteput(uaecptr addr, uae_u32 b);

#endif

#ifndef MD_HAVE_MEM_1_FUNCS

#define longget_1 longget
#define wordget_1 wordget
#define byteget_1 byteget
#define longput_1 longput
#define wordput_1 wordput
#define byteput_1 byteput

#endif

STATIC_INLINE uae_u32 get_long(uaecptr addr)
{
    return longget_1(addr);
}
STATIC_INLINE uae_u32 get_word(uaecptr addr)
{
    return wordget_1(addr);
}
STATIC_INLINE uae_u32 get_byte(uaecptr addr)
{
    return byteget_1(addr);
}
STATIC_INLINE void put_long(uaecptr addr, uae_u32 l)
{
    longput_1(addr, l);
}
STATIC_INLINE void put_word(uaecptr addr, uae_u32 w)
{
    wordput_1(addr, w);
}
STATIC_INLINE void put_byte(uaecptr addr, uae_u32 b)
{
    byteput_1(addr, b);
}

STATIC_INLINE uae_u8 *get_real_address(uaecptr addr)
{
    return get_mem_bank(addr).xlateaddr(addr);
}

STATIC_INLINE int valid_address(uaecptr addr, uae_u32 size)
{
    return get_mem_bank(addr).check(addr, size);
}

/* For faster access in custom chip emulation.  */
extern uae_u32 chipmem_lget (uaecptr) REGPARAM;
extern uae_u32 chipmem_wget (uaecptr) REGPARAM;
extern uae_u32 chipmem_bget (uaecptr) REGPARAM;
extern void chipmem_lput (uaecptr, uae_u32) REGPARAM;
extern void chipmem_wput (uaecptr, uae_u32) REGPARAM;
extern void chipmem_bput (uaecptr, uae_u32) REGPARAM;

extern uae_u32 chipmem_agnus_lget (uaecptr) REGPARAM;
extern uae_u32 chipmem_agnus_wget (uaecptr) REGPARAM;
extern uae_u32 chipmem_agnus_bget (uaecptr) REGPARAM;
extern void chipmem_agnus_lput (uaecptr, uae_u32) REGPARAM;
extern void chipmem_agnus_wput (uaecptr, uae_u32) REGPARAM;
extern void chipmem_agnus_bput (uaecptr, uae_u32) REGPARAM;

extern uae_u32 chipmem_mask, kickmem_mask;
extern uae_u8 *kickmemory;
extern int kickmem_size;
extern addrbank dummy_bank;

/* 68020+ Chip RAM DMA contention emulation */
extern uae_u32 chipmem_lget_ce2 (uaecptr) REGPARAM;
extern uae_u32 chipmem_wget_ce2 (uaecptr) REGPARAM;
extern uae_u32 chipmem_bget_ce2 (uaecptr) REGPARAM;
extern void chipmem_lput_ce2 (uaecptr, uae_u32) REGPARAM;
extern void chipmem_wput_ce2 (uaecptr, uae_u32) REGPARAM;
extern void chipmem_bput_c2 (uaecptr, uae_u32) REGPARAM;

#ifdef NATMEM_OFFSET

typedef struct shmpiece_reg {
    uae_u8 *native_address;
    int id;
    uae_u32 size;
    struct shmpiece_reg *next;
    struct shmpiece_reg *prev;
} shmpiece;

extern shmpiece *shm_start;

#endif

extern int canbang;

extern uae_u8 *mapped_malloc (size_t, char *);
extern void mapped_free (uae_u8 *);
extern void clearexec (void);
extern void mapkick (void);
extern int read_kickstart (struct zfile *f, uae_u8 *mem, int size, int dochecksum, int *cloanto_rom);
extern void decode_cloanto_rom_do (uae_u8 *mem, int size, int real_size, uae_u8 *key, int keysize);

#define ROMTYPE_KICK 1
#define ROMTYPE_KICKCD32 2
#define ROMTYPE_EXTCD32 4
#define ROMTYPE_EXTCDTV 8
#define ROMTYPE_AR 16
#define ROMTYPE_KEY 32
#define ROMTYPE_ARCADIA 64

struct romdata {
    char *name;
    int version, revision;
    uae_u32 crc32;
    uae_u32 size;
    int id;
    int cpu;
    int cloanto;
    int type;
};

extern struct romdata *getromdatabycrc (uae_u32 crc32);
extern struct romdata *getromdatabydata (uae_u8 *rom, int size);
extern struct romdata *getromdatabyid (int id);
extern struct romdata *getromdatabyzfile (struct zfile *f);
extern struct romdata *getarcadiarombyname (char *name);
extern void getromname (struct romdata*, char*);
extern struct romdata *getromdatabyname (char*);
extern void romlist_add (char *path, struct romdata *rd);
extern char *romlist_get (struct romdata *rd);
extern void romlist_clear (void);

extern uae_u8 *load_keyfile (struct uae_prefs *p, char *path, int *size);
extern void free_keyfile (uae_u8 *key);
