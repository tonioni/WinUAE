/*
 * UAE CD device policy shared by native ioctl and SPTI backends.
 * Copyright 2002-2010 Toni Wilen
 * Copyright 2024 Dimitris Panokostas
 */
#include "sysconfig.h"
#include "sysdeps.h"
#include "blkdev_ioctl_common.h"
#include "scsidev.h"
#include "gui.h"
#include <limits.h>

int host_cd_unit_index(const int *table, int count, int unit)
{
    if (unit < 0 || unit >= count || table[unit] <= 0 || table[unit] > count)
	return -1;
    return table[unit] - 1;
}
int host_cd_unit_number(const int *table, int count, int index)
{
    if (index < 0 || index >= count)
	return -1;
    for (int i = 0; i < count; i++)
	if (table[i] - 1 == index)
	    return i;
    return -1;
}
int host_cd_pause(cda_play *cda, int paused)
{
    if (!cda)
	return -1;
    int old = cda->cdda_paused;
    if ((paused && cda->cdda_play) || !paused)
	cda->cdda_paused = paused;
    return old;
}
int host_cd_stop(cda_play *cda)
{
    if (!cda)
	return 0;
    ciw_cdda_stop(cda);
    return 1;
}
uae_u32 host_cd_volume(cda_play *cda, uae_u16 left, uae_u16 right)
{
    if (!cda)
	return (uae_u32)-1;
    uae_u32 old = ((uae_u32)cda->cdda_volume[1] << 16) | (uae_u16)cda->cdda_volume[0];
    cda->cdda_volume[0] = left;
    cda->cdda_volume[1] = right;
    return old;
}
int host_cd_play(cda_play *cda, device_info *info, const host_cd_backend &backend,
		 cda_play_read_block read, int start, int end, int scan,
		 play_status_callback status, play_subchannel_callback sub, bool clear_subcode)
{
    if (!cda || !info)
	return 0;
    cda->di = info;
    if (clear_subcode)
	cda->subcodevalid = false;
    cda->cdda_play_finished = 0;
    cda->cdda_subfunc = sub;
    cda->cdda_statusfunc = status;
    cda->cdda_scan = scan ? 10 : 0;
    cda->cdda_delay = ciw_cdda_setstate(cda, -1, -1);
    cda->cdda_delay_frames = ciw_cdda_setstate(cda, -2, -1);
    ciw_cdda_setstate(cda, AUDIO_STATUS_NOT_SUPPORTED, -1);
    cda->read_block = read;
    if ((backend.open && !backend.open(backend.context)) || !isaudiotrack(&info->toc, start)) {
	ciw_cdda_setstate(cda, AUDIO_STATUS_PLAY_ERROR, -1);
	return 0;
    }
    if (!cda->cdda_play)
	uae_start_thread(_T("ioctl_cdda_play"), ciw_cdda_play, cda, NULL);
    cda->cdda_start = start;
    cda->cdda_end = end;
    cda->cd_last_pos = start;
    cda->cdda_play++;
    return 1;
}
int host_cd_copy_audio(uae_u8 *dest, const uae_u8 *source, int errorfield, int subcode)
{
    if (!dest || !source || errorfield < 0 || errorfield > 2 ||
	(subcode != 0 && subcode != 1 && subcode != 2 && subcode != 4))
	return -1;
    int bytes = 2352 + (errorfield == 1 ? 294 : errorfield == 2 ? 296 : 0);
    int raw = 2352 + (errorfield ? 296 : 0);
    memcpy(dest, source, bytes);
    if (subcode == 1) {
	memcpy(dest + bytes, source + raw, SUB_CHANNEL_SIZE);
	bytes += SUB_CHANNEL_SIZE;
    } else if (subcode == 4) {
	sub_to_deinterleaved(source + raw, dest + bytes);
	bytes += SUB_CHANNEL_SIZE;
    } else if (subcode == 2) {
	uae_u8 sub[SUB_CHANNEL_SIZE];
	sub_to_deinterleaved(source + raw, sub);
	memcpy(dest + bytes, sub + SUB_ENTRY_SIZE, SUB_ENTRY_SIZE);
	bytes += SUB_ENTRY_SIZE;
    }
    return bytes;
}
int host_cd_rawread(cda_play *cda, device_info *info, const host_cd_backend &backend, uae_u8 *data,
		    int sector, int count, int sectorsize, uae_u32 extra)
{
    if (!cda || !info || !data || !backend.read || count <= 0 || sector > INT_MAX - count)
	return 0;
    if (log_scsi)
	write_log(_T("IOCTL rawread unit=%d sector=%d blocksize=%d\n"), backend.unitnum, sector,
		  sectorsize);
    ciw_cdda_stop(cda);
    gui_flicker_led(LED_CD, backend.unitnum, LED_CD_ACTIVE);
    int ret = 0;
    if (sectorsize > 0) {
	if (sectorsize != 2048 && sectorsize != 2336 && sectorsize != 2352 &&
	    sectorsize != 2048 + 96 && sectorsize != 2336 + 96 && sectorsize != 2352 + 96)
	    return 0;
	if (count > INT_MAX / sectorsize)
	    return 0;
	for (int i = 0; i < count; i++, sector++) {
	    if (backend.read(backend.context, data, sector, 1, sectorsize) != sectorsize)
		break;
	    cda->cd_last_pos = sector;
	    data += sectorsize;
	    ret += sectorsize;
	}
    } else {
	int sectortype = (extra >> 16) & 255;
	int errorfield = (extra >> 9) & 3;
	int subcode = extra & 7;
	if ((subcode != 0 && subcode != 1 && subcode != 2 && subcode != 4) || errorfield == 3)
	    return -1;
	if (!isaudiotrack(&info->toc, sector))
	    return 0;
	if (sectortype != 0 && sectortype != 1)
	    return -2;
	int raw = 2352 + (errorfield ? 296 : 0) + (subcode ? SUB_CHANNEL_SIZE : 0);
	if (count > INT_MAX / raw)
	    return 0;
	for (int i = 0; i < count; i++, sector++) {
	    uae_u8 buffer[2352 + 296 + SUB_CHANNEL_SIZE] = {};
	    if (backend.read(backend.context, buffer, sector, 1, raw) != raw)
		break;
	    int bytes = host_cd_copy_audio(data, buffer, errorfield, subcode);
	    cda->cd_last_pos = sector;
	    data += bytes;
	    ret += bytes;
	}
    }
    return ret;
}
void host_cd_init_info(device_info *info, int unitnum)
{
    info->bus = unitnum;
    info->target = 0;
    info->lun = 0;
    info->media_inserted = 0;
    info->bytespersector = 2048;
}
void host_cd_finish_info(device_info *info, int unitnum, bool cdrom, const TCHAR *label)
{
    info->removable = cdrom;
    info->write_protected = cdrom;
    info->type = cdrom ? INQ_ROMD : INQ_DASD;
    info->unitnum = unitnum + 1;
    _tcsncpy(info->label, label, sizeof(info->label) / sizeof(TCHAR) - 1);
    info->label[sizeof(info->label) / sizeof(TCHAR) - 1] = 0;
    info->backend = _T("IOCTL");
}
int host_cd_scsiemu(int unitnum, const uae_u8 *command, int (*eject)(int, bool))
{
    if (command[0] != 0x1b)
	return -1;
    int mode = command[4] & 3;
    if (mode == 2 || mode == 3)
	eject(unitnum, mode == 2);
    return 1;
}
