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

static uae_u8 *rom;
static int board_mask;
static int configured;
static uae_u8 acmemory[128];

static DeviceState devobject;
static SCSIDevice *scsid[8];
static SCSIBus scsibus;

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

	req->dev = d;
	req->hba_private = hba_private;
	req->bus = &scsibus;
	req->bus->qbus.parent = &devobject;
	
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
	if (lun != 0 || target < 0 || target >= 8)
		return NULL;
	return scsid[target];
}
void scsi_req_cancel(SCSIRequest *req)
{
	write_log (_T("scsi_req_cancel\n"));
}

static uae_u8 read_rombyte (uaecptr addr)
{
	uae_u8 v = rom[addr];
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

void ncr_io_bput (uaecptr addr, uae_u32 val)
{
	addr &= A4091_IO_MASK;
	lsi_mmio_write (devobject.lsistate, beswap (addr), val, 1);
}

static void ncr_bput2 (uaecptr addr, uae_u32 val)
{
	uae_u32 v = val;
	addr &= board_mask;
	if (addr < A4091_IO_OFFSET || addr >= A4091_IO_END)
		return;
	ncr_io_bput (addr, val);
}

uae_u32 ncr_io_bget (uaecptr addr)
{
	addr &= A4091_IO_MASK;
	return lsi_mmio_read (devobject.lsistate, beswap (addr), 1);
}

static uae_u32 ncr_bget2 (uaecptr addr)
{
	uae_u32 v = 0;

	addr &= board_mask;
	if (rom && addr >= ROM_VECTOR && addr < A4091_IO_OFFSET)
		return read_rombyte (addr);
	if (addr == A4091_DIP_OFFSET)
		return 0xff;
	if (addr < A4091_IO_OFFSET || addr >= A4091_IO_END)
		return v;
	addr &= A4091_IO_MASK;
	return ncr_io_bget (addr);
}

extern addrbank ncr_bank;

static uae_u32 REGPARAM2 ncr_lget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= board_mask;
	if (addr >= A4091_IO_ALT) {
		v = (ncr_bget2 (addr + 3) << 0) | (ncr_bget2 (addr + 2) << 8) |
			(ncr_bget2 (addr + 1) << 16) | (ncr_bget2 (addr + 0) << 24);
	} else {
		v = (ncr_bget2 (addr + 3) << 0) | (ncr_bget2 (addr + 2) << 8) |
			(ncr_bget2 (addr + 1) << 16) | (ncr_bget2 (addr + 0) << 24);
	}
#if NCR_DEBUG > 0
	if (addr < ROM_VECTOR)
		write_log (_T("ncr_lget %08X=%08X PC=%08X\n"), addr, v, M68K_GETPC);
#endif
	return v;
}

static uae_u32 REGPARAM2 ncr_wget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= board_mask;
	v = (ncr_bget2 (addr) << 8) | ncr_bget2 (addr + 1);
#if NCR_DEBUG > 0
	if (addr < ROM_VECTOR)
		write_log (_T("ncr_wget %08X=%04X PC=%08X\n"), addr, v, M68K_GETPC);
#endif
	return v;
}

static uae_u32 REGPARAM2 ncr_bget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= board_mask;
	if (!configured) {
		if (addr >= sizeof acmemory)
			return 0;
		return acmemory[addr];
	}
	v = ncr_bget2 (addr);
	return v;
}

static void REGPARAM2 ncr_lput (uaecptr addr, uae_u32 l)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= board_mask;
#if NCR_DEBUG > 0
	if (addr < ROM_VECTOR)
		write_log (_T("ncr_lput %08X=%08X PC=%08X\n"), addr, l, M68K_GETPC);
#endif
	if (addr >= A4091_IO_ALT) {
		ncr_bput2 (addr + 3, l >> 0);
		ncr_bput2 (addr + 2, l >> 8);
		ncr_bput2 (addr + 1, l >> 16);
		ncr_bput2 (addr + 0, l >> 24);
	} else {
		ncr_bput2 (addr + 3, l >> 0);
		ncr_bput2 (addr + 2, l >> 8);
		ncr_bput2 (addr + 1, l >> 16);
		ncr_bput2 (addr + 0, l >> 24);
	}
}

static uae_u32 expamem_hi, expamem_lo;

static void REGPARAM2 ncr_wput (uaecptr addr, uae_u32 w)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	w &= 0xffff;
	addr &= board_mask;
#if NCR_DEBUG > 0
	if (addr < ROM_VECTOR)
		write_log (_T("ncr_wput %04X=%04X PC=%08X\n"), addr, w & 65535, M68K_GETPC);
#endif
	if (!configured) {
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
				value >>= 16;
				chipmem_wput (regs.regs[11] + 0x20, value);
				chipmem_wput (regs.regs[11] + 0x28, value);
			} else {
				expamem_hi = w & 0xff00;
				value = expamem_hi | (expamem_lo >> 4);
			}
			map_banks (&ncr_bank, value, BOARD_SIZE >> 16, 0);
			board_mask = 0x00ffffff;
			write_log (_T("A4091 Z3 autoconfigured at %04X0000\n"), value);
			configured = 1;
			expamem_next();
			break;
		}
		return;
	}
	ncr_bput2 (addr, w >> 8);
	ncr_bput2 (addr + 1, w);
}

static void REGPARAM2 ncr_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	b &= 0xff;
	addr &= board_mask;
	if (!configured) {
		switch (addr)
		{
			case 0x4c:
			write_log (_T("A4091 AUTOCONFIG SHUT-UP!\n"));
			configured = 1;
			expamem_next ();
			break;
			case 0x48:
			expamem_lo = b & 0xff;
			break;
		}
		return;
	}
	ncr_bput2 (addr, b);
}

static addrbank ncr_bank = {
	ncr_lget, ncr_wget, ncr_bget,
	ncr_lput, ncr_wput, ncr_bput,
	default_xlate, default_check, NULL, _T("A4091"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO
};

static void ew (int addr, uae_u32 value)
{
	if (addr == 00 || addr == 02 || addr == 0x40 || addr == 0x42) {
		acmemory[addr] = (value & 0xf0);
		acmemory[addr + 2] = (value & 0x0f) << 4;
	} else {
		acmemory[addr] = ~(value & 0xf0);
		acmemory[addr + 2] = ~((value & 0x0f) << 4);
	}
}

void ncr_init (void)
{
	lsi_scsi_init (&devobject);
}

void ncr_autoconfig_init (void)
{
	int roms[3];
	int i;

	configured = 0;

	roms[0] = 58;
	roms[1] = 57;
	roms[2] = -1;

	memset (acmemory, 0xff, sizeof acmemory);

	struct zfile *z = read_rom_name (currprefs.a4091romfile);
	if (!z) {
		struct romlist *rl = getromlistbyids(roms);
		if (rl) {
			struct romdata *rd = rl->rd;
			z = read_rom (&rd);
		}
	}
	if (z) {
		write_log (_T("A4091 BOOT ROM '%s'\n"), zfile_getname (z));
		rom = xmalloc (uae_u8, ROM_SIZE * 4);
		for (i = 0; i < ROM_SIZE; i++) {
			uae_u8 b;
			zfile_fread (&b, 1, 1, z);
			rom[i * 4 + 0] = b;
			rom[i * 4 + 2] = b << 4;
			if (i < 0x20) {
				acmemory[i * 4 + 0] = b;
			} else if (i >= 0x40 && i < 0x60) {
				acmemory[(i - 0x40) * 4 + 2] = b;
			}
		}
		zfile_fclose(z);
	} else {
		romwarning (roms);
	}

	ncr_init ();
	map_banks (&ncr_bank, 0xe80000 >> 16, 65536 >> 16, 0);
}

static void freescsi (struct scsi_data *sd)
{
	if (!sd)
		return;
	hdf_hd_close (sd->hfd);
	scsi_free (sd);
}

static void freescsi (SCSIDevice *scsi)
{
	if (scsi) {
		freescsi ((struct scsi_data*)scsi->handle);
		xfree (scsi);
	}
}

void ncr_free (void)
{
	for (int ch = 0; ch < 8; ch++) {
		freescsi (scsid[ch]);
		scsid[ch] = NULL;
	}
}

void ncr_reset (void)
{
	configured = 0;
	board_mask = 0xffff;
	if (currprefs.cs_mbdmac == 2) {
		configured = -1;
	}
	if (devobject.lsistate)
		lsi_scsi_reset (&devobject);
}

static int add_scsi_hd (int ch, struct hd_hardfiledata *hfd, struct uaedev_config_info *ci, int scsi_level)
{
	void *handle;
	
	freescsi (scsid[ch]);
	scsid[ch] = NULL;
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
	scsid[ch] = xcalloc (SCSIDevice, 1);
	scsid[ch]->handle = handle;
	return scsid[ch] ? 1 : 0;
}


static int add_scsi_cd (int ch, int unitnum)
{
	void *handle;
	device_func_init (0);
	freescsi (scsid[ch]);
	scsid[ch] = NULL;
	handle = scsi_alloc_cd (ch, unitnum, false);
	if (!handle)
		return 0;
	scsid[ch] = xcalloc (SCSIDevice, 1);
	scsid[ch]->handle = handle;
	return scsid[ch] ? 1 : 0;
}

static int add_scsi_tape (int ch, const TCHAR *tape_directory, bool readonly)
{
	void *handle;
	freescsi (scsid[ch]);
	scsid[ch] = NULL;
	handle = scsi_alloc_tape (ch, tape_directory, readonly);
	if (!handle)
		return 0;
	scsid[ch] = xcalloc (SCSIDevice, 1);
	scsid[ch]->handle = handle;
	return scsid[ch] ? 1 : 0;
}

int a4000t_add_scsi_unit (int ch, struct uaedev_config_info *ci)
{
	if (ci->type == UAEDEV_CD)
		return add_scsi_cd (ch, ci->device_emu_unit);
	else if (ci->type == UAEDEV_TAPE)
		return add_scsi_tape (ch, ci->rootdir, ci->readonly);
	else
		return add_scsi_hd (ch, NULL, ci, 1);
}

int a4091_add_scsi_unit (int ch, struct uaedev_config_info *ci)
{
	if (ci->type == UAEDEV_CD)
		return add_scsi_cd (ch, ci->device_emu_unit);
	else if (ci->type == UAEDEV_TAPE)
		return add_scsi_tape (ch, ci->rootdir, ci->readonly);
	else
		return add_scsi_hd (ch, NULL, ci, 1);
}



#endif

