#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "uae.h"
#include "memory.h"
#include "newcpu.h"
#include "casablanca.h"

static uae_u32 REGPARAM2 casa_lget(uaecptr addr)
{
	write_log(_T("casa_lget %08x %08x\n"), addr, M68K_GETPC);
	return 0;
}
static uae_u32 REGPARAM2 casa_wget(uaecptr addr)
{
	write_log(_T("casa_wget %08x %08x\n"), addr, M68K_GETPC);
	return 0;
}

static uae_u32 REGPARAM2 casa_bget(uaecptr addr)
{
	uae_u8 v = 0;
	write_log(_T("casa_bget %08x %08x\n"), addr, M68K_GETPC);

	if (addr == 0x020007c3)
		v = 4;

	return v;
}

static void REGPARAM2 casa_lput(uaecptr addr, uae_u32 l)
{
	write_log(_T("casa_lput %08x %08x %08x\n"), addr, l, M68K_GETPC);
}
static void REGPARAM2 casa_wput(uaecptr addr, uae_u32 w)
{
	write_log(_T("casa_wput %08x %04x %08x\n"), addr, w & 0xffff, M68K_GETPC);
}

static void REGPARAM2 casa_bput(uaecptr addr, uae_u32 b)
{
	write_log(_T("casa_bput %08x %02x %08x\n"), addr, b & 0xff, M68K_GETPC);
}

static addrbank casa_ram_bank = {
	casa_lget, casa_wget, casa_bget,
	casa_lput, casa_wput, casa_bput,
	default_xlate, default_check, NULL, NULL, _T("Casablanca IO"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO | ABFLAG_SAFE, S_READ, S_WRITE,
};


void casablanca_map_overlay(void)
{
	// Casablanca has ROM at address zero, no chip ram, no overlay.
	map_banks(&kickmem_bank, 524288 >> 16, 524288 >> 16, 0);
	map_banks(&extendedkickmem_bank, 0 >> 16, 524288 >> 16, 0);
	map_banks(&casa_ram_bank, 0x02000000 >> 16, 0x01000000 >> 16, 0);
}
