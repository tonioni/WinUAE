/*
* UAE - The Un*x Amiga Emulator
*
* Other IDE controllers
*
* (c) 2015 Toni Wilen
*/

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"

#include "memory.h"
#include "newcpu.h"
#include "uae.h"
#include "gui.h"
#include "filesys.h"
#include "threaddep/thread.h"
#include "debug.h"
#include "ide.h"
#include "idecontrollers.h"
#include "zfile.h"
#include "custom.h"
#include "rommgr.h"
#include "cpuboard.h"
#include "scsi.h"
#include "ncr9x_scsi.h"
#include "autoconf.h"

#define DEBUG_IDE 0
#define DEBUG_IDE_GVP 0
#define DEBUG_IDE_ALF 0
#define DEBUG_IDE_APOLLO 0
#define DEBUG_IDE_MASOBOSHI 0

#define GVP_IDE 0 // GVP A3001
#define ALF_IDE 1
#define APOLLO_IDE (ALF_IDE + MAX_DUPLICATE_EXPANSION_BOARDS)
#define MASOBOSHI_IDE (APOLLO_IDE + MAX_DUPLICATE_EXPANSION_BOARDS)
#define ADIDE_IDE (MASOBOSHI_IDE + MAX_DUPLICATE_EXPANSION_BOARDS)
#define MTEC_IDE (ADIDE_IDE + MAX_DUPLICATE_EXPANSION_BOARDS)
#define PROTAR_IDE (MTEC_IDE + MAX_DUPLICATE_EXPANSION_BOARDS)
#define ROCHARD_IDE (PROTAR_IDE + MAX_DUPLICATE_EXPANSION_BOARDS)
#define x86_AT_IDE (ROCHARD_IDE + 2 * MAX_DUPLICATE_EXPANSION_BOARDS)
#define GOLEMFAST_IDE (x86_AT_IDE + 2 * MAX_DUPLICATE_EXPANSION_BOARDS)
#define BUDDHA_IDE (GOLEMFAST_IDE + 2 * MAX_DUPLICATE_EXPANSION_BOARDS)
#define DATAFLYERPLUS_IDE (BUDDHA_IDE + 3 * MAX_DUPLICATE_EXPANSION_BOARDS)
#define ATEAM_IDE (DATAFLYERPLUS_IDE + MAX_DUPLICATE_EXPANSION_BOARDS)
#define TOTAL_IDE (ATEAM_IDE + MAX_DUPLICATE_EXPANSION_BOARDS)

#define ALF_ROM_OFFSET 0x0100
#define GVP_IDE_ROM_OFFSET 0x8000
#define APOLLO_ROM_OFFSET 0x8000
#define ADIDE_ROM_OFFSET 0x8000
#define MASOBOSHI_ROM_OFFSET 0x0080
#define MASOBOSHI_ROM_OFFSET_END 0xf000
#define MASOBOSHI_SCSI_OFFSET 0xf800
#define MASOBOSHI_SCSI_OFFSET_END 0xfc00

/* masoboshi:

IDE 

- FFCC = base address, data (0)
- FF81 = -004B
- FF41 = -008B
- FF01 = -01CB
- FEC1 = -010B
- FE81 = -014B
- FE41 = -018B select (6)
- FE01 = -01CB status (7)
- FE03 = command (7)

- FA00 = ESP, 2 byte register spacing
- F9CC = data

- F047 = -0F85 (-0FB9) interrupt request? (bit 3)
- F040 = -0F8C interrupt request? (bit 1) Write anything = clear irq?
- F000 = some status register

- F04C = DMA address (long)
- F04A = number of words
- F044 = ???
- F047 = bit 7 = start dma

*/

#define MAX_IDE_UNITS 10

static struct ide_board *gvp_ide_rom_board, *gvp_ide_controller_board;
static struct ide_board *alf_board[MAX_DUPLICATE_EXPANSION_BOARDS];
static struct ide_board *apollo_board[MAX_DUPLICATE_EXPANSION_BOARDS];
static struct ide_board *masoboshi_board[MAX_DUPLICATE_EXPANSION_BOARDS];
static struct ide_board *adide_board[MAX_DUPLICATE_EXPANSION_BOARDS];
static struct ide_board *mtec_board[MAX_DUPLICATE_EXPANSION_BOARDS];
static struct ide_board *protar_board[MAX_DUPLICATE_EXPANSION_BOARDS];
static struct ide_board *rochard_board[MAX_DUPLICATE_EXPANSION_BOARDS];
static struct ide_board *x86_at_ide_board[MAX_DUPLICATE_EXPANSION_BOARDS];
static struct ide_board *golemfast_board[MAX_DUPLICATE_EXPANSION_BOARDS];
static struct ide_board *buddha_board[MAX_DUPLICATE_EXPANSION_BOARDS];
static struct ide_board *dataflyerplus_board[MAX_DUPLICATE_EXPANSION_BOARDS];
static struct ide_board *ateam_board[MAX_DUPLICATE_EXPANSION_BOARDS];

static struct ide_hdf *idecontroller_drive[TOTAL_IDE * 2];
static struct ide_thread_state idecontroller_its;

static struct ide_board *ide_boards[MAX_IDE_UNITS + 1];

static void freencrunit(struct ide_board *ide)
{
	if (!ide)
		return;
	for (int i = 0; i < MAX_IDE_UNITS; i++) {
		if (ide_boards[i] == ide) {
			ide_boards[i] = NULL;
		}
	}
	for(int i = 0; i < MAX_IDE_PORTS_BOARD; i++) {
		remove_ide_unit(&ide->ide[i], 0);
	}
	if (ide->self_ptr)
		*ide->self_ptr = NULL;
	xfree(ide->rom);
	xfree(ide);
}

static struct ide_board *allocide(struct ide_board **idep, struct romconfig *rc, int ch)
{
	struct ide_board *ide;

	if (ch < 0) {
		if (*idep) {
			freencrunit(*idep);
			*idep = NULL;
		}
		ide = xcalloc(struct ide_board, 1);
		for (int i = 0; i < MAX_IDE_UNITS; i++) {
			if (ide_boards[i] == NULL) {
				ide_boards[i] = ide;
				rc->unitdata = ide;
				ide->rc = rc;
				ide->self_ptr = idep;
				if (idep)
					*idep = ide;
				return ide;
			}
		}
	}
	return *idep;
}

static struct ide_board *getide(struct romconfig *rc)
{
	for (int i = 0; i < MAX_IDE_UNITS; i++) {
		if (ide_boards[i]) {
			struct ide_board *ide = ide_boards[i];
			if (ide->rc == rc) {
				ide->original_rc = rc;
				ide->rc = NULL;
				return ide;
			}
		}
	}
	return NULL;
}

static struct ide_board *getideboard(uaecptr addr)
{
	for (int i = 0; ide_boards[i]; i++) {
		if (!ide_boards[i]->baseaddress && !ide_boards[i]->configured)
			return ide_boards[i];
		if ((addr & ~ide_boards[i]->mask) == ide_boards[i]->baseaddress)
			return ide_boards[i];
	}
	return NULL;
}

static void init_ide(struct ide_board *board, int ide_num, int maxunit, bool byteswap, bool adide)
{
	for (int i = 0; i < maxunit / 2; i++) {
		struct ide_hdf **idetable = &idecontroller_drive[(ide_num + i) * 2];
		alloc_ide_mem (idetable, 2, &idecontroller_its);
		board->ide[i] = idetable[0];
		idetable[0]->board = board;
		idetable[1]->board = board;
		idetable[0]->byteswap = byteswap;
		idetable[1]->byteswap = byteswap;
		idetable[0]->adide = adide;
		idetable[1]->adide = adide;
		ide_initialize(idecontroller_drive, ide_num + i);
	}
	idecontroller_its.idetable = idecontroller_drive;
	idecontroller_its.idetotal = TOTAL_IDE * 2;
	start_ide_thread(&idecontroller_its);
}

static void add_ide_standard_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc, struct ide_board **ideboard, int idetype, bool byteswap, bool adide, int maxunit)
{
	struct ide_hdf *ide;
	struct ide_board *ideb;
	if (ch >= maxunit)
		return;
	ideb = allocide(&ideboard[ci->controller_type_unit], rc, ch);
	if (!ideb)
		return;
	ideb->keepautoconfig = true;
	ideb->type = idetype;
	ide = add_ide_unit (&idecontroller_drive[(idetype + ci->controller_type_unit) * 2], 2, ch, ci, rc);
	init_ide(ideb, idetype + ci->controller_type_unit, maxunit, byteswap, adide);
}

static bool ide_interrupt_check(struct ide_board *board, bool edge_triggered)
{
	if (!board->configured)
		return false;
	bool irq = false;
	for (int i = 0; i < MAX_IDE_PORTS_BOARD; i++) {
		if (board->ide[i] && !irq) {
			irq = ide_irq_check(board->ide[i], edge_triggered);
		}
	}
#if 0
	if (board->irq != irq)
		write_log(_T("IDE irq %d -> %d\n"), board->irq, irq);
#endif
	board->irq = irq;
	return irq;
}

static bool ide_rethink(struct ide_board *board, bool edge_triggered)
{
	bool irq = false;
	if (board->configured) {
		if (board->intena && ide_interrupt_check(board, edge_triggered)) {
			irq = true;
		}
	}
	return irq;
}

void x86_doirq(uint8_t irqnum);

void idecontroller_rethink(void)
{
	bool irq = false;
	for (int i = 0; ide_boards[i]; i++) {
		if (ide_boards[i] == x86_at_ide_board[0] || ide_boards[i] == x86_at_ide_board[1]) {
			bool x86irq = ide_rethink(ide_boards[i], true);
			if (x86irq) {
				//write_log(_T("x86 IDE IRQ\n"));
				x86_doirq(ide_boards[i] == x86_at_ide_board[0] ? 14 : 15);
			}
		} else {
			irq |= ide_rethink(ide_boards[i], false);
		}
	}
	if (irq && !(intreq & 0x0008)) {
		INTREQ_0(0x8000 | 0x0008);
	}
}

void idecontroller_hsync(void)
{
	for (int i = 0; ide_boards[i]; i++) {
		struct ide_board *board = ide_boards[i];
		if (board->hsync_cnt > 0) {
			board->hsync_cnt--;
			if (!board->hsync_cnt && board->hsync_code)
				board->hsync_code(board);
		}
		if (board->configured) {
			for (int j = 0; j < MAX_IDE_PORTS_BOARD; j++) {
				if (board->ide[j]) {
					ide_interrupt_hsync(board->ide[j]);
				}
			}
			if (ide_interrupt_check(board, false)) {
				idecontroller_rethink();
			}
		}
	}
}

static void reset_ide(struct ide_board *board)
{
	board->configured = 0;
	board->intena = false;
	board->enabled = false;
}

void idecontroller_reset(void)
{
	for (int i = 0; ide_boards[i]; i++) {
		reset_ide(ide_boards[i]);
	}
}

void idecontroller_free(void)
{
	stop_ide_thread(&idecontroller_its);
	for (int i = 0; i < MAX_IDE_UNITS; i++) {
		freencrunit(ide_boards[i]);
	}
}

static bool is_gvp2_intreq(uaecptr addr)
{
	if (ISCPUBOARD(BOARD_GVP, BOARD_GVP_SUB_A3001SII) && (addr & 0x440) == 0x440)
		return true;
	return false;
}
static bool is_gvp1_intreq(uaecptr addr)
{
	if (ISCPUBOARD(BOARD_GVP, BOARD_GVP_SUB_A3001SI) && (addr & 0x440) == 0x40)
		return true;
	return false;
}

static bool get_ide_is_8bit(struct ide_board *board)
{
	struct ide_hdf *ide = board->ide[0];
	if (!ide)
		return false;
	if (ide->ide_drv)
		ide = ide->pair;
	return ide->mode_8bit;
}

static uae_u32 get_ide_reg_multi(struct ide_board *board, int reg, int portnum, int dataportsize)
{
	struct ide_hdf *ide = board->ide[portnum];
	if (!ide)
		return 0;
	if (ide->ide_drv)
		ide = ide->pair;
	if (reg == 0) {
		if (dataportsize)
			return ide_get_data(ide);
		else
			return ide_get_data_8bit(ide);
	} else {
		return ide_read_reg(ide, reg);
	}
}
static void put_ide_reg_multi(struct ide_board *board, int reg, uae_u32 v, int portnum, int dataportsize)
{
	struct ide_hdf *ide = board->ide[portnum];
	if (!ide)
		return;
	if (ide->ide_drv)
		ide = ide->pair;
	if (reg == 0) {
		if (dataportsize)
			ide_put_data(ide, v);
		else
			ide_put_data_8bit(ide, v);
	} else {
		ide_write_reg(ide, reg, v);
	}
}
static uae_u32 get_ide_reg(struct ide_board *board, int reg)
{
	return get_ide_reg_multi(board, reg, 0, reg == 0 ? 1 : 0);
}
static void put_ide_reg(struct ide_board *board, int reg, uae_u32 v)
{
	put_ide_reg_multi(board, reg, v, 0, reg == 0 ? 1 : 0);
}
static uae_u32 get_ide_reg_8bitdata(struct ide_board *board, int reg)
{
	return get_ide_reg_multi(board, reg, 0, 0);
}
static void put_ide_reg_8bitdata(struct ide_board *board, int reg, uae_u32 v)
{
	put_ide_reg_multi(board, reg, v, 0, 0);
}


static int get_gvp_reg(uaecptr addr, struct ide_board *board)
{
	int reg = -1;

	if (addr & 0x1000) {
		reg = IDE_SECONDARY + ((addr >> 8) & 7);
	} else if (addr & 0x0800) {
		reg = (addr >> 8) & 7;
	}
	if (!(addr & 0x400) && (addr & 0x20)) {
		if (reg < 0)
			reg = 0;
		int extra = (addr >> 1) & 15;
		if (extra >= 8)
			reg |= IDE_SECONDARY;
		reg |= extra;
	}
	if (reg >= 0)
		reg &= IDE_SECONDARY | 7;

	return reg;
}

static int get_apollo_reg(uaecptr addr, struct ide_board *board)
{
	if (addr & 0x4000)
		return -1;
	int reg = addr & 0x1fff;
	reg >>= 10;
	if (addr & 0x2000)
		reg |= IDE_SECONDARY;
	if (reg != 0 && !(addr & 1))
		reg = -1;
	return reg;
}

static int get_alf_reg(uaecptr addr, struct ide_board *board)
{
	if (addr & 0x8000)
		return -1;
	if (addr & 0x4000) {
		;
	} else if (addr & 0x1000) {
		addr &= 0xfff;
		addr >>= 9;
	} else if (addr & 0x2000) {
		addr &= 0xfff;
		addr >>= 9;
		addr |= IDE_SECONDARY;
	}
	return addr;
}

static int get_masoboshi_reg(uaecptr addr, struct ide_board *board)
{
	int reg;
	if (addr < 0xfc00)
		return -1;
	reg = 7 - ((addr >> 6) & 7);
	if (addr < 0xfe00)
		reg |= IDE_SECONDARY;
	return reg;
}
static void masoboshi_ide_dma(struct ide_board *board)
{
	board->state2[0] |= 0x80;
}

static int get_adide_reg(uaecptr addr, struct ide_board *board)
{
	int reg;
	if (addr & 0x8000)
		return -1;
	reg = (addr >> 1) & 7;
	if (addr & 0x10)
		reg |= IDE_SECONDARY;
	return reg;
}

static int get_buddha_reg(uaecptr addr, struct ide_board *board, int *portnum)
{
	int reg = -1;
	if (addr < 0x800 || addr >= 0xe00)
		return reg;
	*portnum = (addr - 0x800) / 0x200;
	reg = (addr >> 2) & 15;
	if (addr & 0x100)
		reg |= IDE_SECONDARY;
	return reg;
}

static int get_rochard_reg(uaecptr addr, struct ide_board *board, int *portnum)
{
	int reg = -1;
	if ((addr & 0x8001) != 0x8001)
		return -1;
	*portnum = (addr & 0x4000) ? 1 : 0;
	reg = (addr >> 5) & 7;
	if (addr & 0x2000)
		reg |= IDE_SECONDARY;
	return reg;
}

static int get_dataflyerplus_reg(uaecptr addr, struct ide_board *board)
{
	int reg = -1;
	if (!(addr & 0x8000))
		return -1;
	reg = (addr / 0x40) & 7;
	return reg;
}

static int get_golemfast_reg(uaecptr addr, struct ide_board *board)
{
	int reg = -1;
	if ((addr & 0x8001) != 0x8000)
		return -1;
	reg = (addr >> 2) & 7;
	if (addr & 0x2000)
		reg |= IDE_SECONDARY;
	return reg;
}

static int get_ateam_reg(uaecptr addr, struct ide_board *board)
{
	if (!(addr & 1))
		return -1;
	if ((addr & 0xf000) != 0xf000)
		return -1;
	addr >>= 7;
	addr &= 15;
	if (addr >= 8) {
		addr &= 7;
		addr |= IDE_SECONDARY;
	}
	return addr;
}


static int getidenum(struct ide_board *board, struct ide_board **arr)
{
	for (int i = 0; i < MAX_DUPLICATE_EXPANSION_BOARDS; i++) {
		if (board == arr[i])
			return i;
	}
	return 0;
}

static uae_u32 ide_read_byte(struct ide_board *board, uaecptr addr)
{
	uaecptr oaddr = addr;
	uae_u8 v = 0xff;

	addr &= board->mask;

#if DEBUG_IDE
	write_log(_T("IDE IO BYTE READ %08x %08x\n"), addr, M68K_GETPC);
#endif
	
	if (addr < 0x40 && (!board->configured || board->keepautoconfig))
		return board->acmemory[addr];

	if (board->type == BUDDHA_IDE) {

		int portnum;
		int regnum = get_buddha_reg(addr, board, &portnum);
		if (regnum >= 0) {
			if (board->ide[portnum])
				v = get_ide_reg_multi(board, regnum, portnum, 1);
		} else if (addr >= 0xf00 && addr < 0x1000) {
			if ((addr & ~3) == 0xf00)
				v = ide_irq_check(board->ide[0], false) ? 0x80 : 0x00;
			else if ((addr & ~3) == 0xf40)
				v = ide_irq_check(board->ide[1], false) ? 0x80 : 0x00;
			else if ((addr & ~3) == 0xf80)
				v = ide_irq_check(board->ide[2], false) ? 0x80 : 0x00;
			else
				v = 0;
		} else if (addr >= 0x7fc && addr <= 0x7ff) {
			v = board->userdata;
		} else {
			v = board->rom[addr & board->rom_mask];
		}

	} else if (board->type == ALF_IDE) {

		if (addr < 0x1100 || (addr & 1)) {
			if (board->rom)
				v = board->rom[addr & board->rom_mask];
			return v;
		}
		int regnum = get_alf_reg(addr, board);
		if (regnum >= 0) {
			v = get_ide_reg(board, regnum);
		}
#if DEBUG_IDE_ALF
		write_log(_T("ALF GET %08x %02x %d %08x\n"), addr, v, regnum, M68K_GETPC);
#endif

	} else if (board->type == GOLEMFAST_IDE) {

		if (!(addr & 0x8000)) {
			if (board->rom) {
				v = board->rom[addr & board->rom_mask];
			}
		} else if ((addr & 0x8700) == 0x8400 || (addr & 0x8700) == 0x8000) {
			v = golemfast_ncr9x_scsi_get(oaddr, getidenum(board, golemfast_board));
		} else if ((addr & 0x8700) == 0x8100) {
			int regnum = get_golemfast_reg(addr, board);
			if (regnum >= 0) {
				v = get_ide_reg(board, regnum);
			}
		} else if ((addr & 0x8700) == 0x8300) {
			v = board->original_rc->device_id ^ 7;
			if (!board->original_rc->autoboot_disabled)
				v |= 0x20;
			if (!(board->original_rc->device_settings & 1))
				v |= 0x08;
			if (ide_irq_check(board->ide[0], false) || ide_drq_check(board->ide[0]))
				v |= 0x80; // IDE IRQ | DRQ
			//write_log(_T("READ JUMPER %08x %02x %08x\n"), addr, v, M68K_GETPC);
		}
		
	} else if (board->type == MASOBOSHI_IDE) {
		int regnum = -1;
		bool rom = false;
		if (addr >= MASOBOSHI_ROM_OFFSET && addr < MASOBOSHI_ROM_OFFSET_END) {
			if (board->rom) {
				v = board->rom[addr & board->rom_mask];
				rom = true;
			}
		} else if ((addr >= 0xf000 && addr <= 0xf00f) || (addr >= 0xf100 && addr <= 0xf10f)) {
			// scsi dma controller
			if (board->subtype)
				v = masoboshi_ncr9x_scsi_get(oaddr, getidenum(board, masoboshi_board));
		} else if (addr == 0xf040) {
			v = 1;
			if (ide_irq_check(board->ide[0], false)) {
				v |= 2;
				board->irq = true;
			}
			if (board->irq) {
				v &= ~1;
			}
			v |= board->state2[0] &0x80;
		} else if (addr == 0xf047) {
			v = board->state;
		} else {
			regnum = get_masoboshi_reg(addr, board);
			if (regnum >= 0) {
				v = get_ide_reg(board, regnum);
			} else if (addr >= MASOBOSHI_SCSI_OFFSET && addr < MASOBOSHI_SCSI_OFFSET_END) {
				if (board->subtype)
					v = masoboshi_ncr9x_scsi_get(oaddr, getidenum(board, masoboshi_board));
				else
					v = 0xff;
			}
		}
#if DEBUG_IDE_MASOBOSHI
		if (!rom)
			write_log(_T("MASOBOSHI BYTE GET %08x %02x %d %08x\n"), addr, v, regnum, M68K_GETPC);
#endif
	} else if (board->type == APOLLO_IDE) {

		if (addr >= APOLLO_ROM_OFFSET) {
			if (board->rom)
				v = board->rom[(addr - APOLLO_ROM_OFFSET) & board->rom_mask];
		} else if (board->configured) {
			if ((addr & 0xc000) == 0x4000) {
				v = apollo_scsi_bget(oaddr, board->userdata);
			} else if (addr < 0x4000) {
				int regnum = get_apollo_reg(addr, board);
				if (regnum >= 0) {
					v = get_ide_reg(board, regnum);
				} else {
					v = 0;
				}
			}
		}

	} else if (board->type == GVP_IDE) {

		if (addr >= GVP_IDE_ROM_OFFSET) {
			if (board->rom) {
				if (addr & 1)
					v = 0xe8; // board id
				else 
					v = board->rom[((addr - GVP_IDE_ROM_OFFSET) / 2) & board->rom_mask];
				return v;
			}
			v = 0xe8;
#if DEBUG_IDE_GVP
			write_log(_T("GVP BOOT GET %08x %02x %08x\n"), addr, v, M68K_GETPC);
#endif
			return v;
		}
		if (board->configured) {
			if (board == gvp_ide_rom_board && ISCPUBOARD(BOARD_GVP, BOARD_GVP_SUB_A3001SII)) {
				if (addr == 0x42) {
					v = 0xff;
				}
#if DEBUG_IDE_GVP
				write_log(_T("GVP BOOT GET %08x %02x %08x\n"), addr, v, M68K_GETPC);
#endif
			} else {
				int regnum = get_gvp_reg(addr, board);
#if DEBUG_IDE_GVP
				write_log(_T("GVP IDE GET %08x %02x %d %08x\n"), addr, v, regnum, M68K_GETPC);
#endif
				if (regnum >= 0) {
					v = get_ide_reg(board, regnum);
				} else if (is_gvp2_intreq(addr)) {
					v = board->irq ? 0x40 : 0x00;
#if DEBUG_IDE_GVP
					write_log(_T("GVP IRQ %02x\n"), v);
#endif
					ide_interrupt_check(board, false);
				} else if (is_gvp1_intreq(addr)) {
					v = board->irq ? 0x80 : 0x00;
#if DEBUG_IDE_GVP
					write_log(_T("GVP IRQ %02x\n"), v);
#endif
					ide_interrupt_check(board, false);
				}
			}
		} else {
			v = 0xff;
		}

	} else if (board->type == ADIDE_IDE) {

		if (addr & ADIDE_ROM_OFFSET) {
			v = board->rom[addr & board->rom_mask];
		} else if (board->configured) {
			int regnum = get_adide_reg(addr, board);
			v = get_ide_reg(board, regnum);
			v = adide_decode_word(v);
		}

	} else if (board->type == MTEC_IDE) {

		if (!(addr & 0x8000)) {
			v = board->rom[addr & board->rom_mask];
		} else if (board->configured) {
			v = get_ide_reg(board, (addr >> 8) & 7);
		}

	} else if (board->type == PROTAR_IDE) {

		v = board->rom[addr & board->rom_mask];

	} else if (board->type == DATAFLYERPLUS_IDE) {

		v = board->rom[addr & board->rom_mask];
		if (board->configured) {
			if (addr == 0x10) {
				if (board->subtype & 2) {
					v = ide_irq_check(board->ide[0], false) ? 0x08 : 0x00;
				}
			} else if (addr < 0x80) {
				if (board->subtype & 1) {
					v = idescsi_scsi_get(oaddr);
				} else {
					v = 0xff;
				}
			}
		}
		if ((addr & 0x8000) && (board->subtype & 2)) {
			int regnum = get_dataflyerplus_reg(addr, board);
			if (regnum >= 0)
				v = get_ide_reg(board, regnum);
		}

	} else if (board->type == ROCHARD_IDE) {

		if (addr & 0x8000) {
			int portnum;
			int regnum = get_rochard_reg(addr, board, &portnum);
			if (regnum >= 0 && board->ide[portnum])
				v = get_ide_reg_multi(board, regnum, portnum, 1);
		} else if ((addr & 0x7c00) == 0x7000) {
			if (board->subtype)
				v = idescsi_scsi_get(oaddr);
			else
				v = 0;
		} else {
			v = board->rom[addr & board->rom_mask];
		}

	} else if (board->type == ATEAM_IDE) {

		if (addr == 1) {
			v = ide_irq_check(board->ide[0], false) ? 0x00 : 0x80;
		} else {
			int reg = get_ateam_reg(addr, board);
			if (reg >= 0) {
				v = get_ide_reg(board, reg);
			} else {
				v = board->rom[addr & board->rom_mask];
			}
		}

	}

	return v;
}

static uae_u32 ide_read_word(struct ide_board *board, uaecptr addr)
{
	uae_u32 v = 0xffff;

	addr &= board->mask;

	if (addr < 0x40 && (!board->configured || board->keepautoconfig)) {
		v = board->acmemory[addr] << 8;
		v |= board->acmemory[addr + 1];
		return v;
	}

	if (board->type == APOLLO_IDE) {

		if (addr >= APOLLO_ROM_OFFSET) {
			if (board->rom) {
				v = board->rom[(addr + 0 - APOLLO_ROM_OFFSET) & board->rom_mask];
				v <<= 8;
				v |= board->rom[(addr + 1 - APOLLO_ROM_OFFSET) & board->rom_mask];
			}
		}

	} else if (board->type == DATAFLYERPLUS_IDE) {

		if (!(addr & 0x8000) && (board->subtype & 2)) {
			if (board->rom) {
				v = board->rom[(addr + 0) & board->rom_mask];
				v <<= 8;
				v |= board->rom[(addr + 1) & board->rom_mask];
			}
		}

	} else if (board->type == ROCHARD_IDE) {

		if (addr < 8192) {
			if (board->rom) {
				v = board->rom[(addr + 0) & board->rom_mask];
				v <<= 8;
				v |= board->rom[(addr + 1) & board->rom_mask];
			}
		}

	}

	if (board->configured) {

		if (board->type == BUDDHA_IDE) {

			int portnum;
			int regnum = get_buddha_reg(addr, board, &portnum);
			if (regnum == IDE_DATA) {
				if (board->ide[portnum])
					v = get_ide_reg_multi(board, IDE_DATA, portnum, 1);
			} else {
				v = ide_read_byte(board, addr) << 8;
				v |= ide_read_byte(board, addr + 1);
			}
			
		} else if (board->type == ALF_IDE) {

			int regnum = get_alf_reg(addr, board);
			if (regnum == IDE_DATA) {
				v = get_ide_reg(board, IDE_DATA);
			} else {
				v = 0;
				if (addr == 0x4000 && board->intena)
					v = board->irq ? 0x8000 : 0x0000;
#if DEBUG_IDE_ALF
				write_log(_T("ALF IO WORD READ %08x %08x\n"), addr, M68K_GETPC);
#endif
			}

		} else if (board->type == GOLEMFAST_IDE) {

			if ((addr & 0x8700) == 0x8100) {
				int regnum = get_golemfast_reg(addr, board);
				if (regnum == IDE_DATA) {
					v = get_ide_reg(board, IDE_DATA);
				} else {
					v = ide_read_byte(board, addr) << 8;
					v |= ide_read_byte(board, addr + 1);
				}
			} else {
				v = ide_read_byte(board, addr) << 8;
				v |= ide_read_byte(board, addr + 1);
			}

		} else if (board->type == MASOBOSHI_IDE) {

			if (addr >= MASOBOSHI_ROM_OFFSET && addr < MASOBOSHI_ROM_OFFSET_END) {
				if (board->rom) {
					v = board->rom[addr & board->rom_mask] << 8;
					v |= board->rom[(addr + 1) & board->rom_mask];
				}
			} else if (addr == 0xf04c || addr == 0xf14c) {
				v = board->dma_ptr >> 16;
			} else if (addr == 0xf04e || addr == 0xf14e) {
				v = board->dma_ptr;
				write_log(_T("MASOBOSHI IDE DMA PTR READ = %08x %08x\n"), board->dma_ptr, M68K_GETPC);
			} else if (addr == 0xf04a || addr == 0xf14a) {
				v = board->dma_cnt;
				write_log(_T("MASOBOSHI IDE DMA LEN READ = %04x %08x\n"), board->dma_cnt, v, M68K_GETPC);
			} else {
				int regnum = get_masoboshi_reg(addr, board);
				if (regnum == IDE_DATA) {
					v = get_ide_reg(board, IDE_DATA);
				} else {
					v = ide_read_byte(board, addr) << 8;
					v |= ide_read_byte(board, addr + 1);
				}
			}

		} else if (board->type == APOLLO_IDE) {

			if ((addr & 0xc000) == 0x4000) {
				v = apollo_scsi_bget(addr, board->userdata);
				v <<= 8;
				v |= apollo_scsi_bget(addr + 1, board->userdata);
			} else if (addr < 0x4000) {
				int regnum = get_apollo_reg(addr, board);
				if (regnum == IDE_DATA) {
					v = get_ide_reg(board, IDE_DATA);
				} else {
					v = 0;
				}
			}

		} else if (board->type == GVP_IDE) {

			if (board == gvp_ide_controller_board || ISCPUBOARD(BOARD_GVP, BOARD_GVP_SUB_A3001SI)) {
				if (addr < 0x60) {
					if (is_gvp1_intreq(addr))
						v = gvp_ide_controller_board->irq ? 0x8000 : 0x0000;
					else if (addr == 0x40) {
						if (ISCPUBOARD(BOARD_GVP, BOARD_GVP_SUB_A3001SII))
							v = board->intena ? 8 : 0;
					}
#if DEBUG_IDE_GVP
					write_log(_T("GVP IO WORD READ %08x %08x\n"), addr, M68K_GETPC);
#endif
				} else {
					int regnum = get_gvp_reg(addr, board);
					if (regnum == IDE_DATA) {
						v = get_ide_reg(board, IDE_DATA);
#if DEBUG_IDE_GVP > 2
						write_log(_T("IDE WORD READ %04x\n"), v);
#endif
					} else {
						v = ide_read_byte(board, addr) << 8;
						v |= ide_read_byte(board, addr + 1);
					}
				}	
			}

		} else if (board->type == ADIDE_IDE) {

			int regnum = get_adide_reg(addr, board);
			if (regnum == IDE_DATA) {
				v = get_ide_reg(board, IDE_DATA);
			} else {
				v = get_ide_reg(board, regnum) << 8;
				v = adide_decode_word(v);
			}

		} else if (board->type == MTEC_IDE) {

			if (board->configured && (addr & 0x8000)) {
				int regnum = (addr >> 8) & 7;
				if (regnum == IDE_DATA)
					v = get_ide_reg(board, regnum);
				else
					v = ide_read_byte(board, addr) << 8;
			}

		} else if (board->type == DATAFLYERPLUS_IDE) {

			if (board->configured) {
				if (board->subtype & 2) {
					int reg = get_dataflyerplus_reg(addr, board);
					if (reg >= 0)
						v = get_ide_reg_multi(board, reg, 0, 1);
				} else {
					v = 0xff;
				}
			}

		} else if (board->type == ROCHARD_IDE) {

			if (board->configured && (addr & 0x8020) == 0x8000) {
				v = get_ide_reg_multi(board, IDE_DATA, (addr & 0x4000) ? 1 : 0, 1);
			}

		} else if (board->type == ATEAM_IDE) {

			if (board->configured && (addr & 0xf800) == 0xf800) {
				v = get_ide_reg_multi(board, IDE_DATA, 0, 1);
			}

		}

	}

#if DEBUG_IDE
	write_log(_T("IDE IO WORD READ %08x %04x %08x\n"), addr, v, M68K_GETPC);
#endif

	return v;
}

static void ide_write_byte(struct ide_board *board, uaecptr addr, uae_u8 v)
{
	uaecptr oaddr = addr;
	addr &= board->mask;

#if DEBUG_IDE
	write_log(_T("IDE IO BYTE WRITE %08x=%02x %08x\n"), addr, v, M68K_GETPC);
#endif

	if (!board->configured) {
		addrbank *ab = board->bank;
		if (addr == 0x48) {
			map_banks_z2(ab, v, (board->mask + 1) >> 16);
			board->baseaddress = v << 16;
			board->configured = 1;
			if (board->type == ROCHARD_IDE) {
				rochard_scsi_init(board->original_rc, board->baseaddress);
			} else if (board->type == MASOBOSHI_IDE) {
				ncr_masoboshi_autoconfig_init(board->original_rc, board->baseaddress);
			} else if (board->type == GOLEMFAST_IDE) {
				ncr_golemfast_autoconfig_init(board->original_rc, board->baseaddress);
			} else if (board->type == DATAFLYERPLUS_IDE) {
				dataflyerplus_scsi_init(board->original_rc, board->baseaddress);
			}
			expamem_next(ab, NULL);
			return;
		}
		if (addr == 0x4c) {
			board->configured = 1;
			expamem_shutup(ab);
			return;
		}
	}
	if (board->configured) {

		if (board->type == BUDDHA_IDE) {

			int portnum;
			int regnum = get_buddha_reg(addr, board, &portnum);
			if (regnum >= 0) {
				if (board->ide[portnum]) {
					put_ide_reg_multi(board, regnum, v, portnum, 1);
				}
			} else if (addr >= 0xfc0 && addr < 0xfc4) {
				board->intena = true;
			} else if (addr >= 0x7fc && addr <= 0x7ff) {
				board->userdata = v;
			}

		} else  if (board->type == ALF_IDE) {

			int regnum = get_alf_reg(addr, board);
			if (regnum >= 0)
				put_ide_reg(board, regnum, v);
#if DEBUG_IDE_ALF
			write_log(_T("ALF PUT %08x %02x %d %08x\n"), addr, v, regnum, M68K_GETPC);
#endif
		} else if (board->type == GOLEMFAST_IDE) {

			if ((addr & 0x8700) == 0x8400 || (addr & 0x8700) == 0x8000) {
				golemfast_ncr9x_scsi_put(oaddr, v, getidenum(board, golemfast_board));
			} else if ((addr & 0x8700) == 0x8100) {
				int regnum = get_golemfast_reg(addr, board);
				if (regnum >= 0)
					put_ide_reg(board, regnum, v);
			}

		} else if (board->type == MASOBOSHI_IDE) {

#if DEBUG_IDE_MASOBOSHI
			write_log(_T("MASOBOSHI IO BYTE PUT %08x %02x %08x\n"), addr, v, M68K_GETPC);
#endif
			int regnum = get_masoboshi_reg(addr, board);
			if (regnum >= 0) {
				put_ide_reg(board, regnum, v);
			} else if (addr >= MASOBOSHI_SCSI_OFFSET && addr < MASOBOSHI_SCSI_OFFSET_END) {
				if (board->subtype)
					masoboshi_ncr9x_scsi_put(oaddr, v, getidenum(board, masoboshi_board));
			} else if ((addr >= 0xf000 && addr <= 0xf007)) {
				if (board->subtype)
					masoboshi_ncr9x_scsi_put(oaddr, v, getidenum(board, masoboshi_board));
			} else if (addr >= 0xf00a && addr <= 0xf00f) {
				// scsi dma controller
				masoboshi_ncr9x_scsi_put(oaddr, v, getidenum(board, masoboshi_board));
			} else if (addr >= 0xf040 && addr <= 0xf04f) {
				// ide dma controller
				if (addr >= 0xf04c && addr < 0xf050) {
					int shift = (3 - (addr - 0xf04c)) * 8;
					uae_u32 mask = 0xff << shift;
					board->dma_ptr &= ~mask;
					board->dma_ptr |= v << shift;
					board->dma_ptr &= 0xffffff;
				} else if (addr >= 0xf04a && addr < 0xf04c) {
					if (addr == 0xf04a) {
						board->dma_cnt &= 0x00ff;
						board->dma_cnt |= v << 8;
					} else {
						board->dma_cnt &= 0xff00;
						board->dma_cnt |= v;
					}
				} else if (addr >= 0xf040 && addr < 0xf048) {
					board->state2[addr - 0xf040] = v;
					board->state2[0] &= ~0x80;
					if (addr == 0xf047) {
						board->state = v;
						board->intena = (v & 8) != 0;
						// masoboshi ide dma
						if (v & 0x80) {
							board->hsync_code = masoboshi_ide_dma;
							board->hsync_cnt = (board->dma_cnt / maxhpos) * 2 + 1;
							write_log(_T("MASOBOSHI IDE DMA %s start %08x, %d\n"), (board->state2[5] & 0x80) ? _T("READ") : _T("WRITE"), board->dma_ptr, board->dma_cnt);
							if (ide_drq_check(board->ide[0])) {
								if (!(board->state2[5] & 0x80)) {
									for (int i = 0; i < board->dma_cnt; i++) {
										put_ide_reg(board, IDE_DATA, get_word(board->dma_ptr & ~1));
										board->dma_ptr += 2;
									}
								} else {
									for (int i = 0; i < board->dma_cnt; i++) {
										put_word(board->dma_ptr & ~1, get_ide_reg(board, IDE_DATA));
										board->dma_ptr += 2;
									}
								}
								board->dma_cnt = 0;
							}
						}
					}
					if (addr == 0xf040) {
						board->state2[0] &= ~0x80;
						board->irq = false;
					}
				}
			}

		} else if (board->type == APOLLO_IDE) {

			if ((addr & 0xc000) == 0x4000) {
				apollo_scsi_bput(oaddr, v, board->userdata);
			} else if (addr < 0x4000) {
				int regnum = get_apollo_reg(addr, board);
				if (regnum >= 0) {
					put_ide_reg(board, regnum, v);
				}
			}

		} else if (board->type == GVP_IDE) {

			if (board == gvp_ide_rom_board && ISCPUBOARD(BOARD_GVP, BOARD_GVP_SUB_A3001SII)) {
#if DEBUG_IDE_GVP
				write_log(_T("GVP BOOT PUT %08x %02x %08x\n"), addr, v, M68K_GETPC);
#endif
			} else {
				int regnum = get_gvp_reg(addr, board);
#if DEBUG_IDE_GVP
				write_log(_T("GVP IDE PUT %08x %02x %d %08x\n"), addr, v, regnum, M68K_GETPC);
#endif
				if (regnum >= 0)
					put_ide_reg(board, regnum, v);
			}

		} else if (board->type == ADIDE_IDE) {

			if (board->configured) {
				int regnum = get_adide_reg(addr, board);
				v = adide_encode_word(v);
				put_ide_reg(board, regnum, v);
			}

		} else if (board->type == MTEC_IDE) {

			if (board->configured && (addr & 0x8000)) {
				put_ide_reg(board, (addr >> 8) & 7, v);
			}

		} else if (board->type == DATAFLYERPLUS_IDE) {

			if (board->configured) {
				if (addr & 0x8000) {
					if  (board->subtype & 2) {
						int regnum = get_dataflyerplus_reg(addr, board);
						if (regnum >= 0)
							put_ide_reg(board, regnum, v);
					}
				} else if (addr < 0x80) {
					if (board->subtype & 1) {
						idescsi_scsi_put(oaddr, v);
					}
				}
			}

		} else if (board->type == ROCHARD_IDE) {

			if (board->configured) {
				if (addr & 0x8000) {
					int portnum;
					int regnum = get_rochard_reg(addr, board, &portnum);
					if (regnum >= 0 && board->ide[portnum])
						put_ide_reg_multi(board, regnum, v, portnum, 1);
				} else if ((addr & 0x7c00) == 0x7000) {
					if (board->subtype)
						idescsi_scsi_put(oaddr, v);
				}
			}

		} else if (board->type == ATEAM_IDE) {

			if ((addr & 0xff01) == 0x0101) {
				// disable interrupt strobe address
				board->intena = false;
			} else if ((addr & 0xff01) == 0x0201) {
				// enable interrupt strobe address
				board->intena = true;
			} else {
				int reg = get_ateam_reg(addr, board);
				if (reg >= 0) {
					put_ide_reg(board, reg, v);
				}
			}

		}

	}
}

static void ide_write_word(struct ide_board *board, uaecptr addr, uae_u16 v)
{
	addr &= board->mask;

#if DEBUG_IDE
	write_log(_T("IDE IO WORD WRITE %08x=%04x %08x\n"), addr, v, M68K_GETPC);
#endif
	if (board->configured) {

		if (board->type == BUDDHA_IDE) {

			int portnum;
			int regnum = get_buddha_reg(addr, board, &portnum);
			if (regnum == IDE_DATA) {
				if (board->ide[portnum])
					put_ide_reg_multi(board, IDE_DATA, v, portnum, 1);
			} else {
				ide_write_byte(board, addr, v >> 8);
				ide_write_byte(board, addr + 1, v);
			}

		} else if (board->type == ALF_IDE) {

			int regnum = get_alf_reg(addr, board);
			if (regnum == IDE_DATA) {
				put_ide_reg(board, IDE_DATA, v);
			} else {
#if DEBUG_IDE_ALF
				write_log(_T("ALF IO WORD WRITE %08x %04x %08x\n"), addr, v, M68K_GETPC);
#endif
			}


		} else if (board->type == GOLEMFAST_IDE) {

			if ((addr & 0x8700) == 0x8100) {
				int regnum = get_golemfast_reg(addr, board);
				if (regnum == IDE_DATA) {
					put_ide_reg(board, IDE_DATA, v);
				} else {
					ide_write_byte(board, addr, v >> 8);
					ide_write_byte(board, addr + 1, v);
				}
			} else {
				ide_write_byte(board, addr, v >> 8);
				ide_write_byte(board, addr + 1, v);
			}
		
		} else if (board->type == MASOBOSHI_IDE) {

			int regnum = get_masoboshi_reg(addr, board);
			if (regnum == IDE_DATA) {
				put_ide_reg(board, IDE_DATA, v);
			} else {
				ide_write_byte(board, addr, v >> 8);
				ide_write_byte(board, addr + 1, v);
			}
#if DEBUG_IDE_MASOBOSHI
			write_log(_T("MASOBOSHI IO WORD WRITE %08x %04x %08x\n"), addr, v, M68K_GETPC);
#endif
	
		} else if (board->type == APOLLO_IDE) {

			if ((addr & 0xc000) == 0x4000) {
				apollo_scsi_bput(addr, v >> 8, board->userdata);
				apollo_scsi_bput(addr + 1, v, board->userdata);
			} else if (addr < 0x4000) {
				int regnum = get_apollo_reg(addr, board);
				if (regnum == IDE_DATA) {
					put_ide_reg(board, IDE_DATA, v);
				}
			}

		} else if (board->type == GVP_IDE) {

			if (board == gvp_ide_controller_board || ISCPUBOARD(BOARD_GVP, BOARD_GVP_SUB_A3001SI)) {
				if (addr < 0x60) {
#if DEBUG_IDE_GVP
					write_log(_T("GVP IO WORD WRITE %08x %04x %08x\n"), addr, v, M68K_GETPC);
#endif
					if (addr == 0x40 && ISCPUBOARD(BOARD_GVP, BOARD_GVP_SUB_A3001SII))
						board->intena = (v & 8) != 0;
				} else {
					int regnum = get_gvp_reg(addr, board);
					if (regnum == IDE_DATA) {
						put_ide_reg(board, IDE_DATA, v);
#if DEBUG_IDE_GVP > 2
						write_log(_T("IDE WORD WRITE %04x\n"), v);
#endif
					} else {
						ide_write_byte(board, addr, v >> 8);
						ide_write_byte(board, addr + 1, v & 0xff);
					}
				}
			}

		} else if (board->type == ADIDE_IDE) {

			int regnum = get_adide_reg(addr, board);
			if (regnum == IDE_DATA) {
				put_ide_reg(board, IDE_DATA, v);
			} else {
				v = adide_encode_word(v);
				put_ide_reg(board, regnum, v >> 8);
			}

		} else if (board->type == MTEC_IDE) {

			if (board->configured && (addr & 0x8000)) {
				int regnum = (addr >> 8) & 7;
				if (regnum == IDE_DATA)
					put_ide_reg(board, regnum, v);
				else
					ide_write_byte(board, addr, v >> 8);
			}

		} else if (board->type == DATAFLYERPLUS_IDE) {

			if (board->configured) {
				if (board->subtype & 2) {
					int reg = get_dataflyerplus_reg(addr, board);
					if (reg >= 0)
						put_ide_reg_multi(board, reg, v, 0, 1);
				}
			}

		} else if (board->type == ROCHARD_IDE) {

			if (board->configured && (addr & 0x8020) == 0x8000) {
				put_ide_reg_multi(board, IDE_DATA, v, (addr & 0x4000) ? 1 : 0, 1);
			}

		} else if (board->type == ATEAM_IDE) {
		
			if (board->configured && (addr & 0xf800) == 0xf800) {
				put_ide_reg_multi(board, IDE_DATA, v, 0, 1);
			}
		
		}
	}
}

static uae_u32 ide_read_wordi(struct ide_board *board, uaecptr addr)
{
	uae_u16 v = 0;
	if (board->type == GOLEMFAST_IDE) {

		if (!(addr & 0x8000)) {
			if (board->rom) {
				v = board->rom[addr & board->rom_mask] << 8;
				v |= board->rom[(addr + 1) & board->rom_mask];
			}
		}

	} else {

		v = dummy_wgeti(addr);

	}
	return v;
}



IDE_MEMORY_FUNCTIONS(ide_controller_gvp, ide, gvp_ide_controller_board);

addrbank gvp_ide_controller_bank = {
	ide_controller_gvp_lget, ide_controller_gvp_wget, ide_controller_gvp_bget,
	ide_controller_gvp_lput, ide_controller_gvp_wput, ide_controller_gvp_bput,
	default_xlate, default_check, NULL, NULL, _T("GVP IDE"),
	dummy_lgeti, dummy_wgeti,
	ABFLAG_IO | ABFLAG_SAFE, S_READ, S_WRITE
};

IDE_MEMORY_FUNCTIONS(ide_rom_gvp, ide, gvp_ide_rom_board);

addrbank gvp_ide_rom_bank = {
	ide_rom_gvp_lget, ide_rom_gvp_wget, ide_rom_gvp_bget,
	ide_rom_gvp_lput, ide_rom_gvp_wput, ide_rom_gvp_bput,
	default_xlate, default_check, NULL, NULL, _T("GVP BOOT"),
	dummy_lgeti, dummy_wgeti,
	ABFLAG_IO | ABFLAG_SAFE, S_READ, S_WRITE
};

static void REGPARAM2 ide_generic_bput (uaecptr addr, uae_u32 b)
{
	struct ide_board *ide = getideboard(addr);
	if (ide)
		ide_write_byte(ide, addr, b);
}
static void REGPARAM2 ide_generic_wput (uaecptr addr, uae_u32 b)
{
	struct ide_board *ide = getideboard(addr);
	if (ide)
		ide_write_word(ide, addr, b);
}
static void REGPARAM2 ide_generic_lput (uaecptr addr, uae_u32 b)
{
	struct ide_board *ide = getideboard(addr);
	if (ide) {
		ide_write_word(ide, addr, b >> 16);
		ide_write_word(ide, addr + 2, b);
	}
}
static uae_u32 REGPARAM2 ide_generic_bget (uaecptr addr)
{
	struct ide_board *ide = getideboard(addr);
	if (ide)
		return ide_read_byte(ide, addr);
	return 0;
}
static uae_u32 REGPARAM2 ide_generic_wget (uaecptr addr)
{
	struct ide_board *ide = getideboard(addr);
	if (ide)
		return ide_read_word(ide, addr);
	return 0;
}
static uae_u32 REGPARAM2 ide_generic_lget (uaecptr addr)
{
	struct ide_board *ide = getideboard(addr);
	if (ide) {
		uae_u32 v = ide_read_word(ide, addr) << 16;
		v |= ide_read_word(ide, addr + 2);
		return v;
	}
	return 0;
}
static uae_u32 REGPARAM2 ide_generic_wgeti(uaecptr addr)
{
	struct ide_board *ide = getideboard(addr);
	if (ide)
		return ide_read_wordi(ide, addr);
	return 0;
}
static uae_u32 REGPARAM2 ide_generic_lgeti(uaecptr addr)
{
	struct ide_board *ide = getideboard(addr);
	if (ide) {
		uae_u32 v = ide_read_wordi(ide, addr) << 16;
		v |= ide_read_wordi(ide, addr + 2);
		return v;
	}
	return 0;
}
static uae_u8 *REGPARAM2 ide_generic_xlate(uaecptr addr)
{
	struct ide_board *ide = getideboard(addr);
	if (!ide)
		return NULL;
	addr &= ide->rom_mask;
	return ide->rom + addr;

}
static int REGPARAM2 ide_generic_check(uaecptr a, uae_u32 b)
{
	struct ide_board *ide = getideboard(a);
	if (!ide)
		return 0;
	a &= ide->rom_mask;
	if (a >= ide->rom_start && a + b < ide->rom_size)
		return 1;
	return 0;
}

static addrbank ide_bank_generic = {
	ide_generic_lget, ide_generic_wget, ide_generic_bget,
	ide_generic_lput, ide_generic_wput, ide_generic_bput,
	ide_generic_xlate, ide_generic_check, NULL, NULL, _T("IDE"),
	ide_generic_lgeti, ide_generic_wgeti,
	ABFLAG_IO | ABFLAG_SAFE, S_READ, S_WRITE
};


static void ew(struct ide_board *ide, int addr, uae_u32 value)
{
	addr &= 0xffff;
	if (addr == 00 || addr == 02 || addr == 0x40 || addr == 0x42) {
		ide->acmemory[addr] = (value & 0xf0);
		ide->acmemory[addr + 2] = (value & 0x0f) << 4;
	} else {
		ide->acmemory[addr] = ~(value & 0xf0);
		ide->acmemory[addr + 2] = ~((value & 0x0f) << 4);
	}
}

static const uae_u8 gvp_ide2_rom_autoconfig[16] = { 0xd1, 0x0d, 0x00, 0x00, 0x07, 0xe1, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00 };
static const uae_u8 gvp_ide2_controller_autoconfig[16] = { 0xc1, 0x0b, 0x00, 0x00, 0x07, 0xe1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const uae_u8 gvp_ide1_controller_autoconfig[16] = { 0xd1, 0x08, 0x00, 0x00, 0x07, 0xe1, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00 };

bool gvp_ide_rom_autoconfig_init(struct autoconfig_info *aci)
{
	const uae_u8 *autoconfig;
	if (ISCPUBOARD(BOARD_GVP, BOARD_GVP_SUB_A3001SI)) {
		autoconfig = gvp_ide1_controller_autoconfig;
	} else {
		autoconfig = gvp_ide2_rom_autoconfig;
	}
	aci->autoconfigp = autoconfig;
	if (!aci->doinit)
		return true;

	struct ide_board *ide = getide(aci->rc);

	if (ISCPUBOARD(BOARD_GVP, BOARD_GVP_SUB_A3001SI)) {
		ide->bank = &gvp_ide_rom_bank;
		init_ide(ide, GVP_IDE, 2, true, false);
		ide->rom_size = 8192;
		gvp_ide_controller_board->intena = true;
		ide->intena = true;
		gvp_ide_controller_board->configured = -1;
	} else {
		ide->bank = &gvp_ide_rom_bank;
		ide->rom_size = 16384;
	}
	ide->configured = 0;
	ide->mask = 65536 - 1;
	ide->type = GVP_IDE;
	ide->configured = 0;
	memset(ide->acmemory, 0xff, sizeof ide->acmemory);

	ide->rom = xcalloc(uae_u8, ide->rom_size);
	memset(ide->rom, 0xff, ide->rom_size);
	ide->rom_mask = ide->rom_size - 1;

	load_rom_rc(aci->rc, ROMTYPE_CB_A3001S1, ide->rom_size, 0, ide->rom, ide->rom_size, LOADROM_FILL);
	for (int i = 0; i < 16; i++) {
		uae_u8 b = autoconfig[i];
		ew(ide, i * 4, b);
	}
	aci->addrbank = ide->bank;
	return true;
}

bool gvp_ide_controller_autoconfig_init(struct autoconfig_info *aci)
{
	if (!aci->doinit) {
		aci->autoconfigp = gvp_ide2_controller_autoconfig;
		return true;
	}
	struct ide_board *ide = getide(aci->rc);

	init_ide(ide, GVP_IDE, 2, true, false);
	ide->configured = 0;
	ide->bank = &gvp_ide_controller_bank;
	memset(ide->acmemory, 0xff, sizeof ide->acmemory);
	for (int i = 0; i < 16; i++) {
		uae_u8 b = gvp_ide2_controller_autoconfig[i];
		ew(ide, i * 4, b);
	}
	aci->addrbank = ide->bank;
	return true;
}

void gvp_add_ide_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	struct ide_hdf *ide;

	if (!allocide(&gvp_ide_rom_board, rc, ch))
		return;
	if (!allocide(&gvp_ide_controller_board, rc, ch))
		return;
	ide = add_ide_unit (&idecontroller_drive[(GVP_IDE + ci->controller_type_unit) * 2], 2, ch, ci, rc);
}

static const uae_u8 alf_autoconfig[16] = { 0xd1, 6, 0x00, 0x00, 0x08, 0x2c, 0x00, 0x00, 0x00, 0x00, ALF_ROM_OFFSET >> 8, ALF_ROM_OFFSET & 0xff };
static const uae_u8 alfplus_autoconfig[16] = { 0xd1, 38, 0x00, 0x00, 0x08, 0x2c, 0x00, 0x00, 0x00, 0x00, ALF_ROM_OFFSET >> 8, ALF_ROM_OFFSET & 0xff };

bool alf_init(struct autoconfig_info *aci)
{
	bool alfplus = is_board_enabled(&currprefs, ROMTYPE_ALFAPLUS, 0);
	if (!aci->doinit) {
		aci->autoconfigp = alfplus ? alfplus_autoconfig : alf_autoconfig;
		return true;
	}

	struct ide_board *ide = getide(aci->rc);
	if (!ide)
		return false;

	ide->configured = 0;

	ide->configured = 0;
	ide->bank = &ide_bank_generic;
	ide->type = ALF_IDE;
	ide->rom_size = 32768 * 6;
	ide->userdata = alfplus;
	ide->intena = alfplus;
	ide->mask = 65536 - 1;

	memset(ide->acmemory, 0xff, sizeof ide->acmemory);

	ide->rom = xcalloc(uae_u8, ide->rom_size);
	memset(ide->rom, 0xff, ide->rom_size);
	ide->rom_mask = ide->rom_size - 1;

	for (int i = 0; i < 16; i++) {
		uae_u8 b = alfplus ? alfplus_autoconfig[i] : alf_autoconfig[i];
		ew(ide, i * 4, b);
	}

	if (!aci->rc->autoboot_disabled) {
		struct zfile *z = read_device_from_romconfig(aci->rc, alfplus ? ROMTYPE_ALFAPLUS : ROMTYPE_ALFA);
		if (z) {
			for (int i = 0; i < 0x1000 / 2; i++) {
				uae_u8 b;
				zfile_fread(&b, 1, 1, z);
				ide->rom[ALF_ROM_OFFSET + i * 4 + 0] = b;
				zfile_fread(&b, 1, 1, z);
				ide->rom[ALF_ROM_OFFSET + i * 4 + 2] = b;
			}
			for (int i = 0; i < 32768 - 0x1000; i++) {
				uae_u8 b;
				zfile_fread(&b, 1, 1, z);
				ide->rom[0x2000 + i * 4 + 1] = b;
				zfile_fread(&b, 1, 1, z);
				ide->rom[0x2000 + i * 4 + 3] = b;
			}
			zfile_fclose(z);
		}
	}
	aci->addrbank = ide->bank;
	return true;
}

void alf_add_ide_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	add_ide_standard_unit(ch, ci, rc, alf_board, ALF_IDE, false, false, 2);
}

// prod 0x22 = IDE + SCSI
// prod 0x23 = SCSI only
// prod 0x33 = IDE only

const uae_u8 apollo_autoconfig[16] = { 0xd1, 0x22, 0x00, 0x00, 0x22, 0x22, 0x00, 0x00, 0x00, 0x00, APOLLO_ROM_OFFSET >> 8, APOLLO_ROM_OFFSET & 0xff };
const uae_u8 apollo_autoconfig_cpuboard[16] = { 0xd2, 0x23, 0x40, 0x00, 0x22, 0x22, 0x00, 0x00, 0x00, 0x00, APOLLO_ROM_OFFSET >> 8, APOLLO_ROM_OFFSET & 0xff };
const uae_u8 apollo_autoconfig_cpuboard_060[16] = { 0xd2, 0x23, 0x40, 0x00, 0x22, 0x22, 0x00, 0x00, 0x00, 0x02, APOLLO_ROM_OFFSET >> 8, APOLLO_ROM_OFFSET & 0xff };

static bool apollo_init(struct autoconfig_info *aci, bool cpuboard)
{
	const uae_u8 *autoconfig = apollo_autoconfig;
	if (cpuboard) {
		if (currprefs.cpu_model == 68060)
			autoconfig = apollo_autoconfig_cpuboard_060;
		else
			autoconfig = apollo_autoconfig_cpuboard;
	}

	if (!aci->doinit) {
		aci->autoconfigp = autoconfig;
		return true;
	}

	struct ide_board *ide = getide(aci->rc);

	if (!ide)
		return false;

	if (cpuboard) {
		// bit 0: scsi enable
		// bit 1: memory disable
		ide->userdata = currprefs.cpuboard_settings & 1;
	} else {
		ide->userdata = aci->rc->autoboot_disabled ? 2 : 0;
	}

	ide->configured = 0;
	ide->bank = &ide_bank_generic;
	ide->rom_size = 32768;
	ide->type = APOLLO_IDE;

	memset(ide->acmemory, 0xff, sizeof ide->acmemory);

	ide->rom = xcalloc(uae_u8, ide->rom_size);
	memset(ide->rom, 0xff, ide->rom_size);
	ide->rom_mask = ide->rom_size - 1;
	ide->keepautoconfig = false;
	for (int i = 0; i < 16; i++) {
		uae_u8 b = autoconfig[i];
		if (cpuboard && i == 9 && (currprefs.cpuboard_settings & 2))
			b |= 1; // memory disable (serial bit 0)
		ew(ide, i * 4, b);
	}
	if (cpuboard) {
		ide->mask = 131072 - 1;
		struct zfile *z = read_device_from_romconfig(aci->rc, ROMTYPE_APOLLO);
		if (z) {
			int len = zfile_size(z);
			// skip 68060 $f0 ROM block
			if (len >= 65536)
				zfile_fseek(z, 32768, SEEK_SET);
			for (int i = 0; i < 32768; i++) {
				uae_u8 b;
				zfile_fread(&b, 1, 1, z);
				ide->rom[i] = b;
			}
			zfile_fclose(z);
		}
	} else {
		ide->mask = 65536 - 1;
		load_rom_rc(aci->rc, ROMTYPE_APOLLO, 16384, 0, ide->rom, 32768, LOADROM_EVENONLY_ODDONE | LOADROM_FILL);
	}
	aci->addrbank = ide->bank;
	return true;
}

bool apollo_init_hd(struct autoconfig_info *aci)
{
	return apollo_init(aci, false);
}
bool apollo_init_cpu(struct autoconfig_info *aci)
{
	return apollo_init(aci, true);
}

void apollo_add_ide_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	add_ide_standard_unit(ch, ci, rc, apollo_board, APOLLO_IDE, true, false, 2);
}

bool masoboshi_init(struct autoconfig_info *aci)
{
	int rom_size = 65536;
	uae_u8 *rom = xcalloc(uae_u8, rom_size);
	memset(rom, 0xff, rom_size);

	load_rom_rc(aci->rc, ROMTYPE_MASOBOSHI, 32768, 0, rom, 65536, LOADROM_EVENONLY_ODDONE | LOADROM_FILL);
	if (!aci->doinit) {
		if (aci->rc && aci->rc->autoboot_disabled)
			memcpy(aci->autoconfig_raw, rom + 0x100, sizeof aci->autoconfig_raw);
		else
			memcpy(aci->autoconfig_raw, rom + 0x000, sizeof aci->autoconfig_raw);
		xfree(rom);
		return true;
	}

	struct ide_board *ide = getide(aci->rc);

	if (!ide)
		return false;

	ide->configured = 0;
	ide->bank = &ide_bank_generic;
	ide->type = MASOBOSHI_IDE;
	ide->rom_size = rom_size;
	ide->rom_mask = ide->mask = rom_size - 1;
	ide->rom = rom;
	ide->subtype = aci->rc->subtype;

	if (aci->rc && aci->rc->autoboot_disabled)
		memcpy(ide->acmemory, ide->rom + 0x100, sizeof ide->acmemory);
	else
		memcpy(ide->acmemory, ide->rom + 0x000, sizeof ide->acmemory);

	aci->addrbank = ide->bank;
	return true;
}

static void masoboshi_add_ide_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	add_ide_standard_unit(ch, ci, rc, masoboshi_board, MASOBOSHI_IDE, true, false, 2);
}

void masoboshi_add_idescsi_unit (int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	if (ch < 0) {
		masoboshi_add_ide_unit(ch, ci, rc);
		masoboshi_add_scsi_unit(ch, ci, rc);
	} else {
		if (ci->controller_type < HD_CONTROLLER_TYPE_SCSI_FIRST)
			masoboshi_add_ide_unit(ch, ci, rc);
		else
			masoboshi_add_scsi_unit(ch, ci, rc);
	}
}

static const uae_u8 adide_autoconfig[16] = { 0xd1, 0x02, 0x00, 0x00, 0x08, 0x17, 0x00, 0x00, 0x00, 0x00, ADIDE_ROM_OFFSET >> 8, ADIDE_ROM_OFFSET & 0xff };

bool adide_init(struct autoconfig_info *aci)
{
	if (!aci->doinit) {
		aci->autoconfigp = adide_autoconfig;
		return true;
	}
	struct ide_board *ide = getide(aci->rc);

	ide->configured = 0;
	ide->keepautoconfig = false;
	ide->bank = &ide_bank_generic;
	ide->rom_size = 32768;
	ide->mask = 65536 - 1;

	memset(ide->acmemory, 0xff, sizeof ide->acmemory);

	ide->rom = xcalloc(uae_u8, ide->rom_size);
	memset(ide->rom, 0xff, ide->rom_size);
	ide->rom_mask = ide->rom_size - 1;
	if (!aci->rc->autoboot_disabled) {
		load_rom_rc(aci->rc, ROMTYPE_ADIDE, 16384, 0, ide->rom, 32768, LOADROM_EVENONLY_ODDONE | LOADROM_FILL);
	}
	for (int i = 0; i < 16; i++) {
		uae_u8 b = adide_autoconfig[i];
		ew(ide, i * 4, b);
	}
	aci->addrbank = ide->bank;
	return true;
}

void adide_add_ide_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	add_ide_standard_unit(ch, ci, rc, adide_board, ADIDE_IDE, false, true, 2);
}

bool mtec_init(struct autoconfig_info *aci)
{
	uae_u8 *rom;
	int rom_size = 32768;

	rom = xcalloc(uae_u8, rom_size);
	memset(rom, 0xff, rom_size);
	load_rom_rc(aci->rc, ROMTYPE_MTEC, 16384, !aci->rc->autoboot_disabled ? 16384 : 0, rom, 32768, LOADROM_EVENONLY_ODDONE | LOADROM_FILL);

	if (!aci->doinit) {
		memcpy(aci->autoconfig_raw, rom, sizeof aci->autoconfig_raw);
		xfree(rom);
		return true;
	}

	struct ide_board *ide = getide(aci->rc);

	ide->configured = 0;
	ide->bank = &ide_bank_generic;
	ide->mask = 65536 - 1;

	ide->rom = rom;
	ide->rom_size = rom_size;
	ide->rom_mask = rom_size - 1;
	memcpy(ide->acmemory, ide->rom, sizeof ide->acmemory);

	aci->addrbank = ide->bank;
	return true;
}

void mtec_add_ide_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	add_ide_standard_unit(ch, ci, rc, mtec_board, MTEC_IDE, false, false, 2);
}

bool rochard_init(struct autoconfig_info *aci)
{
	if (!aci->doinit) {
		load_rom_rc(aci->rc, ROMTYPE_ROCHARD, 8192, !aci->rc->autoboot_disabled ? 8192 : 0, aci->autoconfig_raw, sizeof aci->autoconfig_raw, 0);
		return true;
	}
	struct ide_board *ide = getide(aci->rc);

	ide->configured = 0;
	ide->bank = &ide_bank_generic;
	ide->rom_size = 32768;
	ide->mask = 65536 - 1;
	ide->subtype = aci->rc->subtype;

	ide->rom = xcalloc(uae_u8, ide->rom_size);
	memset(ide->rom, 0xff, ide->rom_size);
	ide->rom_mask = ide->rom_size - 1;
	load_rom_rc(aci->rc, ROMTYPE_ROCHARD, 8192, !aci->rc->autoboot_disabled ? 8192 : 0, ide->rom, 16384, 0);
	memcpy(ide->acmemory, ide->rom, sizeof ide->acmemory);
	aci->addrbank = ide->bank;
	return true;
}

static void rochard_add_ide_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	add_ide_standard_unit(ch, ci, rc, rochard_board, ROCHARD_IDE, true, false, 4);
}

bool buddha_init(struct autoconfig_info *aci)
{
	const struct expansionromtype *ert = get_device_expansion_rom(ROMTYPE_BUDDHA);

	if (!aci->doinit) {
		aci->autoconfigp = ert->autoconfig;
		return true;
	}
	struct ide_board *ide = getide(aci->rc);

	ide->configured = 0;
	ide->bank = &ide_bank_generic;
	ide->rom_size = 65536;
	ide->mask = 65536 - 1;

	ide->rom = xcalloc(uae_u8, ide->rom_size);
	memset(ide->rom, 0xff, ide->rom_size);
	ide->rom_mask = ide->rom_size - 1;
	load_rom_rc(aci->rc, ROMTYPE_BUDDHA, 32768, 0, ide->rom, 65536, LOADROM_EVENONLY_ODDONE | LOADROM_FILL);
	for (int i = 0; i < 16; i++) {
		uae_u8 b = ert->autoconfig[i];
		if (i == 1 && (aci->rc->device_settings & 1))
			b = 42;
		ew(ide, i * 4, b);
	}
	aci->addrbank = ide->bank;
	return true;
}

void buddha_add_ide_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	add_ide_standard_unit(ch, ci, rc, buddha_board, BUDDHA_IDE, false, false, 6);
}

void rochard_add_idescsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	if (ch < 0) {
		rochard_add_ide_unit(ch, ci, rc);
		rochard_add_scsi_unit(ch, ci, rc);
	} else {
		if (ci->controller_type < HD_CONTROLLER_TYPE_SCSI_FIRST)
			rochard_add_ide_unit(ch, ci, rc);
		else
			rochard_add_scsi_unit(ch, ci, rc);
	}
}

bool golemfast_init(struct autoconfig_info *aci)
{
	int rom_size = 32768;
	uae_u8 *rom;
	
	rom = xcalloc(uae_u8, rom_size);
	memset(rom, 0xff, rom_size);
	load_rom_rc(aci->rc, ROMTYPE_GOLEMFAST, 16384, 0, rom, 32768, 0);

	if (!aci->doinit) {
		memcpy(aci->autoconfig_raw, rom, sizeof aci->autoconfig_raw);
		xfree(rom);
		return true;
	}

	struct ide_board *ide = getide(aci->rc);

	ide->rom = rom;
	ide->configured = 0;
	ide->bank = &ide_bank_generic;
	ide->rom_size = rom_size;
	ide->mask = 65536 - 1;

	ide->rom_mask = ide->rom_size - 1;
	memcpy(ide->acmemory, ide->rom, sizeof ide->acmemory);
	aci->addrbank = ide->bank;
	return true;
}

static void golemfast_add_ide_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	add_ide_standard_unit(ch, ci, rc, golemfast_board, GOLEMFAST_IDE, false, false, 2);
}

void golemfast_add_idescsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	if (ch < 0) {
		golemfast_add_ide_unit(ch, ci, rc);
		golemfast_add_scsi_unit(ch, ci, rc);
	} else {
		if (ci->controller_type < HD_CONTROLLER_TYPE_SCSI_FIRST)
			golemfast_add_ide_unit(ch, ci, rc);
		else
			golemfast_add_scsi_unit(ch, ci, rc);
	}
}

bool dataflyerplus_init(struct autoconfig_info *aci)
{
	int rom_size = 16384;
	uae_u8 *rom;

	rom = xcalloc(uae_u8, rom_size);
	memset(rom, 0xff, rom_size);
	load_rom_rc(aci->rc, ROMTYPE_DATAFLYER, 32768, aci->rc->autoboot_disabled ? 8192 : 0, rom, 16384, LOADROM_EVENONLY_ODDONE);

	if (!aci->doinit) {
		memcpy(aci->autoconfig_raw, rom, sizeof aci->autoconfig_raw);
		xfree(rom);
		return true;
	}

	struct ide_board *ide = getide(aci->rc);

	ide->rom = rom;
	ide->configured = 0;
	ide->bank = &ide_bank_generic;
	ide->rom_size = rom_size;
	ide->mask = 65536 - 1;
	ide->keepautoconfig = false;
	ide->subtype = ((aci->rc->device_settings & 3) <= 1) ? 1 : 0; // scsi
	ide->subtype |= ((aci->rc->device_settings & 3) != 1) ? 2 : 0; // ide

	ide->rom_mask = ide->rom_size - 1;
	memcpy(ide->acmemory, ide->rom, sizeof ide->acmemory);
	aci->addrbank = ide->bank;

	return true;
}

static void dataflyerplus_add_ide_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	add_ide_standard_unit(ch, ci, rc, dataflyerplus_board, DATAFLYERPLUS_IDE, true, false, 2);
}

void dataflyerplus_add_idescsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	if (ch < 0) {
		dataflyerplus_add_ide_unit(ch, ci, rc);
		dataflyerplus_add_scsi_unit(ch, ci, rc);
	} else {
		if (ci->controller_type < HD_CONTROLLER_TYPE_SCSI_FIRST)
			dataflyerplus_add_ide_unit(ch, ci, rc);
		else
			dataflyerplus_add_scsi_unit(ch, ci, rc);
	}
}

bool ateam_init(struct autoconfig_info *aci)
{
	uae_u8 *rom;
	int rom_size = 32768;

	rom = xcalloc(uae_u8, rom_size);
	memset(rom, 0xff, rom_size);
	load_rom_rc(aci->rc, ROMTYPE_ATEAM, 16384, !aci->rc->autoboot_disabled ? 0xc000 : 0x8000, rom, 32768, LOADROM_EVENONLY_ODDONE | LOADROM_FILL);

	if (!aci->doinit) {
		memcpy(aci->autoconfig_raw, rom, sizeof aci->autoconfig_raw);
		xfree(rom);
		return true;
	}

	struct ide_board *ide = getide(aci->rc);

	ide->configured = 0;
	ide->keepautoconfig = false;
	ide->bank = &ide_bank_generic;
	ide->mask = 65536 - 1;

	ide->rom = rom;
	ide->rom_size = rom_size;
	ide->rom_mask = rom_size - 1;
	memcpy(ide->acmemory, ide->rom, sizeof ide->acmemory);

	aci->addrbank = ide->bank;
	return true;
}

void ateam_add_ide_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	add_ide_standard_unit(ch, ci, rc, ateam_board, ATEAM_IDE, true, false, 2);
}


extern void x86_xt_ide_bios(struct zfile*, struct romconfig*);
static bool x86_at_hd_init(struct autoconfig_info *aci, int type)
{
	static const int parent[] = { ROMTYPE_A1060, ROMTYPE_A2088, ROMTYPE_A2088T, ROMTYPE_A2286, ROMTYPE_A2386, 0 };
	aci->parent_romtype = parent;
	if (!aci->doinit)
		return true;

	struct ide_board *ide = getide(aci->rc);
	if (!ide)
		return false;

	ide->intena = type == 0;
	ide->configured = 1;
	ide->bank = &ide_bank_generic;

	struct zfile *f = read_device_from_romconfig(aci->rc, 0);
	if (f) {
		x86_xt_ide_bios(f, aci->rc);
		zfile_fclose(f);
	}
	return true;
}
bool x86_at_hd_init_1(struct autoconfig_info *aci)
{
	return x86_at_hd_init(aci, 0);
}
bool x86_at_hd_init_2(struct autoconfig_info *aci)
{
	return x86_at_hd_init(aci, 0);
}
bool x86_at_hd_init_xt(struct autoconfig_info *aci)
{
	return x86_at_hd_init(aci, 1);
}

void x86_add_at_hd_unit_1(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	add_ide_standard_unit(ch, ci, rc, &x86_at_ide_board[0], x86_AT_IDE + 0, false, false, 2);
}
void x86_add_at_hd_unit_2(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	add_ide_standard_unit(ch, ci, rc, &x86_at_ide_board[1], x86_AT_IDE + 1, false, false, 2);
}
void x86_add_at_hd_unit_xt(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	add_ide_standard_unit(ch, ci, rc, &x86_at_ide_board[2], x86_AT_IDE + 2, false, false, 2);
}

static int x86_ide_reg(int portnum, int *unit)
{
	if (portnum >= 0x1f0 && portnum < 0x1f8) {
		*unit = 0;
		return portnum & 7;
	}
	if (portnum == 0x3f6) {
		*unit = 0;
		return 6 | IDE_SECONDARY;
	}
	if (portnum >= 0x170 && portnum < 0x178) {
		*unit = 1;
		return portnum & 7;
	}
	if (portnum == 0x376) {
		*unit = 1;
		return 6 | IDE_SECONDARY;
	}
	if (portnum >= 0x300 && portnum < 0x310) {
		*unit = 2;
		if (portnum < 0x308)
			return portnum & 7;
		if (portnum == 0x308)
			return 8;
		if (portnum >= 0x308+6)
			return (portnum & 7) | IDE_SECONDARY;
	}
	return -1;
}

void x86_ide_hd_put(int portnum, uae_u16 v, int size)
{

	if (portnum < 0) {
		for (int i = 0; i < MAX_DUPLICATE_EXPANSION_BOARDS; i++) {
			struct ide_board *board = x86_at_ide_board[i];
			if (board)
				ide_reset_device(board->ide[0]);
		}
		return;
	}
	int unit;
	int regnum = x86_ide_reg(portnum, &unit);
	if (regnum >= 0) {
		struct ide_board *board = x86_at_ide_board[unit];
		if (board) {
#if 0
			if (regnum == 0 || regnum == 8)
				write_log(_T("WRITE %04x = %04x %d\n"), portnum, v, size);
#endif
			if (size == 0) {
				if (get_ide_is_8bit(board)) {
					v = get_ide_reg_8bitdata(board, regnum);
				} else {
					if (regnum == 8) {
						board->data_latch = v;
					} else if (regnum == 0) {
						v <<= 8;
						v |= board->data_latch;
						put_ide_reg(board, regnum, v);
					} else {
						put_ide_reg_8bitdata(board, regnum, v);
					}
				}
			} else {
				put_ide_reg(board, regnum, (v >> 8) | (v << 8));
			}
		}
	}
}
uae_u16 x86_ide_hd_get(int portnum, int size)
{
	uae_u16 v = 0;
	int unit;
	int regnum = x86_ide_reg(portnum, &unit);
	if (regnum >= 0) {
		struct ide_board *board = x86_at_ide_board[unit];
		if (board) {

			if (size == 0) {
				if (get_ide_is_8bit(board)) {
					v = get_ide_reg_8bitdata(board, regnum);
				} else {
					if (regnum == 0) {
						board->data_latch = get_ide_reg(board, regnum);
						v = board->data_latch >> 8;
					} else if (regnum == 8) {
						v = board->data_latch;
					} else {
						v = get_ide_reg_8bitdata(board, regnum & 7);
					}
				}
			} else {
				v = get_ide_reg(board, regnum);
				v = (v >> 8) | (v << 8);
			}
#if 0
			if (regnum == 0 || regnum == 8)
				write_log(_T("READ %04x = %04x %d\n"), portnum, v, size);
#endif
		}
	}
	return v;
}
