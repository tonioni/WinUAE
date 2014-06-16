/*
* UAE - The Un*x Amiga Emulator
*
* tablet.library
*
*/

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "traps.h"
#include "autoconf.h"
#include "execlib.h"
#include "tabletlibrary.h"

static uaecptr lib_init, lib_name, lib_id, base;
static uaecptr tablettags;
#define TAGS_SIZE (12 * 4)

static int tablet_x, tablet_y, tablet_resx, tablet_resy;
static int tablet_pressure, tablet_buttonbits, tablet_inproximity;
static int tablet_maxx, tablet_maxy, tablet_maxz;
static int ksversion;

void tabletlib_tablet (int x, int y, int z, int pressure, int maxpres, uae_u32 buttonbits, int inproximity, int ax, int ay, int az)
{
	tablet_x = x;
	tablet_y = y;
	tablet_pressure = pressure << 15;
	tablet_buttonbits = buttonbits;
	tablet_inproximity = inproximity;
}

void tabletlib_tablet_info (int maxx, int maxy, int maxz, int maxax, int maxay, int maxaz, int xres, int yres)
{
	tablet_maxx = maxx;
	tablet_maxy = maxy;
	tablet_resx = xres;
	tablet_resy = yres;
}

static void filltags (uaecptr tabletdata)
{
	uaecptr p = tablettags;
	if (!p)
		return;
	put_word (tabletdata + 0, 0);
	put_word (tabletdata + 2, 0);
	put_long (tabletdata + 4, tablet_x);
	put_long (tabletdata + 8, tablet_y);
	put_long (tabletdata + 12, tablet_maxx);
	put_long (tabletdata + 16, tablet_maxy);

	//write_log(_T("P=%08X BUT=%08X\n"), tablet_pressure, tablet_buttonbits);

	// pressure
	put_long (p, 0x8003a000 + 6);
	p += 4;
	put_long (p, tablet_pressure);
	p += 4;
	// buttonbits
	put_long (p, 0x8003a000 + 7);
	p += 4;
	put_long (p, tablet_buttonbits);
	p += 4;
	// resolutionx
	put_long (p, 0x8003a000 + 9);
	p += 4;
	put_long (p, tablet_resx);
	p += 4;
	// resolutiony
	put_long (p, 0x8003a000 + 10);
	p += 4;
	put_long (p, tablet_resy);
	p += 4;
	if (tablet_inproximity == 0) {
		// inproximity
		put_long (p, 0x8003a000 + 8);
		p += 4;
		put_long (p, 0);
		p += 4;
	}
	put_long (p, 0);
}

static uae_u32 REGPARAM2 lib_initcode (TrapContext *ctx)
{
	base = m68k_dreg (regs, 0);
	tablettags = base + SIZEOF_LIBRARY;
	tablet_inproximity = -1;
	tablet_x = tablet_y = 0;
	tablet_buttonbits = tablet_pressure = 0;
	ksversion = get_word (m68k_areg (regs, 6) + 20);
	return base;
}
static uae_u32 REGPARAM2 lib_openfunc (TrapContext *ctx)
{
	put_word (m68k_areg (regs, 6) + 32, get_word (m68k_areg (regs, 6) + 32) + 1);
	return m68k_areg (regs, 6);
}
static uae_u32 REGPARAM2 lib_closefunc (TrapContext *ctx)
{
	put_word (m68k_areg (regs, 6) + 32, get_word (m68k_areg (regs, 6) + 32) - 1);
	return 0;
}
static uae_u32 REGPARAM2 lib_expungefunc (TrapContext *context)
{
	return 0;
}

#define TAG_DONE   (0L)		/* terminates array of TagItems. ti_Data unused */
#define TAG_IGNORE (1L)		/* ignore this item, not end of array */
#define TAG_MORE   (2L)		/* ti_Data is pointer to another array of TagItems */
#define TAG_SKIP   (3L)		/* skip this and the next ti_Data items */

static uae_u32 REGPARAM2 lib_allocfunc (TrapContext *context)
{
	uae_u32 tags = m68k_areg (regs, 0);
	uae_u32 mem;
	m68k_dreg (regs, 0) = 24;
	m68k_dreg (regs, 1) = 65536 + 1;
	mem = CallLib (context, get_long (4), -0xC6); /* AllocMem */
	if (!mem)
		return 0;
	for (;;) {
		uae_u32 t = get_long(tags);
		if (t == TAG_DONE)
			break;
		if (t == TAG_SKIP) {
			tags += 8 + get_long(tags + 4) * 8;
		} else if (t == TAG_MORE) {
			tags = get_long(tags + 4);
		} else if (t == TAG_IGNORE) {
			tags += 8;
		} else {
			t -= 0x8003a000;
			// clear "unknown" tags
			if (t != 6 && t != 8)
				put_long(tags, TAG_IGNORE);
			tags += 8;
		}
	}
	put_long (mem + 20, tablettags);
	filltags (mem);
	return mem;
}
static uae_u32 REGPARAM2 lib_freefunc (TrapContext *context)
{
	m68k_areg (regs, 1) = m68k_areg (regs, 0);
	m68k_dreg (regs, 0) = 24;
	CallLib(context, get_long (4), -0xD2);
	return 0;
}
static uae_u32 REGPARAM2 lib_dofunc (TrapContext *context)
{
	uaecptr im = m68k_areg (regs, 0);
	uaecptr td = m68k_areg (regs, 1);
	filltags (td);
	if (ksversion < 39)
		return 0;
	td = get_long (im + 52);
	if (!td)
		return 0;
	return 1;
}
static uae_u32 REGPARAM2 lib_unkfunc (TrapContext *context)
{
	write_log (_T("tablet.library unknown function called\n"));
	return 0;
}

uaecptr tabletlib_startup (uaecptr resaddr)
{
	if (!currprefs.tablet_library)
		return resaddr;
	put_word (resaddr + 0x0, 0x4AFC);
	put_long (resaddr + 0x2, resaddr);
	put_long (resaddr + 0x6, resaddr + 0x1A); /* Continue scan here */
	put_word (resaddr + 0xA, 0x8127); /* RTF_AUTOINIT|RTF_COLDSTART; Version 1 */
	put_word (resaddr + 0xC, 0x0900); /* NT_LIBRARY; pri 00 */
	put_long (resaddr + 0xE, lib_name);
	put_long (resaddr + 0x12, lib_id);
	put_long (resaddr + 0x16, lib_init);
	resaddr += 0x1A;
	return resaddr;
}

void tabletlib_install (void)
{
	uae_u32 functable, datatable;
	uae_u32 initcode, openfunc, closefunc, expungefunc;
	uae_u32 allocfunc, freefunc, dofunc, unkfunc;
	TCHAR tmp[100];

	if (!currprefs.tablet_library)
		return;

	_stprintf (tmp, _T("UAE tablet.library %d.%d.%d"), UAEMAJOR, UAEMINOR, UAESUBREV);
	lib_name = ds (_T("tablet.library"));
	lib_id = ds (tmp);

	initcode = here ();
	calltrap (deftrap (lib_initcode)); dw (RTS);
	openfunc = here ();
	calltrap (deftrap (lib_openfunc)); dw (RTS);
	closefunc = here ();
	calltrap (deftrap (lib_closefunc)); dw (RTS);
	expungefunc = here ();
	calltrap (deftrap (lib_expungefunc)); dw (RTS);
	allocfunc = here ();
	calltrap (deftrap2 (lib_allocfunc, TRAPFLAG_EXTRA_STACK, _T("tablet_alloc"))); dw (RTS);
	freefunc = here ();
	calltrap (deftrap2 (lib_freefunc, TRAPFLAG_EXTRA_STACK, _T("tablet_free"))); dw (RTS);
	dofunc = here ();
	calltrap (deftrap (lib_dofunc)); dw (RTS);
	unkfunc = here ();
	calltrap (deftrap (lib_unkfunc)); dw (RTS);

	/* FuncTable */
	functable = here ();
	dl (openfunc);
	dl (closefunc);
	dl (expungefunc);
	dl (EXPANSION_nullfunc);
	dl (allocfunc);
	dl (freefunc);
	dl (dofunc);
	dl (0xFFFFFFFF); /* end of table */

	/* DataTable */
	datatable = here ();
	dw (0xE000); /* INITBYTE */
	dw (0x0008); /* LN_TYPE */
	dw (0x0900); /* NT_LIBRARY */
	dw (0xC000); /* INITLONG */
	dw (0x000A); /* LN_NAME */
	dl (lib_name);
	dw (0xE000); /* INITBYTE */
	dw (0x000E); /* LIB_FLAGS */
	dw (0x0600); /* LIBF_SUMUSED | LIBF_CHANGED */
	dw (0xD000); /* INITWORD */
	dw (0x0027); /* LIB_VERSION */
	dw (UAEMAJOR);
	dw (0xD000); /* INITWORD */
	dw (0x0016); /* LIB_REVISION */
	dw (UAEMINOR);
	dw (0xC000); /* INITLONG */
	dw (0x0018); /* LIB_IDSTRING */
	dl (lib_id);
	dw (0x0000); /* end of table */

	lib_init = here ();
	dl (SIZEOF_LIBRARY + TAGS_SIZE); /* size of lib base */
	dl (functable);
	dl (datatable);
	dl (initcode);
}
