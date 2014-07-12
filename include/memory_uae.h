/*
* UAE - The Un*x Amiga Emulator
*
* memory management
*
* Copyright 1995 Bernd Schmidt
*/

#ifndef MEMORY_H
#define MEMORY_H

extern void memory_reset (void);
extern void a1000_reset (void);

#ifdef JIT
extern int special_mem;

extern uae_u8 *cache_alloc (int);
extern void cache_free (uae_u8*);
#endif

#define S_READ 1
#define S_WRITE 2

bool init_shm (void);
void free_shm (void);
bool preinit_shm (void);
extern bool canbang;
extern int candirect;

#ifdef ADDRESS_SPACE_24BIT
#define MEMORY_BANKS 256
#define MEMORY_RANGE_MASK ((1<<24)-1)
#else
#define MEMORY_BANKS 65536
#define MEMORY_RANGE_MASK (~0)
#endif

typedef uae_u32 (REGPARAM3 *mem_get_func)(uaecptr) REGPARAM;
typedef void (REGPARAM3 *mem_put_func)(uaecptr, uae_u32) REGPARAM;
typedef uae_u8 *(REGPARAM3 *xlate_func)(uaecptr) REGPARAM;
typedef int (REGPARAM3 *check_func)(uaecptr, uae_u32) REGPARAM;

extern uae_u8 *address_space, *good_address_map;
extern uae_u32 max_z3fastmem;

extern uae_u32 wait_cpu_cycle_read (uaecptr addr, int mode);
extern void wait_cpu_cycle_write (uaecptr addr, int mode, uae_u32 v);
extern uae_u32 wait_cpu_cycle_read_ce020 (uaecptr addr, int mode);
extern void wait_cpu_cycle_write_ce020 (uaecptr addr, int mode, uae_u32 v);

#undef DIRECT_MEMFUNCS_SUCCESSFUL
#include "machdep/maccess.h"

#define chipmem_start_addr 0x00000000
#define bogomem_start_addr 0x00C00000
#define cardmem_start_addr 0x00E00000
#define kickmem_start_addr 0x00F80000

#define ROM_SIZE_512 524288
#define ROM_SIZE_256 262144

extern bool ersatzkickfile;
extern bool cloanto_rom, kickstart_rom;
extern uae_u16 kickstart_version;
extern bool uae_boot_rom;
extern int uae_boot_rom_size;
extern uaecptr rtarea_base;

extern uae_u8* baseaddr[];

enum { ABFLAG_UNK = 0, ABFLAG_RAM = 1, ABFLAG_ROM = 2, ABFLAG_ROMIN = 4, ABFLAG_IO = 8, ABFLAG_NONE = 16, ABFLAG_SAFE = 32 };
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
	TCHAR *name;
	/* for instruction opcode/operand fetches */
	mem_get_func lgeti, wgeti;
	int flags;
	uae_u32 mask;
	uae_u32 startmask;
	uae_u32 start;
	uae_u32 allocated;
} addrbank;

#define CE_MEMBANK_FAST32 0
#define CE_MEMBANK_CHIP16 1
#define CE_MEMBANK_CHIP32 2
#define CE_MEMBANK_CIA 3
#define CE_MEMBANK_FAST16 4
extern uae_u8 ce_banktype[65536], ce_cachable[65536];

#ifdef JIT
#define MEMORY_LGET(name, nojit) \
static uae_u32 REGPARAM3 name ## _lget (uaecptr) REGPARAM; \
static uae_u32 REGPARAM2 name ## _lget (uaecptr addr) \
{ \
	uae_u8 *m; \
	if (nojit) special_mem |= S_READ; \
	addr -= name ## _bank.start & name ## _bank.mask; \
	addr &= name ## _bank.mask; \
	m = name ## _bank.baseaddr + addr; \
	return do_get_mem_long ((uae_u32 *)m); \
}
#define MEMORY_WGET(name, nojit) \
static uae_u32 REGPARAM3 name ## _wget (uaecptr) REGPARAM; \
static uae_u32 REGPARAM2 name ## _wget (uaecptr addr) \
{ \
	uae_u8 *m; \
	if (nojit) special_mem |= S_READ; \
	addr -= name ## _bank.start & name ## _bank.mask; \
	addr &= name ## _bank.mask; \
	m = name ## _bank.baseaddr + addr; \
	return do_get_mem_word ((uae_u16 *)m); \
}
#define MEMORY_BGET(name, nojit) \
static uae_u32 REGPARAM3 name ## _bget (uaecptr) REGPARAM; \
static uae_u32 REGPARAM2 name ## _bget (uaecptr addr) \
{ \
	if (nojit) special_mem |= S_READ; \
	addr -= name ## _bank.start & name ## _bank.mask; \
	addr &= name ## _bank.mask; \
	return name ## _bank.baseaddr[addr]; \
}
#define MEMORY_LPUT(name, nojit) \
static void REGPARAM3 name ## _lput (uaecptr, uae_u32) REGPARAM; \
static void REGPARAM2 name ## _lput (uaecptr addr, uae_u32 l) \
{ \
	uae_u8 *m;  \
	if (nojit) special_mem |= S_WRITE; \
	addr -= name ## _bank.start & name ## _bank.mask; \
	addr &= name ## _bank.mask; \
	m = name ## _bank.baseaddr + addr; \
	do_put_mem_long ((uae_u32 *)m, l); \
}
#define MEMORY_WPUT(name, nojit) \
static void REGPARAM3 name ## _wput (uaecptr, uae_u32) REGPARAM; \
static void REGPARAM2 name ## _wput (uaecptr addr, uae_u32 w) \
{ \
	uae_u8 *m;  \
	if (nojit) special_mem |= S_WRITE; \
	addr -= name ## _bank.start & name ## _bank.mask; \
	addr &= name ## _bank.mask; \
	m = name ## _bank.baseaddr + addr; \
	do_put_mem_word ((uae_u16 *)m, w); \
}
#define MEMORY_BPUT(name, nojit) \
static void REGPARAM3 name ## _bput (uaecptr, uae_u32) REGPARAM; \
static void REGPARAM2 name ## _bput (uaecptr addr, uae_u32 b) \
{ \
	if (nojit) special_mem |= S_WRITE; \
	addr -= name ## _bank.start & name ## _bank.mask; \
	addr &= name ## _bank.mask; \
	name ## _bank.baseaddr[addr] = b; \
}
#define MEMORY_CHECK(name) \
static int REGPARAM3 name ## _check (uaecptr addr, uae_u32 size) REGPARAM; \
static int REGPARAM2 name ## _check (uaecptr addr, uae_u32 size) \
{ \
	addr -= name ## _bank.start & name ## _bank.mask; \
	addr &= name ## _bank.mask; \
	return (addr + size) <= name ## _bank.allocated; \
}
#define MEMORY_XLATE(name) \
static uae_u8 *REGPARAM3 name ## _xlate (uaecptr addr) REGPARAM; \
static uae_u8 *REGPARAM2 name ## _xlate (uaecptr addr) \
{ \
	addr -= name ## _bank.start & name ## _bank.mask; \
	addr &= name ## _bank.mask; \
	return name ## _bank.baseaddr + addr; \
}
#else
#define MEMORY_LGET(name, nojit) \
static uae_u32 REGPARAM3 name ## _lget (uaecptr) REGPARAM; \
static uae_u32 REGPARAM2 name ## _lget (uaecptr addr) \
{ \
	uae_u8 *m; \
	addr -= name ## _bank.start & name ## _bank.mask; \
	addr &= name ## _bank.mask; \
	m = name ## _bank.baseaddr + addr; \
	return do_get_mem_long ((uae_u32 *)m); \
}
#define MEMORY_WGET(name, nojit) \
static uae_u32 REGPARAM3 name ## _wget (uaecptr) REGPARAM; \
static uae_u32 REGPARAM2 name ## _wget (uaecptr addr) \
{ \
	uae_u8 *m; \
	addr -= name ## _bank.start & name ## _bank.mask; \
	addr &= name ## _bank.mask; \
	m = name ## _bank.baseaddr + addr; \
	return do_get_mem_word ((uae_u16 *)m); \
}
#define MEMORY_BGET(name, nojit) \
static uae_u32 REGPARAM3 name ## _bget (uaecptr) REGPARAM; \
static uae_u32 REGPARAM2 name ## _bget (uaecptr addr) \
{ \
	addr -= name ## _bank.start & name ## _bank.mask; \
	addr &= name ## _bank.mask; \
	return name ## _bank.baseaddr[addr]; \
}
#define MEMORY_LPUT(name, nojit) \
static void REGPARAM3 name ## _lput (uaecptr, uae_u32) REGPARAM; \
static void REGPARAM2 name ## _lput (uaecptr addr, uae_u32 l) \
{ \
	uae_u8 *m;  \
	addr -= name ## _bank.start & name ## _bank.mask; \
	addr &= name ## _bank.mask; \
	m = name ## _bank.baseaddr + addr; \
	do_put_mem_long ((uae_u32 *)m, l); \
}
#define MEMORY_WPUT(name, nojit) \
static void REGPARAM3 name ## _wput (uaecptr, uae_u32) REGPARAM; \
static void REGPARAM2 name ## _wput (uaecptr addr, uae_u32 w) \
{ \
	uae_u8 *m;  \
	addr -= name ## _bank.start & name ## _bank.mask; \
	addr &= name ## _bank.mask; \
	m = name ## _bank.baseaddr + addr; \
	do_put_mem_word ((uae_u16 *)m, w); \
}
#define MEMORY_BPUT(name, nojit) \
static void REGPARAM3 name ## _bput (uaecptr, uae_u32) REGPARAM; \
static void REGPARAM2 name ## _bput (uaecptr addr, uae_u32 b) \
{ \
	addr -= name ## _bank.start & name ## _bank.mask; \
	addr &= name ## _bank.mask; \
	name ## _bank.baseaddr[addr] = b; \
}
#define MEMORY_CHECK(name) \
static int REGPARAM3 name ## _check (uaecptr addr, uae_u32 size) REGPARAM; \
static int REGPARAM2 name ## _check (uaecptr addr, uae_u32 size) \
{ \
	addr -= name ## _bank.start & name ## _bank.mask; \
	addr &= name ## _bank.mask; \
	return (addr + size) <= name ## _bank.allocated; \
}
#define MEMORY_XLATE(name) \
static uae_u8 *REGPARAM3 name ## _xlate (uaecptr addr) REGPARAM; \
static uae_u8 *REGPARAM2 name ## _xlate (uaecptr addr) \
{ \
	addr -= name ## _bank.start & name ## _bank.mask; \
	addr &= name ## _bank.mask; \
	return name ## _bank.baseaddr + addr; \
}
#endif

#define MEMORY_FUNCTIONS(name) \
MEMORY_LGET(name, 0); \
MEMORY_WGET(name, 0); \
MEMORY_BGET(name, 0); \
MEMORY_LPUT(name, 0); \
MEMORY_WPUT(name, 0); \
MEMORY_BPUT(name, 0); \
MEMORY_CHECK(name); \
MEMORY_XLATE(name);
#define MEMORY_FUNCTIONS_NOJIT(name) \
MEMORY_LGET(name, 1); \
MEMORY_WGET(name, 1); \
MEMORY_BGET(name, 1); \
MEMORY_LPUT(name, 1); \
MEMORY_WPUT(name, 1); \
MEMORY_BPUT(name, 1); \
MEMORY_CHECK(name); \
MEMORY_XLATE(name);

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
extern addrbank fastmem_nojit_bank;
extern addrbank fastmem2_bank;
extern addrbank fastmem2_nojit_bank;
extern addrbank gfxmem_bank;
extern addrbank gayle_bank;
extern addrbank gayle2_bank;
extern addrbank mbres_bank;
extern addrbank akiko_bank;
extern addrbank cardmem_bank;
extern addrbank bogomem_bank;
extern addrbank z3fastmem_bank;
extern addrbank z3fastmem2_bank;
extern addrbank z3chipmem_bank;
extern addrbank a3000lmem_bank;
extern addrbank a3000hmem_bank;
extern addrbank extendedkickmem_bank;
extern addrbank extendedkickmem2_bank;
extern addrbank custmem1_bank;
extern addrbank custmem2_bank;

extern void rtarea_init (void);
extern void rtarea_init_mem (void);
extern void rtarea_setup (void);
extern void expamem_init (void);
extern void expamem_reset (void);
extern void expamem_next (void);

extern uae_u32 last_custom_value1;

/* Default memory access functions */

extern void dummy_put (uaecptr addr, int size, uae_u32 val);
extern uae_u32 dummy_get (uaecptr addr, int size, bool inst);

extern int REGPARAM3 default_check(uaecptr addr, uae_u32 size) REGPARAM;
extern uae_u8 *REGPARAM3 default_xlate(uaecptr addr) REGPARAM;
/* 680x0 opcode fetches */
extern uae_u32 REGPARAM3 dummy_lgeti (uaecptr addr) REGPARAM;
extern uae_u32 REGPARAM3 dummy_wgeti (uaecptr addr) REGPARAM;

#define bankindex(addr) (((uaecptr)(addr)) >> 16)

extern addrbank *mem_banks[MEMORY_BANKS];

#ifdef JIT
extern uae_u8 *baseaddr[MEMORY_BANKS];
#endif

#define get_mem_bank(addr) (*mem_banks[bankindex(addr)])

#ifdef JIT
#define put_mem_bank(addr, b, realstart) do { \
	(mem_banks[bankindex(addr)] = (b)); \
	if ((b)->baseaddr) \
	baseaddr[bankindex(addr)] = (b)->baseaddr - (realstart); \
	else \
	baseaddr[bankindex(addr)] = (uae_u8*)(((uae_u8*)b)+1); \
} while (0)
#else
#define put_mem_bank(addr, b, realstart) \
	(mem_banks[bankindex(addr)] = (b));
#endif

extern void memory_init (void);
extern void memory_cleanup (void);
extern void map_banks (addrbank *bank, int first, int count, int realsize);
extern void map_banks_quick (addrbank *bank, int first, int count, int realsize);
extern void map_banks_cond (addrbank *bank, int first, int count, int realsize);
extern void map_overlay (int chip);
extern void memory_hardreset (int);
extern void memory_clear (void);
extern void free_fastmemory (int);

#define longget(addr) (call_mem_get_func(get_mem_bank(addr).lget, addr))
#define wordget(addr) (call_mem_get_func(get_mem_bank(addr).wget, addr))
#define byteget(addr) (call_mem_get_func(get_mem_bank(addr).bget, addr))
#define longgeti(addr) (call_mem_get_func(get_mem_bank(addr).lgeti, addr))
#define wordgeti(addr) (call_mem_get_func(get_mem_bank(addr).wgeti, addr))
#define longput(addr,l) (call_mem_put_func(get_mem_bank(addr).lput, addr, l))
#define wordput(addr,w) (call_mem_put_func(get_mem_bank(addr).wput, addr, w))
#define byteput(addr,b) (call_mem_put_func(get_mem_bank(addr).bput, addr, b))

STATIC_INLINE uae_u32 get_long (uaecptr addr)
{
	return longget (addr);
}
STATIC_INLINE uae_u32 get_word (uaecptr addr)
{
	return wordget (addr);
}
STATIC_INLINE uae_u32 get_byte (uaecptr addr)
{
	return byteget (addr);
}
STATIC_INLINE uae_u32 get_longi(uaecptr addr)
{
	return longgeti (addr);
}
STATIC_INLINE uae_u32 get_wordi(uaecptr addr)
{
	return wordgeti (addr);
}

/*
* Read a host pointer from addr
*/
#if SIZEOF_VOID_P == 4
# define get_pointer(addr) ((void *)get_long (addr))
#else
# if SIZEOF_VOID_P == 8
STATIC_INLINE void *get_pointer (uaecptr addr)
{
	const unsigned int n = SIZEOF_VOID_P / 4;
	union {
		void    *ptr;
		uae_u32  longs[SIZEOF_VOID_P / 4];
	} p;
	unsigned int i;

	for (i = 0; i < n; i++) {
#ifdef WORDS_BIGENDIAN
		p.longs[i]     = get_long (addr + i * 4);
#else
		p.longs[n - 1 - i] = get_long (addr + i * 4);
#endif
	}
	return p.ptr;
}
# else
#  error "Unknown or unsupported pointer size."
# endif
#endif

STATIC_INLINE void put_long (uaecptr addr, uae_u32 l)
{
	longput(addr, l);
}
STATIC_INLINE void put_word (uaecptr addr, uae_u32 w)
{
	wordput(addr, w);
}
STATIC_INLINE void put_byte (uaecptr addr, uae_u32 b)
{
	byteput(addr, b);
}

extern void put_long_slow (uaecptr addr, uae_u32 v);
extern void put_word_slow (uaecptr addr, uae_u32 v);
extern void put_byte_slow (uaecptr addr, uae_u32 v);
extern uae_u32 get_long_slow (uaecptr addr);
extern uae_u32 get_word_slow (uaecptr addr);
extern uae_u32 get_byte_slow (uaecptr addr);


/*
* Store host pointer v at addr
*/
#if SIZEOF_VOID_P == 4
# define put_pointer(addr, p) (put_long ((addr), (uae_u32)(p)))
#else
# if SIZEOF_VOID_P == 8
STATIC_INLINE void put_pointer (uaecptr addr, void *v)
{
	const unsigned int n = SIZEOF_VOID_P / 4;
	union {
		void    *ptr;
		uae_u32  longs[SIZEOF_VOID_P / 4];
	} p;
	unsigned int i;

	p.ptr = v;

	for (i = 0; i < n; i++) {
#ifdef WORDS_BIGENDIAN
		put_long (addr + i * 4, p.longs[i]);
#else
		put_long (addr + i * 4, p.longs[n - 1 - i]);
#endif
	}
}
# endif
#endif

STATIC_INLINE uae_u8 *get_real_address (uaecptr addr)
{
	return get_mem_bank (addr).xlateaddr(addr);
}

STATIC_INLINE int valid_address (uaecptr addr, uae_u32 size)
{
	return get_mem_bank (addr).check(addr, size);
}

extern int addr_valid (const TCHAR*, uaecptr,uae_u32);

/* For faster access in custom chip emulation.  */
extern void REGPARAM3 chipmem_lput (uaecptr, uae_u32) REGPARAM;
extern void REGPARAM3 chipmem_wput (uaecptr, uae_u32) REGPARAM;
extern void REGPARAM3 chipmem_bput (uaecptr, uae_u32) REGPARAM;

extern uae_u32 REGPARAM3 chipmem_agnus_wget (uaecptr) REGPARAM;
extern void REGPARAM3 chipmem_agnus_wput (uaecptr, uae_u32) REGPARAM;

extern addrbank dummy_bank;

/* 68020+ Chip RAM DMA contention emulation */
extern void REGPARAM3 chipmem_bput_c2 (uaecptr, uae_u32) REGPARAM;

extern uae_u32 (REGPARAM3 *chipmem_lget_indirect)(uaecptr) REGPARAM;
extern uae_u32 (REGPARAM3 *chipmem_wget_indirect)(uaecptr) REGPARAM;
extern uae_u32 (REGPARAM3 *chipmem_bget_indirect)(uaecptr) REGPARAM;
extern void (REGPARAM3 *chipmem_lput_indirect)(uaecptr, uae_u32) REGPARAM;
extern void (REGPARAM3 *chipmem_wput_indirect)(uaecptr, uae_u32) REGPARAM;
extern void (REGPARAM3 *chipmem_bput_indirect)(uaecptr, uae_u32) REGPARAM;
extern int (REGPARAM3 *chipmem_check_indirect)(uaecptr, uae_u32) REGPARAM;
extern uae_u8 *(REGPARAM3 *chipmem_xlate_indirect)(uaecptr) REGPARAM;
 
#ifdef NATMEM_OFFSET

typedef struct shmpiece_reg {
	uae_u8 *native_address;
	int id;
	uae_u32 size;
	const TCHAR *name;
	struct shmpiece_reg *next;
	struct shmpiece_reg *prev;
} shmpiece;

extern shmpiece *shm_start;

#endif

extern uae_u8 *mapped_malloc (size_t, const TCHAR*);
extern void mapped_free (uae_u8 *);
extern void clearexec (void);
extern void mapkick (void);
extern void a3000_fakekick (int);

extern uaecptr strcpyha_safe (uaecptr dst, const uae_char *src);
extern uae_char *strcpyah_safe (uae_char *dst, uaecptr src, int maxsize);
extern void memcpyha_safe (uaecptr dst, const uae_u8 *src, int size);
extern void memcpyha (uaecptr dst, const uae_u8 *src, int size);
extern void memcpyah_safe (uae_u8 *dst, uaecptr src, int size);
extern void memcpyah (uae_u8 *dst, uaecptr src, int size);

extern uae_s32 getz2size (struct uae_prefs *p);
extern ULONG getz2endaddr (void);

#endif /* MEMORY_H */
