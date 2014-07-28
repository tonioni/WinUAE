/*
* UAE - The Un*x Amiga Emulator
*
* NCR 53C9x
*
* (c) 2014 Toni Wilen
*/

#include "sysconfig.h"
#include "sysdeps.h"

#ifdef NCR9X

#define NCR_DEBUG 0

#include "options.h"
#include "uae.h"
#include "memory.h"
#include "rommgr.h"
#include "custom.h"
#include "newcpu.h"
#include "ncr9x_scsi.h"
#include "scsi.h"
#include "filesys.h"
#include "zfile.h"
#include "blkdev.h"
#include "cpuboard.h"
#include "qemuvga\qemuuaeglue.h"
#include "qemuvga\queue.h"
#include "qemuvga\scsi\scsi.h"
#include "qemuvga\scsi\esp.h"

struct ncr9x_state
{
	TCHAR *name;
	DeviceState devobject;
	SCSIDevice *scsid[8];
	SCSIBus scsibus;
	uae_u32 board_mask;
	uae_u8 *rom;
	uae_u8 acmemory[128];
	int configured;
	bool enabled;
	int rom_start, rom_end, rom_offset;
	int io_start, io_end;
	addrbank *bank;
	bool irq;
	void (*irq_func)(int);
	int led;
	uaecptr dma_ptr;
	int dma_cnt;
};



/*
	Blizzard SCSI Kit IV:

	scsi: 0x8000
	dma: 0x10000

	pa >>= 1;
	if (!bsc->sc_datain)
		pa |= 0x80000000;
	bsc->sc_dmabase[0x8000] = (u_int8_t)(pa >> 24);
	bsc->sc_dmabase[0] = (u_int8_t)(pa >> 24);
	bsc->sc_dmabase[0] = (u_int8_t)(pa >> 16);
	bsc->sc_dmabase[0] = (u_int8_t)(pa >> 8);
	bsc->sc_dmabase[0] = (u_int8_t)(pa);

	Blizzard 2060:

	scsi: 0x1ff00
	dma: 0x1fff0

	bsc->sc_reg[0xe0] = BZTZSC_PB_LED;	LED

	pa >>= 1;
	if (!bsc->sc_datain)
		pa |= 0x80000000;
	bsc->sc_dmabase[12] = (u_int8_t)(pa);
	bsc->sc_dmabase[8] = (u_int8_t)(pa >> 8);
	bsc->sc_dmabase[4] = (u_int8_t)(pa >> 16);
	bsc->sc_dmabase[0] = (u_int8_t)(pa >> 24);
	
*/

static struct ncr9x_state blizzard_scsi;


static struct ncr9x_state *ncrs[] =
{
	&blizzard_scsi,
	NULL
};

static void set_irq2(int level)
{
	if (level)
		INTREQ(0x8000 | 0x0008);
}

void ncr9x_rethink(void)
{
	for (int i = 0; ncrs[i]; i++) {
		if (ncrs[i]->irq)
			INTREQ(0x8000 | 0x0008);
	}
}

void esp_irq_raise(qemu_irq irq)
{
	struct ncr9x_state *ncr = (struct ncr9x_state*)irq;
	ncr->irq = true;
	ncr->irq_func(ncr->irq);
}
void esp_irq_lower(qemu_irq irq)
{
	struct ncr9x_state *ncr = (struct ncr9x_state*)irq;
	ncr->irq = false;
	ncr->irq_func(ncr->irq);
}

static void cyberstorm_mk1_mk2_dma_read(void *opaque, uint8_t *buf, int len)
{
	struct ncr9x_state *ncr = (struct ncr9x_state*)opaque;
	while (len > 0) {
		uae_u16 v = get_word(ncr->dma_ptr & ~1);
		*buf++ = v >> 8;
		len--;
		if (len > 0) {
			*buf++ = v;
			len--;
		}
		ncr->dma_ptr += 2;
	}
}
static void cyberstorm_mk1_mk2_dma_write(void *opaque, uint8_t *buf, int len)
{
	struct ncr9x_state *ncr = (struct ncr9x_state*)opaque;
	while (len > 0) {
		uae_u16 v;
		v = *buf++;
		len--;
		v <<= 8;
		if (len > 0) {
			v |= *buf++;
			len--;
		}
		put_word(ncr->dma_ptr & ~1, v);
		ncr->dma_ptr += 2;
	}
}

static void blizzard_dma_read(void *opaque, uint8_t *buf, int len)
{
	struct ncr9x_state *ncr = (struct ncr9x_state*)opaque;
	while (len > 0) {
		uae_u16 v = get_word((ncr->dma_ptr & 0x7fffffff) * 2);
		*buf++ = v >> 8;
		len--;
		if (len > 0) {
			*buf++ = v;
			len--;
		}
		ncr->dma_ptr++;
	}
}
static void blizzard_dma_write(void *opaque, uint8_t *buf, int len)
{
	struct ncr9x_state *ncr = (struct ncr9x_state*)opaque;
	while (len > 0) {
		uae_u16 v;
		v = *buf++;
		len--;
		v <<= 8;
		if (len > 0) {
			v |= *buf++;
			len--;
		}
		put_word((ncr->dma_ptr & 0x7fffffff) * 2, v);
		ncr->dma_ptr++;
	}
}

static int get_scb_len(uae_u8 cmd)
{
	if (cmd <= 0x1f)
		return 6;
	if (cmd >= 0x20 && cmd <= 0x5f)
		return 10;
	if (cmd >= 0x80 && cmd <= 0x9f)
		return 16;
	if (cmd >= 0xa0 && cmd <= 0xbf)
		return 12;
	return 0;
}

void scsiesp_req_continue(SCSIRequest *req)
{
	struct scsi_data *sd = (struct scsi_data*)req->dev->handle;
	if (sd->data_len < 0) {
		esp_command_complete(req, sd->status, 0);
	}
	else if (sd->data_len) {
		esp_transfer_data(req, sd->data_len);
	} else {
		if (sd->direction > 0)
			scsi_emulate_cmd(sd);
		esp_command_complete(req, sd->status, 0);
	}
}
SCSIRequest *scsiesp_req_new(SCSIDevice *d, uint32_t tag, uint32_t lun, uint8_t *buf, void *hba_private)
{
	SCSIRequest *req = xcalloc(SCSIRequest, 1);
	struct scsi_data *sd = (struct scsi_data*)d->handle;
	struct ncr9x_state *ncr = (struct ncr9x_state*)sd->privdata;
	int len = get_scb_len(buf[0]);

	req->dev = d;
	req->hba_private = hba_private;
	req->bus = &ncr->scsibus;
	req->bus->qbus.parent = &ncr->devobject;

	memcpy(sd->cmd, buf, len);
	sd->cmd_len = len;
	return req;
}
int32_t scsiesp_req_enqueue(SCSIRequest *req)
{
	struct scsi_data *sd = (struct scsi_data*)req->dev->handle;

	sd->data_len = 0;
	scsi_start_transfer(sd);
	scsi_emulate_analyze(sd);
	//write_log (_T("%02x.%02x.%02x.%02x.%02x.%02x\n"), sd->cmd[0], sd->cmd[1], sd->cmd[2], sd->cmd[3], sd->cmd[4], sd->cmd[5]);

	if (sd->direction <= 0)
		scsi_emulate_cmd(sd);
	if (sd->direction == 0)
		return 1;
	return sd->data_len;
}
void scsiesp_req_unref(SCSIRequest *req)
{
	xfree(req);
}
uint8_t *scsiesp_req_get_buf(SCSIRequest *req)
{
	struct scsi_data *sd = (struct scsi_data*)req->dev->handle;
	sd->data_len = 0;
	return sd->buffer;
}
SCSIDevice *scsiesp_device_find(SCSIBus *bus, int channel, int target, int lun)
{
	struct ncr9x_state *ncr = (struct ncr9x_state*)bus->privdata;
	if (lun != 0 || target < 0 || target >= 8)
		return NULL;
	return ncr->scsid[target];
}
void scsiesp_req_cancel(SCSIRequest *req)
{
	write_log(_T("scsi_req_cancel\n"));
}

#define IO_MASK 0xff

static uaecptr beswap(uaecptr addr)
{
	return (addr & ~3) | (3 - (addr & 3));
}

static void ncr9x_io_bput(struct ncr9x_state *ncr, uaecptr addr, uae_u32 val)
{
	addr &= ncr->board_mask;
	if (currprefs.cpuboard_type == BOARD_BLIZZARD_2060) {
		if (addr >= BLIZZARD_2060_DMA_OFFSET) {
			//write_log (_T("Blizzard DMA PUT %08x %02X\n"), addr, (uae_u8)val);
			addr &= 0xf;
			addr >>= 2;
			addr = 3 - addr;

			ncr->dma_ptr &= ~(0xff << (addr * 8));
			ncr->dma_ptr |= (val & 0xff) << (addr * 8);
			if (addr == 3)
				esp_dma_enable(ncr->devobject.lsistate, 1);
			return;
		} else if (addr >= BLIZZARD_2060_LED_OFFSET) {
			ncr->led = val;
			return;
		}
	} else if (currprefs.cpuboard_type == BOARD_BLIZZARD_1230_IV_SCSI || currprefs.cpuboard_type == BOARD_BLIZZARD_1260_SCSI) {
		if (addr >= BLIZZARD_SCSI_KIT_DMA_OFFSET) {
			addr &= 0x18000;
			if (addr == 0x18000) {
				ncr->dma_ptr = 0;
				ncr->dma_cnt = 4;
			} else {
				ncr->dma_ptr <<= 8;
				ncr->dma_ptr |= (uae_u8)val;
				ncr->dma_cnt--;
				if (ncr->dma_cnt == 0)
					esp_dma_enable(ncr->devobject.lsistate, 1);
			}
			//write_log(_T("Blizzard DMA PUT %08x %02X\n"), addr, (uae_u8)val);
			return;
		}
	} else if (currprefs.cpuboard_type == BOARD_CSMK1) {
		if (addr >= CYBERSTORM_MK1_JUMPER_OFFSET) {
			if (addr == CYBERSTORM_MK1_JUMPER_OFFSET)
				esp_dma_enable(ncr->devobject.lsistate, 1);
		} else if (addr >= CYBERSTORM_MK1_DMA_OFFSET) {
			addr &= 7;
			addr >>= 1;
			addr = 3 - addr;
			ncr->dma_ptr &= ~(0xff << (addr * 8));
			ncr->dma_ptr |= (val & 0xff) << (addr * 8);
			return;
		} else if (addr >= CYBERSTORM_MK2_LED_OFFSET) {
			ncr->led = val;
			return;
		}
	} else if (currprefs.cpuboard_type == BOARD_CSMK2) {
		if (addr >= CYBERSTORM_MK2_DMA_OFFSET) {
			addr &= 0xf;
			addr >>= 2;
			addr = 3 - addr;
			ncr->dma_ptr &= ~(0xff << (addr * 8));
			ncr->dma_ptr |= (val & 0xff) << (addr * 8);
			if (addr == 0)
				esp_dma_enable(ncr->devobject.lsistate, 1);
			return;
		 } else if (addr >= CYBERSTORM_MK2_LED_OFFSET) {
			 ncr->led = val;
			 return;
		 }
	}
	addr &= IO_MASK;
	addr >>= 2;
	esp_reg_write(ncr->devobject.lsistate, (addr), val);
}
uae_u32 ncr9x_io_bget(struct ncr9x_state *ncr, uaecptr addr)
{
	addr &= ncr->board_mask;
	if (currprefs.cpuboard_type == BOARD_BLIZZARD_2060) {
		if (addr >= BLIZZARD_2060_DMA_OFFSET) {
			write_log(_T("Blizzard DMA GET %08x\n"), addr);
			return 0;
		} else if (addr >= BLIZZARD_2060_LED_OFFSET) {
			return ncr->led;
		}
	} else if (currprefs.cpuboard_type == BOARD_BLIZZARD_1230_IV_SCSI || currprefs.cpuboard_type == BOARD_BLIZZARD_1260_SCSI) {
		if (addr >= BLIZZARD_SCSI_KIT_DMA_OFFSET)
			return 0;
	} else if (currprefs.cpuboard_type == BOARD_CSMK1) {
		if (addr >= CYBERSTORM_MK1_JUMPER_OFFSET) {
			return 0xff;
		} else if (addr >= CYBERSTORM_MK1_DMA_OFFSET) {
			return 0;
		} else if (addr >= CYBERSTORM_MK1_LED_OFFSET) {
			return ncr->led;
		}
	} else if (currprefs.cpuboard_type == BOARD_CSMK2) {
		if (addr >= CYBERSTORM_MK2_DMA_OFFSET) {
			return 0;
		} else if (addr >= CYBERSTORM_MK2_LED_OFFSET) {
			return ncr->led;
		}
	}
	addr &= IO_MASK;
	addr >>= 2;
	return esp_reg_read(ncr->devobject.lsistate, (addr));
}

static uae_u32 ncr9x_bget2(struct ncr9x_state *ncr, uaecptr addr)
{
	uae_u32 v = 0;

	addr &= ncr->board_mask;
	if (ncr->io_end && (addr < ncr->io_start || addr >= ncr->io_end))
		return v;
	return ncr9x_io_bget(ncr, addr);
}
static void ncr9x_bput2(struct ncr9x_state *ncr, uaecptr addr, uae_u32 val)
{
	uae_u32 v = val;
	addr &= ncr->board_mask;
	if (ncr->io_end && (addr < ncr->io_start || addr >= ncr->io_end))
		return;
	ncr9x_io_bput(ncr, addr, val);
}

static uae_u32 REGPARAM2 ncr9x_lget(struct ncr9x_state *ncr, uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= ncr->board_mask;
	v = (ncr9x_bget2(ncr, addr + 3) << 0) | (ncr9x_bget2(ncr, addr + 2) << 8) |
		(ncr9x_bget2(ncr, addr + 1) << 16) | (ncr9x_bget2(ncr, addr + 0) << 24);
	return v;
}

static uae_u32 REGPARAM2 ncr9x_wget(struct ncr9x_state *ncr, uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= ncr->board_mask;
	v = (ncr9x_bget2(ncr, addr) << 8) | ncr9x_bget2(ncr, addr + 1);
	return v;
}

static uae_u32 REGPARAM2 ncr9x_bget(struct ncr9x_state *ncr, uaecptr addr)
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
	v = ncr9x_bget2(ncr, addr);
	return v;
}


static void REGPARAM2 ncr9x_lput(struct ncr9x_state *ncr, uaecptr addr, uae_u32 l)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= ncr->board_mask;
	ncr9x_bput2(ncr, addr + 3, l >> 0);
	ncr9x_bput2(ncr, addr + 2, l >> 8);
	ncr9x_bput2(ncr, addr + 1, l >> 16);
	ncr9x_bput2(ncr, addr + 0, l >> 24);
}


static void REGPARAM2 ncr9x_wput(struct ncr9x_state *ncr, uaecptr addr, uae_u32 w)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	w &= 0xffff;
	addr &= ncr->board_mask;
	if (!ncr->configured)
		return;
	ncr9x_bput2(ncr, addr, w >> 8);
	ncr9x_bput2(ncr, addr + 1, w);
}

static void REGPARAM2 ncr9x_bput(struct ncr9x_state *ncr, uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	b &= 0xff;
	addr &= ncr->board_mask;
	if (!ncr->configured) {
		return;
	}
	ncr9x_bput2(ncr, addr, b);
}

static void REGPARAM2 bncr9x_bput(uaecptr addr, uae_u32 b)
{
	ncr9x_bput(&blizzard_scsi, addr, b);
}
static void REGPARAM2 bncr9x_wput(uaecptr addr, uae_u32 b)
{
	ncr9x_wput(&blizzard_scsi, addr, b);
}
static void REGPARAM2 bncr9x_lput(uaecptr addr, uae_u32 b)
{
	ncr9x_lput(&blizzard_scsi, addr, b);
}
static uae_u32 REGPARAM2 bncr9x_bget(uaecptr addr)
{
	return ncr9x_bget(&blizzard_scsi, addr);
}
static uae_u32 REGPARAM2 bncr9x_wget(uaecptr addr)
{
	return ncr9x_wget(&blizzard_scsi, addr);
}
static uae_u32 REGPARAM2 bncr9x_lget(uaecptr addr)
{
	return ncr9x_lget(&blizzard_scsi, addr);
}

static addrbank ncr9x_bank_blizzard = {
	bncr9x_lget, bncr9x_wget, bncr9x_bget,
	bncr9x_lput, bncr9x_wput, bncr9x_bput,
	default_xlate, default_check, NULL, _T("53C94/FAS216"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO
};

uae_u32 cpuboard_ncr9x_scsi_get(uaecptr addr)
{
	return ncr9x_io_bget(&blizzard_scsi, addr);
}
void cpuboard_ncr9x_scsi_put(uaecptr addr, uae_u32 v)
{
	ncr9x_io_bput(&blizzard_scsi, addr, v);
}

static void ew(struct ncr9x_state *ncr, int addr, uae_u8 value)
{
	if (addr == 00 || addr == 02 || addr == 0x40 || addr == 0x42) {
		ncr->acmemory[addr] = (value & 0xf0);
		ncr->acmemory[addr + 2] = (value & 0x0f) << 4;
	}
	else {
		ncr->acmemory[addr] = ~(value & 0xf0);
		ncr->acmemory[addr + 2] = ~((value & 0x0f) << 4);
	}
}


static void freescsi_hdf(struct scsi_data *sd)
{
	if (!sd)
		return;
	hdf_hd_close(sd->hfd);
	scsi_free(sd);
}

static void freescsi(SCSIDevice *scsi)
{
	if (scsi) {
		freescsi_hdf((struct scsi_data*)scsi->handle);
		xfree(scsi);
	}
}

static void ncr9x_free2(struct ncr9x_state *ncr)
{
	for (int ch = 0; ch < 8; ch++) {
		freescsi(ncr->scsid[ch]);
		ncr->scsid[ch] = NULL;
	}
}

void ncr9x_free(void)
{
	ncr9x_free2(&blizzard_scsi);
}

void ncr9x_init(void)
{
	if (!blizzard_scsi.devobject.lsistate) {
		if (currprefs.cpuboard_type == BOARD_CSMK2 || currprefs.cpuboard_type == BOARD_CSMK1)
			esp_scsi_init(&blizzard_scsi.devobject, cyberstorm_mk1_mk2_dma_read, cyberstorm_mk1_mk2_dma_write);
		else
			esp_scsi_init(&blizzard_scsi.devobject, blizzard_dma_read, blizzard_dma_write);
	}
}

static void ncr9x_reset_board(struct ncr9x_state *ncr)
{
	ncr->configured = 0;
	if (currprefs.cpuboard_type == BOARD_CSMK1)
		ncr->board_mask = 0xffff;
	else
		ncr->board_mask = 0x1ffff;
	ncr->irq = false;
	if (ncr->devobject.lsistate)
		esp_scsi_reset(&ncr->devobject, ncr);
	ncr->bank = &ncr9x_bank_blizzard;
	ncr->name = ncr->bank->name;
	ncr->irq_func = set_irq2;
}

void ncr9x_reset(void)
{
	ncr9x_reset_board(&blizzard_scsi);
	blizzard_scsi.configured = -1;
	blizzard_scsi.enabled = true;
}


static int add_ncr_scsi_hd(struct ncr9x_state *ncr, int ch, struct hd_hardfiledata *hfd, struct uaedev_config_info *ci, int scsi_level)
{
	struct scsi_data *handle;
	freescsi(ncr->scsid[ch]);
	ncr->scsid[ch] = NULL;
	if (!hfd) {
		hfd = xcalloc(struct hd_hardfiledata, 1);
		memcpy(&hfd->hfd.ci, ci, sizeof(struct uaedev_config_info));
	}
	if (!hdf_hd_open(hfd))
		return 0;
	hfd->ansi_version = scsi_level;
	handle = scsi_alloc_hd(ch, hfd);
	if (!handle)
		return 0;
	handle->privdata = ncr;
	ncr->scsid[ch] = xcalloc(SCSIDevice, 1);
	ncr->scsid[ch]->handle = handle;
	ncr->enabled = true;
	return ncr->scsid[ch] ? 1 : 0;
}


static int add_ncr_scsi_cd(struct ncr9x_state *ncr, int ch, int unitnum)
{
	struct scsi_data *handle;
	device_func_init(0);
	freescsi(ncr->scsid[ch]);
	ncr->scsid[ch] = NULL;
	handle = scsi_alloc_cd(ch, unitnum, false);
	if (!handle)
		return 0;
	handle->privdata = ncr;
	ncr->scsid[ch] = xcalloc(SCSIDevice, 1);
	ncr->scsid[ch]->handle = handle;
	ncr->enabled = true;
	return ncr->scsid[ch] ? 1 : 0;
}

static int add_ncr_scsi_tape(struct ncr9x_state *ncr, int ch, const TCHAR *tape_directory, bool readonly)
{
	struct scsi_data *handle;
	freescsi(ncr->scsid[ch]);
	ncr->scsid[ch] = NULL;
	handle = scsi_alloc_tape(ch, tape_directory, readonly);
	if (!handle)
		return 0;
	handle->privdata = ncr;
	ncr->scsid[ch] = xcalloc(SCSIDevice, 1);
	ncr->scsid[ch]->handle = handle;
	ncr->enabled = true;
	return ncr->scsid[ch] ? 1 : 0;
}

int cpuboard_ncr9x_add_scsi_unit(int ch, struct uaedev_config_info *ci)
{
	if (ci->type == UAEDEV_CD)
		return add_ncr_scsi_cd(&blizzard_scsi, ch, ci->device_emu_unit);
	else if (ci->type == UAEDEV_TAPE)
		return add_ncr_scsi_tape(&blizzard_scsi, ch, ci->rootdir, ci->readonly);
	else
		return add_ncr_scsi_hd(&blizzard_scsi, ch, NULL, ci, 1);
}

#endif
