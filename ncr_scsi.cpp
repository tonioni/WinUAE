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

#define NCR_DEBUG 2

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
#include "autoconf.h"
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
	DeviceState devobject;
	SCSIDevice *scsid[8];
	SCSIBus scsibus;
	uae_u32 board_mask;
	uae_u8 *rom;
	int ramsize;
	uae_u8 acmemory[128];
	uae_u32 expamem_hi;
	uae_u32 expamem_lo;
	uaecptr baseaddress;
	int configured;
	bool enabled;
	int rom_start, rom_end, rom_offset;
	int io_start, io_end;
	addrbank *bank;
	bool irq;
	void (*irq_func)(int);
	struct romconfig *rc;
	struct ncr_state **self_ptr;
};

#define MAX_NCR_UNITS 10
static struct ncr_state *ncr_units[MAX_NCR_UNITS + 1];

static void freescsi (SCSIDevice *scsi)
{
	if (scsi) {
		free_scsi((struct scsi_data*)scsi->handle);
		xfree (scsi);
	}
}

static void freencrunit(struct ncr_state *ncr)
{
	if (!ncr)
		return;
	for (int i = 0; i < MAX_NCR_UNITS; i++) {
		if (ncr_units[i] == ncr) {
			ncr_units[i] = NULL;
		}
	}
	for (int ch = 0; ch < 8; ch++) {
		freescsi (ncr->scsid[ch]);
		ncr->scsid[ch] = NULL;
	}
	xfree(ncr->rom);
	if (ncr->self_ptr)
		*ncr->self_ptr = NULL;
	xfree(ncr);
}

static struct ncr_state *allocscsi(struct ncr_state **ncr, struct romconfig *rc, int ch)
{
	struct ncr_state *scsi;

	if (ch < 0) {
		freencrunit(*ncr);
		*ncr = NULL;
	}
	if ((*ncr) == NULL) {
		scsi = xcalloc(struct ncr_state, 1);
		for (int i = 0; i < MAX_NCR_UNITS; i++) {
			if (ncr_units[i] == NULL) {
				ncr_units[i] = scsi;
				if (rc)
					rc->unitdata = scsi;
				scsi->rc = rc;
				scsi->self_ptr = ncr;
				*ncr = scsi;
				return scsi;
			}
		}
	}
	return *ncr;
}

static struct ncr_state *getscsi(struct romconfig *rc)
{
	for (int i = 0; i < MAX_NCR_UNITS; i++) {
		if (ncr_units[i]) {
			struct ncr_state *ncr = ncr_units[i];
			if (ncr->rc == rc)
				return ncr;
		}
	}
	return NULL;
}

static struct ncr_state *getscsiboard(uaecptr addr)
{
	for (int i = 0; ncr_units[i]; i++) {
		if (!ncr_units[i]->baseaddress && !ncr_units[i]->configured)
			return ncr_units[i];
		if ((addr & ~ncr_units[i]->board_mask) == ncr_units[i]->baseaddress)
			return ncr_units[i];
	}
	return NULL;
}

static struct ncr_state *ncr_cs;
static struct ncr_state *ncr_bppc;
static struct ncr_state *ncr_cpuboard;
static struct ncr_state *ncr_we;
static struct ncr_state *ncr_a4000t;
static struct ncr_state *ncra4091[MAX_DUPLICATE_EXPANSION_BOARDS];
static struct ncr_state *ncr_wildfire;

static void set_irq2(int level)
{
	if (level)
		INTREQ(0x8000 | 0x0008);
}

void ncr_rethink(void)
{
	for (int i = 0; ncr_units[i]; i++) {
		if (ncr_units[i] != ncr_cs && ncr_units[i]->irq)
			INTREQ_0(0x8000 | 0x0008);
	}
	if (ncr_cs && ncr_cs->irq)
		cyberstorm_mk3_ppc_irq(1);
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
	ncr710_io_bput(ncr_cpuboard, addr, v);
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
	return ncr710_io_bget(ncr_cpuboard, addr);
}

static uae_u32 ncr_bget2 (struct ncr_state *ncr, uaecptr addr)
{
	uae_u32 v = 0;

	addr &= ncr->board_mask;
	if (ncr->rom && addr >= ncr->rom_start && addr < ncr->rom_end)
		return read_rombyte (ncr, addr - ncr->rom_offset);
	if (addr == A4091_DIP_OFFSET) {
		uae_u8 v = 0;
		v |= ncr->rc->device_id;
		v |= ncr->rc->device_settings << 3;
		v ^= 0xff & ~7;
		return v;
	}
	if (ncr->io_end && (addr < ncr->io_start || addr >= ncr->io_end))
		return v;
	if (ncr->newncr)
		return ncr_io_bget(ncr, addr);
	else
		return ncr710_io_bget(ncr, addr);
}

static uae_u32 REGPARAM2 ncr_lget (struct ncr_state *ncr, uaecptr addr)
{
	uae_u32 v = 0;
	if (ncr) {
		addr &= ncr->board_mask;
		if (ncr == ncr_we) {
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
	}
	return v;
}

static uae_u32 REGPARAM2 ncr_wget (struct ncr_state *ncr, uaecptr addr)
{
	uae_u32 v = 0;
	if (ncr) {
		v = (ncr_bget2 (ncr, addr) << 8) | ncr_bget2 (ncr, addr + 1);
	}
	return v;
}

static uae_u32 REGPARAM2 ncr_bget (struct ncr_state *ncr, uaecptr addr)
{
	uae_u32 v = 0;
	if (ncr) {
		addr &= ncr->board_mask;
		if (!ncr->configured) {
			addr &= 65535;
			if (addr >= sizeof ncr->acmemory)
				return 0;
			return ncr->acmemory[addr];
		}
		v = ncr_bget2 (ncr, addr);
	}
	return v;
}

static void REGPARAM2 ncr_lput (struct ncr_state *ncr, uaecptr addr, uae_u32 l)
{
	if (!ncr)
		return;
	addr &= ncr->board_mask;
	if (ncr == ncr_we) {
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

static void REGPARAM2 ncr_wput (struct ncr_state *ncr, uaecptr addr, uae_u32 w)
{
	if (!ncr)
		return;
	w &= 0xffff;
	addr &= ncr->board_mask;
	if (!ncr->configured) {
		addr &= 65535;
		switch (addr)
		{
			case 0x44:
			map_banks_z3(ncr->bank, expamem_z3_pointer >> 16, BOARD_SIZE >> 16);
			ncr->board_mask = 0x00ffffff;
			ncr->baseaddress = expamem_z3_pointer;
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
	if (!ncr)
		return;
	b &= 0xff;
	addr &= ncr->board_mask;
	if (!ncr->configured) {
		addr &= 65535;
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
	ncr710_io_bput(ncr_a4000t, addr, v);
}
uae_u32 ncr710_io_bget_a4000t(uaecptr addr)
{
	return ncr710_io_bget(ncr_a4000t, addr);
}

void ncr815_io_bput_wildfire(uaecptr addr, uae_u32 v)
{
	ncr_io_bput(ncr_wildfire, addr, v);
}
uae_u32 ncr815_io_bget_wildfire(uaecptr addr)
{
	return ncr_io_bget(ncr_wildfire, addr);
}

static void REGPARAM2 ncr_generic_bput (uaecptr addr, uae_u32 b)
{
	struct ncr_state *ncr = getscsiboard(addr);
	if (ncr)
		ncr_bput(ncr, addr, b);
}
static void REGPARAM2 ncr_generic_wput (uaecptr addr, uae_u32 b)
{
	struct ncr_state *ncr = getscsiboard(addr);
	if (ncr)
		ncr_wput(ncr, addr, b);
}
static void REGPARAM2 ncr_generic_lput (uaecptr addr, uae_u32 b)
{
	struct ncr_state *ncr = getscsiboard(addr);
	if (ncr)
		ncr_lput(ncr, addr, b);
}
static uae_u32 REGPARAM2 ncr_generic_bget (uaecptr addr)
{
	struct ncr_state *ncr = getscsiboard(addr);
	if (ncr)
		return ncr_bget(ncr, addr);
	return 0;
}
static uae_u32 REGPARAM2 ncr_generic_wget (uaecptr addr)
{
	struct ncr_state *ncr = getscsiboard(addr);
	if (ncr)
		return ncr_wget(ncr, addr);
	return 0;
}
static uae_u32 REGPARAM2 ncr_generic_lget (uaecptr addr)
{
	struct ncr_state *ncr = getscsiboard(addr);
	if (ncr)
		return ncr_lget(ncr, addr);
	return 0;
}

static void REGPARAM2 cs_bput(uaecptr addr, uae_u32 b)
{
	ncr_bput(ncr_cs, addr, b);
}
static void REGPARAM2 cs_wput(uaecptr addr, uae_u32 b)
{
	ncr_wput(ncr_cs, addr, b);
}
static void REGPARAM2 cs_lput(uaecptr addr, uae_u32 b)
{
	ncr_lput(ncr_cs, addr, b);
}
static uae_u32 REGPARAM2 cs_bget(uaecptr addr)
{
	return ncr_bget(ncr_cs, addr);
}
static uae_u32 REGPARAM2 cs_wget(uaecptr addr)
{
	return ncr_wget(ncr_cs, addr);
}
static uae_u32 REGPARAM2 cs_lget(uaecptr addr)
{
	return ncr_lget(ncr_cs, addr);
}

static addrbank ncr_bank_cs_scsi_ram = {
	cs_lget, cs_wget, cs_bget,
	cs_lput, cs_wput, cs_bput,
	cyberstorm_scsi_ram_xlate, cyberstorm_scsi_ram_check, NULL, NULL, _T("CyberStorm SCSI RAM"),
	cs_lget, cs_wget,
	ABFLAG_IO | ABFLAG_THREADSAFE, S_READ, S_WRITE
};
static addrbank ncr_bank_cs_scsi_io = {
	cs_lget, cs_wget, cs_bget,
	cs_lput, cs_wput, cs_bput,
	default_xlate, default_check, NULL, NULL, _T("CyberStorm SCSI IO"),
	dummy_lgeti, dummy_wgeti,
	ABFLAG_IO | ABFLAG_THREADSAFE, S_READ, S_WRITE
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
	sub_bank_lgeti, sub_bank_wgeti,
	ABFLAG_IO | ABFLAG_THREADSAFE, S_READ, S_WRITE, ncr_sub_bank_cs
};

addrbank ncr_bank_generic = {
	ncr_generic_lget, ncr_generic_wget, ncr_generic_bget,
	ncr_generic_lput, ncr_generic_wput, ncr_generic_bput,
	default_xlate, default_check, NULL, NULL, _T("NCR53C700/800"),
	dummy_lgeti, dummy_wgeti,
	ABFLAG_IO | ABFLAG_THREADSAFE, S_READ, S_WRITE
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

static void ncr_init_board(struct ncr_state *ncr)
{
	if (!ncr)
		return;
	if (!ncr->devobject.lsistate) {
		if (ncr->newncr)
			lsi_scsi_init(&ncr->devobject);
		else
			lsi710_scsi_init (&ncr->devobject);
	}
	if (ncr->newncr)
		lsi_scsi_reset(&ncr->devobject, ncr);
	else
		lsi710_scsi_reset (&ncr->devobject, ncr);
	ncr->board_mask = 0xffff;
	ncr->irq_func = set_irq2;
	ncr->bank = &ncr_bank_generic;
	ncr->configured = 0;
}

static void ncr_reset_board (struct ncr_state *ncr)
{
	if (!ncr)
		return;
	ncr->irq = false;
}

// 01010040
// 01020040 = H
// 01040040 = J
// 01080040 = K

static const uae_u8 warpengine_a4000_autoconfig[16] = {
	0x90, 0x13, 0x75, 0x00, 0x08, 0x9b, 0x00, 0x19, 0x01, 0x0e, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00
};
#define WARP_ENGINE_ROM_SIZE 32768

addrbank *ncr710_warpengine_autoconfig_init(struct romconfig *rc)
{
	struct ncr_state *ncr = getscsi(rc);

	if (!ncr)
		return &expamem_null;

	ncr_we = ncr;
	xfree(ncr->rom);
	ncr->rom = NULL;

	ncr->enabled = true;
	memset (ncr->acmemory, 0xff, sizeof ncr->acmemory);
	ncr->rom_start = 0x10;
	ncr->rom_offset = 0;
	ncr->rom_end = WARP_ENGINE_ROM_SIZE * 4;
	ncr->io_start = WARP_ENGINE_IO_OFFSET;
	ncr->io_end = WARP_ENGINE_IO_END;

	for (int i = 0; i < 16; i++) {
		uae_u8 b = warpengine_a4000_autoconfig[i];
		if (i == 9) {
			b = currprefs.cpuboard_settings & 7;
			if (!b)
				b = 1;
			else
				b <<= 1;
		}
		ew(ncr, i * 4, b);
	}
	ncr->rom = xcalloc (uae_u8, WARP_ENGINE_ROM_SIZE * 4);
	struct zfile *z = read_device_from_romconfig(rc, ROMTYPE_CB_WENGINE);
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
	}

	ncr_reset_board(ncr);

	return &ncr_bank_generic;
}

addrbank *ncr710_a4091_autoconfig_init (struct romconfig *rc)
{
	struct ncr_state *ncr = getscsi(rc);

	if (!ncr)
		return &expamem_null;

	xfree(ncr->rom);
	ncr->rom = NULL;

	ncr->enabled = true;
	memset (ncr->acmemory, 0xff, sizeof ncr->acmemory);
	ncr->rom_start = 0;
	ncr->rom_offset = A4091_ROM_OFFSET;
	ncr->rom_end = A4091_IO_OFFSET;
	ncr->io_start = A4091_IO_OFFSET;
	ncr->io_end = A4091_IO_END;

	ncr_reset_board(ncr);

	struct zfile *z = read_device_from_romconfig(rc, ROMTYPE_A4091);
	if (z) {
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
	}

	return &ncr_bank_generic;
}

void ncr_free(void)
{
	for (int i = 0; i < MAX_NCR_UNITS; i++) {
		freencrunit(ncr_units[i]);
	}
}

void ncr_init(void)
{
}

void ncr_reset(void)
{
	for (int i = 0; i < MAX_NCR_UNITS; i++) {
		ncr_reset_board(ncr_units[i]);
	}
}

static void allocscsidevice(struct ncr_state *ncr, int ch, struct scsi_data *handle)
{
	handle->privdata = ncr;
	ncr->scsid[ch] = xcalloc (SCSIDevice, 1);
	ncr->scsid[ch]->id = ch;
	ncr->scsid[ch]->handle = handle;
}

static void add_ncr_scsi_hd (struct ncr_state *ncr, int ch, struct hd_hardfiledata *hfd, struct uaedev_config_info *ci)
{
	struct scsi_data *handle = NULL;

	freescsi (ncr->scsid[ch]);
	ncr->scsid[ch] = NULL;
	if (!add_scsi_hd(&handle, ch, hfd, ci))
		return;
	allocscsidevice(ncr, ch, handle);
	ncr->enabled = true;
}

static void add_ncr_scsi_cd (struct ncr_state *ncr, int ch, int unitnum)
{
	struct scsi_data *handle = NULL;

	freescsi (ncr->scsid[ch]);
	ncr->scsid[ch] = NULL;
	if (!add_scsi_cd(&handle, ch, unitnum))
		return;
	allocscsidevice(ncr, ch, handle);
	ncr->enabled = true;
}

static void add_ncr_scsi_tape (struct ncr_state *ncr, int ch, const TCHAR *tape_directory, bool readonly)
{
	struct scsi_data *handle = NULL;

	freescsi (ncr->scsid[ch]);
	ncr->scsid[ch] = NULL;
	if (!add_scsi_tape(&handle, ch, tape_directory, readonly))
		return;
	allocscsidevice(ncr, ch, handle);
	ncr->enabled = true;
}

static void ncr_add_scsi_unit(struct ncr_state **ncrp, int ch, struct uaedev_config_info *ci, struct romconfig *rc, bool newncr)
{
	struct ncr_state *ncr = allocscsi(ncrp, rc, ch);
	if (!ncr)
		return;
	ncr->newncr = newncr;
	ncr_init_board(ncr);
	if (ch >= 0 && ncr) {
		if (ci->type == UAEDEV_CD)
			add_ncr_scsi_cd (ncr, ch, ci->device_emu_unit);
		else if (ci->type == UAEDEV_TAPE)
			add_ncr_scsi_tape (ncr, ch, ci->rootdir, ci->readonly);
		else if (ci->type == UAEDEV_HDF)
			add_ncr_scsi_hd (ncr, ch, NULL, ci);
	}
}

void a4000t_add_scsi_unit (int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	ncr_add_scsi_unit(&ncr_a4000t, ch, ci, rc, false);
	ncr_a4000t->configured = -1;
	ncr_a4000t->enabled = true;
}

void warpengine_add_scsi_unit (int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	ncr_add_scsi_unit(&ncr_we, ch, ci, rc, false);
}

void tekmagic_add_scsi_unit (int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	ncr_add_scsi_unit(&ncr_cpuboard, ch, ci, rc, false);
}

void a4091_add_scsi_unit (int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	ncr_add_scsi_unit(&ncra4091[ci->controller_type_unit], ch, ci, rc, false);
}

void cyberstorm_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	ncr_add_scsi_unit(&ncr_cs, ch, ci, rc, true);
	ncr_cs->configured = -1;
	ncr_cs->enabled = true;
	ncr_cs->ramsize = CYBERSTORM_SCSI_RAM_SIZE;
	ncr_cs->irq_func = cyberstorm_mk3_ppc_irq;
	ncr_cs->bank = &ncr_bank_cyberstorm;
	ncr_cs->baseaddress = 0xf40000;
}

void blizzardppc_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	ncr_add_scsi_unit(&ncr_bppc, ch, ci, rc, false);
	ncr_bppc->configured = -1;
	ncr_bppc->enabled = true;
	ncr_bppc->irq_func = blizzardppc_irq;
	ncr_bppc->bank = &ncr_bank_cyberstorm;
	ncr_bppc->baseaddress = 0xf40000;
}

void wildfire_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	ncr_add_scsi_unit(&ncr_wildfire, ch, ci, rc, true);
	ncr_wildfire->configured = -1;
	ncr_wildfire->enabled = true;
	ncr_wildfire->irq_func = wildfire_ncr815_irq;
	ncr_wildfire->bank = &ncr_bank_generic;
}

#endif
