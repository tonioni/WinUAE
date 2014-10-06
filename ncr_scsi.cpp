/*
* UAE - The Un*x Amiga Emulator
*
* A4000T / A4091 NCR 53C710 SCSI
*
* (c) 2007-2014 Toni Wilen
*/

#include "sysconfig.h"
#include "sysdeps.h"

#ifdef NCR

#define NCR_DEBUG 0

#include "options.h"
#include "uae.h"
#include "memory.h"
#include "rommgr.h"
#include "custom.h"
#include "newcpu.h"
#include "ncr_scsi.h"
#include "scsi.h"
#include "filesys.h"
#include "zfile.h"
#include "blkdev.h"
#include "cpuboard.h"
#include "qemuvga/qemuuaeglue.h"
#include "qemuvga/queue.h"
#include "qemuvga/scsi/scsi.h"
#include "gui.h"

#define BOARD_SIZE 16777216
#define IO_MASK 0xff

#define A4091_ROM_VECTOR 0x0200
#define A4091_ROM_OFFSET 0x0000
#define A4091_ROM_SIZE 32768
#define A4091_ROM_MASK (A4091_ROM_SIZE - 1)

#define A4091_IO_OFFSET 0x00800000
#define A4091_IO_ALT 0x00840000
#define A4091_IO_END 0x00880000

#define A4091_DIP_OFFSET 0x008c0003

#define WARP_ENGINE_IO_OFFSET 0x40000
#define WARP_ENGINE_IO_END 0x80000

#define CYBERSTORM_SCSI_RAM_OFFSET 0x1000
#define CYBERSTORM_SCSI_RAM_SIZE 0x2000
#define CYBERSTORM_SCSI_RAM_MASK 0x1fff

struct ncr_state
{
	bool newncr;
	const TCHAR *name;
	DeviceState devobject;
	SCSIDevice *scsid[8];
	SCSIBus scsibus;
	uae_u32 board_mask;
	uae_u8 *rom;
	int ramsize;
	uae_u8 acmemory[128];
	uae_u32 expamem_hi;
	uae_u32 expamem_lo;
	int configured;
	bool enabled;
	int rom_start, rom_end, rom_offset;
	int io_start, io_end;
	addrbank *bank;
	bool irq;
	void (*irq_func)(int);
};

static struct ncr_state ncr_a4091;
static struct ncr_state ncr_a4091_2;
static struct ncr_state ncr_a4000t;
static struct ncr_state ncr_we;
static struct ncr_state ncr_cpuboard;

static struct ncr_state ncr_cs;
static struct ncr_state ncr_bppc;

static struct ncr_state *ncrs[] =
{
	&ncr_a4091,
	&ncr_a4091_2,
	&ncr_a4000t,
	&ncr_we,
	&ncr_cpuboard,
	&ncr_bppc,
	NULL
};

static struct ncr_state *ncra4091[] =
{
	&ncr_a4091,
	&ncr_a4091_2
};

extern void cyberstorm_irq(int);
extern void blizzardppc_irq(int);

static void set_irq2(int level)
{
	if (level)
		INTREQ(0x8000 | 0x0008);
}

void ncr_rethink(void)
{
	for (int i = 0; ncrs[i]; i++) {
		if (ncrs[i]->irq)
			INTREQ_0(0x8000 | 0x0008);
	}
	if (ncr_cs.irq)
		cyberstorm_irq(1);
}

/* 720+ */

void pci_set_irq(PCIDevice *pci_dev, int level)
{
	struct ncr_state *ncr = (struct ncr_state*)pci_dev;
	ncr->irq = level != 0;
	ncr->irq_func(ncr->irq);
}

void scsi_req_continue(SCSIRequest *req)
{
	struct scsi_data *sd = (struct scsi_data*)req->dev->handle;
	if (sd->data_len < 0) {
		lsi_command_complete(req, sd->status, 0);
	} else if (sd->data_len) {
		lsi_transfer_data(req, sd->data_len);
	} else {
		if (sd->direction > 0)
			scsi_emulate_cmd(sd);
		lsi_command_complete(req, sd->status, 0);
	}
}
SCSIRequest *scsi_req_new(SCSIDevice *d, uint32_t tag, uint32_t lun, uint8_t *buf, int len, void *hba_private)
{
	SCSIRequest *req = xcalloc(SCSIRequest, 1);
	struct scsi_data *sd = (struct scsi_data*)d->handle;
	struct ncr_state *ncr = (struct ncr_state*)sd->privdata;

	req->dev = d;
	req->hba_private = hba_private;
	req->bus = &ncr->scsibus;
	req->bus->qbus.parent = &ncr->devobject;

	memcpy(sd->cmd, buf, len);
	sd->cmd_len = len;
	return req;
}
int32_t scsi_req_enqueue(SCSIRequest *req)
{
	struct scsi_data *sd = (struct scsi_data*)req->dev->handle;

	if (sd->device_type == UAEDEV_CD)
		gui_flicker_led (LED_CD, sd->id, 1);

	sd->data_len = 0;
	scsi_start_transfer(sd);
	scsi_emulate_analyze(sd);
	//write_log (_T("%02x.%02x.%02x.%02x.%02x.%02x\n"), sd->cmd[0], sd->cmd[1], sd->cmd[2], sd->cmd[3], sd->cmd[4], sd->cmd[5]);

	if (sd->direction <= 0)
		scsi_emulate_cmd(sd);
	if (sd->direction == 0)
		return 1;
	return -sd->direction;
}
void scsi_req_unref(SCSIRequest *req)
{
	xfree(req);
}
uint8_t *scsi_req_get_buf(SCSIRequest *req)
{
	struct scsi_data *sd = (struct scsi_data*)req->dev->handle;
	sd->data_len = 0;
	return sd->buffer;
}
SCSIDevice *scsi_device_find(SCSIBus *bus, int channel, int target, int lun)
{
	struct ncr_state *ncr = (struct ncr_state*)bus->privdata;
	if (lun != 0 || target < 0 || target >= 8)
		return NULL;
	return ncr->scsid[target];
}
void scsi_req_cancel(SCSIRequest *req)
{
	write_log(_T("scsi_req_cancel\n"));
}

int pci_dma_rw(PCIDevice *dev, dma_addr_t addr, void *buf, dma_addr_t len, DMADirection dir)
{
	int i = 0;
	uae_u8 *p = (uae_u8*)buf;
	while (len > 0) {
		if (!dir) {
			*p = get_byte(addr);
		}
		else {
			put_byte(addr, *p);
		}
		p++;
		len--;
		addr++;
	}
	return 0;
}
/* 710 */

void pci710_set_irq(PCIDevice *pci_dev, int level)
{
	struct ncr_state *ncr = (struct ncr_state*)pci_dev;
	ncr->irq = level != 0;
	ncr->irq_func(ncr->irq);
}

void scsi710_req_continue(SCSIRequest *req)
{
	struct scsi_data *sd = (struct scsi_data*)req->dev->handle;
	if (sd->data_len < 0) {
		lsi710_command_complete(req, sd->status, 0);
	} else if (sd->data_len) {
		lsi710_transfer_data(req, sd->data_len);
	} else {
		if (sd->direction > 0)
			scsi_emulate_cmd(sd);
		lsi710_command_complete(req, sd->status, 0);
	}
}
SCSIRequest *scsi710_req_new(SCSIDevice *d, uint32_t tag, uint32_t lun, uint8_t *buf, int len, void *hba_private)
{
	SCSIRequest *req = xcalloc(SCSIRequest, 1);
	struct scsi_data *sd = (struct scsi_data*)d->handle;
	struct ncr_state *ncr = (struct ncr_state*)sd->privdata;

	req->dev = d;
	req->hba_private = hba_private;
	req->bus = &ncr->scsibus;
	req->bus->qbus.parent = &ncr->devobject;
	
	memcpy (sd->cmd, buf, len);
	sd->cmd_len = len;
	return req;
}
int32_t scsi710_req_enqueue(SCSIRequest *req)
{
	struct scsi_data *sd = (struct scsi_data*)req->dev->handle;

	if (sd->device_type == UAEDEV_CD)
		gui_flicker_led (LED_CD, sd->id, 1);

	sd->data_len = 0;
	scsi_start_transfer (sd);
	scsi_emulate_analyze (sd);
	//write_log (_T("%02x.%02x.%02x.%02x.%02x.%02x\n"), sd->cmd[0], sd->cmd[1], sd->cmd[2], sd->cmd[3], sd->cmd[4], sd->cmd[5]);
	
	if (sd->direction <= 0)
		scsi_emulate_cmd(sd);
	if (sd->direction == 0)
		return 1;
	return -sd->direction;
}
void scsi710_req_unref(SCSIRequest *req)
{
	xfree (req);
}
uint8_t *scsi710_req_get_buf(SCSIRequest *req)
{
	struct scsi_data *sd = (struct scsi_data*)req->dev->handle;
	sd->data_len = 0;
	return sd->buffer;
}
SCSIDevice *scsi710_device_find(SCSIBus *bus, int channel, int target, int lun)
{
	struct ncr_state *ncr = (struct ncr_state*)bus->privdata;
	if (lun != 0 || target < 0 || target >= 8)
		return NULL;
	return ncr->scsid[target];
}
void scsi710_req_cancel(SCSIRequest *req)
{
	write_log (_T("scsi_req_cancel\n"));
}

int pci710_dma_rw(PCIDevice *dev, dma_addr_t addr, void *buf, dma_addr_t len, DMADirection dir)
{
	int i = 0;
	uae_u8 *p = (uae_u8*)buf;
	while (len > 0) {
		if (!dir) {
			*p = get_byte (addr);
		} else {
			put_byte (addr, *p);
		}
		p++;
		len--;
		addr++;
	}
	return 0;
}

static uae_u8 read_rombyte(struct ncr_state *ncr, uaecptr addr)
{
	uae_u8 v = ncr->rom[addr];
	//write_log (_T("%08X = %02X PC=%08X\n"), addr, v, M68K_GETPC);
	return v;
}

static uaecptr beswap(uaecptr addr)
{
	return (addr & ~3) | (3 - (addr & 3));
}

static void ncr_io_bput(struct ncr_state *ncr, uaecptr addr, uae_u32 val)
{
	if (addr >= CYBERSTORM_SCSI_RAM_OFFSET && ncr->ramsize) {
		cyberstorm_scsi_ram_put(addr, val);
		return;
	}
	addr &= IO_MASK;
	lsi_mmio_write(ncr->devobject.lsistate, beswap(addr), val, 1);
}
static void ncr710_io_bput(struct ncr_state *ncr, uaecptr addr, uae_u32 val)
{
	addr &= IO_MASK;
	lsi710_mmio_write(ncr->devobject.lsistate, beswap(addr), val, 1);
}
void cpuboard_ncr710_io_bput(uaecptr addr, uae_u32 v)
{
	ncr710_io_bput(&ncr_cpuboard, addr, v);
}

static void ncr_bput2 (struct ncr_state *ncr, uaecptr addr, uae_u32 val)
{
	uae_u32 v = val;
	addr &= ncr->board_mask;
	if (ncr->io_end && (addr < ncr->io_start || addr >= ncr->io_end))
		return;
	if (ncr->newncr)
		ncr_io_bput(ncr, addr, val);
	else
		ncr710_io_bput(ncr, addr, val);
}

static uae_u32 ncr_io_bget(struct ncr_state *ncr, uaecptr addr)
{
	if (addr >= CYBERSTORM_SCSI_RAM_OFFSET && ncr->ramsize)
		return cyberstorm_scsi_ram_get(addr);
	addr &= IO_MASK;
	return lsi_mmio_read(ncr->devobject.lsistate, beswap(addr), 1);
}
static uae_u32 ncr710_io_bget(struct ncr_state *ncr, uaecptr addr)
{
	addr &= IO_MASK;
	return lsi710_mmio_read(ncr->devobject.lsistate, beswap(addr), 1);
}
uae_u32 cpuboard_ncr710_io_bget(uaecptr addr)
{
	return ncr710_io_bget(&ncr_cpuboard, addr);
}

static uae_u32 ncr_bget2 (struct ncr_state *ncr, uaecptr addr)
{
	uae_u32 v = 0;

	addr &= ncr->board_mask;
	if (ncr->rom && addr >= ncr->rom_start && addr < ncr->rom_end)
		return read_rombyte (ncr, addr - ncr->rom_offset);
	if (addr == A4091_DIP_OFFSET)
		return 0xff;
	if (ncr->io_end && (addr < ncr->io_start || addr >= ncr->io_end))
		return v;
	if (ncr->newncr)
		return ncr_io_bget(ncr, addr);
	else
		return ncr710_io_bget(ncr, addr);
}

static uae_u32 REGPARAM2 ncr_lget (struct ncr_state *ncr, uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= ncr->board_mask;
	if (ncr == &ncr_we) {
		addr &= ~0x80;
		v = (ncr_bget2(ncr, addr + 3) << 0) | (ncr_bget2(ncr, addr + 2) << 8) |
			(ncr_bget2(ncr, addr + 1) << 16) | (ncr_bget2(ncr, addr + 0) << 24);
	} else {
		if (addr >= A4091_IO_ALT) {
			v = (ncr_bget2 (ncr, addr + 3) << 0) | (ncr_bget2 (ncr, addr + 2) << 8) |
				(ncr_bget2 (ncr, addr + 1) << 16) | (ncr_bget2 (ncr, addr + 0) << 24);
		} else {
			v = (ncr_bget2 (ncr, addr + 3) << 0) | (ncr_bget2 (ncr, addr + 2) << 8) |
				(ncr_bget2 (ncr, addr + 1) << 16) | (ncr_bget2 (ncr, addr + 0) << 24);
		}
	}
	return v;
}

static uae_u32 REGPARAM2 ncr_wget (struct ncr_state *ncr, uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v = (ncr_bget2 (ncr, addr) << 8) | ncr_bget2 (ncr, addr + 1);
	return v;
}

static uae_u32 REGPARAM2 ncr_bget (struct ncr_state *ncr, uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= ncr->board_mask;
	if (!ncr->configured) {
		if (addr >= sizeof ncr->acmemory)
			return 0;
		return ncr->acmemory[addr];
	}
	v = ncr_bget2 (ncr, addr);
	return v;
}

static void REGPARAM2 ncr_lput (struct ncr_state *ncr, uaecptr addr, uae_u32 l)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= ncr->board_mask;
	if (ncr == &ncr_we) {
		addr &= ~0x80;
		ncr_bput2(ncr, addr + 3, l >> 0);
		ncr_bput2(ncr, addr + 2, l >> 8);
		ncr_bput2(ncr, addr + 1, l >> 16);
		ncr_bput2(ncr, addr + 0, l >> 24);
	} else {
		if (addr >= A4091_IO_ALT) {
			ncr_bput2 (ncr, addr + 3, l >> 0);
			ncr_bput2 (ncr, addr + 2, l >> 8);
			ncr_bput2 (ncr, addr + 1, l >> 16);
			ncr_bput2 (ncr, addr + 0, l >> 24);
		} else {
			ncr_bput2 (ncr, addr + 3, l >> 0);
			ncr_bput2 (ncr, addr + 2, l >> 8);
			ncr_bput2 (ncr, addr + 1, l >> 16);
			ncr_bput2 (ncr, addr + 0, l >> 24);
		}
	}
}

DECLARE_MEMORY_FUNCTIONS(ncr4)

static addrbank ncr_bank_a4091 = {
	ncr4_lget, ncr4_wget, ncr4_bget,
	ncr4_lput, ncr4_wput, ncr4_bput,
	default_xlate, default_check, NULL, NULL, _T("A4091"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO | ABFLAG_SAFE | ABFLAG_THREADSAFE
};

DECLARE_MEMORY_FUNCTIONS(ncr42)

static addrbank ncr_bank_a4091_2 = {
	ncr42_lget, ncr42_wget, ncr42_bget,
	ncr42_lput, ncr42_wput, ncr42_bput,
	default_xlate, default_check, NULL, NULL, _T("A4091 #2"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO | ABFLAG_SAFE | ABFLAG_THREADSAFE
};

static void REGPARAM2 ncr_wput (struct ncr_state *ncr, uaecptr addr, uae_u32 w)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	w &= 0xffff;
	addr &= ncr->board_mask;
	if (!ncr->configured) {
		switch (addr)
		{
			case 0x44:
			map_banks (ncr->bank, expamem_z3_pointer >> 16, BOARD_SIZE >> 16, 0);
			ncr->board_mask = 0x00ffffff;
			ncr->configured = 1;
			expamem_next (ncr->bank, NULL);
			break;
		}
		return;
	}
	ncr_bput2(ncr, addr, w >> 8);
	ncr_bput2 (ncr, addr + 1, w);
}

static void REGPARAM2 ncr_bput (struct ncr_state *ncr, uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	b &= 0xff;
	addr &= ncr->board_mask;
	if (!ncr->configured) {
		switch (addr)
		{
			case 0x4c:
			ncr->configured = 1;
			expamem_shutup(ncr->bank);
			break;
			case 0x48:
			ncr->expamem_lo = b & 0xff;
			break;
		}
		return;
	}
	ncr_bput2 (ncr, addr, b);
}

void ncr710_io_bput_a4000t(uaecptr addr, uae_u32 v)
{
	ncr710_io_bput(&ncr_a4000t, addr, v);
}
uae_u32 ncr710_io_bget_a4000t(uaecptr addr)
{
	return ncr710_io_bget(&ncr_a4000t, addr);
}

static void REGPARAM2 ncr4_bput (uaecptr addr, uae_u32 b)
{
	ncr_bput(&ncr_a4091, addr, b);
}
static void REGPARAM2 ncr4_wput (uaecptr addr, uae_u32 b)
{
	ncr_wput(&ncr_a4091, addr, b);
}
static void REGPARAM2 ncr4_lput (uaecptr addr, uae_u32 b)
{
	ncr_lput(&ncr_a4091, addr, b);
}
static uae_u32 REGPARAM2 ncr4_bget (uaecptr addr)
{
	return ncr_bget(&ncr_a4091, addr);
}
static uae_u32 REGPARAM2 ncr4_wget (uaecptr addr)
{
	return ncr_wget(&ncr_a4091, addr);
}
static uae_u32 REGPARAM2 ncr4_lget (uaecptr addr)
{
	return ncr_lget(&ncr_a4091, addr);
}

static void REGPARAM2 ncr42_bput (uaecptr addr, uae_u32 b)
{
	ncr_bput(&ncr_a4091_2, addr, b);
}
static void REGPARAM2 ncr42_wput (uaecptr addr, uae_u32 b)
{
	ncr_wput(&ncr_a4091_2, addr, b);
}
static void REGPARAM2 ncr42_lput (uaecptr addr, uae_u32 b)
{
	ncr_lput(&ncr_a4091_2, addr, b);
}
static uae_u32 REGPARAM2 ncr42_bget (uaecptr addr)
{
	return ncr_bget(&ncr_a4091_2, addr);
}
static uae_u32 REGPARAM2 ncr42_wget (uaecptr addr)
{
	return ncr_wget(&ncr_a4091_2, addr);
}
static uae_u32 REGPARAM2 ncr42_lget (uaecptr addr)
{
	return ncr_lget(&ncr_a4091_2, addr);
}

static void REGPARAM2 we_bput (uaecptr addr, uae_u32 b)
{
	ncr_bput(&ncr_we, addr, b);
}
static void REGPARAM2 we_wput (uaecptr addr, uae_u32 b)
{
	ncr_wput(&ncr_we, addr, b);
}
static void REGPARAM2 we_lput (uaecptr addr, uae_u32 b)
{
	ncr_lput(&ncr_we, addr, b);
}
static uae_u32 REGPARAM2 we_bget (uaecptr addr)
{
	return ncr_bget(&ncr_we, addr);
}
static uae_u32 REGPARAM2 we_wget (uaecptr addr)
{
	return ncr_wget(&ncr_we, addr);
}
static uae_u32 REGPARAM2 we_lget (uaecptr addr)
{
	return ncr_lget(&ncr_we, addr);
}

static void REGPARAM2 cs_bput(uaecptr addr, uae_u32 b)
{
	ncr_bput(&ncr_cs, addr, b);
}
static void REGPARAM2 cs_wput(uaecptr addr, uae_u32 b)
{
	ncr_wput(&ncr_cs, addr, b);
}
static void REGPARAM2 cs_lput(uaecptr addr, uae_u32 b)
{
	ncr_lput(&ncr_cs, addr, b);
}
static uae_u32 REGPARAM2 cs_bget(uaecptr addr)
{
	return ncr_bget(&ncr_cs, addr);
}
static uae_u32 REGPARAM2 cs_wget(uaecptr addr)
{
	return ncr_wget(&ncr_cs, addr);
}
static uae_u32 REGPARAM2 cs_lget(uaecptr addr)
{
	return ncr_lget(&ncr_cs, addr);
}


static void REGPARAM2 bppc_bput(uaecptr addr, uae_u32 b)
{
	ncr_bput(&ncr_bppc, addr, b);
}
static void REGPARAM2 bppc_wput(uaecptr addr, uae_u32 b)
{
	ncr_wput(&ncr_bppc, addr, b);
}
static void REGPARAM2 bppc_lput(uaecptr addr, uae_u32 b)
{
	ncr_lput(&ncr_bppc, addr, b);
}
static uae_u32 REGPARAM2 bppc_bget(uaecptr addr)
{
	return ncr_bget(&ncr_bppc, addr);
}
static uae_u32 REGPARAM2 bppc_wget(uaecptr addr)
{
	return ncr_wget(&ncr_bppc, addr);
}
static uae_u32 REGPARAM2 bppc_lget(uaecptr addr)
{
	return ncr_lget(&ncr_bppc, addr);
}

static addrbank ncr_bank_warpengine = {
	we_lget, we_wget, we_bget,
	we_lput, we_wput, we_bput,
	default_xlate, default_check, NULL, NULL, _T("Warp Engine SCSI"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO | ABFLAG_THREADSAFE
};

static addrbank ncr_bank_cs_scsi_ram = {
	cs_lget, cs_wget, cs_bget,
	cs_lput, cs_wput, cs_bput,
	cyberstorm_scsi_ram_xlate, cyberstorm_scsi_ram_check, NULL, NULL, _T("CyberStorm SCSI RAM"),
	cs_lget, cs_wget, ABFLAG_IO | ABFLAG_THREADSAFE
};
static addrbank ncr_bank_cs_scsi_io = {
	cs_lget, cs_wget, cs_bget,
	cs_lput, cs_wput, cs_bput,
	default_xlate, default_check, NULL, NULL, _T("CyberStorm SCSI IO"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO | ABFLAG_THREADSAFE
};

static struct addrbank_sub ncr_sub_bank_cs[] = {
	{ &ncr_bank_cs_scsi_io,  0x0000, 0x0000 },
	{ &ncr_bank_cs_scsi_ram, 0x1000, 0x0000 },
	{ &ncr_bank_cs_scsi_ram, 0x3000, 0x2000 },
	{ &ncr_bank_cs_scsi_ram, 0x5000, 0x4000 },
	{ &ncr_bank_cs_scsi_ram, 0x7000, 0x6000 },
	{ &ncr_bank_cs_scsi_ram, 0x9000, 0x8000 },
	{ &ncr_bank_cs_scsi_ram, 0xb000, 0xa000 },
	{ &ncr_bank_cs_scsi_ram, 0xd000, 0xc000 },
	{ &ncr_bank_cs_scsi_ram, 0xf000, 0xe000 },
	{ NULL }
};

addrbank ncr_bank_cyberstorm = {
	sub_bank_lget, sub_bank_wget, sub_bank_bget,
	sub_bank_lput, sub_bank_wput, sub_bank_bput,
	sub_bank_xlate, sub_bank_check, NULL, NULL, _T("CyberStorm SCSI"),
	sub_bank_lgeti, sub_bank_wgeti, ABFLAG_IO | ABFLAG_THREADSAFE, ncr_sub_bank_cs
};

addrbank ncr_bank_blizzardppc = {
	bppc_lget, bppc_wget, bppc_bget,
	bppc_lput, bppc_wput, bppc_bput,
	default_xlate, default_check, NULL, NULL, _T("Blizzard PPC SCSI"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO | ABFLAG_THREADSAFE
};



static void ew (struct ncr_state *ncr, int addr, uae_u8 value)
{
	if (addr == 00 || addr == 02 || addr == 0x40 || addr == 0x42) {
		ncr->acmemory[addr] = (value & 0xf0);
		ncr->acmemory[addr + 2] = (value & 0x0f) << 4;
	} else {
		ncr->acmemory[addr] = ~(value & 0xf0);
		ncr->acmemory[addr + 2] = ~((value & 0x0f) << 4);
	}
}

void ncr_init(void)
{
	ncr_cs.newncr = true;
	if (!ncr_cs.devobject.lsistate)
		lsi_scsi_init(&ncr_cs.devobject);
	ncr_cs.ramsize = CYBERSTORM_SCSI_RAM_SIZE;
}

void ncr710_init (void)
{
	for (int i = 0; ncrs[i]; i++) {
		ncrs[i]->newncr = false;
		if (!ncrs[i]->devobject.lsistate)
			lsi710_scsi_init (&ncrs[i]->devobject);
	}
}

static void ncr_reset_board(struct ncr_state *ncr)
{
	ncr->configured = 0;
	ncr->board_mask = 0xffff;
	ncr->irq = false;
	if (ncr->devobject.lsistate)
		lsi_scsi_reset(&ncr->devobject, ncr);
	ncr->bank = &ncr_bank_cyberstorm;
	ncr->irq_func = cyberstorm_irq;
}

static void ncr710_reset_board (struct ncr_state *ncr)
{
	ncr->configured = 0;
	ncr->board_mask = 0xffff;
	ncr->irq = false;
	ncr->irq_func = set_irq2;
	if (ncr->devobject.lsistate)
		lsi710_scsi_reset (&ncr->devobject, ncr);
	if (ncr == &ncr_a4000t) {
		ncr->name = _T("A4000T NCR SCSI");
		ncr->bank = NULL;
	}
	if (ncr == &ncr_a4091) {
		ncr->name = _T("A4091 SCSI");
		ncr->bank = &ncr_bank_a4091;
	}
	if (ncr == &ncr_a4091_2) {
		ncr->name = _T("A4091 SCSI #2");
		ncr->bank = &ncr_bank_a4091_2;
	}
	if (ncr == &ncr_we) {
		ncr->name = _T("Warp Engine SCSI");
		ncr->bank = &ncr_bank_warpengine;
	}
	if (ncr == &ncr_bppc) {
		ncr->name = _T("Blizzard PPC SCSI");
		ncr->bank = &ncr_bank_blizzardppc;
		ncr->irq_func = blizzardppc_irq;
	}
}

static const uae_u8 warpengine_a4000_autoconfig[16] = {
	0x90, 0x13, 0x75, 0x00, 0x08, 0x9b, 0x00, 0x19, 0x02, 0x0e, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00
};
#define WARP_ENGINE_ROM_SIZE 32768

addrbank *ncr710_warpengine_autoconfig_init(void)
{
	int roms[2];
	struct ncr_state *ncr = &ncr_we;
	struct zfile *z = NULL;

	xfree(ncr->rom);
	ncr->rom = NULL;

	roms[0] = 93;
	roms[1] = -1;

	ncr->enabled = true;
	memset (ncr->acmemory, 0xff, sizeof ncr->acmemory);
	ncr->rom_start = 0x10;
	ncr->rom_offset = 0;
	ncr->rom_end = WARP_ENGINE_ROM_SIZE * 4;
	ncr->io_start = WARP_ENGINE_IO_OFFSET;
	ncr->io_end = WARP_ENGINE_IO_END;

	struct romlist *rl = getromlistbyids(roms, NULL);
	if (rl) {
		struct romdata *rd = rl->rd;
		z = read_rom (rd);
	}
	for (int i = 0; i < 16; i++) {
		uae_u8 b = warpengine_a4000_autoconfig[i];
		ew(ncr, i * 4, b);
	}
	ncr->rom = xcalloc (uae_u8, WARP_ENGINE_ROM_SIZE * 4);
	if (z) {
		for (int i = 0; i < WARP_ENGINE_ROM_SIZE; i++) {
			uae_u8 b = 0xff;
			zfile_fread(&b, 1, 1, z);
			ncr->rom[i * 4 + 0] = b | 0x0f;
			ncr->rom[i * 4 + 1] = 0xff;
			ncr->rom[i * 4 + 2] = (b << 4) | 0x0f;
			ncr->rom[i * 4 + 3] = 0xff;
		}
		zfile_fclose(z);
	} else {
		romwarning (roms);
	}

	ncr710_init ();
	ncr710_reset_board(ncr);

	return &ncr_bank_warpengine;
}

addrbank *ncr710_a4091_autoconfig_init (int devnum)
{
	struct ncr_state *ncr = ncra4091[devnum];
	int roms[3];
	const TCHAR *romname;

	xfree(ncr->rom);
	ncr->rom = NULL;

	if (!ncr->enabled && devnum > 0)
		return &expamem_null;

	roms[0] = 58;
	roms[1] = 57;
	roms[2] = -1;

	ncr->enabled = true;
	memset (ncr->acmemory, 0xff, sizeof ncr->acmemory);
	ncr->rom_start = 0;
	ncr->rom_offset = A4091_ROM_OFFSET;
	ncr->rom_end = A4091_IO_OFFSET;
	ncr->io_start = A4091_IO_OFFSET;
	ncr->io_end = A4091_IO_END;

	ncr710_init ();
	ncr710_reset_board(ncr);

	romname = devnum && currprefs.a4091rom.roms[1].romfile[0] ? currprefs.a4091rom.roms[1].romfile : currprefs.a4091rom.roms[0].romfile;
	struct zfile *z = read_rom_name (romname);
	if (!z) {
		struct romlist *rl = getromlistbyids(roms, romname);
		if (rl) {
			struct romdata *rd = rl->rd;
			z = read_rom (rd);
		}
	}
	if (z) {
		write_log (_T("%s BOOT ROM '%s'\n"), ncr->name, zfile_getname (z));
		ncr->rom = xmalloc (uae_u8, A4091_ROM_SIZE * 4);
		for (int i = 0; i < A4091_ROM_SIZE; i++) {
			uae_u8 b;
			zfile_fread (&b, 1, 1, z);
			ncr->rom[i * 4 + 0] = b | 0x0f;
			ncr->rom[i * 4 + 2] = (b << 4) | 0x0f;
			if (i < 0x20) {
				ncr->acmemory[i * 4 + 0] = b;
			} else if (i >= 0x40 && i < 0x60) {
				ncr->acmemory[(i - 0x40) * 4 + 2] = b;
			}
		}
		zfile_fclose(z);
	} else {
		romwarning (roms);
	}

	return ncr == &ncr_a4091 ? &ncr_bank_a4091 : &ncr_bank_a4091_2;
}

static void freescsi_hdf (struct scsi_data *sd)
{
	if (!sd)
		return;
	hdf_hd_close (sd->hfd);
	scsi_free (sd);
}

static void freescsi (SCSIDevice *scsi)
{
	if (scsi) {
		freescsi_hdf ((struct scsi_data*)scsi->handle);
		xfree (scsi);
	}
}

static void ncr_free2(struct ncr_state *ncr)
{
	for (int ch = 0; ch < 8; ch++) {
		freescsi (ncr->scsid[ch]);
		ncr->scsid[ch] = NULL;
	}
}

void ncr710_free(void)
{
	for (int i = 0; ncrs[i]; i++) {
		ncr_free2(ncrs[i]);
	}
}

void ncr_free(void)
{
	ncr_free2(&ncr_cs);
}

void ncr710_reset(void)
{
	for (int i = 0; ncrs[i]; i++) {
		ncr710_reset_board(ncrs[i]);
	}
	if (currprefs.cs_mbdmac & 2) {
		ncr_a4000t.configured = -1;
		ncr_a4000t.enabled = true;
	}
	if (currprefs.cpuboard_type == BOARD_BLIZZARDPPC) {
		ncr_bppc.configured = -1;
		ncr_bppc.enabled = true;
	}
}

void ncr_reset(void)
{
	ncr_reset_board(&ncr_cs);
	ncr_cs.configured = -1;
	ncr_cs.enabled = true;
}

static int add_ncr_scsi_hd (struct ncr_state *ncr, int ch, struct hd_hardfiledata *hfd, struct uaedev_config_info *ci, int scsi_level)
{
	struct scsi_data *handle;
	freescsi (ncr->scsid[ch]);
	ncr->scsid[ch] = NULL;
	if (!hfd) {
		hfd = xcalloc (struct hd_hardfiledata, 1);
		memcpy (&hfd->hfd.ci, ci, sizeof (struct uaedev_config_info));
	}
	if (!hdf_hd_open (hfd))
		return 0;
	hfd->ansi_version = scsi_level;
	handle = scsi_alloc_hd (ch, hfd);
	if (!handle)
		return 0;
	handle->privdata = ncr;
	ncr->scsid[ch] = xcalloc (SCSIDevice, 1);
	ncr->scsid[ch]->handle = handle;
	ncr->enabled = true;
	return ncr->scsid[ch] ? 1 : 0;
}


static int add_ncr_scsi_cd (struct ncr_state *ncr, int ch, int unitnum)
{
	struct scsi_data *handle;
	device_func_init (0);
	freescsi (ncr->scsid[ch]);
	ncr->scsid[ch] = NULL;
	handle = scsi_alloc_cd (ch, unitnum, false);
	if (!handle)
		return 0;
	handle->privdata = ncr;
	ncr->scsid[ch] = xcalloc (SCSIDevice, 1);
	ncr->scsid[ch]->handle = handle;
	ncr->enabled = true;
	return ncr->scsid[ch] ? 1 : 0;
}

static int add_ncr_scsi_tape (struct ncr_state *ncr, int ch, const TCHAR *tape_directory, bool readonly)
{
	struct scsi_data *handle;
	freescsi (ncr->scsid[ch]);
	ncr->scsid[ch] = NULL;
	handle = scsi_alloc_tape (ch, tape_directory, readonly);
	if (!handle)
		return 0;
	handle->privdata = ncr;
	ncr->scsid[ch] = xcalloc (SCSIDevice, 1);
	ncr->scsid[ch]->handle = handle;
	ncr->enabled = true;
	return ncr->scsid[ch] ? 1 : 0;
}

static int ncr_add_scsi_unit(struct ncr_state *ncr, int ch, struct uaedev_config_info *ci)
{
	if (ci->type == UAEDEV_CD)
		return add_ncr_scsi_cd (ncr, ch, ci->device_emu_unit);
	else if (ci->type == UAEDEV_TAPE)
		return add_ncr_scsi_tape (ncr, ch, ci->rootdir, ci->readonly);
	else
		return add_ncr_scsi_hd (ncr, ch, NULL, ci, 1);
}

int a4000t_add_scsi_unit (int ch, struct uaedev_config_info *ci)
{
	return ncr_add_scsi_unit(&ncr_a4000t, ch, ci);
}

int warpengine_add_scsi_unit (int ch, struct uaedev_config_info *ci)
{
	return ncr_add_scsi_unit(&ncr_we, ch, ci);
}

int tekmagic_add_scsi_unit (int ch, struct uaedev_config_info *ci)
{
	return ncr_add_scsi_unit(&ncr_cpuboard, ch, ci);
}

int a4091_add_scsi_unit (int ch, struct uaedev_config_info *ci, int devnum)
{
	return ncr_add_scsi_unit(ncra4091[devnum], ch, ci);
}

int cyberstorm_add_scsi_unit(int ch, struct uaedev_config_info *ci)
{
	return ncr_add_scsi_unit(&ncr_cs, ch, ci);
}

int blizzardppc_add_scsi_unit(int ch, struct uaedev_config_info *ci)
{
	return ncr_add_scsi_unit(&ncr_bppc, ch, ci);
}

#endif
