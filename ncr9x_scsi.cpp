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
#include "flashrom.h"
#include "qemuvga/qemuuaeglue.h"
#include "qemuvga/queue.h"
#include "qemuvga/scsi/scsi.h"
#include "qemuvga/scsi/esp.h"
#include "gui.h"

#define FASTLANE_BOARD_SIZE (2 * 16777216)
#define FASTLANE_ROM_SIZE 32768
#define FASTLANE_HARDBITS 0x01000041
#define FASTLANE_RESETDMA 0x01000081

/* NetBSD Fastlane definitions */
#define FLSC_HB_DISABLED	0x01
#define FLSC_HB_BUSID6		0x02
#define FLSC_HB_SEAGATE		0x04
#define FLSC_HB_SLOW		0x08
#define FLSC_HB_SYNCHRON	0x10
#define FLSC_HB_CREQ		0x20
#define FLSC_HB_IACT		0x40
#define FLSC_HB_MINT		0x80

#define FLSC_PB_ESI		0x01
#define FLSC_PB_EDI		0x02
#define FLSC_PB_ENABLE_DMA	0x04
#define FLSC_PB_DMA_WRITE	0x08
#define FLSC_PB_LED		0x10

#define OKTAGON_BOARD_SIZE 65536
#define OKTAGON_ROM_SIZE 32768
#define OKTAGON_ROM_OFFSET 0x100

#define OKTAGON_ESP_ADDR 0x03000
#define OKTAGON_DMA_START 0x01000
#define OKTAGON_DMA_END 0x2000
#define OKTAGON_INTENA 0x8000
#define OKTAGON_EEPROM_SCL 0x8010
#define OKTAGON_EEPROM_SDA 0x8018
#define OKTAGON_EEPROM_SIZE 512

#define DKB_BOARD_SIZE 131072
#define DKB_ROM_SIZE 32768
#define DKB_ROM_OFFSET 0x8000


struct ncr9x_state
{
	const TCHAR *name;
	DeviceState devobject;
	SCSIDevice *scsid[8];
	SCSIBus scsibus;
	uae_u32 board_mask;
	uae_u8 *rom;
	uae_u8 acmemory[128];
	int configured;
	uae_u32 expamem_hi;
	uae_u32 expamem_lo;
	bool enabled;
	int rom_start, rom_end, rom_offset;
	int io_start, io_end;
	addrbank *bank;
	bool chipirq, boardirq;
	void (*irq_func)(struct ncr9x_state*);
	int led;
	uaecptr dma_ptr;
	int dma_cnt;
	int state;

	uae_u8 data;
	bool data_valid;
	void *eeprom;
	uae_u8 eeprom_data[512];
	bool romisoddonly;
	bool romisevenonly;

	uae_u8 *dkb_data_buf;
	int dkb_data_size_allocated;
	int dkb_data_size;
	int dkb_data_offset;
	uae_u8 *dkb_data_write_buffer;
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

static struct ncr9x_state ncr_blizzard_scsi;
static struct ncr9x_state ncr_fastlane_scsi[MAX_BOARD_ROMS];
static struct ncr9x_state ncr_oktagon2008_scsi[MAX_BOARD_ROMS];
static struct ncr9x_state ncr_dkb1200_scsi;


static struct ncr9x_state *ncrs[] =
{
	&ncr_blizzard_scsi,
	&ncr_fastlane_scsi[0],
	&ncr_fastlane_scsi[1],
	&ncr_oktagon2008_scsi[0],
	&ncr_oktagon2008_scsi[1],
	&ncr_dkb1200_scsi,
	NULL
};

void ncr9x_rethink(void)
{
	for (int i = 0; ncrs[i]; i++) {
		if (ncrs[i]->boardirq)
			INTREQ_0(0x8000 | 0x0008);
	}
}

static void set_irq2(struct ncr9x_state *ncr)
{
	if (ncr->chipirq && !ncr->boardirq) {
		ncr->boardirq= true;
		ncr9x_rethink();
	}
}

static void set_irq2_dkb1200(struct ncr9x_state *ncr)
{
	if (!(ncr->state & 0x40))
		ncr->boardirq = false;
	if (ncr->chipirq && !ncr->boardirq && (ncr->state & 0x40)) {
		ncr->boardirq= true;
		ncr9x_rethink();
	}
}

static void set_irq2_oktagon(struct ncr9x_state *ncr)
{
	if (!(ncr->state & 0x80))
		ncr->boardirq = false;
	if (ncr->chipirq && !ncr->boardirq && (ncr->state & 0x80)) {
		ncr->boardirq = true;
		ncr9x_rethink();
	}
}

static void set_irq2_fastlane(struct ncr9x_state *ncr)
{
	if (!ncr->chipirq || !(ncr->state & FLSC_PB_ESI)) {
		ncr->state |= FLSC_HB_MINT;
		ncr->boardirq = false;
		return;
	}
	ncr->state |= FLSC_HB_CREQ;
	ncr->state &= ~FLSC_HB_MINT;
	if (ncr->state & FLSC_PB_ESI) {
		if (!ncr->boardirq) {
			ncr->boardirq = true;
			ncr9x_rethink();
		}
	}
}

void esp_irq_raise(qemu_irq irq)
{
	struct ncr9x_state *ncr = (struct ncr9x_state*)irq;
	ncr->chipirq = true;
	ncr->irq_func(ncr);
}
void esp_irq_lower(qemu_irq irq)
{
	struct ncr9x_state *ncr = (struct ncr9x_state*)irq;
	ncr->chipirq = false;
	ncr->boardirq = false;
	ncr->irq_func(ncr);
}

static void dkb_buffer_size(struct ncr9x_state *ncr, int size)
{
	size = (size + 1) & ~1;
	if (ncr->dkb_data_size_allocated >= size)
		return;
	if (ncr->dkb_data_buf)
		xfree(ncr->dkb_data_buf);
	ncr->dkb_data_buf = xmalloc(uae_u8, size);
	ncr->dkb_data_size_allocated = size;
}

/* Fake DMA */
static int dkb_dma_read(void *opaque, uint8_t *buf, int len)
{
	struct ncr9x_state *ncr = (struct ncr9x_state*)opaque;
	ncr->dkb_data_offset = 0;
	ncr->dkb_data_write_buffer = buf;
	dkb_buffer_size(ncr, len);
	return 0;
}
static int dkb_dma_write(void *opaque, uint8_t *buf, int len)
{
	struct ncr9x_state *ncr = (struct ncr9x_state*)opaque;
	ncr->dkb_data_offset = 0;
	dkb_buffer_size(ncr, len);
	memcpy(ncr->dkb_data_buf, buf, len);
	if (len & 1)
		ncr->dkb_data_buf[len] = 0;
	ncr->dkb_data_size = len;
	return 0;
}

/* Fake DMA */
static int oktagon_dma_read(void *opaque, uint8_t *buf, int len)
{
	struct ncr9x_state *ncr = (struct ncr9x_state*)opaque;
	esp_dma_enable(ncr->devobject.lsistate, 0);
	if (ncr->data_valid) {
		*buf = ncr->data;
		ncr->data_valid = false;
	}
	return 1;
}
static int oktagon_dma_write(void *opaque, uint8_t *buf, int len)
{
	struct ncr9x_state *ncr = (struct ncr9x_state*)opaque;
	esp_dma_enable(ncr->devobject.lsistate, 0);
	if (!ncr->data_valid) {
		ncr->data = *buf;
		ncr->data_valid = true;
		return 1;
	}
	return 0;
}

/* Following are true DMA */

static int fastlane_dma_read(void *opaque, uint8_t *buf, int len)
{
	struct ncr9x_state *ncr = (struct ncr9x_state*)opaque;
	if (!(ncr->state & FLSC_PB_DMA_WRITE)) {
		write_log(_T("fastlane_dma_read mismatched direction!\n"));
		return -1;
	}
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
	return -1;
}
static int fastlane_dma_write(void *opaque, uint8_t *buf, int len)
{
	struct ncr9x_state *ncr = (struct ncr9x_state*)opaque;
	if (ncr->state & FLSC_PB_DMA_WRITE) {
		write_log(_T("fastlane_dma_write mismatched direction!\n"));
		return -1;
	}
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
	return -1;
}

static int cyberstorm_mk1_mk2_dma_read(void *opaque, uint8_t *buf, int len)
{
	struct ncr9x_state *ncr = (struct ncr9x_state*)opaque;
	if (!(ncr->dma_ptr & 0x00000001)) {
		write_log(_T("cyberstorm_dma_read mismatched direction!\n"));
		return -1;
	}
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
	return -1;
}
static int cyberstorm_mk1_mk2_dma_write(void *opaque, uint8_t *buf, int len)
{
	struct ncr9x_state *ncr = (struct ncr9x_state*)opaque;
	if (ncr->dma_ptr & 0x00000001) {
		write_log(_T("cyberstorm_dma_write mismatched direction!\n"));
		return -1;
	}
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
	return -1;
}

static int blizzard_dma_read(void *opaque, uint8_t *buf, int len)
{
	struct ncr9x_state *ncr = (struct ncr9x_state*)opaque;
	if (!(ncr->dma_ptr & 0x80000000)) {
		write_log(_T("blizzard_dma_read mismatched direction!\n"));
		return -1;
	}
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
	return -1;
}
static int blizzard_dma_write(void *opaque, uint8_t *buf, int len)
{
	struct ncr9x_state *ncr = (struct ncr9x_state*)opaque;
	if (ncr->dma_ptr & 0x80000000) {
		write_log(_T("blizzard_dma_write mismatched direction!\n"));
		return -1;
	}
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
	return -1;
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
	} else if (sd->data_len) {
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

	if (sd->device_type == UAEDEV_CD)
		gui_flicker_led (LED_CD, sd->id, 1);

	sd->data_len = 0;
	scsi_start_transfer(sd);
	scsi_emulate_analyze(sd);
#if 0
	write_log (_T("%02x.%02x.%02x.%02x.%02x.%02x\n"), sd->cmd[0], sd->cmd[1], sd->cmd[2], sd->cmd[3], sd->cmd[4], sd->cmd[5]);
#endif
	if (sd->direction <= 0)
		scsi_emulate_cmd(sd);
	if (sd->direction == 0)
		return 1;
	if (sd->direction > 0)
		return -sd->data_len;
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

#define IO_MASK 0x3f

static uaecptr beswap(uaecptr addr)
{
	return (addr & ~3) | (3 - (addr & 3));
}

static void ncr9x_io_bput(struct ncr9x_state *ncr, uaecptr addr, uae_u32 val)
{
	int reg_shift = 2;
	addr &= ncr->board_mask;
	if (ncr == &ncr_oktagon2008_scsi[0] || ncr == &ncr_oktagon2008_scsi[1]) {
		if (addr == OKTAGON_EEPROM_SCL) {
			eeprom_i2c_set(ncr->eeprom, BITBANG_I2C_SCL, (val & 0x80) != 0);
		} else if (addr == OKTAGON_EEPROM_SDA) {
			eeprom_i2c_set(ncr->eeprom, BITBANG_I2C_SDA, (val & 0x80) != 0);
		} else if (addr >= OKTAGON_DMA_START && addr < OKTAGON_DMA_END) {
			ncr->data = val;
			ncr->data_valid = true;
			esp_dma_enable(ncr->devobject.lsistate, 1);
			return;
		} else if (addr == OKTAGON_INTENA) {
			ncr->state = val;
			set_irq2_oktagon(ncr);
			return;
		}
		if (addr < OKTAGON_ESP_ADDR || addr >= OKTAGON_ESP_ADDR + 0x100) {
			return;
		}
		reg_shift = 1;
	} else if (ncr == &ncr_fastlane_scsi[0] || ncr == &ncr_fastlane_scsi[1]) {
		if (addr >= FASTLANE_HARDBITS) {
			if (addr == FASTLANE_HARDBITS) {
				int oldstate = ncr->state;
				ncr->state = val;
				if (!(oldstate & FLSC_PB_ENABLE_DMA) && (ncr->state & FLSC_PB_ENABLE_DMA))
					esp_dma_enable(ncr->devobject.lsistate, 1);
				else if ((oldstate & FLSC_PB_ENABLE_DMA) && !(ncr->state & FLSC_PB_ENABLE_DMA))
					esp_dma_enable(ncr->devobject.lsistate, 0);
				set_irq2_fastlane(ncr);
			} else if (addr == FASTLANE_RESETDMA) {
				ncr->dma_cnt = 4;
				ncr->dma_ptr = 0;
			}
			return;
		} else if (addr < 0x01000000) {
			addr &= 3;
			addr = 3 - addr;
			ncr->dma_ptr &= ~(0xff << (addr * 8));
			ncr->dma_ptr |= (val & 0xff) << (addr * 8);
			ncr->dma_cnt--;
			if (ncr->dma_cnt == 0 && (ncr->state & FLSC_PB_ENABLE_DMA))
				esp_dma_enable(ncr->devobject.lsistate, 1);
			return;
		}
	} else if (currprefs.cpuboard_type == BOARD_BLIZZARD_2060) {
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
	} else if (currprefs.cpuboard_type == BOARD_DKB1200) {
		if (addr == 0x10100) {
			ncr->state = val;
			esp_dma_enable(ncr->devobject.lsistate, 1);
			//write_log(_T("DKB IO PUT %02x %08x\n"), val & 0xff, M68K_GETPC);
			return;
		}
		if (addr >= 0x10084 && addr < 0x10088) {
			//write_log(_T("DKB PUT BYTE %02x\n"), val & 0xff);
			if (ncr->dkb_data_offset < ncr->dkb_data_size) {
				ncr->dkb_data_buf[ncr->dkb_data_offset++] = val;
				if (ncr->dkb_data_offset == ncr->dkb_data_size) {
					memcpy(ncr->dkb_data_write_buffer, ncr->dkb_data_buf, ncr->dkb_data_size);
					esp_fake_dma_done(ncr->devobject.lsistate);
				}
			}
			return;
		}
		if (addr < 0x10000 || addr >= 0x10040) {
			write_log(_T("DKB IO %08X PUT %02x %08x\n"), addr, val & 0xff, M68K_GETPC);
			return;
		}
	}
	addr >>= reg_shift;
	addr &= IO_MASK;
#if NCR_DEBUG > 1
	write_log(_T("ESP write %02X %02X %08X\n"), addr, val & 0xff, M68K_GETPC);
#endif
	esp_reg_write(ncr->devobject.lsistate, (addr), val);
}


uae_u32 ncr9x_io_bget(struct ncr9x_state *ncr, uaecptr addr)
{
	uae_u8 v;
	int reg_shift = 2;
	addr &= ncr->board_mask;
	if (ncr == &ncr_oktagon2008_scsi[0] || ncr == &ncr_oktagon2008_scsi[1]) {
		if (addr == OKTAGON_EEPROM_SCL) {
			return eeprom_i2c_set(ncr->eeprom, BITBANG_I2C_SCL, -1) ? 0x80 : 0x00;
		} else if (addr == OKTAGON_EEPROM_SDA) {
			return eeprom_i2c_set(ncr->eeprom, BITBANG_I2C_SDA, -1) ? 0x80 : 0x00;
		} else if (addr >= OKTAGON_DMA_START && addr < OKTAGON_DMA_END) {
			esp_dma_enable(ncr->devobject.lsistate, 1);
			v = ncr->data;
			ncr->data_valid = false;
			return v;
		} else if (addr == OKTAGON_INTENA) {
			return ncr->state;
		}
		if (addr < OKTAGON_ESP_ADDR || addr >= OKTAGON_ESP_ADDR + 0x100)
			return 0xff;
		reg_shift = 1;
	} else if (ncr == &ncr_fastlane_scsi[0] || ncr == &ncr_fastlane_scsi[1]) {
		if (addr >= FASTLANE_HARDBITS) {
			if (addr == FASTLANE_HARDBITS) {
				uae_u8 v = ncr->state;
				v &= ~(FLSC_HB_DISABLED | FLSC_HB_BUSID6 | FLSC_HB_SEAGATE | FLSC_HB_SLOW | FLSC_HB_SYNCHRON);
				return v;
			}
			return 0;
		}
	} else if (currprefs.cpuboard_type == BOARD_BLIZZARD_2060) {
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
	} else if (currprefs.cpuboard_type == BOARD_DKB1200) {
		if (addr == 0x10100) {
			uae_u8 v = 0;
			if (ncr->chipirq)
				v |= 0x40;
			if (ncr->dkb_data_offset < ncr->dkb_data_size)
				v |= 0x80;
			//write_log(_T("DKB IO GET %02x %08x\n"), v, M68K_GETPC);
			return v;
		}
		if (addr >= 0x10080 && addr < 0x10084) {
			//write_log(_T("DKB GET BYTE %02x\n"), ncr->dkb_data_buf[ncr->dkb_data_offset]);
			if (ncr->dkb_data_offset >= ncr->dkb_data_size)
				return 0;
			if (ncr->dkb_data_offset == ncr->dkb_data_size - 1) {
				esp_fake_dma_done(ncr->devobject.lsistate);
				//write_log(_T("DKB fake dma finished\n"));
			}
			return ncr->dkb_data_buf[ncr->dkb_data_offset++];
		}
		if (addr < 0x10000 || addr >= 0x10040) {
			write_log(_T("DKB IO GET %08x %08x\n"), addr, M68K_GETPC);
			return 0;
		}
	}
	addr >>= reg_shift;
	addr &= IO_MASK;
	v = esp_reg_read(ncr->devobject.lsistate, (addr));
#if NCR_DEBUG > 1
	write_log(_T("ESP read %02X %02X %08X\n"), addr, v, M68K_GETPC);
#endif
	return v;
}

static uae_u8 read_rombyte(struct ncr9x_state *ncr, uaecptr addr)
{
	uae_u8 v = ncr->rom[addr];
#if 0
	if (addr == 0x104)
		activate_debugger();
#endif
	return v;
}

static uae_u32 ncr9x_bget2(struct ncr9x_state *ncr, uaecptr addr)
{
	uae_u32 v = 0;

	addr &= ncr->board_mask;
	if (ncr->rom && addr >= ncr->rom_start && addr < ncr->rom_end) {
		if (addr < ncr->io_start || (!ncr->romisoddonly && !ncr->romisevenonly) || (ncr->romisoddonly && (addr & 1)) || (ncr->romisevenonly && (addr & 1)))
			return read_rombyte (ncr, addr - ncr->rom_offset);
	}
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
	if (ncr == &ncr_oktagon2008_scsi[0] || ncr == &ncr_oktagon2008_scsi[1]) {
		v =  ncr9x_io_bget(ncr, addr + 0) << 24;
		v |= ncr9x_io_bget(ncr, addr + 1) << 16;
		v |= ncr9x_io_bget(ncr, addr + 2) <<  8;
		v |= ncr9x_io_bget(ncr, addr + 3) <<  0;
	} else {
		v =  ncr9x_bget2(ncr, addr + 0) << 24;
		v |= ncr9x_bget2(ncr, addr + 1) << 16;
		v |= ncr9x_bget2(ncr, addr + 2) <<  8;
		v |= ncr9x_bget2(ncr, addr + 3) <<  0;
	}
	return v;
}

static uae_u32 REGPARAM2 ncr9x_wget(struct ncr9x_state *ncr, uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= ncr->board_mask;
	if (ncr == &ncr_oktagon2008_scsi[0] || ncr == &ncr_oktagon2008_scsi[1]) {
		v = ncr9x_io_bget(ncr, addr) << 8;
		v |= ncr9x_io_bget(ncr, addr + 1);
	} else {
		v = ncr9x_bget2(ncr, addr) << 8;
		v |= ncr9x_bget2(ncr, addr + 1);
	}
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
	if (ncr == &ncr_oktagon2008_scsi[0] || ncr == &ncr_oktagon2008_scsi[1]) {
		ncr9x_io_bput(ncr, addr + 0, l >> 24);
		ncr9x_io_bput(ncr, addr + 1, l >> 16);
		ncr9x_io_bput(ncr, addr + 2, l >>  8);
		ncr9x_io_bput(ncr, addr + 3, l >>  0);
	} else {
		ncr9x_bput2(ncr, addr + 0, l >> 24);
		ncr9x_bput2(ncr, addr + 1, l >> 16);
		ncr9x_bput2(ncr, addr + 2, l >>  8);
		ncr9x_bput2(ncr, addr + 3, l >>  0);
	}
}


static void REGPARAM2 ncr9x_wput(struct ncr9x_state *ncr, uaecptr addr, uae_u32 w)
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
			map_banks (ncr->bank, expamem_z3_pointer >> 16, FASTLANE_BOARD_SIZE >> 16, 0);
			ncr->configured = 1;
			expamem_next (ncr->bank, NULL);
			break;
		}
		return;
	}
	if (ncr == &ncr_oktagon2008_scsi[0] || ncr == &ncr_oktagon2008_scsi[1]) {
		ncr9x_io_bput(ncr, addr, w >> 8);
		ncr9x_io_bput(ncr, addr + 1, w);
	} else {
		ncr9x_bput2(ncr, addr, w >> 8);
		ncr9x_bput2(ncr, addr + 1, w);
	}
}

static void REGPARAM2 ncr9x_bput(struct ncr9x_state *ncr, uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	b &= 0xff;
	addr &= ncr->board_mask;
	if (!ncr->configured) {
		switch (addr)
		{
			case 0x48:
			if (ncr == &ncr_oktagon2008_scsi[0] || ncr == &ncr_oktagon2008_scsi[1]) {
				map_banks (ncr->bank, expamem_z2_pointer >> 16, OKTAGON_BOARD_SIZE >> 16, 0);
				ncr->configured = 1;
				expamem_next (ncr->bank, NULL);
			} else if (ncr == &ncr_dkb1200_scsi) {
				map_banks (ncr->bank, expamem_z2_pointer >> 16, DKB_BOARD_SIZE >> 16, 0);
				ncr->configured = 1;
				expamem_next (ncr->bank, NULL);
			}
			break;
			case 0x4c:
			ncr->configured = 1;
			expamem_shutup(ncr->bank);
			break;
		}
		return;
	}
	ncr9x_bput2(ncr, addr, b);
}

SCSI_MEMORY_FUNCTIONS(bncr9x, ncr9x, ncr_blizzard_scsi);

static addrbank ncr9x_bank_blizzard = {
	bncr9x_lget, bncr9x_wget, bncr9x_bget,
	bncr9x_lput, bncr9x_wput, bncr9x_bput,
	default_xlate, default_check, NULL, NULL, _T("53C94/FAS216"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO
};

uae_u32 cpuboard_ncr9x_scsi_get(uaecptr addr)
{
	return ncr9x_io_bget(&ncr_blizzard_scsi, addr);
}
void cpuboard_ncr9x_scsi_put(uaecptr addr, uae_u32 v)
{
	ncr9x_io_bput(&ncr_blizzard_scsi, addr, v);
}

static void ew(struct ncr9x_state *ncr, int addr, uae_u8 value)
{
	if (addr == 00 || addr == 02 || addr == 0x40 || addr == 0x42) {
		ncr->acmemory[addr] = (value & 0xf0);
		ncr->acmemory[addr + 2] = (value & 0x0f) << 4;
	} else {
		ncr->acmemory[addr] = ~(value & 0xf0);
		ncr->acmemory[addr + 2] = ~((value & 0x0f) << 4);
	}
}

SCSI_MEMORY_FUNCTIONS(ncr_fastlane, ncr9x, ncr_fastlane_scsi[0]);
SCSI_MEMORY_FUNCTIONS(ncr2_fastlane, ncr9x, ncr_fastlane_scsi[1]);
DECLARE_MEMORY_FUNCTIONS(ncr_fastlane)
static addrbank ncr_bank_fastlane = {
	ncr_fastlane_lget, ncr_fastlane_wget, ncr_fastlane_bget,
	ncr_fastlane_lput, ncr_fastlane_wput, ncr_fastlane_bput,
	default_xlate, default_check, NULL, NULL, _T("Fastlane"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO
};
DECLARE_MEMORY_FUNCTIONS(ncr2_fastlane)
static addrbank ncr_bank_fastlane_2 = {
	ncr2_fastlane_lget, ncr2_fastlane_wget, ncr2_fastlane_bget,
	ncr2_fastlane_lput, ncr2_fastlane_wput, ncr2_fastlane_bput,
	default_xlate, default_check, NULL, NULL, _T("Fastlane #2"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO
};

SCSI_MEMORY_FUNCTIONS(ncr_oktagon, ncr9x, ncr_oktagon2008_scsi[0]);
SCSI_MEMORY_FUNCTIONS(ncr2_oktagon, ncr9x, ncr_oktagon2008_scsi[1]);
DECLARE_MEMORY_FUNCTIONS(ncr_oktagon2008)
static addrbank ncr_bank_oktagon = {
	ncr_oktagon_lget, ncr_oktagon_wget, ncr_oktagon_bget,
	ncr_oktagon_lput, ncr_oktagon_wput, ncr_oktagon_bput,
	default_xlate, default_check, NULL, NULL, _T("Oktagon 2008"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO | ABFLAG_SAFE
};
DECLARE_MEMORY_FUNCTIONS(ncr2_oktagon2008)
static addrbank ncr_bank_oktagon_2 = {
	ncr2_oktagon_lget, ncr2_oktagon_wget, ncr2_oktagon_bget,
	ncr2_oktagon_lput, ncr2_oktagon_wput, ncr2_oktagon_bput,
	default_xlate, default_check, NULL, NULL, _T("Oktagon 2008 #2"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO
};
SCSI_MEMORY_FUNCTIONS(ncr_dkb, ncr9x, ncr_dkb1200_scsi);
DECLARE_MEMORY_FUNCTIONS(ncr_dkb)
static addrbank ncr_bank_dkb = {
	ncr_dkb_lget, ncr_dkb_wget, ncr_dkb_bget,
	ncr_dkb_lput, ncr_dkb_wput, ncr_dkb_bput,
	default_xlate, default_check, NULL, NULL, _T("DKB"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO
};

static void ncr9x_reset_board(struct ncr9x_state *ncr)
{
	ncr->configured = 0;
	if (currprefs.cpuboard_type == BOARD_CSMK1)
		ncr->board_mask = 0xffff;
	else
		ncr->board_mask = 0x1ffff;
	ncr->boardirq = false;
	ncr->chipirq = false;
	if (ncr->devobject.lsistate)
		esp_scsi_reset(&ncr->devobject, ncr);
	ncr->irq_func = set_irq2;
	if (ncr == &ncr_blizzard_scsi) {
		ncr->bank = &ncr9x_bank_blizzard;
	} else if (ncr == &ncr_fastlane_scsi[0]) {
		ncr->bank = &ncr_bank_fastlane;
		ncr->board_mask = FASTLANE_BOARD_SIZE - 1;
		ncr->irq_func = set_irq2_fastlane;
	} else if (ncr == &ncr_fastlane_scsi[1]) {
		ncr->bank = &ncr_bank_fastlane_2;
		ncr->board_mask = FASTLANE_BOARD_SIZE - 1;
		ncr->irq_func = set_irq2_fastlane;
	} else if (ncr == &ncr_oktagon2008_scsi[0]) {
		ncr->bank = &ncr_bank_oktagon;
		ncr->board_mask = OKTAGON_BOARD_SIZE - 1;
		ncr->irq_func = set_irq2_oktagon;
	} else if (ncr == &ncr_oktagon2008_scsi[1]) {
		ncr->bank = &ncr_bank_oktagon_2;
		ncr->board_mask = OKTAGON_BOARD_SIZE - 1;
		ncr->irq_func = set_irq2_oktagon;
	} else if (ncr == &ncr_dkb1200_scsi) {
		ncr->bank = &ncr_bank_dkb;
		ncr->board_mask = DKB_BOARD_SIZE - 1;
		ncr->irq_func = set_irq2_dkb1200;
	}
	ncr->name = ncr->bank->name;
}

void ncr9x_reset(void)
{
	for (int i = 0; ncrs[i]; i++) {
		ncr9x_reset_board(ncrs[i]);
		ncrs[i]->configured = 0;
		ncrs[i]->enabled = false;
	}
	ncr_blizzard_scsi.configured = -1;
	ncr_blizzard_scsi.enabled = true;
}

addrbank *ncr_fastlane_autoconfig_init(int devnum)
{
	int roms[2];
	struct ncr9x_state *ncr = &ncr_fastlane_scsi[devnum];
	const TCHAR *romname;

	xfree(ncr->rom);
	ncr->rom = NULL;

	if (!ncr->enabled && devnum > 0)
		return &expamem_null;

	roms[0] = 102;
	roms[1] = -1;

	ncr->enabled = true;
	memset (ncr->acmemory, 0xff, sizeof ncr->acmemory);
	ncr->rom_start = 0x800;
	ncr->rom_offset = 0;
	ncr->rom_end = FASTLANE_ROM_SIZE * 4;
	ncr->io_start = 0;
	ncr->io_end = 0;

	ncr9x_init ();
	ncr9x_reset_board(ncr);

	romname = devnum && currprefs.fastlanerom.roms[1].romfile[0] ? currprefs.fastlanerom.roms[1].romfile : currprefs.fastlanerom.roms[0].romfile;
	struct zfile *z = read_rom_name (romname);
	if (!z) {
		struct romlist *rl = getromlistbyids(roms, romname);
		if (rl) {
			struct romdata *rd = rl->rd;
			z = read_rom (rd);
		}
	}

	ncr->rom = xcalloc (uae_u8, FASTLANE_ROM_SIZE * 4);
	if (z) {
		// memory board at offset 0x100
		int autoconfig_offset = 0;
		write_log (_T("%s BOOT ROM '%s'\n"), ncr->name, zfile_getname (z));
		memset(ncr->rom, 0xff, FASTLANE_ROM_SIZE * 4);
		for (int i = 0; i < FASTLANE_ROM_SIZE; i++) {
			int ia = i - autoconfig_offset;
			uae_u8 b;
			zfile_fread (&b, 1, 1, z);
			ncr->rom[i * 4 + 0] = b | 0x0f;
			ncr->rom[i * 4 + 2] = (b << 4) | 0x0f;
			if (ia >= 0 && ia < 0x20) {
				ncr->acmemory[ia * 4 + 0] = b;
			} else if (ia >= 0x40 && ia < 0x60) {
				ncr->acmemory[(ia - 0x40) * 4 + 2] = b;
			}
		}
		zfile_fclose(z);
	}

	return ncr == &ncr_fastlane_scsi[0] ? &ncr_bank_fastlane : &ncr_bank_fastlane_2;
}

static const uae_u8 oktagon_autoconfig[16] = {
	0xd1, 0x05, 0x00, 0x00, 0x08, 0x2c, 0x00, 0x00, 0x00, 0x00, OKTAGON_ROM_OFFSET >> 8, OKTAGON_ROM_OFFSET & 0xff
};

// Only offsets 0x100 to 0x10f contains non-FF data
static const uae_u8 oktagon_eeprom[16] =
{
	0x0b, 0xf4, 0x3f, 0x0a, 0xff, 0x06, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0xaf, 0xff
};

addrbank *ncr_oktagon_autoconfig_init(int devnum)
{
	int roms[2];
	struct ncr9x_state *ncr = &ncr_oktagon2008_scsi[devnum];
	const TCHAR *romname;

	xfree(ncr->rom);
	ncr->rom = NULL;
	eeprom_free(ncr->eeprom);
	ncr->eeprom = NULL;

	if (!ncr->enabled && devnum > 0)
		return &expamem_null;

	roms[0] = 103;
	roms[1] = -1;

	ncr->enabled = true;
	memset (ncr->acmemory, 0xff, sizeof ncr->acmemory);
	ncr->rom_start = 0;
	ncr->rom_offset = 0;
	ncr->rom_end = OKTAGON_ROM_SIZE * 2;
	ncr->io_start = 0x2000;
	ncr->io_end = ncr->rom_end;
	ncr->romisoddonly = true;

	memset(ncr->eeprom_data, 0xff, OKTAGON_EEPROM_SIZE);
	memcpy(ncr->eeprom_data + 0x100, oktagon_eeprom, 16);
	ncr->eeprom = eeprom_new(ncr->eeprom_data, OKTAGON_EEPROM_SIZE, NULL);

	ncr9x_init ();
	ncr9x_reset_board(ncr);

	romname = devnum && currprefs.oktagonrom.roms[1].romfile[0] ? currprefs.oktagonrom.roms[1].romfile : currprefs.oktagonrom.roms[0].romfile;
	struct zfile *z = read_rom_name (romname);
	if (!z) {
		struct romlist *rl = getromlistbyids(roms, romname);
		if (rl) {
			struct romdata *rd = rl->rd;
			z = read_rom (rd);
		}
	}
	ncr->rom = xcalloc (uae_u8, OKTAGON_ROM_SIZE * 6);
	if (z) {
		// memory board at offset 0x100
		int autoconfig_offset = 0;
		write_log (_T("%s BOOT ROM '%s'\n"), ncr->name, zfile_getname (z));
		memset(ncr->rom, 0xff, OKTAGON_ROM_SIZE * 4);
		for (int i = 0; i < 0x1000 / 2; i++) {
			uae_u8 b;
			zfile_fread(&b, 1, 1, z);
			ncr->rom[OKTAGON_ROM_OFFSET + i * 4 + 0] = b;
			zfile_fread(&b, 1, 1, z);
			ncr->rom[OKTAGON_ROM_OFFSET + i * 4 + 2] = b;
		}
		for (int i = 0; i < OKTAGON_ROM_SIZE - 0x1000; i++) {
			uae_u8 b;
			zfile_fread(&b, 1, 1, z);
			ncr->rom[0x2000 + i * 4 + 1] = b;
			zfile_fread(&b, 1, 1, z);
			ncr->rom[0x2000 + i * 4 + 3] = b;
		}

		for (int i = 0; i < 16; i++) {
			uae_u8 b = oktagon_autoconfig[i];
			ew(ncr, i * 4, b);
		}
		zfile_fclose(z);
	}

	return ncr == &ncr_oktagon2008_scsi[0] ? &ncr_bank_oktagon : &ncr_bank_oktagon_2;
}


addrbank *ncr_dkb_autoconfig_init(void)
{
	int roms[2];
	struct ncr9x_state *ncr = &ncr_dkb1200_scsi;
	const TCHAR *romname;

	xfree(ncr->rom);
	ncr->rom = NULL;

	roms[0] = 112;
	roms[1] = -1;

	ncr->enabled = true;
	memset (ncr->acmemory, 0xff, sizeof ncr->acmemory);
	ncr->rom_start = 0;
	ncr->rom_offset = 0;
	ncr->rom_end = DKB_ROM_SIZE * 2;
	ncr->io_start = 0x10000;
	ncr->io_end = 0x20000;

	ncr9x_init ();
	ncr9x_reset_board(ncr);

	romname = currprefs.acceleratorromfile;
		
	struct zfile *z = read_rom_name (romname);
	if (!z) {
		struct romlist *rl = getromlistbyids(roms, romname);
		if (rl) {
			struct romdata *rd = rl->rd;
			z = read_rom (rd);
		}
	}
	ncr->rom = xcalloc (uae_u8, DKB_ROM_SIZE * 2);
	if (z) {
		// memory board at offset 0x100
		int autoconfig_offset = 0;
		int i;
		write_log (_T("%s BOOT ROM '%s'\n"), ncr->name, zfile_getname (z));
		memset(ncr->rom, 0xff, DKB_ROM_SIZE * 2);

		zfile_fseek(z, 0, SEEK_SET);
		for (i = 0; i < (sizeof ncr->acmemory) / 2; i++) {
			uae_u8 b;
			zfile_fread(&b, 1, 1, z);
			ncr->acmemory[i * 2] = b;
		}
		for (;;) {
			uae_u8 b;
			if (!zfile_fread(&b, 1, 1, z))
				break;
			ncr->rom[i * 2] = b;
			i++;
		}
		zfile_fclose(z);
	}

	return &ncr_bank_dkb;
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
	for (int i = 0; ncrs[i]; i++) {
		ncr9x_free2(ncrs[i]);
	}
}

void ncr9x_init(void)
{
	if (!ncr_blizzard_scsi.devobject.lsistate) {
		if (currprefs.cpuboard_type == BOARD_CSMK2 || currprefs.cpuboard_type == BOARD_CSMK1)
			esp_scsi_init(&ncr_blizzard_scsi.devobject, cyberstorm_mk1_mk2_dma_read, cyberstorm_mk1_mk2_dma_write);
		else
			esp_scsi_init(&ncr_blizzard_scsi.devobject, blizzard_dma_read, blizzard_dma_write);
		esp_scsi_init(&ncr_fastlane_scsi[0].devobject, fastlane_dma_read, fastlane_dma_write);
		esp_scsi_init(&ncr_fastlane_scsi[1].devobject, fastlane_dma_read, fastlane_dma_write);
		esp_scsi_init(&ncr_oktagon2008_scsi[0].devobject, oktagon_dma_read, oktagon_dma_write);
		esp_scsi_init(&ncr_oktagon2008_scsi[1].devobject, oktagon_dma_read, oktagon_dma_write);
		esp_scsi_init(&ncr_dkb1200_scsi.devobject, dkb_dma_read, dkb_dma_write);
	}
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

static int ncr9x_add_scsi_unit(struct ncr9x_state *ncr, int ch, struct uaedev_config_info *ci)
{
	if (ci->type == UAEDEV_CD)
		return add_ncr_scsi_cd (ncr, ch, ci->device_emu_unit);
	else if (ci->type == UAEDEV_TAPE)
		return add_ncr_scsi_tape (ncr, ch, ci->rootdir, ci->readonly);
	else
		return add_ncr_scsi_hd (ncr, ch, NULL, ci, 1);
}

int cpuboard_ncr9x_add_scsi_unit(int ch, struct uaedev_config_info *ci)
{
	return ncr9x_add_scsi_unit(&ncr_blizzard_scsi, ch, ci);
}

int cpuboard_dkb_add_scsi_unit(int ch, struct uaedev_config_info *ci)
{
	return ncr9x_add_scsi_unit(&ncr_dkb1200_scsi, ch, ci);
}

int fastlane_add_scsi_unit (int ch, struct uaedev_config_info *ci, int devnum)
{
	return ncr9x_add_scsi_unit(&ncr_fastlane_scsi[devnum], ch, ci);
}

int oktagon_add_scsi_unit (int ch, struct uaedev_config_info *ci, int devnum)
{
	return ncr9x_add_scsi_unit(&ncr_oktagon2008_scsi[devnum], ch, ci);
}

#endif
