
#include "cputest.h"
#include "cputbl_test.h"
#include "readcpu.h"
#include "disasm.h"
#include "ini.h"

#include "options.h"

#define MAX_REGISTERS 16

static uae_u32 registers[] =
{
	0x00000010,
	0x00000000,
	0xffffffff,
	0xffffff00,
	0xffff0000,
	0x80008080,
	0x7fff7fff,
	0xaaaaaaaa,

	0x00000000,
	0x00000080,
	0x00008000,
	0x00007fff,
	0xfffffffe,
	0xffffff00,
	0x00000000, // replaced with opcode memory
	0x00000000  // replaced with stack
};

// TODO: fill FPU registers
static floatx80 fpuregisters[8];
static uae_u32 fpu_fpiar, fpu_fpcr, fpu_fpsr;

const int areg_byteinc[] = { 1, 1, 1, 1, 1, 1, 1, 2 };
const int imm8_table[] = { 8, 1, 2, 3, 4, 5, 6, 7 };

int movem_index1[256];
int movem_index2[256];
int movem_next[256];
cpuop_func *cpufunctbl[65536];
struct cputbl_data
{
	uae_s16 length;
	uae_s8 disp020[2];
	uae_u8 branch;
};
static struct cputbl_data cpudatatbl[65536];

struct regstruct regs;
struct flag_struct regflags;

static int verbose = 1;
static int feature_exception3_data = 0;
static int feature_exception3_instruction = 0;
static int feature_sr_mask = 0;
static int feature_full_extension_format = 0;
static uae_u32 feature_addressing_modes[2];
static int ad8r[2], pc8r[2];

#define LOW_MEMORY_END 0x8000
#define HIGH_MEMORY_START (0x01000000 - 0x8000)

// large enough for RTD
#define STACK_SIZE (0x8000 + 8)
#define RESERVED_SUPERSTACK 1024

static uae_u32 test_low_memory_start;
static uae_u32 test_low_memory_end;
static uae_u32 test_high_memory_start;
static uae_u32 test_high_memory_end;

static uae_u8 low_memory[32768], high_memory[32768], *test_memory;
static uae_u8 low_memory_temp[32768], high_memory_temp[32768], *test_memory_temp;
static uae_u8 dummy_memory[4];
static uaecptr test_memory_start, test_memory_end, opcode_memory_start;
static uae_u32 test_memory_size;
static int hmem_rom, lmem_rom;
static uae_u8 *opcode_memory;
static uae_u8 *storage_buffer;
static char inst_name[16+1];
static int storage_buffer_watermark;
static int max_storage_buffer = 1000000;

static bool out_of_test_space;
static uaecptr out_of_test_space_addr;
static int test_exception;
static int forced_immediate_mode;
static uaecptr test_exception_addr;
static int test_exception_3_inst;
static uae_u8 imm8_cnt;
static uae_u16 imm16_cnt;
static uae_u32 imm32_cnt;
static uae_u32 addressing_mask;
static int opcodecnt;
static int cpu_stopped;
static int cpu_lvl = 0;
static int test_count;
static int testing_active;
static time_t starttime;
static int filecount;
static uae_u16 sr_undefined_mask;

struct uae_prefs currprefs;

struct accesshistory
{
	uaecptr addr;
	uae_u32 val;
	uae_u32 oldval;
	int size;
};
static int ahcnt, ahcnt2;
static int noaccesshistory = 0;

#define MAX_ACCESSHIST 32
static struct accesshistory ahist[MAX_ACCESSHIST];
static struct accesshistory ahist2[MAX_ACCESSHIST];

#define OPCODE_AREA 32

static bool valid_address(uaecptr addr, int size, int w)
{
	addr &= addressing_mask;
	size--;
	if (addr + size < LOW_MEMORY_END) {
		if (addr < test_low_memory_start)
			goto oob;
		// exception vectors needed during tests
		if ((addr + size >= 0x0c && addr < 0x30 || (addr + size >= 0x80 && addr < 0xc0)) && regs.vbr == 0)
			goto oob;
		if (addr + size >= test_low_memory_end)
			goto oob;
		if (w && lmem_rom)
			goto oob;
		return 1;
	}
	if (addr >= HIGH_MEMORY_START && addr < HIGH_MEMORY_START + 0x8000) {
		if (addr < test_high_memory_start)
			goto oob;
		if (addr + size >= test_high_memory_end)
			goto oob;
		if (w && hmem_rom)
			goto oob;
		return 1;
	}
	if (addr >= test_memory_start && addr + size < test_memory_end - RESERVED_SUPERSTACK) {
		// make sure we don't modify our test instruction
		if (testing_active && w) {
			if (addr >= opcode_memory_start && addr + size < opcode_memory_start + OPCODE_AREA)
				goto oob;
		}
		return 1;
	}
oob:
	return 0;
}

static uae_u8 *get_addr(uaecptr addr, int size, int w)
{
	// allow debug output to read memory even if oob condition
	if (w >= 0 && out_of_test_space)
		goto oob;
	if (!valid_address(addr, 1, w))
		goto oob;
	if (size > 1) {
		if (!valid_address(addr + size - 1, 1, w))
			goto oob;
	}
	addr &= addressing_mask;
	size--;
	if (addr + size < LOW_MEMORY_END) {
		return low_memory + addr;
	} else if (addr >= HIGH_MEMORY_START && addr < HIGH_MEMORY_START + 0x8000) {
		return high_memory + (addr - HIGH_MEMORY_START);
	} else if (addr >= test_memory_start && addr + size < test_memory_end - RESERVED_SUPERSTACK) {
		return test_memory + (addr - test_memory_start);
	}
oob:
	if (w >= 0) {
		if (!out_of_test_space) {
			out_of_test_space = true;
			out_of_test_space_addr = addr;
		}
	}
	dummy_memory[0] = 0;
	dummy_memory[1] = 0;
	dummy_memory[2] = 0;
	dummy_memory[3] = 0;
	return dummy_memory;
}

uae_u32 REGPARAM2 op_illg_1(uae_u32 opcode)
{
	if ((opcode & 0xf000) == 0xf000)
		test_exception = 11;
	else if ((opcode & 0xf000) == 0xa000)
		test_exception = 10;
	else
		test_exception = 4;
	return 0;
}
uae_u32 REGPARAM2 op_unimpl_1(uae_u32 opcode)
{
	return 0;
}
void REGPARAM2 op_unimpl(uae_u32 opcode)
{
	op_unimpl_1(opcode);
}
uae_u32 REGPARAM2 op_illg(uae_u32 opcode)
{
	return op_illg_1(opcode);
}

uae_u16 get_word_test_prefetch(int o)
{
	// no real prefetch
	if (cpu_lvl < 2)
		o -= 2;
	regs.irc = get_word_test(m68k_getpci() + o + 2);
	return get_word_test(m68k_getpci() + o);
}

// Move from SR does two writes to same address:
// ignore the first one
static void previoussame(uaecptr addr, int size)
{
	if (!ahcnt)
		return;
	struct accesshistory *ah = &ahist[ahcnt - 1];
	if (ah->addr == addr && ah->size == size) {
		ahcnt--;
	}
}

void put_byte_test(uaecptr addr, uae_u32 v)
{
	uae_u8 *p = get_addr(addr, 1, 1);
	if (!out_of_test_space && !noaccesshistory) {
		previoussame(addr, sz_byte);
		if (ahcnt >= MAX_ACCESSHIST) {
			wprintf(_T("ahist overflow!"));
			abort();
		}
		struct accesshistory *ah = &ahist[ahcnt++];
		ah->addr = addr;
		ah->val = v & 0xff;
		ah->oldval = *p;
		ah->size = sz_byte;
	}
	*p = v;
}
void put_word_test(uaecptr addr, uae_u32 v)
{
	if (addr & 1) {
		put_byte_test(addr + 0, v >> 8);
		put_byte_test(addr + 1, v >> 0);
	} else {
		uae_u8 *p = get_addr(addr, 2, 1);
		if (!out_of_test_space && !noaccesshistory) {
			previoussame(addr, sz_word);
			if (ahcnt >= MAX_ACCESSHIST) {
				wprintf(_T("ahist overflow!"));
				abort();
			}
			struct accesshistory *ah = &ahist[ahcnt++];
			ah->addr = addr;
			ah->val = v & 0xffff;
			ah->oldval = (p[0] << 8) | p[1];
			ah->size = sz_word;
		}
		p[0] = v >> 8;
		p[1] = v & 0xff;
	}
}
void put_long_test(uaecptr addr, uae_u32 v)
{
	if (addr & 1) {
		put_byte_test(addr + 0, v >> 24);
		put_word_test(addr + 1, v >> 8);
		put_byte_test(addr + 3, v >> 0);
	} else if (addr & 2) {
		put_word_test(addr + 0, v >> 16);
		put_word_test(addr + 2, v >> 0);
	} else {
		uae_u8 *p = get_addr(addr, 4, 1);
		if (!out_of_test_space && !noaccesshistory) {
			previoussame(addr, sz_long);
			if (ahcnt >= MAX_ACCESSHIST) {
				wprintf(_T("ahist overflow!"));
				abort();
			}
			struct accesshistory *ah = &ahist[ahcnt++];
			ah->addr = addr;
			ah->val = v;
			ah->oldval = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
			ah->size = sz_long;
		}
		p[0] = v >> 24;
		p[1] = v >> 16;
		p[2] = v >> 8;
		p[3] = v >> 0;
	}
}


static void undo_memory(struct accesshistory *ahp, int  *cntp)
{
	out_of_test_space = 0;
	int cnt = *cntp;
	noaccesshistory = 1;
	for (int i = cnt - 1; i >= 0; i--) {
		struct accesshistory *ah = &ahp[i];
		switch (ah->size)
		{
		case sz_byte:
			put_byte_test(ah->addr, ah->oldval);
			break;
		case sz_word:
			put_word_test(ah->addr, ah->oldval);
			break;
		case sz_long:
			put_long_test(ah->addr, ah->oldval);
			break;
		}
	}
	noaccesshistory = 0;
	if (out_of_test_space) {
		wprintf(_T("undo_memory out of test space fault!?\n"));
		abort();
	}
}


uae_u32 get_byte_test(uaecptr addr)
{
	uae_u8 *p = get_addr(addr, 1, 0);
	return *p;
}
uae_u32 get_word_test(uaecptr addr)
{
	if (addr & 1) {
		return (get_byte_test(addr + 0) << 8) | (get_byte_test(addr + 1) << 0);
	} else {
		uae_u8 *p = get_addr(addr, 2, 0);
		return (p[0] << 8) | (p[1]);
	}
}
uae_u32 get_long_test(uaecptr addr)
{
	if (addr & 1) {
		uae_u8 v0 = get_byte_test(addr + 0);
		uae_u16 v1 = get_word_test(addr + 1);
		uae_u8 v3 = get_byte_test(addr + 3);
		return (v0 << 24) | (v1 << 8) | (v3 << 0);
	} else if (addr & 2) {
		uae_u16 v0 = get_word_test(addr + 0);
		uae_u16 v1 = get_word_test(addr + 2);
		return (v0 << 16) | (v1 << 0);
	} else {
		uae_u8 *p = get_addr(addr, 4, 0);
		return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | (p[3]);
	}
}

uae_u32 get_byte_debug(uaecptr addr)
{
	uae_u8 *p = get_addr(addr, 1, -1);
	return *p;
}
uae_u32 get_word_debug(uaecptr addr)
{
	uae_u8 *p = get_addr(addr, 2, -1);
	return (p[0] << 8) | (p[1]);
}
uae_u32 get_long_debug(uaecptr addr)
{
	uae_u8 *p = get_addr(addr, 4, -1);
	return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | (p[3]);
}
uae_u32 get_iword_debug(uaecptr addr)
{
	return get_word_debug(addr);
}
uae_u32 get_ilong_debug(uaecptr addr)
{
	return get_long_debug(addr);
}

uae_u32 get_byte_cache_debug(uaecptr addr, bool *cached)
{
	*cached = false;
	return get_byte_test(addr);
}
uae_u32 get_word_cache_debug(uaecptr addr, bool *cached)
{
	*cached = false;
	return get_word_test(addr);
}
uae_u32 get_long_cache_debug(uaecptr addr, bool *cached)
{
	*cached = false;
	return get_long_test(addr);
}

uae_u32 sfc_nommu_get_byte(uaecptr addr)
{
	return get_byte_test(addr);
}
uae_u32 sfc_nommu_get_word(uaecptr addr)
{
	return get_word_test(addr);
}
uae_u32 sfc_nommu_get_long(uaecptr addr)
{
	return get_long_test(addr);
}

uae_u32 memory_get_byte(uaecptr addr)
{
	return get_byte_test(addr);
}
uae_u32 memory_get_word(uaecptr addr)
{
	return get_word_test(addr);
}
uae_u32 memory_get_wordi(uaecptr addr)
{
	return get_word_test(addr);
}
uae_u32 memory_get_long(uaecptr addr)
{
	return get_long_test(addr);
}
uae_u32 memory_get_longi(uaecptr addr)
{
	return get_long_test(addr);
}

void memory_put_long(uaecptr addr, uae_u32 v)
{
	put_long_test(addr, v);
}
void memory_put_word(uaecptr addr, uae_u32 v)
{
	put_word_test(addr, v);
}
void memory_put_byte(uaecptr addr, uae_u32 v)
{
	put_byte_test(addr, v);
}

uae_u8 *memory_get_real_address(uaecptr addr)
{
	return NULL;
}

uae_u32 next_iword_test(void)
{
	uae_u32 v = get_word_test_prefetch(0);
	regs.pc += 2;
	return v;
}

uae_u32 next_ilong_test(void)
{
	uae_u32 v = get_word_test_prefetch(0) << 16;
	v |= get_word_test_prefetch(2);
	regs.pc += 4;
	return v;
}

uae_u32(*x_get_long)(uaecptr);
uae_u32(*x_get_word)(uaecptr);
void (*x_put_long)(uaecptr, uae_u32);
void (*x_put_word)(uaecptr, uae_u32);

uae_u32(*x_cp_get_long)(uaecptr);
uae_u32(*x_cp_get_word)(uaecptr);
uae_u32(*x_cp_get_byte)(uaecptr);
void (*x_cp_put_long)(uaecptr, uae_u32);
void (*x_cp_put_word)(uaecptr, uae_u32);
void (*x_cp_put_byte)(uaecptr, uae_u32);
uae_u32(*x_cp_next_iword)(void);
uae_u32(*x_cp_next_ilong)(void);

uae_u32(*x_next_iword)(void);
uae_u32(*x_next_ilong)(void);
void (*x_do_cycles)(unsigned long);

uae_u32(REGPARAM3 *x_cp_get_disp_ea_020)(uae_u32 base, int idx) REGPARAM;

static int SPCFLAG_TRACE, SPCFLAG_DOTRACE;

uae_u32 get_disp_ea_test(uae_u32 base, uae_u32 dp)
{
	int reg = (dp >> 12) & 15;
	uae_s32 regd = regs.regs[reg];
	if ((dp & 0x800) == 0)
		regd = (uae_s32)(uae_s16)regd;
	return base + (uae_s8)dp + regd;
}

static void activate_trace(void)
{
	SPCFLAG_TRACE = 0;
	SPCFLAG_DOTRACE = 1;
}

static void do_trace(void)
{
	if (regs.t0 && currprefs.cpu_model >= 68020) {
		// this is obsolete
		return;
	}
	if (regs.t1) {
		activate_trace();
	}
}

void REGPARAM2 MakeSR(void)
{
	regs.sr = ((regs.t1 << 15) | (regs.t0 << 14)
		| (regs.s << 13) | (regs.m << 12) | (regs.intmask << 8)
		| (GET_XFLG() << 4) | (GET_NFLG() << 3)
		| (GET_ZFLG() << 2) | (GET_VFLG() << 1)
		| GET_CFLG());
}
void MakeFromSR_x(int t0trace)
{
	int oldm = regs.m;
	int olds = regs.s;
	int oldt0 = regs.t0;
	int oldt1 = regs.t1;

	SET_XFLG((regs.sr >> 4) & 1);
	SET_NFLG((regs.sr >> 3) & 1);
	SET_ZFLG((regs.sr >> 2) & 1);
	SET_VFLG((regs.sr >> 1) & 1);
	SET_CFLG(regs.sr & 1);

	if (regs.t1 == ((regs.sr >> 15) & 1) &&
		regs.t0 == ((regs.sr >> 14) & 1) &&
		regs.s == ((regs.sr >> 13) & 1) &&
		regs.m == ((regs.sr >> 12) & 1) &&
		regs.intmask == ((regs.sr >> 8) & 7))
		return;

	regs.t1 = (regs.sr >> 15) & 1;
	regs.t0 = (regs.sr >> 14) & 1;
	regs.s = (regs.sr >> 13) & 1;
	regs.m = (regs.sr >> 12) & 1;
	regs.intmask = (regs.sr >> 8) & 7;

	if (currprefs.cpu_model >= 68020) {
		/* 68060 does not have MSP but does have M-bit.. */
		if (currprefs.cpu_model >= 68060)
			regs.msp = regs.isp;
		if (olds != regs.s) {
			if (olds) {
				if (oldm)
					regs.msp = m68k_areg (regs, 7);
				else
					regs.isp = m68k_areg (regs, 7);
				m68k_areg (regs, 7) = regs.usp;
			} else {
				regs.usp = m68k_areg (regs, 7);
				m68k_areg (regs, 7) = regs.m ? regs.msp : regs.isp;
			}
		} else if (olds && oldm != regs.m) {
			if (oldm) {
				regs.msp = m68k_areg (regs, 7);
				m68k_areg (regs, 7) = regs.isp;
			} else {
				regs.isp = m68k_areg (regs, 7);
				m68k_areg (regs, 7) = regs.msp;
			}
		}
		if (currprefs.cpu_model >= 68060)
			regs.t0 = 0;
	} else {
		regs.t0 = regs.m = 0;
		if (olds != regs.s) {
			if (olds) {
				regs.isp = m68k_areg (regs, 7);
				m68k_areg (regs, 7) = regs.usp;
			} else {
				regs.usp = m68k_areg (regs, 7);
				m68k_areg (regs, 7) = regs.isp;
			}
		}
	}

	if (regs.t1 || regs.t0) {
		SPCFLAG_TRACE = 1;
	} else {
		/* Keep SPCFLAG_DOTRACE, we still want a trace exception for
		SR-modifying instructions (including STOP).  */
		SPCFLAG_TRACE = 0;
	}
	// Stop SR-modification does not generate T0
	// If this SR modification set Tx bit, no trace until next instruction.
	if ((oldt0 && t0trace && currprefs.cpu_model >= 68020) || oldt1) {
		// Always trace if Tx bits were already set, even if this SR modification cleared them.
		activate_trace();
	}

}

void REGPARAM2 MakeFromSR_T0(void)
{
	MakeFromSR_x(1);
}
void REGPARAM2 MakeFromSR(void)
{
	MakeFromSR_x(0);
}

static void exception_check_trace(int nr)
{
	SPCFLAG_TRACE = 0;
	SPCFLAG_DOTRACE = 0;
	if (regs.t1 && !regs.t0) {
		/* trace stays pending if exception is div by zero, chk,
		* trapv or trap #x
		*/
		if (nr == 5 || nr == 6 || nr == 7 || (nr >= 32 && nr <= 47))
			SPCFLAG_DOTRACE = 1;
	}
	regs.t1 = regs.t0 = 0;
}

void m68k_setstopped(void)
{
	/* A traced STOP instruction drops through immediately without
	actually stopping.  */
	if (SPCFLAG_DOTRACE == 0) {
		cpu_stopped = 1;
	} else {
		cpu_stopped = 0;
	}
}

void check_t0_trace(void)
{
	if (regs.t0 && currprefs.cpu_model >= 68020) {
		SPCFLAG_TRACE = 0;
		SPCFLAG_DOTRACE = 1;
	}
}

void cpureset(void)
{
	test_exception = 1;
}

void exception3_read(uae_u32 opcode, uae_u32 addr)
{
	test_exception = 3;
	test_exception_3_inst = 0;
	test_exception_addr = addr;
}
void exception3_write(uae_u32 opcode, uae_u32 addr)
{
	test_exception = 3;
	test_exception_3_inst = 0;
	test_exception_addr = addr;
}
void REGPARAM2 Exception(int n)
{
	test_exception = n;
	test_exception_3_inst = 0;
	test_exception_addr = m68k_getpci();
}
void REGPARAM2 Exception_cpu(int n)
{
	test_exception = n;
	test_exception_3_inst = 0;
	test_exception_addr = m68k_getpci();

	bool t0 = currprefs.cpu_model >= 68020 && regs.t0;
	// check T0 trace
	if (t0) {
		activate_trace();
	}
}
void exception3i(uae_u32 opcode, uaecptr addr)
{
	test_exception = 3;
	test_exception_3_inst = 1;
	test_exception_addr = addr;
}
void exception3b(uae_u32 opcode, uaecptr addr, bool w, bool i, uaecptr pc)
{
	test_exception = 3;
	test_exception_3_inst = i;
	test_exception_addr = addr;
}

int cctrue(int cc)
{
	uae_u32 cznv = regflags.cznv;

	switch (cc) {
	case 0:  return 1;											/*					T  */
	case 1:  return 0;											/*					F  */
	case 2:  return (cznv & (FLAGVAL_C | FLAGVAL_Z)) == 0;		/* !CFLG && !ZFLG	HI */
	case 3:  return (cznv & (FLAGVAL_C | FLAGVAL_Z)) != 0;		/*  CFLG || ZFLG	LS */
	case 4:  return (cznv & FLAGVAL_C) == 0;					/* !CFLG			CC */
	case 5:  return (cznv & FLAGVAL_C) != 0;					/*  CFLG			CS */
	case 6:  return (cznv & FLAGVAL_Z) == 0;					/* !ZFLG			NE */
	case 7:  return (cznv & FLAGVAL_Z) != 0;					/*  ZFLG			EQ */
	case 8:  return (cznv & FLAGVAL_V) == 0;					/* !VFLG			VC */
	case 9:  return (cznv & FLAGVAL_V) != 0;					/*  VFLG			VS */
	case 10: return (cznv & FLAGVAL_N) == 0;					/* !NFLG			PL */
	case 11: return (cznv & FLAGVAL_N) != 0;					/*  NFLG			MI */

	case 12: /*  NFLG == VFLG		GE */
		return ((cznv >> FLAGBIT_N) & 1) == ((cznv >> FLAGBIT_V) & 1);
	case 13: /*  NFLG != VFLG		LT */
		return ((cznv >> FLAGBIT_N) & 1) != ((cznv >> FLAGBIT_V) & 1);
	case 14: /* !GET_ZFLG && (GET_NFLG == GET_VFLG);  GT */
		return !(cznv & FLAGVAL_Z) && (((cznv >> FLAGBIT_N) & 1) == ((cznv >> FLAGBIT_V) & 1));
	case 15: /* GET_ZFLG || (GET_NFLG != GET_VFLG);   LE */
		return (cznv & FLAGVAL_Z) || (((cznv >> FLAGBIT_N) & 1) != ((cznv >> FLAGBIT_V) & 1));
	}
	return 0;
}

static uae_u32 xorshiftstate;
static uae_u32 xorshift32(void)
{
	uae_u32 x = xorshiftstate;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	xorshiftstate = x;
	return xorshiftstate;
}

static int rand16_cnt;
static uae_u16 rand16(void)
{
	int cnt = rand16_cnt & 15;
	uae_u16 v = 0;
	rand16_cnt++;
	if (cnt < 15) {
		v = (uae_u16)xorshift32();
		if (cnt <= 6)
			v &= ~0x8000;
		else
			v |= 0x8000;
	}
	if (forced_immediate_mode == 1)
		v &= ~1;
	if (forced_immediate_mode == 2)
		v |= 1;
	return v;
}
static int rand32_cnt;
static uae_u32 rand32(void)
{
	int cnt = rand32_cnt & 31;
	uae_u32 v = 0;
	rand32_cnt++;
	if (cnt < 31) {
		v = xorshift32();
		if (cnt <= 14)
			v &= ~0x80000000;
		else
			v |= 0x80000000;
	}
	if (forced_immediate_mode == 1)
		v &= ~1;
	if (forced_immediate_mode == 2)
		v |= 1;
	return v;
}

// first 3 values: positive
// next 4 values: negative
// last: zero
static int rand8_cnt;
static uae_u8 rand8(void)
{
	int cnt = rand8_cnt & 7;
	uae_u8 v = 0;
	rand8_cnt++;
	if (cnt < 7) {
		v = (uae_u8)xorshift32();
		if (cnt <= 2)
			v &= ~0x80;
		else
			v |= 0x80;
	}
	if (forced_immediate_mode == 1)
		v &= ~1;
	if (forced_immediate_mode == 2)
		v |= 1;
	return v;
}

static uae_u8 frand8(void)
{
	return (uae_u8)xorshift32();
}

static void fill_memory_buffer(uae_u8 *p, int size)
{
	for (int i = 0; i < size; i++) {
		p[i] = frand8();
	}
	// fill extra zeros
	for (int i = 0; i < size; i++) {
		if (frand8() < 0x70)
			p[i] = 0x00;
	}
}

static void fill_memory(void)
{
	fill_memory_buffer(low_memory_temp, 32768);
	fill_memory_buffer(high_memory_temp, 32768);
	fill_memory_buffer(test_memory_temp, test_memory_size);
}

static void save_memory(const TCHAR *path, const TCHAR *name, uae_u8 *p, int size)
{
	TCHAR fname[1000];
	_stprintf(fname, _T("%s%s"), path, name);
	FILE *f = _tfopen(fname, _T("wb"));
	if (!f) {
		wprintf(_T("Couldn't open '%s'\n"), fname);
		abort();
	}
	fwrite(p, 1, size, f);
	fclose(f);
}



static uae_u8 *store_rel(uae_u8 *dst, uae_u8 mode, uae_u32 s, uae_u32 d)
{
	int diff = (uae_s32)d - (uae_s32)s;
	if (diff == 0)
		return dst;
	if (diff >= -128 && diff < 128) {
		*dst++ = mode | CT_RELATIVE_START_BYTE;
		*dst++ = diff & 0xff;
	} else if (diff >= -32768 && diff < 32767) {
		*dst++ = mode | CT_RELATIVE_START_WORD;
		*dst++ = (diff >> 8) & 0xff;
		*dst++ = diff & 0xff;
	} else if (d < LOW_MEMORY_END) {
		*dst++ = mode | CT_ABSOLUTE_WORD;
		*dst++ = (d >> 8) & 0xff;
		*dst++ = d & 0xff;
	} else if ((d & ~addressing_mask) == ~addressing_mask && (d & addressing_mask) >= HIGH_MEMORY_START) {
		*dst++ = mode | CT_ABSOLUTE_WORD;
		*dst++ = (d >> 8) & 0xff;
		*dst++ = d & 0xff;
	} else {
		*dst++ = mode | CT_ABSOLUTE_LONG;
		*dst++ = (d >> 24) & 0xff;
		*dst++ = (d >> 16) & 0xff;
		*dst++ = (d >> 8) & 0xff;
		*dst++ = d & 0xff;
	}
	return dst;
}

static uae_u8 *store_fpureg(uae_u8 *dst, uae_u8 mode, floatx80 d)
{
	*dst++ = mode | CT_SIZE_FPU;
	*dst++ = (d.high >> 8) & 0xff;
	*dst++ = (d.high >> 0) & 0xff;
	*dst++ = (d.low >> 56) & 0xff;
	*dst++ = (d.low >> 48) & 0xff;
	*dst++ = (d.low >> 40) & 0xff;
	*dst++ = (d.low >> 32) & 0xff;
	*dst++ = (d.low >> 24) & 0xff;
	*dst++ = (d.low >> 16) & 0xff;
	*dst++ = (d.low >>  8) & 0xff;
	*dst++ = (d.low >>  0) & 0xff;
	return dst;
}

static uae_u8 *store_reg(uae_u8 *dst, uae_u8 mode, uae_u32 s, uae_u32 d, int size)
{
	if (s == d && size < 0)
		return dst;
	if (((s & 0xffffff00) == (d & 0xffffff00) && size < 0) || size == sz_byte) {
		*dst++ = mode | CT_SIZE_BYTE;
		*dst++ = d & 0xff;
	} else if (((s & 0xffff0000) == (d & 0xffff0000) && size < 0) || size == sz_word) {
		*dst++ = mode | CT_SIZE_WORD;
		*dst++ = (d >> 8) & 0xff;
		*dst++ = d & 0xff;
	} else {
		*dst++ = mode | CT_SIZE_LONG;
		*dst++ = (d >> 24) & 0xff;
		*dst++ = (d >> 16) & 0xff;
		*dst++ = (d >> 8) & 0xff;
		*dst++ = d & 0xff;
	}
	return dst;
}

static uae_u8 *store_mem_bytes(uae_u8 *dst, uaecptr start, int len, uae_u8 *old)
{
	if (!len)
		return dst;
	if (len > 32) {
		wprintf(_T("too long byte count!\n"));
		abort();
	}
	uaecptr oldstart = start;
	uae_u8 offset = 0;
	// start
	for (int i = 0; i < len; i++) {
		uae_u8 v = get_byte_test(start);
		if (v != *old)
			break;
		start++;
		old++;
	}
	// end
	offset = start - oldstart;
	if (offset > 7) {
		start -= (offset - 7);
		offset = 7;
	}
	len -= offset;
	for (int i = len - 1; i >= 0; i--) {
		uae_u8 v = get_byte_test(start + i);
		if (v != old[i])
			break;
		len--;
	}
	if (!len)
		return dst;
	*dst++ = CT_MEMWRITES | CT_PC_BYTES;
	*dst++ = (offset << 5) | (uae_u8)(len == 32 ? 0 : len);
	for (int i = 0; i < len; i++) {
		*dst++ = get_byte_test(start);
		start++;
	}
	return dst;
}

static uae_u8 *store_mem(uae_u8 *dst, int storealways)
{
	if (ahcnt == 0)
		return dst;
	for (int i = 0; i < ahcnt; i++) {
		struct accesshistory *ah = &ahist[i];
		if (ah->oldval == ah->val && !storealways)
 			continue;
		uaecptr addr = ah->addr;
		addr &= addressing_mask;
 		if (addr < LOW_MEMORY_END) {
			*dst++ = CT_MEMWRITE | CT_ABSOLUTE_WORD;
			*dst++ = (addr >> 8) & 0xff;
			*dst++ = addr & 0xff;
		} else if (addr >= HIGH_MEMORY_START) {
			*dst++ = CT_MEMWRITE | CT_ABSOLUTE_WORD;
			*dst++ = (addr >> 8) & 0xff;
			*dst++ = addr & 0xff;
		} else if (addr >= opcode_memory_start && addr < opcode_memory_start + 32768) {
			*dst++ = CT_MEMWRITE | CT_RELATIVE_START_WORD;
			uae_u16 diff = addr - opcode_memory_start;
			*dst++ = (diff >> 8) & 0xff;
			*dst++ = diff & 0xff;
		} else if (addr < opcode_memory_start && addr >= opcode_memory_start - 32768) {
			*dst++ = CT_MEMWRITE | CT_RELATIVE_START_WORD;
			uae_u16 diff = addr - opcode_memory_start;
			*dst++ = (diff >> 8) & 0xff;
			*dst++ = diff & 0xff;
		} else {
			*dst++ = CT_MEMWRITE | CT_ABSOLUTE_LONG;
			*dst++ = (addr >> 24) & 0xff;
			*dst++ = (addr >> 16) & 0xff;
			*dst++ = (addr >> 8) & 0xff;
			*dst++ = addr & 0xff;
		}
		dst = store_reg(dst, CT_MEMWRITE, 0, ah->oldval, ah->size);
		dst = store_reg(dst, CT_MEMWRITE, 0, ah->val, ah->size);
	}
	return dst;
}


static void pl(uae_u8 *p, uae_u32 v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v >> 0;
}

static bool load_file(const TCHAR *path, const TCHAR *file, uae_u8 *p, int size, int offset)
{
	TCHAR fname[1000];
	if (path) {
		_stprintf(fname, _T("%s%s"), path, file);
	} else {
		_tcscpy(fname, file);
	}
	FILE *f = _tfopen(fname, _T("rb"));
	if (!f)
		return false;
	if (offset < 0) {
		fseek(f, -size, SEEK_END);
	} else {
		fseek(f, offset, SEEK_SET);
	}
	int lsize = fread(p, 1, size, f);
	if (lsize != size) {
		wprintf(_T("Couldn't read file '%s'\n"), fname);
		exit(0);
	}
	fclose(f);
	return true;
}

static void save_data(uae_u8 *dst, const TCHAR *dir)
{
	TCHAR path[1000];

	if (dst == storage_buffer)
		return;

	if (dst - storage_buffer > max_storage_buffer) {
		wprintf(_T("data buffer overrun!\n"));
		abort();
	}

	_wmkdir(dir);
	_stprintf(path, _T("%s/%04d.dat"), dir, filecount);
	FILE *f = _tfopen(path, _T("wb"));
	if (!f) {
		wprintf(_T("couldn't open '%s'\n"), path);
		abort();
	}
	if (filecount == 0) {
		uae_u8 data[4];
		pl(data, 0x00000001);
		fwrite(data, 1, 4, f);
		pl(data, (uae_u32)starttime);
		fwrite(data, 1, 4, f);
		pl(data, (hmem_rom << 16) | lmem_rom);
		fwrite(data, 1, 4, f);
		pl(data, test_memory_start);
		fwrite(data, 1, 4, f);
		pl(data, test_memory_size);
		fwrite(data, 1, 4, f);
		pl(data, opcode_memory_start - test_memory_start);
		fwrite(data, 1, 4, f);
		pl(data, (cpu_lvl << 16) | sr_undefined_mask);
		fwrite(data, 1, 4, f);
		pl(data, currprefs.fpu_model);
		fwrite(data, 1, 4, f);
		fwrite(inst_name, 1, sizeof(inst_name) - 1, f);
		fclose(f);
		filecount++;
		save_data(dst, dir);
	} else {
		uae_u8 data[4];
		pl(data, 0x00000001);
		fwrite(data, 1, 4, f);
		pl(data, (uae_u32)starttime);
		fwrite(data, 1, 4, f);
		pl(data, 0);
		fwrite(data, 1, 4, f);
		fwrite(data, 1, 4, f);
		*dst++ = CT_END_FINISH;
		fwrite(storage_buffer, 1, dst - storage_buffer, f);
		fclose(f);
		filecount++;
	}
}

static int full_format_cnt;

static uaecptr putfpuimm(uaecptr pc, int opcodesize, int *isconstant)
{
	// TODO: generate FPU immediates
	switch (opcodesize)
	{
	case 0: // L
		put_long_test(pc, rand32());
		pc += 4;
		break;
	case 4: // W
		put_word_test(pc, rand16());
		pc += 2;
		break;
	case 6: // B
		put_word_test(pc, rand16());
		pc += 2;
		break;
	case 1: // S
		put_long(pc, 0);
		pc += 4;
		break;
	case 2: // X
		put_long(pc, 0);
		put_long(pc + 4, 0);
		put_long(pc + 8, 0);
		pc += 12;
		break;
	case 3: // P
		put_long(pc, 0);
		put_long(pc + 4, 0);
		put_long(pc + 8, 0);
		pc += 12;
		break;
	case 5: // D
		put_long(pc, 0);
		put_long(pc + 4, 0);
		pc += 8;
		break;
	}
	return pc;
}

// generate mostly random EA.
static int create_ea(uae_u16 *opcodep, uaecptr pc, int mode, int reg, struct instr *dp, int *isconstant, int srcdst, int fpuopcode, int opcodesize)
{
	uaecptr old_pc = pc;
	uae_u16 opcode = *opcodep;
	int am = mode >= imm ? imm : mode;

	if (!((1 << am) & feature_addressing_modes[srcdst]))
		return -1;

	switch (mode)
	{
	case Dreg:
	case Areg:
	case Aind:
	case Aipi:
	case Apdi:
		break;
	case Ad16:
	case PC16:
		put_word_test(pc, rand16());
		*isconstant = 16;
		pc += 2;
		break;
	case Ad8r:
	case PC8r:
	{
		uae_u16 v = rand16();
		if (!feature_full_extension_format)
			v &= ~0x100;
		if (mode == Ad8r) {
			if ((ad8r[srcdst] & 3) == 1)
				v &= ~0x100;
			if ((ad8r[srcdst] & 3) == 2)
				v |= 0x100;
		} else if (mode == PC8r) {
			if ((pc8r[srcdst] & 3) == 1)
				v &= ~0x100;
			if ((pc8r[srcdst] & 3) == 2)
				v |= 0x100;
		}
		if (currprefs.cpu_model < 68020 || (v & 0x100) == 0) {
			*isconstant = 16;
			put_word_test(pc, v);
			pc += 2;
		} else {
			// full format extension
			// attempt to generate every possible combination,
			// one by one.
			for (;;) {
				v &= 0xff00;
				v |= full_format_cnt & 0xff;
				// skip reserved combinations for now
				int is = (v >> 6) & 1;
				int iis = v & 7;
				int sz = (v >> 4) & 3;
				int bit3 = (v >> 3) & 1;
				if ((is && iis >= 4) || iis == 4 || sz == 0 || bit3 == 1) {
					full_format_cnt++;
					continue;
				}
				break;
			}
			put_word_test(pc, v);
			uaecptr pce = pc;
			pc += 2;
			// calculate lenght of extension
			ShowEA_disp(&pce, mode == Ad8r ? regs.regs[reg + 8] : pce, NULL, NULL);
			while (pc < pce) {
				v = rand16();
				put_word_test(pc, v);
				pc += 2;
			}
			*isconstant = 32;
		}
		break;
	}
	case absw:
		put_word_test(pc, rand16());
		*isconstant = 16;
		pc += 2;
		break;
	case absl:
		put_long_test(pc, rand32());
		*isconstant = 32;
		pc += 4;
		break;
	case imm:
		if (fpuopcode >= 0 && opcodesize < 8) {
			pc = putfpuimm(pc, opcodesize, isconstant);
		} else {
			if (dp->size == sz_long) {
				put_long_test(pc, rand32());
				*isconstant = 32;
				pc += 4;
			}
			else {
				put_word_test(pc, rand16());
				*isconstant = 16;
				pc += 2;
			}
		}
		break;
	case imm0:
	{
		// byte immediate but randomly fill also upper byte
		uae_u16 v = rand16();
		if ((imm8_cnt & 3) == 0)
			v &= 0xff;
		imm8_cnt++;
		put_word_test(pc, v);
		*isconstant = 16;
		pc += 2;
		break;
	}
	case imm1:
	{
		// word immediate
		if (fpuopcode >= 0) {
			uae_u16 v = 0;
			if (opcodesize == 7) {
				// FMOVECR
				v = 0x4000;
				v |= opcodesize << 10;
				v |= imm16_cnt & 0x3ff;
				imm16_cnt++;
				if ((opcode & 0x3f) != 0)
					return -1;
				*isconstant = 0;
				if (imm16_cnt < 0x400)
					*isconstant = -1;
			} else if (opcodesize == 8) {
				// FMOVEM/FMOVE to/from control registers
				v |= 0x8000;
				v |= (imm16_cnt & 15) << 11;
				v |= rand16() & 0x07ff;
				imm16_cnt++;
				if (imm16_cnt >= 32)
					*isconstant = 0;
				else
					*isconstant = -1;
			} else {
				v |= fpuopcode;
				if (imm16_cnt & 1) {
					// EA to FP reg
					v |= 0x4000;
					v |= opcodesize << 10;
					imm16_cnt++;
				} else {
					// FP reg to FP reg
					v |= ((imm16_cnt >> 1) & 7) << 10;
					// clear mode/reg field
					opcode &= ~0x3f;
					imm16_cnt++;
					if (opcodesize != 2) {
						// not X: skip
						return -2;
					}
				}
				*isconstant = 16;
			}
			put_word_test(pc, v);
			pc += 2;
		} else {
			if (opcodecnt == 1) {
				// STOP #xxxx: test all combinations
				// (also includes RTD)
				put_word_test(pc, imm16_cnt++);
				if (imm16_cnt == 0)
					*isconstant = 0;
				else
					*isconstant = -1;
			} else {
				uae_u16 v = rand16();
				if ((imm16_cnt & 7) == 0)
					v &= 0x00ff;
				if ((imm16_cnt & 15) == 0)
					v &= 0x000f;
				imm16_cnt++;
				put_word_test(pc, v);
				*isconstant = 16;
			}
			pc += 2;
		}
		break;
	}
	case imm2:
	{
		// long immediate
		uae_u32 v = rand32();
		if ((imm32_cnt & 7) == 0)
			v &= 0x0000ffff;
		imm32_cnt++;
		put_long_test(pc, v);
		if (imm32_cnt < 256)
			*isconstant = -1;
		else
			*isconstant = 32;
		pc += 4;
		break;
	}
	}
	*opcodep = opcode;
	return pc - old_pc;
}

static int imm_special;

static int handle_specials_branch(uae_u16 opcode, uaecptr pc, struct instr *dp, int *isconstant)
{
	// 68020 BCC.L is BCC.B to odd address if 68000/010
	if ((opcode & 0xf0ff) == 0x60ff) {
		if (currprefs.cpu_model >= 68020) {
			return 0;
		}
		return -2;
	}
	return 0;
}

static int handle_specials_stack(uae_u16 opcode, uaecptr pc, struct instr *dp, int *isconstant)
{
	int offset = 0;
	if (opcode == 0x4e73 || opcode == 0x4e74 || opcode == 0x4e75 || opcode == 0x4e77) {
		uae_u32 v;
		uaecptr addr = regs.regs[8 + 7];
		imm_special++;
		// RTE, RTD, RTS and RTR
		if (opcode == 0x4e77) {
			// RTR
			v = imm_special;
			uae_u16 ccr = v & 31;
			ccr |= rand16() & ~31;
			put_word_test(addr, ccr);
			addr += 2;
			offset += 2;
			*isconstant = imm_special >= (1 << (0 + 5)) * 4 ? 0 : -1;
		} else if (opcode == 0x4e77) {
			// RTD
			v = imm_special >> 2;
			uae_u16 sr = v & 31;
			sr |= (v >> 5) << 12;
			put_word_test(addr, sr);
			addr += 2;
			offset += 2;
			*isconstant = imm_special >= (1 << (4 + 5)) * 4 ? 0 : -1;
		} else if (opcode == 0x4e73) {
			// RTE
			if (currprefs.cpu_model == 68000) {
				v = imm_special >> 2;
				uae_u16 sr = v & 31;
				sr |= (v >> 5) << 12;
				put_word_test(addr, sr);
				addr += 2;
				offset += 2;
			} else {
				// TODO 68010+ RTE
			}
			*isconstant = imm_special >= (1 << (4 + 5)) * 4 ? 0 : -1;
		} else if (opcode == 0x4e75) {
			// RTS
			*isconstant = imm_special >= 256 ? 0 : -1;
		}
		v = rand32();
		switch (imm_special & 3)
		{
		case 0:
		case 3:
			v = opcode_memory_start + 128;
			break;
		case 1:
			v &= 0xffff;
			break;
		case 2:
			v = opcode_memory_start + (uae_s16)v;
			break;
		}
		put_long_test(addr, v);
		if (out_of_test_space) {
			wprintf(_T("handle_specials out of bounds access!?"));
			abort();
		}
	}
	return offset;
}

static void execute_ins(uae_u16 opc)
{
	uae_u16 opw1 = (opcode_memory[2] << 8) | (opcode_memory[3] << 0);
	uae_u16 opw2 = (opcode_memory[4] << 8) | (opcode_memory[5] << 0);
	if (opc == 0x6100
		&& opw1 == 0x001e
//		&& opw2 == 0x2770
		)
		printf("");

	// execute instruction
	SPCFLAG_TRACE = 0;
	SPCFLAG_DOTRACE = 0;

	MakeFromSR();

	testing_active = 1;

	if (SPCFLAG_TRACE)
		do_trace();

	(*cpufunctbl[opc])(opc);

	if (!test_exception) {
		if (SPCFLAG_DOTRACE)
			Exception(9);

		if (cpu_stopped && regs.s == 0 && currprefs.cpu_model <= 68010) {
			// 68000/68010 undocumented special case:
			// if STOP clears S-bit and T was not set:
			// cause privilege violation exception, PC pointing to following instruction.
			// If T was set before STOP: STOP works as documented.
			cpu_stopped = 0;
			Exception(8);
		}
	}

	testing_active = 0;

	if (regs.s) {
		regs.regs[15] = regs.usp;
	}
}

// any instruction that can branch execution
static int isbranchinst(struct instr *dp)
{
	switch (dp->mnemo)
	{
	case i_Bcc:
	case i_BSR:
	case i_JMP:
	case i_JSR:
		return 1;
	case i_RTS:
	case i_RTR:
	case i_RTD:
	case i_RTE:
		return 2;
	case i_DBcc:
	case i_FBcc:
		return -1;
	case i_FDBcc:
		return 1;
	}
	return 0;
}

// only test instructions that can have
// special trace behavior
static int is_test_trace(struct instr *dp)
{
	if (isbranchinst(dp))
		return 1;
	switch (dp->mnemo)
	{
	case i_STOP:
	case i_MV2SR:
	case i_MVSR2:
		return 1;
	}
	return 0;
}

static int isunsupported(struct instr *dp)
{
	switch (dp->mnemo)
	{
	case i_MOVE2C:
	case i_MOVEC2:
		return 1;
	case i_RTE:
		if (cpu_lvl > 0)
			return 1;
		break;
	}
	return 0;

}

static const TCHAR *sizes[] = { _T("B"), _T("W"), _T("L") };

static void test_mnemo(const TCHAR *path, const TCHAR *mnemo, int opcodesize, int fpuopcode)
{
	TCHAR dir[1000];
	uae_u8 *dst = NULL;
	int mn;
	int size;
	bool fpumode = 0;

	uae_u32 test_cnt = 0;

	if (mnemo == NULL || mnemo[0] == 0)
		return;

	size = 3;
	if (fpuopcode < 0) {
		if (opcodesize == 0)
			size = 2;
		else if (opcodesize == 4)
			size = 1;
		else if (opcodesize == 6)
			size = 0;
	}

	xorshiftstate = 1;
	filecount = 0;

	struct mnemolookup *lookup = NULL;
	for (int i = 0; lookuptab[i].name; i++) {
		lookup = &lookuptab[i];
		if (!_tcsicmp(lookup->name, mnemo) || (lookup->friendlyname && !_tcsicmp(lookup->friendlyname, mnemo)))
			break;
	}
	if (!lookup) {
		wprintf(_T("'%s' not found.\n"), mnemo);
		return;
	}
	mn = lookup->mnemo;
	const TCHAR *mns = lookup->name;
	if (fpuopcode >= 0) {
		mns = fpuopcodes[fpuopcode];
		if (opcodesize == 7)
			mns = _T("FMOVECR");
		if (opcodesize == 8)
			mns = _T("FMOVEM");
	}

	int pathlen = _tcslen(path);
	_stprintf(dir, _T("%s%s"), path, mns);
	if (fpuopcode < 0) {
		if (size < 3) {
			_tcscat(dir, _T("."));
			_tcscat(dir, sizes[size]);
		}
	} else {
		_tcscat(dir, _T("."));
		_tcscat(dir, fpsizes[opcodesize < 7 ? opcodesize : 2]);
	}
	memset(inst_name, 0, sizeof(inst_name));
	ua_copy(inst_name, sizeof(inst_name), dir + pathlen);

	opcodecnt = 0;
	for (int opcode = 0; opcode < 65536; opcode++) {
		struct instr *dp = table68k + opcode;
		// match requested mnemonic
		if (dp->mnemo != lookup->mnemo)
			continue;
		// match requested size
		if ((size >= 0 && size <= 2) && (size != dp->size || dp->unsized))
			continue;
		if (size == 3 && !dp->unsized)
			continue;
		// skip all unsupported instructions if not specifically testing i_ILLG
		if (dp->clev > cpu_lvl && lookup->mnemo != i_ILLG)
			continue;
		opcodecnt++;
		if (isunsupported(dp))
			return;
		fpumode = currprefs.fpu_model && (opcode & 0xf000) == 0xf000;
	}

	if (!opcodecnt)
		return;

	wprintf(_T("%s\n"), dir);

	int quick = 0;
	int rounds = 4;
	int subtest_count = 0;

	int count = 0;

	registers[8 + 6] = opcode_memory_start - 0x100;
	registers[8 + 7] = test_memory_end - STACK_SIZE;

	uae_u32 cur_registers[MAX_REGISTERS];
	for (int i = 0; i < MAX_REGISTERS; i++) {
		cur_registers[i] = registers[i];
	}
	floatx80 cur_fpuregisters[MAX_REGISTERS];
	for (int i = 0; i < 8; i++) {
		cur_fpuregisters[i] = fpuregisters[i];
	}

	dst = storage_buffer;

	memcpy(low_memory, low_memory_temp, 32768);
	memcpy(high_memory, high_memory_temp, 32768);
	memcpy(test_memory, test_memory_temp, test_memory_size);

	full_format_cnt = 0;

	int sr_override = 0;

	for (;;) {

		if (quick)
			break;

		for (int i = 0; i < MAX_REGISTERS; i++) {
			dst = store_reg(dst, CT_DREG + i, 0, cur_registers[i], -1);
			regs.regs[i] = cur_registers[i];
		}
		for (int i = 0; i < 8; i++) {
			dst = store_fpureg(dst, CT_FPREG + i, cur_fpuregisters[i]);
			regs.fp[i].fpx = cur_fpuregisters[i];
		}

		for (int opcode = 0; opcode < 65536; opcode++) {

			struct instr *dp = table68k + opcode;
			// match requested mnemonic
			if (dp->mnemo != lookup->mnemo)
				continue;

			// match requested size
			if ((size >= 0 && size <= 2) && (size != dp->size || dp->unsized))
				continue;
			if (size == 3 && !dp->unsized)
				continue;
			// skip all unsupported instructions if not specifically testing i_ILLG
			if (dp->clev > cpu_lvl && lookup->mnemo != i_ILLG)
				continue;

			int extra_loops = 3;
			while (extra_loops-- > 0) {

				// force both odd and even immediate values
				// for better address error testing
				forced_immediate_mode = 0;
				if ((currprefs.cpu_model == 68000 || currprefs.cpu_model == 68010) && (feature_exception3_data == 1 || feature_exception3_instruction == 1)) {
					if (dp->size > 0) {
						if (extra_loops == 1)
							forced_immediate_mode = 1;
						if (extra_loops == 0)
							forced_immediate_mode = 2;
					} else {
						extra_loops = 0;
					}
				} else if (currprefs.cpu_model == 68020 && ((ad8r[0] & 3) == 2 || (pc8r[0] & 3) == 2 || (ad8r[1] & 3) == 2 || (pc8r[1] & 3) == 2)) {
					; // if only 68020+ addressing modes: do extra loops
				} else {
					extra_loops = 0;
				}

				imm8_cnt = 0;
				imm16_cnt = 0;
				imm32_cnt = 0;
				imm_special = 0;

				// retry few times if out of bounds access
				int oob_retries = 10;
				// if instruction has immediate(s), repeat instruction test multiple times
				// each round generates new random immediate(s)
				int constant_loops = 32;
				while (constant_loops-- > 0) {
					uae_u8 oldbytes[OPCODE_AREA];
					memcpy(oldbytes, opcode_memory, sizeof(oldbytes));

					uae_u16 opc = opcode;
					int isconstant_src = 0;
					int isconstant_dst = 0;
					int did_out_of_bounds = 0;
					uae_u8 *prev_dst2 = dst;

					out_of_test_space = 0;
					ahcnt = 0;

					if (opc == 0xf228)
						printf("");
					if (subtest_count == 1568)
						printf("");


					uaecptr pc = opcode_memory_start + 2;

					// create source addressing mode
					if (dp->suse) {
						int o = create_ea(&opc, pc, dp->smode, dp->sreg, dp, &isconstant_src, 0, fpuopcode, opcodesize);
						if (o < 0) {
							memcpy(opcode_memory, oldbytes, sizeof(oldbytes));
							if (o == -1)
								goto nextopcode;
							continue;
						}
						pc += o;
					}

					uae_u8 *ao = opcode_memory + 2;
					uae_u16 apw1 = (ao[0] << 8) | (ao[1] << 0);
					uae_u16 apw2 = (ao[2] << 8) | (ao[3] << 0);
					if (opc == 0x3fb2
						&& apw1 == 0xa190
						&& apw2 == 0x2770
						)
						printf("");

					if (opc != opcode) {
						// source EA modified opcode
						dp = table68k + opc;
					}

					// create destination addressing mode
					if (dp->duse) {
						int o = create_ea(&opc, pc, dp->dmode, dp->dreg, dp, &isconstant_dst, 1, fpuopcode, opcodesize);
						if (o < 0) {
							memcpy(opcode_memory, oldbytes, sizeof(oldbytes));
							if (o == -1)
								goto nextopcode;
							continue;
							goto nextopcode;
						}
						pc += o;
					}

					uae_u8 *bo = opcode_memory + 2;
					uae_u16 bopw1 = (bo[0] << 8) | (bo[1] << 0);
					uae_u16 bopw2 = (bo[2] << 8) | (bo[3] << 0);
					if (opc == 0xf228
						&& bopw1 == 0x003a
						//&& bopw2 == 0x2770
						)
						printf("");

					// bcc.x
					pc += handle_specials_branch(opc, pc, dp, &isconstant_src);

					put_word_test(opcode_memory_start, opc);
					// end word, needed to detect if instruction finished normally when
					// running on real hardware.
					put_word_test(pc, 0x4afc); // illegal instruction
					pc += 2;

					if (isconstant_src < 0 || isconstant_dst < 0) {
						constant_loops++;
						quick = 1;
					} else {
						// the smaller the immediate, the less test loops
						if (constant_loops > isconstant_src && constant_loops > isconstant_dst)
							constant_loops = isconstant_dst > isconstant_src ? isconstant_dst : isconstant_src;

						// don't do extra tests if no immediates
						if (!isconstant_dst && !isconstant_src)
							extra_loops = 0;
					}

					if (out_of_test_space) {
						wprintf(_T("Setting up test instruction generated out of bounds error!?\n"));
						abort();
					}

					dst = store_mem_bytes(dst, opcode_memory_start, pc - opcode_memory_start, oldbytes);
					ahcnt = 0;


					TCHAR out[256];
					uaecptr srcaddr, dstaddr;
					memset(out, 0, sizeof(out));
					// disassemble and output generated instruction
					for (int i = 0; i < MAX_REGISTERS; i++) {
						regs.regs[i] = cur_registers[i];
					}
					for (int i = 0; i < 8; i++) {
						regs.fp[i].fpx = cur_fpuregisters[i];
					}
					uaecptr nextpc;
					m68k_disasm_2(out, sizeof(out) / sizeof(TCHAR), opcode_memory_start, &nextpc, 1, &srcaddr, &dstaddr, 0xffffffff, 0);
					if (verbose) {
						my_trim(out);
						wprintf(_T("%08u %s"), subtest_count, out);
					}
#if 0
					// can't test because dp may be empty if instruction is invalid
					if (nextpc != pc - 2) {
						wprintf(_T("Disassembler/generator instruction length mismatch!\n"));
						abort();
					}
#endif
					int bc = isbranchinst(dp);
					if (bc) {
						if (bc < 0) {
							srcaddr = dstaddr;
						}
						if (bc == 2) {
							// RTS and friends
							int stackoffset = handle_specials_stack(opc, pc, dp, &isconstant_src);
							if (isconstant_src < 0 || isconstant_dst < 0) {
								constant_loops++;
								quick = 0;
							}
							srcaddr = get_long_test(regs.regs[8 + 7] + stackoffset);
						}
						// branch target is not accessible? skip.
						if ((srcaddr >= cur_registers[15] - 16 && srcaddr <= cur_registers[15] + 16) || ((srcaddr & 1) && !feature_exception3_instruction)) {
							// lets not jump directly to stack..
							prev_dst2 = dst;
							if (verbose) {
								if (srcaddr & 1)
									wprintf(_T(" Branch target is odd\n"));
								else
									wprintf(_T(" Branch target is stack\n"));
							}
							continue;
						}				
						testing_active = 1;
						if (!valid_address(srcaddr, 2, 1)) {
							if (verbose) {
								wprintf(_T(" Branch target inaccessible\n"));
							}
							testing_active = 0;
							prev_dst2 = dst;
							continue;
						} else {
							// branch target = generate exception
							put_word_test(srcaddr, 0x4afc);
							dst = store_mem(dst, 1);
							memcpy(&ahist2, &ahist, sizeof(struct accesshistory) *MAX_ACCESSHIST);
							ahcnt2 = ahcnt;
						}
						testing_active = 0;
					}

					*dst++ = CT_END_INIT;

					int exception_array[256] = { 0 };
					int ok = 0;
					int cnt_stopped = 0;

					uae_u16 last_sr = 0;
					uae_u32 last_pc = 0;
					uae_u32 last_registers[MAX_REGISTERS];
					floatx80 last_fpuregisters[8];
					uae_u32 last_fpiar = 0;
					uae_u32 last_fpsr = 0;
					uae_u32 last_fpcr = 0;

					int ccr_done = 0;
					int prev_s_cnt = 0;
					int s_cnt = 0;
					int t_cnt = 0;

					int extraccr = 0;

					// extra loops for supervisor and trace
					uae_u16 sr_allowed_mask = feature_sr_mask & 0xf000;
					uae_u16 sr_mask_request = feature_sr_mask & 0xf000;

					for (;;) {
						uae_u16 sr_mask = 0;

						if (extraccr & 1)
							sr_mask |= 0x2000; // S
						if (extraccr & 2)
							sr_mask |= 0x4000; // T0
						if (extraccr & 4)
							sr_mask |= 0x8000; // T1
						if (extraccr & 8)
							sr_mask |= 0x1000; // M

						if((sr_mask & ~sr_allowed_mask) || (sr_mask & ~sr_mask_request))
							goto nextextra;

						if (extraccr) {
							*dst++ = (uae_u8)extraccr;
						}

						// Test every CPU CCR or FPU SR/rounding/precision combination
						for (int ccr = 0; ccr < (fpumode ? 256 : 32); ccr++) {

							bool skipped = false;

							out_of_test_space = 0;
							test_exception = 0;
							cpu_stopped = 0;
							ahcnt = 0;

							memset(&regs, 0, sizeof(regs));

							regs.pc = opcode_memory_start;
							regs.irc = get_word_test(regs.pc + 2);

							// set up registers
							for (int i = 0; i < MAX_REGISTERS; i++) {
								regs.regs[i] = cur_registers[i];
							}
							regs.fpcr = 0;
							regs.fpsr = 0;
							regs.fpiar = 0;
							if (fpumode) {
								for (int i = 0; i < 8; i++) {
									regs.fp[i].fpx = cur_fpuregisters[i];
								}
								regs.fpiar = regs.pc;
								// condition codes
								regs.fpsr = (ccr & 15) << 24;
								// precision and rounding
								regs.fpcr = (ccr >> 4) << 4;
							}
							regs.sr = ccr | sr_mask;
							regs.usp = regs.regs[8 + 7];
							regs.isp = test_memory_end - 0x80;
							regs.msp = test_memory_end;

							// data size optimization, only store data
							// if it is different than in previous round
							if (!ccr && !extraccr) {
								last_sr = regs.sr;
								last_pc = regs.pc;
								for (int i = 0; i < 16; i++) {
									last_registers[i] = regs.regs[i];
								}
								for (int i = 0; i < 8; i++) {
									last_fpuregisters[i] = regs.fp[i].fpx;
								}
								last_fpiar = regs.fpiar;
								last_fpcr = regs.fpcr;
								last_fpsr = regs.fpsr;
							}

							if (regs.sr & 0x2000)
								prev_s_cnt++;

							execute_ins(opc);

							if (regs.s)
								s_cnt++;

							// validate PC
							uae_u8 *pcaddr = get_addr(regs.pc, 2, 0);

							// examine results
							if (cpu_stopped) {
								cnt_stopped++;
								// CPU stopped, skip test
								skipped = 1;
							} else if (out_of_test_space) {
								exception_array[0]++;
								// instruction accessed memory out of test address space bounds
								skipped = 1;
								did_out_of_bounds = true;
							} else if (test_exception) {
								// generated exception
								exception_array[test_exception]++;
								if (test_exception == 8 && !(sr_mask & 0x2000)) {
									// Privilege violation exception? Switch to super mode in next round.
									// Except if reset..
									if (lookup->mnemo != i_RESET) {
										sr_mask_request |= 0x2000;
										sr_allowed_mask |= 0x2000;
									}
								}
								if (test_exception == 3) {
									if (!feature_exception3_data && !test_exception_3_inst) {
										skipped = 1;
									}
									if (!feature_exception3_instruction && test_exception_3_inst) {
										skipped = 1;
									}
								} else {
									if (feature_exception3_data == 2) {
										skipped = 1;
									}
									if (feature_exception3_instruction == 2) {
										skipped = 1;
									}
								}
								if (test_exception == 9) {
									t_cnt++;
								}
							} else {
								// instruction executed successfully
								ok++;
								// validate branch instructions
								if (isbranchinst(dp)) {
									if ((regs.pc != srcaddr && regs.pc != pc - 2) || pcaddr[0] != 0x4a && pcaddr[1] != 0xfc) {
										printf("Branch instruction target fault\n");
										exit(0);
									}
								}
							}
							MakeSR();
							if (!skipped) {
								// save modified registers
								for (int i = 0; i < MAX_REGISTERS; i++) {
									uae_u32 s = last_registers[i];
									uae_u32 d = regs.regs[i];
									if (s != d) {
										dst = store_reg(dst, CT_DREG + i, s, d, -1);
										last_registers[i] = d;
									}
								}
								if (regs.sr != last_sr) {
									dst = store_reg(dst, CT_SR, last_sr, regs.sr, -1);
									last_sr = regs.sr;
								}
								if (regs.pc != last_pc) {
									dst = store_rel(dst, CT_PC, last_pc, regs.pc);
									last_pc = regs.pc;
								}
								if (currprefs.fpu_model) {
									for (int i = 0; i < 8; i++) {
										floatx80 s = last_fpuregisters[i];
										floatx80 d = regs.fp[i].fpx;
										if (s.high != d.high || s.low != d.low) {
											dst = store_fpureg(dst, CT_FPREG + i, d);
											last_fpuregisters[i] = d;
										}
									}
									if (regs.fpiar != last_fpiar) {
										dst = store_rel(dst, CT_FPIAR, last_fpiar, regs.fpiar);
										last_fpiar = regs.fpiar;
									}
									if (regs.fpsr != last_fpsr) {
										dst = store_reg(dst, CT_FPSR, last_fpsr, regs.fpsr, -1);
										last_fpsr = regs.fpsr;
									}
									if (regs.fpcr != last_fpcr) {
										dst = store_reg(dst, CT_FPCR, last_fpcr, regs.fpcr, -1);
										last_fpcr = regs.fpcr;
									}
								}
								dst = store_mem(dst, 0);
								if (test_exception) {
									*dst++ = CT_END | test_exception;
								} else {
									*dst++ = CT_END;
								}
								test_count++;
								subtest_count++;
								ccr_done++;
							} else {
								*dst++ = CT_END_SKIP;
							}
							undo_memory(ahist, &ahcnt);
						}
					nextextra:
						extraccr++;
						if (extraccr >= 16)
							break;
					}
					*dst++ = CT_END;

					undo_memory(ahist2, &ahcnt2);

					if (!ccr_done) {
						// undo whole previous ccr/extra loop if all tests were skipped
						dst = prev_dst2;
						//*dst++ = CT_END_INIT;
						memcpy(opcode_memory, oldbytes, sizeof(oldbytes));
					} else {
						full_format_cnt++;
					}
					if (verbose) {
						wprintf(_T(" OK=%d OB=%d S=%d/%d T=%d STP=%d"), ok, exception_array[0], prev_s_cnt, s_cnt, t_cnt, cnt_stopped);
						for (int i = 2; i < 128; i++) {
							if (exception_array[i])
								wprintf(_T(" E%d=%d"), i, exception_array[i]);
						}
					}
					if (dst - storage_buffer >= storage_buffer_watermark) {
						save_data(dst, dir);
						dst = storage_buffer;
						for (int i = 0; i < MAX_REGISTERS; i++) {
							dst = store_reg(dst, CT_DREG + i, 0, cur_registers[i], -1);
							regs.regs[i] = cur_registers[i];
						}
						if (currprefs.fpu_model) {
							for (int i = 0; i < 8; i++) {
								dst = store_fpureg(dst, CT_FPREG + i, cur_fpuregisters[i]);
								regs.fp[i].fpx = cur_fpuregisters[i];
							}
						}
					}
					if (verbose) {
						wprintf(_T("\n"));
					}
					if (did_out_of_bounds) {
						if (oob_retries) {
							oob_retries--;
							constant_loops++;
						} else {
							full_format_cnt++;
						}
					}
				}
			}
		nextopcode:;
		}

		save_data(dst, dir);
		dst = storage_buffer;

		if (opcodecnt == 1)
			break;
		if (lookup->mnemo == i_ILLG)
			break;

		rounds--;
		if (rounds < 0)
			break;

		// randomize registers
		for (int i = 0; i < 16 - 2; i++) {
			cur_registers[i] = rand32();
		}
		cur_registers[0] &= 0xffff;
		cur_registers[8] &= 0xffff;
		cur_registers[8 + 6]--;
		cur_registers[8 + 7] -= 2;

	}

	wprintf(_T("- %d tests\n"), subtest_count);
}

static void my_trim(TCHAR *s)
{
	int len;
	while (_tcslen(s) > 0 && _tcscspn(s, _T("\t \r\n")) == 0)
		memmove(s, s + 1, (_tcslen(s + 1) + 1) * sizeof(TCHAR));
	len = _tcslen(s);
	while (len > 0 && _tcscspn(s + len - 1, _T("\t \r\n")) == 0)
		s[--len] = '\0';
}

static const TCHAR *addrmodes[] =
{
	_T("Dreg"), _T("Areg"), _T("Aind"), _T("Aipi"), _T("Apdi"), _T("Ad16"), _T("Ad8r"),
	_T("absw"), _T("absl"), _T("PC16"), _T("PC8r"), _T("imm"), _T("Ad8rf"), _T("PC8rf"),
	NULL
};

#define INISECTION _T("cputest")

int __cdecl main(int argc, char *argv[])
{
	const struct cputbl *tbl = NULL;
	TCHAR path[1000];

	struct ini_data *ini = ini_load(_T("cputestgen.ini"), false);
	if (!ini) {
		wprintf(_T("couldn't open cputestgen.ini"));
		return 0;
	}

	currprefs.cpu_model = 68000;
	ini_getval(ini, INISECTION, _T("cpu"), &currprefs.cpu_model);
	if (currprefs.cpu_model != 68000 && currprefs.cpu_model != 68010 && currprefs.cpu_model != 68020) {
		wprintf(_T("Unsupported CPU model.\n"));
		return 0;
	}
	currprefs.fpu_model = 0;
	currprefs.fpu_mode = 1;
	ini_getval(ini, INISECTION, _T("fpu"), &currprefs.fpu_model);
	if (currprefs.fpu_model && currprefs.cpu_model < 68020) {
		wprintf(_T("FPU requires 68020 CPU.\n"));
		return 0;
	}
	if (currprefs.fpu_model != 0 && currprefs.fpu_model != 68881 && currprefs.fpu_model != 68882 && currprefs.fpu_model != 68040 && currprefs.fpu_model != 68060) {
		wprintf(_T("Unsupported FPU model.\n"));
		return 0;
	}

	verbose = 1;
	ini_getval(ini, INISECTION, _T("verbose"), &verbose);

	feature_addressing_modes[0] = 0xffffffff;
	feature_addressing_modes[1] = 0xffffffff;
	ad8r[0] = ad8r[1] = 1;
	pc8r[0] = pc8r[1] = 1;

	feature_exception3_data = 0;
	ini_getval(ini, INISECTION, _T("feature_exception3_data"), &feature_exception3_data);
	feature_exception3_instruction = 0;
	ini_getval(ini, INISECTION, _T("feature_exception3_instruction"), &feature_exception3_instruction);
	feature_sr_mask = 0;
	ini_getval(ini, INISECTION, _T("feature_sr_mask"), &feature_sr_mask);
	feature_full_extension_format = 0;
	if (currprefs.cpu_model > 68000) {
		ini_getval(ini, INISECTION, _T("feature_full_extension_format"), &feature_full_extension_format);
		if (feature_full_extension_format) {
			ad8r[0] |= 2;
			ad8r[1] |= 2;
			pc8r[0] |= 2;
			pc8r[1] |= 2;
		}
	}
	for (int j = 0; j < 2; j++) {
		TCHAR *am = NULL;
		if (ini_getstring(ini, INISECTION, j ? _T("feature_addressing_modes_dst") : _T("feature_addressing_modes_src"), &am)) {
			if (_tcslen(am) > 0) {
				feature_addressing_modes[j] = 0;
				ad8r[j] = 0;
				pc8r[j] = 0;
				TCHAR *p = am;
				while (p && *p) {
					TCHAR *pp = _tcschr(p, ',');
					if (pp) {
						*pp++ = 0;
					}
					TCHAR amtext[256];
					_tcscpy(amtext, p);
					my_trim(amtext);
					for (int i = 0; addrmodes[i]; i++) {
						if (!_tcsicmp(addrmodes[i], amtext)) {
							feature_addressing_modes[j] |= 1 << i;
							break;
						}
					}
					p = pp;
				}
				if (feature_addressing_modes[j] & (1 << Ad8r))
					ad8r[j] |= 1;
				if (feature_addressing_modes[j] & (1 << imm0)) // Ad8rf
					ad8r[j] |= 2;
				if (feature_addressing_modes[j] & (1 << PC8r))
					pc8r[j] |= 1;
				if (feature_addressing_modes[j] & (1 << imm1)) // PC8rf
					pc8r[j] |= 2;
				if (ad8r[j])
					feature_addressing_modes[j] |= 1 << Ad8r;
				if (pc8r[j])
					feature_addressing_modes[j] |= 1 << PC8r;
			}
			xfree(am);
		}
	}


	TCHAR *mode = NULL;
	ini_getstring(ini, INISECTION, _T("mode"), &mode);

	TCHAR *ipath = NULL;
	ini_getstring(ini, INISECTION, _T("path"), &ipath);
	if (!ipath) {
		_tcscpy(path, _T("data/"));
	} else {
		_tcscpy(path, ipath);
	}
	free(ipath);

	_stprintf(path + _tcslen(path), _T("%lu/"), currprefs.cpu_model);
	_wmkdir(path);

	xorshiftstate = 1;

	int v = 0;
	ini_getval(ini, INISECTION, _T("test_memory_start"), &v);
	if (!v) {
		wprintf(_T("test_memory_start is required\n"));
		return 0;
	}
	test_memory_start = v;

	v = 0;
	ini_getval(ini, INISECTION, _T("test_memory_size"), &v);
	if (!v) {
		wprintf(_T("test_memory_start is required\n"));
		return 0;
	}
	test_memory_size = v;
	test_memory_end = test_memory_start + test_memory_size;

	test_low_memory_start = 0x0000;
	test_low_memory_end = 0x8000;
	v = 0;
	if (ini_getval(ini, INISECTION, _T("test_low_memory_start"), &v))
		test_low_memory_start = v;
	v = 0;
	if (ini_getval(ini, INISECTION, _T("test_low_memory_end"), &v))
		test_low_memory_end = v;

	test_high_memory_start = 0x00ff8000;
	test_high_memory_end = 0x01000000;
	v = 0;
	if (ini_getval(ini, INISECTION, _T("test_high_memory_start"), &v))
		test_high_memory_start = v;
	v = 0;
	if (ini_getval(ini, INISECTION, _T("test_high_memory_end"), &v))
		test_high_memory_end = v;

	test_memory = (uae_u8 *)calloc(1, test_memory_size);
	test_memory_temp = (uae_u8 *)calloc(1, test_memory_size);
	if (!test_memory || !test_memory_temp) {
		wprintf(_T("Couldn't allocate test memory\n"));
		return 0;
	}

	opcode_memory = test_memory + test_memory_size / 2;
	opcode_memory_start = test_memory_start + test_memory_size / 2;

	fill_memory();

	TCHAR *lmem_rom_name = NULL;
	ini_getstring(ini, INISECTION, _T("low_rom"), &lmem_rom_name);
	if (lmem_rom_name) {
		if (load_file(NULL, lmem_rom_name, low_memory_temp, 32768, 0)) {
			wprintf(_T("Low test memory ROM loaded\n"));
			lmem_rom = 1;
		}
	}
	free(lmem_rom_name);

	TCHAR *hmem_rom_name = NULL;
	ini_getstring(ini, INISECTION, _T("high_rom"), &hmem_rom_name);
	if (hmem_rom_name) {
		if (load_file(NULL, hmem_rom_name, high_memory_temp, 32768, -1)) {
			wprintf(_T("High test memory ROM loaded\n"));
			hmem_rom = 1;
		}
	}
	free(hmem_rom_name);

	save_memory(path, _T("lmem.dat"), low_memory_temp, 32768);
	save_memory(path, _T("hmem.dat"), high_memory_temp, 32768);
	save_memory(path, _T("tmem.dat"), test_memory_temp, test_memory_size);

	storage_buffer = (uae_u8 *)calloc(max_storage_buffer, 1);
	// FMOVEM stores can use lots of memory
	storage_buffer_watermark = max_storage_buffer - 70000;

	for (int i = 0; i < 256; i++) {
		int j;
		for (j = 0; j < 8; j++) {
			if (i & (1 << j)) break;
		}
		movem_index1[i] = j;
		movem_index2[i] = 7 - j;
		movem_next[i] = i & (~(1 << j));
	}

	read_table68k();
	do_merges();

	if (currprefs.cpu_model == 68000) {
		tbl = op_smalltbl_90_test_ff;
		cpu_lvl = 0;
		addressing_mask = 0x00ffffff;
	} else if (currprefs.cpu_model == 68010) {
		tbl = op_smalltbl_91_test_ff;
		cpu_lvl = 1;
		addressing_mask = 0x00ffffff;
	} else if (currprefs.cpu_model == 68020) {
		tbl = op_smalltbl_92_test_ff;
		cpu_lvl = 2;
		addressing_mask = 0x00ffffff;
	} else {
		wprintf(_T("Unsupported CPU model.\n"));
		abort();
	}

	for (int opcode = 0; opcode < 65536; opcode++) {
		cpufunctbl[opcode] = op_illg_1;
	}

	for (int i = 0; tbl[i].handler != NULL; i++) {
		int opcode = tbl[i].opcode;
		cpufunctbl[opcode] = tbl[i].handler;
		cpudatatbl[opcode].length = tbl[i].length;
		cpudatatbl[opcode].disp020[0] = tbl[i].disp020[0];
		cpudatatbl[opcode].disp020[1] = tbl[i].disp020[1];
		cpudatatbl[opcode].branch = tbl[i].branch;
	}

	for (int opcode = 0; opcode < 65536; opcode++) {
		cpuop_func *f;
		instr *table = &table68k[opcode];

		if (table->mnemo == i_ILLG)
			continue;

		if (table->clev > cpu_lvl) {
			continue;
		}

		if (table->handler != -1) {
			int idx = table->handler;
			f = cpufunctbl[idx];
			if (f == op_illg_1)
				abort();
			cpufunctbl[opcode] = f;
			memcpy(&cpudatatbl[opcode], &cpudatatbl[idx], sizeof(struct cputbl_data));
		}
	}

	x_get_long = get_long_test;
	x_get_word = get_word_test;
	x_put_long = put_long_test;
	x_put_word = put_word_test;

	x_next_iword = next_iword_test;
	x_cp_next_iword = next_iword_test;
	x_next_ilong = next_ilong_test;
	x_cp_next_ilong = next_ilong_test;

	x_cp_get_disp_ea_020 = x_get_disp_ea_020;

	x_cp_get_long = get_long_test;
	x_cp_get_word = get_word_test;
	x_cp_get_byte = get_byte_test;
	x_cp_put_long = put_long_test;
	x_cp_put_word = put_word_test;
	x_cp_put_byte = put_byte_test;

	if (currprefs.fpu_model) {
		fpu_reset();
	}

	starttime = time(0);

	if (!mode) {
		wprintf(_T("Mode must be 'all' or '<mnemonic>'\n"));
		return 0;
	}

	TCHAR *modep = mode;
	while(modep) {

		int all = 0;
		int mnemo = -1;
		int fpuopcode = -1;
		int sizes = -1;

		if (!_tcsicmp(mode, _T("all"))) {

			verbose = 0;
			for (int j = 1; lookuptab[j].name; j++) {
				for (int i = 0; i < 8; i++) {
					test_mnemo(path, lookuptab[j].name, i, fpuopcode);
				}
			}
			// illg last. All currently selected CPU model's unsupported opcodes
			// (illegal instruction, a-line and f-line)
			for (int i = 0; i < 8; i++) {
				test_mnemo(path, lookuptab[0].name, i, fpuopcode);
			}
			break;
		}

		TCHAR *sp = _tcschr(modep, ',');
		if (sp)
			*sp++ = 0;
		TCHAR modetxt[100];
		_tcscpy(modetxt, modep);
		my_trim(modetxt);
		TCHAR *s = _tcschr(modetxt, '.');
		if (s) {
			*s = 0;
			TCHAR c = _totlower(s[1]);
			if (c == 'b')
				sizes = 6;
			if (c == 'w')
				sizes = 4;
			if (c == 'l')
				sizes = 0;
			if (c == 'u')
				sizes = 8;
			if (c == 's')
				sizes = 1;
			if (c == 'x')
				sizes = 2;
			if (c == 'p')
				sizes = 3;
			if (c == 'd')
				sizes = 5;
		}
		for (int j = 0; lookuptab[j].name; j++) {
			if (!_tcsicmp(modetxt, lookuptab[j].name)) {
				mnemo = j;
				break;
			}
		}
		if (mnemo < 0) {
			if (_totlower(modetxt[0]) == 'f') {
				if (!_tcsicmp(modetxt, _T("fmovecr"))) {
					mnemo = i_FPP;
					sizes = 7;
					fpuopcode = 0;
				} else if (!_tcsicmp(modetxt, _T("fmovem"))) {
					mnemo = i_FPP;
					sizes = 8;
					fpuopcode = 0;
				} else {
					for (int i = 0; i < 64; i++) {
						if (fpuopcodes[i] && !_tcsicmp(modetxt, fpuopcodes[i])) {
							mnemo = i_FPP;
							fpuopcode = i;
							break;
						}
					}
				}
			}
			if (mnemo < 0) {
				wprintf(_T("Couldn't find '%s'\n"), modetxt);
				return 0;
			}
		}

		if (mnemo >= 0 && sizes < 0) {
			if (fpuopcode >= 0) {
				for (int i = 0; i < 8; i++) {
					test_mnemo(path, lookuptab[mnemo].name, i, fpuopcode);
				}
			} else {
				test_mnemo(path, lookuptab[mnemo].name, 0, -1);
				test_mnemo(path, lookuptab[mnemo].name, 4, -1);
				test_mnemo(path, lookuptab[mnemo].name, 6, -1);
				test_mnemo(path, lookuptab[mnemo].name, -1, -1);
			}
		} else {
			test_mnemo(path, lookuptab[mnemo].name, sizes, fpuopcode);
		}

		modep = sp;
	}

	wprintf(_T("%d total tests generated\n"), test_count);

	return 0;
}
