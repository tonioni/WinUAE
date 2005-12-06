 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Debugger
  *
  * (c) 1995 Bernd Schmidt
  *
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include <ctype.h>
#include <signal.h>

#include "config.h"
#include "options.h"
#include "threaddep/thread.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "cpu_prefetch.h"
#include "debug.h"
#include "cia.h"
#include "xwin.h"
#include "gui.h"
#include "identify.h"
#include "sound.h"
#include "disk.h"
#include "savestate.h"
#include "autoconf.h"
#include "akiko.h"
#include "inputdevice.h"

static int debugger_active;
static uaecptr skipaddr_start, skipaddr_end;
static int skipaddr_doskip;
static uae_u32 skipins;
static int do_skip;
static int debug_rewind;
static int memwatch_enabled, memwatch_triggered;
int debugging;
int exception_debugging;
int debug_copper;
static uaecptr debug_copper_pc;

extern int audio_channel_mask;

static FILE *logfile;

void activate_debugger (void)
{
    if (logfile)
	fclose (logfile);
    logfile = 0;
    do_skip = 0;
    if (debugger_active)
	return;
    debugger_active = 1;
    set_special (SPCFLAG_BRK);
    debugging = 1;
}

int firsthist = 0;
int lasthist = 0;
static struct regstruct history[MAX_HIST];
static struct flag_struct historyf[MAX_HIST];

static char help[] = {
    "          HELP for UAE Debugger\n"
    "         -----------------------\n\n"
    "  g [<address>]         Start execution at the current address or <address>\n"
    "  c                     Dump state of the CIA, disk drives and custom registers\n"
    "  r                     Dump state of the CPU\n"
    "  m <address> [<lines>] Memory dump starting at <address>\n"
    "  d <address> [<lines>] Disassembly starting at <address>\n"
    "  t [instructions]      Step one or more instructions\n"
    "  z                     Step through one instruction - useful for JSR, DBRA etc\n"
    "  f                     Step forward until PC in RAM (\"boot block finder\")\n"
    "  f <address>           Add/remove breakpoint\n"
    "  fi                    Step forward until PC points to RTS/RTD or RTE\n"
    "  fi <opcode>           Step forward until PC points to <opcode>\n"
    "  fl                    List breakpoints\n"
    "  fd                    Remove all breakpoints\n"
    "  f <addr1> <addr2>     Step forward until <addr1> <= PC <= <addr2>\n"
    "  e [<addr>]            Dump contents of all custom registers\n"
    "  i [<addr>]            Dump contents of interrupt and trap vectors\n"
    "  o <0-2|addr> [<lines>]View memory as Copper instructions\n"
    "  od                    Enable/disable Copper vpos/hpos tracing\n"
    "  ot                    Copper single step trace\n"
    "  ob <addr>             Copper breakpoint\n"
    "  O                     Display bitplane offsets\n"
    "  O <plane> <offset>    Offset a bitplane\n"
    "  H[H] <cnt>            Show PC history (HH=full CPU info) <cnt> instructions\n"
    "  C <value>             Search for values like energy or lifes in games\n"
    "  W <address> <value>   Write into Amiga memory\n"
    "  w <num> <address> <length> <R/W/RW> [<value>]\n"
    "                        Add/remove memory watchpoints\n"
    "  wd                    Enable illegal access logger\n"
    "  S <file> <addr> <n>   Save a block of Amiga memory\n"
    "  s <string>/<values> [<addr>] [<length>]\n"
    "                        Search for string/bytes\n"
    "  T                     Show exec tasks and their PCs\n"
    "  h,?                   Show this help page\n"
    "  b                     Step to previous state capture position\n"
    "  am <channel mask>     Enable or disable audio channels\n"
    "  di <mode> [<track>]   Break on disk access. R=DMA read,W=write,RW=both,P=PIO\n"
    "                        Also enables extended disk logging\n"
    "  q                     Quit the emulator. You don't want to use this command.\n\n"
};

static void debug_help (void)
{
    console_out (help);
}



static void ignore_ws (char **c)
{
    while (**c && isspace(**c)) (*c)++;
}

static uae_u32 readint (char **c);
static uae_u32 readhex (char **c)
{
    uae_u32 val = 0;
    char nc;

    ignore_ws (c);
    if (**c == '!' || **c == '_') {
	(*c)++;
	return readint (c);
    }
    while (isxdigit(nc = **c)) {
	(*c)++;
	val *= 16;
	nc = toupper(nc);
	if (isdigit(nc)) {
	    val += nc - '0';
	} else {
	    val += nc - 'A' + 10;
	}
    }
    return val;
}

static uae_u32 readint (char **c)
{
    uae_u32 val = 0;
    char nc;
    int negative = 0;

    ignore_ws (c);
    if (**c == '$') {
	(*c)++;
	return readhex (c);
    }
    if (**c == '0' && toupper((*c)[1]) == 'X') {
	(*c)+= 2;
	return readhex (c);
    }
    if (**c == '-')
	negative = 1, (*c)++;
    while (isdigit(nc = **c)) {
	(*c)++;
	val *= 10;
	val += nc - '0';
    }
    return val * (negative ? -1 : 1);
}

static char next_char(char **c)
{
    ignore_ws (c);
    return *(*c)++;
}

static char peek_next_char(char **c)
{
    char *pc = *c;
    return pc[1];
}

static int more_params (char **c)
{
    ignore_ws (c);
    return (**c) != 0;
}

static int next_string (char **c, char *out, int max, int forceupper)
{
    char *p = out;

    *p = 0;
    while (**c != 0) {
	if (**c == 32) {
	    ignore_ws (c);
	    return strlen (out);
	}
	*p = next_char (c);
	if (forceupper)
	    *p = toupper(*p);
	*++p = 0;
	max--;
	if (max <= 1)
	    break;
    }
    return strlen (out);
}

static uae_u32 nextaddr (uae_u32 addr)
{
    if (addr == 0xffffffff) {
	if (currprefs.bogomem_size)
	    return 0xc00000 + currprefs.bogomem_size;
	if (currprefs.fastmem_size)
	    return 0x200000 + currprefs.fastmem_size;
	return currprefs.chipmem_size;
    }
    if (addr == currprefs.chipmem_size) {
	if (currprefs.fastmem_size)
	    return 0x200000;
	else if (currprefs.bogomem_size)
	    return 0xc00000;
	return 0xffffffff;
    }
    if (addr == 0x200000 + currprefs.fastmem_size) {
	if (currprefs.bogomem_size)
	    return 0xc00000;
	return 0xffffffff;
    }
    if (addr == 0xc00000 + currprefs.bogomem_size)
	return 0xffffffff;
    return addr + 1;
}

static void dumpmem (uaecptr addr, uaecptr *nxmem, int lines)
{
    char line[80];
    int cols = 8;
    for (;lines--;) {
	int i;
	sprintf (line, "%08lx ", addr);
	for (i = 0; i < cols; i++) {
	    uae_u8 b1 = get_byte (addr + 0);
	    uae_u8 b2 = get_byte (addr + 1);
	    addr += 2;
	    sprintf (line + 9 + i * 5, "%02x%02x ", b1, b2);
	    line[9 + cols * 5 + 1 + i * 2 + 0] = b1 >= 32 && b1 < 127 ? b1 : '.';
	    line[9 + cols * 5 + 1 + i * 2 + 1] = b2 >= 32 && b2 < 127 ? b2 : '.';
	}
	line[9 + cols * 5] = ' ';
	line[9 + cols * 5 + 1 + 2 * cols] = 0;
	console_out ("%s", line);
	console_out ("\n");
    }
    *nxmem = addr;
}

static void dump_custom_regs (void)
{
    int len, i, j, end;
    uae_u8 *p1, *p2, *p3, *p4;

    p1 = p2 = save_custom (&len, 0, 1);
    p1 += 4; // skip chipset type
    for (i = 0; i < 4; i++) {
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
    end = 0;
    while (custd[end].name)
	end++;
    end++;
    end /= 2;
    for (i = 0; i < end; i++) {
	uae_u16 v1, v2;
	int addr1, addr2;
	j = end + i;
	addr1 = custd[i].adr & 0x1ff;
	addr2 = custd[j].adr & 0x1ff;
	v1 = (p1[addr1 + 0] << 8) | p1[addr1 + 1];
	v2 = (p1[addr2 + 0] << 8) | p1[addr2 + 1];
	console_out ("%03.3X %s\t%04.4X\t%03.3X %s\t%04.4X\n",
	    addr1, custd[i].name, v1,
	    addr2, custd[j].name, v2);
    }
    free (p2);
}

static void dump_vectors (uaecptr addr)
{
    int i = 0, j = 0;
    
    if (addr == 0xffffffff)
	addr = regs.vbr;

    while (int_labels[i].name || trap_labels[j].name) {
	if (int_labels[i].name) {
	    console_out ("$%08X: %s  \t $%08X\t", int_labels[i].adr + addr,
		int_labels[i].name, get_long (int_labels[i].adr + (int_labels[i].adr == 4 ? 0 : addr)));
	    i++;
	} else {
	    console_out ("\t\t\t\t");
	}
	if (trap_labels[j].name) {
	    console_out("$%08X: %s  \t $%08X", trap_labels[j].adr + addr,
	       trap_labels[j].name, get_long (trap_labels[j].adr + addr));
	    j++;
	}
	console_out ("\n");
    }
}

static void disassemble_wait (FILE *file, unsigned long insn)
{
    int vp, hp, ve, he, bfd, v_mask, h_mask;

    vp = (insn & 0xff000000) >> 24;
    hp = (insn & 0x00fe0000) >> 16;
    ve = (insn & 0x00007f00) >> 8;
    he = (insn & 0x000000fe);
    bfd = (insn & 0x00008000) >> 15;

    /* bit15 can never be masked out*/
    v_mask = vp & (ve | 0x80);
    h_mask = hp & he;
    if (v_mask > 0) {
	console_out ("vpos ");
	if (ve != 0x7f) {
	    console_out ("& 0x%02x ", ve);
	}
	console_out (">= 0x%02x", v_mask);
    }
    if (he > 0) {
	if (v_mask > 0) {
	    console_out (" and");
	}
	console_out (" hpos ");
	if (he != 0xfe) {
	    console_out ("& 0x%02x ", he);
	}
	console_out (">= 0x%02x", h_mask);
    } else {
	console_out (", ignore horizontal");
    }

    console_out (".\n                        \t; VP %02x, VE %02x; HP %02x, HE %02x; BFD %d\n",
	     vp, ve, hp, he, bfd);
}

#define NR_COPPER_RECORDS 40000
/* Record copper activity for the debugger.  */
struct cop_record
{
  int hpos, vpos;
  uaecptr addr;
};
static struct cop_record *cop_record[2];
static int nr_cop_records[2], curr_cop_set;

void record_copper_reset(void)
{
/* Start a new set of copper records.  */
    curr_cop_set ^= 1;
    nr_cop_records[curr_cop_set] = 0;
}

void record_copper (uaecptr addr, int hpos, int vpos)
{
    int t = nr_cop_records[curr_cop_set];
    if (!cop_record[0]) {
	cop_record[0] = malloc (NR_COPPER_RECORDS * sizeof (struct cop_record));
	cop_record[1] = malloc (NR_COPPER_RECORDS * sizeof (struct cop_record));
    }
    if (t < NR_COPPER_RECORDS) {
	cop_record[curr_cop_set][t].addr = addr;
	cop_record[curr_cop_set][t].hpos = hpos;
	cop_record[curr_cop_set][t].vpos = vpos;
	nr_cop_records[curr_cop_set] = t + 1;
    }
    if (debug_copper & 2) { /* trace */
	debug_copper &= ~2;
	activate_debugger();
    }
    if ((debug_copper & 4) && addr >= debug_copper_pc && addr <= debug_copper_pc + 3) {
	debug_copper &= ~4;
	activate_debugger();
    }
}

static int find_copper_record (uaecptr addr, int *phpos, int *pvpos)
{
    int s = curr_cop_set ^ 1;
    int t = nr_cop_records[s];
    int i;
    for (i = 0; i < t; i++) {
	if (cop_record[s][i].addr == addr) {
	    *phpos = cop_record[s][i].hpos;
	    *pvpos = cop_record[s][i].vpos;
	    return 1;
	}
    }
    return 0;
}

/* simple decode copper by Mark Cox */
static void decode_copper_insn (FILE* file, unsigned long insn, unsigned long addr)
{
    uae_u32 insn_type = insn & 0x00010001;
    int hpos, vpos;
    char here = ' ';
    char record[] = "          ";
    if (find_copper_record (addr, &hpos, &vpos)) {
	sprintf (record, " [%03x %03x]", vpos, hpos);
    }
    
    if (get_copper_address(-1) >= addr && get_copper_address(-1) <= addr + 3)
	here = '*';

    console_out ("%c%08lx: %04lx %04lx%s\t; ", here, addr, insn >> 16, insn & 0xFFFF, record);

    switch (insn_type) {
    case 0x00010000: /* WAIT insn */
	console_out ("Wait for ");
	disassemble_wait (file, insn);

	if (insn == 0xfffffffe)
	    console_out ("                           \t; End of Copperlist\n");

	break;

    case 0x00010001: /* SKIP insn */
	console_out ("Skip if ");
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
		console_out ("%s := 0x%04lx\n", custd[i].name, insn & 0xffff);
	    else
		console_out ("%04x := 0x%04lx\n", addr, insn & 0xffff);
	}
	break;

    default:
	abort ();
    }

}

static uaecptr decode_copperlist (FILE* file, uaecptr address, int nolines)
{
    uae_u32 insn;
    while (nolines-- > 0) {
	insn = get_long (address);
	decode_copper_insn (file, insn, address);
	address += 4;
    }
    return address;
    /* You may wonder why I don't stop this at the end of the copperlist?
     * Well, often nice things are hidden at the end and it is debatable the actual
     * values that mean the end of the copperlist */
}

static int copper_debugger (char **c)
{
    static uaecptr nxcopper;
    uae_u32 maddr;
    int lines;

    if (**c == 'd') {
	next_char(c);
	if (debug_copper)
	    debug_copper = 0;
	else
	    debug_copper = 1;
	console_out ("Copper debugger %s.\n", debug_copper ? "enabled" : "disabled");
    } else if(**c == 't') {
	debug_copper = 1|2;
	return 1;
    } else if(**c == 'b') {
	(*c)++;
	debug_copper = 1|4;
	if (more_params(c)) {
	    debug_copper_pc = readhex(c);
	    console_out ("Copper breakpoint @0x%08.8x\n", debug_copper_pc);
	} else {
	    debug_copper &= ~4;
	}
    } else {
	if (more_params(c)) {
	    maddr = readhex(c);
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

/* cheat-search by Holger Jakob */
static void cheatsearch (char **c)
{
    uae_u8 *p = get_real_address (0);
    static uae_u32 *vlist = NULL;
    uae_u32 ptr;
    uae_u32 val = 0;
    uae_u32 type = 0; /* not yet */
    uae_u32 count = 0;
    uae_u32 fcount = 0;
    uae_u32 full = 0;

    ignore_ws (c);
    val = readhex (c);
    if (vlist == NULL) {
	vlist = malloc (256*4);
	if (vlist != 0) {
	    for (count = 0; count<255; count++)
		vlist[count] = 0;
	    count = 0;
	    for (ptr = 0; ptr < allocated_chipmem - 40; ptr += 2, p += 2) {
		if (ptr >= 0x438 && p[3] == (val & 0xff)
		    && p[2] == (val >> 8 & 0xff)
		    && p[1] == (val >> 16 & 0xff)
		    && p[0] == (val >> 24 & 0xff))
		{
		    if (count < 255) {
			vlist[count++]=ptr;
			console_out ("%08x: %x%x%x%x\n",ptr,p[0],p[1],p[2],p[3]);
		    } else
			full = 1;
		}
	    }
	    console_out ("Found %d possible addresses with %d\n",count,val);
	    console_out ("Now continue with 'g' and use 'C' with a different value\n");
	}
    } else {
	for (count = 0; count<255; count++) {
	    if (p[vlist[count]+3] == (val & 0xff)
		&& p[vlist[count]+2] == (val>>8 & 0xff)
		&& p[vlist[count]+1] == (val>>16 & 0xff)
		&& p[vlist[count]] == (val>>24 & 0xff))
	    {
		fcount++;
		console_out ("%08x: %x%x%x%x\n", vlist[count], p[vlist[count]],
			p[vlist[count]+1], p[vlist[count]+2], p[vlist[count]+3]);
	    }
	}
	console_out ("%d hits of %d found\n",fcount,val);
	free (vlist);
	vlist = NULL;
    }
}

#define BREAKPOINT_TOTAL 8
struct breakpoint_node {
    uaecptr addr;
    int enabled;
};
static struct breakpoint_node bpnodes[BREAKPOINT_TOTAL];

static addrbank **debug_mem_banks;
#define MEMWATCH_TOTAL 4
struct memwatch_node {
    uaecptr addr;
    int size;
    int rw;
    uae_u32 val;
    int val_enabled;
    uae_u32 modval;
    int modval_written;
};
static struct memwatch_node mwnodes[MEMWATCH_TOTAL];
static struct memwatch_node mwhit;

static uae_u8 *illgdebug;
static int illgdebug_break;
extern int cdtv_enabled, cd32_enabled;

static void illg_init (void)
{
    int i;

    free (illgdebug);
    illgdebug = xmalloc (0x1000000);
    if (!illgdebug)
	return;
    memset (illgdebug, 3, 0x1000000);
    memset (illgdebug, 0, currprefs.chipmem_size);
    memset (illgdebug + 0xc00000, 0, currprefs.bogomem_size);
    memset (illgdebug + 0x200000, 0, currprefs.fastmem_size);
    i = 0;
    while (custd[i].name) {
	int rw = custd[i].rw;
	illgdebug[custd[i].adr] = rw;
	illgdebug[custd[i].adr + 1] = rw;
	i++;
    }
    for (i = 0; i < 16; i++) { /* CIAs */
	if (i == 11)
	    continue;
	illgdebug[0xbfe001 + i * 0x100] = 0;
	illgdebug[0xbfd000 + i * 0x100] = 0;
    }
    memset (illgdebug + 0xf80000, 1, 512 * 1024); /* KS ROM */
    memset (illgdebug + 0xdc0000, 0, 0x3f); /* clock */
#ifdef CDTV
    if (cdtv_enabled) {
	memset (illgdebug + 0xf00000, 1, 256 * 1024); /* CDTV ext ROM */
	memset (illgdebug + 0xdc8000, 0, 4096); /* CDTV batt RAM */
    }
#endif
#ifdef CD32
    if (cd32_enabled) {
	memset (illgdebug + AKIKO_BASE, 0, AKIKO_BASE_END - AKIKO_BASE);
	memset (illgdebug + 0xe00000, 1, 512 * 1024); /* CD32 ext ROM */
    }
#endif
    if (cloanto_rom)
	memset (illgdebug + 0xe00000, 1, 512 * 1024);
#ifdef FILESYS
    if (nr_units (currprefs.mountinfo) > 0) /* filesys "rom" */
	memset (illgdebug + RTAREA_BASE, 1, 0x10000);
#endif
}

/* add special custom register check here */
static void illg_debug_check (uaecptr addr, int rw, int size, uae_u32 val)
{
    return;
}

static void illg_debug_do (uaecptr addr, int rw, int size, uae_u32 val)
{
    uae_u8 mask;
    uae_u32 pc = m68k_getpc ();
    char rws = rw ? 'W' : 'R';
    int i;

    for (i = size - 1; i >= 0; i--) {
	uae_u8 v = val >> (i * 8);
	uae_u32 ad = addr + i;
	if (ad >= 0x1000000)
	    mask = 3;
	else
	    mask = illgdebug[ad];
	if (!mask)
	    continue;
	if (mask & 0x80) {
	    illg_debug_check (ad, rw, size, val);
	} else if ((mask & 3) == 3) {
	    if (rw)
		write_log ("RW: %08.8X=%02.2X %c PC=%08.8X\n", ad, v, rws, pc);
	    else
		write_log ("RW: %08.8X    %c PC=%08.8X\n", ad, rws, pc);
	    if (illgdebug_break)
		activate_debugger ();
	} else if ((mask & 1) && rw) {
	    write_log ("RO: %08.8X=%02.2X %c PC=%08.8X\n", ad, v, rws, pc);
	    if (illgdebug_break)
		activate_debugger ();
	} else if ((mask & 2) && !rw) {
	    write_log ("WO: %08.8X    %c PC=%08.8X\n", ad, rws, pc);
	    if (illgdebug_break)
		activate_debugger ();
	}
    }
}

static int debug_mem_off (uaecptr addr)
{
    return (munge24 (addr) >> 16) & 0xff;
}

static void memwatch_func (uaecptr addr, int rw, int size, uae_u32 val)
{
    int i, brk;

    if (illgdebug)
	illg_debug_do (addr, rw, size, val);
    addr = munge24 (addr);
    for (i = 0; i < MEMWATCH_TOTAL; i++) {
	uaecptr addr2 = mwnodes[i].addr;
	uaecptr addr3 = addr2 + mwnodes[i].size;
	int rw2 = mwnodes[i].rw;

	brk = 0;
	if (mwnodes[i].size == 0)
	    continue;
	if (mwnodes[i].val_enabled && mwnodes[i].val != val)
	    continue;
	if (rw != rw2 && rw2 < 2)
	    continue;
	if (addr >= addr2 && addr < addr3)
	    brk = 1;
	if (!brk && size == 2 && (addr + 1 >= addr2 && addr + 1 < addr3))
	    brk = 1;
	if (!brk && size == 4 && ((addr + 2 >= addr2 && addr + 2 < addr3) || (addr + 3 >= addr2 && addr + 3 < addr3)))
	    brk = 1;
	if (brk && mwnodes[i].modval_written) {
	    if (!rw) {
		brk = 0;
	    } else if (mwnodes[i].modval_written == 1) {
		mwnodes[i].modval_written = 2;
		mwnodes[i].modval = val;
		brk = 0;
	    } else if (mwnodes[i].modval == val) {
		brk = 0;
	    }
	}
	if (brk) {
	    mwhit.addr = addr;
	    mwhit.rw = rw;
	    mwhit.size = size;
	    mwhit.val = 0;
	    if (mwhit.rw)
		mwhit.val = val;
	    memwatch_triggered = i + 1;
	    debugging = 1;
	    set_special (SPCFLAG_BRK);
	    break;
	}
    }
}

static uae_u32 REGPARAM debug_lget (uaecptr addr)
{
    int off = debug_mem_off (addr);
    uae_u32 v;
    v = debug_mem_banks[off]->lget(addr);
    memwatch_func (addr, 0, 4, v);
    return v;
}
static uae_u32 REGPARAM2 debug_wget (uaecptr addr)
{
    int off = debug_mem_off (addr);
    uae_u32 v;
    v = debug_mem_banks[off]->wget(addr);
    memwatch_func (addr, 0, 2, v);
    return v;
}
static uae_u32 REGPARAM2 debug_bget (uaecptr addr)
{
    int off = debug_mem_off (addr);
    uae_u32 v;
    v = debug_mem_banks[off]->bget(addr);
    memwatch_func (addr, 0, 1, v);
    return v;
}
static void REGPARAM2 debug_lput (uaecptr addr, uae_u32 v)
{
    int off = debug_mem_off (addr);
    memwatch_func (addr, 1, 4, v);
    debug_mem_banks[off]->lput(addr, v);
}
static void REGPARAM2 debug_wput (uaecptr addr, uae_u32 v)
{
    int off = debug_mem_off (addr);
    memwatch_func (addr, 1, 2, v);
    debug_mem_banks[off]->wput(addr, v);
}
static void REGPARAM2 debug_bput (uaecptr addr, uae_u32 v)
{
    int off = debug_mem_off (addr);
    memwatch_func (addr, 1, 1, v);
    debug_mem_banks[off]->bput(addr, v);
}
static int REGPARAM2 debug_check (uaecptr addr, uae_u32 size)
{
    return debug_mem_banks[munge24 (addr) >> 16]->check (addr, size);
}
static uae_u8 *REGPARAM2 debug_xlate (uaecptr addr)
{
    return debug_mem_banks[munge24 (addr) >> 16]->xlateaddr (addr);
}

static void deinitialize_memwatch (void)
{
    int i;
    addrbank *a1, *a2;

    if (!memwatch_enabled)
	return;
    for (i = 0; i < 256; i++) {
	a1 = debug_mem_banks[i];
	a2 = mem_banks[i];
	memcpy (a2, a1, sizeof (addrbank));
	free (a1);
    }
    free (debug_mem_banks);
    debug_mem_banks = 0;
    memwatch_enabled = 0;
    free (illgdebug);
    illgdebug = 0;
}

static int initialize_memwatch (void)
{
    int i;
    addrbank *a1, *a2;

    if (!currprefs.address_space_24)
	return 0;
    debug_mem_banks = xmalloc (sizeof (addrbank*) * 256);
    for (i = 0; i < 256; i++) {
	a1 = debug_mem_banks[i] = xmalloc (sizeof (addrbank));
	a2 = mem_banks[i];
	memcpy (a1, a2, sizeof (addrbank));
    }
    for (i = 0; i < 256; i++) {
	a2 = mem_banks[i];
	a2->bget = debug_bget;
	a2->wget = debug_wget;
	a2->lget = debug_lget;
	a2->bput = debug_bput;
	a2->wput = debug_wput;
	a2->lput = debug_lput;
	a2->check = debug_check;
	a2->xlateaddr = debug_xlate;
    }
    memwatch_enabled = 1;
    return 1;
}

static void memwatch_dump (int num)
{
    int i;
    struct memwatch_node *mwn;
    for (i = 0; i < MEMWATCH_TOTAL; i++) {
	if ((num >= 0 && num == i) || (num < 0)) {
	    mwn = &mwnodes[i];
	    if (mwn->size == 0)
		continue;
	    console_out ("%d: %08.8X - %08.8X (%d) %s",
		i, mwn->addr, mwn->addr + (mwn->size - 1), mwn->size,
		mwn->rw == 0 ? "R" : (mwn->rw == 1 ? "W" : "RW"));
	    if (mwn->val_enabled)
		console_out (" =%X", mwn->val);
	    if (mwn->modval_written)
		console_out (" =M");
	    console_out("\n");
	}
    }
}

static void memwatch (char **c)
{
    int num;
    struct memwatch_node *mwn;
    char nc;

    if (!memwatch_enabled) {
	if (!initialize_memwatch ()) {
	    console_out ("Memwatch breakpoints require 24-bit address space\n");
	    return;
	}
	console_out ("Memwatch breakpoints enabled\n");
    }

    ignore_ws (c);
    if (!more_params (c)) {
	memwatch_dump (-1);
	return;
    }
    nc = next_char (c);
    if (nc == '-') {
	deinitialize_memwatch ();
	console_out ("Memwatch breakpoints disabled\n");
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
		write_log ("cleared logging addresses %08.8X - %08.8X\n", addr, addr + len);
		while (len > 0) {
		    addr &= 0xffffff;
		    illgdebug[addr] = 0;
		    addr++;
		    len--;
		}
	    }
	} else {
	    illg_init ();
	    console_out ("Illegal memory access logging enabled\n");
	    ignore_ws (c);
	    illgdebug_break = 0;
	    if (more_params (c))
		illgdebug_break = 1;
	}
	return;
    }
    num = nc - '0';
    if (num < 0 || num >= MEMWATCH_TOTAL)
	return;
    mwn = &mwnodes[num];
    mwn->size = 0;
    ignore_ws (c);
    if (!more_params (c)) {
	console_out ("Memwatch %d removed\n", num);
	return;
    }
    mwn->addr = readhex (c);
    mwn->size = 1;
    mwn->rw = 2;
    mwn->val_enabled = 0;
    mwn->modval_written = 0;
    ignore_ws (c);
    if (more_params (c)) {
	mwn->size = readhex (c);
	ignore_ws (c);
	if (more_params (c)) {
	    char nc = toupper (next_char (c));
	    if (nc == 'W')
		mwn->rw = 1;
	    else if (nc == 'R' && toupper(**c) != 'W')
		mwn->rw = 0;
	    else if (nc == 'R' && toupper(**c) == 'W')
		next_char (c);
	    ignore_ws (c);
	    if (more_params (c)) {
		if (toupper(**c) == 'M') {
		    mwn->modval_written = 1;
		} else {
		    mwn->val = readhex (c);
		    mwn->val_enabled = 1;
		}
	    }
	}
    }
    memwatch_dump (num);
}

static void writeintomem (char **c)
{
    uae_u32 addr = 0;
    uae_u32 val = 0;
    char cc;

    ignore_ws(c);
    addr = readhex (c);
    ignore_ws(c);
    val = readhex (c);
    if (val > 0xffff) {
	put_long (addr, val);
	cc = 'L';
    } else if (val > 0xff) {
	put_word (addr, val);
	cc = 'W';
    } else {
	put_byte (addr, val);
	cc = 'B';
    }
    console_out ("Wrote %x (%u) at %08x.%c\n", val, val, addr, cc);
}

static void show_exec_tasks (void)
{
    uaecptr execbase = get_long (4);
    uaecptr taskready = get_long (execbase + 406);
    uaecptr taskwait = get_long (execbase + 420);
    uaecptr node, end;
    console_out ("execbase at 0x%08lx\n", (unsigned long) execbase);
    console_out ("Current:\n");
    node = get_long (execbase + 276);
    console_out ("%08lx: %08lx %s\n", node, 0, get_real_address (get_long (node + 10)));
    console_out ("Ready:\n");
    node = get_long (taskready);
    end = get_long (taskready + 4);
    while (node) {
	console_out ("%08lx: %08lx %s\n", node, 0, get_real_address (get_long (node + 10)));
	node = get_long (node);
    }
    console_out ("Waiting:\n");
    node = get_long (taskwait);
    end = get_long (taskwait + 4);
    while (node) {
	console_out ("%08lx: %08lx %s\n", node, 0, get_real_address (get_long (node + 10)));
	node = get_long (node);
    }
}

static int trace_same_insn_count;
static uae_u8 trace_insn_copy[10];
static struct regstruct trace_prev_regs;
static uaecptr nextpc;

static int instruction_breakpoint (char **c)
{
    struct breakpoint_node *bpn;
    int i;

    if (more_params (c)) {
	char nc = toupper((*c)[0]);
	if (nc == 'I') {
	    next_char (c);
	    if (more_params (c))
		skipins = readhex (c);
	    else
		skipins = 0x10000;
	    do_skip = 1;
	    skipaddr_doskip = 1;
	    return 1;
	} else if (nc == 'D' && (*c)[1] == 0) {
	    for (i = 0; i < BREAKPOINT_TOTAL; i++)
		bpnodes[i].enabled = 0;
	    console_out ("All breakpoints removed\n");
	    return 0;
	} else if (nc == 'L') {
	    int got = 0;
	    for (i = 0; i < BREAKPOINT_TOTAL; i++) {
		bpn = &bpnodes[i];
		if (!bpn->enabled)
		    continue;
		console_out ("%8X ", bpn->addr);
		got = 1;
	    }
	    if (!got)
		console_out ("No breakpoints\n");
	    else
		console_out ("\n");
	    return 0;
	}
	skipaddr_doskip = 1;
	skipaddr_start = readhex (c);
	if (more_params (c)) {
	    skipaddr_end = readhex (c);
	} else {
	    for (i = 0; i < BREAKPOINT_TOTAL; i++) {
		bpn = &bpnodes[i];
		if (bpn->enabled && bpn->addr == skipaddr_start) {
		    bpn->enabled = 0;
		    console_out ("Breakpoint removed\n");
		    skipaddr_start = 0xffffffff;
		    skipaddr_doskip = 0;
		    return 0;
		}
	    }
	    for (i = 0; i < BREAKPOINT_TOTAL; i++) {
		bpn = &bpnodes[i];
		if (bpn->enabled)
		    continue;
		bpn->addr = skipaddr_start;
		bpn->enabled = 1;
		console_out ("Breakpoint added\n");
		skipaddr_start = 0xffffffff;
		skipaddr_doskip = 0;
		break;
	    }
	    return 0;
	}
    }
    if (skipaddr_start == 0xC0DEDBAD) {
	trace_same_insn_count = 0;
	logfile = fopen ("uae.trace", "w");
	memcpy (trace_insn_copy, regs.pc_p, 10);
	memcpy (&trace_prev_regs, &regs, sizeof regs);
    }
    do_skip = 1;
    skipaddr_doskip = -1;
    return 1;
}

static void savemem (char **cc)
{
    uae_u8 b;
    uae_u32 src, src2, len, len2;
    char *name;
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
    if (!more_params (cc))
	goto S_argh;
    len2 = len = readhex (cc);
    fp = fopen (name, "wb");
    if (fp == NULL) {
	console_out ("Couldn't open file '%s'\n", name);
	return;
    }
    while (len > 0) {
	b = get_byte (src);
	src++;
	len--;
	if (fwrite (&b, 1, 1, fp) != 1) {
	    console_out ("Error writing file\n");
	    break;
	}
    }
    fclose (fp);
    if (len == 0)
	console_out ("Wrote %08X - %08X (%d bytes) to '%s'\n",
	    src2, src2 + len2, len2, name);
    return;
S_argh:
    console_out ("S-command needs more arguments!\n");
}

static void searchmem (char **cc)
{
    int i, sslen, got, val, stringmode;
    uae_u8 ss[256];
    uae_u32 addr, endaddr;
    char nc;

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
	    nc = toupper (next_char (cc));
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
	    nc = toupper (next_char (cc));
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
    endaddr = nextaddr (0xffffffff);
    if (more_params (cc)) {
	addr = readhex (cc);
	if (more_params (cc))
	    endaddr = readhex (cc);
    }
    console_out ("Searching from %08x to %08x..\n", addr, endaddr);
    while (addr < endaddr && addr != 0xffffffff) {
	for (i = 0; i < sslen; i++) {
	    uae_u8 b = get_byte (addr + i);
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
	    console_out (" %08x", addr);
	    if (got > 100) {
		console_out ("\nMore than 100 results, aborting..");
		break;
	    }
	}
	addr = nextaddr (addr);
    }
    if (!got)
	console_out ("nothing found");
    console_out ("\n");
}

static int staterecorder (char **cc)
{
    char nc;

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
    return 0;
}

static void disk_debug(char **inptr)
{
    char parm[10];
    int i;

    disk_debug_logging = 2;
    disk_debug_mode = 0;
    disk_debug_track = -1;
    ignore_ws(inptr);
    if (!next_string (inptr, parm, sizeof (parm), 1))
	goto end;
    for (i = 0; i < strlen(parm); i++) {
	if (parm[i] == 'R')
	    disk_debug_mode |= DISK_DEBUG_DMA_READ;
	if (parm[i] == 'W')
	    disk_debug_mode |= DISK_DEBUG_DMA_WRITE;
	if (parm[i] == 'P')
	    disk_debug_mode |= DISK_DEBUG_PIO;
    }
    if (more_params(inptr))
	disk_debug_track = readint(inptr);
    if (disk_debug_track < 0 || disk_debug_track > 2 * 83)
	disk_debug_track = -1;
end:
    console_out("disk breakpoint mode %c%c%c track %d\n",
	disk_debug_mode & DISK_DEBUG_DMA_READ ? 'R' : '-',
	disk_debug_mode & DISK_DEBUG_DMA_WRITE ? 'W' : '-',
	disk_debug_mode & DISK_DEBUG_PIO ? 'P' : '-',
	disk_debug_track);
}

static void m68k_modify (char **inptr)
{
    uae_u32 v;
    char parm[10];
    char c1, c2;

    if (!next_string (inptr, parm, sizeof (parm), 1))
	return;
    c1 = toupper (parm[0]);
    c2 = toupper (parm[1]);
    if (c1 == 'A' || c1 == 'D' || c1 == 'P') {
	if (!isdigit (c2))
	    return;
	c2 -= '0';
    }
    v = readhex (inptr);
    if (c1 == 'A')
	regs.regs[8 + c2] = v;
    else if (c1 == 'D')
	regs.regs[c2] = v;
    else if (c1 == 'P' && c2 == 0)
	regs.irc = v;
    else if (c1 == 'P' && c2 == 1)
	regs.ir = v;
    else if (!strcmp (parm, "VBR"))
	regs.vbr = v;
    else if (!strcmp (parm, "USP")) {
	regs.usp = v;
	MakeFromSR ();
    } else if (!strcmp (parm, "ISP")) {
	regs.isp = v;
	MakeFromSR ();
    } else if (!strcmp (parm, "MSP")) {
	regs.msp = v;
	MakeFromSR ();
    } else if (!strcmp (parm, "SR")) {
	regs.sr = v;
	MakeFromSR ();
    } else if (!strcmp (parm, "CCR")) {
	regs.sr = (regs.sr & ~15) | (v & 15);
	MakeFromSR ();
    }
}

static void debug_1 (void)
{
    char input[80];
    uaecptr nxdis, nxmem, addr;

    m68k_dumpstate (stdout, &nextpc);
    nxdis = nextpc; nxmem = 0;

    for (;;) {
	char cmd, *inptr;

	console_out (">");
	console_flush ();
	if (!console_get (input, 80))
	    continue;
	inptr = input;
	cmd = next_char (&inptr);
	switch (cmd) {
	case 'c': dumpcia (); dumpdisk (); dumpcustom (); break;
	case 'i':
	    addr = 0xffffffff;
	    if (more_params (&inptr))
		addr = readhex (&inptr);
	    dump_vectors (addr);
	break;
	case 'e': dump_custom_regs (); break;
	case 'r': if (more_params(&inptr))
		    m68k_modify (&inptr);
		  else
		    m68k_dumpstate (stdout, &nextpc);
	break;
	case 'C': cheatsearch (&inptr); break;
	case 'W': writeintomem (&inptr); break;
	case 'w': memwatch (&inptr); break;
	case 'S': savemem (&inptr); break;
	case 's':
	    if (*inptr == 'c') {
		screenshot (1, 1);
	    } else {
		searchmem (&inptr);
	    }
	break;
	case 'd':
	{
	    if (*inptr == 'i') {
		next_char(&inptr);
		disk_debug(&inptr);
	    } else {
		uae_u32 daddr;
		int count;
		if (more_params(&inptr))
		    daddr = readhex(&inptr);
		else
		    daddr = nxdis;
		if (more_params(&inptr))
		    count = readhex(&inptr);
		else
		    count = 10;
		m68k_disasm (stdout, daddr, &nxdis, count);
	    }
	}
	break;
	case 'T': show_exec_tasks (); break;
	case 't':
	    if (more_params (&inptr))
		skipaddr_doskip = readint (&inptr);
	    if (skipaddr_doskip <= 0 || skipaddr_doskip > 10000)
		skipaddr_doskip = 1;
	    set_special (SPCFLAG_BRK);
	    exception_debugging = 1;
	    return;
	case 'z':
	    skipaddr_start = nextpc;
	    skipaddr_doskip = 1;
	    do_skip = 1;
	    exception_debugging = 1;
	    return;

	case 'f':
	    if (instruction_breakpoint (&inptr))
		return;
	    break;

	case 'q': uae_quit();
	    debugger_active = 0;
	    debugging = 0;
	    return;

	case 'g':
	    if (more_params (&inptr)) {
		m68k_setpc (readhex (&inptr));
		fill_prefetch_slow ();
	    }
	    debugger_active = 0;
	    debugging = 0;
	    exception_debugging = 0;
	    return;

	case 'H':
	{
	    int count, temp, badly;
	    uae_u32 oldpc = m68k_getpc();
	    struct regstruct save_regs = regs;
	    struct flag_struct save_flags = regflags;

	    badly = 0;
	    if (inptr[0] == 'H') {
		badly = 1;
		inptr++;
	    }

	    if (more_params(&inptr))
		count = readhex(&inptr);
	    else
		count = 10;
	    if (count < 0)
		break;
	    temp = lasthist;
	    while (count-- > 0 && temp != firsthist) {
		if (temp == 0)
		    temp = MAX_HIST-1;
		else
		    temp--;
	    }
	    while (temp != lasthist) {
		regs = history[temp];
		regflags = historyf[temp];
		m68k_setpc(history[temp].pc);
		if (badly) {
		    m68k_dumpstate(stdout, NULL);
		} else {
		    m68k_disasm(stdout, history[temp].pc, NULL, 1);
		}
		if (++temp == MAX_HIST)
		    temp = 0;
	    }
	    regs = save_regs;
	    regflags = save_flags;
	    m68k_setpc(oldpc);
	}
	break;
	case 'm':
	{
	    uae_u32 maddr; int lines;
	    if (more_params(&inptr))
		maddr = readhex(&inptr);
	    else
		maddr = nxmem;
	    if (more_params(&inptr))
		lines = readhex(&inptr);
	    else
		lines = 20;
	    dumpmem(maddr, &nxmem, lines);
	}
	break;
	case 'o':
	{
	    if (copper_debugger(&inptr)) {
	        debugger_active = 0;
	        debugging = 0;
		return;
	    }
	    break;
	}
	case 'O':
	    if (more_params (&inptr)) {
		int plane = readint (&inptr);
		int offs = readint (&inptr);
		if (plane >= 0 && plane < 8)
		    bpl_off[plane] = offs;
	    } else {
		int i;
		for (i = 0; i < 8; i++)
		    console_out ("Plane %d offset %d\n", i, bpl_off[i]);
	    }
	    break;
	case 'b':
	    if (staterecorder (&inptr))
		return;
	    break;
	case 'a':
	    if (more_params (&inptr)) {
		char nc = next_char (&inptr);
		if (nc == 'm')
		    audio_channel_mask = readint (&inptr);
	    }
	    break;
	case 'h':
	case '?':
	    debug_help ();
	break;
	}
    }
}

static void addhistory(void)
{
    history[lasthist] = regs;
    history[lasthist].pc = m68k_getpc();
    historyf[lasthist] = regflags;
    if (++lasthist == MAX_HIST)
	lasthist = 0;
    if (lasthist == firsthist) {
	if (++firsthist == MAX_HIST) firsthist = 0;
    }
}

void debug (void)
{
    int i;

    if (savestate_state)
	return;

    bogusframe = 1;
    addhistory();

    if (do_skip && skipaddr_start == 0xC0DEDBAD) {
#if 0
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
#endif
	m68k_dumpstate (logfile, &nextpc);
	trace_same_insn_count = 1;
	memcpy (trace_insn_copy, regs.pc_p, 10);
	memcpy (&trace_prev_regs, &regs, sizeof regs);
    }

    if (!memwatch_triggered) {
	if (do_skip) {
	    uae_u32 pc = munge24 (m68k_getpc());
	    uae_u16 opcode = (currprefs.cpu_compatible || currprefs.cpu_cycle_exact) ? regs.ir : get_word (pc);
	    int bp = 0;

	    for (i = 0; i < BREAKPOINT_TOTAL; i++) {
		if (!bpnodes[i].enabled)
		    continue;
		if (bpnodes[i].addr == pc) {
		    bp = 1;
		    console_out ("Breakpoint at %08.8X\n", pc);
		    break;
		}
	    }
	    if (skipaddr_doskip) {
		if (skipaddr_start == pc)
		    bp = 1;
		if (skipins != 0xffffffff) {
		    if (skipins == 0x10000) {
			if (opcode == 0x4e75 || opcode == 0x4e73 || opcode == 0x4e77)
			    bp = 1;
		    } else if (opcode == skipins)
			bp = 1;
		} else if (skipaddr_start == 0xffffffff && skipaddr_doskip < 0) {
		    if ((pc < 0xe00000 || pc >= 0x1000000) && opcode != 0x4ef9)
			bp = 1;
		} else if (skipaddr_start == 0xffffffff && skipaddr_doskip > 0) {
			bp = 1;
		} else if (skipaddr_end != 0xffffffff) {
		    if (pc >= skipaddr_start && pc < skipaddr_end)
			bp = 1;
		}
	    }
	    if (!bp) {
		set_special (SPCFLAG_BRK);
		return;
	    }
	}
    } else {
	write_log ("Memwatch %d: break at %08.8X.%c %c %08.8X\n", memwatch_triggered - 1, mwhit.addr,
	    mwhit.size == 1 ? 'B' : (mwhit.size == 2 ? 'W' : 'L'), mwhit.rw ? 'W' : 'R', mwhit.val);
	memwatch_triggered = 0;
    }
    if (skipaddr_doskip > 0) {
	skipaddr_doskip--;
	if (skipaddr_doskip > 0) {
	    set_special (SPCFLAG_BRK);
	    return;
	}
    }

    inputdevice_unacquire ();
    pause_sound ();
    do_skip = 0;
    skipaddr_start = 0xffffffff;
    skipaddr_end = 0xffffffff;
    skipins = 0xffffffff;
    skipaddr_doskip = 0;
    exception_debugging = 0;
    debug_rewind = 0;
#if 0
    if (!currprefs.statecapture) {
	changed_prefs.statecapture = currprefs.statecapture = 1;
	savestate_init ();
    }
#endif
    debug_1 ();
    if (!debug_rewind && !currprefs.cachesize
#ifdef FILESYS
	&& nr_units (currprefs.mountinfo) == 0
#endif
	) {
	savestate_capture (1);
    }
    for (i = 0; i < BREAKPOINT_TOTAL; i++) {
	if (bpnodes[i].enabled)
	    do_skip = 1;
    }
    if (do_skip) {
	set_special (SPCFLAG_BRK);
	debugging = 1;
    }
    resume_sound ();
    inputdevice_acquire ();
}

int notinrom (void)
{
    if (munge24 (m68k_getpc()) < 0xe0000)
	return 1;
    return 0;
}
/*
const char *debuginfo (int mode)
{
    static char txt[100];
    uae_u32 pc = m68k_getpc();
    sprintf (txt, "PC=%08.8X INS=%04.4X %04.4X %04.4X",
	pc, get_word(pc), get_word(pc+2), get_word(pc+4));
    return txt;
}
*/