/*
* UAE - The Un*x Amiga Emulator
*
* Misc accelerator board special features
* Blizzard 1230 IV, 1240/1260, 2040/2060, PPC
* CyberStorm MK1, MK2, MK3, PPC.
* Warp Engine
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
#include "ncr9x_scsi.h"
#include "debug.h"
#include "flashrom.h"
#include "uae.h"
#include "uae/ppc.h"

#define CPUBOARD_IO_LOG 0
#define CPUBOARD_IRQ_LOG 0

#define F0_WAITSTATES (2 * CYCLE_UNIT)

// CS MK3/PPC
#define CYBERSTORM_MAPROM_BASE 0xfff00000
#define CSIII_NCR			0xf40000
#define CSIII_BASE			0xf60000
#define CSIII_REG_RESET		0x00 // 0x00
#define CSIII_REG_IRQ		0x01 // 0x08
#define CSIII_REG_WAITSTATE	0x02 // 0x10
#define CSIII_REG_SHADOW	0x03 // 0x18
#define CSIII_REG_LOCK		0x04 // 0x20
#define CSIII_REG_INT		0x05 // 0x28
#define CSIII_REG_IPL_EMU	0x06 // 0x30
#define CSIII_REG_INT_LVL	0x07 // 0x38

// BPPC only
#define BPPC_MAPROM_ON		0x12
#define BPPC_MAPROM_OFF		0x13
#define BPPC_UNLOCK_FLASH	0x92
#define BPPC_LOCK_FLASH		0x93
#define BPPC_MAGIC_UNLOCK_VALUE 0x42

/* bit definitions */
#define	P5_SET_CLEAR		0x80

/* REQ_RESET 0x00 */
// 0x10/0x08/0x04 only work if P5_SELF_RESET is cleared
#define	P5_PPC_RESET		0x10
#define	P5_M68K_RESET		0x08
#define	P5_AMIGA_RESET		0x04
#define	P5_AUX_RESET		0x02
#define	P5_SCSI_RESET		0x01

/* REG_IRQ 0x08 */
#define P5_IRQ_SCSI			0x01
#define P5_IRQ_SCSI_EN		0x02
// 0x04
#define P5_IRQ_PPC_1		0x08
#define P5_IRQ_PPC_2		0x10
// 0x20
// 0x40 always cleared

/* REG_WAITSTATE 0x10 */
#define	P5_PPC_WRITE		0x08
#define	P5_PPC_READ			0x04
#define	P5_M68K_WRITE		0x02
#define	P5_M68K_READ		0x01

/* REG_SHADOW 0x18 */
#define	P5_SELF_RESET		0x40
// 0x20
// 0x10
// 0x08 always set
#define P5_FLASH			0x04 // Flash writable (CSMK3,CSPPC only)
// 0x02 (can be modified even when locked)
#define	P5_SHADOW			0x01 // KS MAP ROM

/* REG_LOCK 0x20. CSMK3,CSPPC only */
#define	P5_MAGIC1			0x60 // REG_SHADOW and flash write protection unlock sequence
#define	P5_MAGIC2			0x50
#define	P5_MAGIC3			0x30
#define	P5_MAGIC4			0x70
// when cleared, another CPU gets either stopped or can't access memory until set again.
#define P5_LOCK_CPU			0x01 

/* REG_INT 0x28 */
// 0x40 always set
// 0x20
// 0x10 always cleared
// 0x08
// 0x04
#define	P5_ENABLE_IPL		0x02
#define	P5_INT_MASTER		0x01 // 1=m68k gets interrupts, 0=ppc gets interrupts.

/* IPL_EMU 0x30 */
#define	P5_DISABLE_INT		0x40 // if set: all CPU interrupts disabled?
#define	P5_M68K_IPL2		0x20
#define	P5_M68K_IPL1		0x10
#define	P5_M68K_IPL0		0x08
#define P5_M68k_IPL_MASK	0x38
#define	P5_PPC_IPL2			0x04
#define	P5_PPC_IPL1			0x02
#define	P5_PPC_IPL0			0x01
#define P5_PPC_IPL_MASK		0x07

/* INT_LVL 0x38 */
#define	P5_LVL7				0x40
#define	P5_LVL6				0x20
#define	P5_LVL5				0x10
#define	P5_LVL4				0x08
#define	P5_LVL3				0x04
#define	P5_LVL2				0x02
#define	P5_LVL1				0x01

#define CS_RAM_BASE 0x0c000000

#define BLIZZARD_RAM_BASE 0x68000000
#define BLIZZARD_RAM_ALIAS_BASE 0x48000000
#define BLIZZARD_MAPROM_BASE 0x4ff80000
#define BLIZZARD_MAPROM_ENABLE 0x80ffff00
#define BLIZZARD_BOARD_DISABLE 0x80fa0000

#define CSMK2_BOARD_DISABLE 0x83000000

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
static int csmk2_flashaddressing;
static bool blizzardmaprom_bank_mapped, blizzardmaprom2_bank_mapped;

static int ppc_irq_pending;

static void set_ppc_interrupt(int level)
{
	if (ppc_irq_pending > level)
		return;
#if CPUBOARD_IRQ_LOG > 0
	if (ppc_irq_pending != level)
		write_log(_T("PPC interrupt set (%d)\n"), level);
#endif
	uae_ppc_interrupt(true);
	ppc_irq_pending = level;
}
static void clear_ppc_interrupt(void)
{
	if (!ppc_irq_pending)
		return;
#if CPUBOARD_IRQ_LOG > 0
	write_log(_T("PPC interrupt clear\n"));
#endif
	uae_ppc_interrupt(false);
	ppc_irq_pending = 0;
}

static void check_ppc_int_lvl(void)
{
	uae_u8 ipl;
	uae_u8 il;
	if (!(io_reg[CSIII_REG_INT] & P5_ENABLE_IPL)) {
		ipl = (~io_reg[CSIII_REG_IPL_EMU]) & P5_PPC_IPL_MASK;
		if (ipl < 7) {
			il = (~io_reg[CSIII_REG_INT_LVL]) & 0x7f;
			if (il) {
				for (int i = ipl; i < 7; i++) {
					if (il & (1 << i)) {
						set_ppc_interrupt(i + 1);
						return;
					}
				}
			}
		}
	}
	clear_ppc_interrupt();
}

bool ppc_interrupt(int new_m68k_ipl)
{
	uae_u8 ppcipl;
	bool m68kint = (io_reg[CSIII_REG_INT] & P5_INT_MASTER) != 0;
	bool active = (io_reg[CSIII_REG_IPL_EMU] & P5_DISABLE_INT) == 0;
	bool iplemu = (io_reg[CSIII_REG_INT] & P5_ENABLE_IPL) == 0;

	if (!active)
		return false;

	if (new_m68k_ipl < 0)
		new_m68k_ipl = 0;
	ppcipl = (io_reg[CSIII_REG_IPL_EMU] ^ 7) & 7;

	if (!m68kint && iplemu && active) {
		if (new_m68k_ipl > ppcipl) {
			set_ppc_interrupt(new_m68k_ipl);
		} else if (!new_m68k_ipl) {
			clear_ppc_interrupt();
		}
		io_reg[CSIII_REG_IPL_EMU] &= ~P5_M68k_IPL_MASK;
		io_reg[CSIII_REG_IPL_EMU] |= (new_m68k_ipl << 3) ^ P5_M68k_IPL_MASK;
	}

	return m68kint;
}


static bool is_blizzard(void)
{
	return currprefs.cpuboard_type == BOARD_BLIZZARD_1230_IV || currprefs.cpuboard_type == BOARD_BLIZZARD_1230_IV_SCSI ||
		currprefs.cpuboard_type == BOARD_BLIZZARD_1260 || currprefs.cpuboard_type == BOARD_BLIZZARD_1260_SCSI;
}
static bool is_blizzard2060(void)
{
	return currprefs.cpuboard_type == BOARD_BLIZZARD_2060;
}
static bool is_csmk1(void)
{
	return currprefs.cpuboard_type == BOARD_CSMK1;
}
static bool is_csmk2(void)
{
	return currprefs.cpuboard_type == BOARD_CSMK2;
}
static bool is_csmk3(void)
{
	return currprefs.cpuboard_type == BOARD_CSMK3 || currprefs.cpuboard_type == BOARD_CSPPC;
}
static bool is_blizzardppc(void)
{
	return currprefs.cpuboard_type == BOARD_BLIZZARDPPC;
}
static bool is_ppc(void)
{
	return currprefs.cpuboard_type == BOARD_BLIZZARDPPC || currprefs.cpuboard_type == BOARD_CSPPC;

}

DECLARE_MEMORY_FUNCTIONS(blizzardio);
static addrbank blizzardio_bank = {
	blizzardio_lget, blizzardio_wget, blizzardio_bget,
	blizzardio_lput, blizzardio_wput, blizzardio_bput,
	default_xlate, default_check, NULL, NULL, _T("CPUBoard IO"),
	blizzardio_wget, blizzardio_bget, ABFLAG_IO
};

DECLARE_MEMORY_FUNCTIONS(blizzardram);
static addrbank blizzardram_bank = {
	blizzardram_lget, blizzardram_wget, blizzardram_bget,
	blizzardram_lput, blizzardram_wput, blizzardram_bput,
	blizzardram_xlate, blizzardram_check, NULL, NULL, _T("CPUBoard RAM"),
	blizzardram_lget, blizzardram_wget, ABFLAG_RAM
};

DECLARE_MEMORY_FUNCTIONS(blizzardea);
static addrbank blizzardea_bank = {
	blizzardea_lget, blizzardea_wget, blizzardea_bget,
	blizzardea_lput, blizzardea_wput, blizzardea_bput,
	blizzardea_xlate, blizzardea_check, NULL, _T("rom_ea"), _T("CPUBoard EA Autoconfig"),
	blizzardea_lget, blizzardea_wget, ABFLAG_IO | ABFLAG_SAFE
};

DECLARE_MEMORY_FUNCTIONS(blizzarde8);
static addrbank blizzarde8_bank = {
	blizzarde8_lget, blizzarde8_wget, blizzarde8_bget,
	blizzarde8_lput, blizzarde8_wput, blizzarde8_bput,
	blizzarde8_xlate, blizzarde8_check, NULL, NULL, _T("CPUBoard E8 Autoconfig"),
	blizzarde8_lget, blizzarde8_wget, ABFLAG_IO | ABFLAG_SAFE
};

DECLARE_MEMORY_FUNCTIONS(blizzardf0);
static addrbank blizzardf0_bank = {
	blizzardf0_lget, blizzardf0_wget, blizzardf0_bget,
	blizzardf0_lput, blizzardf0_wput, blizzardf0_bput,
	blizzardf0_xlate, blizzardf0_check, NULL, _T("rom_f0"), _T("CPUBoard F00000"),
	blizzardf0_lget, blizzardf0_wget, ABFLAG_ROM
};

DECLARE_MEMORY_FUNCTIONS(blizzardram_nojit);
static addrbank blizzardram_nojit_bank = {
	blizzardram_nojit_lget, blizzardram_nojit_wget, blizzardram_nojit_bget,
	blizzardram_nojit_lput, blizzardram_nojit_wput, blizzardram_nojit_bput,
	blizzardram_nojit_xlate, blizzardram_nojit_check, NULL, NULL, _T("CPUBoard RAM"),
	blizzardram_nojit_lget, blizzardram_nojit_wget, ABFLAG_RAM
};

DECLARE_MEMORY_FUNCTIONS(blizzardmaprom);
static addrbank blizzardmaprom_bank = {
	blizzardmaprom_lget, blizzardmaprom_wget, blizzardmaprom_bget,
	blizzardmaprom_lput, blizzardmaprom_wput, blizzardmaprom_bput,
	blizzardmaprom_xlate, blizzardmaprom_check, NULL, _T("maprom"), _T("CPUBoard MAPROM"),
	blizzardmaprom_lget, blizzardmaprom_wget, ABFLAG_RAM
};
DECLARE_MEMORY_FUNCTIONS(blizzardmaprom2);
static addrbank blizzardmaprom2_bank = {
	blizzardmaprom2_lget, blizzardmaprom2_wget, blizzardmaprom2_bget,
	blizzardmaprom2_lput, blizzardmaprom2_wput, blizzardmaprom2_bput,
	blizzardmaprom2_xlate, blizzardmaprom2_check, NULL, _T("maprom2"), _T("CPUBoard MAPROM2"),
	blizzardmaprom2_lget, blizzardmaprom2_wget, ABFLAG_RAM
};

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
uae_u8 *REGPARAM2 cyberstorm_scsi_ram_xlate(uaecptr addr)
{
	addr &= 0xffff;
	addr += (CSIII_NCR & 0x7ffff);
	return blizzardf0_bank.baseaddr + addr;
}
int REGPARAM2 cyberstorm_scsi_ram_check(uaecptr a, uae_u32 b)
{
	a &= 0xffff;
	return a >= 0x1000 && a + b < 0x3000;
}

MEMORY_FUNCTIONS(blizzardram);

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

static void no_rom_protect(void)
{
	if (delayed_rom_protect)
		return;
	delayed_rom_protect = 10;
	protect_roms(false);
}

MEMORY_BGET(blizzardmaprom2, 1);
MEMORY_WGET(blizzardmaprom2, 1);
MEMORY_LGET(blizzardmaprom2, 1);
MEMORY_BPUT(blizzardmaprom2, 1);
MEMORY_WPUT(blizzardmaprom2, 1);
MEMORY_LPUT(blizzardmaprom2, 1);
MEMORY_CHECK(blizzardmaprom2);
MEMORY_XLATE(blizzardmaprom2);

MEMORY_BGET(blizzardmaprom, 1);
MEMORY_WGET(blizzardmaprom, 1);
MEMORY_LGET(blizzardmaprom, 1);
MEMORY_CHECK(blizzardmaprom);
MEMORY_XLATE(blizzardmaprom);

static void REGPARAM2 blizzardmaprom_lput(uaecptr addr, uae_u32 l)
{
	uae_u32 *m;
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (is_blizzard2060() && !maprom_state)
		return;
	addr &= blizzardmaprom_bank.mask;
	m = (uae_u32 *)(blizzardmaprom_bank.baseaddr + addr);
	do_put_mem_long(m, l);
	if (maprom_state && !(addr & 0x80000)) {
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
	if (is_blizzard2060() && !maprom_state)
		return;
	addr &= blizzardmaprom_bank.mask;
	m = (uae_u16 *)(blizzardmaprom_bank.baseaddr + addr);
	do_put_mem_word(m, w);
	if (maprom_state && !(addr & 0x80000)) {
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
	if (is_blizzard2060() && !maprom_state)
		return;
	addr &= blizzardmaprom_bank.mask;
	blizzardmaprom_bank.baseaddr[addr] = b;
	if (maprom_state && !(addr & 0x80000)) {
		no_rom_protect();
		kickmem_bank.baseaddr[addr] = b;
	}
}

MEMORY_CHECK(blizzardea);
MEMORY_XLATE(blizzardea);

static int REGPARAM2 blizzarde8_check(uaecptr addr, uae_u32 size)
{
	return 0;
}
static uae_u8 *REGPARAM2 blizzarde8_xlate(uaecptr addr)
{
	return NULL;
}

static uae_u32 REGPARAM2 blizzardf0_lget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u32 *m;

	//write_log(_T("F0 LONGGET %08x\n"), addr);

	regs.memory_waitstate_cycles += F0_WAITSTATES * 6;

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

	if (is_csmk3() || is_blizzardppc()) {
		if (flash_unlocked) {
			return flash_read(flashrom, addr);
		}
	} else if (is_csmk2()) {
		addr &= 65535;
		addr += 65536;
		return flash_read(flashrom, addr);
	} else if (is_csmk1()) {
		addr &= 65535;
		addr += 65536;
		return flash_read(flashrom, addr);
	}
	addr &= blizzardf0_bank.mask;
	v = blizzardf0_bank.baseaddr[addr];
	return v;
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

	if (is_csmk3() || is_blizzardppc()) {
		if (flash_unlocked) {
			flash_write(flashrom, addr, b);
		}
	} else if (is_csmk2()) {
		addr &= 65535;
		addr += 65536;
		addr &= ~3;
		addr |= csmk2_flashaddressing;
		flash_write(flashrom, addr, b);
	} else if (is_csmk1()) {
		addr &= 65535;
		addr += 65536;
		flash_write(flashrom, addr, b);
	}
}

MEMORY_CHECK(blizzardf0);
MEMORY_XLATE(blizzardf0);

static uae_u32 REGPARAM2 blizzardea_lget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u32 *m;

	addr &= blizzardea_bank.mask;
	m = (uae_u32 *)(blizzardea_bank.baseaddr + addr);
	return do_get_mem_long(m);
}
static uae_u32 REGPARAM2 blizzardea_wget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u16 *m, v;

	addr &= blizzardea_bank.mask;
	m = (uae_u16 *)(blizzardea_bank.baseaddr + addr);
	v = do_get_mem_word(m);
	return v;
}
static uae_u32 REGPARAM2 blizzardea_bget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u8 v;

	addr &= blizzardea_bank.mask;
	if (is_blizzard2060() && addr >= BLIZZARD_2060_SCSI_OFFSET) {
		v = cpuboard_ncr9x_scsi_get(addr);
	} else if ((currprefs.cpuboard_type == BOARD_BLIZZARD_1230_IV_SCSI || currprefs.cpuboard_type == BOARD_BLIZZARD_1260_SCSI) && addr >= BLIZZARD_SCSI_KIT_SCSI_OFFSET) {
		v = cpuboard_ncr9x_scsi_get(addr);
	} else if (is_csmk1()) {
		if (addr >= CYBERSTORM_MK1_SCSI_OFFSET) {
			v = cpuboard_ncr9x_scsi_get(addr);
		} else if (flash_active(flashrom, addr)) {
			v = flash_read(flashrom, addr);
		} else {
			v = blizzardea_bank.baseaddr[addr];
		}
	} else if (is_csmk2()) {
		if (addr >= CYBERSTORM_MK2_SCSI_OFFSET) {
			v = cpuboard_ncr9x_scsi_get(addr);
		} else if (flash_active(flashrom, addr)) {
			v = flash_read(flashrom, addr);
		} else {
			v = blizzardea_bank.baseaddr[addr];
		}
	} else {
		v = blizzardea_bank.baseaddr[addr];
	}
	return v;
}

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
	addr &= blizzardea_bank.mask;

	if (is_blizzard2060() && addr >= BLIZZARD_2060_SCSI_OFFSET) {
		cpuboard_ncr9x_scsi_put(addr, b);
	} else if ((currprefs.cpuboard_type == BOARD_BLIZZARD_1230_IV_SCSI || currprefs.cpuboard_type == BOARD_BLIZZARD_1260_SCSI) && addr >= BLIZZARD_SCSI_KIT_SCSI_OFFSET) {
		cpuboard_ncr9x_scsi_put(addr, b);
	} else if (is_csmk1()) {
		if (addr >= CYBERSTORM_MK1_SCSI_OFFSET) {
			cpuboard_ncr9x_scsi_put(addr, b);
		} else {
			flash_write(flashrom, addr, b);
		}
	} else if (is_csmk2()) {
		if (addr >= CYBERSTORM_MK2_SCSI_OFFSET) {
			cpuboard_ncr9x_scsi_put(addr, b);
		}  else {
			addr &= ~3;
			addr |= csmk2_flashaddressing;
			flash_write(flashrom, addr, b);
		}
	}
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
		write_log(_T("Blizzard/CyberStorm Z2 autoconfigured at %02X0000\n"), b);
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
	uae_u8 *dst = kickmem_bank.baseaddr;
	protect_roms(false);
	memcpy(dst, src, 524288);
	protect_roms(true);
}
static void cyberstorm_copymaprom(void)
{
	if (blizzardmaprom_bank.baseaddr) {
		uae_u8 *src = blizzardmaprom_bank.baseaddr;
		uae_u8 *dst = kickmem_bank.baseaddr;
		protect_roms(false);
		memcpy(dst, src, 524288);
		protect_roms(true);
	}
}
static void cyberstormmk2_copymaprom(void)
{
	if (blizzardmaprom_bank.baseaddr) {
		uae_u8 *src = a3000hmem_bank.baseaddr + a3000hmem_bank.allocated - 524288;
		uae_u8 *dst = kickmem_bank.baseaddr;
		protect_roms(false);
		memcpy(dst, src, 524288);
		protect_roms(true);
	}
}
static void cyberstormmk1_copymaprom(void)
{
	if (blizzardmaprom_bank.baseaddr) {
		uae_u8 *src = blizzardmaprom_bank.baseaddr;
		uae_u8 *dst = kickmem_bank.baseaddr;
		protect_roms(false);
		memcpy(dst, src, 524288);
		protect_roms(true);
	}
}

void cpuboard_rethink(void)
{
	if (is_csmk3() || is_blizzardppc()) {
		if (!(io_reg[CSIII_REG_IRQ] & (P5_IRQ_SCSI_EN | P5_IRQ_SCSI))) {
			INTREQ(0x8000 | 0x0008);
		}
		if (!(io_reg[CSIII_REG_IRQ] & (P5_IRQ_PPC_1 | P5_IRQ_PPC_2))) {
			INTREQ(0x8000 | 0x0008);
		}
		ppc_interrupt(intlev());
	}
}

static void blizzardppc_maprom(void)
{
	if (cpuboard_size <= 2 * 524288)
		return;
	if (maprom_state) {
		map_banks(&blizzardmaprom2_bank, CYBERSTORM_MAPROM_BASE >> 16, 524288 >> 16, 0);
	} else {
		map_banks(&blizzardmaprom_bank, CYBERSTORM_MAPROM_BASE >> 16, 524288 >> 16, 0);
	}
}
static void cyberstorm_maprom(void)
{
	if (a3000hmem_bank.allocated <= 2 * 524288)
		return;
	if (maprom_state && is_ppc())
		map_banks(&blizzardmaprom2_bank, CYBERSTORM_MAPROM_BASE >> 16, 524288 >> 16, 0);
	else
		map_banks(&blizzardmaprom_bank, CYBERSTORM_MAPROM_BASE >> 16, 524288 >> 16, 0);
}

static void cyberstormmk2_maprom(void)
{
	if (maprom_state)
		map_banks_nojitdirect(&blizzardmaprom_bank, blizzardmaprom_bank.start >> 16, 524288 >> 16, 0);
}

void cyberstorm_irq(int level)
{
	if (level)
		io_reg[CSIII_REG_IRQ] &= ~P5_IRQ_SCSI;
	else
		io_reg[CSIII_REG_IRQ] |= P5_IRQ_SCSI;
	cpuboard_rethink();
}

void blizzardppc_irq(int level)
{
	if (level)
		io_reg[CSIII_REG_IRQ] &= ~P5_IRQ_SCSI;
	else
		io_reg[CSIII_REG_IRQ] |= P5_IRQ_SCSI;
	cpuboard_rethink();
}


static uae_u32 REGPARAM2 blizzardio_bget(uaecptr addr)
{
	uae_u8 v = 0;
#ifdef JIT
	special_mem |= S_READ;
#endif
	//write_log(_T("CS IO XBGET %08x=%02X PC=%08x\n"), addr, v & 0xff, M68K_GETPC);
	if (is_csmk3() || is_blizzardppc()) {
		uae_u32 bank = addr & 0x10000;
		if (bank == 0) {
			int reg = (addr & 0xff) / 8;
			v = io_reg[reg];
			if (reg == CSIII_REG_LOCK) {
				v &= 0x01;
				v |= 0x10;
				if (is_blizzardppc())
					v |= 0x08; // BPPC identification
			} else if (reg == CSIII_REG_IRQ)  {
				v &= 0x3f;
			} else if (reg == CSIII_REG_INT) {
				v |= 0x40;
				v &= ~0x10;
			} else if (reg == CSIII_REG_SHADOW) {
				v |= 0x08;
			} else if (reg == CSIII_REG_RESET) {
				v &= 0x1f;
			} else if (reg == CSIII_REG_IPL_EMU) {
				// PPC not master: m68k IPL = all ones
				if (io_reg[CSIII_REG_INT] & P5_INT_MASTER)
					v |= P5_M68k_IPL_MASK;
			}
#if CPUBOARD_IO_LOG > 0
			if (reg != CSIII_REG_IRQ || CPUBOARD_IO_LOG > 2)
				write_log(_T("CS IO BGET %08x=%02X PC=%08x\n"), addr, v & 0xff, M68K_GETPC);
#endif
		} else {
#if CPUBOARD_IO_LOG > 0
			write_log(_T("CS IO BGET %08x=%02X PC=%08x\n"), addr, v & 0xff, M68K_GETPC);
#endif
		}
	}
	return v;
}
static uae_u32 REGPARAM2 blizzardio_wget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	if (is_csmk3() || is_blizzardppc()) {
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
	write_log(_T("CS IO LGET %08x PC=%08x\n"), addr, M68K_GETPC);
	if (is_blizzard2060() && currprefs.maprom) {
		if (addr & 0x10000000) {
			maprom_state = 0;
		} else {
			maprom_state = 1;
		}
	}
	return 0;
}
static void REGPARAM2 blizzardio_bput(uaecptr addr, uae_u32 v)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
#if CPUBOARD_IO_LOG > 1
	write_log(_T("CS IO XBPUT %08x %02x PC=%08x\n"), addr, v & 0xff, M68K_GETPC);
#endif
	if (is_csmk2()) {
		csmk2_flashaddressing = addr & 3;
		if (addr == 0x880000e3 && v == 0x2a) {
			maprom_state = 1;
			write_log(_T("CSMKII: MAPROM enabled\n"));
			cyberstormmk2_copymaprom();
		}
	} else if (is_blizzard()) {
		if ((addr & 65535) == (BLIZZARD_MAPROM_ENABLE & 65535)) {
			if (v != 0x42 || maprom_state || !currprefs.maprom)
				return;
			maprom_state = 1;
			write_log(_T("Blizzard: MAPROM enabled\n"));
			blizzard_copymaprom();
		}
	} else if (is_csmk3() || is_blizzardppc()) {
#if CPUBOARD_IO_LOG > 0
		write_log(_T("CS IO BPUT %08x %02x PC=%08x\n"), addr, v & 0xff, M68K_GETPC);
#endif
		uae_u32 bank = addr & 0x10000;
		if (bank == 0) {
			addr &= 0xff;
			if (is_blizzardppc()) {
				if (addr == BPPC_UNLOCK_FLASH && v == BPPC_MAGIC_UNLOCK_VALUE) {
					flash_unlocked = 1;
					write_log(_T("BPPC: flash unlocked\n"));
				} else 	if (addr == BPPC_LOCK_FLASH) {
					flash_unlocked = 0;
					write_log(_T("BPPC: flash locked\n"));
				} else if (addr == BPPC_MAPROM_ON) {
					write_log(_T("BPPC: maprom enabled\n"));
					maprom_state = 1;
					cyberstorm_copymaprom();
					blizzardppc_maprom();
				} else if (addr == BPPC_MAPROM_OFF) {
					write_log(_T("BPPC: maprom disabled\n"));
					maprom_state = 0;
					blizzardppc_maprom();
				}
			}
			addr /= 8;
			uae_u8 oldval = io_reg[addr];
			if (addr == CSIII_REG_LOCK) {
				uae_u8 regval = io_reg[CSIII_REG_LOCK] & 0x0f;
				if (v == P5_MAGIC1)
					regval |= P5_MAGIC1;
				else if ((v & 0x70) == P5_MAGIC2 && (io_reg[CSIII_REG_LOCK] & 0x70) == P5_MAGIC1)
					regval |= P5_MAGIC2;
				else if ((v & 0x70) == P5_MAGIC3 && (io_reg[CSIII_REG_LOCK] & 0x70) == P5_MAGIC2)
					regval |= P5_MAGIC3;
				else if ((v & 0x70) == P5_MAGIC4 && (io_reg[CSIII_REG_LOCK] & 0x70) == P5_MAGIC3)
					regval |= P5_MAGIC4;
				if ((regval & 0x70) == P5_MAGIC3)
					flash_unlocked = 1;
				else
					flash_unlocked &= ~2;
				if (v & P5_LOCK_CPU) {
					if (v & 0x80) {
						if (uae_ppc_cpu_unlock())
							regval |= P5_LOCK_CPU;
					} else {
						if (!(regval & P5_LOCK_CPU))
							uae_ppc_cpu_lock();
						regval &= ~P5_LOCK_CPU;
					}
#if CPUBOARD_IO_LOG > 0
					write_log(_T("CSIII_REG_LOCK bit 0 = %d!\n"), (v & 0x80) ? 1 : 0);
#endif
				}
				io_reg[CSIII_REG_LOCK] = regval;
			} else {
				uae_u32 regval;
				// reg shadow is only writable if unlock sequence is active
				if (addr == CSIII_REG_SHADOW) {
					if (v & 2) {
						// for some reason this unknown bit can be modified even when locked
						io_reg[CSIII_REG_LOCK] &= ~2;
						if (v & 0x80)
							io_reg[CSIII_REG_LOCK] |= 2;
					}
					if ((io_reg[CSIII_REG_LOCK] & 0x70) != P5_MAGIC3)
						return;
				}
				if (v & 0x80)
					io_reg[addr] |= v & 0x7f;
				else
					io_reg[addr] &= ~v;
				regval = io_reg[addr];
				if (addr == CSIII_REG_RESET) {
					map_banks(&dummy_bank, 0xf00000 >> 16, 0x80000 >> 16, 0);
					map_banks(&blizzardio_bank, 0xf60000 >> 16, 0x10000 >> 16, 0);
					if (regval & P5_SCSI_RESET) {
						if ((regval & P5_SCSI_RESET) != (oldval & P5_SCSI_RESET))
							write_log(_T("CS: SCSI reset cleared\n"));
						map_banks(&blizzardf0_bank, 0xf00000 >> 16, 0x40000 >> 16, 0);
						if (is_blizzardppc() || flash_size(flashrom) >= 262144) {
							map_banks(&ncr_bank_blizzardppc, 0xf40000 >> 16, 0x10000 >> 16, 0);
						} else {
							map_banks(&ncr_bank_cyberstorm, 0xf40000 >> 16, 0x10000 >> 16, 0);
							map_banks(&blizzardio_bank, 0xf50000 >> 16, 0x10000 >> 16, 0);
						}
					} else {
						if ((regval & P5_SCSI_RESET) != (oldval & P5_SCSI_RESET))
							write_log(_T("CS: SCSI reset\n"));
						map_banks(&blizzardf0_bank, 0xf00000 >> 16, 0x60000 >> 16, 0);
					}
					if ((oldval & P5_PPC_RESET) && !(regval & P5_PPC_RESET)) {
						uae_ppc_cpu_stop();
					} else if (!(oldval & P5_PPC_RESET) && (regval & P5_PPC_RESET)) {
						uae_ppc_cpu_reboot();
					}
					if ((regval & P5_M68K_RESET) && !(oldval & P5_M68K_RESET)) {
						m68k_reset();
						write_log(_T("CS: M68K Reset\n"));
					} else if (!(regval & P5_M68K_RESET) && (oldval & P5_M68K_RESET)) {
						write_log(_T("CS: M68K Halted\n"));
						if (!(regval & P5_PPC_RESET)) {
							write_log(_T("CS: PPC is also halted. STOP.\n"));
							cpu_halt(5);
						} else {
							// halt 68k, leave ppc message processing active.
							cpu_halt(-1);
						}
					}
					if (!(io_reg[CSIII_REG_SHADOW] & P5_SELF_RESET)) {
						if (!(regval & P5_AMIGA_RESET)) {
							uae_reset(0, 0);
							write_log(_T("CS: Amiga Reset\n"));
							io_reg[addr] |= P5_AMIGA_RESET;
						}
					} else {
						io_reg[CSIII_REG_RESET] &= ~P5_AMIGA_RESET;
						io_reg[CSIII_REG_RESET] |= oldval & P5_AMIGA_RESET;
					}
				} else if (addr == CSIII_REG_IPL_EMU) {
#if CPUBOARD_IRQ_LOG > 0
					regval &= ~P5_M68k_IPL_MASK;
					regval |= oldval & P5_M68k_IPL_MASK;
					io_reg[addr] = regval;
					if ((regval & P5_DISABLE_INT) != (oldval & P5_DISABLE_INT))
						write_log(_T("CS: interrupt state: %s\n"), (regval & P5_DISABLE_INT) ? _T("disabled") : _T("enabled"));
					if ((regval & P5_PPC_IPL_MASK) != (oldval & P5_PPC_IPL_MASK))
						write_log(_T("CS: PPC IPL %02x\n"), (~regval) & P5_PPC_IPL_MASK);
#endif
					check_ppc_int_lvl();
				} else if (addr == CSIII_REG_INT) {
#if CPUBOARD_IRQ_LOG > 0
					if ((regval & P5_INT_MASTER) != (oldval & P5_INT_MASTER))
						write_log(_T("CS: interrupt master: %s\n"), (regval & P5_INT_MASTER) ? _T("m68k") : _T("ppc"));
					if ((regval & P5_ENABLE_IPL) != (oldval & P5_ENABLE_IPL))
						write_log(_T("CS: IPL state: %s\n"), (regval & P5_ENABLE_IPL) ? _T("disabled") : _T("enabled"));
#endif
					check_ppc_int_lvl();
				} else if (addr == CSIII_REG_INT_LVL) {
#if CPUBOARD_IRQ_LOG > 0
					if (regval != oldval)
						write_log(_T("CS: interrupt level: %02x\n"), regval);
#endif
					check_ppc_int_lvl();
				} else if (addr == CSIII_REG_SHADOW) {
					if (is_csmk3() && ((oldval ^ regval) & 1)) {
						maprom_state = (regval & 1) ? 0 : 1;
						write_log(_T("CyberStorm MAPROM = %d\n"), maprom_state);
						cyberstorm_copymaprom();
						cyberstorm_maprom();
					}
					if ((regval & P5_FLASH) != (oldval & P5_FLASH)) {
						flash_unlocked = (regval & P5_FLASH) ? 0 : 1;
						write_log(_T("CS: Flash writable = %d\n"), flash_unlocked);
					}
//					if ((oldval ^ regval) & 7) {
//						activate_debugger();
//					}
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
		write_log(_T("CS IO WPUT %08x %04x\n"), addr, v);
		if((addr & 65535) == (BLIZZARD_BOARD_DISABLE & 65535)) {
			if (v != 0xcafe)
				return;
			write_log(_T("Blizzard: board disable!\n"));
			cpu_halt(4); // not much choice..
		}
	} else if (is_csmk3() || is_blizzardppc()) {
		write_log(_T("CS IO WPUT %08x %04x\n"), addr, v);
	} else if (is_csmk2()) {
		write_log(_T("CS IO WPUT %08x %04x\n"), addr, v);
		if (addr == CSMK2_BOARD_DISABLE) {
			if (v != 0xcafe)
				return;
			write_log(_T("CSMK2: board disable!\n"));
			cpu_halt(4); // not much choice..
		}
	}
}
static void REGPARAM2 blizzardio_lput(uaecptr addr, uae_u32 v)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	write_log(_T("CS IO LPUT %08x %08x\n"), addr, v);
	if (is_csmk1()) {
		if (addr == 0x80f80000) {
			maprom_state = 1;
			cyberstormmk1_copymaprom();
		}
	}
	if (is_blizzard2060() && currprefs.maprom) {
		if (addr & 0x10000000) {
			maprom_state = 0;
		} else {
			maprom_state = 1;
		}
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
			blizzardppc_maprom();
		}
	}
	if (is_csmk3()) {
		map_banks(&blizzardf0_bank, 0xf00000 >> 16, 0x40000 >> 16, 0);
		map_banks(&blizzardio_bank, 0xf50000 >> 16, (3 * 65536) >> 16, 0);
		cyberstorm_maprom();
	}
	if (is_csmk2()) {
		map_banks(&blizzardio_bank, 0x88000000 >> 16, 65536 >> 16, 0);
		map_banks(&blizzardio_bank, 0x83000000 >> 16, 65536 >> 16, 0);
		map_banks(&blizzardf0_bank, 0xf00000 >> 16, 65536 >> 16, 0);
		cyberstormmk2_maprom();
	}
	if (is_csmk1()) {
		map_banks(&blizzardio_bank, 0x80f80000 >> 16, 65536 >> 16, 0);
		map_banks(&blizzardf0_bank, 0xf00000 >> 16, 65536 >> 16, 0);
		map_banks(&blizzardmaprom_bank, 0x07f80000 >> 16, 524288 >> 16, 0);
	}
	if (is_blizzard2060()) {
		map_banks(&blizzardf0_bank, 0xf00000 >> 16, 65536 >> 16, 0);
		map_banks(&blizzardio_bank, 0x80000000 >> 16, 0x10000000 >> 16, 0);
		if (currprefs.maprom)
			map_banks_nojitdirect(&blizzardmaprom_bank, (a3000hmem_bank.start + a3000hmem_bank.allocated - 524288) >> 16, 524288 >> 16, 0);
	}
}

void cpuboard_reset(bool hardreset)
{
	if (is_blizzard() || is_blizzardppc())
		canbang = 0;
	configured = false;
	delayed_rom_protect = 0;
	currprefs.cpuboardmem1_size = changed_prefs.cpuboardmem1_size;
	if (hardreset || (!currprefs.maprom && (is_blizzard() || is_blizzard2060())))
		maprom_state = 0;
	if (is_csmk3() || is_blizzardppc()) {
		if (hardreset)
			memset(io_reg, 0x7f, sizeof io_reg);
		io_reg[CSIII_REG_RESET] = 0x7f;
		io_reg[CSIII_REG_IRQ] = 0x7f;
		io_reg[CSIII_REG_IPL_EMU] = 0x40;
		io_reg[CSIII_REG_LOCK] = 0x01;
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
		mapped_free(&blizzardram_bank);
	} else {
		xfree(blizzardram_nojit_bank.baseaddr);
	}
	if (blizzardmaprom_bank_mapped)
		mapped_free(&blizzardmaprom_bank);
	if (blizzardmaprom2_bank_mapped)
		mapped_free(&blizzardmaprom2_bank);

	blizzardram_bank.baseaddr = NULL;
	blizzardram_nojit_bank.baseaddr = NULL;
	blizzardmaprom_bank.baseaddr = NULL;
	blizzardmaprom2_bank.baseaddr = NULL;
	blizzardmaprom_bank_mapped = false;
	blizzardmaprom2_bank_mapped = false;

	mapped_free(&blizzardf0_bank);
	blizzardf0_bank.baseaddr = NULL;

	mapped_free(&blizzardea_bank);
	blizzardea_bank.baseaddr = NULL;

	cpuboard_size = cpuboard2_size = -1;

	blizzardmaprom_bank.flags &= ~(ABFLAG_INDIRECT | ABFLAG_NOALLOC);
	blizzardmaprom2_bank.flags &= ~(ABFLAG_INDIRECT | ABFLAG_NOALLOC);
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
		blizzardram_bank.startmask = BLIZZARD_RAM_BASE;

		blizzardram_nojit_bank.start = blizzardram_bank.start;
		blizzardram_nojit_bank.allocated = blizzardram_bank.allocated;
		blizzardram_nojit_bank.mask = blizzardram_bank.mask;
		blizzardram_nojit_bank.startmask = blizzardram_bank.startmask;


		blizzard_jit = 0 && BLIZZARD_RAM_BASE + blizzardram_bank.allocated <= max_z3fastmem && currprefs.jit_direct_compatible_memory;
		if (blizzard_jit) {
			if (cpuboard_size) {
				blizzardram_bank.label = _T("blizzard");
				mapped_malloc(&blizzardram_bank);
			}
		} else {
			if (cpuboard_size)
				blizzardram_bank.baseaddr = xmalloc(uae_u8, blizzardram_bank.allocated);
		}
		blizzardram_nojit_bank.baseaddr = blizzardram_bank.baseaddr;

		maprom_base = blizzardram_bank.allocated - 524288;

		blizzardf0_bank.start = 0x00f00000;
		blizzardf0_bank.allocated = 524288;
		blizzardf0_bank.mask = blizzardf0_bank.allocated - 1;
		mapped_malloc(&blizzardf0_bank);

		if (!is_blizzardppc()) {
			blizzardea_bank.allocated = 2 * 65536;
			blizzardea_bank.mask = blizzardea_bank.allocated - 1;
			// Blizzard 12xx autoconfig ROM must be mapped at $ea0000-$ebffff, board requires it.
			mapped_malloc(&blizzardea_bank);
		}

		if (cpuboard_size > 2 * 524288) {
			if (!is_blizzardppc()) {
				blizzardmaprom_bank.baseaddr = blizzardram_bank.baseaddr + cpuboard_size - 524288;
				blizzardmaprom_bank.start = BLIZZARD_MAPROM_BASE;
				blizzardmaprom_bank.allocated = 524288;
				blizzardmaprom_bank.mask = 524288 - 1;
				blizzardmaprom_bank.flags |= ABFLAG_INDIRECT | ABFLAG_NOALLOC;
				mapped_malloc(&blizzardmaprom_bank);
				blizzardmaprom_bank_mapped = true;
			} else {
				blizzardmaprom_bank.baseaddr = blizzardram_bank.baseaddr + cpuboard_size - 524288;
				blizzardmaprom_bank.start = CYBERSTORM_MAPROM_BASE;
				blizzardmaprom_bank.allocated = 524288;
				blizzardmaprom_bank.mask = 524288 - 1;
				blizzardmaprom_bank.flags |= ABFLAG_INDIRECT | ABFLAG_NOALLOC;
				mapped_malloc(&blizzardmaprom_bank);
				blizzardmaprom_bank_mapped = true;
				blizzardmaprom2_bank.baseaddr = blizzardram_bank.baseaddr + cpuboard_size - 2 * 524288;
				blizzardmaprom2_bank.start = CYBERSTORM_MAPROM_BASE;
				blizzardmaprom2_bank.allocated = 524288;
				blizzardmaprom2_bank.mask = 524288 - 1;
				blizzardmaprom2_bank.flags |= ABFLAG_INDIRECT | ABFLAG_NOALLOC;
				mapped_malloc(&blizzardmaprom2_bank);
				blizzardmaprom2_bank_mapped = true;
			}
		}

	} else if (is_csmk1()) {

		blizzardf0_bank.start = 0x00f00000;
		blizzardf0_bank.allocated = 65536;
		blizzardf0_bank.mask = blizzardf0_bank.allocated - 1;
		mapped_malloc(&blizzardf0_bank);

		blizzardea_bank.allocated = 2 * 65536;
		blizzardea_bank.mask = 65535 - 1;
		mapped_malloc(&blizzardea_bank);

		blizzardmaprom_bank.allocated = 524288;
		blizzardmaprom_bank.start = 0x07f80000;
		blizzardmaprom_bank.mask = 524288 - 1;
		blizzardmaprom_bank_mapped = true;
		mapped_malloc(&blizzardmaprom_bank);

	} else if (is_csmk2() || is_blizzard2060()) {

		blizzardea_bank.allocated = 2 * 65536;
		blizzardea_bank.mask = blizzardea_bank.allocated - 1;
		mapped_malloc(&blizzardea_bank);

		blizzardf0_bank.start = 0x00f00000;
		blizzardf0_bank.allocated = 65536;
		blizzardf0_bank.mask = blizzardf0_bank.allocated - 1;
		mapped_malloc(&blizzardf0_bank);

		blizzardmaprom_bank.baseaddr = a3000hmem_bank.baseaddr + a3000hmem_bank.allocated - 524288;
		blizzardmaprom_bank.start = a3000hmem_bank.start + a3000hmem_bank.allocated - 524288;
		blizzardmaprom_bank.allocated = 524288;
		blizzardmaprom_bank.mask = 524288 - 1;
		blizzardmaprom_bank.flags |= ABFLAG_INDIRECT | ABFLAG_NOALLOC;
		mapped_malloc(&blizzardmaprom_bank);

	} else if (is_csmk3()) {
	
		blizzardram_bank.start = CS_RAM_BASE;
		blizzardram_bank.allocated = cpuboard_size;
		blizzardram_bank.mask = blizzardram_bank.allocated - 1;
		if (cpuboard_size) {
			blizzardram_bank.label = _T("cyberstorm");
			mapped_malloc(&blizzardram_bank);
		}

		blizzardf0_bank.start = 0x00f00000;
		blizzardf0_bank.allocated = 524288;
		blizzardf0_bank.mask = blizzardf0_bank.allocated - 1;
		mapped_malloc(&blizzardf0_bank);

		if (a3000hmem_bank.allocated > 2 * 524288) {
			blizzardmaprom_bank.flags |= ABFLAG_INDIRECT | ABFLAG_NOALLOC;
			blizzardmaprom_bank.baseaddr = a3000hmem_bank.baseaddr + a3000hmem_bank.allocated - 1 * 524288;
			blizzardmaprom_bank.start = CYBERSTORM_MAPROM_BASE;
			blizzardmaprom_bank.allocated = 524288;
			blizzardmaprom_bank.mask = 524288 - 1;
			mapped_malloc(&blizzardmaprom_bank);
			blizzardmaprom_bank_mapped = true;
			blizzardmaprom2_bank.flags |= ABFLAG_INDIRECT | ABFLAG_NOALLOC;
			blizzardmaprom2_bank.baseaddr = a3000hmem_bank.baseaddr + a3000hmem_bank.allocated - 2 * 524288;
			blizzardmaprom2_bank.start = CYBERSTORM_MAPROM_BASE;
			blizzardmaprom2_bank.allocated = 524288;
			blizzardmaprom2_bank.mask = 524288 - 1;
			mapped_malloc(&blizzardmaprom2_bank);
			blizzardmaprom2_bank_mapped = true;
		}
	}
}

void cpuboard_clear(void)
{
}

bool is_ppc_cpu(struct uae_prefs *p)
{
	return p->cpuboard_type == BOARD_CSPPC || p->cpuboard_type == BOARD_BLIZZARDPPC;
}

bool cpuboard_maprom(void)
{
	if (!currprefs.cpuboard_type || !cpuboard_size)
		return false;
	if (is_blizzard() || is_blizzardppc()) {
		if (maprom_state)
			blizzard_copymaprom();
	} else if (is_csmk3()) {
		if (maprom_state)
			cyberstorm_copymaprom();
	}
	return true;
}

bool cpuboard_08000000(struct uae_prefs *p)
{
	switch (p->cpuboard_type)
	{
		case BOARD_BLIZZARD_2060:
		case BOARD_CSMK1:
		case BOARD_CSMK2:
		case BOARD_CSMK3:
		case BOARD_CSPPC:
		case BOARD_WARPENGINE_A4000:
		return true;
	}
	return false;
}

bool cpuboard_blizzardram(struct uae_prefs *p)
{
	switch (p->cpuboard_type)
	{
		case BOARD_BLIZZARD_1230_IV:
		case BOARD_BLIZZARD_1260:
		case BOARD_BLIZZARDPPC:
		return true;
	}
	return false;
}

static void fixserial(uae_u8 *rom, int size)
{
	uae_u8 value1 = rom[16];
	uae_u8 value2 = rom[17];
	uae_u8 value3 = rom[18];
	int seroffset = 17, longseroffset = 24;
	uae_u32 serialnum = 0x1234;
	char serial[10];

#if 0
	return;
#endif

	if (currprefs.cpuboard_type == BOARD_BLIZZARDPPC) {
		value1 = 'I';
		value2 = 'D';
		if (currprefs.cpu_model == 68060)
			value3 = 'H';
		else if (currprefs.fpu_model)
			value3 = 'B';
		else
			value3 = 'A';
		seroffset = 19;
		sprintf(serial, "%04X", serialnum);
	} else if (currprefs.cpuboard_type == BOARD_CSPPC) {
		value1 = 'D';
		value2 = 'B';
		sprintf(serial, "%05X", serialnum);
		value3 = 0;
		seroffset = 18;
	} else if (currprefs.cpuboard_type == BOARD_CSMK3) {
		value1 = 'F';
		sprintf(serial, "%05X", serialnum);
		value2 = value3 = 0;
	} else {
		return;
	}

	rom[16] = value1;
	if (value2)
		rom[17] = value2;
	if (value3)
		rom[18] = value3;
	if (rom[seroffset] == 0) {
		strcpy((char*)rom + seroffset, serial);
		if (longseroffset) {
			rom[longseroffset + 0] = serialnum >> 24;
			rom[longseroffset + 1] = serialnum >> 16;
			rom[longseroffset + 2] = serialnum >>  8;
			rom[longseroffset + 3] = serialnum >>  0;
		}
	}
}

static struct zfile *flashfile_open(const TCHAR *name)
{
	struct zfile *f;
	TCHAR path[MAX_DPATH];

	if (!name[0])
		return NULL;
	f = zfile_fopen(name, _T("rb+"), ZFD_NONE);
	if (!f) {
		f = zfile_fopen(name, _T("rb"), ZFD_NORMAL);
		if (!f) {
			fetch_rompath(path, sizeof path / sizeof(TCHAR));
			_tcscat(path, name);
			f = zfile_fopen(path, _T("rb+"), ZFD_NONE);
			if (!f)
				f = zfile_fopen(path, _T("rb"), ZFD_NORMAL);
		}
	}
	if (f)
		write_log(_T("Accelerator board flash file '%s' loaded.\n"), name);
	return f;
}

static struct zfile *board_rom_open(int *roms, const TCHAR *name)
{
	struct zfile *zf = NULL;
	struct romlist *rl = getromlistbyids(roms);
	if (rl)
		zf = read_rom(rl->rd);
	if (!zf) {
		TCHAR path[MAX_DPATH];
		fetch_rompath(path, sizeof path / sizeof(TCHAR));
		_tcscat(path, name);
		zf = zfile_fopen(path, _T("rb"), ZFD_NORMAL);
	}
	return zf;
}

addrbank *cpuboard_autoconfig_init(void)
{
	struct zfile *autoconfig_rom = NULL;
	int roms[2], roms2[2];
	bool autoconf = true;
	const TCHAR *defaultromname = NULL;
	const TCHAR *romname = currprefs.acceleratorromfile;
	bool isflashrom = false;
	struct romdata *rd = NULL;

	roms[0] = -1;
	roms[1] = -1;
	roms2[0] = -1;
	roms2[1] = -1;
	switch (currprefs.cpuboard_type)
	{
	case BOARD_BLIZZARD_1230_IV_SCSI:
		roms2[0] = 94;
	case BOARD_BLIZZARD_1230_IV:
		roms[0] = 89;
		break;
	case BOARD_BLIZZARD_1260_SCSI:
		roms2[0] = 94;
	case BOARD_BLIZZARD_1260:
		roms[0] = 90;
		break;
	case BOARD_BLIZZARD_2060:
		roms[0] = 92;
		break;
	case BOARD_WARPENGINE_A4000:
		return &expamem_null;
	case BOARD_CSMK1:
		roms[0] = currprefs.cpu_model == 68040 ? 95 : 101;
		isflashrom = true;
		break;
	case BOARD_CSMK2:
		roms[0] = 96;
		isflashrom = true;
		break;
	case BOARD_CSMK3:
		roms[0] = 97;
		isflashrom = true;
		break;
	case BOARD_CSPPC:
		roms[0] = 98;
		isflashrom = true;
		break;
	case BOARD_BLIZZARDPPC:
		roms[0] = currprefs.cpu_model == 68040 ? 99 : 100;
		isflashrom = true;
		break;
	default:
		return &expamem_null;
	}

	struct romlist *rl = getromlistbyids(roms);
	if (!rl) {
		rd = getromlistbyidsallroms(roms);
		if (!rd)
			return &expamem_null;
	} else {
		rd = rl->rd;
	}
	defaultromname = rd->defaultfilename;
	if (rl && !isflashrom) {
		autoconfig_rom = zfile_fopen(romname, _T("rb"));
		if (!autoconfig_rom)
			autoconfig_rom = read_rom(rl->rd);
	} else if (isflashrom) {
		autoconfig_rom = flashfile_open(romname);
		if (!autoconfig_rom) {
			if (rl)
				autoconfig_rom = flashfile_open(rl->path);
			if (!autoconfig_rom)
				autoconfig_rom = flashfile_open(defaultromname);
		}
		if (!autoconfig_rom) {
			romwarning(roms);
			write_log(_T("Couldn't open CPU board rom '%s'\n"), defaultromname);
			return &expamem_null;
		}
	}

	if (!autoconfig_rom && roms[0] != -1) {
		romwarning(roms);
		write_log (_T("ROM id %d not found for CPU board emulation\n"), roms[0]);
		return &expamem_null;
	}
	protect_roms(false);
	if (is_blizzard2060()) {
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
	} else if (is_csmk1()) {
		earom_size = 131072;
		f0rom_size = 65536;
		for (int i = 0; i < 32768; i++) {
			uae_u8 b = 0xff;
			zfile_fread(&b, 1, 1, autoconfig_rom);
			blizzardea_bank.baseaddr[i * 2 + 0] = b;
			blizzardea_bank.baseaddr[i * 2 + 1] = 0xff;
		}
		zfile_fread(blizzardea_bank.baseaddr + 65536, 65536, 1, autoconfig_rom);
		if (zfile_needwrite(autoconfig_rom)) {
			flashrom_file = autoconfig_rom;
			autoconfig_rom = NULL;
		}
		flashrom = flash_new(blizzardea_bank.baseaddr, earom_size, earom_size, 0x20, flashrom_file);
		memcpy(blizzardf0_bank.baseaddr, blizzardea_bank.baseaddr + 65536, 65536);
	} else if (is_csmk2()) {
		earom_size = 131072;
		f0rom_size = 65536;
		zfile_fread(blizzardea_bank.baseaddr, earom_size, 1, autoconfig_rom);
		if (zfile_needwrite(autoconfig_rom)) {
			flashrom_file = autoconfig_rom;
			autoconfig_rom = NULL;
		}
		flashrom = flash_new(blizzardea_bank.baseaddr, earom_size, earom_size, 0x20, flashrom_file);
		memcpy(blizzardf0_bank.baseaddr, blizzardea_bank.baseaddr + 65536, 65536);
	} else if (is_csmk3() || is_blizzardppc()) {
		uae_u8 flashtype;
		earom_size = 0;
		if (is_blizzardppc()) {
			flashtype = 0xa4;
			f0rom_size = 524288;
		} else {
			flashtype = 0x20;
			f0rom_size = 131072;
			if (zfile_size(autoconfig_rom) >= 262144) {
				flashtype = 0xa4;
				f0rom_size = 524288;
			}
		}
		// empty rom space must be cleared, not set to ones.
		memset(blizzardf0_bank.baseaddr, 0x00, f0rom_size);
		if (f0rom_size == 524288) // but empty config data must be 0xFF
			memset(blizzardf0_bank.baseaddr + 0x50000, 0xff, 0x10000);
		zfile_fread(blizzardf0_bank.baseaddr, f0rom_size, 1, autoconfig_rom);
		autoconf = false;
		if (zfile_needwrite(autoconfig_rom)) {
			flashrom_file = autoconfig_rom;
			autoconfig_rom = NULL;
		}
		fixserial(blizzardf0_bank.baseaddr, f0rom_size);
		flashrom = flash_new(blizzardf0_bank.baseaddr, f0rom_size, f0rom_size, flashtype, flashrom_file);
	} else {
		// 1230 MK IV / 1240/60
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
		zfile_fclose(autoconfig_rom);
		autoconfig_rom = NULL;
		if (roms2[0] != -1) {
			defaultromname = _T("blizzard_scsi_kit_iv.rom");
			autoconfig_rom = board_rom_open(roms2, currprefs.acceleratorextromfile);
			if (!autoconfig_rom)
				autoconfig_rom = board_rom_open(roms2, defaultromname);
			if (autoconfig_rom) {
				memset(blizzardea_bank.baseaddr + 0x10000, 0xff, 65536);
				zfile_fread(blizzardea_bank.baseaddr + 0x10000, 32768, 1, autoconfig_rom);
			} else {
				romwarning(roms2);
				write_log(_T("Blizzard SCSI Kit IV ROM not found\n"));
			}
		}
	}
	protect_roms(true);
	zfile_fclose(autoconfig_rom);

	if (f0rom_size)
		map_banks(&blizzardf0_bank, 0xf00000 >> 16, (f0rom_size > 262144 ? 262144 : f0rom_size) >> 16, 0);
	if (!autoconf)
		return &expamem_null;
	return &blizzarde8_bank;
}
