/*
* UAE - The Un*x Amiga Emulator
*
* Memory management
*
* (c) 1995 Bernd Schmidt
*/

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "uae.h"
#include "memory.h"
#include "rommgr.h"
#include "ersatz.h"
#include "zfile.h"
#include "custom.h"
#include "events.h"
#include "newcpu.h"
#include "autoconf.h"
#include "savestate.h"
#include "ar.h"
#include "crc32.h"
#include "gui.h"
#include "cdtv.h"
#include "akiko.h"
#include "arcadia.h"
#include "enforcer.h"
#include "a2091.h"
#include "gayle.h"
#include "debug.h"

bool canbang;
int candirect = -1;
#ifdef JIT
/* Set by each memory handler that does not simply access real memory. */
int special_mem;
#endif

static bool isdirectjit (void)
{
	return currprefs.cachesize && !currprefs.comptrustbyte;
}

static bool canjit (void)
{
	if (currprefs.cpu_model < 68020 && currprefs.address_space_24)
		return 0;
	return 1;
}

static void nocanbang (void)
{
	canbang = 0;
}

bool ersatzkickfile;

uae_u32 allocated_chipmem;
uae_u32 allocated_fastmem;
uae_u32 allocated_bogomem;
uae_u32 allocated_gfxmem;
uae_u32 allocated_z3fastmem, allocated_z3fastmem2, allocated_z3chipmem;
uae_u32 allocated_a3000lmem;
uae_u32 allocated_a3000hmem;
uae_u32 allocated_cardmem;
uae_u8 ce_banktype[65536];
uae_u8 ce_cachable[65536];

#if defined(CPU_64_BIT)
uae_u32 max_z3fastmem = 2048UL * 1024 * 1024;
#else
uae_u32 max_z3fastmem = 512 * 1024 * 1024;
#endif

static size_t bootrom_filepos, chip_filepos, bogo_filepos, rom_filepos, a3000lmem_filepos, a3000hmem_filepos;

/* Set if we notice during initialization that settings changed,
and we must clear all memory to prevent bogus contents from confusing
the Kickstart.  */
static bool need_hardreset;

/* The address space setting used during the last reset.  */
static bool last_address_space_24;

addrbank *mem_banks[MEMORY_BANKS];

/* This has two functions. It either holds a host address that, when added
to the 68k address, gives the host address corresponding to that 68k
address (in which case the value in this array is even), OR it holds the
same value as mem_banks, for those banks that have baseaddr==0. In that
case, bit 0 is set (the memory access routines will take care of it).  */

uae_u8 *baseaddr[MEMORY_BANKS];

#ifdef NO_INLINE_MEMORY_ACCESS
__inline__ uae_u32 longget (uaecptr addr)
{
	return call_mem_get_func (get_mem_bank (addr).lget, addr);
}
__inline__ uae_u32 wordget (uaecptr addr)
{
	return call_mem_get_func (get_mem_bank (addr).wget, addr);
}
__inline__ uae_u32 byteget (uaecptr addr)
{
	return call_mem_get_func (get_mem_bank (addr).bget, addr);
}
__inline__ void longput (uaecptr addr, uae_u32 l)
{
	call_mem_put_func (get_mem_bank (addr).lput, addr, l);
}
__inline__ void wordput (uaecptr addr, uae_u32 w)
{
	call_mem_put_func (get_mem_bank (addr).wput, addr, w);
}
__inline__ void byteput (uaecptr addr, uae_u32 b)
{
	call_mem_put_func (get_mem_bank (addr).bput, addr, b);
}
#endif

int addr_valid (TCHAR *txt, uaecptr addr, uae_u32 len)
{
	addrbank *ab = &get_mem_bank(addr);
	if (ab == 0 || !(ab->flags & ABFLAG_RAM) || addr < 0x100 || len < 0 || len > 16777215 || !valid_address (addr, len)) {
		write_log (L"corrupt %s pointer %x (%d) detected!\n", txt, addr, len);
		return 0;
	}
	return 1;
}

uae_u32	chipmem_mask, chipmem_full_mask, chipmem_full_size;
uae_u32 kickmem_mask, extendedkickmem_mask, extendedkickmem2_mask, bogomem_mask;
uae_u32 a3000lmem_mask, a3000hmem_mask, cardmem_mask;

static int illegal_count;
/* A dummy bank that only contains zeros */

static uae_u32 REGPARAM3 dummy_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 dummy_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 dummy_bget (uaecptr) REGPARAM;
static void REGPARAM3 dummy_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 dummy_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 dummy_bput (uaecptr, uae_u32) REGPARAM;
static int REGPARAM3 dummy_check (uaecptr addr, uae_u32 size) REGPARAM;

#define	MAX_ILG 200
#define NONEXISTINGDATA 0
//#define NONEXISTINGDATA 0xffffffff

static void dummylog (int rw, uaecptr addr, int size, uae_u32 val, int ins)
{
	if (M68K_GETPC == 0xf81a16)
		activate_debugger ();
	if (illegal_count >= MAX_ILG)
		return;
	/* ignore Zorro3 expansion space */
	if (addr >= 0xff000000 && addr <= 0xff000200)
		return;
	/* autoconfig and extended rom */
	if (addr >= 0xe00000 && addr <= 0xf7ffff)
		return;
	/* motherboard ram */
	if (addr >= 0x08000000 && addr <= 0x08000007)
		return;
	if (addr >= 0x07f00000 && addr <= 0x07f00007)
		return;
	if (addr >= 0x07f7fff0 && addr <= 0x07ffffff)
		return;
	if (MAX_ILG >= 0)
		illegal_count++;
	if (ins) {
		write_log (L"WARNING: Illegal opcode %cget at %08lx PC=%x\n",
			size == 2 ? 'w' : 'l', addr, M68K_GETPC);
	} else if (rw) {
		write_log (L"Illegal %cput at %08lx=%08lx PC=%x\n",
			size == 1 ? 'b' : size == 2 ? 'w' : 'l', addr, val, M68K_GETPC);
	} else {
		write_log (L"Illegal %cget at %08lx PC=%x\n",
			size == 1 ? 'b' : size == 2 ? 'w' : 'l', addr, M68K_GETPC);
	}
}

static uae_u32 dummy_get (uaecptr addr, int size)
{
	uae_u32 v;
	if (currprefs.cpu_model >= 68020)
		return NONEXISTINGDATA;
	v = (regs.irc << 16) | regs.irc;
	if (size == 4) {
		;
	} else if (size == 2) {
		v &= 0xffff;
	} else {
		v = (addr & 1) ? (v & 0xff) : ((v >> 8) & 0xff);
	}
#if 0
	if (addr >= 0x10000000)
		write_log (L"%08X %d = %08x\n", addr, size, v);
#endif
	return v;
}

static uae_u32 REGPARAM2 dummy_lget (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	if (currprefs.illegal_mem)
		dummylog (0, addr, 4, 0, 0);
	return dummy_get (addr, 4);
}
uae_u32 REGPARAM2 dummy_lgeti (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	if (currprefs.illegal_mem)
		dummylog (0, addr, 4, 0, 1);
	return dummy_get (addr, 4);
}

static uae_u32 REGPARAM2 dummy_wget (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	if (currprefs.illegal_mem)
		dummylog (0, addr, 2, 0, 0);
	return dummy_get (addr, 2);
}
uae_u32 REGPARAM2 dummy_wgeti (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	if (currprefs.illegal_mem)
		dummylog (0, addr, 2, 0, 1);
	return dummy_get (addr, 2);
}

static uae_u32 REGPARAM2 dummy_bget (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	if (currprefs.illegal_mem)
		dummylog (0, addr, 1, 0, 0);
	return dummy_get (addr, 1);
}

static void REGPARAM2 dummy_lput (uaecptr addr, uae_u32 l)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (currprefs.illegal_mem)
		dummylog (1, addr, 4, l, 0);
}
static void REGPARAM2 dummy_wput (uaecptr addr, uae_u32 w)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (currprefs.illegal_mem)
		dummylog (1, addr, 2, w, 0);
}
static void REGPARAM2 dummy_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (currprefs.illegal_mem)
		dummylog (1, addr, 1, b, 0);
}

static int REGPARAM2 dummy_check (uaecptr addr, uae_u32 size)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	return 0;
}

static void REGPARAM2 none_put (uaecptr addr, uae_u32 v)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
}
static uae_u32 REGPARAM2 ones_get (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	return 0xffffffff;
}

/* Chip memory */

uae_u8 *chipmemory;

static int REGPARAM3 chipmem_check (uaecptr addr, uae_u32 size) REGPARAM;
static uae_u8 *REGPARAM3 chipmem_xlate (uaecptr addr) REGPARAM;

#ifdef AGA

/* AGA ce-chipram access */

static void ce2_timeout (void)
{
	wait_cpu_cycle_read (0, -1);
}

static uae_u32 REGPARAM2 chipmem_lget_ce2 (uaecptr addr)
{
	uae_u32 *m;

#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= chipmem_mask;
	m = (uae_u32 *)(chipmemory + addr);
	ce2_timeout ();
	return do_get_mem_long (m);
}

static uae_u32 REGPARAM2 chipmem_wget_ce2 (uaecptr addr)
{
	uae_u16 *m, v;

#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= chipmem_mask;
	m = (uae_u16 *)(chipmemory + addr);
	ce2_timeout ();
	v = do_get_mem_word (m);
	//last_custom_value = v;
	return v;
}

static uae_u32 REGPARAM2 chipmem_bget_ce2 (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= chipmem_mask;
	ce2_timeout ();
	return chipmemory[addr];
}

static void REGPARAM2 chipmem_lput_ce2 (uaecptr addr, uae_u32 l)
{
	uae_u32 *m;

#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= chipmem_mask;
	m = (uae_u32 *)(chipmemory + addr);
	ce2_timeout ();
	do_put_mem_long (m, l);
}

static void REGPARAM2 chipmem_wput_ce2 (uaecptr addr, uae_u32 w)
{
	uae_u16 *m;

#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= chipmem_mask;
	m = (uae_u16 *)(chipmemory + addr);
	ce2_timeout ();
	//last_custom_value = w;
	do_put_mem_word (m, w);
}

static void REGPARAM2 chipmem_bput_ce2 (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= chipmem_mask;
	ce2_timeout ();
	chipmemory[addr] = b;
}

#endif

uae_u32 REGPARAM2 chipmem_lget (uaecptr addr)
{
	uae_u32 *m;

	addr &= chipmem_mask;
	m = (uae_u32 *)(chipmemory + addr);
	return do_get_mem_long (m);
}

static uae_u32 REGPARAM2 chipmem_wget (uaecptr addr)
{
	uae_u16 *m, v;

	addr &= chipmem_mask;
	m = (uae_u16 *)(chipmemory + addr);
	v = do_get_mem_word (m);
	//last_custom_value = v;
	return v;
}

static uae_u32 REGPARAM2 chipmem_bget (uaecptr addr)
{
	uae_u8 v;
	addr &= chipmem_mask;
	v = chipmemory[addr];
	//last_custom_value = (v << 8) | v;
	return v;
}

void REGPARAM2 chipmem_lput (uaecptr addr, uae_u32 l)
{
	uae_u32 *m;

	addr &= chipmem_mask;
	m = (uae_u32 *)(chipmemory + addr);
	do_put_mem_long (m, l);
}

void REGPARAM2 chipmem_wput (uaecptr addr, uae_u32 w)
{
	uae_u16 *m;

	addr &= chipmem_mask;
	m = (uae_u16 *)(chipmemory + addr);
	//last_custom_value = w;
	do_put_mem_word (m, w);
}

void REGPARAM2 chipmem_bput (uaecptr addr, uae_u32 b)
{
	addr &= chipmem_mask;
	//last_custom_value = (b << 8) | b;
	chipmemory[addr] = b;
}

/* cpu chipmem access inside agnus addressable ram but no ram available */
static uae_u32 chipmem_dummy (void)
{
	/* not really right but something random that has more ones than zeros.. */
	return 0xffff & ~((1 << (rand () & 31)) | (1 << (rand () & 31)));
}
void REGPARAM2 chipmem_dummy_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
}
void REGPARAM2 chipmem_dummy_wput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
}
void REGPARAM2 chipmem_dummy_lput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
}
static uae_u32 REGPARAM2 chipmem_dummy_bget (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	return chipmem_dummy ();
}
static uae_u32 REGPARAM2 chipmem_dummy_wget (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	return chipmem_dummy ();
}
static uae_u32 REGPARAM2 chipmem_dummy_lget (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	return (chipmem_dummy () << 16) | chipmem_dummy ();
}

static uae_u32 REGPARAM2 chipmem_agnus_lget (uaecptr addr)
{
	uae_u32 *m;

	addr &= chipmem_full_mask;
	m = (uae_u32 *)(chipmemory + addr);
	return do_get_mem_long (m);
}

uae_u32 REGPARAM2 chipmem_agnus_wget (uaecptr addr)
{
	uae_u16 *m;

	addr &= chipmem_full_mask;
	m = (uae_u16 *)(chipmemory + addr);
	return do_get_mem_word (m);
}

static uae_u32 REGPARAM2 chipmem_agnus_bget (uaecptr addr)
{
	addr &= chipmem_full_mask;
	return chipmemory[addr];
}

static void REGPARAM2 chipmem_agnus_lput (uaecptr addr, uae_u32 l)
{
	uae_u32 *m;

	addr &= chipmem_full_mask;
	if (addr >= chipmem_full_size)
		return;
	m = (uae_u32 *)(chipmemory + addr);
	do_put_mem_long (m, l);
}

void REGPARAM2 chipmem_agnus_wput (uaecptr addr, uae_u32 w)
{
	uae_u16 *m;

	addr &= chipmem_full_mask;
	if (addr >= chipmem_full_size)
		return;
	m = (uae_u16 *)(chipmemory + addr);
	do_put_mem_word (m, w);
}

static void REGPARAM2 chipmem_agnus_bput (uaecptr addr, uae_u32 b)
{
	addr &= chipmem_full_mask;
	if (addr >= chipmem_full_size)
		return;
	chipmemory[addr] = b;
}

static int REGPARAM2 chipmem_check (uaecptr addr, uae_u32 size)
{
	addr &= chipmem_mask;
	return (addr + size) <= chipmem_full_size;
}

static uae_u8 *REGPARAM2 chipmem_xlate (uaecptr addr)
{
	addr &= chipmem_mask;
	return chipmemory + addr;
}

STATIC_INLINE void REGPARAM2 chipmem_lput_bigmem (uaecptr addr, uae_u32 v)
{
	put_long (addr, v);
}
STATIC_INLINE void REGPARAM2 chipmem_wput_bigmem (uaecptr addr, uae_u32 v)
{
	put_word (addr, v);
}
STATIC_INLINE void REGPARAM2 chipmem_bput_bigmem (uaecptr addr, uae_u32 v)
{
	put_byte (addr, v);
}
STATIC_INLINE uae_u32 REGPARAM2 chipmem_lget_bigmem (uaecptr addr)
{
	return get_long (addr);
}
STATIC_INLINE uae_u32 REGPARAM2 chipmem_wget_bigmem (uaecptr addr)
{
	return get_word (addr);
}
STATIC_INLINE uae_u32 REGPARAM2 chipmem_bget_bigmem (uaecptr addr)
{
	return get_byte (addr);
}
STATIC_INLINE int REGPARAM2 chipmem_check_bigmem (uaecptr addr, uae_u32 size)
{
	return valid_address (addr, size);
}
STATIC_INLINE uae_u8* REGPARAM2 chipmem_xlate_bigmem (uaecptr addr)
{
	return get_real_address (addr);
}

uae_u32 (REGPARAM2 *chipmem_lget_indirect)(uaecptr);
uae_u32 (REGPARAM2 *chipmem_wget_indirect)(uaecptr);
uae_u32 (REGPARAM2 *chipmem_bget_indirect)(uaecptr);
void (REGPARAM2 *chipmem_lput_indirect)(uaecptr, uae_u32);
void (REGPARAM2 *chipmem_wput_indirect)(uaecptr, uae_u32);
void (REGPARAM2 *chipmem_bput_indirect)(uaecptr, uae_u32);
int (REGPARAM2 *chipmem_check_indirect)(uaecptr, uae_u32);
uae_u8 *(REGPARAM2 *chipmem_xlate_indirect)(uaecptr);

static void chipmem_setindirect (void)
{
	if (currprefs.z3chipmem_size) {
		chipmem_lget_indirect = chipmem_lget_bigmem;
		chipmem_wget_indirect = chipmem_wget_bigmem;
		chipmem_bget_indirect = chipmem_bget_bigmem;
		chipmem_lput_indirect = chipmem_lput_bigmem;
		chipmem_wput_indirect = chipmem_wput_bigmem;
		chipmem_bput_indirect = chipmem_bput_bigmem;
		chipmem_check_indirect = chipmem_check_bigmem;
		chipmem_xlate_indirect = chipmem_xlate_bigmem;
	} else {
		chipmem_lget_indirect = chipmem_lget;
		chipmem_wget_indirect = chipmem_agnus_wget;
		chipmem_bget_indirect = chipmem_agnus_bget;
		chipmem_lput_indirect = chipmem_lput;
		chipmem_wput_indirect = chipmem_agnus_wput;
		chipmem_bput_indirect = chipmem_agnus_bput;
		chipmem_check_indirect = chipmem_check;
		chipmem_xlate_indirect = chipmem_xlate;
	}
}

/* Slow memory */

static uae_u8 *bogomemory;
static int bogomemory_allocated;

static uae_u32 REGPARAM3 bogomem_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 bogomem_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 bogomem_bget (uaecptr) REGPARAM;
static void REGPARAM3 bogomem_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 bogomem_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 bogomem_bput (uaecptr, uae_u32) REGPARAM;
static int REGPARAM3 bogomem_check (uaecptr addr, uae_u32 size) REGPARAM;
static uae_u8 *REGPARAM3 bogomem_xlate (uaecptr addr) REGPARAM;

static uae_u32 REGPARAM2 bogomem_lget (uaecptr addr)
{
	uae_u32 *m;
	addr &= bogomem_mask;
	m = (uae_u32 *)(bogomemory + addr);
	return do_get_mem_long (m);
}

static uae_u32 REGPARAM2 bogomem_wget (uaecptr addr)
{
	uae_u16 *m;
	addr &= bogomem_mask;
	m = (uae_u16 *)(bogomemory + addr);
	return do_get_mem_word (m);
}

static uae_u32 REGPARAM2 bogomem_bget (uaecptr addr)
{
	addr &= bogomem_mask;
	return bogomemory[addr];
}

static void REGPARAM2 bogomem_lput (uaecptr addr, uae_u32 l)
{
	uae_u32 *m;
	addr &= bogomem_mask;
	m = (uae_u32 *)(bogomemory + addr);
	do_put_mem_long (m, l);
}

static void REGPARAM2 bogomem_wput (uaecptr addr, uae_u32 w)
{
	uae_u16 *m;
	addr &= bogomem_mask;
	m = (uae_u16 *)(bogomemory + addr);
	do_put_mem_word (m, w);
}

static void REGPARAM2 bogomem_bput (uaecptr addr, uae_u32 b)
{
	addr &= bogomem_mask;
	bogomemory[addr] = b;
}

static int REGPARAM2 bogomem_check (uaecptr addr, uae_u32 size)
{
	addr &= bogomem_mask;
	return (addr + size) <= allocated_bogomem;
}

static uae_u8 *REGPARAM2 bogomem_xlate (uaecptr addr)
{
	addr &= bogomem_mask;
	return bogomemory + addr;
}

/* CDTV expension memory card memory */

uae_u8 *cardmemory;

static uae_u32 REGPARAM3 cardmem_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 cardmem_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 cardmem_bget (uaecptr) REGPARAM;
static void REGPARAM3 cardmem_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 cardmem_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 cardmem_bput (uaecptr, uae_u32) REGPARAM;
static int REGPARAM3 cardmem_check (uaecptr addr, uae_u32 size) REGPARAM;
static uae_u8 *REGPARAM3 cardmem_xlate (uaecptr addr) REGPARAM;

static uae_u32 REGPARAM2 cardmem_lget (uaecptr addr)
{
	uae_u32 *m;
	addr &= cardmem_mask;
	m = (uae_u32 *)(cardmemory + addr);
	return do_get_mem_long (m);
}

static uae_u32 REGPARAM2 cardmem_wget (uaecptr addr)
{
	uae_u16 *m;
	addr &= cardmem_mask;
	m = (uae_u16 *)(cardmemory + addr);
	return do_get_mem_word (m);
}

static uae_u32 REGPARAM2 cardmem_bget (uaecptr addr)
{
	addr &= cardmem_mask;
	return cardmemory[addr];
}

static void REGPARAM2 cardmem_lput (uaecptr addr, uae_u32 l)
{
	uae_u32 *m;
	addr &= cardmem_mask;
	m = (uae_u32 *)(cardmemory + addr);
	do_put_mem_long (m, l);
}

static void REGPARAM2 cardmem_wput (uaecptr addr, uae_u32 w)
{
	uae_u16 *m;
	addr &= cardmem_mask;
	m = (uae_u16 *)(cardmemory + addr);
	do_put_mem_word (m, w);
}

static void REGPARAM2 cardmem_bput (uaecptr addr, uae_u32 b)
{
	addr &= cardmem_mask;
	cardmemory[addr] = b;
}

static int REGPARAM2 cardmem_check (uaecptr addr, uae_u32 size)
{
	addr &= cardmem_mask;
	return (addr + size) <= allocated_cardmem;
}

static uae_u8 *REGPARAM2 cardmem_xlate (uaecptr addr)
{
	addr &= cardmem_mask;
	return cardmemory + addr;
}

/* A3000 motherboard fast memory */
static uae_u8 *a3000lmemory, *a3000hmemory;
uae_u32 a3000lmem_start, a3000hmem_start;

static uae_u32 REGPARAM3 a3000lmem_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 a3000lmem_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 a3000lmem_bget (uaecptr) REGPARAM;
static void REGPARAM3 a3000lmem_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 a3000lmem_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 a3000lmem_bput (uaecptr, uae_u32) REGPARAM;
static int REGPARAM3 a3000lmem_check (uaecptr addr, uae_u32 size) REGPARAM;
static uae_u8 *REGPARAM3 a3000lmem_xlate (uaecptr addr) REGPARAM;

static uae_u32 REGPARAM2 a3000lmem_lget (uaecptr addr)
{
	uae_u32 *m;
	addr &= a3000lmem_mask;
	m = (uae_u32 *)(a3000lmemory + addr);
	return do_get_mem_long (m);
}

static uae_u32 REGPARAM2 a3000lmem_wget (uaecptr addr)
{
	uae_u16 *m;
	addr &= a3000lmem_mask;
	m = (uae_u16 *)(a3000lmemory + addr);
	return do_get_mem_word (m);
}

static uae_u32 REGPARAM2 a3000lmem_bget (uaecptr addr)
{
	addr &= a3000lmem_mask;
	return a3000lmemory[addr];
}

static void REGPARAM2 a3000lmem_lput (uaecptr addr, uae_u32 l)
{
	uae_u32 *m;
	addr &= a3000lmem_mask;
	m = (uae_u32 *)(a3000lmemory + addr);
	do_put_mem_long (m, l);
}

static void REGPARAM2 a3000lmem_wput (uaecptr addr, uae_u32 w)
{
	uae_u16 *m;
	addr &= a3000lmem_mask;
	m = (uae_u16 *)(a3000lmemory + addr);
	do_put_mem_word (m, w);
}

static void REGPARAM2 a3000lmem_bput (uaecptr addr, uae_u32 b)
{
	addr &= a3000lmem_mask;
	a3000lmemory[addr] = b;
}

static int REGPARAM2 a3000lmem_check (uaecptr addr, uae_u32 size)
{
	addr &= a3000lmem_mask;
	return (addr + size) <= allocated_a3000lmem;
}

static uae_u8 *REGPARAM2 a3000lmem_xlate (uaecptr addr)
{
	addr &= a3000lmem_mask;
	return a3000lmemory + addr;
}

static uae_u32 REGPARAM3 a3000hmem_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 a3000hmem_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 a3000hmem_bget (uaecptr) REGPARAM;
static void REGPARAM3 a3000hmem_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 a3000hmem_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 a3000hmem_bput (uaecptr, uae_u32) REGPARAM;
static int REGPARAM3 a3000hmem_check (uaecptr addr, uae_u32 size) REGPARAM;
static uae_u8 *REGPARAM3 a3000hmem_xlate (uaecptr addr) REGPARAM;

static uae_u32 REGPARAM2 a3000hmem_lget (uaecptr addr)
{
	uae_u32 *m;
	addr &= a3000hmem_mask;
	m = (uae_u32 *)(a3000hmemory + addr);
	return do_get_mem_long (m);
}

static uae_u32 REGPARAM2 a3000hmem_wget (uaecptr addr)
{
	uae_u16 *m;
	addr &= a3000hmem_mask;
	m = (uae_u16 *)(a3000hmemory + addr);
	return do_get_mem_word (m);
}

static uae_u32 REGPARAM2 a3000hmem_bget (uaecptr addr)
{
	addr &= a3000hmem_mask;
	return a3000hmemory[addr];
}

static void REGPARAM2 a3000hmem_lput (uaecptr addr, uae_u32 l)
{
	uae_u32 *m;
	addr &= a3000hmem_mask;
	m = (uae_u32 *)(a3000hmemory + addr);
	do_put_mem_long (m, l);
}

static void REGPARAM2 a3000hmem_wput (uaecptr addr, uae_u32 w)
{
	uae_u16 *m;
	addr &= a3000hmem_mask;
	m = (uae_u16 *)(a3000hmemory + addr);
	do_put_mem_word (m, w);
}

static void REGPARAM2 a3000hmem_bput (uaecptr addr, uae_u32 b)
{
	addr &= a3000hmem_mask;
	a3000hmemory[addr] = b;
}

static int REGPARAM2 a3000hmem_check (uaecptr addr, uae_u32 size)
{
	addr &= a3000hmem_mask;
	return (addr + size) <= allocated_a3000hmem;
}

static uae_u8 *REGPARAM2 a3000hmem_xlate (uaecptr addr)
{
	addr &= a3000hmem_mask;
	return a3000hmemory + addr;
}

/* Kick memory */

uae_u8 *kickmemory;
uae_u16 kickstart_version;
static int kickmem_size;

/*
* A1000 kickstart RAM handling
*
* RESET instruction unhides boot ROM and disables write protection
* write access to boot ROM hides boot ROM and enables write protection
*
*/
static int a1000_kickstart_mode;
static uae_u8 *a1000_bootrom;
static void a1000_handle_kickstart (int mode)
{
	if (!a1000_bootrom)
		return;
	if (mode == 0) {
		a1000_kickstart_mode = 0;
		memcpy (kickmemory, kickmemory + 262144, 262144);
		kickstart_version = (kickmemory[262144 + 12] << 8) | kickmemory[262144 + 13];
	} else {
		a1000_kickstart_mode = 1;
		memcpy (kickmemory, a1000_bootrom, 262144);
		kickstart_version = 0;
	}
	if (kickstart_version == 0xffff)
		kickstart_version = 0;
}

void a1000_reset (void)
{
	a1000_handle_kickstart (1);
}

static uae_u32 REGPARAM3 kickmem_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 kickmem_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 kickmem_bget (uaecptr) REGPARAM;
static void REGPARAM3 kickmem_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 kickmem_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 kickmem_bput (uaecptr, uae_u32) REGPARAM;
static int REGPARAM3 kickmem_check (uaecptr addr, uae_u32 size) REGPARAM;
static uae_u8 *REGPARAM3 kickmem_xlate (uaecptr addr) REGPARAM;

static uae_u32 REGPARAM2 kickmem_lget (uaecptr addr)
{
	uae_u32 *m;
	addr &= kickmem_mask;
	m = (uae_u32 *)(kickmemory + addr);
	return do_get_mem_long (m);
}

static uae_u32 REGPARAM2 kickmem_wget (uaecptr addr)
{
	uae_u16 *m;
	addr &= kickmem_mask;
	m = (uae_u16 *)(kickmemory + addr);
	return do_get_mem_word (m);
}

static uae_u32 REGPARAM2 kickmem_bget (uaecptr addr)
{
	addr &= kickmem_mask;
	return kickmemory[addr];
}

static void REGPARAM2 kickmem_lput (uaecptr addr, uae_u32 b)
{
	uae_u32 *m;
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (a1000_kickstart_mode) {
		if (addr >= 0xfc0000) {
			addr &= kickmem_mask;
			m = (uae_u32 *)(kickmemory + addr);
			do_put_mem_long (m, b);
			return;
		} else
			a1000_handle_kickstart (0);
	} else if (currprefs.illegal_mem)
		write_log (L"Illegal kickmem lput at %08lx\n", addr);
}

static void REGPARAM2 kickmem_wput (uaecptr addr, uae_u32 b)
{
	uae_u16 *m;
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (a1000_kickstart_mode) {
		if (addr >= 0xfc0000) {
			addr &= kickmem_mask;
			m = (uae_u16 *)(kickmemory + addr);
			do_put_mem_word (m, b);
			return;
		} else
			a1000_handle_kickstart (0);
	} else if (currprefs.illegal_mem)
		write_log (L"Illegal kickmem wput at %08lx\n", addr);
}

static void REGPARAM2 kickmem_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (a1000_kickstart_mode) {
		if (addr >= 0xfc0000) {
			addr &= kickmem_mask;
			kickmemory[addr] = b;
			return;
		} else
			a1000_handle_kickstart (0);
	} else if (currprefs.illegal_mem)
		write_log (L"Illegal kickmem bput at %08lx\n", addr);
}

static void REGPARAM2 kickmem2_lput (uaecptr addr, uae_u32 l)
{
	uae_u32 *m;
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= kickmem_mask;
	m = (uae_u32 *)(kickmemory + addr);
	do_put_mem_long (m, l);
}

static void REGPARAM2 kickmem2_wput (uaecptr addr, uae_u32 w)
{
	uae_u16 *m;
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= kickmem_mask;
	m = (uae_u16 *)(kickmemory + addr);
	do_put_mem_word (m, w);
}

static void REGPARAM2 kickmem2_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= kickmem_mask;
	kickmemory[addr] = b;
}

static int REGPARAM2 kickmem_check (uaecptr addr, uae_u32 size)
{
	addr &= kickmem_mask;
	return (addr + size) <= kickmem_size;
}

static uae_u8 *REGPARAM2 kickmem_xlate (uaecptr addr)
{
	addr &= kickmem_mask;
	return kickmemory + addr;
}

/* CD32/CDTV extended kick memory */

uae_u8 *extendedkickmemory, *extendedkickmemory2;
static int extendedkickmem_size, extendedkickmem2_size;
static uae_u32 extendedkickmem_start, extendedkickmem2_start;
static int extendedkickmem_type;

#define EXTENDED_ROM_CD32 1
#define EXTENDED_ROM_CDTV 2
#define EXTENDED_ROM_KS 3
#define EXTENDED_ROM_ARCADIA 4

static uae_u32 REGPARAM3 extendedkickmem_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 extendedkickmem_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 extendedkickmem_bget (uaecptr) REGPARAM;
static void REGPARAM3 extendedkickmem_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 extendedkickmem_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 extendedkickmem_bput (uaecptr, uae_u32) REGPARAM;
static int REGPARAM3 extendedkickmem_check (uaecptr addr, uae_u32 size) REGPARAM;
static uae_u8 *REGPARAM3 extendedkickmem_xlate (uaecptr addr) REGPARAM;
static uae_u32 REGPARAM2 extendedkickmem_lget (uaecptr addr)
{
	uae_u32 *m;
	addr -= extendedkickmem_start & extendedkickmem_mask;
	addr &= extendedkickmem_mask;
	m = (uae_u32 *)(extendedkickmemory + addr);
	return do_get_mem_long (m);
}
static uae_u32 REGPARAM2 extendedkickmem_wget (uaecptr addr)
{
	uae_u16 *m;
	addr -= extendedkickmem_start & extendedkickmem_mask;
	addr &= extendedkickmem_mask;
	m = (uae_u16 *)(extendedkickmemory + addr);
	return do_get_mem_word (m);
}
static uae_u32 REGPARAM2 extendedkickmem_bget (uaecptr addr)
{
	addr -= extendedkickmem_start & extendedkickmem_mask;
	addr &= extendedkickmem_mask;
	return extendedkickmemory[addr];
}
static void REGPARAM2 extendedkickmem_lput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (currprefs.illegal_mem)
		write_log (L"Illegal extendedkickmem lput at %08lx\n", addr);
}
static void REGPARAM2 extendedkickmem_wput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (currprefs.illegal_mem)
		write_log (L"Illegal extendedkickmem wput at %08lx\n", addr);
}
static void REGPARAM2 extendedkickmem_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (currprefs.illegal_mem)
		write_log (L"Illegal extendedkickmem lput at %08lx\n", addr);
}
static int REGPARAM2 extendedkickmem_check (uaecptr addr, uae_u32 size)
{
	addr -= extendedkickmem_start & extendedkickmem_mask;
	addr &= extendedkickmem_mask;
	return (addr + size) <= extendedkickmem_size;
}
static uae_u8 *REGPARAM2 extendedkickmem_xlate (uaecptr addr)
{
	addr -= extendedkickmem_start & extendedkickmem_mask;
	addr &= extendedkickmem_mask;
	return extendedkickmemory + addr;
}

static uae_u32 REGPARAM3 extendedkickmem2_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 extendedkickmem2_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 extendedkickmem2_bget (uaecptr) REGPARAM;
static void REGPARAM3 extendedkickmem2_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 extendedkickmem2_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 extendedkickmem2_bput (uaecptr, uae_u32) REGPARAM;
static int REGPARAM3 extendedkickmem2_check (uaecptr addr, uae_u32 size) REGPARAM;
static uae_u8 *REGPARAM3 extendedkickmem2_xlate (uaecptr addr) REGPARAM;
static uae_u32 REGPARAM2 extendedkickmem2_lget (uaecptr addr)
{
	uae_u32 *m;
	addr -= extendedkickmem2_start & extendedkickmem2_mask;
	addr &= extendedkickmem2_mask;
	m = (uae_u32 *)(extendedkickmemory2 + addr);
	return do_get_mem_long (m);
}
static uae_u32 REGPARAM2 extendedkickmem2_wget (uaecptr addr)
{
	uae_u16 *m;
	addr -= extendedkickmem2_start & extendedkickmem2_mask;
	addr &= extendedkickmem2_mask;
	m = (uae_u16 *)(extendedkickmemory2 + addr);
	return do_get_mem_word (m);
}
static uae_u32 REGPARAM2 extendedkickmem2_bget (uaecptr addr)
{
	addr -= extendedkickmem2_start & extendedkickmem2_mask;
	addr &= extendedkickmem2_mask;
	return extendedkickmemory2[addr];
}
static void REGPARAM2 extendedkickmem2_lput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (currprefs.illegal_mem)
		write_log (L"Illegal extendedkickmem2 lput at %08lx\n", addr);
}
static void REGPARAM2 extendedkickmem2_wput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (currprefs.illegal_mem)
		write_log (L"Illegal extendedkickmem2 wput at %08lx\n", addr);
}
static void REGPARAM2 extendedkickmem2_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (currprefs.illegal_mem)
		write_log (L"Illegal extendedkickmem2 lput at %08lx\n", addr);
}
static int REGPARAM2 extendedkickmem2_check (uaecptr addr, uae_u32 size)
{
	addr -= extendedkickmem2_start & extendedkickmem2_mask;
	addr &= extendedkickmem2_mask;
	return (addr + size) <= extendedkickmem2_size;
}
static uae_u8 *REGPARAM2 extendedkickmem2_xlate (uaecptr addr)
{
	addr -= extendedkickmem2_start & extendedkickmem2_mask;
	addr &= extendedkickmem2_mask;
	return extendedkickmemory2 + addr;
}


/* Default memory access functions */

int REGPARAM2 default_check (uaecptr a, uae_u32 b)
{
	return 0;
}

static int be_cnt;

uae_u8 *REGPARAM2 default_xlate (uaecptr a)
{
	if (quit_program == 0) {
		/* do this only in 68010+ mode, there are some tricky A500 programs.. */
		if ((currprefs.cpu_model > 68000 || !currprefs.cpu_compatible) && !currprefs.mmu_model) {
#if defined(ENFORCER)
			enforcer_disable ();
#endif

			if (be_cnt < 3) {
				int i, j;
				uaecptr a2 = a - 32;
				uaecptr a3 = m68k_getpc () - 32;
				write_log (L"Your Amiga program just did something terribly stupid %08X PC=%08X\n", a, M68K_GETPC);
				m68k_dumpstate (0, 0);
				for (i = 0; i < 10; i++) {
					write_log (L"%08X ", i >= 5 ? a3 : a2);
					for (j = 0; j < 16; j += 2) {
						write_log (L" %04X", get_word (i >= 5 ? a3 : a2));
						if (i >= 5) a3 += 2; else a2 += 2;
					}
					write_log (L"\n");
				}
				memory_map_dump ();
			}
			be_cnt++;
			if (be_cnt > 1000) {
				uae_reset (0);
				be_cnt = 0;
			} else {
				regs.panic = 1;
				regs.panic_pc = m68k_getpc ();
				regs.panic_addr = a;
				set_special (SPCFLAG_BRK);
			}
		}
	}
	return kickmem_xlate (2); /* So we don't crash. */
}

/* Address banks */

addrbank dummy_bank = {
	dummy_lget, dummy_wget, dummy_bget,
	dummy_lput, dummy_wput, dummy_bput,
	default_xlate, dummy_check, NULL, NULL,
	dummy_lgeti, dummy_wgeti, ABFLAG_NONE
};

addrbank ones_bank = {
	ones_get, ones_get, ones_get,
	none_put, none_put, none_put,
	default_xlate, dummy_check, NULL, L"Ones",
	dummy_lgeti, dummy_wgeti, ABFLAG_NONE
};

addrbank chipmem_bank = {
	chipmem_lget, chipmem_wget, chipmem_bget,
	chipmem_lput, chipmem_wput, chipmem_bput,
	chipmem_xlate, chipmem_check, NULL, L"Chip memory",
	chipmem_lget, chipmem_wget, ABFLAG_RAM
};

addrbank chipmem_dummy_bank = {
	chipmem_dummy_lget, chipmem_dummy_wget, chipmem_dummy_bget,
	chipmem_dummy_lput, chipmem_dummy_wput, chipmem_dummy_bput,
	default_xlate, dummy_check, NULL, L"Dummy Chip memory",
	dummy_lgeti, dummy_wgeti, ABFLAG_IO
};


#ifdef AGA
addrbank chipmem_bank_ce2 = {
	chipmem_lget_ce2, chipmem_wget_ce2, chipmem_bget_ce2,
	chipmem_lput_ce2, chipmem_wput_ce2, chipmem_bput_ce2,
	chipmem_xlate, chipmem_check, NULL, L"Chip memory (68020 'ce')",
	chipmem_lget_ce2, chipmem_wget_ce2, ABFLAG_RAM
};
#endif

addrbank bogomem_bank = {
	bogomem_lget, bogomem_wget, bogomem_bget,
	bogomem_lput, bogomem_wput, bogomem_bput,
	bogomem_xlate, bogomem_check, NULL, L"Slow memory",
	bogomem_lget, bogomem_wget, ABFLAG_RAM
};

addrbank cardmem_bank = {
	cardmem_lget, cardmem_wget, cardmem_bget,
	cardmem_lput, cardmem_wput, cardmem_bput,
	cardmem_xlate, cardmem_check, NULL, L"CDTV memory card",
	cardmem_lget, cardmem_wget, ABFLAG_RAM
};

addrbank a3000lmem_bank = {
	a3000lmem_lget, a3000lmem_wget, a3000lmem_bget,
	a3000lmem_lput, a3000lmem_wput, a3000lmem_bput,
	a3000lmem_xlate, a3000lmem_check, NULL, L"RAMSEY memory (low)",
	a3000lmem_lget, a3000lmem_wget, ABFLAG_RAM
};

addrbank a3000hmem_bank = {
	a3000hmem_lget, a3000hmem_wget, a3000hmem_bget,
	a3000hmem_lput, a3000hmem_wput, a3000hmem_bput,
	a3000hmem_xlate, a3000hmem_check, NULL, L"RAMSEY memory (high)",
	a3000hmem_lget, a3000hmem_wget, ABFLAG_RAM
};

addrbank kickmem_bank = {
	kickmem_lget, kickmem_wget, kickmem_bget,
	kickmem_lput, kickmem_wput, kickmem_bput,
	kickmem_xlate, kickmem_check, NULL, L"Kickstart ROM",
	kickmem_lget, kickmem_wget, ABFLAG_ROM
};

addrbank kickram_bank = {
	kickmem_lget, kickmem_wget, kickmem_bget,
	kickmem2_lput, kickmem2_wput, kickmem2_bput,
	kickmem_xlate, kickmem_check, NULL, L"Kickstart Shadow RAM",
	kickmem_lget, kickmem_wget, ABFLAG_UNK | ABFLAG_SAFE
};

addrbank extendedkickmem_bank = {
	extendedkickmem_lget, extendedkickmem_wget, extendedkickmem_bget,
	extendedkickmem_lput, extendedkickmem_wput, extendedkickmem_bput,
	extendedkickmem_xlate, extendedkickmem_check, NULL, L"Extended Kickstart ROM",
	extendedkickmem_lget, extendedkickmem_wget, ABFLAG_ROM
};
addrbank extendedkickmem2_bank = {
	extendedkickmem2_lget, extendedkickmem2_wget, extendedkickmem2_bget,
	extendedkickmem2_lput, extendedkickmem2_wput, extendedkickmem2_bput,
	extendedkickmem2_xlate, extendedkickmem2_check, NULL, L"Extended 2nd Kickstart ROM",
	extendedkickmem2_lget, extendedkickmem2_wget, ABFLAG_ROM
};


static uae_u32 allocated_custmem1, allocated_custmem2;
static uae_u32 custmem1_mask, custmem2_mask;
static uae_u8 *custmem1, *custmem2;

static uae_u32 REGPARAM3 custmem1_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 custmem1_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 custmem1_bget (uaecptr) REGPARAM;
static void REGPARAM3 custmem1_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 custmem1_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 custmem1_bput (uaecptr, uae_u32) REGPARAM;
static int REGPARAM3 custmem1_check (uaecptr addr, uae_u32 size) REGPARAM;
static uae_u8 *REGPARAM3 custmem1_xlate (uaecptr addr) REGPARAM;

static uae_u32 REGPARAM2 custmem1_lget (uaecptr addr)
{
	uae_u8 *m;
	addr -= currprefs.custom_memory_addrs[0] & custmem1_mask;
	addr &= custmem1_mask;
	m = custmem1 + addr;
	return do_get_mem_long ((uae_u32 *)m);
}
static uae_u32 REGPARAM2 custmem1_wget (uaecptr addr)
{
	uae_u8 *m;
	addr -= currprefs.custom_memory_addrs[0] & custmem1_mask;
	addr &= custmem1_mask;
	m = custmem1 + addr;
	return do_get_mem_word ((uae_u16 *)m);
}
static uae_u32 REGPARAM2 custmem1_bget (uaecptr addr)
{
	addr -= currprefs.custom_memory_addrs[0] & custmem1_mask;
	addr &= custmem1_mask;
	return custmem1[addr];
}
static void REGPARAM2 custmem1_lput (uaecptr addr, uae_u32 l)
{
	uae_u8 *m;
	addr -= currprefs.custom_memory_addrs[0] & custmem1_mask;
	addr &= custmem1_mask;
	m = custmem1 + addr;
	do_put_mem_long ((uae_u32 *)m, l);
}
static void REGPARAM2 custmem1_wput (uaecptr addr, uae_u32 w)
{
	uae_u8 *m;
	addr -= currprefs.custom_memory_addrs[0] & custmem1_mask;
	addr &= custmem1_mask;
	m = custmem1 + addr;
	do_put_mem_word ((uae_u16 *)m, w);
}
static void REGPARAM2 custmem1_bput (uaecptr addr, uae_u32 b)
{
	addr -= currprefs.custom_memory_addrs[0] & custmem1_mask;
	addr &= custmem1_mask;
	custmem1[addr] = b;
}
static int REGPARAM2 custmem1_check (uaecptr addr, uae_u32 size)
{
	addr -= currprefs.custom_memory_addrs[0] & custmem1_mask;
	addr &= custmem1_mask;
	return (addr + size) <= currprefs.custom_memory_sizes[0];
}
static uae_u8 *REGPARAM2 custmem1_xlate (uaecptr addr)
{
	addr -= currprefs.custom_memory_addrs[0] & custmem1_mask;
	addr &= custmem1_mask;
	return custmem1 + addr;
}

static uae_u32 REGPARAM3 custmem2_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 custmem2_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 custmem2_bget (uaecptr) REGPARAM;
static void REGPARAM3 custmem2_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 custmem2_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 custmem2_bput (uaecptr, uae_u32) REGPARAM;
static int REGPARAM3 custmem2_check (uaecptr addr, uae_u32 size) REGPARAM;
static uae_u8 *REGPARAM3 custmem2_xlate (uaecptr addr) REGPARAM;

static uae_u32 REGPARAM2 custmem2_lget (uaecptr addr)
{
	uae_u8 *m;
	addr -= currprefs.custom_memory_addrs[1] & custmem2_mask;
	addr &= custmem2_mask;
	m = custmem2 + addr;
	return do_get_mem_long ((uae_u32 *)m);
}
static uae_u32 REGPARAM2 custmem2_wget (uaecptr addr)
{
	uae_u8 *m;
	addr -= currprefs.custom_memory_addrs[1] & custmem2_mask;
	addr &= custmem2_mask;
	m = custmem2 + addr;
	return do_get_mem_word ((uae_u16 *)m);
}
static uae_u32 REGPARAM2 custmem2_bget (uaecptr addr)
{
	addr -= currprefs.custom_memory_addrs[1] & custmem2_mask;
	addr &= custmem2_mask;
	return custmem2[addr];
}
static void REGPARAM2 custmem2_lput (uaecptr addr, uae_u32 l)
{
	uae_u8 *m;
	addr -= currprefs.custom_memory_addrs[1] & custmem2_mask;
	addr &= custmem2_mask;
	m = custmem2 + addr;
	do_put_mem_long ((uae_u32 *)m, l);
}
static void REGPARAM2 custmem2_wput (uaecptr addr, uae_u32 w)
{
	uae_u8 *m;
	addr -= currprefs.custom_memory_addrs[1] & custmem2_mask;
	addr &= custmem2_mask;
	m = custmem2 + addr;
	do_put_mem_word ((uae_u16 *)m, w);
}
static void REGPARAM2 custmem2_bput (uaecptr addr, uae_u32 b)
{
	addr -= currprefs.custom_memory_addrs[1] & custmem2_mask;
	addr &= custmem2_mask;
	custmem2[addr] = b;
}
static int REGPARAM2 custmem2_check (uaecptr addr, uae_u32 size)
{
	addr -= currprefs.custom_memory_addrs[1] & custmem2_mask;
	addr &= custmem2_mask;
	return (addr + size) <= currprefs.custom_memory_sizes[1];
}
static uae_u8 *REGPARAM2 custmem2_xlate (uaecptr addr)
{
	addr -= currprefs.custom_memory_addrs[1] & custmem2_mask;
	addr &= custmem2_mask;
	return custmem2 + addr;
}

addrbank custmem1_bank = {
	custmem1_lget, custmem1_wget, custmem1_bget,
	custmem1_lput, custmem1_wput, custmem1_bput,
	custmem1_xlate, custmem1_check, NULL, L"Non-autoconfig RAM #1",
	custmem1_lget, custmem1_wget, ABFLAG_RAM
};
addrbank custmem2_bank = {
	custmem1_lget, custmem1_wget, custmem1_bget,
	custmem1_lput, custmem1_wput, custmem1_bput,
	custmem1_xlate, custmem1_check, NULL, L"Non-autoconfig RAM #2",
	custmem1_lget, custmem1_wget, ABFLAG_RAM
};

#define fkickmem_size 524288
static int a3000_f0;
void a3000_fakekick (int map)
{
	static uae_u8 *kickstore;

	if (map) {
		uae_u8 *fkickmemory = a3000lmemory + allocated_a3000lmem - fkickmem_size;
		if (fkickmemory[2] == 0x4e && fkickmemory[3] == 0xf9 && fkickmemory[4] == 0x00) {
			if (!kickstore)
				kickstore = xmalloc (uae_u8, fkickmem_size);
			memcpy (kickstore, kickmemory, fkickmem_size);
			if (fkickmemory[5] == 0xfc) {
				memcpy (kickmemory, fkickmemory, fkickmem_size / 2);
				memcpy (kickmemory + fkickmem_size / 2, fkickmemory, fkickmem_size / 2);
				extendedkickmem_size = 65536;
				extendedkickmem_mask = extendedkickmem_size - 1;
				extendedkickmemory = mapped_malloc (extendedkickmem_size, L"rom_f0");
				extendedkickmem_bank.baseaddr = extendedkickmemory;
				memcpy (extendedkickmemory, fkickmemory + fkickmem_size / 2, 65536);
				map_banks (&extendedkickmem_bank, 0xf0, 1, 1);
				a3000_f0 = 1;
			} else {
				memcpy (kickmemory, fkickmemory, fkickmem_size);
			}
		}
	} else {
		if (a3000_f0) {
			map_banks (&dummy_bank, 0xf0, 1, 1);
			mapped_free (extendedkickmemory);
			extendedkickmemory = NULL;
			a3000_f0 = 0;
		}
		if (kickstore)
			memcpy (kickmemory, kickstore, fkickmem_size);
		xfree (kickstore);
		kickstore = NULL;
	}
}

static uae_char *kickstring = "exec.library";
static int read_kickstart (struct zfile *f, uae_u8 *mem, int size, int dochecksum, int noalias)
{
	uae_char buffer[20];
	int i, j, oldpos;
	int cr = 0, kickdisk = 0;

	if (size < 0) {
		zfile_fseek (f, 0, SEEK_END);
		size = zfile_ftell (f) & ~0x3ff;
		zfile_fseek (f, 0, SEEK_SET);
	}
	oldpos = zfile_ftell (f);
	i = zfile_fread (buffer, 1, 11, f);
	if (!memcmp (buffer, "KICK", 4)) {
		zfile_fseek (f, 512, SEEK_SET);
		kickdisk = 1;
#if 0
	} else if (size >= 524288 && !memcmp (buffer, "AMIG", 4)) {
		/* ReKick */
		zfile_fseek (f, oldpos + 0x6c, SEEK_SET);
		cr = 2;
#endif
	} else if (memcmp ((uae_char*)buffer, "AMIROMTYPE1", 11) != 0) {
		zfile_fseek (f, oldpos, SEEK_SET);
	} else {
		cloanto_rom = 1;
		cr = 1;
	}

	memset (mem, 0, size);
	for (i = 0; i < 8; i++)
		mem[size - 16 + i * 2 + 1] = 0x18 + i;
	mem[size - 20] = size >> 24;
	mem[size - 19] = size >> 16;
	mem[size - 18] = size >>  8;
	mem[size - 17] = size >>  0;

	i = zfile_fread (mem, 1, size, f);

	if (kickdisk && i > 262144)
		i = 262144;
#if 0
	if (i >= 262144 && (i != 262144 && i != 524288 && i != 524288 * 2 && i != 524288 * 4)) {
		notify_user (NUMSG_KSROMREADERROR);
		return 0;
	}
#endif
	if (i < size - 20)
		kickstart_fix_checksum (mem, size);

	j = 1;
	while (j < i)
		j <<= 1;
	i = j;

	if (!noalias && i == size / 2)
		memcpy (mem + size / 2, mem, size / 2);

	if (cr) {
		if (!decode_rom (mem, size, cr, i))
			return 0;
	}
	if (currprefs.cs_a1000ram) {
		int off = 0;
		a1000_bootrom = xcalloc (uae_u8, 262144);
		while (off + i < 262144) {
			memcpy (a1000_bootrom + off, kickmemory, i);
			off += i;
		}
		memset (kickmemory, 0, kickmem_size);
		a1000_handle_kickstart (1);
		dochecksum = 0;
		i = 524288;
	}

	for (j = 0; j < 256 && i >= 262144; j++) {
		if (!memcmp (mem + j, kickstring, strlen (kickstring) + 1))
			break;
	}

	if (j == 256 || i < 262144)
		dochecksum = 0;
	if (dochecksum)
		kickstart_checksum (mem, size);
	return i;
}

static int load_extendedkickstart (void)
{
	struct zfile *f;
	int size, off;

	if (_tcslen (currprefs.romextfile) == 0)
		return 0;
	if (is_arcadia_rom (currprefs.romextfile) == ARCADIA_BIOS) {
		extendedkickmem_type = EXTENDED_ROM_ARCADIA;
		return 0;
	}
	f = read_rom_name (currprefs.romextfile);
	if (!f) {
		notify_user (NUMSG_NOEXTROM);
		return 0;
	}
	zfile_fseek (f, 0, SEEK_END);
	size = zfile_ftell (f);
	extendedkickmem_size = 524288;
	off = 0;
	if (currprefs.cs_cd32cd) {
		extendedkickmem_type = EXTENDED_ROM_CD32;
	} else if (currprefs.cs_cdtvcd || currprefs.cs_cdtvram) {
		extendedkickmem_type = EXTENDED_ROM_CDTV;
	} else if (size > 300000) {
		extendedkickmem_type = EXTENDED_ROM_CD32;
	} else if (need_uae_boot_rom () != 0xf00000) {
		extendedkickmem_type = EXTENDED_ROM_CDTV;
	} else
		return 0;

	zfile_fseek (f, off, SEEK_SET);
	switch (extendedkickmem_type) {
	case EXTENDED_ROM_CDTV:
		extendedkickmemory = mapped_malloc (extendedkickmem_size, L"rom_f0");
		extendedkickmem_bank.baseaddr = extendedkickmemory;
		break;
	case EXTENDED_ROM_CD32:
		extendedkickmemory = mapped_malloc (extendedkickmem_size, L"rom_e0");
		extendedkickmem_bank.baseaddr = extendedkickmemory;
		break;
	}
	read_kickstart (f, extendedkickmemory, extendedkickmem_size, 0, 1);
	extendedkickmem_mask = extendedkickmem_size - 1;
	zfile_fclose (f);
	return 1;
}

static int patch_shapeshifter (uae_u8 *kickmemory)
{
	/* Patch Kickstart ROM for ShapeShifter - from Christian Bauer.
	* Changes 'lea $400,a0' and 'lea $1000,a0' to 'lea $3000,a0' for
	* ShapeShifter compatability.
	*/
	int i, patched = 0;
	uae_u8 kickshift1[] = { 0x41, 0xf8, 0x04, 0x00 };
	uae_u8 kickshift2[] = { 0x41, 0xf8, 0x10, 0x00 };
	uae_u8 kickshift3[] = { 0x43, 0xf8, 0x04, 0x00 };

	for (i = 0x200; i < 0x300; i++) {
		if (!memcmp (kickmemory + i, kickshift1, sizeof (kickshift1)) ||
			!memcmp (kickmemory + i, kickshift2, sizeof (kickshift2)) ||
			!memcmp (kickmemory + i, kickshift3, sizeof (kickshift3))) {
				kickmemory[i + 2] = 0x30;
				write_log (L"Kickstart KickShifted @%04X\n", i);
				patched++;
		}
	}
	return patched;
}

/* disable incompatible drivers */
static int patch_residents (uae_u8 *kickmemory, int size)
{
	int i, j, patched = 0;
	uae_char *residents[] = { "NCR scsi.device", 0 };
	// "scsi.device", "carddisk.device", "card.resource" };
	uaecptr base = size == 524288 ? 0xf80000 : 0xfc0000;

	if (currprefs.cs_mbdmac == 2)
		residents[0] = NULL;
	for (i = 0; i < size - 100; i++) {
		if (kickmemory[i] == 0x4a && kickmemory[i + 1] == 0xfc) {
			uaecptr addr;
			addr = (kickmemory[i + 2] << 24) | (kickmemory[i + 3] << 16) | (kickmemory[i + 4] << 8) | (kickmemory[i + 5] << 0);
			if (addr != i + base)
				continue;
			addr = (kickmemory[i + 14] << 24) | (kickmemory[i + 15] << 16) | (kickmemory[i + 16] << 8) | (kickmemory[i + 17] << 0);
			if (addr >= base && addr < base + size) {
				j = 0;
				while (residents[j]) {
					if (!memcmp (residents[j], kickmemory + addr - base, strlen (residents[j]) + 1)) {
						TCHAR *s = au (residents[j]);
						write_log (L"KSPatcher: '%s' at %08X disabled\n", s, i + base);
						xfree (s);
						kickmemory[i] = 0x4b; /* destroy RTC_MATCHWORD */
						patched++;
						break;
					}
					j++;
				}
			}
		}
	}
	return patched;
}

static void patch_kick (void)
{
	int patched = 0;
	if (kickmem_size >= 524288 && currprefs.kickshifter)
		patched += patch_shapeshifter (kickmemory);
	patched += patch_residents (kickmemory, kickmem_size);
	if (extendedkickmemory) {
		patched += patch_residents (extendedkickmemory, extendedkickmem_size);
		if (patched)
			kickstart_fix_checksum (extendedkickmemory, extendedkickmem_size);
	}
	if (patched)
		kickstart_fix_checksum (kickmemory, kickmem_size);
}

static int load_kickstart (void)
{
	struct zfile *f;
	TCHAR tmprom[MAX_DPATH], tmprom2[MAX_DPATH];
	int patched = 0;

	cloanto_rom = 0;
	f = read_rom_name (currprefs.romfile);
	_tcscpy (tmprom, currprefs.romfile);
	if (f == NULL) {
		_stprintf (tmprom2, L"%s%s", start_path_data, currprefs.romfile);
		f = rom_fopen (tmprom2, L"rb", ZFD_NORMAL);
		if (f == NULL) {
			_stprintf (currprefs.romfile, L"%sroms/kick.rom", start_path_data);
			f = rom_fopen (currprefs.romfile, L"rb", ZFD_NORMAL);
			if (f == NULL) {
				_stprintf (currprefs.romfile, L"%skick.rom", start_path_data);
				f = rom_fopen (currprefs.romfile, L"rb", ZFD_NORMAL);
				if (f == NULL) {
					_stprintf (currprefs.romfile, L"%s../shared/rom/kick.rom", start_path_data);
					f = rom_fopen (currprefs.romfile, L"rb", ZFD_NORMAL);
					if (f == NULL) {
						_stprintf (currprefs.romfile, L"%s../System/rom/kick.rom", start_path_data);
						f = rom_fopen (currprefs.romfile, L"rb", ZFD_NORMAL);
						if (f == NULL)
							f = read_rom_name_guess (tmprom);
					}
				}
			}
		} else {
			_tcscpy (currprefs.romfile, tmprom2);
		}
	}
	addkeydir (currprefs.romfile);
	if (f == NULL) { /* still no luck */
#if defined(AMIGA)||defined(__POS__)
#define USE_UAE_ERSATZ "USE_UAE_ERSATZ"
		if( !getenv(USE_UAE_ERSATZ))
		{
			write_log (L"Using current ROM. (create ENV:%s to "
				"use uae's ROM replacement)\n",USE_UAE_ERSATZ);
			memcpy(kickmemory,(char*)0x1000000-kickmem_size,kickmem_size);
			kickstart_checksum (kickmemory, kickmem_size);
			goto chk_sum;
		}
#else
		goto err;
#endif
	}

	if (f != NULL) {
		int filesize, size, maxsize;
		int kspos = 524288;
		int extpos = 0;

		maxsize = 524288;
		zfile_fseek (f, 0, SEEK_END);
		filesize = zfile_ftell (f);
		zfile_fseek (f, 0, SEEK_SET);
		if (filesize == 1760 * 512) {
			filesize = 262144;
			maxsize = 262144;
		}
		if (filesize == 524288 + 8) {
			/* GVP 0xf0 kickstart */
			zfile_fseek (f, 8, SEEK_SET);
		}
		if (filesize >= 524288 * 2) {
			struct romdata *rd = getromdatabyzfile(f);
			zfile_fseek (f, kspos, SEEK_SET);
		}
		if (filesize >= 524288 * 4) {
			kspos = 524288 * 3;
			extpos = 0;
			zfile_fseek (f, kspos, SEEK_SET);
		}
		size = read_kickstart (f, kickmemory, maxsize, 1, 0);
		if (size == 0)
			goto err;
		kickmem_mask = size - 1;
		kickmem_size = size;
		if (filesize >= 524288 * 2 && !extendedkickmem_type) {
			extendedkickmem_size = 0x80000;
			if (currprefs.cs_cdtvcd || currprefs.cs_cdtvram) {
				extendedkickmem_type = EXTENDED_ROM_CDTV;
				extendedkickmem_size *= 2;
				extendedkickmemory = mapped_malloc (extendedkickmem_size, L"rom_f0");
			} else {
				extendedkickmem_type = EXTENDED_ROM_KS;
				extendedkickmemory = mapped_malloc (extendedkickmem_size, L"rom_e0");
			}
			extendedkickmem_bank.baseaddr = extendedkickmemory;
			zfile_fseek (f, extpos, SEEK_SET);
			read_kickstart (f, extendedkickmemory, extendedkickmem_size, 0, 1);
			extendedkickmem_mask = extendedkickmem_size - 1;
		}
		if (filesize > 524288 * 2) {
			extendedkickmem2_size = 524288 * 2;
			extendedkickmemory2 = mapped_malloc (extendedkickmem2_size, L"rom_a8");
			extendedkickmem2_bank.baseaddr = extendedkickmemory2;
			zfile_fseek (f, extpos + 524288, SEEK_SET);
			read_kickstart (f, extendedkickmemory2, 524288, 0, 1);
			zfile_fseek (f, extpos + 524288 * 2, SEEK_SET);
			read_kickstart (f, extendedkickmemory2 + 524288, 524288, 0, 1);
			extendedkickmem2_mask = extendedkickmem2_size - 1;
		}
	}

#if defined(AMIGA)
chk_sum:
#endif

	kickstart_version = (kickmemory[12] << 8) | kickmemory[13];
	if (kickstart_version == 0xffff)
		kickstart_version = 0;
	zfile_fclose (f);
	return 1;
err:
	_tcscpy (currprefs.romfile, tmprom);
	zfile_fclose (f);
	return 0;
}

#ifndef NATMEM_OFFSET

uae_u8 *mapped_malloc (size_t s, TCHAR *file)
{
	return xmalloc (uae_u8, s);
}

void mapped_free (uae_u8 *p)
{
	xfree (p);
}

#else

#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <sys/mman.h>

shmpiece *shm_start;

static void dumplist (void)
{
	shmpiece *x = shm_start;
	write_log (L"Start Dump:\n");
	while (x) {
		write_log (L"this=%p,Native %p,id %d,prev=%p,next=%p,size=0x%08x\n",
			x, x->native_address, x->id, x->prev, x->next, x->size);
		x = x->next;
	}
	write_log (L"End Dump:\n");
}

static shmpiece *find_shmpiece (uae_u8 *base)
{
	shmpiece *x = shm_start;

	while (x && x->native_address != base)
		x = x->next;
	if (!x) {
		write_log (L"NATMEM: Failure to find mapping at %08X, %p\n", base - NATMEM_OFFSET, base);
		nocanbang ();
		return 0;
	}
	return x;
}

static void delete_shmmaps (uae_u32 start, uae_u32 size)
{
	if (!canjit ())
		return;

	while (size) {
		uae_u8 *base = mem_banks[bankindex (start)]->baseaddr;
		if (base) {
			shmpiece *x;
			//base = ((uae_u8*)NATMEM_OFFSET)+start;

			x = find_shmpiece (base);
			if (!x)
				return;

			if (x->size > size) {
				if (isdirectjit ())
					write_log (L"NATMEM WARNING: size mismatch mapping at %08x (size %08x, delsize %08x)\n",start,x->size,size);
				size = x->size;
			}
#if 0
			dumplist ();
			nocanbang ();
			return;
		}
#endif
		shmdt (x->native_address);
		size -= x->size;
		start += x->size;
		if (x->next)
			x->next->prev = x->prev;	/* remove this one from the list */
		if (x->prev)
			x->prev->next = x->next;
		else
			shm_start = x->next;
		xfree (x);
	} else {
		size -= 0x10000;
		start += 0x10000;
	}
}
}

static void add_shmmaps (uae_u32 start, addrbank *what)
{
	shmpiece *x = shm_start;
	shmpiece *y;
	uae_u8 *base = what->baseaddr;

	if (!canjit ())
		return;
	if (!base)
		return;

	x = find_shmpiece (base);
	if (!x)
		return;
	y = xmalloc (shmpiece, 1);
	*y = *x;
	base = ((uae_u8 *) NATMEM_OFFSET) + start;
	y->native_address = (uae_u8*)shmat (y->id, base, 0);
	if (y->native_address == (void *) -1) {
		write_log (L"NATMEM: Failure to map existing at %08x (%p)\n", start, base);
		dumplist ();
		nocanbang ();
		return;
	}
	y->next = shm_start;
	y->prev = NULL;
	if (y->next)
		y->next->prev = y;
	shm_start = y;
}

uae_u8 *mapped_malloc (size_t s, const TCHAR *file)
{
	int id;
	void *answer;
	shmpiece *x;
	static int recurse;

	if (!canjit ()) {
		nocanbang ();
		return xcalloc (uae_u8, s + 4);
	}

	id = shmget (IPC_PRIVATE, s, 0x1ff, file);
	if (id == -1) {
		uae_u8 *p;
		nocanbang ();
		if (recurse)
			return NULL;
		recurse++;
		p = mapped_malloc (s, file);
		recurse--;
		return p;
	}
	answer = shmat (id, 0, 0);
	shmctl (id, IPC_RMID, NULL);
	if (answer != (void *) -1) {
		x = xmalloc (shmpiece, 1);
		x->native_address = (uae_u8*)answer;
		x->id = id;
		x->size = s;
		x->next = shm_start;
		x->prev = NULL;
		if (x->next)
			x->next->prev = x;
		shm_start = x;
		return (uae_u8*)answer;
	}
	if (recurse)
		return NULL;
	nocanbang ();
	recurse++;
	uae_u8 *r =  mapped_malloc (s, file);
	recurse--;
	return r;
}

#endif

static void init_mem_banks (void)
{
	int i;

	for (i = 0; i < MEMORY_BANKS; i++)
		put_mem_bank (i << 16, &dummy_bank, 0);
#ifdef NATMEM_OFFSET
	delete_shmmaps (0, 0xFFFF0000);
#endif
}

static void allocate_memory (void)
{
	/* emulate 0.5M+0.5M with 1M Agnus chip ram aliasing */
	if ((allocated_chipmem != currprefs.chipmem_size || allocated_bogomem != currprefs.bogomem_size) &&
		currprefs.chipmem_size == 0x80000 && currprefs.bogomem_size >= 0x80000 &&
		(currprefs.chipset_mask & CSMASK_ECS_AGNUS) && !(currprefs.chipset_mask & CSMASK_AGA) && !canjit ()) {
			int memsize1, memsize2;
			if (chipmemory)
				mapped_free (chipmemory);
			chipmemory = 0;
			if (bogomemory_allocated)
				mapped_free (bogomemory);
			bogomemory = 0;
			bogomemory_allocated = 0;
			memsize1 = allocated_chipmem = currprefs.chipmem_size;
			memsize2 = allocated_bogomem = currprefs.bogomem_size;
			chipmem_mask = allocated_chipmem - 1;
			chipmem_full_mask = allocated_chipmem * 2 - 1;
			chipmem_full_size = 0x80000 * 2;
			chipmemory = mapped_malloc (memsize1 + memsize2, L"chip");
			bogomemory = chipmemory + memsize1;
			bogomem_mask = allocated_bogomem - 1;
			if (chipmemory == 0) {
				write_log (L"Fatal error: out of memory for chipmem.\n");
				allocated_chipmem = 0;
			} else {
				need_hardreset = 1;
			}
	}

	if (allocated_chipmem != currprefs.chipmem_size) {
		int memsize;
		if (chipmemory)
			mapped_free (chipmemory);
		chipmemory = 0;
		if (currprefs.chipmem_size > 2 * 1024 * 1024)
			free_fastmemory ();

		memsize = allocated_chipmem = chipmem_full_size = currprefs.chipmem_size;
		chipmem_full_mask = chipmem_mask = allocated_chipmem - 1;
		if (memsize < 0x100000)
			memsize = 0x100000;
		if (memsize > 0x100000 && memsize < 0x200000)
			memsize = 0x200000;
		chipmemory = mapped_malloc (memsize, L"chip");
		if (chipmemory == 0) {
			write_log (L"Fatal error: out of memory for chipmem.\n");
			allocated_chipmem = 0;
		} else {
			need_hardreset = 1;
			if (memsize > allocated_chipmem)
				memset (chipmemory + allocated_chipmem, 0xff, memsize - allocated_chipmem);
		}
		currprefs.chipset_mask = changed_prefs.chipset_mask;
		chipmem_full_mask = allocated_chipmem - 1;
		if (currprefs.chipset_mask & CSMASK_ECS_AGNUS) {
			if (allocated_chipmem < 0x100000)
				chipmem_full_mask = 0x100000 - 1;
			if (allocated_chipmem > 0x100000 && allocated_chipmem < 0x200000)
				chipmem_full_mask = chipmem_mask = 0x200000 - 1;
		}
	}

	if (allocated_bogomem != currprefs.bogomem_size) {
		if (bogomemory_allocated)
			mapped_free (bogomemory);
		bogomemory = 0;
		bogomemory_allocated = 0;

		allocated_bogomem = currprefs.bogomem_size;
		if (allocated_bogomem == 0x180000)
			allocated_bogomem = 0x200000;
		bogomem_mask = allocated_bogomem - 1;

		if (allocated_bogomem) {
			bogomemory = mapped_malloc (allocated_bogomem, L"bogo");
			if (bogomemory == 0) {
				write_log (L"Out of memory for bogomem.\n");
				allocated_bogomem = 0;
			}
		}
		need_hardreset = 1;
	}
	if (allocated_a3000lmem != currprefs.mbresmem_low_size) {
		if (a3000lmemory)
			mapped_free (a3000lmemory);
		a3000lmemory = 0;

		allocated_a3000lmem = currprefs.mbresmem_low_size;
		a3000lmem_mask = allocated_a3000lmem - 1;
		a3000lmem_start = 0x08000000 - allocated_a3000lmem;
		if (allocated_a3000lmem) {
			a3000lmemory = mapped_malloc (allocated_a3000lmem, L"ramsey_low");
			if (a3000lmemory == 0) {
				write_log (L"Out of memory for a3000lowmem.\n");
				allocated_a3000lmem = 0;
			}
		}
		need_hardreset = 1;
	}
	if (allocated_a3000hmem != currprefs.mbresmem_high_size) {
		if (a3000hmemory)
			mapped_free (a3000hmemory);
		a3000hmemory = 0;

		allocated_a3000hmem = currprefs.mbresmem_high_size;
		a3000hmem_mask = allocated_a3000hmem - 1;
		a3000hmem_start = 0x08000000;
		if (allocated_a3000hmem) {
			a3000hmemory = mapped_malloc (allocated_a3000hmem, L"ramsey_high");
			if (a3000hmemory == 0) {
				write_log (L"Out of memory for a3000highmem.\n");
				allocated_a3000hmem = 0;
			}
		}
		need_hardreset = 1;
	}
	if (allocated_cardmem != currprefs.cs_cdtvcard * 1024) {
		if (cardmemory)
			mapped_free (cardmemory);
		cardmemory = 0;

		allocated_cardmem = currprefs.cs_cdtvcard * 1024;
		cardmem_mask = allocated_cardmem - 1;
		if (allocated_cardmem) {
			cardmemory = mapped_malloc (allocated_cardmem, L"rom_e0");
			if (cardmemory == 0) {
				write_log (L"Out of memory for cardmem.\n");
				allocated_cardmem = 0;
			}
		}
		cdtv_loadcardmem(cardmemory, allocated_cardmem);
	}
	if (allocated_custmem1 != currprefs.custom_memory_sizes[0]) {
		if (custmem1)
			mapped_free (custmem1);
		custmem1 = 0;
		allocated_custmem1 = currprefs.custom_memory_sizes[0];
		custmem1_mask = allocated_custmem1 - 1;
		if (allocated_custmem1) {
			custmem1 = mapped_malloc (allocated_custmem1, L"custmem1");
			if (!custmem1)
				allocated_custmem1 = 0;
		}
	}
	if (allocated_custmem2 != currprefs.custom_memory_sizes[1]) {
		if (custmem2)
			mapped_free (custmem2);
		custmem2 = 0;
		allocated_custmem2 = currprefs.custom_memory_sizes[1];
		custmem2_mask = allocated_custmem2 - 1;
		if (allocated_custmem2) {
			custmem2 = mapped_malloc (allocated_custmem2, L"custmem2");
			if (!custmem2)
				allocated_custmem2 = 0;
		}
	}

	if (savestate_state == STATE_RESTORE) {
		if (bootrom_filepos) {
			restore_ram (bootrom_filepos, rtarea);
		}
		restore_ram (chip_filepos, chipmemory);
		if (allocated_bogomem > 0)
			restore_ram (bogo_filepos, bogomemory);
		if (allocated_a3000lmem > 0)
			restore_ram (a3000lmem_filepos, a3000lmemory);
		if (allocated_a3000hmem > 0)
			restore_ram (a3000hmem_filepos, a3000hmemory);
	}
	chipmem_bank.baseaddr = chipmemory;
#ifdef AGA
	chipmem_bank_ce2.baseaddr = chipmemory;
#endif
	bogomem_bank.baseaddr = bogomemory;
	a3000lmem_bank.baseaddr = a3000lmemory;
	a3000hmem_bank.baseaddr = a3000hmemory;
	cardmem_bank.baseaddr = cardmemory;
	bootrom_filepos = 0;
	chip_filepos = 0;
	bogo_filepos = 0;
	a3000lmem_filepos = 0;
	a3000hmem_filepos = 0;
}

static void fill_ce_banks (void)
{
	int i;

	memset (ce_banktype, CE_MEMBANK_FAST, sizeof ce_banktype);
	// data cachable regions
	memset (ce_cachable, 0, sizeof ce_cachable);
	memset (ce_cachable + (0x00200000 >> 16), 1, currprefs.fastmem_size >> 16);
	memset (ce_cachable + (0x10000000 >> 16), 1, currprefs.z3fastmem_size >> 16);

	if (&get_mem_bank (0) == &chipmem_bank) {
		for (i = 0; i < (0x200000 >> 16); i++)
			ce_banktype[i] = CE_MEMBANK_CHIP;
	}
	if (!currprefs.cs_slowmemisfast) {
		for (i = (0xc00000 >> 16); i < (0xe00000 >> 16); i++)
			ce_banktype[i] = CE_MEMBANK_CHIP;
	}
	for (i = (0xd00000 >> 16); i < (0xe00000 >> 16); i++)
		ce_banktype[i] = CE_MEMBANK_CHIP;
	for (i = (0xa00000 >> 16); i < (0xc00000 >> 16); i++) {
		addrbank *b;
		ce_banktype[i] = CE_MEMBANK_CIA;
		b = &get_mem_bank (i << 16);
		if (b != &cia_bank) {
			ce_banktype[i] = CE_MEMBANK_FAST;
			ce_cachable[i] = 1;
		}
	}
	// CD32 ROM is 16-bit
	if (currprefs.cs_cd32cd) {
		for (i = (0xe00000 >> 16); i < (0xe80000 >> 16); i++)
			ce_banktype[i] = CE_MEMBANK_FAST16BIT;
		for (i = (0xf80000 >> 16); i <= (0xff0000 >> 16); i++)
			ce_banktype[i] = CE_MEMBANK_FAST16BIT;
	}
	if (currprefs.address_space_24) {
		for (i = 1; i < 256; i++)
			memcpy (&ce_banktype[i * 256], &ce_banktype[0], 256);
	}
}

void map_overlay (int chip)
{
	int size;
	addrbank *cb;

	size = allocated_chipmem >= 0x180000 ? (allocated_chipmem >> 16) : 32;
	cb = &chipmem_bank;
#ifdef AGA
#if 0
	if (currprefs.cpu_cycle_exact && currprefs.cpu_model >= 68020)
		cb = &chipmem_bank_ce2;
#endif
#endif
	if (chip) {
		map_banks (&dummy_bank, 0, 32, 0);
		if (!isdirectjit ()) {
			map_banks (cb, 0, size, allocated_chipmem);
			if ((currprefs.chipset_mask & CSMASK_ECS_AGNUS) && allocated_bogomem == 0) {
				int start = allocated_chipmem >> 16;
				if (allocated_chipmem < 0x100000) {
					int dummy = (0x100000 - allocated_chipmem) >> 16;
					map_banks (&chipmem_dummy_bank, start, dummy, 0);
					map_banks (&chipmem_dummy_bank, start + 16, dummy, 0);
				} else if (allocated_chipmem < 0x200000 && allocated_chipmem > 0x100000) {
					int dummy = (0x200000 - allocated_chipmem) >> 16;
					map_banks (&chipmem_dummy_bank, start, dummy, 0);
				}
			}
		} else {
			map_banks (cb, 0, allocated_chipmem >> 16, 0);
		}
	} else {
		addrbank *rb = NULL;
		if (size < 32)
			size = 32;
		cb = &get_mem_bank (0xf00000);
		if (!rb && cb && (cb->flags & ABFLAG_ROM) && get_word (0xf00000) == 0x1114)
			rb = cb;
		cb = &get_mem_bank (0xe00000);
		if (!rb && cb && (cb->flags & ABFLAG_ROM) && get_word (0xe00000) == 0x1114)
			rb = cb;
		if (!rb)
			rb = &kickmem_bank;
		map_banks (rb, 0, size, 0x80000);
	}
	fill_ce_banks ();
	if (savestate_state != STATE_RESTORE && savestate_state != STATE_REWIND && valid_address (regs.pc, 4))
		m68k_setpc (m68k_getpc ());
}

void memory_reset (void)
{
	int bnk, bnk_end;
	int gayle;

	be_cnt = 0;
	currprefs.chipmem_size = changed_prefs.chipmem_size;
	currprefs.bogomem_size = changed_prefs.bogomem_size;
	currprefs.mbresmem_low_size = changed_prefs.mbresmem_low_size;
	currprefs.mbresmem_high_size = changed_prefs.mbresmem_high_size;
	currprefs.cs_ksmirror_e0 = changed_prefs.cs_ksmirror_e0;
	currprefs.cs_ksmirror_a8 = changed_prefs.cs_ksmirror_a8;
	currprefs.cs_ciaoverlay = changed_prefs.cs_ciaoverlay;
	currprefs.cs_cdtvram = changed_prefs.cs_cdtvram;
	currprefs.cs_cdtvcard = changed_prefs.cs_cdtvcard;
	currprefs.cs_a1000ram = changed_prefs.cs_a1000ram;
	currprefs.cs_ide = changed_prefs.cs_ide;
	currprefs.cs_fatgaryrev = changed_prefs.cs_fatgaryrev;
	currprefs.cs_ramseyrev = changed_prefs.cs_ramseyrev;

	gayle = (currprefs.chipset_mask & CSMASK_AGA) || currprefs.cs_pcmcia || currprefs.cs_ide > 0;

	need_hardreset = 0;
	/* Use changed_prefs, as m68k_reset is called later.  */
	if (last_address_space_24 != changed_prefs.address_space_24)
		need_hardreset = 1;

	last_address_space_24 = changed_prefs.address_space_24;

	init_mem_banks ();
	allocate_memory ();
	chipmem_setindirect ();

	if (_tcscmp (currprefs.romfile, changed_prefs.romfile) != 0
		|| _tcscmp (currprefs.romextfile, changed_prefs.romextfile) != 0)
	{
		write_log (L"ROM loader..\n");
		kickstart_rom = 1;
		ersatzkickfile = 0;
		a1000_handle_kickstart (0);
		xfree (a1000_bootrom);
		a1000_bootrom = 0;
		a1000_kickstart_mode = 0;

		memcpy (currprefs.romfile, changed_prefs.romfile, sizeof currprefs.romfile);
		memcpy (currprefs.romextfile, changed_prefs.romextfile, sizeof currprefs.romextfile);
		need_hardreset = 1;
		mapped_free (extendedkickmemory);
		extendedkickmemory = 0;
		extendedkickmem_size = 0;
		extendedkickmemory2 = 0;
		extendedkickmem2_size = 0;
		extendedkickmem_type = 0;
		load_extendedkickstart ();
		kickmem_mask = 524288 - 1;
		if (!load_kickstart ()) {
			if (_tcslen (currprefs.romfile) > 0) {
				write_log (L"Failed to open '%s'\n", currprefs.romfile);
				notify_user (NUMSG_NOROM);
			}
#ifdef AUTOCONFIG
			init_ersatz_rom (kickmemory);
			kickmem_mask = 262144 - 1;
			kickmem_size = 262144;
			ersatzkickfile = 1;
#else
			uae_restart (-1, NULL);
#endif
		} else {
			struct romdata *rd = getromdatabydata (kickmemory, kickmem_size);
			if (rd) {
				write_log (L"Known ROM '%s' loaded\n", rd->name);
				if ((rd->cpu & 3) == 3 && changed_prefs.cpu_model != 68030) {
					notify_user (NUMSG_KS68030);
					uae_restart (-1, NULL);
				} else if ((rd->cpu & 3) == 1 && changed_prefs.cpu_model < 68020) {
					notify_user (NUMSG_KS68EC020);
					uae_restart (-1, NULL);
				} else if ((rd->cpu & 3) == 2 && (changed_prefs.cpu_model < 68020 || changed_prefs.address_space_24)) {
					notify_user (NUMSG_KS68020);
					uae_restart (-1, NULL);
				}
				if (rd->cloanto)
					cloanto_rom = 1;
				kickstart_rom = 0;
				if ((rd->type & ROMTYPE_SPECIALKICK | ROMTYPE_KICK) == ROMTYPE_KICK)
					kickstart_rom = 1;
				if ((rd->cpu & 4) && currprefs.cs_compatible) {
					/* A4000 ROM = need ramsey, gary and ide */
					if (currprefs.cs_ramseyrev < 0)
						changed_prefs.cs_ramseyrev = currprefs.cs_ramseyrev = 0x0f;
					changed_prefs.cs_fatgaryrev = currprefs.cs_fatgaryrev = 0;
					if (currprefs.cs_ide != IDE_A4000)
						changed_prefs.cs_ide = currprefs.cs_ide = -1;
				}
			} else {
				write_log (L"Unknown ROM '%s' loaded\n", currprefs.romfile);
			}
		}
		patch_kick ();
		write_log (L"ROM loader end\n");
	}

	if (cloanto_rom && currprefs.maprom < 0x01000000)
		currprefs.maprom = changed_prefs.maprom = 0;

	gayle = currprefs.cs_ksmirror_a8 || currprefs.cs_pcmcia || currprefs.cs_ide > 0;

	map_banks (&custom_bank, 0xC0, 0xE0 - 0xC0, 0);
	map_banks (&cia_bank, 0xA0, 32, 0);
	if (!currprefs.cs_a1000ram)
		/* D80000 - DDFFFF not mapped (A1000 = custom chips) */
		map_banks (&dummy_bank, 0xD8, 6, 0);

	/* map "nothing" to 0x200000 - 0x9FFFFF (0xBEFFFF if Gayle) */
	bnk = allocated_chipmem >> 16;
	if (bnk < 0x20 + (currprefs.fastmem_size >> 16))
		bnk = 0x20 + (currprefs.fastmem_size >> 16);
	bnk_end = gayle ? 0xBF : 0xA0;
	map_banks (&dummy_bank, bnk, bnk_end - bnk, 0);
	if (gayle)
		map_banks (&dummy_bank, 0xc0, 0xd8 - 0xc0, 0);

	if (bogomemory != 0) {
		int t = currprefs.bogomem_size >> 16;
		if (t > 0x1C)
			t = 0x1C;
		if (t > 0x10 && ((currprefs.chipset_mask & CSMASK_AGA) || currprefs.cpu_model >= 68020))
			t = 0x10;
		map_banks (&bogomem_bank, 0xC0, t, 0);
	}
	if (currprefs.cs_ide || currprefs.cs_pcmcia) {
		if (currprefs.cs_ide == IDE_A600A1200 || currprefs.cs_pcmcia) {
			map_banks (&gayle_bank, 0xD8, 6, 0);
			map_banks (&gayle2_bank, 0xDD, 2, 0);
		}
		gayle_map_pcmcia ();
		if (currprefs.cs_ide == IDE_A4000 || currprefs.cs_mbdmac == 2)
			map_banks (&gayle_bank, 0xDD, 1, 0);
		if (currprefs.cs_ide < 0 && !currprefs.cs_pcmcia)
			map_banks (&gayle_bank, 0xD8, 6, 0);
		if (currprefs.cs_ide < 0)
			map_banks (&gayle_bank, 0xDD, 1, 0);
	}
	if (currprefs.cs_rtc || currprefs.cs_cdtvram)
		map_banks (&clock_bank, 0xDC, 1, 0);
	else if (currprefs.cs_ksmirror_a8 || currprefs.cs_ide > 0 || currprefs.cs_pcmcia)
		map_banks (&clock_bank, 0xDC, 1, 0); /* none clock */
	if (currprefs.cs_fatgaryrev >= 0 || currprefs.cs_ramseyrev >= 0)
		map_banks (&mbres_bank, 0xDE, 1, 0);
	if (currprefs.cs_cd32c2p || currprefs.cs_cd32cd || currprefs.cs_cd32nvram)
		map_banks (&akiko_bank, AKIKO_BASE >> 16, 1, 0);
	if (currprefs.cs_cdtvcd)
		cdtv_check_banks ();
	if (currprefs.cs_mbdmac == 1)
		a3000scsi_reset ();

	if (a3000lmemory != 0)
		map_banks (&a3000lmem_bank, a3000lmem_start >> 16, allocated_a3000lmem >> 16, 0);
	if (a3000hmemory != 0)
		map_banks (&a3000hmem_bank, a3000hmem_start >> 16, allocated_a3000hmem >> 16, 0);
	if (cardmemory != 0)
		map_banks (&cardmem_bank, cardmem_start >> 16, allocated_cardmem >> 16, 0);

	map_banks (&kickmem_bank, 0xF8, 8, 0);
	if (currprefs.maprom)
		map_banks (&kickram_bank, currprefs.maprom >> 16, 8, 0);
	/* map beta Kickstarts at 0x200000/0xC00000/0xF00000 */
	if (kickmemory[0] == 0x11 && kickmemory[2] == 0x4e && kickmemory[3] == 0xf9 && kickmemory[4] == 0x00) {
		uae_u32 addr = kickmemory[5];
		if (addr == 0x20 && currprefs.chipmem_size <= 0x200000 && currprefs.fastmem_size == 0)
			map_banks (&kickmem_bank, addr, 8, 0);
		if (addr == 0xC0 && currprefs.bogomem_size == 0)
			map_banks (&kickmem_bank, addr, 8, 0);
		if (addr == 0xF0)
			map_banks (&kickmem_bank, addr, 8, 0);
	}

	if (a1000_bootrom)
		a1000_handle_kickstart (1);

#ifdef AUTOCONFIG
	map_banks (&expamem_bank, 0xE8, 1, 0);
#endif

	if (a3000_f0)
		map_banks (&extendedkickmem_bank, 0xf0, 1, 0);

	/* Map the chipmem into all of the lower 8MB */
	map_overlay (1);

	switch (extendedkickmem_type) {
	case EXTENDED_ROM_KS:
		map_banks (&extendedkickmem_bank, 0xE0, 8, 0);
		break;
#ifdef CDTV
	case EXTENDED_ROM_CDTV:
		map_banks (&extendedkickmem_bank, 0xF0, extendedkickmem_size == 2 * 524288 ? 16 : 8, 0);
		break;
#endif
#ifdef CD32
	case EXTENDED_ROM_CD32:
		map_banks (&extendedkickmem_bank, 0xE0, 8, 0);
		break;
#endif
	}

#ifdef AUTOCONFIG
	if (need_uae_boot_rom ())
		map_banks (&rtarea_bank, rtarea_base >> 16, 1, 0);
#endif

	if ((cloanto_rom || currprefs.cs_ksmirror_e0) && (currprefs.maprom != 0xe00000) && !extendedkickmem_type)
		map_banks (&kickmem_bank, 0xE0, 8, 0);
	if (currprefs.cs_ksmirror_a8) {
		if (extendedkickmem2_size) {
			map_banks (&extendedkickmem2_bank, 0xa8, 16, 0);
		} else {
			struct romdata *rd = getromdatabypath (currprefs.cartfile);
			if (!rd || rd->id != 63) {
				if (extendedkickmem_type == EXTENDED_ROM_CD32 || extendedkickmem_type == EXTENDED_ROM_KS)
					map_banks (&extendedkickmem_bank, 0xb0, 8, 0);
				else
					map_banks (&kickmem_bank, 0xb0, 8, 0);
				map_banks (&kickmem_bank, 0xa8, 8, 0);
			}
		}
	}

	if (currprefs.custom_memory_sizes[0]) {
		map_banks (&custmem1_bank,
			currprefs.custom_memory_addrs[0] >> 16,
			currprefs.custom_memory_sizes[0] >> 16, 0);
	}
	if (currprefs.custom_memory_sizes[1]) {
		map_banks (&custmem2_bank,
			currprefs.custom_memory_addrs[1] >> 16,
			currprefs.custom_memory_sizes[1] >> 16, 0);
	}

#ifdef ARCADIA
	if (is_arcadia_rom (currprefs.romextfile) == ARCADIA_BIOS) {
		if (_tcscmp (currprefs.romextfile, changed_prefs.romextfile) != 0)
			memcpy (currprefs.romextfile, changed_prefs.romextfile, sizeof currprefs.romextfile);
		if (_tcscmp (currprefs.cartfile, changed_prefs.cartfile) != 0)
			memcpy (currprefs.cartfile, changed_prefs.cartfile, sizeof currprefs.cartfile);
		arcadia_unmap ();
		is_arcadia_rom (currprefs.romextfile);
		is_arcadia_rom (currprefs.cartfile);
		arcadia_map_banks ();
	}
#endif

#ifdef ACTION_REPLAY
#ifdef ARCADIA
	if (!arcadia_bios) {
#endif
		action_replay_memory_reset ();
#ifdef ARCADIA
	}
#endif
#endif
	write_log (L"memory init end\n");
}

void memory_init (void)
{
	allocated_chipmem = 0;
	allocated_bogomem = 0;
	kickmemory = 0;
	extendedkickmemory = 0;
	extendedkickmem_size = 0;
	extendedkickmemory2 = 0;
	extendedkickmem2_size = 0;
	extendedkickmem_type = 0;
	chipmemory = 0;
	allocated_a3000lmem = allocated_a3000hmem = 0;
	a3000lmemory = a3000hmemory = 0;
	bogomemory = 0;
	cardmemory = 0;
	custmem1 = 0;
	custmem2 = 0;

	init_mem_banks ();

	kickmemory = mapped_malloc (0x80000, L"kick");
	memset (kickmemory, 0, 0x80000);
	kickmem_bank.baseaddr = kickmemory;
	_tcscpy (currprefs.romfile, L"<none>");
	currprefs.romextfile[0] = 0;

#ifdef ACTION_REPLAY
	action_replay_load();
	action_replay_init(1);
#ifdef ACTION_REPLAY_HRTMON
	hrtmon_load();
#endif
#endif
}

void memory_cleanup (void)
{
	if (a3000lmemory)
		mapped_free (a3000lmemory);
	if (a3000hmemory)
		mapped_free (a3000hmemory);
	if (bogomemory)
		mapped_free (bogomemory);
	if (kickmemory)
		mapped_free (kickmemory);
	if (a1000_bootrom)
		xfree (a1000_bootrom);
	if (chipmemory)
		mapped_free (chipmemory);
	if (cardmemory) {
		cdtv_savecardmem (cardmemory, allocated_cardmem);
		mapped_free (cardmemory);
	}
	if (custmem1)
		mapped_free (custmem1);
	if (custmem2)
		mapped_free (custmem2);

	bogomemory = 0;
	kickmemory = 0;
	a3000lmemory = a3000hmemory = 0;
	a1000_bootrom = 0;
	a1000_kickstart_mode = 0;
	chipmemory = 0;
	cardmemory = 0;
	custmem1 = 0;
	custmem2 = 0;

#ifdef ACTION_REPLAY
	action_replay_cleanup();
#endif
#ifdef ARCADIA
	arcadia_unmap ();
#endif
}

void memory_hardreset (void)
{
	if (savestate_state == STATE_RESTORE)
		return;
	if (chipmemory)
		memset (chipmemory, 0, allocated_chipmem);
	if (bogomemory)
		memset (bogomemory, 0, allocated_bogomem);
	if (a3000lmemory)
		memset (a3000lmemory, 0, allocated_a3000lmem);
	if (a3000hmemory)
		memset (a3000hmemory, 0, allocated_a3000hmem);
	expansion_clear ();
}

void map_banks (addrbank *bank, int start, int size, int realsize)
{
	int bnr, old;
	unsigned long int hioffs = 0, endhioffs = 0x100;
	addrbank *orgbank = bank;
	uae_u32 realstart = start;

	//write_log (L"MAP_BANK %04X0000 %d %s\n", start, size, bank->name);

	old = debug_bankchange (-1);
	flush_icache (0, 3); /* Sure don't want to keep any old mappings around! */
#ifdef NATMEM_OFFSET
	delete_shmmaps (start << 16, size << 16);
#endif

	if (!realsize)
		realsize = size << 16;

	if ((size << 16) < realsize) {
		write_log (L"Broken mapping, size=%x, realsize=%x\nStart is %x\n",
			size, realsize, start);
	}

#ifndef ADDRESS_SPACE_24BIT
	if (start >= 0x100) {
		int real_left = 0;
		for (bnr = start; bnr < start + size; bnr++) {
			if (!real_left) {
				realstart = bnr;
				real_left = realsize >> 16;
#ifdef NATMEM_OFFSET
				add_shmmaps (realstart << 16, bank);
#endif
			}
			put_mem_bank (bnr << 16, bank, realstart << 16);
			real_left--;
		}
		debug_bankchange (old);
		return;
	}
#endif
	if (last_address_space_24)
		endhioffs = 0x10000;
#ifdef ADDRESS_SPACE_24BIT
	endhioffs = 0x100;
#endif
	for (hioffs = 0; hioffs < endhioffs; hioffs += 0x100) {
		int real_left = 0;
		for (bnr = start; bnr < start + size; bnr++) {
			if (!real_left) {
				realstart = bnr + hioffs;
				real_left = realsize >> 16;
#ifdef NATMEM_OFFSET
				add_shmmaps (realstart << 16, bank);
#endif
			}
			put_mem_bank ((bnr + hioffs) << 16, bank, realstart << 16);
			real_left--;
		}
	}
	debug_bankchange (old);
	fill_ce_banks ();
}

#ifdef SAVESTATE

/* memory save/restore code */

uae_u8 *save_bootrom (int *len)
{
	if (!uae_boot_rom)
		return 0;
	*len = uae_boot_rom_size;
	return rtarea;
}

uae_u8 *save_cram (int *len)
{
	*len = allocated_chipmem;
	return chipmemory;
}

uae_u8 *save_bram (int *len)
{
	*len = allocated_bogomem;
	return bogomemory;
}

uae_u8 *save_a3000lram (int *len)
{
	*len = allocated_a3000lmem;
	return a3000lmemory;
}

uae_u8 *save_a3000hram (int *len)
{
	*len = allocated_a3000hmem;
	return a3000hmemory;
}

void restore_bootrom (int len, size_t filepos)
{
	bootrom_filepos = filepos;
}

void restore_cram (int len, size_t filepos)
{
	chip_filepos = filepos;
	changed_prefs.chipmem_size = len;
}

void restore_bram (int len, size_t filepos)
{
	bogo_filepos = filepos;
	changed_prefs.bogomem_size = len;
}

void restore_a3000lram (int len, size_t filepos)
{
	a3000lmem_filepos = filepos;
	changed_prefs.mbresmem_low_size = len;
}

void restore_a3000hram (int len, size_t filepos)
{
	a3000hmem_filepos = filepos;
	changed_prefs.mbresmem_high_size = len;
}

uae_u8 *restore_rom (uae_u8 *src)
{
	uae_u32 crc32, mem_start, mem_size, mem_type, version;
	TCHAR *s, *romn;
	int i, crcdet;
	struct romlist *rl = romlist_getit ();

	mem_start = restore_u32 ();
	mem_size = restore_u32 ();
	mem_type = restore_u32 ();
	version = restore_u32 ();
	crc32 = restore_u32 ();
	romn = restore_string ();
	crcdet = 0;
	for (i = 0; i < romlist_count (); i++) {
		if (rl[i].rd->crc32 == crc32 && crc32) {
			if (zfile_exists (rl[i].path)) {
				switch (mem_type)
				{
				case 0:
					_tcsncpy (changed_prefs.romfile, rl[i].path, 255);
					break;
				case 1:
					_tcsncpy (changed_prefs.romextfile, rl[i].path, 255);
					break;
				}
				write_log (L"ROM '%s' = '%s'\n", romn, rl[i].path);
				crcdet = 1;
			} else {
				write_log (L"ROM '%s' = '%s' invalid rom scanner path!", romn, rl[i].path);
			}
			break;
		}
	}
	s = restore_string ();
	if (!crcdet) {
		if (zfile_exists (s)) {
			switch (mem_type)
			{
			case 0:
				_tcsncpy (changed_prefs.romfile, s, 255);
				break;
			case 1:
				_tcsncpy (changed_prefs.romextfile, s, 255);
				break;
			}
			write_log (L"ROM detected (path) as '%s'\n", s);
			crcdet = 1;
		}
	}
	xfree (s);
	if (!crcdet)
		write_log (L"WARNING: ROM '%s' not found!\n", romn);
	xfree (romn);
	return src;
}

uae_u8 *save_rom (int first, int *len, uae_u8 *dstptr)
{
	static int count;
	uae_u8 *dst, *dstbak;
	uae_u8 *mem_real_start;
	uae_u32 version;
	TCHAR *path;
	int mem_start, mem_size, mem_type, saverom;
	int i;
	TCHAR tmpname[1000];

	version = 0;
	saverom = 0;
	if (first)
		count = 0;
	for (;;) {
		mem_type = count;
		mem_size = 0;
		switch (count) {
		case 0: /* Kickstart ROM */
			mem_start = 0xf80000;
			mem_real_start = kickmemory;
			mem_size = kickmem_size;
			path = currprefs.romfile;
			/* 256KB or 512KB ROM? */
			for (i = 0; i < mem_size / 2 - 4; i++) {
				if (longget (i + mem_start) != longget (i + mem_start + mem_size / 2))
					break;
			}
			if (i == mem_size / 2 - 4) {
				mem_size /= 2;
				mem_start += 262144;
			}
			version = longget (mem_start + 12); /* version+revision */
			_stprintf (tmpname, L"Kickstart %d.%d", wordget (mem_start + 12), wordget (mem_start + 14));
			break;
		case 1: /* Extended ROM */
			if (!extendedkickmem_type)
				break;
			mem_start = extendedkickmem_start;
			mem_real_start = extendedkickmemory;
			mem_size = extendedkickmem_size;
			path = currprefs.romextfile;
			_stprintf (tmpname, L"Extended");
			break;
		default:
			return 0;
		}
		count++;
		if (mem_size)
			break;
	}
	if (dstptr)
		dstbak = dst = dstptr;
	else
		dstbak = dst = xmalloc (uae_u8, 4 + 4 + 4 + 4 + 4 + 256 + 256 + mem_size);
	save_u32 (mem_start);
	save_u32 (mem_size);
	save_u32 (mem_type);
	save_u32 (version);
	save_u32 (get_crc32 (mem_real_start, mem_size));
	save_string (tmpname);
	save_string (path);
	if (saverom) {
		for (i = 0; i < mem_size; i++)
			*dst++ = byteget (mem_start + i);
	}
	*len = dst - dstbak;
	return dstbak;
}

#endif /* SAVESTATE */

/* memory helpers */

void memcpyha_safe (uaecptr dst, const uae_u8 *src, int size)
{
	if (!addr_valid (L"memcpyha", dst, size))
		return;
	while (size--)
		put_byte (dst++, *src++);
}
void memcpyha (uaecptr dst, const uae_u8 *src, int size)
{
	while (size--)
		put_byte (dst++, *src++);
}
void memcpyah_safe (uae_u8 *dst, uaecptr src, int size)
{
	if (!addr_valid (L"memcpyah", src, size))
		return;
	while (size--)
		*dst++ = get_byte (src++);
}
void memcpyah (uae_u8 *dst, uaecptr src, int size)
{
	while (size--)
		*dst++ = get_byte (src++);
}
uae_char *strcpyah_safe (uae_char *dst, uaecptr src, int maxsize)
{
	uae_char *res = dst;
	uae_u8 b;
	do {
		if (!addr_valid (L"_tcscpyah", src, 1))
			return res;
		b = get_byte (src++);
		*dst++ = b;
		maxsize--;
		if (maxsize <= 1) {
			*dst++= 0;
			break;
		}
	} while (b);
	return res;
}
uaecptr strcpyha_safe (uaecptr dst, const uae_char *src)
{
	uaecptr res = dst;
	uae_u8 b;
	do {
		if (!addr_valid (L"_tcscpyha", dst, 1))
			return res;
		b = *src++;
		put_byte (dst++, b);
	} while (b);
	return res;
}
