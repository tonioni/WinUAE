#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "traps.h"
#include "autoconf.h"
#include "custom.h"
#include "picasso96.h"
#include "savestate.h"
#include "gfxboard.h"
#include "video.h"
#include "xwin.h"

static uae_u32 REGPARAM3 gfxmem_lget(uaecptr) REGPARAM;
static uae_u32 REGPARAM3 gfxmem_wget(uaecptr) REGPARAM;
static uae_u32 REGPARAM3 gfxmem_bget(uaecptr) REGPARAM;
static void REGPARAM3 gfxmem_lput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 gfxmem_wput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 gfxmem_bput(uaecptr, uae_u32) REGPARAM;
static int REGPARAM3 gfxmem_check(uaecptr, uae_u32) REGPARAM;
static uae_u8 *REGPARAM3 gfxmem_xlate(uaecptr) REGPARAM;

static uae_u32 REGPARAM3 gfxmem2_lget(uaecptr) REGPARAM;
static uae_u32 REGPARAM3 gfxmem2_wget(uaecptr) REGPARAM;
static uae_u32 REGPARAM3 gfxmem2_bget(uaecptr) REGPARAM;
static void REGPARAM3 gfxmem2_lput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 gfxmem2_wput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 gfxmem2_bput(uaecptr, uae_u32) REGPARAM;
static int REGPARAM3 gfxmem2_check(uaecptr, uae_u32) REGPARAM;
static uae_u8 *REGPARAM3 gfxmem2_xlate(uaecptr) REGPARAM;

static uae_u32 REGPARAM3 gfxmem3_lget(uaecptr) REGPARAM;
static uae_u32 REGPARAM3 gfxmem3_wget(uaecptr) REGPARAM;
static uae_u32 REGPARAM3 gfxmem3_bget(uaecptr) REGPARAM;
static void REGPARAM3 gfxmem3_lput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 gfxmem3_wput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 gfxmem3_bput(uaecptr, uae_u32) REGPARAM;
static int REGPARAM3 gfxmem3_check(uaecptr, uae_u32) REGPARAM;
static uae_u8 *REGPARAM3 gfxmem3_xlate(uaecptr) REGPARAM;

static uae_u32 REGPARAM3 gfxmem4_lget(uaecptr) REGPARAM;
static uae_u32 REGPARAM3 gfxmem4_wget(uaecptr) REGPARAM;
static uae_u32 REGPARAM3 gfxmem4_bget(uaecptr) REGPARAM;
static void REGPARAM3 gfxmem4_lput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 gfxmem4_wput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 gfxmem4_bput(uaecptr, uae_u32) REGPARAM;
static int REGPARAM3 gfxmem4_check(uaecptr, uae_u32) REGPARAM;
static uae_u8 *REGPARAM3 gfxmem4_xlate(uaecptr) REGPARAM;
static void unix_picasso_mark_dirty_offset(int index, uae_u32 offset, uae_u32 size);

#define UNIX_RTG_MEMORY_FUNCTIONS(prefix, bank, index) \
static uae_u32 REGPARAM2 prefix ## _lget(uaecptr addr) \
{ \
    addr &= bank.mask; \
    if (!bank.baseaddr || addr + 4 > bank.allocated_size) { \
        return 0; \
    } \
    return do_get_mem_long((uae_u32 *)(bank.baseaddr + addr)); \
} \
static uae_u32 REGPARAM2 prefix ## _wget(uaecptr addr) \
{ \
    addr &= bank.mask; \
    if (!bank.baseaddr || addr + 2 > bank.allocated_size) { \
        return 0; \
    } \
    return do_get_mem_word((uae_u16 *)(bank.baseaddr + addr)); \
} \
static uae_u32 REGPARAM2 prefix ## _bget(uaecptr addr) \
{ \
    addr &= bank.mask; \
    if (!bank.baseaddr || addr >= bank.allocated_size) { \
        return 0; \
    } \
    return bank.baseaddr[addr]; \
} \
static void REGPARAM2 prefix ## _lput(uaecptr addr, uae_u32 value) \
{ \
    addr &= bank.mask; \
    if (!bank.baseaddr || addr + 4 > bank.allocated_size) { \
        return; \
    } \
    do_put_mem_long((uae_u32 *)(bank.baseaddr + addr), value); \
    unix_picasso_mark_dirty_offset(index, addr, 4); \
} \
static void REGPARAM2 prefix ## _wput(uaecptr addr, uae_u32 value) \
{ \
    addr &= bank.mask; \
    if (!bank.baseaddr || addr + 2 > bank.allocated_size) { \
        return; \
    } \
    do_put_mem_word((uae_u16 *)(bank.baseaddr + addr), value); \
    unix_picasso_mark_dirty_offset(index, addr, 2); \
} \
static void REGPARAM2 prefix ## _bput(uaecptr addr, uae_u32 value) \
{ \
    addr &= bank.mask; \
    if (!bank.baseaddr || addr >= bank.allocated_size) { \
        return; \
    } \
    bank.baseaddr[addr] = (uae_u8)value; \
    unix_picasso_mark_dirty_offset(index, addr, 1); \
} \
static int REGPARAM2 prefix ## _check(uaecptr addr, uae_u32 size) \
{ \
    addr &= bank.mask; \
    return bank.baseaddr && addr + size <= bank.allocated_size; \
} \
static uae_u8 *REGPARAM2 prefix ## _xlate(uaecptr addr) \
{ \
    addr &= bank.mask; \
    if (!bank.baseaddr || addr >= bank.allocated_size) { \
        return NULL; \
    } \
    return bank.baseaddr + addr; \
}

// RTG RAM is allocated before autoconfig chooses the real Zorro address.
// mapped_malloc_dynamic() requires a non-zero start for its temporary label;
// the address is overwritten when expansion autoconfig maps the board.
addrbank gfxmem_bank = {
    gfxmem_lget, gfxmem_wget, gfxmem_bget,
    gfxmem_lput, gfxmem_wput, gfxmem_bput,
    gfxmem_xlate, gfxmem_check, NULL, NULL, _T("RTG RAM"),
    gfxmem_lget, gfxmem_wget,
    ABFLAG_RAM | ABFLAG_RTG, S_READ, S_WRITE,
    NULL, 0, 0, 1
};

addrbank gfxmem2_bank = {
    gfxmem2_lget, gfxmem2_wget, gfxmem2_bget,
    gfxmem2_lput, gfxmem2_wput, gfxmem2_bput,
    gfxmem2_xlate, gfxmem2_check, NULL, NULL, _T("RTG RAM #2"),
    gfxmem2_lget, gfxmem2_wget,
    ABFLAG_RAM | ABFLAG_RTG, S_READ, S_WRITE,
    NULL, 0, 0, 1
};

addrbank gfxmem3_bank = {
    gfxmem3_lget, gfxmem3_wget, gfxmem3_bget,
    gfxmem3_lput, gfxmem3_wput, gfxmem3_bput,
    gfxmem3_xlate, gfxmem3_check, NULL, NULL, _T("RTG RAM #3"),
    gfxmem3_lget, gfxmem3_wget,
    ABFLAG_RAM | ABFLAG_RTG, S_READ, S_WRITE,
    NULL, 0, 0, 1
};

addrbank gfxmem4_bank = {
    gfxmem4_lget, gfxmem4_wget, gfxmem4_bget,
    gfxmem4_lput, gfxmem4_wput, gfxmem4_bput,
    gfxmem4_xlate, gfxmem4_check, NULL, NULL, _T("RTG RAM #4"),
    gfxmem4_lget, gfxmem4_wget,
    ABFLAG_RAM | ABFLAG_RTG, S_READ, S_WRITE,
    NULL, 0, 0, 1
};

UNIX_RTG_MEMORY_FUNCTIONS(gfxmem, gfxmem_bank, 0)
UNIX_RTG_MEMORY_FUNCTIONS(gfxmem2, gfxmem2_bank, 1)
UNIX_RTG_MEMORY_FUNCTIONS(gfxmem3, gfxmem3_bank, 2)
UNIX_RTG_MEMORY_FUNCTIONS(gfxmem4, gfxmem4_bank, 3)

addrbank *gfxmem_banks[MAX_RTG_BOARDS] = {
    &gfxmem_bank,
    &gfxmem2_bank,
    &gfxmem3_bank,
    &gfxmem4_bank
};

static const int unix_rtg_watch_page_size = 4096;
static uae_u8 *unix_rtg_dirty_pages[MAX_RTG_BOARDS];
static uae_u8 **unix_rtg_writewatch_pages[MAX_RTG_BOARDS];
static int unix_rtg_watch_page_count[MAX_RTG_BOARDS];
static int unix_rtg_writewatch_count[MAX_RTG_BOARDS];
static int unix_rtg_watch_offset[MAX_RTG_BOARDS];

static bool unix_picasso_valid_watch_index(int index)
{
    return index >= 0 && index < MAX_RTG_BOARDS && gfxmem_banks[index] != NULL;
}

static void unix_picasso_mark_dirty_offset(int index, uae_u32 offset, uae_u32 size)
{
    if (!size || !unix_picasso_valid_watch_index(index) || !unix_rtg_dirty_pages[index]) {
        return;
    }

    addrbank *bank = gfxmem_banks[index];
    if (!bank->allocated_size || offset >= bank->allocated_size) {
        return;
    }

    uae_u32 end = offset + size;
    if (end < offset || end > bank->allocated_size) {
        end = bank->allocated_size;
    }
    if (end <= offset) {
        return;
    }

    int first_page = offset / unix_rtg_watch_page_size;
    int last_page = (end - 1) / unix_rtg_watch_page_size;
    if (last_page >= unix_rtg_watch_page_count[index]) {
        last_page = unix_rtg_watch_page_count[index] - 1;
    }
    for (int page = first_page; page <= last_page; page++) {
        unix_rtg_dirty_pages[index][page] = 1;
    }
}

static void unix_picasso_mark_dirty_addr(int index, uaecptr addr, uae_u32 size)
{
    if (!size || !unix_picasso_valid_watch_index(index)) {
        return;
    }

    addrbank *bank = gfxmem_banks[index];
    if (addr < bank->start || addr >= bank->start + bank->allocated_size) {
        return;
    }
    unix_picasso_mark_dirty_offset(index, addr - bank->start, size);
}

static void unix_picasso_mark_renderinfo_rect(const RenderInfo *ri, uae_u32 x, uae_u32 y,
    uae_u32 width, uae_u32 height, int bytes_per_pixel)
{
    if (!ri || !width || !height || bytes_per_pixel <= 0) {
        return;
    }

    uae_u32 row_bytes = width * bytes_per_pixel;
    int row_stride = ri->BytesPerRow > 0 ? ri->BytesPerRow : (int)row_bytes;
    for (uae_u32 row = 0; row < height; row++) {
        unix_picasso_mark_dirty_addr(0,
            ri->AMemory + (y + row) * row_stride + x * bytes_per_pixel,
            row_bytes);
    }
}

void picasso_allocatewritewatch(int index, int gfxmemsize)
{
    if (index < 0 || index >= MAX_RTG_BOARDS) {
        return;
    }

    xfree(unix_rtg_dirty_pages[index]);
    xfree(unix_rtg_writewatch_pages[index]);
    unix_rtg_dirty_pages[index] = NULL;
    unix_rtg_writewatch_pages[index] = NULL;
    unix_rtg_watch_page_count[index] = 0;
    unix_rtg_writewatch_count[index] = 0;
    unix_rtg_watch_offset[index] = 0;

    if (gfxmemsize <= 0) {
        return;
    }

    int pages = (gfxmemsize + unix_rtg_watch_page_size - 1) / unix_rtg_watch_page_size;
    unix_rtg_dirty_pages[index] = xcalloc(uae_u8, pages);
    unix_rtg_writewatch_pages[index] = xcalloc(uae_u8 *, pages);
    if (!unix_rtg_dirty_pages[index] || !unix_rtg_writewatch_pages[index]) {
        xfree(unix_rtg_dirty_pages[index]);
        xfree(unix_rtg_writewatch_pages[index]);
        unix_rtg_dirty_pages[index] = NULL;
        unix_rtg_writewatch_pages[index] = NULL;
        return;
    }
    unix_rtg_watch_page_count[index] = pages;
}

int picasso_getwritewatch(int index, int offset, uae_u8 ***gwwbufp, uae_u8 **startp)
{
    if (gwwbufp) {
        *gwwbufp = NULL;
    }
    if (startp) {
        *startp = NULL;
    }
    if (!unix_picasso_valid_watch_index(index) || !unix_rtg_dirty_pages[index] ||
        !unix_rtg_writewatch_pages[index] || offset < 0) {
        return -1;
    }

    addrbank *bank = gfxmem_banks[index];
    if (!bank->baseaddr || offset >= (int)bank->allocated_size) {
        unix_rtg_writewatch_count[index] = 0;
        return -1;
    }

    uae_u8 *start = bank->baseaddr + offset;
    int first_page = offset / unix_rtg_watch_page_size;
    int count = 0;
    for (int page = first_page; page < unix_rtg_watch_page_count[index]; page++) {
        if (!unix_rtg_dirty_pages[index][page]) {
            continue;
        }
        uae_u32 page_offset = page * unix_rtg_watch_page_size;
        if (page_offset < (uae_u32)offset) {
            page_offset = offset;
        }
        unix_rtg_writewatch_pages[index][count++] = bank->baseaddr + page_offset;
        unix_rtg_dirty_pages[index][page] = 0;
    }

    unix_rtg_writewatch_count[index] = count;
    unix_rtg_watch_offset[index] = offset;
    if (gwwbufp) {
        *gwwbufp = unix_rtg_writewatch_pages[index];
    }
    if (startp) {
        *startp = start;
    }
    return count;
}

bool picasso_is_vram_dirty(int index, uaecptr addr, int size)
{
    if (!unix_picasso_valid_watch_index(index) || size <= 0) {
        return false;
    }

    addrbank *bank = gfxmem_banks[index];
    if (!bank->baseaddr || addr < bank->start) {
        return false;
    }
    uae_u32 base_offset = addr - bank->start;
    uae_u32 offset = base_offset + unix_rtg_watch_offset[index];
    if (offset < base_offset || offset >= bank->allocated_size) {
        return false;
    }

    uae_u8 *start = bank->baseaddr + offset;
    uae_u8 *end = start + size;
    for (int i = 0; i < unix_rtg_writewatch_count[index]; i++) {
        uae_u8 *page = unix_rtg_writewatch_pages[index][i];
        uae_u8 *page_end = page + unix_rtg_watch_page_size;
        if (start < page_end && end > page) {
            return true;
        }
    }
    return false;
}

struct unix_rtg_board {
    int id;
    const TCHAR *name;
    const TCHAR *manufacturer;
    const TCHAR *config_name;
    int config_type;
};

static const unix_rtg_board unix_rtg_boards[] = {
    { GFXBOARD_UAE_Z2, _T("UAE [Zorro II]"), NULL, _T("ZorroII"), 2 },
    { GFXBOARD_UAE_Z3, _T("UAE [Zorro III]"), NULL, _T("ZorroIII"), 3 },
    { -1, NULL, NULL, NULL, 0 }
};

static const unix_rtg_board *unix_rtg_find_board(int id)
{
    for (int i = 0; unix_rtg_boards[i].name; i++) {
        if (unix_rtg_boards[i].id == id) {
            return &unix_rtg_boards[i];
        }
    }
    return NULL;
}

enum {
    UNIX_PICASSO_STATE_SETDISPLAY = 1,
    UNIX_PICASSO_STATE_SETPANNING = 2,
    UNIX_PICASSO_STATE_SETGC = 4,
    UNIX_PICASSO_STATE_SETDAC = 8,
    UNIX_PICASSO_STATE_SETSWITCH = 16,
    UNIX_PICASSO_STATE_SPRITE = 32
};

#define UNIX_RTG_DEFAULT_MODEFLAGS (RGBFF_CLUT | RGBFF_R5G6B5PC | RGBFF_B8G8R8A8)
#define UNIX_UAEGFX_VERSION 3
#define UNIX_UAEGFX_REVISION 4

#define UNIX_LIB_SIZE 34
#define UNIX_CARD_FLAGS UNIX_LIB_SIZE
#define UNIX_CARD_EXECBASE (UNIX_CARD_FLAGS + 2)
#define UNIX_CARD_EXPANSIONBASE (UNIX_CARD_EXECBASE + 4)
#define UNIX_CARD_SEGMENTLIST (UNIX_CARD_EXPANSIONBASE + 4)
#define UNIX_CARD_NAME (UNIX_CARD_SEGMENTLIST + 4)
#define UNIX_CARD_RESLIST (UNIX_CARD_NAME + 4)
#define UNIX_CARD_RESLISTSIZE (UNIX_CARD_RESLIST + 4)
#define UNIX_CARD_BOARDINFO (UNIX_CARD_RESLISTSIZE + 4)
#define UNIX_CARD_VBLANKIRQ (UNIX_CARD_BOARDINFO + 4)
#define UNIX_CARD_PORTSIRQ (UNIX_CARD_VBLANKIRQ + 22)
#define UNIX_CARD_IRQFLAG (UNIX_CARD_PORTSIRQ + 22)
#define UNIX_CARD_IRQPTR (UNIX_CARD_IRQFLAG + 4)
#define UNIX_CARD_IRQEXECBASE (UNIX_CARD_IRQPTR + 4)
#define UNIX_CARD_IRQCODE (UNIX_CARD_IRQEXECBASE + 4)
#define UNIX_CARD_SIZEOF (UNIX_CARD_IRQCODE + 2 * 11 * 2)

#define UNIX_BIB_GRANTDIRECTACCESS 26
#define UNIX_BIF_GRANTDIRECTACCESS (1 << UNIX_BIB_GRANTDIRECTACCESS)
#define UNIX_BIB_VBLANKINTERRUPT 4
#define UNIX_BIF_VBLANKINTERRUPT (1 << UNIX_BIB_VBLANKINTERRUPT)
#define UNIX_BIB_DACSWITCH 28
#define UNIX_BIF_DACSWITCH (1 << UNIX_BIB_DACSWITCH)

#define UNIX_PSSO_BoardInfo_FreeCardMem (PSSO_BoardInfo_AllocCardMem + 4)
#define UNIX_PSSO_BoardInfo_SetSwitch (UNIX_PSSO_BoardInfo_FreeCardMem + 4)
#define UNIX_PSSO_BoardInfo_SetColorArray (UNIX_PSSO_BoardInfo_SetSwitch + 4)
#define UNIX_PSSO_BoardInfo_SetDAC (UNIX_PSSO_BoardInfo_SetColorArray + 4)
#define UNIX_PSSO_BoardInfo_SetGC (UNIX_PSSO_BoardInfo_SetDAC + 4)
#define UNIX_PSSO_BoardInfo_SetPanning (UNIX_PSSO_BoardInfo_SetGC + 4)
#define UNIX_PSSO_BoardInfo_CalculateBytesPerRow (UNIX_PSSO_BoardInfo_SetPanning + 4)
#define UNIX_PSSO_BoardInfo_CalculateMemory (UNIX_PSSO_BoardInfo_CalculateBytesPerRow + 4)
#define UNIX_PSSO_BoardInfo_GetCompatibleFormats (UNIX_PSSO_BoardInfo_CalculateMemory + 4)
#define UNIX_PSSO_BoardInfo_SetDisplay (UNIX_PSSO_BoardInfo_GetCompatibleFormats + 4)
#define UNIX_PSSO_BoardInfo_ResolvePixelClock (UNIX_PSSO_BoardInfo_SetDisplay + 4)
#define UNIX_PSSO_BoardInfo_GetPixelClock (UNIX_PSSO_BoardInfo_ResolvePixelClock + 4)
#define UNIX_PSSO_BoardInfo_SetClock (UNIX_PSSO_BoardInfo_GetPixelClock + 4)
#define UNIX_PSSO_BoardInfo_SetMemoryMode (UNIX_PSSO_BoardInfo_SetClock + 4)
#define UNIX_PSSO_BoardInfo_SetWriteMask (UNIX_PSSO_BoardInfo_SetMemoryMode + 4)
#define UNIX_PSSO_BoardInfo_SetClearMask (UNIX_PSSO_BoardInfo_SetWriteMask + 4)
#define UNIX_PSSO_BoardInfo_SetReadPlane (UNIX_PSSO_BoardInfo_SetClearMask + 4)
#define UNIX_PSSO_BoardInfo_WaitVerticalSync (UNIX_PSSO_BoardInfo_SetReadPlane + 4)
#define UNIX_PSSO_BoardInfo_SetInterrupt (UNIX_PSSO_BoardInfo_WaitVerticalSync + 4)
#define UNIX_PSSO_BoardInfo_WaitBlitter (UNIX_PSSO_BoardInfo_SetInterrupt + 4)
#define UNIX_PSSO_BoardInfo_ScrollPlanar (UNIX_PSSO_BoardInfo_WaitBlitter + 4)
#define UNIX_PSSO_BoardInfo_ScrollPlanarDefault (UNIX_PSSO_BoardInfo_ScrollPlanar + 4)
#define UNIX_PSSO_BoardInfo_UpdatePlanar (UNIX_PSSO_BoardInfo_ScrollPlanarDefault + 4)
#define UNIX_PSSO_BoardInfo_UpdatePlanarDefault (UNIX_PSSO_BoardInfo_UpdatePlanar + 4)
#define UNIX_PSSO_BoardInfo_BlitPlanar2Chunky (UNIX_PSSO_BoardInfo_UpdatePlanarDefault + 4)
#define UNIX_PSSO_BoardInfo_BlitPlanar2ChunkyDefault (UNIX_PSSO_BoardInfo_BlitPlanar2Chunky + 4)
#define UNIX_PSSO_BoardInfo_FillRect (UNIX_PSSO_BoardInfo_BlitPlanar2ChunkyDefault + 4)
#define UNIX_PSSO_BoardInfo_FillRectDefault (UNIX_PSSO_BoardInfo_FillRect + 4)
#define UNIX_PSSO_BoardInfo_InvertRect (UNIX_PSSO_BoardInfo_FillRectDefault + 4)
#define UNIX_PSSO_BoardInfo_InvertRectDefault (UNIX_PSSO_BoardInfo_InvertRect + 4)
#define UNIX_PSSO_BoardInfo_BlitRect (UNIX_PSSO_BoardInfo_InvertRectDefault + 4)
#define UNIX_PSSO_BoardInfo_BlitRectDefault (UNIX_PSSO_BoardInfo_BlitRect + 4)
#define UNIX_PSSO_BoardInfo_BlitTemplate (UNIX_PSSO_BoardInfo_BlitRectDefault + 4)
#define UNIX_PSSO_BoardInfo_BlitTemplateDefault (UNIX_PSSO_BoardInfo_BlitTemplate + 4)
#define UNIX_PSSO_BoardInfo_BlitPattern (UNIX_PSSO_BoardInfo_BlitTemplateDefault + 4)
#define UNIX_PSSO_BoardInfo_BlitPatternDefault (UNIX_PSSO_BoardInfo_BlitPattern + 4)
#define UNIX_PSSO_BoardInfo_DrawLine (UNIX_PSSO_BoardInfo_BlitPatternDefault + 4)
#define UNIX_PSSO_BoardInfo_DrawLineDefault (UNIX_PSSO_BoardInfo_DrawLine + 4)
#define UNIX_PSSO_BoardInfo_BlitRectNoMaskComplete (UNIX_PSSO_BoardInfo_DrawLineDefault + 4)
#define UNIX_PSSO_BoardInfo_BlitRectNoMaskCompleteDefault (UNIX_PSSO_BoardInfo_BlitRectNoMaskComplete + 4)
#define UNIX_PSSO_BoardInfo_BlitPlanar2Direct (UNIX_PSSO_BoardInfo_BlitRectNoMaskCompleteDefault + 4)
#define UNIX_PSSO_BoardInfo_BlitPlanar2DirectDefault (UNIX_PSSO_BoardInfo_BlitPlanar2Direct + 4)
#define UNIX_PSSO_BoardInfo_Reserved0 (UNIX_PSSO_BoardInfo_BlitPlanar2DirectDefault + 4)
#define UNIX_PSSO_BoardInfo_Reserved0Default (UNIX_PSSO_BoardInfo_Reserved0 + 4)
#define UNIX_PSSO_BoardInfo_Reserved1 (UNIX_PSSO_BoardInfo_Reserved0Default + 4)
#define UNIX_PSSO_SetSplitPosition (UNIX_PSSO_BoardInfo_Reserved1 + 4)
#define UNIX_PSSO_ReInitMemory (UNIX_PSSO_SetSplitPosition + 4)
#define UNIX_PSSO_BoardInfo_GetCompatibleDACFormats (UNIX_PSSO_ReInitMemory + 4)
#define UNIX_PSSO_BoardInfo_CoerceMode (UNIX_PSSO_BoardInfo_GetCompatibleDACFormats + 4)
#define UNIX_PSSO_BoardInfo_Reserved3Default (UNIX_PSSO_BoardInfo_CoerceMode + 4)
#define UNIX_PSSO_BoardInfo_Reserved4 (UNIX_PSSO_BoardInfo_Reserved3Default + 4)
#define UNIX_PSSO_BoardInfo_Reserved4Default (UNIX_PSSO_BoardInfo_Reserved4 + 4)
#define UNIX_PSSO_BoardInfo_Reserved5 (UNIX_PSSO_BoardInfo_Reserved4Default + 4)
#define UNIX_PSSO_BoardInfo_Reserved5Default (UNIX_PSSO_BoardInfo_Reserved5 + 4)
#define UNIX_PSSO_BoardInfo_SetDPMSLevel (UNIX_PSSO_BoardInfo_Reserved5Default + 4)
#define UNIX_PSSO_BoardInfo_ResetChip (UNIX_PSSO_BoardInfo_SetDPMSLevel + 4)
#define UNIX_PSSO_BoardInfo_GetFeatureAttrs (UNIX_PSSO_BoardInfo_ResetChip + 4)
#define UNIX_PSSO_BoardInfo_AllocBitMap (UNIX_PSSO_BoardInfo_GetFeatureAttrs + 4)
#define UNIX_PSSO_BoardInfo_FreeBitMap (UNIX_PSSO_BoardInfo_AllocBitMap + 4)
#define UNIX_PSSO_BoardInfo_GetBitMapAttr (UNIX_PSSO_BoardInfo_FreeBitMap + 4)
#define UNIX_PSSO_BoardInfo_SetSprite (UNIX_PSSO_BoardInfo_GetBitMapAttr + 4)
#define UNIX_PSSO_BoardInfo_SetSpritePosition (UNIX_PSSO_BoardInfo_SetSprite + 4)
#define UNIX_PSSO_BoardInfo_SetSpriteImage (UNIX_PSSO_BoardInfo_SetSpritePosition + 4)
#define UNIX_PSSO_BoardInfo_SetSpriteColor (UNIX_PSSO_BoardInfo_SetSpriteImage + 4)

#define UNIX_RTG_CURSOR_MAXWIDTH 256
#define UNIX_RTG_CURSOR_MAXHEIGHT 256

struct unix_rtg_sprite_state {
    bool enabled;
    bool visible;
    bool image_valid;
    int x;
    int y;
    int xoffset;
    int yoffset;
    int width;
    int height;
    uae_u8 pixels[UNIX_RTG_CURSOR_MAXWIDTH * UNIX_RTG_CURSOR_MAXHEIGHT];
    uae_u32 colors[4];
};

static unix_rtg_sprite_state unix_rtg_sprites[MAX_AMIGAMONITORS];
static uaecptr unix_uaegfx_vblankname;
static uaecptr unix_uaegfx_portsname;
static uaecptr unix_uaegfx_abi_interrupt;
static bool unix_uaegfx_interrupt_enabled;

struct unix_rtg_mode_size {
    int width;
    int height;
};

static const unix_rtg_mode_size unix_rtg_standard_mode_sizes[] = {
    { 320, 200 },
    { 320, 240 },
    { 320, 256 },
    { 640, 400 },
    { 640, 480 },
    { 640, 512 },
    { 800, 600 },
    { 1024, 768 },
    { 1280, 720 },
    { 1280, 1024 },
    { 0, 0 }
};
static const int unix_rtg_max_mode_sizes = 128;
static unix_rtg_mode_size unix_rtg_mode_sizes[unix_rtg_max_mode_sizes];
static int unix_rtg_mode_count;
static bool unix_rtg_mode_sizes_ready;

static uaecptr unix_picasso_amem;
static uaecptr unix_picasso_amemend;
static uaecptr unix_uaegfx_resname;
static uaecptr unix_uaegfx_prefix;
static uaecptr unix_uaegfx_resid;
static uaecptr unix_uaegfx_base;
static uaecptr unix_uaegfx_rom;
static uaecptr unix_picasso_boardinfo;
static uae_u32 unix_p96_restored_flags;
static int unix_uaegfx_old;
static int unix_uaegfx_active;
static uae_u32 unix_reserved_gfxmem;

enum {
    UNIX_RTG_TRACE_FILLRECT = 1 << 0,
    UNIX_RTG_TRACE_INVERTRECT = 1 << 1,
    UNIX_RTG_TRACE_BLITRECT = 1 << 2,
    UNIX_RTG_TRACE_BLITRECT_NOMASK = 1 << 3,
    UNIX_RTG_TRACE_BLITTEMPLATE = 1 << 4,
    UNIX_RTG_TRACE_BLITPATTERN = 1 << 5,
    UNIX_RTG_TRACE_PLANAR2CHUNKY = 1 << 6,
    UNIX_RTG_TRACE_PLANAR2DIRECT = 1 << 7
};

static bool unix_rtg_trace_blits(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *value = getenv("WINUAE_UNIX_RTG_TRACE_BLITS");
        enabled = value && value[0] && strcmp(value, "0") != 0;
    }
    return enabled != 0;
}

static void unix_rtg_trace_blit(uae_u32 bit, const TCHAR *name)
{
    static uae_u32 seen;
    if (unix_rtg_trace_blits() && !(seen & bit)) {
        seen |= bit;
        write_log(_T("Unix RTG %s\n"), name);
    }
}

static uae_u32 unix_rtg_modeflags(void)
{
    return currprefs.picasso96_modeflags ? currprefs.picasso96_modeflags : UNIX_RTG_DEFAULT_MODEFLAGS;
}

static void unix_rtg_add_mode_size(int width, int height)
{
    if (width <= 0 || height <= 0) {
        return;
    }

    int insert = 0;
    while (insert < unix_rtg_mode_count) {
        unix_rtg_mode_size *mode = &unix_rtg_mode_sizes[insert];
        if (mode->width == width && mode->height == height) {
            return;
        }
        if (mode->width > width || (mode->width == width && mode->height > height)) {
            break;
        }
        insert++;
    }
    if (unix_rtg_mode_count >= unix_rtg_max_mode_sizes) {
        return;
    }
    for (int i = unix_rtg_mode_count; i > insert; i--) {
        unix_rtg_mode_sizes[i] = unix_rtg_mode_sizes[i - 1];
    }
    unix_rtg_mode_sizes[insert].width = width;
    unix_rtg_mode_sizes[insert].height = height;
    unix_rtg_mode_count++;
}

static void unix_rtg_rebuild_mode_sizes(void)
{
    unix_rtg_mode_count = 0;
    unix_rtg_mode_sizes_ready = true;

    for (int i = 0; unix_rtg_standard_mode_sizes[i].width; i++) {
        unix_rtg_add_mode_size(unix_rtg_standard_mode_sizes[i].width, unix_rtg_standard_mode_sizes[i].height);
    }

    struct unix_video_display_mode modes[unix_rtg_max_mode_sizes];
    int count = unix_video_get_display_modes(currprefs.gfx_apmode[APMODE_RTG].gfx_display,
        modes, unix_rtg_max_mode_sizes);
    for (int i = 0; i < count; i++) {
        unix_rtg_add_mode_size(modes[i].width, modes[i].height);
    }

    write_log(_T("Unix RTG host mode list: %d modes\n"), unix_rtg_mode_count);
}

static void unix_rtg_ensure_mode_sizes(void)
{
    if (!unix_rtg_mode_sizes_ready) {
        unix_rtg_rebuild_mode_sizes();
    }
}

static int unix_picasso_bytes_per_pixel(uae_u32 rgbfmt)
{
    switch (rgbfmt) {
    case RGBFB_CLUT:
    case RGBFB_Y4U1V1:
        return 1;
    case RGBFB_R5G6B5:
    case RGBFB_R5G5B5:
    case RGBFB_R5G6B5PC:
    case RGBFB_R5G5B5PC:
    case RGBFB_B5G6R5PC:
    case RGBFB_B5G5R5PC:
    case RGBFB_Y4U2V2:
        return 2;
    case RGBFB_R8G8B8:
    case RGBFB_B8G8R8:
        return 3;
    case RGBFB_A8R8G8B8:
    case RGBFB_A8B8G8R8:
    case RGBFB_R8G8B8A8:
    case RGBFB_B8G8R8A8:
        return 4;
    }
    return 0;
}

static uae_u32 unix_picasso_rgbmask_for_format(uae_u32 rgbfmt)
{
    switch (rgbfmt) {
    case RGBFB_CLUT:
        return RGBMASK_8BIT;
    case RGBFB_R5G5B5PC:
    case RGBFB_R5G5B5:
    case RGBFB_B5G5R5PC:
        return RGBMASK_15BIT;
    case RGBFB_R5G6B5PC:
    case RGBFB_R5G6B5:
    case RGBFB_B5G6R5PC:
        return RGBMASK_16BIT;
    case RGBFB_R8G8B8:
    case RGBFB_B8G8R8:
        return RGBMASK_24BIT;
    case RGBFB_A8R8G8B8:
    case RGBFB_A8B8G8R8:
    case RGBFB_R8G8B8A8:
    case RGBFB_B8G8R8A8:
        return RGBMASK_32BIT;
    }
    return 0;
}

static bool unix_picasso_renderinfo(TrapContext *ctx, uaecptr renderinfo, RenderInfo *ri)
{
    if (!ri || !trap_valid_address(ctx, renderinfo, PSSO_RenderInfo_sizeof)) {
        write_log(_T("Unix RTG invalid RenderInfo: %08X\n"), renderinfo);
        return false;
    }

    uaecptr mem = trap_get_long(ctx, renderinfo + PSSO_RenderInfo_Memory);
    int bytes_per_row = (uae_s16)trap_get_word(ctx, renderinfo + PSSO_RenderInfo_BytesPerRow);
    RGBFTYPE rgbfmt = (RGBFTYPE)trap_get_long(ctx, renderinfo + PSSO_RenderInfo_RGBFormat);

    if (bytes_per_row < 0 || !trap_valid_address(ctx, mem, bytes_per_row > 0 ? bytes_per_row : 1)) {
        write_log(_T("Unix RTG invalid RenderInfo memory: %08X bpr=%d fmt=%d\n"),
            mem, bytes_per_row, rgbfmt);
        return false;
    }

    ri->AMemory = mem;
    ri->Memory = get_real_address(mem);
    ri->BytesPerRow = bytes_per_row;
    ri->RGBFormat = rgbfmt;
    return ri->Memory != NULL;
}

static bool unix_picasso_validate_rect(RenderInfo *ri, uae_u32 rgbfmt,
    uae_u32 *x, uae_u32 *y, uae_u32 *width, uae_u32 *height)
{
    if (!ri || !x || !y || !width || !height ||
        *x > 32767 || *y > 32767 || *width > 32767 || *height > 32767) {
        return false;
    }
    if (!*width || !*height) {
        return true;
    }

    int bytes_per_pixel = unix_picasso_bytes_per_pixel(rgbfmt);
    int bytes_per_row = ri->BytesPerRow;
    if (!bytes_per_pixel || bytes_per_row < 0) {
        return false;
    }
    if (!bytes_per_row) {
        if (*x) {
            return false;
        }
        bytes_per_row = *width * bytes_per_pixel;
    }
    if (*x * bytes_per_pixel >= (uae_u32)bytes_per_row) {
        return false;
    }

    uae_u32 x2 = *x + *width;
    if (x2 * bytes_per_pixel > (uae_u32)bytes_per_row) {
        x2 = bytes_per_row / bytes_per_pixel;
        *width = x2 - *x;
    }

    addrbank *bank = gfxmem_banks[0];
    if (!bank || !bank->baseaddr || ri->AMemory < bank->start || ri->AMemory >= bank->start + bank->allocated_size) {
        return false;
    }
    uaecptr end = ri->AMemory + (*y + *height - 1) * ri->BytesPerRow + (*x + *width - 1) * bytes_per_pixel;
    return end >= bank->start && end < bank->start + bank->allocated_size;
}

static void unix_picasso_store_pen(uae_u8 *dst, uae_u32 pen, int bytes_per_pixel)
{
    switch (bytes_per_pixel) {
    case 1:
        dst[0] = (uae_u8)pen;
        break;
    case 2:
        do_put_mem_word((uae_u16 *)dst, (uae_u16)pen);
        break;
    case 3:
        dst[0] = (uae_u8)pen;
        dst[1] = (uae_u8)(pen >> 8);
        dst[2] = (uae_u8)(pen >> 16);
        break;
    case 4:
        do_put_mem_long((uae_u32 *)dst, pen);
        break;
    }
}

static void unix_picasso_store_pen_masked(uae_u8 *dst, uae_u32 pen, int bytes_per_pixel, uae_u8 mask)
{
    if (bytes_per_pixel == 1 && mask != 0xff) {
        dst[0] = (uae_u8)((pen & mask) | (dst[0] & ~mask));
        return;
    }
    unix_picasso_store_pen(dst, pen, bytes_per_pixel);
}

static uae_u8 unix_picasso_blit_op(uae_u8 src, uae_u8 dst, BLIT_OPCODE op)
{
    switch (op) {
    case BLIT_FALSE:
        return 0;
    case BLIT_NOR:
        return (uae_u8)~(src | dst);
    case BLIT_ONLYDST:
        return (uae_u8)(dst & ~src);
    case BLIT_NOTSRC:
        return (uae_u8)~src;
    case BLIT_ONLYSRC:
        return (uae_u8)(src & ~dst);
    case BLIT_NOTDST:
        return (uae_u8)~dst;
    case BLIT_EOR:
        return src ^ dst;
    case BLIT_NAND:
        return (uae_u8)~(src & dst);
    case BLIT_AND:
        return src & dst;
    case BLIT_NEOR:
        return (uae_u8)~(src ^ dst);
    case BLIT_DST:
        return dst;
    case BLIT_NOTONLYSRC:
        return (uae_u8)(~src | dst);
    case BLIT_SRC:
        return src;
    case BLIT_NOTONLYDST:
        return (uae_u8)(~dst | src);
    case BLIT_OR:
        return src | dst;
    case BLIT_TRUE:
        return 0xff;
    default:
        return dst;
    }
}

static uae_u32 unix_picasso_rgb_full_mask(uae_u32 rgbfmt)
{
    static const uae_u32 masks[] = {
        0x00000000,
        0xffffffff,
        0x00ffffff,
        0x00ffffff,
        0xffffffff,
        0x7fff7fff,
        0xffffff00,
        0xffffff00,
        0x00ffffff,
        0x00ffffff,
        0xffffffff,
        0xff7fff7f,
        0xffffffff,
        0x7fff7fff,
        0xffffffff,
        0xffffffff
    };

    if (rgbfmt < sizeof masks / sizeof masks[0]) {
        return masks[rgbfmt];
    }
    return 0xffffffff;
}

static void unix_picasso_xor_pixel(uae_u8 *dst, int bytes_per_pixel, uae_u32 rgbfmt, uae_u8 mask)
{
    uae_u32 rgbmask = unix_picasso_rgb_full_mask(rgbfmt);

    switch (bytes_per_pixel) {
    case 1:
        dst[0] ^= (uae_u8)(rgbmask & mask);
        break;
    case 2:
        do_put_mem_word((uae_u16 *)dst, do_get_mem_word((uae_u16 *)dst) ^ (uae_u16)rgbmask);
        break;
    case 3:
        dst[0] ^= 0xff;
        dst[1] ^= 0xff;
        dst[2] ^= 0xff;
        break;
    case 4:
        do_put_mem_long((uae_u32 *)dst, do_get_mem_long((uae_u32 *)dst) ^ rgbmask);
        break;
    }
}

static uae_u32 unix_picasso_load_pen(const uae_u8 *src, int bytes_per_pixel)
{
    switch (bytes_per_pixel) {
    case 1:
        return src[0];
    case 2:
        return do_get_mem_word((uae_u16 *)src);
    case 3:
        return src[0] | ((uae_u32)src[1] << 8) | ((uae_u32)src[2] << 16);
    case 4:
        return do_get_mem_long((uae_u32 *)src);
    }
    return 0;
}

static uae_u32 unix_picasso_blit_op_long(uae_u32 src, uae_u32 src_inv, uae_u32 dst, uae_u32 dst_inv,
    BLIT_OPCODE op)
{
    switch (op) {
    case BLIT_FALSE:
        return 0;
    case BLIT_NOR:
        return ~(src | dst);
    case BLIT_ONLYDST:
        return dst & src_inv;
    case BLIT_NOTSRC:
        return src_inv;
    case BLIT_ONLYSRC:
        return src & dst_inv;
    case BLIT_NOTDST:
        return dst_inv;
    case BLIT_EOR:
        return src ^ dst;
    case BLIT_NAND:
        return ~(src & dst);
    case BLIT_AND:
        return src & dst;
    case BLIT_NEOR:
        return ~(src ^ dst);
    case BLIT_DST:
        return dst;
    case BLIT_NOTONLYSRC:
        return src_inv | dst;
    case BLIT_NOTONLYDST:
        return dst_inv | src;
    case BLIT_OR:
        return src | dst;
    case BLIT_TRUE:
        return 0xffffffff;
    default:
        return dst;
    }
}

static int unix_picasso_depth_supported(int depth)
{
    uae_u32 flags = unix_rtg_modeflags();

    if (depth == 8 && (flags & RGBFF_CLUT)) {
        return 1;
    }
    if (depth == 15 && (flags & (RGBFF_R5G5B5PC | RGBFF_R5G5B5 | RGBFF_B5G5R5PC))) {
        return 1;
    }
    if (depth == 16 && (flags & (RGBFF_R5G6B5PC | RGBFF_R5G6B5 | RGBFF_B5G6R5PC))) {
        return 1;
    }
    if (depth == 24 && (flags & (RGBFF_R8G8B8 | RGBFF_B8G8R8))) {
        return 1;
    }
    if (depth == 32 && (flags & (RGBFF_A8R8G8B8 | RGBFF_A8B8G8R8 | RGBFF_R8G8B8A8 | RGBFF_B8G8R8A8))) {
        return 1;
    }
    return 0;
}

static int unix_picasso_mode_depth_count(void)
{
    int depths = 0;
    depths += unix_picasso_depth_supported(8);
    depths += unix_picasso_depth_supported(15);
    depths += unix_picasso_depth_supported(16);
    depths += unix_picasso_depth_supported(24);
    depths += unix_picasso_depth_supported(32);
    return depths;
}

static bool unix_picasso_mode_fits(int width, int height, int bytes_per_pixel)
{
    return bytes_per_pixel > 0 &&
        gfxmem_bank.allocated_size >= (uae_u32)width * (uae_u32)height * (uae_u32)bytes_per_pixel;
}

static void unix_picasso_max_resolution(int mode, int *max_width, int *max_height)
{
    static const int mode_depths[MAXMODES][3] = {
        { 0, 0, 0 },
        { 8, 0, 0 },
        { 15, 16, 0 },
        { 24, 0, 0 },
        { 32, 0, 0 }
    };

    *max_width = 0;
    *max_height = 0;
    if (mode < 0 || mode >= MAXMODES) {
        return;
    }

    unix_rtg_ensure_mode_sizes();
    for (int i = 0; i < unix_rtg_mode_count; i++) {
        int width = unix_rtg_mode_sizes[i].width;
        int height = unix_rtg_mode_sizes[i].height;
        for (int depth_index = 0; mode_depths[mode][depth_index]; depth_index++) {
            int depth = mode_depths[mode][depth_index];
            if (!unix_picasso_depth_supported(depth) ||
                !unix_picasso_mode_fits(width, height, (depth + 7) / 8)) {
                continue;
            }
            if (width > *max_width) {
                *max_width = width;
            }
            if (height > *max_height) {
                *max_height = height;
            }
        }
    }
}

static int unix_picasso_resolution_count(void)
{
    int count = 0;
    unix_rtg_ensure_mode_sizes();
    for (int i = 0; i < unix_rtg_mode_count; i++) {
        int width = unix_rtg_mode_sizes[i].width;
        int height = unix_rtg_mode_sizes[i].height;
        if ((uae_u32)width * (uae_u32)height <= gfxmem_bank.allocated_size - 256) {
            count++;
        }
    }
    return count;
}

static int unix_picasso_resolution_memory_size(void)
{
    if (currprefs.picasso96_noautomodes) {
        return 0;
    }
    return unix_picasso_resolution_count() *
        (PSSO_LibResolution_sizeof + unix_picasso_mode_depth_count() * PSSO_ModeInfo_sizeof);
}

static int unix_picasso_mode_id(int width, int height, int index)
{
    static const struct {
        int width;
        int height;
        int id;
    } mode_ids[] = {
        { 320, 240, 1 },
        { 640, 480, 3 },
        { 800, 600, 4 },
        { 1024, 768, 5 },
        { 1280, 1024, 7 },
        { 320, 256, 9 },
        { 640, 512, 10 },
        { 1280, 720, 142 },
        { 0, 0, 0 }
    };

    for (int i = 0; mode_ids[i].width; i++) {
        if (mode_ids[i].width == width && mode_ids[i].height == height) {
            return 0x50001000 | (mode_ids[i].id * 0x10000);
        }
    }
    return 0x51001000 - index * 0x10000;
}

static void unix_amiga_list_add_tail(TrapContext *ctx, uaecptr list, uaecptr node)
{
    trap_put_long(ctx, node + 0, list + 4);
    trap_put_long(ctx, node + 4, trap_get_long(ctx, list + 8));
    trap_put_long(ctx, trap_get_long(ctx, list + 8), node);
    trap_put_long(ctx, list + 8, node);
}

static void unix_copy_lib_resolution(TrapContext *ctx, const struct LibResolution *res, uaecptr ptr)
{
    trap_set_bytes(ctx, ptr, 0, PSSO_LibResolution_sizeof);
    for (int i = 0; i < 6; i++) {
        trap_put_byte(ctx, ptr + PSSO_LibResolution_P96ID + i, res->P96ID[i]);
    }
    for (int i = 0; i < MAXRESOLUTIONNAMELENGTH && res->Name[i]; i++) {
        trap_put_byte(ctx, ptr + PSSO_LibResolution_Name + i, res->Name[i]);
    }
    trap_put_long(ctx, ptr + PSSO_LibResolution_DisplayID, res->DisplayID);
    trap_put_word(ctx, ptr + PSSO_LibResolution_Width, res->Width);
    trap_put_word(ctx, ptr + PSSO_LibResolution_Height, res->Height);
    trap_put_word(ctx, ptr + PSSO_LibResolution_Flags, res->Flags);
    for (int i = 0; i < MAXMODES; i++) {
        trap_put_long(ctx, ptr + PSSO_LibResolution_Modes + i * 4, res->Modes[i]);
    }
    trap_put_long(ctx, ptr + 10, ptr + PSSO_LibResolution_P96ID);
    trap_put_long(ctx, ptr + PSSO_LibResolution_BoardInfo, res->BoardInfo);
}

static void unix_fill_mode_info(TrapContext *ctx, uaecptr ptr, struct LibResolution *res, int width, int height, int depth)
{
    switch (depth) {
    case 8:
        res->Modes[CHUNKY] = ptr;
        break;
    case 15:
    case 16:
        res->Modes[HICOLOR] = ptr;
        break;
    case 24:
        res->Modes[TRUECOLOR] = ptr;
        break;
    default:
        res->Modes[TRUEALPHA] = ptr;
        break;
    }

    trap_set_bytes(ctx, ptr, 0, PSSO_ModeInfo_sizeof);
    trap_put_word(ctx, ptr + PSSO_ModeInfo_Active, 1);
    trap_put_word(ctx, ptr + PSSO_ModeInfo_Width, width);
    trap_put_word(ctx, ptr + PSSO_ModeInfo_Height, height);
    trap_put_byte(ctx, ptr + PSSO_ModeInfo_Depth, depth);
    trap_put_word(ctx, ptr + PSSO_ModeInfo_HorTotal, width + 8);
    trap_put_word(ctx, ptr + PSSO_ModeInfo_HorBlankSize, 8);
    trap_put_word(ctx, ptr + PSSO_ModeInfo_HorSyncStart, 2);
    trap_put_word(ctx, ptr + PSSO_ModeInfo_HorSyncSize, 2);
    trap_put_word(ctx, ptr + PSSO_ModeInfo_VerTotal, height + 8);
    trap_put_word(ctx, ptr + PSSO_ModeInfo_VerBlankSize, 8);
    trap_put_word(ctx, ptr + PSSO_ModeInfo_VerSyncStart, 2);
    trap_put_word(ctx, ptr + PSSO_ModeInfo_VerSyncSize, 2);
    trap_put_byte(ctx, ptr + PSSO_ModeInfo_first_union, 98);
    trap_put_byte(ctx, ptr + PSSO_ModeInfo_second_union, 14);
    trap_put_long(ctx, ptr + PSSO_ModeInfo_PixelClock, width * height * 60);
}

static bool unix_add_mode(TrapContext *ctx, uaecptr board_info, uaecptr *amem, int width, int height, int index)
{
    static const int depths[] = { 8, 15, 16, 24, 32 };
    struct LibResolution res = { 0 };
    bool added = false;

    memcpy(res.P96ID, "P96-0:", 6);
    snprintf(res.Name, sizeof res.Name, "UAE:%4dx%4d", width, height);
    res.DisplayID = unix_picasso_mode_id(width, height, index);
    res.BoardInfo = board_info;
    res.Width = width;
    res.Height = height;
    res.Flags = P96F_PUBLIC;

    for (int i = 0; i < (int)(sizeof depths / sizeof depths[0]); i++) {
        int depth = depths[i];
        int bytes_per_pixel = (depth + 7) / 8;
        if (!unix_picasso_depth_supported(depth)) {
            continue;
        }
        if (!unix_picasso_mode_fits(width, height, bytes_per_pixel)) {
            continue;
        }
        unix_fill_mode_info(ctx, *amem, &res, width, height, depth);
        *amem += PSSO_ModeInfo_sizeof;
        added = true;
    }

    if (!added) {
        return false;
    }

    unix_copy_lib_resolution(ctx, &res, *amem);
    unix_amiga_list_add_tail(ctx, board_info + PSSO_BoardInfo_ResolutionsList, *amem);
    *amem += PSSO_LibResolution_sizeof;
    write_log(_T("Unix RTG mode: %08X %dx%d\n"), res.DisplayID, width, height);
    return true;
}

static void unix_picasso_init_alloc(TrapContext *ctx, int size)
{
    unix_picasso_amem = 0;
    unix_picasso_amemend = 0;
    if (unix_uaegfx_base) {
        size = trap_get_long(ctx, unix_uaegfx_base + UNIX_CARD_RESLISTSIZE);
        unix_picasso_amem = trap_get_long(ctx, unix_uaegfx_base + UNIX_CARD_RESLIST);
    } else if (unix_uaegfx_active) {
        unix_reserved_gfxmem = size;
        unix_picasso_amem = gfxmem_bank.start + gfxmem_bank.allocated_size - size;
    }
    unix_picasso_amemend = unix_picasso_amem + size;
    write_log(_T("Unix RTG P96 RESINFO: %08X-%08X (%d bytes)\n"),
        unix_picasso_amem, unix_picasso_amemend, size);
    picasso_allocatewritewatch(0, gfxmem_bank.allocated_size);
}

static uae_u32 REGPARAM2 unix_picasso_find_card(TrapContext *ctx)
{
    uaecptr board_info = trap_get_areg(ctx, 0);
    struct picasso96_state_struct *state = &picasso96_state[currprefs.rtgboards[0].monitor_id];

    if (!unix_uaegfx_active || !(gfxmem_bank.flags & ABFLAG_MAPPED)) {
        return 0;
    }
    if (unix_uaegfx_base) {
        trap_put_long(ctx, unix_uaegfx_base + UNIX_CARD_BOARDINFO, board_info);
    }
    unix_picasso_boardinfo = board_info;

    if (!gfxmem_bank.allocated_size || state->CardFound) {
        return 0;
    }
    trap_put_long(ctx, board_info + PSSO_BoardInfo_MemoryBase, gfxmem_bank.start);
    trap_put_long(ctx, board_info + PSSO_BoardInfo_MemorySize, gfxmem_bank.allocated_size - unix_reserved_gfxmem);
    state->CardFound = 1;
    write_log(_T("Unix RTG FindCard: boardinfo=%08X mem=%08X size=%u\n"),
        board_info, gfxmem_bank.start, gfxmem_bank.allocated_size - unix_reserved_gfxmem);
    return (uae_u32)-1;
}

static uae_u32 REGPARAM2 unix_picasso_set_switch(TrapContext *ctx)
{
    int monid = currprefs.rtgboards[0].monitor_id;
    struct amigadisplay *ad = &adisplays[monid];
    struct picasso96_state_struct *state = &picasso96_state[monid];
    struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[monid];
    bool oldstate = ad->picasso_on || ad->picasso_requested_on;
    bool requested = (trap_get_dreg(ctx, 0) & 0xffff) != 0;

    if (state->SwitchState != (requested ? 1 : 0)) {
        state->SwitchState = requested ? 1 : 0;
        atomic_or(&vidinfo->picasso_state_change, UNIX_PICASSO_STATE_SETSWITCH);
    }
    ad->picasso_requested_on = requested;
    write_log(_T("Unix RTG SetSwitch: %d old=%d\n"), requested ? 1 : 0, oldstate ? 1 : 0);
    return oldstate ? 1 : 0;
}

static uae_u32 REGPARAM2 unix_picasso_set_color_array(TrapContext *ctx)
{
    int monid = currprefs.rtgboards[0].monitor_id;
    struct picasso96_state_struct *state = &picasso96_state[monid];
    struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[monid];
    uaecptr board_info = trap_get_areg(ctx, 0);
    uae_u16 start = trap_get_dreg(ctx, 0);
    uae_u16 count = trap_get_dreg(ctx, 1);

    if (start >= 256 || start + count > 256) {
        return 0;
    }
    for (int i = 0; i < count; i++) {
        uaecptr src = board_info + PSSO_BoardInfo_CLUT + (start + i) * 3;
        state->CLUT[start + i].Red = trap_get_byte(ctx, src + 0);
        state->CLUT[start + i].Green = trap_get_byte(ctx, src + 1);
        state->CLUT[start + i].Blue = trap_get_byte(ctx, src + 2);
        state->CLUT[start + i].Pad = 0;
        vidinfo->clut[start + i] =
            0xff000000 |
            ((uae_u32)state->CLUT[start + i].Red << 16) |
            ((uae_u32)state->CLUT[start + i].Green << 8) |
            state->CLUT[start + i].Blue;
    }
    vidinfo->full_refresh = 1;
    return 1;
}

static uae_u32 REGPARAM2 unix_picasso_set_dac(TrapContext *ctx)
{
    int monid = currprefs.rtgboards[0].monitor_id;
    struct picasso96_state_struct *state = &picasso96_state[monid];
    struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[monid];
    uae_u16 index = trap_get_dreg(ctx, 0);
    RGBFTYPE rgbfmt = (RGBFTYPE)trap_get_dreg(ctx, 7);

    if (state->advDragging) {
        vidinfo->dacrgbformat[index ? 1 : 0] = rgbfmt;
    } else {
        vidinfo->dacrgbformat[0] = rgbfmt;
        vidinfo->dacrgbformat[1] = rgbfmt;
    }
    atomic_or(&vidinfo->picasso_state_change, UNIX_PICASSO_STATE_SETDAC);
    return 1;
}

static uae_u32 REGPARAM2 unix_picasso_set_gc(TrapContext *ctx)
{
    int monid = currprefs.rtgboards[0].monitor_id;
    struct picasso96_state_struct *state = &picasso96_state[monid];
    struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[monid];
    uaecptr board_info = trap_get_areg(ctx, 0);
    uaecptr mode_info = trap_get_areg(ctx, 1);

    trap_put_long(ctx, board_info + PSSO_BoardInfo_ModeInfo, mode_info);
    trap_put_word(ctx, board_info + PSSO_BoardInfo_Border, trap_get_dreg(ctx, 0));

    uae_u16 width = trap_get_word(ctx, mode_info + PSSO_ModeInfo_Width);
    if (width != state->Width) {
        state->ModeChanged = true;
    }
    state->Width = width;
    state->VirtualWidth = state->Width;
    uae_u16 height = trap_get_word(ctx, mode_info + PSSO_ModeInfo_Height);
    if (height != state->Height) {
        state->ModeChanged = true;
    }
    state->Height = height;
    state->VirtualHeight = state->Height;
    state->GC_Depth = trap_get_byte(ctx, mode_info + PSSO_ModeInfo_Depth);
    state->GC_Flags = trap_get_byte(ctx, mode_info + PSSO_ModeInfo_Flags);
    state->HLineDBL = 1;
    state->VLineDBL = 1;
    state->HostAddress = NULL;
    atomic_or(&vidinfo->picasso_state_change, UNIX_PICASSO_STATE_SETGC);
    write_log(_T("Unix RTG SetGC: %dx%dx%d\n"), state->Width, state->Height, state->GC_Depth);
    return 1;
}

static void unix_picasso_set_panning_init(struct picasso96_state_struct *state)
{
    state->XYOffset = state->Address + (state->XOffset * state->BytesPerPixel) + (state->YOffset * state->BytesPerRow);
    state->BigAssBitmap = state->VirtualWidth > state->Width || state->VirtualHeight > state->Height;
}

static uae_u32 REGPARAM2 unix_picasso_set_panning(TrapContext *ctx)
{
    int monid = currprefs.rtgboards[0].monitor_id;
    struct picasso96_state_struct *state = &picasso96_state[monid];
    struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[monid];
    uaecptr board_info = trap_get_areg(ctx, 0);
    uaecptr bitmap_extra = trap_get_long(ctx, board_info + PSSO_BoardInfo_BitMapExtra);

    state->Address = trap_get_areg(ctx, 1);
    state->XOffset = (uae_s16)(trap_get_dreg(ctx, 1) & 0xffff);
    state->YOffset = (uae_s16)(trap_get_dreg(ctx, 2) & 0xffff);
    trap_put_word(ctx, board_info + PSSO_BoardInfo_XOffset, (uae_u16)state->XOffset);
    trap_put_word(ctx, board_info + PSSO_BoardInfo_YOffset, (uae_u16)state->YOffset);

    state->VirtualWidth = bitmap_extra ? trap_get_word(ctx, bitmap_extra + PSSO_BitMapExtra_Width) : trap_get_dreg(ctx, 0);
    state->VirtualHeight = bitmap_extra ? trap_get_word(ctx, bitmap_extra + PSSO_BitMapExtra_Height) : state->Height;
    if (!state->VirtualWidth) {
        state->VirtualWidth = state->Width;
    }
    if (!state->VirtualHeight) {
        state->VirtualHeight = state->Height;
    }
    state->RGBFormat = (RGBFTYPE)trap_get_dreg(ctx, 7);
    state->BytesPerPixel = unix_picasso_bytes_per_pixel(state->RGBFormat);
    state->BytesPerRow = state->VirtualWidth * state->BytesPerPixel;
    unix_picasso_set_panning_init(state);
    state->Extent = state->Address + state->BytesPerRow * state->VirtualHeight;
    vidinfo->set_panning_called = 1;

    atomic_or(&vidinfo->picasso_state_change, UNIX_PICASSO_STATE_SETPANNING);
    write_log(_T("Unix RTG SetPanning: addr=%08X xy=%d,%d virt=%dx%d bpr=%d fmt=%d\n"),
        state->Address, state->XOffset, state->YOffset, state->VirtualWidth, state->VirtualHeight,
        state->BytesPerRow, state->RGBFormat);
    return 1;
}

static uae_u32 REGPARAM2 unix_picasso_calculate_bytes_per_row(TrapContext *ctx)
{
    return (trap_get_dreg(ctx, 0) & 0xffff) * unix_picasso_bytes_per_pixel(trap_get_dreg(ctx, 7));
}

static uae_u32 REGPARAM2 unix_picasso_coerce_mode(TrapContext *ctx)
{
    uae_u16 board_width = trap_get_dreg(ctx, 2);
    uae_u16 friend_width = trap_get_dreg(ctx, 3);
    return board_width > friend_width ? board_width : friend_width;
}

static uae_u32 REGPARAM2 unix_picasso_get_compatible_dac_formats(TrapContext *ctx)
{
    int monid = currprefs.rtgboards[0].monitor_id;
    struct picasso96_state_struct *state = &picasso96_state[monid];
    uae_u32 rgbfmt = trap_get_dreg(ctx, 7);

    if (unix_picasso_rgbmask_for_format(rgbfmt)) {
        state->advDragging = true;
        return RGBMASK_8BIT | RGBMASK_15BIT | RGBMASK_16BIT | RGBMASK_24BIT | RGBMASK_32BIT;
    }
    return 0;
}

static uae_u32 REGPARAM2 unix_picasso_fill_rect(TrapContext *ctx)
{
    unix_rtg_trace_blit(UNIX_RTG_TRACE_FILLRECT, _T("FillRect"));

    RenderInfo ri;
    uaecptr renderinfo = trap_get_areg(ctx, 1);
    uae_u32 x = (uae_u16)trap_get_dreg(ctx, 0);
    uae_u32 y = (uae_u16)trap_get_dreg(ctx, 1);
    uae_u32 width = (uae_u16)trap_get_dreg(ctx, 2);
    uae_u32 height = (uae_u16)trap_get_dreg(ctx, 3);
    uae_u32 pen = trap_get_dreg(ctx, 4);
    uae_u8 mask = (uae_u8)trap_get_dreg(ctx, 5);
    uae_u32 rgbfmt = trap_get_dreg(ctx, 7);
    int bytes_per_pixel = unix_picasso_bytes_per_pixel(rgbfmt);

    if (!bytes_per_pixel || !unix_picasso_renderinfo(ctx, renderinfo, &ri)) {
        return 0;
    }
    if (!unix_picasso_validate_rect(&ri, rgbfmt, &x, &y, &width, &height)) {
        write_log(_T("Unix RTG FillRect invalid region: %08X:%d:%d (%dx%d)-(%dx%d)\n"),
            ri.AMemory, ri.BytesPerRow, ri.RGBFormat, x, y, width, height);
        return 1;
    }
    if (!width || !height) {
        return 1;
    }

    uae_u8 *dst = ri.Memory + y * ri.BytesPerRow + x * bytes_per_pixel;
    for (uae_u32 row = 0; row < height; row++, dst += ri.BytesPerRow) {
        if (bytes_per_pixel == 1 && mask != 0xff) {
            for (uae_u32 col = 0; col < width; col++) {
                dst[col] = (uae_u8)((pen & mask) | (dst[col] & ~mask));
            }
        } else {
            for (uae_u32 col = 0; col < width; col++) {
                unix_picasso_store_pen(dst + col * bytes_per_pixel, pen, bytes_per_pixel);
            }
        }
    }
    unix_picasso_mark_renderinfo_rect(&ri, x, y, width, height, bytes_per_pixel);
    return 1;
}

static uae_u32 REGPARAM2 unix_picasso_invert_rect(TrapContext *ctx)
{
    unix_rtg_trace_blit(UNIX_RTG_TRACE_INVERTRECT, _T("InvertRect"));

    RenderInfo ri;
    uaecptr renderinfo = trap_get_areg(ctx, 1);
    uae_u32 x = (uae_u16)trap_get_dreg(ctx, 0);
    uae_u32 y = (uae_u16)trap_get_dreg(ctx, 1);
    uae_u32 width = (uae_u16)trap_get_dreg(ctx, 2);
    uae_u32 height = (uae_u16)trap_get_dreg(ctx, 3);
    uae_u8 mask = (uae_u8)trap_get_dreg(ctx, 4);
    uae_u32 rgbfmt = trap_get_dreg(ctx, 7);
    int bytes_per_pixel = unix_picasso_bytes_per_pixel(rgbfmt);

    if (!bytes_per_pixel || !unix_picasso_renderinfo(ctx, renderinfo, &ri)) {
        return 0;
    }
    if (!unix_picasso_validate_rect(&ri, rgbfmt, &x, &y, &width, &height)) {
        write_log(_T("Unix RTG InvertRect invalid region: %08X:%d:%d (%dx%d)-(%dx%d)\n"),
            ri.AMemory, ri.BytesPerRow, ri.RGBFormat, x, y, width, height);
        return 1;
    }
    if (!width || !height) {
        return 1;
    }

    if (bytes_per_pixel > 1) {
        mask = 0xff;
    }
    if (!mask) {
        return 1;
    }

    uae_u32 width_in_bytes = width * bytes_per_pixel;
    uae_u8 *dst = ri.Memory + y * ri.BytesPerRow + x * bytes_per_pixel;
    for (uae_u32 row = 0; row < height; row++, dst += ri.BytesPerRow) {
        for (uae_u32 col = 0; col < width_in_bytes; col++) {
            dst[col] ^= mask;
        }
    }
    unix_picasso_mark_renderinfo_rect(&ri, x, y, width, height, bytes_per_pixel);
    return 1;
}

static uae_u32 unix_picasso_blit_rect_common(TrapContext *ctx, uaecptr srcinfo, uaecptr dstinfo,
    uae_u32 srcx, uae_u32 srcy, uae_u32 dstx, uae_u32 dsty, uae_u32 width, uae_u32 height,
    uae_u8 mask, uae_u32 rgbfmt, BLIT_OPCODE opcode)
{
    RenderInfo src_ri;
    RenderInfo dst_ri;
    RenderInfo *dst = &src_ri;
    int bytes_per_pixel = unix_picasso_bytes_per_pixel(rgbfmt);

    if (!bytes_per_pixel || !unix_picasso_renderinfo(ctx, srcinfo, &src_ri)) {
        return 0;
    }
    if (dstinfo) {
        if (!unix_picasso_renderinfo(ctx, dstinfo, &dst_ri)) {
            return 0;
        }
        dst = &dst_ri;
    }
    if (bytes_per_pixel > 1 && mask != 0xff) {
        return 0;
    }
    if (!unix_picasso_validate_rect(&src_ri, rgbfmt, &srcx, &srcy, &width, &height) ||
        !unix_picasso_validate_rect(dst, rgbfmt, &dstx, &dsty, &width, &height)) {
        write_log(_T("Unix RTG BlitRect invalid region: %08X->%08X fmt=%d (%dx%d)\n"),
            src_ri.AMemory, dst->AMemory, rgbfmt, width, height);
        return 1;
    }
    if (!width || !height) {
        return 1;
    }

    uae_u32 width_in_bytes = width * bytes_per_pixel;
    uae_u8 *srcbase = src_ri.Memory + srcy * src_ri.BytesPerRow + srcx * bytes_per_pixel;
    uae_u8 *dstbase = dst->Memory + dsty * dst->BytesPerRow + dstx * bytes_per_pixel;

    if (opcode == BLIT_SRC && mask == 0xff) {
        if (dstbase > srcbase && dstbase < srcbase + height * src_ri.BytesPerRow) {
            for (uae_s32 row = height - 1; row >= 0; row--) {
                memmove(dstbase + row * dst->BytesPerRow, srcbase + row * src_ri.BytesPerRow, width_in_bytes);
            }
        } else {
            for (uae_u32 row = 0; row < height; row++) {
                memmove(dstbase + row * dst->BytesPerRow, srcbase + row * src_ri.BytesPerRow, width_in_bytes);
            }
        }
        unix_picasso_mark_renderinfo_rect(dst, dstx, dsty, width, height, bytes_per_pixel);
        return 1;
    }

    if (opcode < BLIT_FALSE || opcode >= BLIT_LAST) {
        return 0;
    }
    for (uae_u32 row = 0; row < height; row++) {
        uae_u8 *srcrow = srcbase + row * src_ri.BytesPerRow;
        uae_u8 *dstrow = dstbase + row * dst->BytesPerRow;
        for (uae_u32 col = 0; col < width_in_bytes; col++) {
            uae_u8 olddst = dstrow[col];
            uae_u8 value = unix_picasso_blit_op(srcrow[col], olddst, opcode);
            dstrow[col] = (uae_u8)((value & mask) | (olddst & ~mask));
        }
    }
    unix_picasso_mark_renderinfo_rect(dst, dstx, dsty, width, height, bytes_per_pixel);
    return 1;
}

static uae_u32 REGPARAM2 unix_picasso_blit_rect(TrapContext *ctx)
{
    unix_rtg_trace_blit(UNIX_RTG_TRACE_BLITRECT, _T("BlitRect"));

    uaecptr renderinfo = trap_get_areg(ctx, 1);
    return unix_picasso_blit_rect_common(ctx, renderinfo, 0,
        (uae_u16)trap_get_dreg(ctx, 0),
        (uae_u16)trap_get_dreg(ctx, 1),
        (uae_u16)trap_get_dreg(ctx, 2),
        (uae_u16)trap_get_dreg(ctx, 3),
        (uae_u16)trap_get_dreg(ctx, 4),
        (uae_u16)trap_get_dreg(ctx, 5),
        (uae_u8)trap_get_dreg(ctx, 6),
        trap_get_dreg(ctx, 7),
        BLIT_SRC);
}

static uae_u32 REGPARAM2 unix_picasso_blit_rect_no_mask_complete(TrapContext *ctx)
{
    unix_rtg_trace_blit(UNIX_RTG_TRACE_BLITRECT_NOMASK, _T("BlitRectNoMaskComplete"));

    return unix_picasso_blit_rect_common(ctx, trap_get_areg(ctx, 1), trap_get_areg(ctx, 2),
        (uae_u16)trap_get_dreg(ctx, 0),
        (uae_u16)trap_get_dreg(ctx, 1),
        (uae_u16)trap_get_dreg(ctx, 2),
        (uae_u16)trap_get_dreg(ctx, 3),
        (uae_u16)trap_get_dreg(ctx, 4),
        (uae_u16)trap_get_dreg(ctx, 5),
        0xff,
        trap_get_dreg(ctx, 7),
        (BLIT_OPCODE)(trap_get_dreg(ctx, 6) & 0xff));
}

struct unix_picasso_pattern_info {
    uaecptr memory;
    uae_u16 xoffset;
    uae_u16 yoffset;
    uae_u32 fgpen;
    uae_u32 bgpen;
    uae_u8 size;
    uae_u8 drawmode;
};

static bool unix_picasso_load_pattern_info(TrapContext *ctx, uaecptr pattern_info, unix_picasso_pattern_info *pattern)
{
    if (!pattern || !trap_valid_address(ctx, pattern_info, PSSO_Pattern_sizeof)) {
        return false;
    }

    pattern->memory = trap_get_long(ctx, pattern_info + PSSO_Pattern_Memory);
    pattern->xoffset = trap_get_word(ctx, pattern_info + PSSO_Pattern_XOffset);
    pattern->yoffset = trap_get_word(ctx, pattern_info + PSSO_Pattern_YOffset);
    pattern->fgpen = trap_get_long(ctx, pattern_info + PSSO_Pattern_FgPen);
    pattern->bgpen = trap_get_long(ctx, pattern_info + PSSO_Pattern_BgPen);
    pattern->size = trap_get_byte(ctx, pattern_info + PSSO_Pattern_Size);
    pattern->drawmode = trap_get_byte(ctx, pattern_info + PSSO_Pattern_DrawMode);
    if (pattern->size > 16) {
        return false;
    }
    return trap_valid_address(ctx, pattern->memory, (2u << pattern->size));
}

static uae_u32 REGPARAM2 unix_picasso_blit_pattern(TrapContext *ctx)
{
    unix_rtg_trace_blit(UNIX_RTG_TRACE_BLITPATTERN, _T("BlitPattern"));

    RenderInfo ri;
    unix_picasso_pattern_info pattern;
    uaecptr renderinfo = trap_get_areg(ctx, 1);
    uaecptr pattern_info = trap_get_areg(ctx, 2);
    uae_u32 x = (uae_u16)trap_get_dreg(ctx, 0);
    uae_u32 y = (uae_u16)trap_get_dreg(ctx, 1);
    uae_u32 width = (uae_u16)trap_get_dreg(ctx, 2);
    uae_u32 height = (uae_u16)trap_get_dreg(ctx, 3);
    uae_u8 mask = (uae_u8)trap_get_dreg(ctx, 4);
    uae_u32 rgbfmt = trap_get_dreg(ctx, 7);
    int bytes_per_pixel = unix_picasso_bytes_per_pixel(rgbfmt);

    if (!bytes_per_pixel ||
        !unix_picasso_renderinfo(ctx, renderinfo, &ri) ||
        !unix_picasso_load_pattern_info(ctx, pattern_info, &pattern)) {
        return 0;
    }
    if (!unix_picasso_validate_rect(&ri, rgbfmt, &x, &y, &width, &height)) {
        write_log(_T("Unix RTG BlitPattern invalid region: %08X:%d:%d (%dx%d)-(%dx%d)\n"),
            ri.AMemory, ri.BytesPerRow, ri.RGBFormat, x, y, width, height);
        return 1;
    }
    if (!width || !height) {
        return 1;
    }

    bool inverted = (pattern.drawmode & INVERS) != 0;
    uae_u8 drawmode = pattern.drawmode & 3;
    uae_u32 ymask = (1u << pattern.size) - 1;
    uae_u8 *dst = ri.Memory + y * ri.BytesPerRow + x * bytes_per_pixel;

    for (uae_u32 row = 0; row < height; row++, dst += ri.BytesPerRow) {
        uae_u32 pattern_row = (row + pattern.yoffset) & ymask;
        uae_u16 data = trap_get_word(ctx, pattern.memory + pattern_row * 2);
        uae_u8 *dstrow = dst;

        for (uae_u32 col = 0; col < width; col++, dstrow += bytes_per_pixel) {
            uae_u32 bit_index = (col + pattern.xoffset) & 15;
            bool bit_set = (data & (0x8000 >> bit_index)) != 0;
            if (inverted) {
                bit_set = !bit_set;
            }

            switch (drawmode) {
            case JAM1:
                if (bit_set) {
                    unix_picasso_store_pen_masked(dstrow, pattern.fgpen, bytes_per_pixel, mask);
                }
                break;
            case JAM2:
                unix_picasso_store_pen_masked(dstrow, bit_set ? pattern.fgpen : pattern.bgpen, bytes_per_pixel, mask);
                break;
            case COMP:
                if (bit_set) {
                    unix_picasso_xor_pixel(dstrow, bytes_per_pixel, rgbfmt, mask);
                }
                break;
            }
        }
    }
    unix_picasso_mark_renderinfo_rect(&ri, x, y, width, height, bytes_per_pixel);
    return 1;
}

struct unix_picasso_template_info {
    uaecptr memory;
    uae_s16 bytes_per_row;
    uae_u8 xoffset;
    uae_u8 drawmode;
    uae_u32 fgpen;
    uae_u32 bgpen;
};

static bool unix_picasso_load_template_info(TrapContext *ctx, uaecptr template_info,
    unix_picasso_template_info *tmpl)
{
    if (!tmpl || !trap_valid_address(ctx, template_info, PSSO_Template_sizeof)) {
        return false;
    }

    tmpl->memory = trap_get_long(ctx, template_info + PSSO_Template_Memory);
    tmpl->bytes_per_row = (uae_s16)trap_get_word(ctx, template_info + PSSO_Template_BytesPerRow);
    tmpl->xoffset = trap_get_byte(ctx, template_info + PSSO_Template_XOffset);
    tmpl->drawmode = trap_get_byte(ctx, template_info + PSSO_Template_DrawMode);
    tmpl->fgpen = trap_get_long(ctx, template_info + PSSO_Template_FgPen);
    tmpl->bgpen = trap_get_long(ctx, template_info + PSSO_Template_BgPen);
    if (tmpl->bytes_per_row <= 0) {
        return false;
    }
    return trap_valid_address(ctx, tmpl->memory, 1);
}

static bool unix_picasso_validate_template_source(TrapContext *ctx,
    const unix_picasso_template_info *tmpl, uae_u32 width, uae_u32 height)
{
    if (!height || !width) {
        return trap_valid_address(ctx, tmpl->memory, 1);
    }

    uae_u64 last_byte = (uae_u64)(height - 1) * tmpl->bytes_per_row + (tmpl->xoffset + width - 1) / 8;
    return last_byte <= 0xffffffffu && trap_valid_address(ctx, tmpl->memory, (uae_u32)last_byte + 1);
}

static uae_u32 REGPARAM2 unix_picasso_blit_template(TrapContext *ctx)
{
    unix_rtg_trace_blit(UNIX_RTG_TRACE_BLITTEMPLATE, _T("BlitTemplate"));

    RenderInfo ri;
    unix_picasso_template_info tmpl;
    uaecptr renderinfo = trap_get_areg(ctx, 1);
    uaecptr template_info = trap_get_areg(ctx, 2);
    uae_u32 x = (uae_u16)trap_get_dreg(ctx, 0);
    uae_u32 y = (uae_u16)trap_get_dreg(ctx, 1);
    uae_u32 width = (uae_u16)trap_get_dreg(ctx, 2);
    uae_u32 height = (uae_u16)trap_get_dreg(ctx, 3);
    uae_u8 mask = (uae_u8)trap_get_dreg(ctx, 4);
    uae_u32 rgbfmt = trap_get_dreg(ctx, 7);
    int bytes_per_pixel = unix_picasso_bytes_per_pixel(rgbfmt);

    if (!bytes_per_pixel ||
        !unix_picasso_renderinfo(ctx, renderinfo, &ri) ||
        !unix_picasso_load_template_info(ctx, template_info, &tmpl)) {
        return 0;
    }
    if (!unix_picasso_validate_rect(&ri, rgbfmt, &x, &y, &width, &height)) {
        write_log(_T("Unix RTG BlitTemplate invalid region: %08X:%d:%d (%dx%d)-(%dx%d)\n"),
            ri.AMemory, ri.BytesPerRow, ri.RGBFormat, x, y, width, height);
        return 1;
    }
    if (!width || !height) {
        return 1;
    }
    if (!unix_picasso_validate_template_source(ctx, &tmpl, width, height)) {
        return 0;
    }

    bool inverted = (tmpl.drawmode & INVERS) != 0;
    uae_u8 drawmode = tmpl.drawmode & 3;
    uae_u8 *dst = ri.Memory + y * ri.BytesPerRow + x * bytes_per_pixel;

    for (uae_u32 row = 0; row < height; row++, dst += ri.BytesPerRow) {
        uaecptr srcrow = tmpl.memory + row * tmpl.bytes_per_row;
        uae_u8 *dstrow = dst;

        for (uae_u32 col = 0; col < width; col++, dstrow += bytes_per_pixel) {
            uae_u32 bit_index = tmpl.xoffset + col;
            uae_u8 data = trap_get_byte(ctx, srcrow + bit_index / 8);
            bool bit_set = (data & (0x80 >> (bit_index & 7))) != 0;
            if (inverted) {
                bit_set = !bit_set;
            }

            switch (drawmode) {
            case JAM1:
                if (bit_set) {
                    unix_picasso_store_pen_masked(dstrow, tmpl.fgpen, bytes_per_pixel, mask);
                }
                break;
            case JAM2:
                unix_picasso_store_pen_masked(dstrow, bit_set ? tmpl.fgpen : tmpl.bgpen, bytes_per_pixel, mask);
                break;
            case COMP:
                if (bit_set) {
                    unix_picasso_xor_pixel(dstrow, bytes_per_pixel, rgbfmt, mask);
                }
                break;
            }
        }
    }
    unix_picasso_mark_renderinfo_rect(&ri, x, y, width, height, bytes_per_pixel);
    return 1;
}

struct unix_picasso_bitmap_info {
    uae_u16 bytes_per_row;
    uae_u16 rows;
    uae_u8 depth;
    uaecptr planes[8];
};

static bool unix_picasso_load_bitmap_info(TrapContext *ctx, uaecptr bitmap_info, unix_picasso_bitmap_info *bitmap)
{
    if (!bitmap || !trap_valid_address(ctx, bitmap_info, PSSO_BitMap_sizeof)) {
        return false;
    }

    bitmap->bytes_per_row = trap_get_word(ctx, bitmap_info + PSSO_BitMap_BytesPerRow);
    bitmap->rows = trap_get_word(ctx, bitmap_info + PSSO_BitMap_Rows);
    bitmap->depth = trap_get_byte(ctx, bitmap_info + PSSO_BitMap_Depth);
    if (!bitmap->bytes_per_row || !bitmap->depth || bitmap->depth > 8) {
        return false;
    }
    for (int i = 0; i < bitmap->depth; i++) {
        bitmap->planes[i] = trap_get_long(ctx, bitmap_info + PSSO_BitMap_Planes + i * 4);
    }
    return true;
}

static bool unix_picasso_validate_bitmap_source(TrapContext *ctx, const unix_picasso_bitmap_info *bitmap,
    uae_u32 srcx, uae_u32 srcy, uae_u32 width, uae_u32 height)
{
    if (!bitmap || !width || !height) {
        return true;
    }
    if (srcy >= bitmap->rows || height > bitmap->rows - srcy) {
        return false;
    }

    uae_u64 last_byte = (uae_u64)(srcy + height - 1) * bitmap->bytes_per_row + (srcx + width - 1) / 8;
    if (last_byte > 0xffffffffu) {
        return false;
    }
    for (int i = 0; i < bitmap->depth; i++) {
        uaecptr plane = bitmap->planes[i];
        if (plane != 0 && plane != 0xffffffff && !trap_valid_address(ctx, plane, (uae_u32)last_byte + 1)) {
            return false;
        }
    }
    return true;
}

static uae_u8 unix_picasso_planar_pixel(TrapContext *ctx, const unix_picasso_bitmap_info *bitmap,
    uae_u32 x, uae_u32 y)
{
    uae_u8 value = 0;
    uaecptr row_offset = y * bitmap->bytes_per_row + x / 8;
    uae_u8 bit = 0x80 >> (x & 7);

    for (int i = 0; i < bitmap->depth; i++) {
        uaecptr plane = bitmap->planes[i];
        if (plane == 0xffffffff) {
            value |= 1 << i;
        } else if (plane != 0 && (trap_get_byte(ctx, plane + row_offset) & bit)) {
            value |= 1 << i;
        }
    }
    return value;
}

static uae_u32 REGPARAM2 unix_picasso_blit_planar2chunky(TrapContext *ctx)
{
    unix_rtg_trace_blit(UNIX_RTG_TRACE_PLANAR2CHUNKY, _T("BlitPlanar2Chunky"));

    RenderInfo ri;
    unix_picasso_bitmap_info bitmap;
    uaecptr bitmap_info = trap_get_areg(ctx, 1);
    uaecptr renderinfo = trap_get_areg(ctx, 2);
    uae_u32 srcx = (uae_u16)trap_get_dreg(ctx, 0);
    uae_u32 srcy = (uae_u16)trap_get_dreg(ctx, 1);
    uae_u32 dstx = (uae_u16)trap_get_dreg(ctx, 2);
    uae_u32 dsty = (uae_u16)trap_get_dreg(ctx, 3);
    uae_u32 width = (uae_u16)trap_get_dreg(ctx, 4);
    uae_u32 height = (uae_u16)trap_get_dreg(ctx, 5);
    BLIT_OPCODE minterm = (BLIT_OPCODE)(trap_get_dreg(ctx, 6) & 0xff);
    uae_u8 mask = (uae_u8)trap_get_dreg(ctx, 7);

    if (minterm < BLIT_FALSE || minterm >= BLIT_LAST ||
        !unix_picasso_renderinfo(ctx, renderinfo, &ri) ||
        !unix_picasso_load_bitmap_info(ctx, bitmap_info, &bitmap) ||
        unix_picasso_bytes_per_pixel(ri.RGBFormat) != 1) {
        return 0;
    }
    if (!unix_picasso_validate_rect(&ri, ri.RGBFormat, &dstx, &dsty, &width, &height)) {
        write_log(_T("Unix RTG BlitPlanar2Chunky invalid region: %08X:%d:%d (%dx%d)-(%dx%d)\n"),
            ri.AMemory, ri.BytesPerRow, ri.RGBFormat, dstx, dsty, width, height);
        return 1;
    }
    if (!width || !height) {
        return 1;
    }
    if (!unix_picasso_validate_bitmap_source(ctx, &bitmap, srcx, srcy, width, height)) {
        return 0;
    }

    uae_u8 *dst = ri.Memory + dsty * ri.BytesPerRow + dstx;
    for (uae_u32 row = 0; row < height; row++, dst += ri.BytesPerRow) {
        for (uae_u32 col = 0; col < width; col++) {
            uae_u8 src = unix_picasso_planar_pixel(ctx, &bitmap, srcx + col, srcy + row);
            uae_u8 olddst = dst[col];
            uae_u8 value = unix_picasso_blit_op(src, olddst, minterm);
            dst[col] = (uae_u8)((value & mask) | (olddst & ~mask));
        }
    }
    unix_picasso_mark_renderinfo_rect(&ri, dstx, dsty, width, height, 1);
    return 1;
}

static uae_u32 unix_picasso_cim_color(TrapContext *ctx, uaecptr cim, uae_u8 index)
{
    return trap_get_long(ctx, cim + 4 + index * 4);
}

static uae_u32 REGPARAM2 unix_picasso_blit_planar2direct(TrapContext *ctx)
{
    unix_rtg_trace_blit(UNIX_RTG_TRACE_PLANAR2DIRECT, _T("BlitPlanar2Direct"));

    RenderInfo ri;
    unix_picasso_bitmap_info bitmap;
    uaecptr bitmap_info = trap_get_areg(ctx, 1);
    uaecptr renderinfo = trap_get_areg(ctx, 2);
    uaecptr cim = trap_get_areg(ctx, 3);
    uae_u32 srcx = (uae_u16)trap_get_dreg(ctx, 0);
    uae_u32 srcy = (uae_u16)trap_get_dreg(ctx, 1);
    uae_u32 dstx = (uae_u16)trap_get_dreg(ctx, 2);
    uae_u32 dsty = (uae_u16)trap_get_dreg(ctx, 3);
    uae_u32 width = (uae_u16)trap_get_dreg(ctx, 4);
    uae_u32 height = (uae_u16)trap_get_dreg(ctx, 5);
    BLIT_OPCODE minterm = (BLIT_OPCODE)(trap_get_dreg(ctx, 6) & 0xff);
    uae_u8 mask = (uae_u8)trap_get_dreg(ctx, 7);

    if (minterm < BLIT_FALSE || minterm >= BLIT_LAST ||
        !trap_valid_address(ctx, cim, sizeof(ColorIndexMapping)) ||
        !unix_picasso_renderinfo(ctx, renderinfo, &ri) ||
        !unix_picasso_load_bitmap_info(ctx, bitmap_info, &bitmap)) {
        return 0;
    }

    int bytes_per_pixel = unix_picasso_bytes_per_pixel(ri.RGBFormat);
    if (bytes_per_pixel < 2) {
        return 0;
    }
    if (!unix_picasso_validate_rect(&ri, ri.RGBFormat, &dstx, &dsty, &width, &height)) {
        write_log(_T("Unix RTG BlitPlanar2Direct invalid region: %08X:%d:%d (%dx%d)-(%dx%d)\n"),
            ri.AMemory, ri.BytesPerRow, ri.RGBFormat, dstx, dsty, width, height);
        return 1;
    }
    if (!width || !height) {
        return 1;
    }
    if (!unix_picasso_validate_bitmap_source(ctx, &bitmap, srcx, srcy, width, height)) {
        return 0;
    }

    uae_u8 depthmask = (1 << bitmap.depth) - 1;
    uae_u32 rgbmask = unix_picasso_rgb_full_mask(ri.RGBFormat);
    uae_u8 *dst = ri.Memory + dsty * ri.BytesPerRow + dstx * bytes_per_pixel;
    for (uae_u32 row = 0; row < height; row++, dst += ri.BytesPerRow) {
        uae_u8 *dstrow = dst;
        for (uae_u32 col = 0; col < width; col++, dstrow += bytes_per_pixel) {
            uae_u8 value = unix_picasso_planar_pixel(ctx, &bitmap, srcx + col, srcy + row) & depthmask;
            uae_u8 inverted_value = (value ^ mask) & depthmask;
            uae_u32 src = unix_picasso_cim_color(ctx, cim, value);
            uae_u32 src_inv = unix_picasso_cim_color(ctx, cim, inverted_value);
            uae_u32 olddst = unix_picasso_load_pen(dstrow, bytes_per_pixel);
            uae_u32 out;
            if (minterm == BLIT_FALSE) {
                out = unix_picasso_cim_color(ctx, cim, 0);
            } else if (minterm == BLIT_TRUE) {
                out = unix_picasso_cim_color(ctx, cim, depthmask);
            } else {
                out = unix_picasso_blit_op_long(src, src_inv, olddst, olddst ^ rgbmask, minterm);
            }
            unix_picasso_store_pen(dstrow, out, bytes_per_pixel);
        }
    }
    unix_picasso_mark_renderinfo_rect(&ri, dstx, dsty, width, height, bytes_per_pixel);
    return 1;
}

static void unix_picasso_reset_palette(struct picasso96_state_struct *state)
{
    for (int i = 0; i < 256 * 2; i++) {
        state->CLUT[i].Pad = 0xff;
    }
}

static uae_u32 REGPARAM2 unix_picasso_set_display(TrapContext *ctx)
{
    int monid = currprefs.rtgboards[0].monitor_id;
    struct picasso96_state_struct *state = &picasso96_state[monid];
    struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[monid];
    unix_picasso_reset_palette(state);
    atomic_or(&vidinfo->picasso_state_change, UNIX_PICASSO_STATE_SETDISPLAY);
    return !(trap_get_dreg(ctx, 0) != 0);
}

static int unix_rtg_monitor_id(void)
{
    int monid = currprefs.rtgboards[0].monitor_id;
    if (monid < 0 || monid >= MAX_AMIGAMONITORS) {
        monid = 0;
    }
    return monid;
}

static void unix_picasso_update_sprite_colors(unix_rtg_sprite_state *sprite)
{
    sprite->colors[0] &= 0x00ffffff;
    for (int i = 1; i < 4; i++) {
        sprite->colors[i] |= 0xff000000;
    }
}

static uae_u32 REGPARAM2 unix_picasso_set_sprite_position(TrapContext *ctx)
{
    int monid = unix_rtg_monitor_id();
    struct picasso96_state_struct *state = &picasso96_state[monid];
    struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[monid];
    unix_rtg_sprite_state *sprite = &unix_rtg_sprites[monid];
    uaecptr board_info = trap_get_areg(ctx, 0);

    int x = (uae_s16)trap_get_word(ctx, board_info + PSSO_BoardInfo_MouseX) - state->XOffset;
    int y = (uae_s16)trap_get_word(ctx, board_info + PSSO_BoardInfo_MouseY) - state->YOffset;
    if (vidinfo->splitypos >= 0) {
        y += vidinfo->splitypos;
    }

    sprite->x = x - sprite->xoffset;
    sprite->y = y - sprite->yoffset;
    atomic_or(&vidinfo->picasso_state_change, UNIX_PICASSO_STATE_SPRITE);
    return sprite->enabled ? 1 : 0;
}

static uae_u32 REGPARAM2 unix_picasso_set_sprite_color(TrapContext *ctx)
{
    int monid = unix_rtg_monitor_id();
    struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[monid];
    unix_rtg_sprite_state *sprite = &unix_rtg_sprites[monid];
    int index = (trap_get_dreg(ctx, 0) & 0xff) + 1;
    uae_u8 red = trap_get_dreg(ctx, 1);
    uae_u8 green = trap_get_dreg(ctx, 2);
    uae_u8 blue = trap_get_dreg(ctx, 3);

    if (!sprite->enabled || index < 0 || index >= 4) {
        return 0;
    }
    sprite->colors[index] = ((uae_u32)red << 16) | ((uae_u32)green << 8) | blue;
    unix_picasso_update_sprite_colors(sprite);
    atomic_or(&vidinfo->picasso_state_change, UNIX_PICASSO_STATE_SPRITE);
    return 1;
}

static uae_u32 unix_picasso_load_sprite_image(TrapContext *ctx, uaecptr board_info)
{
    int monid = unix_rtg_monitor_id();
    struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[monid];
    unix_rtg_sprite_state *sprite = &unix_rtg_sprites[monid];
    int width = trap_get_byte(ctx, board_info + PSSO_BoardInfo_MouseWidth);
    int height = trap_get_byte(ctx, board_info + PSSO_BoardInfo_MouseHeight);
    uae_u32 flags = trap_get_long(ctx, board_info + PSSO_BoardInfo_Flags);
    int hiressprite = (flags & BIF_HIRESSPRITE) ? 2 : 1;
    bool doubledsprite = (flags & BIF_BIGSPRITE) != 0;
    uaecptr image = trap_get_long(ctx, board_info + PSSO_BoardInfo_MouseImage);
    int datasize = 4 * hiressprite + height * 4 * hiressprite;

    sprite->image_valid = false;
    sprite->width = 0;
    sprite->height = 0;
    sprite->xoffset = trap_get_byte(ctx, board_info + PSSO_BoardInfo_MouseXOffset);
    sprite->yoffset = trap_get_byte(ctx, board_info + PSSO_BoardInfo_MouseYOffset);
    unix_picasso_update_sprite_colors(sprite);

    if (!sprite->enabled) {
        return 0;
    }
    if (width <= 0 || height <= 0 || width > UNIX_RTG_CURSOR_MAXWIDTH ||
        height > UNIX_RTG_CURSOR_MAXHEIGHT || image == 0 ||
        !trap_valid_address(ctx, image, datasize)) {
        atomic_or(&vidinfo->picasso_state_change, UNIX_PICASSO_STATE_SPRITE);
        return 1;
    }

    memset(sprite->pixels, 0, sizeof sprite->pixels);
    for (int y = 0, yy = 0; y < height; y++, yy++) {
        uae_u8 *dst = sprite->pixels + y * UNIX_RTG_CURSOR_MAXWIDTH;
        uae_u8 *previous = dst;
        uaecptr src = image + 4 * hiressprite + yy * 4 * hiressprite;
        int x = 0;
        while (x < width) {
            uae_u32 d1 = trap_get_long(ctx, src);
            uae_u32 d2 = trap_get_long(ctx, src + 2 * hiressprite);
            int maxbits = width - x;
            if (maxbits > 16 * hiressprite) {
                maxbits = 16 * hiressprite;
            }
            for (int bit = 0; bit < maxbits && x < width; bit++) {
                uae_u8 color = ((d2 & 0x80000000) ? 2 : 0) + ((d1 & 0x80000000) ? 1 : 0);
                d1 <<= 1;
                d2 <<= 1;
                *dst++ = color;
                x++;
                if (doubledsprite && x < width) {
                    *dst++ = color;
                    x++;
                }
            }
        }
        if (doubledsprite && y + 1 < height) {
            y++;
            memcpy(sprite->pixels + y * UNIX_RTG_CURSOR_MAXWIDTH, previous, width);
        }
    }

    sprite->width = width;
    sprite->height = height;
    sprite->image_valid = true;
    atomic_or(&vidinfo->picasso_state_change, UNIX_PICASSO_STATE_SPRITE);
    return 1;
}

static uae_u32 REGPARAM2 unix_picasso_set_sprite_image(TrapContext *ctx)
{
    return unix_picasso_load_sprite_image(ctx, trap_get_areg(ctx, 0));
}

static uae_u32 REGPARAM2 unix_picasso_set_sprite(TrapContext *ctx)
{
    int monid = unix_rtg_monitor_id();
    struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[monid];
    unix_rtg_sprite_state *sprite = &unix_rtg_sprites[monid];
    bool activate = trap_get_dreg(ctx, 0) != 0;

    if (!sprite->enabled) {
        return 0;
    }
    if (activate) {
        unix_picasso_load_sprite_image(ctx, trap_get_areg(ctx, 0));
        sprite->visible = true;
    } else {
        sprite->visible = false;
    }
    atomic_or(&vidinfo->picasso_state_change, UNIX_PICASSO_STATE_SPRITE);
    return 1;
}

static uae_u32 REGPARAM2 unix_picasso_set_interrupt(TrapContext *ctx)
{
    unix_uaegfx_interrupt_enabled = trap_get_dreg(ctx, 0) != 0;
    return unix_uaegfx_interrupt_enabled ? 1 : 0;
}

static void unix_init_vblank_abi(TrapContext *ctx, uaecptr base, uaecptr ABI)
{
    if (!base || !ABI) {
        return;
    }
    for (int i = 0; i < 22; i++) {
        trap_put_byte(ctx, ABI + PSSO_BoardInfo_HardInterrupt + i,
            trap_get_byte(ctx, base + UNIX_CARD_PORTSIRQ + i));
    }
    unix_uaegfx_abi_interrupt = ABI;
}

static void unix_init_vblank_irq(TrapContext *ctx, uaecptr base)
{
    uaecptr vblank_irq = base + UNIX_CARD_VBLANKIRQ;
    uaecptr ports_irq = base + UNIX_CARD_PORTSIRQ;
    uaecptr code = base + UNIX_CARD_IRQCODE;
    uaecptr p = code;

    trap_put_word(ctx, vblank_irq + 8, 0x0205);
    trap_put_long(ctx, vblank_irq + 10, unix_uaegfx_vblankname);
    trap_put_long(ctx, vblank_irq + 14, base + UNIX_CARD_IRQFLAG);
    trap_put_long(ctx, vblank_irq + 18, code);

    trap_put_word(ctx, ports_irq + 8, 0x0205);
    trap_put_long(ctx, ports_irq + 10, unix_uaegfx_portsname);
    trap_put_long(ctx, ports_irq + 14, base + UNIX_CARD_IRQFLAG);
    trap_put_long(ctx, ports_irq + 18, code + 11 * 2);

    trap_put_long(ctx, p, 0x08910000); p += 4;  // bclr #0,(a1)
    trap_put_word(ctx, p, 0x670e); p += 2;      // beq.s
    trap_put_long(ctx, p, 0x2c690008); p += 4;  // move.l 8(a1),a6
    trap_put_long(ctx, p, 0x22690004); p += 4;  // move.l 4(a1),a1
    trap_put_long(ctx, p, 0x4eaeff4c); p += 4;  // jsr Cause(a6)
    trap_put_word(ctx, p, 0x7000); p += 2;      // moveq #0,d0
    trap_put_word(ctx, p, RTS); p += 2;

    trap_put_long(ctx, p, 0x08910001); p += 4;  // bclr #1,(a1)
    trap_put_word(ctx, p, 0x670e); p += 2;      // beq.s
    trap_put_long(ctx, p, 0x2c690008); p += 4;  // move.l 8(a1),a6
    trap_put_long(ctx, p, 0x22690004); p += 4;  // move.l 4(a1),a1
    trap_put_long(ctx, p, 0x4eaeff4c); p += 4;  // jsr Cause(a6)
    trap_put_word(ctx, p, 0x7000); p += 2;      // moveq #0,d0
    trap_put_word(ctx, p, RTS);

    trap_call_add_areg(ctx, 1, vblank_irq);
    trap_call_add_dreg(ctx, 0, 5);
    trap_call_lib(ctx, trap_get_long(ctx, 4), -168);
    trap_call_add_areg(ctx, 1, ports_irq);
    trap_call_add_dreg(ctx, 0, 3);
    trap_call_lib(ctx, trap_get_long(ctx, 4), -168);
}

void picasso_trigger_vblank(void)
{
    TrapContext *ctx = NULL;
    if (!unix_uaegfx_abi_interrupt || !unix_uaegfx_base || !unix_uaegfx_interrupt_enabled ||
        !currprefs.rtg_hardwareinterrupt || currprefs.win32_rtgvblankrate == -2) {
        return;
    }
    trap_put_long(ctx, unix_uaegfx_base + UNIX_CARD_IRQPTR,
        unix_uaegfx_abi_interrupt + PSSO_BoardInfo_SoftInterrupt);
    trap_put_byte(ctx, unix_uaegfx_base + UNIX_CARD_IRQFLAG, currprefs.win32_rtgvblankrate ? 2 : 1);
    if (currprefs.win32_rtgvblankrate != 0) {
        INTREQ(0x8000 | 0x0008);
    }
}

void unix_rtg_overlay_sprite(int monid, uae_u32 *dst, int width, int height, int rowpixels)
{
    if (monid < 0 || monid >= MAX_AMIGAMONITORS || !dst || width <= 0 || height <= 0 ||
        rowpixels <= 0 || !currprefs.rtg_hardwaresprite) {
        return;
    }

    unix_rtg_sprite_state *sprite = &unix_rtg_sprites[monid];
    if (!sprite->enabled || !sprite->visible || !sprite->image_valid ||
        sprite->width <= 0 || sprite->height <= 0) {
        return;
    }

    for (int sy = 0; sy < sprite->height; sy++) {
        int dy = sprite->y + sy;
        if (dy < 0 || dy >= height) {
            continue;
        }
        const uae_u8 *src = sprite->pixels + sy * UNIX_RTG_CURSOR_MAXWIDTH;
        uae_u32 *row = dst + dy * rowpixels;
        for (int sx = 0; sx < sprite->width; sx++) {
            int dx = sprite->x + sx;
            if (dx < 0 || dx >= width) {
                continue;
            }
            uae_u8 pen = src[sx];
            if (pen) {
                row[dx] = sprite->colors[pen & 3];
            }
        }
    }
}

static uae_u32 REGPARAM2 unix_picasso_default_unsupported(TrapContext *)
{
    return 0;
}

static void unix_picasso_init_board(TrapContext *ctx, uaecptr board_info)
{
    int monid = currprefs.rtgboards[0].monitor_id;
    struct picasso96_state_struct *state = &picasso96_state[monid];
    uae_u32 flags = trap_get_long(ctx, board_info + PSSO_BoardInfo_Flags);

    write_log(_T("Unix RTG mode mask: %x BI=%08x\n"), unix_rtg_modeflags(), board_info);
    trap_put_word(ctx, board_info + PSSO_BoardInfo_BitsPerCannon, 8);
    trap_put_word(ctx, board_info + PSSO_BoardInfo_RGBFormats, unix_rtg_modeflags());
    trap_put_long(ctx, board_info + PSSO_BoardInfo_BoardType, BT_uaegfx);
    trap_put_long(ctx, board_info + PSSO_BoardInfo_GraphicsControllerType, 0);
    trap_put_long(ctx, board_info + PSSO_BoardInfo_PaletteChipType, 0);
    trap_put_long(ctx, board_info + PSSO_BoardInfo_BoardName, unix_uaegfx_prefix);
    trap_put_long(ctx, board_info + PSSO_BoardInfo_MemoryClock, 200000000);

    for (int i = 0; i < MAXMODES; i++) {
        trap_put_long(ctx, board_info + PSSO_BoardInfo_PixelClockCount + i * 4, 1);
        trap_put_word(ctx, board_info + PSSO_BoardInfo_MaxHorValue + i * 2, 0x4000);
        trap_put_word(ctx, board_info + PSSO_BoardInfo_MaxVerValue + i * 2, 0x4000);
    }

    flags &= 0xffff0000;
    flags |= BIF_BLITTER | BIF_NOMEMORYMODEMIX | BIF_INDISPLAYCHAIN | UNIX_BIF_GRANTDIRECTACCESS;
    if (currprefs.rtg_dacswitch) {
        flags |= UNIX_BIF_DACSWITCH;
    }
    if (currprefs.rtg_hardwaresprite) {
        flags |= BIF_HARDWARESPRITE;
    }
    if (currprefs.rtg_hardwareinterrupt && !unix_uaegfx_old) {
        flags |= UNIX_BIF_VBLANKINTERRUPT;
    }
    trap_put_long(ctx, board_info + PSSO_BoardInfo_Flags, flags);
    write_log(_T("Unix RTG hardware sprite support %s\n"),
        (flags & BIF_HARDWARESPRITE) ? _T("enabled") : _T("disabled"));
    write_log(_T("Unix RTG vblank interrupt support %s\n"),
        (flags & UNIX_BIF_VBLANKINTERRUPT) ? _T("enabled") : _T("disabled"));

    for (int mode = 0; mode < MAXMODES; mode++) {
        int max_width;
        int max_height;
        unix_picasso_max_resolution(mode, &max_width, &max_height);
        trap_put_word(ctx, board_info + PSSO_BoardInfo_MaxHorResolution + mode * 2, max_width);
        trap_put_word(ctx, board_info + PSSO_BoardInfo_MaxVerResolution + mode * 2, max_height);
    }

    state->CardFound = 1;
    memset(&unix_rtg_sprites[monid], 0, sizeof unix_rtg_sprites[monid]);
    unix_rtg_sprites[monid].enabled = currprefs.rtg_hardwaresprite;
    unix_picasso_update_sprite_colors(&unix_rtg_sprites[monid]);
}

#define UNIX_PUTABI(func) \
    do { \
        if (ABI) { \
            trap_put_long(ctx, ABI + (func), here()); \
        } \
        save_rom_absolute(ABI + (func)); \
    } while (0)

#define UNIX_RTGCALL(func, fallback, call) \
    do { \
        UNIX_PUTABI(func); \
        dl(0x48e78000); \
        calltrap(deftrap(call)); \
        dw(0x4a80); \
        dl(0x4cdf0001); \
        dw(0x6604); \
        dw(0x2f28); \
        dw(fallback); \
        dw(RTS); \
    } while (0)

#define UNIX_RTGCALL2(func, call) \
    do { \
        UNIX_PUTABI(func); \
        calltrap(deftrap(call)); \
        dw(RTS); \
    } while (0)

#define UNIX_RTGCALLDEFAULT(func, fallback) \
    do { \
        UNIX_PUTABI(func); \
        dw(0x2f28); \
        dw(fallback); \
        dw(RTS); \
    } while (0)

#define UNIX_RTGNONE(func) \
    do { \
        if (ABI) { \
            trap_put_long(ctx, ABI + (func), start); \
        } \
        save_rom_absolute(ABI + (func)); \
    } while (0)

static void unix_init_uaegfx_funcs(TrapContext *ctx, uaecptr start, uaecptr ABI)
{
    if (unix_uaegfx_old || !ABI) {
        return;
    }

    org(start);
    dw(RTS);

    UNIX_PUTABI(UNIX_PSSO_BoardInfo_ResolvePixelClock);
    dl(0x2340002c);
    dw(0x7000);
    dl(0x137c0062);
    dw(0x002a);
    dl(0x137c000e);
    dw(0x002b);
    dw(RTS);

    UNIX_PUTABI(UNIX_PSSO_BoardInfo_GetPixelClock);
    dw(0x203c);
    dl(100227260);
    dw(RTS);

    UNIX_PUTABI(UNIX_PSSO_BoardInfo_CalculateMemory);
    dw(0x2009);
    dw(RTS);

    UNIX_PUTABI(UNIX_PSSO_BoardInfo_GetCompatibleFormats);
    dw(0x203c);
    dl(RGBMASK_8BIT | RGBMASK_15BIT | RGBMASK_16BIT | RGBMASK_24BIT | RGBMASK_32BIT);
    dw(RTS);

    UNIX_RTGCALL2(UNIX_PSSO_BoardInfo_CalculateBytesPerRow, unix_picasso_calculate_bytes_per_row);
    UNIX_RTGNONE(UNIX_PSSO_BoardInfo_SetClock);
    UNIX_RTGNONE(UNIX_PSSO_BoardInfo_SetMemoryMode);
    UNIX_RTGNONE(UNIX_PSSO_BoardInfo_SetWriteMask);
    UNIX_RTGNONE(UNIX_PSSO_BoardInfo_SetClearMask);
    UNIX_RTGNONE(UNIX_PSSO_BoardInfo_SetReadPlane);
    UNIX_RTGNONE(UNIX_PSSO_BoardInfo_WaitVerticalSync);
    UNIX_RTGNONE(UNIX_PSSO_BoardInfo_WaitBlitter);

    UNIX_RTGCALL(UNIX_PSSO_BoardInfo_BlitPlanar2Direct, UNIX_PSSO_BoardInfo_BlitPlanar2DirectDefault, unix_picasso_blit_planar2direct);
    UNIX_RTGCALL(UNIX_PSSO_BoardInfo_FillRect, UNIX_PSSO_BoardInfo_FillRectDefault, unix_picasso_fill_rect);
    UNIX_RTGCALL(UNIX_PSSO_BoardInfo_BlitRect, UNIX_PSSO_BoardInfo_BlitRectDefault, unix_picasso_blit_rect);
    UNIX_RTGCALL(UNIX_PSSO_BoardInfo_BlitPlanar2Chunky, UNIX_PSSO_BoardInfo_BlitPlanar2ChunkyDefault, unix_picasso_blit_planar2chunky);
    UNIX_RTGCALL(UNIX_PSSO_BoardInfo_BlitTemplate, UNIX_PSSO_BoardInfo_BlitTemplateDefault, unix_picasso_blit_template);
    UNIX_RTGCALL(UNIX_PSSO_BoardInfo_InvertRect, UNIX_PSSO_BoardInfo_InvertRectDefault, unix_picasso_invert_rect);
    UNIX_RTGCALL(UNIX_PSSO_BoardInfo_BlitRectNoMaskComplete, UNIX_PSSO_BoardInfo_BlitRectNoMaskCompleteDefault, unix_picasso_blit_rect_no_mask_complete);
    UNIX_RTGCALL(UNIX_PSSO_BoardInfo_BlitPattern, UNIX_PSSO_BoardInfo_BlitPatternDefault, unix_picasso_blit_pattern);

    UNIX_RTGCALL2(UNIX_PSSO_BoardInfo_SetSwitch, unix_picasso_set_switch);
    UNIX_RTGCALL2(UNIX_PSSO_BoardInfo_SetColorArray, unix_picasso_set_color_array);
    UNIX_RTGCALL2(UNIX_PSSO_BoardInfo_SetDAC, unix_picasso_set_dac);
    UNIX_RTGCALL2(UNIX_PSSO_BoardInfo_SetGC, unix_picasso_set_gc);
    UNIX_RTGCALL2(UNIX_PSSO_BoardInfo_SetPanning, unix_picasso_set_panning);
    UNIX_RTGCALL2(UNIX_PSSO_BoardInfo_SetDisplay, unix_picasso_set_display);

    if (currprefs.rtg_hardwaresprite) {
        UNIX_RTGCALL2(UNIX_PSSO_BoardInfo_SetSprite, unix_picasso_set_sprite);
        UNIX_RTGCALL2(UNIX_PSSO_BoardInfo_SetSpritePosition, unix_picasso_set_sprite_position);
        UNIX_RTGCALL2(UNIX_PSSO_BoardInfo_SetSpriteImage, unix_picasso_set_sprite_image);
        UNIX_RTGCALL2(UNIX_PSSO_BoardInfo_SetSpriteColor, unix_picasso_set_sprite_color);
    }

    UNIX_RTGCALLDEFAULT(UNIX_PSSO_BoardInfo_ScrollPlanar, UNIX_PSSO_BoardInfo_ScrollPlanarDefault);
    UNIX_RTGCALLDEFAULT(UNIX_PSSO_BoardInfo_UpdatePlanar, UNIX_PSSO_BoardInfo_UpdatePlanarDefault);
    UNIX_RTGCALLDEFAULT(UNIX_PSSO_BoardInfo_DrawLine, UNIX_PSSO_BoardInfo_DrawLineDefault);

    if (currprefs.rtg_dacswitch) {
        UNIX_RTGCALL2(UNIX_PSSO_BoardInfo_GetCompatibleDACFormats, unix_picasso_get_compatible_dac_formats);
        UNIX_RTGCALL2(UNIX_PSSO_BoardInfo_CoerceMode, unix_picasso_coerce_mode);
    }

    if (currprefs.rtg_hardwareinterrupt) {
        UNIX_RTGCALL2(UNIX_PSSO_BoardInfo_SetInterrupt, unix_picasso_set_interrupt);
        unix_init_vblank_abi(ctx, unix_uaegfx_base, ABI);
    }

    write_log(_T("Unix RTG uaegfx.card code: %08X-%08X BI=%08X\n"), start, here(), ABI);
}

void restore_p96_finish(void)
{
    int monid = currprefs.rtgboards[0].monitor_id;
    if (monid < 0 || monid >= MAX_AMIGAMONITORS) {
        monid = 0;
    }
    struct amigadisplay *ad = &adisplays[monid];
    struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[monid];

    if (unix_uaegfx_rom && unix_picasso_boardinfo) {
        unix_init_uaegfx_funcs(NULL, unix_uaegfx_rom, unix_picasso_boardinfo);
        ad->picasso_requested_on = (unix_p96_restored_flags & 1) != 0;
        vidinfo->picasso_active = ad->picasso_requested_on;
        atomic_or(&vidinfo->picasso_state_change, UNIX_PICASSO_STATE_SETGC |
            UNIX_PICASSO_STATE_SETPANNING | UNIX_PICASSO_STATE_SETSWITCH);
        set_config_changed();
    }
}

uae_u8 *restore_p96(uae_u8 *src)
{
    int monid = currprefs.rtgboards[0].monitor_id;
    if (monid < 0 || monid >= MAX_AMIGAMONITORS) {
        monid = 0;
    }
    struct picasso96_state_struct *state = &picasso96_state[monid];
    struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[monid];

    if (restore_u32() != 2) {
        return src;
    }

    InitPicasso96(monid);
    unix_p96_restored_flags = restore_u32();
    changed_prefs.rtgboards[0].rtgmem_size = restore_u32();
    state->Address = restore_u32();
    state->RGBFormat = (RGBFTYPE)restore_u32();
    state->Width = restore_u16();
    state->Height = restore_u16();
    state->VirtualWidth = restore_u16();
    state->VirtualHeight = restore_u16();
    state->XOffset = (uae_s16)restore_u16();
    state->YOffset = (uae_s16)restore_u16();
    state->GC_Depth = restore_u8();
    state->GC_Flags = restore_u8();
    state->BytesPerRow = restore_u16();
    state->BytesPerPixel = restore_u8();
    unix_uaegfx_base = restore_u32();
    unix_uaegfx_rom = restore_u32();
    unix_picasso_boardinfo = restore_u32();

    for (int i = 0; i < 4; i++) {
        restore_u32();
    }
    if (unix_p96_restored_flags & 64) {
        for (int i = 0; i < 256; i++) {
            state->CLUT[i].Red = restore_u8();
            state->CLUT[i].Green = restore_u8();
            state->CLUT[i].Blue = restore_u8();
            state->CLUT[i].Pad = 0;
            vidinfo->clut[i] = 0xff000000 |
                ((uae_u32)state->CLUT[i].Red << 16) |
                ((uae_u32)state->CLUT[i].Green << 8) |
                state->CLUT[i].Blue;
        }
    }
    if (unix_p96_restored_flags & 128) {
        for (int i = 0; i < 6; i++) {
            restore_u32();
        }
        restore_u8();
        restore_u8();
        for (int i = 0; i < 13; i++) {
            restore_u16();
        }
        for (int i = 0; i < 256; i++) {
            restore_u8();
            restore_u8();
            restore_u8();
        }
    }

    state->HostAddress = NULL;
    state->HLineDBL = 1;
    state->VLineDBL = 1;
    unix_picasso_set_panning_init(state);
    state->Extent = state->Address + state->BytesPerRow * state->VirtualHeight;
    vidinfo->set_panning_called = (unix_p96_restored_flags & 4) != 0;
    vidinfo->full_refresh = 1;
    return src;
}

uae_u8 *save_p96(size_t *len, uae_u8 *dstptr)
{
    int monid = currprefs.rtgboards[0].monitor_id;
    if (monid < 0 || monid >= MAX_AMIGAMONITORS) {
        monid = 0;
    }
    struct amigadisplay *ad = &adisplays[monid];
    struct picasso96_state_struct *state = &picasso96_state[monid];
    struct picasso_vidbuf_description *vidinfo = &picasso_vidinfo[monid];

    if (len) {
        *len = 0;
    }
    if (currprefs.rtgboards[0].rtgmem_size == 0) {
        return NULL;
    }

    uae_u8 *dstbak;
    uae_u8 *dst;
    if (dstptr) {
        dstbak = dst = dstptr;
    } else {
        dstbak = dst = xmalloc(uae_u8, 2048);
    }

    uae_u32 flags = (ad->picasso_on ? 1 : 0) |
        ((state->Width && state->Height) ? 2 : 0) |
        (vidinfo->set_panning_called ? 4 : 0) |
        64;

    save_u32(2);
    save_u32(flags);
    save_u32(currprefs.rtgboards[0].rtgmem_size);
    save_u32(state->Address);
    save_u32(state->RGBFormat);
    save_u16(state->Width);
    save_u16(state->Height);
    save_u16(state->VirtualWidth);
    save_u16(state->VirtualHeight);
    save_u16((uae_u16)state->XOffset);
    save_u16((uae_u16)state->YOffset);
    save_u8(state->GC_Depth);
    save_u8(state->GC_Flags);
    save_u16(state->BytesPerRow);
    save_u8(state->BytesPerPixel);
    save_u32(unix_uaegfx_base);
    save_u32(unix_uaegfx_rom);
    save_u32(unix_picasso_boardinfo);
    for (int i = 0; i < 4; i++) {
        save_u32(0);
    }
    for (int i = 0; i < 256; i++) {
        save_u8(state->CLUT[i].Red);
        save_u8(state->CLUT[i].Green);
        save_u8(state->CLUT[i].Blue);
    }

    if (len) {
        *len = dst - dstbak;
    }
    return dstbak;
}

static uae_u32 REGPARAM2 unix_picasso_init_card(TrapContext *ctx)
{
    uaecptr board_info = trap_get_areg(ctx, 0);
    uaecptr amem = unix_picasso_amem;
    int count = 0;

    if (!amem) {
        write_log(_T("Unix RTG InitCard without resolution memory\n"));
        return 0;
    }

    unix_picasso_init_board(ctx, board_info);
    unix_init_uaegfx_funcs(ctx, unix_uaegfx_rom, board_info);

    unix_rtg_ensure_mode_sizes();
    for (int i = 0; i < unix_rtg_mode_count; i++) {
        int width = unix_rtg_mode_sizes[i].width;
        int height = unix_rtg_mode_sizes[i].height;
        if ((uae_u32)width * (uae_u32)height > gfxmem_bank.allocated_size - 256) {
            continue;
        }
        if (unix_add_mode(ctx, board_info, &amem, width, height, ++count)) {
            continue;
        }
    }

    if (amem > unix_picasso_amemend) {
        write_log(_T("Unix RTG resolution list overflow %08X > %08X\n"), amem, unix_picasso_amemend);
    }
    write_log(_T("Unix RTG InitCard: %d modes\n"), count);
    return (uae_u32)-1;
}

static uae_u32 REGPARAM2 unix_gfx_open(TrapContext *ctx)
{
    trap_put_word(ctx, unix_uaegfx_base + 32, trap_get_word(ctx, unix_uaegfx_base + 32) + 1);
    return unix_uaegfx_base;
}

static uae_u32 REGPARAM2 unix_gfx_close(TrapContext *ctx)
{
    trap_put_word(ctx, unix_uaegfx_base + 32, trap_get_word(ctx, unix_uaegfx_base + 32) - 1);
    return 0;
}

static uae_u32 REGPARAM2 unix_gfx_expunge(TrapContext *)
{
    return 0;
}

static uaecptr unix_uaegfx_card_install(TrapContext *ctx, uae_u32 extrasize)
{
    uaecptr openfunc, closefunc, expungefunc, findcardfunc, initcardfunc;
    uaecptr functable, datatable, exec, olda2;

    if (unix_uaegfx_old || !(gfxmem_bank.flags & ABFLAG_MAPPED)) {
        return 0;
    }

    exec = trap_get_long(ctx, 4);
    unix_uaegfx_resid = ds(_T("UAE Graphics Card 4.0"));
    unix_uaegfx_vblankname = ds(_T("UAE Graphics Card VBLANK"));
    unix_uaegfx_portsname = ds(_T("UAE Graphics Card PORTS"));

    openfunc = here();
    calltrap(deftrap(unix_gfx_open));
    dw(RTS);
    closefunc = here();
    calltrap(deftrap(unix_gfx_close));
    dw(RTS);
    expungefunc = here();
    calltrap(deftrap(unix_gfx_expunge));
    dw(RTS);
    findcardfunc = here();
    calltrap(deftrap(unix_picasso_find_card));
    dw(RTS);
    initcardfunc = here();
    calltrap(deftrap(unix_picasso_init_card));
    dw(RTS);

    functable = here();
    dl(openfunc);
    dl(closefunc);
    dl(expungefunc);
    dl(EXPANSION_nullfunc);
    dl(findcardfunc);
    dl(initcardfunc);
    dl(0xffffffff);

    datatable = makedatatable(unix_uaegfx_resid, unix_uaegfx_resname, 0x09, -50, UNIX_UAEGFX_VERSION, UNIX_UAEGFX_REVISION);
    olda2 = trap_get_areg(ctx, 2);

    trap_call_add_areg(ctx, 0, functable);
    trap_call_add_areg(ctx, 1, datatable);
    trap_call_add_areg(ctx, 2, 0);
    trap_call_add_dreg(ctx, 0, UNIX_CARD_SIZEOF + extrasize);
    trap_call_add_dreg(ctx, 1, 0);
    unix_uaegfx_base = trap_call_lib(ctx, exec, -0x54);
    trap_set_areg(ctx, 2, olda2);
    if (!unix_uaegfx_base) {
        return 0;
    }

    trap_call_add_areg(ctx, 1, unix_uaegfx_base);
    trap_call_lib(ctx, exec, -0x18c);

    trap_call_add_areg(ctx, 1, EXPANSION_explibname);
    trap_call_add_dreg(ctx, 0, 0);
    trap_put_long(ctx, unix_uaegfx_base + UNIX_CARD_EXPANSIONBASE, trap_call_lib(ctx, exec, -0x228));
    trap_put_long(ctx, unix_uaegfx_base + UNIX_CARD_EXECBASE, exec);
    trap_put_long(ctx, unix_uaegfx_base + UNIX_CARD_IRQEXECBASE, exec);
    trap_put_long(ctx, unix_uaegfx_base + UNIX_CARD_NAME, unix_uaegfx_resname);
    trap_put_long(ctx, unix_uaegfx_base + UNIX_CARD_RESLIST, unix_uaegfx_base + UNIX_CARD_SIZEOF);
    trap_put_long(ctx, unix_uaegfx_base + UNIX_CARD_RESLISTSIZE, extrasize);

    if (currprefs.rtg_hardwareinterrupt) {
        unix_init_vblank_irq(ctx, unix_uaegfx_base);
    }

    unix_uaegfx_active = 1;
    write_log(_T("Unix uaegfx.card %d.%d init @%08X (%u bytes modes)\n"),
        UNIX_UAEGFX_VERSION, UNIX_UAEGFX_REVISION, unix_uaegfx_base, extrasize);
    return unix_uaegfx_base;
}

static void unix_picasso_alloc2(TrapContext *ctx)
{
    int size = 0;

    unix_picasso_amem = 0;
    unix_picasso_amemend = 0;
    if (!gfxmem_bank.allocated_size) {
        return;
    }
    unix_rtg_rebuild_mode_sizes();
    if (!currprefs.picasso96_noautomodes) {
        size = unix_picasso_resolution_memory_size();
    }
    unix_uaegfx_card_install(ctx, size);
    unix_picasso_init_alloc(ctx, size);
}

void picasso96_alloc(TrapContext *ctx)
{
    if (currprefs.rtgboards[0].rtgmem_type >= GFXBOARD_HARDWARE) {
        return;
    }
    if (!currprefs.rtgboards[0].rtgmem_size) {
        return;
    }
    unix_uaegfx_resname = ds(_T("uaegfx.card"));
    unix_uaegfx_prefix = ds(_T("UAE"));
    if (unix_uaegfx_old) {
        return;
    }
    unix_picasso_alloc2(ctx);
}

uae_u32 picasso_demux(uae_u32, TrapContext *ctx)
{
    uae_u32 num = trap_get_long(ctx, trap_get_areg(ctx, 7) + 4);

    if (unix_uaegfx_base && num >= 16 && num <= 39) {
        write_log(_T("Unix RTG: obsolete Picasso96 uaelib hook ignored\n"));
        return 0;
    }
    if (!unix_uaegfx_old) {
        write_log(_T("Unix RTG: Picasso96 uaelib hook in use\n"));
        unix_uaegfx_old = 1;
        unix_uaegfx_active = 1;
    }

    switch (num) {
    case 16:
        return unix_picasso_find_card(ctx);
    case 18:
        return unix_picasso_set_switch(ctx);
    case 19:
        return unix_picasso_set_color_array(ctx);
    case 20:
        return unix_picasso_set_dac(ctx);
    case 21:
        return unix_picasso_set_gc(ctx);
    case 22:
        return unix_picasso_set_panning(ctx);
    case 23:
        return unix_picasso_calculate_bytes_per_row(ctx);
    case 26:
        return unix_picasso_set_display(ctx);
    case 29:
        return unix_picasso_init_card(ctx);
    case 35:
        return gfxmem_bank.allocated_size ? 1 : 0;
    default:
        return 0;
    }
}

void uaegfx_install_code(uaecptr start)
{
    unix_uaegfx_rom = start;
    org(start);
}

#ifndef GFXBOARD
bool gfxboard_set(int, bool)
{
    return false;
}

void gfxboard_refresh(int) {}
void gfxboard_reset_init(void) {}

int gfxboard_get_configtype(struct rtgboardconfig *rbc)
{
    const unix_rtg_board *board = rbc ? unix_rtg_find_board(rbc->rtgmem_type) : NULL;
    return board ? board->config_type : 0;
}

int gfxboard_get_vram_min(struct rtgboardconfig *)
{
    return -1;
}

int gfxboard_get_vram_max(struct rtgboardconfig *)
{
    return -1;
}

uae_u32 gfxboard_get_romtype(struct rtgboardconfig *)
{
    return 0;
}

const TCHAR *gfxboard_get_name(int id)
{
    const unix_rtg_board *board = unix_rtg_find_board(id);
    return board ? board->name : NULL;
}

const TCHAR *gfxboard_get_manufacturername(int id)
{
    const unix_rtg_board *board = unix_rtg_find_board(id);
    return board ? board->manufacturer : NULL;
}

const TCHAR *gfxboard_get_configname(int id)
{
    const unix_rtg_board *board = unix_rtg_find_board(id);
    return board ? board->config_name : NULL;
}

int gfxboard_get_index_from_id(int id)
{
    return unix_rtg_find_board(id) ? id : -1;
}

int gfxboard_get_id_from_index(int index)
{
    return unix_rtg_find_board(index) ? index : -1;
}

struct gfxboard_func *gfxboard_get_func(struct rtgboardconfig *)
{
    return NULL;
}

bool gfxboard_get_switcher(struct rtgboardconfig *rbc)
{
    return rbc && unix_rtg_find_board(rbc->rtgmem_type);
}

bool gfxboard_need_byteswap(struct rtgboardconfig *)
{
    return false;
}

int gfxboard_get_autoconfig_size(struct rtgboardconfig *)
{
    return -1;
}

int gfxboard_is_registers(struct rtgboardconfig *)
{
    return 0;
}

int gfxboard_num_boards(struct rtgboardconfig *)
{
    return 1;
}

int gfxboard_get_devnum(struct uae_prefs *, int index)
{
    return index;
}
#endif
