/*
* UAE - The Un*x Amiga Emulator
*
* X86 Bridge board emulation
* A1060, A2088, A2088T
*
* Copyright 2015 Toni Wilen
* x86 and PC support chip emulation from Fake86.
*
*/

#define X86_DEBUG_BRIDGE 1
#define FLOPPY_IO_DEBUG 0
#define X86_DEBUG_BRIDGE_IO 0

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "custom.h"
#include "memory.h"
#include "debug.h"
#include "x86.h"
#include "newcpu.h"
#include "uae.h"
#include "rommgr.h"
#include "autoconf.h"
#include "zfile.h"
#include "disk.h"

#define TYPE_SIDECAR 0
#define TYPE_2088 1
#define TYPE_2088T 2

void x86_init(unsigned char *memp, unsigned char *iop);
void x86_reset(void);
int x86_execute(void);

void exec86(uint32_t execloops);
void reset86(int v20);
void intcall86(uint8_t intnum);

static void doirq(uint8_t irqnum);

static frame_time_t last_cycles;

struct x86_bridge
{
	uae_u8 acmemory[128];
	addrbank *bank;
	int configured;
	int type;
	uaecptr baseaddress;
	uae_u8 *pc_ram;
	uae_u8 *io_ports;
	uae_u8 *amiga_io;
	bool x86_reset;
	bool amiga_irq;
	bool amiga_forced_interrupts;
	bool pc_irq3a, pc_irq3b, pc_irq7;
	uae_u8 pc_jumpers;
	int pc_maxram;
	int settings;
};

#define X86_BRIDGE_A1060 0
#define X86_BRIDGE_MAX (X86_BRIDGE_A1060 + 1)

#define ACCESS_MODE_BYTE 0
#define ACCESS_MODE_WORD 1
#define ACCESS_MODE_GFX 2
#define ACCESS_MODE_IO 3

#define IO_AMIGA_INTERRUPT_STATUS 0x1ff1
#define IO_PC_INTERRUPT_STATUS 0x1ff3
#define IO_NEGATE_PC_RESET 0x1ff5
#define IO_MODE_REGISTER 0x1ff7
#define IO_INTERRUPT_MASK 0x1ff9
#define IO_PC_INTERRUPT_CONTROL 0x1ffb
#define IO_CONTROL_REGISTER 0x1ffd
#define IO_KEYBOARD_REGISTER_A1000 0x61f
#define IO_KEYBOARD_REGISTER_A2000 0x1fff

static struct x86_bridge *bridges[X86_BRIDGE_MAX];

static struct x86_bridge *x86_bridge_alloc(void)
{
	struct x86_bridge *xb = xcalloc(struct x86_bridge, 1);
	return xb;
};

static uae_u8 get_mode_register(struct x86_bridge *xb, uae_u8 v)
{
	if (xb->type == TYPE_SIDECAR) {
		v = 0x84;
		if (!(xb->settings & (1 << 12)))
			v |= 0x02;
		if (!(xb->settings & (1 << 8)))
			v |= 0x08;
		if (!(xb->settings & (1 << 9)))
			v |= 0x10;
		if ((xb->settings & (1 << 10)))
			v |= 0x20;
		if ((xb->settings & (1 << 11)))
			v |= 0x40;
	} else {
		v |= 0x80;
	}
	return v;
}

static uae_u8 x86_bridge_put_io(struct x86_bridge *xb, uaecptr addr, uae_u8 v)
{
#if X86_DEBUG_BRIDGE_IO
	write_log(_T("IO write %08x %02x\n"), addr, v);
#endif

	switch (addr)
	{
		// read-only
		case IO_AMIGA_INTERRUPT_STATUS:
		v = xb->amiga_io[addr];
		break;
		case IO_PC_INTERRUPT_STATUS:
		v = xb->amiga_io[addr];
#if X86_DEBUG_BRIDGE_IRQ
		write_log(_T("IO_PC_INTERRUPT_STATUS %02x\n"), v);
#endif
		break;
		case IO_NEGATE_PC_RESET:
		v = xb->amiga_io[addr];
		break;
		case IO_PC_INTERRUPT_CONTROL:
		if ((v & 1) || (v & (2 | 4 | 8)) != (2 | 4 | 8)) {
#if X86_DEBUG_BRIDGE_IRQ
			write_log(_T("IO_PC_INTERRUPT_CONTROL %02x\n"), v);
#endif
			if (xb->amiga_forced_interrupts) {
				if (v & 1)
					doirq(1);
				if (xb->amiga_io[IO_CONTROL_REGISTER] & 8) {
					if (!(v & 2) || !(v & 4))
						doirq(3);
					if (!(v & 8))
						doirq(7);
				}
			}
		}
		break;
		case IO_CONTROL_REGISTER:
		if (!(v & 0x4)) {
			xb->x86_reset = true;
		}
		if (!(v & 1))
			v |= 2;
		else if (!(v & 2))
			v |= 1;
#if X86_DEBUG_BRIDGE_IO
		write_log(_T("IO_CONTROL_REGISTER %02x\n"), v);
#endif
		break;
		case IO_KEYBOARD_REGISTER_A1000:
		if (xb->type == TYPE_SIDECAR) {
#if X86_DEBUG_BRIDGE_IO
			write_log(_T("IO_KEYBOARD_REGISTER %02x\n"), v);
#endif
			xb->io_ports[0x60] = v;
		}
		break;
		case IO_KEYBOARD_REGISTER_A2000:
		if (xb->type >= TYPE_2088) {
#if X86_DEBUG_BRIDGE_IO
			write_log(_T("IO_KEYBOARD_REGISTER %02x\n"), v);
#endif
			xb->io_ports[0x60] = v;
		}
		break;
		case IO_MODE_REGISTER:
		v = get_mode_register(xb, v);
#if X86_DEBUG_BRIDGE_IO
		write_log(_T("IO_MODE_REGISTER %02x\n"), v);
#endif
		break;
	}

	xb->amiga_io[addr] = v;
	return v;
}
static uae_u8 x86_bridge_get_io(struct x86_bridge *xb, uaecptr addr)
{
	uae_u8 v = xb->amiga_io[addr];

	v = xb->amiga_io[addr];

	switch(addr)
	{
		case IO_AMIGA_INTERRUPT_STATUS:
		xb->amiga_io[addr] = 0;
#if X86_DEBUG_BRIDGE_IRQ
		if (v)
			write_log(_T("IO_AMIGA_INTERRUPT_STATUS %02x. CLEARED.\n"), v);
#endif
		break;
		case IO_PC_INTERRUPT_STATUS:
		v |= 0xf0;
#if X86_DEBUG_BRIDGE_IRQ
		write_log(_T("IO_PC_INTERRUPT_STATUS %02x\n"), v);
#endif
		break;
		case IO_NEGATE_PC_RESET:
		{
			if (xb->x86_reset) {
				write_log(_T("x86 CPU start!\n"));
				reset86(xb->type == TYPE_2088T);
				xb->x86_reset = false;
			}
			// because janus.library has stupid CPU loop wait
			int vp = vpos;
			while (vp == vpos) {
				x_do_cycles(maxhpos * CYCLE_UNIT);
			}
		}
		break;
		case IO_MODE_REGISTER:
		v = get_mode_register(xb, v);
#if X86_DEBUG_BRIDGE_IO
		write_log(_T("IO_MODE_REGISTER %02x\n"), v);
#endif
		break;
	}

	return v;
}

static void set_interrupt(struct x86_bridge *xb, int bit)
{
	if (xb->amiga_io[IO_AMIGA_INTERRUPT_STATUS] & (1 << bit))
		return;
#if X86_DEBUG_BRIDGE_IRQ
	write_log(_T("IO_AMIGA_INTERRUPT_STATUS set bit %d\n"), bit);
#endif
	xb->amiga_io[IO_AMIGA_INTERRUPT_STATUS] |= 1 << bit;
	x86_bridge_rethink();
}

/* 8237 and 8253 from fake86 with small modifications */

#define PIT_MODE_LATCHCOUNT	0
#define PIT_MODE_LOBYTE	1
#define PIT_MODE_HIBYTE	2
#define PIT_MODE_TOGGLE	3

struct i8253_s {
	uint16_t chandata[3];
	uint8_t accessmode[3];
	uint8_t bytetoggle[3];
	uint32_t effectivedata[3];
	float chanfreq[3];
	uint8_t active[3];
	uint16_t counter[3];
};
static struct i8253_s i8253;
static uint16_t latched_val, latched;

static uint64_t hostfreq;
static uint64_t tickgap, curtick, lasttick, lasti8253tick, i8253tickgap;

static void inittiming()
{
#ifdef _WIN32
	LARGE_INTEGER queryperf;
	QueryPerformanceFrequency(&queryperf);
	hostfreq = queryperf.QuadPart;
	QueryPerformanceCounter(&queryperf);
	curtick = queryperf.QuadPart;
#else
	hostfreq = 1000000;
	gettimeofday(&tv, NULL);
	curtick = (uint64_t)tv.tv_sec * (uint64_t)1000000 + (uint64_t)tv.tv_usec;
#endif
	lasti8253tick = lasttick = curtick;
	i8253tickgap = hostfreq / 119318;
}

static void timing(void)
{
	struct x86_bridge *xb = bridges[0];

#ifdef _WIN32
	LARGE_INTEGER queryperf;
	QueryPerformanceCounter(&queryperf);
	curtick = queryperf.QuadPart;
#else
	gettimeofday(&tv, NULL);
	curtick = (uint64_t)tv.tv_sec * (uint64_t)1000000 + (uint64_t)tv.tv_usec;
#endif

	if (i8253.active[0]) { //timer interrupt channel on i8253
		if (curtick >= (lasttick + tickgap)) {
			lasttick = curtick;
			doirq(0);
		}
	}

	if (curtick >= (lasti8253tick + i8253tickgap)) {
		for (int i8253chan = 0; i8253chan<3; i8253chan++) {
			bool cantrun = (i8253chan == 2 && !(xb->io_ports[0x61] & 1));
			if (i8253.active[i8253chan] && !cantrun) {
				if (i8253.counter[i8253chan] < 10)
					i8253.counter[i8253chan] = i8253.chandata[i8253chan];
				i8253.counter[i8253chan] -= 10;
			}
		}
		lasti8253tick = curtick;
	}
}

static void out8253(uint16_t portnum, uint8_t value)
{
	uint8_t curbyte;
	portnum &= 3;
	switch (portnum)
	{
		case 0:
		case 1:
		case 2: //channel data
		if ((i8253.accessmode[portnum] == PIT_MODE_LOBYTE) || ((i8253.accessmode[portnum] == PIT_MODE_TOGGLE) && (i8253.bytetoggle[portnum] == 0)))
			curbyte = 0;
		else if ((i8253.accessmode[portnum] == PIT_MODE_HIBYTE) || ((i8253.accessmode[portnum] == PIT_MODE_TOGGLE) && (i8253.bytetoggle[portnum] == 1)))
			curbyte = 1;
		if (curbyte == 0) { //low byte
			i8253.chandata[portnum] = (i8253.chandata[portnum] & 0xFF00) | value;
		} else {   //high byte
			i8253.chandata[portnum] = (i8253.chandata[portnum] & 0x00FF) | ((uint16_t)value << 8);
		}
		if (i8253.chandata[portnum] == 0)
			i8253.effectivedata[portnum] = 65536;
		else
			i8253.effectivedata[portnum] = i8253.chandata[portnum];
		i8253.counter[portnum] = i8253.chandata[portnum];
		i8253.active[portnum] = 1;
		tickgap = (uint64_t)((float)hostfreq / (float)((float)1193182 / (float)i8253.effectivedata[0]));
		if (i8253.accessmode[portnum] == PIT_MODE_TOGGLE)
			i8253.bytetoggle[portnum] = (~i8253.bytetoggle[portnum]) & 1;
		i8253.chanfreq[portnum] = (float)((uint32_t)(((float) 1193182.0 / (float)i8253.effectivedata[portnum]) * (float) 1000.0)) / (float) 1000.0;
		//printf("[DEBUG] PIT channel %u counter changed to %u (%f Hz)\n", portnum, i8253.chandata[portnum], i8253.chanfreq[portnum]);
		break;
		case 3: //mode/command
		i8253.accessmode[value >> 6] = (value >> 4) & 3;
		if (i8253.accessmode[value >> 6] == PIT_MODE_LATCHCOUNT) {
			latched_val = i8253.counter[value >> 6];
			latched = 2;
		} else if (i8253.accessmode[value >> 6] == PIT_MODE_TOGGLE)
			i8253.bytetoggle[value >> 6] = 0;
		break;
	}
}

static uint8_t in8253(uint16_t portnum)
{
	uint8_t curbyte;
	portnum &= 3;
	switch (portnum)
	{
		case 0:
		case 1:
		case 2: //channel data
		if (latched == 2) {
			latched = 1;
			return latched_val & 0xff;
		} else if (latched == 1) {
			latched = 0;
			return latched_val >> 8;
		}
		if ((i8253.accessmode[portnum] == 0) || (i8253.accessmode[portnum] == PIT_MODE_LOBYTE) || ((i8253.accessmode[portnum] == PIT_MODE_TOGGLE) && (i8253.bytetoggle[portnum] == 0)))
			curbyte = 0;
		else if ((i8253.accessmode[portnum] == PIT_MODE_HIBYTE) || ((i8253.accessmode[portnum] == PIT_MODE_TOGGLE) && (i8253.bytetoggle[portnum] == 1)))
			curbyte = 1;
		if ((i8253.accessmode[portnum] == 0) || (i8253.accessmode[portnum] == PIT_MODE_TOGGLE))
			i8253.bytetoggle[portnum] = (~i8253.bytetoggle[portnum]) & 1;
		if (curbyte == 0) { //low byte
			return ((uint8_t)i8253.counter[portnum]);
		} else {   //high byte
			return ((uint8_t)(i8253.counter[portnum] >> 8));
		}
		break;
	}
	return (0);
}

//#define DEBUG_DMA

struct dmachan_s {
	uint32_t page;
	uint32_t addr;
	uint32_t reload;
	uint32_t count;
	uint8_t direction;
	uint8_t autoinit;
	uint8_t writemode;
	uint8_t modeselect;
	uint8_t masked;
	uint8_t verifymode;
};

static struct dmachan_s dmachan[4];
static uint8_t flipflop = 0;

static int write8237(struct x86_bridge *xb, uint8_t channel, uint8_t data)
{
	if (dmachan[channel].masked)
		return 0;
	if (dmachan[channel].autoinit && (dmachan[channel].count > dmachan[channel].reload))
		dmachan[channel].count = 0;
	if (dmachan[channel].count > dmachan[channel].reload)
		return 0;
	//if (dmachan[channel].direction) ret = RAM[dmachan[channel].page + dmachan[channel].addr + dmachan[channel].count];
	//	else ret = RAM[dmachan[channel].page + dmachan[channel].addr - dmachan[channel].count];
	if (!dmachan[channel].verifymode) {
		xb->pc_ram[dmachan[channel].page + dmachan[channel].addr] = data;
		if (dmachan[channel].direction == 0) {
			dmachan[channel].addr++;
		} else {
			dmachan[channel].addr--;
		}
		dmachan[channel].addr &= 0xffff;
	}
	dmachan[channel].count++;
	if (dmachan[channel].count > dmachan[channel].reload)
		return -1;
	return 1;
}

static uint8_t read8237(struct x86_bridge *xb, uint8_t channel, bool *end)
{
	uint8_t ret = 128;
	*end = false;
	if (dmachan[channel].masked) {
		*end = true;
		return ret;
	}
	if (dmachan[channel].autoinit && (dmachan[channel].count > dmachan[channel].reload))
		dmachan[channel].count = 0;
	if (dmachan[channel].count > dmachan[channel].reload) {
		*end = true;
		return ret;
	}
	//if (dmachan[channel].direction) ret = RAM[dmachan[channel].page + dmachan[channel].addr + dmachan[channel].count];
	//	else ret = RAM[dmachan[channel].page + dmachan[channel].addr - dmachan[channel].count];
	if (!dmachan[channel].verifymode) {
		ret = xb->pc_ram[dmachan[channel].page + dmachan[channel].addr];
		if (dmachan[channel].direction == 0) {
			dmachan[channel].addr++;
		} else {
			dmachan[channel].addr--;
		}
		dmachan[channel].addr &= 0xffff;
	}
	dmachan[channel].count++;
	if (dmachan[channel].count > dmachan[channel].reload)
		*end = true;
	return ret;
}

static void out8237(uint16_t addr, uint8_t value)
{
	uint8_t channel;
#ifdef DEBUG_DMA
	write_log("out8237(0x%X, %X);\n", addr, value);
#endif
	switch (addr) {
		case 0x0:
		case 0x2: //channel 1 address register
		case 0x4:
		case 0x6:
		channel = addr / 2;
		if (flipflop == 1)
			dmachan[channel].addr = (dmachan[channel].addr & 0x00FF) | ((uint32_t)value << 8);
		else
			dmachan[channel].addr = (dmachan[channel].addr & 0xFF00) | value;
#ifdef DEBUG_DMA
		if (flipflop == 1) write_log("[NOTICE] DMA channel %d address register = %04X\n", channel, dmachan[channel].addr);
#endif
		flipflop = ~flipflop & 1;
		break;
		case 0x1:
		case 0x3: //channel 1 count register
		case 0x5:
		case 0x7:
		channel = addr / 2;
		if (flipflop == 1)
			dmachan[channel].reload = (dmachan[channel].reload & 0x00FF) | ((uint32_t)value << 8);
		else
			dmachan[channel].reload = (dmachan[channel].reload & 0xFF00) | value;
		if (flipflop == 1) {
			if (dmachan[channel].reload == 0)
				dmachan[channel].reload = 65536;
			dmachan[channel].count = 0;
#ifdef DEBUG_DMA
			write_log("[NOTICE] DMA channel %d reload register = %04X\n", channel, dmachan[channel].reload);
#endif
		}
		flipflop = ~flipflop & 1;
		break;
		case 0xA: //write single mask register
		channel = value & 3;
		dmachan[channel].masked = (value >> 2) & 1;
#ifdef DEBUG_DMA
		write_log("[NOTICE] DMA channel %u masking = %u\n", channel, dmachan[channel].masked);
#endif
		break;
		case 0xB: //write mode register
		channel = value & 3;
		dmachan[channel].direction = (value >> 5) & 1;
		dmachan[channel].autoinit = (value >> 4) & 1;
		dmachan[channel].modeselect = (value >> 6) & 3;
		dmachan[channel].writemode = (value >> 2) & 1; //not quite accurate
		dmachan[channel].verifymode = ((value >> 2) & 3) == 0;
#ifdef DEBUG_DMA
		write_log("[NOTICE] DMA channel %u write mode reg: direction = %u, autoinit = %u, write mode = %u, verify mode = %u, mode select = %u\n",
			   channel, dmachan[channel].direction, dmachan[channel].autoinit, dmachan[channel].writemode, dmachan[channel].verifymode, dmachan[channel].modeselect);
#endif
		break;
		case 0xC: //clear byte pointer flip-flop
#ifdef DEBUG_DMA
		write_log("[NOTICE] DMA cleared byte pointer flip-flop\n");
#endif
		flipflop = 0;
		break;
		case 0x81: // 2
		case 0x82: // 3
		case 0x83: // DMA channel 1 page register
		// Original PC design. It can't get any more stupid.
		if (addr == 0x81)
			channel = 2;
		else if (addr == 0x82)
			channel = 3;
		else
			channel = 1;
		dmachan[channel].page = (uint32_t)value << 16;
#ifdef DEBUG_DMA
		write_log("[NOTICE] DMA channel %d page base = %05X\n", channel, dmachan[channel].page);
#endif
		break;
	}
}

static uint8_t in8237(uint16_t addr)
{
	uint8_t channel;
#ifdef DEBUG_DMA
	write_log("in8237(0x%X);\n", addr);
#endif
	switch (addr) {
		case 0x0:
		case 0x2: //channel 1 address register
		case 0x4:
		case 0x6:
		channel = addr / 2;
		flipflop = ~flipflop & 1;
		if (flipflop == 0)
			return dmachan[channel].addr >> 8;
		else
			return dmachan[channel].addr;
		break;
		case 0x1:
		case 0x3: //channel 1 count register
		case 0x5:
		case 0x7:
		channel = addr / 2;
		flipflop = ~flipflop & 1;
		if (flipflop == 0)
			return dmachan[channel].reload >> 8;
		else
			return dmachan[channel].reload;
		break;
	}
	return (0);
}

struct structpic {
	uint8_t imr; //mask register
	uint8_t irr; //request register
	uint8_t isr; //service register
	uint8_t icwstep; //used during initialization to keep track of which ICW we're at
	uint8_t icw[5];
	uint8_t intoffset; //interrupt vector offset
	uint8_t priority; //which IRQ has highest priority
	uint8_t autoeoi; //automatic EOI mode
	uint8_t readmode; //remember what to return on read register from OCW3
	uint8_t enabled;
};

static struct structpic i8259;

static uint8_t in8259(uint16_t portnum)
{
	switch (portnum & 1) {
		case 0:
		if (i8259.readmode == 0)
			return(i8259.irr);
		else
			return(i8259.isr);
		case 1: //read mask register
		return(i8259.imr);
	}
	return (0);
}

extern uint32_t makeupticks;
static int keyboardwaitack;

void out8259(uint16_t portnum, uint8_t value)
{
	uint8_t i;
	switch (portnum & 1) {
		case 0:
		if (value & 0x10) { //begin initialization sequence
			i8259.icwstep = 1;
			i8259.imr = 0; //clear interrupt mask register
			i8259.icw[i8259.icwstep++] = value;
			return;
		}
		if ((value & 0x98) == 8) { //it's an OCW3
			if (value & 2)
				i8259.readmode = value & 2;
		}
		if (value & 0x20) { //EOI command
			if (keyboardwaitack) {
				keyboardwaitack = 0;
				set_interrupt(bridges[0], 4);
			}
			for (i = 0; i < 8; i++)
				if ((i8259.isr >> i) & 1) {
					i8259.isr ^= (1 << i);
#if 0
					if ((i == 0) && (makeupticks>0)) {
						makeupticks = 0;
						i8259.irr |= 1;
					}
#endif
					return;
				}
		}
		break;
		case 1:
		if ((i8259.icwstep == 3) && (i8259.icw[1] & 2))
			i8259.icwstep = 4; //single mode, so don't read ICW3
		if (i8259.icwstep<5) {
			i8259.icw[i8259.icwstep++] = value;
			return;
		}
		//if we get to this point, this is just a new IMR value
		i8259.imr = value;
		break;
	}
}

static void doirq(uint8_t irqnum)
{
	i8259.irr |= (1 << irqnum);
	if (irqnum == 1)
		keyboardwaitack = 1;
}

static uint8_t nextintr(void)
{
	uint8_t i, tmpirr;
	tmpirr = i8259.irr & (~i8259.imr); //XOR request register with inverted mask register
	for (i = 0; i<8; i++) {
		if ((tmpirr >> i) & 1) {
			i8259.irr ^= (1 << i);
			i8259.isr |= (1 << i);
			return(i8259.icw[2] + i);
		}
	}
	return(0); //this won't be reached, but without it the compiler gives a warning
}

void check_x86_irq(void)
{
	if (i8259.irr & (~i8259.imr)) {
		intcall86(nextintr());	/* get next interrupt from the i8259, if any */
	}
}

struct pc_floppy
{
	uae_u8 phys_cyl;
	uae_u8 cyl;
	uae_u8 sector;
	uae_u8 head;
};

static struct pc_floppy floppy_pc[4];
static uae_u8 floppy_dpc;
static uae_s8 floppy_idx;
static uae_u8 floppy_dir;
static uae_u8 floppy_cmd_len;
static uae_u8 floppy_cmd[16];
static uae_u8 floppy_result[16];
static uae_u8 floppy_status[4];
static uae_u8 floppy_num;
static int floppy_delay_hsync;
static bool floppy_irq;
static bool floppy_did_reset;
static bool floppy_high_density;

static void floppy_reset(void)
{
	write_log(_T("Floppy reset\n"));
	floppy_idx = 0;
	floppy_dir = 0;
	floppy_did_reset = true;
}

static void do_floppy_irq(void)
{
	if (floppy_delay_hsync > 0) {
		floppy_irq = true;
		doirq(6);
	}
	floppy_delay_hsync = 0;
}

static int floppy_exists(void)
{
	uae_u8 sel = floppy_dpc & 3;
	if (sel == 0)
		return 0;
	if (sel == 1)
		return 1;
	return -1;
}

static int floppy_selected(void)
{
	uae_u8 motormask = (floppy_dpc >> 4) & 15;
	uae_u8 sel = floppy_dpc & 3;
	if (floppy_exists() < 0)
		return -1;
	if (motormask & (1 << sel))
		return sel;
	return -1;
}

static void floppy_do_cmd(struct x86_bridge *xb)
{
	uae_u8 cmd = floppy_cmd[0];
	struct pc_floppy *pcf = &floppy_pc[floppy_num];
	struct floppy_reserved fr = { 0 };
	bool valid_floppy;

	valid_floppy = disk_reserved_getinfo(floppy_num, &fr);

	if (cmd == 8) {
		write_log(_T("Floppy%d Sense Interrupt Status\n"), floppy_num);
		floppy_cmd_len = 2;
		if (!floppy_irq) {
			floppy_status[0] = 0x80;
			floppy_cmd_len = 1;
		} else if (floppy_did_reset) {
			floppy_did_reset = false;
			floppy_status[0] = 0xc0;
		}
		floppy_irq = false;
		floppy_result[0] = floppy_status[0];
		floppy_result[1] = pcf->cyl;
		floppy_status[0] = 0;
		goto end;
	}

	memset(floppy_status, 0, sizeof floppy_status);
	memset(floppy_result, 0, sizeof floppy_result);
	if (floppy_exists()) {
		if (pcf->head)
			floppy_status[0] |= 4;
		floppy_status[3] |= pcf->cyl == 0 ? 0x10 : 0x00;
		floppy_status[3] |= pcf->head ? 4 : 0;
		floppy_status[3] |= 8; // two side
		if (fr.wrprot)
			floppy_status[3] |= 0x40; // write protected
		floppy_status[3] |= 0x20; // ready
	}
	floppy_status[3] |= floppy_dpc & 3;
	floppy_status[0] |= floppy_dpc & 3;
	floppy_cmd_len = 0;

	switch (cmd & 31)
	{
		case 3:
		write_log(_T("Floppy%d Specify SRT=%d HUT=%d HLT=%d ND=%d\n"), floppy_num, floppy_cmd[1] >> 6, floppy_cmd[1] & 0x3f, floppy_cmd[2] >> 1, floppy_cmd[2] & 1);
		floppy_delay_hsync = -5;
		break;

		case 4:
		write_log(_T("Floppy%d Sense Interrupt Status\n"), floppy_num);
		floppy_delay_hsync = 5;
		floppy_result[0] = floppy_status[3];
		floppy_cmd_len = 1;
		break;

		case 5:
		{
			write_log(_T("Floppy%d write MT=%d MF=%d C=%d:H=%d:R=%d:N=%d:EOT=%d:GPL=%d:DTL=%d\n"),
					  (floppy_cmd[0] & 0x80) ? 1 : 0, (floppy_cmd[0] & 0x40) ? 1 : 0,
					  floppy_cmd[2], floppy_cmd[3], floppy_cmd[4], floppy_cmd[5],
					  floppy_cmd[6], floppy_cmd[7], floppy_cmd[8], floppy_cmd[9]);
			write_log(_T("DMA addr %08x len %04x\n"), dmachan[2].page | dmachan[2].addr, dmachan[2].reload);
			floppy_delay_hsync = 50;
			if (valid_floppy) {
				if (fr.img && pcf->cyl != floppy_cmd[2]) {
					floppy_status[0] |= 0x40; // abnormal termination
					floppy_status[2] |= 0x20; // wrong cylinder
				} else if (fr.img) {
					bool end = false;
					pcf->sector = floppy_cmd[4] - 1;
					pcf->head = (floppy_cmd[1] & 4) ? 1 : 0;
					while (!end) {
						int len = 128 << floppy_cmd[5];
						uae_u8 buf[512] = { 0 };
						for (int i = 0; i < 512 && i < len; i++) {
							buf[i] = read8237(xb, 2, &end);
							if (end)
								break;
						}
						zfile_fseek(fr.img, (pcf->cyl * fr.secs * fr.heads + pcf->head * fr.secs + pcf->sector) * 512, SEEK_SET);
						zfile_fwrite(buf, 1, 512, fr.img);
						pcf->sector++;
						if (!(floppy_cmd[0] & 0x80))
							break;
						if (pcf->sector >= fr.secs) {
							pcf->sector = 0;
							// todo: check limits
							if (!pcf->head) {
								pcf->head = 1;
							} else {
								break;
							}
						}
					}
					floppy_result[3] = pcf->cyl;
					floppy_result[4] = pcf->head;
					floppy_result[5] = pcf->sector + 1;
					floppy_result[6] = floppy_cmd[5];
				} else {
					floppy_status[0] |= 0x40; // abnormal termination
					floppy_status[0] |= 0x10; // equipment check
				}
			}
			floppy_cmd_len = 7;
			if (fr.wrprot) {
				floppy_status[0] |= 0x40; // abnormal termination
				floppy_status[1] |= 0x02; // not writable
			}
			floppy_result[0] = floppy_status[0];
			floppy_result[1] = floppy_status[1];
			floppy_result[2] = floppy_status[2];
			floppy_delay_hsync = 10;
			disk_reserved_setinfo(floppy_num, pcf->cyl, pcf->head, 1);
		}
		break;

		case 6:
		{
			write_log(_T("Floppy%d read MT=%d MF=%d SK=%d C=%d:H=%d:R=%d:N=%d:EOT=%d:GPL=%d:DTL=%d\n"),
				floppy_num, (floppy_cmd[0] & 0x80) ? 1 : 0, (floppy_cmd[0] & 0x40) ? 1 : 0, (floppy_cmd[0] & 0x20) ? 1 : 0,
				floppy_cmd[2], floppy_cmd[3], floppy_cmd[4], floppy_cmd[5],
				floppy_cmd[6], floppy_cmd[7], floppy_cmd[8], floppy_cmd[9]);
			write_log(_T("DMA addr %08x len %04x\n"), dmachan[2].page | dmachan[2].addr, dmachan[2].reload);
			floppy_delay_hsync = 50;
			bool nodata = false;
			if (valid_floppy) {
				if (fr.img && pcf->cyl != floppy_cmd[2]) {
					floppy_status[0] |= 0x40; // abnormal termination
					floppy_status[2] |= 0x20; // wrong cylinder
				} else if (fr.img) {
					bool end = false;
					pcf->sector = floppy_cmd[4] - 1;
					pcf->head = (floppy_cmd[1] & 4) ? 1 : 0;
					while (!end) {
						int len = 128 << floppy_cmd[5];
						uae_u8 buf[512];
						zfile_fseek(fr.img, (pcf->cyl * fr.secs * fr.heads + pcf->head * fr.secs + pcf->sector) * 512, SEEK_SET);
						zfile_fread(buf, 1, 512, fr.img);
						for (int i = 0; i < 512 && i < len; i++) {
							if (write8237(xb, 2, buf[i]) <= 0)
								end = true;
						}
						pcf->sector++;
						if (!(floppy_cmd[0] & 0x80))
							break;
						if (pcf->sector >= fr.secs) {
							pcf->sector = 0;
							// todo: check limits
							if (!pcf->head) {
								pcf->head = 1;
							}
						}
					}
					floppy_result[3] = pcf->cyl;
					floppy_result[4] = pcf->head;
					floppy_result[5] = pcf->sector + 1;
					floppy_result[6] = floppy_cmd[5];
				} else {
					nodata = true;
				}
			}
			if (nodata) {
				floppy_status[0] |= 0x40; // abnormal termination
				floppy_status[1] |= 0x04; // no data
			}
			floppy_cmd_len = 7;
			floppy_result[0] = floppy_status[0];
			floppy_result[1] = floppy_status[1];
			floppy_result[2] = floppy_status[2];
			floppy_delay_hsync = 10;
			disk_reserved_setinfo(floppy_num, pcf->cyl, pcf->head, 1);
		}
		break;

		case 7:
		write_log(_T("Floppy%d recalibrate\n"), floppy_num);
		if (floppy_selected() >= 0) {
			pcf->cyl = 0;
			pcf->phys_cyl = 0;
			pcf->head = 0;
		} else {
			floppy_status[0] |= 0x40; // abnormal termination
			floppy_status[0] |= 0x10; // equipment check
		}
		floppy_status[0] |= 0x20; // seek end
		floppy_delay_hsync = 50;
		disk_reserved_setinfo(floppy_num, pcf->cyl, pcf->head, 1);
		break;

		case 10:
		write_log(_T("Floppy read ID\n"));
		if (!valid_floppy && fr.img) {
			floppy_status[0] |= 0x40; // abnormal termination
			floppy_status[1] |= 0x04; // no data
		}
		floppy_cmd_len = 7;
		floppy_result[0] = floppy_status[0];
		floppy_result[1] = floppy_status[1];
		floppy_result[2] = floppy_status[2];
		floppy_result[3] = pcf->cyl;
		floppy_result[4] = pcf->head;
		floppy_result[5] = pcf->sector + 1;
		floppy_result[6] = 2;

		pcf->sector++;
		pcf->sector %= fr.secs;

		floppy_delay_hsync = 10;
		disk_reserved_setinfo(floppy_num, pcf->cyl, pcf->head, 1);
		break;

		case 13:
		{
			write_log(_T("Floppy%d format MF=%d N=%d:SC=%d:GPL=%d:D=%d\n"),
					  floppy_num, (floppy_cmd[0] & 0x40) ? 1 : 0,
					  floppy_cmd[2], floppy_cmd[3], floppy_cmd[4], floppy_cmd[5]);
			write_log(_T("DMA addr %08x len %04x\n"), dmachan[2].page | dmachan[2].addr, dmachan[2].reload);
			int secs = floppy_cmd[3];
			if (valid_floppy && fr.img) {
				// TODO: CHRN values totally ignored
				pcf->head = (floppy_cmd[1] & 4) ? 1 : 0;
				uae_u8 buf[512];
				memset(buf, floppy_cmd[5], sizeof buf);
				for (int i = 0; i < secs && i < fr.secs; i++) {
					bool dmaend;
					uae_u8 cx = read8237(xb, 2, &dmaend);
					uae_u8 hx = read8237(xb, 2, &dmaend);
					uae_u8 rx = read8237(xb, 2, &dmaend);
					uae_u8 nx = read8237(xb, 2, &dmaend);
					pcf->sector = rx - 1;
					write_log(_T("Floppy%d %d/%d: C=%d H=%d R=%d N=%d\n"), floppy_num, i, fr.secs, cx, hx, rx, nx);
					zfile_fseek(fr.img, (pcf->cyl * fr.secs * fr.heads + pcf->head * fr.secs + pcf->sector) * 512, SEEK_SET);
					zfile_fwrite(buf, 1, 512, fr.img);
				}
			} else {
				floppy_status[0] |= 0x40; // abnormal termination
				floppy_status[0] |= 0x10; // equipment check
			}
			floppy_cmd_len = 7;
			if (fr.wrprot) {
				floppy_status[0] |= 0x40; // abnormal termination
				floppy_status[1] |= 0x02; // not writable
			}
			floppy_result[0] = floppy_status[0];
			floppy_result[1] = floppy_status[1];
			floppy_result[2] = floppy_status[2];
			floppy_result[3] = pcf->cyl;
			floppy_result[4] = pcf->head;
			floppy_result[5] = pcf->sector + 1;
			floppy_result[6] = floppy_cmd[2];
			floppy_delay_hsync = 10;
			disk_reserved_setinfo(floppy_num, pcf->cyl, pcf->head, 1);
		}
		break;

		case 15:
		{
			int newcyl = floppy_cmd[2];
			floppy_delay_hsync = abs(pcf->phys_cyl - newcyl) * 100 + 50;
			pcf->phys_cyl = newcyl;
			pcf->head = (floppy_cmd[1] & 4) ? 1 : 0;
			floppy_status[0] |= 0x20; // seek end
			if (fr.cyls < 80 && floppy_high_density)
				pcf->cyl = pcf->phys_cyl / 2;
			else
				pcf->cyl = pcf->phys_cyl;
			write_log(_T("Floppy%d seek to %d %d\n"), floppy_num, pcf->phys_cyl, pcf->head);
			disk_reserved_setinfo(floppy_num, pcf->cyl, pcf->head, 1);
		}
		break;

		default:
		floppy_status[0] = 0x80;
		floppy_cmd_len = 1;
		break;
	}

end:
	if (floppy_cmd_len) {
		floppy_idx = -1;
		floppy_dir = 1;
		write_log(_T("Status return: "));
		for (int i = 0; i < floppy_cmd_len; i++) {
			write_log(_T("%02x "), floppy_result[i]);
		}
		write_log(_T("\n"));
	} else {
		floppy_idx = 0;
		floppy_dir = 0;
	}
}

static void outfloppy(struct x86_bridge *xb, int portnum, uae_u8 v)
{
	switch (portnum)
	{
		case 0x3f2: // dpc
		if ((v & 4) && !(floppy_dpc & 4)) {
			floppy_reset();
			floppy_delay_hsync = 20;
		}
#if FLOPPY_IO_DEBUG
		write_log(_T("DPC: Motormask %02x sel=%d enable=%d dma=%d\n"), (v >> 4) & 15, v & 3, (v & 8) ? 1 : 0, (v & 4) ? 1 : 0);
#endif
		floppy_dpc = v;
		floppy_num = v & 3;
		for (int i = 0; i < 2; i++) {
			disk_reserved_setinfo(0, floppy_pc[i].cyl, floppy_pc[i].head, floppy_selected() == i);
		}
		break;
		case 0x3f5: // data reg
		floppy_cmd[floppy_idx] = v;
		if (floppy_idx == 0) {
			switch(v & 31)
			{
				case 3: // specify
				floppy_cmd_len = 3;
				break;
				case 4: // sense drive status
				floppy_cmd_len = 2;
				break;
				case 5: // write data
				floppy_cmd_len = 9;
				break;
				case 6: // read data
				floppy_cmd_len = 9;
				break;
				case 7: // recalibrate
				floppy_cmd_len = 2;
				break;
				case 8: // sense interrupt status
				floppy_cmd_len = 1;
				break;
				case 10: // read id
				floppy_cmd_len = 2;
				break;
				case 13: // format track
				floppy_cmd_len = 6;
				break;
				case 15: // seek
				floppy_cmd_len = 3;
				break;
				default:
				write_log(_T("Floppy unimplemented command %02x\n"), v);
				floppy_cmd_len = 1;
				break;
			}
		}
		floppy_idx++;
		if (floppy_idx >= floppy_cmd_len) {
			floppy_do_cmd(xb);
		}
		break;
	}
#if FLOPPY_IO_DEBUG
	write_log(_T("out floppy port %04x %02x\n"), portnum, v);
#endif
}
static uae_u8 infloppy(struct x86_bridge *xb, int portnum)
{
	uae_u8 v = 0;
	switch (portnum)
	{
		case 0x3f4: // main status
		v = 0;
		if (!floppy_delay_hsync)
			v |= 0x80;
		if (floppy_idx || floppy_delay_hsync)
			v |= 0x10;
		if ((v & 0x80) && floppy_dir)
			v |= 0x40;
		break;
		case 0x3f5: // data reg
		if (floppy_cmd_len && floppy_dir) {
			int idx = (-floppy_idx) - 1;
			if (idx < sizeof floppy_result) {
				v = floppy_result[idx];
				idx++;
				floppy_idx--;
				if (idx >= floppy_cmd_len) {
					floppy_cmd_len = 0;
					floppy_dir = 0;
					floppy_idx = 0;
				}
			}
		}
		break;
	}
#if FLOPPY_IO_DEBUG
	write_log(_T("in  floppy port %04x %02x\n"), portnum, v);
#endif
	return v;
}

static void set_pc_address_access(struct x86_bridge *xb, uaecptr addr)
{
	if (addr >= 0xb0000 && addr < 0xb2000) {
		// mono
		set_interrupt(xb, 0);
	}
	if (addr >= 0xb8000 && addr < 0xc0000) {
		// color
		set_interrupt(xb, 1);
	}
}

static void set_pc_io_access(struct x86_bridge *xb, uaecptr portnum, bool write)
{
	if (write && portnum >= 0x3b0 && portnum < 0x3bf) {
		// mono crt
		set_interrupt(xb, 2);
	} else if (write && portnum >= 0x3d0 && portnum < 0x3df) {
		// color crt
		set_interrupt(xb, 3);
	} else if (portnum >= 0x37a && portnum < 0x37b) {
		// LPT1
		set_interrupt(xb, 5);
	} else if (portnum >= 0x2f8 && portnum < 0x2f9) {
		// COM2
		set_interrupt(xb, 6);
	}
}

static bool is_port_enabled(struct x86_bridge *xb, uint16_t portnum)
{
	uae_u8 enables = xb->amiga_io[IO_MODE_REGISTER];
	// COM2
	if (portnum >= 0x2f8 && portnum < 0x300) {
		if (!(enables & 1))
			return false;
	}
	// LPT1
	if (portnum >= 0x378 && portnum < 0x37f) {
		if (!(enables & 2))
			return false;
	}
	// Keyboard
	// ???
	// Mono video
	if (portnum >= 0x3b0 && portnum < 0x3bf) {
		if (!(enables & 8))
			return false;
	}
	// Color video
	if (portnum >= 0x3d0 && portnum < 0x3df) {
		if (!(enables & 16))
			return false;
	}
	return true;
}

void portout(uint16_t portnum, uint8_t v)
{
	struct x86_bridge *xb = bridges[0];
	uae_u8 *io = xb->io_ports;
	int aio = -1;

	if (portnum >= 0x400)
		return;

	if (!is_port_enabled(xb, portnum))
		return;

	set_pc_io_access(xb, portnum, true);

	switch(portnum)
	{
		case 0x20:
		case 0x21:
		out8259(portnum, v);
		break;
		case 0x40:
		case 0x41:
		case 0x42:
		case 0x43:
		out8253(portnum, v);
		break;
		case 0x80:
		case 0x81:
		case 0x82:
		case 0x83:
		case 0x84:
		case 0x85:
		case 0x86:
		case 0x87:
		case 0x88:
		case 0x89:
		case 0x8a:
		case 0x8b:
		case 0x8c:
		case 0x8d:
		case 0x8e:
		case 0x8f:
		case 0x00:
		case 0x01:
		case 0x02:
		case 0x03:
		case 0x04:
		case 0x05:
		case 0x06:
		case 0x07:
		case 0x08:
		case 0x09:
		case 0x0a:
		case 0x0b:
		case 0x0c:
		case 0x0d:
		case 0x0e:
		case 0x0f:
		out8237(portnum, v);
		break;

		case 0x60:
		aio = 0x41f;
		break;
		case 0x61:
		//write_log(_T("OUT Port B %02x\n"), v);
		aio = 0x5f;
		break;
		case 0x62:
		//write_log(_T("AMIGA SYSINT. %02x\n"), v);
		set_interrupt(xb, 7);
		aio = 0x3f;
		if (xb->type > TYPE_SIDECAR) {
			// default video mode bits 4-5 come from jumpers.
			if (!(xb->io_ports[0x63] & 8)) {
				xb->pc_jumpers &= ~0xcf;
				xb->pc_jumpers |= v & 0xcf;
			}
		}
		break;
		case 0x63:
		write_log(_T("OUT CONFIG %02x\n"), v);
		if (xb->type > TYPE_SIDECAR) {
			if (xb->io_ports[0x63] & 8) {
				v |= 8;
			}
			if (xb->type == TYPE_2088T) {
				int speed = v >> 6;
				if (speed == 0)
					write_log(_T("A2088T: 4.77MHz\n"));
				if (speed == 1)
					write_log(_T("A2088T: 7.15MHz\n"));
				if (speed == 2)
					write_log(_T("A2088T: 9.54MHz\n"));
			}
		}
		break;

		case 0x2f8:
		if (xb->amiga_io[0x13f] & 0x80)
			aio = 0x7f;
		else
			aio = 0x7d;
		break;
		case 0x2f9:
		if (xb->amiga_io[0x13f] & 0x80)
			aio = 0x9f;
		else
			aio = 0xbd;
		break;
		case 0x2fb:
		aio = 0x11f;
		break;
		case 0x2fc:
		aio = 0x13f;
		break;
		case 0x2fa:
		case 0x2fd:
		case 0x2fe:
		case 0x2ff:
		aio = 0x1f;
		break;

		// mono video
		case 0x3b0:
		case 0x3b2:
		case 0x3b4:
		case 0x3b6:
		aio = 0x1ff;
		break;
		case 0x3b1:
		case 0x3b3:
		case 0x3b5:
		case 0x3b7:
		aio = 0x2a1 + (xb->amiga_io[0x1ff] & 15) * 2;
		break;
		case 0x3b8:
		aio = 0x2ff;
		break;

		// color video
		case 0x3d0:
		case 0x3d2:
		case 0x3d4:
		case 0x3d6:
		aio = 0x21f;
		break;
		case 0x3d1:
		case 0x3d3:
		case 0x3d5:
		case 0x3d7:
		aio = 0x2c1 + (xb->amiga_io[0x21f] & 15) * 2;
		break;
		case 0x3d8:
		aio = 0x23f;
		break;
		case 0x3d9:
		aio = 0x25f;
		break;
		case 0x3dd:
		aio = 0x29f;
		break;

		case 0x378:
		write_log(_T("BIOS DIAGNOSTICS CODE: %02x\n"), v);
		aio = 0x19f; // ??
		break;
		case 0x379:
		if (xb->amiga_io[IO_MODE_REGISTER] & 2) {
			xb->amiga_forced_interrupts = (v & 40) ? false : true;
		}
		aio = 0x19f; // ??
		break;
		case 0x37a:
		aio = 0x1df;
		break;

		case 0x3ba:
		case 0x3bb:
		case 0x3bc:
		case 0x3bd:
		case 0x3be:
		case 0x3bf:
		case 0x3da:
		case 0x3de:
		case 0x3df:
		aio = 0x1f;
		break;

		// floppy
		case 0x3f0:
		case 0x3f1:
		case 0x3f2:
		case 0x3f3:
		case 0x3f4:
		case 0x3f5:
		case 0x3f6:
		case 0x3f7:
		outfloppy(xb, portnum, v);
		break;
	}

	//write_log(_T("X86_OUT %08x %02X\n"), portnum, v);

	if (aio >= 0)
		xb->amiga_io[aio] = v;
	xb->io_ports[portnum] = v;
}
void portout16(uint16_t portnum, uint16_t value)
{
	write_log(_T("portout16 %08x\n"), portnum);
}
uint8_t portin(uint16_t portnum)
{
	struct x86_bridge *xb = bridges[0];
	int aio = -1;

	if (!is_port_enabled(xb, portnum))
		return 0;

	if (portnum >= 0x400)
		return 0;

	set_pc_io_access(xb, portnum, false);

	uae_u8 v = xb->io_ports[portnum];
	switch (portnum)
	{
		case 0x20:
		case 0x21:
		v = in8259(portnum);
		break;
		case 0x40:
		case 0x41:
		case 0x42:
		case 0x43:
		v = in8253(portnum);
		break;
		case 0x80:
		case 0x81:
		case 0x82:
		case 0x83:
		case 0x84:
		case 0x85:
		case 0x86:
		case 0x87:
		case 0x88:
		case 0x89:
		case 0x8a:
		case 0x8b:
		case 0x8c:
		case 0x8d:
		case 0x8e:
		case 0x8f:
		case 0x00:
		case 0x01:
		case 0x02:
		case 0x03:
		case 0x04:
		case 0x05:
		case 0x06:
		case 0x07:
		case 0x08:
		case 0x09:
		case 0x0a:
		case 0x0b:
		case 0x0c:
		case 0x0d:
		case 0x0e:
		case 0x0f:
		v = in8237(portnum);
		break;

		case 0x2f8:
		if (xb->amiga_io[0x11f] & 0x80)
			aio = 0x7f;
		else
			aio = 0x9d;
		xb->pc_irq3b = false;
		break;
		case 0x2f9:
		if (xb->amiga_io[0x11f] & 0x80)
			aio = 0x9f;
		else
			aio = 0xdd;
		break;
		case 0x2fa:
		aio = 0xff;
		break;
		case 0x2fd:
		aio = 0x15f;
		break;
		case 0x2fe:
		aio = 0x17f;
		break;
		case 0x2fb:
		case 0x2fc:
		case 0x2ff:
		aio = 0x1f;
		break;

		case 0x378:
		aio = 0x19f; // ?
		break;
		case 0x379:
		xb->pc_irq7 = false;
		aio = 0x1bf;
		break;
		case 0x37a:
		aio = 0x19f; // ?
		break;

		// mono video
		case 0x3b0:
		xb->pc_irq3a = false;
		aio = 0x1ff;
		break;
		case 0x3b2:
		case 0x3b4:
		case 0x3b6:
		aio = 0x1ff;
		break;
		case 0x3b1:
		case 0x3b3:
		case 0x3b5:
		case 0x3b7:
		aio = 0x2a1 + (xb->amiga_io[0x1ff] & 15) * 2;
		break;
		case 0x3b8:
		aio = 0x2ff;
		break;

		// color video
		case 0x3d0:
		case 0x3d2:
		case 0x3d4:
		case 0x3d6:
		aio = 0x21f;
		break;
		case 0x3d1:
		case 0x3d3:
		case 0x3d5:
		case 0x3d7:
		aio = 0x2c1 + (xb->amiga_io[0x21f] & 15) * 2;
		break;
		case 0x3d8:
		aio = 0x23f;
		break;
		case 0x3d9:
		aio = 0x25f;
		break;
		case 0x3ba:
		case 0x3da:
		v = 0;
		// not really correct but easy.
		if (vpos < 20)
			v |= 8 | 1;
		if (get_cycles() - last_cycles > maxhpos / 2)
			v |= 1;
		last_cycles = get_cycles();
		break;
		case 0x3dd:
		aio = 0x29f;
		break;

		case 0x3bb:
		case 0x3bc:
		case 0x3bd:
		case 0x3be:
		case 0x3bf:
		case 0x3de:
		case 0x3df:
		aio = 0x1f;
		break;

		// floppy
		case 0x3f0:
		case 0x3f1:
		case 0x3f2:
		case 0x3f3:
		case 0x3f4:
		case 0x3f5:
		case 0x3f6:
		case 0x3f7:
		v = infloppy(xb, portnum);
		break;

		case 0x60:
		//write_log(_T("PC read keycode %02x\n"), v);
		break;
		case 0x61:
		v = xb->amiga_io[0x5f];
		//write_log(_T("IN Port B %02x\n"), v);
		break;
		case 0x62:
		{
			v = xb->amiga_io[0x3f];
			if (xb->type == TYPE_SIDECAR) {
				// Sidecar has jumpers.
				if (xb->amiga_io[0x5f] & 8) {
					// bit 0-1: display (11=mono 80x25,10=01=color 80x25,00=no video)
					// bit 2-3: number of drives (11=1..)
					v &= 0xf0;
					v |= (xb->pc_jumpers >> 4) & 0x0f;
				} else {
					v &= 0xf0;
					// bit 0: 0=loop on POST
					// bit 1: 0=8087 installed
					// bit 2-3: (11=640k,10=256k,01=512k,00=128k) RAM size
					v |= xb->pc_jumpers & 0xf;
				}
			} else {
				// A2088+ are software configurable (Except default video mode)
				if (!(xb->amiga_io[0x5f] & 4)) {
					v &= 0xf0;
					v |= (xb->pc_jumpers >> 4) & 0x0f;
				} else {
					v &= 0xf0;
					v |= xb->pc_jumpers & 0xf;
				}
			}
			v &= ~0x20;
			if (!(xb->amiga_io[0x5f] & 1) && i8253.active[2])
				v |= 0x20;
			//write_log(_T("IN Port C %02x\n"), v);
		}
		break;
		case 0x63:
		//write_log(_T("IN Control %02x\n"), v);
		break;
		default:
		write_log(_T("X86_IN unknown %02x\n"), portnum);
		return 0;
	}
	
	if (aio >= 0)
		v = xb->amiga_io[aio];
	//write_log(_T("X86_IN %08x %02X\n"), portnum, v);
	return v;
}
uint16_t portin16(uint16_t portnum)
{
	write_log(_T("portin16 %08x\n"), portnum);
	return 0;
}

void write86(uint32_t addr32, uint8_t value)
{
	struct x86_bridge *xb = bridges[0];
	addr32 &= 0xFFFFF;
	if (addr32 >= xb->pc_maxram && addr32 < 0xa0000)
		return;
	set_pc_address_access(xb, addr32);
	xb->pc_ram[addr32] = value;
}
void writew86(uint32_t addr32, uint16_t value)
{
	struct x86_bridge *xb = bridges[0];
	addr32 &= 0xFFFFF;
	if (addr32 >= xb->pc_maxram && addr32 < 0xa0000)
		return;
	set_pc_address_access(xb, addr32);
	xb->pc_ram[addr32] = value & 0xff;
	xb->pc_ram[addr32 + 1] = value >> 8;
}
uint8_t read86(uint32_t addr32)
{
	struct x86_bridge *xb = bridges[0];
	addr32 &= 0xFFFFF;
	return xb->pc_ram[addr32];
}
uint16_t readw86(uint32_t addr32)
{
	uint16_t v;
	struct x86_bridge *xb = bridges[0];
	addr32 &= 0xFFFFF;

	v = xb->pc_ram[addr32];
	v |= xb->pc_ram[addr32 + 1] << 8;
	return v;
}

static uaecptr get_x86_address(struct x86_bridge *xb, uaecptr addr, int *mode)
{
	addr -= xb->baseaddress;
	*mode = addr >> 17;
	addr &= 0x1ffff;
	if (addr < 0x10000) {
		int opt = (xb->amiga_io[IO_MODE_REGISTER] >> 5) & 3;
		// disk buffer
		switch(opt)
		{
			case 1:
			default:
			return 0xa0000 + addr;
			case 2:
			return 0xd0000 + addr;
			case 3:
			return 0xe0000 + addr;
		}
	}
	if (addr < 0x18000) {
		// color video
		addr -= 0x10000;
		return 0xb8000 + addr;
	}
	if (addr < 0x1c000) {
		addr -= 0x18000;
		// parameter
		return 0xf0000 + addr;
	}
	if (addr < 0x1e000) {
		addr -= 0x1c000;
		// mono video
		return 0xb0000 + addr;
	}
	// IO
	addr -= 0x1e000;
	return addr;
}

static struct x86_bridge *get_x86_bridge(uaecptr addr)
{
	return bridges[0];
}

static uae_u32 REGPARAM2 x86_bridge_wget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u16 v = 0;
	struct x86_bridge *xb = get_x86_bridge(addr);
	if (!xb)
		return v;
	int mode;
	uaecptr a = get_x86_address(xb, addr, &mode);
	if (mode == ACCESS_MODE_WORD) {
		v = (xb->pc_ram[a + 1] << 8) | (xb->pc_ram[a + 0] << 0);
	} else if (mode == ACCESS_MODE_GFX) {
		write_log(_T("ACCESS_MODE_GFX\n"));
	} else if (mode == ACCESS_MODE_IO) {
		v = x86_bridge_get_io(xb, a);
		v |= x86_bridge_get_io(xb, a + 1) << 8;
	} else if (mode >= 0) {
		v = (xb->pc_ram[a + 1] << 0) | (xb->pc_ram[a + 0] << 8);
	}
#if X86_DEBUG_BRIDGE > 1
	write_log(_T("x86_bridge_wget %08x (%08x,%d) PC=%08x\n"), addr - xb->baseaddress, a, mode, M68K_GETPC);
#endif
	return v;
}
static uae_u32 REGPARAM2 x86_bridge_lget(uaecptr addr)
{
	uae_u32 v;
	v = x86_bridge_wget(addr) << 16;
	v |= x86_bridge_wget(addr + 2);
	return v;
}
static uae_u32 REGPARAM2 x86_bridge_bget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u8 v = 0;
	struct x86_bridge *xb = get_x86_bridge(addr);
	if (!xb)
		return v;
	if (!xb->configured) {
		uaecptr offset = addr & 65535;
		if (offset >= sizeof xb->acmemory)
			return 0;
		return xb->acmemory[offset];
	}
	int mode;
	uaecptr a = get_x86_address(xb, addr, &mode);
	if (mode == ACCESS_MODE_WORD) {
		v = xb->pc_ram[a ^ 1];
	} else if (mode == ACCESS_MODE_IO) {
		v = x86_bridge_get_io(xb, a);
	} else if (mode >= 0) {
		v = xb->pc_ram[a];
	}
#if X86_DEBUG_BRIDGE > 1
	write_log(_T("x86_bridge_bget %08x (%08x,%d) %02x PC=%08x\n"), addr - xb->baseaddress, a, mode, v, M68K_GETPC);
#endif
	return v;
}

static void REGPARAM2 x86_bridge_wput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	struct x86_bridge *xb = get_x86_bridge(addr);
	if (!xb)
		return;
	int mode;
	uaecptr a = get_x86_address(xb, addr, &mode);

#if X86_DEBUG_BRIDGE > 1
	write_log(_T("pci_bridge_wput %08x (%08x,%d) %04x PC=%08x\n"), addr - xb->baseaddress, a, mode, b & 0xffff, M68K_GETPC);
#endif

	if (a >= 0xf8000 || mode < 0)
		return;

	if (mode == ACCESS_MODE_IO) {
		uae_u16 v = b;
		x86_bridge_put_io(xb, a, v & 0xff);
		x86_bridge_put_io(xb, a + 1, v >> 8);
	} else if (mode == ACCESS_MODE_WORD) {
		xb->pc_ram[a + 0] = b;
		xb->pc_ram[a + 1] = b >> 8;
	} else if (mode == ACCESS_MODE_GFX) {
		write_log(_T("ACCESS_MODE_GFX\n"));
	} else {
		xb->pc_ram[a + 1] = b;
		xb->pc_ram[a + 0] = b >> 8;
	}
}
static void REGPARAM2 x86_bridge_lput(uaecptr addr, uae_u32 b)
{
	x86_bridge_wput(addr, b >> 16);
	x86_bridge_wput(addr + 2, b);
}
static void REGPARAM2 x86_bridge_bput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	struct x86_bridge *xb = get_x86_bridge(addr);
	if (!xb)
		return;
	if (!xb->configured) {
		uaecptr offset = addr & 65535;
		switch (offset)
		{
			case 0x48:
			map_banks_z2(xb->bank, b, expamem_z2_size >> 16);
			xb->baseaddress = b << 16;
			xb->configured = 1;
			expamem_next(xb->bank, NULL);
			break;
			case 0x4c:
			xb->configured = -1;
			expamem_shutup(xb->bank);
			break;
		}
	}

	int mode;
	uaecptr a = get_x86_address(xb, addr, &mode);

	if (a >= 0xf8000 || mode < 0)
		return;

#if X86_DEBUG_BRIDGE > 1
	write_log(_T("x86_bridge_bput %08x (%08x,%d) %02x PC=%08x\n"), addr - xb->baseaddress, a, mode, b & 0xff, M68K_GETPC);
#endif

	if (mode == ACCESS_MODE_IO) {
		x86_bridge_put_io(xb, a, b);
	} else if (mode == ACCESS_MODE_WORD) {
		xb->pc_ram[a ^ 1] = b;
	} else {
		xb->pc_ram[a] = b;
	}
}

addrbank x86_bridge_bank = {
	x86_bridge_lget, x86_bridge_wget, x86_bridge_bget,
	x86_bridge_lput, x86_bridge_wput, x86_bridge_bput,
	default_xlate, default_check, NULL, NULL, _T("X86 BRIDGE"),
	x86_bridge_lget, x86_bridge_wget, ABFLAG_IO | ABFLAG_SAFE
};

void x86_bridge_rethink(void)
{
	struct x86_bridge *xb = bridges[0];
	if (!xb)
		return;
	if (!(xb->amiga_io[IO_CONTROL_REGISTER] & 1)) {
		uae_u8 intreq = xb->amiga_io[IO_AMIGA_INTERRUPT_STATUS];
		uae_u8 intena = xb->amiga_io[IO_INTERRUPT_MASK];
		uae_u8 status = intreq & ~intena;
		if (status)
			INTREQ_0(0x8000 | 0x0008);
	}
}

void x86_bridge_free(void)
{
	for (int i = 0; i < X86_BRIDGE_MAX; i++) {
		struct x86_bridge *xb = bridges[i];
		if (!xb)
			continue;
		xfree(xb->amiga_io);
		xfree(xb->io_ports);
		xfree(xb->pc_ram);
		bridges[i] = NULL;
	}
}

void x86_bridge_reset(void)
{
}

void x86_bridge_hsync(void)
{
	struct x86_bridge *xb = bridges[0];
	if (!xb)
		return;

	if (floppy_delay_hsync > 1 || floppy_delay_hsync < -1) {
		if (floppy_delay_hsync > 0)
			floppy_delay_hsync--;
		else
			floppy_delay_hsync++;
		if (floppy_delay_hsync == 1 || floppy_delay_hsync == -1)
			do_floppy_irq();
	}

	if (!xb->x86_reset) {
		exec86(30);
		timing();
		exec86(30);
		timing();
		exec86(30);
		timing();
	}
}

static void ew(uae_u8 *acmemory, int addr, uae_u8 value)
{
	if (addr == 00 || addr == 02 || addr == 0x40 || addr == 0x42) {
		acmemory[addr] = (value & 0xf0);
		acmemory[addr + 2] = (value & 0x0f) << 4;
	} else {
		acmemory[addr] = ~(value & 0xf0);
		acmemory[addr + 2] = ~((value & 0x0f) << 4);
	}
}

static void a1060_reset(struct x86_bridge *xb)
{
	xb->x86_reset = true;
	xb->configured = 0;
	xb->amiga_forced_interrupts = false;
	xb->amiga_irq = false;
	xb->pc_irq3a = xb->pc_irq3b = xb->pc_irq7 = false;
	memset(xb->amiga_io, 0, 0x10000);
	memset(xb->io_ports, 0, 0x10000);
	memset(xb->pc_ram, 0, 0x100000 - 32768);
	xb->amiga_io[IO_CONTROL_REGISTER] =	0xfe;
	xb->amiga_io[IO_PC_INTERRUPT_CONTROL] = 0xff;
	xb->amiga_io[IO_INTERRUPT_MASK] = 0xff;
	xb->amiga_io[IO_MODE_REGISTER] = 0x00;
	xb->amiga_io[IO_PC_INTERRUPT_STATUS] = 0xfe;
	inittiming();
}

int is_x86_cpu(struct uae_prefs *p)
{
	struct x86_bridge *xb = bridges[0];
	if (!xb) {
		if (is_device_rom(&currprefs, ROMTYPE_A1060, 0) < 0 &&
			is_device_rom(&currprefs, ROMTYPE_A2088XT, 0) < 0 &&
			is_device_rom(&currprefs, ROMTYPE_A2088T, 0) < 0)
			return X86_STATE_INACTIVE;
	}
	if (!xb || xb->x86_reset)
		return X86_STATE_STOP;
	return X86_STATE_ACTIVE;
}

static const uae_u8 a1060_autoconfig[16] = { 0xc4, 0x01, 0x80, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

addrbank *x86_bridge_init(struct romconfig *rc, int type)
{
	struct x86_bridge *xb = x86_bridge_alloc();
	if (!xb)
		return &expamem_null;
	bridges[0] = xb;

	xb->pc_ram = xcalloc(uae_u8, 0x100000);
	xb->io_ports = xcalloc(uae_u8, 0x10000);
	xb->amiga_io = xcalloc(uae_u8, 0x10000);
	xb->type = type;
	xb->pc_jumpers = (rc->device_settings & 0xff) ^ ((0x80 | 0x40) | (0x20 | 0x10));
	xb->settings = rc->device_settings;

	int ramsize = (xb->settings >> 2) & 3;
	switch(ramsize) {
		case 0:
		xb->pc_maxram = 128 * 1024;
		break;
		case 1:
		xb->pc_maxram = 256 * 1024;
		break;
		case 2:
		xb->pc_maxram = 512 * 1024;
		break;
		case 3:
		xb->pc_maxram = 640 * 1024;
		break;
	}

	floppy_high_density = xb->type >= TYPE_2088T;

	a1060_reset(xb);

	// load bios
	load_rom_rc(rc, NULL, 32768, 0, xb->pc_ram + 0xf8000, 32768, LOADROM_FILL);

	xb->bank = &x86_bridge_bank;
	for (int i = 0; i < sizeof a1060_autoconfig; i++) {
		ew(xb->acmemory, i * 4, a1060_autoconfig[i]);
	}

	return xb->bank;
}

addrbank *a1060_init(struct romconfig *rc)
{
	return x86_bridge_init(rc, TYPE_SIDECAR);
}
addrbank *a2088xt_init(struct romconfig *rc)
{
	return x86_bridge_init(rc, TYPE_2088);
}
addrbank *a2088t_init(struct romconfig *rc)
{
	return x86_bridge_init(rc, TYPE_2088T);
}
