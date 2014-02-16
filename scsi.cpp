/*
* UAE - The Un*x Amiga Emulator
*
* SCSI emulation (not uaescsi.device)
*
* Copyright 2007 Toni Wilen
*
*/

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "scsi.h"
#include "filesys.h"
#include "blkdev.h"
#include "zfile.h"

static int outcmd[] = { 0x0a, 0x2a, 0xaa, 0x15, 0x55, -1 };
static int incmd[] = { 0x01, 0x03, 0x05, 0x08, 0x12, 0x1a, 0x5a, 0x25, 0x28, 0x34, 0x37, 0x42, 0x43, 0xa8, 0x51, 0x52, 0xbd, -1 };
static int nonecmd[] = { 0x00, 0x0b, 0x11, 0x16, 0x17, 0x19, 0x1b, 0x1e, 0x2b, 0x35, -1 };
static int scsicmdsizes[] = { 6, 10, 10, 12, 16, 12, 10, 10 };

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

void scsi_emulate_analyze (struct scsi_data *sd)
{
	int cmd_len, data_len;

	data_len = sd->data_len;
	cmd_len = scsicmdsizes[sd->cmd[0] >> 5];
	sd->cmd_len = cmd_len;
	switch (sd->cmd[0])
	{
	case 0x0a:
		data_len = sd->cmd[4] * sd->blocksize;
	break;
	case 0x2a:
		data_len = ((sd->cmd[7] << 8) | (sd->cmd[8] << 0)) * (uae_s64)sd->blocksize;
	break;
	case 0xaa:
		data_len = ((sd->cmd[6] << 24) | (sd->cmd[7] << 16) | (sd->cmd[8] << 8) | (sd->cmd[9] << 0)) * (uae_s64)sd->blocksize;
	break;
	case 0x2f: // VERIFY
		if (sd->cmd[1] & 2) {
			sd->data_len = ((sd->cmd[7] << 8) | (sd->cmd[8] << 0)) * (uae_s64)sd->blocksize;
			sd->direction = 1;
		} else {
			sd->data_len = 0;
			sd->direction = 0;
		}
		return;
	}
	sd->data_len = data_len;
	sd->direction = scsi_data_dir (sd);
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
#if 0
	write_log (_T("REQUEST SENSE %d, "), sd->data_len);
	for (int i = 0; i < sd->data_len; i++) {
		if (i > 0)
			write_log (_T("."));
		write_log (_T("%02X"), sd->buffer[i]);
	}
	write_log (_T("\n"));
#endif
}
static void copysense(struct scsi_data *sd)
{
	int len = sd->cmd[4];
	//write_log (_T("REQUEST SENSE length %d (%d)\n"), len, sd->sense_len);
	memset(sd->buffer, 0, len);
	memcpy(sd->buffer, sd->sense, sd->sense_len > len ? len : sd->sense_len);
	if (sd->sense_len == 0)
		sd->buffer[0] = 0x70;
	sd->data_len = len;
	showsense (sd);
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
	//write_log (_T("CMD=%02x\n"), sd->cmd[0]);
	if (sd->device_type == UAEDEV_CD && sd->cd_emu_unit >= 0) {
		if (sd->cmd[0] == 0x03) { /* REQUEST SENSE */
			scsi_cd_emulate(sd->cd_emu_unit, sd->cmd, 0, 0, 0, 0, 0, 0, 0, sd->atapi); /* ack request sense */
			copysense(sd);
		} else {
			scsi_clear_sense(sd);
			sd->status = scsi_cd_emulate(sd->cd_emu_unit, sd->cmd, sd->cmd_len, sd->buffer, &sd->data_len, sd->reply, &sd->reply_len, sd->sense, &sd->sense_len, sd->atapi);
			copyreply(sd);
		}
	} else if (sd->device_type == UAEDEV_HDF && sd->nativescsiunit < 0) {
		if (sd->cmd[0] == 0x03) { /* REQUEST SENSE */
			copysense(sd);
		} else {
			scsi_clear_sense(sd);
			sd->status = scsi_hd_emulate(&sd->hfd->hfd, sd->hfd,
				sd->cmd, sd->cmd_len, sd->buffer, &sd->data_len, sd->reply, &sd->reply_len, sd->sense, &sd->sense_len);
			copyreply(sd);
		}
	} else if (sd->device_type == UAEDEV_TAPE && sd->nativescsiunit < 0) {
		if (sd->cmd[0] == 0x03) { /* REQUEST SENSE */
			scsi_tape_emulate(sd->tape, sd->cmd, 0, 0, 0, sd->reply, &sd->reply_len, sd->sense, &sd->sense_len); /* get request sense extra bits */
			copysense(sd);
		} else {
			scsi_clear_sense(sd);
			sd->status = scsi_tape_emulate(sd->tape,
				sd->cmd, sd->cmd_len, sd->buffer, &sd->data_len, sd->reply, &sd->reply_len, sd->sense, &sd->sense_len);
			copyreply(sd);
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

struct scsi_data *scsi_alloc_hd(int id, struct hd_hardfiledata *hfd)
{
	struct scsi_data *sd = xcalloc (struct scsi_data, 1);
	sd->hfd = hfd;
	sd->id = id;
	sd->nativescsiunit = -1;
	sd->cd_emu_unit = -1;
	sd->blocksize = hfd->hfd.ci.blocksize;
	sd->device_type = UAEDEV_HDF;
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
	xfree(sd);
}

void scsi_start_transfer(struct scsi_data *sd)
{
	sd->offset = 0;
}

int scsi_send_data(struct scsi_data *sd, uae_u8 b)
{
	if (sd->direction == 1) {
		if (sd->offset >= SCSI_DATA_BUFFER_SIZE) {
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
		write_log (_T("scsi_send_data() without direction!\n"));
		return 0;
	}
	if (sd->offset == sd->data_len)
		return 1;
	return 0;
}

int scsi_receive_data(struct scsi_data *sd, uae_u8 *b)
{
	if (!sd->data_len)
		return -1;
	*b = sd->buffer[sd->offset++];
	if (sd->offset == sd->data_len)
		return 1; // requested length got
	return 0;
}
