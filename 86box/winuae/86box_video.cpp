/*
* UAE - The Un*x Amiga Emulator
*
* Services the 86Box video cards expect from the 86Box application, for
* the RTG boards in gfxboard.cpp. The card sources are compiled with
* 86box_video_redirect.h, which sends their memory, I/O, timer and device
* calls here (the rtg_ functions) instead of to the bridgeboard's PC.
*/

#include "sysconfig.h"
#include "sysdeps.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "uae.h"
#include "threaddep/thread.h"
#include "uae/time.h"

#include "86box_glue.h"
#include "86box_video.h"
/* Declares the rtg_ functions with 86Box's prototypes */
#include "86box_video_redirect.h"

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/machine.h>
#include <86box/plat.h>
#include <86box/thread.h>
#include <86box/ui.h>
#include <86box/log.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/vid_ddc.h>

/* gfxboard.cpp's video_blit_memtoscreen(), not 86Box's */
#undef video_blit_memtoscreen

static void *gfxboard_priv;

/* Video state of the 86Box application */

monitor_t monitors[MONITORS_NUM];
int monitor_index_global;
uint32_t *video_6to8;
uint32_t *video_8togs;
uint32_t *video_8to32;
uint32_t *video_15to32;
uint32_t *video_16to32;
uint8_t edatlookup[4][4];
dbcs_font_t *fontdatksc5601;
dbcs_font_t *fontdatksc5601_user;
int enable_overscan;
int suppress_overscan;
int monitor_edid;
char monitor_edid_path[1024];

/* No IBM 8514/A or XGA on the RTG boards */
int ibm8514_active;
int xga_active;

void ibm8514_set_poll(svga_t *svga)
{
}

void ibm8514_recalctimings(svga_t *svga)
{
}

void xga_set_poll(svga_t *svga)
{
}

void xga_recalctimings(svga_t *svga)
{
}

void xga_write_test(uint32_t addr, uint8_t val, void *priv)
{
}

uint8_t xga_read_test(uint32_t addr, void *priv)
{
	return 0xff;
}

int rtg_machine_has_bus(int m, uintptr_t bus_flags)
{
	return 0;
}

static int calc_6to8(int c)
{
	int ic = c;
	if (ic == 64) {
		ic = 63;
	} else {
		ic &= 0x3f;
	}
	return (int)((ic / 63.0) * 255.0) & 0xff;
}

static int calc_8to32(int c)
{
	int b = (int)(((c & 3) / 3.0) * 255.0);
	int g = (int)((((c >> 2) & 7) / 7.0) * 255.0);
	int r = (int)((((c >> 5) & 7) / 7.0) * 255.0);
	return b | (g << 8) | (r << 16) | 0xff000000;
}

/* 15 and 16 bit pixels to 32 bit, swapped: red in the low bits */
static uint32_t calc_15to32(int c, bool swapped)
{
	int lo = (int)(((c & 31) / 31.0) * 255.0);
	int g = (int)((((c >> 5) & 31) / 31.0) * 255.0);
	int hi = (int)((((c >> 10) & 31) / 31.0) * 255.0);
	return swapped ? (hi | (g << 8) | (lo << 16)) : (lo | (g << 8) | (hi << 16));
}

static uint32_t calc_16to32(int c, bool swapped)
{
	int lo = (int)(((c & 31) / 31.0) * 255.0);
	int g = (int)((((c >> 5) & 63) / 63.0) * 255.0);
	int hi = (int)((((c >> 11) & 31) / 31.0) * 255.0);
	return swapped ? (hi | (g << 8) | (lo << 16)) : (lo | (g << 8) | (hi << 16));
}

/* The RAMDACs call this on each mode register write, rebuild only when the
   red/blue order changes */
static int video_rbswapped = -1;

void pcemvideorbswap(bool swapped)
{
	if (video_rbswapped == swapped) {
		return;
	}
	video_rbswapped = swapped;
	for (int c = 0; c < 65536; c++) {
		video_15to32[c] = calc_15to32(c & 0x7fff, swapped);
		video_16to32[c] = calc_16to32(c, swapped);
	}
}

#define TARGET_WIDTH 2112
#define TARGET_HEIGHT 2112

static bitmap_t *target_bitmap;
/* Horizontally doubled copy of the frame (svga_t horizontal_linedbl) */
static bitmap_t *double_bitmap;
static bitmap_t *blit_bitmap;
static int blit_width, blit_height, blit_hmult, blit_vmult;

static bitmap_t *alloc_bitmap(void)
{
	bitmap_t *b = xcalloc(bitmap_t, 1);
	b->w = TARGET_WIDTH;
	b->h = TARGET_HEIGHT;
	b->dat = xcalloc(uint32_t, b->w * b->h);
	for (int i = 0; i < b->h; i++) {
		b->line[i] = b->dat + b->w * i;
	}
	return b;
}

static void video_init_tables(bool swapped)
{
	for (int c = 0; c < 4; c++) {
		for (int d = 0; d < 4; d++) {
			edatlookup[c][d] = 0;
			if (c & 1) {
				edatlookup[c][d] |= 1;
			}
			if (d & 1) {
				edatlookup[c][d] |= 2;
			}
			if (c & 2) {
				edatlookup[c][d] |= 0x10;
			}
			if (d & 2) {
				edatlookup[c][d] |= 0x20;
			}
		}
	}
	if (!video_6to8) {
		video_6to8 = xcalloc(uint32_t, 256);
		video_8togs = xcalloc(uint32_t, 256);
		video_8to32 = xcalloc(uint32_t, 256);
		for (int c = 0; c < 256; c++) {
			video_6to8[c] = calc_6to8(c);
			video_8togs[c] = c | (c << 16) | (c << 24);
			video_8to32[c] = calc_8to32(c);
		}
		video_15to32 = xcalloc(uint32_t, 65536);
		video_16to32 = xcalloc(uint32_t, 65536);
	}
	pcemvideorbswap(swapped);
	if (!target_bitmap) {
		target_bitmap = alloc_bitmap();
		double_bitmap = alloc_bitmap();
	}
	blit_bitmap = target_bitmap;
	blit_width = blit_height = blit_hmult = blit_vmult = 0;

	monitor_t *m = &monitors[0];
	memset(m, 0, sizeof(monitor_t));
	m->mon_xsize = 640;
	m->mon_ysize = 480;
	m->mon_bpp = 8;
	m->mon_changeframecount = 2;
	m->target_buffer = target_bitmap;
	monitor_index_global = 0;
}

/* gfxboard.cpp shows the frame */

uint8_t *getpcembuffer32(int x, int y, int yy)
{
	return (uint8_t *)(blit_bitmap->line[y + yy] + x);
}

/* svga_doblit() for WinUAE: the frame at x, y of the target buffer is
   w x h pixels, doubled horizontally here for svga_t horizontal_linedbl.
   Lines y1 to y2 - 1 of it changed since the last frame. */
void svga_doblit_uae(svga_t *svga, int x, int y, int w, int h, int y1, int y2)
{
	int hmult = svga->horizontal_linedbl ? 2 : 1;
	int vmult = svga->linedbl ? 2 : 1;

	if (w < 128 || h < 32) {
		w = h = 0;
	}
	if (w != blit_width || h != blit_height || hmult != blit_hmult || vmult != blit_vmult) {
		blit_width = w;
		blit_height = h;
		blit_hmult = hmult;
		blit_vmult = vmult;
		gfxboard_resize(w * hmult, h, hmult, vmult, gfxboard_priv);
		y1 = 0;
		y2 = h;
	}
	if (y2 > h) {
		y2 = h;
	}
	svga->monitor->mon_xsize = w;
	svga->monitor->mon_ysize = h;
	blit_bitmap = target_bitmap;
	if (hmult > 1 && w > 0) {
		if (x + w * 2 > TARGET_WIDTH) {
			w = (TARGET_WIDTH - x) / 2;
		}
		for (int yy = y1; yy < y2 && y + yy < TARGET_HEIGHT; yy++) {
			uint32_t *s = target_bitmap->line[y + yy] + x;
			uint32_t *d = double_bitmap->line[y + yy] + x;
			for (int xx = 0; xx < w; xx++) {
				d[xx * 2 + 0] = s[xx];
				d[xx * 2 + 1] = s[xx];
			}
		}
		blit_bitmap = double_bitmap;
	}
	video_blit_memtoscreen(x, y, y1, y2, w * hmult, h);
}

void video_blit_memtoscreen_monitor(int x, int y, int w, int h, int monitor_index)
{
	blit_bitmap = target_bitmap;
	video_blit_memtoscreen(x, y, 0, h, w, h);
}

void video_wait_for_buffer_monitor(int monitor_index)
{
}

void video_inform_monitor(int type, const video_timings_t *ptr, int monitor_index)
{
	/* The card's access timings would be taken from the x86 CPU's cycles */
}

uint8_t video_force_resize_get_monitor(int monitor_index)
{
	return 0;
}

void video_force_resize_set_monitor(uint8_t res, int monitor_index)
{
}

void set_screen_size_monitor(int x, int y, int monitor_index)
{
}

void video_lightpen_hsync(void)
{
}

void video_lightpen_vsync(void)
{
}

void video_lightpen_check_trigger_strobe(int x_offset, int y, int x_offset_from_hsync, int firstline, double pix_clock, int monitor_used)
{
}

void video_clamp_vram(uint64_t bios_flags, int *vram)
{
}

void video_load_font(char *fn, int format, int offset)
{
}

size_t ddc_load_edid(char *path, uint8_t *buf, size_t size)
{
	return 0;
}

/* The card: svga_t of the board */

static svga_t *board_svga;

void *svga_get_object(void)
{
	return board_svga;
}

int svga_poll_eod(void *p)
{
	svga_t *svga = (svga_t *)p;
	svga->vsync_eod = 0;
	svga_poll(svga);
	return svga->vsync_eod;
}

int svga_get_vtotal(void *p)
{
	svga_t *svga = (svga_t *)p;
	int v = svga->vtotal;
	if (svga->crtc[0x17] & 4) {
		v *= 2;
	}
	return v;
}

bool svga_on(void *p)
{
	svga_t *svga = (svga_t *)p;
	return svga->scrblank == 0 && svga->hdisp >= 80 && (svga->crtc[0x17] & 0x80) && !svga->dpms;
}

void svga_setvram(void *p, uint8_t *vram)
{
	svga_t *svga = (svga_t *)p;
	svga->vram = vram;
}

uint8_t pcem_read_io(int port, int index)
{
	if (port == 0x3d5 && board_svga) {
		return board_svga->crtc[index];
	}
	write_log(_T("pcem_read_io: unknown port %04x\n"), port);
	return 0;
}

/* Memory mappings, in 64k blocks of the board's PCI address space */

#define MAX_MAPPINGS 16
#define MAPBLOCKSIZE 0x10000
#define MAX_MAPBLOCKS (0x80000000 / MAPBLOCKSIZE)

static mem_mapping_t *mappings[MAX_MAPPINGS];
/* When each mapping was last enabled or moved */
static uint32_t mapping_stamps[MAX_MAPPINGS];
static uint32_t mapping_stamp;
static mem_mapping_t **mapblocks;

static mem_mapping_t *linear_mapping;
void *pcem_mapping_linear_priv;
uint32_t pcem_mapping_linear_offset;
uint8_t (*pcem_linear_read_b)(uint32_t addr, void *priv);
uint16_t (*pcem_linear_read_w)(uint32_t addr, void *priv);
uint32_t (*pcem_linear_read_l)(uint32_t addr, void *priv);
void (*pcem_linear_write_b)(uint32_t addr, uint8_t val, void *priv);
void (*pcem_linear_write_w)(uint32_t addr, uint16_t val, void *priv);
void (*pcem_linear_write_l)(uint32_t addr, uint32_t val, void *priv);

static uint8_t dummy_read_b(uint32_t addr, void *priv)
{
	return 0xff;
}

static uint16_t dummy_read_w(uint32_t addr, void *priv)
{
	return 0xffff;
}

static uint32_t dummy_read_l(uint32_t addr, void *priv)
{
	return 0xffffffff;
}

static void dummy_write_b(uint32_t addr, uint8_t val, void *priv)
{
}

static void dummy_write_w(uint32_t addr, uint16_t val, void *priv)
{
}

static void dummy_write_l(uint32_t addr, uint32_t val, void *priv)
{
}

/* Recalculate the blocks of mapping m. Where mappings overlap the one
   enabled or moved last wins, so a card's MMIO set up after its linear
   window shows through it. */
static void mapping_recalc(mem_mapping_t *m)
{
	for (int i = 0; i < MAX_MAPPINGS; i++) {
		if (mappings[i] == m && m->enable) {
			mapping_stamps[i] = ++mapping_stamp;
		}
	}
	if (m == linear_mapping || (!linear_mapping && m->size >= 1024 * 1024)) {
		bool on = m->enable && m->size;
		linear_mapping = m;
		pcem_mapping_linear_priv = m->priv;
		pcem_mapping_linear_offset = m->base;
		pcem_linear_read_b = on && m->read_b ? m->read_b : dummy_read_b;
		pcem_linear_read_w = on && m->read_w ? m->read_w : dummy_read_w;
		pcem_linear_read_l = on && m->read_l ? m->read_l : dummy_read_l;
		pcem_linear_write_b = on && m->write_b ? m->write_b : dummy_write_b;
		pcem_linear_write_w = on && m->write_w ? m->write_w : dummy_write_w;
		pcem_linear_write_l = on && m->write_l ? m->write_l : dummy_write_l;
	}
	if (!mapblocks || !m->size) {
		return;
	}
	uint32_t start = (m->base & 0x7fffffff) / MAPBLOCKSIZE;
	uint32_t end = ((m->base & 0x7fffffff) + m->size - 1) / MAPBLOCKSIZE;
	for (uint32_t b = start; b <= end && b < MAX_MAPBLOCKS; b++) {
		mapblocks[b] = NULL;
		uint32_t addr = b * MAPBLOCKSIZE;
		uint32_t stamp = 0;
		for (int i = 0; i < MAX_MAPPINGS; i++) {
			mem_mapping_t *mm = mappings[i];
			if (mm && mm->enable && mm->size && addr + MAPBLOCKSIZE > (mm->base & 0x7fffffff) && addr < (mm->base & 0x7fffffff) + mm->size &&
				mapping_stamps[i] >= stamp) {
				mapblocks[b] = mm;
				stamp = mapping_stamps[i];
			}
		}
	}
}

void rtg_mem_mapping_add(mem_mapping_t *m, uint32_t base, uint32_t size,
	uint8_t (*read_b)(uint32_t addr, void *priv),
	uint16_t (*read_w)(uint32_t addr, void *priv),
	uint32_t (*read_l)(uint32_t addr, void *priv),
	void (*write_b)(uint32_t addr, uint8_t val, void *priv),
	void (*write_w)(uint32_t addr, uint16_t val, void *priv),
	void (*write_l)(uint32_t addr, uint32_t val, void *priv),
	uint8_t *exec, uint32_t flags, void *priv)
{
	int slot = -1;
	for (int i = 0; i < MAX_MAPPINGS; i++) {
		if (mappings[i] == m) {
			slot = i;
			break;
		}
		if (!mappings[i] && slot < 0) {
			slot = i;
		}
	}
	if (slot < 0) {
		write_log(_T("86Box video: too many memory mappings\n"));
		return;
	}
	mappings[slot] = m;
	m->enable = size != 0;
	m->base = base;
	m->size = size;
	m->mask = size ? 0xffffffff : 0;
	m->read_b = read_b;
	m->read_w = read_w;
	m->read_l = read_l;
	m->write_b = write_b;
	m->write_w = write_w;
	m->write_l = write_l;
	m->exec = exec;
	m->flags = flags;
	m->priv = priv;
	mapping_recalc(m);
}

void rtg_mem_mapping_set_handler(mem_mapping_t *m,
	uint8_t (*read_b)(uint32_t addr, void *priv),
	uint16_t (*read_w)(uint32_t addr, void *priv),
	uint32_t (*read_l)(uint32_t addr, void *priv),
	void (*write_b)(uint32_t addr, uint8_t val, void *priv),
	void (*write_w)(uint32_t addr, uint16_t val, void *priv),
	void (*write_l)(uint32_t addr, uint32_t val, void *priv))
{
	m->read_b = read_b;
	m->read_w = read_w;
	m->read_l = read_l;
	m->write_b = write_b;
	m->write_w = write_w;
	m->write_l = write_l;
	mapping_recalc(m);
}

void rtg_mem_mapping_set_p(mem_mapping_t *m, void *priv)
{
	m->priv = priv;
	mapping_recalc(m);
}

void rtg_mem_mapping_set_addr(mem_mapping_t *m, uint32_t base, uint32_t size)
{
	m->enable = 0;
	mapping_recalc(m);
	m->enable = 1;
	m->base = base;
	m->size = size;
	mapping_recalc(m);
}

void rtg_mem_mapping_set_base_ignore(mem_mapping_t *m, uint32_t base_ignore)
{
	m->base_ignore = base_ignore;
}

void rtg_mem_mapping_disable(mem_mapping_t *m)
{
	m->enable = 0;
	mapping_recalc(m);
}

void rtg_mem_mapping_enable(mem_mapping_t *m)
{
	m->enable = 1;
	mapping_recalc(m);
}

static mem_mapping_t *get_mapping(uint32_t addr)
{
	addr &= 0x7fffffff;
	mem_mapping_t *m = mapblocks ? mapblocks[addr / MAPBLOCKSIZE] : NULL;
	if (m && (addr < (m->base & 0x7fffffff) || addr >= (m->base & 0x7fffffff) + m->size)) {
		m = NULL;
	}
	return m;
}

void put_mem_pcem(uint32_t addr, uint32_t v, int size)
{
	mem_mapping_t *m = get_mapping(addr);
	if (!m) {
		return;
	}
	if (size == 0) {
		if (m->write_b) {
			m->write_b(addr, v, m->priv);
		}
	} else if (size == 1) {
		if (m->write_w) {
			m->write_w(addr, v, m->priv);
		} else {
			put_mem_pcem(addr + 0, v >> 0, 0);
			put_mem_pcem(addr + 1, v >> 8, 0);
		}
	} else if (size == 2) {
		if (m->write_l) {
			m->write_l(addr, v, m->priv);
		} else {
			put_mem_pcem(addr + 0, v >> 0, 1);
			put_mem_pcem(addr + 2, v >> 16, 1);
		}
	}
}

uint32_t get_mem_pcem(uint32_t addr, int size)
{
	uint32_t v = 0;
	mem_mapping_t *m = get_mapping(addr);
	if (!m) {
		return 0;
	}
	if (size == 0) {
		if (m->read_b) {
			v = m->read_b(addr, m->priv);
		}
	} else if (size == 1) {
		if (m->read_w) {
			v = m->read_w(addr, m->priv);
		} else {
			v = get_mem_pcem(addr + 0, 0) << 0;
			v |= get_mem_pcem(addr + 1, 0) << 8;
		}
	} else if (size == 2) {
		if (m->read_l) {
			v = m->read_l(addr, m->priv);
		} else {
			v = get_mem_pcem(addr + 0, 1) << 0;
			v |= get_mem_pcem(addr + 2, 1) << 16;
		}
	}
	return v;
}

void pcem_linear_mark(int offset)
{
	if (!linear_mapping) {
		return;
	}
	uint16_t w = pcem_linear_read_w(offset, pcem_mapping_linear_priv);
	pcem_linear_write_w(offset, w, pcem_mapping_linear_priv);
}

/* No bus mastering from the cards */

uint32_t rtg_mem_readl_phys(uint32_t addr)
{
	return 0xffffffff;
}

void rtg_dma_bm_read(uint32_t PhysAddress, uint8_t *DataRead, uint32_t TotalSize, int TransferSize)
{
	memset(DataRead, 0xff, TotalSize);
}

void rtg_dma_bm_write(uint32_t PhysAddress, const uint8_t *DataWrite, uint32_t TotalSize, int TransferSize)
{
}

/* I/O ports. Several handlers can share a port, as in 86Box. */

#define MAX_IO_PORT 0x10000
#define MAX_PORT_HANDLERS 4

struct port_handler
{
	uint8_t (*inb)(uint16_t addr, void *priv);
	uint16_t (*inw)(uint16_t addr, void *priv);
	uint32_t (*inl)(uint16_t addr, void *priv);
	void (*outb)(uint16_t addr, uint8_t val, void *priv);
	void (*outw)(uint16_t addr, uint16_t val, void *priv);
	void (*outl)(uint16_t addr, uint32_t val, void *priv);
	void *priv;
};

static struct port_handler (*ports)[MAX_PORT_HANDLERS];

void rtg_io_sethandler(uint16_t base, uint16_t size,
	uint8_t (*inb)(uint16_t addr, void *priv),
	uint16_t (*inw)(uint16_t addr, void *priv),
	uint32_t (*inl)(uint16_t addr, void *priv),
	void (*outb)(uint16_t addr, uint8_t val, void *priv),
	void (*outw)(uint16_t addr, uint16_t val, void *priv),
	void (*outl)(uint16_t addr, uint32_t val, void *priv),
	void *priv)
{
	if (!ports) {
		ports = (struct port_handler(*)[MAX_PORT_HANDLERS])xcalloc(struct port_handler, MAX_IO_PORT * MAX_PORT_HANDLERS);
	}
	for (int i = 0; i < size && base + i < MAX_IO_PORT; i++) {
		struct port_handler *ph = ports[base + i];
		int j;
		for (j = 0; j < MAX_PORT_HANDLERS; j++) {
			if (!ph[j].inb && !ph[j].inw && !ph[j].inl && !ph[j].outb && !ph[j].outw && !ph[j].outl) {
				break;
			}
		}
		if (j == MAX_PORT_HANDLERS) {
			write_log(_T("86Box video: too many handlers for port %04x\n"), base + i);
			continue;
		}
		ph[j].inb = inb;
		ph[j].inw = inw;
		ph[j].inl = inl;
		ph[j].outb = outb;
		ph[j].outw = outw;
		ph[j].outl = outl;
		ph[j].priv = priv;
	}
}

void rtg_io_removehandler(uint16_t base, uint16_t size,
	uint8_t (*inb)(uint16_t addr, void *priv),
	uint16_t (*inw)(uint16_t addr, void *priv),
	uint32_t (*inl)(uint16_t addr, void *priv),
	void (*outb)(uint16_t addr, uint8_t val, void *priv),
	void (*outw)(uint16_t addr, uint16_t val, void *priv),
	void (*outl)(uint16_t addr, uint32_t val, void *priv),
	void *priv)
{
	if (!ports) {
		return;
	}
	for (int i = 0; i < size && base + i < MAX_IO_PORT; i++) {
		struct port_handler *ph = ports[base + i];
		for (int j = 0; j < MAX_PORT_HANDLERS; j++) {
			if (ph[j].priv == priv && ph[j].inb == inb && ph[j].inw == inw && ph[j].inl == inl &&
				ph[j].outb == outb && ph[j].outw == outw && ph[j].outl == outl) {
				memset(&ph[j], 0, sizeof(struct port_handler));
				break;
			}
		}
	}
}

void put_io_pcem(uint32_t addr, uint32_t v, int size)
{
	addr &= MAX_IO_PORT - 1;
	if (!ports) {
		return;
	}
	struct port_handler *ph = ports[addr];
	bool found = false;
	for (int j = 0; j < MAX_PORT_HANDLERS; j++) {
		if (size == 0 && ph[j].outb) {
			ph[j].outb(addr, v, ph[j].priv);
			found = true;
		} else if (size == 1 && ph[j].outw) {
			ph[j].outw(addr, v, ph[j].priv);
			found = true;
		} else if (size == 2 && ph[j].outl) {
			ph[j].outl(addr, v, ph[j].priv);
			found = true;
		}
	}
	if (!found && size == 1) {
		put_io_pcem(addr + 0, v >> 0, 0);
		put_io_pcem(addr + 1, v >> 8, 0);
	} else if (!found && size == 2) {
		put_io_pcem(addr + 0, v >> 0, 1);
		put_io_pcem(addr + 2, v >> 16, 1);
	}
}

uint32_t get_io_pcem(uint32_t addr, int size)
{
	addr &= MAX_IO_PORT - 1;
	uint32_t v = size == 0 ? 0xff : (size == 1 ? 0xffff : 0xffffffff);
	if (!ports) {
		return v;
	}
	struct port_handler *ph = ports[addr];
	bool found = false;
	for (int j = 0; j < MAX_PORT_HANDLERS; j++) {
		if (size == 0 && ph[j].inb) {
			v &= ph[j].inb(addr, ph[j].priv);
			found = true;
		} else if (size == 1 && ph[j].inw) {
			v &= ph[j].inw(addr, ph[j].priv);
			found = true;
		} else if (size == 2 && ph[j].inl) {
			v &= ph[j].inl(addr, ph[j].priv);
			found = true;
		}
	}
	if (!found && size == 1) {
		v = get_io_pcem(addr + 0, 0) << 0;
		v |= get_io_pcem(addr + 1, 0) << 8;
	} else if (!found && size == 2) {
		v = get_io_pcem(addr + 0, 1) << 0;
		v |= get_io_pcem(addr + 2, 1) << 16;
	}
	return v;
}

/* Timers: every enabled timer fires once per Amiga horizontal sync */

static pc_timer_t *timer_head;

void rtg_timer_disable(pc_timer_t *timer)
{
	if (!(timer->flags & TIMER_ENABLED)) {
		return;
	}
	timer->flags &= ~TIMER_ENABLED;
	if (timer->prev) {
		timer->prev->next = timer->next;
	} else {
		timer_head = timer->next;
	}
	if (timer->next) {
		timer->next->prev = timer->prev;
	}
	timer->prev = timer->next = NULL;
}

void rtg_timer_enable(pc_timer_t *timer)
{
	if (timer->flags & TIMER_ENABLED) {
		return;
	}
	timer->flags |= TIMER_ENABLED;
	timer->prev = NULL;
	timer->next = timer_head;
	if (timer_head) {
		timer_head->prev = timer;
	}
	timer_head = timer;
}

void rtg_timer_add(pc_timer_t *timer, void (*callback)(void *priv), void *priv, int start_timer)
{
	memset(timer, 0, sizeof(pc_timer_t));
	timer->callback = callback;
	timer->priv = priv;
	if (start_timer) {
		rtg_timer_enable(timer);
	}
}

void rtg_timer_stop(pc_timer_t *timer)
{
	rtg_timer_disable(timer);
}

void rtg_timer_on_auto(pc_timer_t *timer, double period)
{
	if (period > 0) {
		rtg_timer_enable(timer);
	} else {
		rtg_timer_disable(timer);
	}
}

void pcemglue_hsync(void)
{
	/* Timers re-enable themselves from their callbacks, take the current
	   ones off the list first. */
	pc_timer_t *due[64];
	int n = 0;
	while (timer_head && n < 64) {
		pc_timer_t *timer = timer_head;
		rtg_timer_disable(timer);
		due[n++] = timer;
	}
	for (int i = 0; i < n; i++) {
		if (due[i]->callback) {
			due[i]->callback(due[i]->priv);
		}
	}
}

/* Devices of the card (RAMDACs, clock generators, I2C), separate from
   the bridgeboard's device list */

#define MAX_DEVICES 8

struct rtg_device
{
	const device_t *dev;
	void *priv;
	int inst;
};

static struct rtg_device devices[MAX_DEVICES];
static const device_t *device_current;
static int device_current_inst;

static void *device_add_common(const device_t *dev, int inst)
{
	for (int i = 0; i < MAX_DEVICES; i++) {
		struct rtg_device *rd = &devices[i];
		if (rd->dev) {
			continue;
		}
		const device_t *prev = device_current;
		int prev_inst = device_current_inst;
		rd->dev = dev;
		rd->inst = inst;
		device_current = dev;
		device_current_inst = inst;
		rd->priv = dev->init ? dev->init(dev) : NULL;
		device_current = prev;
		device_current_inst = prev_inst;
		return rd->priv;
	}
	write_log(_T("86Box video: too many devices, can't add '%s'\n"), dev->name);
	return NULL;
}

void *rtg_device_add(const device_t *dev)
{
	return device_add_common(dev, 0);
}

void *rtg_device_add_inst(const device_t *dev, int inst)
{
	return device_add_common(dev, inst);
}

void *rtg_device_add_params(const device_t *dev, void *params)
{
	return device_add_common(dev, 0);
}

void *rtg_device_add_inst_params(const device_t *dev, int inst, void *params)
{
	return device_add_common(dev, inst);
}

int rtg_device_get_instance(void)
{
	return device_current_inst;
}

static void close_devices(void)
{
	for (int i = MAX_DEVICES - 1; i >= 0; i--) {
		struct rtg_device *rd = &devices[i];
		if (rd->dev && rd->dev->close && rd->priv) {
			rd->dev->close(rd->priv);
		}
		memset(rd, 0, sizeof(struct rtg_device));
	}
}

/* Card options WinUAE sets, the rest keep the device's default */
int rtg_device_get_config_int(const char *s)
{
	if (!strcmp(s, "bilinear") || !strcmp(s, "dithering") || !strcmp(s, "dithersub") || !strcmp(s, "dacfilter")) {
		return 1;
	}
	if (!strcmp(s, "recompiler")) {
		/* The Voodoo has code generators for these hosts
		   (vid_voodoo_render.h) */
#if defined(__amd64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64)
		return 1;
#else
		return 0;
#endif
	}
	if (!strcmp(s, "sli") || !strcmp(s, "type")) {
		return 0;
	}
	if (!strcmp(s, "framebuffer_memory") || !strcmp(s, "texture_memory")) {
		return 2;
	}
	if (!strcmp(s, "memory")) {
		int vram = pcem_getvramsize() >> 20;
		return vram > 0 ? vram : 2;
	}
	if (!strcmp(s, "render_threads")) {
#ifdef _WIN32
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		if (si.dwNumberOfProcessors >= 8) {
			return 4;
		}
		if (si.dwNumberOfProcessors >= 4) {
			return 2;
		}
		return 1;
#else
		long cpus = sysconf(_SC_NPROCESSORS_ONLN);
		if (cpus >= 8) {
			return 4;
		}
		if (cpus >= 4) {
			return 2;
		}
		return 1;
#endif
	}
	if (device_current && device_current->config) {
		for (const device_config_t *c = device_current->config; c->type != CONFIG_END && c->name; c++) {
			if (!strcmp(c->name, s)) {
				return c->default_int;
			}
		}
	}
	return 0;
}

/* gfxboard.cpp supplies the ROMs */

const char *rtg_device_get_config_bios(const char *name)
{
	return NULL;
}

const char *rtg_device_get_bios_file(const device_t *dev, const char *internal_name, unsigned int file_no)
{
	return NULL;
}

uint64_t rtg_device_get_bios_flags(const device_t *dev, const char *internal_name)
{
	return 0;
}

uint32_t rtg_device_get_bios_local(const device_t *dev, const char *internal_name)
{
	return 0;
}

int rom_init(rom_t *rom, const char *fn, uint32_t address, int size, int mask, int file_offset, uint32_t flags)
{
	return -1;
}

int rom_init_interleaved(rom_t *rom, const char *fn_low, const char *fn_high, uint32_t address, int size, int mask, int file_offset, uint32_t flags)
{
	return -1;
}

uint8_t rom_read(uint32_t addr, void *priv)
{
	return 0xff;
}

uint16_t rom_readw(uint32_t addr, void *priv)
{
	return 0xffff;
}

uint32_t rom_readl(uint32_t addr, void *priv)
{
	return 0xffffffff;
}

/* PCI: the board is the only card on the bus */

static uint8_t (*pci_card_read)(int func, int addr, int len, void *priv);
static void (*pci_card_write)(int func, int addr, int len, uint8_t val, void *priv);
static void *pci_card_priv;

void rtg_pci_add_card(uint8_t add_type, uint8_t (*read)(int func, int addr, int len, void *priv),
	void (*write)(int func, int addr, int len, uint8_t val, void *priv), void *priv, uint8_t *slot)
{
	pci_card_read = read;
	pci_card_write = write;
	pci_card_priv = priv;
	if (slot) {
		*slot = 0;
	}
}

void rtg_pci_irq(uint8_t slot, uint8_t pci_int, int level, int set, uint8_t *irq_state)
{
	gfxboard_intreq(gfxboard_priv, set ? 1 : 0, true);
}

void put_pci_pcem(uint32_t addr, uint8_t v)
{
	if (pci_card_write) {
		pci_card_write(0, addr, 1, v, pci_card_priv);
	}
}

uint8_t get_pci_pcem(uint32_t addr)
{
	uint8_t v = 0;
	if (pci_card_read) {
		v = pci_card_read(0, addr, 1, pci_card_priv);
	}
	return v;
}

/* Starting and stopping the card */

void *pcem_device_init(const device_t *dev, void *gfxboard, bool swapped, void **svga)
{
	if (!mapblocks) {
		mapblocks = xcalloc(mem_mapping_t *, MAX_MAPBLOCKS);
	}
	memset(mapblocks, 0, sizeof(mem_mapping_t *) * MAX_MAPBLOCKS);
	memset(mappings, 0, sizeof(mappings));
	memset(mapping_stamps, 0, sizeof(mapping_stamps));
	mapping_stamp = 0;
	if (ports) {
		memset(ports, 0, sizeof(struct port_handler) * MAX_IO_PORT * MAX_PORT_HANDLERS);
	}
	linear_mapping = NULL;
	pcem_mapping_linear_priv = NULL;
	pcem_mapping_linear_offset = 0;
	pcem_linear_read_b = dummy_read_b;
	pcem_linear_read_w = dummy_read_w;
	pcem_linear_read_l = dummy_read_l;
	pcem_linear_write_b = dummy_write_b;
	pcem_linear_write_w = dummy_write_w;
	pcem_linear_write_l = dummy_write_l;
	pci_card_read = NULL;
	pci_card_write = NULL;
	pci_card_priv = NULL;
	timer_head = NULL;
	gfxboard_priv = gfxboard;
	video_init_tables(swapped);

	void *priv = device_add_common(dev, 0);
	board_svga = svga_get_pri();
	*svga = board_svga;
	return priv;
}

void pcem_device_close(const device_t *dev, void *priv)
{
	/* The card is the first device, its RAMDAC and friends follow it */
	close_devices();
	board_svga = NULL;
	timer_head = NULL;
}

void pcem_device_force_redraw(const device_t *dev, void *priv)
{
	if (dev->force_redraw) {
		dev->force_redraw(priv);
	}
}

/* Platform services for the card threads (Voodoo, S3 and Matrox FIFOs) */

uint64_t plat_timer_read(void)
{
	return read_processor_time();
}

void plat_delay_ms(uint32_t count)
{
	sleep_millis(count);
}

char *plat_get_string(int id)
{
	static char empty[1];
	return empty;
}

int ui_msgbox_header(int flags, char *header, char *message)
{
	write_log("86Box video: %s %s\n", header ? header : "", message ? message : "");
	return 0;
}

void *log_open(const char *dev_name)
{
	return NULL;
}

void log_close(void *priv)
{
}

struct rtg_thread
{
	uae_thread_id tid;
};

thread_t *thread_create_named(void (*thread_func)(void *param), void *param, const char *name)
{
	struct rtg_thread *t = xcalloc(struct rtg_thread, 1);
	if (!uae_start_thread(_T("86Box video"), thread_func, param, &t->tid)) {
		xfree(t);
		return NULL;
	}
	return (thread_t *)t;
}

int thread_wait(thread_t *arg)
{
	struct rtg_thread *t = (struct rtg_thread *)arg;
	if (!t) {
		return 0;
	}
	uae_wait_thread(t->tid);
	xfree(t);
	return 1;
}

event_t *thread_create_event(void)
{
	uae_sem_t sem = NULL;
	uae_sem_init(&sem, 1, 0);
	return (event_t *)sem;
}

void thread_set_event(event_t *event)
{
	uae_sem_t sem = (uae_sem_t)event;
	uae_sem_post(&sem);
}

void thread_reset_event(event_t *event)
{
	uae_sem_t sem = (uae_sem_t)event;
	uae_sem_unpost(&sem);
}

int thread_wait_event(event_t *event, int timeout)
{
	uae_sem_t sem = (uae_sem_t)event;
	return uae_sem_trywait_delay(&sem, timeout < 0 ? -1 : timeout);
}

void thread_destroy_event(event_t *event)
{
	uae_sem_t sem = (uae_sem_t)event;
	uae_sem_destroy(&sem);
}

mutex_t *thread_create_mutex(void)
{
	uae_sem_t sem = NULL;
	uae_sem_init(&sem, 0, 1);
	return (mutex_t *)sem;
}

void thread_close_mutex(mutex_t *mutex)
{
	uae_sem_t sem = (uae_sem_t)mutex;
	if (sem) {
		uae_sem_destroy(&sem);
	}
}

int thread_wait_mutex(mutex_t *mutex)
{
	uae_sem_t sem = (uae_sem_t)mutex;
	if (!sem) {
		return 0;
	}
	uae_sem_wait(&sem);
	return 1;
}

int thread_release_mutex(mutex_t *mutex)
{
	uae_sem_t sem = (uae_sem_t)mutex;
	if (!sem) {
		return 0;
	}
	uae_sem_post(&sem);
	return 1;
}
