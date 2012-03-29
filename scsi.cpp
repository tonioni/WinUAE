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

static int outcmd[] = { 0x0a, 0x2a, 0x2f, 0xaa, -1 };
static int incmd[] = { 0x03, 0x08, 0x12, 0x1a, 0x25, 0x28, 0x37, 0x42, 0x43, 0xa8, -1 };
static int nonecmd[] = { 0x00, 0x35, -1 };

int scsi_data_dir(struct scsi_data *sd)
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
	write_log (_T("SCSI command %02X, no direction specified (IN?)!\n"), sd->cmd[0]);
	return -2;
}

void scsi_emulate_cmd(struct scsi_data *sd)
{
	sd->status = 0;
	if (sd->cmd[0] == 0x03) { /* REQUEST SENSE */
		int len = sd->buffer[4];
		memset (sd->buffer, 0, len);
		memcpy (sd->buffer, sd->sense, sd->sense_len > len ? len : sd->sense_len);
		sd->data_len = len;
	} else if (sd->nativescsiunit < 0) {
		sd->status = scsi_emulate(&sd->hfd->hfd, sd->hfd,
			sd->cmd, sd->len, sd->buffer, &sd->data_len, sd->reply, &sd->reply_len, sd->sense, &sd->sense_len);
		if (sd->status == 0) {
			if (sd->reply_len > 0) {
				memset(sd->buffer, 0, 256);
				memcpy(sd->buffer, sd->reply, sd->reply_len);
			}
		}
	} else {
		struct amigascsi as;

		memset(sd->sense, 0, 256);
		memset(&as, 0, sizeof as);
		memcpy (&as.cmd, sd->cmd, sd->len);
		as.flags = 2 | 1;
		if (sd->direction > 0)
			as.flags &= ~1;
		as.sense_len = 32;
		as.cmd_len = sd->len;
		as.data = sd->buffer;
		as.len = sd->direction < 0 ? DEVICE_SCSI_BUFSIZE : sd->data_len;
		sys_command_scsi_direct_native(sd->nativescsiunit, &as);
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

struct scsi_data *scsi_alloc(int id, struct hd_hardfiledata *hfd)
{
	struct scsi_data *sd = xcalloc (struct scsi_data, 1);
	sd->hfd = hfd;
	sd->id = id;
	sd->nativescsiunit = -1;
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
	}
	xfree(sd);
}

void scsi_start_transfer(struct scsi_data *sd, int len)
{
	sd->len = len;
	sd->offset = 0;
}

int scsi_send_data(struct scsi_data *sd, uae_u8 b)
{
	if (sd->direction) {
		if (sd->offset >= SCSI_DATA_BUFFER_SIZE) {
			write_log (_T("SCSI data buffer overflow!\n"));
			return 0;
		}
		sd->buffer[sd->offset++] = b;
	} else {
		if (sd->offset >= 16) {
			write_log (_T("SCSI command buffer overflow!\n"));
			return 0;
		}
		sd->cmd[sd->offset++] = b;
	}
	if (sd->offset == sd->len)
		return 1;
	return 0;
}

int scsi_receive_data(struct scsi_data *sd, uae_u8 *b)
{
	*b = sd->buffer[sd->offset++];
	if (sd->offset == sd->len)
		return 1;
	return 0;
}
