/*
* UAE - The Un*x Amiga Emulator
*
* Debugger
*
* (c) 1995 Bernd Schmidt
* (c) 2006 Toni Wilen
*
*/

#include "sysconfig.h"
#include "sysdeps.h"

#include <ctype.h>
#include <signal.h>

#include "options.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "cpu_prefetch.h"
#include "debug.h"
#include "debugmem.h"
#include "cia.h"
#include "xwin.h"
#include "identify.h"
#include "audio.h"
#include "sounddep/sound.h"
#include "disk.h"
#include "savestate.h"
#include "autoconf.h"
#include "akiko.h"
#include "inputdevice.h"
#include "crc32.h"
#include "cpummu.h"
#include "rommgr.h"
#include "inputrecord.h"
#include "calc.h"
#include "cpummu.h"
#include "cpummu030.h"
#include "ar.h"
#include "pci.h"
#include "ppc/ppcd.h"
#include "uae/io.h"
#include "uae/ppc.h"
#include "drawing.h"
#include "devices.h"
#include "blitter.h"
#include "ini.h"
#include "readcpu.h"

#define TRACE_SKIP_INS 1
#define TRACE_MATCH_PC 2
#define TRACE_MATCH_INS 3
#define TRACE_RANGE_PC 4
#define TRACE_SKIP_LINE 5
#define TRACE_RAM_PC 6
#define TRACE_CHECKONLY 10

static int trace_mode;
static uae_u32 trace_param1;
static uae_u32 trace_param2;

int debugger_active;
static int debug_rewind;
static int memwatch_triggered;
int memwatch_access_validator;
int memwatch_enabled;
int debugging;
int exception_debugging;
int no_trace_exceptions;
int debug_copper = 0;
int debug_dma = 0, debug_heatmap = 0;
int debug_sprite_mask = 0xff;
int debug_illegal = 0;
uae_u64 debug_illegal_mask;
static int debug_mmu_mode;
static bool break_if_enforcer;
static uaecptr debug_pc;

static int trace_cycles;
static int last_hpos1, last_hpos2;
static int last_vpos1, last_vpos2;
static int last_frame = -1;
static uae_u32 last_cycles1, last_cycles2;

static uaecptr processptr;
static uae_char *processname;

static uaecptr debug_copper_pc;

extern int audio_channel_mask;
extern int inputdevice_logging;

static void debug_cycles(void)
{
	trace_cycles = 1;
	last_cycles2 = get_cycles();
	last_vpos2 = vpos;
	last_hpos2 = current_hpos();
}

void deactivate_debugger (void)
{
	debugger_active = 0;
	debugging = 0;
	exception_debugging = 0;
	processptr = 0;
	xfree (processname);
	processname = NULL;
	debugmem_enable();
	debug_pc = 0xffffffff;
}

void activate_debugger (void)
{
	if (isfullscreen() > 0)
		return;

	debugger_load_libraries();

	debug_pc = 0xffffffff;
	trace_mode = 0;
	if (debugger_active) {
		write_log(_T("Debugger already active!?\n"));
		return;
	}
	debug_cycles();
	debugger_active = 1;
	set_special (SPCFLAG_BRK);
	debugging = 1;
	mmu_triggered = 0;
	debugmem_disable();
}

void activate_debugger_new(void)
{
	activate_debugger();
	debug_pc = M68K_GETPC;
}

void activate_debugger_new_pc(uaecptr pc, int len)
{
	activate_debugger();
	trace_mode = TRACE_RANGE_PC;
	trace_param1 = pc;
	trace_param2 = pc + len;
}

bool debug_enforcer(void)
{
	if (!break_if_enforcer)
		return false;
	activate_debugger();
	return true;
}


int firsthist = 0;
int lasthist = 0;
struct cpuhistory {
	struct regstruct regs;
	int fp, vpos, hpos;
};
static struct cpuhistory history[MAX_HIST];

static const TCHAR help[] = {
	_T("          HELP for UAE Debugger\n")
	_T("         -----------------------\n\n")
	_T("  g [<address>]         Start execution at the current address or <address>.\n")
	_T("  c                     Dump state of the CIA, disk drives and custom registers.\n")
	_T("  r                     Dump state of the CPU.\n")
	_T("  r <reg> <value>       Modify CPU registers (Dx,Ax,USP,ISP,VBR,...).\n")
	_T("  rc[d]                 Show CPU instruction or data cache contents.\n")
	_T("  m <address> [<lines>] Memory dump starting at <address>.\n")
	_T("  a <address>           Assembler.\n")
	_T("  d <address> [<lines>] Disassembly starting at <address>.\n")
	_T("  t [instructions]      Step one or more instructions.\n")
	_T("  z                     Step through one instruction - useful for JSR, DBRA etc.\n")
	_T("  f                     Step forward until PC in RAM (\"boot block finder\").\n")
	_T("  f <address>           Add/remove breakpoint.\n")
	_T("  fa <address> [<start>] [<end>]\n")
	_T("                        Find effective address <address>.\n")
	_T("  fi                    Step forward until PC points to RTS, RTD or RTE.\n")
	_T("  fi <opcode>           Step forward until PC points to <opcode>.\n")
	_T("  fp \"<name>\"/<addr>    Step forward until process <name> or <addr> is active.\n")
	_T("  fl                    List breakpoints.\n")
	_T("  fd                    Remove all breakpoints.\n")
	_T("  fs <lines to wait> | <vpos> <hpos> Wait n scanlines/position.\n")
	_T("  fc <CCKs to wait>     Wait n color clocks.\n")
	_T("  fo <num> <reg> <oper> <val> [<mask> <val2>] Conditional register breakpoint.\n")
	_T("   reg=Dx,Ax,PC,USP,ISP,VBR,SR. oper:!=,==,<,>,>=,<=,-,!- (-=val to val2 range).\n")
	_T("  f <addr1> <addr2>     Step forward until <addr1> <= PC <= <addr2>.\n")
	_T("  e[x]                  Dump contents of all custom registers, ea = AGA colors.\n")
	_T("  i [<addr>]            Dump contents of interrupt and trap vectors.\n")
	_T("  il [<mask>]           Exception breakpoint.\n")
	_T("  o <0-2|addr> [<lines>]View memory as Copper instructions.\n")
	_T("  od                    Enable/disable Copper vpos/hpos tracing.\n")
	_T("  ot                    Copper single step trace.\n")
	_T("  ob <addr>             Copper breakpoint.\n")
	_T("  H[H] <cnt>            Show PC history (HH=full CPU info) <cnt> instructions.\n")
	_T("  C <value>             Search for values like energy or lifes in games.\n")
	_T("  Cl                    List currently found trainer addresses.\n")
	_T("  D[idxzs <[max diff]>] Deep trainer. i=new value must be larger, d=smaller,\n")
	_T("                        x = must be same, z = must be different, s = restart.\n")
	_T("  W <addr> <values[.x] separated by space> Write into Amiga memory.\n")
	_T("  W <addr> 'string'     Write into Amiga memory.\n")
	_T("  Wf <addr> <endaddr> <bytes or string like above>, fill memory.\n")
	_T("  Wc <addr> <endaddr> <destaddr>, copy memory.\n")
	_T("  w <num> <address> <length> <R/W/I> <F/C/L/N> [<value>[.x]] (read/write/opcode) (freeze/mustchange/logonly/nobreak).\n")
	_T("                        Add/remove memory watchpoints.\n")
	_T("  wd [<0-1>]            Enable illegal access logger. 1 = enable break.\n")
	_T("  L <file> <addr> [<n>] Load a block of Amiga memory.\n")
	_T("  S <file> <addr> <n>   Save a block of Amiga memory.\n")
	_T("  s \"<string>\"/<values> [<addr>] [<length>]\n")
	_T("                        Search for string/bytes.\n")
	_T("  T or Tt               Show exec tasks and their PCs.\n")
	_T("  Td,Tl,Tr,Tp,Ts,TS,Ti,TO,TM,Tf Show devs, libs, resources, ports, semaphores,\n")
	_T("                        residents, interrupts, doslist, memorylist, fsres.\n")
	_T("  b                     Step to previous state capture position.\n")
	_T("  M<a/b/s> <val>        Enable or disable audio channels, bitplanes or sprites.\n")
	_T("  sp <addr> [<addr2][<size>] Dump sprite information.\n")
	_T("  di <mode> [<track>]   Break on disk access. R=DMA read,W=write,RW=both,P=PIO.\n")
	_T("                        Also enables level 1 disk logging.\n")
	_T("  did <log level>       Enable disk logging.\n")
	_T("  dj [<level bitmask>]  Enable joystick/mouse input debugging.\n")
	_T("  smc [<0-1>]           Enable self-modifying code detector. 1 = enable break.\n")
	_T("  dm                    Dump current address space map.\n")
	_T("  v <vpos> [<hpos>]     Show DMA data (accurate only in cycle-exact mode).\n")
	_T("                        v [-1 to -4] = enable visual DMA debugger.\n")
	_T("  vh [<ratio> <lines>]  \"Heat map\"\n")
	_T("  I <custom event>      Send custom event string\n")
	_T("  ?<value>              Hex ($ and 0x)/Bin (%)/Dec (!) converter and calculator.\n")
#ifdef _WIN32
	_T("  x                     Close debugger.\n")
	_T("  xx                    Switch between console and GUI debugger.\n")
	_T("  mg <address>          Memory dump starting at <address> in GUI.\n")
	_T("  dg <address>          Disassembly starting at <address> in GUI.\n")
#endif
	_T("  q                     Quit the emulator. You don't want to use this command.\n\n")
};

void debug_help (void)
{
	console_out (help);
}


struct mw_acc {
	uae_u32 mask;
	const TCHAR *name;
};

static const struct mw_acc memwatch_access_masks[] =
{
	{ MW_MASK_ALL, _T("ALL") },
	{ MW_MASK_NONE, _T("NONE") },
	{ MW_MASK_ALL & ~(MW_MASK_CPU_I | MW_MASK_CPU_D_R | MW_MASK_CPU_D_W), _T("DMA") },

	{ MW_MASK_BLITTER_A | MW_MASK_BLITTER_B | MW_MASK_BLITTER_C | MW_MASK_BLITTER_D_N | MW_MASK_BLITTER_D_L | MW_MASK_BLITTER_D_F, _T("BLT") },
	{ MW_MASK_BLITTER_D_N | MW_MASK_BLITTER_D_L | MW_MASK_BLITTER_D_F, _T("BLTD") },

	{ MW_MASK_AUDIO_0 | MW_MASK_AUDIO_1 | MW_MASK_AUDIO_2 | MW_MASK_AUDIO_3, _T("AUD") },

	{ MW_MASK_BPL_0 | MW_MASK_BPL_1 | MW_MASK_BPL_2 | MW_MASK_BPL_3 |
		MW_MASK_BPL_4 | MW_MASK_BPL_5 | MW_MASK_BPL_6 | MW_MASK_BPL_7, _T("BPL") },

	{ MW_MASK_SPR_0 | MW_MASK_SPR_1 | MW_MASK_SPR_2 | MW_MASK_SPR_3 |
		MW_MASK_SPR_4 | MW_MASK_SPR_5 | MW_MASK_SPR_6 | MW_MASK_SPR_7, _T("SPR") },

	{ MW_MASK_CPU_I | MW_MASK_CPU_D_R | MW_MASK_CPU_D_W, _T("CPU") },
	{ MW_MASK_CPU_D_R | MW_MASK_CPU_D_W, _T("CPUD") },
	{ MW_MASK_CPU_I, _T("CPUI") },
	{ MW_MASK_CPU_D_R, _T("CPUDR") },
	{ MW_MASK_CPU_D_W, _T("CPUDW") },

	{ MW_MASK_COPPER, _T("COP") },

	{ MW_MASK_BLITTER_A, _T("BLTA") },
	{ MW_MASK_BLITTER_B, _T("BLTB") },
	{ MW_MASK_BLITTER_C, _T("BLTC") },
	{ MW_MASK_BLITTER_D_N, _T("BLTDN") },
	{ MW_MASK_BLITTER_D_L, _T("BLTDL") },
	{ MW_MASK_BLITTER_D_F, _T("BLTDF") },

	{ MW_MASK_DISK, _T("DSK") },

	{ MW_MASK_AUDIO_0, _T("AUD0") },
	{ MW_MASK_AUDIO_1, _T("AUD1") },
	{ MW_MASK_AUDIO_2, _T("AUD2") },
	{ MW_MASK_AUDIO_3, _T("AUD3") },

	{ MW_MASK_BPL_0, _T("BPL0") },
	{ MW_MASK_BPL_1, _T("BPL1") },
	{ MW_MASK_BPL_2, _T("BPL2") },
	{ MW_MASK_BPL_3, _T("BPL3") },
	{ MW_MASK_BPL_4, _T("BPL4") },
	{ MW_MASK_BPL_5, _T("BPL5") },
	{ MW_MASK_BPL_6, _T("BPL6") },
	{ MW_MASK_BPL_7, _T("BPL7") },

	{ MW_MASK_SPR_0, _T("SPR0") },
	{ MW_MASK_SPR_1, _T("SPR1") },
	{ MW_MASK_SPR_2, _T("SPR2") },
	{ MW_MASK_SPR_3, _T("SPR3") },
	{ MW_MASK_SPR_4, _T("SPR4") },
	{ MW_MASK_SPR_5, _T("SPR5") },
	{ MW_MASK_SPR_6, _T("SPR6") },
	{ MW_MASK_SPR_7, _T("SPR7") },

	{ 0, NULL },
};

static void mw_help(void)
{
	for (int i = 0; memwatch_access_masks[i].mask; i++) {
		console_out_f(_T("%s "), memwatch_access_masks[i].name);
	}
	console_out_f(_T("\n"));
}

static int debug_linecounter;
#define MAX_LINECOUNTER 1000

static int debug_out (const TCHAR *format, ...)
{
	va_list parms;
	TCHAR buffer[4000];

	va_start (parms, format);
	_vsntprintf (buffer, 4000 - 1, format, parms);
	va_end (parms);

	console_out (buffer);
	if (debug_linecounter < MAX_LINECOUNTER)
		debug_linecounter++;
	if (debug_linecounter >= MAX_LINECOUNTER)
		return 0;
	return 1;
}

uae_u32 get_byte_debug (uaecptr addr)
{
	uae_u32 v = 0xff;
	if (debug_mmu_mode) {
		flagtype olds = regs.s;
		regs.s = (debug_mmu_mode & 4) != 0;
		TRY(p) {
			if (currprefs.mmu_model == 68030) {
				v = mmu030_get_generic (addr, debug_mmu_mode, sz_byte, MMU030_SSW_SIZE_B);
			} else {
				if (debug_mmu_mode & 1) {
					bool odd = (addr & 1) != 0;
					addr &= ~1;
					v = mmu_get_iword(addr, sz_byte);
					if (!odd)
						v >>= 8;
				} else {
					v = mmu_get_user_byte (addr, regs.s != 0, false, sz_byte, false);
				}
			}
		} CATCH(p) {
		} ENDTRY
		regs.s = olds;
	} else {
		v = get_byte (addr);
	}
	return v;
}
uae_u32 get_word_debug (uaecptr addr)
{
	uae_u32 v = 0xffff;
	if (debug_mmu_mode) {
		flagtype olds = regs.s;
		regs.s = (debug_mmu_mode & 4) != 0;
		TRY(p) {
			if (currprefs.mmu_model == 68030) {
				v = mmu030_get_generic (addr, debug_mmu_mode, sz_word, MMU030_SSW_SIZE_W);
			} else {
				if (debug_mmu_mode & 1) {
					v = mmu_get_iword(addr, sz_word);
				} else {
					v = mmu_get_user_word (addr, regs.s != 0, false, sz_word, false);
				}
			}
		} CATCH(p) {
		} ENDTRY
		regs.s = olds;
	} else {
		v = get_word (addr);
	}
	return v;
}
uae_u32 get_long_debug (uaecptr addr)
{
	uae_u32 v = 0xffffffff;
	if (debug_mmu_mode) {
		flagtype olds = regs.s;
		regs.s = (debug_mmu_mode & 4) != 0;
		TRY(p) {
			if (currprefs.mmu_model == 68030) {
				v = mmu030_get_generic (addr, debug_mmu_mode, sz_long, MMU030_SSW_SIZE_L);
			} else {
				if (debug_mmu_mode & 1) {
					v = mmu_get_ilong(addr, sz_long);
				} else {
					v = mmu_get_user_long (addr, regs.s != 0, false, sz_long, false);
				}
			}
		} CATCH(p) {
		} ENDTRY
		regs.s = olds;
	} else {
		v = get_long (addr);
	}
	return v;
}
uae_u32 get_iword_debug (uaecptr addr)
{
	if (debug_mmu_mode) {
		return get_word_debug (addr);
	} else {
		if (valid_address (addr, 2))
			return get_word (addr);
		return 0xffff;
	}
}
uae_u32 get_ilong_debug (uaecptr addr)
{
	if (debug_mmu_mode) {
		return get_long_debug (addr);
	} else {
		if (valid_address (addr, 4))
			return get_long (addr);
		return 0xffffffff;
	}
}
uae_u8 *get_real_address_debug(uaecptr addr)
{
	if (debug_mmu_mode) {
		flagtype olds = regs.s;
		TRY(p) {
			if (currprefs.mmu_model >= 68040)
				addr = mmu_translate(addr, 0, regs.s != 0, (debug_mmu_mode & 1), false, 0);
			else
				addr = mmu030_translate(addr, regs.s != 0, (debug_mmu_mode & 1), false);
		} CATCH(p) {
		} ENDTRY
	}
	return get_real_address(addr);
}

int debug_safe_addr (uaecptr addr, int size)
{
	if (debug_mmu_mode) {
		flagtype olds = regs.s;
		regs.s = (debug_mmu_mode & 4) != 0;
		TRY(p) {
			if (currprefs.mmu_model >= 68040)
				addr = mmu_translate (addr, 0, regs.s != 0, (debug_mmu_mode & 1), false, size);
			else
				addr = mmu030_translate (addr, regs.s != 0, (debug_mmu_mode & 1), false);
		} CATCH(p) {
			STOPTRY;
			return 0;
		} ENDTRY
		regs.s = olds;
	}
	addrbank *ab = &get_mem_bank (addr);
	if (!ab)
		return 0;
	if (ab->flags & ABFLAG_SAFE)
		return 1;
	if (!ab->check (addr, size))
		return 0;
	if (ab->flags & (ABFLAG_RAM | ABFLAG_ROM | ABFLAG_ROMIN | ABFLAG_SAFE))
		return 1;
	return 0;
}

static bool iscancel (int counter)
{
	static int cnt;

	cnt++;
	if (cnt < counter)
		return false;
	cnt = 0;
	if (!console_isch ())
		return false;
	console_getch ();
	return true;
}

static bool isoperator(TCHAR **cp)
{
	TCHAR c = **cp;
	return c == '+' || c == '-' || c == '/' || c == '*' || c == '(' || c == ')';
}

static void ignore_ws (TCHAR **c)
{
	while (**c && _istspace(**c))
		(*c)++;
}
static TCHAR peekchar (TCHAR **c)
{
	return **c;
}
static TCHAR readchar (TCHAR **c)
{
	TCHAR cc = **c;
	(*c)++;
	return cc;
}
static TCHAR next_char (TCHAR **c)
{
	ignore_ws (c);
	return *(*c)++;
}
static TCHAR peek_next_char (TCHAR **c)
{
	TCHAR *pc = *c;
	return pc[1];
}
static int more_params (TCHAR **c)
{
	ignore_ws (c);
	return (**c) != 0;
}

static uae_u32 readint (TCHAR **c);
static uae_u32 readbin (TCHAR **c);
static uae_u32 readhex (TCHAR **c);

static const TCHAR *debugoper[] = {
	_T("=="),
	_T("!="),
	_T("<="),
	_T(">="),
	_T("<"),
	_T(">"),
	_T("-"),
	_T("!-"),
	NULL
};

static int getoperidx(TCHAR **c)
{
	int i;
	TCHAR *p = *c;
	TCHAR tmp[10];
	int extra = 0;

	i = 0;
	while (p[i]) {
		tmp[i] = _totupper(p[i]);
		if (i >= sizeof(tmp) / sizeof(TCHAR) - 1)
			break;
		i++;
	}
	tmp[i] = 0;
	if (!_tcsncmp(tmp, _T("!="), 2)) {
		(*c) += 2;
		return BREAKPOINT_CMP_NEQUAL;
	} else if (!_tcsncmp(tmp, _T("=="), 2)) {
		(*c) += 2;
		return BREAKPOINT_CMP_EQUAL;
	} else if (!_tcsncmp(tmp, _T(">="), 2)) {
		(*c) += 2;
		return BREAKPOINT_CMP_LARGER_EQUAL;
	} else if (!_tcsncmp(tmp, _T("<="), 2)) {
		(*c) += 2;
		return BREAKPOINT_CMP_SMALLER_EQUAL;
	} else if (!_tcsncmp(tmp, _T(">"), 1)) {
		(*c) += 1;
		return BREAKPOINT_CMP_LARGER;
	} else if (!_tcsncmp(tmp, _T("<"), 1)) {
		(*c) += 1;
		return BREAKPOINT_CMP_SMALLER;
	} else if (!_tcsncmp(tmp, _T("-"), 1)) {
		(*c) += 1;
		return BREAKPOINT_CMP_RANGE;
	} else if (!_tcsncmp(tmp, _T("!-"), 2)) {
		(*c) += 2;
		return BREAKPOINT_CMP_NRANGE;
	}
	return -1;
}

static const TCHAR *debugregs[] = {
	_T("D0"),
	_T("D1"),
	_T("D2"),
	_T("D3"),
	_T("D4"),
	_T("D5"),
	_T("D6"),
	_T("D7"),
	_T("A0"),
	_T("A1"),
	_T("A2"),
	_T("A3"),
	_T("A4"),
	_T("A5"),
	_T("A6"),
	_T("A7"),
	_T("PC"),
	_T("USP"),
	_T("MSP"),
	_T("ISP"),
	_T("VBR"),
	_T("SR"),
	_T("CCR"),
	_T("CACR"),
	_T("CAAR"),
	_T("SFC"),
	_T("DFC"),
	_T("TC"),
	_T("ITT0"),
	_T("ITT1"),
	_T("DTT0"),
	_T("DTT1"),
	_T("BUSC"),
	_T("PCR"),
	NULL
};

static int getregidx(TCHAR **c)
{
	int i;
	TCHAR *p = *c;
	TCHAR tmp[10];
	int extra = 0;

	i = 0;
	while (p[i]) {
		tmp[i] = _totupper(p[i]);
		if (i >= sizeof(tmp) / sizeof(TCHAR) - 1)
			break;
		i++;
	}
	tmp[i] = 0;
	for (int i = 0; debugregs[i]; i++) {
		if (!_tcsncmp(tmp, debugregs[i], _tcslen(debugregs[i]))) {
			(*c) += _tcslen(debugregs[i]);
			return i;
		}
	}
	return -1;
}

static uae_u32 returnregx(int regid)
{
	if (regid < BREAKPOINT_REG_PC)
		return regs.regs[regid];
	switch(regid)
	{
		case BREAKPOINT_REG_PC:
		return M68K_GETPC;
		case BREAKPOINT_REG_USP:
		return regs.usp;
		case BREAKPOINT_REG_MSP:
		return regs.msp;
		case BREAKPOINT_REG_ISP:
		return regs.isp;
		case BREAKPOINT_REG_VBR:
		return regs.vbr;
		case BREAKPOINT_REG_SR:
		MakeSR();
		return regs.sr;
		case BREAKPOINT_REG_CCR:
		MakeSR();
		return regs.sr & 31;
		case BREAKPOINT_REG_CACR:
		return regs.cacr;
		case BREAKPOINT_REG_CAAR:
		return regs.caar;
		case BREAKPOINT_REG_SFC:
		return regs.sfc;
		case BREAKPOINT_REG_DFC:
		return regs.dfc;
		case BREAKPOINT_REG_TC:
		if (currprefs.cpu_model == 68030)
			return tc_030;
		return regs.tcr;
		case BREAKPOINT_REG_ITT0:
		if (currprefs.cpu_model == 68030)
			return tt0_030;
		return regs.itt0;
		case BREAKPOINT_REG_ITT1:
		if (currprefs.cpu_model == 68030)
			return tt1_030;
		return regs.itt1;
		case BREAKPOINT_REG_DTT0:
		return regs.dtt0;
		case BREAKPOINT_REG_DTT1:
		return regs.dtt1;
		case BREAKPOINT_REG_BUSC:
		return regs.buscr;
		case BREAKPOINT_REG_PCR:
		return regs.fpcr;
	}
	return 0;
}

static int readregx (TCHAR **c, uae_u32 *valp)
{
	int i;
	uae_u32 addr;
	TCHAR *p = *c;
	TCHAR tmp[10], *tp;
	int extra = 0;

	addr = 0;
	i = 0;
	while (p[i]) {
		tmp[i] = _totupper (p[i]);
		if (i >= sizeof (tmp) / sizeof (TCHAR) - 1)
			break;
		i++;
	}
	tmp[i] = 0;
	tp = tmp;
	if (_totupper (tmp[0]) == 'R') {
		tp = tmp + 1;
		extra = 1;
	}
	if (!_tcsncmp (tp, _T("USP"), 3)) {
		addr = regs.usp;
		(*c) += 3;
	} else if (!_tcsncmp (tp, _T("VBR"), 3)) {
		addr = regs.vbr;
		(*c) += 3;
	} else if (!_tcsncmp (tp, _T("MSP"), 3)) {
		addr = regs.msp;
		(*c) += 3;
	} else if (!_tcsncmp (tp, _T("ISP"), 3)) {
		addr = regs.isp;
		(*c) += 3;
	} else if (!_tcsncmp (tp, _T("PC"), 2)) {
		addr = regs.pc;
		(*c) += 2;
	} else if (tp[0] == 'A' || tp[0] == 'D') {
		int reg = 0;
		if (tp[0] == 'A')
			reg += 8;
		reg += tp[1] - '0';
		if (reg < 0 || reg > 15)
			return 0;
		addr = regs.regs[reg];
		(*c) += 2;
	} else {
		return 0;
	}
	*valp = addr;
	(*c) += extra;
	return 1;
}

static bool readbinx (TCHAR **c, uae_u32 *valp)
{
	uae_u32 val = 0;
	bool first = true;

	ignore_ws (c);
	for (;;) {
		TCHAR nc = **c;
		if (nc != '1' && nc != '0') {
			if (first)
				return false;
			break;
		}
		first = false;
		(*c)++;
		val <<= 1;
		if (nc == '1')
			val |= 1;
	}
	*valp = val;
	return true;
}

static bool readhexx (TCHAR **c, uae_u32 *valp)
{
	uae_u32 val = 0;
	TCHAR nc;

	ignore_ws (c);
	if (!isxdigit (peekchar (c)))
		return false;
	while (isxdigit (nc = **c)) {
		(*c)++;
		val *= 16;
		nc = _totupper (nc);
		if (isdigit (nc)) {
			val += nc - '0';
		} else {
			val += nc - 'A' + 10;
		}
	}
	*valp = val;
	return true;
}

static bool readintx (TCHAR **c, uae_u32 *valp)
{
	uae_u32 val = 0;
	TCHAR nc;
	int negative = 0;

	ignore_ws (c);
	if (**c == '-')
		negative = 1, (*c)++;
	if (!isdigit (peekchar (c)))
		return false;
	while (isdigit (nc = **c)) {
		(*c)++;
		val *= 10;
		val += nc - '0';
	}
	*valp = val * (negative ? -1 : 1);
	return true;
}

static int checkvaltype2 (TCHAR **c, uae_u32 *val, TCHAR def)
{
	TCHAR nc;

	ignore_ws (c);
	nc = _totupper (**c);
	if (nc == '!') {
		(*c)++;
		return readintx (c, val) ? 1 : 0;
	}
	if (nc == '$') {
		(*c)++;
		return  readhexx (c, val) ? 1 : 0;
	}
	if (nc == '0' && _totupper ((*c)[1]) == 'X') {
		(*c)+= 2;
		return  readhexx (c, val) ? 1 : 0;
	}
	if (nc == '%') {
		(*c)++;
		return readbinx (c, val) ? 1: 0;
	}
	if (nc >= 'A' && nc <= 'Z' && nc != 'A' && nc != 'D') {
		if (readregx (c, val))
			return 1;
	}
	TCHAR name[256];
	name[0] = 0;
	for (int i = 0; i < sizeof name / sizeof(TCHAR) - 1; i++) {
		nc = (*c)[i];
		if (nc == 0 || nc == ' ')
			break;
		name[i] = nc;
		name[i + 1] = 0;
	}
	if (name[0]) {
		TCHAR *np = name;
		if (*np == '#')
			np++;
		if (debugmem_get_symbol_value(np, val)) {
			(*c) += _tcslen(name);
			return 1;
		}
	}
	if (def == '!') {
		return readintx (c, val) ? -1 : 0;
	} else if (def == '$') {
		return readhexx (c, val) ? -1 : 0;
	} else if (def == '%') {
		return readbinx (c, val) ? -1 : 0;
	}
	return 0;
}

static int readsize (int val, TCHAR **c)
{
	TCHAR cc = _totupper (readchar(c));
	if (cc == 'B')
		return 1;
	if (cc == 'W')
		return 2;
	if (cc == '3')
		return 3;
	if (cc == 'L')
		return 4;
	return 0;
}

static int checkvaltype (TCHAR **cp, uae_u32 *val, int *size, TCHAR def)
{
	TCHAR form[256], *p;
	bool gotop = false;
	double out;

	form[0] = 0;
	if (size)
		*size = 0;
	p = form;
	for (;;) {
		uae_u32 v;
		if (!checkvaltype2 (cp, &v, def))
			return 0;
		*val = v;
		// stupid but works!
		_stprintf(p, _T("%u"), v);
		p += _tcslen (p);
		if (peekchar (cp) == '.') {
			readchar (cp);
			if (size)
				*size = readsize (v, cp);
		}
		if (!isoperator (cp))
			break;
		gotop = true;
		*p++= readchar (cp);
		*p = 0;
	}
	if (!gotop) {
		if (size && *size == 0) {
			uae_s32 v = (uae_s32)(*val);
			if (v > 65535 || v < -32767) {
				*size = 4;
			} else if (v > 255 || v < -127) {
				*size = 2;
			} else {
				*size = 1;
			}
		}
		return 1;
	}
	if (calc (form, &out)) {
		*val = (uae_u32)out;
		if (size && *size == 0) {
			uae_s32 v = (uae_s32)(*val);
			if (v > 255 || v < -127) {
				*size = 2;
			} else if (v > 65535 || v < -32767) {
				*size = 4;
			} else {
				*size = 1;
			}
		}
		return 1;
	}
	return 0;
}


static uae_u32 readnum (TCHAR **c, int *size, TCHAR def)
{
	uae_u32 val;
	if (checkvaltype (c, &val, size, def))
		return val;
	return 0;
}

static uae_u32 readint (TCHAR **c)
{
	int size;
	return readnum (c, &size, '!');
}
static uae_u32 readhex (TCHAR **c)
{
	int size;
	return readnum (c, &size, '$');
}
static uae_u32 readbin (TCHAR **c)
{
	int size;
	return readnum (c, &size, '%');
}
static uae_u32 readint (TCHAR **c, int *size)
{
	return readnum (c, size, '!');
}
static uae_u32 readhex (TCHAR **c, int *size)
{
	return readnum (c, size, '$');
}

static int next_string (TCHAR **c, TCHAR *out, int max, int forceupper)
{
	TCHAR *p = out;
	int startmarker = 0;

	if (**c == '\"') {
		startmarker = 1;
		(*c)++;
	}
	*p = 0;
	while (**c != 0) {
		if (**c == '\"' && startmarker)
			break;
		if (**c == 32 && !startmarker) {
			ignore_ws (c);
			break;
		}
		*p = next_char (c);
		if (forceupper)
			*p = _totupper(*p);
		*++p = 0;
		max--;
		if (max <= 1)
			break;
	}
	return _tcslen (out);
}

static void converter (TCHAR **c)
{
	uae_u32 v = readint (c);
	TCHAR s[100];
	int i;

	for (i = 0; i < 32; i++)
		s[i] = (v & (1 << (31 - i))) ? '1' : '0';
	s[i] = 0;
	console_out_f (_T("0x%08X = %%%s = %u = %d\n"), v, s, v, (uae_s32)v);
}

int notinrom (void)
{
	uaecptr pc = munge24 (m68k_getpc ());
	if (pc < 0x00e00000 || pc > 0x00ffffff)
		return 1;
	return 0;
}

static uae_u32 lastaddr (void)
{
	for (int i = MAX_RAM_BOARDS - 1; i >= 0; i--) {
		if (currprefs.z3fastmem[i].size)
			return z3fastmem_bank[i].start + currprefs.z3fastmem[i].size;
	}
	if (currprefs.z3chipmem_size)
		return z3chipmem_bank.start + currprefs.z3chipmem_size;
	if (currprefs.mbresmem_high_size)
		return a3000hmem_bank.start + currprefs.mbresmem_high_size;
	if (currprefs.mbresmem_low_size)
		return a3000lmem_bank.start + currprefs.mbresmem_low_size;
	if (currprefs.bogomem_size)
		return bogomem_bank.start + currprefs.bogomem_size;
	for (int i = MAX_RAM_BOARDS - 1; i >= 0; i--) {
		if (currprefs.fastmem[i].size)
			return fastmem_bank[i].start + currprefs.fastmem[i].size;
	}
	return currprefs.chipmem_size;
}

static uaecptr nextaddr2 (uaecptr addr, int *next)
{
	uaecptr prev, prevx;
	int size, sizex;

	if (addr >= lastaddr ()) {
		*next = -1;
		return 0xffffffff;
	}
	prev = currprefs.z3autoconfig_start + currprefs.z3fastmem[0].size;
	size = currprefs.z3fastmem[1].size;

	if (currprefs.z3fastmem[0].size) {
		prevx = prev;
		sizex = size;
		size = currprefs.z3fastmem[0].size;
		prev = z3fastmem_bank[0].start;
		if (addr == prev + size) {
			*next = prevx + sizex;
			return prevx;
		}
	}
	if (currprefs.z3chipmem_size) {
		prevx = prev;
		sizex = size;
		size = currprefs.z3chipmem_size;
		prev = z3chipmem_bank.start;
		if (addr == prev + size) {
			*next = prevx + sizex;
			return prevx;
		}
	}
	if (currprefs.mbresmem_high_size) {
		sizex = size;
		prevx = prev;
		size = currprefs.mbresmem_high_size;
		prev = a3000hmem_bank.start;
		if (addr == prev + size) {
			*next = prevx + sizex;
			return prevx;
		}
	}
	if (currprefs.mbresmem_low_size) {
		prevx = prev;
		sizex = size;
		size = currprefs.mbresmem_low_size;
		prev = a3000lmem_bank.start;
		if (addr == prev + size) {
			*next = prevx + sizex;
			return prevx;
		}
	}
	if (currprefs.bogomem_size) {
		sizex = size;
		prevx = prev;
		size = currprefs.bogomem_size;
		prev = bogomem_bank.start;
		if (addr == prev + size) {
			*next = prevx + sizex;
			return prevx;
		}
	}
	if (currprefs.fastmem[0].size) {
		sizex = size;
		prevx = prev;
		size = currprefs.fastmem[0].size;
		prev = fastmem_bank[0].start;
		if (addr == prev + size) {
			*next = prevx + sizex;
			return prevx;
		}
	}
	sizex = size;
	prevx = prev;
	size = currprefs.chipmem_size;
	if (addr == size) {
		*next = prevx + sizex;
		return prevx;
	}
	if (addr == 1)
		*next = size;
	return addr;
}

static uaecptr nextaddr (uaecptr addr, uaecptr last, uaecptr *end)
{
	static uaecptr old;
	int next = last;
	if (last && addr >= last) {
		old = 0xffffffff;
		return 0xffffffff;
	}
	if (addr == 0xffffffff) {
		if (end)
			*end = currprefs.chipmem_size;
		return 0;
	}
	if (end)
		next = *end;
	addr = nextaddr2 (addr + 1, &next);
	if (end)
		*end = next;
	if (old != next) {
		if (addr != 0xffffffff)
			console_out_f (_T("Scanning.. %08x - %08x (%s)\n"), addr & 0xffffff00, next, get_mem_bank (addr).name);
		old = next;
	}
#if 0
	if (next && addr != 0xffffffff) {
		uaecptr xa = addr;
		if (xa == 1)
			xa = 0;
		console_out_f ("%08X -> %08X (%08X)...\n", xa, xa + next - 1, next);
	}
#endif
	return addr;
}

uaecptr dumpmem2 (uaecptr addr, TCHAR *out, int osize)
{
	int i, cols = 8;
	int nonsafe = 0;

	if (osize <= (9 + cols * 5 + 1 + 2 * cols))
		return addr;
	_stprintf (out, _T("%08X "), addr);
	for (i = 0; i < cols; i++) {
		uae_u8 b1, b2;
		b1 = b2 = 0;
		if (debug_safe_addr (addr, 1)) {
			b1 = get_byte_debug (addr + 0);
			b2 = get_byte_debug (addr + 1);
			_stprintf (out + 9 + i * 5, _T("%02X%02X "), b1, b2);
			out[9 + cols * 5 + 1 + i * 2 + 0] = b1 >= 32 && b1 < 127 ? b1 : '.';
			out[9 + cols * 5 + 1 + i * 2 + 1] = b2 >= 32 && b2 < 127 ? b2 : '.';
		} else {
			nonsafe++;
			_tcscpy (out + 9 + i * 5, _T("**** "));
			out[9 + cols * 5 + 1 + i * 2 + 0] = '*';
			out[9 + cols * 5 + 1 + i * 2 + 1] = '*';
		}
		addr += 2;
	}
	out[9 + cols * 5] = ' ';
	out[9 + cols * 5 + 1 + 2 * cols] = 0;
	if (nonsafe == cols) {
		addrbank *ab = &get_mem_bank (addr);
		if (ab->name)
			memcpy (out + (9 + 4 + 1) * sizeof (TCHAR), ab->name, _tcslen (ab->name) * sizeof (TCHAR));
	}
	return addr;
}

static void dumpmem (uaecptr addr, uaecptr *nxmem, int lines)
{
	TCHAR line[MAX_LINEWIDTH + 1];
	for (;lines--;) {
		addr = dumpmem2 (addr, line, sizeof(line));
		debug_out (_T("%s"), line);
		if (!debug_out (_T("\n")))
			break;
	}
	*nxmem = addr;
}

static void dump_custom_regs(bool aga, bool ext)
{
	int len, end;
	uae_u8 *p1, *p2, *p3, *p4;
	TCHAR extra1[256], extra2[256];

	extra1[0] = 0;
	extra2[0] = 0;
	if (aga) {
		dump_aga_custom();
		return;
	}

	p1 = p2 = save_custom (&len, 0, 1);
	p1 += 4; // skip chipset type
	for (int i = 0; i < 4; i++) {
		p4 = p1 + 0xa0 + i * 16;
		p3 = save_audio (i, &len, 0);
		p4[0] = p3[12];
		p4[1] = p3[13];
		p4[2] = p3[14];
		p4[3] = p3[15];
		p4[4] = p3[4];
		p4[5] = p3[5];
		p4[6] = p3[8];
		p4[7] = p3[9];
		p4[8] = 0;
		p4[9] = p3[1];
		p4[10] = p3[10];
		p4[11] = p3[11];
		free (p3);
	}
	int total = 0;
	int i = 0;
	while (custd[i].name) {
		if (!(custd[i].special & CD_NONE))
			total++;
		i++;
	}
	int cnt1 = 0;
	int cnt2 = 0;
	i = 0;
	while (i < total / 2 + 1) {
		for (;;) {
			cnt2++;
			if (!(custd[cnt2].special & CD_NONE))
				break;
		}
		i++;
	}
	for (int i = 0; i < total / 2 + 1; i++) {
		uae_u16 v1, v2;
		int addr1, addr2;
		addr1 = custd[cnt1].adr & 0x1ff;
		addr2 = custd[cnt2].adr & 0x1ff;
		v1 = (p1[addr1 + 0] << 8) | p1[addr1 + 1];
		v2 = (p1[addr2 + 0] << 8) | p1[addr2 + 1];
		if (ext) {
			struct custom_store *cs;
			cs = &custom_storage[addr1 >> 1];
			_stprintf(extra1, _T("\t%04X %08X %s"), cs->value, cs->pc & ~1, (cs->pc & 1) ? _T("COP") : _T("CPU"));
			cs = &custom_storage[addr2 >> 1];
			_stprintf(extra2, _T("\t%04X %08X %s"), cs->value, cs->pc & ~1, (cs->pc & 1) ? _T("COP") : _T("CPU"));
		}
		console_out_f (_T("%03X %s\t%04X%s\t%03X %s\t%04X%s\n"),
			addr1, custd[cnt1].name, v1, extra1,
			addr2, custd[cnt2].name, v2, extra2);
		for (;;) {
			cnt1++;
			if (!(custd[cnt1].special & CD_NONE))
				break;
		}
		for (;;) {
			cnt2++;
			if (!(custd[cnt2].special & CD_NONE))
				break;
		}
	}
	xfree(p2);
}

static void dump_vectors (uaecptr addr)
{
	int i = 0, j = 0;

	if (addr == 0xffffffff)
		addr = regs.vbr;

	while (int_labels[i].name || trap_labels[j].name) {
		if (int_labels[i].name) {
			console_out_f (_T("$%08X %02d: %12s $%08X  "), int_labels[i].adr + addr, int_labels[i].adr / 4,
				int_labels[i].name, get_long_debug (int_labels[i].adr + addr));
			i++;
		}
		if (trap_labels[j].name) {
			console_out_f (_T("$%08X %02d: %12s $%08X"), trap_labels[j].adr + addr, trap_labels[j].adr / 4,
				trap_labels[j].name, get_long_debug (trap_labels[j].adr + addr));
			j++;
		}
		console_out (_T("\n"));
	}
}

static void disassemble_wait (FILE *file, unsigned long insn)
{
	int vp, hp, ve, he, bfd, v_mask, h_mask;
	int doout = 0;

	vp = (insn & 0xff000000) >> 24;
	hp = (insn & 0x00fe0000) >> 16;
	ve = (insn & 0x00007f00) >> 8;
	he = (insn & 0x000000fe);
	bfd = (insn & 0x00008000) >> 15;

	/* bit15 can never be masked out*/
	v_mask = vp & (ve | 0x80);
	h_mask = hp & he;
	if (v_mask > 0) {
		doout = 1;
		console_out (_T("vpos "));
		if (ve != 0x7f) {
			console_out_f (_T("& 0x%02x "), ve);
		}
		console_out_f (_T(">= 0x%02x"), v_mask);
	}
	if (he > 0) {
		if (v_mask > 0) {
			console_out (_T(" and"));
		}
		console_out (_T(" hpos "));
		if (he != 0xfe) {
			console_out_f (_T("& 0x%02x "), he);
		}
		console_out_f (_T(">= 0x%02x"), h_mask);
	} else {
		if (doout)
			console_out (_T(", "));
		console_out (_T(", ignore horizontal"));
	}

	console_out_f (_T("\n                        \t;  VP %02x, VE %02x; HP %02x, HE %02x; BFD %d\n"),
		vp, ve, hp, he, bfd);
}

#define NR_COPPER_RECORDS 100000
/* Record copper activity for the debugger.  */
struct cop_rec
{
	uae_u16 w1, w2;
	int hpos, vpos;
	int bhpos, bvpos;
	uaecptr addr;
};
static struct cop_rec *cop_record[2];
static int nr_cop_records[2], curr_cop_set;

#define NR_DMA_REC_HPOS 256
#define NR_DMA_REC_VPOS 1000
static struct dma_rec *dma_record[2];
static int dma_record_toggle, dma_record_frame[2];

void record_dma_reset (void)
{
	int v, h;
	struct dma_rec *dr, *dr2;

	if (!dma_record[0])
		return;
	dma_record_toggle ^= 1;
	dr = dma_record[dma_record_toggle];
	for (v = 0; v < NR_DMA_REC_VPOS; v++) {
		for (h = 0; h < NR_DMA_REC_HPOS; h++) {
			dr2 = &dr[v * NR_DMA_REC_HPOS + h];
			memset (dr2, 0, sizeof (struct dma_rec));
			dr2->reg = 0xffff;
			dr2->addr = 0xffffffff;
		}
	}
}

void record_copper_reset (void)
{
	/* Start a new set of copper records.  */
	curr_cop_set ^= 1;
	nr_cop_records[curr_cop_set] = 0;
}

STATIC_INLINE uae_u32 ledcolor (uae_u32 c, uae_u32 *rc, uae_u32 *gc, uae_u32 *bc, uae_u32 *a)
{
	uae_u32 v = rc[(c >> 16) & 0xff] | gc[(c >> 8) & 0xff] | bc[(c >> 0) & 0xff];
	if (a)
		v |= a[255 - ((c >> 24) & 0xff)];
	return v;
}

STATIC_INLINE void putpixel (uae_u8 *buf, int bpp, int x, xcolnr c8)
{
	if (x <= 0)
		return;

	switch (bpp) {
	case 1:
		buf[x] = (uae_u8)c8;
		break;
	case 2:
		{
			uae_u16 *p = (uae_u16*)buf + x;
			*p = (uae_u16)c8;
			break;
		}
	case 3:
		/* no 24 bit yet */
		break;
	case 4:
		{
			uae_u32 *p = (uae_u32*)buf + x;
			*p = c8;
			break;
		}
	}
}

#define lc(x) ledcolor (x, xredcolors, xgreencolors, xbluecolors, NULL)
#define DMARECORD_SUBITEMS 8
struct dmadebug
{
	uae_u32 l[DMARECORD_SUBITEMS];
	uae_u8 r, g, b;
	bool enabled;
	int max;
	const TCHAR *name;
};

static uae_u32 intlevc[] = { 0x000000, 0x444444, 0x008800, 0xffff00, 0x000088, 0x880000, 0xff0000, 0xffffff };
static struct dmadebug debug_colors[DMARECORD_MAX];
static bool debug_colors_set;

static void set_dbg_color(int index, int extra, uae_u8 r, uae_u8 g, uae_u8 b, int max, const TCHAR *name)
{
	if (extra == 0) {
		debug_colors[index].r = r;
		debug_colors[index].g = g;
		debug_colors[index].b = b;
		debug_colors[index].enabled = true;
	}
	if (name != NULL)
		debug_colors[index].name = name;
	if (max > 0)
		debug_colors[index].max = max;
	debug_colors[index].l[extra] = lc((r << 16) | (g << 8) | (b << 0));
}

static void set_debug_colors(void)
{
	if (debug_colors_set)
		return;
	debug_colors_set = true;
	set_dbg_color(0,						0, 0x22, 0x22, 0x22, 1, _T("-"));
	set_dbg_color(DMARECORD_REFRESH,		0, 0x44, 0x44, 0x44, 4, _T("Refresh"));
	set_dbg_color(DMARECORD_CPU,			0, 0xa2, 0x53, 0x42, 2, _T("CPU")); // code
	set_dbg_color(DMARECORD_COPPER,			0, 0xee, 0xee, 0x00, 3, _T("Copper"));
	set_dbg_color(DMARECORD_AUDIO,			0, 0xff, 0x00, 0x00, 4, _T("Audio"));
	set_dbg_color(DMARECORD_BLITTER,		0, 0x00, 0x88, 0x88, 2, _T("Blitter"));
	set_dbg_color(DMARECORD_BITPLANE,		0, 0x00, 0x00, 0xff, 8, _T("Bitplane"));
	set_dbg_color(DMARECORD_SPRITE,			0, 0xff, 0x00, 0xff, 8, _T("Sprite"));
	set_dbg_color(DMARECORD_DISK,			0, 0xff, 0xff, 0xff, 3, _T("Disk"));

	for (int i = 0; i < DMARECORD_MAX; i++) {
		for (int j = 1; j < DMARECORD_SUBITEMS; j++) {
			debug_colors[i].l[j] = debug_colors[i].l[0];
		}
	}

	set_dbg_color(DMARECORD_CPU,			1, 0xad, 0x98, 0xd6, 0, NULL); // data
	set_dbg_color(DMARECORD_COPPER,			1, 0xaa, 0xaa, 0x22, 0, NULL); // wait
	set_dbg_color(DMARECORD_COPPER,			2, 0x66, 0x66, 0x44, 0, NULL); // special
	set_dbg_color(DMARECORD_BLITTER,		1, 0x00, 0x88, 0xff, 0, NULL); // fill
	set_dbg_color(DMARECORD_BLITTER,		2, 0x00, 0xff, 0x00, 0, NULL); // line
}

static void debug_draw_cycles (uae_u8 *buf, int bpp, int line, int width, int height, uae_u32 *xredcolors, uae_u32 *xgreencolors, uae_u32 *xbluescolors)
{
	int y, x, xx, dx, xplus, yplus;
	struct dma_rec *dr;
	int t;

	if (debug_dma >= 4)
		yplus = 2;
	else
		yplus = 1;
	if (debug_dma >= 5)
		xplus = 3;
	else if (debug_dma >= 3)
		xplus = 2;
	else
		xplus = 1;

	t = dma_record_toggle ^ 1;
	y = line / yplus;
	if (yplus < 2)
		y -= 8;

	if (y < 0)
		return;
	if (y > maxvpos)
		return;
	if (y >= height)
		return;

	dx = width - xplus * ((maxhpos + 1) & ~1) - 16;

	uae_s8 intlev = 0;
	for (x = 0; x < maxhpos; x++) {
		uae_u32 c = debug_colors[0].l[0];
		xx = x * xplus + dx;
		dr = &dma_record[t][y * NR_DMA_REC_HPOS + x];
		if (dr->reg != 0xffff && debug_colors[dr->type].enabled) {
			c = debug_colors[dr->type].l[dr->extra];
		}
		if (dr->intlev > intlev)
			intlev = dr->intlev;
		putpixel (buf, bpp, xx + 4, c);
		if (xplus)
			putpixel (buf, bpp, xx + 4 + 1, c);
		if (debug_dma >= 6)
			putpixel (buf, bpp, xx + 4 + 2, c);
	}
	putpixel (buf, bpp, dx + 0, 0);
	putpixel (buf, bpp, dx + 1, lc(intlevc[intlev]));
	putpixel (buf, bpp, dx + 2, lc(intlevc[intlev]));
	putpixel (buf, bpp, dx + 3, 0);
}

#define HEATMAP_WIDTH 256
#define HEATMAP_HEIGHT 256
#define HEATMAP_COUNT 32
#define HEATMAP_DIV 8
static const int max_heatmap = 16 * 1048576; // 16M
static uae_u32 *heatmap_debug_colors;

static struct memory_heatmap *heatmap;
struct memory_heatmap
{
	uae_u32 mask;
	uae_u32 cpucnt;
	uae_u16 cnt;
	uae_u16 type, extra;
};

static void debug_draw_heatmap(uae_u8 *buf, int bpp, int line, int width, int height, uae_u32 *xredcolors, uae_u32 *xgreencolors, uae_u32 *xbluescolors)
{
	struct memory_heatmap *mht = heatmap;
	int dx = 16;
	int y = line;

	if (y < 0 || y >= HEATMAP_HEIGHT)
		return;

	mht += y * HEATMAP_WIDTH;

	for (int x = 0; x < HEATMAP_WIDTH; x++) {
		uae_u32 c = heatmap_debug_colors[mht->cnt * DMARECORD_MAX + mht->type];
		//c = heatmap_debug_colors[(HEATMAP_COUNT - 1) * DMARECORD_MAX + DMARECORD_CPU_I];
		int xx = x + dx;
		putpixel(buf, bpp, xx, c);
		if (mht->cnt > 0)
			mht->cnt--;
		mht++;
	}
}

void debug_draw(uae_u8 *buf, int bpp, int line, int width, int height, uae_u32 *xredcolors, uae_u32 *xgreencolors, uae_u32 *xbluescolors)
{
	if (!heatmap_debug_colors) {
		heatmap_debug_colors = xcalloc(uae_u32, DMARECORD_MAX * HEATMAP_COUNT);
		set_debug_colors();
		for (int i = 0; i < HEATMAP_COUNT; i++) {
			uae_u32 *cp = heatmap_debug_colors + i * DMARECORD_MAX;
			for (int j = 0; j < DMARECORD_MAX; j++) {
				uae_u8 r = debug_colors[j].r;
				uae_u8 g = debug_colors[j].g;
				uae_u8 b = debug_colors[j].b;
				r = r * i / HEATMAP_COUNT;
				g = g * i / HEATMAP_COUNT;
				b = b * i / HEATMAP_COUNT;
				cp[j] = lc((r << 16) | (g << 8) | (b << 0));
			}
		}
	}

	if (heatmap) {
		debug_draw_heatmap(buf, bpp, line, width, height, xredcolors, xgreencolors, xbluecolors);
	} else if (dma_record[0]) {
		debug_draw_cycles(buf, bpp, line, width, height, xredcolors, xgreencolors, xbluecolors);
	}
}

struct heatmapstore
{
	TCHAR *s;
	double v;
};

static void heatmap_stats(TCHAR **c)
{
	int range = 95;
	int maxlines = 30;
	double max;
	int maxcnt;
	uae_u32 mask = MW_MASK_CPU_I;
	const TCHAR *maskname = NULL;

	if (more_params(c)) {
		if (**c == 'c' && peek_next_char(c) == 0) {
			for (int i = 0; i < max_heatmap / HEATMAP_DIV; i++) {
				struct memory_heatmap *hm = &heatmap[i];
				memset(hm, 0, sizeof(struct memory_heatmap));
			}
			console_out(_T("heatmap data cleared\n"));
			return;
		}
		if (!isdigit(peek_next_char(c))) {
			TCHAR str[100];
			if (next_string(c, str, sizeof str / sizeof (TCHAR), true)) {
				for (int j = 0; memwatch_access_masks[j].mask; j++) {
					if (!_tcsicmp(str, memwatch_access_masks[j].name)) {
						mask = memwatch_access_masks[j].mask;
						maskname = memwatch_access_masks[j].name;
						console_out_f(_T("Mask %08x Name %s\n"), mask, maskname);
						break;
					}
				}
			}
			if (more_params(c)) {
				maxlines = readint(c);
			}
		} else {
			range = readint(c);
			if (more_params(c)) {
				maxlines = readint(c);
			}
		}
	}
	if (maxlines <= 0)
		maxlines = 10000;

	if (mask != MW_MASK_CPU_I) {

		int found = -1;
		int firstaddress = 0;
		for (int lines = 0; lines < maxlines; lines++) {

			for (; firstaddress < max_heatmap / HEATMAP_DIV; firstaddress++) {
				struct memory_heatmap *hm = &heatmap[firstaddress];
				if (hm->mask & mask)
					break;
			}

			if (firstaddress == max_heatmap / HEATMAP_DIV)
				return;

			int lastaddress;
			for (lastaddress = firstaddress; lastaddress < max_heatmap / HEATMAP_DIV; lastaddress++) {
				struct memory_heatmap *hm = &heatmap[lastaddress];
				if (!(hm->mask & mask))
					break;
			}
			lastaddress--;

			console_out_f(_T("%03d: %08x - %08x %08x (%d) %s\n"), 
				lines,
				firstaddress * HEATMAP_DIV, lastaddress * HEATMAP_DIV + HEATMAP_DIV - 1,
				lastaddress * HEATMAP_DIV - firstaddress * HEATMAP_DIV + HEATMAP_DIV - 1,
				lastaddress * HEATMAP_DIV - firstaddress * HEATMAP_DIV + HEATMAP_DIV - 1, 
				maskname);

			firstaddress = lastaddress + 1;
		}

	} else {
#define MAX_HEATMAP_LINES 1000
		struct heatmapstore linestore[MAX_HEATMAP_LINES] = { 0 };
		int storecnt = 0;
		uae_u32 maxlimit = 0xffffffff;

		max = 0;
		maxcnt = 0;
		for (int i = 0; i < max_heatmap / HEATMAP_DIV; i++) {
			struct memory_heatmap *hm = &heatmap[i];
			if (hm->cpucnt > 0) {
				max += hm->cpucnt;
				maxcnt++;
			}
		}

		if (!maxcnt) {
			console_out(_T("No CPU accesses found\n"));
			return;
		}

		for (int lines = 0; lines < maxlines; lines++) {

			int found = -1;
			int foundcnt = 0;

			for (int i = 0; i < max_heatmap / HEATMAP_DIV; i++) {
				struct memory_heatmap *hm = &heatmap[i];
				if (hm->cpucnt > 0 && hm->cpucnt > foundcnt && hm->cpucnt < maxlimit) {
					foundcnt = hm->cpucnt;
					found = i;
				}
			}
			if (found < 0)
				break;

			int totalcnt = 0;
			int cntrange = foundcnt * range / 100;
			if (cntrange <= 0)
				cntrange = 1;

			int lastaddress;
			for (lastaddress = found; lastaddress < max_heatmap / HEATMAP_DIV; lastaddress++) {
				struct memory_heatmap *hm = &heatmap[lastaddress];
				if (hm->cpucnt == 0 || hm->cpucnt < cntrange || hm->cpucnt >= maxlimit)
					break;
				totalcnt += hm->cpucnt;
			}
			lastaddress--;

			int firstaddress;
			for (firstaddress = found - 1; firstaddress >= 0; firstaddress--) {
				struct memory_heatmap *hm = &heatmap[firstaddress];
				if (hm->cpucnt == 0 || hm->cpucnt < cntrange || hm->cpucnt >= maxlimit)
					break;
				totalcnt += hm->cpucnt;
			}
			firstaddress--;

			firstaddress *= HEATMAP_DIV;
			lastaddress *= HEATMAP_DIV;

			TCHAR tmp[100];
			double pct = totalcnt / max * 100.0;
			_stprintf(tmp, _T("%03d: %08x - %08x %08x (%d) %.5f%%\n"), lines + 1,
				firstaddress, lastaddress + HEATMAP_DIV - 1,
				lastaddress - firstaddress + HEATMAP_DIV - 1,
				lastaddress - firstaddress + HEATMAP_DIV - 1,
				pct);
			linestore[storecnt].s = my_strdup(tmp);
			linestore[storecnt].v = pct;

			storecnt++;
			if (storecnt >= MAX_HEATMAP_LINES)
				break;

			maxlimit = foundcnt;
		}

		for (int lines1 = 0; lines1 < storecnt; lines1++) {
			for (int lines2 = lines1 + 1; lines2 < storecnt; lines2++) {
				if (linestore[lines1].v < linestore[lines2].v) {
					struct heatmapstore hms;
					memcpy(&hms, &linestore[lines1], sizeof(struct heatmapstore));
					memcpy(&linestore[lines1], &linestore[lines2], sizeof(struct heatmapstore));
					memcpy(&linestore[lines2], &hms, sizeof(struct heatmapstore));
				}
			}
		}
		for (int lines1 = 0; lines1 < storecnt; lines1++) {
			console_out(linestore[lines1].s);
			xfree(linestore[lines1].s);
		}

	}

}

static void free_heatmap(void)
{
	xfree(heatmap);
	heatmap = NULL;
	debug_heatmap = 0;
}

static void init_heatmap(void)
{
	if (!heatmap)
		heatmap = xcalloc(struct memory_heatmap, max_heatmap / HEATMAP_DIV);
}

static void memwatch_heatmap (uaecptr addr, int rwi, int size, uae_u32 accessmask)
{
	if (addr >= max_heatmap || !heatmap)
		return;
	struct memory_heatmap *hm = &heatmap[addr / HEATMAP_DIV];
	if (accessmask & MW_MASK_CPU_I) {
		hm->cpucnt++;
	}
	hm->cnt = HEATMAP_COUNT - 1;
	int type = 0;
	int extra = 0;
	for (int i = 0; i < 32; i++) {
		if (accessmask & (1 << i)) {
			switch (1 << i)
			{
				case MW_MASK_BPL_0:
				case MW_MASK_BPL_1:
				case MW_MASK_BPL_2:
				case MW_MASK_BPL_3:
				case MW_MASK_BPL_4:
				case MW_MASK_BPL_5:
				case MW_MASK_BPL_6:
				case MW_MASK_BPL_7:
				type = DMARECORD_BITPLANE;
				break;
				case MW_MASK_AUDIO_0:
				case MW_MASK_AUDIO_1:
				case MW_MASK_AUDIO_2:
				case MW_MASK_AUDIO_3:
				type = DMARECORD_AUDIO;
				break;
				case MW_MASK_BLITTER_A:
				case MW_MASK_BLITTER_B:
				case MW_MASK_BLITTER_C:
				case MW_MASK_BLITTER_D_N:
				case MW_MASK_BLITTER_D_F:
				case MW_MASK_BLITTER_D_L:
				type = DMARECORD_BLITTER;
				break;
				case MW_MASK_COPPER:
				type = DMARECORD_COPPER;
				break;
				case MW_MASK_DISK:
				type = DMARECORD_DISK;
				break;
				case MW_MASK_CPU_I:
				type = DMARECORD_CPU;
				break;
				case MW_MASK_CPU_D_R:
				case MW_MASK_CPU_D_W:
				type = DMARECORD_CPU;
				extra = 1;
				break;
			}
		}
	}
	hm->type = type;
	hm->extra = extra;
	hm->mask |= accessmask;
}

void record_dma_event (int evt, int hpos, int vpos)
{
	struct dma_rec *dr;

	if (!dma_record[0])
		return;
	if (hpos >= NR_DMA_REC_HPOS || vpos >= NR_DMA_REC_VPOS)
		return;
	dr = &dma_record[dma_record_toggle][vpos * NR_DMA_REC_HPOS + hpos];
	dr->evt |= evt;
}

void record_dma_replace(int hpos, int vpos, int type, int extra)
{
	struct dma_rec *dr;
	if (!dma_record[0])
		return;
	if (hpos >= NR_DMA_REC_HPOS || vpos >= NR_DMA_REC_VPOS)
		return;
	dr = &dma_record[dma_record_toggle][vpos * NR_DMA_REC_HPOS + hpos];
	if (dr->reg == 0xffff) {
		write_log(_T("DMA record replace without old data!\n"));
		return;
	}
	if (dr->type != type) {
		write_log(_T("DMA record replace type change %d -> %d!\n"), dr->type, type);
		return;
	}
	dr->extra = extra;
}

struct dma_rec *record_dma (uae_u16 reg, uae_u16 dat, uae_u32 addr, int hpos, int vpos, int type, int extra)
{
	struct dma_rec *dr;

	if (!dma_record[0]) {
		dma_record[0] = xmalloc (struct dma_rec, NR_DMA_REC_HPOS * NR_DMA_REC_VPOS);
		dma_record[1] = xmalloc (struct dma_rec, NR_DMA_REC_HPOS * NR_DMA_REC_VPOS);
		dma_record_toggle = 0;
		record_dma_reset ();
		dma_record_frame[0] = -1;
		dma_record_frame[1] = -1;
	}

	if (hpos >= NR_DMA_REC_HPOS || vpos >= NR_DMA_REC_VPOS)
		return NULL;

	dr = &dma_record[dma_record_toggle][vpos * NR_DMA_REC_HPOS + hpos];
	dma_record_frame[dma_record_toggle] = timeframes;
	if (dr->reg != 0xffff) {
		write_log (_T("DMA conflict: v=%d h=%d OREG=%04X NREG=%04X\n"), vpos, hpos, dr->reg, reg);
		return dr;
	}
	dr->reg = reg;
	dr->dat = dat;
	dr->addr = addr;
	dr->type = type;
	dr->extra = extra;
	dr->intlev = regs.intmask;
	return dr;
}


static bool get_record_dma_info(struct dma_rec *dr, int hpos, int vpos, uae_u32 cycles, TCHAR *l1, TCHAR *l2, TCHAR *l3, TCHAR *l4, TCHAR *l5)
{
	bool longsize = false;
	bool got = false;
	int r = dr->reg;
	const TCHAR *sr;

	if (l1)
		l1[0] = 0;
	if (l2)
		l2[0] = 0;
	if (l3)
		l3[0] = 0;
	if (l4)
		l4[0] = 0;
	if (l5)
		l5[0] = 0;

	if (dr->type != 0 || dr->reg != 0xffff || dr->evt)
		got = true;

	sr = _T("    ");
	if (dr->type == DMARECORD_COPPER) {
		sr = _T("COP ");
	} else if (dr->type == DMARECORD_BLITTER) {
		if (dr->extra == 2)
			sr = _T("BLL ");
		else
			sr = _T("BLT ");
	} else if (dr->type == DMARECORD_REFRESH) {
		sr = _T("RFS ");
	} else if (dr->type == DMARECORD_AUDIO) {
		sr = _T("AUD ");
	} else if (dr->type == DMARECORD_DISK) {
		sr = _T("DSK ");
	} else if (dr->type == DMARECORD_SPRITE) {
		sr = _T("SPR ");
	}
	_stprintf (l1, _T("[%02X %3d]"), hpos, hpos);
	if (l4) {
		_tcscpy (l4, _T("        "));
	}
	if (r != 0xffff) {
		if (r & 0x1000) {
			if ((r & 0x0100) == 0x0000)
				_tcscpy (l2, _T("CPU-R "));
			else if ((r & 0x0100) == 0x0100)
				_tcscpy (l2, _T("CPU-W "));
			if ((r & 0xff) == 4) {
				l2[5] = 'L';
				longsize = true;
			}
			if ((r & 0xff) == 2)
				l2[5] = 'W';
			if ((r & 0xff) == 1)
				l2[5] = 'B';
		} else {
			_stprintf (l2, _T("%4s %03X"), sr, r);
		}
		if (l3) {
			_stprintf (l3, longsize ? _T("%08X") : _T("    %04X"), dr->dat);
		}
		if (l4 && dr->addr != 0xffffffff)
			_stprintf (l4, _T("%08X"), dr->addr & 0x00ffffff);
	} else {
		_tcscpy (l2, _T("        "));
		if (l3) {
			_tcscpy (l3, _T("        "));
		}
	}
	if (l3) {
		int cl2 = 0;
		if (dr->evt & DMA_EVENT_BLITNASTY)
			l3[cl2++] = 'N';
		if (dr->evt & DMA_EVENT_BLITSTARTFINISH)
			l3[cl2++] = 'B';
		if (dr->evt & DMA_EVENT_BLITIRQ)
			l3[cl2++] = 'b';
		if (dr->evt & DMA_EVENT_BPLFETCHUPDATE)
			l3[cl2++] = 'p';
		if (dr->evt & DMA_EVENT_COPPERWAKE)
			l3[cl2++] = 'W';
		if (dr->evt & DMA_EVENT_NOONEGETS) {
			l3[cl2++] = '#';
		} else if (dr->evt & DMA_EVENT_COPPERWANTED) {
			l3[cl2++] = 'c';
		}
		if (dr->evt & DMA_EVENT_CPUIRQ)
			l3[cl2++] = 'I';
		if (dr->evt & DMA_EVENT_INTREQ)
			l3[cl2++] = 'i';
		if (dr->evt & DMA_EVENT_SPECIAL)
			l3[cl2++] = 'X';
	}
	if (l5) {
		_stprintf (l5, _T("%08X"), cycles + (vpos * maxhpos + hpos) * CYCLE_UNIT);
	}
	return got;
}



static void decode_dma_record (int hpos, int vpos, int toggle, bool logfile)
{
	struct dma_rec *dr;
	int h, i, maxh;
	uae_u32 cycles;

	if (!dma_record[0] || hpos < 0 || vpos < 0)
		return;
	dr = &dma_record[dma_record_toggle ^ toggle][vpos * NR_DMA_REC_HPOS];
	if (logfile)
		write_dlog (_T("Line: %02X %3d HPOS %02X %3d:\n"), vpos, vpos, hpos, hpos);
	else
		console_out_f (_T("Line: %02X %3d HPOS %02X %3d:\n"), vpos, vpos, hpos, hpos);
	h = hpos;
	dr += hpos;
	maxh = hpos + 80;
	if (maxh > maxhpos)
		maxh = maxhpos;
	cycles = vsync_cycles;
	if (toggle)
		cycles -= maxvpos * maxhpos * CYCLE_UNIT;
	while (h < maxh) {
		int col = 9;
		int cols = 8;
		TCHAR l1[81];
		TCHAR l2[81];
		TCHAR l3[81];
		TCHAR l4[81];
		TCHAR l5[81];
		l1[0] = 0;
		l2[0] = 0;
		l3[0] = 0;
		l4[0] = 0;
		l5[0] = 0;
		for (i = 0; i < cols && h < maxh; i++, h++, dr++) {
			TCHAR l1l[16], l2l[16], l3l[16], l4l[16], l5l[16];

			get_record_dma_info(dr, h, vpos, cycles, l1l, l2l, l3l, l4l, l5l);

			TCHAR *p = l1 + _tcslen(l1);
			_stprintf(p, _T("%9s "), l1l);
			p = l2 + _tcslen(l2);
			_stprintf(p, _T("%9s "), l2l);
			p = l3 + _tcslen(l3);
			_stprintf(p, _T("%9s "), l3l);
			p = l4 + _tcslen(l4);
			_stprintf(p, _T("%9s "), l4l);
			p = l5 + _tcslen(l5);
			_stprintf(p, _T("%9s "), l5l);
		}
		if (logfile) {
			write_dlog (_T("%s\n"), l1);
			write_dlog (_T("%s\n"), l2);
			write_dlog (_T("%s\n"), l3);
			write_dlog (_T("%s\n"), l4);
			write_dlog (_T("%s\n"), l5);
			write_dlog (_T("\n"));
		} else {
			console_out_f (_T("%s\n"), l1);
			console_out_f (_T("%s\n"), l2);
			console_out_f (_T("%s\n"), l3);
			console_out_f (_T("%s\n"), l4);
			console_out_f (_T("%s\n"), l5);
			console_out_f (_T("\n"));
		}
	}
}

void log_dma_record (void)
{
	if (!input_record && !input_play)
		return;
	if (!debug_dma)
		debug_dma = 1;
	decode_dma_record (0, 0, 0, true);
}

static void init_record_copper(void)
{
	if (!cop_record[0]) {
		cop_record[0] = xmalloc(struct cop_rec, NR_COPPER_RECORDS);
		cop_record[1] = xmalloc(struct cop_rec, NR_COPPER_RECORDS);
	}
}

void record_copper_blitwait (uaecptr addr, int hpos, int vpos)
{
	int t = nr_cop_records[curr_cop_set];
	init_record_copper();
	cop_record[curr_cop_set][t].bhpos = hpos;
	cop_record[curr_cop_set][t].bvpos = vpos;
}

void record_copper (uaecptr addr, uae_u16 word1, uae_u16 word2, int hpos, int vpos)
{
	int t = nr_cop_records[curr_cop_set];
	init_record_copper();
	if (t < NR_COPPER_RECORDS) {
		cop_record[curr_cop_set][t].addr = addr;
		cop_record[curr_cop_set][t].w1 = word1;
		cop_record[curr_cop_set][t].w2 = word2;
		cop_record[curr_cop_set][t].hpos = hpos;
		cop_record[curr_cop_set][t].vpos = vpos;
		cop_record[curr_cop_set][t].bvpos = -1;
		nr_cop_records[curr_cop_set] = t + 1;
	}
	if (debug_copper & 2) { /* trace */
		debug_copper &= ~2;
		activate_debugger_new();
	}
	if ((debug_copper & 4) && addr >= debug_copper_pc && addr <= debug_copper_pc + 3) {
		debug_copper &= ~4;
		activate_debugger_new();
	}
}

static struct cop_rec *find_copper_records (uaecptr addr)
{
	int s = curr_cop_set ^ 1;
	int t = nr_cop_records[s];
	int i;
	for (i = 0; i < t; i++) {
		if (cop_record[s][i].addr == addr)
			return &cop_record[s][i];
	}
	return 0;
}

/* simple decode copper by Mark Cox */
static void decode_copper_insn (FILE* file, uae_u16 mword1, uae_u16 mword2, unsigned long addr)
{
	struct cop_rec *cr = NULL;
	uae_u32 insn_type, insn;
	TCHAR here = ' ';
	TCHAR record[] = _T("          ");

	if ((cr = find_copper_records (addr))) {
		_stprintf (record, _T(" [%03x %03x]"), cr->vpos, cr->hpos);
		insn = (cr->w1 << 16) | cr->w2;
	} else {
		insn = (mword1 << 16) | mword2;
	}

	insn_type = insn & 0x00010001;

	if (get_copper_address (-1) >= addr && get_copper_address(-1) <= addr + 3)
		here = '*';

	console_out_f (_T("%c%08x: %04x %04x%s\t;%c "), here, addr, insn >> 16, insn & 0xFFFF, record, insn != ((mword1 << 16) | mword2) ? '!' : ' ');

	switch (insn_type) {
	case 0x00010000: /* WAIT insn */
		console_out (_T("Wait for "));
		disassemble_wait (file, insn);

		if (insn == 0xfffffffe)
			console_out (_T("                           \t;  End of Copperlist\n"));

		break;

	case 0x00010001: /* SKIP insn */
		console_out (_T("Skip if "));
		disassemble_wait (file, insn);
		break;

	case 0x00000000:
	case 0x00000001: /* MOVE insn */
		{
			int addr = (insn >> 16) & 0x1fe;
			int i = 0;
			while (custd[i].name) {
				if (custd[i].adr == addr + 0xdff000)
					break;
				i++;
			}
			if (custd[i].name)
				console_out_f (_T("%s := 0x%04x\n"), custd[i].name, insn & 0xffff);
			else
				console_out_f (_T("%04x := 0x%04x\n"), addr, insn & 0xffff);
		}
		break;

	default:
		abort ();
	}

	if (cr && cr->bvpos >= 0) {
		console_out_f (_T("                 BLT [%03x %03x]\n"), cr->bvpos, cr->bhpos);
	}
}

static uaecptr decode_copperlist (FILE* file, uaecptr address, int nolines)
{
	while (nolines-- > 0) {
		decode_copper_insn (file, chipmem_wget_indirect (address), chipmem_wget_indirect (address + 2), address);
		address += 4;
	}
	return address;
	/* You may wonder why I don't stop this at the end of the copperlist?
	* Well, often nice things are hidden at the end and it is debatable the actual
	* values that mean the end of the copperlist */
}

static int copper_debugger (TCHAR **c)
{
	static uaecptr nxcopper;
	uae_u32 maddr;
	int lines;

	if (**c == 'd') {
		next_char (c);
		if (debug_copper)
			debug_copper = 0;
		else
			debug_copper = 1;
		console_out_f (_T("Copper debugger %s.\n"), debug_copper ? _T("enabled") : _T("disabled"));
	} else if (**c == 't') {
		debug_copper = 1|2;
		return 1;
	} else if (**c == 'b') {
		(*c)++;
		debug_copper = 1|4;
		if (more_params (c)) {
			debug_copper_pc = readhex (c);
			console_out_f (_T("Copper breakpoint @0x%08x\n"), debug_copper_pc);
		} else {
			debug_copper &= ~4;
		}
	} else {
		if (more_params (c)) {
			maddr = readhex (c);
			if (maddr == 1 || maddr == 2)
				maddr = get_copper_address (maddr);
			else if (maddr == 0)
				maddr = get_copper_address (-1);
		} else
			maddr = nxcopper;

		if (more_params (c))
			lines = readhex (c);
		else
			lines = 20;

		nxcopper = decode_copperlist (stdout, maddr, lines);
	}
	return 0;
}

#define MAX_CHEAT_VIEW 100
struct trainerstruct {
	uaecptr addr;
	int size;
};

static struct trainerstruct *trainerdata;
static int totaltrainers;

static void clearcheater(void)
{
	if (!trainerdata)
		trainerdata =  xmalloc(struct trainerstruct, MAX_CHEAT_VIEW);
	memset(trainerdata, 0, sizeof (struct trainerstruct) * MAX_CHEAT_VIEW);
	totaltrainers = 0;
}
static int addcheater(uaecptr addr, int size)
{
	if (totaltrainers >= MAX_CHEAT_VIEW)
		return 0;
	trainerdata[totaltrainers].addr = addr;
	trainerdata[totaltrainers].size = size;
	totaltrainers++;
	return 1;
}
static void listcheater(int mode, int size)
{
	int i, skip;

	if (!trainerdata)
		return;
	if (mode)
		skip = 6;
	else
		skip = 8;
	for(i = 0; i < totaltrainers; i++) {
		struct trainerstruct *ts = &trainerdata[i];
		uae_u16 b;

		if (size) {
			b = get_byte_debug (ts->addr);
		} else {
			b = get_word_debug (ts->addr);
		}
		if (mode)
			console_out_f (_T("%08X=%04X "), ts->addr, b);
		else
			console_out_f (_T("%08X "), ts->addr);
		if ((i % skip) == skip)
			console_out (_T("\n"));
	}
}

static void deepcheatsearch (TCHAR **c)
{
	static int first = 1;
	static uae_u8 *memtmp;
	static int memsize, memsize2;
	uae_u8 *p1, *p2;
	uaecptr addr, end;
	int i, wasmodified, nonmodified;
	static int size;
	static int inconly, deconly, maxdiff;
	int addrcnt, cnt;
	TCHAR v;

	v = _totupper (**c);

	if(!memtmp || v == 'S') {
		maxdiff = 0x10000;
		inconly = 0;
		deconly = 0;
		size = 1;
	}

	if (**c)
		(*c)++;
	ignore_ws (c);
	if ((**c) == '1' || (**c) == '2') {
		size = **c - '0';
		(*c)++;
	}
	if (more_params (c))
		maxdiff = readint (c);

	if (!memtmp || v == 'S') {
		first = 1;
		xfree (memtmp);
		memsize = 0;
		addr = 0xffffffff;
		while ((addr = nextaddr (addr, 0, &end)) != 0xffffffff)  {
			memsize += end - addr;
			addr = end - 1;
		}
		memsize2 = (memsize + 7) / 8;
		memtmp = xmalloc (uae_u8, memsize + memsize2);
		if (!memtmp)
			return;
		memset (memtmp + memsize, 0xff, memsize2);
		p1 = memtmp;
		addr = 0xffffffff;
		while ((addr = nextaddr (addr, 0, &end)) != 0xffffffff) {
			for (i = addr; i < end; i++)
				*p1++ = get_byte_debug (i);
			addr = end - 1;
		}
		console_out (_T("Deep trainer first pass complete.\n"));
		return;
	}
	inconly = deconly = 0;
	wasmodified = v == 'X' ? 0 : 1;
	nonmodified = v == 'Z' ? 1 : 0;
	if (v == 'I')
		inconly = 1;
	if (v == 'D')
		deconly = 1;
	p1 = memtmp;
	p2 = memtmp + memsize;
	addrcnt = 0;
	cnt = 0;
	addr = 0xffffffff;
	while ((addr = nextaddr (addr, 0, NULL)) != 0xffffffff) {
		uae_s32 b, b2;
		int doremove = 0;
		int addroff = addrcnt >> 3;
		int addrmask ;

		if (size == 1) {
			b = (uae_s8)get_byte_debug (addr);
			b2 = (uae_s8)p1[addrcnt];
			addrmask = 1 << (addrcnt & 7);
		} else {
			b = (uae_s16)get_word_debug (addr);
			b2 = (uae_s16)((p1[addrcnt] << 8) | p1[addrcnt + 1]);
			addrmask = 3 << (addrcnt & 7);
		}

		if (p2[addroff] & addrmask) {
			if (wasmodified && !nonmodified) {
				int diff = b - b2;
				if (b == b2)
					doremove = 1;
				if (abs(diff) > maxdiff)
					doremove = 1;
				if (inconly && diff < 0)
					doremove = 1;
				if (deconly && diff > 0)
					doremove = 1;
			} else if (nonmodified && b == b2) {
				doremove = 1;
			} else if (!wasmodified && b != b2) {
				doremove = 1;
			}
			if (doremove)
				p2[addroff] &= ~addrmask;
			else
				cnt++;
		}
		if (size == 1) {
			p1[addrcnt] = b;
			addrcnt++;
		} else {
			p1[addrcnt] = b >> 8;
			p1[addrcnt + 1] = b >> 0;
			addr = nextaddr (addr, 0, NULL);
			if (addr == 0xffffffff)
				break;
			addrcnt += 2;
		}
		if  (iscancel (65536)) {
			console_out_f (_T("Aborted at %08X\n"), addr);
			break;
		}
	}

	console_out_f (_T("%d addresses found\n"), cnt);
	if (cnt <= MAX_CHEAT_VIEW) {
		clearcheater ();
		cnt = 0;
		addrcnt = 0;
		addr = 0xffffffff;
		while ((addr = nextaddr(addr, 0, NULL)) != 0xffffffff) {
			int addroff = addrcnt >> 3;
			int addrmask = (size == 1 ? 1 : 3) << (addrcnt & 7);
			if (p2[addroff] & addrmask)
				addcheater (addr, size);
			addrcnt += size;
			cnt++;
		}
		if (cnt > 0)
			console_out (_T("\n"));
		listcheater (1, size);
	} else {
		console_out (_T("Now continue with 'g' and use 'D' again after you have lost another life\n"));
	}
}

/* cheat-search by Toni Wilen (originally by Holger Jakob) */
static void cheatsearch (TCHAR **c)
{
	static uae_u8 *vlist;
	static int listsize;
	static int first = 1;
	static int size = 1;
	uae_u32 val, memcnt, prevmemcnt;
	int i, count, vcnt, memsize;
	uaecptr addr, end;

	memsize = 0;
	addr = 0xffffffff;
	while ((addr = nextaddr (addr, 0, &end)) != 0xffffffff)  {
		memsize += end - addr;
		addr = end - 1;
	}

	if (_totupper (**c) == 'L') {
		listcheater (1, size);
		return;
	}
	ignore_ws (c);
	if (!more_params (c)) {
		first = 1;
		console_out (_T("Search reset\n"));
		xfree (vlist);
		listsize = memsize;
		vlist = xcalloc (uae_u8, listsize >> 3);
		return;
	}
	if (first)
		val = readint (c, &size);
	else
		val = readint (c);

	if (vlist == NULL) {
		listsize = memsize;
		vlist = xcalloc (uae_u8, listsize >> 3);
	}

	count = 0;
	vcnt = 0;

	clearcheater ();
	addr = 0xffffffff;
	prevmemcnt = memcnt = 0;
	while ((addr = nextaddr (addr, 0, &end)) != 0xffffffff) {
		if (addr + size < end) {
			for (i = 0; i < size; i++) {
				int shift = (size - i - 1) * 8;
				if (get_byte_debug (addr + i) != ((val >> shift) & 0xff))
					break;
			}
			if (i == size) {
				int voffset = memcnt >> 3;
				int vmask = 1 << (memcnt & 7);
				if (!first) {
					while (prevmemcnt < memcnt) {
						vlist[prevmemcnt >> 3] &= ~(1 << (prevmemcnt & 7));
						prevmemcnt++;
					}
					if (vlist[voffset] & vmask) {
						count++;
						addcheater(addr, size);
					} else {
						vlist[voffset] &= ~vmask;
					}
					prevmemcnt = memcnt + 1;
				} else {
					vlist[voffset] |= vmask;
					count++;
				}
			}
		}
		memcnt++;
		if  (iscancel (65536)) {
			console_out_f (_T("Aborted at %08X\n"), addr);
			break;
		}
	}
	if (!first) {
		while (prevmemcnt < memcnt) {
			vlist[prevmemcnt >> 3] &= ~(1 << (prevmemcnt & 7));
			prevmemcnt++;
		}
		listcheater (0, size);
	}
	console_out_f (_T("Found %d possible addresses with 0x%X (%u) (%d bytes)\n"), count, val, val, size);
	if (count > 0)
		console_out (_T("Now continue with 'g' and use 'C' with a different value\n"));
	first = 0;
}

struct breakpoint_node bpnodes[BREAKPOINT_TOTAL];
static addrbank **debug_mem_banks;
static addrbank *debug_mem_area;
struct memwatch_node mwnodes[MEMWATCH_TOTAL];
static int mwnodes_start, mwnodes_end;
static struct memwatch_node mwhit;

#define MUNGWALL_SLOTS 16
struct mungwall_data
{
	int slots;
	uae_u32 start[MUNGWALL_SLOTS], end[MUNGWALL_SLOTS];
};
static struct mungwall_data **mungwall;

static uae_u8 *illgdebug, *illghdebug;
static int illgdebug_break;

static void illg_free (void)
{
	xfree (illgdebug);
	illgdebug = NULL;
	xfree (illghdebug);
	illghdebug = NULL;
}

static void illg_init (void)
{
	int i;
	uae_u8 c = 3;
	uaecptr addr, end;

	illgdebug = xcalloc (uae_u8, 0x01000000);
	illghdebug = xcalloc (uae_u8, 65536);
	if (!illgdebug || !illghdebug) {
		illg_free();
		return;
	}
	addr = 0xffffffff;
	while ((addr = nextaddr (addr, 0, &end)) != 0xffffffff)  {
		if (end < 0x01000000) {
			memset (illgdebug + addr, c, end - addr);
		} else {
			uae_u32 s = addr >> 16;
			uae_u32 e = end >> 16;
			memset (illghdebug + s, c, e - s);
		}
		addr = end - 1;
	}
	for (int i = 0; i < MAX_RTG_BOARDS; i++) {
		if (currprefs.rtgboards[i].rtgmem_size)
			memset (illghdebug + (gfxmem_banks[i]->start >> 16), 3, currprefs.rtgboards[i].rtgmem_size >> 16);
	}

	i = 0;
	while (custd[i].name) {
		int rw = (custd[i].special & CD_WO) ? 2 : 1;
		illgdebug[custd[i].adr] = rw;
		illgdebug[custd[i].adr + 1] = rw;
		i++;
	}
	for (i = 0; i < 16; i++) { /* CIAs */
		if (i == 11)
			continue;
		illgdebug[0xbfe001 + i * 0x100] = c;
		illgdebug[0xbfd000 + i * 0x100] = c;
	}
	memset (illgdebug + 0xf80000, 1, 512 * 1024); /* KS ROM */
	memset (illgdebug + 0xdc0000, c, 0x3f); /* clock */
#ifdef CDTV
	if (currprefs.cs_cdtvram) {
		memset (illgdebug + 0xdc8000, c, 4096); /* CDTV batt RAM */
		memset (illgdebug + 0xf00000, 1, 256 * 1024); /* CDTV ext ROM */
	}
#endif
#ifdef CD32
	if (currprefs.cs_cd32cd) {
		memset (illgdebug + AKIKO_BASE, c, AKIKO_BASE_END - AKIKO_BASE);
		memset (illgdebug + 0xe00000, 1, 512 * 1024); /* CD32 ext ROM */
	}
#endif
	if (currprefs.cs_ksmirror_e0)
		memset (illgdebug + 0xe00000, 1, 512 * 1024);
	if (currprefs.cs_ksmirror_a8)
		memset (illgdebug + 0xa80000, 1, 2 * 512 * 1024);
#ifdef FILESYS
	if (uae_boot_rom_type) /* filesys "rom" */
		memset (illgdebug + rtarea_base, 1, 0x10000);
#endif
	if (currprefs.cs_ide > 0)
		memset (illgdebug + 0xdd0000, 3, 65536);
}

/* add special custom register check here */
static void illg_debug_check (uaecptr addr, int rwi, int size, uae_u32 val)
{
	return;
}

static void illg_debug_do (uaecptr addr, int rwi, int size, uae_u32 val)
{
	uae_u8 mask;
	uae_u32 pc = m68k_getpc ();
	int i;

	for (i = size - 1; i >= 0; i--) {
		uae_u8 v = val >> (i * 8);
		uae_u32 ad = addr + i;
		if (ad >= 0x01000000)
			mask = illghdebug[ad >> 16];
		else
			mask = illgdebug[ad];
		if ((mask & 3) == 3)
			return;
		if (mask & 0x80) {
			illg_debug_check (ad, rwi, size, val);
		} else if ((mask & 3) == 0) {
			if (rwi & 2)
				console_out_f (_T("W: %08X=%02X PC=%08X\n"), ad, v, pc);
			else if (rwi & 1)
				console_out_f (_T("R: %08X    PC=%08X\n"), ad, pc);
			if (illgdebug_break)
				activate_debugger_new();
		} else if (!(mask & 1) && (rwi & 1)) {
			console_out_f (_T("RO: %08X=%02X PC=%08X\n"), ad, v, pc);
			if (illgdebug_break)
				activate_debugger_new();
		} else if (!(mask & 2) && (rwi & 2)) {
			console_out_f (_T("WO: %08X    PC=%08X\n"), ad, pc);
			if (illgdebug_break)
				activate_debugger_new();
		}
	}
}

static int debug_mem_off (uaecptr *addrp)
{
	uaecptr addr = *addrp;
	addrbank *ba;
	int offset = munge24 (addr) >> 16;
	if (!debug_mem_banks)
		return offset;
	ba = debug_mem_banks[offset];
	if (!ba)
		return offset;
	if (ba->mask || ba->startmask) {
		uae_u32 start = ba->startmask ? ba->startmask : ba->start;
		addr -= start;
		addr &= ba->mask;
		addr += start;
	}
	*addrp = addr;
	return offset;
}

struct smc_item {
	uae_u32 addr;
	uae_u8 cnt;
};

static int smc_size, smc_mode;
static struct smc_item *smc_table;

static void smc_free (void)
{
	if (smc_table)
		console_out (_T("SMCD disabled\n"));
	xfree(smc_table);
	smc_mode = 0;
	smc_table = NULL;
}

static void initialize_memwatch (int mode);
static void smc_detect_init (TCHAR **c)
{
	int v, i;

	ignore_ws (c);
	v = readint (c);
	smc_free ();
	smc_size = 1 << 24;
	if (currprefs.z3fastmem[0].size)
		smc_size = currprefs.z3autoconfig_start + currprefs.z3fastmem[0].size;
	smc_size += 4;
	smc_table = xmalloc (struct smc_item, smc_size);
	if (!smc_table)
		return;
	for (i = 0; i < smc_size; i++) {
		smc_table[i].addr = 0xffffffff;
		smc_table[i].cnt = 0;
	}
	if (!memwatch_enabled)
		initialize_memwatch (0);
	if (v)
		smc_mode = 1;
	console_out_f (_T("SMCD enabled. Break=%d\n"), smc_mode);
}

#define SMC_MAXHITS 8
static void smc_detector (uaecptr addr, int rwi, int size, uae_u32 *valp)
{
	int i, hitcnt;
	uaecptr hitaddr, hitpc;

	if (!smc_table)
		return;
	if (addr >= smc_size)
		return;
	if (rwi == 2) {
		for (i = 0; i < size; i++) {
			if (smc_table[addr + i].cnt < SMC_MAXHITS) {
				smc_table[addr + i].addr = m68k_getpc ();
			}
		}
		return;
	}
	hitpc = smc_table[addr].addr;
	if (hitpc == 0xffffffff)
		return;
	hitaddr = addr;
	hitcnt = 0;
	while (addr < smc_size && smc_table[addr].addr != 0xffffffff) {
		smc_table[addr++].addr = 0xffffffff;
		hitcnt++;
	}
	if ((hitpc & 0xFFF80000) == 0xF80000)
		return;
	if (currprefs.cpu_model == 68000 && currprefs.cpu_compatible) {
		/* ignore single-word unconditional jump instructions
		* (instruction prefetch from PC+2 can cause false positives) */
		if (regs.irc == 0x4e75 || regs.irc == 4e74 || regs.irc == 0x4e72 || regs.irc == 0x4e77)
			return; /* RTS, RTD, RTE, RTR */
		if ((regs.irc & 0xff00) == 0x6000 && (regs.irc & 0x00ff) != 0 && (regs.irc & 0x00ff) != 0xff)
			return; /* BRA.B */
	}
	if (hitcnt < 100) {
		smc_table[hitaddr].cnt++;
		console_out_f (_T("SMC at %08X - %08X (%d) from %08X\n"),
			hitaddr, hitaddr + hitcnt, hitcnt, hitpc);
		if (smc_mode)
			activate_debugger_new();
		if (smc_table[hitaddr].cnt >= SMC_MAXHITS)
			console_out_f (_T("* hit count >= %d, future hits ignored\n"), SMC_MAXHITS);
	}
}

uae_u8 *save_debug_memwatch (int *len, uae_u8 *dstptr)
{
	uae_u8 *dstbak, *dst;
	int total;

	total = 0;
	for (int i = 0; i < MEMWATCH_TOTAL; i++) {
		if (mwnodes[i].size > 0)
			total++;
	}
	if (!total)
		return NULL;

	if (dstptr)
		dstbak = dst = dstptr;
	else
		dstbak = dst = xmalloc (uae_u8, 1000);
	save_u32 (1);
	save_u8 (total);
	for (int i = 0; i < MEMWATCH_TOTAL; i++) {
		struct memwatch_node *m = &mwnodes[i];
		if (m->size <= 0)
			continue;
		save_store_pos ();
		save_u8 (i);
		save_u8 (m->modval_written);
		save_u8 (m->mustchange);
		save_u8 (m->frozen);
		save_u8 (m->val_enabled);
		save_u8 (m->rwi);
		save_u32 (m->addr);
		save_u32 (m->size);
		save_u32 (m->modval);
		save_u32 (m->val_mask);
		save_u32 (m->val_size);
		save_u32 (m->val);
		save_u32 (m->pc);
		save_u32 (m->access_mask);
		save_u32 (m->reg);
		save_u8(m->nobreak);
		save_u8(m->reportonly);
		save_store_size ();
	}
	*len = dst - dstbak;
	return dstbak;
}

uae_u8 *restore_debug_memwatch (uae_u8 *src)
{
	if (restore_u32 () != 1)
		return src;
	int total = restore_u8 ();
	for (int i = 0; i < total; i++) {
		restore_store_pos ();
		int idx = restore_u8 ();
		struct memwatch_node *m = &mwnodes[idx];
		m->modval_written = restore_u8 ();
		m->mustchange = restore_u8 ();
		m->frozen = restore_u8 ();
		m->val_enabled = restore_u8 ();
		m->rwi = restore_u8 ();
		m->addr = restore_u32 ();
		m->size = restore_u32 ();
		m->modval = restore_u32 ();
		m->val_mask = restore_u32 ();
		m->val_size = restore_u32 ();
		m->val = restore_u32 ();
		m->pc = restore_u32 ();
		m->access_mask = restore_u32();
		m->reg = restore_u32();
		m->nobreak = restore_u8();
		m->reportonly = restore_u8();
		restore_store_size ();
	}
	return src;
}

void restore_debug_memwatch_finish (void)
{
	for (int i = 0; i < MEMWATCH_TOTAL; i++) {
		struct memwatch_node *m = &mwnodes[i];
		if (m->size) {
			if (!memwatch_enabled)
				initialize_memwatch (0);
			return;
		}
	}
}

void debug_check_reg(uae_u32 addr, int write, uae_u16 v)
{
	if (!memwatch_access_validator)
		return;
	int reg = addr & 0x1ff;
	const struct customData *cd = &custd[reg >> 1];

	if (((addr & 0xfe00) != 0xf000 && (addr & 0xffff0000) != 0) || ((addr & 0xffff0000) != 0 && (addr & 0xffff0000) != 0x00df0000) || (addr & 0x0600)) {
		write_log(_T("Mirror custom register %08x (%s) %s access. PC=%08x\n"), addr, cd->name, write ? _T("write") : _T("read"), M68K_GETPC);
	}

	int spc = cd->special;
	if ((spc & CD_AGA) && !(currprefs.chipset_mask & CSMASK_AGA))
		spc |= CD_NONE;
	if ((spc & CD_ECS_DENISE) && !(currprefs.chipset_mask & CSMASK_ECS_DENISE))
		spc |= CD_NONE;
	if ((spc & CD_ECS_AGNUS) && !(currprefs.chipset_mask & CSMASK_ECS_AGNUS))
		spc |= CD_NONE;
	if (spc & CD_NONE) {
		write_log(_T("Non-existing custom register %04x (%s) %s access. PC=%08x\n"), reg, cd->name, write ? _T("write") : _T("read"), M68K_GETPC);
		return;
	}

	if (spc & CD_COLOR) {
		if (currprefs.chipset_mask & CSMASK_AGA)
			return;
	}

	if (write & !(spc & CD_WO)) {
		write_log(_T("Write access to read-only custom register %04x (%s). PC=%08x\n"), reg, cd->name, M68K_GETPC);
		return;
	} else if (!write && (spc & CD_WO)) {
		write_log(_T("Read access from write-only custom register %04x (%s). PC=%08x\n"), reg, cd->name, M68K_GETPC);
		return;
	}

	if (write && cd->mask[2]) {
		int idx = (currprefs.chipset_mask & CSMASK_AGA) ? 2 : (currprefs.chipset_mask & CSMASK_ECS_AGNUS) ? 1 : 0;
		uae_u16 mask = cd->mask[idx];
		if (v & ~mask) {
			write_log(_T("Unuset bits set %04x when writing custom register %04x (%s) PC=%08x\n"), v & ~mask, reg, cd->name, M68K_GETPC);
		}
	}

	if (spc & CD_DMA_PTR) {
		uae_u32 addr = (custom_storage[((reg & ~2) >> 1)].value << 16) | custom_storage[((reg | 2) >> 1)].value;
		if (currprefs.z3chipmem_size) {
			if (addr >= currprefs.z3chipmem_start && addr < currprefs.z3chipmem_start + currprefs.z3chipmem_size)
				return;
		}
		if(addr >= currprefs.chipmem_size)
			write_log(_T("DMA pointer %04x (%s) set to invalid value %08x %s=%08x\n"), reg, cd->name, addr,
				custom_storage[reg >> 1].pc & 1 ? _T("COP") : _T("PC"), custom_storage[reg >> 1].pc);
	}
}

void debug_invalid_reg(int reg, int size, uae_u16 v)
{
	if (!memwatch_access_validator)
		return;
	reg &= 0x1ff;
	if (size == 1) {
		if (reg == 2) // DMACONR low byte
			return;
		if (reg == 6) // VPOS
			return;
	}
	const struct customData *cd = &custd[reg >> 1];
	if (size == -2 && (reg & 1)) {
		write_log(_T("Unaligned word write to register %04x (%s) val %04x PC=%08x\n"), reg, cd->name, v, M68K_GETPC);
	} else if (size == -1) {
		write_log(_T("Byte write to register %04x (%s) val %02x PC=%08x\n"), reg, cd->name, v & 0xff, M68K_GETPC);
	} else if (size == 2 && (reg & 1)) {
		write_log(_T("Unaligned word read from register %04x (%s) PC=%08x\n"), reg, cd->name, M68K_GETPC);
	} else if (size == 1) {
		write_log(_T("Byte read from register %04x (%s) PC=%08x\n"), reg, cd->name, M68K_GETPC);
	}
}

static void is_valid_dma(int reg, int ptrreg, uaecptr addr)
{
	if (!memwatch_access_validator)
		return;
	if (reg == 0x1fe) // refresh
		return;
	if (currprefs.z3chipmem_size) {
		if (addr >= currprefs.z3chipmem_start && addr < currprefs.z3chipmem_start + currprefs.z3chipmem_size)
			return;
	}
	if (!(addr & ~(currprefs.chipmem_size - 1)))
		return;
	const struct customData *cdreg = &custd[reg >> 1];
	const struct customData *cdptr = &custd[ptrreg >> 1];
	write_log(_T("DMA DAT %04x (%s), PT %04x (%s) accessed invalid memory %08x. Init: %08x, PC/COP=%08x\n"),
		reg, cdreg->name, ptrreg, cdptr->name, addr,
		(custom_storage[ptrreg >> 1].value << 16) | (custom_storage[(ptrreg >> 1) + 1].value),
		custom_storage[ptrreg >> 1].pc);
}

static void mungwall_memwatch(uaecptr addr, int rwi, int size, uae_u32 valp)
{
	struct mungwall_data *mwd = mungwall[addr >> 16];
	if (!mwd)
		return;
	for (int i = 0; i < mwd->slots; i++) {
		if (!mwd->end[i])
			continue;
		if (addr + size > mwd->start[i] && addr < mwd->end[i]) {

		}
	}
}

static void memwatch_hit_msg(int mw)
{
	console_out_f(_T("Memwatch %d: break at %08X.%c %c%c%c %08X PC=%08X "), mw, mwhit.addr,
		mwhit.size == 1 ? 'B' : (mwhit.size == 2 ? 'W' : 'L'),
		(mwhit.rwi & 1) ? 'R' : ' ', (mwhit.rwi & 2) ? 'W' : ' ', (mwhit.rwi & 4) ? 'I' : ' ',
		mwhit.val, mwhit.pc);
	for (int i = 0; memwatch_access_masks[i].mask; i++) {
		if (mwhit.access_mask == memwatch_access_masks[i].mask)
			console_out_f(_T("%s (%03x)\n"), memwatch_access_masks[i].name, mwhit.reg);
	}
	if (mwhit.access_mask & (MW_MASK_BLITTER_A | MW_MASK_BLITTER_B | MW_MASK_BLITTER_C | MW_MASK_BLITTER_D_N | MW_MASK_BLITTER_D_L | MW_MASK_BLITTER_D_F)) {
		blitter_debugdump();
	}
}

static int memwatch_func (uaecptr addr, int rwi, int size, uae_u32 *valp, uae_u32 accessmask, uae_u32 reg)
{
	uae_u32 val = *valp;

	if (debugging > 0)
		return 1;

	if (mungwall)
		mungwall_memwatch(addr, rwi, size, val);

	if (illgdebug)
		illg_debug_do (addr, rwi, size, val);

	if (heatmap)
		memwatch_heatmap (addr, rwi, size, accessmask);

	addr = munge24 (addr);

	if (smc_table && (rwi >= 2))
		smc_detector (addr, rwi, size, valp);

	for (int i = mwnodes_start; i <= mwnodes_end; i++) {
		struct memwatch_node *m = &mwnodes[i];
		uaecptr addr2 = m->addr;
		uaecptr addr3 = addr2 + m->size;
		int rwi2 = m->rwi;
		uae_u32 oldval = 0;
		int isoldval = 0;
		int brk = 0;

		if (m->size == 0)
			continue;
		if (!(rwi & rwi2))
			continue;
		if (!(m->access_mask & accessmask))
			continue;

		if (addr >= addr2 && addr < addr3)
			brk = 1;
		if (!brk && size == 2 && (addr + 1 >= addr2 && addr + 1 < addr3))
			brk = 1;
		if (!brk && size == 4 && ((addr + 2 >= addr2 && addr + 2 < addr3) || (addr + 3 >= addr2 && addr + 3 < addr3)))
			brk = 1;

		if (!brk)
			continue;
		if (mem_banks[addr >> 16]->check (addr, size)) {
			uae_u8 *p = mem_banks[addr >> 16]->xlateaddr (addr);
			if (size == 1)
				oldval = p[0];
			else if (size == 2)
				oldval = (p[0] << 8) | p[1];
			else
				oldval = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | (p[3] << 0);
			isoldval = 1;
		}

		if (m->pc != 0xffffffff) {
			if (m->pc != regs.instruction_pc)
				continue;
		}

		if (!m->frozen && m->val_enabled) {
			int trigger = 0;
			uae_u32 mask = m->size == 4 ? 0xffffffff : (1 << (m->size * 8)) - 1;
			uae_u32 mval = m->val;
			int scnt = size;
			for (;;) {
				if (((mval & mask) & m->val_mask) == ((val & mask) & m->val_mask))
					trigger = 1;
				if (mask & 0x80000000)
					break;
				if (m->size == 1) {
					mask <<= 8;
					mval <<= 8;
					scnt--;
				} else if (m->size == 2) {
					mask <<= 16;
					scnt -= 2;
					mval <<= 16;
				} else {
					scnt -= 4;
				}
				if (scnt <= 0)
					break;
			}
			if (!trigger)
				continue;
		}

		if (m->mustchange && rwi == 2 && isoldval) {
			if (oldval == *valp)
				continue;
		}

		if (m->modval_written) {
			if (!rwi) {
				brk = 0;
			} else if (m->modval_written == 1) {
				m->modval_written = 2;
				m->modval = val;
				brk = 0;
			} else if (m->modval == val) {
				brk = 0;
			}
		}
		if (m->frozen) {
			if (m->val_enabled) {
				int shift = (addr + size - 1) - (m->addr + m->val_size - 1);
				uae_u32 sval;
				uae_u32 mask;

				if (m->val_size == 4)
					mask = 0xffffffff;
				else if (m->val_size == 2)
					mask = 0x0000ffff;
				else
					mask = 0x000000ff;

				sval = m->val;
				if (shift < 0) {
					shift = -8 * shift;
					sval >>= shift;
					mask >>= shift;
				} else {
					shift = 8 * shift;
					sval <<= shift;
					mask <<= shift;
				}
				*valp = (sval & mask) | ((*valp) & ~mask);
				//write_log (_T("%08x %08x %08x %08x %d\n"), addr, m->addr, *valp, mask, shift);
				return 1;
			}
			return 0;
		}
		//	if (!notinrom ())
		//	    return 1;
		mwhit.addr = addr;
		mwhit.rwi = rwi;
		mwhit.size = size;
		mwhit.val = 0;
		mwhit.access_mask = accessmask;
		mwhit.reg = reg;
		if (mwhit.rwi & 2)
			mwhit.val = val;
		memwatch_triggered = i + 1;
		if (m->reportonly) {
			memwatch_hit_msg(memwatch_triggered - 1);
		}
		if (!m->nobreak && !m->reportonly) {
			debugging = 1;
			debug_pc = M68K_GETPC;
			debug_cycles();
			set_special(SPCFLAG_BRK);
		}
		return 1;
	}
	return 1;
}

static int mmu_hit (uaecptr addr, int size, int rwi, uae_u32 *v);

static uae_u32 REGPARAM2 mmu_lget (uaecptr addr)
{
	int off = debug_mem_off (&addr);
	uae_u32 v = 0;
	if (!mmu_hit (addr, 4, 0, &v))
		v = debug_mem_banks[off]->lget (addr);
	return v;
}
static uae_u32 REGPARAM2 mmu_wget (uaecptr addr)
{
	int off = debug_mem_off (&addr);
	uae_u32 v = 0;
	if (!mmu_hit (addr, 2, 0, &v))
		v = debug_mem_banks[off]->wget (addr);
	return v;
}
static uae_u32 REGPARAM2 mmu_bget (uaecptr addr)
{
	int off = debug_mem_off (&addr);
	uae_u32 v = 0;
	if (!mmu_hit(addr, 1, 0, &v))
		v = debug_mem_banks[off]->bget (addr);
	return v;
}
static void REGPARAM2 mmu_lput (uaecptr addr, uae_u32 v)
{
	int off = debug_mem_off (&addr);
	if (!mmu_hit (addr, 4, 1, &v))
		debug_mem_banks[off]->lput (addr, v);
}
static void REGPARAM2 mmu_wput (uaecptr addr, uae_u32 v)
{
	int off = debug_mem_off (&addr);
	if (!mmu_hit (addr, 2, 1, &v))
		debug_mem_banks[off]->wput (addr, v);
}
static void REGPARAM2 mmu_bput (uaecptr addr, uae_u32 v)
{
	int off = debug_mem_off (&addr);
	if (!mmu_hit (addr, 1, 1, &v))
		debug_mem_banks[off]->bput (addr, v);
}
static uae_u32 REGPARAM2 mmu_lgeti (uaecptr addr)
{
	int off = debug_mem_off (&addr);
	uae_u32 v = 0;
	if (!mmu_hit (addr, 4, 4, &v))
		v = debug_mem_banks[off]->lgeti (addr);
	return v;
}
static uae_u32 REGPARAM2 mmu_wgeti (uaecptr addr)
{
	int off = debug_mem_off (&addr);
	uae_u32 v = 0;
	if (!mmu_hit (addr, 2, 4, &v))
		v = debug_mem_banks[off]->wgeti (addr);
	return v;
}

static uae_u32 REGPARAM2 debug_lget(uaecptr addr)
{
	uae_u32 off = debug_mem_off(&addr);
	uae_u32 v;
	v = debug_mem_banks[off]->lget(addr);
	memwatch_func(addr, 1, 4, &v, MW_MASK_CPU_D_R, 0);
	return v;
}
static uae_u32 REGPARAM2 debug_wget (uaecptr addr)
{
	int off = debug_mem_off (&addr);
	uae_u32 v;
	v = debug_mem_banks[off]->wget (addr);
	memwatch_func (addr, 1, 2, &v, MW_MASK_CPU_D_R, 0);
	return v;
}
static uae_u32 REGPARAM2 debug_bget (uaecptr addr)
{
	int off = debug_mem_off (&addr);
	uae_u32 v;
	v = debug_mem_banks[off]->bget (addr);
	memwatch_func (addr, 1, 1, &v, MW_MASK_CPU_D_R, 0);
	return v;
}
static uae_u32 REGPARAM2 debug_lgeti (uaecptr addr)
{
	int off = debug_mem_off (&addr);
	uae_u32 v;
	v = debug_mem_banks[off]->lgeti (addr);
	memwatch_func (addr, 4, 4, &v, MW_MASK_CPU_I, 0);
	return v;
}
static uae_u32 REGPARAM2 debug_wgeti (uaecptr addr)
{
	int off = debug_mem_off (&addr);
	uae_u32 v;
	v = debug_mem_banks[off]->wgeti (addr);
	memwatch_func (addr, 4, 2, &v, MW_MASK_CPU_I, 0);
	return v;
}
static void REGPARAM2 debug_lput (uaecptr addr, uae_u32 v)
{
	int off = debug_mem_off (&addr);
	if (memwatch_func (addr, 2, 4, &v, MW_MASK_CPU_D_W, 0))
		debug_mem_banks[off]->lput (addr, v);
}
static void REGPARAM2 debug_wput (uaecptr addr, uae_u32 v)
{
	int off = debug_mem_off (&addr);
	if (memwatch_func (addr, 2, 2, &v, MW_MASK_CPU_D_W, 0))
		debug_mem_banks[off]->wput (addr, v);
}
static void REGPARAM2 debug_bput (uaecptr addr, uae_u32 v)
{
	int off = debug_mem_off (&addr);
	if (memwatch_func (addr, 2, 1, &v, MW_MASK_CPU_D_W, 0))
		debug_mem_banks[off]->bput (addr, v);
}
static int REGPARAM2 debug_check (uaecptr addr, uae_u32 size)
{
	return debug_mem_banks[munge24 (addr) >> 16]->check (addr, size);
}
static uae_u8 *REGPARAM2 debug_xlate (uaecptr addr)
{
	return debug_mem_banks[munge24 (addr) >> 16]->xlateaddr (addr);
}

uae_u16 debug_wputpeekdma_chipset (uaecptr addr, uae_u32 v, uae_u32 mask, int reg)
{
	if (!memwatch_enabled)
		return v;
	addr &= 0x1fe;
	addr += 0xdff000;
	memwatch_func (addr, 2, 2, &v, mask, reg);
	return v;
}
uae_u16 debug_wputpeekdma_chipram (uaecptr addr, uae_u32 v, uae_u32 mask, int reg, int ptrreg)
{
	if (!memwatch_enabled)
		return v;
	is_valid_dma(reg, ptrreg, addr);
	if (debug_mem_banks[addr >> 16] == NULL)
		return v;
	if (!currprefs.z3chipmem_size)
		addr &= chipmem_bank.mask;
	memwatch_func (addr & chipmem_bank.mask, 2, 2, &v, mask, reg);
	return v;
}
uae_u16 debug_wgetpeekdma_chipram (uaecptr addr, uae_u32 v, uae_u32 mask, int reg, int ptrreg)
{
	uae_u32 vv = v;
	if (!memwatch_enabled)
		return v;
	is_valid_dma(reg, ptrreg, addr);
	if (debug_mem_banks[addr >> 16] == NULL)
		return v;
	if (!currprefs.z3chipmem_size)
		addr &= chipmem_bank.mask;
	memwatch_func (addr, 1, 2, &vv, mask, reg);
	return vv;
}

static void debug_putlpeek (uaecptr addr, uae_u32 v)
{
	if (!memwatch_enabled)
		return;
	memwatch_func (addr, 2, 4, &v, MW_MASK_CPU_D_W, 0);
}
void debug_wputpeek (uaecptr addr, uae_u32 v)
{
	if (!memwatch_enabled)
		return;
	memwatch_func (addr, 2, 2, &v, MW_MASK_CPU_D_W, 0);
}
void debug_bputpeek (uaecptr addr, uae_u32 v)
{
	if (!memwatch_enabled)
		return;
	memwatch_func (addr, 2, 1, &v, MW_MASK_CPU_D_W, 0);
}
void debug_bgetpeek (uaecptr addr, uae_u32 v)
{
	uae_u32 vv = v;
	if (!memwatch_enabled)
		return;
	memwatch_func (addr, 1, 1, &vv, MW_MASK_CPU_D_R, 0);
}
void debug_wgetpeek (uaecptr addr, uae_u32 v)
{
	uae_u32 vv = v;
	if (!memwatch_enabled)
		return;
	memwatch_func (addr, 1, 2, &vv, MW_MASK_CPU_D_R, 0);
}
void debug_lgetpeek (uaecptr addr, uae_u32 v)
{
	uae_u32 vv = v;
	if (!memwatch_enabled)
		return;
	memwatch_func (addr, 1, 4, &vv, MW_MASK_CPU_D_R, 0);
}

struct membank_store
{
	addrbank *addr;
	addrbank newbank;
	int banknr;
};

static struct membank_store *membank_stores;
static int membank_total;
#define MEMWATCH_STORE_SLOTS 32

static void memwatch_reset (void)
{
	for (int i = 0; i < membank_total; i++) {
		addrbank *ab = debug_mem_banks[i];
		if (!ab)
			continue;
		map_banks_quick (ab, i, 1, 1);
	}
	for (int i = 0; membank_stores[i].addr; i++) {
		struct membank_store *ms = &membank_stores[i];
		/* name was allocated in memwatch_remap */
		xfree ((char*)ms->newbank.name);
		memset (ms, 0, sizeof (struct membank_store));
		ms->addr = NULL;
	}
	memset (debug_mem_banks, 0, membank_total * sizeof (addrbank*));
}

static void memwatch_remap (uaecptr addr)
{
	int mode = 0;
	int i;
	int banknr;
	struct membank_store *ms;
	addrbank *bank;
	addrbank *newbank = NULL;

	addr &= ~65535;
	banknr = addr >> 16;
	if (debug_mem_banks[banknr])
		return;
	bank = mem_banks[banknr];
	for (i = 0 ; i < MEMWATCH_STORE_SLOTS; i++) {
		ms = &membank_stores[i];
		if (ms->addr == NULL)
			break;
		if (ms->addr == bank) {
			newbank = &ms->newbank;
			break;
		}
	}
	if (i >= MEMWATCH_STORE_SLOTS)
		return;
	if (!newbank) {
		TCHAR tmp[200];
		_stprintf (tmp, _T("%s [D]"), bank->name);
		ms->addr = bank;
		ms->banknr = banknr;
		newbank = &ms->newbank;
		memcpy (newbank, bank, sizeof(addrbank));
		newbank->bget = mode ? mmu_bget : debug_bget;
		newbank->wget = mode ? mmu_wget : debug_wget;
		newbank->lget = mode ? mmu_lget : debug_lget;
		newbank->bput = mode ? mmu_bput : debug_bput;
		newbank->wput = mode ? mmu_wput : debug_wput;
		newbank->lput = mode ? mmu_lput : debug_lput;
		newbank->check = debug_check;
		newbank->xlateaddr = debug_xlate;
		newbank->wgeti = mode ? mmu_wgeti : debug_wgeti;
		newbank->lgeti = mode ? mmu_lgeti : debug_lgeti;
		/* name will be freed by memwatch_reset */
		newbank->name = my_strdup (tmp);
		if (!newbank->mask)
			newbank->mask = -1;
	}
	debug_mem_banks[banknr] = bank;
	map_banks_quick (newbank, banknr, 1, 1);
	// map aliases
	for (i = 0; i < membank_total; i++) {
		uaecptr addr2 = i << 16;
		addrbank *ab = &get_mem_bank(addr2);
		if (ab != ms->addr)
			continue;
		if ((addr2 & ab->mask) == (addr & bank->mask)) {
			debug_mem_banks[i] = ms->addr;
			map_banks_quick (newbank, i, 1, 1);
		}
	}
}

static void memwatch_setup (void)
{
	memwatch_reset ();
	mwnodes_start = MEMWATCH_TOTAL - 1;
	mwnodes_end = 0;
	for (int i = 0; i < MEMWATCH_TOTAL; i++) {
		struct memwatch_node *m = &mwnodes[i];
		uae_u32 size = 0;
		if (!m->size)
			continue;
		if (mwnodes_start > i)
			mwnodes_start = i;
		if (mwnodes_end < i)
			mwnodes_end = i;
		while (size < m->size) {
			memwatch_remap (m->addr + size);
			size += 65536;
		}
	}
}

static int deinitialize_memwatch (void)
{
	int oldmode;

	if (!memwatch_enabled && !mmu_enabled)
		return -1;
	memwatch_reset ();
	oldmode = mmu_enabled ? 1 : 0;
	xfree (debug_mem_banks);
	debug_mem_banks = NULL;
	xfree (debug_mem_area);
	debug_mem_area = NULL;
	xfree (membank_stores);
	membank_stores = NULL;
	memwatch_enabled = 0;
	mmu_enabled = 0;
	xfree (illgdebug);
	illgdebug = 0;
	return oldmode;
}

static void initialize_memwatch (int mode)
{
	membank_total = currprefs.address_space_24 ? 256 : 65536;
	deinitialize_memwatch ();
	debug_mem_banks = xcalloc (addrbank*, membank_total);
	debug_mem_area = xcalloc (addrbank, membank_total);
	membank_stores = xcalloc (struct membank_store, MEMWATCH_STORE_SLOTS);
	for (int i = 0; i < MEMWATCH_TOTAL; i++) {
		struct memwatch_node *m = &mwnodes[i];
		m->pc = 0xffffffff;
	}
#if 0
	int i, j, as;
	addrbank *a1, *a2, *oa;
	oa = NULL;
	for (i = 0; i < as; i++) {
		a1 = debug_mem_banks[i] = debug_mem_area + i;
		a2 = mem_banks[i];
		if (a2 != oa) {
			for (j = 0; membank_stores[j].addr; j++) {
				if (membank_stores[j].addr == a2)
					break;
			}
			if (membank_stores[j].addr == NULL) {
				membank_stores[j].addr = a2;
				memcpy (&membank_stores[j].store, a2, sizeof (addrbank));
			}
		}
		memcpy (a1, a2, sizeof (addrbank));
	}
	for (i = 0; i < as; i++) {
		a2 = mem_banks[i];
		a2->bget = mode ? mmu_bget : debug_bget;
		a2->wget = mode ? mmu_wget : debug_wget;
		a2->lget = mode ? mmu_lget : debug_lget;
		a2->bput = mode ? mmu_bput : debug_bput;
		a2->wput = mode ? mmu_wput : debug_wput;
		a2->lput = mode ? mmu_lput : debug_lput;
		a2->check = debug_check;
		a2->xlateaddr = debug_xlate;
		a2->wgeti = mode ? mmu_wgeti : debug_wgeti;
		a2->lgeti = mode ? mmu_lgeti : debug_lgeti;
	}
#endif
	if (mode)
		mmu_enabled = 1;
	else
		memwatch_enabled = 1;
}

int debug_bankchange (int mode)
{
	if (mode == -1) {
		int v = deinitialize_memwatch ();
		if (v < 0)
			return -2;
		return v;
	}
	if (mode >= 0) {
		initialize_memwatch (mode);
		memwatch_setup ();
	}
	return -1;
}

addrbank *get_mem_bank_real(uaecptr addr)
{
	addrbank *ab = &get_mem_bank(addr);
	if (!memwatch_enabled)
		return ab;
	addrbank *ab2 = debug_mem_banks[addr >> 16];
	if (ab2)
		return ab2;
	return ab;
}

static const TCHAR *getsizechar (int size)
{
	if (size == 4)
		return _T(".l");
	if (size == 3)
		return _T(".3");
	if (size == 2)
		return _T(".w");
	if (size == 1)
		return _T(".b");
	return _T("");
}

void memwatch_dump2 (TCHAR *buf, int bufsize, int num)
{
	int i;
	struct memwatch_node *mwn;

	if (buf)
		memset (buf, 0, bufsize * sizeof (TCHAR));
	for (i = 0; i < MEMWATCH_TOTAL; i++) {
		if ((num >= 0 && num == i) || (num < 0)) {
			uae_u32 usedmask = 0;
			mwn = &mwnodes[i];
			if (mwn->size == 0)
				continue;
			buf = buf_out (buf, &bufsize, _T("%2d: %08X - %08X (%d) %c%c%c"),
				i, mwn->addr, mwn->addr + (mwn->size - 1), mwn->size,
				(mwn->rwi & 1) ? 'R' : ' ', (mwn->rwi & 2) ? 'W' : ' ', (mwn->rwi & 4) ? 'I' : ' ');
			if (mwn->frozen)
				buf = buf_out (buf, &bufsize, _T(" F"));
			if (mwn->val_enabled)
				buf = buf_out (buf, &bufsize, _T(" =%X%s"), mwn->val, getsizechar (mwn->val_size));
			if (mwn->modval_written)
				buf = buf_out (buf, &bufsize, _T(" =M"));
			if (mwn->mustchange)
				buf = buf_out(buf, &bufsize, _T(" C"));
			if (mwn->pc != 0xffffffff)
				buf = buf_out(buf, &bufsize, _T(" PC=%08x"), mwn->pc);
			if (mwn->reportonly)
				buf = buf_out(buf, &bufsize, _T(" L"));
			if (mwn->nobreak)
				buf = buf_out(buf, &bufsize, _T(" N"));
			for (int j = 0; memwatch_access_masks[j].mask; j++) {
				uae_u32 mask = memwatch_access_masks[j].mask;
				if ((mwn->access_mask & mask) == mask && (usedmask & mask) == 0) {
					buf = buf_out(buf, &bufsize, _T(" "));
					buf = buf_out(buf, &bufsize, memwatch_access_masks[j].name);
					usedmask |= mask;
				}
			}
			buf = buf_out (buf, &bufsize, _T("\n"));
		}
	}
}

static void memwatch_dump (int num)
{
	TCHAR *buf;
	int multiplier = num < 0 ? MEMWATCH_TOTAL : 1;

	buf = xmalloc (TCHAR, 50 * multiplier);
	if (!buf)
		return;
	memwatch_dump2 (buf, 50 * multiplier, num);
	f_out (stdout, _T("%s"), buf);
	xfree (buf);
}

static void memwatch (TCHAR **c)
{
	int num;
	struct memwatch_node *mwn;
	TCHAR nc, *cp;

	if (!memwatch_enabled) {
		initialize_memwatch (0);
		console_out (_T("Memwatch breakpoints enabled\n"));
		memwatch_access_validator = 0;
	}

	cp = *c;
	ignore_ws (c);
	if (!more_params (c)) {
		memwatch_dump (-1);
		return;
	}
	nc = next_char (c);
	if (nc == '-') {
		deinitialize_memwatch ();
		console_out (_T("Memwatch breakpoints disabled\n"));
		return;
	}
	if (nc == 'l') {
		memwatch_access_validator = !memwatch_access_validator;
		console_out_f(_T("Memwatch DMA validator %s\n"), memwatch_access_validator ? _T("enabled") : _T("disabled"));
		return;
	}

	if (nc == 'd') {
		if (illgdebug) {
			ignore_ws (c);
			if (more_params (c)) {
				uae_u32 addr = readhex (c);
				uae_u32 len = 1;
				if (more_params (c))
					len = readhex (c);
				console_out_f (_T("Cleared logging addresses %08X - %08X\n"), addr, addr + len);
				while (len > 0) {
					addr &= 0xffffff;
					illgdebug[addr] = 7;
					addr++;
					len--;
				}
			} else {
				illg_free();
				console_out (_T("Illegal memory access logging disabled\n"));
			}
		} else {
			illg_init ();
			ignore_ws (c);
			illgdebug_break = 0;
			if (more_params (c))
				illgdebug_break = 1;
			console_out_f (_T("Illegal memory access logging enabled. Break=%d\n"), illgdebug_break);
		}
		return;
	}
	*c = cp;
	num = readint (c);
	if (num < 0 || num >= MEMWATCH_TOTAL)
		return;
	mwn = &mwnodes[num];
	mwn->size = 0;
	ignore_ws (c);
	if (!more_params (c)) {
		console_out_f (_T("Memwatch %d removed\n"), num);
		memwatch_setup ();
		return;
	}
	mwn->addr = readhex (c);
	mwn->size = 1;
	mwn->rwi = 7;
	mwn->val_enabled = 0;
	mwn->val_mask = 0xffffffff;
	mwn->val = 0;
	mwn->access_mask = 0;
	mwn->reg = 0xffffffff;
	mwn->frozen = 0;
	mwn->modval_written = 0;
	ignore_ws (c);
	if (more_params (c)) {
		mwn->size = readhex (c);
		ignore_ws (c);
		if (more_params (c)) {
			TCHAR *cs = *c;
			while (*cs) {
				for (int i = 0; memwatch_access_masks[i].mask; i++) {
					const TCHAR *n = memwatch_access_masks[i].name;
					int len = _tcslen(n);
					if (!_tcsnicmp(cs, n, len)) {
						if (cs[len] == 0 || cs[len] == 10 || cs[len] == 13) {
							mwn->access_mask |= memwatch_access_masks[i].mask;
							while (len > 0) {
								len--;
								cs[len] = ' ';
							}
						}
					}
				}
				cs++;
			}
			ignore_ws (c);
			if (more_params(c)) {
				for (;;) {
					TCHAR ncc = _totupper(peek_next_char(c));
					TCHAR nc = _totupper (next_char (c));
					if (mwn->rwi == 7)
						mwn->rwi = 0;
					if (nc == 'F')
						mwn->frozen = 1;
					if (nc == 'W')
						mwn->rwi |= 2;
					if (nc == 'I')
						mwn->rwi |= 4;
					if (nc == 'R')
						mwn->rwi |= 1;
					if (ncc == ' ')
						break;
					if (nc == 'P' && ncc == 'C') {
						next_char(c);
						mwn->pc = readhex(c, NULL);
					}
					if (ncc == 'L')
						mwn->reportonly = true;
					if (ncc == 'N')
						mwn->nobreak = true;
					if (!more_params(c))
						break;
				}
				ignore_ws (c);
			}
			if (more_params (c)) {
				if (_totupper (**c) == 'M') {
					mwn->modval_written = 1;
				} else if (_totupper (**c) == 'C') {
					mwn->mustchange = 1;
				} else {
					mwn->val = readhex (c, &mwn->val_size);
					mwn->val_enabled = 1;
				}
			}
		}
	}
	if (!mwn->access_mask)
		mwn->access_mask = MW_MASK_CPU_D_R | MW_MASK_CPU_D_W | MW_MASK_CPU_I;
	if (mwn->frozen && mwn->rwi == 0)
		mwn->rwi = 3;
	memwatch_setup ();
	memwatch_dump (num);
}

static void copymem(TCHAR **c)
{
	uae_u32 addr = 0, eaddr = 0, dst = 0;

	ignore_ws(c);
	if (!more_params (c))
		return;
	addr = readhex (c);
	ignore_ws (c);
	if (!more_params (c))
		return;
	eaddr = readhex (c);
	ignore_ws (c);
	if (!more_params (c))
		return;
	dst = readhex (c);

	if (addr >= eaddr)
		return;
	uae_u32 addrb = addr;
	uae_u32 dstb = dst;
	uae_u32 len = eaddr - addr;
	if (dst <= addr) {
		while (addr < eaddr) {
			put_byte(dst, get_byte(addr));
			addr++;
			dst++;
		}
	} else {
		dst += eaddr - addr;
		while (addr < eaddr) {
			dst--;
			eaddr--;
			put_byte(dst, get_byte(eaddr));
		}
	}
	console_out_f(_T("Copied from %08x - %08x to %08x - %08x\n"), addrb, addrb + len - 1, dstb, dstb + len - 1);
}

static void writeintomem (TCHAR **c)
{
	uae_u32 addr = 0;
	uae_u32 eaddr = 0xffffffff;
	uae_u32 val = 0;
	TCHAR cc;
	int len = 1;
	bool fillmode = false;

	if (**c == 'f') {
		fillmode = true;
		(*c)++;
	} else if (**c == 'c') {
		(*c)++;
		copymem(c);
		return;
	}

	ignore_ws(c);
	addr = readhex (c);
	ignore_ws (c);

	if (fillmode) {
		if (!more_params (c))
			return;
		eaddr = readhex(c);
		ignore_ws (c);
	}

	if (!more_params (c))
		return;
	TCHAR *cb = *c;
	cc = peekchar (c);
	uae_u32 addrc = addr;
	for(;;) {
		uae_u32 addrb = addr;
		*c = cb;
		if (cc == '\'' || cc == '\"') {
			next_char (c);
			while (more_params (c)) {
				TCHAR str[2];
				char *astr;
				cc = next_char (c);
				if (cc == '\'' || cc == '\"')
					break;
				str[0] = cc;
				str[1] = 0;
				astr = ua (str);
				put_byte (addr, astr[0]);
				xfree (astr);
				addr++;
				if (addr >= eaddr)
					break;
			}
		} else {
			for (;;) {
				ignore_ws (c);
				if (!more_params (c))
					break;
				val = readhex (c, &len);

				if (len == 4) {
					put_long (addr, val);
					cc = 'L';
				} else if (len == 2) {
					put_word (addr, val);
					cc = 'W';
				} else if (len == 1) {
					put_byte (addr, val);
					cc = 'B';
				} else {
					break;
				}
				if (!fillmode)
					console_out_f (_T("Wrote %X (%u) at %08X.%c\n"), val, val, addr, cc);
				addr += len;
				if (addr >= eaddr)
					break;
			}
		}
		if (eaddr == 0xffffffff || addr <= addrb || addr >= eaddr)
			break;
	}
	if (eaddr != 0xffffffff)
		console_out_f(_T("Wrote data to %08x - %08x\n"), addrc, addr);
}

static uae_u8 *dump_xlate (uae_u32 addr)
{
	if (!mem_banks[addr >> 16]->check (addr, 1))
		return NULL;
	return mem_banks[addr >> 16]->xlateaddr (addr);
}

#if 0
#define UAE_MEMORY_REGIONS_MAX 64
#define UAE_MEMORY_REGION_NAME_LENGTH 64

#define UAE_MEMORY_REGION_RAM (1 << 0)
#define UAE_MEMORY_REGION_ALIAS (1 << 1)
#define UAE_MEMORY_REGION_MIRROR (1 << 2)

typedef struct UaeMemoryRegion {
	uaecptr start;
	int size;
	TCHAR name[UAE_MEMORY_REGION_NAME_LENGTH];
	TCHAR rom_name[UAE_MEMORY_REGION_NAME_LENGTH];
	uaecptr alias;
	int flags;
} UaeMemoryRegion;

typedef struct UaeMemoryMap {
	UaeMemoryRegion regions[UAE_MEMORY_REGIONS_MAX];
	int num_regions;
} UaeMemoryMap;
#endif

static const TCHAR *bankmodes[] = { _T("F32"), _T("C16"), _T("C32"), _T("CIA"), _T("F16"), _T("F16X") };

static void memory_map_dump_3(UaeMemoryMap *map, int log)
{
	bool imold;
	int i, j, max;
	addrbank *a1 = mem_banks[0];
	TCHAR txt[256];

	map->num_regions = 0;
	imold = currprefs.illegal_mem;
	currprefs.illegal_mem = false;
	max = currprefs.address_space_24 ? 256 : 65536;
	j = 0;
	for (i = 0; i < max + 1; i++) {
		addrbank *a2 = NULL;
		if (i < max)
			a2 = mem_banks[i];
		if (a1 != a2) {
			int k, mirrored, mirrored2, size, size_out;
			TCHAR size_ext;
			uae_u8 *caddr;
			TCHAR tmp[MAX_DPATH];
			const TCHAR *name = a1->name;
			struct addrbank_sub *sb = a1->sub_banks;
			int bankoffset = 0;
			int region_size;

			k = j;
			caddr = dump_xlate (k << 16);
			mirrored = caddr ? 1 : 0;
			k++;
			while (k < i && caddr) {
				if (dump_xlate (k << 16) == caddr) {
					mirrored++;
				}
				k++;
			}
			mirrored2 = mirrored;
			if (mirrored2 == 0)
				mirrored2 = 1;

			while (bankoffset < 65536) {
				int bankoffset2 = bankoffset;
				if (sb) {
					uaecptr daddr;
					if (!sb->bank)
						break;
					daddr = (j << 16) | bankoffset;
					a1 = get_sub_bank(&daddr);
					name = a1->name;
					for (;;) {
						bankoffset2 += MEMORY_MIN_SUBBANK;
						if (bankoffset2 >= 65536)
							break;
						daddr = (j << 16) | bankoffset2;
						addrbank *dab = get_sub_bank(&daddr);
						if (dab != a1)
							break;
					}
					sb++;
					size = (bankoffset2 - bankoffset) / 1024;
					region_size = size * 1024;
				} else {
					size = (i - j) << (16 - 10);
					region_size = ((i - j) << 16) / mirrored2;
				}

				if (name == NULL)
					name = _T("<none>");

				size_out = size;
				size_ext = 'K';
				if (j >= 256 && (size_out / mirrored2 >= 1024) && !((size_out / mirrored2) & 1023)) {
					size_out /= 1024;
					size_ext = 'M';
				}
				_stprintf (txt, _T("%08X %7d%c/%d = %7d%c %s%s %s %s"), (j << 16) | bankoffset, size_out, size_ext,
					mirrored, mirrored ? size_out / mirrored : size_out, size_ext,
					(a1->flags & ABFLAG_CACHE_ENABLE_INS) ? _T("I") : _T("-"),
					(a1->flags & ABFLAG_CACHE_ENABLE_DATA) ? _T("D") : _T("-"),
					bankmodes[ce_banktype[j]],
					name);
				tmp[0] = 0;
				if ((a1->flags & ABFLAG_ROM) && mirrored) {
					TCHAR *p = txt + _tcslen (txt);
					uae_u32 crc = 0xffffffff;
					if (a1->check(((j << 16) | bankoffset), (size * 1024) / mirrored))
						crc = get_crc32 (a1->xlateaddr((j << 16) | bankoffset), (size * 1024) / mirrored);
					struct romdata *rd = getromdatabycrc (crc);
					_stprintf (p, _T(" (%08X)"), crc);
					if (rd) {
						tmp[0] = '=';
						getromname (rd, tmp + 1);
						_tcscat (tmp, _T("\n"));
					}
				}

				if (a1 != &dummy_bank) {
					for (int m = 0; m < mirrored2; m++) {
						if (map->num_regions >= UAE_MEMORY_REGIONS_MAX)
							break;
						UaeMemoryRegion *r = &map->regions[map->num_regions];
						r->start = (j << 16) + bankoffset + region_size * m;
						r->size = region_size;
						r->flags = 0;
						r->memory = NULL;
						if (!(a1->flags & ABFLAG_PPCIOSPACE)) {
							r->memory = dump_xlate((j << 16) | bankoffset);
							if (r->memory)
								r->flags |= UAE_MEMORY_REGION_RAM;
						}
						/* just to make it easier to spot in debugger */
						r->alias = 0xffffffff;
						if (m >= 0) {
							r->alias = j << 16;
							r->flags |= UAE_MEMORY_REGION_ALIAS | UAE_MEMORY_REGION_MIRROR;
						}
						_stprintf(r->name, _T("%s"), name);
						_stprintf(r->rom_name, _T("%s"), tmp);
						map->num_regions += 1;
					}
				}
				_tcscat (txt, _T("\n"));
				if (log > 0)
					write_log (_T("%s"), txt);
				else if (log == 0)
					console_out (txt);
				if (tmp[0]) {
					if (log > 0)
						write_log (_T("%s"), tmp);
					else if (log == 0)
						console_out (tmp);
				}
				if (!sb)
					break;
				bankoffset = bankoffset2;
			}
			j = i;
			a1 = a2;
		}
	}
	pci_dump(log);
	currprefs.illegal_mem = imold;
}

void uae_memory_map(UaeMemoryMap *map)
{
	memory_map_dump_3(map, -1);
}

static void memory_map_dump_2 (int log)
{
	UaeMemoryMap map;
	memory_map_dump_3(&map, log);
#if 0
	for (int i = 0; i < map.num_regions; i++) {
		TCHAR txt[256];
		UaeMemoryRegion *r = &map.regions[i];
		int size = r->size / 1024;
		TCHAR size_ext = 'K';
		int mirrored = 1;
		int size_out = 0;
		_stprintf (txt, _T("%08X %7u%c/%d = %7u%c %s\n"), r->start, size, size_ext,
			r->flags & UAE_MEMORY_REGION_RAM, size, size_ext, r->name);
		if (log)
			write_log (_T("%s"), txt);
		else
			console_out (txt);
		if (r->rom_name[0]) {
			if (log)
				write_log (_T("%s"), r->rom_name);
			else
				console_out (r->rom_name);
		}
	}
#endif
}

void memory_map_dump (void)
{
	memory_map_dump_2(1);
}

STATIC_INLINE uaecptr BPTR2APTR (uaecptr addr)
{
	return addr << 2;
}
static TCHAR *BSTR2CSTR (uae_u8 *bstr)
{
	TCHAR *s;
	char *cstr = xmalloc (char, bstr[0] + 1);
	if (cstr) {
		memcpy (cstr, bstr + 1, bstr[0]);
		cstr[bstr[0]] = 0;
	}
	s = au (cstr);
	xfree (cstr);
	return s;
}

static void print_task_info (uaecptr node, bool nonactive)
{
	TCHAR *s;
	int process = get_byte_debug (node + 8) == 13 ? 1 : 0;

	console_out_f (_T("%08X: "), node);
	s = au ((char*)get_real_address_debug(get_long_debug (node + 10)));
	console_out_f (process ? _T("PROCESS '%s'\n") : _T("TASK    '%s'\n"), s);
	xfree (s);
	if (process) {
		uaecptr cli = BPTR2APTR (get_long_debug (node + 172));
		int tasknum = get_long_debug (node + 140);
		if (cli && tasknum) {
			uae_u8 *command_bstr = get_real_address_debug(BPTR2APTR (get_long_debug (cli + 16)));
			TCHAR *command = BSTR2CSTR (command_bstr);
			console_out_f (_T(" [%d, '%s']\n"), tasknum, command);
			xfree (command);
		} else {
			console_out (_T("\n"));
		}
	}
	if (nonactive) {
		uae_u32 sigwait = get_long_debug(node + 22);
		if (sigwait)
			console_out_f(_T("          Waiting signals: %08x\n"), sigwait);
		int offset = kickstart_version >= 37 ? 74 : 70;
		uae_u32 sp = get_long_debug(node + 54) + offset;
		uae_u32 pc = get_long_debug(sp);
		console_out_f(_T("          SP: %08x PC: %08x\n"), sp, pc);
	}
}

static void show_exec_tasks (void)
{
	uaecptr execbase = get_long_debug (4);
	uaecptr taskready = execbase + 406;
	uaecptr taskwait = execbase + 420;
	uaecptr node;
	console_out_f (_T("Execbase at 0x%08X\n"), execbase);
	console_out (_T("Current:\n"));
	node = get_long_debug (execbase + 276);
	print_task_info (node, false);
	console_out_f (_T("Ready:\n"));
	node = get_long_debug (taskready);
	while (node && get_long_debug(node)) {
		print_task_info (node, true);
		node = get_long_debug (node);
	}
	console_out (_T("Waiting:\n"));
	node = get_long_debug (taskwait);
	while (node && get_long_debug(node)) {
		print_task_info (node, true);
		node = get_long_debug (node);
	}
}

static uaecptr get_base (const uae_char *name, int offset)
{
	uaecptr v = get_long_debug (4);
	addrbank *b = &get_mem_bank(v);

	if (!b || !b->check (v, 400) || !(b->flags & ABFLAG_RAM))
		return 0;
	v += offset;
	while ((v = get_long_debug (v))) {
		uae_u32 v2;
		uae_u8 *p;
		b = &get_mem_bank (v);
		if (!b || !b->check (v, 32) || (!(b->flags & ABFLAG_RAM) && !(b->flags & ABFLAG_ROMIN)))
			goto fail;
		v2 = get_long_debug (v + 10); // name
		b = &get_mem_bank (v2);
		if (!b || !b->check (v2, 20))
			goto fail;
		if ((b->flags & ABFLAG_ROM) || (b->flags & ABFLAG_RAM) || (b->flags & ABFLAG_ROMIN)) {
			p = b->xlateaddr (v2);
			if (!memcmp (p, name, strlen (name) + 1))
				return v;
		}
	}
	return 0;
fail:
	return 0xffffffff;
}

static TCHAR *getfrombstr(uaecptr pp)
{
	uae_u8 len = get_byte(pp << 2);
	TCHAR *s = xcalloc (TCHAR, len + 1);
	char data[256];
	for (int i = 0; i < len; i++) {
		data[i] = get_byte((pp << 2) + 1 + i);
		data[i + 1] = 0;
	}
	return au_copy (s, len + 1, data);
}

// read one byte from expansion autoconfig ROM
static void copyromdata(uae_u8 bustype, uaecptr rom, int offset, uae_u8 *out, int size)
{
	switch (bustype & 0xc0)
	{
	case 0x00: // nibble
		while (size-- > 0) {
			*out++ = (get_byte_debug(rom + offset * 4 + 0) & 0xf0) | ((get_byte_debug(rom + offset * 4 + 2) & 0xf0) >> 4);
			offset++;
		}
		break;
	case 0x40: // byte
		while (size-- > 0) {
			*out++ = get_byte_debug(rom + offset * 2);
			offset++;
		}
		break;
	case 0x80: // word
	default:
		while (size-- > 0) {
			*out++ = get_byte_debug(rom + offset);
			offset++;
		}
		break;
	}
}

static void show_exec_lists (TCHAR *t)
{
	uaecptr execbase = get_long_debug (4);
	uaecptr list = 0, node;
	TCHAR c = t[0];

	if (c == 'o' || c == 'O') { // doslist
		uaecptr dosbase = get_base ("dos.library", 378);
		if (dosbase) {
			uaecptr rootnode = get_long_debug (dosbase + 34);
			uaecptr dosinfo = get_long_debug (rootnode + 24) << 2;
			console_out_f (_T("ROOTNODE: %08x DOSINFO: %08x\n"), rootnode, dosinfo);
			uaecptr doslist = get_long_debug (dosinfo + 4) << 2;
			while (doslist) {
				int type = get_long_debug (doslist + 4);
				uaecptr msgport = get_long_debug (doslist + 8);
				uaecptr lock = get_long_debug(doslist + 12);
				TCHAR *name = getfrombstr(get_long_debug(doslist + 40));
				console_out_f(_T("%08x: Type=%d Port=%08x Lock=%08x '%s'\n"), doslist, type, msgport, lock, name);
				if (type == 0) {
					uaecptr fssm = get_long_debug(doslist + 28) << 2;
					console_out_f (_T(" - H=%08x Stack=%5d Pri=%2d Start=%08x Seg=%08x GV=%08x\n"),
						get_long_debug (doslist + 16) << 2, get_long_debug (doslist + 20),
						get_long_debug (doslist + 24), fssm,
						get_long_debug (doslist + 32) << 2, get_long_debug (doslist + 36));
					if (fssm >= 0x100 && (fssm & 3) == 0) {
						TCHAR *unitname = getfrombstr(get_long_debug(fssm + 4));
						console_out_f (_T("   %s:%d %08x\n"), unitname, get_long_debug(fssm), get_long_debug(fssm + 8));
						uaecptr de = get_long_debug(fssm + 8) << 2;
						if (de) {
							console_out_f (_T("    TableSize       %u\n"), get_long_debug(de + 0));
							console_out_f (_T("    SizeBlock       %u\n"), get_long_debug(de + 4));
							console_out_f (_T("    SecOrg          %u\n"), get_long_debug(de + 8));
							console_out_f (_T("    Surfaces        %u\n"), get_long_debug(de + 12));
							console_out_f (_T("    SectorPerBlock  %u\n"), get_long_debug(de + 16));
							console_out_f (_T("    BlocksPerTrack  %u\n"), get_long_debug(de + 20));
							console_out_f (_T("    Reserved        %u\n"), get_long_debug(de + 24));
							console_out_f (_T("    PreAlloc        %u\n"), get_long_debug(de + 28));
							console_out_f (_T("    Interleave      %u\n"), get_long_debug(de + 32));
							console_out_f (_T("    LowCyl          %u\n"), get_long_debug(de + 36));
							console_out_f (_T("    HighCyl         %u (Total %u)\n"), get_long_debug(de + 40), get_long_debug(de + 40) - get_long_debug(de + 36) + 1);
							console_out_f (_T("    NumBuffers      %u\n"), get_long_debug(de + 44));
							console_out_f (_T("    BufMemType      0x%08x\n"), get_long_debug(de + 48));
							console_out_f (_T("    MaxTransfer     0x%08x\n"), get_long_debug(de + 52));
							console_out_f (_T("    Mask            0x%08x\n"), get_long_debug(de + 56));
							console_out_f (_T("    BootPri         %d\n"), get_long_debug(de + 60));
							console_out_f (_T("    DosType         0x%08x\n"), get_long_debug(de + 64));
						}
						xfree(unitname);
					}
				} else if (type == 2) {
					console_out_f(_T(" - VolumeDate=%08x %08x %08x LockList=%08x DiskType=%08x\n"),
						get_long_debug(doslist + 16), get_long_debug(doslist + 20), get_long_debug(doslist + 24),
						get_long_debug(doslist + 28),
						get_long_debug(doslist + 32));
				}
				xfree (name);
				doslist = get_long_debug (doslist) << 2;
			}
		} else {
			console_out_f (_T("can't find dos.library\n"));
		}
		return;
	} else if (c == 'i' || c == 'I') { // interrupts
		static const int it[] = {  1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0 };
		static const int it2[] = { 1, 1, 1, 2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 6, 6, 7 };
		list = execbase + 84;
		for (int i = 0; i < 16; i++) {
			console_out_f (_T("%2d %d: %08X\n"), i + 1, it2[i], list);
			if (it[i]) {
				console_out_f (_T("  [H] %08X\n"), get_long_debug (list));
				node = get_long_debug (list + 8);
				if (node) {
					uae_u8 *addr = get_real_address_debug(get_long_debug (node + 10));
					TCHAR *name = addr ? au ((char*)addr) : au("<null>");
					console_out_f (_T("      %08X (C=%08X D=%08X) '%s'\n"), node, get_long_debug (list + 4), get_long_debug (list), name);
					xfree (name);
				}
			} else {
				int cnt = 0;
				node = get_long_debug (list);
				node = get_long_debug (node);
				while (get_long_debug (node)) {
					uae_u8 *addr = get_real_address_debug(get_long_debug (node + 10));
					TCHAR *name = addr ? au ((char*)addr) : au("<null>");
					uae_s8 pri = get_byte_debug(node + 9);
					console_out_f (_T("  [S] %08x %+03d (C=%08x D=%08X) '%s'\n"), node, pri, get_long_debug (node + 18), get_long_debug (node + 14), name);
					if (i == 4 - 1 || i == 14 - 1) {
						if (!_tcsicmp (name, _T("cia-a")) || !_tcsicmp (name, _T("cia-b"))) {
							static const TCHAR *ciai[] = { _T("A"), _T("B"), _T("ALRM"), _T("SP"), _T("FLG") };
							uaecptr cia = node + 22;
							for (int j = 0; j < 5; j++) {
								uaecptr ciap = get_long_debug (cia);
								console_out_f (_T("        %5s: %08x"), ciai[j], ciap);
								if (ciap) {
									uae_u8 *addr2 = get_real_address_debug(get_long_debug (ciap + 10));
									TCHAR *name2 = addr ? au ((char*)addr2) : au("<null>");
									console_out_f (_T(" (C=%08x D=%08X) '%s'"), get_long_debug (ciap + 18), get_long_debug (ciap + 14), name2);
									xfree (name2);
								}
								console_out_f (_T("\n"));
								cia += 4;
							}
						}
					}
					xfree (name);
					node = get_long_debug (node);
					cnt++;
				}
				if (!cnt)
					console_out_f (_T("  [S] <none>\n"));
			}
			list += 12;
		}
		return;
	} else if (c == 'e') { // expansion
		uaecptr expbase = get_base("expansion.library", 378);
		if (expbase) {
			if (t[1] == 'm') {
				uaecptr list = get_long_debug(expbase + 74);
				while (list && get_long_debug(list)) {
					uaecptr name = get_long(list + 10);
					uae_s8 pri = get_byte(list + 9);
					uae_u16 flags = get_word_debug(list + 14);
					uae_u32 dn = get_long_debug(list + 16);
					uae_u8 *addr = get_real_address_debug(name);
					TCHAR *name1 = addr ? au((char*)addr) : au("<null>");
					my_trim(name1);
					console_out_f(_T("%08x %04x %08x %d %s\n"), list, flags, dn, pri, name1);
					xfree(name1);
					list = get_long_debug(list);
				}
			} else {
				list = get_long_debug(expbase + 60);
				while (list && get_long_debug(list)) {
					uae_u32 addr = get_long_debug(list + 32);
					uae_u16 rom_vector = get_word_debug(list + 16 + 10);
					uae_u8 type = get_byte_debug(list + 16 + 0);
					console_out_f(_T("%02x %02x %08x %08x %04x %02x %08x %04x (%u/%u)\n"),
						type, get_byte_debug(list + 16 + 2),
						addr, get_long_debug(list + 36),
						get_word_debug(list + 16 + 4), get_byte_debug(list + 16 + 1),
						get_long_debug(list + 16 + 6), rom_vector,
						get_word_debug(list + 16 + 4), get_byte_debug(list + 16 + 1));
					for (int i = 0; i < 16; i++) {
						console_out_f(_T("%02x"), get_byte_debug(list + 16 + i));
						if (i < 15)
							console_out_f(_T("."));
					}
					console_out_f(_T("\n"));
					if ((type & 0x10)) {
						uae_u8 diagarea[256];
						uae_u16 nameoffset;
						uaecptr rom = addr + rom_vector;
						uae_u8 config = get_byte_debug(rom);
						copyromdata(config, rom, 0, diagarea, 16);
						nameoffset = (diagarea[8] << 8) | diagarea[9];
						console_out_f(_T(" %02x %02x Size %04x Diag %04x Boot %04x Name %04x %04x %04x\n"),
							diagarea[0], diagarea[1],
							(diagarea[2] << 8) | diagarea[3],
							(diagarea[4] << 8) | diagarea[5],
							(diagarea[6] << 8) | diagarea[7],
							nameoffset,
							(diagarea[10] << 8) | diagarea[11],
							(diagarea[12] << 8) | diagarea[13]);
						if (nameoffset != 0 && nameoffset != 0xffff) {
							copyromdata(config, rom, nameoffset, diagarea, 256);
							diagarea[sizeof diagarea - 1] = 0;
							TCHAR *str = au((char*)diagarea);
							console_out_f(_T(" '%s'\n"), str);
							xfree(str);
						}
					}
					list = get_long_debug(list);
				}
			}
		}
		return;
	} else if (c == 'R') { // residents
		list = get_long_debug(execbase + 300);
		while (list) {
			uaecptr resident = get_long_debug (list);
			if (!resident)
				break;
			if (resident & 0x80000000) {
				console_out_f (_T("-> %08X\n"), resident & 0x7fffffff);
				list = resident & 0x7fffffff;
				continue;
			}
			uae_u8 *addr;
			addr = get_real_address_debug(get_long_debug (resident + 14));
			TCHAR *name1 = addr ? au ((char*)addr) : au("<null>");
			my_trim (name1);
			addr = get_real_address_debug(get_long_debug (resident + 18));
			TCHAR *name2 = addr ? au ((char*)addr) : au("<null>");
			my_trim (name2);
			console_out_f (_T("%08X %08X: %02X %3d %02X %+3.3d '%s' ('%s')\n"),
				list, resident,
				get_byte_debug (resident + 10), get_byte_debug (resident + 11),
				get_byte_debug (resident + 12), (uae_s8)get_byte_debug (resident + 13),
				name1, name2);
			xfree (name2);
			xfree (name1);
			list += 4;
		}
		return;
	} else if (c == 'f' || c== 'F') { // filesystem.resource
		uaecptr fs = get_base ("FileSystem.resource", 336);
		if (fs) {
			static const TCHAR *fsnames[] = {
				_T("DosType"),
				_T("Version"),
				_T("PatchFlags"),
				_T("Type"),
				_T("Task"),
				_T("Lock"),
				_T("Handler"),
				_T("StackSize"),
				_T("Priority"),
				_T("Startup"),
				_T("SegList"),
				_T("GlobalVec"),
				NULL
			};
			uae_u8 *addr = get_real_address_debug(get_long_debug (fs + 14));
			TCHAR *name = addr ? au ((char*)addr) : au ("<null>");
			my_trim (name);
			console_out_f (_T("%08x: '%s'\n"), fs, name);
			xfree (name);
			node = get_long_debug (fs + 18);
			while (get_long_debug (node)) {
				TCHAR *name = au ((char*)get_real_address_debug(get_long_debug (node + 10)));
				my_trim (name);
				console_out_f (_T("%08x: '%s'\n"), node, name);
				xfree (name);
				for (int i = 0; fsnames[i]; i++) {
					uae_u32 v = get_long_debug (node + 14 + i * 4);
					console_out_f (_T("%16s = %08x %d\n"), fsnames[i], v, v);
				}
				console_out_f (_T("\n"));
				node = get_long_debug (node);
			}

		} else {
			console_out_f (_T("FileSystem.resource not found.\n"));
		}
		return;
	} else if (c == 'm' || c == 'M') { // memory
		list = execbase + 322;
		node = get_long_debug (list);
		while (get_long_debug (node)) {
			TCHAR *name = au ((char*)get_real_address_debug(get_long_debug (node + 10)));
			uae_u16 v = get_word_debug (node + 8);
			console_out_f (_T("%08x %d %d %s\n"), node, (int)((v >> 8) & 0xff), (uae_s8)(v & 0xff), name);
			xfree (name);
			console_out_f (_T("Attributes %04x First %08x Lower %08x Upper %08x Free %d\n"),
				get_word_debug (node + 14), get_long_debug (node + 16), get_long_debug (node + 20), 
				get_long_debug (node + 24), get_long_debug (node + 28));
			uaecptr mc = get_long_debug (node + 16);
			while (mc) {
				uae_u32 mc1 = get_long_debug (mc);
				uae_u32 mc2 = get_long_debug (mc + 4);
				console_out_f (_T(" %08x: %08x-%08x,%08x,%08x (%d)\n"), mc, mc, mc + mc2, mc1, mc2, mc2);
				mc = mc1;
			}
			console_out_f (_T("\n"));
			node = get_long_debug (node);
		}
		return;
	}

	bool full = false;
	switch (c)
	{
	case 'r': // resources
		list = execbase + 336;
		break;
	case 'd': // devices
		list = execbase + 350;
		full = true;
		break;
	case 'l': // libraries
		list = execbase + 378;
		full = true;
		break;
	case 'p': // ports
		list = execbase + 392;
		break;
	case 's': // semaphores
		list = execbase + 532;
		break;
	}
	if (list == 0)
		return;
	node = get_long_debug (list);
	while (get_long_debug (node)) {
		TCHAR *name = au ((char*)get_real_address_debug(get_long_debug (node + 10)));
		uae_u16 v = get_word_debug (node + 8);
		console_out_f (_T("%08x %d %d"), node, (int)((v >> 8) & 0xff), (uae_s8)(v & 0xff));
		if (full) {
			uae_u16 ver = get_word_debug(node + 20);
			uae_u16 rev = get_word_debug(node + 22);
			uae_u32 op = get_word_debug(node + 32);
			console_out_f(_T(" %d.%d %d"), ver, rev, op);
		}
		console_out_f(_T(" %s"), name);
		xfree (name);
		if (full) {
			uaecptr idstring = get_long_debug(node + 24);
			if (idstring) {
				name = au((char*)get_real_address_debug(idstring));
				console_out_f(_T(" (%s)"), name);
				xfree(name);
			}
		}
		console_out_f(_T("\n"));
		node = get_long_debug (node);
	}
}

static void breakfunc(uae_u32 v)
{
	write_log(_T("Cycle breakpoint hit\n"));
	debugging = 1;
	set_special (SPCFLAG_BRK);
}

static int cycle_breakpoint(TCHAR **c)
{
	TCHAR nc = (*c)[0];
	next_char(c);
	if (more_params(c)) {
		int count = readint(c);
		if (nc == 's') {
			if (more_params(c)) {
				int mvp = maxvpos + lof_store;
				int hp = readint(c);
				int chp = current_hpos();
				if (count == vpos && chp < hp) {
					count += mvp - vpos;
				} else if (count >= vpos) {
					count = count - vpos;
				} else {
					count += mvp - vpos;
				}
				count *= maxhpos;
				if (hp >= chp) {
					count += hp - chp;
				} else {
					count += maxhpos - chp;
				}
			} else {
				count *= maxhpos;
			}
		}
		event2_newevent_x(-1, count, 0, breakfunc);
		return 1;
	}
	return 0;
}

#if 0
static int trace_same_insn_count;
static uae_u8 trace_insn_copy[10];
static struct regstruct trace_prev_regs;
#endif
static uaecptr nextpc;

int instruction_breakpoint (TCHAR **c)
{
	struct breakpoint_node *bpn;
	int i;

	if (more_params (c)) {
		TCHAR nc = _totupper ((*c)[0]);
		if (nc == 'O') {
			// bpnum register operation value1 [mask value2]
			next_char(c);
			if (more_params(c)) {
				int bpidx = readint(c);
				if (more_params(c) && bpidx >= 0 && bpidx < BREAKPOINT_TOTAL) {
					bpn = &bpnodes[bpidx];
					int regid = getregidx(c);
					if (regid >= 0) {
						bpn->type = regid;
						bpn->mask = 0xffffffff;
						if (more_params(c)) {
							int operid = getoperidx(c);
							if (more_params(c) && operid >= 0) {
								bpn->oper = operid;
								bpn->value1 = readhex(c);
								bpn->enabled = 1;
								if (more_params(c)) {
									bpn->mask = readhex(c);
									if (more_params(c))  {
										bpn->value2 = readhex(c);
									}
								}
								console_out(_T("Breakpoint added.\n"));
							}
						}
					}
				}
			}
			return 0;
		} else if (nc == 'I') {
			next_char (c);
			if (more_params (c))
				trace_param1 = readhex (c);
			else
				trace_param1 = 0x10000;
			trace_mode = TRACE_MATCH_INS;
			return 1;
		} else if (nc == 'D' && (*c)[1] == 0) {
			for (i = 0; i < BREAKPOINT_TOTAL; i++)
				bpnodes[i].enabled = 0;
			console_out(_T("All breakpoints removed.\n"));
			return 0;
		} else if (nc == 'R' && (*c)[1] == 0) {
			if (more_params(c)) {
				int bpnum = readint(c);
				if (bpnum >= 0 && bpnum < BREAKPOINT_TOTAL) {
					bpnodes[bpnum].enabled = 0;
					console_out_f(_T("Breakpoint %d removed.\n"), bpnum);
				}
			}
			return 0;
		} else if (nc == 'L') {
			int got = 0;
			for (i = 0; i < BREAKPOINT_TOTAL; i++) {
				bpn = &bpnodes[i];
				if (!bpn->enabled)
					continue;
				console_out_f (_T("%d: %s %s %08x [%08x %08x]\n"), i, debugregs[bpn->type], debugoper[bpn->oper], bpn->value1, bpn->mask, bpn->value2);
				got = 1;
			}
			if (!got)
				console_out (_T("No breakpoints.\n"));
			else
				console_out (_T("\n"));
			return 0;
		}
		trace_mode = TRACE_RANGE_PC;
		trace_param1 = readhex (c);
		if (more_params (c)) {
			trace_param2 = readhex (c);
			return 1;
		} else {
			for (i = 0; i < BREAKPOINT_TOTAL; i++) {
				bpn = &bpnodes[i];
				if (bpn->enabled && bpn->value1 == trace_param1) {
					bpn->enabled = 0;
					console_out (_T("Breakpoint removed.\n"));
					trace_mode = 0;
					return 0;
				}
			}
			for (i = 0; i < BREAKPOINT_TOTAL; i++) {
				bpn = &bpnodes[i];
				if (bpn->enabled)
					continue;
				bpn->value1 = trace_param1;
				bpn->type = BREAKPOINT_REG_PC;
				bpn->oper = BREAKPOINT_CMP_EQUAL;
				bpn->enabled = 1;
				console_out (_T("Breakpoint added.\n"));
				trace_mode = 0;
				break;
			}
			return 0;
		}
	}
	trace_mode = TRACE_RAM_PC;
	return 1;
}

static int process_breakpoint (TCHAR **c)
{
	processptr = 0;
	xfree (processname);
	processname = NULL;
	if (!more_params (c))
		return 0;
	if (**c == '\"') {
		TCHAR pn[200];
		next_string (c, pn, 200, 0);
		processname = ua (pn);
	} else {
		processptr = readhex (c);
	}
	trace_mode = TRACE_CHECKONLY;
	return 1;
}

static void saveloadmem (TCHAR **cc, bool save)
{
	uae_u8 b;
	uae_u32 src, src2;
	int len, len2;
	TCHAR *name;
	FILE *fp;

	if (!more_params (cc))
		goto S_argh;

	name = *cc;
	while (**cc != '\0' && !isspace (**cc))
		(*cc)++;
	if (!isspace (**cc))
		goto S_argh;

	**cc = '\0';
	(*cc)++;
	if (!more_params (cc))
		goto S_argh;
	src2 = src = readhex (cc);
	if (save) {
		if (!more_params(cc))
			goto S_argh;
	}
	len2 = len = -1;
	if (more_params(cc)) {
		len2 = len = readhex (cc);
	}
	fp = uae_tfopen (name, save ? _T("wb") : _T("rb"));
	if (fp == NULL) {
		console_out_f (_T("Couldn't open file '%s'.\n"), name);
		return;
	}
	if (save) {
		while (len > 0) {
			b = get_byte_debug (src);
			src++;
			len--;
			if (fwrite (&b, 1, 1, fp) != 1) {
				console_out (_T("Error writing file.\n"));
				break;
			}
		}
		if (len == 0)
			console_out_f (_T("Wrote %08X - %08X (%d bytes) to '%s'.\n"),
				src2, src2 + len2, len2, name);
	} else {
		len2 = 0;
		while (len != 0) {
			if (fread(&b, 1, 1, fp) != 1) {
				if (len > 0)
					console_out (_T("Unexpected end of file.\n"));
				len = 0;
				break;
			}
			put_byte (src, b);
			src++;
			if (len > 0)
				len--;
			len2++;
		}
		if (len == 0)
			console_out_f (_T("Read %08X - %08X (%d bytes) to '%s'.\n"),
				src2, src2 + len2, len2, name);
	}
	fclose (fp);
	return;
S_argh:
	console_out (_T("Command needs more arguments!\n"));
}

static void searchmem (TCHAR **cc)
{
	int i, sslen, got, val, stringmode;
	uae_u8 ss[256];
	uae_u32 addr, endaddr;
	TCHAR nc;

	got = 0;
	sslen = 0;
	stringmode = 0;
	ignore_ws (cc);
	if (**cc == '"') {
		stringmode = 1;
		(*cc)++;
		while (**cc != '"' && **cc != 0) {
			ss[sslen++] = tolower (**cc);
			(*cc)++;
		}
		if (**cc != 0)
			(*cc)++;
	} else {
		for (;;) {
			if (**cc == 32 || **cc == 0)
				break;
			nc = _totupper (next_char (cc));
			if (isspace (nc))
				break;
			if (isdigit(nc))
				val = nc - '0';
			else
				val = nc - 'A' + 10;
			if (val < 0 || val > 15)
				return;
			val *= 16;
			if (**cc == 32 || **cc == 0)
				break;
			nc = _totupper (next_char (cc));
			if (isspace (nc))
				break;
			if (isdigit(nc))
				val += nc - '0';
			else
				val += nc - 'A' + 10;
			if (val < 0 || val > 255)
				return;
			ss[sslen++] = (uae_u8)val;
		}
	}
	if (sslen == 0)
		return;
	ignore_ws (cc);
	addr = 0;
	endaddr = lastaddr ();
	if (more_params (cc)) {
		addr = readhex (cc);
		if (more_params (cc))
			endaddr = readhex (cc);
	}
	console_out_f (_T("Searching from %08X to %08X..\n"), addr, endaddr);
	while ((addr = nextaddr (addr, endaddr, NULL)) != 0xffffffff) {
		if (addr == endaddr)
			break;
		for (i = 0; i < sslen; i++) {
			uae_u8 b = get_byte_debug (addr + i);
			if (stringmode) {
				if (tolower (b) != ss[i])
					break;
			} else {
				if (b != ss[i])
					break;
			}
		}
		if (i == sslen) {
			got++;
			console_out_f (_T(" %08X"), addr);
			if (got > 100) {
				console_out (_T("\nMore than 100 results, aborting.."));
				break;
			}
		}
		if  (iscancel (65536)) {
			console_out_f (_T("Aborted at %08X\n"), addr);
			break;
		}
	}
	if (!got)
		console_out (_T("nothing found"));
	console_out (_T("\n"));
}

static int staterecorder (TCHAR **cc)
{
#if 0
	TCHAR nc;

	if (!more_params (cc)) {
		if (savestate_dorewind (1)) {
			debug_rewind = 1;
			return 1;
		}
		return 0;
	}
	nc = next_char (cc);
	if (nc == 'l') {
		savestate_listrewind ();
		return 0;
	}
#endif
	return 0;
}

static int debugtest_modes[DEBUGTEST_MAX];
static const TCHAR *debugtest_names[] = {
	_T("Blitter"), _T("Keyboard"), _T("Floppy")
};

void debugtest (enum debugtest_item di, const TCHAR *format, ...)
{
	va_list parms;
	TCHAR buffer[1000];

	if (!debugtest_modes[di])
		return;
	va_start (parms, format);
	_vsntprintf (buffer, 1000 - 1, format, parms);
	va_end (parms);
	write_log (_T("%s PC=%08X: %s\n"), debugtest_names[di], M68K_GETPC, buffer);
	if (debugtest_modes[di] == 2)
		activate_debugger_new();
}

static void debugtest_set (TCHAR **inptr)
{
	int i, val, val2;
	ignore_ws (inptr);

	val2 = 1;
	if (!more_params (inptr)) {
		for (i = 0; i < DEBUGTEST_MAX; i++)
			debugtest_modes[i] = 0;
		console_out (_T("All debugtests disabled\n"));
		return;
	}
	val = readint (inptr);
	if (more_params (inptr)) {
		val2 = readint (inptr);
		if (val2 > 0)
			val2 = 2;
	}
	if (val < 0) {
		for (i = 0; i < DEBUGTEST_MAX; i++)
			debugtest_modes[i] = val2;
		console_out (_T("All debugtests enabled\n"));
		return;
	}
	if (val >= 0 && val < DEBUGTEST_MAX) {
		if (debugtest_modes[val])
			debugtest_modes[val] = 0;
		else
			debugtest_modes[val] = val2;
		console_out_f (_T("Debugtest '%s': %s. break = %s\n"),
			debugtest_names[val], debugtest_modes[val] ? _T("on") :_T("off"), val2 == 2 ? _T("on") : _T("off"));
	}
}

static void debug_sprite (TCHAR **inptr)
{
	uaecptr saddr, addr, addr2;
	int xpos, xpos_ecs;
	int ypos, ypos_ecs;
	int ypose, ypose_ecs;
	int attach;
	uae_u64 w1, w2, ww1, ww2;
	int size = 1, width;
	int ecs, sh10;
	int y, i;
	TCHAR tmp[80];
	int max = 2;

	addr2 = 0;
	ignore_ws (inptr);
	addr = readhex (inptr);
	ignore_ws (inptr);
	if (more_params (inptr))
		size = readhex (inptr);
	if (size != 1 && size != 2 && size != 4) {
		addr2 = size;
		ignore_ws (inptr);
		if (more_params (inptr))
			size = readint (inptr);
		if (size != 1 && size != 2 && size != 4)
			size = 1;
	}
	for (;;) {
		ecs = 0;
		sh10 = 0;
		saddr = addr;
		width = size * 16;
		w1 = get_word_debug (addr);
		w2 = get_word_debug (addr + size * 2);
		console_out_f (_T("    %06X "), addr);
		for (i = 0; i < size * 2; i++)
			console_out_f (_T("%04X "), get_word_debug (addr + i * 2));
		console_out_f (_T("\n"));

		ypos = w1 >> 8;
		xpos = w1 & 255;
		ypose = w2 >> 8;
		attach = (w2 & 0x80) ? 1 : 0;
		if (w2 & 4)
			ypos |= 256;
		if (w2 & 2)
			ypose |= 256;
		ypos_ecs = ypos;
		ypose_ecs = ypose;
		if (w2 & 0x40)
			ypos_ecs |= 512;
		if (w2 & 0x20)
			ypose_ecs |= 512;
		xpos <<= 1;
		if (w2 & 0x01)
			xpos |= 1;
		xpos_ecs = xpos << 2;
		if (w2 & 0x10)
			xpos_ecs |= 2;
		if (w2 & 0x08)
			xpos_ecs |= 1;
		if (w2 & (0x40 | 0x20 | 0x10 | 0x08))
			ecs = 1;
		if (w1 & 0x80)
			sh10 = 1;
		if (ypose < ypos)
			ypose += 256;

		for (y = ypos; y < ypose; y++) {
			int x;
			addr += size * 4;
			if (addr2)
				addr2 += size * 4;
			if (size == 1) {
				w1 = get_word_debug (addr);
				w2 = get_word_debug (addr + 2);
				if (addr2) {
					ww1 = get_word_debug (addr2);
					ww2 = get_word_debug (addr2 + 2);
				}
			} else if (size == 2) {
				w1 = get_long_debug (addr);
				w2 = get_long_debug (addr + 4);
				if (addr2) {
					ww1 = get_long_debug (addr2);
					ww2 = get_long_debug (addr2 + 4);
				}
			} else if (size == 4) {
				w1 = get_long_debug (addr + 0);
				w2 = get_long_debug (addr + 8);
				w1 <<= 32;
				w2 <<= 32;
				w1 |= get_long_debug (addr + 4);
				w2 |= get_long_debug (addr + 12);
				if (addr2) {
					ww1 = get_long_debug (addr2 + 0);
					ww2 = get_long_debug (addr2 + 8);
					ww1 <<= 32;
					ww2 <<= 32;
					ww1 |= get_long_debug (addr2 + 4);
					ww2 |= get_long_debug (addr2 + 12);
				}
			}
			width = size * 16;
			for (x = 0; x < width; x++) {
				int v1 = w1 & 1;
				int v2 = w2 & 1;
				int v = v1 * 2 + v2;
				w1 >>= 1;
				w2 >>= 1;
				if (addr2) {
					int vv1 = ww1 & 1;
					int vv2 = ww2 & 1;
					int vv = vv1 * 2 + vv2;
					vv1 >>= 1;
					vv2 >>= 1;
					v *= 4;
					v += vv;
					tmp[width - (x + 1)] = v >= 10 ? 'A' + v - 10 : v + '0';
				} else {
					tmp[width - (x + 1)] = v + '0';
				}
			}
			tmp[width] = 0;
			console_out_f (_T("%3d %06X %s\n"), y, addr, tmp);
		}

		console_out_f (_T("Sprite address %08X, width = %d\n"), saddr, size * 16);
		console_out_f (_T("OCS: StartX=%d StartY=%d EndY=%d\n"), xpos, ypos, ypose);
		console_out_f (_T("ECS: StartX=%d (%d.%d) StartY=%d EndY=%d%s\n"), xpos_ecs, xpos_ecs / 4, xpos_ecs & 3, ypos_ecs, ypose_ecs, ecs ? _T(" (*)") : _T(""));
		console_out_f (_T("Attach: %d. AGA SSCAN/SH10 bit: %d\n"), attach, sh10);

		addr += size * 4;
		if (get_word_debug (addr) == 0 && get_word_debug (addr + size * 4) == 0)
			break;
		max--;
		if (max <= 0)
			break;
	}

}

int debug_write_memory_16 (uaecptr addr, uae_u16 v)
{
	addrbank *ad;
	
	ad = &get_mem_bank (addr);
	if (ad) {
		ad->wput (addr, v);
		return 1;
	}
	return -1;
}
int debug_write_memory_8 (uaecptr addr, uae_u8 v)
{
	addrbank *ad;
	
	ad = &get_mem_bank (addr);
	if (ad) {
		ad->bput (addr, v);
		return 1;
	}
	return -1;
}
int debug_peek_memory_16 (uaecptr addr)
{
	addrbank *ad;
	
	ad = &get_mem_bank (addr);
	if (ad->flags & (ABFLAG_RAM | ABFLAG_ROM | ABFLAG_ROMIN | ABFLAG_SAFE))
		return ad->wget (addr);
	if (ad == &custom_bank) {
		addr &= 0x1fe;
		return (ar_custom[addr + 0] << 8) | ar_custom[addr + 1];
	}
	return -1;
}
int debug_read_memory_16 (uaecptr addr)
{
	addrbank *ad;
	
	ad = &get_mem_bank (addr);
	if (ad)
		return ad->wget (addr);
	return -1;
}
int debug_read_memory_8 (uaecptr addr)
{
	addrbank *ad;
	
	ad = &get_mem_bank (addr);
	if (ad)
		return ad->bget (addr);
	return -1;
}

static void disk_debug (TCHAR **inptr)
{
	TCHAR parm[10];
	int i;

	if (**inptr == 'd') {
		(*inptr)++;
		ignore_ws (inptr);
		disk_debug_logging = readint (inptr);
		console_out_f (_T("Disk logging level %d\n"), disk_debug_logging);
		return;
	}
	disk_debug_mode = 0;
	disk_debug_track = -1;
	ignore_ws (inptr);
	if (!next_string (inptr, parm, sizeof (parm) / sizeof (TCHAR), 1))
		goto end;
	for (i = 0; i < _tcslen(parm); i++) {
		if (parm[i] == 'R')
			disk_debug_mode |= DISK_DEBUG_DMA_READ;
		if (parm[i] == 'W')
			disk_debug_mode |= DISK_DEBUG_DMA_WRITE;
		if (parm[i] == 'P')
			disk_debug_mode |= DISK_DEBUG_PIO;
	}
	if (more_params(inptr))
		disk_debug_track = readint (inptr);
	if (disk_debug_track < 0 || disk_debug_track > 2 * 83)
		disk_debug_track = -1;
	if (disk_debug_logging == 0)
		disk_debug_logging = 1;
end:
	console_out_f (_T("Disk breakpoint mode %c%c%c track %d\n"),
		disk_debug_mode & DISK_DEBUG_DMA_READ ? 'R' : '-',
		disk_debug_mode & DISK_DEBUG_DMA_WRITE ? 'W' : '-',
		disk_debug_mode & DISK_DEBUG_PIO ? 'P' : '-',
		disk_debug_track);
}

static void find_ea (TCHAR **inptr)
{
	uae_u32 ea, sea, dea;
	uaecptr addr, end, end2;
	int hits = 0;

	addr = 0;
	end = lastaddr ();
	ea = readhex (inptr);
	if (more_params(inptr)) {
		addr = readhex (inptr);
		if (more_params(inptr))
			end = readhex (inptr);
	}
	console_out_f (_T("Searching from %08X to %08X\n"), addr, end);
	end2 = 0;
	while((addr = nextaddr (addr, end, &end2)) != 0xffffffff) {
		if ((addr & 1) == 0 && addr + 6 <= end2) {
			sea = 0xffffffff;
			dea = 0xffffffff;
			m68k_disasm_ea (addr, NULL, 1, &sea, &dea, 0xffffffff);
			if (ea == sea || ea == dea) {
				m68k_disasm (addr, NULL, 0xffffffff, 1);
				hits++;
				if (hits > 100) {
					console_out_f (_T("Too many hits. End addr = %08X\n"), addr);
					break;
				}
			}
			if  (iscancel (65536)) {
				console_out_f (_T("Aborted at %08X\n"), addr);
				break;
			}
		}
	}
}

static void m68k_modify (TCHAR **inptr)
{
	uae_u32 v;
	TCHAR parm[10];
	TCHAR c1, c2;
	int i;

	if (!next_string (inptr, parm, sizeof (parm) / sizeof (TCHAR), 1))
		return;
	c1 = _totupper (parm[0]);
	c2 = 99;
	if (c1 == 'A' || c1 == 'D' || c1 == 'P') {
		c2 = _totupper (parm[1]);
		if (isdigit (c2))
			c2 -= '0';
		else
			c2 = 99;
	}
	v = readhex (inptr);
	if (c1 == 'A' && c2 < 8)
		regs.regs[8 + c2] = v;
	else if (c1 == 'D' && c2 < 8)
		regs.regs[c2] = v;
	else if (c1 == 'P' && c2 == 0)
		regs.irc = v;
	else if (c1 == 'P' && c2 == 1)
		regs.ir = v;
	else if (!_tcscmp (parm, _T("SR"))) {
		regs.sr = v;
		MakeFromSR ();
	} else if (!_tcscmp (parm, _T("CCR"))) {
		regs.sr = (regs.sr & ~31) | (v & 31);
		MakeFromSR ();
	} else if (!_tcscmp (parm, _T("USP"))) {
		regs.usp = v;
	} else if (!_tcscmp (parm, _T("ISP"))) {
		regs.isp = v;
	} else if (!_tcscmp (parm, _T("PC"))) {
		m68k_setpc (v);
		fill_prefetch ();
	} else {
		for (i = 0; m2cregs[i].regname; i++) {
			if (!_tcscmp (parm, m2cregs[i].regname))
				val_move2c2 (m2cregs[i].regno, v);
		}
	}
}

static void ppc_disasm(uaecptr addr, uaecptr *nextpc, int cnt)
{
	PPCD_CB disa;

	while(cnt-- > 0) {
		uae_u32 instr = get_long_debug(addr);
		disa.pc = addr;
		disa.instr = instr;
		PPCDisasm(&disa);
		TCHAR *mnemo = au(disa.mnemonic);
		TCHAR *ops = au(disa.operands);
		console_out_f(_T("%08X  %08X  %-12s%-30s\n"), addr, instr, mnemo, ops);
		xfree(ops);
		xfree(mnemo);
		addr += 4;
	}
	if (nextpc)
		*nextpc = addr;
}

static void dma_disasm(int frames, int vp, int hp, int frames_end, int vp_end, int hp_end)
{
	if (!dma_record[0] || frames < 0 || vp < 0 || hp < 0)
		return;
	for (;;) {
		struct dma_rec *dr = NULL;
		if (dma_record_frame[0] == frames)
			dr = &dma_record[0][vp * NR_DMA_REC_HPOS + hp];
		else if (dma_record_frame[1] == frames)
			dr = &dma_record[1][vp * NR_DMA_REC_HPOS + hp];
		if (!dr)
			return;
		TCHAR l1[16], l2[16], l3[16], l4[16];
		if (get_record_dma_info(dr, hp, vp, 0, l1, l2, l3, l4, NULL)) {
			console_out_f(_T(" - %02X %s %s %s\n"), hp, l2, l3, l4);
		}
		hp++;
		if (hp >= maxhpos) {
			hp = 0;
			vp++;
			if (vp >= maxvpos + 1) {
				vp = 0;
				frames++;
				break;
			}
		}
		if ((frames == frames_end && vp == vp_end && hp == hp_end) || frames > frames_end)
			break;
		if (vp_end < 0 || hp_end < 0 || frames_end < 0)
			break;
	}
}

static uaecptr nxdis, nxmem, asmaddr;
static bool ppcmode, asmmode;

static bool debug_line (TCHAR *input)
{
	TCHAR cmd, *inptr;
	uaecptr addr;

	inptr = input;

	if (asmmode) {
		if (more_params(&inptr)) {
			if (!_tcsicmp(inptr, _T("x"))) {
				asmmode = false;
				return false;
			}
			uae_u16 asmout[16];
			int inss = m68k_asm(inptr, asmout, asmaddr);
			if (inss > 0) {
				for (int i = 0; i < inss; i++) {
					put_word(asmaddr + i * 2, asmout[i]);
				}
				m68k_disasm(asmaddr, &nxdis, 0xffffffff, 1);
				asmaddr = nxdis;
			}
			console_out_f(_T("%08X "), asmaddr);
			return false;
		} else {
			asmmode = false;
			return false;
		}
	}

	cmd = next_char (&inptr);

	switch (cmd)
	{
		case 'I':
		if (more_params (&inptr)) {
			static int recursive;
			if (!recursive) {
				recursive++;
				handle_custom_event(inptr, 0);
				device_check_config();
				recursive--;
			}
		}
		break;
		case 'c': dumpcia (); dumpdisk (_T("DEBUG")); dumpcustom (); break;
		case 'i':
		{
			if (*inptr == 'l') {
				next_char (&inptr);
				if (more_params (&inptr)) {
					debug_illegal_mask = readhex (&inptr);
					if (more_params(&inptr))
						debug_illegal_mask |= ((uae_u64)readhex(&inptr)) << 32;
				} else {
					debug_illegal_mask = debug_illegal ? 0 : -1;
					debug_illegal_mask &= ~((uae_u64)255 << 24); // mask interrupts
				}
				console_out_f (_T("Exception breakpoint mask: %08X %08X\n"), (uae_u32)(debug_illegal_mask >> 32), (uae_u32)debug_illegal_mask);
				debug_illegal = debug_illegal_mask ? 1 : 0;
			} else {
				addr = 0xffffffff;
				if (more_params (&inptr))
					addr = readhex (&inptr);
				dump_vectors (addr);
			}
			break;
		}
		case 'e':
		{
			bool aga = tolower(*inptr) == 'a';
			if (aga)
				next_char(&inptr);
			bool ext = tolower(*inptr) == 'x';
			dump_custom_regs(aga, ext);
		}
		break;
		case 'r':
		{
			if (*inptr == 'c') {
				next_char(&inptr);
				m68k_dumpcache(*inptr == 'd');
			} else if (*inptr == 's') {
				if (*(inptr + 1) == 's')
					debugmem_list_stackframe(true);
				else
					debugmem_list_stackframe(false);
			} else if (more_params(&inptr)) {
				m68k_modify(&inptr);
			} else {
				m68k_dumpstate(&nextpc, 0xffffffff);
			}
		}
		break;
		case 'D': deepcheatsearch (&inptr); break;
		case 'C': cheatsearch (&inptr); break;
		case 'W': writeintomem (&inptr); break;
		case 'w': memwatch (&inptr); break;
		case 'S': saveloadmem (&inptr, true); break;
		case 'L': saveloadmem (&inptr, false); break;
		case 's':
			if (*inptr == 'e' && *(inptr + 1) == 'g') {
				next_char(&inptr);
				next_char(&inptr);
				addr = 0xffffffff;
				if (*inptr == 's') {
					debugmem_list_segment(1, addr);
				} else {
					if (more_params(&inptr)) {
						addr = readhex(&inptr);
					}
					debugmem_list_segment(0, addr);
				}
			} else if (*inptr == 'c') {
				screenshot(-1, 1, 1);
			} else if (*inptr == 'p') {
				inptr++;
				debug_sprite (&inptr);
			} else if (*inptr == 'm') {
				if (*(inptr + 1) == 'c') {
					next_char (&inptr);
					next_char (&inptr);
					if (!smc_table)
						smc_detect_init (&inptr);
					else
						smc_free ();
				}
			} else {
				searchmem (&inptr);
			}
			break;
		case 'a':
			asmaddr = nxdis;
			if (more_params(&inptr)) {
				asmaddr = readhex(&inptr);
				if (more_params(&inptr)) {
					uae_u16 asmout[16];
					int inss = m68k_asm(inptr, asmout, asmaddr);
					if (inss > 0) {
						for (int i = 0; i < inss; i++) {
							put_word(asmaddr + i * 2, asmout[i]);
						}
						m68k_disasm(asmaddr, &nxdis, 1, 0xffffffff);
						asmaddr = nxdis;
						return false;
					}
				}
			}
			asmmode = true;
			console_out_f(_T("%08X "), asmaddr);
		break;
		case 'd':
			{
				if (*inptr == 'i') {
					next_char (&inptr);
					disk_debug (&inptr);
				} else if (*inptr == 'j') {
					inptr++;
					inputdevice_logging = 1 | 2;
					if (more_params (&inptr))
						inputdevice_logging = readint (&inptr);
					console_out_f (_T("Input logging level %d\n"), inputdevice_logging);
				} else if (*inptr == 'm') {
					memory_map_dump_2 (0);
				} else if (*inptr == 't') {
					next_char (&inptr);
					debugtest_set (&inptr);
#ifdef _WIN32
				} else if (*inptr == 'g') {
					extern void update_disassembly (uae_u32);
					next_char (&inptr);
					if (more_params (&inptr))
						update_disassembly (readhex (&inptr));
#endif
				} else {
					uae_u32 daddr;
					int count;
					if (*inptr == 'p') {
						ppcmode = true;
						next_char(&inptr);
					} else if(*inptr == 'o') {
						ppcmode = false;
						next_char(&inptr);
					}
					if (more_params (&inptr))
						daddr = readhex (&inptr);
					else
						daddr = nxdis;
					if (more_params (&inptr))
						count = readhex (&inptr);
					else
						count = 10;
					if (ppcmode) {
						ppc_disasm(daddr, &nxdis, count);
					} else {
						m68k_disasm (daddr, &nxdis, 0xffffffff, count);
					}
				}
			}
			break;
		case 'T':
			if (inptr[0] == 'L')
				debugger_scan_libraries();
			else if (inptr[0] == 't' || inptr[0] == 0)
				show_exec_tasks ();
			else
				show_exec_lists (&inptr[0]);
			break;
		case 't':
			no_trace_exceptions = 0;
			debug_cycles();
			if (*inptr == 't') {
				no_trace_exceptions = 1;
				inptr++;
			}
			if (*inptr == 'r') {
				// break when PC in debugmem
				if (debugmem_get_range(&trace_param1, &trace_param2)) {
					trace_mode = TRACE_RANGE_PC;
					return true;
				}
			} else if (*inptr == 's') {
				if (*(inptr + 1) == 'e') {
					debugmem_enable_stackframe(true);
				} else if (*(inptr + 1) == 'd') {
					debugmem_enable_stackframe(false);
				} else if (*(inptr + 1) == 'p') {
					if (debugmem_break_stack_pop()) {
						debugger_active = 0;
						return true;
					}
				} else {
					if (debugmem_break_stack_pop()) {
						debugger_active = 0;
						return true;
					}
				}
			} else if (*inptr == 'l') {
				// skip next source line
				if (debugmem_isactive()) {
					trace_mode = TRACE_SKIP_LINE;
					trace_param1 = 1;
					trace_param2 = debugmem_get_sourceline(M68K_GETPC, NULL, 0);
					return true;
				}
			} else {
				if (more_params(&inptr))
					trace_param1 = readint(&inptr);
				if (trace_param1 <= 0 || trace_param1 > 10000)
					trace_param1 = 1;
				trace_mode = TRACE_SKIP_INS;
				exception_debugging = 1;
				return true;
			}
			break;
		case 'z':
			trace_mode = TRACE_MATCH_PC;
			trace_param1 = nextpc;
			exception_debugging = 1;
			debug_cycles();
			return true;

		case 'f':
			if (inptr[0] == 'a') {
				next_char (&inptr);
				find_ea (&inptr);
			} else if (inptr[0] == 'p') {
				inptr++;
				if (process_breakpoint (&inptr))
					return true;
			} else if (inptr[0] == 'c' || inptr[0] == 's') {
				if (cycle_breakpoint(&inptr))
					return true;
			} else if (inptr[0] == 'e' && inptr[1] == 'n') {
				break_if_enforcer = break_if_enforcer ? false : true;
				console_out_f(_T("Break when enforcer hit: %s\n"), break_if_enforcer ? _T("enabled") : _T("disabled"));
			} else {
				if (instruction_breakpoint(&inptr)) {
					debug_cycles();
					return true;
				}
			}
			break;

		case 'q':
			uae_quit();
			deactivate_debugger();
			return true;

		case 'g':
			if (more_params (&inptr)) {
				m68k_setpc (readhex (&inptr));
				fill_prefetch ();
			}
			deactivate_debugger();
			return true;

		case 'x':
			if (_totupper(inptr[0]) == 'X') {
				debugger_change(-1);
			} else {
				deactivate_debugger();
				close_console();
				return true;
			}
			break;

		case 'H':
			{
				int count, temp, badly, skip;
				uae_u32 addr = 0;
				uae_u32 oldpc = m68k_getpc ();
				int lastframes, lastvpos, lasthpos;
				struct regstruct save_regs = regs;

				badly = 0;
				if (inptr[0] == 'H') {
					badly = 1;
					inptr++;
				}

				if (more_params(&inptr))
					count = readint (&inptr);
				else
					count = 10;
				if (count > 1000) {
					addr = count;
					count = MAX_HIST;
				}
				if (count < 0)
					break;
				skip = count;
				if (more_params (&inptr))
					skip = count - readint (&inptr);

				temp = lasthist;
				while (count-- > 0 && temp != firsthist) {
					if (temp == 0)
						temp = MAX_HIST - 1;
					else
						temp--;
				}
				lastframes = lastvpos = lasthpos = -1;
				while (temp != lasthist) {
					regs = history[temp].regs;
					if (regs.pc == addr || addr == 0) {
						m68k_setpc (regs.pc);
						if (badly) {
							m68k_dumpstate(NULL, 0xffffffff);
						} else {
							if (lastvpos >= 0) {
								dma_disasm(lastframes, lastvpos, lasthpos, history[temp].fp, history[temp].vpos, history[temp].hpos);
							}
							lastframes = history[temp].fp;
							lastvpos = history[temp].vpos;
							lasthpos = history[temp].hpos;
							console_out_f(_T("%2d "), regs.intmask ? regs.intmask : (regs.s ? -1 : 0));
							m68k_disasm (regs.pc, NULL, 0xffffffff, 1);
						}
						if (addr && regs.pc == addr)
							break;
					}
					if (skip-- < 0)
						break;
					if (++temp == MAX_HIST)
						temp = 0;
				}
				regs = save_regs;
				m68k_setpc (oldpc);
			}
			break;
		case 'M':
			if (more_params (&inptr)) {
				switch (next_char (&inptr))
				{
				case 'a':
					if (more_params (&inptr))
						audio_channel_mask = readhex (&inptr);
					console_out_f (_T("Audio mask = %02X\n"), audio_channel_mask);
					break;
				case 's':
					if (more_params (&inptr))
						debug_sprite_mask = readhex (&inptr);
					console_out_f (_T("Sprite mask: %02X\n"), debug_sprite_mask);
					break;
				case 'b':
					if (more_params (&inptr)) {
						debug_bpl_mask = readhex (&inptr) & 0xff;
						if (more_params (&inptr))
							debug_bpl_mask_one = readhex (&inptr) & 0xff;
						notice_screen_contents_lost(0);
					}
					console_out_f (_T("Bitplane mask: %02X (%02X)\n"), debug_bpl_mask, debug_bpl_mask_one);
					break;
				}
			}
			break;
		case 'm':
			{
				uae_u32 maddr;
				int lines;
#ifdef _WIN32
				if (*inptr == 'g') {
					extern void update_memdump (uae_u32);
					next_char (&inptr);
					if (more_params (&inptr))
						update_memdump (readhex (&inptr));
					break;
				}
#endif
				if (*inptr == 'm' && inptr[1] == 'u') {
					inptr += 2;
					if (inptr[0] == 'd') {
						if (currprefs.mmu_model >= 68040)
							mmu_dump_tables();
					} else {
						if (currprefs.mmu_model) {
							if (more_params (&inptr))
								debug_mmu_mode = readint (&inptr);
							else
								debug_mmu_mode = 0;
							console_out_f (_T("MMU translation function code = %d\n"), debug_mmu_mode);
						}
					}
					break;
				}
				if (more_params (&inptr)) {
					maddr = readhex (&inptr);
				} else {
					maddr = nxmem;
				}
				if (more_params (&inptr))
					lines = readhex (&inptr);
				else
					lines = 20;
				dumpmem (maddr, &nxmem, lines);
			}
			break;
		case 'v':
		case 'V':
			{
				int v1 = vpos, v2 = 0;
				if (*inptr == 'h') {
					inptr++;
					if (more_params(&inptr) && *inptr == '?') {
						mw_help();
					} else if (!heatmap) {
						debug_heatmap = 1;
						init_heatmap();
						if (more_params(&inptr)) {
							v1 = readint(&inptr);
							if (v1 < 0) {
								debug_heatmap = 2;
							}
						}
						TCHAR buf[200];
						TCHAR *pbuf;
						_stprintf(buf, _T("0 dff000 200 NONE"));
						pbuf = buf;
						memwatch(&pbuf);
						_stprintf(buf, _T("1 0 %08x NONE"), currprefs.chipmem_size);
						pbuf = buf;
						memwatch(&pbuf);
						if (currprefs.bogomem_size) {
							_stprintf(buf, _T("2 c00000 %08x NONE"), currprefs.bogomem_size);
							pbuf = buf;
							memwatch(&pbuf);
						}
						console_out_f(_T("Heatmap enabled\n"));
					} else {
						if (*inptr == 'd') {
							console_out_f(_T("Heatmap disabled\n"));
							free_heatmap();
						} else {
							heatmap_stats(&inptr);
						}
					}
				} else if (*inptr == 'o') {
					if (debug_dma) {
						console_out_f (_T("DMA debugger disabled\n"), debug_dma);
						record_dma_reset();
						reset_drawing();
						debug_dma = 0;
					}
				} else if (*inptr == 'm') {
					set_debug_colors();
					inptr++;
					if (more_params(&inptr)) {
						v1 = readint(&inptr);
						if (v1 >= 0 && v1 < DMARECORD_MAX) {
							v2 = readint(&inptr);
							if (v2 >= 0 && v2 <= DMARECORD_SUBITEMS) {
								if (more_params(&inptr)) {
									uae_u32 rgb = readhex(&inptr);
									if (v2 == 0) {
										for (int i = 0; i < DMARECORD_SUBITEMS; i++) {
											debug_colors[v1].l[i] = rgb;
										}
									} else {
										v2--;
										debug_colors[v1].l[v2] = rgb;
									}
									debug_colors[v1].enabled = true;
								} else {
									debug_colors[v1].enabled = !debug_colors[v1].enabled;
								}
								console_out_f(_T("%d,%d: %08x %s %s\n"), v1, v2, debug_colors[v1].l[v2], debug_colors[v1].enabled ? _T("*") : _T(" "), debug_colors[v1].name);
							}
						}
					} else {
						for (int i = 0; i < DMARECORD_MAX; i++) {
							for (int j = 0; j < DMARECORD_SUBITEMS; j++) {
								if (j < debug_colors[i].max) {
									console_out_f(_T("%d,%d: %08x %s %s\n"), i, j, debug_colors[i].l[j], debug_colors[i].enabled ? _T("*") : _T(" "), debug_colors[i].name);
								}
							}
						}
					}
				} else {
					if (more_params(&inptr) && *inptr == '?') {
						mw_help();
					} else {
						free_heatmap();
						if (more_params (&inptr))
							v1 = readint (&inptr);
						if (more_params (&inptr))
							v2 = readint (&inptr);
						if (debug_dma && v1 >= 0 && v2 >= 0) {
							decode_dma_record (v2, v1, cmd == 'v', false);
						} else {
							if (debug_dma) {
								record_dma_reset();
								reset_drawing();
							}
							debug_dma = v1 < 0 ? -v1 : 1;
							console_out_f (_T("DMA debugger enabled, mode=%d.\n"), debug_dma);
						}
					}
				}
			}
			break;
		case 'o':
			{
				if (copper_debugger (&inptr)) {
					debugger_active = 0;
					debugging = 0;
					return true;
				}
				break;
			}
		case 'O':
			break;
		case 'b':
			if (staterecorder (&inptr))
				return true;
			break;
		case 'u':
			{
				if (more_params(&inptr)) {
					if (*inptr == 'a') {
						debugmem_inhibit_break(1);
						console_out(_T("All break to debugger methods inhibited.\n"));
					} else if (*inptr == 'c') {
						debugmem_inhibit_break(-1);
						console_out(_T("All break to debugger methods allowed.\n"));
					}
				} else {
					if (debugmem_inhibit_break(0)) {
						console_out(_T("Current break to debugger method inhibited.\n"));
					} else {
						console_out(_T("Current break to debugger method allowed.\n"));
					}
				}
			}
			break;
		case 'U':
			if (currprefs.mmu_model && more_params (&inptr)) {
				int i;
				uaecptr addrl = readhex (&inptr);
				uaecptr addrp;
				console_out_f (_T("%08X translates to:\n"), addrl);
				for (i = 0; i < 4; i++) {
					bool super = (i & 2) != 0;
					bool data = (i & 1) != 0;
					console_out_f (_T("S%dD%d="), super, data);
					TRY(prb) {
						if (currprefs.mmu_model >= 68040)
							addrp = mmu_translate (addrl, 0, super, data, false, sz_long);
						else
							addrp = mmu030_translate (addrl, super, data, false);
						console_out_f (_T("%08X"), addrp);
						TRY(prb2) {
							if (currprefs.mmu_model >= 68040)
								addrp = mmu_translate (addrl, 0, super, data, true, sz_long);
							else
								addrp = mmu030_translate (addrl, super, data, true);
							console_out_f (_T(" RW"));
						} CATCH(prb2) {
							console_out_f (_T(" RO"));
						} ENDTRY
					} CATCH(prb) {
						console_out_f (_T("***********"));
					} ENDTRY
					console_out_f (_T(" "));
				}
				console_out_f (_T("\n"));
			}
			break;
		case 'h':
		case '?':
			if (more_params (&inptr))
				converter (&inptr);
			else
				debug_help ();
			break;
	}
	return false;
}

static void debug_1 (void)
{
	TCHAR input[MAX_LINEWIDTH];

	m68k_dumpstate(&nextpc, debug_pc);
	debug_pc = 0xffffffff;
	nxdis = nextpc; nxmem = 0;
	debugger_active = 1;

	for (;;) {
		int v;

		if (!debugger_active)
			return;
		update_debug_info ();
		console_out (_T(">"));
		console_flush ();
		debug_linecounter = 0;
		v = console_get (input, MAX_LINEWIDTH);
		if (v < 0)
			return;
		if (v == 0)
			continue;
		if (debug_line (input))
			return;
	}
}

static void addhistory (void)
{
	uae_u32 pc = currprefs.cpu_model >= 68020 && currprefs.cpu_compatible ? regs.instruction_pc : m68k_getpc();
	history[lasthist].regs = regs;
	history[lasthist].regs.pc = pc;
	history[lasthist].vpos = vpos;
	history[lasthist].hpos = current_hpos();
	history[lasthist].fp = timeframes;

	if (++lasthist == MAX_HIST)
		lasthist = 0;
	if (lasthist == firsthist) {
		if (++firsthist == MAX_HIST) firsthist = 0;
	}
}

static void debug_continue(void)
{
	set_special (SPCFLAG_BRK);
}

void debug (void)
{
	int i;
	int wasactive;

	if (savestate_state)
		return;

	bogusframe = 1;
	addhistory ();

#if 0
	if (do_skip && skipaddr_start == 0xC0DEDBAD) {
		if (trace_same_insn_count > 0) {
			if (memcmp (trace_insn_copy, regs.pc_p, 10) == 0
				&& memcmp (trace_prev_regs.regs, regs.regs, sizeof regs.regs) == 0)
			{
				trace_same_insn_count++;
				return;
			}
		}
		if (trace_same_insn_count > 1)
			fprintf (logfile, "[ repeated %d times ]\n", trace_same_insn_count);
		m68k_dumpstate (logfile, &nextpc);
		trace_same_insn_count = 1;
		memcpy (trace_insn_copy, regs.pc_p, 10);
		memcpy (&trace_prev_regs, &regs, sizeof regs);
	}
#endif

	if (!memwatch_triggered) {
		if (trace_mode) {
			uae_u32 pc;
			uae_u16 opcode;
			int bp = 0;

			pc = munge24 (m68k_getpc ());
			opcode = currprefs.cpu_model < 68020 && (currprefs.cpu_compatible || currprefs.cpu_cycle_exact) ? regs.ir : get_word_debug (pc);

			for (i = 0; i < BREAKPOINT_TOTAL; i++) {
				struct breakpoint_node *bpn = &bpnodes[i];
				if (!bpn->enabled)
					continue;
				if (bpn->type == BREAKPOINT_REG_PC) {
					if (bpn->value1 == pc) {
						bp = 1;
						break;
					}
				} else if (bpn->type >= 0 && bpn->type < BREAKPOINT_REG_END) {
					uae_u32 value1 = bpn->value1 & bpn->mask;
					uae_u32 value2 = bpn->value2 & bpn->mask;
					uae_u32 cval = returnregx(bpn->type) & bpn->mask;
					switch (bpn->oper)
					{
						case BREAKPOINT_CMP_EQUAL:
						if (cval == value1)
							bp = i + 1;
						break;
						case BREAKPOINT_CMP_NEQUAL:
						if (cval != value1)
							bp = i + 1;
						break;
						case BREAKPOINT_CMP_SMALLER:
						if (cval <= value1)
							bp = i + 1;
						break;
						case BREAKPOINT_CMP_LARGER:
						if (cval >= value1)
							bp = i + 1;
						break;
						case BREAKPOINT_CMP_RANGE:
						if (cval >= value1 && cval <= value2)
							bp = i + 1;
						break;
						case BREAKPOINT_CMP_NRANGE:
						if (cval <= value1 || cval >= value2)
							bp = i + 1;
						break;
					}
				}
			}

			if (trace_mode) {
				if (trace_mode == TRACE_MATCH_PC && trace_param1 == pc)
					bp = -1;
				if (trace_mode == TRACE_RAM_PC) {
					addrbank *ab = &get_mem_bank(pc);
					if (ab->flags & ABFLAG_RAM) {
						uae_u16 ins = get_word_debug(pc);
						// skip JMP xxxxxx (LVOs)
						if (ins != 0x4ef9) {
							bp = -1;
						}
					}
				}
				if ((processptr || processname) && notinrom()) {
					uaecptr execbase = get_long_debug (4);
					uaecptr activetask = get_long_debug (execbase + 276);
					int process = get_byte_debug (activetask + 8) == 13 ? 1 : 0;
					char *name = (char*)get_real_address_debug(get_long_debug (activetask + 10));
					if (process) {
						uaecptr cli = BPTR2APTR(get_long_debug (activetask + 172));
						uaecptr seglist = 0;

						uae_char *command = NULL;
						if (cli) {
							if (processname)
								command = (char*)get_real_address_debug(BPTR2APTR(get_long_debug (cli + 16)));
							seglist = BPTR2APTR(get_long_debug (cli + 60));
						} else {
							seglist = BPTR2APTR(get_long_debug (activetask + 128));
							seglist = BPTR2APTR(get_long_debug (seglist + 12));
						}
						if (activetask == processptr || (processname && (!stricmp (name, processname) || (command && command[0] && !strnicmp (command + 1, processname, command[0]) && processname[command[0]] == 0)))) {
							while (seglist) {
								uae_u32 size = get_long_debug (seglist - 4) - 4;
								if (pc >= (seglist + 4) && pc < (seglist + size)) {
									bp = 1;
									break;
								}
								seglist = BPTR2APTR(get_long_debug (seglist));
							}
						}
					}
				} else if (trace_mode == TRACE_MATCH_INS) {
					if (trace_param1 == 0x10000) {
						if (opcode == 0x4e75 || opcode == 0x4e73 || opcode == 0x4e77)
							bp = -1;
					} else if (opcode == trace_param1) {
						bp = -1;
					}
				} else if (trace_mode == TRACE_SKIP_INS) {
					if (trace_param1 != 0)
						trace_param1--;
					if (trace_param1 == 0) {
						bp = -1;
					}
#if 0
				} else if (skipaddr_start == 0xffffffff && skipaddr_doskip > 0) {
					bp = -1;
#endif
				} else if (trace_mode == TRACE_RANGE_PC) {
					if (pc >= trace_param1 && pc < trace_param2)
						bp = -1;
				} else if (trace_mode == TRACE_SKIP_LINE) {
					if (trace_param1 != 0)
						trace_param1--;
					if (trace_param1 == 0) {
						int line = debugmem_get_sourceline(pc, NULL, 0);
						if (line > 0 && line != trace_param2)
							bp = -1;
					}
				}
			}
			if (!bp) {
				debug_continue();
				return;
			}
			if (bp > 0)
				console_out_f(_T("Breakpoint %d triggered.\n"), bp - 1);
			debug_cycles();
		}
	} else {
		memwatch_hit_msg(memwatch_triggered - 1);
		memwatch_triggered = 0;
	}

	wasactive = ismouseactive ();
#ifdef WITH_PPC
	uae_ppc_pause(1);
#endif
	inputdevice_unacquire ();
	pause_sound ();
	setmouseactive(0, 0);
	activate_console ();
	trace_mode = 0;
	exception_debugging = 0;
	debug_rewind = 0;
	processptr = 0;
#if 0
	if (!currprefs.statecapture) {
		changed_prefs.statecapture = currprefs.statecapture = 1;
		savestate_init ();
	}
#endif
	debugmem_disable();

	if (trace_cycles && last_frame >= 0) {
		if (last_frame + 2 >= timeframes) {
			console_out_f(_T("Cycles: %d Chip, %d CPU. (V=%d H=%d -> V=%d H=%d)\n"),
				(last_cycles2 - last_cycles1) / CYCLE_UNIT,
				(last_cycles2 - last_cycles1) / cpucycleunit,
				last_vpos1, last_hpos1,
				last_vpos2, last_hpos2);
		}
	}
	trace_cycles = 0;

	debug_1 ();
	debugmem_enable();
	if (!debug_rewind && !currprefs.cachesize
#ifdef FILESYS
		&& nr_units () == 0
#endif
		) {
			savestate_capture (1);
	}
	if (!trace_mode) {
		for (i = 0; i < BREAKPOINT_TOTAL; i++) {
			if (bpnodes[i].enabled)
				trace_mode = TRACE_CHECKONLY;
		}
	}
	if (trace_mode) {
		set_special (SPCFLAG_BRK);
		debugging = -1;
	}
	resume_sound ();
	inputdevice_acquire (TRUE);
#ifdef WITH_PPC
	uae_ppc_pause(0);
#endif
	setmouseactive(0, wasactive ? 2 : 0);

	last_cycles1 = get_cycles();
	last_vpos1 = vpos;
	last_hpos1 = current_hpos();
	last_frame = timeframes;
}

const TCHAR *debuginfo (int mode)
{
	static TCHAR txt[100];
	uae_u32 pc = M68K_GETPC;
	_stprintf (txt, _T("PC=%08X INS=%04X %04X %04X"),
		pc, get_word_debug (pc), get_word_debug (pc + 2), get_word_debug (pc + 4));
	return txt;
}

void mmu_disasm (uaecptr pc, int lines)
{
	debug_mmu_mode = regs.s ? 6 : 2;
	m68k_dumpstate(NULL, 0xffffffff);
	m68k_disasm (pc, NULL, 0xffffffff, lines);
}

static int mmu_logging;

#define MMU_PAGE_SHIFT 16

struct mmudata {
	uae_u32 flags;
	uae_u32 addr;
	uae_u32 len;
	uae_u32 remap;
	uae_u32 p_addr;
};

static struct mmudata *mmubanks;
static uae_u32 mmu_struct, mmu_callback, mmu_regs;
static uae_u32 mmu_fault_bank_addr, mmu_fault_addr;
static int mmu_fault_size, mmu_fault_rw;
static int mmu_slots;
static struct regstruct mmur;

struct mmunode {
	struct mmudata *mmubank;
	struct mmunode *next;
};
static struct mmunode **mmunl;
extern regstruct mmu_backup_regs;

#define MMU_READ_U (1 << 0)
#define MMU_WRITE_U (1 << 1)
#define MMU_READ_S (1 << 2)
#define MMU_WRITE_S (1 << 3)
#define MMU_READI_U (1 << 4)
#define MMU_READI_S (1 << 5)

#define MMU_MAP_READ_U (1 << 8)
#define MMU_MAP_WRITE_U (1 << 9)
#define MMU_MAP_READ_S (1 << 10)
#define MMU_MAP_WRITE_S (1 << 11)
#define MMU_MAP_READI_U (1 << 12)
#define MMU_MAP_READI_S (1 << 13)

void mmu_do_hit (void)
{
	int i;
	uaecptr p;
	uae_u32 pc;

	mmu_triggered = 0;
	pc = m68k_getpc ();
	p = mmu_regs + 18 * 4;
	put_long (p, pc);
	regs = mmu_backup_regs;
	regs.intmask = 7;
	regs.t0 = regs.t1 = 0;
	if (!regs.s) {
		regs.usp = m68k_areg (regs, 7);
		if (currprefs.cpu_model >= 68020)
			m68k_areg (regs, 7) = regs.m ? regs.msp : regs.isp;
		else
			m68k_areg (regs, 7) = regs.isp;
		regs.s = 1;
	}
	MakeSR ();
	m68k_setpc (mmu_callback);
	fill_prefetch ();

	if (currprefs.cpu_model > 68000) {
		for (i = 0 ; i < 9; i++) {
			m68k_areg (regs, 7) -= 4;
			put_long (m68k_areg (regs, 7), 0);
		}
		m68k_areg (regs, 7) -= 4;
		put_long (m68k_areg (regs, 7), mmu_fault_addr);
		m68k_areg (regs, 7) -= 2;
		put_word (m68k_areg (regs, 7), 0); /* WB1S */
		m68k_areg (regs, 7) -= 2;
		put_word (m68k_areg (regs, 7), 0); /* WB2S */
		m68k_areg (regs, 7) -= 2;
		put_word (m68k_areg (regs, 7), 0); /* WB3S */
		m68k_areg (regs, 7) -= 2;
		put_word (m68k_areg (regs, 7),
			(mmu_fault_rw ? 0 : 0x100) | (mmu_fault_size << 5)); /* SSW */
		m68k_areg (regs, 7) -= 4;
		put_long (m68k_areg (regs, 7), mmu_fault_bank_addr);
		m68k_areg (regs, 7) -= 2;
		put_word (m68k_areg (regs, 7), 0x7002);
	}
	m68k_areg (regs, 7) -= 4;
	put_long (m68k_areg (regs, 7), get_long_debug (p - 4));
	m68k_areg (regs, 7) -= 2;
	put_word (m68k_areg (regs, 7), mmur.sr);
#ifdef JIT
	set_special(SPCFLAG_END_COMPILE);
#endif
}

static void mmu_do_hit_pre (struct mmudata *md, uaecptr addr, int size, int rwi, uae_u32 v)
{
	uae_u32 p, pc;
	int i;

	mmur = regs;
	pc = m68k_getpc ();
	if (mmu_logging)
		console_out_f (_T("MMU: hit %08X SZ=%d RW=%d V=%08X PC=%08X\n"), addr, size, rwi, v, pc);

	p = mmu_regs;
	put_long (p, 0); p += 4;
	for (i = 0; i < 16; i++) {
		put_long (p, regs.regs[i]);
		p += 4;
	}
	put_long (p, pc); p += 4;
	put_long (p, 0); p += 4;
	put_long (p, regs.usp); p += 4;
	put_long (p, regs.isp); p += 4;
	put_long (p, regs.msp); p += 4;
	put_word (p, regs.sr); p += 2;
	put_word (p, (size << 1) | ((rwi & 2) ? 1 : 0)); /* size and rw */ p += 2;
	put_long (p, addr); /* fault address */ p += 4;
	put_long (p, md->p_addr); /* bank address */ p += 4;
	put_long (p, v); p += 4;
	mmu_fault_addr = addr;
	mmu_fault_bank_addr = md->p_addr;
	mmu_fault_size = size;
	mmu_fault_rw = rwi;
	mmu_triggered = 1;
}

static int mmu_hit (uaecptr addr, int size, int rwi, uae_u32 *v)
{
	int s, trig;
	uae_u32 flags;
	struct mmudata *md;
	struct mmunode *mn;

	if (mmu_triggered)
		return 1;

	mn = mmunl[addr >> MMU_PAGE_SHIFT];
	if (mn == NULL)
		return 0;

	s = regs.s;
	while (mn) {
		md = mn->mmubank;
		if (addr >= md->addr && addr < md->addr + md->len) {
			flags = md->flags;
			if (flags & (MMU_MAP_READ_U | MMU_MAP_WRITE_U | MMU_MAP_READ_S | MMU_MAP_WRITE_S | MMU_MAP_READI_U | MMU_MAP_READI_S)) {
				trig = 0;
				if (!s && (flags & MMU_MAP_READ_U) && (rwi & 1))
					trig = 1;
				if (!s && (flags & MMU_MAP_WRITE_U) && (rwi & 2))
					trig = 1;
				if (s && (flags & MMU_MAP_READ_S) && (rwi & 1))
					trig = 1;
				if (s && (flags & MMU_MAP_WRITE_S) && (rwi & 2))
					trig = 1;
				if (!s && (flags & MMU_MAP_READI_U) && (rwi & 4))
					trig = 1;
				if (s && (flags & MMU_MAP_READI_S) && (rwi & 4))
					trig = 1;
				if (trig) {
					uaecptr maddr = md->remap + (addr - md->addr);
					if (maddr == addr) /* infinite mmu hit loop? no thanks.. */
						return 1;
					if (mmu_logging)
						console_out_f (_T("MMU: remap %08X -> %08X SZ=%d RW=%d\n"), addr, maddr, size, rwi);
					if ((rwi & 2)) {
						switch (size)
						{
						case 4:
							put_long (maddr, *v);
							break;
						case 2:
							put_word (maddr, *v);
							break;
						case 1:
							put_byte (maddr, *v);
							break;
						}
					} else {
						switch (size)
						{
						case 4:
							*v = get_long_debug (maddr);
							break;
						case 2:
							*v = get_word_debug (maddr);
							break;
						case 1:
							*v = get_byte_debug (maddr);
							break;
						}
					}
					return 1;
				}
			}
			if (flags & (MMU_READ_U | MMU_WRITE_U | MMU_READ_S | MMU_WRITE_S | MMU_READI_U | MMU_READI_S)) {
				trig = 0;
				if (!s && (flags & MMU_READ_U) && (rwi & 1))
					trig = 1;
				if (!s && (flags & MMU_WRITE_U) && (rwi & 2))
					trig = 1;
				if (s && (flags & MMU_READ_S) && (rwi & 1))
					trig = 1;
				if (s && (flags & MMU_WRITE_S) && (rwi & 2))
					trig = 1;
				if (!s && (flags & MMU_READI_U) && (rwi & 4))
					trig = 1;
				if (s && (flags & MMU_READI_S) && (rwi & 4))
					trig = 1;
				if (trig) {
					mmu_do_hit_pre (md, addr, size, rwi, *v);
					return 1;
				}
			}
		}
		mn = mn->next;
	}
	return 0;
}

#ifdef JIT

static void mmu_free_node(struct mmunode *mn)
{
	if (!mn)
		return;
	mmu_free_node (mn->next);
	xfree (mn);
}

static void mmu_free(void)
{
	struct mmunode *mn;
	int i;

	for (i = 0; i < mmu_slots; i++) {
		mn = mmunl[i];
		mmu_free_node (mn);
	}
	xfree (mmunl);
	mmunl = NULL;
	xfree (mmubanks);
	mmubanks = NULL;
}

#endif

static int getmmubank(struct mmudata *snptr, uaecptr p)
{
	snptr->flags = get_long_debug (p);
	if (snptr->flags == 0xffffffff)
		return 1;
	snptr->addr = get_long_debug (p + 4);
	snptr->len = get_long_debug (p + 8);
	snptr->remap = get_long_debug (p + 12);
	snptr->p_addr = p;
	return 0;
}

int mmu_init(int mode, uaecptr parm, uaecptr parm2)
{
	uaecptr p, p2, banks;
	int size;
	struct mmudata *snptr;
	struct mmunode *mn;
#ifdef JIT
	static int wasjit;
	if (currprefs.cachesize) {
		wasjit = currprefs.cachesize;
		changed_prefs.cachesize = 0;
		console_out (_T("MMU: JIT disabled\n"));
		check_prefs_changed_comp(false);
	}
	if (mode == 0) {
		if (mmu_enabled) {
			mmu_free ();
			deinitialize_memwatch ();
			console_out (_T("MMU: disabled\n"));
			changed_prefs.cachesize = wasjit;
		}
		mmu_logging = 0;
		return 1;
	}
#endif

	if (mode == 1) {
		if (!mmu_enabled)
			return 0xffffffff;
		return mmu_struct;
	}

	p = parm;
	mmu_struct = p;
	if (get_long_debug (p) != 1) {
		console_out_f (_T("MMU: version mismatch %d <> %d\n"), get_long_debug (p), 1);
		return 0;
	}
	p += 4;
	mmu_logging = get_long_debug (p) & 1;
	p += 4;
	mmu_callback = get_long_debug (p);
	p += 4;
	mmu_regs = get_long_debug (p);
	p += 4;

	if (mode == 3) {
		int off;
		uaecptr addr = get_long_debug (parm2 + 4);
		if (!mmu_enabled)
			return 0;
		off = addr >> MMU_PAGE_SHIFT;
		mn = mmunl[off];
		while (mn) {
			if (mn->mmubank->p_addr == parm2) {
				getmmubank(mn->mmubank, parm2);
				if (mmu_logging)
					console_out_f (_T("MMU: bank update %08X: %08X - %08X %08X\n"),
					mn->mmubank->flags, mn->mmubank->addr, mn->mmubank->len + mn->mmubank->addr,
					mn->mmubank->remap);
			}
			mn = mn->next;
		}
		return 1;
	}

	mmu_slots = 1 << ((currprefs.address_space_24 ? 24 : 32) - MMU_PAGE_SHIFT);
	mmunl = xcalloc (struct mmunode*, mmu_slots);
	size = 1;
	p2 = get_long_debug (p);
	while (get_long_debug (p2) != 0xffffffff) {
		p2 += 16;
		size++;
	}
	p = banks = get_long_debug (p);
	snptr = mmubanks = xmalloc (struct mmudata, size);
	for (;;) {
		int off;
		if (getmmubank(snptr, p))
			break;
		p += 16;
		off = snptr->addr >> MMU_PAGE_SHIFT;
		if (mmunl[off] == NULL) {
			mn = mmunl[off] = xcalloc (struct mmunode, 1);
		} else {
			mn = mmunl[off];
			while (mn->next)
				mn = mn->next;
			mn = mn->next = xcalloc (struct mmunode, 1);
		}
		mn->mmubank = snptr;
		snptr++;
	}

	initialize_memwatch (1);
	console_out_f (_T("MMU: enabled, %d banks, CB=%08X S=%08X BNK=%08X SF=%08X, %d*%d\n"),
		size - 1, mmu_callback, parm, banks, mmu_regs, mmu_slots, 1 << MMU_PAGE_SHIFT);
	set_special (SPCFLAG_BRK);
	return 1;
}

void debug_parser (const TCHAR *cmd, TCHAR *out, uae_u32 outsize)
{
	TCHAR empty[2] = { 0 };
	TCHAR *input = my_strdup (cmd);
	if (out == NULL && outsize == 0) {
		setconsolemode (empty, 1);
	} else if (out != NULL && outsize > 0) {
		out[0] = 0;
		setconsolemode (out, outsize);
	}
	debug_line (input);
	setconsolemode (NULL, 0);
	xfree (input);
}

/*

trainer file is .ini file with following one or more [patch] sections.
Each [patch] section describes single trainer option.

After [patch] section must come at least one patch descriptor.

[patch]
name=name
enable=true/false
event=KEY_F1

; patch descriptor
data=200e46802d400026200cxx02 ; this is comment
offset=2
access=write
setvalue=<value>
type=nop/freeze/set/setonce

; patch descriptor
data=11223344556677889900
offset=10
replacedata=4e71
replaceoffset=4

; next patch section
[patch]


name: name of the option (appears in GUI in the future)
enable: true = automatically enabled at startup. (false=manually activated using key shortcut etc.., will be implemented later)
event: inputevents.def event name

data: match data, when emulated CPU executes first opcode of this data and following words also match: match is detected. x = anything.
offset: word offset from beginning of "data" that points to memory read/write instruction that you want to "patch". Default=0.
access: read=read access, write=write access. Default: write if instruction does both memory read and write, read if read-only.

setvalue: value to write if type is set or setonce.
type=nop: found instruction's write does nothing. This instruction only. Other instruction(s) modifying same memory location are not skipped.
type=freeze: found instruction's memory read always returns value in memory. Write does nothing.
type=set: found instruction's memory read always returns "setvalue" contents. Write works normally.
type=setonce: "setvalue" contents are written to memory when patch is detected.

replacedata: data to be copied over data + replaceoffset. x masking is also supported. Memory is modified.
replaceoffset: word offset from data.

---

Internally it uses debugger memory watch points to modify/freeze memory contents. No memory or code is modified.
Only type=setonce and replacedata modifies memory.

When CPU emulator current to be executed instruction's matches contents of data[offset], other words of data are also checked.
If all words match: instruction's effective address(es) are calculated and matching (read/write) EA is stored. Matching part
of patch is now done.

Reason for this complexity is to enable single patch to work even if game is relocatable or it uses different memory
locations depending on hardware config.

If type=nop/freeze/set: debugger memwatch point is set that handles faking of read/write access.
If type=setonce: "setvalue" contents gets written to detected effective address.
If replacedata is set: copy code.

Detection phase may cause increased CPU load, this may get optimized more but it shouldn't be (too) noticeable in basic
A500 or A1200 modes.

*/

#define TRAINER_NOP 0
#define TRAINER_FREEZE 1
#define TRAINER_SET 2
#define TRAINER_SETONCE 3

struct trainerpatch
{
	TCHAR *name;
	uae_u16 *data;
	uae_u16 *maskdata;
	uae_u16 *replacedata;
	uae_u16 *replacemaskdata;
	uae_u16 *replacedata_original;
	uae_u16 first;
	int length;
	int offset;
	int access;
	int replacelength;
	int replaceoffset;
	uaecptr addr;
	uaecptr varaddr;
	int varsize;
	uae_u32 oldval;
	int patchtype;
	int setvalue;
	int *events;
	int eventcount;
	int memwatchindex;
	bool enabledatstart;
	bool enabled;
};

static struct trainerpatch **tpptr;
static int tpptrcnt;
bool debug_opcode_watch;

static int debug_trainer_get_ea(struct trainerpatch *tp, uaecptr pc, uae_u16 opcode, uaecptr *addr)
{
	struct instr *dp = table68k + opcode;
	uae_u32 sea = 0, dea = 0;
	uaecptr spc = 0, dpc = 0;
	uaecptr pc2 = pc + 2;
	if (dp->suse) {
		spc = pc2;
		pc2 = ShowEA(NULL, pc2, opcode, dp->sreg, dp->smode, dp->size, NULL, &sea, 1);
		if (sea == spc)
			spc = 0xffffffff;
	}
	if (dp->duse) {
		dpc = pc2;
		pc2 = ShowEA(NULL, pc2, opcode, dp->dreg, dp->dmode, dp->size, NULL, &dea, 1);
		if (dea == dpc)
			dpc = 0xffffffff;
	}
	if (dea && dpc != 0xffffffff && tp->access == 1) {
		*addr = dea;
		return 1 << dp->size;
	}
	if (sea && spc != 0xffffffff && tp->access == 0) {
		*addr = sea;
		return 1 << dp->size;
	}
	if (dea && tp->access > 1) {
		*addr = dea;
		return 1 << dp->size;
	}
	if (sea && tp->access > 1) {
		*addr = sea;
		return 1 << dp->size;
	}
	return 0;
}

static void debug_trainer_enable(struct trainerpatch *tp, bool enable)
{
	if (tp->enabled == enable)
		return;

	if (tp->replacedata) {
		if (enable) {
			bool first = false;
			if (!tp->replacedata_original) {
				tp->replacedata_original = xcalloc(uae_u16, tp->replacelength);
				first = true;
			}
			for (int j = 0; j < tp->replacelength; j++) {
				uae_u16 v = tp->replacedata[j];
				uae_u16 m = tp->replacemaskdata[j];
				uaecptr addr = (tp->addr - tp->offset * 2) + j * 2 + tp->replaceoffset * 2;
				if (m == 0xffff) {
					x_put_word(addr, v);
				} else {
					uae_u16 vo = x_get_word(addr);
					x_put_word(addr, (vo & ~m) | (v & m));
					if (first)
						tp->replacedata_original[j] = vo;
				}
			}
		} else if (tp->replacedata_original) {
			for (int j = 0; j < tp->replacelength; j++) {
				uae_u16 m = tp->replacemaskdata[j];
				uaecptr addr = (tp->addr - tp->offset * 2) + j * 2 + tp->replaceoffset * 2;
				if (m != 0xffff) {
					x_put_word(addr, tp->replacedata_original[j]);
				}
			}
		}
	}

	if (tp->patchtype == TRAINER_SETONCE && tp->varaddr != 0xffffffff) {
		uae_u32 v = enable ? tp->setvalue : tp->oldval;
		switch (tp->varsize)
		{
		case 1:
			x_put_byte(tp->varaddr, tp->setvalue);
			break;
		case 2:
			x_put_word(tp->varaddr, tp->setvalue);
			break;
		case 4:
			x_put_long(tp->varaddr, tp->setvalue);
			break;
		}
	}

	if ((tp->patchtype == TRAINER_NOP || tp->patchtype == TRAINER_FREEZE || tp->patchtype == TRAINER_SET) && tp->varaddr != 0xffffffff) {
		struct memwatch_node *mwn;
		if (!memwatch_enabled)
			initialize_memwatch(0);
		if (enable) {
			int i;
			for (i = MEMWATCH_TOTAL - 1; i >= 0; i--) {
				mwn = &mwnodes[i];
				if (!mwn->size)
					break;
			}
			if (i < 0) {
				write_log(_T("Trainer out of free memwatchpoints ('%s' %08x\n).\n"), tp->name, tp->addr);
			} else {
				mwn->addr = tp->varaddr;
				mwn->size = tp->varsize;
				mwn->rwi = 1 | 2;
				mwn->access_mask = MW_MASK_CPU_D_R | MW_MASK_CPU_D_W;
				mwn->reg = 0xffffffff;
				mwn->pc = tp->patchtype == TRAINER_NOP ? tp->addr : 0xffffffff;
				mwn->frozen = tp->patchtype == TRAINER_FREEZE || tp->patchtype == TRAINER_NOP;
				mwn->modval_written = 0;
				mwn->val_enabled = 0;
				mwn->val_mask = 0xffffffff;
				mwn->val = 0;
				if (tp->patchtype == TRAINER_SET) {
					mwn->val_enabled = 1;
					mwn->val = tp->setvalue;
				}
				mwn->nobreak = true;
				memwatch_setup();
				TCHAR buf[256];
				memwatch_dump2(buf, sizeof(buf) / sizeof(TCHAR), i);
				write_log(_T("%s"), buf);
			}
		} else {
			mwn = &mwnodes[tp->memwatchindex];
			mwn->size = 0;
			memwatch_setup();
		}
	}

	write_log(_T("Trainer '%s' %s (addr=%08x)\n"), tp->name, enable ? _T("enabled") : _T("disabled"), tp->addr);
	tp->enabled = enable;
}

void debug_trainer_match(void)
{
	uaecptr pc = m68k_getpc();
	uae_u16 opcode = x_get_word(pc);
	for (int i = 0; i < tpptrcnt; i++) {
		struct trainerpatch *tp = tpptr[i];
		if (tp->first != opcode)
			continue;
		if (tp->addr)
			continue;
		int j;
		for (j = 0; j < tp->length; j++) {
			uae_u16 d = x_get_word(pc + (j - tp->offset) * 2);
			if ((d & tp->maskdata[j]) != tp->data[j])
				break;
		}
		if (j < tp->length)
			continue;
		tp->first = 0xffff;
		tp->addr = pc;
		tp->varsize = -1;
		tp->varaddr = 0xffffffff;
		tp->oldval = 0xffffffff;
		if (tp->access >= 0) {
			tp->varsize = debug_trainer_get_ea(tp, pc, opcode, &tp->varaddr);
			switch (tp->varsize)
			{
			case 1:
				tp->oldval = x_get_byte(tp->varaddr);
				break;
			case 2:
				tp->oldval = x_get_word(tp->varaddr);
				break;
			case 4:
				tp->oldval = x_get_long(tp->varaddr);
				break;
			}
		}
		write_log(_T("Patch %d match at %08x. Addr %08x, size %d, val %08x\n"), i, pc, tp->varaddr, tp->varsize, tp->oldval);

		if (tp->enabledatstart)
			debug_trainer_enable(tp, true);

		// all detected?
		for (j = 0; j < tpptrcnt; j++) {
			struct trainerpatch *tp = tpptr[j];
			if (!tp->addr)
				break;
		}
		if (j == tpptrcnt)
			debug_opcode_watch = false;
	}
}

static int parsetrainerdata(const TCHAR *data, uae_u16 *outdata, uae_u16 *outmask)
{
	int len = _tcslen(data);
	uae_u16 v = 0, vm = 0;
	int j = 0;
	for (int i = 0; i < len; ) {
		TCHAR c1 = _totupper(data[i + 0]);
		TCHAR c2 = _totupper(data[i + 1]);
		if (c1 > 0 && c1 <= ' ') {
			i++;
			continue;
		}
		if (i + 1 >= len)
			return 0;

		vm <<= 8;
		vm |= 0xff;
		if (c1 == 'X' || c1 == '?')
			vm &= 0x0f;
		if (c2 == 'X' || c2 == '?')
			vm &= 0xf0;

		if (c1 >= 'A')
			c1 -= 'A' - 10;
		else if (c1 >= '0')
			c1 -= '0';
		if (c2 >= 'A')
			c2 -= 'A' - 10;
		else if (c2 >= '0')
			c2 -= '0';

		v <<= 8;
		if (c1 >= 0 && c1 < 16)
			v |= c1 << 4;
		if (c2 >= 0 && c2 < 16)
			v |= c2;

		if (i & 2) {
			outdata[j] = v;
			outmask[j] = vm;
			j++;
		}

		i += 2;
	}
	return j;
}

void debug_init_trainer(const TCHAR *file)
{
	TCHAR section[256];
	int cnt = 1;

	struct ini_data *ini = ini_load(file, false);
	if (!ini)
		return;

	write_log(_T("Loaded '%s'\n"), file);

	_tcscpy(section, _T("patch"));

	for (;;) {
		struct ini_context ictx;
		ini_initcontext(ini, &ictx);

		for (;;) {
			TCHAR *name = NULL;
			TCHAR *data;

			ini_getstring_multi(ini, section, _T("name"), &name, &ictx);

			if (!ini_getstring_multi(ini, section, _T("data"), &data, &ictx))
				break;
			ini_setcurrentasstart(ini, &ictx);
			ini_setlast(ini, section, _T("data"), &ictx);

			TCHAR *p = _tcschr(data, ';');
			if (p)
				*p = 0;
			my_trim(data);

			struct trainerpatch *tp = xcalloc(struct trainerpatch, 1);

			int datalen = (_tcslen(data) + 3) / 4;
			tp->data = xcalloc(uae_u16, datalen);
			tp->maskdata = xcalloc(uae_u16, datalen);
			tp->length = parsetrainerdata(data, tp->data, tp->maskdata);
			xfree(data);

			ini_getval_multi(ini, section, _T("offset"), &tp->offset, &ictx);
			if (tp->offset < 0 || tp->offset >= tp->length)
				tp->offset = 0;

			if (ini_getstring_multi(ini, section, _T("replacedata"), &data, &ictx)) {
				int replacedatalen = (_tcslen(data) + 3) / 4;
				tp->replacedata = xcalloc(uae_u16, replacedatalen);
				tp->replacemaskdata = xcalloc(uae_u16, replacedatalen);
				tp->replacelength = parsetrainerdata(data, tp->replacedata, tp->replacemaskdata);
				ini_getval_multi(ini, section, _T("replaceoffset"), &tp->offset, &ictx);
				if (tp->replaceoffset < 0 || tp->replaceoffset >= tp->length)
					tp->replaceoffset = 0;
				tp->access = -1;
				xfree(data);
			}

			tp->access = 2;
			if (ini_getstring_multi(ini, section, _T("access"), &data, &ictx)) {
				if (!_tcsicmp(data, _T("read")))
					tp->access = 0;
				else if (!_tcsicmp(data, _T("write")))
					tp->access = 1;
				xfree(data);
			}

			if (ini_getstring_multi(ini, section, _T("type"), &data, &ictx)) {
				if (!_tcsicmp(data, _T("freeze")))
					tp->patchtype = TRAINER_FREEZE;
				else if (!_tcsicmp(data, _T("nop")))
					tp->patchtype = TRAINER_NOP;
				else if (!_tcsicmp(data, _T("set")))
					tp->patchtype = TRAINER_SET;
				else if (!_tcsicmp(data, _T("setonce")))
					tp->patchtype = TRAINER_SETONCE;
				xfree(data);
			}

			if (ini_getstring_multi(ini, section, _T("setvalue"), &data, &ictx)) {
				TCHAR *endptr;
				if (data[0] == '$') {
					tp->setvalue = _tcstol(data + 1, &endptr, 16);
				} else if (_tcslen(data) > 2 && data[0] == '0' && _totupper(data[1]) == 'x') {
					tp->setvalue = _tcstol(data + 2, &endptr, 16);
				} else {
					tp->setvalue = _tcstol(data, &endptr, 10);
				}
				xfree(data);
			}

			if (ini_getstring(ini, section, _T("enable"), &data)) {
				if (!_tcsicmp(data, _T("true")))
					tp->enabledatstart = true;
				xfree(data);
			}

			if (ini_getstring(ini, section, _T("event"), &data)) {
				TCHAR *s = data;
				_tcscat(s, _T(","));
				while (*s) {
					bool end = false;
					while (*s == ' ')
						s++;
					TCHAR *se = _tcschr(s, ',');
					if (se) {
						*se = 0;
					} else {
						end = true;
					}
					TCHAR *se2 = se - 1;
					while (se2 > s) {
						if (*se2 != ' ')
							break;
						*se2 = 0;
						se2--;
					}
					int evt = inputdevice_geteventid(s);
					if (evt > 0) {
						if (tp->events) {
							tp->events = xrealloc(int, tp->events, tp->eventcount + 1);
						} else {
							tp->events = xmalloc(int, 1);
						}
						tp->events[tp->eventcount++] = evt;
					} else {
						write_log(_T("Unknown event '%s'\n"), s);
					}
					if (end)
						break;
					s = se + 1;
				}
				xfree(data);
			}

			tp->first = tp->data[tp->offset];
			tp->name = name;

			if (tpptrcnt)
				tpptr = xrealloc(struct trainerpatch*, tpptr, tpptrcnt + 1);
			else
				tpptr = xcalloc(struct trainerpatch*, tpptrcnt + 1);
			tpptr[tpptrcnt++] = tp;

			write_log(_T("%d: '%s' parsed and enabled\n"), cnt, tp->name ? tp->name : _T("<no name>"));
			cnt++;

			ini_setlastasstart(ini, &ictx);
		}

		if (!ini_nextsection(ini, section))
			break;

	}

end:
	if (tpptrcnt > 0)
		debug_opcode_watch = true;

	ini_free(ini);
}

bool debug_trainer_event(int evt, int state)
{
	for (int i = 0; i < tpptrcnt; i++) {
		struct trainerpatch *tp = tpptr[i];
		for (int j = 0; j < tp->eventcount; j++) {
			if (tp->events[j] <= 0)
				continue;
			if (tp->events[j] == evt) {
				if (!state)
					return true;
				write_log(_T("Trainer %d ('%s') -> %s\n"), i, tp->name, tp->enabled ? _T("off") : _T("on"));
				debug_trainer_enable(tp, !tp->enabled);
				return true;
			}
		}
	}
	return false;
}
