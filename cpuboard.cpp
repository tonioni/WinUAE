/*
* UAE - The Un*x Amiga Emulator
*
* Misc accelerator board special features
* Blizzard 1230 IV, 1240/1260, 2060
*
* Copyright 2014 Toni Wilen
*
*/

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "zfile.h"
#include "rommgr.h"
#include "autoconf.h"
#include "cpuboard.h"
#include "custom.h"
#include "newcpu.h"
#include "ncr_scsi.h"
#include "debug.h"
#include "flashrom.h"
#include "uae.h"

#define F0_WAITSTATES (2 * CYCLE_UNIT)

// CS MK3
#define CYBERSTORM_MAPROM_BASE 0xfff00000
#define CSIII_NCR			0xf40000
#define CSIII_BASE			0xf60000
#define CSIII_REG_RESET		0x00 // 0x00
#define CSIII_REG_IRQ		0x01 // 0x08
#define CSIII_REG_WAITSTATE	0x02 // 0x10
#define CSIII_REG_SHADOW	0x03 // 0x18
#define CSIII_REG_LOCK		0x04 // 0x20
#define CSIII_REG_INT		0x05 // 0x28
#define CSIII_IPL_EMU		0x06 // 0x30
#define CSIII_INT_LVL		0x07 // 0x38
#define BPPC_MAGIC			0x13

/* bit definitions */
#define	P5_SET_CLEAR	0x80
/* REQ_RESET */
#define	P5_PPC_RESET	0x10
#define	P5_M68K_RESET	0x08
#define	P5_AMIGA_RESET	0x04
#define	P5_AUX_RESET	0x02
#define	P5_SCSI_RESET	0x01
/* REG_WAITSTATE */
#define	P5_PPC_WRITE	0x08
#define	P5_PPC_READ	0x04
#define	P5_M68K_WRITE	0x02
#define	P5_M68K_READ	0x01
/* REG_SHADOW */
#define	P5_SELF_RESET	0x40
#define	P5_SHADOW	0x01
/* REG_LOCK */
#define	P5_MAGIC1	0x60
#define	P5_MAGIC2	0x50
#define	P5_MAGIC3	0x30
#define	P5_MAGIC4	0x70
/* REG_INT */
#define	P5_ENABLE_IPL	0x02
#define	P5_INT_MASTER	0x01
/* IPL_EMU */
#define	P5_DISABLE_INT	0x40
#define	P5_M68K_IPL2	0x20
#define	P5_M68K_IPL1	0x10
#define	P5_M68K_IPL0	0x08
#define	P5_PPC_IPL2	0x04
#define	P5_PPC_IPL1	0x02
#define	P5_PPC_IPL0	0x01
#define P5_IPL_MASK	0x07
/* INT_LVL */
#define	P5_LVL7		0x40
#define	P5_LVL6		0x20
#define	P5_LVL5		0x10
#define	P5_LVL4		0x08
#define	P5_LVL3		0x04
#define	P5_LVL2		0x02
#define	P5_LVL1		0x01

#define CS_RAM_BASE 0x0c000000

#define BLIZZARD_RAM_BASE 0x68000000
#define BLIZZARD_RAM_ALIAS_BASE 0x48000000
#define BLIZZARD_MAPROM_BASE 0x4ff80000
#define BLIZZARD_MAPROM_ENABLE 0x80ffff00
#define BLIZZARD_BOARD_DISABLE 0x80fa0000

static int cpuboard_size = -1, cpuboard2_size = -1;
static int configured;
static int blizzard_jit;
static int maprom_state;
static uae_u32 maprom_base;
static int delayed_rom_protect;
static int f0rom_size, earom_size;
static uae_u8 io_reg[64];
static void *flashrom;
static struct zfile *flashrom_file;
static int flash_unlocked;

static bool is_blizzard(void)
{
	return currprefs.cpuboard_type == BOARD_BLIZZARD_1230_IV || currprefs.cpuboard_type == BOARD_BLIZZARD_1260 || currprefs.cpuboard_type == BOARD_BLIZZARD_2060; 
}
static bool is_cs(void)
{
	return currprefs.cpuboard_type == BOARD_CSMK3 || currprefs.cpuboard_type == BOARD_CSPPC;
}
static bool is_blizzardppc(void)
{
	return currprefs.cpuboard_type == BOARD_BLIZZARDPPC;
}

DECLARE_MEMORY_FUNCTIONS(blizzardram);

static addrbank blizzardram_bank = {
	blizzardram_lget, blizzardram_wget, blizzardram_bget,
	blizzardram_lput, blizzardram_wput, blizzardram_bput,
	blizzardram_xlate, blizzardram_check, NULL, _T("CPUBoard RAM"),
	blizzardram_lget, blizzardram_wget, ABFLAG_RAM
};

MEMORY_FUNCTIONS(blizzardram);

DECLARE_MEMORY_FUNCTIONS(blizzardram_nojit);

static addrbank blizzardram_nojit_bank = {
	blizzardram_nojit_lget, blizzardram_nojit_wget, blizzardram_nojit_bget,
	blizzardram_nojit_lput, blizzardram_nojit_wput, blizzardram_nojit_bput,
	blizzardram_nojit_xlate, blizzardram_nojit_check, NULL, _T("CPUBoard RAM"),
	blizzardram_nojit_lget, blizzardram_nojit_wget, ABFLAG_RAM
};

MEMORY_BGET(blizzardram_nojit, 1);
MEMORY_WGET(blizzardram_nojit, 1);
MEMORY_LGET(blizzardram_nojit, 1);
MEMORY_CHECK(blizzardram_nojit);
MEMORY_XLATE(blizzardram_nojit);

static void REGPARAM2 blizzardram_nojit_lput(uaecptr addr, uae_u32 l)
{
	uae_u32 *m;

#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= blizzardram_nojit_bank.mask;
	if (maprom_state && addr >= maprom_base)
		return;
	m = (uae_u32 *)(blizzardram_nojit_bank.baseaddr + addr);
	do_put_mem_long(m, l);
}
static void REGPARAM2 blizzardram_nojit_wput(uaecptr addr, uae_u32 w)
{
	uae_u16 *m;

#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= blizzardram_nojit_bank.mask;
	if (maprom_state && addr >= maprom_base)
		return;
	m = (uae_u16 *)(blizzardram_nojit_bank.baseaddr + addr);
	do_put_mem_word(m, w);
}
static void REGPARAM2 blizzardram_nojit_bput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= blizzardram_nojit_bank.mask;
	if (maprom_state && addr >= maprom_base)
		return;
	blizzardram_nojit_bank.baseaddr[addr] = b;
}

DECLARE_MEMORY_FUNCTIONS(blizzardmaprom);

static addrbank blizzardmaprom_bank = {
	blizzardmaprom_lget, blizzardmaprom_wget, blizzardmaprom_bget,
	blizzardmaprom_lput, blizzardmaprom_wput, blizzardmaprom_bput,
	blizzardmaprom_xlate, blizzardmaprom_check, NULL, _T("CPUBoard MAPROM"),
	blizzardmaprom_lget, blizzardmaprom_wget, ABFLAG_RAM
};

MEMORY_BGET(blizzardmaprom, 1);
MEMORY_WGET(blizzardmaprom, 1);
MEMORY_LGET(blizzardmaprom, 1);
MEMORY_CHECK(blizzardmaprom);
MEMORY_XLATE(blizzardmaprom);

static void no_rom_protect(void)
{
	if (delayed_rom_protect)
		return;
	delayed_rom_protect = 10;
	protect_roms(false);
}

static void REGPARAM2 blizzardmaprom_lput(uaecptr addr, uae_u32 l)
{
	uae_u32 *m;
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= blizzardmaprom_bank.mask;
	m = (uae_u32 *)(blizzardmaprom_bank.baseaddr + addr);
	do_put_mem_long(m, l);
	if (maprom_state) {
		no_rom_protect();
		m = (uae_u32 *)(kickmem_bank.baseaddr + addr);
		do_put_mem_long(m, l);
	}
}
static void REGPARAM2 blizzardmaprom_wput(uaecptr addr, uae_u32 w)
{
	uae_u16 *m;
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= blizzardmaprom_bank.mask;
	m = (uae_u16 *)(blizzardmaprom_bank.baseaddr + addr);
	do_put_mem_word(m, w);
	if (maprom_state) {
		no_rom_protect();
		m = (uae_u16 *)(kickmem_bank.baseaddr + addr);
		do_put_mem_word(m, w);
	}
}
static void REGPARAM2 blizzardmaprom_bput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= blizzardmaprom_bank.mask;
	blizzardmaprom_bank.baseaddr[addr] = b;
	if (maprom_state) {
		no_rom_protect();
		kickmem_bank.baseaddr[addr] = b;
	}
}

DECLARE_MEMORY_FUNCTIONS(blizzardea);

static addrbank blizzardea_bank = {
	blizzardea_lget, blizzardea_wget, blizzardea_bget,
	blizzardea_lput, blizzardea_wput, blizzardea_bput,
	blizzardea_xlate, blizzardea_check, NULL, _T("Blizzard EA Autoconfig"),
	blizzardea_lget, blizzardea_wget, ABFLAG_IO | ABFLAG_SAFE
};

MEMORY_BGET(blizzardea, 0);
MEMORY_WGET(blizzardea, 0);
MEMORY_LGET(blizzardea, 0);
MEMORY_CHECK(blizzardea);
MEMORY_XLATE(blizzardea);

DECLARE_MEMORY_FUNCTIONS(blizzarde8);

static addrbank blizzarde8_bank = {
	blizzarde8_lget, blizzarde8_wget, blizzarde8_bget,
	blizzarde8_lput, blizzarde8_wput, blizzarde8_bput,
	blizzarde8_xlate, blizzarde8_check, NULL, _T("Blizzard E8 Autoconfig"),
	blizzarde8_lget, blizzarde8_wget, ABFLAG_IO | ABFLAG_SAFE
};

static int REGPARAM2 blizzarde8_check(uaecptr addr, uae_u32 size)
{
	return 0;
}
static uae_u8 *REGPARAM2 blizzarde8_xlate(uaecptr addr)
{
	return NULL;
}

DECLARE_MEMORY_FUNCTIONS(blizzardf0);

static addrbank blizzardf0_bank = {
	blizzardf0_lget, blizzardf0_wget, blizzardf0_bget,
	blizzardf0_lput, blizzardf0_wput, blizzardf0_bput,
	blizzardf0_xlate, blizzardf0_check, NULL, _T("CPUBoard F00000"),
	blizzardf0_lget, blizzardf0_wget, ABFLAG_ROM
};

static uae_u32 REGPARAM2 blizzardf0_lget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u32 *m;

	regs.memory_waitstate_cycles += F0_WAITSTATES * 6;

	if (is_blizzardppc() && (flash_unlocked & 2))
		addr += 262144;

	addr &= blizzardf0_bank.mask;
	m = (uae_u32 *)(blizzardf0_bank.baseaddr + addr);
	return do_get_mem_long(m);
}
static uae_u32 REGPARAM2 blizzardf0_wget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u16 *m, v;

	regs.memory_waitstate_cycles += F0_WAITSTATES * 3;

	if (is_blizzardppc() && (flash_unlocked & 2))
		addr += 262144;

	addr &= blizzardf0_bank.mask;
	m = (uae_u16 *)(blizzardf0_bank.baseaddr + addr);
	v = do_get_mem_word(m);
	return v;
}
static uae_u32 REGPARAM2 blizzardf0_bget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u8 v;

	regs.memory_waitstate_cycles += F0_WAITSTATES * 1;

	if (is_cs() || is_blizzardppc()) {
		if (is_blizzardppc() && (flash_unlocked & 2))
			addr += 262144;
		if (flash_unlocked) {
			return flash_read(flashrom, addr);
		}
	}
	addr &= blizzardf0_bank.mask;
	v = blizzardf0_bank.baseaddr[addr];
	return v;
}

// hack to map F41000 SCSI SCRIPTS RAM to JIT friendly address
void cyberstorm_scsi_ram_put(uaecptr addr, uae_u32 v)
{
	addr &= 0xffff;
	addr += (CSIII_NCR & 0x7ffff);
	blizzardf0_bank.baseaddr[addr] = v;
}
uae_u32 cyberstorm_scsi_ram_get(uaecptr addr)
{
	uae_u32 v;
	addr &= 0xffff;
	addr += (CSIII_NCR & 0x7ffff);
	v = blizzardf0_bank.baseaddr[addr];
	return v;
}
uae_u8 *REGPARAM2 cyberstorm_scsi_ram_xlate (uaecptr addr)
{
	addr &= 0xffff;
	addr += (CSIII_NCR & 0x7ffff);
	return blizzardf0_bank.baseaddr + addr;
}
int REGPARAM2 cyberstorm_scsi_ram_check (uaecptr a, uae_u32 b)
{
	a &= 0xffff;
	return a >= 0x1000 && a + b < 0x3000;
}

static void REGPARAM2 blizzardf0_lput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	regs.memory_waitstate_cycles += F0_WAITSTATES * 6;
}
static void REGPARAM2 blizzardf0_wput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	regs.memory_waitstate_cycles += F0_WAITSTATES * 3;
}
static void REGPARAM2 blizzardf0_bput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	regs.memory_waitstate_cycles += F0_WAITSTATES * 1;

	if (is_cs() || is_blizzardppc()) {
		if (flash_unlocked) {
			if (is_blizzardppc() && (flash_unlocked & 2))
				addr += 262144;
			flash_write(flashrom, addr, b);
		}
	}
}

MEMORY_CHECK(blizzardf0);
MEMORY_XLATE(blizzardf0);

static void REGPARAM2 blizzardea_lput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
}
static void REGPARAM2 blizzardea_wput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
}
static void REGPARAM2 blizzardea_bput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
}

static void REGPARAM2 blizzarde8_lput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
}
static void REGPARAM2 blizzarde8_wput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
}
static void REGPARAM2 blizzarde8_bput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	b &= 0xff;
	addr &= 65535;
	if (addr == 0x48 && !configured) {
		map_banks(&blizzardea_bank, b, 0x20000 >> 16, 0x20000);
		write_log(_T("Blizzard Z2 autoconfigured at %02X0000\n"), b);
		configured = 1;
		expamem_next();
		return;
	}
	if (addr == 0x4c && !configured) {
		write_log(_T("Blizzard Z2 SHUT-UP!\n"));
		configured = 1;
		expamem_next();
		return;
	}
}
static uae_u32 REGPARAM2 blizzarde8_bget(uaecptr addr)
{
	uae_u32 v = 0xffff;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v = blizzardea_bank.baseaddr[addr & blizzardea_bank.mask];
	return v;
}
static uae_u32 REGPARAM2 blizzarde8_wget(uaecptr addr)
{
	uae_u32 v = 0xffff;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v = (blizzardea_bank.baseaddr[addr & blizzardea_bank.mask] << 8) | blizzardea_bank.baseaddr[(addr + 1) & blizzardea_bank.mask];
	return v;
}
static uae_u32 REGPARAM2 blizzarde8_lget(uaecptr addr)
{
	uae_u32 v = 0xffff;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v = (blizzarde8_wget(addr) << 16) | blizzarde8_wget(addr + 2);
	return v;
}

static void blizzard_copymaprom(void)
{
	uae_u8 *src = get_real_address(BLIZZARD_MAPROM_BASE);
	uae_u8 *dst = get_real_address(0xf80000);
	protect_roms(false);
	memcpy(dst, src, 524288);
	protect_roms(true);
}
static void cyberstorm_copymaprom(void)
{
	if (blizzardmaprom_bank.baseaddr) {
		uae_u8 *src = blizzardmaprom_bank.baseaddr;
		uae_u8 *dst = get_real_address(0xf80000);
		protect_roms(false);
		memcpy(dst, src, 524288);
		protect_roms(true);
	}
}

void cpuboard_rethink(void)
{
	if (is_cs() || is_blizzardppc()) {
		if (!(io_reg[CSIII_REG_IRQ] & 3))
			INTREQ(0x8000 | 0x0008);
	}
}

static void cyberstorm_maprom(void)
{
	map_banks(&blizzardmaprom_bank, CYBERSTORM_MAPROM_BASE >> 16, 524288 >> 16, 0);
}

void cyberstorm_irq(int level)
{
	if (level)
		io_reg[CSIII_REG_IRQ] &= ~1;
	else
		io_reg[CSIII_REG_IRQ] |= 1;
	cpuboard_rethink();
}

void blizzardppc_irq(int level)
{
	if (level)
		io_reg[CSIII_REG_IRQ] &= ~1;
	else
		io_reg[CSIII_REG_IRQ] |= 1;
	cpuboard_rethink();
}

DECLARE_MEMORY_FUNCTIONS(blizzardio);

static addrbank blizzardio_bank = {
	blizzardio_lget, blizzardio_wget, blizzardio_bget,
	blizzardio_lput, blizzardio_wput, blizzardio_bput,
	default_xlate, default_check, NULL, _T("CPUBoard IO"),
	blizzardio_wget, blizzardio_bget, ABFLAG_IO
};

static uae_u32 REGPARAM2 blizzardio_bget(uaecptr addr)
{
	uae_u8 v = 0;
#ifdef JIT
	special_mem |= S_READ;
#endif
	if (is_cs() || is_blizzardppc()) {
		uae_u32 bank = addr & 0x10000;
		if (bank == 0) {
			int reg = (addr & 0xff) / 8;
			v = io_reg[reg];
			if (reg == CSIII_REG_LOCK && is_blizzardppc())
				v |= 0x08;
			if (reg != CSIII_REG_IRQ)
				write_log(_T("CS IO BGET %08x=%02X PC=%08x\n"), addr, v & 0xff, M68K_GETPC);
		} else {
			write_log(_T("CS IO BGET %08x=%02X PC=%08x\n"), addr, v & 0xff, M68K_GETPC);
		}
	}
	return v;
}
static uae_u32 REGPARAM2 blizzardio_wget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	if (is_cs() || is_blizzardppc()) {
		;//write_log(_T("CS IO WGET %08x\n"), addr);
		//activate_debugger();
	}
	return 0;
}
static uae_u32 REGPARAM2 blizzardio_lget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	if (is_cs() || is_blizzardppc()) {
		write_log(_T("CS IO LGET %08x PC=%08x\n"), addr, M68K_GETPC);
		//activate_debugger();
	}
	return 0;
}
static void REGPARAM2 blizzardio_bput(uaecptr addr, uae_u32 v)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (is_blizzard()) {
		if ((addr & 65535) == (BLIZZARD_MAPROM_ENABLE & 65535)) {
			if (v != 0x42 || maprom_state || !currprefs.maprom)
				return;
			maprom_state = 1;
			write_log(_T("Blizzard MAPROM enabled\n"));
			blizzard_copymaprom();
		}
	} else if (is_cs() || is_blizzardppc()) {
		write_log(_T("CS IO BPUT %08x %02x PC=%08x\n"), addr, v & 0xff, M68K_GETPC);
		uae_u32 bank = addr & 0x10000;
		if (bank == 0) {
			addr &= 0xff;
			if (is_blizzardppc()) {
				if (addr == 0x92 && v == 0x42)
					flash_unlocked |= 1;
				if (addr == 0x93)
					flash_unlocked &= ~1;
				if (addr == 0x12) {
					maprom_state = 1;
					cyberstorm_copymaprom();
				}
			}
			addr /= 8;
			uae_u8 oldval = io_reg[addr];
			if (addr == CSIII_REG_LOCK) {
				if (v == P5_MAGIC1)
					io_reg[CSIII_REG_LOCK] = v;
				else if (v == P5_MAGIC2 && io_reg[CSIII_REG_LOCK] == P5_MAGIC1)
					io_reg[CSIII_REG_LOCK] = v;
				else if (v == P5_MAGIC3 && io_reg[CSIII_REG_LOCK] == P5_MAGIC2)
					io_reg[CSIII_REG_LOCK] = v;
				else if (v == P5_MAGIC4 && io_reg[CSIII_REG_LOCK] == P5_MAGIC3)
					io_reg[CSIII_REG_LOCK] = v;
				else
					io_reg[CSIII_REG_LOCK] = 0;
				if (io_reg[CSIII_REG_LOCK] == P5_MAGIC3)
					flash_unlocked |= 2;
				else
					flash_unlocked &= ~2;
			} else {
				uae_u32 regval;
				if (addr == CSIII_REG_SHADOW && io_reg[CSIII_REG_LOCK] != P5_MAGIC3)
					return;
				if (v & 0x80)
					io_reg[addr] |= v & 0x7f;
				else
					io_reg[addr] &= ~v;
				regval = io_reg[addr];
				if (addr == CSIII_REG_RESET) {
					if (regval & P5_SCSI_RESET) {
						if (is_blizzardppc())
							map_banks(&ncr_bank_blizzardppc, 0xf40000 >> 16, 65536 >> 16, 0);
						else
							map_banks(&ncr_bank_cyberstorm, 0xf40000 >> 16, 65536 >> 16, 0);
					} else {
						map_banks(&dummy_bank, 0xf40000 >> 16, 65536 >> 16, 0);
					}
					if (!(regval & P5_AMIGA_RESET)) {
						uae_reset(0, 0);
						io_reg[addr] |= P5_AMIGA_RESET;
					}
					if (!(regval & P5_M68K_RESET)) {
						m68k_reset();
						io_reg[addr] |= P5_M68K_RESET;
					}
				} else if (addr == CSIII_REG_SHADOW) {
					if (is_cs() && ((oldval ^ regval) & 1)) {
						maprom_state = (regval & 1) ? 0 : 1;
						cyberstorm_copymaprom();
					}
				}
				cpuboard_rethink();
			}
		}
	}
}
static void REGPARAM2 blizzardio_wput(uaecptr addr, uae_u32 v)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (is_blizzard()) {
		if((addr & 65535) == (BLIZZARD_BOARD_DISABLE & 65535)) {
			if (v != 0xcafe)
				return;
			write_log(_T("Blizzard board disable!\n"));
			cpu_halt(4); // not much choice..
		}
	} else if (is_cs() || is_blizzardppc()) {
		write_log(_T("CS IO WPUT %08x %04x\n"), addr, v);
	}
}
static void REGPARAM2 blizzardio_lput(uaecptr addr, uae_u32 v)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (is_cs() || is_blizzardppc()) {
		write_log(_T("CS IO LPUT %08x %08x\n"), addr, v);
	}
}

void cpuboard_vsync(void)
{
	if (delayed_rom_protect <= 0)
		return;
	delayed_rom_protect--;
	if (delayed_rom_protect == 0)
		protect_roms(true);
}

void cpuboard_map(void)
{
	if (!currprefs.cpuboard_type)
		return;
	if (is_blizzard() || is_blizzardppc()) {
		if (cpuboard_size) {
			if (blizzard_jit) {
				map_banks(&blizzardram_bank, blizzardram_bank.start >> 16, cpuboard_size >> 16, 0);
				map_banks(&blizzardram_bank, BLIZZARD_RAM_BASE >> 16, cpuboard_size >> 16, 0);
			} else {
				for (int i = 0; i < 0x08000000; i += cpuboard_size) {
					map_banks_nojitdirect(&blizzardram_nojit_bank, (BLIZZARD_RAM_ALIAS_BASE + i)  >> 16, cpuboard_size >> 16, 0);
					map_banks_nojitdirect(&blizzardram_nojit_bank, (BLIZZARD_RAM_BASE + i) >> 16, cpuboard_size >> 16, 0);
				}
				if (currprefs.maprom && !is_blizzardppc()) {
					for (int i = 0; i < 0x08000000; i += cpuboard_size) {
						map_banks_nojitdirect(&blizzardmaprom_bank, (BLIZZARD_RAM_ALIAS_BASE + i + cpuboard_size - 524288) >> 16, 524288 >> 16, 0);
						map_banks_nojitdirect(&blizzardmaprom_bank, (BLIZZARD_RAM_BASE + i + cpuboard_size - 524288) >> 16, 524288 >> 16, 0);
					}
				}
			}
		}
		if (!is_blizzardppc()) {
			map_banks(&blizzardf0_bank, 0xf00000 >> 16, 65536 >> 16, 0);
			map_banks(&blizzardio_bank, BLIZZARD_MAPROM_ENABLE >> 16, 65536 >> 16, 0);
			map_banks(&blizzardio_bank, BLIZZARD_BOARD_DISABLE >> 16, 65536 >> 16, 0);
		} else {
			map_banks(&blizzardf0_bank, 0xf00000 >> 16, 0x60000 >> 16, 0);
			map_banks(&blizzardio_bank, 0xf60000 >> 16, (2 * 65536) >> 16, 0);
			map_banks(&blizzardf0_bank, 0xf70000 >> 16, 0x10000 >> 16, 0);
			cyberstorm_maprom();
		}
	}
	if (is_cs()) {
		map_banks(&blizzardf0_bank, 0xf00000 >> 16, 262144 >> 16, 0);
		map_banks(&blizzardio_bank, 0xf50000 >> 16, (3 * 65536) >> 16, 0);
		cyberstorm_maprom();
	}
}

void cpuboard_reset(bool hardreset)
{
	if (is_blizzard() || is_blizzardppc())
		canbang = 0;
	configured = false;
	delayed_rom_protect = 0;
	currprefs.cpuboardmem1_size = changed_prefs.cpuboardmem1_size;
	if (hardreset || !currprefs.maprom)
		maprom_state = 0;
	if (is_cs() || is_blizzardppc()) {
		if (hardreset)
			memset(io_reg, 0x7f, sizeof io_reg);
		io_reg[CSIII_REG_RESET] = 0x7f;
		io_reg[CSIII_REG_IRQ] = 0x7f;
		maprom_state = 0;
	}
	flash_unlocked = 0;

	flash_free(flashrom);
	flashrom = NULL;
	zfile_fclose(flashrom_file);
	flashrom_file = NULL;
}

void cpuboard_cleanup(void)
{
	configured = false;
	maprom_state = 0;

	flash_free(flashrom);
	flashrom = NULL;
	zfile_fclose(flashrom_file);
	flashrom_file = NULL;

	if (blizzard_jit) {
		mapped_free(blizzardram_bank.baseaddr);
	} else {
		xfree(blizzardram_nojit_bank.baseaddr);
	}
	blizzardram_bank.baseaddr = NULL;
	blizzardram_nojit_bank.baseaddr = NULL;
	blizzardmaprom_bank.baseaddr = NULL;

	mapped_free(blizzardf0_bank.baseaddr);
	blizzardf0_bank.baseaddr = NULL;

	mapped_free(blizzardea_bank.baseaddr);
	blizzardea_bank.baseaddr = NULL;

	cpuboard_size = cpuboard2_size = -1;

}

void cpuboard_init(void)
{
	if (!currprefs.cpuboard_type)
		return;

	if (cpuboard_size == currprefs.cpuboardmem1_size)
		return;

	cpuboard_cleanup();

	cpuboard_size = currprefs.cpuboardmem1_size;

	if (is_blizzard() || is_blizzardppc()) {
		blizzardram_bank.start = BLIZZARD_RAM_ALIAS_BASE;
		blizzardram_bank.allocated = cpuboard_size;
		blizzardram_bank.mask = blizzardram_bank.allocated - 1;

		blizzardram_nojit_bank.start = blizzardram_bank.start;
		blizzardram_nojit_bank.allocated = blizzardram_bank.allocated;
		blizzardram_nojit_bank.mask = blizzardram_bank.mask;


		blizzard_jit = 0 && BLIZZARD_RAM_BASE + blizzardram_bank.allocated <= max_z3fastmem && currprefs.jit_direct_compatible_memory;
		if (blizzard_jit) {
			if (cpuboard_size)
				blizzardram_bank.baseaddr = mapped_malloc(blizzardram_bank.allocated, _T("blizzard"));
		} else {
			if (cpuboard_size)
				blizzardram_bank.baseaddr = xmalloc(uae_u8, blizzardram_bank.allocated);
		}
		blizzardram_nojit_bank.baseaddr = blizzardram_bank.baseaddr;

		blizzardmaprom_bank.baseaddr = blizzardram_bank.baseaddr + cpuboard_size - 524288;
		blizzardmaprom_bank.start = BLIZZARD_MAPROM_BASE;
		blizzardmaprom_bank.allocated = 524288;
		blizzardmaprom_bank.mask = 524288 - 1;

		maprom_base = blizzardram_bank.allocated - 524288;

		blizzardf0_bank.start = 0x00f00000;
		blizzardf0_bank.allocated = 524288;
		blizzardf0_bank.mask = blizzardf0_bank.allocated - 1;
		blizzardf0_bank.baseaddr = mapped_malloc(blizzardf0_bank.allocated, _T("rom_f0"));

		if (!is_blizzardppc()) {
			blizzardea_bank.allocated = 2 * 65536;
			blizzardea_bank.mask = blizzardea_bank.allocated - 1;
			// Blizzard 12xx autoconfig ROM must be mapped at $ea0000-$ebffff, board requires it.
			blizzardea_bank.baseaddr = mapped_malloc(blizzardea_bank.allocated, _T("rom_ea"));
		}

	} else if (is_cs()) {
	
		blizzardram_bank.start = CS_RAM_BASE;
		blizzardram_bank.allocated = cpuboard_size;
		blizzardram_bank.mask = blizzardram_bank.allocated - 1;
		if (cpuboard_size)
			blizzardram_bank.baseaddr = mapped_malloc(blizzardram_bank.allocated, _T("cyberstorm"));

		blizzardf0_bank.start = 0x00f00000;
		blizzardf0_bank.allocated = 524288;
		blizzardf0_bank.mask = blizzardf0_bank.allocated - 1;
		blizzardf0_bank.baseaddr = mapped_malloc(blizzardf0_bank.allocated, _T("rom_f0"));

		blizzardmaprom_bank.baseaddr = a3000hmem_bank.baseaddr + a3000hmem_bank.allocated - 524288;
		blizzardmaprom_bank.start = CYBERSTORM_MAPROM_BASE;
		blizzardmaprom_bank.allocated = 524288;
		blizzardmaprom_bank.mask = 524288 - 1;
	}
}

void cpuboard_clear(void)
{
}

bool cpuboard_maprom(void)
{
	if (!currprefs.cpuboard_type || !cpuboard_size)
		return false;
	if (is_blizzard() || is_blizzardppc()) {
		if (maprom_state)
			blizzard_copymaprom();
	} else if (is_cs()) {
		if (!(io_reg[CSIII_REG_SHADOW] & P5_SHADOW))
			cyberstorm_copymaprom();
	}
	return true;
}

static struct zfile *flashfile_open(const TCHAR *name)
{
	struct zfile *f;
	TCHAR path[MAX_DPATH];

	fetch_rompath(path, sizeof path / sizeof(TCHAR));
	_tcscat(path, name);
	f = zfile_fopen(path, _T("rb+"), ZFD_NONE);
	if (!f)
		f = zfile_fopen(path, _T("rb"), ZFD_NORMAL);
	return f;
}

addrbank *cpuboard_autoconfig_init(void)
{
	struct zfile *autoconfig_rom = NULL;
	int roms[2];
	bool autoconf = true;
	const TCHAR *romname = NULL;

	roms[0] = -1;
	roms[1] = -1;
	switch (currprefs.cpuboard_type)
	{
	case BOARD_BLIZZARD_1230_IV:
		roms[0] = 89;
		break;
	case BOARD_BLIZZARD_1260:
		roms[0] = 90;
		break;
	case BOARD_BLIZZARD_2060:
		roms[0] = 91;
		break;
	case BOARD_WARPENGINE_A4000:
		expamem_next();
		return NULL;
	case BOARD_CSMK3:
	case BOARD_CSPPC:
	case BOARD_BLIZZARDPPC:
		break;
	default:
		expamem_next();
		return NULL;
	}

	struct romlist *rl = getromlistbyids(roms);
	if (rl) {
		autoconfig_rom = read_rom(rl->rd);
	}

	if (currprefs.cpuboard_type == BOARD_CSMK3) {
		romname = _T("cyberstormmk3.rom");
	}
	if (currprefs.cpuboard_type == BOARD_CSPPC) {
		romname = _T("cyberstormppc.rom");
	}
	if (currprefs.cpuboard_type == BOARD_BLIZZARDPPC) {
		romname = _T("blizzardppc.rom");
	}

	if (romname != NULL) {
		autoconfig_rom = flashfile_open(romname);
		if (!autoconfig_rom) {
			write_log(_T("Couldn't open CPU board rom '%s'\n"), romname);
			expamem_next();
			return NULL;
		}
	}

	if (!autoconfig_rom && roms[0] != -1) {
		write_log (_T("ROM id %d not found for CPU board emulation\n"), roms[0]);
		expamem_next();
		return NULL;
	}
	protect_roms(false);
	if (currprefs.cpuboard_type == BOARD_BLIZZARD_2060) {
		f0rom_size = 65536;
		earom_size = 131072;
		// 2060 = 2x32k
		for (int i = 0; i < 32768; i++) {
			uae_u8 b = 0xff;
			zfile_fread(&b, 1, 1, autoconfig_rom);
			blizzardf0_bank.baseaddr[i * 2 + 1] = b;
			zfile_fread(&b, 1, 1, autoconfig_rom);
			blizzardf0_bank.baseaddr[i * 2 + 0] = b;
			zfile_fread(&b, 1, 1, autoconfig_rom);
			blizzardea_bank.baseaddr[i * 2 + 1] = b;
			zfile_fread(&b, 1, 1, autoconfig_rom);
			blizzardea_bank.baseaddr[i * 2 + 0] = b;
		}
	} else if (is_cs() || is_blizzardppc()) {
		f0rom_size = is_blizzardppc() ? 524288 : 131072;
		earom_size = 0;
		memset(blizzardf0_bank.baseaddr, 0xff, f0rom_size);
		zfile_fread(blizzardf0_bank.baseaddr, f0rom_size, 1, autoconfig_rom);
		autoconf = false;
		if (zfile_needwrite(autoconfig_rom)) {
			flashrom_file = autoconfig_rom;
			autoconfig_rom = NULL;
		}
		flashrom = flash_new(blizzardf0_bank.baseaddr, f0rom_size, f0rom_size, flashrom_file);
	}
	else {
		f0rom_size = 65536;
		earom_size = 131072;
		// 12xx = 1x32k
		for (int i = 0; i < 16384; i++) {
			uae_u8 b = 0xff;
			zfile_fread(&b, 1, 1, autoconfig_rom);
			blizzardf0_bank.baseaddr[i] = b;
			zfile_fread(&b, 1, 1, autoconfig_rom);
			blizzardea_bank.baseaddr[i] = b;
		}
	}
	protect_roms(true);
	zfile_fclose(autoconfig_rom);

	if (f0rom_size)
		map_banks(&blizzardf0_bank, 0xf00000 >> 16, 262144 >> 16, 0);
	if (!autoconf) {
		expamem_next();
		return NULL;
	}
	return &blizzarde8_bank;
}
