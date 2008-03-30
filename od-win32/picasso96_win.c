/*
* UAE - The U*nix Amiga Emulator
*
* Picasso96 Support Module
*
* Copyright 1997-2001 Brian King <Brian_King@CodePoet.com>
* Copyright 2000-2001 Bernd Roesch <>
*
* Theory of operation:
* On the Amiga side, a Picasso card consists mainly of a memory area that
* contains the frame buffer.  On the UAE side, we allocate a block of memory
* that will hold the frame buffer.  This block is in normal memory, it is
* never directly on the graphics card. All graphics operations, which are
* mainly reads and writes into this block and a few basic operations like
* filling a rectangle, operate on this block of memory.
* Since the memory is not on the graphics card, some work must be done to
* synchronize the display with the data in the Picasso frame buffer.  There
* are various ways to do this. One possibility is to allocate a second
* buffer of the same size, and perform all write operations twice.  Since
* we never read from the second buffer, it can actually be placed in video
* memory.  The X11 driver could be made to use the Picasso frame buffer as
* the data buffer of an XImage, which could then be XPutImage()d from time
* to time.  Another possibility is to translate all Picasso accesses into
* Xlib (or GDI, or whatever your graphics system is) calls.  This possibility
* is a bit tricky, since there is a risk of generating very many single pixel
* accesses which may be rather slow.
*
* TODO:
* - we want to add a manual switch to override SetSwitch for hardware banging
*   programs started from a Picasso workbench.
*/

#define P96DX 0

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "threaddep/thread.h"
#include "memory.h"
#include "custom.h"
#include "events.h"
#include "newcpu.h"
#include "xwin.h"
#include "savestate.h"
#include "autoconf.h"
#include "traps.h"

#if defined(PICASSO96)

#include "registry.h"
#include "dxwrap.h"
#include "rp.h"
#include "picasso96_win.h"
#include "win32gfx.h"

#define NOBLITTER 0
#define NOBLITTER_BLIT 0

static int hwsprite = 0;
static int picasso96_BT = BT_uaegfx;
static int picasso96_GCT = GCT_Unknown;
static int picasso96_PCT = PCT_Unknown;
#ifndef UAEGFX_INTERNAL
static int uaegfxcard_old = 1;
#endif

int mman_GetWriteWatch (PVOID lpBaseAddress, SIZE_T dwRegionSize, PVOID *lpAddresses, PULONG_PTR lpdwCount, PULONG lpdwGranularity);
void mman_ResetWatch (PVOID lpBaseAddress, SIZE_T dwRegionSize);

#define P96TRACING_ENABLED 0

int p96hack_vpos, p96hack_vpos2, p96refresh_active;
int have_done_picasso; /* For the JIT compiler */
static int p96syncrate;
int p96hsync_counter, palette_changed;
#if defined(X86_MSVC_ASSEMBLY)
#define SWAPSPEEDUP
#endif
#ifdef PICASSO96
#ifdef DEBUG // Change this to _DEBUG for debugging
#define P96TRACING_ENABLED 1
#define P96TRACING_LEVEL 1
#endif
static void flushpixels(void);
#if P96TRACING_ENABLED
#define P96TRACE(x) do { write_log x; } while(0)
#define P96TRACE_SPR(x) do { write_log x; } while(0)
#else
#define P96TRACE(x)
#define P96TRACE_SPR(x)
#endif
#define P96TRACE2(x) do { write_log x; } while(0)

static void REGPARAM2 gfxmem_lputx (uaecptr, uae_u32) REGPARAM;
static void REGPARAM2 gfxmem_wputx (uaecptr, uae_u32) REGPARAM;
static void REGPARAM2 gfxmem_bputx (uaecptr, uae_u32) REGPARAM;

static uae_u8 all_ones_bitmap, all_zeros_bitmap; /* yuk */

struct picasso96_state_struct picasso96_state;
struct picasso_vidbuf_description picasso_vidinfo;
static struct PicassoResolution *newmodes;

static int picasso_convert, host_mode;

/* These are the maximum resolutions... They are filled in by GetSupportedResolutions() */
/* have to fill this in, otherwise problems occur on the Amiga side P96 s/w which expects
/* data here. */
static struct ScreenResolution planar = { 320, 240 };
static struct ScreenResolution chunky = { 640, 480 };
static struct ScreenResolution hicolour = { 640, 480 };
static struct ScreenResolution truecolour = { 640, 480 };
static struct ScreenResolution alphacolour = { 640, 480 };

uae_u32 p96_rgbx16[65536];

#if P96DX > 0
static LPDIRECTDRAWSURFACE7 p96surface;
#endif
static LPDIRECTDRAWSURFACE7 cursorsurface;
static int cursorwidth, cursorheight;
static uae_u32 cursorrgb[4], cursorrgbn[4];
static int reloadcursor, cursorvisible, cursordeactivate;
static uaecptr cursorbi;


static uaecptr uaegfx_resname,
    uaegfx_resid,
    uaegfx_init,
    uaegfx_base;

typedef enum {
    BLIT_FALSE,
    BLIT_NOR,
    BLIT_ONLYDST,
    BLIT_NOTSRC,
    BLIT_ONLYSRC,
    BLIT_NOTDST,
    BLIT_EOR,
    BLIT_NAND,
    BLIT_AND,
    BLIT_NEOR,
    BLIT_DST,
    BLIT_NOTONLYSRC,
    BLIT_SRC,
    BLIT_NOTONLYDST,
    BLIT_OR,
    BLIT_TRUE,
    BLIT_LAST
} BLIT_OPCODE;

#include "win32gui.h"
#include "resource.h"
#define UAE_RTG_LIBRARY_VERSION 40
#define UAE_RTG_LIBRARY_REVISION 3994
static void checkrtglibrary(void)
{
    uae_u32 v;
    static int checked = FALSE;

    if (checked)
	return;
    v = get_long (4); // execbase
    v += 378; // liblist
    while ((v = get_long (v))) {
	uae_u32 v2 = get_long (v + 10); // name
	uae_u8 *p;
	addrbank *b = &get_mem_bank(v2);
	if (!b || !b->check (v2, 12))
	    continue;
	p = b->xlateaddr(v2);
	if (!memcmp(p, "rtg.library\0", 12)) {
	    uae_u16 ver = get_word (v + 20);
	    uae_u16 rev = get_word (v + 22);
	    if (ver * 10000 + rev < UAE_RTG_LIBRARY_VERSION * 10000 + UAE_RTG_LIBRARY_REVISION) {
		char msg[2000];
		WIN32GUI_LoadUIString(IDS_OLDRTGLIBRARY, msg, sizeof(msg));
		gui_message(msg, ver, rev, UAE_RTG_LIBRARY_VERSION, UAE_RTG_LIBRARY_REVISION);
	    } else {
		write_log ("P96: rtg.library %d.%d detected\n", ver, rev);
	    }
	    checked = TRUE;
	}
    }
}

static uae_u32 p2ctab[256][2];
static int set_gc_called = 0, init_picasso_screen_called = 0;
//fastscreen
static uaecptr oldscr = 0;


static void endianswap (uae_u32 *vp, int bpp)
{
    uae_u32 v = *vp;
    switch (bpp)
    {
	case 2:
	*vp = (((v >> 8) & 0x00ff) | (v << 8)) & 0xffff;
	break;
	case 4:
	*vp = ((v >> 24) & 0x000000ff) | ((v >> 8) & 0x0000ff00) | ((v << 8) & 0x00ff0000) | ((v << 24) & 0xff000000);
	break;
    }
}

#if P96TRACING_ENABLED
/*
* Debugging dumps
*/
static void DumpModeInfoStructure (uaecptr amigamodeinfoptr)
{
    write_log ("ModeInfo Structure Dump:\n");
    write_log ("  Node.ln_Succ  = 0x%x\n", get_long (amigamodeinfoptr));
    write_log ("  Node.ln_Pred  = 0x%x\n", get_long (amigamodeinfoptr + 4));
    write_log ("  Node.ln_Type  = 0x%x\n", get_byte (amigamodeinfoptr + 8));
    write_log ("  Node.ln_Pri   = %d\n", get_byte (amigamodeinfoptr + 9));
    /*write_log ("  Node.ln_Name  = %s\n", uaememptr->Node.ln_Name); */
    write_log ("  OpenCount     = %d\n", get_word (amigamodeinfoptr + PSSO_ModeInfo_OpenCount));
    write_log ("  Active        = %d\n", get_byte (amigamodeinfoptr + PSSO_ModeInfo_Active));
    write_log ("  Width         = %d\n", get_word (amigamodeinfoptr + PSSO_ModeInfo_Width));
    write_log ("  Height        = %d\n", get_word (amigamodeinfoptr + PSSO_ModeInfo_Height));
    write_log ("  Depth         = %d\n", get_byte (amigamodeinfoptr + PSSO_ModeInfo_Depth));
    write_log ("  Flags         = %d\n", get_byte (amigamodeinfoptr + PSSO_ModeInfo_Flags));
    write_log ("  HorTotal      = %d\n", get_word (amigamodeinfoptr + PSSO_ModeInfo_HorTotal));
    write_log ("  HorBlankSize  = %d\n", get_word (amigamodeinfoptr + PSSO_ModeInfo_HorBlankSize));
    write_log ("  HorSyncStart  = %d\n", get_word (amigamodeinfoptr + PSSO_ModeInfo_HorSyncStart));
    write_log ("  HorSyncSize   = %d\n", get_word (amigamodeinfoptr + PSSO_ModeInfo_HorSyncSize));
    write_log ("  HorSyncSkew   = %d\n", get_byte (amigamodeinfoptr + PSSO_ModeInfo_HorSyncSkew));
    write_log ("  HorEnableSkew = %d\n", get_byte (amigamodeinfoptr + PSSO_ModeInfo_HorEnableSkew));
    write_log ("  VerTotal      = %d\n", get_word (amigamodeinfoptr + PSSO_ModeInfo_VerTotal));
    write_log ("  VerBlankSize  = %d\n", get_word (amigamodeinfoptr + PSSO_ModeInfo_VerBlankSize));
    write_log ("  VerSyncStart  = %d\n", get_word (amigamodeinfoptr + PSSO_ModeInfo_VerSyncStart));
    write_log ("  VerSyncSize   = %d\n", get_word (amigamodeinfoptr + PSSO_ModeInfo_VerSyncSize));
    write_log ("  Clock         = %d\n", get_byte (amigamodeinfoptr + PSSO_ModeInfo_first_union));
    write_log ("  ClockDivide   = %d\n", get_byte (amigamodeinfoptr + PSSO_ModeInfo_second_union));
    write_log ("  PixelClock    = %d\n", get_long (amigamodeinfoptr + PSSO_ModeInfo_PixelClock));
}

static void DumpLibResolutionStructure (uaecptr amigalibresptr)
{
    int i;
    uaecptr amigamodeinfoptr;
    struct LibResolution *uaememptr = (struct LibResolution *)get_mem_bank(amigalibresptr).xlateaddr(amigalibresptr);

    write_log ("LibResolution Structure Dump:\n");

    if (get_long (amigalibresptr + PSSO_LibResolution_DisplayID) == 0xFFFFFFFF) {
	write_log ("  Finished With LibResolutions...\n");
    } else {
	write_log ("  Name      = %s\n", uaememptr->P96ID);
	write_log ("  DisplayID = 0x%x\n", get_long (amigalibresptr + PSSO_LibResolution_DisplayID));
	write_log ("  Width     = %d\n", get_word (amigalibresptr + PSSO_LibResolution_Width));
	write_log ("  Height    = %d\n", get_word (amigalibresptr + PSSO_LibResolution_Height));
	write_log ("  Flags     = %d\n", get_word (amigalibresptr + PSSO_LibResolution_Flags));
	for (i = 0; i < MAXMODES; i++) {
	    amigamodeinfoptr = get_long (amigalibresptr + PSSO_LibResolution_Modes + i*4);
	    write_log ("  ModeInfo[%d] = 0x%x\n", i, amigamodeinfoptr);
	    if (amigamodeinfoptr)
		DumpModeInfoStructure (amigamodeinfoptr);
	}
	write_log ("  BoardInfo = 0x%x\n", get_long (amigalibresptr + PSSO_LibResolution_BoardInfo));
    }
}

static char binary_byte[9] = { 0,0,0,0,0,0,0,0,0 };

static char *BuildBinaryString (uae_u8 value)
{
    int i;
    for (i = 0; i < 8; i++) {
	binary_byte[i] = (value & (1 << (7 - i))) ? '#' : '.';
    }
    return binary_byte;
}

static void DumpPattern (struct Pattern *patt)
{
    uae_u8 *mem;
    int row, col;
    for (row = 0; row < (1 << patt->Size); row++) {
	mem = patt->Memory + row * 2;
	for (col = 0; col < 2; col++) {
	    write_log ("%s ", BuildBinaryString (*mem++));
	}
	write_log ("\n");
    }
}

static void DumpTemplate (struct Template *tmp, unsigned long w, unsigned long h)
{
    uae_u8 *mem = tmp->Memory;
    unsigned int row, col, width;
    width = (w + 7) >> 3;
    write_log ("xoffset = %d, bpr = %d\n", tmp->XOffset, tmp->BytesPerRow);
    for (row = 0; row < h; row++) {
	mem = tmp->Memory + row * tmp->BytesPerRow;
	for (col = 0; col < width; col++) {
	    write_log ("%s ", BuildBinaryString (*mem++));
	}
	write_log ("\n");
    }
}

static void DumpLine(struct Line *line)
{
    if (line) {
	write_log ("Line->X = %d\n", line->X);
	write_log ("Line->Y = %d\n", line->Y);
	write_log ("Line->Length = %d\n", line->Length);
	write_log ("Line->dX = %d\n", line->dX);
	write_log ("Line->dY = %d\n", line->dY);
	write_log ("Line->sDelta = %d\n", line->sDelta);
	write_log ("Line->lDelta = %d\n", line->lDelta);
	write_log ("Line->twoSDminusLD = %d\n", line->twoSDminusLD);
	write_log ("Line->LinePtrn = %d\n", line->LinePtrn);
	write_log ("Line->PatternShift = %d\n", line->PatternShift);
	write_log ("Line->FgPen = 0x%x\n", line->FgPen);
	write_log ("Line->BgPen = 0x%x\n", line->BgPen);
	write_log ("Line->Horizontal = %d\n", line->Horizontal);
	write_log ("Line->DrawMode = %d\n", line->DrawMode);
	write_log ("Line->Xorigin = %d\n", line->Xorigin);
	write_log ("Line->Yorigin = %d\n", line->Yorigin);
    }
}

static void ShowSupportedResolutions (void)
{
    int i = 0;

    write_log ("-----------------\n");
    while (newmodes[i].depth >= 0) {
	write_log ("%s\n", newmodes[i].name);
	i++;
    }
    write_log ("-----------------\n");
}

#endif

static void **gwwbuf;
static int gwwbufsize, gwwpagesize, gwwpagemask;
extern uae_u8 *natmem_offset;

static uae_u8 GetBytesPerPixel (uae_u32 RGBfmt)
{
    switch (RGBfmt)
    {
	case RGBFB_CLUT:
	    return 1;

	case RGBFB_A8R8G8B8:
	case RGBFB_A8B8G8R8:
	case RGBFB_R8G8B8A8:
	case RGBFB_B8G8R8A8:
	    return 4;

	case RGBFB_B8G8R8:
	case RGBFB_R8G8B8:
	    return 3;

	case RGBFB_R5G5B5:
	case RGBFB_R5G6B5:
	case RGBFB_R5G6B5PC:
	case RGBFB_R5G5B5PC:
	case RGBFB_B5G6R5PC:
	case RGBFB_B5G5R5PC:
	    return 2;
    }
    return 0;
}

/*
* Amiga <-> native structure conversion functions
*/

static int CopyRenderInfoStructureA2U (uaecptr amigamemptr, struct RenderInfo *ri)
{
    uaecptr memp = get_long (amigamemptr + PSSO_RenderInfo_Memory);

    if (valid_address (memp, PSSO_RenderInfo_sizeof)) {
	ri->Memory = get_real_address (memp);
	ri->BytesPerRow = get_word (amigamemptr + PSSO_RenderInfo_BytesPerRow);
	ri->RGBFormat = get_long (amigamemptr + PSSO_RenderInfo_RGBFormat);
	return 1;
    }
    write_log ("ERROR - Invalid RenderInfo memory area...\n");
    return 0;
}

static int CopyPatternStructureA2U (uaecptr amigamemptr, struct Pattern *pattern)
{
    uaecptr memp = get_long (amigamemptr + PSSO_Pattern_Memory);
    if (valid_address (memp, PSSO_Pattern_sizeof)) {
	pattern->Memory = get_real_address (memp);
	pattern->XOffset = get_word (amigamemptr + PSSO_Pattern_XOffset);
	pattern->YOffset = get_word (amigamemptr + PSSO_Pattern_YOffset);
	pattern->FgPen = get_long (amigamemptr + PSSO_Pattern_FgPen);
	pattern->BgPen = get_long (amigamemptr + PSSO_Pattern_BgPen);
	pattern->Size = get_byte (amigamemptr + PSSO_Pattern_Size);
	pattern->DrawMode = get_byte (amigamemptr + PSSO_Pattern_DrawMode);
	return 1;
    }
    write_log ("ERROR - Invalid Pattern memory area...\n");
    return 0;
}

static void CopyColorIndexMappingA2U (uaecptr amigamemptr, struct ColorIndexMapping *cim, int Bpp)
{
    int i;
    cim->ColorMask = get_long (amigamemptr);
    for (i = 0; i < 256; i++, amigamemptr += 4) {
	uae_u32 v = get_long (amigamemptr + 4);
	endianswap (&v, Bpp);
	cim->Colors[i] = v;
    }
}

static int CopyBitMapStructureA2U (uaecptr amigamemptr, struct BitMap *bm)
{
    int i;

    bm->BytesPerRow = get_word (amigamemptr + PSSO_BitMap_BytesPerRow);
    bm->Rows = get_word (amigamemptr + PSSO_BitMap_Rows);
    bm->Flags = get_byte (amigamemptr + PSSO_BitMap_Flags);
    bm->Depth = get_byte (amigamemptr + PSSO_BitMap_Depth);

    /* ARGH - why is THIS happening? */
    if(bm->Depth > 8)
	bm->Depth = 8;

    for (i = 0; i < bm->Depth; i++) {
	uaecptr plane = get_long (amigamemptr + PSSO_BitMap_Planes + i * 4);
	switch (plane) {
	case 0:
	    bm->Planes[i] = &all_zeros_bitmap;
	    break;
	case 0xFFFFFFFF:
	    bm->Planes[i] = &all_ones_bitmap;
	    break;
	default:
	    if (valid_address (plane, bm->BytesPerRow * bm->Rows))
		bm->Planes[i] = get_real_address (plane);
	    else
		return 0;
	    break;
	}
    }
    return 1;
}

static int CopyTemplateStructureA2U (uaecptr amigamemptr, struct Template *tmpl)
{
    uaecptr memp = get_long (amigamemptr + PSSO_Template_Memory);

    if (valid_address (memp, sizeof(struct Template))) {
	tmpl->Memory = get_real_address (memp);
	tmpl->BytesPerRow = get_word (amigamemptr + PSSO_Template_BytesPerRow);
	tmpl->XOffset = get_byte (amigamemptr + PSSO_Template_XOffset);
	tmpl->DrawMode = get_byte (amigamemptr + PSSO_Template_DrawMode);
	tmpl->FgPen = get_long (amigamemptr + PSSO_Template_FgPen);
	tmpl->BgPen = get_long (amigamemptr + PSSO_Template_BgPen);
	return 1;
    }
    write_log ("ERROR - Invalid Template memory area...\n");
    return 0;
}

static int CopyLineStructureA2U(uaecptr amigamemptr, struct Line *line)
{
    if (valid_address(amigamemptr, sizeof(struct Line))) {
	line->X = get_word (amigamemptr + PSSO_Line_X);
	line->Y = get_word (amigamemptr + PSSO_Line_Y);
	line->Length = get_word (amigamemptr + PSSO_Line_Length);
	line->dX = get_word (amigamemptr + PSSO_Line_dX);
	line->dY = get_word (amigamemptr + PSSO_Line_dY);
	line->lDelta = get_word (amigamemptr + PSSO_Line_lDelta);
	line->sDelta = get_word (amigamemptr + PSSO_Line_sDelta);
	line->twoSDminusLD = get_word (amigamemptr + PSSO_Line_twoSDminusLD);
	line->LinePtrn = get_word (amigamemptr + PSSO_Line_LinePtrn);
	line->PatternShift = get_word (amigamemptr + PSSO_Line_PatternShift);
	line->FgPen = get_long (amigamemptr + PSSO_Line_FgPen);
	line->BgPen = get_long (amigamemptr + PSSO_Line_BgPen);
	line->Horizontal = get_word (amigamemptr + PSSO_Line_Horizontal);
	line->DrawMode = get_byte (amigamemptr + PSSO_Line_DrawMode);
	line->Xorigin = get_word (amigamemptr + PSSO_Line_Xorigin);
	line->Yorigin = get_word (amigamemptr + PSSO_Line_Yorigin);
	return 1;
    }
    write_log ("ERROR - Invalid Line structure...\n");
    return 0;
}

/* list is Amiga address of list, in correct endian format for UAE
* node is Amiga address of node, in correct endian format for UAE */
static void AmigaListAddTail (uaecptr l, uaecptr n)
{
    put_long (n + 0, l + 4); // n->ln_Succ = (struct Node *)&l->lh_Tail;
    put_long (n + 4, get_long (l + 8)); // n->ln_Pred = l->lh_TailPred;
    put_long (get_long (l + 8) + 0, n); // l->lh_TailPred->ln_Succ = n;
    put_long (l + 8, n); // l->lh_TailPred = n;
}

static int renderinfo_is_current_screen (struct RenderInfo *ri)
{
    if (! picasso_on)
	return 0;
    if (ri->Memory != gfxmemory + (picasso96_state.Address - gfxmem_start))
	return 0;
    return 1;
}

/*
* Fill a rectangle in the screen.
*/
static void do_fillrect_frame_buffer (struct RenderInfo *ri, int X, int Y,
				    int Width, int Height, uae_u32 Pen, int Bpp)
{
    int cols;
    uae_u8 *dst;
    int lines;
    int bpr = ri->BytesPerRow;

    dst = ri->Memory + X * Bpp + Y * ri->BytesPerRow;
    endianswap (&Pen, Bpp);
    switch (Bpp)
    {
	case 1:
	    for (lines = 0; lines < Height; lines++, dst += bpr) {
		memset (dst, Pen, Width);
	    }
	break;
	case 2:
	    Pen |= Pen << 16;
	    for (lines = 0; lines < Height; lines++, dst += bpr) {
		uae_u32 *p = (uae_u32*)dst;
		for (cols = 0; cols < Width / 2; cols++)
		    *p++ = Pen;
		if (Width & 1)
		    ((uae_u16*)p)[0] = Pen;
	    }
        break;
	case 3:
	    for (lines = 0; lines < Height; lines++, dst += bpr) {
		uae_u8 *p = (uae_u8*)dst;
		for (cols = 0; cols < Width; cols++) {
		    *p++ = Pen >> 0;
		    *p++ = Pen >> 8;
		    *p++ = Pen >> 16;
		}
	    }
	break;
	case 4:
	    for (lines = 0; lines < Height; lines++, dst += bpr) {
		uae_u32 *p = (uae_u32*)dst;
		for (cols = 0; cols < Width; cols++)
		    *p++ = Pen;
	    }
	break;
    }
}

static void disablemouse (void)
{
    if (cursorsurface)
        IDirectDraw7_Release (cursorsurface);
    cursorsurface = NULL;
    cursordeactivate = 0;
}

static int remcursor_x, remcursor_y, remcursor_w, remcursor_h;
static int newcursor_x, newcursor_y;
static int oldcursor_x, oldcursor_y;
static void mouseupdate (void)
{
    int fx1, fy1, fx2, fy2;
    int x = newcursor_x;
    int y = newcursor_y;
    int forced = 0;

    if (cursordeactivate > 0) {
	cursordeactivate--;
	if (cursordeactivate == 0) {
	    disablemouse ();
	    cursorvisible = 0;
	    oldcursor_x = -10000;
	}
	x = oldcursor_x;
	y = oldcursor_y;
	forced = 1;
    }
    if (oldcursor_x == x && oldcursor_y == y && !forced)
	return;
    oldcursor_x = x;
    oldcursor_y = y;

    if (x <= -cursorwidth)
	x = -cursorwidth;
    if (y <= -cursorheight)
	y = -cursorheight;
    if (x >= picasso96_state.Width)
	x = picasso96_state.Width;
    if (y >= picasso96_state.Height)
	y = picasso96_state.Height;

    fx1 = remcursor_x;
    fy1 = remcursor_y;
    fx2 = fx1 + remcursor_h;
    fy2 = fy1 + remcursor_w;

    remcursor_x = x;
    remcursor_y = y;
    remcursor_w = cursorwidth;
    remcursor_h = cursorheight;

    if (fx2 == fx1 || fy1 == fy2)
	return;

    if (x < fx1)
	fx1 = x;
    if (y < fy1)
	fy1 = y;
    if (x + cursorwidth > fx2)
	fx2 = x + cursorwidth;
    if (y + cursorheight > fy2)
	fy2 = y + cursorheight;

    if (fx1 < 0)
	fx1 = 0;
    if (fy1 < 0)
	fy1 = 0;
    if (fx1 >= picasso96_state.Width)
	fx1 = picasso96_state.Width - 1;
    if (fy1 >= picasso96_state.Height)
	fy1 = picasso96_state.Height - 1;
    fx2 -= fx1;
    fy2 -= fy1;
    if (fx1 + fx2 >= picasso96_state.Width)
	fx2 = picasso96_state.Width - fx1;
    if (fy1 + fy2 >= picasso96_state.Height)
	fy2 = picasso96_state.Height - fy1;
    DX_Invalidate (fx1, fy1, fx2, fy2);
    gfx_unlock_picasso ();
}

static int framecnt;
int p96skipmode = -1;
static int doskip (void)
{
    if (framecnt >= currprefs.gfx_framerate)
	framecnt = 0;
    return framecnt > 0;
}

void picasso_handle_vsync (void)
{
    static int vsynccnt;
    
#ifdef RETROPLATFORM
    rp_vsync ();
#endif

    if (!picasso_on)
	return;

    framecnt++;
    mouseupdate ();
    
    if (currprefs.chipset_refreshrate >= 100) {
	vsynccnt++;
	if (vsynccnt < 2)
	    return;
	vsynccnt = 0;
    }
    if (doskip () && p96skipmode == 0) {
	;
    } else {
	flushpixels ();
    }
    gfx_unlock_picasso ();
}

static int set_panning_called = 0;


typedef enum {

    /* DEST = RGBFB_B8G8R8A8,32 */
    RGBFB_A8R8G8B8_32 = 1,
    RGBFB_A8B8G8R8_32,
    RGBFB_R8G8B8A8_32,
    RGBFB_B8G8R8A8_32,
    RGBFB_R8G8B8_32,
    RGBFB_B8G8R8_32,
    RGBFB_R5G6B5PC_32,
    RGBFB_R5G5B5PC_32,
    RGBFB_R5G6B5_32,
    RGBFB_R5G5B5_32,
    RGBFB_B5G6R5PC_32,
    RGBFB_B5G5R5PC_32,
    RGBFB_CLUT_RGBFB_32,

    /* DEST = RGBFB_R5G6B5PC,16 */
    RGBFB_A8R8G8B8_16,
    RGBFB_A8B8G8R8_16,
    RGBFB_R8G8B8A8_16,
    RGBFB_B8G8R8A8_16,
    RGBFB_R8G8B8_16,
    RGBFB_B8G8R8_16,
    RGBFB_R5G6B5PC_16,
    RGBFB_R5G5B5PC_16,
    RGBFB_R5G6B5_16,
    RGBFB_R5G5B5_16,
    RGBFB_B5G6R5PC_16,
    RGBFB_B5G5R5PC_16,
    RGBFB_CLUT_RGBFB_16,

    /* DEST = RGBFB_CLUT,8 */
    RGBFB_CLUT_8
};

static void setconvert (void)
{
    int d = picasso_vidinfo.pixbytes;
    int v;

    v = 0;
    switch (picasso96_state.RGBFormat)
    {
	case RGBFB_CLUT:
	if (d == 1)
	    v = RGBFB_CLUT_8;
	else if (d == 2)
	    v = RGBFB_CLUT_RGBFB_16;
	else if (d == 4)
	    v = RGBFB_CLUT_RGBFB_32;
	break;

	case RGBFB_B5G6R5PC:
	if (d == 2)
	    v = RGBFB_B5G6R5PC_16;
	else if (d == 4)
	    v = RGBFB_B5G6R5PC_32;
	break;
	case RGBFB_R5G6B5PC:
	if (d == 2)
	    v = RGBFB_R5G6B5PC_16;
	else if (d == 4)
	    v = RGBFB_R5G6B5PC_32;
	break;

	case RGBFB_R5G5B5PC:
	if (d == 4)
	    v = RGBFB_R5G5B5PC_32;
	else if (d == 2)
	    v = RGBFB_R5G5B5PC_16;
	break;
	case RGBFB_R5G6B5:
	if (d == 4)
	    v = RGBFB_R5G6B5_32;
	break;
	case RGBFB_R5G5B5:
	if (d == 4)
	    v = RGBFB_R5G5B5_32;
	break;
	case RGBFB_B5G5R5PC:
	if (d == 4)
	    v = RGBFB_B5G5R5PC_32;
	break;


	case RGBFB_R8G8B8:
	if (d == 4)
	    v = RGBFB_R8G8B8_32;
	break;
	case RGBFB_B8G8R8:
	if (d == 4)
	    v = RGBFB_B8G8R8_32;
	break;
	case RGBFB_A8B8G8R8:
	if (d == 2)
	    v = RGBFB_A8B8G8R8_16;
	else if (d == 4)
	    v = RGBFB_A8B8G8R8_32;
	break;
	case RGBFB_B8G8R8A8:
	if (d == 2)
	    v = RGBFB_B8G8R8A8_16;
	else if (d == 4)
	    v = RGBFB_B8G8R8A8_32;
	break;
    }
    picasso_convert = v;
    host_mode = DirectDraw_GetSurfacePixelFormat (NULL);
    write_log ("RTG conversion: Depth=%d HostRGBF=%d P96RGBF=%d Mode=%d\n", d, host_mode, picasso96_state.RGBFormat, v);
}

/* Clear our screen, since we've got a new Picasso screen-mode, and refresh with the proper contents
* This is called on several occasions:
* 1. Amiga-->Picasso transition, via SetSwitch()
* 2. Picasso-->Picasso transition, via SetPanning().
* 3. whenever the graphics code notifies us that the screen contents have been lost.
*/
extern unsigned int new_beamcon0;
void picasso_refresh (void)
{
    struct RenderInfo ri;
    static int beamcon0_before, p96refresh_was;

    if (! picasso_on)
	return;
    {  //for higher P96 mousedraw rate
	/* HACK */
	extern uae_u16 vtotal;
	if (p96hack_vpos2) {
	    vtotal = p96hack_vpos2;
	    beamcon0_before = new_beamcon0;
	    new_beamcon0 |= 0x80;
	    p96refresh_active = 1;
	    p96refresh_was = 1;
	} else {
	    if (p96refresh_was) {
		new_beamcon0 = beamcon0_before;
		p96refresh_was = 0;
	    }
	    new_beamcon0 |= 0x20;
	}
		/* HACK until ntsc timing is fixed.. */
    } //end for higher P96 mousedraw rate
    have_done_picasso = 1;
    palette_changed = 1;
    setconvert ();

    /* Make sure that the first time we show a Picasso video mode, we don't blit any crap.
    * We can do this by checking if we have an Address yet.  */
    if (picasso96_state.Address) {
	unsigned int width, height;

	/* blit the stuff from our static frame-buffer to the gfx-card */
	ri.Memory = gfxmemory + (picasso96_state.Address - gfxmem_start);
	ri.BytesPerRow = picasso96_state.BytesPerRow;
	ri.RGBFormat = picasso96_state.RGBFormat;

	if (set_panning_called) {
	    width = (picasso96_state.VirtualWidth < picasso96_state.Width) ?
		picasso96_state.VirtualWidth : picasso96_state.Width;
	    height = (picasso96_state.VirtualHeight < picasso96_state.Height) ?
		picasso96_state.VirtualHeight : picasso96_state.Height;
	    // Let's put a black-border around the case where we've got a sub-screen...
	    if (!picasso96_state.BigAssBitmap) {
		if (picasso96_state.XOffset || picasso96_state.YOffset)
		    DX_Fill (0, 0, picasso96_state.Width, picasso96_state.Height, 0);
	    }
	} else {
	    width = picasso96_state.Width;
	    height = picasso96_state.Height;
	}
	flushpixels ();
    } else {
	write_log ("ERROR - picasso_refresh() can't refresh!\n");
    }
}

#define BLT_SIZE 4
#define BLT_MULT 1
#define BLT_NAME BLIT_FALSE_32
#define BLT_FUNC(s,d) *d = 0
#include "p96_blit.c"
#define BLT_NAME BLIT_NOR_32
#define BLT_FUNC(s,d) *d = ~(*s | * d)
#include "p96_blit.c"
#define BLT_NAME BLIT_ONLYDST_32
#define BLT_FUNC(s,d) *d = (*d) & ~(*s)
#include "p96_blit.c"
#define BLT_NAME BLIT_NOTSRC_32
#define BLT_FUNC(s,d) *d = ~(*s)
#include "p96_blit.c" 
#define BLT_NAME BLIT_ONLYSRC_32
#define BLT_FUNC(s,d) *d = (*s) & ~(*d)
#include "p96_blit.c"
#define BLT_NAME BLIT_NOTDST_32
#define BLT_FUNC(s,d) *d = ~(*d)
#include "p96_blit.c"
#define BLT_NAME BLIT_EOR_32
#define BLT_FUNC(s,d) *d = (*s) ^ (*d)
#include "p96_blit.c"
#define BLT_NAME BLIT_NAND_32
#define BLT_FUNC(s,d) *d = ~((*s) & (*d))
#include "p96_blit.c"
#define BLT_NAME BLIT_AND_32
#define BLT_FUNC(s,d) *d = (*s) & (*d)
#include "p96_blit.c"
#define BLT_NAME BLIT_NEOR_32
#define BLT_FUNC(s,d) *d = ~((*s) ^ (*d))
#include "p96_blit.c"
#define BLT_NAME BLIT_NOTONLYSRC_32
#define BLT_FUNC(s,d) *d = ~(*s) | (*d)
#include "p96_blit.c"
#define BLT_NAME BLIT_NOTONLYDST_32
#define BLT_FUNC(s,d) *d = ~(*d) | (*s)
#include "p96_blit.c"
#define BLT_NAME BLIT_OR_32
#define BLT_FUNC(s,d) *d = (*s) | (*d)
#include "p96_blit.c"
#define BLT_NAME BLIT_TRUE_32
#define BLT_FUNC(s,d) *d = 0xffffffff
#include "p96_blit.c"
#define BLT_NAME BLIT_30_32
#define BLT_FUNC(s,d) tmp = *d ; *d = *s; *s = tmp;
#define BLT_TEMP
#include "p96_blit.c"
#undef BLT_SIZE
#undef BLT_MULT

#define BLT_SIZE 3
#define BLT_MULT 1
#define BLT_NAME BLIT_FALSE_24
#define BLT_FUNC(s,d) *d = 0
#include "p96_blit.c"
#define BLT_NAME BLIT_NOR_24
#define BLT_FUNC(s,d) *d = ~(*s | * d)
#include "p96_blit.c"
#define BLT_NAME BLIT_ONLYDST_24
#define BLT_FUNC(s,d) *d = (*d) & ~(*s)
#include "p96_blit.c"
#define BLT_NAME BLIT_NOTSRC_24
#define BLT_FUNC(s,d) *d = ~(*s)
#include "p96_blit.c" 
#define BLT_NAME BLIT_ONLYSRC_24
#define BLT_FUNC(s,d) *d = (*s) & ~(*d)
#include "p96_blit.c"
#define BLT_NAME BLIT_NOTDST_24
#define BLT_FUNC(s,d) *d = ~(*d)
#include "p96_blit.c"
#define BLT_NAME BLIT_EOR_24
#define BLT_FUNC(s,d) *d = (*s) ^ (*d)
#include "p96_blit.c"
#define BLT_NAME BLIT_NAND_24
#define BLT_FUNC(s,d) *d = ~((*s) & (*d))
#include "p96_blit.c"
#define BLT_NAME BLIT_AND_24
#define BLT_FUNC(s,d) *d = (*s) & (*d)
#include "p96_blit.c"
#define BLT_NAME BLIT_NEOR_24
#define BLT_FUNC(s,d) *d = ~((*s) ^ (*d))
#include "p96_blit.c"
#define BLT_NAME BLIT_NOTONLYSRC_24
#define BLT_FUNC(s,d) *d = ~(*s) | (*d)
#include "p96_blit.c"
#define BLT_NAME BLIT_NOTONLYDST_24
#define BLT_FUNC(s,d) *d = ~(*d) | (*s)
#include "p96_blit.c"
#define BLT_NAME BLIT_OR_24
#define BLT_FUNC(s,d) *d = (*s) | (*d)
#include "p96_blit.c"
#define BLT_NAME BLIT_TRUE_24
#define BLT_FUNC(s,d) *d = 0xffffffff
#include "p96_blit.c"
#define BLT_NAME BLIT_30_24
#define BLT_FUNC(s,d) tmp = *d ; *d = *s; *s = tmp;
#define BLT_TEMP
#include "p96_blit.c"
#undef BLT_SIZE
#undef BLT_MULT

#define BLT_SIZE 2
#define BLT_MULT 2
#define BLT_NAME BLIT_FALSE_16
#define BLT_FUNC(s,d) *d = 0
#include "p96_blit.c"
#define BLT_NAME BLIT_NOR_16
#define BLT_FUNC(s,d) *d = ~(*s | * d)
#include "p96_blit.c"
#define BLT_NAME BLIT_ONLYDST_16
#define BLT_FUNC(s,d) *d = (*d) & ~(*s)
#include "p96_blit.c"
#define BLT_NAME BLIT_NOTSRC_16
#define BLT_FUNC(s,d) *d = ~(*s)
#include "p96_blit.c" 
#define BLT_NAME BLIT_ONLYSRC_16
#define BLT_FUNC(s,d) *d = (*s) & ~(*d)
#include "p96_blit.c"
#define BLT_NAME BLIT_NOTDST_16
#define BLT_FUNC(s,d) *d = ~(*d)
#include "p96_blit.c"
#define BLT_NAME BLIT_EOR_16
#define BLT_FUNC(s,d) *d = (*s) ^ (*d)
#include "p96_blit.c"
#define BLT_NAME BLIT_NAND_16
#define BLT_FUNC(s,d) *d = ~((*s) & (*d))
#include "p96_blit.c"
#define BLT_NAME BLIT_AND_16
#define BLT_FUNC(s,d) *d = (*s) & (*d)
#include "p96_blit.c"
#define BLT_NAME BLIT_NEOR_16
#define BLT_FUNC(s,d) *d = ~((*s) ^ (*d))
#include "p96_blit.c"
#define BLT_NAME BLIT_NOTONLYSRC_16
#define BLT_FUNC(s,d) *d = ~(*s) | (*d)
#include "p96_blit.c"
#define BLT_NAME BLIT_NOTONLYDST_16
#define BLT_FUNC(s,d) *d = ~(*d) | (*s)
#include "p96_blit.c"
#define BLT_NAME BLIT_OR_16
#define BLT_FUNC(s,d) *d = (*s) | (*d)
#include "p96_blit.c"
#define BLT_NAME BLIT_TRUE_16
#define BLT_FUNC(s,d) *d = 0xffffffff
#include "p96_blit.c"
#define BLT_NAME BLIT_30_16
#define BLT_FUNC(s,d) tmp = *d ; *d = *s; *s = tmp;
#define BLT_TEMP
#include "p96_blit.c"
#undef BLT_SIZE
#undef BLT_MULT

#define BLT_SIZE 1
#define BLT_MULT 4
#define BLT_NAME BLIT_FALSE_8
#define BLT_FUNC(s,d) *d = 0
#include "p96_blit.c"
#define BLT_NAME BLIT_NOR_8
#define BLT_FUNC(s,d) *d = ~(*s | * d)
#include "p96_blit.c"
#define BLT_NAME BLIT_ONLYDST_8
#define BLT_FUNC(s,d) *d = (*d) & ~(*s)
#include "p96_blit.c"
#define BLT_NAME BLIT_NOTSRC_8
#define BLT_FUNC(s,d) *d = ~(*s)
#include "p96_blit.c" 
#define BLT_NAME BLIT_ONLYSRC_8
#define BLT_FUNC(s,d) *d = (*s) & ~(*d)
#include "p96_blit.c"
#define BLT_NAME BLIT_NOTDST_8
#define BLT_FUNC(s,d) *d = ~(*d)
#include "p96_blit.c"
#define BLT_NAME BLIT_EOR_8
#define BLT_FUNC(s,d) *d = (*s) ^ (*d)
#include "p96_blit.c"
#define BLT_NAME BLIT_NAND_8
#define BLT_FUNC(s,d) *d = ~((*s) & (*d))
#include "p96_blit.c"
#define BLT_NAME BLIT_AND_8
#define BLT_FUNC(s,d) *d = (*s) & (*d)
#include "p96_blit.c"
#define BLT_NAME BLIT_NEOR_8
#define BLT_FUNC(s,d) *d = ~((*s) ^ (*d))
#include "p96_blit.c"
#define BLT_NAME BLIT_NOTONLYSRC_8
#define BLT_FUNC(s,d) *d = ~(*s) | (*d)
#include "p96_blit.c"
#define BLT_NAME BLIT_NOTONLYDST_8
#define BLT_FUNC(s,d) *d = ~(*d) | (*s)
#include "p96_blit.c"
#define BLT_NAME BLIT_OR_8
#define BLT_FUNC(s,d) *d = (*s) | (*d)
#include "p96_blit.c"
#define BLT_NAME BLIT_TRUE_8
#define BLT_FUNC(s,d) *d = 0xffffffff
#include "p96_blit.c"
#define BLT_NAME BLIT_30_8
#define BLT_FUNC(s,d) tmp = *d ; *d = *s; *s = tmp;
#define BLT_TEMP
#include "p96_blit.c"
#undef BLT_SIZE
#undef BLT_MULT

#define PARMS width, height, src, dst, ri->BytesPerRow, dstri->BytesPerRow

/*
* Functions to perform an action on the frame-buffer
*/
static int do_blitrect_frame_buffer (struct RenderInfo *ri, struct
    RenderInfo *dstri, unsigned long srcx, unsigned long srcy,
    unsigned long dstx, unsigned long dsty, unsigned long width, unsigned
    long height, uae_u8 mask, BLIT_OPCODE opcode)
{
    uae_u8 *src, *dst;
    uae_u8 Bpp = GetBytesPerPixel (ri->RGBFormat);
    unsigned long total_width = width * Bpp;

    src = ri->Memory + srcx * Bpp + srcy * ri->BytesPerRow;
    dst = dstri->Memory + dstx * Bpp + dsty * dstri->BytesPerRow;
    if (mask != 0xFF && Bpp > 1) {
	write_log ("WARNING - BlitRect() has mask 0x%x with Bpp %d.\n", mask, Bpp);
    }

    P96TRACE (("(%dx%d)=(%dx%d)=(%dx%d)=%d\n", srcx, srcy, dstx, dsty, width, height, opcode));
    if (mask == 0xFF || Bpp > 1) {

	if(opcode == BLIT_SRC) {
	    /* handle normal case efficiently */
	    if (ri->Memory == dstri->Memory && dsty == srcy) {
		unsigned long i;
		for (i = 0; i < height; i++, src += ri->BytesPerRow, dst += dstri->BytesPerRow)
		    memmove (dst, src, total_width);
	    } else if (dsty < srcy) {
		unsigned long i;
		for (i = 0; i < height; i++, src += ri->BytesPerRow, dst += dstri->BytesPerRow)
		    memcpy (dst, src, total_width);
	    } else {
		unsigned long i;
		src += (height - 1) * ri->BytesPerRow;
		dst += (height - 1) * dstri->BytesPerRow;
		for (i = 0; i < height; i++, src -= ri->BytesPerRow, dst -= dstri->BytesPerRow)
		    memcpy (dst, src, total_width);
	    }
	    return 1;

	} else if (Bpp == 4) {

	    /* 32-bit optimized */
	    switch (opcode)
	    {
	        case BLIT_FALSE: BLIT_FALSE_32 (PARMS); break;
	        case BLIT_NOR: BLIT_NOR_32 (PARMS); break;
	        case BLIT_ONLYDST: BLIT_ONLYDST_32 (PARMS); break;
	        case BLIT_NOTSRC: BLIT_NOTSRC_32 (PARMS); break;
	        case BLIT_ONLYSRC: BLIT_ONLYSRC_32 (PARMS); break;
	        case BLIT_NOTDST: BLIT_NOTDST_32 (PARMS); break;
	        case BLIT_EOR: BLIT_EOR_32 (PARMS); break;
	        case BLIT_NAND: BLIT_NAND_32 (PARMS); break;
	        case BLIT_AND: BLIT_AND_32 (PARMS); break;
	        case BLIT_NEOR: BLIT_NEOR_32 (PARMS); break;
	        case BLIT_NOTONLYSRC: BLIT_NOTONLYSRC_32 (PARMS); break;
	        case BLIT_NOTONLYDST: BLIT_NOTONLYDST_32 (PARMS); break;
	        case BLIT_OR: BLIT_OR_32 (PARMS); break;
	        case BLIT_TRUE: BLIT_TRUE_32 (PARMS); break;
	        case 30: BLIT_30_32 (PARMS); break;
	    }
	} else if (Bpp == 3) {

	    /* 24-bit (not very) optimized */
	    switch (opcode)
	    {
	        case BLIT_FALSE: BLIT_FALSE_24 (PARMS); break;
	        case BLIT_NOR: BLIT_NOR_24 (PARMS); break;
	        case BLIT_ONLYDST: BLIT_ONLYDST_24 (PARMS); break;
	        case BLIT_NOTSRC: BLIT_NOTSRC_24 (PARMS); break;
	        case BLIT_ONLYSRC: BLIT_ONLYSRC_24 (PARMS); break;
	        case BLIT_NOTDST: BLIT_NOTDST_24 (PARMS); break;
	        case BLIT_EOR: BLIT_EOR_24 (PARMS); break;
	        case BLIT_NAND: BLIT_NAND_24 (PARMS); break;
	        case BLIT_AND: BLIT_AND_24 (PARMS); break;
	        case BLIT_NEOR: BLIT_NEOR_24 (PARMS); break;
	        case BLIT_NOTONLYSRC: BLIT_NOTONLYSRC_24 (PARMS); break;
	        case BLIT_NOTONLYDST: BLIT_NOTONLYDST_24 (PARMS); break;
	        case BLIT_OR: BLIT_OR_24 (PARMS); break;
	        case BLIT_TRUE: BLIT_TRUE_24 (PARMS); break;
	        case 30: BLIT_30_24 (PARMS); break;
	    }
	
	} else if (Bpp == 2) {

	    /* 16-bit optimized */
	    switch (opcode)
	    {
	        case BLIT_FALSE: BLIT_FALSE_16 (PARMS); break;
	        case BLIT_NOR: BLIT_NOR_16 (PARMS); break;
	        case BLIT_ONLYDST: BLIT_ONLYDST_16 (PARMS); break;
	        case BLIT_NOTSRC: BLIT_NOTSRC_16 (PARMS); break;
	        case BLIT_ONLYSRC: BLIT_ONLYSRC_16 (PARMS); break;
	        case BLIT_NOTDST: BLIT_NOTDST_16 (PARMS); break;
	        case BLIT_EOR: BLIT_EOR_16 (PARMS); break;
	        case BLIT_NAND: BLIT_NAND_16 (PARMS); break;
	        case BLIT_AND: BLIT_AND_16 (PARMS); break;
	        case BLIT_NEOR: BLIT_NEOR_16 (PARMS); break;
	        case BLIT_NOTONLYSRC: BLIT_NOTONLYSRC_16 (PARMS); break;
	        case BLIT_NOTONLYDST: BLIT_NOTONLYDST_16 (PARMS); break;
	        case BLIT_OR: BLIT_OR_16 (PARMS); break;
	        case BLIT_TRUE: BLIT_TRUE_16 (PARMS); break;
	        case 30: BLIT_30_16 (PARMS); break;
	    }

	} else if (Bpp == 1) {

	    /* 8-bit optimized */
	    switch (opcode)
	    {
	        case BLIT_FALSE: BLIT_FALSE_8 (PARMS); break;
	        case BLIT_NOR: BLIT_NOR_8 (PARMS); break;
	        case BLIT_ONLYDST: BLIT_ONLYDST_8 (PARMS); break;
	        case BLIT_NOTSRC: BLIT_NOTSRC_8 (PARMS); break;
	        case BLIT_ONLYSRC: BLIT_ONLYSRC_8 (PARMS); break;
	        case BLIT_NOTDST: BLIT_NOTDST_8 (PARMS); break;
	        case BLIT_EOR: BLIT_EOR_8 (PARMS); break;
	        case BLIT_NAND: BLIT_NAND_8 (PARMS); break;
	        case BLIT_AND: BLIT_AND_8 (PARMS); break;
	        case BLIT_NEOR: BLIT_NEOR_8 (PARMS); break;
	        case BLIT_NOTONLYSRC: BLIT_NOTONLYSRC_8 (PARMS); break;
	        case BLIT_NOTONLYDST: BLIT_NOTONLYDST_8 (PARMS); break;
	        case BLIT_OR: BLIT_OR_8 (PARMS); break;
	        case BLIT_TRUE: BLIT_TRUE_8 (PARMS); break;
	        case 30: BLIT_30_8 (PARMS); break;
	    }

	}
	return 1;
    }
    return 0;
}

/*
SetSpritePosition:
Synopsis: SetSpritePosition(bi, RGBFormat);
Inputs: a0: struct BoardInfo *bi
d7: RGBFTYPE RGBFormat

*/
static uae_u32 REGPARAM2 picasso_SetSpritePosition (TrapContext *ctx)
{
    struct regstruct *regs = &ctx->regs;
    uaecptr bi = m68k_areg (regs, 0);
    newcursor_x = (uae_s16)get_word (bi + PSSO_BoardInfo_MouseX) - picasso96_state.XOffset;
    newcursor_y = (uae_s16)get_word (bi + PSSO_BoardInfo_MouseY) - picasso96_state.YOffset;
    if (!hwsprite)
	return 0;
    return 1;
}


/*
SetSpriteColor:
Synopsis: SetSpriteColor(bi, index, red, green, blue, RGBFormat);
Inputs: a0: struct BoardInfo *bi
d0.b: index
d1.b: red
d2.b: green
d3.b: blue
d7: RGBFTYPE RGBFormat

This function changes one of the possible three colors of the hardware sprite.
*/
static uae_u32 REGPARAM2 picasso_SetSpriteColor (TrapContext *ctx)
{
    struct regstruct *regs = &ctx->regs;
    uaecptr bi = m68k_areg (regs, 0);
    uae_u8 idx = m68k_dreg (regs, 0);
    uae_u8 red = m68k_dreg (regs, 1);
    uae_u8 green = m68k_dreg (regs, 2);
    uae_u8 blue = m68k_dreg (regs, 3);
    idx++;
    if (!hwsprite)
	return 0;
    if (idx >= 4)
	return 0;
    cursorrgb[idx] = (red << 16) | (green << 8) | (blue << 0);
    P96TRACE_SPR (("SetSpriteColor(%08x,%d:%02X%02X%02X). %x\n", bi, idx, red, green, blue, bi + PSSO_BoardInfo_MousePens));
    return 1;
}


static uae_u32 setspriteimage (uaecptr bi);
static RECT cursor_r1, cursor_r2;
static int mouseput;
static void recursor (HRESULT ddrval)
{
    if (ddrval == DDERR_SURFACELOST || ddrval == DDERR_DEVICEDOESNTOWNSURFACE) {
	reloadcursor = 1;
        setspriteimage (cursorbi);
    }
}
void picasso_putcursor (int sx, int sy, int sw, int sh)
{
    int xdiff, ydiff, xdiff2, ydiff2;
    DWORD ddrval;
    LPDIRECTDRAWSURFACE7 dstsurf = dxdata.secondary;
    
    if (cursorsurface == NULL || !cursorvisible || !hwsprite)
	return;

    if (remcursor_x + remcursor_w < sx)
	return;
    if (remcursor_y + remcursor_h < sy)
	return;
    if (remcursor_x > sx + sw)
	return;
    if (remcursor_y > sy + sh)
	return;
    if (remcursor_w == 0 || remcursor_h == 0)
	return;

    cursor_r1.left = remcursor_x;
    cursor_r1.top = remcursor_y;
    cursor_r1.right = cursor_r1.left + remcursor_w;
    cursor_r1.bottom = cursor_r1.top + remcursor_h;

    xdiff = ydiff = 0;
    xdiff2 = ydiff2 = 0;
    if (cursor_r1.right > picasso96_state.Width)
	xdiff = picasso96_state.Width - cursor_r1.right;
    if (cursor_r1.bottom > picasso96_state.Height)
	ydiff = picasso96_state.Height - cursor_r1.bottom;
    if (xdiff <= -cursorwidth)
	return;
    if (ydiff <= -cursorheight)
	return;

    if (cursor_r1.left < 0) {
	xdiff2 = cursor_r1.left;
	cursor_r1.left = 0;
    }
    if (cursor_r1.top < 0) {
	ydiff2 = cursor_r1.top;
	cursor_r1.top = 0;
    }

    cursor_r1.right += xdiff;
    cursor_r1.bottom += ydiff;

    cursor_r2.left = 0;
    cursor_r2.top = cursorheight;
    cursor_r2.right = cursorwidth + xdiff + xdiff2;
    cursor_r2.bottom = cursorheight + cursorheight + ydiff + ydiff2;

    ddrval = IDirectDrawSurface7_Blt (cursorsurface, &cursor_r2, dstsurf, &cursor_r1, DDBLT_WAIT, NULL);
    if (FAILED(ddrval)) {
	write_log ("Cursor surface blit1 failed: %s\n", DXError (ddrval));
	recursor (ddrval);
	return;
    }

    cursor_r2.left = -xdiff2;
    cursor_r2.top = -ydiff2;
    cursor_r2.right = cursor_r2.left + cursorwidth + xdiff + xdiff2;
    cursor_r2.bottom = cursor_r2.top + cursorheight + ydiff + ydiff2;

    ddrval = IDirectDrawSurface7_Blt (dstsurf, &cursor_r1, cursorsurface, &cursor_r2, DDBLT_WAIT | DDBLT_KEYSRC, NULL);
    if (FAILED(ddrval)) {
	write_log ("Cursor surface blit2 failed: %s\n", DXError (ddrval));
	recursor (ddrval);
	return;
    }

    cursor_r2.left = 0;
    cursor_r2.top = cursorheight;
    cursor_r2.right = cursorwidth + xdiff + xdiff2;
    cursor_r2.bottom = cursorheight + cursorheight + ydiff + ydiff2;

    mouseput = 1;
}
void picasso_clearcursor (void)
{
    DWORD ddrval;
    LPDIRECTDRAWSURFACE7 dstsurf = dxdata.secondary;

    if (cursorsurface == NULL)
	return;
    if (!mouseput)
	return;
    mouseput = 0;

    ddrval = IDirectDrawSurface7_Blt (dstsurf, &cursor_r1, cursorsurface, &cursor_r2, DDBLT_WAIT, NULL);
    if (FAILED(ddrval))
	write_log ("Cursor surface clearblit failed: %s\n", DXError (ddrval));
}

STATIC_INLINE uae_u16 rgb32torgb16pc (uae_u32 rgb)
{
    return (((rgb >> (16 + 3)) & 0x1f) << 11) | (((rgb >> (8 + 2)) & 0x3f) << 5) | (((rgb >> (0 + 3)) & 0x1f) << 0);
}

static void updatesprcolors (void)
{
    int i;
    for (i = 1; i < 4; i++) {
	uae_u32 v, vx;
	v = cursorrgb[i];
	switch (picasso_vidinfo.pixbytes)
	{
	    case 1:
	    cursorrgbn[0] = 16;
	     /* use custom chip sprite palette */
	    cursorrgbn[i] = i + 16;
	    break;
	    case 2:
	    vx = 0xff00ff; /* RGB -> B5G6R5 */
	    cursorrgbn[0] = rgb32torgb16pc (vx);
	    cursorrgbn[i] = rgb32torgb16pc (v);
	    break;
	    case 4:
	    cursorrgbn[0] = 0xff00ff;
	    cursorrgbn[i] = v;
	    break;
	}
    }
}

static void putmousepixel (uae_u8 *d, int bpp, int idx)
{
    uae_u32 val = cursorrgbn[idx];
    switch (bpp)
    {
	case 1:
	((uae_u8*)d)[0] = (uae_u8)val;
	break;
	case 2:
	((uae_u16*)d)[0] = (uae_u16)val;
	break;
	case 4:
	((uae_u32*)d)[0] = (uae_u32)val;
	break;
    }
}

static uae_u32 setspriteimage (uaecptr bi)
{
    DDSURFACEDESC2 desc;
    uae_u32 flags;
    int x, y, yy, bits, bpp;
    DWORD ddrval;
    uae_u8 *dptr;
    uae_u8 *tmpbuf;
    DWORD pitch;
    int hiressprite, doubledsprite;
    int ret = 0;
    int w, h;

    cursordeactivate = 0;
    if (!hwsprite)
	return 0;
    oldcursor_x = -10000;
    w = (uae_u32)get_byte (bi + PSSO_BoardInfo_MouseWidth);
    h = (uae_u32)get_byte (bi + PSSO_BoardInfo_MouseHeight);
    tmpbuf = NULL;
    flags = get_long (bi + PSSO_BoardInfo_Flags);
    hiressprite = 1;
    doubledsprite = 0;
    if (flags & BIF_HIRESSPRITE)
	hiressprite = 2;
    if (flags & BIF_BIGSPRITE)
	doubledsprite = 1;
    updatesprcolors ();

    P96TRACE_SPR (("SetSpriteImage(%08x,%08x,w=%d,h=%d,%d/%d,%08x)\n",
	bi, get_long (bi + PSSO_BoardInfo_MouseImage), w, h,
	hiressprite - 1, doubledsprite, bi + PSSO_BoardInfo_MouseImage));

    bpp = picasso_vidinfo.pixbytes;
    if (!w || !h || get_long (bi + PSSO_BoardInfo_MouseImage) == 0) {
	cursordeactivate = 1;
	ret = 1;
	goto end;
    }

    tmpbuf = xmalloc (w * h * bpp);
    yy = 0;
    for (y = 0; y < h; y++, yy++) {
        uae_u8 *p = tmpbuf + w * bpp * y;
	uae_u8 *pprev = p;
	uaecptr img = get_long (bi + PSSO_BoardInfo_MouseImage) + 4 + yy * 4 * hiressprite;
	x = 0;
	while (x < w) {
	    uae_u32 d1 = get_long (img);
	    uae_u32 d2 = get_long (img + 2 * hiressprite);
	    int maxbits = w - x;
	    if (maxbits > 16 * hiressprite)
		maxbits = 16 * hiressprite;
	    for (bits = 0; bits < maxbits && x < w; bits++) {
		uae_u8 c = ((d2 & 0x80000000) ? 2 : 0) + ((d1 & 0x80000000) ? 1 : 0);
		d1 <<= 1;
		d2 <<= 1;
		putmousepixel (p, bpp, c);
		p += bpp;
		x++;
		if (doubledsprite && x < w) {
		    putmousepixel (p, bpp, c);
		    p += bpp;
		    x++;
		}
	    }
	}
	if (doubledsprite && y < h) {
	    y++;
	    memcpy (p, pprev, w * bpp);
	}
    }

    if (cursorsurface) {
	for (;;) {
	    if (!reloadcursor && w == cursorwidth && h == cursorheight) {
		int different = 0;
		desc.dwSize = sizeof (desc);
		ddrval = DD_OK;
		while (FAILED (ddrval = IDirectDrawSurface7_Lock (cursorsurface, NULL, &desc, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL))) {
		    if (ddrval == DDERR_SURFACELOST) {
			ddrval = IDirectDrawSurface7_Restore (cursorsurface);
			if (FAILED (ddrval))
			    break;
		    }
		}
		if (FAILED (ddrval))
		    break;
		dptr = (uae_u8*)desc.lpSurface;
		pitch = desc.lPitch;
		for (y = 0; y < h; y++) {
		    uae_u8 *p1 = tmpbuf + w * bpp * y;
		    uae_u8 *p2 = dptr + pitch * y;
		    if (memcmp (p1, p2, w * bpp))
			different = 1;
		}
		IDirectDrawSurface_Unlock (cursorsurface, NULL);
		if (!different) {
		    P96TRACE_SPR (("sprite was same as previously\n"));
		    ret = 1;
		    goto end;
		}
	    }
	    break;
	}
	IDirectDraw7_Release (cursorsurface);
    }

    cursorwidth = w;
    cursorheight = h;
    cursorsurface = NULL;

    ZeroMemory (&desc, sizeof desc);
    desc.dwSize = sizeof desc;
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_CKSRCBLT;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    desc.dwWidth = w;
    desc.dwHeight = h * 2;
    DirectDraw_GetPrimaryPixelFormat (&desc);
    desc.ddckCKSrcBlt.dwColorSpaceLowValue = cursorrgbn[0];
    desc.ddckCKSrcBlt.dwColorSpaceHighValue = cursorrgbn[0];
    ddrval = IDirectDraw7_CreateSurface (dxdata.maindd, &desc, &cursorsurface, NULL);
    if (FAILED(ddrval)) {
	write_log ("Cursor surface creation failed: %s\n", DXError (ddrval));
	return 0;
    }
    desc.dwSize = sizeof (desc);
    while (FAILED(ddrval = IDirectDrawSurface7_Lock (cursorsurface, NULL, &desc, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL))) {
	if (ddrval == DDERR_SURFACELOST) {
	    ddrval = IDirectDrawSurface7_Restore (cursorsurface);
	    if (FAILED(ddrval)) {
		write_log ("sprite surface failed to restore: %s\n", DXError (ddrval));
	        goto end;
	    }
	}
    }
    dptr = (uae_u8*)desc.lpSurface;
    pitch = desc.lPitch;
    for (y = 0; y < h; y++) {
	uae_u8 *p1 = tmpbuf + w * bpp * y;
	uae_u8 *p2 = dptr + pitch * y;
	memcpy (p2, p1, w * bpp);
    }
    IDirectDrawSurface_Unlock (cursorsurface, NULL);
    ret = 1;
    reloadcursor = 0;
    P96TRACE_SPR (("hardware sprite created\n"));
end:
    xfree (tmpbuf);
    return ret;
}

/*
SetSpriteImage:
Synopsis: SetSpriteImage(bi, RGBFormat);
Inputs: a0: struct BoardInfo *bi
d7: RGBFTYPE RGBFormat

This function gets new sprite image data from the MouseImage field of the BoardInfo structure and writes
it to the board.

There are three possible cases:

BIB_HIRESSPRITE is set:
skip the first two long words and the following sprite data is arranged as an array of two longwords. Those form the
two bit planes for one image line respectively.

BIB_HIRESSPRITE and BIB_BIGSPRITE are not set:
skip the first two words and the following sprite data is arranged as an array of two words. Those form the two
bit planes for one image line respectively.

BIB_HIRESSPRITE is not set and BIB_BIGSPRITE is set:
skip the first two words and the following sprite data is arranged as an array of two words. Those form the two bit
planes for one image line respectively. You have to double each pixel horizontally and vertically. All coordinates
used in this case already assume a zoomed sprite, only the sprite data is not zoomed yet. You will have to
compensate for this when accounting for hotspot offsets and sprite dimensions.
*/
static uae_u32 REGPARAM2 picasso_SetSpriteImage (TrapContext *ctx)
{
    struct regstruct *regs = &ctx->regs;
    uaecptr bi = m68k_areg (regs, 0);
    cursorbi = bi;
    return setspriteimage (bi);
}

/*
SetSprite:
Synopsis: SetSprite(bi, activate, RGBFormat);
Inputs: a0: struct BoardInfo *bi
d0: BOOL activate
d7: RGBFTYPE RGBFormat

This function activates or deactivates the hardware sprite.
*/
static uae_u32 REGPARAM2 picasso_SetSprite (TrapContext *ctx)
{
    struct regstruct *regs = &ctx->regs;
    uae_u32 result = 0;
    uae_u32 activate = m68k_dreg (regs, 0);
    if (!hwsprite)
	return 0;
    if (activate) {
	picasso_SetSpriteImage (ctx);
	cursorvisible = 1;
    } else {
	cursordeactivate = 2;
    }
    result = 1;
    P96TRACE_SPR (("SetSprite: %d\n", activate));
    return result;
}

/*
* BOOL FindCard(struct BoardInfo *bi);      and
*
* FindCard is called in the first stage of the board initialisation and
* configuration and is used to look if there is a free and unconfigured
* board of the type the driver is capable of managing. If it finds one,
* it immediately reserves it for use by Picasso96, usually by clearing
* the CDB_CONFIGME bit in the flags field of the ConfigDev struct of
* this expansion card. But this is only a common example, a driver can
* do whatever it wants to mark this card as used by the driver. This
* mechanism is intended to ensure that a board is only configured and
* used by one driver. FindBoard also usually fills some fields of the
* BoardInfo struct supplied by the caller, the rtg.library, for example
* the MemoryBase, MemorySize and RegisterBase fields.
*/
static uae_u32 REGPARAM2 picasso_FindCard (TrapContext *ctx)
{
    struct regstruct *regs = &ctx->regs;
    uaecptr AmigaBoardInfo = m68k_areg (regs, 0);
    /* NOTES: See BoardInfo struct definition in Picasso96 dev info */
#ifdef UAEGFX_INTERNAL
    if (!uaegfx_base)
	return 0;
    put_long (uaegfx_base + CARD_BOARDINFO, AmigaBoardInfo);
#endif
    if (allocated_gfxmem && !picasso96_state.CardFound) {
	/* Fill in MemoryBase, MemorySize */
	put_long (AmigaBoardInfo + PSSO_BoardInfo_MemoryBase, gfxmem_start);
	put_long (AmigaBoardInfo + PSSO_BoardInfo_MemorySize, allocated_gfxmem);
	picasso96_state.CardFound = 1; /* mark our "card" as being found */
	return -1;
    } else
	return 0;
}

static void FillBoardInfo (uaecptr amigamemptr, struct LibResolution *res, struct PicassoResolution *dm, int depth)
{
    int i;

    switch (depth)
    {
	case 8:
	res->Modes[CHUNKY] = amigamemptr;
	break;
	case 15:
	case 16:
	res->Modes[HICOLOR] = amigamemptr;
	break;
	case 24:
	res->Modes[TRUECOLOR] = amigamemptr;
	break;
	default:
	res->Modes[TRUEALPHA] = amigamemptr;
	break;
    }
    for (i = 0; i < PSSO_ModeInfo_sizeof; i++)
	put_byte (amigamemptr + i, 0);

    put_word (amigamemptr + PSSO_ModeInfo_Width, dm->res.width);
    put_word (amigamemptr + PSSO_ModeInfo_Height, dm->res.height);
    put_byte (amigamemptr + PSSO_ModeInfo_Depth, depth);
    put_byte (amigamemptr + PSSO_ModeInfo_Flags, 0);
    put_word (amigamemptr + PSSO_ModeInfo_HorTotal, dm->res.width);
    put_word (amigamemptr + PSSO_ModeInfo_HorBlankSize, 0);
    put_word (amigamemptr + PSSO_ModeInfo_HorSyncStart, 0);
    put_word (amigamemptr + PSSO_ModeInfo_HorSyncSize, 0);
    put_byte (amigamemptr + PSSO_ModeInfo_HorSyncSkew, 0);
    put_byte (amigamemptr + PSSO_ModeInfo_HorEnableSkew, 0);

    put_word (amigamemptr + PSSO_ModeInfo_VerTotal, dm->res.height);
    put_word (amigamemptr + PSSO_ModeInfo_VerBlankSize, 0);
    put_word (amigamemptr + PSSO_ModeInfo_VerSyncStart, 0);
    put_word (amigamemptr + PSSO_ModeInfo_VerSyncSize, 0);

    put_byte (amigamemptr + PSSO_ModeInfo_first_union, 98);
    put_byte (amigamemptr + PSSO_ModeInfo_second_union, 14);

    put_long (amigamemptr + PSSO_ModeInfo_PixelClock,
	dm->res.width * dm->res.height * (currprefs.gfx_refreshrate ? abs (currprefs.gfx_refreshrate) : default_freq));
}

struct modeids {
    int width, height;
    int id;
};
static struct modeids mi[] =
{
/* "original" modes */

    320, 200, 0,
    320, 240, 1,
    640, 400, 2,
    640, 480, 3,
    800, 600, 4,
   1024, 768, 5,
   1152, 864, 6,
   1280,1024, 7,
   1600,1280, 8,

/* new modes */

    704, 480, 129,
    704, 576, 130,
    720, 480, 131,
    720, 576, 132,
    768, 483, 133,
    768, 576, 134,
    800, 480, 135,
    848, 480, 136,
    854, 480, 137,
    948, 576, 138,
   1024, 576, 139,
   1152, 768, 140,
   1152, 864, 141,
   1280, 720, 142,
   1280, 768, 143,
   1280, 800, 144,
   1280, 854, 145,
   1280, 960, 146,
   1366, 768, 147,
   1440, 900, 148,
   1440, 960, 149,
   1600,1200, 150,
   1680,1050, 151,
   1920,1080, 152,
   1920,1200, 153,
   2048,1152, 154,
   2048,1536, 155,
   2560,1600, 156,
   2560,2048, 157,
    400, 300, 158,
    512, 384, 159,
    640, 432, 160,
   1360, 768, 161,
   1360,1024, 162,
   1400,1050, 163,
   1792,1344, 164,
   1800,1440, 165,
   1856,1392, 166,
   1920,1440, 167,
    480, 360, 168,
    640, 350, 169,
   1600, 900, 170,
    960, 600, 171,
   1088, 612, 172,
   -1,-1,0
};

static int AssignModeID (int dm, int count, int *unkcnt)
{
    int i, w, h;

    w = newmodes[dm].res.width;
    h = newmodes[dm].res.height;
    for (i = 0; mi[i].width > 0; i++) {
	if (w == mi[i].width && h == mi[i].height)
	    return 0x50001000 | (mi[i].id * 0x10000);
    }
    (*unkcnt)++;
    write_log ("P96: Non-unique mode %dx%d\n", w, h);
    return 0x51000000 - (*unkcnt) * 0x10000;
}

static uaecptr picasso96_amem, picasso96_amemend;


static void CopyLibResolutionStructureU2A (struct LibResolution *libres, uaecptr amigamemptr)
{
    int i;

    for (i = 0; i < PSSO_LibResolution_sizeof; i++)
	put_byte (amigamemptr + i, 0);
    for (i = 0; i < strlen (libres->P96ID); i++)
	put_byte (amigamemptr + PSSO_LibResolution_P96ID + i, libres->P96ID[i]);
    put_long (amigamemptr + PSSO_LibResolution_DisplayID, libres->DisplayID);
    put_word (amigamemptr + PSSO_LibResolution_Width, libres->Width);
    put_word (amigamemptr + PSSO_LibResolution_Height, libres->Height);
    put_word (amigamemptr + PSSO_LibResolution_Flags, libres->Flags);
    for (i = 0; i < MAXMODES; i++)
	put_long (amigamemptr + PSSO_LibResolution_Modes + i * 4, libres->Modes[i]);
    put_long (amigamemptr + 10, amigamemptr + PSSO_LibResolution_P96ID);
    put_long (amigamemptr + PSSO_LibResolution_BoardInfo, libres->BoardInfo);
}

static int p96depth (int depth)
{
    uae_u32 f = currprefs.picasso96_modeflags;
    int ok = 0;

    if (depth == 8 && (f & RGBFF_CLUT))
	ok = 1;
    if (depth == 15 && (f & (RGBFF_R5G5B5PC | RGBFF_R5G5B5 | RGBFF_B5G5R5PC)))
	ok = 2;
    if (depth == 16 && (f & (RGBFF_R5G6B5PC | RGBFF_R5G6B5 | RGBFF_B5G6R5PC)))
	ok = 2;
    if (depth == 24 && (f & (RGBFF_R8G8B8 | RGBFF_B8G8R8)))
	ok = 3;
    if (depth == 32 && (f & (RGBFF_A8R8G8B8 | RGBFF_A8B8G8R8 | RGBFF_R8G8B8A8 | RGBFF_B8G8R8A8)))
	ok = 4;
    return ok;
}

static int missmodes[] = { 320, 200, 320, 240, 640, 400, 640, 480, -1 };

#ifdef UAEGFX_INTERNAL
static uaecptr uaegfx_card_install (TrapContext *ctx, uae_u32 size);
#endif
void picasso96_alloc (TrapContext *ctx)
{
    int i, j, size, cnt;
    int misscnt, depths;
    SYSTEM_INFO si;

    uaegfx_resname = ds ("uaegfx.card");
    xfree (newmodes);
    newmodes = NULL;
    picasso96_amem = picasso96_amemend = 0;
    if (allocated_gfxmem == 0)
	return;
    misscnt = 0;
    cnt = 0;
    newmodes = xmalloc (sizeof (struct PicassoResolution) * MAX_PICASSO_MODES);
    size = 0;

    depths = 0;
    if (p96depth (8))
	depths++;
    if (p96depth (15))
	depths++;
    if (p96depth (16))
	depths++;
    if (p96depth (24))
	depths++;
    if (p96depth (32))
	depths++;

    i = 0;
    while (DisplayModes[i].depth >= 0) {
	for (j = 0; missmodes[j * 2] >= 0; j++) {
	    if (DisplayModes[i].res.width == missmodes[j * 2 + 0] && DisplayModes[i].res.height == missmodes[j * 2 + 1]) {
		missmodes[j * 2 + 0] = 0;
		missmodes[j * 2 + 1] = 0;
	    }
	}
	i++;
    }

    i = 0;
    while (DisplayModes[i].depth >= 0) {
	j = i;
	size += PSSO_LibResolution_sizeof;
	while (missmodes[misscnt * 2] == 0)
	    misscnt++;
	if (missmodes[misscnt * 2] >= 0) {
	    if (DisplayModes[i].res.width > missmodes[misscnt * 2 + 0] || DisplayModes[i].res.height > missmodes[misscnt * 2 + 1]) {
		int depth;
		int w = DisplayModes[i].res.width;
		int h = DisplayModes[i].res.height;
		for (depth = 8; depth <= 32; depth++) {
		    struct PicassoResolution *pr = &newmodes[cnt];
		    if (!p96depth (depth))
			continue;
		    memcpy (pr, &DisplayModes[i], sizeof (struct PicassoResolution));
		    pr->res.width = missmodes[misscnt * 2 + 0];
		    pr->res.height = missmodes[misscnt * 2 + 1];
		    sprintf (pr->name, "%dx%dx%d FAKE", pr->res.width, pr->res.height, depth);
		    size += PSSO_ModeInfo_sizeof;
		}
		misscnt++;
		continue;
	    }
	}
        memcpy (&newmodes[cnt], &DisplayModes[i], sizeof (struct PicassoResolution));
        size += PSSO_ModeInfo_sizeof * depths;
        i++;
        cnt++;
	while (DisplayModes[i].depth >= 0
	    && DisplayModes[i].res.width == DisplayModes[j].res.width
	    && DisplayModes[i].res.height == DisplayModes[j].res.height)
		i++;
    }
    newmodes[cnt].depth = -1;

    for (i = 0; i < cnt; i++) {
	int depth;
	for (depth = 8; depth <= 32; depth++) {
	    if (!p96depth (depth))
		continue;
	    switch (depth) {
	    case 1:
		if (newmodes[i].res.width > chunky.width)
		    chunky.width = newmodes[i].res.width;
		if (newmodes[i].res.height > chunky.height)
		    chunky.height = newmodes[i].res.height;
		break;
	    case 2:
		if (newmodes[i].res.width > hicolour.width)
		    hicolour.width = newmodes[i].res.width;
		if (newmodes[i].res.height > hicolour.height)
		    hicolour.height = newmodes[i].res.height;
		break;
	    case 3:
		if (newmodes[i].res.width > truecolour.width)
		    truecolour.width = newmodes[i].res.width;
		if (newmodes[i].res.height > truecolour.height)
		    truecolour.height = newmodes[i].res.height;
		break;
	    case 4:
		if (newmodes[i].res.width > alphacolour.width)
		    alphacolour.width = newmodes[i].res.width;
		if (newmodes[i].res.height > alphacolour.height)
		    alphacolour.height = newmodes[i].res.height;
		break;
	    }
	}
    }
#if 0
    ShowSupportedResolutions ();
#endif
#ifdef UAEGFX_INTERNAL
    picasso96_amem = 0;
    if (uaegfx_card_install (ctx, size)) {
	picasso96_amem = get_long (uaegfx_base + CARD_RESLIST);
	picasso96_amemend = picasso96_amem + size;
	write_log("P96 RESINFO: %08X-%08X (%d,%d)\n", picasso96_amem, picasso96_amemend, cnt, size);
    }
#else
    m68k_dreg (&ctx->regs, 0) = size;
    m68k_dreg (&ctx->regs, 1) = 65536 + 1;
    if ((picasso96_amem = CallLib (ctx, get_long (4), -0xC6))) { /* AllocMem */
	uaecptr rt;
	picasso96_amemend = picasso96_amem + size;
	write_log("P96 RESINFO: %08X-%08X (%d,%d)\n", picasso96_amem, picasso96_amemend, cnt, size);
	/* put magic rtarea pointer to end of display ram */
	put_long (p96ram_start + allocated_gfxmem - 12, 'UAE_');
	rt = need_uae_boot_rom ();
	if (rt)
	    rt += 0xff60;
	put_long (p96ram_start + allocated_gfxmem - 8, rt);
	put_long (p96ram_start + allocated_gfxmem - 4, '_UAE');
    }
#endif

    xfree (gwwbuf);
    GetSystemInfo (&si);
    gwwpagesize = si.dwPageSize;
    gwwbufsize = allocated_gfxmem / gwwpagesize + 1;
    gwwpagemask = gwwpagesize - 1;
    gwwbuf = xmalloc (gwwbufsize * sizeof (void*));
}

#ifndef UAEGFX_INTERNAL
static void uaegfxversion (uaecptr bi)
{
    uaecptr addr = get_long (bi + 16); /* gbi_BoardName */
    int max = 1000;
    int ok = 0;

    uaegfxcard_old = 1;

    addr &= ~1;
    for (;;) {
	if (!valid_address (addr, 24) || bi == 0) {
	    max = 0;
	    break;
	}
	addr -= 2;
	if (get_word (addr) == 0x4afc)
	    break;
	max--;
	if (max < 0)
	    break;
    }
    if (max > 0) {
	uaecptr romtagp;
	romtagp = get_long (addr + 2);
	if (romtagp == addr) {
	    uaecptr ver = get_long (addr + 18);
	    if (valid_address (ver, 8)) {
		char *vers = my_strdup (get_real_address (ver));
		int version = get_byte (addr + 11);
		while (strlen (vers) > 0 && (vers[strlen (vers) - 1] == 10 || vers[strlen (vers) - 1] == 13))
		    vers[strlen (vers) - 1] = 0;

		write_log ("P96: v%d %08X %s\n", version, addr, vers);
		if (version == 1) {
		    static int warned;
		    if (!warned)
			gui_message("Buggy libs:picasso96/uaegfx.card detected.\nPlease replace it with official version.\n");
		    warned = 1;
		}
		if (version >= 2)
		    uaegfxcard_old = 0;
		xfree (vers);
		ok = 1;
	    }
	}
    }
    if (!ok)
	write_log ("P96: uaegfx.card not detected!?\n");
}
#endif

static void inituaegfxfuncs (uaecptr ABI);
static void inituaegfx (uaecptr ABI)
{
    uae_u32 flags;

    write_log ("RTG mode mask: %x\n", currprefs.picasso96_modeflags);
    put_word (ABI + PSSO_BoardInfo_BitsPerCannon, 8);
    put_word (ABI + PSSO_BoardInfo_RGBFormats, currprefs.picasso96_modeflags);
    put_long (ABI + PSSO_BoardInfo_BoardType, picasso96_BT);
    put_long (ABI + PSSO_BoardInfo_GraphicsControllerType, picasso96_GCT);
    put_long (ABI + PSSO_BoardInfo_PaletteChipType, picasso96_PCT);
    put_long (ABI + PSSO_BoardInfo_BoardName, uaegfx_resname);
    put_long (ABI + PSSO_BoardInfo_BoardType, 1);

    /* only 1 clock */
    put_long (ABI + PSSO_BoardInfo_PixelClockCount + PLANAR * 4, 1);
    put_long (ABI + PSSO_BoardInfo_PixelClockCount + CHUNKY * 4, 1);
    put_long (ABI + PSSO_BoardInfo_PixelClockCount + HICOLOR * 4, 1);
    put_long (ABI + PSSO_BoardInfo_PixelClockCount + TRUECOLOR * 4, 1);
    put_long (ABI + PSSO_BoardInfo_PixelClockCount + TRUEALPHA * 4, 1);

    /* we have 16 bits for horizontal and vertical timings - hack */
    put_word (ABI + PSSO_BoardInfo_MaxHorValue + PLANAR * 2, 0xffff);
    put_word (ABI + PSSO_BoardInfo_MaxHorValue + CHUNKY * 2, 0xffff);
    put_word (ABI + PSSO_BoardInfo_MaxHorValue + HICOLOR * 2, 0xffff);
    put_word (ABI + PSSO_BoardInfo_MaxHorValue + TRUECOLOR * 2, 0xffff);
    put_word (ABI + PSSO_BoardInfo_MaxHorValue + TRUEALPHA * 2, 0xffff);
    put_word (ABI + PSSO_BoardInfo_MaxVerValue + PLANAR * 2, 0xffff);
    put_word (ABI + PSSO_BoardInfo_MaxVerValue + CHUNKY * 2, 0xffff);
    put_word (ABI + PSSO_BoardInfo_MaxVerValue + HICOLOR * 2, 0xffff);
    put_word (ABI + PSSO_BoardInfo_MaxVerValue + TRUECOLOR * 2, 0xffff);
    put_word (ABI + PSSO_BoardInfo_MaxVerValue + TRUEALPHA * 2, 0xffff);

    flags = get_long (ABI + PSSO_BoardInfo_Flags);
    flags &= 0xffff0000;
    flags |= BIF_BLITTER | BIF_NOMEMORYMODEMIX;
#ifndef UAEGFX_INTERNAL
    if (flags & BIF_HARDWARESPRITE) {
	hwsprite = 1;
	put_word (ABI + PSSO_BoardInfo_SoftSpriteFlags, 0);
	write_log ("P96: uaegfx.card: hardware sprite support enabled\n");
    } else {
	hwsprite = 0;
	write_log ("P96: uaegfx.card: no hardware sprite support\n");
	put_word (ABI + PSSO_BoardInfo_SoftSpriteFlags, currprefs.picasso96_modeflags);
    }
    if (uaegfxcard_old && (flags & BIF_HARDWARESPRITE)) {
	flags &= ~BIF_HARDWARESPRITE;
	write_log ("P96: uaegfx.card: old version (<2.0), forced hardware sprite disabled\n");
    }
    if (!(flags & BIF_BLITTER))
	write_log ("P96: no blitter support, bogus uaegfx.card!?\n");
#else
    hwsprite = 1;
    flags |= BIF_HARDWARESPRITE;
#endif
    put_long (ABI + PSSO_BoardInfo_Flags, flags);
    if (flags & BIF_NOBLITTER)
	write_log ("P96: blitter disabled in devs:monitors/uaegfx!\n");

    put_word (ABI + PSSO_BoardInfo_MaxHorResolution + 0, planar.width);
    put_word (ABI + PSSO_BoardInfo_MaxHorResolution + 2, chunky.width);
    put_word (ABI + PSSO_BoardInfo_MaxHorResolution + 4, hicolour.width);
    put_word (ABI + PSSO_BoardInfo_MaxHorResolution + 6, truecolour.width);
    put_word (ABI + PSSO_BoardInfo_MaxHorResolution + 8, alphacolour.width);
    put_word (ABI + PSSO_BoardInfo_MaxVerResolution + 0, planar.height);
    put_word (ABI + PSSO_BoardInfo_MaxVerResolution + 2, chunky.height);
    put_word (ABI + PSSO_BoardInfo_MaxVerResolution + 4, hicolour.height);
    put_word (ABI + PSSO_BoardInfo_MaxVerResolution + 6, truecolour.height);
    put_word (ABI + PSSO_BoardInfo_MaxVerResolution + 8, alphacolour.height);
#ifdef UAEGFX_INTERNAL
    inituaegfxfuncs (ABI);
#endif
}

/****************************************
* InitCard()
*
* a2: BoardInfo structure ptr - Amiga-based address in Intel endian-format
*
*/
static uae_u32 REGPARAM2 picasso_InitCard (TrapContext *ctx)
{
    struct regstruct *regs = &ctx->regs;
    struct LibResolution res;
    int ModeInfoStructureCount = 1, LibResolutionStructureCount = 0;
    int i, j, unkcnt;
    uaecptr amem;
    uaecptr AmigaBoardInfo = m68k_areg (regs, 0);

#ifndef UAEGFX_INTERNAL
    uaegfxversion (AmigaBoardInfo);
#endif
    if (!picasso96_amem) {
	write_log ("P96: InitCard() but no resolution memory!\n");
	return 0;
    }
    amem = picasso96_amem;

    inituaegfx (AmigaBoardInfo);

    i = 0;
    unkcnt = 0;
    while (newmodes[i].depth >= 0) {
	int depth;
	j = i;
	/* Add a LibResolution structure to the ResolutionsList MinList in our BoardInfo */
	res.DisplayID = AssignModeID (i, LibResolutionStructureCount, &unkcnt);
	res.BoardInfo = AmigaBoardInfo;
	res.Width = newmodes[i].res.width;
	res.Height = newmodes[i].res.height;
	res.Flags = P96F_PUBLIC;
	memcpy (res.P96ID, "P96-0:", 6);
	sprintf (res.Name, "UAE:%dx%d", res.Width, res.Height);
	res.Modes[PLANAR] = 0;
	res.Modes[CHUNKY] = 0;
	res.Modes[HICOLOR] = 0;
	res.Modes[TRUECOLOR] = 0;
	res.Modes[TRUEALPHA] = 0;

	for (depth = 8; depth <= 32; depth++) {
	    if (!p96depth (depth))
		continue;
	    /* Handle this display mode's depth */
	    /* New: Only add the modes when there is enough P96 RTG memory to hold the bitmap */
	    if(allocated_gfxmem >= newmodes[i].res.width * newmodes[i].res.height * (depth + 7) / 8) {
		ModeInfoStructureCount++;
		FillBoardInfo (amem, &res, &newmodes[i], depth);
		amem += PSSO_ModeInfo_sizeof;
	    }
	}
	while (newmodes[i].depth >= 0
	    && newmodes[i].res.width == newmodes[j].res.width
	    && newmodes[i].res.height == newmodes[j].res.height)
		i++;

	LibResolutionStructureCount++;
	CopyLibResolutionStructureU2A (&res, amem);
#if P96TRACING_ENABLED && P96TRACING_LEVEL > 1
	DumpLibResolutionStructure(amem);
#endif
	AmigaListAddTail (AmigaBoardInfo + PSSO_BoardInfo_ResolutionsList, amem);
	amem += PSSO_LibResolution_sizeof;
    }
    if (amem > picasso96_amemend)
	write_log ("P96: display resolution list corruption %08x<>%08x (%d)\n", amem, picasso96_amemend, i);

    return -1;
}

/*
* SetSwitch:
* a0: struct BoardInfo
* d0.w: BOOL state
* this function should set a board switch to let the Amiga signal pass
* through when supplied with a 0 in d0 and to show the board signal if
* a 1 is passed in d0. You should remember the current state of the
* switch to avoid unneeded switching. If your board has no switch, then
* simply supply a function that does nothing except a RTS.
*
* NOTE: Return the opposite of the switch-state. BDK
*/
static uae_u32 REGPARAM2 picasso_SetSwitch (TrapContext *ctx)
{
    struct regstruct *regs = &ctx->regs;
    uae_u16 flag = m68k_dreg (regs, 0) & 0xFFFF;
    char p96text[100];

    /* Do not switch immediately.  Tell the custom chip emulation about the
    * desired state, and wait for custom.c to call picasso_enablescreen
    * whenever it is ready to change the screen state.  */
    picasso_requested_on = flag;
    p96text[0] = 0;
    if (flag)
	sprintf (p96text, "Picasso96 %dx%dx%d (%dx%dx%d)",
	    picasso96_state.Width, picasso96_state.Height, picasso96_state.BytesPerPixel * 8,
	    picasso_vidinfo.width, picasso_vidinfo.height, picasso_vidinfo.pixbytes * 8);
    write_log ("SetSwitch() - %s\n", flag ? p96text : "amiga");
    /* Put old switch-state in D0 */
    return !flag;
}


static void init_picasso_screen(void);
void picasso_enablescreen (int on)
{
    if (!init_picasso_screen_called)
	init_picasso_screen();

    picasso_refresh ();
    checkrtglibrary();
}

/*
* SetColorArray:
* a0: struct BoardInfo
* d0.w: startindex
* d1.w: count
* when this function is called, your driver has to fetch "count" color
* values starting at "startindex" from the CLUT field of the BoardInfo
* structure and write them to the hardware. The color values are always
* between 0 and 255 for each component regardless of the number of bits
* per cannon your board has. So you might have to shift the colors
* before writing them to the hardware.
*/
static int updateclut (uaecptr clut, int start, int count)
{
    int i, changed = 0;
    clut += start * 3;
    for (i = start; i < start + count; i++) {
	int r = get_byte (clut);
	int g = get_byte (clut + 1);
	int b = get_byte (clut + 2);

	changed |= (picasso96_state.CLUT[i].Red != r
	    || picasso96_state.CLUT[i].Green != g
	    || picasso96_state.CLUT[i].Blue != b);

	picasso96_state.CLUT[i].Red = r;
	picasso96_state.CLUT[i].Green = g;
	picasso96_state.CLUT[i].Blue = b;
	clut += 3;
    }
    return changed;
}
static uae_u32 REGPARAM2 picasso_SetColorArray (TrapContext *ctx)
{
    /* Fill in some static UAE related structure about this new CLUT setting
     * We need this for CLUT-based displays, and for mapping CLUT to hi/true colour */
    struct regstruct *regs = &ctx->regs;
    uae_u16 start = m68k_dreg (regs, 0);
    uae_u16 count = m68k_dreg (regs, 1);
    uaecptr boardinfo = m68k_areg (regs, 0);
    uaecptr clut = boardinfo + PSSO_BoardInfo_CLUT;
    if (updateclut (clut, start, count))
	palette_changed = 1;
    P96TRACE(("SetColorArray(%d,%d)\n", start, count));
    return 1;
}

/*
* SetDAC:
* a0: struct BoardInfo
* d7: RGBFTYPE RGBFormat
* This function is called whenever the RGB format of the display changes,
* e.g. from chunky to TrueColor. Usually, all you have to do is to set
* the RAMDAC of your board accordingly.
*/
static uae_u32 REGPARAM2 picasso_SetDAC (TrapContext *ctx)
{
    struct regstruct *regs = &ctx->regs;
/* Fill in some static UAE related structure about this new DAC setting
    * Lets us keep track of what pixel format the Amiga is thinking about in our frame-buffer */

    P96TRACE(("SetDAC()\n"));
    return 1;
}

static void init_picasso_screen (void)
{
    reloadcursor = 1;
    if(set_panning_called) {
	picasso96_state.Extent = picasso96_state.Address + picasso96_state.BytesPerRow * picasso96_state.VirtualHeight;
    }
    if (set_gc_called) {
	gfx_set_picasso_modeinfo (picasso96_state.Width, picasso96_state.Height,
	    picasso96_state.GC_Depth, picasso96_state.RGBFormat);
    }
    if((picasso_vidinfo.width == picasso96_state.Width) &&
	(picasso_vidinfo.height == picasso96_state.Height) &&
	(picasso_vidinfo.depth == (picasso96_state.GC_Depth >> 3)) &&
	(picasso_vidinfo.selected_rgbformat == picasso96_state.RGBFormat))
    {
	picasso_refresh ();
    }
    init_picasso_screen_called = 1;
    mman_ResetWatch (p96ram_start + natmem_offset, allocated_gfxmem);

}

/*
* SetGC:
* a0: struct BoardInfo
* a1: struct ModeInfo
* d0: BOOL Border
* This function is called whenever another ModeInfo has to be set. This
* function simply sets up the CRTC and TS registers to generate the
* timing used for that screen mode. You should not set the DAC, clocks
* or linear start adress. They will be set when appropriate by their
* own functions.
*/
static uae_u32 REGPARAM2 picasso_SetGC (TrapContext *ctx)
{
    struct regstruct *regs = &ctx->regs;
    /* Fill in some static UAE related structure about this new ModeInfo setting */
    uaecptr AmigaBoardInfo = m68k_areg (regs, 0);
    uae_u32 border   = m68k_dreg (regs, 0);
    uaecptr modeinfo = m68k_areg (regs, 1);

    put_long (AmigaBoardInfo + PSSO_BoardInfo_ModeInfo, modeinfo);
    put_word (AmigaBoardInfo + PSSO_BoardInfo_Border, border);

    picasso96_state.Width = get_word (modeinfo + PSSO_ModeInfo_Width);
    picasso96_state.VirtualWidth = picasso96_state.Width; /* in case SetPanning doesn't get called */

    picasso96_state.Height = get_word (modeinfo + PSSO_ModeInfo_Height);
    picasso96_state.VirtualHeight = picasso96_state.Height; /* in case SetPanning doesn't get called */

    picasso96_state.GC_Depth = get_byte (modeinfo + PSSO_ModeInfo_Depth);
    picasso96_state.GC_Flags = get_byte (modeinfo + PSSO_ModeInfo_Flags);

    P96TRACE(("SetGC(%d,%d,%d,%d)\n", picasso96_state.Width, picasso96_state.Height, picasso96_state.GC_Depth, border));
    set_gc_called = 1;
    picasso96_state.HostAddress = NULL;
    init_picasso_screen ();
    init_hz_p96 ();
    return 1;
}

/*
* SetPanning:
* a0: struct BoardInfo
* a1: UBYTE *Memory
* d0: uae_u16 Width
* d1: WORD XOffset
* d2: WORD YOffset
* d7: RGBFTYPE RGBFormat
* This function sets the view origin of a display which might also be
* overscanned. In register a1 you get the start address of the screen
* bitmap on the Amiga side. You will have to subtract the starting
* address of the board memory from that value to get the memory start
* offset within the board. Then you get the offset in pixels of the
* left upper edge of the visible part of an overscanned display. From
* these values you will have to calculate the LinearStartingAddress
* fields of the CRTC registers.

  * NOTE: SetPanning() can be used to know when a Picasso96 screen is
  * being opened.  Better to do the appropriate clearing of the
  * background here than in SetSwitch() derived functions,
  * because SetSwitch() is not called for subsequent Picasso screens.
*/

static void picasso_SetPanningInit (void)
{
    picasso96_state.XYOffset = picasso96_state.Address + (picasso96_state.XOffset * picasso96_state.BytesPerPixel)
	+ (picasso96_state.YOffset * picasso96_state.BytesPerRow);
    if(picasso96_state.VirtualWidth > picasso96_state.Width || picasso96_state.VirtualHeight > picasso96_state.Height)
	picasso96_state.BigAssBitmap = 1;
    else
	picasso96_state.BigAssBitmap = 0;
}

static uae_u32 REGPARAM2 picasso_SetPanning (TrapContext *ctx)
{
    struct regstruct *regs = &ctx->regs;
    uae_u16 Width = m68k_dreg (regs, 0);
    uaecptr start_of_screen = m68k_areg (regs, 1);
    uaecptr bi = m68k_areg (regs, 0);
    uaecptr bmeptr = get_long (bi + PSSO_BoardInfo_BitMapExtra);  /* Get our BoardInfo ptr's BitMapExtra ptr */
    uae_u16 bme_width, bme_height;
    int changed = 0;

    if(oldscr == 0) {
	oldscr = start_of_screen;
	changed = 1;
    }
    if (oldscr != start_of_screen) {
	set_gc_called = 0;
	oldscr = start_of_screen;
	changed = 1;
    }

    if (changed) {
#if P96DX > 0
	freesurface (p96surface);
	p96surface = NULL;
	if (picasso96_state.BytesPerPixel == picasso_vidinfo.pixbytes)
	    p96surface = createsurface (get_real_address (picasso96_state.Address),
		picasso96_state.Width * picasso96_state.BytesPerPixel, picasso96_state.Width, picasso96_state.Height);
	write_log ("P96Surface: %08X %p\n", picasso96_state.Address, p96surface);
#endif
    }

    bme_width = get_word (bmeptr + PSSO_BitMapExtra_Width);
    bme_height = get_word (bmeptr + PSSO_BitMapExtra_Height);

    picasso96_state.Address = start_of_screen; /* Amiga-side address */
    picasso96_state.XOffset = (uae_s16)(m68k_dreg (regs, 1) & 0xFFFF);
    picasso96_state.YOffset = (uae_s16)(m68k_dreg (regs, 2) & 0xFFFF);
    put_word (bi + PSSO_BoardInfo_XOffset, picasso96_state.XOffset);
    put_word (bi + PSSO_BoardInfo_YOffset, picasso96_state.YOffset);
    picasso96_state.VirtualWidth = bme_width;
    picasso96_state.VirtualHeight = bme_height;
    picasso96_state.RGBFormat = m68k_dreg (regs, 7);
    picasso96_state.BytesPerPixel = GetBytesPerPixel (picasso96_state.RGBFormat);
    picasso96_state.BytesPerRow = picasso96_state.VirtualWidth * picasso96_state.BytesPerPixel;
    picasso_SetPanningInit();

    palette_changed = 1;
    set_panning_called = 1;
    P96TRACE(("SetPanning(%d, %d, %d) Start 0x%x, BPR %d Bpp %d RGBF %d\n",
	Width, picasso96_state.XOffset, picasso96_state.YOffset,
	start_of_screen, picasso96_state.BytesPerRow, picasso96_state.BytesPerPixel, picasso96_state.RGBFormat));
    init_picasso_screen ();
    set_panning_called = 0;

    return 1;
}

static void do_xor8 (uae_u8 *p, int w, uae_u32 v)
{
    while (ALIGN_POINTER_TO32 (p) != 3 && w) {
	*p ^= v;
	p++;
	w--;
    }
    while (w >= 2 * 4) {
	*((uae_u32*)p) ^= v;
	p += 4;
	*((uae_u32*)p) ^= v;
	p += 4;
	w -= 2 * 4;
    }
    while (w) {
	*p ^= v;
	p++;
	w--;
    }
}

/*
* InvertRect:
*
* Inputs:
* a0:struct BoardInfo *bi
* a1:struct RenderInfo *ri
* d0.w:X
* d1.w:Y
* d2.w:Width
* d3.w:Height
* d4.l:Mask
* d7.l:RGBFormat
*
* This function is used to invert a rectangular area on the board. It is called by BltBitMap,
* BltPattern and BltTemplate.
*/
static uae_u32 REGPARAM2 picasso_InvertRect (TrapContext *ctx)
{
    struct regstruct *regs = &ctx->regs;
    uaecptr renderinfo = m68k_areg (regs, 1);
    unsigned long X = (uae_u16)m68k_dreg (regs, 0);
    unsigned long Y = (uae_u16)m68k_dreg (regs, 1);
    unsigned long Width = (uae_u16)m68k_dreg (regs, 2);
    unsigned long Height = (uae_u16)m68k_dreg (regs, 3);
    uae_u8 mask = (uae_u8)m68k_dreg (regs, 4);
    int Bpp = GetBytesPerPixel (m68k_dreg (regs, 7));
    uae_u32 xorval;
    unsigned int lines;
    struct RenderInfo ri;
    uae_u8 *uae_mem, *rectstart;
    unsigned long width_in_bytes;
    uae_u32 result = 0;

    if (NOBLITTER)
	return 0;
    if (CopyRenderInfoStructureA2U (renderinfo, &ri)) {
	P96TRACE(("InvertRect %dbpp 0x%lx\n", Bpp, (long)mask));

	if (mask != 0xFF && Bpp > 1)
	    mask = 0xFF;

	xorval = 0x01010101 * (mask & 0xFF);
	width_in_bytes = Bpp * Width;
	rectstart = uae_mem = ri.Memory + Y * ri.BytesPerRow + X * Bpp;

	for (lines = 0; lines < Height; lines++, uae_mem += ri.BytesPerRow)
	    do_xor8 (uae_mem, width_in_bytes, xorval);
	result = 1;
    }

    return result; /* 1 if supported, 0 otherwise */
}

/***********************************************************
FillRect:
***********************************************************
* a0: struct BoardInfo *
* a1: struct RenderInfo *
* d0: WORD X
* d1: WORD Y
* d2: WORD Width
* d3: WORD Height
* d4: uae_u32 Pen
* d5: UBYTE Mask
* d7: uae_u32 RGBFormat
***********************************************************/
static uae_u32 REGPARAM2 picasso_FillRect (TrapContext *ctx)
{
    struct regstruct *regs = &ctx->regs;
    uaecptr renderinfo = m68k_areg (regs, 1);
    uae_u32 X = (uae_u16)m68k_dreg (regs, 0);
    uae_u32 Y = (uae_u16)m68k_dreg (regs, 1);
    uae_u32 Width = (uae_u16)m68k_dreg (regs, 2);
    uae_u32 Height = (uae_u16)m68k_dreg (regs, 3);
    uae_u32 Pen = m68k_dreg (regs, 4);
    uae_u8 Mask = (uae_u8)m68k_dreg (regs, 5);
    RGBFTYPE RGBFormat = m68k_dreg (regs, 7);
    uae_u8 *oldstart;
    int Bpp;
    struct RenderInfo ri;
    uae_u32 result = 0;

    if (NOBLITTER)
	return 0;
    if (CopyRenderInfoStructureA2U (renderinfo, &ri) && Y != 0xFFFF) {
	Bpp = GetBytesPerPixel (RGBFormat);

	P96TRACE(("FillRect(%d, %d, %d, %d) Pen 0x%x BPP %d BPR %d Mask 0x%x\n",
	    X, Y, Width, Height, Pen, Bpp, ri.BytesPerRow, Mask));

	if(Bpp > 1)
	    Mask = 0xFF;

	if (Mask == 0xFF) {

	    /* Do the fill-rect in the frame-buffer */
	    do_fillrect_frame_buffer (&ri, X, Y, Width, Height, Pen, Bpp);
	    result = 1;

	} else {

	    /* We get here only if Mask != 0xFF */
	    if (Bpp != 1) {
		write_log ("WARNING - FillRect() has unhandled mask 0x%x with Bpp %d. Using fall-back routine.\n", Mask, Bpp);
	    } else {
		Pen &= Mask;
		Mask = ~Mask;
		oldstart = ri.Memory + Y * ri.BytesPerRow + X * Bpp;
		{
		    uae_u8 *start = oldstart;
		    uae_u8 *end = start + Height * ri.BytesPerRow;
		    for (; start != end; start += ri.BytesPerRow) {
			uae_u8 *p = start;
			unsigned long cols;
			for (cols = 0; cols < Width; cols++) {
			    uae_u32 tmpval = do_get_mem_byte (p + cols) & Mask;
			    do_put_mem_byte (p + cols, (uae_u8)(Pen | tmpval));
			}
		    }
		}
		result = 1;
	    }
	}
    }
    return result;
}

/*
* BlitRect() is a generic (any chunky pixel format) rectangle copier
* NOTE: If dstri is NULL, then we're only dealing with one RenderInfo area, and called from picasso_BlitRect()
*
* OpCodes:
* 0 = FALSE:	dst = 0
* 1 = NOR:	dst = ~(src | dst)
* 2 = ONLYDST:	dst = dst & ~src
* 3 = NOTSRC:	dst = ~src
* 4 = ONLYSRC:	dst = src & ~dst
* 5 = NOTDST:	dst = ~dst
* 6 = EOR:	dst = src^dst
* 7 = NAND:	dst = ~(src & dst)
* 8 = AND:	dst = (src & dst)
* 9 = NEOR:	dst = ~(src ^ dst)
*10 = DST:	dst = dst
*11 = NOTONLYSRC: dst = ~src | dst
*12 = SRC:	dst = src
*13 = NOTONLYDST: dst = ~dst | src
*14 = OR:	dst = src | dst
*15 = TRUE:	dst = 0xFF
*/
struct blitdata
{
    struct RenderInfo ri_struct;
    struct RenderInfo dstri_struct;
    struct RenderInfo *ri; /* Self-referencing pointers */
    struct RenderInfo *dstri;
    unsigned long srcx;
    unsigned long srcy;
    unsigned long dstx;
    unsigned long dsty;
    unsigned long width;
    unsigned long height;
    uae_u8 mask;
    BLIT_OPCODE opcode;
} blitrectdata;

STATIC_INLINE int BlitRectHelper (void)
{
    struct RenderInfo *ri = blitrectdata.ri;
    struct RenderInfo *dstri = blitrectdata.dstri;
    unsigned long srcx = blitrectdata.srcx;
    unsigned long srcy = blitrectdata.srcy;
    unsigned long dstx = blitrectdata.dstx;
    unsigned long dsty = blitrectdata.dsty;
    unsigned long width = blitrectdata.width;
    unsigned long height = blitrectdata.height;
    uae_u8 mask = blitrectdata.mask;
    BLIT_OPCODE opcode = blitrectdata.opcode;

    uae_u8 Bpp = GetBytesPerPixel (ri->RGBFormat);

    if (opcode == BLIT_DST) {
	write_log ( "WARNING: BlitRect() being called with opcode of BLIT_DST\n" );
	return 1;
    }

    /*
    * If we have no destination RenderInfo, then we're dealing with a single-buffer action, called
    * from picasso_BlitRect().  The code in do_blitrect_frame_buffer() deals with the frame-buffer,
    * while the do_blit() code deals with the visible screen.
    *
    * If we have a destination RenderInfo, then we've been called from picasso_BlitRectNoMaskComplete()
    * and we need to put the results on the screen from the frame-buffer.
    */
    if (dstri == NULL || dstri->Memory == ri->Memory) {
	if (mask != 0xFF && Bpp > 1)
	    mask = 0xFF;
	dstri = ri;
    }
    /* Do our virtual frame-buffer memory first */
    return do_blitrect_frame_buffer (ri, dstri, srcx, srcy, dstx, dsty, width, height, mask, opcode);
}

STATIC_INLINE int BlitRect (uaecptr ri, uaecptr dstri,
		unsigned long srcx, unsigned long srcy, unsigned long dstx, unsigned long dsty,
		unsigned long width, unsigned long height, uae_u8 mask, BLIT_OPCODE opcode)
{
    /* Set up the params */
    CopyRenderInfoStructureA2U(ri, &blitrectdata.ri_struct);
    blitrectdata.ri = &blitrectdata.ri_struct;
    if(dstri) {
	CopyRenderInfoStructureA2U(dstri, &blitrectdata.dstri_struct);
	blitrectdata.dstri = &blitrectdata.dstri_struct;
    } else {
	blitrectdata.dstri = NULL;
    }
    blitrectdata.srcx = srcx;
    blitrectdata.srcy = srcy;
    blitrectdata.dstx = dstx;
    blitrectdata.dsty = dsty;
    blitrectdata.width = width;
    blitrectdata.height = height;
    blitrectdata.mask = mask;
    blitrectdata.opcode = opcode;

    return BlitRectHelper ();
}

/***********************************************************
BlitRect:
***********************************************************
* a0:	struct BoardInfo
* a1:	struct RenderInfo
* d0:	WORD SrcX
* d1:	WORD SrcY
* d2:	WORD DstX
* d3:	WORD DstY
* d4:	WORD Width
* d5:	WORD Height
* d6:	UBYTE Mask
* d7:	uae_u32 RGBFormat
***********************************************************/
static uae_u32 REGPARAM2 picasso_BlitRect (TrapContext *ctx)
{
    struct regstruct *regs = &ctx->regs;
    uaecptr renderinfo = m68k_areg (regs, 1);
    unsigned long srcx = (uae_u16)m68k_dreg (regs, 0);
    unsigned long srcy = (uae_u16)m68k_dreg (regs, 1);
    unsigned long dstx = (uae_u16)m68k_dreg (regs, 2);
    unsigned long dsty = (uae_u16)m68k_dreg (regs, 3);
    unsigned long width = (uae_u16)m68k_dreg (regs, 4);
    unsigned long height = (uae_u16)m68k_dreg (regs, 5);
    uae_u8  Mask = (uae_u8)m68k_dreg (regs, 6);
    uae_u32 result = 0;

    if (NOBLITTER_BLIT)
	return 0;
    P96TRACE(("BlitRect(%d, %d, %d, %d, %d, %d, 0x%x)\n", srcx, srcy, dstx, dsty, width, height, Mask));
    result = BlitRect (renderinfo, (uaecptr)NULL, srcx, srcy, dstx, dsty, width, height, Mask, BLIT_SRC);
    return result;
}

/***********************************************************
BlitRectNoMaskComplete:
***********************************************************
* a0:	struct BoardInfo
* a1:	struct RenderInfo (src)
* a2:	struct RenderInfo (dst)
* d0:	WORD SrcX
* d1:	WORD SrcY
* d2:	WORD DstX
* d3:	WORD DstY
* d4:	WORD Width
* d5:	WORD Height
* d6:	UBYTE OpCode
* d7:	uae_u32 RGBFormat
* NOTE:	MUST return 0 in D0 if we're not handling this operation
*	because the RGBFormat or opcode aren't supported.
*	OTHERWISE return 1
***********************************************************/
static uae_u32 REGPARAM2 picasso_BlitRectNoMaskComplete (TrapContext *ctx)
{
    struct regstruct *regs = &ctx->regs;
    uaecptr srcri = m68k_areg (regs, 1);
    uaecptr dstri = m68k_areg (regs, 2);
    unsigned long srcx = (uae_u16)m68k_dreg (regs, 0);
    unsigned long srcy = (uae_u16)m68k_dreg (regs, 1);
    unsigned long dstx = (uae_u16)m68k_dreg (regs, 2);
    unsigned long dsty = (uae_u16)m68k_dreg (regs, 3);
    unsigned long width = (uae_u16)m68k_dreg (regs, 4);
    unsigned long height = (uae_u16)m68k_dreg (regs, 5);
    uae_u8 OpCode = m68k_dreg (regs, 6);
    uae_u32 RGBFmt = m68k_dreg (regs, 7);
    uae_u32 result = 0;

    if (NOBLITTER_BLIT)
	return 0;

    P96TRACE(("BlitRectNoMaskComplete() op 0x%02x, %08x:(%4d,%4d) --> %08x:(%4d,%4d), wh(%4d,%4d)\n",
	OpCode, get_long (srcri + PSSO_RenderInfo_Memory), srcx, srcy, get_long (dstri + PSSO_RenderInfo_Memory), dstx, dsty, width, height));
    result = BlitRect (srcri, dstri, srcx, srcy, dstx, dsty, width, height, 0xFF, OpCode);
    return result;
}

/* NOTE: fgpen MUST be in host byte order */
STATIC_INLINE void PixelWrite (uae_u8 *mem, int bits, uae_u32 fgpen, int Bpp, uae_u32 mask)
{
    switch (Bpp)
    {
	case 1:
	    if (mask != 0xFF)
		fgpen = (fgpen & mask) | (mem[bits] & ~mask);
	    mem[bits] = (uae_u8)fgpen;
	    break;
	case 2:
	    ((uae_u16 *)mem)[bits] = (uae_u16)fgpen;
	    break;
	case 3:
	    mem[bits * 3 + 0] = fgpen >> 0;
	    mem[bits * 3 + 1] = fgpen >> 8;
	    mem[bits * 3 + 2] = fgpen >> 16;
	    break;
	case 4:
	    ((uae_u32 *)mem)[bits] = fgpen;
	    break;
    }
}

/*
* BlitPattern:
*
* Synopsis:BlitPattern(bi, ri, pattern, X, Y, Width, Height, Mask, RGBFormat);
* Inputs:
* a0:struct BoardInfo *bi
* a1:struct RenderInfo *ri
* a2:struct Pattern *pattern
* d0.w:X
* d1.w:Y
* d2.w:Width
* d3.w:Height
* d4.w:Mask
* d7.l:RGBFormat
*
* This function is used to paint a pattern on the board memory using the blitter. It is called by
* BltPattern, if a AreaPtrn is used with positive AreaPtSz. The pattern consists of a b/w image
* using a single plane of image data which will be expanded repeatedly to the destination RGBFormat
* using ForeGround and BackGround pens as well as draw modes. The width of the pattern data is
* always 16 pixels (one word) and the height is calculated as 2^Size. The data must be shifted up
* and to the left by XOffset and YOffset pixels at the beginning.
*/
static uae_u32 REGPARAM2 picasso_BlitPattern (TrapContext *ctx)
{
    struct regstruct *regs = &ctx->regs;
    uaecptr rinf = m68k_areg (regs, 1);
    uaecptr pinf = m68k_areg (regs, 2);
    unsigned long X = (uae_u16)m68k_dreg (regs, 0);
    unsigned long Y = (uae_u16)m68k_dreg (regs, 1);
    unsigned long W = (uae_u16)m68k_dreg (regs, 2);
    unsigned long H = (uae_u16)m68k_dreg (regs, 3);
    uae_u8 Mask = (uae_u8)m68k_dreg (regs, 4);
    uae_u32 RGBFmt = m68k_dreg (regs, 7);
    uae_u8 Bpp = GetBytesPerPixel (RGBFmt);
    int inversion = 0;
    struct RenderInfo ri;
    struct Pattern pattern;
    unsigned long rows;
    uae_u8 *uae_mem;
    int xshift;
    unsigned long ysize_mask;
    uae_u32 result = 0;

    if (NOBLITTER)
	return 1;

    if(CopyRenderInfoStructureA2U (rinf, &ri) && CopyPatternStructureA2U (pinf, &pattern)) {
	Bpp = GetBytesPerPixel(ri.RGBFormat);
	uae_mem = ri.Memory + Y * ri.BytesPerRow + X * Bpp; /* offset with address */

	if (pattern.DrawMode & INVERS)
	    inversion = 1;

	pattern.DrawMode &= 0x03;
	if (Mask != 0xFF) {
	    if(Bpp > 1)
		Mask = 0xFF;
	    result = 1;
	} else {
	    result = 1;
	}

	if(result) {
	    uae_u32 fgpen, bgpen;
	    P96TRACE(("BlitPattern() xy(%d,%d), wh(%d,%d) draw 0x%x, off(%d,%d), ph %d\n",
	    X, Y, W, H, pattern.DrawMode, pattern.XOffset, pattern.YOffset, 1 << pattern.Size));

#if P96TRACING_ENABLED
	    DumpPattern(&pattern);
#endif
	    ysize_mask = (1 << pattern.Size) - 1;
	    xshift = pattern.XOffset & 15;

	    fgpen = pattern.FgPen;
	    endianswap (&fgpen, Bpp);
	    bgpen = pattern.BgPen;
	    endianswap (&bgpen, Bpp);

	    for (rows = 0; rows < H; rows++, uae_mem += ri.BytesPerRow) {
		unsigned long prow = (rows + pattern.YOffset) & ysize_mask;
		unsigned int d = do_get_mem_word (((uae_u16 *)pattern.Memory) + prow);
		uae_u8 *uae_mem2 = uae_mem;
		unsigned long cols;

		if (xshift != 0)
		    d = (d << xshift) | (d >> (16 - xshift));

		for (cols = 0; cols < W; cols += 16, uae_mem2 += Bpp * 16) {
		    long bits;
		    long max = W - cols;
		    unsigned int data = d;

		    if (max > 16)
			max = 16;

		    switch (pattern.DrawMode)
		    {
			case JAM1:
			{
			    for (bits = 0; bits < max; bits++) {
				int bit_set = data & 0x8000;
				data <<= 1;
				if (inversion)
				    bit_set = !bit_set;
				if (bit_set)
				    PixelWrite (uae_mem2, bits, fgpen, Bpp, Mask);
			    }
			    break;
			}
			case JAM2:
			{
			    for (bits = 0; bits < max; bits++) {
				int bit_set = data & 0x8000;
				data <<= 1;
				if (inversion)
				    bit_set = !bit_set;
				PixelWrite (uae_mem2, bits, bit_set ? fgpen : bgpen, Bpp, Mask);
			    }
			    break;
			}
			case COMP:
			{
			    for (bits = 0; bits < max; bits++) {
				int bit_set = data & 0x8000;
				data <<= 1;
				if (bit_set) {
				    switch (Bpp)
				    {
					case 1:
					{
					    uae_mem2[bits] ^= 0xff & Mask;
					}
					break;
					case 2:
					{
					    uae_u16 *addr = (uae_u16 *)uae_mem2;
					    addr[bits] ^= 0xffff;
					}
					break;
					case 3:
					{
					    uae_u32 *addr = (uae_u32 *)(uae_mem2 + bits * 3);
					    do_put_mem_long (addr, do_get_mem_long (addr) ^ 0x00ffffff);
					}
					break;
					case 4:
					{
					    uae_u32 *addr = (uae_u32 *)uae_mem2;
					    addr[bits] ^= 0xffffffff;
					}
					break;
				    }
				}
			    }
			    break;
			}
		    }
		}
	    }
	    result = 1;
	}
    }

    return result;
}

/*************************************************
BlitTemplate:
**************************************************
* Synopsis: BlitTemplate(bi, ri, template, X, Y, Width, Height, Mask, RGBFormat);
* a0: struct BoardInfo *bi
* a1: struct RenderInfo *ri
* a2: struct Template *template
* d0.w: X
* d1.w: Y
* d2.w: Width
* d3.w: Height
* d4.w: Mask
* d7.l: RGBFormat
*
* This function is used to paint a template on the board memory using the blitter.
* It is called by BltPattern and BltTemplate. The template consists of a b/w image
* using a single plane of image data which will be expanded to the destination RGBFormat
* using ForeGround and BackGround pens as well as draw modes.
***********************************************************************************/
static uae_u32 REGPARAM2 picasso_BlitTemplate (TrapContext *ctx)
{
    struct regstruct *regs = &ctx->regs;
    uae_u8 inversion = 0;
    uaecptr rinf = m68k_areg (regs, 1);
    uaecptr tmpl = m68k_areg (regs, 2);
    unsigned long X = (uae_u16)m68k_dreg (regs, 0);
    unsigned long Y = (uae_u16)m68k_dreg (regs, 1);
    unsigned long W = (uae_u16)m68k_dreg (regs, 2);
    unsigned long H = (uae_u16)m68k_dreg (regs, 3);
    uae_u16 Mask = (uae_u16)m68k_dreg (regs, 4);
    struct Template tmp;
    struct RenderInfo ri;
    unsigned long rows;
    int bitoffset;
    uae_u8 *uae_mem, Bpp;
    uae_u8 *tmpl_base;
    uae_u32 result = 0;

    if (NOBLITTER)
	return 0;

    if (CopyRenderInfoStructureA2U (rinf, &ri) && CopyTemplateStructureA2U (tmpl, &tmp)) {
	Bpp = GetBytesPerPixel (ri.RGBFormat);
	uae_mem = ri.Memory + Y * ri.BytesPerRow + X * Bpp; /* offset into address */

	if (tmp.DrawMode & INVERS)
	    inversion = 1;

	tmp.DrawMode &= 0x03;

	if (Mask != 0xFF) {
	    if(Bpp > 1)
		Mask = 0xFF;
	    if(tmp.DrawMode == COMP) {
		write_log ("WARNING - BlitTemplate() has unhandled mask 0x%x with COMP DrawMode. Using fall-back routine.\n", Mask);
		return 0;
	    } else {
		result = 1;
	    }
	} else {
	    result = 1;
	}

	if(result) {
	    uae_u32 fgpen, bgpen;

	    P96TRACE(("BlitTemplate() xy(%d,%d), wh(%d,%d) draw 0x%x fg 0x%x bg 0x%x \n",
		X, Y, W, H, tmp.DrawMode, tmp.FgPen, tmp.BgPen));

	    bitoffset = tmp.XOffset % 8;

#if P96TRACING_ENABLED && P96TRACING_LEVEL > 0
	    DumpTemplate(&tmp, W, H);
#endif

	    tmpl_base = tmp.Memory + tmp.XOffset / 8;

	    fgpen = tmp.FgPen;
	    endianswap (&fgpen, Bpp);
	    bgpen = tmp.BgPen;
	    endianswap (&bgpen, Bpp);

	    for (rows = 0; rows < H; rows++, uae_mem += ri.BytesPerRow, tmpl_base += tmp.BytesPerRow) {
		unsigned long cols;
		uae_u8 *tmpl_mem = tmpl_base;
		uae_u8 *uae_mem2 = uae_mem;
		unsigned int data = *tmpl_mem;

		for (cols = 0; cols < W; cols += 8, uae_mem2 += Bpp * 8) {
		    unsigned int byte;
		    long bits;
		    long max = W - cols;

		    if (max > 8)
			max = 8;

		    data <<= 8;
		    data |= *++tmpl_mem;

		    byte = data >> (8 - bitoffset);

		    switch (tmp.DrawMode)
		    {
			case JAM1:
			{
			    for (bits = 0; bits < max; bits++) {
				int bit_set = (byte & 0x80);
				byte <<= 1;
				if (inversion)
				    bit_set = !bit_set;
				if (bit_set)
				    PixelWrite (uae_mem2, bits, fgpen, Bpp, Mask);
			    }
			    break;
			}
			case JAM2:
			{
			    for (bits = 0; bits < max; bits++) {
				int bit_set = (byte & 0x80);
				byte <<= 1;
				if (inversion)
				    bit_set = !bit_set;
   				PixelWrite (uae_mem2, bits, bit_set ? fgpen : bgpen, Bpp, Mask);
			    }
			    break;
			}
			case COMP:
			{
			    for (bits = 0; bits < max; bits++) {
				int bit_set = (byte & 0x80);
				byte <<= 1;
				if (bit_set) {
				    switch (Bpp)
				    {
					case 1:
					{
					    uae_u8 *addr = uae_mem2;
					    addr[bits] ^= 0xff;
					}
					break;
					case 2:
					{
					    uae_u16 *addr = (uae_u16 *)uae_mem2;
					    addr[bits] ^= 0xffff;
					}
					break;
					case 3:
					{
					    uae_u32 *addr = (uae_u32 *)(uae_mem2 + bits * 3);
					    do_put_mem_long (addr, do_get_mem_long (addr) ^ 0x00FFFFFF);
					}
					break;
					case 4:
					{
					    uae_u32 *addr = (uae_u32 *)uae_mem2;
					    addr[bits] ^= 0xffffffff;
					}
					break;
				    }
				}
			    }
			    break;
			}
		    }
		}
	    }
	    result = 1;
	}
    }

    return 1;
}

/*
* CalculateBytesPerRow:
* a0:	struct BoardInfo
* d0:	uae_u16 Width
* d7:	RGBFTYPE RGBFormat
* This function calculates the amount of bytes needed for a line of
* "Width" pixels in the given RGBFormat.
*/
static uae_u32 REGPARAM2 picasso_CalculateBytesPerRow (TrapContext *ctx)
{
    struct regstruct *regs = &ctx->regs;
    uae_u16 width = m68k_dreg (regs, 0);
    uae_u32 type = m68k_dreg (regs, 7);
    width = GetBytesPerPixel (type) * width;
    return width;
}

/*
* SetDisplay:
* a0: struct BoardInfo
* d0: BOOL state
* This function enables and disables the video display.
*
* NOTE: return the opposite of the state
*/
static uae_u32 REGPARAM2 picasso_SetDisplay (TrapContext *ctx)
{
    struct regstruct *regs = &ctx->regs;
    uae_u32 state = m68k_dreg (regs, 0);
    P96TRACE (("SetDisplay(%d)\n", state));
    return !state;
}

void picasso_handle_hsync (void)
{
    static int p96hsync;

    if (currprefs.gfxmem_size == 0)
	return;
    if (WIN32GFX_IsPicassoScreen () && currprefs.gfx_pfullscreen && currprefs.gfx_pvsync) {
	if (DirectDraw_GetVerticalBlankStatus ())
	    p96hsync = 0;
    } else {
	p96hsync--;
    }
    if (p96hsync <= 0) {
	if (uae_boot_rom) {
	    int off = get_long (rtarea_base + 36) + 12 - 1;
	    if (off >= 0 && off < 0x10000)
		rtarea[off]++;
	}
	p96hsync = p96syncrate;
    }
}

void init_hz_p96 (void)
{
    int rate;
    p96syncrate = maxvpos * vblank_hz;
    if (isfullscreen () > 0)
	rate = DirectDraw_CurrentRefreshRate ();
    else
	rate = abs (currprefs.gfx_refreshrate);
    if (rate <= 0)
	rate = 60;
    p96syncrate /= rate;
}

/* NOTE: Watch for those planeptrs of 0x00000000 and 0xFFFFFFFF for all zero / all one bitmaps !!!! */
static void PlanarToChunky (struct RenderInfo *ri, struct BitMap *bm,
			   unsigned long srcx, unsigned long srcy,
			   unsigned long dstx, unsigned long dsty,
			   unsigned long width, unsigned long height,
			   uae_u8 mask)
{
    int j;

    uae_u8 *PLANAR[8], *image = ri->Memory + dstx * GetBytesPerPixel (ri->RGBFormat) + dsty * ri->BytesPerRow;
    int Depth = bm->Depth;
    unsigned long rows, bitoffset = srcx & 7;
    long eol_offset;

    /* Set up our bm->Planes[] pointers to the right horizontal offset */
    for (j = 0; j < Depth; j++) {
	uae_u8 *p = bm->Planes[j];
	if (p != &all_zeros_bitmap && p != &all_ones_bitmap)
	    p += srcx / 8 + srcy * bm->BytesPerRow;
	PLANAR[j] = p;
	if ((mask & (1 << j)) == 0)
	    PLANAR[j] = &all_zeros_bitmap;
    }
    eol_offset = (long)bm->BytesPerRow - (long)((width + 7) >> 3);
    for (rows = 0; rows < height; rows++, image += ri->BytesPerRow) {
	unsigned long cols;

	for (cols = 0; cols < width; cols += 8) {
	    int k;
	    uae_u32 a = 0, b = 0;
	    unsigned int msk = 0xFF;
	    long tmp = cols + 8 - width;
	    if (tmp > 0) {
		msk <<= tmp;
		b = do_get_mem_long ((uae_u32 *)(image + cols + 4));
		if (tmp < 4)
		    b &= 0xFFFFFFFF >> (32 - tmp * 8);
		else if (tmp > 4) {
		    a = do_get_mem_long ((uae_u32 *)(image + cols));
		    a &= 0xFFFFFFFF >> (64 - tmp * 8);
		}
	    }
	    for (k = 0; k < Depth; k++) {
		unsigned int data;
		if (PLANAR[k] == &all_zeros_bitmap)
		    data = 0;
		else if (PLANAR[k] == &all_ones_bitmap)
		    data = 0xFF;
		else {
		    data = (uae_u8)(do_get_mem_word ((uae_u16 *)PLANAR[k]) >> (8 - bitoffset));
		    PLANAR[k]++;
		}
		data &= msk;
		a |= p2ctab[data][0] << k;
		b |= p2ctab[data][1] << k;
	    }
	    do_put_mem_long ((uae_u32 *)(image + cols), a);
	    do_put_mem_long ((uae_u32 *)(image + cols + 4), b);
	}
	for (j = 0; j < Depth; j++) {
	    if (PLANAR[j] != &all_zeros_bitmap && PLANAR[j] != &all_ones_bitmap) {
		PLANAR[j] += eol_offset;
	    }
	}
    }
}

/*
* BlitPlanar2Chunky:
* a0: struct BoardInfo *bi
* a1: struct BitMap *bm - source containing planar information and assorted details
* a2: struct RenderInfo *ri - dest area and its details
* d0.w: SrcX
* d1.w: SrcY
* d2.w: DstX
* d3.w: DstY
* d4.w: SizeX
* d5.w: SizeY
* d6.b: MinTerm - uh oh!
* d7.b: Mask - uh oh!
*
* This function is currently used to blit from planar bitmaps within system memory to chunky bitmaps
* on the board. Watch out for plane pointers that are 0x00000000 (represents a plane with all bits "0")
* or 0xffffffff (represents a plane with all bits "1").
*/
static uae_u32 REGPARAM2 picasso_BlitPlanar2Chunky (TrapContext *ctx)
{
    struct regstruct *regs = &ctx->regs;
    uaecptr bm = m68k_areg (regs, 1);
    uaecptr ri = m68k_areg (regs, 2);
    unsigned long srcx = (uae_u16)m68k_dreg (regs, 0);
    unsigned long srcy = (uae_u16)m68k_dreg (regs, 1);
    unsigned long dstx = (uae_u16)m68k_dreg (regs, 2);
    unsigned long dsty = (uae_u16)m68k_dreg (regs, 3);
    unsigned long width = (uae_u16)m68k_dreg (regs, 4);
    unsigned long height = (uae_u16)m68k_dreg (regs, 5);
    uae_u8 minterm = m68k_dreg (regs, 6) & 0xFF;
    uae_u8 mask = m68k_dreg (regs, 7) & 0xFF;
    struct RenderInfo local_ri;
    struct BitMap local_bm;
    uae_u32 result = 0;

    if (NOBLITTER)
	return 0;
    if (minterm != 0x0C) {
	write_log ("ERROR - BlitPlanar2Chunky() has minterm 0x%x, which I don't handle. Using fall-back routine.\n",
	    minterm);
    } else if(CopyRenderInfoStructureA2U (ri, &local_ri) && CopyBitMapStructureA2U (bm, &local_bm)) {
	P96TRACE(("BlitPlanar2Chunky(%d, %d, %d, %d, %d, %d) Minterm 0x%x, Mask 0x%x, Depth %d\n",
	    srcx, srcy, dstx, dsty, width, height, minterm, mask, local_bm.Depth));
	P96TRACE(("P2C - BitMap has %d BPR, %d rows\n", local_bm.BytesPerRow, local_bm.Rows));
	PlanarToChunky (&local_ri, &local_bm, srcx, srcy, dstx, dsty, width, height, mask);
	result = 1;
    }

    return result;
}

/* NOTE: Watch for those planeptrs of 0x00000000 and 0xFFFFFFFF for all zero / all one bitmaps !!!! */
static void PlanarToDirect (struct RenderInfo *ri, struct BitMap *bm,
			   unsigned long srcx, unsigned long srcy,
			   unsigned long dstx, unsigned long dsty,
			   unsigned long width, unsigned long height, uae_u8 mask,
			   struct ColorIndexMapping *cim)
{
    int j;
    int bpp = GetBytesPerPixel (ri->RGBFormat);
    uae_u8 *PLANAR[8];
    uae_u8 *image = ri->Memory + dstx * bpp + dsty * ri->BytesPerRow;
    int Depth = bm->Depth;
    unsigned long rows;
    long eol_offset;

    if(!bpp)
	return;

    /* Set up our bm->Planes[] pointers to the right horizontal offset */
    for (j = 0; j < Depth; j++) {
	uae_u8 *p = bm->Planes[j];
	if (p != &all_zeros_bitmap && p != &all_ones_bitmap)
	    p += srcx / 8 + srcy * bm->BytesPerRow;
	PLANAR[j] = p;
	if ((mask & (1 << j)) == 0)
	    PLANAR[j] = &all_zeros_bitmap;
    }

    eol_offset = (long)bm->BytesPerRow - (long)((width + (srcx & 7)) >> 3);
    for (rows = 0; rows < height; rows++, image += ri->BytesPerRow) {
	unsigned long cols;
	uae_u8 *image2 = image;
	unsigned int bitoffs = 7 - (srcx & 7);
	int i;

	for (cols = 0; cols < width; cols ++) {
	    int v = 0, k;
	    for (k = 0; k < Depth; k++) {
		if (PLANAR[k] == &all_ones_bitmap)
		    v |= 1 << k;
		else if (PLANAR[k] != &all_zeros_bitmap) {
		    v |= ((*PLANAR[k] >> bitoffs) & 1) << k;
		}
	    }
	    switch (bpp)
	    {
		case 2:
		    ((uae_u16 *)image2)[0] = (uae_u16)(cim->Colors[v]);
		    image2 += 2;
		break;
		case 3:
		    image2[0] = cim->Colors[v] >> 0;
		    image2[1] = cim->Colors[v] >> 8;
		    image2[2] = cim->Colors[v] >> 16;
		    image2 += 3;
		break;
		case 4:
		    ((uae_u32 *)image2)[0] = cim->Colors[v];
		    image2 += 4;
		break;
	    }
	    bitoffs--;
	    bitoffs &= 7;
	    if (bitoffs == 7) {
		int k;
		for (k = 0; k < Depth; k++) {
		    if (PLANAR[k] != &all_zeros_bitmap && PLANAR[k] != &all_ones_bitmap) {
			PLANAR[k]++;
		    }
		}
	    }
	}

	for (i = 0; i < Depth; i++) {
	    if (PLANAR[i] != &all_zeros_bitmap && PLANAR[i] != &all_ones_bitmap) {
		PLANAR[i] += eol_offset;
	    }
	}
    }
}

/*
* BlitPlanar2Direct:
*
* Synopsis:
* BlitPlanar2Direct(bi, bm, ri, cim, SrcX, SrcY, DstX, DstY, SizeX, SizeY, MinTerm, Mask);
* Inputs:
* a0:struct BoardInfo *bi
* a1:struct BitMap *bm
* a2:struct RenderInfo *ri
* a3:struct ColorIndexMapping *cmi
* d0.w:SrcX
* d1.w:SrcY
* d2.w:DstX
* d3.w:DstY
* d4.w:SizeX
* d5.w:SizeY
* d6.b:MinTerm
* d7.b:Mask
*
* This function is currently used to blit from planar bitmaps within system memory to direct color
* bitmaps (15, 16, 24 or 32 bit) on the board. Watch out for plane pointers that are 0x00000000 (represents
* a plane with all bits "0") or 0xffffffff (represents a plane with all bits "1"). The ColorIndexMapping is
* used to map the color index of each pixel formed by the bits in the bitmap's planes to a direct color value
* which is written to the destination RenderInfo. The color mask and all colors within the mapping are words,
* triple bytes or longwords respectively similar to the color values used in FillRect(), BlitPattern() or
* BlitTemplate().
*/
static uae_u32 REGPARAM2 picasso_BlitPlanar2Direct (TrapContext *ctx)
{
    struct regstruct *regs = &ctx->regs;
    uaecptr bm = m68k_areg (regs, 1);
    uaecptr ri = m68k_areg (regs, 2);
    uaecptr cim = m68k_areg (regs, 3);
    unsigned long srcx = (uae_u16)m68k_dreg (regs, 0);
    unsigned long srcy = (uae_u16)m68k_dreg (regs, 1);
    unsigned long dstx = (uae_u16)m68k_dreg (regs, 2);
    unsigned long dsty = (uae_u16)m68k_dreg (regs, 3);
    unsigned long width = (uae_u16)m68k_dreg (regs, 4);
    unsigned long height = (uae_u16)m68k_dreg (regs, 5);
    uae_u8 minterm = m68k_dreg (regs, 6);
    uae_u8 Mask = m68k_dreg (regs, 7);
    struct RenderInfo local_ri;
    struct BitMap local_bm;
    struct ColorIndexMapping local_cim;
    uae_u32 result = 0;

    if (NOBLITTER)
	return 0;
    if (minterm != 0x0C) {
	write_log ("WARNING - BlitPlanar2Direct() has unhandled op-code 0x%x. Using fall-back routine.\n",
	    minterm);
    }
    else if(CopyRenderInfoStructureA2U (ri, &local_ri) &&
	     CopyBitMapStructureA2U (bm, &local_bm))
    {
	Mask = 0xFF;
	CopyColorIndexMappingA2U (cim, &local_cim, GetBytesPerPixel (local_ri.RGBFormat));
	P96TRACE(("BlitPlanar2Direct(%d, %d, %d, %d, %d, %d) Minterm 0x%x, Mask 0x%x, Depth %d\n",
	    srcx, srcy, dstx, dsty, width, height, minterm, Mask, local_bm.Depth));
	PlanarToDirect (&local_ri, &local_bm, srcx, srcy, dstx, dsty, width, height, Mask, &local_cim);
	result = 1;
    }
    return result;
}

STATIC_INLINE void copyrow (uae_u8 *src, uae_u8 *dst, int x, int y, int width)
{
    uae_u8 *src2 = src + y * picasso96_state.BytesPerRow;
    uae_u8 *dst2 = dst + y * picasso_vidinfo.rowbytes;
    int endx = x + width;
    int dstpix = picasso_vidinfo.pixbytes;
    int srcpix = picasso96_state.BytesPerPixel;

    if (picasso96_state.RGBFormat == host_mode) {
        memcpy (dst2 + x * dstpix, src2 + x * srcpix, width * dstpix);
        return;
    }

    switch (picasso_convert)
    {
	case RGBFB_B8G8R8A8_32:
	case RGBFB_B5G6R5PC_16:
	case RGBFB_R5G6B5PC_16:
	case RGBFB_CLUT_8:
	    memcpy (dst2 + x * dstpix, src2 + x * srcpix, width * dstpix);
	break;

	case RGBFB_R8G8B8_32:
	    while (x < endx) {
		((uae_u32*)dst2)[x] = (src2[x * 3 + 0] << 16) | (src2[x * 3 + 1] << 8) | (src2[x * 3 + 2] << 0);
		x++;
	    }
	break;
	case RGBFB_B8G8R8_32:
    	    while (x < endx) {
		((uae_u32*)dst2)[x] = ((uae_u32*)(src2 + x * 3))[0] & 0x00ffffff;
		x++;
	    }
	break;
	case RGBFB_R8G8B8A8_32:
	    while (x < endx) {
		((uae_u32*)dst2)[x] = (src2[x * 4 + 0] << 16) | (src2[x * 4 + 1] << 8) | (src2[x * 4 + 2] << 0);
		x++;
	    }
	break;
	case RGBFB_A8R8G8B8_32:
	    while (x < endx) {
		((uae_u32*)dst2)[x] = (src2[x * 4 + 1] << 16) | (src2[x * 4 + 2] << 8) | (src2[x * 4 + 3] << 0);
		x++;
	    }
	break;
	case RGBFB_A8B8G8R8_32:
	    while (x < endx) {
		((uae_u32*)dst2)[x] = ((uae_u32*)src2)[0] << 8;
		x++;
	    }
	break;

	case RGBFB_R5G6B5PC_32:
	case RGBFB_R5G5B5PC_32:
	case RGBFB_R5G6B5_32:
	case RGBFB_R5G5B5_32:
	case RGBFB_B5G6R5PC_32:
	case RGBFB_B5G5R5PC_32:
	    while (x < endx) {
		((uae_u32*)dst2)[x] = p96_rgbx16[((uae_u16*)src2)[x]];
		x++;
		((uae_u32*)dst2)[x] = p96_rgbx16[((uae_u16*)src2)[x]];
		x++;
		((uae_u32*)dst2)[x] = p96_rgbx16[((uae_u16*)src2)[x]];
		x++;
		((uae_u32*)dst2)[x] = p96_rgbx16[((uae_u16*)src2)[x]];
		x++;
	    }
	break;

	case RGBFB_R5G5B5PC_16:
	case RGBFB_R5G6B5_16:
	case RGBFB_R5G5B5_16:
	case RGBFB_B5G5R5PC_16:
	    while (x < endx) {
		((uae_u16*)dst2)[x] = (uae_u16)p96_rgbx16[((uae_u16*)src2)[x]];
		x++;
		((uae_u16*)dst2)[x] = (uae_u16)p96_rgbx16[((uae_u16*)src2)[x]];
		x++;
		((uae_u16*)dst2)[x] = (uae_u16)p96_rgbx16[((uae_u16*)src2)[x]];
		x++;
		((uae_u16*)dst2)[x] = (uae_u16)p96_rgbx16[((uae_u16*)src2)[x]];
		x++;
	    }
	break;

	case RGBFB_R8G8B8_16:
	break;
	case RGBFB_B8G8R8_16:
	break;
	case RGBFB_R8G8B8A8_16:
	break;
	case RGBFB_A8R8G8B8_16:
	break;
	case RGBFB_A8B8G8R8_16:
	break;

	case RGBFB_B8G8R8A8_16:
	    while (x < endx) {
		uae_u32 v;
		v = ((uae_u32*)src2)[x];
		((uae_u16*)dst2)[x] = p96_rgbx16[(((v >> (16 + 3)) & 0x1f) << 11) | (((v >> (8 + 2)) & 0x3f) << 5) | (((v >> (0 + 3)) & 0x1f) << 0)];
		x++;
	    }
	break;

	case RGBFB_CLUT_RGBFB_32:
	    while (x < endx) {
		((uae_u32*)dst2)[x] = picasso_vidinfo.clut[src2[x]];
		x++;
		((uae_u32*)dst2)[x] = picasso_vidinfo.clut[src2[x]];
		x++;
		((uae_u32*)dst2)[x] = picasso_vidinfo.clut[src2[x]];
		x++;
		((uae_u32*)dst2)[x] = picasso_vidinfo.clut[src2[x]];
		x++;
	    }
	break;
	case RGBFB_CLUT_RGBFB_16:
	    while (x < endx) {
		((uae_u16*)dst2)[x] = picasso_vidinfo.clut[src2[x]];
		x++;
		((uae_u16*)dst2)[x] = picasso_vidinfo.clut[src2[x]];
		x++;
		((uae_u16*)dst2)[x] = picasso_vidinfo.clut[src2[x]];
		x++;
		((uae_u16*)dst2)[x] = picasso_vidinfo.clut[src2[x]];
		x++;
    	    }
	break;
    }
}

static void copyall (uae_u8 *src, uae_u8 *dst)
{
    int y;

    if (picasso96_state.RGBFormat == host_mode) {
	int w = picasso96_state.Width * picasso_vidinfo.pixbytes;
	for (y = 0; y < picasso96_state.Height; y++) {
	    memcpy (dst, src, w);
	    dst += picasso_vidinfo.rowbytes;
	    src += picasso96_state.BytesPerRow;
	}
    } else {
	for (y = 0; y < picasso96_state.Height; y++)
	    copyrow (src, dst, 0, y, picasso96_state.Width);
    }
}

static void flushpixels (void)
{
    int i;
    int rowwidth_src = picasso96_state.Width * picasso96_state.BytesPerPixel;
    uae_u8 *src = p96ram_start + natmem_offset;
    int off = picasso96_state.XYOffset - gfxmem_start;
    uae_u8 *src_start = src + (off & ~gwwpagemask);
    uae_u8 *src_end = src + ((off + rowwidth_src * picasso96_state.Height + gwwpagesize - 1) & ~gwwpagemask);
    int maxy = -1;
    int miny = picasso96_state.Height - 1;
    int lock = 0;
    uae_u8 *dst = NULL;
    ULONG_PTR gwwcnt;


    if (!picasso_vidinfo.extra_mem || !gwwbuf || src_start >= src_end)
	return;

    if (palette_changed)
	palette_changed = -1;

    for (;;) {
	int dofull;

	gwwcnt = 0;

	if (doskip () && p96skipmode == 1)
	    break;

	if (palette_changed < 0) {
	    palette_changed = 1;
	    if (picasso_palette ()) {
		reloadcursor = 1;
		setspriteimage (cursorbi);
	    }
	    gwwcnt = allocated_gfxmem / gwwpagesize;
	    for (i = 0; i < gwwcnt; i++)
		gwwbuf[i] = src + i * gwwpagesize;
	} else {
	    ULONG ps;
	    gwwcnt = gwwbufsize;
	    if (mman_GetWriteWatch (src_start, src_end - src_start, gwwbuf, &gwwcnt, &ps))
		break;
	}

	if (gwwcnt == 0)
	    break;

	dofull = gwwcnt >= ((src_end - src_start) / gwwpagesize) * 80 / 100;

#if P96DX > 0
	if (p96surface && dofull && picasso_vidinfo.pixbytes == picasso96_state.BytesPerPixel) {
	    flushpixels_surface (gwwbuf, gwwcnt, src_start, src_end);
	    return;
	}
#endif

	if (dst == NULL) {
	    if (DirectDraw_IsLocked() == FALSE) {
		if (!lock)
		    dst = gfx_lock_picasso ();
		lock = 1;
	    } else {
		dst = picasso96_state.HostAddress;
	    }
	}
	if (dst == NULL)
	    break;

	if (doskip () && p96skipmode == 2)
	    break;

	if (dofull) {
	    copyall (src + off, dst);
	    miny = 0;
	    maxy = picasso96_state.Height;
	    break;
	}

	for (i = 0; i < gwwcnt; i++) {
	    uae_u8 *p = gwwbuf[i];

	    if (p >= src_start && p < src_end) {
		int y, x, realoffset;

		if (p >= src + off) {
		    realoffset = p - (src + off);
		} else {
		    realoffset = 0;
		}

		y = realoffset / picasso96_state.BytesPerRow;
		if (y < picasso96_state.Height) {
		    int w = gwwpagesize / picasso96_state.BytesPerPixel;
		    x = (realoffset % picasso96_state.BytesPerRow) / picasso96_state.BytesPerPixel;
		    if (x < picasso96_state.Width)
			copyrow (src + off, dst, x, y, picasso96_state.Width - x);
		    w = (gwwpagesize - (picasso96_state.BytesPerRow - x * picasso96_state.BytesPerPixel)) / picasso96_state.BytesPerPixel;
		    if (y < miny)
			miny = y;
		    y++;
		    while (y < picasso96_state.Height && w > 0) {
			int maxw = w > picasso96_state.Width ? picasso96_state.Width : w;
			copyrow (src + off, dst, 0, y, maxw);
			w -= maxw;
			y++;
		    }
		    if (y > maxy)
			maxy = y;
		}

	    }

	}
	break;
    }

    if(lock)
	gfx_unlock_picasso ();
    if (dst && gwwcnt) {
	if (doskip () && p96skipmode == 3) {
	    ;
	} else {
	    mman_ResetWatch (src_start, src_end - src_start);
	}
	palette_changed = 0;
    }
    if (maxy >= 0) {
	if (doskip () && p96skipmode == 4) {
	    ;
	} else {
	    DX_Invalidate (0, miny, picasso96_state.Width, maxy - miny);
	}
    }
}

static uae_u32 REGPARAM2 gfxmem_lgetx (uaecptr addr)
{
    uae_u32 *m;

    addr -= gfxmem_start & gfxmem_mask;
    addr &= gfxmem_mask;
    m = (uae_u32 *)(gfxmemory + addr);
    return do_get_mem_long(m);
}

static uae_u32 REGPARAM2 gfxmem_wgetx (uaecptr addr)
{
    uae_u16 *m;
    addr -= gfxmem_start & gfxmem_mask;
    addr &= gfxmem_mask;
    m = (uae_u16 *)(gfxmemory + addr);
    return do_get_mem_word(m);
}

static uae_u32 REGPARAM2 gfxmem_bgetx (uaecptr addr)
{
    addr -= gfxmem_start & gfxmem_mask;
    addr &= gfxmem_mask;
    return gfxmemory[addr];
}

static void REGPARAM2 gfxmem_lputx (uaecptr addr, uae_u32 l)
{
    uae_u32 *m;
    addr -= gfxmem_start & gfxmem_mask;
    addr &= gfxmem_mask;
    m = (uae_u32 *)(gfxmemory + addr);
    do_put_mem_long(m, l);
}

static void REGPARAM2 gfxmem_wputx (uaecptr addr, uae_u32 w)
{
    uae_u16 *m;
    addr -= gfxmem_start & gfxmem_mask;
    addr &= gfxmem_mask;
    m = (uae_u16 *)(gfxmemory + addr);
    do_put_mem_word(m, (uae_u16)w);
}

static void REGPARAM2 gfxmem_bputx (uaecptr addr, uae_u32 b)
{
    addr -= gfxmem_start & gfxmem_mask;
    addr &= gfxmem_mask;
    gfxmemory[addr] = b;
}

static int REGPARAM2 gfxmem_check (uaecptr addr, uae_u32 size)
{
    addr -= gfxmem_start & gfxmem_mask;
    addr &= gfxmem_mask;
    return (addr + size) < allocated_gfxmem;
}

static uae_u8 *REGPARAM2 gfxmem_xlate (uaecptr addr)
{
    addr -= gfxmem_start & gfxmem_mask;
    addr &= gfxmem_mask;
    return gfxmemory + addr;
}

addrbank gfxmem_bankx = {
    gfxmem_lgetx, gfxmem_wgetx, gfxmem_bgetx,
    gfxmem_lputx, gfxmem_wputx, gfxmem_bputx,
    gfxmem_xlate, gfxmem_check, NULL, "RTG RAM",
    dummy_lgeti, dummy_wgeti, ABFLAG_RAM
};

/* Call this function first, near the beginning of code flow
* Place in InitGraphics() which seems reasonable...
* Also put it in reset_drawing() for safe-keeping.  */
void InitPicasso96 (void)
{
    int i;

    have_done_picasso = 0;
    palette_changed = 0;
//fastscreen
    oldscr = 0;
//fastscreen
    memset (&picasso96_state, 0, sizeof(struct picasso96_state_struct));

    for (i = 0; i < 256; i++) {
        p2ctab[i][0] = (((i & 128) ? 0x01000000 : 0)
    	    | ((i & 64) ? 0x010000 : 0)
	    | ((i & 32) ? 0x0100 : 0)
	    | ((i & 16) ? 0x01 : 0));
	p2ctab[i][1] = (((i & 8) ? 0x01000000 : 0)
	    | ((i & 4) ? 0x010000 : 0)
	    | ((i & 2) ? 0x0100 : 0)
	    | ((i & 1) ? 0x01 : 0));
    }
}

uae_u8 *restore_p96 (uae_u8 *src)
{
    uae_u32 flags;
    if (restore_u32 () != 1)
	return src;
    InitPicasso96();
    flags = restore_u32();
    picasso_requested_on = !!(flags & 1);
    picasso96_state.SwitchState = picasso_requested_on;
    picasso_on = 0;
    init_picasso_screen_called = 0;
    set_gc_called = !!(flags & 2);
    set_panning_called = !!(flags & 4);
    changed_prefs.gfxmem_size = restore_u32();
    picasso96_state.Address = restore_u32();
    picasso96_state.RGBFormat = restore_u32();
    picasso96_state.Width = restore_u16();
    picasso96_state.Height = restore_u16();
    picasso96_state.VirtualWidth = restore_u16();
    picasso96_state.VirtualHeight = restore_u16();
    picasso96_state.XOffset = restore_u16();
    picasso96_state.YOffset = restore_u16();
    picasso96_state.GC_Depth = restore_u8();
    picasso96_state.GC_Flags = restore_u8();
    picasso96_state.BytesPerRow = restore_u16();
    picasso96_state.BytesPerPixel = restore_u8();
    picasso96_state.HostAddress = NULL;
    picasso_SetPanningInit();
    picasso96_state.Extent = picasso96_state.Address + picasso96_state.BytesPerRow * picasso96_state.VirtualHeight;
    if (set_gc_called) {
	init_picasso_screen ();
	init_hz_p96 ();
    }
    return src;
}

uae_u8 *save_p96 (int *len, uae_u8 *dstptr)
{
    uae_u8 *dstbak, *dst;

    if (currprefs.gfxmem_size == 0)
	return NULL;
    if (dstptr)
	dstbak = dst = dstptr;
    else
	dstbak = dst = malloc (1000);
    save_u32 (1);
    save_u32 ((picasso_on ? 1 : 0) | (set_gc_called ? 2 : 0) | (set_panning_called ? 4 : 0));
    save_u32 (currprefs.gfxmem_size);
    save_u32 (picasso96_state.Address);
    save_u32 (picasso96_state.RGBFormat);
    save_u16 (picasso96_state.Width);
    save_u16 (picasso96_state.Height);
    save_u16 (picasso96_state.VirtualWidth);
    save_u16 (picasso96_state.VirtualHeight);
    save_u16 (picasso96_state.XOffset);
    save_u16 (picasso96_state.YOffset);
    save_u8 (picasso96_state.GC_Depth);
    save_u8 (picasso96_state.GC_Flags);
    save_u16 (picasso96_state.BytesPerRow);
    save_u8 (picasso96_state.BytesPerPixel);
    *len = dst - dstbak;
    return dstbak;
}

#endif

#ifdef UAEGFX_INTERNAL
#define RTGCALL(ABI,func,funcdef,call) \
    put_long (ABI + func, here ()); \
    dl (0x48e78000); \
    calltrap (deftrap (call)); \
    dw (0x4a80); \
    dl (0x4cdf0001);\
    dw (0x6604); \
    dw (0x2f28); \
    dw (funcdef); \
    dw (RTS);

#define RTGCALL2(ABI,func,call) \
    put_long (ABI + func, here ()); \
    calltrap (deftrap (call)); \
    dw (RTS);

#define RTGCALLDEFAULT(ABI,func,funcdef) \
    put_long (ABI + func, here ()); \
    dw (0x2f28); \
    dw (funcdef); \
    dw (RTS);

#define RTGNONE(ABI,func) \
    put_long (ABI + func, start);

static void inituaegfxfuncs (uaecptr ABI)
{
    uaecptr start = here ();

    dw (RTS);
    /* ResolvePixelClock
	move.l	D0,gmi_PixelClock(a1)	; pass the pixelclock through
	moveq	#0,D0			; index is 0
	move.b	#98,gmi_Numerator(a1)	; whatever
	move.b	#14,gmi_Denominator(a1)	; whatever
	rts
    */
    put_long (ABI + PSSO_BoardInfo_ResolvePixelClock, here ());
    dl (0x2340002c);
    dw (0x7000);
    dl (0x137c0062); dw (0x002a);
    dl (0x137c000e); dw (0x002b);
    dw (RTS);

    /* GetPixelClock
	move.l #CLOCK,D0 ; fill in D0 with our one true pixel clock
	rts
    */
    put_long (ABI + PSSO_BoardInfo_GetPixelClock, here ());
    dw (0x203c);
    dl (100227260);
    dw (RTS);

    /* CalculateMemory
	; this is simple, because we're not supporting planar modes in UAE
	move.l	a1,d0
	rts
    */
    put_long (ABI + PSSO_BoardInfo_CalculateMemory, here ());
    dw (0x2009);
    dw (RTS);

    /* GetCompatibleFormats
	; all formats can coexist without any problems, since we don't support planar stuff in UAE
	move.l	#~RGBFF_PLANAR,d0
	rts
    */
    put_long (ABI + PSSO_BoardInfo_GetCompatibleFormats, here ());
    dw (0x203c);
    dl (0xfffffffe);
    dw (RTS);

    /* CalculateBytesPerRow (optimized) */
    put_long (ABI + PSSO_BoardInfo_CalculateBytesPerRow, here ());
    dl (0x0c400140); // cmp.w #320,d0
    dw (0x6504); // bcs.s .l1
    calltrap (deftrap (picasso_CalculateBytesPerRow));
    dw (RTS);
    dw (0x0c87); dl (0x00000010); // l1: cmp.l #10,d7
    dw (0x640a); // bcc.s .l2
    dw (0x7200); // moveq #0,d1
    dl (0x123b7010); // move.b table(pc,d7.w),d1
    dw (0x6b04); // bmi.s l3
    dw (0xe368); // lsl.w d1,d0
    dw (RTS); // .l2
    dw (0x3200); // .l3 move.w d0,d1
    dw (0xd041);  // add.w d1,d0
    dw (0xd041); // add.w d1,d0
    dw (RTS);
    dl (0x0000ffff); // table
    dl (0x01010202);
    dl (0x02020101);
    dl (0x01010100);

    RTGNONE(ABI, PSSO_BoardInfo_SetClock);
    RTGNONE(ABI, PSSO_BoardInfo_SetMemoryMode);
    RTGNONE(ABI, PSSO_BoardInfo_SetWriteMask);
    RTGNONE(ABI, PSSO_BoardInfo_SetClearMask);
    RTGNONE(ABI, PSSO_BoardInfo_SetReadPlane);

    RTGNONE(ABI, PSSO_BoardInfo_WaitVerticalSync); /* FIXME */
    RTGNONE(ABI, PSSO_BoardInfo_WaitBlitter);

#if 0
    RTGCALL2(ABI, PSSO_BoardInfo_, picasso_);
    RTGCALL(ABI, PSSO_BoardInfo_, PSSO_BoardInfo_Default, picasso_);
    RTGCALLDEFAULT(ABI, PSSO_BoardInfo_, PSSO_BoardInfo_Default);
#endif

    RTGCALL(ABI, PSSO_BoardInfo_BlitPlanar2Direct, PSSO_BoardInfo_BlitPlanar2DirectDefault, picasso_BlitPlanar2Direct);
    RTGCALL(ABI, PSSO_BoardInfo_FillRect, PSSO_BoardInfo_FillRectDefault, picasso_FillRect);
    RTGCALL(ABI, PSSO_BoardInfo_BlitRect, PSSO_BoardInfo_BlitRectDefault, picasso_BlitRect);
    RTGCALL(ABI, PSSO_BoardInfo_BlitPlanar2Chunky, PSSO_BoardInfo_BlitPlanar2ChunkyDefault, picasso_BlitPlanar2Chunky);
    RTGCALL(ABI, PSSO_BoardInfo_BlitTemplate, PSSO_BoardInfo_BlitTemplateDefault, picasso_BlitTemplate);
    RTGCALL(ABI, PSSO_BoardInfo_InvertRect, PSSO_BoardInfo_InvertRectDefault, picasso_InvertRect);
    RTGCALL(ABI, PSSO_BoardInfo_BlitRectNoMaskComplete, PSSO_BoardInfo_BlitRectNoMaskCompleteDefault, picasso_BlitRectNoMaskComplete);
    RTGCALL(ABI, PSSO_BoardInfo_BlitPattern, PSSO_BoardInfo_BlitPatternDefault, picasso_BlitPattern);

    RTGCALL2(ABI, PSSO_BoardInfo_SetSwitch, picasso_SetSwitch);
    RTGCALL2(ABI, PSSO_BoardInfo_SetColorArray, picasso_SetColorArray);
    RTGCALL2(ABI, PSSO_BoardInfo_SetDAC, picasso_SetDAC);
    RTGCALL2(ABI, PSSO_BoardInfo_SetGC, picasso_SetGC);
    RTGCALL2(ABI, PSSO_BoardInfo_SetPanning, picasso_SetPanning);
    RTGCALL2(ABI, PSSO_BoardInfo_SetDisplay, picasso_SetDisplay);

    RTGCALL2(ABI, PSSO_BoardInfo_SetSprite, picasso_SetSprite);
    RTGCALL2(ABI, PSSO_BoardInfo_SetSpritePosition, picasso_SetSpritePosition);
    RTGCALL2(ABI, PSSO_BoardInfo_SetSpriteImage, picasso_SetSpriteImage);
    RTGCALL2(ABI, PSSO_BoardInfo_SetSpriteColor, picasso_SetSpriteColor);

    RTGCALLDEFAULT(ABI, PSSO_BoardInfo_ScrollPlanar, PSSO_BoardInfo_ScrollPlanarDefault);
    RTGCALLDEFAULT(ABI, PSSO_BoardInfo_UpdatePlanar, PSSO_BoardInfo_UpdatePlanarDefault);
    RTGCALLDEFAULT(ABI, PSSO_BoardInfo_DrawLine, PSSO_BoardInfo_DrawLineDefault);

    write_log ("uaegfx.card magic code: %08X-%08X\n", start, here ());
}

#define UAEGFX_VERSION 3
#define UAEGFX_REVISION 0

static uae_u32 REGPARAM2 gfx_open (TrapContext *context)
{
    put_word (uaegfx_base + 32, get_word (uaegfx_base + 32) + 1);
    return uaegfx_base;
}
static uae_u32 REGPARAM2 gfx_close (TrapContext *context)
{
    put_word (uaegfx_base + 32, get_word (uaegfx_base + 32) - 1);
    return 0;
}
static uae_u32 REGPARAM2 gfx_expunge (TrapContext *context)
{
    return 0;
}

static uaecptr uaegfx_card_install (TrapContext *ctx, uae_u32 extrasize)
{
    uae_u32 functable, datatable, a2;
    uaecptr openfunc, closefunc, expungefunc;
    uaecptr findcardfunc, initcardfunc;
    uaecptr exec = get_long (4);

    uaegfx_resid = ds ("UAE Graphics Card 3.0");

    /* Open */
    openfunc = here ();
    calltrap (deftrap (gfx_open)); dw (RTS);

    /* Close */
    closefunc = here ();
    calltrap (deftrap (gfx_close)); dw (RTS);

    /* Expunge */
    expungefunc = here ();
    calltrap (deftrap (gfx_expunge)); dw (RTS);

    /* FindCard */
    findcardfunc = here ();
    calltrap (deftrap (picasso_FindCard)); dw (RTS);

    /* InitCard */
    initcardfunc = here ();
    calltrap (deftrap (picasso_InitCard)); dw (RTS);

    functable = here ();
    dl (openfunc);
    dl (closefunc);
    dl (expungefunc);
    dl (EXPANSION_nullfunc);
    dl (findcardfunc);
    dl (initcardfunc);
    dl (0xFFFFFFFF); /* end of table */

    datatable = makedatatable (uaegfx_resid, uaegfx_resname, 0x09, -50, UAEGFX_VERSION, UAEGFX_REVISION);

    a2 = m68k_areg (&ctx->regs, 2);
    m68k_areg (&ctx->regs, 0) = functable;
    m68k_areg (&ctx->regs, 1) = datatable;
    m68k_areg (&ctx->regs, 2) = 0;
    m68k_dreg (&ctx->regs, 0) = CARD_SIZEOF + extrasize;
    m68k_dreg (&ctx->regs, 1) = 0;
    uaegfx_base = CallLib (ctx, exec, -0x54); /* MakeLibrary */
    m68k_areg (&ctx->regs, 2) = a2;
    if (!uaegfx_base)
	return 0;
    m68k_areg (&ctx->regs, 1) = uaegfx_base;
    CallLib (ctx, exec, -0x18c); /* AddLibrary */
    m68k_areg (&ctx->regs, 1) = EXPANSION_explibname;
    m68k_dreg (&ctx->regs, 0) = 0;
    put_long (uaegfx_base + CARD_EXPANSIONBASE, CallLib (ctx, exec, -0x228)); /* OpenLibrary */
    put_long (uaegfx_base + CARD_EXECBASE, exec);
    put_long (uaegfx_base + CARD_NAME, uaegfx_resname);
    put_long (uaegfx_base + CARD_RESLIST, uaegfx_base + CARD_SIZEOF);

    write_log ("uaegfx.card %d.%d init @%08X\n", UAEGFX_VERSION, UAEGFX_REVISION, uaegfx_base);
    return uaegfx_base;
}
#endif

#ifndef UAEGFX_INTERNAL
uae_u32 picasso_demux (uae_u32 arg, TrapContext *context)
{
    switch (arg)
    {
     case 16: return picasso_FindCard (context);
     case 17: return picasso_FillRect (context);
     case 18: return picasso_SetSwitch (context);
     case 19: return picasso_SetColorArray (context);
     case 20: return picasso_SetDAC (context);
     case 21: return picasso_SetGC (context);
     case 22: return picasso_SetPanning (context);
     case 23: return picasso_CalculateBytesPerRow (context);
     case 24: return picasso_BlitPlanar2Chunky (context);
     case 25: return picasso_BlitRect (context);
     case 26: return picasso_SetDisplay (context);
     case 27: return picasso_BlitTemplate (context);
     case 28: return picasso_BlitRectNoMaskComplete (context);
     case 29: return picasso_InitCard (context);
     case 30: return picasso_BlitPattern (context);
     case 31: return picasso_InvertRect (context);
     case 32: return picasso_BlitPlanar2Direct (context);
     /* case 34: return picasso_WaitVerticalSync (); handled in asm-code */
     case 35: return allocated_gfxmem ? 1 : 0;
     case 36: return picasso_SetSprite (context);
     case 37: return picasso_SetSpritePosition (context);
     case 38: return picasso_SetSpriteImage (context);
     case 39: return picasso_SetSpriteColor (context);
    }
    return 0;
}
#endif

#endif


