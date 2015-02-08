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

#define DEBUG_IDE 0
#define DEBUG_IDE_GVP 0
#define DEBUG_IDE_ALF 0
#define DEBUG_IDE_APOLLO 0
#define DEBUG_IDE_MASOBOSHI 0

#define GVP_IDE 0 // GVP A3001
#define ALF_IDE 1
#define APOLLO_IDE 3
#define MASOBOSHI_IDE 5
#define TOTAL_IDE 7

#define ALF_ROM_OFFSET 0x0100
#define GVP_IDE_ROM_OFFSET 0x8000
#define APOLLO_ROM_OFFSET 0x8000
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

static struct ide_board gvp_ide_rom_board, gvp_ide_controller_board;
static struct ide_board alf_board[2];
static struct ide_board apollo_board[2];
static struct ide_board masoboshi_board[2];
static struct ide_hdf *idecontroller_drive[TOTAL_IDE * 2];
static struct ide_thread_state idecontroller_its;

static struct ide_board *ide_boards[] =
{
	&gvp_ide_rom_board,
	&alf_board[0],
	&alf_board[1],
	&apollo_board[0],
	&apollo_board[1],
	&masoboshi_board[0],
	&masoboshi_board[1],
	NULL
};

static void init_ide(struct ide_board *board, int ide_num, bool byteswap)
{
	struct ide_hdf **idetable = &idecontroller_drive[ide_num * 2];
	alloc_ide_mem (idetable, 2, &idecontroller_its);
	board->ide = idetable[0];
	idetable[0]->board = board;
	idetable[1]->board = board;
	idetable[0]->byteswap = byteswap;
	idetable[1]->byteswap = byteswap;
	ide_initialize(idecontroller_drive, ide_num);
	idecontroller_its.idetable = idecontroller_drive;
	idecontroller_its.idetotal = TOTAL_IDE * 2;
	start_ide_thread(&idecontroller_its);
}

static bool ide_irq_check(struct ide_board *board)
{
	if (!board->configured)
		return false;
	bool irq = ide_interrupt_check(board->ide);
	board->irq = irq;
	return irq;
}

static bool ide_rethink(struct ide_board *board)
{
	bool irq = false;
	if (board->configured) {
		if (board->intena && ide_irq_check(board)) {
			irq = true;
		}
	}
	return irq;
}

void idecontroller_rethink(void)
{
	bool irq = false;
	for (int i = 0; ide_boards[i]; i++) {
		irq |= ide_rethink(ide_boards[i]);
	}
	if (irq && !(intreq & 0x0008)) {
		INTREQ_0(0x8000 | 0x0008);
	}
}

void idecontroller_hsync(void)
{
	for (int i = 0; ide_boards[i]; i++) {
		if (ide_irq_check(ide_boards[i])) {
			idecontroller_rethink();
		}
	}
}

void idecontroller_free_units(void)
{
	for (int i = 0; i < TOTAL_IDE * 2; i++) {
		remove_ide_unit(idecontroller_drive, i);
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
}

static bool is_gvp2_intreq(uaecptr addr)
{
	if (currprefs.cpuboard_type == BOARD_A3001_II && (addr & 0x440) == 0x440)
		return true;
	return false;
}
static bool is_gvp1_intreq(uaecptr addr)
{
	if (currprefs.cpuboard_type == BOARD_A3001_I && (addr & 0x440) == 0x40)
		return true;
	return false;
}

static int get_gvp_reg(uaecptr addr, struct ide_board *board, struct ide_hdf **idep)
{
	struct ide_hdf *ide;
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

	ide = board->ide;
	if (idecontroller_drive[GVP_IDE * 2]->ide_drv)
		ide = ide->pair;
	*idep = ide;
	return reg;
}

static int get_apollo_reg(uaecptr addr, struct ide_board *board, struct ide_hdf **idep)
{
	struct ide_hdf *ide;
	if (addr & 0x4000)
		return -1;
	int reg = addr & 0x1fff;
	reg >>= 10;
	if (addr & 0x2000)
		reg |= IDE_SECONDARY;
	ide = board->ide;
	if (idecontroller_drive[APOLLO_IDE * 2]->ide_drv)
		ide = ide->pair;
	*idep = ide;
	if (reg != 0 && !(addr & 1))
		reg = -1;
	write_log(_T("APOLLO %04x = %d\n"), addr, reg);
	return reg;
}

static int get_alf_reg(uaecptr addr, struct ide_board *board, struct ide_hdf **idep)
{
	struct ide_hdf *ide;
	if (addr & 0x8000)
		return -1;
	ide = board->ide;
	if (idecontroller_drive[ALF_IDE * 2]->ide_drv)
		ide = ide->pair;
	*idep = ide;
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

static int get_masoboshi_reg(uaecptr addr, struct ide_board *board, struct ide_hdf **idep)
{
	int reg;
	struct ide_hdf *ide;
	if (addr < 0xfc00)
		return -1;
	ide = board->ide;
	if (idecontroller_drive[MASOBOSHI_IDE * 2]->ide_drv)
		ide = ide->pair;
	*idep = ide;
	reg = 7 - ((addr >> 6) & 7);
	if (addr < 0xfe00)
		reg |= IDE_SECONDARY;
	return reg;
}

static uae_u32 ide_read_byte(struct ide_board *board, uaecptr addr)
{
	uae_u8 v = 0xff;
	addr &= 0xffff;

#ifdef JIT
	special_mem |= S_READ;
#endif

	addr &= board->mask;

#if DEBUG_IDE
	write_log(_T("IDE IO BYTE READ %08x %08x\n"), addr, M68K_GETPC);
#endif
	
	if (addr < 0x40)
		return board->acmemory[addr];
	if (board->type == ALF_IDE) {

		if (addr < 0x1100 || (addr & 1)) {
			if (board->rom)
				v = board->rom[addr & board->rom_mask];
			return v;
		}
		struct ide_hdf *ide;
		int regnum = get_alf_reg(addr, board, &ide);
		if (regnum >= 0) {
			v = ide_read_reg(ide, regnum);
		}
#if DEBUG_IDE_ALF
		write_log(_T("ALF GET %08x %02x %d %08x\n"), addr, v, regnum, M68K_GETPC);
#endif

	} else if (board->type == MASOBOSHI_IDE) {
		int regnum = -1;
		bool rom = false;
		if (addr >= MASOBOSHI_ROM_OFFSET && addr < MASOBOSHI_ROM_OFFSET_END) {
			if (board->rom) {
				v = board->rom[addr & board->rom_mask];
				rom = true;
			}
		} else if (addr >= 0xf000 && addr <= 0xf007) {
			v = masoboshi_ncr9x_scsi_get(addr, board == &masoboshi_board[0] ? 0 : 1);
		} else if (addr == 0xf040) {
			v = 0;
			if (ide_drq_check(board->ide))
				v |= 2;
			if (ide_interrupt_check(board->ide)) {
				v |= 1;
			}
			v |= masoboshi_ncr9x_scsi_get(addr, board == &masoboshi_board[0] ? 0 : 1);
		} else if (addr == 0xf047) {
			v = board->state;
		} else {
			struct ide_hdf *ide;
			regnum = get_masoboshi_reg(addr, board, &ide);
			if (regnum >= 0) {
				v = ide_read_reg(ide, regnum);
			} else if (addr >= MASOBOSHI_SCSI_OFFSET && addr < MASOBOSHI_SCSI_OFFSET_END) {
				v = masoboshi_ncr9x_scsi_get(addr, board == &masoboshi_board[0] ? 0 : 1);
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
				v = apollo_scsi_bget(addr);
			} else if (addr < 0x4000) {
				struct ide_hdf *ide;
				int regnum = get_apollo_reg(addr, board, &ide);
				if (regnum >= 0) {
					v = ide_read_reg(ide, regnum);
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
			if (board == &gvp_ide_rom_board && currprefs.cpuboard_type == BOARD_A3001_II) {
				if (addr == 0x42) {
					v = 0xff;
				}
#if DEBUG_IDE_GVP
				write_log(_T("GVP BOOT GET %08x %02x %08x\n"), addr, v, M68K_GETPC);
#endif
			} else {
				struct ide_hdf *ide;
				int regnum = get_gvp_reg(addr, board, &ide);
#if DEBUG_IDE_GVP
				write_log(_T("GVP IDE GET %08x %02x %d %08x\n"), addr, v, regnum, M68K_GETPC);
#endif
				if (regnum >= 0) {
					v = ide_read_reg(ide, regnum);
				} else if (is_gvp2_intreq(addr)) {
					v = board->irq ? 0x40 : 0x00;
#if DEBUG_IDE_GVP
					write_log(_T("GVP IRQ %02x\n"), v);
#endif
					ide_irq_check(board);
				} else if (is_gvp1_intreq(addr)) {
					v = gvp_ide_controller_board.irq ? 0x80 : 0x00;
#if DEBUG_IDE_GVP
					write_log(_T("GVP IRQ %02x\n"), v);
#endif
					ide_irq_check(board);
				}
			}
		} else {
			v = 0xff;
		}
	}
	return v;
}

static uae_u32 ide_read_word(struct ide_board *board, uaecptr addr)
{
	uae_u32 v = 0xffff;

#ifdef JIT
	special_mem |= S_READ;
#endif

	addr &= board->mask;

	if (board->type == APOLLO_IDE) {

		if (addr >= APOLLO_ROM_OFFSET) {
			if (board->rom) {
				v = board->rom[(addr + 0 - APOLLO_ROM_OFFSET) & board->rom_mask];
				v <<= 8;
				v |= board->rom[(addr + 1 - APOLLO_ROM_OFFSET) & board->rom_mask];
			}
		}
	}


	if (board->configured) {

		if (board->type == ALF_IDE) {

			struct ide_hdf *ide;
			int regnum = get_alf_reg(addr, board, &ide);
			if (regnum == IDE_DATA) {
				v = ide_get_data(ide);
			} else {
				v = 0;
				if (addr == 0x4000 && board->intena)
					v = board->irq ? 0x8000 : 0x0000;
#if DEBUG_IDE_ALF
				write_log(_T("ALF IO WORD READ %08x %08x\n"), addr, M68K_GETPC);
#endif
			}

	} else if (board->type == MASOBOSHI_IDE) {

		if (addr >= MASOBOSHI_ROM_OFFSET && addr < MASOBOSHI_ROM_OFFSET_END) {
			if (board->rom) {
				v = board->rom[addr & board->rom_mask] << 8;
				v |= board->rom[(addr + 1) & board->rom_mask];
			}
		} else {
			struct ide_hdf *ide;
			int regnum = get_masoboshi_reg(addr, board, &ide);
			if (regnum == IDE_DATA) {
				v = ide_get_data(ide);
			} else {
				v = ide_read_byte(board, addr) << 8;
				v |= ide_read_byte(board, addr + 1);
			}
		}

	} else if (board->type == APOLLO_IDE) {

			if ((addr & 0xc000) == 0x4000) {
				v = apollo_scsi_bget(addr);
				v <<= 8;
				v |= apollo_scsi_bget(addr + 1);
			} else if (addr < 0x4000) {
				struct ide_hdf *ide;
				int regnum = get_apollo_reg(addr, board, &ide);
				if (regnum == IDE_DATA) {
					v = ide_get_data(ide);
				} else {
					v = 0;
				}
			}

		} else if (board->type == GVP_IDE) {

			if (board == &gvp_ide_controller_board || currprefs.cpuboard_type == BOARD_A3001_I) {
				if (addr < 0x60) {
					if (is_gvp1_intreq(addr))
						v = gvp_ide_controller_board.irq ? 0x8000 : 0x0000;
					else if (addr == 0x40) {
						if (currprefs.cpuboard_type == BOARD_A3001_II)
							v = board->intena ? 8 : 0;
					}
#if DEBUG_IDE_GVP
					write_log(_T("GVP IO WORD READ %08x %08x\n"), addr, M68K_GETPC);
#endif
				} else {
					struct ide_hdf *ide;
					int regnum = get_gvp_reg(addr, board, &ide);
					if (regnum == IDE_DATA) {
						v = ide_get_data(ide);
#if DEBUG_IDE_GVP > 2
						write_log(_T("IDE WORD READ %04x\n"), v);
#endif
					} else {
						v = ide_read_byte(board, addr) << 8;
						v |= ide_read_byte(board, addr + 1);
					}
				}	
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
	addr &= board->mask;

#ifdef JIT
	special_mem |= S_WRITE;
#endif

#if DEBUG_IDE
	write_log(_T("IDE IO BYTE WRITE %08x=%02x %08x\n"), addr, v, M68K_GETPC);
#endif

	if (!board->configured) {
		addrbank *ab = board->bank;
		if (addr == 0x48) {
			map_banks_z2(ab, v, (board->mask + 1) >> 16);
			board->configured = 1;
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
		if (board->type == ALF_IDE) {
			struct ide_hdf *ide;
			int regnum = get_alf_reg(addr, board, &ide);
			if (regnum >= 0)
				ide_write_reg(ide, regnum, v);
#if DEBUG_IDE_ALF
			write_log(_T("ALF PUT %08x %02x %d %08x\n"), addr, v, regnum, M68K_GETPC);
#endif
	} else if (board->type == MASOBOSHI_IDE) {

#if DEBUG_IDE_MASOBOSHI
			write_log(_T("MASOBOSHI IO BYTE PUT %08x %02x %08x\n"), addr, v, M68K_GETPC);
#endif
			struct ide_hdf *ide;
			int regnum = get_masoboshi_reg(addr, board, &ide);
			if (regnum >= 0) {
				ide_write_reg(ide, regnum, v);
			} else if (addr >= MASOBOSHI_SCSI_OFFSET && addr < MASOBOSHI_SCSI_OFFSET_END) {
				masoboshi_ncr9x_scsi_put(addr, v, board == &masoboshi_board[0] ? 0 : 1);
			} else if ((addr >= 0xf000 && addr <= 0xf007) || (addr >= 0xf04a && addr <= 0xf04f)) {
				masoboshi_ncr9x_scsi_put(addr, v, board == &masoboshi_board[0] ? 0 : 1);
			} else if (addr >= 0xf040 && addr < 0xf048) {
				masoboshi_ncr9x_scsi_put(addr, v, board == &masoboshi_board[0] ? 0 : 1);
				if (addr == 0xf047) {
					board->state = v;
					board->intena = (v & 8) != 0;
				}
				if (addr == 0xf040) {
					board->irq = false;
				}
				write_log(_T("MASOBOSHI STATUS BYTE PUT %08x %02x %08x\n"), addr, v, M68K_GETPC);
			}

	} else if (board->type == APOLLO_IDE) {

			if ((addr & 0xc000) == 0x4000) {
				apollo_scsi_bput(addr, v);
			} else if (addr < 0x4000) {
				struct ide_hdf *ide;
				int regnum = get_apollo_reg(addr, board, &ide);
				if (regnum >= 0) {
					ide_write_reg(ide, regnum, v);
				}
			}

	} else if (board->type == GVP_IDE) {
			if (board == &gvp_ide_rom_board && currprefs.cpuboard_type == BOARD_A3001_II) {
#if DEBUG_IDE_GVP
				write_log(_T("GVP BOOT PUT %08x %02x %08x\n"), addr, v, M68K_GETPC);
#endif
			} else {
				struct ide_hdf *ide;
				int regnum = get_gvp_reg(addr, board, &ide);
#if DEBUG_IDE_GVP
				write_log(_T("GVP IDE PUT %08x %02x %d %08x\n"), addr, v, regnum, M68K_GETPC);
#endif
				if (regnum >= 0)
					ide_write_reg(ide, regnum, v);
			}
		}
	}
}

static void ide_write_word(struct ide_board *board, uaecptr addr, uae_u16 v)
{
	addr &= board->mask;

#ifdef JIT
	special_mem |= S_WRITE;
#endif

#if DEBUG_IDE
	write_log(_T("IDE IO WORD WRITE %08x=%04x %08x\n"), addr, v, M68K_GETPC);
#endif
	if (board->configured) {
		if (board->type == ALF_IDE) {

			struct ide_hdf *ide;
			int regnum = get_alf_reg(addr, board, &ide);
			if (regnum == IDE_DATA) {
				ide_put_data(ide, v);
			} else {
#if DEBUG_IDE_ALF
				write_log(_T("ALF IO WORD WRITE %08x %04x %08x\n"), addr, v, M68K_GETPC);
#endif
			}

		} else if (board->type == MASOBOSHI_IDE) {

			struct ide_hdf *ide;
			int regnum = get_masoboshi_reg(addr, board, &ide);
			if (regnum == IDE_DATA) {
				ide_put_data(ide, v);
			} else {
				ide_write_byte(board, addr, v >> 8);
				ide_write_byte(board, addr + 1, v);
			}
#if DEBUG_IDE_MASOBOSHI
			write_log(_T("MASOBOSHI IO WORD WRITE %08x %04x %08x\n"), addr, v, M68K_GETPC);
#endif
	
		} else if (board->type == APOLLO_IDE) {

			if ((addr & 0xc000) == 0x4000) {
				apollo_scsi_bput(addr, v >> 8);
				apollo_scsi_bput(addr + 1, v);
			} else if (addr < 0x4000) {
				struct ide_hdf *ide;
				int regnum = get_apollo_reg(addr, board, &ide);
				if (regnum == IDE_DATA) {
					ide_put_data(ide, v);
				}
			}

		} else if (board->type == GVP_IDE) {

			if (board == &gvp_ide_controller_board || currprefs.cpuboard_type == BOARD_A3001_I) {
				if (addr < 0x60) {
#if DEBUG_IDE_GVP
					write_log(_T("GVP IO WORD WRITE %08x %04x %08x\n"), addr, v, M68K_GETPC);
#endif
					if (addr == 0x40 && currprefs.cpuboard_type == BOARD_A3001_II)
						board->intena = (v & 8) != 0;
				} else {
					struct ide_hdf *ide;
					int regnum = get_gvp_reg(addr, board, &ide);
					if (regnum == IDE_DATA) {
						ide_put_data(ide, v);
#if DEBUG_IDE_GVP > 2
						write_log(_T("IDE WORD WRITE %04x\n"), v);
#endif
					} else {
						ide_write_byte(board, addr, v >> 8);
						ide_write_byte(board, addr + 1, v & 0xff);
					}
				}
			}
		}
	}
}

IDE_MEMORY_FUNCTIONS(ide_controller_gvp, ide, gvp_ide_controller_board);

addrbank gvp_ide_controller_bank = {
	ide_controller_gvp_lget, ide_controller_gvp_wget, ide_controller_gvp_bget,
	ide_controller_gvp_lput, ide_controller_gvp_wput, ide_controller_gvp_bput,
	default_xlate, default_check, NULL, NULL, _T("GVP IDE"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO | ABFLAG_SAFE
};

IDE_MEMORY_FUNCTIONS(ide_rom_gvp, ide, gvp_ide_rom_board);

addrbank gvp_ide_rom_bank = {
	ide_rom_gvp_lget, ide_rom_gvp_wget, ide_rom_gvp_bget,
	ide_rom_gvp_lput, ide_rom_gvp_wput, ide_rom_gvp_bput,
	default_xlate, default_check, NULL, NULL, _T("GVP BOOT"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO | ABFLAG_SAFE
};

IDE_MEMORY_FUNCTIONS(alf, ide, alf_board[0]);
IDE_MEMORY_FUNCTIONS(alf2, ide, alf_board[1]);

addrbank alf_bank = {
	alf_lget, alf_wget, alf_bget,
	alf_lput, alf_wput, alf_bput,
	default_xlate, default_check, NULL, NULL, _T("ALF"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO | ABFLAG_SAFE
};
addrbank alf_bank2 = {
	alf2_lget, alf2_wget, alf2_bget,
	alf2_lput, alf2_wput, alf2_bput,
	default_xlate, default_check, NULL, NULL, _T("ALF #2"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO | ABFLAG_SAFE
};

IDE_MEMORY_FUNCTIONS(apollo_ide, ide, apollo_board[0]);

addrbank apollo_bank = {
	apollo_ide_lget, apollo_ide_wget, apollo_ide_bget,
	apollo_ide_lput, apollo_ide_wput, apollo_ide_bput,
	default_xlate, default_check, NULL, NULL, _T("Apollo"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO | ABFLAG_SAFE
};

IDE_MEMORY_FUNCTIONS(masoboshi_ide, ide, masoboshi_board[0]);

addrbank masoboshi_bank = {
	masoboshi_ide_lget, masoboshi_ide_wget, masoboshi_ide_bget,
	masoboshi_ide_lput, masoboshi_ide_wput, masoboshi_ide_bput,
	default_xlate, default_check, NULL, NULL, _T("Masoboshi"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO | ABFLAG_SAFE
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

addrbank *gvp_ide_rom_autoconfig_init(int devnum)
{
	struct ide_board *ide = &gvp_ide_rom_board;
	int roms[2];
	struct romlist *rl;
	const uae_u8 *autoconfig;

	if (currprefs.cpuboard_type == BOARD_A3001_I) {
		ide->bank = &gvp_ide_rom_bank;
		autoconfig = gvp_ide1_controller_autoconfig;
		init_ide(ide, GVP_IDE, true);
		ide->rom_size = 8192;
		gvp_ide_controller_board.intena = true;
		gvp_ide_controller_board.configured = -1;
		roms[0] = 114;
		roms[1] = -1;
	} else {
		ide->bank = &gvp_ide_rom_bank;
		autoconfig = gvp_ide2_rom_autoconfig;
		ide->rom_size = 16384;
		roms[0] = -1;
	}
	ide->mask = 65536 - 1;
	ide->type = GVP_IDE;
	ide->configured = 0;
	memset(ide->acmemory, 0xff, sizeof ide->acmemory);


	ide->rom = xcalloc(uae_u8, ide->rom_size);
	memset(ide->rom, 0xff, ide->rom_size);
	ide->rom_mask = ide->rom_size - 1;
	int index;
	struct boardromconfig *brc = get_device_rom(&currprefs, ROMTYPE_CPUBOARD, &index);
	struct zfile *z = NULL;
	if (brc) {
		const TCHAR *romname = brc->roms[index].romfile;
		z = read_rom_name(romname);
		if (!z) {
			rl = getromlistbyids(roms, romname);
			if (rl) {
				z = read_rom(rl->rd);
			}
		}
	}
	if (z) {
		for (int i = 0; i < 16; i++) {
			uae_u8 b = autoconfig[i];
			ew(ide, i * 4, b);
		}
		write_log(_T("GVP IDE BOOT ROM '%s'\n"), zfile_getname(z));
		int size = zfile_fread(ide->rom, 1, ide->rom_size, z);
		zfile_fclose(z);
	} else {
		romwarning(roms);
	}
	return ide->bank;
}

addrbank *gvp_ide_controller_autoconfig_init(int devnum)
{
	struct ide_board *ide = &gvp_ide_controller_board;

	init_ide(ide, GVP_IDE, true);
	ide->configured = 0;
	ide->bank = &gvp_ide_controller_bank;
	memset(ide->acmemory, 0xff, sizeof ide->acmemory);
	for (int i = 0; i < 16; i++) {
		uae_u8 b = gvp_ide2_controller_autoconfig[i];
		ew(ide, i * 4, b);
	}
	return ide->bank;
}

int gvp_add_ide_unit(int ch, struct uaedev_config_info *ci)
{
	struct ide_hdf *ide;

	ide = add_ide_unit (&idecontroller_drive[GVP_IDE], 2, ch, ci);
	if (ide == NULL)
		return 0;
	return 1;
}

static const uae_u8 alf_autoconfig[16] = { 0xd1, 6, 0x00, 0x00, 0x08, 0x2c, 0x00, 0x00, 0x00, 0x00, ALF_ROM_OFFSET >> 8, ALF_ROM_OFFSET & 0xff };
static const uae_u8 alfplus_autoconfig[16] = { 0xd1, 38, 0x00, 0x00, 0x08, 0x2c, 0x00, 0x00, 0x00, 0x00, ALF_ROM_OFFSET >> 8, ALF_ROM_OFFSET & 0xff };

addrbank *alf_init(int devnum)
{
	struct ide_board *ide = &alf_board[devnum];
	int roms[2];
	bool alfplus = cfgfile_board_enabled(&currprefs, ROMTYPE_ALFAPLUS);

	if (devnum > 0 && !ide->enabled)
		return &expamem_null;

	roms[0] = alfplus ? 118 : 117;
	roms[1] = -1;

	init_ide(ide, ALF_IDE + devnum, false);
	ide->configured = 0;
	ide->bank = &alf_bank;
	ide->type = ALF_IDE + devnum;
	ide->rom_size = 32768 * 6;
	ide->userdata = alfplus;
	ide->intena = alfplus;
	ide->mask = 65536 - 1;

	memset(ide->acmemory, 0xff, sizeof ide->acmemory);

	ide->rom = xcalloc(uae_u8, ide->rom_size);
	memset(ide->rom, 0xff, ide->rom_size);
	ide->rom_mask = ide->rom_size - 1;

	struct zfile *z = read_device_rom(&currprefs, devnum, alfplus ? ROMTYPE_ALFAPLUS : ROMTYPE_ALFA, roms);
	for (int i = 0; i < 16; i++) {
		uae_u8 b = alfplus ? alfplus_autoconfig[i] : alf_autoconfig[i];
		ew(ide, i * 4, b);
	}
	if (z) {
		write_log(_T("ALF BOOT ROM '%s'\n"), zfile_getname(z));
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
	} else {
		romwarning(roms);
	}
	return ide->bank;
}

int alf_add_ide_unit(int ch, struct uaedev_config_info *ci)
{
	struct ide_hdf *ide;

	ide = add_ide_unit (&idecontroller_drive[(ALF_IDE + ci->controller_type_unit) * 2], 2, ch, ci);
	if (ide == NULL)
		return 0;
	return 1;
}

// prod 0x22 = IDE + SCSI
// prod 0x23 = SCSI only
// prod 0x33 = IDE only

const uae_u8 apollo_autoconfig[16] = { 0xd2, 0x23, 0x00, 0x00, 0x22, 0x22, 0x00, 0x00, 0x00, 0x00, APOLLO_ROM_OFFSET >> 8, APOLLO_ROM_OFFSET & 0xff };
const uae_u8 apollo_autoconfig_cpuboard[16] = { 0xd2, 0x23, 0x00, 0x00, 0x22, 0x22, 0x00, 0x00, 0x00, 0x00, APOLLO_ROM_OFFSET >> 8, APOLLO_ROM_OFFSET & 0xff };
const uae_u8 apollo_autoconfig_cpuboard_060[16] = { 0xd2, 0x23, 0x00, 0x00, 0x22, 0x22, 0x00, 0x00, 0x00, 0x02, APOLLO_ROM_OFFSET >> 8, APOLLO_ROM_OFFSET & 0xff };

addrbank *apollo_init(int devnum)
{
	struct ide_board *ide;
	int roms[2];
	const uae_u8 *autoconfig;
	bool cpuboard = false;
	struct zfile *z = NULL;

	if (devnum < 0) {
		cpuboard = true;
		devnum = 0;
	}

	ide = &apollo_board[devnum];
	if (devnum > 0 && !ide->enabled)
		return &expamem_null;

	roms[0] = -1;
	init_ide(ide, APOLLO_IDE + devnum, false);
	ide->configured = 0;
	ide->bank = &apollo_bank;
	ide->type = APOLLO_IDE + devnum;
	ide->rom_size = 32768;
	ide->mask = 131072 - 1;

	memset(ide->acmemory, 0xff, sizeof ide->acmemory);

	ide->rom = xcalloc(uae_u8, ide->rom_size);
	memset(ide->rom, 0xff, ide->rom_size);
	ide->rom_mask = ide->rom_size - 1;
	autoconfig = apollo_autoconfig;
	if (cpuboard) {
		if (currprefs.cpu_model == 68060)
			autoconfig = apollo_autoconfig_cpuboard_060;
		else
			autoconfig = apollo_autoconfig_cpuboard;
		z = read_device_rom(&currprefs, devnum, ROMTYPE_CPUBOARD, roms);
	} else {
		z = read_device_rom(&currprefs, devnum, ROMTYPE_APOLLO, roms);
	}
	for (int i = 0; i < 16; i++) {
		uae_u8 b = autoconfig[i];
		ew(ide, i * 4, b);
	}
	if (z) {
		int len = zfile_size(z);
		write_log(_T("Apollo BOOT ROM '%s' %d\n"), zfile_getname(z), len);
		// skip 68060 $f0 ROM block
		if (len >= 65536)
			zfile_fseek(z, 32768, SEEK_SET);
		for (int i = 0; i < 32768; i++) {
			uae_u8 b;
			zfile_fread(&b, 1, 1, z);
			ide->rom[i] = b;
		}
		zfile_fclose(z);
	} else {
		romwarning(roms);
	}
	return ide->bank;
}
addrbank *apollo_init_cpu(int devnum)
{
	return apollo_init(-1);
}

int apollo_add_ide_unit(int ch, struct uaedev_config_info *ci)
{
	struct ide_hdf *ide;

	ide = add_ide_unit (&idecontroller_drive[(APOLLO_IDE + ci->controller_type_unit) * 2], 2, ch, ci);
	if (ide == NULL)
		return 0;
	return 1;
}

static const uae_u8 masoboshi_fake_no_rom[] = { 0xcf, 0x1f, 0xff, 0xbf, 0xbf, 0xff, 0xff, 0xff, 0xff, 0x7f, 0x9f, 0x2f };

addrbank *masoboshi_init(int devnum)
{
	struct ide_board *ide;
	int roms[2];

	ide = &masoboshi_board[devnum];
	if (devnum > 0 && !ide->enabled)
		return &expamem_null;

	roms[0] = 120;
	roms[1] = -1;
	init_ide(ide, MASOBOSHI_IDE + devnum, true);
	ide->configured = 0;
	ide->bank = &masoboshi_bank;
	ide->type = MASOBOSHI_IDE + devnum;
	ide->rom_size = 65536;
	ide->mask = 65536 - 1;

	ide->rom = xcalloc(uae_u8, ide->rom_size);
	memset(ide->rom, 0xff, ide->rom_size);
	memset(ide->acmemory, 0xff, sizeof ide->acmemory);
	ide->rom_mask = ide->rom_size - 1;
	if (is_device_rom(&currprefs, devnum, ROMTYPE_MASOBOSHI)) {
		struct zfile *z = read_device_rom(&currprefs, devnum, ROMTYPE_MASOBOSHI, roms);
		if (z) {
			int len = zfile_size(z);
			write_log(_T("Masoboshi BOOT ROM '%s' %d\n"), zfile_getname(z), len);
			for (int i = 0; i < 32768; i++) {
				uae_u8 b;
				zfile_fread(&b, 1, 1, z);
				ide->rom[i * 2 + 0] = b;
				ide->rom[i * 2 + 1] = 0xff;
			}
			zfile_fclose(z);
			memcpy(ide->acmemory, ide->rom, sizeof ide->acmemory);
		} else {
			romwarning(roms);
		}
	} else {
		for (int i = 0; i < sizeof masoboshi_fake_no_rom; i++) {
			ide->acmemory[i * 2 + 0] =  masoboshi_fake_no_rom[i];
		}
	}
	// init SCSI part
	ncr_masoboshi_autoconfig_init(devnum);
	return ide->bank;
}

int masoboshi_add_ide_unit(int ch, struct uaedev_config_info *ci)
{
	struct ide_hdf *ide;

	ide = add_ide_unit (&idecontroller_drive[(MASOBOSHI_IDE + ci->controller_type_unit) * 2], 2, ch, ci);
	if (ide == NULL)
		return 0;
	return 1;
}
