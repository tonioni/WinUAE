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
#include "debug.h"

#define SCSI_EMU_DEBUG 0
#define RAW_SCSI_DEBUG 0

extern int log_scsiemu;

static const int outcmd[] = { 0x0a, 0x2a, 0xaa, 0x15, 0x55, -1 };
static const int incmd[] = { 0x01, 0x03, 0x05, 0x08, 0x12, 0x1a, 0x5a, 0x25, 0x28, 0x34, 0x37, 0x42, 0x43, 0xa8, 0x51, 0x52, 0xbd, -1 };
static const int nonecmd[] = { 0x00, 0x0b, 0x11, 0x16, 0x17, 0x19, 0x1b, 0x1e, 0x2b, 0x35, -1 };
static const int scsicmdsizes[] = { 6, 10, 10, 12, 16, 12, 10, 10 };

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

void scsi_emulate_analyze (struct scsi_data *sd)
{
	int cmd_len, data_len, data_len2, tmp_len;

	data_len = sd->data_len;
	data_len2 = 0;
	cmd_len = scsicmdsizes[sd->cmd[0] >> 5];
	sd->cmd_len = cmd_len;
	switch (sd->cmd[0])
	{
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
	case 0x0a: // WRITE(6)
		data_len = sd->cmd[4] * sd->blocksize;
		scsi_grow_buffer(sd, data_len);
	break;
	case 0x2a: // WRITE(10)
		data_len = ((sd->cmd[7] << 8) | (sd->cmd[8] << 0)) * (uae_s64)sd->blocksize;
		scsi_grow_buffer(sd, data_len);
	break;
	case 0xaa: // WRITE(12)
		data_len = ((sd->cmd[6] << 24) | (sd->cmd[7] << 16) | (sd->cmd[8] << 8) | (sd->cmd[9] << 0)) * (uae_s64)sd->blocksize;
		scsi_grow_buffer(sd, data_len);
	break;
	case 0xbe: // READ CD
	case 0xb9: // READ CD MSF
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
	if (log_scsiemu) {
		for (int i = 0; i < sd->data_len; i++) {
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
#if SCSI_EMU_DEBUG
	write_log (_T("CMD=%02x.%02x.%02x.%02x.%02x.%02x (%d,%d)\n"),
		sd->cmd[0], sd->cmd[1], sd->cmd[2], sd->cmd[3], sd->cmd[4], sd->cmd[5], sd->device_type, sd->nativescsiunit);
#endif
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
			scsi_hd_emulate(&sd->hfd->hfd, sd->hfd, sd->cmd, 0, 0, 0, 0, 0, 0, 0);
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

void free_scsi (struct scsi_data *sd)
{
	if (!sd)
		return;
	hdf_hd_close (sd->hfd);
	scsi_free (sd);
}

int add_scsi_hd (struct scsi_data **sd, int ch, struct hd_hardfiledata *hfd, struct uaedev_config_info *ci, int scsi_level)
{
	free_scsi (sd[ch]);
	sd[ch] = NULL;
	if (!hfd) {
		hfd = xcalloc (struct hd_hardfiledata, 1);
		memcpy (&hfd->hfd.ci, ci, sizeof (struct uaedev_config_info));
	}
	if (!hdf_hd_open (hfd))
		return 0;
	hfd->ansi_version = scsi_level;
	sd[ch] = scsi_alloc_hd (ch, hfd);
	return sd[ch] ? 1 : 0;
}

int add_scsi_cd (struct scsi_data **sd, int ch, int unitnum)
{
	device_func_init (0);
	free_scsi (sd[ch]);
	sd[ch] = scsi_alloc_cd (ch, unitnum, false);
	return sd[ch] ? 1 : 0;
}

int add_scsi_tape (struct scsi_data **sd, int ch, const TCHAR *tape_directory, bool readonly)
{
	free_scsi (sd[ch]);
	sd[ch] = scsi_alloc_tape (ch, tape_directory, readonly);
	return sd[ch] ? 1 : 0;
}

void scsi_freenative(struct scsi_data **sd)
{
	for (int i = 0; i < MAX_TOTAL_SCSI_DEVICES; i++) {
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

	scsi_freenative (sd);
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
	uae_u8 data;
	int initiator_id, target_id;
	struct scsi_data *device[8];
	struct scsi_data *target;
};

void raw_scsi_reset(struct raw_scsi *rs)
{
	rs->target = NULL;
	rs->io = 0;
	rs->bus_phase = SCSI_SIGNAL_PHASE_FREE;
}

void raw_scsi_busfree(struct raw_scsi *rs)
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
	for (int i = 0; i < 8; i++) {
		if ((1 << i) & v)
			return i;
	}
	return -1;
}

void raw_scsi_set_signal_phase(struct raw_scsi *rs, bool busy, bool select, bool atn)
{
	switch (rs->bus_phase)
	{
		case SCSI_SIGNAL_PHASE_FREE:
		if (busy && !select) {
			rs->bus_phase = SCSI_SIGNAL_PHASE_ARBIT;
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
		if (!busy) {
			uae_u8 data = rs->data & ~(1 << rs->initiator_id);
			rs->target_id = getbit(data);
			if (rs->target_id >= 0) {
				rs->target = rs->device[rs->target_id];
				if (rs->target) {
#if RAW_SCSI_DEBUG
					write_log(_T("raw_scsi: selected id %d\n"), rs->target_id);
#endif
					rs->io |= SCSI_IO_BUSY;
				} else {
#if RAW_SCSI_DEBUG
					write_log(_T("raw_scsi: selected non-existing id %d\n"), rs->target_id);
#endif
					rs->target_id = -1;
				}
			}
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
			rs->bus_phase = rs->atn ? SCSI_SIGNAL_PHASE_MESSAGE_IN : SCSI_SIGNAL_PHASE_COMMAND;
			rs->io = SCSI_IO_BUSY | SCSI_IO_REQ;
		}
		break;
	}
}

uae_u8 raw_scsi_get_signal_phase(struct raw_scsi *rs)
{
	uae_u8 v = rs->io;
	if (rs->bus_phase >= 0)
		v |= rs->bus_phase;
	return v;
}

uae_u8 raw_scsi_get_data(struct raw_scsi *rs)
{
	struct scsi_data *sd = rs->target;
	uae_u8 v = 0;

	switch (rs->bus_phase)
	{
		case SCSI_SIGNAL_PHASE_FREE:
#if RAW_SCSI_DEBUG
		write_log(_T("raw_scsi: bus free\n"));
#endif
		v = 0;
		break;
		case SCSI_SIGNAL_PHASE_ARBIT:
#if RAW_SCSI_DEBUG
		write_log(_T("raw_scsi: arbitration\n"));
#endif
		v = rs->data;
		break;
		case SCSI_SIGNAL_PHASE_DATA_IN:
		if (scsi_receive_data(sd, & v)) {
			rs->bus_phase = SCSI_SIGNAL_PHASE_STATUS;
#if RAW_SCSI_DEBUG
			write_log(_T("raw_scsi: data in finished, %d bytes: status phase\n"), sd->offset);
#endif
		}
		break;
		case SCSI_SIGNAL_PHASE_STATUS:
#if RAW_SCSI_DEBUG
		write_log(_T("raw_scsi: status byte read %02x\n"), sd->status);
#endif
		v = sd->status;
		bus_free(rs);
		break;
		default:
		write_log(_T("raw_scsi_get_data but bus phase is %d!\n"), rs->bus_phase);
		break;
	}

	return v;
}

void raw_scsi_put_data(struct raw_scsi *rs, uae_u8 data)
{
	struct scsi_data *sd = rs->target;
	int len;

	rs->data = data;
	switch (rs->bus_phase)
	{
		case SCSI_SIGNAL_PHASE_ARBIT:
		rs->initiator_id = getbit(data);
		write_log(_T("raw_scsi: arbitration initiator id %d\n"), rs->initiator_id);
		break;
		case SCSI_SIGNAL_PHASE_SELECT_1:
		break;
		case SCSI_SIGNAL_PHASE_COMMAND:
		sd->cmd[sd->offset++] = data;
		len = scsicmdsizes[sd->cmd[0] >> 5];
		if (sd->offset >= len) {
#if RAW_SCSI_DEBUG
			write_log(_T("raw_scsi: got command %02x (%d bytes)\n"), sd->cmd[0], len);
#endif
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
		if (scsi_send_data(sd, data)) {
#if RAW_SCSI_DEBUG
			write_log(_T("raw_scsi: data out finished, %d bytes\n"), sd->data_len);
#endif
			scsi_emulate_cmd(sd);
			rs->bus_phase = SCSI_SIGNAL_PHASE_STATUS;
		}
		break;
		default:
		write_log(_T("raw_scsi_put_data but bus phase is %d!\n"), rs->bus_phase);
		break;
	}
}

struct apollo_soft_scsi
{
	bool enabled;
	int configured;
	bool autoconfig;
	bool irq;
	struct raw_scsi rscsi;
};
static struct apollo_soft_scsi apolloscsi[2];

void apollo_scsi_bput(uaecptr addr, uae_u8 v)
{
	int bank = addr & (0x800 | 0x400);
	struct apollo_soft_scsi *as = &apolloscsi[0];
	struct raw_scsi *rs = &as->rscsi;
	addr &= 0x3fff;
	if (bank == 0) {
		raw_scsi_put_data(rs, v);
	} else if (bank == 0xc00 && !(addr & 1)) {
		as->irq = (v & 64) != 0;
		raw_scsi_set_signal_phase(rs,
			(v & 128) != 0,
			(v & 32) != 0,
			false);
	} else if (bank == 0x400 && (addr & 1)) {
		raw_scsi_set_signal_phase(rs, true, false, false);
		raw_scsi_put_data(rs, v);
	}
	//write_log(_T("apollo scsi put %04x = %02x\n"), addr, v);
}

uae_u8 apollo_scsi_bget(uaecptr addr)
{
	int bank = addr & (0x800 | 0x400);
	struct apollo_soft_scsi *as = &apolloscsi[0];
	struct raw_scsi *rs = &as->rscsi;
	uae_u8 v = 0xff;
	addr &= 0x3fff;
	if (bank == 0) {
		v = raw_scsi_get_data(rs);
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

int apollo_add_scsi_unit(int ch, struct uaedev_config_info *ci, int devnum)
{
	struct raw_scsi *rs = &apolloscsi[devnum].rscsi;
	raw_scsi_reset(rs);
	if (ci->type == UAEDEV_CD)
		return add_scsi_cd(rs->device, ch, ci->device_emu_unit);
	else if (ci->type == UAEDEV_TAPE)
		return add_scsi_tape(rs->device, ch, ci->rootdir, ci->readonly);
	else
		return add_scsi_hd(rs->device, ch, NULL, ci, 1);
	return 0;
}

void apolloscsi_free(void)
{
	for (int j = 0; j < 2; j++) {
		struct raw_scsi *rs = &apolloscsi[j].rscsi;
		for (int i = 0; i < 8; i++) {
			free_scsi (rs->device[i]);
			rs->device[i] = NULL;
		}
	}
}

void apolloscsi_reset(void)
{
	raw_scsi_reset(&apolloscsi[0].rscsi);
	raw_scsi_reset(&apolloscsi[1].rscsi);
}