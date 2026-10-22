#include "sysconfig.h"
#include "sysdeps.h"

#include "uae.h"
#include "ibm.h"
#include "cpu.h"
#include "device.h"
#include "mem.h"
#include "x86.h"
#include "timer.h"
#include "sound.h"
#include "rom.h"
#include "thread.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include "pcemglue.h"

#include "threaddep/thread.h"
#include "machdep/maccess.h"
#include "gfxboard.h"
#include "uae/time.h"

#include "video.h"
#include "vid_svga.h"
#include "86box/winuae/86box_glue.h"

// PCem CPU and PIT state the video code reads; the x86 emulation is 86Box's
CPU_STATE cpu_state;
uint64_t tsc;
float cpuclock;
uint64_t VGACONST1, VGACONST2;
int has_vlb;
int MCA;
struct svga_t *mb_vga;
int pci_burst_time, pci_nonburst_time;


void pclog(char const *format, ...)
{
	va_list parms;
	va_start(parms, format);
	char buf[256];
	vsprintf(buf, format, parms);
	write_log("%s", buf);
	va_end(parms);
}
#ifdef DEBUGGER
extern void activate_debugger(void);
#endif
void fatal(char const *format, ...)
{
	va_list parms;
	va_start(parms, format);
	char buf[256];
	vsprintf(buf, format, parms);
	write_log("PCEMFATAL: %s", buf);
	va_end(parms);
#ifdef DEBUGGER
	activate_debugger();
#endif
}

void video_updatetiming(void)
{
	write_log(_T("video_updatetiming\n"));
}

void device_speed_changed(void)
{
	write_log(_T("device_speed_changed\n"));
}



int video_is_mda()
{
	return 0;
}

// VIDEO STUFF

int video_timing_read_b;
int video_timing_read_w;
int video_timing_read_l;

int video_timing_write_b;
int video_timing_write_w;
int video_timing_write_l;

int cycles_lost;

uint8_t fontdat[2048][8];
uint8_t fontdatm[2048][16];
uint8_t fontdatw[512][32];	/* Wyse700 font */
uint8_t fontdat8x12[256][16];	/* MDSI Genius font */
uint8_t fontdat12x18[256][36];	/* IM1024 font */
uint8_t fontdatksc5601[16384][32]; /* Korean KSC-5601 font */
uint8_t fontdatksc5601_user[192][32]; /* Korean KSC-5601 user defined font */
uint32_t *video_6to8;

int PCI;
int readflash;
int egareads;
int egawrites;
int changeframecount;
PCBITMAP *buffer32;
static int buffer32size;
static uae_u8 *tempbuf;
int vid_resize;
int xsize, ysize;
uint64_t timer_freq;

uint8_t edatlookup[4][4];
uint8_t rotatevga[8][256];
uint32_t *video_15to32, *video_16to32;
monitor_t monitors[MONITORS_NUM];
bitmap_t target_bitmap;

static void *gfxboard_priv;

static mem_mapping_t *pcem_mapping_linear;
void *pcem_mapping_linear_priv;
uae_u32 pcem_mapping_linear_offset;
uint8_t(*pcem_linear_read_b)(uint32_t addr, void *priv);
uint16_t(*pcem_linear_read_w)(uint32_t addr, void *priv);
uint32_t(*pcem_linear_read_l)(uint32_t addr, void *priv);
void (*pcem_linear_write_b)(uint32_t addr, uint8_t  val, void *priv);
void (*pcem_linear_write_w)(uint32_t addr, uint16_t val, void *priv);
void (*pcem_linear_write_l)(uint32_t addr, uint32_t val, void *priv);

#define MAX_PCEMMAPPINGS 10
static mem_mapping_t *pcemmappings[MAX_PCEMMAPPINGS];
#define PCEMMAPBLOCKSIZE 0x10000
#define MAX_PCEMMAPBLOCKS (0x80000000 / PCEMMAPBLOCKSIZE)
mem_mapping_t **pcemmap;

static uint8_t dummy_bread(uint32_t addr, void *p)
{
	return 0;
}
static uint16_t dummy_wread(uint32_t addr, void *p)
{
	return 0;
}
static uint32_t dummy_lread(uint32_t addr, void *p)
{
	return 0;
}
static void dummy_bwrite(uint32_t addr, uint8_t v, void *p)
{

}
static void dummy_wwrite(uint32_t addr, uint16_t v, void *p)
{

}
static void dummy_lwrite(uint32_t addr, uint32_t v, void *p)
{

}

uint64_t timer_read(void)
{
	return read_processor_time();
}

static pc_timer_t *timer_head = NULL;

void timer_enablex(pc_timer_t *timer)
{
	pc_timer_t *timer_node = timer_head;
	if (timer->enabled)
		timer_disablex(timer);
	timer->enabled = 1;
	if (!timer_head)
	{
		timer_head = timer;
		timer->next = timer->prev = NULL;
		return;
	}
	timer_node = timer_head;
}
void timer_disablex(pc_timer_t *timer)
{
	if (!timer->enabled)
		return;
	timer->enabled = 0;
	if (timer->prev)
		timer->prev->next = timer->next;
	else
		timer_head = timer->next;
	if (timer->next)
		timer->next->prev = timer->prev;
	timer->prev = timer->next = NULL;
}
void timer_addx(pc_timer_t *timer, void (*callback)(void* p), void *p, int start_timer)
{
	memset(timer, 0, sizeof(pc_timer_t));

	timer->callback = callback;
	timer->p = p;
	timer->enabled = 0;
	timer->prev = timer->next = NULL;
}
void timer_set_delay_u64x(pc_timer_t *timer, uint64_t delay)
{
	timer_enablex(timer);
}
static void timer_remove_headx(void)
{
	if (timer_head)
	{
		pc_timer_t *timer = timer_head;
		timer_head = timer->next;
		if (timer_head) {
			timer_head->prev = NULL;
		}
		timer->next = timer->prev = NULL;
		timer->enabled = 0;
	}
}
void timer_advance_u64x(pc_timer_t *timer, uint64_t delay)
{
	uint32_t int_delay = delay >> 32;
	uint32_t frac_delay = delay & 0xffffffff;

	if ((frac_delay + timer->ts_frac) < frac_delay)
		timer->ts_integer++;
	timer->ts_frac += frac_delay;
	timer->ts_integer += int_delay;

	timer_enablex(timer);
}


void pcemglue_hsync(void)
{
	while (timer_head) {
		timer_head->callback(timer_head->p);
		timer_remove_headx();
	}
}

void pcemvideorbswap(bool swapped)
{
	for (int c = 0; c < 65536; c++) {
		if (swapped) {
			video_15to32[c] = (((c >> 10) & 31) << 3) | (((c >> 5) & 31) << 11) | (((c >> 0) & 31) << 19);
		} else {
			video_15to32[c] = ((c & 31) << 3) | (((c >> 5) & 31) << 11) | (((c >> 10) & 31) << 19);
		}
		if (swapped) {
			video_16to32[c] = (((c >> 11) & 31) << 3) | (((c >> 5) & 63) << 10) | (((c >> 0) & 31) << 19);
		} else {
			video_16to32[c] = ((c & 31) << 3) | (((c >> 5) & 63) << 10) | (((c >> 11) & 31) << 19);
		}
	}
}

static int calc_6to8(int c)
{
	int    ic;
	int    i8;
	double d8;

	ic = c;
	if (ic == 64)
		ic = 63;
	else
		ic &= 0x3f;
	d8 = (ic / 63.0) * 255.0;
	i8 = (int)d8;

	return (i8 & 0xff);
}

void initpcemvideo(void *p, bool swapped)
{
	int c, d, e;

	int pcemblocks = MAX_PCEMMAPBLOCKS;
	if (!pcemmap) {
		pcemmap = xcalloc(mem_mapping_t *, pcemblocks);
		if (!pcemmap) {
			gui_message(_T("out of memory for PCI mappin table\n"));
			abort();
		}
	}
	for (int i = 0; i < pcemblocks; i++) {
		pcemmap[i] = NULL;
	}
	for (int i = 0; i < MAX_PCEMMAPPINGS; i++) {
		pcemmappings[i] = NULL;
	}

	gfxboard_priv = p;
	has_vlb = 0;
	timer_freq = syncbase;

	for (c = 0; c < 256; c++)
	{
		e = c;
		for (d = 0; d < 8; d++)
		{
			rotatevga[d][c] = e;
			e = (e >> 1) | ((e & 1) ? 0x80 : 0);
		}
	}
	for (c = 0; c < 4; c++)
	{
		for (d = 0; d < 4; d++)
		{
			edatlookup[c][d] = 0;
			if (c & 1) edatlookup[c][d] |= 1;
			if (d & 1) edatlookup[c][d] |= 2;
			if (c & 2) edatlookup[c][d] |= 0x10;
			if (d & 2) edatlookup[c][d] |= 0x20;
			//                        printf("Edat %i,%i now %02X\n",c,d,edatlookup[c][d]);
		}
	}

	if (!video_15to32)
		video_15to32 = (uint32_t*)malloc(4 * 65536);
	if (!video_16to32)
		video_16to32 = (uint32_t*)malloc(4 * 65536);
	pcemvideorbswap(swapped);

	if (!buffer32) {
		buffer32 = (PCBITMAP *)calloc(sizeof(PCBITMAP) + sizeof(uint8_t *) * 4096, 1);
		buffer32->w = 2048;
		buffer32->h = 4096;
		target_bitmap.w = buffer32->w;
		target_bitmap.h = buffer32->h;
		buffer32->dat = xcalloc(uae_u8, buffer32->w * buffer32->h * 4);
		target_bitmap.dat = (uae_u32*)buffer32->dat;
		for (int i = 0; i < buffer32->h; i++) {
			buffer32->line[i] = buffer32->dat + buffer32->w * 4 * i;
			target_bitmap.line[i] = (uae_u32*)buffer32->line[i];
		}
	}

	video_6to8 = (uint32_t*)malloc(4 * 256);
	for (uint16_t c = 0; c < 256; c++) {
		video_6to8[c] = calc_6to8(c);
	}

	pcem_linear_read_b = dummy_bread;
	pcem_linear_read_w = dummy_wread;
	pcem_linear_read_l = dummy_lread;
	pcem_linear_write_b = dummy_bwrite;
	pcem_linear_write_w = dummy_wwrite;
	pcem_linear_write_l = dummy_lwrite;
	pcem_mapping_linear = NULL;
	pcem_mapping_linear_offset = 0;
	timer_head = NULL;

	monitors[0].mon_changeframecount = 2;
	monitors[0].target_buffer = &target_bitmap;
}

extern void *svga_get_object(void);

uae_u8 pcem_read_io(int port, int index)
{
	svga_t *svga = (svga_t*)svga_get_object();
	if (port == 0x3d5) {
		return svga->crtc[index];
	} else {
		write_log("unknown port %04x!\n", port);
		abort();
	}
}

#if 0
void update_pcembuffer32(void *buf, int w, int h, int bytesperline)
{
	if (!buf) {
		w = 2048;
		h = 2048;
		if (!tempbuf) {
			tempbuf = xmalloc(uae_u8, w * 4);
		}
		buf = tempbuf;
		bytesperline = 0;
	}
	if (!buffer32 || h > buffer32->h) {
		free(buffer32);
		buffer32 = (PCBITMAP *)calloc(sizeof(PCBITMAP) + sizeof(uint8_t *) * h, 1);
	}
	if (buffer32->w == w && buffer32->h == h && buffer32->line[0] == buf)
		return;
	buffer32->w = w;
	buffer32->h = h;
	uae_u8 *b = (uae_u8*)buf;
	for (int i = 0; i < h; i++) {
		buffer32->line[i] = b;
		b += bytesperline;
	}
}
#endif

uae_u8 *getpcembuffer32(int x, int y, int yy)
{
	return buffer32->line[y + yy] + x * 4;
}

void video_inform_monitor(int type, const video_timings_t *ptr, int monitor_index)
{
}

int monitor_index_global;

void video_wait_for_buffer(void)
{
	//write_log(_T("video_wait_for_buffer\n"));
}
void updatewindowsize(int x, int mx, int y, int my)
{
	x *= mx;
	gfxboard_resize(x, y, mx, my, gfxboard_priv);
}

#define MAX_ADDED_DEVICES 8

struct addeddevice {
	const device_t *dev;
	void *priv;
};

static addeddevice added_devices[MAX_ADDED_DEVICES];

void *device_add(const device_t *d)
{
	for (int i = 0; i < MAX_ADDED_DEVICES; i++) {
		if (!added_devices[i].dev) {
			added_devices[i].dev = d;
			added_devices[i].priv = d->init(d);
			return added_devices[i].priv;
		}
	}
	return NULL;
}

void pcemfreeaddeddevices(void)
{
	for (int i = 0; i < MAX_ADDED_DEVICES; i++) {
		if (added_devices[i].dev) {
			added_devices[i].dev->close(added_devices[i].priv);
			added_devices[i].dev = NULL;
		}
	}
}

static void (*pci_card_write)(int func, int addr, uint8_t val, void *priv);
static uint8_t(*pci_card_read)(int func, int addr, void *priv);
static void *pci_card_priv;

void put_pci_pcem(uaecptr addr, uae_u8 v)
{
	if (pci_card_write) {
		pci_card_write(0, addr, v, pci_card_priv);
	}
}
uae_u8 get_pci_pcem(uaecptr addr)
{
	uae_u8 v = 0;
	if (pci_card_read) {
		v = pci_card_read(0, addr, pci_card_priv);
	}
	return v;
}

void pci_add_card(uint8_t add_type, uint8_t(*read)(int func, int addr, void *priv),
	void (*write)(int func, int addr, uint8_t val, void *priv), void *priv, uint8_t *slot)
{
	pci_card_read = read;
	pci_card_write = write;
	pci_card_priv = priv;
}

int pci_add(uint8_t(*read)(int func, int addr, void *priv), void (*write)(int func, int addr, uint8_t val, void *priv), void *priv)
{
	pci_card_read = read;
	pci_card_write = write;
	pci_card_priv = priv;
	return 0;
}

void pci_set_irq_routing(int card, int irq)
{
	//write_log(_T("pci_set_irq_routing %d %d\n"), card, irq);
}
void pci_set_irq(int card, int pci_int, uint8_t *state)
{
	//write_log(_T("pci_set_irq %d %d\n"), card, pci_int);
	gfxboard_intreq(gfxboard_priv, 1, true);
}
void pci_clear_irq(int card, int pci_int, uint8_t *state)
{
	//write_log(_T("pci_clear_irq %d %d\n"), card, pci_int);
	gfxboard_intreq(gfxboard_priv, 0, true);
}
int rom_init(rom_t *rom, const char *fn, uint32_t address, int size, int mask, int file_offset, uint32_t flags)
{
	return 0;
}

void thread_sleep(int n)
{
	sleep_millis(n);
}

typedef struct pcem_thread_handle
{
	uae_thread_id tid;
} pcem_thread_handle;

thread_t *thread_create(void (*thread_rout)(void *param), void *param)
{
	pcem_thread_handle *handle = xcalloc(pcem_thread_handle, 1);
	if (!uae_start_thread(_T("PCem helper"), thread_rout, param, &handle->tid)) {
		xfree(handle);
		return NULL;
	}
	return (thread_t*)handle;
}
void thread_kill(thread_t *handle)
{
	if (!handle) {
		return;
	}
	pcem_thread_handle *thread = (pcem_thread_handle*)handle;
	uae_end_thread(&thread->tid);
	xfree(thread);
}
event_t *thread_create_event(void)
{
	uae_sem_t sem = { 0 };
	uae_sem_init(&sem, 1, 0);
	return sem;
}
int thread_wait(thread_t *arg)
{
	if (!arg) {
		return 0;
	}
	pcem_thread_handle *thread = (pcem_thread_handle*)arg;
	uae_wait_thread(thread->tid);
	xfree(thread);
	return 1;
}
void thread_set_event(event_t *event)
{
	uae_sem_t sem = (uae_sem_t)event;
	uae_sem_post(&sem);
}
void thread_reset_event(event_t *_event)
{
	uae_sem_t sem = (uae_sem_t)_event;
	uae_sem_unpost(&sem);
}
int thread_wait_event(event_t *event, int timeout)
{
	uae_sem_t sem = (uae_sem_t)event;
	return uae_sem_trywait_delay(&sem, timeout < 0 ? -1 : timeout);
}
void thread_destroy_event(event_t *_event)
{
	uae_sem_t sem = (uae_sem_t)_event;
	uae_sem_destroy(&sem);
}

typedef struct win_mutex_t
{
	HANDLE handle;
} win_mutex_t;

mutex_t* thread_create_mutex(void)
{
	win_mutex_t *mutex = xcalloc(win_mutex_t,1);
	mutex->handle = CreateSemaphore(NULL, 1, 1, NULL);
	return mutex;
}

int thread_wait_mutex(mutex_t *_mutex)
{
	if (!_mutex) {
		return 0;
	}
	win_mutex_t *mutex = (win_mutex_t*)_mutex;
	WaitForSingleObject(mutex->handle, INFINITE);
	return 1;
}

int thread_release_mutex(mutex_t *_mutex)
{
	if (!_mutex) {
		return 0;
	}
	win_mutex_t *mutex = (win_mutex_t*)_mutex;
	ReleaseSemaphore(mutex->handle, 1, NULL);
	return 1;
}

void thread_close_mutex(mutex_t *_mutex)
{
	win_mutex_t *mutex = (win_mutex_t*)_mutex;
	CloseHandle(mutex->handle);
	xfree(mutex);
}

int thread_test_mutex(mutex_t *_mutex)
{
	if (!_mutex) {
		return 0;
	}
	win_mutex_t *mutex = (win_mutex_t *)_mutex;
	DWORD ret = WaitForSingleObject(mutex->handle, 0);
	if (ret == WAIT_OBJECT_0)
		return 1;
	return 0;
}



static mem_mapping_t *getmm(uaecptr *addrp)
{
	uaecptr addr = *addrp;
	addr &= 0x7fffffff;
	int index = addr / PCEMMAPBLOCKSIZE;
	mem_mapping_t *m = pcemmap[index];
	if (!m) {
		write_log(_T("%08x no mapping\n"), addr);
	}
	return m;
}

void put_mem_pcem(uaecptr addr, uae_u32 v, int size)
{
#if 0
	write_log("put_mem_pcem %08x %08x %d\n", addr, v, size);
#endif
	mem_mapping_t *m = getmm(&addr);
	if (m) {
		if (size == 0) {
			m->write_b(addr, v, m->p);
		} else if (size == 1) {
			m->write_w(addr, v, m->p);
		} else if (size == 2) {
			m->write_l(addr, v, m->p);
		}
	}
}
uae_u32 get_mem_pcem(uaecptr addr, int size)
{
	uae_u32 v = 0;
	mem_mapping_t *m = getmm(&addr);
	if (m) {
		if (size == 0) {
			v = m->read_b(addr, m->p);
		} else if (size == 1) {
			v = m->read_w(addr, m->p);
		} else if (size == 2) {
			v = m->read_l(addr, m->p);
		}
	}
#if 0
	write_log("get_mem_pcem %08x %08x %d\n", addr, v, size);
#endif
	return v;
}

static void mapping_recalc(mem_mapping_t *mapping)
{
	if (mapping->read_b == NULL)
		mapping->read_b = dummy_bread;
	if (mapping->read_w == NULL)
		mapping->read_w = dummy_wread;
	if (mapping->read_l == NULL)
		mapping->read_l = dummy_lread;
	if (mapping->write_b == NULL)
		mapping->write_b = dummy_bwrite;
	if (mapping->write_w == NULL)
		mapping->write_w = dummy_wwrite;
	if (mapping->write_l == NULL)
		mapping->write_l = dummy_lwrite;


	if (mapping == pcem_mapping_linear || (!pcem_mapping_linear && mapping->size >= 1024 * 1024)) {
		pcem_mapping_linear = mapping;
		pcem_mapping_linear_priv = mapping->p;
		pcem_mapping_linear_offset = mapping->base;
		if (!mapping->enable) {
			pcem_linear_read_b = dummy_bread;
			pcem_linear_read_w = dummy_wread;
			pcem_linear_read_l = dummy_lread;
			pcem_linear_write_b = dummy_bwrite;
			pcem_linear_write_w = dummy_wwrite;
			pcem_linear_write_l = dummy_lwrite;
		} else {
			pcem_linear_read_b = mapping->read_b;
			pcem_linear_read_w = mapping->read_w;
			pcem_linear_read_l = mapping->read_l;
			pcem_linear_write_b = mapping->write_b;
			pcem_linear_write_w = mapping->write_w;
			pcem_linear_write_l = mapping->write_l;
		}
	}

	for (uae_u32 i = 0; i < mapping->size; i += PCEMMAPBLOCKSIZE) {
		uae_u32 addr = i + mapping->base;
		addr &= 0x7fffffff;
		int offset = addr / PCEMMAPBLOCKSIZE;
		if (offset >= 0 && offset < MAX_PCEMMAPBLOCKS) {
			if (mapping->enable) {
				pcemmap[offset] = mapping;
			} else {
				pcemmap[offset] = NULL;
			}
		}
	}
}

void mem_mapping_addx(mem_mapping_t *mapping,
	uint32_t base,
	uint32_t size,
	uint8_t(*read_b)(uint32_t addr, void *p),
	uint16_t(*read_w)(uint32_t addr, void *p),
	uint32_t(*read_l)(uint32_t addr, void *p),
	void (*write_b)(uint32_t addr, uint8_t  val, void *p),
	void (*write_w)(uint32_t addr, uint16_t val, void *p),
	void (*write_l)(uint32_t addr, uint32_t val, void *p),
	uint8_t *exec,
	uint32_t flags,
	void *p)
{
	mapping->enable = 0;
	for (int i = 0; i < MAX_PCEMMAPPINGS; i++) {
		if (pcemmappings[i] == mapping)
			return;
	}
	for (int i = 0; i < MAX_PCEMMAPPINGS; i++) {
		if (pcemmappings[i] == NULL) {
			pcemmappings[i] = mapping;
			mapping->base = base;
			mapping->size = size;
			mapping->read_b = read_b;
			mapping->read_w = read_w;
			mapping->read_l = read_l;
			mapping->write_b = write_b;
			mapping->write_w = write_w;
			mapping->write_l = write_l;
			mapping->exec = exec;
			mapping->flags = flags;
			mapping->p = p;
			mapping->next = NULL;
			mapping_recalc(mapping);
			return;
		}
	}
}

void mem_mapping_set_handlerx(mem_mapping_t *mapping,
	uint8_t(*read_b)(uint32_t addr, void *p),
	uint16_t(*read_w)(uint32_t addr, void *p),
	uint32_t(*read_l)(uint32_t addr, void *p),
	void (*write_b)(uint32_t addr, uint8_t  val, void *p),
	void (*write_w)(uint32_t addr, uint16_t val, void *p),
	void (*write_l)(uint32_t addr, uint32_t val, void *p))
{
	mapping->read_b = read_b;
	mapping->read_w = read_w;
	mapping->read_l = read_l;
	mapping->write_b = write_b;
	mapping->write_w = write_w;
	mapping->write_l = write_l;
	mapping_recalc(mapping);
}

void mem_mapping_set_px(mem_mapping_t *mapping, void *p)
{
	mapping->p = p;
}

void pci_add_specific(int card, uint8_t(*read)(int func, int addr, void *priv), void (*write)(int func, int addr, uint8_t val, void *priv), void *priv)
{
}

void mca_add(uint8_t(*read)(int addr, void *priv), void (*write)(int addr, uint8_t val, void *priv), void (*reset)(void *priv), void *priv)
{
}

#define MAX_IO_PORT 0x10000
static uint8_t(*port_inb[MAX_IO_PORT])(uint16_t addr, void *priv);
static uint16_t(*port_inw[MAX_IO_PORT])(uint16_t addr, void *priv);
static uint32_t(*port_inl[MAX_IO_PORT])(uint16_t addr, void *priv);
static void(*port_outb[MAX_IO_PORT])(uint16_t addr, uint8_t  val, void *priv);
static void(*port_outw[MAX_IO_PORT])(uint16_t addr, uint16_t val, void *priv);
static void(*port_outl[MAX_IO_PORT])(uint16_t addr, uint32_t val, void *priv);
static void *port_priv[MAX_IO_PORT];

void put_io_pcem(uaecptr addr, uae_u32 v, int size)
{
#if 0
	write_log(_T("put_io_pcem(%08x,%08x,%d)\n"), addr, v, size);
#endif

	addr &= MAX_IO_PORT - 1;
	if (size == 0) {
		if (port_outb[addr])
			port_outb[addr](addr, v, port_priv[addr]);
	} else if (size == 1) {
		if (port_outw[addr]) {
			port_outw[addr](addr, v, port_priv[addr]);
		} else {
			put_io_pcem(addr + 0, v >> 0, 0);
			put_io_pcem(addr + 1, v >> 8, 0);
		}
	} else if (size == 2) {
		if (port_outl[addr]) {
			port_outl[addr](addr, v, port_priv[addr]);
		} else {
			put_io_pcem(addr + 0, v >> 16, 1);
			put_io_pcem(addr + 2, v >> 0, 1);
		}
	}
}
uae_u32 get_io_pcem(uaecptr addr, int size)
{
#if 0
	write_log(_T("get_io_pcem(%08x,%d)\n"), addr, size);
#endif

	uae_u32 v = 0;
	addr &= MAX_IO_PORT - 1;
	if (size == 0) {
		if (port_inb[addr])
			v = port_inb[addr](addr, port_priv[addr]);
	} else if (size == 1) {
		if (port_inw[addr]) {
			v = port_inw[addr](addr, port_priv[addr]);
		} else {
			v = get_io_pcem(addr + 0, 0) << 0;
			v |= get_io_pcem(addr + 1, 0) << 8;
		}
	} else if (size == 2) {
		if (port_inl[addr]) {
			v = port_inl[addr](addr, port_priv[addr]);
		} else {
			v = get_io_pcem(addr + 0, 1) << 16;
			v |= get_io_pcem(addr + 2, 1) << 0;
		}
	}
	return v;
}

void io_sethandlerx(uint16_t base, int size,
	uint8_t(*inb)(uint16_t addr, void *priv),
	uint16_t(*inw)(uint16_t addr, void *priv),
	uint32_t(*inl)(uint16_t addr, void *priv),
	void (*outb)(uint16_t addr, uint8_t  val, void *priv),
	void (*outw)(uint16_t addr, uint16_t val, void *priv),
	void (*outl)(uint16_t addr, uint32_t val, void *priv),
	void *priv)
{
	if (base + size > MAX_IO_PORT)
		return;

	for (int i = 0; i < size; i++) {
		int io = base + i;
		port_inb[io] = inb;
		port_inw[io] = inw;
		port_inl[io] = inl;
		port_outb[io] = outb;
		port_outw[io] = outw;
		port_outl[io] = outl;
		port_priv[io] = priv;
	}
}

void io_removehandlerx(uint16_t base, int size,
	uint8_t(*inb)(uint16_t addr, void *priv),
	uint16_t(*inw)(uint16_t addr, void *priv),
	uint32_t(*inl)(uint16_t addr, void *priv),
	void (*outb)(uint16_t addr, uint8_t  val, void *priv),
	void (*outw)(uint16_t addr, uint16_t val, void *priv),
	void (*outl)(uint16_t addr, uint32_t val, void *priv),
	void *priv)
{
	if (base + size > MAX_IO_PORT)
		return;

	for (int i = 0; i < size; i++) {
		int io = base + i;
		port_inb[io] = NULL;
		port_inw[io] = NULL;
		port_inl[io] = NULL;
		port_outb[io] = NULL;
		port_outw[io] = NULL;
		port_outl[io] = NULL;
	}
}

void mem_mapping_set_addrx(mem_mapping_t *mapping, uint32_t base, uint32_t size)
{
	mapping->enable = 0;
	mapping_recalc(mapping);
	mapping->enable = 1;
	mapping->base = base;
	mapping->size = size;
	mapping_recalc(mapping);
}

void mem_mapping_disablex(mem_mapping_t *mapping)
{
	mapping->enable = 0;
	mapping_recalc(mapping);
}

void mem_mapping_enablex(mem_mapping_t *mapping)
{
	mapping->enable = 1;
	mapping_recalc(mapping);
}

void pcem_linear_mark(int offset)
{
	if (!pcem_mapping_linear)
		return;
	uae_u16 w = pcem_linear_read_w(offset, pcem_mapping_linear_priv);
	pcem_linear_write_w(offset, w, pcem_mapping_linear_priv);
}

int model_get_config_int(const char *s)
{
	return 0;
}
char *model_get_config_string(const char *s)
{
	return NULL;
}
void upc_set_mouse(void (*mouse_write)(uint8_t, void*), void *p)
{
}


/* DMA Bus Master Page Read/Write */
void
dma_bm_read(uint32_t PhysAddress, uint8_t *DataRead, uint32_t TotalSize, int TransferSize)
{
#if 0
	uint32_t n;
	uint32_t n2;
	uint8_t  bytes[4] = { 0, 0, 0, 0 };

	n = TotalSize & ~(TransferSize - 1);
	n2 = TotalSize - n;

	/* Do the divisible block, if there is one. */
	if (n) {
		for (uint32_t i = 0; i < n; i += TransferSize)
			mem_read_phys((void *)&(DataRead[i]), PhysAddress + i, TransferSize);
	}

	/* Do the non-divisible block, if there is one. */
	if (n2) {
		mem_read_phys((void *)bytes, PhysAddress + n, TransferSize);
		memcpy((void *)&(DataRead[n]), bytes, n2);
	}
#endif
}

void
dma_bm_write(uint32_t PhysAddress, const uint8_t *DataWrite, uint32_t TotalSize, int TransferSize)
{
#if 0
	uint32_t n;
	uint32_t n2;
	uint8_t  bytes[4] = { 0, 0, 0, 0 };

	n = TotalSize & ~(TransferSize - 1);
	n2 = TotalSize - n;

	/* Do the divisible block, if there is one. */
	if (n) {
		for (uint32_t i = 0; i < n; i += TransferSize)
			mem_write_phys((void *)&(DataWrite[i]), PhysAddress + i, TransferSize);
	}

	/* Do the non-divisible block, if there is one. */
	if (n2) {
		mem_read_phys((void *)bytes, PhysAddress + n, TransferSize);
		memcpy(bytes, (void *)&(DataWrite[n]), n2);
		mem_write_phys((void *)bytes, PhysAddress + n, TransferSize);
	}

	if (dma_at)
		mem_invalidate_range(PhysAddress, PhysAddress + TotalSize - 1);
#endif
}

void video_force_resize_set_monitor(uint8_t res, int monitor_index)
{
}

// The PCem versions of these went with PCem's x86 emulation
int device_get_config_int(const char *s)
{
	int v;
	return x86_device_config(NULL, s, &v) ? v : 0;
}

void mem_mapping_enable(mem_mapping_t *mapping)
{
	mem_mapping_enablex(mapping);
}
