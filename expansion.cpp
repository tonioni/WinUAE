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

//#define EXP_DEBUG

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "uae.h"
#include "memory.h"
#include "rommgr.h"
#include "autoconf.h"
#include "custom.h"
#include "newcpu.h"
#include "savestate.h"
#include "zfile.h"
#include "catweasel.h"
#include "cdtv.h"
#include "a2091.h"
#include "a2065.h"
#include "gfxboard.h"
#include "cd32_fmv.h"
#include "ncr_scsi.h"
#include "debug.h"
#include "gayle.h"

#define MAX_EXPANSION_BOARDS 8

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

#define zorroII		0xc0 /* Type of Expansion Card */
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
bool uae_boot_rom;
int uae_boot_rom_size; /* size = code size only */
static bool chipdone;

/* ********************************************************** */

static void (*card_init[MAX_EXPANSION_BOARDS]) (void);
static void (*card_map[MAX_EXPANSION_BOARDS]) (void);
static TCHAR *card_name[MAX_EXPANSION_BOARDS];

static int ecard, cardno, z3num;

static uae_u16 uae_id;

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


static uae_u32 REGPARAM3 expamem_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 expamem_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 expamem_bget (uaecptr) REGPARAM;
static void REGPARAM3 expamem_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 expamem_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 expamem_bput (uaecptr, uae_u32) REGPARAM;

addrbank expamem_bank = {
	expamem_lget, expamem_wget, expamem_bget,
	expamem_lput, expamem_wput, expamem_bput,
	default_xlate, default_check, NULL, _T("Autoconfig"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO | ABFLAG_SAFE
};

static void expamem_map_clear (void)
{
	write_log (_T("expamem_map_clear() got called. Shouldn't happen.\n"));
}

static void expamem_init_clear (void)
{
	memset (expamem, 0xff, sizeof expamem);
}
/* autoconfig area is "non-existing" after last device */
static void expamem_init_clear_zero (void)
{
	map_banks (&dummy_bank, 0xe8, 1, 0);
}

static void expamem_init_clear2 (void)
{
	expamem_bank.name = _T("Autoconfig");
	expamem_init_clear_zero ();
	ecard = cardno;
}

static void expamem_init_last (void)
{
	expamem_init_clear2 ();
	write_log (_T("Memory map after autoconfig:\n"));
	memory_map_dump ();
}

void expamem_next (void)
{
	expamem_init_clear ();
	map_banks (&expamem_bank, 0xE8, 1, 0);
	++ecard;
	if (ecard < cardno) {
		expamem_bank.name = card_name[ecard] ? card_name[ecard] : (TCHAR*) _T("None");
		(*card_init[ecard]) ();
	} else {
		expamem_init_clear2 ();
	}
}

static uae_u32 REGPARAM2 expamem_lget (uaecptr addr)
{
	write_log (_T("warning: READ.L from address $%lx PC=%x\n"), addr, M68K_GETPC);
	return (expamem_wget (addr) << 16) | expamem_wget (addr + 2);
}

static uae_u32 REGPARAM2 expamem_wget (uaecptr addr)
{
	uae_u32 v = (expamem_bget (addr) << 8) | expamem_bget (addr + 1);
	write_log (_T("warning: READ.W from address $%lx=%04x PC=%x\n"), addr, v & 0xffff, M68K_GETPC);
	return v;
}

static uae_u32 REGPARAM2 expamem_bget (uaecptr addr)
{
	uae_u8 b;
#ifdef JIT
	special_mem |= S_READ;
#endif
	if (!chipdone) {
		chipdone = true;
		addextrachip (get_long (4));
	}
	addr &= 0xFFFF;
	b = expamem[addr];
#ifdef EXP_DEBUG
	write_log (_T("expamem_bget %x %x\n"), addr, b);
#endif
	return b;
}

static void REGPARAM2 expamem_write (uaecptr addr, uae_u32 value)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
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
	return ((expamem[0] | expamem[2] >> 4) & 0xc0);
}

static void REGPARAM2 expamem_lput (uaecptr addr, uae_u32 value)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	write_log (_T("warning: WRITE.L to address $%lx : value $%lx\n"), addr, value);
}

static void REGPARAM2 expamem_wput (uaecptr addr, uae_u32 value)
{
#ifdef EXP_DEBUG
	write_log (_T("expamem_wput %x %x\n"), addr, value);
#endif
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	value &= 0xffff;
	if (ecard >= cardno)
		return;
	if (expamem_type () != zorroIII)
		write_log (_T("warning: WRITE.W to address $%lx : value $%x\n"), addr, value);
	else {
		switch (addr & 0xff) {
		case 0x44:
			if (expamem_type () == zorroIII) {
				if (currprefs.jit_direct_compatible_memory) {
					uae_u32 p1, p2 = 0;
					// +Bernd Roesch & Toni Wilen
					p1 = get_word (regs.regs[11] + 0x20);
					if (expamem[0] & add_memory) {
						// Z3 RAM expansion
						p2 = 0;
						while (!p2 && z3num < 2) {
							if (z3num == 0 && currprefs.z3fastmem_size)
								p2 = z3fastmem_bank.start >> 16;
							else if (z3num == 1 && currprefs.z3fastmem2_size)
								p2 = z3fastmem2_bank.start >> 16;
							if (!p2)
								z3num++;
						}
						z3num++;
					} else {
						// Z3 P96 RAM
						if (gfxmem_bank.start & 0xff000000)
							p2 = gfxmem_bank.start >> 16;
					}
					put_word (regs.regs[11] + 0x20, p2);
					put_word (regs.regs[11] + 0x28, p2);
					// -Bernd Roesch
					expamem_hi = p2;
					(*card_map[ecard]) ();
					if (p1 != p2)
						write_log (_T("   Card %d remapped %04x0000 -> %04x0000\n"), ecard + 1, p1, p2);
				} else {
					expamem_hi = value & 0xff00;
					(*card_map[ecard]) ();
				}
				write_log (_T("   Card %d (Zorro%s) done.\n"), ecard + 1, expamem_type () == 0xc0 ? _T("II") : _T("III"));
				expamem_next ();
			}
			break;
		case 0x4c:
			write_log (_T("   Card %d (Zorro%s) had no success.\n"), ecard + 1, expamem_type () == 0xc0 ? _T("II") : _T("III"));
			expamem_hi = expamem_lo = 0;
			(*card_map[ecard]) ();
			break;
		}
	}
}

static void REGPARAM2 expamem_bput (uaecptr addr, uae_u32 value)
{
#ifdef EXP_DEBUG
	write_log (_T("expamem_bput %x %x\n"), addr, value);
#endif
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	if (ecard >= cardno)
		return;
	value &= 0xff;
	switch (addr & 0xff) {
	case 0x30:
	case 0x32:
		expamem_hi = 0;
		expamem_lo = 0;
		expamem_write (0x48, 0x00);
		break;

	case 0x48:
		if (expamem_type () == zorroII) {
			expamem_hi = value;
			(*card_map[ecard]) ();
			write_log (_T("   Card %d (Zorro%s) done.\n"), ecard + 1, expamem_type () == 0xc0 ? _T("II") : _T("III"));
			expamem_next ();
		} else if (expamem_type() == zorroIII)
			expamem_lo = value;
		break;

	case 0x4a:
		if (expamem_type () == zorroII)
			expamem_lo = value;
		break;

	case 0x4c:
		expamem_hi = expamem_lo = 0;
		(*card_map[ecard]) ();
		write_log (_T("   Card %d (Zorro%s) had no success.\n"), ecard + 1, expamem_type () == 0xc0 ? _T("II") : _T("III"));
		expamem_next ();
		break;
	}
}

#ifdef CD32

static void expamem_map_cd32fmv (void)
{
	uaecptr start = ((expamem_hi | (expamem_lo >> 4)) << 16);
	cd32_fmv_init (start);
}

static void expamem_init_cd32fmv (void)
{
	int ids[] = { 23, -1 };
	struct romlist *rl = getromlistbyids (ids);
	struct romdata *rd;
	struct zfile *z;

	expamem_init_clear ();
	if (!rl)
		return;
	write_log (_T("CD32 FMV ROM '%s' %d.%d\n"), rl->path, rl->rd->ver, rl->rd->rev);
	rd = rl->rd;
	z = read_rom (&rd);
	if (z) {
		zfile_fread (expamem, 128, 1, z);
		zfile_fclose (z);
	}
}

#endif

/* ********************************************************** */

/*
*  Fast Memory
*/


MEMORY_FUNCTIONS(fastmem);

addrbank fastmem_bank = {
	fastmem_lget, fastmem_wget, fastmem_bget,
	fastmem_lput, fastmem_wput, fastmem_bput,
	fastmem_xlate, fastmem_check, NULL, _T("Fast memory"),
	fastmem_lget, fastmem_wget, ABFLAG_RAM
};


#ifdef CATWEASEL

/*
* Catweasel ZorroII
*/

static uae_u32 REGPARAM3 catweasel_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 catweasel_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 catweasel_bget (uaecptr) REGPARAM;
static void REGPARAM3 catweasel_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 catweasel_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 catweasel_bput (uaecptr, uae_u32) REGPARAM;
static int REGPARAM3 catweasel_check (uaecptr addr, uae_u32 size) REGPARAM;
static uae_u8 *REGPARAM3 catweasel_xlate (uaecptr addr) REGPARAM;

static uae_u32 catweasel_mask;
static uae_u32 catweasel_start;

static uae_u32 REGPARAM2 catweasel_lget (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	write_log (_T("catweasel_lget @%08X!\n"),addr);
	return 0;
}

static uae_u32 REGPARAM2 catweasel_wget (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	write_log (_T("catweasel_wget @%08X!\n"),addr);
	return 0;
}

static uae_u32 REGPARAM2 catweasel_bget (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr -= catweasel_start & catweasel_mask;
	addr &= catweasel_mask;
	return catweasel_do_bget (addr);
}

static void REGPARAM2 catweasel_lput (uaecptr addr, uae_u32 l)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	write_log (_T("catweasel_lput @%08X=%08X!\n"),addr,l);
}

static void REGPARAM2 catweasel_wput (uaecptr addr, uae_u32 w)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	write_log (_T("catweasel_wput @%08X=%04X!\n"),addr,w);
}

static void REGPARAM2 catweasel_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
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
	catweasel_xlate, catweasel_check, NULL, _T("Catweasel"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO
};

static void expamem_map_catweasel (void)
{
	catweasel_start = ((expamem_hi | (expamem_lo >> 4)) << 16);
	if (catweasel_start) {
		map_banks (&catweasel_bank, catweasel_start >> 16, 1, 0);
		write_log (_T("Catweasel MK%d: mapped @$%lx\n"), cwc.type, catweasel_start);
	}
}

static void expamem_init_catweasel (void)
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
}

#endif

#ifdef FILESYS

/*
* Filesystem device ROM
* This is very simple, the Amiga shouldn't be doing things with it.
*/

static uae_u32 REGPARAM3 filesys_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 filesys_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 filesys_bget (uaecptr) REGPARAM;
static void REGPARAM3 filesys_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 filesys_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 filesys_bput (uaecptr, uae_u32) REGPARAM;

static uae_u32 filesys_start; /* Determined by the OS */
uae_u8 *filesysory;

static uae_u32 REGPARAM2 filesys_lget (uaecptr addr)
{
	uae_u8 *m;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr -= filesys_start & 65535;
	addr &= 65535;
	m = filesysory + addr;
#ifdef EXP_DEBUG
	write_log (_T("filesys_lget %x %x\n"), addr, do_get_mem_long ((uae_u32 *)m));
#endif
	return do_get_mem_long ((uae_u32 *)m);
}

static uae_u32 REGPARAM2 filesys_wget (uaecptr addr)
{
	uae_u8 *m;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr -= filesys_start & 65535;
	addr &= 65535;
	m = filesysory + addr;
#ifdef EXP_DEBUG
	write_log (_T("filesys_wget %x %x\n"), addr, do_get_mem_word ((uae_u16 *)m));
#endif
	return do_get_mem_word ((uae_u16 *)m);
}

static uae_u32 REGPARAM2 filesys_bget (uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr -= filesys_start & 65535;
	addr &= 65535;
#ifdef EXP_DEBUG
	write_log (_T("filesys_bget %x %x\n"), addr, filesysory[addr]);
#endif
	return filesysory[addr];
}

static void REGPARAM2 filesys_lput (uaecptr addr, uae_u32 l)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	write_log (_T("filesys_lput called PC=%p\n"), M68K_GETPC);
}

static void REGPARAM2 filesys_wput (uaecptr addr, uae_u32 w)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	write_log (_T("filesys_wput called PC=%p\n"), M68K_GETPC);
}

static void REGPARAM2 filesys_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
#ifdef EXP_DEBUG
	write_log (_T("filesys_bput %x %x\n"), addr, b);
#endif
}

static addrbank filesys_bank = {
	filesys_lget, filesys_wget, filesys_bget,
	filesys_lput, filesys_wput, filesys_bput,
	default_xlate, default_check, NULL, _T("Filesystem Autoconfig Area"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO | ABFLAG_SAFE
};

#endif /* FILESYS */

/*
*  Z3fastmem Memory
*/

MEMORY_FUNCTIONS(z3fastmem);
MEMORY_FUNCTIONS(z3fastmem2);
MEMORY_FUNCTIONS(z3chipmem);

addrbank z3fastmem_bank = {
	z3fastmem_lget, z3fastmem_wget, z3fastmem_bget,
	z3fastmem_lput, z3fastmem_wput, z3fastmem_bput,
	z3fastmem_xlate, z3fastmem_check, NULL, _T("Zorro III Fast RAM"),
	z3fastmem_lget, z3fastmem_wget, ABFLAG_RAM
};
addrbank z3fastmem2_bank = {
	z3fastmem2_lget, z3fastmem2_wget, z3fastmem2_bget,
	z3fastmem2_lput, z3fastmem2_wput, z3fastmem2_bput,
	z3fastmem2_xlate, z3fastmem2_check, NULL, _T("Zorro III Fast RAM #2"),
	z3fastmem2_lget, z3fastmem2_wget, ABFLAG_RAM
};
addrbank z3chipmem_bank = {
	z3chipmem_lget, z3chipmem_wget, z3chipmem_bget,
	z3chipmem_lput, z3chipmem_wput, z3chipmem_bput,
	z3chipmem_xlate, z3chipmem_check, NULL, _T("MegaChipRAM"),
	z3chipmem_lget, z3chipmem_wget, ABFLAG_RAM
};

/* Z3-based UAEGFX-card */

/* ********************************************************** */

/*
*     Expansion Card (ZORRO II) for 1/2/4/8 MB of Fast Memory
*/

static void expamem_map_fastcard (void)
{
	fastmem_bank.start = ((expamem_hi | (expamem_lo >> 4)) << 16);
	if (fastmem_bank.start) {
		map_banks (&fastmem_bank, fastmem_bank.start >> 16, fastmem_bank.allocated >> 16, 0);
		write_log (_T("Fastcard: mapped @$%lx: %dMB fast memory\n"), fastmem_bank.start, fastmem_bank.allocated >> 20);
	}
}

static void expamem_init_fastcard (void)
{
	uae_u16 mid = (currprefs.a2091 || currprefs.uae_hide) ? commodore : uae_id;
	uae_u8 pid = (currprefs.a2091 || currprefs.uae_hide) ? commodore_a2091_ram : (currprefs.maprom ? 1 : 81);
	uae_u8 type = add_memory | zorroII | (currprefs.a2091 ? chainedconfig : 0);

	expamem_init_clear ();
	if (fastmem_bank.allocated == 0x100000)
		type |= Z2_MEM_1MB;
	else if (fastmem_bank.allocated == 0x200000)
		type |= Z2_MEM_2MB;
	else if (fastmem_bank.allocated == 0x400000)
		type |= Z2_MEM_4MB;
	else if (fastmem_bank.allocated == 0x800000)
		type |= Z2_MEM_8MB;

	expamem_write (0x00, type);

	expamem_write (0x08, care_addr);

	expamem_write (0x04, pid);

	expamem_write (0x10, mid >> 8);
	expamem_write (0x14, mid & 0xff);

	expamem_write (0x18, 0x00); /* ser.no. Byte 0 */
	expamem_write (0x1c, 0x00); /* ser.no. Byte 1 */
	expamem_write (0x20, 0x00); /* ser.no. Byte 2 */
	expamem_write (0x24, 0x01); /* ser.no. Byte 3 */

	expamem_write (0x28, 0x00); /* Rom-Offset hi */
	expamem_write (0x2c, 0x00); /* ROM-Offset lo */

	expamem_write (0x40, 0x00); /* Ctrl/Statusreg.*/
}

/* ********************************************************** */

#ifdef FILESYS

/*
* Filesystem device
*/

static void expamem_map_filesys (void)
{
	uaecptr a;

	filesys_start = ((expamem_hi | (expamem_lo >> 4)) << 16);
	map_banks (&filesys_bank, filesys_start >> 16, 1, 0);
	write_log (_T("Filesystem: mapped memory @$%lx.\n"), filesys_start);
	/* 68k code needs to know this. */
	a = here ();
	org (rtarea_base + RTAREA_FSBOARD);
	dl (filesys_start + 0x2000);
	org (a);
}

#define FILESYS_DIAGPOINT 0x01e0
#define FILESYS_BOOTPOINT 0x01e6
#define FILESYS_DIAGAREA 0x2000

static void expamem_init_filesys (void)
{
	/* struct DiagArea - the size has to be large enough to store several device ROMTags */
	uae_u8 diagarea[] = { 0x90, 0x00, /* da_Config, da_Flags */
		0x02, 0x00, /* da_Size */
		FILESYS_DIAGPOINT >> 8, FILESYS_DIAGPOINT & 0xff,
		FILESYS_BOOTPOINT >> 8, FILESYS_BOOTPOINT & 0xff
	};

	expamem_init_clear ();
	expamem_write (0x00, Z2_MEM_64KB | rom_card | zorroII);

	expamem_write (0x08, no_shutup);

	expamem_write (0x04, currprefs.maprom ? 2 : 82);
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

	memcpy (filesysory, expamem, 0x3000);
}

#endif

/*
* Zorro III expansion memory
*/

static void expamem_map_z3fastmem_2 (addrbank *bank, uaecptr *startp, uae_u32 size, uae_u32 allocated, int chip)
{
	int z3fs = ((expamem_hi | (expamem_lo >> 4)) << 16);
	int start = *startp;

	if (currprefs.jit_direct_compatible_memory) {
		if (z3fs && start != z3fs) {
			write_log (_T("WARNING: Z3MEM mapping changed from $%08x to $%08x\n"), start, z3fs);
			map_banks (&dummy_bank, start >> 16, size >> 16, allocated);
			*startp = z3fs;
			map_banks (bank, start >> 16, size >> 16, allocated);
		}
	} else {
		map_banks (bank, z3fs >> 16, size >> 16, allocated);
		start = z3fs;
		*startp = z3fs;
	}
	write_log (_T("Z3MEM (32bit): mapped @$%08x: %d MB Zorro III %s memory \n"),
			start, allocated / 0x100000, chip ? _T("chip") : _T("fast"));
}

static void expamem_map_z3fastmem (void)
{
	expamem_map_z3fastmem_2 (&z3fastmem_bank, &z3fastmem_bank.start, currprefs.z3fastmem_size, z3fastmem_bank.allocated, 0);
}
static void expamem_map_z3fastmem2 (void)
{
	expamem_map_z3fastmem_2 (&z3fastmem2_bank, &z3fastmem2_bank.start, currprefs.z3fastmem2_size, z3fastmem2_bank.allocated, 0);
}

static void expamem_init_z3fastmem_2 (addrbank *bank, uae_u32 start, uae_u32 size, uae_u32 allocated)
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
	int subsize = (allocated == 0x100000 ? Z3_SS_MEM_1MB
		: allocated == 0x200000 ? Z3_SS_MEM_2MB
		: allocated == 0x400000 ? Z3_SS_MEM_4MB
		: allocated == 0x800000 ? Z3_SS_MEM_8MB
		: Z3_SS_MEM_SAME);

	if (allocated < 0x1000000)
		code = Z3_MEM_16MB; /* Z3 physical board size is always at least 16M */

	expamem_init_clear ();
	expamem_write (0x00, add_memory | zorroIII | code);

	expamem_write (0x08, care_addr | force_z3 | (allocated > 0x800000 ? ext_size : subsize));

	expamem_write (0x04, currprefs.maprom ? 3 : 83);

	expamem_write (0x10, uae_id >> 8);
	expamem_write (0x14, uae_id & 0xff);

	expamem_write (0x18, 0x00); /* ser.no. Byte 0 */
	expamem_write (0x1c, 0x00); /* ser.no. Byte 1 */
	expamem_write (0x20, 0x00); /* ser.no. Byte 2 */
	expamem_write (0x24, 0x01); /* ser.no. Byte 3 */

	expamem_write (0x28, 0x00); /* Rom-Offset hi */
	expamem_write (0x2c, 0x00); /* ROM-Offset lo */

	expamem_write (0x40, 0x00); /* Ctrl/Statusreg.*/

	map_banks (bank, start >> 16, size >> 16, allocated);

}
static void expamem_init_z3fastmem (void)
{
	expamem_init_z3fastmem_2 (&z3fastmem_bank, z3fastmem_bank.start, currprefs.z3fastmem_size, z3fastmem_bank.allocated);
}
static void expamem_init_z3fastmem2 (void)
{
	expamem_init_z3fastmem_2 (&z3fastmem2_bank, z3fastmem2_bank.start, currprefs.z3fastmem2_size, z3fastmem2_bank.allocated);
}

#ifdef PICASSO96
/*
*  Fake Graphics Card (ZORRO III) - BDK
*/

static void expamem_map_gfxcard (void)
{
	gfxmem_bank.start = (expamem_hi | (expamem_lo >> 4)) << 16;
	if (gfxmem_bank.start) {
		map_banks (&gfxmem_bank, gfxmem_bank.start >> 16, gfxmem_bank.allocated >> 16, gfxmem_bank.allocated);
		write_log (_T("%sUAEGFX-card: mapped @$%lx, %d MB RTG RAM\n"), currprefs.rtgmem_type ? _T("Z3") : _T("Z2"), gfxmem_bank.baseaddr, gfxmem_bank.allocated / 0x100000);
	}
}

static void expamem_init_gfxcard (bool z3)
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
}
static void expamem_init_gfxcard_z3 (void)
{
	expamem_init_gfxcard (true);
}
static void expamem_init_gfxcard_z2 (void)
{
	expamem_init_gfxcard (false);
}
#endif


#ifdef SAVESTATE
static size_t fast_filepos, z3_filepos, z3_filepos2, z3_fileposchip, p96_filepos;
#endif

void free_fastmemory (void)
{
	if (fastmem_bank.baseaddr)
		mapped_free (fastmem_bank.baseaddr);
	fastmem_bank.baseaddr = 0;
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
		uae_u8 *mem = mapped_malloc (alloc, name);
		if (mem) {
			bank->baseaddr = mem;
			*currpsize = alloc;
			*changedpsize = alloc;
			bank->mask = alloc - 1;
			bank->allocated = alloc;
			return true;
		}
		write_log (_T("Out of memory for %s, %d bytes.\n"), name, alloc);
		alloc /= 2;
	}

	return false;
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

	z3chipmem_bank.start = currprefs.z3fastmem_start;
	if (currprefs.mbresmem_high_size == 128 * 1024 * 1024)
		z3chipmem_bank.start += 16 * 1024 * 1024;
	if (currprefs.jit_direct_compatible_memory)
		z3fastmem_bank.start = z3chipmem_bank.start;
	else
		z3fastmem_bank.start = 0x40000000;
	if (currprefs.z3chipmem_size)
		z3fastmem_bank.start += currprefs.z3chipmem_size;
	z3fastmem2_bank.start = z3fastmem_bank.start + currprefs.z3fastmem_size;

	if (fastmem_bank.allocated != currprefs.fastmem_size) {
		free_fastmemory ();
		fastmem_bank.allocated = currprefs.fastmem_size;
		fastmem_bank.mask = fastmem_bank.allocated - 1;

		if (fastmem_bank.allocated) {
			fastmem_bank.baseaddr = mapped_malloc (fastmem_bank.allocated, _T("fast"));
			if (fastmem_bank.baseaddr == 0) {
				write_log (_T("Out of memory for fastmem card.\n"));
				fastmem_bank.allocated = 0;
			}
		}
		memory_hardreset (1);
	}
	if (z3fastmem_bank.allocated != currprefs.z3fastmem_size) {
		if (z3fastmem_bank.baseaddr)
			mapped_free (z3fastmem_bank.baseaddr);
		mapped_malloc_dynamic (&currprefs.z3fastmem_size, &changed_prefs.z3fastmem_size, &z3fastmem_bank, 1, _T("z3"));
		memory_hardreset (1);
	}
	if (z3fastmem2_bank.allocated != currprefs.z3fastmem2_size) {
		if (z3fastmem2_bank.baseaddr)
			mapped_free (z3fastmem2_bank.baseaddr);
		z3fastmem2_bank.baseaddr = 0;

		z3fastmem2_bank.allocated = currprefs.z3fastmem2_size;
		z3fastmem2_bank.mask = z3fastmem2_bank.allocated - 1;

		if (z3fastmem2_bank.allocated) {
			z3fastmem2_bank.baseaddr = mapped_malloc (z3fastmem2_bank.allocated, _T("z3_2"));
			if (z3fastmem2_bank.baseaddr == 0) {
				write_log (_T("Out of memory for 32 bit fast memory #2.\n"));
				z3fastmem2_bank.allocated = 0;
			}
		}
		memory_hardreset (1);
	}
	if (z3chipmem_bank.allocated != currprefs.z3chipmem_size) {
		if (z3chipmem_bank.baseaddr)
			mapped_free (z3chipmem_bank.baseaddr);
		mapped_malloc_dynamic (&currprefs.z3chipmem_size, &changed_prefs.z3chipmem_size, &z3chipmem_bank, 16, _T("z3_chip"));
		memory_hardreset (1);
	}

#ifdef PICASSO96
	if (gfxmem_bank.allocated != currprefs.rtgmem_size) {
		if (gfxmem_bank.baseaddr)
			mapped_free (gfxmem_bank.baseaddr);
		mapped_malloc_dynamic (&currprefs.rtgmem_size, &changed_prefs.rtgmem_size, &gfxmem_bank, 1, currprefs.rtgmem_type ? _T("z3_gfx") : _T("z2_gfx"));
		memory_hardreset (1);
	}
#endif

#ifdef SAVESTATE
	if (savestate_state == STATE_RESTORE) {
		if (fastmem_bank.allocated > 0) {
			restore_ram (fast_filepos, fastmem_bank.baseaddr);
			map_banks (&fastmem_bank, fastmem_bank.start >> 16, currprefs.fastmem_size >> 16,
				fastmem_bank.allocated);
		}
		if (z3fastmem_bank.allocated > 0) {
			restore_ram (z3_filepos, z3fastmem_bank.baseaddr);
			map_banks (&z3fastmem_bank, z3fastmem_bank.start >> 16, currprefs.z3fastmem_size >> 16,
				z3fastmem_bank.allocated);
		}
		if (z3fastmem2_bank.allocated > 0) {
			restore_ram (z3_filepos2, z3fastmem2_bank.baseaddr);
			map_banks (&z3fastmem2_bank, z3fastmem2_bank.start >> 16, currprefs.z3fastmem2_size >> 16,
				z3fastmem2_bank.allocated);
		}
		if (z3chipmem_bank.allocated > 0) {
			restore_ram (z3_fileposchip, z3chipmem_bank.baseaddr);
			map_banks (&z3chipmem_bank, z3chipmem_bank.start >> 16, currprefs.z3chipmem_size >> 16,
				z3chipmem_bank.allocated);
		}
#ifdef PICASSO96
		if (gfxmem_bank.allocated > 0 && gfxmem_bank.start > 0) {
			restore_ram (p96_filepos, gfxmem_bank.baseaddr);
			map_banks (&gfxmem_bank, gfxmem_bank.start >> 16, currprefs.rtgmem_size >> 16,
				gfxmem_bank.allocated);
		}
#endif
	}
#endif /* SAVESTATE */
}

static uaecptr check_boot_rom (void)
{
	uaecptr b = RTAREA_DEFAULT;
	addrbank *ab;

	if (currprefs.cs_cdtvcd || currprefs.cs_cdtvscsi || currprefs.uae_hide > 1)
		b = RTAREA_BACKUP;
	if (currprefs.cs_mbdmac == 1)
		b = RTAREA_BACKUP;
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
	if (currprefs.rtgmem_size)
		return b;
	if (currprefs.win32_automount_removable)
		return b;
	if (currprefs.chipmem_size > 2 * 1024 * 1024)
		return b;
	if (currprefs.z3chipmem_size)
		return b;
	return 0;
}

uaecptr need_uae_boot_rom (void)
{
	uaecptr v;

	uae_boot_rom = 0;
	v = check_boot_rom ();
	if (v)
		uae_boot_rom = 1;
	if (!rtarea_base) {
		uae_boot_rom = 0;
		v = 0;
	}
	return v;
}

static void expamem_init_a2065 (void)
{
#ifdef A2065
	a2065_init ();
#endif
}
static void expamem_init_cdtv (void)
{
#ifdef CDTV
	cdtv_init ();
#endif
}
static void expamem_init_a2091 (void)
{
#ifdef A2091
	a2091_init ();
#endif
}
static void expamem_init_a4091 (void)
{
#ifdef NCR
	ncr_autoconfig_init ();
#endif
}
static void expamem_init_gfxboard_memory (void)
{
#ifdef GFXBOARD
	gfxboard_init_memory ();
#endif
}
static void expamem_init_gfxboard_registers (void)
{
#ifdef GFXBOARD
	gfxboard_init_registers ();
#endif
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

	/* check if Kickstart version is below 1.3 */
	if (kickstart_version && do_mount
		&& (/* Kickstart 1.0 & 1.1! */
		kickstart_version == 0xFFFF
		/* Kickstart < 1.3 */
		|| kickstart_version < 34))
	{
		/* warn user */
		write_log (_T("Kickstart version is below 1.3!  Disabling automount devices.\n"));
		do_mount = 0;
	}
	if (need_uae_boot_rom () == 0)
		do_mount = 0;

	if (fastmem_bank.baseaddr != NULL && currprefs.chipmem_size <= 2 * 1024 * 1024) {
		if (currprefs.fastmem_autoconfig) {
			fastmem_bank.name = _T("Fast memory");
			card_name[cardno] = _T("Z2Fast");
			card_init[cardno] = expamem_init_fastcard;
			card_map[cardno++] = expamem_map_fastcard;
		} else {
			fastmem_bank.name = _T("Fast memory (non-autoconfig)");
			map_banks (&fastmem_bank, 0x00200000 >> 16, fastmem_bank.allocated >> 16, 0);
		}
	}
	// immediately after Z2Fast so that they can be emulated as A590/A2091 with fast ram.
#ifdef A2091
	if (currprefs.a2091) {
		card_name[cardno] = _T("A2091");
		card_init[cardno] = expamem_init_a2091;
		card_map[cardno++] = NULL;
	}
#endif
#ifdef CDTV
	if (currprefs.cs_cdtvcd) {
		card_name[cardno] = _T("CDTV DMAC");
		card_init[cardno] = expamem_init_cdtv;
		card_map[cardno++] = NULL;
	}
#endif
#ifdef CD32
	if (currprefs.cs_cd32cd && currprefs.fastmem_size == 0 && currprefs.chipmem_size <= 0x200000) {
		int ids[] = { 23, -1 };
		struct romlist *rl = getromlistbyids (ids);
		if (rl && !_tcscmp (rl->path, currprefs.cartfile)) {
			card_name[cardno] = _T("CD32MPEG");
			card_init[cardno] = expamem_init_cd32fmv;
			card_map[cardno++] = expamem_map_cd32fmv;
		}
	}
#endif
#ifdef A2065
	if (currprefs.a2065name[0]) {
		card_name[cardno] = _T("A2065");
		card_init[cardno] = expamem_init_a2065;
		card_map[cardno++] = NULL;
	}
#endif
#ifdef FILESYS
	if (do_mount) {
		card_name[cardno] = _T("UAEFS");
		card_init[cardno] = expamem_init_filesys;
		card_map[cardno++] = expamem_map_filesys;
	}
#endif
#ifdef CATWEASEL
	if (currprefs.catweasel && catweasel_init ()) {
		card_name[cardno] = _T("CWMK2");
		card_init[cardno] = expamem_init_catweasel;
		card_map[cardno++] = expamem_map_catweasel;
	}
#endif
#ifdef PICASSO96
	if (currprefs.rtgmem_type == GFXBOARD_UAE_Z2 && gfxmem_bank.baseaddr != NULL) {
		card_name[cardno] = _T("Z2RTG");
		card_init[cardno] = expamem_init_gfxcard_z2;
		card_map[cardno++] = expamem_map_gfxcard;
	}
#endif
#ifdef GFXBOARD
	if (currprefs.rtgmem_type >= GFXBOARD_HARDWARE && !gfxboard_is_z3 (currprefs.rtgmem_type)) {
		card_name[cardno] = _T("Gfxboard VRAM Zorro II");
		card_init[cardno] = expamem_init_gfxboard_memory;
		card_map[cardno++] = NULL;
		if (gfxboard_num_boards (currprefs.rtgmem_type) == 3) {
			card_name[cardno] = _T("Gfxboard VRAM Zorro II Extra");
			card_init[cardno] = gfxboard_init_memory_p4_z2;
			card_map[cardno++] = NULL;
		}
		if (gfxboard_is_registers (currprefs.rtgmem_type)) {
			card_name[cardno] = _T ("Gfxboard Registers");
			card_init[cardno] = expamem_init_gfxboard_registers;
			card_map[cardno++] = NULL;
		}
	}
#endif
	/* Z3 boards last */

	if (z3fastmem_bank.baseaddr != NULL) {
		z3num = 0;
		card_name[cardno] = _T("Z3Fast");
		card_init[cardno] = expamem_init_z3fastmem;
		card_map[cardno++] = expamem_map_z3fastmem;
		if (currprefs.jit_direct_compatible_memory)
			map_banks (&z3fastmem_bank, z3fastmem_bank.start >> 16, currprefs.z3fastmem_size >> 16, z3fastmem_bank.allocated);
		if (z3fastmem2_bank.baseaddr != NULL) {
			card_name[cardno] = _T("Z3Fast2");
			card_init[cardno] = expamem_init_z3fastmem2;
			card_map[cardno++] = expamem_map_z3fastmem2;
			if (currprefs.jit_direct_compatible_memory)
				map_banks (&z3fastmem2_bank, z3fastmem2_bank.start >> 16, currprefs.z3fastmem2_size >> 16, z3fastmem2_bank.allocated);
		}
	}
	if (z3chipmem_bank.baseaddr != NULL)
		map_banks (&z3chipmem_bank, z3chipmem_bank.start >> 16, currprefs.z3chipmem_size >> 16, z3chipmem_bank.allocated);
#ifdef NCR
	if (currprefs.a4091) {
		card_name[cardno] = _T("A4091");
		card_init[cardno] = expamem_init_a4091;
		card_map[cardno++] = NULL;
	}
#endif
#ifdef PICASSO96
	if (currprefs.rtgmem_type == GFXBOARD_UAE_Z3 && gfxmem_bank.baseaddr != NULL) {
		card_name[cardno] = _T("Z3RTG");
		card_init[cardno] = expamem_init_gfxcard_z3;
		card_map[cardno++] = expamem_map_gfxcard;
	}
#endif
#ifdef GFXBOARD
	if (currprefs.rtgmem_type >= GFXBOARD_HARDWARE && gfxboard_is_z3 (currprefs.rtgmem_type)) {
		card_name[cardno] = _T ("Gfxboard VRAM Zorro III");
		card_init[cardno] = expamem_init_gfxboard_memory;
		card_map[cardno++] = NULL;
		card_name[cardno] = _T ("Gfxboard Registers");
		card_init[cardno] = expamem_init_gfxboard_registers;
		card_map[cardno++] = NULL;
	}
#endif
	if (cardno > 0 && cardno < MAX_EXPANSION_BOARDS) {
		card_name[cardno] = _T("Empty");
		card_init[cardno] = expamem_init_last;
		card_map[cardno++] = expamem_map_clear;
	}

	if (cardno == 0 || savestate_state)
		expamem_init_clear_zero ();
	else
		(*card_init[0]) ();
}

void expansion_init (void)
{
	if (savestate_state != STATE_RESTORE) {

		fastmem_bank.allocated = 0;
		fastmem_bank.mask = fastmem_bank.start = 0;
		fastmem_bank.baseaddr = NULL;

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
	filesysory = 0;
#endif

	allocate_expamem ();

#ifdef FILESYS
	filesysory = mapped_malloc (0x10000, _T("filesys"));
	if (!filesysory) {
		write_log (_T("virtual memory exhausted (filesysory)!\n"));
		exit (0);
	}
	filesys_bank.baseaddr = filesysory;
#endif
}

void expansion_cleanup (void)
{
	mapped_free (fastmem_bank.baseaddr);
	fastmem_bank.baseaddr = NULL;
	mapped_free (z3fastmem_bank.baseaddr);
	z3fastmem_bank.baseaddr = NULL;
	mapped_free (z3fastmem2_bank.baseaddr);
	z3fastmem2_bank.baseaddr = NULL;
	mapped_free (z3chipmem_bank.baseaddr);
	z3chipmem_bank.baseaddr = NULL;

#ifdef PICASSO96
	mapped_free (gfxmem_bank.baseaddr);
	gfxmem_bank.baseaddr = NULL;
#endif

#ifdef FILESYS
	mapped_free (filesysory);
	filesysory = NULL;
#endif

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
	clear_bank (&z3fastmem_bank);
	clear_bank (&z3fastmem2_bank);
	clear_bank (&z3chipmem_bank);
	clear_bank (&gfxmem_bank);
}

#ifdef SAVESTATE

/* State save/restore code.  */

uae_u8 *save_fram (int *len)
{
	*len = fastmem_bank.allocated;
	return fastmem_bank.baseaddr;
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

void restore_fram (int len, size_t filepos)
{
	fast_filepos = filepos;
	changed_prefs.fastmem_size = len;
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
	static uae_u8 t[20];
	uae_u8 *dst = t, *dstbak = t;
	if (dstptr)
		dst = dstbak = dstptr;
	save_u32 (fastmem_bank.start);
	save_u32 (z3fastmem_bank.start);
	save_u32 (gfxmem_bank.start);
	save_u32 (rtarea_base);
	*len = 4 + 4 + 4 + 4;
	return dstbak;
}

uae_u8 *restore_expansion (uae_u8 *src)
{
	fastmem_bank.start = restore_u32 ();
	z3fastmem_bank.start = restore_u32 ();
	gfxmem_bank.start = restore_u32 ();
	rtarea_base = restore_u32 ();
	if (rtarea_base != 0 && rtarea_base != RTAREA_DEFAULT && rtarea_base != RTAREA_BACKUP)
		rtarea_base = 0;
	return src;
}

#endif /* SAVESTATE */
