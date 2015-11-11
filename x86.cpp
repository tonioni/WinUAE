/*
* UAE - The Un*x Amiga Emulator
*
* X86 Bridge board emulation
* A1060, A2088, A2088T, A2286, A2386
*
* Copyright 2015 Toni Wilen
* 8088 x86 and PC support chip emulation from Fake86.
* 80286+ CPU and AT support chip emulation from DOSBox.
*
*/

#define X86_DEBUG_BRIDGE 1
#define FLOPPY_IO_DEBUG 0
#define X86_DEBUG_BRIDGE_IO 0
#define X86_DEBUG_BRIDGE_IRQ 0
#define X86_IO_PORT_DEBUG 0
#define X86_DEBUG_SPECIAL_IO 0

#define DEBUG_DMA 0
#define DEBUG_PIT 0
#define DEBUG_INT 0

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
#include "zfile.h"
#include "disk.h"
#include "driveclick.h"
#include "scsi.h"
#include "idecontrollers.h"
#include "gfxboard.h"

#include "dosbox/dosbox.h"
#include "dosbox/mem.h"
#include "dosbox/paging.h"
#include "dosbox/setup.h"
#include "dosbox/cpu.h"
#include "dosbox/pic.h"
#include "dosbox/timer.h"
typedef Bits(CPU_Decoder)(void);
extern CPU_Decoder *cpudecoder;
extern Bit32s CPU_Cycles;

#define TYPE_SIDECAR 0
#define TYPE_2088 1
#define TYPE_2088T 2
#define TYPE_2286 3
#define TYPE_2386 4

void x86_reset(void);
int x86_execute(void);

void exec86(uint32_t execloops);
void reset86(int v20);
void intcall86(uint8_t intnum);

void x86_doirq(uint8_t irqnum);

static frame_time_t last_cycles;
#define DEFAULT_X86_INSTRUCTION_COUNT 120
static int x86_instruction_count;
static bool x86_turbo_allowed;
static bool x86_turbo_enabled;
bool x86_turbo_on;
bool x86_cpu_active;

void CPU_Init(Section*, int, int);
void CPU_ShutDown(Section*);
void CPU_JMP(bool use32, Bitu selector, Bitu offset, Bitu oldeip);
void PAGING_Init(Section * sec);
void MEM_Init(Section * sec);
void MEM_ShutDown(Section * sec);
void CMOS_Init(Section* sec, int);
void CMOS_Destroy(Section* sec);
void PIC_Init(Section* sec);
void PIC_Destroy(Section* sec);
void TIMER_Destroy(Section*);
void TIMER_Init(Section* sec);
void TIMER_SetGate2(bool);
bool TIMER_GetGate2(void);
static Section_prop *dosbox_sec;
Bitu x86_in_keyboard(Bitu port);
void x86_out_keyboard(Bitu port, Bitu val);
void cmos_selreg(Bitu port, Bitu val, Bitu iolen);
void cmos_writereg(Bitu port, Bitu val, Bitu iolen);
Bitu cmos_readreg(Bitu port, Bitu iolen);
void x86_pic_write(Bitu port, Bitu val);
Bitu x86_pic_read(Bitu port);
void x86_timer_write(Bitu port, Bitu val);
Bitu x86_timer_read(Bitu port);
void PIC_ActivateIRQ(Bitu irq);
void PIC_DeActivateIRQ(Bitu irq);
void PIC_runIRQs(void);
void KEYBOARD_AddBuffer(Bit8u data);
void FPU_Init(Section*);
Bit8u *x86_cmos_regs(Bit8u *regs);
void MEM_SetVGAHandler(void);
Bit32s ticksDone;
Bit32u ticksScheduled;
HostPt mono_start, mono_end, color_start, color_end;
int x86_memsize;
int x86_biosstart;
int x86_fpu_enabled;
int x86_cmos_bank;
int x86_xrom_start[2];
int x86_xrom_end[2];
int x86_vga_mode;

struct x86_bridge
{
	uae_u8 acmemory[128];
	addrbank *bank;
	int configured;
	int type;
	uaecptr baseaddress;
	int pc_maxram;
	uae_u8 *pc_ram;
	uae_u8 *pc_rom;
	uae_u8 *io_ports;
	uae_u8 *amiga_io;
	bool x86_reset;
	bool amiga_irq;
	bool amiga_forced_interrupts;
	bool pc_irq3a, pc_irq3b, pc_irq7;
	int delayed_interrupt;
	uae_u8 pc_jumpers;
	int pc_maxbaseram;
	int bios_size;
	int settings;
	int dosbox_cpu;
	int dosbox_cpu_arch;
	bool x86_reset_requested;
	struct zfile *cmosfile;
	uae_u8 cmosregs[3 * 0x40];
	uae_u8 vlsi_regs[0x100];
	int a2386_default_video;
	int cmossize;
	int scamp_idx1, scamp_idx2;
	uae_u8 rombank[1024 * 1024 / 4096];
	float dosbox_vpos_tick;
	float dosbox_tick_vpos_cnt;
	struct romconfig *rc;
};
static int x86_found;

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
#define IO_A2386_CONFIG 0x1f9f

#define ISVGA() (currprefs.rtgmem_type == GFXBOARD_VGA)

static struct x86_bridge *bridges[X86_BRIDGE_MAX];

static struct x86_bridge *x86_bridge_alloc(void)
{
	struct x86_bridge *xb = xcalloc(struct x86_bridge, 1);
	return xb;
};

void x86_init_reset(void)
{
	struct x86_bridge *xb = bridges[0];
	xb->x86_reset_requested = true;
	write_log(_T("8042 CPU reset requested\n"));
}

static void reset_dosbox_cpu(void)
{
	struct x86_bridge *xb = bridges[0];

	CPU_ShutDown(dosbox_sec);
	CPU_Init(dosbox_sec, xb->dosbox_cpu, xb->dosbox_cpu_arch);
	CPU_JMP(false, 0xffff, 0, 0);
}

static void reset_x86_cpu(struct x86_bridge *xb)
{
	write_log(_T("x86 CPU reset\n"));
	if (!xb->dosbox_cpu) {
		reset86(xb->type == ROMTYPE_A2088T);
	} else {
		reset_dosbox_cpu();
	}
	// reset x86 hd controllers
	x86_ide_hd_put(-1, 0, 0);
	x86_xt_hd_bput(-1, 0);
}

uint8_t x86_get_jumpers(void)
{
	struct x86_bridge *xb = bridges[0];
	uint8_t v = 0;

	if (xb->type >= TYPE_2286) {
		if (xb->pc_maxbaseram > 512 * 1024)
			v |= 0x20;
	}

	if (xb->type == TYPE_2386) {
		// A2386 = software controlled
		if (xb->a2386_default_video)
			v |= 0x40;
	} else {
		// Others have default video jumper
		if (!(xb->settings & (1 << 14))) {
			// default video mde, 0 = cga, 1 = mda
			v |= 0x40;
		}
	}

	if (!(xb->settings & (1 << 15))) {
		// key lock state: 0 = on, 1 = off
		v |= 0x80;
	}
	return v;
}

static uae_u8 get_mode_register(struct x86_bridge *xb, uae_u8 v)
{
	if (xb->type == TYPE_SIDECAR) {
		// PC/XT + keyboard
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
	} else if (xb->type < TYPE_2286) {
		// PC/XT
		v |= 0x80;
	} else {
		// AT
		v &= ~0x80;
	}
	return v;
}

static uae_u8 x86_bridge_put_io(struct x86_bridge *xb, uaecptr addr, uae_u8 v)
{
#if X86_DEBUG_BRIDGE_IO
	uae_u8 old = xb->amiga_io[addr];
	write_log(_T("IO write %08x %02x -> %02x\n"), addr, old, v);
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
		write_log(_T("IO_PC_INTERRUPT_STATUS %02X -> %02x\n"), old, v);
#endif
		break;
		case IO_NEGATE_PC_RESET:
		v = xb->amiga_io[addr];
		break;
		case IO_PC_INTERRUPT_CONTROL:
		if ((v & 1) || (v & (2 | 4 | 8)) != (2 | 4 | 8)) {
#if X86_DEBUG_BRIDGE_IRQ
			write_log(_T("IO_PC_INTERRUPT_CONTROL %02X -> %02x\n"), old, v);
#endif
			if (xb->type < TYPE_2286 && (v & 1))
				x86_doirq(1);
			if (!(v & 2) || !(v & 4))
				x86_doirq(3);
			if (!(v & 8))
				x86_doirq(7);
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
		write_log(_T("IO_CONTROL_REGISTER %02X -> %02x\n"), old, v);
#endif
		break;
		case IO_KEYBOARD_REGISTER_A1000:
		if (xb->type == TYPE_SIDECAR) {
#if X86_DEBUG_BRIDGE_IO
			write_log(_T("IO_KEYBOARD_REGISTER %02X -> %02x\n"), old, v);
#endif
			xb->io_ports[0x60] = v;
		}
		break;
		case IO_KEYBOARD_REGISTER_A2000:
		if (xb->type >= TYPE_2088) {
#if X86_DEBUG_BRIDGE_IO
			write_log(_T("IO_KEYBOARD_REGISTER %02X -> %02x\n"), old, v);
#endif
			xb->io_ports[0x60] = v;
			if (xb->type >= TYPE_2286) {
				KEYBOARD_AddBuffer(v);
			}
		}
		break;
		case IO_MODE_REGISTER:
		v = get_mode_register(xb, v);
#if X86_DEBUG_BRIDGE_IO
		write_log(_T("IO_MODE_REGISTER %02X -> %02x\n"), old, v);
#endif
		break;
		case IO_INTERRUPT_MASK:
#if X86_DEBUG_BRIDGE_IO
		write_log(_T("IO_INTERRUPT_MASK %02X -> %02x\n"), old, v);
#endif
		break;
		case IO_A2386_CONFIG:
		write_log(_T("A2386 CONFIG BYTE %02x\n"), v);
		if (v == 8 || v == 9) {
			xb->a2386_default_video = v & 1;
			write_log(_T("A2386 Default mode = %s\n"), xb->a2386_default_video ? _T("MDA") : _T("CGA"));
		}
		break;

		default:
		if (addr >= 0x400)
			write_log(_T("Unknown bridge IO write %08x = %02x\n"), addr, v);
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
				reset_x86_cpu(xb);
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

		default:
		if (addr >= 0x400)
			write_log(_T("Unknown bridge IO read %08x\n"), addr);
		break;
	}

#if X86_DEBUG_BRIDGE_IO > 1
	write_log(_T("IO read %08x %02x\n"), addr, v);
#endif
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

static uint64_t hostfreq = 313 * 50 * 227;
static uint64_t tickgap, curtick, lasttick, lasti8253tick, i8253tickgap;

static void inittiming()
{
	curtick = 0;
	lasti8253tick = lasttick = curtick;
	i8253tickgap = hostfreq / 119318;
}

static void timing(int count)
{
	struct x86_bridge *xb = bridges[0];

	curtick += count;

	if (i8253.active[0]) { //timer interrupt channel on i8253
		if (curtick >= (lasttick + tickgap)) {
			lasttick = curtick;
			x86_doirq(0);
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
#if DEBUG_PIT
	write_log("out8253(0x%X = %02x);\n", portnum, value);
#endif
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
	uint8_t out = 0;
#if DEBUG_PIT
	uint16_t portnum2 = portnum;
#endif
	portnum &= 3;
	switch (portnum)
	{
		case 0:
		case 1:
		case 2: //channel data
		if (latched == 2) {
			latched = 1;
			out = latched_val & 0xff;
		} else if (latched == 1) {
			latched = 0;
			out = latched_val >> 8;
		} else {
			if ((i8253.accessmode[portnum] == 0) || (i8253.accessmode[portnum] == PIT_MODE_LOBYTE) || ((i8253.accessmode[portnum] == PIT_MODE_TOGGLE) && (i8253.bytetoggle[portnum] == 0)))
				curbyte = 0;
			else if ((i8253.accessmode[portnum] == PIT_MODE_HIBYTE) || ((i8253.accessmode[portnum] == PIT_MODE_TOGGLE) && (i8253.bytetoggle[portnum] == 1)))
				curbyte = 1;
			if ((i8253.accessmode[portnum] == 0) || (i8253.accessmode[portnum] == PIT_MODE_TOGGLE))
				i8253.bytetoggle[portnum] = (~i8253.bytetoggle[portnum]) & 1;
			if (curbyte == 0) { //low byte
				out = ((uint8_t)i8253.counter[portnum]);
			} else {   //high byte
				out = ((uint8_t)(i8253.counter[portnum] >> 8));
			}
		}
		break;
	}
#if DEBUG_PIT
	write_log("in8253(0x%X = %02x);\n", portnum2, out);
#endif
	return out;
}

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

static struct dmachan_s dmachan[2 * 4];
static uae_u8 dmareg[2 * 16];
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
	int chipnum = 0;
	int reg = -1;
#if DEBUG_DMA
	write_log("out8237(0x%X, %X);\n", addr, value);
#endif
	if (addr >= 0x80 && addr <= 0x8f) {
		dmareg[addr & 0xf] = value;
		reg = addr;
	} else if (addr >= 0xc0 && addr <= 0xdf) {
		reg = (addr - 0xc0) / 2;
		chipnum = 4;
	} else if (addr <= 0x0f) {
		reg = addr;
		chipnum = 0;
	}
	switch (reg) {
		case 0x0:
		case 0x2: //channel 1 address register
		case 0x4:
		case 0x6:
		channel = reg / 2 + chipnum;
		if (flipflop == 1)
			dmachan[channel].addr = (dmachan[channel].addr & 0x00FF) | ((uint32_t)value << 8);
		else
			dmachan[channel].addr = (dmachan[channel].addr & 0xFF00) | value;
#if DEBUG_DMA
		if (flipflop == 1) write_log("[NOTICE] DMA channel %d address register = %04X\n", channel, dmachan[channel].addr);
#endif
		flipflop = ~flipflop & 1;
		break;
		case 0x1:
		case 0x3: //channel 1 count register
		case 0x5:
		case 0x7:
		channel = reg / 2 + chipnum;
		if (flipflop == 1)
			dmachan[channel].reload = (dmachan[channel].reload & 0x00FF) | ((uint32_t)value << 8);
		else
			dmachan[channel].reload = (dmachan[channel].reload & 0xFF00) | value;
		if (flipflop == 1) {
			if (dmachan[channel].reload == 0)
				dmachan[channel].reload = 65536;
			dmachan[channel].count = 0;
#if DEBUG_DMA
			write_log("[NOTICE] DMA channel %d reload register = %04X\n", channel, dmachan[channel].reload);
#endif
		}
		flipflop = ~flipflop & 1;
		break;
		case 0xA: //write single mask register
		channel = (value & 3) + chipnum;
		dmachan[channel].masked = (value >> 2) & 1;
#if DEBUG_DMA
		write_log("[NOTICE] DMA channel %u masking = %u\n", channel, dmachan[channel].masked);
#endif
		break;
		case 0xB: //write mode register
		channel = (value & 3) + chipnum;
		dmachan[channel].direction = (value >> 5) & 1;
		dmachan[channel].autoinit = (value >> 4) & 1;
		dmachan[channel].modeselect = (value >> 6) & 3;
		dmachan[channel].writemode = (value >> 2) & 1; //not quite accurate
		dmachan[channel].verifymode = ((value >> 2) & 3) == 0;
#if DEBUG_DMA
		write_log("[NOTICE] DMA channel %u write mode reg: direction = %u, autoinit = %u, write mode = %u, verify mode = %u, mode select = %u\n",
			   channel, dmachan[channel].direction, dmachan[channel].autoinit, dmachan[channel].writemode, dmachan[channel].verifymode, dmachan[channel].modeselect);
#endif
		break;
		case 0xC: //clear byte pointer flip-flop
#if DEBUG_DMA
		write_log("[NOTICE] DMA cleared byte pointer flip-flop\n");
#endif
		flipflop = 0;
		break;
		case 0x89: // 6
		case 0x8a: // 7
		case 0x8b: // 5
		case 0x81: // 2
		case 0x82: // 3
		case 0x83: // DMA channel 1 page register
		// Original PC design. It can't get any more stupid.
		if ((addr & 3) == 1)
			channel = 2;
		else if ((addr & 3) == 2)
			channel = 3;
		else
			channel = 1;
		if (addr >= 0x84)
			channel += 4;
		dmachan[channel].page = (uint32_t)value << 16;
#if DEBUG_DMA
		write_log("[NOTICE] DMA channel %d page base = %05X\n", channel, dmachan[channel].page);
#endif
		break;
	}
}

static uint8_t in8237(uint16_t addr)
{
	struct x86_bridge *xb = bridges[0];
	uint8_t channel;
	int reg = -1;
	int chipnum = addr >= 0xc0 ? 4 : 0;
	uint8_t out = 0;

	if (addr >= 0xc0 && addr <= 0xdf) {
		reg = (addr - 0xc0) / 2;
		chipnum = 4;
	} else if (addr <= 0x0f) {
		reg = addr;
		chipnum = 0;
	}
	switch (reg) {
		case 0x0:
		case 0x2: //channel 1 address register
		case 0x4:
		case 0x6:
		channel = reg / 2 + chipnum;
		flipflop = ~flipflop & 1;
		if (flipflop == 0)
			out = dmachan[channel].addr >> 8;
		else
			out = dmachan[channel].addr;
		break;
		case 0x1:
		case 0x3: //channel 1 count register
		case 0x5:
		case 0x7:
		channel = reg / 2 + chipnum;
		flipflop = ~flipflop & 1;
		if (flipflop == 0)
			out = dmachan[channel].reload >> 8;
		else
			out = dmachan[channel].reload;
		break;
	}
	if (addr >= 0x80 && addr <= 0x8f)
		out = dmareg[addr & 0xf];

#if DEBUG_DMA
	write_log("in8237(0x%X = %02x);\n", addr, out);
#endif
	return out;
}

static int keyboardwaitack;

void x86_ack_keyboard(void)
{
	if (keyboardwaitack)
		set_interrupt(bridges[0], 4);
	keyboardwaitack = 0;
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

static struct structpic i8259[2];

static uint8_t in8259(uint16_t portnum)
{
	struct x86_bridge *xb = bridges[0];
	uint8_t out = 0;

	int chipnum = (portnum & 0x80) ? 1 : 0;
	if (chipnum && xb->type < TYPE_2286)
		return 0;
	switch (portnum & 1) {
		case 0:
		if (i8259[chipnum].readmode == 0)
			out = (i8259[chipnum].irr);
		else
			out = (i8259[chipnum].isr);
		break;
		case 1: //read mask register
		out = (i8259[chipnum].imr);
		break;
	}
#if DEBUG_INT
	write_log("in8259(0x%X = %02x);\n", portnum, out);
#endif
	return out;
}

extern uint32_t makeupticks;

void out8259(uint16_t portnum, uint8_t value)
{
	struct x86_bridge *xb = bridges[0];
	uint8_t i;
	int chipnum = (portnum & 0x80) ? 1 : 0;

#if DEBUG_INT
	write_log("out8259(0x%X = %02x);\n", portnum, value);
#endif

	if (chipnum && xb->type < TYPE_2286)
		return;
	switch (portnum & 1) {
		case 0:
		if (value & 0x10) { //begin initialization sequence
			i8259[chipnum].icwstep = 1;
			i8259[chipnum].imr = 0; //clear interrupt mask register
			i8259[chipnum].icw[i8259[chipnum].icwstep++] = value;
			return;
		}
		if ((value & 0x98) == 8) { //it's an OCW3
			if (value & 2)
				i8259[chipnum].readmode = value & 2;
		}
		if (value & 0x20) { //EOI command
			if (!chipnum) {
				x86_ack_keyboard();
				keyboardwaitack = 0;
			}
			for (i = 0; i < 8; i++)
				if ((i8259[chipnum].isr >> i) & 1) {
					i8259[chipnum].isr ^= (1 << i);
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
		if ((i8259[chipnum].icwstep == 3) && (i8259[chipnum].icw[1] & 2))
			i8259[chipnum].icwstep = 4; //single mode, so don't read ICW3
		if (i8259[chipnum].icwstep<5) {
			i8259[chipnum].icw[i8259[chipnum].icwstep++] = value;
			return;
		}
		//if we get to this point, this is just a new IMR value
		i8259[chipnum].imr = value;
		break;
	}
}

static uint8_t nextintr(void)
{
	uint8_t i, tmpirr;
	tmpirr = i8259[0].irr & (~i8259[0].imr); //XOR request register with inverted mask register
	for (i = 0; i<8; i++) {
		if ((tmpirr >> i) & 1) {
			i8259[0].irr ^= (1 << i);
			i8259[0].isr |= (1 << i);
			return(i8259[0].icw[2] + i);
		}
	}
	return(0); //this won't be reached, but without it the compiler gives a warning
}

void x86_clearirq(uint8_t irqnum)
{
	struct x86_bridge *xb = bridges[0];
	if (xb->dosbox_cpu) {
		PIC_DeActivateIRQ(irqnum);
	}
}

void x86_doirq(uint8_t irqnum)
{
	struct x86_bridge *xb = bridges[0];
	if (xb->dosbox_cpu) {
		PIC_ActivateIRQ(irqnum);
	} else {
		i8259[0].irr |= (1 << irqnum);
	}
	if (irqnum == 1)
		keyboardwaitack = 1;
}

void x86_doirq_keyboard(void)
{
	x86_doirq(1);
}

void check_x86_irq(void)
{
	struct x86_bridge *xb = bridges[0];
	if (xb->dosbox_cpu) {
		PIC_runIRQs();
	} else {
		if (i8259[0].irr & (~i8259[0].imr)) {
			uint8_t irq = nextintr();
			intcall86(irq);	/* get next interrupt from the i8259, if any */
		}
	}
}

struct pc_floppy
{
	int seek_offset;
	int phys_cyl;
	int cyl;
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
static int floppy_seeking[4];
static uae_u8 floppy_seekcyl[4];
static int floppy_delay_hsync;
static bool floppy_did_reset;
static bool floppy_irq;
static uae_s8 floppy_rate;

#define PC_SEEK_DELAY 50

static void floppy_reset(void)
{
	struct x86_bridge *xb = bridges[0];

	write_log(_T("Floppy reset\n"));
	floppy_idx = 0;
	floppy_dir = 0;
	floppy_did_reset = true;
	if (xb->type == TYPE_2286) {
		// apparently A2286 BIOS AT driver assumes
		// floppy reset also resets IDE.
		// Perhaps this is forgotten feature from
		// Commodore PC model that uses same BIOS variant?
		x86_ide_hd_put(-1, 0, 0);
	}
}

static void floppy_hardreset(void)
{
	floppy_rate = -1;
	floppy_reset();
}


static void do_floppy_irq2(void)
{
	write_log(_T("floppy%d irq (enable=%d)\n"), floppy_num, (floppy_dpc & 8) != 0);
	if (floppy_dpc & 8) {
		floppy_irq = true;
		x86_doirq(6);
	}
}

static void do_floppy_irq(void)
{
	if (floppy_delay_hsync > 0) {
		do_floppy_irq2();
	}
	floppy_delay_hsync = 0;
}

static void do_floppy_seek(int num, int error)
{
	struct pc_floppy *pcf = &floppy_pc[floppy_num];

	disk_reserved_reset_disk_change(num);
	if (!error) {
		struct floppy_reserved fr = { 0 };
		bool valid_floppy = disk_reserved_getinfo(floppy_num, &fr);
		if (floppy_seekcyl[num] != pcf->phys_cyl) {
			if (floppy_seekcyl[num] > pcf->phys_cyl)
				pcf->phys_cyl++;
			else if (pcf->phys_cyl > 0)
				pcf->phys_cyl--;

#ifdef DRIVESOUND
			if (valid_floppy)
				driveclick_click(fr.num, pcf->phys_cyl);
#endif
			write_log(_T("Floppy%d seeking.. %d\n"), floppy_num, pcf->phys_cyl);

			if (pcf->phys_cyl - pcf->seek_offset <= 0) {
				pcf->phys_cyl = 0;
				if (pcf->seek_offset) {
					floppy_seekcyl[num] = 0;
					write_log(_T("Floppy%d early track zero\n"), floppy_num);
					pcf->seek_offset = 0;
				}
			}

			if (valid_floppy && fr.cyls < 80 && fr.drive_cyls >=80)
				pcf->cyl = pcf->phys_cyl / 2;
			else
				pcf->cyl = pcf->phys_cyl;

			floppy_seeking[num] = PC_SEEK_DELAY;
			disk_reserved_setinfo(floppy_num, pcf->cyl, pcf->head, 1);
			return;
		}

		if (valid_floppy && pcf->phys_cyl > fr.drive_cyls + 3) {
			pcf->seek_offset = pcf->phys_cyl - (fr.drive_cyls + 3);
			pcf->phys_cyl = fr.drive_cyls + 3;
			goto done;
		}

		if (valid_floppy && fr.cyls < 80 && fr.drive_cyls >= 80)
			pcf->cyl = pcf->phys_cyl / 2;
		else
			pcf->cyl = pcf->phys_cyl;

		if (floppy_seekcyl[num] != pcf->phys_cyl)
			return;
	}

done:
	write_log(_T("Floppy%d seek done err=%d. pcyl=%d cyl=%d h=%d\n"), floppy_num, error, pcf->phys_cyl,pcf->cyl, pcf->head);

	floppy_status[0] = 0;
	floppy_status[0] |= 0x20; // seek end
	if (error) {
		floppy_status[0] |= 0x40; // abnormal termination
		floppy_status[0] |= 0x10; // equipment check
	}
	floppy_status[0] |= num;
	floppy_status[0] |= pcf->head ? 4 : 0;
	do_floppy_irq2();
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

static bool floppy_valid_rate(struct floppy_reserved *fr)
{
	struct x86_bridge *xb = bridges[0];
	// A2386 BIOS sets 720k data rate for both 720k and 1.4M drives
	// probably because it thinks Amiga half-speed drive is connected?
	if (xb->type == TYPE_2386 && fr->rate == 0 && floppy_rate == 2)
		return true;
	return fr->rate == floppy_rate || floppy_rate < 0;
}

static void floppy_do_cmd(struct x86_bridge *xb)
{
	uae_u8 cmd = floppy_cmd[0];
	struct pc_floppy *pcf = &floppy_pc[floppy_num];
	struct floppy_reserved fr = { 0 };
	bool valid_floppy;

	valid_floppy = disk_reserved_getinfo(floppy_num, &fr);

	if (floppy_cmd_len) {
		write_log(_T("Command: "));
		for (int i = 0; i < floppy_cmd_len; i++) {
			write_log(_T("%02x "), floppy_cmd[i]);
		}
		write_log(_T("\n"));
	}

	if (cmd == 8) {
		write_log(_T("Floppy%d Sense Interrupt Status\n"), floppy_num);
		floppy_cmd_len = 2;
		if (floppy_irq) {
			write_log(_T("Floppy interrupt reset\n"));
		}
		if (!floppy_irq) {
			floppy_status[0] = 0x80;
			floppy_cmd_len = 1;
		} else if (floppy_did_reset) {
			floppy_did_reset = false;
			// 0xc0 status after reset.
			floppy_status[0] = 0xc0;
		}
		x86_clearirq(6);
		floppy_irq = false;
		floppy_result[0] = floppy_status[0];
		floppy_result[1] = pcf->phys_cyl;
		floppy_status[0] = 0;
		goto end;
	}

	memset(floppy_status, 0, sizeof floppy_status);
	memset(floppy_result, 0, sizeof floppy_result);
	if (floppy_exists() >= 0) {
		if (pcf->head)
			floppy_status[0] |= 4;
		floppy_status[3] |= pcf->phys_cyl == 0 ? 0x10 : 0x00;
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
		write_log(_T("Floppy%d Specify SRT=%d HUT=%d HLT=%d ND=%d\n"), floppy_num, floppy_cmd[1] >> 4, floppy_cmd[1] & 0x0f, floppy_cmd[2] >> 1, floppy_cmd[2] & 1);
		floppy_delay_hsync = -5;
		break;

		case 4:
		write_log(_T("Floppy%d Sense Drive Status\n"), floppy_num);
		floppy_delay_hsync = 5;
		floppy_result[0] = floppy_status[3];
		floppy_cmd_len = 1;
		break;

		case 5:
		{
			write_log(_T("Floppy%d write MT=%d MF=%d C=%d:H=%d:R=%d:N=%d:EOT=%d:GPL=%d:DTL=%d\n"),
					  (floppy_cmd[0] & 0x80) ? 1 : 0, (floppy_cmd[0] & 0x40) ? 1 : 0,
					  floppy_cmd[2], floppy_cmd[3], floppy_cmd[4], floppy_cmd[5],
					  floppy_cmd[6], floppy_cmd[7], floppy_cmd[8]);
			write_log(_T("DMA addr %08x len %04x\n"), dmachan[2].page | dmachan[2].addr, dmachan[2].reload);
			floppy_delay_hsync = 50;
			int eot = floppy_cmd[6];
			bool mt = (floppy_cmd[0] & 0x80) != 0;
			int cyl = pcf->cyl;
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
						if (pcf->sector == eot) {
							pcf->sector = 0;
							if (mt) {
								if (pcf->head)
									pcf->cyl++;
								pcf->head ^= 1;
							}
							break;
						}
						if (pcf->sector >= fr.secs) {
							pcf->sector = 0;
							pcf->head ^= 1;
						}
					}
					floppy_result[3] = cyl;
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
				floppy_cmd[6], floppy_cmd[7], floppy_cmd[8]);
			write_log(_T("DMA addr %08x len %04x\n"), dmachan[2].page | dmachan[2].addr, dmachan[2].reload);
			floppy_delay_hsync = 50;
			int eot = floppy_cmd[6];
			bool mt = (floppy_cmd[0] & 0x80) != 0;
			int cyl = pcf->cyl;
			bool nodata = false;
			if (valid_floppy) {
				if (!floppy_valid_rate(&fr)) {
					nodata = true;
				} else if (pcf->head && fr.heads == 1) {
					nodata = true;
				} else if (fr.img && pcf->cyl != floppy_cmd[2]) {
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
						if (pcf->sector == eot) {
							pcf->sector = 0;
							if (mt) {
								if (pcf->head)
									pcf->cyl++;
								pcf->head ^= 1;
							}
							break;
						}
						if (pcf->sector >= fr.secs) {
							pcf->sector = 0;
							pcf->head ^= 1;
						}
					}
					floppy_result[3] = cyl;
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
		if (valid_floppy) {
			floppy_seekcyl[floppy_num] = 0;
			floppy_seeking[floppy_num] = PC_SEEK_DELAY;
		} else {
			floppy_seeking[floppy_num] = -PC_SEEK_DELAY;
		}
		break;

		case 10:
		write_log(_T("Floppy read ID\n"));
		if (!valid_floppy || !fr.img || !floppy_valid_rate(&fr) || (pcf->head && fr.heads == 1)) {
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

		if (valid_floppy && fr.img) {
			pcf->sector++;
			pcf->sector %= fr.secs;
		}

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
			write_log(_T("Floppy%d seek %d->%d (max %d)\n"), floppy_num, pcf->phys_cyl, newcyl, fr.cyls);
			floppy_seekcyl[floppy_num] = newcyl;
			floppy_seeking[floppy_num] = valid_floppy ? PC_SEEK_DELAY : -PC_SEEK_DELAY;
			pcf->head = (floppy_cmd[1] & 4) ? 1 : 0;
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
	if (!x86_turbo_allowed) {
		x86_turbo_allowed = true;
	}

	switch (portnum)
	{
		case 0x3f2: // dpc
		if ((v & 4) && !(floppy_dpc & 4)) {
			floppy_reset();
			floppy_delay_hsync = 20;
		}
#if FLOPPY_IO_DEBUG
		write_log(_T("DPC=%02x: Motormask %02x sel=%d dmaen=%d reset=%d\n"), v, (v >> 4) & 15, v & 3, (v & 8) ? 1 : 0, (v & 4) ? 0 : 1);
#endif
#ifdef DRIVESOUND
		for (int i = 0; i < 2; i++) {
			int mask = 0x10 << i;
			if ((floppy_dpc & mask) != (v & mask)) {
				struct floppy_reserved fr = { 0 };
				bool valid_floppy = disk_reserved_getinfo(i, &fr);
				if (valid_floppy)
					driveclick_motor(fr.num, (v & mask) ? 1 : 0);
			}
		}
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
		case 0x3f7: // configuration control
		if (xb->type >= TYPE_2286) {
			write_log(_T("FDC Control Register %02x\n"), v);
			floppy_rate = v & 3;
		} else {
			floppy_rate = -1;
		}
		break;
		default:
		write_log(_T("Unknown FDC %04x write %02x\n"), portnum, v);
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
		if (!floppy_delay_hsync && (floppy_dpc & 4))
			v |= 0x80;
		if (floppy_idx || floppy_delay_hsync)
			v |= 0x10;
		if ((v & 0x80) && floppy_dir)
			v |= 0x40;
		for (int i = 0; i < 4; i++) {
			if (floppy_seeking[i])
				v |= 1 << i;
		}
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
		case 0x3f7: // digital input register
		if (xb->type >= TYPE_2286) {
			struct floppy_reserved fr = { 0 };
			bool valid_floppy = disk_reserved_getinfo(floppy_num, &fr);
			v = 0x00;
			if (valid_floppy && fr.disk_changed)
				v = 0x80;
		}
		break;
		default:
		write_log(_T("Unknown FDC %04x read\n"), portnum);
		break;
	}
#if FLOPPY_IO_DEBUG
	write_log(_T("in  floppy port %04x %02x\n"), portnum, v);
#endif
	return v;
}

void bridge_mono_hit(void)
{
	struct x86_bridge *xb = bridges[0];
	if (xb->amiga_io[IO_MODE_REGISTER] & 8)
		set_interrupt(xb, 0);
}
void bridge_color_hit(void)
{
	struct x86_bridge *xb = bridges[0];
	if (xb->amiga_io[IO_MODE_REGISTER] & 16)
		set_interrupt(xb, 1);
}


static void set_pc_address_access(struct x86_bridge *xb, uaecptr addr)
{
	if (addr >= 0xb0000 && addr < 0xb2000) {
		// mono
		if (xb->amiga_io[IO_MODE_REGISTER] & 8) {
			xb->delayed_interrupt |= 1 << 0;
			//set_interrupt(xb, 0);
		}
	}
	if (addr >= 0xb8000 && addr < 0xc0000) {
		// color
		if (xb->amiga_io[IO_MODE_REGISTER] & 16) {
			xb->delayed_interrupt |= 1 << 1;
			//set_interrupt(xb, 1);
		}
	}
}

static void set_pc_io_access(struct x86_bridge *xb, uaecptr portnum, bool write)
{
	uae_u8 mode_register = xb->amiga_io[IO_MODE_REGISTER];
	if (write && (portnum == 0x3b1 || portnum == 0x3b3 || portnum == 0x3b5 || portnum == 0x3b7 || portnum == 0x3b8)) {
		// mono crt data register
		if (mode_register & 8) {
			xb->delayed_interrupt |= 1 << 2;
			//set_interrupt(xb, 2);
		}
	} else if (write && (portnum == 0x3d1 || portnum == 0x3d3 || portnum == 0x3d5 || portnum == 0x3d7 || portnum == 0x3d8 || portnum == 0x3d9 || portnum == 0x3dd)) {
			// color crt data register
		if (mode_register & 16) {
			xb->delayed_interrupt |= 1 << 3;
			//set_interrupt(xb, 3);
		}
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
	return true;
}

static uae_u8 get0x3da(struct x86_bridge *xb)
{
	static int toggle;
	uae_u8 v = 0;
	// not really correct but easy.
	if (vpos < 40) {
		v |= 8 | 1;
	}
	if (toggle) {
		// hblank or vblank active
		v |= 1;
	}
	// just toggle to keep programs happy and fast..
	toggle = !toggle;
	return v;
}

static uae_u8 vlsi_in(struct x86_bridge *xb, int portnum)
{
	uae_u8 v = 0;
	switch(portnum)
	{
		case 0xed:
		v = xb->vlsi_regs[xb->scamp_idx2];
		break;
	}
	//write_log(_T("VLSI_IN %02x = %02x\n"), portnum, v);
	return v;
}

static void vlsi_reg_out(struct x86_bridge *xb, int reg, uae_u8 v)
{
	uae_u32 shadow_start = 0;
	uae_u32 shadow_size = 0;

	switch(reg)
	{
		case 0x1d:
		x86_cmos_bank = (v & 0x20) ? 1 : 0;
		break;
		case 0x0e: // ABAX
		shadow_size = 0x8000;
		shadow_start = 0xa0000;
		break;
		case 0x0f: // CAXS
		shadow_size = 0x4000;
		shadow_start = 0xc0000;
		break;
		case 0x10: // DAXS
		shadow_size = 0x4000;
		shadow_start = 0xd0000;
		break;
		case 0x11: // FEAXS
		shadow_size = 0x4000;
		shadow_start = 0xe0000;
		break;
	}
	if (shadow_size) {
		for (int i = 0; i < 4; i++) {
			int state = (v >> (i * 2)) & 3;
			write_log(_T("%06x - %06x : shadow status=%d\n"), shadow_start, shadow_start + shadow_size - 1, state);
			shadow_start += shadow_size;
		}

	}
}

static void vlsi_out(struct x86_bridge *xb, int portnum, uae_u8 v)
{
	switch (portnum)
	{
		case 0xe8:
		xb->scamp_idx1 = v;
		break;
		case 0xec:
		xb->scamp_idx2 = v;
		break;
		case 0xed:
		xb->vlsi_regs[xb->scamp_idx2] = v;
		vlsi_reg_out(xb, xb->scamp_idx2, v);
		break;
	}
	//write_log(_T("VLSI_OUT %02x = %02x\n"), portnum, v);
}


void portout(uint16_t portnum, uint8_t v)
{
	struct x86_bridge *xb = bridges[0];
	uae_u8 *io = xb->io_ports;
	int aio = -1;
	uae_u8 enables = xb->amiga_io[IO_MODE_REGISTER];
	bool mda_emu = (enables & 8) != 0;
	bool cga_emu = (enables & 16) != 0;

	if (portnum >= 0x400)
		return;

	if (!is_port_enabled(xb, portnum))
		return;

	set_pc_io_access(xb, portnum, true);

	switch(portnum)
	{
		case 0x20:
		case 0x21:
		case 0xa0:
		case 0xa1:
		if (xb->dosbox_cpu)
			x86_pic_write(portnum, v);
		else
			out8259(portnum, v);
		break;
		case 0x40:
		case 0x41:
		case 0x42:
		case 0x43:
		if (xb->dosbox_cpu)
			x86_timer_write(portnum, v);
		else
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

		case 0xc0:
		case 0xc1:
		case 0xc2:
		case 0xc3:
		case 0xc4:
		case 0xc5:
		case 0xc6:
		case 0xc7:
		case 0xc8:
		case 0xc9:
		case 0xca:
		case 0xcb:
		case 0xcc:
		case 0xcd:
		case 0xce:
		case 0xcf:
		case 0xd0:
		case 0xd1:
		case 0xd2:
		case 0xd3:
		case 0xd4:
		case 0xd5:
		case 0xd6:
		case 0xd7:
		case 0xd8:
		case 0xd9:
		case 0xda:
		case 0xdb:
		case 0xdc:
		case 0xdd:
		case 0xde:
		case 0xdf:

		out8237(portnum, v);
		break;

		case 0x60:
		if (xb->type >= TYPE_2286) {
			x86_out_keyboard(0x60, v);
		} else {
			aio = 0x41f;
		}
		break;
		case 0x61:
		//write_log(_T("OUT Port B %02x\n"), v);
		if (xb->type >= TYPE_2286) {
			x86_out_keyboard(0x61, v);
		} else {
			if (xb->dosbox_cpu) {
				TIMER_SetGate2(v & 1);
			}
			aio = 0x5f;
		}
		break;
		case 0x62:
#if X86_DEBUG_SPECIAL_IO
		write_log(_T("AMIGA SYSINT. %02x\n"), v);
#endif
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
#if X86_DEBUG_SPECIAL_IO
		write_log(_T("OUT CONFIG %02x\n"), v);
#endif
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
		case 0x64:
		if (xb->type >= TYPE_2286) {
			x86_out_keyboard(0x64, v);
		}
		break;

		case 0x70:
		if (xb->type >= TYPE_2286)
			cmos_selreg(portnum, v, 1);
		break;
		case 0x71:
		if (xb->type >= TYPE_2286)
			cmos_writereg(portnum, v, 1);
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

		// vga
		case 0x3c2:
		x86_vga_mode = v & 1;
		case 0x3c0:
		case 0x3c1:
		case 0x3c3:
		case 0x3c4:
		case 0x3c5:
		case 0x3c6:
		case 0x3c7:
		case 0x3c8:
		case 0x3c9:
		case 0x3ca:
		case 0x3cb:
		case 0x3cc:
		case 0x3cd:
		case 0x3ce:
		case 0x3cf:
		case 0x3b9:
		if (ISVGA()) {
			vga_io_put(portnum, v);
		}
		break;

		// mono video
		case 0x3b0:
		case 0x3b2:
		case 0x3b4:
		case 0x3b6:
		if (mda_emu) {
			aio = 0x1ff;
		} else if (ISVGA()) {
			if (x86_vga_mode == 0)
				vga_io_put(portnum, v);
		}
		break;
		case 0x3b1:
		case 0x3b3:
		case 0x3b5:
		case 0x3b7:
		if (mda_emu) {
			aio = 0x2a1 + (xb->amiga_io[0x1ff] & 15) * 2;
		} else if (ISVGA()) {
			if (x86_vga_mode == 0)
				vga_io_put(portnum, v);
		}
		break;
		case 0x3b8:
		if (mda_emu) {
			aio = 0x2ff;
		}
		break;

		// color video
		case 0x3d0:
		case 0x3d2:
		case 0x3d4:
		case 0x3d6:
		if (cga_emu) {
			aio = 0x21f;
		} else if (ISVGA()) {
			if (x86_vga_mode == 1)
				vga_io_put(portnum, v);
		}
		break;
		case 0x3d1:
		case 0x3d3:
		case 0x3d5:
		case 0x3d7:
		if (cga_emu) {
			aio = 0x2c1 + (xb->amiga_io[0x21f] & 15) * 2;
		} else if (ISVGA()) {
			if (x86_vga_mode == 1)
				vga_io_put(portnum, v);
		}
		break;
		case 0x3d8:
		if (cga_emu) {
			aio = 0x23f;
		}
		break;
		case 0x3d9:
		if (cga_emu) {
			aio = 0x25f;
		}
		break;
		case 0x3dd:
		if (cga_emu) {
			aio = 0x29f;
		}
		break;

		case 0x3ba:
		if (cga_emu) {
			aio = 0x1f;
		} else if (ISVGA()) {
			if (x86_vga_mode == 0)
				vga_io_put(portnum, v);
		}
		break;
		case 0x3da:
		if (cga_emu) {
			aio = 0x1f;
		} else if (ISVGA()) {
			if (x86_vga_mode == 1)
				vga_io_put(portnum, v);
		}
		break;

		case 0x378:
		write_log(_T("BIOS DIAGNOSTICS CODE: %02x ~%02X\n"), v, v ^ 0xff);
		aio = 0x19f; // ??
		break;
		case 0x379:
#if X86_DEBUG_SPECIAL_IO
		write_log(_T("0x379: %02x\n"), v);
#endif
		if (xb->amiga_io[IO_MODE_REGISTER] & 2) {
			xb->amiga_forced_interrupts = (v & 40) ? false : true;
		}
		aio = 0x19f; // ??
		break;
		case 0x37a:
		aio = 0x1df;
		break;

		case 0x3bb:
		case 0x3bc:
		case 0x3bd:
		case 0x3be:
		case 0x3bf:
		if (mda_emu) {
			aio = 0x1f;
		}
		break;

		case 0x3de:
		case 0x3df:
		if (cga_emu) {
			aio = 0x1f;
		}
		break;

		// A2386SX only
		case 0xe8:
		case 0xe9:
		case 0xea:
		case 0xeb:
		case 0xec:
		case 0xed:
		case 0xee:
		case 0xef:
		case 0xf0:
		case 0xf1:
		case 0xf4:
		case 0xf5:
		case 0xf8:
		case 0xf9:
		case 0xfa:
		case 0xfb:
		case 0xfc:
		case 0xfe:
		if (xb->type >= TYPE_2386)
			vlsi_out(xb, portnum, v);
		break;

		// floppy
		case 0x3f0:
		case 0x3f1:
		case 0x3f2:
		case 0x3f3:
		case 0x3f4:
		case 0x3f5:
		case 0x3f7:
		outfloppy(xb, portnum, v);
		break;

		// at ide 1
		case 0x170:
		case 0x171:
		case 0x172:
		case 0x173:
		case 0x174:
		case 0x175:
		case 0x176:
		case 0x177:
		case 0x376:
		x86_ide_hd_put(portnum, v, 0);
		break;
		// at ide 0
		case 0x1f0:
		case 0x1f1:
		case 0x1f2:
		case 0x1f3:
		case 0x1f4:
		case 0x1f5:
		case 0x1f6:
		case 0x1f7:
		case 0x3f6:
		x86_ide_hd_put(portnum, v, 0);
		break;

		// xt hd
		case 0x320:
		case 0x321:
		case 0x322:
		case 0x323:
		x86_xt_hd_bput(portnum, v);
		break;

		// universal xt bios
		case 0x300:
		case 0x301:
		case 0x302:
		case 0x303:
		case 0x304:
		case 0x305:
		case 0x306:
		case 0x307:
		case 0x308:
		case 0x309:
		case 0x30a:
		case 0x30b:
		case 0x30c:
		case 0x30d:
		case 0x30e:
		case 0x30f:
		x86_ide_hd_put(portnum, v, 0);
		break;

		case 0x101:
		case 0x102:
		// A2286 BIOS timer test fails if CPU is too fast
		// so we'll run normal speed until tests are done
		if (!x86_turbo_allowed) {
			x86_turbo_allowed = true;
		}
		break;

		default:
		write_log(_T("X86_OUT unknown %02x %02x\n"), portnum, v);
		break;
	}

#if X86_IO_PORT_DEBUG
	write_log(_T("X86_OUT %08x %02X\n"), portnum, v);
#endif
	if (aio >= 0)
		xb->amiga_io[aio] = v;
	xb->io_ports[portnum] = v;
}
void portout16(uint16_t portnum, uint16_t value)
{
	switch (portnum)
	{
		case 0x170:
		case 0x1f0:
		case 0x300:
		x86_ide_hd_put(portnum, value, 1);
		break;
		default:
		portout(portnum, value);
		portout(portnum + 1, value >> 8);
		break;
	}
}
static void portout32(uint16_t portnum, uint32_t value)
{
	switch (portnum)
	{
		case 0x170:
		case 0x1f0:
		case 0x300:
		x86_ide_hd_put(portnum, value, 1);
		x86_ide_hd_put(portnum, value >> 16, 1);
		break;
		default:
		write_log(_T("portout32 %08x %08x\n"), portnum, value);
		break;
	}
}

uint8_t portin(uint16_t portnum)
{
	struct x86_bridge *xb = bridges[0];
	int aio = -1;
	uae_u8 enables = xb->amiga_io[IO_MODE_REGISTER];
	bool mda_emu = (enables & 8) != 0;
	bool cga_emu = (enables & 16) != 0;

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
		case 0xa0:
		case 0xa1:
		if (xb->dosbox_cpu)
			v = x86_pic_read(portnum);
		else
			v = in8259(portnum);
		break;

		case 0x40:
		case 0x41:
		case 0x42:
		case 0x43:
		if (xb->dosbox_cpu)
			v = x86_timer_read(portnum);
		else
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

		case 0xc0:
		case 0xc1:
		case 0xc2:
		case 0xc3:
		case 0xc4:
		case 0xc5:
		case 0xc6:
		case 0xc7:
		case 0xc8:
		case 0xc9:
		case 0xca:
		case 0xcb:
		case 0xcc:
		case 0xcd:
		case 0xce:
		case 0xcf:
		case 0xd0:
		case 0xd1:
		case 0xd2:
		case 0xd3:
		case 0xd4:
		case 0xd5:
		case 0xd6:
		case 0xd7:
		case 0xd8:
		case 0xd9:
		case 0xda:
		case 0xdb:
		case 0xdc:
		case 0xdd:
		case 0xde:
		case 0xdf:

		v = in8237(portnum);
		break;

		case 0x71:
		if (xb->type >= TYPE_2286)
			v = cmos_readreg(portnum, 1);
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

		// vga
		case 0x3c0:
		case 0x3c1:
		case 0x3c2:
		case 0x3c3:
		case 0x3c4:
		case 0x3c5:
		case 0x3c6:
		case 0x3c7:
		case 0x3c8:
		case 0x3c9:
		case 0x3ca:
		case 0x3cb:
		case 0x3cc:
		case 0x3cd:
		case 0x3ce:
		case 0x3cf:
		case 0x3b9:
		if (ISVGA()) {
			v = vga_io_get(portnum);
		}
		break;

		// mono video
		case 0x3b0:
		xb->pc_irq3a = false;
		aio = 0x1ff;
		break;
		case 0x3b2:
		case 0x3b4:
		case 0x3b6:
		if (mda_emu) {
			aio = 0x1ff;
		} else if (ISVGA()) {
			if (x86_vga_mode == 0)
				v = vga_io_get(portnum);
		}
		break;
		case 0x3b1:
		case 0x3b3:
		case 0x3b5:
		case 0x3b7:
		if (mda_emu) {
			aio = 0x2a1 + (xb->amiga_io[0x1ff] & 15) * 2;
		} else if (ISVGA()) {
			if (x86_vga_mode == 0)
				v = vga_io_get(portnum);
		}
		break;
		case 0x3b8:
		if (mda_emu) {
			aio = 0x2ff;
		}
		break;

		// color video
		case 0x3d0:
		case 0x3d2:
		case 0x3d4:
		case 0x3d6:
		if (cga_emu) {
			aio = 0x21f;
		} else if (ISVGA()) {
			if (x86_vga_mode == 1)
				v = vga_io_get(portnum);
		}
		break;
		case 0x3d1:
		case 0x3d3:
		case 0x3d5:
		case 0x3d7:
		if (cga_emu) {
			aio = 0x2c1 + (xb->amiga_io[0x21f] & 15) * 2;
		} else if (ISVGA()) {
			if (x86_vga_mode == 1)
				v = vga_io_get(portnum);
		}
		break;
		case 0x3d8:
		if (cga_emu) {
			aio = 0x23f;
		}
		break;
		case 0x3d9:
		if (cga_emu) {
			aio = 0x25f;
		}
		break;

		case 0x3ba:
		if (mda_emu) {
			v = get0x3da(xb);
		} else if (ISVGA()) {
			if (x86_vga_mode == 0)
				v = vga_io_get(portnum);
		}
		break;
		case 0x3da:
		if (cga_emu) {
			v = get0x3da(xb);
		} else if (ISVGA()) {
			if (x86_vga_mode == 1)
				v = vga_io_get(portnum);
		}
		break;
		case 0x3dd:
		if (cga_emu) {
			aio = 0x29f;
		}
		break;

		case 0x3bb:
		case 0x3bc:
		case 0x3bd:
		case 0x3be:
		case 0x3bf:
		if (mda_emu) {
			aio = 0x1f;
		}
		break;
		case 0x3de:
		case 0x3df:
		if (cga_emu) {
			aio = 0x1f;
		}
		break;

		// A2386SX only
		case 0xe8:
		case 0xe9:
		case 0xea:
		case 0xeb:
		case 0xec:
		case 0xed:
		case 0xee:
		case 0xef:
		case 0xf0:
		case 0xf1:
		case 0xf4:
		case 0xf5:
		case 0xf8:
		case 0xf9:
		case 0xfa:
		case 0xfb:
		case 0xfc:
		case 0xfe:
		if (xb->type >= TYPE_2386)
			v = vlsi_in(xb, portnum);
		break;

		// floppy
		case 0x3f0:
		case 0x3f1:
		case 0x3f2:
		case 0x3f3:
		case 0x3f4:
		case 0x3f5:
		case 0x3f7:
		v = infloppy(xb, portnum);
		break;

		case 0x60:
		if (xb->type >= TYPE_2286) {
			v = x86_in_keyboard(0x60);
			//write_log(_T("PC read keycode %02x\n"), v);
		}
		break;
		case 0x61:
		if (xb->type >= TYPE_2286) {
			// bios test hack
			v = x86_in_keyboard(0x61);
		} else {
			v = xb->amiga_io[0x5f];
		}
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
			if (!(xb->amiga_io[0x5f] & 1)) {
				bool timer2 = false;
				if (xb->dosbox_cpu) {
					timer2 = TIMER_GetGate2();
				} else {
					timer2 = i8253.active[2] != 0;
				}
				if (timer2)
					v |= 0x20;
			}
			//write_log(_T("IN Port C %02x\n"), v);
		}
		break;
		case 0x63:
		//write_log(_T("IN Control %02x\n"), v);
		break;
		case 0x64:
		if (xb->type >= TYPE_2286) {
			v = x86_in_keyboard(0x64);
		}
		break;

		// at ide 1
		case 0x170:
		case 0x171:
		case 0x172:
		case 0x173:
		case 0x174:
		case 0x175:
		case 0x176:
		case 0x177:
		case 0x376:
		v = x86_ide_hd_get(portnum, 0);
		break;
		// at ide 0
		case 0x1f0:
		case 0x1f1:
		case 0x1f2:
		case 0x1f3:
		case 0x1f4:
		case 0x1f5:
		case 0x1f6:
		case 0x1f7:
		case 0x3f6:
		v = x86_ide_hd_get(portnum, 0);
		break;

		// xt hd
		case 0x320:
		case 0x321:
		case 0x322:
		case 0x323:
		v = x86_xt_hd_bget(portnum);
		break;

		// universal xt bios
		case 0x300:
		case 0x301:
		case 0x302:
		case 0x303:
		case 0x304:
		case 0x305:
		case 0x306:
		case 0x307:
		case 0x308:
		case 0x309:
		case 0x30a:
		case 0x30b:
		case 0x30c:
		case 0x30d:
		case 0x30e:
		case 0x30f:
		v = x86_ide_hd_get(portnum, 0);
		break;

		default:
		write_log(_T("X86_IN unknown %02x\n"), portnum);
		return 0;
	}
	
	if (aio >= 0)
		v = xb->amiga_io[aio];

#if X86_IO_PORT_DEBUG
	write_log(_T("X86_IN %08x %02X\n"), portnum, v);
#endif

	return v;
}
uint16_t portin16(uint16_t portnum)
{
	uae_u16 v = 0;
	switch (portnum)
	{
		case 0x170:
		case 0x1f0:
		case 0x300:
		v = x86_ide_hd_get(portnum, 1);
		break;
		default:
		write_log(_T("portin16 %08x\n"), portnum);
		break;
	}
	return v;
}
static uint32_t portin32(uint16_t portnum)
{
	uint32_t v = 0;
	switch (portnum)
	{
		case 0x170:
		case 0x1f0:
		case 0x300:
		v = x86_ide_hd_get(portnum, 1) << 0;
		v |= x86_ide_hd_get(portnum, 1) << 16;
		break;
		default:
		write_log(_T("portin32 %08x\n"), portnum);
		break;
	}
	return v;
}

void write86(uint32_t addr32, uint8_t value)
{
	struct x86_bridge *xb = bridges[0];
	addr32 &= 0xFFFFF;
	if (addr32 >= xb->pc_maxbaseram && addr32 < 0xa0000)
		return;
	if (addr32 >= x86_xrom_start[0] && addr32 < x86_xrom_end[0])
		return;
	if (addr32 >= x86_xrom_start[1] && addr32 < x86_xrom_end[1])
		return;
	set_pc_address_access(xb, addr32);
	xb->pc_ram[addr32] = value;
}
void writew86(uint32_t addr32, uint16_t value)
{
	struct x86_bridge *xb = bridges[0];
	addr32 &= 0xFFFFF;
	if (addr32 >= xb->pc_maxbaseram && addr32 < 0xa0000)
		return;
	if (addr32 >= x86_xrom_start[0] && addr32 < x86_xrom_end[0])
		return;
	if (addr32 >= x86_xrom_start[1] && addr32 < x86_xrom_end[1])
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
	if (!xb->baseaddress || addr >= 0x80000) {
		*mode = -1;
		return 0;
	}
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
		if (xb->type >= TYPE_2286)
			return 0xd0000 + addr;
		else
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
	struct x86_bridge *xb = get_x86_bridge(addr);
	if (!xb)
		return;
	int mode;
	uaecptr a = get_x86_address(xb, addr, &mode);

#if X86_DEBUG_BRIDGE > 1
	write_log(_T("pci_bridge_wput %08x (%08x,%d) %04x PC=%08x\n"), addr - xb->baseaddress, a, mode, b & 0xffff, M68K_GETPC);
#endif

	if (a >= 0x100000 || mode < 0)
		return;
	if (xb->rombank[a / 4096])
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

	if (a >= 0x100000 || mode < 0)
		return;
	if (xb->rombank[a / 4096])
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
	x86_bridge_lget, x86_bridge_wget,
	ABFLAG_IO | ABFLAG_SAFE, S_READ, S_WRITE
};

void x86_bridge_rethink(void)
{
	struct x86_bridge *xb = bridges[0];
	if (!xb)
		return;
	if (!(xb->amiga_io[IO_CONTROL_REGISTER] & 1)) {
		xb->amiga_io[IO_AMIGA_INTERRUPT_STATUS] |= xb->delayed_interrupt;
		xb->delayed_interrupt = 0;
		uae_u8 intreq = xb->amiga_io[IO_AMIGA_INTERRUPT_STATUS];
		uae_u8 intena = xb->amiga_io[IO_INTERRUPT_MASK];
		uae_u8 status = intreq & ~intena;
		if (status)
			INTREQ_0(0x8000 | 0x0008);
	}
}

void x86_bridge_free(void)
{
	x86_bridge_reset();
	x86_found = 0;
}

void x86_bridge_reset(void)
{
	x86_xrom_start[0] = x86_xrom_end[0] = 0;
	x86_xrom_start[1] = x86_xrom_end[1] = 0;
	for (int i = 0; i < X86_BRIDGE_MAX; i++) {
		struct x86_bridge *xb = bridges[i];
		if (!xb)
			continue;
		if (xb->dosbox_cpu) {
			if (xb->cmosfile) {
				uae_u8 *regs = x86_cmos_regs(NULL);
				zfile_fseek(xb->cmosfile, 0, SEEK_SET);
				zfile_fwrite(regs, 1, xb->cmossize, xb->cmosfile);
			}
			CPU_ShutDown(dosbox_sec);
			CMOS_Destroy(dosbox_sec);
			TIMER_Destroy(dosbox_sec);
			PIC_Destroy(dosbox_sec);
			MEM_ShutDown(dosbox_sec);
			delete dosbox_sec;
		}
		xfree(xb->amiga_io);
		xfree(xb->io_ports);
		xfree(xb->pc_ram);
		xfree(xb->pc_rom);
		zfile_fclose(xb->cmosfile);
		bridges[i] = NULL;
	}
}

static void check_floppy_delay(void)
{
	for (int i = 0; i < 4; i++) {
		if (floppy_seeking[i]) {
			bool neg = floppy_seeking[i] < 0;
			if (floppy_seeking[i] > 0)
				floppy_seeking[i]--;
			else if (neg)
				floppy_seeking[i]++;
			if (floppy_seeking[i] == 0)
				do_floppy_seek(i, neg);
		}
	}
	if (floppy_delay_hsync > 1 || floppy_delay_hsync < -1) {
		if (floppy_delay_hsync > 0)
			floppy_delay_hsync--;
		else
			floppy_delay_hsync++;
		if (floppy_delay_hsync == 1 || floppy_delay_hsync == -1)
			do_floppy_irq();
	}
}

static void x86_cpu_execute(int cnt)
{
	struct x86_bridge *xb = bridges[0];
	if (!xb->x86_reset) {
		if (xb->dosbox_cpu) {
			if (xb->x86_reset_requested) {
				xb->x86_reset_requested = false;
				reset_x86_cpu(xb);
			}
			Bit32s old_cpu_cycles = CPU_Cycles;
			if (CPU_Cycles > cnt)
				CPU_Cycles = cnt;
			if (PIC_RunQueue()) {
				(*cpudecoder)();
			}
			CPU_Cycles = old_cpu_cycles -= cnt;
			if (CPU_Cycles < 0)
				CPU_Cycles = 0;
			check_x86_irq();
		} else {
			exec86(cnt);
		}
	}

	// BIOS has CPU loop delays in floppy driver...
	check_floppy_delay();
}

void x86_bridge_execute_until(int until)
{
	struct x86_bridge *xb = bridges[0];
	if (!xb)
		return;
	if (!x86_turbo_allowed)
		return;
	for (;;) {
		x86_cpu_execute(until ? 10 : 1);
		if (until == 0)
			break;
		frame_time_t rpt = read_processor_time();
		if ((int)until - (int)rpt <= 0)
			break;
	}
}

void x86_bridge_sync_change(void)
{
	struct x86_bridge *xb = bridges[0];
	if (!xb)
		return;

	xb->dosbox_vpos_tick = maxvpos * vblank_hz / 1000;
	if (xb->dosbox_vpos_tick >= xb->dosbox_vpos_tick)
		xb->dosbox_tick_vpos_cnt -= xb->dosbox_vpos_tick;
}

void x86_bridge_vsync(void)
{
	struct x86_bridge *xb = bridges[0];
	if (!xb)
		return;

	if (xb->delayed_interrupt) {
		x86_bridge_rethink();
	}
}

void x86_bridge_hsync(void)
{
	struct x86_bridge *xb = bridges[0];
	if (!xb)
		return;

	check_floppy_delay();

	if (!xb->x86_reset) {
		if (xb->dosbox_cpu) {
			xb->dosbox_tick_vpos_cnt++;
			if (xb->dosbox_tick_vpos_cnt >= xb->dosbox_vpos_tick) {
				TIMER_AddTick();
				xb->dosbox_tick_vpos_cnt -= xb->dosbox_vpos_tick;
			}
			x86_cpu_execute(x86_instruction_count);
		} else {
			for (int i = 0; i < 3; i++) {
				x86_cpu_execute(x86_instruction_count / 3);
				timing(maxhpos / 3);
			}
		}
	}

	if (currprefs.x86_speed_throttle != changed_prefs.x86_speed_throttle) {
		currprefs.x86_speed_throttle = changed_prefs.x86_speed_throttle;
		x86_instruction_count = DEFAULT_X86_INSTRUCTION_COUNT;
		if (currprefs.x86_speed_throttle < 0) {
			x86_turbo_enabled = true;
		} else {
			x86_turbo_enabled = false;
			x86_instruction_count = DEFAULT_X86_INSTRUCTION_COUNT + DEFAULT_X86_INSTRUCTION_COUNT * currprefs.x86_speed_throttle / 1000;
		}
	}

	if (x86_turbo_allowed && x86_turbo_enabled && !x86_turbo_on) {
		x86_turbo_on = true;
	} else if ((!x86_turbo_allowed || !x86_turbo_enabled) && x86_turbo_on) {
		x86_turbo_on = false;
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

static void setrombank(struct x86_bridge *xb, int start, int len)
{
	while (len > 0) {
		xb->rombank[start / 4096] = 1;
		start += 4096;
		len -= 4096;
	}
}

static void bridge_reset(struct x86_bridge *xb)
{
	xb->x86_reset = true;
	xb->configured = 0;
	xb->amiga_forced_interrupts = false;
	xb->amiga_irq = false;
	xb->pc_irq3a = xb->pc_irq3b = xb->pc_irq7 = false;
	x86_turbo_allowed = false;
	x86_cpu_active = false;
	x86_instruction_count = DEFAULT_X86_INSTRUCTION_COUNT;
	memset(xb->amiga_io, 0, 0x10000);
	memset(xb->io_ports, 0, 0x10000);
	memset(xb->pc_ram, 0, 0x100000 - xb->bios_size);
	memset(xb->rombank, 0, sizeof xb->rombank);

	for (int i = 0; i < 2; i++) {
		if (x86_xrom_end[i] - x86_xrom_start[i] > 0) {
			memcpy(xb->pc_ram + x86_xrom_start[i], xb->pc_rom + x86_xrom_start[i], x86_xrom_end[i] - x86_xrom_start[i]);
			setrombank(xb, x86_xrom_start[i], x86_xrom_end[i] - x86_xrom_start[i]);
		}
	}
	memcpy(xb->pc_ram + 0x100000 - xb->bios_size, xb->pc_rom + 0x100000 - xb->bios_size, xb->bios_size);
	setrombank(xb, 0x100000 - xb->bios_size, xb->bios_size);

	xb->amiga_io[IO_CONTROL_REGISTER] =	0xfe;
	xb->amiga_io[IO_PC_INTERRUPT_CONTROL] = 0xff;
	xb->amiga_io[IO_INTERRUPT_MASK] = 0xff;
	xb->amiga_io[IO_MODE_REGISTER] = 0x00;
	xb->amiga_io[IO_PC_INTERRUPT_STATUS] = 0xfe;
	x86_cmos_bank = 0;
	memset(xb->vlsi_regs, 0, sizeof xb->vlsi_regs);

	if (xb->type >= TYPE_2286) {
		int sel1 = (xb->settings >> 10) & 1;
		int sel2 = (xb->settings >> 11) & 1;
		// only A0000 or D0000 is valid for AT
		if ((!sel1 && !sel2) || (sel1 && sel2)) {
			sel1 = 0;
			sel2 = 1;
		}
		xb->amiga_io[IO_MODE_REGISTER] |= sel1 << 5;
		xb->amiga_io[IO_MODE_REGISTER] |= sel2 << 6;
	}
	if (xb->type == TYPE_2386)
		x86_turbo_allowed = true;

	x86_bridge_sync_change();
	inittiming();
	floppy_hardreset();
}

int is_x86_cpu(struct uae_prefs *p)
{
	struct x86_bridge *xb = bridges[0];
	if (!xb) {
		if (x86_found > 0)
			return X86_STATE_STOP;
		else if (x86_found < 0)
			return X86_STATE_INACTIVE;
		if (is_device_rom(p, ROMTYPE_A1060, 0) < 0 &&
			is_device_rom(p, ROMTYPE_A2088, 0) < 0 &&
			is_device_rom(p, ROMTYPE_A2088T, 0) < 0 &&
			is_device_rom(p, ROMTYPE_A2286, 0) < 0 &&
			is_device_rom(p, ROMTYPE_A2386, 0) < 0) {
			if (p == &currprefs)
				x86_found = -1;
			return X86_STATE_INACTIVE;
		} else {
			if (p == &currprefs)
				x86_found = 1;
		}
	}
	if (!xb || xb->x86_reset)
		return X86_STATE_STOP;
	return X86_STATE_ACTIVE;
}

static void load_vga_bios(void)
{
	struct x86_bridge *xb = bridges[0];
	if (!xb || !ISVGA())
		return;
	struct zfile *zf = read_device_rom(&currprefs, ROMTYPE_x86_VGA, 0, NULL);
	x86_xrom_start[1] = 0xc0000;
	x86_xrom_end[1] = x86_xrom_start[1];
	if (zf) {
		x86_xrom_end[1] += zfile_fread(xb->pc_rom + x86_xrom_start[1], 1, 65536, zf);
		zfile_fclose(zf);
		x86_xrom_end[1] += 4095;
		x86_xrom_end[1] &= ~4095;
		memcpy(xb->pc_ram + x86_xrom_start[1], xb->pc_rom + x86_xrom_start[1], x86_xrom_end[1] - x86_xrom_start[1]);
		setrombank(xb, x86_xrom_start[1], x86_xrom_end[1] - x86_xrom_start[1]);
	}
	if (xb->dosbox_cpu) {
		MEM_ShutDown(dosbox_sec);
		MEM_Init(dosbox_sec);
		MEM_SetVGAHandler();
	}
}

void x86_xt_ide_bios(struct zfile *z, struct romconfig *rc)
{
	struct x86_bridge *xb = bridges[0];
	uae_u32 addr = 0;
	if (!xb || !z)
		return;
	switch (rc->device_settings)
	{
		case 0:
		addr = 0xcc000;
		break;
		case 1:
		addr = 0xdc000;
		break;
		case 2:
		default:
		addr = 0xec000;
		break;
	}
	x86_xrom_start[0] = addr;
	x86_xrom_end[0] = x86_xrom_start[0] + 0x4000;
	zfile_fread(xb->pc_rom + x86_xrom_start[0], 1, 32768, z);
	memcpy(xb->pc_ram + x86_xrom_start[0], xb->pc_rom + x86_xrom_start[0], x86_xrom_end[0] - x86_xrom_start[0]);
	setrombank(xb, x86_xrom_start[0], x86_xrom_end[0] - x86_xrom_start[0]);
	if (xb->dosbox_cpu) {
		MEM_ShutDown(dosbox_sec);
		MEM_Init(dosbox_sec);
		if (ISVGA()) {
			MEM_SetVGAHandler();
		}
	}
}

static const uae_u8 a1060_autoconfig[16] = { 0xc4, 0x01, 0x80, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const uae_u8 a2386_autoconfig[16] = { 0xc4, 0x67, 0x80, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

addrbank *x86_bridge_init(struct romconfig *rc, uae_u32 romtype, int type)
{
	struct x86_bridge *xb = x86_bridge_alloc();
	const uae_u8 *ac;
	if (!xb)
		return &expamem_null;
	bridges[0] = xb;
	xb->rc = rc;

	xb->type = type;

	xb->io_ports = xcalloc(uae_u8, 0x10000);
	xb->amiga_io = xcalloc(uae_u8, 0x10000);

	x86_xrom_start[0] = x86_xrom_end[0] = 0;
	x86_xrom_start[1] = x86_xrom_end[1] = 0;
	xb->settings = rc->device_settings;
	if (xb->type >= TYPE_2286) {
		xb->dosbox_cpu = ((xb->settings >> 19) & 3) + 1;
		xb->dosbox_cpu_arch = (xb->settings >> 23) & 7;
		xb->settings |= 0xff;
		ac = xb->type >= TYPE_2386 ? a2386_autoconfig : a1060_autoconfig;
		xb->pc_maxram = (1024 * 1024) << ((xb->settings >> 16) & 7);
		xb->bios_size = 65536;
	} else {
		xb->dosbox_cpu = (xb->settings >> 19) & 7;
		xb->dosbox_cpu_arch = (xb->settings >> 23) & 7;
		ac = a1060_autoconfig;
		xb->pc_maxram = 1 * 1024 * 1024;
		xb->bios_size = 32768;
	}

	xb->pc_ram = xcalloc(uae_u8, xb->pc_maxram + 1 * 1024 * 1024);
	xb->pc_rom = xcalloc(uae_u8, 0x100000);
	x86_memsize = xb->pc_maxram;
	mono_start =  xb->pc_ram + 0xb0000;
	mono_end =    xb->pc_ram + 0xb2000;
	color_start = xb->pc_ram + 0xb8000;
	color_end =   xb->pc_ram + 0xc0000;
	x86_biosstart = 0x100000 - xb->bios_size;
	MemBase = xb->pc_ram;

	if (xb->dosbox_cpu) {
		ticksDone = 0;
		ticksScheduled = 0;
		x86_fpu_enabled = (xb->settings >> 22) & 1;
		dosbox_sec = new Section_prop("dummy");
		MEM_Init(dosbox_sec);
		PAGING_Init(dosbox_sec);
		CMOS_Init(dosbox_sec, xb->type == TYPE_2386 ? 0x7f : 0x3f);
		PIC_Init(dosbox_sec);
		TIMER_Init(dosbox_sec);
		FPU_Init(dosbox_sec);
		if (xb->type >= TYPE_2286) {
			xb->cmossize = xb->type == TYPE_2386 ? 192 : 64;
			xb->cmosfile = zfile_fopen(currprefs.flashfile, _T("rb+"), ZFD_NORMAL);
			if (!xb->cmosfile) {
				xb->cmosfile = zfile_fopen(currprefs.flashfile, _T("wb"));
			}
			memset(xb->cmosregs, 0, sizeof xb->cmosregs);
			if (xb->cmosfile) {
				if (zfile_fread(xb->cmosregs, 1, xb->cmossize, xb->cmosfile) == xb->cmossize) {
					x86_cmos_regs(xb->cmosregs);
				}
			}
		}
	}
	if (ISVGA()) {
		if (xb->dosbox_cpu) {
			MEM_SetVGAHandler();
		}
		load_vga_bios();
	}

	xb->pc_jumpers = (xb->settings & 0xff) ^ ((0x80 | 0x40) | (0x20 | 0x10 | 0x01 | 0x02));

	int ramsize = (xb->settings >> 2) & 3;
	switch(ramsize) {
		case 0:
		xb->pc_maxbaseram = 128 * 1024;
		break;
		case 1:
		xb->pc_maxbaseram = 256 * 1024;
		break;
		case 2:
		xb->pc_maxbaseram = 512 * 1024;
		break;
		case 3:
		xb->pc_maxbaseram = 640 * 1024;
		break;
	}

	bridge_reset(xb);

	// load bios
	if (!load_rom_rc(rc, romtype, xb->bios_size, 0, xb->pc_rom + 0x100000 - xb->bios_size, xb->bios_size, LOADROM_FILL)) {
		error_log(_T("Bridgeboard BIOS failed to load"));
		x86_bridge_free();
		return &expamem_null;
	}
	memcpy(xb->pc_ram + 0x100000 - xb->bios_size, xb->pc_rom + 0x100000 - xb->bios_size, xb->bios_size);
	setrombank(xb, 0x100000 - xb->bios_size, xb->bios_size);

	xb->bank = &x86_bridge_bank;
	for (int i = 0; i < 16; i++) {
		ew(xb->acmemory, i * 4, ac[i]);
	}

	return xb->bank;
}

addrbank *a1060_init(struct romconfig *rc)
{
	return x86_bridge_init(rc, ROMTYPE_A1060, TYPE_SIDECAR);
}
addrbank *a2088xt_init(struct romconfig *rc)
{
	return x86_bridge_init(rc, ROMTYPE_A2088, TYPE_2088);
}
addrbank *a2088t_init(struct romconfig *rc)
{
	return x86_bridge_init(rc, ROMTYPE_A2088T, TYPE_2088T);
}
addrbank *a2286_init(struct romconfig *rc)
{
	return x86_bridge_init(rc, ROMTYPE_A2286, TYPE_2286);
}
addrbank *a2386_init(struct romconfig *rc)
{
	return x86_bridge_init(rc, ROMTYPE_A2386, TYPE_2386);
}

/* dosbox cpu core support stuff */

void IO_WriteB(Bitu port, Bitu val)
{
	portout(port, val);
}
void IO_WriteW(Bitu port, Bitu val)
{
	portout16(port, val);
}
void IO_WriteD(Bitu port, Bitu val)
{
	portout32(port, val);
}
Bitu IO_ReadB(Bitu port)
{
	return portin(port);
}
Bitu IO_ReadW(Bitu port)
{
	return portin16(port);
}
Bitu IO_ReadD(Bitu port)
{
	return portin32(port);
}

Bits CPU_Core_Prefetch_Run(void)
{
	return 0;
}

bool CommandLine::FindCommand(unsigned int which, std::string & value)
{
	return false;
}
unsigned int CommandLine::GetCount(void)
{
	return 0;
}
CommandLine::CommandLine(int argc, char const * const argv[])
{
}
CommandLine::CommandLine(char const * const name, char const * const cmdline)
{
}
void Section::AddDestroyFunction(SectionFunction func, bool canchange)
{
}
int Section_prop::Get_int(string const&_propname) const
{
	return 0;
}
const char* Section_prop::Get_string(string const& _propname) const
{
	return NULL;
}
Prop_multival* Section_prop::Get_multival(string const& _propname) const
{
	return NULL;
}
void Section_prop::HandleInputline(string const& gegevens)
{
}
void Section_prop::PrintData(FILE* outfile) const
{
}
Section_prop::~Section_prop()
{
}
string Section_prop::GetPropValue(string const& _property) const
{
	return NO_SUCH_PROPERTY;
}

void DOSBOX_RunMachine(void)
{
}

void GFX_SetTitle(Bit32s cycles, Bits frameskip, bool paused)
{
}

void E_Exit(char *format, ...)
{
	va_list parms;
	va_start(parms, format);
	char buffer[1000];
	vsnprintf(buffer, sizeof(buffer), format, parms);
	write_log("DOSBOX E_Exit: %s\n", buffer);
	va_end(parms);
}
void GFX_ShowMsg(const char *format, ...)
{
	va_list parms;
	va_start(parms, format);
	char buffer[1000];
	vsnprintf(buffer, sizeof(buffer), format, parms);
	write_log("DOSBOX GFX_ShowMsg: %s\n", buffer);
	va_end(parms);
}

