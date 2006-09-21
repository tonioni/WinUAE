 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Arcadia emulation
  *
  * Copyright 2005 Toni Wilen
  *
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "debug.h"
#include "arcadia.h"
#include "zfile.h"

/* supported roms (mame	0.90)
 *
 * - ar_airh
 * - ar_bowl
 * - ar_fast
 * - ar_ldrb
 * - ar_ldrba
 * - ar_ninj
 * - ar_rdwr
 * - ar_socc (99u8)
 * - ar_sdwr
 * - ar_spot
 * - ar_sprg
 * - ar_xeon
 *
 */

int arcadia_flag, arcadia_coin[2];
struct arcadiarom *arcadia_rom;

static char arcadia_path[MAX_DPATH];

static struct arcadiarom roms[]	= {
    { "ar_airh.zip", "scpa211", "airh_", 1, 5, 0, 2, 4, 7, 6, 1, 3 },
    { "ar_bowl.zip", "scpa211", "bowl_", 1, 7, 6, 0, 1, 2, 3, 4, 5 },
    { "ar_dart.zip", "scpa211", "dart_", 1, 4, 0, 7, 6, 3, 1, 2, 5 },
    { "ar_fast.zip", "scpav3_0.1", "fastv28.", 0, 7, 6, 5, 4, 3, 2, 1, 0 },
    { "ar_ldrb.zip", "scpa211", "lbg240", 0, 7, 6, 5, 4, 3, 2, 1, 0 },
    { "ar_ldrba.zip", "scpa211", "ldrb_", 1, 2, 3, 4, 1, 0, 7, 5, 6 },
    { "ar_ninj.zip", "scpa211", "ninj_", 1, 1, 6, 5, 7, 4, 2, 0, 3 },
    { "ar_rdwr.zip", "scpa211", "rdwr_", 1, 3, 1, 6, 4, 0, 5, 2, 7 },
    { "ar_socc.zip", "scpav3_0.1", "socc30.", 2, 0, 7, 1, 6, 5, 4, 3, 2 },
    { "ar_sdwr.zip", "scpa211", "sdwr_", 1, 6, 3, 4, 5, 2, 1, 0, 7 },
    { "ar_spot.zip", "scpav3_0.1", "spotv2.", 0, 7, 6, 5, 4, 3, 2, 1, 0 },
    { "ar_sprg.zip", "scpa211", "sprg_", 1, 4, 7, 3, 0, 6, 5, 2, 1 },
    { "ar_xeon.zip", "scpa211", "xeon_", 1, 3, 1, 2, 4, 0, 5, 6, 7 },
    { NULL, NULL, NULL }
};

static uae_u8 *arbmemory, *arbbmemory;
static int boot_read;

#define	arb_start 0x800000
#define	arb_mask 0x1fffff
#define	allocated_arbmemory 0x200000

#define arbb_start 0xf00000
#define	arbb_mask 0x7ffff
#define	allocated_arbbmemory 0x80000

#define	nvram_offset 0x1fc000
#define	bios_offset 0x180000
#define	NVRAM_SIZE 0x4000

static int nvwrite;

static int load_rom8 (char *xpath, uae_u8 *mem,	int extra)
{
    struct zfile *zf;
    char path[MAX_DPATH];
    int i;
    uae_u8 *tmp = xmalloc (131072);
    char *bin = extra == 1 ? ".bin" : "";

    memset (tmp, 0, 131072);
    sprintf (path, "%s%s%s", xpath, extra == 2 ? "hi" : "h", bin);
    zf = zfile_fopen (path, "rb");
    if (!zf)
	goto end;
    if (zfile_fread (tmp, 65536, 1, zf) == 0)
	goto end;
    zfile_fclose (zf);
    sprintf (path, "%s%s%s", xpath, extra == 2 ? "lo" : "l", bin);
    zf = zfile_fopen (path, "rb");
    if (!zf)
	goto end;
    if (zfile_fread (tmp + 65536, 65536, 1, zf) == 0)
	goto end;
    zfile_fclose (zf);
    for (i = 0; i < 65536; i++) {
	mem[i * 2 + 0] = tmp[i];
	mem[i * 2 + 1] = tmp[i + 65536];
    }
    xfree (tmp);
    return 1;
    end:
    xfree (tmp);
    return 0;
}

static struct arcadiarom *is_arcadia (char *xpath)
{
    char path[MAX_DPATH], *p;
    struct arcadiarom *rom = NULL;
    int i;

    strcpy (path, xpath);
    p = path;
    for (i = strlen (xpath) - 1; i > 0; i--) {
	if (path[i] == '\\' || path[i] == '/') {
	    path[i++] = 0;
	    p = path + i;
	    break;
	}
    }
    for (i = 0; roms[i].name; i++) {
	if (!strcmpi (p, roms[i].name)) {
	    rom = &roms[i];
	    break;
	}
    }
    if (!rom)
	return 0;
    return rom;
}

static int load_roms (char *xpath, struct arcadiarom *rom)
{
    char path[MAX_DPATH], path2[MAX_DPATH], path3[MAX_DPATH], *p;
    int i;

    strcpy (path3, xpath);
    p = path3 + strlen (path3) - 1;
    while (p > path3) {
	if (p[0] == '\\' || p[0] == '/') {
	    *p = 0;
	    break;
	}
	p--;
    }
    if (p == path3)
	*p = 0;
    strcpy (path2, xpath);
    if (strchr (rom->bios, '/'))
	strcpy (path2, path3);

    sprintf (path, "%s/ar_bios.zip/%s", path3, rom->bios);
    if (!load_rom8 (path, arbmemory + bios_offset, 0)) {
	write_log ("Arcadia: bios load failed ('%s')\n", path);
	sprintf (path, "%s/%s", path2, rom->bios);
	if (!load_rom8 (path, arbmemory + bios_offset, 0)) {
	    write_log ("Arcadia: bios load failed ('%s')\n", path);
	    return 0;
	}
    }
    write_log ("Arcadia: bios '%s' loaded\n", path);
    i = 0;
    for (;;) {
	sprintf (path, "%s/%s%d", xpath, rom->rom, i + 1);
	if (!load_rom8 (path, arbmemory + 2 * 65536 * i, rom->extra)) {
	    if (i == 0)
		write_log ("Arcadia: game rom load failed ('%s')\n", path);
	    break;
	}
	i++;
    }
    if (i == 0)
	return 0;
    write_log ("Arcadia: game rom loaded\n");
    return 1;
}

static uae_u8 bswap (uae_u8 v,int b7,int b6,int	b5,int b4,int b3,int b2,int b1,int b0)
{
    uae_u8 b = 0;

    b |= ((v >> b7) & 1) << 7;
    b |= ((v >> b6) & 1) << 6;
    b |= ((v >> b5) & 1) << 5;
    b |= ((v >> b4) & 1) << 4;
    b |= ((v >> b3) & 1) << 3;
    b |= ((v >> b2) & 1) << 2;
    b |= ((v >> b1) & 1) << 1;
    b |= ((v >> b0) & 1) << 0;
    return b;
}

static void decrypt_roms (struct arcadiarom *rom)
{
    int i, j;

    for (i = 1; i < 0x20000; i += 2) {
	arbmemory[i] = bswap (arbmemory[i],
	    rom->b7,rom->b6,rom->b5,rom->b4,rom->b3,rom->b2,rom->b1,rom->b0);
    	if (rom->extra == 2)
	    arbmemory[i - 1] = bswap (arbmemory[i - 1],7,6,5,4,3,2,1,0);
    }
    for (i = 1; i < 0x20000; i += 2) {
	j = i + bios_offset;
        arbmemory[j] = bswap (arbmemory[j],6,1,0,2,3,4,5,7);
    }
    if (!strcmp (rom->name, "ar_dart.zip"))
	arbmemory[1] = 0xfc;
}

static uae_u32 REGPARAM2 arbb_lget (uaecptr addr)
{
    uae_u32 *m;

    addr -= arbb_start & arbb_mask;
    addr &= arbb_mask;
    m = (uae_u32 *)(arbbmemory + addr);
    return do_get_mem_long (m);
}
static uae_u32 REGPARAM2 arbb_wget (uaecptr addr)
{
    uae_u16 *m;

    addr -= arbb_start & arbb_mask;
    addr &= arbb_mask;
    m = (uae_u16 *)(arbbmemory + addr);
    return do_get_mem_word (m);
}
static uae_u32 REGPARAM2 arbb_bget (uaecptr addr)
{
    addr -= arbb_start & arbb_mask;
    addr &= arbb_mask;
    return arbbmemory[addr];
}

static void REGPARAM2 arbb_lput (uaecptr addr, uae_u32 l)
{
}

static void REGPARAM2 arbb_wput (uaecptr addr, uae_u32 w)
{
}

static void REGPARAM2 arbb_bput (uaecptr addr, uae_u32 b)
{
}

static addrbank arcadia_boot_bank = {
    arbb_lget, arbb_wget, arbb_bget,
    arbb_lput, arbb_wput, arbb_bput,
    default_xlate, default_check, NULL, "Arcadia BIOS"
};

static uae_u32 REGPARAM2 arb_lget (uaecptr addr)
{
    uae_u32 *m;

    addr -= arb_start & arb_mask;
    addr &= arb_mask;
    m = (uae_u32 *)(arbmemory + addr);
    return do_get_mem_long (m);
}
static uae_u32 REGPARAM2 arb_wget (uaecptr addr)
{
    uae_u16 *m;

    addr -= arb_start & arb_mask;
    addr &= arb_mask;
    m = (uae_u16 *)(arbmemory + addr);
    return do_get_mem_word (m);
}
static uae_u32 REGPARAM2 arb_bget (uaecptr addr)
{
    addr -= arb_start & arb_mask;
    addr &= arb_mask;
    return arbmemory[addr];
}

static void REGPARAM2 arb_lput (uaecptr addr, uae_u32 l)
{
    uae_u32 *m;

    addr -= arb_start & arb_mask;
    addr &= arb_mask;
    if (addr >= nvram_offset) {
	m = (uae_u32 *)(arbmemory + addr);
	do_put_mem_long (m, l);
	nvwrite++;
    }
}

static void REGPARAM2 arb_wput (uaecptr addr, uae_u32 w)
{
    uae_u16 *m;

    addr -= arb_start & arb_mask;
    addr &= arb_mask;
    if (addr >= nvram_offset) {
	m = (uae_u16 *)(arbmemory + addr);
	do_put_mem_word (m, w);
	nvwrite++;
    }
}

static void REGPARAM2 arb_bput (uaecptr addr, uae_u32 b)
{
    addr -= arb_start & arb_mask;
    addr &= arb_mask;
    if (addr >= nvram_offset) {
	arbmemory[addr] = b;
	nvwrite++;
    }
}

static addrbank arcadia_rom_bank = {
    arb_lget, arb_wget, arb_bget,
    arb_lput, arb_wput, arb_bput,
    default_xlate, default_check, NULL, "Arcadia Game ROM"
};

int is_arcadia_rom (char *path)
{
    strcpy (arcadia_path, path);
    arcadia_rom = is_arcadia (path);
    return arcadia_rom ? 1 : 0;
}

static void nvram_write (void)
{
    struct zfile *f = zfile_fopen (currprefs.flashfile, "rb+");
    if (!f) {
	f = zfile_fopen (currprefs.flashfile, "wb");
	if (!f)
	    return;
    }
    zfile_fwrite (arbmemory + nvram_offset, NVRAM_SIZE, 1, f);
    zfile_fclose (f);
}

static void nvram_read (void)
{
    struct zfile *f;

    f = zfile_fopen (currprefs.flashfile, "rb");
    memset (arbmemory + nvram_offset, 0, NVRAM_SIZE);
    if (!f)
	return;
    zfile_fread (arbmemory + nvram_offset, NVRAM_SIZE, 1, f);
    zfile_fclose (f);
}

void arcadia_unmap (void)
{
    xfree (arbmemory);
    arbmemory = NULL;
    arcadia_rom = NULL;
}

int arcadia_map_banks (void)
{
    if (!arcadia_rom)
	return 0;
    arbmemory = xmalloc (allocated_arbmemory);
    arbbmemory = arbmemory + bios_offset;
    memset (arbmemory, 0, allocated_arbmemory);
    if (!load_roms (arcadia_path, arcadia_rom)) {
	arcadia_unmap ();
	return 0;
    }
    decrypt_roms (arcadia_rom);
    nvram_read ();
    map_banks (&arcadia_rom_bank, arb_start >> 16,
	allocated_arbmemory >> 16, 0);
    map_banks (&arcadia_boot_bank, 0xf0,
	8, 0);
    return 1;
}

void arcadia_vsync (void)
{
    static int cnt;

    cnt--;
    if (cnt > 0)
	return;
    cnt = 50;
    if (!nvwrite)
	return;
    nvram_write ();
    nvwrite = 0;
}

uae_u8 arcadia_parport (int port, uae_u8 pra, uae_u8 dra)
{
    uae_u8 v;

    v = (pra & dra) | (dra ^ 0xff);
    if (port) {
	if (dra & 1)
	    arcadia_coin[0] = arcadia_coin[1] = 0;
	return 0;
    }
    v = 0;
    v |= (arcadia_flag & 1) ? 0 : 2;
    v |= (arcadia_flag & 2) ? 0 : 4;
    v |= (arcadia_flag & 4) ? 0 : 8;
    v |= (arcadia_coin[0] & 3) << 4;
    v |= (arcadia_coin[1] & 3) << 6;
    return v;
}

