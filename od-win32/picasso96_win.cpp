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

#define MULTIDISPLAY 0
#define WINCURSOR 1

#include "sysconfig.h"
#include "sysdeps.h"

#if defined(PICASSO96)

#include "options.h"
#include "threaddep/thread.h"
#include "memory_uae.h"
#include "custom.h"
#include "events.h"
#include "newcpu.h"
#include "xwin.h"
#include "savestate.h"
#include "autoconf.h"
#include "traps.h"
#include "native2amiga.h"
#include "drawing.h"
#include "inputdevice.h"
#include "debug.h"
#include "registry.h"
#include "dxwrap.h"
#include "rp.h"
#include "picasso96_win.h"
#include "win32gfx.h"
#include "direct3d.h"
#include "clipboard.h"
#include "gfxboard.h"

int debug_rtg_blitter = 3;

#define NOBLITTER (0 || !(debug_rtg_blitter & 1))
#define NOBLITTER_BLIT (0 || !(debug_rtg_blitter & 2))

#define USE_HARDWARESPRITE 1
#define P96TRACING_ENABLED 0
#define P96SPRTRACING_ENABLED 0

static int hwsprite = 0;
static int picasso96_BT = BT_uaegfx;
static int picasso96_GCT = GCT_Unknown;
static int picasso96_PCT = PCT_Unknown;

int mman_GetWriteWatch (PVOID lpBaseAddress, SIZE_T dwRegionSize, PVOID *lpAddresses, PULONG_PTR lpdwCount, PULONG lpdwGranularity);
void mman_ResetWatch (PVOID lpBaseAddress, SIZE_T dwRegionSize);

int p96refresh_active;
bool have_done_picasso = 1; /* For the JIT compiler */
static int p96syncrate;
int p96hsync_counter, full_refresh;
#if defined(X86_MSVC_ASSEMBLY)
#define SWAPSPEEDUP
#endif
#ifdef PICASSO96
#ifdef DEBUG // Change this to _DEBUG for debugging
#define P96TRACING_ENABLED 1
#define P96TRACING_LEVEL 1
#endif
#if P96TRACING_ENABLED
#define P96TRACE(x) do { write_log x; } while(0)
#else
#define P96TRACE(x)
#endif
#if P96SPRTRACING_ENABLED
#define P96TRACE_SPR(x) do { write_log x; } while(0)
#else
#define P96TRACE_SPR(x)
#endif
#define P96TRACE2(x) do { write_log x; } while(0)

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
uae_u32 p96rc[256], p96gc[256], p96bc[256];

static int cursorwidth, cursorheight, cursorok;
static uae_u8 *cursordata;
static uae_u32 cursorrgb[4], cursorrgbn[4];
static int cursordeactivate, setupcursor_needed;
static bool cursorvisible;
static HCURSOR wincursor;
static int wincursor_shown;
static uaecptr boardinfo, ABI_interrupt;
static int interrupt_enabled;
double p96vblank;
static int rtg_clear_flag;
static bool picasso_active;

static int uaegfx_old, uaegfx_active;
static uae_u32 reserved_gfxmem;
static uaecptr uaegfx_resname,
	uaegfx_resid,
	uaegfx_init,
	uaegfx_base,
	uaegfx_rom;

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
	BLIT_SWAP = 30
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
		addrbank *b = &get_mem_bank (v2);
		if (!b || !b->check (v2, 12))
			continue;
		p = b->xlateaddr(v2);
		if (!memcmp(p, "rtg.library\0", 12)) {
			uae_u16 ver = get_word (v + 20);
			uae_u16 rev = get_word (v + 22);
			if (ver * 10000 + rev < UAE_RTG_LIBRARY_VERSION * 10000 + UAE_RTG_LIBRARY_REVISION) {
				TCHAR msg[2000];
				WIN32GUI_LoadUIString(IDS_OLDRTGLIBRARY, msg, sizeof(msg));
				gui_message(msg, ver, rev, UAE_RTG_LIBRARY_VERSION, UAE_RTG_LIBRARY_REVISION);
			} else {
				write_log (_T("P96: rtg.library %d.%d detected\n"), ver, rev);
			}
			checked = TRUE;
		}
	}
}

static uae_u32 p2ctab[256][2];
static int set_gc_called = 0, init_picasso_screen_called = 0;
//fastscreen
static uaecptr oldscr = 0;


STATIC_INLINE void endianswap (uae_u32 *vp, int bpp)
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
	write_log (_T("ModeInfo Structure Dump:\n"));
	write_log (_T("  Node.ln_Succ  = 0x%x\n"), get_long (amigamodeinfoptr));
	write_log (_T("  Node.ln_Pred  = 0x%x\n"), get_long (amigamodeinfoptr + 4));
	write_log (_T("  Node.ln_Type  = 0x%x\n"), get_byte (amigamodeinfoptr + 8));
	write_log (_T("  Node.ln_Pri   = %d\n"), get_byte (amigamodeinfoptr + 9));
	/*write_log (_T("  Node.ln_Name  = %s\n"), uaememptr->Node.ln_Name); */
	write_log (_T("  OpenCount     = %d\n"), get_word (amigamodeinfoptr + PSSO_ModeInfo_OpenCount));
	write_log (_T("  Active        = %d\n"), get_byte (amigamodeinfoptr + PSSO_ModeInfo_Active));
	write_log (_T("  Width         = %d\n"), get_word (amigamodeinfoptr + PSSO_ModeInfo_Width));
	write_log (_T("  Height        = %d\n"), get_word (amigamodeinfoptr + PSSO_ModeInfo_Height));
	write_log (_T("  Depth         = %d\n"), get_byte (amigamodeinfoptr + PSSO_ModeInfo_Depth));
	write_log (_T("  Flags         = %d\n"), get_byte (amigamodeinfoptr + PSSO_ModeInfo_Flags));
	write_log (_T("  HorTotal      = %d\n"), get_word (amigamodeinfoptr + PSSO_ModeInfo_HorTotal));
	write_log (_T("  HorBlankSize  = %d\n"), get_word (amigamodeinfoptr + PSSO_ModeInfo_HorBlankSize));
	write_log (_T("  HorSyncStart  = %d\n"), get_word (amigamodeinfoptr + PSSO_ModeInfo_HorSyncStart));
	write_log (_T("  HorSyncSize   = %d\n"), get_word (amigamodeinfoptr + PSSO_ModeInfo_HorSyncSize));
	write_log (_T("  HorSyncSkew   = %d\n"), get_byte (amigamodeinfoptr + PSSO_ModeInfo_HorSyncSkew));
	write_log (_T("  HorEnableSkew = %d\n"), get_byte (amigamodeinfoptr + PSSO_ModeInfo_HorEnableSkew));
	write_log (_T("  VerTotal      = %d\n"), get_word (amigamodeinfoptr + PSSO_ModeInfo_VerTotal));
	write_log (_T("  VerBlankSize  = %d\n"), get_word (amigamodeinfoptr + PSSO_ModeInfo_VerBlankSize));
	write_log (_T("  VerSyncStart  = %d\n"), get_word (amigamodeinfoptr + PSSO_ModeInfo_VerSyncStart));
	write_log (_T("  VerSyncSize   = %d\n"), get_word (amigamodeinfoptr + PSSO_ModeInfo_VerSyncSize));
	write_log (_T("  Clock         = %d\n"), get_byte (amigamodeinfoptr + PSSO_ModeInfo_first_union));
	write_log (_T("  ClockDivide   = %d\n"), get_byte (amigamodeinfoptr + PSSO_ModeInfo_second_union));
	write_log (_T("  PixelClock    = %d\n"), get_long (amigamodeinfoptr + PSSO_ModeInfo_PixelClock));
}

static void DumpLibResolutionStructure (uaecptr amigalibresptr)
{
	int i;
	uaecptr amigamodeinfoptr;
	struct LibResolution *uaememptr = (struct LibResolution *)get_mem_bank(amigalibresptr).xlateaddr(amigalibresptr);

	write_log (_T("LibResolution Structure Dump:\n"));

	if (get_long (amigalibresptr + PSSO_LibResolution_DisplayID) == 0xFFFFFFFF) {
		write_log (_T("  Finished With LibResolutions...\n"));
	} else {
		write_log (_T("  Name      = %s\n"), uaememptr->P96ID);
		write_log (_T("  DisplayID = 0x%x\n"), get_long (amigalibresptr + PSSO_LibResolution_DisplayID));
		write_log (_T("  Width     = %d\n"), get_word (amigalibresptr + PSSO_LibResolution_Width));
		write_log (_T("  Height    = %d\n"), get_word (amigalibresptr + PSSO_LibResolution_Height));
		write_log (_T("  Flags     = %d\n"), get_word (amigalibresptr + PSSO_LibResolution_Flags));
		for (i = 0; i < MAXMODES; i++) {
			amigamodeinfoptr = get_long (amigalibresptr + PSSO_LibResolution_Modes + i*4);
			write_log (_T("  ModeInfo[%d] = 0x%x\n"), i, amigamodeinfoptr);
			if (amigamodeinfoptr)
				DumpModeInfoStructure (amigamodeinfoptr);
		}
		write_log (_T("  BoardInfo = 0x%x\n"), get_long (amigalibresptr + PSSO_LibResolution_BoardInfo));
	}
}

static TCHAR binary_byte[9] = { 0,0,0,0,0,0,0,0,0 };

static TCHAR *BuildBinaryString (uae_u8 value)
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
			write_log (_T("%s "), BuildBinaryString (*mem++));
		}
		write_log (_T("\n"));
	}
}

static void DumpTemplate (struct Template *tmp, unsigned long w, unsigned long h)
{
	uae_u8 *mem = tmp->Memory;
	unsigned int row, col, width;
	width = (w + 7) >> 3;
	write_log (_T("xoffset = %d, bpr = %d\n"), tmp->XOffset, tmp->BytesPerRow);
	for (row = 0; row < h; row++) {
		mem = tmp->Memory + row * tmp->BytesPerRow;
		for (col = 0; col < width; col++) {
			write_log (_T("%s "), BuildBinaryString (*mem++));
		}
		write_log (_T("\n"));
	}
}

static void DumpLine(struct Line *line)
{
	if (line) {
		write_log (_T("Line->X = %d\n"), line->X);
		write_log (_T("Line->Y = %d\n"), line->Y);
		write_log (_T("Line->Length = %d\n"), line->Length);
		write_log (_T("Line->dX = %d\n"), line->dX);
		write_log (_T("Line->dY = %d\n"), line->dY);
		write_log (_T("Line->sDelta = %d\n"), line->sDelta);
		write_log (_T("Line->lDelta = %d\n"), line->lDelta);
		write_log (_T("Line->twoSDminusLD = %d\n"), line->twoSDminusLD);
		write_log (_T("Line->LinePtrn = %d\n"), line->LinePtrn);
		write_log (_T("Line->PatternShift = %d\n"), line->PatternShift);
		write_log (_T("Line->FgPen = 0x%x\n"), line->FgPen);
		write_log (_T("Line->BgPen = 0x%x\n"), line->BgPen);
		write_log (_T("Line->Horizontal = %d\n"), line->Horizontal);
		write_log (_T("Line->DrawMode = %d\n"), line->DrawMode);
		write_log (_T("Line->Xorigin = %d\n"), line->Xorigin);
		write_log (_T("Line->Yorigin = %d\n"), line->Yorigin);
	}
}

static void ShowSupportedResolutions (void)
{
	int i = 0;

	write_log (_T("-----------------\n"));
	while (newmodes[i].depth >= 0) {
		write_log (_T("%s\n"), newmodes[i].name);
		i++;
	}
	write_log (_T("-----------------\n"));
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

STATIC_INLINE bool validatecoords2 (struct RenderInfo *ri, uae_u32 X, uae_u32 Y, uae_u32 Width, uae_u32 Height)
{
	if (Width >= 32768)
		return false;
	if (Height >= 32768)
		return false;
	if (X >= 32768)
		return false;
	if (Y >= 32768)
		return false;
	if (!Width || !Height)
		return true;
	if (ri) {
		int bpp = GetBytesPerPixel (ri->RGBFormat);
		if (Width * bpp > ri->BytesPerRow)
			return false;
		if (!valid_address (ri->AMemory, (Height - 1) * ri->BytesPerRow + (Width - 1) * bpp))
			return false;
	}
	return true;
}
static bool validatecoords (struct RenderInfo *ri, uae_u32 X, uae_u32 Y, uae_u32 Width, uae_u32 Height)
{
	if (validatecoords2 (ri, X, Y, Width, Height))
		return true;
	write_log (_T("RTG invalid region: %08X:%d:%d (%dx%d)-(%dx%d)\n"), ri->AMemory, ri->BytesPerRow, ri->RGBFormat, X, Y, Width, Height);
	return false;
}

/*
* Amiga <-> native structure conversion functions
*/

static int CopyRenderInfoStructureA2U (uaecptr amigamemptr, struct RenderInfo *ri)
{
	if (valid_address (amigamemptr, PSSO_RenderInfo_sizeof)) {
		uaecptr memp = get_long (amigamemptr + PSSO_RenderInfo_Memory);
		ri->AMemory = memp;
		ri->Memory = get_real_address (memp);
		ri->BytesPerRow = get_word (amigamemptr + PSSO_RenderInfo_BytesPerRow);
		ri->RGBFormat = (RGBFTYPE)get_long (amigamemptr + PSSO_RenderInfo_RGBFormat);
		// Can't really validate this better at this point, no height.
		if (valid_address (memp, ri->BytesPerRow))
			return 1;
	}
	write_log (_T("ERROR - Invalid RenderInfo memory area...\n"));
	return 0;
}

static int CopyPatternStructureA2U (uaecptr amigamemptr, struct Pattern *pattern)
{
	if (valid_address (amigamemptr, PSSO_Pattern_sizeof)) {
		uaecptr memp = get_long (amigamemptr + PSSO_Pattern_Memory);
		pattern->Memory = get_real_address (memp);
		pattern->XOffset = get_word (amigamemptr + PSSO_Pattern_XOffset);
		pattern->YOffset = get_word (amigamemptr + PSSO_Pattern_YOffset);
		pattern->FgPen = get_long (amigamemptr + PSSO_Pattern_FgPen);
		pattern->BgPen = get_long (amigamemptr + PSSO_Pattern_BgPen);
		pattern->Size = get_byte (amigamemptr + PSSO_Pattern_Size);
		pattern->DrawMode = get_byte (amigamemptr + PSSO_Pattern_DrawMode);
		if (valid_address (memp, 2))
			return 1;
	}
	write_log (_T("ERROR - Invalid Pattern memory area...\n"));
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
	write_log (_T("ERROR - Invalid Template memory area...\n"));
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
	write_log (_T("ERROR - Invalid Line structure...\n"));
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
	if (ri->Memory != gfxmem_bank.baseaddr + (picasso96_state.Address - gfxmem_bank.start))
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

static void setupcursor (void)
{
	uae_u8 *dptr = NULL;
	int bpp = 4;
	DWORD pitch;
	D3DLOCKED_RECT locked;
	HRESULT hr;

	if (currprefs.rtgmem_type >= GFXBOARD_HARDWARE)
		return;
	gfx_lock ();
	setupcursor_needed = 1;
	if (cursorsurfaced3d) {
		if (SUCCEEDED (hr = cursorsurfaced3d->LockRect (0, &locked, NULL, 0))) {
			dptr = (uae_u8*)locked.pBits;
			pitch = locked.Pitch;
			for (int y = 0; y < CURSORMAXHEIGHT; y++) {
				uae_u8 *p2 = dptr + pitch * y;
				memset (p2, 0, CURSORMAXWIDTH * bpp);
			}
			if (cursordata && cursorwidth && cursorheight) {
				dptr = (uae_u8*)locked.pBits;
				pitch = locked.Pitch;
				for (int y = 0; y < cursorheight; y++) {
					uae_u8 *p1 = cursordata + cursorwidth * bpp * y;
					uae_u8 *p2 = dptr + pitch * y;
					memcpy (p2, p1, cursorwidth * bpp);
				}
			}
			cursorsurfaced3d->UnlockRect (0);
			setupcursor_needed = 0;
			P96TRACE_SPR((_T("cursorsurface3d updated\n")));
		} else {
			P96TRACE_SPR((_T("cursorsurfaced3d LockRect() failed %08x\n"), hr));
		}
	}
	gfx_unlock ();
}

static void disablemouse (void)
{
	cursorok = FALSE;
	cursordeactivate = 0;
	if (!hwsprite)
		return;
	if (!currprefs.gfx_api)
		return;
	D3D_setcursor (0, 0, 0, 0, false, true);
}

static int newcursor_x, newcursor_y;

static void mouseupdate (void)
{
	int x = newcursor_x;
	int y = newcursor_y;
	int forced = 0;

	if (!hwsprite)
		return;
	if (cursordeactivate > 0) {
		cursordeactivate--;
		if (cursordeactivate == 0) {
			disablemouse ();
			cursorvisible = false;
		}
	}

	if (!currprefs.gfx_api)
		return;
	if (currprefs.gf[1].gfx_filter_autoscale == RTG_MODE_CENTER) {
		D3D_setcursor (x, y, WIN32GFX_GetWidth (), WIN32GFX_GetHeight(), cursorvisible, scalepicasso == 2);
	} else {
		D3D_setcursor (x, y, picasso96_state.Width, picasso96_state.Height, cursorvisible, false);
	}
}

static int framecnt;
int p96skipmode = -1;
static int doskip (void)
{
	if (framecnt >= currprefs.gfx_framerate)
		framecnt = 0;
	return framecnt > 0;
}

void picasso_trigger_vblank (void)
{
	if (!ABI_interrupt || !uaegfx_base || !interrupt_enabled || !currprefs.rtg_hardwareinterrupt)
		return;
	put_long (uaegfx_base + CARD_IRQPTR, ABI_interrupt + PSSO_BoardInfo_SoftInterrupt);
	put_byte (uaegfx_base + CARD_IRQFLAG, 1);
	if (currprefs.win32_rtgvblankrate != 0)
		INTREQ (0x8000 | 0x0008);
}

static bool rtg_render (void)
{
	bool flushed = false;
	bool uaegfx = currprefs.rtgmem_type < GFXBOARD_HARDWARE;

	if (doskip () && p96skipmode == 0) {
		;
	} else {
		if (uaegfx) {
			flushed = picasso_flushpixels (gfxmem_bank.start + natmem_offset, picasso96_state.XYOffset - gfxmem_bank.start);
		} else {
			gfxboard_vsync_handler ();
		}
	}
	return flushed;
}
static void rtg_show (void)
{
	gfx_unlock_picasso (true);
}
static void rtg_clear (void)
{
	rtg_clear_flag = 4;
}

static void picasso_handle_vsync2 (void)
{
	static int vsynccnt;
	int thisisvsync = 1;
	int vsync = isvsync_rtg ();
	int mult;
	bool rendered = false;
	bool uaegfx = currprefs.rtgmem_type < GFXBOARD_HARDWARE;

	if (picasso_on) {
		if (vsync < 0) {
			vsync_busywait_end (NULL);
			vsync_busywait_do (NULL, false, false);
		}
	}

	getvsyncrate (currprefs.chipset_refreshrate, &mult);
	if (vsync && mult < 0) {
		vsynccnt++;
		if (vsynccnt < 2)
			thisisvsync = 0;
		else
			vsynccnt = 0;
	}

	framecnt++;

	if (!uaegfx && !picasso_on) {
		rtg_render ();
		return;
	}

	if (!picasso_on)
		return;

	if (uaegfx)
		mouseupdate ();

	if (thisisvsync) {
		rendered = rtg_render ();
		frame_drawn ();
	}

	if (uaegfx) {
		if (setupcursor_needed)
			setupcursor ();
		if (thisisvsync)
			picasso_trigger_vblank ();
	}

	if (vsync < 0) {
		vsync_busywait_start ();
	}

	if (thisisvsync && !rendered)
		rtg_show ();
}

static int p96hsync;

void picasso_handle_vsync (void)
{
	bool uaegfx = currprefs.rtgmem_type < GFXBOARD_HARDWARE;

	if (currprefs.rtgmem_size == 0)
		return;

	if (!picasso_on && uaegfx) {
		createwindowscursor (0, 0, 0, 0, 0, 1);
		picasso_trigger_vblank ();
		return;
	}

	int vsync = isvsync_rtg ();
	if (vsync < 0) {
		p96hsync = 0;
		picasso_handle_vsync2 ();
	} else if (currprefs.win32_rtgvblankrate == 0) {
		picasso_handle_vsync2 ();
	}
}

void picasso_handle_hsync (void)
{
	bool uaegfx = currprefs.rtgmem_type < GFXBOARD_HARDWARE;

	if (currprefs.rtgmem_size == 0)
		return;

	int vsync = isvsync_rtg ();
	if (vsync < 0) {
		p96hsync++;
		if (p96hsync >= p96syncrate * 3) {
			p96hsync = 0;
			// kickstart vblank vsync_busywait stuff
			picasso_handle_vsync ();
		}
		return;
	}

	if (currprefs.win32_rtgvblankrate == 0)
		return;

	p96hsync++;
	if (p96hsync >= p96syncrate) {
		if (!picasso_on) {
			if (uaegfx) {
				createwindowscursor (0, 0, 0, 0, 0, 1);
				picasso_trigger_vblank ();
			}
		} else {
			picasso_handle_vsync2 ();
		}
		p96hsync = 0;
	}
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

static uae_u32 setspriteimage (uaecptr bi);
static void recursor (void)
{
	cursorok = FALSE;
	setspriteimage (boardinfo);
}

static int getconvert (int rgbformat, int pixbytes)
{
	int v = 0;
	int d = pixbytes;

	switch (rgbformat)
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
		else
			v = RGBFB_R5G6B5_16;
		break;
	case RGBFB_R5G5B5:
		if (d == 4)
			v = RGBFB_R5G5B5_32;
		else
			v = RGBFB_R5G5B5_16;
		break;
	case RGBFB_B5G5R5PC:
		if (d == 4)
			v = RGBFB_B5G5R5PC_32;
		else
			v = RGBFB_B5G5R5PC_16;
		break;

	case RGBFB_A8R8G8B8:
		if (d == 2)
			v = RGBFB_A8R8G8B8_16;
		else if (d == 4)
			v = RGBFB_A8R8G8B8_32;
		break;
	case RGBFB_R8G8B8:
		if (d == 2)
			v = RGBFB_R8G8B8_16;
		else if (d == 4)
			v = RGBFB_R8G8B8_32;
		break;
	case RGBFB_B8G8R8:
		if (d == 2)
			v = RGBFB_B8G8R8_16;
		else if (d == 4)
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
	case RGBFB_R8G8B8A8:
		if (d == 2)
			v = RGBFB_R8G8B8A8_16;
		else if (d == 4)
			v = RGBFB_R8G8B8A8_32;
		break;
	}
	return v;
}

static void setconvert (void)
{
	static int ohost_mode, orgbformat;

	picasso_convert = getconvert (picasso96_state.RGBFormat, picasso_vidinfo.pixbytes);
	if (currprefs.gfx_api) {
		host_mode = picasso_vidinfo.pixbytes == 4 ? RGBFB_B8G8R8A8 : RGBFB_B5G6R5PC;
	} else {
		host_mode = DirectDraw_GetSurfacePixelFormat (NULL);
	}
	if (picasso_vidinfo.pixbytes == 4)
		alloc_colors_rgb (8, 8, 8, 16, 8, 0, 0, 0, 0, 0, p96rc, p96gc, p96bc);
	else
		alloc_colors_rgb (5, 6, 5, 11, 5, 0, 0, 0, 0, 0, p96rc, p96gc, p96bc);
	gfx_set_picasso_colors (picasso96_state.RGBFormat);
	picasso_palette ();
	if (host_mode != ohost_mode || picasso96_state.RGBFormat != orgbformat) {
		write_log (_T("RTG conversion: Depth=%d HostRGBF=%d P96RGBF=%d Mode=%d\n"),
			picasso_vidinfo.pixbytes, host_mode, picasso96_state.RGBFormat, picasso_convert);
		ohost_mode = host_mode;
		orgbformat = picasso96_state.RGBFormat;
	}
	recursor ();
	full_refresh = 1;
}

bool picasso_is_active (void)
{
	return picasso_active;
}

/* Clear our screen, since we've got a new Picasso screen-mode, and refresh with the proper contents
* This is called on several occasions:
* 1. Amiga-->Picasso transition, via SetSwitch()
* 2. Picasso-->Picasso transition, via SetPanning().
* 3. whenever the graphics code notifies us that the screen contents have been lost.
*/
void picasso_refresh (void)
{
	struct RenderInfo ri;

	if (! picasso_on)
		return;
	full_refresh = 1;
	setconvert ();
	setupcursor ();
	rtg_clear ();

	if (currprefs.rtgmem_type >= GFXBOARD_HARDWARE) {
		gfxboard_refresh ();
		return;
	}

	/* Make sure that the first time we show a Picasso video mode, we don't blit any crap.
	* We can do this by checking if we have an Address yet. 
	*/
	if (picasso96_state.Address) {
		unsigned int width, height;

		/* blit the stuff from our static frame-buffer to the gfx-card */
		ri.Memory = gfxmem_bank.baseaddr + (picasso96_state.Address - gfxmem_bank.start);
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
	} else {
		write_log (_T("ERROR - picasso_refresh() can't refresh!\n"));
	}
}

#define BLT_SIZE 4
#define BLT_MULT 1
#define BLT_NAME BLIT_FALSE_32
#define BLT_FUNC(s,d) *d = 0
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NOR_32
#define BLT_FUNC(s,d) *d = ~(*s | * d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_ONLYDST_32
#define BLT_FUNC(s,d) *d = (*d) & ~(*s)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NOTSRC_32
#define BLT_FUNC(s,d) *d = ~(*s)
#include "p96_blit.cpp" 
#define BLT_NAME BLIT_ONLYSRC_32
#define BLT_FUNC(s,d) *d = (*s) & ~(*d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NOTDST_32
#define BLT_FUNC(s,d) *d = ~(*d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_EOR_32
#define BLT_FUNC(s,d) *d = (*s) ^ (*d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NAND_32
#define BLT_FUNC(s,d) *d = ~((*s) & (*d))
#include "p96_blit.cpp"
#define BLT_NAME BLIT_AND_32
#define BLT_FUNC(s,d) *d = (*s) & (*d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NEOR_32
#define BLT_FUNC(s,d) *d = ~((*s) ^ (*d))
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NOTONLYSRC_32
#define BLT_FUNC(s,d) *d = ~(*s) | (*d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NOTONLYDST_32
#define BLT_FUNC(s,d) *d = ~(*d) | (*s)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_OR_32
#define BLT_FUNC(s,d) *d = (*s) | (*d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_TRUE_32
#define BLT_FUNC(s,d) *d = 0xffffffff
#include "p96_blit.cpp"
#define BLT_NAME BLIT_SWAP_32
#define BLT_FUNC(s,d) tmp = *d ; *d = *s; *s = tmp;
#define BLT_TEMP
#include "p96_blit.cpp"
#undef BLT_SIZE
#undef BLT_MULT

#define BLT_SIZE 3
#define BLT_MULT 1
#define BLT_NAME BLIT_FALSE_24
#define BLT_FUNC(s,d) *d = 0
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NOR_24
#define BLT_FUNC(s,d) *d = ~(*s | * d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_ONLYDST_24
#define BLT_FUNC(s,d) *d = (*d) & ~(*s)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NOTSRC_24
#define BLT_FUNC(s,d) *d = ~(*s)
#include "p96_blit.cpp" 
#define BLT_NAME BLIT_ONLYSRC_24
#define BLT_FUNC(s,d) *d = (*s) & ~(*d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NOTDST_24
#define BLT_FUNC(s,d) *d = ~(*d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_EOR_24
#define BLT_FUNC(s,d) *d = (*s) ^ (*d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NAND_24
#define BLT_FUNC(s,d) *d = ~((*s) & (*d))
#include "p96_blit.cpp"
#define BLT_NAME BLIT_AND_24
#define BLT_FUNC(s,d) *d = (*s) & (*d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NEOR_24
#define BLT_FUNC(s,d) *d = ~((*s) ^ (*d))
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NOTONLYSRC_24
#define BLT_FUNC(s,d) *d = ~(*s) | (*d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NOTONLYDST_24
#define BLT_FUNC(s,d) *d = ~(*d) | (*s)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_OR_24
#define BLT_FUNC(s,d) *d = (*s) | (*d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_TRUE_24
#define BLT_FUNC(s,d) *d = 0xffffffff
#include "p96_blit.cpp"
#define BLT_NAME BLIT_SWAP_24
#define BLT_FUNC(s,d) tmp = *d ; *d = *s; *s = tmp;
#define BLT_TEMP
#include "p96_blit.cpp"
#undef BLT_SIZE
#undef BLT_MULT

#define BLT_SIZE 2
#define BLT_MULT 2
#define BLT_NAME BLIT_FALSE_16
#define BLT_FUNC(s,d) *d = 0
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NOR_16
#define BLT_FUNC(s,d) *d = ~(*s | * d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_ONLYDST_16
#define BLT_FUNC(s,d) *d = (*d) & ~(*s)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NOTSRC_16
#define BLT_FUNC(s,d) *d = ~(*s)
#include "p96_blit.cpp" 
#define BLT_NAME BLIT_ONLYSRC_16
#define BLT_FUNC(s,d) *d = (*s) & ~(*d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NOTDST_16
#define BLT_FUNC(s,d) *d = ~(*d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_EOR_16
#define BLT_FUNC(s,d) *d = (*s) ^ (*d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NAND_16
#define BLT_FUNC(s,d) *d = ~((*s) & (*d))
#include "p96_blit.cpp"
#define BLT_NAME BLIT_AND_16
#define BLT_FUNC(s,d) *d = (*s) & (*d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NEOR_16
#define BLT_FUNC(s,d) *d = ~((*s) ^ (*d))
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NOTONLYSRC_16
#define BLT_FUNC(s,d) *d = ~(*s) | (*d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NOTONLYDST_16
#define BLT_FUNC(s,d) *d = ~(*d) | (*s)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_OR_16
#define BLT_FUNC(s,d) *d = (*s) | (*d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_TRUE_16
#define BLT_FUNC(s,d) *d = 0xffffffff
#include "p96_blit.cpp"
#define BLT_NAME BLIT_SWAP_16
#define BLT_FUNC(s,d) tmp = *d ; *d = *s; *s = tmp;
#define BLT_TEMP
#include "p96_blit.cpp"
#undef BLT_SIZE
#undef BLT_MULT

#define BLT_SIZE 1
#define BLT_MULT 4
#define BLT_NAME BLIT_FALSE_8
#define BLT_FUNC(s,d) *d = 0
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NOR_8
#define BLT_FUNC(s,d) *d = ~(*s | * d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_ONLYDST_8
#define BLT_FUNC(s,d) *d = (*d) & ~(*s)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NOTSRC_8
#define BLT_FUNC(s,d) *d = ~(*s)
#include "p96_blit.cpp" 
#define BLT_NAME BLIT_ONLYSRC_8
#define BLT_FUNC(s,d) *d = (*s) & ~(*d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NOTDST_8
#define BLT_FUNC(s,d) *d = ~(*d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_EOR_8
#define BLT_FUNC(s,d) *d = (*s) ^ (*d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NAND_8
#define BLT_FUNC(s,d) *d = ~((*s) & (*d))
#include "p96_blit.cpp"
#define BLT_NAME BLIT_AND_8
#define BLT_FUNC(s,d) *d = (*s) & (*d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NEOR_8
#define BLT_FUNC(s,d) *d = ~((*s) ^ (*d))
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NOTONLYSRC_8
#define BLT_FUNC(s,d) *d = ~(*s) | (*d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_NOTONLYDST_8
#define BLT_FUNC(s,d) *d = ~(*d) | (*s)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_OR_8
#define BLT_FUNC(s,d) *d = (*s) | (*d)
#include "p96_blit.cpp"
#define BLT_NAME BLIT_TRUE_8
#define BLT_FUNC(s,d) *d = 0xffffffff
#include "p96_blit.cpp"
#define BLT_NAME BLIT_SWAP_8
#define BLT_FUNC(s,d) tmp = *d ; *d = *s; *s = tmp;
#define BLT_TEMP
#include "p96_blit.cpp"
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
		write_log (_T("WARNING - BlitRect() has mask 0x%x with Bpp %d.\n"), mask, Bpp);
	}

	P96TRACE ((_T("(%dx%d)=(%dx%d)=(%dx%d)=%d\n"), srcx, srcy, dstx, dsty, width, height, opcode));
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
			case BLIT_SWAP: BLIT_SWAP_32 (PARMS); break;
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
			case BLIT_SWAP: BLIT_SWAP_24 (PARMS); break;
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
			case BLIT_SWAP: BLIT_SWAP_16 (PARMS); break;
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
			case BLIT_SWAP: BLIT_SWAP_8 (PARMS); break;
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
	uaecptr bi = m68k_areg (regs, 0);
	boardinfo = bi;
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
	uaecptr bi = m68k_areg (regs, 0);
	uae_u8 idx = m68k_dreg (regs, 0);
	uae_u8 red = m68k_dreg (regs, 1);
	uae_u8 green = m68k_dreg (regs, 2);
	uae_u8 blue = m68k_dreg (regs, 3);
	boardinfo = bi;
	idx++;
	if (!hwsprite)
		return 0;
	if (idx >= 4)
		return 0;
	cursorrgb[idx] = (red << 16) | (green << 8) | (blue << 0);
	P96TRACE_SPR ((_T("SetSpriteColor(%08x,%d:%02X%02X%02X). %x\n"), bi, idx, red, green, blue, bi + PSSO_BoardInfo_MousePens));
	return 1;
}

STATIC_INLINE uae_u16 rgb32torgb16pc (uae_u32 rgb)
{
	return (((rgb >> (16 + 3)) & 0x1f) << 11) | (((rgb >> (8 + 2)) & 0x3f) << 5) | (((rgb >> (0 + 3)) & 0x1f) << 0);
}

static void updatesprcolors (int bpp)
{
	int i;
	for (i = 0; i < 4; i++) {
		uae_u32 v = cursorrgb[i];
		switch (bpp)
		{
		case 2:
			cursorrgbn[i] = rgb32torgb16pc (v);
			break;
		case 4:
			if (i > 0)
				v |= 0xff000000;
			else
				v &= 0x00ffffff;
			cursorrgbn[i] = v;
			break;
		}
	}
}

STATIC_INLINE void putmousepixel (uae_u8 *d, int bpp, int idx)
{
	uae_u32 val;

	val = cursorrgbn[idx];
	switch (bpp)
	{
	case 2:
		((uae_u16*)d)[0] = (uae_u16)val;
		break;
	case 4:
		((uae_u32*)d)[0] = (uae_u32)val;
		break;
	}
}

static void putwinmousepixel (HDC andDC, HDC xorDC, int x, int y, int c, uae_u32 *ct)
{
	if (c == 0) {
		SetPixel (andDC, x, y, RGB (255, 255, 255));
		SetPixel (xorDC, x, y, RGB (0, 0, 0));
	} else {
		uae_u32 val = ct[c];
		SetPixel (andDC, x, y, RGB (0, 0, 0));
		SetPixel (xorDC, x, y, RGB ((val >> 16) & 0xff, (val >> 8) & 0xff, val & 0xff));
	}
}

static int wincursorcnt;
static int tmp_sprite_w, tmp_sprite_h, tmp_sprite_hires, tmp_sprite_doubled;
static uae_u8 *tmp_sprite_data;
static uae_u32 tmp_sprite_colors[4];

extern uaecptr sprite_0;
extern int sprite_0_width, sprite_0_height, sprite_0_doubled;
extern uae_u32 sprite_0_colors[4];

int createwindowscursor (uaecptr src, int w, int h, int hiressprite, int doubledsprite, int chipset)
{
	HBITMAP andBM, xorBM;
	HBITMAP andoBM, xoroBM;
	HDC andDC, xorDC, DC, mainDC;
	ICONINFO ic;
	int x, y, yy, w2, h2;
	int ret, isdata, datasize;
	HCURSOR oldwincursor = wincursor;
	uae_u8 *realsrc;
	uae_u32 *ct;

	ret = 0;
	wincursor_shown = 0;

	if (isfullscreen () > 0 || currprefs.input_tablet == 0 || currprefs.input_magic_mouse == 0)
		goto exit;
	if (currprefs.input_magic_mouse_cursor != MAGICMOUSE_HOST_ONLY)
		goto exit;

	if (chipset) {
		if (!sprite_0 || !mousehack_alive ()) {
			if (wincursor)
				SetCursor (normalcursor);
			goto exit;
		}
		w2 = w = sprite_0_width;
		h2 = h = sprite_0_height;
		hiressprite = sprite_0_width / 16;
		doubledsprite = sprite_0_doubled;
		if (doubledsprite) {
			h2 *= 2;
			w2 *= 2;
		}
		src = sprite_0;
		ct = sprite_0_colors;
	} else {
		h2 = h;
		w2 = w;
		ct = cursorrgbn;
	}
	datasize = h * ((w + 15) / 16) * 4;
	realsrc = get_real_address (src);

	if (w > 64 || h > 64)
		goto exit;

	if (wincursor && tmp_sprite_data) {
		if (w == tmp_sprite_w && h == tmp_sprite_h &&
			!memcmp (tmp_sprite_data, realsrc, datasize) && !memcmp (tmp_sprite_colors, ct, sizeof (uae_u32)*4)
			&& hiressprite == tmp_sprite_hires && doubledsprite == tmp_sprite_doubled
			) {
				if (GetCursor () == wincursor) {
					wincursor_shown = 1;
					return 1;
				}
		}
	}
	write_log (_T("wincursor: %dx%d hires=%d doubled=%d\n"), w2, h2, hiressprite, doubledsprite);

	xfree (tmp_sprite_data);
	tmp_sprite_data = NULL;
	tmp_sprite_w = tmp_sprite_h = 0;

	DC = mainDC = andDC = xorDC = NULL;
	andBM = xorBM = NULL;
	DC = GetDC (NULL);
	if (!DC)
		goto end;
	mainDC = CreateCompatibleDC (DC);
	andDC = CreateCompatibleDC (DC);
	xorDC = CreateCompatibleDC (DC);
	if (!mainDC || !andDC || !xorDC)
		goto end;
	andBM = CreateCompatibleBitmap (DC, w2, h2);
	xorBM = CreateCompatibleBitmap (DC, w2, h2);
	if (!andBM || !xorBM)
		goto end;
	andoBM = (HBITMAP)SelectObject (andDC, andBM);
	xoroBM = (HBITMAP)SelectObject (xorDC, xorBM);

	isdata = 0;
	for (y = 0, yy = 0; y < h2; yy++) {
		int dbl;
		uaecptr img = src + yy * 4 * hiressprite;
		for (dbl = 0; dbl < (doubledsprite ? 2 : 1); dbl++) {
			x = 0;
			while (x < w2) {
				uae_u32 d1 = get_long (img);
				uae_u32 d2 = get_long (img + 2 * hiressprite);
				int bits;
				int maxbits = w2 - x;

				if (maxbits > 16 * hiressprite)
					maxbits = 16 * hiressprite;
				for (bits = 0; bits < maxbits && x < w2; bits++) {
					uae_u8 c = ((d2 & 0x80000000) ? 2 : 0) + ((d1 & 0x80000000) ? 1 : 0);
					d1 <<= 1;
					d2 <<= 1;
					putwinmousepixel (andDC, xorDC, x, y, c, ct);
					if (c > 0)
						isdata = 1;
					x++;
					if (doubledsprite && x < w2) {
						putwinmousepixel (andDC, xorDC, x, y, c, ct);
						x++;
					}
				}
			}
			if (y <= h2)
				y++;
		}
	}
	ret = 1;

	SelectObject (andDC, andoBM);
	SelectObject (xorDC, xoroBM);

end:
	DeleteDC (xorDC);
	DeleteDC (andDC);
	DeleteDC (mainDC);
	ReleaseDC (NULL, DC);

	if (!isdata) {
		wincursor = LoadCursor (NULL, IDC_ARROW);
	} else if (ret) {
		memset (&ic, 0, sizeof ic);
		ic.hbmColor = xorBM;
		ic.hbmMask = andBM;
		wincursor = CreateIconIndirect (&ic);
		tmp_sprite_w = w;
		tmp_sprite_h = h;
		tmp_sprite_data = xmalloc (uae_u8, datasize);
		tmp_sprite_hires = hiressprite;
		tmp_sprite_doubled = doubledsprite;
		memcpy (tmp_sprite_data, realsrc, datasize);
		memcpy (tmp_sprite_colors, ct, sizeof (uae_u32) * 4);
	}

	DeleteObject (andBM);
	DeleteObject (xorBM);

	if (wincursor) {
		SetCursor (wincursor);
		wincursor_shown = 1;
	}

	if (!ret)
		write_log (_T("RTG Windows color cursor creation failed\n"));

exit:
	if (currprefs.input_tablet && currprefs.input_magic_mouse && currprefs.input_magic_mouse_cursor == MAGICMOUSE_NATIVE_ONLY) {
		if (GetCursor () != NULL)
			SetCursor (NULL);
	} else {
		if (wincursor == oldwincursor && normalcursor != NULL)
			SetCursor (normalcursor);
	}
	if (oldwincursor)
		DestroyIcon (oldwincursor);
	oldwincursor = NULL;

	return ret;
}

int picasso_setwincursor (void)
{
	if (wincursor) {
		SetCursor (wincursor);
		return 1;
	} else if (!picasso_on) {
		if (createwindowscursor (0, 0, 0, 0, 0, 1))
			return 1;
	}
	return 0;
}

static uae_u32 setspriteimage (uaecptr bi)
{
	uae_u32 flags;
	int x, y, yy, bits, bpp;
	int hiressprite, doubledsprite;
	int ret = 0;
	int w, h;

	cursordeactivate = 0;
	if (!hwsprite)
		return 0;
	xfree (cursordata);
	cursordata = NULL;
	bpp = 4;
	w = get_byte (bi + PSSO_BoardInfo_MouseWidth);
	h = get_byte (bi + PSSO_BoardInfo_MouseHeight);
	flags = get_long (bi + PSSO_BoardInfo_Flags);
	hiressprite = 1;
	doubledsprite = 0;
	if (flags & BIF_HIRESSPRITE)
		hiressprite = 2;
	if (flags & BIF_BIGSPRITE)
		doubledsprite = 1;
	updatesprcolors (bpp);

	P96TRACE_SPR ((_T("SetSpriteImage(%08x,%08x,w=%d,h=%d,%d/%d,%08x)\n"),
		bi, get_long (bi + PSSO_BoardInfo_MouseImage), w, h,
		hiressprite - 1, doubledsprite, bi + PSSO_BoardInfo_MouseImage));

	if (!w || !h || get_long (bi + PSSO_BoardInfo_MouseImage) == 0) {
		cursordeactivate = 1;
		ret = 1;
		goto end;
	}

	createwindowscursor (get_long (bi + PSSO_BoardInfo_MouseImage) + 4 * hiressprite,
		w, h, hiressprite, doubledsprite, 0);

	cursordata = xmalloc (uae_u8, w * h * bpp);
	for (y = 0, yy = 0; y < h; y++, yy++) {
		uae_u8 *p = cursordata + w * bpp * y;
		uae_u8 *pprev = p;
		uaecptr img = get_long (bi + PSSO_BoardInfo_MouseImage) + 4 * hiressprite + yy * 4 * hiressprite;
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

	cursorwidth = w;
	if (cursorwidth > CURSORMAXWIDTH)
		cursorwidth = CURSORMAXWIDTH;
	cursorheight = h;
	if (cursorheight > CURSORMAXHEIGHT)
		cursorheight = CURSORMAXHEIGHT;

	setupcursor ();
	ret = 1;
	cursorok = TRUE;
	P96TRACE_SPR ((_T("hardware sprite created\n")));
end:
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
	uaecptr bi = m68k_areg (regs, 0);
	boardinfo = bi;
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
	uae_u32 result = 0;
	uae_u32 activate = m68k_dreg (regs, 0);
	if (!hwsprite)
		return 0;
	if (activate) {
		picasso_SetSpriteImage (ctx);
		cursorvisible = true;
	} else {
		cursordeactivate = 2;
	}
	result = 1;
	P96TRACE_SPR ((_T("SetSprite: %d\n"), activate));

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
static void picasso96_alloc2 (TrapContext *ctx);
static uae_u32 REGPARAM2 picasso_FindCard (TrapContext *ctx)
{
	uaecptr AmigaBoardInfo = m68k_areg (regs, 0);
	/* NOTES: See BoardInfo struct definition in Picasso96 dev info */
	if (!uaegfx_active || !gfxmem_bank.start)
		return 0;
	if (uaegfx_base) {
		put_long (uaegfx_base + CARD_BOARDINFO, AmigaBoardInfo);
	} else if (uaegfx_old) {
		picasso96_alloc2 (ctx);
	}
	boardinfo = AmigaBoardInfo;
	if (gfxmem_bank.allocated && !picasso96_state.CardFound) {
		/* Fill in MemoryBase, MemorySize */
		put_long (AmigaBoardInfo + PSSO_BoardInfo_MemoryBase, gfxmem_bank.start);
		put_long (AmigaBoardInfo + PSSO_BoardInfo_MemorySize, gfxmem_bank.allocated - reserved_gfxmem);
		picasso96_state.CardFound = 1; /* mark our "card" as being found */
		return -1;
	} else
		return 0;
}

static void FillBoardInfo (uaecptr amigamemptr, struct LibResolution *res, int width, int height, int depth)
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

	put_word (amigamemptr + PSSO_ModeInfo_Width, width);
	put_word (amigamemptr + PSSO_ModeInfo_Height, height);
	put_byte (amigamemptr + PSSO_ModeInfo_Depth, depth);
	put_byte (amigamemptr + PSSO_ModeInfo_Flags, 0);
	put_word (amigamemptr + PSSO_ModeInfo_HorTotal, width);
	put_word (amigamemptr + PSSO_ModeInfo_HorBlankSize, 0);
	put_word (amigamemptr + PSSO_ModeInfo_HorSyncStart, 0);
	put_word (amigamemptr + PSSO_ModeInfo_HorSyncSize, 0);
	put_byte (amigamemptr + PSSO_ModeInfo_HorSyncSkew, 0);
	put_byte (amigamemptr + PSSO_ModeInfo_HorEnableSkew, 0);

	put_word (amigamemptr + PSSO_ModeInfo_VerTotal, height);
	put_word (amigamemptr + PSSO_ModeInfo_VerBlankSize, 0);
	put_word (amigamemptr + PSSO_ModeInfo_VerSyncStart, 0);
	put_word (amigamemptr + PSSO_ModeInfo_VerSyncSize, 0);

	put_byte (amigamemptr + PSSO_ModeInfo_first_union, 98);
	put_byte (amigamemptr + PSSO_ModeInfo_second_union, 14);

	put_long (amigamemptr + PSSO_ModeInfo_PixelClock,
		width * height * (currprefs.gfx_apmode[1].gfx_refreshrate ? abs (currprefs.gfx_apmode[1].gfx_refreshrate) : default_freq));
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
	320, 256, 9,
	640, 512,10,

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
	1152, 648, 173,
	1776,1000, 174,
	2560,1440, 175,
	-1,-1,0
};

static int AssignModeID (int w, int h, int *unkcnt)
{
	int i;

#ifdef NEWMODES2
	return 0x40000000 | (w << 14) | h;
#endif
	for (i = 0; mi[i].width > 0; i++) {
		if (w == mi[i].width && h == mi[i].height)
			return 0x50001000 | (mi[i].id * 0x10000);
	}
	(*unkcnt)++;
	write_log (_T("P96: Non-unique mode %dx%d\n"), w, h);
	return 0x51001000 - (*unkcnt) * 0x10000;
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

void picasso_allocatewritewatch (int gfxmemsize)
{
	SYSTEM_INFO si;

	xfree (gwwbuf);
	GetSystemInfo (&si);
	gwwpagesize = si.dwPageSize;
	gwwbufsize = gfxmemsize / gwwpagesize + 1;
	gwwpagemask = gwwpagesize - 1;
	gwwbuf = xmalloc (void*, gwwbufsize);
}

static ULONG_PTR writewatchcount;
static int watch_offset;
void picasso_getwritewatch (int offset)
{
	ULONG ps;
	writewatchcount = gwwbufsize;
	watch_offset = offset;
	if (GetWriteWatch (WRITE_WATCH_FLAG_RESET, gfxmem_bank.start + natmem_offset + offset, (gwwbufsize - 1) * gwwpagesize, gwwbuf, &writewatchcount, &ps)) {
		write_log (_T("picasso_getwritewatch %d\n"), GetLastError ());
		writewatchcount = 0;
		return;
	}
}
bool picasso_is_vram_dirty (uaecptr addr, int size)
{
	static ULONG_PTR last;
	uae_u8 *a = addr + natmem_offset + watch_offset;
	int s = size;
	int ms = gwwpagesize;

	for (;;) {
		for (ULONG_PTR i = last; i < writewatchcount; i++) {
			uae_u8 *ma = (uae_u8*)gwwbuf[i];
			if (
				(a < ma && a + s >= ma) ||
				(a < ma + ms && a + s >= ma + ms) ||
				(a >= ma && a < ma + ms)) {
				last = i;
				return true;
			}
		}
		if (last == 0)
			break;
		last = 0;
	}
	return false;
}

static void init_alloc (TrapContext *ctx, int size)
{
	picasso96_amem = picasso96_amemend = 0;
	if (uaegfx_base) {
		int size = get_long (uaegfx_base + CARD_RESLISTSIZE);
		picasso96_amem = get_long (uaegfx_base + CARD_RESLIST);
	} else if (uaegfx_active) {
		reserved_gfxmem = size;
		picasso96_amem = gfxmem_bank.start + gfxmem_bank.allocated - size;
	}
	picasso96_amemend = picasso96_amem + size;
	write_log (_T("P96 RESINFO: %08X-%08X (%d,%d)\n"), picasso96_amem, picasso96_amemend, size / PSSO_ModeInfo_sizeof, size);
	picasso_allocatewritewatch (gfxmem_bank.allocated);
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

static int _cdecl resolution_compare (const void *a, const void *b)
{
	struct PicassoResolution *ma = (struct PicassoResolution *)a;
	struct PicassoResolution *mb = (struct PicassoResolution *)b;
	if (ma->res.width < mb->res.width)
		return -1;
	if (ma->res.width > mb->res.width)
		return 1;
	if (ma->res.height < mb->res.height)
		return -1;
	if (ma->res.height > mb->res.height)
		return 1;
	return ma->depth - mb->depth;
}

static int missmodes[] = { 320, 200, 320, 240, 320, 256, 640, 400, 640, 480, 640, 512, 800, 600, 1024, 768, 1280, 1024, -1 };

static uaecptr uaegfx_card_install (TrapContext *ctx, uae_u32 size);

static void picasso96_alloc2 (TrapContext *ctx)
{
	int i, j, size, cnt;
	int misscnt, depths;

	xfree (newmodes);
	newmodes = NULL;
	picasso96_amem = picasso96_amemend = 0;
	if (gfxmem_bank.allocated == 0)
		return;
	misscnt = 0;
	newmodes = xmalloc (struct PicassoResolution, MAX_PICASSO_MODES);
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

	for (int mon = 0; Displays[mon].monitorname; mon++) {
		struct PicassoResolution *DisplayModes = Displays[mon].DisplayModes;
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
	}

	cnt = 0;
	for (int mon = 0; Displays[mon].monitorname; mon++) {
		struct PicassoResolution *DisplayModes = Displays[mon].DisplayModes;
		i = 0;
		while (DisplayModes[i].depth >= 0) {
			if (DisplayModes[i].rawmode) {
				i++;
				continue;
			}
			j = i;
			size += PSSO_LibResolution_sizeof;
			while (missmodes[misscnt * 2] == 0)
				misscnt++;
			if (missmodes[misscnt * 2] >= 0) {
				int w = DisplayModes[i].res.width;
				int h = DisplayModes[i].res.height;
				if (w > missmodes[misscnt * 2 + 0] || (w == missmodes[misscnt * 2 + 0] && h > missmodes[misscnt * 2 + 1])) {	
					struct PicassoResolution *pr = &newmodes[cnt];
					memcpy (pr, &DisplayModes[i], sizeof (struct PicassoResolution));
					pr->res.width = missmodes[misscnt * 2 + 0];
					pr->res.height = missmodes[misscnt * 2 + 1];
					_stprintf (pr->name, _T("%dx%d FAKE"), pr->res.width, pr->res.height);
					size += PSSO_ModeInfo_sizeof * depths;
					cnt++;
					misscnt++;
					continue;
				}
			}
			int k;
			for (k = 0; k < cnt; k++) {
				if (newmodes[k].res.width == DisplayModes[i].res.width &&
					newmodes[k].res.height == DisplayModes[i].res.height &&
					newmodes[k].depth == DisplayModes[i].depth)
					break;
			}
			if (k >= cnt) {
				memcpy (&newmodes[cnt], &DisplayModes[i], sizeof (struct PicassoResolution));
				size += PSSO_ModeInfo_sizeof * depths;
				cnt++;
			}
			i++;
		}
	}
	qsort (newmodes, cnt, sizeof (struct PicassoResolution), resolution_compare);


#if MULTIDISPLAY
	for (i = 0; Displays[i].name; i++) {
		size += PSSO_LibResolution_sizeof;
		size += PSSO_ModeInfo_sizeof * depths;
	}
#endif
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
	uaegfx_card_install (ctx, size);
	init_alloc (ctx, size);
}

void picasso96_alloc (TrapContext *ctx)
{
	if (uaegfx_old || currprefs.rtgmem_type >= GFXBOARD_HARDWARE)
		return;
	uaegfx_resname = ds (_T("uaegfx.card"));
	picasso96_alloc2 (ctx);
}

static void inituaegfxfuncs (uaecptr start, uaecptr ABI);
static void inituaegfx (uaecptr ABI)
{
	uae_u32 flags;

	cursorvisible = false;
	cursorok = 0;
	cursordeactivate = 0;
	write_log (_T("RTG mode mask: %x\n"), currprefs.picasso96_modeflags);
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
	if (flags & BIF_NOBLITTER)
		write_log (_T("P96: Blitter disabled in devs:monitors/uaegfx!\n"));
	flags |= BIF_BLITTER | BIF_NOMEMORYMODEMIX;
	flags &= ~BIF_HARDWARESPRITE;
	if (currprefs.gfx_api && D3D_goodenough () > 0 && USE_HARDWARESPRITE && currprefs.rtg_hardwaresprite) {
		hwsprite = 1;
		flags |= BIF_HARDWARESPRITE;
		write_log (_T("P96: Hardware sprite support enabled\n"));
	} else {
		hwsprite = 0;
		write_log (_T("P96: Hardware sprite support disabled\n"));
	}
	if (currprefs.rtg_hardwareinterrupt && !uaegfx_old)
		flags |= BIF_VBLANKINTERRUPT;
	if (!(flags & BIF_INDISPLAYCHAIN)) {
		write_log (_T("P96: BIF_INDISPLAYCHAIN force-enabled!\n"));
		flags |= BIF_INDISPLAYCHAIN;
	}
	put_long (ABI + PSSO_BoardInfo_Flags, flags);
	if (debug_rtg_blitter != 3)
		write_log (_T("P96: Blitter mode = %x!\n"), debug_rtg_blitter);

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
	inituaegfxfuncs (uaegfx_rom, ABI);
}

static void addmode (uaecptr AmigaBoardInfo, uaecptr *amem, struct LibResolution *res, int w, int h, const TCHAR *name, int display, int *unkcnt)
{
	int depth;

	if (display > 0) {
		res->DisplayID = 0x51000000 + display * 0x100000;
	} else {
		res->DisplayID = AssignModeID (w, h, unkcnt);
	}
	res->BoardInfo = AmigaBoardInfo;
	res->Width = w;
	res->Height = h;
	res->Flags = P96F_PUBLIC;
	memcpy (res->P96ID, "P96-0:", 6);
	if (name) {
		char *n2 = ua (name);
		strcpy (res->Name, n2);
		xfree (n2);
	} else {
		sprintf (res->Name, "UAE:%4dx%4d", w, h);
	}

	for (depth = 8; depth <= 32; depth++) {
		if (!p96depth (depth))
			continue;
		if(gfxmem_bank.allocated >= w * h * (depth + 7) / 8) {
			FillBoardInfo (*amem, res, w, h, depth);
			*amem += PSSO_ModeInfo_sizeof;
		}
	}
}

/****************************************
* InitCard()
*
* a2: BoardInfo structure ptr - Amiga-based address in Intel endian-format
*
*/
static uae_u32 REGPARAM2 picasso_InitCard (TrapContext *ctx)
{
	int LibResolutionStructureCount = 0;
	int i, j, unkcnt, cnt;
	uaecptr amem;
	uaecptr AmigaBoardInfo = m68k_areg (regs, 0);

	if (!picasso96_amem) {
		write_log (_T("P96: InitCard() but no resolution memory!\n"));
		return 0;
	}
	amem = picasso96_amem;

	inituaegfx (AmigaBoardInfo);

	i = 0;
	unkcnt = cnt = 0;
	while (newmodes[i].depth >= 0) {
		struct LibResolution res = { 0 };
		TCHAR *s;
		j = i;
		addmode (AmigaBoardInfo, &amem, &res, newmodes[i].res.width, newmodes[i].res.height, NULL, 0, &unkcnt);
		s = au (res.Name);
		write_log (_T("%2d: %08X %4dx%4d %s\n"), ++cnt, res.DisplayID, res.Width, res.Height, s);
		xfree (s);
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
#if MULTIDISPLAY
	for (i = 0; Displays[i].name; i++) {
		struct LibResolution res = { 0 };
		struct MultiDisplay *md = &Displays[i];
		int w = md->rect.right - md->rect.left;
		int h = md->rect.bottom - md->rect.top;
		TCHAR tmp[100];
		if (md->primary)
			strcpy (tmp, "UAE:Primary");
		else
			_stprintf (tmp, "UAE:Display#%d", i);
		addmode (AmigaBoardInfo, &amem, &res, w, h, tmp, i + 1, &unkcnt);
		write_log (_T("%08X %4dx%4d %s\n"), res.DisplayID, res.Width + 16, res.Height, res.Name);
		LibResolutionStructureCount++;
		CopyLibResolutionStructureU2A (&res, amem);
#if P96TRACING_ENABLED && P96TRACING_LEVEL > 1
		DumpLibResolutionStructure(amem);
#endif
		AmigaListAddTail (AmigaBoardInfo + PSSO_BoardInfo_ResolutionsList, amem);
		amem += PSSO_LibResolution_sizeof;
	}
#endif

	if (amem > picasso96_amemend)
		write_log (_T("P96: display resolution list corruption %08x<>%08x (%d)\n"), amem, picasso96_amemend, i);

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
	uae_u16 flag = m68k_dreg (regs, 0) & 0xFFFF;
	TCHAR p96text[100];

	/* Do not switch immediately.  Tell the custom chip emulation about the
	* desired state, and wait for custom.c to call picasso_enablescreen
	* whenever it is ready to change the screen state.  */
	picasso_requested_on = flag != 0;
	picasso_active = picasso_requested_on;
	p96text[0] = 0;
	if (flag)
		_stprintf (p96text, _T("Picasso96 %dx%dx%d (%dx%dx%d)"),
		picasso96_state.Width, picasso96_state.Height, picasso96_state.BytesPerPixel * 8,
		picasso_vidinfo.width, picasso_vidinfo.height, picasso_vidinfo.pixbytes * 8);
	write_log (_T("SetSwitch() - %s\n"), flag ? p96text : _T("amiga"));
	/* Put old switch-state in D0 */
	return !flag;
}


static void init_picasso_screen (void);
void picasso_enablescreen (int on)
{
	if (!init_picasso_screen_called)
		init_picasso_screen ();

	picasso_refresh ();
	if (currprefs.rtgmem_type < GFXBOARD_HARDWARE)
		checkrtglibrary();
}

static void resetpalette(void)
{
	for (int i = 0; i < 256; i++)
		picasso96_state.CLUT[i].Pad = 0xff;
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

		//write_log(_T("%d: %02x%02x%02x\n"), i, r, g, b);
		changed |= picasso96_state.CLUT[i].Red != r
			|| picasso96_state.CLUT[i].Green != g
			|| picasso96_state.CLUT[i].Blue != b;
		if (picasso96_state.CLUT[i].Pad) {
			changed = 1;
			picasso96_state.CLUT[i].Pad = 0;
		}
		picasso96_state.CLUT[i].Red = r;
		picasso96_state.CLUT[i].Green = g;
		picasso96_state.CLUT[i].Blue = b;
		clut += 3;
	}
	changed |= picasso_palette ();
	return changed;
}
static uae_u32 REGPARAM2 picasso_SetColorArray (TrapContext *ctx)
{
	/* Fill in some static UAE related structure about this new CLUT setting
	* We need this for CLUT-based displays, and for mapping CLUT to hi/true colour */
	uae_u16 start = m68k_dreg (regs, 0);
	uae_u16 count = m68k_dreg (regs, 1);
	uaecptr boardinfo = m68k_areg (regs, 0);
	uaecptr clut = boardinfo + PSSO_BoardInfo_CLUT;
	if (start > 256 || start + count > 256)
		return 0;
	if (updateclut (clut, start, count))
		full_refresh = 1;
	P96TRACE((_T("SetColorArray(%d,%d)\n"), start, count));
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
	/* Fill in some static UAE related structure about this new DAC setting
	* Lets us keep track of what pixel format the Amiga is thinking about in our frame-buffer */

	P96TRACE((_T("SetDAC()\n")));
	rtg_clear ();
	return 1;
}

static void init_picasso_screen (void)
{
	if(set_panning_called) {
		picasso96_state.Extent = picasso96_state.Address + picasso96_state.BytesPerRow * picasso96_state.VirtualHeight;
	}
	if (set_gc_called) {
		gfx_set_picasso_modeinfo (picasso96_state.Width, picasso96_state.Height,
			picasso96_state.GC_Depth, picasso96_state.RGBFormat);
		set_gc_called = 0;
	}
	if((picasso_vidinfo.width == picasso96_state.Width) &&
		(picasso_vidinfo.height == picasso96_state.Height) &&
		(picasso_vidinfo.depth == (picasso96_state.GC_Depth >> 3)) &&
		(picasso_vidinfo.selected_rgbformat == picasso96_state.RGBFormat))
	{
		picasso_refresh ();
	}
	init_picasso_screen_called = 1;
	mman_ResetWatch (gfxmem_bank.start + natmem_offset, gfxmem_bank.allocated);

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

	P96TRACE((_T("SetGC(%d,%d,%d,%d)\n"), picasso96_state.Width, picasso96_state.Height, picasso96_state.GC_Depth, border));
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
	uae_u16 Width = m68k_dreg (regs, 0);
	uaecptr start_of_screen = m68k_areg (regs, 1);
	uaecptr bi = m68k_areg (regs, 0);
	uaecptr bmeptr = get_long (bi + PSSO_BoardInfo_BitMapExtra);  /* Get our BoardInfo ptr's BitMapExtra ptr */
	uae_u16 bme_width, bme_height;
	int changed = 0;
	RGBFTYPE rgbf;

	if (oldscr == 0) {
		oldscr = start_of_screen;
		changed = 1;
	}
	if (oldscr != start_of_screen) {
		oldscr = start_of_screen;
		changed = 1;
	}

	bme_width = get_word (bmeptr + PSSO_BitMapExtra_Width);
	bme_height = get_word (bmeptr + PSSO_BitMapExtra_Height);
	rgbf = picasso96_state.RGBFormat;

	picasso96_state.Address = start_of_screen; /* Amiga-side address */
	picasso96_state.XOffset = (uae_s16)(m68k_dreg (regs, 1) & 0xFFFF);
	picasso96_state.YOffset = (uae_s16)(m68k_dreg (regs, 2) & 0xFFFF);
	put_word (bi + PSSO_BoardInfo_XOffset, picasso96_state.XOffset);
	put_word (bi + PSSO_BoardInfo_YOffset, picasso96_state.YOffset);
	picasso96_state.VirtualWidth = bme_width;
	picasso96_state.VirtualHeight = bme_height;
	picasso96_state.RGBFormat = (RGBFTYPE)m68k_dreg (regs, 7);
	picasso96_state.BytesPerPixel = GetBytesPerPixel (picasso96_state.RGBFormat);
	picasso96_state.BytesPerRow = picasso96_state.VirtualWidth * picasso96_state.BytesPerPixel;
	picasso_SetPanningInit();

	if (rgbf != picasso96_state.RGBFormat)
		setconvert ();

	full_refresh = 1;
	set_panning_called = 1;
	P96TRACE((_T("SetPanning(%d, %d, %d) (%dx%d) Start 0x%x, BPR %d Bpp %d RGBF %d\n"),
		Width, picasso96_state.XOffset, picasso96_state.YOffset,
		bme_width, bme_height,
		start_of_screen, picasso96_state.BytesPerRow, picasso96_state.BytesPerPixel, picasso96_state.RGBFormat));
	init_picasso_screen ();
	set_panning_called = 0;

	return 1;
}

#ifdef CPU_64_BIT
static void do_xor8 (uae_u8 *p, int w, uae_u32 v)
{
	while (ALIGN_POINTER_TO32 (p) != 7 && w) {
		*p ^= v;
		p++;
		w--;
	}
	uae_u64 vv = v | ((uae_u64)v << 32);
	while (w >= 2 * 8) {
		*((uae_u64*)p) ^= vv;
		p += 8;
		*((uae_u64*)p) ^= vv;
		p += 8;
		w -= 2 * 8;
	}
	while (w) {
		*p ^= v;
		p++;
		w--;
	}
}
#else
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
#endif
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
		P96TRACE((_T("InvertRect %dbpp 0x%lx\n"), Bpp, (long)mask));

		if (!validatecoords (&ri, X, Y, Width, Height))
			return 1;

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
	uaecptr renderinfo = m68k_areg (regs, 1);
	uae_u32 X = (uae_u16)m68k_dreg (regs, 0);
	uae_u32 Y = (uae_u16)m68k_dreg (regs, 1);
	uae_u32 Width = (uae_u16)m68k_dreg (regs, 2);
	uae_u32 Height = (uae_u16)m68k_dreg (regs, 3);
	uae_u32 Pen = m68k_dreg (regs, 4);
	uae_u8 Mask = (uae_u8)m68k_dreg (regs, 5);
	RGBFTYPE RGBFormat = (RGBFTYPE)m68k_dreg (regs, 7);
	uae_u8 *oldstart;
	int Bpp;
	struct RenderInfo ri;
	uae_u32 result = 0;

	if (NOBLITTER)
		return 0;
	if (CopyRenderInfoStructureA2U (renderinfo, &ri)) {
		if (!validatecoords (&ri, X, Y, Width, Height))
			return 1;

		Bpp = GetBytesPerPixel (RGBFormat);

		P96TRACE((_T("FillRect(%d, %d, %d, %d) Pen 0x%x BPP %d BPR %d Mask 0x%x\n"),
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
				write_log (_T("WARNING - FillRect() has unhandled mask 0x%x with Bpp %d. Using fall-back routine.\n"), Mask, Bpp);
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

	if (!validatecoords (ri, srcx, srcy, width, height))
		return 1;
	if (!validatecoords (dstri, dstx, dsty, width, height))
		return 1;

	uae_u8 Bpp = GetBytesPerPixel (ri->RGBFormat);

	if (opcode == BLIT_DST) {
		write_log ( _T("WARNING: BlitRect() being called with opcode of BLIT_DST\n") );
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
	P96TRACE((_T("BlitRect(%d, %d, %d, %d, %d, %d, 0x%x)\n"), srcx, srcy, dstx, dsty, width, height, Mask));
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
	uaecptr srcri = m68k_areg (regs, 1);
	uaecptr dstri = m68k_areg (regs, 2);
	unsigned long srcx = (uae_u16)m68k_dreg (regs, 0);
	unsigned long srcy = (uae_u16)m68k_dreg (regs, 1);
	unsigned long dstx = (uae_u16)m68k_dreg (regs, 2);
	unsigned long dsty = (uae_u16)m68k_dreg (regs, 3);
	unsigned long width = (uae_u16)m68k_dreg (regs, 4);
	unsigned long height = (uae_u16)m68k_dreg (regs, 5);
	BLIT_OPCODE OpCode = (BLIT_OPCODE)(m68k_dreg (regs, 6) & 0xff);
	uae_u32 RGBFmt = m68k_dreg (regs, 7);
	uae_u32 result = 0;

	if (NOBLITTER_BLIT)
		return 0;
	P96TRACE((_T("BlitRectNoMaskComplete() op 0x%02x, %08x:(%4d,%4d) --> %08x:(%4d,%4d), wh(%4d,%4d)\n"),
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
		return 0;
	if(CopyRenderInfoStructureA2U (rinf, &ri) && CopyPatternStructureA2U (pinf, &pattern)) {
		if (!validatecoords (&ri, X, Y, W, H))
			return 1;

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
			P96TRACE((_T("BlitPattern() xy(%d,%d), wh(%d,%d) draw 0x%x, off(%d,%d), ph %d\n"),
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
		if (!validatecoords (&ri, X, Y, W, H))
			return 1;

		Bpp = GetBytesPerPixel (ri.RGBFormat);
		uae_mem = ri.Memory + Y * ri.BytesPerRow + X * Bpp; /* offset into address */

		if (tmp.DrawMode & INVERS)
			inversion = 1;

		tmp.DrawMode &= 0x03;

		if (Mask != 0xFF) {
			if(Bpp > 1)
				Mask = 0xFF;
			if(tmp.DrawMode == COMP) {
				write_log (_T("WARNING - BlitTemplate() has unhandled mask 0x%x with COMP DrawMode. Using fall-back routine.\n"), Mask);
				return 0;
			} else {
				result = 1;
			}
		} else {
			result = 1;
		}

		if(result) {
			uae_u32 fgpen, bgpen;

			P96TRACE((_T("BlitTemplate() xy(%d,%d), wh(%d,%d) draw 0x%x fg 0x%x bg 0x%x \n"),
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
	uae_u32 state = m68k_dreg (regs, 0);
	P96TRACE ((_T("SetDisplay(%d)\n"), state));
	resetpalette ();
	return !state;
}

void init_hz_p96 (void)
{
	if (currprefs.win32_rtgvblankrate < 0 || isvsync_rtg ())  {
		double rate = getcurrentvblankrate ();
		if (rate < 0)
			p96vblank = vblank_hz;
		else
			p96vblank = getcurrentvblankrate ();
	} else if (currprefs.win32_rtgvblankrate == 0) {
		p96vblank = vblank_hz;
	} else {
		p96vblank = currprefs.win32_rtgvblankrate;
	}
	if (p96vblank <= 0)
		p96vblank = 60;
	if (p96vblank >= 300)
		p96vblank = 300;
	p96syncrate = maxvpos_nom * vblank_hz / p96vblank;
	write_log (_T("RTGFREQ: %d*%.4f = %.4f / %.1f = %d\n"), maxvpos_nom, vblank_hz, maxvpos_nom * vblank_hz, p96vblank, p96syncrate);
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
		write_log (_T("ERROR - BlitPlanar2Chunky() has minterm 0x%x, which I don't handle. Using fall-back routine.\n"),
			minterm);
	} else if (CopyRenderInfoStructureA2U (ri, &local_ri) && CopyBitMapStructureA2U (bm, &local_bm)) {
		P96TRACE((_T("BlitPlanar2Chunky(%d, %d, %d, %d, %d, %d) Minterm 0x%x, Mask 0x%x, Depth %d\n"),
			srcx, srcy, dstx, dsty, width, height, minterm, mask, local_bm.Depth));
		P96TRACE((_T("P2C - BitMap has %d BPR, %d rows\n"), local_bm.BytesPerRow, local_bm.Rows));
		PlanarToChunky (&local_ri, &local_bm, srcx, srcy, dstx, dsty, width, height, mask);
		result = 1;
	}
	return result;
}

/* NOTE: Watch for those planeptrs of 0x00000000 and 0xFFFFFFFF for all zero / all one bitmaps !!!! */
static void PlanarToDirect (struct RenderInfo *ri, struct BitMap *bm,
	unsigned long srcx, unsigned long srcy,
	unsigned long dstx, unsigned long dsty,
	unsigned long width, unsigned long height, uae_u8 mask, struct ColorIndexMapping *cim)
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
		write_log (_T("WARNING - BlitPlanar2Direct() has unhandled op-code 0x%x. Using fall-back routine.\n"), minterm);
		return 0;
	}
	if (CopyRenderInfoStructureA2U (ri, &local_ri) && CopyBitMapStructureA2U (bm, &local_bm)) {
		Mask = 0xFF;
		CopyColorIndexMappingA2U (cim, &local_cim, GetBytesPerPixel (local_ri.RGBFormat));
		P96TRACE((_T("BlitPlanar2Direct(%d, %d, %d, %d, %d, %d) Minterm 0x%x, Mask 0x%x, Depth %d\n"),
			srcx, srcy, dstx, dsty, width, height, minterm, Mask, local_bm.Depth));
		PlanarToDirect (&local_ri, &local_bm, srcx, srcy, dstx, dsty, width, height, Mask, &local_cim);
		result = 1;
	}
	return result;
}

#include "statusline.h"
void picasso_statusline (uae_u8 *dst)
{
	int y, yy, slx, sly;
	int dst_height, dst_width, pitch;

	dst_height = picasso96_state.Height;
	if (dst_height > picasso_vidinfo.height)
		dst_height = picasso_vidinfo.height;
	dst_width = picasso96_state.Width;
	if (dst_width > picasso_vidinfo.width)
		dst_width = picasso_vidinfo.width;
	pitch = picasso_vidinfo.rowbytes;
	statusline_getpos (&slx, &sly, picasso96_state.Width, dst_height);
	yy = 0;
	for (y = 0; y < TD_TOTAL_HEIGHT; y++) {
		uae_u8 *buf = dst + (y + sly) * pitch;
		draw_status_line_single (buf, picasso_vidinfo.pixbytes, y, dst_width, p96rc, p96gc, p96bc, NULL);
		yy++;
	}
}

static void copyrow (uae_u8 *src, uae_u8 *dst, int x, int y, int width, int srcbytesperrow, int srcpixbytes, int dstbytesperrow, int dstpixbytes, bool direct, int convert_mode)
{
	uae_u8 *src2 = src + y * srcbytesperrow;
	uae_u8 *dst2 = dst + y * dstbytesperrow;
	int endx = x + width, endx4;
	int dstpix = dstpixbytes;
	int srcpix = srcpixbytes;

	if (direct) {
		memcpy (dst2 + x * dstpix, src2 + x * srcpix, width * dstpix);
		return;
	}
	// native match?
	if (currprefs.gfx_api) {
		switch (convert_mode)
		{
			case RGBFB_B8G8R8A8_32:
			case RGBFB_R5G6B5PC_16:
				memcpy (dst2 + x * dstpix, src2 + x * srcpix, width * dstpix);
			return;
		}
	} else {
		switch (convert_mode)
		{
			case RGBFB_B8G8R8A8_32:
			case RGBFB_R5G6B5PC_16:
				memcpy (dst2 + x * dstpix, src2 + x * srcpix, width * dstpix);
			return;
		}
	}

	endx4 = endx & ~3;

	switch (convert_mode)
	{
		/* 24bit->32bit */
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

		/* 32bit->32bit */
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
			((uae_u32*)dst2)[x] = ((uae_u32*)src2)[x] >> 8;
			x++;
		}
		break;

		/* 15/16bit->32bit */
	case RGBFB_R5G6B5PC_32:
	case RGBFB_R5G5B5PC_32:
	case RGBFB_R5G6B5_32:
	case RGBFB_R5G5B5_32:
	case RGBFB_B5G6R5PC_32:
	case RGBFB_B5G5R5PC_32:
		{
			while ((x & 3) && x < endx) {
				((uae_u32*)dst2)[x] = p96_rgbx16[((uae_u16*)src2)[x]];
				x++;
			}
			while (x < endx4) {
				((uae_u32*)dst2)[x] = p96_rgbx16[((uae_u16*)src2)[x]];
				x++;
				((uae_u32*)dst2)[x] = p96_rgbx16[((uae_u16*)src2)[x]];
				x++;
				((uae_u32*)dst2)[x] = p96_rgbx16[((uae_u16*)src2)[x]];
				x++;
				((uae_u32*)dst2)[x] = p96_rgbx16[((uae_u16*)src2)[x]];
				x++;
			}
			while (x < endx) {
				((uae_u32*)dst2)[x] = p96_rgbx16[((uae_u16*)src2)[x]];
				x++;
			}
		}
		break;

		/* 16/15bit->16bit */
	case RGBFB_R5G5B5PC_16:
	case RGBFB_R5G6B5_16:
	case RGBFB_R5G5B5_16:
	case RGBFB_B5G5R5PC_16:
	case RGBFB_B5G6R5PC_16:
	case RGBFB_R5G6B5PC_16:
	{
			while ((x & 3) && x < endx) {
				((uae_u16*)dst2)[x] = (uae_u16)p96_rgbx16[((uae_u16*)src2)[x]];
				x++;
			}
			while (x < endx4) {
				((uae_u16*)dst2)[x] = (uae_u16)p96_rgbx16[((uae_u16*)src2)[x]];
				x++;
				((uae_u16*)dst2)[x] = (uae_u16)p96_rgbx16[((uae_u16*)src2)[x]];
				x++;
				((uae_u16*)dst2)[x] = (uae_u16)p96_rgbx16[((uae_u16*)src2)[x]];
				x++;
				((uae_u16*)dst2)[x] = (uae_u16)p96_rgbx16[((uae_u16*)src2)[x]];
				x++;
			}
			while (x < endx) {
				((uae_u16*)dst2)[x] = (uae_u16)p96_rgbx16[((uae_u16*)src2)[x]];
				x++;
			}
		}
		break;

		/* 24bit->16bit */
	case RGBFB_R8G8B8_16:
		while (x < endx) {
			uae_u8 r, g, b;
			r = src2[x * 3 + 0];
			g = src2[x * 3 + 1];
			b = src2[x * 3 + 2];
			((uae_u16*)dst2)[x] = p96_rgbx16[(((r >> 3) & 0x1f) << 11) | (((g >> 2) & 0x3f) << 5) | (((b >> 3) & 0x1f) << 0)];
			x++;
		}
		break;
	case RGBFB_B8G8R8_16:
		while (x < endx) {
			uae_u32 v;
			v = ((uae_u32*)(&src2[x * 3]))[0] >> 8;
			((uae_u16*)dst2)[x] = p96_rgbx16[(((v >> (8 + 3)) & 0x1f) << 11) | (((v >> (0 + 2)) & 0x3f) << 5) | (((v >> (16 + 3)) & 0x1f) << 0)];
			x++;
		}
		break;

		/* 32bit->16bit */
	case RGBFB_R8G8B8A8_16:
		while (x < endx) {
			uae_u32 v;
			v = ((uae_u32*)src2)[x];
			((uae_u16*)dst2)[x] = p96_rgbx16[(((v >> (0 + 3)) & 0x1f) << 11) | (((v >> (8 + 2)) & 0x3f) << 5) | (((v >> (16 + 3)) & 0x1f) << 0)];
			x++;
		}
		break;
	case RGBFB_A8R8G8B8_16:
		while (x < endx) {
			uae_u32 v;
			v = ((uae_u32*)src2)[x];
			((uae_u16*)dst2)[x] = p96_rgbx16[(((v >> (8 + 3)) & 0x1f) << 11) | (((v >> (16 + 2)) & 0x3f) << 5) | (((v >> (24 + 3)) & 0x1f) << 0)];
			x++;
		}
		break;
	case RGBFB_A8B8G8R8_16:
		while (x < endx) {
			uae_u32 v;
			v = ((uae_u32*)src2)[x];
			((uae_u16*)dst2)[x] = p96_rgbx16[(((v >> (24 + 3)) & 0x1f) << 11) | (((v >> (16 + 2)) & 0x3f) << 5) | (((v >> (8 + 3)) & 0x1f) << 0)];
			x++;
		}
		break;
	case RGBFB_B8G8R8A8_16:
		while (x < endx) {
			uae_u32 v;
			v = ((uae_u32*)src2)[x];
			((uae_u16*)dst2)[x] = p96_rgbx16[(((v >> (16 + 3)) & 0x1f) << 11) | (((v >> (8 + 2)) & 0x3f) << 5) | (((v >> (0 + 3)) & 0x1f) << 0)];
			x++;
		}
		break;

		/* 8bit->32bit */
	case RGBFB_CLUT_RGBFB_32:
		{
			while ((x & 3) && x < endx) {
				((uae_u32*)dst2)[x] = picasso_vidinfo.clut[src2[x]];
				x++;
			}
			while (x < endx4) {
				((uae_u32*)dst2)[x] = picasso_vidinfo.clut[src2[x]];
				x++;
				((uae_u32*)dst2)[x] = picasso_vidinfo.clut[src2[x]];
				x++;
				((uae_u32*)dst2)[x] = picasso_vidinfo.clut[src2[x]];
				x++;
				((uae_u32*)dst2)[x] = picasso_vidinfo.clut[src2[x]];
				x++;
			}
			while (x < endx) {
				((uae_u32*)dst2)[x] = picasso_vidinfo.clut[src2[x]];
				x++;
			}
		}
		break;

		/* 8bit->16bit */
	case RGBFB_CLUT_RGBFB_16:
		{
			while ((x & 3) && x < endx) {
				((uae_u16*)dst2)[x] = picasso_vidinfo.clut[src2[x]];
				x++;
			}
			while (x < endx4) {
				((uae_u16*)dst2)[x] = picasso_vidinfo.clut[src2[x]];
				x++;
				((uae_u16*)dst2)[x] = picasso_vidinfo.clut[src2[x]];
				x++;
				((uae_u16*)dst2)[x] = picasso_vidinfo.clut[src2[x]];
				x++;
				((uae_u16*)dst2)[x] = picasso_vidinfo.clut[src2[x]];
				x++;
			}
			while (x < endx) {
				((uae_u16*)dst2)[x] = picasso_vidinfo.clut[src2[x]];
				x++;
			}
		}
		break;
	}
}

static void copyallinvert (uae_u8 *src, uae_u8 *dst, int pwidth, int pheight, int srcbytesperrow, int srcpixbytes, int dstbytesperrow, int dstpixbytes, bool direct, int mode_convert)
{
	int x, y, w;

	w = pwidth * dstpixbytes;
	if (direct) {
		for (y = 0; y < pheight; y++) {
			for (x = 0; x < w; x++)
				dst[x] = src[x] ^ 0xff;
			dst += dstbytesperrow;
			src += srcbytesperrow;
		}
	} else {
		uae_u8 *src2 = src;
		for (y = 0; y < pheight; y++) {
			for (x = 0; x < w; x++)
				src2[x] ^= 0xff;
			copyrow (src, dst, 0, y, pwidth, srcbytesperrow, srcpixbytes, dstbytesperrow, dstpixbytes, direct, mode_convert);
			for (x = 0; x < w; x++)
				src2[x] ^= 0xff;
			src2 += srcbytesperrow;
		}
	}
}

static void copyall (uae_u8 *src, uae_u8 *dst, int pwidth, int pheight, int srcbytesperrow, int srcpixbytes, int dstbytesperrow, int dstpixbytes, bool direct, int mode_convert)
{
	int y;

	if (direct) {
		int w = pwidth * picasso_vidinfo.pixbytes;
		for (y = 0; y < pheight; y++) {
			memcpy (dst, src, w);
			dst += dstbytesperrow;
			src += srcbytesperrow;
		}
	} else {
		for (y = 0; y < pheight; y++)
			copyrow (src, dst, 0, y, pwidth, srcbytesperrow, srcpixbytes, dstbytesperrow, dstpixbytes, direct, mode_convert);
	}
}

uae_u8 *getrtgbuffer (int *widthp, int *heightp, int *pitch, int *depth, uae_u8 *palette)
{
	uae_u8 *src = gfxmem_bank.start + natmem_offset;
	int off = picasso96_state.XYOffset - gfxmem_bank.start;
	int width, height, pixbytes;
	uae_u8 *dst;
	int convert;
	int hmode;

	if (!picasso_vidinfo.extra_mem)
		return NULL;

	width = picasso96_state.VirtualWidth;
	height = picasso96_state.VirtualHeight;
	pixbytes = picasso96_state.BytesPerPixel == 1 && palette ? 1 : 4;

	dst = xmalloc (uae_u8, width * height * pixbytes);
	if (!dst)
		return NULL;
	hmode = pixbytes == 1 ? RGBFB_CLUT : RGBFB_B8G8R8A8;
	convert = getconvert (picasso96_state.RGBFormat, pixbytes);

	if (pixbytes > 1 && hmode != convert) {
		copyall (src + off, dst, width, height, picasso96_state.BytesPerRow, picasso96_state.BytesPerPixel, width * pixbytes, pixbytes, false, convert);
	} else {
		uae_u8 *dstp = dst;
		uae_u8 *srcp = src;
		for (int y = 0; y < height; y++) {
			memcpy (dstp, srcp, width * pixbytes);
			dstp += width * pixbytes;
			srcp += picasso96_state.BytesPerRow;
		}
	}
	if (pixbytes == 1) {
		for (int i = 0; i < 256; i++) {
			palette[i * 3 + 0] = picasso96_state.CLUT[i].Red;
			palette[i * 3 + 1] = picasso96_state.CLUT[i].Green;
			palette[i * 3 + 2] = picasso96_state.CLUT[i].Blue;
		}
	}

	*widthp = width;
	*heightp = height;
	*pitch = width * pixbytes;
	*depth = pixbytes * 8;

	return dst;
}
void freertgbuffer (uae_u8 *dst)
{
	xfree (dst);
}

void picasso_invalidate (int x, int y, int w, int h)
{
	DX_Invalidate (x, y, w, h);
}

bool picasso_flushpixels (uae_u8 *src, int off)
{
	int i;
	uae_u8 *src_start;
	uae_u8 *src_end;
	int lock = 0;
	uae_u8 *dst = NULL;
	ULONG_PTR gwwcnt;
	int pwidth = picasso96_state.Width > picasso96_state.VirtualWidth ? picasso96_state.VirtualWidth : picasso96_state.Width;
	int pheight = picasso96_state.Height > picasso96_state.VirtualHeight ? picasso96_state.VirtualHeight : picasso96_state.Height;
	int maxy = -1;
	int miny = pheight - 1;
	int flushlines = 0, matchcount = 0;

	src_start = src + (off & ~gwwpagemask);
	src_end = src + ((off + picasso96_state.BytesPerRow * pheight + gwwpagesize - 1) & ~gwwpagemask);
#if 0
	write_log (_T("%dx%d %dx%d %dx%d (%dx%d)\n"), picasso96_state.Width, picasso96_state.Width,
		picasso96_state.VirtualWidth, picasso96_state.VirtualHeight,
		picasso_vidinfo.width, picasso_vidinfo.height,
		pwidth, pheight);
#endif
	if (!picasso_vidinfo.extra_mem || !gwwbuf || src_start >= src_end)
		return false;

	if (flashscreen) {
		full_refresh = 1;
	}
	if (full_refresh || rtg_clear_flag)
		full_refresh = -1;

	for (;;) {
		bool dofull;

		gwwcnt = 0;

		if (doskip () && p96skipmode == 1)
			break;

		if (full_refresh < 0) {
			gwwcnt = (src_end - src_start) / gwwpagesize + 1;
			full_refresh = 1;
			for (i = 0; i < gwwcnt; i++)
				gwwbuf[i] = src_start + i * gwwpagesize;
		} else {
			ULONG ps;
			gwwcnt = gwwbufsize;
			if (mman_GetWriteWatch (src_start, src_end - src_start, gwwbuf, &gwwcnt, &ps))
				break;
		}

		matchcount += gwwcnt;

		if (gwwcnt == 0)
			break;

		dofull = gwwcnt >= ((src_end - src_start) / gwwpagesize) * 80 / 100;

		dst = gfx_lock_picasso (dofull, rtg_clear_flag != 0);
		if (rtg_clear_flag)
			rtg_clear_flag--;
		if (dst == NULL)
			break;
		lock = 1;
		dst += picasso_vidinfo.offset;

		if (doskip () && p96skipmode == 2)
			break;

		if (dofull) {
			if (flashscreen != 0)
				copyallinvert (src + off, dst, pwidth, pheight,
					picasso96_state.BytesPerRow, picasso96_state.BytesPerPixel,
					picasso_vidinfo.rowbytes, picasso_vidinfo.pixbytes,
					picasso96_state.RGBFormat == host_mode, picasso_convert);
			else
				copyall (src + off, dst, pwidth, pheight,
					picasso96_state.BytesPerRow, picasso96_state.BytesPerPixel,
					picasso_vidinfo.rowbytes, picasso_vidinfo.pixbytes,
					picasso96_state.RGBFormat == host_mode, picasso_convert);

			miny = 0;
			maxy = pheight;
			flushlines = -1;
			break;
		}

		for (i = 0; i < gwwcnt; i++) {
			uae_u8 *p = (uae_u8*)gwwbuf[i];

			if (p >= src_start && p < src_end) {
				int y, x, realoffset;

				if (p >= src + off) {
					realoffset = p - (src + off);
				} else {
					realoffset = 0;
				}

				y = realoffset / picasso96_state.BytesPerRow;
				if (y < pheight) {
					int w = gwwpagesize / picasso96_state.BytesPerPixel;
					x = (realoffset % picasso96_state.BytesPerRow) / picasso96_state.BytesPerPixel;
					if (x < pwidth)
						copyrow (src + off, dst, x, y, pwidth - x,
							picasso96_state.BytesPerRow, picasso96_state.BytesPerPixel,
							picasso_vidinfo.rowbytes, picasso_vidinfo.pixbytes,
							picasso96_state.RGBFormat == host_mode, picasso_convert);
						flushlines++;
					w = (gwwpagesize - (picasso96_state.BytesPerRow - x * picasso96_state.BytesPerPixel)) / picasso96_state.BytesPerPixel;
					if (y < miny)
						miny = y;
					y++;
					while (y < pheight && w > 0) {
						int maxw = w > pwidth ? pwidth : w;
						copyrow (src + off, dst, 0, y, maxw,
							picasso96_state.BytesPerRow, picasso96_state.BytesPerPixel,
							picasso_vidinfo.rowbytes, picasso_vidinfo.pixbytes,
							picasso96_state.RGBFormat == host_mode, picasso_convert);
						w -= maxw;
						y++;
						flushlines++;
					}
					if (y > maxy)
						maxy = y;
				}

			}

		}
		break;
	}

	if (0 && flushlines) {
		write_log (_T("%d:%d\n"), flushlines, matchcount);
	}

	if (currprefs.leds_on_screen & STATUSLINE_RTG) {
		if (dst == NULL) {
			dst = gfx_lock_picasso (false, false);
			if (dst)
				lock = 1;
		}
		if (dst) {
			if (!(currprefs.leds_on_screen & STATUSLINE_TARGET))
				picasso_statusline (dst);
			maxy = picasso_vidinfo.height;
			if (miny > picasso_vidinfo.height - TD_TOTAL_HEIGHT)
				miny = picasso_vidinfo.height - TD_TOTAL_HEIGHT;
		}
	}
	if (maxy >= 0) {
		if (doskip () && p96skipmode == 4) {
			;
		} else {
			picasso_invalidate (0, miny, pwidth, maxy - miny);
		}
	}

	if (lock)
		gfx_unlock_picasso (true);
	if (dst && gwwcnt) {
		if (doskip () && p96skipmode == 3) {
			;
		} else {
			mman_ResetWatch (src_start, src_end - src_start);
		}
		full_refresh = 0;
	}
	return lock != 0; 
}

MEMORY_FUNCTIONS(gfxmem);

addrbank gfxmem_bank = {
	gfxmem_lget, gfxmem_wget, gfxmem_bget,
	gfxmem_lput, gfxmem_wput, gfxmem_bput,
	gfxmem_xlate, gfxmem_check, NULL, _T("RTG RAM"),
	dummy_lgeti, dummy_wgeti, ABFLAG_RAM
};

/* Call this function first, near the beginning of code flow
* Place in InitGraphics() which seems reasonable...
* Also put it in reset_drawing() for safe-keeping.  */
void InitPicasso96 (void)
{
	int i;

	//fastscreen
	oldscr = 0;
	//fastscreen
	memset (&picasso96_state, 0, sizeof (struct picasso96_state_struct));

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

#endif

static uae_u32 REGPARAM2 picasso_SetInterrupt (TrapContext *ctx)
{
	uaecptr bi = m68k_areg (regs, 0);
	uae_u32 onoff = m68k_dreg (regs, 0);
	interrupt_enabled = onoff;
	//write_log (_T("Picasso_SetInterrupt(%08x,%d)\n"), bi, onoff);
	return onoff;
}

static uaecptr uaegfx_vblankname, uaegfx_portsname;
static void initvblankABI (uaecptr base, uaecptr ABI)
{
	for (int i = 0; i < 22; i++)
		put_byte (ABI + PSSO_BoardInfo_HardInterrupt + i, get_byte (base + CARD_PORTSIRQ + i));
	ABI_interrupt = ABI;
}
static void initvblankirq (TrapContext *ctx, uaecptr base)
{
	uaecptr p1 = base + CARD_VBLANKIRQ;
	uaecptr p2 = base + CARD_PORTSIRQ;
	uaecptr c = base + CARD_IRQCODE;

	put_word (p1 + 8, 0x0205);
	put_long (p1 + 10, uaegfx_vblankname);
	put_long (p1 + 14, base + CARD_IRQFLAG);
	put_long (p1 + 18, c);

	put_word (p2 + 8, 0x0205);
	put_long (p2 + 10, uaegfx_portsname);
	put_long (p2 + 14, base + CARD_IRQFLAG);
	put_long (p2 + 18, c);

	put_word (c, 0x4a11); c += 2;		// tst.b (a1) CARD_IRQFLAG
	put_word (c, 0x670e); c += 2;		// beq.s label
	put_word (c, 0x4211); c += 2;		// clr.b (a1)
	put_long (c, 0x2c690008); c += 4;	// move.l 8(a1),a6 CARD_IRQEXECBASE
	put_long (c, 0x22690004); c += 4;	// move.l 4(a1),a1 CARD_IRQPTR
	put_long (c, 0x4eaeff4c); c += 4;	// jsr Cause(a6)
	put_word (c, 0x7000); c += 2;		// label: moveq #0,d0
	put_word (c, RTS);					// rts

	m68k_areg (regs, 1) = p1;
	m68k_dreg (regs, 0) = 5;			/* VERTB */
	CallLib (ctx, get_long (4), -168); 	/* AddIntServer */
	m68k_areg (regs, 1) = p2;
	m68k_dreg (regs, 0) = 3;			/* PORTS */
	CallLib (ctx, get_long (4), -168);	/* AddIntServer */
}

static uae_u32 REGPARAM2 picasso_SetClock(TrapContext *ctx)
{
	uaecptr bi = m68k_areg (regs, 0);
	P96TRACE((_T("SetClock\n")));
	return 0;
}

static uae_u32 REGPARAM2 picasso_SetMemoryMode(TrapContext *ctx)
{
	uaecptr bi = m68k_areg (regs, 0);
	uae_u32 rgbformat = m68k_dreg (regs, 7);
	P96TRACE((_T("SetMemoryMode\n")));
	return 0;
}

#define PUTABI(func) \
	if (ABI) \
	put_long (ABI + func, here ());

#define RTGCALL(func,funcdef,call) \
	PUTABI (func); \
	dl (0x48e78000); \
	calltrap (deftrap (call)); \
	dw (0x4a80); \
	dl (0x4cdf0001);\
	dw (0x6604); \
	dw (0x2f28); \
	dw (funcdef); \
	dw (RTS);

#define RTGCALL2(func,call) \
	PUTABI (func); \
	calltrap (deftrap (call)); \
	dw (RTS);

#define RTGCALLDEFAULT(func,funcdef) \
	PUTABI (func); \
	dw (0x2f28); \
	dw (funcdef); \
	dw (RTS);

#define RTGNONE(func) \
	if (ABI) \
	put_long (ABI + func, start);

static void inituaegfxfuncs (uaecptr start, uaecptr ABI)
{
	if (uaegfx_old)
		return;
	org (start);

	dw (RTS);
	/* ResolvePixelClock
	move.l	D0,gmi_PixelClock(a1)	; pass the pixelclock through
	moveq	#0,D0					; index is 0
	move.b	#98,gmi_Numerator(a1)	; whatever
	move.b	#14,gmi_Denominator(a1)	; whatever
	rts
	*/
	PUTABI (PSSO_BoardInfo_ResolvePixelClock);
	dl (0x2340002c);
	dw (0x7000);
	dl (0x137c0062); dw (0x002a);
	dl (0x137c000e); dw (0x002b);
	dw (RTS);

	/* GetPixelClock
	move.l #CLOCK,D0 ; fill in D0 with our one true pixel clock
	rts
	*/
	PUTABI (PSSO_BoardInfo_GetPixelClock);
	dw (0x203c);
	dl (100227260);
	dw (RTS);

	/* CalculateMemory
	; this is simple, because we're not supporting planar modes in UAE
	move.l	a1,d0
	rts
	*/
	PUTABI (PSSO_BoardInfo_CalculateMemory);
	dw (0x2009);
	dw (RTS);

	/* GetCompatibleFormats
	; all formats can coexist without any problems, since we don't support planar stuff in UAE
	move.l	#RGBMASK_8BIT | RGBMASK_15BIT | RGBMASK_16BIT | RGBMASK_24BIT | RGBMASK_32BIT,d0
	rts
	*/
	PUTABI (PSSO_BoardInfo_GetCompatibleFormats);
	dw (0x203c);
	dl (RGBMASK_8BIT | RGBMASK_15BIT | RGBMASK_16BIT | RGBMASK_24BIT | RGBMASK_32BIT);
	dw (RTS);

	/* CalculateBytesPerRow (optimized) */
	PUTABI (PSSO_BoardInfo_CalculateBytesPerRow);
	dl (0x0c400140); // cmp.w #320,d0
	dw (0x6504); // bcs.s .l1
	calltrap (deftrap (picasso_CalculateBytesPerRow));
	dw (RTS);
	dw (0x0c87); dl (0x00000010); // l1: cmp.l #$10,d7
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

	//RTGCALL2(PSSO_BoardInfo_SetClock, picasso_SetClock);
	//RTGCALL2(PSSO_BoardInfo_SetMemoryMode, picasso_SetMemoryMode);
	RTGNONE(PSSO_BoardInfo_SetClock);
	RTGNONE(PSSO_BoardInfo_SetMemoryMode);
	RTGNONE(PSSO_BoardInfo_SetWriteMask);
	RTGNONE(PSSO_BoardInfo_SetClearMask);
	RTGNONE(PSSO_BoardInfo_SetReadPlane);

#if 1
	RTGNONE(PSSO_BoardInfo_WaitVerticalSync);
#else
	PUTABI (PSSO_BoardInfo_WaitVerticalSync);
	dl (0x48e7203e);	// movem.l d2/a5/a6,-(sp)
	dl (0x2c68003c);
	dw (0x93c9);
	dl (0x4eaefeda);
	dw (0x2440);
	dw (0x70ff);
	dl (0x4eaefeb6);
	dw (0x7400);
	dw (0x1400);
	dw (0x6b40);
	dw (0x49f9);
	dl (uaegfx_base + CARD_VSYNCLIST);
	dw (0x47f9);
	dl (uaegfx_base + CARD_VSYNCLIST + CARD_VSYNCMAX * 8);
	dl (0x4eaeff88);
	dw (0xb9cb);
	dw (0x6606);
	dl (0x4eaeff82);
	dw (0x601c);
	dw (0x4a94);
	dw (0x6704);
	dw (0x508c);
	dw (0x60ee);
	dw (0x288a);
	dl (0x29420004);
	dl (0x4eaeff82);
	dw (0x7000);
	dw (0x05c0);
	dl (0x4eaefec2);
	dw (0x4294);
	dw (0x7000);
	dw (0x1002);
	dw (0x6b04);
	dl (0x4eaefeb0);
	dl (0x4cdf7c04);
	dw (RTS);
#endif
	RTGNONE(PSSO_BoardInfo_WaitBlitter);

#if 0
	RTGCALL2(PSSO_BoardInfo_, picasso_);
	RTGCALL(PSSO_BoardInfo_, PSSO_BoardInfo_Default, picasso_);
	RTGCALLDEFAULT(PSSO_BoardInfo_, PSSO_BoardInfo_Default);
#endif

	RTGCALL(PSSO_BoardInfo_BlitPlanar2Direct, PSSO_BoardInfo_BlitPlanar2DirectDefault, picasso_BlitPlanar2Direct);
	RTGCALL(PSSO_BoardInfo_FillRect, PSSO_BoardInfo_FillRectDefault, picasso_FillRect);
	RTGCALL(PSSO_BoardInfo_BlitRect, PSSO_BoardInfo_BlitRectDefault, picasso_BlitRect);
	RTGCALL(PSSO_BoardInfo_BlitPlanar2Chunky, PSSO_BoardInfo_BlitPlanar2ChunkyDefault, picasso_BlitPlanar2Chunky);
	RTGCALL(PSSO_BoardInfo_BlitTemplate, PSSO_BoardInfo_BlitTemplateDefault, picasso_BlitTemplate);
	RTGCALL(PSSO_BoardInfo_InvertRect, PSSO_BoardInfo_InvertRectDefault, picasso_InvertRect);
	RTGCALL(PSSO_BoardInfo_BlitRectNoMaskComplete, PSSO_BoardInfo_BlitRectNoMaskCompleteDefault, picasso_BlitRectNoMaskComplete);
	RTGCALL(PSSO_BoardInfo_BlitPattern, PSSO_BoardInfo_BlitPatternDefault, picasso_BlitPattern);

	RTGCALL2(PSSO_BoardInfo_SetSwitch, picasso_SetSwitch);
	RTGCALL2(PSSO_BoardInfo_SetColorArray, picasso_SetColorArray);
	RTGCALL2(PSSO_BoardInfo_SetDAC, picasso_SetDAC);
	RTGCALL2(PSSO_BoardInfo_SetGC, picasso_SetGC);
	RTGCALL2(PSSO_BoardInfo_SetPanning, picasso_SetPanning);
	RTGCALL2(PSSO_BoardInfo_SetDisplay, picasso_SetDisplay);

	RTGCALL2(PSSO_BoardInfo_SetSprite, picasso_SetSprite);
	RTGCALL2(PSSO_BoardInfo_SetSpritePosition, picasso_SetSpritePosition);
	RTGCALL2(PSSO_BoardInfo_SetSpriteImage, picasso_SetSpriteImage);
	RTGCALL2(PSSO_BoardInfo_SetSpriteColor, picasso_SetSpriteColor);

	RTGCALLDEFAULT(PSSO_BoardInfo_ScrollPlanar, PSSO_BoardInfo_ScrollPlanarDefault);
	RTGCALLDEFAULT(PSSO_BoardInfo_UpdatePlanar, PSSO_BoardInfo_UpdatePlanarDefault);
	RTGCALLDEFAULT(PSSO_BoardInfo_DrawLine, PSSO_BoardInfo_DrawLineDefault);

	if (currprefs.rtg_hardwareinterrupt)
		RTGCALL2(PSSO_BoardInfo_SetInterrupt, picasso_SetInterrupt);

	write_log (_T("uaegfx.card magic code: %08X-%08X ABI=%08X\n"), start, here (), ABI);

	if (ABI && currprefs.rtg_hardwareinterrupt)
		initvblankABI (uaegfx_base, ABI);
}

void picasso_reset (void)
{
	if (savestate_state != STATE_RESTORE) {
		uaegfx_base = 0;
		uaegfx_old = 0;
		uaegfx_active = 0;
		interrupt_enabled = 0;
		reserved_gfxmem = 0;
		resetpalette ();
		InitPicasso96 ();
	}
}

void uaegfx_install_code (uaecptr start)
{
	uaegfx_rom = start;
	org (start);
	inituaegfxfuncs (start, 0);
}

#define UAEGFX_VERSION 3
#define UAEGFX_REVISION 3

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

	if (uaegfx_old || !gfxmem_bank.start)
		return NULL;

	uaegfx_resid = ds (_T("UAE Graphics Card 3.3"));
	uaegfx_vblankname = ds (_T("UAE Graphics Card VBLANK"));
	uaegfx_portsname = ds (_T("UAE Graphics Card PORTS"));

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

	a2 = m68k_areg (regs, 2);
	m68k_areg (regs, 0) = functable;
	m68k_areg (regs, 1) = datatable;
	m68k_areg (regs, 2) = 0;
	m68k_dreg (regs, 0) = CARD_SIZEOF + extrasize;
	m68k_dreg (regs, 1) = 0;
	uaegfx_base = CallLib (ctx, exec, -0x54); /* MakeLibrary */
	m68k_areg (regs, 2) = a2;
	if (!uaegfx_base)
		return 0;
	m68k_areg (regs, 1) = uaegfx_base;
	CallLib (ctx, exec, -0x18c); /* AddLibrary */
	m68k_areg (regs, 1) = EXPANSION_explibname;
	m68k_dreg (regs, 0) = 0;
	put_long (uaegfx_base + CARD_EXPANSIONBASE, CallLib (ctx, exec, -0x228)); /* OpenLibrary */
	put_long (uaegfx_base + CARD_EXECBASE, exec);
	put_long (uaegfx_base + CARD_IRQEXECBASE, exec);
	put_long (uaegfx_base + CARD_NAME, uaegfx_resname);
	put_long (uaegfx_base + CARD_RESLIST, uaegfx_base + CARD_SIZEOF);
	put_long (uaegfx_base + CARD_RESLISTSIZE, extrasize);

	if (currprefs.rtg_hardwareinterrupt)
		initvblankirq (ctx, uaegfx_base);

	write_log (_T("uaegfx.card %d.%d init @%08X\n"), UAEGFX_VERSION, UAEGFX_REVISION, uaegfx_base);
	uaegfx_active = 1;
	return uaegfx_base;
}

uae_u32 picasso_demux (uae_u32 arg, TrapContext *ctx)
{
	uae_u32 num = get_long (m68k_areg (regs, 7) + 4);

	if (uaegfx_base) {
		if (num >= 16 && num <= 39) {
			write_log (_T("uaelib: obsolete Picasso96 uaelib hook called, call ignored\n"));
			return 0;
		}
	}
	if (!uaegfx_old) {
		write_log (_T("uaelib: uaelib hook in use\n"));
		uaegfx_old = 1;
		uaegfx_active = 1;
	}
	switch (num)
	{
     case 16: return picasso_FindCard (ctx);
     case 17: return picasso_FillRect (ctx);
     case 18: return picasso_SetSwitch (ctx);
     case 19: return picasso_SetColorArray (ctx);
     case 20: return picasso_SetDAC (ctx);
     case 21: return picasso_SetGC (ctx);
     case 22: return picasso_SetPanning (ctx);
     case 23: return picasso_CalculateBytesPerRow (ctx);
     case 24: return picasso_BlitPlanar2Chunky (ctx);
     case 25: return picasso_BlitRect (ctx);
     case 26: return picasso_SetDisplay (ctx);
     case 27: return picasso_BlitTemplate (ctx);
     case 28: return picasso_BlitRectNoMaskComplete (ctx);
     case 29: return picasso_InitCard (ctx);
     case 30: return picasso_BlitPattern (ctx);
     case 31: return picasso_InvertRect (ctx);
     case 32: return picasso_BlitPlanar2Direct (ctx);
     //case 34: return picasso_WaitVerticalSync (ctx);
     case 35: return gfxmem_bank.allocated ? 1 : 0;
	 case 36: return picasso_SetSprite (ctx);
	 case 37: return picasso_SetSpritePosition (ctx);
	 case 38: return picasso_SetSpriteImage (ctx);
	 case 39: return picasso_SetSpriteColor (ctx);
	}

	return 0;
}

void restore_p96_finish (void)
{
	init_alloc (NULL, 0);
	if (uaegfx_rom && boardinfo)
		inituaegfxfuncs (uaegfx_rom, boardinfo);
#if 0
	if (picasso_requested_on) {
		picasso_on = true;
		set_gc_called = 1;
		init_picasso_screen ();
		init_hz_p96 ();
		picasso_refresh ();
	}
#endif
}

uae_u8 *restore_p96 (uae_u8 *src)
{
	uae_u32 flags;
	int i;

	if (restore_u32 () != 2)
		return src;
	InitPicasso96 ();
	flags = restore_u32 ();
	picasso_requested_on = !!(flags & 1);
	hwsprite = !!(flags & 8);
	cursorvisible = !!(flags & 16);
	picasso96_state.SwitchState = picasso_requested_on;
	picasso_on = 0;
	init_picasso_screen_called = 0;
	set_gc_called = !!(flags & 2);
	set_panning_called = !!(flags & 4);
	interrupt_enabled = !!(flags & 32);
	changed_prefs.rtgmem_size = restore_u32 ();
	picasso96_state.Address = restore_u32 ();
	picasso96_state.RGBFormat = (RGBFTYPE)restore_u32 ();
	picasso96_state.Width = restore_u16 ();
	picasso96_state.Height = restore_u16 ();
	picasso96_state.VirtualWidth = restore_u16 ();
	picasso96_state.VirtualHeight = restore_u16 ();
	picasso96_state.XOffset = restore_u16 ();
	picasso96_state.YOffset = restore_u16 ();
	picasso96_state.GC_Depth = restore_u8 ();
	picasso96_state.GC_Flags = restore_u8 ();
	picasso96_state.BytesPerRow = restore_u16 ();
	picasso96_state.BytesPerPixel = restore_u8 ();
	uaegfx_base = restore_u32 ();
	uaegfx_rom = restore_u32 ();
	boardinfo = restore_u32 ();
	for (i = 0; i < 4; i++)
		cursorrgb[i] = restore_u32 ();
	if (flags & 64) {
		for (i = 0; i < 256; i++) {
			picasso96_state.CLUT[i].Red = restore_u8 ();
			picasso96_state.CLUT[i].Green = restore_u8 ();
			picasso96_state.CLUT[i].Blue = restore_u8 ();
		}
	}
	picasso96_state.HostAddress = NULL;
	picasso_SetPanningInit();
	picasso96_state.Extent = picasso96_state.Address + picasso96_state.BytesPerRow * picasso96_state.VirtualHeight;
	return src;
}

uae_u8 *save_p96 (int *len, uae_u8 *dstptr)
{
	uae_u8 *dstbak, *dst;
	int i;

	if (currprefs.rtgmem_size == 0)
		return NULL;
	if (dstptr)
		dstbak = dst = dstptr;
	else
		dstbak = dst = xmalloc (uae_u8, 1000);
	save_u32 (2);
	save_u32 ((picasso_on ? 1 : 0) | (set_gc_called ? 2 : 0) | (set_panning_called ? 4 : 0) |
		(hwsprite ? 8 : 0) | (cursorvisible ? 16 : 0) | (interrupt_enabled ? 32 : 0) | 64);
	save_u32 (currprefs.rtgmem_size);
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
	save_u32 (uaegfx_base);
	save_u32 (uaegfx_rom);
	save_u32 (boardinfo);
	for (i = 0; i < 4; i++)
		save_u32 (cursorrgb[i]);
	for (i = 0; i < 256; i++) {
		save_u8 (picasso96_state.CLUT[i].Red);
		save_u8 (picasso96_state.CLUT[i].Green);
		save_u8 (picasso96_state.CLUT[i].Blue);
	}
	*len = dst - dstbak;
	return dstbak;
}

#endif


