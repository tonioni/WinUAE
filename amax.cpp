
#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "zfile.h"
#include "amax.h"
#include "custom.h"
#include "memory.h"
#include "newcpu.h"

static int data_scramble[8] = { 3, 2, 4, 5, 7, 6, 0, 1 };
static int addr_scramble[16] = { 14, 12, 2, 10, 15, 13, 1, 0, 7, 6, 5, 4, 8, 9, 11, 3 };

static int romptr;
static uae_u8 *rom;
static int rom_size, rom_oddeven;
static uae_u8 data;
static uae_u8 bfd100, bfe001;
static uae_u8 dselect;

#define AMAX_LOG 0

static void load_byte (void)
{
	int addr, i;
	uae_u8 val, v;

	v = 0xff;
	addr = 0;
	for (i = 0; i < 16; i++) {
		if (romptr & (1 << i))
			addr |= 1 << addr_scramble[i];
	}
	if (rom_oddeven < 0) {
		val = v;
	} else {
		v = rom[addr * 2 + rom_oddeven];
		val = 0;
		for (i = 0; i < 8; i++) {
			if (v & (1 << data_scramble[i]))
				val |= 1 << i;
		}
	}
	data = val;
	if (AMAX_LOG > 0)
		write_log (_T("AMAX: load byte, rom=%d addr=%06x (%06x) data=%02x (%02x) PC=%08X\n"), rom_oddeven, romptr, addr, v, val, M68K_GETPC);
}

static void amax_check (void)
{
	/* DIR low = reset address counter */
	if ((bfd100 & 2)) {
		if (romptr && AMAX_LOG > 0)
			write_log (_T("AMAX: counter reset PC=%08X\n"), M68K_GETPC);
		romptr = 0;
	}
}

static int dwlastbit;

void amax_diskwrite (uae_u16 w)
{
	int i;

	/* this is weird, 1->0 transition in disk write line increases address pointer.. */
	for (i = 0; i < 16; i++) {
		if (dwlastbit && !(w & 0x8000)) {
			romptr++;
			if (AMAX_LOG > 0)
				write_log (_T("AMAX: counter increase %d PC=%08X\n"), romptr, M68K_GETPC);
		}
		dwlastbit = (w & 0x8000) ? 1 : 0;
		w <<= 1;
	}
	romptr &= rom_size - 1;
	amax_check ();
}

static uae_u8 bfe001_ov;

void amax_bfe001_write (uae_u8 pra, uae_u8 dra)
{
	uae_u8 v = dra & pra;

	bfe001 = v;
	/* CHNG low -> high: shift data register */
	if ((v & 4) && !(bfe001_ov & 4)) {
		data <<= 1;
		data |= 1;
		if (AMAX_LOG > 0)
			write_log (_T("AMAX: data shifted\n"));
	}
	/* TK0 = even, WPRO = odd */
	rom_oddeven = -1;
	if ((v & (8 | 16)) != (8 | 16)) {
		rom_oddeven = 0;
		if (!(v & 16))
			rom_oddeven = 1;
	}
	bfe001_ov = v;
	amax_check ();
}

void amax_disk_select (uae_u8 v, uae_u8 ov)
{
	bfd100 = v;

	if (!(bfd100 & dselect) && (ov & dselect))
		load_byte ();
	amax_check ();
}

uae_u8 amax_disk_status (void)
{
	uae_u8 st = 0x3c;

	if (!(data & 0x80))
		st &= ~0x20;
	return st;
}

void amax_reset (void)
{
	romptr = 0;
	rom_oddeven = 0;
	bfe001_ov = 0;
	dwlastbit = 0;
	data = 0xff;
	xfree (rom);
	rom = NULL;
	dselect = 0;
}

void amax_init (void)
{
	struct zfile *z;

	if (!currprefs.amaxromfile[0])
		return;
	amax_reset ();
	z = zfile_fopen (currprefs.amaxromfile, _T("rb"), ZFD_NORMAL);
	if (!z) {
		write_log (_T("AMAX: failed to load rom '%s'\n"), currprefs.amaxromfile);
		return;
	}
	zfile_fseek (z, 0, SEEK_END);
	rom_size = zfile_ftell (z);
	zfile_fseek (z, 0, SEEK_SET);
	rom = xmalloc (uae_u8, rom_size);
	zfile_fread (rom, rom_size, 1, z);
	zfile_fclose (z);
	write_log (_T("AMAX: '%s' loaded, %d bytes\n"), currprefs.amaxromfile, rom_size);
	dselect = 0x20;
}



