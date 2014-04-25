/*
* UAE - The Un*x Amiga Emulator
*
* Memory management
*
* (c) 1995 Bernd Schmidt
*/

#define DEBUG_STUPID 0

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
#include "gfxboard.h"

bool canbang;
int candirect = -1;
static bool rom_write_enabled;
#ifdef JIT
/* Set by each memory handler that does not simply access real memory. */
int special_mem;
#endif
static int mem_hardreset;

static bool isdirectjit (void)
{
	return currprefs.cachesize && !currprefs.comptrustbyte;
}

static bool canjit (void)
{
	if (currprefs.cpu_model < 68020 || currprefs.address_space_24)
		return false;
	return true;
}
static bool needmman (void)
{
	if (!currprefs.jit_direct_compatible_memory)
		return false;
#ifdef _WIN32
	return true;
#endif
	if (canjit ())
		return true;
	return false;
}

static void nocanbang (void)
{
	canbang = 0;
}

uae_u8 ce_banktype[65536];
uae_u8 ce_cachable[65536];

static size_t bootrom_filepos, chip_filepos, bogo_filepos, rom_filepos, a3000lmem_filepos, a3000hmem_filepos;

/* Set if we notice during initialization that settings changed,
and we must clear all memory to prevent bogus contents from confusing
the Kickstart.  */
static bool need_hardreset;
static bool bogomem_aliasing;

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

int addr_valid (const TCHAR *txt, uaecptr addr, uae_u32 len)
{
	addrbank *ab = &get_mem_bank(addr);
	if (ab == 0 || !(ab->flags & (ABFLAG_RAM | ABFLAG_ROM)) || addr < 0x100 || len < 0 || len > 16777215 || !valid_address (addr, len)) {
		write_log (_T("corrupt %s pointer %x (%d) detected!\n"), txt, addr, len);
		return 0;
	}
	return 1;
}

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
	if (illegal_count >= MAX_ILG && MAX_ILG > 0)
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
		write_log (_T("WARNING: Illegal opcode %cget at %08x PC=%x\n"),
			size == 2 ? 'w' : 'l', addr, M68K_GETPC);
	} else if (rw) {
		write_log (_T("Illegal %cput at %08x=%08x PC=%x\n"),
			size == 1 ? 'b' : size == 2 ? 'w' : 'l', addr, val, M68K_GETPC);
	} else {
		write_log (_T("Illegal %cget at %08x PC=%x\n"),
			size == 1 ? 'b' : size == 2 ? 'w' : 'l', addr, M68K_GETPC);
	}
}

// 250ms delay
static void gary_wait(uaecptr addr, int size)
{
	static int cnt = 50;

#if 0
	int lines = 313 * 12;
	while (lines-- > 0)
		x_do_cycles(228 * CYCLE_UNIT);
#endif

	if (cnt > 0) {
		write_log (_T("Gary timeout: %08x %d\n"), addr, size);
		cnt--;
	}
}

static bool gary_nonrange(uaecptr addr)
{
	if (currprefs.cs_fatgaryrev < 0)
		return false;
	if (addr < 0xb80000)
		return false;
	if (addr >= 0xd00000 && addr < 0xdc0000)
		return true;
	if (addr >= 0xdd0000 && addr < 0xde0000)
		return true;
	if (addr >= 0xdf8000 && addr < 0xe00000)
		return false;
	if (addr >= 0xe80000 && addr < 0xf80000)
		return false;
	return true;
}

void dummy_put (uaecptr addr, int size, uae_u32 val)
{
	if (gary_nonrange(addr) || (size > 1 && gary_nonrange(addr + size - 1))) {
		if (gary_timeout)
			gary_wait (addr, size);
		if (gary_toenb && currprefs.mmu_model)
			exception2 (addr, true, size, regs.s ? 4 : 0);
	}
}

uae_u32 dummy_get (uaecptr addr, int size, bool inst)
{
	uae_u32 v = NONEXISTINGDATA;

	if (gary_nonrange(addr) || (size > 1 && gary_nonrange(addr + size - 1))) {
		if (gary_timeout)
			gary_wait (addr, size);
		if (gary_toenb && currprefs.mmu_model)
			exception2 (addr, false, size, (regs.s ? 4 : 0) | (inst ? 0 : 1));
		return v;
	}

	if (currprefs.cpu_model >= 68040)
		return v;
	if (!currprefs.cpu_compatible)
		return v;
	if (currprefs.address_space_24)
		addr &= 0x00ffffff;
	if (addr >= 0x10000000)
		return v;
	if ((currprefs.cpu_model <= 68010) || (currprefs.cpu_model == 68020 && (currprefs.chipset_mask & CSMASK_AGA) && currprefs.address_space_24)) {
		if (size == 4) {
			v = regs.db & 0xffff;
			if (addr & 1)
				v = (v << 8) | (v >> 8);
			v = (v << 16) | v;
		} else if (size == 2) {
			v = regs.db & 0xffff;
			if (addr & 1)
				v = (v << 8) | (v >> 8);
		} else {
			v = regs.db;
			v = (addr & 1) ? (v & 0xff) : ((v >> 8) & 0xff);
		}
	}
#if 0
	if (addr >= 0x10000000)
		write_log (_T("%08X %d = %08x\n"), addr, size, v);
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
	return dummy_get (addr, 4, false);
}
uae_u32 REGPARAM2 dummy_lgeti (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	if (currprefs.illegal_mem)
		dummylog (0, addr, 4, 0, 1);
	return dummy_get (addr, 4, true);
}

static uae_u32 REGPARAM2 dummy_wget (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
#if 0
	if (addr == 0xb0b000) {
		extern uae_u16 isideint(void);
		return isideint();
	}
#endif
	if (currprefs.illegal_mem)
		dummylog (0, addr, 2, 0, 0);
	return dummy_get (addr, 2, false);
}
uae_u32 REGPARAM2 dummy_wgeti (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	if (currprefs.illegal_mem)
		dummylog (0, addr, 2, 0, 1);
	return dummy_get (addr, 2, true);
}

static uae_u32 REGPARAM2 dummy_bget (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	if (currprefs.illegal_mem)
		dummylog (0, addr, 1, 0, 0);
	return dummy_get (addr, 1, false);
}

static void REGPARAM2 dummy_lput (uaecptr addr, uae_u32 l)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (currprefs.illegal_mem)
		dummylog (1, addr, 4, l, 0);
	dummy_put (addr, 4, l);
}
static void REGPARAM2 dummy_wput (uaecptr addr, uae_u32 w)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (currprefs.illegal_mem)
		dummylog (1, addr, 2, w, 0);
	dummy_put (addr, 2, w);
}
static void REGPARAM2 dummy_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (currprefs.illegal_mem)
		dummylog (1, addr, 1, b, 0);
	dummy_put (addr, 1, b);
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

static uae_u32 chipmem_full_mask;
static uae_u32 chipmem_full_size;

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
	addr &= chipmem_bank.mask;
	m = (uae_u32 *)(chipmem_bank.baseaddr + addr);
	ce2_timeout ();
	return do_get_mem_long (m);
}

static uae_u32 REGPARAM2 chipmem_wget_ce2 (uaecptr addr)
{
	uae_u16 *m, v;

#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= chipmem_bank.mask;
	m = (uae_u16 *)(chipmem_bank.baseaddr + addr);
	ce2_timeout ();
	v = do_get_mem_word (m);
	return v;
}

static uae_u32 REGPARAM2 chipmem_bget_ce2 (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= chipmem_bank.mask;
	ce2_timeout ();
	return chipmem_bank.baseaddr[addr];
}

static void REGPARAM2 chipmem_lput_ce2 (uaecptr addr, uae_u32 l)
{
	uae_u32 *m;

#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= chipmem_bank.mask;
	m = (uae_u32 *)(chipmem_bank.baseaddr + addr);
	ce2_timeout ();
	do_put_mem_long (m, l);
}

static void REGPARAM2 chipmem_wput_ce2 (uaecptr addr, uae_u32 w)
{
	uae_u16 *m;

#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= chipmem_bank.mask;
	m = (uae_u16 *)(chipmem_bank.baseaddr + addr);
	ce2_timeout ();
	do_put_mem_word (m, w);
}

static void REGPARAM2 chipmem_bput_ce2 (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= chipmem_bank.mask;
	ce2_timeout ();
	chipmem_bank.baseaddr[addr] = b;
}

#endif

uae_u32 REGPARAM2 chipmem_lget (uaecptr addr)
{
	uae_u32 *m;

	addr &= chipmem_bank.mask;
	m = (uae_u32 *)(chipmem_bank.baseaddr + addr);
	return do_get_mem_long (m);
}

static uae_u32 REGPARAM2 chipmem_wget (uaecptr addr)
{
	uae_u16 *m, v;

	addr &= chipmem_bank.mask;
	m = (uae_u16 *)(chipmem_bank.baseaddr + addr);
	v = do_get_mem_word (m);
	return v;
}

static uae_u32 REGPARAM2 chipmem_bget (uaecptr addr)
{
	uae_u8 v;
	addr &= chipmem_bank.mask;
	v = chipmem_bank.baseaddr[addr];
	return v;
}

void REGPARAM2 chipmem_lput (uaecptr addr, uae_u32 l)
{
	uae_u32 *m;

	addr &= chipmem_bank.mask;
	m = (uae_u32 *)(chipmem_bank.baseaddr + addr);
	do_put_mem_long (m, l);
}

#if 0
static int tables[256];
static int told, toldv;
#endif

void REGPARAM2 chipmem_wput (uaecptr addr, uae_u32 w)
{
	uae_u16 *m;

	addr &= chipmem_bank.mask;
	m = (uae_u16 *)(chipmem_bank.baseaddr + addr);
	do_put_mem_word (m, w);
#if 0
	if (addr == 4) {
		write_log (_T("*"));
#if 0
		if (told)
			tables[toldv] += hsync_counter - told;
		told = hsync_counter;
		toldv = w;
#endif
	}
#endif
}

void REGPARAM2 chipmem_bput (uaecptr addr, uae_u32 b)
{
	addr &= chipmem_bank.mask;
	chipmem_bank.baseaddr[addr] = b;
}

/* cpu chipmem access inside agnus addressable ram but no ram available */
static uae_u32 chipmem_dummy (void)
{
	/* not really right but something random that has more ones than zeros.. */
	return 0xffff & ~((1 << (uaerand () & 31)) | (1 << (uaerand () & 31)));
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
	m = (uae_u32 *)(chipmem_bank.baseaddr + addr);
	return do_get_mem_long (m);
}

uae_u32 REGPARAM2 chipmem_agnus_wget (uaecptr addr)
{
	uae_u16 *m;

	addr &= chipmem_full_mask;
	m = (uae_u16 *)(chipmem_bank.baseaddr + addr);
	return do_get_mem_word (m);
}

static uae_u32 REGPARAM2 chipmem_agnus_bget (uaecptr addr)
{
	addr &= chipmem_full_mask;
	return chipmem_bank.baseaddr[addr];
}

static void REGPARAM2 chipmem_agnus_lput (uaecptr addr, uae_u32 l)
{
	uae_u32 *m;

	addr &= chipmem_full_mask;
	if (addr >= chipmem_full_size)
		return;
	m = (uae_u32 *)(chipmem_bank.baseaddr + addr);
	do_put_mem_long (m, l);
}

void REGPARAM2 chipmem_agnus_wput (uaecptr addr, uae_u32 w)
{
	uae_u16 *m;

	addr &= chipmem_full_mask;
	if (addr >= chipmem_full_size)
		return;
	m = (uae_u16 *)(chipmem_bank.baseaddr + addr);
	do_put_mem_word (m, w);
}

static void REGPARAM2 chipmem_agnus_bput (uaecptr addr, uae_u32 b)
{
	addr &= chipmem_full_mask;
	if (addr >= chipmem_full_size)
		return;
	chipmem_bank.baseaddr[addr] = b;
}

static int REGPARAM2 chipmem_check (uaecptr addr, uae_u32 size)
{
	addr &= chipmem_bank.mask;
	return (addr + size) <= chipmem_full_size;
}

static uae_u8 *REGPARAM2 chipmem_xlate (uaecptr addr)
{
	addr &= chipmem_bank.mask;
	return chipmem_bank.baseaddr + addr;
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

MEMORY_FUNCTIONS(bogomem);

/* CDTV expension memory card memory */

MEMORY_FUNCTIONS(cardmem);

/* A3000 motherboard fast memory */

MEMORY_FUNCTIONS(a3000lmem);
MEMORY_FUNCTIONS(a3000hmem);

/* Kick memory */

uae_u16 kickstart_version;

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
	protect_roms (false);
	if (mode == 0) {
		a1000_kickstart_mode = 0;
		memcpy (kickmem_bank.baseaddr, kickmem_bank.baseaddr + ROM_SIZE_256, ROM_SIZE_256);
		kickstart_version = (kickmem_bank.baseaddr[ROM_SIZE_256 + 12] << 8) | kickmem_bank.baseaddr[ROM_SIZE_256 + 13];
	} else {
		a1000_kickstart_mode = 1;
		memcpy (kickmem_bank.baseaddr, a1000_bootrom, ROM_SIZE_256);
		kickstart_version = 0;
	}
	if (kickstart_version == 0xffff)
		kickstart_version = 0;
}

void a1000_reset (void)
{
	a1000_handle_kickstart (1);
}

static void REGPARAM3 kickmem_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 kickmem_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 kickmem_bput (uaecptr, uae_u32) REGPARAM;

MEMORY_BGET(kickmem);
MEMORY_WGET(kickmem);
MEMORY_LGET(kickmem);
MEMORY_CHECK(kickmem);
MEMORY_XLATE(kickmem);

static void REGPARAM2 kickmem_lput (uaecptr addr, uae_u32 b)
{
	uae_u32 *m;
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (currprefs.rom_readwrite && rom_write_enabled) {
		addr &= kickmem_bank.mask;
		m = (uae_u32 *)(kickmem_bank.baseaddr + addr);
		do_put_mem_long (m, b);
#if 0
		if (addr == ROM_SIZE_512-4) {
			rom_write_enabled = false;
			write_log (_T("ROM write disabled\n"));
		}
#endif
	} else if (a1000_kickstart_mode) {
		if (addr >= 0xfc0000) {
			addr &= kickmem_bank.mask;
			m = (uae_u32 *)(kickmem_bank.baseaddr + addr);
			do_put_mem_long (m, b);
			return;
		} else
			a1000_handle_kickstart (0);
	} else if (currprefs.illegal_mem) {
		write_log (_T("Illegal kickmem lput at %08x\n"), addr);
	}
}

static void REGPARAM2 kickmem_wput (uaecptr addr, uae_u32 b)
{
	uae_u16 *m;
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (currprefs.rom_readwrite && rom_write_enabled) {
		addr &= kickmem_bank.mask;
		m = (uae_u16 *)(kickmem_bank.baseaddr + addr);
		do_put_mem_word (m, b);
	} else if (a1000_kickstart_mode) {
		if (addr >= 0xfc0000) {
			addr &= kickmem_bank.mask;
			m = (uae_u16 *)(kickmem_bank.baseaddr + addr);
			do_put_mem_word (m, b);
			return;
		} else
			a1000_handle_kickstart (0);
	} else if (currprefs.illegal_mem) {
		write_log (_T("Illegal kickmem wput at %08x\n"), addr);
	}
}

static void REGPARAM2 kickmem_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (currprefs.rom_readwrite && rom_write_enabled) {
		addr &= kickmem_bank.mask;
		kickmem_bank.baseaddr[addr] = b;
	} else if (a1000_kickstart_mode) {
		if (addr >= 0xfc0000) {
			addr &= kickmem_bank.mask;
			kickmem_bank.baseaddr[addr] = b;
			return;
		} else
			a1000_handle_kickstart (0);
	} else if (currprefs.illegal_mem) {
		write_log (_T("Illegal kickmem bput at %08x\n"), addr);
	}
}

static void REGPARAM2 kickmem2_lput (uaecptr addr, uae_u32 l)
{
	uae_u32 *m;
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= kickmem_bank.mask;
	m = (uae_u32 *)(kickmem_bank.baseaddr + addr);
	do_put_mem_long (m, l);
}

static void REGPARAM2 kickmem2_wput (uaecptr addr, uae_u32 w)
{
	uae_u16 *m;
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= kickmem_bank.mask;
	m = (uae_u16 *)(kickmem_bank.baseaddr + addr);
	do_put_mem_word (m, w);
}

static void REGPARAM2 kickmem2_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= kickmem_bank.mask;
	kickmem_bank.baseaddr[addr] = b;
}

/* CD32/CDTV extended kick memory */

static int extendedkickmem_type;

#define EXTENDED_ROM_CD32 1
#define EXTENDED_ROM_CDTV 2
#define EXTENDED_ROM_KS 3
#define EXTENDED_ROM_ARCADIA 4

static void REGPARAM3 extendedkickmem_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 extendedkickmem_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 extendedkickmem_bput (uaecptr, uae_u32) REGPARAM;

MEMORY_BGET(extendedkickmem);
MEMORY_WGET(extendedkickmem);
MEMORY_LGET(extendedkickmem);
MEMORY_CHECK(extendedkickmem);
MEMORY_XLATE(extendedkickmem);

static void REGPARAM2 extendedkickmem_lput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (currprefs.illegal_mem)
		write_log (_T("Illegal extendedkickmem lput at %08x\n"), addr);
}
static void REGPARAM2 extendedkickmem_wput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (currprefs.illegal_mem)
		write_log (_T("Illegal extendedkickmem wput at %08x\n"), addr);
}
static void REGPARAM2 extendedkickmem_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (currprefs.illegal_mem)
		write_log (_T("Illegal extendedkickmem lput at %08x\n"), addr);
}


static void REGPARAM3 extendedkickmem2_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 extendedkickmem2_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 extendedkickmem2_bput (uaecptr, uae_u32) REGPARAM;

MEMORY_BGET(extendedkickmem2);
MEMORY_WGET(extendedkickmem2);
MEMORY_LGET(extendedkickmem2);
MEMORY_CHECK(extendedkickmem2);
MEMORY_XLATE(extendedkickmem2);

static void REGPARAM2 extendedkickmem2_lput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (currprefs.illegal_mem)
		write_log (_T("Illegal extendedkickmem2 lput at %08x\n"), addr);
}
static void REGPARAM2 extendedkickmem2_wput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (currprefs.illegal_mem)
		write_log (_T("Illegal extendedkickmem2 wput at %08x\n"), addr);
}
static void REGPARAM2 extendedkickmem2_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (currprefs.illegal_mem)
		write_log (_T("Illegal extendedkickmem2 lput at %08x\n"), addr);
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
				write_log (_T("Your Amiga program just did something terribly stupid %08X PC=%08X\n"), a, M68K_GETPC);
				if (debugging || DEBUG_STUPID) {
					activate_debugger ();
					m68k_dumpstate (0);
				}
				for (i = 0; i < 10; i++) {
					write_log (_T("%08X "), i >= 5 ? a3 : a2);
					for (j = 0; j < 16; j += 2) {
						write_log (_T(" %04X"), get_word (i >= 5 ? a3 : a2));
						if (i >= 5) a3 += 2; else a2 += 2;
					}
					write_log (_T("\n"));
				}
				memory_map_dump ();
			}
			be_cnt++;
			if (regs.s || be_cnt > 1000) {
				cpu_halt (3);
				be_cnt = 0;
			} else {
				regs.panic = 4;
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
	default_xlate, dummy_check, NULL, _T("Ones"),
	dummy_lgeti, dummy_wgeti, ABFLAG_NONE
};

addrbank chipmem_bank = {
	chipmem_lget, chipmem_wget, chipmem_bget,
	chipmem_lput, chipmem_wput, chipmem_bput,
	chipmem_xlate, chipmem_check, NULL, _T("Chip memory"),
	chipmem_lget, chipmem_wget, ABFLAG_RAM
};

addrbank chipmem_dummy_bank = {
	chipmem_dummy_lget, chipmem_dummy_wget, chipmem_dummy_bget,
	chipmem_dummy_lput, chipmem_dummy_wput, chipmem_dummy_bput,
	default_xlate, dummy_check, NULL, _T("Dummy Chip memory"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO
};


#ifdef AGA
addrbank chipmem_bank_ce2 = {
	chipmem_lget_ce2, chipmem_wget_ce2, chipmem_bget_ce2,
	chipmem_lput_ce2, chipmem_wput_ce2, chipmem_bput_ce2,
	chipmem_xlate, chipmem_check, NULL, _T("Chip memory (68020 'ce')"),
	chipmem_lget_ce2, chipmem_wget_ce2, ABFLAG_RAM
};
#endif

addrbank bogomem_bank = {
	bogomem_lget, bogomem_wget, bogomem_bget,
	bogomem_lput, bogomem_wput, bogomem_bput,
	bogomem_xlate, bogomem_check, NULL, _T("Slow memory"),
	bogomem_lget, bogomem_wget, ABFLAG_RAM
};

addrbank cardmem_bank = {
	cardmem_lget, cardmem_wget, cardmem_bget,
	cardmem_lput, cardmem_wput, cardmem_bput,
	cardmem_xlate, cardmem_check, NULL, _T("CDTV memory card"),
	cardmem_lget, cardmem_wget, ABFLAG_RAM
};

addrbank a3000lmem_bank = {
	a3000lmem_lget, a3000lmem_wget, a3000lmem_bget,
	a3000lmem_lput, a3000lmem_wput, a3000lmem_bput,
	a3000lmem_xlate, a3000lmem_check, NULL, _T("RAMSEY memory (low)"),
	a3000lmem_lget, a3000lmem_wget, ABFLAG_RAM
};

addrbank a3000hmem_bank = {
	a3000hmem_lget, a3000hmem_wget, a3000hmem_bget,
	a3000hmem_lput, a3000hmem_wput, a3000hmem_bput,
	a3000hmem_xlate, a3000hmem_check, NULL, _T("RAMSEY memory (high)"),
	a3000hmem_lget, a3000hmem_wget, ABFLAG_RAM
};

addrbank kickmem_bank = {
	kickmem_lget, kickmem_wget, kickmem_bget,
	kickmem_lput, kickmem_wput, kickmem_bput,
	kickmem_xlate, kickmem_check, NULL, _T("Kickstart ROM"),
	kickmem_lget, kickmem_wget, ABFLAG_ROM
};

addrbank kickram_bank = {
	kickmem_lget, kickmem_wget, kickmem_bget,
	kickmem2_lput, kickmem2_wput, kickmem2_bput,
	kickmem_xlate, kickmem_check, NULL, _T("Kickstart Shadow RAM"),
	kickmem_lget, kickmem_wget, ABFLAG_UNK | ABFLAG_SAFE
};

addrbank extendedkickmem_bank = {
	extendedkickmem_lget, extendedkickmem_wget, extendedkickmem_bget,
	extendedkickmem_lput, extendedkickmem_wput, extendedkickmem_bput,
	extendedkickmem_xlate, extendedkickmem_check, NULL, _T("Extended Kickstart ROM"),
	extendedkickmem_lget, extendedkickmem_wget, ABFLAG_ROM
};
addrbank extendedkickmem2_bank = {
	extendedkickmem2_lget, extendedkickmem2_wget, extendedkickmem2_bget,
	extendedkickmem2_lput, extendedkickmem2_wput, extendedkickmem2_bput,
	extendedkickmem2_xlate, extendedkickmem2_check, NULL, _T("Extended 2nd Kickstart ROM"),
	extendedkickmem2_lget, extendedkickmem2_wget, ABFLAG_ROM
};

MEMORY_FUNCTIONS(custmem1);
MEMORY_FUNCTIONS(custmem2);

addrbank custmem1_bank = {
	custmem1_lget, custmem1_wget, custmem1_bget,
	custmem1_lput, custmem1_wput, custmem1_bput,
	custmem1_xlate, custmem1_check, NULL, _T("Non-autoconfig RAM #1"),
	custmem1_lget, custmem1_wget, ABFLAG_RAM
};
addrbank custmem2_bank = {
	custmem2_lget, custmem2_wget, custmem2_bget,
	custmem2_lput, custmem2_wput, custmem2_bput,
	custmem2_xlate, custmem2_check, NULL, _T("Non-autoconfig RAM #2"),
	custmem2_lget, custmem2_wget, ABFLAG_RAM
};

#define fkickmem_size ROM_SIZE_512
static int a3000_f0;
void a3000_fakekick (int map)
{
	static uae_u8 *kickstore;

	protect_roms (false);
	if (map) {
		uae_u8 *fkickmemory = a3000lmem_bank.baseaddr + a3000lmem_bank.allocated - fkickmem_size;
		if (fkickmemory[2] == 0x4e && fkickmemory[3] == 0xf9 && fkickmemory[4] == 0x00) {
			if (!kickstore)
				kickstore = xmalloc (uae_u8, fkickmem_size);
			memcpy (kickstore, kickmem_bank.baseaddr, fkickmem_size);
			if (fkickmemory[5] == 0xfc) {
				memcpy (kickmem_bank.baseaddr, fkickmemory, fkickmem_size / 2);
				memcpy (kickmem_bank.baseaddr + fkickmem_size / 2, fkickmemory, fkickmem_size / 2);
				extendedkickmem_bank.allocated = 65536;
				extendedkickmem_bank.mask = extendedkickmem_bank.allocated - 1;
				extendedkickmem_bank.baseaddr = mapped_malloc (extendedkickmem_bank.allocated, _T("rom_f0"));
				memcpy (extendedkickmem_bank.baseaddr, fkickmemory + fkickmem_size / 2, 65536);
				map_banks (&extendedkickmem_bank, 0xf0, 1, 1);
				a3000_f0 = 1;
			} else {
				memcpy (kickmem_bank.baseaddr, fkickmemory, fkickmem_size);
			}
		}
	} else {
		if (a3000_f0) {
			map_banks (&dummy_bank, 0xf0, 1, 1);
			mapped_free (extendedkickmem_bank.baseaddr);
			extendedkickmem_bank.baseaddr = NULL;
			a3000_f0 = 0;
		}
		if (kickstore)
			memcpy (kickmem_bank.baseaddr, kickstore, fkickmem_size);
		xfree (kickstore);
		kickstore = NULL;
	}
	protect_roms (true);
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
	} else if (size >= ROM_SIZE_512 && !memcmp (buffer, "AMIG", 4)) {
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

	if (kickdisk && i > ROM_SIZE_256)
		i = ROM_SIZE_256;
#if 0
	if (i >= ROM_SIZE_256 && (i != ROM_SIZE_256 && i != ROM_SIZE_512 && i != ROM_SIZE_512 * 2 && i != ROM_SIZE_512 * 4)) {
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
		a1000_bootrom = xcalloc (uae_u8, ROM_SIZE_256);
		while (off + i < ROM_SIZE_256) {
			memcpy (a1000_bootrom + off, kickmem_bank.baseaddr, i);
			off += i;
		}
		memset (kickmem_bank.baseaddr, 0, kickmem_bank.allocated);
		a1000_handle_kickstart (1);
		dochecksum = 0;
		i = ROM_SIZE_512;
	}

	for (j = 0; j < 256 && i >= ROM_SIZE_256; j++) {
		if (!memcmp (mem + j, kickstring, strlen (kickstring) + 1))
			break;
	}

	if (j == 256 || i < ROM_SIZE_256)
		dochecksum = 0;
	if (dochecksum)
		kickstart_checksum (mem, size);
	return i;
}

static bool load_extendedkickstart (const TCHAR *romextfile, int type)
{
	struct zfile *f;
	int size, off;
	bool ret = false;

	if (_tcslen (romextfile) == 0)
		return false;
	if (is_arcadia_rom (romextfile) == ARCADIA_BIOS) {
		extendedkickmem_type = EXTENDED_ROM_ARCADIA;
		return false;
	}
	f = read_rom_name (romextfile);
	if (!f) {
		notify_user (NUMSG_NOEXTROM);
		return false;
	}
	zfile_fseek (f, 0, SEEK_END);
	size = zfile_ftell (f);
	extendedkickmem_bank.allocated = ROM_SIZE_512;
	off = 0;
	if (type == 0) {
		if (currprefs.cs_cd32cd) {
			extendedkickmem_type = EXTENDED_ROM_CD32;
		} else if (currprefs.cs_cdtvcd || currprefs.cs_cdtvram) {
			extendedkickmem_type = EXTENDED_ROM_CDTV;
		} else if (size > 300000) {
			extendedkickmem_type = EXTENDED_ROM_CD32;
		} else if (need_uae_boot_rom () != 0xf00000) {
			extendedkickmem_type = EXTENDED_ROM_CDTV;
		}	
	} else {
		extendedkickmem_type = type;
	}
	if (extendedkickmem_type) {
		zfile_fseek (f, off, SEEK_SET);
		switch (extendedkickmem_type) {
		case EXTENDED_ROM_CDTV:
			extendedkickmem_bank.baseaddr = mapped_malloc (extendedkickmem_bank.allocated, _T("rom_f0"));
			extendedkickmem_bank.start = 0xf00000;
			break;
		case EXTENDED_ROM_CD32:
			extendedkickmem_bank.baseaddr = mapped_malloc (extendedkickmem_bank.allocated, _T("rom_e0"));
			extendedkickmem_bank.start = 0xe00000;
			break;
		}
		if (extendedkickmem_bank.baseaddr) {
			read_kickstart (f, extendedkickmem_bank.baseaddr, extendedkickmem_bank.allocated, 0, 1);
			extendedkickmem_bank.mask = extendedkickmem_bank.allocated - 1;
			ret = true;
		}
	}
	zfile_fclose (f);
	return ret;
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
				write_log (_T("Kickstart KickShifted @%04X\n"), i);
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
	uaecptr base = size == ROM_SIZE_512 ? 0xf80000 : 0xfc0000;

	if (currprefs.cs_mbdmac != 2) {
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
							write_log (_T("KSPatcher: '%s' at %08X disabled\n"), s, i + base);
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
	}
	return patched;
}

static void patch_kick (void)
{
	int patched = 0;
	if (kickmem_bank.allocated >= ROM_SIZE_512 && currprefs.kickshifter)
		patched += patch_shapeshifter (kickmem_bank.baseaddr);
	patched += patch_residents (kickmem_bank.baseaddr, kickmem_bank.allocated);
	if (extendedkickmem_bank.baseaddr) {
		patched += patch_residents (extendedkickmem_bank.baseaddr, extendedkickmem_bank.allocated);
		if (patched)
			kickstart_fix_checksum (extendedkickmem_bank.baseaddr, extendedkickmem_bank.allocated);
	}
	if (patched)
		kickstart_fix_checksum (kickmem_bank.baseaddr, kickmem_bank.allocated);
}

extern unsigned char arosrom[];
extern unsigned int arosrom_len;
extern int seriallog;
static bool load_kickstart_replacement (void)
{
	struct zfile *f;
	
	f = zfile_fopen_data (_T("aros.gz"), arosrom_len, arosrom);
	if (!f)
		return false;
	f = zfile_gunzip (f);
	if (!f)
		return false;

	extendedkickmem_bank.allocated = ROM_SIZE_512;
	extendedkickmem_bank.mask = ROM_SIZE_512 - 1;
	extendedkickmem_type = EXTENDED_ROM_KS;
	extendedkickmem_bank.baseaddr = mapped_malloc (extendedkickmem_bank.allocated, _T("rom_e0"));
	read_kickstart (f, extendedkickmem_bank.baseaddr, ROM_SIZE_512, 0, 1);

	kickmem_bank.allocated = ROM_SIZE_512;
	kickmem_bank.mask = ROM_SIZE_512 - 1;
	read_kickstart (f, kickmem_bank.baseaddr, ROM_SIZE_512, 1, 0);

	zfile_fclose (f);

	changed_prefs.custom_memory_addrs[0] = currprefs.custom_memory_addrs[0] = 0xa80000;
	changed_prefs.custom_memory_sizes[0] = currprefs.custom_memory_sizes[0] = 512 * 1024;
	changed_prefs.custom_memory_addrs[1] = currprefs.custom_memory_addrs[1] = 0xb00000;
	changed_prefs.custom_memory_sizes[1] = currprefs.custom_memory_sizes[1] = 512 * 1024;

	seriallog = -1;
	return true;
}

static int load_kickstart (void)
{
	struct zfile *f;
	TCHAR tmprom[MAX_DPATH], tmprom2[MAX_DPATH];
	int patched = 0;

	cloanto_rom = 0;
	if (!_tcscmp (currprefs.romfile, _T(":AROS")))
		return load_kickstart_replacement ();
	f = read_rom_name (currprefs.romfile);
	_tcscpy (tmprom, currprefs.romfile);
	if (f == NULL) {
		_stprintf (tmprom2, _T("%s%s"), start_path_data, currprefs.romfile);
		f = rom_fopen (tmprom2, _T("rb"), ZFD_NORMAL);
		if (f == NULL) {
			_stprintf (currprefs.romfile, _T("%sroms/kick.rom"), start_path_data);
			f = rom_fopen (currprefs.romfile, _T("rb"), ZFD_NORMAL);
			if (f == NULL) {
				_stprintf (currprefs.romfile, _T("%skick.rom"), start_path_data);
				f = rom_fopen (currprefs.romfile, _T("rb"), ZFD_NORMAL);
				if (f == NULL) {
					_stprintf (currprefs.romfile, _T("%s../shared/rom/kick.rom"), start_path_data);
					f = rom_fopen (currprefs.romfile, _T("rb"), ZFD_NORMAL);
					if (f == NULL) {
						_stprintf (currprefs.romfile, _T("%s../System/rom/kick.rom"), start_path_data);
						f = rom_fopen (currprefs.romfile, _T("rb"), ZFD_NORMAL);
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
	if (f == NULL) /* still no luck */
		goto err;

	if (f != NULL) {
		int filesize, size, maxsize;
		int kspos = ROM_SIZE_512;
		int extpos = 0;

		maxsize = ROM_SIZE_512;
		zfile_fseek (f, 0, SEEK_END);
		filesize = zfile_ftell (f);
		zfile_fseek (f, 0, SEEK_SET);
		if (filesize == 1760 * 512) {
			filesize = ROM_SIZE_256;
			maxsize = ROM_SIZE_256;
		}
		if (filesize == ROM_SIZE_512 + 8) {
			/* GVP 0xf0 kickstart */
			zfile_fseek (f, 8, SEEK_SET);
		}
		if (filesize >= ROM_SIZE_512 * 2) {
			struct romdata *rd = getromdatabyzfile(f);
			zfile_fseek (f, kspos, SEEK_SET);
		}
		if (filesize >= ROM_SIZE_512 * 4) {
			kspos = ROM_SIZE_512 * 3;
			extpos = 0;
			zfile_fseek (f, kspos, SEEK_SET);
		}
		size = read_kickstart (f, kickmem_bank.baseaddr, maxsize, 1, 0);
		if (size == 0)
			goto err;
		kickmem_bank.mask = size - 1;
		kickmem_bank.allocated = size;
		if (filesize >= ROM_SIZE_512 * 2 && !extendedkickmem_type) {
			extendedkickmem_bank.allocated = ROM_SIZE_512;
			if (currprefs.cs_cdtvcd || currprefs.cs_cdtvram) {
				extendedkickmem_type = EXTENDED_ROM_CDTV;
				extendedkickmem_bank.allocated *= 2;
				extendedkickmem_bank.baseaddr = mapped_malloc (extendedkickmem_bank.allocated, _T("rom_f0"));
				extendedkickmem_bank.start = 0xf00000;
			} else {
				extendedkickmem_type = EXTENDED_ROM_KS;
				extendedkickmem_bank.baseaddr = mapped_malloc (extendedkickmem_bank.allocated, _T("rom_e0"));
				extendedkickmem_bank.start = 0xe00000;
			}
			zfile_fseek (f, extpos, SEEK_SET);
			read_kickstart (f, extendedkickmem_bank.baseaddr, extendedkickmem_bank.allocated, 0, 1);
			extendedkickmem_bank.mask = extendedkickmem_bank.allocated - 1;
		}
		if (filesize > ROM_SIZE_512 * 2) {
			extendedkickmem2_bank.allocated = ROM_SIZE_512 * 2;
			extendedkickmem2_bank.baseaddr = mapped_malloc (extendedkickmem2_bank.allocated, _T("rom_a8"));
			zfile_fseek (f, extpos + ROM_SIZE_512, SEEK_SET);
			read_kickstart (f, extendedkickmem2_bank.baseaddr, ROM_SIZE_512, 0, 1);
			zfile_fseek (f, extpos + ROM_SIZE_512 * 2, SEEK_SET);
			read_kickstart (f, extendedkickmem2_bank.baseaddr + ROM_SIZE_512, ROM_SIZE_512, 0, 1);
			extendedkickmem2_bank.mask = extendedkickmem2_bank.allocated - 1;
			extendedkickmem2_bank.start = 0xa80000;
		}
	}

#if defined(AMIGA)
chk_sum:
#endif

	kickstart_version = (kickmem_bank.baseaddr[12] << 8) | kickmem_bank.baseaddr[13];
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

uae_u8 *mapped_malloc (size_t s, const TCHAR *file)
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
	write_log (_T("Start Dump:\n"));
	while (x) {
		write_log (_T("this=%p,Native %p,id %d,prev=%p,next=%p,size=0x%08x\n"),
			x, x->native_address, x->id, x->prev, x->next, x->size);
		x = x->next;
	}
	write_log (_T("End Dump:\n"));
}

static shmpiece *find_shmpiece (uae_u8 *base, bool safe)
{
	shmpiece *x = shm_start;

	while (x && x->native_address != base)
		x = x->next;
	if (!x) {
		if (safe || bogomem_aliasing)
			return 0;
		write_log (_T("NATMEM: Failure to find mapping at %08X, %p\n"), base - NATMEM_OFFSET, base);
		nocanbang ();
		return 0;
	}
	return x;
}

static void delete_shmmaps (uae_u32 start, uae_u32 size)
{
	if (!needmman ())
		return;

	while (size) {
		uae_u8 *base = mem_banks[bankindex (start)]->baseaddr;
		if (base) {
			shmpiece *x;
			//base = ((uae_u8*)NATMEM_OFFSET)+start;

			x = find_shmpiece (base, true);
			if (!x)
				return;

			if (x->size > size) {
				if (isdirectjit ())
					write_log (_T("NATMEM WARNING: size mismatch mapping at %08x (size %08x, delsize %08x)\n"),start,x->size,size);
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

	if (!needmman ())
		return;

	if (!base)
		return;

	x = find_shmpiece (base, false);
	if (!x)
		return;

	y = xmalloc (shmpiece, 1);
	*y = *x;
	base = ((uae_u8 *) NATMEM_OFFSET) + start;
	y->native_address = (uae_u8*)shmat (y->id, base, 0);
	if (y->native_address == (void *) -1) {
		write_log (_T("NATMEM: Failure to map existing at %08x (%p)\n"), start, base);
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

	if (!needmman ()) {
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
		x->name = file;
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

static bool singlebit (uae_u32 v)
{
	while (v && !(v & 1))
		v >>= 1;
	return (v & ~1) == 0;
}

static void allocate_memory (void)
{
	bogomem_aliasing = false;
	/* emulate 0.5M+0.5M with 1M Agnus chip ram aliasing */
	if (currprefs.chipmem_size == 0x80000 && currprefs.bogomem_size >= 0x80000 &&
		(currprefs.chipset_mask & CSMASK_ECS_AGNUS) && !(currprefs.chipset_mask & CSMASK_AGA) && currprefs.cpu_model < 68020) {
			if ((chipmem_bank.allocated != currprefs.chipmem_size || bogomem_bank.allocated != currprefs.bogomem_size)) {
				int memsize1, memsize2;
				mapped_free (chipmem_bank.baseaddr);
				chipmem_bank.baseaddr = 0;
				mapped_free (bogomem_bank.baseaddr);
				bogomem_bank.baseaddr = NULL;
				bogomem_bank.allocated = 0;
				memsize1 = chipmem_bank.allocated = currprefs.chipmem_size;
				memsize2 = bogomem_bank.allocated = currprefs.bogomem_size;
				chipmem_bank.mask = chipmem_bank.allocated - 1;
				chipmem_bank.start = chipmem_start_addr;
				chipmem_full_mask = bogomem_bank.allocated * 2 - 1;
				chipmem_full_size = 0x80000 * 2;
				chipmem_bank.baseaddr = mapped_malloc (memsize1 + memsize2, _T("chip"));
				bogomem_bank.baseaddr = chipmem_bank.baseaddr + memsize1;
				bogomem_bank.mask = bogomem_bank.allocated - 1;
				bogomem_bank.start = bogomem_start_addr;
				if (chipmem_bank.baseaddr == 0) {
					write_log (_T("Fatal error: out of memory for chipmem.\n"));
					chipmem_bank.allocated = 0;
				} else {
					need_hardreset = true;
				}
			}
			bogomem_aliasing = true;
	}

	if (chipmem_bank.allocated != currprefs.chipmem_size) {
		int memsize;
		mapped_free (chipmem_bank.baseaddr);
		chipmem_bank.baseaddr = 0;
		if (currprefs.chipmem_size > 2 * 1024 * 1024)
			free_fastmemory ();

		memsize = chipmem_bank.allocated = chipmem_full_size = currprefs.chipmem_size;
		chipmem_full_mask = chipmem_bank.mask = chipmem_bank.allocated - 1;
		chipmem_bank.start = chipmem_start_addr;
		if (!currprefs.cachesize && memsize < 0x100000)
			memsize = 0x100000;
		if (memsize > 0x100000 && memsize < 0x200000)
			memsize = 0x200000;
		chipmem_bank.baseaddr = mapped_malloc (memsize, _T("chip"));
		if (chipmem_bank.baseaddr == 0) {
			write_log (_T("Fatal error: out of memory for chipmem.\n"));
			chipmem_bank.allocated = 0;
		} else {
			need_hardreset = true;
			if (memsize > chipmem_bank.allocated)
				memset (chipmem_bank.baseaddr + chipmem_bank.allocated, 0xff, memsize - chipmem_bank.allocated);
		}
		currprefs.chipset_mask = changed_prefs.chipset_mask;
		chipmem_full_mask = chipmem_bank.allocated - 1;
		if ((currprefs.chipset_mask & CSMASK_ECS_AGNUS) && !currprefs.cachesize) {
			if (chipmem_bank.allocated < 0x100000)
				chipmem_full_mask = 0x100000 - 1;
			if (chipmem_bank.allocated > 0x100000 && chipmem_bank.allocated < 0x200000)
				chipmem_full_mask = chipmem_bank.mask = 0x200000 - 1;
		}
	}

	if (bogomem_bank.allocated != currprefs.bogomem_size) {
		if (!(bogomem_bank.allocated == 0x200000 && currprefs.bogomem_size == 0x180000)) {
			mapped_free (bogomem_bank.baseaddr);
			bogomem_bank.baseaddr = NULL;
			bogomem_bank.allocated = 0;

			bogomem_bank.allocated = currprefs.bogomem_size;
			if (bogomem_bank.allocated >= 0x180000)
				bogomem_bank.allocated = 0x200000;
			bogomem_bank.mask = bogomem_bank.allocated - 1;
			bogomem_bank.start = bogomem_start_addr;

			if (bogomem_bank.allocated) {
				bogomem_bank.baseaddr = mapped_malloc (bogomem_bank.allocated, _T("bogo"));
				if (bogomem_bank.baseaddr == 0) {
					write_log (_T("Out of memory for bogomem.\n"));
					bogomem_bank.allocated = 0;
				}
			}
			need_hardreset = true;
		}
	}
	if (a3000lmem_bank.allocated != currprefs.mbresmem_low_size) {
		mapped_free (a3000lmem_bank.baseaddr);
		a3000lmem_bank.baseaddr = NULL;

		a3000lmem_bank.allocated = currprefs.mbresmem_low_size;
		a3000lmem_bank.mask = a3000lmem_bank.allocated - 1;
		a3000lmem_bank.start = 0x08000000 - a3000lmem_bank.allocated;
		if (a3000lmem_bank.allocated) {
			a3000lmem_bank.baseaddr = mapped_malloc (a3000lmem_bank.allocated, _T("ramsey_low"));
			if (a3000lmem_bank.baseaddr == 0) {
				write_log (_T("Out of memory for a3000lowmem.\n"));
				a3000lmem_bank.allocated = 0;
			}
		}
		need_hardreset = true;
	}
	if (a3000hmem_bank.allocated != currprefs.mbresmem_high_size) {
		mapped_free (a3000hmem_bank.baseaddr);
		a3000hmem_bank.baseaddr = NULL;

		a3000hmem_bank.allocated = currprefs.mbresmem_high_size;
		a3000hmem_bank.mask = a3000hmem_bank.allocated - 1;
		a3000hmem_bank.start = 0x08000000;
		if (a3000hmem_bank.allocated) {
			a3000hmem_bank.baseaddr = mapped_malloc (a3000hmem_bank.allocated, _T("ramsey_high"));
			if (a3000hmem_bank.baseaddr == 0) {
				write_log (_T("Out of memory for a3000highmem.\n"));
				a3000hmem_bank.allocated = 0;
			}
		}
		need_hardreset = true;
	}
#ifdef CDTV
	if (cardmem_bank.allocated != currprefs.cs_cdtvcard * 1024) {
		mapped_free (cardmem_bank.baseaddr);
		cardmem_bank.baseaddr = NULL;

		cardmem_bank.allocated = currprefs.cs_cdtvcard * 1024;
		cardmem_bank.mask = cardmem_bank.allocated - 1;
		cardmem_bank.start = 0xe00000;
		if (cardmem_bank.allocated) {
			cardmem_bank.baseaddr = mapped_malloc (cardmem_bank.allocated, _T("rom_e0"));
			if (cardmem_bank.baseaddr == 0) {
				write_log (_T("Out of memory for cardmem.\n"));
				cardmem_bank.allocated = 0;
			}
		}
		cdtv_loadcardmem(cardmem_bank.baseaddr, cardmem_bank.allocated);
	}
#endif
	if (custmem1_bank.allocated != currprefs.custom_memory_sizes[0]) {
		mapped_free (custmem1_bank.baseaddr);
		custmem1_bank.baseaddr = NULL;
		custmem1_bank.allocated = currprefs.custom_memory_sizes[0];
		// custmem1 and 2 can have non-power of 2 size so only set correct mask if size is power of 2.
		custmem1_bank.mask = singlebit (custmem1_bank.allocated) ? custmem1_bank.allocated - 1 : -1;
		custmem1_bank.start = currprefs.custom_memory_addrs[0];
		if (custmem1_bank.allocated) {
			custmem1_bank.baseaddr = mapped_malloc (custmem1_bank.allocated, _T("custmem1"));
			if (!custmem1_bank.baseaddr)
				custmem1_bank.allocated = 0;
		}
	}
	if (custmem2_bank.allocated != currprefs.custom_memory_sizes[1]) {
		mapped_free (custmem2_bank.baseaddr);
		custmem2_bank.baseaddr = NULL;
		custmem2_bank.allocated = currprefs.custom_memory_sizes[1];
		custmem2_bank.mask = singlebit (custmem2_bank.allocated) ? custmem2_bank.allocated - 1 : -1;
		custmem2_bank.start = currprefs.custom_memory_addrs[1];
		if (custmem2_bank.allocated) {
			custmem2_bank.baseaddr = mapped_malloc (custmem2_bank.allocated, _T("custmem2"));
			if (!custmem2_bank.baseaddr)
				custmem2_bank.allocated = 0;
		}
	}

	if (savestate_state == STATE_RESTORE) {
		if (bootrom_filepos) {
			protect_roms (false);
			restore_ram (bootrom_filepos, rtarea);
			protect_roms (true);
		}
		restore_ram (chip_filepos, chipmem_bank.baseaddr);
		if (bogomem_bank.allocated > 0)
			restore_ram (bogo_filepos, bogomem_bank.baseaddr);
		if (a3000lmem_bank.allocated > 0)
			restore_ram (a3000lmem_filepos, a3000lmem_bank.baseaddr);
		if (a3000hmem_bank.allocated > 0)
			restore_ram (a3000hmem_filepos, a3000hmem_bank.baseaddr);
	}
#ifdef AGA
	chipmem_bank_ce2.baseaddr = chipmem_bank.baseaddr;
#endif
	bootrom_filepos = 0;
	chip_filepos = 0;
	bogo_filepos = 0;
	a3000lmem_filepos = 0;
	a3000hmem_filepos = 0;
}

static void fill_ce_banks (void)
{
	int i;

	if (currprefs.cpu_model <= 68010) {
		memset (ce_banktype, CE_MEMBANK_FAST16, sizeof ce_banktype);
	} else {
		memset (ce_banktype, CE_MEMBANK_FAST32, sizeof ce_banktype);
	}
	// data cachable regions (2 = burst supported)
	memset (ce_cachable, 0, sizeof ce_cachable);
	memset (ce_cachable + (0x00200000 >> 16), 1 | 2, currprefs.fastmem_size >> 16);
	memset (ce_cachable + (0x00c00000 >> 16), 1, currprefs.bogomem_size >> 16);
	memset (ce_cachable + (z3fastmem_bank.start >> 16), 1 | 2, currprefs.z3fastmem_size >> 16);
	memset (ce_cachable + (z3fastmem2_bank.start >> 16), 1 | 2, currprefs.z3fastmem2_size >> 16);
	memset (ce_cachable + (a3000hmem_bank.start >> 16), 1 | 2, currprefs.mbresmem_high_size >> 16);
	memset (ce_cachable + (a3000lmem_bank.start >> 16), 1 | 2, currprefs.mbresmem_low_size >> 16);

	if (&get_mem_bank (0) == &chipmem_bank) {
		for (i = 0; i < (0x200000 >> 16); i++) {
			ce_banktype[i] = (currprefs.cs_mbdmac || (currprefs.chipset_mask & CSMASK_AGA)) ? CE_MEMBANK_CHIP32 : CE_MEMBANK_CHIP16;
		}
	}
	if (!currprefs.cs_slowmemisfast) {
		for (i = (0xc00000 >> 16); i < (0xe00000 >> 16); i++)
			ce_banktype[i] = ce_banktype[0];
	}
	for (i = (0xd00000 >> 16); i < (0xe00000 >> 16); i++)
		ce_banktype[i] = CE_MEMBANK_CHIP16;
	for (i = (0xa00000 >> 16); i < (0xc00000 >> 16); i++) {
		addrbank *b;
		ce_banktype[i] = CE_MEMBANK_CIA;
		b = &get_mem_bank (i << 16);
		if (b != &cia_bank) {
			ce_banktype[i] = CE_MEMBANK_FAST32;
			ce_cachable[i] = 1;
		}
	}
	// CD32 ROM is 16-bit
	if (currprefs.cs_cd32cd) {
		for (i = (0xe00000 >> 16); i < (0xe80000 >> 16); i++)
			ce_banktype[i] = CE_MEMBANK_FAST16;
		for (i = (0xf80000 >> 16); i <= (0xff0000 >> 16); i++)
			ce_banktype[i] = CE_MEMBANK_FAST16;
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

	size = chipmem_bank.allocated >= 0x180000 ? (chipmem_bank.allocated >> 16) : 32;
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
			map_banks (cb, 0, size, chipmem_bank.allocated);
			if ((currprefs.chipset_mask & CSMASK_ECS_AGNUS) && bogomem_bank.allocated == 0) {
				int start = chipmem_bank.allocated >> 16;
				if (chipmem_bank.allocated < 0x100000) {
					int dummy = (0x100000 - chipmem_bank.allocated) >> 16;
					map_banks (&chipmem_dummy_bank, start, dummy, 0);
					map_banks (&chipmem_dummy_bank, start + 16, dummy, 0);
				} else if (chipmem_bank.allocated < 0x200000 && chipmem_bank.allocated > 0x100000) {
					int dummy = (0x200000 - chipmem_bank.allocated) >> 16;
					map_banks (&chipmem_dummy_bank, start, dummy, 0);
				}
			}
		} else {
			map_banks (cb, 0, chipmem_bank.allocated >> 16, 0);
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
	if (!isrestore () && valid_address (regs.pc, 4))
		m68k_setpc_normal (m68k_getpc ());
}

uae_s32 getz2size (struct uae_prefs *p)
{
	ULONG start;
	start = p->fastmem_size;
	if (p->rtgmem_size && !gfxboard_is_z3 (p->rtgmem_type)) {
		while (start & (p->rtgmem_size - 1) && start < 8 * 1024 * 1024)
			start += 1024 * 1024;
		if (start + p->rtgmem_size > 8 * 1024 * 1024)
			return -1;
	}
	start += p->rtgmem_size;
	return start;
}

ULONG getz2endaddr (void)
{
	ULONG start;
	start = currprefs.fastmem_size;
	if (currprefs.rtgmem_size && !gfxboard_is_z3 (currprefs.rtgmem_type)) {
		if (!start)
			start = 0x00200000;
		while (start & (currprefs.rtgmem_size - 1) && start < 4 * 1024 * 1024)
			start += 1024 * 1024;
	}
	return start + 2 * 1024 * 1024;
}

void memory_clear (void)
{
	mem_hardreset = 0;
	if (savestate_state == STATE_RESTORE)
		return;
	if (chipmem_bank.baseaddr)
		memset (chipmem_bank.baseaddr, 0, chipmem_bank.allocated);
	if (bogomem_bank.baseaddr)
		memset (bogomem_bank.baseaddr, 0, bogomem_bank.allocated);
	if (a3000lmem_bank.baseaddr)
		memset (a3000lmem_bank.baseaddr, 0, a3000lmem_bank.allocated);
	if (a3000hmem_bank.baseaddr)
		memset (a3000hmem_bank.baseaddr, 0, a3000hmem_bank.allocated);
	expansion_clear ();
}

void memory_reset (void)
{
	int bnk, bnk_end;
	bool gayleorfatgary;

	need_hardreset = false;
	rom_write_enabled = true;
	/* Use changed_prefs, as m68k_reset is called later.  */
	if (last_address_space_24 != changed_prefs.address_space_24)
		need_hardreset = true;
	last_address_space_24 = changed_prefs.address_space_24;

	if (mem_hardreset > 2)
		memory_init ();

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

	gayleorfatgary = (currprefs.chipset_mask & CSMASK_AGA) || currprefs.cs_pcmcia || currprefs.cs_ide > 0 || currprefs.cs_mbdmac;

	init_mem_banks ();
	allocate_memory ();
	chipmem_setindirect ();

	if (mem_hardreset > 1
		|| _tcscmp (currprefs.romfile, changed_prefs.romfile) != 0
		|| _tcscmp (currprefs.romextfile, changed_prefs.romextfile) != 0)
	{
		protect_roms (false);
		write_log (_T("ROM loader.. (%s)\n"), currprefs.romfile);
		kickstart_rom = 1;
		a1000_handle_kickstart (0);
		xfree (a1000_bootrom);
		a1000_bootrom = 0;
		a1000_kickstart_mode = 0;

		memcpy (currprefs.romfile, changed_prefs.romfile, sizeof currprefs.romfile);
		memcpy (currprefs.romextfile, changed_prefs.romextfile, sizeof currprefs.romextfile);
		need_hardreset = true;
		mapped_free (extendedkickmem_bank.baseaddr);
		extendedkickmem_bank.baseaddr = NULL;
		extendedkickmem_bank.allocated = 0;
		extendedkickmem2_bank.baseaddr = NULL;
		extendedkickmem2_bank.allocated = 0;
		extendedkickmem_type = 0;
		load_extendedkickstart (currprefs.romextfile, 0);
		load_extendedkickstart (currprefs.romextfile2, EXTENDED_ROM_CDTV);
		kickmem_bank.mask = ROM_SIZE_512 - 1;
		if (!load_kickstart ()) {
			if (_tcslen (currprefs.romfile) > 0) {
				error_log (_T("Failed to open '%s'\n"), currprefs.romfile);
				notify_user (NUMSG_NOROM);
			}
			load_kickstart_replacement ();
		} else {
			struct romdata *rd = getromdatabydata (kickmem_bank.baseaddr, kickmem_bank.allocated);
			if (rd) {
				write_log (_T("Known ROM '%s' loaded\n"), rd->name);
				if ((rd->cpu & 8) && changed_prefs.cpu_model < 68030) {
					notify_user (NUMSG_KS68030PLUS);
					uae_restart (-1, NULL);
				} else if ((rd->cpu & 3) == 3 && changed_prefs.cpu_model != 68030) {
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
				if ((rd->type & (ROMTYPE_SPECIALKICK | ROMTYPE_KICK)) == ROMTYPE_KICK)
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
				write_log (_T("Unknown ROM '%s' loaded\n"), currprefs.romfile);
			}
		}
		patch_kick ();
		write_log (_T("ROM loader end\n"));
		protect_roms (true);
	}

	if ((cloanto_rom || extendedkickmem_bank.allocated) && currprefs.maprom && currprefs.maprom < 0x01000000) {
		currprefs.maprom = changed_prefs.maprom = 0x00a80000;
		if (extendedkickmem2_bank.allocated) // can't do if 2M ROM
			currprefs.maprom = changed_prefs.maprom = 0;
	}

	map_banks (&custom_bank, 0xC0, 0xE0 - 0xC0, 0);
	map_banks (&cia_bank, 0xA0, 32, 0);
	if (!currprefs.cs_a1000ram && currprefs.cs_rtc != 3)
		/* D80000 - DDFFFF not mapped (A1000 or A2000 = custom chips) */
		map_banks (&dummy_bank, 0xD8, 6, 0);

	/* map "nothing" to 0x200000 - 0x9FFFFF (0xBEFFFF if Gayle or Fat Gary) */
	bnk = chipmem_bank.allocated >> 16;
	if (bnk < 0x20 + (currprefs.fastmem_size >> 16))
		bnk = 0x20 + (currprefs.fastmem_size >> 16);
	bnk_end = gayleorfatgary ? 0xBF : 0xA0;
	map_banks (&dummy_bank, bnk, bnk_end - bnk, 0);
	if (gayleorfatgary) {
		 // a3000 or a4000 = custom chips from 0xc0 to 0xd0
		if (currprefs.cs_ide == IDE_A4000 || currprefs.cs_mbdmac)
			map_banks (&dummy_bank, 0xd0, 8, 0);
		else
			map_banks (&dummy_bank, 0xc0, 0xd8 - 0xc0, 0);
	}

	if (bogomem_bank.baseaddr) {
		int t = currprefs.bogomem_size >> 16;
		if (t > 0x1C)
			t = 0x1C;
		if (t > 0x18 && ((currprefs.chipset_mask & CSMASK_AGA) || (currprefs.cpu_model >= 68020 && !currprefs.address_space_24)))
			t = 0x18;
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
	if (currprefs.cs_rtc == 3) // A2000 clock
		map_banks (&clock_bank, 0xD8, 4, 0);
	if (currprefs.cs_rtc == 1 || currprefs.cs_rtc == 2 || currprefs.cs_cdtvram)
		map_banks (&clock_bank, 0xDC, 1, 0);
	else if (currprefs.cs_ksmirror_a8 || currprefs.cs_ide > 0 || currprefs.cs_pcmcia)
		map_banks (&clock_bank, 0xDC, 1, 0); /* none clock */
	if (currprefs.cs_fatgaryrev >= 0 || currprefs.cs_ramseyrev >= 0)
		map_banks (&mbres_bank, 0xDE, 1, 0);
#ifdef CD32
	if (currprefs.cs_cd32c2p || currprefs.cs_cd32cd || currprefs.cs_cd32nvram) {
		map_banks (&akiko_bank, AKIKO_BASE >> 16, 1, 0);
		map_banks (&gayle2_bank, 0xDD, 2, 0);
	}
#endif
#ifdef CDTV
	if (currprefs.cs_cdtvcd)
		cdtv_check_banks ();
#endif
#ifdef A2091
	if (currprefs.cs_mbdmac == 1)
		a3000scsi_reset ();
#endif

	if (a3000lmem_bank.baseaddr)
		map_banks (&a3000lmem_bank, a3000lmem_bank.start >> 16, a3000lmem_bank.allocated >> 16, 0);
	if (a3000hmem_bank.baseaddr)
		map_banks (&a3000hmem_bank, a3000hmem_bank.start >> 16, a3000hmem_bank.allocated >> 16, 0);
#ifdef CDTV
	if (cardmem_bank.baseaddr)
		map_banks (&cardmem_bank, cardmem_bank.start >> 16, cardmem_bank.allocated >> 16, 0);
#endif

	map_banks (&kickmem_bank, 0xF8, 8, 0);
	if (currprefs.maprom)
		map_banks (&kickram_bank, currprefs.maprom >> 16, extendedkickmem2_bank.allocated ? 32 : (extendedkickmem_bank.allocated ? 16 : 8), 0);
	/* map beta Kickstarts at 0x200000/0xC00000/0xF00000 */
	if (kickmem_bank.baseaddr[0] == 0x11 && kickmem_bank.baseaddr[2] == 0x4e && kickmem_bank.baseaddr[3] == 0xf9 && kickmem_bank.baseaddr[4] == 0x00) {
		uae_u32 addr = kickmem_bank.baseaddr[5];
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
		map_banks (&extendedkickmem_bank, 0xF0, extendedkickmem_bank.allocated == 2 * ROM_SIZE_512 ? 16 : 8, 0);
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
		if (extendedkickmem2_bank.allocated) {
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


	if (mem_hardreset) {
		memory_clear ();
	}
	write_log (_T("memory init end\n"));
}


void memory_init (void)
{
	init_mem_banks ();
	virtualdevice_init ();

	chipmem_bank.allocated = 0;
	bogomem_bank.allocated = 0;
	kickmem_bank.baseaddr = NULL;
	extendedkickmem_bank.baseaddr = NULL;
	extendedkickmem_bank.allocated = 0;
	extendedkickmem2_bank.baseaddr = NULL;
	extendedkickmem2_bank.allocated = 0;
	extendedkickmem_type = 0;
	chipmem_bank.baseaddr = 0;
	a3000lmem_bank.allocated = a3000hmem_bank.allocated = 0;
	a3000lmem_bank.baseaddr = a3000hmem_bank.baseaddr = NULL;
	bogomem_bank.baseaddr = NULL;
	cardmem_bank.baseaddr = NULL;
	custmem1_bank.allocated = custmem2_bank.allocated = 0;
	custmem1_bank.baseaddr = NULL;
	custmem2_bank.baseaddr = NULL;

	kickmem_bank.baseaddr = mapped_malloc (ROM_SIZE_512, _T("kick"));
	memset (kickmem_bank.baseaddr, 0, ROM_SIZE_512);
	_tcscpy (currprefs.romfile, _T("<none>"));
	currprefs.romextfile[0] = 0;

#ifdef ACTION_REPLAY
	action_replay_unload (0);
	action_replay_load ();
	action_replay_init (1);
#ifdef ACTION_REPLAY_HRTMON
	hrtmon_load ();
#endif
#endif
}

void memory_cleanup (void)
{
	mapped_free (a3000lmem_bank.baseaddr);
	mapped_free (a3000hmem_bank.baseaddr);
	mapped_free (bogomem_bank.baseaddr);
	mapped_free (kickmem_bank.baseaddr);
	xfree (a1000_bootrom);
	mapped_free (chipmem_bank.baseaddr);
#ifdef CDTV
	if (cardmem_bank.baseaddr) {
		cdtv_savecardmem (cardmem_bank.baseaddr, cardmem_bank.allocated);
		mapped_free (cardmem_bank.baseaddr);
	}
#endif
	mapped_free (custmem1_bank.baseaddr);
	mapped_free (custmem2_bank.baseaddr);

	bogomem_bank.baseaddr = NULL;
	kickmem_bank.baseaddr = NULL;
	a3000lmem_bank.baseaddr = a3000hmem_bank.baseaddr = NULL;
	a1000_bootrom = NULL;
	a1000_kickstart_mode = 0;
	chipmem_bank.baseaddr = NULL;
	cardmem_bank.baseaddr = NULL;
	custmem1_bank.baseaddr = NULL;
	custmem2_bank.baseaddr = NULL;

#ifdef ACTION_REPLAY
	action_replay_cleanup();
#endif
#ifdef ARCADIA
	arcadia_unmap ();
#endif
}

void memory_hardreset (int mode)
{
	if (mode + 1 > mem_hardreset)
		mem_hardreset = mode + 1;
}

// do not map if it conflicts with custom banks
void map_banks_cond (addrbank *bank, int start, int size, int realsize)
{
	for (int i = 0; i < MAX_CUSTOM_MEMORY_ADDRS; i++) {
		int cstart = currprefs.custom_memory_addrs[i] >> 16;
		if (!cstart)
			continue;
		int csize = currprefs.custom_memory_sizes[i] >> 16;
		if (!csize)
			continue;
		if (start <= cstart && start + size >= cstart)
			return;
		if (cstart <= start && (cstart + size >= start || start + size > cstart))
			return;
	}
	map_banks (bank, start, size, realsize);
}

static void map_banks2 (addrbank *bank, int start, int size, int realsize, int quick)
{
	int bnr, old;
	unsigned long int hioffs = 0, endhioffs = 0x100;
	addrbank *orgbank = bank;
	uae_u32 realstart = start;

	if (!quick)
		old = debug_bankchange (-1);
	flush_icache (0, 3); /* Sure don't want to keep any old mappings around! */
#ifdef NATMEM_OFFSET
	if (!quick)
		delete_shmmaps (start << 16, size << 16);
#endif

	if (!realsize)
		realsize = size << 16;

	if ((size << 16) < realsize) {
		write_log (_T("Broken mapping, size=%x, realsize=%x\nStart is %x\n"),
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
				if (!quick)
					add_shmmaps (realstart << 16, bank);
#endif
			}
			put_mem_bank (bnr << 16, bank, realstart << 16);
			real_left--;
		}
		if (!quick)
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
				if (!quick)
					add_shmmaps (realstart << 16, bank);
#endif
			}
			put_mem_bank ((bnr + hioffs) << 16, bank, realstart << 16);
			real_left--;
		}
	}
	if (!quick)
		debug_bankchange (old);
	fill_ce_banks ();
}

void map_banks (addrbank *bank, int start, int size, int realsize)
{
	map_banks2 (bank, start, size, realsize, 0);
}
void map_banks_quick (addrbank *bank, int start, int size, int realsize)
{
	map_banks2 (bank, start, size, realsize, 1);
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
	*len = chipmem_bank.allocated;
	return chipmem_bank.baseaddr;
}

uae_u8 *save_bram (int *len)
{
	*len = bogomem_bank.allocated;
	return bogomem_bank.baseaddr;
}

uae_u8 *save_a3000lram (int *len)
{
	*len = a3000lmem_bank.allocated;
	return a3000lmem_bank.baseaddr;
}

uae_u8 *save_a3000hram (int *len)
{
	*len = a3000hmem_bank.allocated;
	return a3000hmem_bank.baseaddr;
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
				write_log (_T("ROM '%s' = '%s'\n"), romn, rl[i].path);
				crcdet = 1;
			} else {
				write_log (_T("ROM '%s' = '%s' invalid rom scanner path!"), romn, rl[i].path);
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
			write_log (_T("ROM detected (path) as '%s'\n"), s);
			crcdet = 1;
		}
	}
	xfree (s);
	if (!crcdet)
		write_log (_T("WARNING: ROM '%s' %d.%d (CRC32=%08x %08x-%08x) not found!\n"),
			romn, version >> 16, version & 0xffff, crc32, mem_start, mem_start + mem_size - 1);
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
			mem_real_start = kickmem_bank.baseaddr;
			mem_size = kickmem_bank.allocated;
			path = currprefs.romfile;
			/* 256KB or 512KB ROM? */
			for (i = 0; i < mem_size / 2 - 4; i++) {
				if (longget (i + mem_start) != longget (i + mem_start + mem_size / 2))
					break;
			}
			if (i == mem_size / 2 - 4) {
				mem_size /= 2;
				mem_start += ROM_SIZE_256;
			}
			version = longget (mem_start + 12); /* version+revision */
			_stprintf (tmpname, _T("Kickstart %d.%d"), wordget (mem_start + 12), wordget (mem_start + 14));
			break;
		case 1: /* Extended ROM */
			if (!extendedkickmem_type)
				break;
			mem_start = extendedkickmem_bank.start;
			mem_real_start = extendedkickmem_bank.baseaddr;
			mem_size = extendedkickmem_bank.allocated;
			path = currprefs.romextfile;
			version = longget (mem_start + 12); /* version+revision */
			if (version == 0xffffffff)
				version = longget (mem_start + 16);
			_stprintf (tmpname, _T("Extended"));
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
	if (!addr_valid (_T("memcpyha"), dst, size))
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
	if (!addr_valid (_T("memcpyah"), src, size))
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
	dst[0] = 0;
	do {
		if (!addr_valid (_T("_tcscpyah"), src, 1))
			return res;
		b = get_byte (src++);
		*dst++ = b;
		*dst = 0;
		maxsize--;
		if (maxsize <= 1)
			break;
	} while (b);
	return res;
}
uaecptr strcpyha_safe (uaecptr dst, const uae_char *src)
{
	uaecptr res = dst;
	uae_u8 b;
	do {
		if (!addr_valid (_T("_tcscpyha"), dst, 1))
			return res;
		b = *src++;
		put_byte (dst++, b);
	} while (b);
	return res;
}
