/*
  * UAE - The Un*x Amiga Emulator
  *
  * MC68000 emulation
  *
  * (c) 1995 Bernd Schmidt
  */

#define MOVEC_DEBUG 0
#define MMUOP_DEBUG 1

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "events.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "cpu_prefetch.h"
#include "autoconf.h"
#include "traps.h"
#include "ersatz.h"
#include "debug.h"
#include "gui.h"
#include "savestate.h"
#include "blitter.h"
#include "ar.h"
#include "gayle.h"
#include "cia.h"

#ifdef JIT
extern uae_u8* compiled_code;
#include "compemu.h"
#include <signal.h>
/* For faster cycles handling */
signed long pissoff = 0;
/* Counter for missed vsyncmintime deadlines */
int gonebad = 0;
#else
/* Need to have these somewhere */
static void build_comp(void) {}
void check_prefs_changed_comp (void) {}
#endif

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
unsigned long irqcycles[15];
int irqdelay[15];
int mmu_enabled, mmu_triggered;
int cpu_cycles;

const int areg_byteinc[] = { 1,1,1,1,1,1,1,2 };
const int imm8_table[] = { 8,1,2,3,4,5,6,7 };

int movem_index1[256];
int movem_index2[256];
int movem_next[256];

#ifdef FPUEMU
#if 0
int fpp_movem_index1[256];
int fpp_movem_index2[256];
int fpp_movem_next[256];
#endif
#endif

cpuop_func *cpufunctbl[65536];

extern uae_u32 get_fpsr(void);

#define COUNT_INSTRS 0
#define MC68060_PCR   0x04300000
#define MC68EC060_PCR 0x04310000

static uae_u64 srp_030, crp_030;
static uae_u32 tt0_030, tt1_030, tc_030;
static uae_u16 mmusr_030;

#if COUNT_INSTRS
static unsigned long int instrcount[65536];
static uae_u16 opcodenums[65536];

static int compfn (const void *el1, const void *el2)
{
    return instrcount[*(const uae_u16 *)el1] < instrcount[*(const uae_u16 *)el2];
}

static char *icountfilename (void)
{
    char *name = getenv ("INSNCOUNT");
    if (name)
	return name;
    return COUNT_INSTRS == 2 ? "frequent.68k" : "insncount";
}

void dump_counts (void)
{
    FILE *f = fopen (icountfilename (), "w");
    unsigned long int total;
    int i;

    write_log ("Writing instruction count file...\n");
    for (i = 0; i < 65536; i++) {
	opcodenums[i] = i;
	total += instrcount[i];
    }
    qsort (opcodenums, 65536, sizeof(uae_u16), compfn);

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

static void set_cpu_caches(void)
{
#ifdef JIT
    if (currprefs.cpu_model < 68040) {
	set_cache_state(regs.cacr & 1);
	if (regs.cacr & 0x08) {
	    regs.cacr &= ~0x08;
	    flush_icache(1);
	}
    } else {
	set_cache_state((regs.cacr & 0x8000) ? 1 : 0);
    }
#endif
}

STATIC_INLINE void count_instr (unsigned int opcode)
{
}
    
static unsigned long REGPARAM3 op_illg_1 (uae_u32 opcode, struct regstruct *regs) REGPARAM;

static unsigned long REGPARAM2 op_illg_1 (uae_u32 opcode, struct regstruct *regs)
{
    op_illg (opcode, regs);
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
	break;
	case 68040:
	lvl = 4;
	tbl = op_smalltbl_1_ff;
	break;
	case 68030:
	lvl = 3;
	tbl = op_smalltbl_2_ff;
	break;
	case 68020:
	lvl = 2;
	tbl = op_smalltbl_3_ff;
	break;
	case 68010:
	lvl = 1;
	tbl = op_smalltbl_4_ff;
  	break;
#endif
#endif
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
	write_log ("no CPU emulation cores available!");
	abort ();
    }

    for (opcode = 0; opcode < 65536; opcode++)
	cpufunctbl[opcode] = op_illg_1;
    for (i = 0; tbl[i].handler != NULL; i++)
	cpufunctbl[tbl[i].opcode] = tbl[i].handler;

    opcnt = 0;
    for (opcode = 0; opcode < 65536; opcode++) {
	cpuop_func *f;

	if (table68k[opcode].mnemo == i_ILLG || table68k[opcode].clev > lvl)
	    continue;

	if (table68k[opcode].handler != -1) {
	    f = cpufunctbl[table68k[opcode].handler];
	    if (f == op_illg_1)
		abort();
	    cpufunctbl[opcode] = f;
	    opcnt++;
	}
    }
    write_log ("Building CPU, %d opcodes (%d %d %d). CPU=%d, FPU=%d, JIT=%d.\n",
	opcnt, lvl,
	currprefs.cpu_cycle_exact ? -1 : currprefs.cpu_compatible ? 1 : 0,
	currprefs.address_space_24, currprefs.cpu_model, currprefs.fpu_model,
	currprefs.cachesize);
#ifdef JIT
    build_comp ();
#endif
    set_cpu_caches ();
}

void fill_prefetch_slow (struct regstruct *regs)
{
#ifdef CPUEMU_12
    if (currprefs.cpu_cycle_exact) {
	regs->ir = get_word_ce (m68k_getpc (regs));
	regs->irc = get_word_ce (m68k_getpc (regs) + 2);
    } else {
#endif
	regs->ir = get_word (m68k_getpc (regs));
	regs->irc = get_word (m68k_getpc (regs) + 2);
#ifdef CPUEMU_12
    }
#endif
}

unsigned long cycles_mask, cycles_val;

static void update_68k_cycles (void)
{
    cycles_mask = 0;
    cycles_val = currprefs.m68k_speed;
    if (currprefs.m68k_speed < 1) {
	cycles_mask = 0xFFFFFFFF;
	cycles_val = 0;
    }
}

static void prefs_changed_cpu (void)
{
    fixup_cpu (&changed_prefs);
    currprefs.cpu_model = changed_prefs.cpu_model;
    currprefs.fpu_model = changed_prefs.fpu_model;
    currprefs.cpu_compatible = changed_prefs.cpu_compatible;
    currprefs.cpu_cycle_exact = changed_prefs.cpu_cycle_exact;
    currprefs.blitter_cycle_exact = changed_prefs.cpu_cycle_exact;
}

void check_prefs_changed_cpu (void)
{
    int changed = 0;

#ifdef JIT
    changed = check_prefs_changed_comp ();
#endif
    if (changed
	|| currprefs.cpu_model != changed_prefs.cpu_model
	|| currprefs.fpu_model != changed_prefs.fpu_model
	|| currprefs.cpu_compatible != changed_prefs.cpu_compatible
	|| currprefs.cpu_cycle_exact != changed_prefs.cpu_cycle_exact) {

	prefs_changed_cpu ();
	if (!currprefs.cpu_compatible && changed_prefs.cpu_compatible)
	    fill_prefetch_slow (&regs);
	build_cpufunctbl ();
	changed = 1;
    }
    if (changed || currprefs.m68k_speed != changed_prefs.m68k_speed) {
	currprefs.m68k_speed = changed_prefs.m68k_speed;
	reset_frame_rate_hack ();
	update_68k_cycles ();
    }
    if (currprefs.cpu_idle != changed_prefs.cpu_idle) {
	currprefs.cpu_idle = changed_prefs.cpu_idle;
    }
    if (changed)
	set_special (&regs, SPCFLAG_BRK);

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
#ifdef FPUEMU
#if 0
    for (i = 0 ; i < 256 ; i++) {
	int j;
	for (j = 7 ; j >= 0 ; j--) {
		if (i & (1 << j)) break;
	}
	fpp_movem_index1[i] = 7-j;
	fpp_movem_index2[i] = j;
	fpp_movem_next[i] = i & (~(1 << j));
    }
#endif
#endif
#if COUNT_INSTRS
    {
	FILE *f = fopen (icountfilename (), "r");
	memset (instrcount, 0, sizeof instrcount);
	if (f) {
	    uae_u32 opcode, count, total;
	    char name[20];
	    write_log ("Reading instruction count file...\n");
	    fscanf (f, "Total: %lu\n", &total);
	    while (fscanf (f, "%lx: %lu %s\n", &opcode, &count, name) == 3) {
		instrcount[opcode] = count;
	    }
	    fclose(f);
	}
    }
#endif
    write_log ("Building CPU table for configuration: %d", currprefs.cpu_model);
    regs.address_space_mask = 0xffffffff;
    if (currprefs.cpu_compatible > 0) {
	if (currprefs.address_space_24 && currprefs.cpu_model >= 68030)
	    currprefs.address_space_24 = 0;
    }
    if (currprefs.fpu_model > 0)
	write_log("/%d", currprefs.fpu_model);
    if (currprefs.cpu_cycle_exact) {
	if (currprefs.cpu_model == 68000)
	    write_log (" prefetch and cycle-exact");
	else
	    write_log (" ~cycle-exact");
    } else if (currprefs.cpu_compatible)
	write_log (" prefetch");
    if (currprefs.address_space_24) {
	regs.address_space_mask = 0x00ffffff;
	write_log (" 24-bit");
    }
    write_log ("\n");

    read_table68k ();
    do_merges ();

    write_log ("%d CPU functions\n", nr_cpuop_funcs);

    build_cpufunctbl ();

#ifdef JIT
    /* We need to check whether NATMEM settings have changed
     * before starting the CPU */
    check_prefs_changed_comp ();
#endif
}

struct regstruct regs, mmu_backup_regs;
static struct regstruct regs_backup[16];
static int backup_pointer = 0;
static long int m68kpc_offset;

#define get_ibyte_1(o) get_byte(regs.pc + (regs.pc_p - regs.pc_oldp) + (o) + 1)
#define get_iword_1(o) get_word(regs.pc + (regs.pc_p - regs.pc_oldp) + (o))
#define get_ilong_1(o) get_long(regs.pc + (regs.pc_p - regs.pc_oldp) + (o))

static uae_s32 ShowEA (void *f, uae_u16 opcode, int reg, amodes mode, wordsizes size, char *buf, uae_u32 *eaddr, int safemode)
{
    uae_u16 dp;
    uae_s8 disp8;
    uae_s16 disp16;
    int r;
    uae_u32 dispreg;
    uaecptr addr = 0;
    uae_s32 offset = 0;
    char buffer[80];

    switch (mode){
     case Dreg:
	sprintf (buffer,"D%d", reg);
	break;
     case Areg:
	sprintf (buffer,"A%d", reg);
	break;
     case Aind:
	sprintf (buffer,"(A%d)", reg);
	addr = regs.regs[reg + 8];
	break;
     case Aipi:
	sprintf (buffer,"(A%d)+", reg);
	addr = regs.regs[reg + 8];
	break;
     case Apdi:
	sprintf (buffer,"-(A%d)", reg);
	addr = regs.regs[reg + 8];
	break;
     case Ad16:
	{
	    char offtxt[80];
	    disp16 = get_iword_1 (m68kpc_offset); m68kpc_offset += 2;
	    if (disp16 < 0)
		sprintf (offtxt, "-$%04x", -disp16);
	    else
		sprintf (offtxt, "$%04x", disp16);
	    addr = m68k_areg(&regs,reg) + disp16;
	    sprintf (buffer,"(A%d,%s) == $%08lx", reg, offtxt, (unsigned long)addr);
	}
	break;
     case Ad8r:
	dp = get_iword_1 (m68kpc_offset); m68kpc_offset += 2;
	disp8 = dp & 0xFF;
	r = (dp & 0x7000) >> 12;
	dispreg = dp & 0x8000 ? m68k_areg(&regs,r) : m68k_dreg(&regs,r);
	if (!(dp & 0x800)) dispreg = (uae_s32)(uae_s16)(dispreg);
	dispreg <<= (dp >> 9) & 3;

	if (dp & 0x100) {
	    uae_s32 outer = 0, disp = 0;
	    uae_s32 base = m68k_areg(&regs,reg);
	    char name[10];
	    sprintf (name,"A%d, ",reg);
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
	    sprintf (buffer,"(%s%c%d.%c*%d+%ld)+%ld == $%08lx", name,
		    dp & 0x8000 ? 'A' : 'D', (int)r, dp & 0x800 ? 'L' : 'W',
		    1 << ((dp >> 9) & 3),
		    disp,outer,
		    (unsigned long)addr);
	} else {
	  addr = m68k_areg(&regs,reg) + (uae_s32)((uae_s8)disp8) + dispreg;
	  sprintf (buffer,"(A%d, %c%d.%c*%d, $%02x) == $%08lx", reg,
	       dp & 0x8000 ? 'A' : 'D', (int)r, dp & 0x800 ? 'L' : 'W',
	       1 << ((dp >> 9) & 3), disp8,
	       (unsigned long)addr);
	}
	break;
     case PC16:
	addr = m68k_getpc (&regs) + m68kpc_offset;
	disp16 = get_iword_1 (m68kpc_offset); m68kpc_offset += 2;
	addr += (uae_s16)disp16;
	sprintf (buffer,"(PC,$%04x) == $%08lx", disp16 & 0xffff,(unsigned long)addr);
	break;
     case PC8r:
	addr = m68k_getpc (&regs) + m68kpc_offset;
	dp = get_iword_1 (m68kpc_offset); m68kpc_offset += 2;
	disp8 = dp & 0xFF;
	r = (dp & 0x7000) >> 12;
	dispreg = dp & 0x8000 ? m68k_areg(&regs,r) : m68k_dreg(&regs,r);
	if (!(dp & 0x800)) dispreg = (uae_s32)(uae_s16)(dispreg);
	dispreg <<= (dp >> 9) & 3;

	if (dp & 0x100) {
	    uae_s32 outer = 0,disp = 0;
	    uae_s32 base = addr;
	    char name[10];
	    sprintf (name,"PC, ");
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
	    sprintf (buffer,"(%s%c%d.%c*%d+%ld)+%ld == $%08lx", name,
		    dp & 0x8000 ? 'A' : 'D', (int)r, dp & 0x800 ? 'L' : 'W',
		    1 << ((dp >> 9) & 3),
		    disp,outer,
		    (unsigned long)addr);
	} else {
	  addr += (uae_s32)((uae_s8)disp8) + dispreg;
	  sprintf (buffer,"(PC, %c%d.%c*%d, $%02x) == $%08lx", dp & 0x8000 ? 'A' : 'D',
		(int)r, dp & 0x800 ? 'L' : 'W',  1 << ((dp >> 9) & 3),
		disp8, (unsigned long)addr);
	}
	break;
     case absw:
	addr = (uae_s32)(uae_s16)get_iword_1 (m68kpc_offset);
	sprintf (buffer,"$%08lx", (unsigned long)addr);
	m68kpc_offset += 2;
	break;
     case absl:
	addr = get_ilong_1 (m68kpc_offset);
	sprintf (buffer,"$%08lx", (unsigned long)addr);
	m68kpc_offset += 4;
	break;
     case imm:
	switch (size){
	 case sz_byte:
	    sprintf (buffer,"#$%02x", (unsigned int)(get_iword_1 (m68kpc_offset) & 0xff));
	    m68kpc_offset += 2;
	    break;
	 case sz_word:
	    sprintf (buffer,"#$%04x", (unsigned int)(get_iword_1 (m68kpc_offset) & 0xffff));
	    m68kpc_offset += 2;
	    break;
	 case sz_long:
	    sprintf (buffer,"#$%08lx", (unsigned long)(get_ilong_1 (m68kpc_offset)));
	    m68kpc_offset += 4;
	    break;
	 default:
	    break;
	}
	break;
     case imm0:
	offset = (uae_s32)(uae_s8)get_iword_1 (m68kpc_offset);
	m68kpc_offset += 2;
	sprintf (buffer,"#$%02x", (unsigned int)(offset & 0xff));
	break;
     case imm1:
	offset = (uae_s32)(uae_s16)get_iword_1 (m68kpc_offset);
	m68kpc_offset += 2;
	buffer[0] = 0;
	sprintf (buffer,"#$%04x", (unsigned int)(offset & 0xffff));
	break;
     case imm2:
	offset = (uae_s32)get_ilong_1 (m68kpc_offset);
	m68kpc_offset += 4;
	sprintf (buffer,"#$%08lx", (unsigned long)offset);
	break;
     case immi:
	offset = (uae_s32)(uae_s8)(reg & 0xff);
	sprintf (buffer,"#$%08lx", (unsigned long)offset);
	break;
     default:
	break;
    }
    if (buf == 0)
	f_out (f, "%s", buffer);
    else
	strcat (buf, buffer);
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
	*val = m68k_dreg (&regs, reg);
	return 1;
     case Areg:
	*val = m68k_areg (&regs, reg);
	return 1;

     case Aind:
     case Aipi:
	addr = m68k_areg (&regs, reg);
	break;
     case Apdi:
	addr = m68k_areg (&regs, reg);
	break;
     case Ad16:
	disp16 = get_iword_1 (m68kpc_offset); m68kpc_offset += 2;
	addr = m68k_areg(&regs,reg) + (uae_s16)disp16;
	break;
     case Ad8r:
	addr = m68k_areg (&regs, reg);
     d8r_common:
	dp = get_iword_1 (m68kpc_offset); m68kpc_offset += 2;
	disp8 = dp & 0xFF;
	r = (dp & 0x7000) >> 12;
	dispreg = dp & 0x8000 ? m68k_areg(&regs,r) : m68k_dreg(&regs,r);
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
	addr = m68k_getpc (&regs) + m68kpc_offset;
	disp16 = get_iword_1 (m68kpc_offset); m68kpc_offset += 2;
	addr += (uae_s16)disp16;
	break;
     case PC8r:
	addr = m68k_getpc (&regs) + m68kpc_offset;
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

    last_addr_for_exception_3 = m68k_getpc (&regs) + m68kpc_offset;
    last_fault_for_exception_3 = addr;
    last_writeaccess_for_exception_3 = 0;
    last_instructionaccess_for_exception_3 = 0;
    return 0;
}
#endif

int get_cpu_model(void)
{
    return currprefs.cpu_model;
}

uae_u32 REGPARAM2 get_disp_ea_020 (struct regstruct *regs, uae_u32 base, uae_u32 dp)
{
    int reg = (dp >> 12) & 15;
    uae_s32 regd = regs->regs[reg];
    if ((dp & 0x800) == 0)
	regd = (uae_s32)(uae_s16)regd;
    regd <<= (dp >> 9) & 3;
    if (dp & 0x100) {
	uae_s32 outer = 0;
	if (dp & 0x80) base = 0;
	if (dp & 0x40) regd = 0;

	if ((dp & 0x30) == 0x20) base += (uae_s32)(uae_s16)next_iword(regs);
	if ((dp & 0x30) == 0x30) base += next_ilong(regs);

	if ((dp & 0x3) == 0x2) outer = (uae_s32)(uae_s16)next_iword(regs);
	if ((dp & 0x3) == 0x3) outer = next_ilong(regs);

	if ((dp & 0x4) == 0) base += regd;
	if (dp & 0x3) base = get_long (base);
	if (dp & 0x4) base += regd;

	return base + outer;
    } else {
	return base + (uae_s32)((uae_s8)dp) + regd;
    }
}

uae_u32 REGPARAM3 get_disp_ea_000 (struct regstruct *regs, uae_u32 base, uae_u32 dp) REGPARAM
{
    int reg = (dp >> 12) & 15;
    uae_s32 regd = regs->regs[reg];
#if 1
    if ((dp & 0x800) == 0)
	regd = (uae_s32)(uae_s16)regd;
    return base + (uae_s8)dp + regd;
#else
    /* Branch-free code... benchmark this again now that
     * things are no longer inline.  */
    uae_s32 regd16;
    uae_u32 mask;
    mask = ((dp & 0x800) >> 11) - 1;
    regd16 = (uae_s32)(uae_s16)regd;
    regd16 &= mask;
    mask = ~mask;
    base += (uae_s8)dp;
    regd &= mask;
    regd |= regd16;
    return base + regd;
#endif
}

void REGPARAM2 MakeSR (struct regstruct *regs)
{
#if 0
    assert((regs.t1 & 1) == regs->t1);
    assert((regs.t0 & 1) == regs->t0);
    assert((regs.s & 1) == regs->s);
    assert((regs.m & 1) == regs->m);
    assert((XFLG & 1) == XFLG);
    assert((NFLG & 1) == NFLG);
    assert((ZFLG & 1) == ZFLG);
    assert((VFLG & 1) == VFLG);
    assert((CFLG & 1) == CFLG);
#endif
    regs->sr = ((regs->t1 << 15) | (regs->t0 << 14)
	       | (regs->s << 13) | (regs->m << 12) | (regs->intmask << 8)
	       | (GET_XFLG(&regs->ccrflags) << 4) | (GET_NFLG(&regs->ccrflags) << 3)
	       | (GET_ZFLG(&regs->ccrflags) << 2) | (GET_VFLG(&regs->ccrflags) << 1)
	       |  GET_CFLG(&regs->ccrflags));
}

void REGPARAM2 MakeFromSR (struct regstruct *regs)
{
    int oldm = regs->m;
    int olds = regs->s;

    SET_XFLG (&regs->ccrflags, (regs->sr >> 4) & 1);
    SET_NFLG (&regs->ccrflags, (regs->sr >> 3) & 1);
    SET_ZFLG (&regs->ccrflags, (regs->sr >> 2) & 1);
    SET_VFLG (&regs->ccrflags, (regs->sr >> 1) & 1);
    SET_CFLG (&regs->ccrflags, regs->sr & 1);
    if (regs->t1 == ((regs->sr >> 15) & 1) &&
	regs->t0 == ((regs->sr >> 14) & 1) &&
	regs->s  == ((regs->sr >> 13) & 1) &&
	regs->m  == ((regs->sr >> 12) & 1) &&
	regs->intmask == ((regs->sr >> 8) & 7))
	    return;
    regs->t1 = (regs->sr >> 15) & 1;
    regs->t0 = (regs->sr >> 14) & 1;
    regs->s  = (regs->sr >> 13) & 1;
    regs->m  = (regs->sr >> 12) & 1;
    regs->intmask = (regs->sr >> 8) & 7;
    if (currprefs.cpu_model >= 68020) {
	 /* 68060 does not have MSP but does have M-bit.. */
	if (currprefs.cpu_model >= 68060)
	    regs->msp = regs->isp;
	if (olds != regs->s) {
	    if (olds) {
		if (oldm)
		    regs->msp = m68k_areg(regs, 7);
		else
		    regs->isp = m68k_areg(regs, 7);
		m68k_areg(regs, 7) = regs->usp;
	    } else {
		regs->usp = m68k_areg(regs, 7);
		m68k_areg(regs, 7) = regs->m ? regs->msp : regs->isp;
	    }
	} else if (olds && oldm != regs->m) {
	    if (oldm) {
		regs->msp = m68k_areg (regs, 7);
		m68k_areg (regs, 7) = regs->isp;
	    } else {
		regs->isp = m68k_areg (regs, 7);
		m68k_areg (regs, 7) = regs->msp;
	    }
	}
	if (currprefs.cpu_model >= 68060)
	    regs->t0 = 0;
    } else {
	regs->t0 = regs->m = 0;
	if (olds != regs->s) {
	    if (olds) {
		regs->isp = m68k_areg (regs, 7);
		m68k_areg (regs, 7) = regs->usp;
	    } else {
		regs->usp = m68k_areg (regs, 7);
		m68k_areg (regs, 7) = regs->isp;
	    }
	}
    }

    doint();
    if (regs->t1 || regs->t0)
	set_special (regs, SPCFLAG_TRACE);
    else
	/* Keep SPCFLAG_DOTRACE, we still want a trace exception for
	   SR-modifying instructions (including STOP).  */
	unset_special (regs, SPCFLAG_TRACE);
}

static void exception_trace (int nr)
{
    unset_special (&regs, SPCFLAG_TRACE | SPCFLAG_DOTRACE);
    if (regs.t1 && !regs.t0) {
	/* trace stays pending if exception is div by zero, chk,
	 * trapv or trap #x
	 */
	if (nr == 5 || nr == 6 || nr ==  7 || (nr >= 32 && nr <= 47))
	    set_special (&regs, SPCFLAG_DOTRACE);
    }
    regs.t1 = regs.t0 = regs.m = 0;
}

static void exception_debug (int nr)
{
#ifdef DEBUGGER
    if (!exception_debugging)
	return;
    console_out ("Exception %d, PC=%08.8X\n", nr, m68k_getpc(&regs));
#endif
}

#ifdef CPUEMU_12

/* cycle-exact exception handler, 68000 only */

static void Exception_ce (int nr, struct regstruct *regs, uaecptr oldpc)
{
    uae_u32 currpc = m68k_getpc (regs), newpc;
    int c;
    int sv = regs->s;

    exception_debug (nr);
    MakeSR(regs);

    c = 0;
    switch (nr)
    {
	case 2: /* bus */
	case 3: /* address */
	c = 6;
	break;
	case 4: /* illegal instruction */
	c = 6;
	break;
	case 5: /* divide by zero */
	c = 10;
	break;
	case 6: /* chk */
	c = 12;
	break;
	case 7: /* trapv */
	c = 6;
	break;
	case 8: /* privilege */
	c = 6;
	break;
	case 9: /* trace */
	c = 6;
	break;
	case 25: /* interrupts */
	case 26:
	case 27:
	case 28:
	case 29:
	case 30:
	case 31:
	c = 12;
	break;
	case 32: /* traps */
	case 33:
	case 34:
	case 35:
	case 36:
	case 37:
	case 38:
	case 39:
	case 40:
	case 41:
	case 42:
	case 43:
	case 44:
	case 45:
	case 46:
	case 47:
	c = 6;
	break;
    }
    /* some delays are interleaved with stack pushes, not bothered yet..
     */
    if (c)
	do_cycles_ce (c * CYCLE_UNIT / 2);
    if (!regs->s) {
	regs->usp = m68k_areg(regs, 7);
	m68k_areg(regs, 7) = regs->isp;
	regs->s = 1;
    }
    if (nr == 2 || nr == 3) { /* 2=bus error,3=address error */
	uae_u16 mode = (sv ? 4 : 0) | (last_instructionaccess_for_exception_3 ? 2 : 1);
	mode |= last_writeaccess_for_exception_3 ? 0 : 16;
	m68k_areg(regs, 7) -= 14;
	/* fixme: bit3=I/N */
	put_word_ce (m68k_areg(regs, 7) + 12, last_addr_for_exception_3);
	put_word_ce (m68k_areg(regs, 7) + 8, regs->sr);
	put_word_ce (m68k_areg(regs, 7) + 10, last_addr_for_exception_3 >> 16);
	put_word_ce (m68k_areg(regs, 7) + 6, last_op_for_exception_3);
	put_word_ce (m68k_areg(regs, 7) + 4, last_fault_for_exception_3);
	put_word_ce (m68k_areg(regs, 7) + 0, mode);
	put_word_ce (m68k_areg(regs, 7) + 2, last_fault_for_exception_3 >> 16);
	write_log ("Exception %d (%x) at %x -> %x!\n", nr, oldpc, currpc, get_long (4 * nr));
	goto kludge_me_do;
    }
    m68k_areg (regs, 7) -= 6;
    put_word_ce (m68k_areg(regs, 7) + 4, currpc);
    put_word_ce (m68k_areg(regs, 7) + 0, regs->sr);
    put_word_ce (m68k_areg(regs, 7) + 2, currpc >> 16);
kludge_me_do:
    newpc = get_word_ce (4 * nr) << 16;
    newpc |= get_word_ce (4 * nr + 2);
    if (newpc & 1) {
	if (nr == 2 || nr == 3)
	    uae_reset (1); /* there is nothing else we can do.. */
	else
	    exception3 (regs->ir, m68k_getpc(regs), newpc);
	return;
    }
    m68k_setpc (regs, newpc);
    fill_prefetch_slow (regs);
    set_special(regs, SPCFLAG_END_COMPILE);
    exception_trace (nr);
}
#endif

static void Exception_normal (int nr, struct regstruct *regs, uaecptr oldpc)
{
    uae_u32 currpc = m68k_getpc (regs), newpc;
    int sv = regs->s;

    exception_debug (nr);
    MakeSR(regs);

    if (!regs->s) {
	regs->usp = m68k_areg(regs, 7);
	if (currprefs.cpu_model >= 68020)
	    m68k_areg(regs, 7) = regs->m ? regs->msp : regs->isp;
	else
	    m68k_areg(regs, 7) = regs->isp;
	regs->s = 1;
    }
    if (currprefs.cpu_model > 68000) {
	if (nr == 2 || nr == 3) {
	    int i;
	    if (currprefs.cpu_model >= 68040) {
		if (nr == 2) {
		    for (i = 0 ; i < 18 ; i++) {
			m68k_areg(regs, 7) -= 2;
			put_word (m68k_areg(regs, 7), 0);
		    }
		    m68k_areg(regs, 7) -= 4;
		    put_long (m68k_areg(regs, 7), last_fault_for_exception_3);
		    m68k_areg(regs, 7) -= 2;
		    put_word (m68k_areg(regs, 7), 0);
		    m68k_areg(regs, 7) -= 2;
		    put_word (m68k_areg(regs, 7), 0);
		    m68k_areg(regs, 7) -= 2;
		    put_word (m68k_areg(regs, 7), 0);
		    m68k_areg(regs, 7) -= 2;
		    put_word (m68k_areg(regs, 7), 0x0140 | (sv ? 6 : 2)); /* SSW */
		    m68k_areg(regs, 7) -= 4;
		    put_long (m68k_areg(regs, 7), last_addr_for_exception_3);
		    m68k_areg(regs, 7) -= 2;
		    put_word (m68k_areg(regs, 7), 0x7000 + nr * 4);
		} else {
		    m68k_areg(regs, 7) -= 4;
		    put_long (m68k_areg(regs, 7), last_fault_for_exception_3);
		    m68k_areg(regs, 7) -= 2;
		    put_word (m68k_areg(regs, 7), 0x2000 + nr * 4);
		}
	    } else {
		uae_u16 ssw = (sv ? 4 : 0) | (last_instructionaccess_for_exception_3 ? 2 : 1);
		ssw |= last_writeaccess_for_exception_3 ? 0 : 0x40;
		ssw |= 0x20;
		for (i = 0 ; i < 36; i++) {
		    m68k_areg(regs, 7) -= 2;
		    put_word (m68k_areg(regs, 7), 0);
		}
		m68k_areg(regs, 7) -= 4;
		put_long (m68k_areg(regs, 7), last_fault_for_exception_3);
		m68k_areg(regs, 7) -= 2;
		put_word (m68k_areg(regs, 7), 0);
		m68k_areg(regs, 7) -= 2;
		put_word (m68k_areg(regs, 7), 0);
		m68k_areg(regs, 7) -= 2;
		put_word (m68k_areg(regs, 7), 0);
		m68k_areg(regs, 7) -= 2;
		put_word (m68k_areg(regs, 7), ssw);
		m68k_areg(regs, 7) -= 2;
		put_word (m68k_areg(regs, 7), 0xb000 + nr * 4);
	    }
	    write_log ("Exception %d (%x) at %x -> %x!\n", nr, oldpc, currpc, get_long (regs->vbr + 4*nr));
	} else if (nr ==5 || nr == 6 || nr == 7 || nr == 9) {
	    m68k_areg(regs, 7) -= 4;
	    put_long (m68k_areg(regs, 7), oldpc);
	    m68k_areg(regs, 7) -= 2;
	    put_word (m68k_areg(regs, 7), 0x2000 + nr * 4);
	} else if (regs->m && nr >= 24 && nr < 32) { /* M + Interrupt */
	    m68k_areg(regs, 7) -= 2;
	    put_word (m68k_areg(regs, 7), nr * 4);
	    m68k_areg(regs, 7) -= 4;
	    put_long (m68k_areg(regs, 7), currpc);
	    m68k_areg(regs, 7) -= 2;
	    put_word (m68k_areg(regs, 7), regs->sr);
	    regs->sr |= (1 << 13);
	    regs->msp = m68k_areg(regs, 7);
	    m68k_areg(regs, 7) = regs->isp;
	    m68k_areg(regs, 7) -= 2;
	    put_word (m68k_areg(regs, 7), 0x1000 + nr * 4);
	} else {
	    m68k_areg(regs, 7) -= 2;
	    put_word (m68k_areg(regs, 7), nr * 4);
	}
    } else if (nr == 2 || nr == 3) {
	uae_u16 mode = (sv ? 4 : 0) | (last_instructionaccess_for_exception_3 ? 2 : 1);
	mode |= last_writeaccess_for_exception_3 ? 0 : 16;
	m68k_areg(regs, 7) -= 14;
	/* fixme: bit3=I/N */
	put_word (m68k_areg(regs, 7) + 0, mode);
	put_long (m68k_areg(regs, 7) + 2, last_fault_for_exception_3);
	put_word (m68k_areg(regs, 7) + 6, last_op_for_exception_3);
	put_word (m68k_areg(regs, 7) + 8, regs->sr);
	put_long (m68k_areg(regs, 7) + 10, last_addr_for_exception_3);
	write_log ("Exception %d (%x) at %x -> %x!\n", nr, oldpc, currpc, get_long (regs->vbr + 4*nr));
	goto kludge_me_do;
    }
    m68k_areg(regs, 7) -= 4;
    put_long (m68k_areg(regs, 7), currpc);
    m68k_areg(regs, 7) -= 2;
    put_word (m68k_areg(regs, 7), regs->sr);
kludge_me_do:
    newpc = get_long (regs->vbr + 4 * nr);
    if (newpc & 1) {
	if (nr == 2 || nr == 3)
	    uae_reset (1); /* there is nothing else we can do.. */
	else
	    exception3 (regs->ir, m68k_getpc(regs), newpc);
	return;
    }
    m68k_setpc (regs, newpc);
    set_special(regs, SPCFLAG_END_COMPILE);
    fill_prefetch_slow (regs);
    exception_trace (nr);
}

void REGPARAM2 Exception (int nr, struct regstruct *regs, uaecptr oldpc)
{
#if 0
    if (1 || nr < 24)
	write_log ("exception %d %08.8X %08.8X (%04.4X %04.4X)\n",
	    nr, oldpc, m68k_getpc(regs), intena, intreq);
#endif
#ifdef CPUEMU_12
    if (currprefs.cpu_cycle_exact && currprefs.cpu_model == 68000)
	Exception_ce (nr, regs, oldpc);
    else
#endif
	Exception_normal (nr, regs, oldpc);
}

STATIC_INLINE void do_interrupt(int nr, struct regstruct *regs)
{
#if 0
    if (nr == 2)
	write_log(".");
	//write_log("irq %d at %x (%04.4X) ", nr, m68k_getpc(regs), intena & intreq);
#endif
    regs->stopped = 0;
    unset_special (regs, SPCFLAG_STOP);
    assert(nr < 8 && nr >= 0);
    Exception (nr + 24, regs, 0);

    regs->intmask = nr;
    doint();
}

void NMI(void)
{
    do_interrupt (7, &regs);
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
	if (regno == 3) return 1; /* 68040/060 only */
	 /* 4 is >=68040, but 0x804 is in 68020 */
	 if (regno2 < 4 || regno == 0x804)
	    return 0;
	return 1;
    } else if (currprefs.cpu_model == 68030) {
	if (regno2 <= 2) return 0;
	if (regno == 0x803 || regno == 0x804)
	    return 0;
	return 1;
    } else if (currprefs.cpu_model == 68040) {
	if (regno == 0x802) return 1; /* 68020 only */
	if (regno2 < 8) return 0;
	return 1;
    }
    return 1;
}

int m68k_move2c (int regno, uae_u32 *regp)
{
#if MOVEC_DEBUG > 0
    write_log("move2c %04.4X <- %08.8X PC=%x\n", regno, *regp, M68K_GETPC);
#endif
    if (movec_illg (regno)) {
	op_illg (0x4E7B, &regs);
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
	    set_cpu_caches();
	}
	break;
	 /* 68040/060 only */
	case 3: regs.tcr = *regp & (currprefs.cpu_model == 68060 ? 0xfffe : 0xc000);
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
	case 0x803: regs.msp = *regp; if (regs.m == 1) m68k_areg(&regs, 7) = regs.msp; break;
	case 0x804: regs.isp = *regp; if (regs.m == 0) m68k_areg(&regs, 7) = regs.isp; break;
	/* 68040 only */
	case 0x805: regs.mmusr = *regp; break;
	/* 68040/060 */
	case 0x806: regs.urp = *regp; break;
	case 0x807: regs.srp = *regp; break;
	/* 68060 only */
	case 0x808:
	{
	    uae_u32 opcr = regs.pcr;
	    regs.pcr &= ~(0x40 | 2 | 1);
	    regs.pcr |= (*regp) & (0x40 | 2 | 1);
	    if (((opcr ^ regs.pcr) & 2) == 2) {
		write_log("68060 FPU state: %s\n", regs.pcr & 2 ? "disabled" : "enabled");
		/* flush possible already translated FPU instructions */
		flush_icache(2);
	    }
	}
	break;
	default:
	    op_illg (0x4E7B, &regs);
	    return 0;
	}
    }
    return 1;
}

int m68k_movec2 (int regno, uae_u32 *regp)
{
#if MOVEC_DEBUG > 0
    write_log("movec2 %04.4X PC=%x\n", regno, M68K_GETPC);
#endif
    if (movec_illg (regno)) {
	op_illg (0x4E7A, &regs);
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
	case 0x803: *regp = regs.m == 1 ? m68k_areg(&regs, 7) : regs.msp; break;
	case 0x804: *regp = regs.m == 0 ? m68k_areg(&regs, 7) : regs.isp; break;
	case 0x805: *regp = regs.mmusr; break;
	case 0x806: *regp = regs.urp; break;
	case 0x807: *regp = regs.srp; break;
	case 0x808: *regp = regs.pcr; break;

	default:
	    op_illg (0x4E7A, &regs);
	    return 0;
	}
    }
#if MOVEC_DEBUG > 0
    write_log("-> %08.8X\n", *regp);
#endif
    return 1;
}

STATIC_INLINE int
div_unsigned(uae_u32 src_hi, uae_u32 src_lo, uae_u32 div, uae_u32 *quot, uae_u32 *rem)
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

void m68k_divl (uae_u32 opcode, uae_u32 src, uae_u16 extra, uaecptr oldpc)
{
#if defined(uae_s64)
    if (src == 0) {
	Exception (5, &regs, oldpc);
	return;
    }
    if (extra & 0x800) {
	/* signed variant */
	uae_s64 a = (uae_s64)(uae_s32)m68k_dreg(&regs, (extra >> 12) & 7);
	uae_s64 quot, rem;

	if (extra & 0x400) {
	    a &= 0xffffffffu;
	    a |= (uae_s64)m68k_dreg(&regs, extra & 7) << 32;
	}
	rem = a % (uae_s64)(uae_s32)src;
	quot = a / (uae_s64)(uae_s32)src;
	if ((quot & UVAL64(0xffffffff80000000)) != 0
	    && (quot & UVAL64(0xffffffff80000000)) != UVAL64(0xffffffff80000000))
	{
	    SET_VFLG (&regs.ccrflags, 1);
	    SET_NFLG (&regs.ccrflags, 1);
	    SET_CFLG (&regs.ccrflags, 0);
	} else {
	    if (((uae_s32)rem < 0) != ((uae_s64)a < 0)) rem = -rem;
	    SET_VFLG (&regs.ccrflags, 0);
	    SET_CFLG (&regs.ccrflags, 0);
	    SET_ZFLG (&regs.ccrflags, ((uae_s32)quot) == 0);
	    SET_NFLG (&regs.ccrflags, ((uae_s32)quot) < 0);
	    m68k_dreg(&regs, extra & 7) = (uae_u32)rem;
	    m68k_dreg(&regs, (extra >> 12) & 7) = (uae_u32)quot;
	}
    } else {
	/* unsigned */
	uae_u64 a = (uae_u64)(uae_u32)m68k_dreg(&regs, (extra >> 12) & 7);
	uae_u64 quot, rem;

	if (extra & 0x400) {
	    a &= 0xffffffffu;
	    a |= (uae_u64)m68k_dreg(&regs, extra & 7) << 32;
	}
	rem = a % (uae_u64)src;
	quot = a / (uae_u64)src;
	if (quot > 0xffffffffu) {
	    SET_VFLG (&regs.ccrflags, 1);
	    SET_NFLG (&regs.ccrflags, 1);
	    SET_CFLG (&regs.ccrflags, 0);
	} else {
	    SET_VFLG (&regs.ccrflags, 0);
	    SET_CFLG (&regs.ccrflags, 0);
	    SET_ZFLG (&regs.ccrflags, ((uae_s32)quot) == 0);
	    SET_NFLG (&regs.ccrflags, ((uae_s32)quot) < 0);
	    m68k_dreg(&regs, extra & 7) = (uae_u32)rem;
	    m68k_dreg(&regs, (extra >> 12) & 7) = (uae_u32)quot;
	}
    }
#else
    if (src == 0) {
	Exception (5, &regs, oldpc);
	return;
    }
    if (extra & 0x800) {
	/* signed variant */
	uae_s32 lo = (uae_s32)m68k_dreg(&regs, (extra >> 12) & 7);
	uae_s32 hi = lo < 0 ? -1 : 0;
	uae_s32 save_high;
	uae_u32 quot, rem;
	uae_u32 sign;

	if (extra & 0x400) {
	    hi = (uae_s32)m68k_dreg(&regs, extra & 7);
	}
	save_high = hi;
	sign = (hi ^ src);
	if (hi < 0) {
	    hi = ~hi;
	    lo = -lo;
	    if (lo == 0) hi++;
	}
	if ((uae_s32)src < 0) src = -src;
	if (div_unsigned(hi, lo, src, &quot, &rem) ||
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
	    m68k_dreg(regs, extra & 7) = rem;
	    m68k_dreg(regs, (extra >> 12) & 7) = quot;
	}
    } else {
	/* unsigned */
	uae_u32 lo = (uae_u32)m68k_dreg(&regs, (extra >> 12) & 7);
	uae_u32 hi = 0;
	uae_u32 quot, rem;

	if (extra & 0x400) {
	    hi = (uae_u32)m68k_dreg(&regs, extra & 7);
	}
	if (div_unsigned(hi, lo, src, &quot, &rem)) {
	    SET_VFLG (1);
	    SET_NFLG (1);
	    SET_CFLG (0);
	} else {
	    SET_VFLG (0);
	    SET_CFLG (0);
	    SET_ZFLG (((uae_s32)quot) == 0);
	    SET_NFLG (((uae_s32)quot) < 0);
	    m68k_dreg(regs, extra & 7) = rem;
	    m68k_dreg(regs, (extra >> 12) & 7) = quot;
	}
    }
#endif
}

STATIC_INLINE void
mul_unsigned(uae_u32 src1, uae_u32 src2, uae_u32 *dst_hi, uae_u32 *dst_lo)
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
#if defined(uae_s64)
    if (extra & 0x800) {
	/* signed variant */
	uae_s64 a = (uae_s64)(uae_s32)m68k_dreg(&regs, (extra >> 12) & 7);

	a *= (uae_s64)(uae_s32)src;
	SET_VFLG (&regs.ccrflags, 0);
	SET_CFLG (&regs.ccrflags, 0);
	SET_ZFLG (&regs.ccrflags, a == 0);
	SET_NFLG (&regs.ccrflags, a < 0);
	if (extra & 0x400)
	    m68k_dreg(&regs, extra & 7) = (uae_u32)(a >> 32);
	else if ((a & UVAL64(0xffffffff80000000)) != 0
		 && (a & UVAL64(0xffffffff80000000)) != UVAL64(0xffffffff80000000))
	{
	    SET_VFLG (&regs.ccrflags, 1);
	}
	m68k_dreg(&regs, (extra >> 12) & 7) = (uae_u32)a;
    } else {
	/* unsigned */
	uae_u64 a = (uae_u64)(uae_u32)m68k_dreg(&regs, (extra >> 12) & 7);

	a *= (uae_u64)src;
	SET_VFLG (&regs.ccrflags, 0);
	SET_CFLG (&regs.ccrflags, 0);
	SET_ZFLG (&regs.ccrflags, a == 0);
	SET_NFLG (&regs.ccrflags, ((uae_s64)a) < 0);
	if (extra & 0x400)
	    m68k_dreg(&regs, extra & 7) = (uae_u32)(a >> 32);
	else if ((a & UVAL64(0xffffffff00000000)) != 0) {
	    SET_VFLG (&regs.ccrflags, 1);
	}
	m68k_dreg(&regs, (extra >> 12) & 7) = (uae_u32)a;
    }
#else
    if (extra & 0x800) {
	/* signed variant */
	uae_s32 src1,src2;
	uae_u32 dst_lo,dst_hi;
	uae_u32 sign;

	src1 = (uae_s32)src;
	src2 = (uae_s32)m68k_dreg(&regs, (extra >> 12) & 7);
	sign = (src1 ^ src2);
	if (src1 < 0) src1 = -src1;
	if (src2 < 0) src2 = -src2;
	mul_unsigned((uae_u32)src1,(uae_u32)src2,&dst_hi,&dst_lo);
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
	    m68k_dreg(&regs, extra & 7) = dst_hi;
	else if ((dst_hi != 0 || (dst_lo & 0x80000000) != 0)
		 && ((dst_hi & 0xffffffff) != 0xffffffff
		     || (dst_lo & 0x80000000) != 0x80000000))
	{
	    SET_VFLG (1);
	}
	m68k_dreg(&regs, (extra >> 12) & 7) = dst_lo;
    } else {
	/* unsigned */
	uae_u32 dst_lo,dst_hi;

	mul_unsigned(src,(uae_u32)m68k_dreg(&regs, (extra >> 12) & 7),&dst_hi,&dst_lo);

	SET_VFLG (0);
	SET_CFLG (0);
	SET_ZFLG (dst_hi == 0 && dst_lo == 0);
	SET_NFLG (((uae_s32)dst_hi) < 0);
	if (extra & 0x400)
	    m68k_dreg(&regs, extra & 7) = dst_hi;
	else if (dst_hi != 0) {
	    SET_VFLG (1);
	}
	m68k_dreg(regs, (extra >> 12) & 7) = dst_lo;
    }
#endif
}

#endif

void m68k_reset (int hardreset)
{
    regs.kick_mask = 0x00F80000;
    regs.spcflags = 0;
#ifdef SAVESTATE
    if (savestate_state == STATE_RESTORE || savestate_state == STATE_REWIND) {
	m68k_setpc (&regs, regs.pc);
	/* MakeFromSR() must not swap stack pointer */
	regs.s = (regs.sr >> 13) & 1;
	MakeFromSR(&regs);
	/* set stack pointer */
	if (regs.s)
	    m68k_areg(&regs, 7) = regs.isp;
	else
	    m68k_areg(&regs, 7) = regs.usp;
	return;
    }
#endif
    m68k_areg (&regs, 7) = get_long (0x00f80000);
    m68k_setpc (&regs, get_long (0x00f80004));
    regs.s = 1;
    regs.m = 0;
    regs.stopped = 0;
    regs.t1 = 0;
    regs.t0 = 0;
    SET_ZFLG (&regs.ccrflags, 0);
    SET_XFLG (&regs.ccrflags, 0);
    SET_CFLG (&regs.ccrflags, 0);
    SET_VFLG (&regs.ccrflags, 0);
    SET_NFLG (&regs.ccrflags, 0);
    regs.intmask = 7;
    regs.vbr = regs.sfc = regs.dfc = 0;
#ifdef FPUEMU
    regs.fpcr = regs.fpsr = regs.fpiar = 0;
    regs.fp_result = 1;
    regs.irc = 0xffff;
#endif
    regs.caar = regs.cacr = 0;
    regs.itt0 = regs.itt1 = regs.dtt0 = regs.dtt1 = 0;
    regs.tcr = regs.mmusr = regs.urp = regs.srp = regs.buscr = 0;

    a3000_fakekick(0);
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
        regs.pcr = currprefs.fpu_model ? MC68060_PCR : MC68EC060_PCR;
	regs.pcr |= (currprefs.cpu060_revision & 0xff) << 8;
	regs.pcr |= 2;
    }
    fill_prefetch_slow (&regs);
}

STATIC_INLINE int in_rom (uaecptr pc)
{
    return (munge24 (pc) & 0xFFF80000) == 0xF80000;
}

STATIC_INLINE int in_rtarea (uaecptr pc)
{
    return (munge24 (pc) & 0xFFFF0000) == RTAREA_BASE;
}

unsigned long REGPARAM2 op_illg (uae_u32 opcode, struct regstruct *regs)
{
    uaecptr pc = m68k_getpc (regs);
    static int warned;
    int inrom = in_rom(pc);
    int inrt = in_rtarea(pc);

    if (cloanto_rom && (opcode & 0xF100) == 0x7100) {
	m68k_dreg (regs, (opcode >> 9) & 7) = (uae_s8)(opcode & 0xFF);
	m68k_incpc (regs, 2);
	fill_prefetch_slow (regs);
	return 4;
    }

    if (opcode == 0x4E7B && inrom && get_long (0x10) == 0) {
	notify_user (NUMSG_KS68020);
	uae_restart (-1, NULL);
    }

#ifdef AUTOCONFIG
    if (opcode == 0xFF0D) {
	if (inrom) {
	    /* This is from the dummy Kickstart replacement */
	    uae_u16 arg = get_iword (regs, 2);
	    m68k_incpc (regs, 4);
	    ersatz_perform (arg);
	    fill_prefetch_slow (regs);
	    return 4;
	} else if (inrt) {
	    /* User-mode STOP replacement */
	    m68k_setstopped (regs, 1);
	    return 4;
	}
    }

    if ((opcode & 0xF000) == 0xA000 && inrt) {
	/* Calltrap. */
	m68k_incpc(regs, 2);
	m68k_handle_trap (opcode & 0xFFF, regs);
	fill_prefetch_slow (regs);
	return 4;
    }
#endif

    if ((opcode & 0xF000) == 0xF000) {
	if (warned < 20) {
	    write_log ("B-Trap %x at %x (%p)\n", opcode, pc, regs->pc_p);
	    warned++;
	}
	Exception(0xB, regs, 0);
	return 4;
    }
    if ((opcode & 0xF000) == 0xA000) {
	if (warned < 20) {
	    write_log ("A-Trap %x at %x (%p)\n", opcode, pc, regs->pc_p);
	    warned++;
	}
	Exception(0xA, regs, 0);
	return 4;
    }
    if (warned < 20) {
	write_log ("Illegal instruction: %04x at %08.8X -> %08.8X\n", opcode, pc, get_long (regs->vbr + 0x10));
	warned++;
    }

    Exception (4, regs, 0);
    return 4;
}

#ifdef CPUEMU_0

static char *mmu30regs[] = { "TCR", "", "SRP", "CRP", "", "", "", "" };

static void mmu_op30_pmove(uaecptr pc, uae_u32 opcode, uae_u16 next, uaecptr extra)
{
    int preg = (next >> 10) & 31;
    int rw = (next >> 9) & 1;
    int fd = (next >> 8) & 1;
    char *reg = NULL;
    uae_u32 otc = tc_030;
    int siz;

    switch (preg)
    {
    case 0x10: // TC
	reg = "TC";
	siz = 4;
	if (rw)
	    put_long(extra, tc_030);
	else
	    tc_030 = get_long(extra);
    break;
    case 0x12: // SRP
	reg = "SRP";
	siz = 8;
	if (rw) {
	    put_long(extra, srp_030 >> 32);
	    put_long(extra + 4, srp_030);
	} else {
	    srp_030 = (uae_u64)get_long(extra) << 32;
	    srp_030 |= get_long(extra + 4);
	}
    break;
    case 0x13: // CRP
	reg = "CRP";
	siz = 8;
	if (rw) {
	    put_long(extra, crp_030 >> 32);
	    put_long(extra + 4, crp_030);
	} else {
	    crp_030 = (uae_u64)get_long(extra) << 32;
	    crp_030 |= get_long(extra + 4);
	}
    break;
    case 0x18: // MMUSR
	reg = "MMUSR";
	siz = 2;
	if (rw)
	    put_word(extra, mmusr_030);
	else
	    mmusr_030 = get_word(extra);
    break;
    case 0x02: // TT0
	reg = "TT0";
	siz = 4;
	if (rw)
	    put_long(extra, tt0_030);
	else
	    tt0_030 = get_long(extra);
    break;
    case 0x03: // TT1
	reg = "TT1";
	siz = 4;
	if (rw)
	    put_long(extra, tt1_030);
	else
	    tt1_030 = get_long(extra);
    break;
    }

    if (!reg) {
	op_illg(opcode, &regs);
	return;
    }
#if MMUOP_DEBUG > 0
    {
	uae_u32 val;
	if (siz == 8) {
	    uae_u32 val2 = get_long(extra);
	    val = get_long(extra + 4);
	    if (rw)
		write_log("PMOVE %s,%08X%08X", reg, val2, val);
	    else
		write_log("PMOVE %08X%08X,%s", val2, val, reg);
	} else {
	    if (siz == 4)
		val = get_long(extra);
	    else
		val = get_word(extra);
	    if (rw)
		write_log("PMOVE %s,%08X", reg, val);
	    else
		write_log("PMOVE %08X,%s", val, reg);
	}
	write_log (" PC=%08X\n", pc);
    }
#endif
    if (currprefs.cs_mbdmac == 1 && currprefs.mbresmem_low_size > 0) {
	if (otc != tc_030) {
	    a3000_fakekick(tc_030 & 0x80000000);
	}
    }
}

static void mmu_op30_ptest(uaecptr pc, uae_u32 opcode, uae_u16 next, uaecptr extra)
{
#if MMUOP_DEBUG > 0
    char tmp[10];

    tmp[0] = 0;
    if ((next >> 8) & 1)
	sprintf(tmp,",A%d", (next >> 4) & 15);
    write_log("PTEST%c %02X,%08X,#%X%s PC=%08X\n",
	((next >> 9) & 1) ? 'W' : 'R', (next & 15), extra, (next >> 10) & 7, tmp, pc); 
#endif
    mmusr_030 = 0;
}

static void mmu_op30_pflush(uaecptr pc, uae_u32 opcode, uae_u16 next, uaecptr extra)
{
#if MMUOP_DEBUG > 0
    write_log("PFLUSH PC=%08X\n", pc);
#endif
}

void mmu_op30(uaecptr pc, uae_u32 opcode, struct regstruct *regs, int isnext, uaecptr extra)
{
    if (currprefs.cpu_model != 68030) {
	m68k_setpc (regs, pc);
	op_illg (opcode, regs);
	return;
    }
    if (isnext) {
	uae_u16 next = get_word(pc + 2);
	if (next & 0x8000)
	    mmu_op30_ptest(pc, opcode, next, extra);
	else if (next & 0x2000)
	    mmu_op30_pflush (pc, opcode, next, extra);
	else
	    mmu_op30_pmove(pc, opcode, next, extra);
	m68k_setpc (regs, m68k_getpc (regs) + 2);
    } else {
#if MMUOP_DEBUG > 0
	write_log("MMU030: %04x PC=%08x\n", opcode, m68k_getpc(regs));
#endif
    }
    return;
}

void mmu_op(uae_u32 opcode, struct regstruct *regs, uae_u32 extra)
{
#if MMUOP_DEBUG > 1
    write_log("mmu_op %04X PC=%08X\n", opcode, m68k_getpc(regs));
#endif
    if ((opcode & 0xFE0) == 0x0500) {
	/* PFLUSH */
	regs->mmusr = 0;
#if MMUOP_DEBUG > 0
	write_log ("PFLUSH\n");
#endif
	return;
    } else if ((opcode & 0x0FD8) == 0x548) {
	if (currprefs.cpu_model < 68060) { /* PTEST not in 68060 */
	    /* PTEST */
#if MMUOP_DEBUG > 0
	    write_log ("PTEST\n");
#endif
	    return;
	}
    } else if ((opcode & 0x0FB8) == 0x588) {
	/* PLPA */
	if (currprefs.cpu_model == 68060) {
#if MMUOP_DEBUG > 0
	    write_log("PLPA\n");
#endif
	    return;
	}
    } else if (opcode == 0xff00 && extra == 0x01c0) {
	/* LPSTOP */
	if (currprefs.cpu_model == 68060) {
#if MMUOP_DEBUG > 0
	    write_log("LPSTOP\n");
#endif
	    return;
	}
    }
#if MMUOP_DEBUG > 0
    write_log("Unknown MMU OP %04X\n", opcode);
#endif
    m68k_setpc (regs, m68k_getpc (regs) - 2);
    op_illg (opcode, regs);
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
	m68k_setpc (&regs, m68k_getpc (&regs));
	fill_prefetch_slow (&regs);
	opcode = get_word (regs.pc);
	if (opcode == 0x4e72 			/* RTE */
	    || opcode == 0x4e74 		/* RTD */
	    || opcode == 0x4e75 		/* RTS */
	    || opcode == 0x4e77 		/* RTR */
	    || opcode == 0x4e76 		/* TRAPV */
	    || (opcode & 0xffc0) == 0x4e80 	/* JSR */
	    || (opcode & 0xffc0) == 0x4ec0 	/* JMP */
	    || (opcode & 0xff00) == 0x6100	/* BSR */
	    || ((opcode & 0xf000) == 0x6000	/* Bcc */
		&& cctrue(&regs.ccrflags, (opcode >> 8) & 0xf))
	    || ((opcode & 0xf0f0) == 0x5050	/* DBcc */
		&& !cctrue(&regs.ccrflags, (opcode >> 8) & 0xf)
		&& (uae_s16)m68k_dreg(&regs, opcode & 7) != 0))
	{
	    last_trace_ad = m68k_getpc (&regs);
	    unset_special (&regs, SPCFLAG_TRACE);
	    set_special (&regs, SPCFLAG_DOTRACE);
	}
    } else if (regs.t1) {
	last_trace_ad = m68k_getpc (&regs);
	unset_special (&regs, SPCFLAG_TRACE);
	set_special (&regs, SPCFLAG_DOTRACE);
    }
}

#define IDLETIME (currprefs.cpu_idle * sleep_resolution / 700)

STATIC_INLINE int do_specialties (int cycles, struct regstruct *regs)
{
    #ifdef ACTION_REPLAY
    #ifdef ACTION_REPLAY_HRTMON
    if ((regs->spcflags & SPCFLAG_ACTION_REPLAY) && hrtmon_flag != ACTION_REPLAY_INACTIVE) {
	int isinhrt = (m68k_getpc(regs) >= hrtmem_start && m68k_getpc(regs) < hrtmem_start + hrtmem_size);
	/* exit from HRTMon? */
	if(hrtmon_flag == ACTION_REPLAY_ACTIVE && !isinhrt)
	    hrtmon_hide();
	/* HRTMon breakpoint? (not via IRQ7) */
	if(hrtmon_flag == ACTION_REPLAY_IDLE && isinhrt)
	    hrtmon_breakenter();
	if(hrtmon_flag == ACTION_REPLAY_ACTIVATE)
	    hrtmon_enter();
	if(!(regs->spcflags & ~SPCFLAG_ACTION_REPLAY))
	    return 0;
    }
    #endif
    if ((regs->spcflags & SPCFLAG_ACTION_REPLAY) && action_replay_flag != ACTION_REPLAY_INACTIVE ) {
	/*if(action_replay_flag == ACTION_REPLAY_ACTIVE && !is_ar_pc_in_rom())*/
	/*	write_log("PC:%p\n",m68k_getpc());*/

	if(action_replay_flag == ACTION_REPLAY_ACTIVATE || action_replay_flag == ACTION_REPLAY_DORESET)
	    action_replay_enter();
	if(action_replay_flag == ACTION_REPLAY_HIDE && !is_ar_pc_in_rom())
	{
	    action_replay_hide();
	    unset_special(regs, SPCFLAG_ACTION_REPLAY);
	}
	if (action_replay_flag == ACTION_REPLAY_WAIT_PC )
	{
	    /*write_log("Waiting for PC: %p, current PC= %p\n",wait_for_pc, m68k_getpc());*/
	    if (m68k_getpc(regs) == wait_for_pc)
	    {
		action_replay_flag = ACTION_REPLAY_ACTIVATE; /* Activate after next instruction. */
	    }
	}
    }
    #endif

    if (regs->spcflags & SPCFLAG_COPPER)
	do_copper ();

    /*n_spcinsns++;*/
#ifdef JIT
    unset_special(regs, SPCFLAG_END_COMPILE);   /* has done its job */
#endif

    while ((regs->spcflags & SPCFLAG_BLTNASTY) && dmaen (DMA_BLITTER) && cycles > 0 && !currprefs.blitter_cycle_exact) {
	int c = blitnasty();
	if (c > 0) {
	    cycles -= c * CYCLE_UNIT * 2;
	    if (cycles < CYCLE_UNIT)
		cycles = 0;
	} else
	    c = 4;
	do_cycles (c * CYCLE_UNIT);
	if (regs->spcflags & SPCFLAG_COPPER)
	    do_copper ();
    }

    if (regs->spcflags & SPCFLAG_DOTRACE)
	Exception (9, regs, last_trace_ad);
	
    if (regs->spcflags & SPCFLAG_TRAP) {
	unset_special (regs, SPCFLAG_TRAP);
	Exception (3, regs, 0);
    }

    while (regs->spcflags & SPCFLAG_STOP) {
	do_cycles (4 * CYCLE_UNIT);
	if (regs->spcflags & SPCFLAG_COPPER)
	    do_copper ();
	if (regs->spcflags & (SPCFLAG_INT | SPCFLAG_DOINT)) {
	    int intr = intlev ();
	    unset_special (regs, SPCFLAG_INT | SPCFLAG_DOINT);
	    if (intr != -1 && intr > regs->intmask)
		do_interrupt (intr, regs);
	}
	if ((regs->spcflags & (SPCFLAG_BRK | SPCFLAG_MODE_CHANGE))) {
	    unset_special (regs, SPCFLAG_BRK | SPCFLAG_MODE_CHANGE);
	    return 1;
	}

	if (currprefs.cpu_idle && currprefs.m68k_speed != 0 && ((regs->spcflags & SPCFLAG_STOP)) == SPCFLAG_STOP) {
	    /* sleep 1ms if STOP-instruction is executed */
	    if (1) {
		static int sleepcnt, lvpos, zerocnt;
		if (vpos != lvpos) {
		    sleepcnt--;
#ifdef JIT
		    if (pissoff == 0 && compiled_code && --zerocnt < 0) {
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

    if (regs->spcflags & SPCFLAG_TRACE)
	do_trace ();

    if (regs->spcflags & SPCFLAG_INT) {
	int intr = intlev ();
        unset_special (regs, SPCFLAG_INT | SPCFLAG_DOINT);
	if (intr != -1 && intr > regs->intmask)
	    do_interrupt (intr, regs);
    }
    if (regs->spcflags & SPCFLAG_DOINT) {
        unset_special (regs, SPCFLAG_DOINT);
        set_special (regs, SPCFLAG_INT);
    }

    if ((regs->spcflags & (SPCFLAG_BRK | SPCFLAG_MODE_CHANGE))) {
	unset_special (regs, SPCFLAG_BRK | SPCFLAG_MODE_CHANGE);
	return 1;
    }
    return 0;
}

void doint (void)
{
    if (currprefs.cpu_compatible)
	set_special (&regs, SPCFLAG_INT);
    else
	set_special (&regs, SPCFLAG_DOINT);
}
//static uae_u32 pcs[1000];


//#define DEBUG_CD32IO
#ifdef DEBUG_CD32IO

static uae_u32 cd32nextpc, cd32request;

static void out_cd32io2(void)
{
    uae_u32 request = cd32request;
    //write_log ("ACTUAL=%d ERROR=%d\n", get_long (request + 32), get_byte (request + 31));
    cd32nextpc = 0;
    cd32request = 0;
}

static void out_cd32io (uae_u32 pc)
{
    char out[100];
    int ioreq = 0;
    uae_u32 request = m68k_areg (&regs, 1);

    if (pc == cd32nextpc) {
	out_cd32io2();
	return;
    }
    out[0] = 0;
    switch (pc)
    {
	case 0xe57cc0:
	case 0xf04c34:
	sprintf (out, "opendevice");
	break;
	case 0xe57ce6:
	case 0xf04c56:
	sprintf (out, "closedevice");
	break;
	case 0xe57e44:
	case 0xf04f2c:
	sprintf (out, "beginio");
	ioreq = 1;
	break;
	case 0xe57ef2:
	case 0xf0500e:
	sprintf (out, "abortio");
	ioreq = -1;
	break;
    }
    if (out[0] == 0)
	return;
    if (cd32request)
	write_log ("old request still not returned!\n");
    cd32request = request;
    cd32nextpc = get_long(m68k_areg (&regs, 7));
    write_log("%s A1=%08.8X\n", out, request);
    if (ioreq) {
	static int cnt = 26;
	int cmd = get_word (request + 28);
#if 0
	if (cmd == 2) {
	    cnt--;
	    if (cnt == 0)
		activate_debugger(1);
	}
#endif
	write_log ("CMD=%d DATA=%08.8X LEN=%d %OFF=%d\n",
	    cmd, get_long(request + 40),
	    get_long(request + 36), get_long(request + 44));
    }
    if (ioreq < 0)
	;//activate_debugger();
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
int cpu_cycles;
static void m68k_run_1 (void)
{
    struct regstruct *r = &regs;

    for (;;) {
	uae_u32 opcode = r->ir;

	count_instr (opcode);

#ifdef DEBUG_CD32IO
	out_cd32io(m68k_getpc(&regs));
#endif

#if 0
	int pc = m68k_getpc();
	if (pc == 0xdff002)
	    write_log("hip\n");
	if (pc != pcs[0] && (pc < 0xd00000 || pc > 0x1000000)) {
	    memmove (pcs + 1, pcs, 998 * 4);
	    pcs[0] = pc;
	    //write_log("%08.8X-%04.4X ",pc, opcode);
	}
#endif
	do_cycles (cpu_cycles);
	cpu_cycles = (*cpufunctbl[opcode])(opcode, r);
	cpu_cycles &= cycles_mask;
	cpu_cycles |= cycles_val;
	if (r->spcflags) {
	    if (do_specialties (cpu_cycles, r))
		return;
	}
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

/* cycle-exact m68k_run() */

static void m68k_run_1_ce (void)
{
    struct regstruct *r = &regs;

    for (;;) {
	uae_u32 opcode = r->ir;

	(*cpufunctbl[opcode])(opcode, r);
	if (r->spcflags) {
	    if (do_specialties (0, r))
		return;
	}
	if (!currprefs.cpu_cycle_exact || currprefs.cpu_model > 68000)
	    return;
    }
}
#endif

#ifdef JIT  /* Completely different run_2 replacement */

void do_nothing(void)
{
    /* What did you expect this to do? */
    do_cycles(0);
    /* I bet you didn't expect *that* ;-) */
}

void exec_nostats(void)
{
    struct regstruct *r = &regs;

    for (;;)
    {
	uae_u16 opcode = get_iword(r, 0);

	cpu_cycles = (*cpufunctbl[opcode])(opcode, r);

	cpu_cycles &= cycles_mask;
	cpu_cycles |= cycles_val;

	do_cycles (cpu_cycles);

	if (end_block(opcode) || r->spcflags)
	    return; /* We will deal with the spcflags in the caller */
    }
}

static int triggered;

extern volatile int bsd_int_requested;

void execute_normal(void)
{
    struct regstruct *r = &regs;
    int blocklen;
    cpu_history pc_hist[MAXRUN];
    int total_cycles;

    if (check_for_cache_miss())
	return;

    total_cycles = 0;
    blocklen = 0;
    start_pc_p = r->pc_oldp;
    start_pc = r->pc;
    for (;;) {
	/* Take note: This is the do-it-normal loop */
	uae_u16 opcode = get_iword (r, 0);

	special_mem = DISTRUST_CONSISTENT_MEM;
	pc_hist[blocklen].location = (uae_u16*)r->pc_p;

	cpu_cycles = (*cpufunctbl[opcode])(opcode, r);

	cpu_cycles &= cycles_mask;
	cpu_cycles |= cycles_val;
	do_cycles (cpu_cycles);
	total_cycles += cpu_cycles;
	pc_hist[blocklen].specmem = special_mem;
	blocklen++;
	if (end_block(opcode) || blocklen >= MAXRUN || r->spcflags || uae_int_requested) {
	    compile_block(pc_hist,blocklen,total_cycles);
	    return; /* We will deal with the spcflags in the caller */
	}
	/* No need to check regs.spcflags, because if they were set,
	   we'd have ended up inside that "if" */
    }
}

typedef void compiled_handler(void);

static void m68k_run_2a (void)
{
    for (;;) {
	((compiled_handler*)(pushall_call_handler))();
	/* Whenever we return from that, we should check spcflags */
	if (uae_int_requested) {
	    intreq |= 0x0008;
	    set_special (&regs, SPCFLAG_INT);
	}
	if (regs.spcflags) {
	    if (do_specialties (0, &regs)) {
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

/* emulate simple prefetch  */
static void m68k_run_2p (void)
{
    uae_u32 prefetch, prefetch_pc;
    struct regstruct *r = &regs;

    prefetch_pc = m68k_getpc (r);
    prefetch = get_longi (prefetch_pc);
    for (;;) {
	uae_u32 opcode;
	uae_u32 pc = m68k_getpc (r);

#ifdef DEBUG_CD32IO
	out_cd32io(m68k_getpc(&regs));
#endif

	do_cycles (cpu_cycles);

	if (pc == prefetch_pc)
	    opcode = prefetch >> 16;
	else if (pc == prefetch_pc + 2)
	    opcode = prefetch & 0xffff;
	else
	    opcode = get_wordi (pc);

	count_instr (opcode);

	prefetch_pc = m68k_getpc (r) + 2;
	prefetch = get_longi (prefetch_pc);
	cpu_cycles = (*cpufunctbl[opcode])(opcode, r);
	cpu_cycles &= cycles_mask;
	cpu_cycles |= cycles_val;
	if (r->spcflags) {
	    if (do_specialties (cpu_cycles, r))
		return;
	}
    }
}

/* Same thing, but don't use prefetch to get opcode.  */
static void m68k_run_2 (void)
{
   struct regstruct *r = &regs;

   for (;;) {
	uae_u32 opcode = get_iword (r, 0);
	count_instr (opcode);

	do_cycles (cpu_cycles);
	cpu_cycles = (*cpufunctbl[opcode])(opcode, r);
	cpu_cycles &= cycles_mask;
	cpu_cycles |= cycles_val;
	if (r->spcflags) {
	    if (do_specialties (cpu_cycles, r))
		return;
	}
    }
}

/* "MMU" 68k  */
static void m68k_run_mmu (void)
{
   for (;;) {
	uae_u32 opcode = get_iword (&regs, 0);
	do_cycles (cpu_cycles);
	mmu_backup_regs = regs;
	cpu_cycles = (*cpufunctbl[opcode])(opcode, &regs);
	cpu_cycles &= cycles_mask;
	cpu_cycles |= cycles_val;
	if (mmu_triggered)
	    mmu_do_hit();
	if (regs.spcflags) {
	    if (do_specialties (cpu_cycles, &regs))
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
    Exception (2, &regs, addr);
}

void m68k_go (int may_quit)
{
    int hardboot = 1;

    if (in_m68k_go || !may_quit) {
	write_log ("Bug! m68k_go is not reentrant.\n");
	abort ();
    }

    reset_frame_rate_hack ();
    update_68k_cycles ();

    in_m68k_go++;
    for (;;) {
	void (*run_func)(void);
	if (quit_program > 0) {
	    int hardreset = (quit_program == 3 ? 1 : 0) | hardboot;
	    if (quit_program == 1)
		break;

	    quit_program = 0;
	    hardboot = 0;
#ifdef SAVESTATE
	    if (savestate_state == STATE_RESTORE)
		restore_state (savestate_fname);
	    else if (savestate_state == STATE_REWIND)
		savestate_rewind ();
#endif
	    reset_all_systems ();
	    customreset (hardreset);
	    m68k_reset (hardreset);
	    if (hardreset) {
		memory_hardreset();
		write_log ("hardreset, memory cleared\n");
	    }
#ifdef SAVESTATE
	    /* We may have been restoring state, but we're done now.  */
	    if (savestate_state == STATE_RESTORE || savestate_state == STATE_REWIND) {
		map_overlay (1);
		fill_prefetch_slow (&regs); /* compatibility with old state saves */
	        memory_map_dump();
	    }
	    savestate_restore_finish ();
#endif
	    fill_prefetch_slow (&regs);
	    if (currprefs.produce_sound == 0)
		eventtab[ev_audio].active = 0;
	    handle_active_events ();
	    if (regs.spcflags)
		do_specialties (0, &regs);
	    m68k_setpc (&regs, regs.pc);
	}

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
		write_log ("double bus error or corrupted stack, forcing reboot..\n");
		regs.panic = 0;
		uae_reset (1);
	    }
	}

#if 0 /* what was the meaning of this? this breaks trace emulation if debugger is used */
	if (regs.spcflags) {
	    uae_u32 of = regs.spcflags;
	    regs.spcflags &= ~(SPCFLAG_BRK | SPCFLAG_MODE_CHANGE);
	    do_specialties (0, &regs);
	    regs.spcflags |= of & (SPCFLAG_BRK | SPCFLAG_MODE_CHANGE);
	}
#endif
#ifndef JIT
	run_func = currprefs.cpu_model == 68000 && currprefs.cpu_cycle_exact ? m68k_run_1_ce :
		    currprefs.cpu_model == 68000 && currprefs.cpu_compatible ? m68k_run_1 :
		    currprefs.cpu_compatible ? m68k_run_2p : m68k_run_2;
#else
	if (mmu_enabled && !currprefs.cachesize) {
	    run_func = m68k_run_mmu;
	} else {
	    run_func = currprefs.cpu_cycle_exact && currprefs.cpu_model == 68000 ? m68k_run_1_ce :
		   currprefs.cpu_compatible > 0 && currprefs.cpu_model == 68000 ? m68k_run_1 :
		   currprefs.cpu_model >= 68020 && currprefs.cachesize ? m68k_run_2a :
		   currprefs.cpu_compatible ? m68k_run_2p : m68k_run_2;
	}
#endif
	run_func ();
    }
    in_m68k_go--;
}

#if 0
static void m68k_verify (uaecptr addr, uaecptr *nextpc)
{
    uae_u32 opcode, val;
    struct instr *dp;

    opcode = get_iword_1(0);
    last_op_for_exception_3 = opcode;
    m68kpc_offset = 2;

    if (cpufunctbl[opcode] == op_illg_1) {
	opcode = 0x4AFC;
    }
    dp = table68k + opcode;

    if (dp->suse) {
	if (!verify_ea (dp->sreg, dp->smode, dp->size, &val)) {
	    Exception (3, &regs, 0);
	    return;
	}
    }
    if (dp->duse) {
	if (!verify_ea (dp->dreg, dp->dmode, dp->size, &val)) {
	    Exception (3, &regs, 0);
	    return;
	}
    }
}
#endif

static const char* ccnames[] =
{ "T ","F ","HI","LS","CC","CS","NE","EQ",
  "VC","VS","PL","MI","GE","LT","GT","LE" };

void m68k_disasm_2 (char *buf, int bufsize, uaecptr addr, uaecptr *nextpc, int cnt, uae_u32 *seaddr, uae_u32 *deaddr, int safemode)
{
    uaecptr newpc = 0;
    m68kpc_offset = addr - m68k_getpc (&regs);

    if (buf)
        memset(buf, 0, bufsize);
    while (cnt-- > 0) {
	char instrname[100], *ccpt;
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

	buf = buf_out(buf, &bufsize, "%08lX ", m68k_getpc (&regs) + m68kpc_offset);

	m68kpc_offset += 2;

	strcpy (instrname, lookup->name);
	ccpt = strstr (instrname, "cc");
	if (ccpt != 0) {
	    strncpy (ccpt, ccnames[dp->cc], 2);
	}
	switch (dp->size){
	 case sz_byte: strcat (instrname, ".B "); break;
	 case sz_word: strcat (instrname, ".W "); break;
	 case sz_long: strcat (instrname, ".L "); break;
	 default: strcat (instrname, "   "); break;
	}

	if (dp->suse) {
	    newpc = m68k_getpc (&regs) + m68kpc_offset;
	    newpc += ShowEA (0, opcode, dp->sreg, dp->smode, dp->size, instrname, seaddr, safemode);
	}
	if (dp->suse && dp->duse)
	    strcat (instrname, ",");
	if (dp->duse) {
	    newpc = m68k_getpc (&regs) + m68kpc_offset;
	    newpc += ShowEA (0, opcode, dp->dreg, dp->dmode, dp->size, instrname, deaddr, safemode);
	}

	for (i = 0; i < (m68kpc_offset - oldpc) / 2; i++) {
	    buf = buf_out (buf, &bufsize, "%04x ", get_iword_1 (oldpc + i * 2));
	}
	while (i++ < 5)
	    buf = buf_out (buf, &bufsize, "     ");
    if (strlen(instrname) > 79) {
        int i = 0;
    }

	buf = buf_out (buf, &bufsize, instrname);

	if (ccpt != 0) {
	    if (deaddr)
		*deaddr = newpc;
	    if (cctrue(&regs.ccrflags, dp->cc))
		buf = buf_out (buf, &bufsize, " == %08lX (TRUE)", newpc);
	    else
		buf = buf_out (buf, &bufsize, " == %08lX (FALSE)", newpc);
	} else if ((opcode & 0xff00) == 0x6100) { /* BSR */
	    if (deaddr)
		*deaddr = newpc;
	    buf = buf_out (buf, &bufsize, " == %08lX", newpc);
	}
	buf = buf_out (buf, &bufsize, "\n");
    }
    if (nextpc)
	*nextpc = m68k_getpc (&regs) + m68kpc_offset;
}

void m68k_disasm_ea (void *f, uaecptr addr, uaecptr *nextpc, int cnt, uae_u32 *seaddr, uae_u32 *deaddr)
{
    char *buf;

    buf = (char*)malloc((MAX_LINEWIDTH + 1) * cnt);
    if (!buf)
        return;
    m68k_disasm_2 (buf, (MAX_LINEWIDTH + 1) * cnt, addr, nextpc, cnt, seaddr, deaddr, 1);
    f_out(f, "%s", buf);
    free(buf);
}
void m68k_disasm (void *f, uaecptr addr, uaecptr *nextpc, int cnt)
{
    char *buf;

    buf = (char*)malloc((MAX_LINEWIDTH + 1) * cnt);
    if (!buf)
        return;
    m68k_disasm_2 (buf, (MAX_LINEWIDTH + 1) * cnt, addr, nextpc, cnt, NULL, NULL, 0);
    f_out(f, "%s", buf);
    free(buf);
}

/*************************************************************
 Disasm the m68kcode at the given address into instrname
 and instrcode
*************************************************************/
void sm68k_disasm(char *instrname, char *instrcode, uaecptr addr, uaecptr *nextpc)
{
    char *ccpt;
    uae_u32 opcode;
    struct mnemolookup *lookup;
    struct instr *dp;
    int oldpc;

    uaecptr newpc = 0;

    m68kpc_offset = addr - m68k_getpc (&regs);

    oldpc = m68kpc_offset;
    opcode = get_iword_1 (m68kpc_offset);
    if (cpufunctbl[opcode] == op_illg_1) {
	opcode = 0x4AFC;
    }
    dp = table68k + opcode;
    for (lookup = lookuptab;lookup->mnemo != dp->mnemo; lookup++);

    m68kpc_offset += 2;

    strcpy (instrname, lookup->name);
    ccpt = strstr (instrname, "cc");
    if (ccpt != 0) {
	strncpy (ccpt, ccnames[dp->cc], 2);
    }
    switch (dp->size){
	case sz_byte: strcat (instrname, ".B "); break;
	case sz_word: strcat (instrname, ".W "); break;
	case sz_long: strcat (instrname, ".L "); break;
	default: strcat (instrname, "   "); break;
    }

    if (dp->suse) {
	newpc = m68k_getpc (&regs) + m68kpc_offset;
	newpc += ShowEA (0, opcode, dp->sreg, dp->smode, dp->size, instrname, NULL, 0);
    }
    if (dp->suse && dp->duse)
	strcat (instrname, ",");
    if (dp->duse) {
	newpc = m68k_getpc (&regs) + m68kpc_offset;
	newpc += ShowEA (0, opcode, dp->dreg, dp->dmode, dp->size, instrname, NULL, 0);
    }

    if (instrcode)
    {
	int i;
	for (i = 0; i < (m68kpc_offset - oldpc) / 2; i++)
	{
	    sprintf(instrcode,"%04x ",get_iword_1 (oldpc + i * 2));
	    instrcode += strlen(instrcode);
	}
    }

    if (nextpc)
	*nextpc = m68k_getpc (&regs) + m68kpc_offset;
}

struct cpum2c m2cregs[] = {
    0, "SFC",
    1, "DFC",
    2, "CACR",
    3, "TC",
    4, "ITT0",
    5, "ITT1",
    6, "DTT0",
    7, "DTT1",
    8, "BUSC",
    0x800, "USP",
    0x801, "VBR",
    0x802, "CAAR",
    0x803, "MSP",
    0x804, "ISP",
    0x805, "MMUS",
    0x806, "URP",
    0x807, "SRP",
    0x808, "PCR",
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
	f_out (f, "  D%d %08lX ", i, m68k_dreg(&regs, i));
	if ((i & 3) == 3) f_out (f, "\n");
    }
    for (i = 0; i < 8; i++){
	f_out (f, "  A%d %08lX ", i, m68k_areg(&regs, i));
	if ((i & 3) == 3) f_out (f, "\n");
    }
    if (regs.s == 0) regs.usp = m68k_areg(&regs, 7);
    if (regs.s && regs.m) regs.msp = m68k_areg(&regs, 7);
    if (regs.s && regs.m == 0) regs.isp = m68k_areg(&regs, 7);
    j = 2;
    f_out(f, "USP  %08.8X ISP  %08.8X ", regs.usp, regs.isp);
    for (i = 0; m2cregs[i].regno>= 0; i++) {
	if (!movec_illg(m2cregs[i].regno)) {
	    if (j > 0 && (j % 4) == 0)
		f_out(f, "\n");
	    f_out (f, "%-4s %08.8X ", m2cregs[i].regname, val_move2c(m2cregs[i].regno));
	    j++;
	}
    }
    if (j > 0)
	f_out (f, "\n");
    f_out (f, "T=%d%d S=%d M=%d X=%d N=%d Z=%d V=%d C=%d IMASK=%d STP=%d\n",
	regs.t1, regs.t0, regs.s, regs.m,
	GET_XFLG(&regs.ccrflags), GET_NFLG(&regs.ccrflags), GET_ZFLG(&regs.ccrflags),
	GET_VFLG(&regs.ccrflags), GET_CFLG(&regs.ccrflags),
	regs.intmask, regs.stopped);
#ifdef FPUEMU
    if (currprefs.fpu_model) {
	uae_u32 fpsr;
	for (i = 0; i < 8; i++){
	    f_out (f, "FP%d: %g ", i, regs.fp[i]);
	    if ((i & 3) == 3) f_out (f, "\n");
	}
	fpsr = get_fpsr();
	f_out (f, "N=%d Z=%d I=%d NAN=%d\n",
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
	f_out (f, "Prefetch %04x (%s) %04x (%s)\n", regs.irc, lookup1->name, regs.ir, lookup2->name);
    }

    m68k_disasm (f, m68k_getpc (&regs), nextpc, 1);
    if (nextpc)
	f_out (f, "Next PC: %08lx\n", *nextpc);
}

#ifdef SAVESTATE

/* CPU save/restore code */

#define CPUTYPE_EC 1
#define CPUMODE_HALT 1

uae_u8 *restore_cpu (uae_u8 *src)
{
    int i, flags, model;
    uae_u32 l;

    changed_prefs.cpu_model = model = restore_u32();
    flags = restore_u32();
    changed_prefs.address_space_24 = 0;
    if (flags & CPUTYPE_EC)
	changed_prefs.address_space_24 = 1;
    if (model > 68000)
	changed_prefs.cpu_compatible = 0;
    currprefs.address_space_24 = changed_prefs.address_space_24;
    currprefs.cpu_compatible = changed_prefs.cpu_compatible;
    currprefs.cpu_cycle_exact = changed_prefs.cpu_cycle_exact;
    currprefs.blitter_cycle_exact = changed_prefs.blitter_cycle_exact;
    for (i = 0; i < 15; i++)
	regs.regs[i] = restore_u32 ();
    regs.pc = restore_u32 ();
    regs.irc = restore_u16 ();
    regs.ir = restore_u16 ();
    regs.usp = restore_u32 ();
    regs.isp = restore_u32 ();
    regs.sr = restore_u16 ();
    l = restore_u32();
    if (l & CPUMODE_HALT) {
	regs.stopped = 1;
	set_special (&regs, SPCFLAG_STOP);
    } else
	regs.stopped = 0;
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
	if (changed_prefs.m68k_speed == 0)
	    currprefs.m68k_speed = changed_prefs.m68k_speed = -1;
    }
    if (model >= 68030) {
	crp_030 = restore_u64();
	srp_030 = restore_u64();
	tt0_030 =restore_u32();
	tt1_030 = restore_u32();
	tc_030 = restore_u32();
	mmusr_030 = restore_u16();
    }
    if (model >= 68040) {
	regs.itt0 = restore_u32();
	regs.itt1 = restore_u32();
	regs.dtt0 = restore_u32();
	regs.dtt1 = restore_u32();
	regs.tcr = restore_u32();
	regs.urp = restore_u32();
	regs.srp = restore_u32();
    }
    if (model >= 68060) {
	regs.buscr = restore_u32();
	regs.pcr = restore_u32();
    }
    if (flags & 0x80000000) {
	int khz = restore_u32();
	restore_u32();
	if (khz > 0 && khz < 800000)
	    currprefs.m68k_speed = changed_prefs.m68k_speed = 0;
    }
    write_log ("CPU %d%s%03d, PC=%08.8X\n",
	model / 1000, flags & 1 ? "EC" : "", model % 1000, regs.pc);

    return src;
}

void restore_cpu_finish(void)
{
    init_m68k ();
    m68k_setpc (&regs, regs.pc);
    set_cpu_caches();
}

uae_u8 *save_cpu (int *len, uae_u8 *dstptr)
{
    uae_u8 *dstbak,*dst;
    int model, i, khz;

    if (dstptr)
	dstbak = dst = dstptr;
    else
	dstbak = dst = (uae_u8*)malloc(1000);
    model = currprefs.cpu_model;
    save_u32 (model);					/* MODEL */
    save_u32 (0x80000000 | (currprefs.address_space_24 ? 1 : 0)); /* FLAGS */
    for(i = 0;i < 15; i++)
	save_u32 (regs.regs[i]);			/* D0-D7 A0-A6 */
    save_u32 (m68k_getpc (&regs));			/* PC */
    save_u16 (regs.irc);				/* prefetch */
    save_u16 (regs.ir);					/* instruction prefetch */
    MakeSR (&regs);
    save_u32 (!regs.s ? regs.regs[15] : regs.usp);	/* USP */
    save_u32 (regs.s ? regs.regs[15] : regs.isp);	/* ISP */
    save_u16 (regs.sr);					/* SR/CCR */
    save_u32 (regs.stopped ? CPUMODE_HALT : 0);		/* flags */
    if(model >= 68010) {
	save_u32 (regs.dfc);				/* DFC */
	save_u32 (regs.sfc);				/* SFC */
	save_u32 (regs.vbr);				/* VBR */
    }
    if(model >= 68020) {
	save_u32 (regs.caar);				/* CAAR */
	save_u32 (regs.cacr);				/* CACR */
	save_u32 (regs.msp);				/* MSP */
    }
    if(model >= 68030) {
	save_u64 (crp_030);				/* CRP */
	save_u64 (srp_030);				/* SRP */
	save_u32 (tt0_030);				/* TT0/AC0 */
	save_u32 (tt1_030);				/* TT1/AC1 */
	save_u32 (tc_030);				/* TCR */
	save_u16 (mmusr_030);				/* MMUSR/ACUSR */
    }
    if(model >= 68040) {
	save_u32 (regs.itt0);				/* ITT0 */
	save_u32 (regs.itt1);				/* ITT1 */
	save_u32 (regs.dtt0);				/* DTT0 */
	save_u32 (regs.dtt1);				/* DTT1 */
	save_u32 (regs.tcr);				/* TCR */
	save_u32 (regs.urp);				/* URP */
	save_u32 (regs.srp);				/* SRP */
    }
    if(model >= 68060) {
	save_u32 (regs.buscr);				/* BUSCR */
	save_u32 (regs.pcr);				/* PCR */
    }
    khz = -1;
    if (currprefs.m68k_speed == 0) {
	khz = currprefs.ntscmode ? 715909 : 709379;
	if (currprefs.cpu_model >= 68020)
	    khz *= 2;
    }
    save_u32 (khz); // clock rate in KHz: -1 = fastest possible 
    save_u32 (0); // spare
    *len = dst - dstbak;
    return dstbak;
}

#endif /* SAVESTATE */

static void exception3f (uae_u32 opcode, uaecptr addr, uaecptr fault, int writeaccess, int instructionaccess)
{
    last_addr_for_exception_3 = addr;
    last_fault_for_exception_3 = fault;
    last_op_for_exception_3 = opcode;
    last_writeaccess_for_exception_3 = writeaccess;
    last_instructionaccess_for_exception_3 = instructionaccess;
    Exception (3, &regs, fault);
}

void exception3 (uae_u32 opcode, uaecptr addr, uaecptr fault)
{
    exception3f (opcode, addr, fault, 0, 0);
}

void exception3i (uae_u32 opcode, uaecptr addr, uaecptr fault)
{
    exception3f (opcode, addr, fault, 0, 1);
}

void exception2 (uaecptr addr, uaecptr fault)
{
    write_log ("delayed exception2!\n");
    regs.panic_pc = m68k_getpc (&regs);
    regs.panic_addr = addr;
    regs.panic = 2;
    set_special (&regs, SPCFLAG_BRK);
    m68k_setpc (&regs, 0xf80000);
#ifdef JIT
    set_special(&regs, SPCFLAG_END_COMPILE);
#endif
    fill_prefetch_slow (&regs);
}

void cpureset (void)
{
    uaecptr pc;
    uaecptr ksboot = 0xf80002 - 2; /* -2 = RESET hasn't increased PC yet */
    uae_u16 ins;

    if (currprefs.cpu_compatible || currprefs.cpu_cycle_exact) {
        customreset (0);
	return;
    }
    pc = m68k_getpc(&regs);
    if (pc >= currprefs.chipmem_size) {
	addrbank *b = &get_mem_bank(pc);
	if (b->check(pc, 2 + 2)) {
	    /* We have memory, hope for the best.. */
	    customreset (0);
	    return;
	}
	write_log("M68K RESET PC=%x, rebooting..\n", pc);
	customreset (0);
	m68k_setpc (&regs, ksboot);
	return;
    }
    /* panic, RAM is going to disappear under PC */
    ins = get_word (pc + 2);
    if ((ins & ~7) == 0x4ed0) {
	int reg = ins & 7;
	uae_u32 addr = m68k_areg (&regs, reg);
	write_log ("reset/jmp (ax) combination emulated -> %x\n", addr);
	customreset (0);
	if (addr < 0x80000)
	    addr += 0xf80000;
	m68k_setpc (&regs, addr - 2);
	return;
    }
    write_log("M68K RESET PC=%x, rebooting..\n", pc);
    customreset (0);
    m68k_setpc (&regs, ksboot);
}


/*

 The routines below take dividend and divisor as parameters.
 They return 0 if division by zero, or exact number of cycles otherwise.

 The number of cycles returned assumes a register operand.
 Effective address time must be added if memory operand.

 For 68000 only (not 68010, 68012, 68020, etc).
 Probably valid for 68008 after adding the extra prefetch cycle.


 Best and worst cases are for register operand:
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


//
// DIVU
// Unsigned division
//

STATIC_INLINE int getDivu68kCycles_2(uae_u32 dividend, uae_u16 divisor)
{
	int mcycles;
	uae_u32 hdivisor;
	int i;

	if(divisor == 0)
		return 0;

	// Overflow
	if((dividend >> 16) >= divisor)
		return (mcycles = 5) * 2;

	mcycles = 38;
	hdivisor = divisor << 16;

	for( i = 0; i < 15; i++)
	{
		uae_u32 temp;
		temp = dividend;

		dividend <<= 1;

		// If carry from shift
		if((uae_s32)temp < 0)
		{
			dividend -= hdivisor;
		}
		else
		{
			mcycles += 2;
			if(dividend >= hdivisor)
			{
				dividend -= hdivisor;
				mcycles--;
			}
		}
	}
	return mcycles * 2;
}
int getDivu68kCycles(uae_u32 dividend, uae_u16 divisor)
{
    int v = getDivu68kCycles_2(dividend, divisor) - 4;
//    write_log("U%d ",	v);
    return v;
}

//
// DIVS
// Signed division
//

STATIC_INLINE int getDivs68kCycles_2(uae_s32 dividend, uae_s16 divisor)
{
	int mcycles;
	uae_u32 aquot;
	int i;

	if(divisor == 0)
		return 0;

	mcycles = 6;

	if( dividend < 0)
		mcycles++;

	// Check for absolute overflow
	if(((uae_u32)abs(dividend) >> 16) >= (uae_u16)abs(divisor))
	{
		return (mcycles + 2) * 2;
	}

	// Absolute quotient
	aquot = (uae_u32) abs(dividend) / (uae_u16)abs(divisor);

	mcycles += 55;

	if(divisor >= 0)
	{
		if(dividend >= 0)
			mcycles--;
		else
			mcycles++;
	}

	// Count 15 msbits in absolute of quotient

	for( i = 0; i < 15; i++)
	{
		if((uae_s16)aquot >= 0)
			mcycles++;
		aquot <<= 1;
	}

	return mcycles * 2;
}
int getDivs68kCycles(uae_s32 dividend, uae_s16 divisor)
{
    int v = getDivs68kCycles_2(dividend, divisor) - 4;
//    write_log("S%d ",	v);
    return v;
}
