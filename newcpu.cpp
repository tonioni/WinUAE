/*
* UAE - The Un*x Amiga Emulator
*
* MC68000 emulation
*
* (c) 1995 Bernd Schmidt
*/

#define MMUOP_DEBUG 2
#define DEBUG_CD32CDTVIO 0
#define EXCEPTION3_DEBUGGER 0
#define CPUTRACE_DEBUG 0

#define VALIDATE_68030_DATACACHE 0
#define VALIDATE_68040_DATACACHE 0
#define DISABLE_68040_COPYBACK 0

#define MORE_ACCURATE_68020_PIPELINE 1

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "events.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "cpummu.h"
#include "cpummu030.h"
#include "cpu_prefetch.h"
#include "autoconf.h"
#include "traps.h"
#include "debug.h"
#include "debugmem.h"
#include "gui.h"
#include "savestate.h"
#include "blitter.h"
#include "ar.h"
#include "gayle.h"
#include "cia.h"
#include "inputrecord.h"
#include "inputdevice.h"
#include "audio.h"
#include "fpp.h"
#include "statusline.h"
#include "uae/ppc.h"
#include "cpuboard.h"
#include "threaddep/thread.h"
#include "x86.h"
#include "bsdsocket.h"
#include "devices.h"
#ifdef JIT
#include "jit/compemu.h"
#include <signal.h>
#else
/* Need to have these somewhere */
bool check_prefs_changed_comp (bool checkonly) { return false; }
#endif
/* For faster JIT cycles handling */
uae_s32 pissoff = 0;

/* Opcode of faulting instruction */
static uae_u16 last_op_for_exception_3;
/* PC at fault time */
static uaecptr last_addr_for_exception_3;
/* Address that generated the exception */
static uaecptr last_fault_for_exception_3;
/* read (0) or write (1) access */
static bool last_writeaccess_for_exception_3;
/* instruction (1) or data (0) access */
static bool last_instructionaccess_for_exception_3;
/* not instruction */
static bool last_notinstruction_for_exception_3;
/* set when writing exception stack frame */
static int exception_in_exception;

int mmu_enabled, mmu_triggered;
int cpu_cycles;
int bus_error_offset;
static int baseclock;
int m68k_pc_indirect;
bool m68k_interrupt_delay;
static bool m68k_reset_delay;

static volatile uae_atomic uae_interrupt;
static volatile uae_atomic uae_interrupts2[IRQ_SOURCE_MAX];
static volatile uae_atomic uae_interrupts6[IRQ_SOURCE_MAX];

static int cacheisets04060, cacheisets04060mask, cacheitag04060mask;
static int cachedsets04060, cachedsets04060mask, cachedtag04060mask;

static int cpu_prefs_changed_flag;

int cpucycleunit;
int cpu_tracer;

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

struct mmufixup mmufixup[2];

#define COUNT_INSTRS 0
#define MC68060_PCR   0x04300000
#define MC68EC060_PCR 0x04310000

static uae_u64 fake_srp_030, fake_crp_030;
static uae_u32 fake_tt0_030, fake_tt1_030, fake_tc_030;
static uae_u16 fake_mmusr_030;

static struct cache020 caches020[CACHELINES020];
static struct cache030 icaches030[CACHELINES030];
static struct cache030 dcaches030[CACHELINES030];
static int icachelinecnt, icachehalfline;
static int dcachelinecnt;
static struct cache040 icaches040[CACHESETS060];
static struct cache040 dcaches040[CACHESETS060];
static int cache_lastline; 

static int fallback_cpu_model, fallback_mmu_model, fallback_fpu_model;
static bool fallback_cpu_compatible, fallback_cpu_address_space_24;
static struct regstruct fallback_regs;
static int fallback_new_cpu_model;

int cpu_last_stop_vpos, cpu_stopped_lines;

#if COUNT_INSTRS
static unsigned long int instrcount[65536];
static uae_u16 opcodenums[65536];

static int compfn (const void *el1, const void *el2)
{
	return instrcount[*(const uae_u16 *)el1] < instrcount[*(const uae_u16 *)el2];
}

static TCHAR *icountfilename (void)
{
	TCHAR *name = getenv ("INSNCOUNT");
	if (name)
		return name;
	return COUNT_INSTRS == 2 ? "frequent.68k" : "insncount";
}

void dump_counts (void)
{
	FILE *f = fopen (icountfilename (), "w");
	unsigned long int total;
	int i;

	write_log (_T("Writing instruction count file...\n"));
	for (i = 0; i < 65536; i++) {
		opcodenums[i] = i;
		total += instrcount[i];
	}
	qsort (opcodenums, 65536, sizeof (uae_u16), compfn);

	fprintf (f, "Total: %lu\n", total);
	for (i=0; i < 65536; i++) {
		unsigned long int cnt = instrcount[opcodenums[i]];
		struct instr *dp;
		struct mnemolookup *lookup;
		if (!cnt)
			break;
		dp = table68k + opcodenums[i];
		for (lookup = lookuptab;lookup->mnemo != dp->mnemo; lookup++)
			;
		fprintf (f, "%04x: %lu %s\n", opcodenums[i], cnt, lookup->name);
	}
	fclose (f);
}
#else
void dump_counts (void)
{
}
#endif

/*

 ok, all this to "record" current instruction state
 for later 100% cycle-exact restoring

 */

static uae_u32 (*x2_prefetch)(int);
static uae_u32 (*x2_prefetch_long)(int);
static uae_u32 (*x2_next_iword)(void);
static uae_u32 (*x2_next_ilong)(void);
static uae_u32 (*x2_get_ilong)(int);
static uae_u32 (*x2_get_iword)(int);
static uae_u32 (*x2_get_ibyte)(int);
static uae_u32 (*x2_get_long)(uaecptr);
static uae_u32 (*x2_get_word)(uaecptr);
static uae_u32 (*x2_get_byte)(uaecptr);
static void (*x2_put_long)(uaecptr,uae_u32);
static void (*x2_put_word)(uaecptr,uae_u32);
static void (*x2_put_byte)(uaecptr,uae_u32);
static void (*x2_do_cycles)(unsigned long);
static void (*x2_do_cycles_pre)(unsigned long);
static void (*x2_do_cycles_post)(unsigned long, uae_u32);

uae_u32 (*x_prefetch)(int);
uae_u32 (*x_next_iword)(void);
uae_u32 (*x_next_ilong)(void);
uae_u32 (*x_get_ilong)(int);
uae_u32 (*x_get_iword)(int);
uae_u32 (*x_get_ibyte)(int);
uae_u32 (*x_get_long)(uaecptr);
uae_u32 (*x_get_word)(uaecptr);
uae_u32 (*x_get_byte)(uaecptr);
void (*x_put_long)(uaecptr,uae_u32);
void (*x_put_word)(uaecptr,uae_u32);
void (*x_put_byte)(uaecptr,uae_u32);

uae_u32 (*x_cp_next_iword)(void);
uae_u32 (*x_cp_next_ilong)(void);
uae_u32 (*x_cp_get_long)(uaecptr);
uae_u32 (*x_cp_get_word)(uaecptr);
uae_u32 (*x_cp_get_byte)(uaecptr);
void (*x_cp_put_long)(uaecptr,uae_u32);
void (*x_cp_put_word)(uaecptr,uae_u32);
void (*x_cp_put_byte)(uaecptr,uae_u32);
uae_u32 (REGPARAM3 *x_cp_get_disp_ea_020)(uae_u32 base, int idx) REGPARAM;

void (*x_do_cycles)(unsigned long);
void (*x_do_cycles_pre)(unsigned long);
void (*x_do_cycles_post)(unsigned long, uae_u32);

uae_u32(*x_phys_get_iword)(uaecptr);
uae_u32(*x_phys_get_ilong)(uaecptr);
uae_u32(*x_phys_get_byte)(uaecptr);
uae_u32(*x_phys_get_word)(uaecptr);
uae_u32(*x_phys_get_long)(uaecptr);
void(*x_phys_put_byte)(uaecptr, uae_u32);
void(*x_phys_put_word)(uaecptr, uae_u32);
void(*x_phys_put_long)(uaecptr, uae_u32);

static void set_x_cp_funcs(void)
{
	x_cp_put_long = x_put_long;
	x_cp_put_word = x_put_word;
	x_cp_put_byte = x_put_byte;
	x_cp_get_long = x_get_long;
	x_cp_get_word = x_get_word;
	x_cp_get_byte = x_get_byte;
	x_cp_next_iword = x_next_iword;
	x_cp_next_ilong = x_next_ilong;
	x_cp_get_disp_ea_020 = x_get_disp_ea_020;

	if (currprefs.mmu_model == 68030) {
		if (currprefs.cpu_compatible) {
			x_cp_put_long = put_long_mmu030c_state;
			x_cp_put_word = put_word_mmu030c_state;
			x_cp_put_byte = put_byte_mmu030c_state;
			x_cp_get_long = get_long_mmu030c_state;
			x_cp_get_word = get_word_mmu030c_state;
			x_cp_get_byte = get_byte_mmu030c_state;
			x_cp_next_iword = next_iword_mmu030c_state;
			x_cp_next_ilong = next_ilong_mmu030c_state;
			x_cp_get_disp_ea_020 = get_disp_ea_020_mmu030c;
		} else {
			x_cp_put_long = put_long_mmu030_state;
			x_cp_put_word = put_word_mmu030_state;
			x_cp_put_byte = put_byte_mmu030_state;
			x_cp_get_long = get_long_mmu030_state;
			x_cp_get_word = get_word_mmu030_state;
			x_cp_get_byte = get_byte_mmu030_state;
			x_cp_next_iword = next_iword_mmu030_state;
			x_cp_next_ilong = next_ilong_mmu030_state;
			x_cp_get_disp_ea_020 = get_disp_ea_020_mmu030;
		}
	}
}

static struct cputracestruct cputrace;

#if CPUTRACE_DEBUG
static void validate_trace (void)
{
	for (int i = 0; i < cputrace.memoryoffset; i++) {
		struct cputracememory *ctm = &cputrace.ctm[i];
		if (ctm->data == 0xdeadf00d) {
			write_log (_T("unfinished write operation %d %08x\n"), i, ctm->addr);
		}
	}
}
#endif

static void debug_trace (void)
{
	if (cputrace.writecounter > 10000 || cputrace.readcounter > 10000)
		write_log (_T("cputrace.readcounter=%d cputrace.writecounter=%d\n"), cputrace.readcounter, cputrace.writecounter);
}

STATIC_INLINE void clear_trace (void)
{
#if CPUTRACE_DEBUG
	validate_trace ();
#endif
	if (cputrace.memoryoffset == MAX_CPUTRACESIZE)
		return;
	struct cputracememory *ctm = &cputrace.ctm[cputrace.memoryoffset++];
	if (cputrace.memoryoffset == MAX_CPUTRACESIZE) {
		write_log(_T("CPUTRACE overflow, stopping tracing.\n"));
		return;
	}
	ctm->mode = 0;
	cputrace.cyclecounter = 0;
	cputrace.cyclecounter_pre = cputrace.cyclecounter_post = 0;
}
static void set_trace (uaecptr addr, int accessmode, int size)
{
#if CPUTRACE_DEBUG
	validate_trace ();
#endif
	if (cputrace.memoryoffset == MAX_CPUTRACESIZE)
		return;
	struct cputracememory *ctm = &cputrace.ctm[cputrace.memoryoffset++];
	if (cputrace.memoryoffset == MAX_CPUTRACESIZE) {
		write_log(_T("CPUTRACE overflow, stopping tracing.\n"));
		return;
	}
	ctm->addr = addr;
	ctm->data = 0xdeadf00d;
	ctm->mode = accessmode | (size << 4);
	cputrace.cyclecounter_pre = -1;
	if (accessmode == 1)
		cputrace.writecounter++;
	else
		cputrace.readcounter++;
	debug_trace ();
}
static void add_trace (uaecptr addr, uae_u32 val, int accessmode, int size)
{
	if (cputrace.memoryoffset < 1) {
#if CPUTRACE_DEBUG
		write_log (_T("add_trace memoryoffset=%d!\n"), cputrace.memoryoffset);
#endif
		return;
	}
	int mode = accessmode | (size << 4);
	struct cputracememory *ctm = &cputrace.ctm[cputrace.memoryoffset - 1];
	ctm->addr = addr;
	ctm->data = val;
	if (!ctm->mode) {
		ctm->mode = mode;
		if (accessmode == 1)
			cputrace.writecounter++;
		else
			cputrace.readcounter++;
	}
	debug_trace ();
	cputrace.cyclecounter_pre = cputrace.cyclecounter_post = 0;
}


static void check_trace2 (void)
{
	if (cputrace.readcounter || cputrace.writecounter ||
		cputrace.cyclecounter || cputrace.cyclecounter_pre || cputrace.cyclecounter_post)
		write_log (_T("CPU tracer invalid state during playback!\n"));
}

static bool check_trace (void)
{
	if (!cpu_tracer)
		return true;
	if (!cputrace.readcounter && !cputrace.writecounter && !cputrace.cyclecounter) {
		if (cpu_tracer != -2) {
			write_log (_T("CPU trace: dma_cycle() enabled. %08x %08x NOW=%08lx\n"),
				cputrace.cyclecounter_pre, cputrace.cyclecounter_post, get_cycles ());
			cpu_tracer = -2; // dma_cycle() allowed to work now
		}
	}
	if (cputrace.readcounter || cputrace.writecounter ||
		cputrace.cyclecounter || cputrace.cyclecounter_pre || cputrace.cyclecounter_post)
		return false;
	x_prefetch = x2_prefetch;
	x_get_ilong = x2_get_ilong;
	x_get_iword = x2_get_iword;
	x_get_ibyte = x2_get_ibyte;
	x_next_iword = x2_next_iword;
	x_next_ilong = x2_next_ilong;
	x_put_long = x2_put_long;
	x_put_word = x2_put_word;
	x_put_byte = x2_put_byte;
	x_get_long = x2_get_long;
	x_get_word = x2_get_word;
	x_get_byte = x2_get_byte;
	x_do_cycles = x2_do_cycles;
	x_do_cycles_pre = x2_do_cycles_pre;
	x_do_cycles_post = x2_do_cycles_post;
	set_x_cp_funcs();
	write_log (_T("CPU tracer playback complete. STARTCYCLES=%08x NOWCYCLES=%08lx\n"), cputrace.startcycles, get_cycles ());
	cputrace.needendcycles = 1;
	cpu_tracer = 0;
	return true;
}

static bool get_trace (uaecptr addr, int accessmode, int size, uae_u32 *data)
{
	int mode = accessmode | (size << 4);
	for (int i = 0; i < cputrace.memoryoffset; i++) {
		struct cputracememory *ctm = &cputrace.ctm[i];
		if (ctm->addr == addr && ctm->mode == mode) {
			ctm->mode = 0;
			write_log (_T("CPU trace: GET %d: PC=%08x %08x=%08x %d %d %08x/%08x/%08x %d/%d (%08lx)\n"),
				i, cputrace.pc, addr, ctm->data, accessmode, size,
				cputrace.cyclecounter, cputrace.cyclecounter_pre, cputrace.cyclecounter_post,
				cputrace.readcounter, cputrace.writecounter, get_cycles ());
			if (accessmode == 1)
				cputrace.writecounter--;
			else
				cputrace.readcounter--;
			if (cputrace.writecounter == 0 && cputrace.readcounter == 0) {
				if (cputrace.cyclecounter_post) {
					int c = cputrace.cyclecounter_post;
					cputrace.cyclecounter_post = 0;
					x_do_cycles (c);
				} else if (cputrace.cyclecounter_pre) {
					check_trace ();
					*data = ctm->data;
					return true; // argh, need to rerun the memory access..
				}
			}
			check_trace ();
			*data = ctm->data;
			return false;
		}
	}
	if (cputrace.cyclecounter_post) {
		int c = cputrace.cyclecounter_post;
		cputrace.cyclecounter_post = 0;
		check_trace ();
		check_trace2 ();
		x_do_cycles (c);
		return false;
	}
	gui_message (_T("CPU trace: GET %08x %d %d NOT FOUND!\n"), addr, accessmode, size);
	check_trace ();
	*data = 0;
	return false;
}

static uae_u32 cputracefunc_x_prefetch (int o)
{
	uae_u32 pc = m68k_getpc ();
	set_trace (pc + o, 2, 2);
	uae_u32 v = x2_prefetch (o);
	add_trace (pc + o, v, 2, 2);
	return v;
}
static uae_u32 cputracefunc2_x_prefetch (int o)
{
	uae_u32 v;
	if (get_trace (m68k_getpc () + o, 2, 2, &v)) {
		v = x2_prefetch (o);
		check_trace2 ();
	}
	return v;
}

static uae_u32 cputracefunc_x_next_iword (void)
{
	uae_u32 pc = m68k_getpc ();
	set_trace (pc, 2, 2);
	uae_u32 v = x2_next_iword ();
	add_trace (pc, v, 2, 2);
	return v;
}
static uae_u32 cputracefunc_x_next_ilong (void)
{
	uae_u32 pc = m68k_getpc ();
	set_trace (pc, 2, 4);
	uae_u32 v = x2_next_ilong ();
	add_trace (pc, v, 2, 4);
	return v;
}
static uae_u32 cputracefunc2_x_next_iword (void)
{
	uae_u32 v;
	if (get_trace (m68k_getpc (), 2, 2, &v)) {
		v = x2_next_iword ();
		check_trace2 ();
	}
	return v;
}
static uae_u32 cputracefunc2_x_next_ilong (void)
{
	uae_u32 v;
	if (get_trace (m68k_getpc (), 2, 4, &v)) {
		v = x2_next_ilong ();
		check_trace2 ();
	}
	return v;
}

static uae_u32 cputracefunc_x_get_ilong (int o)
{
	uae_u32 pc = m68k_getpc ();
	set_trace (pc + o, 2, 4);
	uae_u32 v = x2_get_ilong (o);
	add_trace (pc + o, v, 2, 4);
	return v;
}
static uae_u32 cputracefunc_x_get_iword (int o)
{
	uae_u32 pc = m68k_getpc ();
	set_trace (pc + o, 2, 2);
	uae_u32 v = x2_get_iword (o);
	add_trace (pc + o, v, 2, 2);
	return v;
}
static uae_u32 cputracefunc_x_get_ibyte (int o)
{
	uae_u32 pc = m68k_getpc ();
	set_trace (pc + o, 2, 1);
	uae_u32 v = x2_get_ibyte (o);
	add_trace (pc + o, v, 2, 1);
	return v;
}
static uae_u32 cputracefunc2_x_get_ilong (int o)
{
	uae_u32 v;
	if (get_trace (m68k_getpc () + o, 2, 4, &v)) {
		v = x2_get_ilong (o);
		check_trace2 ();
	}
	return v;
}
static uae_u32 cputracefunc2_x_get_iword (int o)
{
	uae_u32 v;
	if (get_trace (m68k_getpc () + o, 2, 2, &v)) {
		v = x2_get_iword (o);
		check_trace2 ();
	}
	return v;
}
static uae_u32 cputracefunc2_x_get_ibyte (int o)
{
	uae_u32 v;
	if (get_trace (m68k_getpc () + o, 2, 1, &v)) {
		v = x2_get_ibyte (o);
		check_trace2 ();
	}
	return v;
}

static uae_u32 cputracefunc_x_get_long (uaecptr o)
{
	set_trace (o, 0, 4);
	uae_u32 v = x2_get_long (o);
	add_trace (o, v, 0, 4);
	return v;
}
static uae_u32 cputracefunc_x_get_word (uaecptr o)
{
	set_trace (o, 0, 2);
	uae_u32 v = x2_get_word (o);
	add_trace (o, v, 0, 2);
	return v;
}
static uae_u32 cputracefunc_x_get_byte (uaecptr o)
{
	set_trace (o, 0, 1);
	uae_u32 v = x2_get_byte (o);
	add_trace (o, v, 0, 1);
	return v;
}
static uae_u32 cputracefunc2_x_get_long (uaecptr o)
{
	uae_u32 v;
	if (get_trace (o, 0, 4, &v)) {
		v = x2_get_long (o);
		check_trace2 ();
	}
	return v;
}
static uae_u32 cputracefunc2_x_get_word (uaecptr o)
{
	uae_u32 v;
	if (get_trace (o, 0, 2, &v)) {
		v = x2_get_word (o);
		check_trace2 ();
	}
	return v;
}
static uae_u32 cputracefunc2_x_get_byte (uaecptr o)
{
	uae_u32 v;
	if (get_trace (o, 0, 1, &v)) {
		v = x2_get_byte (o);
		check_trace2 ();
	}
	return v;
}

static void cputracefunc_x_put_long (uaecptr o, uae_u32 val)
{
	clear_trace ();
	add_trace (o, val, 1, 4);
	x2_put_long (o, val);
}
static void cputracefunc_x_put_word (uaecptr o, uae_u32 val)
{
	clear_trace ();
	add_trace (o, val, 1, 2);
	x2_put_word (o, val);
}
static void cputracefunc_x_put_byte (uaecptr o, uae_u32 val)
{
	clear_trace ();
	add_trace (o, val, 1, 1);
	x2_put_byte (o, val);
}
static void cputracefunc2_x_put_long (uaecptr o, uae_u32 val)
{
	uae_u32 v;
	if (get_trace (o, 1, 4, &v)) {
		x2_put_long (o, val);
		check_trace2 ();
	}
	if (v != val)
		write_log (_T("cputracefunc2_x_put_long %d <> %d\n"), v, val);
}
static void cputracefunc2_x_put_word (uaecptr o, uae_u32 val)
{
	uae_u32 v;
	if (get_trace (o, 1, 2, &v)) {
		x2_put_word (o, val);
		check_trace2 ();
	}
	if (v != val)
		write_log (_T("cputracefunc2_x_put_word %d <> %d\n"), v, val);
}
static void cputracefunc2_x_put_byte (uaecptr o, uae_u32 val)
{
	uae_u32 v;
	if (get_trace (o, 1, 1, &v)) {
		x2_put_byte (o, val);
		check_trace2 ();
	}
	if (v != val)
		write_log (_T("cputracefunc2_x_put_byte %d <> %d\n"), v, val);
}

static void cputracefunc_x_do_cycles (unsigned long cycles)
{
	while (cycles >= CYCLE_UNIT) {
		cputrace.cyclecounter += CYCLE_UNIT;
		cycles -= CYCLE_UNIT;
		x2_do_cycles (CYCLE_UNIT);
	}
	if (cycles > 0) {
		cputrace.cyclecounter += cycles;
		x2_do_cycles (cycles);
	}
}

static void cputracefunc2_x_do_cycles (unsigned long cycles)
{
	if (cputrace.cyclecounter > cycles) {
		cputrace.cyclecounter -= cycles;
		return;
	}
	cycles -= cputrace.cyclecounter;
	cputrace.cyclecounter = 0;
	check_trace ();
	x_do_cycles = x2_do_cycles;
	if (cycles > 0)
		x_do_cycles (cycles);
}

static void cputracefunc_x_do_cycles_pre (unsigned long cycles)
{
	cputrace.cyclecounter_post = 0;
	cputrace.cyclecounter_pre = 0;
	while (cycles >= CYCLE_UNIT) {
		cycles -= CYCLE_UNIT;
		cputrace.cyclecounter_pre += CYCLE_UNIT;
		x2_do_cycles (CYCLE_UNIT);
	}
	if (cycles > 0) {
		x2_do_cycles (cycles);
		cputrace.cyclecounter_pre += cycles;
	}
	cputrace.cyclecounter_pre = 0;
}
// cyclecounter_pre = how many cycles we need to SWALLOW
// -1 = rerun whole access
static void cputracefunc2_x_do_cycles_pre (unsigned long cycles)
{
	if (cputrace.cyclecounter_pre == -1) {
		cputrace.cyclecounter_pre = 0;
		check_trace ();
		check_trace2 ();
		x_do_cycles (cycles);
		return;
	}
	if (cputrace.cyclecounter_pre > cycles) {
		cputrace.cyclecounter_pre -= cycles;
		return;
	}
	cycles -= cputrace.cyclecounter_pre;
	cputrace.cyclecounter_pre = 0;
	check_trace ();
	if (cycles > 0)
		x_do_cycles (cycles);
}

static void cputracefunc_x_do_cycles_post (unsigned long cycles, uae_u32 v)
{
	if (cputrace.memoryoffset < 1) {
#if CPUTRACE_DEBUG
		write_log (_T("cputracefunc_x_do_cycles_post memoryoffset=%d!\n"), cputrace.memoryoffset);
#endif
		return;
	}
	struct cputracememory *ctm = &cputrace.ctm[cputrace.memoryoffset - 1];
	ctm->data = v;
	cputrace.cyclecounter_post = cycles;
	cputrace.cyclecounter_pre = 0;
	while (cycles >= CYCLE_UNIT) {
		cycles -= CYCLE_UNIT;
		cputrace.cyclecounter_post -= CYCLE_UNIT;
		x2_do_cycles (CYCLE_UNIT);
	}
	if (cycles > 0) {
		cputrace.cyclecounter_post -= cycles;
		x2_do_cycles (cycles);
	}
	cputrace.cyclecounter_post = 0;
}
// cyclecounter_post = how many cycles we need to WAIT
static void cputracefunc2_x_do_cycles_post (unsigned long cycles, uae_u32 v)
{
	uae_u32 c;
	if (cputrace.cyclecounter_post) {
		c = cputrace.cyclecounter_post;
		cputrace.cyclecounter_post = 0;
	} else {
		c = cycles;
	}
	check_trace ();
	if (c > 0)
		x_do_cycles (c);
}

static void do_cycles_post (unsigned long cycles, uae_u32 v)
{
	do_cycles (cycles);
}
static void do_cycles_ce_post (unsigned long cycles, uae_u32 v)
{
	do_cycles_ce (cycles);
}
static void do_cycles_ce020_post (unsigned long cycles, uae_u32 v)
{
	do_cycles_ce020 (cycles);
}

static uae_u8 dcache_check_nommu(uaecptr addr, bool write, uae_u32 size)
{
	return ce_cachable[addr >> 16];
}

static void mem_access_delay_long_write_ce030_cicheck(uaecptr addr, uae_u32 v)
{
	mem_access_delay_long_write_ce020(addr, v);
	mmu030_cache_state = ce_cachable[addr >> 16];
}
static void mem_access_delay_word_write_ce030_cicheck(uaecptr addr, uae_u32 v)
{
	mem_access_delay_word_write_ce020(addr, v);
	mmu030_cache_state = ce_cachable[addr >> 16];
}
static void mem_access_delay_byte_write_ce030_cicheck(uaecptr addr, uae_u32 v)
{
	mem_access_delay_byte_write_ce020(addr, v);
	mmu030_cache_state = ce_cachable[addr >> 16];
}

static void put_long030_cicheck(uaecptr addr, uae_u32 v)
{
	put_long(addr, v);
	mmu030_cache_state = ce_cachable[addr >> 16];
}
static void put_word030_cicheck(uaecptr addr, uae_u32 v)
{
	put_word(addr, v);
	mmu030_cache_state = ce_cachable[addr >> 16];
}
static void put_byte030_cicheck(uaecptr addr, uae_u32 v)
{
	put_byte(addr, v);
	mmu030_cache_state = ce_cachable[addr >> 16];
}

static uae_u32 (*icache_fetch)(uaecptr);
static uae_u32 (*dcache_lget)(uaecptr);
static uae_u32 (*dcache_wget)(uaecptr);
static uae_u32 (*dcache_bget)(uaecptr);
static uae_u8 (*dcache_check)(uaecptr, bool, uae_u32);
static void (*dcache_lput)(uaecptr, uae_u32);
static void (*dcache_wput)(uaecptr, uae_u32);
static void (*dcache_bput)(uaecptr, uae_u32);

uae_u32(*read_data_030_bget)(uaecptr);
uae_u32(*read_data_030_wget)(uaecptr);
uae_u32(*read_data_030_lget)(uaecptr);
void(*write_data_030_bput)(uaecptr,uae_u32);
void(*write_data_030_wput)(uaecptr,uae_u32);
void(*write_data_030_lput)(uaecptr,uae_u32);

uae_u32(*read_data_030_fc_bget)(uaecptr, uae_u32);
uae_u32(*read_data_030_fc_wget)(uaecptr, uae_u32);
uae_u32(*read_data_030_fc_lget)(uaecptr, uae_u32);
void(*write_data_030_fc_bput)(uaecptr, uae_u32, uae_u32);
void(*write_data_030_fc_wput)(uaecptr, uae_u32, uae_u32);
void(*write_data_030_fc_lput)(uaecptr, uae_u32, uae_u32);

 
 static void set_x_ifetches(void)
{
	if (m68k_pc_indirect) {
		if (currprefs.cachesize) {
			// indirect via addrbank
			x_get_ilong = get_iilong_jit;
			x_get_iword = get_iiword_jit;
			x_get_ibyte = get_iibyte_jit;
			x_next_iword = next_iiword_jit;
			x_next_ilong = next_iilong_jit;
		} else {
			// indirect via addrbank
			x_get_ilong = get_iilong;
			x_get_iword = get_iiword;
			x_get_ibyte = get_iibyte;
			x_next_iword = next_iiword;
			x_next_ilong = next_iilong;
		}
	} else {
		// direct to memory
		x_get_ilong = get_dilong;
		x_get_iword = get_diword;
		x_get_ibyte = get_dibyte;
		x_next_iword = next_diword;
		x_next_ilong = next_dilong;
	}
}

// indirect memory access functions
static void set_x_funcs (void)
{
	if (currprefs.mmu_model) {
		if (currprefs.cpu_model == 68060) {

			x_prefetch = get_iword_mmu060;
			x_get_ilong = get_ilong_mmu060;
			x_get_iword = get_iword_mmu060;
			x_get_ibyte = get_ibyte_mmu060;
			x_next_iword = next_iword_mmu060;
			x_next_ilong = next_ilong_mmu060;
			x_put_long = put_long_mmu060;
			x_put_word = put_word_mmu060;
			x_put_byte = put_byte_mmu060;
			x_get_long = get_long_mmu060;
			x_get_word = get_word_mmu060;
			x_get_byte = get_byte_mmu060;

		} else if (currprefs.cpu_model == 68040) {

			x_prefetch = get_iword_mmu040;
			x_get_ilong = get_ilong_mmu040;
			x_get_iword = get_iword_mmu040;
			x_get_ibyte = get_ibyte_mmu040;
			x_next_iword = next_iword_mmu040;
			x_next_ilong = next_ilong_mmu040;
			x_put_long = put_long_mmu040;
			x_put_word = put_word_mmu040;
			x_put_byte = put_byte_mmu040;
			x_get_long = get_long_mmu040;
			x_get_word = get_word_mmu040;
			x_get_byte = get_byte_mmu040;

		} else {

			if (currprefs.cpu_memory_cycle_exact) {
				x_prefetch = get_iword_mmu030c_state;
				x_get_ilong = get_ilong_mmu030c_state;
				x_get_iword = get_iword_mmu030c_state;
				x_get_ibyte = NULL;
				x_next_iword = next_iword_mmu030c_state;
				x_next_ilong = next_ilong_mmu030c_state;
				x_do_cycles = do_cycles;
				x_do_cycles_pre = do_cycles;
				x_do_cycles_post = do_cycles_post;
			} else if (currprefs.cpu_compatible) {
				x_prefetch = get_iword_mmu030c_state;
				x_get_ilong = get_ilong_mmu030c_state;
				x_get_iword = get_iword_mmu030c_state;
				x_get_ibyte = NULL;
				x_next_iword = next_iword_mmu030c_state;
				x_next_ilong = next_ilong_mmu030c_state;
				x_do_cycles = do_cycles;
				x_do_cycles_pre = do_cycles;
				x_do_cycles_post = do_cycles_post;
			} else {
				x_prefetch = get_iword_mmu030;
				x_get_ilong = get_ilong_mmu030;
				x_get_iword = get_iword_mmu030;
				x_get_ibyte = get_ibyte_mmu030;
				x_next_iword = next_iword_mmu030;
				x_next_ilong = next_ilong_mmu030;
			}
			x_put_long = put_long_mmu030;
			x_put_word = put_word_mmu030;
			x_put_byte = put_byte_mmu030;
			x_get_long = get_long_mmu030;
			x_get_word = get_word_mmu030;
			x_get_byte = get_byte_mmu030;
			if (currprefs.cpu_data_cache) {
				x_put_long = put_long_dc030;
				x_put_word = put_word_dc030;
				x_put_byte = put_byte_dc030;
				x_get_long = get_long_dc030;
				x_get_word = get_word_dc030;
				x_get_byte = get_byte_dc030;
			}

		}
		x_do_cycles = do_cycles;
		x_do_cycles_pre = do_cycles;
		x_do_cycles_post = do_cycles_post;
	} else if (currprefs.cpu_model < 68020) {
		// 68000/010
		if (currprefs.cpu_cycle_exact) {
			x_prefetch = get_word_ce000_prefetch;
			x_get_ilong = NULL;
			x_get_iword = get_wordi_ce000;
			x_get_ibyte = NULL;
			x_next_iword = NULL;
			x_next_ilong = NULL;
			x_put_long = put_long_ce000;
			x_put_word = put_word_ce000;
			x_put_byte = put_byte_ce000;
			x_get_long = get_long_ce000;
			x_get_word = get_word_ce000;
			x_get_byte = get_byte_ce000;
			x_do_cycles = do_cycles_ce;
			x_do_cycles_pre = do_cycles_ce;
			x_do_cycles_post = do_cycles_ce_post;
		} else if (currprefs.cpu_memory_cycle_exact) {
			// cpu_memory_cycle_exact + cpu_compatible
			x_prefetch = get_word_000_prefetch;
			x_get_ilong = NULL;
			x_get_iword = get_iiword;
			x_get_ibyte = get_iibyte;
			x_next_iword = NULL;
			x_next_ilong = NULL;
			x_put_long = put_long_ce000;
			x_put_word = put_word_ce000;
			x_put_byte = put_byte_ce000;
			x_get_long = get_long_ce000;
			x_get_word = get_word_ce000;
			x_get_byte = get_byte_ce000;
			x_do_cycles = do_cycles;
			x_do_cycles_pre = do_cycles;
			x_do_cycles_post = do_cycles_post;
		} else if (currprefs.cpu_compatible) {
			// cpu_compatible only
			x_prefetch = get_word_000_prefetch;
			x_get_ilong = NULL;
			x_get_iword = get_iiword;
			x_get_ibyte = get_iibyte;
			x_next_iword = NULL;
			x_next_ilong = NULL;
			x_put_long = put_long;
			x_put_word = put_word;
			x_put_byte = put_byte;
			x_get_long = get_long;
			x_get_word = get_word;
			x_get_byte = get_byte;
			x_do_cycles = do_cycles;
			x_do_cycles_pre = do_cycles;
			x_do_cycles_post = do_cycles_post;
		} else {
			x_prefetch = NULL;
			x_get_ilong = get_dilong;
			x_get_iword = get_diword;
			x_get_ibyte = get_dibyte;
			x_next_iword = next_diword;
			x_next_ilong = next_dilong;
			x_put_long = put_long;
			x_put_word = put_word;
			x_put_byte = put_byte;
			x_get_long = get_long;
			x_get_word = get_word;
			x_get_byte = get_byte;
			x_do_cycles = do_cycles;
			x_do_cycles_pre = do_cycles;
			x_do_cycles_post = do_cycles_post;
		}
	} else if (!currprefs.cpu_cycle_exact) {
		// 68020+ no ce
		if (currprefs.cpu_memory_cycle_exact) {
			// cpu_memory_cycle_exact + cpu_compatible
			if (currprefs.cpu_model == 68020 && !currprefs.cachesize) {
				x_prefetch = get_word_020_prefetch;
				x_get_ilong = get_long_020_prefetch;
				x_get_iword = get_word_020_prefetch;
				x_get_ibyte = NULL;
				x_next_iword = next_iword_020_prefetch;
				x_next_ilong = next_ilong_020_prefetch;
				x_put_long = put_long_ce020;
				x_put_word = put_word_ce020;
				x_put_byte = put_byte_ce020;
				x_get_long = get_long_ce020;
				x_get_word = get_word_ce020;
				x_get_byte = get_byte_ce020;
				x_do_cycles = do_cycles;
				x_do_cycles_pre = do_cycles;
				x_do_cycles_post = do_cycles_post;
			} else if (currprefs.cpu_model == 68030 && !currprefs.cachesize) {
				x_prefetch = get_word_030_prefetch;
				x_get_ilong = get_long_030_prefetch;
				x_get_iword = get_word_030_prefetch;
				x_get_ibyte = NULL;
				x_next_iword = next_iword_030_prefetch;
				x_next_ilong = next_ilong_030_prefetch;
				x_put_long = put_long_ce030;
				x_put_word = put_word_ce030;
				x_put_byte = put_byte_ce030;
				x_get_long = get_long_ce030;
				x_get_word = get_word_ce030;
				x_get_byte = get_byte_ce030;
				x_do_cycles = do_cycles;
				x_do_cycles_pre = do_cycles;
				x_do_cycles_post = do_cycles_post;
			} else if (currprefs.cpu_model < 68040) {
				// JIT or 68030+ does not have real prefetch only emulation
				x_prefetch = NULL;
				set_x_ifetches();
				x_put_long = put_long;
				x_put_word = put_word;
				x_put_byte = put_byte;
				x_get_long = get_long;
				x_get_word = get_word;
				x_get_byte = get_byte;
				x_do_cycles = do_cycles;
				x_do_cycles_pre = do_cycles;
				x_do_cycles_post = do_cycles_post;
			} else {
				// 68040+ (same as below)
				x_prefetch = NULL;
				x_get_ilong = get_ilong_cache_040;
				x_get_iword = get_iword_cache_040;
				x_get_ibyte = NULL;
				x_next_iword = next_iword_cache040;
				x_next_ilong = next_ilong_cache040;
				if (currprefs.cpu_data_cache) {
					x_put_long = put_long_cache_040;
					x_put_word = put_word_cache_040;
					x_put_byte = put_byte_cache_040;
					x_get_long = get_long_cache_040;
					x_get_word = get_word_cache_040;
					x_get_byte = get_byte_cache_040;
				} else {
					x_get_byte = mem_access_delay_byte_read_c040;
					x_get_word = mem_access_delay_word_read_c040;
					x_get_long = mem_access_delay_long_read_c040;
					x_put_byte = mem_access_delay_byte_write_c040;
					x_put_word = mem_access_delay_word_write_c040;
					x_put_long = mem_access_delay_long_write_c040;
				}
				x_do_cycles = do_cycles;
				x_do_cycles_pre = do_cycles;
				x_do_cycles_post = do_cycles_post;
			}
		} else if (currprefs.cpu_compatible) {
			// cpu_compatible only
			if (currprefs.cpu_model == 68020 && !currprefs.cachesize) {
				x_prefetch = get_word_020_prefetch;
				x_get_ilong = get_long_020_prefetch;
				x_get_iword = get_word_020_prefetch;
				x_get_ibyte = NULL;
				x_next_iword = next_iword_020_prefetch;
				x_next_ilong = next_ilong_020_prefetch;
				x_put_long = put_long;
				x_put_word = put_word;
				x_put_byte = put_byte;
				x_get_long = get_long;
				x_get_word = get_word;
				x_get_byte = get_byte;
				x_do_cycles = do_cycles;
				x_do_cycles_pre = do_cycles;
				x_do_cycles_post = do_cycles_post;
			} else if (currprefs.cpu_model == 68030 && !currprefs.cachesize) {
				x_prefetch = get_word_030_prefetch;
				x_get_ilong = get_long_030_prefetch;
				x_get_iword = get_word_030_prefetch;
				x_get_ibyte = NULL;
				x_next_iword = next_iword_030_prefetch;
				x_next_ilong = next_ilong_030_prefetch;
				x_put_long = put_long_030;
				x_put_word = put_word_030;
				x_put_byte = put_byte_030;
				x_get_long = get_long_030;
				x_get_word = get_word_030;
				x_get_byte = get_byte_030;
				x_do_cycles = do_cycles;
				x_do_cycles_pre = do_cycles;
				x_do_cycles_post = do_cycles_post;
			} else if (currprefs.cpu_model < 68040) {
				// JIT or 68030+ does not have real prefetch only emulation
				x_prefetch = NULL;
				set_x_ifetches();
				x_put_long = put_long;
				x_put_word = put_word;
				x_put_byte = put_byte;
				x_get_long = get_long;
				x_get_word = get_word;
				x_get_byte = get_byte;
				x_do_cycles = do_cycles;
				x_do_cycles_pre = do_cycles;
				x_do_cycles_post = do_cycles_post;
			} else {
				x_prefetch = NULL;
				x_get_ilong = get_ilong_cache_040;
				x_get_iword = get_iword_cache_040;
				x_get_ibyte = NULL;
				x_next_iword = next_iword_cache040;
				x_next_ilong = next_ilong_cache040;
				if (currprefs.cpu_data_cache) {
					x_put_long = put_long_cache_040;
					x_put_word = put_word_cache_040;
					x_put_byte = put_byte_cache_040;
					x_get_long = get_long_cache_040;
					x_get_word = get_word_cache_040;
					x_get_byte = get_byte_cache_040;
				} else {
					x_put_long = put_long;
					x_put_word = put_word;
					x_put_byte = put_byte;
					x_get_long = get_long;
					x_get_word = get_word;
					x_get_byte = get_byte;
				}
				x_do_cycles = do_cycles;
				x_do_cycles_pre = do_cycles;
				x_do_cycles_post = do_cycles_post;
			}
		} else {
			x_prefetch = NULL;
			set_x_ifetches();
			if (currprefs.cachesize) {
				x_put_long = put_long_jit;
				x_put_word = put_word_jit;
				x_put_byte = put_byte_jit;
				x_get_long = get_long_jit;
				x_get_word = get_word_jit;
				x_get_byte = get_byte_jit;
			} else {
				x_put_long = put_long;
				x_put_word = put_word;
				x_put_byte = put_byte;
				x_get_long = get_long;
				x_get_word = get_word;
				x_get_byte = get_byte;
			}
			x_do_cycles = do_cycles;
			x_do_cycles_pre = do_cycles;
			x_do_cycles_post = do_cycles_post;
		}
		// 68020+ cycle exact
	} else if (currprefs.cpu_model == 68020) {
		x_prefetch = get_word_ce020_prefetch;
		x_get_ilong = get_long_ce020_prefetch;
		x_get_iword = get_word_ce020_prefetch;
		x_get_ibyte = NULL;
		x_next_iword = next_iword_020ce;
		x_next_ilong = next_ilong_020ce;
		x_put_long = put_long_ce020;
		x_put_word = put_word_ce020;
		x_put_byte = put_byte_ce020;
		x_get_long = get_long_ce020;
		x_get_word = get_word_ce020;
		x_get_byte = get_byte_ce020;
		x_do_cycles = do_cycles_ce020;
		x_do_cycles_pre = do_cycles_ce020;
		x_do_cycles_post = do_cycles_ce020_post;
	} else if (currprefs.cpu_model == 68030) {
		x_prefetch = get_word_ce030_prefetch;
		x_get_ilong = get_long_ce030_prefetch;
		x_get_iword = get_word_ce030_prefetch;
		x_get_ibyte = NULL;
		x_next_iword = next_iword_030ce;
		x_next_ilong = next_ilong_030ce;
		if (currprefs.cpu_data_cache) {
			x_put_long = put_long_dc030;
			x_put_word = put_word_dc030;
			x_put_byte = put_byte_dc030;
			x_get_long = get_long_dc030;
			x_get_word = get_word_dc030;
			x_get_byte = get_byte_dc030;
		} else {
			x_put_long = put_long_ce030;
			x_put_word = put_word_ce030;
			x_put_byte = put_byte_ce030;
			x_get_long = get_long_ce030;
			x_get_word = get_word_ce030;
			x_get_byte = get_byte_ce030;
		}
		x_do_cycles = do_cycles_ce020;
		x_do_cycles_pre = do_cycles_ce020;
		x_do_cycles_post = do_cycles_ce020_post;
	} else if (currprefs.cpu_model >= 68040) {
		x_prefetch = NULL;
		x_get_ilong = get_ilong_cache_040;
		x_get_iword = get_iword_cache_040;
		x_get_ibyte = NULL;
		x_next_iword = next_iword_cache040;
		x_next_ilong = next_ilong_cache040;
		if (currprefs.cpu_data_cache) {
			x_put_long = put_long_cache_040;
			x_put_word = put_word_cache_040;
			x_put_byte = put_byte_cache_040;
			x_get_long = get_long_cache_040;
			x_get_word = get_word_cache_040;
			x_get_byte = get_byte_cache_040;
		} else {
			x_get_byte = mem_access_delay_byte_read_c040;
			x_get_word = mem_access_delay_word_read_c040;
			x_get_long = mem_access_delay_long_read_c040;
			x_put_byte = mem_access_delay_byte_write_c040;
			x_put_word = mem_access_delay_word_write_c040;
			x_put_long = mem_access_delay_long_write_c040;
		}
		x_do_cycles = do_cycles_ce020;
		x_do_cycles_pre = do_cycles_ce020;
		x_do_cycles_post = do_cycles_ce020_post;
	}
	x2_prefetch = x_prefetch;
	x2_get_ilong = x_get_ilong;
	x2_get_iword = x_get_iword;
	x2_get_ibyte = x_get_ibyte;
	x2_next_iword = x_next_iword;
	x2_next_ilong = x_next_ilong;
	x2_put_long = x_put_long;
	x2_put_word = x_put_word;
	x2_put_byte = x_put_byte;
	x2_get_long = x_get_long;
	x2_get_word = x_get_word;
	x2_get_byte = x_get_byte;
	x2_do_cycles = x_do_cycles;
	x2_do_cycles_pre = x_do_cycles_pre;
	x2_do_cycles_post = x_do_cycles_post;

	if (cpu_tracer > 0) {
		x_prefetch = cputracefunc_x_prefetch;
		x_get_ilong = cputracefunc_x_get_ilong;
		x_get_iword = cputracefunc_x_get_iword;
		x_get_ibyte = cputracefunc_x_get_ibyte;
		x_next_iword = cputracefunc_x_next_iword;
		x_next_ilong = cputracefunc_x_next_ilong;
		x_put_long = cputracefunc_x_put_long;
		x_put_word = cputracefunc_x_put_word;
		x_put_byte = cputracefunc_x_put_byte;
		x_get_long = cputracefunc_x_get_long;
		x_get_word = cputracefunc_x_get_word;
		x_get_byte = cputracefunc_x_get_byte;
		x_do_cycles = cputracefunc_x_do_cycles;
		x_do_cycles_pre = cputracefunc_x_do_cycles_pre;
		x_do_cycles_post = cputracefunc_x_do_cycles_post;
	} else if (cpu_tracer < 0) {
		if (!check_trace ()) {
			x_prefetch = cputracefunc2_x_prefetch;
			x_get_ilong = cputracefunc2_x_get_ilong;
			x_get_iword = cputracefunc2_x_get_iword;
			x_get_ibyte = cputracefunc2_x_get_ibyte;
			x_next_iword = cputracefunc2_x_next_iword;
			x_next_ilong = cputracefunc2_x_next_ilong;
			x_put_long = cputracefunc2_x_put_long;
			x_put_word = cputracefunc2_x_put_word;
			x_put_byte = cputracefunc2_x_put_byte;
			x_get_long = cputracefunc2_x_get_long;
			x_get_word = cputracefunc2_x_get_word;
			x_get_byte = cputracefunc2_x_get_byte;
			x_do_cycles = cputracefunc2_x_do_cycles;
			x_do_cycles_pre = cputracefunc2_x_do_cycles_pre;
			x_do_cycles_post = cputracefunc2_x_do_cycles_post;
		}
	}

	set_x_cp_funcs();
	mmu_set_funcs();
	mmu030_set_funcs();

	dcache_lput = put_long;
	dcache_wput = put_word;
	dcache_bput = put_byte;
	dcache_lget = get_long;
	dcache_wget = get_word;
	dcache_bget = get_byte;
	dcache_check = dcache_check_nommu;

	icache_fetch = get_longi;
	if (currprefs.cpu_cycle_exact) {
		icache_fetch = mem_access_delay_longi_read_ce020;
	}
	if (currprefs.cpu_model >= 68040 && currprefs.cpu_memory_cycle_exact) {
		icache_fetch = mem_access_delay_longi_read_c040;
		dcache_bget = mem_access_delay_byte_read_c040;
		dcache_wget = mem_access_delay_word_read_c040;
		dcache_lget = mem_access_delay_long_read_c040;
		dcache_bput = mem_access_delay_byte_write_c040;
		dcache_wput = mem_access_delay_word_write_c040;
		dcache_lput = mem_access_delay_long_write_c040;
	}

	if (currprefs.cpu_model == 68030) {

		if (currprefs.cpu_data_cache) {
			read_data_030_bget = read_dcache030_mmu_bget;
			read_data_030_wget = read_dcache030_mmu_wget;
			read_data_030_lget = read_dcache030_mmu_lget;
			write_data_030_bput = write_dcache030_mmu_bput;
			write_data_030_wput = write_dcache030_mmu_wput;
			write_data_030_lput = write_dcache030_mmu_lput;

			read_data_030_fc_bget = read_dcache030_bget;
			read_data_030_fc_wget = read_dcache030_wget;
			read_data_030_fc_lget = read_dcache030_lget;
			write_data_030_fc_bput = write_dcache030_bput;
			write_data_030_fc_wput = write_dcache030_wput;
			write_data_030_fc_lput = write_dcache030_lput;
		} else {
			read_data_030_bget = dcache_bget;
			read_data_030_wget = dcache_wget;
			read_data_030_lget = dcache_lget;
			write_data_030_bput = dcache_bput;
			write_data_030_wput = dcache_wput;
			write_data_030_lput = dcache_lput;

			read_data_030_fc_bget = mmu030_get_fc_byte;
			read_data_030_fc_wget = mmu030_get_fc_word;
			read_data_030_fc_lget = mmu030_get_fc_long;
			write_data_030_fc_bput = mmu030_put_fc_byte;
			write_data_030_fc_wput = mmu030_put_fc_word;
			write_data_030_fc_lput = mmu030_put_fc_long;
		}

		if (currprefs.mmu_model) {
			if (currprefs.cpu_compatible) {
				icache_fetch = uae_mmu030_get_ilong_fc;
			} else {
				icache_fetch = uae_mmu030_get_ilong;
			}
			dcache_lput = uae_mmu030_put_long_fc;
			dcache_wput = uae_mmu030_put_word_fc;
			dcache_bput = uae_mmu030_put_byte_fc;
			dcache_lget = uae_mmu030_get_long_fc;
			dcache_wget = uae_mmu030_get_word_fc;
			dcache_bget = uae_mmu030_get_byte_fc;
			if (currprefs.cpu_data_cache) {
				read_data_030_bget = read_dcache030_mmu_bget;
				read_data_030_wget = read_dcache030_mmu_wget;
				read_data_030_lget = read_dcache030_mmu_lget;
				write_data_030_bput = write_dcache030_mmu_bput;
				write_data_030_wput = write_dcache030_mmu_wput;
				write_data_030_lput = write_dcache030_mmu_lput;
				dcache_check = uae_mmu030_check_fc;
			} else {
				read_data_030_bget = uae_mmu030_get_byte;
				read_data_030_wget = uae_mmu030_get_word;
				read_data_030_lget = uae_mmu030_get_long;
				write_data_030_bput = uae_mmu030_put_byte;
				write_data_030_wput = uae_mmu030_put_word;
				write_data_030_lput = uae_mmu030_put_long;
			}
		} else if (currprefs.cpu_memory_cycle_exact) {
			icache_fetch = mem_access_delay_longi_read_ce020;
			dcache_lget = mem_access_delay_long_read_ce020;
			dcache_wget = mem_access_delay_word_read_ce020;
			dcache_bget = mem_access_delay_byte_read_ce020;
			if (currprefs.cpu_data_cache) {
				dcache_lput = mem_access_delay_long_write_ce030_cicheck;
				dcache_wput = mem_access_delay_word_write_ce030_cicheck;
				dcache_bput = mem_access_delay_byte_write_ce030_cicheck;
			} else {
				dcache_lput = mem_access_delay_long_write_ce020;
				dcache_wput = mem_access_delay_word_write_ce020;
				dcache_bput = mem_access_delay_byte_write_ce020;
			}
		} else if (currprefs.cpu_data_cache) {
			dcache_lput = put_long030_cicheck;
			dcache_wput = put_word030_cicheck;
			dcache_bput = put_byte030_cicheck;
		}
	}
}

bool can_cpu_tracer (void)
{
	return (currprefs.cpu_model == 68000 || currprefs.cpu_model == 68020) && currprefs.cpu_memory_cycle_exact;
}

bool is_cpu_tracer (void)
{
	return cpu_tracer > 0;
}
bool set_cpu_tracer (bool state)
{
	if (cpu_tracer < 0)
		return false;
	int old = cpu_tracer;
	if (input_record)
		state = true;
	cpu_tracer = 0;
	if (state && can_cpu_tracer ()) {
		cpu_tracer = 1;
		set_x_funcs ();
		if (old != cpu_tracer)
			write_log (_T("CPU tracer enabled\n"));
	}
	if (old > 0 && state == false) {
		set_x_funcs ();
		write_log (_T("CPU tracer disabled\n"));
	}
	return is_cpu_tracer ();
}

static void invalidate_cpu_data_caches(void)
{
	if (currprefs.cpu_model == 68030) {
		for (int i = 0; i < CACHELINES030; i++) {
			dcaches030[i].valid[0] = 0;
			dcaches030[i].valid[1] = 0;
			dcaches030[i].valid[2] = 0;
			dcaches030[i].valid[3] = 0;
		}
	} else if (currprefs.cpu_model >= 68040) {
		dcachelinecnt = 0;
		for (int i = 0; i < CACHESETS060; i++) {
			for (int j = 0; j < CACHELINES040; j++) {
				dcaches040[i].valid[j] = false;
			}
		}
	}
}

void flush_cpu_caches(bool force)
{
	bool doflush = currprefs.cpu_compatible || currprefs.cpu_memory_cycle_exact;

	if (currprefs.cpu_model == 68020) {
		if ((regs.cacr & 0x08) || force) { // clear instr cache
			for (int i = 0; i < CACHELINES020; i++)
				caches020[i].valid = 0;
			regs.cacr &= ~0x08;
		}
		if (regs.cacr & 0x04) { // clear entry in instr cache
			caches020[(regs.caar >> 2) & (CACHELINES020 - 1)].valid = 0;
			regs.cacr &= ~0x04;
		}
	} else if (currprefs.cpu_model == 68030) {
		if ((regs.cacr & 0x08) || force) { // clear instr cache
			if (doflush) {
				for (int i = 0; i < CACHELINES030; i++) {
					icaches030[i].valid[0] = 0;
					icaches030[i].valid[1] = 0;
					icaches030[i].valid[2] = 0;
					icaches030[i].valid[3] = 0;
				}
			}
			regs.cacr &= ~0x08;
		}
		if (regs.cacr & 0x04) { // clear entry in instr cache
			icaches030[(regs.caar >> 4) & (CACHELINES030 - 1)].valid[(regs.caar >> 2) & 3] = 0;
			regs.cacr &= ~0x04;
		}
		if ((regs.cacr & 0x800) || force) { // clear data cache
			if (doflush) {
				for (int i = 0; i < CACHELINES030; i++) {
					dcaches030[i].valid[0] = 0;
					dcaches030[i].valid[1] = 0;
					dcaches030[i].valid[2] = 0;
					dcaches030[i].valid[3] = 0;
				}
			}
			regs.cacr &= ~0x800;
		}
		if (regs.cacr & 0x400) { // clear entry in data cache
			dcaches030[(regs.caar >> 4) & (CACHELINES030 - 1)].valid[(regs.caar >> 2) & 3] = 0;
			regs.cacr &= ~0x400;
		}
	} else if (currprefs.cpu_model >= 68040) {
		if (doflush && force) {
			mmu_flush_cache();
			icachelinecnt = 0;
			icachehalfline = 0;
			for (int i = 0; i < CACHESETS060; i++) {
				for (int j = 0; j < CACHELINES040; j++) {
					icaches040[i].valid[j] = false;
				}
			}
		}
	}
}

#if VALIDATE_68040_DATACACHE > 1
static void validate_dcache040(void)
{
	for (int i = 0; i < cachedsets04060; i++) {
		struct cache040 *c = &dcaches040[i];
		for (int j = 0; j < CACHELINES040; j++) {
			if (c->valid[j]) {
				uae_u32 addr = (c->tag[j] & cachedtag04060mask) | (i << 4);
				if (addr < 0x200000 || (addr >= 0xd80000 && addr < 0xe00000) || (addr >= 0xe80000 && addr < 0xf00000) || (addr >= 0xa00000 && addr < 0xc00000)) {
					write_log(_T("Chip RAM or IO address cached! %08x\n"), addr);
				}
				for (int k = 0; k < 4; k++) {
					if (!c->dirty[j][k]) {
						uae_u32 v = get_long(addr + k * 4);
						if (v != c->data[j][k]) {
							write_log(_T("Address %08x data cache mismatch %08x != %08x\n"), addr, v, c->data[j][k]);
						}
					}
				}
			}
		}
	}
}
#endif

static void dcache040_push_line(int index, int line, bool writethrough, bool invalidate)
{
	struct cache040 *c = &dcaches040[index];
#if VALIDATE_68040_DATACACHE
	if (!c->valid[line]) {
		write_log("dcache040_push_line pushing invalid line!\n");
	}
#endif
	if (c->gdirty[line]) {
		uae_u32 addr = (c->tag[line] & cachedtag04060mask) | (index << 4);
		for (int i = 0; i < 4; i++) {
			if (c->dirty[line][i] || (!writethrough && currprefs.cpu_model == 68060)) {
				dcache_lput(addr + i * 4, c->data[line][i]);
				c->dirty[line][i] = false;
			}
		}
		c->gdirty[line] = false;
	}
	if (invalidate)
		c->valid[line] = false;

#if VALIDATE_68040_DATACACHE > 1
	validate_dcache040();
#endif
}

static void flush_cpu_caches_040_2(int cache, int scope, uaecptr addr, bool push, bool pushinv)
{

#if VALIDATE_68040_DATACACHE
	write_log(_T("push %d %d %d %08x %d %d\n"), cache, scope, areg, addr, push, pushinv);
#endif

	if (cache & 2)
		regs.prefetch020addr = 0xffffffff;
	for (int k = 0; k < 2; k++) {
		if (cache & (1 << k)) {
			if (scope == 3) {
				// all
				if (!k) {
					// data
					for (int i = 0; i < cachedsets04060; i++) {
						struct cache040 *c = &dcaches040[i];
						for (int j = 0; j < CACHELINES040; j++) {
							if (c->valid[j]) {
								if (push) {
									dcache040_push_line(i, j, false, pushinv);
								} else {
									c->valid[j] = false;
								}
							}
						}
					}
					dcachelinecnt = 0;
				} else {
					// instruction
					flush_cpu_caches(true);
				}
			} else {
				uae_u32 pagesize;
				if (scope == 2) {
					// page
					pagesize = mmu_pagesize_8k ? 8192 : 4096;
				} else {
					// line
					pagesize = 16;
				}
				addr &= ~(pagesize - 1);
				for (int j = 0; j < pagesize; j += 16, addr += 16) {
					int index;
					uae_u32 tag;
					uae_u32 tagmask;
					struct cache040 *c;
					if (k) {
						tagmask = cacheitag04060mask;
						index = (addr >> 4) & cacheisets04060mask;
						c = &icaches040[index];
					} else {
						tagmask = cachedtag04060mask;
						index = (addr >> 4) & cachedsets04060mask;
						c = &dcaches040[index];
					}
					tag = addr & tagmask;
					for (int i = 0; i < CACHELINES040; i++) {
						if (c->valid[i] && c->tag[i] == tag) {
							if (push) {
								dcache040_push_line(index, i, false, pushinv);
							} else {
								c->valid[i] = false;
							}
						}
					}
				}
			}
		}
	}
}

void flush_cpu_caches_040(uae_u16 opcode)
{
	// 0 (1) = data, 1 (2) = instruction
	int cache = (opcode >> 6) & 3;
	int scope = (opcode >> 3) & 3;
	int areg = opcode & 7;
	uaecptr addr = m68k_areg(regs, areg);
	bool push = (opcode & 0x20) != 0;
	bool pushinv = (regs.cacr & 0x01000000) == 0; // 68060 DPI

	flush_cpu_caches_040_2(cache, scope, addr, push, pushinv);
	mmu_flush_cache();
}

void cpu_invalidate_cache(uaecptr addr, int size)
{
	if (!currprefs.cpu_data_cache)
		return;
	if (currprefs.cpu_model == 68030) {
		uaecptr end = addr + size;
		addr &= ~3;
		while (addr < end) {
			dcaches030[(addr >> 4) & (CACHELINES030 - 1)].valid[(addr >> 2) & 3] = 0;
			addr += 4;
		}
	} else if (currprefs.cpu_model >= 68040) {
		uaecptr end = addr + size;
		while (addr < end) {
			flush_cpu_caches_040_2(0, 1, addr, true, true);
			addr += 16;
		}
	}
}


void set_cpu_caches (bool flush)
{
	regs.prefetch020addr = 0xffffffff;
	regs.cacheholdingaddr020 = 0xffffffff;
	cache_default_data &= ~CACHE_DISABLE_ALLOCATE;

	// 68060 FIC 1/2 instruction cache
	cacheisets04060 = currprefs.cpu_model == 68060 && !(regs.cacr & 0x00002000) ? CACHESETS060 : CACHESETS040;
	cacheisets04060mask = cacheisets04060 - 1;
	cacheitag04060mask = ~((cacheisets04060 << 4) - 1);
	// 68060 FOC 1/2 data cache
	cachedsets04060 = currprefs.cpu_model == 68060 && !(regs.cacr & 0x08000000) ? CACHESETS060 : CACHESETS040;
	cachedsets04060mask = cachedsets04060 - 1;
	cachedtag04060mask = ~((cachedsets04060 << 4) - 1);
	cache_lastline = 0;

#ifdef JIT
	if (currprefs.cachesize) {
		if (currprefs.cpu_model < 68040) {
			set_cache_state (regs.cacr & 1);
			if (regs.cacr & 0x08) {
				flush_icache (3);
			}
		} else {
			set_cache_state ((regs.cacr & 0x8000) ? 1 : 0);
		}
	}
#endif
	flush_cpu_caches(flush);
}

STATIC_INLINE void count_instr (unsigned int opcode)
{
}

static uae_u32 REGPARAM2 op_illg_1 (uae_u32 opcode)
{
	op_illg (opcode);
	return 4;
}
static uae_u32 REGPARAM2 op_unimpl_1 (uae_u32 opcode)
{
	if ((opcode & 0xf000) == 0xf000 || currprefs.cpu_model < 68060)
		op_illg (opcode);
	else
		op_unimpl (opcode);
	return 4;
}

// generic+direct, generic+direct+jit, generic+indirect, more compatible, cycle-exact, mmu, mmu+more compatible, mmu+mc+ce
static const struct cputbl *cputbls[6][8] =
{
	// 68000
	{ op_smalltbl_5_ff, op_smalltbl_45_ff, op_smalltbl_55_ff, op_smalltbl_12_ff, op_smalltbl_14_ff, NULL, NULL, NULL },
	// 68010
	{ op_smalltbl_4_ff, op_smalltbl_44_ff, op_smalltbl_54_ff, op_smalltbl_11_ff, op_smalltbl_13_ff, NULL, NULL, NULL },
	// 68020
	{ op_smalltbl_3_ff, op_smalltbl_43_ff, op_smalltbl_53_ff, op_smalltbl_20_ff, op_smalltbl_21_ff, NULL, NULL, NULL },
	// 68030
	{ op_smalltbl_2_ff, op_smalltbl_42_ff, op_smalltbl_52_ff, op_smalltbl_22_ff, op_smalltbl_23_ff, op_smalltbl_32_ff, op_smalltbl_34_ff, op_smalltbl_35_ff },
	// 68040
	{ op_smalltbl_1_ff, op_smalltbl_41_ff, op_smalltbl_51_ff, op_smalltbl_25_ff, op_smalltbl_25_ff, op_smalltbl_31_ff, op_smalltbl_31_ff, op_smalltbl_31_ff },
	// 68060
	{ op_smalltbl_0_ff, op_smalltbl_40_ff, op_smalltbl_50_ff, op_smalltbl_24_ff, op_smalltbl_24_ff, op_smalltbl_33_ff, op_smalltbl_33_ff, op_smalltbl_33_ff }
};

static void build_cpufunctbl (void)
{
	int i, opcnt;
	unsigned long opcode;
	const struct cputbl *tbl = 0;
	int lvl, mode;

	if (!currprefs.cachesize) {
		if (currprefs.mmu_model) {
			if (currprefs.cpu_cycle_exact)
				mode = 7;
			else if (currprefs.cpu_compatible)
				mode = 6;
			else
				mode = 5;
		} else if (currprefs.cpu_cycle_exact) {
			mode = 4;
		} else if (currprefs.cpu_compatible) {
			mode = 3;
		} else {
			mode = 0;
		}
		m68k_pc_indirect = mode != 0 ? 1 : 0;
	} else {
		mode = 1;
		m68k_pc_indirect = 0;
		if (currprefs.comptrustbyte) {
			mode = 2;
			m68k_pc_indirect = -1;
		}
	}
	lvl = (currprefs.cpu_model - 68000) / 10;
	if (lvl == 6)
		lvl = 5;
	tbl = cputbls[lvl][mode];

	if (tbl == NULL) {
		write_log (_T("no CPU emulation cores available CPU=%d!"), currprefs.cpu_model);
		abort ();
	}

	for (opcode = 0; opcode < 65536; opcode++)
		cpufunctbl[opcode] = op_illg_1;
	for (i = 0; tbl[i].handler != NULL; i++) {
		opcode = tbl[i].opcode;
		cpufunctbl[opcode] = tbl[i].handler;
		cpudatatbl[opcode].length = tbl[i].length;
		cpudatatbl[opcode].disp020[0] = tbl[i].disp020[0];
		cpudatatbl[opcode].disp020[1] = tbl[i].disp020[1];
		cpudatatbl[opcode].branch = tbl[i].branch;
	}

	/* hack fpu to 68000/68010 mode */
	if (currprefs.fpu_model && currprefs.cpu_model < 68020) {
		tbl = op_smalltbl_3_ff;
		for (i = 0; tbl[i].handler != NULL; i++) {
			if ((tbl[i].opcode & 0xfe00) == 0xf200) {
				cpufunctbl[tbl[i].opcode] = tbl[i].handler;
				cpudatatbl[tbl[i].opcode].length = tbl[i].length;
				cpudatatbl[tbl[i].opcode].disp020[0] = tbl[i].disp020[0];
				cpudatatbl[tbl[i].opcode].disp020[1] = tbl[i].disp020[1];
				cpudatatbl[tbl[i].opcode].branch = tbl[i].branch;
			}
		}
	}

	opcnt = 0;
	for (opcode = 0; opcode < 65536; opcode++) {
		cpuop_func *f;
		instr *table = &table68k[opcode];

		if (table->mnemo == i_ILLG)
			continue;		

		/* unimplemented opcode? */
		if (table->unimpclev > 0 && lvl >= table->unimpclev) {
			if (currprefs.cpu_model == 68060) {
				// remove unimplemented integer instructions
				// unimpclev == 5: not implemented in 68060,
				// generates unimplemented instruction exception.
				if (currprefs.int_no_unimplemented && table->unimpclev == 5) {
					cpufunctbl[opcode] = op_unimpl_1;
					continue;
				}
				// remove unimplemented instruction that were removed in previous models,
				// generates normal illegal instruction exception.
				// unimplclev < 5: instruction was removed in 68040 or previous model.
				// clev=4: implemented in 68040 or later. unimpclev=5: not in 68060
				if (table->unimpclev < 5 || (table->clev == 4 && table->unimpclev == 5)) {
					cpufunctbl[opcode] = op_illg_1;
					continue;
				}
			} else {
				cpufunctbl[opcode] = op_illg_1;
				continue;
			}
		}

		if (currprefs.fpu_model && currprefs.cpu_model < 68020) {
			/* more hack fpu to 68000/68010 mode */
			if (table->clev > lvl && (opcode & 0xfe00) != 0xf200)
				continue;
		} else if (table->clev > lvl) {
			continue;
		}

		if (table->handler != -1) {
			int idx = table->handler;
			f = cpufunctbl[idx];
			if (f == op_illg_1)
				abort ();
			cpufunctbl[opcode] = f;
			memcpy(&cpudatatbl[opcode], &cpudatatbl[idx], sizeof(struct cputbl_data));
			opcnt++;
		}
	}
	write_log (_T("Building CPU, %d opcodes (%d %d %d)\n"),
		opcnt, lvl,
		currprefs.cpu_cycle_exact ? -2 : currprefs.cpu_memory_cycle_exact ? -1 : currprefs.cpu_compatible ? 1 : 0, currprefs.address_space_24);
#ifdef JIT
	write_log(_T("JIT: &countdown =  %p\n"), &countdown);
	write_log(_T("JIT: &build_comp = %p\n"), &build_comp);
	build_comp ();
#endif

	write_log(_T("CPU=%d, FPU=%d%s, MMU=%d, JIT%s=%d."),
		currprefs.cpu_model,
		currprefs.fpu_model, currprefs.fpu_model ? (currprefs.fpu_mode > 0 ? _T(" (softfloat)") : (currprefs.fpu_mode < 0 ? _T(" (host 80b)") : _T(" (host 64b)"))) : _T(""),
		currprefs.mmu_model,
		currprefs.cachesize ? (currprefs.compfpu ? _T("=CPU/FPU") : _T("=CPU")) : _T(""),
		currprefs.cachesize);

	regs.address_space_mask = 0xffffffff;
	if (currprefs.cpu_compatible) {
		if (currprefs.address_space_24 && currprefs.cpu_model >= 68040)
			currprefs.address_space_24 = false;
	}
	m68k_interrupt_delay = false;
	if (currprefs.cpu_cycle_exact) {
		if (tbl == op_smalltbl_14_ff || tbl == op_smalltbl_13_ff || tbl == op_smalltbl_21_ff || tbl == op_smalltbl_23_ff)
			m68k_interrupt_delay = true;
	}

	if (currprefs.cpu_cycle_exact) {
		if (currprefs.cpu_model == 68000)
			write_log(_T(" prefetch and cycle-exact"));
		else
			write_log(_T(" ~cycle-exact"));
	} else if (currprefs.cpu_memory_cycle_exact) {
			write_log(_T(" ~memory-cycle-exact"));
	} else if (currprefs.cpu_compatible) {
		if (currprefs.cpu_model <= 68020) {
			write_log(_T(" prefetch"));
		} else {
			write_log(_T(" fake prefetch"));
		}
	}
	if (currprefs.m68k_speed < 0)
		write_log(_T(" fast"));
	if (currprefs.int_no_unimplemented && currprefs.cpu_model == 68060) {
		write_log(_T(" no unimplemented integer instructions"));
	}
	if (currprefs.fpu_no_unimplemented && currprefs.fpu_model) {
		write_log(_T(" no unimplemented floating point instructions"));
	}
	if (currprefs.address_space_24) {
		regs.address_space_mask = 0x00ffffff;
		write_log(_T(" 24-bit"));
	}
	write_log(_T("\n"));

	set_cpu_caches (true);
	target_cpu_speed();
}

#define CYCLES_DIV 8192
static unsigned long cycles_mult;

static void update_68k_cycles (void)
{
	cycles_mult = 0;
	if (currprefs.m68k_speed >= 0 && !currprefs.cpu_cycle_exact) {
		if (currprefs.m68k_speed_throttle < 0) {
			cycles_mult = (unsigned long)(CYCLES_DIV * 1000 / (1000 + currprefs.m68k_speed_throttle));
		} else if (currprefs.m68k_speed_throttle > 0) {
			cycles_mult = (unsigned long)(CYCLES_DIV * 1000 / (1000 + currprefs.m68k_speed_throttle));
		}
	}
	if (currprefs.m68k_speed == 0) {
		if (currprefs.cpu_model >= 68040) {
			if (!cycles_mult)
				cycles_mult = CYCLES_DIV / 8;
			else
				cycles_mult /= 8;
		} else if (currprefs.cpu_model >= 68020) {
			if (!cycles_mult)
				cycles_mult = CYCLES_DIV / 4;
			else
				cycles_mult /= 4;
		}
	}

	currprefs.cpu_clock_multiplier = changed_prefs.cpu_clock_multiplier;
	currprefs.cpu_frequency = changed_prefs.cpu_frequency;

	baseclock = (currprefs.ntscmode ? CHIPSET_CLOCK_NTSC : CHIPSET_CLOCK_PAL) * 8;
	cpucycleunit = CYCLE_UNIT / 2;
	if (currprefs.cpu_clock_multiplier) {
		if (currprefs.cpu_clock_multiplier >= 256) {
			cpucycleunit = CYCLE_UNIT / (currprefs.cpu_clock_multiplier >> 8);
		} else {
			cpucycleunit = CYCLE_UNIT * currprefs.cpu_clock_multiplier;
		}
		if (currprefs.cpu_model >= 68040)
			cpucycleunit /= 2;
	} else if (currprefs.cpu_frequency) {
		cpucycleunit = CYCLE_UNIT * baseclock / currprefs.cpu_frequency;
	} else if (currprefs.cpu_memory_cycle_exact && currprefs.cpu_clock_multiplier == 0) {
		if (currprefs.cpu_model >= 68040) {
			cpucycleunit = CYCLE_UNIT / 16;
		} if (currprefs.cpu_model == 68030) {
			cpucycleunit = CYCLE_UNIT / 8;
		} else if (currprefs.cpu_model == 68020) {
			cpucycleunit = CYCLE_UNIT / 4;
		} else {
			cpucycleunit = CYCLE_UNIT / 2;
		}
	}
	if (cpucycleunit < 1)
		cpucycleunit = 1;
	if (currprefs.cpu_cycle_exact)
		write_log (_T("CPU cycleunit: %d (%.3f)\n"), cpucycleunit, (float)cpucycleunit / CYCLE_UNIT);
	set_config_changed ();
}

static void prefs_changed_cpu (void)
{
	fixup_cpu (&changed_prefs);
	check_prefs_changed_comp(false);
	currprefs.cpu_model = changed_prefs.cpu_model;
	currprefs.fpu_model = changed_prefs.fpu_model;
	if (currprefs.mmu_model != changed_prefs.mmu_model) {
		int oldmmu = currprefs.mmu_model;
		currprefs.mmu_model = changed_prefs.mmu_model;
		if (currprefs.mmu_model >= 68040) {
			uae_u32 tcr = regs.tcr;
			mmu_reset();
			mmu_set_tc(tcr);
			mmu_set_super(regs.s != 0);
			mmu_tt_modified();
		} else if (currprefs.mmu_model == 68030) {
			mmu030_reset(-1);
			mmu030_flush_atc_all();
			tc_030 = fake_tc_030;
			tt0_030 = fake_tt0_030;
			tt1_030 = fake_tt1_030;
			srp_030 = fake_srp_030;
			crp_030 = fake_crp_030;
			mmu030_decode_tc(tc_030, false);
		} else if (oldmmu == 68030) {
			fake_tc_030 = tc_030;
			fake_tt0_030 = tt0_030;
			fake_tt1_030 = tt1_030;
			fake_srp_030 = srp_030;
			fake_crp_030 = crp_030;
		}
	}
	currprefs.mmu_ec = changed_prefs.mmu_ec;
	if (currprefs.cpu_compatible != changed_prefs.cpu_compatible) {
		currprefs.cpu_compatible = changed_prefs.cpu_compatible;
		flush_cpu_caches(true);
		invalidate_cpu_data_caches();
	}
	if (currprefs.cpu_data_cache != changed_prefs.cpu_data_cache) {
		currprefs.cpu_data_cache = changed_prefs.cpu_data_cache;
		invalidate_cpu_data_caches();
	}
	currprefs.address_space_24 = changed_prefs.address_space_24;
	currprefs.cpu_cycle_exact = changed_prefs.cpu_cycle_exact;
	currprefs.cpu_memory_cycle_exact = changed_prefs.cpu_memory_cycle_exact;
	currprefs.int_no_unimplemented = changed_prefs.int_no_unimplemented;
	currprefs.fpu_no_unimplemented = changed_prefs.fpu_no_unimplemented;
	currprefs.blitter_cycle_exact = changed_prefs.blitter_cycle_exact;
}

static int check_prefs_changed_cpu2(void)
{
	int changed = 0;

#ifdef JIT
	changed = check_prefs_changed_comp(true) ? 1 : 0;
#endif
	if (changed
		|| currprefs.cpu_model != changed_prefs.cpu_model
		|| currprefs.fpu_model != changed_prefs.fpu_model
		|| currprefs.mmu_model != changed_prefs.mmu_model
		|| currprefs.mmu_ec != changed_prefs.mmu_ec
		|| currprefs.cpu_data_cache != changed_prefs.cpu_data_cache
		|| currprefs.int_no_unimplemented != changed_prefs.int_no_unimplemented
		|| currprefs.fpu_no_unimplemented != changed_prefs.fpu_no_unimplemented
		|| currprefs.cpu_compatible != changed_prefs.cpu_compatible
		|| currprefs.cpu_cycle_exact != changed_prefs.cpu_cycle_exact
		|| currprefs.cpu_memory_cycle_exact != changed_prefs.cpu_memory_cycle_exact
		|| currprefs.fpu_mode != changed_prefs.fpu_mode) {
			cpu_prefs_changed_flag |= 1;
	}
	if (changed
		|| currprefs.m68k_speed != changed_prefs.m68k_speed
		|| currprefs.m68k_speed_throttle != changed_prefs.m68k_speed_throttle
		|| currprefs.cpu_clock_multiplier != changed_prefs.cpu_clock_multiplier
		|| currprefs.reset_delay != changed_prefs.reset_delay
		|| currprefs.cpu_frequency != changed_prefs.cpu_frequency) {
			cpu_prefs_changed_flag |= 2;
	}
	return cpu_prefs_changed_flag;
}

void check_prefs_changed_cpu(void)
{
	if (!config_changed)
		return;

	currprefs.cpu_idle = changed_prefs.cpu_idle;
	currprefs.ppc_cpu_idle = changed_prefs.ppc_cpu_idle;
	currprefs.reset_delay = changed_prefs.reset_delay;
	currprefs.cpuboard_settings = changed_prefs.cpuboard_settings;

	if (check_prefs_changed_cpu2()) {
		set_special(SPCFLAG_MODE_CHANGE);
		reset_frame_rate_hack();
	}
}

void init_m68k (void)
{
	prefs_changed_cpu ();
	update_68k_cycles ();

	for (int i = 0 ; i < 256 ; i++) {
		int j;
		for (j = 0 ; j < 8 ; j++) {
			if (i & (1 << j)) break;
		}
		movem_index1[i] = j;
		movem_index2[i] = 7 - j;
		movem_next[i] = i & (~(1 << j));
	}

#if COUNT_INSTRS
	{
		FILE *f = fopen (icountfilename (), "r");
		memset (instrcount, 0, sizeof instrcount);
		if (f) {
			uae_u32 opcode, count, total;
			TCHAR name[20];
			write_log (_T("Reading instruction count file...\n"));
			fscanf (f, "Total: %lu\n", &total);
			while (fscanf (f, "%x: %lu %s\n", &opcode, &count, name) == 3) {
				instrcount[opcode] = count;
			}
			fclose (f);
		}
	}
#endif

	read_table68k ();
	do_merges ();

	write_log (_T("%d CPU functions\n"), nr_cpuop_funcs);
}

struct regstruct regs, mmu_backup_regs;
struct flag_struct regflags;
static int m68kpc_offset;

static const TCHAR *fpsizes[] = {
	_T("L"),
	_T("S"),
	_T("X"),
	_T("P"),
	_T("W"),
	_T("D"),
	_T("B"),
	_T("P")
};
static const int fpsizeconv[] = {
	sz_long,
	sz_single,
	sz_extended,
	sz_packed,
	sz_word,
	sz_double,
	sz_byte,
	sz_packed
};
static const int datasizes[] = {
	1,
	2,
	4,
	4,
	8,
	12,
	12
};

static void showea_val(TCHAR *buffer, uae_u16 opcode, uaecptr addr, int size)
{
	struct mnemolookup *lookup;
	instr *table = &table68k[opcode];

	if (addr >= 0xe90000 && addr < 0xf00000)
		goto skip;
	if (addr >= 0xdff000 && addr < 0xe00000)
		goto skip;

	for (lookup = lookuptab; lookup->mnemo != table->mnemo; lookup++)
		;
	if (!(lookup->flags & 1))
		goto skip;
	buffer += _tcslen(buffer);
	if (debug_safe_addr(addr, datasizes[size])) {
		bool cached = false;
		switch (size)
		{
			case sz_byte:
			{
				uae_u8 v = get_byte_cache_debug(addr, &cached);
				uae_u8 v2 = v;
				if (cached)
					v2 = get_byte_debug(addr);
				if (v != v2) {
					_stprintf(buffer, _T(" [%02x:%02x]"), v, v2);
				} else {
					_stprintf(buffer, _T(" [%s%02x]"), cached ? _T("*") : _T(""), v);
				}
			}
			break;
			case sz_word:
			{
				uae_u16 v = get_word_cache_debug(addr, &cached);
				uae_u16 v2 = v;
				if (cached)
					v2 = get_word_debug(addr);
				if (v != v2) {
					_stprintf(buffer, _T(" [%04x:%04x]"), v, v2);
				} else {
					_stprintf(buffer, _T(" [%s%04x]"), cached ? _T("*") : _T(""), v);
				}
			}
			break;
			case sz_long:
			{
				uae_u32 v = get_long_cache_debug(addr, &cached);
				uae_u32 v2 = v;
				if (cached)
					v2 = get_long_debug(addr);
				if (v != v2) {
					_stprintf(buffer, _T(" [%08x:%08x]"), v, v2);
				} else {
					_stprintf(buffer, _T(" [%s%08x]"), cached ? _T("*") : _T(""), v);
				}
			}
			break;
			case sz_single:
			{
				fpdata fp;
				fpp_to_single(&fp, get_long_debug(addr));
				_stprintf(buffer, _T("[%s]"), fpp_print(&fp, 0));
			}
			break;
			case sz_double:
			{
				fpdata fp;
				fpp_to_double(&fp, get_long_debug(addr), get_long_debug(addr + 4));
				_stprintf(buffer, _T("[%s]"), fpp_print(&fp, 0));
			}
			break;
			case sz_extended:
			{
				fpdata fp;
				fpp_to_exten(&fp, get_long_debug(addr), get_long_debug(addr + 4), get_long_debug(addr + 8));
				_stprintf(buffer, _T("[%s]"), fpp_print(&fp, 0));
				break;
			}
			case sz_packed:
				_stprintf(buffer, _T("[%08x%08x%08x]"), get_long_debug(addr), get_long_debug(addr + 4), get_long_debug(addr + 8));
				break;
		}
	}
skip:
	for (int i = 0; i < size; i++) {
		TCHAR name[256];
		if (debugmem_get_symbol(addr + i, name, sizeof(name) / sizeof(TCHAR))) {
			_stprintf(buffer + _tcslen(buffer), _T(" %s"), name);
		}
	}
}

static uaecptr ShowEA_disp(uaecptr *pcp, uaecptr base, TCHAR *buffer, const TCHAR *name)
{
	uaecptr addr;
	uae_u16 dp;
	int r;
	uae_u32 dispreg;
	uaecptr pc = *pcp;
	TCHAR mult[20];

	dp = get_iword_debug(pc);
	pc += 2;

	r = (dp & 0x7000) >> 12; // REGISTER

	dispreg = dp & 0x8000 ? m68k_areg(regs, r) : m68k_dreg(regs, r);
	if (!(dp & 0x800)) { // W/L
		dispreg = (uae_s32)(uae_s16)(dispreg);
	}

	if (currprefs.cpu_model >= 68020) {
		dispreg <<= (dp >> 9) & 3; // SCALE
	}

	int m = 1 << ((dp >> 9) & 3);
	mult[0] = 0;
	if (m > 1) {
		_stprintf(mult, _T("*%d"), m);
	}

	buffer[0] = 0;
	if ((dp & 0x100) && currprefs.cpu_model >= 68020) {
		TCHAR dr[20];
		// Full format extension (68020+)
		uae_s32 outer = 0, disp = 0;
		if (dp & 0x80) { // BS (base register suppress)
			base = 0;
			name = NULL;
		}
		_stprintf(dr, _T("%c%d.%c"), dp & 0x8000 ? 'A' : 'D', (int)r, dp & 0x800 ? 'L' : 'W');
		if (dp & 0x40) { // IS (index suppress)
			dispreg = 0;
			dr[0] = 0;
		}

		_tcscpy(buffer, _T("("));
		TCHAR *p = buffer + _tcslen(buffer);

		if (dp & 3) {
			// indirect
			_stprintf(p, _T("["));
			p += _tcslen(p);
		} else {
			// (an,dn,word/long)
			if (name) {
				_stprintf(p, _T("%s,"), name);
				p += _tcslen(p);
			}
			if (dr[0]) {
				_stprintf(p, _T("%s%s,"), dr, mult);
				p += _tcslen(p);
			}
		}

		if ((dp & 0x30) == 0x20) { // BD SIZE = 2 (WORD)
			disp = (uae_s32)(uae_s16)get_iword_debug(pc);
			_stprintf(p, _T("$%04x,"), (uae_s16)disp);
			p += _tcslen(p);
			pc += 2;
			base += disp;
		} else if ((dp & 0x30) == 0x30) { // BD SIZE = 3 (LONG)
			disp = get_ilong_debug(pc);
			_stprintf(p, _T("$%08x,"), disp);
			p += _tcslen(p);
			pc += 4;
			base += disp;
		}

		if (dp & 3) {
			if (name) {
				_stprintf(p, _T("%s,"), name);
				p += _tcslen(p);
			}

			if (!(dp & 0x04)) {
				if (dr[0]) {
					_stprintf(p, _T("%s%s,"), dr, mult);
					p += _tcslen(p);
				}
			}

			if (p[-1] == ',')
				p--;
			_stprintf(p, _T("],"));
			p += _tcslen(p);

			if ((dp & 0x04)) {
				if (dr[0]) {
					_stprintf(p, _T("%s%s,"), dr, mult);
					p += _tcslen(p);
				}
			}

		}

		if ((dp & 0x03) == 0x02) {
			outer = (uae_s32)(uae_s16)get_iword_debug(pc);
			_stprintf(p, _T("$%04x,"), (uae_s16)outer);
			p += _tcslen(p);
			pc += 2;
		} else 	if ((dp & 0x03) == 0x03) {
			outer = get_ilong_debug(pc);
			_stprintf(p, _T("$%08x,"), outer);
			p += _tcslen(p);
			pc += 4;
		}

		if (p[-1] == ',')
			p--;
		_stprintf(p, _T(")"));
		p += _tcslen(p);

		if ((dp & 0x4) == 0)
			base += dispreg;
		if (dp & 0x3)
			base = get_long_debug(base);
		if (dp & 0x4)
			base += dispreg;

		addr = base + outer;

		_stprintf(p, _T(" == $%08x"), addr);
		p += _tcslen(p);

	} else {
		// Brief format extension
		TCHAR regstr[20];
		uae_s8 disp8 = dp & 0xFF;

		regstr[0] = 0;
		_stprintf(regstr, _T(",%c%d.%c"), dp & 0x8000 ? 'A' : 'D', (int)r, dp & 0x800 ? 'L' : 'W');
		addr = base + (uae_s32)((uae_s8)disp8) + dispreg;
		_stprintf(buffer, _T("(%s%s%s,$%02x) == $%08x"), name, regstr, mult, disp8, addr);
		if (dp & 0x100) {
			_tcscat(buffer, _T(" (68020+)"));
		}
	}

	*pcp = pc;
	return addr;
}

uaecptr ShowEA (void *f, uaecptr pc, uae_u16 opcode, int reg, amodes mode, wordsizes size, TCHAR *buf, uae_u32 *eaddr, int safemode)
{
	uaecptr addr = pc;
	uae_s16 disp16;
	uae_s32 offset = 0;
	TCHAR buffer[80];

	switch (mode){
	case Dreg:
		_stprintf (buffer, _T("D%d"), reg);
		break;
	case Areg:
		_stprintf (buffer, _T("A%d"), reg);
		break;
	case Aind:
		_stprintf (buffer, _T("(A%d)"), reg);
		addr = regs.regs[reg + 8];
		showea_val(buffer, opcode, addr, size);
		break;
	case Aipi:
		_stprintf (buffer, _T("(A%d)+"), reg);
		addr = regs.regs[reg + 8];
		showea_val(buffer, opcode, addr, size);
		break;
	case Apdi:
		_stprintf (buffer, _T("-(A%d)"), reg);
		addr = regs.regs[reg + 8];
		showea_val(buffer, opcode, addr - datasizes[size], size);
		break;
	case Ad16:
		{
			TCHAR offtxt[8];
			disp16 = get_iword_debug (pc); pc += 2;
			if (disp16 < 0)
				_stprintf (offtxt, _T("-$%04x"), -disp16);
			else
				_stprintf (offtxt, _T("$%04x"), disp16);
			addr = m68k_areg (regs, reg) + disp16;
			_stprintf (buffer, _T("(A%d,%s) == $%08x"), reg, offtxt, addr);
			showea_val(buffer, opcode, addr, size);
		}
		break;
	case Ad8r:
		{
			TCHAR name[10];
			_stprintf(name, _T("A%d"), reg);
			addr = ShowEA_disp(&pc, m68k_areg(regs, reg), buffer, name);
			showea_val(buffer, opcode, addr, size);
		}
		break;
	case PC16:
		disp16 = get_iword_debug (pc); pc += 2;
		addr += (uae_s16)disp16;
		_stprintf (buffer, _T("(PC,$%04x) == $%08x"), disp16 & 0xffff, addr);
		showea_val(buffer, opcode, addr, size);
		break;
	case PC8r:
		{
			addr = ShowEA_disp(&pc, addr, buffer, _T("PC"));
			showea_val(buffer, opcode, addr, size);
		}
		break;
	case absw:
		addr = (uae_s32)(uae_s16)get_iword_debug (pc);
		_stprintf (buffer, _T("$%04x"), (uae_u16)addr);
		pc += 2;
		showea_val(buffer, opcode, addr, size);
		break;
	case absl:
		addr = get_ilong_debug (pc);
		_stprintf (buffer, _T("$%08x"), addr);
		pc += 4;
		showea_val(buffer, opcode, addr, size);
		break;
	case imm:
		switch (size){
		case sz_byte:
			_stprintf (buffer, _T("#$%02x"), (get_iword_debug (pc) & 0xff));
			pc += 2;
			break;
		case sz_word:
			_stprintf (buffer, _T("#$%04x"), (get_iword_debug (pc) & 0xffff));
			pc += 2;
			break;
		case sz_long:
			_stprintf(buffer, _T("#$%08x"), (get_ilong_debug(pc)));
			pc += 4;
			break;
		case sz_single:
			{
				fpdata fp;
				fpp_to_single(&fp, get_ilong_debug(pc));
				_stprintf(buffer, _T("#%s"), fpp_print(&fp, 0));
				pc += 4;
			}
			break;
		case sz_double:
			{
				fpdata fp;
				fpp_to_double(&fp, get_ilong_debug(pc), get_ilong_debug(pc + 4));
				_stprintf(buffer, _T("#%s"), fpp_print(&fp, 0));
				pc += 8;
			}
			break;
		case sz_extended:
		{
			fpdata fp;
			fpp_to_exten(&fp, get_ilong_debug(pc), get_ilong_debug(pc + 4), get_ilong_debug(pc + 8));
			_stprintf(buffer, _T("#%s"), fpp_print(&fp, 0));
			pc += 12;
			break;
		}
		case sz_packed:
			_stprintf(buffer, _T("#$%08x%08x%08x"), get_ilong_debug(pc), get_ilong_debug(pc + 4), get_ilong_debug(pc + 8));
			pc += 12;
			break;
		default:
			break;
		}
		break;
	case imm0:
		offset = (uae_s32)(uae_s8)get_iword_debug (pc);
		_stprintf (buffer, _T("#$%02x"), (uae_u32)(offset & 0xff));
		addr = pc + 2 + offset;
		if ((opcode & 0xf000) == 0x6000) {
			showea_val(buffer, opcode, addr, 1);
		}
		pc += 2;
		break;
	case imm1:
		offset = (uae_s32)(uae_s16)get_iword_debug (pc);
		buffer[0] = 0;
		_stprintf (buffer, _T("#$%04x"), (uae_u32)(offset & 0xffff));
		addr = pc + offset;
		if ((opcode & 0xf000) == 0x6000) {
			showea_val(buffer, opcode, addr, 2);
		}
		pc += 2;
		break;
	case imm2:
		offset = (uae_s32)get_ilong_debug (pc);
		_stprintf (buffer, _T("#$%08x"), (uae_u32)offset);
		addr = pc + offset;
		if ((opcode & 0xf000) == 0x6000) {
			showea_val(buffer, opcode, addr, 4);
		}
		pc += 4;
		break;
	case immi:
		offset = (uae_s32)(uae_s8)(reg & 0xff);
		_stprintf (buffer, _T("#$%02x"), (uae_u8)offset);
		addr = pc + offset;
		break;
	default:
		break;
	}
	if (buf == NULL)
		f_out (f, _T("%s"), buffer);
	else
		_tcscat (buf, buffer);
	if (eaddr)
		*eaddr = addr;
	return pc;
}

#if 0
/* The plan is that this will take over the job of exception 3 handling -
* the CPU emulation functions will just do a longjmp to m68k_go whenever
* they hit an odd address. */
static int verify_ea (int reg, amodes mode, wordsizes size, uae_u32 *val)
{
	uae_u16 dp;
	uae_s8 disp8;
	uae_s16 disp16;
	int r;
	uae_u32 dispreg;
	uaecptr addr;
	uae_s32 offset = 0;

	switch (mode){
	case Dreg:
		*val = m68k_dreg (regs, reg);
		return 1;
	case Areg:
		*val = m68k_areg (regs, reg);
		return 1;

	case Aind:
	case Aipi:
		addr = m68k_areg (regs, reg);
		break;
	case Apdi:
		addr = m68k_areg (regs, reg);
		break;
	case Ad16:
		disp16 = get_iword_1 (m68kpc_offset); m68kpc_offset += 2;
		addr = m68k_areg (regs, reg) + (uae_s16)disp16;
		break;
	case Ad8r:
		addr = m68k_areg (regs, reg);
d8r_common:
		dp = get_iword_1 (m68kpc_offset); m68kpc_offset += 2;
		disp8 = dp & 0xFF;
		r = (dp & 0x7000) >> 12;
		dispreg = dp & 0x8000 ? m68k_areg (regs, r) : m68k_dreg (regs, r);
		if (!(dp & 0x800)) dispreg = (uae_s32)(uae_s16)(dispreg);
		dispreg <<= (dp >> 9) & 3;

		if (dp & 0x100) {
			uae_s32 outer = 0, disp = 0;
			uae_s32 base = addr;
			if (dp & 0x80) base = 0;
			if (dp & 0x40) dispreg = 0;
			if ((dp & 0x30) == 0x20) { disp = (uae_s32)(uae_s16)get_iword_1 (m68kpc_offset); m68kpc_offset += 2; }
			if ((dp & 0x30) == 0x30) { disp = get_ilong_1 (m68kpc_offset); m68kpc_offset += 4; }
			base += disp;

			if ((dp & 0x3) == 0x2) { outer = (uae_s32)(uae_s16)get_iword_1 (m68kpc_offset); m68kpc_offset += 2; }
			if ((dp & 0x3) == 0x3) { outer = get_ilong_1 (m68kpc_offset); m68kpc_offset += 4; }

			if (!(dp & 4)) base += dispreg;
			if (dp & 3) base = get_long (base);
			if (dp & 4) base += dispreg;

			addr = base + outer;
		} else {
			addr += (uae_s32)((uae_s8)disp8) + dispreg;
		}
		break;
	case PC16:
		addr = m68k_getpc () + m68kpc_offset;
		disp16 = get_iword_1 (m68kpc_offset); m68kpc_offset += 2;
		addr += (uae_s16)disp16;
		break;
	case PC8r:
		addr = m68k_getpc () + m68kpc_offset;
		goto d8r_common;
	case absw:
		addr = (uae_s32)(uae_s16)get_iword_1 (m68kpc_offset);
		m68kpc_offset += 2;
		break;
	case absl:
		addr = get_ilong_1 (m68kpc_offset);
		m68kpc_offset += 4;
		break;
	case imm:
		switch (size){
		case sz_byte:
			*val = get_iword_1 (m68kpc_offset) & 0xff;
			m68kpc_offset += 2;
			break;
		case sz_word:
			*val = get_iword_1 (m68kpc_offset) & 0xffff;
			m68kpc_offset += 2;
			break;
		case sz_long:
			*val = get_ilong_1 (m68kpc_offset);
			m68kpc_offset += 4;
			break;
		default:
			break;
		}
		return 1;
	case imm0:
		*val = (uae_s32)(uae_s8)get_iword_1 (m68kpc_offset);
		m68kpc_offset += 2;
		return 1;
	case imm1:
		*val = (uae_s32)(uae_s16)get_iword_1 (m68kpc_offset);
		m68kpc_offset += 2;
		return 1;
	case imm2:
		*val = get_ilong_1 (m68kpc_offset);
		m68kpc_offset += 4;
		return 1;
	case immi:
		*val = (uae_s32)(uae_s8)(reg & 0xff);
		return 1;
	default:
		addr = 0;
		break;
	}
	if ((addr & 1) == 0)
		return 1;

	last_addr_for_exception_3 = m68k_getpc () + m68kpc_offset;
	last_fault_for_exception_3 = addr;
	last_writeaccess_for_exception_3 = 0;
	last_instructionaccess_for_exception_3 = 0;
	return 0;
}
#endif

int get_cpu_model (void)
{
	return currprefs.cpu_model;
}


STATIC_INLINE int in_rom (uaecptr pc)
{
	return (munge24 (pc) & 0xFFF80000) == 0xF80000;
}

STATIC_INLINE int in_rtarea (uaecptr pc)
{
	return (munge24 (pc) & 0xFFFF0000) == rtarea_base && (uae_boot_rom_type || currprefs.uaeboard > 0);
}

STATIC_INLINE void wait_memory_cycles (void)
{
	if (regs.memory_waitstate_cycles) {
		x_do_cycles(regs.memory_waitstate_cycles);
		regs.memory_waitstate_cycles = 0;
	}
	if (regs.ce020extracycles >= 16) {
		regs.ce020extracycles = 0;
		x_do_cycles(4 * CYCLE_UNIT);
	}
}

STATIC_INLINE int adjust_cycles (int cycles)
{
	int mc = regs.memory_waitstate_cycles;
	regs.memory_waitstate_cycles = 0;
	if (currprefs.m68k_speed < 0 || cycles_mult == 0)
		return cycles + mc;
	cycles *= cycles_mult;
	cycles /= CYCLES_DIV;
	return cycles + mc;
}

void m68k_cancel_idle(void)
{
	cpu_last_stop_vpos = -1;
}

static void m68k_set_stop(void)
{
	if (regs.stopped)
		return;
	regs.stopped = 1;
	set_special(SPCFLAG_STOP);
	if (cpu_last_stop_vpos >= 0) {
		cpu_last_stop_vpos = vpos;
	}
}

static void m68k_unset_stop(void)
{
	regs.stopped = 0;
	unset_special(SPCFLAG_STOP);
	if (cpu_last_stop_vpos >= 0) {
		cpu_stopped_lines += vpos - cpu_last_stop_vpos;
		cpu_last_stop_vpos = vpos;
	}
}

static void activate_trace(void)
{
	unset_special (SPCFLAG_TRACE);
	set_special (SPCFLAG_DOTRACE);
}

// make sure interrupt is checked immediately after current instruction
static void doint_imm(void)
{
	doint();
	if (!currprefs.cachesize && !(regs.spcflags & SPCFLAG_INT) && (regs.spcflags & SPCFLAG_DOINT))
		set_special(SPCFLAG_INT);
}

void REGPARAM2 MakeSR (void)
{
	regs.sr = ((regs.t1 << 15) | (regs.t0 << 14)
		| (regs.s << 13) | (regs.m << 12) | (regs.intmask << 8)
		| (GET_XFLG () << 4) | (GET_NFLG () << 3)
		| (GET_ZFLG () << 2) | (GET_VFLG () << 1)
		|  GET_CFLG ());
}

static void SetSR (uae_u16 sr)
{
	regs.sr &= 0xff00;
	regs.sr |= sr;

	SET_XFLG ((regs.sr >> 4) & 1);
	SET_NFLG ((regs.sr >> 3) & 1);
	SET_ZFLG ((regs.sr >> 2) & 1);
	SET_VFLG ((regs.sr >> 1) & 1);
	SET_CFLG (regs.sr & 1);
}

static void MakeFromSR_x(int t0trace)
{
	int oldm = regs.m;
	int olds = regs.s;
	int oldt0 = regs.t0;
	int oldt1 = regs.t1;

	SET_XFLG ((regs.sr >> 4) & 1);
	SET_NFLG ((regs.sr >> 3) & 1);
	SET_ZFLG ((regs.sr >> 2) & 1);
	SET_VFLG ((regs.sr >> 1) & 1);
	SET_CFLG (regs.sr & 1);
	if (regs.t1 == ((regs.sr >> 15) & 1) &&
		regs.t0 == ((regs.sr >> 14) & 1) &&
		regs.s  == ((regs.sr >> 13) & 1) &&
		regs.m  == ((regs.sr >> 12) & 1) &&
		regs.intmask == ((regs.sr >> 8) & 7))
		return;
	regs.t1 = (regs.sr >> 15) & 1;
	regs.t0 = (regs.sr >> 14) & 1;
	regs.s  = (regs.sr >> 13) & 1;
	regs.m  = (regs.sr >> 12) & 1;
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
	if (currprefs.mmu_model)
		mmu_set_super (regs.s != 0);

	doint_imm();
	if (regs.t1 || regs.t0) {
		set_special (SPCFLAG_TRACE);
	} else {
		/* Keep SPCFLAG_DOTRACE, we still want a trace exception for
		SR-modifying instructions (including STOP).  */
		unset_special (SPCFLAG_TRACE);
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

static void exception_check_trace (int nr)
{
	unset_special (SPCFLAG_TRACE | SPCFLAG_DOTRACE);
	if (regs.t1 && !regs.t0) {
		/* trace stays pending if exception is div by zero, chk,
		* trapv or trap #x
		*/
		if (nr == 5 || nr == 6 || nr == 7 || (nr >= 32 && nr <= 47))
			set_special (SPCFLAG_DOTRACE);
	}
	regs.t1 = regs.t0 = 0;
}

static void exception_debug (int nr)
{
#ifdef DEBUGGER
	if (!exception_debugging)
		return;
	console_out_f (_T("Exception %d, PC=%08X\n"), nr, M68K_GETPC);
#endif
}

#ifdef CPUEMU_13

/* cycle-exact exception handler, 68000 only */

/*

Address/Bus Error:

- 8 idle cycles
- write PC low word
- write SR
- write PC high word
- write instruction word
- write fault address low word
- write status code
- write fault address high word
- 2 idle cycles
- read exception address high word
- read exception address low word
- prefetch
- 2 idle cycles
- prefetch

Division by Zero:

- 8 idle cycles
- write PC low word
- write SR
- write PC high word
- read exception address high word
- read exception address low word
- prefetch
- 2 idle cycles
- prefetch

Traps:

- 4 idle cycles
- write PC low word
- write SR
- write PC high word
- read exception address high word
- read exception address low word
- prefetch
- 2 idle cycles
- prefetch

TrapV:

(- normal prefetch done by TRAPV)
- write PC low word
- write SR
- write PC high word
- read exception address high word
- read exception address low word
- prefetch
- 2 idle cycles
- prefetch

CHK:

- 8 idle cycles
- write PC low word
- write SR
- write PC high word
- read exception address high word
- read exception address low word
- prefetch
- 2 idle cycles
- prefetch

Illegal Instruction:
Privilege violation:
Trace:
Line A:
Line F:

- 4 idle cycles
- write PC low word
- write SR
- write PC high word
- read exception address high word
- read exception address low word
- prefetch
- 2 idle cycles
- prefetch

Interrupt:

- 6 idle cycles
- write PC low word
- read exception number byte from (0xfffff1 | (interrupt number << 1))
- 4 idle cycles
- write SR
- write PC high word
- read exception address high word
- read exception address low word
- prefetch
- 2 idle cycles
- prefetch

*/

static int iack_cycle(int nr)
{
	int vector;

	if (1) {
		// non-autovectored
		vector = x_get_byte(0x00fffff1 | ((nr - 24) << 1));
		if (currprefs.cpu_cycle_exact)
			x_do_cycles(4 * cpucycleunit);
	} else {
		// autovectored

	}
	return vector;
}

static void Exception_ce000 (int nr)
{
	uae_u32 currpc = m68k_getpc (), newpc;
	int sv = regs.s;
	int start, interrupt;
	int vector_nr = nr;

	start = 6;
	interrupt = nr >= 24 && nr < 24 + 8;
	if (!interrupt) {
		start = 8;
		if (nr == 7) // TRAPV
			start = 0;
		else if (nr >= 32 && nr < 32 + 16) // TRAP #x
			start = 4;
		else if (nr == 4 || nr == 8 || nr == 9 || nr == 10 || nr == 11) // ILLG, PRIV, TRACE, LINEA, LINEF
			start = 4;
	}

	if (start)
		x_do_cycles (start * cpucycleunit);

	exception_debug (nr);
	MakeSR ();

	if (!regs.s) {
		regs.usp = m68k_areg (regs, 7);
		m68k_areg (regs, 7) = regs.isp;
		regs.s = 1;
	}
	if (nr == 2 || nr == 3) { /* 2=bus error, 3=address error */
		if ((m68k_areg(regs, 7) & 1) || exception_in_exception < 0) {
			cpu_halt (CPU_HALT_DOUBLE_FAULT);
			return;
		}
		uae_u16 mode = (sv ? 4 : 0) | (last_instructionaccess_for_exception_3 ? 2 : 1);
		mode |= last_writeaccess_for_exception_3 ? 0 : 16;
		mode |= last_notinstruction_for_exception_3 ? 8 : 0;
		// undocumented bits seem to contain opcode
		mode |= last_op_for_exception_3 & ~31;
		m68k_areg (regs, 7) -= 14;
		exception_in_exception = -1;
		x_put_word (m68k_areg (regs, 7) + 12, last_addr_for_exception_3);
		x_put_word (m68k_areg (regs, 7) + 8, regs.sr);
		x_put_word (m68k_areg (regs, 7) + 10, last_addr_for_exception_3 >> 16);
		x_put_word (m68k_areg (regs, 7) + 6, last_op_for_exception_3);
		x_put_word (m68k_areg (regs, 7) + 4, last_fault_for_exception_3);
		x_put_word (m68k_areg (regs, 7) + 0, mode);
		x_put_word (m68k_areg (regs, 7) + 2, last_fault_for_exception_3 >> 16);
		x_do_cycles (2 * cpucycleunit);
		write_log (_T("Exception %d (%04x %x) at %x -> %x!\n"),
			nr, last_op_for_exception_3, last_addr_for_exception_3, currpc, get_long_debug (4 * nr));
		goto kludge_me_do;
	}
	if (currprefs.cpu_model == 68010) {
		// 68010 creates only format 0 and 8 stack frames
		m68k_areg (regs, 7) -= 8;
		if (m68k_areg(regs, 7) & 1) {
			exception3_notinstruction(regs.ir, m68k_areg(regs, 7) + 4);
			return;
		}
		exception_in_exception = 1;
		x_put_word (m68k_areg (regs, 7) + 4, currpc); // write low address
		if (interrupt)
			vector_nr = iack_cycle(nr);
		x_put_word (m68k_areg (regs, 7) + 0, regs.sr); // write SR
		x_put_word (m68k_areg (regs, 7) + 2, currpc >> 16); // write high address
		x_put_word (m68k_areg (regs, 7) + 6, vector_nr * 4);
	} else {
		m68k_areg (regs, 7) -= 6;
		if (m68k_areg(regs, 7) & 1) {
			exception3_notinstruction(regs.ir, m68k_areg(regs, 7) + 4);
			return;
		}
		exception_in_exception = 1;
		x_put_word (m68k_areg (regs, 7) + 4, currpc); // write low address
		if (interrupt)
			vector_nr = iack_cycle(nr);
		x_put_word (m68k_areg (regs, 7) + 0, regs.sr); // write SR
		x_put_word (m68k_areg (regs, 7) + 2, currpc >> 16); // write high address
	}
kludge_me_do:
	newpc = x_get_word (regs.vbr + 4 * vector_nr) << 16; // read high address
	newpc |= x_get_word (regs.vbr + 4 * vector_nr + 2); // read low address
	exception_in_exception = 0;
	if (newpc & 1) {
		if (nr == 2 || nr == 3)
			cpu_halt (CPU_HALT_DOUBLE_FAULT);
		else
			exception3_notinstruction(regs.ir, newpc);
		return;
	}
	m68k_setpc (newpc);
	if (interrupt)
		regs.intmask = nr - 24;
	branch_stack_push(currpc, currpc);
	regs.ir = x_get_word (m68k_getpc ()); // prefetch 1
	x_do_cycles (2 * cpucycleunit);
	regs.ipl_pin = intlev();
	ipl_fetch();
	regs.irc = x_get_word (m68k_getpc () + 2); // prefetch 2
#ifdef JIT
	set_special (SPCFLAG_END_COMPILE);
#endif
	exception_check_trace (nr);
}
#endif

static uae_u32 exception_pc (int nr)
{
	// bus error, address error, illegal instruction, privilege violation, a-line, f-line
	if (nr == 2 || nr == 3 || nr == 4 || nr == 8 || nr == 10 || nr == 11)
		return regs.instruction_pc;
	return m68k_getpc ();
}

static void Exception_build_stack_frame (uae_u32 oldpc, uae_u32 currpc, uae_u32 ssw, int nr, int format)
{
    int i;
   
#if 0
    if (nr < 24 || nr > 31) { // do not print debugging for interrupts
        write_log(_T("Building exception stack frame (format %X)\n"), format);
    }
#endif

	switch (format) {
        case 0x0: // four word stack frame
        case 0x1: // throwaway four word stack frame
            break;
        case 0x2: // six word stack frame
            m68k_areg (regs, 7) -= 4;
            x_put_long (m68k_areg (regs, 7), oldpc);
            break;
		case 0x3: // floating point post-instruction stack frame (68040)
			m68k_areg (regs, 7) -= 4;
			x_put_long (m68k_areg (regs, 7), regs.fp_ea);
			break;
		case 0x7: // access error stack frame (68040)

			for (i = 3; i >= 0; i--) {
				// WB1D/PD0,PD1,PD2,PD3
                m68k_areg (regs, 7) -= 4;
                x_put_long (m68k_areg (regs, 7), mmu040_move16[i]);
			}

            m68k_areg (regs, 7) -= 4;
            x_put_long (m68k_areg (regs, 7), 0); // WB1A
			m68k_areg (regs, 7) -= 4;
            x_put_long (m68k_areg (regs, 7), 0); // WB2D
            m68k_areg (regs, 7) -= 4;
            x_put_long (m68k_areg (regs, 7), regs.wb2_address); // WB2A
			m68k_areg (regs, 7) -= 4;
            x_put_long (m68k_areg (regs, 7), regs.wb3_data); // WB3D
            m68k_areg (regs, 7) -= 4;
            x_put_long (m68k_areg (regs, 7), regs.mmu_fault_addr); // WB3A

			m68k_areg (regs, 7) -= 4;
            x_put_long (m68k_areg (regs, 7), regs.mmu_fault_addr); // FA
            
			m68k_areg (regs, 7) -= 2;
            x_put_word (m68k_areg (regs, 7), 0);
            m68k_areg (regs, 7) -= 2;
            x_put_word (m68k_areg (regs, 7), regs.wb2_status);
            regs.wb2_status = 0;
            m68k_areg (regs, 7) -= 2;
            x_put_word (m68k_areg (regs, 7), regs.wb3_status);
            regs.wb3_status = 0;

			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), ssw);
            m68k_areg (regs, 7) -= 4;
            x_put_long (m68k_areg (regs, 7), regs.mmu_effective_addr);
            break;
        case 0x9: // coprocessor mid-instruction stack frame (68020, 68030)
            m68k_areg (regs, 7) -= 4;
            x_put_long (m68k_areg (regs, 7), regs.fp_ea);
            m68k_areg (regs, 7) -= 4;
			x_put_long (m68k_areg (regs, 7), regs.fp_opword);
			m68k_areg (regs, 7) -= 4;
            x_put_long (m68k_areg (regs, 7), oldpc);
            break;
        case 0x8: // bus and address error stack frame (68010)
            write_log(_T("Exception stack frame format %X not implemented\n"), format);
            return;
        case 0x4: // floating point unimplemented stack frame (68LC040, 68EC040)
			// or 68060 bus access fault stack frame
			m68k_areg (regs, 7) -= 4;
			x_put_long (m68k_areg (regs, 7), ssw);
			m68k_areg (regs, 7) -= 4;
			x_put_long (m68k_areg (regs, 7), oldpc);
			break;
		case 0xB: // long bus cycle fault stack frame (68020, 68030)
			// Store state information to internal register space
#if MMU030_DEBUG
			if (mmu030_idx >= MAX_MMU030_ACCESS) {
				write_log(_T("mmu030_idx out of bounds! %d >= %d\n"), mmu030_idx, MAX_MMU030_ACCESS);
			}
#endif
			if (!(ssw & MMU030_SSW_RW)) {
				mmu030_ad[mmu030_idx].val = regs.wb3_data;
			}
			for (i = 0; i < mmu030_idx + 1; i++) {
				m68k_areg (regs, 7) -= 4;
				x_put_long (m68k_areg (regs, 7), mmu030_ad[i].val);
			}
			while (i < MAX_MMU030_ACCESS) {
				uae_u32 v = 0;
				m68k_areg (regs, 7) -= 4;
				// mmu030_idx is always small enough if instruction is FMOVEM.
				if (mmu030_state[1] & MMU030_STATEFLAG1_FMOVEM) {
#if MMU030_DEBUG
					if (mmu030_idx >= MAX_MMU030_ACCESS - 2) {
						write_log(_T("mmu030_idx (FMOVEM) out of bounds! %d >= %d\n"), mmu030_idx, MAX_MMU030_ACCESS - 2);
					}
#endif
					if (i == MAX_MMU030_ACCESS - 2)
						v = mmu030_fmovem_store[0];
					else if (i == MAX_MMU030_ACCESS - 1)
						v = mmu030_fmovem_store[1];
				}
				x_put_long (m68k_areg (regs, 7), v);
				i++;
			}
			 // version & internal information (We store index here)
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), mmu030_idx);
			// 3* internal registers
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), mmu030_state[2]);
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg(regs, 7), regs.wb2_address); // = mmu030_state[1]
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), mmu030_state[0]);
			// data input buffer = fault address
			m68k_areg (regs, 7) -= 4;
			x_put_long (m68k_areg (regs, 7), regs.mmu_fault_addr);
			// 2xinternal
			{
				uae_u32 ps = (regs.prefetch020_valid[0] ? 1 : 0) | (regs.prefetch020_valid[1] ? 2 : 0) | (regs.prefetch020_valid[2] ? 4 : 0);
				ps |= ((regs.pipeline_r8[0] & 7) << 8);
				ps |= ((regs.pipeline_r8[1] & 7) << 11);
				ps |= ((regs.pipeline_pos & 15) << 16);
				ps |= ((regs.pipeline_stop & 15) << 20);
				if (mmu030_opcode == -1)
					ps |= 1 << 31;
				m68k_areg (regs, 7) -= 4;
				x_put_long (m68k_areg (regs, 7), ps);
			}
			// stage b address
			m68k_areg (regs, 7) -= 4;
			x_put_long (m68k_areg (regs, 7), mm030_stageb_address);
			// 2xinternal
			m68k_areg (regs, 7) -= 4;
			x_put_long (m68k_areg (regs, 7), mmu030_disp_store[1]);
		/* fall through */
		case 0xA:
			// short bus cycle fault stack frame (68020, 68030)
			// used when instruction's last write causes bus fault
			m68k_areg (regs, 7) -= 4;
			if (format == 0xb) {
				x_put_long(m68k_areg(regs, 7), mmu030_disp_store[0]);
			} else {
				uae_u32 ps = (regs.prefetch020_valid[0] ? 1 : 0) | (regs.prefetch020_valid[1] ? 2 : 0) | (regs.prefetch020_valid[2] ? 4 : 0);
				ps |= ((regs.pipeline_r8[0] & 7) << 8);
				ps |= ((regs.pipeline_r8[1] & 7) << 11);
				ps |= ((regs.pipeline_pos & 15) << 16);
				ps |= ((regs.pipeline_stop & 15) << 20);
				x_put_long(m68k_areg(regs, 7), ps);
			}
			m68k_areg (regs, 7) -= 4;
			// Data output buffer = value that was going to be written
			x_put_long (m68k_areg (regs, 7), regs.wb3_data);
			m68k_areg (regs, 7) -= 4;
			if (format == 0xb) {
				x_put_long(m68k_areg(regs, 7), (mmu030_opcode & 0xffff) | (regs.prefetch020[0] << 16));  // Internal register (opcode storage)
			} else {
				x_put_long(m68k_areg(regs, 7), regs.irc | (regs.prefetch020[0] << 16));  // Internal register (opcode storage)
			}
			m68k_areg (regs, 7) -= 4;
			x_put_long (m68k_areg (regs, 7), regs.mmu_fault_addr); // data cycle fault address
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), regs.prefetch020[2]);  // Instr. pipe stage B
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), regs.prefetch020[1]);  // Instr. pipe stage C
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), ssw);
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), regs.wb2_address); // = mmu030_state[1]);
			break;
		default:
            write_log(_T("Unknown exception stack frame format: %X\n"), format);
            return;
    }
    m68k_areg (regs, 7) -= 2;
    x_put_word (m68k_areg (regs, 7), (format << 12) | (nr * 4));
    m68k_areg (regs, 7) -= 4;
    x_put_long (m68k_areg (regs, 7), currpc);
    m68k_areg (regs, 7) -= 2;
    x_put_word (m68k_areg (regs, 7), regs.sr);
}

static void Exception_build_stack_frame_common (uae_u32 oldpc, uae_u32 currpc, uae_u32 ssw, int nr)
{
	if (nr == 5 || nr == 6 || nr == 7 || nr == 9) {
		Exception_build_stack_frame(oldpc, currpc, regs.mmu_ssw, nr, 0x2);
	} else if (nr == 60 || nr == 61) {
		Exception_build_stack_frame(oldpc, regs.instruction_pc, regs.mmu_ssw, nr, 0x0);
	} else if (nr >= 48 && nr <= 55) {
		if (regs.fpu_exp_pre) {
			if (currprefs.cpu_model == 68060 && nr == 55 && regs.fp_unimp_pend == 2) { // packed decimal real
				Exception_build_stack_frame(regs.fp_ea, regs.instruction_pc, 0, nr, 0x2);
			} else {
				Exception_build_stack_frame(oldpc, regs.instruction_pc, 0, nr, 0x0);
			}
		} else { /* post-instruction */
			if (currprefs.cpu_model == 68060 && nr == 55 && regs.fp_unimp_pend == 2) { // packed decimal real
				Exception_build_stack_frame(regs.fp_ea, currpc, 0, nr, 0x2);
			} else {
				Exception_build_stack_frame(oldpc, currpc, 0, nr, 0x3);
			}
		}
	} else if (nr == 11 && regs.fp_unimp_ins) {
		regs.fp_unimp_ins = false;
		if ((currprefs.cpu_model == 68060 && (currprefs.fpu_model == 0 || (regs.pcr & 2))) ||
			(currprefs.cpu_model == 68040 && currprefs.fpu_model == 0)) {
			Exception_build_stack_frame(regs.fp_ea, currpc, regs.instruction_pc, nr, 0x4);
		} else {
			Exception_build_stack_frame(regs.fp_ea, currpc, regs.mmu_ssw, nr, 0x2);
		}
	} else {
		Exception_build_stack_frame(oldpc, currpc, regs.mmu_ssw, nr, 0x0);
	}
}

// 68030 MMU
static void Exception_mmu030 (int nr, uaecptr oldpc)
{
    uae_u32 currpc = m68k_getpc (), newpc;
	int interrupt;

	interrupt = nr >= 24 && nr < 24 + 8;

    exception_debug (nr);
    MakeSR ();
    
    if (!regs.s) {
        regs.usp = m68k_areg (regs, 7);
        m68k_areg(regs, 7) = regs.m ? regs.msp : regs.isp;
        regs.s = 1;
        mmu_set_super (1);
    }
 
#if 0
    if (nr < 24 || nr > 31) { // do not print debugging for interrupts
        write_log (_T("Exception_mmu030: Exception %i: %08x %08x %08x\n"),
                   nr, currpc, oldpc, regs.mmu_fault_addr);
    }
#endif

    newpc = x_get_long (regs.vbr + 4 * nr);

#if 0
	write_log (_T("Exception %d -> %08x\n"), nr, newpc);
#endif

	if (regs.m && interrupt) { /* M + Interrupt */
        Exception_build_stack_frame (oldpc, currpc, regs.mmu_ssw, nr, 0x0);
		MakeSR ();
		regs.m = 0;
		regs.msp = m68k_areg (regs, 7);
		m68k_areg (regs, 7) = regs.isp;
        Exception_build_stack_frame (oldpc, currpc, regs.mmu_ssw, nr, 0x1);
    } else if (nr == 2) {
		if (1 && (mmu030_state[1] & MMU030_STATEFLAG1_LASTWRITE)) {
			Exception_build_stack_frame(oldpc, currpc, regs.mmu_ssw, nr, 0xA);
		} else {
			Exception_build_stack_frame(oldpc, currpc, regs.mmu_ssw, nr, 0xB);
		}
    } else if (nr == 3) {
		regs.mmu_fault_addr = last_fault_for_exception_3;
		mmu030_state[0] = mmu030_state[1] = 0;
		mmu030_data_buffer_out = 0;
        Exception_build_stack_frame (last_fault_for_exception_3, currpc, MMU030_SSW_RW | MMU030_SSW_SIZE_W | (regs.s ? 6 : 2), nr,  0xA);
	} else {
		Exception_build_stack_frame_common(oldpc, currpc, regs.mmu_ssw, nr);
	}

	if (newpc & 1) {
		if (nr == 2 || nr == 3)
			cpu_halt (CPU_HALT_DOUBLE_FAULT);
		else
			exception3_read(regs.ir, newpc);
		return;
	}
	if (interrupt)
		regs.intmask = nr - 24;
	m68k_setpci (newpc);
	fill_prefetch ();
	exception_check_trace (nr);
}

// 68040/060 MMU
static void Exception_mmu (int nr, uaecptr oldpc)
{
	uae_u32 currpc = m68k_getpc (), newpc;
	int interrupt;

	interrupt = nr >= 24 && nr < 24 + 8;

	// exception vector fetch and exception stack frame
	// operations don't allocate new cachelines
	cache_default_data |= CACHE_DISABLE_ALLOCATE;
	
	exception_debug (nr);
	MakeSR ();

	if (!regs.s) {
		regs.usp = m68k_areg (regs, 7);
		if (currprefs.cpu_model >= 68020 && currprefs.cpu_model < 68060) {
			m68k_areg (regs, 7) = regs.m ? regs.msp : regs.isp;
		} else {
			m68k_areg (regs, 7) = regs.isp;
		}
		regs.s = 1;
		mmu_set_super (1);
	}

	newpc = x_get_long (regs.vbr + 4 * nr);
#if 0
	write_log (_T("Exception %d: %08x -> %08x\n"), nr, currpc, newpc);
#endif

	if (nr == 2) { // bus error
        //write_log (_T("Exception_mmu %08x %08x %08x\n"), currpc, oldpc, regs.mmu_fault_addr);
        if (currprefs.mmu_model == 68040)
			Exception_build_stack_frame(oldpc, currpc, regs.mmu_ssw, nr, 0x7);
		else
			Exception_build_stack_frame(regs.mmu_fault_addr, currpc, regs.mmu_fslw, nr, 0x4);
	} else if (nr == 3) { // address error
        Exception_build_stack_frame(last_fault_for_exception_3, currpc, 0, nr, 0x2);
		write_log (_T("Exception %d (%x) at %x -> %x!\n"), nr, last_fault_for_exception_3, currpc, get_long_debug (regs.vbr + 4 * nr));
	} else if (regs.m && interrupt) { /* M + Interrupt */
		Exception_build_stack_frame(oldpc, currpc, regs.mmu_ssw, nr, 0x0);
		MakeSR();
		regs.m = 0;
		if (currprefs.cpu_model < 68060) {
			regs.msp = m68k_areg(regs, 7);
			Exception_build_stack_frame(oldpc, currpc, regs.mmu_ssw, nr, 0x1);
		}
	} else {
		Exception_build_stack_frame_common(oldpc, currpc, regs.mmu_ssw, nr);
	}
    
	if (newpc & 1) {
		if (nr == 2 || nr == 3)
			cpu_halt (CPU_HALT_DOUBLE_FAULT);
		else
			exception3_read(regs.ir, newpc);
		return;
	}

	cache_default_data &= ~CACHE_DISABLE_ALLOCATE;

	m68k_setpci (newpc);
	if (interrupt)
		regs.intmask = nr - 24;
	fill_prefetch ();
	exception_check_trace (nr);
}

static void add_approximate_exception_cycles(int nr)
{
	int cycles;

	if (currprefs.cpu_model > 68000)
		return;
	if (nr >= 24 && nr <= 31) {
		/* Interrupts */
		cycles = 44 + 4; 
	} else if (nr >= 32 && nr <= 47) {
		/* Trap (total is 34, but cpuemux.c already adds 4) */ 
		cycles = 34 -4;
	} else {
		switch (nr)
		{
			case 2: cycles = 50; break;		/* Bus error */
			case 3: cycles = 50; break;		/* Address error */
			case 4: cycles = 34; break;		/* Illegal instruction */
			case 5: cycles = 38; break;		/* Division by zero */
			case 6: cycles = 40; break;		/* CHK */
			case 7: cycles = 34; break;		/* TRAPV */
			case 8: cycles = 34; break;		/* Privilege violation */
			case 9: cycles = 34; break;		/* Trace */
			case 10: cycles = 34; break;	/* Line-A */
			case 11: cycles = 34; break;	/* Line-F */
			default:
			cycles = 4;
			break;
		}
	}
	cycles = adjust_cycles(cycles * CYCLE_UNIT / 2);
	x_do_cycles(cycles);
}

static void Exception_normal (int nr)
{
	uae_u32 newpc;
	uae_u32 currpc = m68k_getpc();
	uae_u32 nextpc;
	int sv = regs.s;
	int interrupt;
	int vector_nr = nr;

	cache_default_data |= CACHE_DISABLE_ALLOCATE;

	interrupt = nr >= 24 && nr < 24 + 8;

	if (interrupt && currprefs.cpu_model <= 68010)
		vector_nr = iack_cycle(nr);

	exception_debug (nr);
	MakeSR ();

	if (!regs.s) {
		regs.usp = m68k_areg (regs, 7);
		if (currprefs.cpu_model >= 68020 && currprefs.cpu_model < 68060) {
			m68k_areg (regs, 7) = regs.m ? regs.msp : regs.isp;
		} else {
			m68k_areg (regs, 7) = regs.isp;
		}
		regs.s = 1;
		if (currprefs.mmu_model)
			mmu_set_super (regs.s != 0);
	}

	if ((m68k_areg(regs, 7) & 1) && currprefs.cpu_model < 68020) {
		if (nr == 2 || nr == 3)
			cpu_halt (CPU_HALT_DOUBLE_FAULT);
		else
			exception3_notinstruction(regs.ir, m68k_areg(regs, 7));
		return;
	}
	if ((nr == 2 || nr == 3) && exception_in_exception < 0) {
		cpu_halt (CPU_HALT_DOUBLE_FAULT);
		return;
	}

	if (!currprefs.cpu_compatible) {
		addrbank *ab = &get_mem_bank(m68k_areg(regs, 7) - 4);
		// Not plain RAM check because some CPU type tests that
		// don't need to return set stack to ROM..
		if (!ab || ab == &dummy_bank || (ab->flags & ABFLAG_IO)) {
			cpu_halt(CPU_HALT_SSP_IN_NON_EXISTING_ADDRESS);
			return;
		}
	}
	
	bool used_exception_build_stack_frame = false;

	if (currprefs.cpu_model > 68000) {
		uae_u32 oldpc = regs.instruction_pc;
		nextpc = exception_pc (nr);
		if (nr == 2 || nr == 3) {
			int i;
			if (currprefs.cpu_model >= 68040) {
				if (nr == 2) {
					if (currprefs.mmu_model) {
						// 68040 mmu bus error
						for (i = 0 ; i < 7 ; i++) {
							m68k_areg (regs, 7) -= 4;
							x_put_long (m68k_areg (regs, 7), 0);
						}
						m68k_areg (regs, 7) -= 4;
						x_put_long (m68k_areg (regs, 7), regs.wb3_data);
						m68k_areg (regs, 7) -= 4;
						x_put_long (m68k_areg (regs, 7), regs.mmu_fault_addr);
						m68k_areg (regs, 7) -= 4;
						x_put_long (m68k_areg (regs, 7), regs.mmu_fault_addr);
						m68k_areg (regs, 7) -= 2;
						x_put_word (m68k_areg (regs, 7), 0);
						m68k_areg (regs, 7) -= 2;
						x_put_word (m68k_areg (regs, 7), 0);
						m68k_areg (regs, 7) -= 2;
						x_put_word (m68k_areg (regs, 7), regs.wb3_status);
						regs.wb3_status = 0;
						m68k_areg (regs, 7) -= 2;
						x_put_word (m68k_areg (regs, 7), regs.mmu_ssw);
						m68k_areg (regs, 7) -= 4;
						x_put_long (m68k_areg (regs, 7), regs.mmu_fault_addr);

						m68k_areg (regs, 7) -= 2;
						x_put_word (m68k_areg (regs, 7), 0x7000 + vector_nr * 4);
						m68k_areg (regs, 7) -= 4;
						x_put_long (m68k_areg (regs, 7), regs.instruction_pc);
						m68k_areg (regs, 7) -= 2;
						x_put_word (m68k_areg (regs, 7), regs.sr);
						newpc = x_get_long (regs.vbr + 4 * vector_nr);
						if (newpc & 1) {
							if (nr == 2 || nr == 3)
								cpu_halt (CPU_HALT_DOUBLE_FAULT);
							else
								exception3_read(regs.ir, newpc);
							return;
						}
						m68k_setpc (newpc);
#ifdef JIT
						set_special (SPCFLAG_END_COMPILE);
#endif
						exception_check_trace (nr);
						return;

					} else {

						// 68040 bus error (not really, some garbage?)
						for (i = 0 ; i < 18 ; i++) {
							m68k_areg (regs, 7) -= 2;
							x_put_word (m68k_areg (regs, 7), 0);
						}
						m68k_areg (regs, 7) -= 4;
						x_put_long (m68k_areg (regs, 7), last_fault_for_exception_3);
						m68k_areg (regs, 7) -= 2;
						x_put_word (m68k_areg (regs, 7), 0);
						m68k_areg (regs, 7) -= 2;
						x_put_word (m68k_areg (regs, 7), 0);
						m68k_areg (regs, 7) -= 2;
						x_put_word (m68k_areg (regs, 7), 0);
						m68k_areg (regs, 7) -= 2;
						x_put_word (m68k_areg (regs, 7), 0x0140 | (sv ? 6 : 2)); /* SSW */
						m68k_areg (regs, 7) -= 4;
						x_put_long (m68k_areg (regs, 7), last_addr_for_exception_3);
						m68k_areg (regs, 7) -= 2;
						x_put_word (m68k_areg (regs, 7), 0x7000 + vector_nr * 4);
						m68k_areg (regs, 7) -= 4;
						x_put_long (m68k_areg (regs, 7), regs.instruction_pc);
						m68k_areg (regs, 7) -= 2;
						x_put_word (m68k_areg (regs, 7), regs.sr);
						goto kludge_me_do;

					}

				} else {
					m68k_areg (regs, 7) -= 4;
					x_put_long (m68k_areg (regs, 7), last_fault_for_exception_3);
					m68k_areg (regs, 7) -= 2;
					x_put_word (m68k_areg (regs, 7), 0x2000 + vector_nr * 4);
				}
			} else {
				// 68020 address error
				uae_u16 ssw = (sv ? 4 : 0) | (last_instructionaccess_for_exception_3 ? 2 : 1);
				ssw |= last_writeaccess_for_exception_3 ? 0 : 0x40;
				ssw |= 0x20;
				for (i = 0 ; i < 36; i++) {
					m68k_areg (regs, 7) -= 2;
					x_put_word (m68k_areg (regs, 7), 0);
				}
				m68k_areg (regs, 7) -= 4;
				x_put_long (m68k_areg (regs, 7), last_fault_for_exception_3);
				m68k_areg (regs, 7) -= 2;
				x_put_word (m68k_areg (regs, 7), 0);
				m68k_areg (regs, 7) -= 2;
				x_put_word (m68k_areg (regs, 7), 0);
				m68k_areg (regs, 7) -= 2;
				x_put_word (m68k_areg (regs, 7), 0);
				m68k_areg (regs, 7) -= 2;
				x_put_word (m68k_areg (regs, 7), ssw);
				m68k_areg (regs, 7) -= 2;
				x_put_word (m68k_areg (regs, 7), 0xb000 + vector_nr * 4);
			}
			write_log (_T("Exception %d (%x) at %x -> %x!\n"), nr, regs.instruction_pc, currpc, get_long_debug (regs.vbr + 4 * vector_nr));
		} else if (regs.m && interrupt) { /* M + Interrupt */
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), vector_nr * 4);
			if (currprefs.cpu_model < 68060) {
				m68k_areg (regs, 7) -= 4;
				x_put_long (m68k_areg (regs, 7), currpc);
				m68k_areg (regs, 7) -= 2;
				x_put_word (m68k_areg (regs, 7), regs.sr);
				regs.sr |= (1 << 13);
				regs.msp = m68k_areg(regs, 7);
				regs.m = 0;
				m68k_areg(regs, 7) = regs.isp;
				m68k_areg (regs, 7) -= 2;
				x_put_word (m68k_areg (regs, 7), 0x1000 + vector_nr * 4);
			}
		} else {
			Exception_build_stack_frame_common(oldpc, currpc, regs.mmu_ssw, nr);
			used_exception_build_stack_frame = true;
		}
 	} else {
		add_approximate_exception_cycles(nr);
		nextpc = m68k_getpc ();
		if (nr == 2 || nr == 3) {
			// 68000 address error
			uae_u16 mode = (sv ? 4 : 0) | (last_instructionaccess_for_exception_3 ? 2 : 1);
			mode |= last_writeaccess_for_exception_3 ? 0 : 16;
			mode |= last_notinstruction_for_exception_3 ? 8 : 0;
			// undocumented bits seem to contain opcode
			mode |= last_op_for_exception_3 & ~31;
			m68k_areg (regs, 7) -= 14;
			exception_in_exception = -1;
			x_put_word (m68k_areg (regs, 7) + 0, mode);
			x_put_long (m68k_areg (regs, 7) + 2, last_fault_for_exception_3);
			x_put_word (m68k_areg (regs, 7) + 6, last_op_for_exception_3);
			x_put_word (m68k_areg (regs, 7) + 8, regs.sr);
			x_put_long (m68k_areg (regs, 7) + 10, last_addr_for_exception_3);
			write_log (_T("Exception %d (%x) at %x -> %x!\n"), nr, last_fault_for_exception_3, currpc, get_long_debug (regs.vbr + 4 * vector_nr));
			goto kludge_me_do;
		}
	}
	if (!used_exception_build_stack_frame) {
		m68k_areg (regs, 7) -= 4;
		x_put_long (m68k_areg (regs, 7), nextpc);
		m68k_areg (regs, 7) -= 2;
		x_put_word (m68k_areg (regs, 7), regs.sr);
	}
	if (currprefs.cpu_model == 68060 && interrupt) {
		regs.m = 0;
	}
kludge_me_do:
	newpc = x_get_long (regs.vbr + 4 * vector_nr);
	exception_in_exception = 0;
	if (newpc & 1) {
		if (nr == 2 || nr == 3)
			cpu_halt (CPU_HALT_DOUBLE_FAULT);
		else
			exception3_notinstruction(regs.ir, newpc);
		return;
	}
	if (interrupt)
		regs.intmask = nr - 24;
	m68k_setpc (newpc);
	cache_default_data &= ~CACHE_DISABLE_ALLOCATE;
#ifdef JIT
	set_special (SPCFLAG_END_COMPILE);
#endif
	branch_stack_push(currpc, nextpc);
	fill_prefetch ();
	exception_check_trace (nr);
}

// address = format $2 stack frame address field
static void ExceptionX (int nr, uaecptr address)
{
	uaecptr pc = m68k_getpc();
	regs.exception = nr;
	if (cpu_tracer) {
		cputrace.state = nr;
	}
	if (!regs.s) {
		regs.instruction_pc_user_exception = pc;
	}

#ifdef JIT
	if (currprefs.cachesize)
		regs.instruction_pc = address == -1 ? pc : address;
#endif

	if (debug_illegal && !in_rom(pc)) {
		if (nr <= 63 && (debug_illegal_mask & ((uae_u64)1 << nr))) {
			write_log(_T("Exception %d breakpoint\n"), nr);
			activate_debugger();
		}
	}

#ifdef CPUEMU_13
	if (currprefs.cpu_cycle_exact && currprefs.cpu_model <= 68010)
		Exception_ce000 (nr);
	else
#endif
		if (currprefs.mmu_model) {
			if (currprefs.cpu_model == 68030)
				Exception_mmu030 (nr, m68k_getpc ());
			else
				Exception_mmu (nr, m68k_getpc ());
		} else {
			Exception_normal (nr);
		}

	regs.exception = 0;
	if (cpu_tracer) {
		cputrace.state = 0;
	}
}

void REGPARAM2 Exception_cpu(int nr)
{
	bool t0 = currprefs.cpu_model >= 68020 && regs.t0;
	ExceptionX (nr, -1);
	// check T0 trace
	if (t0) {
		activate_trace();
	}
}
void REGPARAM2 Exception (int nr)
{
	ExceptionX (nr, -1);
}
void REGPARAM2 ExceptionL (int nr, uaecptr address)
{
	ExceptionX (nr, address);
}

static void bus_error(void)
{
	TRY (prb2) {
		Exception (2);
	} CATCH (prb2) {
		cpu_halt (CPU_HALT_BUS_ERROR_DOUBLE_FAULT);
	} ENDTRY
}

static void do_interrupt (int nr)
{
	if (debug_dma)
		record_dma_event (DMA_EVENT_CPUIRQ, current_hpos (), vpos);

	if (inputrecord_debug & 2) {
		if (input_record > 0)
			inprec_recorddebug_cpu (2);
		else if (input_play > 0)
			inprec_playdebug_cpu (2);
	}

	m68k_unset_stop();
	assert (nr < 8 && nr >= 0);

	for (;;) {
		Exception (nr + 24);
		if (!currprefs.cpu_compatible || currprefs.cpu_model == 68060)
			break;
		if (m68k_interrupt_delay)
			nr = regs.ipl;
		else
			nr = intlev();
		if (nr <= 0 || regs.intmask >= nr)
			break;
	}

	doint ();
}

void NMI (void)
{
	do_interrupt (7);
}

static void m68k_reset_sr(void)
{
	SET_XFLG ((regs.sr >> 4) & 1);
	SET_NFLG ((regs.sr >> 3) & 1);
	SET_ZFLG ((regs.sr >> 2) & 1);
	SET_VFLG ((regs.sr >> 1) & 1);
	SET_CFLG (regs.sr & 1);
	regs.t1 = (regs.sr >> 15) & 1;
	regs.t0 = (regs.sr >> 14) & 1;
	regs.s  = (regs.sr >> 13) & 1;
	regs.m  = (regs.sr >> 12) & 1;
	regs.intmask = (regs.sr >> 8) & 7;
	/* set stack pointer */
	if (regs.s)
		m68k_areg (regs, 7) = regs.isp;
	else
		m68k_areg (regs, 7) = regs.usp;
}

static void m68k_reset2(bool hardreset)
{
	uae_u32 v;

	regs.halted = 0;
	gui_data.cpu_halted = 0;
	gui_led (LED_CPU, 0, -1);

	regs.spcflags = 0;
	m68k_reset_delay = 0;
	regs.ipl = regs.ipl_pin = 0;
	for (int i = 0; i < IRQ_SOURCE_MAX; i++) {
		uae_interrupts2[i] = 0;
		uae_interrupts6[i] = 0;
		uae_interrupt = 0;
	}

#ifdef SAVESTATE
	if (isrestore ()) {
		m68k_reset_sr();
		m68k_setpc_normal (regs.pc);
		return;
	} else {
		m68k_reset_delay = currprefs.reset_delay;
		set_special(SPCFLAG_CHECK);
	}
#endif
	regs.s = 1;
	if (currprefs.cpuboard_type) {
		uaecptr stack;
		v = cpuboard_get_reset_pc(&stack);
		m68k_areg (regs, 7) = stack;
	} else {
		v = get_long (4);
		m68k_areg (regs, 7) = get_long (0);
	}

	m68k_setpc_normal(v);
	regs.m = 0;
	regs.stopped = 0;
	regs.t1 = 0;
	regs.t0 = 0;
	SET_ZFLG (0);
	SET_XFLG (0);
	SET_CFLG (0);
	SET_VFLG (0);
	SET_NFLG (0);
	regs.intmask = 7;
	regs.vbr = regs.sfc = regs.dfc = 0;
	regs.irc = 0xffff;
#ifdef FPUEMU
	fpu_reset ();
#endif
	regs.caar = regs.cacr = 0;
	regs.itt0 = regs.itt1 = regs.dtt0 = regs.dtt1 = 0;
	regs.tcr = regs.mmusr = regs.urp = regs.srp = regs.buscr = 0;
	mmu_tt_modified (); 
	if (currprefs.cpu_model == 68020) {
		regs.cacr |= 8;
		set_cpu_caches (false);
	}

	mmufixup[0].reg = -1;
	mmufixup[1].reg = -1;
	mmu030_cache_state = CACHE_ENABLE_ALL;
	mmu_cache_state = CACHE_ENABLE_ALL;
	if (currprefs.cpu_model >= 68040) {
		set_cpu_caches(false);
	}
	if (currprefs.mmu_model >= 68040) {
		mmu_reset ();
		mmu_set_tc (regs.tcr);
		mmu_set_super (regs.s != 0);
	} else if (currprefs.mmu_model == 68030) {
		mmu030_reset (hardreset || regs.halted);
	} else {
		a3000_fakekick (0);
		/* only (E)nable bit is zeroed when CPU is reset, A3000 SuperKickstart expects this */
		fake_tc_030 &= ~0x80000000;
		fake_tt0_030 &= ~0x80000000;
		fake_tt1_030 &= ~0x80000000;
		if (hardreset || regs.halted) {
			fake_srp_030 = fake_crp_030 = 0;
			fake_tt0_030 = fake_tt1_030 = fake_tc_030 = 0;
		}
		fake_mmusr_030 = 0;
	}

	/* 68060 FPU is not compatible with 68040,
	* 68060 accelerators' boot ROM disables the FPU
	*/
	regs.pcr = 0;
	if (currprefs.cpu_model == 68060) {
		regs.pcr = currprefs.fpu_model == 68060 ? MC68060_PCR : MC68EC060_PCR;
		regs.pcr |= (currprefs.cpu060_revision & 0xff) << 8;
		if (currprefs.fpu_model == 0 || (currprefs.cpuboard_type == 0 && rtarea_base != 0xf00000)) {
			/* disable FPU if no accelerator board and no $f0 ROM */
			regs.pcr |= 2;
		}
	}
//	regs.ce020memcycles = 0;
	regs.ce020startcycle = regs.ce020endcycle = 0;

	fill_prefetch ();
}

void m68k_reset(void)
{
	m68k_reset2(false);
}

void cpu_change(int newmodel)
{
	if (newmodel == currprefs.cpu_model)
		return;
	fallback_new_cpu_model = newmodel;
	cpu_halt(CPU_HALT_ACCELERATOR_CPU_FALLBACK);
}

void cpu_fallback(int mode)
{
	int fallbackmodel;
	if (currprefs.chipset_mask & CSMASK_AGA) {
		fallbackmodel = 68020;
	} else {
		fallbackmodel = 68000;
	}
	if (mode < 0) {
		if (currprefs.cpu_model > fallbackmodel) {
			cpu_change(fallbackmodel);
		} else if (fallback_new_cpu_model) {
			cpu_change(fallback_new_cpu_model);
		}
	} else if (mode == 0) {
		cpu_change(fallbackmodel);
	} else if (mode) {
		if (fallback_cpu_model) {
			cpu_change(fallback_cpu_model);
		}
	}
}

static void cpu_do_fallback(void)
{
	bool fallbackmode = false;
	if ((fallback_new_cpu_model < 68020 && !(currprefs.chipset_mask & CSMASK_AGA)) || (fallback_new_cpu_model == 68020 && (currprefs.chipset_mask & CSMASK_AGA))) {
		// -> 68000/68010 or 68EC020
		fallback_cpu_model = currprefs.cpu_model;
		fallback_fpu_model = currprefs.fpu_model;
		fallback_mmu_model = currprefs.mmu_model;
		fallback_cpu_compatible = currprefs.cpu_compatible;
		fallback_cpu_address_space_24 = currprefs.address_space_24;
		changed_prefs.cpu_model = currprefs.cpu_model_fallback && fallback_new_cpu_model <= 68020 ? currprefs.cpu_model_fallback : fallback_new_cpu_model;
		changed_prefs.fpu_model = 0;
		changed_prefs.mmu_model = 0;
		changed_prefs.cpu_compatible = true;
		changed_prefs.address_space_24 = true;
		memcpy(&fallback_regs, &regs, sizeof(struct regstruct));
		fallback_regs.pc = M68K_GETPC;
		fallbackmode = true;
	} else {
		// -> 68020+
		changed_prefs.cpu_model = fallback_cpu_model;
		changed_prefs.fpu_model = fallback_fpu_model;
		changed_prefs.mmu_model = fallback_mmu_model;
		changed_prefs.cpu_compatible = fallback_cpu_compatible;
		changed_prefs.address_space_24 = fallback_cpu_address_space_24;
		fallback_cpu_model = 0;
	}
	init_m68k();
	build_cpufunctbl();
	m68k_reset2(false);
	if (!fallbackmode) {
		// restore original 68020+
		memcpy(&regs, &fallback_regs, sizeof(regs));
		restore_banks();
		memory_restore();
		memory_map_dump();
		m68k_setpc(fallback_regs.pc);
	} else {
		// 68000/010/EC020
		memory_restore();
		expansion_cpu_fallback();
		memory_map_dump();
	}
}

static void m68k_reset_restore(void)
{
	// hardreset and 68000/68020 fallback mode? Restore original mode.
	if (fallback_cpu_model) {
		fallback_new_cpu_model = fallback_cpu_model;
		fallback_regs.pc = 0;
		cpu_do_fallback();
	}
}

void REGPARAM2 op_unimpl (uae_u16 opcode)
{
	static int warned;
	if (warned < 20) {
		write_log (_T("68060 unimplemented opcode %04X, PC=%08x\n"), opcode, regs.instruction_pc);
		warned++;
	}
	ExceptionL (61, regs.instruction_pc);
}

uae_u32 REGPARAM2 op_illg (uae_u32 opcode)
{
	uaecptr pc = m68k_getpc ();
	static int warned;
	int inrom = in_rom (pc);
	int inrt = in_rtarea (pc);

	if ((opcode == 0x4afc || opcode == 0xfc4a) && !valid_address(pc, 4) && valid_address(pc - 4, 4)) {
		// PC fell off the end of RAM
		bus_error();
		return 4;
	}

	if (debugmem_illg(opcode)) {
		m68k_incpc_normal(2);
		return 4;
	}

	if (cloanto_rom && (opcode & 0xF100) == 0x7100) {
		m68k_dreg (regs, (opcode >> 9) & 7) = (uae_s8)(opcode & 0xFF);
		m68k_incpc_normal (2);
		fill_prefetch ();
		return 4;
	}

	if (opcode == 0x4E7B && inrom) {
		if (get_long (0x10) == 0) {
			notify_user (NUMSG_KS68020);
			uae_restart (-1, NULL);
			m68k_setstopped();
			return 4;
		}
	}

#ifdef AUTOCONFIG
	if (opcode == 0xFF0D && inrt) {
		/* User-mode STOP replacement */
		m68k_setstopped ();
		return 4;
	}

	if ((opcode & 0xF000) == 0xA000 && inrt) {
		/* Calltrap. */
		m68k_incpc_normal (2);
		m68k_handle_trap(opcode & 0xFFF);
		fill_prefetch ();
		return 4;
	}
#endif

	if ((opcode & 0xF000) == 0xF000) {
		if (warned < 20) {
			write_log(_T("B-Trap %04X at %08X -> %08X\n"), opcode, pc, get_long_debug(regs.vbr + 0x2c));
			warned++;
		}
		Exception (0xB);
		//activate_debugger_new();
		return 4;
	}
	if ((opcode & 0xF000) == 0xA000) {
		if (warned < 20) {
			write_log(_T("A-Trap %04X at %08X -> %08X\n"), opcode, pc, get_long_debug(regs.vbr + 0x28));
			warned++;
		}
		Exception (0xA);
		//activate_debugger_new();
		return 4;
	}
	if (warned < 20) {
		write_log (_T("Illegal instruction: %04x at %08X -> %08X\n"), opcode, pc, get_long_debug(regs.vbr + 0x10));
		warned++;
		//activate_debugger_new();
	}

	Exception (4);
	return 4;
}

#ifdef CPUEMU_0

static bool mmu_op30_invea(uae_u32 opcode)
{
	int eamode = (opcode >> 3) & 7;
	int rreg = opcode & 7;

	// Dn, An, (An)+, -(An), immediate and PC-relative not allowed
	if (eamode == 0 || eamode == 1 || eamode == 3 || eamode == 4 || (eamode == 7 && rreg > 1))
		return true;
	return false;
}

static bool mmu_op30fake_pmove (uaecptr pc, uae_u32 opcode, uae_u16 next, uaecptr extra)
{
	int preg = (next >> 10) & 31;
	int rw = (next >> 9) & 1;
	int fd = (next >> 8) & 1;
	int unused = (next & 0xff);
	const TCHAR *reg = NULL;
	uae_u32 otc = fake_tc_030;
	int siz;

	if (mmu_op30_invea(opcode))
		return true;
	// unused low 8 bits must be zeroed
	if (unused)
		return true;
	// read and fd set?
	if (rw && fd)
		return true;

	switch (preg)
	{
	case 0x10: // TC
		reg = _T("TC");
		siz = 4;
		if (rw)
			x_put_long (extra, fake_tc_030);
		else
			fake_tc_030 = x_get_long (extra);
		break;
	case 0x12: // SRP
		reg = _T("SRP");
		siz = 8;
		if (rw) {
			x_put_long (extra, fake_srp_030 >> 32);
			x_put_long (extra + 4, (uae_u32)fake_srp_030);
		} else {
			fake_srp_030 = (uae_u64)x_get_long (extra) << 32;
			fake_srp_030 |= x_get_long (extra + 4);
		}
		break;
	case 0x13: // CRP
		reg = _T("CRP");
		siz = 8;
		if (rw) {
			x_put_long (extra, fake_crp_030 >> 32);
			x_put_long (extra + 4, (uae_u32)fake_crp_030);
		} else {
			fake_crp_030 = (uae_u64)x_get_long (extra) << 32;
			fake_crp_030 |= x_get_long (extra + 4);
		}
		break;
	case 0x18: // MMUSR
		if (fd) {
			// FD must be always zero when MMUSR read or write
			return true;
		}
		reg = _T("MMUSR");
		siz = 2;
		if (rw)
			x_put_word (extra, fake_mmusr_030);
		else
			fake_mmusr_030 = x_get_word (extra);
		break;
	case 0x02: // TT0
		reg = _T("TT0");
		siz = 4;
		if (rw)
			x_put_long (extra, fake_tt0_030);
		else
			fake_tt0_030 = x_get_long (extra);
		break;
	case 0x03: // TT1
		reg = _T("TT1");
		siz = 4;
		if (rw)
			x_put_long (extra, fake_tt1_030);
		else
			fake_tt1_030 = x_get_long (extra);
		break;
	}

	if (!reg)
		return true;

#if MMUOP_DEBUG > 0
	{
		uae_u32 val;
		if (siz == 8) {
			uae_u32 val2 = x_get_long (extra);
			val = x_get_long (extra + 4);
			if (rw)
				write_log (_T("PMOVE %s,%08X%08X"), reg, val2, val);
			else
				write_log (_T("PMOVE %08X%08X,%s"), val2, val, reg);
		} else {
			if (siz == 4)
				val = x_get_long (extra);
			else
				val = x_get_word (extra);
			if (rw)
				write_log (_T("PMOVE %s,%08X"), reg, val);
			else
				write_log (_T("PMOVE %08X,%s"), val, reg);
		}
		write_log (_T(" PC=%08X\n"), pc);
	}
#endif
	if ((currprefs.cs_mbdmac & 1) && currprefs.mbresmem_low_size > 0) {
		if (otc != fake_tc_030) {
			a3000_fakekick (fake_tc_030 & 0x80000000);
		}
	}
	return false;
}

static bool mmu_op30fake_ptest (uaecptr pc, uae_u32 opcode, uae_u16 next, uaecptr extra)
{
	int eamode = (opcode >> 3) & 7;
 	int rreg = opcode & 7;
    int level = (next&0x1C00)>>10;
    int a = (next >> 8) & 1;

	if (mmu_op30_invea(opcode))
		return true;
    if (!level && a)
		return true;

#if MMUOP_DEBUG > 0
	TCHAR tmp[10];

	tmp[0] = 0;
	if ((next >> 8) & 1)
		_stprintf (tmp, _T(",A%d"), (next >> 4) & 15);
	write_log (_T("PTEST%c %02X,%08X,#%X%s PC=%08X\n"),
		((next >> 9) & 1) ? 'W' : 'R', (next & 15), extra, (next >> 10) & 7, tmp, pc);
#endif
	fake_mmusr_030 = 0;
	return false;
}

static bool mmu_op30fake_pload (uaecptr pc, uae_u32 opcode, uae_u16 next, uaecptr extra)
{
  	int unused = (next & (0x100 | 0x80 | 0x40 | 0x20));

	if (mmu_op30_invea(opcode))
		return true;
	if (unused)
		return true;
	write_log(_T("PLOAD\n"));
	return false;
}

static bool mmu_op30fake_pflush (uaecptr pc, uae_u32 opcode, uae_u16 next, uaecptr extra)
{
	int flushmode = (next >> 8) & 31;
	int fc = next & 31;
	int mask = (next >> 5) & 3;
	int fc_bits = next & 0x7f;
	TCHAR fname[100];

	switch (flushmode)
	{
	case 0x00:
	case 0x02:
		return mmu_op30fake_pload(pc, opcode, next, extra);
	case 0x18:
		if (mmu_op30_invea(opcode))
			return true;
		_stprintf (fname, _T("FC=%x MASK=%x EA=%08x"), fc, mask, 0);
		break;
	case 0x10:
		_stprintf (fname, _T("FC=%x MASK=%x"), fc, mask);
		break;
	case 0x04:
		if (fc_bits)
			return true;
		_tcscpy (fname, _T("ALL"));
		break;
	default:
		return true;
	}
#if MMUOP_DEBUG > 0
	write_log (_T("PFLUSH %s PC=%08X\n"), fname, pc);
#endif
	return false;
}

// 68030 (68851) MMU instructions only
bool mmu_op30 (uaecptr pc, uae_u32 opcode, uae_u16 extra, uaecptr extraa)
{
	int type = extra >> 13;
	bool fline = false;

	switch (type)
	{
	case 0:
	case 2:
	case 3:
		if (currprefs.mmu_model)
			fline = mmu_op30_pmove (pc, opcode, extra, extraa); 
		else
			fline = mmu_op30fake_pmove (pc, opcode, extra, extraa);
	break;
	case 1:
		if (currprefs.mmu_model)
			fline = mmu_op30_pflush (pc, opcode, extra, extraa); 
		else
			fline = mmu_op30fake_pflush (pc, opcode, extra, extraa);
	break;
	case 4:
		if (currprefs.mmu_model)
			fline = mmu_op30_ptest (pc, opcode, extra, extraa);
		else
			fline = mmu_op30fake_ptest (pc, opcode, extra, extraa);
	break;
	}
	if (fline) {
		m68k_setpc(pc);
		op_illg(opcode);
	}
	return fline;	
}

/* check if an address matches a ttr */
static int fake_mmu_do_match_ttr(uae_u32 ttr, uaecptr addr, bool super)
{
	if (ttr & MMU_TTR_BIT_ENABLED)	{	/* TTR enabled */
		uae_u8 msb, mask;

		msb = ((addr ^ ttr) & MMU_TTR_LOGICAL_BASE) >> 24;
		mask = (ttr & MMU_TTR_LOGICAL_MASK) >> 16;

		if (!(msb & ~mask)) {

			if ((ttr & MMU_TTR_BIT_SFIELD_ENABLED) == 0) {
				if (((ttr & MMU_TTR_BIT_SFIELD_SUPER) == 0) != (super == 0)) {
					return TTR_NO_MATCH;
				}
			}

			return (ttr & MMU_TTR_BIT_WRITE_PROTECT) ? TTR_NO_WRITE : TTR_OK_MATCH;
		}
	}
	return TTR_NO_MATCH;
}

static int fake_mmu_match_ttr(uaecptr addr, bool super, bool data)
{
	int res;

	if (data) {
		res = fake_mmu_do_match_ttr(regs.dtt0, addr, super);
		if (res == TTR_NO_MATCH)
			res = fake_mmu_do_match_ttr(regs.dtt1, addr, super);
	} else {
		res = fake_mmu_do_match_ttr(regs.itt0, addr, super);
		if (res == TTR_NO_MATCH)
			res = fake_mmu_do_match_ttr(regs.itt1, addr, super);
	}
	return res;
}

// 68040+ MMU instructions only
void mmu_op (uae_u32 opcode, uae_u32 extra)
{
	if (currprefs.mmu_model) {
		mmu_op_real (opcode, extra);
		return;
	}
#if MMUOP_DEBUG > 1
	write_log (_T("mmu_op %04X PC=%08X\n"), opcode, m68k_getpc ());
#endif
	if ((opcode & 0xFE0) == 0x0500) {
		/* PFLUSH */
		regs.mmusr = 0;
#if MMUOP_DEBUG > 0
		write_log (_T("PFLUSH\n"));
#endif
		return;
	} else if ((opcode & 0x0FD8) == 0x0548) {
		if (currprefs.cpu_model < 68060) { /* PTEST not in 68060 */
			/* PTEST */
			int regno = opcode & 7;
			uae_u32 addr = m68k_areg(regs, regno);
			bool write = (opcode & 32) == 0;
			bool super = (regs.dfc & 4) != 0;
			bool data = (regs.dfc & 3) != 2;

			regs.mmusr = 0;
			if (fake_mmu_match_ttr(addr, super, data) != TTR_NO_MATCH) {
				regs.mmusr = MMU_MMUSR_T | MMU_MMUSR_R;
			}
			regs.mmusr |= addr & 0xfffff000;
#if MMUOP_DEBUG > 0
			write_log (_T("PTEST%c %08x\n"), write ? 'W' : 'R', addr);
#endif
			return;
		}
	} else if ((opcode & 0x0FB8) == 0x0588) {
		/* PLPA */
		if (currprefs.cpu_model == 68060) {
			int regno = opcode & 7;
			uae_u32 addr = m68k_areg (regs, regno);
			int write = (opcode & 0x40) == 0;
			bool data = (regs.dfc & 3) != 2;
			bool super = (regs.dfc & 4) != 0;

			if (fake_mmu_match_ttr(addr, super, data) == TTR_NO_MATCH) {
				m68k_areg (regs, regno) = addr;
			}
#if MMUOP_DEBUG > 0
			write_log (_T("PLPA\n"));
#endif
			return;
		}
	}
#if MMUOP_DEBUG > 0
	write_log (_T("Unknown MMU OP %04X\n"), opcode);
#endif
	m68k_setpc_normal (m68k_getpc () - 2);
	op_illg (opcode);
}

#endif

static void do_trace (void)
{
	if (regs.t0 && currprefs.cpu_model >= 68020) {
		// this is obsolete
		return;
	}
	if (regs.t1) {
		activate_trace();
	}
}

static void check_uae_int_request(void)
{
	bool irq2 = false;
	bool irq6 = false;
	if (atomic_and(&uae_interrupt, 0)) {
		for (int i = 0; i < IRQ_SOURCE_MAX; i++) {
			if (!irq2 && uae_interrupts2[i]) {
				uae_atomic v = atomic_and(&uae_interrupts2[i], 0);
				if (v) {
					INTREQ_f(0x8000 | 0x0008);
					irq2 = true;
				}
			}
			if (!irq6 && uae_interrupts6[i]) {
				uae_atomic v = atomic_and(&uae_interrupts6[i], 0);
				if (v) {
					INTREQ_f(0x8000 | 0x2000);
					irq6 = true;
				}
			}
		}
	}
	if (uae_int_requested) {
		if (!irq2 && (uae_int_requested & 0x00ff)) {
			INTREQ_f(0x8000 | 0x0008);
			irq2 = true;
		}
		if (!irq6 && (uae_int_requested & 0xff00)) {
			INTREQ_f(0x8000 | 0x2000);
			irq6 = true;
		}
		if (uae_int_requested & 0xff0000) {
			if (!cpuboard_is_ppcboard_irq()) {
				atomic_and(&uae_int_requested, ~0x010000);
			}
		}
	}
	if (irq2 || irq6) {
		doint();
	}
}

void safe_interrupt_set(int num, int id, bool i6)
{
	if (!is_mainthread()) {
		set_special_exter(SPCFLAG_UAEINT);
		volatile uae_atomic *p;
		if (i6)
			p = &uae_interrupts6[num];
		else
			p = &uae_interrupts2[num];
		atomic_or(p, 1 << id);
		atomic_or(&uae_interrupt, 1);
	} else {
		uae_u16 v = i6 ? 0x2000 : 0x0008;
		if (currprefs.cpu_cycle_exact || (!(intreq & v) && !currprefs.cpu_cycle_exact)) {
			INTREQ_0(0x8000 | v);
		}
	}
}

int cpu_sleep_millis(int ms)
{
	int ret = 0;
#ifdef WITH_PPC
	int state = ppc_state;
	if (state)
		uae_ppc_spinlock_release();
#endif
#ifdef WITH_X86
//	if (x86_turbo_on) {
//		execute_other_cpu(read_processor_time() + vsynctimebase / 20);
//	} else {
		ret = sleep_millis_main(ms);
//	}
#endif
#ifdef WITH_PPC
	if (state)
		uae_ppc_spinlock_get();
#endif
	return ret;
}

#define PPC_HALTLOOP_SCANLINES 25
// ppc_cpu_idle
// 0 = busy
// 1-9 = wait, levels
// 10 = max wait

static bool haltloop_do(int vsynctimeline, int rpt_end, int lines)
{
	int ovpos = vpos;
	while (lines-- >= 0) {
		ovpos = vpos;
		while (ovpos == vpos) {
			x_do_cycles(8 * CYCLE_UNIT);
			unset_special(SPCFLAG_UAEINT);
			check_uae_int_request();
#ifdef WITH_PPC
			ppc_interrupt(intlev());
			uae_ppc_execute_check();
#endif
			if (regs.spcflags & SPCFLAG_COPPER)
				do_copper();
			if (regs.spcflags & (SPCFLAG_BRK | SPCFLAG_MODE_CHANGE)) {
				if (regs.spcflags & SPCFLAG_BRK) {
					unset_special(SPCFLAG_BRK);
	#ifdef DEBUGGER
					if (debugging)
						debug();
	#endif
				}
				return true;
			}
		}

		// sync chipset with real time
		for (;;) {
			check_uae_int_request();
#ifdef WITH_PPC
			ppc_interrupt(intlev());
			uae_ppc_execute_check();
#endif
			if (event_wait)
				break;
			int d = read_processor_time() - rpt_end;
			if (d < -2 * vsynctimeline || d >= 0)
				break;
		}
	}
	return false;
}

static bool haltloop(void)
{
#ifdef WITH_PPC
	if (regs.halted < 0) {
		int rpt_end = 0;
		int ovpos = vpos;

		while (regs.halted) {
			int vsynctimeline = vsynctimebase / (maxvpos_display + 1);
			int lines;
			int rpt_scanline = read_processor_time();
			int rpt_end = rpt_scanline + vsynctimeline;

			// See expansion handling.
			// Dialog must be opened from main thread.
			if (regs.halted == -2) {
				regs.halted = -1;
				notify_user (NUMSG_UAEBOOTROM_PPC);
			}

			if (currprefs.ppc_cpu_idle) {

				int maxlines = 100 - (currprefs.ppc_cpu_idle - 1) * 10;
				int i;

				event_wait = false;
				for (i = 0; i < ev_max; i++) {
					if (i == ev_hsync)
						continue;
					if (i == ev_audio)
						continue;
					if (!eventtab[i].active)
						continue;
					if (eventtab[i].evtime - currcycle < maxlines * maxhpos * CYCLE_UNIT)
						break;
				}
				if (currprefs.ppc_cpu_idle >= 10 || (i == ev_max && vpos > 0 && vpos < maxvpos - maxlines)) {
					cpu_sleep_millis(1);
				}
				check_uae_int_request();
				uae_ppc_execute_check();

				lines = (read_processor_time() - rpt_scanline) / vsynctimeline + 1;

			} else {

				event_wait = true;
				lines = 0;

			}

			if (lines > maxvpos / 2)
				lines = maxvpos / 2;

			if (haltloop_do(vsynctimeline, rpt_end, lines))
				return true;

		}

	} else  {
#endif
		while (regs.halted) {
			static int prevvpos;
			if (vpos == 0 && prevvpos) {
				prevvpos = 0;
				cpu_sleep_millis(8);
			}
			if (vpos)
				prevvpos = 1;
			x_do_cycles(8 * CYCLE_UNIT);

			if (regs.spcflags & SPCFLAG_COPPER)
				do_copper();

			if (regs.spcflags) {
				if ((regs.spcflags & (SPCFLAG_BRK | SPCFLAG_MODE_CHANGE)))
					return true;
			}
		}
#ifdef WITH_PPC
	}
#endif

	return false;
}

#ifdef WITH_PPC
static bool uae_ppc_poll_check_halt(void)
{
	if (regs.halted) {
		if (haltloop())
			return true;
	}
	return false;
}
#endif


// handle interrupt delay (few cycles)
STATIC_INLINE bool time_for_interrupt (void)
{
	return regs.ipl > regs.intmask || regs.ipl == 7;
}

void doint (void)
{
#ifdef WITH_PPC
	if (ppc_state) {
		if (!ppc_interrupt(intlev()))
			return;
	}
#endif
	if (m68k_interrupt_delay) {
		regs.ipl_pin = intlev ();
		unset_special (SPCFLAG_INT);
		return;
	}
	if (currprefs.cpu_compatible && currprefs.cpu_model < 68020)
		set_special (SPCFLAG_INT);
	else
		set_special (SPCFLAG_DOINT);
}

static int do_specialties (int cycles)
{
	bool stopped_debug = false;

	if (regs.spcflags & SPCFLAG_MODE_CHANGE)
		return 1;
	
	if (regs.spcflags & SPCFLAG_CHECK) {
		if (regs.halted) {
			if (regs.halted == CPU_HALT_ACCELERATOR_CPU_FALLBACK) {
				return 1;
			}
			unset_special(SPCFLAG_CHECK);
			if (haltloop())
				return 1;
		}
		if (m68k_reset_delay) {
			int vsynccnt = 60;
			int vsyncstate = -1;
			while (vsynccnt > 0 && !quit_program) {
				x_do_cycles(8 * CYCLE_UNIT);
				if (regs.spcflags & SPCFLAG_COPPER)
					do_copper();
				if (timeframes != vsyncstate) {
					vsyncstate = timeframes;
					vsynccnt--;
				}
			}
		}
		m68k_reset_delay = 0;
		unset_special(SPCFLAG_CHECK);
	}

#ifdef ACTION_REPLAY
#ifdef ACTION_REPLAY_HRTMON
	if ((regs.spcflags & SPCFLAG_ACTION_REPLAY) && hrtmon_flag != ACTION_REPLAY_INACTIVE) {
		int isinhrt = (m68k_getpc () >= hrtmem_start && m68k_getpc () < hrtmem_start + hrtmem_size);
		/* exit from HRTMon? */
		if (hrtmon_flag == ACTION_REPLAY_ACTIVE && !isinhrt)
			hrtmon_hide ();
		/* HRTMon breakpoint? (not via IRQ7) */
		if (hrtmon_flag == ACTION_REPLAY_IDLE && isinhrt)
			hrtmon_breakenter ();
		if (hrtmon_flag == ACTION_REPLAY_ACTIVATE)
			hrtmon_enter ();
	}
#endif
	if ((regs.spcflags & SPCFLAG_ACTION_REPLAY) && action_replay_flag != ACTION_REPLAY_INACTIVE) {
		/*if (action_replay_flag == ACTION_REPLAY_ACTIVE && !is_ar_pc_in_rom ())*/
		/*	write_log (_T("PC:%p\n"), m68k_getpc ());*/

		if (action_replay_flag == ACTION_REPLAY_ACTIVATE || action_replay_flag == ACTION_REPLAY_DORESET)
			action_replay_enter ();
		if ((action_replay_flag == ACTION_REPLAY_HIDE || action_replay_flag == ACTION_REPLAY_ACTIVE) && !is_ar_pc_in_rom ()) {
			action_replay_hide ();
			unset_special (SPCFLAG_ACTION_REPLAY);
		}
		if (action_replay_flag == ACTION_REPLAY_WAIT_PC) {
			/*write_log (_T("Waiting for PC: %p, current PC= %p\n"), wait_for_pc, m68k_getpc ());*/
			if (m68k_getpc () == wait_for_pc) {
				action_replay_flag = ACTION_REPLAY_ACTIVATE; /* Activate after next instruction. */
			}
		}
	}
#endif

	if (regs.spcflags & SPCFLAG_COPPER)
		do_copper ();

#ifdef JIT
	unset_special (SPCFLAG_END_COMPILE);   /* has done its job */
#endif

	while ((regs.spcflags & SPCFLAG_BLTNASTY) && dmaen (DMA_BLITTER) && cycles > 0 && ((currprefs.waiting_blits && currprefs.cpu_model >= 68020) || !currprefs.blitter_cycle_exact)) {
		int c = blitnasty ();
		if (c < 0) {
			break;
		} else if (c > 0) {
			cycles -= c * CYCLE_UNIT * 2;
			if (cycles < CYCLE_UNIT)
				cycles = 0;
		} else {
			c = 4;
		}
		x_do_cycles (c * CYCLE_UNIT);
		if (regs.spcflags & SPCFLAG_COPPER)
			do_copper ();
#ifdef WITH_PPC
		if (ppc_state)  {
			if (uae_ppc_poll_check_halt())
				return true;
			uae_ppc_execute_check();
		}
#endif
	}

	if (regs.spcflags & SPCFLAG_DOTRACE)
		Exception (9);

	if (regs.spcflags & SPCFLAG_TRAP) {
		unset_special (SPCFLAG_TRAP);
		Exception (3);
	}

	if ((regs.spcflags & SPCFLAG_STOP) && regs.s == 0 && currprefs.cpu_model <= 68010) {
		// 68000/68010 undocumented special case:
		// if STOP clears S-bit and T was not set:
		// cause privilege violation exception, PC pointing to following instruction.
		// If T was set before STOP: STOP works as documented.
		m68k_unset_stop();
		Exception(8);
	}

	bool first = true;
	while ((regs.spcflags & SPCFLAG_STOP) && !(regs.spcflags & SPCFLAG_BRK)) {
	isstopped:
		check_uae_int_request();
		{
			if (bsd_int_requested)
				bsdsock_fake_int_handler ();
		}

		if (cpu_tracer > 0) {
			cputrace.stopped = regs.stopped;
			cputrace.intmask = regs.intmask;
			cputrace.sr = regs.sr;
			cputrace.state = 1;
			cputrace.pc = m68k_getpc ();
			cputrace.memoryoffset = 0;
			cputrace.cyclecounter = cputrace.cyclecounter_pre = cputrace.cyclecounter_post = 0;
			cputrace.readcounter = cputrace.writecounter = 0;
		}
		if (!first)
			x_do_cycles (currprefs.cpu_cycle_exact ? 2 * CYCLE_UNIT : 4 * CYCLE_UNIT);
		first = false;
		if (regs.spcflags & SPCFLAG_COPPER)
			do_copper ();

		if (m68k_interrupt_delay) {
			ipl_fetch ();
			if (time_for_interrupt ()) {
				do_interrupt (regs.ipl);
			}
		} else {
			if (regs.spcflags & (SPCFLAG_INT | SPCFLAG_DOINT)) {
				int intr = intlev ();
				unset_special (SPCFLAG_INT | SPCFLAG_DOINT);
#ifdef WITH_PPC
				bool m68kint = true;
				if (ppc_state) {
					m68kint = ppc_interrupt(intr);
				}
				if (m68kint) {
#endif
					if (intr > 0 && intr > regs.intmask)
						do_interrupt (intr);
#ifdef WITH_PPC
				}
#endif
			}
		}

		if (regs.spcflags & SPCFLAG_MODE_CHANGE) {
			m68k_resumestopped();
			return 1;
		}

#ifdef WITH_PPC
		if (ppc_state) {
			uae_ppc_execute_check();
			uae_ppc_poll_check_halt();
		}
#endif

	}

	if (regs.spcflags & SPCFLAG_TRACE)
		do_trace ();

	if (regs.spcflags & SPCFLAG_UAEINT) {
		check_uae_int_request();
		unset_special(SPCFLAG_UAEINT);
	}

	if (m68k_interrupt_delay) {
		if (time_for_interrupt ()) {
			do_interrupt (regs.ipl);
		}
	} else {
		if (regs.spcflags & SPCFLAG_INT) {
			int intr = intlev ();
			unset_special (SPCFLAG_INT | SPCFLAG_DOINT);
			if (intr > 0 && (intr > regs.intmask || intr == 7))
				do_interrupt (intr);
		}
	}

	if (regs.spcflags & SPCFLAG_DOINT) {
		unset_special (SPCFLAG_DOINT);
		set_special (SPCFLAG_INT);
	}

	if ((regs.spcflags & SPCFLAG_BRK) || stopped_debug) {
		unset_special(SPCFLAG_BRK);
#ifdef DEBUGGER
		if (stopped_debug && !regs.stopped) {
			if (debugging) {
				debugger_active = 1;
				stopped_debug = false;
			}
		}
		if (debugging) {
			if (!stopped_debug)
				debug();
			if (regs.stopped) {
				stopped_debug = true;
				if (debugging) {
					debugger_active = 0;
				}
				goto isstopped;
			}
		}
#endif
	}

	return 0;
}

//static uae_u32 pcs[1000];

#if DEBUG_CD32CDTVIO

static uae_u32 cd32nextpc, cd32request;

static void out_cd32io2 (void)
{
	uae_u32 request = cd32request;
	write_log (_T("%08x returned\n"), request);
	//write_log (_T("ACTUAL=%d ERROR=%d\n"), get_long (request + 32), get_byte (request + 31));
	cd32nextpc = 0;
	cd32request = 0;
}

static void out_cd32io (uae_u32 pc)
{
	TCHAR out[100];
	int ioreq = 0;
	uae_u32 request = m68k_areg (regs, 1);

	if (pc == cd32nextpc) {
		out_cd32io2 ();
		return;
	}
	out[0] = 0;
	switch (pc)
	{
	case 0xe57cc0:
	case 0xf04c34:
		_stprintf (out, _T("opendevice"));
		break;
	case 0xe57ce6:
	case 0xf04c56:
		_stprintf (out, _T("closedevice"));
		break;
	case 0xe57e44:
	case 0xf04f2c:
		_stprintf (out, _T("beginio"));
		ioreq = 1;
		break;
	case 0xe57ef2:
	case 0xf0500e:
		_stprintf (out, _T("abortio"));
		ioreq = -1;
		break;
	}
	if (out[0] == 0)
		return;
	if (cd32request)
		write_log (_T("old request still not returned!\n"));
	cd32request = request;
	cd32nextpc = get_long (m68k_areg (regs, 7));
	write_log (_T("%s A1=%08X\n"), out, request);
	if (ioreq) {
		static int cnt = 0;
		int cmd = get_word (request + 28);
#if 0
		if (cmd == 33) {
			uaecptr data = get_long (request + 40);
			write_log (_T("CD_CONFIG:\n"));
			for (int i = 0; i < 16; i++) {
				write_log (_T("%08X=%08X\n"), get_long (data), get_long (data + 4));
				data += 8;
			}
		}
#endif
#if 0
		if (cmd == 37) {
			cnt--;
			if (cnt <= 0)
				activate_debugger ();
		}
#endif
		write_log (_T("CMD=%d DATA=%08X LEN=%d %OFF=%d PC=%x\n"),
			cmd, get_long (request + 40),
			get_long (request + 36), get_long (request + 44), M68K_GETPC);
	}
	if (ioreq < 0)
		;//activate_debugger ();
}

#endif

#ifndef CPUEMU_11

static void m68k_run_1 (void)
{
}

#else

/* It's really sad to have two almost identical functions for this, but we
do it all for performance... :(
This version emulates 68000's prefetch "cache" */
static void m68k_run_1 (void)
{
	struct regstruct *r = &regs;
	bool exit = false;

	while (!exit) {
		TRY (prb) {
			while (!exit) {
				r->opcode = r->ir;

				count_instr (r->opcode);

#if DEBUG_CD32CDTVIO
				out_cd32io (m68k_getpc ());
#endif

#if 0
				int pc = m68k_getpc ();
				if (pc == 0xdff002)
					write_log (_T("hip\n"));
				if (pc != pcs[0] && (pc < 0xd00000 || pc > 0x1000000)) {
					memmove (pcs + 1, pcs, 998 * 4);
					pcs[0] = pc;
					//write_log (_T("%08X-%04X "), pc, r->opcode);
				}
#endif
				if (debug_opcode_watch) {
					debug_trainer_match();
				}
				do_cycles (cpu_cycles);
				r->instruction_pc = m68k_getpc ();
				cpu_cycles = (*cpufunctbl[r->opcode])(r->opcode);
				cpu_cycles = adjust_cycles (cpu_cycles);
				if (r->spcflags) {
					if (do_specialties (cpu_cycles))
						exit = true;
				}
				regs.ipl = regs.ipl_pin;
				if (!currprefs.cpu_compatible || (currprefs.cpu_cycle_exact && currprefs.cpu_model <= 68010))
					exit = true;
			}
		} CATCH (prb) {
			bus_error();
			if (r->spcflags) {
				if (do_specialties(cpu_cycles))
					exit = true;
			}
			regs.ipl = regs.ipl_pin;
		} ENDTRY
	}
}

#endif /* CPUEMU_11 */

#ifndef CPUEMU_13

static void m68k_run_1_ce (void)
{
}

#else

/* cycle-exact m68k_run () */

static void m68k_run_1_ce (void)
{
	struct regstruct *r = &regs;
	bool first = true;
	bool exit = false;

	while (!exit) {
		TRY (prb) {
			if (first) {
				if (cpu_tracer < 0) {
					memcpy (&r->regs, &cputrace.regs, 16 * sizeof (uae_u32));
					r->ir = cputrace.ir;
					r->irc = cputrace.irc;
					r->sr = cputrace.sr;
					r->usp = cputrace.usp;
					r->isp = cputrace.isp;
					r->intmask = cputrace.intmask;
					r->stopped = cputrace.stopped;
					m68k_setpc (cputrace.pc);
					if (!r->stopped) {
						if (cputrace.state > 1) {
							write_log (_T("CPU TRACE: EXCEPTION %d\n"), cputrace.state);
							Exception (cputrace.state);
						} else if (cputrace.state == 1) {
							write_log (_T("CPU TRACE: %04X\n"), cputrace.opcode);
							(*cpufunctbl[cputrace.opcode])(cputrace.opcode);
						}
					} else {
						write_log (_T("CPU TRACE: STOPPED\n"));
					}
					if (r->stopped)
						set_special (SPCFLAG_STOP);
					set_cpu_tracer (false);
					goto cont;
				}
				set_cpu_tracer (false);
				first = false;
			}

			while (!exit) {
				r->opcode = r->ir;

#if DEBUG_CD32CDTVIO
				out_cd32io (m68k_getpc ());
#endif
				if (cpu_tracer) {
					memcpy (&cputrace.regs, &r->regs, 16 * sizeof (uae_u32));
					cputrace.opcode = r->opcode;
					cputrace.ir = r->ir;
					cputrace.irc = r->irc;
					cputrace.sr = r->sr;
					cputrace.usp = r->usp;
					cputrace.isp = r->isp;
					cputrace.intmask = r->intmask;
					cputrace.stopped = r->stopped;
					cputrace.state = 1;
					cputrace.pc = m68k_getpc ();
					cputrace.startcycles = get_cycles ();
					cputrace.memoryoffset = 0;
					cputrace.cyclecounter = cputrace.cyclecounter_pre = cputrace.cyclecounter_post = 0;
					cputrace.readcounter = cputrace.writecounter = 0;
				}

				if (inputrecord_debug & 4) {
					if (input_record > 0)
						inprec_recorddebug_cpu (1);
					else if (input_play > 0)
						inprec_playdebug_cpu (1);
				}

				if (debug_opcode_watch) {
					debug_trainer_match();
				}

				r->instruction_pc = m68k_getpc ();
				(*cpufunctbl[r->opcode])(r->opcode);
				wait_memory_cycles();
				if (cpu_tracer) {
					cputrace.state = 0;
				}
cont:
				if (cputrace.needendcycles) {
					cputrace.needendcycles = 0;
					write_log (_T("STARTCYCLES=%08x ENDCYCLES=%08lx\n"), cputrace.startcycles, get_cycles ());
					log_dma_record ();
				}

				if (r->spcflags || time_for_interrupt ()) {
					if (do_specialties (0))
						exit = true;
				}

				if (!currprefs.cpu_cycle_exact || currprefs.cpu_model > 68010)
					exit = true;
			}
		} CATCH (prb) {
			bus_error();
			if (r->spcflags || time_for_interrupt()) {
				if (do_specialties(0))
					exit = true;
			}
		} ENDTRY
	}
}

#endif

#if defined(CPUEMU_20) && defined(JIT)
// emulate simple prefetch
static uae_u16 get_word_020_prefetchf (uae_u32 pc)
{
	if (pc == regs.prefetch020addr) {
		uae_u16 v = regs.prefetch020[0];
		regs.prefetch020[0] = regs.prefetch020[1];
		regs.prefetch020[1] = regs.prefetch020[2];
		regs.prefetch020[2] = x_get_word (pc + 6);
		regs.prefetch020addr += 2;
		return v;
	} else if (pc == regs.prefetch020addr + 2) {
		uae_u16 v = regs.prefetch020[1];
		regs.prefetch020[0] = regs.prefetch020[2];
		regs.prefetch020[1] = x_get_word (pc + 4);
		regs.prefetch020[2] = x_get_word (pc + 6);
		regs.prefetch020addr = pc + 2;
		return v;
	} else if (pc == regs.prefetch020addr + 4) {
		uae_u16 v = regs.prefetch020[2];
		regs.prefetch020[0] = x_get_word (pc + 2);
		regs.prefetch020[1] = x_get_word (pc + 4);
		regs.prefetch020[2] = x_get_word (pc + 6);
		regs.prefetch020addr = pc + 2;
		return v;
	} else {
		regs.prefetch020addr = pc + 2;
		regs.prefetch020[0] = x_get_word (pc + 2);
		regs.prefetch020[1] = x_get_word (pc + 4);
		regs.prefetch020[2] = x_get_word (pc + 6);
		return x_get_word (pc);
	}
}
#endif

#ifdef WITH_THREADED_CPU
static volatile int cpu_thread_active;
static uae_sem_t cpu_in_sema, cpu_out_sema, cpu_wakeup_sema;

static volatile int cpu_thread_ilvl;
static volatile uae_u32 cpu_thread_indirect_mode;
static volatile uae_u32 cpu_thread_indirect_addr;
static volatile uae_u32 cpu_thread_indirect_val;
static volatile uae_u32 cpu_thread_indirect_size;
static volatile uae_u32 cpu_thread_reset;
static uae_thread_id cpu_thread_tid;

static bool m68k_cs_initialized;

static int do_specialties_thread(void)
{
	if (regs.spcflags & SPCFLAG_MODE_CHANGE)
		return 1;

#ifdef JIT
	unset_special(SPCFLAG_END_COMPILE);   /* has done its job */
#endif

	if (regs.spcflags & SPCFLAG_DOTRACE)
		Exception(9);

	if (regs.spcflags & SPCFLAG_TRAP) {
		unset_special(SPCFLAG_TRAP);
		Exception(3);
	}

	if (regs.spcflags & SPCFLAG_TRACE)
		do_trace();

	for (;;) {

		if (regs.spcflags & (SPCFLAG_BRK | SPCFLAG_MODE_CHANGE)) {
			return 1;
		}

		int ilvl = cpu_thread_ilvl;
		if (ilvl > 0 && (ilvl > regs.intmask || ilvl == 7)) {
			do_interrupt(ilvl);
		}

		if (!(regs.spcflags & SPCFLAG_STOP))
			break;

		uae_sem_wait(&cpu_wakeup_sema);
	}

	return 0;
}

static void init_cpu_thread(void)
{
	if (!currprefs.cpu_thread)
		return;
	if (m68k_cs_initialized)
		return;
	uae_sem_init(&cpu_in_sema, 0, 0);
	uae_sem_init(&cpu_out_sema, 0, 0);
	uae_sem_init(&cpu_wakeup_sema, 0, 0);
	m68k_cs_initialized = true;
}

extern addrbank *thread_mem_banks[MEMORY_BANKS];

uae_u32 process_cpu_indirect_memory_read(uae_u32 addr, int size)
{
	// Do direct access if call is from filesystem etc thread 
	if (cpu_thread_tid != uae_thread_get_id()) {
		uae_u32 data = 0;
		addrbank *ab = thread_mem_banks[bankindex(addr)];
		switch (size)
		{
		case 0:
			data = ab->bget(addr) & 0xff;
			break;
		case 1:
			data = ab->wget(addr) & 0xffff;
			break;
		case 2:
			data = ab->lget(addr);
			break;
		}
		return data;
	}

	cpu_thread_indirect_mode = 2;
	cpu_thread_indirect_addr = addr;
	cpu_thread_indirect_size = size;
	uae_sem_post(&cpu_out_sema);
	uae_sem_wait(&cpu_in_sema);
	cpu_thread_indirect_mode = 0xfe;
	return cpu_thread_indirect_val;
}

void process_cpu_indirect_memory_write(uae_u32 addr, uae_u32 data, int size)
{
	if (cpu_thread_tid != uae_thread_get_id()) {
		addrbank *ab = thread_mem_banks[bankindex(addr)];
		switch (size)
		{
		case 0:
			ab->bput(addr, data & 0xff);
			break;
		case 1:
			ab->wput(addr, data & 0xffff);
			break;
		case 2:
			ab->lput(addr, data);
			break;
		}
		return;
	}
	cpu_thread_indirect_mode = 1;
	cpu_thread_indirect_addr = addr;
	cpu_thread_indirect_size = size;
	cpu_thread_indirect_val = data;
	uae_sem_post(&cpu_out_sema);
	uae_sem_wait(&cpu_in_sema);
	cpu_thread_indirect_mode = 0xff;
}

static void run_cpu_thread(void *(*f)(void *))
{
	int framecnt = -1;
	int vp = 0;
	int intlev_prev = 0;

	cpu_thread_active = 0;
	uae_sem_init(&cpu_in_sema, 0, 0);
	uae_sem_init(&cpu_out_sema, 0, 0);
	uae_sem_init(&cpu_wakeup_sema, 0, 0);

	if (!uae_start_thread(_T("cpu"), f, NULL, NULL))
		return;
	while (!cpu_thread_active) {
		sleep_millis(1);
	}

	while (!(regs.spcflags & SPCFLAG_MODE_CHANGE)) {
		int maxperloop = 10;

		while (!uae_sem_trywait(&cpu_out_sema)) {
			uae_u32 cmd, addr, data, size, mode;

			addr = cpu_thread_indirect_addr;
			data = cpu_thread_indirect_val;
			size = cpu_thread_indirect_size;
			mode = cpu_thread_indirect_mode;

			switch(mode)
			{
				case 1:
				{
					addrbank *ab = thread_mem_banks[bankindex(addr)];
					switch (size)
					{
					case 0:
						ab->bput(addr, data & 0xff);
						break;
					case 1:
						ab->wput(addr, data & 0xffff);
						break;
					case 2:
						ab->lput(addr, data);
						break;
					}
					uae_sem_post(&cpu_in_sema);
					break;
				}
				case 2:
				{
					addrbank *ab = thread_mem_banks[bankindex(addr)];
					switch (size)
					{
					case 0:
						data = ab->bget(addr) & 0xff;
						break;
					case 1:
						data = ab->wget(addr) & 0xffff;
						break;
					case 2:
						data = ab->lget(addr);
						break;
					}
					cpu_thread_indirect_val = data;
					uae_sem_post(&cpu_in_sema);
					break;
				}
				default:
					write_log(_T("cpu_thread_indirect_mode=%08x!\n"), mode);
					break;
			}

			if (maxperloop-- < 0)
				break;
		}

		if (framecnt != timeframes) {
			framecnt = timeframes;
		}

		if (cpu_thread_reset) {
			bool hardreset = cpu_thread_reset & 2;
			bool keyboardreset = cpu_thread_reset & 4;
			custom_reset(hardreset, keyboardreset);
			cpu_thread_reset = 0;
			uae_sem_post(&cpu_in_sema);
		}

		if (regs.spcflags & SPCFLAG_BRK) {
			unset_special(SPCFLAG_BRK);
#ifdef DEBUGGER
			if (debugging) {
				debug();
			}
#endif
		}

		if (vp == vpos) {

			do_cycles((maxhpos / 2) * CYCLE_UNIT);

			if (regs.spcflags & SPCFLAG_COPPER) {
				do_copper();
			}

			check_uae_int_request();
			if (regs.spcflags & (SPCFLAG_INT | SPCFLAG_DOINT)) {
				int intr = intlev();
				unset_special(SPCFLAG_INT | SPCFLAG_DOINT);
				if (intr > 0) {
					cpu_thread_ilvl = intr;
					cycles_do_special();
					uae_sem_post(&cpu_wakeup_sema);
				} else {
					cpu_thread_ilvl = 0;
				}
			}
			continue;
		}

		frame_time_t next = vsyncmintimepre + (vsynctimebase * vpos / (maxvpos + 1));
		frame_time_t c = read_processor_time();
		if ((int)next - (int)c > 0 && (int)next - (int)c < vsyncmaxtime * 2)
			continue;

		vp = vpos;

	}

	while (cpu_thread_active) {
		uae_sem_post(&cpu_in_sema);
		uae_sem_post(&cpu_wakeup_sema);
		sleep_millis(1);
	}

}

#endif

void custom_reset_cpu(bool hardreset, bool keyboardreset)
{
#ifdef WITH_THREADED_CPU
	if (cpu_thread_tid != uae_thread_get_id()) {
		custom_reset(hardreset, keyboardreset);
		return;
	}
	cpu_thread_reset = 1 | (hardreset ? 2 : 0) | (keyboardreset ? 4 : 0);
	uae_sem_post(&cpu_wakeup_sema);
	uae_sem_wait(&cpu_in_sema);
#else
	custom_reset(hardreset, keyboardreset);
#endif
}

#ifdef JIT  /* Completely different run_2 replacement */

void do_nothing (void)
{
	if (!currprefs.cpu_thread) {
		/* What did you expect this to do? */
		do_cycles (0);
		/* I bet you didn't expect *that* ;-) */
	}
}

void exec_nostats (void)
{
	struct regstruct *r = &regs;

	for (;;)
	{
		if (currprefs.cpu_compatible) {
			r->opcode = get_word_020_prefetchf(m68k_getpc());
		} else {
			r->opcode = x_get_iword(0);
		}
		cpu_cycles = (*cpufunctbl[r->opcode])(r->opcode);
		cpu_cycles = adjust_cycles (cpu_cycles);

		if (!currprefs.cpu_thread) {
			do_cycles (cpu_cycles);

#ifdef WITH_PPC
			if (ppc_state)
				ppc_interrupt(intlev());
#endif
		}

		if (end_block(r->opcode) || r->spcflags || uae_int_requested)
			return; /* We will deal with the spcflags in the caller */
	}
}

void execute_normal (void)
{
	struct regstruct *r = &regs;
	int blocklen;
	cpu_history pc_hist[MAXRUN];
	int total_cycles;

	if (check_for_cache_miss ())
		return;

	total_cycles = 0;
	blocklen = 0;
	start_pc_p = r->pc_oldp;
	start_pc = r->pc;
	for (;;) {
		/* Take note: This is the do-it-normal loop */
		regs.instruction_pc = m68k_getpc ();
		if (currprefs.cpu_compatible) {
			r->opcode = get_word_020_prefetchf (regs.instruction_pc);
		} else {
			r->opcode = x_get_iword(0);
		}

		special_mem = DISTRUST_CONSISTENT_MEM;
		pc_hist[blocklen].location = (uae_u16*)r->pc_p;

		cpu_cycles = (*cpufunctbl[r->opcode])(r->opcode);
		cpu_cycles = adjust_cycles(cpu_cycles);
		if (!currprefs.cpu_thread) {
			do_cycles (cpu_cycles);
		}
		total_cycles += cpu_cycles;
		pc_hist[blocklen].specmem = special_mem;
		blocklen++;
		if (end_block (r->opcode) || blocklen >= MAXRUN || r->spcflags || uae_int_requested) {
			compile_block (pc_hist, blocklen, total_cycles);
			return; /* We will deal with the spcflags in the caller */
		}
		/* No need to check regs.spcflags, because if they were set,
		we'd have ended up inside that "if" */

#ifdef WITH_PPC
		if (ppc_state)
			ppc_interrupt(intlev());
#endif
	}
}

typedef void compiled_handler (void);

#ifdef WITH_THREADED_CPU
static void *cpu_thread_run_jit(void *v)
{
	cpu_thread_tid = uae_thread_get_id();
	cpu_thread_active = 1;
#ifdef USE_STRUCTURED_EXCEPTION_HANDLING
	__try
#endif
	{
		for (;;) {
			((compiled_handler*)(pushall_call_handler))();
			/* Whenever we return from that, we should check spcflags */
			if (regs.spcflags || cpu_thread_ilvl > 0) {
				if (do_specialties_thread()) {
					break;
				}
			}
		}
	}
#ifdef USE_STRUCTURED_EXCEPTION_HANDLING
#ifdef JIT
	__except (EvalException(GetExceptionInformation()))
#else
	__except (DummyException(GetExceptionInformation(), GetExceptionCode()))
#endif
	{
		// EvalException does the good stuff...
	}
#endif
	cpu_thread_active = 0;
	return 0;
}
#endif

static void m68k_run_jit(void)
{
#ifdef WITH_THREADED_CPU
	if (currprefs.cpu_thread) {
		run_cpu_thread(cpu_thread_run_jit);
		return;
	}
#endif

	for (;;) {
#ifdef USE_STRUCTURED_EXCEPTION_HANDLING
		__try {
#endif
			for (;;) {
				((compiled_handler*)(pushall_call_handler))();
				/* Whenever we return from that, we should check spcflags */
				check_uae_int_request();
				if (regs.spcflags) {
					if (do_specialties(0)) {
						return;
					}
				}
			}

#ifdef USE_STRUCTURED_EXCEPTION_HANDLING
		} __except (EvalException(GetExceptionInformation())) {
			// Something very bad happened, generate fake bus error exception
			// Either emulation continues normally or crashes.
			// Without this it would have crashed in any case..
			uaecptr pc = M68K_GETPC;
			write_log(_T("Unhandled JIT exception! PC=%08x\n"), pc);
			if (pc & 1)
				Exception(3);
			else
				Exception(2);
		}
#endif
	}

}
#endif /* JIT */

#ifndef CPUEMU_0

static void m68k_run_2 (void)
{
}

#else

static void opcodedebug (uae_u32 pc, uae_u16 opcode, bool full)
{
	struct mnemolookup *lookup;
	struct instr *dp;
	uae_u32 addr;
	int fault;

	if (cpufunctbl[opcode] == op_illg_1)
		opcode = 0x4AFC;
	dp = table68k + opcode;
	for (lookup = lookuptab;lookup->mnemo != dp->mnemo; lookup++)
		;
	fault = 0;
	TRY(prb) {
		addr = mmu_translate (pc, 0, (regs.mmu_ssw & 4) ? 1 : 0, 0, 0, sz_word);
	} CATCH (prb) {
		fault = 1;
	} ENDTRY
	if (!fault) {
		TCHAR buf[100];
		if (full)
			write_log (_T("mmufixup=%d %04x %04x\n"), mmufixup[0].reg, regs.wb3_status, regs.mmu_ssw);
		m68k_disasm_2 (buf, sizeof buf / sizeof (TCHAR), addr, NULL, 1, NULL, NULL, 0xffffffff, 0);
		write_log (_T("%s\n"), buf);
		if (full)
			m68k_dumpstate(NULL, 0xffffffff);
	}
}

static void check_halt(void)
{
	if (regs.halted)
		do_specialties (0);
}

void cpu_halt (int id)
{
	// id < 0: m68k halted, PPC active.
	// id > 0: emulation halted.
	if (!regs.halted) {
		write_log (_T("CPU halted: reason = %d PC=%08x\n"), id, M68K_GETPC);
		if (currprefs.crash_auto_reset) {
			write_log(_T("Forcing hard reset\n"));
			uae_reset(true, false);
			quit_program = -quit_program;
			set_special(SPCFLAG_BRK | SPCFLAG_MODE_CHANGE);
			return;
		}
		regs.halted = id;
		gui_data.cpu_halted = id;
		gui_led(LED_CPU, 0, -1);
		if (id >= 0) {
			regs.intmask = 7;
			MakeSR ();
			audio_deactivate ();
			if (debugging)
				activate_debugger();
		}
	}
	set_special(SPCFLAG_CHECK);
}

#ifdef CPUEMU_33

/* MMU 68060  */
static void m68k_run_mmu060 (void)
{
	struct flag_struct f;
	int halt = 0;

	check_halt();
	while (!halt) {
		TRY (prb) {
			for (;;) {
				f.cznv = regflags.cznv;
				f.x = regflags.x;
				regs.instruction_pc = m68k_getpc ();

				do_cycles (cpu_cycles);

				mmu_opcode = -1;
				mmu060_state = 0;
				mmu_opcode = regs.opcode = x_prefetch (0);
				mmu060_state = 1;

				count_instr (regs.opcode);
				cpu_cycles = (*cpufunctbl[regs.opcode])(regs.opcode);

				cpu_cycles = adjust_cycles (cpu_cycles);

				if (regs.spcflags) {
					if (do_specialties (cpu_cycles))
						return;
				}
			}
		} CATCH (prb) {
			m68k_setpci (regs.instruction_pc);
			regflags.cznv = f.cznv;
			regflags.x = f.x;

			if (mmufixup[0].reg >= 0) {
				m68k_areg (regs, mmufixup[0].reg) = mmufixup[0].value;
				mmufixup[0].reg = -1;
			}
			if (mmufixup[1].reg >= 0) {
				m68k_areg (regs, mmufixup[1].reg) = mmufixup[1].value;
				mmufixup[1].reg = -1;
			}

			TRY (prb2) {
				Exception (prb);
			} CATCH (prb2) {
				halt = 1;
			} ENDTRY
		} ENDTRY
	}
	cpu_halt(halt);
}

#endif

#ifdef CPUEMU_31

/* Aranym MMU 68040  */
static void m68k_run_mmu040 (void)
{
	flag_struct f;
	int halt = 0;

	check_halt();
	while (!halt) {
		TRY (prb) {
			for (;;) {
				f.cznv = regflags.cznv;
				f.x = regflags.x;
				mmu_restart = true;
				regs.instruction_pc = m68k_getpc ();

				do_cycles (cpu_cycles);

				mmu_opcode = -1;
				mmu_opcode = regs.opcode = x_prefetch (0);
				count_instr (regs.opcode);
				cpu_cycles = (*cpufunctbl[regs.opcode])(regs.opcode);
				cpu_cycles = adjust_cycles (cpu_cycles);

				if (regs.spcflags) {
					if (do_specialties (cpu_cycles))
						return;
				}
			}
		} CATCH (prb) {

			if (mmu_restart) {
				/* restore state if instruction restart */
				regflags.cznv = f.cznv;
				regflags.x = f.x;
				m68k_setpci (regs.instruction_pc);
			}

			if (mmufixup[0].reg >= 0) {
				m68k_areg (regs, mmufixup[0].reg) = mmufixup[0].value;
				mmufixup[0].reg = -1;
			}

			TRY (prb2) {
				Exception (prb);
			} CATCH (prb2) {
				halt = 1;
			} ENDTRY
		} ENDTRY
	}
	cpu_halt(halt);
}

#endif

#ifdef CPUEMU_32

// Previous MMU 68030
static void m68k_run_mmu030 (void)
{
	struct flag_struct f;
	int halt = 0;

	mmu030_opcode_stageb = -1;
	mmu030_fake_prefetch = -1;
	check_halt();
	while(!halt) {
		TRY (prb) {
			for (;;) {
				int cnt;
insretry:
				regs.instruction_pc = m68k_getpc ();
				f.cznv = regflags.cznv;
				f.x = regflags.x;

				mmu030_state[0] = mmu030_state[1] = mmu030_state[2] = 0;
				mmu030_opcode = -1;
				if (mmu030_fake_prefetch >= 0) {
					// use fake prefetch opcode only if mapping changed
					uaecptr new_addr = mmu030_translate(regs.instruction_pc, regs.s != 0, false, false);
					if (mmu030_fake_prefetch_addr != new_addr) {
						regs.opcode = mmu030_fake_prefetch;
						write_log(_T("MMU030 fake prefetch remap: %04x, %08x -> %08x\n"), mmu030_fake_prefetch, mmu030_fake_prefetch_addr, new_addr); 
					} else {
						if (mmu030_opcode_stageb < 0) {
							regs.opcode = x_prefetch (0);
						} else {
							regs.opcode = mmu030_opcode_stageb;
							mmu030_opcode_stageb = -1;
						}
					}
					mmu030_fake_prefetch = -1;
				} else if (mmu030_opcode_stageb < 0) {
					if (currprefs.cpu_compatible)
						regs.opcode = regs.irc;
					else
						regs.opcode = x_prefetch (0);
				} else {
					regs.opcode = mmu030_opcode_stageb;
					mmu030_opcode_stageb = -1;
				}

				mmu030_opcode = regs.opcode;
				mmu030_ad[0].done = false;

				cnt = 50;
				for (;;) {
					regs.opcode = regs.irc = mmu030_opcode;
					mmu030_idx = 0;

					mmu030_retry = false;

					if (!currprefs.cpu_cycle_exact) {

						count_instr (regs.opcode);
						do_cycles (cpu_cycles);

						cpu_cycles = (*cpufunctbl[regs.opcode])(regs.opcode);

					} else {
						
						(*cpufunctbl[regs.opcode])(regs.opcode);

						wait_memory_cycles();
					}

					cnt--; // so that we don't get in infinite loop if things go horribly wrong
					if (!mmu030_retry)
						break;
					if (cnt < 0) {
						cpu_halt (CPU_HALT_CPU_STUCK);
						break;
					}
					if (mmu030_retry && mmu030_opcode == -1)
						goto insretry; // urgh
				}

				mmu030_opcode = -1;

				if (!currprefs.cpu_cycle_exact) {

					cpu_cycles = adjust_cycles (cpu_cycles);
					if (regs.spcflags) {
						if (do_specialties (cpu_cycles))
							return;
					}

				} else {

					if (regs.spcflags || time_for_interrupt ()) {
						if (do_specialties (0))
							return;
					}

					regs.ipl = regs.ipl_pin;

				}

			}

		} CATCH (prb) {

			if (mmu030_opcode == -1) {
				// full prefetch fill access fault
				mmufixup[0].reg = -1;
				mmufixup[1].reg = -1;
			} else if (mmu030_state[1] & MMU030_STATEFLAG1_LASTWRITE) {
				mmufixup[0].reg = -1;
				mmufixup[1].reg = -1;
			} else {
				regflags.cznv = f.cznv;
				regflags.x = f.x;

				if (mmufixup[0].reg >= 0) {
					m68k_areg(regs, mmufixup[0].reg) = mmufixup[0].value;
					mmufixup[0].reg = -1;
				}
				if (mmufixup[1].reg >= 0) {
					m68k_areg(regs, mmufixup[1].reg) = mmufixup[1].value;
					mmufixup[1].reg = -1;
				}
			}

			m68k_setpci (regs.instruction_pc);

			TRY (prb2) {
				Exception (prb);
			} CATCH (prb2) {
				halt = 1;
			} ENDTRY
		} ENDTRY
	}
	cpu_halt (halt);
}

#endif


/* "cycle exact" 68040/060 */

static void m68k_run_3ce (void)
{
	struct regstruct *r = &regs;
	bool exit = false;
	int extracycles = 0;

	while (!exit) {
		TRY(prb) {
			while (!exit) {
				r->instruction_pc = m68k_getpc();
				r->opcode = get_iword_cache_040(0);
				// "prefetch"
				if (regs.cacr & 0x8000)
					fill_icache040(r->instruction_pc + 16);

				if (debug_opcode_watch) {
					debug_trainer_match();
				}

				(*cpufunctbl[r->opcode])(r->opcode);

				if (r->spcflags) {
					if (do_specialties (0))
						exit = true;
				}

				// workaround for situation when all accesses are cached
				extracycles++;
				if (extracycles >= 8) {
					extracycles = 0;
					x_do_cycles(CYCLE_UNIT);
				}
			}
		} CATCH(prb) {
			bus_error();
			if (r->spcflags) {
				if (do_specialties(0))
					exit = true;
			}
		} ENDTRY
	}
}

/* "prefetch" 68040/060 */

static void m68k_run_3p(void)
{
	struct regstruct *r = &regs;
	bool exit = false;
	int cycles;

	while (!exit)  {
		TRY(prb) {
			while (!exit) {
				r->instruction_pc = m68k_getpc();
				r->opcode = get_iword_cache_040(0);
				// "prefetch"
				if (regs.cacr & 0x8000)
					fill_icache040(r->instruction_pc + 16);

				if (debug_opcode_watch) {
					debug_trainer_match();
				}

				(*cpufunctbl[r->opcode])(r->opcode);

				cpu_cycles = 1 * CYCLE_UNIT;
				cycles = adjust_cycles(cpu_cycles);
				do_cycles(cycles);

				if (r->spcflags) {
					if (do_specialties(0))
						exit = true;
				}

			}
		} CATCH(prb) {
			bus_error();
			if (r->spcflags) {
				if (do_specialties(0))
					exit = true;
			}
		} ENDTRY
	}
}

/* "cycle exact" 68020/030  */

static void m68k_run_2ce (void)
{
	struct regstruct *r = &regs;
	bool exit = false;
	bool first = true;

	while (!exit) {
		TRY(prb) {
			if (first) {
				if (cpu_tracer < 0) {
					memcpy (&r->regs, &cputrace.regs, 16 * sizeof (uae_u32));
					r->ir = cputrace.ir;
					r->irc = cputrace.irc;
					r->sr = cputrace.sr;
					r->usp = cputrace.usp;
					r->isp = cputrace.isp;
					r->intmask = cputrace.intmask;
					r->stopped = cputrace.stopped;

					r->msp = cputrace.msp;
					r->vbr = cputrace.vbr;
					r->caar = cputrace.caar;
					r->cacr = cputrace.cacr;
					r->cacheholdingdata020 = cputrace.cacheholdingdata020;
					r->cacheholdingaddr020 = cputrace.cacheholdingaddr020;
					r->prefetch020addr = cputrace.prefetch020addr;
					memcpy(&r->prefetch020, &cputrace.prefetch020, CPU_PIPELINE_MAX * sizeof(uae_u16));
					memcpy(&r->prefetch020_valid, &cputrace.prefetch020_valid, CPU_PIPELINE_MAX * sizeof(uae_u8));
					memcpy(&caches020, &cputrace.caches020, sizeof caches020);

					m68k_setpc (cputrace.pc);
					if (!r->stopped) {
						if (cputrace.state > 1)
							Exception (cputrace.state);
						else if (cputrace.state == 1)
							(*cpufunctbl[cputrace.opcode])(cputrace.opcode);
					}
					if (regs.stopped)
						set_special (SPCFLAG_STOP);
					set_cpu_tracer (false);
					goto cont;
				}
				set_cpu_tracer (false);
				first = false;
			}

			while (!exit) {
#if 0
				static int prevopcode;
#endif
				r->instruction_pc = m68k_getpc ();

#if 0
				if (regs.irc == 0xfffb) {
					gui_message (_T("OPCODE %04X HAS FAULTY PREFETCH! PC=%08X"), prevopcode, r->instruction_pc);
				}
#endif

				//write_log (_T("%x %04x\n"), r->instruction_pc, regs.irc);

				r->opcode = regs.irc;
#if 0
				prevopcode = r->opcode;
				regs.irc = 0xfffb;
#endif
				//write_log (_T("%08x %04x\n"), r->instruction_pc, opcode);

#if DEBUG_CD32CDTVIO
				out_cd32io (r->instruction_pc);
#endif

				if (cpu_tracer) {

#if CPUTRACE_DEBUG
					validate_trace ();
#endif
					memcpy (&cputrace.regs, &r->regs, 16 * sizeof (uae_u32));
					cputrace.opcode = r->opcode;
					cputrace.ir = r->ir;
					cputrace.irc = r->irc;
					cputrace.sr = r->sr;
					cputrace.usp = r->usp;
					cputrace.isp = r->isp;
					cputrace.intmask = r->intmask;
					cputrace.stopped = r->stopped;
					cputrace.state = 1;
					cputrace.pc = m68k_getpc ();

					cputrace.msp = r->msp;
					cputrace.vbr = r->vbr;
					cputrace.caar = r->caar;
					cputrace.cacr = r->cacr;
					cputrace.cacheholdingdata020 = r->cacheholdingdata020;
					cputrace.cacheholdingaddr020 = r->cacheholdingaddr020;
					cputrace.prefetch020addr = r->prefetch020addr;
					memcpy(&cputrace.prefetch020, &r->prefetch020, CPU_PIPELINE_MAX * sizeof (uae_u16));
					memcpy(&cputrace.prefetch020_valid, &r->prefetch020_valid, CPU_PIPELINE_MAX * sizeof(uae_u8));
					memcpy(&cputrace.caches020, &caches020, sizeof caches020);

					cputrace.memoryoffset = 0;
					cputrace.cyclecounter = cputrace.cyclecounter_pre = cputrace.cyclecounter_post = 0;
					cputrace.readcounter = cputrace.writecounter = 0;
				}

				if (inputrecord_debug & 4) {
					if (input_record > 0)
						inprec_recorddebug_cpu (1);
					else if (input_play > 0)
						inprec_playdebug_cpu (1);
				}

				if (debug_opcode_watch) {
					debug_trainer_match();
				}

				(*cpufunctbl[r->opcode])(r->opcode);
		
				wait_memory_cycles();

		cont:
				if (r->spcflags || time_for_interrupt ()) {
					if (do_specialties (0))
						exit = true;
				}

				regs.ipl = regs.ipl_pin;

			}
		} CATCH(prb) {
			bus_error();
			if (r->spcflags || time_for_interrupt()) {
				if (do_specialties(0))
					exit = true;
			}
			regs.ipl = regs.ipl_pin;
		} ENDTRY
	}
}

#ifdef CPUEMU_20

// full prefetch 020 (more compatible)
static void m68k_run_2p (void)
{
	struct regstruct *r = &regs;
	bool exit = false;
	bool first = true;

	while (!exit) {
		TRY(prb) {

			if (first) {
				if (cpu_tracer < 0) {
					memcpy (&r->regs, &cputrace.regs, 16 * sizeof (uae_u32));
					r->ir = cputrace.ir;
					r->irc = cputrace.irc;
					r->sr = cputrace.sr;
					r->usp = cputrace.usp;
					r->isp = cputrace.isp;
					r->intmask = cputrace.intmask;
					r->stopped = cputrace.stopped;

					r->msp = cputrace.msp;
					r->vbr = cputrace.vbr;
					r->caar = cputrace.caar;
					r->cacr = cputrace.cacr;
					r->cacheholdingdata020 = cputrace.cacheholdingdata020;
					r->cacheholdingaddr020 = cputrace.cacheholdingaddr020;
					r->prefetch020addr = cputrace.prefetch020addr;
					memcpy(&r->prefetch020, &cputrace.prefetch020, CPU_PIPELINE_MAX * sizeof(uae_u16));
					memcpy(&r->prefetch020_valid, &cputrace.prefetch020_valid, CPU_PIPELINE_MAX * sizeof(uae_u8));
					memcpy(&caches020, &cputrace.caches020, sizeof caches020);

					m68k_setpc (cputrace.pc);
					if (!r->stopped) {
						if (cputrace.state > 1)
							Exception (cputrace.state);
						else if (cputrace.state == 1)
							(*cpufunctbl[cputrace.opcode])(cputrace.opcode);
					}
					if (regs.stopped)
						set_special (SPCFLAG_STOP);
					set_cpu_tracer (false);
					goto cont;
				}
				set_cpu_tracer (false);
				first = false;
			}

			while (!exit) {
				r->instruction_pc = m68k_getpc ();
				r->opcode = regs.irc;

#if DEBUG_CD32CDTVIO
				out_cd32io (m68k_getpc ());
#endif

				if (cpu_tracer) {

#if CPUTRACE_DEBUG
					validate_trace ();
#endif
					memcpy (&cputrace.regs, &r->regs, 16 * sizeof (uae_u32));
					cputrace.opcode = r->opcode;
					cputrace.ir = r->ir;
					cputrace.irc = r->irc;
					cputrace.sr = r->sr;
					cputrace.usp = r->usp;
					cputrace.isp = r->isp;
					cputrace.intmask = r->intmask;
					cputrace.stopped = r->stopped;
					cputrace.state = 1;
					cputrace.pc = m68k_getpc ();

					cputrace.msp = r->msp;
					cputrace.vbr = r->vbr;
					cputrace.caar = r->caar;
					cputrace.cacr = r->cacr;
					cputrace.cacheholdingdata020 = r->cacheholdingdata020;
					cputrace.cacheholdingaddr020 = r->cacheholdingaddr020;
					cputrace.prefetch020addr = r->prefetch020addr;
					memcpy(&cputrace.prefetch020, &r->prefetch020, CPU_PIPELINE_MAX * sizeof(uae_u16));
					memcpy(&cputrace.prefetch020_valid, &r->prefetch020_valid, CPU_PIPELINE_MAX * sizeof(uae_u8));
					memcpy(&cputrace.caches020, &caches020, sizeof caches020);

					cputrace.memoryoffset = 0;
					cputrace.cyclecounter = cputrace.cyclecounter_pre = cputrace.cyclecounter_post = 0;
					cputrace.readcounter = cputrace.writecounter = 0;
				}

				if (inputrecord_debug & 4) {
					if (input_record > 0)
						inprec_recorddebug_cpu (1);
					else if (input_play > 0)
						inprec_playdebug_cpu (1);
				}

				if (debug_opcode_watch) {
					debug_trainer_match();
				}

				if (cpu_cycles > 0)
					x_do_cycles(cpu_cycles);

				if (currprefs.cpu_memory_cycle_exact) {

					(*cpufunctbl[r->opcode])(r->opcode);
					// 0% = no extra cycles
					cpu_cycles = 4 * CYCLE_UNIT * cycles_mult;
					cpu_cycles /= CYCLES_DIV;
					cpu_cycles -= CYCLE_UNIT;
					if (cpu_cycles <= 0)
						cpu_cycles = cpucycleunit;

				} else {

					cpu_cycles = (*cpufunctbl[r->opcode])(r->opcode);
					cpu_cycles = adjust_cycles (cpu_cycles);

				}
cont:
				if (r->spcflags) {
					if (do_specialties (cpu_cycles))
						exit = true;
				}
				ipl_fetch ();
			}
		} CATCH(prb) {
			bus_error();
			if (r->spcflags) {
				if (do_specialties(cpu_cycles))
					exit = true;
			}
			ipl_fetch();
		} ENDTRY
	}
}

#endif

#ifdef WITH_THREADED_CPU
static void *cpu_thread_run_2(void *v)
{
	bool exit = false;
	struct regstruct *r = &regs;

	cpu_thread_tid = uae_thread_get_id();

	cpu_thread_active = 1;
	while (!exit) {
		TRY(prb)
		{
			while (!exit) {
				r->instruction_pc = m68k_getpc();

				r->opcode = x_get_iword(0);

				(*cpufunctbl[r->opcode])(r->opcode);

				if (regs.spcflags || cpu_thread_ilvl > 0) {
					if (do_specialties_thread())
						exit = true;
				}

			}
		} CATCH(prb)
		{
			bus_error();
			if (r->spcflags) {
				if (do_specialties_thread())
					exit = true;
			}
		} ENDTRY
	}
	cpu_thread_active = 0;
	return 0;
}
#endif

/* Same thing, but don't use prefetch to get opcode.  */
static void m68k_run_2 (void)
{
#ifdef WITH_THREADED_CPU
	if (currprefs.cpu_thread) {
		run_cpu_thread(cpu_thread_run_2);
		return;
	}
#endif

	struct regstruct *r = &regs;
	bool exit = false;

	while (!exit) {
		TRY(prb) {
			while (!exit) {
				r->instruction_pc = m68k_getpc ();

				r->opcode = x_get_iword(0);
				count_instr (r->opcode);

				if (debug_opcode_watch) {
					debug_trainer_match();
				}
				do_cycles (cpu_cycles);

				cpu_cycles = (*cpufunctbl[r->opcode])(r->opcode);
				cpu_cycles = adjust_cycles (cpu_cycles);

				if (r->spcflags) {
					if (do_specialties (cpu_cycles))
						exit = true;
				}
			}
		} CATCH(prb) {
			bus_error();
			if (r->spcflags) {
				if (do_specialties(cpu_cycles))
					exit = true;
			}
		} ENDTRY
	}
}

/* fake MMU 68k  */
#if 0
static void m68k_run_mmu (void)
{
	for (;;) {
		regs.opcode = get_iiword (0);
		do_cycles (cpu_cycles);
		mmu_backup_regs = regs;
		cpu_cycles = (*cpufunctbl[regs.opcode])(regs.opcode);
		cpu_cycles = adjust_cycles (cpu_cycles);
		if (mmu_triggered)
			mmu_do_hit ();
		if (regs.spcflags) {
			if (do_specialties (cpu_cycles))
				return;
		}
	}
}
#endif

#endif /* CPUEMU_0 */

int in_m68k_go = 0;

#if 0
static void exception2_handle (uaecptr addr, uaecptr fault)
{
	last_addr_for_exception_3 = addr;
	last_fault_for_exception_3 = fault;
	last_writeaccess_for_exception_3 = 0;
	last_instructionaccess_for_exception_3 = 0;
	Exception (2);
}
#endif

static bool cpu_hardreset, cpu_keyboardreset;

bool is_hardreset(void)
{
	return cpu_hardreset;
}
bool is_keyboardreset(void)
{
	return  cpu_keyboardreset;
}

void m68k_go (int may_quit)
{
	int hardboot = 1;
	int startup = 1;

#ifdef WITH_THREADED_CPU
	init_cpu_thread();
#endif
	if (in_m68k_go || !may_quit) {
		write_log (_T("Bug! m68k_go is not reentrant.\n"));
		abort ();
	}

	reset_frame_rate_hack ();
	update_68k_cycles ();
	start_cycles = 0;

	set_cpu_tracer (false);

	cpu_prefs_changed_flag = 0;
	in_m68k_go++;
	for (;;) {
		void (*run_func)(void);

		cputrace.state = -1;

		if (regs.halted == CPU_HALT_ACCELERATOR_CPU_FALLBACK) {
			regs.halted = 0;
			cpu_do_fallback();
		}

		if (currprefs.inprecfile[0] && input_play) {
			inprec_open (currprefs.inprecfile, NULL);
			changed_prefs.inprecfile[0] = currprefs.inprecfile[0] = 0;
			quit_program = UAE_RESET;
		}
		if (input_play || input_record)
			inprec_startup ();

		if (quit_program > 0) {
			int restored = 0;
			cpu_keyboardreset = quit_program == UAE_RESET_KEYBOARD;
			cpu_hardreset = ((quit_program == UAE_RESET_HARD ? 1 : 0) | hardboot) != 0;

			if (quit_program == UAE_QUIT)
				break;

			hsync_counter = 0;
			vsync_counter = 0;
			quit_program = 0;
			hardboot = 0;

#ifdef SAVESTATE
			if (savestate_state == STATE_DORESTORE)
				savestate_state = STATE_RESTORE;
			if (savestate_state == STATE_RESTORE)
				restore_state (savestate_fname);
			else if (savestate_state == STATE_REWIND)
				savestate_rewind ();
#endif
			if (cpu_hardreset)
				m68k_reset_restore();
			prefs_changed_cpu();
			build_cpufunctbl();
			set_x_funcs();
			set_cycles (start_cycles);
			custom_reset (cpu_hardreset != 0, cpu_keyboardreset);
			m68k_reset2 (cpu_hardreset != 0);
			if (cpu_hardreset) {
				memory_clear ();
				write_log (_T("hardreset, memory cleared\n"));
			}
			cpu_hardreset = false;
#ifdef SAVESTATE
			/* We may have been restoring state, but we're done now.  */
			if (isrestore ()) {
				if (debug_dma) {
					record_dma_reset ();
					record_dma_reset ();
				}
				savestate_restore_finish ();
				memory_map_dump ();
				if (currprefs.mmu_model == 68030) {
					mmu030_decode_tc (tc_030, true);
				} else if (currprefs.mmu_model >= 68040) {
					mmu_set_tc (regs.tcr);
				}
				startup = 1;
				restored = 1;
			}
#endif
			if (currprefs.produce_sound == 0)
				eventtab[ev_audio].active = 0;
			m68k_setpc_normal (regs.pc);
			check_prefs_changed_audio ();

			if (!restored || hsync_counter == 0)
				savestate_check ();
			if (input_record == INPREC_RECORD_START)
				input_record = INPREC_RECORD_NORMAL;
			statusline_clear();
		} else {
			if (input_record == INPREC_RECORD_START) {
				input_record = INPREC_RECORD_NORMAL;
				savestate_init ();
				hsync_counter = 0;
				vsync_counter = 0;
				savestate_check ();
			}
		}

		if (changed_prefs.inprecfile[0] && input_record)
			inprec_prepare_record (savestate_fname[0] ? savestate_fname : NULL);

		if (changed_prefs.trainerfile[0])
			debug_init_trainer(changed_prefs.trainerfile);

		set_cpu_tracer (false);

#ifdef DEBUGGER
		if (debugging)
			debug ();
#endif
		if (regs.spcflags & SPCFLAG_MODE_CHANGE) {
			if (cpu_prefs_changed_flag & 1) {
				uaecptr pc = m68k_getpc();
				prefs_changed_cpu();
				fpu_modechange();
				custom_cpuchange();
				build_cpufunctbl();
				m68k_setpc_normal(pc);
				fill_prefetch();
			}
			if (cpu_prefs_changed_flag & 2) {
				fixup_cpu(&changed_prefs);
				currprefs.m68k_speed = changed_prefs.m68k_speed;
				currprefs.m68k_speed_throttle = changed_prefs.m68k_speed_throttle;
				update_68k_cycles();
				target_cpu_speed();
			}
			cpu_prefs_changed_flag = 0;
		}

		set_x_funcs();
		if (startup) {
			custom_prepare ();
			protect_roms (true);
		}
		startup = 0;
		event_wait = true;
		unset_special(SPCFLAG_MODE_CHANGE);

		if (!regs.halted) {
			// check that PC points to something that looks like memory.
			uaecptr pc = m68k_getpc();
			addrbank *ab = get_mem_bank_real(pc);
			if (ab == NULL || ab == &dummy_bank || (!currprefs.cpu_compatible && !valid_address(pc, 2)) || (pc & 1)) {
				cpu_halt(CPU_HALT_INVALID_START_ADDRESS);
			}
		}
		if (regs.halted) {
			cpu_halt (regs.halted);
			if (regs.halted < 0) {
				haltloop();
				continue;
			}
		}

#if 0
		if (mmu_enabled && !currprefs.cachesize) {
			run_func = m68k_run_mmu;
		} else {
#endif
			run_func = currprefs.cpu_cycle_exact && currprefs.cpu_model <= 68010 ? m68k_run_1_ce :
				currprefs.cpu_compatible && currprefs.cpu_model <= 68010 ? m68k_run_1 :
#ifdef JIT
				currprefs.cpu_model >= 68020 && currprefs.cachesize ? m68k_run_jit :
#endif
				currprefs.cpu_model == 68030 && currprefs.mmu_model ? m68k_run_mmu030 :
				currprefs.cpu_model == 68040 && currprefs.mmu_model ? m68k_run_mmu040 :
				currprefs.cpu_model == 68060 && currprefs.mmu_model ? m68k_run_mmu060 :

				currprefs.cpu_model >= 68040 && currprefs.cpu_cycle_exact ? m68k_run_3ce :
				currprefs.cpu_model >= 68020 && currprefs.cpu_cycle_exact ? m68k_run_2ce :

				currprefs.cpu_model <= 68020 && currprefs.cpu_compatible ? m68k_run_2p :
				currprefs.cpu_model == 68030 && currprefs.cpu_compatible ? m68k_run_2p :
				currprefs.cpu_model >= 68040 && currprefs.cpu_compatible ? m68k_run_3p :

				m68k_run_2;
#if 0
		}
#endif
		run_func();
	}
	protect_roms (false);
	in_m68k_go--;
}

#if 0
static void m68k_verify (uaecptr addr, uaecptr *nextpc)
{
	uae_u16 opcode, val;
	struct instr *dp;

	opcode = get_iword_1 (0);
	last_op_for_exception_3 = opcode;
	m68kpc_offset = 2;

	if (cpufunctbl[opcode] == op_illg_1) {
		opcode = 0x4AFC;
	}
	dp = table68k + opcode;

	if (dp->suse) {
		if (!verify_ea (dp->sreg, dp->smode, dp->size, &val)) {
			Exception (3, 0);
			return;
		}
	}
	if (dp->duse) {
		if (!verify_ea (dp->dreg, dp->dmode, dp->size, &val)) {
			Exception (3, 0);
			return;
		}
	}
}
#endif

static const TCHAR *ccnames[] =
{
	_T("T "),_T("F "),_T("HI"),_T("LS"),_T("CC"),_T("CS"),_T("NE"),_T("EQ"),
	_T("VC"),_T("VS"),_T("PL"),_T("MI"),_T("GE"),_T("LT"),_T("GT"),_T("LE")
};
static const TCHAR *fpccnames[] =
{
	_T("F"),
	_T("EQ"),
	_T("OGT"),
	_T("OGE"),
	_T("OLT"),
	_T("OLE"),
	_T("OGL"),
	_T("OR"),
	_T("UN"),
	_T("UEQ"),
	_T("UGT"),
	_T("UGE"),
	_T("ULT"),
	_T("ULE"),
	_T("NE"),
	_T("T"),
	_T("SF"),
	_T("SEQ"),
	_T("GT"),
	_T("GE"),
	_T("LT"),
	_T("LE"),
	_T("GL"),
	_T("GLE"),
	_T("NGLE"),
	_T("NGL"),
	_T("NLE"),
	_T("NLT"),
	_T("NGE"),
	_T("NGT"),
	_T("SNE"),
	_T("ST")
};
static const TCHAR *fpuopcodes[] =
{
	_T("FMOVE"),
	_T("FINT"),
	_T("FSINH"),
	_T("FINTRZ"),
	_T("FSQRT"),
	NULL,
	_T("FLOGNP1"),
	NULL,
	_T("FETOXM1"),
	_T("FTANH"),
	_T("FATAN"),
	NULL,
	_T("FASIN"),
	_T("FATANH"),
	_T("FSIN"),
	_T("FTAN"),
	_T("FETOX"),	// 0x10
	_T("FTWOTOX"),
	_T("FTENTOX"),
	NULL,
	_T("FLOGN"),
	_T("FLOG10"),
	_T("FLOG2"),
	NULL,
	_T("FABS"),
	_T("FCOSH"),
	_T("FNEG"),
	NULL,
	_T("FACOS"),
	_T("FCOS"),
	_T("FGETEXP"),
	_T("FGETMAN"),
	_T("FDIV"),		// 0x20
	_T("FMOD"),
	_T("FADD"),
	_T("FMUL"),
	_T("FSGLDIV"),
	_T("FREM"),
	_T("FSCALE"),
	_T("FSGLMUL"),
	_T("FSUB"),
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	_T("FSINCOS"),	// 0x30
	_T("FSINCOS"),
	_T("FSINCOS"),
	_T("FSINCOS"),
	_T("FSINCOS"),
	_T("FSINCOS"),
	_T("FSINCOS"),
	_T("FSINCOS"),
	_T("FCMP"),
	NULL,
	_T("FTST"),
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

static const TCHAR *movemregs[] =
{
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
	_T("FP0"),
	_T("FP1"),
	_T("FP2"),
	_T("FP3"),
	_T("FP4"),
	_T("FP5"),
	_T("FP6"),
	_T("FP7"),
	_T("FPIAR"),
	_T("FPSR"),
	_T("FPCR")
};

static void addmovemreg (TCHAR *out, int *prevreg, int *lastreg, int *first, int reg, int fpmode)
{
	TCHAR *p = out + _tcslen (out);
	if (*prevreg < 0) {
		*prevreg = reg;
		*lastreg = reg;
		return;
	}
	if (reg < 0 || fpmode == 2 || (*prevreg) + 1 != reg || (reg & 8) != ((*prevreg & 8))) {
		_stprintf (p, _T("%s%s"), (*first) ? _T("") : _T("/"), movemregs[*lastreg]);
		p = p + _tcslen (p);
		if (*lastreg != *prevreg) {
			if ((*lastreg) + 2 == reg) {
				_stprintf(p, _T("/%s"), movemregs[*prevreg]);
			} else if ((*lastreg) != (*prevreg)) {
				_stprintf(p, _T("-%s"), movemregs[*prevreg]);
			}
		}
		*lastreg = reg;
		*first = 0;
	}
	*prevreg = reg;
}

static bool movemout (TCHAR *out, uae_u16 mask, int mode, int fpmode, bool dst)
{
	unsigned int dmask, amask;
	int prevreg = -1, lastreg = -1, first = 1;

	if (mode == Apdi && !fpmode) {
		uae_u8 dmask2;
		uae_u8 amask2;
		
		amask2 = mask & 0xff;
		dmask2 = (mask >> 8) & 0xff;
		dmask = 0;
		amask = 0;
		for (int i = 0; i < 8; i++) {
			if (dmask2 & (1 << i))
				dmask |= 1 << (7 - i);
			if (amask2 & (1 << i))
				amask |= 1 << (7 - i);
		}
	} else {
		dmask = mask & 0xff;
		amask = (mask >> 8) & 0xff;
		if (fpmode == 1 && mode != Apdi) {
			uae_u8 dmask2 = dmask;
			dmask = 0;
			for (int i = 0; i < 8; i++) {
				if (dmask2 & (1 << i))
					dmask |= 1 << (7 - i);
			}
		}
	}
	bool dataout = dmask != 0 || amask != 0;
	if (dst && dataout)
		_tcscat(out, _T(","));
	if (fpmode) {
		while (dmask) { addmovemreg(out, &prevreg, &lastreg, &first, movem_index1[dmask] + (fpmode == 2 ? 24 : 16), fpmode); dmask = movem_next[dmask]; }
	} else {
		while (dmask) { addmovemreg (out, &prevreg, &lastreg, &first, movem_index1[dmask], fpmode); dmask = movem_next[dmask]; }
		while (amask) { addmovemreg (out, &prevreg, &lastreg, &first, movem_index1[amask] + 8, fpmode); amask = movem_next[amask]; }
	}
	addmovemreg(out, &prevreg, &lastreg, &first, -1, fpmode);
	return dataout;
}

static void disasm_size (TCHAR *instrname, struct instr *dp)
{
	if (dp->unsized) {
		_tcscat(instrname, _T(" "));
		return;
	}
	switch (dp->size)
	{
	case sz_byte:
		_tcscat (instrname, _T(".B "));
		break;
	case sz_word:
		_tcscat (instrname, _T(".W "));
		break;
	case sz_long:
		_tcscat (instrname, _T(".L "));
		break;
	default:
		_tcscat (instrname, _T(" "));
		break;
	}
}

static void asm_add_extensions(uae_u16 *data, int *dcntp, int mode, uae_u32 v, int extcnt, uae_u16 *ext, uaecptr pc, int size)
{
	int dcnt = *dcntp;
	if (mode < 0)
		return;
	if (mode == Ad16) {
		data[dcnt++] = v;
	}
	if (mode == PC16) {
		data[dcnt++] = v - (pc + 2);
	}
	if (mode == Ad8r || mode == PC8r) {
		for (int i = 0; i < extcnt; i++) {
			data[dcnt++] = ext[i];
		}
	}
	if (mode == absw) {
		data[dcnt++] = (uae_u16)v;
	}
	if (mode == absl) {
		data[dcnt++] = (uae_u16)(v >> 16);
		data[dcnt++] = (uae_u16)v;
	}
	if ((mode == imm && size == 0) || mode == imm0) {
		data[dcnt++] = (uae_u8)v;
	}
	if ((mode == imm && size == 1) || mode == imm1) {
		data[dcnt++] = (uae_u16)v;
	}
	if ((mode == imm && size == 2) || mode == imm2) {
		data[dcnt++] = (uae_u16)(v >> 16);
		data[dcnt++] = (uae_u16)v;
	}
	*dcntp = dcnt;
}

static int asm_isdreg(const TCHAR *s)
{
	if (s[0] == 'D' && s[1] >= '0' && s[1] <= '7')
		return s[1] - '0';
	return -1;
}
static int asm_isareg(const TCHAR *s)
{
	if (s[0] == 'A' && s[1] >= '0' && s[1] <= '7')
		return s[1] - '0';
	if (s[0] == 'S' && s[1] == 'P')
		return 7;
	return -1;
}
static int asm_ispc(const TCHAR *s)
{
	if (s[0] == 'P' && s[1] == 'C')
		return 1;
	return 0;
}

static uae_u32 asmgetval(const TCHAR *s)
{
	TCHAR *endptr;
	if (s[0] == '-')
		return _tcstol(s, &endptr, 16);
	return _tcstoul(s, &endptr, 16);
}

static int asm_parse_mode020(TCHAR *s, uae_u8 *reg, uae_u32 *v, int *extcnt, uae_u16 *ext)
{
	return -1;
}

static int asm_parse_mode(TCHAR *s, uae_u8 *reg, uae_u32 *v, int *extcnt, uae_u16 *ext)
{
	TCHAR *ss = s;
	*reg = -1;
	*v = 0;
	*ext = 0;
	*extcnt = 0;
	if (s[0] == 0)
		return -1;
	// Dn
	if (asm_isdreg(s) >= 0 && s[2] == 0) {
		*reg = asm_isdreg(s);
		return Dreg;
	}
	// An
	if (asm_isareg(s) >= 0 && s[2] == 0) {
		*reg = asm_isareg(s);
		return Areg;
	}
	// (An) and (An)+
	if (s[0] == '(' && asm_isareg(s + 1) >= 0 && s[3] == ')') {
		*reg = asm_isareg(s + 1);
		if (s[4] == '+' && s[5] == 0)
			return Aipi;
		if (s[4] == 0)
			return Aind;
		return -1;
	}
	// -(An)
	if (s[0] == '-' && s[1] == '(' && asm_isareg(s + 2) >= 0 && s[4] == ')' && s[5] == 0) {
		*reg = asm_isareg(s + 2);
		return Apdi;
	}
	// Immediate
	if (s[0] == '#') {
		if (s[1] == '!') {
			*v = _tstol(s + 2);
		} else {
			*v = asmgetval(s + 1);
		}
		return imm;
	}
	// Value
	if (s[0] == '!') {
		*v = _tstol(s + 1);
	} else {
		*v = asmgetval(s);
	}
	int dots = 0;
	int fullext = 0;
	for (int i = 0; i < _tcslen(s); i++) {
		if (s[i] == ',') {
			dots++;
		} else if (s[i] == '[') {
			if (fullext > 0)
				fullext = -1;
			else
				fullext = 1;
		} else if (s[i] == ']') {
			if (fullext != 1)
				fullext = -1;
			else
				fullext = 2;
			fullext++;
		}
	}
	if (fullext < 0 || fullext == 1)
		return -1;
	if (fullext == 2) {
		return asm_parse_mode020(s, reg, v, extcnt, ext);
	}
	while (*s != 0) {
		// d16(An)
		if (dots == 0 && s[0] == '(' && asm_isareg(s + 1) >= 0 && s[3] == ')' && s[4] == 0) {
			*reg = asm_isareg(s + 1);
			return Ad16;
		}
		// d16(PC)
		if (dots == 0 && s[0] == '(' && asm_ispc(s + 1) && s[3] == ')' && s[4] == 0) {
			*reg = 2;
			return PC16;
		}
		// (d16,An) / (d16,PC)
		if (dots == 1 && s[0] == '(' && !asm_ispc(s + 1) && asm_isareg(s + 1) < 0 && asm_isdreg(s + 1) < 0) {
			TCHAR *startptr, *endptr;
			if (s[1] == '!') {
				startptr = s + 2;
				*v = _tcstol(startptr, &endptr, 10);
			} else {
				startptr = s + 1;
				*v = _tcstol(startptr, &endptr, 16);
			}
			if (endptr == startptr || endptr[0] != ',')
				return -1;
			if (asm_ispc(endptr + 1) && endptr[3] == ')') {
				*reg = 2;
				return PC16;
			}
			if (asm_isareg(endptr + 1) >= 0 && endptr[3] == ')') {
				*reg = asm_isareg(endptr + 1);
				return Ad16;
			}
			return -1;
		}
		// Ad8r PC8r
		if (s[0] == '(') {
			TCHAR *s2 = s;
			if (!asm_ispc(s + 1) && asm_isareg(s + 1) < 0 && asm_isdreg(s + 1) < 0) {
				if (dots != 2)
					return -1;
				TCHAR *startptr, *endptr;
				if (s[1] == '!') {
					startptr = s + 2;
					*v = _tcstol(startptr, &endptr, 10);
				} else {
					startptr = s + 1;
					*v = _tcstol(startptr, &endptr, 16);
				}
				if (endptr == startptr || endptr[0] != ',')
					return -1;
				s2 = endptr + 1;
			} else if (((asm_isareg(s + 1) >= 0 || asm_ispc(s + 1)) && s[3] == ',') || (asm_isdreg(s + 4) >= 0 || asm_isareg(s + 4) >= 0)) {
				if (dots != 1)
					return -1;
				s2 = s + 1;
			} else {
				return -1;
			}
			uae_u8 reg2;
			bool ispc = asm_ispc(s2);
			if (ispc) {
				*reg = 3;
			} else {
				*reg = asm_isareg(s2);
			}
			*extcnt = 1;
			s2 += 2;
			if (*s2 != ',')
				return -1;
			s2++;
			if (asm_isdreg(s2) >= 0) {
				reg2 = asm_isdreg(s2);
			} else {
				reg2 = asm_isareg(s2);
				*ext |= 1 << 15;
			}
			s2 += 2;
			*ext |= reg2 << 12;
			*ext |= (*v) & 0xff;
			if (s2[0] == '.' && s2[1] == 'W') {
				s2 += 2;
			} else if (s2[0] == '.' && s2[1] == 'L') {
				*ext |= 1 << 11;
				s2 += 2;
			}
			if (s2[0] == '*') {
				TCHAR scale = s2[1];
				if (scale == '2')
					*ext |= 1 << 9;
				else if (scale == '4')
					*ext |= 2 << 9;
				else if (scale == '8')
					*ext |= 3 << 9;
				else
					return -1;
				s2 += 2;
			}
			if (s2[0] == ')' && s2[1] == 0) {
				return ispc ? PC8r : Ad8r;
			}
			return -1;
		}
		s++;
	}
	// abs.w
	if (s - ss > 2 && s[-2] == '.' && s[-1] == 'W') {
		*reg = 0;
		return absw;
	}
	// abs.l
	*reg = 1;
	return absl;
}

static TCHAR *asm_parse_parm(TCHAR *parm, TCHAR *out)
{
	TCHAR *p = parm;
	bool quote = false;

	for (;;) {
		if (*p == '(') {
			quote = true;
		}
		if (*p == ')') {
			if (!quote)
				return NULL;
			quote = false;
		}
		if ((*p == ',' || *p == 0) && !quote) {
			TCHAR c = *p;
			p[0] = 0;
			_tcscpy(out, parm);
			my_trim(out);
			if (c)
				p++;
			return p;
		}
		p++;
	}
}

static bool m68k_asm_parse_movec(TCHAR *s, TCHAR *d)
{
	for (int i = 0; m2cregs[i].regname; i++) {
		if (!_tcscmp(s, m2cregs[i].regname)) {
			uae_u16 v = m2cregs[i].regno;
			if (asm_isareg(d) >= 0)
				v |= 0x8000 | (asm_isareg(d) << 12);
			else if (asm_isdreg(d) >= 0)
				v |= (asm_isdreg(d) << 12);
			else
				return false;
			_stprintf(s, _T("#%X"), v);
			return true;
		}
	}
	return false;
}

static bool m68k_asm_parse_movem(TCHAR *s, int dir)
{
	TCHAR *d = s;
	uae_u16 regmask = 0;
	uae_u16 mask = dir ? 0x8000 : 0x0001;
	bool ret = false;
	while(*s) {
		int dreg = asm_isdreg(s);
		int areg = asm_isareg(s);
		if (dreg < 0 && areg < 0)
			break;
		int reg = dreg >= 0 ? dreg : areg + 8;
		regmask |= dir ? (mask >> reg) : (mask << reg);
		s += 2;
		if (*s == 0) {
			ret = true;
			break;
		} else if (*s == '/') {
			s++;
			continue;
		} else if (*s == '-') {
			s++;
			int dreg2 = asm_isdreg(s);
			int areg2 = asm_isareg(s);
			if (dreg2 < 0 && areg2 < 0)
				break;
			int reg2 = dreg2 >= 0 ? dreg2 : areg2 + 8;
			if (reg2 < reg)
				break;
			while (reg2 >= reg) {
				regmask |= dir ? (mask >> reg) : (mask << reg);
				reg++;
			}
			s += 2;
			if (*s == 0) {
				ret = true;
				break;
			}
		} else {
			break;
		}
	}
	if (ret)
		_stprintf(d, _T("#%X"), regmask);
	return ret;
}

int m68k_asm(TCHAR *sline, uae_u16 *out, uaecptr pc)
{
	TCHAR *p;
	const TCHAR *cp1;
	TCHAR ins[256], parms[256];
	TCHAR line[256];
	TCHAR srcea[256], dstea[256];
	uae_u16 data[16], sexts[8], dexts[8];
	int sextcnt, dextcnt;
	int dcnt = 0;
	int cc = -1;
	int quick = 0;
	bool immrelpc = false;

	if (_tcslen(sline) > 100)
		return -1;

	srcea[0] = dstea[0] = 0;
	parms[0] = 0;

	// strip all white space except first space
	p = line;
	bool firstsp = true;
	for (int i = 0; sline[i]; i++) {
		TCHAR c = sline[i];
		if (c == 32 && firstsp) {
			firstsp = false;
			*p++ = 32;
		}
		if (c <= 32)
			continue;
		*p++ = c;
	}
	*p = 0;

	to_upper(line, _tcslen(line));

	p = line;
	while (*p && *p != ' ')
		p++;
	if (*p == ' ') {
		*p = 0;
		_tcscpy(parms, p + 1);
		my_trim(parms);
	}
	_tcscpy(ins, line);
	
	if (_tcslen(ins) == 0)
		return 0;

	int size = 1;
	int inssize = -1;
	cp1 = _tcschr(line, '.');
	if (cp1) {
		size = cp1[1];
		if (size == 'W')
			size = 1;
		else if (size == 'L')
			size = 2;
		else if (size == 'B')
			size = 0;
		else
			return 0;
		inssize = size;
		line[cp1 - line] = 0;
		_tcscpy(ins, line);
	}

	TCHAR *parmp = parms;
	parmp = asm_parse_parm(parmp, srcea);
	if (!parmp)
		return 0;
	if (srcea[0]) {
		parmp = asm_parse_parm(parmp, dstea);
		if (!parmp)
			return 0;
	}

	int smode = -1;
	int dmode = -1;
	uae_u8 sreg = -1;
	uae_u8 dreg = -1;
	uae_u32 sval = 0;
	uae_u32 dval = 0;
	int ssize = -1;
	int dsize = -1;

	dmode = asm_parse_mode(dstea, &dreg, &dval, &dextcnt, dexts);


	// Common alias
	if (!_tcscmp(ins, _T("BRA"))) {
		_tcscpy(ins, _T("BT"));
	} else if (!_tcscmp(ins, _T("BSR"))) {
		immrelpc = true;
	} else if (!_tcscmp(ins, _T("MOVEM"))) {
		if (dmode >= Aind && _tcschr(dstea, '-') == NULL && _tcschr(dstea, '/') == NULL) {
			_tcscpy(ins, _T("MVMLE"));
			if (!m68k_asm_parse_movem(srcea, dmode == Apdi))
				return -1;
		} else {
			TCHAR tmp[256];
			_tcscpy(ins, _T("MVMEL"));
			_tcscpy(tmp, srcea);
			_tcscpy(srcea, dstea);
			_tcscpy(dstea, tmp);
			if (!m68k_asm_parse_movem(srcea, 0))
				return -1;
			dmode = asm_parse_mode(dstea, &dreg, &dval, &dextcnt, dexts);
		}
	} else if (!_tcscmp(ins, _T("MOVEC"))) {
		if (dmode == Dreg || dmode == Areg) {
			_tcscpy(ins, _T("MOVEC2"));
			if (!m68k_asm_parse_movec(srcea, dstea))
				return -1;
		} else {
			TCHAR tmp[256];
			_tcscpy(ins, _T("MOVE2C"));
			_tcscpy(tmp, srcea);
			_tcscpy(srcea, dstea);
			dstea[0] = 0;
			if (!m68k_asm_parse_movec(srcea, tmp))
				return -1;
		}
		dmode = -1;
	}
	
	if (dmode == Areg) {
		int l = _tcslen(ins);
		if (l <= 2)
			return -1;
		TCHAR last = ins[l- 1];
		if (last == 'Q') {
			last = ins[l - 2];
			if (last != 'A') {
				ins[l - 1] = 'A';
				ins[l] = 'Q';
				ins[l + 1] = 0;
			}
		} else if (last != 'A') {
			_tcscat(ins, _T("A"));
		}
	}

	bool fp = ins[0] == 'F';

	if (ins[_tcslen(ins) - 1] == 'Q' && _tcslen(ins) > 3 && !fp) {
		quick = 1;
		ins[_tcslen(ins) - 1] = 0;
	}

	struct mnemolookup *lookup;
	for (lookup = lookuptab; lookup->name; lookup++) {
		if (!_tcscmp(ins, lookup->name))
			break;
	}
	if (!lookup->name) {
		// Check cc variants
		for (lookup = lookuptab; lookup->name; lookup++) {
			const TCHAR *ccp = _tcsstr(lookup->name, _T("cc"));
			if (ccp) {
				TCHAR tmp[256];
				for (int i = 0; i < (fp ? 32 : 16); i++) {
					const TCHAR *ccname = fp ? fpccnames[i] : ccnames[i];
					_tcscpy(tmp, lookup->name);
					_tcscpy(tmp + (ccp - lookup->name), ccname);
					if (tmp[_tcslen(tmp) - 1] == ' ')
						tmp[_tcslen(tmp) - 1] = 0;
					if (!_tcscmp(tmp, ins)) {
						_tcscpy(ins, lookup->name);
						cc = i;
						if (lookup->mnemo == i_DBcc || lookup->mnemo == i_Bcc) {
							// Bcc.B uses same encoding mode as MOVEQ
							immrelpc = true;
						}
						if (size == 0) {
							quick = 2;
						}
						break;
					}
				}
			}
			if (cc >= 0)
				break;
		}
	}

	if (!lookup->name)
		return 0;

	int mnemo = lookup->mnemo;

	int found = 0;
	int sizemask = 0;
	int tsize = size;
	int unsized = 0;

	for (int round = 0; round < 9; round++) {

		if (!found && round == 8)
			return 0;

		if (round == 3) {
			// Q is always LONG sized
			if (quick == 1) {
				tsize = 2;
			}
			bool isimm = srcea[0] == '#';
			if (immrelpc && !isimm) {
				TCHAR tmp[256];
				_tcscpy(tmp, srcea);
				srcea[0] = '#';
				_tcscpy(srcea + 1, tmp);
			}
			smode = asm_parse_mode(srcea, &sreg, &sval, &sextcnt, sexts);
			if (immrelpc && !isimm) {
				sval = sval - (pc + 2);
			}
			if (quick) {
				smode = immi;
				sreg = sval & 0xff;
			}
		}

		if (round == 1) {
			if (!quick && (sizemask == 1 || sizemask == 2 || sizemask == 4)) {
				tsize = 0;
				if (sizemask == 2)
					tsize = 1;
				else if (sizemask == 4)
					tsize = 2;
			} else {
				continue;
			}
		}
		if (round == 2 && !found) {
			unsized = 1;
		}

		if (round == 4 && smode == imm) {
			smode = imm0;
		} else if (round == 5 && smode == imm0) {
			smode = imm1;
		} else if (round == 6 && smode == imm1) {
			smode = imm2;
		} else if (round == 7 && smode == imm2) {
			smode = immi;
			sreg = sval & 0xff;
		} else if (round == 4) {
			round += 5 - 1;
		}

		for (int opcode = 0; opcode < 65536; opcode++) {
			struct instr *table = &table68k[opcode];
			if (table->mnemo != mnemo)
				continue;
			if (cc >= 0 && table->cc != cc)
				continue;

#if 0
			if (round == 0) {
				console_out_f(_T("%s OP=%04x S=%d SR=%d SM=%d SU=%d SP=%d DR=%d DM=%d DU=%d DP=%d SDU=%d\n"), lookup->name, opcode, table->size,
					table->sreg, table->smode, table->suse, table->spos,
					table->dreg, table->dmode, table->duse, table->dpos,
					table->sduse);
			}
#endif

			if (table->duse && !(table->dmode == dmode || (dmode >= imm && dmode <= imm2 && table->dmode >= imm && table->dmode <= imm2)))
				continue;
			if (round == 0) {
				sizemask |= 1 << table->size;
			}
			if (unsized > 0 && !table->unsized) {
				continue;
			}

			found++;

			if (round >= 3) {

				if (
					((table->size == tsize || table->unsized)) &&
					((!table->suse && smode < 0) || (table->suse && table->smode == smode)) &&
					((!table->duse && dmode < 0) || (table->duse && (table->dmode == dmode || (dmode == imm && (table->dmode >= imm && table->dmode <= imm2))))) &&
					((table->sreg == sreg || (table->smode >= absw && table->smode != immi))) &&
					((table->dreg == dreg || table->dmode >= absw))
					)
				{
					if (inssize >= 0 && tsize != inssize)
						continue;


					data[dcnt++] = opcode;
					asm_add_extensions(data, &dcnt, smode, sval, sextcnt, sexts, pc, tsize);
					if (smode >= 0)
						asm_add_extensions(data, &dcnt, dmode, dval, dextcnt, dexts, pc, tsize);
					for (int i = 0; i < dcnt; i++) {
						out[i] = data[i];
					}
					return dcnt;
				}

			}
		}
	}

	return 0;
}

static void resolve_if_jmp(TCHAR *s, uae_u32 addr)
{
	uae_u16 opcode = get_word_debug(addr);
	if (opcode == 0x4ef9) { // JMP x.l
		TCHAR *p = s + _tcslen(s);
		uae_u32 addr2 = get_long_debug(addr + 2);
		_stprintf(p, _T(" == $%08x "), addr2);
		showea_val(p + _tcslen(p), opcode, addr2, 4);
		TCHAR txt[256];
		bool ext;
		if (debugmem_get_segment(addr2, NULL, &ext, NULL, txt)) {
			if (ext) {
				_tcscat(p, _T(" "));
				_tcscat(p, txt);
			}
		}
	}
}

static bool mmu_op30_helper_get_fc(uae_u16 extra, TCHAR *out)
{
	switch (extra & 0x0018) {
	case 0x0010:
		_stprintf(out, _T("#%d"), extra & 7);
		return true;
	case 0x0008:
		_stprintf(out, _T("D%d"), extra & 7);
		return true;
	case 0x0000:
		if (extra & 1) {
			_tcscpy(out, _T("DFC"));
		} else {
			_tcscpy(out, _T("SFC"));
		}
		return true;
	default:
		return false;
	}
}

static uaecptr disasm_mmu030(uaecptr pc, uae_u16 opcode, uae_u16 extra, struct instr *dp, TCHAR *instrname, uae_u32 *seaddr2, int safemode)
{
	int type = extra >> 13;

	_tcscpy(instrname, _T("F-LINE (MMU 68030)"));
	pc += 2;

	switch (type)
	{
	case 0:
	case 2:
	case 3:
	{
		// PMOVE
		int preg = (extra >> 10) & 31;
		int rw = (extra >> 9) & 1;
		int fd = (extra >> 8) & 1;
		int unused = (extra & 0xff);
		const TCHAR *r = NULL;

		if (mmu_op30_invea(opcode))
			break;
		if (unused)
			break;
		if (rw && fd)
			break;
		switch (preg)
		{
		case 0x10:
			r = _T("TC");
			break;
		case 0x12:
			r = _T("SRP");
			break;
		case 0x13:
			r = _T("CRP");
			break;
		case 0x18:
			r = _T("MMUSR");
			break;
		case 0x02:
			r = _T("TT0");
			break;
		case 0x03:
			r = _T("TT1");
			break;
		}
		if (!r)
			break;

		_tcscpy(instrname, _T("PMOVE"));
		if (fd)
			_tcscat(instrname, _T("FD"));
		_tcscat(instrname, _T(" "));

		if (!rw) {
			pc = ShowEA(NULL, pc, opcode, dp->sreg, dp->smode, dp->size, instrname, seaddr2, safemode);
			_tcscat(instrname, _T(","));
		}
		_tcscat(instrname, r);
		if (rw) {
			_tcscat(instrname, _T(","));
			pc = ShowEA(NULL, pc, opcode, dp->sreg, dp->smode, dp->size, instrname, seaddr2, safemode);
		}
		break;
	}
	case 1:
	{
		// PLOAD/PFLUSH
		uae_u16 mode = (extra >> 8) & 31;
		int unused = (extra & (0x100 | 0x80 | 0x40 | 0x20));
		uae_u16 fc_mask = (extra & 0x00E0) >> 5;
		uae_u16 fc_bits = extra & 0x7f;
		TCHAR fc[10];

		if (unused)
			break;

		switch (mode) {
		case 0x00: // PLOAD W
		case 0x02: // PLOAD R
			if (mmu_op30_invea(opcode))
				break;
			_stprintf(instrname, _T("PLOAD%c %s,"), mode == 0 ? 'W' : 'R', fc);
			pc = ShowEA(NULL, pc, opcode, dp->sreg, dp->smode, dp->size, instrname, seaddr2, safemode);
			break;
		case 0x04: // PFLUSHA
			if (fc_bits)
				break;
			_tcscpy(instrname, _T("PFLUSHA"));
			break;
		case 0x10: // FC
			if (!mmu_op30_helper_get_fc(extra, fc))
				break;
			_stprintf(instrname, _T("PFLUSH %s,%d"), fc, fc_mask);
			break;
		case 0x18: // FC + EA
			if (mmu_op30_invea(opcode))
				break;
			if (!mmu_op30_helper_get_fc(extra, fc))
				break;
			_stprintf(instrname, _T("PFLUSH %s,%d"), fc, fc_mask);
			_tcscat(instrname, _T(","));
			pc = ShowEA(NULL, pc, opcode, dp->sreg, dp->smode, dp->size, instrname, seaddr2, safemode);
			break;
		}
		break;
	}
	case 4:
	{
		// PTEST
		int level = (extra & 0x1C00) >> 10;
		int rw = (extra >> 9) & 1;
		int a = (extra >> 8) & 1;
		int areg = (extra & 0xE0) >> 5;
		TCHAR fc[10];

		if (mmu_op30_invea(opcode))
			break;
		if (!mmu_op30_helper_get_fc(extra, fc))
			break;
		if (!level && a)
			break;
		_stprintf(instrname, _T("PTEST%c %s,"), rw ? 'R' : 'W', fc);
		pc = ShowEA(NULL, pc, opcode, dp->sreg, dp->smode, dp->size, instrname, seaddr2, safemode);
		_stprintf(instrname + _tcslen(instrname), _T(",#%d"), level);
		if (a)
			_stprintf(instrname + _tcslen(instrname), _T(",A%d"), areg);
		break;
	}
	}
	return pc;
}


void m68k_disasm_2 (TCHAR *buf, int bufsize, uaecptr pc, uaecptr *nextpc, int cnt, uae_u32 *seaddr, uae_u32 *deaddr, uaecptr lastpc, int safemode)
{
	uae_u32 seaddr2;
	uae_u32 deaddr2;

	if (!table68k)
		return;
	while (cnt-- > 0) {
		TCHAR instrname[256], *ccpt;
		TCHAR segout[256], segname[256];
		int i;
		uae_u32 opcode;
		uae_u16 extra;
		struct mnemolookup *lookup;
		struct instr *dp;
		uaecptr oldpc;
		uaecptr m68kpc_illg = 0;
		bool illegal = false;
		int segid, lastsegid;
		TCHAR *symbolpos;

		seaddr2 = deaddr2 = 0;
		oldpc = pc;
		opcode = get_word_debug (pc);
		extra = get_word_debug (pc + 2);
		if (cpufunctbl[opcode] == op_illg_1 || cpufunctbl[opcode] == op_unimpl_1) {
			m68kpc_illg = pc + 2;
			illegal = TRUE;
		}

		dp = table68k + opcode;
		if (dp->mnemo == i_ILLG) {
			illegal = FALSE;
			opcode = 0x4AFC;
			dp = table68k + opcode;
		}
		for (lookup = lookuptab;lookup->mnemo != dp->mnemo; lookup++)
			;

		lastsegid = -1;
		bool exact = false;
		if (lastpc != 0xffffffff) {
			lastsegid = debugmem_get_segment(lastpc, NULL, NULL, NULL, NULL);
		}
		segid = debugmem_get_segment(pc, &exact, NULL, segout, segname);
		if (segid && (lastsegid != -1 || exact) && (segid != lastsegid || pc == lastpc || exact)) {
			buf = buf_out(buf, &bufsize, _T("%s\n"), segname);
		}
		symbolpos = buf;

		buf = buf_out (buf, &bufsize, _T("%08X "), pc);

		if (segid) {
			buf = buf_out(buf, &bufsize, _T("%s "), segout);
		}

		pc += 2;
		
		if (lookup->friendlyname)
			_tcscpy (instrname, lookup->friendlyname);
		else
			_tcscpy (instrname, lookup->name);
		ccpt = _tcsstr (instrname, _T("cc"));
		if (ccpt != 0) {
			if ((opcode & 0xf000) == 0xf000)
				_tcscpy (ccpt, fpccnames[extra & 0x1f]);
			else
				_tcsncpy (ccpt, ccnames[dp->cc], 2);
		}
		disasm_size (instrname, dp);

		if (lookup->mnemo == i_MOVEC2 || lookup->mnemo == i_MOVE2C) {
			uae_u16 imm = extra;
			uae_u16 creg = imm & 0x0fff;
			uae_u16 r = imm >> 12;
			TCHAR regs[16];
			const TCHAR *cname = _T("?");
			int j;
			for (j = 0; m2cregs[j].regname; j++) {
				if (m2cregs[j].regno == creg)
					break;
			}
			_stprintf(regs, _T("%c%d"), r >= 8 ? 'A' : 'D', r >= 8 ? r - 8 : r);
			if (m2cregs[j].regname)
				cname = m2cregs[j].regname;
			if (lookup->mnemo == i_MOVE2C) {
				_tcscat(instrname, regs);
				_tcscat(instrname, _T(","));
				_tcscat(instrname, cname);
			} else {
				_tcscat(instrname, cname);
				_tcscat(instrname, _T(","));
				_tcscat(instrname, regs);
			}
			pc += 2;
		} else if (lookup->mnemo == i_CHK2) {
			TCHAR *p;
			if (!(extra & 0x0800)) {
				instrname[1] = 'M';
				instrname[2] = 'P';
			}
			pc = ShowEA(NULL, pc, opcode, dp->dreg, dp->dmode, dp->size, instrname, &seaddr2, safemode);
			extra = get_word_debug(pc);
			pc += 2;
			p = instrname + _tcslen(instrname);
			_stprintf(p, (extra & 0x8000) ? _T(",A%d") : _T(",D%d"), (extra >> 12) & 7);
		} else if (lookup->mnemo == i_ORSR || lookup->mnemo == i_ANDSR || lookup->mnemo == i_EORSR) {
			pc = ShowEA(NULL, pc, opcode, dp->sreg, dp->smode, dp->size, instrname, &seaddr2, safemode);
			_tcscat(instrname, dp->size == sz_byte ? _T(",CCR") : _T(",SR"));
		} else if (lookup->mnemo == i_MVR2USP) {
			pc = ShowEA(NULL, pc, opcode, dp->sreg, dp->smode, dp->size, instrname, &seaddr2, safemode);
			_tcscat(instrname, _T(",USP"));
		} else if (lookup->mnemo == i_MVUSP2R) {
			_tcscat(instrname, _T("USP,"));
			pc = ShowEA(NULL, pc, opcode, dp->sreg, dp->smode, dp->size, instrname, &seaddr2, safemode);
		} else if (lookup->mnemo == i_MV2SR) {
			pc = ShowEA(NULL, pc, opcode, dp->sreg, dp->smode, dp->size, instrname, &seaddr2, safemode);
			_tcscat(instrname, dp->size == sz_byte ? _T(",CCR") : _T(",SR"));
		} else if (lookup->mnemo == i_MVSR2) {		
			_tcscat(instrname, dp->size == sz_byte ? _T("CCR,") : _T("SR,"));
			pc = ShowEA(NULL, pc, opcode, dp->sreg, dp->smode, dp->size, instrname, &seaddr2, safemode);
		} else if (lookup->mnemo == i_MVMEL) {
			uae_u16 mask = extra;
			pc += 2;
			pc = ShowEA (NULL, pc, opcode, dp->dreg, dp->dmode, dp->size, instrname, deaddr, safemode);
			movemout (instrname, mask, dp->dmode, 0, true);
		} else if (lookup->mnemo == i_MVMLE) {
			uae_u16 mask = extra;
			pc += 2;
			if (movemout(instrname, mask, dp->dmode, 0, false))
				_tcscat(instrname, _T(","));
			pc = ShowEA(NULL, pc, opcode, dp->dreg, dp->dmode, dp->size, instrname, deaddr, safemode);
		} else if (lookup->mnemo == i_DIVL || lookup->mnemo == i_MULL) {
			TCHAR *p;
			pc = ShowEA(NULL, pc, opcode, dp->dreg, dp->dmode, dp->size, instrname, &seaddr2, safemode);
			extra = get_word_debug(pc);
			pc += 2;
			p = instrname + _tcslen(instrname);
			if (extra & 0x0400)
				_stprintf(p, _T(",D%d:D%d"), extra & 7, (extra >> 12) & 7);
			else
				_stprintf(p, _T(",D%d"), (extra >> 12) & 7);
		} else if (lookup->mnemo == i_MOVES) {
			TCHAR *p;
			pc += 2;
			if (!(extra & 0x0800)) {
				pc = ShowEA(NULL, pc, opcode, dp->dreg, dp->dmode, dp->size, instrname, &seaddr2, safemode);
				p = instrname + _tcslen(instrname);
				_stprintf(p, _T(",%c%d"), (extra & 0x8000) ? 'A' : 'D', (extra >> 12) & 7);
			} else {
				p = instrname + _tcslen(instrname);
				_stprintf(p, _T("%c%d,"), (extra & 0x8000) ? 'A' : 'D', (extra >> 12) & 7);
				pc = ShowEA(NULL, pc, opcode, dp->dreg, dp->dmode, dp->size, instrname, &seaddr2, safemode);
			}
		} else if (lookup->mnemo == i_BFEXTS || lookup->mnemo == i_BFEXTU ||
				   lookup->mnemo == i_BFCHG || lookup->mnemo == i_BFCLR ||
				   lookup->mnemo == i_BFFFO || lookup->mnemo == i_BFINS ||
				   lookup->mnemo == i_BFSET || lookup->mnemo == i_BFTST) {
			TCHAR *p;
			int reg = -1;

			pc += 2;
			p = instrname + _tcslen(instrname);
			if (lookup->mnemo == i_BFEXTS || lookup->mnemo == i_BFEXTU || lookup->mnemo == i_BFFFO || lookup->mnemo == i_BFINS)
				reg = (extra >> 12) & 7;
			if (lookup->mnemo == i_BFINS)
				_stprintf(p, _T("D%d,"), reg);
			pc = ShowEA(NULL, pc, opcode, dp->dreg, dp->dmode, dp->size, instrname, &seaddr2, safemode);
			_tcscat(instrname, _T(" {"));
			p = instrname + _tcslen(instrname);
			if (extra & 0x0800)
				_stprintf(p, _T("D%d"), (extra >> 6) & 7);
			else
				_stprintf(p, _T("%d"), (extra >> 6) & 31);
			_tcscat(instrname, _T(":"));
			p = instrname + _tcslen(instrname);
			if (extra & 0x0020)
				_stprintf(p, _T("D%d"), extra & 7);
			else
				_stprintf(p, _T("%d"), extra  & 31);
			_tcscat(instrname, _T("}"));
			p = instrname + _tcslen(instrname);
			if (lookup->mnemo == i_BFFFO || lookup->mnemo == i_BFEXTS || lookup->mnemo == i_BFEXTU)
				_stprintf(p, _T(",D%d"), reg);
		} else if (lookup->mnemo == i_CPUSHA || lookup->mnemo == i_CPUSHL || lookup->mnemo == i_CPUSHP ||
			lookup->mnemo == i_CINVA || lookup->mnemo == i_CINVL || lookup->mnemo == i_CINVP) {
			if ((opcode & 0xc0) == 0xc0)
				_tcscat(instrname, _T("BC"));
			else if (opcode & 0x80)
				_tcscat(instrname, _T("IC"));
			else if (opcode & 0x40)
				_tcscat(instrname, _T("DC"));
			else
				_tcscat(instrname, _T("?"));
			if (lookup->mnemo == i_CPUSHL || lookup->mnemo == i_CPUSHP || lookup->mnemo == i_CINVL || lookup->mnemo == i_CINVP) {
				TCHAR *p = instrname + _tcslen(instrname);
				_stprintf(p, _T(",(A%d)"), opcode & 7);
			}
		} else if (lookup->mnemo == i_MOVE16) {
			TCHAR *p = instrname + _tcslen(instrname);
			if (opcode & 0x20) {
				_stprintf(p, _T("(A%d)+,(A%d)+"), opcode & 7, (extra >> 12) & 7);
				pc += 2;
			} else {
				uae_u32 addr = get_long_debug(pc + 2);
				int ay = opcode & 7;
				pc += 4;
				switch ((opcode >> 3) & 3)
				{
				case 0:
					_stprintf(p, _T("(A%d)+,$%08x"), ay, addr);
					break;
				case 1:
					_stprintf(p, _T("$%08x,(A%d)+"), addr, ay);
					break;
				case 2:
					_stprintf(p, _T("(A%d),$%08x"), ay, addr);
					break;
				case 3:
					_stprintf(p, _T("$%08x,(A%d)"), addr, ay);
					break;
				}
			}
		} else if (lookup->mnemo == i_FPP) {
			TCHAR *p;
			int ins = extra & 0x3f;
			int size = (extra >> 10) & 7;

			pc += 2;
			if ((extra & 0xfc00) == 0x5c00) { // FMOVECR (=i_FPP with source specifier = 7)
				fpdata fp;
				fpu_get_constant(&fp, extra);
				_stprintf(instrname, _T("FMOVECR.X #0x%02x [%s],FP%d"), extra & 0x7f, fpp_print(&fp, 0), (extra >> 7) & 7);
			} else if ((extra & 0x8000) == 0x8000) { // FMOVEM
				int dr = (extra >> 13) & 1;
				int mode;
				int dreg = (extra >> 4) & 7;
				int regmask, fpmode;
				
				if (extra & 0x4000) {
					mode = (extra >> 11) & 3;
					regmask = extra & 0xff;  // FMOVEM FPx
					fpmode = 1;
					_tcscpy(instrname, _T("FMOVEM.X "));
				} else {
					mode = 0;
					regmask = (extra >> 10) & 7;  // FMOVEM control
					fpmode = 2;
					_tcscpy(instrname, _T("FMOVEM.L "));
					if (regmask == 1 || regmask == 2 || regmask == 4)
						_tcscpy(instrname, _T("FMOVE.L "));
				}
				p = instrname + _tcslen(instrname);
				if (dr) {
					if (mode & 1)
						_stprintf(instrname, _T("D%d"), dreg);
					else
						movemout(instrname, regmask, dp->dmode, fpmode, false);
					_tcscat(instrname, _T(","));
					pc = ShowEA(NULL, pc, opcode, dp->dreg, dp->dmode, dp->size, instrname, deaddr, safemode);
				} else {
					pc = ShowEA(NULL, pc, opcode, dp->dreg, dp->dmode, dp->size, instrname, deaddr, safemode);
					p = instrname + _tcslen(instrname);
					if (mode & 1)
						_stprintf(p, _T(",D%d"), dreg);
					else
						movemout(p, regmask, dp->dmode, fpmode, true);
				}
			} else {
				if (fpuopcodes[ins])
					_tcscpy(instrname, fpuopcodes[ins]);
				else
					_tcscpy(instrname, _T("F?"));

				if ((extra & 0xe000) == 0x6000) { // FMOVE to memory
					int kfactor = extra & 0x7f;
					_tcscpy(instrname, _T("FMOVE."));
					_tcscat(instrname, fpsizes[size]);
					_tcscat(instrname, _T(" "));
					p = instrname + _tcslen(instrname);
					_stprintf(p, _T("FP%d,"), (extra >> 7) & 7);
					pc = ShowEA(NULL, pc, opcode, dp->dreg, dp->dmode, fpsizeconv[size], instrname, &deaddr2, safemode);
					p = instrname + _tcslen(instrname);
					if (size == 7) {
						_stprintf(p, _T(" {D%d}"), (kfactor >> 4));
					} else if (kfactor) {
						if (kfactor & 0x40)
							kfactor |= ~0x3f;
						_stprintf(p, _T(" {%d}"), kfactor);
					}
				} else {
					if (extra & 0x4000) { // source is EA
						_tcscat(instrname, _T("."));
						_tcscat(instrname, fpsizes[size]);
						_tcscat(instrname, _T(" "));
						pc = ShowEA(NULL, pc, opcode, dp->dreg, dp->dmode, fpsizeconv[size], instrname, &seaddr2, safemode);
					} else { // source is FPx
						p = instrname + _tcslen(instrname);
						_stprintf(p, _T(".X FP%d"), (extra >> 10) & 7);
					}
					p = instrname + _tcslen(instrname);
					if ((extra & 0x4000) || (((extra >> 7) & 7) != ((extra >> 10) & 7)))
						_stprintf(p, _T(",FP%d"), (extra >> 7) & 7);
					if (ins >= 0x30 && ins < 0x38) { // FSINCOS
						p = instrname + _tcslen(instrname);
						_stprintf(p, _T(",FP%d"), extra & 7);
					}
				}
			}
		} else if (lookup->mnemo == i_MMUOP030) {
			pc = disasm_mmu030(pc, opcode, extra, dp, instrname, &seaddr2, safemode);
		} else if ((opcode & 0xf000) == 0xa000) {
			_tcscpy(instrname, _T("A-LINE"));
		} else {
			if (dp->suse) {
				pc = ShowEA (NULL, pc, opcode, dp->sreg, dp->smode, dp->size, instrname, &seaddr2, safemode);

				// JSR x(a6) / JMP x(a6)
				if (opcode == 0x4ea8 + 6 || opcode == 0x4ee8 + 6) {
					TCHAR sname[256];
					if (debugger_get_library_symbol(m68k_areg(regs, 6), 0xffff0000 | extra, sname)) {
						TCHAR *p = instrname + _tcslen(instrname);
						_stprintf(p, _T(" %s"), sname);
						resolve_if_jmp(instrname, m68k_areg(regs, 6) + (uae_s16)extra);
					}
				}
				// show target address if JSR x(pc) + JMP xxxx combination
				if (opcode == 0x4eba && seaddr2 && instrname[0]) { // JSR x(pc)
					resolve_if_jmp(instrname, seaddr2);
				}
			}
			if (dp->suse && dp->duse)
				_tcscat (instrname, _T(","));
			if (dp->duse) {
				pc = ShowEA (NULL, pc, opcode, dp->dreg, dp->dmode, dp->size, instrname, &deaddr2, safemode);
			}
		}

		for (i = 0; i < (pc - oldpc) / 2 && i < 5; i++) {
			buf = buf_out (buf, &bufsize, _T("%04x "), get_word_debug (oldpc + i * 2));
		}
		while (i++ < 5)
			buf = buf_out (buf, &bufsize, _T("     "));

		if (illegal)
			buf = buf_out (buf, &bufsize, _T("[ "));
		buf = buf_out (buf, &bufsize, instrname);
		if (illegal)
			buf = buf_out (buf, &bufsize, _T(" ]"));

		if (ccpt != 0) {
			uaecptr addr2 = deaddr2 ? deaddr2 : seaddr2;
			if (deaddr)
				*deaddr = pc;
			if ((opcode & 0xf000) == 0xf000) {
				if (fpp_cond(dp->cc)) {
					buf = buf_out(buf, &bufsize, _T(" == $%08x (T)"), addr2);
				} else {
					buf = buf_out(buf, &bufsize, _T(" == $%08x (F)"), addr2);
				}
			} else {
				if (dp->mnemo == i_Bcc || dp->mnemo == i_DBcc) {
					if (cctrue(dp->cc)) {
						buf = buf_out(buf, &bufsize, _T(" == $%08x (T)"), addr2);
					} else {
						buf = buf_out(buf, &bufsize, _T(" == $%08x (F)"), addr2);
					}
				} else {
					if (cctrue(dp->cc)) {
						buf = buf_out(buf, &bufsize, _T(" (T)"));
					} else {
						buf = buf_out(buf, &bufsize, _T(" (F)"));
					}
				}
			}
		} else if ((opcode & 0xff00) == 0x6100) { /* BSR */
			if (deaddr)
				*deaddr = pc;
			buf = buf_out (buf, &bufsize, _T(" == $%08x"), seaddr2);
		}
		buf = buf_out (buf, &bufsize, _T("\n"));

		for (uaecptr segpc = oldpc; segpc < pc; segpc++) {
			TCHAR segout[256];
			if (debugmem_get_symbol(segpc, segout, sizeof(segout) / sizeof(TCHAR))) {
				_tcscat(segout, _T(":\n"));
				if (bufsize > _tcslen(segout)) {
					memmove(symbolpos + _tcslen(segout), symbolpos, (_tcslen(symbolpos) + 1) * sizeof(TCHAR));
					memcpy(symbolpos, segout, _tcslen(segout) * sizeof(TCHAR));
					bufsize -= _tcslen(segout);
					buf += _tcslen(segout);
					symbolpos += _tcslen(segout);
				}
			}
		}

		int srcline = -1;
		for (uaecptr segpc = oldpc; segpc < pc; segpc++) {
			TCHAR sourceout[256];
			int line = debugmem_get_sourceline(segpc, sourceout, sizeof(sourceout) / sizeof(TCHAR));
			if (line < 0)
				break;
			if (srcline != line) {
				if (srcline < 0)
					buf = buf_out(buf, &bufsize, _T("\n"));
				buf = buf_out(buf, &bufsize, sourceout);
				srcline = line;
			}
		}
		if (srcline >= 0) {
			buf = buf_out(buf, &bufsize, _T("\n"));
		}

		if (illegal)
			pc =  m68kpc_illg;
	}
	if (nextpc)
		*nextpc = pc;
	if (seaddr)
		*seaddr = seaddr2;
	if (deaddr)
		*deaddr = deaddr2;
}

void m68k_disasm_ea (uaecptr addr, uaecptr *nextpc, int cnt, uae_u32 *seaddr, uae_u32 *deaddr, uaecptr lastpc)
{
	TCHAR *buf;

	buf = xcalloc (TCHAR, (MAX_LINEWIDTH + 1) * cnt);
	if (!buf)
		return;
	m68k_disasm_2 (buf, MAX_LINEWIDTH * cnt, addr, nextpc, cnt, seaddr, deaddr, lastpc, 1);
	xfree (buf);
}
void m68k_disasm (uaecptr addr, uaecptr *nextpc, uaecptr lastpc, int cnt)
{
	TCHAR *buf;

	buf = xcalloc (TCHAR, (MAX_LINEWIDTH + 1) * cnt);
	if (!buf)
		return;
	m68k_disasm_2 (buf, MAX_LINEWIDTH * cnt, addr, nextpc, cnt, NULL, NULL, lastpc, 0);
	console_out_f (_T("%s"), buf);
	xfree (buf);
}

/*************************************************************
Disasm the m68kcode at the given address into instrname
and instrcode
*************************************************************/
void sm68k_disasm (TCHAR *instrname, TCHAR *instrcode, uaecptr addr, uaecptr *nextpc, uaecptr lastpc)
{
	TCHAR *ccpt;
	uae_u32 opcode;
	struct mnemolookup *lookup;
	struct instr *dp;
	uaecptr pc, oldpc;

	pc = oldpc = addr;
	opcode = get_word_debug (pc);
	if (cpufunctbl[opcode] == op_illg_1) {
		opcode = 0x4AFC;
	}
	dp = table68k + opcode;
	for (lookup = lookuptab;lookup->mnemo != dp->mnemo; lookup++);

	pc += 2;

	_tcscpy (instrname, lookup->name);
	ccpt = _tcsstr (instrname, _T("cc"));
	if (ccpt != 0) {
		_tcsncpy (ccpt, ccnames[dp->cc], 2);
	}
	switch (dp->size){
	case sz_byte: _tcscat (instrname, _T(".B ")); break;
	case sz_word: _tcscat (instrname, _T(".W ")); break;
	case sz_long: _tcscat (instrname, _T(".L ")); break;
	default: _tcscat (instrname, _T("   ")); break;
	}

	if (dp->suse) {
		pc = ShowEA (0, pc, opcode, dp->sreg, dp->smode, dp->size, instrname, NULL, 0);
	}
	if (dp->suse && dp->duse)
		_tcscat (instrname, _T(","));
	if (dp->duse) {
		pc = ShowEA (0, pc, opcode, dp->dreg, dp->dmode, dp->size, instrname, NULL, 0);
	}
	if (instrcode)
	{
		int i;
		for (i = 0; i < (pc - oldpc) / 2; i++)
		{
			_stprintf (instrcode, _T("%04x "), get_iword_debug (oldpc + i * 2));
			instrcode += _tcslen (instrcode);
		}
	}
	if (nextpc)
		*nextpc = pc;
}

struct cpum2c m2cregs[] = {
	{ 0, _T("SFC") },
	{ 1, _T("DFC") },
	{ 2, _T("CACR") },
	{ 3, _T("TC") },
	{ 4, _T("ITT0") },
	{ 5, _T("ITT1") },
	{ 6, _T("DTT0") },
	{ 7, _T("DTT1") },
	{ 8, _T("BUSC") },
	{ 0x800, _T("USP") },
	{ 0x801, _T("VBR") },
	{ 0x802, _T("CAAR") },
	{ 0x803, _T("MSP") },
	{ 0x804, _T("ISP") },
	{ 0x805, _T("MMUS") },
	{ 0x806, _T("URP") },
	{ 0x807, _T("SRP") },
	{ 0x808, _T("PCR") },
    { -1, NULL }
};

void m68k_dumpstate(uaecptr *nextpc, uaecptr prevpc)
{
	int i, j;
	uaecptr pc = M68K_GETPC;

	for (i = 0; i < 8; i++){
		console_out_f (_T("  D%d %08X "), i, m68k_dreg (regs, i));
		if ((i & 3) == 3) console_out_f (_T("\n"));
	}
	for (i = 0; i < 8; i++){
		console_out_f (_T("  A%d %08X "), i, m68k_areg (regs, i));
		if ((i & 3) == 3) console_out_f (_T("\n"));
	}
	if (regs.s == 0)
		regs.usp = m68k_areg (regs, 7);
	if (regs.s && regs.m)
		regs.msp = m68k_areg (regs, 7);
	if (regs.s && regs.m == 0)
		regs.isp = m68k_areg (regs, 7);
	j = 2;
	console_out_f (_T("USP  %08X ISP  %08X "), regs.usp, regs.isp);
	for (i = 0; m2cregs[i].regno>= 0; i++) {
		if (!movec_illg (m2cregs[i].regno)) {
			if (!_tcscmp (m2cregs[i].regname, _T("USP")) || !_tcscmp (m2cregs[i].regname, _T("ISP")))
				continue;
			if (j > 0 && (j % 4) == 0)
				console_out_f (_T("\n"));
			console_out_f (_T("%-4s %08X "), m2cregs[i].regname, val_move2c (m2cregs[i].regno));
			j++;
		}
	}
	if (j > 0)
		console_out_f (_T("\n"));
		console_out_f (_T("T=%d%d S=%d M=%d X=%d N=%d Z=%d V=%d C=%d IMASK=%d STP=%d\n"),
		regs.t1, regs.t0, regs.s, regs.m,
		GET_XFLG (), GET_NFLG (), GET_ZFLG (),
		GET_VFLG (), GET_CFLG (),
		regs.intmask, regs.stopped);
#ifdef FPUEMU
	if (currprefs.fpu_model) {
		uae_u32 fpsr;
		for (i = 0; i < 8; i++) {
			if (!(i & 1))
				console_out_f(_T("%d: "), i);
			console_out_f (_T("%s "), fpp_print(&regs.fp[i], -1));
			console_out_f (_T("%s "), fpp_print(&regs.fp[i], 0));
			if (i & 1)
				console_out_f (_T("\n"));
		}
		fpsr = fpp_get_fpsr ();
		console_out_f (_T("FPSR: %08X FPCR: %08x FPIAR: %08x N=%d Z=%d I=%d NAN=%d\n"),
			fpsr, regs.fpcr, regs.fpiar,
			(fpsr & 0x8000000) != 0,
			(fpsr & 0x4000000) != 0,
			(fpsr & 0x2000000) != 0,
			(fpsr & 0x1000000) != 0);
	}
#endif
	if (currprefs.mmu_model == 68030) {
		console_out_f (_T("SRP: %llX CRP: %llX\n"), srp_030, crp_030);
		console_out_f (_T("TT0: %08X TT1: %08X TC: %08X\n"), tt0_030, tt1_030, tc_030);
	}
	if (currprefs.cpu_compatible) {
		if (currprefs.cpu_model == 68000) {
			struct instr *dp;
			struct mnemolookup *lookup1, *lookup2;
			dp = table68k + regs.irc;
			for (lookup1 = lookuptab; lookup1->mnemo != dp->mnemo; lookup1++)
				;
			dp = table68k + regs.ir;
			for (lookup2 = lookuptab; lookup2->mnemo != dp->mnemo; lookup2++)
				;
			console_out_f (_T("Prefetch %04x (%s) %04x (%s) Chip latch %08X\n"), regs.irc, lookup1->name, regs.ir, lookup2->name, regs.chipset_latch_rw);
		} else if (currprefs.cpu_model == 68020 || currprefs.cpu_model == 68030) {
			console_out_f (_T("Prefetch %08x %08x (%d) %04x (%d) %04x (%d) %04x (%d)\n"),
				regs.cacheholdingaddr020, regs.cacheholdingdata020, regs.cacheholdingdata_valid,
				regs.prefetch020[0], regs.prefetch020_valid[0],
				regs.prefetch020[1], regs.prefetch020_valid[1],
				regs.prefetch020[2], regs.prefetch020_valid[2]);
		}
	}
	if (prevpc != 0xffffffff && pc - prevpc < 100) {
		while (prevpc < pc) {
			m68k_disasm(prevpc, &prevpc, 0xffffffff, 1);
		}
	}
	m68k_disasm (pc, nextpc, pc, 1);
	if (nextpc)
		console_out_f (_T("Next PC: %08x\n"), *nextpc);
}

void m68k_dumpcache (bool dc)
{
	if (!currprefs.cpu_compatible)
		return;
	if (currprefs.cpu_model == 68020) {
		for (int i = 0; i < CACHELINES020; i += 4) {
			for (int j = 0; j < 4; j++) {
				int s = i + j;
				uaecptr addr;
				int fc;
				struct cache020 *c = &caches020[s];
				fc = c->tag & 1;
				addr = c->tag & ~1;
				addr |= s << 2;
				console_out_f (_T("%08X%c:%08X%c"), addr, fc ? 'S' : 'U', c->data, c->valid ? '*' : ' ');
			}
			console_out_f (_T("\n"));
		}
	} else if (currprefs.cpu_model == 68030) {
		for (int i = 0; i < CACHELINES030; i++) {
			struct cache030 *c = dc ? &dcaches030[i] : &icaches030[i];
			int fc;
			uaecptr addr;
			if (!dc) {
				fc = (c->tag & 1) ? 6 : 2;
			} else {
				fc = c->fc;
			}
			addr = c->tag & ~1;
			addr |= i << 4;
			console_out_f (_T("%08X %d: "), addr, fc);
			for (int j = 0; j < 4; j++) {
				console_out_f (_T("%08X%c "), c->data[j], c->valid[j] ? '*' : ' ');
			}
			console_out_f (_T("\n"));
		}
	} else if (currprefs.cpu_model >= 68040) {
		uae_u32 tagmask = dc ? cachedtag04060mask : cacheitag04060mask;
		for (int i = 0; i < cachedsets04060; i++) {
			struct cache040 *c = dc ? &dcaches040[i] : &icaches040[i];
			for (int j = 0; j < CACHELINES040; j++) {
				if (c->valid[j]) {
					uae_u32 addr = (c->tag[j] & tagmask) | (i << 4);
					write_log(_T("%02d:%d %08x = %08x%c %08x%c %08x%c %08x%c\n"),
						i, j, addr,
						c->data[j][0], c->dirty[j][0] ? '*' : ' ',
						c->data[j][1], c->dirty[j][1] ? '*' : ' ',
						c->data[j][2], c->dirty[j][2] ? '*' : ' ',
						c->data[j][3], c->dirty[j][3] ? '*' : ' ');
				}
			}
		}
	}
}

#ifdef SAVESTATE

/* CPU save/restore code */

#define CPUTYPE_EC 1
#define CPUMODE_HALT 1

uae_u8 *restore_cpu (uae_u8 *src)
{
	int flags, model;
	uae_u32 l;

	currprefs.cpu_model = changed_prefs.cpu_model = model = restore_u32 ();
	flags = restore_u32 ();
	changed_prefs.address_space_24 = 0;
	if (flags & CPUTYPE_EC)
		changed_prefs.address_space_24 = 1;
	currprefs.address_space_24 = changed_prefs.address_space_24;
	currprefs.cpu_compatible = changed_prefs.cpu_compatible;
	currprefs.cpu_cycle_exact = changed_prefs.cpu_cycle_exact;
	currprefs.cpu_memory_cycle_exact = changed_prefs.cpu_memory_cycle_exact;
	currprefs.blitter_cycle_exact = changed_prefs.blitter_cycle_exact;
	currprefs.cpu_frequency = changed_prefs.cpu_frequency = 0;
	currprefs.cpu_clock_multiplier = changed_prefs.cpu_clock_multiplier = 0;
	for (int i = 0; i < 15; i++)
		regs.regs[i] = restore_u32 ();
	regs.pc = restore_u32 ();
	regs.irc = restore_u16 ();
	regs.ir = restore_u16 ();
	regs.usp = restore_u32 ();
	regs.isp = restore_u32 ();
	regs.sr = restore_u16 ();
	l = restore_u32 ();
	if (l & CPUMODE_HALT) {
		regs.stopped = 1;
	} else {
		regs.stopped = 0;
	}
	if (model >= 68010) {
		regs.dfc = restore_u32 ();
		regs.sfc = restore_u32 ();
		regs.vbr = restore_u32 ();
	}
	if (model >= 68020) {
		regs.caar = restore_u32 ();
		regs.cacr = restore_u32 ();
		regs.msp = restore_u32 ();
	}
	if (model >= 68030) {
		crp_030 = fake_crp_030 = restore_u64 ();
		srp_030 = fake_srp_030 = restore_u64 ();
		tt0_030 = fake_tt0_030 = restore_u32 ();
		tt1_030 = fake_tt1_030 = restore_u32 ();
		tc_030 = fake_tc_030 = restore_u32 ();
		mmusr_030 = fake_mmusr_030 = restore_u16 ();
	}
	if (model >= 68040) {
		regs.itt0 = restore_u32 ();
		regs.itt1 = restore_u32 ();
		regs.dtt0 = restore_u32 ();
		regs.dtt1 = restore_u32 ();
		regs.tcr = restore_u32 ();
		regs.urp = restore_u32 ();
		regs.srp = restore_u32 ();
	}
	if (model >= 68060) {
		regs.buscr = restore_u32 ();
		regs.pcr = restore_u32 ();
	}
	if (flags & 0x80000000) {
		int khz = restore_u32 ();
		restore_u32 ();
		if (khz > 0 && khz < 800000)
			currprefs.m68k_speed = changed_prefs.m68k_speed = 0;
	}
	set_cpu_caches (true);
	if (flags & 0x40000000) {
		if (model == 68020) {
			for (int i = 0; i < CACHELINES020; i++) {
				caches020[i].data = restore_u32 ();
				caches020[i].tag = restore_u32 ();
				caches020[i].valid = restore_u8 () != 0;
			}
			regs.prefetch020addr = restore_u32 ();
			regs.cacheholdingaddr020 = restore_u32 ();
			regs.cacheholdingdata020 = restore_u32 ();
			if (flags & 0x20000000) {
				if (flags & 0x4000000) {
					// 3.6 new (back to 16 bits)
					for (int i = 0; i < CPU_PIPELINE_MAX; i++) {
						uae_u32 v = restore_u32();
						regs.prefetch020[i] = v >> 16;
						regs.prefetch020_valid[i] = (v & 1) != 0;
					}
				} else {
					// old
					uae_u32 v = restore_u32();
					regs.prefetch020[0] = v >> 16;
					regs.prefetch020[1] = (uae_u16)v;
					v = restore_u32();
					regs.prefetch020[2] = v >> 16;
					regs.prefetch020[3] = (uae_u16)v;
					restore_u32();
					restore_u32();
					regs.prefetch020_valid[0] = true;
					regs.prefetch020_valid[1] = true;
					regs.prefetch020_valid[2] = true;
					regs.prefetch020_valid[3] = true;
				}
			}
		} else if (model == 68030) {
			for (int i = 0; i < CACHELINES030; i++) {
				for (int j = 0; j < 4; j++) {
					icaches030[i].data[j] = restore_u32 ();
					icaches030[i].valid[j] = restore_u8 () != 0;
				}
				icaches030[i].tag = restore_u32 ();
			}
			for (int i = 0; i < CACHELINES030; i++) {
				for (int j = 0; j < 4; j++) {
					dcaches030[i].data[j] = restore_u32 ();
					dcaches030[i].valid[j] = restore_u8 () != 0;
				}
				dcaches030[i].tag = restore_u32 ();
			}
			regs.prefetch020addr = restore_u32 ();
			regs.cacheholdingaddr020 = restore_u32 ();
			regs.cacheholdingdata020 = restore_u32 ();
			if (flags & 0x4000000) {
				for (int i = 0; i < CPU_PIPELINE_MAX; i++) {
					uae_u32 v = restore_u32();
					regs.prefetch020[i] = v >> 16;
					regs.prefetch020_valid[i] = (v & 1) != 0;
				}
			} else {
				for (int i = 0; i < CPU_PIPELINE_MAX; i++) {
					regs.prefetch020[i] = restore_u32 ();
					regs.prefetch020_valid[i] = false;
				}
			}
		} else if (model == 68040) {
			if (flags & 0x8000000) {
				for (int i = 0; i < ((model == 68060 && (flags & 0x4000000)) ? CACHESETS060 : CACHESETS040); i++) {
					for (int j = 0; j < CACHELINES040; j++) {
						struct cache040 *c = &icaches040[i];
						c->data[j][0] = restore_u32();
						c->data[j][1] = restore_u32();
						c->data[j][2] = restore_u32();
						c->data[j][3] = restore_u32();
						c->tag[j] = restore_u32();
						c->valid[j] = restore_u16() & 1;
					}
				}
				regs.prefetch020addr = restore_u32();
				regs.cacheholdingaddr020 = restore_u32();
				regs.cacheholdingdata020 = restore_u32();
				for (int i = 0; i < CPU_PIPELINE_MAX; i++)
					regs.prefetch040[i] = restore_u32();
				if (flags & 0x4000000) {
					for (int i = 0; i < (model == 68060 ? CACHESETS060 : CACHESETS040); i++) {
						for (int j = 0; j < CACHELINES040; j++) {
							struct cache040 *c = &dcaches040[i];
							c->data[j][0] = restore_u32();
							c->data[j][1] = restore_u32();
							c->data[j][2] = restore_u32();
							c->data[j][3] = restore_u32();
							c->tag[j] = restore_u32();
							uae_u16 v = restore_u16();
							c->valid[j] = (v & 1) != 0;
							c->dirty[j][0] = (v & 0x10) != 0;
							c->dirty[j][1] = (v & 0x20) != 0;
							c->dirty[j][2] = (v & 0x40) != 0;
							c->dirty[j][3] = (v & 0x80) != 0;
							c->gdirty[j] = c->dirty[j][0] || c->dirty[j][1] || c->dirty[j][2] || c->dirty[j][3];
						}
					}
				}
			}
		}
		if (model >= 68020) {
			restore_u32 (); // regs.ce020memcycles
			regs.ce020startcycle = regs.ce020endcycle = 0;
			restore_u32 ();
		}
	}
	if (flags & 0x10000000) {
		regs.chipset_latch_rw = restore_u32 ();
		regs.chipset_latch_read = restore_u32 ();
		regs.chipset_latch_write = restore_u32 ();
	}

	regs.pipeline_pos = -1;
	regs.pipeline_stop = 0;
	if (flags & 0x4000000 && currprefs.cpu_model == 68020) {
		regs.pipeline_pos = restore_u16();
		regs.pipeline_r8[0] = restore_u16();
		regs.pipeline_r8[1] = restore_u16();
		regs.pipeline_stop = restore_u16();
	}

	m68k_reset_sr();

	write_log (_T("CPU: %d%s%03d, PC=%08X\n"),
		model / 1000, flags & 1 ? _T("EC") : _T(""), model % 1000, regs.pc);

	return src;
}

static void fill_prefetch_quick (void)
{
	if (currprefs.cpu_model >= 68020) {
		fill_prefetch ();
		return;
	}
	// old statefile compatibility, this needs to done,
	// even in 68000 cycle-exact mode
	regs.ir = get_word (m68k_getpc ());
	regs.irc = get_word (m68k_getpc () + 2);
}

void restore_cpu_finish (void)
{
	if (!currprefs.fpu_model)
		fpu_reset();
	init_m68k ();
	m68k_setpc_normal (regs.pc);
	doint ();
	fill_prefetch_quick ();
	set_cycles (start_cycles);
	events_schedule ();
	if (regs.stopped)
		set_special (SPCFLAG_STOP);
	//activate_debugger ();
}

uae_u8 *save_cpu_trace (int *len, uae_u8 *dstptr)
{
	uae_u8 *dstbak, *dst;

	if (cputrace.state <= 0)
		return NULL;

	if (dstptr)
		dstbak = dst = dstptr;
	else
		dstbak = dst = xmalloc (uae_u8, 10000);

	save_u32 (2 | 4 | 16 | 32);
	save_u16 (cputrace.opcode);
	for (int i = 0; i < 16; i++)
		save_u32 (cputrace.regs[i]);
	save_u32 (cputrace.pc);
	save_u16 (cputrace.irc);
	save_u16 (cputrace.ir);
	save_u32 (cputrace.usp);
	save_u32 (cputrace.isp);
	save_u16 (cputrace.sr);
	save_u16 (cputrace.intmask);
	save_u16 ((cputrace.stopped ? 1 : 0) | (regs.stopped ? 2 : 0));
	save_u16 (cputrace.state);
	save_u32 (cputrace.cyclecounter);
	save_u32 (cputrace.cyclecounter_pre);
	save_u32 (cputrace.cyclecounter_post);
	save_u32 (cputrace.readcounter);
	save_u32 (cputrace.writecounter);
	save_u32 (cputrace.memoryoffset);
	write_log (_T("CPUT SAVE: PC=%08x C=%08X %08x %08x %08x %d %d %d\n"),
		cputrace.pc, cputrace.startcycles,
		cputrace.cyclecounter, cputrace.cyclecounter_pre, cputrace.cyclecounter_post,
		cputrace.readcounter, cputrace.writecounter, cputrace.memoryoffset);
	for (int i = 0; i < cputrace.memoryoffset; i++) {
		save_u32 (cputrace.ctm[i].addr);
		save_u32 (cputrace.ctm[i].data);
		save_u32 (cputrace.ctm[i].mode);
		write_log (_T("CPUT%d: %08x %08x %08x\n"), i, cputrace.ctm[i].addr, cputrace.ctm[i].data, cputrace.ctm[i].mode);
	}
	save_u32 (cputrace.startcycles);

	if (currprefs.cpu_model == 68020) {
		for (int i = 0; i < CACHELINES020; i++) {
			save_u32 (cputrace.caches020[i].data);
			save_u32 (cputrace.caches020[i].tag);
			save_u8 (cputrace.caches020[i].valid ? 1 : 0);
		}
		save_u32 (cputrace.prefetch020addr);
		save_u32 (cputrace.cacheholdingaddr020);
		save_u32 (cputrace.cacheholdingdata020);
		for (int i = 0; i < CPU_PIPELINE_MAX; i++) {
			save_u16 (cputrace.prefetch020[i]);
		}
		for (int i = 0; i < CPU_PIPELINE_MAX; i++) {
			save_u32 (cputrace.prefetch020[i]);
		}
		for (int i = 0; i < CPU_PIPELINE_MAX; i++) {
			save_u8 (cputrace.prefetch020_valid[i]);
		}
		save_u16(cputrace.pipeline_pos);
		save_u16(cputrace.pipeline_r8[0]);
		save_u16(cputrace.pipeline_r8[1]);
		save_u16(cputrace.pipeline_stop);
	}

	*len = dst - dstbak;
	cputrace.needendcycles = 1;
	return dstbak;
}

uae_u8 *restore_cpu_trace (uae_u8 *src)
{
	cpu_tracer = 0;
	cputrace.state = 0;
	uae_u32 v = restore_u32 ();
	if (!(v & 2))
		return src;
	cputrace.opcode = restore_u16 ();
	for (int i = 0; i < 16; i++)
		cputrace.regs[i] = restore_u32 ();
	cputrace.pc = restore_u32 ();
	cputrace.irc = restore_u16 ();
	cputrace.ir = restore_u16 ();
	cputrace.usp = restore_u32 ();
	cputrace.isp = restore_u32 ();
	cputrace.sr = restore_u16 ();
	cputrace.intmask = restore_u16 ();
	cputrace.stopped = restore_u16 ();
	cputrace.state = restore_u16 ();
	cputrace.cyclecounter = restore_u32 ();
	cputrace.cyclecounter_pre = restore_u32 ();
	cputrace.cyclecounter_post = restore_u32 ();
	cputrace.readcounter = restore_u32 ();
	cputrace.writecounter = restore_u32 ();
	cputrace.memoryoffset = restore_u32 ();
	for (int i = 0; i < cputrace.memoryoffset; i++) {
		cputrace.ctm[i].addr = restore_u32 ();
		cputrace.ctm[i].data = restore_u32 ();
		cputrace.ctm[i].mode = restore_u32 ();
	}
	cputrace.startcycles = restore_u32 ();

	if (v & 4) {
		if (currprefs.cpu_model == 68020) {
			for (int i = 0; i < CACHELINES020; i++) {
				cputrace.caches020[i].data = restore_u32 ();
				cputrace.caches020[i].tag = restore_u32 ();
				cputrace.caches020[i].valid = restore_u8 () != 0;
			}
			cputrace.prefetch020addr = restore_u32 ();
			cputrace.cacheholdingaddr020 = restore_u32 ();
			cputrace.cacheholdingdata020 = restore_u32 ();
			for (int i = 0; i < CPU_PIPELINE_MAX; i++) {
				cputrace.prefetch020[i] = restore_u16 ();
			}
			if (v & 8) {
				// backwards compatibility
				uae_u32 v2 = restore_u32();
				cputrace.prefetch020[0] = v2 >> 16;
				cputrace.prefetch020[1] = (uae_u16)v2;
				v2 = restore_u32();
				cputrace.prefetch020[2] = v2 >> 16;
				cputrace.prefetch020[3] = (uae_u16)v2;
				restore_u32();
				restore_u32();
				cputrace.prefetch020_valid[0] = true;
				cputrace.prefetch020_valid[1] = true;
				cputrace.prefetch020_valid[2] = true;
				cputrace.prefetch020_valid[3] = true;

				cputrace.prefetch020[0] = cputrace.prefetch020[1];
				cputrace.prefetch020[1] = cputrace.prefetch020[2];
				cputrace.prefetch020[2] = cputrace.prefetch020[3];
				cputrace.prefetch020_valid[3] = false;
			}
			if (v & 16) {
				if ((v & 32) && !(v & 8)) {
					restore_u32();
					restore_u32();
					restore_u32();
					restore_u32();
				}
				for (int i = 0; i < CPU_PIPELINE_MAX; i++) {
					cputrace.prefetch020_valid[i] = restore_u8() != 0;
				}
			}
			if (v & 32) {
				cputrace.pipeline_pos = restore_u16();
				cputrace.pipeline_r8[0] = restore_u16();
				cputrace.pipeline_r8[1] = restore_u16();
				cputrace.pipeline_stop = restore_u16();
			}
		}
	}

	cputrace.needendcycles = 1;
	if (v && cputrace.state) {
		if (currprefs.cpu_model > 68000) {
			if (v & 4)
				cpu_tracer = -1;
			// old format?
			if ((v & (4 | 8)) != (4 | 8) && (v & (32 | 16 | 8 | 4)) != (32 | 16 | 4))
				cpu_tracer = 0;
		} else {
			cpu_tracer = -1;
		}
	}

	return src;
}

uae_u8 *restore_cpu_extra (uae_u8 *src)
{
	restore_u32 ();
	uae_u32 flags = restore_u32 ();

	currprefs.cpu_cycle_exact = changed_prefs.cpu_cycle_exact = (flags & 1) ? true : false;
	currprefs.cpu_memory_cycle_exact = changed_prefs.cpu_memory_cycle_exact = currprefs.cpu_cycle_exact;
	if ((flags & 32) && !(flags & 1))
		currprefs.cpu_memory_cycle_exact = changed_prefs.cpu_memory_cycle_exact = true;
	currprefs.blitter_cycle_exact = changed_prefs.blitter_cycle_exact = currprefs.cpu_cycle_exact;
	currprefs.cpu_compatible = changed_prefs.cpu_compatible = (flags & 2) ? true : false;
	currprefs.cpu_frequency = changed_prefs.cpu_frequency = restore_u32 ();
	currprefs.cpu_clock_multiplier = changed_prefs.cpu_clock_multiplier = restore_u32 ();
	//currprefs.cachesize = changed_prefs.cachesize = (flags & 8) ? 8192 : 0;

	currprefs.m68k_speed = changed_prefs.m68k_speed = 0;
	if (flags & 4)
		currprefs.m68k_speed = changed_prefs.m68k_speed = -1;
	if (flags & 16)
		currprefs.m68k_speed = changed_prefs.m68k_speed = (flags >> 24) * CYCLE_UNIT;

	currprefs.cpu060_revision = changed_prefs.cpu060_revision = restore_u8 ();
	currprefs.fpu_revision = changed_prefs.fpu_revision = restore_u8 ();

	return src;
}

uae_u8 *save_cpu_extra (int *len, uae_u8 *dstptr)
{
	uae_u8 *dstbak, *dst;
	uae_u32 flags;

	if (dstptr)
		dstbak = dst = dstptr;
	else
		dstbak = dst = xmalloc (uae_u8, 1000);
	save_u32 (0); // version
	flags = 0;
	flags |= currprefs.cpu_cycle_exact ? 1 : 0;
	flags |= currprefs.cpu_compatible ? 2 : 0;
	flags |= currprefs.m68k_speed < 0 ? 4 : 0;
	flags |= currprefs.cachesize > 0 ? 8 : 0;
	flags |= currprefs.m68k_speed > 0 ? 16 : 0;
	flags |= currprefs.cpu_memory_cycle_exact ? 32 : 0;
	if (currprefs.m68k_speed > 0)
		flags |= (currprefs.m68k_speed / CYCLE_UNIT) << 24;
	save_u32 (flags);
	save_u32 (currprefs.cpu_frequency);
	save_u32 (currprefs.cpu_clock_multiplier);
	save_u8 (currprefs.cpu060_revision);
	save_u8 (currprefs.fpu_revision);
	*len = dst - dstbak;
	return dstbak;
}

uae_u8 *save_cpu (int *len, uae_u8 *dstptr)
{
	uae_u8 *dstbak, *dst;
	int model, khz;

	if (dstptr)
		dstbak = dst = dstptr;
	else
		dstbak = dst = xmalloc (uae_u8, 1000 + 30000);
	model = currprefs.cpu_model;
	save_u32 (model);					/* MODEL */
	save_u32(0x80000000 | 0x40000000 | 0x20000000 | 0x10000000 | 0x8000000 | 0x4000000 | (currprefs.address_space_24 ? 1 : 0)); /* FLAGS */
	for (int i = 0;i < 15; i++)
		save_u32 (regs.regs[i]);		/* D0-D7 A0-A6 */
	save_u32 (m68k_getpc ());			/* PC */
	save_u16 (regs.irc);				/* prefetch */
	save_u16 (regs.ir);					/* instruction prefetch */
	MakeSR ();
	save_u32 (!regs.s ? regs.regs[15] : regs.usp);	/* USP */
	save_u32 (regs.s ? regs.regs[15] : regs.isp);	/* ISP */
	save_u16 (regs.sr);								/* SR/CCR */
	save_u32 (regs.stopped ? CPUMODE_HALT : 0);		/* flags */
	if (model >= 68010) {
		save_u32 (regs.dfc);			/* DFC */
		save_u32 (regs.sfc);			/* SFC */
		save_u32 (regs.vbr);			/* VBR */
	}
	if (model >= 68020) {
		save_u32 (regs.caar);			/* CAAR */
		save_u32 (regs.cacr);			/* CACR */
		save_u32 (regs.msp);			/* MSP */
	}
	if (model >= 68030) {
		if (currprefs.mmu_model) {
			save_u64 (crp_030);				/* CRP */
			save_u64 (srp_030);				/* SRP */
			save_u32 (tt0_030);				/* TT0/AC0 */
			save_u32 (tt1_030);				/* TT1/AC1 */
			save_u32 (tc_030);				/* TCR */
			save_u16 (mmusr_030);			/* MMUSR/ACUSR */
		} else {
			save_u64 (fake_crp_030);		/* CRP */
			save_u64 (fake_srp_030);		/* SRP */
			save_u32 (fake_tt0_030);		/* TT0/AC0 */
			save_u32 (fake_tt1_030);		/* TT1/AC1 */
			save_u32 (fake_tc_030);			/* TCR */
			save_u16 (fake_mmusr_030);		/* MMUSR/ACUSR */
		}
	}
	if (model >= 68040) {
		save_u32 (regs.itt0);			/* ITT0 */
		save_u32 (regs.itt1);			/* ITT1 */
		save_u32 (regs.dtt0);			/* DTT0 */
		save_u32 (regs.dtt1);			/* DTT1 */
		save_u32 (regs.tcr);			/* TCR */
		save_u32 (regs.urp);			/* URP */
		save_u32 (regs.srp);			/* SRP */
	}
	if (model >= 68060) {
		save_u32 (regs.buscr);			/* BUSCR */
		save_u32 (regs.pcr);			/* PCR */
	}
	khz = -1;
	if (currprefs.m68k_speed == 0) {
		khz = currprefs.ntscmode ? 715909 : 709379;
		if (currprefs.cpu_model >= 68020)
			khz *= 2;
	}
	save_u32 (khz); // clock rate in KHz: -1 = fastest possible
	save_u32 (0); // spare
	if (model == 68020) {
		for (int i = 0; i < CACHELINES020; i++) {
			save_u32 (caches020[i].data);
			save_u32 (caches020[i].tag);
			save_u8 (caches020[i].valid ? 1 : 0);
		}
		save_u32 (regs.prefetch020addr);
		save_u32 (regs.cacheholdingaddr020);
		save_u32 (regs.cacheholdingdata020);
		for (int i = 0; i < CPU_PIPELINE_MAX; i++)
			save_u32 ((regs.prefetch020[i] << 16) | (regs.prefetch020_valid[i] ? 1 : 0));
	} else if (model == 68030) {
		for (int i = 0; i < CACHELINES030; i++) {
			for (int j = 0; j < 4; j++) {
				save_u32 (icaches030[i].data[j]);
				save_u8 (icaches030[i].valid[j] ? 1 : 0);
			}
			save_u32 (icaches030[i].tag);
		}
		for (int i = 0; i < CACHELINES030; i++) {
			for (int j = 0; j < 4; j++) {
				save_u32 (dcaches030[i].data[j]);
				save_u8 (dcaches030[i].valid[j] ? 1 : 0);
			}
			save_u32 (dcaches030[i].tag);
		}
		save_u32 (regs.prefetch020addr);
		save_u32 (regs.cacheholdingaddr020);
		save_u32 (regs.cacheholdingdata020);
		for (int i = 0; i < CPU_PIPELINE_MAX; i++)
			save_u32 (regs.prefetch020[i]);
	} else if (model >= 68040) {
		for (int i = 0; i < (model == 68060 ? CACHESETS060 : CACHESETS040); i++) {
			for (int j = 0; j < CACHELINES040; j++) {
				struct cache040 *c = &icaches040[i];
				save_u32(c->data[j][0]);
				save_u32(c->data[j][1]);
				save_u32(c->data[j][2]);
				save_u32(c->data[j][3]);
				save_u32(c->tag[j]);
				save_u16(c->valid[j] ? 1 : 0);
			}
		}
		save_u32(regs.prefetch020addr);
		save_u32(regs.cacheholdingaddr020);
		save_u32(regs.cacheholdingdata020);
		for (int i = 0; i < CPU_PIPELINE_MAX; i++) {
			save_u32(regs.prefetch040[i]);
		}
		for (int i = 0; i < (model == 68060 ? CACHESETS060 : CACHESETS040); i++) {
			for (int j = 0; j < CACHELINES040; j++) {
				struct cache040 *c = &dcaches040[i];
				save_u32(c->data[j][0]);
				save_u32(c->data[j][1]);
				save_u32(c->data[j][2]);
				save_u32(c->data[j][3]);
				save_u32(c->tag[j]);
				uae_u16 v = c->valid[j] ? 1 : 0;
				v |= c->dirty[j][0] ? 0x10 : 0;
				v |= c->dirty[j][1] ? 0x20 : 0;
				v |= c->dirty[j][2] ? 0x40 : 0;
				v |= c->dirty[j][3] ? 0x80 : 0;
				save_u16(v);
			}
		}
	}
	if (currprefs.cpu_model >= 68020) {
		save_u32 (0); //save_u32 (regs.ce020memcycles);
		save_u32 (0);
	}
	save_u32 (regs.chipset_latch_rw);
	save_u32 (regs.chipset_latch_read);
	save_u32 (regs.chipset_latch_write);
	if (currprefs.cpu_model == 68020) {
		save_u16(regs.pipeline_pos);
		save_u16(regs.pipeline_r8[0]);
		save_u16(regs.pipeline_r8[1]);
		save_u16(regs.pipeline_stop);
	}
	*len = dst - dstbak;
	return dstbak;
}

uae_u8 *save_mmu (int *len, uae_u8 *dstptr)
{
	uae_u8 *dstbak, *dst;
	int model;

	model = currprefs.mmu_model;
	if (model != 68030 && model != 68040 && model != 68060)
		return NULL;
	if (dstptr)
		dstbak = dst = dstptr;
	else
		dstbak = dst = xmalloc (uae_u8, 1000);
	save_u32 (model);	/* MODEL */
	save_u32 (0);		/* FLAGS */
	*len = dst - dstbak;
	return dstbak;
}

uae_u8 *restore_mmu (uae_u8 *src)
{
	int flags, model;

	changed_prefs.mmu_model = model = restore_u32 ();
	flags = restore_u32 ();
	write_log (_T("MMU: %d\n"), model);
	return src;
}

#endif /* SAVESTATE */

static void exception3f (uae_u32 opcode, uaecptr addr, bool writeaccess, bool instructionaccess, bool notinstruction, uaecptr pc, bool plus2)
{
	if (currprefs.cpu_model >= 68040)
		addr &= ~1;
	if (currprefs.cpu_model >= 68020) {
		if (pc == 0xffffffff)
			last_addr_for_exception_3 = regs.instruction_pc;
		else
			last_addr_for_exception_3 = pc;
	} else if (pc == 0xffffffff) {
		last_addr_for_exception_3 = m68k_getpc ();
		if (plus2)
			last_addr_for_exception_3 += 2;
	} else {
		last_addr_for_exception_3 = pc;
	}
	last_fault_for_exception_3 = addr;
	last_op_for_exception_3 = opcode;
	last_writeaccess_for_exception_3 = writeaccess;
	last_instructionaccess_for_exception_3 = instructionaccess;
	last_notinstruction_for_exception_3 = notinstruction;
	Exception (3);
#if EXCEPTION3_DEBUGGER
	activate_debugger();
#endif
}

void exception3_notinstruction(uae_u32 opcode, uaecptr addr)
{
	exception3f (opcode, addr, true, false, true, 0xffffffff, false);
}
void exception3_read(uae_u32 opcode, uaecptr addr)
{
	exception3f (opcode, addr, false, 0, false, 0xffffffff, false);
}
void exception3_write(uae_u32 opcode, uaecptr addr)
{
	exception3f (opcode, addr, true, 0, false, 0xffffffff, false);
}
void exception3i (uae_u32 opcode, uaecptr addr)
{
	exception3f (opcode, addr, 0, 1, false, 0xffffffff, true);
}
void exception3b (uae_u32 opcode, uaecptr addr, bool w, bool i, uaecptr pc)
{
	exception3f (opcode, addr, w, i, false, pc, true);
}

void exception2_setup(uaecptr addr, bool read, int size, uae_u32 fc)
{
	last_addr_for_exception_3 = m68k_getpc() + bus_error_offset;
	last_fault_for_exception_3 = addr;
	last_writeaccess_for_exception_3 = read == 0;
	last_instructionaccess_for_exception_3 = (fc & 1) == 0;
	last_op_for_exception_3 = regs.opcode;
	last_notinstruction_for_exception_3 = exception_in_exception != 0;
}

void exception2 (uaecptr addr, bool read, int size, uae_u32 fc)
{
	if (currprefs.mmu_model) {
		if (currprefs.mmu_model == 68030) {
			uae_u32 flags = size == 1 ? MMU030_SSW_SIZE_B : (size == 2 ? MMU030_SSW_SIZE_W : MMU030_SSW_SIZE_L);
			mmu030_page_fault (addr, read, flags, fc);
		} else {
			mmu_bus_error (addr, 0, fc, read == false, size, 0, true);
		}
	} else {
		exception2_setup(addr, read, size, fc);
		THROW(2);
	}
}

void cpureset (void)
{
    /* RESET hasn't increased PC yet, 1 word offset */
	uaecptr pc;
	uaecptr ksboot = 0xf80002 - 2;
	uae_u16 ins;
	addrbank *ab;

	if (currprefs.cpu_model == 68060 && currprefs.cpuboard_type == 0 && rtarea_base != 0xf00000) {
		// disable FPU at reset if no accelerator board and no $f0 ROM.
		regs.pcr |= 2;
	}
	m68k_reset_delay = currprefs.reset_delay;
	set_special(SPCFLAG_CHECK);
	send_internalevent(INTERNALEVENT_CPURESET);
	if ((currprefs.cpu_compatible || currprefs.cpu_memory_cycle_exact) && currprefs.cpu_model <= 68020) {
		custom_reset_cpu(false, false);
		return;
	}
	pc = m68k_getpc () + 2;
	ab = &get_mem_bank (pc);
	if (ab->check (pc, 2)) {
		write_log (_T("CPU reset PC=%x (%s)..\n"), pc - 2, ab->name);
		ins = get_word (pc);
		custom_reset_cpu(false, false);
		// did memory disappear under us?
		if (ab == &get_mem_bank (pc))
			return;
		// it did
		if ((ins & ~7) == 0x4ed0) {
			int reg = ins & 7;
			uae_u32 addr = m68k_areg (regs, reg);
			if (addr < 0x80000)
				addr += 0xf80000;
			write_log (_T("reset/jmp (ax) combination at %08x emulated -> %x\n"), pc, addr);
			m68k_setpc_normal (addr - 2);
			return;
		}
	}
	// the best we can do, jump directly to ROM entrypoint
	// (which is probably what program wanted anyway)
	write_log (_T("CPU Reset PC=%x (%s), invalid memory -> %x.\n"), pc, ab->name, ksboot + 2);
	custom_reset_cpu(false, false);
	m68k_setpc_normal (ksboot);
}


void m68k_setstopped (void)
{
	/* A traced STOP instruction drops through immediately without
	actually stopping.  */
	if ((regs.spcflags & SPCFLAG_DOTRACE) == 0) {
		m68k_set_stop();
	} else {
		m68k_resumestopped ();
	}
}

void m68k_resumestopped (void)
{
	if (!regs.stopped)
		return;
	if (currprefs.cpu_cycle_exact && currprefs.cpu_model == 68000) {
		x_do_cycles (6 * cpucycleunit);
	}
	fill_prefetch ();
	m68k_unset_stop();
}


uae_u32 mem_access_delay_word_read (uaecptr addr)
{
	uae_u32 v;
	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
	case CE_MEMBANK_CHIP32:
		v = wait_cpu_cycle_read (addr, 1);
		break;
	case CE_MEMBANK_FAST16:
	case CE_MEMBANK_FAST32:
		v = get_word (addr);
		x_do_cycles_post (4 * cpucycleunit, v);
		break;
	default:
		v = get_word (addr);
		break;
	}
	regs.db = v;
	return v;
}
uae_u32 mem_access_delay_wordi_read (uaecptr addr)
{
	uae_u32 v;
	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
	case CE_MEMBANK_CHIP32:
		v = wait_cpu_cycle_read (addr, 2);
		break;
	case CE_MEMBANK_FAST16:
	case CE_MEMBANK_FAST32:
		v = get_wordi (addr);
		x_do_cycles_post (4 * cpucycleunit, v);
		break;
	default:
		v = get_wordi (addr);
		break;
	}
	regs.db = v;
	return v;
}

uae_u32 mem_access_delay_byte_read (uaecptr addr)
{
	uae_u32  v;
	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
	case CE_MEMBANK_CHIP32:
		v = wait_cpu_cycle_read (addr, 0);
		break;
	case CE_MEMBANK_FAST16:
	case CE_MEMBANK_FAST32:
		v = get_byte (addr);
		x_do_cycles_post (4 * cpucycleunit, v);
		break;
	default:
		v = get_byte (addr);
		break;
	}
	regs.db = (v << 8) | v;
	return v;
}
void mem_access_delay_byte_write (uaecptr addr, uae_u32 v)
{
	regs.db = (v << 8)  | v;
	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
	case CE_MEMBANK_CHIP32:
		wait_cpu_cycle_write (addr, 0, v);
		return;
	case CE_MEMBANK_FAST16:
	case CE_MEMBANK_FAST32:
		put_byte (addr, v);
		x_do_cycles_post (4 * cpucycleunit, v);
		return;
	}
	put_byte (addr, v);
}
void mem_access_delay_word_write (uaecptr addr, uae_u32 v)
{
	regs.db = v;
	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
	case CE_MEMBANK_CHIP32:
		wait_cpu_cycle_write (addr, 1, v);
		return;
	case CE_MEMBANK_FAST16:
	case CE_MEMBANK_FAST32:
		put_word (addr, v);
		x_do_cycles_post (4 * cpucycleunit, v);
		return;
	}
	put_word (addr, v);
}

static void start_020_cycle(void)
{
	regs.ce020startcycle = get_cycles();
}

static void start_020_cycle_prefetch(bool opcode)
{
	regs.ce020startcycle = get_cycles();
	// back to back prefetch cycles require 2 extra cycles (maybe)
	if (opcode && regs.ce020startcycle == regs.ce020prefetchendcycle && currprefs.cpu_cycle_exact) {
		x_do_cycles(2 * cpucycleunit);
		regs.ce020startcycle = get_cycles();
	}
}
static void end_020_cycle(void)
{
	regs.ce020endcycle = get_cycles();
}
static void end_020_cycle_prefetch(bool opcode)
{
	regs.ce020endcycle = get_cycles();
	if (opcode) {
		regs.ce020prefetchendcycle = regs.ce020endcycle;
	} else {
		regs.ce020prefetchendcycle = regs.ce020startcycle;
	}
}

// this one is really simple and easy
static void fill_icache020 (uae_u32 addr, bool opcode)
{
	int index;
	uae_u32 tag;
	uae_u32 data;
	struct cache020 *c;

	regs.fc030 = (regs.s ? 4 : 0) | 2;
	addr &= ~3;
	if (regs.cacheholdingaddr020 == addr)
		return;
	index = (addr >> 2) & (CACHELINES020 - 1);
	tag = regs.s | (addr & ~((CACHELINES020 << 2) - 1));
	c = &caches020[index];
	if ((regs.cacr & 1) && c->valid && c->tag == tag) {
		// cache hit
		regs.cacheholdingaddr020 = addr;
		regs.cacheholdingdata020 = c->data;
		regs.cacheholdingdata_valid = true;
		return;
	}

	// cache miss
#if 0
	// Prefetch apparently can be queued by bus controller
	// even if bus controller is currently processing
	// previous data access.
	// Other combinations are not possible.
	if (!regs.ce020memcycle_data) {
		if (regs.ce020memcycles > 0)
			x_do_cycles (regs.ce020memcycles);
		regs.ce020memcycles = 0;
	}
#endif

	start_020_cycle_prefetch(opcode);
	data = icache_fetch(addr);
	end_020_cycle_prefetch(opcode);

	// enabled and not frozen
	if ((regs.cacr & 1) && !(regs.cacr & 2)) {
		c->tag = tag;
		c->valid = true;
		c->data = data;
	}
	regs.cacheholdingaddr020 = addr;
	regs.cacheholdingdata020 = data;
	regs.cacheholdingdata_valid = true;
}

#if MORE_ACCURATE_68020_PIPELINE
#define PIPELINE_DEBUG 0
#if PIPELINE_DEBUG
static uae_u16 pipeline_opcode;
#endif
static void pipeline_020(uaecptr pc)
{
	uae_u16 w = regs.prefetch020[1];

	if (regs.prefetch020_valid[1] == 0) {
		regs.pipeline_stop = -1;
		return;
	}
	if (regs.pipeline_pos < 0)
		return;
	if (regs.pipeline_pos > 0) {
		// handle annoying 68020+ addressing modes
		if (regs.pipeline_pos == regs.pipeline_r8[0]) {
			regs.pipeline_r8[0] = 0;
			if (w & 0x100) {
				int extra = 0;
				if ((w & 0x30) == 0x20)
					extra += 2;
				if ((w & 0x30) == 0x30)
					extra += 4;
				if ((w & 0x03) == 0x02)
					extra += 2;
				if ((w & 0x03) == 0x03)
					extra += 4;
				regs.pipeline_pos += extra;
			}
			return;
		}
		if (regs.pipeline_pos == regs.pipeline_r8[1]) {
			regs.pipeline_r8[1] = 0;
			if (w & 0x100) {
				int extra = 0;
				if ((w & 0x30) == 0x20)
					extra += 2;
				if ((w & 0x30) == 0x30)
					extra += 4;
				if ((w & 0x03) == 0x02)
					extra += 2;
				if ((w & 0x03) == 0x03)
					extra += 4;
				regs.pipeline_pos += extra;
			}
			return;
		}
	}
	if (regs.pipeline_pos > 2) {
		regs.pipeline_pos -= 2;
		// If stop set, prefetches stop 1 word early.
		if (regs.pipeline_stop > 0 && regs.pipeline_pos == 2)
			regs.pipeline_stop = -1;
		return;
	}
	if (regs.pipeline_stop) {
		regs.pipeline_stop = -1;
		return;
	}
#if PIPELINE_DEBUG
	pipeline_opcode = w;
#endif
	regs.pipeline_r8[0] = cpudatatbl[w].disp020[0];
	regs.pipeline_r8[1] = cpudatatbl[w].disp020[1];
	regs.pipeline_pos = cpudatatbl[w].length;
#if PIPELINE_DEBUG
	if (!regs.pipeline_pos) {
		write_log(_T("Opcode %04x has no size PC=%08x!\n"), w, pc);
	}
#endif
	// illegal instructions, TRAP, TRAPV, A-line, F-line don't stop prefetches
	int branch = cpudatatbl[w].branch;
	if (regs.pipeline_pos > 0 && branch) {
		// Short branches (Bcc.s) still do one more prefetch.
#if 0
		// RTS and other unconditional single opcode instruction stop immediately.
		if (branch == 2) {
			// Immediate stop
			regs.pipeline_stop = -1;
		} else {
			// Stop 1 word early than normally
			regs.pipeline_stop = 1;
		}
#else
		regs.pipeline_stop = 1;
#endif
	}
}

#endif

static uae_u32 get_word_ce020_prefetch_2 (int o, bool opcode)
{
	uae_u32 pc = m68k_getpc () + o;
	uae_u32 v;

	v = regs.prefetch020[0];
	regs.prefetch020[0] = regs.prefetch020[1];
	regs.prefetch020[1] = regs.prefetch020[2];
#if MORE_ACCURATE_68020_PIPELINE
	pipeline_020(pc);
#endif
	if (pc & 2) {
		// branch instruction detected in pipeline: stop fetches until branch executed.
		if (!MORE_ACCURATE_68020_PIPELINE || regs.pipeline_stop >= 0) {
			fill_icache020 (pc + 2 + 4, opcode);
		}
		regs.prefetch020[2] = regs.cacheholdingdata020 >> 16;
	} else {
		regs.prefetch020[2] = (uae_u16)regs.cacheholdingdata020;
	}
	regs.db = regs.prefetch020[0];
	do_cycles_ce020_internal(2);
	return v;
}

uae_u32 get_word_ce020_prefetch (int o)
{
	return get_word_ce020_prefetch_2(o, false);
}

uae_u32 get_word_ce020_prefetch_opcode (int o)
{
	return get_word_ce020_prefetch_2(o, true);
}

uae_u32 get_word_020_prefetch (int o)
{
	uae_u32 pc = m68k_getpc () + o;
	uae_u32 v;

	v = regs.prefetch020[0];
	regs.prefetch020[0] = regs.prefetch020[1];
	regs.prefetch020[1] = regs.prefetch020[2];
#if MORE_ACCURATE_68020_PIPELINE
	pipeline_020(pc);
#endif
	if (pc & 2) {
		// branch instruction detected in pipeline: stop fetches until branch executed.
		if (!MORE_ACCURATE_68020_PIPELINE || regs.pipeline_stop >= 0) {
			fill_icache020 (pc + 2 + 4, false);
		}
		regs.prefetch020[2] = regs.cacheholdingdata020 >> 16;
	} else {
		regs.prefetch020[2] = (uae_u16)regs.cacheholdingdata020;
	}
	regs.db = regs.prefetch020[0];
	return v;
}

// these are also used by 68030.

#if 0
#define RESET_CE020_CYCLES \
	regs.ce020memcycles = 0; \
	regs.ce020memcycle_data = true;
#define STORE_CE020_CYCLES \
	unsigned long cycs = get_cycles ()
#define ADD_CE020_CYCLES \
	regs.ce020memcycles += get_cycles () - cycs
#endif

uae_u32 mem_access_delay_long_read_ce020 (uaecptr addr)
{
	uae_u32 v;
	start_020_cycle();
	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
		v  = wait_cpu_cycle_read_ce020 (addr + 0, 1) << 16;
		v |= wait_cpu_cycle_read_ce020 (addr + 2, 1) <<  0;
		break;
	case CE_MEMBANK_CHIP32:
		if ((addr & 3) != 0) {
			v  = wait_cpu_cycle_read_ce020 (addr + 0, 1) << 16;
			v |= wait_cpu_cycle_read_ce020 (addr + 2, 1) <<  0;
		} else {
			v = wait_cpu_cycle_read_ce020 (addr, -1);
		}
		break;
	case CE_MEMBANK_FAST32:
		v = get_long (addr);
		if ((addr & 3) != 0)
			do_cycles_ce020_mem (2 * CPU020_MEM_CYCLE, v);
		else
			do_cycles_ce020_mem (1 * CPU020_MEM_CYCLE, v);
		break;
	case CE_MEMBANK_FAST16:
		v = get_long (addr);
		do_cycles_ce020_mem (2 * CPU020_MEM_CYCLE, v);
		break;
	default:
		v = get_long (addr);
		break;
	}
	end_020_cycle();
	return v;
}

uae_u32 mem_access_delay_longi_read_ce020 (uaecptr addr)
{
	uae_u32 v;
	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
		v  = wait_cpu_cycle_read_ce020 (addr + 0, 2) << 16;
		v |= wait_cpu_cycle_read_ce020 (addr + 2, 2) <<  0;
		break;
	case CE_MEMBANK_CHIP32:
		if ((addr & 3) != 0) {
			v  = wait_cpu_cycle_read_ce020 (addr + 0, 2) << 16;
			v |= wait_cpu_cycle_read_ce020 (addr + 2, 2) <<  0;
		} else {
			v = wait_cpu_cycle_read_ce020 (addr, -2);
		}
		break;
	case CE_MEMBANK_FAST32:
		v = get_longi (addr);
		if ((addr & 3) != 0)
			do_cycles_ce020_mem (2 * CPU020_MEM_CYCLE, v);
		else
			do_cycles_ce020_mem (1 * CPU020_MEM_CYCLE, v);
		break;
	case CE_MEMBANK_FAST16:
		v = get_longi (addr);
		do_cycles_ce020_mem (2 * CPU020_MEM_CYCLE, v);
		break;
	default:
		v = get_longi (addr);
		break;
	}
	return v;
}

uae_u32 mem_access_delay_wordi_read_ce020 (uaecptr addr)
{
	uae_u32 v;
	start_020_cycle();
	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
	case CE_MEMBANK_CHIP32:
		if ((addr & 3) == 3) {
			v  = wait_cpu_cycle_read_ce020 (addr + 0, 0) << 8;
			v |= wait_cpu_cycle_read_ce020 (addr + 1, 0) << 0;
		} else {
			v = wait_cpu_cycle_read_ce020 (addr, 1);
		}
		break;
	case CE_MEMBANK_FAST16:
	case CE_MEMBANK_FAST32:
		v = get_wordi (addr);
		if ((addr & 3) == 3)
			do_cycles_ce020_mem (2 * CPU020_MEM_CYCLE, v);
		else
			do_cycles_ce020_mem (1 * CPU020_MEM_CYCLE, v);
		 break;
	default:
		 v = get_wordi (addr);
		break;
	}
	end_020_cycle();
	return v;
}

uae_u32 mem_access_delay_word_read_ce020 (uaecptr addr)
{
	uae_u32 v;
	start_020_cycle();
	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
	case CE_MEMBANK_CHIP32:
		if ((addr & 3) == 3) {
			v  = wait_cpu_cycle_read_ce020 (addr + 0, 0) << 8;
			v |= wait_cpu_cycle_read_ce020 (addr + 1, 0) << 0;
		} else {
			v = wait_cpu_cycle_read_ce020 (addr, 1);
		}
		break;
	case CE_MEMBANK_FAST16:
	case CE_MEMBANK_FAST32:
		v = get_word (addr);
		if ((addr & 3) == 3)
			do_cycles_ce020_mem (2 * CPU020_MEM_CYCLE, v);
		else
			do_cycles_ce020_mem (1 * CPU020_MEM_CYCLE, v);
		 break;
	default:
		 v = get_word (addr);
		break;
	}
	end_020_cycle();
	return v;
}

uae_u32 mem_access_delay_byte_read_ce020 (uaecptr addr)
{
	uae_u32 v;
	start_020_cycle();
	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
	case CE_MEMBANK_CHIP32:
		v = wait_cpu_cycle_read_ce020 (addr, 0);
		break;
	case CE_MEMBANK_FAST16:
	case CE_MEMBANK_FAST32:
		v = get_byte (addr);
		do_cycles_ce020_mem (1 * CPU020_MEM_CYCLE, v);
		break;
	default:
		v = get_byte (addr);
		break;
	}
	end_020_cycle();
	return v;
}

void mem_access_delay_byte_write_ce020 (uaecptr addr, uae_u32 v)
{
	start_020_cycle();
	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
	case CE_MEMBANK_CHIP32:
		wait_cpu_cycle_write_ce020 (addr, 0, v);
		break;
	case CE_MEMBANK_FAST16:
	case CE_MEMBANK_FAST32:
		put_byte (addr, v);
		do_cycles_ce020_mem (1 * CPU020_MEM_CYCLE, v);
		break;
	default:
		put_byte (addr, v);
	break;
	}
	end_020_cycle();
}

void mem_access_delay_word_write_ce020 (uaecptr addr, uae_u32 v)
{
	start_020_cycle();
	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
	case CE_MEMBANK_CHIP32:
		if ((addr & 3) == 3) {
			wait_cpu_cycle_write_ce020 (addr + 0, 0, (v >> 8) & 0xff);
			wait_cpu_cycle_write_ce020 (addr + 1, 0, (v >> 0) & 0xff);
		} else {
			wait_cpu_cycle_write_ce020 (addr + 0, 1, v);
		}
		break;
	case CE_MEMBANK_FAST16:
	case CE_MEMBANK_FAST32:
		put_word (addr, v);
		if ((addr & 3) == 3)
			do_cycles_ce020_mem (2 * CPU020_MEM_CYCLE, v);
		else
			do_cycles_ce020_mem (1 * CPU020_MEM_CYCLE, v);
		break;
	default:
		put_word (addr, v);
	break;
	}
	end_020_cycle();
}

void mem_access_delay_long_write_ce020 (uaecptr addr, uae_u32 v)
{
	start_020_cycle();
	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
		wait_cpu_cycle_write_ce020 (addr + 0, 1, (v >> 16) & 0xffff);
		wait_cpu_cycle_write_ce020 (addr + 2, 1, (v >>  0) & 0xffff);
		break;
	case CE_MEMBANK_CHIP32:
		if ((addr & 3) == 3) {
			wait_cpu_cycle_write_ce020 (addr + 0, 1, (v >> 16) & 0xffff);
			wait_cpu_cycle_write_ce020 (addr + 2, 1, (v >>  0) & 0xffff);
		} else {
			wait_cpu_cycle_write_ce020 (addr + 0, -1, v);
		}
		break;
	case CE_MEMBANK_FAST32:
		put_long (addr, v);
		if ((addr & 3) != 0)
			do_cycles_ce020_mem (2 * CPU020_MEM_CYCLE, v);
		else
			do_cycles_ce020_mem (1 * CPU020_MEM_CYCLE, v);
		break;
	case CE_MEMBANK_FAST16:
		put_long (addr, v);
		do_cycles_ce020_mem (2 * CPU020_MEM_CYCLE, v);
		break;
	default:
		put_long (addr, v);
		break;
	}
	end_020_cycle();
}


// 68030 caches aren't so simple as 68020 cache..

STATIC_INLINE struct cache030 *geticache030 (struct cache030 *cp, uaecptr addr, uae_u32 *tagp, int *lwsp)
{
	int index, lws;
	uae_u32 tag;
	struct cache030 *c;

	addr &= ~3;
	index = (addr >> 4) & (CACHELINES030 - 1);
	tag = regs.s | (addr & ~((CACHELINES030 << 4) - 1));
	lws = (addr >> 2) & 3;
	c = &cp[index];
	*tagp = tag;
	*lwsp = lws;
	return c;
}

STATIC_INLINE void update_icache030 (struct cache030 *c, uae_u32 val, uae_u32 tag, int lws)
{
	if (c->tag != tag)
		c->valid[0] = c->valid[1] = c->valid[2] = c->valid[3] = false;
	c->tag = tag;
	c->valid[lws] = true;
	c->data[lws] = val;
}

STATIC_INLINE struct cache030 *getdcache030 (struct cache030 *cp, uaecptr addr, uae_u32 *tagp, int *lwsp)
{
	int index, lws;
	uae_u32 tag;
	struct cache030 *c;

	addr &= ~3;
	index = (addr >> 4) & (CACHELINES030 - 1);
	tag = addr & ~((CACHELINES030 << 4) - 1);
	lws = (addr >> 2) & 3;
	c = &cp[index];
	*tagp = tag;
	*lwsp = lws;
	return c;
}

STATIC_INLINE void update_dcache030 (struct cache030 *c, uae_u32 val, uae_u32 tag, uae_u8 fc, int lws)
{
	if (c->tag != tag)
		c->valid[0] = c->valid[1] = c->valid[2] = c->valid[3] = false;
	c->tag = tag;
	c->fc = fc;
	c->valid[lws] = true;
	c->data[lws] = val;
}

static void fill_icache030 (uae_u32 addr)
{
	int lws;
	uae_u32 tag;
	uae_u32 data;
	struct cache030 *c;

	regs.fc030 = (regs.s ? 4 : 0) | 2;
	addr &= ~3;
	if (regs.cacheholdingaddr020 == addr || regs.cacheholdingdata_valid == 0)
		return;
	c = geticache030 (icaches030, addr, &tag, &lws);
	if ((regs.cacr & 1) && c->valid[lws] && c->tag == tag) {
		// cache hit
		regs.cacheholdingaddr020 = addr;
		regs.cacheholdingdata020 = c->data[lws];
		return;
	}

	TRY (prb2) {
		// cache miss
		if (currprefs.cpu_cycle_exact) {
			regs.ce020memcycle_data = false;
			start_020_cycle_prefetch(false);
			data = icache_fetch(addr);
			end_020_cycle_prefetch(false);
		} else {
			data = icache_fetch(addr);
		}
	} CATCH (prb2) {
		// Bus error/MMU access fault: Delayed exception.
		regs.cacheholdingdata_valid = 0;
		regs.cacheholdingaddr020 = 0xffffffff;
		regs.cacheholdingdata020 = 0xffffffff;
		end_020_cycle_prefetch(false);
		STOPTRY;
		return;
	} ENDTRY

	if (mmu030_cache_state & CACHE_ENABLE_INS) {
		if ((regs.cacr & 0x03) == 0x01) {
			// instruction cache not frozen and enabled
			update_icache030 (c, data, tag, lws);
		}
		if ((mmu030_cache_state & CACHE_ENABLE_INS_BURST) && (regs.cacr & 0x11) == 0x11 && (c->valid[0] + c->valid[1] + c->valid[2] + c->valid[3] == 1)) {
			// do burst fetch if cache enabled, not frozen, all slots invalid, no chip ram
			int i;
			for (i = 0; i < 4; i++) {
				if (c->valid[i])
					break;
			}
			uaecptr baddr = addr & ~15;
			if (currprefs.mmu_model) {
				TRY (prb) {
					if (currprefs.cpu_cycle_exact)
						do_cycles_ce020(3 * (CPU020_MEM_CYCLE - 1));
					for (int j = 0; j < 3; j++) {
						i++;
						i &= 3;
						c->data[i] = icache_fetch(baddr + i * 4);
						c->valid[i] = true;
					}
				} CATCH (prb) {
					; // abort burst fetch if bus error, do not report it.
				} ENDTRY
			} else {
				for (int j = 0; j < 3; j++) {
					i++;
					i &= 3;
					c->data[i] = icache_fetch(baddr + i * 4);
					c->valid[i] = true;
				}
				if (currprefs.cpu_cycle_exact)
					do_cycles_ce020_mem (3 * (CPU020_MEM_CYCLE - 1), c->data[3]);
			}
		}
	}
	regs.cacheholdingaddr020 = addr;
	regs.cacheholdingdata020 = data;
}

#if VALIDATE_68030_DATACACHE
static void validate_dcache030(void)
{
	for (int i = 0; i < CACHELINES030; i++) {
		struct cache030 *c = &dcaches030[i];
		uae_u32 addr = c->tag & ~((CACHELINES030 << 4) - 1);
		addr |= i << 4;
		for (int j = 0; j < 4; j++) {
			if (c->valid[j]) {
				uae_u32 v = get_long(addr);
				if (v != c->data[j]) {
					write_log(_T("Address %08x data cache mismatch %08x != %08x\n"), addr, v, c->data[j]);
				}
			}
			addr += 4;
		}
	}
}

static void validate_dcache030_read(uae_u32 addr, uae_u32 ov, int size)
{
	uae_u32 ov2;
	if (size == 2) {
		ov2 = get_long(addr);
	} else if (size == 1) {
		ov2 = get_word(addr);
		ov &= 0xffff;
	} else {
		ov2 = get_byte(addr);
		ov &= 0xff;
	}
	if (ov2 != ov) {
		write_log(_T("Address read %08x data cache mismatch %08x != %08x\n"), addr, ov2, ov);
	}
}
#endif

// and finally the worst part, 68030 data cache..
static void write_dcache030x(uaecptr addr, uae_u32 val, uae_u32 size, uae_u32 fc)
{
	if (regs.cacr & 0x100) {
		static const uae_u32 mask[3] = { 0xff000000, 0xffff0000, 0xffffffff };
		struct cache030 *c1, *c2;
		int lws1, lws2;
		uae_u32 tag1, tag2;
		int aligned = addr & 3;
		int wa = regs.cacr & 0x2000;
		int width = 8 << size;
		int offset = 8 * aligned;
		int hit;

		c1 = getdcache030(dcaches030, addr, &tag1, &lws1);
		hit = c1->tag == tag1 && c1->fc == fc && c1->valid[lws1];

		// Write-allocate can create new valid cache entry if
		// long aligned long write and MMU CI is not active.
		// All writes ignore external CIIN signal.
		// CACHE_DISABLE_ALLOCATE = emulation only method to disable WA caching completely.

		if (width == 32 && offset == 0 && wa) {
			if (!(mmu030_cache_state & CACHE_DISABLE_MMU) && !(mmu030_cache_state & CACHE_DISABLE_ALLOCATE)) {
				update_dcache030(c1, val, tag1, fc, lws1);
#if VALIDATE_68030_DATACACHE
				validate_dcache030();
#endif
			} else if (hit) {
				// Does real 68030 do this if MMU cache inhibited?
				c1->valid[lws1] = false;
			}
			return;
		}

		if (hit || wa) {
			if (hit) {
				uae_u32 val_left_aligned = val << (32 - width);
				c1->data[lws1] &= ~(mask[size] >> offset);
				c1->data[lws1] |= val_left_aligned >> offset;
			} else {
				c1->valid[lws1] = false;
			}
		}

		// do we need to update a 2nd cache entry ?
		if (width + offset > 32) {
			c2 = getdcache030(dcaches030, addr + 4, &tag2, &lws2);
			hit = c2->tag == tag2 && c2->fc == fc && c2->valid[lws2];
			if (hit || wa) {
				if (hit) {
					c2->data[lws2] &= 0xffffffff >> (width + offset - 32);
					c2->data[lws2] |= val << (32 - (width + offset - 32));
				} else {
					c2->valid[lws2] = false;
				}
			}
		}
#if VALIDATE_68030_DATACACHE
		validate_dcache030();
#endif
	}
}

void write_dcache030_bput(uaecptr addr, uae_u32 v,uae_u32 fc)
{
	regs.fc030 = fc;
	dcache_bput(addr, v);
	write_dcache030x(addr, v, 0, fc);
}
void write_dcache030_wput(uaecptr addr, uae_u32 v,uae_u32 fc)
{
	regs.fc030 = fc;
	dcache_wput(addr, v);
	write_dcache030x(addr, v, 1, fc);
}
void write_dcache030_lput(uaecptr addr, uae_u32 v,uae_u32 fc)
{
	regs.fc030 = fc;
	dcache_lput(addr, v);
	write_dcache030x(addr, v, 2, fc);
}

// 68030 MMU bus fault retry case, direct write, store to cache if enabled
void write_dcache030_retry(uaecptr addr, uae_u32 v, uae_u32 fc, int size, int flags)
{
	regs.fc030 = fc;
	mmu030_put_generic(addr, v, fc, size, flags);
	write_dcache030x(addr, v, size, fc);
}

static void dcache030_maybe_burst(uaecptr addr, struct cache030 *c, int lws)
{
	if ((c->valid[0] + c->valid[1] + c->valid[2] + c->valid[3] == 1) && ce_banktype[addr >> 16] == CE_MEMBANK_FAST32) {
		// do burst fetch if cache enabled, not frozen, all slots invalid, no chip ram
		int i;
		uaecptr baddr = addr & ~15;
		for (i = 0; i < 4; i++) {
			if (c->valid[i])
				break;
		}
		if (currprefs.mmu_model) {
			TRY (prb) {
				if (currprefs.cpu_cycle_exact)
					do_cycles_ce020(3 * (CPU020_MEM_CYCLE - 1));
				for (int j = 0; j < 3; j++) {
					i++;
					i &= 3;
					c->data[i] = dcache_lget (baddr + i * 4);
					c->valid[i] = true;
				}
			} CATCH (prb) {
				; // abort burst fetch if bus error
			} ENDTRY
		} else {
			for (int j = 0; j < 3; j++) {
				i++;
				i &= 3;
				c->data[i] = dcache_lget (baddr + i * 4);
				c->valid[i] = true;
			}
			if (currprefs.cpu_cycle_exact)
				do_cycles_ce020_mem (3 * (CPU020_MEM_CYCLE - 1), c->data[i]);
		}
#if VALIDATE_68030_DATACACHE
		validate_dcache030();
#endif
	}
}

static uae_u32 read_dcache030_debug(uaecptr addr, uae_u32 size, uae_u32 fc, bool *cached)
{
	static const uae_u32 mask[3] = { 0x000000ff, 0x0000ffff, 0xffffffff };
	struct cache030 *c1, *c2;
	int lws1, lws2;
	uae_u32 tag1, tag2;
	int aligned = addr & 3;
	uae_u32 v1, v2;
	int width = 8 << size;
	int offset = 8 * aligned;
	uae_u32 out;

	*cached = false;
	if (!currprefs.cpu_data_cache) {
		if (size == 0)
			return get_byte_debug(addr);
		if (size == 1)
			return get_word_debug(addr);
		return get_long_debug(addr);
	}

	c1 = getdcache030(dcaches030, addr, &tag1, &lws1);
	addr &= ~3;
	if (!c1->valid[lws1] || c1->tag != tag1 || c1->fc != fc) {
		v1 = get_long_debug(addr);
	} else {
		// Cache hit, inhibited caching do not prevent read hits.
		v1 = c1->data[lws1];
		*cached = true;
	}

	// only one long fetch needed?
	if (width + offset <= 32) {
		out = v1 >> (32 - (offset + width));
		out &= mask[size];
		return out;
	}

	// no, need another one
	addr += 4;
	c2 = getdcache030(dcaches030, addr, &tag2, &lws2);
	if (!c2->valid[lws2] || c2->tag != tag2 || c2->fc != fc) {
		v2 = get_long_debug(addr);
	} else {
		v2 = c2->data[lws2];
		*cached = true;
	}

	uae_u64 v64 = ((uae_u64)v1 << 32) | v2;
	out = (uae_u32)(v64 >> (64 - (offset + width)));
	out &= mask[size];
	return out;
}

static bool read_dcache030_2(uaecptr addr, uae_u32 size, uae_u32 *valp)
{
	// data cache enabled?
	if (!(regs.cacr & 0x100))
		return false;

	uae_u32 addr_o = addr;
	uae_u32 fc = regs.fc030;
	static const uae_u32 mask[3] = { 0x000000ff, 0x0000ffff, 0xffffffff };
	struct cache030 *c1, *c2;
	int lws1, lws2;
	uae_u32 tag1, tag2;
	int aligned = addr & 3;
	uae_u32 v1, v2;
	int width = 8 << size;
	int offset = 8 * aligned;
	uae_u32 out;

	c1 = getdcache030(dcaches030, addr, &tag1, &lws1);
	addr &= ~3;
	if (!c1->valid[lws1] || c1->tag != tag1 || c1->fc != fc) {
		// MMU validate address, returns zero if valid but uncacheable
		// throws bus error if invalid
		uae_u8 cs = dcache_check(addr_o, false, size);
		if (!(cs & CACHE_ENABLE_DATA))
			return false;
		v1 = dcache_lget(addr);
		update_dcache030(c1, v1, tag1, fc, lws1);
		if ((cs & CACHE_ENABLE_DATA_BURST) && (regs.cacr & 0x1100) == 0x1100)
			dcache030_maybe_burst(addr, c1, lws1);
#if VALIDATE_68030_DATACACHE
		validate_dcache030();
#endif
	} else {
		// Cache hit, inhibited caching do not prevent read hits.
		v1 = c1->data[lws1];
	}

	// only one long fetch needed?
	if (width + offset <= 32) {
		out = v1 >> (32 - (offset + width));
		out &= mask[size];
#if VALIDATE_68030_DATACACHE
		validate_dcache030_read(addr_o, out, size);
#endif
		*valp = out;
		return true;
	}

	// no, need another one
	addr += 4;
	c2 = getdcache030(dcaches030, addr, &tag2, &lws2);
	if (!c2->valid[lws2] || c2->tag != tag2 || c2->fc != fc) {
		uae_u8 cs = dcache_check(addr, false, 2);
		if (!(cs & CACHE_ENABLE_DATA))
			return false;
		v2 = dcache_lget(addr);
		update_dcache030(c2, v2, tag2, fc, lws2);
		if ((cs & CACHE_ENABLE_DATA_BURST) && (regs.cacr & 0x1100) == 0x1100)
			dcache030_maybe_burst(addr, c2, lws2);
#if VALIDATE_68030_DATACACHE
		validate_dcache030();
#endif
	} else {
		v2 = c2->data[lws2];
	}

	uae_u64 v64 = ((uae_u64)v1 << 32) | v2;
	out = (uae_u32)(v64 >> (64 - (offset + width)));
	out &= mask[size];

#if VALIDATE_68030_DATACACHE
	validate_dcache030_read(addr_o, out, size);
#endif
	*valp = out;
	return true;
}

static uae_u32 read_dcache030 (uaecptr addr, uae_u32 size, uae_u32 fc)
{
	uae_u32 val;
	regs.fc030 = fc;

	if (!read_dcache030_2(addr, size, &val)) {
		// read from memory, data cache is disabled or inhibited.
		if (size == 2)
			return dcache_lget(addr);
		else if (size == 1)
			return dcache_wget(addr);
		else
			return dcache_bget(addr);
	}
	return val;
}

// 68030 MMU bus fault retry case, either read from cache or use direct reads
uae_u32 read_dcache030_retry(uaecptr addr, uae_u32 fc, int size, int flags)
{
	uae_u32 val;
	regs.fc030 = fc;

	if (!read_dcache030_2(addr, size, &val)) {
		return mmu030_get_generic(addr, fc, size, flags);
	}
	return val;
}

uae_u32 read_dcache030_bget(uaecptr addr, uae_u32 fc)
{
	return read_dcache030(addr, 0, fc);
}
uae_u32 read_dcache030_wget(uaecptr addr, uae_u32 fc)
{
	return read_dcache030(addr, 1, fc);
}
uae_u32 read_dcache030_lget(uaecptr addr, uae_u32 fc)
{
	return read_dcache030(addr, 2, fc);
}

uae_u32 read_dcache030_mmu_bget(uaecptr addr)
{
	return read_dcache030_bget(addr, (regs.s ? 4 : 0) | 1);
}
uae_u32 read_dcache030_mmu_wget(uaecptr addr)
{
	return read_dcache030_wget(addr, (regs.s ? 4 : 0) | 1);
}
uae_u32 read_dcache030_mmu_lget(uaecptr addr)
{
	return read_dcache030_lget(addr, (regs.s ? 4 : 0) | 1);
}
void write_dcache030_mmu_bput(uaecptr addr, uae_u32 val)
{
	write_dcache030_bput(addr, val, (regs.s ? 4 : 0) | 1);
}
void write_dcache030_mmu_wput(uaecptr addr, uae_u32 val)
{
	write_dcache030_wput(addr, val, (regs.s ? 4 : 0) | 1);
}
void write_dcache030_mmu_lput(uaecptr addr, uae_u32 val)
{
	write_dcache030_lput(addr, val, (regs.s ? 4 : 0) | 1);
}

uae_u32 read_dcache030_lrmw_mmu_fcx(uaecptr addr, uae_u32 size, int fc)
{
	if (currprefs.cpu_data_cache) {
		mmu030_cache_state = CACHE_DISABLE_MMU;
		if (size == 0)
			return read_dcache030_bget(addr, fc);
		if (size == 1)
			return read_dcache030_wget(addr, fc);
		return read_dcache030_lget(addr, fc);
	} else {
		if (size == 0)
			return read_data_030_bget(addr);
		if (size == 1)
			return read_data_030_wget(addr);
		return read_data_030_lget(addr);
	}
}
uae_u32 read_dcache030_lrmw_mmu(uaecptr addr, uae_u32 size)
{
	return read_dcache030_lrmw_mmu_fcx(addr, size, (regs.s ? 4 : 0) | 1);
}
void write_dcache030_lrmw_mmu_fcx(uaecptr addr, uae_u32 val, uae_u32 size, int fc)
{
	if (currprefs.cpu_data_cache) {
		mmu030_cache_state = CACHE_DISABLE_MMU;
		if (size == 0)
			write_dcache030_bput(addr, val, fc);
		else if (size == 1)
			write_dcache030_wput(addr, val, fc);
		else
			write_dcache030_lput(addr, val, fc);
	} else {
		if (size == 0)
			write_data_030_bput(addr, val);
		else if (size == 1)
			write_data_030_wput(addr, val);
		else
			write_data_030_lput(addr, val);
	}
}
void write_dcache030_lrmw_mmu(uaecptr addr, uae_u32 val, uae_u32 size)
{
	write_dcache030_lrmw_mmu_fcx(addr, val, size, (regs.s ? 4 : 0) | 1);
}

static void do_access_or_bus_error(uaecptr pc, uaecptr pcnow)
{
	// TODO: handle external bus errors
	if (!currprefs.mmu_model)
		return;
	if (pc != 0xffffffff)
		regs.instruction_pc = pc;
	mmu030_opcode = -1;
	mmu030_page_fault(pcnow, true, -1, 0);
}

static uae_u32 get_word_ce030_prefetch_2 (int o)
{
	uae_u32 pc = m68k_getpc () + o;
	uae_u32 v;

	v = regs.prefetch020[0];
	regs.prefetch020[0] = regs.prefetch020[1];
	regs.prefetch020[1] = regs.prefetch020[2];
#if MORE_ACCURATE_68020_PIPELINE
	pipeline_020(pc);
#endif
	if (pc & 2) {
		// branch instruction detected in pipeline: stop fetches until branch executed.
		if (!MORE_ACCURATE_68020_PIPELINE || regs.pipeline_stop >= 0) {
			fill_icache030 (pc + 2 + 4);
		} else {
			if (regs.cacheholdingdata_valid > 0)
				regs.cacheholdingdata_valid++;
		}
		regs.prefetch020[2] = regs.cacheholdingdata020 >> 16;
	} else {
		pc += 4;
		// cacheholdingdata020 may be invalid if RTE from bus error
		if ((!MORE_ACCURATE_68020_PIPELINE || regs.pipeline_stop >= 0) && regs.cacheholdingaddr020 != pc) {
			fill_icache030 (pc);
		}
		regs.prefetch020[2] = (uae_u16)regs.cacheholdingdata020;
	}
	regs.db = regs.prefetch020[0];
	do_cycles_ce020_internal(2);
	return v;
}

uae_u32 get_word_ce030_prefetch (int o)
{
	return get_word_ce030_prefetch_2(o);
}
uae_u32 get_word_ce030_prefetch_opcode (int o)
{
	return get_word_ce030_prefetch_2(o);
}

uae_u32 get_word_030_prefetch (int o)
{
	uae_u32 pc = m68k_getpc () + o;
	uae_u32 v;

	v = regs.prefetch020[0];
	regs.prefetch020[0] = regs.prefetch020[1];
	regs.prefetch020[1] = regs.prefetch020[2];
	regs.prefetch020_valid[0] = regs.prefetch020_valid[1];
	regs.prefetch020_valid[1] = regs.prefetch020_valid[2];
	regs.prefetch020_valid[2] = false;
	if (!regs.prefetch020_valid[1]) {
		do_access_or_bus_error(0xffffffff, pc + 4);
	}
#if MORE_ACCURATE_68020_PIPELINE
	pipeline_020(pc);
#endif
	if (pc & 2) {
		// branch instruction detected in pipeline: stop fetches until branch executed.
		if (!MORE_ACCURATE_68020_PIPELINE || regs.pipeline_stop >= 0) {
			fill_icache030 (pc + 2 + 4);
		} else {
			if (regs.cacheholdingdata_valid > 0)
				regs.cacheholdingdata_valid++;
		}
		regs.prefetch020[2] = regs.cacheholdingdata020 >> 16;
	} else {
		pc += 4;
		// cacheholdingdata020 may be invalid if RTE from bus error
		if ((!MORE_ACCURATE_68020_PIPELINE || regs.pipeline_stop >= 0) && regs.cacheholdingaddr020 != pc) {
			fill_icache030 (pc);
		}
		regs.prefetch020[2] = (uae_u16)regs.cacheholdingdata020;
	}
	regs.prefetch020_valid[2] = regs.cacheholdingdata_valid;
	regs.db = regs.prefetch020[0];
	return v;
}

uae_u32 get_word_icache030(uaecptr addr)
{
	fill_icache030(addr);
	return regs.cacheholdingdata020 >> ((addr & 2) ? 0 : 16);
}
uae_u32 get_long_icache030(uaecptr addr)
{
	uae_u32 v;
	fill_icache030(addr);
	if ((addr & 2) == 0)
		return regs.cacheholdingdata020;
	v = regs.cacheholdingdata020 << 16;
	fill_icache030(addr + 4);
	v |= regs.cacheholdingdata020 >> 16;
	return v;
}

uae_u32 fill_icache040(uae_u32 addr)
{
	int index, lws;
	uae_u32 tag, addr2;
	struct cache040 *c;
	int line;

	addr2 = addr & ~15;
	lws = (addr >> 2) & 3;

	if (regs.prefetch020addr == addr2) {
		return regs.prefetch040[lws];
	}

	if (regs.cacr & 0x8000) {
		uae_u8 cs = mmu_cache_state;

		if (!(ce_cachable[addr >> 16] & CACHE_ENABLE_INS))
			cs = CACHE_DISABLE_MMU;

		index = (addr >> 4) & cacheisets04060mask;
		tag = addr & cacheitag04060mask;
		c = &icaches040[index];
		for (int i = 0; i < CACHELINES040; i++) {
			if (c->valid[cache_lastline] && c->tag[cache_lastline] == tag) {
				// cache hit
				if (!(cs & CACHE_ENABLE_INS) || (cs & CACHE_DISABLE_MMU)) {
					c->valid[cache_lastline] = false;
					goto end;
				}
				if ((lws & 1) != icachehalfline) {
					icachehalfline ^= 1;
					icachelinecnt++;
				}
				return c->data[cache_lastline][lws];
			}
			cache_lastline++;
			cache_lastline &= (CACHELINES040 - 1);
		}
		// cache miss
		regs.prefetch020addr = 0xffffffff;
		regs.prefetch040[0] = icache_fetch(addr2 +  0);
		regs.prefetch040[1] = icache_fetch(addr2 +  4);
		regs.prefetch040[2] = icache_fetch(addr2 +  8);
		regs.prefetch040[3] = icache_fetch(addr2 + 12);
		regs.prefetch020addr = addr2;
		if (!(cs & CACHE_ENABLE_INS) || (cs & CACHE_DISABLE_MMU))
			goto end;
		if (regs.cacr & 0x00004000) // 68060 NAI
			goto end;
		if (c->valid[0] && c->valid[1] && c->valid[2] && c->valid[3]) {
			line = icachelinecnt & (CACHELINES040 - 1);
			icachehalfline = (lws & 1) ? 0 : 1;
		} else {
			for (line = 0; line < CACHELINES040; line++) {
				if (c->valid[line] == false)
					break;
			}
		}
		c->tag[line] = tag;
		c->valid[line] = true;
		c->data[line][0] = regs.prefetch040[0];
		c->data[line][1] = regs.prefetch040[1];
		c->data[line][2] = regs.prefetch040[2];
		c->data[line][3] = regs.prefetch040[3];
		if ((lws & 1) != icachehalfline) {
			icachehalfline ^= 1;
			icachelinecnt++;
		}
		return c->data[line][lws];

	}

end:
	if (regs.prefetch020addr == addr2)
		return regs.prefetch040[lws];
	regs.prefetch020addr = addr2;
	regs.prefetch040[0] = icache_fetch(addr2 +  0);
	regs.prefetch040[1] = icache_fetch(addr2 +  4);
	regs.prefetch040[2] = icache_fetch(addr2 +  8);
	regs.prefetch040[3] = icache_fetch(addr2 + 12);
	return regs.prefetch040[lws];
}

STATIC_INLINE void do_cycles_c040_mem (int clocks, uae_u32 val)
{
	x_do_cycles_post (clocks * cpucycleunit, val);
}

uae_u32 mem_access_delay_longi_read_c040 (uaecptr addr)
{
	uae_u32 v;
	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
		v  = wait_cpu_cycle_read_ce020 (addr + 0, 1) << 16;
		v |= wait_cpu_cycle_read_ce020 (addr + 2, 1) <<  0;
		break;
	case CE_MEMBANK_CHIP32:
		if ((addr & 3) != 0) {
			v  = wait_cpu_cycle_read_ce020 (addr + 0, 1) << 16;
			v |= wait_cpu_cycle_read_ce020 (addr + 2, 1) <<  0;
		} else {
			v = wait_cpu_cycle_read_ce020 (addr, -1);
		}
		break;
	case CE_MEMBANK_FAST16:
		v = get_longi (addr);
		do_cycles_c040_mem(1, v);
		break;
	case CE_MEMBANK_FAST32:
		v = get_longi (addr);
		break;
	default:
		v = get_longi (addr);
		break;
	}
	return v;
}
uae_u32 mem_access_delay_long_read_c040 (uaecptr addr)
{
	uae_u32 v;
	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
		v  = wait_cpu_cycle_read_ce020 (addr + 0, 1) << 16;
		v |= wait_cpu_cycle_read_ce020 (addr + 2, 1) <<  0;
		break;
	case CE_MEMBANK_CHIP32:
		if ((addr & 3) != 0) {
			v  = wait_cpu_cycle_read_ce020 (addr + 0, 1) << 16;
			v |= wait_cpu_cycle_read_ce020 (addr + 2, 1) <<  0;
		} else {
			v = wait_cpu_cycle_read_ce020 (addr, -1);
		}
		break;
	case CE_MEMBANK_FAST16:
		v = get_long (addr);
		do_cycles_c040_mem(1, v);
		break;
	case CE_MEMBANK_FAST32:
		v = get_long (addr);
		break;
	default:
		v = get_long (addr);
		break;
	}
	return v;
}

uae_u32 mem_access_delay_word_read_c040 (uaecptr addr)
{
	uae_u32 v;
	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
	case CE_MEMBANK_CHIP32:
		if ((addr & 3) == 3) {
			v  = wait_cpu_cycle_read_ce020 (addr + 0, 0) << 8;
			v |= wait_cpu_cycle_read_ce020 (addr + 1, 0) << 0;
		} else {
			v = wait_cpu_cycle_read_ce020 (addr, 1);
		}
		break;
	case CE_MEMBANK_FAST16:
		v = get_word (addr);
		do_cycles_c040_mem (2, v);
		break;
	case CE_MEMBANK_FAST32:
		v = get_word (addr);
		 break;
	default:
		 v = get_word (addr);
		break;
	}
	return v;
}

uae_u32 mem_access_delay_byte_read_c040 (uaecptr addr)
{
	uae_u32 v;
	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
	case CE_MEMBANK_CHIP32:
		v = wait_cpu_cycle_read_ce020 (addr, 0);
		break;
	case CE_MEMBANK_FAST16:
		v = get_byte (addr);
		do_cycles_c040_mem (1, v);
		break;
	case CE_MEMBANK_FAST32:
		v = get_byte (addr);
		break;
	default:
		v = get_byte (addr);
		break;
	}
	return v;
}

void mem_access_delay_byte_write_c040 (uaecptr addr, uae_u32 v)
{
	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
	case CE_MEMBANK_CHIP32:
		wait_cpu_cycle_write_ce020 (addr, 0, v);
		break;
	case CE_MEMBANK_FAST16:
		put_byte (addr, v);
		do_cycles_c040_mem (1, v);
		break;
	case CE_MEMBANK_FAST32:
		put_byte (addr, v);
		break;
	default:
		put_byte (addr, v);
	break;
	}
}

void mem_access_delay_word_write_c040 (uaecptr addr, uae_u32 v)
{
	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
	case CE_MEMBANK_CHIP32:
		if ((addr & 3) == 3) {
			wait_cpu_cycle_write_ce020 (addr + 0, 0, (v >> 8) & 0xff);
			wait_cpu_cycle_write_ce020 (addr + 1, 0, (v >> 0) & 0xff);
		} else {
			wait_cpu_cycle_write_ce020 (addr + 0, 1, v);
		}
		break;
	case CE_MEMBANK_FAST16:
		put_word (addr, v);
		if ((addr & 3) == 3)
			do_cycles_c040_mem(2, v);
		else
			do_cycles_c040_mem(1, v);
		break;
	case CE_MEMBANK_FAST32:
		put_word (addr, v);
		break;
	default:
		put_word (addr, v);
	break;
	}
}

void mem_access_delay_long_write_c040 (uaecptr addr, uae_u32 v)
{
	switch (ce_banktype[addr >> 16])
	{
	case CE_MEMBANK_CHIP16:
		wait_cpu_cycle_write_ce020 (addr + 0, 1, (v >> 16) & 0xffff);
		wait_cpu_cycle_write_ce020 (addr + 2, 1, (v >>  0) & 0xffff);
		break;
	case CE_MEMBANK_CHIP32:
		if ((addr & 3) == 3) {
			wait_cpu_cycle_write_ce020 (addr + 0, 1, (v >> 16) & 0xffff);
			wait_cpu_cycle_write_ce020 (addr + 2, 1, (v >>  0) & 0xffff);
		} else {
			wait_cpu_cycle_write_ce020 (addr + 0, -1, v);
		}
		break;
	case CE_MEMBANK_FAST16:
		put_long (addr, v);
		do_cycles_ce020_mem (2 * CPU020_MEM_CYCLE, v);
		break;
	case CE_MEMBANK_FAST32:
		put_long (addr, v);
		break;
	default:
		put_long (addr, v);
		break;
	}
}

static uae_u32 dcache040_get_data(uaecptr addr, struct cache040 *c, int line, int size)
{
	static const uae_u32 mask[3] = { 0x000000ff, 0x0000ffff, 0xffffffff };
	int offset = (addr & 15) * 8;
	int offset32 = offset & 31;
	int slot = offset / 32;
	int width = 8 << size;
	uae_u32 vv;

	if (offset32 + width <= 32) {
		uae_u32 v = c->data[line][slot];
		v >>= 32 - (offset32 + width);
		v &= mask[size];
		vv = v;
	} else {
#if VALIDATE_68040_DATACACHE
		if (slot >= 3) {
			write_log(_T("invalid dcache040_get_data!\n"));
			return 0;
		}
#endif
		uae_u64 v = c->data[line][slot];
		v <<= 32;
		v |= c->data[line][slot + 1];
		v >>= 64 - (offset32 + width);
		vv = v & mask[size];
	}
	return vv;
}

static void dcache040_update(uaecptr addr, struct cache040 *c, int line, uae_u32 val, int size)
{
	static const uae_u64 mask64[3] = { 0xff, 0xffff, 0xffffffff };
	static const uae_u32 mask32[3] = { 0xff, 0xffff, 0xffffffff };
	int offset = (addr & 15) * 8;
	int offset32 = offset & 31;
	int slot = offset / 32;
	int width = 8 << size;

#if VALIDATE_68040_DATACACHE > 1
	validate_dcache040();
#endif

	if (offset32 + width <= 32) {
		int shift = 32 - (offset32 + width);
		uae_u32 v = c->data[line][slot];
		v &= ~(mask32[size] << shift);
		v |= val << shift;
		c->data[line][slot] = v;
		c->dirty[line][slot] = true;
	} else {
#if VALIDATE_68040_DATACACHE
		if (slot >= 3) {
			write_log(_T("invalid dcache040_update!\n"));
			return;
		}
#endif
		int shift = 64 - (offset32 + width);
		uae_u64 v = c->data[line][slot];
		v <<= 32;
		v |= c->data[line][slot + 1];
		v &= ~(mask64[size] << shift);
		v |= ((uae_u64)val) << shift;
		c->data[line][slot] = v >> 32;
		c->dirty[line][slot] = true;
		c->data[line][slot + 1] = (uae_u32)v;
		c->dirty[line][slot + 1] = true;
	}
	c->gdirty[line] = true;
}

static int dcache040_fill_line(int index, uae_u32 tag, uaecptr addr)
{
	// cache miss
	struct cache040 *c = &dcaches040[index];
	int line;
	if (c->valid[0] && c->valid[1] && c->valid[2] && c->valid[3]) {
		// all lines allocated, choose one, push and invalidate.
		line = dcachelinecnt & (CACHELINES040 - 1);
		dcachelinecnt++;
		dcache040_push_line(index, line, false, true);
	} else {
		// at least one invalid
		for (line = 0; line < CACHELINES040; line++) {
			if (c->valid[line] == false)
				break;
		}
	}
	c->tag[line] = tag;
	c->dirty[line][0] = false;
	c->dirty[line][1] = false;
	c->dirty[line][2] = false;
	c->dirty[line][3] = false;
	c->gdirty[line] = false;
	c->data[line][0] = dcache_lget(addr + 0);
	c->data[line][1] = dcache_lget(addr + 4);
	c->data[line][2] = dcache_lget(addr + 8);
	c->data[line][3] = dcache_lget(addr + 12);
	c->valid[line] = true;
	return line;
}

static uae_u32 read_dcache040_debug(uae_u32 addr, int size, bool *cached)
{
	int index;
	uae_u32 tag;
	struct cache040 *c;
	int line;
	uae_u32 addr_o = addr;
	uae_u8 cs = mmu_cache_state;

	*cached = false;
	if (!currprefs.cpu_data_cache)
		goto nocache;
	if (!(regs.cacr & 0x80000000))
		goto nocache;

	addr &= ~15;
	index = (addr >> 4) & cachedsets04060mask;
	tag = addr & cachedtag04060mask;
	c = &dcaches040[index];
	for (line = 0; line < CACHELINES040; line++) {
		if (c->valid[line] && c->tag[line] == tag) {
			// cache hit
			return dcache040_get_data(addr_o, c, line, size);
		}
	}
nocache:
	if (size == 0)
		return get_byte_debug(addr);
	if (size == 1)
		return get_word_debug(addr);
	return get_long_debug(addr);
}

static uae_u32 read_dcache040(uae_u32 addr, int size, uae_u32 (*fetch)(uaecptr))
{
	int index;
	uae_u32 tag;
	struct cache040 *c;
	int line;
	uae_u32 addr_o = addr;
	uae_u8 cs = mmu_cache_state;

	if (!(regs.cacr & 0x80000000))
		goto nocache;

#if VALIDATE_68040_DATACACHE > 1
	validate_dcache040();
#endif

	// Simple because 68040+ caches physical addresses (68030 caches logical addresses)
	if (!(ce_cachable[addr >> 16] & CACHE_ENABLE_DATA))
		cs = CACHE_DISABLE_MMU;

	addr &= ~15;
	index = (addr >> 4) & cachedsets04060mask;
	tag = addr & cachedtag04060mask;
	c = &dcaches040[index];
	for (line = 0; line < CACHELINES040; line++) {
		if (c->valid[line] && c->tag[line] == tag) {
			// cache hit
			dcachelinecnt++;
			// Cache hit but MMU disabled: do not cache, push and invalidate possible existing line
			if (cs & CACHE_DISABLE_MMU) {
				dcache040_push_line(index, line, false, true);
				goto nocache;
			}
			return dcache040_get_data(addr_o, c, line, size);
		}
	}
	// Cache miss
	// 040+ always caches whole line
	if ((cs & CACHE_DISABLE_MMU) || !(cs & CACHE_ENABLE_DATA) || (cs & CACHE_DISABLE_ALLOCATE) || (regs.cacr & 0x40000000)) {
nocache:
		return fetch(addr_o);
	}
	// Allocate new cache line, return requested data.
	line = dcache040_fill_line(index, tag, addr);
	return dcache040_get_data(addr_o, c, line, size);
}

static void write_dcache040(uae_u32 addr, uae_u32 val, int size, void (*store)(uaecptr, uae_u32))
{
	static const uae_u32 mask[3] = { 0x000000ff, 0x0000ffff, 0xffffffff };
	int index;
	uae_u32 tag;
	struct cache040 *c;
	int line;
	uae_u32 addr_o = addr;
	uae_u8 cs = mmu_cache_state;

	val &= mask[size];

	if (!(regs.cacr & 0x80000000))
		goto nocache;

	if (!(ce_cachable[addr >> 16] & CACHE_ENABLE_DATA))
		cs = CACHE_DISABLE_MMU;

	addr &= ~15;
	index = (addr >> 4) & cachedsets04060mask;
	tag = addr & cachedtag04060mask;
	c = &dcaches040[index];
	for (line = 0; line < CACHELINES040; line++) {
		if (c->valid[line] && c->tag[line] == tag) {
			// cache hit
			dcachelinecnt++;
			// Cache hit but MMU disabled: do not cache, push and invalidate possible existing line
			if (cs & CACHE_DISABLE_MMU) {
				dcache040_push_line(index, line, false, true);
				goto nocache;
			}
			dcache040_update(addr_o, c, line, val, size);
			// If not copyback mode: push modifications immediately (write-through)
			if (!(cs & CACHE_ENABLE_COPYBACK) || DISABLE_68040_COPYBACK) {
				dcache040_push_line(index, line, true, false);
			}
			return;
		}
	}
	// Cache miss
	// 040+ always caches whole line
	// Writes misses in write-through mode don't allocate new cache lines
	if (!(cs & CACHE_ENABLE_DATA) || (cs & CACHE_DISABLE_MMU) || (cs & CACHE_DISABLE_ALLOCATE) || !(cs & CACHE_ENABLE_COPYBACK) || (regs.cacr & 0x400000000)) {
nocache:
		store(addr_o, val);
		return;
	}
	// Allocate new cache line and update it with new data.
	line = dcache040_fill_line(index, tag, addr);
	dcache040_update(addr_o, c, line, val, size);
	if (DISABLE_68040_COPYBACK) {
		dcache040_push_line(index, line, true, false);
	}
}

// really unoptimized
uae_u32 get_word_icache040(uaecptr addr)
{
	uae_u32 v = fill_icache040(addr);
	return v >> ((addr & 2) ? 0 : 16);
}
uae_u32 get_long_icache040(uaecptr addr)
{
	uae_u32 v1, v2;
	v1 = fill_icache040(addr);
	if ((addr & 2) == 0)
		return v1;
	v2 = fill_icache040(addr + 4);
	return (v2 >> 16) | (v1 << 16);
}
uae_u32 get_ilong_cache_040(int o)
{
	return get_long_icache040(m68k_getpci() + o);
}
uae_u32 get_iword_cache_040(int o)
{
	return get_word_icache040(m68k_getpci() + o);
}

void put_long_cache_040(uaecptr addr, uae_u32 v)
{
	int offset = addr & 15;
	// access must not cross cachelines
	if (offset < 13) {
		write_dcache040(addr, v, 2, dcache_lput);
	} else if (offset == 13 || offset == 15) {
		write_dcache040(addr + 0, v >> 24, 0, dcache_bput);
		write_dcache040(addr + 1, v >>  8, 1, dcache_wput);
		write_dcache040(addr + 3, v >>  0, 0, dcache_bput);
	} else if (offset == 14) {
		write_dcache040(addr + 0, v >> 16, 1, dcache_wput);
		write_dcache040(addr + 2, v >>  0, 1, dcache_wput);
	}
}
void put_word_cache_040(uaecptr addr, uae_u32 v)
{
	int offset = addr & 15;
	if (offset < 15) {
		write_dcache040(addr, v, 1, dcache_wput);
	} else {
		write_dcache040(addr + 0, v >> 8, 0, dcache_bput);
		write_dcache040(addr + 1, v >> 0, 0, dcache_bput);
	}
}
void put_byte_cache_040(uaecptr addr, uae_u32 v)
{
	return write_dcache040(addr, v, 0, dcache_bput);
}

uae_u32 get_long_cache_040(uaecptr addr)
{
	uae_u32 v;
	int offset = addr & 15;
	if (offset < 13) {
		v = read_dcache040(addr, 2, dcache_lget);
	} else if (offset == 13 || offset == 15) {
		v =  read_dcache040(addr + 0, 0, dcache_bget) << 24;
		v |= read_dcache040(addr + 1, 1, dcache_wget) <<  8;
		v |= read_dcache040(addr + 3, 0, dcache_bget) <<  0;
	} else /* if (offset == 14) */ {
		v =  read_dcache040(addr + 0, 1, dcache_wget) << 16;
		v |= read_dcache040(addr + 2, 1, dcache_wget) <<  0;
	}
	return v;
}
uae_u32 get_word_cache_040(uaecptr addr)
{
	uae_u32 v;
	int offset = addr & 15;
	if (offset < 15) {
		v = read_dcache040(addr, 1, dcache_wget);
	} else {
		v =  read_dcache040(addr + 0, 0, dcache_bget) << 8;
		v |= read_dcache040(addr + 1, 0, dcache_bget) << 0;
	}
	return v;
}
uae_u32 get_byte_cache_040(uaecptr addr)
{
	return read_dcache040(addr, 0, dcache_bget);
}
uae_u32 next_iword_cache040(void)
{
	uae_u32 r = get_word_icache040(m68k_getpci());
	m68k_incpci(2);
	return r;
}
uae_u32 next_ilong_cache040(void)
{
	uae_u32 r = get_long_icache040(m68k_getpci());
	m68k_incpci(4);
	return r;
}

uae_u32 get_byte_cache_debug(uaecptr addr, bool *cached)
{
	*cached = false;
	if (currprefs.cpu_model == 68030) {
		return read_dcache030_debug(addr, 0, regs.s ? 5 : 1, cached);
	} else if (currprefs.cpu_model >= 68040) {
		return read_dcache040_debug(addr, 0, cached);
	}
	return get_byte_debug(addr);
}
uae_u32 get_word_cache_debug(uaecptr addr, bool *cached)
{
	*cached = false;
	if (currprefs.cpu_model == 68030) {
		return read_dcache030_debug(addr, 1, regs.s ? 5 : 1, cached);
	} else if (currprefs.cpu_model >= 68040) {
		return read_dcache040_debug(addr, 1, cached);
	}
	return get_word_debug(addr);
}

uae_u32 get_long_cache_debug(uaecptr addr, bool *cached)
{
	*cached = false;
	if (currprefs.cpu_model == 68030) {
		return read_dcache030_debug(addr, 2, regs.s ? 5 : 1, cached);
	} else if (currprefs.cpu_model >= 68040) {
		return read_dcache040_debug(addr, 2, cached);
	}
	return get_long_debug(addr);
}

void check_t0_trace(void)
{
	if (regs.t0 && currprefs.cpu_model >= 68020) {
		unset_special (SPCFLAG_TRACE);
		set_special (SPCFLAG_DOTRACE);
	}
}

static void reset_pipeline_state(void)
{
#if MORE_ACCURATE_68020_PIPELINE
	regs.pipeline_pos = 0;
	regs.pipeline_stop = 0;
	regs.pipeline_r8[0] = regs.pipeline_r8[1] = -1;
#endif
}

static int add_prefetch_030(int idx, uae_u16 w, uaecptr pc)
{
	regs.prefetch020[0] = regs.prefetch020[1];
	regs.prefetch020_valid[0] = regs.prefetch020_valid[1];
	regs.prefetch020[1] = regs.prefetch020[2];
	regs.prefetch020_valid[1] = regs.prefetch020_valid[2];
	regs.prefetch020[2] = w;
	regs.prefetch020_valid[2] = regs.cacheholdingdata_valid;

#if MORE_ACCURATE_68020_PIPELINE
	if (idx >= 1) {
		pipeline_020(pc);
	}
#endif

	if  (!regs.prefetch020_valid[2]) {
		if (idx == 0 || !regs.pipeline_stop) {
			// Pipeline refill and first opcode word is invalid?
			// Generate previously detected bus error/MMU fault
			do_access_or_bus_error(pc, pc + idx * 2);
		}
	}
	return idx + 1;
}

void fill_prefetch_030_ntx(void)
{
	uaecptr pc = m68k_getpc ();
	uaecptr pc2 = pc;
	int idx = 0;

	pc &= ~3;
	mmu030_idx = 0;
	reset_pipeline_state();
	regs.cacheholdingdata_valid = 1;
	regs.cacheholdingaddr020 = 0xffffffff;
	regs.prefetch020_valid[0] = regs.prefetch020_valid[1] = regs.prefetch020_valid[2] = 0;

	fill_icache030(pc);
	if (pc2 & 2) {
		idx = add_prefetch_030(idx, regs.cacheholdingdata020, pc2);
	} else {
		idx = add_prefetch_030(idx, regs.cacheholdingdata020 >>	16, pc2);
		idx = add_prefetch_030(idx, regs.cacheholdingdata020, pc2);
	}

	fill_icache030(pc + 4);
	if (pc2 & 2) {
		idx = add_prefetch_030(idx, regs.cacheholdingdata020 >>	16, pc2);
		idx = add_prefetch_030(idx, regs.cacheholdingdata020, pc2);
	} else {
		idx = add_prefetch_030(idx, regs.cacheholdingdata020 >>	16, pc2);
	}

	if (currprefs.cpu_cycle_exact)
		regs.irc = get_word_ce030_prefetch_opcode (0);
	else
		regs.irc = get_word_030_prefetch (0);
}

void fill_prefetch_030_ntx_continue (void)
{
	uaecptr pc = m68k_getpc ();
	uaecptr pc_orig = pc;
	int idx = 0;

	mmu030_idx = 0;
	reset_pipeline_state();
	regs.cacheholdingdata_valid = 1;
	regs.cacheholdingaddr020 = 0xffffffff;

	for (int i = 2; i >= 0; i--) {
		if (!regs.prefetch020_valid[i])
			break;
#if MORE_ACCURATE_68020_PIPELINE
		if (idx >= 1) {
			pipeline_020(pc);
		}
#endif
		pc += 2;
		idx++;
	}

	if (idx < 3 && !regs.pipeline_stop) {
		uaecptr pc2 = pc;
		pc &= ~3;

		fill_icache030(pc);
		if (pc2 & 2) {
			idx = add_prefetch_030(idx, regs.cacheholdingdata020, pc_orig);
		} else {
			idx = add_prefetch_030(idx, regs.cacheholdingdata020 >> 16, pc_orig);
			if (idx < 3)
				idx = add_prefetch_030(idx, regs.cacheholdingdata020, pc_orig);
		}

		if (idx < 3) {
			fill_icache030(pc + 4);
			if (pc2 & 2) {
				idx = add_prefetch_030(idx, regs.cacheholdingdata020 >>	16, pc_orig);
				if (idx < 3)
					idx = add_prefetch_030(idx, regs.cacheholdingdata020, pc_orig);
			} else {
				idx = add_prefetch_030(idx, regs.cacheholdingdata020 >>	16, pc_orig);
			}
		}
	}

	if (currprefs.cpu_cycle_exact)
		regs.irc = get_word_ce030_prefetch_opcode (0);
	else
		regs.irc = get_word_030_prefetch (0);
}

void fill_prefetch_020_ntx(void)
{
	uaecptr pc = m68k_getpc ();
	uaecptr pc2 = pc;
	int idx = 0;

	pc &= ~3;
	reset_pipeline_state();

	fill_icache020 (pc, true);
	if (pc2 & 2) {
		idx = add_prefetch_030(idx, regs.cacheholdingdata020, pc);
	} else {
		idx = add_prefetch_030(idx, regs.cacheholdingdata020 >>	16, pc);
		idx = add_prefetch_030(idx, regs.cacheholdingdata020, pc);
	}

	fill_icache020 (pc + 4, true);
	if (pc2 & 2) {
		idx = add_prefetch_030(idx, regs.cacheholdingdata020 >>	16, pc);
		idx = add_prefetch_030(idx, regs.cacheholdingdata020, pc);
	} else {
		idx = add_prefetch_030(idx, regs.cacheholdingdata020 >>	16, pc);
	}

	if (currprefs.cpu_cycle_exact)
		regs.irc = get_word_ce020_prefetch_opcode (0);
	else
		regs.irc = get_word_020_prefetch (0);
}

// Not exactly right, requires logic analyzer checks.
void continue_ce020_prefetch(void)
{
	fill_prefetch_020_ntx();
}
void continue_020_prefetch(void)
{
	fill_prefetch_020_ntx();
}

void continue_ce030_prefetch(void)
{
	fill_prefetch_030_ntx();
}
void continue_030_prefetch(void)
{
	fill_prefetch_030_ntx();
}

void fill_prefetch_020(void)
{
	fill_prefetch_020_ntx();
	check_t0_trace();
}

void fill_prefetch_030(void)
{
	fill_prefetch_030_ntx();
	check_t0_trace();
}


void fill_prefetch (void)
{
	reset_pipeline_state();
	if (currprefs.cachesize)
		return;
	if (!currprefs.cpu_compatible)
		return;
	if (currprefs.cpu_model >= 68040) {
		if (currprefs.cpu_compatible || currprefs.cpu_memory_cycle_exact) {
			fill_icache040(m68k_getpc() + 16);
			fill_icache040(m68k_getpc());
		}
	} else if (currprefs.cpu_model == 68020) {
		fill_prefetch_020 ();
	} else if (currprefs.cpu_model == 68030) {
		fill_prefetch_030 ();
	} else if (currprefs.cpu_model <= 68010) {
		uaecptr pc = m68k_getpc ();
		regs.ir = x_get_word (pc);
		regs.irc = x_get_word (pc + 2);
	}
}

extern bool cpuboard_fc_check(uaecptr addr, uae_u32 *v, int size, bool write);

uae_u32 sfc_nommu_get_byte(uaecptr addr)
{
	uae_u32 v;
	if (!cpuboard_fc_check(addr, &v, 0, false))
		v = x_get_byte(addr);
	return v;
}
uae_u32 sfc_nommu_get_word(uaecptr addr)
{
	uae_u32 v;
	if (!cpuboard_fc_check(addr, &v, 1, false))
		v = x_get_word(addr);
	return v;
}
uae_u32 sfc_nommu_get_long(uaecptr addr)
{
	uae_u32 v;
	if (!cpuboard_fc_check(addr, &v, 2, false))
		v = x_get_long(addr);
	return v;
}
void dfc_nommu_put_byte(uaecptr addr, uae_u32 v)
{
	if (!cpuboard_fc_check(addr, &v, 0, true))
		x_put_byte(addr, v);
}
void dfc_nommu_put_word(uaecptr addr, uae_u32 v)
{
	if (!cpuboard_fc_check(addr, &v, 1, true))
		x_put_word(addr, v);
}
void dfc_nommu_put_long(uaecptr addr, uae_u32 v)
{
	if (!cpuboard_fc_check(addr, &v, 2, true))
		x_put_long(addr, v);
}
