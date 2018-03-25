
/*
 *
 * Combined Enforcer, MungWall and SegTracker emulation.
 *
 */

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "newcpu.h"
#include "debug.h"
#include "debugmem.h"
#include "filesys.h"
#include "zfile.h"
#include "uae.h"
#include "fsdb.h"

#define N_GSYM 0x20
#define N_FUN 0x24
#define N_STSYM 0x26
#define N_LCSYM 0x28
#define N_MAIN 0x2a
#define N_ROSYM 0x2c
#define N_RSYM 0x40
#define N_SLINE 0x44
#define N_DSLINE 0x46
#define N_SSYM 0x60
#define N_SO 0x64
#define N_LSYM 0x80
#define N_SOL 0x84
#define N_PSYM 0xa0
#define N_EINCL 0xa2
#define N_LBRAC 0xc0
#define N_RBRAC 0xe0

#define MAX_SOURCELINELEN 100

static mem_get_func debugmem_func_lgeti;
static mem_get_func debugmem_func_wgeti;
static mem_get_func debugmem_func_lget;
static mem_get_func debugmem_func_wget;
static mem_get_func debugmem_func_bget;
static mem_put_func debugmem_func_lput;
static mem_put_func debugmem_func_wput;
static mem_put_func debugmem_func_bput;
static xlate_func debugmem_func_xlate;
static bool debugmem_initialized;
static bool debug_waiting;
static uaecptr debug_task;
static uae_u8 *exec_thistask;
static int executable_last_segment;
static bool libraries_loaded, fds_loaded;
static bool debugmem_mapped;

static int debugstack_word_state;
static uaecptr debugstack_word_addr;
static uaecptr debugstack_word_val;
static bool break_stack_pop, break_stack_push, break_stack_s;
static bool debugmem_active;
static int stackframemode;

uae_u32 debugmem_chiplimit;
bool debugmem_trace;

#define MAX_DEBUGMEMALLOCS 10000
#define MAX_DEBUGSEGS 1000
#define MAX_DEBUGSYMS 10000
#define MAX_STACKVARS 10000
#define MAX_DEBUGCODEFILES 1000
#define MAX_STACKFRAMES 100


struct debugstackframe
{
	uaecptr current_pc;
	uaecptr branch_pc;
	uaecptr next_pc;
	uaecptr stack;
	uae_u32 regs[16];
	uae_u16 sr;
};
static struct debugstackframe *stackframes, *stackframessuper;
static int stackframecnt, stackframecntsuper;

static uae_u32 debugmemptr;

struct stab
{
	TCHAR *string;
	uae_u8 type;
	uae_u8 other;
	uae_u16 desc;
	uae_u32 val;
};
static struct stab *stabs;
static int stabscount;

struct stabtype
{
	TCHAR *name;
	uae_u32 id;
};

struct debugcodefile
{
	const TCHAR *name;
	int length;
	uae_u8 *data;
	int lines;
	uae_u8 **lineptr;
	struct stabtype *stabtypes;
	int stabtypecount;
};
static struct debugcodefile **codefiles;
static int codefilecnt;

struct linemapping
{
	struct debugcodefile *file;
	int line;
};
static struct linemapping *linemap;
static int linemapsize;

#define SYMBOL_LOCAL 0
#define SYMBOL_GLOBAL 1

#define SYMBOLTYPE_FUNC 1

struct debugsymbol
{
	TCHAR *name;
	uae_u32 segment;
	uae_u32 allocid;
	uae_u32 value;
	uae_u32 flags;
	uae_u32 type;
	struct debugmemallocs *section;
	void *data;
};
static struct debugsymbol **symbols;
static int symbolcnt;

struct libname
{
	TCHAR *name;
	uae_char *aname;
	uae_u32 id;
	uae_u32 base;
};
static struct libname *libnames;
static int libnamecnt;
struct libsymbol
{
	struct libname *lib;
	TCHAR *name;
	uae_u32 value;
};
static struct libsymbol *libsymbols;
static int libsymbolcnt;

struct stackvar
{
	TCHAR *name;
	uae_u32 offset;
	struct debugsymbol *func;
};
static struct stackvar **stackvars;
static int stackvarcnt;

#define DEBUGALLOC_HUNK 1
#define DEBUGALLOC_ALLOCMEM 2
#define DEBUGALLOC_ALLOCVEC 3
#define DEBUGALLOC_SEG 4

struct debugmemallocs
{
	uae_u16 type;
	uae_u32 id;
	uae_u16 internalid;
	uae_u32 parentid;
	uae_u32 idtype;
	uaecptr start;
	uae_u32 size;
	uae_u32 start_page;
	uae_u32 pages;
	uaecptr pc;
	uae_u32 data;
	uae_u32 relative_start;
};

static struct debugmemallocs **allocs;
static int alloccnt;

#define HUNK_GAP 32768
#define PAGE_SIZE 256
#define PAGE_SIZE_MASK (PAGE_SIZE - 1)

#define DEBUGMEM_READ 0x01
#define DEBUGMEM_WRITE 0x02
#define DEBUGMEM_FETCH 0x04
#define DEBUGMEM_INITIALIZED 0x08
#define DEBUGMEM_STARTBLOCK 0x10
#define DEBUGMEM_ALLOCATED 0x40
#define DEBUGMEM_INUSE 0x80
#define DEBUGMEM_PARTIAL 0x100
#define DEBUGMEM_NOSTACKCHECK 0x200
#define DEBUGMEM_STACK 0x1000

struct debugmemdata
{
	uae_u16 id;
	uae_u16 flags;
	uae_u8 unused_start;
	uae_u8 unused_end;
	uae_u8 state[PAGE_SIZE];
};

static struct debugmemdata **dmd;
static int totalmemdata;

struct debugsegtracker
{
	TCHAR *name;
	uae_u32 allocid;
	uaecptr resident;
};
static struct debugsegtracker **dsegt;
static int segtrackermax;
static uae_u32 inhibit_break, last_break;

bool debugmem_break(int type)
{
	if (inhibit_break & (1 << type))
		return false;
	last_break = type;
	activate_debugger_new();
	return true;
}

bool debugmem_inhibit_break(int mode)
{
	if (mode < 0) {
		inhibit_break = 0;
		return true;
	} else if (mode > 0) {
		inhibit_break = 0xffffffff;
		return true;
	} else {
		inhibit_break ^= 1 << last_break;
	}
	return (inhibit_break & (1 << last_break)) != 0;
}

static void debugreportsegment(struct debugsegtracker *seg, bool verbose)
{
	struct debugmemallocs *alloc = allocs[seg->allocid];
	int parentid = alloc->id;
	for (int i = 0; i < MAX_DEBUGMEMALLOCS; i++) {
		struct debugmemallocs *a = allocs[i];
		if (a->parentid == parentid) {
			if (verbose) {
				console_out_f(_T("Segment %d (%s): %08x %08x - %08x (%d)\n"),
					a->internalid, seg->name, a->idtype, a->start, a->start + a->size - 1, a->size);
			} else {
				console_out_f(_T("Segment %d: %08x %08x - %08x (%d)\n"),
					a->internalid, a->idtype, a->start, a->start + a->size - 1, a->size);
			}
		}
	}
}

static void debugreportalloc(struct debugmemallocs *a)
{
	uae_u32 off = debugmem_bank.start;
	if (a->type == DEBUGALLOC_HUNK) {
		console_out_f(_T("Segment %d: %08x %08x - %08x (%d)\n"),
			a->id, a->idtype, a->start + off, a->start + off + a->size - 1, a->size);
	} else if (a->type == DEBUGALLOC_ALLOCMEM) {
		console_out_f(_T("AllocMem ID=%d: %08x %08x - %08x (%d) AllocFlags: %08x PC: %08x\n"),
			a->id, a->idtype, a->start + off, a->start + off + a->size - 1, a->size, a->data, a->pc);
	} else if (a->type == DEBUGALLOC_ALLOCVEC) {
		console_out_f(_T("AllocVec allocation ID=%d: %08x %08x - %08x (%d) AllocFlags: %08x PC: %08x\n"),
			a->id, a->idtype, a->start + off, a->start + off + a->size - 1, a->size, a->data, a->pc);
	} else if (a->type == DEBUGALLOC_SEG) {
		static int lastsegment;
		struct debugsegtracker *sg = NULL;
		const TCHAR *name = _T("<unknown>");
		for (int i = 0; i < MAX_DEBUGSEGS; i++) {
			sg = dsegt[(i + lastsegment) % MAX_DEBUGSEGS];
			if (sg->allocid == a->parentid) {
				name = sg->name;
				lastsegment = i;
				break;
			}
		}
		console_out_f(_T("Segment %d (%s): %08x %08x - %08x (%d)\n"),
			a->internalid, name, a->idtype, a->start, a->start + a->size - 1, a->size);
	}
}

static void debugreport(struct debugmemdata *dm, uaecptr addr, int rwi, int size, const TCHAR *msg)
{
	int offset = addr & PAGE_SIZE_MASK;
	uaecptr addr_start = addr & ~PAGE_SIZE_MASK;
	uae_u8 state = dm->state[offset];

	console_out_f(_T("Invalid access. Addr=%08x RW=%c Size=%d: %s\n"), addr, (rwi & DEBUGMEM_WRITE) ? 'W' : 'R', size, msg);
	console_out_f(_T("Page: %08x - %08x. State=%c Modified=%c, Start=%02X, End=%02X\n"),
		addr_start, addr_start + PAGE_SIZE - 1,
		!(state & (DEBUGMEM_ALLOCATED | DEBUGMEM_INUSE)) ? 'I' : (state & DEBUGMEM_WRITE) ? 'W' : 'R',
		(state & DEBUGMEM_WRITE) ? '*' : (state & DEBUGMEM_INITIALIZED) ? '+' : '-',
		dm->unused_start, PAGE_SIZE - dm->unused_end);
	debugmem_break(1);
}

#if 0
static uae_u32 debug_get_disp_ea(uaecptr pc, uae_u32 base)
{
	uae_u16 dp = get_word_debug(pc);
	pc += 2;
	int reg = (dp >> 12) & 15;
	uae_s32 regd = regs.regs[reg];
	if ((dp & 0x800) == 0)
		regd = (uae_s32)(uae_s16)regd;
	regd <<= (dp >> 9) & 3;
	if (dp & 0x100) {
		uae_s32 outer = 0;
		if (dp & 0x80) base = 0;
		if (dp & 0x40) regd = 0;

		if ((dp & 0x30) == 0x20) {
			base += (uae_s32)(uae_s16)get_word_debug(pc);
			pc += 2;
		}
		if ((dp & 0x30) == 0x30) {
			base += get_long_debug(pc);
			pc += 4;
		}

		if ((dp & 0x3) == 0x2) {
			outer = (uae_s32)(uae_s16)get_word_debug(pc);
			pc += 2;
		}
		if ((dp & 0x3) == 0x3) {
			outer = get_long_debug(pc);
			pc += 4;
		}

		if ((dp & 0x4) == 0)
			base += regd;
		if (dp & 0x3)
			base = get_long_debug(base);
		if (dp & 0x4)
			base += regd;

		return base + outer;
	} else {
		return base + (uae_s32)((uae_s8)dp) + regd;
	}
}

static uaecptr calc_jsr(void)
{
	uae_u16 opcode = regs.opcode;
	uaecptr pc = regs.instruction_pc;
	if ((opcode & 0xff00) == 0x6100) {
		uae_u8 o = opcode & 0xff;
		if (o == 0) {
			return pc + (uae_s16)get_word_debug(pc + 2) + 2;
		} else if (o == 0xff) {
			return pc + (uae_s32)get_long_debug(pc + 2) + 2;
		} else {
			return pc + (uae_s8)o + 2;
		}
	} else if ((opcode & 0xffc0) == 0x4e80) {
		int reg = opcode & 7;
		int mode = (opcode >> 3) & 7;
		switch (mode)
		{
		case 2:
			return m68k_areg(regs, reg);
		case 5:
			return m68k_areg(regs, reg) + (uae_s16)get_word_debug(pc + 2);
		case 6:
			return debug_get_disp_ea(pc + 2, m68k_areg(regs, reg));
		case 7:
			if (reg == 0)
				return (uae_u32)(uae_s16)get_word_debug(pc + 2);
			if (reg == 1)
				return get_long_debug(pc + 2);
			if (reg == 2)
				return pc + (uae_s16)get_word_debug(pc + 2) + 2;
			if (reg == 3)
				return debug_get_disp_ea(pc + 2, pc + 2);
			break;
		}
	}
	return 0xffffffff;
}
#endif

void branch_stack_pop_rte(uaecptr oldpc)
{
	if (!stackframes)
		return;
	uaecptr newpc = m68k_getpc();
	bool found = false;
	for (int i = stackframecntsuper - 1; i >= 0; i--) {
		struct debugstackframe *sf = &stackframessuper[i];
		if (sf->next_pc == newpc) {
			stackframecntsuper = i;
			found = true;
			break;
		}
	}
	if (found) {
		if (break_stack_pop) {
			break_stack_pop = false;
			debugmem_break(0);
		}
	} else {
		// if no match, it probably was stack switch to other address,
		// assume it matched..
		if (stackframecntsuper > 0)
			stackframecntsuper--;
	}
	if (found && break_stack_pop && break_stack_s) {
		break_stack_push = false;
		break_stack_pop = false;
		debugmem_break(0);
	}
}

void branch_stack_pop_rts(uaecptr oldpc)
{
	if (!stackframes)
		return;
	if (!stackframemode) {
		if (debug_waiting || (!regs.s && get_long_host(exec_thistask) != debug_task))
			return;
	}
	int cnt = regs.s ? stackframecntsuper : stackframecnt;
	uaecptr newpc = m68k_getpc();
	bool found = false;
	for (int i = cnt - 1; i >= 0; i--) {
		struct debugstackframe *sf = regs.s ? &stackframessuper[i] : &stackframes[i];
		if (sf->next_pc == newpc) {
			cnt = i;
			found = true;
			break;
		}
	}
	if (regs.s) {
		stackframecntsuper = cnt;
	} else {
		stackframecnt = cnt;
		stackframecntsuper = 0;
	}
	if (found && break_stack_pop && ((break_stack_s && regs.s) || (!break_stack_s && !regs.s))) {
		break_stack_push = false;
		break_stack_pop = false;
		debugmem_break(0);
	}
}

void branch_stack_push(uaecptr oldpc, uaecptr newpc)
{
	if (!stackframes)
		return;
	if (!stackframemode) {
		if (debug_waiting || (!regs.s && get_long_host(exec_thistask) != debug_task))
			return;
	}
	int cnt = regs.s ? stackframecntsuper : stackframecnt;
	struct debugstackframe *sf = regs.s ? &stackframessuper[cnt] : &stackframes[cnt];
	sf->current_pc = regs.instruction_pc;
	sf->next_pc = newpc;
	sf->stack = m68k_areg(regs, 7);
	sf->branch_pc = m68k_getpc();
	MakeSR();
	sf->sr = regs.sr;
	memcpy(sf->regs, regs.regs, sizeof(uae_u32) * 16);
	if (regs.s) {
		stackframecntsuper++;
	} else {
		stackframecnt++;
		stackframecntsuper = 0;
	}
	if (break_stack_push && ((break_stack_s && regs.s) || (!break_stack_s && !regs.s))) {
		break_stack_push = false;
		break_stack_pop = false;
		debugmem_break(11);
	}
}

bool debugmem_break_stack_pop(void)
{
	if (!stackframes)
		return false;
	break_stack_s = regs.s != 0;
	break_stack_pop = true;
	return true;
}

bool debugmem_break_stack_push(void)
{
	if (!stackframes)
		return false;
	break_stack_s = regs.s != 0;
	break_stack_pop = true;
	break_stack_push = true;
	return true;
}

static bool debugmem_func(uaecptr addr, int rwi, int size, uae_u32 val)
{
	bool ret = true;
	uaecptr oaddr = addr;
	struct debugmemdata *dmfirst = NULL;

	if (debug_waiting && (rwi & DEBUGMEM_FETCH)) {
		// first instruction?
		if (addr == debugmem_bank.start + HUNK_GAP + 8) {
			debugmem_break(2);
			debug_waiting = false;
		}
	}

	// in some situations addr may already have start substracted
	if (addr >= debugmem_bank.start) {
		addr -= debugmem_bank.start;
	}

	for (int i = 0; i < size; i++) {
		int offset = addr & PAGE_SIZE_MASK;
		int page = addr / PAGE_SIZE;
		struct debugmemdata *dm = dmd[page];
		uae_u8 state = dm->state[offset];

		if (!i)
			dmfirst = dm;

		if (!(state & DEBUGMEM_INUSE)) {
			debugreport(dm, oaddr, rwi, size, _T("Accessing invalid memory"));
			return false;
		}

		if (!(rwi & DEBUGMEM_NOSTACKCHECK) || ((rwi & DEBUGMEM_NOSTACKCHECK) && !(dm->flags & DEBUGMEM_STACK))) {
			if ((rwi & DEBUGMEM_FETCH) && !(state & DEBUGMEM_INITIALIZED)) {
				debugreport(dm, oaddr, rwi, size, _T("Instruction fetch from uninitialized memory"));
				return false;
			}

			if ((rwi & DEBUGMEM_FETCH) && (state & DEBUGMEM_WRITE) && !(state & DEBUGMEM_FETCH)) {
				debugreport(dm, oaddr, rwi, size, _T("Instruction fetch from memory that was modified"));
				return false;
			}
		}

		if ((rwi & DEBUGMEM_READ) && !(state & DEBUGMEM_INITIALIZED) && !(state & DEBUGMEM_WRITE)) {
			debugreport(dm, oaddr, rwi, size, _T("Reading uninitialized memory"));
			return false;
		}

		if (dm->flags & DEBUGMEM_PARTIAL) {
			if (offset < dm->unused_start) {
				debugreport(dm, oaddr, rwi, size, _T("Reading from partially invalid page (low)"));
				return false;
			}
			if (offset >= PAGE_SIZE - dm->unused_end) {
				debugreport(dm, oaddr, rwi, size, _T("Reading from partially invalid page (high)"));
				return false;
			}
		}
		if ((state | rwi) != state) {
			//console_out_f(_T("addr %08x %d/%d (%02x -> %02x) PC=%08x\n"), addr, i, size, state, rwi, M68K_GETPC);
			dm->state[offset] |= rwi;
			
		}
		addr++;
	}

	return ret;
}

static uae_u32 REGPARAM2 debugmem_lget(uaecptr addr)
{
	uae_u32 v = 0xdeadf00d;
	if (debugmem_func(addr, DEBUGMEM_READ, 4, 0))
		v = debugmem_func_lget(addr);
	return v;
}
static uae_u32 REGPARAM2 debugmem_wget(uaecptr addr)
{
	uae_u32 v = 0xd00d;
	if (debugmem_func(addr, DEBUGMEM_READ, 2, 0))
		v = debugmem_func_wget(addr);
	return v;
}
static uae_u32 REGPARAM2 debugmem_bget(uaecptr addr)
{
	uae_u32 v = 0xdd;
	if (debugmem_func(addr, DEBUGMEM_READ, 1, 0))
		v = debugmem_func_bget(addr);
	return v;
}
static uae_u32 REGPARAM2 debugmem_lgeti(uaecptr addr)
{
	uae_u32 v = 0x4afc4afc;
	if (debugmem_func(addr, DEBUGMEM_READ | DEBUGMEM_FETCH, 4, 0))
		v = debugmem_func_lgeti(addr);
	return v;
}
static uae_u32 REGPARAM2 debugmem_wgeti(uaecptr addr)
{
	uae_u32 v = 0x4afc;
	if (debugmem_func(addr, DEBUGMEM_READ | DEBUGMEM_FETCH, 2, 0))
		v = debugmem_func_wgeti(addr);
	return v;
}
static void REGPARAM2 debugmem_lput(uaecptr addr, uae_u32 v)
{
	if (debugmem_func(addr, DEBUGMEM_WRITE, 4, v))
		debugmem_func_lput(addr, v);
}
static void REGPARAM2 debugmem_wput(uaecptr addr, uae_u32 v)
{
	if (debugmem_func(addr, DEBUGMEM_WRITE, 2, v))
		debugmem_func_wput(addr, v);
}
static void REGPARAM2 debugmem_bput(uaecptr addr, uae_u32 v)
{
	if (debugmem_func(addr, DEBUGMEM_WRITE, 1, v))
		debugmem_func_bput(addr, v);
}
static uae_u8 *REGPARAM2 debugmem_xlate(uaecptr addr)
{
	if (debug_waiting)
		debugmem_func(addr, DEBUGMEM_FETCH | DEBUGMEM_READ | DEBUGMEM_NOSTACKCHECK, 2, 0);
	addr -= debugmem_bank.start & debugmem_bank.mask;
	addr &= debugmem_bank.mask;
	return debugmem_bank.baseaddr + addr;
}

static int debugmem_free(uaecptr addr, uae_u32 size)
{
	uaecptr oaddr = addr;
	addr -= debugmem_bank.start;
	int page = addr / PAGE_SIZE;
	struct debugmemdata *dm = dmd[page];
	bool ok = true;

	if (!(dm->flags & DEBUGMEM_ALLOCATED)) {
		console_out_f(_T("Invalid memory free (%08x %d) Start address points to unallocated memory\n"), oaddr, size);
		ok = false;
	} else if (!(dm->flags & DEBUGMEM_STARTBLOCK)) {
		console_out_f(_T("Invalid memory free (%08x %d) Start address points to allocated memory but not start of allocated memory\n"), oaddr, size);
		ok = false;
	} else {
		struct debugmemallocs *dma = allocs[dm->id];
		if (dma->start == addr && dma->size == size) {
			// it was valid!
			for (int i = 0; i < dma->pages; i++) {
				struct debugmemdata *dm2 = dmd[page + i];
				memset(dm2, 0, sizeof(struct debugmemdata));
			}
			memset(debugmem_bank.baseaddr + dma->start_page * PAGE_SIZE, 0x95, dma->pages * PAGE_SIZE);
			memset(dma, 0, sizeof(struct debugmemallocs));
			return dm->id;
		}
		console_out_f(_T("Invalid memory free (%08x %d) ID=%d Start address points to start of allocated memory but size does not match allocation size (%d <> %d)\n"),
			oaddr, size, dm->id, size, dma->size);
	}

	// report free memory error
	int end = (size + PAGE_SIZE - 1) & ~PAGE_SIZE_MASK;
	int allocid = -1;
	for (int i = 0; i < end; i++) {
		if (page + i >= totalmemdata) {
			console_out_f(_T("Free end address is out of range\n"));
			ok = false;
			break;
		}
		struct debugmemdata *dm2 = dmd[page + i];
		if ((dm2->flags & DEBUGMEM_ALLOCATED) && allocid != page + i) {
			struct debugmemallocs *dma = allocs[dm2->id];
			console_out_f(_T("Conflicts with existing allocation ID=%d (%08x - %08x %d)\n"),
				dm->id, dma->start, dma->start + dma->size - 1, dma->size);
			debugreportalloc(dma);
			allocid = page + i;
		}
	}
	debugmem_break(3);
	return 0;
}


static uae_u32 REGPARAM2 debugmem_chipmem_lget(uaecptr addr)
{
	uae_u32 *m;

	if (addr < debugmem_chiplimit)
		return debugmem_chiphit(addr, 0, -4);
	addr &= chipmem_bank.mask;
	m = (uae_u32 *)(chipmem_bank.baseaddr + addr);
	return do_get_mem_long(m);
}

static uae_u32 REGPARAM2 debugmem_chipmem_wget(uaecptr addr)
{
	uae_u16 *m, v;

	if (addr < debugmem_chiplimit)
		return debugmem_chiphit(addr, 0, -2);
	addr &= chipmem_bank.mask;
	m = (uae_u16 *)(chipmem_bank.baseaddr + addr);
	v = do_get_mem_word(m);
	return v;
}

static uae_u32 REGPARAM2 debugmem_chipmem_bget(uaecptr addr)
{
	uae_u8 v;
	if (addr < debugmem_chiplimit)
		return debugmem_chiphit(addr, 0, -1);
	addr &= chipmem_bank.mask;
	v = chipmem_bank.baseaddr[addr];
	return v;
}

void REGPARAM2 debugmem_chipmem_lput(uaecptr addr, uae_u32 l)
{
	if (addr < debugmem_chiplimit) {
		debugmem_chiphit(addr, l, 4);
	} else {
		uae_u32 *m;
		addr &= chipmem_bank.mask;
		m = (uae_u32 *)(chipmem_bank.baseaddr + addr);
		do_put_mem_long(m, l);
	}
}

void REGPARAM2 debugmem_chipmem_wput(uaecptr addr, uae_u32 w)
{
	if (addr < debugmem_chiplimit) {
		debugmem_chiphit(addr, w, 2);
	} else {
		uae_u16 *m;
		addr &= chipmem_bank.mask;
		m = (uae_u16 *)(chipmem_bank.baseaddr + addr);
		do_put_mem_word(m, w);
	}
}

void REGPARAM2 debugmem_chipmem_bput(uaecptr addr, uae_u32 b)
{
	if (addr < debugmem_chiplimit) {
		debugmem_chiphit(addr, b, 1);
	} else {
		addr &= chipmem_bank.mask;
		chipmem_bank.baseaddr[addr] = b;
	}
}

static int REGPARAM2 debugmem_chipmem_check(uaecptr addr, uae_u32 size)
{
	addr &= chipmem_bank.mask;
	return (addr + size) <= chipmem_bank.reserved_size;
}

static uae_u8 *REGPARAM2 debugmem_chipmem_xlate(uaecptr addr)
{
	addr &= chipmem_bank.mask;
	return chipmem_bank.baseaddr + addr;
}

static addrbank debugmem_chipmem_bank = {
	debugmem_chipmem_lget, debugmem_chipmem_wget, debugmem_chipmem_bget,
	debugmem_chipmem_lput, debugmem_chipmem_wput, debugmem_chipmem_bput,
	debugmem_chipmem_xlate, debugmem_chipmem_check, NULL, _T("chip"), _T("Debug Chip memory"),
	debugmem_chipmem_lget, debugmem_chipmem_wget,
	ABFLAG_RAM | ABFLAG_THREADSAFE | ABFLAG_CHIPRAM | ABFLAG_CACHE_ENABLE_BOTH, 0, 0
};

static void setchipbank(bool activate)
{
	if (activate) {
		map_banks(&debugmem_chipmem_bank, 0, 1, 0);
	} else {
		map_banks(&chipmem_bank, 0, 1, 0);
	}
}

static struct debugmemallocs *getallocblock(void)
{
	int round = 0;
	alloccnt++;
	if (alloccnt >= MAX_DEBUGMEMALLOCS) {
		alloccnt = 1;
	}
	while (allocs[alloccnt]->id != 0) {
		alloccnt++;
		if (alloccnt >= MAX_DEBUGMEMALLOCS) {
			alloccnt = 1;
			if (round) {
				console_out_f(_T("debugmem out of alloc blocks!\n"));
				return NULL;
			}
			round++;
		}
	}
	struct debugmemallocs *dm = allocs[alloccnt];
	dm->id = alloccnt;
	return dm;
}

static struct debugmemallocs *debugmem_allocate(uae_u32 size, uae_u32 flags, uae_u32 parentid)
{
	struct debugmemallocs *dm = getallocblock();
	if (!dm)
		return NULL;
	int offset = debugmemptr / PAGE_SIZE;
	bool gotit = true;
	int totalsize = 0;
	int extrasize = 0;
	for (extrasize = HUNK_GAP; extrasize >= PAGE_SIZE; extrasize /= 2) {
		totalsize = (extrasize + size + PAGE_SIZE - 1) & ~PAGE_SIZE_MASK;
		for (int i = 0; i < totalmemdata; i++) {
			struct debugmemdata *dm = dmd[offset];
			if (offset + totalsize / PAGE_SIZE >= totalmemdata) {
				offset = 0;
				continue;
			}
			gotit = true;
			// extra + size continous space available?
			if (!(dm->flags & DEBUGMEM_ALLOCATED)) {
				for (int j = 0; j < totalsize / PAGE_SIZE; j++) {
					struct debugmemdata *dm2 = dmd[offset + j];
					if (dm->flags & DEBUGMEM_ALLOCATED) {
						gotit = false;
						break;
					}
				}
			}
			if (gotit)
				break;
			offset++;
			offset = offset % totalmemdata;
		}
		if (gotit)
			break;
	}
	if (!gotit || !totalsize || !extrasize) {
		console_out_f(_T("debugmem out of free space! Alloc size %d, flags %08x\n"), size, flags);
		return 0;
	}
	dm->parentid = parentid;
	dm->start_page = offset;
	dm->pages = totalsize / PAGE_SIZE;
	for (int j = 0; j < dm->pages; j++) {
		struct debugmemdata *dm2 = dmd[offset + j];
		dm2->flags |= DEBUGMEM_ALLOCATED;
		dm2->id = dm->id;
	}
	memset(debugmem_bank.baseaddr + offset * PAGE_SIZE, 0xa3, totalsize);
	int startoffset = extrasize / PAGE_SIZE;
	dm->start = (offset + startoffset) * PAGE_SIZE;
	dm->size = size;
	dm->pc = M68K_GETPC;
	for (int j = 0; j < (size + PAGE_SIZE - 1) / PAGE_SIZE; j++) {
		struct debugmemdata *dm2 = dmd[offset + startoffset + j];
		dm2->flags |= DEBUGMEM_INUSE | flags;
		if (j == 0) {
			dm2->flags |= DEBUGMEM_STARTBLOCK;
		}
		memset(dm2->state, ((flags & DEBUGMEM_INITIALIZED) ? DEBUGMEM_INITIALIZED : 0) | DEBUGMEM_INUSE, PAGE_SIZE);
		uae_u8 filler = (flags & DEBUGMEM_INITIALIZED) ? 0x00 : 0x99;
		memset(debugmem_bank.baseaddr + (offset + startoffset + j) * PAGE_SIZE, filler, PAGE_SIZE);
		if (j == (size + PAGE_SIZE - 1) / PAGE_SIZE - 1) {
			if (size & PAGE_SIZE_MASK) {
				dm2->unused_end = PAGE_SIZE - (size & PAGE_SIZE_MASK);
				dm2->flags |= DEBUGMEM_PARTIAL;
				memset(dm2->state + (PAGE_SIZE - dm2->unused_end), 0, dm2->unused_end);
				memset(debugmem_bank.baseaddr + (offset + startoffset + j) * PAGE_SIZE + (PAGE_SIZE - dm2->unused_end), 0x97, dm2->unused_end);
			}
		}
	}
	debugmemptr = offset * PAGE_SIZE + extrasize + ((size + PAGE_SIZE - 1) & ~PAGE_SIZE_MASK);
	return dm;
}

static int debugmem_unreserve(uaecptr addr, uae_u32 size, bool noerror)
{
	for (int i = 0; i < MAX_DEBUGMEMALLOCS; i++) {
		struct debugmemallocs *alloc = allocs[i];
		if (alloc->type != DEBUGALLOC_SEG)
			continue;
		if (alloc->start == addr && alloc->size == size) {
			int id = alloc->parentid;
			memset(alloc, 0, sizeof(struct debugmemallocs));
			return id;
		}
		if (alloc->start == addr) {
			console_out_f(_T("Invalid memory unreserve %08x - %08x %d. Start address points to start of reserved memory but size does not match allocation size (%d <> %d)\n"),
				addr, addr + size - 1, size, size, alloc->size);
			return 0;
		}
	}
	if (!noerror)
		console_out_f(_T("Invalid memory unreserve %08x - %08x %d\n"), addr, addr + size - 1, size);
	return 0;
}

// 0 = allocmem
// 1 = allocvec

uaecptr debugmem_allocmem(int mode, uae_u32 size, uae_u32 flags, uae_u32 caller)
{
	if (!debugmem_bank.baseaddr || !size)
		return 0;
	if (flags & 2) // MEMF_CHIP?
		return 0;
	uae_u16 aflags = DEBUGMEM_READ | DEBUGMEM_WRITE;
	if (flags & 0x10000) // MEMF_CLEAR
		aflags |= DEBUGMEM_INITIALIZED;
	if (mode)
		size += 4;
	struct debugmemallocs *dm = debugmem_allocate(size, aflags, 0);
	if (!dm)
		return 0;
	dm->type = mode ? DEBUGALLOC_ALLOCVEC : DEBUGALLOC_ALLOCMEM;
	dm->pc = caller;
	debugreportalloc(dm);
	uaecptr mem = dm->start + debugmem_bank.start;
	if (mode) {
		put_long(mem, size);
		mem += 4;
	}
	return mem;
}

uae_u32 debugmem_freemem(int mode, uaecptr addr, uae_u32 size, uae_u32 caller)
{
	if (!debugmem_bank.baseaddr || addr < debugmem_bank.start)
		return 0;
	if (mode > 0) {
		addr -= 4;
		size = get_long(addr);
	}
	int id = debugmem_free(addr, size);
	if (id) {
		console_out_f(_T("ID=%d: %s(%08x,%d) %08x - %08x PC=%08x\n"), id, mode ? _T("FreeVec") : _T("AllocMem"), addr, size, addr, addr + size - 1, caller);
	}
	return 1;
}

static struct debugmemallocs *debugmem_reserve(uaecptr addr, uae_u32 size, uae_u32 parentid)
{
	struct debugmemallocs *dm = getallocblock();
	if (!dm)
		return NULL;
	dm->type = DEBUGALLOC_SEG;
	dm->parentid = parentid;
	dm->start = addr;
	dm->size = size;
	return dm;
}

uae_u32 debugmem_exit(void)
{
	bool err = false;
	console_out_f(_T("Debugged program exited\n"));
	for (int i = 0; i < MAX_DEBUGMEMALLOCS; i++) {
		struct debugmemallocs *dma = allocs[i];
		if (dma->type == DEBUGALLOC_ALLOCMEM || dma->type == DEBUGALLOC_ALLOCVEC) {
			err = true;
			console_out_f(_T("Memory allocation (%08x,%d) %08x - %08x was not freed.\n"),
				dma->start + debugmem_bank.start, dma->size,
				dma->start + debugmem_bank.start, dma->start + (dma->size - 1) + debugmem_bank.start);
		}
	}
	if (err) {
		debugmem_break(4);
	}

	debugmem_trace = false;
	debugmem_active = false;
	debugmem_chiplimit = 0;
	chipmem_setindirect();
	setchipbank(false);
	return 1;
}

static uae_u32 gl(uae_u8 *p)
{
	return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | (p[3]);
}

static struct debugcodefile *loadcodefile(const TCHAR *path, const TCHAR *name)
{
	TCHAR fpath[MAX_DPATH];
	fpath[0] = 0;
	if (path)
		_tcscat(fpath, path);
	_tcscat(fpath, name);
	struct zfile *zf = zfile_fopen(fpath, _T("rb"));
	if (!zf) {
		console_out_f(_T("Couldn't open source file '%s'\n"), fpath);
		return NULL;
	}
	int length;
	uae_u8 *data2 = zfile_getdata(zf, 0, -1, &length);
	if (!data2) {
		zfile_fclose(zf);
		console_out_f(_T("Couldn't read source file '%s'\n"), fpath);
		return NULL;
	}
	uae_u8 *data = xcalloc(uae_u8, length + 1);
	memcpy(data, data2, length);
	xfree(data2);
	zfile_fclose(zf);
	struct debugcodefile *cf = codefiles[codefilecnt];
	if (!cf) {
		cf = codefiles[codefilecnt] = xcalloc(struct debugcodefile, 1);
	}
	codefilecnt++;
	cf->name = my_strdup(name);
	cf->data = data;
	cf->length = length;
	cf->lines = 1;
	for (int i = 0; i < length; i++) {
		if (data[i] == 0) {
			data[i] = 32;
		}
		if (data[i] == 10) {
			data[i] = 0;
			cf->lines++;
		}
	}
	cf->lineptr = xcalloc(uae_u8*, 1 + cf->lines + 2);
	int linecnt = 1;
	int lasti = 0;
	for (int i = 0; i <= length; i++) {
		if (data[i] == 0) {
			uae_u8 *s = &data[lasti];
			lasti = i + 1;
			if (strlen((char*)s) >= MAX_SOURCELINELEN) {
				s[MAX_SOURCELINELEN] = 0;
			}
			cf->lineptr[linecnt++] = s;
			int len = strlen((char*)s);
			if (len > 0 && s[len - 1] == 13)
				s[len - 1] = 0;
		}
	}
	console_out_f(_T("Loaded source file '%s', %d bytes, %d lines\n"), fpath, cf->length, cf->lines);
	return cf;
}

static uae_u32 maptohunks(uae_u32 offset)
{
	for (int i = 1; i <= executable_last_segment; i++) {
		struct debugmemallocs *alloc = allocs[i];
		if (offset >= alloc->relative_start && offset < alloc->relative_start + alloc->size) {
			uae_u32 address = offset - alloc->relative_start + alloc->start + debugmem_bank.start + 8;
			return address;
		}
	}
	return 0xffffffff;
}

static struct debugsymbol *issymbol(const TCHAR *name)
{
	for (int i = 0; i < symbolcnt; i++) {
		struct debugsymbol *ds = symbols[i];
		if (!_tcsicmp(ds->name, name)) {
			return ds;
		}
	}
	return NULL;
}


static struct stab *findstab(uae_u8 type, int *idxp)
{
	int idx = idxp ? *idxp : 0;
	while (idx < stabscount) {
		if (stabs[idx].type == type) {
			if (idxp)
				*idxp = idx + 1;
			return &stabs[idx];
		}
		idx++;
	}
	return NULL;
}

static void parse_stabs(void)
{
	TCHAR *path = NULL;
	struct stab *s;
	int idx;

	idx = 0;
	for (;;) {
		s = findstab(N_SO, &idx);
		if (!s)
			break;
		if (s->string && s->string[_tcslen(s->string) - 1] == '/') {
			path = s->string;
			break;
		}
	}
	idx = 0;
	while (idx < stabscount) {
		s = &stabs[idx++];
		if (!s)
			break;
		if (s->type == N_SO && s->string && s->string[_tcslen(s->string) - 1] != '/') {
			struct debugcodefile *cf = loadcodefile(NULL, s->string);
			if (!cf) {
				cf = loadcodefile(path, s->string);
				if (!cf) {
					if (!_tcsnicmp(path, _T("/cygdrive/"), 10)) {
						TCHAR path2[MAX_DPATH];
						_stprintf(path2, _T("%c:/%s"), path[10], path + 12);
						cf = loadcodefile(path2, s->string);
						if (!cf) {
							console_out_f(_T("Failed to load '%s'\n"), s->string);
							continue;
						}
					}
				}
			}
			int linecnt = 0;
			struct debugsymbol *last_func = NULL;
			while (idx < stabscount) {
				TCHAR stripname[256];
				int type = 0;
				TCHAR mode = 0;
				s = &stabs[idx++];
				if (s->type == N_SO)
					break;
				if (s->string) {
					_tcscpy(stripname, s->string);
					const TCHAR *ss = _tcschr(s->string, ':');
					if (ss) {
						mode = ss[1];
						type = _tstol(ss + 2);
						stripname[ss - s->string] = 0;
					}
				}

				switch(s->type)
				{
					case N_SLINE:
					case N_DSLINE:
						if (s->val >= linemapsize) {
							console_out_f(_T("Line address larger than segment size!? %08x >= %08x\n"), s->val, linemapsize);
							return;
						}
						linemap[s->val].file = cf;
						linemap[s->val].line = s->desc;
						//write_log(_T("%08x %d %s\n"), s->val, s->desc, cf->name);
						linecnt++;
					break;
					case N_FUN:
					case N_STSYM:
					case N_LCSYM:
					{
						if (s->string) {
							struct debugsymbol *ds = issymbol(s->string);
							if (!ds) {
								uae_u32 addr = maptohunks(s->val);
								if (addr != 0xffffffff) {
									ds = symbols[symbolcnt++];
									ds->name = my_strdup(stripname);
									ds->value = addr;
									ds->flags = mode == mode > 'Z' ? SYMBOL_LOCAL : SYMBOL_GLOBAL;
								}
							}
							if (ds) {
								if (mode == 'F' || mode == 'f')
									ds->type = SYMBOLTYPE_FUNC;
								if (s->type == N_FUN) {
									last_func = ds;
								}
							}
						}
					}
					break;
					case N_GSYM:
					{
						for (int i = 0; i < symbolcnt; i++) {
							struct debugsymbol *ds = symbols[i];
							if (ds->name[0] == '_' && !_tcscmp(ds->name + 1, stripname)) {
								ds->flags = SYMBOL_GLOBAL;
								break;
							}
						}
					}
					break;
					case N_LSYM:
					{
						// type?
						const TCHAR *ts = _tcschr(s->string, '=');
						if (ts) {
							const TCHAR *tts = _tcschr(s->string, ':');
							if (tts) {
								int tid = _tstol(tts + 2);
								if (tid > 0) {
									if (!cf->stabtypes) {
										cf->stabtypecount = 1000;
										cf->stabtypes = xcalloc(struct stabtype, cf->stabtypecount);
									}
									if (tid < cf->stabtypecount) {
										struct stabtype *st = &cf->stabtypes[tid];
										st->name = my_strdup(s->string);
										st->name[tts - s->string] = 0;
										st->id = tid;
									}
								}
							}
						} else if (last_func) {
							// stack variable
							if (stackvarcnt < MAX_STACKVARS) {
								struct stackvar *sv = stackvars[stackvarcnt++];
								sv->func = last_func;
								sv->name = my_strdup(stripname);
								sv->offset = s->val;
								if (!last_func->data)
									last_func->data = sv;
							}
						}
					}
					break;
					case N_LBRAC:
					case N_RBRAC:
					break;
					case N_ROSYM:
					case N_RSYM:
						//console_out_f(_T("%02x %02x %04x %08x %s\n"), s->type, s->other, s->desc, s->val, s->string);
					break;
					default:
						//console_out_f(_T("%02x %02x %04x %08x %s\n"), s->type, s->other, s->desc, s->val, s->string);
					break;
				}
			}
		}
	}
	/*
	int prev = -1;
	for (int i = 0; i < linemapsize; i++) {
		struct linemapping *lm = &linemap[i];
		if (lm->line >= 0) {
			prev = i;
		} else if (prev >= 0 && lm->line < 0) {
			lm->file = linemap[prev].file;
			lm->line = linemap[prev].line;
		}

	}
	*/
}

uaecptr debugmem_reloc(uaecptr exeaddress, uae_u32 len, uaecptr task, uae_u32 *stack)
{
	uae_u8 *p = get_real_address(exeaddress);
	uae_u8 *lastptr = NULL;
	struct debugmemallocs *hunks[1000];
	uae_u32 lens[1000], memtypes[1000];
	uae_u32 parentid = 0;

	debugmem_init();
	if (!debugmem_initialized)
		return 0;

	console_out_f(_T("Loading executable, exe=%08x\n"), exeaddress);

	if (gl(p) != 0x3f3) {
		console_out_f(_T("HUNK_HEADER not found!\n"));
		return 0;
	}
	p += 4;
	if (gl(p) != 0) {
		console_out_f(_T("Long word after HUNK_HEADER is not zero!\n"));
		return 0;
	}
	p += 4;
	int hunkcnt = gl(p);
	int first = gl(p + 4);
	int last = gl(p + 8);
	if (hunkcnt > 1000 || (last - first + 1) > 1000) {
		console_out_f(_T("Too many hunks.\n"));
		return 0;
	}
	if (first > last) {
		console_out_f(_T("First hunk larger than last hunk (%d > %d).\n"), first, last);
		return 0;
	}
	p += 12;
	uae_u32 relative_start = 0;
	for (int i = first; i <= last; i++) {
		uae_u32 len = gl(p);
		p += 4;
		memtypes[i] = 0;
		if ((len & 0xc0000000) == 0xc0000000) {
			memtypes[i] = gl(p);
			p += 4;
		} else if (len & 0x40000000) {
			memtypes[i] = 0x40000000;
		} else {
			memtypes[i] = len & 0xc0000000;
		}
		len &= ~(0x80000000 | 0x40000000);
		lens[i] = len * 4;
		struct debugmemallocs *dma = debugmem_allocate(lens[i] + 8, DEBUGMEM_READ | DEBUGMEM_WRITE | DEBUGMEM_FETCH | DEBUGMEM_INITIALIZED, parentid);
		hunks[i] = dma;
		dma->type = DEBUGALLOC_HUNK;
		dma->relative_start = relative_start;
		relative_start += lens[i];
		if (!hunks[i]) {
			console_out_f(_T("Hunk #%d memory allocation failed, size %d\n"), i, len + 8);
			return 0;
		}
		if (!parentid) {
			parentid = hunks[i]->id;
		}
		linemapsize += lens[i];
	}
	for (int i = first; i <= last; i++) {
		struct debugmemallocs *memdm = hunks[i];
		uae_u32 len = lens[i];
		uae_u32 hunktype = gl(p) & ~0xc0000000;
		uae_u32 hunklen = gl(p + 4);
		hunklen *= 4;
		p += 8;
		uae_u32 memflags = 0;
		if (hunktype != 0x3e9 && hunktype != 0x3ea && hunktype != 0x3eb) {
			console_out_f(_T("Unknown hunk #%d %08x.\n"), i, hunktype);
			return 0;
		}
		memdm->idtype = hunktype | memtypes[i];
		if (hunklen > len) {
			console_out_f(_T("Hunk #%d contents (%d) larger than allocation (%d)!\n"), i, hunklen, len);
			return 0;
		}
		uae_u8 *mem = memdm->start + debugmem_bank.baseaddr;
		uaecptr memaddr = memdm->start + debugmem_bank.start;
		if (hunktype != 0x3eb) {
			for (int c = 0; c < hunklen; c++) {
				put_byte_host(mem + 8 + c, *p++);
			}
		}
		put_long_host(mem, len / 4);
		if (lastptr) {
			put_long_host(lastptr, memaddr / 4 + 1);
		}
		lastptr = mem + 4;
		if (gl(p) == 0x3ec) { // hunk reloc
			if (hunktype == 0x3eb) {
				console_out_f(_T("HUNK_BSS with HUNK_RELOC32!\n"));
				return 0;
			}
			p += 4;
			for (;;) {
				int reloccnt = gl(p);
				p += 4;
				if (!reloccnt)
					break;
				int relochunk = gl(p);
				p += 4;
				if (relochunk > last) {
					console_out_f(_T("HUNK_RELOC hunk #%d is larger than last hunk (%d)!\n"), relochunk, last);
					return 0;
				}
				uaecptr hunkptr = hunks[relochunk]->start + debugmem_bank.start + 8;
				uae_u8 *currenthunk = mem + 8;
				for (int j = 0; j < reloccnt; j++) {
					uae_u32 reloc = gl(p);
					p += 4;
					if (reloc >= len - 3) {
						console_out_f(_T("HUNK_RELOC hunk #%d offset %d larger than hunk lenght %d!\n"), i, reloc, len);
						return 0;
					}
					put_long_host(currenthunk + reloc, get_long_host(currenthunk + reloc) + hunkptr);
				}
			}
		}

		for (;;) {

			if (gl(p) == 0x3f0) { // hunk symbol

				int symcnt = 0;
				p += 4;
				for (;;) {
					int size = gl(p);
					p += 4;
					if (!size)
						break;
					struct debugsymbol *ds = symbols[symbolcnt++];
					ds->name = au((char*)p);
					p += 4 * size;
					ds->value = gl(p) + hunks[i]->start + 8 + debugmem_bank.start;
					ds->allocid = hunks[i]->id;
					ds->section = hunks[i];
					ds->flags = SYMBOL_GLOBAL;
					p += 4;
					symcnt++;
				}
				console_out_f(_T("Hunk %d: %d symbols loaded.\n"), i, symcnt);

			} else if (gl(p) == 0x3f1) { // hunk debug

				p += 4;
				int size = gl(p);
				p += 4;
				uae_u8 *p2 = p;
				if (size >= 12) {
					if (gl(p) == 0x0000010b) { // "ZMAGIC"
						p += 4;
						int symtab_size = gl(p);
						p += 4;
						int stringtab_size = gl(p);
						p += 4;
						uae_u8 *stringtab = p + symtab_size;
						if (!stabs) {
							stabs = xcalloc(struct stab, symtab_size / 12);
						} else {
							stabs = xrealloc(struct stab, stabs, stabscount + symtab_size / 12);
						}
						for (int i = 0; i <= symtab_size - 12; i += 12, p += 12) {
							struct stab *s = &stabs[stabscount++];
							TCHAR *str = NULL;
							int string_idx = gl(p);
							uae_u8 type = p[4];
							uae_u8 other = p[5];
							uae_u16 desc = (p[6] << 8) | p[7];
							uae_u32 value = gl(p + 8);
							if (string_idx) {
								uae_char *s = (uae_char*)stringtab + string_idx;
								str = au(s);
							}
							s->type = type;
							s->other = other;
							s->desc = desc;
							s->val = value;
							s->string = str;
						}
						console_out_f(_T("%d stabs loaded.\n"), symtab_size / 12);
					} else {
						console_out_f(_T("HUNK_DEBUG is not in expected format\n"));
					}
				}
				p = p2 + 4 * size;

			} else if (gl(p) == 0x3f2) {

				p += 4;

			} else {

				break;

			}
		}
	}
	if (gl(p - 4) != 0x3f2) {
		console_out_f(_T("HUNK_END not found, got %08x\n"), gl(p - 4));
		return 0;
	}
	struct debugmemallocs *stackmem = debugmem_allocate(*stack, DEBUGMEM_READ | DEBUGMEM_WRITE | DEBUGMEM_INITIALIZED | DEBUGMEM_STACK, parentid);
	if (!stackmem)
		return 0;
	stackmem->type = DEBUGALLOC_HUNK;
	stackmem->idtype = 0xffff;
	*stack = stackmem->start + debugmem_bank.start;
	executable_last_segment = alloccnt;

	linemap = xcalloc(struct linemapping, linemapsize + 1);
	for (int i = 0; i < linemapsize + 1; i++) {
		linemap[i].line = -1;
	}

	parse_stabs();
	for (int i = 1; i <= executable_last_segment; i++) {
		debugreportalloc(allocs[i]);
	}

	console_out_f(_T("Executable load complete.\n"));

	uaecptr execbase = get_long_debug(4);
	exec_thistask = get_real_address(execbase + 276);

	setchipbank(true);
	chipmem_setindirect();
	debugger_scan_libraries();

	debug_waiting = true;
	debug_task = task;
	debugmem_trace = true;
	debugmem_active = true;
	return hunks[first]->start + debugmem_bank.start + 8;
}

static uae_char *gethunktext(uae_u8 *p, uae_char *namebuf, int len)
{
	memcpy(namebuf, p, len);
	namebuf[len] = 0;
	return namebuf;
}

static void scan_library_list(uaecptr v, int *cntp)
{
	while ((v = get_long_debug(v))) {
		uae_u32 v2;
		uae_u8 *p;
		addrbank *b = &get_mem_bank(v);
		if (!b || !b->check(v, 32) || !(b->flags & ABFLAG_RAM))
			return;
		v2 = get_long_debug(v + 10); // name
		b = &get_mem_bank(v2);
		if (!b || !b->check(v2, 20))
			return;
		if (!(b->flags & ABFLAG_ROM) && !(b->flags & ABFLAG_ROMIN) && !(b->flags & ABFLAG_RAM))
			return;
		p = b->xlateaddr(v2);
		struct libname *found = NULL;
		for (int i = 0; i < libnamecnt; i++) {
			struct libname *name = &libnames[i];
			char n[256];
			sprintf(n, "%s.library", name->aname);
			if (!strcmp((char*)p, n)) {
				name->base = v;
				found = name;
				break;
			}
			sprintf(n, "%s.device", name->aname);
			if (!strcmp((char*)p, n)) {
				name->base = v;
				found = name;
				break;
			}
			sprintf(n, "%s.resource", name->aname);
			if (!strcmp((char*)p, n)) {
				name->base = v;
				found = name;
				break;
			}
		}
		if (found) {
			(*cntp)++;
			//console_out_f(_T("%08x = '%s'\n"), found->base, found->name);
		}
	}
}

void debugger_scan_libraries(void)
{
	if (!libnamecnt)
		return;
	uaecptr v = get_long_debug(4);
	addrbank *b = &get_mem_bank(v);
	if (!b || !b->check(v, 400) || !(b->flags & ABFLAG_RAM))
		return;
	int cnt = 0;
	scan_library_list(v + 378, &cnt);
	scan_library_list(v + 350, &cnt);
	scan_library_list(v + 336, &cnt);
	console_out_f(_T("%d libraries matched with library symbols.\n"), cnt);
}


bool debugger_get_library_symbol(uaecptr base, uaecptr addr, TCHAR *out)
{
	for (int i = 0; i < libnamecnt; i++) {
		struct libname *name = &libnames[i];
		if (name->base == base) {
			for (int j = 0; j < libsymbolcnt; j++) {
				struct libsymbol *lvo = &libsymbols[j];
				if (lvo->lib == name) {
					if (lvo->value == addr) {
						_stprintf(out, _T("%s/%s"), name->name, lvo->name);
						return true;
					}
				}
			}
		}
	}
	return false;
}

static bool debugger_load_fd(void)
{
	TCHAR plugin_path[MAX_DPATH];
	TCHAR path[MAX_DPATH];
	uae_char line[256];
	struct my_opendir_s *h;
	struct zfile *zf = NULL;

	if (fds_loaded)
		return true;
	fds_loaded = true;

	if (!get_plugin_path(plugin_path, sizeof plugin_path / sizeof(TCHAR), _T("debugger\\fd")))
		return false;
	h = my_opendir(plugin_path);
	if (!h)
		return false;

	for (;;) {
		TCHAR filename[MAX_DPATH];
		if (!my_readdir(h, filename))
			break;
		if (_tcslen(filename) < 3 || _tcsicmp(filename + _tcslen(filename) - 3, _T(".fd")))
			continue;

		_tcscpy(path, plugin_path);
		_tcscat(path, filename);
		zf = zfile_fopen(path, _T("rb"));
		if (!zf)
			continue;

		int cnt = 0;
		int bias = -1;
		struct libname *lvo = NULL;
		uae_u32 lvoid = 1;
		if (libnamecnt > 0) {
			lvoid = libnames[libnamecnt - 1].id;
		}
		for (;;) {
			if (!zfile_fgetsa(line, sizeof(line), zf))
				break;
			for (;;) {
				int len = strlen(line);
				if (len < 1)
					break;
				char c = line[len - 1];
				if (c != 10 && c != 13 && c != 32 && c != '\t')
					break;
				line[len - 1] = 0;
			}
			if (line[0] == '#' && line[1] == '#') {
				char *p1 = &line[2];
				char *p2 = strchr(p1, ' ');
				if (p2) {
					*p2 = 0;
					p2++;
				}
				if (!strcmp(p1, "base")) {
					if (p2[0] == '_')
						p2++;
					lvo = &libnames[libnamecnt++];
					TCHAR *name2 = au(p2);
					lvo->name = name2;
					lvo->aname = strdup(p2);
					lvo->id = lvoid++;
					cnt = 0;
				} else if (!strcmp(p1, "bias")) {
					bias = atol(p2);
				}
				continue;
			}
			if (line[0] == '*' || line[0] == '#')
				continue;
			if (bias < 0 || !lvo)
				continue;
			char *p3 = strchr(line, '(');
			if (p3)
				*p3 = 0;
			TCHAR *name2 = au(line);
			struct libsymbol *ls = &libsymbols[libsymbolcnt++];
			ls->name = name2;
			ls->value = -bias;
			ls->lib = lvo;
			bias += 6;
			cnt++;
		}
		if (lvo)
			console_out_f(_T("Loaded '%s', %d LVOs\n"), lvo->name, cnt);
		zfile_fclose(zf);
	}
	my_closedir(h);
	return true;
}

static bool debugger_load_library(const TCHAR *name)
{
	TCHAR plugin_path[MAX_DPATH];
	uae_char namebuf[256];
	bool ret = false;
	int filelen;
	struct zfile *zf = NULL;

	if (libraries_loaded)
		return true;
	libraries_loaded = true;

	if (get_plugin_path(plugin_path, sizeof plugin_path / sizeof(TCHAR), _T("debugger"))) {
		_tcscat(plugin_path, name);
		zf = zfile_fopen(plugin_path, _T("rb"));
	}
	if (!zf) {
		zf = zfile_fopen(name, _T("rb"));
		if (!zf) {
			console_out_f(_T("Couldn't open '%s'\n"), name);
			return false;
		}
	}
	uae_u8 *file = zfile_getdata(zf, 0, -1, &filelen);
	zfile_fclose(zf);

	uae_u32 len;
	uae_u8 *p = file;
	if (gl(p) != 0x03e7) {
		console_out_f(_T("'%s' is not a library\n"), name);
		goto end;
	}

	if (!libnames) {
		libnames = xcalloc(struct libname, 1000);
		libsymbols = xcalloc(struct libsymbol, 10000);
	}

	struct libname *lvo = NULL;
	int lvoid = 1;
	for (;;) {
		if (p == file + filelen) {
			ret = true;
			goto end;
		}
		if (p == file + filelen) {
			goto end;
		}
		uae_u32 hunk = gl(p);
		p += 4;
		switch (hunk)
		{
			case 0x3e7: // HUNK_UNIT
			{
				lvo = NULL;
				len = gl(p) * 4;
				p += 4;
				uae_char *name = gethunktext(p, namebuf, len);
				if (strlen(name) > 4 && !strcmp(&name[strlen(name) - 4], "_LVO")) {
					name[strlen(name) - 4] = 0;
					lvo = &libnames[libnamecnt++];
					TCHAR *name2 = au(name);
					lvo->name = name2;
					lvo->aname = strdup(name);
					lvo->id = lvoid++;
				}
				p += len;
			}
			break;
			case 0x3e8: // HUNK_NAME
			{
				len = gl(p) * 4;
				p += 4;
				uae_char *name = gethunktext(p, namebuf, len);
				p += len;
			}
			break;
			case 0x3e9: // HUNK_CODE
			case 0x3ea: // HUNK_DATA
			{
				len = gl(p) * 4;
				p += 4 + len;
			}
			break;
			case 0x3eb: // HUNK_BSS
			{
				p += 4;
			}
			break;
			case 0x3f2: // HUNK_END
			break;
			case 0x3ef: // HUNK_EXT
			{
				for (;;) {
					len = gl(p);
					p += 4;
					if (!len)
						break;
					uae_u8 type = len >> 24;
					len &= 0xffffff;
					len *= 4;
					if (type == 2) {
						uae_u8 *p2 = p;
						p += len;
						uae_u32 value = gl(p);
						p += 4;
						if (lvo) {
							uae_char *name = gethunktext(p2, namebuf, len);
							if (!strncmp(name, "_LVO", 4)) {
								TCHAR *name2 = au(name + 4);
								struct libsymbol *ls = &libsymbols[libsymbolcnt++];
								ls->name = name2;
								ls->value = value;
								ls->lib = lvo;
							}
						}
					} else if (type & 0x80) {
						p += len;
						len = gl(p) * 4; // number of references
						p += 4;
						p += len;
					} else {
						p += len;
						p += 4;
					}
				}
			}
			break;
			case 0x3ec: // HUNK_RELOC
			{
				for (;;) {
					int reloccnt = gl(p);
					p += 4;
					if (!reloccnt)
						break;
					p += (1 + reloccnt) * 4;
				}
			}
			break;
			case 0x3f1: // HUNK_DEBUG
			{
				len = gl(p) * 4;
				p += 4;
				p += len;
			}
			break;
			default:
			console_out_f(_T("Unknown hunk %08x\n"), hunk);
			goto end;
		}
	}
end:
	if (ret)
		console_out_f(_T("Loaded '%s', %ld libraries, %ld LVOs.\n"), name, libnamecnt, libsymbolcnt);
	xfree(file);
	return ret;
}

bool debugger_load_libraries(void)
{
	debugger_load_library(_T("amiga.lib"));
	debugger_load_fd();
	return true;
}


void debugmem_addsegs(TrapContext *ctx, uaecptr seg, uaecptr name, uae_u32 lock)
{
	uae_u8 *file = NULL;
	int filelen;
	bool hasfile = false;
	int fileoffset = 0;
	int segmentid;

	if (!debugmem_initialized)
		debugmem_init();
	if (!seg || !debugmem_initialized)
		return;
	seg *= 4;
	uaecptr seg2 = seg;
	uaecptr resident = 0;
	while (seg) {
		uaecptr next = get_long(seg) * 4;
		uaecptr len = get_long(seg - 4) * 4;
		for (int i = 0; i < len - 26; i += 2) {
			uae_u16 w = get_word(seg + 4 + i);
			if (w == 0x4afc) {
				uae_u32 l = get_long(seg + 4 + i + 2);
				if (l == seg + 4 + i) {
					resident = seg + 4 + i;
					seg = 0;
					break;
				}
			}
		}
		seg = next;
	}
	if (!resident)
		return;
	console_out_f(_T("Adding segment %08x, Resident %08x.\n"), seg2, resident);
	struct debugsegtracker *sg = NULL;
	for (segmentid = 0; segmentid < MAX_DEBUGSEGS; segmentid++) {
		if (!dsegt[segmentid]->allocid) {
			sg = dsegt[segmentid];
			if (segmentid >= segtrackermax)
				segtrackermax = segmentid + 1;
			break;
		}
	}
	if (!sg)
		return;
	sg->resident = resident;
	if (name) {
		uae_char aname[256];
		TCHAR nativepath[MAX_DPATH];
		nativepath[0] = 0;
		strcpyah_safe(aname, name, sizeof aname);
		sg->name = au(aname);
		if (lock && !get_native_path(ctx, lock, nativepath)) {
			struct zfile *zf = zfile_fopen(nativepath, _T("rb"));
			if (zf) {
				file = zfile_getdata(zf, 0, -1, &filelen);
				zfile_fclose(zf);

			}
			while (file) {
				if (gl(file) != 0x03f3)
					break;
				if (gl(file + 4) != 0x0)
					break;
				int hunks = gl(file + 8);
				hasfile = true;
				fileoffset = 5 * 4 + hunks * 4;
				break;
			}
		}
		console_out_f(_T("Name '%s', native path '%s'\n"), sg->name, nativepath[0] ? nativepath : _T("<n/a>"));
	} else {
		sg->name = my_strdup(_T("<unknown>"));
	}
	int parentid = 0;
	int cnt = 1;
	seg = seg2;
	while (seg) {
		uaecptr next = get_long(seg) * 4;
		uaecptr len = get_long(seg - 4) * 4;
		struct debugmemallocs *dm = debugmem_reserve(seg, len, parentid);
		if (!parentid) {
			sg->allocid = dm->id;
			parentid = dm->id;
			dm->parentid = dm->id;
		}
		if (hasfile) {
			dm->idtype = gl(&file[fileoffset]);
			fileoffset += 4;
			int hunklen = gl(&file[fileoffset]);
			fileoffset += 4;
			if (dm->idtype == 0x3e9 || dm->idtype == 0x3ea) {
				fileoffset += hunklen * 4;
			}
			for (;;) {
				uae_u32 hunktype = gl(&file[fileoffset]);
				if (hunktype == 0x3e9 || hunktype == 0x3ea || hunktype == 0x3eb)
					break;
				if (hunktype == 0x3ec) {
					fileoffset += 4;
					for (;;) {
						int reloccnt = gl(&file[fileoffset]);
						fileoffset += 4;
						if (!reloccnt)
							break;
						fileoffset += (1 + reloccnt) * 4;
					}
				} else if (hunktype == 0x3f0) {
					int symcnt = 0;
					fileoffset += 4;
					for (;;) {
						int size = gl(&file[fileoffset]);
						fileoffset += 4;
						if (!size)
							break;
						struct debugsymbol *ds = symbols[symbolcnt++];
						ds->name = au((char*)&file[fileoffset]);
						fileoffset += 4 * size;
						ds->value = gl(&file[fileoffset]) + seg + 4;
						ds->segment = segmentid;
						ds->section = dm;
						fileoffset += 4;
						symcnt++;
					}
					console_out_f(_T("%d symbols loaded.\n"), symcnt);
				} else if (hunktype == 0x3f1) {
					fileoffset += 4;
					int debugsize = gl(&file[fileoffset]);
					fileoffset += 4;
					fileoffset += debugsize * 4;
				} else if (hunktype == 0x3f2) {
					fileoffset += 4;
				} else {
					break;
				}
				if (fileoffset >= filelen)
					break;
			}
		}
		dm->internalid = cnt++;
		debugreportalloc(dm);
		seg = next;
	}
	xfree(file);
	console_out_f(_T("Segment '%s' %08x added.\n"), sg->name, seg2);
}

void debugmem_remsegs(uaecptr seg)
{
	int parentid = 0;
	if (!seg || !debugmem_initialized)
		return;
	seg *= 4;
	uaecptr seg2 = seg;
	while (seg) {
		uaecptr next = get_long(seg) * 4;
		uaecptr len = get_long(seg - 4) * 4;
		parentid = debugmem_unreserve(seg, len, true);
		if (!parentid) {
			if (seg2 == seg)
				return;
			debugmem_break(5);
			return;
		}
		seg = next;
	}
	console_out_f(_T("Freeing segment %08x...\n"), seg);
	struct debugmemallocs *nextavail = NULL;
	for (int i = 0; i < MAX_DEBUGMEMALLOCS; i++) {
		struct debugmemallocs *alloc = allocs[i];
		if (alloc->parentid == parentid) {
			console_out_f(_T("Segment not freed: "));
			debugreportalloc(alloc);
			nextavail = alloc;
		}
	}
	struct debugsegtracker *sg = NULL;
	for (int i = 0; i < MAX_DEBUGSEGS; i++) {
		if (dsegt[i]->allocid == parentid) {
			sg = dsegt[i];
			if (nextavail)
				sg->allocid = nextavail->id;
			break;
		}
	}
	if (!sg) {
		return;
	}
	if (!nextavail) {
		console_out_f(_T("Segment '%s' %08x freed.\n"), sg->name, seg2);
		xfree(sg->name);
		memset(sg, 0, sizeof(struct debugsegtracker));
	} else {
		console_out_f(_T("Segment '%s' %08x partially freed.\n"), sg->name, seg2);
	}
}

static void allocate_stackframebuffers(void)
{
	if (!stackframes) {
		stackframes = xcalloc(struct debugstackframe, MAX_STACKFRAMES);
		stackframessuper = xcalloc(struct debugstackframe, MAX_STACKFRAMES);
	}
	stackframecnt = 0;
	stackframecntsuper = 0;
}

void debugmem_init(void)
{
	debug_waiting = false;
	if (!debugmem_bank.baseaddr) {
		int size = 0x10000000;
		for (uae_u32 mem = 0x70000000; mem < 0xf0000000; mem += size) {
			if (get_mem_bank_real(mem) == &dummy_bank && get_mem_bank_real(mem + size - 65536) == &dummy_bank) {
				debugmem_bank.reserved_size = size;
				debugmem_bank.mask = debugmem_bank.reserved_size - 1;
				debugmem_bank.start = mem;
				if (!mapped_malloc(&debugmem_bank)) {
					console_out_f(_T("Failed to automatically allocate debugmem (mapped_malloc)!\n"));
					return;
				}
				map_banks(&debugmem_bank, debugmem_bank.start >> 16, debugmem_bank.allocated_size >> 16, 0);
				console_out_f(_T("Automatically allocated debugmem location: %08x - %08x %08x\n"),
					debugmem_bank.start, debugmem_bank.start + debugmem_bank.allocated_size - 1, debugmem_bank.allocated_size);
				break;
			}
		}
		if (!debugmem_bank.baseaddr) {
			console_out_f(_T("Failed to automatically allocate debugmem (no space)!\n"));
			return;
		}
	}
	memset(debugmem_bank.baseaddr, 0xa5, debugmem_bank.allocated_size);
	if (!debugmem_func_lgeti) {

		debugmem_func_lgeti = debugmem_bank.lgeti;
		debugmem_func_wgeti = debugmem_bank.wgeti;
		debugmem_func_lget = debugmem_bank.lget;
		debugmem_func_wget = debugmem_bank.wget;
		debugmem_func_bget = debugmem_bank.bget;
		debugmem_func_lput = debugmem_bank.lput;
		debugmem_func_wput = debugmem_bank.wput;
		debugmem_func_bput = debugmem_bank.bput;
		debugmem_func_xlate = debugmem_bank.xlateaddr;

		debugmem_bank.lgeti = debugmem_lgeti;
		debugmem_bank.wgeti = debugmem_wgeti;
		debugmem_bank.lget = debugmem_lget;
		debugmem_bank.wget = debugmem_wget;
		debugmem_bank.bget = debugmem_bget;
		debugmem_bank.lput = debugmem_lput;
		debugmem_bank.wput = debugmem_wput;
		debugmem_bank.bput = debugmem_bput;
		debugmem_bank.xlateaddr = debugmem_xlate;
	}
	alloccnt = 0;
	if (!allocs) {
		allocs = xcalloc(struct debugmemallocs*, MAX_DEBUGMEMALLOCS);
		struct debugmemallocs *a = xcalloc(struct debugmemallocs, MAX_DEBUGMEMALLOCS);
		for (int i = 0; i < MAX_DEBUGMEMALLOCS; i++) {
			allocs[i] = a + i;
		}
	}
	if (dmd) {
		xfree(dmd[0]);
		xfree(dmd);
	}
	totalmemdata = debugmem_bank.allocated_size / PAGE_SIZE;
	dmd = xcalloc(struct debugmemdata*, totalmemdata);
	struct debugmemdata *d = xcalloc(struct debugmemdata, totalmemdata);
	for (int i = 0; i < totalmemdata; i++) {
		dmd[i] = d + i;
	}
	debugmemptr = 0;
	if (!dsegt) {
		dsegt = xcalloc(struct debugsegtracker*, MAX_DEBUGSEGS);
		struct debugsegtracker *sg = xcalloc(struct debugsegtracker, MAX_DEBUGSEGS);
		for (int i = 0; i < MAX_DEBUGSEGS; i++) {
			dsegt[i] = sg + i;
		}
	}
	if (!symbols) {
		symbols = xcalloc(struct debugsymbol*, MAX_DEBUGSYMS);
		struct debugsymbol *sg = xcalloc(struct debugsymbol, MAX_DEBUGSYMS);
		for (int i = 0; i < MAX_DEBUGSYMS; i++) {
			symbols[i] = sg + i;
		}
	}
	if (!stackvars) {
		stackvars = xcalloc(struct stackvar*, MAX_STACKVARS);
		struct stackvar *sg = xcalloc(struct stackvar, MAX_STACKVARS);
		for (int i = 0; i < MAX_STACKVARS; i++) {
			stackvars[i] = sg + i;
		}
	}
	if (!codefiles) {
		codefiles = xcalloc(struct debugcodefile*, MAX_DEBUGCODEFILES);
	}
	for (int i = 0; i < MAX_DEBUGMEMALLOCS; i++) {
		memset(allocs[i], 0, sizeof(struct debugmemallocs));
	}
	for (int i = 0; i < totalmemdata; i++) {
		memset(dmd[i], 0, sizeof(struct debugmemdata));
	}
	for (int i = 0; i < MAX_DEBUGSEGS; i++) {
		xfree(dsegt[i]->name);
		memset(dsegt[i], 0, sizeof(struct debugsegtracker));
	}
	for (int i = 0; i < MAX_DEBUGSYMS; i++) {
		xfree(symbols[i]->name);
		memset(symbols[i], 0, sizeof(struct debugsymbol));
	}
	for (int i = 0; i < MAX_STACKVARS; i++) {
		xfree(stackvars[i]->name);
		memset(stackvars[i], 0, sizeof(struct stackvar));
	}
	for (int i = 0; i < codefilecnt; i++) {
		struct debugcodefile *cf = codefiles[i];
		xfree(cf->lineptr);
		xfree(cf->data);
		xfree(cf->stabtypes);
		memset(cf, 0, sizeof(struct debugcodefile));
	}
	xfree(stabs);
	stabs = NULL;
	allocate_stackframebuffers();

	debugmem_active = false;
	stabscount = 0;
	linemapsize = 0;
	codefilecnt = 0;
	symbolcnt = 0;
	executable_last_segment = 0;
	segtrackermax = 0;
	debugmem_initialized = true;
	debugmem_chiplimit = 0x400;
	debugstack_word_state = 0;

	debugger_load_libraries();
}

void debugmem_disable(void)
{
	if (!debugmem_initialized)
		return;
	debugmem_bank.lgeti = debugmem_func_lgeti;
	debugmem_bank.wgeti = debugmem_func_wgeti;
	debugmem_bank.lget = debugmem_func_lget;
	debugmem_bank.wget = debugmem_func_wget;
	debugmem_bank.bget = debugmem_func_bget;
	debugmem_bank.lput = debugmem_func_lput;
	debugmem_bank.wput = debugmem_func_wput;
	debugmem_bank.bput = debugmem_func_bput;
	debugmem_bank.xlateaddr = debugmem_func_xlate;
	debugmem_mapped = false;
}
void debugmem_enable(void)
{
	if (!debugmem_initialized)
		return;
	debugmem_bank.lgeti = debugmem_lgeti;
	debugmem_bank.wgeti = debugmem_wgeti;
	debugmem_bank.lget = debugmem_lget;
	debugmem_bank.wget = debugmem_wget;
	debugmem_bank.bget = debugmem_bget;
	debugmem_bank.lput = debugmem_lput;
	debugmem_bank.wput = debugmem_wput;
	debugmem_bank.bput = debugmem_bput;
	debugmem_bank.xlateaddr = debugmem_xlate;
	debugmem_mapped = true;
}

static struct debugsegtracker *findsegment(uaecptr addr, struct debugmemallocs **allocp)
{
	for (int i = 0; i < segtrackermax; i++) {
		struct debugsegtracker *seg = dsegt[i];
		if (!seg->allocid)
			continue;
		struct debugmemallocs *alloc = allocs[seg->allocid];
		if (addr >= alloc->start && addr < alloc->start + alloc->size) {
			if (allocp)
				*allocp = alloc;
			return seg;
		}
	}
	return NULL;
}

static struct debugmemallocs *ismysegment(uaecptr addr)
{
	if (addr < debugmem_bank.start)
		return NULL;
	addr -= debugmem_bank.start;
	if (addr >= debugmem_bank.allocated_size)
		return false;
	for (int i = 1; i <= executable_last_segment; i++) {
		struct debugmemallocs *alloc = allocs[i];
		if (addr >= alloc->start && addr < alloc->start + alloc->size)
			return alloc;
	}
	return NULL;
}

bool debugmem_get_symbol_value(const TCHAR *name, uae_u32 *valp)
{
	for (int i = 0; i < symbolcnt; i++) {
		struct debugsymbol *ds = symbols[i];
		if (!_tcscmp(ds->name, name)) {
			*valp = ds->value;
			return true;
		}
	}
	for (int i = 0; i < symbolcnt; i++) {
		struct debugsymbol *ds = symbols[i];
		if (!_tcsicmp(ds->name, name)) {
			*valp = ds->value;
			return true;
		}
	}
	return false;
}

int debugmem_get_symbol(uaecptr addr, TCHAR *out, int maxsize)
{
	if (out)
		out[0] = 0;
	int found = 0;
	for (int i = 0; i < symbolcnt; i++) {
		struct debugsymbol *ds = symbols[i];
		if (ds->value == addr) {
			if (out) {
				TCHAR txt[256];
				_tcscpy(txt, ds->name);

				if ((ds->type == SYMBOLTYPE_FUNC) || !(ds->flags & SYMBOL_GLOBAL)) {
					_tcscat(txt, _T(" ("));
					if (!(ds->flags & SYMBOL_GLOBAL))
						_tcscat(txt, _T("L"));
					if (ds->type == SYMBOLTYPE_FUNC)
						_tcscat(txt, _T("F"));
					_tcscat(txt, _T(")"));
				}

#if 0
				if ((ds->flags & SYMBOL_FUNC) && ds->data) {
					bool first = true;
					struct stackvar *sv = (struct stackvar*)ds->data;
					TCHAR *p = txt + _tcslen(txt);
					*p++ = '\n';
					*p++ = '-';
					*p = 0;
					int size = sizeof(txt) / sizeof(TCHAR) - _tcslen(txt);
					while (sv->func == ds) {
						TCHAR txt2[256];
						TCHAR *p2 = txt2;
						if (!first)
							*p2++ = ',';
						first = false;
						_stprintf(p2, _T("%s:+%05X"), sv->name, sv->offset);
						if (size <= _tcslen(txt2))
							break;
						size -= _tcslen(txt2);
						_tcscat(txt, txt2);
						p += _tcslen(txt);
						sv++;
					}
					*p = 0;
				}
#endif

				if (maxsize > _tcslen(txt)) {
					if (found)
						_tcscat(out, _T("\n"));
					_tcscat(out, txt);
					maxsize -= _tcslen(txt);
				}
			}
			found = i + 1;
		}
	}
	return found;
}

struct debugcodefile *last_codefile;

int debugmem_get_sourceline(uaecptr addr, TCHAR *out, int maxsize)
{
	if (addr < debugmem_bank.start)
		return -1;
	addr -= debugmem_bank.start;
	if (addr >= debugmem_bank.allocated_size)
		return -1;
	if (out)
		out[0] = 0;
	for (int i = 1; i <= executable_last_segment; i++) {
		struct debugmemallocs *alloc = allocs[i];
		if (addr >= alloc->start && addr < alloc->start + alloc->size) {
			int offset = addr - alloc->start + alloc->relative_start - 8;
			if (offset < 0)
				return -1;
			struct linemapping *lm = &linemap[offset];
			int line = lm->line;
			if (line < 0)
				return -1;
			if (!out)
				return line;
			int lastline = line;
			for (;;) {
				offset++;
				if (offset >= linemapsize)
					break;
				struct linemapping *lm2 = &linemap[offset];
				if (!lm2->file)
					continue;
				if (lm2->file != lm->file)
					break;
				if (lm2->line <= 0)
					continue;
				lastline = lm2->line;
				break;
			}
			struct debugcodefile *cf = lm->file;
			if (cf->lineptr[line] && cf->lineptr[line][0]) {
				if (last_codefile != cf) {
					TCHAR txt[256];
					last_codefile = cf;
					_stprintf(txt, _T("Source file: %s\n"), cf->name);
					if (maxsize > _tcslen(txt)) {
						_tcscat(out, txt);
						maxsize -= _tcslen(txt);
					}
				}
				if (lastline - line > 10)
					lastline = line + 10;
				for (int j = line; j < lastline; j++) {
					TCHAR txt[256];
					TCHAR *s = au((uae_char*)cf->lineptr[j]);
					if (maxsize > 6 + _tcslen(s) + 2) {
						_stprintf(txt, _T("%5d %s\n"), j, s);
						_tcscat(out, txt);
						maxsize -= _tcslen(txt) + 2;
					}
					xfree(s);
				}
				return line;
			}
			return -1;
		}
	}
	return -1;
}

int debugmem_get_segment(uaecptr addr, bool *exact, bool *ext, TCHAR *out, TCHAR *name)
{
	if (out)
		out[0] = 0;
	if (name)
		name[0] = 0;
	if (exact)
		*exact = false;
	struct debugmemallocs *alloc = ismysegment(addr);
	if (alloc) {
		if (exact && alloc->start + 8 + debugmem_bank.start == addr)
			*exact = true;
		if (out)
			_stprintf(out, _T("[%06X]"), ((addr - debugmem_bank.start) - (alloc->start + 8)) & 0xffffff);
		if (name)
			_stprintf(name, _T("Segment %d: %08x %08x-%08x"),
				alloc->id, alloc->idtype, alloc->start + debugmem_bank.start, alloc->start + alloc->size - 1 + debugmem_bank.start);
		if (ext)
			*ext = false;
		return alloc->id;
	} else {
		struct debugmemallocs *alloc;
		struct debugsegtracker *seg = findsegment(addr, &alloc);
		if (seg) {
			if (out)
				_stprintf(out, _T("[%06X]"), ((addr - debugmem_bank.start) - (alloc->start + 8)) & 0xffffff);
			if (name)
				_stprintf(name, _T("Segment %d ('%s') %08x %08x-%08x"),
					alloc->internalid, seg->name, alloc->idtype, alloc->start, alloc->start + alloc->size - 1);
			if (ext)
				*ext = true;
			return alloc->id;
		}
	}
	return 0;
}

bool debugmem_list_segment(int mode, uaecptr addr)
{
	if (mode) {
		for (int i = 0; i < segtrackermax; i++) {
			struct debugsegtracker *seg = dsegt[i];
			if (!seg->allocid)
				continue;
			console_out_f(_T("Load module '%s':\n"), seg->name);
			debugreportsegment(seg, true);
		}
	} else {
		if (addr == 0xffffffff) {
			if (!executable_last_segment) {
				console_out(_T("No executable loaded\n"));
				return false;
			}
			for (int i = 1; i <= executable_last_segment; i++) {
				debugreportalloc(allocs[i]);
			}
			return true;
		} else {
			struct debugsegtracker *seg = findsegment(addr, NULL);
			if (seg) {
				debugreportsegment(seg, false);
				return true;
			}
			if (ismysegment(addr)) {
				for (int i = 1; i <= executable_last_segment; i++) {
					debugreportalloc(allocs[i]);
				}
				return true;
			}
		}
	}
	return false;
}

bool debugmem_get_range(uaecptr *start, uaecptr *end)
{
	if (!executable_last_segment)
		return false;
	*start = allocs[1]->start + debugmem_bank.start;
	*end = allocs[executable_last_segment - 1]->start + allocs[executable_last_segment - 1]->size + debugmem_bank.start;
	return true;
}

bool debugmem_isactive(void)
{
	return executable_last_segment != 0;
}

uae_u32 debugmem_chiphit(uaecptr addr, uae_u32 v, int size)
{
	static int recursive;
	if (recursive) {
		return 0xdeadf00d;
	}
	recursive++;
	bool dbg = false;
	if (size > 0) {
		if (debugmem_active) {
			console_out_f(_T("%s write to %08x, value = %08x\n"), size == 4 ? _T("Long") : (size == 2 ? _T("Word") : _T("Byte")), addr, v);
			dbg = debugmem_break(6);
		}
	} else {
		size = -size;
		// execbase?
		if (size == 4 && addr == 4) {
			recursive--;
			return do_get_mem_long((uae_u32*)(chipmem_bank.baseaddr + 4));
		}
		// exception vectors
		if (regs.vbr < 0x100) {
			// vbr == 0 so skip aligned long reads
			if (size == 4 && addr >= regs.vbr + 8 && addr < regs.vbr + 0x100) {
				recursive--;
				return do_get_mem_long((uae_u32*)(chipmem_bank.baseaddr + addr));
			}
		}
		if (debugmem_active) {
			console_out_f(_T("%s read from %08x\n"), size == 4 ? _T("Long") : (size == 2 ? _T("Word") : _T("Byte")), addr);
			dbg = debugmem_break(7);
		}
	}
	if (debugmem_active) {
		if (!dbg)
			m68k_dumpstate(0, 0xffffffff);
	}
	recursive--;
	return 0xdeadf00d;
}

bool debugmem_extinvalidmem(uaecptr addr, uae_u32 v, int size)
{
	static int recursive;
	bool dbg = false;
	if (!debugmem_bank.baseaddr || !debugmem_active)
		return false;
	if (recursive) {
		return true;
	}
	recursive++;
	if (size > 0) {
		console_out_f(_T("%s write to %08x, value = %08x\n"), size == 4 ? _T("Long") : (size == 2 ? _T("Word") : _T("Byte")), addr, v);
		dbg = debugmem_break(8);
	} else {
		size = -size;
		console_out_f(_T("%s read from %08x\n"), size == 4 ? _T("Long") : (size == 2 ? _T("Word") : _T("Byte")), addr, v);
		dbg = debugmem_break(9);
	}
	if (!dbg)
		m68k_dumpstate(0, 0xffffffff);
	recursive--;
	return true;
}

bool debugmem_enable_stackframe(bool enable)
{
	if (enable && !stackframemode) {
		stackframemode = 1;
		console_out(_T("Full stack frame tracking enabled.\n"));
		allocate_stackframebuffers();
		return true;
	} else if (!enable && stackframemode) {
		stackframemode = 0;
		console_out(_T("Full stack frame tracking disabled.\n"));
		return true;
	}
	return false;
}

bool debugmem_list_stackframe(bool super)
{
	if (!debugmem_bank.baseaddr && !stackframemode) {
		return false;
	}
	int cnt = super ? stackframecntsuper : stackframecnt;
	if (!cnt)
		return false;
	for (int i = 0; i < cnt; i++) {
		struct debugstackframe *sf = super ? &stackframessuper[i] : &stackframes[i];
		console_out_f(_T("%08x -> %08x SP=%08x"), sf->current_pc, sf->branch_pc, sf->stack);
		if (sf->sr & 0x2000)
			console_out_f(_T(" SR=%04x"), sf->sr);
		TCHAR txt1[256], txt2[256];
		if (debugmem_get_segment(sf->branch_pc, NULL, NULL, txt1, txt2)) {
			console_out_f(_T(" %s %s"), txt1, txt2);
		}
		if (debugmem_get_symbol(sf->branch_pc, txt1, sizeof(txt1) / sizeof(TCHAR))) {
			console_out_f(_T(" %s"), txt1);
		}
		console_out_f(_T("\n"));
		uae_u32 sregs[16];
		memcpy(sregs, regs.regs, sizeof(uae_u32) * 16);
		memcpy(regs.regs, sf->regs, sizeof(uae_u32) * 16);
		m68k_disasm(sf->current_pc, NULL, 0xffffffff, 2);
		memcpy(regs.regs, sregs, sizeof(uae_u32) * 16);
		console_out_f(_T("\n"));
	}
	return true;
}