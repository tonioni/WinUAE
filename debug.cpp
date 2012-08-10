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
#include "cia.h"
#include "xwin.h"
#include "identify.h"
#include "audio.h"
#include "sound.h"
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

int debugger_active;
static uaecptr skipaddr_start, skipaddr_end;
static int skipaddr_doskip;
static uae_u32 skipins;
static int do_skip;
static int debug_rewind;
static int memwatch_enabled, memwatch_triggered;
static uae_u16 sr_bpmask, sr_bpvalue;
int debugging;
int exception_debugging;
int no_trace_exceptions;
int debug_copper = 0;
int debug_dma = 0;
int debug_sprite_mask = 0xff;
int debug_illegal = 0;
uae_u64 debug_illegal_mask;

static uaecptr processptr;
static uae_char *processname;

static uaecptr debug_copper_pc;

extern int audio_channel_mask;
extern int inputdevice_logging;

void deactivate_debugger (void)
{
	debugger_active = 0;
	debugging = 0;
	exception_debugging = 0;
	processptr = 0;
	xfree (processname);
	processname = NULL;
}

void activate_debugger (void)
{
	do_skip = 0;
	if (debugger_active)
		return;
	debugger_active = 1;
	set_special (SPCFLAG_BRK);
	debugging = 1;
	mmu_triggered = 0;
}

int firsthist = 0;
int lasthist = 0;
static struct regstruct history[MAX_HIST];

static TCHAR help[] = {
	_T("          HELP for UAE Debugger\n")
	_T("         -----------------------\n\n")
	_T("  g [<address>]         Start execution at the current address or <address>.\n")
	_T("  c                     Dump state of the CIA, disk drives and custom registers.\n")
	_T("  r                     Dump state of the CPU.\n")
	_T("  r <reg> <value>       Modify CPU registers (Dx,Ax,USP,ISP,VBR,...).\n")
	_T("  m <address> [<lines>] Memory dump starting at <address>.\n")
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
	_T("  fs <val> <mask>       Break when (SR & mask) = val.\n")                   
	_T("  f <addr1> <addr2>     Step forward until <addr1> <= PC <= <addr2>.\n")
	_T("  e                     Dump contents of all custom registers, ea = AGA colors.\n")
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
	_T("  W <address> <values[.x] separated by space> Write into Amiga memory.\n")
	_T("  W <address> 'string' Write into Amiga memory.\n")
	_T("  w <num> <address> <length> <R/W/I/F/C> [<value>[.x]] (read/write/opcode/freeze/mustchange).\n")
	_T("                        Add/remove memory watchpoints.\n")
	_T("  wd [<0-1>]            Enable illegal access logger. 1 = enable break.\n")
	_T("  S <file> <addr> <n>   Save a block of Amiga memory.\n")
	_T("  s \"<string>\"/<values> [<addr>] [<length>]\n")
	_T("                        Search for string/bytes.\n")
	_T("  T or Tt               Show exec tasks and their PCs.\n")
	_T("  Td,Tl,Tr              Show devices, libraries or resources.\n")
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
	_T("  ?<value>              Hex ($ and 0x)/Bin (%)/Dec (!) converter.\n")
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

static int readregx (TCHAR **c, uae_u32 *valp)
{
	int i;
	uae_u32 addr;
	TCHAR *p = *c;
	TCHAR tmp[10];
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
	if (_totupper (tmp[0]) == 'R') {
		memmove (tmp, tmp + 1, sizeof (tmp) - sizeof (TCHAR));
		extra = 1;
	}
	if (!_tcscmp (tmp, _T("USP"))) {
		addr = regs.usp;
		(*c) += 3;
	} else if (!_tcscmp (tmp, _T("VBR"))) {
		addr = regs.vbr;
		(*c) += 3;
	} else if (!_tcscmp (tmp, _T("MSP"))) {
		addr = regs.msp;
		(*c) += 3;
	} else if (!_tcscmp (tmp, _T("ISP"))) {
		addr = regs.isp;
		(*c) += 3;
	} else if (!_tcscmp (tmp, _T("PC"))) {
		addr = regs.pc;
		(*c) += 2;
	} else if (tmp[0] == 'A' || tmp[0] == 'D') {
		int reg = 0;
		if (tmp[0] == 'A')
			reg += 8;
		reg += tmp[1] - '0';
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
	if (def == '!') {
		return readintx (c, val) ? -1 : 0;
		return -1;
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
			*size = readsize (v, cp);
		}
		if (!isoperator (cp))
			break;
		gotop = true;
		*p++= readchar (cp);
		*p = 0;
	}
	if (!gotop) {
		if (*size == 0) {
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
	if (calc (form, &out)) {
		*val = (uae_u32)out;
		if (*size == 0) {
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
	TCHAR *p = s;
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
	if (currprefs.z3fastmem2_size)
		return z3fastmem2_start + currprefs.z3fastmem2_size;
	if (currprefs.z3fastmem_size)
		return z3fastmem_start + currprefs.z3fastmem_size;
	if (currprefs.z3chipmem_size)
		return z3chipmem_start + currprefs.z3chipmem_size;
	if (currprefs.mbresmem_high_size)
		return a3000hmem_start + currprefs.mbresmem_high_size;
	if (currprefs.mbresmem_low_size)
		return a3000lmem_start + currprefs.mbresmem_low_size;
	if (currprefs.bogomem_size)
		return bogomem_start + currprefs.bogomem_size;
	if (currprefs.fastmem_size)
		return fastmem_start + currprefs.fastmem_size;
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
	prev = currprefs.z3fastmem_start + currprefs.z3fastmem_size;
	size = currprefs.z3fastmem2_size;

	if (currprefs.z3fastmem_size) {
		prevx = prev;
		sizex = size;
		size = currprefs.z3fastmem_size;
		prev = z3fastmem_start;
		if (addr == prev + size) {
			*next = prevx + sizex;
			return prevx;
		}
	}
	if (currprefs.z3chipmem_size) {
		prevx = prev;
		sizex = size;
		size = currprefs.z3chipmem_size;
		prev = z3chipmem_start;
		if (addr == prev + size) {
			*next = prevx + sizex;
			return prevx;
		}
	}
	if (currprefs.mbresmem_high_size) {
		sizex = size;
		prevx = prev;
		size = currprefs.mbresmem_high_size;
		prev = a3000hmem_start;
		if (addr == prev + size) {
			*next = prevx + sizex;
			return prevx;
		}
	}
	if (currprefs.mbresmem_low_size) {
		prevx = prev;
		sizex = size;
		size = currprefs.mbresmem_low_size;
		prev = a3000lmem_start;
		if (addr == prev + size) {
			*next = prevx + sizex;
			return prevx;
		}
	}
	if (currprefs.bogomem_size) {
		sizex = size;
		prevx = prev;
		size = currprefs.bogomem_size;
		prev = bogomem_start;
		if (addr == prev + size) {
			*next = prevx + sizex;
			return prevx;
		}
	}
	if (currprefs.fastmem_size) {
		sizex = size;
		prevx = prev;
		size = currprefs.fastmem_size;
		prev = fastmem_start;
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
	uaecptr paddr = addr;
	int next;
	if (last && 0) {
		if (addr >= last)
			return 0xffffffff;
		return addr + 1;
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

int safe_addr (uaecptr addr, int size)
{
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

uaecptr dumpmem2 (uaecptr addr, TCHAR *out, int osize)
{
	int i, cols = 8;
	int nonsafe = 0;

	if (osize <= (9 + cols * 5 + 1 + 2 * cols))
		return addr;
	_stprintf (out, _T("%08lX "), addr);
	for (i = 0; i < cols; i++) {
		uae_u8 b1, b2;
		b1 = b2 = 0;
		if (safe_addr (addr, 1)) {
			b1 = get_byte (addr + 0);
			b2 = get_byte (addr + 1);
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

static void dump_custom_regs (int aga)
{
	int len, i, j, end;
	uae_u8 *p1, *p2, *p3, *p4;

	if (aga) {
		dump_aga_custom();
		return;
	}

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
		console_out_f (_T("%03X %s\t%04X\t%03X %s\t%04X\n"),
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
			console_out_f (_T("$%08X %02d: %12s $%08X  "), int_labels[i].adr + addr, int_labels[i].adr / 4,
				int_labels[i].name, get_long (int_labels[i].adr + addr));
			i++;
		}
		if (trap_labels[j].name) {
			console_out_f (_T("$%08X %02d: %12s $%08X"), trap_labels[j].adr + addr, trap_labels[j].adr / 4,
				trap_labels[j].name, get_long (trap_labels[j].adr + addr));
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

	console_out_f (_T("\n                        \t; VP %02x, VE %02x; HP %02x, HE %02x; BFD %d\n"),
		vp, ve, hp, he, bfd);
}

#define NR_COPPER_RECORDS 100000
/* Record copper activity for the debugger.  */
struct cop_rec
{
	int hpos, vpos;
	uaecptr addr;
};
static struct cop_rec *cop_record[2];
static int nr_cop_records[2], curr_cop_set;

#define NR_DMA_REC_HPOS 256
#define NR_DMA_REC_VPOS 1000
static struct dma_rec *dma_record[2];
static int dma_record_toggle;

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

#define lc(x) ledcolor (x, xredcolors, xgreencolors, xbluecolors, NULL);
void debug_draw_cycles (uae_u8 *buf, int bpp, int line, int width, int height, uae_u32 *xredcolors, uae_u32 *xgreencolors, uae_u32 *xbluescolors)
{
	int y, x, xx, dx, xplus, yplus;
	struct dma_rec *dr;
	int t;
	uae_u32 cc[DMARECORD_MAX];

	if (debug_dma >= 4)
		yplus = 2;
	else
		yplus = 1;
	if (debug_dma >= 3)
		xplus = 2;
	else
		xplus = 1;

	t = dma_record_toggle ^ 1;
	y = line / yplus - 8;

	if (y < 0)
		return;
	if (y > maxvpos)
		return;
	if (y >= height)
		return;

	dx = width - xplus * ((maxhpos + 1) & ~1) - 16;

	cc[0] = lc(0x222222);
	cc[DMARECORD_REFRESH] = lc(0x444444);
	cc[DMARECORD_CPU] = lc(0x888888);
	cc[DMARECORD_COPPER] = lc(0xeeee00);
	cc[DMARECORD_AUDIO] = lc(0xff0000);
	cc[DMARECORD_BLITTER] = lc(0x00ff00);
	cc[DMARECORD_BLITTER_LINE] = lc(0x008800);
	cc[DMARECORD_BITPLANE] = lc(0x0000ff);
	cc[DMARECORD_SPRITE] = lc(0xff00ff);
	cc[DMARECORD_DISK] = lc(0xffffff);

	for (x = 0; x < maxhpos; x++) {
		uae_u32 c = cc[0];
		xx = x * xplus + dx;
		dr = &dma_record[t][y * NR_DMA_REC_HPOS + x];
		if (dr->reg != 0xffff) {
			c = cc[dr->type];	    
		}
		putpixel (buf, bpp, xx, c);
		if (xplus)
			putpixel (buf, bpp, xx + 1, c);
	}
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

struct dma_rec *record_dma (uae_u16 reg, uae_u16 dat, uae_u32 addr, int hpos, int vpos, int type)
{
	struct dma_rec *dr;

	if (!dma_record[0]) {
		dma_record[0] = xmalloc (struct dma_rec, NR_DMA_REC_HPOS * NR_DMA_REC_VPOS);
		dma_record[1] = xmalloc (struct dma_rec, NR_DMA_REC_HPOS * NR_DMA_REC_VPOS);
		dma_record_toggle = 0;
		record_dma_reset ();
	}
	if (hpos >= NR_DMA_REC_HPOS || vpos >= NR_DMA_REC_VPOS)
		return NULL;
	dr = &dma_record[dma_record_toggle][vpos * NR_DMA_REC_HPOS + hpos];
	if (dr->reg != 0xffff) {
		write_log (_T("DMA conflict: v=%d h=%d OREG=%04X NREG=%04X\n"), vpos, hpos, dr->reg, reg);
		return dr;
	}
	dr->reg = reg;
	dr->dat = dat;
	dr->addr = addr;
	dr->type = type;
	return dr;
}

static void decode_dma_record (int hpos, int vpos, int toggle, bool logfile)
{
	struct dma_rec *dr;
	int h, i, maxh, cnt;
	uae_u32 cycles;

	if (!dma_record[0])
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
	cnt = 0;
	while (h < maxh) {
		int col = 9;
		int cols = 8;
		TCHAR l1[81];
		TCHAR l2[81];
		TCHAR l3[81];
		TCHAR l4[81];
		TCHAR l5[81];
		for (i = 0; i < cols && h < maxh; i++, h++, dr++) {
			int cl = i * col, cl2;
			int r = dr->reg;
			TCHAR *sr;

			sr = _T("    ");
			if (dr->type == DMARECORD_COPPER)
				sr = _T("COP ");
			else if (dr->type == DMARECORD_BLITTER)
				sr = _T("BLT ");
			else if (dr->type == DMARECORD_REFRESH)
				sr = _T("RFS ");
			else if (dr->type == DMARECORD_AUDIO)
				sr = _T("AUD ");
			else if (dr->type == DMARECORD_DISK)
				sr = _T("DSK ");
			else if (dr->type == DMARECORD_SPRITE)
				sr = _T("SPR ");
			_stprintf (l1 + cl, _T("[%02X %3d]"), h, h);
			_tcscpy (l4 + cl, _T("        "));
			if (r != 0xffff) {
				if (r & 0x1000) {
					if ((r & 0x0100) == 0x0000)
						_tcscpy (l2 + cl, _T("  CPU-R  "));
					else if ((r & 0x0100) == 0x0100)
						_tcscpy (l2 + cl, _T("  CPU-W  "));
					if ((r & 0xff) == 4)
						l2[cl + 7] = 'L';
					if ((r & 0xff) == 2)
						l2[cl + 7] = 'W';
					if ((r & 0xff) == 1)
						l2[cl + 7] = 'B';
				} else {
					_stprintf (l2 + cl, _T("%4s %03X"), sr, r);
				}
				_stprintf (l3 + cl, _T("    %04X"), dr->dat);
				if (dr->addr != 0xffffffff)
					_stprintf (l4 + cl, _T("%08X"), dr->addr & 0x00ffffff);
			} else {
				_tcscpy (l2 + cl, _T("        "));
				_tcscpy (l3 + cl, _T("        "));
			}
			cl2 = cl;
			if (dr->evt & DMA_EVENT_BLITNASTY)
				l3[cl2++] = 'N';
			if (dr->evt & DMA_EVENT_BLITFINISHED)
				l3[cl2++] = 'B';
			if (dr->evt & DMA_EVENT_BLITIRQ)
				l3[cl2++] = 'b';
			if (dr->evt & DMA_EVENT_BPLFETCHUPDATE)
				l3[cl2++] = 'p';
			if (dr->evt & DMA_EVENT_COPPERWAKE)
				l3[cl2++] = 'W';
			if (dr->evt & DMA_EVENT_COPPERWANTED)
				l3[cl2++] = 'c';
			if (dr->evt & DMA_EVENT_CPUIRQ)
				l3[cl2++] = 'I';
			if (dr->evt & DMA_EVENT_INTREQ)
				l3[cl2++] = 'i';
			_stprintf (l5 + cl, _T("%08X"), cycles + (vpos * maxhpos + (hpos + cnt)) * CYCLE_UNIT);
			if (i < cols - 1 && h < maxh - 1) {
				l1[cl + col - 1] = 32;
				l2[cl + col - 1] = 32;
				l3[cl + col - 1] = 32;
				l4[cl + col - 1] = 32;
				l5[cl + col - 1] = 32;
			}
			cnt++;
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

void record_copper (uaecptr addr, int hpos, int vpos)
{
	int t = nr_cop_records[curr_cop_set];
	if (!cop_record[0]) {
		cop_record[0] = xmalloc (struct cop_rec, NR_COPPER_RECORDS);
		cop_record[1] = xmalloc (struct cop_rec, NR_COPPER_RECORDS);
	}
	if (t < NR_COPPER_RECORDS) {
		cop_record[curr_cop_set][t].addr = addr;
		cop_record[curr_cop_set][t].hpos = hpos;
		cop_record[curr_cop_set][t].vpos = vpos;
		nr_cop_records[curr_cop_set] = t + 1;
	}
	if (debug_copper & 2) { /* trace */
		debug_copper &= ~2;
		activate_debugger ();
	}
	if ((debug_copper & 4) && addr >= debug_copper_pc && addr <= debug_copper_pc + 3) {
		debug_copper &= ~4;
		activate_debugger ();
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
static void decode_copper_insn (FILE* file, unsigned long insn, unsigned long addr)
{
	struct cop_rec *cr = NULL;
	uae_u32 insn_type = insn & 0x00010001;
	TCHAR here = ' ';
	TCHAR record[] = _T("          ");

	if ((cr = find_copper_records (addr))) {
		_stprintf (record, _T(" [%03x %03x]"), cr->vpos, cr->hpos);
	}

	if (get_copper_address (-1) >= addr && get_copper_address(-1) <= addr + 3)
		here = '*';

	console_out_f (_T("%c%08lx: %04lx %04lx%s\t; "), here, addr, insn >> 16, insn & 0xFFFF, record);

	switch (insn_type) {
	case 0x00010000: /* WAIT insn */
		console_out (_T("Wait for "));
		disassemble_wait (file, insn);

		if (insn == 0xfffffffe)
			console_out (_T("                           \t; End of Copperlist\n"));

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
				console_out_f (_T("%s := 0x%04lx\n"), custd[i].name, insn & 0xffff);
			else
				console_out_f (_T("%04x := 0x%04lx\n"), addr, insn & 0xffff);
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
		insn = (chipmem_wget_indirect (address) << 16) | chipmem_wget_indirect (address + 2);
		decode_copper_insn (file, insn, address);
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
			b = get_byte (ts->addr);
		} else {
			b = get_word (ts->addr);
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
				*p1++ = get_byte (i);
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
			b = (uae_s8)get_byte (addr);
			b2 = (uae_s8)p1[addrcnt];
			addrmask = 1 << (addrcnt & 7);
		} else {
			b = (uae_s16)get_word (addr);
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
			} else if (nonmodified && b != b2) {
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
				if (get_byte (addr + i) != ((val >> shift) & 0xff))
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
static struct memwatch_node mwhit;

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
	if (currprefs.rtgmem_size)
		memset (illghdebug + (p96ram_start >> 16), 3, currprefs.rtgmem_size >> 16);

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
	if (uae_boot_rom) /* filesys "rom" */
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
				activate_debugger ();
		} else if (!(mask & 1) && (rwi & 1)) {
			console_out_f (_T("RO: %08X=%02X PC=%08X\n"), ad, v, pc);
			if (illgdebug_break)
				activate_debugger ();
		} else if (!(mask & 2) && (rwi & 2)) {
			console_out_f (_T("WO: %08X    PC=%08X\n"), ad, pc);
			if (illgdebug_break)
				activate_debugger ();
		}
	}
}

static int debug_mem_off (uaecptr addr)
{
	return munge24 (addr) >> 16;
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
	if (currprefs.z3fastmem_size)
		smc_size = currprefs.z3fastmem_start + currprefs.z3fastmem_size;
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
			activate_debugger ();
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

static int memwatch_func (uaecptr addr, int rwi, int size, uae_u32 *valp)
{
	int i, brk;
	uae_u32 val = *valp;

	if (illgdebug)
		illg_debug_do (addr, rwi, size, val);
	addr = munge24 (addr);
	if (smc_table && (rwi >= 2))
		smc_detector (addr, rwi, size, valp);
	for (i = 0; i < MEMWATCH_TOTAL; i++) {
		struct memwatch_node *m = &mwnodes[i];
		uaecptr addr2 = m->addr;
		uaecptr addr3 = addr2 + m->size;
		int rwi2 = m->rwi;
		uae_u32 oldval = 0;
		int isoldval = 0;

		brk = 0;
		if (m->size == 0)
			continue;
		if (!(rwi & rwi2))
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

		if (!m->frozen && m->val_enabled) {
			int trigger = 0;
			uae_u32 mask = (1 << (m->size * 8)) - 1;
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
				write_log (_T("%p %p %08x %08x %d\n"), addr, m->addr, *valp, mask, shift);
				return 1;
			}
			return 0;
		}
		//	if (!notinrom ())
		//	    return 1;
		mwhit.pc = M68K_GETPC;
		mwhit.addr = addr;
		mwhit.rwi = rwi;
		mwhit.size = size;
		mwhit.val = 0;
		if (mwhit.rwi & 2)
			mwhit.val = val;
		memwatch_triggered = i + 1;
		debugging = 1;
		set_special (SPCFLAG_BRK);
		return 1;
	}
	return 1;
}

static int mmu_hit (uaecptr addr, int size, int rwi, uae_u32 *v);

static uae_u32 REGPARAM2 mmu_lget (uaecptr addr)
{
	int off = debug_mem_off (addr);
	uae_u32 v = 0;
	if (!mmu_hit (addr, 4, 0, &v))
		v = debug_mem_banks[off]->lget (addr);
	return v;
}
static uae_u32 REGPARAM2 mmu_wget (uaecptr addr)
{
	int off = debug_mem_off (addr);
	uae_u32 v = 0;
	if (!mmu_hit (addr, 2, 0, &v))
		v = debug_mem_banks[off]->wget (addr);
	return v;
}
static uae_u32 REGPARAM2 mmu_bget (uaecptr addr)
{
	int off = debug_mem_off (addr);
	uae_u32 v = 0;
	if (!mmu_hit(addr, 1, 0, &v))
		v = debug_mem_banks[off]->bget (addr);
	return v;
}
static void REGPARAM2 mmu_lput (uaecptr addr, uae_u32 v)
{
	int off = debug_mem_off (addr);
	if (!mmu_hit (addr, 4, 1, &v))
		debug_mem_banks[off]->lput (addr, v);
}
static void REGPARAM2 mmu_wput (uaecptr addr, uae_u32 v)
{
	int off = debug_mem_off (addr);
	if (!mmu_hit (addr, 2, 1, &v))
		debug_mem_banks[off]->wput (addr, v);
}
static void REGPARAM2 mmu_bput (uaecptr addr, uae_u32 v)
{
	int off = debug_mem_off (addr);
	if (!mmu_hit (addr, 1, 1, &v))
		debug_mem_banks[off]->bput (addr, v);
}

static uae_u32 REGPARAM2 debug_lget (uaecptr addr)
{
	int off = debug_mem_off (addr);
	uae_u32 v;
	v = debug_mem_banks[off]->lget (addr);
	memwatch_func (addr, 1, 4, &v);
	return v;
}
static uae_u32 REGPARAM2 mmu_lgeti (uaecptr addr)
{
	int off = debug_mem_off (addr);
	uae_u32 v = 0;
	if (!mmu_hit (addr, 4, 4, &v))
		v = debug_mem_banks[off]->lgeti (addr);
	return v;
}
static uae_u32 REGPARAM2 mmu_wgeti (uaecptr addr)
{
	int off = debug_mem_off (addr);
	uae_u32 v = 0;
	if (!mmu_hit (addr, 2, 4, &v))
		v = debug_mem_banks[off]->wgeti (addr);
	return v;
}

static uae_u32 REGPARAM2 debug_wget (uaecptr addr)
{
	int off = debug_mem_off (addr);
	uae_u32 v;
	v = debug_mem_banks[off]->wget (addr);
	memwatch_func (addr, 1, 2, &v);
	return v;
}
static uae_u32 REGPARAM2 debug_bget (uaecptr addr)
{
	int off = debug_mem_off (addr);
	uae_u32 v;
	v = debug_mem_banks[off]->bget (addr);
	memwatch_func (addr, 1, 1, &v);
	return v;
}
static uae_u32 REGPARAM2 debug_lgeti (uaecptr addr)
{
	int off = debug_mem_off (addr);
	uae_u32 v;
	v = debug_mem_banks[off]->lgeti (addr);
	memwatch_func (addr, 4, 4, &v);
	return v;
}
static uae_u32 REGPARAM2 debug_wgeti (uaecptr addr)
{
	int off = debug_mem_off (addr);
	uae_u32 v;
	v = debug_mem_banks[off]->wgeti (addr);
	memwatch_func (addr, 4, 2, &v);
	return v;
}
static void REGPARAM2 debug_lput (uaecptr addr, uae_u32 v)
{
	int off = debug_mem_off (addr);
	if (memwatch_func (addr, 2, 4, &v))
		debug_mem_banks[off]->lput (addr, v);
}
static void REGPARAM2 debug_wput (uaecptr addr, uae_u32 v)
{
	int off = debug_mem_off (addr);
	if (memwatch_func (addr, 2, 2, &v))
		debug_mem_banks[off]->wput (addr, v);
}
static void REGPARAM2 debug_bput (uaecptr addr, uae_u32 v)
{
	int off = debug_mem_off (addr);
	if (memwatch_func (addr, 2, 1, &v))
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

uae_u16 debug_wputpeekdma (uaecptr addr, uae_u32 v)
{
	if (!memwatch_enabled)
		return v;
	memwatch_func (addr, 2, 2, &v);
	return v;
}
uae_u16 debug_wgetpeekdma (uaecptr addr, uae_u32 v)
{
	uae_u32 vv = v;
	if (!memwatch_enabled)
		return v;
	memwatch_func (addr, 1, 2, &vv);
	return vv;
}

void debug_putlpeek (uaecptr addr, uae_u32 v)
{
	if (!memwatch_enabled)
		return;
	memwatch_func (addr, 2, 4, &v);
}
void debug_wputpeek (uaecptr addr, uae_u32 v)
{
	if (!memwatch_enabled)
		return;
	memwatch_func (addr, 2, 2, &v);
}
void debug_bputpeek (uaecptr addr, uae_u32 v)
{
	if (!memwatch_enabled)
		return;
	memwatch_func (addr, 2, 1, &v);
}
void debug_bgetpeek (uaecptr addr, uae_u32 v)
{
	uae_u32 vv = v;
	if (!memwatch_enabled)
		return;
	memwatch_func (addr, 1, 1, &vv);
}
void debug_wgetpeek (uaecptr addr, uae_u32 v)
{
	uae_u32 vv = v;
	if (!memwatch_enabled)
		return;
	memwatch_func (addr, 1, 2, &vv);
}
void debug_lgetpeek (uaecptr addr, uae_u32 v)
{
	uae_u32 vv = v;
	if (!memwatch_enabled)
		return;
	memwatch_func (addr, 1, 4, &vv);
}

struct membank_store
{
	addrbank *addr;
	addrbank store;
};

static struct membank_store *membank_stores;

static int deinitialize_memwatch (void)
{
	int i, oldmode;

	if (!memwatch_enabled && !mmu_enabled)
		return -1;
	for (i = 0; membank_stores[i].addr; i++) {
		memcpy (membank_stores[i].addr, &membank_stores[i].store, sizeof (addrbank));
	}
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
	int i, j, as;
	addrbank *a1, *a2, *oa;

	deinitialize_memwatch ();
	as = currprefs.address_space_24 ? 256 : 65536;
	debug_mem_banks = xmalloc (addrbank*, as);
	debug_mem_area = xmalloc (addrbank, as);
	membank_stores = xcalloc (struct membank_store, 32);
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
	if (mode >= 0)
		initialize_memwatch (mode);
	return -1;
}

static TCHAR *getsizechar (int size)
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
			mwn = &mwnodes[i];
			if (mwn->size == 0)
				continue;
			buf = buf_out (buf, &bufsize, _T("%2d: %08X - %08X (%d) %c%c%c"),
				i, mwn->addr, mwn->addr + (mwn->size - 1), mwn->size,
				(mwn->rwi & 1) ? 'R' : ' ', (mwn->rwi & 2) ? 'W' : ' ', (mwn->rwi & 4) ? 'I' : ' ');
			if (mwn->frozen)
				buf = buf_out (buf, &bufsize, _T("F"));
			if (mwn->val_enabled)
				buf = buf_out (buf, &bufsize, _T(" =%X%s"), mwn->val, getsizechar (mwn->val_size));
			if (mwn->modval_written)
				buf = buf_out (buf, &bufsize, _T(" =M"));
			if (mwn->mustchange)
				buf = buf_out (buf, &bufsize, _T(" C"));
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
		return;
	}
	mwn->addr = readhex (c);
	mwn->size = 1;
	mwn->rwi = 7;
	mwn->val_enabled = 0;
	mwn->val_mask = 0xffffffff;
	mwn->frozen = 0;
	mwn->modval_written = 0;
	ignore_ws (c);
	if (more_params (c)) {
		mwn->size = readhex (c);
		ignore_ws (c);
		if (more_params (c)) {
			for (;;) {
				TCHAR ncc = peek_next_char(c);
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
				if (!more_params(c))
					break;
			}
			ignore_ws (c);
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
	if (mwn->frozen && mwn->rwi == 0)
		mwn->rwi = 3;
	memwatch_dump (num);
}

static void writeintomem (TCHAR **c)
{
	uae_u32 addr = 0;
	uae_u32 val = 0;
	TCHAR cc;
	int len = 1;

	ignore_ws(c);
	addr = readhex (c);

	ignore_ws (c);
	if (!more_params (c))
		return;
	cc = peekchar (c);
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
			console_out_f (_T("Wrote %X (%u) at %08X.%c\n"), val, val, addr, cc);
			addr += len;
		}
	}
}

static uae_u8 *dump_xlate (uae_u32 addr)
{
	if (!mem_banks[addr >> 16]->check (addr, 1))
		return NULL;
	return mem_banks[addr >> 16]->xlateaddr (addr);
}

static void memory_map_dump_2 (int log)
{
	bool imold;
	int i, j, max;
	addrbank *a1 = mem_banks[0];
	TCHAR txt[256];

	imold = currprefs.illegal_mem;
	currprefs.illegal_mem = false;
	max = currprefs.address_space_24 ? 256 : 65536;
	j = 0;
	for (i = 0; i < max + 1; i++) {
		addrbank *a2 = NULL;
		if (i < max)
			a2 = mem_banks[i];
		if (a1 != a2) {
			int k, mirrored, size, size_out;
			TCHAR size_ext;
			uae_u8 *caddr;
			TCHAR *name;
			TCHAR tmp[MAX_DPATH];

			name = a1->name;
			if (name == NULL)
				name = _T("<none>");

			k = j;
			caddr = dump_xlate (k << 16);
			mirrored = caddr ? 1 : 0;
			k++;
			while (k < i && caddr) {
				if (dump_xlate (k << 16) == caddr)
					mirrored++;
				k++;
			}
			size = (i - j) << (16 - 10);
			size_out = size;
			size_ext = 'K';
			if (j >= 256) {
				size_out /= 1024;
				size_ext = 'M';
			}
			_stprintf (txt, _T("%08X %7d%c/%d = %7d%c %s"), j << 16, size_out, size_ext,
				mirrored, mirrored ? size_out / mirrored : size_out, size_ext, name);

			tmp[0] = 0;
			if (a1->flags == ABFLAG_ROM && mirrored) {
				TCHAR *p = txt + _tcslen (txt);
				uae_u32 crc = get_crc32 (a1->xlateaddr(j << 16), (size * 1024) / mirrored);
				struct romdata *rd = getromdatabycrc (crc);
				_stprintf (p, _T(" (%08X)"), crc);
				if (rd) {
					tmp[0] = '=';
					getromname (rd, tmp + 1);
					_tcscat (tmp, _T("\n"));
				}
			}
			_tcscat (txt, _T("\n"));
			if (log)
				write_log (txt);
			else
				console_out (txt);
			if (tmp[0]) {
				if (log)
					write_log (tmp);
				else
					console_out (tmp);
			}
			j = i;
			a1 = a2;
		}
	}
	currprefs.illegal_mem = imold;
}
void memory_map_dump (void)
{
	memory_map_dump_2 (1);
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

static void print_task_info (uaecptr node)
{
	TCHAR *s;
	int process = get_byte (node + 8) == 13 ? 1 : 0;

	console_out_f (_T("%08X: "), node);
	s = au ((char*)get_real_address (get_long (node + 10)));
	console_out_f (process ? _T(" PROCESS '%s'\n") : _T(" TASK    '%s'\n"), s);
	xfree (s);
	if (process) {
		uaecptr cli = BPTR2APTR (get_long (node + 172));
		int tasknum = get_long (node + 140);
		if (cli && tasknum) {
			uae_u8 *command_bstr = get_real_address (BPTR2APTR (get_long (cli + 16)));
			TCHAR *command = BSTR2CSTR (command_bstr);
			console_out_f (_T(" [%d, '%s']\n"), tasknum, command);
			xfree (command);
		} else {
			console_out (_T("\n"));
		}
	}
}

static void show_exec_tasks (void)
{
	uaecptr execbase = get_long (4);
	uaecptr taskready = get_long (execbase + 406);
	uaecptr taskwait = get_long (execbase + 420);
	uaecptr node, end;
	console_out_f (_T("Execbase at 0x%08X\n"), execbase);
	console_out (_T("Current:\n"));
	node = get_long (execbase + 276);
	print_task_info (node);
	console_out_f (_T("Ready:\n"));
	node = get_long (taskready);
	end = get_long (taskready + 4);
	while (node) {
		print_task_info (node);
		node = get_long (node);
	}
	console_out (_T("Waiting:\n"));
	node = get_long (taskwait);
	end = get_long (taskwait + 4);
	while (node) {
		print_task_info (node);
		node = get_long (node);
	}
}

static uaecptr get_base (const uae_char *name)
{
	uaecptr v = get_long (4);
	addrbank *b = &get_mem_bank(v);

	if (!b || !b->check (v, 400) || b->flags != ABFLAG_RAM)
		return 0;
	v += 378; // liblist
	while (v = get_long (v)) {
		uae_u32 v2;
		uae_u8 *p;
		b = &get_mem_bank (v);
		if (!b || !b->check (v, 32) || b->flags != ABFLAG_RAM)
			goto fail;
		v2 = get_long (v + 10); // name
		b = &get_mem_bank (v2);
		if (!b || !b->check (v2, 20))
			goto fail;
		if (b->flags == ABFLAG_ROM || b->flags == ABFLAG_RAM) {
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
	uae_u8 *p = get_real_address ((uaecptr)(pp << 2));
	TCHAR *s = xmalloc (TCHAR, p[0] + 1);
	return au_copy (s, p[0] + 1, (char*)p + 1);
}

static void show_exec_lists (TCHAR t)
{
	uaecptr execbase = get_long (4);
	uaecptr list = 0, node;

	if (_totupper (t) == 'O') { // doslist
		uaecptr dosbase = get_base ("dos.library");
		if (dosbase) {
			uaecptr rootnode = get_long (dosbase + 34);
			uaecptr dosinfo = get_long (rootnode + 24) << 2;
			console_out_f (_T("ROOTNODE: %08x DOSINFO: %08x\n"), rootnode, dosinfo);
			uaecptr doslist = get_long (dosinfo + 4) << 2;
			while (doslist) {
				int type = get_long (doslist + 4);
				uaecptr msgport = get_long (doslist + 8);
				TCHAR *name = getfrombstr (get_long (doslist + 40));
				console_out_f (_T("%08x: %d %08x '%s'\n"), doslist, type, msgport, name);
				if (type == 0) {
					console_out_f (_T(" - H=%08x Stack=%5d Pri=%2d Start=%08x Seg=%08x GV=%08x\n"),
						get_long (doslist + 16) << 2, get_long (doslist + 20),
						get_long (doslist + 24), get_long (doslist + 28),
						get_long (doslist + 32) << 2, get_long (doslist + 36));
				}
				xfree (name);
				doslist = get_long (doslist) << 2;
			}
		} else {
			console_out_f (_T("can't find dos.library\n"));
		}
		return;
	} else if (_totupper (t) == 'I') { // interrupts
		static const int it[] = {  1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0 };
		static const int it2[] = { 1, 1, 1, 2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 6, 6, 7 };
		list = execbase + 84;
		for (int i = 0; i < 16; i++) {
			console_out_f (_T("%2d %d: %08x\n"), i + 1, it2[i], list);
			if (it[i]) {
				console_out_f (_T("  [H] %08x\n"), get_long (list));
				node = get_long (list + 8);
				if (node) {
					uae_u8 *addr = get_real_address (get_long (node + 10));
					TCHAR *name = addr ? au ((char*)addr) : au("<null>");
					console_out_f (_T("      %08x (C=%08X D=%08X) '%s'\n"), node, get_long (list + 4), get_long (list), name);
					xfree (name);
				}
			} else {
				int cnt = 0;
				node = get_long (list);
				node = get_long (node);
				while (get_long (node)) {
					uae_u8 *addr = get_real_address (get_long (node + 10));
					TCHAR *name = addr ? au ((char*)addr) : au("<null>");
					console_out_f (_T("  [S] %08x (C=%08x D=%08X) '%s'\n"), node, get_long (node + 18), get_long (node + 14), name);
					if (i == 4 - 1 || i == 14 - 1) {
						if (!_tcsicmp (name, _T("cia-a")) || !_tcsicmp (name, _T("cia-b"))) {
							static const TCHAR *ciai[] = { _T("A"), _T("B"), _T("ALRM"), _T("SP"), _T("FLG") };
							uaecptr cia = node + 22;
							for (int j = 0; j < 5; j++) {
								uaecptr ciap = get_long (cia);
								console_out_f (_T("        %5s: %08x"), ciai[j], ciap);
								if (ciap) {
									uae_u8 *addr2 = get_real_address (get_long (ciap + 10));
									TCHAR *name2 = addr ? au ((char*)addr2) : au("<null>");
									console_out_f (_T(" (C=%08x D=%08X) '%s'"), get_long (ciap + 18), get_long (ciap + 14), name2);
									xfree (name2);
								}
								console_out_f (_T("\n"));
								cia += 4;
							}
						}
					}
					xfree (name);
					node = get_long (node);
					cnt++;
				}
				if (!cnt)
					console_out_f (_T("  [S] <none>\n"));
			}
			list += 12;
		}
		return;
	}

	switch (_totupper (t))
	{
	case 'R':
		list = execbase + 336;
		break;
	case 'D':
		list = execbase + 350;
		break;
	case 'L':
		list = execbase + 378;
		break;
	}
	if (list == 0)
		return;
	node = get_long (list);
	while (get_long (node)) {
		TCHAR *name = au ((char*)get_real_address (get_long (node + 10)));
		console_out_f (_T("%08x %s\n"), node, name);
		xfree (name);
		node = get_long (node);
	}
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
		if (nc == 'S') {
			next_char (c);
			sr_bpvalue = sr_bpmask = 0;
			if (more_params (c)) {
				sr_bpmask = 0xffff;
				sr_bpvalue = readhex (c);
				if (more_params (c))
					sr_bpmask = readhex (c);
			}
			console_out_f (_T("SR breakpoint, value=%04X, mask=%04X\n"), sr_bpvalue, sr_bpmask);
			return 0;
		} else if (nc == 'I') {
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
			console_out (_T("All breakpoints removed\n"));
			return 0;
		} else if (nc == 'L') {
			int got = 0;
			for (i = 0; i < BREAKPOINT_TOTAL; i++) {
				bpn = &bpnodes[i];
				if (!bpn->enabled)
					continue;
				console_out_f (_T("%8X "), bpn->addr);
				got = 1;
			}
			if (!got)
				console_out (_T("No breakpoints\n"));
			else
				console_out (_T("\n"));
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
					console_out (_T("Breakpoint removed\n"));
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
				console_out (_T("Breakpoint added\n"));
				skipaddr_start = 0xffffffff;
				skipaddr_doskip = 0;
				break;
			}
			return 0;
		}
	}
#if 0
	if (skipaddr_start == 0xC0DEDBAD) {
		trace_same_insn_count = 0;
		logfile = fopen ("uae.trace", "w");
		memcpy (trace_insn_copy, regs.pc_p, 10);
		memcpy (&trace_prev_regs, &regs, sizeof regs);
	}
#endif
	do_skip = 1;
	skipaddr_doskip = -1;
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
	do_skip = 1;
	skipaddr_doskip = 1;
	skipaddr_start = 0;
	return 1;
}

static void savemem (TCHAR **cc)
{
	uae_u8 b;
	uae_u32 src, src2, len, len2;
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
	if (!more_params (cc))
		goto S_argh;
	len2 = len = readhex (cc);
	fp = _tfopen (name, _T("wb"));
	if (fp == NULL) {
		console_out_f (_T("Couldn't open file '%s'\n"), name);
		return;
	}
	while (len > 0) {
		b = get_byte (src);
		src++;
		len--;
		if (fwrite (&b, 1, 1, fp) != 1) {
			console_out (_T("Error writing file\n"));
			break;
		}
	}
	fclose (fp);
	if (len == 0)
		console_out_f (_T("Wrote %08X - %08X (%d bytes) to '%s'\n"),
		src2, src2 + len2, len2, name);
	return;
S_argh:
	console_out (_T("S-command needs more arguments!\n"));
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
		activate_debugger ();
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
		w1 = get_word (addr);
		w2 = get_word (addr + size * 2);
		console_out_f (_T("    %06X "), addr);
		for (i = 0; i < size * 2; i++)
			console_out_f (_T("%04X "), get_word (addr + i * 2));
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
				w1 = get_word (addr);
				w2 = get_word (addr + 2);
				if (addr2) {
					ww1 = get_word (addr2);
					ww2 = get_word (addr2 + 2);
				}
			} else if (size == 2) {
				w1 = get_long (addr);
				w2 = get_long (addr + 4);
				if (addr2) {
					ww1 = get_long (addr2);
					ww2 = get_long (addr2 + 4);
				}
			} else if (size == 4) {
				w1 = get_long (addr + 0);
				w2 = get_long (addr + 8);
				w1 <<= 32;
				w2 <<= 32;
				w1 |= get_long (addr + 4);
				w2 |= get_long (addr + 12);
				if (addr2) {
					ww1 = get_long (addr2 + 0);
					ww2 = get_long (addr2 + 8);
					ww1 <<= 32;
					ww2 <<= 32;
					ww1 |= get_long (addr2 + 4);
					ww2 |= get_long (addr2 + 12);
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
		if (get_word (addr) == 0 && get_word (addr + size * 4) == 0)
			break;
		max--;
		if (max <= 0)
			break;
	}

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
	uaecptr addr, end;
	int hits = 0;

	addr = 0;
	end = lastaddr();
	ea = readhex (inptr);
	if (more_params(inptr)) {
		addr = readhex (inptr);
		if (more_params(inptr))
			end = readhex (inptr);
	}
	console_out_f (_T("Searching from %08X to %08X\n"), addr, end);
	while((addr = nextaddr (addr, end, &end)) != 0xffffffff) {
		if ((addr & 1) == 0 && addr + 6 <= end) {
			sea = 0xffffffff;
			dea = 0xffffffff;
			m68k_disasm_ea (NULL, addr, NULL, 1, &sea, &dea);
			if (ea == sea || ea == dea) {
				m68k_disasm (stdout, addr, NULL, 1);
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

static uaecptr nxdis, nxmem;

static BOOL debug_line (TCHAR *input)
{
	TCHAR cmd, *inptr;
	uaecptr addr;

	inptr = input;
	cmd = next_char (&inptr);

	switch (cmd)
	{
		case 'c': dumpcia (); dumpdisk (); dumpcustom (); break;
		case 'i':
		{
			if (*inptr == 'l') {
				next_char (&inptr);
				if (more_params (&inptr)) {
					debug_illegal_mask = readhex (&inptr);
				} else {
					debug_illegal_mask = debug_illegal ? 0 : -1;
					debug_illegal_mask &= ~((uae_u64)255 << 24); // mask interrupts
				}
				console_out_f (_T("Exception breakpoint mask: %0I64X\n"), debug_illegal_mask);
				debug_illegal = debug_illegal_mask ? 1 : 0;
			} else {
				addr = 0xffffffff;
				if (more_params (&inptr))
					addr = readhex (&inptr);
				dump_vectors (addr);
			}
			break;
		}
		case 'e': dump_custom_regs (tolower(*inptr) == 'a'); break;
		case 'r':
			{
				if (more_params(&inptr))
					m68k_modify (&inptr);
				else
					m68k_dumpstate (stdout, &nextpc);
			}
			break;
		case 'D': deepcheatsearch (&inptr); break;
		case 'C': cheatsearch (&inptr); break;
		case 'W': writeintomem (&inptr); break;
		case 'w': memwatch (&inptr); break;
		case 'S': savemem (&inptr); break;
		case 's':
			if (*inptr == 'c') {
				screenshot (1, 1);
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
					if (more_params (&inptr))
						daddr = readhex (&inptr);
					else
						daddr = nxdis;
					if (more_params (&inptr))
						count = readhex (&inptr);
					else
						count = 10;
					m68k_disasm (stdout, daddr, &nxdis, count);
				}
			}
			break;
		case 'T':
			if (inptr[0] == 't' || inptr[0] == 0)
				show_exec_tasks ();
			else
				show_exec_lists (inptr[0]);
			break;
		case 't':
			no_trace_exceptions = 0;
			if (*inptr != 't') {
				if (more_params (&inptr))
					skipaddr_doskip = readint (&inptr);
				if (skipaddr_doskip <= 0 || skipaddr_doskip > 10000)
					skipaddr_doskip = 1;
			} else {
				no_trace_exceptions = 1;
			}
			set_special (SPCFLAG_BRK);
			exception_debugging = 1;
			return true;
		case 'z':
			skipaddr_start = nextpc;
			skipaddr_doskip = 1;
			do_skip = 1;
			exception_debugging = 1;
			return true;

		case 'f':
			if (inptr[0] == 'a') {
				next_char (&inptr);
				find_ea (&inptr);
			} else if (inptr[0] == 'p') {
				inptr++;
				if (process_breakpoint (&inptr))
					return true;
			} else {
				if (instruction_breakpoint (&inptr))
					return true;
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
				while (temp != lasthist) {
					regs = history[temp];
					if (history[temp].pc == addr || addr == 0) {
						m68k_setpc (history[temp].pc);
						if (badly)
							m68k_dumpstate (stdout, NULL);
						else
							m68k_disasm (stdout, history[temp].pc, NULL, 1);
						if (addr && history[temp].pc == addr)
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
						notice_screen_contents_lost ();
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
				if (more_params (&inptr))
					v1 = readint (&inptr);
				if (more_params (&inptr))
					v2 = readint (&inptr);
				if (debug_dma) {
					decode_dma_record (v2, v1, cmd == 'v', false);
				} else {
					debug_dma = v1 < 0 ? -v1 : 1;
					console_out_f (_T("DMA debugger enabled, mode=%d.\n"), debug_dma);
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
		case 'U':
			if (currprefs.cpu_model && more_params (&inptr)) {
				int i;
				uaecptr addrl = readhex (&inptr);
				uaecptr addrp;
				console_out_f (_T("%08X translates to:\n"), addrl);
				for (i = 0; i < 4; i++) {
					bool super = (i & 2) != 0;
					bool data = (i & 1) != 0;
					console_out_f (_T("S%dD%d="), super, data);
					TRY(prb) {
						addrp = mmu_translate (addrl, super, data, false);
						console_out_f (_T("%08X"), addrp);
						TRY(prb2) {
							addrp = mmu_translate (addrl, super, data, true);
							console_out_f (_T(" RW"));
						} CATCH(prb2) {
							console_out_f (_T(" RO"));
						}
					} CATCH(prb) {
						console_out_f (_T("***********"));
					}
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

	m68k_dumpstate (stdout, &nextpc);
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
	uae_u32 pc = m68k_getpc ();
	//    if (!notinrom())
	//	return;
	history[lasthist] = regs;
	history[lasthist].pc = m68k_getpc ();
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
		if (do_skip) {
			uae_u32 pc;
			uae_u16 opcode;
			int bp = 0;

			pc = munge24 (m68k_getpc ());
			opcode = currprefs.cpu_model < 68020 && (currprefs.cpu_compatible || currprefs.cpu_cycle_exact) ? regs.ir : get_word (pc);

			for (i = 0; i < BREAKPOINT_TOTAL; i++) {
				if (!bpnodes[i].enabled)
					continue;
				if (bpnodes[i].addr == pc) {
					bp = 1;
					console_out_f (_T("Breakpoint at %08X\n"), pc);
					break;
				}
			}

			if (skipaddr_doskip) {
				if (skipaddr_start == pc)
					bp = 1;
				if ((processptr || processname) && notinrom()) {
					uaecptr execbase = get_long (4);
					uaecptr activetask = get_long (execbase + 276);
					int process = get_byte (activetask + 8) == 13 ? 1 : 0;
					char *name = (char*)get_real_address (get_long (activetask + 10));
					if (process) {
						uaecptr cli = BPTR2APTR(get_long (activetask + 172));
						uaecptr seglist = 0;

						uae_char *command = NULL;
						if (cli) {
							if (processname)
								command = (char*)get_real_address (BPTR2APTR(get_long (cli + 16)));
							seglist = BPTR2APTR(get_long (cli + 60));
						} else {
							seglist = BPTR2APTR(get_long (activetask + 128));
							seglist = BPTR2APTR(get_long (seglist + 12));
						}
						if (activetask == processptr || (processname && (!stricmp (name, processname) || (command && command[0] && !strnicmp (command + 1, processname, command[0]) && processname[command[0]] == 0)))) {
							while (seglist) {
								uae_u32 size = get_long (seglist - 4) - 4;
								if (pc >= (seglist + 4) && pc < (seglist + size)) {
									bp = 1;
									break;
								}
								seglist = BPTR2APTR(get_long (seglist));
							}
						}
					}
				} else if (skipins != 0xffffffff) {
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
			if (sr_bpmask || sr_bpvalue) {
				MakeSR ();
				if ((regs.sr & sr_bpmask) == sr_bpvalue) {
					console_out (_T("SR breakpoint\n"));
					bp = 1;
				}
			}
			if (!bp) {
				set_special (SPCFLAG_BRK);
				return;
			}
		}
	} else {
		console_out_f (_T("Memwatch %d: break at %08X.%c %c%c%c %08X PC=%08X\n"), memwatch_triggered - 1, mwhit.addr,
			mwhit.size == 1 ? 'B' : (mwhit.size == 2 ? 'W' : 'L'),
			(mwhit.rwi & 1) ? 'R' : ' ', (mwhit.rwi & 2) ? 'W' : ' ', (mwhit.rwi & 4) ? 'I' : ' ',
			mwhit.val, mwhit.pc);
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
	setmouseactive (0);
	do_skip = 0;
	skipaddr_start = 0xffffffff;
	skipaddr_end = 0xffffffff;
	skipins = 0xffffffff;
	skipaddr_doskip = 0;
	exception_debugging = 0;
	debug_rewind = 0;
	processptr = 0;
#if 0
	if (!currprefs.statecapture) {
		changed_prefs.statecapture = currprefs.statecapture = 1;
		savestate_init ();
	}
#endif
	debug_1 ();
	if (!debug_rewind && !currprefs.cachesize
#ifdef FILESYS
		&& nr_units () == 0
#endif
		) {
			savestate_capture (1);
	}
	for (i = 0; i < BREAKPOINT_TOTAL; i++) {
		if (bpnodes[i].enabled)
			do_skip = 1;
	}
	if (sr_bpmask || sr_bpvalue)
		do_skip = 1;
	if (do_skip) {
		set_special (SPCFLAG_BRK);
		m68k_resumestopped ();
		debugging = 1;
	}
	resume_sound ();
	inputdevice_acquire (TRUE);
}

const TCHAR *debuginfo (int mode)
{
	static TCHAR txt[100];
	uae_u32 pc = M68K_GETPC;
	_stprintf (txt, _T("PC=%08X INS=%04X %04X %04X"),
		pc, get_word (pc), get_word (pc + 2), get_word (pc + 4));
	return txt;
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
	put_long (m68k_areg (regs, 7), get_long (p - 4));
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
							*v = get_long (maddr);
							break;
						case 2:
							*v = get_word (maddr);
							break;
						case 1:
							*v = get_byte (maddr);
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

static int getmmubank(struct mmudata *snptr, uaecptr p)
{
	snptr->flags = get_long (p);
	if (snptr->flags == 0xffffffff)
		return 1;
	snptr->addr = get_long (p + 4);
	snptr->len = get_long (p + 8);
	snptr->remap = get_long (p + 12);
	snptr->p_addr = p;
	return 0;
}

int mmu_init(int mode, uaecptr parm, uaecptr parm2)
{
	uaecptr p, p2, banks;
	int size;
	struct mmudata *snptr;
	struct mmunode *mn;
	static int wasjit;
#ifdef JIT
	if (currprefs.cachesize) {
		wasjit = currprefs.cachesize;
		changed_prefs.cachesize = 0;
		console_out (_T("MMU: JIT disabled\n"));
		check_prefs_changed_comp ();
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
	if (get_long (p) != 1) {
		console_out_f (_T("MMU: version mismatch %d <> %d\n"), get_long (p), 1);
		return 0;
	}
	p += 4;
	mmu_logging = get_long (p) & 1;
	p += 4;
	mmu_callback = get_long (p);
	p += 4;
	mmu_regs = get_long (p);
	p += 4;

	if (mode == 3) {
		int off;
		uaecptr addr = get_long (parm2 + 4);
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
	p2 = get_long (p);
	while (get_long (p2) != 0xffffffff) {
		p2 += 16;
		size++;
	}
	p = banks = get_long (p);
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
	if (out == NULL || outsize == 0)
		setconsolemode (empty, 1);
	else
		setconsolemode (out, outsize);
	debug_line (input);
	setconsolemode (NULL, 0);
	xfree (input);
}
