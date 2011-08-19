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
#define EXCEPTION3_DEBUG 0

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "events.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "cpummu.h"
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

static uae_u64 srp_030, crp_030;
static uae_u32 tt0_030, tt1_030, tc_030;
static uae_u16 mmusr_030;

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

	write_log (L"Writing instruction count file...\n");
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
void (*x_do_cycles)(unsigned long);
void (*x_do_cycles_pre)(unsigned long);
void (*x_do_cycles_post)(unsigned long, uae_u32);

static struct cputracestruct cputrace;

STATIC_INLINE void clear_trace (void)
{
	struct cputracememory *ctm = &cputrace.ctm[cputrace.memoryoffset++];
	ctm->mode = 0;
	cputrace.cyclecounter = 0;
	cputrace.cyclecounter_pre = cputrace.cyclecounter_post = 0;
}
static void set_trace (uaecptr addr, int accessmode, int size)
{
	struct cputracememory *ctm = &cputrace.ctm[cputrace.memoryoffset++];
	ctm->addr = addr;
	ctm->data = 0xdeadf00d;
	ctm->mode = accessmode | (size << 4);
	cputrace.cyclecounter_pre = -1;
	if (accessmode == 1)
		cputrace.writecounter++;
	else
		cputrace.readcounter++;
}
static void add_trace (uaecptr addr, uae_u32 val, int accessmode, int size)
{
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
	cputrace.cyclecounter_pre = cputrace.cyclecounter_post = 0;
}


static void check_trace2 (void)
{
	if (cputrace.readcounter || cputrace.writecounter ||
		cputrace.cyclecounter || cputrace.cyclecounter_pre || cputrace.cyclecounter_post)
		write_log (L"CPU tracer invalid state during playback!\n");
}

static bool check_trace (void)
{
	if (!cpu_tracer)
		return true;
	if (!cputrace.readcounter && !cputrace.writecounter && !cputrace.cyclecounter) {
		if (cpu_tracer != -2) {
			write_log (L"CPU trace: dma_cycle() enabled. %08x %08x NOW=%08X\n",
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
	write_log (L"CPU tracer playback complete. STARTCYCLES=%08x NOWCYCLES=%08x\n", cputrace.startcycles, get_cycles ());
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
			write_log (L"CPU trace: GET %d: PC=%08x %08x=%08x %d %d %08x/%08x/%08x %d/%d (%08X)\n",
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
	gui_message (L"CPU trace: GET %08x %d %d NOT FOUND!\n", addr, accessmode, size);
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
		write_log (L"cputracefunc2_x_put_long %d <> %d\n", v, val);
}
static void cputracefunc2_x_put_word (uaecptr o, uae_u32 val)
{
	uae_u32 v;
	if (get_trace (o, 1, 2, &v)) {
		x2_put_word (o, val);
		check_trace2 ();
	}
	if (v != val)
		write_log (L"cputracefunc2_x_put_word %d <> %d\n", v, val);
}
static void cputracefunc2_x_put_byte (uaecptr o, uae_u32 val)
{
	uae_u32 v;
	if (get_trace (o, 1, 1, &v)) {
		x2_put_byte (o, val);
		check_trace2 ();
	}
	if (v != val)
		write_log (L"cputracefunc2_x_put_byte %d <> %d\n", v, val);
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

// indirect memory access functions
static void set_x_funcs (void)
{
	if (currprefs.mmu_model) {
		x_prefetch = get_iword_mmu;
		x_prefetch_long = get_ilong_mmu;
		x_get_ilong = get_ilong_mmu;
		x_get_iword = get_iword_mmu;
		x_get_ibyte = get_ibyte_mmu;
		x_next_iword = next_iword_mmu;
		x_next_ilong = next_ilong_mmu;
		x_put_long = put_long_mmu;
		x_put_word = put_word_mmu;
		x_put_byte = put_byte_mmu;
		x_get_long = get_long_mmu;
		x_get_word = get_word_mmu;
		x_get_byte = get_byte_mmu;
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
		x_do_cycles = do_cycles_ce;
		x_do_cycles_pre = do_cycles_ce;
		x_do_cycles_post = do_cycles_ce_post;
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
		x_do_cycles = do_cycles_ce;
		x_do_cycles_pre = do_cycles_ce;
		x_do_cycles_post = do_cycles_ce_post;
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
			write_log (L"CPU tracer enabled\n");
	}
	if (old > 0 && state == false) {
		set_x_funcs ();
		write_log (L"CPU tracer disabled\n");
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

static unsigned long REGPARAM2 op_illg_1 (uae_u32 opcode)
{
	op_illg (opcode);
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
			tbl = op_smalltbl_21_ff;
		if (currprefs.mmu_model)
			tbl = op_smalltbl_31_ff;
		break;
	case 68040:
		lvl = 4;
		tbl = op_smalltbl_1_ff;
		if (currprefs.cpu_cycle_exact)
			tbl = op_smalltbl_22_ff;
		if (currprefs.mmu_model)
			tbl = op_smalltbl_31_ff;
		break;
	case 68030:
		lvl = 3;
		tbl = op_smalltbl_2_ff;
		if (currprefs.cpu_cycle_exact)
			tbl = op_smalltbl_23_ff;
		break;
	case 68020:
		lvl = 2;
		tbl = op_smalltbl_3_ff;
		if (currprefs.cpu_cycle_exact)
			tbl = op_smalltbl_20_ff;
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
		write_log (L"no CPU emulation cores available CPU=%d!", currprefs.cpu_model);
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

		if (table68k[opcode].mnemo == i_ILLG)
			continue;
		if (currprefs.fpu_model && currprefs.cpu_model < 68020) {
			/* more hack fpu to 68000/68010 mode */
			if (table68k[opcode].clev > lvl && (opcode & 0xfe00) != 0xf200)
				continue;
		} else if (table68k[opcode].clev > lvl) {
			continue;
		}

		if (table68k[opcode].handler != -1) {
			int idx = table68k[opcode].handler;
			f = cpufunctbl[idx];
			if (f == op_illg_1)
				abort ();
			cpufunctbl[opcode] = f;
			opcnt++;
		}
	}
	write_log (L"Building CPU, %d opcodes (%d %d %d)\n",
		opcnt, lvl,
		currprefs.cpu_cycle_exact ? -1 : currprefs.cpu_compatible ? 1 : 0, currprefs.address_space_24);
	write_log (L"CPU=%d, FPU=%d, MMU=%d, JIT%s=%d.\n",
		currprefs.cpu_model, currprefs.fpu_model,
		currprefs.mmu_model,
		currprefs.cachesize ? (currprefs.compfpu ? L"=CPU/FPU" : L"=CPU") : L"",
		currprefs.cachesize);
#ifdef JIT
	build_comp ();
#endif
	set_cpu_caches ();
	if (currprefs.mmu_model) {
		mmu_reset ();
		mmu_set_tc (regs.tcr);
		mmu_set_super (regs.s != 0);
	}
}

void fill_prefetch (void)
{
	if (currprefs.mmu_model)
		return;
	regs.ir = x_get_word (m68k_getpc ());
	regs.irc = x_get_word (m68k_getpc () + 2);
}
static void fill_prefetch_quick (void)
{
	regs.ir = get_word (m68k_getpc ());
	regs.irc = get_word (m68k_getpc () + 2);
}

static unsigned long cycles_mask, cycles_val;

static void update_68k_cycles (void)
{
	cycles_mask = 0;
	cycles_val = currprefs.m68k_speed;
	if (currprefs.m68k_speed < 1) {
		cycles_mask = 0xFFFFFFFF;
		cycles_val = 0;
	}
	currprefs.cpu_clock_multiplier = changed_prefs.cpu_clock_multiplier;
	currprefs.cpu_frequency = changed_prefs.cpu_frequency;

	baseclock = currprefs.ntscmode ? 28636360 : 28375160;
	cpucycleunit = CYCLE_UNIT / 2;
	if (currprefs.cpu_clock_multiplier) {
		if (currprefs.cpu_clock_multiplier >= 256) {
			cpucycleunit = CYCLE_UNIT / (currprefs.cpu_clock_multiplier >> 8);
		} else {
			cpucycleunit = CYCLE_UNIT * currprefs.cpu_clock_multiplier;
		}
	} else if (currprefs.cpu_frequency) {
		cpucycleunit = CYCLE_UNIT * baseclock / currprefs.cpu_frequency;
	}
	if (cpucycleunit < 1)
		cpucycleunit = 1;
	if (currprefs.cpu_cycle_exact)
		write_log (L"CPU cycleunit: %d (%.3f)\n", cpucycleunit, (float)cpucycleunit / CYCLE_UNIT);
	config_changed = 1;
}

static void prefs_changed_cpu (void)
{
	fixup_cpu (&changed_prefs);
	currprefs.cpu_model = changed_prefs.cpu_model;
	currprefs.fpu_model = changed_prefs.fpu_model;
	currprefs.mmu_model = changed_prefs.mmu_model;
	currprefs.cpu_compatible = changed_prefs.cpu_compatible;
	currprefs.cpu_cycle_exact = changed_prefs.cpu_cycle_exact;
	currprefs.blitter_cycle_exact = changed_prefs.cpu_cycle_exact;
}

void check_prefs_changed_cpu (void)
{
	bool changed = 0;

	if (!config_changed)
		return;
#ifdef JIT
	changed = check_prefs_changed_comp ();
#endif
	if (changed
		|| currprefs.cpu_model != changed_prefs.cpu_model
		|| currprefs.fpu_model != changed_prefs.fpu_model
		|| currprefs.mmu_model != changed_prefs.mmu_model
		|| currprefs.cpu_compatible != changed_prefs.cpu_compatible
		|| currprefs.cpu_cycle_exact != changed_prefs.cpu_cycle_exact) {

			prefs_changed_cpu ();
			if (!currprefs.cpu_compatible && changed_prefs.cpu_compatible)
				fill_prefetch_quick ();
			build_cpufunctbl ();
			changed = 1;
	}
	if (changed
		|| currprefs.m68k_speed != changed_prefs.m68k_speed
		|| currprefs.cpu_clock_multiplier != changed_prefs.cpu_clock_multiplier
		|| currprefs.cpu_frequency != changed_prefs.cpu_frequency) {
			currprefs.m68k_speed = changed_prefs.m68k_speed;
			reset_frame_rate_hack ();
			update_68k_cycles ();
			changed = 1;
	}

	if (currprefs.cpu_idle != changed_prefs.cpu_idle) {
		currprefs.cpu_idle = changed_prefs.cpu_idle;
	}
	if (changed)
		set_special (SPCFLAG_BRK);

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
			write_log (L"Reading instruction count file...\n");
			fscanf (f, "Total: %lu\n", &total);
			while (fscanf (f, "%lx: %lu %s\n", &opcode, &count, name) == 3) {
				instrcount[opcode] = count;
			}
			fclose (f);
		}
	}
#endif
	write_log (L"Building CPU table for configuration: %d", currprefs.cpu_model);
	regs.address_space_mask = 0xffffffff;
	if (currprefs.cpu_compatible) {
		if (currprefs.address_space_24 && currprefs.cpu_model >= 68030)
			currprefs.address_space_24 = false;
	}
	if (currprefs.fpu_model > 0)
		write_log (L"/%d", currprefs.fpu_model);
	if (currprefs.cpu_cycle_exact) {
		if (currprefs.cpu_model == 68000)
			write_log (L" prefetch and cycle-exact");
		else
			write_log (L" ~cycle-exact");
	} else if (currprefs.cpu_compatible)
		write_log (L" prefetch");
	if (currprefs.address_space_24) {
		regs.address_space_mask = 0x00ffffff;
		write_log (L" 24-bit");
	}
	write_log (L"\n");

	read_table68k ();
	do_merges ();

	write_log (L"%d CPU functions\n", nr_cpuop_funcs);

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

static uae_s32 ShowEA (void *f, uae_u16 opcode, int reg, amodes mode, wordsizes size, TCHAR *buf, uae_u32 *eaddr, int safemode)
{
	uae_u16 dp;
	uae_s8 disp8;
	uae_s16 disp16;
	int r;
	uae_u32 dispreg;
	uaecptr addr = 0;
	uae_s32 offset = 0;
	TCHAR buffer[80];

	switch (mode){
	case Dreg:
		_stprintf (buffer, L"D%d", reg);
		break;
	case Areg:
		_stprintf (buffer, L"A%d", reg);
		break;
	case Aind:
		_stprintf (buffer, L"(A%d)", reg);
		addr = regs.regs[reg + 8];
		break;
	case Aipi:
		_stprintf (buffer, L"(A%d)+", reg);
		addr = regs.regs[reg + 8];
		break;
	case Apdi:
		_stprintf (buffer, L"-(A%d)", reg);
		addr = regs.regs[reg + 8];
		break;
	case Ad16:
		{
			TCHAR offtxt[80];
			disp16 = get_iword_1 (m68kpc_offset); m68kpc_offset += 2;
			if (disp16 < 0)
				_stprintf (offtxt, L"-$%04x", -disp16);
			else
				_stprintf (offtxt, L"$%04x", disp16);
			addr = m68k_areg (regs, reg) + disp16;
			_stprintf (buffer, L"(A%d, %s) == $%08lx", reg, offtxt, (unsigned long)addr);
		}
		break;
	case Ad8r:
		dp = get_iword_1 (m68kpc_offset); m68kpc_offset += 2;
		disp8 = dp & 0xFF;
		r = (dp & 0x7000) >> 12;
		dispreg = dp & 0x8000 ? m68k_areg (regs, r) : m68k_dreg (regs, r);
		if (!(dp & 0x800)) dispreg = (uae_s32)(uae_s16)(dispreg);
		dispreg <<= (dp >> 9) & 3;

		if (dp & 0x100) {
			uae_s32 outer = 0, disp = 0;
			uae_s32 base = m68k_areg (regs, reg);
			TCHAR name[10];
			_stprintf (name, L"A%d, ", reg);
			if (dp & 0x80) { base = 0; name[0] = 0; }
			if (dp & 0x40) dispreg = 0;
			if ((dp & 0x30) == 0x20) { disp = (uae_s32)(uae_s16)get_iword_1 (m68kpc_offset); m68kpc_offset += 2; }
			if ((dp & 0x30) == 0x30) { disp = get_ilong_1 (m68kpc_offset); m68kpc_offset += 4; }
			base += disp;

			if ((dp & 0x3) == 0x2) { outer = (uae_s32)(uae_s16)get_iword_1 (m68kpc_offset); m68kpc_offset += 2; }
			if ((dp & 0x3) == 0x3) { outer = get_ilong_1 (m68kpc_offset); m68kpc_offset += 4; }

			if (!(dp & 4)) base += dispreg;
			if ((dp & 3) && !safemode) base = get_long (base);
			if (dp & 4) base += dispreg;

			addr = base + outer;
			_stprintf (buffer, L"(%s%c%d.%c*%d+%ld)+%ld == $%08lx", name,
				dp & 0x8000 ? 'A' : 'D', (int)r, dp & 0x800 ? 'L' : 'W',
				1 << ((dp >> 9) & 3),
				disp, outer,
				(unsigned long)addr);
		} else {
			addr = m68k_areg (regs, reg) + (uae_s32)((uae_s8)disp8) + dispreg;
			_stprintf (buffer, L"(A%d, %c%d.%c*%d, $%02x) == $%08lx", reg,
				dp & 0x8000 ? 'A' : 'D', (int)r, dp & 0x800 ? 'L' : 'W',
				1 << ((dp >> 9) & 3), disp8,
				(unsigned long)addr);
		}
		break;
	case PC16:
		addr = m68k_getpc () + m68kpc_offset;
		disp16 = get_iword_1 (m68kpc_offset); m68kpc_offset += 2;
		addr += (uae_s16)disp16;
		_stprintf (buffer, L"(PC,$%04x) == $%08lx", disp16 & 0xffff, (unsigned long)addr);
		break;
	case PC8r:
		addr = m68k_getpc () + m68kpc_offset;
		dp = get_iword_1 (m68kpc_offset); m68kpc_offset += 2;
		disp8 = dp & 0xFF;
		r = (dp & 0x7000) >> 12;
		dispreg = dp & 0x8000 ? m68k_areg (regs, r) : m68k_dreg (regs, r);
		if (!(dp & 0x800)) dispreg = (uae_s32)(uae_s16)(dispreg);
		dispreg <<= (dp >> 9) & 3;

		if (dp & 0x100) {
			uae_s32 outer = 0, disp = 0;
			uae_s32 base = addr;
			TCHAR name[10];
			_stprintf (name, L"PC, ");
			if (dp & 0x80) { base = 0; name[0] = 0; }
			if (dp & 0x40) dispreg = 0;
			if ((dp & 0x30) == 0x20) { disp = (uae_s32)(uae_s16)get_iword_1 (m68kpc_offset); m68kpc_offset += 2; }
			if ((dp & 0x30) == 0x30) { disp = get_ilong_1 (m68kpc_offset); m68kpc_offset += 4; }
			base += disp;

			if ((dp & 0x3) == 0x2) { outer = (uae_s32)(uae_s16)get_iword_1 (m68kpc_offset); m68kpc_offset += 2; }
			if ((dp & 0x3) == 0x3) { outer = get_ilong_1 (m68kpc_offset); m68kpc_offset += 4; }

			if (!(dp & 4)) base += dispreg;
			if ((dp & 3) && !safemode) base = get_long (base);
			if (dp & 4) base += dispreg;

			addr = base + outer;
			_stprintf (buffer, L"(%s%c%d.%c*%d+%ld)+%ld == $%08lx", name,
				dp & 0x8000 ? 'A' : 'D', (int)r, dp & 0x800 ? 'L' : 'W',
				1 << ((dp >> 9) & 3),
				disp, outer,
				(unsigned long)addr);
		} else {
			addr += (uae_s32)((uae_s8)disp8) + dispreg;
			_stprintf (buffer, L"(PC, %c%d.%c*%d, $%02x) == $%08lx", dp & 0x8000 ? 'A' : 'D',
				(int)r, dp & 0x800 ? 'L' : 'W',  1 << ((dp >> 9) & 3),
				disp8, (unsigned long)addr);
		}
		break;
	case absw:
		addr = (uae_s32)(uae_s16)get_iword_1 (m68kpc_offset);
		_stprintf (buffer, L"$%08lx", (unsigned long)addr);
		m68kpc_offset += 2;
		break;
	case absl:
		addr = get_ilong_1 (m68kpc_offset);
		_stprintf (buffer, L"$%08lx", (unsigned long)addr);
		m68kpc_offset += 4;
		break;
	case imm:
		switch (size){
		case sz_byte:
			_stprintf (buffer, L"#$%02x", (unsigned int)(get_iword_1 (m68kpc_offset) & 0xff));
			m68kpc_offset += 2;
			break;
		case sz_word:
			_stprintf (buffer, L"#$%04x", (unsigned int)(get_iword_1 (m68kpc_offset) & 0xffff));
			m68kpc_offset += 2;
			break;
		case sz_long:
			_stprintf (buffer, L"#$%08lx", (unsigned long)(get_ilong_1 (m68kpc_offset)));
			m68kpc_offset += 4;
			break;
		default:
			break;
		}
		break;
	case imm0:
		offset = (uae_s32)(uae_s8)get_iword_1 (m68kpc_offset);
		m68kpc_offset += 2;
		_stprintf (buffer, L"#$%02x", (unsigned int)(offset & 0xff));
		break;
	case imm1:
		offset = (uae_s32)(uae_s16)get_iword_1 (m68kpc_offset);
		m68kpc_offset += 2;
		buffer[0] = 0;
		_stprintf (buffer, L"#$%04x", (unsigned int)(offset & 0xffff));
		break;
	case imm2:
		offset = (uae_s32)get_ilong_1 (m68kpc_offset);
		m68kpc_offset += 4;
		_stprintf (buffer, L"#$%08lx", (unsigned long)offset);
		break;
	case immi:
		offset = (uae_s32)(uae_s8)(reg & 0xff);
		_stprintf (buffer, L"#$%08lx", (unsigned long)offset);
		break;
	default:
		break;
	}
	if (buf == 0)
		f_out (f, L"%s", buffer);
	else
		_tcscat (buf, buffer);
	if (eaddr)
		*eaddr = addr;
	return offset;
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
		write_log (L"get_bitfield() can't happen %d\n", (offset + width + 7) >> 3);
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
		write_log (L"put_bitfield() can't happen %d\n", (offset + 7) >> 3);
		break;
	}
}

uae_u32 REGPARAM2 x_get_bitfield (uae_u32 src, uae_u32 bdata[2], uae_s32 offset, int width)
{
	uae_u32 tmp, res, mask;

	offset &= 7;
	mask = 0xffffffffu << (32 - width);
	switch ((offset + width + 7) >> 3) {
	case 1:
		tmp = x_get_byte (src);
		res = tmp << (24 + offset);
		bdata[0] = tmp & ~(mask >> (24 + offset));
		break;
	case 2:
		tmp = x_get_word (src);
		res = tmp << (16 + offset);
		bdata[0] = tmp & ~(mask >> (16 + offset));
		break;
	case 3:
		tmp = x_get_word (src);
		res = tmp << (16 + offset);
		bdata[0] = tmp & ~(mask >> (16 + offset));
		tmp = x_get_byte (src + 2);
		res |= tmp << (8 + offset);
		bdata[1] = tmp & ~(mask >> (8 + offset));
		break;
	case 4:
		tmp = x_get_long (src);
		res = tmp << offset;
		bdata[0] = tmp & ~(mask >> offset);
		break;
	case 5:
		tmp = x_get_long (src);
		res = tmp << offset;
		bdata[0] = tmp & ~(mask >> offset);
		tmp = x_get_byte (src + 4);
		res |= tmp >> (8 - offset);
		bdata[1] = tmp & ~(mask << (8 - offset));
		break;
	default:
		/* Panic? */
		write_log (L"x_get_bitfield() can't happen %d\n", (offset + width + 7) >> 3);
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
		x_put_byte (dst, bdata[0] | (val << (8 - offset)));
		break;
	case 2:
		x_put_word (dst, bdata[0] | (val << (16 - offset)));
		break;
	case 3:
		x_put_word (dst, bdata[0] | (val >> (offset - 16)));
		x_put_byte (dst + 2, bdata[1] | (val << (24 - offset)));
		break;
	case 4:
		x_put_long (dst, bdata[0] | (val << (32 - offset)));
		break;
	case 5:
		x_put_long (dst, bdata[0] | (val >> (offset - 32)));
		x_put_byte (dst + 4, bdata[1] | (val << (40 - offset)));
		break;
	default:
		write_log (L"x_put_bitfield() can't happen %d\n", (offset + 7) >> 3);
		break;
	}
}

uae_u32 REGPARAM2 get_disp_ea_020 (uae_u32 base, uae_u32 dp)
{
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

uae_u32 REGPARAM2 x_get_disp_ea_020 (uae_u32 base, uae_u32 dp)
{
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
	console_out_f (L"Exception %d, PC=%08X\n", nr, M68K_GETPC);
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
		write_log (L"Exception %d (%x) at %x -> %x!\n", nr, last_addr_for_exception_3, currpc, get_long (4 * nr));
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
			uae_reset (1); /* there is nothing else we can do.. */
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

static void Exception_mmu (int nr)
{
	uae_u32 newpc;
	int sv = regs.s;
	int pc = exception_pc (nr);
	int i;

	exception_debug (nr);
	MakeSR ();

	if (!regs.s) {
		regs.usp = m68k_areg (regs, 7);
		if (currprefs.cpu_model >= 68020)
			m68k_areg (regs, 7) = regs.m ? regs.msp : regs.isp;
		else
			m68k_areg (regs, 7) = regs.isp;
		regs.s = 1;
		mmu_set_super (1);
	}
	if (nr == 2) {
//		write_log (L"%08x %08x %08x\n", currpc, oldpc, regs.mmu_fault_addr);
//		if (currpc == 0x0013b5e2)
//			activate_debugger ();
		// bus error
		for (i = 0 ; i < 7 ; i++) {
			m68k_areg (regs, 7) -= 4;
			put_long_mmu (m68k_areg (regs, 7), 0);
		}
		m68k_areg (regs, 7) -= 4;
		put_long_mmu (m68k_areg (regs, 7), regs.wb3_data);
		m68k_areg (regs, 7) -= 4;
		put_long_mmu (m68k_areg (regs, 7), regs.mmu_fault_addr);
		m68k_areg (regs, 7) -= 4;
		put_long_mmu (m68k_areg (regs, 7), regs.mmu_fault_addr);
		m68k_areg (regs, 7) -= 2;
		put_word_mmu (m68k_areg (regs, 7), 0);
		m68k_areg (regs, 7) -= 2;
		put_word_mmu (m68k_areg (regs, 7), 0);
		m68k_areg (regs, 7) -= 2;
		put_word_mmu (m68k_areg (regs, 7), regs.wb3_status);
		regs.wb3_status = 0;
		m68k_areg (regs, 7) -= 2;
		put_word_mmu (m68k_areg (regs, 7), regs.mmu_ssw);
		m68k_areg (regs, 7) -= 4;
		put_long_mmu (m68k_areg (regs, 7), regs.mmu_fault_addr);

		m68k_areg (regs, 7) -= 2;
		put_word_mmu (m68k_areg (regs, 7), 0x7000 + nr * 4);
		m68k_areg (regs, 7) -= 4;
		put_long_mmu (m68k_areg (regs, 7), regs.instruction_pc);
		m68k_areg (regs, 7) -= 2;
		put_word_mmu (m68k_areg (regs, 7), regs.sr);
		goto kludge_me_do;

	} else if (nr == 3) {

		// address error
		uae_u16 ssw = (sv ? 4 : 0) | (last_instructionaccess_for_exception_3 ? 2 : 1);
		ssw |= last_writeaccess_for_exception_3 ? 0 : 0x40;
		ssw |= 0x20;
		for (i = 0 ; i < 36; i++) {
			m68k_areg (regs, 7) -= 2;
			put_word_mmu (m68k_areg (regs, 7), 0);
		}
		m68k_areg (regs, 7) -= 4;
		put_long_mmu (m68k_areg (regs, 7), last_fault_for_exception_3);
		m68k_areg (regs, 7) -= 2;
		put_word_mmu (m68k_areg (regs, 7), 0);
		m68k_areg (regs, 7) -= 2;
		put_word_mmu (m68k_areg (regs, 7), 0);
		m68k_areg (regs, 7) -= 2;
		put_word_mmu (m68k_areg (regs, 7), 0);
		m68k_areg (regs, 7) -= 2;
		put_word_mmu (m68k_areg (regs, 7), ssw);
		m68k_areg (regs, 7) -= 2;
		put_word_mmu (m68k_areg (regs, 7), 0xb000 + nr * 4);

	} else if (nr ==5 || nr == 6 || nr == 7 || nr == 9) {

		m68k_areg (regs, 7) -= 4;
		put_long_mmu (m68k_areg (regs, 7), regs.instruction_pc);
		m68k_areg (regs, 7) -= 2;
		put_word_mmu (m68k_areg (regs, 7), 0x2000 + nr * 4);

	} else if (regs.m && nr >= 24 && nr < 32) { /* M + Interrupt */

		m68k_areg (regs, 7) -= 2;
		put_word_mmu (m68k_areg (regs, 7), nr * 4);
		m68k_areg (regs, 7) -= 4;
		put_long_mmu (m68k_areg (regs, 7), regs.instruction_pc);
		m68k_areg (regs, 7) -= 2;
		put_word_mmu (m68k_areg (regs, 7), regs.sr);
		regs.sr |= (1 << 13);
		regs.msp = m68k_areg (regs, 7);
		m68k_areg (regs, 7) = regs.isp;
		m68k_areg (regs, 7) -= 2;
		put_word_mmu (m68k_areg (regs, 7), 0x1000 + nr * 4);

	} else {

		m68k_areg (regs, 7) -= 2;
		put_word_mmu (m68k_areg (regs, 7), nr * 4);

	}
	m68k_areg (regs, 7) -= 4;
	put_long_mmu (m68k_areg (regs, 7), pc);
	m68k_areg (regs, 7) -= 2;
	put_word_mmu (m68k_areg (regs, 7), regs.sr);
kludge_me_do:
	newpc = get_long_mmu (regs.vbr + 4 * nr);
	if (newpc & 1) {
		if (nr == 2 || nr == 3)
			uae_reset (1); /* there is nothing else we can do.. */
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
		if (currprefs.cpu_model >= 68020)
			m68k_areg (regs, 7) = regs.m ? regs.msp : regs.isp;
		else
			m68k_areg (regs, 7) = regs.isp;
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
								uae_reset (1); /* there is nothing else we can do.. */
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
			write_log (L"Exception %d (%x) at %x -> %x!\n", nr, regs.instruction_pc, currpc, x_get_long (regs.vbr + 4*nr));
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
			write_log (L"Exception %d (%x) at %x -> %x!\n", nr, last_fault_for_exception_3, currpc, x_get_long (regs.vbr + 4*nr));
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
			uae_reset (1); /* there is nothing else we can do.. */
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

void REGPARAM2 Exception (int nr)
{
	regs.exception = nr;
	if (cpu_tracer) {
		cputrace.state = nr;
	}

	if (currprefs.cachesize)
		regs.instruction_pc = m68k_getpc ();
#ifdef CPUEMU_12
	if (currprefs.cpu_cycle_exact && currprefs.cpu_model == 68000)
		Exception_ce000 (nr);
	else
#endif
		if (currprefs.mmu_model)
			Exception_mmu (nr);
		else
			Exception_normal (nr);

	if (debug_illegal && !in_rom (M68K_GETPC)) {
		int v = nr;
		if (nr <= 63 && (debug_illegal_mask & ((uae_u64)1 << nr))) {
			write_log (L"Exception %d breakpoint\n", nr);
			activate_debugger ();
		}
	}
	regs.exception = 0;
	if (cpu_tracer) {
		cputrace.state = 0;
	}
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
	write_log (L"move2c %04X <- %08X PC=%x\n", regno, *regp, M68K_GETPC);
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
		case 4: regs.itt0 = *regp & 0xffffe364; break;
		case 5: regs.itt1 = *regp & 0xffffe364; break;
		case 6: regs.dtt0 = *regp & 0xffffe364; break;
		case 7: regs.dtt1 = *regp & 0xffffe364; break;
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
				if (((opcr ^ regs.pcr) & 2) == 2) {
					write_log (L"68060 FPU state: %s\n", regs.pcr & 2 ? L"disabled" : L"enabled");
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
	write_log (L"movec2 %04X PC=%x\n", regno, M68K_GETPC);
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
	write_log (L"-> %08X\n", *regp);
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
#if defined (uae_s64)
	if (src == 0) {
		Exception (5);
		return;
	}
	if (extra & 0x800) {
		/* signed variant */
		uae_s64 a = (uae_s64)(uae_s32)m68k_dreg (regs, (extra >> 12) & 7);
		uae_s64 quot, rem;

		if (extra & 0x400) {
			a &= 0xffffffffu;
			a |= (uae_s64)m68k_dreg (regs, extra & 7) << 32;
		}
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
	if (src == 0) {
		Exception (5);
		return;
	}
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

void m68k_reset (int hardreset)
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
	if (currprefs.cpu_model == 68020) {
		regs.cacr |= 8;
		set_cpu_caches ();
	}

	mmufixup[0].reg = -1;
	mmufixup[1].reg = -1;
	if (currprefs.mmu_model) {
		mmu_reset ();
		mmu_set_tc (regs.tcr);
		mmu_set_super (regs.s != 0);
	}

	a3000_fakekick (0);
	/* only (E)nable bit is zeroed when CPU is reset, A3000 SuperKickstart expects this */
	tc_030 &= ~0x80000000;
	tt0_030 &= ~0x80000000;
	tt1_030 &= ~0x80000000;
	if (hardreset) {
		srp_030 = crp_030 = 0;
		tt0_030 = tt1_030 = tc_030 = 0;
	}
	mmusr_030 = 0;

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
	regs.ce020tmpcycles = MAX68020CYCLES;
	regs.ce020memcycles = 0;
	fill_prefetch_quick ();
}

unsigned long REGPARAM2 op_illg (uae_u32 opcode)
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
			//write_log (L"B-Trap %x at %x (%p)\n", opcode, pc, regs.pc_p);
			//warned++;
		}
		Exception (0xB);
		//activate_debugger ();
		return 4;
	}
	if ((opcode & 0xF000) == 0xA000) {
		if (warned < 20) {
			//write_log (L"A-Trap %x at %x (%p)\n", opcode, pc, regs.pc_p);
			//warned++;
		}
		Exception (0xA);
		//activate_debugger();
		return 4;
	}
	if (warned < 20) {
		write_log (L"Illegal instruction: %04x at %08X -> %08X\n", opcode, pc, get_long (regs.vbr + 0x10));
		warned++;
		//activate_debugger();
	}

	Exception (4);
	return 4;
}

#ifdef CPUEMU_0

static TCHAR *mmu30regs[] = { L"TCR", L"", L"SRP", L"CRP", L"", L"", L"", L"" };

static void mmu_op30_pmove (uaecptr pc, uae_u32 opcode, uae_u16 next, uaecptr extra)
{
	int preg = (next >> 10) & 31;
	int rw = (next >> 9) & 1;
	int fd = (next >> 8) & 1;
	TCHAR *reg = NULL;
	uae_u32 otc = tc_030;
	int siz;

	switch (preg)
	{
	case 0x10: // TC
		reg = L"TC";
		siz = 4;
		if (rw)
			x_put_long (extra, tc_030);
		else
			tc_030 = x_get_long (extra);
		break;
	case 0x12: // SRP
		reg = L"SRP";
		siz = 8;
		if (rw) {
			x_put_long (extra, srp_030 >> 32);
			x_put_long (extra + 4, srp_030);
		} else {
			srp_030 = (uae_u64)x_get_long (extra) << 32;
			srp_030 |= x_get_long (extra + 4);
		}
		break;
	case 0x13: // CRP
		reg = L"CRP";
		siz = 8;
		if (rw) {
			x_put_long (extra, crp_030 >> 32);
			x_put_long (extra + 4, crp_030);
		} else {
			crp_030 = (uae_u64)x_get_long (extra) << 32;
			crp_030 |= x_get_long (extra + 4);
		}
		break;
	case 0x18: // MMUSR
		reg = L"MMUSR";
		siz = 2;
		if (rw)
			x_put_word (extra, mmusr_030);
		else
			mmusr_030 = x_get_word (extra);
		break;
	case 0x02: // TT0
		reg = L"TT0";
		siz = 4;
		if (rw)
			x_put_long (extra, tt0_030);
		else
			tt0_030 = x_get_long (extra);
		break;
	case 0x03: // TT1
		reg = L"TT1";
		siz = 4;
		if (rw)
			x_put_long (extra, tt1_030);
		else
			tt1_030 = x_get_long (extra);
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
				write_log (L"PMOVE %s,%08X%08X", reg, val2, val);
			else
				write_log (L"PMOVE %08X%08X,%s", val2, val, reg);
		} else {
			if (siz == 4)
				val = x_get_long (extra);
			else
				val = x_get_word (extra);
			if (rw)
				write_log (L"PMOVE %s,%08X", reg, val);
			else
				write_log (L"PMOVE %08X,%s", val, reg);
		}
		write_log (L" PC=%08X\n", pc);
	}
#endif
	if (currprefs.cs_mbdmac == 1 && currprefs.mbresmem_low_size > 0) {
		if (otc != tc_030) {
			a3000_fakekick (tc_030 & 0x80000000);
		}
	}
}

static void mmu_op30_ptest (uaecptr pc, uae_u32 opcode, uae_u16 next, uaecptr extra)
{
#if MMUOP_DEBUG > 0
	TCHAR tmp[10];

	tmp[0] = 0;
	if ((next >> 8) & 1)
		_stprintf (tmp, L",A%d", (next >> 4) & 15);
	write_log (L"PTEST%c %02X,%08X,#%X%s PC=%08X\n",
		((next >> 9) & 1) ? 'W' : 'R', (next & 15), extra, (next >> 10) & 7, tmp, pc);
#endif
	mmusr_030 = 0;
}

static void mmu_op30_pflush (uaecptr pc, uae_u32 opcode, uae_u16 next, uaecptr extra)
{
#if MMUOP_DEBUG > 0
	write_log (L"PFLUSH PC=%08X\n", pc);
#endif
}

void mmu_op30 (uaecptr pc, uae_u32 opcode, uae_u16 extra, uaecptr extraa)
{
	if (currprefs.cpu_model != 68030) {
		m68k_setpc (pc);
		op_illg (opcode);
		return;
	}
	if (extra & 0x8000)
		mmu_op30_ptest (pc, opcode, extra, extraa);
	else if (extra & 0x2000)
		mmu_op30_pflush (pc, opcode, extra, extraa);
	else
		mmu_op30_pmove (pc, opcode, extra, extraa);
}

void mmu_op (uae_u32 opcode, uae_u32 extra)
{
	if (currprefs.cpu_model) {
		mmu_op_real (opcode, extra);
		return;
	}
#if MMUOP_DEBUG > 1
	write_log (L"mmu_op %04X PC=%08X\n", opcode, m68k_getpc ());
#endif
	if ((opcode & 0xFE0) == 0x0500) {
		/* PFLUSH */
		regs.mmusr = 0;
#if MMUOP_DEBUG > 0
		write_log (L"PFLUSH\n");
#endif
		return;
	} else if ((opcode & 0x0FD8) == 0x548) {
		if (currprefs.cpu_model < 68060) { /* PTEST not in 68060 */
			/* PTEST */
#if MMUOP_DEBUG > 0
			write_log (L"PTEST\n");
#endif
			return;
		}
	} else if ((opcode & 0x0FB8) == 0x588) {
		/* PLPA */
		if (currprefs.cpu_model == 68060) {
#if MMUOP_DEBUG > 0
			write_log (L"PLPA\n");
#endif
			return;
		}
	}
#if MMUOP_DEBUG > 0
	write_log (L"Unknown MMU OP %04X\n", opcode);
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
	if (currprefs.cpu_compatible)
		set_special (SPCFLAG_INT);
	else
		set_special (SPCFLAG_DOINT);
}

#define IDLETIME (currprefs.cpu_idle * sleep_resolution / 700)

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
		/*	write_log (L"PC:%p\n", m68k_getpc ());*/

		if (action_replay_flag == ACTION_REPLAY_ACTIVATE || action_replay_flag == ACTION_REPLAY_DORESET)
			action_replay_enter ();
		if (action_replay_flag == ACTION_REPLAY_HIDE && !is_ar_pc_in_rom ()) {
			action_replay_hide ();
			unset_special (SPCFLAG_ACTION_REPLAY);
		}
		if (action_replay_flag == ACTION_REPLAY_WAIT_PC) {
			/*write_log (L"Waiting for PC: %p, current PC= %p\n", wait_for_pc, m68k_getpc ());*/
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
		if (c > 0) {
			cycles -= c * CYCLE_UNIT * 2;
			if (cycles < CYCLE_UNIT)
				cycles = 0;
		} else
			c = 4;
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

		if (uae_int_requested) {
			INTREQ_f (0x8008);
			set_special (SPCFLAG_INT);
		}
		{
			extern int volatile uaenet_int_requested;
			if (uaenet_int_requested) {
				INTREQ_f (0x8000 | 0x2000);
				set_special (SPCFLAG_INT);
			}
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

		if (!uae_int_requested && currprefs.cpu_idle && currprefs.m68k_speed != 0 && ((regs.spcflags & SPCFLAG_STOP)) == SPCFLAG_STOP) {
			/* sleep 1ms if STOP-instruction is executed */
			if (1) {
				static int sleepcnt, lvpos, zerocnt;
				if (vpos != lvpos) {
					sleepcnt--;
#ifdef JIT
					if (pissoff == 0 && currprefs.cachesize && --zerocnt < 0) {
						sleepcnt = -1;
						zerocnt = IDLETIME / 4;
					}
#endif
					lvpos = vpos;
					if (sleepcnt < 0) {
						sleepcnt = IDLETIME / 2;
						sleep_millis (1);
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

	if ((regs.spcflags & (SPCFLAG_BRK | SPCFLAG_MODE_CHANGE))) {
		unset_special (SPCFLAG_BRK | SPCFLAG_MODE_CHANGE);
		return 1;
	}
	return 0;
}

//static uae_u32 pcs[1000];

#if DEBUG_CD32CDTVIO

static uae_u32 cd32nextpc, cd32request;

static void out_cd32io2 (void)
{
	uae_u32 request = cd32request;
	write_log (L"%08x returned\n", request);
	//write_log (L"ACTUAL=%d ERROR=%d\n", get_long (request + 32), get_byte (request + 31));
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
		_stprintf (out, L"opendevice");
		break;
	case 0xe57ce6:
	case 0xf04c56:
		_stprintf (out, L"closedevice");
		break;
	case 0xe57e44:
	case 0xf04f2c:
		_stprintf (out, L"beginio");
		ioreq = 1;
		break;
	case 0xe57ef2:
	case 0xf0500e:
		_stprintf (out, L"abortio");
		ioreq = -1;
		break;
	}
	if (out[0] == 0)
		return;
	if (cd32request)
		write_log (L"old request still not returned!\n");
	cd32request = request;
	cd32nextpc = get_long (m68k_areg (regs, 7));
	write_log (L"%s A1=%08X\n", out, request);
	if (ioreq) {
		static int cnt = 0;
		int cmd = get_word (request + 28);
#if 0
		if (cmd == 37) {
			cnt--;
			if (cnt <= 0)
				activate_debugger ();
		}
#endif
		write_log (L"CMD=%d DATA=%08X LEN=%d %OFF=%d PC=%x\n",
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

	for (;;) {
		uae_u16 opcode = r->ir;

		count_instr (opcode);

#if DEBUG_CD32CDTVIO
		out_cd32io (m68k_getpc ());
#endif

#if 0
		int pc = m68k_getpc ();
		if (pc == 0xdff002)
			write_log (L"hip\n");
		if (pc != pcs[0] && (pc < 0xd00000 || pc > 0x1000000)) {
			memmove (pcs + 1, pcs, 998 * 4);
			pcs[0] = pc;
			//write_log (L"%08X-%04X ", pc, opcode);
		}
#endif
		do_cycles (cpu_cycles);
		cpu_cycles = (*cpufunctbl[opcode])(opcode);
		cpu_cycles &= cycles_mask;
		cpu_cycles |= cycles_val;
		if (r->spcflags) {
			if (do_specialties (cpu_cycles))
				return;
		}
		regs.ipl = regs.ipl_pin;
		if (!currprefs.cpu_compatible || (currprefs.cpu_cycle_exact && currprefs.cpu_model == 68000))
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
				write_log (L"CPU TRACE: EXCEPTION %d\n", cputrace.state);
				Exception (cputrace.state);
			} else if (cputrace.state == 1) {
				write_log (L"CPU TRACE: %04X\n", cputrace.opcode);
				(*cpufunctbl[cputrace.opcode])(cputrace.opcode);
			}
		} else {
			write_log (L"CPU TRACE: STOPPED\n");
		}
		if (r->stopped)
			set_special (SPCFLAG_STOP);
		set_cpu_tracer (false);
		goto cont;
	}

	set_cpu_tracer (false);

	for (;;) {
		uae_u16 opcode = r->ir;

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
			write_log (L"STARTCYCLES=%08x ENDCYCLES=%08x\n", cputrace.startcycles, get_cycles ());
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

		cpu_cycles &= cycles_mask;
		cpu_cycles |= cycles_val;

		do_cycles (cpu_cycles);

		if (end_block (opcode) || r->spcflags || uae_int_requested)
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
		uae_u16 opcode = get_iword (0);

		special_mem = DISTRUST_CONSISTENT_MEM;
		pc_hist[blocklen].location = (uae_u16*)r->pc_p;

		cpu_cycles = (*cpufunctbl[opcode])(opcode);

		cpu_cycles &= cycles_mask;
		cpu_cycles |= cycles_val;
		do_cycles (cpu_cycles);
		total_cycles += cpu_cycles;
		pc_hist[blocklen].specmem = special_mem;
		blocklen++;
		if (end_block (opcode) || blocklen >= MAXRUN || r->spcflags || uae_int_requested) {
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
		if (uae_int_requested) {
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
			write_log (L"mmufixup=%d %04x %04x\n", mmufixup[0].reg, regs.wb3_status, regs.mmu_ssw);
		m68k_disasm_2 (buf, sizeof buf / sizeof (TCHAR), addr, NULL, 1, NULL, NULL, 0);
		write_log (L"%s\n", buf);
		if (full)
			m68k_dumpstate (stdout, NULL);
	}
}

/* Aranym MMU 68040  */
static void m68k_run_mmu040 (void)
{
	uae_u16 opcode;
	uaecptr pc;
retry:
	TRY (prb) {
		for (;;) {
			pc = regs.instruction_pc = regs.instruction_pc = m68k_getpc ();
#if 0
			if (regs.regs[8+1] == 0x5b) {
				static int cnt = 6;
				cnt--;
				if (cnt > 0)
					activate_debugger();
			}
#endif
#if 0
			static int done;
			if (pc == 0x16AF94) {
//				write_log (L"D0=%d A7=%08x\n", regs.regs[0], regs.regs[15]);
				if (regs.regs[0] == 360) {
					done = 1;
					activate_debugger ();
				}
			}
/*
			if (pc == 0x16B01A) {
				write_log (L"-> ERR\n");
			}
			if (pc == 0x16B018) {
				write_log (L"->\n");
			}
*/
			if (pc == 0x17967C || pc == 0x13b5e2 - 4) {
				if (done) {
					write_log (L"*\n");
					mmu_dump_tables ();
					activate_debugger ();
				}
			}
#endif

			opcode = get_iword_mmu (0);
			count_instr (opcode);
			do_cycles (cpu_cycles);
			cpu_cycles = (*cpufunctbl[opcode])(opcode);
			cpu_cycles &= cycles_mask;
			cpu_cycles |= cycles_val;
			if (regs.spcflags) {
				if (do_specialties (cpu_cycles))
					return;
			}
		}
	} CATCH (prb) {

		if (currprefs.mmu_model == 68060) {
			regs.instruction_pc = pc;
			if (mmufixup[1].reg >= 0) {
				m68k_areg (regs, mmufixup[1].reg) = mmufixup[1].value;
				mmufixup[1].reg = -1;
			}
		} else {
#if 0
			if (regs.wb3_status & 0x80) {
				// movem to memory?
				if ((opcode & 0xff80) == 0x4880) {
					regs.mmu_ssw |= MMU_SSW_CM;
					//write_log (L"MMU_SSW_CM\n");
				}
			}
#endif
		}

		//opcodedebug (pc, opcode, false);

		if (mmufixup[0].reg >= 0) {
			m68k_areg (regs, mmufixup[0].reg) = mmufixup[0].value;
			mmufixup[0].reg = -1;
		}
		//activate_debugger ();
		TRY (prb2) {
			Exception (prb);
		} CATCH (prb2) {
			write_log (L"MMU: double bus error, rebooting..\n");
			regs.tcr = 0;
			m68k_reset (0);
			m68k_setpc (0xf80002);
			mmu_reset ();
			uae_reset (1);
			return;
		}
		goto retry;
	}

}

/* "cycle exact" 68020/030  */

STATIC_INLINE void docodece020 (uae_u32 opcode)
{
	(*cpufunctbl[opcode])(opcode);
	if (regs.ce020memcycles > 0) {
		regs.ce020tmpcycles = CYCLE_UNIT * MAX68020CYCLES;
		x_do_cycles (regs.ce020memcycles);
		regs.ce020memcycles = 0;
	}
}

static void m68k_run_2ce (void)
{
	struct regstruct *r = &regs;

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
		memcpy (&r->prefetch020, &cputrace.prefetch020, CPU_PIPELINE_MAX * sizeof (uae_u16));
		memcpy (&caches020, &cputrace.caches020, sizeof caches020);

		m68k_setpc (cputrace.pc);
		if (!r->stopped) {
			if (cputrace.state > 1)
				Exception (cputrace.state);
			else if (cputrace.state == 1)
				docodece020 (cputrace.opcode);
		}
		if (regs.stopped)
			set_special (SPCFLAG_STOP);
		set_cpu_tracer (false);
		goto cont;
	}

	set_cpu_tracer (false);

	for (;;) {
		r->instruction_pc = m68k_getpc ();
		uae_u16 opcode = x_prefetch (0);

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

			cputrace.msp = r->msp;
			cputrace.vbr = r->vbr;
			cputrace.caar = r->caar;
			cputrace.cacr = r->cacr;
			cputrace.cacheholdingdata020 = r->cacheholdingdata020;
			cputrace.cacheholdingaddr020 = r->cacheholdingaddr020;
			cputrace.prefetch020addr = r->prefetch020addr;
			memcpy (&cputrace.prefetch020, &r->prefetch020, CPU_PIPELINE_MAX * sizeof (uae_u16));
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

		docodece020 (opcode);

cont:
		if (r->spcflags || time_for_interrupt ()) {
			if (do_specialties (0))
				return;
		}

		regs.ce020tmpcycles -= cpucycleunit;
		if (regs.ce020tmpcycles <= 0) {
			x_do_cycles (1 * CYCLE_UNIT);
			regs.ce020tmpcycles = CYCLE_UNIT * MAX68020CYCLES;;
		}
		regs.ipl = regs.ipl_pin;
	}
}

/* emulate simple prefetch  */
static void m68k_run_2p (void)
{
	uae_u32 prefetch, prefetch_pc;
	struct regstruct *r = &regs;

	prefetch_pc = m68k_getpc ();
	prefetch = x_get_long (prefetch_pc);
	for (;;) {
		uae_u32 pc = m68k_getpc ();
		uae_u16 opcode;

		r->instruction_pc = pc;

#if DEBUG_CD32CDTVIO
		out_cd32io (m68k_getpc ());
#endif

		x_do_cycles (cpu_cycles);

		if (pc == prefetch_pc) {
			opcode = prefetch >> 16;
		} else if (pc == prefetch_pc + 2) {
			opcode = prefetch;
		} else {
			opcode = x_get_word (pc);
			prefetch_pc = pc + 2;
			prefetch = x_get_long (prefetch_pc);
		}

		count_instr (opcode);

		cpu_cycles = (*cpufunctbl[opcode])(opcode);
		cpu_cycles &= cycles_mask;
		cpu_cycles |= cycles_val;
		if (r->spcflags) {
			if (do_specialties (cpu_cycles))
				return;
		}
	}
}

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
			write_log (L"%04X ", opcode);
			used[opcode] = 1;
		}
#endif	
		do_cycles (cpu_cycles);
		cpu_cycles = (*cpufunctbl[opcode])(opcode);
		cpu_cycles &= cycles_mask;
		cpu_cycles |= cycles_val;
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
		cpu_cycles &= cycles_mask;
		cpu_cycles |= cycles_val;
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
		write_log (L"Bug! m68k_go is not reentrant.\n");
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
			quit_program = 2;
		}
		if (input_play || input_record)
			inprec_startup ();

		if (quit_program > 0) {
			int hardreset = (quit_program == 3 ? 1 : 0) | hardboot;
			if (quit_program == 1)
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
			custom_reset (hardreset);
			m68k_reset (hardreset);
			if (hardreset) {
				memory_hardreset ();
				write_log (L"hardreset, memory cleared\n");
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
			get_real_address (regs.isp); /* stack in no one's land? -> reboot */
			if (regs.isp & 1)
				regs.panic = 1;
			if (!regs.panic)
				exception2_handle (regs.panic_pc, regs.panic_addr);
			if (regs.panic) {
				/* system is very badly confused */
				write_log (L"double bus error or corrupted stack, forcing reboot..\n");
				regs.panic = 0;
				uae_reset (1);
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
		if (startup)
			custom_prepare ();
		startup = 0;
		if (mmu_enabled && !currprefs.cachesize) {
			run_func = m68k_run_mmu;
		} else {
			run_func = currprefs.cpu_cycle_exact && currprefs.cpu_model == 68000 ? m68k_run_1_ce :
				currprefs.cpu_compatible && currprefs.cpu_model == 68000 ? m68k_run_1 :
#ifdef JIT
				currprefs.cpu_model >= 68020 && currprefs.cachesize ? m68k_run_jit :
#endif
				(currprefs.cpu_model == 68040 || currprefs.cpu_model == 68060) && currprefs.mmu_model ? m68k_run_mmu040 :
				currprefs.cpu_model >= 68020 && currprefs.cpu_cycle_exact ? m68k_run_2ce :
				currprefs.cpu_compatible ? m68k_run_2p : m68k_run_2;
		}
		run_func ();
	}
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
{ L"T ",L"F ",L"HI",L"LS",L"CC",L"CS",L"NE",L"EQ",
L"VC",L"VS",L"PL",L"MI",L"GE",L"LT",L"GT",L"LE" };

static void addmovemreg (TCHAR *out, int *prevreg, int *lastreg, int *first, int reg)
{
	TCHAR *p = out + _tcslen (out);
	if (*prevreg < 0) {
		*prevreg = reg;
		*lastreg = reg;
		return;
	}
	if ((*prevreg) + 1 != reg || (reg & 8) != ((*prevreg & 8))) {
		_stprintf (p, L"%s%c%d", (*first) ? L"" : L"/", (*lastreg) < 8 ? 'D' : 'A', (*lastreg) & 7);
		p = p + _tcslen (p);
		if ((*lastreg) + 2 == reg) {
			_stprintf (p, L"/%c%d", (*prevreg) < 8 ? 'D' : 'A', (*prevreg) & 7);
		} else if ((*lastreg) != (*prevreg)) {
			_stprintf (p, L"-%c%d", (*prevreg) < 8 ? 'D' : 'A', (*prevreg) & 7);
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
		_tcscat (instrname, L".B ");
		break;
	case sz_word:
		_tcscat (instrname, L".W ");
		break;
	case sz_long:
		_tcscat (instrname, L".L ");
		break;
	default:
		_tcscat (instrname, L"   ");
		break;
	}
}

void m68k_disasm_2 (TCHAR *buf, int bufsize, uaecptr addr, uaecptr *nextpc, int cnt, uae_u32 *seaddr, uae_u32 *deaddr, int safemode)
{
	uaecptr newpc = 0;
	m68kpc_offset = addr - m68k_getpc ();

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

		oldpc = m68kpc_offset;
		opcode = get_iword_1 (m68kpc_offset);
		if (cpufunctbl[opcode] == op_illg_1) {
			opcode = 0x4AFC;
		}
		dp = table68k + opcode;
		for (lookup = lookuptab;lookup->mnemo != dp->mnemo; lookup++)
			;

		buf = buf_out (buf, &bufsize, L"%08lX ", m68k_getpc () + m68kpc_offset);

		m68kpc_offset += 2;

		if (lookup->friendlyname)
			_tcscpy (instrname, lookup->friendlyname);
		else
			_tcscpy (instrname, lookup->name);
		ccpt = _tcsstr (instrname, L"cc");
		if (ccpt != 0) {
			_tcsncpy (ccpt, ccnames[dp->cc], 2);
		}
		disasm_size (instrname, dp);

		if (lookup->mnemo == i_MOVEC2 || lookup->mnemo == i_MOVE2C) {
			uae_u16 imm = get_iword_1 (m68kpc_offset);
			uae_u16 creg = imm & 0x0fff;
			uae_u16 r = imm >> 12;
			TCHAR regs[16], *cname = L"?";
			int i;
			for (i = 0; m2cregs[i].regname; i++) {
				if (m2cregs[i].regno == creg)
					break;
			}
			_stprintf (regs, L"%c%d", r >= 8 ? 'A' : 'D', r >= 8 ? r - 8 : r);
			if (m2cregs[i].regname)
				cname = m2cregs[i].regname;
			if (lookup->mnemo == i_MOVE2C) {
				_tcscat (instrname, regs);
				_tcscat (instrname, L",");
				_tcscat (instrname, cname);
			} else {
				_tcscat (instrname, cname);
				_tcscat (instrname, L",");
				_tcscat (instrname, regs);
			}
			m68kpc_offset += 2;
		} else if (lookup->mnemo == i_MVMEL) {
			newpc = m68k_getpc () + m68kpc_offset;
			m68kpc_offset += 2;
			newpc += ShowEA (0, opcode, dp->dreg, dp->dmode, dp->size, instrname, deaddr, safemode);
			_tcscat (instrname, L",");
			movemout (instrname, get_iword_1 (oldpc + 2), dp->dmode);
		} else if (lookup->mnemo == i_MVMLE) {
			m68kpc_offset += 2;
			movemout (instrname, get_iword_1 (oldpc + 2), dp->dmode);
			_tcscat (instrname, L",");
			newpc = m68k_getpc () + m68kpc_offset;
			newpc += ShowEA (0, opcode, dp->dreg, dp->dmode, dp->size, instrname, deaddr, safemode);
		} else {
			if (dp->suse) {
				newpc = m68k_getpc () + m68kpc_offset;
				newpc += ShowEA (0, opcode, dp->sreg, dp->smode, dp->size, instrname, seaddr, safemode);
			}
			if (dp->suse && dp->duse)
				_tcscat (instrname, L",");
			if (dp->duse) {
				newpc = m68k_getpc () + m68kpc_offset;
				newpc += ShowEA (0, opcode, dp->dreg, dp->dmode, dp->size, instrname, deaddr, safemode);
			}
		}

		for (i = 0; i < (m68kpc_offset - oldpc) / 2; i++) {
			buf = buf_out (buf, &bufsize, L"%04x ", get_iword_1 (oldpc + i * 2));
		}
		while (i++ < 5)
			buf = buf_out (buf, &bufsize, L"     ");

		buf = buf_out (buf, &bufsize, instrname);

		if (ccpt != 0) {
			if (deaddr)
				*deaddr = newpc;
			if (cctrue (dp->cc))
				buf = buf_out (buf, &bufsize, L" == $%08lX (T)", newpc);
			else
				buf = buf_out (buf, &bufsize, L" == $%08lX (F)", newpc);
		} else if ((opcode & 0xff00) == 0x6100) { /* BSR */
			if (deaddr)
				*deaddr = newpc;
			buf = buf_out (buf, &bufsize, L" == $%08lX", newpc);
		}
		buf = buf_out (buf, &bufsize, L"\n");
	}
	if (nextpc)
		*nextpc = m68k_getpc () + m68kpc_offset;
}

void m68k_disasm_ea (void *f, uaecptr addr, uaecptr *nextpc, int cnt, uae_u32 *seaddr, uae_u32 *deaddr)
{
	TCHAR *buf;

	buf = xmalloc (TCHAR, (MAX_LINEWIDTH + 1) * cnt);
	if (!buf)
		return;
	m68k_disasm_2 (buf, (MAX_LINEWIDTH + 1) * cnt, addr, nextpc, cnt, seaddr, deaddr, 1);
	f_out (f, L"%s", buf);
	xfree (buf);
}
void m68k_disasm (void *f, uaecptr addr, uaecptr *nextpc, int cnt)
{
	TCHAR *buf;

	buf = xmalloc (TCHAR, (MAX_LINEWIDTH + 1) * cnt);
	if (!buf)
		return;
	m68k_disasm_2 (buf, (MAX_LINEWIDTH + 1) * cnt, addr, nextpc, cnt, NULL, NULL, 0);
	f_out (f, L"%s", buf);
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
	int oldpc;

	uaecptr newpc = 0;

	m68kpc_offset = addr - m68k_getpc ();

	oldpc = m68kpc_offset;
	opcode = get_iword_1 (m68kpc_offset);
	if (cpufunctbl[opcode] == op_illg_1) {
		opcode = 0x4AFC;
	}
	dp = table68k + opcode;
	for (lookup = lookuptab;lookup->mnemo != dp->mnemo; lookup++);

	m68kpc_offset += 2;

	_tcscpy (instrname, lookup->name);
	ccpt = _tcsstr (instrname, L"cc");
	if (ccpt != 0) {
		_tcsncpy (ccpt, ccnames[dp->cc], 2);
	}
	switch (dp->size){
	case sz_byte: _tcscat (instrname, L".B "); break;
	case sz_word: _tcscat (instrname, L".W "); break;
	case sz_long: _tcscat (instrname, L".L "); break;
	default: _tcscat (instrname, L"   "); break;
	}

	if (dp->suse) {
		newpc = m68k_getpc () + m68kpc_offset;
		newpc += ShowEA (0, opcode, dp->sreg, dp->smode, dp->size, instrname, NULL, 0);
	}
	if (dp->suse && dp->duse)
		_tcscat (instrname, L",");
	if (dp->duse) {
		newpc = m68k_getpc () + m68kpc_offset;
		newpc += ShowEA (0, opcode, dp->dreg, dp->dmode, dp->size, instrname, NULL, 0);
	}

	if (instrcode)
	{
		int i;
		for (i = 0; i < (m68kpc_offset - oldpc) / 2; i++)
		{
			_stprintf (instrcode, L"%04x ", get_iword_1 (oldpc + i * 2));
			instrcode += _tcslen (instrcode);
		}
	}

	if (nextpc)
		*nextpc = m68k_getpc () + m68kpc_offset;
}

struct cpum2c m2cregs[] = {
	0, L"SFC",
	1, L"DFC",
	2, L"CACR",
	3, L"TC",
	4, L"ITT0",
	5, L"ITT1",
	6, L"DTT0",
	7, L"DTT1",
	8, L"BUSC",
	0x800, L"USP",
	0x801, L"VBR",
	0x802, L"CAAR",
	0x803, L"MSP",
	0x804, L"ISP",
	0x805, L"MMUS",
	0x806, L"URP",
	0x807, L"SRP",
	0x808, L"PCR",
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

void m68k_dumpstate (void *f, uaecptr *nextpc)
{
	int i, j;

	for (i = 0; i < 8; i++){
		f_out (f, L"  D%d %08lX ", i, m68k_dreg (regs, i));
		if ((i & 3) == 3) f_out (f, L"\n");
	}
	for (i = 0; i < 8; i++){
		f_out (f, L"  A%d %08lX ", i, m68k_areg (regs, i));
		if ((i & 3) == 3) f_out (f, L"\n");
	}
	if (regs.s == 0)
		regs.usp = m68k_areg (regs, 7);
	if (regs.s && regs.m)
		regs.msp = m68k_areg (regs, 7);
	if (regs.s && regs.m == 0)
		regs.isp = m68k_areg (regs, 7);
	j = 2;
	f_out (f, L"USP  %08X ISP  %08X ", regs.usp, regs.isp);
	for (i = 0; m2cregs[i].regno>= 0; i++) {
		if (!movec_illg (m2cregs[i].regno)) {
			if (!_tcscmp (m2cregs[i].regname, L"USP") || !_tcscmp (m2cregs[i].regname, L"ISP"))
				continue;
			if (j > 0 && (j % 4) == 0)
				f_out (f, L"\n");
			f_out (f, L"%-4s %08X ", m2cregs[i].regname, val_move2c (m2cregs[i].regno));
			j++;
		}
	}
	if (j > 0)
		f_out (f, L"\n");
	f_out (f, L"T=%d%d S=%d M=%d X=%d N=%d Z=%d V=%d C=%d IMASK=%d STP=%d\n",
		regs.t1, regs.t0, regs.s, regs.m,
		GET_XFLG (), GET_NFLG (), GET_ZFLG (),
		GET_VFLG (), GET_CFLG (),
		regs.intmask, regs.stopped);
#ifdef FPUEMU
	if (currprefs.fpu_model) {
		uae_u32 fpsr;
		for (i = 0; i < 8; i++){
			f_out (f, L"FP%d: %g ", i, regs.fp[i]);
			if ((i & 3) == 3)
				f_out (f, L"\n");
		}
		fpsr = get_fpsr ();
		f_out (f, L"N=%d Z=%d I=%d NAN=%d\n",
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
		f_out (f, L"Prefetch %04x (%s) %04x (%s)\n", regs.irc, lookup1->name, regs.ir, lookup2->name);
	}

	m68k_disasm (f, m68k_getpc (), nextpc, 1);
	if (nextpc)
		f_out (f, L"Next PC: %08lx\n", *nextpc);
}

#ifdef SAVESTATE

/* CPU save/restore code */

#define CPUTYPE_EC 1
#define CPUMODE_HALT 1



uae_u8 *restore_cpu (uae_u8 *src)
{
	int i, flags, model;
	uae_u32 l;

	changed_prefs.cpu_model = model = restore_u32 ();
	flags = restore_u32 ();
	changed_prefs.address_space_24 = 0;
	if (flags & CPUTYPE_EC)
		changed_prefs.address_space_24 = 1;
	if (model > 68020)
		changed_prefs.cpu_compatible = 0;
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
		/* A500 speed in 68020 mode isn't too logical.. */
		if (changed_prefs.m68k_speed == 0 && !(currprefs.cpu_cycle_exact))
			currprefs.m68k_speed = changed_prefs.m68k_speed = -1;
	}
	if (model >= 68030) {
		crp_030 = restore_u64 ();
		srp_030 = restore_u64 ();
		tt0_030 =restore_u32 ();
		tt1_030 = restore_u32 ();
		tc_030 = restore_u32 ();
		mmusr_030 = restore_u16 ();
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
			for (int i = 0; i < CPU_PIPELINE_MAX; i++)
				regs.prefetch020[i] = restore_u16 ();
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
		}
		if (model >= 68020) {
			regs.ce020memcycles = restore_u32 ();
			regs.ce020tmpcycles = restore_u32 ();
		}
	}

	write_log (L"CPU: %d%s%03d, PC=%08X\n",
		model / 1000, flags & 1 ? L"EC" : L"", model % 1000, regs.pc);

	return src;
}

void restore_cpu_finish (void)
{
	init_m68k ();
	m68k_setpc (regs.pc);
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
		dstbak = dst = xmalloc (uae_u8, 1000);

	save_u32 (2);
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
	write_log (L"CPUT SAVE: PC=%08x C=%08X %08x %08x %08x %d %d %d\n",
		cputrace.pc, cputrace.startcycles,
		cputrace.cyclecounter, cputrace.cyclecounter_pre, cputrace.cyclecounter_post,
		cputrace.readcounter, cputrace.writecounter, cputrace.memoryoffset);
	for (int i = 0; i < cputrace.memoryoffset; i++) {
		save_u32 (cputrace.ctm[i].addr);
		save_u32 (cputrace.ctm[i].data);
		save_u32 (cputrace.ctm[i].mode);
		write_log (L"CPUT%d: %08x %08x %08x\n", i, cputrace.ctm[i].addr, cputrace.ctm[i].data, cputrace.ctm[i].mode);
	}
	save_u32 (cputrace.startcycles);
	*len = dst - dstbak;
	cputrace.needendcycles = 1;
	return dstbak;
}

uae_u8 *restore_cpu_trace (uae_u8 *src)
{
	cpu_tracer = 0;
	cputrace.state = 0;
	uae_u32 v = restore_u32 ();
	if (v != 0 && v != 2)
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
	cputrace.needendcycles = 1;
	if (v && cputrace.state)
		cpu_tracer = -1;

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
	currprefs.cachesize = changed_prefs.cachesize = (flags & 8) ? 8192 : 0;

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
	save_u32 (0x80000000 | 0x40000000 | (currprefs.address_space_24 ? 1 : 0)); /* FLAGS */
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
		save_u64 (crp_030);				/* CRP */
		save_u64 (srp_030);				/* SRP */
		save_u32 (tt0_030);				/* TT0/AC0 */
		save_u32 (tt1_030);				/* TT1/AC1 */
		save_u32 (tc_030);				/* TCR */
		save_u16 (mmusr_030);			/* MMUSR/ACUSR */
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
			save_u16 (regs.prefetch020[i]);
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
	}
	if (currprefs.cpu_model >= 68020) {
		save_u32 (regs.ce020memcycles);
		save_u32 (regs.ce020tmpcycles);
	}
	*len = dst - dstbak;
	return dstbak;
}

uae_u8 *save_mmu (int *len, uae_u8 *dstptr)
{
	uae_u8 *dstbak, *dst;
	int model;

	model = currprefs.mmu_model;
	if (model != 68040 && model != 68060)
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
	write_log (L"MMU: %d\n", model);
	return src;
}

#endif /* SAVESTATE */

static void exception3f (uae_u32 opcode, uaecptr addr, int writeaccess, int instructionaccess)
{
	if (currprefs.cpu_model >= 68040)
		addr &= ~1;
	if (currprefs.cpu_model <= 68010)
		last_addr_for_exception_3 = m68k_getpc () + 2;
	else
		last_addr_for_exception_3 = regs.instruction_pc;
	last_fault_for_exception_3 = addr;
	last_op_for_exception_3 = opcode;
	last_writeaccess_for_exception_3 = writeaccess;
	last_instructionaccess_for_exception_3 = instructionaccess;
	Exception (3);
#if EXCEPTION3_DEBUG
	activate_debugger();
#endif
}

void exception3 (uae_u32 opcode, uaecptr addr)
{
	exception3f (opcode, addr, 0, 0);
}

void exception3i (uae_u32 opcode, uaecptr addr)
{
	exception3f (opcode, addr, 0, 1);
}

void exception2 (uaecptr addr)
{
	write_log (L"delayed exception2!\n");
	regs.panic_pc = m68k_getpc ();
	regs.panic_addr = addr;
	regs.panic = 2;
	set_special (SPCFLAG_BRK);
	m68k_setpc (0xf80000);
#ifdef JIT
	set_special (SPCFLAG_END_COMPILE);
#endif
	fill_prefetch ();
}

void cpureset (void)
{
	uaecptr pc;
	uaecptr ksboot = 0xf80002 - 2; /* -2 = RESET hasn't increased PC yet */
	uae_u16 ins;

	if (currprefs.cpu_compatible || currprefs.cpu_cycle_exact) {
		custom_reset (0);
		return;
	}
	pc = m68k_getpc ();
	if (pc >= currprefs.chipmem_size) {
		addrbank *b = &get_mem_bank (pc);
		if (b->check (pc, 2 + 2)) {
			/* We have memory, hope for the best.. */
			custom_reset (0);
			return;
		}
		write_log (L"M68K RESET PC=%x, rebooting..\n", pc);
		custom_reset (0);
		m68k_setpc (ksboot);
		return;
	}
	/* panic, RAM is going to disappear under PC */
	ins = get_word (pc + 2);
	if ((ins & ~7) == 0x4ed0) {
		int reg = ins & 7;
		uae_u32 addr = m68k_areg (regs, reg);
		write_log (L"reset/jmp (ax) combination emulated -> %x\n", addr);
		custom_reset (0);
		if (addr < 0x80000)
			addr += 0xf80000;
		m68k_setpc (addr - 2);
		return;
	}
	write_log (L"M68K RESET PC=%x, rebooting..\n", pc);
	custom_reset (0);
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
static void fill_icache020 (uae_u32 addr)
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
	data = mem_access_delay_longi_read_ce020 (addr);
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

	if (pc == regs.prefetch020addr) {
		uae_u32 v = regs.prefetch020[0];
		regs.prefetch020[0] = regs.prefetch020[1];
		regs.prefetch020[1] = regs.prefetch020[2];
		pc += 4 + 2;
		if (regs.cacheholdingaddr020 != (pc & ~3))
			fill_icache020 (pc);
		regs.prefetch020[2] = (regs.cacheholdingaddr020 == pc) ? (regs.cacheholdingdata020 >> 16) : (regs.cacheholdingdata020 >> 0);
		regs.prefetch020addr += 2;
		return v;
	} else {
		regs.prefetch020addr = pc;
		fill_icache020 (pc);
		regs.prefetch020[0] = (regs.cacheholdingaddr020 == pc) ? (regs.cacheholdingdata020 >> 16) : (regs.cacheholdingdata020 >> 0);
		pc += 2;
		fill_icache020 (pc);
		regs.prefetch020[1] = (regs.cacheholdingaddr020 == pc) ? (regs.cacheholdingdata020 >> 16) : (regs.cacheholdingdata020 >> 0);
		pc += 2;
		fill_icache020 (pc);
		regs.prefetch020[2] = (regs.cacheholdingaddr020 == pc) ? (regs.cacheholdingdata020 >> 16) : (regs.cacheholdingdata020 >> 0);
		return get_word_ce020_prefetch (o);
	}
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
	if (addr == regs.cacheholdingaddr020)
		return;
	c = getcache030 (icaches030, addr, &tag, &lws);
	if (c->valid[lws] && c->tag == tag) {
		// cache hit
		regs.cacheholdingaddr020 = addr;
		regs.cacheholdingdata020 = c->data[lws];
		return;
	}
	// cache miss
	data = mem_access_delay_longi_read_ce020 (addr);
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
		write_log (L"%08x %d %d %08x %08x %d\n", addr, aligned, size, val, tag1, lws1);
		if (aligned == 2)
			write_log (L"*\n");
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
		if (size == 2)
			return mem_access_delay_long_read_ce020 (addr);
		else if (size == 1)
			return mem_access_delay_word_read_ce020 (addr);
		else
			return mem_access_delay_byte_read_ce020 (addr);
	}

	c1 = getcache030 (dcaches030, addr, &tag1, &lws1);
	addr &= ~3;
	if (!c1->valid[lws1] || c1->tag != tag1) {
		v1 = mem_access_delay_long_read_ce020 (addr);
		update_cache030 (c1, v1, tag1, lws1);
	} else {
		v1 = c1->data[lws1];
		if (get_long (addr) != v1) {
			write_log (L"data cache mismatch %d %d %08x %08x != %08x %08x %d PC=%08x\n",
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
		v2 = mem_access_delay_long_read_ce020 (addr);
		update_cache030 (c2, v2, tag2, lws2);
	} else {
		v2 = c2->data[lws2];
		if (get_long (addr) != v2) {
			write_log (L"data cache mismatch %d %d %08x %08x != %08x %08x %d PC=%08x\n",
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

	write_log (L"dcache030 weirdness!?\n");
	return 0;
}

uae_u32 get_word_ce030_prefetch (int o)
{
	uae_u32 pc = m68k_getpc () + o;

	if (pc == regs.prefetch020addr) {
		uae_u32 v = regs.prefetch020[0];
		regs.prefetch020[0] = regs.prefetch020[1];
		regs.prefetch020[1] = regs.prefetch020[2];
		pc += 4 + 2;
		fill_icache030 (pc);
		regs.prefetch020[2] = (regs.cacheholdingaddr020 == pc) ? (regs.cacheholdingdata020 >> 16) : (regs.cacheholdingdata020 >> 0);
		regs.prefetch020addr += 2;
		fill_icache030 (pc + 4);
		return v;
	} else {
		regs.prefetch020addr = pc;
		fill_icache030 (pc);
		regs.prefetch020[0] = (regs.cacheholdingaddr020 == pc) ? (regs.cacheholdingdata020 >> 16) : (regs.cacheholdingdata020 >> 0);
		pc += 2;
		fill_icache030 (pc);
		regs.prefetch020[1] = (regs.cacheholdingaddr020 == pc) ? (regs.cacheholdingdata020 >> 16) : (regs.cacheholdingdata020 >> 0);
		pc += 2;
		fill_icache030 (pc);
		regs.prefetch020[2] = (regs.cacheholdingaddr020 == pc) ? (regs.cacheholdingdata020 >> 16) : (regs.cacheholdingdata020 >> 0);
		return get_word_ce030_prefetch (o);
	}
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

void m68k_do_rte_mmu (uaecptr a7)
{
	uae_u16 ssr = get_word_mmu (a7 + 8 + 4);
	if (ssr & MMU_SSW_CT) {
		uaecptr src_a7 = a7 + 8 - 8;
		uaecptr dst_a7 = a7 + 8 + 52;
		put_word_mmu (dst_a7 + 0, get_word_mmu (src_a7 + 0));
		put_long_mmu (dst_a7 + 2, get_long_mmu (src_a7 + 2));
		// skip this word
		put_long_mmu (dst_a7 + 8, get_long_mmu (src_a7 + 8));
	}
}

void flush_mmu (uaecptr addr, int n)
{
}

void m68k_do_rts_mmu (void)
{
	m68k_setpc (get_long_mmu (m68k_areg (regs, 7)));
	m68k_areg (regs, 7) += 4;
}

void m68k_do_bsr_mmu (uaecptr oldpc, uae_s32 offset)
{
	put_long_mmu (m68k_areg (regs, 7) - 4, oldpc);
	m68k_areg (regs, 7) -= 4;
	m68k_incpci (offset);
}
