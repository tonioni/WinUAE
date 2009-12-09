/*
* UAE - The Un*x Amiga Emulator
*
* CDTV DMAC/CDROM controller emulation
*
* Copyright 2004/2007 Toni Wilen
*
* Thanks to Mark Knibbs <markk@clara.co.uk> for CDTV Technical information
*
*/

//#define ROMHACK
//#define ROMHACK2
//#define CDTV_DEBUG
#define CDTV_DEBUG_CMD
//#define CDTV_DEBUG_6525

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "debug.h"
#include "cdtv.h"
#include "blkdev.h"
#include "gui.h"
#include "zfile.h"
#include "threaddep/thread.h"
#include "a2091.h"
#include "uae.h"

/* DMAC CNTR bits. */
#define CNTR_TCEN               (1<<7)
#define CNTR_PREST              (1<<6)
#define CNTR_PDMD               (1<<5)
#define CNTR_INTEN              (1<<4)
#define CNTR_DDIR               (1<<3)
/* ISTR bits. */
#define ISTR_INTX               (1<<8)
#define ISTR_INT_F              (1<<7)
#define ISTR_INTS               (1<<6)
#define ISTR_E_INT              (1<<5)
#define ISTR_INT_P              (1<<4)
#define ISTR_UE_INT             (1<<3)
#define ISTR_OE_INT             (1<<2)
#define ISTR_FF_FLG             (1<<1)
#define ISTR_FE_FLG             (1<<0)

#define AUDIO_STATUS_NOT_SUPPORTED  0x00
#define AUDIO_STATUS_IN_PROGRESS    0x11
#define AUDIO_STATUS_PAUSED         0x12
#define AUDIO_STATUS_PLAY_COMPLETE  0x13
#define AUDIO_STATUS_PLAY_ERROR     0x14
#define AUDIO_STATUS_NO_STATUS      0x15

#define MODEL_NAME "MATSHITA0.96"
/* also MATSHITA0.97 exists but is apparently rare */

static smp_comm_pipe requests;
static volatile int thread_alive;

static int configured;
static uae_u8 dmacmemory[100];

#define	MAX_TOC_ENTRIES 103
static uae_u8 cdrom_toc[MAX_TOC_ENTRIES * 13];
static uae_u32 last_cd_position, play_start, play_end;
static uae_u8 cdrom_qcode[16], cd_audio_status;
static int datatrack;

static volatile int cdtv_command_len;
static volatile uae_u8 cdtv_command_buf[6];
static volatile uae_u8 dmac_istr, dmac_cntr;
static volatile uae_u16 dmac_dawr;
static volatile uae_u32 dmac_acr;
static volatile int dmac_wtc;
static volatile int dmac_dma;

static volatile int activate_stch, cdrom_command_done, play_state, play_state_cmd, play_statewait;
static volatile int cdrom_sector, cdrom_sectors, cdrom_length, cdrom_offset;
static volatile int cd_playing, cd_paused, cd_motor, cd_media, cd_error, cd_finished, cd_isready, cd_hunt;

static volatile int cdtv_hsync, dma_finished, cdtv_sectorsize;
static volatile uae_u64 dma_wait;
static int first;
static int cd_volume;
static int cd_led;

#ifdef ROMHACK
#define ROM_VECTOR 0x2000
#define ROM_OFFSET 0x2000
static int rom_size, rom_mask;
static uae_u8 *rom;
#endif

static void do_stch (void);

static void INT2 (void)
{
	if (!(intreq & 8)) {
		INTREQ_0 (0x8000 | 0x0008);
	}
	cd_led ^= LED_CD_ACTIVE2;
}

static volatile int cdrom_command_cnt_out, cdrom_command_size_out;
static uae_u8 cdrom_command_output[16];

static volatile int stch, sten, scor, sbcp;
static volatile int cmd, enable, xaen, dten;

static int unitnum = -1;

/* convert minutes, seconds and frames -> logical sector number */
static int msf2lsn (int	msf)
{
	int sector = ((msf >> 16) & 0xff) * 60 * 75 + ((msf >> 8) & 0xff) * 75 + ((msf >> 0) & 0xff);
	if (sector < 0)
		sector = 0;
	return sector;
}

/* convert logical sector number -> minutes, seconds and frames */
static int lsn2msf (int	sectors)
{
	int msf;
	msf = (sectors / (75 * 60)) << 16;
	msf |= ((sectors / 75) % 60) << 8;
	msf |= (sectors % 75) << 0;
	return msf;
}

static int get_toc (void)
{
	uae_u8 *buf;
	int i;

	datatrack = 0;
	buf = sys_command_cd_toc (DF_IOCTL, unitnum);
	if (!buf)
		return 0;
	i = (buf[0] << 8) | (buf[1] << 0);
	memcpy (cdrom_toc, buf, i);
	last_cd_position = (buf[4 + 2 * 11 + 8] << 16) | (buf[4 + 2 * 11 + 9] << 8) | (buf[4 + 2 * 11 + 10] << 0);
	last_cd_position = lsn2msf (msf2lsn (last_cd_position) - 1);
	if (buf[4 + 3 * 11 + 3] == 1 && (buf[4 + 3 * 11 + 1] & 0x0c) == 0x04)
		datatrack = 1;
	return 1;
}

static void finished_cdplay (void)
{
	cd_audio_status = AUDIO_STATUS_PLAY_COMPLETE;
	cd_playing = 0;
	cd_finished = 1;
	cd_paused = 0;
	do_stch ();
}

static int get_qcode (void)
{
	uae_u8 *s;
	static uae_u8 subq0;

	memset (cdrom_qcode, 0, 16);
	s = sys_command_cd_qcode (DF_IOCTL, unitnum);
	if (!s)
		return 0;
	memcpy (cdrom_qcode, s, 16);
	if (cd_playing) {
		if (s[1] == AUDIO_STATUS_IN_PROGRESS) {
			int end = msf2lsn((s[5 + 4] << 16) | (s[6 + 4] << 8) | (s[7 + 4]));
			if (end >= play_end - 75)
				finished_cdplay ();
		} else if (s[1] == AUDIO_STATUS_PLAY_COMPLETE) {
			finished_cdplay ();
		}
	}
	s[1] = cd_audio_status;
	return 1;
}

static void cdaudiostop (void)
{
	cd_finished = 0;
	if (cd_playing)
		cd_finished = 1;
	cd_playing = 0;
	cd_paused = 0;
	cd_motor = 0;
	cd_audio_status = AUDIO_STATUS_NO_STATUS;
	if (unitnum < 0)
		return;
	sys_command_cd_pause (DF_IOCTL, unitnum, 0);
	sys_command_cd_stop (DF_IOCTL, unitnum);
}

static int pause_audio (int pause)
{
	sys_command_cd_pause (DF_IOCTL, unitnum, pause);
	if (!cd_playing) {
		cd_paused = 0;
		cd_audio_status = AUDIO_STATUS_NO_STATUS;
		return 0;
	}
	cd_paused = pause;
	cd_audio_status = pause ? AUDIO_STATUS_PAUSED : AUDIO_STATUS_IN_PROGRESS;
	return 1;
}

static int read_sectors (int start, int length)
{
#ifdef CDTV_DEBUG
	write_log (L"READ DATA sector %d, %d sectors (blocksize=%d)\n", start, length, cdtv_sectorsize);
#endif
	cdrom_sector = start;
	cdrom_sectors = length;
	cdrom_offset = start * cdtv_sectorsize;
	cdrom_length = length * cdtv_sectorsize;
	cd_motor = 1;
	cd_audio_status = AUDIO_STATUS_NOT_SUPPORTED;
	if (cd_playing)
		cdaudiostop ();
	return 0;
}

static int ismedia (void)
{
	if (unitnum < 0)
		return 0;
	return sys_command_ismedia (DF_IOCTL, unitnum, 0);
}

static void do_play (void)
{
	sys_command_cd_pause (DF_IOCTL, unitnum, 0);
	cd_audio_status = AUDIO_STATUS_PLAY_ERROR;
	if (sys_command_cd_play (DF_IOCTL, unitnum, lsn2msf (play_start), lsn2msf (play_end), 0)) {
		cd_audio_status = AUDIO_STATUS_IN_PROGRESS;
		cd_playing = 1;
	} else {
		cd_error = 1;
	}
	activate_stch = 1;
}

static int play_cdtrack (uae_u8 *p)
{
	int track_start = p[1];
	int index_start = p[2];
	int track_end = p[3];
	int index_end = p[4];
	uae_u32 start, end;
	int i, j;

	i = (cdrom_toc[0] << 8) | (cdrom_toc[1] << 0);
	i -= 2;
	i /= 11;
	end = last_cd_position;
	for (j = 0; j < i; j++) {
		uae_u8 *s = cdrom_toc + 4 + j * 11;
		if (track_start == s[3])
			start = (s[8] << 16) | (s[9] << 8) | s[10];
		if (track_end == s[3])
			end = (s[8] << 16) | (s[9] << 8) | s[10];
	}
	play_end = msf2lsn (end);
	play_start = msf2lsn (start);
#ifdef CDTV_DEBUG
	write_log (L"PLAY CD AUDIO from %d-%d, %06X (%d) to %06X (%d)\n",
		track_start, track_end, start, msf2lsn (start), end, msf2lsn (end));
#endif
	play_state = 1;
	play_state_cmd = 1;
	return 0;
}


static int play_cd (uae_u8 *p)
{
	uae_u32 start, end;

	start = (p[1] << 16) | (p[2] << 8) | p[3];
	end = (p[4] << 16) | (p[5] << 8) | p[6];
	if (p[0] == 0x09) /* end is length in lsn-mode */
		end += start;
	if (start == 0 && end == 0) {
		cd_finished = 0;
		if (cd_playing)
			cd_finished = 1;
		cd_playing = 0;
		cd_paused = 0;
		cd_motor = 0;
		cd_audio_status = AUDIO_STATUS_PLAY_COMPLETE;
		sys_command_cd_pause (DF_IOCTL, unitnum, 0);
		sys_command_cd_stop (DF_IOCTL, unitnum);
		cd_isready = 50;
		cd_error = 1;
		return 0;
	}
	if (p[0] == 0x09) { /* lsn */
		start = lsn2msf (start);
		if (end < 0x00ffffff)
			end = lsn2msf (end);
	}
	if (end == 0x00ffffff || end > last_cd_position)
		end = last_cd_position;
	play_end = msf2lsn (end);
	play_start = msf2lsn (start);
#ifdef CDTV_DEBUG
	write_log (L"PLAY CD AUDIO from %06X (%d) to %06X (%d)\n",
		start, msf2lsn (start), end, msf2lsn (end));
#endif
	play_state = 1;
	play_state_cmd = 1;
	return 0;
}

static int cdrom_subq (uae_u8 *out, int msflsn)
{
	uae_u8 *s = cdrom_qcode;
	uae_u32 trackposlsn, trackposmsf;
	uae_u32 diskposlsn, diskposmsf;

	get_qcode ();
	out[0] = cd_audio_status;
	s += 4;
	out[1] = s[1];
	out[2] = s[2];
	out[3] = s[3];
	trackposmsf = (s[9] << 16) | (s[10] << 8) | s[11];
	diskposmsf = (s[5] << 16) | (s[6] << 8) | s[7];
	trackposlsn = msf2lsn (trackposmsf);
	diskposlsn = msf2lsn (diskposmsf);
	out[4] = 0;
	out[5] = (msflsn ? diskposmsf : diskposlsn) >> 16;
	out[6] = (msflsn ? diskposmsf : diskposlsn) >> 8;
	out[7] = (msflsn ? diskposmsf : diskposlsn) >> 0;
	out[8] = 0;
	out[9] = (msflsn ? trackposmsf : trackposlsn) >> 16;
	out[10] = (msflsn ? trackposmsf : trackposlsn) >> 8;
	out[11] = (msflsn ? trackposmsf : trackposlsn) >> 0;
	out[12] = 0;

	return 13;
}

static int cdrom_info (uae_u8 *out)
{
	uae_u8 *p;
	uae_u32 size;
	int i;

	if (!ismedia ())
		return -1;
	cd_motor = 1;
	out[0] = cdrom_toc[2];
	i = (cdrom_toc[0] << 8) | (cdrom_toc[1] << 0);
	i -= 2 + 11;
	i /= 11;
	p = cdrom_toc + 4 + i * 11;
	out[1] = p[3];
	p = cdrom_toc + 4 + 2 * 11;
	size =  ((p[8] << 16) | (p[9] << 8) | p[10]);
	out[2] = size >> 16;
	out[3] = size >> 8;
	out[4] = size >> 0;
	cd_finished = 1;
	return 5;
}

static int read_toc (int track, int msflsn, uae_u8 *out)
{
	uae_u8 *buf = cdrom_toc, *s;
	int i, j;

	if (!ismedia ())
		return -1;
	if (!out)
		return 0;
	cd_motor = 1;
	i = (buf[0] << 8) | (buf[1] << 0);
	i -= 2;
	i /= 11;
	for (j = 0; j < i; j++) {
		s = buf + 4 + j * 11;
		if (track == s[3]) {
			uae_u32 msf = (s[8] << 16) | (s[9] << 8) | s[10];
			uae_u32 lsn = msf2lsn (msf);
			out[0] = 0;
			out[1] = s[1];
			out[2] = s[3];
			out[3] = buf[3];
			out[4] = 0;
			out[5] = (msflsn ? msf : lsn) >> 16;
			out[6] = (msflsn ? msf : lsn) >> 8;
			out[7] = (msflsn ? msf : lsn) >> 0;
			cd_finished = 1;
			return 8;
		}
	}
	return -1;
}

static int cdrom_modeset (uae_u8 *cmd)
{
	cdtv_sectorsize = (cmd[2] << 8) | cmd[3];
	if (cdtv_sectorsize != 2048 && cdtv_sectorsize != 2336) {
		write_log (L"CDTV: tried to set unknown sector size %d\n", cdtv_sectorsize);
		cdtv_sectorsize = 2048;
	}
	return 0;
}

static void cdrom_command_accepted (int size, uae_u8 *cdrom_command_input, int *cdrom_command_cnt_in)
{
#ifdef CDTV_DEBUG_CMD
	TCHAR tmp[200];
	int i;
#endif
	cdrom_command_size_out = size;
#ifdef CDTV_DEBUG_CMD
	tmp[0] = 0;
	for (i = 0; i < *cdrom_command_cnt_in; i++)
		_stprintf (tmp + i * 3, L"%02X%c", cdrom_command_input[i], i < *cdrom_command_cnt_in - 1 ? '.' : ' ');
	write_log (L"CD<-: %s\n", tmp);
	if (size > 0) {
		tmp[0] = 0;
		for (i = 0; i < size; i++)
			_stprintf (tmp + i * 3, L"%02X%c", cdrom_command_output[i], i < size - 1 ? '.' : ' ');
		write_log (L"CD->: %s\n", tmp);
	}
#endif
	*cdrom_command_cnt_in = 0;
	cdrom_command_cnt_out = 0;
	cdrom_command_done = 1;
}

static void cdrom_command_thread (uae_u8 b)
{
	static uae_u8 cdrom_command_input[16];
	static int cdrom_command_cnt_in;
	uae_u8 *s;

	cdrom_command_input[cdrom_command_cnt_in] = b;
	cdrom_command_cnt_in++;
	s = cdrom_command_input;

	switch (cdrom_command_input[0])
	{
	case 0x01: /* seek */
		if (cdrom_command_cnt_in == 7) {
			cdrom_command_accepted (0, s, &cdrom_command_cnt_in);
			cd_finished = 1;
		}
		break;
	case 0x02: /* read */
		if (cdrom_command_cnt_in == 7) {
			read_sectors((s[1] << 16) | (s[2] << 8) | (s[3] << 0), (s[4] << 8) | (s[5] << 0));
			cdrom_command_accepted (0, s, &cdrom_command_cnt_in);
		}
		break;
	case 0x04: /* motor on */
		if (cdrom_command_cnt_in == 7) {
			cd_motor = 1;
			cdrom_command_accepted (0, s, &cdrom_command_cnt_in);
			cd_finished = 1;
		}
		break;
	case 0x05: /* motor off */
		if (cdrom_command_cnt_in == 7) {
			cd_motor = 0;
			cdrom_command_accepted (0, s, &cdrom_command_cnt_in);
			cd_finished = 1;
		}
		break;
	case 0x09: /* play (lsn) */
	case 0x0a: /* play (msf) */
		if (cdrom_command_cnt_in == 7) {
			cdrom_command_accepted (play_cd (cdrom_command_input), s, &cdrom_command_cnt_in);
		}
		break;
	case 0x0b:
		if (cdrom_command_cnt_in == 7) {
			cdrom_command_accepted (play_cdtrack (cdrom_command_input), s, &cdrom_command_cnt_in);
		}
		break;
	case 0x81:
		if (cdrom_command_cnt_in == 1) {
			uae_u8 flag = 0;
			if (!cd_isready)
				flag |= 1 << 0;
			if (cd_playing)
				flag |= 1 << 2;
			if (cd_finished)
				flag |= 1 << 3;
			if (cd_error)
				flag |= 1 << 4;
			if (cd_motor)
				flag |= 1 << 5;
			if (cd_media)
				flag |= 1 << 6;
			cdrom_command_output[0] = flag;
			cdrom_command_accepted (1, s, &cdrom_command_cnt_in);
			cd_finished = 0;
			if (first == -1)
				first = 100;
		}
		break;
	case 0x82:
		if (cdrom_command_cnt_in == 7) {
			if (cd_error)
				cdrom_command_output[2] |= 1 << 4;
			cd_error = 0;
			cd_isready = 0;
			cdrom_command_accepted (6, s, &cdrom_command_cnt_in);
			cd_finished = 1;
		}
		break;
	case 0x83:
		if (cdrom_command_cnt_in == 7) {
			memcpy (cdrom_command_output, MODEL_NAME, strlen (MODEL_NAME)); 
			cdrom_command_accepted (strlen (MODEL_NAME), s, &cdrom_command_cnt_in);
			cd_finished = 1;
		}
	case 0x84:
		if (cdrom_command_cnt_in == 7) {
			cdrom_command_accepted (cdrom_modeset (cdrom_command_input), s, &cdrom_command_cnt_in);
			cd_finished = 1;
		}
		break;
	case 0x87: /* subq */
		if (cdrom_command_cnt_in == 7) {
			cdrom_command_accepted (cdrom_subq (cdrom_command_output, cdrom_command_input[1] & 2), s, &cdrom_command_cnt_in);
		}
		break;
	case 0x89:
		if (cdrom_command_cnt_in == 7) {
			cdrom_command_accepted (cdrom_info (cdrom_command_output), s, &cdrom_command_cnt_in);
		}
		break;
	case 0x8a: /* read toc */
		if (cdrom_command_cnt_in == 7) {
			cdrom_command_accepted (read_toc (cdrom_command_input[2], cdrom_command_input[1] & 2, cdrom_command_output), s, &cdrom_command_cnt_in);
		}
		break;
	case 0x8b:
		if (cdrom_command_cnt_in == 7) {
			pause_audio (s[1] == 0x00 ? 1 : 0);
			cdrom_command_accepted (0, s, &cdrom_command_cnt_in);
			cd_finished = 1;
		}
		break;
	case 0xa3: /* front panel */
		if (cdrom_command_cnt_in == 7) {
			cdrom_command_accepted (0, s, &cdrom_command_cnt_in);
			cd_finished = 1;
		}
		break;
	default:
		write_log (L"unknown CDROM command %02X!\n", s[0]);
		cd_error = 1;
		cdrom_command_accepted (0, s, &cdrom_command_cnt_in);
		break;
	}
}

static uae_u8 *read_raw (int sector, int size)
{
	int osector = sector;
	static struct zfile *f;
	static int track;
	int trackcnt;
	TCHAR fname[MAX_DPATH];
	static uae_u8 buf[4096];
	uae_u32 prevlsn = 0;
	uae_u8 *s = cdrom_toc + 4;

	memset (buf, 0, sizeof buf);
	trackcnt = 0;
	for (;;) {
		uae_u32 msf = (s[8] << 16) | (s[9] << 8) | s[10];
		uae_u32 lsn = msf2lsn (msf);
		if (s[3] >= 0xa0) {
			s += 11;
			continue;
		}
		if (sector < lsn - prevlsn)
			break;
		trackcnt++;
		sector -= lsn - prevlsn;
		prevlsn = lsn;
		s += 11;
	}
	if (track != trackcnt) {
		_stprintf (fname, L"track%d.bin", trackcnt);
		zfile_fclose (f);
		f = zfile_fopen (fname, L"rb", ZFD_NORMAL);
		if (!f)
			write_log (L"failed to open '%s'\n", fname);
		else
			write_log (L"opened '%s'\n", fname);
		track = trackcnt;
	}
	if (f) {
		write_log (L"CDTV fakeraw: %dx%d=%d\n", sector, size, sector * size);
		zfile_fseek (f, sector * size, SEEK_SET);
		zfile_fread (buf, size, 1, f);
		return buf;
	}
	return sys_command_cd_rawread (DF_IOCTL, unitnum, osector, size);
}

static void dma_do_thread (void)
{
	static int readsector;
	uae_u8 *p = NULL;
	int cnt;

	while (dma_finished)
		sleep_millis (2);

	if (!cdtv_sectorsize)
		return;
	cnt = dmac_wtc;
#ifdef CDTV_DEBUG
	write_log (L"DMAC DMA: sector=%d, addr=%08X, words=%d (of %d)\n",
		cdrom_offset / cdtv_sectorsize, dmac_acr, cnt, cdrom_length / 2);
#endif
	dma_wait += cnt * (uae_u64)312 * 50 / 75 + 1;
	while (cnt > 0 && dmac_dma) {
		if (!p || readsector != (cdrom_offset / cdtv_sectorsize)) {
			readsector = cdrom_offset / cdtv_sectorsize;
			if (cdtv_sectorsize == 2336)
				p = read_raw (readsector, cdtv_sectorsize);
			else
				p = sys_command_cd_read (DF_IOCTL, unitnum, readsector);
			if (!p) {
				cd_error = 1;
				activate_stch = 1;
				write_log (L"CDTV: CD read error!\n");
				break;
			}

		}
		put_byte (dmac_acr, p[(cdrom_offset % cdtv_sectorsize) + 0]);
		put_byte (dmac_acr + 1, p[(cdrom_offset % cdtv_sectorsize) + 1]);
		cnt--;
		dmac_acr += 2;
		cdrom_length -= 2;
		cdrom_offset += 2;
	}
	dmac_wtc = 0;
	dmac_dma = 0;
	dma_finished = 1;
	cd_finished = 1;
}

static void *dev_thread (void *p)
{
	write_log (L"CDTV: CD thread started\n");
	thread_alive = 1;
	for (;;) {

		uae_u32 b = read_comm_pipe_u32_blocking (&requests);
		if (unitnum < 0)
			continue;

		switch (b)
		{
		case 0x0100:
			dma_do_thread ();
			break;
		case 0x0101:
			{
				if (ismedia () != cd_media) {
					cd_media = ismedia ();
					get_toc ();
					activate_stch = 1;
					if (cd_playing)
						cd_error = 1;
					if (!cd_media)
						cd_hunt = 1;
				}
				if (cd_media)
					get_qcode ();
			}
			break;
		case 0x0102: // pause
			sys_command_cd_pause (DF_IOCTL, unitnum, 1);
			break;
		case 0x0103: // unpause
			sys_command_cd_pause (DF_IOCTL, unitnum, 0);
			break;
		case 0x0104: // stop
			cdaudiostop ();
			break;
		case 0x0110: // do_play!
			do_play ();
			break;
		case 0xffff:
			thread_alive = -1;
			return NULL;
		default:
			cdrom_command_thread (b);
			break;
		}

	}
}

static void cdrom_command (uae_u8 b)
{
	write_comm_pipe_u32 (&requests, b, 1);
}

static uae_u8 tp_a, tp_b, tp_c, tp_ad, tp_bd, tp_cd;
static uae_u8 tp_imr, tp_cr, tp_air;

static void tp_check_interrupts (void)
{
	/* MC = 1 ? */
	if ((tp_cr & 1) != 1)
		return;

	if (sten == 1) {
		sten = -1;
		if (tp_cd & (1 << 3))
			tp_air |= 1 << 3;
	}
	if ((tp_air & tp_cd) & 0x1f)
		INT2 ();
}


static void tp_bput (int addr, uae_u8 v)
{
	static int volstrobe1, volstrobe2;
#ifdef CDTV_DEBUG_6525
	write_log (L"6525 write %x=%02X PC=%x\n", addr, v, M68K_GETPC);
#endif
	switch (addr)
	{
	case 0:
		tp_a = v;
		break;
	case 1:
		tp_b = v;
		break;
	case 2:
		if (!(tp_cr & 1))
			tp_c = v;
		break;
	case 3:
		tp_ad = v;
		break;
	case 4:
		tp_bd = v;
		break;
	case 5:
		tp_cd = v;
		break;
	case 6:
		tp_cr = v;
		break;
	case 7:
		tp_air = v;
		break;
	}
	cmd = (tp_b >> 0) & 1;
	enable = (tp_b >> 1) & 1;
	xaen = (tp_b >> 2) & 1;
	dten = (tp_b >> 3) & 1;

	if (!volstrobe1 && ((tp_b >> 6) & 1)) {
		cd_volume >>= 1;
		cd_volume |= ((tp_b >> 5) & 1) << 11;
		volstrobe1 = 1;
	} else if (volstrobe1 && !((tp_b >> 6) & 1)) {
		volstrobe1 = 0;
	}
	if (!volstrobe2 && ((tp_b >> 7) & 1)) {
#ifdef CDTV_DEBUG_CMD
		write_log (L"CDTV CD volume = %d\n", cd_volume);
#endif
		cd_volume = 0;
		volstrobe2 = 1;
	} else if (volstrobe2 && !((tp_b >> 7) & 1)) {
		volstrobe2 = 0;
	}
}

static uae_u8 tp_bget (int addr)
{
	uae_u8 v = 0;
	switch (addr)
	{
	case 0:
		v = tp_a;
		write_log (L"TPA read!\n");
		break;
	case 1:
		v = tp_b;
		break;
	case 2:
		v = (sbcp << 0) | ((scor ^ 1) << 1) | ((stch ^ 1) << 2) | (sten << 3);
		if (tp_cr & 1) {
			if (!v)
				v |= 1 << 5; // /IRQ
		} else {
			v |= tp_c & ~(0x80 | 0x40);
		}
		v |= tp_c & (0x80 | 0x40);
		sbcp = 0;
		break;
	case 3:
		v = tp_ad;
		break;
	case 4:
		v = tp_bd;
		break;
	case 5:
		v = tp_cd;
		break;
	case 6:
		v = tp_cr;
		break;
	case 7:
		v = tp_air;
		tp_air = 0;
		break;
	}

#ifdef CDTV_DEBUG_6525
	if (addr < 7)
		write_log (L"6525 read %x=%02X PC=%x\n", addr, v, M68K_GETPC);
#endif
	return v;
}

static uae_u32 REGPARAM3 dmac_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 dmac_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM3 dmac_bget (uaecptr) REGPARAM;
static void REGPARAM3 dmac_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 dmac_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM3 dmac_bput (uaecptr, uae_u32) REGPARAM;

static void dmac_start_dma (void)
{
	if (!(dmac_cntr & CNTR_PDMD)) { // non-scsi dma
		write_comm_pipe_u32 (&requests, 0x0100, 1);
	}
}

void cdtv_getdmadata (uae_u32 *acr)
{
	*acr = dmac_acr;
}

static void do_hunt (void)
{
	int i;
	for (i = 0; i < MAX_TOTAL_DEVICES; i++) {
		if (sys_command_ismedia (DF_IOCTL, i, 1) > 0)
			break;
	}
	if (i == MAX_TOTAL_DEVICES)
		return;
	if (unitnum >= 0) {
		cdaudiostop ();
		sys_command_close (DF_IOCTL, unitnum);
	}
	if (sys_command_open (DF_IOCTL, i) > 0) {
		unitnum = i;
		cd_hunt = 0;
		write_log (L"CDTV: autodetected unit %d\n", unitnum);
	} else {
		unitnum = -1;
	}
}

static void checkint (void)
{
	int irq = 0;

	if (currprefs.cs_cdtvscsi && (wdscsi_getauxstatus () & 0x80)) {
		dmac_istr |= ISTR_INTS;
		if ((dmac_cntr & CNTR_INTEN) && (dmac_istr & ISTR_INTS))
			irq = 1;
	}
	if ((dmac_cntr & CNTR_INTEN) && (dmac_istr & ISTR_E_INT))
		irq = 1;
	if (irq)
		INT2 ();
}

void cdtv_scsi_int (void)
{
	checkint ();
}
void cdtv_scsi_clear_int (void)
{
	dmac_istr &= ~ISTR_INTS;
}

void rethink_cdtv (void)
{
	checkint ();
	tp_check_interrupts ();
}


void CDTV_hsync_handler (void)
{
	static int subqcnt;

	if (!currprefs.cs_cdtvcd || !configured)
		return;

	cdtv_hsync++;

	if (dma_wait >= 1024)
		dma_wait -= 1024;
	if (dma_wait >= 0 && dma_wait < 1024 && dma_finished) {
		if ((dmac_cntr & (CNTR_INTEN | CNTR_TCEN)) == (CNTR_INTEN | CNTR_TCEN)) {
			dmac_istr |= ISTR_INT_P | ISTR_E_INT;
#ifdef CDTV_DEBUG
			write_log (L"DMA finished\n");
#endif
		}
		dma_finished = 0;
		cdtv_hsync = -1;
	}
	checkint();

	if (cdrom_command_done) {
		cdrom_command_done = 0;
		sten = 1;
		tp_check_interrupts ();
	}

	if (cdtv_hsync < 312 * 50 / 75 && cdtv_hsync >= 0)
		return;
	cdtv_hsync = 0;

	if (first > 0) {
		first--;
		if (first == 0)
			do_stch ();
	}

	if (play_state == 1) {
		play_state = 2;
		cd_playing = 1;
		cd_motor = 1;
		activate_stch = 1;
		play_statewait = 5;
	} else if (play_statewait > 0) {
		play_statewait--;
	} else if (play_state == 2) {
		write_comm_pipe_u32 (&requests, 0x0110, 1);
		play_state = 0;
	}

	if (cd_isready > 0) {
		cd_isready--;
		if (!cd_isready)
			do_stch ();
	}
	if (cd_playing)
		cd_led |= LED_CD_AUDIO;
	else
		cd_led &= ~LED_CD_AUDIO;
	if (dmac_dma)
		cd_led |= LED_CD_ACTIVE;
	else
		cd_led &= ~LED_CD_ACTIVE;
	if (cd_led)
		gui_cd_led (0, cd_led);

	if (cd_media && (tp_cr & 1)) {
		tp_air |= 1 << 1;
		INT2 ();
	}

	subqcnt--;
	if (subqcnt < 0) {
		write_comm_pipe_u32 (&requests, 0x0101, 1);
		subqcnt = 75;
		if (cd_hunt)
			do_hunt ();
	}
	if (activate_stch)
		do_stch ();
}

static void do_stch (void)
{
	static int stch_cnt;

	if ((tp_cr & 1) && !(tp_air & (1 << 2)) && (tp_cd & (1 << 2))) {
		activate_stch = 0;
		tp_air |= 1 << 2;
		INT2 ();
#ifdef CDTV_DEBUG
		write_log (L"STCH %d\n", stch_cnt++);
#endif
	}
}

void bleh (void)
{
#if 0
	cd_playing = cd_finished = cd_motor = cd_media = 1;
	cd_isready = 0;
	cd_playing = 0;
	do_stch();
#endif
}

static void cdtv_reset (void)
{
	write_log (L"CDTV: reset\n");
	cdaudiostop();
	cd_playing = cd_paused = 0;
	cd_motor = 0;
	cd_media = 0;
	cd_error = 0;
	cd_finished = 0;
	cd_led = 0;
	stch = 0;
	first = -1;
}

static uae_u32 dmac_bget2 (uaecptr addr)
{
	uae_u8 v = 0;

	if (addr < 0x40)
		return dmacmemory[addr];

	if (addr >= 0xb0 && addr < 0xc0)
		return tp_bget ((addr - 0xb0) / 2);

#ifdef ROMHACK
	if (addr >= ROM_OFFSET) {
		if (rom) {
			int off = addr & rom_mask;
			return rom[off];
		}
		return 0;
	}
#endif

	switch (addr)
	{
	case 0x41:
		v = dmac_istr;
		if (v)
			v |= ISTR_INT_P;
		dmac_istr &= ~0xf;
		break;
	case 0x43:
		v = dmac_cntr;
		break;
	case 0x91:
		if (currprefs.cs_cdtvscsi)
			v = wdscsi_getauxstatus ();
		break;
	case 0x93:
		if (currprefs.cs_cdtvscsi) {
			v = wdscsi_get ();
			checkint ();
		}
		break;
	case 0xa1:
		if (cdrom_command_cnt_out >= 0) {
			v = cdrom_command_output[cdrom_command_cnt_out];
			cdrom_command_output[cdrom_command_cnt_out++] = 0;
			if (cdrom_command_cnt_out >= cdrom_command_size_out) {
				stch = 1;
				sten = 0;
				cdrom_command_size_out = 0;
				cdrom_command_cnt_out = -1;
			} else {
				sten = 1;
				tp_check_interrupts ();
			}
		}
		break;
	case 0xe8:
	case 0xe9:
		dmac_istr |= ISTR_FE_FLG;
		break;
		/* XT IO */
	case 0xa3:
	case 0xa5:
	case 0xa7:
		v = 0xff;
		break;
	}

#ifdef CDTV_DEBUG
	if (addr != 0x41)
		write_log (L"dmac_bget %04X=%02X PC=%08X\n", addr, v, M68K_GETPC);
#endif

	return v;
}

static void dmac_bput2 (uaecptr addr, uae_u32 b)
{
	if (addr >= 0xb0 && addr < 0xc0) {
		tp_bput ((addr - 0xb0) / 2, b);
		return;
	}

#ifdef CDTV_DEBUG
	write_log (L"dmac_bput %04X=%02X PC=%08X\n", addr, b & 255, M68K_GETPC);
#endif

	switch (addr)
	{
	case 0x43:
		dmac_cntr = b;
		if (dmac_cntr & CNTR_PREST)
			cdtv_reset ();
		break;
	case 0x80:
		dmac_wtc &= 0x00ffffff;
		dmac_wtc |= b << 24;
		break;
	case 0x81:
		dmac_wtc &= 0xff00ffff;
		dmac_wtc |= b << 16;
		break;
	case 0x82:
		dmac_wtc &= 0xffff00ff;
		dmac_wtc |= b << 8;
		break;
	case 0x83:
		dmac_wtc &= 0xffffff00;
		dmac_wtc |= b << 0;
		break;
	case 0x84:
		dmac_acr &= 0x00ffffff;
		dmac_acr |= b << 24;
		break;
	case 0x85:
		dmac_acr &= 0xff00ffff;
		dmac_acr |= b << 16;
		break;
	case 0x86:
		dmac_acr &= 0xffff00ff;
		dmac_acr |= b << 8;
		break;
	case 0x87:
		dmac_acr &= 0xffffff01;
		dmac_acr |= (b & ~ 1) << 0;
		break;
	case 0x8e:
		dmac_dawr &= 0x00ff;
		dmac_dawr |= b << 8;
		break;
	case 0x8f:
		dmac_dawr &= 0xff00;
		dmac_dawr |= b << 0;
		break;
	case 0x91:
		if (currprefs.cs_cdtvscsi) {
			wdscsi_sasr (b);
			checkint ();
		}
		break;
	case 0x93:
		if (currprefs.cs_cdtvscsi) {
			wdscsi_put (b);
			checkint ();
		}
		break;
	case 0xa1:
		cdrom_command (b);
		break;
	case 0xe0:
	case 0xe1:
		if (!dmac_dma) {
			dmac_dma = 1;
			dmac_start_dma ();
		}
		break;
	case 0xe2:
	case 0xe3:
		dmac_dma = 0;
		dma_finished = 0;
		break;
	case 0xe4:
	case 0xe5:
		dmac_istr = 0;
		checkint ();
		break;
	case 0xe8:
	case 0xe9:
		dmac_istr |= ISTR_FE_FLG;
		break;
	}

	tp_check_interrupts ();
}

static uae_u32 REGPARAM2 dmac_lget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= 65535;
	v = (dmac_bget2 (addr) << 24) | (dmac_bget2 (addr + 1) << 16) |
		(dmac_bget2 (addr + 2) << 8) | (dmac_bget2 (addr + 3));
#ifdef CDTV_DEBUG
	write_log (L"dmac_lget %08X=%08X PC=%08X\n", addr, v, M68K_GETPC);
#endif
	return v;
}

static uae_u32 REGPARAM2 dmac_wget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= 65535;
	v = (dmac_bget2 (addr) << 8) | dmac_bget2 (addr + 1);
#ifdef CDTV_DEBUG
	write_log (L"dmac_wget %08X=%04X PC=%08X\n", addr, v, M68K_GETPC);
#endif
	return v;
}

static uae_u32 REGPARAM2 dmac_bget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= 65535;
	v = dmac_bget2 (addr);
	if (!configured)
		return v;
	return v;
}

static void REGPARAM2 dmac_lput (uaecptr addr, uae_u32 l)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= 65535;
#ifdef CDTV_DEBUG
	write_log (L"dmac_lput %08X=%08X PC=%08X\n", addr, l, M68K_GETPC);
#endif
	dmac_bput2 (addr, l >> 24);
	dmac_bput2 (addr + 1, l >> 16);
	dmac_bput2 (addr + 2, l >> 8);
	dmac_bput2 (addr + 3, l);
}

static void REGPARAM2 dmac_wput (uaecptr addr, uae_u32 w)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= 65535;
#ifdef CDTV_DEBUG
	write_log (L"dmac_wput %04X=%04X PC=%08X\n", addr, w & 65535, M68K_GETPC);
#endif
	dmac_bput2 (addr, w >> 8);
	dmac_bput2 (addr + 1, w);
}

static void REGPARAM2 dmac_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= 65535;
	b &= 0xff;
	if (addr == 0x48) {
		map_banks (&dmac_bank, b, 0x10000 >> 16, 0x10000);
		write_log (L"CDTV DMAC autoconfigured at %02X0000\n", b);
		configured = 1;
		expamem_next ();
		return;
	}
	if (addr == 0x4c) {
		write_log (L"CDTV DMAC AUTOCONFIG SHUT-UP!\n");
		configured = 1;
		expamem_next ();
		return;
	}
	if (!configured)
		return;
	dmac_bput2 (addr, b);
}

static void open_unit (void)
{
	struct device_info di1, *di2;
	int first = -1;
	int cdtvunit = -1, audiounit = -1;

	if (unitnum >= 0)
		sys_command_close (DF_IOCTL, unitnum);
	unitnum = -1;
	cdtv_reset ();
	if (!device_func_init (DEVICE_TYPE_ANY)) {
		write_log (L"no CDROM support\n");
		return;
	}
	for (unitnum = 0; unitnum < MAX_TOTAL_DEVICES; unitnum++) {
		if (sys_command_open (DF_IOCTL, unitnum)) {
			di2 = sys_command_info (DF_IOCTL, unitnum, &di1);
			if (di2 && di2->type == INQ_ROMD) {
				write_log (L"%s: ", di2->label);
				if (first < 0)
					first = unitnum;
				if (get_toc () > 0) {
					if (datatrack) {
						uae_u8 *p = sys_command_cd_read (DF_IOCTL, unitnum, 16);
						if (p) {
							if (!memcmp (p + 8, "CDTV", 4)) {
								write_log (L"CDTV\n");
								if (cdtvunit < 0)
									cdtvunit = unitnum;
							}
						}
					} else {
						write_log (L"Audio CD\n");
						if (audiounit < 0)
							audiounit = unitnum;
					}
				} else {
					write_log (L"TOC read failed\n");
				}
			}
			sys_command_close (DF_IOCTL, unitnum);
		}
	}
	unitnum = audiounit;
	if (cdtvunit >= 0)
		unitnum = cdtvunit;
	if (unitnum < 0)
		unitnum = first;
	cd_media = 0;
	if (unitnum >= 0) {
		sys_command_open (DF_IOCTL, unitnum);
		cd_media = ismedia () ? -1 : 0;
		if (!cd_media)
			cd_hunt = 1;
		if (!get_toc())
			cd_media = 0;
		cdaudiostop ();
	}
}

static void ew (int addr, uae_u32 value)
{
	addr &= 0xffff;
	if (addr == 00 || addr == 02 || addr == 0x40 || addr == 0x42) {
		dmacmemory[addr] = (value & 0xf0);
		dmacmemory[addr + 2] = (value & 0x0f) << 4;
	} else {
		dmacmemory[addr] = ~(value & 0xf0);
		dmacmemory[addr + 2] = ~((value & 0x0f) << 4);
	}
}

static uae_u32 REGPARAM2 dmac_wgeti (uaecptr addr)
{
	uae_u32 v = 0xffff;
#ifdef JIT
	special_mem |= S_READ;
#endif
#ifdef ROMHACK
	addr &= 65535;
	if (addr >= ROM_OFFSET)
		v = (rom[addr & rom_mask] << 8) | rom[(addr + 1) & rom_mask];
#endif
	return v;
}
static uae_u32 REGPARAM2 dmac_lgeti (uaecptr addr)
{
	uae_u32 v = 0xffff;
#ifdef JIT
	special_mem |= S_READ;
#endif
#ifdef ROMHACK
	addr &= 65535;
	v = (dmac_wgeti(addr) << 16) | dmac_wgeti(addr + 2);
#endif
	return v;
}


addrbank dmac_bank = {
	dmac_lget, dmac_wget, dmac_bget,
	dmac_lput, dmac_wput, dmac_bput,
	default_xlate, default_check, NULL, L"CDTV DMAC/CD Controller",
	dmac_lgeti, dmac_wgeti, ABFLAG_IO
};


void cdtv_entergui (void)
{
	if (cd_playing && !cd_paused)
		write_comm_pipe_u32 (&requests, 0x102, 1);
}
void cdtv_exitgui (void)
{
	if (cd_playing && !cd_paused)
		write_comm_pipe_u32 (&requests, 0x103, 1);
}


/* CDTV batterybacked RAM emulation */
#define CDTV_NVRAM_MASK 16383
#define CDTV_NVRAM_SIZE 32768
static uae_u8 cdtv_battram[CDTV_NVRAM_SIZE];

void cdtv_loadcardmem(uae_u8 *p, int size)
{
	struct zfile *f;

	memset (p, 0, size);
	f = zfile_fopen (currprefs.flashfile, L"rb", ZFD_NORMAL);
	if (!f)
		return;
	zfile_fseek (f, CDTV_NVRAM_SIZE, SEEK_SET);
	zfile_fread (p, size, 1, f);
	zfile_fclose (f);
}

void cdtv_savecardmem(uae_u8 *p, int size)
{
	struct zfile *f;

	f = zfile_fopen (currprefs.flashfile, L"rb+", ZFD_NORMAL);
	if (!f)
		return;
	zfile_fseek (f, CDTV_NVRAM_SIZE, SEEK_SET);
	zfile_fwrite (p, size, 1, f);
	zfile_fclose (f);
}

static void cdtv_battram_reset (void)
{
	struct zfile *f;
	int v;

	memset (cdtv_battram, 0, CDTV_NVRAM_SIZE);
	f = zfile_fopen (currprefs.flashfile, L"rb+", ZFD_NORMAL);
	if (!f) {
		f = zfile_fopen (currprefs.flashfile, L"wb", 0);
		if (f) {
			zfile_fwrite (cdtv_battram, CDTV_NVRAM_SIZE, 1, f);
			zfile_fclose (f);
		}
		return;
	}
	v = zfile_fread (cdtv_battram, 1, CDTV_NVRAM_SIZE, f);
	if (v < CDTV_NVRAM_SIZE)
		zfile_fwrite (cdtv_battram + v, 1, CDTV_NVRAM_SIZE - v, f);
	zfile_fclose (f);
}

void cdtv_battram_write (int addr, int v)
{
	struct zfile *f;
	int offset = addr & CDTV_NVRAM_MASK;

	if (offset >= CDTV_NVRAM_SIZE)
		return;
	if (cdtv_battram[offset] == v)
		return;
	cdtv_battram[offset] = v;
	f = zfile_fopen (currprefs.flashfile, L"rb+", ZFD_NORMAL);
	if (!f)
		return;
	zfile_fseek (f, offset, SEEK_SET);
	zfile_fwrite (cdtv_battram + offset, 1, 1, f);
	zfile_fclose (f);
}

uae_u8 cdtv_battram_read (int addr)
{
	uae_u8 v;
	int offset;
	offset = addr & CDTV_NVRAM_MASK;
	if (offset >= CDTV_NVRAM_SIZE)
		return 0;
	v = cdtv_battram[offset];
	return v;
}

int cdtv_add_scsi_unit(int ch, TCHAR *path, int blocksize, int readonly,
	TCHAR *devname, int sectors, int surfaces, int reserved,
	int bootpri, TCHAR *filesys)
{
	return addscsi (ch, path, blocksize, readonly, devname, sectors, surfaces, reserved, bootpri, filesys, 1);
}

void cdtv_free (void)
{
	if (thread_alive > 0) {
		dmac_dma = 0;
		dma_finished = 0;
		write_comm_pipe_u32 (&requests, 0x0104, 1);
		write_comm_pipe_u32 (&requests, 0xffff, 1);
		while (thread_alive > 0)
			sleep_millis (10);
	}
	thread_alive = 0;
	cdaudiostop ();
	if (unitnum >= 0)
		sys_command_close (DF_IOCTL, unitnum);
	unitnum = -1;
	configured = 0;
}

#ifdef ROMHACK2
extern uae_u8 *extendedkickmemory, *cardmemory;
static void romhack (void)
{
	struct zfile *z;
	int roms[5];
	struct romlist *rl;
	int rom_size;
	uae_u8 *rom, *p;

	extendedkickmemory[0x558c] = 0xff;

	roms[0] = 55;
	roms[1] = 54;
	roms[2] = 53;
	roms[3] = -1;

	rl = getromlistbyids(roms);
	if (rl) {
		write_log (L"A590/A2091 BOOT ROM '%s' %d.%d\n", rl->path, rl->rd->ver, rl->rd->rev);
		z = zfile_fopen(rl->path, "rb", ZFD_NORMAL);
		if (z) {
			rom_size = 16384;
			rom = (uae_u8*)xmalloc (rom_size);
			zfile_fread (rom, rom_size, 1, z);
			rom[0x2071] = 0xe0; rom[0x2072] |= 0x40;
			rom[0x2075] = 0xe0; rom[0x2076] |= 0x40;
			rom[0x207d] = 0xe0; rom[0x207e] |= 0x40;
			rom[0x2081] = 0xe0; rom[0x2082] |= 0x40;
			rom[0x2085] = 0xe0; rom[0x2086] |= 0x40;
			rom[0x207b] = 0x32;
			p = cardmemory + 0x4000;
			memcpy (p, rom + 0x2000, 0x2000);
			memcpy (p + 0x2000, rom, 0x2000);
		}
		zfile_fclose(z);
	}
	//kickmemory[0x3592c] = 0xff;
}
#endif

void cdtv_init (void)
{
	if (!thread_alive) {
		init_comm_pipe (&requests, 100, 1);
		uae_start_thread (L"cdtv", dev_thread, NULL, NULL);
		while (!thread_alive)
			sleep_millis(10);
	}
	write_comm_pipe_u32 (&requests, 0x0104, 1);

	configured = 0;
	tp_a = tp_b = tp_c = tp_ad = tp_bd = tp_cd = 0;
	tp_imr = tp_cr = tp_air = 0;
	stch = 1;
	sten = 1;
	scor = 1;
	sbcp = 0;
	cdrom_command_cnt_out = -1;
	cmd = enable = xaen = dten = 0;
	memset (dmacmemory, 0xff, 100);
	ew (0x00, 0xc0 | 0x01);
	ew (0x04, 0x03);
	ew (0x08, 0x40);
	ew (0x10, 0x02);
	ew (0x14, 0x02);

	ew (0x18, 0x00); /* ser.no. Byte 0 */
	ew (0x1c, 0x00); /* ser.no. Byte 1 */
	ew (0x20, 0x00); /* ser.no. Byte 2 */
	ew (0x24, 0x00); /* ser.no. Byte 3 */

#ifdef ROMHACK2
	romhack();
#endif

	/* KS autoconfig handles the rest */
	map_banks (&dmac_bank, 0xe80000 >> 16, 0x10000 >> 16, 0x10000);
	cdtv_battram_reset ();
	open_unit ();
}
