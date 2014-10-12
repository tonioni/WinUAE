/*
* UAE - The Un*x Amiga Emulator
*
* CDTV-CR emulation
*
* Copyright 2014 Toni Wilen
*
*
*/

#define CDTVCR_DEBUG 1

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "newcpu.h"
#include "debug.h"
#include "zfile.h"
#include "gui.h"
#include "cdtvcr.h"

#define CDTVCR_MASK 0xffff

#define CDTVCR_RAM_OFFSET 0x1000
#define CDTVCR_RAM_SIZE 4096
#define CDTVCR_RAM_MASK (CDTVCR_RAM_SIZE - 1)

#define CDTVCR_CLOCK 0xc10

static uae_u8 cdtvcr_ram[CDTVCR_RAM_SIZE];

static void cdtvcr_battram_reset (void)
{
	struct zfile *f;
	int v;

	memset (cdtvcr_ram, 0, CDTVCR_RAM_SIZE);
	f = zfile_fopen (currprefs.flashfile, _T("rb+"), ZFD_NORMAL);
	if (!f) {
		f = zfile_fopen (currprefs.flashfile, _T("wb"), 0);
		if (f) {
			zfile_fwrite (cdtvcr_ram, CDTVCR_RAM_SIZE, 1, f);
			zfile_fclose (f);
		}
		return;
	}
	v = zfile_fread (cdtvcr_ram, 1, CDTVCR_RAM_SIZE, f);
	if (v < CDTVCR_RAM_SIZE)
		zfile_fwrite (cdtvcr_ram + v, 1, CDTVCR_RAM_SIZE - v, f);
	zfile_fclose (f);
}

static void cdtvcr_battram_write (int addr, int v)
{
	struct zfile *f;
	int offset = addr & CDTVCR_RAM_MASK;

	if (offset >= CDTVCR_RAM_SIZE)
		return;
	gui_flicker_led (LED_MD, 0, 2);
	if (cdtvcr_ram[offset] == v)
		return;
	cdtvcr_ram[offset] = v;
	f = zfile_fopen (currprefs.flashfile, _T("rb+"), ZFD_NORMAL);
	if (!f)
		return;
	zfile_fseek (f, offset, SEEK_SET);
	zfile_fwrite (cdtvcr_ram + offset, 1, 1, f);
	zfile_fclose (f);
}

static uae_u8 cdtvcr_battram_read (int addr)
{
	uae_u8 v;
	int offset;
	offset = addr & CDTVCR_RAM_MASK;
	if (offset >= CDTVCR_RAM_SIZE)
		return 0;
	gui_flicker_led (LED_MD, 0, 1);
	v = cdtvcr_ram[offset];
	return v;
}

static void cdtvcr_bput2 (uaecptr addr, uae_u32 b)
{
	addr &= CDTVCR_MASK;
	if (addr >= CDTVCR_RAM_OFFSET && addr < CDTVCR_RAM_OFFSET + CDTVCR_RAM_SIZE) {
		addr -= CDTVCR_RAM_OFFSET;
		cdtvcr_battram_write(addr, b);
	}
}
static uae_u8 cdtvcr_bget2 (uaecptr addr)
{
	uae_u8 v = 0;
	addr &= CDTVCR_MASK;
	if (addr >= CDTVCR_RAM_OFFSET && addr < CDTVCR_RAM_OFFSET + CDTVCR_RAM_SIZE) {
		addr -= CDTVCR_RAM_OFFSET;
		v = cdtvcr_battram_read(addr);
	} else if (addr == 0x9dc) {
		v = 'C';
	} else if (addr == 0x9dd) {
		v = 'D';
	} else if (addr == 0x9de) {
		v = 'T';
	} else if (addr == 0x9df) {
		v = 'V';
	} else if (addr >= CDTVCR_CLOCK && addr < CDTVCR_CLOCK + 0x20) {
		int reg = addr - CDTVCR_CLOCK;
		int days, mins, ticks;
		int tickcount = currprefs.ntscmode ? 60 : 50;
		struct timeval tv;
		struct mytimeval mtv;
		gettimeofday (&tv, NULL);
		tv.tv_sec -= _timezone;
		mtv.tv_sec = tv.tv_sec;
		mtv.tv_usec = tv.tv_usec;
		timeval_to_amiga(&mtv, &days, &mins, &ticks, tickcount);
		switch (reg)
		{
			case 0:
			case 1:
			v = 0;
			break;
			case 2:
			v = days >> 8;
			break;
			case 3:
			v = days;
			break;
			case 4:
			v = mins / 60;
			break;
			case 5:
			v = mins % 60;
			break;
			case 6:
			v = ticks / tickcount;
			break;
			case 7:
			v = ticks % tickcount;
			break;
			case 8:
			v = tickcount;
			break;

		}

	}
	return v;
}

static uae_u32 REGPARAM2 cdtvcr_lget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v = (cdtvcr_bget2 (addr) << 24) | (cdtvcr_bget2 (addr + 1) << 16) |
		(cdtvcr_bget2 (addr + 2) << 8) | (cdtvcr_bget2 (addr + 3));
#if CDTVCR_DEBUG
	write_log (_T("cdtvcr_lget %08X=%08X PC=%08X\n"), addr, v, M68K_GETPC);
#endif
	return v;
}

static uae_u32 REGPARAM2 cdtvcr_wgeti (uaecptr addr)
{
	uae_u32 v = 0xffff;
#ifdef JIT
	special_mem |= S_READ;
#endif
	return v;
}
static uae_u32 REGPARAM2 cdtvcr_lgeti (uaecptr addr)
{
	uae_u32 v = 0xffff;
#ifdef JIT
	special_mem |= S_READ;
#endif
	return v;
}

static uae_u32 REGPARAM2 cdtvcr_wget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v = (cdtvcr_bget2 (addr) << 8) | cdtvcr_bget2 (addr + 1);
#if CDTVCR_DEBUG
	write_log (_T("cdtvcr_wget %08X=%04X PC=%08X\n"), addr, v, M68K_GETPC);
#endif
	return v;
}

static uae_u32 REGPARAM2 cdtvcr_bget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v = cdtvcr_bget2 (addr);
	return v;
}

static void REGPARAM2 cdtvcr_lput (uaecptr addr, uae_u32 l)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
#if CDTVCR_DEBUG
	write_log (_T("cdtvcr_lput %08X=%08X PC=%08X\n"), addr, l, M68K_GETPC);
#endif
	cdtvcr_bput2 (addr, l >> 24);
	cdtvcr_bput2 (addr + 1, l >> 16);
	cdtvcr_bput2 (addr + 2, l >> 8);
	cdtvcr_bput2 (addr + 3, l);
}

static void REGPARAM2 cdtvcr_wput (uaecptr addr, uae_u32 w)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
#if CDTVCR_DEBUG
	write_log (_T("cdtvcr_wput %08X=%04X PC=%08X\n"), addr, w & 65535, M68K_GETPC);
#endif
	cdtvcr_bput2 (addr, w >> 8);
	cdtvcr_bput2 (addr + 1, w);
}

static void REGPARAM2 cdtvcr_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	cdtvcr_bput2 (addr, b);
}


addrbank cdtvcr_bank = {
	cdtvcr_lget, cdtvcr_wget, cdtvcr_bget,
	cdtvcr_lput, cdtvcr_wput, cdtvcr_bput,
	default_xlate, default_check, NULL, NULL, _T("CDTV-CR"),
	cdtvcr_lgeti, cdtvcr_wgeti, ABFLAG_IO
};

void cdtvcr_reset(void)
{
	cdtvcr_battram_reset();
}

void cdtvcr_free(void)
{
}
