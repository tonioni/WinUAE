/*
 * compiler/compemu_support.cpp - Core dynamic translation engine
 *
 * Copyright (c) 2001-2009 Milan Jurik of ARAnyM dev team (see AUTHORS)
 * 
 * Inspired by Christian Bauer's Basilisk II
 *
 * This file is part of the ARAnyM project which builds a new and powerful
 * TOS/FreeMiNT compatible virtual machine running on almost any hardware.
 *
 * JIT compiler m68k -> IA-32 and AMD64 / ARM
 *
 * Original 68040 JIT compiler for UAE, copyright 2000-2002 Bernd Meyer
 * Adaptation for Basilisk II and improvements, copyright 2000-2004 Gwenole Beauchesne
 * Portions related to CPU detection come from linux/arch/i386/kernel/setup.c
 *
 * ARAnyM is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * ARAnyM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ARAnyM; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef UAE
#define writemem_special writemem
#define readmem_special  readmem
#define USE_MATCHSTATE 0
#else
#if !FIXED_ADDRESSING
#error "Only Fixed Addressing is supported with the JIT Compiler"
#endif

#if defined(X86_ASSEMBLY) && !SAHF_SETO_PROFITABLE
#error "Only [LS]AHF scheme to [gs]et flags is supported with the JIT Compiler"
#endif

/* NOTE: support for AMD64 assumes translation cache and other code
 * buffers are allocated into a 32-bit address space because (i) B2/JIT
 * code is not 64-bit clean and (ii) it's faster to resolve branches
 * that way.
 */
#if !defined(CPU_i386) && !defined(CPU_x86_64) && !defined(CPU_arm)
#error "Only IA-32, X86-64 and ARM v6 targets are supported with the JIT Compiler"
#endif

#define USE_MATCH 0
#endif

/* kludge for Brian, so he can compile under MSVC++ */
#define USE_NORMAL_CALLING_CONVENTION 0

#include "sysconfig.h"
#include "sysdeps.h"

#ifdef JIT

#ifdef UAE
#include "options.h"
#include "events.h"
#include "memory.h"
#include "custom.h"
#else
#include "sysdeps.h"
#include "cpu_emulation.h"
#include "main.h"
#include "vm_alloc.h"

#include "m68k.h"
#include "memory.h"
#include "readcpu.h"
#endif
#include "newcpu.h"
#include "comptbl.h"
#ifdef UAE
#include "compemu.h"
#else
#include "compiler/compemu.h"
#include "fpu/fpu.h"
#include "fpu/flags.h"
#include "parameters.h"
#endif

#ifdef UAE

#define UNUSED(x)
#define HAVE_GET_WORD_UNSWAPPED
#include "uae.h"
#include "uae/log.h"
#define jit_log uae_log
#define jit_log2(...)

#define MEMBaseDiff ((uae_u32)NATMEM_OFFSET)

// %%% BRIAN KING WAS HERE %%%
extern bool canbang;

#include "compemu_prefs.cpp"

#define cache_size currprefs.cachesize

#else
#define DEBUG 0
#include "debug.h"
#endif

#define PROFILE_COMPILE_TIME		1
#define PROFILE_UNTRANSLATED_INSNS	1

# include <csignal>
# include <cstdlib>
# include <cerrno>
# include <cassert>

#if defined(CPU_x86_64) && 0
#define RECORD_REGISTER_USAGE		1
#endif

#ifdef WIN32
#undef write_log
#define write_log dummy_write_log
static void dummy_write_log(const char *, ...) { }
static void dummy_write_log(const TCHAR *, ...) { }
#endif

#ifdef JIT_DEBUG
#undef abort
#define abort() do { \
	fprintf(stderr, "Abort in file %s at line %d\n", __FILE__, __LINE__); \
	compiler_dumpstate(); \
	exit(EXIT_FAILURE); \
} while (0)
#endif

#ifdef RECORD_REGISTER_USAGE
static uint64 reg_count[16];
static int reg_count_local[16];

static int reg_count_compare(const void *ap, const void *bp)
{
    const int a = *((int *)ap);
    const int b = *((int *)bp);
    return reg_count[b] - reg_count[a];
}
#endif

#if PROFILE_COMPILE_TIME
#include <time.h>
static uae_u32 compile_count	= 0;
static clock_t compile_time		= 0;
static clock_t emul_start_time	= 0;
static clock_t emul_end_time	= 0;
#endif

#if PROFILE_UNTRANSLATED_INSNS
const int untranslated_top_ten = 20;
static uae_u32 raw_cputbl_count[65536] = { 0, };
static uae_u16 opcode_nums[65536];


static int untranslated_compfn(const void *e1, const void *e2)
{
	return raw_cputbl_count[*(const uae_u16 *)e1] < raw_cputbl_count[*(const uae_u16 *)e2];
}
#endif

static compop_func *compfunctbl[65536];
static compop_func *nfcompfunctbl[65536];
#ifdef NOFLAGS_SUPPORT
static compop_func *nfcpufunctbl[65536];
#endif
uae_u8* comp_pc_p;

#ifdef UAE
/* defined in uae.h */
#else
// External variables
// newcpu.cpp
extern int quit_program;
#endif

// gb-- Extra data for Basilisk II/JIT
#ifdef JIT_DEBUG
static bool		JITDebug			= false;	// Enable runtime disassemblers through mon?
#else
const bool		JITDebug			= false;	// Don't use JIT debug mode at all
#endif
#if USE_INLINING
static bool		follow_const_jumps	= true;		// Flag: translation through constant jumps	
#else
const bool		follow_const_jumps	= false;
#endif

#ifdef UAE
/* ... */
#else
const uae_u32	MIN_CACHE_SIZE		= 1024;		// Minimal translation cache size (1 MB)
static uae_u32	cache_size			= 0;		// Size of total cache allocated for compiled blocks
static uae_u32	current_cache_size	= 0;		// Cache grows upwards: how much has been consumed already
#endif
static bool		lazy_flush		= true;	// Flag: lazy translation cache invalidation
static bool		avoid_fpu		= true;	// Flag: compile FPU instructions ?
static bool		have_cmov		= false;	// target has CMOV instructions ?
static bool		have_lahf_lm		= true;		// target has LAHF supported in long mode ?
static bool		have_rat_stall		= true;	// target has partial register stalls ?
const bool		tune_alignment		= true;	// Tune code alignments for running CPU ?
const bool		tune_nop_fillers	= true;	// Tune no-op fillers for architecture
static bool		setzflg_uses_bsf	= false;	// setzflg virtual instruction can use native BSF instruction correctly?
static int		align_loops		= 32;	// Align the start of loops
static int		align_jumps		= 32;	// Align the start of jumps
static int		optcount[10]		= {
	10,		// How often a block has to be executed before it is translated
	0,		// How often to use naive translation
	0, 0, 0, 0,
	-1, -1, -1, -1
};

#ifdef UAE
/* FIXME: currently in compemu.h */
#else
struct op_properties {
	uae_u8 use_flags;
	uae_u8 set_flags;
	uae_u8 is_addx;
	uae_u8 cflow;
};
static op_properties prop[65536];

static inline int end_block(uae_u32 opcode)
{
	return (prop[opcode].cflow & fl_end_block);
}

static inline bool is_const_jump(uae_u32 opcode)
{
	return (prop[opcode].cflow == fl_const_jump);
}

static inline bool may_trap(uae_u32 opcode)
{
	return (prop[opcode].cflow & fl_trap);
}

#endif

static inline unsigned int cft_map (unsigned int f)
{
#ifndef HAVE_GET_WORD_UNSWAPPED
    return f;
#else
	return ((f >> 8) & 255) | ((f & 255) << 8);
#endif
}

uae_u8* start_pc_p;
uae_u32 start_pc;
uae_u32 current_block_pc_p;
static uintptr current_block_start_target;
uae_u32 needed_flags;
static uintptr next_pc_p;
static uintptr taken_pc_p;
static int     branch_cc;
static int redo_current_block;

int segvcount=0;
int soft_flush_count=0;
int hard_flush_count=0;
int checksum_count=0;
static uae_u8* current_compile_p=NULL;
static uae_u8* max_compile_start;
static uae_u8* compiled_code=NULL;
static uae_s32 reg_alloc_run;
const int POPALLSPACE_SIZE = 1024; /* That should be enough space */
static uae_u8 *popallspace=NULL;

void* pushall_call_handler=NULL;
static void* popall_do_nothing=NULL;
static void* popall_exec_nostats=NULL;
static void* popall_execute_normal=NULL;
static void* popall_cache_miss=NULL;
static void* popall_recompile_block=NULL;
static void* popall_check_checksum=NULL;

/* The 68k only ever executes from even addresses. So right now, we
 * waste half the entries in this array
 * UPDATE: We now use those entries to store the start of the linked
 * lists that we maintain for each hash result.
 */
static cacheline cache_tags[TAGSIZE];
int letit=0;
static blockinfo* hold_bi[MAX_HOLD_BI];
static blockinfo* active;
static blockinfo* dormant;

op_properties prop[65536];

#ifdef NOFLAGS_SUPPORT
/* 68040 */
extern const struct comptbl op_smalltbl_0_nf[];
#endif
extern const struct comptbl op_smalltbl_0_comp_nf[];
extern const struct comptbl op_smalltbl_0_comp_ff[];
#ifdef NOFLAGS_SUPPORT
/* 68020 + 68881 */
extern const struct cputbl op_smalltbl_1_nf[];
/* 68020 */
extern const struct cputbl op_smalltbl_2_nf[];
/* 68010 */
extern const struct cputbl op_smalltbl_3_nf[];
/* 68000 */
extern const struct cputbl op_smalltbl_4_nf[];
/* 68000 slow but compatible.  */
extern const struct cputbl op_smalltbl_5_nf[];
#endif

static bigstate live;
static smallstate empty_ss;
static smallstate default_ss;
static int optlev;

static int writereg(int r, int size);
static void unlock2(int r);
static void setlock(int r);
static int readreg_specific(int r, int size, int spec);
static int writereg_specific(int r, int size, int spec);
static void prepare_for_call_1(void);
static void prepare_for_call_2(void);
static void align_target(uae_u32 a);

static void inline flush_cpu_icache(void *from, void *to);
static void inline write_jmp_target(uae_u32 *jmpaddr, cpuop_func* a);
static void inline emit_jmp_target(uae_u32 a);

uae_u32 m68k_pc_offset;

/* Some arithmetic operations can be optimized away if the operands
 * are known to be constant. But that's only a good idea when the
 * side effects they would have on the flags are not important. This
 * variable indicates whether we need the side effects or not
 */
uae_u32 needflags=0;

/* Flag handling is complicated.
 *
 * x86 instructions create flags, which quite often are exactly what we
 * want. So at times, the "68k" flags are actually in the x86 flags.
 *
 * Then again, sometimes we do x86 instructions that clobber the x86
 * flags, but don't represent a corresponding m68k instruction. In that
 * case, we have to save them.
 *
 * We used to save them to the stack, but now store them back directly
 * into the regflags.cznv of the traditional emulation. Thus some odd
 * names.
 *
 * So flags can be in either of two places (used to be three; boy were
 * things complicated back then!); And either place can contain either
 * valid flags or invalid trash (and on the stack, there was also the
 * option of "nothing at all", now gone). A couple of variables keep
 * track of the respective states.
 *
 * To make things worse, we might or might not be interested in the flags.
 * by default, we are, but a call to dont_care_flags can change that
 * until the next call to live_flags. If we are not, pretty much whatever
 * is in the register and/or the native flags is seen as valid.
 */

static inline blockinfo* get_blockinfo(uae_u32 cl)
{
	return cache_tags[cl+1].bi;
}

static inline blockinfo* get_blockinfo_addr(void* addr)
{
	blockinfo*  bi=get_blockinfo(cacheline(addr));

	while (bi) {
		if (bi->pc_p==addr)
			return bi;
		bi=bi->next_same_cl;
	}
	return NULL;
}


/*******************************************************************
 * All sorts of list related functions for all of the lists        *
 *******************************************************************/

static inline void remove_from_cl_list(blockinfo* bi)
{
	uae_u32 cl=cacheline(bi->pc_p);

	if (bi->prev_same_cl_p)
		*(bi->prev_same_cl_p)=bi->next_same_cl;
	if (bi->next_same_cl)
		bi->next_same_cl->prev_same_cl_p=bi->prev_same_cl_p;
	if (cache_tags[cl+1].bi)
		cache_tags[cl].handler=cache_tags[cl+1].bi->handler_to_use;
	else
		cache_tags[cl].handler=(cpuop_func*)popall_execute_normal;
}

static inline void remove_from_list(blockinfo* bi)
{
	if (bi->prev_p)
		*(bi->prev_p)=bi->next;
	if (bi->next)
		bi->next->prev_p=bi->prev_p;
}

static inline void remove_from_lists(blockinfo* bi)
{
	remove_from_list(bi);
	remove_from_cl_list(bi);
}

static inline void add_to_cl_list(blockinfo* bi)
{
	uae_u32 cl=cacheline(bi->pc_p);

	if (cache_tags[cl+1].bi)
		cache_tags[cl+1].bi->prev_same_cl_p=&(bi->next_same_cl);
	bi->next_same_cl=cache_tags[cl+1].bi;

	cache_tags[cl+1].bi=bi;
	bi->prev_same_cl_p=&(cache_tags[cl+1].bi);

	cache_tags[cl].handler=bi->handler_to_use;
}

static inline void raise_in_cl_list(blockinfo* bi)
{
	remove_from_cl_list(bi);
	add_to_cl_list(bi);
}

static inline void add_to_active(blockinfo* bi)
{
	if (active)
		active->prev_p=&(bi->next);
	bi->next=active;

	active=bi;
	bi->prev_p=&active;
}

static inline void add_to_dormant(blockinfo* bi)
{
	if (dormant)
		dormant->prev_p=&(bi->next);
	bi->next=dormant;

	dormant=bi;
	bi->prev_p=&dormant;
}

static inline void remove_dep(dependency* d)
{
	if (d->prev_p)
		*(d->prev_p)=d->next;
	if (d->next)
		d->next->prev_p=d->prev_p;
	d->prev_p=NULL;
	d->next=NULL;
}

/* This block's code is about to be thrown away, so it no longer
   depends on anything else */
static inline void remove_deps(blockinfo* bi)
{
	remove_dep(&(bi->dep[0]));
	remove_dep(&(bi->dep[1]));
}

static inline void adjust_jmpdep(dependency* d, cpuop_func* a)
{
	*(d->jmp_off)=(uae_u32)a-((uae_u32)d->jmp_off+4);
}

/********************************************************************
 * Soft flush handling support functions                            *
 ********************************************************************/

static inline void set_dhtu(blockinfo* bi, cpuop_func *dh)
{
	jit_log2("bi is %p",bi);
	if (dh!=bi->direct_handler_to_use) {
		dependency* x=bi->deplist;
		jit_log2("bi->deplist=%p",bi->deplist);
		while (x) {
			jit_log2("x is %p",x);
			jit_log2("x->next is %p",x->next);
			jit_log2("x->prev_p is %p",x->prev_p);

			if (x->jmp_off) {
				adjust_jmpdep(x,dh);
			}
			x=x->next;
		}
		bi->direct_handler_to_use=dh;
	}
}

static inline void invalidate_block(blockinfo* bi)
{
	int i;

	bi->optlevel=0;
	bi->count=currprefs.optcount[0]-1;
	bi->handler=NULL;
	bi->handler_to_use=(cpuop_func*)popall_execute_normal;
	bi->direct_handler=NULL;
	set_dhtu(bi,bi->direct_pen);
	bi->needed_flags=0xff;

	for (i=0;i<2;i++) {
		bi->dep[i].jmp_off=NULL;
		bi->dep[i].target=NULL;
	}
	remove_deps(bi);
}

static inline void create_jmpdep(blockinfo* bi, int i, uae_u32* jmpaddr, uae_u32 target)
{
	blockinfo*  tbi=get_blockinfo_addr((void*)(uintptr)target);

	Dif(!tbi) {
		jit_abort("Could not create jmpdep!");
	}
	bi->dep[i].jmp_off=jmpaddr;
	bi->dep[i].target=tbi;
	bi->dep[i].next=tbi->deplist;
	if (bi->dep[i].next)
		bi->dep[i].next->prev_p=&(bi->dep[i].next);
	bi->dep[i].prev_p=&(tbi->deplist);
	tbi->deplist=&(bi->dep[i]);
}

static inline void attached_state(blockinfo* bi)
{
	bi->havestate=1;
	if (bi->direct_handler_to_use==bi->direct_handler)
		set_dhtu(bi,bi->direct_pen);
	bi->direct_handler=bi->direct_pen;
	bi->status=BI_TARGETTED;
}

#ifdef UAE
static inline blockinfo* get_blockinfo_addr_new(void* addr, int setstate)
#else
static inline blockinfo* get_blockinfo_addr_new(void* addr, int /* setstate */)
#endif
{
	blockinfo*  bi=get_blockinfo_addr(addr);
	int i;

	if (!bi) {
		for (i=0;i<MAX_HOLD_BI && !bi;i++) {
			if (hold_bi[i]) {
				(void)cacheline(addr);

				bi=hold_bi[i];
				hold_bi[i]=NULL;
				bi->pc_p=(uae_u8*)addr;
				invalidate_block(bi);
				add_to_active(bi);
				add_to_cl_list(bi);

			}
		}
	}
	if (!bi) {
		jit_abort("Looking for blockinfo, can't find free one");
	}

#if USE_MATCHSTATE
	if (setstate &&
		!bi->havestate) {
			big_to_small_state(&live,&(bi->env));
			attached_state(bi);
	}
#endif
	return bi;
}

static void prepare_block(blockinfo* bi);

static inline void alloc_blockinfos(void)
{
	int i;
	blockinfo* bi;

	for (i=0;i<MAX_HOLD_BI;i++) {
		if (hold_bi[i])
			return;
		bi=hold_bi[i]=(blockinfo*)current_compile_p;
		current_compile_p+=sizeof(blockinfo);

		prepare_block(bi);
	}
}

/********************************************************************
 * Functions to emit data into memory, and other general support    *
 ********************************************************************/

static uae_u8* target;

static inline void emit_byte(uae_u8 x)
{
	*target++=x;
}

static inline void emit_word(uae_u16 x)
{
	*((uae_u16*)target)=x;
	target+=2;
}

static inline void emit_long(uae_u32 x)
{
	*((uae_u32*)target)=x;
	target+=4;
}

static __inline__ void emit_quad(uae_u64 x)
{
    *((uae_u64*)target)=x;
    target+=8;
}

static inline void emit_block(const uae_u8 *block, uae_u32 blocklen)
{
	memcpy((uae_u8 *)target,block,blocklen);
	target+=blocklen;
}

static inline uae_u32 reverse32(uae_u32 v)
{
#if 0
	// gb-- We have specialized byteswapping functions, just use them
	return do_byteswap_32(v);
#else
	return ((v>>24)&0xff) | ((v>>8)&0xff00) | ((v<<8)&0xff0000) | ((v<<24)&0xff000000);
#endif
}

/********************************************************************
 * Getting the information about the target CPU                     *
 ********************************************************************/

#include "codegen_x86.cpp"

void set_target(uae_u8* t)
{
	target=t;
}

static inline uae_u8* get_target_noopt(void)
{
	return target;
}

inline uae_u8* get_target(void)
{
	return get_target_noopt();
}


/********************************************************************
 * Flags status handling. EMIT TIME!                                *
 ********************************************************************/

static void bt_l_ri_noclobber(RR4 r, IMM i);

static void make_flags_live_internal(void)
{
	if (live.flags_in_flags==VALID)
		return;
	Dif (live.flags_on_stack==TRASH) {
		jit_abort(_T("Want flags, got something on stack, but it is TRASH"));
	}
	if (live.flags_on_stack==VALID) {
		int tmp;
		tmp=readreg_specific(FLAGTMP,4,FLAG_NREG2);
		raw_reg_to_flags(tmp);
		unlock2(tmp);

		live.flags_in_flags=VALID;
		return;
	}
	jit_abort(_T("Huh? live.flags_in_flags=%d, live.flags_on_stack=%d, but need to make live"),
		live.flags_in_flags,live.flags_on_stack);
}

static void flags_to_stack(void)
{
	if (live.flags_on_stack==VALID)
		return;
	if (!live.flags_are_important) {
		live.flags_on_stack=VALID;
		return;
	}
	Dif (live.flags_in_flags!=VALID)
		jit_abort(_T("flags_to_stack != VALID"));
	else  {
		int tmp;
		tmp=writereg_specific(FLAGTMP,4,FLAG_NREG1);
		raw_flags_to_reg(tmp);
		unlock2(tmp);
	}
	live.flags_on_stack=VALID;
}

static inline void clobber_flags(void)
{
	if (live.flags_in_flags==VALID && live.flags_on_stack!=VALID)
		flags_to_stack();
	live.flags_in_flags=TRASH;
}

/* Prepare for leaving the compiled stuff */
static inline void flush_flags(void)
{
	flags_to_stack();
	return;
}

int touchcnt;

/********************************************************************
 * Partial register flushing for optimized calls                    *
 ********************************************************************/

struct regusage {
	uae_u16 rmask;
	uae_u16 wmask;
};

static inline void ru_set(uae_u16 *mask, int reg)
{
#if USE_OPTIMIZED_CALLS
	*mask |= 1 << reg;
#else
	UNUSED(mask);
	UNUSED(reg);
#endif
}

static inline bool ru_get(const uae_u16 *mask, int reg)
{
#if USE_OPTIMIZED_CALLS
	return (*mask & (1 << reg));
#else
	UNUSED(mask);
	UNUSED(reg);
	/* Default: instruction reads & write to register */
	return true;
#endif
}

static inline void ru_set_read(regusage *ru, int reg)
{
	ru_set(&ru->rmask, reg);
}

static inline void ru_set_write(regusage *ru, int reg)
{
	ru_set(&ru->wmask, reg);
}

static inline bool ru_read_p(const regusage *ru, int reg)
{
	return ru_get(&ru->rmask, reg);
}

static inline bool ru_write_p(const regusage *ru, int reg)
{
	return ru_get(&ru->wmask, reg);
}

#if 0
static void ru_fill_ea(regusage *ru, int reg, amodes mode,
					   wordsizes size, int write_mode)
{
	switch (mode) {
	case Areg:
		reg += 8;
		/* fall through */
	case Dreg:
		ru_set(write_mode ? &ru->wmask : &ru->rmask, reg);
		break;
	case Ad16:
		/* skip displacment */
		m68k_pc_offset += 2;
	case Aind:
	case Aipi:
	case Apdi:
		ru_set_read(ru, reg+8);
		break;
	case Ad8r:
		ru_set_read(ru, reg+8);
		/* fall through */
	case PC8r: {
		uae_u16 dp = comp_get_iword((m68k_pc_offset+=2)-2);
		reg = (dp >> 12) & 15;
		ru_set_read(ru, reg);
		if (dp & 0x100)
			m68k_pc_offset += (((dp & 0x30) >> 3) & 7) + ((dp & 3) * 2);
		break;
	}
	case PC16:
	case absw:
	case imm0:
	case imm1:
		m68k_pc_offset += 2;
		break;
	case absl:
	case imm2:
		m68k_pc_offset += 4;
		break;
	case immi:
		m68k_pc_offset += (size == sz_long) ? 4 : 2;
		break;
	}
}

/* TODO: split into a static initialization part and a dynamic one
   (instructions depending on extension words) */

static void ru_fill(regusage *ru, uae_u32 opcode)
{
	m68k_pc_offset += 2;

	/* Default: no register is used or written to */
	ru->rmask = 0;
	ru->wmask = 0;

	uae_u32 real_opcode = cft_map(opcode);
	struct instr *dp = &table68k[real_opcode];

	bool rw_dest = true;
	bool handled = false;

	/* Handle some instructions specifically */
	uae_u16 ext;
	switch (dp->mnemo) {
	case i_BFCHG:
	case i_BFCLR:
	case i_BFEXTS:
	case i_BFEXTU:
	case i_BFFFO:
	case i_BFINS:
	case i_BFSET:
	case i_BFTST:
		ext = comp_get_iword((m68k_pc_offset+=2)-2);
		if (ext & 0x800) ru_set_read(ru, (ext >> 6) & 7);
		if (ext & 0x020) ru_set_read(ru, ext & 7);
		ru_fill_ea(ru, dp->dreg, (amodes)dp->dmode, (wordsizes)dp->size, 1);
		if (dp->dmode == Dreg)
			ru_set_read(ru, dp->dreg);
		switch (dp->mnemo) {
		case i_BFEXTS:
		case i_BFEXTU:
		case i_BFFFO:
			ru_set_write(ru, (ext >> 12) & 7);
			break;
		case i_BFINS:
			ru_set_read(ru, (ext >> 12) & 7);
			/* fall through */
		case i_BFCHG:
		case i_BFCLR:
		case i_BSET:
			if (dp->dmode == Dreg)
				ru_set_write(ru, dp->dreg);
			break;
		}
		handled = true;
		rw_dest = false;
		break;

	case i_BTST:
		rw_dest = false;
		break;

	case i_CAS:
	{
		ext = comp_get_iword((m68k_pc_offset+=2)-2);
		int Du = ext & 7;
		ru_set_read(ru, Du);
		int Dc = (ext >> 6) & 7;
		ru_set_read(ru, Dc);
		ru_set_write(ru, Dc);
		break;
	}
	case i_CAS2:
	{
		int Dc1, Dc2, Du1, Du2, Rn1, Rn2;
		ext = comp_get_iword((m68k_pc_offset+=2)-2);
		Rn1 = (ext >> 12) & 15;
		Du1 = (ext >> 6) & 7;
		Dc1 = ext & 7;
		ru_set_read(ru, Rn1);
		ru_set_read(ru, Du1);
		ru_set_read(ru, Dc1);
		ru_set_write(ru, Dc1);
		ext = comp_get_iword((m68k_pc_offset+=2)-2);
		Rn2 = (ext >> 12) & 15;
		Du2 = (ext >> 6) & 7;
		Dc2 = ext & 7;
		ru_set_read(ru, Rn2);
		ru_set_read(ru, Du2);
		ru_set_write(ru, Dc2);
		break;
	}
	case i_DIVL: case i_MULL:
		m68k_pc_offset += 2;
		break;
	case i_LEA:
	case i_MOVE: case i_MOVEA: case i_MOVE16:
		rw_dest = false;
		break;
	case i_PACK: case i_UNPK:
		rw_dest = false;
		m68k_pc_offset += 2;
		break;
	case i_TRAPcc:
		m68k_pc_offset += (dp->size == sz_long) ? 4 : 2;
		break;
	case i_RTR:
		/* do nothing, just for coverage debugging */
		break;
	/* TODO: handle EXG instruction */
	}

	/* Handle A-Traps better */
	if ((real_opcode & 0xf000) == 0xa000) {
		handled = true;
	}

	/* Handle EmulOps better */
	if ((real_opcode & 0xff00) == 0x7100) {
		handled = true;
		ru->rmask = 0xffff;
		ru->wmask = 0;
	}

	if (dp->suse && !handled)
		ru_fill_ea(ru, dp->sreg, (amodes)dp->smode, (wordsizes)dp->size, 0);

	if (dp->duse && !handled)
		ru_fill_ea(ru, dp->dreg, (amodes)dp->dmode, (wordsizes)dp->size, 1);

	if (rw_dest)
		ru->rmask |= ru->wmask;

	handled = handled || dp->suse || dp->duse;

	/* Mark all registers as used/written if the instruction may trap */
	if (may_trap(opcode)) {
		handled = true;
		ru->rmask = 0xffff;
		ru->wmask = 0xffff;
	}

	if (!handled) {
		write_log("ru_fill: %04x = { %04x, %04x }\n",
				  real_opcode, ru->rmask, ru->wmask);
		abort();
	}
}
#endif

/********************************************************************
 * register allocation per block logging                            *
 ********************************************************************/

static uae_s8 vstate[VREGS];
static uae_s8 vwritten[VREGS];
static uae_s8 nstate[N_REGS];

#define L_UNKNOWN -127
#define L_UNAVAIL -1
#define L_NEEDED -2
#define L_UNNEEDED -3

#ifdef UAE
static inline void big_to_small_state(bigstate* b, smallstate* s)
{
	int i;
	int count=0;

	for (i=0;i<N_REGS;i++) {
		s->nat[i].validsize=0;
		s->nat[i].dirtysize=0;
		if (b->nat[i].nholds) {
			int index=b->nat[i].nholds-1;
			int r=b->nat[i].holds[index];
			s->nat[i].holds=r;
			s->nat[i].validsize=b->state[r].validsize;
			s->nat[i].dirtysize=b->state[r].dirtysize;
			count++;
		}
	}
	write_log (_T("JIT: count=%d\n"),count);
	for (i=0;i<N_REGS;i++) {  // FIXME --- don't do dirty yet
		s->nat[i].dirtysize=0;
	}
}
#else
static inline void big_to_small_state(bigstate * /* b */, smallstate * s)
{
  int i;
	
  for (i = 0; i < VREGS; i++)
	s->virt[i] = vstate[i];
  for (i = 0; i < N_REGS; i++)
	s->nat[i] = nstate[i];
}
#endif

#if 0
static inline int callers_need_recompile(bigstate * /* b */, smallstate * s)
{
  int i;
  int reverse = 0;

  for (i = 0; i < VREGS; i++) {
	if (vstate[i] != L_UNNEEDED && s->virt[i] == L_UNNEEDED)
	  return 1;
	if (vstate[i] == L_UNNEEDED && s->virt[i] != L_UNNEEDED)
	  reverse++;
  }
  for (i = 0; i < N_REGS; i++) {
	if (nstate[i] >= 0 && nstate[i] != s->nat[i])
	  return 1;
	if (nstate[i] < 0 && s->nat[i] >= 0)
	  reverse++;
  }
  if (reverse >= 2 && USE_MATCH)
	return 1;	/* In this case, it might be worth recompiling the
				 * callers */
  return 0;
}
#endif

static inline void log_startblock(void)
{
	int i;

	for (i = 0; i < VREGS; i++) {
		vstate[i]=L_UNKNOWN;
	}
	for (i=0;i<N_REGS;i++)
		nstate[i]=L_UNKNOWN;
}

/* Using an n-reg for a temp variable */
static inline void log_isused(int n)
{
	if (nstate[n]==L_UNKNOWN)
		nstate[n]=L_UNAVAIL;
}

static inline void log_visused(int r)
{
  if (vstate[r] == L_UNKNOWN)
	vstate[r] = L_NEEDED;
}

static inline void do_load_reg(int n, int r)
{
  if (r == FLAGTMP)
	raw_load_flagreg(n, r);
  else if (r == FLAGX)
	raw_load_flagx(n, r);
  else
	raw_mov_l_rm(n, (uintptr) live.state[r].mem);
}

static inline void check_load_reg(int n, int r)
{
  raw_mov_l_rm(n, (uintptr) live.state[r].mem);
}

static inline void log_vwrite(int r)
{
  vwritten[r] = 1;
}

/* Using an n-reg to hold a v-reg */
static inline void log_isreg(int n, int r)
{
	if (nstate[n] == L_UNKNOWN)
		nstate[n]=r;
	if (vstate[r]==L_UNKNOWN)
		vstate[r]=L_NEEDED;
}

static inline void log_clobberreg(int r)
{
	if (vstate[r]==L_UNKNOWN)
		vstate[r]=L_UNNEEDED;
}

/* This ends all possibility of clever register allocation */

static inline void log_flush(void)
{
	int i;
  
	for (i=0;i<VREGS;i++)
		if (vstate[i]==L_UNKNOWN)
			vstate[i]=L_NEEDED;
	for (i=0;i<N_REGS;i++)
		if (nstate[i]==L_UNKNOWN)
			nstate[i]=L_UNAVAIL;
}

static inline void log_dump(void)
{
	int i;

	return;

	jit_log("----------------------");
	for (i=0;i<N_REGS;i++) {
		switch(nstate[i]) {
		case L_UNKNOWN:
			jit_log("Nat %d : UNKNOWN",i);
			break;
		case L_UNAVAIL:
			jit_log("Nat %d : UNAVAIL",i);
			break;
		default:
			jit_log("Nat %d : %d",i,nstate[i]);
			break;
		}
	}
	for (i=0;i<VREGS;i++) {
		if (vstate[i] == L_UNNEEDED) {
			jit_log("Virt %d: UNNEEDED",i);
		}
	}
}

/********************************************************************
 * register status handling. EMIT TIME!                             *
 ********************************************************************/

static inline void set_status(int r, int status)
{
	if (status==ISCONST)
		log_clobberreg(r);
	live.state[r].status=status;
}

static inline int isinreg(int r)
{
	return live.state[r].status==CLEAN || live.state[r].status==DIRTY;
}

static inline void adjust_nreg(int r, uae_u32 val)
{
	if (!val)
		return;
	raw_lea_l_brr(r,r,val);
}

static  void tomem(int r)
{
	int rr=live.state[r].realreg;

	if (isinreg(r)) {
		if (live.state[r].val && live.nat[rr].nholds==1
			&& !live.nat[rr].locked) {
				jit_log2("RemovingA offset %x from reg %d (%d) at %p", live.state[r].val,r,rr,target);
				adjust_nreg(rr,live.state[r].val);
				live.state[r].val=0;
				live.state[r].dirtysize=4;
				set_status(r,DIRTY);
		}
	}

	if (live.state[r].status==DIRTY) {
		switch (live.state[r].dirtysize) {
		case 1: raw_mov_b_mr((uintptr)live.state[r].mem,rr); break;
		case 2: raw_mov_w_mr((uintptr)live.state[r].mem,rr); break;
		case 4: raw_mov_l_mr((uintptr)live.state[r].mem,rr); break;
		default: abort();
		}
		set_status(r,CLEAN);
		live.state[r].dirtysize=0;
	}
}

static inline int isconst(int r)
{
	return live.state[r].status==ISCONST;
}

int is_const(int r)
{
	return isconst(r);
}

static inline void writeback_const(int r)
{
	if (!isconst(r))
		return;
	Dif (live.state[r].needflush==NF_HANDLER) {
		jit_abort (_T("Trying to write back constant NF_HANDLER!"));
	}

	raw_mov_l_mi((uintptr)live.state[r].mem,live.state[r].val);
	live.state[r].val=0;
	set_status(r,INMEM);
}

static inline void tomem_c(int r)
{
	if (isconst(r)) {
		writeback_const(r);
	}
	else
		tomem(r);
}

static  void evict(int r)
{
	int rr;

	if (!isinreg(r))
		return;
	tomem(r);
	rr=live.state[r].realreg;

	Dif (live.nat[rr].locked &&
		live.nat[rr].nholds==1) {
			jit_abort (_T("register %d in nreg %d is locked!"),r,live.state[r].realreg);
	}

	live.nat[rr].nholds--;
	if (live.nat[rr].nholds!=live.state[r].realind) { /* Was not last */
		int topreg=live.nat[rr].holds[live.nat[rr].nholds];
		int thisind=live.state[r].realind;
	
		live.nat[rr].holds[thisind]=topreg;
		live.state[topreg].realind=thisind;
	}
	live.state[r].realreg=-1;
	set_status(r,INMEM);
}

static inline void free_nreg(int r)
{
	int i=live.nat[r].nholds;

	while (i) {
		int vr;

		--i;
		vr=live.nat[r].holds[i];
		evict(vr);
	}
	Dif (live.nat[r].nholds!=0) {
		jit_abort (_T("Failed to free nreg %d, nholds is %d"),r,live.nat[r].nholds);
	}
}

/* Use with care! */
static inline void isclean(int r)
{
	if (!isinreg(r))
		return;
	live.state[r].validsize=4;
	live.state[r].dirtysize=0;
	live.state[r].val=0;
	set_status(r,CLEAN);
}

static inline void disassociate(int r)
{
	isclean(r);
	evict(r);
}

static inline void set_const(int r, uae_u32 val)
{
	disassociate(r);
	live.state[r].val=val;
	set_status(r,ISCONST);
}

static inline uae_u32 get_offset(int r)
{
	return live.state[r].val;
}

static  int alloc_reg_hinted(int r, int size, int willclobber, int hint)
{
	int bestreg;
	uae_s32 when;
	int i;
	uae_s32 badness=0; /* to shut up gcc */
	bestreg=-1;
	when=2000000000;

	/* XXX use a regalloc_order table? */
	for (i=N_REGS;i--;) {
		badness=live.nat[i].touched;
		if (live.nat[i].nholds==0)
			badness=0;
		if (i==hint)
			badness-=200000000;
		if (!live.nat[i].locked && badness<when) {
			if ((size==1 && live.nat[i].canbyte) ||
				(size==2 && live.nat[i].canword) ||
				(size==4)) {
					bestreg=i;
					when=badness;
					if (live.nat[i].nholds==0 && hint<0)
						break;
					if (i==hint)
						break;
			}
		}
	}
	Dif (bestreg==-1)
		jit_abort(_T("alloc_reg_hinted bestreg=-1"));

	if (live.nat[bestreg].nholds>0) {
		free_nreg(bestreg);
	}
	if (isinreg(r)) {
		int rr=live.state[r].realreg;
		/* This will happen if we read a partially dirty register at a
		bigger size */
		Dif (willclobber || live.state[r].validsize>=size)
			jit_abort(_T("willclobber || live.state[r].validsize>=size"));
		Dif (live.nat[rr].nholds!=1)
			jit_abort(_T("live.nat[rr].nholds!=1"));
		if (size==4 && live.state[r].validsize==2) {
			log_isused(bestreg);
			raw_mov_l_rm(bestreg,(uintptr)live.state[r].mem);
			raw_bswap_32(bestreg);
			raw_zero_extend_16_rr(rr,rr);
			raw_zero_extend_16_rr(bestreg,bestreg);
			raw_bswap_32(bestreg);
			raw_lea_l_rr_indexed(rr,rr,bestreg);
			live.state[r].validsize=4;
			live.nat[rr].touched=touchcnt++;
			return rr;
		}
		if (live.state[r].validsize==1) {
			/* Nothing yet */
		}
		evict(r);
	}

	if (!willclobber) {
		if (live.state[r].status!=UNDEF) {
			if (isconst(r)) {
				raw_mov_l_ri(bestreg,live.state[r].val);
				live.state[r].val=0;
				live.state[r].dirtysize=4;
				set_status(r,DIRTY);
				log_isused(bestreg);
			}
			else {
				if (r==FLAGTMP)
					raw_load_flagreg(bestreg,r);
				else if (r==FLAGX)
					raw_load_flagx(bestreg,r);
				else {
					raw_mov_l_rm(bestreg,(uae_u32)live.state[r].mem);
				}
				live.state[r].dirtysize=0;
				set_status(r,CLEAN);
				log_isreg(bestreg,r);
			}
		}
		else {
			live.state[r].val=0;
			live.state[r].dirtysize=0;
			set_status(r,CLEAN);
			log_isused(bestreg);
		}
		live.state[r].validsize=4;
	}
	else { /* this is the easiest way, but not optimal. FIXME! */
		/* Now it's trickier, but hopefully still OK */
		if (!isconst(r) || size==4) {
			live.state[r].validsize=size;
			live.state[r].dirtysize=size;
			live.state[r].val=0;
			set_status(r,DIRTY);
			if (size == 4) {
				log_isused(bestreg);
			}			
			else {
				log_isreg(bestreg,r);
			}
		}
		else {
			if (live.state[r].status!=UNDEF)
				raw_mov_l_ri(bestreg,live.state[r].val);
			live.state[r].val=0;
			live.state[r].validsize=4;
			live.state[r].dirtysize=4;
			set_status(r,DIRTY);
			log_isused(bestreg);
		}
	}
	live.state[r].realreg=bestreg;
	live.state[r].realind=live.nat[bestreg].nholds;
	live.nat[bestreg].touched=touchcnt++;
	live.nat[bestreg].holds[live.nat[bestreg].nholds]=r;
	live.nat[bestreg].nholds++;

	return bestreg;
}

/*
static  int alloc_reg(int r, int size, int willclobber)
{
	return alloc_reg_hinted(r,size,willclobber,-1);
}
*/

static void unlock2(int r)
{
	Dif (!live.nat[r].locked)
		jit_abort(_T("unlock2 %d not locked"), r);
	live.nat[r].locked--;
}

static  void setlock(int r)
{
	live.nat[r].locked++;
}


static void mov_nregs(int d, int s)
{
	(void)live.nat[s].nholds;
	int nd=live.nat[d].nholds;
	int i;

	if (s==d)
		return;

	if (nd>0)
		free_nreg(d);

	raw_mov_l_rr(d,s);
	log_isused(d);

	for (i=0;i<live.nat[s].nholds;i++) {
		int vs=live.nat[s].holds[i];

		live.state[vs].realreg=d;
		live.state[vs].realind=i;
		live.nat[d].holds[i]=vs;
	}
	live.nat[d].nholds=live.nat[s].nholds;

	live.nat[s].nholds=0;
}


static inline void make_exclusive(int r, int size, int spec)
{
	reg_status oldstate;
	int rr=live.state[r].realreg;
	int nr;
	int nind;
	int ndirt=0;
	int i;

	if (!isinreg(r))
		return;
	if (live.nat[rr].nholds==1)
		return;
	for (i=0;i<live.nat[rr].nholds;i++) {
		int vr=live.nat[rr].holds[i];
		if (vr!=r &&
			(live.state[vr].status==DIRTY || live.state[vr].val))
			ndirt++;
	}
	if (!ndirt && size<live.state[r].validsize && !live.nat[rr].locked) {
		/* Everything else is clean, so let's keep this register */
		for (i=0;i<live.nat[rr].nholds;i++) {
			int vr=live.nat[rr].holds[i];
			if (vr!=r) {
				evict(vr);
				i--; /* Try that index again! */
			}
		}
		Dif (live.nat[rr].nholds!=1) {
			jit_abort (_T("natreg %d holds %d vregs, %d not exclusive"),
				rr,live.nat[rr].nholds,r);
		}
		return;
	}

	/* We have to split the register */
	oldstate=live.state[r];

	setlock(rr); /* Make sure this doesn't go away */
	/* Forget about r being in the register rr */
	disassociate(r);
	/* Get a new register, that we will clobber completely */
	if (oldstate.status==DIRTY) {
		/* If dirtysize is <4, we need a register that can handle the
		eventual smaller memory store! Thanks to Quake68k for exposing
		this detail ;-) */
		nr=alloc_reg_hinted(r,oldstate.dirtysize,1,spec);
	}
	else {
		nr=alloc_reg_hinted(r,4,1,spec);
	}
	nind=live.state[r].realind;
	live.state[r]=oldstate;   /* Keep all the old state info */
	live.state[r].realreg=nr;
	live.state[r].realind=nind;

	if (size<live.state[r].validsize) {
		if (live.state[r].val) {
			/* Might as well compensate for the offset now */
			raw_lea_l_brr(nr,rr,oldstate.val);
			live.state[r].val=0;
			live.state[r].dirtysize=4;
			set_status(r,DIRTY);
		}
		else
			raw_mov_l_rr(nr,rr);  /* Make another copy */
	}
	unlock2(rr);
}

static inline void add_offset(int r, uae_u32 off)
{
	live.state[r].val+=off;
}

static inline void remove_offset(int r, int spec)
{
	int rr;

	if (isconst(r))
		return;
	if (live.state[r].val==0)
		return;
	if (isinreg(r) && live.state[r].validsize<4)
		evict(r);

	if (!isinreg(r))
		alloc_reg_hinted(r,4,0,spec);

	Dif (live.state[r].validsize!=4) {
		jit_abort (_T("Validsize=%d in remove_offset"),live.state[r].validsize);
	}
	make_exclusive(r,0,-1);
	/* make_exclusive might have done the job already */
	if (live.state[r].val==0)
		return;

	rr=live.state[r].realreg;

	if (live.nat[rr].nholds==1) {
		jit_log2("RemovingB offset %x from reg %d (%d) at %p", live.state[r].val,r,rr,target);
		adjust_nreg(rr,live.state[r].val);
		live.state[r].dirtysize=4;
		live.state[r].val=0;
		set_status(r,DIRTY);
		return;
	}
	jit_abort(_T("Failed in remove_offset"));
}

static inline void remove_all_offsets(void)
{
	int i;

	for (i=0;i<VREGS;i++)
		remove_offset(i,-1);
}

static inline void flush_reg_count(void)
{
#ifdef RECORD_REGISTER_USAGE
    for (int r = 0; r < 16; r++)
	if (reg_count_local[r])
	    ADDQim(reg_count_local[r], ((uintptr)reg_count) + (8 * r), X86_NOREG, X86_NOREG, 1);
#endif
}

static inline void record_register(int r)
{
#ifdef RECORD_REGISTER_USAGE
    if (r < 16)
	reg_count_local[r]++;
#else
	UNUSED(r);
#endif
}

static inline int readreg_general(int r, int size, int spec, int can_offset)
{
	int n;
	int answer=-1;

	if (live.state[r].status==UNDEF) {
		jit_log("WARNING: Unexpected read of undefined register %d",r);
	}
	if (!can_offset)
		remove_offset(r,spec);

	if (isinreg(r) && live.state[r].validsize>=size) {
		n=live.state[r].realreg;
		switch(size) {
		case 1:
			if (live.nat[n].canbyte || spec>=0) {
				answer=n;
			}
			break;
		case 2:
			if (live.nat[n].canword || spec>=0) {
				answer=n;
			}
			break;
		case 4:
			answer=n;
			break;
		default: abort();
		}
		if (answer<0)
			evict(r);
	}
	/* either the value was in memory to start with, or it was evicted and
	is in memory now */
	if (answer<0) {
		answer=alloc_reg_hinted(r,spec>=0?4:size,0,spec);
	}

	if (spec>=0 && spec!=answer) {
		/* Too bad */
		mov_nregs(spec,answer);
		answer=spec;
	}
	live.nat[answer].locked++;
	live.nat[answer].touched=touchcnt++;
	return answer;
}



static int readreg(int r, int size)
{
	return readreg_general(r,size,-1,0);
}

static int readreg_specific(int r, int size, int spec)
{
	return readreg_general(r,size,spec,0);
}

static int readreg_offset(int r, int size)
{
	return readreg_general(r,size,-1,1);
}

/* writereg_general(r, size, spec)
 *
 * INPUT
 * - r    : mid-layer register
 * - size : requested size (1/2/4)
 * - spec : -1 if find or make a register free, otherwise specifies
 *          the physical register to use in any case
 *
 * OUTPUT
 * - hard (physical, x86 here) register allocated to virtual register r
 */
static inline int writereg_general(int r, int size, int spec)
{
	int n;
	int answer=-1;

	if (size<4) {
		remove_offset(r,spec);
	}

	make_exclusive(r,size,spec);
	if (isinreg(r)) {
		int nvsize=size>live.state[r].validsize?size:live.state[r].validsize;
		int ndsize=size>live.state[r].dirtysize?size:live.state[r].dirtysize;
		n=live.state[r].realreg;

		Dif (live.nat[n].nholds!=1)
			jit_abort (_T("live.nat[%d].nholds!=1"), n);
		switch(size) {
		case 1:
			if (live.nat[n].canbyte || spec>=0) {
				live.state[r].dirtysize=ndsize;
				live.state[r].validsize=nvsize;
				answer=n;
			}
			break;
		case 2:
			if (live.nat[n].canword || spec>=0) {
				live.state[r].dirtysize=ndsize;
				live.state[r].validsize=nvsize;
				answer=n;
			}
			break;
		case 4:
			live.state[r].dirtysize=ndsize;
			live.state[r].validsize=nvsize;
			answer=n;
			break;
		default: abort();
		}
		if (answer<0)
			evict(r);
	}
	/* either the value was in memory to start with, or it was evicted and
	is in memory now */
	if (answer<0) {
		answer=alloc_reg_hinted(r,size,1,spec);
	}
	if (spec>=0 && spec!=answer) {
		mov_nregs(spec,answer);
		answer=spec;
	}
	if (live.state[r].status==UNDEF)
		live.state[r].validsize=4;
	live.state[r].dirtysize=size>live.state[r].dirtysize?size:live.state[r].dirtysize;
	live.state[r].validsize=size>live.state[r].validsize?size:live.state[r].validsize;

	live.nat[answer].locked++;
	live.nat[answer].touched=touchcnt++;
	if (size==4) {
		live.state[r].val=0;
	}
	else {
		Dif (live.state[r].val) {
			jit_abort(_T("JIT: Problem with val\n"));
		}
	}
	set_status(r,DIRTY);
	return answer;
}

static int writereg(int r, int size)
{
	return writereg_general(r,size,-1);
}

static int writereg_specific(int r, int size, int spec)
{
	return writereg_general(r,size,spec);
}

static inline int rmw_general(int r, int wsize, int rsize, int spec)
{
	int n;
	int answer=-1;

	if (live.state[r].status==UNDEF) {
		jit_log("WARNING: Unexpected read of undefined register %d",r);
	}
	remove_offset(r,spec);
	make_exclusive(r,0,spec);

	Dif (wsize<rsize) {
		jit_abort(_T("Cannot handle wsize<rsize in rmw_general()"));
	}
	if (isinreg(r) && live.state[r].validsize>=rsize) {
		n=live.state[r].realreg;
		Dif (live.nat[n].nholds!=1)
			jit_abort (_T("live.nat[n].nholds!=1"), n);

		switch(rsize) {
		case 1:
			if (live.nat[n].canbyte || spec>=0) {
				answer=n;
			}
			break;
		case 2:
			if (live.nat[n].canword || spec>=0) {
				answer=n;
			}
			break;
		case 4:
			answer=n;
			break;
		default: abort();
		}
		if (answer<0)
			evict(r);
	}
	/* either the value was in memory to start with, or it was evicted and
	is in memory now */
	if (answer<0) {
		answer=alloc_reg_hinted(r,spec>=0?4:rsize,0,spec);
	}

	if (spec>=0 && spec!=answer) {
		/* Too bad */
		mov_nregs(spec,answer);
		answer=spec;
	}
	if (wsize>live.state[r].dirtysize)
		live.state[r].dirtysize=wsize;
	if (wsize>live.state[r].validsize)
		live.state[r].validsize=wsize;
	set_status(r,DIRTY);

	live.nat[answer].locked++;
	live.nat[answer].touched=touchcnt++;

	Dif (live.state[r].val) {
		jit_abort(_T("Problem with val(rmw)"));
	}
	return answer;
}

static int rmw(int r, int wsize, int rsize)
{
	return rmw_general(r,wsize,rsize,-1);
}

static int rmw_specific(int r, int wsize, int rsize, int spec)
{
	return rmw_general(r,wsize,rsize,spec);
}


/* needed for restoring the carry flag on non-P6 cores */
static void bt_l_ri_noclobber(RR4 r, IMM i)
{
	int size=4;
	if (i<16)
		size=2;
	r=readreg(r,size);
	raw_bt_l_ri(r,i);
	unlock2(r);
}

/********************************************************************
 * FPU register status handling. EMIT TIME!                         *
 ********************************************************************/

static  void f_tomem(int r)
{
	if (live.fate[r].status==DIRTY) {
#if USE_LONG_DOUBLE
		raw_fmov_ext_mr((uintptr)live.fate[r].mem,live.fate[r].realreg);
#else
		raw_fmov_mr((uintptr)live.fate[r].mem,live.fate[r].realreg);
#endif
		live.fate[r].status=CLEAN;
	}
}

static  void f_tomem_drop(int r)
{
	if (live.fate[r].status==DIRTY) {
#if USE_LONG_DOUBLE
		raw_fmov_ext_mr_drop((uintptr)live.fate[r].mem,live.fate[r].realreg);
#else
		raw_fmov_mr_drop((uintptr)live.fate[r].mem,live.fate[r].realreg);
#endif
		live.fate[r].status=INMEM;
	}
}


static inline int f_isinreg(int r)
{
	return live.fate[r].status==CLEAN || live.fate[r].status==DIRTY;
}

static void f_evict(int r)
{
	int rr;

	if (!f_isinreg(r))
		return;
	rr=live.fate[r].realreg;
	if (live.fat[rr].nholds==1)
		f_tomem_drop(r);
	else
		f_tomem(r);

	Dif (live.fat[rr].locked &&
		live.fat[rr].nholds==1) {
			jit_abort(_T("FPU register %d in nreg %d is locked!"),r,live.fate[r].realreg);
	}

	live.fat[rr].nholds--;
	if (live.fat[rr].nholds!=live.fate[r].realind) { /* Was not last */
		int topreg=live.fat[rr].holds[live.fat[rr].nholds];
		int thisind=live.fate[r].realind;
		live.fat[rr].holds[thisind]=topreg;
		live.fate[topreg].realind=thisind;
	}
	live.fate[r].status=INMEM;
	live.fate[r].realreg=-1;
}

static inline void f_free_nreg(int r)
{
	int i=live.fat[r].nholds;

	while (i) {
		int vr;

		--i;
		vr=live.fat[r].holds[i];
		f_evict(vr);
	}
	Dif (live.fat[r].nholds!=0) {
		jit_abort(_T("Failed to free nreg %d, nholds is %d"),r,live.fat[r].nholds);
	}
}


/* Use with care! */
static inline void f_isclean(int r)
{
	if (!f_isinreg(r))
		return;
	live.fate[r].status=CLEAN;
}

static inline void f_disassociate(int r)
{
	f_isclean(r);
	f_evict(r);
}



static  int f_alloc_reg(int r, int willclobber)
{
	int bestreg;
	uae_s32 when;
	int i;
	uae_s32 badness;
	bestreg=-1;
	when=2000000000;
	for (i=N_FREGS;i--;) {
		badness=live.fat[i].touched;
		if (live.fat[i].nholds==0)
			badness=0;

		if (!live.fat[i].locked && badness<when) {
			bestreg=i;
			when=badness;
			if (live.fat[i].nholds==0)
				break;
		}
	}
	Dif (bestreg==-1)
		abort();

	if (live.fat[bestreg].nholds>0) {
		f_free_nreg(bestreg);
	}
	if (f_isinreg(r)) {
		f_evict(r);
	}

	if (!willclobber) {
		if (live.fate[r].status!=UNDEF) {
#if USE_LONG_DOUBLE
			raw_fmov_ext_rm(bestreg,(uintptr)live.fate[r].mem);
#else
			raw_fmov_rm(bestreg,(uintptr)live.fate[r].mem);
#endif
		}
		live.fate[r].status=CLEAN;
	}
	else {
		live.fate[r].status=DIRTY;
	}
	live.fate[r].realreg=bestreg;
	live.fate[r].realind=live.fat[bestreg].nholds;
	live.fat[bestreg].touched=touchcnt++;
	live.fat[bestreg].holds[live.fat[bestreg].nholds]=r;
	live.fat[bestreg].nholds++;

	return bestreg;
}

static  void f_unlock(int r)
{
	Dif (!live.fat[r].locked)
		jit_abort (_T("unlock %d"), r);
	live.fat[r].locked--;
}

static  void f_setlock(int r)
{
	live.fat[r].locked++;
}

static inline int f_readreg(int r)
{
	int n;
	int answer=-1;

	if (f_isinreg(r)) {
		n=live.fate[r].realreg;
		answer=n;
	}
	/* either the value was in memory to start with, or it was evicted and
	is in memory now */
	if (answer<0)
		answer=f_alloc_reg(r,0);

	live.fat[answer].locked++;
	live.fat[answer].touched=touchcnt++;
	return answer;
}

static inline void f_make_exclusive(int r, int clobber)
{
	freg_status oldstate;
	int rr=live.fate[r].realreg;
	int nr;
	int nind;
	int ndirt=0;
	int i;

	if (!f_isinreg(r))
		return;
	if (live.fat[rr].nholds==1)
		return;
	for (i=0;i<live.fat[rr].nholds;i++) {
		int vr=live.fat[rr].holds[i];
		if (vr!=r && live.fate[vr].status==DIRTY)
			ndirt++;
	}
	if (!ndirt && !live.fat[rr].locked) {
		/* Everything else is clean, so let's keep this register */
		for (i=0;i<live.fat[rr].nholds;i++) {
			int vr=live.fat[rr].holds[i];
			if (vr!=r) {
				f_evict(vr);
				i--; /* Try that index again! */
			}
		}
		Dif (live.fat[rr].nholds!=1) {
			jit_log("realreg %d holds %d (",rr,live.fat[rr].nholds);
			for (i=0;i<live.fat[rr].nholds;i++) {
				jit_log(" %d(%d,%d)",live.fat[rr].holds[i],
					live.fate[live.fat[rr].holds[i]].realreg,
					live.fate[live.fat[rr].holds[i]].realind);
			}
			jit_log("");
			jit_abort("x");
		}
		return;
	}

	/* We have to split the register */
	oldstate=live.fate[r];

	f_setlock(rr); /* Make sure this doesn't go away */
	/* Forget about r being in the register rr */
	f_disassociate(r);
	/* Get a new register, that we will clobber completely */
	nr=f_alloc_reg(r,1);
	nind=live.fate[r].realind;
	if (!clobber)
		raw_fmov_rr(nr,rr);  /* Make another copy */
	live.fate[r]=oldstate;   /* Keep all the old state info */
	live.fate[r].realreg=nr;
	live.fate[r].realind=nind;
	f_unlock(rr);
}


static inline int f_writereg(int r)
{
	int n;
	int answer=-1;

	f_make_exclusive(r,1);
	if (f_isinreg(r)) {
		n=live.fate[r].realreg;
		answer=n;
	}
	if (answer<0) {
		answer=f_alloc_reg(r,1);
	}
	live.fate[r].status=DIRTY;
	live.fat[answer].locked++;
	live.fat[answer].touched=touchcnt++;
	return answer;
}

static int f_rmw(int r)
{
	int n;

	f_make_exclusive(r,0);
	if (f_isinreg(r)) {
		n=live.fate[r].realreg;
	}
	else
		n=f_alloc_reg(r,0);
	live.fate[r].status=DIRTY;
	live.fat[n].locked++;
	live.fat[n].touched=touchcnt++;
	return n;
}

static void fflags_into_flags_internal(uae_u32 tmp)
{
	int r;

	clobber_flags();
	r=f_readreg(FP_RESULT);
	raw_fflags_into_flags(r);
	f_unlock(r);
}


#if defined(CPU_arm)
#include "compemu_midfunc_arm.cpp"
#endif

#include "compemu_midfunc_x86.cpp"

/********************************************************************
 * Support functions exposed to gencomp. CREATE time                *
 ********************************************************************/

#ifdef UAE
#else
void set_zero(int r, int tmp)
{
    if (setzflg_uses_bsf)
	bsf_l_rr(r,r);
    else
	simulate_bsf(tmp,r);
}
#endif

int kill_rodent(int r)
{
	return KILLTHERAT &&
		have_rat_stall &&
		(live.state[r].status==INMEM ||
		live.state[r].status==CLEAN ||
		live.state[r].status==ISCONST ||
		live.state[r].dirtysize==4);
}

uae_u32 get_const(int r)
{
		Dif (!isconst(r)) {
			jit_abort (_T("Register %d should be constant, but isn't"),r);
	}
	return live.state[r].val;
}

void sync_m68k_pc(void)
{
	if (m68k_pc_offset) {
		add_l_ri(PC_P,m68k_pc_offset);
		comp_pc_p+=m68k_pc_offset;
		m68k_pc_offset=0;
	}
}

#ifdef UAE
uae_u32 scratch[VREGS];
fptype fscratch[VFREGS];
#else
/********************************************************************
 * Scratch registers management                                     *
 ********************************************************************/

struct scratch_t {
	uae_u32		regs[VREGS];
	fpu_register	fregs[VFREGS];
};

static scratch_t scratch;
#endif

/********************************************************************
 * Support functions exposed to newcpu                              *
 ********************************************************************/

static inline const char *str_on_off(bool b)
{
	return b ? "on" : "off";
}

#if 0
bool compiler_use_jit(void)
{
	// Check for the "jit" prefs item
	if (!bx_options.jit.jit)
		return false;
	
	// Don't use JIT if translation cache size is less then MIN_CACHE_SIZE KB
	if (bx_options.jit.jitcachesize < MIN_CACHE_SIZE) {
		panicbug("<JIT compiler> : translation cache size is less than %d KB. Disabling JIT.\n", MIN_CACHE_SIZE);
		return false;
	}
	
	return true;
}
#endif

void init_comp(void)
{
	int i;
	uae_u8* cb=can_byte;
	uae_u8* cw=can_word;
	uae_u8* au=always_used;

	for (i=0;i<VREGS;i++) {
		live.state[i].realreg=-1;
		live.state[i].needflush=NF_SCRATCH;
		live.state[i].val=0;
		set_status(i,UNDEF);
	}

	for (i=0;i<VFREGS;i++) {
		live.fate[i].status=UNDEF;
		live.fate[i].realreg=-1;
		live.fate[i].needflush=NF_SCRATCH;
	}

	for (i=0;i<VREGS;i++) {
		if (i<16) { /* First 16 registers map to 68k registers */
			live.state[i].mem=&regs.regs[i];
			live.state[i].needflush=NF_TOMEM;
			set_status(i,INMEM);
		}
		else
			live.state[i].mem=scratch+i;
	}
	live.state[PC_P].mem=(uae_u32*)&(regs.pc_p);
	live.state[PC_P].needflush=NF_TOMEM;
	set_const(PC_P,(uintptr)comp_pc_p);

	live.state[FLAGX].mem=(uae_u32*)&(regflags.x);
	live.state[FLAGX].needflush=NF_TOMEM;
	set_status(FLAGX,INMEM);

#if defined(CPU_arm)
	live.state[FLAGTMP].mem=(uae_u32*)&(regflags.nzcv);
#else
	live.state[FLAGTMP].mem=(uae_u32*)&(regflags.cznv);
#endif
	live.state[FLAGTMP].needflush=NF_TOMEM;
	set_status(FLAGTMP,INMEM);

	live.state[NEXT_HANDLER].needflush=NF_HANDLER;
	set_status(NEXT_HANDLER,UNDEF);

	for (i=0;i<VFREGS;i++) {
		if (i<8) { /* First 8 registers map to 68k FPU registers */
			live.fate[i].mem=(uae_u32*)(&regs.fp[i].fp);
			live.fate[i].needflush=NF_TOMEM;
			live.fate[i].status=INMEM;
		}
		else if (i==FP_RESULT) {
			live.fate[i].mem=(uae_u32*)(&regs.fp_result);
			live.fate[i].needflush=NF_TOMEM;
			live.fate[i].status=INMEM;
		}
		else
			live.fate[i].mem=(uae_u32*)(fscratch+i);
	}


	for (i=0;i<N_REGS;i++) {
		live.nat[i].touched=0;
		live.nat[i].nholds=0;
		live.nat[i].locked=0;
		if (*cb==i) {
			live.nat[i].canbyte=1; cb++;
		} else live.nat[i].canbyte=0;
		if (*cw==i) {
			live.nat[i].canword=1; cw++;
		} else live.nat[i].canword=0;
		if (*au==i) {
			live.nat[i].locked=1; au++;
		}
	}

	for (i=0;i<N_FREGS;i++) {
		live.fat[i].touched=0;
		live.fat[i].nholds=0;
		live.fat[i].locked=0;
	}

	touchcnt=1;
	m68k_pc_offset=0;
	live.flags_in_flags=TRASH;
	live.flags_on_stack=VALID;
	live.flags_are_important=1;

	raw_fp_init();
}

static void vinton(int i, uae_s8* vton, int depth)
{
	int n;
	int rr;

	Dif (vton[i]==-1) {
		jit_abort (_T("JIT: Asked to load register %d, but nowhere to go\n"),i);
	}
	n=vton[i];
	Dif (live.nat[n].nholds>1)
		jit_abort (_T("vinton"));
	if (live.nat[n].nholds && depth<N_REGS) {
		vinton(live.nat[n].holds[0],vton,depth+1);
	}
	if (!isinreg(i))
		return;  /* Oops --- got rid of that one in the recursive calls */
	rr=live.state[i].realreg;
	if (rr!=n)
		mov_nregs(n,rr);
}

#if USE_MATCHSTATE
/* This is going to be, amongst other things, a more elaborate version of
flush() */
static inline void match_states(smallstate* s)
{
	uae_s8 vton[VREGS];
	uae_s8 ndone[N_REGS];
	int i;
	int again=0;

	for (i=0;i<VREGS;i++)
		vton[i]=-1;

	for (i=0;i<N_REGS;i++)
		if (s->nat[i].validsize)
			vton[s->nat[i].holds]=i;

	flush_flags(); /* low level */
	sync_m68k_pc(); /* mid level */

	/* We don't do FREGS yet, so this is raw flush() code */
	for (i=0;i<VFREGS;i++) {
		if (live.fate[i].needflush==NF_SCRATCH ||
			live.fate[i].status==CLEAN) {
				f_disassociate(i);
		}
	}
	for (i=0;i<VFREGS;i++) {
		if (live.fate[i].needflush==NF_TOMEM &&
			live.fate[i].status==DIRTY) {
				f_evict(i);
		}
	}
	raw_fp_cleanup_drop();

	/* Now comes the fun part. First, we need to remove all offsets */
	for (i=0;i<VREGS;i++)
		if (!isconst(i) && live.state[i].val)
			remove_offset(i,-1);

	/* Next, we evict everything that does not end up in registers,
	write back overly dirty registers, and write back constants */
	for (i=0;i<VREGS;i++) {
		switch (live.state[i].status) {
		case ISCONST:
			if (i!=PC_P)
				writeback_const(i);
			break;
		case DIRTY:
			if (vton[i]==-1) {
				evict(i);
				break;
			}
			if (live.state[i].dirtysize>s->nat[vton[i]].dirtysize)
				tomem(i);
			/* Fall-through! */
		case CLEAN:
			if (vton[i]==-1 ||
				live.state[i].validsize<s->nat[vton[i]].validsize)
				evict(i);
			else
				make_exclusive(i,0,-1);
			break;
		case INMEM:
			break;
		case UNDEF:
			break;
		default:
			write_log (_T("JIT: Weird status: %d\n"),live.state[i].status);
			abort();
		}
	}

	/* Quick consistency check */
	for (i=0;i<VREGS;i++) {
		if (isinreg(i)) {
			int n=live.state[i].realreg;

			if (live.nat[n].nholds!=1) {
				write_log (_T("JIT: Register %d isn't alone in nreg %d\n"),
					i,n);
				abort();
			}
			if (vton[i]==-1) {
				write_log (_T("JIT: Register %d is still in register, shouldn't be\n"),
					i);
				abort();
			}
		}
	}

	/* Now we need to shuffle things around so the VREGs are in the
	right N_REGs. */
	for (i=0;i<VREGS;i++) {
		if (isinreg(i) && vton[i]!=live.state[i].realreg)
			vinton(i,vton,0);
	}

	/* And now we may need to load some registers from memory */
	for (i=0;i<VREGS;i++) {
		int n=vton[i];
		if (n==-1) {
			Dif (isinreg(i)) {
				write_log (_T("JIT: Register %d unexpectedly in nreg %d\n"),
					i,live.state[i].realreg);
				abort();
			}
		}
		else {
			switch(live.state[i].status) {
			case CLEAN:
			case DIRTY:
				Dif (n!=live.state[i].realreg)
					abort();
				break;
			case INMEM:
				Dif (live.nat[n].nholds) {
					write_log (_T("JIT: natreg %d holds %d vregs, should be empty\n"),
						n,live.nat[n].nholds);
				}
				raw_mov_l_rm(n,(uae_u32)live.state[i].mem);
				live.state[i].validsize=4;
				live.state[i].dirtysize=0;
				live.state[i].realreg=n;
				live.state[i].realind=0;
				live.state[i].val=0;
				live.state[i].is_swapped=0;
				live.nat[n].nholds=1;
				live.nat[n].holds[0]=i;

				set_status(i,CLEAN);
				break;
			case ISCONST:
				if (i!=PC_P) {
					write_log (_T("JIT: Got constant in matchstate for reg %d. Bad!\n"),i);
					abort();
				}
				break;
			case UNDEF:
				break;
			}
		}
	}

	/* One last consistency check, and adjusting the states in live
	to those in s */
	for (i=0;i<VREGS;i++) {
		int n=vton[i];
		switch(live.state[i].status) {
		case INMEM:
			if (n!=-1)
				abort();
			break;
		case ISCONST:
			if (i!=PC_P)
				abort();
			break;
		case CLEAN:
		case DIRTY:
			if (n==-1)
				abort();
			if (live.state[i].dirtysize>s->nat[n].dirtysize)
				abort;
			if (live.state[i].validsize<s->nat[n].validsize)
				abort;
			live.state[i].dirtysize=s->nat[n].dirtysize;
			live.state[i].validsize=s->nat[n].validsize;
			if (live.state[i].dirtysize)
				set_status(i,DIRTY);
			break;
		case UNDEF:
			break;
		}
		if (n!=-1)
			live.nat[n].touched=touchcnt++;
	}
}
#else
static inline void match_states(smallstate* s)
{
	flush(1);
}
#endif

/* Only do this if you really mean it! The next call should be to init!*/
void flush(int save_regs)
{
	int i;

	log_flush();
	flush_flags(); /* low level */
	sync_m68k_pc(); /* mid level */

	if (save_regs) {
		for (i=0;i<VFREGS;i++) {
			if (live.fate[i].needflush==NF_SCRATCH ||
				live.fate[i].status==CLEAN) {
					f_disassociate(i);
			}
		}
		for (i=0;i<VREGS;i++) {
			if (live.state[i].needflush==NF_TOMEM) {
				switch(live.state[i].status) {
				case INMEM:
					if (live.state[i].val) {
						raw_add_l_mi((uintptr)live.state[i].mem,live.state[i].val);
						live.state[i].val=0;
					}
					break;
				case CLEAN:
				case DIRTY:
					remove_offset(i,-1); tomem(i); break;
				case ISCONST:
					if (i!=PC_P)
						writeback_const(i);
					break;
				default: break;
				}
				Dif (live.state[i].val && i!=PC_P) {
					jit_log("Register %d still has val %x", i,live.state[i].val);
				}
			}
		}
		for (i=0;i<VFREGS;i++) {
			if (live.fate[i].needflush==NF_TOMEM &&
				live.fate[i].status==DIRTY) {
					f_evict(i);
			}
		}
		raw_fp_cleanup_drop();
	}
	if (needflags) {
		jit_log("Warning! flush with needflags=1!");
	}
}

static void flush_keepflags(void)
{
	int i;

	for (i=0;i<VFREGS;i++) {
		if (live.fate[i].needflush==NF_SCRATCH ||
			live.fate[i].status==CLEAN) {
				f_disassociate(i);
		}
	}
	for (i=0;i<VREGS;i++) {
		if (live.state[i].needflush==NF_TOMEM) {
			switch(live.state[i].status) {
			case INMEM:
				/* Can't adjust the offset here --- that needs "add" */
				break;
			case CLEAN:
			case DIRTY:
				remove_offset(i,-1); tomem(i); break;
			case ISCONST:
				if (i!=PC_P)
					writeback_const(i);
				break;
			default: break;
			}
		}
	}
	for (i=0;i<VFREGS;i++) {
		if (live.fate[i].needflush==NF_TOMEM &&
			live.fate[i].status==DIRTY) {
				f_evict(i);
		}
	}
	raw_fp_cleanup_drop();
}

void freescratch(void)
{
	int i;
	for (i=0;i<N_REGS;i++)
		if (live.nat[i].locked && i!=4) {
			jit_log("Warning! %d is locked",i);
	}

	for (i=0;i<VREGS;i++)
		if (live.state[i].needflush==NF_SCRATCH) {
			forget_about(i);
		}

		for (i=0;i<VFREGS;i++)
			if (live.fate[i].needflush==NF_SCRATCH) {
				f_forget_about(i);
			}
}

/********************************************************************
 * Support functions, internal                                      *
 ********************************************************************/


static void align_target(uae_u32 a)
{
	if (!a)
		return;

	/* Fill with NOPs --- makes debugging with gdb easier */
	while ((uintptr)target&(a-1))
		*target++=0x90; // Attention x86 specific code
}

static inline int isinrom(uintptr addr)
{
#ifdef UAE
	return (addr>=(uae_u32)kickmem_bank.baseaddr &&
		addr<(uae_u32)kickmem_bank.baseaddr+8*65536);
#else
	return ((addr >= (uintptr)ROMBaseHost) && (addr < (uintptr)ROMBaseHost + ROMSize));
#endif
}

static void flush_all(void)
{
	int i;

	log_flush();
	for (i=0;i<VREGS;i++)
		if (live.state[i].status==DIRTY) {
			if (!call_saved[live.state[i].realreg]) {
				tomem(i);
			}
		}
		for (i=0;i<VFREGS;i++)
			if (f_isinreg(i))
				f_evict(i);
		raw_fp_cleanup_drop();
}

/* Make sure all registers that will get clobbered by a call are
   save and sound in memory */
static void prepare_for_call_1(void)
{
	flush_all();  /* If there are registers that don't get clobbered,
				  * we should be a bit more selective here */
}

/* We will call a C routine in a moment. That will clobber all registers,
   so we need to disassociate everything */
static void prepare_for_call_2(void)
{
	int i;
	for (i=0;i<N_REGS;i++)
		if (!call_saved[i] && live.nat[i].nholds>0)
			free_nreg(i);

	for (i=0;i<N_FREGS;i++)
		if (live.fat[i].nholds>0)
			f_free_nreg(i);

	live.flags_in_flags=TRASH;  /* Note: We assume we already rescued the
								flags at the very start of the call_r
								functions! */
}

/********************************************************************
 * Memory access and related functions, CREATE time                 *
 ********************************************************************/

void register_branch(uae_u32 not_taken, uae_u32 taken, uae_u8 cond)
{
	next_pc_p=not_taken;
	taken_pc_p=taken;
	branch_cc=cond;
}

/*
static uae_u32 get_handler_address(uae_u32 addr)
{
	(void)cacheline(addr);
	blockinfo* bi=get_blockinfo_addr_new((void*)(uintptr)addr,0);
	return (uintptr)&(bi->direct_handler_to_use);
}
*/

/* Note: get_handler may fail in 64 Bit environments, if direct_handler_to_use is
 * 		 outside 32 bit
 */
static uae_u32 get_handler(uae_u32 addr)
{
	(void)cacheline(addr);
	blockinfo* bi=get_blockinfo_addr_new((void*)(uintptr)addr,0);
	return (uintptr)bi->direct_handler_to_use;
}

/*
static void load_handler(int reg, uae_u32 addr)
{
	mov_l_rm(reg,get_handler_address(addr));
}
*/

/* This version assumes that it is writing *real* memory, and *will* fail
*  if that assumption is wrong! No branches, no second chances, just
*  straight go-for-it attitude */

static void writemem_real(int address, int source, int offset, int size, int tmp, int clobber)
{
	int f=tmp;

#ifdef NATMEM_OFFSET
	if (canbang) {  /* Woohoo! go directly at the memory! */
		if (clobber)
			f=source;
		switch(size) {
		case 1: mov_b_bRr(address,source,MEMBaseDiff); break;
		case 2: mov_w_rr(f,source); gen_bswap_16(f); mov_w_bRr(address,f,MEMBaseDiff); break;
		case 4: mov_l_rr(f,source); gen_bswap_32(f); mov_l_bRr(address,f,MEMBaseDiff); break;
		}
		forget_about(tmp);
		forget_about(f);
		return;
	}
#endif

	mov_l_rr(f,address);
	shrl_l_ri(f,16);  /* The index into the baseaddr table */
	mov_l_rm_indexed(f,(uae_u32)(baseaddr),f);

	if (address==source) { /* IBrowse does this! */
		if (size > 1) {
			add_l(f,address); /* f now holds the final address */
			switch (size) {
			case 2: gen_bswap_16(source); mov_w_Rr(f,source,0);
				gen_bswap_16(source); return;
			case 4: gen_bswap_32(source); mov_l_Rr(f,source,0);
				gen_bswap_32(source); return;
			}
		}
	}
	switch (size) { /* f now holds the offset */
	case 1: mov_b_mrr_indexed(address,f,source); break;
	case 2: gen_bswap_16(source); mov_w_mrr_indexed(address,f,source);
		gen_bswap_16(source); break;	   /* base, index, source */
	case 4: gen_bswap_32(source); mov_l_mrr_indexed(address,f,source);
		gen_bswap_32(source); break;
	}
}

static inline void writemem(int address, int source, int offset, int size, int tmp)
{
	int f=tmp;

	mov_l_rr(f,address);
	shrl_l_ri(f,16);   /* The index into the mem bank table */
	mov_l_rm_indexed(f,(uae_u32)mem_banks,f);
	/* Now f holds a pointer to the actual membank */
	mov_l_rR(f,f,offset);
	/* Now f holds the address of the b/w/lput function */
	call_r_02(f,address,source,4,size);
	forget_about(tmp);
}

void writebyte(int address, int source, int tmp)
{
	int distrust = currprefs.comptrustbyte;
	if ((special_mem&S_WRITE) || distrust)
		writemem_special(address,source,20,1,tmp);
	else
		writemem_real(address,source,20,1,tmp,0);
}

static inline void writeword_general(int address, int source, int tmp,
	int clobber)
{
	int distrust = currprefs.comptrustword;
	if ((special_mem&S_WRITE) || distrust)
		writemem_special(address,source,16,2,tmp);
	else
		writemem_real(address,source,16,2,tmp,clobber);
}

void writeword_clobber(int address, int source, int tmp)
{
	writeword_general(address,source,tmp,1);
}

void writeword(int address, int source, int tmp)
{
	writeword_general(address,source,tmp,0);
}

static inline void writelong_general(int address, int source, int tmp,
	int clobber)
{
	int  distrust = currprefs.comptrustlong;
	if ((special_mem&S_WRITE) || distrust)
		writemem_special(address,source,12,4,tmp);
	else
		writemem_real(address,source,12,4,tmp,clobber);
}

void writelong_clobber(int address, int source, int tmp)
{
	writelong_general(address,source,tmp,1);
}

void writelong(int address, int source, int tmp)
{
	writelong_general(address,source,tmp,0);
}



/* This version assumes that it is reading *real* memory, and *will* fail
*  if that assumption is wrong! No branches, no second chances, just
*  straight go-for-it attitude */

static void readmem_real(int address, int dest, int offset, int size, int tmp)
{
	int f=tmp;

	if (size==4 && address!=dest)
		f=dest;

#ifdef NATMEM_OFFSET
	if (canbang) {  /* Woohoo! go directly at the memory! */
		switch(size) {
		case 1: mov_b_brR(dest,address,MEMBaseDiff); break;
		case 2: mov_w_brR(dest,address,MEMBaseDiff); gen_bswap_16(dest); break;
		case 4: mov_l_brR(dest,address,MEMBaseDiff); gen_bswap_32(dest); break;
		}
		forget_about(tmp);
		return;
	}
#endif

	mov_l_rr(f,address);
	shrl_l_ri(f,16);   /* The index into the baseaddr table */
	mov_l_rm_indexed(f,(uae_u32)baseaddr,f);
	/* f now holds the offset */

	switch(size) {
	case 1: mov_b_rrm_indexed(dest,address,f); break;
	case 2: mov_w_rrm_indexed(dest,address,f); gen_bswap_16(dest); break;
	case 4: mov_l_rrm_indexed(dest,address,f); gen_bswap_32(dest); break;
	}
	forget_about(tmp);
}



static inline void readmem(int address, int dest, int offset, int size, int tmp)
{
	int f=tmp;

	mov_l_rr(f,address);
	shrl_l_ri(f,16);   /* The index into the mem bank table */
	mov_l_rm_indexed(f,(uae_u32)mem_banks,f);
	/* Now f holds a pointer to the actual membank */
	mov_l_rR(f,f,offset);
	/* Now f holds the address of the b/w/lget function */
	call_r_11(dest,f,address,size,4);
	forget_about(tmp);
}

void readbyte(int address, int dest, int tmp)
{
	int distrust = currprefs.comptrustbyte;
	if ((special_mem&S_READ) || distrust)
		readmem_special(address,dest,8,1,tmp);
	else
		readmem_real(address,dest,8,1,tmp);
}

void readword(int address, int dest, int tmp)
{
	int distrust = currprefs.comptrustword;
	if ((special_mem&S_READ) || distrust)
		readmem_special(address,dest,4,2,tmp);
	else
		readmem_real(address,dest,4,2,tmp);
}

void readlong(int address, int dest, int tmp)
{
	int distrust = currprefs.comptrustlong;
	if ((special_mem&S_READ) || distrust)
		readmem_special(address,dest,0,4,tmp);
	else
		readmem_real(address,dest,0,4,tmp);
}



/* This one might appear a bit odd... */
static inline void get_n_addr_old(int address, int dest, int tmp)
{
	readmem(address,dest,24,4,tmp);
}

static inline void get_n_addr_real(int address, int dest, int tmp)
{
	int f=tmp;
	if (address!=dest)
		f=dest;

#ifdef NATMEM_OFFSET
	if (canbang) {
		lea_l_brr(dest,address,MEMBaseDiff);
		forget_about(tmp);
		return;
	}
#endif
	mov_l_rr(f,address);
	mov_l_rr(dest,address); // gb-- nop if dest==address
	shrl_l_ri(f,16);
	mov_l_rm_indexed(f,(uae_u32)baseaddr,f);
	add_l(dest,f);
	forget_about(tmp);
}

void get_n_addr(int address, int dest, int tmp)
{
	int distrust = currprefs.comptrustnaddr;
	if (special_mem || distrust)
		get_n_addr_old(address,dest,tmp);
	else
		get_n_addr_real(address,dest,tmp);
}

void get_n_addr_jmp(int address, int dest, int tmp)
{
#if 0
	/* For this, we need to get the same address as the rest of UAE
	 would --- otherwise we end up translating everything twice */
	get_n_addr(address,dest,tmp);
#else
	int f=tmp;
	if (address!=dest)
		f=dest;
	mov_l_rr(f,address);
	shrl_l_ri(f,16);   /* The index into the baseaddr bank table */
	mov_l_rm_indexed(dest,(uae_u32)baseaddr,f);
	add_l(dest,address);
	and_l_ri (dest, ~1);
	forget_about(tmp);
#endif
}


/* base is a register, but dp is an actual value. 
   target is a register, as is tmp */
void calc_disp_ea_020(int base, uae_u32 dp, int target, int tmp)
{
	int reg = (dp >> 12) & 15;
	int regd_shift=(dp >> 9) & 3;

	if (dp & 0x100) {
		int ignorebase=(dp&0x80);
		int ignorereg=(dp&0x40);
		int addbase=0;
		int outer=0;

		if ((dp & 0x30) == 0x20) addbase = (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2);
		if ((dp & 0x30) == 0x30) addbase = comp_get_ilong((m68k_pc_offset+=4)-4);

		if ((dp & 0x3) == 0x2) outer = (uae_s32)(uae_s16)comp_get_iword((m68k_pc_offset+=2)-2);
		if ((dp & 0x3) == 0x3) outer = comp_get_ilong((m68k_pc_offset+=4)-4);

		if ((dp & 0x4) == 0) {  /* add regd *before* the get_long */
			if (!ignorereg) {
				if ((dp & 0x800) == 0)
					sign_extend_16_rr(target,reg);
				else
					mov_l_rr(target,reg);
				shll_l_ri(target,regd_shift);
			}
			else
				mov_l_ri(target,0);

			/* target is now regd */
			if (!ignorebase)
				add_l(target,base);
			add_l_ri(target,addbase);
			if (dp&0x03) readlong(target,target,tmp);
		} else { /* do the getlong first, then add regd */
			if (!ignorebase) {
				mov_l_rr(target,base);
				add_l_ri(target,addbase);
			}
			else
				mov_l_ri(target,addbase);
			if (dp&0x03) readlong(target,target,tmp);

			if (!ignorereg) {
				if ((dp & 0x800) == 0)
					sign_extend_16_rr(tmp,reg);
				else
					mov_l_rr(tmp,reg);
				shll_l_ri(tmp,regd_shift);
				/* tmp is now regd */
				add_l(target,tmp);
			}
		}
		add_l_ri(target,outer);
	}
	else { /* 68000 version */
		if ((dp & 0x800) == 0) { /* Sign extend */
			sign_extend_16_rr(target,reg);
			lea_l_brr_indexed(target,base,target,regd_shift,(uae_s32)((uae_s8)dp));
		}
		else {
			lea_l_brr_indexed(target,base,reg,regd_shift,(uae_s32)((uae_s8)dp));
		}
	}
	forget_about(tmp);
}





void set_cache_state(int enabled)
{
	if (enabled!=letit)
		flush_icache_hard(0, 3);
	letit=enabled;
}

int get_cache_state(void)
{
	return letit;
}

uae_u32 get_jitted_size(void)
{
	if (compiled_code)
		return current_compile_p-compiled_code;
	return 0;
}

void alloc_cache(void)
{
	if (compiled_code) {
		flush_icache_hard(0, 3);
		cache_free(compiled_code);
	}
	if (veccode == NULL)
		veccode = cache_alloc (256);
	if (popallspace == NULL)
		popallspace = cache_alloc (1024);
	compiled_code = NULL;
	if (currprefs.cachesize == 0)
		return;

	while (!compiled_code && currprefs.cachesize) {
		compiled_code=cache_alloc(currprefs.cachesize*1024);
		if (!compiled_code)
			currprefs.cachesize/=2;
	}
	if (compiled_code) {
		max_compile_start = compiled_code + currprefs.cachesize*1024 - BYTES_PER_INST;
		current_compile_p=compiled_code;
	}
}

static void calc_checksum(blockinfo* bi, uae_u32* c1, uae_u32* c2)
{
	uae_u32 k1=0;
	uae_u32 k2=0;
	uae_s32 len=bi->len;
	uintptr tmp = (uintptr)bi->min_pcp;
	uae_u32* pos;

	len+=(tmp&3);
	tmp &= ~((uintptr)3);
	pos=(uae_u32*)tmp;

	if (len >= 0 && len <= MAX_CHECKSUM_LEN) {
		while (len>0) {
			k1+=*pos;
			k2^=*pos;
			pos++;
			len-=4;
		}
		*c1 = k1;
		*c2 = k2;
	}
	else {
		*c1=0;
		*c2=0;
	}
}

#if 0
static void show_checksum(CSI_TYPE* csi)
{
	uae_u32 k1=0;
	uae_u32 k2=0;
	uae_s32 len=CSI_LENGTH(csi);
	uae_u32 tmp=(uintptr)CSI_START_P(csi);
	uae_u32* pos;

	len+=(tmp&3);
	tmp&=(~3);
	pos=(uae_u32*)tmp;

	if (len<0 || len>MAX_CHECKSUM_LEN) {
		return;
	}
	else {
		while (len>0) {
			jit_log("%08x ",*pos);
			pos++;
			len-=4;
		}
		jit_log(" bla");
	}
}
#endif


int check_for_cache_miss(void)
{
	blockinfo* bi=get_blockinfo_addr(regs.pc_p);

	if (bi) {
		int cl=cacheline(regs.pc_p);
		if (bi!=cache_tags[cl+1].bi) {
			raise_in_cl_list(bi);
			return 1;
		}
	}
	return 0;
}


static void recompile_block(void)
{
	/* An existing block's countdown code has expired. We need to make
	sure that execute_normal doesn't refuse to recompile due to a
	perceived cache miss... */
	blockinfo*  bi=get_blockinfo_addr(regs.pc_p);

	Dif (!bi)
		jit_abort(_T("recompile_block"));
	raise_in_cl_list(bi);
	execute_normal();
	return;
}
static void cache_miss(void)
{
	blockinfo*  bi=get_blockinfo_addr(regs.pc_p);
	uae_u32     cl=cacheline(regs.pc_p);
	blockinfo*  bi2=get_blockinfo(cl);

	if (!bi) {
		execute_normal(); /* Compile this block now */
		return;
	}
	Dif (!bi2 || bi==bi2) {
		jit_abort(_T("Unexplained cache miss %p %p"),bi,bi2);
	}
	raise_in_cl_list(bi);
	return;
}

static int called_check_checksum(blockinfo* bi);

static void check_checksum(void)
{
	blockinfo*  bi=get_blockinfo_addr(regs.pc_p);
	uae_u32     cl=cacheline(regs.pc_p);
	blockinfo*  bi2=get_blockinfo(cl);

	uae_u32     c1,c2;

	checksum_count++;
	/* These are not the droids you are looking for...  */
	if (!bi) {
		/* Whoever is the primary target is in a dormant state, but
		calling it was accidental, and we should just compile this
		new block */
		execute_normal();
		return;
	}
	if (bi!=bi2) {
		/* The block was hit accidentally, but it does exist. Cache miss */
		cache_miss();
		return;
	}

	if (bi->c1 || bi->c2)
		calc_checksum(bi,&c1,&c2);
	else {
		c1=c2=1;  /* Make sure it doesn't match */
	}
	if (c1==bi->c1 && c2==bi->c2) {
		/* This block is still OK. So we reactivate. Of course, that
		means we have to move it into the needs-to-be-flushed list */
		bi->handler_to_use=bi->handler;
		set_dhtu(bi,bi->direct_handler);

		/*	write_log (_T("JIT: reactivate %p/%p (%x %x/%x %x)\n"),bi,bi->pc_p,
		c1,c2,bi->c1,bi->c2);*/
		remove_from_list(bi);
		add_to_active(bi);
		raise_in_cl_list(bi);
	}
	else {
		/* This block actually changed. We need to invalidate it,
		and set it up to be recompiled */
		/* write_log (_T("JIT: discard %p/%p (%x %x/%x %x)\n"),bi,bi->pc_p,
		c1,c2,bi->c1,bi->c2); */
		invalidate_block(bi);
		raise_in_cl_list(bi);
		execute_normal();
	}
}


static inline void create_popalls(void)
{
	int i,r;

#ifdef UAE
#else
  if ((popallspace = alloc_code(POPALLSPACE_SIZE)) == NULL) {
	  write_log("FATAL: Could not allocate popallspace!\n");
	  abort();
  }
  vm_protect(popallspace, POPALLSPACE_SIZE, VM_PAGE_READ | VM_PAGE_WRITE);

  int stack_space = STACK_OFFSET;
  for (i=0;i<N_REGS;i++) {
	  if (need_to_preserve[i])
		  stack_space += sizeof(void *);
  }
  stack_space %= STACK_ALIGN;
  if (stack_space)
	  stack_space = STACK_ALIGN - stack_space;
#endif

	current_compile_p=popallspace;
	set_target(current_compile_p);
#if USE_PUSH_POP
	/* If we can't use gcc inline assembly, we need to pop some
	registers before jumping back to the various get-out routines.
	This generates the code for it.
	*/
	popall_do_nothing=current_compile_p;
	for (i=0;i<N_REGS;i++) {
		if (need_to_preserve[i])
			raw_pop_l_r(i);
	}
	raw_jmp((uintptr)do_nothing);
	align_target(32);

	popall_execute_normal=get_target();
	for (i=0;i<N_REGS;i++) {
		if (need_to_preserve[i])
			raw_pop_l_r(i);
	}
	raw_jmp((uintptr)execute_normal);
	align_target(32);

	popall_cache_miss=get_target();
	for (i=0;i<N_REGS;i++) {
		if (need_to_preserve[i])
			raw_pop_l_r(i);
	}
	raw_jmp((uintptr)cache_miss);
	align_target(32);

	popall_recompile_block=get_target();
	for (i=0;i<N_REGS;i++) {
		if (need_to_preserve[i])
			raw_pop_l_r(i);
	}
	raw_jmp((uintptr)recompile_block);
	align_target(32);

	popall_exec_nostats=get_target();
	for (i=0;i<N_REGS;i++) {
		if (need_to_preserve[i])
			raw_pop_l_r(i);
	}
	raw_jmp((uintptr)exec_nostats);
	align_target(32);

	popall_check_checksum=get_target();
	for (i=0;i<N_REGS;i++) {
		if (need_to_preserve[i])
			raw_pop_l_r(i);
	}
	raw_jmp((uintptr)check_checksum);
	align_target(32);

	current_compile_p=get_target();
#else
	popall_exec_nostats=exec_nostats;
	popall_execute_normal=execute_normal;
	popall_cache_miss=cache_miss;
	popall_recompile_block=recompile_block;
	popall_do_nothing=do_nothing;
	popall_check_checksum=check_checksum;
#endif

	/* And now, the code to do the matching pushes and then jump
	into a handler routine */
	pushall_call_handler=get_target();
#if USE_PUSH_POP
	for (i=N_REGS;i--;) {
		if (need_to_preserve[i])
			raw_push_l_r(i);
	}
#endif
	r=REG_PC_TMP;
	raw_mov_l_rm(r,(uae_u32)&regs.pc_p);
	raw_and_l_ri(r,TAGMASK);
	raw_jmp_m_indexed((uae_u32)cache_tags,r,4);
}

static inline void reset_lists(void)
{
	int i;

	for (i=0;i<MAX_HOLD_BI;i++)
		hold_bi[i]=NULL;
	active=NULL;
	dormant=NULL;
}

static void prepare_block(blockinfo* bi)
{
	int i;

	set_target(current_compile_p);
	align_target(32);
	bi->direct_pen=(cpuop_func*)get_target();
	raw_mov_l_rm(0,(uintptr)&(bi->pc_p));
	raw_mov_l_mr((uintptr)&regs.pc_p,0);
	raw_jmp((uintptr)popall_execute_normal);

	align_target(32);
	bi->direct_pcc=(cpuop_func*)get_target();
	raw_mov_l_rm(0,(uintptr)&(bi->pc_p));
	raw_mov_l_mr((uintptr)&regs.pc_p,0);
	raw_jmp((uintptr)popall_check_checksum);

	align_target(32);
	current_compile_p=get_target();

	bi->deplist=NULL;
	for (i=0;i<2;i++) {
		bi->dep[i].prev_p=NULL;
		bi->dep[i].next=NULL;
	}
	bi->env=default_ss;
	bi->status=BI_NEW;
	bi->havestate=0;
	//bi->env=empty_ss;
}

void compemu_reset(void)
{
	set_cache_state(0);
}

void build_comp(void)
{
	int i;
	int jumpcount=0;
	unsigned long opcode;
	const struct comptbl* tbl=op_smalltbl_0_comp_ff;
	const struct comptbl* nftbl=op_smalltbl_0_comp_nf;
	int count;
#ifdef NOFLAGS_SUPPORT
	struct comptbl *nfctbl = (currprefs.cpu_level >= 5 ? op_smalltbl_0_nf
		: currprefs.cpu_level == 4 ? op_smalltbl_1_nf
		: (currprefs.cpu_level == 2 || currprefs.cpu_level == 3) ? op_smalltbl_2_nf
		: currprefs.cpu_level == 1 ? op_smalltbl_3_nf
		: ! currprefs.cpu_compatible ? op_smalltbl_4_nf
		: op_smalltbl_5_nf);
#endif
	raw_init_cpu();
#ifdef NATMEM_OFFSET
	install_exception_handler();
#endif
	write_log (_T("JIT: Building Compiler function table\n"));
	for (opcode = 0; opcode < 65536; opcode++) {
#ifdef NOFLAGS_SUPPORT
		nfcpufunctbl[opcode] = op_illg;
#endif
		compfunctbl[opcode] = NULL;
		nfcompfunctbl[opcode] = NULL;
		prop[opcode].use_flags = 0x1f;
		prop[opcode].set_flags = 0x1f;
		prop[opcode].is_jump=1;
	}

	for (i = 0; tbl[i].opcode < 65536; i++) {
		int isjmp=(tbl[i].specific&1);
		int isaddx=(tbl[i].specific&8);
		int iscjmp=(tbl[i].specific&16);

		prop[tbl[i].opcode].is_jump=isjmp;
		prop[tbl[i].opcode].is_const_jump=iscjmp;
		prop[tbl[i].opcode].is_addx=isaddx;
		compfunctbl[tbl[i].opcode] = tbl[i].handler;
	}
	for (i = 0; nftbl[i].opcode < 65536; i++) {
		nfcompfunctbl[nftbl[i].opcode] = nftbl[i].handler;
#ifdef NOFLAGS_SUPPORT
		nfcpufunctbl[nftbl[i].opcode] = nfctbl[i].handler;
#endif
	}

#ifdef NOFLAGS_SUPPORT
	for (i = 0; nfctbl[i].handler; i++) {
		nfcpufunctbl[nfctbl[i].opcode] = nfctbl[i].handler;
	}
#endif

	for (opcode = 0; opcode < 65536; opcode++) {
		compop_func *f;
		compop_func *nff;
#ifdef NOFLAGS_SUPPORT
		compop_func *nfcf;
#endif
		int isjmp,isaddx,iscjmp;
		int lvl;

		lvl = (currprefs.cpu_model - 68000) / 10;
		if (lvl > 4)
			lvl--;
		if (table68k[opcode].mnemo == i_ILLG || table68k[opcode].clev > lvl)
			continue;

		if (table68k[opcode].handler != -1) {
			f = compfunctbl[table68k[opcode].handler];
			nff = nfcompfunctbl[table68k[opcode].handler];
#ifdef NOFLAGS_SUPPORT
			nfcf = nfcpufunctbl[table68k[opcode].handler];
#endif
			isjmp=prop[table68k[opcode].handler].is_jump;
			iscjmp=prop[table68k[opcode].handler].is_const_jump;
			isaddx=prop[table68k[opcode].handler].is_addx;
			prop[opcode].is_jump=isjmp;
			prop[opcode].is_const_jump=iscjmp;
			prop[opcode].is_addx=isaddx;
			compfunctbl[opcode] = f;
			nfcompfunctbl[opcode] = nff;
#ifdef NOFLAGS_SUPPORT
			Dif (nfcf == op_illg)
				abort();
			nfcpufunctbl[opcode] = nfcf;
#endif
		}
		prop[opcode].set_flags =table68k[opcode].flagdead;
		prop[opcode].use_flags =table68k[opcode].flaglive;
		/* Unconditional jumps don't evaluate condition codes, so they
		don't actually use any flags themselves */
		if (prop[opcode].is_const_jump)
			prop[opcode].use_flags=0;
	}
#ifdef NOFLAGS_SUPPORT
	for (i = 0; nfctbl[i].handler != NULL; i++) {
		if (nfctbl[i].specific)
			nfcpufunctbl[tbl[i].opcode] = nfctbl[i].handler;
	}
#endif

	count=0;
	for (opcode = 0; opcode < 65536; opcode++) {
		if (compfunctbl[opcode])
			count++;
	}
	jit_log("Supposedly %d compileable opcodes!",count);

	/* Initialise state */
	alloc_cache();
	create_popalls();
	reset_lists();

	for (i=0;i<TAGSIZE;i+=2) {
		cache_tags[i].handler=(cpuop_func*)popall_execute_normal;
		cache_tags[i+1].bi=NULL;
	}
	compemu_reset();

	for (i=0;i<N_REGS;i++) {
		empty_ss.nat[i].holds=-1;
		empty_ss.nat[i].validsize=0;
		empty_ss.nat[i].dirtysize=0;
	}
	default_ss=empty_ss;
#if 0
	default_ss.nat[6].holds=11;
	default_ss.nat[6].validsize=4;
	default_ss.nat[5].holds=12;
	default_ss.nat[5].validsize=4;
#endif
}


static void flush_icache_none(int)
{
	/* Nothing to do.  */
}

void flush_icache_hard(uaecptr ptr, int n)
{
	blockinfo* bi, *dbi;

	hard_flush_count++;
#if 0
	write_log (_T("JIT: Flush Icache_hard(%d/%x/%p), %u instruction bytes\n"),
		n,regs.pc,regs.pc_p,current_compile_p-compiled_code);
#endif
	bi=active;
	while(bi) {
		cache_tags[cacheline(bi->pc_p)].handler=(cpuop_func*)popall_execute_normal;
		cache_tags[cacheline(bi->pc_p)+1].bi=NULL;
		dbi=bi; bi=bi->next;
	}
	bi=dormant;
	while(bi) {
		cache_tags[cacheline(bi->pc_p)].handler=(cpuop_func*)popall_execute_normal;
		cache_tags[cacheline(bi->pc_p)+1].bi=NULL;
		dbi=bi; bi=bi->next;
	}

	reset_lists();
	if (!compiled_code)
		return;
	current_compile_p=compiled_code;
	set_special(0); /* To get out of compiled code */
}


/* "Soft flushing" --- instead of actually throwing everything away,
   we simply mark everything as "needs to be checked".
*/

void flush_icache(uaecptr ptr, int n)
{
	blockinfo* bi;
	blockinfo* bi2;

	if (currprefs.comp_hardflush) {
		flush_icache_hard(ptr, n);
		return;
	}
	soft_flush_count++;
	if (!active)
		return;

	bi=active;
	while (bi) {
		uae_u32 cl=cacheline(bi->pc_p);
		if (!bi->handler) {
			/* invalidated block */
			if (bi==cache_tags[cl+1].bi)
				cache_tags[cl].handler=(cpuop_func*)popall_execute_normal;
			bi->handler_to_use=(cpuop_func*)popall_execute_normal;
			set_dhtu(bi,bi->direct_pen);
		}
		else {
			if (bi==cache_tags[cl+1].bi)
				cache_tags[cl].handler=(cpuop_func*)popall_check_checksum;
			bi->handler_to_use=(cpuop_func*)popall_check_checksum;
			set_dhtu(bi,bi->direct_pcc);
		}
		bi2=bi;
		bi=bi->next;
	}
	/* bi2 is now the last entry in the active list */
	bi2->next=dormant;
	if (dormant)
		dormant->prev_p=&(bi2->next);

	dormant=active;
	active->prev_p=&dormant;
	active=NULL;
}

/*
static void catastrophe(void)
{
	jit_abort(_T("catastprophe"));
}
*/

int failure;

#ifdef JIT_DEBUG
static uae_u8 *last_regs_pc_p = 0;
static uae_u8 *last_compiled_block_addr = 0;

void compiler_dumpstate(void)
{
	if (!JITDebug)
		return;
	
	panicbug("### Host addresses");
	panicbug("MEM_BASE    : %x", MEMBaseDiff);
	panicbug("PC_P        : %p", &regs.pc_p);
	panicbug("SPCFLAGS    : %p", &regs.spcflags);
	panicbug("D0-D7       : %p-%p", &regs.regs[0], &regs.regs[7]);
	panicbug("A0-A7       : %p-%p", &regs.regs[8], &regs.regs[15]);
	panicbug("");
	
	panicbug("### M68k processor state");
	m68k_dumpstate(0);
	panicbug("");
	
	panicbug("### Block in Atari address space");
	panicbug("M68K block   : %p",
			  (void *)(uintptr)last_regs_pc_p);
	if (last_regs_pc_p != 0) {
		panicbug("Native block : %p (%d bytes)",
			  (void *)last_compiled_block_addr,
			  get_blockinfo_addr(last_regs_pc_p)->direct_handler_size);
	}
	panicbug("");
}
#endif

void compile_block(cpu_history* pc_hist, int blocklen, int totcycles)
{
	if (letit && compiled_code && currprefs.cpu_model>=68020) {
#if PROFILE_COMPILE_TIME
		compile_count++;
		clock_t start_time = clock();
#endif
#ifdef JIT_DEBUG
		bool disasm_block = false;
#endif

		/* OK, here we need to 'compile' a block */
		int i;
		int r;
		int was_comp=0;
		uae_u8 liveflags[MAXRUN+1];
#if USE_CHECKSUM_INFO
		bool trace_in_rom = isinrom((uintptr)pc_hist[0].location);
		uintptr max_pcp=(uintptr)pc_hist[blocklen - 1].location;
		uintptr min_pcp=max_pcp;
#else
		uintptr max_pcp=(uintptr)pc_hist[0].location;
		uintptr min_pcp=max_pcp;
#endif
		uae_u32 cl=cacheline(pc_hist[0].location);
		void* specflags=(void*)&regs.spcflags;
		blockinfo* bi=NULL;
		blockinfo* bi2;
		int extra_len=0;

		compile_count++;
		if (current_compile_p>=max_compile_start)
			flush_icache_hard(0, 3);

		alloc_blockinfos();

		bi=get_blockinfo_addr_new(pc_hist[0].location,0);
		bi2=get_blockinfo(cl);

		optlev=bi->optlevel;
		if (bi->handler) {
			Dif (bi!=bi2) {
				/* I don't think it can happen anymore. Shouldn't, in
				any case. So let's make sure... */
				jit_abort (_T("JIT: WOOOWOO count=%d, ol=%d %p %p\n"),
					bi->count,bi->optlevel,bi->handler_to_use,
					cache_tags[cl].handler);
			}

			Dif (bi->count!=-1 && bi->status!=BI_TARGETTED) {
				/* What the heck? We are not supposed to be here! */
				jit_abort(_T("BI_TARGETTED"));
			}
		}
		if (bi->count==-1) {
			optlev++;
			while (!currprefs.optcount[optlev])
				optlev++;
			bi->count=currprefs.optcount[optlev]-1;
		}
		current_block_pc_p=(uintptr)pc_hist[0].location;

		remove_deps(bi); /* We are about to create new code */
		bi->optlevel=optlev;
		bi->pc_p=(uae_u8*)pc_hist[0].location;
#if USE_CHECKSUM_INFO
		free_checksum_info_chain(bi->csi);
		bi->csi = NULL;
#endif

		liveflags[blocklen]=0x1f; /* All flags needed afterwards */
		i=blocklen;
		while (i--) {
			uae_u16* currpcp=pc_hist[i].location;
			int op=cft_map(*currpcp);

#if USE_CHECKSUM_INFO
			trace_in_rom = trace_in_rom && isinrom((uintptr)currpcp);
			if (follow_const_jumps && is_const_jump(op)) {
				checksum_info *csi = alloc_checksum_info();
				csi->start_p = (uae_u8 *)min_pcp;
				csi->length = max_pcp - min_pcp + LONGEST_68K_INST;
				csi->next = bi->csi;
				bi->csi = csi;
				max_pcp = (uintptr)currpcp;
			}
			min_pcp = (uintptr)currpcp;
#else
			if ((uintptr)currpcp<min_pcp)
				min_pcp=(uintptr)currpcp;
			if ((uintptr)currpcp>max_pcp)
				max_pcp=(uintptr)currpcp;
#endif

			if (currprefs.compnf) {
				liveflags[i]=((liveflags[i+1]&
					(~prop[op].set_flags))|
					prop[op].use_flags);
				if (prop[op].is_addx && (liveflags[i+1]&FLAG_Z)==0)
					liveflags[i]&= ~FLAG_Z;
			}
			else {
				liveflags[i]=0x1f;
			}
		}

#if USE_CHECKSUM_INFO
		checksum_info *csi = alloc_checksum_info();
		csi->start_p = (uae_u8 *)min_pcp;
		csi->length = max_pcp - min_pcp + LONGEST_68K_INST;
		csi->next = bi->csi;
		bi->csi = csi;
#endif

		bi->needed_flags=liveflags[0];

		/* This is the non-direct handler */
		align_target(32);
		set_target(get_target()+1);
		align_target(16);
		/* Now aligned at n*32+16 */

		bi->handler=
			bi->handler_to_use=(cpuop_func*)get_target();
		raw_cmp_l_mi((uae_u32)&regs.pc_p,(uae_u32)pc_hist[0].location);
		raw_jnz((uae_u32)popall_cache_miss);
		/* This was 16 bytes on the x86, so now aligned on (n+1)*32 */

		was_comp=0;

#if USE_MATCHSTATE
		comp_pc_p=(uae_u8*)pc_hist[0].location;
		init_comp();
		match_states(&(bi->env));
		was_comp=1;
#endif

		bi->direct_handler=(cpuop_func*)get_target();
		set_dhtu(bi,bi->direct_handler);
		current_block_start_target=(uintptr)get_target();

		if (bi->count>=0) { /* Need to generate countdown code */
			raw_mov_l_mi((uintptr)&regs.pc_p,(uintptr)pc_hist[0].location);
			raw_sub_l_mi((uintptr)&(bi->count),1);
			raw_jl((uintptr)popall_recompile_block);
		}
		if (optlev==0) { /* No need to actually translate */
			/* Execute normally without keeping stats */
			raw_mov_l_mi((uintptr)&regs.pc_p,(uintptr)pc_hist[0].location);
			raw_jmp((uintptr)popall_exec_nostats);
		}
		else {
			reg_alloc_run=0;
			next_pc_p=0;
			taken_pc_p=0;
			branch_cc=0; // Only to be initialized. Will be set together with next_pc_p

			log_startblock();
			for (i=0;i<blocklen &&
				get_target_noopt()<max_compile_start;i++) {
					cpuop_func **cputbl;
					compop_func **comptbl;
					uae_u16 opcode;

					opcode=cft_map((uae_u16)*pc_hist[i].location);
					special_mem=pc_hist[i].specmem;
					needed_flags=(liveflags[i+1] & prop[opcode].set_flags);
					if (!needed_flags && currprefs.compnf) {
#ifdef NOFLAGS_SUPPORT
						cputbl=nfcpufunctbl;
#else
						cputbl=cpufunctbl;
#endif
						comptbl=nfcompfunctbl;
					}
					else {
						cputbl=cpufunctbl;
						comptbl=compfunctbl;
					}

#ifdef FLIGHT_RECORDER
					{
						mov_l_ri(S1, ((uintptr)(pc_hist[i].location)) | 1);
						/* store also opcode to second register */
						clobber_flags();
						remove_all_offsets();
						int arg = readreg_specific(S1,4,REG_PAR1);
						prepare_for_call_1();
						unlock2(arg);
						prepare_for_call_2();
						raw_call((uintptr)m68k_record_step);
					}
#endif

					failure = 1; // gb-- defaults to failure state
					if (comptbl[opcode] && optlev>1) {
						failure=0;
						if (!was_comp) {
							comp_pc_p=(uae_u8*)pc_hist[i].location;
							init_comp();
						}
						was_comp++;

						comptbl[opcode](opcode);
						freescratch();
						if (!(liveflags[i+1] & FLAG_CZNV)) {
							/* We can forget about flags */
							dont_care_flags();
						}
#if INDIVIDUAL_INST
						flush(1);
						nop();
						flush(1);
						was_comp=0;
#endif
					}

					if (failure) {
						if (was_comp) {
							flush(1);
							was_comp=0;
						}
						raw_mov_l_ri(REG_PAR1,(uae_u32)opcode);
						raw_mov_l_ri(REG_PAR2,(uae_u32)&regs);
#if USE_NORMAL_CALLING_CONVENTION
						raw_push_l_r(REG_PAR2);
						raw_push_l_r(REG_PAR1);
#endif
						raw_mov_l_mi((uintptr)&regs.pc_p,
							(uintptr)pc_hist[i].location);
						raw_call((uintptr)cputbl[opcode]);
						//raw_add_l_mi((uintptr)&oink,1); // FIXME
#if USE_NORMAL_CALLING_CONVENTION
						raw_inc_sp(8);
#endif

						if (i<blocklen-1) {
							uae_s8* branchadd;

							raw_mov_l_rm(0,(uintptr)specflags);
							raw_test_l_rr(0,0);
							raw_jz_b_oponly();
							branchadd=(uae_s8*)get_target();
							emit_byte(0);
							raw_sub_l_mi((uae_u32)&countdown,scaled_cycles(totcycles));
							raw_jmp((uintptr)popall_do_nothing);
							*branchadd=(uintptr)get_target()-(uintptr)branchadd-1;
						}
					}
			}
#if 0 /* This isn't completely kosher yet; It really needs to be
			be integrated into a general inter-block-dependency scheme */
			if (next_pc_p && taken_pc_p &&
				was_comp && taken_pc_p==current_block_pc_p) {
					blockinfo* bi1=get_blockinfo_addr_new((void*)next_pc_p,0);
					blockinfo* bi2=get_blockinfo_addr_new((void*)taken_pc_p,0);
					uae_u8 x=bi1->needed_flags;

					if (x==0xff || 1) {  /* To be on the safe side */
						uae_u16* next=(uae_u16*)next_pc_p;
						uae_u16 op=cft_map(*next);

						x=0x1f;
						x&=(~prop[op].set_flags);
						x|=prop[op].use_flags;
					}

					x|=bi2->needed_flags;
					if (!(x & FLAG_CZNV)) {
						/* We can forget about flags */
						dont_care_flags();
						extra_len+=2; /* The next instruction now is part of this
									  block */
					}

			}
#endif

			if (next_pc_p) { /* A branch was registered */
				uintptr t1=next_pc_p;
				uintptr t2=taken_pc_p;
				int     cc=branch_cc;

				uae_u32* branchadd;
				uae_u32* tba;
				bigstate tmp;
				blockinfo* tbi;

				if (taken_pc_p<next_pc_p) {
					/* backward branch. Optimize for the "taken" case ---
					which means the raw_jcc should fall through when
					the 68k branch is taken. */
					t1=taken_pc_p;
					t2=next_pc_p;
					cc=branch_cc^1;
				}

#if !USE_MATCHSTATE
				flush_keepflags();
#endif
				tmp=live; /* ouch! This is big... */
				raw_jcc_l_oponly(cc);
				branchadd=(uae_u32*)get_target();
				emit_long(0);
		
				/* predicted outcome */
				tbi=get_blockinfo_addr_new((void*)t1,1);
				match_states(&(tbi->env));
				raw_sub_l_mi((uae_u32)&countdown,scaled_cycles(totcycles));
				raw_jcc_l_oponly(9);
				tba=(uae_u32*)get_target();
				emit_long(get_handler(t1)-((uae_u32)tba+4));
				raw_mov_l_mi((uintptr)&regs.pc_p,t1);
				raw_jmp((uintptr)popall_do_nothing);
				create_jmpdep(bi,0,tba,t1);

				align_target(16);
				/* not-predicted outcome */
				*branchadd=(uae_u32)get_target()-((uae_u32)branchadd+4);
				live=tmp; /* Ouch again */
				tbi=get_blockinfo_addr_new((void*)t2,1);
				match_states(&(tbi->env));

				//flush(1); /* Can only get here if was_comp==1 */
				raw_sub_l_mi((uae_u32)&countdown,scaled_cycles(totcycles));
				raw_jcc_l_oponly(9);
				tba=(uae_u32*)get_target();
				emit_long(get_handler(t2)-((uae_u32)tba+4));
				raw_mov_l_mi((uintptr)&regs.pc_p,t2);
				raw_jmp((uintptr)popall_do_nothing);
				create_jmpdep(bi,1,tba,t2);
			}
			else
			{
				if (was_comp) {
					flush(1);
				}

				/* Let's find out where next_handler is... */
				if (was_comp && isinreg(PC_P)) {
					r=live.state[PC_P].realreg;
					raw_and_l_ri(r,TAGMASK);
					int r2 = (r==0) ? 1 : 0;
					raw_mov_l_ri(r2,(uintptr)popall_do_nothing);
					raw_sub_l_mi((uae_u32)&countdown,scaled_cycles(totcycles));
					raw_cmov_l_rm_indexed(r2,(uae_u32)cache_tags,r,9);
					raw_jmp_r(r2);
				}
				else if (was_comp && isconst(PC_P)) {
					uae_u32 v=live.state[PC_P].val;
					uae_u32* tba;
					blockinfo* tbi;

					tbi=get_blockinfo_addr_new((void*)v,1);
					match_states(&(tbi->env));

					raw_sub_l_mi((uae_u32)&countdown,scaled_cycles(totcycles));
					raw_jcc_l_oponly(9);
					tba=(uae_u32*)get_target();
					emit_long(get_handler(v)-((uae_u32)tba+4));
					raw_mov_l_mi((uintptr)&regs.pc_p,v);
					raw_jmp((uintptr)popall_do_nothing);
					create_jmpdep(bi,0,tba,v);
				}
				else {
					r=REG_PC_TMP;
					raw_mov_l_rm(r,(uintptr)&regs.pc_p);
					raw_and_l_ri(r,TAGMASK);
					int r2 = (r==0) ? 1 : 0;
					raw_mov_l_ri(r2,(uintptr)popall_do_nothing);
					raw_sub_l_mi((uae_u32)&countdown,scaled_cycles(totcycles));
					raw_cmov_l_rm_indexed(r2,(uintptr)cache_tags,r,9);
					raw_jmp_r(r2);
				}
			}
		}

#if USE_MATCH	
	if (callers_need_recompile(&live,&(bi->env))) {
	    mark_callers_recompile(bi);
	}

	big_to_small_state(&live,&(bi->env));
#endif

#if USE_CHECKSUM_INFO
		remove_from_list(bi);
		if (trace_in_rom) {
			// No need to checksum that block trace on cache invalidation
			free_checksum_info_chain(bi->csi);
			bi->csi = NULL;
			add_to_dormant(bi);
		}
		else {
			calc_checksum(bi,&(bi->c1),&(bi->c2));
			add_to_active(bi);
		}
#else
		if (next_pc_p+extra_len>=max_pcp &&
			next_pc_p+extra_len<max_pcp+LONGEST_68K_INST)
			max_pcp=next_pc_p+extra_len;  /* extra_len covers flags magic */
		else
			max_pcp+=LONGEST_68K_INST;

		bi->len=max_pcp-min_pcp;
		bi->min_pcp=min_pcp;

		remove_from_list(bi);
		if (isinrom(min_pcp) && isinrom(max_pcp)) {
			add_to_dormant(bi); /* No need to checksum it on cache flush.
								Please don't start changing ROMs in
								flight! */
		}
		else {
			calc_checksum(bi,&(bi->c1),&(bi->c2));
			add_to_active(bi);
		}
#endif

#ifdef JIT_DEBUG
		if (JITDebug)
			bi->direct_handler_size = get_target() - (uae_u8 *)current_block_start_target;

		if (JITDebug && disasm_block) {
			uaecptr block_addr = start_pc + ((char *)pc_hist[0].location - (char *)start_pc_p);
			D(panicbug("M68K block @ 0x%08x (%d insns)\n", block_addr, blocklen));
			uae_u32 block_size = ((uae_u8 *)pc_hist[blocklen - 1].location - (uae_u8 *)pc_hist[0].location) + 1;
			disasm_m68k_block((uae_u8 *)pc_hist[0].location, block_size);
			D(panicbug("Compiled block @ 0x%08x\n", pc_hist[0].location));
			disasm_native_block((uae_u8 *)current_block_start_target, bi->direct_handler_size);
			getchar();
		}
#endif

		log_dump();
		align_target(32);
		current_compile_p=get_target();

		raise_in_cl_list(bi);
		bi->nexthandler=current_compile_p;

		/* We will flush soon, anyway, so let's do it now */
		if (current_compile_p>=max_compile_start)
			flush_icache_hard(0, 3);

		do_extra_cycles(totcycles); /* for the compilation time */
	}
}

#ifdef UAE
    /* Slightly different function defined in newcpu.cpp */
#else
void do_nothing(void)
{
    /* What did you expect this to do? */
}
#endif

#ifdef UAE
    /* Different implementation in newcpu.cpp */
#else
void exec_nostats(void)
{
	for (;;)  { 
		uae_u32 opcode = GET_OPCODE;
#ifdef FLIGHT_RECORDER
		m68k_record_step(m68k_getpc(), opcode);
#endif
		(*cpufunctbl[opcode])(opcode);
		cpu_check_ticks();
		if (end_block(opcode) || SPCFLAGS_TEST(SPCFLAG_ALL)) {
			return; /* We will deal with the spcflags in the caller */
		}
	}
}
#endif

#ifdef UAE
    /* Different implementation in newcpu.cpp */
#else
void execute_normal(void)
{
	if (!check_for_cache_miss()) {
		cpu_history pc_hist[MAXRUN];
		int blocklen = 0;
#if 0 && FIXED_ADDRESSING
		start_pc_p = regs.pc_p;
		start_pc = get_virtual_address(regs.pc_p);
#else
		start_pc_p = regs.pc_oldp;  
		start_pc = regs.pc; 
#endif
		for (;;)  { /* Take note: This is the do-it-normal loop */
			pc_hist[blocklen++].location = (uae_u16 *)regs.pc_p;
			uae_u32 opcode = GET_OPCODE;
#ifdef FLIGHT_RECORDER
			m68k_record_step(m68k_getpc(), opcode);
#endif
			(*cpufunctbl[opcode])(opcode);
			cpu_check_ticks();
			if (end_block(opcode) || SPCFLAGS_TEST(SPCFLAG_ALL) || blocklen>=MAXRUN) {
				compile_block(pc_hist, blocklen);
				return; /* We will deal with the spcflags in the caller */
			}
			/* No need to check regs.spcflags, because if they were set,
			we'd have ended up inside that "if" */
		}
	}
}
#endif

typedef void (*compiled_handler)(void);

#ifdef UAE
#else
void m68k_do_compile_execute(void)
{
	for (;;) {
		((compiled_handler)(pushall_call_handler))();
		/* Whenever we return from that, we should check spcflags */
		if (SPCFLAGS_TEST(SPCFLAG_ALL)) {
			if (m68k_do_specialties ())
				return;
		}
	}
}
#endif

#ifdef UAE
#else
void m68k_compile_execute (void)
{
setjmpagain:
    TRY(prb) {
	for (;;) {
	    if (quit_program > 0) {
		if (quit_program == 1) {
#ifdef FLIGHT_RECORDER
		    dump_log();
#endif
		    break;
		}
		quit_program = 0;
		m68k_reset ();
	    }
	    m68k_do_compile_execute();
	}
    }
    CATCH(prb) {
	flush_icache(0);
        Exception(prb, 0);
    	goto setjmpagain;
    }
}
#endif

#endif /* JIT */
