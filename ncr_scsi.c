 /*
  * UAE - The Un*x Amiga Emulator
  *
  * A4000T NCR 53C710 SCSI (nothing done yet)
  *
  * (c) 2007 Toni Wilen
  */

#define NCR_LOG 0

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "ncr_scsi.h"
#include "zfile.h"

#define NCRNAME "NCR53C710"
#define NCR_REGS 0x40

#define ROM_VECTOR 0x8080
#define ROM_OFFSET 0x8000
#define ROM_SIZE 32768
#define ROM_MASK (ROM_SIZE - 1)
#define BOARD_SIZE (65536 * 2)

static uae_u8 *rom;
static int configured;
static uae_u8 acmemory[100];

static uae_u8 ncrregs[NCR_REGS];

struct ncrscsi {
    char *name;
    int be, le;
};

static struct ncrscsi regsinfo[] =
{
    "SCNTL0",	 0,  3,
    "SCNTL1",	 1,  2,
    "SDID",	 2,  1,
    "SIEN",	 3,  0,
    "SCID",	 4,  7,
    "SXFER",	 5,  6,
    "SODL",	 6,  5,
    "SOCL",	 7,  4,
    "SFBR",	 8, 11,
    "SIDL",	 9, 10,
    "SBDL",	10, -1,
    "SBCL",	11,  8,
    "DSTAT",	12, 15,
    "SSTAT0",	13, 14,
    "SSTAT1",	14, 13,
    "SSTAT2",	15, 12,
    "DSA0",	16, 19,
    "DSA1",	17, 18,
    "DSA2",	18, 17,
    "DSA3",	19, 16,
    "CTEST0",	20, 23,
    "CTEST1",	21, 22,
    "CTEST2",	22, 21,
    "CTEST3",	23, 20,
    "CTEST4",	24, 27,
    "CTEST5",	25, 26,
    "CTEST6",	26, 25,
    "CTEST7",	27, 24,
    "TEMP0",	28, 31,
    "TEMP1",	29, 30,
    "TEMP2",	30, 29,
    "TEMP3",	31, 28,
    "DFIFO",	32, 35,
    "ISTAT",	33, 34,
    "CTEST8",	34, 33,
    "LCRC",	35, 32,
    "DBC0",	36, 39,
    "DBC1",	37, 38,
    "DBC2",	38, 37,
    "DCMD",	39, 36,
    "DNAD0",	40, 43,
    "DNAD1",	41, 42,
    "DNAD2",	42, 41,
    "DNAD3",	43, 40,
    "DSP0",	44, 47,
    "DSP1",	45, 46,
    "DSP2",	46, 45,
    "DSP3",	47, 44,
    "DSPS0",	48, 51,
    "DSPS1",	49, 50,
    "DSPS2",	50, 49,
    "DSPS3",	51, 48,
    "SCRATCH0",	52, 55,
    "SCRATCH1",	53, 54,
    "SCRATCH2",	54, 53,
    "SCRATCH3",	55, 52,
    "DMODE",	56, 59,
    "DIEN",	57, 58,
    "DWT",	58, 57,
    "DCNTL",	59, 56,
    "ADDER0",	60, 63,
    "ADDER1",	61, 62,
    "ADDER2",	62, 61,
    "ADDER3",	63, 60,
    NULL
};

static char *regname(uaecptr addr)
{
    int i;

    for (i = 0; regsinfo[i].name; i++) {
	if (regsinfo[i].le == addr)
	    return regsinfo[i].name;
    }
    return "?";
}

static uae_u8 read_rom(uaecptr addr)
{
    int off;
    uae_u8 v;
    
    addr -= 0x8080;
    off = (addr & (BOARD_SIZE - 1)) / 4;
    off += 0x80;

    if ((addr & 2))
	v = (rom[off] & 0x0f) << 4;
    else
	v = (rom[off] & 0xf0);
    write_log("%08.8X:%04.4X = %02.2X PC=%08X\n", addr, off, v, M68K_GETPC);
    return v;
}

void ncr_bput2(uaecptr addr, uae_u32 val)
{
    addr &= 0xffff;
    if (addr >= NCR_REGS)
	return;
    switch (addr)
    {
	case 0x08:
	ncrregs[0x22] |= 2;
	break;
	case 0x02: // SCNTL1
	break;
	case 0x22 : // ISTAT
	break;
    }
    write_log("%s write %04.4X (%s) = %02.2X PC=%08.8X\n", NCRNAME, addr, regname(addr), val & 0xff, M68K_GETPC);
    ncrregs[addr] = val;
}

uae_u32 ncr_bget2(uaecptr addr)
{
    uae_u32 v;

    addr &= 0xffff;
    if (rom && addr >= ROM_OFFSET)
	return read_rom(addr);
    if (addr >= NCR_REGS)
	return 0;
    v = ncrregs[addr];
    switch (addr)
    {
	case 0x0c: // SSTAT2
	v &= ~7;
	v |= ncrregs[8] & 7;
	break;
	case 0x0e: // SSTAT0
	v |= 0x20;
	break;
        case 0x22: // ISTAT
	ncrregs[addr] &= 0x40;
	break;
	case 0x21: // CTEST8
	v &= 0x0f;
	v |= 0x20;
	break;
    }

    write_log("%s read  %04.4X (%s) = %02.2X PC=%08.8X\n", NCRNAME, addr, regname(addr), v, M68K_GETPC);
    return v;
}

static addrbank ncr_bank;

static uae_u32 REGPARAM2 ncr_lget (uaecptr addr)
{
    uae_u32 v;
#ifdef JIT
    special_mem |= S_READ;
#endif
    addr &= 65535;
    v = (ncr_bget2 (addr) << 24) | (ncr_bget2 (addr + 1) << 16) |
	(ncr_bget2 (addr + 2) << 8) | (ncr_bget2 (addr + 3));
#ifdef NCR_DEBUG
    if (addr >= 0x40 && addr < ROM_OFFSET)
	write_log ("ncr_lget %08.8X=%08.8X PC=%08.8X\n", addr, v, M68K_GETPC);
#endif
    return v;
}

static uae_u32 REGPARAM2 ncr_wget (uaecptr addr)
{
    uae_u32 v;
#ifdef JIT
    special_mem |= S_READ;
#endif
    addr &= 65535;
    v = (ncr_bget2 (addr) << 8) | ncr_bget2 (addr + 1);
#if NCR_DEBUG > 0
    if (addr >= 0x40 && addr < ROM_OFFSET)
	write_log ("ncr_wget %08.8X=%04.4X PC=%08.8X\n", addr, v, M68K_GETPC);
#endif
    return v;
}

static uae_u32 REGPARAM2 ncr_bget (uaecptr addr)
{
    uae_u32 v;
#ifdef JIT
    special_mem |= S_READ;
#endif
    addr &= 65535;
    if (!configured) {
	if (addr >= sizeof acmemory)
	    return 0;
	return acmemory[addr];
    }
    v = ncr_bget2 (addr);
    return v;
}

static void REGPARAM2 ncr_lput (uaecptr addr, uae_u32 l)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
    addr &= 65535;
#if NCR_DEBUG > 0
    if (addr >= 0x40 && addr < ROM_OFFSET)
	write_log ("ncr_lput %08.8X=%08.8X PC=%08.8X\n", addr, l, M68K_GETPC);
#endif
    ncr_bput2 (addr, l >> 24);
    ncr_bput2 (addr + 1, l >> 16);
    ncr_bput2 (addr + 2, l >> 8);
    ncr_bput2 (addr + 3, l);
}

static void REGPARAM2 ncr_wput (uaecptr addr, uae_u32 w)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
    addr &= 65535;
#if NCR_DEBUG > 0
    if (addr >= 0x40 && addr < ROM_OFFSET)
	write_log ("ncr_wput %04.4X=%04.4X PC=%08.8X\n", addr, w & 65535, M68K_GETPC);
#endif
    ncr_bput2 (addr, w >> 8);
    ncr_bput2 (addr + 1, w);
}

static void REGPARAM2 ncr_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
    special_mem |= S_WRITE;
#endif
    b &= 0xff;
    addr &= 65535;
    if (addr == 0x48) {
	map_banks (&ncr_bank, b, BOARD_SIZE >> 16, BOARD_SIZE);
	write_log ("A4091 autoconfigured at %02.2X0000\n", b);
	configured = 1;
	expamem_next();
	return;
    }
    if (addr == 0x4c) {
	write_log ("A4091 AUTOCONFIG SHUT-UP!\n");
	configured = 1;
	expamem_next();
	return;
    }
    if (!configured)
	return;
    ncr_bput2 (addr, b);
}

static uae_u32 REGPARAM2 ncr_wgeti (uaecptr addr)
{
    uae_u32 v = 0xffff;
#ifdef JIT
    special_mem |= S_READ;
#endif
    addr &= 65535;
    if (addr >= ROM_OFFSET)
	v = (rom[addr & ROM_MASK] << 8) | rom[(addr + 1) & ROM_MASK];
    return v;
}
static uae_u32 REGPARAM2 ncr_lgeti (uaecptr addr)
{
    uae_u32 v = 0xffff;
#ifdef JIT
    special_mem |= S_READ;
#endif
    addr &= 65535;
    v = (ncr_wgeti(addr) << 16) | ncr_wgeti(addr + 2);
    return v;
}

static addrbank ncr_bank = {
    ncr_lget, ncr_wget, ncr_bget,
    ncr_lput, ncr_wput, ncr_bput,
    default_xlate, default_check, NULL, "A4091",
    ncr_lgeti, ncr_wgeti, ABFLAG_IO
};

static void ew (int addr, uae_u32 value)
{
    addr &= 0xffff;
    if (addr == 00 || addr == 02 || addr == 0x40 || addr == 0x42) {
	acmemory[addr] = (value & 0xf0);
	acmemory[addr + 2] = (value & 0x0f) << 4;
    } else {
	acmemory[addr] = ~(value & 0xf0);
	acmemory[addr + 2] = ~((value & 0x0f) << 4);
    }
}

void ncr_free (void)
{
}

void ncr_reset (void)
{
    configured = 0; 
}

void ncr_init (void)
{
    struct zfile *z;
    int roms[3];
    struct romlist *rl;

    configured = 0;
    memset (acmemory, 0xff, 100);
    ew (0x00, 0xc0 | 0x02 | 0x10);
    /* A4091 hardware id */
    ew (0x04, 0x54);
    /* commodore's manufacturer id */
    ew (0x10, 0x02);
    ew (0x14, 0x02);
    /* rom vector */
    ew (0x28, ROM_VECTOR >> 8);
    ew (0x2c, ROM_VECTOR);

    roms[0] = 57;
    roms[1] = 56;
    roms[2] = -1;

    rl = getrombyids(roms);
    if (rl) {
	write_log("A4091 BOOT ROM '%s' %d.%d ", rl->path, rl->rd->ver, rl->rd->rev);
	z = zfile_fopen(rl->path, "rb");
	if (z) {
	    write_log("loaded\n");
	    rom = xmalloc (ROM_SIZE);
	    zfile_fread (rom, ROM_SIZE, 1, z);
	    zfile_fclose(z);
	} else {
	    write_log("failed to load\n");
	}
    }
    map_banks (&ncr_bank, 0xe80000 >> 16, 0x10000 >> 16, 0x10000);
}

