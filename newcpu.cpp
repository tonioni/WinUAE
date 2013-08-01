/*
* UAE - The Un*x Amiga Emulator
*
* MC68000 emulation
*
* (c) 1995 Bernd Schmidt
*/

#define MOVEC_DEBUG 0
#define MMUOP_DEBUG 2
#define DEBUG_CD32CDTVIO 0
#define EXCEPTION3_DEBUGGER 0
#define CPUTRACE_DEBUG 0

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
#include "gui.h"
#include "savestate.h"
#include "blitter.h"
#include "ar.h"
#include "gayle.h"
#include "cia.h"
#include "inputrecord.h"
#include "inputdevice.h"
#ifdef JIT
#include "jit/compemu.h"
#include <signal.h>
#else
/* Need to have these somewhere */
static void build_comp (void) {}
bool check_prefs_changed_comp (void) { return false; }
#endif
/* For faster JIT cycles handling */
signed long pissoff = 0;

/* Opcode of faulting instruction */
static uae_u16 last_op_for_exception_3;
/* PC at fault time */
static uaecptr last_addr_for_exception_3;
/* Address that generated the exception */
static uaecptr last_fault_for_exception_3;
/* read (0) or write (1) access */
static int last_writeaccess_for_exception_3;
/* instruction (1) or data (0) access */
static int last_instructionaccess_for_exception_3;
int mmu_enabled, mmu_triggered;
int cpu_cycles;
static int baseclock;

int cpucycleunit;
int cpu_tracer;

const int areg_byteinc[] = { 1, 1, 1, 1, 1, 1, 1, 2 };
const int imm8_table[] = { 8, 1, 2, 3, 4, 5, 6, 7 };

int movem_index1[256];
int movem_index2[256];
int movem_next[256];

cpuop_func *cpufunctbl[65536];

struct mmufixup mmufixup[2];

extern uae_u32 get_fpsr (void);

#define COUNT_INSTRS 0
#define MC68060_PCR   0x04300000
#define MC68EC060_PCR 0x04310000

static uae_u64 fake_srp_030, fake_crp_030;
static uae_u32 fake_tt0_030, fake_tt1_030, fake_tc_030;
static uae_u16 fake_mmusr_030;

static struct cache020 caches020[CACHELINES020];
static struct cache030 icaches030[CACHELINES030];
static struct cache030 dcaches030[CACHELINES030];
static struct cache040 caches040[CACHESETS040];

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
uae_u32 (*x_prefetch_long)(int);
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

static struct cputracestruct cputrace;

#if CPUTRACE_DEBUG
static void validate_trace (void)
{
	for (int i = 0; i < cputrace.memoryoffset; i++) {
		struct cputracememory *ctm = &cputrace.ctm[i];
		if (ctm->data == 0xdeadf00d) {
			write_log (L"unfinished write operation %d %08x\n", i, ctm->addr);
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
	struct cputracememory *ctm = &cputrace.ctm[cputrace.memoryoffset++];
	ctm->mode = 0;
	cputrace.cyclecounter = 0;
	cputrace.cyclecounter_pre = cputrace.cyclecounter_post = 0;
}
static void set_trace (uaecptr addr, int accessmode, int size)
{
#if CPUTRACE_DEBUG
	validate_trace ();
#endif
	struct cputracememory *ctm = &cputrace.ctm[cputrace.memoryoffset++];
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
		write_log (L"add_trace memoryoffset=%d!\n", cputrace.memoryoffset);
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
			write_log (_T("CPU trace: dma_cycle() enabled. %08x %08x NOW=%08X\n"),
				cputrace.cyclecounter_pre, cputrace.cyclecounter_post, get_cycles ());
			cpu_tracer = -2; // dma_cycle() allowed to work now
		}
	}
	if (cputrace.readcounter || cputrace.writecounter ||
		cputrace.cyclecounter || cputrace.cyclecounter_pre || cputrace.cyclecounter_post)
		return false;
	x_prefetch = x2_prefetch;
	x_prefetch_long = x2_prefetch_long;
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
	write_log (_T("CPU tracer playback complete. STARTCYCLES=%08x NOWCYCLES=%08x\n"), cputrace.startcycles, get_cycles ());
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
			write_log (_T("CPU trace: GET %d: PC=%08x %08x=%08x %d %d %08x/%08x/%08x %d/%d (%08X)\n"),
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
static uae_u32 cputracefunc_x_prefetch_long (int o)
{
	uae_u32 pc = m68k_getpc ();
	set_trace (pc + o, 2, 4);
	uae_u32 v = x2_prefetch_long (o);
	add_trace (pc + o, v, 2, 4);
	return v;
}
static uae_u32 cputracefunc2_x_prefetch_long (int o)
{
	uae_u32 v;
	if (get_trace (m68k_getpc () + o, 2, 4, &v)) {
		v = x2_prefetch_long (o);
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
		write_log (L"cputracefunc_x_do_cycles_post memoryoffset=%d!\n", cputrace.memoryoffset);
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

// indirect memory access functions
static void set_x_funcs (void)
{
	if (currprefs.mmu_model) {
		if (currprefs.cpu_model == 68060) {
			x_prefetch = get_iword_mmu060;
			x_prefetch_long = get_ilong_mmu060;
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
			x_prefetch_long = get_ilong_mmu040;
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
			x_prefetch = get_iword_mmu030;
			x_prefetch_long = get_ilong_mmu030;
			x_get_ilong = get_ilong_mmu030;
			x_get_iword = get_iword_mmu030;
			x_get_ibyte = get_ibyte_mmu030;
			x_next_iword = next_iword_mmu030;
			x_next_ilong = next_ilong_mmu030;
			x_put_long = put_long_mmu030;
			x_put_word = put_word_mmu030;
			x_put_byte = put_byte_mmu030;
			x_get_long = get_long_mmu030;
			x_get_word = get_word_mmu030;
			x_get_byte = get_byte_mmu030;
		}
		x_do_cycles = do_cycles;
		x_do_cycles_pre = do_cycles;
		x_do_cycles_post = do_cycles_post;
	} else if (currprefs.cpu_model < 68020) {
		if (currprefs.cpu_cycle_exact) {
			x_prefetch = get_word_ce000_prefetch;
			x_prefetch_long = NULL;
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
		} else if (currprefs.cpu_compatible) {
			x_prefetch = get_word_prefetch;
			x_prefetch_long = get_long_prefetch;
			x_get_ilong = NULL;
			x_get_iword = get_iword;
			x_get_ibyte = get_ibyte;
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
			x_prefetch_long = NULL;
			x_get_ilong = get_ilong;
			x_get_iword = get_iword;
			x_get_ibyte = get_ibyte;
			x_next_iword = next_iword;
			x_next_ilong = next_ilong;
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
		if (currprefs.cpu_compatible) {
			if (currprefs.cpu_model == 68020) {
				x_prefetch = get_word_prefetch;
				x_prefetch_long = get_long_prefetch;
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
			} else {
				// 68030+ does not have real prefetch only emulation
				x_prefetch = NULL;
				x_prefetch_long = NULL;
				x_get_ilong = get_ilong;
				x_get_iword = get_iword;
				x_get_ibyte = get_ibyte;
				x_next_iword = next_iword;
				x_next_ilong = next_ilong;
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
		} else {
			x_prefetch = NULL;
			x_prefetch_long = NULL;
			x_get_ilong = get_ilong;
			x_get_iword = get_iword;
			x_get_ibyte = get_ibyte;
			x_next_iword = next_iword;
			x_next_ilong = next_ilong;
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
		// 68020+ cycle exact
	} else if (currprefs.cpu_model == 68020) {
		x_prefetch = get_word_ce020_prefetch;
		x_prefetch_long = NULL;
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
	} else {
		x_prefetch = get_word_ce030_prefetch;
		x_prefetch_long = NULL;
		x_get_ilong = get_long_ce030_prefetch;
		x_get_iword = get_word_ce030_prefetch;
		x_get_ibyte = NULL;
		x_next_iword = next_iword_030ce;
		x_next_ilong = next_ilong_030ce;
		x_put_long = put_long_ce030;
		x_put_word = put_word_ce030;
		x_put_byte = put_byte_ce030;
		x_get_long = get_long_ce030;
		x_get_word = get_word_ce030;
		x_get_byte = get_byte_ce030;
		x_do_cycles = do_cycles_ce020;
		x_do_cycles_pre = do_cycles_ce020;
		x_do_cycles_post = do_cycles_ce020_post;
	}
	x2_prefetch = x_prefetch;
	x2_prefetch_long = x_prefetch_long;
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
		x_prefetch_long = cputracefunc_x_prefetch_long;
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
			x_prefetch_long = cputracefunc2_x_prefetch_long;
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

bool can_cpu_tracer (void)
{
	return (currprefs.cpu_model == 68000 || currprefs.cpu_model == 68020) && currprefs.cpu_cycle_exact;
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

static void set_cpu_caches (void)
{
	int i;

	regs.prefetch020addr = 0xffffffff;
	regs.cacheholdingaddr020 = 0xffffffff;

#ifdef JIT
	if (currprefs.cachesize) {
		if (currprefs.cpu_model < 68040) {
			set_cache_state (regs.cacr & 1);
			if (regs.cacr & 0x08) {
				flush_icache (0, 3);
			}
		} else {
			set_cache_state ((regs.cacr & 0x8000) ? 1 : 0);
		}
	}
#endif
	if (currprefs.cpu_model == 68020) {
		if (regs.cacr & 0x08) { // clear instr cache
			for (i = 0; i < CACHELINES020; i++)
				caches020[i].valid = 0;
		}
		if (regs.cacr & 0x04) { // clear entry in instr cache
			caches020[(regs.caar >> 2) & (CACHELINES020 - 1)].valid = 0;
			regs.cacr &= ~0x04;
		}
	} else if (currprefs.cpu_model == 68030) {
		//regs.cacr |= 0x100;
		if (regs.cacr & 0x08) { // clear instr cache
			for (i = 0; i < CACHELINES030; i++) {
				icaches030[i].valid[0] = 0;
				icaches030[i].valid[1] = 0;
				icaches030[i].valid[2] = 0;
				icaches030[i].valid[3] = 0;
			}
		}
		if (regs.cacr & 0x04) { // clear entry in instr cache
			icaches030[(regs.caar >> 4) & (CACHELINES030 - 1)].valid[(regs.caar >> 2) & 3] = 0;
			regs.cacr &= ~0x04;
		}
		if (regs.cacr & 0x800) { // clear data cache
			for (i = 0; i < CACHELINES030; i++) {
				dcaches030[i].valid[0] = 0;
				dcaches030[i].valid[1] = 0;
				dcaches030[i].valid[2] = 0;
				dcaches030[i].valid[3] = 0;
			}
			regs.cacr &= ~0x800;
		}
		if (regs.cacr & 0x400) { // clear entry in data cache
			dcaches030[(regs.caar >> 4) & (CACHELINES030 - 1)].valid[(regs.caar >> 2) & 3] = 0;
			regs.cacr &= ~0x400;
		}
	} else if (currprefs.cpu_model == 68040) {
		if (!(regs.cacr & 0x8000)) {
			for (i = 0; i < CACHESETS040; i++) {
				caches040[i].valid[0] = 0;
				caches040[i].valid[1] = 0;
				caches040[i].valid[2] = 0;
				caches040[i].valid[3] = 0;
			}
		}
	}
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

static void build_cpufunctbl (void)
{
	int i, opcnt;
	unsigned long opcode;
	const struct cputbl *tbl = 0;
	int lvl;

	switch (currprefs.cpu_model)
	{
#ifdef CPUEMU_0
#ifndef CPUEMU_68000_ONLY
	case 68060:
		lvl = 5;
		tbl = op_smalltbl_0_ff;
		if (currprefs.cpu_cycle_exact)
			tbl = op_smalltbl_22_ff;
		if (currprefs.mmu_model)
			tbl = op_smalltbl_33_ff;
		break;
	case 68040:
		lvl = 4;
		tbl = op_smalltbl_1_ff;
		if (currprefs.cpu_cycle_exact)
			tbl = op_smalltbl_23_ff;
		if (currprefs.mmu_model)
			tbl = op_smalltbl_31_ff;
		break;
	case 68030:
		lvl = 3;
		tbl = op_smalltbl_2_ff;
		if (currprefs.cpu_cycle_exact)
			tbl = op_smalltbl_24_ff;
		if (currprefs.mmu_model)
			tbl = op_smalltbl_32_ff;
		break;
	case 68020:
		lvl = 2;
		tbl = op_smalltbl_3_ff;
#ifdef CPUEMU_20
		if (currprefs.cpu_compatible)
			tbl = op_smalltbl_20_ff;
#endif
#ifdef CPUEMU_21
		if (currprefs.cpu_cycle_exact)
			tbl = op_smalltbl_21_ff;
#endif
		break;
	case 68010:
		lvl = 1;
		tbl = op_smalltbl_4_ff;
		break;
#endif
#endif
	default:
		changed_prefs.cpu_model = currprefs.cpu_model = 68000;
	case 68000:
		lvl = 0;
		tbl = op_smalltbl_5_ff;
#ifdef CPUEMU_11
		if (currprefs.cpu_compatible)
			tbl = op_smalltbl_11_ff; /* prefetch */
#endif
#ifdef CPUEMU_12
		if (currprefs.cpu_cycle_exact)
			tbl = op_smalltbl_12_ff; /* prefetch and cycle-exact */
#endif
		break;
	}

	if (tbl == 0) {
		write_log (_T("no CPU emulation cores available CPU=%d!"), currprefs.cpu_model);
		abort ();
	}

	for (opcode = 0; opcode < 65536; opcode++)
		cpufunctbl[opcode] = op_illg_1;
	for (i = 0; tbl[i].handler != NULL; i++) {
		opcode = tbl[i].opcode;
		cpufunctbl[opcode] = tbl[i].handler;
	}

	/* hack fpu to 68000/68010 mode */
	if (currprefs.fpu_model && currprefs.cpu_model < 68020) {
		tbl = op_smalltbl_3_ff;
		for (i = 0; tbl[i].handler != NULL; i++) {
			if ((tbl[i].opcode & 0xfe00) == 0xf200)
				cpufunctbl[tbl[i].opcode] = tbl[i].handler;
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
			if (currprefs.int_no_unimplemented && currprefs.cpu_model == 68060) {
				cpufunctbl[opcode] = op_unimpl_1;
			} else {
				cpufunctbl[opcode] = op_illg_1;
			}
			continue;
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
			opcnt++;
		}
	}
	write_log (_T("Building CPU, %d opcodes (%d %d %d)\n"),
		opcnt, lvl,
		currprefs.cpu_cycle_exact ? -1 : currprefs.cpu_compatible ? 1 : 0, currprefs.address_space_24);
	write_log (_T("CPU=%d, FPU=%d, MMU=%d, JIT%s=%d.\n"),
		currprefs.cpu_model, currprefs.fpu_model,
		currprefs.mmu_model,
		currprefs.cachesize ? (currprefs.compfpu ? _T("=CPU/FPU") : _T("=CPU")) : _T(""),
		currprefs.cachesize);
#ifdef JIT
	build_comp ();
#endif
	set_cpu_caches ();
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
	if (currprefs.m68k_speed == 0 && currprefs.cpu_model >= 68020) {
		if (!cycles_mult)
			cycles_mult = CYCLES_DIV / 4;
		else
			cycles_mult /= 4;
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
	} else if (currprefs.cpu_frequency) {
		cpucycleunit = CYCLE_UNIT * baseclock / currprefs.cpu_frequency;
	} else if (currprefs.cpu_cycle_exact && currprefs.cpu_clock_multiplier == 0) {
		if (currprefs.cpu_model >= 68030) {
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
	currprefs.cpu_model = changed_prefs.cpu_model;
	currprefs.fpu_model = changed_prefs.fpu_model;
	currprefs.mmu_model = changed_prefs.mmu_model;
	currprefs.cpu_compatible = changed_prefs.cpu_compatible;
	currprefs.cpu_cycle_exact = changed_prefs.cpu_cycle_exact;
	currprefs.int_no_unimplemented = changed_prefs.int_no_unimplemented;
	currprefs.fpu_no_unimplemented = changed_prefs.fpu_no_unimplemented;
	currprefs.blitter_cycle_exact = changed_prefs.blitter_cycle_exact;
}

void check_prefs_changed_cpu (void)
{
	bool changed = false;

	if (!config_changed)
		return;
#ifdef JIT
	changed = check_prefs_changed_comp ();
#endif
	if (changed
		|| currprefs.cpu_model != changed_prefs.cpu_model
		|| currprefs.fpu_model != changed_prefs.fpu_model
		|| currprefs.mmu_model != changed_prefs.mmu_model
		|| currprefs.int_no_unimplemented != changed_prefs.int_no_unimplemented
		|| currprefs.fpu_no_unimplemented != changed_prefs.fpu_no_unimplemented
		|| currprefs.cpu_compatible != changed_prefs.cpu_compatible
		|| currprefs.cpu_cycle_exact != changed_prefs.cpu_cycle_exact) {

			prefs_changed_cpu ();
			if (!currprefs.cpu_compatible && changed_prefs.cpu_compatible)
				fill_prefetch ();
			build_cpufunctbl ();
			changed = true;
	}
	if (changed
		|| currprefs.m68k_speed != changed_prefs.m68k_speed
		|| currprefs.m68k_speed_throttle != changed_prefs.m68k_speed_throttle
		|| currprefs.cpu_clock_multiplier != changed_prefs.cpu_clock_multiplier
		|| currprefs.cpu_frequency != changed_prefs.cpu_frequency) {
			currprefs.m68k_speed = changed_prefs.m68k_speed;
			currprefs.m68k_speed_throttle = changed_prefs.m68k_speed_throttle;
			update_68k_cycles ();
			changed = true;
	}

	if (currprefs.cpu_idle != changed_prefs.cpu_idle) {
		currprefs.cpu_idle = changed_prefs.cpu_idle;
	}
	if (changed) {
		set_special (SPCFLAG_BRK);
		reset_frame_rate_hack ();
	}

}

void init_m68k (void)
{
	int i;

	prefs_changed_cpu ();
	update_68k_cycles ();

	for (i = 0 ; i < 256 ; i++) {
		int j;
		for (j = 0 ; j < 8 ; j++) {
			if (i & (1 << j)) break;
		}
		movem_index1[i] = j;
		movem_index2[i] = 7-j;
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
			while (fscanf (f, "%lx: %lu %s\n", &opcode, &count, name) == 3) {
				instrcount[opcode] = count;
			}
			fclose (f);
		}
	}
#endif
	write_log (_T("Building CPU table for configuration: %d"), currprefs.cpu_model);
	regs.address_space_mask = 0xffffffff;
	if (currprefs.cpu_compatible) {
		if (currprefs.address_space_24 && currprefs.cpu_model >= 68030)
			currprefs.address_space_24 = false;
	}
	if (currprefs.fpu_model > 0)
		write_log (_T("/%d"), currprefs.fpu_model);
	if (currprefs.cpu_cycle_exact) {
		if (currprefs.cpu_model == 68000)
			write_log (_T(" prefetch and cycle-exact"));
		else
			write_log (_T(" ~cycle-exact"));
	} else if (currprefs.cpu_compatible) {
		if (currprefs.cpu_model <= 68020) {
			write_log (_T(" prefetch"));
		} else {
			write_log (_T(" fake prefetch"));
		}
	}
	if (currprefs.int_no_unimplemented && currprefs.cpu_model == 68060) {
		write_log (_T(" no unimplemented integer instructions"));
	}
	if (currprefs.fpu_no_unimplemented && currprefs.fpu_model) {
		write_log (_T(" no unimplemented floating point instructions"));
	}
	if (currprefs.address_space_24) {
		regs.address_space_mask = 0x00ffffff;
		write_log (_T(" 24-bit"));
	}
	write_log (_T("\n"));

	read_table68k ();
	do_merges ();

	write_log (_T("%d CPU functions\n"), nr_cpuop_funcs);

	build_cpufunctbl ();
	set_x_funcs ();

#ifdef JIT
	/* We need to check whether NATMEM settings have changed
	* before starting the CPU */
	check_prefs_changed_comp ();
#endif
}

struct regstruct regs, mmu_backup_regs;
struct flag_struct regflags;
static long int m68kpc_offset;

#define get_ibyte_1(o) get_byte (regs.pc + (regs.pc_p - regs.pc_oldp) + (o) + 1)
#define get_iword_1(o) get_word (regs.pc + (regs.pc_p - regs.pc_oldp) + (o))
#define get_ilong_1(o) get_long (regs.pc + (regs.pc_p - regs.pc_oldp) + (o))

static uaecptr ShowEA (void *f, uaecptr pc, uae_u16 opcode, int reg, amodes mode, wordsizes size, TCHAR *buf, uae_u32 *eaddr, int safemode)
{
	uae_u16 dp;
	uae_s8 disp8;
	uae_s16 disp16;
	int r;
	uae_u32 dispreg;
	uaecptr addr = pc;
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
		break;
	case Aipi:
		_stprintf (buffer, _T("(A%d)+"), reg);
		addr = regs.regs[reg + 8];
		break;
	case Apdi:
		_stprintf (buffer, _T("-(A%d)"), reg);
		addr = regs.regs[reg + 8];
		break;
	case Ad16:
		{
			TCHAR offtxt[80];
			disp16 = get_iword_debug (pc); pc += 2;
			if (disp16 < 0)
				_stprintf (offtxt, _T("-$%04x"), -disp16);
			else
				_stprintf (offtxt, _T("$%04x"), disp16);
			addr = m68k_areg (regs, reg) + disp16;
			_stprintf (buffer, _T("(A%d, %s) == $%08lx"), reg, offtxt, (unsigned long)addr);
		}
		break;
	case Ad8r:
		dp = get_iword_debug (pc); pc += 2;
		disp8 = dp & 0xFF;
		r = (dp & 0x7000) >> 12;
		dispreg = dp & 0x8000 ? m68k_areg (regs, r) : m68k_dreg (regs, r);
		if (!(dp & 0x800)) dispreg = (uae_s32)(uae_s16)(dispreg);
		dispreg <<= (dp >> 9) & 3;

		if (dp & 0x100) {
			uae_s32 outer = 0, disp = 0;
			uae_s32 base = m68k_areg (regs, reg);
			TCHAR name[10];
			_stprintf (name, _T("A%d, "), reg);
			if (dp & 0x80) { base = 0; name[0] = 0; }
			if (dp & 0x40) dispreg = 0;
			if ((dp & 0x30) == 0x20) { disp = (uae_s32)(uae_s16)get_iword_debug (pc); pc += 2; }
			if ((dp & 0x30) == 0x30) { disp = get_ilong_debug (pc); pc += 4; }
			base += disp;

			if ((dp & 0x3) == 0x2) { outer = (uae_s32)(uae_s16)get_iword_debug (pc); pc += 2; }
			if ((dp & 0x3) == 0x3) { outer = get_ilong_debug (pc); pc += 4; }

			if (!(dp & 4)) base += dispreg;
			if ((dp & 3) && !safemode) base = get_ilong_debug (base);
			if (dp & 4) base += dispreg;

			addr = base + outer;
			_stprintf (buffer, _T("(%s%c%d.%c*%d+%ld)+%ld == $%08lx"), name,
				dp & 0x8000 ? 'A' : 'D', (int)r, dp & 0x800 ? 'L' : 'W',
				1 << ((dp >> 9) & 3),
				disp, outer,
				(unsigned long)addr);
		} else {
			addr = m68k_areg (regs, reg) + (uae_s32)((uae_s8)disp8) + dispreg;
			_stprintf (buffer, _T("(A%d, %c%d.%c*%d, $%02x) == $%08lx"), reg,
				dp & 0x8000 ? 'A' : 'D', (int)r, dp & 0x800 ? 'L' : 'W',
				1 << ((dp >> 9) & 3), disp8,
				(unsigned long)addr);
		}
		break;
	case PC16:
		disp16 = get_iword_debug (pc); pc += 2;
		addr += (uae_s16)disp16;
		_stprintf (buffer, _T("(PC,$%04x) == $%08lx"), disp16 & 0xffff, (unsigned long)addr);
		break;
	case PC8r:
		dp = get_iword_debug (pc); pc += 2;
		disp8 = dp & 0xFF;
		r = (dp & 0x7000) >> 12;
		dispreg = dp & 0x8000 ? m68k_areg (regs, r) : m68k_dreg (regs, r);
		if (!(dp & 0x800)) dispreg = (uae_s32)(uae_s16)(dispreg);
		dispreg <<= (dp >> 9) & 3;

		if (dp & 0x100) {
			uae_s32 outer = 0, disp = 0;
			uae_s32 base = addr;
			TCHAR name[10];
			_stprintf (name, _T("PC, "));
			if (dp & 0x80) { base = 0; name[0] = 0; }
			if (dp & 0x40) dispreg = 0;
			if ((dp & 0x30) == 0x20) { disp = (uae_s32)(uae_s16)get_iword_debug (pc); pc += 2; }
			if ((dp & 0x30) == 0x30) { disp = get_ilong_debug (pc); pc += 4; }
			base += disp;

			if ((dp & 0x3) == 0x2) { outer = (uae_s32)(uae_s16)get_iword_debug (pc); pc += 2; }
			if ((dp & 0x3) == 0x3) { outer = get_ilong_debug (pc); pc += 4; }

			if (!(dp & 4)) base += dispreg;
			if ((dp & 3) && !safemode) base = get_ilong_debug (base);
			if (dp & 4) base += dispreg;

			addr = base + outer;
			_stprintf (buffer, _T("(%s%c%d.%c*%d+%ld)+%ld == $%08lx"), name,
				dp & 0x8000 ? 'A' : 'D', (int)r, dp & 0x800 ? 'L' : 'W',
				1 << ((dp >> 9) & 3),
				disp, outer,
				(unsigned long)addr);
		} else {
			addr += (uae_s32)((uae_s8)disp8) + dispreg;
			_stprintf (buffer, _T("(PC, %c%d.%c*%d, $%02x) == $%08lx"), dp & 0x8000 ? 'A' : 'D',
				(int)r, dp & 0x800 ? 'L' : 'W',  1 << ((dp >> 9) & 3),
				disp8, (unsigned long)addr);
		}
		break;
	case absw:
		addr = (uae_s32)(uae_s16)get_iword_debug (pc);
		_stprintf (buffer, _T("$%08lx"), (unsigned long)addr);
		pc += 2;
		break;
	case absl:
		addr = get_ilong_debug (pc);
		_stprintf (buffer, _T("$%08lx"), (unsigned long)addr);
		pc += 4;
		break;
	case imm:
		switch (size){
		case sz_byte:
			_stprintf (buffer, _T("#$%02x"), (unsigned int)(get_iword_debug (pc) & 0xff));
			pc += 2;
			break;
		case sz_word:
			_stprintf (buffer, _T("#$%04x"), (unsigned int)(get_iword_debug (pc) & 0xffff));
			pc += 2;
			break;
		case sz_long:
			_stprintf (buffer, _T("#$%08lx"), (unsigned long)(get_ilong_debug (pc)));
			pc += 4;
			break;
		default:
			break;
		}
		break;
	case imm0:
		offset = (uae_s32)(uae_s8)get_iword_debug (pc);
		_stprintf (buffer, _T("#$%02x"), (unsigned int)(offset & 0xff));
		addr = pc + 2 + offset;
		pc += 2;
		break;
	case imm1:
		offset = (uae_s32)(uae_s16)get_iword_debug (pc);
		buffer[0] = 0;
		_stprintf (buffer, _T("#$%04x"), (unsigned int)(offset & 0xffff));
		addr = pc + offset;
		pc += 2;
		break;
	case imm2:
		offset = (uae_s32)get_ilong_debug (pc);
		_stprintf (buffer, _T("#$%08lx"), (unsigned long)offset);
		addr = pc + offset;
		pc += 4;
		break;
	case immi:
		offset = (uae_s32)(uae_s8)(reg & 0xff);
		_stprintf (buffer, _T("#$%08lx"), (unsigned long)offset);
		addr = pc + offset;
		break;
	default:
		break;
	}
	if (buf == 0)
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

/*
* extract bitfield data from memory and return it in the MSBs
* bdata caches the unmodified data for put_bitfield()
*/
uae_u32 REGPARAM2 get_bitfield (uae_u32 src, uae_u32 bdata[2], uae_s32 offset, int width)
{
	uae_u32 tmp, res, mask;

	offset &= 7;
	mask = 0xffffffffu << (32 - width);
	switch ((offset + width + 7) >> 3) {
	case 1:
		tmp = get_byte (src);
		res = tmp << (24 + offset);
		bdata[0] = tmp & ~(mask >> (24 + offset));
		break;
	case 2:
		tmp = get_word (src);
		res = tmp << (16 + offset);
		bdata[0] = tmp & ~(mask >> (16 + offset));
		break;
	case 3:
		tmp = get_word (src);
		res = tmp << (16 + offset);
		bdata[0] = tmp & ~(mask >> (16 + offset));
		tmp = get_byte (src + 2);
		res |= tmp << (8 + offset);
		bdata[1] = tmp & ~(mask >> (8 + offset));
		break;
	case 4:
		tmp = get_long (src);
		res = tmp << offset;
		bdata[0] = tmp & ~(mask >> offset);
		break;
	case 5:
		tmp = get_long (src);
		res = tmp << offset;
		bdata[0] = tmp & ~(mask >> offset);
		tmp = get_byte (src + 4);
		res |= tmp >> (8 - offset);
		bdata[1] = tmp & ~(mask << (8 - offset));
		break;
	default:
		/* Panic? */
		write_log (_T("get_bitfield() can't happen %d\n"), (offset + width + 7) >> 3);
		res = 0;
		break;
	}
	return res;
}
/*
* write bitfield data (in the LSBs) back to memory, upper bits
* must be cleared already.
*/
void REGPARAM2 put_bitfield (uae_u32 dst, uae_u32 bdata[2], uae_u32 val, uae_s32 offset, int width)
{
	offset = (offset & 7) + width;
	switch ((offset + 7) >> 3) {
	case 1:
		put_byte (dst, bdata[0] | (val << (8 - offset)));
		break;
	case 2:
		put_word (dst, bdata[0] | (val << (16 - offset)));
		break;
	case 3:
		put_word (dst, bdata[0] | (val >> (offset - 16)));
		put_byte (dst + 2, bdata[1] | (val << (24 - offset)));
		break;
	case 4:
		put_long (dst, bdata[0] | (val << (32 - offset)));
		break;
	case 5:
		put_long (dst, bdata[0] | (val >> (offset - 32)));
		put_byte (dst + 4, bdata[1] | (val << (40 - offset)));
		break;
	default:
		write_log (_T("put_bitfield() can't happen %d\n"), (offset + 7) >> 3);
		break;
	}
}

uae_u32 REGPARAM2 x_get_bitfield (uae_u32 src, uae_u32 bdata[2], uae_s32 offset, int width)
{
	uae_u32 tmp1, tmp2, res, mask;

	offset &= 7;
	mask = 0xffffffffu << (32 - width);
	switch ((offset + width + 7) >> 3) {
	case 1:
		tmp1 = x_cp_get_byte (src);
		res = tmp1 << (24 + offset);
		bdata[0] = tmp1 & ~(mask >> (24 + offset));
		break;
	case 2:
		tmp1 = x_cp_get_word (src);
		res = tmp1 << (16 + offset);
		bdata[0] = tmp1 & ~(mask >> (16 + offset));
		break;
	case 3:
		tmp1 = x_cp_get_word (src);
		tmp2 = x_cp_get_byte (src + 2);
		res = tmp1 << (16 + offset);
		bdata[0] = tmp1 & ~(mask >> (16 + offset));
		res |= tmp2 << (8 + offset);
		bdata[1] = tmp2 & ~(mask >> (8 + offset));
		break;
	case 4:
		tmp1 = x_cp_get_long (src);
		res = tmp1 << offset;
		bdata[0] = tmp1 & ~(mask >> offset);
		break;
	case 5:
		tmp1 = x_cp_get_long (src);
		tmp2 = x_cp_get_byte (src + 4);
		res = tmp1 << offset;
		bdata[0] = tmp1 & ~(mask >> offset);
		res |= tmp2 >> (8 - offset);
		bdata[1] = tmp2 & ~(mask << (8 - offset));
		break;
	default:
		/* Panic? */
		write_log (_T("x_get_bitfield() can't happen %d\n"), (offset + width + 7) >> 3);
		res = 0;
		break;
	}
	return res;
}

void REGPARAM2 x_put_bitfield (uae_u32 dst, uae_u32 bdata[2], uae_u32 val, uae_s32 offset, int width)
{
	offset = (offset & 7) + width;
	switch ((offset + 7) >> 3) {
	case 1:
		x_cp_put_byte (dst, bdata[0] | (val << (8 - offset)));
		break;
	case 2:
		x_cp_put_word (dst, bdata[0] | (val << (16 - offset)));
		break;
	case 3:
		x_cp_put_word (dst, bdata[0] | (val >> (offset - 16)));
		x_cp_put_byte (dst + 2, bdata[1] | (val << (24 - offset)));
		break;
	case 4:
		x_cp_put_long (dst, bdata[0] | (val << (32 - offset)));
		break;
	case 5:
		x_cp_put_long (dst, bdata[0] | (val >> (offset - 32)));
		x_cp_put_byte (dst + 4, bdata[1] | (val << (40 - offset)));
		break;
	default:
		write_log (_T("x_put_bitfield() can't happen %d\n"), (offset + 7) >> 3);
		break;
	}
}

uae_u32 REGPARAM2 get_disp_ea_020 (uae_u32 base, int idx)
{
	uae_u16 dp = next_iword ();
	int reg = (dp >> 12) & 15;
	uae_s32 regd = regs.regs[reg];
	if ((dp & 0x800) == 0)
		regd = (uae_s32)(uae_s16)regd;
	regd <<= (dp >> 9) & 3;
	if (dp & 0x100) {
		uae_s32 outer = 0;
		if (dp & 0x80) base = 0;
		if (dp & 0x40) regd = 0;

		if ((dp & 0x30) == 0x20)
			base += (uae_s32)(uae_s16) next_iword ();
		if ((dp & 0x30) == 0x30)
			base += next_ilong ();

		if ((dp & 0x3) == 0x2)
			outer = (uae_s32)(uae_s16) next_iword ();
		if ((dp & 0x3) == 0x3)
			outer = next_ilong ();

		if ((dp & 0x4) == 0)
			base += regd;
		if (dp & 0x3)
			base = get_long (base);
		if (dp & 0x4)
			base += regd;

		return base + outer;
	} else {
		return base + (uae_s32)((uae_s8)dp) + regd;
	}
}

uae_u32 REGPARAM2 x_get_disp_ea_020 (uae_u32 base, int idx)
{
	uae_u16 dp = x_next_iword ();
	int reg = (dp >> 12) & 15;
	int cycles = 0;
	uae_u32 v;

	uae_s32 regd = regs.regs[reg];
	if ((dp & 0x800) == 0)
		regd = (uae_s32)(uae_s16)regd;
	regd <<= (dp >> 9) & 3;
	if (dp & 0x100) {
		uae_s32 outer = 0;
		if (dp & 0x80)
			base = 0;
		if (dp & 0x40)
			regd = 0;

		if ((dp & 0x30) == 0x20) {
			base += (uae_s32)(uae_s16) x_next_iword ();
			cycles++;
		}
		if ((dp & 0x30) == 0x30) {
			base += x_next_ilong ();
			cycles++;
		}

		if ((dp & 0x3) == 0x2) {
			outer = (uae_s32)(uae_s16) x_next_iword ();
			cycles++;
		}
		if ((dp & 0x3) == 0x3) {
			outer = x_next_ilong ();
			cycles++;
		}

		if ((dp & 0x4) == 0) {
			base += regd;
			cycles++;
		}
		if (dp & 0x3) {
			base = x_get_long (base);
			cycles++;
		}
		if (dp & 0x4) {
			base += regd;
			cycles++;
		}
		v = base + outer;
	} else {
		v = base + (uae_s32)((uae_s8)dp) + regd;
	}
	if (cycles && currprefs.cpu_cycle_exact)
		x_do_cycles (cycles * cpucycleunit);
	return v;
}


uae_u32 REGPARAM2 x_get_disp_ea_ce020 (uae_u32 base, int idx)
{
	uae_u16 dp = next_iword_020ce ();
	int reg = (dp >> 12) & 15;
	uae_u32 v;

	uae_s32 regd = regs.regs[reg];
	if ((dp & 0x800) == 0)
		regd = (uae_s32)(uae_s16)regd;
	regd <<= (dp >> 9) & 3;
	if (dp & 0x100) {
		uae_s32 outer = 0;
		if (dp & 0x80)
			base = 0;
		if (dp & 0x40)
			regd = 0;

		if ((dp & 0x30) == 0x20) {
			base += (uae_s32)(uae_s16) next_iword_020ce ();
		}
		if ((dp & 0x30) == 0x30) {
			base += next_ilong_020ce ();
		}

		if ((dp & 0x3) == 0x2) {
			outer = (uae_s32)(uae_s16) next_iword_020ce ();
		}
		if ((dp & 0x3) == 0x3) {
			outer = next_ilong_020ce ();
		}

		if ((dp & 0x4) == 0) {
			base += regd;
		}
		if (dp & 0x3) {
			base = x_get_long (base);
		}
		if (dp & 0x4) {
			base += regd;
		}
		v = base + outer;
	} else {
		v = base + (uae_s32)((uae_s8)dp) + regd;
	}
	return v;
}

STATIC_INLINE int in_rom (uaecptr pc)
{
	return (munge24 (pc) & 0xFFF80000) == 0xF80000;
}

STATIC_INLINE int in_rtarea (uaecptr pc)
{
	return (munge24 (pc) & 0xFFFF0000) == rtarea_base && uae_boot_rom;
}

void REGPARAM2 MakeSR (void)
{
	regs.sr = ((regs.t1 << 15) | (regs.t0 << 14)
		| (regs.s << 13) | (regs.m << 12) | (regs.intmask << 8)
		| (GET_XFLG () << 4) | (GET_NFLG () << 3)
		| (GET_ZFLG () << 2) | (GET_VFLG () << 1)
		|  GET_CFLG ());
}

void SetSR (uae_u16 sr)
{
	regs.sr &= 0xff00;
	regs.sr |= sr;

	SET_XFLG ((regs.sr >> 4) & 1);
	SET_NFLG ((regs.sr >> 3) & 1);
	SET_ZFLG ((regs.sr >> 2) & 1);
	SET_VFLG ((regs.sr >> 1) & 1);
	SET_CFLG (regs.sr & 1);
}

void REGPARAM2 MakeFromSR (void)
{
	int oldm = regs.m;
	int olds = regs.s;

	if (currprefs.cpu_cycle_exact && currprefs.cpu_model >= 68020) {
		x_do_cycles (6 * CYCLE_UNIT);
		regs.ce020memcycles = 0;
	}

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

	doint ();
	if (regs.t1 || regs.t0)
		set_special (SPCFLAG_TRACE);
	else
		/* Keep SPCFLAG_DOTRACE, we still want a trace exception for
		SR-modifying instructions (including STOP).  */
		unset_special (SPCFLAG_TRACE);
}

static void exception_trace (int nr)
{
	unset_special (SPCFLAG_TRACE | SPCFLAG_DOTRACE);
	if (regs.t1 && !regs.t0) {
		/* trace stays pending if exception is div by zero, chk,
		* trapv or trap #x
		*/
		if (nr == 5 || nr == 6 || nr == 7 || (nr >= 32 && nr <= 47))
			set_special (SPCFLAG_DOTRACE);
	}
	regs.t1 = regs.t0 = regs.m = 0;
}

static void exception_debug (int nr)
{
#ifdef DEBUGGER
	if (!exception_debugging)
		return;
	console_out_f (_T("Exception %d, PC=%08X\n"), nr, M68K_GETPC);
#endif
}

#ifdef CPUEMU_12

/* cycle-exact exception handler, 68000 only */

/*

Address/Bus Error:

- 6 idle cycles
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

- 6 idle cycles
- write PC low word
- write SR
- write PC high word
- read exception address high word
- read exception address low word
- prefetch
- 2 idle cycles
- prefetch

Traps:

- 2 idle cycles
- write PC low word
- write SR
- write PC high word
- read exception address high word
- read exception address low word
- prefetch
- 2 idle cycles
- prefetch

TrapV:

- write PC low word
- write SR
- write PC high word
- read exception address high word
- read exception address low word
- prefetch
- 2 idle cycles
- prefetch

CHK:

- 6 idle cycles
- write PC low word
- write SR
- write PC high word
- read exception address high word
- read exception address low word
- prefetch
- 2 idle cycles
- prefetch

Illegal Instruction:

- 2 idle cycles
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

static void Exception_ce000 (int nr)
{
	uae_u32 currpc = m68k_getpc (), newpc;
	int sv = regs.s;
	int start, interrupt;

	start = 6;
	if (nr == 7) // TRAPV
		start = 0;
	else if (nr >= 32 && nr < 32 + 16) // TRAP #x
		start = 2;
	else if (nr == 4 || nr == 8) // ILLG & PRIVIL VIOL
		start = 2;
	interrupt = nr >= 24 && nr < 24 + 8;

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
		uae_u16 mode = (sv ? 4 : 0) | (last_instructionaccess_for_exception_3 ? 2 : 1);
		mode |= last_writeaccess_for_exception_3 ? 0 : 16;
		m68k_areg (regs, 7) -= 14;
		/* fixme: bit3=I/N */
		x_put_word (m68k_areg (regs, 7) + 12, last_addr_for_exception_3);
		x_put_word (m68k_areg (regs, 7) + 8, regs.sr);
		x_put_word (m68k_areg (regs, 7) + 10, last_addr_for_exception_3 >> 16);
		x_put_word (m68k_areg (regs, 7) + 6, last_op_for_exception_3);
		x_put_word (m68k_areg (regs, 7) + 4, last_fault_for_exception_3);
		x_put_word (m68k_areg (regs, 7) + 0, mode);
		x_put_word (m68k_areg (regs, 7) + 2, last_fault_for_exception_3 >> 16);
		x_do_cycles (2 * cpucycleunit);
		write_log (_T("Exception %d (%x) at %x -> %x!\n"), nr, last_addr_for_exception_3, currpc, get_long (4 * nr));
		goto kludge_me_do;
	}
	m68k_areg (regs, 7) -= 6;
	x_put_word (m68k_areg (regs, 7) + 4, currpc); // write low address
	if (interrupt) {
		// fetch interrupt vector number
		nr = x_get_byte (0x00fffff1 | ((nr - 24) << 1));
		x_do_cycles (4 * cpucycleunit);
	}
	x_put_word (m68k_areg (regs, 7) + 0, regs.sr); // write SR
	x_put_word (m68k_areg (regs, 7) + 2, currpc >> 16); // write high address
kludge_me_do:
	newpc = x_get_word (4 * nr) << 16; // read high address
	newpc |= x_get_word (4 * nr + 2); // read low address
	if (newpc & 1) {
		if (nr == 2 || nr == 3)
			cpu_halt (2);
		else
			exception3 (regs.ir, newpc);
		return;
	}
	m68k_setpc (newpc);
	regs.ir = x_get_word (m68k_getpc ()); // prefetch 1
	x_do_cycles (2 * cpucycleunit);
	regs.irc = x_get_word (m68k_getpc () + 2); // prefetch 2
#ifdef JIT
	set_special (SPCFLAG_END_COMPILE);
#endif
	exception_trace (nr);
}
#endif

static uae_u32 exception_pc (int nr)
{
	// zero divide, chk, trapcc/trapv, trace, trap#
	if (nr == 5 || nr == 6 || nr == 7 || nr == 9 || (nr >= 32 && nr <= 47))
		return m68k_getpc ();
	return regs.instruction_pc;
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
        case 0x7: // access error stack frame (68040)
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
			x_put_word (m68k_areg (regs, 7), ssw);
            m68k_areg (regs, 7) -= 4;
            x_put_long (m68k_areg (regs, 7), regs.mmu_effective_addr);
            break;
        case 0x9: // coprocessor mid-instruction stack frame (68020, 68030)
            m68k_areg (regs, 7) -= 4;
            x_put_long (m68k_areg (regs, 7), 0);
            m68k_areg (regs, 7) -= 4;
            x_put_long (m68k_areg (regs, 7), 0);
            m68k_areg (regs, 7) -= 4;
            x_put_long (m68k_areg (regs, 7), oldpc);
            break;
        case 0x3: // floating point post-instruction stack frame (68040)
        case 0x8: // bus and address error stack frame (68010)
            write_log(_T("Exception stack frame format %X not implemented\n"), format);
            return;
        case 0x4: // floating point unimplemented stack frame (68LC040, 68EC040)
				// or 68060 bus access fault stack frame
			if (currprefs.cpu_model == 68040) {
				// this is actually created in fpp.c
				write_log(_T("Exception stack frame format %X not implemented\n"), format);
				return;
			}
			// 68060 bus access fault
			m68k_areg (regs, 7) -= 4;
			x_put_long (m68k_areg (regs, 7), regs.mmu_fslw);
			m68k_areg (regs, 7) -= 4;
			x_put_long (m68k_areg (regs, 7), regs.mmu_fault_addr);
			break;
		case 0xB: // long bus cycle fault stack frame (68020, 68030)
			// We always use B frame because it is easier to emulate,
			// our PC always points at start of instruction but A frame assumes
			// it is + 2 and handling this properly is not easy.
			// Store state information to internal register space
			for (i = 0; i < mmu030_idx + 1; i++) {
				m68k_areg (regs, 7) -= 4;
				x_put_long (m68k_areg (regs, 7), mmu030_ad[i].val);
			}
			while (i < 9) {
				m68k_areg (regs, 7) -= 4;
				x_put_long (m68k_areg (regs, 7), 0);
				i++;
			}
			 // version & internal information (We store index here)
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), mmu030_idx);
			// 3* internal registers
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), mmu030_state[2]);
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), mmu030_state[1]);
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), mmu030_state[0]);
			// data input buffer = fault address
			m68k_areg (regs, 7) -= 4;
			x_put_long (m68k_areg (regs, 7), regs.mmu_fault_addr);
			// 2xinternal
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), 0);
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), 0);
			// stage b address
			m68k_areg (regs, 7) -= 4;
			x_put_long (m68k_areg (regs, 7), mm030_stageb_address);
			// 2xinternal
			m68k_areg (regs, 7) -= 4;
			x_put_long (m68k_areg (regs, 7), mmu030_disp_store[1]);
		/* fall through */
		case 0xA: // short bus cycle fault stack frame (68020, 68030)
			m68k_areg (regs, 7) -= 4;
			x_put_long (m68k_areg (regs, 7), mmu030_disp_store[0]);
			m68k_areg (regs, 7) -= 4;
			 // Data output buffer = value that was going to be written
			x_put_long (m68k_areg (regs, 7), (mmu030_state[1] & MMU030_STATEFLAG1_MOVEM1) ? mmu030_data_buffer : mmu030_ad[mmu030_idx].val);
			m68k_areg (regs, 7) -= 4;
			x_put_long (m68k_areg (regs, 7), mmu030_opcode);  // Internal register (opcode storage)
			m68k_areg (regs, 7) -= 4;
			x_put_long (m68k_areg (regs, 7), regs.mmu_fault_addr); // data cycle fault address
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), 0);  // Instr. pipe stage B
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), 0);  // Instr. pipe stage C
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), ssw);
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), 0);  // Internal register
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


// 68030 MMU
static void Exception_mmu030 (int nr, uaecptr oldpc)
{
    uae_u32 currpc = m68k_getpc (), newpc;
    int sv = regs.s;
    
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

#if 0
	write_log (_T("Exception %d -> %08x\n", nr, newpc));
#endif


    newpc = x_get_long (regs.vbr + 4 * nr);

	if (regs.m && nr >= 24 && nr < 32) { /* M + Interrupt */
        Exception_build_stack_frame (oldpc, currpc, regs.mmu_ssw, nr, 0x1);
    } else if (nr ==5 || nr == 6 || nr == 7 || nr == 9 || nr == 56) {
        Exception_build_stack_frame (oldpc, currpc, regs.mmu_ssw, nr, 0x2);
    } else if (nr == 2) {
        Exception_build_stack_frame (oldpc, currpc, regs.mmu_ssw, nr,  0xB);
    } else if (nr == 3) {
		regs.mmu_fault_addr = last_fault_for_exception_3;
		mmu030_state[0] = mmu030_state[1] = 0;
		mmu030_data_buffer = 0;
        Exception_build_stack_frame (last_fault_for_exception_3, currpc, MMU030_SSW_RW | MMU030_SSW_SIZE_W | (regs.s ? 6 : 2), nr,  0xA);
    } else {
        Exception_build_stack_frame (oldpc, currpc, regs.mmu_ssw, nr, 0x0);
    }
    
	if (newpc & 1) {
		if (nr == 2 || nr == 3)
			cpu_halt (2);
		else
			exception3 (regs.ir, newpc);
		return;
	}
	m68k_setpc (newpc);
#ifdef JIT
	set_special (SPCFLAG_END_COMPILE);
#endif
	fill_prefetch ();
	exception_trace (nr);
}

// 68040/060 MMU
static void Exception_mmu (int nr, uaecptr oldpc)
{
	uae_u32 currpc = m68k_getpc (), newpc;
	int sv = regs.s;

	exception_debug (nr);
	MakeSR ();

	if (!regs.s) {
		regs.usp = m68k_areg (regs, 7);
		if (currprefs.cpu_model == 68060) {
			m68k_areg (regs, 7) = regs.isp;
			if (nr >= 24 && nr < 32)
				regs.m = 0;
		} else if (currprefs.cpu_model >= 68020) {
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
			Exception_build_stack_frame(oldpc, currpc, regs.mmu_fslw, nr, 0x4);
	} else if (nr == 3) { // address error
        Exception_build_stack_frame(last_fault_for_exception_3, currpc, 0, nr, 0x2);
		write_log (_T("Exception %d (%x) at %x -> %x! %s at %d\n"), nr, last_fault_for_exception_3, currpc, get_long (regs.vbr + 4 * nr), __FILE__, __LINE__);
	} else if (nr == 5 || nr == 6 || nr == 7 || nr == 9) {
        Exception_build_stack_frame(oldpc, currpc, regs.mmu_ssw, nr, 0x2);
	} else if (regs.m && nr >= 24 && nr < 32) { /* M + Interrupt */
        Exception_build_stack_frame(oldpc, currpc, regs.mmu_ssw, nr, 0x1);
	} else if (nr == 61) {
        Exception_build_stack_frame(oldpc, regs.instruction_pc, regs.mmu_ssw, nr, 0x0);
	} else {
        Exception_build_stack_frame(oldpc, currpc, regs.mmu_ssw, nr, 0x0);
	}
    
	if (newpc & 1) {
		if (nr == 2 || nr == 3)
			cpu_halt (2);
		else
			exception3 (regs.ir, newpc);
		return;
	}
	m68k_setpc (newpc);
#ifdef JIT
	set_special (SPCFLAG_END_COMPILE);
#endif
	fill_prefetch ();
	exception_trace (nr);
}


static void Exception_normal (int nr)
{
	uae_u32 currpc, newpc;
	int sv = regs.s;

	if (nr >= 24 && nr < 24 + 8 && currprefs.cpu_model <= 68010)
		nr = x_get_byte (0x00fffff1 | (nr << 1));

	exception_debug (nr);
	MakeSR ();

	if (!regs.s) {
		regs.usp = m68k_areg (regs, 7);
		if (currprefs.cpu_model == 68060) {
			m68k_areg (regs, 7) = regs.isp;
			if (nr >= 24 && nr < 32)
				regs.m = 0;
		} else if (currprefs.cpu_model >= 68020) {
			m68k_areg (regs, 7) = regs.m ? regs.msp : regs.isp;
		} else {
			m68k_areg (regs, 7) = regs.isp;
		}
		regs.s = 1;
		if (currprefs.mmu_model)
			mmu_set_super (regs.s != 0);
	}
	if (currprefs.cpu_model > 68000) {
		currpc = exception_pc (nr);
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
						x_put_word (m68k_areg (regs, 7), 0x7000 + nr * 4);
						m68k_areg (regs, 7) -= 4;
						x_put_long (m68k_areg (regs, 7), regs.instruction_pc);
						m68k_areg (regs, 7) -= 2;
						x_put_word (m68k_areg (regs, 7), regs.sr);
						newpc = x_get_long (regs.vbr + 4 * nr);
						if (newpc & 1) {
							if (nr == 2 || nr == 3)
								cpu_halt (2);
							else
								exception3 (regs.ir, newpc);
							return;
						}
						m68k_setpc (newpc);
#ifdef JIT
						set_special (SPCFLAG_END_COMPILE);
#endif
						exception_trace (nr);
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
						x_put_word (m68k_areg (regs, 7), 0x7000 + nr * 4);
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
					x_put_word (m68k_areg (regs, 7), 0x2000 + nr * 4);
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
				x_put_word (m68k_areg (regs, 7), 0xb000 + nr * 4);
			}
			write_log (_T("Exception %d (%x) at %x -> %x!\n"), nr, regs.instruction_pc, currpc, x_get_long (regs.vbr + 4*nr));
		} else if (nr ==5 || nr == 6 || nr == 7 || nr == 9) {
			m68k_areg (regs, 7) -= 4;
			x_put_long (m68k_areg (regs, 7), regs.instruction_pc);
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), 0x2000 + nr * 4);
		} else if (regs.m && nr >= 24 && nr < 32) { /* M + Interrupt */
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), nr * 4);
			m68k_areg (regs, 7) -= 4;
			x_put_long (m68k_areg (regs, 7), currpc);
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), regs.sr);
			regs.sr |= (1 << 13);
			regs.msp = m68k_areg (regs, 7);
			m68k_areg (regs, 7) = regs.isp;
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), 0x1000 + nr * 4);
		} else {
			m68k_areg (regs, 7) -= 2;
			x_put_word (m68k_areg (regs, 7), nr * 4);
		}
	} else {
		currpc = m68k_getpc ();
		if (nr == 2 || nr == 3) {
			// 68000 address error
			uae_u16 mode = (sv ? 4 : 0) | (last_instructionaccess_for_exception_3 ? 2 : 1);
			mode |= last_writeaccess_for_exception_3 ? 0 : 16;
			m68k_areg (regs, 7) -= 14;
			/* fixme: bit3=I/N */
			x_put_word (m68k_areg (regs, 7) + 0, mode);
			x_put_long (m68k_areg (regs, 7) + 2, last_fault_for_exception_3);
			x_put_word (m68k_areg (regs, 7) + 6, last_op_for_exception_3);
			x_put_word (m68k_areg (regs, 7) + 8, regs.sr);
			x_put_long (m68k_areg (regs, 7) + 10, last_addr_for_exception_3);
			write_log (_T("Exception %d (%x) at %x -> %x!\n"), nr, last_fault_for_exception_3, currpc, x_get_long (regs.vbr + 4*nr));
			goto kludge_me_do;
		}
	}
	m68k_areg (regs, 7) -= 4;
	x_put_long (m68k_areg (regs, 7), currpc);
	m68k_areg (regs, 7) -= 2;
	x_put_word (m68k_areg (regs, 7), regs.sr);
kludge_me_do:
	newpc = x_get_long (regs.vbr + 4 * nr);
	if (newpc & 1) {
		if (nr == 2 || nr == 3)
			cpu_halt (2);
		else
			exception3 (regs.ir, newpc);
		return;
	}
	m68k_setpc (newpc);
#ifdef JIT
	set_special (SPCFLAG_END_COMPILE);
#endif
	fill_prefetch ();
	exception_trace (nr);
}

// address = format $2 stack frame address field
static void ExceptionX (int nr, uaecptr address)
{
	regs.exception = nr;
	if (cpu_tracer) {
		cputrace.state = nr;
	}

#ifdef JIT
	if (currprefs.cachesize)
		regs.instruction_pc = address == -1 ? m68k_getpc () : address;
#endif
#ifdef CPUEMU_12
	if (currprefs.cpu_cycle_exact && currprefs.cpu_model == 68000)
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

	if (debug_illegal && !in_rom (M68K_GETPC)) {
		int v = nr;
		if (nr <= 63 && (debug_illegal_mask & ((uae_u64)1 << nr))) {
			write_log (_T("Exception %d breakpoint\n"), nr);
			activate_debugger ();
		}
	}
	regs.exception = 0;
	if (cpu_tracer) {
		cputrace.state = 0;
	}
}

void REGPARAM2 Exception (int nr)
{
	ExceptionX (nr, -1);
}
void REGPARAM2 Exception (int nr, uaecptr address)
{
	ExceptionX (nr, address);
}

STATIC_INLINE void do_interrupt (int nr)
{
	if (debug_dma)
		record_dma_event (DMA_EVENT_CPUIRQ, current_hpos (), vpos);

	if (inputrecord_debug & 2) {
		if (input_record > 0)
			inprec_recorddebug_cpu (2);
		else if (input_play > 0)
			inprec_playdebug_cpu (2);
	}

	regs.stopped = 0;
	unset_special (SPCFLAG_STOP);
	assert (nr < 8 && nr >= 0);

	Exception (nr + 24);

	regs.intmask = nr;
	doint ();
}

void NMI (void)
{
	do_interrupt (7);
}

#ifndef CPUEMU_68000_ONLY

int movec_illg (int regno)
{
	int regno2 = regno & 0x7ff;

	if (currprefs.cpu_model == 68060) {
		if (regno <= 8)
			return 0;
		if (regno == 0x800 || regno == 0x801 ||
			regno == 0x806 || regno == 0x807 || regno == 0x808)
			return 0;
		return 1;
	} else if (currprefs.cpu_model == 68010) {
		if (regno2 < 2)
			return 0;
		return 1;
	} else if (currprefs.cpu_model == 68020) {
		if (regno == 3)
			return 1; /* 68040/060 only */
		/* 4 is >=68040, but 0x804 is in 68020 */
		if (regno2 < 4 || regno == 0x804)
			return 0;
		return 1;
	} else if (currprefs.cpu_model == 68030) {
		if (regno2 <= 2)
			return 0;
		if (regno == 0x803 || regno == 0x804)
			return 0;
		return 1;
	} else if (currprefs.cpu_model == 68040) {
		if (regno == 0x802)
			return 1; /* 68020 only */
		if (regno2 < 8) return 0;
		return 1;
	}
	return 1;
}

int m68k_move2c (int regno, uae_u32 *regp)
{
#if MOVEC_DEBUG > 0
	write_log (_T("move2c %04X <- %08X PC=%x\n"), regno, *regp, M68K_GETPC);
#endif
	if (movec_illg (regno)) {
		op_illg (0x4E7B);
		return 0;
	} else {
		switch (regno) {
		case 0: regs.sfc = *regp & 7; break;
		case 1: regs.dfc = *regp & 7; break;
		case 2:
			{
				uae_u32 cacr_mask = 0;
				if (currprefs.cpu_model == 68020)
					cacr_mask = 0x0000000f;
				else if (currprefs.cpu_model == 68030)
					cacr_mask = 0x00003f1f;
				else if (currprefs.cpu_model == 68040)
					cacr_mask = 0x80008000;
				else if (currprefs.cpu_model == 68060)
					cacr_mask = 0xf8e0e000;
				regs.cacr = *regp & cacr_mask;
				set_cpu_caches ();
			}
			break;
			/* 68040/060 only */
		case 3:
			regs.tcr = *regp & (currprefs.cpu_model == 68060 ? 0xfffe : 0xc000);
			if (currprefs.mmu_model)
				mmu_set_tc (regs.tcr);
			break;

			/* no differences between 68040 and 68060 */
		case 4: regs.itt0 = *regp & 0xffffe364; mmu_tt_modified (); break;
		case 5: regs.itt1 = *regp & 0xffffe364; mmu_tt_modified (); break;
		case 6: regs.dtt0 = *regp & 0xffffe364; mmu_tt_modified (); break;
		case 7: regs.dtt1 = *regp & 0xffffe364; mmu_tt_modified (); break;
			/* 68060 only */
		case 8: regs.buscr = *regp & 0xf0000000; break;

		case 0x800: regs.usp = *regp; break;
		case 0x801: regs.vbr = *regp; break;
		case 0x802: regs.caar = *regp & 0xfc; break;
		case 0x803: regs.msp = *regp; if (regs.m == 1) m68k_areg (regs, 7) = regs.msp; break;
		case 0x804: regs.isp = *regp; if (regs.m == 0) m68k_areg (regs, 7) = regs.isp; break;
			/* 68040 only */
		case 0x805: regs.mmusr = *regp; break;
			/* 68040/060 */
		case 0x806: regs.urp = *regp & 0xfffffe00; break;
		case 0x807: regs.srp = *regp & 0xfffffe00; break;
			/* 68060 only */
		case 0x808:
			{
				uae_u32 opcr = regs.pcr;
				regs.pcr &= ~(0x40 | 2 | 1);
				regs.pcr |= (*regp) & (0x40 | 2 | 1);
				if (currprefs.fpu_model <= 0)
					regs.pcr |= 2;
				if (((opcr ^ regs.pcr) & 2) == 2) {
					write_log (_T("68060 FPU state: %s\n"), regs.pcr & 2 ? _T("disabled") : _T("enabled"));
					/* flush possible already translated FPU instructions */
					flush_icache (0, 3);
				}
			}
			break;
		default:
			op_illg (0x4E7B);
			return 0;
		}
	}
	return 1;
}

int m68k_movec2 (int regno, uae_u32 *regp)
{
#if MOVEC_DEBUG > 0
	write_log (_T("movec2 %04X PC=%x\n"), regno, M68K_GETPC);
#endif
	if (movec_illg (regno)) {
		op_illg (0x4E7A);
		return 0;
	} else {
		switch (regno) {
		case 0: *regp = regs.sfc; break;
		case 1: *regp = regs.dfc; break;
		case 2:
			{
				uae_u32 v = regs.cacr;
				uae_u32 cacr_mask = 0;
				if (currprefs.cpu_model == 68020)
					cacr_mask = 0x00000003;
				else if (currprefs.cpu_model == 68030)
					cacr_mask = 0x00003313;
				else if (currprefs.cpu_model == 68040)
					cacr_mask = 0x80008000;
				else if (currprefs.cpu_model == 68060)
					cacr_mask = 0xf880e000;
				*regp = v & cacr_mask;
			}
			break;
		case 3: *regp = regs.tcr; break;
		case 4: *regp = regs.itt0; break;
		case 5: *regp = regs.itt1; break;
		case 6: *regp = regs.dtt0; break;
		case 7: *regp = regs.dtt1; break;
		case 8: *regp = regs.buscr; break;

		case 0x800: *regp = regs.usp; break;
		case 0x801: *regp = regs.vbr; break;
		case 0x802: *regp = regs.caar; break;
		case 0x803: *regp = regs.m == 1 ? m68k_areg (regs, 7) : regs.msp; break;
		case 0x804: *regp = regs.m == 0 ? m68k_areg (regs, 7) : regs.isp; break;
		case 0x805: *regp = regs.mmusr; break;
		case 0x806: *regp = regs.urp; break;
		case 0x807: *regp = regs.srp; break;
		case 0x808: *regp = regs.pcr; break;

		default:
			op_illg (0x4E7A);
			return 0;
		}
	}
#if MOVEC_DEBUG > 0
	write_log (_T("-> %08X\n"), *regp);
#endif
	return 1;
}

STATIC_INLINE int div_unsigned (uae_u32 src_hi, uae_u32 src_lo, uae_u32 div, uae_u32 *quot, uae_u32 *rem)
{
	uae_u32 q = 0, cbit = 0;
	int i;

	if (div <= src_hi) {
		return 1;
	}
	for (i = 0 ; i < 32 ; i++) {
		cbit = src_hi & 0x80000000ul;
		src_hi <<= 1;
		if (src_lo & 0x80000000ul) src_hi++;
		src_lo <<= 1;
		q = q << 1;
		if (cbit || div <= src_hi) {
			q |= 1;
			src_hi -= div;
		}
	}
	*quot = q;
	*rem = src_hi;
	return 0;
}

void m68k_divl (uae_u32 opcode, uae_u32 src, uae_u16 extra)
{
	if ((extra & 0x400) && currprefs.int_no_unimplemented && currprefs.cpu_model == 68060) {
		op_unimpl (opcode);
		return;
	}
	if (src == 0) {
		Exception (5);
		return;
	}
#if defined (uae_s64)
	if (extra & 0x800) {
		/* signed variant */
		uae_s64 a = (uae_s64)(uae_s32)m68k_dreg (regs, (extra >> 12) & 7);
		uae_s64 quot, rem;

		if (extra & 0x400) {
			a &= 0xffffffffu;
			a |= (uae_s64)m68k_dreg (regs, extra & 7) << 32;
		}

		if (a == 0x8000000000000000 && src == -1) {
			SET_VFLG (1);
			SET_NFLG (1);
			SET_CFLG (0);
		} else {
			rem = a % (uae_s64)(uae_s32)src;
			quot = a / (uae_s64)(uae_s32)src;
			if ((quot & UVAL64 (0xffffffff80000000)) != 0
				&& (quot & UVAL64 (0xffffffff80000000)) != UVAL64 (0xffffffff80000000))
			{
				SET_VFLG (1);
				SET_NFLG (1);
				SET_CFLG (0);
			} else {
				if (((uae_s32)rem < 0) != ((uae_s64)a < 0)) rem = -rem;
				SET_VFLG (0);
				SET_CFLG (0);
				SET_ZFLG (((uae_s32)quot) == 0);
				SET_NFLG (((uae_s32)quot) < 0);
				m68k_dreg (regs, extra & 7) = (uae_u32)rem;
				m68k_dreg (regs, (extra >> 12) & 7) = (uae_u32)quot;
			}
		}
	} else {
		/* unsigned */
		uae_u64 a = (uae_u64)(uae_u32)m68k_dreg (regs, (extra >> 12) & 7);
		uae_u64 quot, rem;

		if (extra & 0x400) {
			a &= 0xffffffffu;
			a |= (uae_u64)m68k_dreg (regs, extra & 7) << 32;
		}
		rem = a % (uae_u64)src;
		quot = a / (uae_u64)src;
		if (quot > 0xffffffffu) {
			SET_VFLG (1);
			SET_NFLG (1);
			SET_CFLG (0);
		} else {
			SET_VFLG (0);
			SET_CFLG (0);
			SET_ZFLG (((uae_s32)quot) == 0);
			SET_NFLG (((uae_s32)quot) < 0);
			m68k_dreg (regs, extra & 7) = (uae_u32)rem;
			m68k_dreg (regs, (extra >> 12) & 7) = (uae_u32)quot;
		}
	}
#else
	if (extra & 0x800) {
		/* signed variant */
		uae_s32 lo = (uae_s32)m68k_dreg (regs, (extra >> 12) & 7);
		uae_s32 hi = lo < 0 ? -1 : 0;
		uae_s32 save_high;
		uae_u32 quot, rem;
		uae_u32 sign;

		if (extra & 0x400) {
			hi = (uae_s32)m68k_dreg (regs, extra & 7);
		}
		save_high = hi;
		sign = (hi ^ src);
		if (hi < 0) {
			hi = ~hi;
			lo = -lo;
			if (lo == 0) hi++;
		}
		if ((uae_s32)src < 0) src = -src;
		if (div_unsigned (hi, lo, src, &quot, &rem) ||
			(sign & 0x80000000) ? quot > 0x80000000 : quot > 0x7fffffff) {
				SET_VFLG (1);
				SET_NFLG (1);
				SET_CFLG (0);
		} else {
			if (sign & 0x80000000) quot = -quot;
			if (((uae_s32)rem < 0) != (save_high < 0)) rem = -rem;
			SET_VFLG (0);
			SET_CFLG (0);
			SET_ZFLG (((uae_s32)quot) == 0);
			SET_NFLG (((uae_s32)quot) < 0);
			m68k_dreg (regs, extra & 7) = rem;
			m68k_dreg (regs, (extra >> 12) & 7) = quot;
		}
	} else {
		/* unsigned */
		uae_u32 lo = (uae_u32)m68k_dreg (regs, (extra >> 12) & 7);
		uae_u32 hi = 0;
		uae_u32 quot, rem;

		if (extra & 0x400) {
			hi = (uae_u32)m68k_dreg (regs, extra & 7);
		}
		if (div_unsigned (hi, lo, src, &quot, &rem)) {
			SET_VFLG (1);
			SET_NFLG (1);
			SET_CFLG (0);
		} else {
			SET_VFLG (0);
			SET_CFLG (0);
			SET_ZFLG (((uae_s32)quot) == 0);
			SET_NFLG (((uae_s32)quot) < 0);
			m68k_dreg (regs, extra & 7) = rem;
			m68k_dreg (regs, (extra >> 12) & 7) = quot;
		}
	}
#endif
}

STATIC_INLINE void mul_unsigned (uae_u32 src1, uae_u32 src2, uae_u32 *dst_hi, uae_u32 *dst_lo)
{
	uae_u32 r0 = (src1 & 0xffff) * (src2 & 0xffff);
	uae_u32 r1 = ((src1 >> 16) & 0xffff) * (src2 & 0xffff);
	uae_u32 r2 = (src1 & 0xffff) * ((src2 >> 16) & 0xffff);
	uae_u32 r3 = ((src1 >> 16) & 0xffff) * ((src2 >> 16) & 0xffff);
	uae_u32 lo;

	lo = r0 + ((r1 << 16) & 0xffff0000ul);
	if (lo < r0) r3++;
	r0 = lo;
	lo = r0 + ((r2 << 16) & 0xffff0000ul);
	if (lo < r0) r3++;
	r3 += ((r1 >> 16) & 0xffff) + ((r2 >> 16) & 0xffff);
	*dst_lo = lo;
	*dst_hi = r3;
}

void m68k_mull (uae_u32 opcode, uae_u32 src, uae_u16 extra)
{
	if ((extra & 0x400) && currprefs.int_no_unimplemented && currprefs.cpu_model == 68060) {
		op_unimpl (opcode);
		return;
	}
#if defined (uae_s64)
	if (extra & 0x800) {
		/* signed variant */
		uae_s64 a = (uae_s64)(uae_s32)m68k_dreg (regs, (extra >> 12) & 7);

		a *= (uae_s64)(uae_s32)src;
		SET_VFLG (0);
		SET_CFLG (0);
		SET_ZFLG (a == 0);
		SET_NFLG (a < 0);
		if (extra & 0x400)
			m68k_dreg (regs, extra & 7) = (uae_u32)(a >> 32);
		else if ((a & UVAL64 (0xffffffff80000000)) != 0
			&& (a & UVAL64 (0xffffffff80000000)) != UVAL64 (0xffffffff80000000))
		{
			SET_VFLG (1);
		}
		m68k_dreg (regs, (extra >> 12) & 7) = (uae_u32)a;
	} else {
		/* unsigned */
		uae_u64 a = (uae_u64)(uae_u32)m68k_dreg (regs, (extra >> 12) & 7);

		a *= (uae_u64)src;
		SET_VFLG (0);
		SET_CFLG (0);
		SET_ZFLG (a == 0);
		SET_NFLG (((uae_s64)a) < 0);
		if (extra & 0x400)
			m68k_dreg (regs, extra & 7) = (uae_u32)(a >> 32);
		else if ((a & UVAL64 (0xffffffff00000000)) != 0) {
			SET_VFLG (1);
		}
		m68k_dreg (regs, (extra >> 12) & 7) = (uae_u32)a;
	}
#else
	if (extra & 0x800) {
		/* signed variant */
		uae_s32 src1, src2;
		uae_u32 dst_lo, dst_hi;
		uae_u32 sign;

		src1 = (uae_s32)src;
		src2 = (uae_s32)m68k_dreg (regs, (extra >> 12) & 7);
		sign = (src1 ^ src2);
		if (src1 < 0) src1 = -src1;
		if (src2 < 0) src2 = -src2;
		mul_unsigned ((uae_u32)src1, (uae_u32)src2, &dst_hi, &dst_lo);
		if (sign & 0x80000000) {
			dst_hi = ~dst_hi;
			dst_lo = -dst_lo;
			if (dst_lo == 0) dst_hi++;
		}
		SET_VFLG (0);
		SET_CFLG (0);
		SET_ZFLG (dst_hi == 0 && dst_lo == 0);
		SET_NFLG (((uae_s32)dst_hi) < 0);
		if (extra & 0x400)
			m68k_dreg (regs, extra & 7) = dst_hi;
		else if ((dst_hi != 0 || (dst_lo & 0x80000000) != 0)
			&& ((dst_hi & 0xffffffff) != 0xffffffff
			|| (dst_lo & 0x80000000) != 0x80000000))
		{
			SET_VFLG (1);
		}
		m68k_dreg (regs, (extra >> 12) & 7) = dst_lo;
	} else {
		/* unsigned */
		uae_u32 dst_lo, dst_hi;

		mul_unsigned (src, (uae_u32)m68k_dreg (regs, (extra >> 12) & 7), &dst_hi, &dst_lo);

		SET_VFLG (0);
		SET_CFLG (0);
		SET_ZFLG (dst_hi == 0 && dst_lo == 0);
		SET_NFLG (((uae_s32)dst_hi) < 0);
		if (extra & 0x400)
			m68k_dreg (regs, extra & 7) = dst_hi;
		else if (dst_hi != 0) {
			SET_VFLG (1);
		}
		m68k_dreg (regs, (extra >> 12) & 7) = dst_lo;
	}
#endif
}

#endif

static void m68k_reset (bool hardreset)
{
	regs.spcflags = 0;
	regs.ipl = regs.ipl_pin = 0;
#ifdef SAVESTATE
	if (isrestore ()) {
		m68k_setpc (regs.pc);
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
		return;
	}
#endif
	m68k_areg (regs, 7) = get_long (0);
	m68k_setpc (get_long (4));
	regs.s = 1;
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
		set_cpu_caches ();
	}

	mmufixup[0].reg = -1;
	mmufixup[1].reg = -1;
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

	regs.halted = 0;
	gui_data.cpu_halted = false;
	gui_led (LED_CPU, 0);

	/* 68060 FPU is not compatible with 68040,
	* 68060 accelerators' boot ROM disables the FPU
	*/
	regs.pcr = 0;
	if (currprefs.cpu_model == 68060) {
		regs.pcr = currprefs.fpu_model == 68060 ? MC68060_PCR : MC68EC060_PCR;
		regs.pcr |= (currprefs.cpu060_revision & 0xff) << 8;
		if (kickstart_rom)
			regs.pcr |= 2; /* disable FPU */
	}
	regs.ce020memcycles = 0;
	fill_prefetch ();
}

void REGPARAM2 op_unimpl (uae_u16 opcode)
{
	static int warned;
	if (warned < 20) {
		write_log (_T("68060 unimplemented opcode %04X, PC=%08x\n"), opcode, regs.instruction_pc);
		warned++;
	}
	Exception (61, regs.instruction_pc);
}

uae_u32 REGPARAM2 op_illg (uae_u32 opcode)
{
	uaecptr pc = m68k_getpc ();
	static int warned;
	int inrom = in_rom (pc);
	int inrt = in_rtarea (pc);

	if (cloanto_rom && (opcode & 0xF100) == 0x7100) {
		m68k_dreg (regs, (opcode >> 9) & 7) = (uae_s8)(opcode & 0xFF);
		m68k_incpc (2);
		fill_prefetch ();
		return 4;
	}

	if (opcode == 0x4E7B && inrom && get_long (0x10) == 0) {
		notify_user (NUMSG_KS68020);
		uae_restart (-1, NULL);
	}

#ifdef AUTOCONFIG
	if (opcode == 0xFF0D && inrt) {
		/* User-mode STOP replacement */
		m68k_setstopped ();
		return 4;
	}

	if ((opcode & 0xF000) == 0xA000 && inrt) {
		/* Calltrap. */
		m68k_incpc (2);
		m68k_handle_trap (opcode & 0xFFF);
		fill_prefetch ();
		return 4;
	}
#endif

	if ((opcode & 0xF000) == 0xF000) {
		if (warned < 20) {
			write_log (_T("B-Trap %x at %x (%p)\n"), opcode, pc, regs.pc_p);
			warned++;
		}
		Exception (0xB);
		//activate_debugger ();
		return 4;
	}
	if ((opcode & 0xF000) == 0xA000) {
		if (warned < 20) {
			write_log (_T("A-Trap %x at %x (%p)\n"), opcode, pc, regs.pc_p);
			warned++;
		}
		Exception (0xA);
		//activate_debugger();
		return 4;
	}
	if (warned < 20) {
		write_log (_T("Illegal instruction: %04x at %08X -> %08X\n"), opcode, pc, get_long (regs.vbr + 0x10));
		warned++;
		//activate_debugger();
	}

	Exception (4);
	return 4;
}

#ifdef CPUEMU_0

static TCHAR *mmu30regs[] = { _T("TCR"), _T(""), _T("SRP"), _T("CRP"), _T(""), _T(""), _T(""), _T("") };

static void mmu_op30fake_pmove (uaecptr pc, uae_u32 opcode, uae_u16 next, uaecptr extra)
{
	int mode = (opcode >> 3) & 7;
	int preg = (next >> 10) & 31;
	int rw = (next >> 9) & 1;
	int fd = (next >> 8) & 1;
	TCHAR *reg = NULL;
	uae_u32 otc = fake_tc_030;
	int siz;

	// Dn, An, (An)+, -(An), abs and indirect
	if (mode == 0 || mode == 1 || mode == 3 || mode == 4 || mode >= 6) {
		op_illg (opcode);
		return;
	}

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
			x_put_long (extra + 4, fake_srp_030);
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
			x_put_long (extra + 4, fake_crp_030);
		} else {
			fake_crp_030 = (uae_u64)x_get_long (extra) << 32;
			fake_crp_030 |= x_get_long (extra + 4);
		}
		break;
	case 0x18: // MMUSR
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

	if (!reg) {
		op_illg (opcode);
		return;
	}
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
	if (currprefs.cs_mbdmac == 1 && currprefs.mbresmem_low_size > 0) {
		if (otc != fake_tc_030) {
			a3000_fakekick (fake_tc_030 & 0x80000000);
		}
	}
}

static void mmu_op30fake_ptest (uaecptr pc, uae_u32 opcode, uae_u16 next, uaecptr extra)
{
#if MMUOP_DEBUG > 0
	TCHAR tmp[10];

	tmp[0] = 0;
	if ((next >> 8) & 1)
		_stprintf (tmp, _T(",A%d"), (next >> 4) & 15);
	write_log (_T("PTEST%c %02X,%08X,#%X%s PC=%08X\n"),
		((next >> 9) & 1) ? 'W' : 'R', (next & 15), extra, (next >> 10) & 7, tmp, pc);
#endif
	fake_mmusr_030 = 0;
}

static void mmu_op30fake_pflush (uaecptr pc, uae_u32 opcode, uae_u16 next, uaecptr extra)
{
	int mode = (opcode >> 3) & 7;
	int reg = opcode & 7;
	int flushmode = (next >> 10) & 7;
	int fc = next & 31;
	int mask = (next >> 5) & 3;
	TCHAR fname[100];

	switch (flushmode)
	{
	case 6:
		// Dn, An, (An)+, -(An), abs and indirect
		if (mode == 0 || mode == 1 || mode == 3 || mode == 4 || mode >= 6) {
			op_illg (opcode);
			return;
		}
		_stprintf (fname, _T("FC=%x MASK=%x EA=%08x"), fc, mask, 0);
		break;
	case 4:
		_stprintf (fname, _T("FC=%x MASK=%x"), fc, mask);
		break;
	case 1:
		_tcscpy (fname, _T("ALL"));
		break;
	default:
		op_illg (opcode);
		return;
	}
#if MMUOP_DEBUG > 0
	write_log (_T("PFLUSH %s PC=%08X\n"), fname, pc);
#endif
}

// 68030 (68851) MMU instructions only
void mmu_op30 (uaecptr pc, uae_u32 opcode, uae_u16 extra, uaecptr extraa)
{
	if (currprefs.mmu_model) {
		if (extra & 0x8000)
			mmu_op30_ptest (pc, opcode, extra, extraa);
		else if ((extra&0xE000)==0x2000 && (extra & 0x1C00))
			mmu_op30_pflush (pc, opcode, extra, extraa);
	    else if ((extra&0xE000)==0x2000 && !(extra & 0x1C00))
	        mmu_op30_pload (pc, opcode, extra, extraa);
		else
			mmu_op30_pmove (pc, opcode, extra, extraa);
		return;
	}

	int type = extra >> 13;

	switch (type)
	{
	case 0:
	case 2:
	case 3:
		mmu_op30fake_pmove (pc, opcode, extra, extraa);
	break;
	case 1:
		mmu_op30fake_pflush (pc, opcode, extra, extraa);
	break;
	case 4:
		mmu_op30fake_ptest (pc, opcode, extra, extraa);
	break;
	default:
		op_illg (opcode);
	break;
	}
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
	} else if ((opcode & 0x0FD8) == 0x548) {
		if (currprefs.cpu_model < 68060) { /* PTEST not in 68060 */
			/* PTEST */
#if MMUOP_DEBUG > 0
			write_log (_T("PTEST\n"));
#endif
			return;
		}
	} else if ((opcode & 0x0FB8) == 0x588) {
		/* PLPA */
		if (currprefs.cpu_model == 68060) {
#if MMUOP_DEBUG > 0
			write_log (_T("PLPA\n"));
#endif
			return;
		}
	}
#if MMUOP_DEBUG > 0
	write_log (_T("Unknown MMU OP %04X\n"), opcode);
#endif
	m68k_setpc (m68k_getpc () - 2);
	op_illg (opcode);
}

#endif

static uaecptr last_trace_ad = 0;

static void do_trace (void)
{
	if (regs.t0 && currprefs.cpu_model >= 68020) {
		uae_u16 opcode;
		/* should also include TRAP, CHK, SR modification FPcc */
		/* probably never used so why bother */
		/* We can afford this to be inefficient... */
		m68k_setpc (m68k_getpc ());
		fill_prefetch ();
		opcode = x_get_word (regs.pc);
		if (opcode == 0x4e73 			/* RTE */
			|| opcode == 0x4e74 		/* RTD */
			|| opcode == 0x4e75 		/* RTS */
			|| opcode == 0x4e77 		/* RTR */
			|| opcode == 0x4e76 		/* TRAPV */
			|| (opcode & 0xffc0) == 0x4e80 	/* JSR */
			|| (opcode & 0xffc0) == 0x4ec0 	/* JMP */
			|| (opcode & 0xff00) == 0x6100	/* BSR */
			|| ((opcode & 0xf000) == 0x6000	/* Bcc */
			&& cctrue ((opcode >> 8) & 0xf))
			|| ((opcode & 0xf0f0) == 0x5050	/* DBcc */
			&& !cctrue ((opcode >> 8) & 0xf)
			&& (uae_s16)m68k_dreg (regs, opcode & 7) != 0))
		{
			last_trace_ad = m68k_getpc ();
			unset_special (SPCFLAG_TRACE);
			set_special (SPCFLAG_DOTRACE);
		}
	} else if (regs.t1) {
		last_trace_ad = m68k_getpc ();
		unset_special (SPCFLAG_TRACE);
		set_special (SPCFLAG_DOTRACE);
	}
}


// handle interrupt delay (few cycles)
STATIC_INLINE bool time_for_interrupt (void)
{
	return regs.ipl > regs.intmask || regs.ipl == 7;
}

void doint (void)
{
	if (currprefs.cpu_cycle_exact) {
		regs.ipl_pin = intlev ();
		unset_special (SPCFLAG_INT);
		return;
	}
	if (currprefs.cpu_compatible && currprefs.cpu_model < 68020)
		set_special (SPCFLAG_INT);
	else
		set_special (SPCFLAG_DOINT);
}

#define IDLETIME (currprefs.cpu_idle * sleep_resolution / 1000)

STATIC_INLINE int do_specialties (int cycles)
{
		regs.instruction_pc = m68k_getpc ();
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
		if (action_replay_flag == ACTION_REPLAY_HIDE && !is_ar_pc_in_rom ()) {
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

	while ((regs.spcflags & SPCFLAG_BLTNASTY) && dmaen (DMA_BLITTER) && cycles > 0 && !currprefs.blitter_cycle_exact) {
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
	}

	if (regs.spcflags & SPCFLAG_DOTRACE)
		Exception (9);

	if (regs.spcflags & SPCFLAG_TRAP) {
		unset_special (SPCFLAG_TRAP);
		Exception (3);
	}

	while (regs.spcflags & SPCFLAG_STOP) {

		if (uae_int_requested || uaenet_int_requested) {
			INTREQ_f (0x8008);
			set_special (SPCFLAG_INT);
		}
		{
			extern void bsdsock_fake_int_handler (void);
			extern int volatile bsd_int_requested;
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
		x_do_cycles (currprefs.cpu_cycle_exact ? 2 * CYCLE_UNIT : 4 * CYCLE_UNIT);
		if (regs.spcflags & SPCFLAG_COPPER)
			do_copper ();

		if (currprefs.cpu_cycle_exact) {
			ipl_fetch ();
			if (time_for_interrupt ()) {
				do_interrupt (regs.ipl);
			}
		} else {
			if (regs.spcflags & (SPCFLAG_INT | SPCFLAG_DOINT)) {
				int intr = intlev ();
				unset_special (SPCFLAG_INT | SPCFLAG_DOINT);
				if (intr > 0 && intr > regs.intmask)
					do_interrupt (intr);
			}
		}

		if ((regs.spcflags & (SPCFLAG_BRK | SPCFLAG_MODE_CHANGE))) {
			unset_special (SPCFLAG_BRK | SPCFLAG_MODE_CHANGE);
			// SPCFLAG_BRK breaks STOP condition, need to prefetch
			m68k_resumestopped ();
			return 1;
		}

		if (!uae_int_requested && !uaenet_int_requested && currprefs.cpu_idle && currprefs.m68k_speed != 0 && (regs.spcflags & SPCFLAG_STOP)) {
			/* sleep 1ms if STOP-instruction is executed
			 * but only if we have free frametime left to prevent slowdown
			 */
			{
				static int sleepcnt, lvpos, zerocnt;
				if (vpos != lvpos) {
					lvpos = vpos;
					frame_time_t rpt = read_processor_time ();
					if ((int)rpt - (int)vsyncmaxtime < 0) {
						sleepcnt--;
#if 0
						if (pissoff == 0 && currprefs.cachesize && --zerocnt < 0) {
							sleepcnt = -1;
							zerocnt = IDLETIME / 4;
						}
#endif
						if (sleepcnt < 0) {
							sleepcnt = IDLETIME / 2;
							sleep_millis_main (1);
						}
					}
				}
			}
		}
	}

	if (regs.spcflags & SPCFLAG_TRACE)
		do_trace ();

	if (currprefs.cpu_cycle_exact) {
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

	if ((regs.spcflags & (SPCFLAG_BRK | SPCFLAG_MODE_CHANGE)))
		return 1;
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

STATIC_INLINE int adjust_cycles (int cycles)
{
	if (currprefs.m68k_speed < 0 || cycles_mult == 0)
		return cycles;
	cpu_cycles *= cycles_mult;
	cpu_cycles /= CYCLES_DIV;
	return cpu_cycles;
}

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

	for (;;) {
		uae_u16 opcode = r->ir;

		count_instr (opcode);

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
			//write_log (_T("%08X-%04X "), pc, opcode);
		}
#endif
		do_cycles (cpu_cycles);
		cpu_cycles = (*cpufunctbl[opcode])(opcode);
		cpu_cycles = adjust_cycles (cpu_cycles);
		if (r->spcflags) {
			if (do_specialties (cpu_cycles)) {
				regs.ipl = regs.ipl_pin;
				return;
			}
		}
		regs.ipl = regs.ipl_pin;
		if (!currprefs.cpu_compatible || (currprefs.cpu_cycle_exact && currprefs.cpu_model <= 68000))
			return;
	}
}

#endif /* CPUEMU_11 */

#ifndef CPUEMU_12

static void m68k_run_1_ce (void)
{
}

#else

/* cycle-exact m68k_run () */

static void m68k_run_1_ce (void)
{
	struct regstruct *r = &regs;
	uae_u16 opcode;

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

	for (;;) {
		opcode = r->ir;

#if DEBUG_CD32CDTVIO
		out_cd32io (m68k_getpc ());
#endif
		if (cpu_tracer) {
			memcpy (&cputrace.regs, &r->regs, 16 * sizeof (uae_u32));
			cputrace.opcode = opcode;
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

		(*cpufunctbl[opcode])(opcode);
		if (cpu_tracer) {
			cputrace.state = 0;
		}
cont:
		if (cputrace.needendcycles) {
			cputrace.needendcycles = 0;
			write_log (_T("STARTCYCLES=%08x ENDCYCLES=%08x\n"), cputrace.startcycles, get_cycles ());
			log_dma_record ();
		}

		if (r->spcflags || time_for_interrupt ()) {
			if (do_specialties (0))
				return;
		}

		if (!currprefs.cpu_cycle_exact || currprefs.cpu_model > 68000)
			return;
	}
}

#endif

#ifdef CPUEMU_20
// emulate simple prefetch
static uae_u32 get_word_020_prefetchf (uae_u32 pc)
{
	if (pc == regs.prefetch020addr) {
		uae_u32 v = regs.prefetch020[0];
		regs.prefetch020[0] = regs.prefetch020[1];
		regs.prefetch020[1] = regs.prefetch020[2];
		regs.prefetch020[2] = x_get_word (pc + 6);
		regs.prefetch020addr += 2;
		return v;
	} else if (pc == regs.prefetch020addr + 2) {
		uae_u32 v = regs.prefetch020[1];
		regs.prefetch020[0] = regs.prefetch020[2];
		regs.prefetch020[1] = x_get_word (pc + 4);
		regs.prefetch020[2] = x_get_word (pc + 6);
		regs.prefetch020addr += 4;
		return v;
	} else if (pc == regs.prefetch020addr + 4) {
		uae_u32 v = regs.prefetch020[2];
		regs.prefetch020[0] = x_get_word (pc + 2);
		regs.prefetch020[1] = x_get_word (pc + 4);
		regs.prefetch020[2] = x_get_word (pc + 6);
		regs.prefetch020addr += 6;
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

#ifdef JIT  /* Completely different run_2 replacement */

void do_nothing (void)
{
	/* What did you expect this to do? */
	do_cycles (0);
	/* I bet you didn't expect *that* ;-) */
}

void exec_nostats (void)
{
	struct regstruct *r = &regs;

	for (;;)
	{
		uae_u16 opcode = get_iword (0);
		cpu_cycles = (*cpufunctbl[opcode])(opcode);
		cpu_cycles = adjust_cycles (cpu_cycles);
		do_cycles (cpu_cycles);

		if (end_block (opcode) || r->spcflags || uae_int_requested || uaenet_int_requested)
			return; /* We will deal with the spcflags in the caller */
	}
}

static int triggered;

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
		uae_u16 opcode;

		regs.instruction_pc = m68k_getpc ();
		if (currprefs.cpu_compatible) {
			opcode = get_word_020_prefetchf (regs.instruction_pc);
		} else {
			opcode = get_iword (0);
		}

		special_mem = DISTRUST_CONSISTENT_MEM;
		pc_hist[blocklen].location = (uae_u16*)r->pc_p;

		cpu_cycles = (*cpufunctbl[opcode])(opcode);
		cpu_cycles = adjust_cycles (cpu_cycles);
		do_cycles (cpu_cycles);
		total_cycles += cpu_cycles;
		pc_hist[blocklen].specmem = special_mem;
		blocklen++;
		if (end_block (opcode) || blocklen >= MAXRUN || r->spcflags || uae_int_requested || uaenet_int_requested) {
			compile_block (pc_hist, blocklen, total_cycles);
			return; /* We will deal with the spcflags in the caller */
		}
		/* No need to check regs.spcflags, because if they were set,
		we'd have ended up inside that "if" */
	}
}

typedef void compiled_handler (void);

static void m68k_run_jit (void)
{
	for (;;) {
		((compiled_handler*)(pushall_call_handler))();
		/* Whenever we return from that, we should check spcflags */
		if (uae_int_requested || uaenet_int_requested) {
			INTREQ_f (0x8008);
			set_special (SPCFLAG_INT);
		}
		if (regs.spcflags) {
			if (do_specialties (0)) {
				return;
			}
		}
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
		addr = mmu_translate (pc, (regs.mmu_ssw & 4) ? 1 : 0, 0, 0);
	} CATCH (prb) {
		fault = 1;
	}
	if (!fault) {
		TCHAR buf[100];
		if (full)
			write_log (_T("mmufixup=%d %04x %04x\n"), mmufixup[0].reg, regs.wb3_status, regs.mmu_ssw);
		m68k_disasm_2 (buf, sizeof buf / sizeof (TCHAR), addr, NULL, 1, NULL, NULL, 0);
		write_log (_T("%s\n"), buf);
		if (full)
			m68k_dumpstate (NULL);
	}
}

void cpu_halt (int id)
{
	if (!regs.halted) {
		write_log (_T("CPU halted: reason = %d\n"), id);
		regs.halted = id;
		gui_data.cpu_halted = true;
		gui_led (LED_CPU, 0);
		regs.intmask = 7;
		MakeSR ();
	}
	while (regs.halted) {
		x_do_cycles (8 * CYCLE_UNIT);
		cpu_cycles = adjust_cycles (cpu_cycles);
		if (regs.spcflags) {
			if ((regs.spcflags & (SPCFLAG_BRK | SPCFLAG_MODE_CHANGE)))
				return;
		}
	}
}

#ifdef CPUEMU_33

/* MMU 68060  */
static void m68k_run_mmu060 (void)
{
	uae_u16 opcode;
	uaecptr pc;
	flag_struct f;

retry:
	TRY (prb) {
		for (;;) {
			f.cznv = regflags.cznv;
			f.x = regflags.x;
			pc = regs.instruction_pc = m68k_getpc ();

			do_cycles (cpu_cycles);

			mmu_opcode = -1;
			mmu060_state = 0;
			mmu_opcode = opcode = x_prefetch (0);
			mmu060_state = 1;

			count_instr (opcode);
			cpu_cycles = (*cpufunctbl[opcode])(opcode);

			cpu_cycles = adjust_cycles (cpu_cycles);
			if (regs.spcflags) {
				if (do_specialties (cpu_cycles))
					return;
			}
		}
	} CATCH (prb) {

		m68k_setpc (regs.instruction_pc);
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
			cpu_halt (1);
			return;
		}
		goto retry;
	}

}

#endif

#ifdef CPUEMU_31

/* Aranym MMU 68040  */
static void m68k_run_mmu040 (void)
{
	uae_u16 opcode;
	flag_struct f;
	uaecptr pc;

retry:
	TRY (prb) {
		for (;;) {
			f.cznv = regflags.cznv;
			f.x = regflags.x;
			mmu_restart = true;
			pc = regs.instruction_pc = m68k_getpc ();

			do_cycles (cpu_cycles);

			mmu_opcode = -1;
			mmu_opcode = opcode = x_prefetch (0);
			count_instr (opcode);
			cpu_cycles = (*cpufunctbl[opcode])(opcode);
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
			m68k_setpc (regs.instruction_pc);
		}

		if (mmufixup[0].reg >= 0) {
			m68k_areg (regs, mmufixup[0].reg) = mmufixup[0].value;
			mmufixup[0].reg = -1;
		}

		TRY (prb2) {
			Exception (prb);
		} CATCH (prb2) {
			cpu_halt (1);
			return;
		}
		goto retry;
	}

}

#endif

#ifdef CPUEMU_32

// Previous MMU 68030
static void m68k_run_mmu030 (void)
{
	uae_u16 opcode;
	uaecptr pc;
	flag_struct f;

	mmu030_opcode_stageb = -1;
retry:
	TRY (prb) {
		for (;;) {
			int cnt;
insretry:
			pc = regs.instruction_pc = m68k_getpc ();
			f.cznv = regflags.cznv;
			f.x = regflags.x;

			mmu030_state[0] = mmu030_state[1] = mmu030_state[2] = 0;
			mmu030_opcode = -1;
			if (mmu030_opcode_stageb < 0) {
				opcode = get_iword_mmu030 (0);
			} else {
				opcode = mmu030_opcode_stageb;
				mmu030_opcode_stageb = -1;
			}

			mmu030_opcode = opcode;
			mmu030_ad[0].done = false;

			cnt = 50;
			for (;;) {
				opcode = mmu030_opcode;
				mmu030_idx = 0;
				count_instr (opcode);
				do_cycles (cpu_cycles);
				mmu030_retry = false;
				cpu_cycles = (*cpufunctbl[opcode])(opcode);
				cnt--; // so that we don't get in infinite loop if things go horribly wrong
				if (!mmu030_retry)
					break;
				if (cnt < 0) {
					cpu_halt (9);
					break;
				}
				if (mmu030_retry && mmu030_opcode == -1)
					goto insretry; // urgh
			}

			mmu030_opcode = -1;

			cpu_cycles = adjust_cycles (cpu_cycles);
			if (regs.spcflags) {
				if (do_specialties (cpu_cycles))
					return;
			}
		}
	} CATCH (prb) {

		regflags.cznv = f.cznv;
		regflags.x = f.x;

		m68k_setpc (regs.instruction_pc);

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
			cpu_halt (1);
			return;
		}
		goto retry;
	}

}

#endif


/* "cycle exact" 68040+ */

static void m68k_run_3ce (void)
{
	struct regstruct *r = &regs;
	uae_u16 opcode;
	bool exit = false;

	for (;;) {
		r->instruction_pc = m68k_getpc ();
		opcode = get_word_ce040_prefetch (0);

		(*cpufunctbl[opcode])(opcode);

		if (r->spcflags || time_for_interrupt ()) {
			if (do_specialties (0))
				exit = true;
		}

		regs.ipl = regs.ipl_pin;

		if (exit)
			return;
	}
}

/* "cycle exact" 68020/030  */


static void m68k_run_2ce (void)
{
	struct regstruct *r = &regs;
	uae_u16 opcode;
	bool exit = false;

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
		memcpy (&r->prefetch020, &cputrace.prefetch020, CPU_PIPELINE_MAX * sizeof (uae_u32));
		memcpy (&caches020, &cputrace.caches020, sizeof caches020);

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

	for (;;) {
		r->instruction_pc = m68k_getpc ();

		opcode = currprefs.cpu_model == 68020 ? get_word_ce020_prefetch (0) : get_word_ce030_prefetch (0);

		//write_log (_T("%08x %04x\n"), r->instruction_pc, opcode);

#if DEBUG_CD32CDTVIO
		out_cd32io (r->instruction_pc);
#endif

		if (cpu_tracer) {

#if CPUTRACE_DEBUG
			validate_trace ();
#endif
			memcpy (&cputrace.regs, &r->regs, 16 * sizeof (uae_u32));
			cputrace.opcode = opcode;
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
			memcpy (&cputrace.prefetch020, &r->prefetch020, CPU_PIPELINE_MAX * sizeof (uae_u32));
			memcpy (&cputrace.caches020, &caches020, sizeof caches020);

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

		(*cpufunctbl[opcode])(opcode);

cont:
		if (r->spcflags || time_for_interrupt ()) {
			if (do_specialties (0))
				exit = true;
		}

		regs.ipl = regs.ipl_pin;

		if (exit)
			return;
	}
}

#ifdef CPUEMU_20

// only opcode fetch prefetch
static void m68k_run_2pf (void)
{
	struct regstruct *r = &regs;

	for (;;) {
		uae_u16 opcode;

		r->instruction_pc = m68k_getpc ();

#if DEBUG_CD32CDTVIO
		out_cd32io (m68k_getpc ());
#endif

		x_do_cycles (cpu_cycles);

		opcode = get_word_020_prefetchf (r->instruction_pc);

		count_instr (opcode);

		cpu_cycles = (*cpufunctbl[opcode])(opcode);
		cpu_cycles = adjust_cycles (cpu_cycles);
		if (r->spcflags) {
			if (do_specialties (cpu_cycles))
				return;
		}
	}
}

// full prefetch 020+
static void m68k_run_2p (void)
{
	struct regstruct *r = &regs;

	for (;;) {
		uae_u16 opcode;

		r->instruction_pc = m68k_getpc ();

#if DEBUG_CD32CDTVIO
		out_cd32io (m68k_getpc ());
#endif

		x_do_cycles (cpu_cycles);

		opcode = get_word_020_prefetch (0);
		count_instr (opcode);

		cpu_cycles = (*cpufunctbl[opcode])(opcode);
		cpu_cycles = adjust_cycles (cpu_cycles);
		if (r->spcflags) {
			if (do_specialties (cpu_cycles)) {
				ipl_fetch ();
				return;
			}
		}
		ipl_fetch ();
	}
}

#endif

//static int used[65536];

/* Same thing, but don't use prefetch to get opcode.  */
static void m68k_run_2 (void)
{
	struct regstruct *r = &regs;

	for (;;) {
		r->instruction_pc = m68k_getpc ();
		uae_u16 opcode = get_iword (0);
		count_instr (opcode);

//		if (regs.s == 0 && regs.regs[15] < 0x10040000 && regs.regs[15] > 0x10000000)
//			activate_debugger();

#if 0
		if (!used[opcode]) {
			write_log (_T("%04X "), opcode);
			used[opcode] = 1;
		}
#endif	
		do_cycles (cpu_cycles);
		cpu_cycles = (*cpufunctbl[opcode])(opcode);
		cpu_cycles = adjust_cycles (cpu_cycles);
		if (r->spcflags) {
			if (do_specialties (cpu_cycles))
				return;
		}
	}
}

/* fake MMU 68k  */
static void m68k_run_mmu (void)
{
	for (;;) {
		uae_u16 opcode = get_iword (0);
		do_cycles (cpu_cycles);
		mmu_backup_regs = regs;
		cpu_cycles = (*cpufunctbl[opcode])(opcode);
		cpu_cycles = adjust_cycles (cpu_cycles);
		if (mmu_triggered)
			mmu_do_hit ();
		if (regs.spcflags) {
			if (do_specialties (cpu_cycles))
				return;
		}
	}
}

#endif /* CPUEMU_0 */

int in_m68k_go = 0;

static void exception2_handle (uaecptr addr, uaecptr fault)
{
	last_addr_for_exception_3 = addr;
	last_fault_for_exception_3 = fault;
	last_writeaccess_for_exception_3 = 0;
	last_instructionaccess_for_exception_3 = 0;
	Exception (2);
}

void m68k_go (int may_quit)
{
	int hardboot = 1;
	int startup = 1;

	if (in_m68k_go || !may_quit) {
		write_log (_T("Bug! m68k_go is not reentrant.\n"));
		abort ();
	}

	reset_frame_rate_hack ();
	update_68k_cycles ();
	start_cycles = 0;

	set_cpu_tracer (false);

	in_m68k_go++;
	for (;;) {
		void (*run_func)(void);

		cputrace.state = -1;

		if (currprefs.inprecfile[0] && input_play) {
			inprec_open (currprefs.inprecfile, NULL);
			changed_prefs.inprecfile[0] = currprefs.inprecfile[0] = 0;
			quit_program = UAE_RESET;
		}
		if (input_play || input_record)
			inprec_startup ();

		if (quit_program > 0) {
			int hardreset = (quit_program == UAE_RESET_HARD ? 1 : 0) | hardboot;
			bool kbreset = quit_program == UAE_RESET_KEYBOARD;
			if (quit_program == UAE_QUIT)
				break;
			int restored = 0;

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
			set_cycles (start_cycles);
			custom_reset (hardreset != 0, kbreset);
			m68k_reset (hardreset != 0);
			if (hardreset) {
				memory_clear ();
				write_log (_T("hardreset, memory cleared\n"));
			}
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
					mmu030_decode_tc (tc_030);
				} else if (currprefs.mmu_model >= 68040) {
					mmu_set_tc (regs.tcr);
				}
				startup = 1;
				restored = 1;
			}
#endif
			if (currprefs.produce_sound == 0)
				eventtab[ev_audio].active = 0;
			m68k_setpc (regs.pc);
			check_prefs_changed_audio ();

			if (!restored || hsync_counter == 0)
				savestate_check ();
			if (input_record == INPREC_RECORD_START)
				input_record = INPREC_RECORD_NORMAL;
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

		set_cpu_tracer (false);

#ifdef DEBUGGER
		if (debugging)
			debug ();
#endif
		if (regs.panic) {
			regs.panic = 0;
			/* program jumped to non-existing memory and cpu was >= 68020 */
			get_real_address (regs.isp); /* stack in no one's land? -> halt */
			if (regs.isp & 1)
				regs.panic = 5;
			if (!regs.panic)
				exception2_handle (regs.panic_pc, regs.panic_addr);
			if (regs.panic) {
				int id = regs.panic;
				/* system is very badly confused */
				regs.panic = 0;
				cpu_halt (id);
			}
		}

#if 0 /* what was the meaning of this? this breaks trace emulation if debugger is used */
		if (regs.spcflags) {
			uae_u32 of = regs.spcflags;
			regs.spcflags &= ~(SPCFLAG_BRK | SPCFLAG_MODE_CHANGE);
			do_specialties (0);
			regs.spcflags |= of & (SPCFLAG_BRK | SPCFLAG_MODE_CHANGE);
		}
#endif
		set_x_funcs ();
		if (startup) {
			custom_prepare ();
			protect_roms (true);
		}
		startup = 0;
		if (regs.halted) {
			cpu_halt (regs.halted);
			continue;
		}
#if 0
		if (mmu_enabled && !currprefs.cachesize) {
			run_func = m68k_run_mmu;
		} else {
#endif
			run_func = currprefs.cpu_cycle_exact && currprefs.cpu_model == 68000 ? m68k_run_1_ce :
				currprefs.cpu_compatible && currprefs.cpu_model == 68000 ? m68k_run_1 :
#ifdef JIT
				currprefs.cpu_model >= 68020 && currprefs.cachesize ? m68k_run_jit :
#endif
				currprefs.cpu_model == 68030 && currprefs.mmu_model ? m68k_run_mmu030 :
				currprefs.cpu_model == 68040 && currprefs.mmu_model ? m68k_run_mmu040 :
				currprefs.cpu_model == 68060 && currprefs.mmu_model ? m68k_run_mmu060 :
				currprefs.cpu_model >= 68040 && currprefs.cpu_cycle_exact ? m68k_run_3ce :
				currprefs.cpu_model >= 68020 && currprefs.cpu_cycle_exact ? m68k_run_2ce :
				currprefs.cpu_compatible ? (currprefs.cpu_model <= 68020 ? m68k_run_2p : m68k_run_2pf) : m68k_run_2;
#if 0
		}
#endif
		run_func ();
		unset_special (SPCFLAG_BRK | SPCFLAG_MODE_CHANGE);
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
{ _T("T "),_T("F "),_T("HI"),_T("LS"),_T("CC"),_T("CS"),_T("NE"),_T("EQ"),
_T("VC"),_T("VS"),_T("PL"),_T("MI"),_T("GE"),_T("LT"),_T("GT"),_T("LE") };

static void addmovemreg (TCHAR *out, int *prevreg, int *lastreg, int *first, int reg)
{
	TCHAR *p = out + _tcslen (out);
	if (*prevreg < 0) {
		*prevreg = reg;
		*lastreg = reg;
		return;
	}
	if ((*prevreg) + 1 != reg || (reg & 8) != ((*prevreg & 8))) {
		_stprintf (p, _T("%s%c%d"), (*first) ? _T("") : _T("/"), (*lastreg) < 8 ? 'D' : 'A', (*lastreg) & 7);
		p = p + _tcslen (p);
		if ((*lastreg) + 2 == reg) {
			_stprintf (p, _T("/%c%d"), (*prevreg) < 8 ? 'D' : 'A', (*prevreg) & 7);
		} else if ((*lastreg) != (*prevreg)) {
			_stprintf (p, _T("-%c%d"), (*prevreg) < 8 ? 'D' : 'A', (*prevreg) & 7);
		}
		*lastreg = reg;
		*first = 0;
	}
	*prevreg = reg;
}

static void movemout (TCHAR *out, uae_u16 mask, int mode)
{
	unsigned int dmask, amask;
	int prevreg = -1, lastreg = -1, first = 1;

	if (mode == Apdi) {
		int i;
		uae_u8 dmask2 = (mask >> 8) & 0xff;
		uae_u8 amask2 = mask & 0xff;
		dmask = 0;
		amask = 0;
		for (i = 0; i < 8; i++) {
			if (dmask2 & (1 << i))
				dmask |= 1 << (7 - i);
			if (amask2 & (1 << i))
				amask |= 1 << (7 - i);
		}
	} else {
		dmask = mask & 0xff;
		amask = (mask >> 8) & 0xff;
	}
	while (dmask) { addmovemreg (out, &prevreg, &lastreg, &first, movem_index1[dmask]); dmask = movem_next[dmask]; }
	while (amask) { addmovemreg (out, &prevreg, &lastreg, &first, movem_index1[amask] + 8); amask = movem_next[amask]; }
	addmovemreg (out, &prevreg, &lastreg, &first, -1);
}

static void disasm_size (TCHAR *instrname, struct instr *dp)
{
#if 0
	int i, size;
	uae_u16 mnemo = dp->mnemo;

	size = dp->size;
	for (i = 0; i < 65536; i++) {
		struct instr *in = &table68k[i];
		if (in->mnemo == mnemo && in != dp) {
			if (size != in->size)
				break;
		}
	}
	if (i == 65536)
		size = -1;
#endif
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
		_tcscat (instrname, _T("   "));
		break;
	}
}

void m68k_disasm_2 (TCHAR *buf, int bufsize, uaecptr pc, uaecptr *nextpc, int cnt, uae_u32 *seaddr, uae_u32 *deaddr, int safemode)
{
	uae_u32 seaddr2;
	uae_u32 deaddr2;

	if (buf)
		memset (buf, 0, bufsize * sizeof (TCHAR));
	if (!table68k)
		return;
	while (cnt-- > 0) {
		TCHAR instrname[100], *ccpt;
		int i;
		uae_u32 opcode;
		struct mnemolookup *lookup;
		struct instr *dp;
		int oldpc;
		uaecptr m68kpc_illg = 0;
		bool illegal = false;

		seaddr2 = deaddr2 = 0;
		oldpc = pc;
		opcode = get_word_debug (pc);
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

		buf = buf_out (buf, &bufsize, _T("%08lX "), pc);

		pc += 2;
		
		if (lookup->friendlyname)
			_tcscpy (instrname, lookup->friendlyname);
		else
			_tcscpy (instrname, lookup->name);
		ccpt = _tcsstr (instrname, _T("cc"));
		if (ccpt != 0) {
			_tcsncpy (ccpt, ccnames[dp->cc], 2);
		}
		disasm_size (instrname, dp);

		if (lookup->mnemo == i_MOVEC2 || lookup->mnemo == i_MOVE2C) {
			uae_u16 imm = get_word_debug (pc);
			uae_u16 creg = imm & 0x0fff;
			uae_u16 r = imm >> 12;
			TCHAR regs[16], *cname = _T("?");
			int i;
			for (i = 0; m2cregs[i].regname; i++) {
				if (m2cregs[i].regno == creg)
					break;
			}
			_stprintf (regs, _T("%c%d"), r >= 8 ? 'A' : 'D', r >= 8 ? r - 8 : r);
			if (m2cregs[i].regname)
				cname = m2cregs[i].regname;
			if (lookup->mnemo == i_MOVE2C) {
				_tcscat (instrname, regs);
				_tcscat (instrname, _T(","));
				_tcscat (instrname, cname);
			} else {
				_tcscat (instrname, cname);
				_tcscat (instrname, _T(","));
				_tcscat (instrname, regs);
			}
			pc += 2;
		} else if (lookup->mnemo == i_MVMEL) {
			uae_u16 mask = get_word_debug (pc);
			pc += 2;
			pc = ShowEA (0, pc, opcode, dp->dreg, dp->dmode, dp->size, instrname, deaddr, safemode);
			_tcscat (instrname, _T(","));
			movemout (instrname, mask, dp->dmode);
		} else if (lookup->mnemo == i_MVMLE) {
			uae_u16 mask = get_word_debug (pc);
			pc += 2;
			movemout (instrname, mask, dp->dmode);
			_tcscat (instrname, _T(","));
			pc = ShowEA (0, pc, opcode, dp->dreg, dp->dmode, dp->size, instrname, deaddr, safemode);
		} else {
			if (dp->suse) {
				pc = ShowEA (0, pc, opcode, dp->sreg, dp->smode, dp->size, instrname, &seaddr2, safemode);
			}
			if (dp->suse && dp->duse)
				_tcscat (instrname, _T(","));
			if (dp->duse) {
				pc = ShowEA (0, pc, opcode, dp->dreg, dp->dmode, dp->size, instrname, &deaddr2, safemode);
			}
		}

		for (i = 0; i < (pc - oldpc) / 2; i++) {
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
			if (cctrue (dp->cc))
				buf = buf_out (buf, &bufsize, _T(" == $%08x (T)"), addr2);
			else
				buf = buf_out (buf, &bufsize, _T(" == $%08x (F)"), addr2);
		} else if ((opcode & 0xff00) == 0x6100) { /* BSR */
			if (deaddr)
				*deaddr = pc;
			buf = buf_out (buf, &bufsize, _T(" == $%08x"), seaddr2);
		}
		buf = buf_out (buf, &bufsize, _T("\n"));

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

void m68k_disasm_ea (uaecptr addr, uaecptr *nextpc, int cnt, uae_u32 *seaddr, uae_u32 *deaddr)
{
	TCHAR *buf;

	buf = xmalloc (TCHAR, (MAX_LINEWIDTH + 1) * cnt);
	if (!buf)
		return;
	m68k_disasm_2 (buf, (MAX_LINEWIDTH + 1) * cnt, addr, nextpc, cnt, seaddr, deaddr, 1);
	xfree (buf);
}
void m68k_disasm (uaecptr addr, uaecptr *nextpc, int cnt)
{
	TCHAR *buf;

	buf = xmalloc (TCHAR, (MAX_LINEWIDTH + 1) * cnt);
	if (!buf)
		return;
	m68k_disasm_2 (buf, (MAX_LINEWIDTH + 1) * cnt, addr, nextpc, cnt, NULL, NULL, 0);
	console_out_f (_T("%s"), buf);
	xfree (buf);
}

/*************************************************************
Disasm the m68kcode at the given address into instrname
and instrcode
*************************************************************/
void sm68k_disasm (TCHAR *instrname, TCHAR *instrcode, uaecptr addr, uaecptr *nextpc)
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
	0, _T("SFC"),
	1, _T("DFC"),
	2, _T("CACR"),
	3, _T("TC"),
	4, _T("ITT0"),
	5, _T("ITT1"),
	6, _T("DTT0"),
	7, _T("DTT1"),
	8, _T("BUSC"),
	0x800, _T("USP"),
	0x801, _T("VBR"),
	0x802, _T("CAAR"),
	0x803, _T("MSP"),
	0x804, _T("ISP"),
	0x805, _T("MMUS"),
	0x806, _T("URP"),
	0x807, _T("SRP"),
	0x808, _T("PCR"),
	-1, NULL
};

void val_move2c2 (int regno, uae_u32 val)
{
	switch (regno) {
	case 0: regs.sfc = val; break;
	case 1: regs.dfc = val; break;
	case 2: regs.cacr = val; break;
	case 3: regs.tcr = val; break;
	case 4: regs.itt0 = val; break;
	case 5: regs.itt1 = val; break;
	case 6: regs.dtt0 = val; break;
	case 7: regs.dtt1 = val; break;
	case 8: regs.buscr = val; break;
	case 0x800: regs.usp = val; break;
	case 0x801: regs.vbr = val; break;
	case 0x802: regs.caar = val; break;
	case 0x803: regs.msp = val; break;
	case 0x804: regs.isp = val; break;
	case 0x805: regs.mmusr = val; break;
	case 0x806: regs.urp = val; break;
	case 0x807: regs.srp = val; break;
	case 0x808: regs.pcr = val; break;
	}
}

uae_u32 val_move2c (int regno)
{
	switch (regno) {
	case 0: return regs.sfc;
	case 1: return regs.dfc;
	case 2: return regs.cacr;
	case 3: return regs.tcr;
	case 4: return regs.itt0;
	case 5: return regs.itt1;
	case 6: return regs.dtt0;
	case 7: return regs.dtt1;
	case 8: return regs.buscr;
	case 0x800: return regs.usp;
	case 0x801: return regs.vbr;
	case 0x802: return regs.caar;
	case 0x803: return regs.msp;
	case 0x804: return regs.isp;
	case 0x805: return regs.mmusr;
	case 0x806: return regs.urp;
	case 0x807: return regs.srp;
	case 0x808: return regs.pcr;
	default: return 0;
	}
}

void m68k_dumpstate (uaecptr pc, uaecptr *nextpc)
{
	int i, j;

	for (i = 0; i < 8; i++){
		console_out_f (_T("  D%d %08lX "), i, m68k_dreg (regs, i));
		if ((i & 3) == 3) console_out_f (_T("\n"));
	}
	for (i = 0; i < 8; i++){
		console_out_f (_T("  A%d %08lX "), i, m68k_areg (regs, i));
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
		for (i = 0; i < 8; i++){
			console_out_f (_T("FP%d: %g "), i, regs.fp[i]);
			if ((i & 3) == 3)
				console_out_f (_T("\n"));
		}
		fpsr = get_fpsr ();
		console_out_f (_T("FPSR: %04X FPCR: %08x FPIAR: %08x N=%d Z=%d I=%d NAN=%d\n"),
			fpsr, regs.fpcr, regs.fpiar,
			(fpsr & 0x8000000) != 0,
			(fpsr & 0x4000000) != 0,
			(fpsr & 0x2000000) != 0,
			(fpsr & 0x1000000) != 0);
	}
#endif
	if (currprefs.cpu_compatible && currprefs.cpu_model == 68000) {
		struct instr *dp;
		struct mnemolookup *lookup1, *lookup2;
		dp = table68k + regs.irc;
		for (lookup1 = lookuptab; lookup1->mnemo != dp->mnemo; lookup1++);
		dp = table68k + regs.ir;
		for (lookup2 = lookuptab; lookup2->mnemo != dp->mnemo; lookup2++);
		console_out_f (_T("Prefetch %04x (%s) %04x (%s)\n"), regs.irc, lookup1->name, regs.ir, lookup2->name);
	}

	if (pc != 0xffffffff) {
		m68k_disasm (pc, nextpc, 1);
		if (nextpc)
			console_out_f (_T("Next PC: %08lx\n"), *nextpc);
	}
}
void m68k_dumpstate (uaecptr *nextpc)
{
	m68k_dumpstate (m68k_getpc (), nextpc);
}

#ifdef SAVESTATE

/* CPU save/restore code */

#define CPUTYPE_EC 1
#define CPUMODE_HALT 1

uae_u8 *restore_cpu (uae_u8 *src)
{
	int i, flags, model;
	uae_u32 l;

	currprefs.cpu_model = changed_prefs.cpu_model = model = restore_u32 ();
	flags = restore_u32 ();
	changed_prefs.address_space_24 = 0;
	if (flags & CPUTYPE_EC)
		changed_prefs.address_space_24 = 1;
	currprefs.address_space_24 = changed_prefs.address_space_24;
	currprefs.cpu_compatible = changed_prefs.cpu_compatible;
	currprefs.cpu_cycle_exact = changed_prefs.cpu_cycle_exact;
	currprefs.blitter_cycle_exact = changed_prefs.blitter_cycle_exact;
	currprefs.cpu_frequency = changed_prefs.cpu_frequency = 0;
	currprefs.cpu_clock_multiplier = changed_prefs.cpu_clock_multiplier = 0;
	for (i = 0; i < 15; i++)
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
	set_cpu_caches ();
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
				// 2.7.0 new
				for (int i = 0; i < CPU_PIPELINE_MAX; i++)
					regs.prefetch020[i] = restore_u32 ();
			} else {
				for (int i = 0; i < CPU_PIPELINE_MAX; i++)
					regs.prefetch020[i] = restore_u16 ();
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
			for (int i = 0; i < CPU_PIPELINE_MAX; i++)
				regs.prefetch020[i] = restore_u32 ();
		}
		if (model >= 68020) {
			regs.ce020memcycles = restore_u32 ();
			restore_u32 ();
		}
	}

	write_log (_T("CPU: %d%s%03d, PC=%08X\n"),
		model / 1000, flags & 1 ? _T("EC") : _T(""), model % 1000, regs.pc);

	return src;
}

void restore_cpu_finish (void)
{
	init_m68k ();
	m68k_setpc (regs.pc);
	doint ();
	fill_prefetch ();
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
		dstbak = dst = xmalloc (uae_u8, 1000);

	save_u32 (2 | 4 | 8);
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
		for (int i = 0; i < CPU_PIPELINE_MAX; i++)
			save_u16 (cputrace.prefetch020[i]);
		for (int i = 0; i < CPU_PIPELINE_MAX; i++)
			save_u32 (cputrace.prefetch020[i]);
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
			for (int i = 0; i < CPU_PIPELINE_MAX; i++)
				cputrace.prefetch020[i] = restore_u16 ();
			if (v & 8) {
				for (int i = 0; i < CPU_PIPELINE_MAX; i++)
					cputrace.prefetch020[i] = restore_u32 ();
			}
		}
	}

	cputrace.needendcycles = 1;
	if (v && cputrace.state) {
		if (currprefs.cpu_model > 68000) {
			if (v & 4)
				cpu_tracer = -1;
			// old format?
			if ((v & (4 | 8)) != (4 | 8))
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
	int model, i, khz;

	if (dstptr)
		dstbak = dst = dstptr;
	else
		dstbak = dst = xmalloc (uae_u8, 1000);
	model = currprefs.cpu_model;
	save_u32 (model);					/* MODEL */
	save_u32 (0x80000000 | 0x40000000 | 0x20000000 | (currprefs.address_space_24 ? 1 : 0)); /* FLAGS */
	for (i = 0;i < 15; i++)
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
			save_u32 (regs.prefetch020[i]);
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
	}
	if (currprefs.cpu_model >= 68020) {
		save_u32 (regs.ce020memcycles);
		save_u32 (0);
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

static void exception3f (uae_u32 opcode, uaecptr addr, int writeaccess, int instructionaccess, uae_u32 pc)
{
	if (currprefs.cpu_model >= 68040)
		addr &= ~1;
	if (currprefs.cpu_model >= 68020) {
		if (pc == 0xffffffff)
			last_addr_for_exception_3 = regs.instruction_pc;
		else
			last_addr_for_exception_3 = pc;
	} else if (pc == 0xffffffff) {
		last_addr_for_exception_3 = m68k_getpc () + 2;
	} else {
		last_addr_for_exception_3 = pc;
	}
	last_fault_for_exception_3 = addr;
	last_op_for_exception_3 = opcode;
	last_writeaccess_for_exception_3 = writeaccess;
	last_instructionaccess_for_exception_3 = instructionaccess;
	Exception (3);
#if EXCEPTION3_DEBUGGER
	activate_debugger();
#endif
}

void exception3 (uae_u32 opcode, uaecptr addr)
{
	exception3f (opcode, addr, 0, 0, 0xffffffff);
}
void exception3i (uae_u32 opcode, uaecptr addr)
{
	exception3f (opcode, addr, 0, 1, 0xffffffff);
}
void exception3 (uae_u32 opcode, uaecptr addr, bool w, bool i, uaecptr pc)
{
	exception3f (opcode, addr, w, i, pc);
}

void exception2 (uaecptr addr)
{
	write_log (_T("delayed exception2!\n"));
	regs.panic_pc = m68k_getpc ();
	regs.panic_addr = addr;
	regs.panic = 6;
	set_special (SPCFLAG_BRK);
	m68k_setpc (0xf80000);
#ifdef JIT
	set_special (SPCFLAG_END_COMPILE);
#endif
	fill_prefetch ();
}


void cpureset (void)
{
    /* RESET hasn't increased PC yet, 1 word offset */
	uaecptr pc;
	uaecptr ksboot = 0xf80002 - 2;
	uae_u16 ins;
	addrbank *ab;

	send_internalevent (INTERNALEVENT_CPURESET);
	if ((currprefs.cpu_compatible || currprefs.cpu_cycle_exact) && currprefs.cpu_model <= 68020) {
		custom_reset (false, false);
		return;
	}
	pc = m68k_getpc () + 2;
	ab = &get_mem_bank (pc);
	if (ab->check (pc, 2)) {
		write_log (_T("CPU reset PC=%x (%s)..\n"), pc - 2, ab->name);
		ins = get_word (pc);
		custom_reset (false, false);
		// did memory disappear under us?
		if (ab == &get_mem_bank (pc))
			return;
		// it did
		if ((ins & ~7) == 0x4ed0) {
			int reg = ins & 7;
			uae_u32 addr = m68k_areg (regs, reg);
			if (addr < 0x80000)
				addr += 0xf80000;
			write_log (_T("reset/jmp (ax) combination emulated -> %x\n"), addr);
			m68k_setpc (addr - 2);
			return;
		}
	}
	// the best we can do, jump directly to ROM entrypoint
	// (which is probably what program wanted anyway)
	write_log (_T("CPU Reset PC=%x (%s), invalid memory -> %x.\n"), pc, ab->name, ksboot + 2);
	custom_reset (false, false);
	m68k_setpc (ksboot);
}


void m68k_setstopped (void)
{
	regs.stopped = 1;
	/* A traced STOP instruction drops through immediately without
	actually stopping.  */
	if ((regs.spcflags & SPCFLAG_DOTRACE) == 0)
		set_special (SPCFLAG_STOP);
	else
		m68k_resumestopped ();
}

void m68k_resumestopped (void)
{
	if (!regs.stopped)
		return;
	regs.stopped = 0;
	if (currprefs.cpu_cycle_exact) {
		if (currprefs.cpu_model == 68000)
			x_do_cycles (6 * cpucycleunit);
	}
	fill_prefetch ();
	unset_special (SPCFLAG_STOP);
}

/*
* Compute exact number of CPU cycles taken
* by DIVU and DIVS on a 68000 processor.
*
* Copyright (c) 2005 by Jorge Cwik, pasti@fxatari.com
*
* This is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This software is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this software; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
*/


/*

The routines below take dividend and divisor as parameters.
They return 0 if division by zero, or exact number of cycles otherwise.

The number of cycles returned assumes a register operand.
Effective address time must be added if memory operand.

For 68000 only (not 68010, 68012, 68020, etc).
Probably valid for 68008 after adding the extra prefetch cycle.


Best and worst cases for register operand:
(Note the difference with the documented range.)


DIVU:

Overflow (always): 10 cycles.
Worst case: 136 cycles.
Best case: 76 cycles.


DIVS:

Absolute overflow: 16-18 cycles.
Signed overflow is not detected prematurely.

Worst case: 156 cycles.
Best case without signed overflow: 122 cycles.
Best case with signed overflow: 120 cycles


*/

int getDivu68kCycles (uae_u32 dividend, uae_u16 divisor)
{
	int mcycles;
	uae_u32 hdivisor;
	int i;

	if (divisor == 0)
		return 0;

	// Overflow
	if ((dividend >> 16) >= divisor)
		return (mcycles = 5) * 2;

	mcycles = 38;
	hdivisor = divisor << 16;

	for (i = 0; i < 15; i++) {
		uae_u32 temp;
		temp = dividend;

		dividend <<= 1;

		// If carry from shift
		if ((uae_s32)temp < 0)
			dividend -= hdivisor;
		else {
			mcycles += 2;
			if (dividend >= hdivisor) {
				dividend -= hdivisor;
				mcycles--;
			}
		}
	}
	return mcycles * 2;
}

int getDivs68kCycles (uae_s32 dividend, uae_s16 divisor)
{
	int mcycles;
	uae_u32 aquot;
	int i;

	if (divisor == 0)
		return 0;

	mcycles = 6;

	if (dividend < 0)
		mcycles++;

	// Check for absolute overflow
	if (((uae_u32)abs (dividend) >> 16) >= (uae_u16)abs (divisor))
		return (mcycles + 2) * 2;

	// Absolute quotient
	aquot = (uae_u32) abs (dividend) / (uae_u16)abs (divisor);

	mcycles += 55;

	if (divisor >= 0) {
		if (dividend >= 0)
			mcycles--;
		else
			mcycles++;
	}

	// Count 15 msbits in absolute of quotient

	for (i = 0; i < 15; i++) {
		if ((uae_s16)aquot >= 0)
			mcycles++;
		aquot <<= 1;
	}

	return mcycles * 2;
}

/* 68000 Z=1. NVC=0
 * 68020 and 68030: Signed: Z=1 NVC=0. Unsigned: V=1, N<dst, Z=!N, C=0.
 * 68040/68060 C=0.
 */
void divbyzero_special (bool issigned, uae_s32 dst)
{
	if (currprefs.cpu_model == 68020 || currprefs.cpu_model == 68030) {
		CLEAR_CZNV ();
		if (issigned == false) {
			if (dst < 0) 
				SET_NFLG (1);
			SET_ZFLG (!GET_NFLG ());
			SET_VFLG (1);
		} else {
			SET_ZFLG (1);
		}
	} else if (currprefs.cpu_model >= 68040) {
		SET_CFLG (0);
	} else {
		// 68000/010
		CLEAR_CZNV ();
	}
}

STATIC_INLINE void fill_cache040 (uae_u32 addr)
{
	int index, i, lws;
	uae_u32 tag;
	uae_u32 data;
	struct cache040 *c;
	static int linecnt;

	addr &= ~15;
	index = (addr >> 4) & (CACHESETS040 - 1);
	tag = regs.s | (addr & ~((CACHESETS040 << 4) - 1));
	lws = (addr >> 2) & 3;
	c = &caches040[index];
	for (i = 0; i < CACHELINES040; i++) {
		if (c->valid[i] && c->tag[i] == tag) {
			// cache hit
			regs.cacheholdingaddr020 = addr;
			regs.cacheholdingdata020 = c->data[i][lws];
			return;
		}
	}
	// cache miss
	data = mem_access_delay_longi_read_ce020 (addr);
	int line = linecnt;
	for (i = 0; i < CACHELINES040; i++) {
		int line = (linecnt + i) & (CACHELINES040 - 1);
		if (c->tag[i] != tag || c->valid[i] == false) {
			c->tag[i] = tag;
			c->valid[i] = true;
			c->data[i][0] = data;
		}
	}
	regs.cacheholdingaddr020 = addr;
	regs.cacheholdingdata020 = data;
}

// this one is really simple and easy
static void fill_icache020 (uae_u32 addr, uae_u32 (*fetch)(uaecptr))
{
	int index;
	uae_u32 tag;
	uae_u32 data;
	struct cache020 *c;

	addr &= ~3;
	index = (addr >> 2) & (CACHELINES020 - 1);
	tag = regs.s | (addr & ~((CACHELINES020 << 2) - 1));
	c = &caches020[index];
	if (c->valid && c->tag == tag) {
		// cache hit
		regs.cacheholdingaddr020 = addr;
		regs.cacheholdingdata020 = c->data;
		return;
	}
	// cache miss
	// Prefetch apparently can be queued by bus controller
	// even if bus controller is currently processing
	// previous data access.
	// Other combinations are not possible.
	if (regs.ce020memcycles < 0)
		regs.ce020memcycles = 0;
	unsigned long cycs = get_cycles ();
	data = fetch (addr);
	// negative cycles = prefetch
	// ce020memcycles was guaranteed zero or positive.
	cycs = get_cycles () - cycs;
	regs.ce020memcycles = -regs.ce020memcycles - cycs;
	if (!(regs.cacr & 2)) {
		c->tag = tag;
		c->valid = !!(regs.cacr & 1);
		c->data = data;
	}
	regs.cacheholdingaddr020 = addr;
	regs.cacheholdingdata020 = data;
}

uae_u32 get_word_ce020_prefetch (int o)
{
	uae_u32 pc = m68k_getpc () + o;
	uae_u32 v;

	if (pc & 2) {
		v = regs.prefetch020[0] & 0xffff;
		regs.prefetch020[0] = regs.prefetch020[1];
		fill_icache020 (pc + 2 + 4, mem_access_delay_longi_read_ce020);
		regs.prefetch020[1] = regs.cacheholdingdata020;
		return v;
	}
	v = regs.prefetch020[0] >> 16;
	return v;
}

uae_u32 get_word_020_prefetch (int o)
{
	uae_u32 pc = m68k_getpc () + o;
	uae_u32 v;

	if (pc & 2) {
		v = regs.prefetch020[0] & 0xffff;
		regs.prefetch020[0] = regs.prefetch020[1];
		fill_icache020 (pc + 2 + 4, get_longi);
		regs.prefetch020[1] = regs.cacheholdingdata020;
		return v;
	}
	v = regs.prefetch020[0] >> 16;
	return v;
}

void usecycles_ce020 (int cycles)
{
	if (regs.ce020memcycles < 0) {
		if (regs.ce020memcycles <= -cycles) {
			regs.ce020memcycles += cycles;
		} else {
			cycles -= -regs.ce020memcycles;
			regs.ce020memcycles = 0;
			x_do_cycles (cycles);
		}
	} else {
		if (regs.ce020memcycles >= cycles) {
			regs.ce020memcycles -= cycles;
		} else {
			cycles -= regs.ce020memcycles;
			regs.ce020memcycles = 0;
			x_do_cycles (cycles);
		}
	}
}

#define RESET_CE020_CYCLES \
	resetcycles_ce020 ()
#define STORE_CE020_CYCLES \
	unsigned long cycs = get_cycles ()
#define ADD_CE020_CYCLES \
	regs.ce020memcycles += get_cycles () - cycs

uae_u32 mem_access_delay_long_read_ce020 (uaecptr addr)
{
	uae_u32 v;
	RESET_CE020_CYCLES;
	STORE_CE020_CYCLES;
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
	ADD_CE020_CYCLES;
	return v;
}

uae_u32 mem_access_delay_longi_read_ce020 (uaecptr addr)
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

uae_u32 mem_access_delay_word_read_ce020 (uaecptr addr)
{
	uae_u32 v;
	RESET_CE020_CYCLES;
	STORE_CE020_CYCLES;
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
	ADD_CE020_CYCLES;
	return v;
}

uae_u32 mem_access_delay_byte_read_ce020 (uaecptr addr)
{
	uae_u32 v;
	RESET_CE020_CYCLES;
	STORE_CE020_CYCLES;
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
	ADD_CE020_CYCLES;
	return v;
}

void mem_access_delay_byte_write_ce020 (uaecptr addr, uae_u32 v)
{
	RESET_CE020_CYCLES;
	STORE_CE020_CYCLES;
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
	ADD_CE020_CYCLES;
}

void mem_access_delay_word_write_ce020 (uaecptr addr, uae_u32 v)
{
	RESET_CE020_CYCLES;
	STORE_CE020_CYCLES;
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
	ADD_CE020_CYCLES;
}

void mem_access_delay_long_write_ce020 (uaecptr addr, uae_u32 v)
{
	RESET_CE020_CYCLES;
	STORE_CE020_CYCLES;
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
	ADD_CE020_CYCLES;
}


// 68030 caches aren't so simple as 68020 cache..
STATIC_INLINE struct cache030 *getcache030 (struct cache030 *cp, uaecptr addr, uae_u32 *tagp, int *lwsp)
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

STATIC_INLINE void update_cache030 (struct cache030 *c, uae_u32 val, uae_u32 tag, int lws)
{
	if (c->tag != tag)
		c->valid[0] = c->valid[1] = c->valid[2] = c->valid[3] = false;
	c->tag = tag;
	c->valid[lws] = true;
	c->data[lws] = val;
}

STATIC_INLINE void fill_icache030 (uae_u32 addr)
{
	int lws;
	uae_u32 tag;
	uae_u32 data;
	struct cache030 *c;

	addr &= ~3;
	c = getcache030 (icaches030, addr, &tag, &lws);
	if (c->valid[lws] && c->tag == tag) {
		// cache hit
		regs.cacheholdingaddr020 = addr;
		regs.cacheholdingdata020 = c->data[lws];
		return;
	}
	// cache miss
	if (currprefs.cpu_cycle_exact) {
		if (regs.ce020memcycles < 0)
			regs.ce020memcycles = 0;
		unsigned long cycs = get_cycles ();
		data = mem_access_delay_longi_read_ce020 (addr);
		cycs = get_cycles () - cycs;
		regs.ce020memcycles = -regs.ce020memcycles - cycs;
	} else {
		data = get_longi (addr);
	}
	if ((regs.cacr & 3) == 1) { // not frozen and enabled
		update_cache030 (c, data, tag, lws);
#if 0
		if ((regs.cacr & 0x11) == 0x11 && lws == 0 && !c->valid[0] && !c->valid[1] && !c->valid[2] && !c->valid[3] && ce_banktype[addr >> 16] == CE_MEMBANK_FAST) {
			// do burst fetch if cache enabled, not frozen, all slots invalid, no chip ram
			c->data[1] = mem_access_delay_long_read_ce020 (addr + 4);
			c->data[2] = mem_access_delay_long_read_ce020 (addr + 8);
			c->data[3] = mem_access_delay_long_read_ce020 (addr + 12);
			c->valid[1] = c->valid[2] = c->valid[3] = true;
		}
#endif
	}
	regs.cacheholdingaddr020 = addr;
	regs.cacheholdingdata020 = data;
}

STATIC_INLINE bool cancache030 (uaecptr addr)
{
	return ce_cachable[addr >> 16] != 0;
}

// and finally the worst part, 68030 data cache..
void write_dcache030 (uaecptr addr, uae_u32 val, int size)
{
	struct cache030 *c1, *c2;
	int lws1, lws2;
	uae_u32 tag1, tag2;
	int aligned = addr & 3;

	if (!(regs.cacr & 0x100) || currprefs.cpu_model == 68040) // data cache disabled? 68040 shares this too.
		return;
	if (!cancache030 (addr))
		return;

	c1 = getcache030 (dcaches030, addr, &tag1, &lws1);
	if (!(regs.cacr & 0x2000)) { // write allocate
		if (c1->tag != tag1 || c1->valid[lws1] == false)
			return;
	}

#if 0
	uaecptr a = 0x1db0c;
	if (addr - (1 << size) + 1 <= a && addr + (1 << size) >= a) {
		write_log (_T("%08x %d %d %08x %08x %d\n"), addr, aligned, size, val, tag1, lws1);
		if (aligned == 2)
			write_log (_T("*\n"));
	}
#endif

	// easy one
	if (size == 2 && aligned == 0) {
		update_cache030 (c1, val, tag1, lws1);
#if 0
		if ((regs.cacr & 0x1100) == 0x1100 && lws1 == 0 && !c1->valid[0] && !c1->valid[1] && !c1->valid[2] && !c1->valid[3] && ce_banktype[addr >> 16] == CE_MEMBANK_FAST) {
			// do burst fetch if cache enabled, not frozen, all slots invalid, no chip ram
			c1->data[1] = mem_access_delay_long_read_ce020 (addr + 4);
			c1->data[2] = mem_access_delay_long_read_ce020 (addr + 8);
			c1->data[3] = mem_access_delay_long_read_ce020 (addr + 12);
			c1->valid[1] = c1->valid[2] = c1->valid[3] = true;
		}
#endif
		return;
	}
	// argh!! merge partial write
	c2 = getcache030 (dcaches030, addr + 4, &tag2, &lws2);
	if (size == 2) {
		if (c1->valid[lws1] && c1->tag == tag1) {
			c1->data[lws1] &= ~(0xffffffff >> (aligned * 8));
			c1->data[lws1] |= val >> (aligned * 8);
		}
		if (c2->valid[lws2] && c2->tag == tag2) {
			c2->data[lws2] &= 0xffffffff >> ((4 - aligned) * 8);
			c2->data[lws2] |= val << ((4 - aligned) * 8);
		}
	} else if (size == 1) {
		val <<= 16;
		if (c1->valid[lws1] && c1->tag == tag1) {
			c1->data[lws1] &= ~(0xffff0000 >> (aligned * 8));
			c1->data[lws1] |= val >> (aligned * 8);
		}
		if (c2->valid[lws2] && c2->tag == tag2 && aligned == 3) {
			c2->data[lws2] &= 0x00ffffff;
			c2->data[lws2] |= val << 8;
		}
	} else if (size == 0) {
		val <<= 24;
		if (c1->valid[lws1] && c1->tag == tag1) {
			c1->data[lws1] &= ~(0xff000000 >> (aligned * 8));
			c1->data[lws1] |= val >> (aligned * 8);
		}
	}
}

uae_u32 read_dcache030 (uaecptr addr, int size)
{
	struct cache030 *c1, *c2;
	int lws1, lws2;
	uae_u32 tag1, tag2;
	int aligned = addr & 3;
	int len = (1 << size) * 8;
	uae_u32 v1, v2;

	if (!(regs.cacr & 0x100) || currprefs.cpu_model == 68040 || !cancache030 (addr)) { // data cache disabled? shared with 68040 "ce"
		if (currprefs.cpu_cycle_exact) {
			if (size == 2)
				return mem_access_delay_long_read_ce020 (addr);
			else if (size == 1)
				return mem_access_delay_word_read_ce020 (addr);
			else
				return mem_access_delay_byte_read_ce020 (addr);
		} else {
			if (size == 2)
				return get_long (addr);
			else if (size == 1)
				return get_word (addr);
			else
				return get_byte (addr);
		}
	}

	c1 = getcache030 (dcaches030, addr, &tag1, &lws1);
	addr &= ~3;
	if (!c1->valid[lws1] || c1->tag != tag1) {
		v1 = currprefs.cpu_cycle_exact ? mem_access_delay_long_read_ce020 (addr) : get_long (addr);
		update_cache030 (c1, v1, tag1, lws1);
	} else {
		v1 = c1->data[lws1];
		if (get_long (addr) != v1) {
			write_log (_T("data cache mismatch %d %d %08x %08x != %08x %08x %d PC=%08x\n"),
				size, aligned, addr, get_long (addr), v1, tag1, lws1, M68K_GETPC);
			v1 = get_long (addr);
		}
	}
	// only one long fetch needed?
	if (size == 0) {
		v1 >>= (3 - aligned) * 8;
		return v1;
	} else if (size == 1 && aligned <= 2) {
		v1 >>= (2 - aligned) * 8;
		return v1;
	} else if (size == 2 && aligned == 0) {
		return v1;
	}
	// need two longs
	addr += 4;
	c2 = getcache030 (dcaches030, addr, &tag2, &lws2);
	if (!c2->valid[lws2] || c2->tag != tag2) {
		v2 = currprefs.cpu_cycle_exact ? mem_access_delay_long_read_ce020 (addr) : get_long (addr);
		update_cache030 (c2, v2, tag2, lws2);
	} else {
		v2 = c2->data[lws2];
		if (get_long (addr) != v2) {
			write_log (_T("data cache mismatch %d %d %08x %08x != %08x %08x %d PC=%08x\n"),
				size, aligned, addr, get_long (addr), v2, tag2, lws2, M68K_GETPC);
			v2 = get_long (addr);
		}
	}
	if (size == 1 && aligned == 3)
		return (v1 << 8) | (v2 >> 24);
	else if (size == 2 && aligned == 1)
		return (v1 << 8) | (v2 >> 24);
	else if (size == 2 && aligned == 2)
		return (v1 << 16) | (v2 >> 16);
	else if (size == 2 && aligned == 3)
		return (v1 << 24) | (v2 >> 8);

	write_log (_T("dcache030 weirdness!?\n"));
	return 0;
}

uae_u32 get_word_ce030_prefetch (int o)
{
	uae_u32 pc = m68k_getpc () + o;
	uae_u32 v;

	if (pc & 2) {
		v = regs.prefetch020[0] & 0xffff;
		regs.prefetch020[0] = regs.prefetch020[1];
		fill_icache030 (pc + 2 + 4);
		regs.prefetch020[1] = regs.cacheholdingdata020;
		return v;
	}
	v = regs.prefetch020[0] >> 16;
	return v;
}

STATIC_INLINE void fill_icache040 (uae_u32 addr)
{
	uae_u32 data;
	addr &= ~3;
	if (currprefs.cpu_cycle_exact)
		data = mem_access_delay_longi_read_ce020 (addr);
	else
		data = get_longi (addr);
	regs.cacheholdingaddr020 = addr;
	regs.cacheholdingdata020 = data;
}

uae_u32 get_word_ce040_prefetch (int o)
{
	uae_u32 pc = m68k_getpc () + o;
	uae_u32 v;

	if (pc & 2) {
		v = regs.prefetch020[0] & 0xffff;
		regs.prefetch020[0] = regs.prefetch020[1];
		regs.prefetch020[1] = regs.prefetch020[2];
		regs.prefetch020[2] = regs.prefetch020[3];
		fill_icache040 (pc + 2 + 4);
		regs.prefetch020[1] = regs.cacheholdingdata020;
		fill_icache040 (pc + 2 + 4 + 4);
		regs.prefetch020[2] = regs.cacheholdingdata020;
		fill_icache040 (pc + 2 + 4 + 4 + 4);
		regs.prefetch020[3] = regs.cacheholdingdata020;
		return v;
	}
	v = regs.prefetch020[0] >> 16;
	return v;
}

void flush_dcache (uaecptr addr, int size)
{
	if (!currprefs.cpu_cycle_exact)
		return;
	if (currprefs.cpu_model >= 68030) {
		for (int i = 0; i < CACHELINES030; i++) {
			dcaches030[i].valid[0] = 0;
			dcaches030[i].valid[1] = 0;
			dcaches030[i].valid[2] = 0;
			dcaches030[i].valid[3] = 0;
		}
	}
}

void fill_prefetch_040 (void)
{
	uaecptr pc = m68k_getpc ();
	pc &= ~3;
	fill_icache040 (pc);
	regs.prefetch020[0] = regs.cacheholdingdata020;
	fill_icache040 (pc + 4);
	regs.prefetch020[1] = regs.cacheholdingdata020;
	fill_icache040 (pc + 4 + 4);
	regs.prefetch020[2] = regs.cacheholdingdata020;
	fill_icache040 (pc + 4 + 4  +4);
	regs.prefetch020[3] = regs.cacheholdingdata020;
}

void fill_prefetch_030 (void)
{
	uaecptr pc = m68k_getpc ();
	pc &= ~3;
	fill_icache030 (pc);
	regs.prefetch020[0] = regs.cacheholdingdata020;
	fill_icache030 (pc + 4);
	regs.prefetch020[1] = regs.cacheholdingdata020;
}

void fill_prefetch_020 (void)
{
	uaecptr pc = m68k_getpc ();
	uae_u32 (*fetch)(uaecptr) = currprefs.cpu_cycle_exact ? mem_access_delay_longi_read_ce020 : get_longi;
	pc &= ~3;
	fill_icache020 (pc, fetch);
	regs.prefetch020[0] = regs.cacheholdingdata020;
	fill_icache020 (pc + 4, fetch);
	regs.prefetch020[1] = regs.cacheholdingdata020;
}

void fill_prefetch_0x0 (void)
{
	if (currprefs.cpu_model >= 68040)
		fill_prefetch_040 ();
	else
		fill_prefetch_030 ();
}

void fill_prefetch (void)
{
	uaecptr pc = m68k_getpc ();
	if (currprefs.cpu_model == 68020) {
		fill_prefetch_020 ();
	} else if (currprefs.cpu_model >= 68030) {
		fill_prefetch_030 ();
	} else if (currprefs.cpu_model >= 68040) {
		fill_prefetch_040 ();
	} else if (currprefs.cpu_model <= 68010) {
		regs.ir = x_get_word (pc);
		regs.irc = x_get_word (pc + 2);
	}
}
