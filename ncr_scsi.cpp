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
#include "memory_uae.h"
#include "rommgr.h"
#include "custom.h"
#include "newcpu.h"
#include "ncr_scsi.h"
#include "scsi.h"
#include "filesys.h"
#include "zfile.h"
#include "blkdev.h"
#include "qemuvga\qemuuaeglue.h"
#include "qemuvga\queue.h"
#include "qemuvga\scsi\scsi.h"

#define ROM_VECTOR 0x0200
#define ROM_OFFSET 0x0000
#define ROM_SIZE 32768
#define ROM_MASK (ROM_SIZE - 1)
#define BOARD_SIZE 16777216

#define A4091_IO_OFFSET 0x00800000
#define A4091_IO_ALT 0x00840000
#define A4091_IO_END 0x00880000
#define A4091_IO_MASK 0xff

#define A4091_DIP_OFFSET 0x008c0003

struct ncr_state
{
	TCHAR *name;
	DeviceState devobject;
	SCSIDevice *scsid[8];
	SCSIBus scsibus;
	uae_u32 board_mask;
	uae_u8 *rom;
	uae_u8 acmemory[128];
	uae_u32 expamem_hi;
	uae_u32 expamem_lo;
	int configured;
	bool enabled;
};

static struct ncr_state ncr_a4091;
static struct ncr_state ncr_a4091_2;
static struct ncr_state ncr_a4000t;

static struct ncr_state *ncra4091[] =
{
	&ncr_a4091,
	&ncr_a4091_2
};

void pci_set_irq(PCIDevice *pci_dev, int level)
{
	if (!level)
		return;
	INTREQ (0x8000 | 0x0008);
}

void scsi_req_continue(SCSIRequest *req)
{
	struct scsi_data *sd = (struct scsi_data*)req->dev->handle;
	if (sd->data_len < 0) {
		lsi_command_complete (req, sd->status, 0);
	} else if (sd->data_len) {
		lsi_transfer_data (req, sd->data_len);
	} else {
		if (sd->direction > 0)
			scsi_emulate_cmd(sd);
		lsi_command_complete (req, sd->status, 0);
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
	
	memcpy (sd->cmd, buf, len);
	sd->cmd_len = len;
	return req;
}
int32_t scsi_req_enqueue(SCSIRequest *req)
{
	struct scsi_data *sd = (struct scsi_data*)req->dev->handle;

	sd->data_len = 0;
	scsi_start_transfer (sd);
	scsi_emulate_analyze (sd);
	//write_log (_T("%02x.%02x.%02x.%02x.%02x.%02x\n"), sd->cmd[0], sd->cmd[1], sd->cmd[2], sd->cmd[3], sd->cmd[4], sd->cmd[5]);
	
	if (sd->direction < 0)
		scsi_emulate_cmd(sd);
	if (sd->direction == 0)
		return 1;
	return -sd->direction;
}
void scsi_req_unref(SCSIRequest *req)
{
	xfree (req);
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
	write_log (_T("scsi_req_cancel\n"));
}

static uae_u8 read_rombyte (struct ncr_state *ncr, uaecptr addr)
{
	uae_u8 v = ncr->rom[addr];
	//write_log (_T("%08X = %02X PC=%08X\n"), addr, v, M68K_GETPC);
	return v;
}

int pci_dma_rw(PCIDevice *dev, dma_addr_t addr, void *buf, dma_addr_t len, DMADirection dir)
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

static uaecptr beswap (uaecptr addr)
{
	return (addr & ~3) | (3 - (addr & 3));
}

void ncr_io_bput (struct ncr_state *ncr, uaecptr addr, uae_u32 val)
{
	addr &= A4091_IO_MASK;
	lsi_mmio_write (ncr->devobject.lsistate, beswap (addr), val, 1);
}

static void ncr_bput2 (struct ncr_state *ncr, uaecptr addr, uae_u32 val)
{
	uae_u32 v = val;
	addr &= ncr->board_mask;
	if (addr < A4091_IO_OFFSET || addr >= A4091_IO_END)
		return;
	ncr_io_bput (ncr, addr, val);
}

uae_u32 ncr_io_bget (struct ncr_state *ncr, uaecptr addr)
{
	addr &= A4091_IO_MASK;
	return lsi_mmio_read (ncr->devobject.lsistate, beswap (addr), 1);
}

static uae_u32 ncr_bget2 (struct ncr_state *ncr, uaecptr addr)
{
	uae_u32 v = 0;

	addr &= ncr->board_mask;
	if (ncr->rom && addr >= ROM_VECTOR && addr < A4091_IO_OFFSET)
		return read_rombyte (ncr, addr);
	if (addr == A4091_DIP_OFFSET)
		return 0xff;
	if (addr < A4091_IO_OFFSET || addr >= A4091_IO_END)
		return v;
	addr &= A4091_IO_MASK;
	return ncr_io_bget (ncr, addr);
}

static uae_u32 REGPARAM2 ncr_lget (struct ncr_state *ncr, uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= ncr->board_mask;
	if (addr >= A4091_IO_ALT) {
		v = (ncr_bget2 (ncr, addr + 3) << 0) | (ncr_bget2 (ncr, addr + 2) << 8) |
			(ncr_bget2 (ncr, addr + 1) << 16) | (ncr_bget2 (ncr, addr + 0) << 24);
	} else {
		v = (ncr_bget2 (ncr, addr + 3) << 0) | (ncr_bget2 (ncr, addr + 2) << 8) |
			(ncr_bget2 (ncr, addr + 1) << 16) | (ncr_bget2 (ncr, addr + 0) << 24);
	}
	return v;
}

static uae_u32 REGPARAM2 ncr_wget (struct ncr_state *ncr, uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= ncr->board_mask;
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

extern addrbank ncr_bank_a4091;
extern addrbank ncr_bank_a4091_2;

static void REGPARAM2 ncr_wput (struct ncr_state *ncr, uaecptr addr, uae_u32 w)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	w &= 0xffff;
	addr &= ncr->board_mask;
	if (!ncr->configured) {
		uae_u32 value;
		switch (addr)
		{
			case 0x44:
			// yes, this could be much better..
			if (currprefs.jit_direct_compatible_memory) {
				value = gfxmem_bank.start + ((currprefs.rtgmem_size + 0xffffff) & ~0xffffff);
				if (value < 0x10000000) {
					value = 0x10000000;
					if (value < z3fastmem_bank.start + currprefs.z3fastmem_size)
						value = z3fastmem_bank.start + currprefs.z3fastmem_size;
					if (value < z3fastmem2_bank.start + currprefs.z3fastmem2_size)
						value = z3fastmem2_bank.start + currprefs.z3fastmem2_size;
				}
				if (value < 0x40000000 && max_z3fastmem >= 0x41000000)
					value = 0x40000000;
				if (ncr == &ncr_a4091_2)
					value += 16 * 1024 * 1024;
				value >>= 16;
				chipmem_wput (regs.regs[11] + 0x20, value);
				chipmem_wput (regs.regs[11] + 0x28, value);
			} else {
				ncr->expamem_hi = w & 0xff00;
				value = ncr->expamem_hi | (ncr->expamem_lo >> 4);
			}
			map_banks (ncr == &ncr_a4091 ? &ncr_bank_a4091 : &ncr_bank_a4091_2, value, BOARD_SIZE >> 16, 0);
			ncr->board_mask = 0x00ffffff;
			write_log (_T("%s Z3 autoconfigured at %04X0000\n"), ncr->name, value);
			ncr->configured = 1;
			expamem_next();
			break;
		}
		return;
	}
	ncr_bput2 (ncr, addr, w >> 8);
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
			write_log (_T("A4091 AUTOCONFIG SHUT-UP!\n"));
			ncr->configured = 1;
			expamem_next ();
			break;
			case 0x48:
			ncr->expamem_lo = b & 0xff;
			break;
		}
		return;
	}
	ncr_bput2 (ncr, addr, b);
}

void ncr_io_bput_a4000t(uaecptr addr, uae_u32 v)
{
	ncr_io_bput(&ncr_a4000t, addr, v);
}
uae_u32 ncr_io_bget_a4000t(uaecptr addr)
{
	return ncr_io_bget(&ncr_a4000t, addr);
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

static addrbank ncr_bank_a4091 = {
	ncr4_lget, ncr4_wget, ncr4_bget,
	ncr4_lput, ncr4_wput, ncr4_bput,
	default_xlate, default_check, NULL, _T("A4091"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO
};

static addrbank ncr_bank_a4091_2 = {
	ncr42_lget, ncr42_wget, ncr42_bget,
	ncr42_lput, ncr42_wput, ncr42_bput,
	default_xlate, default_check, NULL, _T("A4091 #2"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO
};


static void ew (struct ncr_state *ncr, int addr, uae_u32 value)
{
	if (addr == 00 || addr == 02 || addr == 0x40 || addr == 0x42) {
		ncr->acmemory[addr] = (value & 0xf0);
		ncr->acmemory[addr + 2] = (value & 0x0f) << 4;
	} else {
		ncr->acmemory[addr] = ~(value & 0xf0);
		ncr->acmemory[addr + 2] = ~((value & 0x0f) << 4);
	}
}

void ncr_init (void)
{
	if (!ncr_a4091.devobject.lsistate)
		lsi_scsi_init (&ncr_a4091.devobject);
	if (!ncr_a4091_2.devobject.lsistate)
		lsi_scsi_init (&ncr_a4091_2.devobject);
	if (!ncr_a4000t.devobject.lsistate)
		lsi_scsi_init (&ncr_a4000t.devobject);
}

static void ncr_reset_board (struct ncr_state *ncr)
{
	ncr->configured = 0;
	ncr->board_mask = 0xffff;
	if (ncr->devobject.lsistate)
		lsi_scsi_reset (&ncr->devobject, ncr);
	if (ncr == &ncr_a4000t)
		ncr->name = _T("A4000T NCR SCSI");
	if (ncr == &ncr_a4091)
		ncr->name = _T("A4091 SCSI");
	if (ncr == &ncr_a4091_2)
		ncr->name = _T("A4091 SCSI #2");
}

void ncr_autoconfig_init (int devnum)
{
	struct ncr_state *ncr = ncra4091[devnum];
	int roms[3];
	int i;

	if (!ncr->enabled && devnum > 0) {
		expamem_next();
		return;
	}
	ncr->enabled = true;

	roms[0] = 58;
	roms[1] = 57;
	roms[2] = -1;

	memset (ncr->acmemory, 0xff, sizeof ncr->acmemory);

	struct zfile *z = read_rom_name (devnum && currprefs.a4091romfile2[0] ? currprefs.a4091romfile2 : currprefs.a4091romfile);
	if (!z) {
		struct romlist *rl = getromlistbyids(roms);
		if (rl) {
			struct romdata *rd = rl->rd;
			z = read_rom (rd);
		}
	}
	if (z) {
		write_log (_T("%s BOOT ROM '%s'\n"), ncr->name, zfile_getname (z));
		ncr->rom = xmalloc (uae_u8, ROM_SIZE * 4);
		for (i = 0; i < ROM_SIZE; i++) {
			uae_u8 b;
			zfile_fread (&b, 1, 1, z);
			ncr->rom[i * 4 + 0] = b;
			ncr->rom[i * 4 + 2] = b << 4;
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

	ncr_init ();
	ncr_reset_board(ncr);
	map_banks (ncr == &ncr_a4091 ? &ncr_bank_a4091 : &ncr_bank_a4091_2, 0xe80000 >> 16, 65536 >> 16, 0);
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

void ncr_free2(struct ncr_state *ncr)
{
	for (int ch = 0; ch < 8; ch++) {
		freescsi (ncr->scsid[ch]);
		ncr->scsid[ch] = NULL;
	}
}

void ncr_free(void)
{
	ncr_free2(&ncr_a4000t);
	ncr_free2(&ncr_a4091);
	ncr_free2(&ncr_a4091_2);
}

void ncr_reset (void)
{
	ncr_reset_board(&ncr_a4091);
	ncr_reset_board(&ncr_a4091_2);
	ncr_reset_board(&ncr_a4000t);
	if (currprefs.cs_mbdmac & 2) {
		ncr_a4000t.configured = -1;
		ncr_a4000t.enabled = true;
	}
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

int a4000t_add_scsi_unit (int ch, struct uaedev_config_info *ci)
{
	if (ci->type == UAEDEV_CD)
		return add_ncr_scsi_cd (&ncr_a4000t, ch, ci->device_emu_unit);
	else if (ci->type == UAEDEV_TAPE)
		return add_ncr_scsi_tape (&ncr_a4000t, ch, ci->rootdir, ci->readonly);
	else
		return add_ncr_scsi_hd (&ncr_a4000t, ch, NULL, ci, 1);
}

int a4091_add_scsi_unit (int ch, struct uaedev_config_info *ci, int devnum)
{
	struct ncr_state *ncr = ncra4091[devnum];
	if (ci->type == UAEDEV_CD)
		return add_ncr_scsi_cd (ncr, ch, ci->device_emu_unit);
	else if (ci->type == UAEDEV_TAPE)
		return add_ncr_scsi_tape (ncr, ch, ci->rootdir, ci->readonly);
	else
		return add_ncr_scsi_hd (ncr, ch, NULL, ci, 1);
}



#endif

