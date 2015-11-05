/*
* UAE - The Un*x Amiga Emulator
*
* AutoConfig (tm) Expansions (ZorroII/III)
*
* Copyright 1996,1997 Stefan Reinauer <stepan@linux.de>
* Copyright 1997 Brian King <Brian_King@Mitel.com>
*   - added gfxcard code
*
*/

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "uae.h"
#include "memory.h"
#include "rommgr.h"
#include "custom.h"
#include "newcpu.h"
#include "savestate.h"
#include "zfile.h"
#include "catweasel.h"
#include "cdtv.h"
#include "threaddep/thread.h"
#include "a2091.h"
#include "a2065.h"
#include "gfxboard.h"
#include "cd32_fmv.h"
#include "ncr_scsi.h"
#include "ncr9x_scsi.h"
#include "scsi.h"
#include "debug.h"
#include "gayle.h"
#include "idecontrollers.h"
#include "cpuboard.h"
#include "sndboard.h"
#include "uae/ppc.h"
#include "autoconf.h"
#include "specialmonitors.h"
#include "inputdevice.h"
#include "pci.h"
#include "x86.h"
#include "filesys.h"

// More information in first revision HRM Appendix_G
#define BOARD_PROTOAUTOCONFIG 1

#define BOARD_AUTOCONFIG_Z2 2
#define BOARD_AUTOCONFIG_Z3 3
#define BOARD_NONAUTOCONFIG_BEFORE 4
#define BOARD_NONAUTOCONFIG_AFTER_Z2 5
#define BOARD_NONAUTOCONFIG_AFTER_Z3 6
#define BOARD_IGNORE 7

#define KS12_BOOT_HACK 1

#define EXP_DEBUG 0

#define MAX_EXPANSION_BOARD_SPACE 16

/* ********************************************************** */
/* 00 / 02 */
/* er_Type */

#define Z2_MEM_8MB		0x00 /* Size of Memory Block */
#define Z2_MEM_4MB		0x07
#define Z2_MEM_2MB		0x06
#define Z2_MEM_1MB		0x05
#define Z2_MEM_512KB	0x04
#define Z2_MEM_256KB	0x03
#define Z2_MEM_128KB	0x02
#define Z2_MEM_64KB		0x01
/* extended definitions */
#define Z3_MEM_16MB		0x00
#define Z3_MEM_32MB		0x01
#define Z3_MEM_64MB		0x02
#define Z3_MEM_128MB	0x03
#define Z3_MEM_256MB	0x04
#define Z3_MEM_512MB	0x05
#define Z3_MEM_1GB		0x06

#define chainedconfig	0x08 /* Next config is part of the same card */
#define rom_card	0x10 /* ROM vector is valid */
#define add_memory	0x20 /* Link RAM into free memory list */

/* Type of Expansion Card */
#define protoautoconfig 0x40
#define zorroII		0xc0
#define zorroIII	0x80

/* ********************************************************** */
/* 04 - 06 & 10-16 */

/* Manufacturer */
#define commodore_g	 513 /* Commodore Braunschweig (Germany) */
#define commodore	 514 /* Commodore West Chester */
#define gvp		2017 /* GVP */
#define ass		2102 /* Advanced Systems & Software */
#define hackers_id	2011 /* Special ID for test cards */

/* Card Type */
#define commodore_a2091	    3 /* A2091 / A590 Card from C= */
#define commodore_a2091_ram 10 /* A2091 / A590 Ram on HD-Card */
#define commodore_a2232	    70 /* A2232 Multiport Expansion */
#define ass_nexus_scsi	    1 /* Nexus SCSI Controller */

#define gvp_series_2_scsi   11
#define gvp_iv_24_gfx	    32

/* ********************************************************** */
/* 08 - 0A  */
/* er_Flags */
#define Z3_SS_MEM_SAME		0x00
#define Z3_SS_MEM_AUTO		0x01
#define Z3_SS_MEM_64KB		0x02
#define Z3_SS_MEM_128KB		0x03
#define Z3_SS_MEM_256KB		0x04
#define Z3_SS_MEM_512KB		0x05
#define Z3_SS_MEM_1MB		0x06 /* Zorro III card subsize */
#define Z3_SS_MEM_2MB		0x07
#define Z3_SS_MEM_4MB		0x08
#define Z3_SS_MEM_6MB		0x09
#define Z3_SS_MEM_8MB		0x0a
#define Z3_SS_MEM_10MB		0x0b
#define Z3_SS_MEM_12MB		0x0c
#define Z3_SS_MEM_14MB		0x0d
#define Z3_SS_MEM_defunct1	0x0e
#define Z3_SS_MEM_defunct2	0x0f

#define force_z3	0x10 /* *MUST* be set if card is Z3 */
#define ext_size	0x20 /* Use extended size table for bits 0-2 of er_Type */
#define no_shutup	0x40 /* Card cannot receive Shut_up_forever */
#define care_addr	0x80 /* Z2=Adress HAS to be $200000-$9fffff Z3=1->mem,0=io */

/* ********************************************************** */
/* 40-42 */
/* ec_interrupt (unused) */

#define enable_irq	0x01 /* enable Interrupt */
#define reset_card	0x04 /* Reset of Expansion Card - must be 0 */
#define card_int2	0x10 /* READ ONLY: IRQ 2 active */
#define card_irq6	0x20 /* READ ONLY: IRQ 6 active */
#define card_irq7	0x40 /* READ ONLY: IRQ 7 active */
#define does_irq	0x80 /* READ ONLY: Card currently throws IRQ */

/* ********************************************************** */

/* ROM defines (DiagVec) */

#define rom_4bit	(0x00<<14) /* ROM width */
#define rom_8bit	(0x01<<14)
#define rom_16bit	(0x02<<14)

#define rom_never	(0x00<<12) /* Never run Boot Code */
#define rom_install	(0x01<<12) /* run code at install time */
#define rom_binddrv	(0x02<<12) /* run code with binddrivers */

uaecptr ROM_filesys_resname, ROM_filesys_resid;
uaecptr ROM_filesys_diagentry;
uaecptr ROM_hardfile_resname, ROM_hardfile_resid;
uaecptr ROM_hardfile_init;
int uae_boot_rom_type;
int uae_boot_rom_size; /* size = code size only */
static bool chipdone;

/* ********************************************************** */

struct card_data
{
	addrbank *(*initrc)(struct romconfig*);
	addrbank *(*initnum)(int);
	addrbank *(*map)(void);
	struct romconfig *rc;
	const TCHAR *name;
	int flags;
	int zorro;
};

static struct card_data cards[MAX_EXPANSION_BOARD_SPACE];

static int ecard, cardno, z3num;
static addrbank *expamem_bank_current;

static uae_u16 uae_id;

static bool isnonautoconfig(int v)
{
	return v == BOARD_NONAUTOCONFIG_AFTER_Z2 ||
		v == BOARD_NONAUTOCONFIG_AFTER_Z3 ||
		v == BOARD_NONAUTOCONFIG_BEFORE;
}

static bool ks12orolder(void)
{
	/* check if Kickstart version is below 1.3 */
	return kickstart_version && kickstart_version < 34;
}
static bool ks11orolder(void)
{
	return kickstart_version && kickstart_version < 33;
}


/* ********************************************************** */

/* Please note: ZorroIII implementation seems to work different
* than described in the HRM. This claims that ZorroIII config
* address is 0xff000000 while the ZorroII config space starts
* at 0x00e80000. In reality, both, Z2 and Z3 cards are
* configured in the ZorroII config space. Kickstart 3.1 doesn't
* even do a single read or write access to the ZorroIII space.
* The original Amiga include files tell the same as the HRM.
* ZorroIII: If you set ext_size in er_Flags and give a Z2-size
* in er_Type you can very likely add some ZorroII address space
* to a ZorroIII card on a real Amiga. This is not implemented
* yet.
*  -- Stefan
*
* Surprising that 0xFF000000 isn't used. Maybe it depends on the
* ROM. Anyway, the HRM says that Z3 cards may appear in Z2 config
* space, so what we are doing here is correct.
*  -- Bernd
*/

/* Autoconfig address space at 0xE80000 */
static uae_u8 expamem[65536];

static uae_u8 expamem_lo;
static uae_u16 expamem_hi;
static uaecptr expamem_z3_sum;
uaecptr expamem_z3_pointer;
uaecptr expamem_z2_pointer;
uae_u32 expamem_z3_size;
uae_u32 expamem_z2_size;
static uae_u32 expamem_board_size;
static uae_u32 expamem_board_pointer;
static bool z3hack_override;

void set_expamem_z3_hack_override(bool overridenoz3hack)
{
	z3hack_override = overridenoz3hack;
}

bool expamem_z3hack(struct uae_prefs *p)
{
	if (z3hack_override)
		return false;
#ifdef WITH_PPC
	if (regs.halted && ppc_state)
		return false;
#endif
	return p->z3_mapping_mode == Z3MAPPING_AUTO || p->z3_mapping_mode == Z3MAPPING_UAE || cpuboard_memorytype(p) == BOARD_MEMORY_BLIZZARD_12xx;
}

/* Ugly hack for >2M chip RAM in single pool
 * We can't add it any later or early boot menu
 * stops working because it sets kicktag at the end
 * of chip ram...
 */
static void addextrachip (uae_u32 sysbase)
{
	if (currprefs.chipmem_size <= 0x00200000)
		return;
	if (sysbase & 0x80000001)
		return;
	if (!valid_address (sysbase, 1000))
		return;
	uae_u32 ml = get_long (sysbase + 322);
	if (!valid_address (ml, 32))
		return;
	uae_u32 next;
	while ((next = get_long (ml))) {
		if (!valid_address (ml, 32))
			return;
		uae_u32 upper = get_long (ml + 24);
		uae_u32 lower = get_long (ml + 20);
		if (lower & ~0xffff) {
			ml = next;
			continue;
		}
		uae_u16 attr = get_word (ml + 14);
		if ((attr & 0x8002) != 2) {
			ml = next;
			continue;
		}
		if (upper >= currprefs.chipmem_size)
			return;
		uae_u32 added = currprefs.chipmem_size - upper;
		uae_u32 first = get_long (ml + 16);
		put_long (ml + 24, currprefs.chipmem_size); // mh_Upper
		put_long (ml + 28, get_long (ml + 28) + added); // mh_Free
		uae_u32 next;
		while (first) {
			next = first;
			first = get_long (next);
		}
		uae_u32 bytes = get_long (next + 4);
		if (next + bytes == 0x00200000) {
			put_long (next + 4, currprefs.chipmem_size - next);
		} else {
			put_long (0x00200000 + 0, 0);
			put_long (0x00200000 + 4, added);
			put_long (next, 0x00200000);
		}
		return;
	}
}

addrbank expamem_null, expamem_none;

DECLARE_MEMORY_FUNCTIONS(expamem);
addrbank expamem_bank = {
	expamem_lget, expamem_wget, expamem_bget,
	expamem_lput, expamem_wput, expamem_bput,
	default_xlate, default_check, NULL, NULL, _T("Autoconfig Z2"),
	dummy_lgeti, dummy_wgeti,
	ABFLAG_IO | ABFLAG_SAFE, S_READ, S_WRITE
};
DECLARE_MEMORY_FUNCTIONS(expamemz3);
static addrbank expamemz3_bank = {
	expamemz3_lget, expamemz3_wget, expamemz3_bget,
	expamemz3_lput, expamemz3_wput, expamemz3_bput,
	default_xlate, default_check, NULL, NULL, _T("Autoconfig Z3"),
	dummy_lgeti, dummy_wgeti,
	ABFLAG_IO | ABFLAG_SAFE, S_READ, S_WRITE
};

static addrbank *expamem_map_clear (void)
{
	write_log (_T("expamem_map_clear() got called. Shouldn't happen.\n"));
	return NULL;
}

static void expamem_init_clear (void)
{
	memset (expamem, 0xff, sizeof expamem);
}
/* autoconfig area is "non-existing" after last device */
static void expamem_init_clear_zero (void)
{
	map_banks(&dummy_bank, 0xe8, 1, 0);
	if (!currprefs.address_space_24)
		map_banks(&dummy_bank, 0xff000000 >> 16, 1, 0);
}

static void expamem_init_clear2 (void)
{
	expamem_bank.name = _T("Autoconfig Z2");
	expamemz3_bank.name = _T("Autoconfig Z3");
	expamem_init_clear_zero ();
	ecard = cardno;
}

static addrbank *expamem_init_last (void)
{
	expamem_init_clear2 ();
	write_log (_T("Memory map after autoconfig:\n"));
	memory_map_dump ();
	return NULL;
}

static uae_u8 REGPARAM2 expamem_read(int addr)
{
	uae_u8 b = (expamem[addr] & 0xf0) | (expamem[addr + 2] >> 4);
	if (addr == 0 || addr == 2 || addr == 0x40 || addr == 0x42)
		return b;
	b = ~b;
	return b;
}

static void REGPARAM2 expamem_write(uaecptr addr, uae_u32 value)
{
	addr &= 0xffff;
	if (addr == 00 || addr == 02 || addr == 0x40 || addr == 0x42) {
		expamem[addr] = (value & 0xf0);
		expamem[addr + 2] = (value & 0x0f) << 4;
	} else {
		expamem[addr] = ~(value & 0xf0);
		expamem[addr + 2] = ~((value & 0x0f) << 4);
	}
}

static int REGPARAM2 expamem_type (void)
{
	return expamem_read(0) & 0xc0;
}

static void call_card_init(int index)
{	
	addrbank *ab, *abe;
	uae_u8 code;
	uae_u32 expamem_z3_pointer_old;

	expamem_bank.name = cards[ecard].name ? cards[ecard].name : _T("None");
	if (cards[ecard].initnum)
		ab = cards[ecard].initnum(0);
	else
		ab = cards[ecard].initrc(cards[ecard].rc);
	expamem_z3_size = 0;
	if (ab == &expamem_none) {
		expamem_init_clear();
		expamem_init_clear_zero();
		map_banks(&expamem_bank, 0xE8, 1, 0);
		if (!currprefs.address_space_24)
			map_banks(&dummy_bank, 0xff000000 >> 16, 1, 0);
		expamem_bank_current = NULL;
		return;
	}
	if (ab == &expamem_null) {
		expamem_next(NULL, NULL);
		return;
	}

	abe = ab;
	if (!abe)
		abe = &expamem_bank;
	if (abe != &expamem_bank) {
		for (int i = 0; i < 16 * 4; i++)
			expamem[i] = abe->bget(i);
	}

	code = expamem_read(0);
	if ((code & 0xc0) == zorroII) {
		// Z2
		code &= 7;
		if (code == 0)
			expamem_z2_size = 8 * 1024 * 1024;
		else
			expamem_z2_size = 32768 << code;

		expamem_board_size = expamem_z2_size;
		expamem_board_pointer = expamem_z2_pointer;

	} else if ((code & 0xc0) == zorroIII) {
		// Z3
		if (expamem_z3_sum < Z3BASE_UAE) {
			expamem_z3_sum = currprefs.z3autoconfig_start;
			if (currprefs.mbresmem_high_size >= 128 * 1024 * 1024 && expamem_z3_sum == Z3BASE_UAE)
				expamem_z3_sum += (currprefs.mbresmem_high_size - 128 * 1024 * 1024) + 16 * 1024 * 1024;
			if (!expamem_z3hack(&currprefs))
				expamem_z3_sum = Z3BASE_REAL;
			if (expamem_z3_sum == Z3BASE_UAE) {
				expamem_z3_sum += currprefs.z3chipmem_size;
			}
		}

		expamem_z3_pointer = expamem_z3_sum;

		code &= 7;
		if (expamem_read(8) & ext_size)
			expamem_z3_size = (16 * 1024 * 1024) << code;
		else
			expamem_z3_size = 16 * 1024 * 1024;
		expamem_z3_sum += expamem_z3_size;

		expamem_z3_pointer_old = expamem_z3_pointer;
		// align 32M boards (FastLane is 32M and needs to be aligned)
		if (expamem_z3_size <= 32 * 1024 * 1024)
			expamem_z3_pointer = (expamem_z3_pointer + expamem_z3_size - 1) & ~(expamem_z3_size - 1);

		expamem_z3_sum += expamem_z3_pointer - expamem_z3_pointer_old;

		expamem_board_size = expamem_z3_size;
		expamem_board_pointer = expamem_z3_pointer;

	} else if ((code & 0xc0) == 0x40) {
		// 0x40 = "Box without init/diagnostic code"
		// proto autoconfig "box" size.
		//expamem_z2_size = (1 << ((code >> 3) & 7)) * 4096;
		// much easier this way, all old-style boards were made for
		// A1000 and didn't have passthrough connector.
		expamem_z2_size = 65536;
		expamem_board_size = expamem_z2_size;
		expamem_board_pointer = expamem_z2_pointer;
	}

	if (ab) {
		// non-NULL: not using expamem_bank
		expamem_bank_current = ab;
		if ((cards[ecard].flags & 1) && currprefs.cs_z3autoconfig && !currprefs.address_space_24) {
			map_banks(&expamemz3_bank, 0xff000000 >> 16, 1, 0);
			map_banks(&dummy_bank, 0xE8, 1, 0);
		} else {
			map_banks(&expamem_bank, 0xE8, 1, 0);
			if (!currprefs.address_space_24)
				map_banks(&dummy_bank, 0xff000000 >> 16, 1, 0);
		}
	} else {
		if ((cards[ecard].flags & 1) && currprefs.cs_z3autoconfig && !currprefs.address_space_24) {
			map_banks(&expamemz3_bank, 0xff000000 >> 16, 1, 0);
			map_banks(&dummy_bank, 0xE8, 1, 0);
			expamem_bank_current = &expamem_bank;
		} else {
			map_banks(&expamem_bank, 0xE8, 1, 0);
			if (!currprefs.address_space_24)
				map_banks(&dummy_bank, 0xff000000 >> 16, 1, 0);
			expamem_bank_current = NULL;
		}
	}
}

static void boardmessage(addrbank *mapped, bool success)
{
	uae_u8 type = expamem_read(0);
	int size = expamem_board_size;
	TCHAR sizemod = 'K';

	size /= 1024;
	if (size > 8 * 1024) {
		sizemod = 'M';
		size /= 1024;
	}
	write_log (_T("Card %d: Z%d 0x%08x %4d%c %s %s%s\n"),
		ecard + 1, (type & 0xc0) == zorroII ? 2 : ((type & 0xc0) == zorroIII ? 3 : 1),
		expamem_board_pointer, size, sizemod,
		type & rom_card ? _T("ROM") : (type & add_memory ? _T("RAM") : _T("IO ")),
		mapped->name,
		success ? _T("") : _T(" SHUT UP"));
#if 0
	for (int i = 0; i < 16; i++) {
		write_log(_T("%s%02X"), i > 0 ? _T(".") : _T(""), expamem_read(i * 4));
	}
	write_log(_T("\n"));
#endif
}

void expamem_shutup(addrbank *mapped)
{
	if (mapped)
		boardmessage(mapped, false);
}

void expamem_next (addrbank *mapped, addrbank *next)
{
	if (mapped)
		boardmessage(mapped, true);

	expamem_init_clear();
	expamem_init_clear_zero();
	for (;;) {
		++ecard;
		if (ecard >= cardno)
			break;
		struct card_data *ec = &cards[ecard];
		if (ec->initrc && isnonautoconfig(ec->zorro)) {
			ec->initrc(cards[ecard].rc);
		} else {
			call_card_init(ecard);
			break;
		}
	}
	if (ecard >= cardno) {
		expamem_init_clear2();
		expamem_init_last();
	}
}


static uae_u32 REGPARAM2 expamem_lget (uaecptr addr)
{
	if (expamem_bank_current && expamem_bank_current != &expamem_bank)
		return expamem_bank_current->lget(addr);
	write_log (_T("warning: Z2 READ.L from address $%08x PC=%x\n"), addr, M68K_GETPC);
	return (expamem_wget (addr) << 16) | expamem_wget (addr + 2);
}

static uae_u32 REGPARAM2 expamem_wget (uaecptr addr)
{
	if (expamem_bank_current && expamem_bank_current != &expamem_bank)
		return expamem_bank_current->wget(addr);
	if (expamem_type() != zorroIII) {
		if (expamem_bank_current && expamem_bank_current != &expamem_bank)
			return expamem_bank_current->bget(addr) << 8;
	}
	uae_u32 v = (expamem_bget (addr) << 8) | expamem_bget (addr + 1);
	if (cpuboards[currprefs.cpuboard_type].subtypes[currprefs.cpuboard_subtype].e8) {
		uae_u32 val = v;
		cpuboards[currprefs.cpuboard_type].subtypes[currprefs.cpuboard_subtype].e8(addr, &val, 2, false);
		v = val;
	}
	write_log (_T("warning: READ.W from address $%08x=%04x PC=%x\n"), addr, v & 0xffff, M68K_GETPC);
	return v;
}

static uae_u32 REGPARAM2 expamem_bget (uaecptr addr)
{
	uae_u8 b;
	if (!chipdone) {
		chipdone = true;
		addextrachip (get_long (4));
	}
	if (expamem_bank_current && expamem_bank_current != &expamem_bank)
		return expamem_bank_current->bget(addr);
	addr &= 0xFFFF;
	b = expamem[addr];
	if (cpuboards[currprefs.cpuboard_type].subtypes[currprefs.cpuboard_subtype].e8) {
		uae_u32 val = b;
		cpuboards[currprefs.cpuboard_type].subtypes[currprefs.cpuboard_subtype].e8(addr, &val, 1, false);
		b = val;
	}
#if EXP_DEBUG
	write_log (_T("expamem_bget %x %x\n"), addr, b);
#endif
	return b;
}

static void REGPARAM2 expamem_lput (uaecptr addr, uae_u32 value)
{
	if (expamem_bank_current && expamem_bank_current != &expamem_bank) {
		expamem_bank_current->lput(addr, value);
		return;
	}
	write_log (_T("warning: Z2 WRITE.L to address $%08x : value $%08x\n"), addr, value);
}

static void REGPARAM2 expamem_wput (uaecptr addr, uae_u32 value)
{
#if EXP_DEBUG
	write_log (_T("expamem_wput %x %x\n"), addr, value);
#endif
	value &= 0xffff;
	if (cpuboards[currprefs.cpuboard_type].subtypes[currprefs.cpuboard_subtype].e8) {
		if (cpuboards[currprefs.cpuboard_type].subtypes[currprefs.cpuboard_subtype].e8(addr, &value, 2, true))
			return;
	}
	if (ecard >= cardno)
		return;
	if (expamem_type () != zorroIII) {
		write_log (_T("warning: WRITE.W to address $%08x : value $%x PC=%08x\n"), addr, value, M68K_GETPC);
	}
	switch (addr & 0xff) {
	case 0x48:
		// A2630 boot rom writes WORDs to Z2 boards!
		if (expamem_type() == zorroII) {
			expamem_lo = 0;
			expamem_hi = (value >> 8) & 0xff;
			expamem_z2_pointer = (expamem_hi | (expamem_lo >> 4)) << 16; 
			expamem_board_pointer = expamem_z2_pointer;
			if (cards[ecard].map) {
				expamem_next(cards[ecard].map(), NULL);
				return;
			}
			if (expamem_bank_current && expamem_bank_current != &expamem_bank) {
				expamem_bank_current->bput(addr, value >> 8);
				return;
			}
		}
		break;
	case 0x44:
		if (expamem_type() == zorroIII) {
			uaecptr addr;
			expamem_hi = value & 0xff00;
			addr = (expamem_hi | (expamem_lo >> 4)) << 16;
			if (!expamem_z3hack(&currprefs)) {
				expamem_z3_pointer = addr;
			} else {
				if (addr != expamem_z3_pointer) {
					put_word (regs.regs[11] + 0x20, expamem_z3_pointer >> 16);
					put_word (regs.regs[11] + 0x28, expamem_z3_pointer >> 16);
				}
			}
			expamem_board_pointer = expamem_z3_pointer;
		}
		if (cards[ecard].map) {
			expamem_next(cards[ecard].map(), NULL);
			return;
		}
		break;
	case 0x4c:
		if (cards[ecard].map) {
			expamem_next (NULL, NULL);
			return;
		}
		break;
	}
	if (expamem_bank_current && expamem_bank_current != &expamem_bank)
		expamem_bank_current->wput(addr, value);
}

static void REGPARAM2 expamem_bput (uaecptr addr, uae_u32 value)
{
#if EXP_DEBUG
	write_log (_T("expamem_bput %x %x\n"), addr, value);
#endif
	value &= 0xff;
	if (cpuboards[currprefs.cpuboard_type].subtypes[currprefs.cpuboard_subtype].e8) {
		if (cpuboards[currprefs.cpuboard_type].subtypes[currprefs.cpuboard_subtype].e8(addr, &value, 1, true))
			return;
	}
	if (ecard >= cardno)
		return;
	if (expamem_type() == protoautoconfig) {
		switch (addr & 0xff) {
		case 0x22:
			expamem_hi = value & 0x7f;
			expamem_z2_pointer = 0xe80000 | (expamem_hi * 4096);
			expamem_board_pointer = expamem_z2_pointer;
			if (cards[ecard].map) {
				expamem_next(cards[ecard].map(), NULL);
				return;
			}
			break;
		}
	} else {
		switch (addr & 0xff) {
		case 0x48:
			if (expamem_type() == zorroII) {
				expamem_hi = value & 0xff;
				expamem_z2_pointer = (expamem_hi | (expamem_lo >> 4)) << 16; 
				expamem_board_pointer = expamem_z2_pointer;
				if (cards[ecard].map) {
					expamem_next(cards[ecard].map(), NULL);
					return;
				}
			} else {
				expamem_lo = value & 0xff;
			}
			break;

		case 0x4a:
			if (expamem_type () == zorroII)
				expamem_lo = value & 0xff;
			break;

		case 0x4c:
			if (cards[ecard].map) {
				expamem_next(expamem_bank_current, NULL);
				return;
			}
			break;
		}
	}
	if (expamem_bank_current && expamem_bank_current != &expamem_bank)
		expamem_bank_current->bput(addr, value);
}

static uae_u32 REGPARAM2 expamemz3_bget (uaecptr addr)
{
	int reg = addr & 0xff;
	if (!expamem_bank_current)
		return 0;
	if (addr & 0x100)
		reg += 2;
	return expamem_bank_current->bget(reg + 0);
}

static uae_u32 REGPARAM2 expamemz3_wget (uaecptr addr)
{
	uae_u32 v = (expamemz3_bget (addr) << 8) | expamemz3_bget (addr + 1);
	write_log (_T("warning: Z3 READ.W from address $%08x=%04x PC=%x\n"), addr, v & 0xffff, M68K_GETPC);
	return v;
}

static uae_u32 REGPARAM2 expamemz3_lget (uaecptr addr)
{
	write_log (_T("warning: Z3 READ.L from address $%08x PC=%x\n"), addr, M68K_GETPC);
	return (expamemz3_wget (addr) << 16) | expamemz3_wget (addr + 2);
}

static void REGPARAM2 expamemz3_bput (uaecptr addr, uae_u32 value)
{
	int reg = addr & 0xff;
	if (!expamem_bank_current)
		return;
	if (addr & 0x100)
		reg += 2;
	if (reg == 0x48) {
		if (expamem_type() == zorroII) {
			expamem_hi = value & 0xff;
			expamem_z2_pointer = (expamem_hi | (expamem_lo >> 4)) << 16; 
			expamem_board_pointer = expamem_z2_pointer;
		} else {
			expamem_lo = value & 0xff;
		}
	} else if (reg == 0x4a) {
		if (expamem_type() == zorroII)
			expamem_lo = value & 0xff;
	}
	expamem_bank_current->bput(reg, value);
}

static void REGPARAM2 expamemz3_wput (uaecptr addr, uae_u32 value)
{
	int reg = addr & 0xff;
	if (!expamem_bank_current)
		return;
	if (addr & 0x100)
		reg += 2;
	if (reg == 0x44) {
		if (expamem_type() == zorroIII) {
			uaecptr addr;
			expamem_hi = value & 0xff00;
			addr = (expamem_hi | (expamem_lo >> 4)) << 16;;
			if (!expamem_z3hack(&currprefs)) {
				expamem_z3_pointer = addr;
			} else {
				if (addr != expamem_z3_pointer) {
					put_word (regs.regs[11] + 0x20, expamem_z3_pointer >> 16);
					put_word (regs.regs[11] + 0x28, expamem_z3_pointer >> 16);
				}
			}
			expamem_board_pointer = expamem_z3_pointer;
		}
	}
	expamem_bank_current->wput(reg, value);
}
static void REGPARAM2 expamemz3_lput (uaecptr addr, uae_u32 value)
{
	write_log (_T("warning: Z3 WRITE.L to address $%08x : value $%08x\n"), addr, value);
}

#ifdef CD32

static addrbank *expamem_map_cd32fmv (void)
{
	return cd32_fmv_init (expamem_z2_pointer);
}

static addrbank *expamem_init_cd32fmv (int devnum)
{
	int ids[] = { 23, -1 };
	struct romlist *rl = getromlistbyids (ids, NULL);
	struct romdata *rd;
	struct zfile *z;

	expamem_init_clear ();
	if (!rl)
		return NULL;
	write_log (_T("CD32 FMV ROM '%s' %d.%d\n"), rl->path, rl->rd->ver, rl->rd->rev);
	rd = rl->rd;
	z = read_rom (rd);
	if (z) {
		zfile_fread (expamem, 128, 1, z);
		zfile_fclose (z);
	}
	return NULL;
}

#endif

/* ********************************************************** */

/*
*  Fast Memory
*/


MEMORY_FUNCTIONS(fastmem);
MEMORY_FUNCTIONS(fastmem_nojit);
MEMORY_FUNCTIONS(fastmem2);
MEMORY_FUNCTIONS(fastmem2_nojit);

addrbank fastmem_bank = {
	fastmem_lget, fastmem_wget, fastmem_bget,
	fastmem_lput, fastmem_wput, fastmem_bput,
	fastmem_xlate, fastmem_check, NULL, _T("fast"), _T("Fast memory"),
	fastmem_lget, fastmem_wget,
	ABFLAG_RAM | ABFLAG_THREADSAFE, 0, 0
};
addrbank fastmem_nojit_bank = {
	fastmem_nojit_lget, fastmem_nojit_wget, fastmem_bget,
	fastmem_nojit_lput, fastmem_nojit_wput, fastmem_bput,
	fastmem_nojit_xlate, fastmem_nojit_check, NULL, NULL, _T("Fast memory (nojit)"),
	fastmem_nojit_lget, fastmem_nojit_wget,
	ABFLAG_RAM | ABFLAG_THREADSAFE, S_READ, S_WRITE
};
addrbank fastmem2_bank = {
	fastmem2_lget, fastmem2_wget, fastmem2_bget,
	fastmem2_lput, fastmem2_wput, fastmem2_bput,
	fastmem2_xlate, fastmem2_check, NULL,_T("fast2"), _T("Fast memory 2"),
	fastmem2_lget, fastmem2_wget,
	ABFLAG_RAM | ABFLAG_THREADSAFE, 0, 0
};
addrbank fastmem2_nojit_bank = {
	fastmem2_nojit_lget, fastmem2_nojit_wget, fastmem2_nojit_bget,
	fastmem2_nojit_lput, fastmem2_nojit_wput, fastmem2_nojit_bput,
	fastmem2_nojit_xlate, fastmem2_nojit_check, NULL, NULL, _T("Fast memory #2 (nojit)"),
	fastmem2_nojit_lget, fastmem2_nojit_wget,
	ABFLAG_RAM | ABFLAG_THREADSAFE, S_READ, S_WRITE
};

static addrbank *fastbanks[] = 
{
	&fastmem_bank,
	&fastmem_nojit_bank,
	&fastmem2_bank,
	&fastmem2_nojit_bank
};

#ifdef CATWEASEL

/*
* Catweasel ZorroII
*/

DECLARE_MEMORY_FUNCTIONS(catweasel);

static uae_u32 catweasel_mask;
static uae_u32 catweasel_start;

static uae_u32 REGPARAM2 catweasel_lget (uaecptr addr)
{
	write_log (_T("catweasel_lget @%08X!\n"),addr);
	return 0;
}

static uae_u32 REGPARAM2 catweasel_wget (uaecptr addr)
{
	write_log (_T("catweasel_wget @%08X!\n"),addr);
	return 0;
}

static uae_u32 REGPARAM2 catweasel_bget (uaecptr addr)
{
	addr -= catweasel_start & catweasel_mask;
	addr &= catweasel_mask;
	return catweasel_do_bget (addr);
}

static void REGPARAM2 catweasel_lput (uaecptr addr, uae_u32 l)
{
	write_log (_T("catweasel_lput @%08X=%08X!\n"),addr,l);
}

static void REGPARAM2 catweasel_wput (uaecptr addr, uae_u32 w)
{
	write_log (_T("catweasel_wput @%08X=%04X!\n"),addr,w);
}

static void REGPARAM2 catweasel_bput (uaecptr addr, uae_u32 b)
{
	addr -= catweasel_start & catweasel_mask;
	addr &= catweasel_mask;
	catweasel_do_bput (addr, b);
}

static int REGPARAM2 catweasel_check (uaecptr addr, uae_u32 size)
{
	return 0;
}

static uae_u8 *REGPARAM2 catweasel_xlate (uaecptr addr)
{
	write_log (_T("catweasel_xlate @%08X size %08X\n"), addr);
	return 0;
}

static addrbank catweasel_bank = {
	catweasel_lget, catweasel_wget, catweasel_bget,
	catweasel_lput, catweasel_wput, catweasel_bput,
	catweasel_xlate, catweasel_check, NULL, NULL, _T("Catweasel"),
	dummy_lgeti, dummy_wgeti,
	ABFLAG_IO, S_READ, S_WRITE
};

static addrbank *expamem_map_catweasel (void)
{
	catweasel_start = expamem_z2_pointer;
	map_banks_z2(&catweasel_bank, catweasel_start >> 16, 1);
	return &catweasel_bank;
}

static addrbank *expamem_init_catweasel (int devnum)
{
	uae_u8 productid = cwc.type >= CATWEASEL_TYPE_MK3 ? 66 : 200;
	uae_u16 vendorid = cwc.type >= CATWEASEL_TYPE_MK3 ? 4626 : 5001;

	catweasel_mask = (cwc.type >= CATWEASEL_TYPE_MK3) ? 0xffff : 0x1ffff;

	expamem_init_clear ();

	expamem_write (0x00, (cwc.type >= CATWEASEL_TYPE_MK3 ? Z2_MEM_64KB : Z2_MEM_128KB) | zorroII);

	expamem_write (0x04, productid);

	expamem_write (0x08, 0);

	expamem_write (0x10, vendorid >> 8);
	expamem_write (0x14, vendorid & 0xff);

	expamem_write (0x18, 0x00); /* ser.no. Byte 0 */
	expamem_write (0x1c, 0x00); /* ser.no. Byte 1 */
	expamem_write (0x20, 0x00); /* ser.no. Byte 2 */
	expamem_write (0x24, 0x00); /* ser.no. Byte 3 */

	expamem_write (0x28, 0x00); /* Rom-Offset hi */
	expamem_write (0x2c, 0x00); /* ROM-Offset lo */

	expamem_write (0x40, 0x00); /* Ctrl/Statusreg.*/
	return NULL;
}

#endif

#ifdef FILESYS

/*
* Filesystem device ROM
* This is very simple, the Amiga shouldn't be doing things with it.
*/

DECLARE_MEMORY_FUNCTIONS(filesys);
addrbank filesys_bank = {
	filesys_lget, filesys_wget, filesys_bget,
	filesys_lput, filesys_wput, filesys_bput,
	default_xlate, default_check, NULL, _T("filesys"), _T("Filesystem autoconfig"),
	dummy_lgeti, dummy_wgeti,
	ABFLAG_IO | ABFLAG_SAFE | ABFLAG_INDIRECT, S_READ, S_WRITE
};

static uae_u32 filesys_start; /* Determined by the OS */

static uae_u32 REGPARAM2 filesys_lget (uaecptr addr)
{
	uae_u8 *m;
	addr -= filesys_start & 65535;
	addr &= 65535;
	m = filesys_bank.baseaddr + addr;
#if EXP_DEBUG
	write_log (_T("filesys_lget %x %x\n"), addr, do_get_mem_long ((uae_u32 *)m));
#endif
	return do_get_mem_long ((uae_u32 *)m);
}

static uae_u32 REGPARAM2 filesys_wget (uaecptr addr)
{
	uae_u8 *m;
	addr -= filesys_start & 65535;
	addr &= 65535;
	m = filesys_bank.baseaddr + addr;
#if EXP_DEBUG
	write_log (_T("filesys_wget %x %x\n"), addr, do_get_mem_word ((uae_u16 *)m));
#endif
	return do_get_mem_word ((uae_u16 *)m);
}

static uae_u32 REGPARAM2 filesys_bget (uaecptr addr)
{
	addr -= filesys_start & 65535;
	addr &= 65535;
#if EXP_DEBUG
	write_log (_T("filesys_bget %x %x\n"), addr, filesys_bank.baseaddr[addr]);
#endif
	return filesys_bank.baseaddr[addr];
}

static void REGPARAM2 filesys_lput (uaecptr addr, uae_u32 l)
{
	write_log (_T("filesys_lput called PC=%08x\n"), M68K_GETPC);
}

static void REGPARAM2 filesys_wput (uaecptr addr, uae_u32 w)
{
	write_log (_T("filesys_wput called PC=%08x\n"), M68K_GETPC);
}

static void REGPARAM2 filesys_bput (uaecptr addr, uae_u32 b)
{
#if EXP_DEBUG
	write_log (_T("filesys_bput %x %x\n"), addr, b);
#endif
}

#endif /* FILESYS */

// experimental hardware uae board

DECLARE_MEMORY_FUNCTIONS(uaeboard);
addrbank uaeboard_bank = {
	uaeboard_lget, uaeboard_wget, uaeboard_bget,
	uaeboard_lput, uaeboard_wput, uaeboard_bput,
	default_xlate, default_check, NULL, _T("uaeboard"), _T("uae x autoconfig board"),
	dummy_lgeti, dummy_wgeti,
	ABFLAG_IO | ABFLAG_SAFE | ABFLAG_INDIRECT, S_READ, S_WRITE
};

static uae_u32 uaeboard_start; /* Determined by the OS */

static uae_u32 REGPARAM2 uaeboard_lget(uaecptr addr)
{
	uae_u8 *m;
	addr -= uaeboard_start & 65535;
	addr &= 65535;
	if (addr == 0x200 || addr == 0x201) {
		mousehack_wakeup();
	}
	m = uaeboard_bank.baseaddr + addr;
#if EXP_DEBUG
	write_log(_T("uaeboard_lget %x %x\n"), addr, do_get_mem_long((uae_u32 *)m));
#endif
	return do_get_mem_long((uae_u32 *)m);
}

static uae_u32 REGPARAM2 uaeboard_wget(uaecptr addr)
{
	uae_u8 *m;
	addr -= uaeboard_start & 65535;
	addr &= 65535;
	if (addr == 0x200 || addr == 0x201) {
		mousehack_wakeup();
	}
	m = uaeboard_bank.baseaddr + addr;
#if EXP_DEBUG
	write_log(_T("uaeboard_wget %x %x\n"), addr, do_get_mem_word((uae_u16 *)m));
#endif
	return do_get_mem_word((uae_u16 *)m);
}

static uae_u32 REGPARAM2 uaeboard_bget(uaecptr addr)
{
	addr -= uaeboard_start & 65535;
	addr &= 65535;
	if (addr == 0x200 || addr == 0x201) {
		mousehack_wakeup();
	}
#if EXP_DEBUG
	write_log(_T("uaeboard_bget %x %x\n"), addr, uaeboard_bank.baseaddr[addr]);
#endif
	return uaeboard_bank.baseaddr[addr];
}

static void REGPARAM2 uaeboard_lput(uaecptr addr, uae_u32 l)
{
	if (addr >= 0x200 + 0x20 && addr < 0x200 + 0x24) {
		mousehack_write(addr - 0x200, l >> 16);
		mousehack_write(addr - 0x200 + 2, l);
	}
	write_log(_T("uaeboard_lput %08x = %08x PC=%08x\n"), addr, l, M68K_GETPC);
}

static void REGPARAM2 uaeboard_wput(uaecptr addr, uae_u32 w)
{
	if (addr >= 0x200 + 0x20 && addr < 0x200 + 0x24)
		mousehack_write(addr - 0x200, w);
	write_log(_T("uaeboard_wput %08x = %04x PC=%08x\n"), addr, w, M68K_GETPC);
}

static void REGPARAM2 uaeboard_bput(uaecptr addr, uae_u32 b)
{
#if EXP_DEBUG
	write_log(_T("uaeboard_bput %x %x\n"), addr, b);
#endif
}


static addrbank *expamem_map_uaeboard(void)
{
	uaeboard_start = expamem_z2_pointer;
	map_banks_z2(&uaeboard_bank, uaeboard_start >> 16, 1);
	return &uaeboard_bank;
}
static addrbank* expamem_init_uaeboard(int devnum)
{
	uae_u8 *p = uaeboard_bank.baseaddr;

	expamem_init_clear();
	expamem_write(0x00, Z2_MEM_64KB | zorroII);

	expamem_write(0x08, no_shutup);

	expamem_write(0x04, 1);
	expamem_write(0x10, 6502 >> 8);
	expamem_write(0x14, 6502 & 0xff);

	expamem_write(0x18, 0x00); /* ser.no. Byte 0 */
	expamem_write(0x1c, 0x00); /* ser.no. Byte 1 */
	expamem_write(0x20, 0x00); /* ser.no. Byte 2 */
	expamem_write(0x24, 0x01); /* ser.no. Byte 3 */

							   /* er_InitDiagVec */
	expamem_write(0x28, 0x00); /* Rom-Offset hi */
	expamem_write(0x2c, 0x00); /* ROM-Offset lo */

	memcpy(p, expamem, 0x100);

	p += 0x100;
	p[0] = 0x00;
	p[1] = 0x00;
	p[2] = 0x02;
	p[3] = 0x00;
	return NULL;
}

/*
*  Z3fastmem Memory
*/

MEMORY_FUNCTIONS(z3fastmem);
MEMORY_FUNCTIONS(z3fastmem2);
MEMORY_FUNCTIONS(z3chipmem);

addrbank z3fastmem_bank = {
	z3fastmem_lget, z3fastmem_wget, z3fastmem_bget,
	z3fastmem_lput, z3fastmem_wput, z3fastmem_bput,
	z3fastmem_xlate, z3fastmem_check, NULL, _T("z3"), _T("Zorro III Fast RAM"),
	z3fastmem_lget, z3fastmem_wget,
	ABFLAG_RAM | ABFLAG_THREADSAFE, 0, 0
};
addrbank z3fastmem2_bank = {
	z3fastmem2_lget, z3fastmem2_wget, z3fastmem2_bget,
	z3fastmem2_lput, z3fastmem2_wput, z3fastmem2_bput,
	z3fastmem2_xlate, z3fastmem2_check, NULL, _T("z3_2"), _T("Zorro III Fast RAM #2"),
	z3fastmem2_lget, z3fastmem2_wget,
	ABFLAG_RAM | ABFLAG_THREADSAFE, 0, 0
};
addrbank z3chipmem_bank = {
	z3chipmem_lget, z3chipmem_wget, z3chipmem_bget,
	z3chipmem_lput, z3chipmem_wput, z3chipmem_bput,
	z3chipmem_xlate, z3chipmem_check, NULL, _T("z3_chip"), _T("MegaChipRAM"),
	z3chipmem_lget, z3chipmem_wget,
	ABFLAG_RAM | ABFLAG_THREADSAFE, 0, 0
};

/* ********************************************************** */

/*
*     Expansion Card (ZORRO II) for 64/128/256/512KB 1/2/4/8MB of Fast Memory
*/

static addrbank *expamem_map_fastcard_2 (int boardnum)
{
	uae_u32 start = ((expamem_hi | (expamem_lo >> 4)) << 16);
	addrbank *ab = fastbanks[boardnum * 2 + ((start < 0x00A00000) ? 0 : 1)];
	uae_u32 size = ab->allocated;
	ab->start = start;
	if (ab->start) {
		map_banks_z2(ab, ab->start >> 16, size >> 16);
	}
	return ab;
}

static void fastmem_autoconfig(int boardnum, int zorro, uae_u8 type, uae_u32 serial, int allocated)
{
	uae_u16 mid = 0;
	uae_u8 pid;
	uae_u8 flags = care_addr;
	DEVICE_MEMORY_CALLBACK dmc = NULL;
	struct romconfig *dmc_rc = NULL;
	uae_u8 ac[16] = { 0 };

	if (boardnum == 1) {
		const struct cpuboardsubtype *cst = &cpuboards[currprefs.cpuboard_type].subtypes[currprefs.cpuboard_subtype];
		if (cst->memory_mid) {
			mid = cst->memory_mid;
			pid = cst->memory_pid;
			serial = cst->memory_serial;
		}
	} else if (boardnum == 0) {
		for (int i = 0; expansionroms[i].name; i++) {
			const struct expansionromtype *erc = &expansionroms[i];
			if (((erc->zorro == zorro) || (zorro < 0 && erc->zorro >= BOARD_NONAUTOCONFIG_BEFORE)) && cfgfile_board_enabled(&currprefs, erc->romtype, 0)) {
				struct romconfig *rc = get_device_romconfig(&currprefs, erc->romtype, 0);
				if (erc->subtypes) {
					const struct expansionsubromtype *srt = &erc->subtypes[rc->subtype];
					if (srt->memory_mid) {
						mid = srt->memory_mid;
						pid = srt->memory_pid;
						serial = srt->memory_serial;
						if (!srt->memory_after)
							type |= chainedconfig;
					}
				} else {
					if (erc->memory_mid) {
						mid = erc->memory_mid;
						pid = erc->memory_pid;
						serial = erc->memory_serial;
						if (!erc->memory_after)
							type |= chainedconfig;
					}
				}
				dmc = erc->memory_callback;
				dmc_rc = rc;
				break;
			}
		}
	}

	if (!mid) {
		if (zorro <= 2) {
			pid = currprefs.maprom && !currprefs.cpuboard_type ? 1 : 81;
		} else {
			int subsize = (allocated == 0x100000 ? Z3_SS_MEM_1MB
						   : allocated == 0x200000 ? Z3_SS_MEM_2MB
						   : allocated == 0x400000 ? Z3_SS_MEM_4MB
						   : allocated == 0x800000 ? Z3_SS_MEM_8MB
						   : Z3_SS_MEM_SAME);
			pid = currprefs.maprom && !currprefs.cpuboard_type ? 3 : 83;
			flags |= force_z3 | (allocated > 0x800000 ? ext_size : subsize);
		}
		mid = uae_id;
	}

	ac[0x00 / 4] = type;
	ac[0x04 / 4] = pid;
	ac[0x08 / 4] = flags;
	ac[0x10 / 4] = mid >> 8;
	ac[0x14 / 4] = mid;
	ac[0x18 / 4] = serial >> 24;
	ac[0x1c / 4] = serial >> 16;
	ac[0x20 / 4] = serial >> 8;
	ac[0x24 / 4] = serial >> 0;

	if (dmc && dmc_rc)
		dmc(dmc_rc, ac, allocated);

	expamem_write(0x00, ac[0x00 / 4]);
	expamem_write(0x04, ac[0x04 / 4]);
	expamem_write(0x08, ac[0x08 / 4]);
	expamem_write(0x10, ac[0x10 / 4]);
	expamem_write(0x14, ac[0x14 / 4]);

	expamem_write(0x18, ac[0x18 / 4]); /* ser.no. Byte 0 */
	expamem_write(0x1c, ac[0x1c / 4]); /* ser.no. Byte 1 */
	expamem_write(0x20, ac[0x20 / 4]); /* ser.no. Byte 2 */
	expamem_write(0x24, ac[0x24 / 4]); /* ser.no. Byte 3 */

	expamem_write(0x28, 0x00); /* ROM-Offset hi */
	expamem_write(0x2c, 0x00); /* ROM-Offset lo */

	expamem_write(0x40, 0x00); /* Ctrl/Statusreg.*/
}

static const uae_u8 a2630_autoconfig[] = { 0xe7, 0x51, 0x40, 0x00, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

static addrbank *expamem_init_fastcard_2(int boardnum)
{
	uae_u8 type = add_memory | zorroII;
	int allocated = boardnum ? fastmem2_bank.allocated : fastmem_bank.allocated;
	uae_u32 serial = 1;

	if (allocated == 0)
		return &expamem_null;

	expamem_init_clear ();
	if (allocated == 65536)
		type |= Z2_MEM_64KB;
	else if (allocated == 131072)
		type |= Z2_MEM_128KB;
	else if (allocated == 262144)
		type |= Z2_MEM_256KB;
	else if (allocated == 524288)
		type |= Z2_MEM_512KB;
	else if (allocated == 0x100000)
		type |= Z2_MEM_1MB;
	else if (allocated == 0x200000)
		type |= Z2_MEM_2MB;
	else if (allocated == 0x400000)
		type |= Z2_MEM_4MB;
	else if (allocated == 0x800000)
		type |= Z2_MEM_8MB;

	if (boardnum == 1) {
		if (ISCPUBOARD(BOARD_COMMODORE, BOARD_COMMODORE_SUB_A26x0)) {
			for (int i = 1; i < 16; i++)
				expamem_write(i * 4, a2630_autoconfig[i]);
			type &= 7;
			type |= a2630_autoconfig[0] & ~7;
			expamem_write(0, type);
			return NULL;
		}
	}

	fastmem_autoconfig(boardnum, BOARD_AUTOCONFIG_Z2, type, serial, allocated);
	fastmem_autoconfig(boardnum, -1, type, serial, allocated);

	return NULL;
}

static addrbank *expamem_init_fastcard(int boardnum)
{
	return expamem_init_fastcard_2(0);
}
static addrbank *expamem_init_fastcard2(int boardnum)
{
	return expamem_init_fastcard_2(1);
}
static addrbank *expamem_map_fastcard (void)
{
	return expamem_map_fastcard_2 (0);
}
static addrbank *expamem_map_fastcard2 (void)
{
	return expamem_map_fastcard_2 (1);
}

bool expansion_is_next_board_fastram(void)
{
	return ecard + 1 < MAX_EXPANSION_BOARD_SPACE && cards[ecard + 1].map == expamem_map_fastcard;
}

/* ********************************************************** */

#ifdef FILESYS

/*
* Filesystem device
*/

static void expamem_map_filesys_update(void)
{
	/* 68k code needs to know this. */
	uaecptr a = here();
	org(rtarea_base + RTAREA_FSBOARD);
	dl(filesys_start + 0x2000);
	org(a);
}

static addrbank *expamem_map_filesys (void)
{
	// Warn if PPC doing autoconfig and UAE expansion enabled
	static bool warned;
	if (!warned && regs.halted < 0) {
		warned = true;
		// can't show dialogs from PPC thread, deadlock danger.
		regs.halted = -2;
	}

	filesys_start = expamem_z2_pointer;
	map_banks_z2(&filesys_bank, filesys_start >> 16, 1);
	expamem_map_filesys_update();
	return &filesys_bank;
}

#define FILESYS_DIAGPOINT 0x01e0
#define FILESYS_BOOTPOINT 0x01e6
#define FILESYS_DIAGAREA 0x2000

#if KS12_BOOT_HACK
static void add_ks12_boot_hack(void)
{
	uaecptr name = ds(_T("UAE boot"));
	align(2);
	uaecptr code = here();
	// allocate fake diagarea
	dl(0x48e73f3e); // movem.l d2-d7/a2-a6,-(sp)
	dw(0x203c); // move.l #x,d0
	dl(0x0300);
	dw(0x7201); // moveq #1,d1
	dl(0x4eaeff3a); // jsr -0xc6(a6)
	dw(0x2440); // move.l d0,a2 ;diag area
	dw(0x9bcd); // sub.l a5,a5 ;expansionbase
	dw(0x97cb); // sub.l a3,a3 ;configdev
	dw(0x4eb9); // jsr
	dl(ROM_filesys_diagentry);
	dl(0x4cdf7cfc); // movem.l (sp)+,d2-d7/a2-a6
	dw(0x4e75);
	// struct Resident
	uaecptr addr = here();
	dw(0x4afc);
	dl(addr);
	dl(addr + 26);
	db(1); // RTF_COLDSTART
	db(kickstart_version); // version
	db(0); // NT_UNKNOWN
	db(1); // priority
	dl(name);
	dl(name);
	dl(code);
}
#endif

static addrbank* expamem_init_filesys (int devnum)
{
	bool ks12 = ks12orolder();
	bool hide = currprefs.uae_hide_autoconfig;

	/* struct DiagArea - the size has to be large enough to store several device ROMTags */
	uae_u8 diagarea[] = { 0x90, 0x00, /* da_Config, da_Flags */
		0x02, 0x00, /* da_Size */
		FILESYS_DIAGPOINT >> 8, FILESYS_DIAGPOINT & 0xff,
		FILESYS_BOOTPOINT >> 8, FILESYS_BOOTPOINT & 0xff,
		0, hide ? 0 : 14, // Name offset
		0, 0, 0, 0,
		hide ? 0 : 'U', hide ? 0 : 'A', hide ? 0 : 'E', 0
	};

	expamem_init_clear ();
	expamem_write (0x00, Z2_MEM_64KB | zorroII | (ks12 ? 0 : rom_card));

	expamem_write (0x08, no_shutup);

	expamem_write (0x04, currprefs.maprom && !currprefs.cpuboard_type ? 2 : 82);
	expamem_write (0x10, uae_id >> 8);
	expamem_write (0x14, uae_id & 0xff);

	expamem_write (0x18, 0x00); /* ser.no. Byte 0 */
	expamem_write (0x1c, 0x00); /* ser.no. Byte 1 */
	expamem_write (0x20, 0x00); /* ser.no. Byte 2 */
	expamem_write (0x24, 0x01); /* ser.no. Byte 3 */

	/* er_InitDiagVec */
	expamem_write (0x28, 0x20); /* Rom-Offset hi */
	expamem_write (0x2c, 0x00); /* ROM-Offset lo */

	expamem_write (0x40, 0x00); /* Ctrl/Statusreg.*/

	/* Build a DiagArea */
	memcpy (expamem + FILESYS_DIAGAREA, diagarea, sizeof diagarea);

	/* Call DiagEntry */
	do_put_mem_word ((uae_u16 *)(expamem + FILESYS_DIAGAREA + FILESYS_DIAGPOINT), 0x4EF9); /* JMP */
	do_put_mem_long ((uae_u32 *)(expamem + FILESYS_DIAGAREA + FILESYS_DIAGPOINT + 2), ROM_filesys_diagentry);

	/* What comes next is a plain bootblock */
	do_put_mem_word ((uae_u16 *)(expamem + FILESYS_DIAGAREA + FILESYS_BOOTPOINT), 0x4EF9); /* JMP */
	do_put_mem_long ((uae_u32 *)(expamem + FILESYS_DIAGAREA + FILESYS_BOOTPOINT + 2), EXPANSION_bootcode);

	if (ks12)
		add_ks12_boot_hack();

	memcpy (filesys_bank.baseaddr, expamem, 0x3000);
	return NULL;
}

#endif

/*
* Zorro III expansion memory
*/

static addrbank * expamem_map_z3fastmem_2 (addrbank *bank, uaecptr *startp, uae_u32 size, uae_u32 allocated, int chip)
{
	int z3fs = expamem_z3_pointer;
	int start = *startp;

	if (expamem_z3hack(&currprefs)) {
		if (z3fs && start != z3fs) {
			write_log (_T("WARNING: Z3MEM mapping changed from $%08x to $%08x\n"), start, z3fs);
			map_banks(&dummy_bank, start >> 16, size >> 16, allocated);
			*startp = z3fs;
			map_banks_z3(bank, start >> 16, size >> 16);
		}
	} else {
		map_banks_z3(bank, z3fs >> 16, size >> 16);
		start = z3fs;
		*startp = z3fs;
	}
	return bank;
}

static addrbank *expamem_map_z3fastmem (void)
{
	return expamem_map_z3fastmem_2 (&z3fastmem_bank, &z3fastmem_bank.start, currprefs.z3fastmem_size, z3fastmem_bank.allocated, 0);
}
static addrbank *expamem_map_z3fastmem2 (void)
{
	return expamem_map_z3fastmem_2 (&z3fastmem2_bank, &z3fastmem2_bank.start, currprefs.z3fastmem2_size, z3fastmem2_bank.allocated, 0);
}

static addrbank *expamem_init_z3fastmem_2(int boardnum, addrbank *bank, uae_u32 start, uae_u32 size, uae_u32 allocated)
{
	int code = (allocated == 0x100000 ? Z2_MEM_1MB
		: allocated == 0x200000 ? Z2_MEM_2MB
		: allocated == 0x400000 ? Z2_MEM_4MB
		: allocated == 0x800000 ? Z2_MEM_8MB
		: allocated == 0x1000000 ? Z3_MEM_16MB
		: allocated == 0x2000000 ? Z3_MEM_32MB
		: allocated == 0x4000000 ? Z3_MEM_64MB
		: allocated == 0x8000000 ? Z3_MEM_128MB
		: allocated == 0x10000000 ? Z3_MEM_256MB
		: allocated == 0x20000000 ? Z3_MEM_512MB
		: Z3_MEM_1GB);

	if (allocated < 0x1000000)
		code = Z3_MEM_16MB; /* Z3 physical board size is always at least 16M */

	expamem_init_clear ();
	fastmem_autoconfig(boardnum, BOARD_AUTOCONFIG_Z3, add_memory | zorroIII | code, 1, allocated);
	map_banks_z3(bank, start >> 16, size >> 16);
	return NULL;
}
static addrbank *expamem_init_z3fastmem (int devnum)
{
	return expamem_init_z3fastmem_2 (0, &z3fastmem_bank, z3fastmem_bank.start, currprefs.z3fastmem_size, z3fastmem_bank.allocated);
}
static addrbank *expamem_init_z3fastmem2(int devnum)
{
	return expamem_init_z3fastmem_2 (1, &z3fastmem2_bank, z3fastmem2_bank.start, currprefs.z3fastmem2_size, z3fastmem2_bank.allocated);
}

#ifdef PICASSO96
/*
*  Fake Graphics Card (ZORRO III) - BDK
*/

static addrbank *expamem_map_gfxcard_z3 (void)
{
	gfxmem_bank.start = expamem_z3_pointer;
	map_banks_z3(&gfxmem_bank, gfxmem_bank.start >> 16, gfxmem_bank.allocated >> 16);
	return &gfxmem_bank;
}

static addrbank *expamem_map_gfxcard_z2 (void)
{
	gfxmem_bank.start = expamem_z2_pointer;
	map_banks_z2(&gfxmem_bank, gfxmem_bank.start >> 16, gfxmem_bank.allocated >> 16);
	return &gfxmem_bank;
}

static addrbank *expamem_init_gfxcard (bool z3)
{
	int code = (gfxmem_bank.allocated == 0x100000 ? Z2_MEM_1MB
		: gfxmem_bank.allocated == 0x200000 ? Z2_MEM_2MB
		: gfxmem_bank.allocated == 0x400000 ? Z2_MEM_4MB
		: gfxmem_bank.allocated == 0x800000 ? Z2_MEM_8MB
		: gfxmem_bank.allocated == 0x1000000 ? Z3_MEM_16MB
		: gfxmem_bank.allocated == 0x2000000 ? Z3_MEM_32MB
		: gfxmem_bank.allocated == 0x4000000 ? Z3_MEM_64MB
		: gfxmem_bank.allocated == 0x8000000 ? Z3_MEM_128MB
		: gfxmem_bank.allocated == 0x10000000 ? Z3_MEM_256MB
		: gfxmem_bank.allocated == 0x20000000 ? Z3_MEM_512MB
		: Z3_MEM_1GB);
	int subsize = (gfxmem_bank.allocated == 0x100000 ? Z3_SS_MEM_1MB
		: gfxmem_bank.allocated == 0x200000 ? Z3_SS_MEM_2MB
		: gfxmem_bank.allocated == 0x400000 ? Z3_SS_MEM_4MB
		: gfxmem_bank.allocated == 0x800000 ? Z3_SS_MEM_8MB
		: Z3_SS_MEM_SAME);

	if (gfxmem_bank.allocated < 0x1000000 && z3)
		code = Z3_MEM_16MB; /* Z3 physical board size is always at least 16M */

	expamem_init_clear ();
	expamem_write (0x00, (z3 ? zorroIII : zorroII) | code);

	expamem_write (0x08, care_addr | (z3 ? (force_z3 | (gfxmem_bank.allocated > 0x800000 ? ext_size: subsize)) : 0));
	expamem_write (0x04, 96);

	expamem_write (0x10, uae_id >> 8);
	expamem_write (0x14, uae_id & 0xff);

	expamem_write (0x18, 0x00); /* ser.no. Byte 0 */
	expamem_write (0x1c, 0x00); /* ser.no. Byte 1 */
	expamem_write (0x20, 0x00); /* ser.no. Byte 2 */
	expamem_write (0x24, 0x01); /* ser.no. Byte 3 */

	expamem_write (0x28, 0x00); /* ROM-Offset hi */
	expamem_write (0x2c, 0x00); /* ROM-Offset lo */

	expamem_write (0x40, 0x00); /* Ctrl/Statusreg.*/
	return NULL;
}
static addrbank *expamem_init_gfxcard_z3(int devnum)
{
	return expamem_init_gfxcard (true);
}
static addrbank *expamem_init_gfxcard_z2 (int devnum)
{
	return expamem_init_gfxcard (false);
}
#endif


#ifdef SAVESTATE
static size_t fast_filepos, fast2_filepos, z3_filepos, z3_filepos2, z3_fileposchip, p96_filepos;
#endif

void free_fastmemory (int boardnum)
{
	if (!boardnum) {
		mapped_free (&fastmem_bank);
	} else {
		mapped_free (&fastmem2_bank);
	}
}

static bool mapped_malloc_dynamic (uae_u32 *currpsize, uae_u32 *changedpsize, addrbank *bank, int max, const TCHAR *name)
{
	int alloc = *currpsize;

	bank->allocated = 0;
	bank->baseaddr = NULL;
	bank->mask = 0;

	if (!alloc)
		return false;

	while (alloc >= max * 1024 * 1024) {
		bank->mask = alloc - 1;
		bank->allocated = alloc;
		bank->label = name;
		if (mapped_malloc (bank)) {
			*currpsize = alloc;
			*changedpsize = alloc;
			return true;
		}
		write_log (_T("Out of memory for %s, %d bytes.\n"), name, alloc);
		alloc /= 2;
	}

	return false;
}

uaecptr expansion_startaddress(uaecptr addr, uae_u32 size)
{
	if (!size)
		return addr;
	if (size < 16 * 1024 * 1024)
		size = 16 * 1024 * 1024;
	if (!expamem_z3hack(&currprefs))
		return (addr + size - 1) & ~(size - 1);
	return addr;
}

static void allocate_expamem (void)
{
	currprefs.fastmem_size = changed_prefs.fastmem_size;
	currprefs.fastmem2_size = changed_prefs.fastmem2_size;
	currprefs.z3fastmem_size = changed_prefs.z3fastmem_size;
	currprefs.z3fastmem2_size = changed_prefs.z3fastmem2_size;
	currprefs.rtgmem_size = changed_prefs.rtgmem_size;
	currprefs.rtgmem_type = changed_prefs.rtgmem_type;
	currprefs.z3chipmem_size = changed_prefs.z3chipmem_size;

	z3chipmem_bank.start = Z3BASE_UAE;
	z3fastmem_bank.start = currprefs.z3autoconfig_start;
	if (currprefs.mbresmem_high_size >= 128 * 1024 * 1024)
		z3chipmem_bank.start += (currprefs.mbresmem_high_size - 128 * 1024 * 1024) + 16 * 1024 * 1024;
	if (!expamem_z3hack(&currprefs))
		z3fastmem_bank.start = Z3BASE_REAL;
	if (z3fastmem_bank.start == Z3BASE_REAL) {
		int z3off = cpuboards[currprefs.cpuboard_type].subtypes[currprefs.cpuboard_subtype].z3extra;
		if (z3off) {
			z3fastmem_bank.start += z3off;
			z3fastmem_bank.start = expansion_startaddress(z3fastmem_bank.start, currprefs.z3fastmem_size);
		}
	}
	if (z3fastmem_bank.start == Z3BASE_UAE) {
		if (currprefs.mbresmem_high_size >= 128 * 1024 * 1024)
			z3fastmem_bank.start += (currprefs.mbresmem_high_size - 128 * 1024 * 1024) + 16 * 1024 * 1024;
		z3fastmem_bank.start += currprefs.z3chipmem_size;
	}
	z3fastmem2_bank.start = z3fastmem_bank.start + currprefs.z3fastmem_size;

	if (currprefs.z3chipmem_size && z3fastmem_bank.start - z3chipmem_bank.start < currprefs.z3chipmem_size)
		currprefs.z3chipmem_size = changed_prefs.z3chipmem_size = 0;	

	if (fastmem_bank.allocated != currprefs.fastmem_size) {
		free_fastmemory (0);

		fastmem_bank.allocated = currprefs.fastmem_size;
		fastmem_bank.mask = fastmem_bank.allocated - 1;

		fastmem_nojit_bank.allocated = fastmem_bank.allocated;
		fastmem_nojit_bank.mask = fastmem_bank.mask;

		if (fastmem_bank.allocated) {
			mapped_malloc (&fastmem_bank);
			fastmem_nojit_bank.baseaddr = fastmem_bank.baseaddr;
			if (fastmem_bank.baseaddr == 0) {
				write_log (_T("Out of memory for fastmem card.\n"));
				fastmem_bank.allocated = 0;
				fastmem_nojit_bank.allocated = 0;
			}
		}
		memory_hardreset (1);
	}

	if (fastmem2_bank.allocated != currprefs.fastmem2_size) {
		free_fastmemory (1);

		fastmem2_bank.allocated = currprefs.fastmem2_size;
		fastmem2_bank.mask = fastmem2_bank.allocated - 1;

		fastmem2_nojit_bank.allocated = fastmem2_bank.allocated;
		fastmem2_nojit_bank.mask = fastmem2_bank.mask;

		if (fastmem2_bank.allocated) {
			mapped_malloc (&fastmem2_bank);
			fastmem2_nojit_bank.baseaddr = fastmem2_bank.baseaddr;
			if (fastmem2_bank.baseaddr == 0) {
				write_log (_T("Out of memory for fastmem2 card.\n"));
				fastmem2_bank.allocated = 0;
				fastmem2_nojit_bank.allocated = 0;
			}
		}
		memory_hardreset (1);
	}

	if (z3fastmem_bank.allocated != currprefs.z3fastmem_size) {
		mapped_free (&z3fastmem_bank);
		mapped_malloc_dynamic (&currprefs.z3fastmem_size, &changed_prefs.z3fastmem_size, &z3fastmem_bank, 1, _T("z3"));
		memory_hardreset (1);
	}
	if (z3fastmem2_bank.allocated != currprefs.z3fastmem2_size) {
		mapped_free (&z3fastmem2_bank);

		z3fastmem2_bank.allocated = currprefs.z3fastmem2_size;
		z3fastmem2_bank.mask = z3fastmem2_bank.allocated - 1;

		if (z3fastmem2_bank.allocated) {
			mapped_malloc (&z3fastmem2_bank);
			if (z3fastmem2_bank.baseaddr == 0) {
				write_log (_T("Out of memory for 32 bit fast memory #2.\n"));
				z3fastmem2_bank.allocated = 0;
			}
		}
		memory_hardreset (1);
	}
	if (z3chipmem_bank.allocated != currprefs.z3chipmem_size) {
		mapped_free (&z3chipmem_bank);
		mapped_malloc_dynamic (&currprefs.z3chipmem_size, &changed_prefs.z3chipmem_size, &z3chipmem_bank, 16, _T("z3_chip"));
		memory_hardreset (1);
	}

#ifdef PICASSO96
	if (gfxmem_bank.allocated != currprefs.rtgmem_size) {
		mapped_free (&gfxmem_bank);
		if (currprefs.rtgmem_type < GFXBOARD_HARDWARE)
			mapped_malloc_dynamic (&currprefs.rtgmem_size, &changed_prefs.rtgmem_size, &gfxmem_bank, 1, currprefs.rtgmem_type ? _T("z3_gfx") : _T("z2_gfx"));
		memory_hardreset (1);
	}
#endif

#ifdef SAVESTATE
	if (savestate_state == STATE_RESTORE) {
		if (fastmem_bank.allocated > 0) {
			restore_ram (fast_filepos, fastmem_bank.baseaddr);
			if (!fastmem_bank.start) {
				// old statefile compatibility support
				fastmem_bank.start = 0x00200000;
			}
			map_banks(&fastmem_bank, fastmem_bank.start >> 16, currprefs.fastmem_size >> 16,
					fastmem_bank.allocated);
		}
		if (fastmem2_bank.allocated > 0) {
			restore_ram (fast2_filepos, fastmem2_bank.baseaddr);
			map_banks(&fastmem2_bank, fastmem2_bank.start >> 16, currprefs.fastmem2_size >> 16,
				fastmem2_bank.allocated);
		}
		if (z3fastmem_bank.allocated > 0) {
			restore_ram (z3_filepos, z3fastmem_bank.baseaddr);
			map_banks(&z3fastmem_bank, z3fastmem_bank.start >> 16, currprefs.z3fastmem_size >> 16,
				z3fastmem_bank.allocated);
		}
		if (z3fastmem2_bank.allocated > 0) {
			restore_ram (z3_filepos2, z3fastmem2_bank.baseaddr);
			map_banks(&z3fastmem2_bank, z3fastmem2_bank.start >> 16, currprefs.z3fastmem2_size >> 16,
				z3fastmem2_bank.allocated);
		}
		if (z3chipmem_bank.allocated > 0) {
			restore_ram (z3_fileposchip, z3chipmem_bank.baseaddr);
			map_banks(&z3chipmem_bank, z3chipmem_bank.start >> 16, currprefs.z3chipmem_size >> 16,
				z3chipmem_bank.allocated);
		}
#ifdef PICASSO96
		if (gfxmem_bank.allocated > 0 && gfxmem_bank.start > 0) {
			restore_ram (p96_filepos, gfxmem_bank.baseaddr);
			map_banks(&gfxmem_bank, gfxmem_bank.start >> 16, currprefs.rtgmem_size >> 16,
				gfxmem_bank.allocated);
		}
#endif
	}
#endif /* SAVESTATE */
}

static uaecptr check_boot_rom (int *boot_rom_type)
{
	uaecptr b = RTAREA_DEFAULT;
	addrbank *ab;

	*boot_rom_type = 0;
	if (currprefs.boot_rom == 1)
		return 0;
	*boot_rom_type = 1;
	if (currprefs.cs_cdtvcd || currprefs.cs_cdtvscsi || currprefs.uae_hide > 1)
		b = RTAREA_BACKUP;
	if (currprefs.cs_mbdmac == 1 || currprefs.cpuboard_type)
		b = RTAREA_BACKUP;
	// CSPPC enables MMU at boot and remaps 0xea0000->0xeffff.
	if (ISCPUBOARD(BOARD_BLIZZARD, BOARD_BLIZZARD_SUB_PPC))
		b = RTAREA_BACKUP_2;
	ab = &get_mem_bank (RTAREA_DEFAULT);
	if (ab) {
		if (valid_address (RTAREA_DEFAULT, 65536))
			b = RTAREA_BACKUP;
	}
	if (nr_directory_units (NULL))
		return b;
	if (nr_directory_units (&currprefs))
		return b;
	if (currprefs.socket_emu)
		return b;
	if (currprefs.uaeserial)
		return b;
	if (currprefs.scsi == 1)
		return b;
	if (currprefs.sana2)
		return b;
	if (currprefs.input_tablet > 0)
		return b;
	if (currprefs.rtgmem_size && currprefs.rtgmem_type < GFXBOARD_HARDWARE)
		return b;
	if (currprefs.win32_automount_removable)
		return b;
	if (currprefs.chipmem_size > 2 * 1024 * 1024)
		return b;
	if (currprefs.z3chipmem_size)
		return b;
	if (currprefs.boot_rom >= 3)
		return b;
	if (currprefs.boot_rom == 2 && b == 0xf00000) {
		*boot_rom_type = -1;
		return b;
	}
	*boot_rom_type = 0;
	return 0;
}

uaecptr need_uae_boot_rom (void)
{
	uaecptr v;

	uae_boot_rom_type = 0;
	v = check_boot_rom (&uae_boot_rom_type);
	if (!rtarea_base) {
		uae_boot_rom_type = 0;
		v = 0;
	}
	return v;
}

static void add_cpu_expansions(int zorro)
{
	const struct cpuboardsubtype *cst = &cpuboards[currprefs.cpuboard_type].subtypes[currprefs.cpuboard_subtype];
	if (cst->init && cst->initzorro == zorro) {
		int idx;
		struct boardromconfig *brc = get_device_rom(&currprefs, ROMTYPE_CPUBOARD, 0, &idx);
		struct romconfig *rc = &brc->roms[idx];
		cards[cardno].flags = cst->initflag;
		cards[cardno].name = cst->name;
		cards[cardno].initrc = cst->init;
		cards[cardno].rc = rc;
		cards[cardno].zorro = zorro;
		cards[cardno++].map = NULL;
		if (cst->init2) {
			cards[cardno].flags = cst->initflag;
			cards[cardno].name = cst->name;
			cards[cardno].initrc = cst->init2;
			cards[cardno].zorro = zorro;
			cards[cardno++].map = NULL;
		}
	}
}

static bool add_fastram_after_expansion(int zorro)
{
	for (int i = 0; expansionroms[i].name; i++) {
		const struct expansionromtype *ert = &expansionroms[i];
		if (ert->zorro == zorro) {
			for (int j = 0; j < MAX_DUPLICATE_EXPANSION_BOARDS; j++) {
				struct romconfig *rc = get_device_romconfig(&currprefs, ert->romtype, j);
				if (rc) {
					if (ert->subtypes) {
						const struct expansionsubromtype *srt = &ert->subtypes[rc->subtype];
						return srt->memory_after;
					}
					return ert->memory_after;
				}
			}
		}
	}
	return false;
}

static void add_expansions(int zorro)
{
	for (int i = 0; expansionroms[i].name; i++) {
		const struct expansionromtype *ert = &expansionroms[i];
		if (ert->zorro == zorro) {
			for (int j = 0; j < MAX_DUPLICATE_EXPANSION_BOARDS; j++) {
				struct romconfig *rc = get_device_romconfig(&currprefs, ert->romtype, j);
				if (rc) {
					if (zorro == 1) {
						ert->init(rc);
						if (ert->init2)
							ert->init2(rc);
					} else {
						cards[cardno].flags = 0;
						cards[cardno].name = ert->name;
						cards[cardno].initrc = ert->init;
						cards[cardno].rc = rc;
						cards[cardno].zorro = zorro;
						cards[cardno++].map = NULL;
						if (ert->init2) {
							cards[cardno].flags = 0;
							cards[cardno].name = ert->name;
							cards[cardno].initrc = ert->init2;
							cards[cardno].rc = rc;
							cards[cardno].zorro = zorro;
							cards[cardno++].map = NULL;
						}
					}
				}
			}
		}
	}
}

void expamem_reset (void)
{
	int do_mount = 1;

	ecard = 0;
	cardno = 0;
	chipdone = false;

	if (currprefs.uae_hide)
		uae_id = commodore;
	else
		uae_id = hackers_id;

	allocate_expamem ();
	expamem_bank.name = _T("Autoconfig [reset]");

	if (need_uae_boot_rom() == 0)
		do_mount = 0;
	if (uae_boot_rom_type <= 0)
		do_mount = 0;

	/* check if Kickstart version is below 1.3 */
	if (ks12orolder() && do_mount) {
		/* warn user */
#if KS12_BOOT_HACK
		do_mount = -1;
		if (ks11orolder()) {
			filesys_start = 0xe90000;
			map_banks_z2(&filesys_bank, filesys_start >> 16, 1);
			expamem_init_filesys(0);
			expamem_map_filesys_update();
		}
#else
		write_log(_T("Kickstart version is below 1.3!  Disabling automount devices.\n"));
		do_mount = 0;
#endif
	}

	if (currprefs.cpuboard_type) {
		// This may require first 128k slot.
		cards[cardno].flags = 1;
		cards[cardno].name = _T("CPUBoard");
		cards[cardno].initrc = cpuboard_autoconfig_init;
		cards[cardno++].map = NULL;
	}

	// add possible non-autoconfig boards
	add_cpu_expansions(BOARD_NONAUTOCONFIG_BEFORE);
	add_expansions(BOARD_NONAUTOCONFIG_BEFORE);

	bool fastmem_after = false;
	if (currprefs.fastmem_autoconfig) {
		fastmem_after = add_fastram_after_expansion(BOARD_AUTOCONFIG_Z2);
		if (!fastmem_after && fastmem_bank.baseaddr != NULL && (fastmem_bank.allocated <= 262144 || currprefs.chipmem_size <= 2 * 1024 * 1024)) {
			cards[cardno].flags = 0;
			cards[cardno].name = _T("Z2Fast");
			cards[cardno].initnum = expamem_init_fastcard;
			cards[cardno++].map = expamem_map_fastcard;
		}
		if (fastmem2_bank.baseaddr != NULL && (fastmem2_bank.allocated <= 262144  || currprefs.chipmem_size <= 2 * 1024 * 1024)) {
			cards[cardno].flags = 0;
			cards[cardno].name = _T("Z2Fast2");
			cards[cardno].initnum = expamem_init_fastcard2;
			cards[cardno++].map = expamem_map_fastcard2;
		}
	} else {
		if (fastmem_bank.baseaddr) {
			fastmem_bank.name = _T("Fast memory (non-autoconfig)");
			map_banks(&fastmem_bank, 0x00200000 >> 16, fastmem_bank.allocated >> 16, 0);
		}
		if (fastmem2_bank.baseaddr != NULL) {
			fastmem2_bank.name = _T("Fast memory 2 (non-autoconfig)");
			map_banks(&fastmem2_bank, (0x00200000 + fastmem_bank.allocated) >> 16, fastmem2_bank.allocated >> 16, 0);
		}
	}

	// immediately after Z2Fast so that they can be emulated as A590/A2091 with fast ram.
	add_cpu_expansions(BOARD_AUTOCONFIG_Z2);
	add_expansions(BOARD_AUTOCONFIG_Z2);

	add_cpu_expansions(BOARD_NONAUTOCONFIG_AFTER_Z2);
	add_expansions(BOARD_NONAUTOCONFIG_AFTER_Z2);

	if (fastmem_after && currprefs.fastmem_autoconfig) {
		if (fastmem_bank.baseaddr != NULL && (fastmem_bank.allocated <= 262144 || currprefs.chipmem_size <= 2 * 1024 * 1024)) {
			cards[cardno].flags = 0;
			cards[cardno].name = _T("Z2Fast");
			cards[cardno].initnum = expamem_init_fastcard;
			cards[cardno++].map = expamem_map_fastcard;
		}
	}

#ifdef CDTV
	if (currprefs.cs_cdtvcd && !currprefs.cs_cdtvcr) {
		cards[cardno].flags = 0;
		cards[cardno].name = _T("CDTV DMAC");
		cards[cardno].initrc = cdtv_init;
		cards[cardno++].map = NULL;
	}
#endif
#ifdef CD32
	if (currprefs.cs_cd32cd && currprefs.fastmem_size == 0 && currprefs.chipmem_size <= 0x200000 && currprefs.cs_cd32fmv) {
		cards[cardno].flags = 0;
		cards[cardno].name = _T("CD32MPEG");
		cards[cardno].initnum = expamem_init_cd32fmv;
		cards[cardno++].map = expamem_map_cd32fmv;
	}
#endif
#ifdef A2065
	if (currprefs.a2065name[0]) {
		cards[cardno].flags = 0;
		cards[cardno].name = _T("A2065");
		cards[cardno].initnum = a2065_init;
		cards[cardno++].map = NULL;
	}
#endif
#ifdef FILESYS
	if (do_mount) {
		cards[cardno].flags = 0;
		cards[cardno].name = _T("UAEFS");
		cards[cardno].initnum = expamem_init_filesys;
		cards[cardno++].map = expamem_map_filesys;
	}
	if (currprefs.uaeboard) {
		cards[cardno].flags = 0;
		cards[cardno].name = _T("UAEBOARD");
		cards[cardno].initnum = expamem_init_uaeboard;
		cards[cardno++].map = expamem_map_uaeboard;
	}
#endif
#ifdef CATWEASEL
	if (currprefs.catweasel && catweasel_init ()) {
		cards[cardno].flags = 0;
		cards[cardno].name = _T("CWMK2");
		cards[cardno].initnum = expamem_init_catweasel;
		cards[cardno++].map = expamem_map_catweasel;
	}
#endif
#ifdef PICASSO96
	if (currprefs.rtgmem_type == GFXBOARD_UAE_Z2 && gfxmem_bank.baseaddr != NULL) {
		cards[cardno].flags = 4;
		cards[cardno].name = _T("Z2RTG");
		cards[cardno].initnum = expamem_init_gfxcard_z2;
		cards[cardno++].map = expamem_map_gfxcard_z2;
	}
#endif
#ifdef GFXBOARD
	if (currprefs.rtgmem_type >= GFXBOARD_HARDWARE && gfxboard_get_configtype(currprefs.rtgmem_type) <= 2) {
		cards[cardno].flags = 4;
		if (currprefs.rtgmem_type == GFXBOARD_A2410) {
			cards[cardno].name = _T("Gfxboard A2410");
			cards[cardno++].initnum = tms_init;
		} else {
			cards[cardno].name = _T("Gfxboard VRAM Zorro II");
			cards[cardno++].initnum = gfxboard_init_memory;
			if (gfxboard_num_boards (currprefs.rtgmem_type) == 3) {
				cards[cardno].flags = 0;
				cards[cardno].name = _T("Gfxboard VRAM Zorro II Extra");
				cards[cardno++].initnum = gfxboard_init_memory_p4_z2;
			}
			if (gfxboard_is_registers (currprefs.rtgmem_type)) {
				cards[cardno].flags = 0;
				cards[cardno].name = _T ("Gfxboard Registers");
				cards[cardno++].initnum = gfxboard_init_registers;
			}
		}
	}
#endif
#ifdef WITH_TOCCATA
	if (currprefs.sound_toccata) {
		cards[cardno].flags = 0;
		cards[cardno].name = _T("Toccata");
		cards[cardno++].initnum = sndboard_init;
	}
#endif
	if (currprefs.monitoremu == MONITOREMU_FIRECRACKER24) {
		cards[cardno].flags = 0;
		cards[cardno].name = _T("FireCracker24");
		cards[cardno++].initnum = specialmonitor_autoconfig_init;
	}

	/* Z3 boards last */
	if (!currprefs.address_space_24) {

		add_cpu_expansions(BOARD_AUTOCONFIG_Z3);

		if (z3fastmem_bank.baseaddr != NULL) {
			z3num = 0;
			cards[cardno].flags = 2 | 1;
			cards[cardno].name = _T("Z3Fast");
			cards[cardno].initnum = expamem_init_z3fastmem;
			cards[cardno++].map = expamem_map_z3fastmem;
			if (expamem_z3hack(&currprefs))
				map_banks_z3(&z3fastmem_bank, z3fastmem_bank.start >> 16, currprefs.z3fastmem_size >> 16);
			if (z3fastmem2_bank.baseaddr != NULL) {
				cards[cardno].flags = 2 | 1;
				cards[cardno].name = _T("Z3Fast2");
				cards[cardno].initnum = expamem_init_z3fastmem2;
				cards[cardno++].map = expamem_map_z3fastmem2;
				if (expamem_z3hack(&currprefs))
					map_banks_z3(&z3fastmem2_bank, z3fastmem2_bank.start >> 16, currprefs.z3fastmem2_size >> 16);
			}
		}
		if (z3chipmem_bank.baseaddr != NULL)
			map_banks_z3(&z3chipmem_bank, z3chipmem_bank.start >> 16, currprefs.z3chipmem_size >> 16);
#ifdef PICASSO96
		if (currprefs.rtgmem_type == GFXBOARD_UAE_Z3 && gfxmem_bank.baseaddr != NULL) {
			cards[cardno].flags = 4 | 1;
			cards[cardno].name = _T("Z3RTG");
			cards[cardno].initnum = expamem_init_gfxcard_z3;
			cards[cardno++].map = expamem_map_gfxcard_z3;
		}
#endif
#ifdef GFXBOARD
		if (currprefs.rtgmem_type >= GFXBOARD_HARDWARE && gfxboard_get_configtype(currprefs.rtgmem_type) == 3) {
			cards[cardno].flags = 4 | 1;
			cards[cardno].name = _T ("Gfxboard VRAM Zorro III");
			cards[cardno++].initnum = gfxboard_init_memory;
			cards[cardno].flags = 1;
			cards[cardno].name = _T ("Gfxboard Registers");
			cards[cardno++].initnum = gfxboard_init_registers;
		}
#endif

		add_expansions(BOARD_AUTOCONFIG_Z3);

	}

	add_cpu_expansions(BOARD_NONAUTOCONFIG_AFTER_Z3);
	add_expansions(BOARD_NONAUTOCONFIG_AFTER_Z3);

	expamem_z3_pointer = 0;
	expamem_z3_sum = 0;
	expamem_z2_pointer = 0;
	if (cardno == 0 || savestate_state)
		expamem_init_clear_zero ();
	else
		call_card_init(0);
}

void expansion_init (void)
{
	if (savestate_state != STATE_RESTORE) {

		fastmem_bank.allocated = 0;
		fastmem_bank.mask = fastmem_bank.start = 0;
		fastmem_bank.baseaddr = NULL;
		fastmem_nojit_bank.allocated = 0;
		fastmem_nojit_bank.mask = fastmem_nojit_bank.start = 0;
		fastmem_nojit_bank.baseaddr = NULL;

		fastmem2_bank.allocated = 0;
		fastmem2_bank.mask = fastmem2_bank.start = 0;
		fastmem2_bank.baseaddr = NULL;
		fastmem2_nojit_bank.allocated = 0;
		fastmem2_nojit_bank.mask = fastmem2_nojit_bank.start = 0;
		fastmem2_nojit_bank.baseaddr = NULL;

#ifdef PICASSO96
		gfxmem_bank.allocated = 0;
		gfxmem_bank.mask = gfxmem_bank.start = 0;
		gfxmem_bank.baseaddr = NULL;
#endif

#ifdef CATWEASEL
		catweasel_mask = catweasel_start = 0;
#endif

		z3fastmem_bank.allocated = 0;
		z3fastmem_bank.mask = z3fastmem_bank.start = 0;
		z3fastmem_bank.baseaddr = NULL;

		z3fastmem2_bank.allocated = 0;
		z3fastmem2_bank.mask = z3fastmem2_bank.start = 0;
		z3fastmem2_bank.baseaddr = NULL;

		z3chipmem_bank.allocated = 0;
		z3chipmem_bank.mask = z3chipmem_bank.start = 0;
		z3chipmem_bank.baseaddr = NULL;
	}

#ifdef FILESYS
	filesys_start = 0;
#endif

	allocate_expamem ();

#ifdef FILESYS
	filesys_bank.allocated = 0x10000;
	if (!mapped_malloc (&filesys_bank)) {
		write_log (_T("virtual memory exhausted (filesysory)!\n"));
		exit (0);
	}
#endif
	if (currprefs.uaeboard) {
		uaeboard_bank.allocated = 0x10000;
		mapped_malloc(&uaeboard_bank);
	}
}

void expansion_cleanup (void)
{
	mapped_free (&fastmem_bank);
	fastmem_nojit_bank.baseaddr = NULL;
	mapped_free (&fastmem2_bank);
	fastmem2_nojit_bank.baseaddr = NULL;
	mapped_free (&z3fastmem_bank);
	mapped_free (&z3fastmem2_bank);
	mapped_free (&z3chipmem_bank);

#ifdef PICASSO96
	if (currprefs.rtgmem_type < GFXBOARD_HARDWARE)
		mapped_free (&gfxmem_bank);
#endif

#ifdef FILESYS
	mapped_free (&filesys_bank);
#endif
	if (currprefs.uaeboard) {
		mapped_free(&uaeboard_bank);
	}

#ifdef CATWEASEL
	catweasel_free ();
#endif
}

static void clear_bank (addrbank *ab)
{
	if (!ab->baseaddr || !ab->allocated)
		return;
	memset (ab->baseaddr, 0, ab->allocated > 0x800000 ? 0x800000 : ab->allocated);
}

void expansion_clear (void)
{
	clear_bank (&fastmem_bank);
	clear_bank (&fastmem2_bank);
	clear_bank (&z3fastmem_bank);
	clear_bank (&z3fastmem2_bank);
	clear_bank (&z3chipmem_bank);
	clear_bank (&gfxmem_bank);
}

#ifdef SAVESTATE

/* State save/restore code.  */

uae_u8 *save_fram (int *len, int num)
{
	if (num) {
		*len = fastmem2_bank.allocated;
		return fastmem2_bank.baseaddr;
	} else {
		*len = fastmem_bank.allocated;
		return fastmem_bank.baseaddr;
	}
}

uae_u8 *save_zram (int *len, int num)
{
	if (num < 0) {
		*len = z3chipmem_bank.allocated;
		return z3chipmem_bank.baseaddr;
	}
	*len = num ? z3fastmem2_bank.allocated : z3fastmem_bank.allocated;
	return num ? z3fastmem2_bank.baseaddr : z3fastmem_bank.baseaddr;
}

uae_u8 *save_pram (int *len)
{
	*len = gfxmem_bank.allocated;
	return gfxmem_bank.baseaddr;
}

void restore_fram (int len, size_t filepos, int num)
{
	if (num) {
		fast2_filepos = filepos;
		changed_prefs.fastmem2_size = len;
	} else {
		fast_filepos = filepos;
		changed_prefs.fastmem_size = len;
	}
}

static void restore_fram2 (int len, size_t filepos)
{
	fast2_filepos = filepos;
	changed_prefs.fastmem2_size = len;
}

void restore_zram (int len, size_t filepos, int num)
{
	if (num == -1) {
		z3_fileposchip = filepos;
		changed_prefs.z3chipmem_size = len;
	} else if (num == 1) {
		z3_filepos2 = filepos;
		changed_prefs.z3fastmem2_size = len;
	} else {
		z3_filepos = filepos;
		changed_prefs.z3fastmem_size = len;
	}
}

void restore_pram (int len, size_t filepos)
{
	p96_filepos = filepos;
	changed_prefs.rtgmem_size = len;
}

uae_u8 *save_expansion (int *len, uae_u8 *dstptr)
{
	uae_u8 *dst, *dstbak;
	if (dstptr)
		dst = dstbak = dstptr;
	else
		dstbak = dst = xmalloc (uae_u8, 6 * 4);
	save_u32 (fastmem_bank.start);
	save_u32 (z3fastmem_bank.start);
	save_u32 (gfxmem_bank.start);
	save_u32 (rtarea_base);
	save_u32 (fastmem_bank.start);
	*len = 4 + 4 + 4 + 4 + 4;
	return dstbak;
}

uae_u8 *restore_expansion (uae_u8 *src)
{
	fastmem_bank.start = restore_u32 ();
	z3fastmem_bank.start = restore_u32 ();
	gfxmem_bank.start = restore_u32 ();
	rtarea_base = restore_u32 ();
	fastmem2_bank.start = restore_u32 ();
	if (rtarea_base != 0 && rtarea_base != RTAREA_DEFAULT && rtarea_base != RTAREA_BACKUP && rtarea_base != RTAREA_BACKUP_2)
		rtarea_base = 0;
	return src;
}

#endif /* SAVESTATE */

#if 0
static const struct expansionsubromtype a2090_sub[] = {
	{
		_T("A2090a"), _T("a2090a"),
		0, 0, 0,
		{ 0 },
	},
	{
		_T("A2090a + 1M RAM"), _T("a2090a_2"),
		0, 0, 0,
		{ 0 },
	},
	{
		NULL
	}
};
#endif
static const struct expansionsubromtype a2091_sub[] = {
	{
		_T("DMAC-01"), _T("dmac01"), 0,
		commodore, commodore_a2091_ram, 0, true,
		{ 0 }
	},
	{
		_T("DMAC-02"), _T("dmac02"), 0,
		commodore, commodore_a2091_ram, 0, true,
		{ 0 }
	},
	{
		NULL
	}
};
static const struct expansionsubromtype gvp1_sub[] = {
	{
		_T("Impact A2000-1/X"), _T("a2000-1"), 0,
		1761, 8, 0, false,
		{ 0 }
	},
	{
		_T("Impact A2000-HC"), _T("a2000-hc"), 0,
		1761, 8, 0, false,
		{ 0 }
	},
	{
		_T("Impact A2000-HC+2"), _T("a2000-hc+"), 0,
		1761, 8, 0, false,
		{ 0 }
	},
	{
		NULL
	}
};
static const struct expansionsubromtype masoboshi_sub[] = {
	{
		_T("MC-302"), _T("mc-302"), 0,
		2157, 3, 0, false,
		{ 0 }
	},
	{
		_T("MC-702"), _T("mc-702"), 0,
		2157, 3, 0, false,
		{ 0 }
	},
	{
		NULL
	}
};
static const struct expansionsubromtype rochard_sub[] = {
	{
		_T("IDE"), _T("ide"), 0,
		2144, 2, 0, false,
		{ 0 }
	},
	{
		_T("IDE+SCSI"), _T("scsi"), 0,
		2144, 2, 0, false,
		{ 0 }
	},
	{
		NULL
	}
};
static const struct expansionsubromtype supra_sub[] = {
	{
		_T("A500 ByteSync/XP"), _T("bytesync"), ROMTYPE_NONE | ROMTYPE_SUPRA,
		1056, 9, 0, false,
		{ 0xd1, 13, 0x00, 0x00, 0x04, 0x20, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00 },
	},
	{
		_T("A2000 Word Sync"), _T("wordsync"), ROMTYPE_NONE | ROMTYPE_SUPRA,
		1056, 9, 0, false,
		{ 0xd1, 12, 0x00, 0x00, 0x04, 0x20, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00 },
	},
	{
		_T("A500 Autoboot"), _T("500"), ROMTYPE_NONE | ROMTYPE_SUPRA,
		1056, 5, 0, false,
		{ 0xd1, 8, 0x00, 0x00, 0x04, 0x20, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00 },
	},
	{
		_T("Non Autoboot (4x4)"), _T("4x4"), ROMTYPE_NOT | ROMTYPE_SUPRA,
		1056, 2, 0, false,
		{ 0xc1, 1, 0x00, 0x00, 0x04, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	},
	{
		_T("A2000 DMA"), _T("dma"), ROMTYPE_NONE | ROMTYPE_SUPRADMA,
		1056, 2, 0, false,
		{ 0xd1, 3, 0x00, 0x00, 0x04, 0x20, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00 },
	},
	{
		NULL
	}
};

static const struct expansionsubromtype mediator_sub[] = {
	{
		_T("1200"), _T("1200"), ROMTYPE_NOT | ROMTYPE_MEDIATOR
	},
	{
		_T("1200SX"), _T("1200sx"), ROMTYPE_NOT | ROMTYPE_MEDIATOR
	},
	{
		_T("1200TX"), _T("1200tx"), ROMTYPE_NOT | ROMTYPE_MEDIATOR
	},
	{
		_T("4000MK2"), _T("4000mkii"), ROMTYPE_NOT | ROMTYPE_MEDIATOR
	},
	{
		NULL
	}
};
static const struct expansionboardsettings mediator_settings[] = {
	{
		_T("Full PCI DMA"),
		_T("fulldma")
	},
	{
		_T("Win Size"),
		_T("winsize")
	},
	{
		_T("Swap Config"),
		_T("swapconfig")
	},
	{
		NULL
	}
};
static const struct expansionboardsettings bridge_settings[] = {
	{
		_T("Full PCI DMA"),
		_T("fulldma")
	},
	{
		NULL
	}
};

static const struct expansionboardsettings x86at286_bridge_settings[] = {
	{
		// 14
		_T("Default video\0") _T("Monochrome\0") _T("Color\0"),
		_T("video\0") _T("mono\0") _T("color\0"),
		true, false, 14
	},
	{
		// 15
		_T("Keyboard lock"),
		_T("keylock"),
		false
	},
	{	// 16 - 18
		_T("Memory\0") _T("1M\0") _T("2M\0") _T("4M\0") _T("8M\0") _T("16M\0"),
		_T("memory\0") _T("1M\0") _T("2M\0") _T("4M\0") _T("8M\0") _T("16M\0"),
		true, false, 0
	},
	{	// 19 - 20
		_T("CPU core\0") _T("DOSBox simple\0") _T("DOSBox normal\0") _T("DOSBox full\0") _T("DOSBox auto\0"),
		_T("cpu\0") _T("dbsimple\0") _T("dbnormal\0") _T("dbfull\0") _T("dbauto\0"),
		true, false, 0
	},
	{	// 22
		_T("FPU"),
		_T("fpu"),
		false, false, 1
	},
	{	// 23 - 25
		_T("CPU Arch\0") _T("auto") _T("386\0") _T("386_prefetch\0") _T("386_slow\0") _T("486_slow\0") _T("486_prefetch\0") _T("pentium_slow\0"), 
		_T("cpuarch\0") _T("auto") _T("386\0") _T("386_prefetch\0") _T("386_slow\0") _T("486_slow\0") _T("486_prefetch\0") _T("pentium_slow\0"),
		true, false, 0
	},
	{
		NULL
	}
};

static const struct expansionboardsettings x86at386_bridge_settings[] = {
	{
		// 15
		_T("Keyboard lock"),
		_T("keylock"),
		false, false, 15
	},
	{	// 16 - 18
		_T("Memory\0") _T("1M\0") _T("2M\0") _T("4M\0") _T("8M\0") _T("16M\0") _T("32M\0") _T("64M\0"),
		_T("memory\0") _T("1M\0") _T("2M\0") _T("4M\0") _T("8M\0") _T("16M\0") _T("32M\0") _T("64M\0"),
		true, false, 0
	},
	{	// 19 - 20
		_T("CPU core\0") _T("DOSBox simple\0") _T("DOSBox normal\0") _T("DOSBox full\0") _T("DOSBox auto\0"),
		_T("cpu\0") _T("dbsimple\0") _T("dbnormal\0") _T("dbfull\0") _T("dbauto\0"),
		true, false, 0
	},
	{	// 22
		_T("FPU"),
		_T("fpu"),
		false, false, 1
	},
	{	// 23 - 25
		_T("CPU Arch\0") _T("auto\0") _T("386\0") _T("386_prefetch\0") _T("386_slow\0") _T("486_slow\0") _T("486_prefetch\0") _T("pentium_slow\0"),
		_T("cpuarch\0") _T("auto\0") _T("386\0") _T("386_prefetch\0") _T("386_slow\0") _T("486_slow\0") _T("486_prefetch\0") _T("pentium_slow\0"),
		true, false, 0
	},
	{
		NULL
	}
};

static const struct expansionboardsettings x86_bridge_settings[] = {
	{
		// 2-3
		_T("Memory (SW1:3-4)\0") _T("128K\0") _T("256K\0") _T("512K\0") _T("640K\0"),
		_T("memory\0") _T("128k\0") _T("256k\0") _T("512k\0") _T("640k\0"),
		true, false, 2
	},
	{
		// 4-5
		_T("Default video (J1)\0") _T("Monochrome\0") _T("Color 40x25\0") _T("Color 80x25\0") _T("None\0"),
		_T("video\0") _T("mono\0") _T("color40\0") _T("color80\0") _T("none\0"),
		true, false, 0
	},
	{	// 19 - 21
		_T("CPU core\0") _T("Fake86\0") _T("DOSBox simple\0") _T("DOSBox normal\0") _T("DOSBox full\0") _T("DOSBox auto\0"),
		_T("cpu\0") _T("fake86\0") _T("dbsimple\0") _T("dbnormal\0") _T("dbfull\0") _T("dbauto\0"),
		true, false, 19 - 6
	},
	{	// 22
		_T("FPU (DOSBox CPU only)"),
		_T("fpu")
	},
	{	// 23 - 25
		_T("CPU Arch (DOSBox CPU only)\0") _T("auto") _T("386\0") _T("386_prefetch\0") _T("386_slow\0") _T("486_slow\0") _T("486_prefetch\0") _T("pentium_slow\0"),
		_T("cpuarch\0") _T("auto") _T("386\0") _T("386_prefetch\0") _T("386_slow\0") _T("486_slow\0") _T("486_prefetch\0") _T("pentium_slow\0"),
		true, false, 0
	},
	{
		NULL
	}
};

static const struct expansionboardsettings x86_bridge_sidecar_settings[] = {
	{
		// 0
		_T("System Diagnostics (SW1:1)"),
		_T("diagnostics"),
	},
	{
		// 1
		_T("8037 installed (SW1:2)"),
		_T("fpu"),
	},
	{
		// 2-3
		_T("Memory (SW1:3-4)\0") _T("128K\0") _T("256K\0") _T("512K\0") _T("640K\0"),
		_T("memory\0") _T("128k\0") _T("256k\0") _T("512k\0") _T("640k\0"),
		true
	},
	{
		// 4-5
		_T("Default video (SW1:5-6)\0") _T("Monochrome\0") _T("Color 40x25\0") _T("Color 80x25\0") _T("None\0"),
		_T("video\0") _T("mono\0") _T("color40\0") _T("color80\0") _T("none\0"),
		true
	},
	{
		// 6-7
		_T("Floppy drives (SW1:7-8)\0") _T("1\0") _T("2\0") _T("3\0") _T("4\0"),
		_T("floppy\0") _T("floppy1\0") _T("floppy2\0") _T("floppy3\0") _T("floppy4\0"),
		true
	},
	{
		// 8
		_T("Disable mono video emulation (SW2:1)"),
		_T("mono_card")
	},
	{
		// 9
		_T("Disable color video emulation (SW2:2)"),
		_T("color_card")
	},
	{
		// 10-11
		_T("Address sector (SW2:3-4)\0") _T("A0000-AFFFF (1)\0") _T("A0000-AFFFF (2)\0") _T("D0000-DFFFF\0") _T("E0000-EFFFF\0"),
		_T("memory\0") _T("sector_a0000_1\0") _T("sector_a0000_2\0") _T("sector_d0000\0") _T("sector_e0000\0"),
		true
	},
	{
		// 12
		_T("Disable parallel port emulation (J11)"),
		_T("parport_card")
	},
	{	// 19 - 21
		_T("CPU core\0") _T("Fake86\0") _T("DOSBox simple\0") _T("DOSBox normal\0") _T("DOSBox full\0") _T("DOSBox auto\0"),
		_T("cpu\0") _T("fake86\0") _T("dbsimple\0") _T("dbnormal\0") _T("dbfull\0") _T("dbauto\0"),
		true, false, 19 - 13
	},
	{	// 22
		_T("FPU (DOSBox CPU only)"),
		_T("fpu")
	},
	{	// 23 - 25
		_T("CPU Arch (DOSBox CPU only)\0") _T("auto") _T("386\0") _T("386_prefetch\0") _T("386_slow\0") _T("486_slow\0") _T("486_prefetch\0") _T("pentium_slow\0"),
		_T("cpuarch\0") _T("auto") _T("386\0") _T("386_prefetch\0") _T("386_slow\0") _T("486_slow\0") _T("486_prefetch\0") _T("pentium_slow\0"),
		true, false, 0
	},
	{
		NULL
	}
};

static const struct expansionboardsettings x86_athdxt_settings[] = {
	{
		_T("ROM Address\0") _T("0xCC000\0") _T("0xDC000\0") _T("0xEC000\0"),
		_T("baseaddress\0") _T("0xcc000\0") _T("0xdc000\0") _T("0xec000\0"),
		true
	},
	{
		NULL
	}
};

static void fastlane_memory_callback(struct romconfig *rc, uae_u8 *ac, int size)
{
	struct zfile *z = read_device_from_romconfig(rc, NULL);
	if (z) {
		// load autoconfig data from rom file
		uae_u8 act[16] = { 0 };
		zfile_fseek(z, 0x80, SEEK_SET);
		zfile_fread(act, 1, 16, z);
		zfile_fclose(z);
		for(int i = 1; i < 16; i++) {
			ac[i] = ~act[i];
			act[i] = ~act[i];
		}
		// don't overwrite uae configured memory size
		ac[0] = (ac[0] & 7) | (act[0] & 0xf8);
		ac[2] = (ac[2] & 0x0f) | (act[2] & 0xf0);
	}
}
static void hda506_memory_callback(struct romconfig *rc, uae_u8 *ac, int size)
{
	if (currprefs.cs_a1000ram)
		ac[1] = 1;
	else
		ac[1] = 2;
}
static void nexus_memory_callback(struct romconfig *rc, uae_u8 *ac, int size)
{
	if (rc->device_settings & 1)
		ac[0] &= ~(1 << 5);
	if (size <= 2 * 1024 * 1024)
		ac[1] = 2;
	else if (size <= 4 * 1024 * 1024)
		ac[1] = 4;
	else
		ac[1] = 8;
}
static const struct expansionboardsettings golemfast_settings[] = {
	{
		_T("IDE"),
		_T("ide")
	},
	{
		NULL
	}
};
static const struct expansionboardsettings nexus_settings[] = {
	{
		_T("MEM TEST"),
		_T("memtest")
	},
	{
		NULL
	}
};
static const struct expansionboardsettings adscsi2000_settings[] = {
	{
		_T("Cache (B)"),
		_T("B")
	},
	{
		NULL
	}
};
static const struct expansionboardsettings warpengine_settings[] = {
	{
		_T("Jumper H"),
		_T("jumper_h")
	},
	{
		_T("Jumper J"),
		_T("jumper_j")
	},
	{
		_T("Jumper K"),
		_T("jumper_k")
	},
	{
		NULL
	}
};
static const struct expansionboardsettings a4091_settings[] = {
	{
		_T("Fast Bus"),
		_T("fastbus"),
	},
	{
		_T("Delayed autoboot"),
		_T("delayedautoboot")
	},
	{
		_T("Syncronous mode"),
		_T("syncmode")
	},
	{
		_T("Termination"),
		_T("term")
	},
	{
		_T("LUN"),
		_T("lun")
	},
	{
		NULL
	}
};


const struct expansionromtype expansionroms[] = {
	{
		_T("cpuboard"), _T("Accelerator"), _T("Accelerator"),
		NULL, NULL, add_cpuboard_unit, ROMTYPE_CPUBOARD, 0, 0, 0, true,
		NULL, 0,
		false, EXPANSIONTYPE_SCSI | EXPANSIONTYPE_IDE
	},
	{
		_T("grex"), _T("G-REX"), _T("DCE"),
		grex_init, NULL, NULL, ROMTYPE_GREX | ROMTYPE_NOT, 0, 0, BOARD_NONAUTOCONFIG_BEFORE, true,
		NULL, 0,
		false, EXPANSIONTYPE_PCI_BRIDGE
	},
	{
		_T("mediator"), _T("Mediator"), _T("Elbox"),
		mediator_init, mediator_init2, NULL, ROMTYPE_MEDIATOR | ROMTYPE_NOT, 0, 0, BOARD_AUTOCONFIG_Z3, false,
		mediator_sub, 0,
		false, EXPANSIONTYPE_PCI_BRIDGE,
		0, 0, 0, false, NULL,
		false,
		mediator_settings
	},
	{
		_T("prometheus"), _T("Prometheus"), _T("Matay"),
		prometheus_init, NULL, NULL, ROMTYPE_PROMETHEUS | ROMTYPE_NOT, 0, 0, BOARD_AUTOCONFIG_Z3, false,
		NULL, 0,
		false, EXPANSIONTYPE_PCI_BRIDGE,
		0, 0, 0, false, NULL,
		false,
		bridge_settings
	},
	{
		_T("apollo"), _T("Apollo 500/2000"), _T("3-State"),
		apollo_init_hd, NULL, apollo_add_scsi_unit, ROMTYPE_APOLLOHD, 0, 0, BOARD_AUTOCONFIG_Z2, false,
		NULL, 0,
		false, EXPANSIONTYPE_SCSI | EXPANSIONTYPE_IDE,
		8738, 0, 0
	},
	{
		_T("add500"), _T("ADD-500"), _T("Archos"),
		add500_init, NULL, add500_add_scsi_unit, ROMTYPE_ADD500, 0, 0, BOARD_AUTOCONFIG_Z2, false,
		NULL, 0,
		false, EXPANSIONTYPE_SCSI,
		8498, 27, 0, true, NULL
	},
	{
		_T("blizzardscsikitiv"), _T("SCSI Kit IV"), _T("Blizzard"),
		NULL, NULL, cpuboard_ncr9x_add_scsi_unit, ROMTYPE_BLIZKIT4, 0, 0, 0, true,
		NULL, 0,
		false, EXPANSIONTYPE_SCSI
	},
	{
		_T("oktagon2008"), _T("Oktagon 2008"), _T("BSC/Alfa Data"),
		ncr_oktagon_autoconfig_init, NULL, oktagon_add_scsi_unit, ROMTYPE_OKTAGON, 0, 0, BOARD_AUTOCONFIG_Z2, false,
		NULL, 0,
		true, EXPANSIONTYPE_SCSI
	},
	{
		_T("alfapower"), _T("AlfaPower/AT-Bus 2008"), _T("BSC/Alfa Data"),
		alf_init, NULL, alf_add_ide_unit, ROMTYPE_ALFA, 0, 0, BOARD_AUTOCONFIG_Z2, false,
		NULL, 0,
		false, EXPANSIONTYPE_IDE,
		2092, 8, 0
	},
	{
		_T("alfapowerplus"), _T("AlfaPower Plus"), _T("BSC/Alfa Data"),
		alf_init, NULL, alf_add_ide_unit, ROMTYPE_ALFAPLUS, 0, 0, BOARD_AUTOCONFIG_Z2, false,
		NULL, 0,
		false, EXPANSIONTYPE_IDE,
		2092, 8, 0
	},
	{
		_T("cltda1000scsi"), _T("A1000/A2000 SCSI"), _T("C-Ltd"),
		cltda1000scsi_init, NULL, cltda1000scsi_add_scsi_unit, ROMTYPE_CLTDSCSI | ROMTYPE_NOT, 0, 0, BOARD_AUTOCONFIG_Z2, false,
		NULL, 0,
		false, EXPANSIONTYPE_SCSI,
		0, 0, 0, false, NULL,
		false, NULL,
		{ 0xc1, 0x0c, 0x00, 0x00, 0x03, 0xec, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	},
	{
		_T("a2090a"), _T("A2090a"), _T("Commodore"),
		a2090_init, NULL, a2090_add_scsi_unit, ROMTYPE_A2090 | ROMTYPE_NONE, 0, 0, BOARD_AUTOCONFIG_Z2, false,
		NULL, 0,
		true, EXPANSIONTYPE_SCSI | EXPANSIONTYPE_CUSTOM_SECONDARY
	},
	{
		_T("a2091"), _T("A590/A2091"), _T("Commodore"),
		a2091_init, NULL, a2091_add_scsi_unit, ROMTYPE_A2091 | ROMTYPE_NONE, 0, 0, BOARD_AUTOCONFIG_Z2, false,
		a2091_sub, 1,
		true, EXPANSIONTYPE_SCSI | EXPANSIONTYPE_CUSTOM_SECONDARY,
		0, 0, 0, true, NULL
	},
	{
		_T("a4091"), _T("A4091"), _T("Commodore"),
		ncr710_a4091_autoconfig_init, NULL, a4091_add_scsi_unit, ROMTYPE_A4091, 0, 0, BOARD_AUTOCONFIG_Z3, false,
		NULL, 0,
		false, EXPANSIONTYPE_SCSI,
		0, 0, 0, false, NULL,
		true, a4091_settings
	},
	{
		_T("dataflyerscsiplus"), _T("DataFlyer SCSI+"), _T("Expansion Systems"),
		dataflyer_init, NULL, dataflyer_add_scsi_unit, ROMTYPE_DATAFLYER | ROMTYPE_NOT, 0, 0, BOARD_NONAUTOCONFIG_BEFORE, true,
		NULL, 0,
		false, EXPANSIONTYPE_SCSI
	},
	{
		_T("gvp1"), _T("GVP Series I"), _T("Great Valley Products"),
		gvp_init_s1, NULL, gvp_s1_add_scsi_unit, ROMTYPE_GVPS1 | ROMTYPE_NONE, ROMTYPE_GVPS12, 0, BOARD_AUTOCONFIG_Z2, false,
		gvp1_sub, 1,
		true, EXPANSIONTYPE_SCSI
	},
	{
		_T("gvp"), _T("GVP Series II"), _T("Great Valley Products"),
		gvp_init_s2, NULL, gvp_s2_add_scsi_unit, ROMTYPE_GVPS2 | ROMTYPE_NONE, ROMTYPE_GVPS12, 0, BOARD_AUTOCONFIG_Z2, false,
		NULL, 0,
		true, EXPANSIONTYPE_SCSI,
		2017, 10, 0
	},
	{
		_T("vector"), _T("Vector Falcon 8000"), _T("HK-Computer"),
		vector_init, NULL, vector_add_scsi_unit, ROMTYPE_VECTOR, 0, 0, BOARD_AUTOCONFIG_Z2, false,
		NULL, 0,
		false, EXPANSIONTYPE_SCSI
	},
	{
		_T("adide"), _T("AdIDE"), _T("ICD"),
		adide_init, NULL, adide_add_ide_unit, ROMTYPE_ADIDE | ROMTYPE_NONE, 0, 0, BOARD_AUTOCONFIG_Z2, false,
		NULL, 0,
		true, EXPANSIONTYPE_IDE
	},
	{
		_T("adscsi2000"), _T("AdSCSI Advantage 2000"), _T("ICD"),
		adscsi_init, NULL, adscsi_add_scsi_unit, ROMTYPE_ADSCSI, 0, 0, BOARD_AUTOCONFIG_Z2, false,
		NULL, 0,
		false, EXPANSIONTYPE_SCSI,
		0, 0, 0, false, NULL,
		true,
		adscsi2000_settings
	},
	{
		_T("kommos"), _T("A500/A2000 SCSI"), _T("Jürgen Kommos"),
		kommos_init, NULL, kommos_add_scsi_unit, ROMTYPE_KOMMOS, 0, 0, BOARD_NONAUTOCONFIG_BEFORE, true,
		NULL, 0,
		false, EXPANSIONTYPE_SCSI
	},
	{
		_T("golem"), _T("Golem SCSI II"), _T("Kupke"),
		golem_init, NULL, golem_add_scsi_unit, ROMTYPE_GOLEM, 0, 0, BOARD_AUTOCONFIG_Z2, false,
		NULL, 0,
		true, EXPANSIONTYPE_SCSI,
		2079, 3, 0
	},
	{
		_T("golemfast"), _T("Golem Fast SCSI/IDE"), _T("Kupke"),
		golemfast_init, NULL, golemfast_add_idescsi_unit, ROMTYPE_GOLEMFAST, 0, 0, BOARD_AUTOCONFIG_Z2, false,
		NULL, 0,
		true, EXPANSIONTYPE_SCSI | EXPANSIONTYPE_IDE,
		0, 0, 0, false, NULL,
		true,
		golemfast_settings
	},
	{
		_T("multievolution"), _T("Multi Evolution 500/2000"), _T("MacroSystem"),
		ncr_multievolution_init, NULL, multievolution_add_scsi_unit, ROMTYPE_MEVOLUTION, 0, 0, BOARD_NONAUTOCONFIG_BEFORE, true,
		NULL, 0,
		false, EXPANSIONTYPE_SCSI,
		18260, 8, 0, true
	},
	{
		_T("paradox"), _T("Paradox SCSI"), _T("Mainhattan Data"),
		paradox_init, NULL, paradox_add_scsi_unit, ROMTYPE_PARADOX | ROMTYPE_NOT, 0, 0, BOARD_NONAUTOCONFIG_BEFORE, false,
		NULL, 0,
		true, EXPANSIONTYPE_SCSI | EXPANSIONTYPE_PARALLEL_ADAPTER
	},
	{
		_T("mtecat"), _T("AT 500"), _T("M-Tec"),
		mtec_init, NULL, mtec_add_ide_unit, ROMTYPE_MTEC, 0, 0, BOARD_AUTOCONFIG_Z2, false,
		NULL, 0,
		true, EXPANSIONTYPE_IDE
	},
	{
		_T("masoboshi"), _T("MasterCard"), _T("Masoboshi"),
		masoboshi_init, NULL, masoboshi_add_idescsi_unit, ROMTYPE_MASOBOSHI | ROMTYPE_NONE, 0, 0, BOARD_AUTOCONFIG_Z2, false,
		masoboshi_sub, 0,
		true, EXPANSIONTYPE_SCSI | EXPANSIONTYPE_IDE
	},
	{
		_T("stardrive"), _T("StarDrive"), _T("Microbotics"),
		stardrive_init, NULL, stardrive_add_scsi_unit, ROMTYPE_STARDRIVE | ROMTYPE_NOT, 0, 0, BOARD_AUTOCONFIG_Z2, true,
		NULL, 0,
		false, EXPANSIONTYPE_SCSI,
		1010, 0, 0, false, NULL,
		false, NULL,
		{ 0xc1, 2, 0x00, 0x00, 0x03, 0xf2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },

	},
	{
		_T("fastlane"), _T("Fastlane"), _T("Phase 5"),
		ncr_fastlane_autoconfig_init, NULL, fastlane_add_scsi_unit, ROMTYPE_FASTLANE, 0, 0, BOARD_AUTOCONFIG_Z3, false,
		NULL, 0,
		false, EXPANSIONTYPE_SCSI,
		8512, 10, 0, false, fastlane_memory_callback
	},
	{
		_T("ptnexus"), _T("Nexus"), _T("Preferred Technologies"),
		ptnexus_init, NULL, ptnexus_add_scsi_unit, ROMTYPE_PTNEXUS | ROMTYPE_NONE, 0, 0, BOARD_AUTOCONFIG_Z2, false,
		NULL, 0,
		false, EXPANSIONTYPE_SCSI,
		2102, 4, 0, false, nexus_memory_callback,
		false, nexus_settings,
		{ 0xd1, 0x01, 0x00, 0x00, 0x08, 0x36, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00 },
	},
	{
		_T("protar"), _T("A500 HD"), _T("Protar"),
		protar_init, NULL, protar_add_ide_unit, ROMTYPE_PROTAR, 0, 0, BOARD_AUTOCONFIG_Z2, false,
		NULL, 0,
		true, EXPANSIONTYPE_SCSI,
		4149, 51, 0
	},
	{
		_T("rochard"), _T("RocHard RH800C"), _T("Roctec"),
		rochard_init, NULL, rochard_add_idescsi_unit, ROMTYPE_ROCHARD | ROMTYPE_NONE, 0, 0, BOARD_AUTOCONFIG_Z2, false,
		rochard_sub, 0,
		true, EXPANSIONTYPE_IDE | EXPANSIONTYPE_SCSI | EXPANSIONTYPE_IDE_PORT_DOUBLED,
		2144, 2, 0
	},
	{
		_T("supradrive"), _T("SupraDrive"), _T("Supra Corporation"),
		supra_init, NULL, supra_add_scsi_unit, ROMTYPE_SUPRA | ROMTYPE_NONE, 0, 0, BOARD_AUTOCONFIG_Z2, false,
		supra_sub, 0,
		true, EXPANSIONTYPE_SCSI
	},
#if 0 /* driver is MIA, 3rd party ScottDevice driver is not enough for full implementation. */
	{
		_T("microforge"), _T("Hard Disk"), _T("Micro Forge"),
		microforge_init, NULL, microforge_add_scsi_unit, ROMTYPE_MICROFORGE | ROMTYPE_NOT, 0, 0, BOARD_NONAUTOCONFIG_BEFORE, true,
		NULL, 0,
		false, EXPANSIONTYPE_SASI | EXPANSIONTYPE_SCSI
	},
#endif

	{
		_T("omtiadapter"), _T("OMTI-Adapter"), _T("C't"),
		omtiadapter_init, NULL, omtiadapter_scsi_unit, ROMTYPE_OMTIADAPTER | ROMTYPE_NOT, 0, 0, BOARD_NONAUTOCONFIG_BEFORE, true,
		NULL, 0,
		false, EXPANSIONTYPE_CUSTOM | EXPANSIONTYPE_SCSI
	},
	{
		_T("alf1"), _T("A.L.F."), _T("Elaborate Bytes"),
		alf1_init, NULL, alf1_add_scsi_unit, ROMTYPE_ALF1 | ROMTYPE_NOT, 0, 0, BOARD_NONAUTOCONFIG_BEFORE, true,
		NULL, 0,
		false, EXPANSIONTYPE_CUSTOM | EXPANSIONTYPE_SCSI
	},
	{
		_T("promigos"), _T("Promigos"), _T("Flesch und Hörnemann"),
		promigos_init, NULL, promigos_add_scsi_unit, ROMTYPE_PROMIGOS | ROMTYPE_NOT, 0, 0, BOARD_NONAUTOCONFIG_BEFORE, true,
		NULL, 0,
		false, EXPANSIONTYPE_CUSTOM | EXPANSIONTYPE_SCSI
	},
	{
		_T("tecmar"), _T("T-Card/T-Disk"), _T("Tecmar"),
		tecmar_init, NULL, tecmar_add_scsi_unit, ROMTYPE_TECMAR | ROMTYPE_NOT, 0, 0, BOARD_NONAUTOCONFIG_BEFORE, true,	
		NULL, 0,
		false, EXPANSIONTYPE_SASI | EXPANSIONTYPE_SCSI
	},
	{
		_T("system2000"), _T("System 2000"), _T("Vortex"),
		system2000_init, NULL, system2000_add_scsi_unit, ROMTYPE_SYSTEM2000 | ROMTYPE_NONE, 0, 0, BOARD_NONAUTOCONFIG_BEFORE, true,
		NULL, 0,
		false, EXPANSIONTYPE_CUSTOM | EXPANSIONTYPE_SCSI
	},
	{
		_T("xebec"), _T("9720H"), _T("Xebec"),
		xebec_init, NULL, xebec_add_scsi_unit, ROMTYPE_XEBEC | ROMTYPE_NOT, 0, 0, BOARD_NONAUTOCONFIG_BEFORE, true,
		NULL, 0,
		false, EXPANSIONTYPE_SASI | EXPANSIONTYPE_SCSI
	},
#if 0
	{
		_T("kronos"), _T("Kronos"), _T("C-Ltd"),
		kronos_init, kronos_add_scsi_unit, ROMTYPE_KRONOS, 0, 0, 2, false,
		NULL, 0,
		false, EXPANSIONTYPE_SCSI,
		0, 0, 0,
		{ 0xd1, 4, 0x00, 0x00, 0x03, 0xec, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00 },
	},
#endif
	{
		_T("hda506"), _T("HDA-506"), _T("Spirit Technology"),
		hda506_init, NULL, hda506_add_scsi_unit, ROMTYPE_HDA506 | ROMTYPE_NOT, 0, 0, BOARD_AUTOCONFIG_Z2, true,
		NULL, 0,
		false, EXPANSIONTYPE_CUSTOM | EXPANSIONTYPE_SCSI,
		0, 0, 0, false, NULL,
		false, NULL,
		{ 0xc1, 0x04, 0x00, 0x00, 0x07, 0xf2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
	},
	{
		_T("amax"), _T("AMAX ROM dongle"), _T("ReadySoft"),
		NULL, NULL, NULL, ROMTYPE_AMAX | ROMTYPE_NONE, 0, 0, 0, false
	},

#if 0
	{
		_T("x86_xt_hd"), _T("x86 XT"), _T("x86"),
		x86_xt_hd_init, NULL, x86_add_xt_hd_unit, ROMTYPE_X86_HD | ROMTYPE_NONE, 0, 0, BOARD_NONAUTOCONFIG_AFTER_Z2, true,
		NULL, 0,
		false, EXPANSIONTYPE_CUSTOM | EXPANSIONTYPE_SCSI
	},
#endif
	{
		_T("x86athdprimary"), _T("AT IDE Primary"), _T("x86"),
		x86_at_hd_init_1, NULL, x86_add_at_hd_unit_1, ROMTYPE_X86_AT_HD1 | ROMTYPE_NOT, 0, 0, BOARD_NONAUTOCONFIG_AFTER_Z2, true,
		NULL, 0,
		false, EXPANSIONTYPE_IDE
	},
	{
		_T("x86athdsecondary"), _T("AT IDE Secondary"), _T("x86"),
		x86_at_hd_init_2, NULL, x86_add_at_hd_unit_2, ROMTYPE_X86_AT_HD2 | ROMTYPE_NOT, 0, 0, BOARD_NONAUTOCONFIG_AFTER_Z2, true,
		NULL, 0,
		false, EXPANSIONTYPE_IDE
	},
	{
		_T("x86athdxt"), _T("XTIDE Universal BIOS HD"), _T("x86"),
		x86_at_hd_init_xt, NULL, x86_add_at_hd_unit_xt, ROMTYPE_X86_XT_IDE | ROMTYPE_NONE, 0, 0, BOARD_NONAUTOCONFIG_AFTER_Z2, true,
		NULL, 0,
		false, EXPANSIONTYPE_IDE,
		0, 0, 0, false, NULL,
		false,
		x86_athdxt_settings
	},

	{
		_T("a1060"), _T("A1060 Sidecar"), _T("Commodore"),
		a1060_init, NULL, NULL, ROMTYPE_A1060 | ROMTYPE_NONE, 0, 0, BOARD_AUTOCONFIG_Z2, true,
		NULL, 0,
		false, EXPANSIONTYPE_X86_BRIDGE,
		0, 0, 0, false, NULL,
		false,
		x86_bridge_sidecar_settings
	},
	{
		_T("a2088"), _T("A2088"), _T("Commodore"),
		a2088xt_init, NULL, NULL, ROMTYPE_A2088 | ROMTYPE_NONE, 0, 0, BOARD_AUTOCONFIG_Z2, true,
		NULL, 0,
		false, EXPANSIONTYPE_X86_BRIDGE,
		0, 0, 0, false, NULL,
		false,
		x86_bridge_settings
	},
	{
		_T("a2088t"), _T("A2088T"), _T("Commodore"),
		a2088t_init, NULL, NULL, ROMTYPE_A2088T | ROMTYPE_NONE, 0, 0, BOARD_AUTOCONFIG_Z2, true,
		NULL, 0,
		false, EXPANSIONTYPE_X86_BRIDGE,
		0, 0, 0, false, NULL,
		false,
		x86_bridge_settings
	},
	{
		_T("a2286"), _T("A2286"), _T("Commodore"),
		a2286_init, NULL, NULL, ROMTYPE_A2286 | ROMTYPE_NONE, 0, 0, BOARD_AUTOCONFIG_Z2, true,
		NULL, 0,
		false, EXPANSIONTYPE_X86_BRIDGE,
		0, 0, 0, false, NULL,
		false,
		x86at286_bridge_settings
	},
	{
		_T("a2386"), _T("A2386SX"), _T("Commodore"),
		a2386_init, NULL, NULL, ROMTYPE_A2386 | ROMTYPE_NONE, 0, 0, BOARD_AUTOCONFIG_Z2, true,
		NULL, 0,
		false, EXPANSIONTYPE_X86_BRIDGE,
		0, 0, 0, false, NULL,
		false,
		x86at386_bridge_settings
	},

	// only here for rom selection
	{
		_T("picassoiv"), _T("Picasso IV"), _T("Village Tronic"),
		NULL, NULL, NULL, ROMTYPE_PICASSOIV | ROMTYPE_NONE, 0, 0, BOARD_IGNORE, true,
		NULL, 0,
		false, EXPANSIONTYPE_RTG,
	},
	{
		_T("x86vga"), _T("x86 VGA"), _T("x86"),
		NULL, NULL, NULL, ROMTYPE_x86_VGA | ROMTYPE_NONE, 0, 0, BOARD_IGNORE, true,
		NULL, 0,
		false, EXPANSIONTYPE_RTG,
	},


	{
		NULL
	}
};

static const struct expansionboardsettings blizzardboard_settings[] = {
	{
		_T("MapROM"),
		_T("maprom")
	},
	{
		NULL
	}
};

static const struct cpuboardsubtype gvpboard_sub[] = {
	{
		_T("A3001 Series I"),
		_T("A3001SI"),
		ROMTYPE_CB_A3001S1, 0,
		gvp_add_ide_unit, EXPANSIONTYPE_IDE | EXPANSIONTYPE_24BIT,
		BOARD_MEMORY_Z2,
		8 * 1024 * 1024,
		0,
		gvp_ide_rom_autoconfig_init, NULL, BOARD_AUTOCONFIG_Z2, 0
	},
	{
		_T("A3001 Series II"),
		_T("A3001SII"),
		0, 0,
		gvp_add_ide_unit, EXPANSIONTYPE_IDE | EXPANSIONTYPE_24BIT,
		BOARD_MEMORY_Z2,
		8 * 1024 * 1024,
		0,
		gvp_ide_rom_autoconfig_init, gvp_ide_controller_autoconfig_init, BOARD_AUTOCONFIG_Z2, 0
	},
	{
		_T("A530"),
		_T("GVPA530"),
		ROMTYPE_GVPS2, 0,
		gvp_s2_add_accelerator_scsi_unit, EXPANSIONTYPE_SCSI | EXPANSIONTYPE_24BIT,
		BOARD_MEMORY_Z2,
		8 * 1024 * 1024,
		0,
		gvp_init_accelerator, NULL, BOARD_AUTOCONFIG_Z2, 1,
		NULL, NULL,
		2017, 9, 0, false
	},
	{
		_T("G-Force 030"),
		_T("GVPGFORCE030"),
		ROMTYPE_GVPS2, ROMTYPE_GVPS12,
		gvp_s2_add_accelerator_scsi_unit, EXPANSIONTYPE_SCSI,
		BOARD_MEMORY_25BITMEM,
		128 * 1024 * 1024,
		0,
		gvp_init_accelerator, NULL, BOARD_AUTOCONFIG_Z2, 1
	},
	{
		_T("Tek Magic 2040/2060"),
		_T("TekMagic"),
		ROMTYPE_CB_TEKMAGIC, 0,
		tekmagic_add_scsi_unit, EXPANSIONTYPE_SCSI,
		BOARD_MEMORY_HIGHMEM,
		128 * 1024 * 1024
	},
	{
		NULL
	}
};
static const struct cpuboardsubtype blizzardboard_sub[] = {
	{
		_T("Blizzard 1230 IV"),
		_T("Blizzard1230IV"),
		ROMTYPE_CB_BLIZ1230, 0,
		NULL, 0,
		BOARD_MEMORY_BLIZZARD_12xx,
		256 * 1024 * 1024,
		0,
		NULL, NULL, 0, 0,
		blizzardboard_settings
	},
	{
		_T("Blizzard 1260"),
		_T("Blizzard1260"),
		ROMTYPE_CB_BLIZ1260, 0,
		NULL, 0,
		BOARD_MEMORY_BLIZZARD_12xx,
		256 * 1024 * 1024,
		0,
		NULL, NULL, 0, 0,
		blizzardboard_settings
	},
	{
		_T("Blizzard 2060"),
		_T("Blizzard2060"),
		ROMTYPE_CB_BLIZ2060, 0,
		cpuboard_ncr9x_add_scsi_unit, EXPANSIONTYPE_SCSI,
		BOARD_MEMORY_HIGHMEM,
		128 * 1024 * 1024,
		0,
		NULL, NULL, 0, 0,
		blizzardboard_settings
	},
	{
		_T("Blizzard PPC"),
		_T("BlizzardPPC"),
		ROMTYPE_CB_BLIZPPC, 0,
		blizzardppc_add_scsi_unit, EXPANSIONTYPE_SCSI,
		BOARD_MEMORY_BLIZZARD_PPC,
		256 * 1024 * 1024
	},
	{
		NULL
	}
};
static const struct cpuboardsubtype cyberstormboard_sub[] = {
	{
		_T("CyberStorm MK I"),
		_T("CyberStormMK1"),
		ROMTYPE_CB_CSMK1, 0,
		cpuboard_ncr9x_add_scsi_unit, EXPANSIONTYPE_SCSI,
		BOARD_MEMORY_HIGHMEM,
		128 * 1024 * 1024
	},
	{
		_T("CyberStorm MK II"),
		_T("CyberStormMK2"),
		ROMTYPE_CB_CSMK2, 0,
		cpuboard_ncr9x_add_scsi_unit, EXPANSIONTYPE_SCSI,
		BOARD_MEMORY_HIGHMEM,
		128 * 1024 * 1024
	},
	{
		_T("CyberStorm MK III"),
		_T("CyberStormMK3"),
		ROMTYPE_CB_CSMK3, 0,
		cyberstorm_add_scsi_unit, EXPANSIONTYPE_SCSI,
		BOARD_MEMORY_HIGHMEM,
		128 * 1024 * 1024
	},
	{
		_T("CyberStorm PPC"),
		_T("CyberStormPPC"),
		ROMTYPE_CB_CSPPC, 0,
		cyberstorm_add_scsi_unit, EXPANSIONTYPE_SCSI,
		BOARD_MEMORY_HIGHMEM,
		128 * 1024 * 1024
	},
	{
		NULL
	}
};
static const struct cpuboardsubtype warpengine_sub[] = {
	{
		_T("Warp Engine A4000"),
		_T("WarpEngineA4000"),
		ROMTYPE_CB_WENGINE, 0,
		warpengine_add_scsi_unit, EXPANSIONTYPE_SCSI,
		BOARD_MEMORY_HIGHMEM,
		128 * 1024 * 1024,
		0x01000000,
		ncr710_warpengine_autoconfig_init, NULL, BOARD_AUTOCONFIG_Z3, 1,
		warpengine_settings
	},
	{
		NULL
	}
};
static const struct expansionboardsettings mtec_settings[] = {
	{
		_T("SCSI disabled"),
		_T("scsioff")
	},
	{
		NULL
	}
};
static const struct cpuboardsubtype mtec_sub[] = {
	{
		_T("E-Matrix 530"),
		_T("e-matrix530"),
		ROMTYPE_CB_EMATRIX, 0,
		ematrix_add_scsi_unit, EXPANSIONTYPE_SCSI,
		BOARD_MEMORY_EMATRIX,
		128 * 1024 * 1024,
		0,
		ncr_ematrix_autoconfig_init, NULL, BOARD_AUTOCONFIG_Z2, 1,
		mtec_settings
	},
	{
		NULL
	}
};
static const struct expansionboardsettings a26x0board_settings[] = {
	{
		_T("OSMODE (J304)"),
		_T("j304")
	},
	{
		NULL
	}
};
static const struct cpuboardsubtype commodore_sub[] = {
	{
		_T("A2620/A2630"),
		_T("A2630"),
		ROMTYPE_CB_A26x0, 0,
		NULL, 0,
		BOARD_MEMORY_25BITMEM,
		128 * 1024 * 1024,
		0,
		NULL, NULL, 0, 0,
		a26x0board_settings,
		cpuboard_io_special
	},
	{
		NULL
	}
};
static const struct cpuboardsubtype dbk_sub[] = {
	{
		_T("1230/1240"),
		_T("DKB12x0"),
		ROMTYPE_CB_DKB12x0, 0,
		cpuboard_dkb_add_scsi_unit, EXPANSIONTYPE_SCSI,
		0,
		128 * 1024 * 1024,
		0,
		ncr_dkb_autoconfig_init, NULL, BOARD_AUTOCONFIG_Z2, 0
	},
	{
		_T("Wildfire"),
		_T("wildfire"),
		ROMTYPE_CB_DBK_WF, 0,
		wildfire_add_scsi_unit, EXPANSIONTYPE_SCSI,
		BOARD_MEMORY_HIGHMEM,
		128 * 1024 * 1024,
		0,
		dkb_wildfire_pci_init, NULL, BOARD_NONAUTOCONFIG_BEFORE, 0
	},
	{
		NULL
	}
};
static const struct cpuboardsubtype fusionforty_sub[] = {
	{
		_T("Fusion Forty"),
		_T("FusionForty"),
		ROMTYPE_CB_FUSION, 0,
		NULL, 0,
		BOARD_MEMORY_HIGHMEM,
		128 * 1024 * 1024
	},
	{
		NULL
	}
};
static const struct cpuboardsubtype apollo_sub[] = {
	{
		_T("Apollo 1240/1260"),
		_T("Apollo"),
		ROMTYPE_CB_APOLLO, 0,
		apollo_add_scsi_unit, EXPANSIONTYPE_SCSI,
		BOARD_MEMORY_HIGHMEM,
		128 * 1024 * 1024,
		0,
		apollo_init_cpu, NULL, 2, 0
	},
	{
		NULL
	}
};
static const struct cpuboardsubtype kupkeboard_sub[] = {
	{
		_T("Golem 030"),
		_T("golem030"),
		ROMTYPE_CB_GOLEM030, 0,
		NULL, 0,
		BOARD_MEMORY_25BITMEM,
		16 * 1024 * 1024
	},
	{
		NULL
	}
};
static const struct cpuboardsubtype icboard_sub[] = {
	{
		_T("ACA 500"),
		_T("aca500"),
		ROMTYPE_CB_ACA500, 0,
		NULL, EXPANSIONTYPE_24BIT
	},
	{
		NULL
	}
};

static const struct cpuboardsubtype dummy_sub[] = {
	{ NULL }
};

const struct cpuboardtype cpuboards[] = {
	{
		-1,
		_T("-"),
		dummy_sub, 0
	},
	{
		BOARD_ACT,
		_T("ACT"),
		apollo_sub, 0
	},
	{
		BOARD_COMMODORE,
		_T("Commodore"),
		commodore_sub, 0
	},
	{
		BOARD_DKB,
		_T("DKB"),
		dbk_sub, 0
	},
	{
		BOARD_GVP,
		_T("Great Valley Products"),
		gvpboard_sub, 0
	},
	{
		BOARD_KUPKE,
		_T("Kupke"),
		kupkeboard_sub, 0
	},
	{
		BOARD_MACROSYSTEM,
		_T("MacroSystem"),
		warpengine_sub, 0
	},
	{
		BOARD_MTEC,
		_T("M-Tec"),
		mtec_sub, 0
	},
	{
		BOARD_BLIZZARD,
		_T("Phase 5 - Blizzard"),
		blizzardboard_sub, 0
	},
	{
		BOARD_CYBERSTORM,
		_T("Phase 5 - CyberStorm"),
		cyberstormboard_sub, 0
	},
	{
		BOARD_RCS,
		_T("RCS Management"),
		fusionforty_sub, 0
	},
#if 0
	{
		BOARD_IC,
		_T("Individual Computers"),
		icboard_sub, 0
	},
#endif
	{
		NULL
	}
};
