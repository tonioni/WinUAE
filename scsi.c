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

static int outcmd[] = { 0x0a, 0x2a, 0xaa, -1 };
static int incmd[] = { 0x08, 0x12, 0x1a, 0x25, 0x28, 0xa8, 0x37, -1 };

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
    return 0;
}

void scsi_emulate_cmd(struct scsi_data *sd)
{
    sd->status = 0;
    if (sd->cmd[0] == 0x03) { /* REQUEST SENSE */
	int len = sd->buffer[4];
	memset (sd->buffer, 0, len);
	memcpy (sd->buffer, sd->sense, sd->sense_len > len ? len : sd->sense_len);
    } else {
	sd->status = scsi_emulate(&sd->hfd->hfd, sd->hfd,
	    sd->cmd, sd->len, sd->buffer, &sd->data_len, sd->reply, &sd->reply_len, sd->sense, &sd->sense_len);
	if (sd->status == 0) {
	    if (sd->reply_len > 0) {
		memset(sd->buffer, 0, 256);
		memcpy(sd->buffer, sd->reply, sd->reply_len);
	    }
	}
    }
    sd->offset = 0;
}

struct scsi_data *scsi_alloc(struct hd_hardfiledata *hfd)
{
    struct scsi_data *sd = xcalloc(sizeof (struct scsi_data), 1);
    sd->hfd = hfd;
    return sd;
}

void scsi_free(struct scsi_data *sd)
{
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
	sd->buffer[sd->offset++] = b;
    } else {
	if (sd->offset >= 16) {
	    write_log("SCSI command buffer overflow!\n");
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
