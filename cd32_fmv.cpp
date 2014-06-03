/*
* UAE - The Un*x Amiga Emulator
*
* CD32 FMV cartridge
*
* Copyright 2008-2010 Toni Wilen
*
*/

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "rommgr.h"
#include "custom.h"
#include "newcpu.h"
#include "zfile.h"
#include "cd32_fmv.h"
#include "uae.h"

#define FMV_DEBUG 1

/*
 0x200000 - 0x23FFFF ROM
 0x240000 io/status (single word register?)
 0x2500xx L64111 audio decoder (word registers)
 0x2700xx CL450 video decoder (word registers)
 0x280000 - 0x2FFFFF RAM
*/

#define IO_BASE			0x040000
#define L64111_BASE		0x050000
#define CL450_BASE		0x070000
#define VRAM_BASE		0x080000

#define BANK_MASK		0x0F0000

#define IO_IRQ_L641111	0x4000
#define IO_IRQ_CL450	0x8000

// L64111 registers (from datasheet)
#define A_DATA			 0	//0
#define A_CONTROL1		 1	//2
#define A_CONTROL2		 2	//4
#define A_CONTROL3		 3	//6
#define A_INT1			 4	//8
#define A_INT2			 5	//10
#define A_TCR			 6	//12
#define A_TORH			 7	//14
#define A_TORL			 8	//16
#define A_PARAM1		 9	//18
#define A_PARAM2		10	//20
#define A_PARAM3		11	//22
#define A_PRESENT1		12	//24
#define A_PRESENT2		13	//26
#define A_PRESENT3		14	//28
#define A_PRESENT4		15	//30
#define A_PRESENT5		16	//32
#define A_FIFO			17	//34
#define A_CB_STATUS		18	//36
#define A_CB_WRITE		19	//38
#define A_CB_READ		20	//40

static int fmv_mask;
static uae_u8 *rom;
static int fmv_rom_size = 262144;
static uaecptr fmv_start = 0x00200000;
static int fmv_size = 1048576;

static uae_u16 l64111regs[32];
static uae_u16 l64111intmask1, l64111intmask2, l64111intstatus1, l64111intstatus2;
static uae_u16 io_reg;

static int isdebug (uaecptr addr)
{
#if FMV_DEBUG > 2
	if (M68K_GETPC >= 0x200100)
		return 1;
	return 0;
#endif
#if (FMV_DEBUG == 2)
	if (M68K_GETPC >= 0x200100 && (addr & fmv_mask) >= VRAM_BASE)
		return 1;
	return 0;
#endif
	return 0;
}

static uae_u8 io_bget (uaecptr addr)
{
	addr &= 0xffff;
	write_log (_T("FMV: IO byte read access %08x!\n"), addr);
	return 0;
}
static uae_u16 io_wget (uaecptr addr)
{
	addr &= 0xffff;
	if (addr != 0)
		return 0;
	return io_reg;
}
static void io_bput (uaecptr addr, uae_u8 v)
{
	addr &= 0xffff;
	write_log (_T("FMV: IO byte write access %08x!\n"), addr);
}
static void io_wput (uaecptr addr, uae_u16 v)
{
	addr &= 0xffff;
	if (addr != 0)
		return;
	write_log (_T("FMV: IO=%04x\n"), v);
	io_reg = v;
}

static uae_u8 l64111_bget (uaecptr addr)
{
	write_log (_T("FMV: L64111 byte read access %08x!\n"), addr);
	return 0;
}
static void l64111_bput (uaecptr addr, uae_u8 v)
{
	write_log (_T("FMV: L64111 byte write access %08x!\n"), addr);
}

static uae_u16 l64111_wget (uaecptr addr)
{
	addr >>= 1;
	addr &= 31;
#if FMV_DEBUG > 0
	write_log (_T("FMV: L64111 read reg %d -> %04x\n"), addr, l64111regs[addr]);
#endif
	if (addr == 4)
		return l64111intstatus1;
	if (addr == 5)
		return l64111intstatus1;

	return l64111regs[addr];
}
static void l64111_wput (uaecptr addr, uae_u16 v)
{
	addr >>= 1;
	addr &= 31;

#if FMV_DEBUG > 0
	write_log (_T("FMV: L64111 write reg %d = %04x\n"), addr, v);
#endif

	if (addr == 4) {
		l64111intmask1 = v;
		return;
	}
	if (addr == 5) {
		l64111intmask2 = v;
		return;
	}

	l64111regs[addr] = v;

}

static uae_u8 cl450_bget (uaecptr addr)
{
	addr &= 0xff;
	write_log (_T("FMV: CL450 byte read access %08x!\n"), addr);
	return 0;
}
static uae_u16 cl450_wget (uaecptr addr)
{
	addr &= 0xff;
	addr >>= 1;
	write_log (_T("FMV: CL450 read reg %d\n"), addr);
	return 0;
}
static void cl450_bput (uaecptr addr, uae_u8 v)
{
	addr &= 0xff;
	write_log (_T("FMV: CL450 byte write access %08x!\n"), addr);
}
static void cl450_wput (uaecptr addr, uae_u16 v)
{
	addr &= 0xff;
	write_log (_T("FMV: CL450 write reg %d = %04x\n"), addr, v);
}

static uae_u8 romram_bget (uaecptr addr)
{
#ifdef FMV_DEBUG
	if (isdebug (addr))
		write_log (_T("romram_bget %08X PC=%08X\n"), addr, M68K_GETPC);
#endif
	if (addr >= IO_BASE && addr < VRAM_BASE)
		return 0;
	return rom[addr];
}
static uae_u16 romram_wget (uaecptr addr)
{
#ifdef FMV_DEBUG
	if (isdebug (addr))
		write_log (_T("romram_wget %08X PC=%08X\n"), addr, M68K_GETPC);
#endif
	if (addr >= IO_BASE && addr < VRAM_BASE)
		return 0;
	return (rom[addr] << 8) | (rom[addr + 1] << 0);
}
static void ram_bput (uaecptr addr, uae_u8 v)
{
	if (addr < VRAM_BASE)
		return;
	rom[addr] = v;
	if (isdebug (addr)) {
		write_log (_T("ram_bput %08X=%02X PC=%08X\n"), addr, v & 0xff, M68K_GETPC);
	}
}
static void ram_wput (uaecptr addr, uae_u16 v)
{
	if (addr < VRAM_BASE)
		return;
	rom[addr + 0] = v >> 8;
	rom[addr + 1] = v >> 0;
	if (isdebug (addr)) {
		write_log (_T("ram_wput %08X=%04X PC=%08X\n"), addr, v & 0xffff, M68K_GETPC);
	}
}

static uae_u32 REGPARAM2 fmv_wget (uaecptr addr)
{
	uae_u32 v;
	addr -= fmv_start & fmv_mask;
	addr &= fmv_mask;
	int mask = addr & BANK_MASK;
	if (mask == L64111_BASE)
		v = l64111_wget (addr);
	else if (mask == CL450_BASE)
		v = cl450_wget (addr);
	else if (mask == IO_BASE)
		v = io_wget (addr);
	else
		v = romram_wget (addr);

#ifdef FMV_DEBUG
	if (isdebug (addr))
		write_log (_T("fmv_wget %08X=%04X PC=%08X\n"), addr, v, M68K_GETPC);
#endif
	return v;
}

static uae_u32 REGPARAM2 fmv_lget (uaecptr addr)
{
	uae_u32 v;
	v = (fmv_wget (addr) << 16) | (fmv_wget (addr + 2) << 0);
#ifdef FMV_DEBUG
	if (isdebug (addr))
		write_log (_T("fmv_lget %08X=%08X PC=%08X\n"), addr, v, M68K_GETPC);
#endif
	return v;
}

static uae_u32 REGPARAM2 fmv_bget (uaecptr addr)
{
	uae_u32 v;
	addr -= fmv_start & fmv_mask;
	addr &= fmv_mask;
	int mask = addr & BANK_MASK;
	if (mask == L64111_BASE)
		v = l64111_bget (addr);
	else if (mask == CL450_BASE)
		v = cl450_bget (addr);
	else if (mask == IO_BASE)
		v = io_bget (addr);
	else
		v = romram_bget (addr);
	return v;
}

static void REGPARAM2 fmv_wput (uaecptr addr, uae_u32 w)
{
	addr -= fmv_start & fmv_mask;
	addr &= fmv_mask;
#ifdef FMV_DEBUG
	if (isdebug (addr))
		write_log (_T("fmv_wput %04X=%04X PC=%08X\n"), addr, w & 65535, M68K_GETPC);
#endif
	int mask = addr & BANK_MASK;
	if (mask == L64111_BASE)
		l64111_wput (addr, w);
	else if (mask == CL450_BASE)
		cl450_wput (addr, w);
	else if (mask == IO_BASE)
		io_wput (addr, w);
	else
		ram_wput (addr, w);
}

static void REGPARAM2 fmv_lput (uaecptr addr, uae_u32 w)
{
#ifdef FMV_DEBUG
	if (isdebug (addr))
		write_log (_T("fmv_lput %08X=%08X PC=%08X\n"), addr, w, M68K_GETPC);
#endif
	fmv_wput (addr + 0, w >> 16);
	fmv_wput (addr + 2, w >>  0);
}

static void REGPARAM2 fmv_bput (uaecptr addr, uae_u32 w)
{
	addr -= fmv_start & fmv_mask;
	addr &= fmv_mask;
	int mask = addr & BANK_MASK;
	if (mask == L64111_BASE)
		l64111_bput (addr, w);
	else if (mask == CL450_BASE)
		cl450_bput (addr, w);
	else if (mask == IO_BASE)
		io_bput (addr, w);
	else
		ram_bput (addr, w);
}

static uae_u32 REGPARAM2 fmv_wgeti (uaecptr addr)
{
	uae_u32 v = 0;
	uae_u8 *m;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr -= fmv_start & fmv_mask;
	addr &= fmv_mask;
	m = rom + addr;
	if (addr < fmv_rom_size)
		return do_get_mem_word ((uae_u16 *)m);
#ifdef FMV_DEBUG
	write_log (_T("fmv_wgeti %08X %08X PC=%08X\n"), addr, v, M68K_GETPC);
#endif
	return v;
}

static uae_u32 REGPARAM2 fmv_lgeti (uaecptr addr)
{
	uae_u32 v = 0;
	uae_u8 *m;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr -= fmv_start & fmv_mask;
	addr &= fmv_mask;
	m = rom + addr;
	if (addr < fmv_rom_size)
		return do_get_mem_long ((uae_u32 *)m);
#ifdef FMV_DEBUG
	write_log (_T("fmv_lgeti %08X %08X PC=%08X\n"), addr, v, M68K_GETPC);
#endif
	return v;
}

static int REGPARAM2 fmv_check (uaecptr addr, uae_u32 size)
{
	addr -= fmv_start & fmv_mask;
	addr &= fmv_mask;
	return (addr + size) <= fmv_size;
}

static uae_u8 *REGPARAM2 fmv_xlate (uaecptr addr)
{
	addr -= fmv_start & fmv_mask;
	addr &= fmv_mask;
	return rom + addr;
}

static addrbank fmv_bank = {
	fmv_lget, fmv_wget, fmv_bget,
	fmv_lput, fmv_wput, fmv_bput,
	fmv_xlate, fmv_check, NULL, _T("CD32 FMV module"),
	fmv_lgeti, fmv_wgeti, ABFLAG_ROM | ABFLAG_IO
};



void cd32_fmv_init (uaecptr start)
{
	int ids[] = { 23, 74, -1 };
	struct romlist *rl = getromlistbyids (ids);
	struct romdata *rd;
	struct zfile *z;

	write_log (_T("CD32 FMV mapped @$%lx\n"), start);
	if (start != fmv_start)
		return;
	if (!rl)
		return;
	rd = rl->rd;
	z = read_rom (rd);
	if (z) {
		write_log (_T("CD32 FMV ROM %d.%d\n"), rd->ver, rd->rev);
		rom = mapped_malloc (fmv_size, _T("fast"));
		if (rom)
			zfile_fread (rom, rd->size, 1, z);
		zfile_fclose (z);
	}
	fmv_mask = fmv_size - 1;
	fmv_bank.baseaddr = rom;
	map_banks (&fmv_bank, start >> 16, fmv_size >> 16, 0);
}
