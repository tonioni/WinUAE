/*
* UAE - The Un*x Amiga Emulator
*
* SCSI and SASI emulation (not uaescsi.device)
*
* Copyright 2007-2015 Toni Wilen
*
*/

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "filesys.h"
#include "blkdev.h"
#include "zfile.h"
#include "debug.h"
#include "memory.h"
#include "scsi.h"
#include "autoconf.h"
#include "rommgr.h"
#include "newcpu.h"
#include "custom.h"
#include "gayle.h"
#include "cia.h"

#define SCSI_EMU_DEBUG 0
#define RAW_SCSI_DEBUG 1
#define NCR5380_DEBUG 0
#define NCR5380_DEBUG_IRQ 0

#define NCR5380_SUPRA 1
#define NONCR_GOLEM 2
#define NCR5380_STARDRIVE 3
#define NONCR_KOMMOS 4
#define NONCR_VECTOR 5
#define NONCR_APOLLO 6
#define NCR5380_PROTAR 7
#define NCR5380_ADD500 8
#define NCR5380_KRONOS 9
#define NCR5380_ADSCSI 10
#define NCR5380_ROCHARD 11
#define NCR5380_CLTD 12
#define NCR5380_PTNEXUS 13
#define NCR5380_DATAFLYER 14
#define NONCR_TECMAR 15
#define NCR5380_XEBEC 16
#define NONCR_MICROFORGE 17
#define NONCR_PARADOX 18
#define OMTI_HDA506 19
#define OMTI_ALF1 20
#define OMTI_PROMIGOS 21
#define OMTI_SYSTEM2000 22
#define OMTI_ADAPTER 23
#define OMTI_X86 24
#define NCR_LAST 25

extern int log_scsiemu;

static const int outcmd[] = { 0x04, 0x0a, 0x0c, 0x2a, 0xaa, 0x15, 0x55, 0x0f, -1 };
static const int incmd[] = { 0x01, 0x03, 0x08, 0x12, 0x1a, 0x5a, 0x25, 0x28, 0x34, 0x37, 0x42, 0x43, 0xa8, 0x51, 0x52, 0xbd, -1 };
static const int nonecmd[] = { 0x00, 0x05, 0x09, 0x0b, 0x11, 0x16, 0x17, 0x19, 0x1b, 0x1e, 0x2b, 0x35, 0xe0, 0xe3, 0xe4, -1 };
static const int scsicmdsizes[] = { 6, 10, 10, 12, 16, 12, 10, 6 };

static void scsi_illegal_command(struct scsi_data *sd)
{
	uae_u8 *s = sd->sense;

	memset (s, 0, sizeof (sd->sense));
	sd->status = SCSI_STATUS_CHECK_CONDITION;
	s[0] = 0x70;
	s[2] = 5; /* ILLEGAL REQUEST */
	s[12] = 0x24; /* ILLEGAL FIELD IN CDB */
	sd->sense_len = 0x12;
}

static void scsi_grow_buffer(struct scsi_data *sd, int newsize)
{
	if (sd->buffer_size >= newsize)
		return;
	uae_u8 *oldbuf = sd->buffer;
	int oldsize = sd->buffer_size;
	sd->buffer_size = newsize + SCSI_DEFAULT_DATA_BUFFER_SIZE;
	write_log(_T("SCSI buffer %d -> %d\n"), oldsize, sd->buffer_size);
	sd->buffer = xmalloc(uae_u8, sd->buffer_size);
	memcpy(sd->buffer, oldbuf, oldsize);
	xfree(oldbuf);
}

static int scsi_data_dir(struct scsi_data *sd)
{
	int i;
	uae_u8 cmd;

	cmd = sd->cmd[0];
	for (i = 0; outcmd[i] >= 0; i++) {
		if (cmd == outcmd[i]) {
			return 1;
		}
	}
	for (i = 0; incmd[i] >= 0; i++) {
		if (cmd == incmd[i]) {
			return -1;
		}
	}
	for (i = 0; nonecmd[i] >= 0; i++) {
		if (cmd == nonecmd[i]) {
			return 0;
		}
	}
	write_log (_T("SCSI command %02X, no direction specified!\n"), sd->cmd[0]);
	return 0;
}

bool scsi_emulate_analyze (struct scsi_data *sd)
{
	int cmd_len, data_len, data_len2, tmp_len;

	data_len = sd->data_len;
	data_len2 = 0;
	cmd_len = scsicmdsizes[sd->cmd[0] >> 5];
	if (sd->hfd && sd->hfd->ansi_version < 2 && cmd_len > 10)
		goto nocmd;
	sd->cmd_len = cmd_len;
	switch (sd->cmd[0])
	{
	case 0x04: // FORMAT UNIT
		if (sd->device_type == UAEDEV_CD)
			goto nocmd;
		// FmtData set?
		if (sd->cmd[1] & 0x10) {
			int cl = (sd->cmd[1] & 8) != 0;
			int dlf = sd->cmd[1] & 7;
			data_len2 = 4;
		} else {
			sd->direction = 0;
			sd->data_len = 0;
			return true;
		}
	break;
	case 0x0c: // INITIALIZE DRIVE CHARACTERICS (SASI)
		if (sd->hfd && sd->hfd->hfd.ci.unit_feature_level < HD_LEVEL_SASI)
			goto nocmd;
		data_len = 8;
	break;
	case 0x08: // READ(6)
		data_len2 = sd->cmd[4] * sd->blocksize;
		scsi_grow_buffer(sd, data_len2);
	break;
	case 0x28: // READ(10)
		data_len2 = ((sd->cmd[7] << 8) | (sd->cmd[8] << 0)) * (uae_s64)sd->blocksize;
		scsi_grow_buffer(sd, data_len2);
	break;
	case 0xa8: // READ(12)
		data_len2 = ((sd->cmd[6] << 24) | (sd->cmd[7] << 16) | (sd->cmd[8] << 8) | (sd->cmd[9] << 0)) * (uae_s64)sd->blocksize;
		scsi_grow_buffer(sd, data_len2);
	break;
	case 0x0f: // WRITE SECTOR BUFFER
		data_len = sd->blocksize;
		scsi_grow_buffer(sd, data_len);
	break;
	case 0x0a: // WRITE(6)
		if (sd->device_type == UAEDEV_CD)
			goto nocmd;
		data_len = sd->cmd[4] * sd->blocksize;
		scsi_grow_buffer(sd, data_len);
	break;
	case 0x2a: // WRITE(10)
		if (sd->device_type == UAEDEV_CD)
			goto nocmd;
		data_len = ((sd->cmd[7] << 8) | (sd->cmd[8] << 0)) * (uae_s64)sd->blocksize;
		scsi_grow_buffer(sd, data_len);
	break;
	case 0xaa: // WRITE(12)
		if (sd->device_type == UAEDEV_CD)
			goto nocmd;
		data_len = ((sd->cmd[6] << 24) | (sd->cmd[7] << 16) | (sd->cmd[8] << 8) | (sd->cmd[9] << 0)) * (uae_s64)sd->blocksize;
		scsi_grow_buffer(sd, data_len);
	break;
	case 0xbe: // READ CD
	case 0xb9: // READ CD MSF
		if (sd->device_type != UAEDEV_CD)
			goto nocmd;
		tmp_len = (sd->cmd[6] << 16) | (sd->cmd[7] << 8) | sd->cmd[8];
		// max block transfer size, it is usually smaller.
		tmp_len *= 2352 + 96;
		scsi_grow_buffer(sd, tmp_len);
	break;
	case 0x2f: // VERIFY
		if (sd->cmd[1] & 2) {
			sd->data_len = ((sd->cmd[7] << 8) | (sd->cmd[8] << 0)) * (uae_s64)sd->blocksize;
			scsi_grow_buffer(sd, sd->data_len);
			sd->direction = 1;
		} else {
			sd->data_len = 0;
			sd->direction = 0;
		}
		return true;
	case 0x15: // MODE SELECT
	case 0x55: 
	if (sd->device_type != UAEDEV_CD && sd->device_type != UAEDEV_TAPE)
		goto nocmd;
	break;
	}
	if (data_len < 0) {
		if (cmd_len == 6)
			sd->data_len = sd->cmd[4];
		else
			sd->data_len = (sd->cmd[7] << 8) | sd->cmd[8];
	} else {
		sd->data_len = data_len;
	}
	sd->direction = scsi_data_dir (sd);
	return true;
nocmd:
	sd->status = SCSI_STATUS_CHECK_CONDITION;
	sd->direction = 0;
	scsi_illegal_command(sd);
	return false;
}

void scsi_illegal_lun(struct scsi_data *sd)
{
	uae_u8 *s = sd->sense;

	memset (s, 0, sizeof (sd->sense));
	sd->status = SCSI_STATUS_CHECK_CONDITION;
	s[0] = 0x70;
	s[2] = SCSI_SK_ILLEGAL_REQ;
	s[12] = SCSI_INVALID_LUN;
	sd->sense_len = 0x12;
}

void scsi_clear_sense(struct scsi_data *sd)
{
	memset (sd->sense, 0, sizeof (sd->sense));
	memset (sd->reply, 0, sizeof (sd->reply));
	sd->sense[0] = 0x70;
}
static void showsense(struct scsi_data *sd)
{
	if (log_scsiemu) {
		for (int i = 0; i < sd->sense_len; i++) {
			if (i > 0)
				write_log (_T("."));
			write_log (_T("%02X"), sd->buffer[i]);
		}
		write_log (_T("\n"));
	}
}
static void copysense(struct scsi_data *sd)
{
	int len = sd->cmd[4];
	if (log_scsiemu)
		write_log (_T("REQUEST SENSE length %d (%d)\n"), len, sd->sense_len);
	if (len == 0)
		len = 4;
	memset(sd->buffer, 0, len);
	memcpy(sd->buffer, sd->sense, sd->sense_len > len ? len : sd->sense_len);
	if (len > 7 && sd->sense_len > 7)
		sd->buffer[7] = sd->sense_len - 8;
	if (sd->sense_len == 0)
		sd->buffer[0] = 0x70;
	showsense (sd);
	sd->data_len = len;
	scsi_clear_sense(sd);
}
static void copyreply(struct scsi_data *sd)
{
	if (sd->status == 0 && sd->reply_len > 0) {
		memset(sd->buffer, 0, 256);
		memcpy(sd->buffer, sd->reply, sd->reply_len);
		sd->data_len = sd->reply_len;
	}
}

static void scsi_set_unit_attention(struct scsi_data *sd, uae_u8 v1, uae_u8 v2)
{
	sd->unit_attention = (v1 << 8) | v2;
}

static void scsi_emulate_reset_device(struct scsi_data *sd)
{
	if (!sd)
		return;
	if (sd->device_type == UAEDEV_HDF && sd->nativescsiunit < 0) {
		scsi_clear_sense(sd);
		//  SCSI bus reset occurred
		scsi_set_unit_attention(sd, 0x29, 0x02);
	}
}

static bool handle_ca(struct scsi_data *sd)
{
	bool cc = sd->sense_len > 2 && sd->sense[2] >= 2;
	bool ua = sd->unit_attention != 0;
	uae_u8 cmd = sd->cmd[0];

	// INQUIRY
	if (cmd == 0x12) {
		if (ua && cc && sd->sense[2] == 6) {
			// INQUIRY clears UA only if previous
			// command was aborted due to UA
			sd->unit_attention = 0;
		}
		memset(sd->reply, 0, sizeof(sd->reply));
		return true;
	}

	// REQUEST SENSE
	if (cmd == 0x03) {
		if (ua) {
			uae_u8 *s = sd->sense;
			scsi_clear_sense(sd);
			s[0] = 0x70;
			s[2] = 6; /* UNIT ATTENTION */
			s[12] = (sd->unit_attention >> 8) & 0xff;
			s[13] = (sd->unit_attention >> 0) & 0xff;
			sd->sense_len = 0x12;
		}
		sd->unit_attention = 0;
		return true;
	}

	scsi_clear_sense(sd);

	if (ua) {
		uae_u8 *s = sd->sense;
		s[0] = 0x70;
		s[2] = 6; /* UNIT ATTENTION */
		s[12] = (sd->unit_attention >> 8) & 0xff;
		s[13] = (sd->unit_attention >> 0) & 0xff;
		sd->sense_len = 0x12;
		sd->unit_attention = 0;
		sd->status = 2;
		return false;
	}

	return true;
}

void scsi_emulate_cmd(struct scsi_data *sd)
{
	sd->status = 0;
	if ((sd->message[0] & 0xc0) == 0x80 && (sd->message[0] & 0x1f)) {
		uae_u8 lun = sd->message[0] & 0x1f;
		if (lun > 7)
			lun = 7;
		sd->cmd[1] &= ~(7 << 5);
		sd->cmd[1] |= lun << 5;
	}
#if SCSI_EMU_DEBUG
	write_log (_T("CMD=%02x.%02x.%02x.%02x.%02x.%02x (%d,%d)\n"),
		sd->cmd[0], sd->cmd[1], sd->cmd[2], sd->cmd[3], sd->cmd[4], sd->cmd[5], sd->device_type, sd->nativescsiunit);
#endif
	if (sd->device_type == UAEDEV_CD && sd->cd_emu_unit >= 0) {
		uae_u32 ua = 0;
		ua = scsi_cd_emulate(sd->cd_emu_unit, NULL, 0, 0, 0, 0, 0, 0, 0, sd->atapi);
		if (ua)
			sd->unit_attention = ua;
		if (handle_ca(sd)) {
			if (sd->cmd[0] == 0x03) { /* REQUEST SENSE */
				scsi_cd_emulate(sd->cd_emu_unit, sd->cmd, 0, 0, 0, 0, 0, 0, 0, sd->atapi); /* ack request sense */
				copysense(sd);
			} else {
				sd->status = scsi_cd_emulate(sd->cd_emu_unit, sd->cmd, sd->cmd_len, sd->buffer, &sd->data_len, sd->reply, &sd->reply_len, sd->sense, &sd->sense_len, sd->atapi);
				copyreply(sd);
			}
		}
	} else if (sd->device_type == UAEDEV_HDF && sd->nativescsiunit < 0) {
		uae_u32 ua = 0;
		ua = scsi_hd_emulate(&sd->hfd->hfd, sd->hfd, NULL, 0, 0, 0, 0, 0, 0, 0);
		if (ua)
			sd->unit_attention = ua;
		if (handle_ca(sd)) {
			if (sd->cmd[0] == 0x03) { /* REQUEST SENSE */
				scsi_hd_emulate(&sd->hfd->hfd, sd->hfd, sd->cmd, 0, 0, 0, 0, 0, sd->sense, &sd->sense_len);
				copysense(sd);
			} else {
				sd->status = scsi_hd_emulate(&sd->hfd->hfd, sd->hfd,
					sd->cmd, sd->cmd_len, sd->buffer, &sd->data_len, sd->reply, &sd->reply_len, sd->sense, &sd->sense_len);
				copyreply(sd);
			}
		}
	} else if (sd->device_type == UAEDEV_TAPE && sd->nativescsiunit < 0) {
		uae_u32 ua = 0;
		ua = scsi_tape_emulate(sd->tape, NULL, 0, 0, 0, 0, 0, 0, 0);
		if (ua)
			sd->unit_attention = ua;
		if (handle_ca(sd)) {
			if (sd->cmd[0] == 0x03) { /* REQUEST SENSE */
				scsi_tape_emulate(sd->tape, sd->cmd, 0, 0, 0, sd->reply, &sd->reply_len, sd->sense, &sd->sense_len); /* get request sense extra bits */
				copysense(sd);
			} else {
				sd->status = scsi_tape_emulate(sd->tape,
					sd->cmd, sd->cmd_len, sd->buffer, &sd->data_len, sd->reply, &sd->reply_len, sd->sense, &sd->sense_len);
				copyreply(sd);
			}
		}
	} else if (sd->nativescsiunit >= 0) {
		struct amigascsi as;

		memset(sd->sense, 0, 256);
		memset(&as, 0, sizeof as);
		memcpy (&as.cmd, sd->cmd, sd->cmd_len);
		as.flags = 2 | 1;
		if (sd->direction > 0)
			as.flags &= ~1;
		as.sense_len = 32;
		as.cmd_len = sd->cmd_len;
		as.data = sd->buffer;
		as.len = sd->direction < 0 ? DEVICE_SCSI_BUFSIZE : sd->data_len;
		sys_command_scsi_direct_native(sd->nativescsiunit, -1, &as);
		sd->status = as.status;
		sd->data_len = as.len;
		if (sd->status) {
			sd->direction = 0;
			sd->data_len = 0;
			memcpy(sd->sense, as.sensedata, as.sense_len);
		}
	}
	sd->offset = 0;
}

static void allocscsibuf(struct scsi_data *sd)
{
	sd->buffer_size = SCSI_DEFAULT_DATA_BUFFER_SIZE;
	sd->buffer = xcalloc(uae_u8, sd->buffer_size);
}

struct scsi_data *scsi_alloc_hd(int id, struct hd_hardfiledata *hfd)
{
	struct scsi_data *sd = xcalloc (struct scsi_data, 1);
	sd->hfd = hfd;
	sd->id = id;
	sd->nativescsiunit = -1;
	sd->cd_emu_unit = -1;
	sd->blocksize = hfd->hfd.ci.blocksize;
	sd->device_type = UAEDEV_HDF;
	allocscsibuf(sd);
	return sd;
}

struct scsi_data *scsi_alloc_cd(int id, int unitnum, bool atapi)
{
	struct scsi_data *sd;
	if (!sys_command_open (unitnum)) {
		write_log (_T("SCSI: CD EMU scsi unit %d failed to open\n"), unitnum);
		return NULL;
	}
	sd = xcalloc (struct scsi_data, 1);
	sd->id = id;
	sd->cd_emu_unit = unitnum;
	sd->nativescsiunit = -1;
	sd->atapi = atapi;
	sd->blocksize = 2048;
	sd->device_type = UAEDEV_CD;
	allocscsibuf(sd);
	return sd;
}

struct scsi_data *scsi_alloc_tape(int id, const TCHAR *tape_directory, bool readonly)
{
	struct scsi_data_tape *tape;
	tape = tape_alloc (id, tape_directory, readonly);
	if (!tape)
		return NULL;
	struct scsi_data *sd = xcalloc (struct scsi_data, 1);
	sd->id = id;
	sd->nativescsiunit = -1;
	sd->cd_emu_unit = -1;
	sd->blocksize = tape->blocksize;
	sd->tape = tape;
	sd->device_type = UAEDEV_TAPE;
	allocscsibuf(sd);
	return sd;
}

struct scsi_data *scsi_alloc_native(int id, int nativeunit)
{
	struct scsi_data *sd;
	if (!sys_command_open (nativeunit)) {
		write_log (_T("SCSI: native scsi unit %d failed to open\n"), nativeunit);
		return NULL;
	}
	sd = xcalloc (struct scsi_data, 1);
	sd->id = id;
	sd->nativescsiunit = nativeunit;
	sd->cd_emu_unit = -1;
	sd->blocksize = 2048;
	sd->device_type = 0;
	allocscsibuf(sd);
	return sd;
}

void scsi_reset(void)
{
	//device_func_init (DEVICE_TYPE_SCSI);
}

void scsi_free(struct scsi_data *sd)
{
	if (!sd)
		return;
	if (sd->nativescsiunit >= 0) {
		sys_command_close (sd->nativescsiunit);
		sd->nativescsiunit = -1;
	}
	if (sd->cd_emu_unit >= 0) {
		sys_command_close (sd->cd_emu_unit);
		sd->cd_emu_unit = -1;
	}
	tape_free (sd->tape);
	xfree(sd->buffer);
	xfree(sd);
}

void scsi_start_transfer(struct scsi_data *sd)
{
	sd->offset = 0;
}

int scsi_send_data(struct scsi_data *sd, uae_u8 b)
{
	if (sd->direction == 1) {
		if (sd->offset >= sd->buffer_size) {
			write_log (_T("SCSI data buffer overflow!\n"));
			return 0;
		}
		sd->buffer[sd->offset++] = b;
	} else if (sd->direction == 2) {
		if (sd->offset >= 16) {
			write_log (_T("SCSI command buffer overflow!\n"));
			return 0;
		}
		sd->cmd[sd->offset++] = b;
		if (sd->offset == sd->cmd_len)
			return 1;
	} else {
		write_log (_T("scsi_send_data() without direction! (%02X)\n"), sd->cmd[0]);
		return 0;
	}
	if (sd->offset == sd->data_len)
		return 1;
	return 0;
}

int scsi_receive_data(struct scsi_data *sd, uae_u8 *b, bool next)
{
	if (!sd->data_len)
		return -1;
	*b = sd->buffer[sd->offset];
	if (next) {
		sd->offset++;
		if (sd->offset == sd->data_len)
			return 1; // requested length got
	}
	return 0;
}

void free_scsi (struct scsi_data *sd)
{
	if (!sd)
		return;
	hdf_hd_close (sd->hfd);
	scsi_free (sd);
}

int add_scsi_hd (struct scsi_data **sd, int ch, struct hd_hardfiledata *hfd, struct uaedev_config_info *ci)
{
	free_scsi (*sd);
	*sd = NULL;
	if (!hfd) {
		hfd = xcalloc (struct hd_hardfiledata, 1);
		memcpy (&hfd->hfd.ci, ci, sizeof (struct uaedev_config_info));
	}
	if (!hdf_hd_open (hfd))
		return 0;
	hfd->ansi_version = ci->unit_feature_level + 1;
	*sd = scsi_alloc_hd (ch, hfd);
	return *sd ? 1 : 0;
}

int add_scsi_cd (struct scsi_data **sd, int ch, int unitnum)
{
	device_func_init (0);
	free_scsi (*sd);
	*sd = scsi_alloc_cd (ch, unitnum, false);
	return *sd ? 1 : 0;
}

int add_scsi_tape (struct scsi_data **sd, int ch, const TCHAR *tape_directory, bool readonly)
{
	free_scsi (*sd);
	*sd = scsi_alloc_tape (ch, tape_directory, readonly);
	return *sd ? 1 : 0;
}

int add_scsi_device(struct scsi_data **sd, int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	if (ci->type == UAEDEV_CD)
		return add_scsi_cd(sd, ch, ci->device_emu_unit);
	else if (ci->type == UAEDEV_TAPE)
		return add_scsi_tape(sd, ch, ci->rootdir, ci->readonly);
	else if (ci->type == UAEDEV_HDF)
		return add_scsi_hd(sd, ch, NULL, ci);
	return 0;
}

void scsi_freenative(struct scsi_data **sd, int max)
{
	for (int i = 0; i < max; i++) {
		free_scsi (sd[i]);
		sd[i] = NULL;
	}
}

void scsi_addnative(struct scsi_data **sd)
{
	int i, j;
	int devices[MAX_TOTAL_SCSI_DEVICES];
	int types[MAX_TOTAL_SCSI_DEVICES];
	struct device_info dis[MAX_TOTAL_SCSI_DEVICES];

	scsi_freenative (sd, MAX_TOTAL_SCSI_DEVICES);
	i = 0;
	while (i < MAX_TOTAL_SCSI_DEVICES) {
		types[i] = -1;
		devices[i] = -1;
		if (sys_command_open (i)) {
			if (sys_command_info (i, &dis[i], 0)) {
				devices[i] = i;
				types[i] = 100 - i;
				if (dis[i].type == INQ_ROMD)
					types[i] = 1000 - i;
			}
			sys_command_close (i);
		}
		i++;
	}
	i = 0;
	while (devices[i] >= 0) {
		j = i + 1;
		while (devices[j] >= 0) {
			if (types[i] > types[j]) {
				int tmp = types[i];
				types[i] = types[j];
				types[j] = tmp;
			}
			j++;
		}
		i++;
	}
	i = 0; j = 0;
	while (devices[i] >= 0 && j < 7) {
		if (sd[j] == NULL) {
			sd[j] = scsi_alloc_native(j, devices[i]);
			write_log (_T("SCSI: %d:'%s'\n"), j, dis[i].label);
			i++;
		}
		j++;
	}
}

// raw scsi

#define SCSI_IO_BUSY 0x80
#define SCSI_IO_ATN 0x40
#define SCSI_IO_SEL 0x20
#define SCSI_IO_REQ 0x10
#define SCSI_IO_DIRECTION 0x01
#define SCSI_IO_COMMAND 0x02
#define SCSI_IO_MESSAGE 0x04

#define SCSI_SIGNAL_PHASE_FREE -1
#define SCSI_SIGNAL_PHASE_ARBIT -2
#define SCSI_SIGNAL_PHASE_SELECT_1 -3
#define SCSI_SIGNAL_PHASE_SELECT_2 -4

#define SCSI_SIGNAL_PHASE_DATA_OUT 0
#define SCSI_SIGNAL_PHASE_DATA_IN 1
#define SCSI_SIGNAL_PHASE_COMMAND 2
#define SCSI_SIGNAL_PHASE_STATUS 3
#define SCSI_SIGNAL_PHASE_MESSAGE_OUT 6
#define SCSI_SIGNAL_PHASE_MESSAGE_IN 7

struct raw_scsi
{
	int io;
	int bus_phase;
	bool atn;
	bool ack;
	bool wait_ack;
	uae_u8 data_write;
	uae_u8 status;
	bool databusoutput;
	int initiator_id, target_id;
	struct scsi_data *device[MAX_TOTAL_SCSI_DEVICES];
	struct scsi_data *target;
	int msglun;
};

struct soft_scsi
{
	uae_u8 regs[8 + 1];
	uae_u8 regs_400[2];
	int c400_count;
	struct raw_scsi rscsi;
	bool irq;
	bool intena;
	bool level6;
	bool enabled;
	bool delayed_irq;
	bool configured;
	uae_u8 acmemory[128];
	uaecptr baseaddress;
	uaecptr baseaddress2;
	uae_u8 *rom;
	int rom_size;
	int board_mask;
	int board_size;
	addrbank *bank;
	int type;
	int subtype;
	int dma_direction;
	bool dma_active;
	bool dma_controller;
	struct romconfig *rc;
	struct soft_scsi **self_ptr;

	int dmac_direction;
	uaecptr dmac_address;
	int dmac_length;
	int dmac_active;
	int chip_state;

	// add500
	uae_u16 databuffer[2];
	bool databuffer_empty;

	// kronos
	uae_u8 *databufferptr;
	int databuffer_size;
	int db_read_index;
	int db_write_index;

	// sasi
	bool active_select;
	bool wait_select;
};


#define MAX_SOFT_SCSI_UNITS 10
static struct soft_scsi *soft_scsi_devices[MAX_SOFT_SCSI_UNITS];
static struct soft_scsi *soft_scsi_units[NCR_LAST * MAX_DUPLICATE_EXPANSION_BOARDS];
bool parallel_port_scsi;
static struct soft_scsi *parallel_port_scsi_data;
static struct soft_scsi *x86_hd_data;

static void soft_scsi_free_unit(struct soft_scsi *s)
{
	if (!s)
		return;
	struct raw_scsi *rs = &s->rscsi;
	for (int j = 0; j < 8; j++) {
		free_scsi (rs->device[j]);
		rs->device[j] = NULL;
	}
	xfree(s->databufferptr);
	xfree(s->rom);
	if (s->self_ptr)
		*s->self_ptr = NULL;
	xfree(s);
}

static void freescsi(struct soft_scsi **ncr)
{
	if (!ncr)
		return;
	for (int i = 0; i < MAX_SOFT_SCSI_UNITS; i++) {
		if (soft_scsi_devices[i] == *ncr) {
			soft_scsi_devices[i] = NULL;
		}
	}
	if (*ncr) {
		soft_scsi_free_unit(*ncr);
	}
	*ncr = NULL;
}

static struct soft_scsi *allocscsi(struct soft_scsi **ncr, struct romconfig *rc, int ch)
{
	struct soft_scsi *scsi;

	if (ch < 0) {
		freescsi(ncr);
		*ncr = NULL;
	}
	if ((*ncr) == NULL) {
		scsi = xcalloc(struct soft_scsi, 1);
		for (int i = 0; i < MAX_SOFT_SCSI_UNITS; i++) {
			if (soft_scsi_devices[i] == NULL) {
				soft_scsi_devices[i] = scsi;
				rc->unitdata = scsi;
				scsi->rc = rc;
				scsi->self_ptr = ncr;
				if (ncr)
					*ncr = scsi;
				return scsi;
			}
		}
	}
	return *ncr;
}

static struct soft_scsi *getscsi(struct romconfig *rc)
{
	if (rc->unitdata)
		return (struct soft_scsi*)rc->unitdata;
	return NULL;
}


static struct soft_scsi *getscsiboard(uaecptr addr)
{
	for (int i = 0; soft_scsi_devices[i]; i++) {
		struct soft_scsi *s = soft_scsi_devices[i];
		if (!s->baseaddress && !s->configured)
			return s;
		if ((addr & ~s->board_mask) == s->baseaddress)
			return s;
		if (s->baseaddress2 && (addr & ~s->board_mask) == s->baseaddress2)
			return s;
		if (s->baseaddress == 0xe80000 && addr < 65536)
			return s;
	}
	return NULL;
}

static void raw_scsi_reset(struct raw_scsi *rs)
{
	rs->target = NULL;
	rs->io = 0;
	rs->bus_phase = SCSI_SIGNAL_PHASE_FREE;
}

extern addrbank soft_bank_generic;

static void ew(struct soft_scsi *scsi, int addr, uae_u32 value)
{
	addr &= 0xffff;
	if (addr == 00 || addr == 02 || addr == 0x40 || addr == 0x42) {
		scsi->acmemory[addr] = (value & 0xf0);
		scsi->acmemory[addr + 2] = (value & 0x0f) << 4;
	} else {
		scsi->acmemory[addr] = ~(value & 0xf0);
		scsi->acmemory[addr + 2] = ~((value & 0x0f) << 4);
	}
}

static void generic_soft_scsi_add(int ch, struct uaedev_config_info *ci, struct romconfig *rc, int type, int boardsize, int romsize, int romtype)
{
	struct soft_scsi *ss = allocscsi(&soft_scsi_units[type * MAX_DUPLICATE_EXPANSION_BOARDS + ci->controller_type_unit], rc, ch);
	ss->type = type;
	ss->configured = 0;
	ss->bank = &soft_bank_generic;
	ss->subtype = rc->subtype;
	ss->intena = false;
	if (boardsize > 0) {
		ss->board_size = boardsize;
		ss->board_mask = ss->board_size - 1;
	}
	if (!ss->board_mask && !ss->board_size) {
		ss->board_size = 0x10000;
		ss->board_mask = 0xffff;
	}
	if (romsize >= 0) {
		ss->rom_size = romsize;
		xfree(ss->rom);
		ss->rom = NULL;
		if (romsize > 0) {
			ss->rom = xcalloc(uae_u8, ss->rom_size);
			memset(ss->rom, 0xff, ss->rom_size);
		}
	}
	memset(ss->acmemory, 0xff, sizeof ss->acmemory);
	const struct expansionromtype *ert = get_device_expansion_rom(romtype);
	if (ert) {
		const uae_u8 *ac = NULL;
		if (ert->subtypes)
			ac = ert->subtypes[rc->subtype].autoconfig;
		else 
			ac = ert->autoconfig;
		if (ac[0]) {
			for (int i = 0; i < 16; i++) {
				uae_u8 b = ac[i];
				ew(ss, i * 4, b);
			}
		}
	}
	raw_scsi_reset(&ss->rscsi);
	if (ch < 0)
		return;
	add_scsi_device(&ss->rscsi.device[ch], ch, ci, rc);
}

static void raw_scsi_busfree(struct raw_scsi *rs)
{
	rs->target = NULL;
	rs->io = 0;
	rs->bus_phase = SCSI_SIGNAL_PHASE_FREE;
}

static void bus_free(struct raw_scsi *rs)
{
	rs->bus_phase = SCSI_SIGNAL_PHASE_FREE;
	rs->io = 0;
}

static int getbit(uae_u8 v)
{
	for (int i = 7; i >= 0; i--) {
		if ((1 << i) & v)
			return i;
	}
	return -1;
}
static int countbits(uae_u8 v)
{
	int cnt = 0;
	for (int i = 7; i >= 0; i--) {
		if ((1 << i) & v)
			cnt++;
	}
	return cnt;
}

static void raw_scsi_reset_bus(struct soft_scsi *scsi)
{
	struct raw_scsi *r = &scsi->rscsi;
#if RAW_SCSI_DEBUG
	write_log(_T("SCSI BUS reset\n"));
#endif
	for (int i = 0; i < 8; i++) {
		scsi_emulate_reset_device(r->device[i]);
	}
}


static void raw_scsi_set_databus(struct raw_scsi *rs, bool databusoutput)
{
	rs->databusoutput = databusoutput;
}

static void raw_scsi_set_signal_phase(struct raw_scsi *rs, bool busy, bool select, bool atn)
{
	switch (rs->bus_phase)
	{
		case SCSI_SIGNAL_PHASE_FREE:
		if (busy && !select && !rs->databusoutput) {
			if (countbits(rs->data_write) != 1) {
#if RAW_SCSI_DEBUG
				write_log(_T("raw_scsi: invalid arbitration scsi id mask! (%02x)\n"), rs->data_write);
#endif
				return;
			}
			rs->bus_phase = SCSI_SIGNAL_PHASE_ARBIT;
			rs->initiator_id = getbit(rs->data_write);
#if RAW_SCSI_DEBUG
			write_log(_T("raw_scsi: arbitration initiator id %d (%02x)\n"), rs->initiator_id, rs->data_write);
#endif
		} else if (!busy && select) {
			if (countbits(rs->data_write) > 2 || rs->data_write == 0) {
#if RAW_SCSI_DEBUG
				write_log(_T("raw_scsi: invalid scsi id selected mask (%02x)\n"), rs->data_write);
#endif
				return;
			}
			rs->initiator_id = -1;
			rs->bus_phase = SCSI_SIGNAL_PHASE_SELECT_1;
#if RAW_SCSI_DEBUG
			write_log(_T("raw_scsi: selected scsi id mask (%02x)\n"), rs->data_write);
#endif
			raw_scsi_set_signal_phase(rs, busy, select, atn);
		}
		break;
		case SCSI_SIGNAL_PHASE_ARBIT:
		rs->target_id = -1;
		rs->target = NULL;
		if (busy && select) {
			rs->bus_phase = SCSI_SIGNAL_PHASE_SELECT_1;
		}
		break;
		case SCSI_SIGNAL_PHASE_SELECT_1:
		rs->atn = atn;
		rs->msglun = -1;
		rs->target_id = -1;
		if (!busy) {
			for (int i = 0; i < 8; i++) {
				if (i == rs->initiator_id)
					continue;
				if ((rs->data_write & (1 << i)) && rs->device[i]) {
					rs->target_id = i;
					rs->target = rs->device[rs->target_id];
#if RAW_SCSI_DEBUG
					write_log(_T("raw_scsi: selected id %d\n"), rs->target_id);
#endif
					rs->io |= SCSI_IO_BUSY;
				}
			}
#if RAW_SCSI_DEBUG
			if (rs->target_id < 0) {
				for (int i = 0; i < 8; i++) {
					if (i == rs->initiator_id)
						continue;
					if ((rs->data_write & (1 << i)) && !rs->device[i]) {
						write_log(_T("raw_scsi: selected non-existing id %d\n"), i);
					}
				}
			}
#endif
			if (rs->target_id >= 0) {
				rs->bus_phase = SCSI_SIGNAL_PHASE_SELECT_2;
			} else {
				if (!select) {
					rs->bus_phase = SCSI_SIGNAL_PHASE_FREE;
				}
			}
		}
		break;
		case SCSI_SIGNAL_PHASE_SELECT_2:
		if (!select) {
			scsi_start_transfer(rs->target);
			rs->bus_phase = rs->atn ? SCSI_SIGNAL_PHASE_MESSAGE_OUT : SCSI_SIGNAL_PHASE_COMMAND;
			rs->io = SCSI_IO_BUSY | SCSI_IO_REQ;
		}
		break;
	}
}

static uae_u8 raw_scsi_get_signal_phase(struct raw_scsi *rs)
{
	uae_u8 v = rs->io;
	if (rs->bus_phase >= 0)
		v |= rs->bus_phase;
	if (rs->ack)
		v &= ~SCSI_IO_REQ;
	return v;
}

static uae_u8 raw_scsi_get_data_2(struct raw_scsi *rs, bool next, bool nodebug)
{
	struct scsi_data *sd = rs->target;
	uae_u8 v = 0;

	switch (rs->bus_phase)
	{
		case SCSI_SIGNAL_PHASE_FREE:
		v = 0;
		break;
		case SCSI_SIGNAL_PHASE_ARBIT:
#if RAW_SCSI_DEBUG
		write_log(_T("raw_scsi: arbitration\n"));
#endif
		v = rs->data_write;
		break;
		case SCSI_SIGNAL_PHASE_DATA_IN:
#if RAW_SCSI_DEBUG > 2
		scsi_receive_data(sd, &v, false);
		write_log(_T("raw_scsi: read data byte %02x (%d/%d)\n"), v, sd->offset, sd->data_len);
#endif
		if (scsi_receive_data(sd, &v, next)) {
			write_log(_T("raw_scsi: data in finished, %d bytes: status phase\n"), sd->offset);
			rs->bus_phase = SCSI_SIGNAL_PHASE_STATUS;
		}
		break;
		case SCSI_SIGNAL_PHASE_STATUS:
#if RAW_SCSI_DEBUG
		if (!nodebug)
			write_log(_T("raw_scsi: status byte read %02x\n"), sd->status);
#endif
		v = sd->status;
		if (next) {
			sd->status = 0;
			rs->bus_phase = SCSI_SIGNAL_PHASE_MESSAGE_IN;
		}
		break;
		case SCSI_SIGNAL_PHASE_MESSAGE_IN:
#if RAW_SCSI_DEBUG
		if (!nodebug)
			write_log(_T("raw_scsi: message byte read %02x\n"), sd->status);
#endif
		v = sd->status;
		rs->status = v;
		if (next) {
			bus_free(rs);
		}
		break;
		default:
		write_log(_T("raw_scsi_get_data but bus phase is %d!\n"), rs->bus_phase);
		break;
	}

	return v;
}

static uae_u8 raw_scsi_get_data(struct raw_scsi *rs, bool next)
{
	return raw_scsi_get_data_2(rs, next, true);
}


static int getmsglen(uae_u8 *msgp, int len)
{
	uae_u8 msg = msgp[0];
	if (msg == 0 || (msg >= 0x02 && msg <= 0x1f) ||msg >= 0x80)
		return 1;
	if (msg >= 0x02 && msg <= 0x1f)
		return 2;
	if (msg >= 0x20 && msg <= 0x2f)
		return 3;
	// extended message, at least 3 bytes
	if (len < 2)
		return 3;
	return msgp[1];
}

static void raw_scsi_write_data(struct raw_scsi *rs, uae_u8 data)
{
	struct scsi_data *sd = rs->target;
	int len;

	switch (rs->bus_phase)
	{
		case SCSI_SIGNAL_PHASE_SELECT_1:
		case SCSI_SIGNAL_PHASE_FREE:
		break;
		case SCSI_SIGNAL_PHASE_COMMAND:
		sd->cmd[sd->offset++] = data;
		len = scsicmdsizes[sd->cmd[0] >> 5];
#if RAW_SCSI_DEBUG > 1
		write_log(_T("raw_scsi: got command byte %02x (%d/%d)\n"), data, sd->offset, len);
#endif
		if (sd->offset >= len) {
			if (rs->msglun >= 0) {
				sd->cmd[1] &= ~(0x80 | 0x40 | 0x20);
				sd->cmd[1] |= rs->msglun << 5;
			}
			scsi_emulate_analyze(rs->target);
			if (sd->direction > 0) {
#if RAW_SCSI_DEBUG
				write_log(_T("raw_scsi: data out %d bytes required\n"), sd->data_len);
#endif
				scsi_start_transfer(sd);
				rs->bus_phase = SCSI_SIGNAL_PHASE_DATA_OUT;
			} else if (sd->direction <= 0) {
				scsi_emulate_cmd(sd);
				scsi_start_transfer(sd);
				if (!sd->status && sd->data_len > 0) {
#if RAW_SCSI_DEBUG
					write_log(_T("raw_scsi: data in %d bytes waiting\n"), sd->data_len);
#endif
					rs->bus_phase = SCSI_SIGNAL_PHASE_DATA_IN;
				} else {
#if RAW_SCSI_DEBUG
					write_log(_T("raw_scsi: no data, status = %d\n"), sd->status);
#endif
					rs->bus_phase = SCSI_SIGNAL_PHASE_STATUS;
				}
			}
		}
		break;
		case SCSI_SIGNAL_PHASE_DATA_OUT:
#if RAW_SCSI_DEBUG > 2
		write_log(_T("raw_scsi: write data byte %02x (%d/%d)\n"), data, sd->offset, sd->data_len);
#endif
		if (scsi_send_data(sd, data)) {
#if RAW_SCSI_DEBUG
			write_log(_T("raw_scsi: data out finished, %d bytes\n"), sd->data_len);
#endif
			scsi_emulate_cmd(sd);
			rs->bus_phase = SCSI_SIGNAL_PHASE_STATUS;
		}
		break;
		case SCSI_SIGNAL_PHASE_MESSAGE_OUT:
		sd->msgout[sd->offset++] = data;
		len = getmsglen(sd->msgout, sd->offset);
		write_log(_T("raw_scsi_put_data got message %02x (%d/%d)\n"), data, sd->offset, len);
		if (sd->offset >= len) {
			write_log(_T("raw_scsi_put_data got message %02x (%d bytes)\n"), sd->msgout[0], len);
			if ((sd->msgout[0] & (0x80 | 0x20)) == 0x80)
				rs->msglun = sd->msgout[0] & 7;
			scsi_start_transfer(sd);
			rs->bus_phase = SCSI_SIGNAL_PHASE_COMMAND;
		}
		break;
		default:
		write_log(_T("raw_scsi_put_data but bus phase is %d!\n"), rs->bus_phase);
		break;
	}
}

static void raw_scsi_put_data(struct raw_scsi *rs, uae_u8 data, bool databusoutput)
{
	rs->data_write = data;
	if (!databusoutput)
		return;
	raw_scsi_write_data(rs, data);
}

static void raw_scsi_set_ack(struct raw_scsi *rs, bool ack)
{
	if (rs->ack != ack) {
		rs->ack = ack;
		if (!ack)
			return;
		if (rs->bus_phase < 0)
			return;
		if (!(rs->bus_phase & SCSI_IO_DIRECTION)) {
			if (rs->databusoutput) {
				raw_scsi_write_data(rs, rs->data_write);
			}
		} else {
			raw_scsi_get_data_2(rs, true, false);
		}
	}
}

// APOLLO SOFTSCSI

void apollo_scsi_bput(uaecptr addr, uae_u8 v)
{
	struct soft_scsi *as = getscsiboard(addr);
	if (!as)
		return;
	int bank = addr & (0x800 | 0x400);
	struct raw_scsi *rs = &as->rscsi;
	addr &= 0x3fff;
	if (bank == 0) {
		raw_scsi_put_data(rs, v, true);
	} else if (bank == 0xc00 && !(addr & 1)) {
		as->irq = (v & 64) != 0;
		raw_scsi_set_signal_phase(rs,
			(v & 128) != 0,
			(v & 32) != 0,
			false);
	} else if (bank == 0x400 && (addr & 1)) {
		raw_scsi_put_data(rs, v, true);
		raw_scsi_set_signal_phase(rs, true, false, false);
	}
	//write_log(_T("apollo scsi put %04x = %02x\n"), addr, v);
}

uae_u8 apollo_scsi_bget(uaecptr addr)
{
	struct soft_scsi *as = getscsiboard(addr);
	if (!as)
		return 0;
	int bank = addr & (0x800 | 0x400);
	struct raw_scsi *rs = &as->rscsi;
	uae_u8 v = 0xff;
	addr &= 0x3fff;
	if (bank == 0) {
		v = raw_scsi_get_data(rs, true);
	} else if (bank == 0x800 && (addr & 1)) {
		uae_u8 t = raw_scsi_get_signal_phase(rs);
		v = 1; // disable switch off
		if (t & SCSI_IO_BUSY)
			v |= 128;
		if (t & SCSI_IO_SEL)
			v |= 32;
		if (t & SCSI_IO_REQ)
			v |= 2;
		if (t & SCSI_IO_DIRECTION)
			v |= 8;
		if (t & SCSI_IO_COMMAND)
			v |= 16;
		if (t & SCSI_IO_MESSAGE)
			v |= 4;
		v ^= (1 | 2 | 4 | 8 | 16 | 32 | 128);
		//v |= apolloscsi.irq ? 64 : 0;
	}
	//write_log(_T("apollo scsi get %04x = %02x\n"), addr, v);
	return v;
}

void apollo_add_ide_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

void apollo_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	if (ch < 0) {
		generic_soft_scsi_add(-1, ci, rc, NONCR_APOLLO, -1, -1, ROMTYPE_APOLLO);
		// make sure IDE side is also initialized
		struct uaedev_config_info ci2 = { 0 };
		apollo_add_ide_unit(-1, &ci2, rc);
	} else {
		if (ci->controller_type < HD_CONTROLLER_TYPE_SCSI_FIRST) {
			apollo_add_ide_unit(ch, ci, rc);
		} else {
			generic_soft_scsi_add(ch, ci, rc, NONCR_APOLLO, -1, -1, ROMTYPE_APOLLO);
		}
	}
}

uae_u8 ncr5380_bget(struct soft_scsi *scsi, int reg);
void ncr5380_bput(struct soft_scsi *scsi, int reg, uae_u8 v);

static void supra_do_dma(struct soft_scsi *ncr)
{
	int len = ncr->dmac_length;
	for (int i = 0; i < len; i++) {
		if (ncr->dmac_direction < 0) {
			x_put_byte(ncr->dmac_address, ncr5380_bget(ncr, 0));
		} else if (ncr->dmac_direction > 0) {
			ncr5380_bput(ncr, 0, x_get_byte(ncr->dmac_address));
		}
		ncr->dmac_length--;
		ncr->dmac_address++;
	}
}

static void xebec_do_dma(struct soft_scsi *ncr)
{
	struct raw_scsi *rs = &ncr->rscsi;
	while (rs->bus_phase == SCSI_SIGNAL_PHASE_DATA_OUT || rs->bus_phase == SCSI_SIGNAL_PHASE_DATA_IN) {
		if (rs->bus_phase == SCSI_SIGNAL_PHASE_DATA_IN) {
			x_put_byte(ncr->dmac_address, ncr5380_bget(ncr, 8));
		} else if (rs->bus_phase == SCSI_SIGNAL_PHASE_DATA_OUT) {
			ncr5380_bput(ncr, 8, x_get_byte(ncr->dmac_address));
		}
	}
}


static void dma_check(struct soft_scsi *ncr)
{
	if (ncr->dmac_active && ncr->dma_direction) {

		if (ncr->type == NCR5380_SUPRA && ncr->subtype == 4) {
			if (ncr->dmac_direction != ncr->dma_direction)  {
				write_log(_T("SUPRADMA: mismatched direction\n"));
				ncr->dmac_active = 0;
				return;
			}
			supra_do_dma(ncr);
		}

		if (ncr->type == NCR5380_XEBEC) {
		
			xebec_do_dma(ncr);

		}
		ncr->dmac_active = 0;
	}
}

// NCR 53C80/MISC SCSI-LIKE

void x86_doirq(uint8_t irqnum);
void ncr80_rethink(void)
{
	for (int i = 0; soft_scsi_devices[i]; i++) {
		if (soft_scsi_devices[i]->irq && soft_scsi_devices[i]->intena) {
			if (soft_scsi_devices[i] == x86_hd_data) {
				x86_doirq(5);
			} else {
				if (soft_scsi_devices[i]->level6)
					INTREQ_0(0x8000 | 0x2000);
				else
					INTREQ_0(0x8000 | 0x0008);
				return;
			}
		}
	}
}

static void ncr5380_set_irq(struct soft_scsi *scsi)
{
	if (scsi->irq)
		return;
	scsi->irq = true;
	ncr80_rethink();
	if (scsi->delayed_irq)
		x_do_cycles(2 * CYCLE_UNIT);
#if NCR5380_DEBUG_IRQ
	write_log(_T("IRQ\n"));
#endif
}

static void ncr5380_databusoutput(struct soft_scsi *scsi)
{
	bool databusoutput = (scsi->regs[1] & 1) != 0;
	struct raw_scsi *r = &scsi->rscsi;

	if (r->bus_phase >= 0 && (r->bus_phase & SCSI_IO_DIRECTION))
		databusoutput = false;
	raw_scsi_set_databus(r, databusoutput);
}

static void ncr5380_check(struct soft_scsi *scsi)
{
	ncr5380_databusoutput(scsi);
}

static void ncr5380_check_phase(struct soft_scsi *scsi)
{
	if (!(scsi->regs[2] & 2))
		return;
	if (scsi->regs[2] & 0x40)
		return;
	if (scsi->rscsi.bus_phase != (scsi->regs[3] & 7)) {
		if (scsi->dma_controller) {
			scsi->regs[5] |= 0x80; // end of dma
			scsi->regs[3] |= 0x80; // last byte sent
		}
		ncr5380_set_irq(scsi);
	}
}

static void ncr5380_reset(struct soft_scsi *scsi)
{
	struct raw_scsi *r = &scsi->rscsi;

	scsi->irq = true;
	memset(scsi->regs, 0, sizeof scsi->regs);
	raw_scsi_reset_bus(scsi);
	scsi->regs[1] = 0x80;
}

uae_u8 ncr5380_bget(struct soft_scsi *scsi, int reg)
{
	if (reg > 8)
		return 0;
	uae_u8 v = scsi->regs[reg];
	struct raw_scsi *r = &scsi->rscsi;
	switch(reg)
	{
		case 1:
		break;
		case 4:
		{
			uae_u8 t = raw_scsi_get_signal_phase(r);
			v = 0;
			if (t & SCSI_IO_BUSY)
				v |= 1 << 6;
			if (t & SCSI_IO_REQ)
				v |= 1 << 5;
			if (t & SCSI_IO_SEL)
				v |= 1 << 1;
			if (r->bus_phase >= 0)
				v |= r->bus_phase << 2;
			if (scsi->regs[1] & 0x80)
				v |= 0x80;
		}
		break;
		case 5:
		{
			uae_u8 t = raw_scsi_get_signal_phase(r);
			v &= (0x80 | 0x40 | 0x20 | 0x04);
			if (t & SCSI_IO_ATN)
				v |= 1 << 1;
			if (r->bus_phase == (scsi->regs[3] & 7)) {
				v |= 1 << 3;
			}
			if (scsi->irq) {
				v |= 1 << 4;
			}
			if (scsi->dma_active && !scsi->dma_controller) {
				v |= 1 << 6;
			}
			if (scsi->regs[2] & 4) {
				// monitor busy
				if (r->bus_phase == SCSI_SIGNAL_PHASE_FREE) {
					// any loss of busy = Busy error
					// not just "unexpected" loss of busy
					v |= 1 << 2;
					scsi->dmac_active = false;
				}
			}
		}
		break;
		case 0:
		v = raw_scsi_get_data(r, false);
		break;
		case 6:
		v = raw_scsi_get_data(r, scsi->dma_active);
		ncr5380_check_phase(scsi);
		break;
		case 7:
		scsi->irq = false;
		break;
		case 8: // fake dma port
		v = raw_scsi_get_data(r, true);
		ncr5380_check_phase(scsi);
		break;
	}
	ncr5380_check(scsi);
	return v;
}

void ncr5380_bput(struct soft_scsi *scsi, int reg, uae_u8 v)
{
	if (reg > 8)
		return;
	bool dataoutput = (scsi->regs[1] & 1) != 0;
	struct raw_scsi *r = &scsi->rscsi;
	uae_u8 old = scsi->regs[reg];
	scsi->regs[reg] = v;
	switch(reg)
	{
		case 0:
		{
			r->data_write = v;
			// assert data bus can be only active if direction is out
			// and bus phase matches
			if (r->databusoutput) {
				if (((scsi->regs[2] & 2) && scsi->dma_active) || r->bus_phase < 0) {
					raw_scsi_write_data(r, v);
					ncr5380_check_phase(scsi);
				}
			}
		}
		break;
		case 1:
		{
			scsi->regs[reg] &= ~((1 << 5) | (1 << 6));
			scsi->regs[reg] |= old & ((1 << 5) | (1 << 6)); // AIP, LA
			if (!(v & 0x80)) {
				bool init = r->bus_phase < 0;
				ncr5380_databusoutput(scsi);
				if (init && !dataoutput && (v & 1) && (scsi->regs[2] & 1)) {
					r->bus_phase = SCSI_SIGNAL_PHASE_SELECT_1;
				}
				raw_scsi_set_signal_phase(r,
					(v & (1 << 3)) != 0,
					(v & (1 << 2)) != 0,
					(v & (1 << 1)) != 0);
				if (!(scsi->regs[2] & 2))
					raw_scsi_set_ack(r, (v & (1 << 4)) != 0);
			}
			if (v & 0x80) { // RST
				ncr5380_reset(scsi);
			}
		}
		break;
		case 2:
		if ((v & 1) && !(old & 1)) { // Arbitrate
			r->databusoutput = false;
			raw_scsi_set_signal_phase(r, true, false, false);
			scsi->regs[1] |= 1 << 6; // AIP
			scsi->regs[1] &= ~(1 << 5); // LA
		} else if (!(v & 1) && (old & 1)) {
			scsi->regs[1] &= ~(1 << 6);
		}
		if (!(v & 2)) {
			// end of dma and dma request
			scsi->regs[5] &= ~(0x80 | 0x40);
			scsi->dma_direction = 0;
			scsi->dma_active = false;
		}
		break;
		case 5:
		scsi->regs[reg] = old;
		if (scsi->regs[2] & 2) {
			scsi->dma_direction = 1;
			scsi->dma_active = true;
			dma_check(scsi);
		}
#if NCR5380_DEBUG
		write_log(_T("DMA send PC=%08x\n"), M68K_GETPC);
#endif
		break;
		case 6:
		if (scsi->regs[2] & 2) {
			scsi->dma_direction = 1;
			scsi->dma_active = true;
		}
#if NCR5380_DEBUG
		write_log(_T("DMA target recv PC=%08x\n"), M68K_GETPC);
#endif
		break;
		case 7:
		if (scsi->regs[2] & 2) {
			scsi->dma_direction = -1;
			scsi->dma_active = true;
			dma_check(scsi);
		}
#if NCR5380_DEBUG
		write_log(_T("DMA initiator recv PC=%08x\n"), M68K_GETPC);
#endif
		break;
		case 8: // fake dma port
		raw_scsi_put_data(r, v, true);
		ncr5380_check_phase(scsi);
		break;
	}
	ncr5380_check(scsi);
}

static bool ncr53400_5380(struct soft_scsi *scsi)
{
	if (scsi->irq)
		scsi->regs_400[1] = 0;
	return !scsi->regs_400[1];
}

static void ncr53400_dmacount(struct soft_scsi *scsi)
{
	scsi->c400_count++;
	if (scsi->c400_count == 128) {
		scsi->c400_count = 0;
		scsi->regs_400[1]--;
		if (scsi->regs_400[1] == 0) {
			scsi->regs[5] &= ~0x40; // dma request
			ncr5380_check_phase(scsi);
		}
	}
}

static uae_u8 ncr53400_bget(struct soft_scsi *scsi, int reg)
{
	struct raw_scsi *rs = &scsi->rscsi;
	uae_u8 v = 0;
	uae_u8 csr = scsi->regs_400[0];

	if (ncr53400_5380(scsi) && reg < 8) {
		v = ncr5380_bget(scsi, reg);
		//write_log(_T("53C80 REG GET %02x -> %02x\n"), reg, v);
		return v;
	}
	if (reg & 0x80) {
		v = raw_scsi_get_data(rs, true);
		ncr53400_dmacount(scsi);
	} else if (reg & 0x100) {
		switch (reg) {
			case 0x100:
				if (scsi->regs_400[1]) {
					v |= 0x02;
				} else {
					v |= 0x04;
				}
				if (ncr53400_5380(scsi))
					v |= 0x80;
				if (scsi->irq)
					v |= 0x01;
				break;
			case 0x101:
				v = scsi->regs_400[1];
				break;
		}
		//write_log(_T("53C400 REG GET %02x -> %02x\n"), reg, v);
	}
	ncr5380_check_phase(scsi);
	return v;
}

static void ncr53400_bput(struct soft_scsi *scsi, int reg, uae_u8 v)
{
	struct raw_scsi *rs = &scsi->rscsi;
	uae_u8 csr = scsi->regs_400[0];

	if (ncr53400_5380(scsi) && reg < 8) {
		ncr5380_bput(scsi, reg, v);
		//write_log(_T("53C80 REG PUT %02x -> %02x\n"), reg, v);
		return;
	}
	if (reg & 0x80) {
		raw_scsi_put_data(rs, v, true);
		ncr53400_dmacount(scsi);
	} else if (reg & 0x100) {
		switch (reg) {
			case 0x100:
				scsi->regs_400[0] = v;
				if (v & 0x80) {
					ncr5380_reset(scsi);
					scsi->regs_400[0] = 0x80;
					scsi->regs_400[1] = 0;
				}
				break;
			case 0x101:
				scsi->regs_400[1] = v;
				scsi->c400_count = 0;
				break;
		}
		//write_log(_T("53C400 REG PUT %02x -> %02x\n"), reg, v);
	}
	ncr5380_check_phase(scsi);
}

/* SASI */

static uae_u8 sasi_tecmar_bget(struct soft_scsi *scsi, int reg)
{
	struct raw_scsi *rs = &scsi->rscsi;
	uae_u8 v = 0;

	if (reg == 1) {

		uae_u8 t = raw_scsi_get_signal_phase(rs);
		v = 0;
		switch (rs->bus_phase)
		{
			case SCSI_SIGNAL_PHASE_DATA_OUT:
			v = 0;
			break;
			case SCSI_SIGNAL_PHASE_DATA_IN:
			v = 1 << 2;
			break;
			case SCSI_SIGNAL_PHASE_COMMAND:
			v = 1 << 3;
			break;
			case SCSI_SIGNAL_PHASE_STATUS:
			v = (1 << 2) | (1 << 3);
			break;
			case SCSI_SIGNAL_PHASE_MESSAGE_IN:
			v = (1 << 2) | (1 << 3) | (1 << 4);
			break;
		}
		if (t & SCSI_IO_BUSY)
			v |= 1 << 1;
		if (t & SCSI_IO_REQ)
			v |= 1 << 0;
		v = v ^ 0xff;

	} else {

		v = raw_scsi_get_data_2(rs, true, false);

	}

	//write_log(_T("SASI READ port %d: %02x\n"), reg, v);

	return v;
}

static void sasi_tecmar_bput(struct soft_scsi *scsi, int reg, uae_u8 v)
{
	struct raw_scsi *rs = &scsi->rscsi;

	//write_log(_T("SASI WRITE port %d: %02x\n"), reg, v);

	if (reg == 1) {
		if ((v & 1)) {
			raw_scsi_busfree(rs);
		}
		if ((v & 2) && !scsi->active_select) {
			// select?
			scsi->active_select = true;
			if (!rs->data_write)
				scsi->wait_select = true;
			else
				raw_scsi_set_signal_phase(rs, false, true, false);
		} else if (!(v & 2) && scsi->active_select) {
			scsi->active_select = false;
			raw_scsi_set_signal_phase(rs, false, false, false);
		}
	} else {
		raw_scsi_put_data(rs, v, true);
		if (scsi->wait_select && scsi->active_select)
			raw_scsi_set_signal_phase(rs, false, true, false);
		scsi->wait_select = false;
	}
}

static uae_u8 sasi_microforge_bget(struct soft_scsi *scsi, int reg)
{
	struct raw_scsi *rs = &scsi->rscsi;
	uae_u8 v = 0;

	if (reg == 1) {

		uae_u8 t = raw_scsi_get_signal_phase(rs);
		v = 0;
		if (rs->bus_phase >= 0) {
			if (rs->bus_phase & SCSI_IO_MESSAGE)
				v |= 1 << 1;
			if (rs->bus_phase & SCSI_IO_COMMAND)
				v |= 1 << 2;
			if (rs->bus_phase & SCSI_IO_DIRECTION)
				v |= 1 << 3;
		}
		if (t & SCSI_IO_BUSY)
			v |= 1 << 0;
		if (t & SCSI_IO_REQ)
			v |= 1 << 4;
		v = v ^ 0xff;

	} else {

		v = raw_scsi_get_data_2(rs, true, false);

	}

	//write_log(_T("SASI READ port %d: %02x\n"), reg, v);

	return v;
}

static void sasi_microforge_bput(struct soft_scsi *scsi, int reg, uae_u8 v)
{
	struct raw_scsi *rs = &scsi->rscsi;

	//write_log(_T("SASI WRITE port %d: %02x\n"), reg, v);

	if (reg == 1) {
		if ((v & 4) && !scsi->active_select) {
			// select?
			scsi->active_select = true;
			if (!rs->data_write)
				scsi->wait_select = true;
			else
				raw_scsi_set_signal_phase(rs, false, true, false);
		} else if (!(v & 4) && scsi->active_select) {
			scsi->active_select = false;
			raw_scsi_set_signal_phase(rs, false, false, false);
		}
	} else {
		raw_scsi_put_data(rs, v, true);
		if (scsi->wait_select && scsi->active_select)
			raw_scsi_set_signal_phase(rs, false, true, false);
		scsi->wait_select = false;
	}
}

// OMTI 5510
static void omti_irq(struct soft_scsi *scsi)
{
	if (scsi->intena && (scsi->chip_state & 2))
		ncr5380_set_irq(scsi);
}
static void omti_check_state(struct soft_scsi *scsi)
{
	struct raw_scsi *rs = &scsi->rscsi;
	if ((rs->bus_phase == SCSI_SIGNAL_PHASE_DATA_IN || rs->bus_phase == SCSI_SIGNAL_PHASE_DATA_OUT) && (scsi->chip_state & 1)) {
		omti_irq(scsi);
	}
}

static uae_u8 omti_bget(struct soft_scsi *scsi, int reg)
{
	struct raw_scsi *rs = &scsi->rscsi;
	uae_u8 t = raw_scsi_get_signal_phase(rs);
	uae_u8 v = 0;

	switch (reg)
	{
		case 0: // DATA IN
		if (rs->bus_phase == SCSI_SIGNAL_PHASE_STATUS) {
			v = raw_scsi_get_data_2(rs, true, false);
			// get message (not used in OMTI protocol)
			raw_scsi_get_data_2(rs, true, false);
		} else {
			v = raw_scsi_get_data_2(rs, true, false);
			if (rs->bus_phase == SCSI_SIGNAL_PHASE_STATUS) {
				omti_irq(scsi);	// command complete interrupt
			}
		}
		break;
		case 1: // STATUS
		if (rs->bus_phase >= 0)
			v |= 8; // busy
		if (v & 8) {
			if (t & SCSI_IO_REQ)
				v |= 1; // req
			if (t & SCSI_IO_DIRECTION)
				v |= 2;
			if (t & SCSI_IO_COMMAND)
				v |= 4;
		}
		v |= 0x80 | 0x40;
		if ((rs->bus_phase == SCSI_SIGNAL_PHASE_DATA_IN || rs->bus_phase == SCSI_SIGNAL_PHASE_DATA_OUT) && (scsi->chip_state & 1))
			v |= 0x10;
		if (scsi->irq)
			v |= 0x20;
		scsi->irq = false;
		break;
		break;
		case 2: // CONFIGURATION
		v = 0xff;
		break;
		case 3: // -
		break;
	}
	omti_check_state(scsi);
	return v;
}
static void omti_bput(struct soft_scsi *scsi, int reg, uae_u8 v)
{
	struct raw_scsi *rs = &scsi->rscsi;
	switch (reg)
	{
		case 0: // DATA OUT
		raw_scsi_put_data(rs, v, true);
		if (rs->bus_phase == SCSI_SIGNAL_PHASE_STATUS)
			omti_irq(scsi);	// command complete interrupt
		break;
		case 1: // RESET
		raw_scsi_busfree(rs);
		scsi->chip_state = 0;
		break;
		case 2: // SELECT
		rs->data_write = 0x01;
		raw_scsi_set_signal_phase(rs, false, true, false);
		raw_scsi_set_signal_phase(rs, false, false, false);
		break;
		case 3: // MASK
		scsi->chip_state = v;
		break;
	}
	omti_check_state(scsi);
}

static int suprareg(struct soft_scsi *ncr, uaecptr addr, bool write)
{
	int reg = (addr & 0x0f) >> 1;
	if ((addr & 0x20) && ncr->subtype == 0) {
		// "dma" data in/out space
		reg = 8;
		if (!(ncr->regs[2] & 2))
			cpu_halt(CPU_HALT_FAKE_DMA);
	}
	return reg;
}

static int stardrivereg(struct soft_scsi *ncr, uaecptr addr)
{
	if ((addr & 0x0191) == 0x191) {
		// "dma" data in/out register
		return 8;
	}
	if ((addr & 0x0181) != 0x181)
		return -1;
	int reg = (addr >> 1) & 7;
	return reg;
}

static int cltdreg(struct soft_scsi *ncr, uaecptr addr)
{
	if (!(addr & 1)) {
		return -1;
	}
	int reg = (addr >> 1) & 7;
	return reg;
}

static int protarreg(struct soft_scsi *ncr, uaecptr addr)
{
	int reg = -1;
	if ((addr & 0x24) == 0x20) {
		// "fake dma" data port
		reg = 8;
	} else if ((addr & 0x20) == 0x00) {
		reg = (addr >> 2) & 7;
	}
	return reg;
}

static int add500reg(struct soft_scsi *ncr, uaecptr addr)
{
	int reg = -1;
	if ((addr & 0x8048) == 0x8000) {
		reg = 8;
	} else if ((addr & 0x8040) == 0x8040) {
		reg = (addr >> 1) & 7;
	}
	return reg;
}

static int adscsireg(struct soft_scsi *ncr, uaecptr addr, bool write)
{
	int reg = -1;
	if ((addr == 0x38 || addr == 0x39) && !write) {
		reg = 8;
	} else if ((addr == 0x20 || addr == 0x21) && write) {
		reg = 8;
	} else  if (addr < 0x20) {
		reg = (addr >> 2) & 7;
	}
	return reg;
}

static int ptnexusreg(struct soft_scsi *ncr, uaecptr addr)
{
	int reg = -1;
	if ((addr & 0x8ff0) == 0) {
		reg = (addr >> 1) & 7;
	}
	return reg;
}

static int xebec_reg(struct soft_scsi *ncr, uaecptr addr)
{
	if (addr < 0x10000) {
		if (addr & 1)
			return (addr & 0xff) >> 1;
		return -1;
	}
	if ((addr & 0x180000) == 0x100000) {
		
		ncr->dmac_active = 1;

	} else if ((addr & 0x180000) == 0x180000) {

		ncr->dmac_active = 0;
		ncr->dmac_address = ncr->baseaddress | 0x80000;

	} else if ((addr & 0x180000) == 0x080000) {
		ncr->dmac_address = addr | ncr->baseaddress | 1;
		ncr->dmac_address += 2;
		if (addr & 1)
			return 0x80000 + (addr & 32767);
		return -1;
	}

//	if (addr >= 0x10000)
//		write_log(_T("XEBEC %08x PC=%08x\n"), addr, M68K_GETPC);
	return -1;
}

static int hda506_reg(struct soft_scsi *ncr, uaecptr addr, bool write)
{
	if ((addr & 0x7fe1) != 0x7fe0)
		return -1;
	addr &= 0x7;
	addr >>= 1;
	return addr;
}

static int alf1_reg(struct soft_scsi *ncr, uaecptr addr, bool write)
{
	if ((addr & 0x7ff9) != 0x0641)
		return -1;
	addr >>= 1;
	addr &= 3;
	return addr;
}

static int system2000_reg(struct soft_scsi *ncr, uaecptr addr, int size, bool write)
{
	if (size != 1)
		return -1;
	if ((addr & 0xc007) != 0x4000)
		return -1;
	addr >>= 3;
	addr &= 3;
	return addr;
}

static int promigos_reg(struct soft_scsi *ncr, uaecptr addr, int size, bool write)
{
	if (size != 1)
		return -1;
	if ((addr & 0x1) != 1)
		return -1;
	addr &= 7;
	if (addr == 1 && write)
		return 3;
	if (addr == 3 && write)
		return 0;
	if (addr == 5 && write)
		return 1;
	if (addr == 5 && !write)
		return 0;
	if (addr == 7 && write)
		return 2;
	if (addr == 7 && !write)
		return 1;
	return -1;
}

static int microforge_reg(struct soft_scsi *ncr, uaecptr addr, bool write)
{
	int reg = -1;
	if ((addr & 0x7000) != 0x7000)
		return -1;
	addr &= 0xfff;
	if (addr == 38 && !write)
		return 0;
	if (addr == 40 && write)
		return 0;
	if (addr == 42 && !write)
		return 1;
	if (addr == 44 && write)
		return 1;
	return reg;
}

static uae_u8 read_supra_dma(struct soft_scsi *ncr, uaecptr addr)
{
	uae_u8 val = 0;

	addr &= 0x1f;
	switch (addr)
	{
		case 0:
		val = ncr->dmac_active ? 0x00 : 0x80;
		break;
		case 4:
		val = 0;
		break;
	}

	write_log(_T("SUPRA DMA GET %08x %02x %08x\n"), addr, val, M68K_GETPC);
	return val;
}

static void write_supra_dma(struct soft_scsi *ncr, uaecptr addr, uae_u8 val)
{
	write_log(_T("SUPRA DMA PUT %08x %02x %08x\n"), addr, val, M68K_GETPC);

	addr &= 0x1f;
	switch (addr)
	{
		case 5: // OCR
		ncr->dmac_direction = (val & 0x80) ? -1 : 1;
		break;
		case 7:
		ncr->dmac_active = (val & 0x80) != 0;
		break;
		case 10: // MTCR
		ncr->dmac_length &= 0x000000ff;
		ncr->dmac_length |= val << 8;
		break;
		case 11: // MTCR
		ncr->dmac_length &= 0x0000ff00;
		ncr->dmac_length |= val << 0;
		break;
		case 12: // MAR
		break;
		case 13: // MAR
		ncr->dmac_address &= 0x0000ffff;
		ncr->dmac_address |= val << 16;
		break;
		case 14: // MAR
		ncr->dmac_address &= 0x00ff00ff;
		ncr->dmac_address |= val << 8;
		break;
		case 15: // MAR
		ncr->dmac_address &= 0x00ffff00;
		ncr->dmac_address |= val << 0;
		break;
	}
}

static void vector_scsi_status(struct raw_scsi *rs)
{
	// Vector Falcon 8000 FPGA seems to handle this internally
	while (rs->bus_phase == SCSI_SIGNAL_PHASE_STATUS || rs->bus_phase == SCSI_SIGNAL_PHASE_MESSAGE_IN) {
		raw_scsi_get_data(rs, true);
	}
}

static int tecmar_clock_reg_select;
static uae_u8 tecmar_clock_regs[64];
static uae_u8 tecmar_clock_bcd(uae_u8 v)
{
	uae_u8 out = v;
	if (!(tecmar_clock_regs[11] & 4)) {
		out = ((v / 10) << 4) + (v % 10);
	}
	return out;
}
static void tecmar_clock_bput(int addr, uae_u8 v)
{
	if (addr == 0) {
		tecmar_clock_reg_select = v & 63;
	}
	else if (addr == 1) {
		tecmar_clock_regs[tecmar_clock_reg_select] = v;
		tecmar_clock_regs[12] = 0x00;
		tecmar_clock_regs[13] = 0x80;
	}
}
static uae_u8 tecmar_clock_bget(int addr)
{
	uae_u8 v = 0;
	if (addr == 0) {
		v = tecmar_clock_reg_select;
	}
	else if (addr == 1) {
		time_t t = time(0);
		t += currprefs.cs_rtc_adjust;
		struct tm *ct = localtime(&t);
		switch (tecmar_clock_reg_select)
		{
			case 0:
			v = tecmar_clock_bcd(ct->tm_sec);
			break;
			case 2:
			v = tecmar_clock_bcd(ct->tm_min);
			break;
			case 4:
			v = tecmar_clock_bcd(ct->tm_hour);
			if (!(tecmar_clock_regs[11] & 2)) {
				if (v >= 12) {
					v -= 12;
					v |= 0x80;
				}
				v++;
			}
			break;
			case 6:
			v = tecmar_clock_bcd(ct->tm_wday + 1);
			break;
			case 7:
			v = tecmar_clock_bcd(ct->tm_mday);
			break;
			case 8:
			v = tecmar_clock_bcd(ct->tm_mon + 1);
			break;
			case 9:
			v = tecmar_clock_bcd(ct->tm_year % 100);
			break;
			default:
			v = tecmar_clock_regs[tecmar_clock_reg_select];
			break;
		}
	}
	return v;
}

static uae_u32 ncr80_bget2(struct soft_scsi *ncr, uaecptr addr, int size)
{
	int reg = -1;
	uae_u32 v = 0;
	int addresstype = -1;
	uaecptr origaddr = addr;

	addr &= ncr->board_mask;

	if (ncr->type == NCR5380_SUPRA) {

		if (ncr->subtype == 4) {
			if ((addr & 0xc000) == 0xc000) {
				v = read_supra_dma(ncr, addr);
			} else if (addr & 0x8000) {
				addresstype = (addr & 1) ? 0 : 1;
			}
		} else if (ncr->subtype == 3) {
			if ((addr & 0x8000) && !(addr & 1))
				addresstype = 0;
		} else {
			if (ncr->subtype != 1 && (addr & 1)) {
				v = 0xff;
			} else if (addr & 0x8000) {
				addresstype = 1;
			} else {
				addresstype = 0;
			}
		}

		if (addresstype == 1) {
			v = ncr->rom[addr & 0x7fff];
		} else if (addresstype == 0) {
			reg = suprareg(ncr, addr, false);
			if (reg >= 0)
				v = ncr5380_bget(ncr, reg);
		}

	} else if (ncr->type == NONCR_GOLEM) {

		int bank = addr & 0x8f81;
		struct raw_scsi *rs = &ncr->rscsi;
		switch(bank)
		{
			case 0x8000:
			case 0x8001:
			case 0x8002:
			case 0x8003:
			v = raw_scsi_get_data(rs, true);
			// message is not retrieved by driver, perhaps hardware does it?
			if (rs->bus_phase == SCSI_SIGNAL_PHASE_MESSAGE_IN)
				raw_scsi_get_data(rs, true);
			break;
			case 0x8200:
			{
				uae_u8 t = raw_scsi_get_signal_phase(rs);
				v = 0;
				if (!(t & SCSI_IO_BUSY))
					v |= 1 << (8 - 8);
				if (rs->bus_phase >= 0) {
					if (!(rs->bus_phase & SCSI_IO_DIRECTION))
						v |= 1 << (13 - 8);
					if (!(rs->bus_phase & SCSI_IO_COMMAND))
						v |= 1 << (10 - 8);
					if (rs->bus_phase != SCSI_SIGNAL_PHASE_STATUS)
						v |= 1 << (15 - 8);
				}
			}
			break;
			case 0x8201:
			{
				uae_u8 t = raw_scsi_get_signal_phase(rs);
				v = 0;
				if (t & SCSI_IO_REQ)
					v |= 1 << 6;
			}
			break;
			default:
			if ((addr & 0xc000) == 0x0000)
				v = ncr->rom[addr];
			break;
		}

	} else if (ncr->type == NCR5380_STARDRIVE) {

		struct raw_scsi *rs = &ncr->rscsi;
		if (addr < sizeof ncr->acmemory) {
			v = ncr->acmemory[addr];
		} else {
			reg = stardrivereg(ncr, addr);
			if (reg >= 0) {
				v = ncr5380_bget(ncr, reg);
			} else if (addr == 0x104) {
				v = 0;
				// bit 3: dreq
				if (rs->bus_phase >= 0 && (rs->io & SCSI_IO_REQ) && (ncr->regs[2] & 2))
					v |= 1 << 3;
			}
		}

	} else if (ncr->type == NCR5380_CLTD) {

		struct raw_scsi *rs = &ncr->rscsi;
		if (!ncr->configured && addr < sizeof ncr->acmemory) {
			v = ncr->acmemory[addr];
		} else {
			reg = cltdreg(ncr, addr);
			if (reg >= 0)
				v = ncr5380_bget(ncr, reg);
		}

	} else if (ncr->type == NCR5380_PTNEXUS) {

		struct raw_scsi *rs = &ncr->rscsi;
		if (!ncr->configured && addr < sizeof ncr->acmemory) {
			v = ncr->acmemory[addr];
		} else if (addr & 0x8000) {
			v = ncr->rom[addr & 16383];
		} else {
			reg = ptnexusreg(ncr, addr);
			if (reg >= 0) {
				v = ncr5380_bget(ncr, reg);
			} else if (addr == 0x11) {
				// fake dma status
				v = 0;
			}
		}

	} else if (ncr->type == NONCR_KOMMOS) {

		struct raw_scsi *rs = &ncr->rscsi;
		if (addr & 0x8000) {
			v = ncr->rom[addr & 0x7fff];
		} else if ((origaddr & 0xf00000) != 0xf00000) {
			if (!(addr & 8)) {
				v = raw_scsi_get_data(rs, true);
			} else {
				uae_u8 t = raw_scsi_get_signal_phase(rs);
				v = 0;
				if (t & SCSI_IO_BUSY)
					v |= 1 << 1;
				if (t & SCSI_IO_REQ)
					v |= 1 << 0;
				if (t & SCSI_IO_DIRECTION)
					v |= 1 << 4;
				if (t & SCSI_IO_COMMAND)
					v |= 1 << 3;
				if (t & SCSI_IO_MESSAGE)
					v |= 1 << 2;
			}
		}

	} else if (ncr->type == NONCR_VECTOR) {

		struct raw_scsi *rs = &ncr->rscsi;
		if (addr < sizeof ncr->acmemory) {
			v = ncr->acmemory[addr];
		} else if (!(addr & 0x8000)) {
			v = ncr->rom[addr];
		} else {
			if ((addr & 0x201) == 0x200) {
				v = (1 << 0);
				uae_u8 t = raw_scsi_get_signal_phase(rs);
				if (t & SCSI_IO_BUSY)
					v &= ~(1 << 0);
				if (t & SCSI_IO_DIRECTION)
					v |= (1 << 6);
				if (t & SCSI_IO_COMMAND)
					v |= (1 << 7);
			} else if ((addr & 0x201) == 0x201) {
				v = 0;
				uae_u8 t = raw_scsi_get_signal_phase(rs);
				if (t & SCSI_IO_REQ)
					v |= 1 << 1;

			} else if ((addr & 0x300) == 0x000) {
				if (size > 1) {
					v = raw_scsi_get_data(rs, true);
					vector_scsi_status(rs);
				} else {
					v = rs->status >= 2 ? 2 : 0;
				}
			} else if ((addr & 0x300) == 0x300) {
				raw_scsi_reset(rs);
			}
		}

	} else if (ncr->type == NCR5380_PROTAR) {

		struct raw_scsi *rs = &ncr->rscsi;
		if (addr < sizeof ncr->acmemory) {
			if (!ncr->configured) {
				v = ncr->acmemory[addr];
			} else {
				reg = protarreg(ncr, addr);
				if (reg >= 0) {
					v = ncr5380_bget(ncr, reg);
				}
			}
		} else {
			v = ncr->rom[addr & 65535];
		}

	} else if (ncr->type == NCR5380_ADD500) {

		struct raw_scsi *rs = &ncr->rscsi;
		if (addr & 0x8000) {
			uae_u8 t = raw_scsi_get_signal_phase(rs);
			if ((addr & 0x8048) == 0x8000) {
				if (!(addr & 1)) {
					if (t & SCSI_IO_REQ) {
						ncr->databuffer[0] = ncr->databuffer[1];
						ncr->databuffer[1] = raw_scsi_get_data(rs, true) << 8;
						ncr->databuffer[1] |= raw_scsi_get_data(rs, true);
						if (ncr->databuffer_empty) {
							ncr->databuffer[0] = ncr->databuffer[1];
							ncr->databuffer[1] = raw_scsi_get_data(rs, true) << 8;
							ncr->databuffer[1] |= raw_scsi_get_data(rs, true);
						}
						ncr->databuffer_empty = false;
					} else {
						ncr->databuffer_empty = true;
					}
				}
				v = ncr->databuffer[0] >> 8;
				ncr->databuffer[0] <<= 8;
			} else {
				reg = add500reg(ncr, addr);
				if (reg >= 0) {
					v = ncr5380_bget(ncr, reg);
				} else if ((addr & 0x8049) == 0x8009) {
					v = 0;
					if (!(t & SCSI_IO_REQ) && ncr->databuffer_empty) {
						v |= 1 << 0;
					}
				}
			}
		} else {
			v = ncr->rom[addr];
		}

	} else if (ncr->type == NCR5380_ADSCSI) {

		struct raw_scsi *rs = &ncr->rscsi;
		if (ncr->configured)
			reg = adscsireg(ncr, addr, false);
		if (reg >= 0) {
			v = ncr5380_bget(ncr, reg);
		} else {
			v = ncr->rom[addr & 65535];
		}
		if (addr == 0x40) {
			uae_u8 t = raw_scsi_get_signal_phase(rs);
			v = 0;
			// bits 0 to 2: ID (inverted)
			v |= (ncr->rc->device_id ^ 7) & 7;
			// shorter delay before drive detection (8s vs 18s)
			v |= 1 << 5;
			if (t & SCSI_IO_REQ) {
				v |= 1 << 6;
				v |= 1 << 7;
			}
		}

	} else if (ncr->type == NCR5380_KRONOS) {

		struct raw_scsi *rs = &ncr->rscsi;
		if (addr < sizeof ncr->acmemory)
			v = ncr->acmemory[addr];
		if (ncr->configured) {
			if (addr == 0x40 || addr == 0x41) {
				v = 0;
			}
		}
		if (addr & 0x8000) {
			v = ncr->rom[addr & 8191];
		}

	} else if (ncr->type == NCR5380_ROCHARD) {

		int reg = (addr & 15) / 2;
		if ((addr & 0x300) == 0x300)
			v = ncr53400_bget(ncr, reg | 0x100);
		else if (addr & 0x200)
			v = ncr53400_bget(ncr, reg | 0x80);
		else
			v = ncr53400_bget(ncr, reg);

	} else if (ncr->type == NCR5380_DATAFLYER) {

		reg = addr & 0xff;
		v = ncr5380_bget(ncr, reg);

	} else if (ncr->type == NONCR_TECMAR) {

		v = ncr->rom[addr];
		if (addr >= 0x2000 && addr < 0x3000) {
			if (addr == 0x2040)
				v = tecmar_clock_bget(0);
			else if (addr == 0x2042)
				v = tecmar_clock_bget(1);
		} else if (addr >= 0x4000 && addr < 0x5000) {
			if (addr == 0x4040)
				v = sasi_tecmar_bget(ncr, 1);
			else if (addr == 0x4042)
				v = sasi_tecmar_bget(ncr, 0);
		}

	} else if (ncr->type == NCR5380_XEBEC) {

		reg = xebec_reg(ncr, addr);
		if (reg >= 0 && reg < 8) {
			v = ncr5380_bget(ncr, reg);
		} else if (reg >= 0x80000) {
			v = ncr->databufferptr[reg & (ncr->databuffer_size - 1)];
		}

	} else if (ncr->type == NONCR_MICROFORGE) {

		reg = microforge_reg(ncr, addr, false);
		if (reg >= 0)
			v = sasi_microforge_bget(ncr, reg);

	} else if (ncr->type == OMTI_HDA506) {

		reg = hda506_reg(ncr, addr, false);
		if (reg >= 0)
			v = omti_bget(ncr, reg);

	} else if (ncr->type == OMTI_ALF1 || ncr->type == OMTI_ADAPTER) {

		reg = alf1_reg(ncr, addr, false);
		if (reg >= 0)
			v = omti_bget(ncr, reg);

	} else if (ncr->type == OMTI_PROMIGOS) {

		reg = promigos_reg(ncr, addr, size, false);
		if (reg >= 0)
			v = omti_bget(ncr, reg);

	} else if (ncr->type == OMTI_SYSTEM2000) {

		reg = system2000_reg(ncr, addr, size, false);
		if (reg >= 0) {
			v = omti_bget(ncr, reg);
		} else if (addr < 0x4000) {
			v = ncr->rom[addr];
		} else if (ncr->rscsi.bus_phase >= 0) {
			if ((addr & 0xc000) == 0x8000) {
				v = ncr->databuffer[addr & 1];
				ncr->databuffer[addr & 1] = omti_bget(ncr, 0);
			} else if ((addr & 0xc000) == 0xc000) {
				v = ncr->databuffer[addr & 1];
			}
		}

	}
#if NCR5380_DEBUG > 1
	if ((origaddr >= 0xf00000 && size == 1) || (origaddr < 0xf00000))
		write_log(_T("GET %08x %02x %d %08x %d\n"), origaddr, v, reg, M68K_GETPC, regs.intmask);
#endif

	return v;
}

static void ncr80_bput2(struct soft_scsi *ncr, uaecptr addr, uae_u32 val, int size)
{
	int reg = -1;
	int addresstype = -1;
	uaecptr origddr = addr;

	addr &= ncr->board_mask;

	if (ncr->type == NCR5380_SUPRA) {

		if (ncr->subtype == 4) {
			if ((addr & 0xc000) == 0xc000) {
				write_supra_dma(ncr, addr, val);
			} else if (addr & 0x8000) {
				addresstype = (addr & 1) ? 0 : 1;
			}
		} else if (ncr->subtype == 3) {
			if ((addr & 0x8000) && !(addr & 1))
				addresstype = 0;
		} else {
			if (ncr->subtype != 1 && (addr & 1))
				return;
			if (!(addr & 0x8000))
				addresstype = 0;
		}
		if (addresstype == 0) {
			reg = suprareg(ncr, addr, true);
			if (reg >= 0)
				ncr5380_bput(ncr, reg, val);
		}

	} else if (ncr->type == NONCR_GOLEM) {

		int bank = addr & 0x8f81;
		struct raw_scsi *rs = &ncr->rscsi;
		switch(bank)
		{
			case 0x8080:
			case 0x8081:
			case 0x8082:
			case 0x8083:
			raw_scsi_put_data(rs, val, true);
			break;
			case 0x8380:
			{
				raw_scsi_put_data(rs, val, true);
				raw_scsi_set_signal_phase(rs, false, true, false);
				uae_u8 t = raw_scsi_get_signal_phase(rs);
				if (t & SCSI_IO_BUSY)
					raw_scsi_set_signal_phase(rs, true, false, false);
			}
			break;
		}

	} else if (ncr->type == NCR5380_STARDRIVE) {

		reg = stardrivereg(ncr, addr);
		if (reg >= 0)
			ncr5380_bput(ncr, reg, val);

	} else if (ncr->type == NCR5380_CLTD) {

		if (ncr->configured) {
			reg = cltdreg(ncr, addr);
			if (reg >= 0)
				ncr5380_bput(ncr, reg, val);
		}

	} else if (ncr->type == NCR5380_PTNEXUS) {

		if (ncr->configured) {
			reg = ptnexusreg(ncr, addr);
			if (reg >= 0) {
				ncr5380_bput(ncr, reg, val);
			} else if (addr == 0x11) {
				ncr->chip_state = val;
			}
		}

	} else if (ncr->type == NONCR_KOMMOS) {

		struct raw_scsi *rs = &ncr->rscsi;
		if (!(addr & 0x8000) && (origddr & 0xf00000) != 0xf00000) {
			if (!(addr & 8)) {
				raw_scsi_put_data(rs, val, true);
			} else {
				// select?
				if (val & 4) {
					raw_scsi_set_signal_phase(rs, false, true, false);
					uae_u8 t = raw_scsi_get_signal_phase(rs);
					if (t & SCSI_IO_BUSY)
						raw_scsi_set_signal_phase(rs, true, false, false);
				}
			}
		}

	} else if (ncr->type == NONCR_VECTOR) {

		struct raw_scsi *rs = &ncr->rscsi;
		if (addr & 0x8000) {
			if ((addr & 0x300) == 0x300) {
				raw_scsi_put_data(rs, val, false);
				raw_scsi_set_signal_phase(rs, false, true, false);
				uae_u8 t = raw_scsi_get_signal_phase(rs);
				if (t & SCSI_IO_BUSY)
					raw_scsi_set_signal_phase(rs, true, false, false);
			} else if ((addr & 0x300) == 0x000) {
				raw_scsi_put_data(rs, val, true);
				vector_scsi_status(rs);
			}

		}

	} else if (ncr->type == NCR5380_PROTAR) {

		reg = protarreg(ncr, addr);
		if (reg >= 0)
			ncr5380_bput(ncr, reg, val);

	} else if (ncr->type == NCR5380_ADD500) {

		if ((addr & 0x8048) == 0x8008) {
			struct raw_scsi *rs = &ncr->rscsi;
			ncr->databuffer_empty = true;
		} else {
			reg = add500reg(ncr, addr);
			if (reg >= 0) {
				ncr5380_bput(ncr, reg, val);
			}
		}

	} else if (ncr->type == NCR5380_ADSCSI) {

		if (ncr->configured)
			reg = adscsireg(ncr, addr, true);
		if (reg >= 0) {
			ncr5380_bput(ncr, reg, val);
		}

	} else if (ncr->type == NCR5380_KRONOS) {

		if (addr == 0x60 || addr == 0x61) {
			;
		}

	} else if (ncr->type == NCR5380_ROCHARD) {

		int reg = (addr & 15) / 2;
		if ((addr & 0x300) == 0x300)
			ncr53400_bput(ncr, reg | 0x100, val);
		else if (addr & 0x200)
			ncr53400_bput(ncr, reg | 0x80, val);
		else
			ncr53400_bput(ncr, reg, val);

	} else if (ncr->type == NCR5380_DATAFLYER) {

		reg = addr & 0xff;
		ncr5380_bput(ncr, reg, val);

	} else if (ncr->type == NONCR_TECMAR) {

		if (addr == 0x22) {
			// box
			ncr->baseaddress = 0xe80000 + ((val & 0x7f) * 4096);
			map_banks_z2(ncr->bank, ncr->baseaddress >> 16, 1);
			expamem_next(ncr->bank, NULL);
		} else if (addr == 0x1020) {
			// memory board control/status
			ncr->rom[addr] = val & (0x80 | 0x40 | 0x02);
		} else if (addr == 0x1024) {
			// memory board memory address reg
			if (currprefs.fastmem2_size)
				map_banks_z2(&fastmem2_bank, val, currprefs.fastmem2_size >> 16);
		}
		else if (addr >= 0x2000 && addr < 0x3000) {
			// clock
			if (addr == 0x2040)
				tecmar_clock_bput(0, val);
			else if (addr == 0x2042)
				tecmar_clock_bput(1, val);
		} else if (addr >= 0x4000 && addr < 0x5000) {
			// sasi
			if (addr == 0x4040)
				sasi_tecmar_bput(ncr, 1, val);
			else if (addr == 0x4042)
				sasi_tecmar_bput(ncr, 0, val);
		}

	} else if (ncr->type == NCR5380_XEBEC) {

		reg = xebec_reg(ncr, addr);
		if (reg >= 0 && reg < 8) {
			ncr5380_bput(ncr, reg, val);
		} else if (reg >= 0x80000) {
			ncr->databufferptr[reg & (ncr->databuffer_size - 1)] = val;
		}

	} else if (ncr->type == NONCR_MICROFORGE) {

		reg = microforge_reg(ncr, addr, true);
		if (reg >= 0)
			sasi_microforge_bput(ncr, reg, val);

	} else if (ncr->type == OMTI_HDA506) {

		reg = hda506_reg(ncr, addr, true);
		if (reg >= 0)
			omti_bput(ncr, reg, val);

	} else if (ncr->type == OMTI_ALF1 || ncr->type == OMTI_ADAPTER) {

		reg = alf1_reg(ncr, addr, true);
		if (reg >= 0)
			omti_bput(ncr, reg, val);

	} else if (ncr->type == OMTI_PROMIGOS) {

		reg = promigos_reg(ncr, addr, size, true);
		if (reg >= 0)
			omti_bput(ncr, reg, val);

	} else if (ncr->type == OMTI_SYSTEM2000) {

		reg = system2000_reg(ncr, addr, size, true);
		if (reg >= 0) {
			omti_bput(ncr, reg, val);
		} else if (ncr->rscsi.bus_phase >= 0) {
			if ((addr & 0x8000) == 0x8000) {
				omti_bput(ncr, 0, val);
			}
		}

	}

#if NCR5380_DEBUG > 1
	write_log(_T("PUT %08x %02x %d %08x %d\n"), origddr, val, reg, M68K_GETPC, regs.intmask);
#endif
}

// x86 bridge hd
void x86_xt_hd_bput(int portnum, uae_u8 v)
{
	struct soft_scsi *scsi = x86_hd_data;
	write_log(_T("X86 HD PUT %04x = %02x\n"), portnum, v);
	if (!scsi)
		return;
	if (portnum < 0) {
		struct raw_scsi *rs = &scsi->rscsi;
		raw_scsi_busfree(rs);
		scsi->chip_state = 0;
		return;
	}
	omti_bput(scsi, v & 3, v);
}
uae_u8 x86_xt_hd_bget(int portnum)
{
	uae_u8 v = 0xff;
	struct soft_scsi *scsi = x86_hd_data;
	write_log(_T("X86 HD GET %04x\n"), portnum);
	if (!scsi)
		return v;
	v = omti_bget(scsi, v & 3);
	return v;
}

// mainhattan paradox scsi
uae_u8 parallel_port_scsi_read(int reg, uae_u8 data, uae_u8 dir)
{
	struct soft_scsi *scsi = parallel_port_scsi_data;
	if (!scsi)
		return data;
	struct raw_scsi *rs = &scsi->rscsi;
	uae_u8 t = raw_scsi_get_signal_phase(rs);
	if (reg == 0) {
		data = raw_scsi_get_data_2(rs, true, false);
		data ^= 0xff;
	} else if (reg == 1) {
		data &= ~3;
		if (rs->bus_phase >= 0 && !(rs->bus_phase & SCSI_IO_COMMAND))
			data |= 2; // POUT
		data |= 1;
		if (rs->bus_phase == SCSI_SIGNAL_PHASE_SELECT_2 || rs->bus_phase >= 0)
			data &= ~1; // BUSY
	}
	t = raw_scsi_get_signal_phase(rs);
	if ((t & SCSI_IO_REQ) && (scsi->chip_state & 4))
		cia_parallelack();
	return data;
}
void parallel_port_scsi_write(int reg, uae_u8 v, uae_u8 dir)
{
	struct soft_scsi *scsi = parallel_port_scsi_data;
	if (!scsi)
		return;
	struct raw_scsi *rs = &scsi->rscsi;
	if (reg == 0) {
		v ^= 0xff;
		raw_scsi_put_data(rs, v, true);
	} else if (reg == 1) {
		// SEL
		if (!(v & 4) && (scsi->chip_state & 4)) {
			raw_scsi_set_signal_phase(rs, false, true, false);
		} else if ((v & 4) && !(scsi->chip_state & 4)) {
			if (rs->bus_phase == SCSI_SIGNAL_PHASE_SELECT_2) {
				raw_scsi_set_signal_phase(rs, false, false, false);
			}
		}
		scsi->chip_state = v;
	}
	uae_u8 t = raw_scsi_get_signal_phase(rs);
	if ((t & SCSI_IO_REQ) && (scsi->chip_state & 4))
		cia_parallelack();
}

static uae_u32 REGPARAM2 ncr80_lget(struct soft_scsi *ncr, uaecptr addr)
{
	uae_u32 v;
	v =  ncr80_bget2(ncr, addr + 0, 4) << 24;
	v |= ncr80_bget2(ncr, addr + 1, 4) << 16;
	v |= ncr80_bget2(ncr, addr + 2, 4) <<  8;
	v |= ncr80_bget2(ncr, addr + 3, 4) <<  0;
	return v;
}

static uae_u32 REGPARAM2 ncr80_wget(struct soft_scsi *ncr, uaecptr addr)
{
	uae_u32 v;
	v = ncr80_bget2(ncr, addr, 2) << 8;
	v |= ncr80_bget2(ncr, addr + 1, 2);
	return v;
}

static uae_u32 REGPARAM2 ncr80_bget(struct soft_scsi *ncr, uaecptr addr)
{
	uae_u32 v;
	addr &= ncr->board_mask;
	if (!ncr->configured) {
		addr &= 65535;
		if (addr >= sizeof ncr->acmemory)
			return 0;
		return ncr->acmemory[addr];
	}
	v = ncr80_bget2(ncr, addr, 1);
	return v;
}

static void REGPARAM2 ncr80_lput(struct soft_scsi *ncr, uaecptr addr, uae_u32 l)
{
	ncr80_bput2(ncr, addr + 0, l >> 24, 4);
	ncr80_bput2(ncr, addr + 1, l >> 16, 4);
	ncr80_bput2(ncr, addr + 2, l >>  8, 4);
	ncr80_bput2(ncr, addr + 3, l >>  0, 4);
}

static void REGPARAM2 ncr80_wput(struct soft_scsi *ncr, uaecptr addr, uae_u32 w)
{
	w &= 0xffff;
	if (!ncr->configured) {
		return;
	}
	ncr80_bput2(ncr, addr, w >> 8, 2);
	ncr80_bput2(ncr, addr + 1, w & 0xff, 2);
}

static void REGPARAM2 ncr80_bput(struct soft_scsi *ncr, uaecptr addr, uae_u32 b)
{
	b &= 0xff;
	addr &= ncr->board_mask;
	if (!ncr->configured) {
		addr &= 65535;
		switch (addr)
		{
			case 0x48:
			map_banks_z2(ncr->bank, expamem_z2_pointer >> 16, ncr->board_size >> 16);
			ncr->baseaddress = expamem_z2_pointer;
			ncr->configured = 1;
			expamem_next (ncr->bank, NULL);
			break;
			case 0x4c:
			ncr->configured = 1;
			expamem_shutup(ncr->bank);
			break;
		}
		return;
	}
	ncr80_bput2(ncr, addr, b, 1);
}

static void REGPARAM2 soft_generic_bput (uaecptr addr, uae_u32 b)
{
	struct soft_scsi *ncr = getscsiboard(addr);
	if (ncr)
		ncr80_bput(ncr, addr, b);
}

static void REGPARAM2 soft_generic_wput (uaecptr addr, uae_u32 b)
{
	struct soft_scsi *ncr = getscsiboard(addr);
	if (ncr)
		ncr80_wput(ncr, addr, b);
}
static void REGPARAM2 soft_generic_lput (uaecptr addr, uae_u32 b)
{
	struct soft_scsi *ncr = getscsiboard(addr);
	if (ncr)
		ncr80_lput(ncr, addr, b);
}
static uae_u32 REGPARAM2 soft_generic_bget (uaecptr addr)
{
	struct soft_scsi *ncr = getscsiboard(addr);
	if (ncr)
		return ncr80_bget(ncr, addr);
	return 0;
}
static uae_u32 REGPARAM2 soft_generic_wget (uaecptr addr)
{
	struct soft_scsi *ncr = getscsiboard(addr);
	if (ncr)
		return ncr80_wget(ncr, addr);
	return 0;
}
static uae_u32 REGPARAM2 soft_generic_lget (uaecptr addr)
{
	struct soft_scsi *ncr = getscsiboard(addr);
	if (ncr)
		return ncr80_lget(ncr, addr);
	return 0;
}

static int REGPARAM2 soft_check(uaecptr addr, uae_u32 size)
{
	struct soft_scsi *ncr = getscsiboard(addr);
	if (!ncr)
		return 0;
	if (!ncr->rom)
		return 0;
	return 1;
}
static uae_u8 *REGPARAM2 soft_xlate(uaecptr addr)
{
	struct soft_scsi *ncr = getscsiboard(addr);
	if (!ncr)
		return 0;
	return ncr->rom + (addr & (ncr->rom_size - 1));
}

addrbank soft_bank_generic = {
	soft_generic_lget, soft_generic_wget, soft_generic_bget,
	soft_generic_lput, soft_generic_wput, soft_generic_bput,
	soft_xlate, soft_check, NULL, NULL, _T("LOWLEVEL/5380 SCSI"),
	soft_generic_lget, soft_generic_wget,
	ABFLAG_IO | ABFLAG_SAFE, S_READ, S_WRITE
};

void soft_scsi_put(uaecptr addr, int size, uae_u32 v)
{
	if (size == 4)
		soft_generic_lput(addr, v);
	else if (size == 2)
		soft_generic_wput(addr, v);
	else
		soft_generic_bput(addr, v);
}
uae_u32 soft_scsi_get(uaecptr addr, int size)
{
	uae_u32 v;
	if (size == 4)
		v = soft_generic_lget(addr);
	else if (size == 2)
		v = soft_generic_wget(addr);
	else
		v = soft_generic_bget(addr);
	return v;
}

/*
	$8380 select unit (unit mask)

	$8200
	 6: REQ (1=active)
	 8: BSY (0=active)
	10: C/D (1=data)
	13: I/O (1=to target)
	15: If not status?

	$8080 write data
	$8000 read data

*/

addrbank *supra_init(struct romconfig *rc)
{
	struct soft_scsi *scsi = getscsi(rc);

	if (!scsi)
		return &expamem_null;

	scsi->intena = true;

	const struct expansionromtype *ert = get_device_expansion_rom(ROMTYPE_SUPRA);
	struct zfile *z = NULL;
	scsi->subtype = rc->subtype;
	if (!rc->autoboot_disabled && scsi->subtype != 3) {
		for (int i = 0; i < 16; i++) {
			uae_u8 b = ert->subtypes[rc->subtype].autoconfig[i];
			ew(scsi, i * 4, b);
		}
		load_rom_rc(rc, ROMTYPE_SUPRA, 16384, 0, scsi->rom, 32768, LOADROM_EVENONLY_ODDONE | LOADROM_FILL);
	}
	return scsi->bank;
}

void supra_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	generic_soft_scsi_add(ch, ci, rc, NCR5380_SUPRA, 65536, 2 * 16384, ROMTYPE_SUPRA);
}

addrbank *golem_init(struct romconfig *rc)
{
	struct soft_scsi *scsi = getscsi(rc);
	
	if (!scsi)
		return &expamem_null;

	scsi->intena = true;

	load_rom_rc(rc, ROMTYPE_GOLEM, 8192, rc->autoboot_disabled ? 8192 : 0, scsi->rom, 8192, 0);
	memcpy(scsi->acmemory, scsi->rom, sizeof scsi->acmemory);

	return scsi->bank;
}

void golem_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	generic_soft_scsi_add(ch, ci, rc, NONCR_GOLEM, 65536, 8192, ROMTYPE_GOLEM);
}

addrbank *stardrive_init(struct romconfig *rc)
{
	struct soft_scsi *scsi = getscsi(rc);
	
	if (!scsi)
		return &expamem_null;

	const struct expansionromtype *ert = get_device_expansion_rom(ROMTYPE_STARDRIVE);
	for (int i = 0; i < 16; i++) {
		uae_u8 b = ert->autoconfig[i];
		ew(scsi, i * 4, b);
	}
	return scsi->bank;
}

void stardrive_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	generic_soft_scsi_add(ch, ci, rc, NCR5380_STARDRIVE, 65536, 0, ROMTYPE_STARDRIVE);
}

addrbank *kommos_init(struct romconfig *rc)
{
	struct soft_scsi *scsi = getscsi(rc);
	
	if (!scsi)
		return NULL;

	scsi->configured = 1;

	load_rom_rc(rc, ROMTYPE_KOMMOS, 32768, 0, scsi->rom, 32768, 0);

	map_banks(scsi->bank, 0xf10000 >> 16, 1, 0);
	map_banks(scsi->bank, 0xeb0000 >> 16, 1, 0);
	scsi->baseaddress = 0xeb0000;
	scsi->baseaddress2 = 0xf10000;

	return NULL;
}

void kommos_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	generic_soft_scsi_add(ch, ci, rc, NONCR_KOMMOS, 65536, 32768, ROMTYPE_KOMMOS);
}

addrbank *vector_init(struct romconfig *rc)
{
	struct soft_scsi *scsi = getscsi(rc);
	int roms[2];
	
	if (!scsi)
		return &expamem_null;

	roms[0] = 128;
	roms[1] = -1;

	scsi->intena = true;

	if (!rc->autoboot_disabled) {
		load_rom_rc(rc, ROMTYPE_VECTOR, 32768, 0, scsi->rom, 32768, 0);
		memcpy(scsi->acmemory, scsi->rom, sizeof scsi->acmemory);
	}
	return scsi->bank;
}

void vector_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	generic_soft_scsi_add(ch, ci, rc, NONCR_VECTOR, 65536, 32768, ROMTYPE_VECTOR);
}


addrbank *protar_init(struct romconfig *rc)
{
	struct soft_scsi *scsi = getscsi(rc);
	
	if (!scsi)
		return &expamem_null;

	load_rom_rc(rc, ROMTYPE_PROTAR, 32768, 0, scsi->rom, 32768, LOADROM_EVENONLY_ODDONE);
	memcpy(scsi->acmemory, scsi->rom + 0x200 * 2, sizeof scsi->acmemory);
	return scsi->bank;
}

void protar_add_ide_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	generic_soft_scsi_add(ch, ci, rc, NCR5380_PROTAR, 65536, 65536, ROMTYPE_PROTAR);
}

addrbank *add500_init(struct romconfig *rc)
{
	struct soft_scsi *scsi = getscsi(rc);
	
	if (!scsi)
		return &expamem_null;

	load_rom_rc(rc, ROMTYPE_ADD500, 16384, 0, scsi->rom, 32768, LOADROM_EVENONLY_ODDONE | LOADROM_FILL);
	memcpy(scsi->acmemory, scsi->rom, sizeof scsi->acmemory);
	return scsi->bank;
}

void add500_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	generic_soft_scsi_add(ch, ci, rc, NCR5380_ADD500, 65536, 32768, ROMTYPE_ADD500);
}

addrbank *kronos_init(struct romconfig *rc)
{
	struct soft_scsi *scsi = getscsi(rc);
	
	if (!scsi)
		return &expamem_null;

	scsi->databuffer_size = 1024;
	scsi->databufferptr = xcalloc(uae_u8, scsi->databuffer_size);

	load_rom_rc(rc, ROMTYPE_KRONOS, 4096, 0, scsi->rom, 32768, LOADROM_EVENONLY_ODDONE | LOADROM_FILL);
	return scsi->bank;
}

void kronos_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	generic_soft_scsi_add(ch, ci, rc, NCR5380_KRONOS, 65536, 32768, ROMTYPE_KRONOS);
}

addrbank *adscsi_init(struct romconfig *rc)
{
	struct soft_scsi *scsi = getscsi(rc);
	
	if (!scsi)
		return &expamem_null;

	load_rom_rc(rc, ROMTYPE_ADSCSI, 32768, 0, scsi->rom, 65536, LOADROM_EVENONLY_ODDONE | LOADROM_FILL);
	memcpy(scsi->acmemory, scsi->rom, sizeof scsi->acmemory);
	return scsi->bank;
}

void adscsi_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	generic_soft_scsi_add(ch, ci, rc, NCR5380_ADSCSI, 65536, 65536, ROMTYPE_ADSCSI);
}

bool rochard_scsi_init(struct romconfig *rc, uaecptr baseaddress)
{
	struct soft_scsi *scsi = getscsi(rc);
	scsi->configured = 1;
	scsi->dma_controller = true;
	scsi->baseaddress = baseaddress;
	return scsi != NULL;
}
void rochard_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	generic_soft_scsi_add(ch, ci, rc, NCR5380_ROCHARD, 65536, -1, ROMTYPE_ROCHARD);
}

uae_u8 rochard_scsi_get(uaecptr addr)
{
	return soft_generic_bget(addr);
}

void rochard_scsi_put(uaecptr addr, uae_u8 v)
{
	soft_generic_bput(addr, v);
}

addrbank *cltda1000scsi_init(struct romconfig *rc) {
	struct soft_scsi *scsi = getscsi(rc);

	if (!scsi)
		return &expamem_null;

	scsi->intena = true;
	scsi->delayed_irq = true;

	const struct expansionromtype *ert = get_device_expansion_rom(ROMTYPE_CLTDSCSI);
	for (int i = 0; i < 16; i++) {
		uae_u8 b = ert->autoconfig[i];
		ew(scsi, i * 4, b);
	}
	return scsi->bank;
}

void cltda1000scsi_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	generic_soft_scsi_add(ch, ci, rc, NCR5380_CLTD, 65536, 0, ROMTYPE_CLTDSCSI);
}

addrbank *ptnexus_init(struct romconfig *rc)
{
	struct soft_scsi *scsi = getscsi(rc);

	if (!scsi)
		return &expamem_null;

	scsi->intena = true;
	scsi->delayed_irq = true;

	const struct expansionromtype *ert = get_device_expansion_rom(ROMTYPE_PTNEXUS);
	for (int i = 0; i < 16; i++) {
		uae_u8 b = ert->autoconfig[i];
		ew(scsi, i * 4, b);
	}

	load_rom_rc(rc, ROMTYPE_PTNEXUS, 8192, 0, scsi->rom, 65536, LOADROM_EVENONLY_ODDONE | LOADROM_FILL);
	return scsi->bank;
}

void ptnexus_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	generic_soft_scsi_add(ch, ci, rc, NCR5380_PTNEXUS, 65536, 65536, ROMTYPE_PTNEXUS);
}

addrbank *dataflyer_init(struct romconfig *rc)
{
	struct soft_scsi *scsi = getscsi(rc);

	if (!scsi)
		return &expamem_null;

	scsi->baseaddress = (currprefs.cs_ide == IDE_A4000) ? 0xdd2000 : 0xda0000;
	scsi->configured = true;

	gayle_dataflyer_enable(true);

	return NULL;
}

void dataflyer_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	generic_soft_scsi_add(ch, ci, rc, NCR5380_DATAFLYER, 4096, 0, ROMTYPE_DATAFLYER);
}

static void expansion_add_protoautoconfig_data(uae_u8 *p, uae_u16 manufacturer_id, uae_u8 product_id)
{
	memset(p, 0, 4096);
	p[0x02] = product_id;
	p[0x08] = manufacturer_id >> 8;
	p[0x0a] = manufacturer_id;
}

static void expansion_add_protoautoconfig_box(uae_u8 *p, int box_size, uae_u16 manufacturer_id, uae_u8 product_id)
{
	expansion_add_protoautoconfig_data(p, manufacturer_id, product_id);
	// "box without init/diagnostics code"
	p[0] = 0x40 | (box_size << 3);
}
static void expansion_add_protoautoconfig_board(uae_u8 *p, int board, uae_u16 manufacturer_id, uae_u8 product_id, int memorysize)
{
	p += (board + 1) * 4096;
	expansion_add_protoautoconfig_data(p, manufacturer_id, product_id);
	// "board without init/diagnostic code"
	p[0] = 0x08;
	if (memorysize) {
		int v = 0;
		switch (memorysize)
		{
			case 64 * 1024:
			v = 1;
			break;
			case 128 * 1024:
			v = 2;
			break;
			case 256 * 1024:
			v = 3;
			break;
			case 512 * 1024:
			v = 4;
			break;
			case 1024 * 1024:
			v = 5;
			break;
			case 2048 * 1024:
			v = 6;
			break;
			case 4096 * 1024:
			default:
			v = 7;
			break;
		}
		p[0] |= v;
	}
}

addrbank *tecmar_init(struct romconfig *rc)
{
	struct soft_scsi *scsi = getscsi(rc);
	int index = 0;

	if (!scsi)
		return &expamem_null;

	const struct expansionromtype *ert = get_device_expansion_rom(ROMTYPE_PTNEXUS);
	scsi->rom = xcalloc(uae_u8, 65536);
	expansion_add_protoautoconfig_box(scsi->rom, 3, 1001, 0);
	// memory
	expansion_add_protoautoconfig_board(scsi->rom, index++, 1001, 1, currprefs.fastmem2_size);
	// clock
	expansion_add_protoautoconfig_board(scsi->rom, index++, 1001, 2, 0);
	// serial
	expansion_add_protoautoconfig_board(scsi->rom, index++, 1001, 3, 0);
	// parallel
	expansion_add_protoautoconfig_board(scsi->rom, index++, 1001, 4, 0);
	// sasi
	expansion_add_protoautoconfig_board(scsi->rom, index++, 1001, 4, 0);
	memset(tecmar_clock_regs, 0, sizeof tecmar_clock_regs);
	tecmar_clock_regs[11] = 0x04 | 0x02 | 0x01;
	scsi->configured = true;
	scsi->baseaddress = 0xe80000;
	return scsi->bank;
}

void tecmar_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	generic_soft_scsi_add(ch, ci, rc, NONCR_TECMAR, 65536, 65536, ROMTYPE_TECMAR);
}

addrbank *microforge_init(struct romconfig *rc)
{
	struct soft_scsi *scsi = getscsi(rc);

	if (!scsi)
		return NULL;

	scsi->configured = 1;

	map_banks(scsi->bank, 0xef0000 >> 16, 0x10000 >> 16, 0);
	scsi->baseaddress = 0xef0000;
	return NULL;
}

void microforge_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	generic_soft_scsi_add(ch, ci, rc, NONCR_MICROFORGE, 65536, 0, ROMTYPE_MICROFORGE);
}

addrbank *xebec_init(struct romconfig *rc)
{
	struct soft_scsi *scsi = getscsi(rc);

	if (!scsi)
		return NULL;

	scsi->configured = 1;

	map_banks(scsi->bank, 0x600000 >> 16, (0x800000 - 0x600000) >> 16, 0);
	scsi->board_mask = 0x1fffff;
	scsi->baseaddress = 0x600000;
	scsi->level6 = true;
	scsi->intena = true;
	scsi->dma_controller = true;
	scsi->databuffer_size = 32768;
	scsi->databufferptr = xcalloc(uae_u8, scsi->databuffer_size);
	return NULL;
}

void xebec_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	generic_soft_scsi_add(ch, ci, rc, NCR5380_XEBEC, 65536, 0, ROMTYPE_XEBEC);
}

addrbank *paradox_init(struct romconfig *rc)
{
	struct soft_scsi *scsi = getscsi(rc);

	if (!scsi)
		return NULL;

	scsi->configured = 1;
	parallel_port_scsi = true;
	parallel_port_scsi_data = scsi;

	return NULL;
}

void paradox_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	generic_soft_scsi_add(ch, ci, rc, NONCR_PARADOX, 0, 0, ROMTYPE_PARADOX);
}

addrbank *hda506_init(struct romconfig *rc)
{
	struct soft_scsi *scsi = getscsi(rc);

	if (!scsi)
		return NULL;

	const struct expansionromtype *ert = get_device_expansion_rom(ROMTYPE_HDA506);
	for (int i = 0; i < 16; i++) {
		uae_u8 b = ert->autoconfig[i];
		ew(scsi, i * 4, b);
	}
	scsi->level6 = true;
	scsi->intena = true;
	return scsi->bank;
}

void hda506_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	generic_soft_scsi_add(ch, ci, rc, OMTI_HDA506, 0, 0, ROMTYPE_HDA506);
}

addrbank *alf1_init(struct romconfig *rc)
{
	struct soft_scsi *scsi = getscsi(rc);

	if (!scsi)
		return NULL;
	map_banks(scsi->bank, 0xef0000 >> 16, 0x10000 >> 16, 0);
	scsi->board_mask = 0xffff;
	scsi->baseaddress = 0xef0000;
	scsi->configured = 1;

	return scsi->bank;
}

void alf1_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	generic_soft_scsi_add(ch, ci, rc, OMTI_ALF1, 65536, 0, ROMTYPE_ALF1);
}

addrbank *promigos_init(struct romconfig *rc)
{
	struct soft_scsi *scsi = getscsi(rc);

	if (!scsi)
		return NULL;
	map_banks(scsi->bank, 0xf40000 >> 16, 0x10000 >> 16, 0);
	scsi->board_mask = 0xffff;
	scsi->baseaddress = 0xf40000;
	scsi->configured = 1;
	scsi->intena = true;

	return scsi->bank;
}

void promigos_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	generic_soft_scsi_add(ch, ci, rc, OMTI_PROMIGOS, 65536, 0, ROMTYPE_PROMIGOS);
}

addrbank *system2000_init(struct romconfig *rc)
{
	struct soft_scsi *scsi = getscsi(rc);

	if (!scsi)
		return NULL;
	map_banks(scsi->bank, 0xf00000 >> 16, 0x10000 >> 16, 0);
	scsi->board_mask = 0xffff;
	scsi->baseaddress = 0xf00000;
	scsi->configured = 1;
	if (!rc->autoboot_disabled) {
		load_rom_rc(rc, NULL, 16384, 0, scsi->rom, 16384, 0);
	}
	return scsi->bank;
}

void system2000_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	generic_soft_scsi_add(ch, ci, rc, OMTI_SYSTEM2000, 65536, 16384, ROMTYPE_SYSTEM2000);
}

addrbank *omtiadapter_init(struct romconfig *rc)
{
	struct soft_scsi *scsi = getscsi(rc);

	if (!scsi)
		return NULL;
	map_banks(scsi->bank, 0x8f0000 >> 16, 0x10000 >> 16, 0);
	scsi->board_mask = 0xffff;
	scsi->baseaddress = 0x8f0000;
	scsi->configured = 1;
	return scsi->bank;
}

void omtiadapter_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	generic_soft_scsi_add(ch, ci, rc, OMTI_ADAPTER, 65536, 0, ROMTYPE_OMTIADAPTER);
}

extern void x86_xt_ide_bios(struct zfile*, struct romconfig *rc);
addrbank *x86_xt_hd_init(struct romconfig *rc)
{
	struct soft_scsi *scsi = getscsi(rc);

	if (!scsi)
		return NULL;
	struct zfile *f = read_device_from_romconfig(rc, 0);
	x86_xt_ide_bios(f, rc);
	zfile_fclose(f);
	scsi->configured = 1;
	x86_hd_data = scsi;
	return NULL;
}

void x86_add_xt_hd_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc)
{
	generic_soft_scsi_add(ch, ci, rc, OMTI_X86, 0, 0, ROMTYPE_X86_HD);
}

void soft_scsi_free(void)
{
	parallel_port_scsi = false;
	parallel_port_scsi_data = NULL;
	x86_hd_data = NULL;
	for (int i = 0; soft_scsi_devices[i]; i++) {
		soft_scsi_free_unit(soft_scsi_devices[i]);
		soft_scsi_devices[i] = NULL;
	}
}

void soft_scsi_reset(void)
{
	for (int i = 0; soft_scsi_devices[i]; i++) {
		raw_scsi_reset(&soft_scsi_devices[i]->rscsi);
	}
}
