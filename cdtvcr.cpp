/*
* UAE - The Un*x Amiga Emulator
*
* CDTV-CR emulation
*
* Copyright 2014 Toni Wilen
*
*
*/

#define CDTVCR_DEBUG 1

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "newcpu.h"
#include "debug.h"
#include "zfile.h"
#include "gui.h"
#include "cdtvcr.h"
#include "blkdev.h"
#include "threaddep/thread.h"
#include "uae.h"
#include "custom.h"

#define CDTVCR_MASK 0xffff

#define CDTVCR_RAM_OFFSET 0x1000
#define CDTVCR_RAM_SIZE 4096
#define CDTVCR_RAM_MASK (CDTVCR_RAM_SIZE - 1)

#define CDTVCR_CLOCK 0xc10
#define CDTVCR_ID 0x9dc

#define CDTVCR_RESET 0xc00

#define CDTVCR_CD_CMD 0xc40
#define CDTVCR_CD_CMD_DO 0xc52
#define CDTVCR_CD_CMD_STATUS 0xc4e
#define CDTVCR_CD_CMD_STATUS2 0xc4f

#define CDTVCR_CD_STATE 0xc53
#define CDTVCR_SYS_STATE 0xc54
#define CDTVCR_CD_SPEED 0xc59
#define CDTVCR_CD_PLAYING 0xc5b
#define CDTVCR_CD_SUBCODES 0xc60
#define CDTVCR_CD_ALLOC 0xc61

#define CDTVCR_INTDISABLE 0xc04
#define CDTVCR_INTACK 0xc05
#define CDTVCR_INTENA 0xc55
#define CDTVCR_INTREQ 0xc56
#define CDTVCR_4510_TRIGGER 0x4000

#define CDTVCR_KEYCMD 0xc80

#define CDTVCR_SUBQ 0x906
#define CDTVCR_SUBBANK 0x917
#define CDTVCR_SUBC 0x918
#define CDTVCR_TOC 0xa00

static uae_u8 cdtvcr_4510_ram[CDTVCR_RAM_OFFSET];
static uae_u8 cdtvcr_ram[CDTVCR_RAM_SIZE];
static uae_u8 cdtvcr_clock[2];

static smp_comm_pipe requests;
static uae_sem_t sub_sem;
static volatile int thread_alive;
static int unitnum = -1;
static struct cd_toc_head toc;
static int datatrack;
static int cdtvcr_media;
static int subqcnt;
static int cd_audio_status;

static void cdtvcr_battram_reset (void)
{
	struct zfile *f;
	int v;

	memset (cdtvcr_ram, 0, CDTVCR_RAM_SIZE);
	f = zfile_fopen (currprefs.flashfile, _T("rb+"), ZFD_NORMAL);
	if (!f) {
		f = zfile_fopen (currprefs.flashfile, _T("wb"), 0);
		if (f) {
			zfile_fwrite (cdtvcr_ram, CDTVCR_RAM_SIZE, 1, f);
			zfile_fclose (f);
		}
		return;
	}
	v = zfile_fread (cdtvcr_ram, 1, CDTVCR_RAM_SIZE, f);
	if (v < CDTVCR_RAM_SIZE)
		zfile_fwrite (cdtvcr_ram + v, 1, CDTVCR_RAM_SIZE - v, f);
	zfile_fclose (f);
}

static void cdtvcr_battram_write (int addr, int v)
{
	struct zfile *f;
	int offset = addr & CDTVCR_RAM_MASK;

	if (offset >= CDTVCR_RAM_SIZE)
		return;
	gui_flicker_led (LED_MD, 0, 2);
	if (cdtvcr_ram[offset] == v)
		return;
	cdtvcr_ram[offset] = v;
	f = zfile_fopen (currprefs.flashfile, _T("rb+"), ZFD_NORMAL);
	if (!f)
		return;
	zfile_fseek (f, offset, SEEK_SET);
	zfile_fwrite (cdtvcr_ram + offset, 1, 1, f);
	zfile_fclose (f);
}

static uae_u8 cdtvcr_battram_read (int addr)
{
	uae_u8 v;
	int offset;
	offset = addr & CDTVCR_RAM_MASK;
	if (offset >= CDTVCR_RAM_SIZE)
		return 0;
	gui_flicker_led (LED_MD, 0, 1);
	v = cdtvcr_ram[offset];
	return v;
}

static int ismedia (void)
{
	cdtvcr_4510_ram[CDTVCR_SYS_STATE] &= ~8;
	if (unitnum < 0)
		return 0;
	if (sys_command_ismedia (unitnum, 0)) {
		cdtvcr_4510_ram[CDTVCR_SYS_STATE] |= 8;
		return 1;
	}
	return 0;
}

static int get_qcode (void)
{
#if 0
	if (!sys_command_cd_qcode (unitnum, cdrom_qcode))
		return 0;
	cdrom_qcode[1] = cd_audio_status;
#endif
	return 1;
}

static int get_toc (void)
{
	uae_u32 msf;
	uae_u8 *p;

	cdtvcr_4510_ram[CDTVCR_SYS_STATE] &= ~4;
	datatrack = 0;
	if (!sys_command_cd_toc (unitnum, &toc))
		return 0;
	cdtvcr_4510_ram[CDTVCR_SYS_STATE] |= 4 | 8;
	if (toc.first_track == 1 && (toc.toc[toc.first_track_offset].control & 0x0c) == 0x04)
		datatrack = 1;
	p = &cdtvcr_4510_ram[CDTVCR_TOC];
	p[0] = toc.first_track;
	p[1] = toc.last_track;
	msf = lsn2msf(toc.lastaddress);
	p[2] = msf >> 16;
	p[3] = msf >>  8;
	p[4] = msf >>  0;
	p += 5;
	for (int j = toc.first_track_offset; j <= toc.last_track_offset; j++) {
		struct cd_toc *s = &toc.toc[j];
		p[0] = (s->adr << 0) | (s->control << 4);
		p[1] = s->track;
		msf = lsn2msf(s->address);
		p[2] = msf >> 16;
		p[3] = msf >>  8;
		p[4] = msf >>  0;
		p += 5;
	}
	return 1;
}

static void cdtvcr_4510_reset(uae_u8 v)
{
	cdtvcr_4510_ram[CDTVCR_ID + 0] = 'C';
	cdtvcr_4510_ram[CDTVCR_ID + 1] = 'D';
	cdtvcr_4510_ram[CDTVCR_ID + 2] = 'T';
	cdtvcr_4510_ram[CDTVCR_ID + 3] = 'V';

	if (v == 3) {
		sys_command_cd_pause (unitnum, 0);
		sys_command_cd_stop (unitnum);
		cdtvcr_4510_ram[CDTVCR_CD_PLAYING] = 0;
		cdtvcr_4510_ram[CDTVCR_CD_STATE] = 0;
		return;
	} else if (v == 2 || v == 1) {
		cdtvcr_4510_ram[CDTVCR_INTENA] = 0;
		cdtvcr_4510_ram[CDTVCR_INTREQ] = 0;
		if (v == 1) {
			memset(cdtvcr_4510_ram, 0, 4096);
		}
		cdtvcr_4510_ram[CDTVCR_INTDISABLE] = 1;
		cdtvcr_4510_ram[CDTVCR_CD_STATE] = 2;
	}

	if (ismedia())
		get_toc();
}

void rethink_cdtvcr(void)
{
	if ((cdtvcr_4510_ram[CDTVCR_INTREQ] & cdtvcr_4510_ram[CDTVCR_INTENA]) && !cdtvcr_4510_ram[CDTVCR_INTDISABLE])
		INTREQ_0 (0x8000 | 0x0008);
}

static void cdtvcr_cmd_done(void)
{
	cdtvcr_4510_ram[CDTVCR_SYS_STATE] &= ~1;
	cdtvcr_4510_ram[CDTVCR_CD_CMD_DO] = 0;
	cdtvcr_4510_ram[CDTVCR_INTREQ] |= 0x40;
	cdtvcr_4510_ram[CDTVCR_CD_CMD_STATUS] = 0;
	cdtvcr_4510_ram[CDTVCR_CD_CMD_STATUS2] = 0;
}

static void cdtvcr_play_done(void)
{
	cdtvcr_4510_ram[CDTVCR_SYS_STATE] &= ~1;
	cdtvcr_4510_ram[CDTVCR_CD_CMD_DO] = 0;
	cdtvcr_4510_ram[CDTVCR_INTREQ] |= 0x80;
	cdtvcr_4510_ram[CDTVCR_CD_CMD_STATUS] = 0;
	cdtvcr_4510_ram[CDTVCR_CD_CMD_STATUS2] = 0;
}

static void cdtvcr_cmd_play_started(void)
{
	cdtvcr_cmd_done();
	cdtvcr_4510_ram[CDTVCR_CD_CMD_STATUS + 2] = 2;
	cdtvcr_4510_ram[CDTVCR_CD_CMD_STATUS2 + 2] = 0;
	cdtvcr_4510_ram[CDTVCR_CD_PLAYING] = 1;
	cdtvcr_4510_ram[CDTVCR_CD_STATE] = 8;
}

static void cdtvcr_start (void)
{
	if (unitnum < 0)
		return;
	cdtvcr_4510_ram[CDTVCR_CD_STATE] = 0;
}

static void cdtvcr_stop (void)
{
	if (unitnum < 0)
		return;
	sys_command_cd_pause (unitnum, 0);
	sys_command_cd_stop (unitnum);
	if (cdtvcr_4510_ram[CDTVCR_CD_PLAYING]) {
		cdtvcr_4510_ram[CDTVCR_INTREQ] |= 0x80;
	}
	cdtvcr_4510_ram[CDTVCR_CD_PLAYING] = 0;
	cdtvcr_4510_ram[CDTVCR_CD_STATE] = 2;
}

static void cdtvcr_pause(bool pause)
{
	sys_command_cd_pause (unitnum, pause ? 1 : 0);
	cdtvcr_4510_ram[CDTVCR_CD_STATE] &= ~4;
	if (pause)
		cdtvcr_4510_ram[CDTVCR_CD_STATE] |= 4;
	cdtvcr_cmd_done();
}

static void setsubchannel(uae_u8 *s)
{
	uae_u8 *d;

	// subchannels
	d = &cdtvcr_4510_ram[CDTVCR_SUBC];
	cdtvcr_4510_ram[CDTVCR_SUBBANK] = cdtvcr_4510_ram[CDTVCR_SUBBANK] ? 0 : SUB_CHANNEL_SIZE + 2;
	d[cdtvcr_4510_ram[CDTVCR_SUBBANK] + 0] = 0;
	d[cdtvcr_4510_ram[CDTVCR_SUBBANK] + 1] = 0;
	for (int i = 0; i < SUB_CHANNEL_SIZE; i++) {
		d[cdtvcr_4510_ram[CDTVCR_SUBBANK] + i + 2] = s[i] & 0x3f;
	}

	// q-channel
	d = &cdtvcr_4510_ram[CDTVCR_SUBQ];
	s += SUB_ENTRY_SIZE;
	/* CtlAdr */
	d[0] = s[0];
	/* Track */
	d[1] = s[1];
	/* Index */
	d[2] = s[2];
	/* TrackPos */
	d[3] = s[3];
	d[4] = s[4];
	d[5] = s[5];
	/* DiskPos */
	d[6] = s[6];
	d[7] = s[7];
	d[8] = s[8];
	d[9] = s[9];
	cdtvcr_4510_ram[CDTVCR_SUBQ - 2] = 1; // qcode valid
	cdtvcr_4510_ram[CDTVCR_SUBQ - 1] = 0;

	if (cdtvcr_4510_ram[CDTVCR_CD_SUBCODES])
		cdtvcr_4510_ram[CDTVCR_INTREQ] |= 2 | 4;
}

static void subfunc(uae_u8 *data, int cnt)
{
	uae_u8 out[SUB_CHANNEL_SIZE];
	sub_to_deinterleaved(data, out);
	setsubchannel(out);
}

static int statusfunc(int status)
{
	if (status == -1)
		return 500;
	if (status == -2)
		return 75;
	if (cd_audio_status != status) {
		if (status == AUDIO_STATUS_PLAY_COMPLETE || status == AUDIO_STATUS_PLAY_ERROR) {
			cdtvcr_play_done();
		} else if (status == AUDIO_STATUS_IN_PROGRESS) {
			cdtvcr_cmd_play_started();
		}
	}
	cd_audio_status = status;
	return 0;
}

static void cdtvcr_play(uae_u32 start, uae_u32 end)
{
	sys_command_cd_pause(unitnum, 0);
	if (!sys_command_cd_play(unitnum, start, end, 0, statusfunc, subfunc))
		cdtvcr_play_done();
}

static void cdtvcr_play_track(uae_u32 track_start, uae_u32 track_end)
{
	int start_found, end_found;
	uae_u32 start, end;

	start_found = end_found = 0;
	for (int j = toc.first_track_offset; j <= toc.last_track_offset; j++) {
		struct cd_toc *s = &toc.toc[j];
		if (track_start == s->track) {
			start_found++;
			start = s->paddress;
		}
		if (track_end == s->track) {
			end = s->paddress;
			end_found++;
		}
	}
	if (start_found == 0) {
		write_log (_T("PLAY CD AUDIO: illegal start track %d\n"), track_start);
		cdtvcr_stop();
		cdtvcr_cmd_done();
		return;
	}
	cdtvcr_play(start, end);
}

static void cdtvcr_read_data(uae_u32 start, uae_u32 addr, uae_u32 len)
{
	uae_u8 buffer[2048];
	int didread;

	while (len) {
		didread = sys_command_cd_read (unitnum, buffer, start, 1);
		if (!didread)
			break;
		for (int i = 0; i < 2048 && len > 0; i++) {
			put_byte(addr + i, buffer[i]);
			len--;
		}
		addr += 2048;
		start++;
	}
	cdtvcr_cmd_done();
}

static void cdtvcr_do_cmd(void)
{
	uae_u32 addr, len, start, end, datalen;
	uae_u32 startlsn, endlsn;
	uae_u8 starttrack, endtrack;
	uae_u8 *p = &cdtvcr_4510_ram[CDTVCR_CD_CMD];

	cdtvcr_4510_ram[CDTVCR_SYS_STATE] |= 1;
	cdtvcr_4510_ram[CDTVCR_CD_CMD_STATUS] = 2;
	cdtvcr_4510_ram[CDTVCR_CD_CMD_STATUS2] = 0;
	write_log(_T("CDTVCR CD command %02x\n"), p[0]);
	for(int i = 0; i < 14; i++)
		write_log(_T(".%02x"), p[i]);
	write_log(_T("\n"));

	start = (p[1] << 16) | (p[2] << 8) | (p[3] << 0);
	startlsn = msf2lsn(start);
	end = (p[4] << 16) | (p[5] << 8) | (p[6] << 0);
	endlsn = msf2lsn(end);
	addr = (p[7] << 24) | (p[8] << 16) | (p[9] << 8) | (p[10] << 0);
	len = (p[4] << 16) | (p[5] << 8) | (p[6] << 0);
	datalen = (p[11] << 16) | (p[12] << 8) | (p[13] << 0);
	starttrack = p[1];
	endtrack = p[3];

	switch(p[0])
	{
		case 2: // start
		cdtvcr_start();
		cdtvcr_cmd_done();
		break;
		case 3: // stop
		cdtvcr_stop();
		cdtvcr_cmd_done();
		break;
		case 4: // toc
		cdtvcr_stop();
		get_toc();
		cdtvcr_cmd_done();
		break;
		case 6: // play
		cdtvcr_stop();
		cdtvcr_play(startlsn, endlsn);
		break;
		case 7: // play track
		cdtvcr_stop();
		cdtvcr_play_track(starttrack, endtrack);
		break;
		case 8: // read
		cdtvcr_stop();
		cdtvcr_read_data(start, addr, datalen);
		break;
		case 9:
		cdtvcr_pause(true);
		break;
		case 10:
		cdtvcr_pause(false);
		break;
		case 11: // stop play
		cdtvcr_stop();
		cdtvcr_cmd_done();
		break;
		default:
		write_log(_T("unsupported command!\n"));
		break;
	}
}

static void cdtvcr_4510_do_something(void)
{
	if (cdtvcr_4510_ram[CDTVCR_INTACK]) {
		cdtvcr_4510_ram[CDTVCR_INTACK] = 0;
		rethink_cdtvcr();
	}
	if (cdtvcr_4510_ram[CDTVCR_CD_CMD_DO]) {
		cdtvcr_4510_ram[CDTVCR_SYS_STATE] |= 1;
		write_comm_pipe_u32 (&requests, 0x0100, 1);
	}
}

static bool cdtvcr_debug(uaecptr addr)
{
	addr &= CDTVCR_MASK;
	return !(addr >= CDTVCR_RAM_OFFSET && addr < CDTVCR_RAM_OFFSET + CDTVCR_RAM_SIZE);
}

static void cdtvcr_bput2 (uaecptr addr, uae_u8 v)
{
	addr &= CDTVCR_MASK;
	if (addr == CDTVCR_4510_TRIGGER) {
		if (v & 0x80)
			cdtvcr_4510_do_something();
	} else if (addr >= CDTVCR_RAM_OFFSET && addr < CDTVCR_RAM_OFFSET + CDTVCR_RAM_SIZE) {
		cdtvcr_battram_write(addr - CDTVCR_RAM_OFFSET, v);
	} else if (addr >= CDTVCR_CLOCK && addr < CDTVCR_CLOCK + 0x20) {
		int reg = addr - CDTVCR_CLOCK;
		switch (reg)
		{
			case 0:
			cdtvcr_clock[0] = v;
			case 1:
			cdtvcr_clock[1] = v;
			break;
		}
	} else {
		switch(addr)
		{
			case CDTVCR_RESET:
			cdtvcr_4510_reset(v);
			break;
		}
	}
	if (addr >= 0xc00 && addr < CDTVCR_RAM_OFFSET) {
		switch (addr)
		{
			case CDTVCR_KEYCMD:
			write_log(_T("Got keycode %x\n"), cdtvcr_4510_ram[CDTVCR_KEYCMD+1]);
			v = 0;
			break;
		}
		cdtvcr_4510_ram[addr] = v;
	}
}

static uae_u8 cdtvcr_bget2 (uaecptr addr)
{
	uae_u8 v = 0;
	addr &= CDTVCR_MASK;
	if (addr < CDTVCR_RAM_OFFSET) {
		v = cdtvcr_4510_ram[addr];
	}
	if (addr >= CDTVCR_RAM_OFFSET && addr < CDTVCR_RAM_OFFSET + CDTVCR_RAM_SIZE) {
		v = cdtvcr_battram_read(addr - CDTVCR_RAM_OFFSET);
	} else if (addr >= CDTVCR_CLOCK && addr < CDTVCR_CLOCK + 0x20) {
		int reg = addr - CDTVCR_CLOCK;
		int days, mins, ticks;
		int tickcount = currprefs.ntscmode ? 60 : 50;
		struct timeval tv;
		struct mytimeval mtv;
		gettimeofday (&tv, NULL);
		tv.tv_sec -= _timezone;
		mtv.tv_sec = tv.tv_sec;
		mtv.tv_usec = tv.tv_usec;
		timeval_to_amiga(&mtv, &days, &mins, &ticks, tickcount);
		switch (reg)
		{
			case 0:
			case 1:
			v = cdtvcr_clock[reg];
			break;
			case 2:
			v = days >> 8;
			break;
			case 3:
			v = days;
			break;
			case 4:
			v = mins / 60;
			break;
			case 5:
			v = mins % 60;
			break;
			case 6:
			v = ticks / tickcount;
			break;
			case 7:
			v = ticks % tickcount;
			break;
			case 8:
			v = tickcount;
			break;

		}
	} else {
		switch(addr)
		{
			case CDTVCR_RESET:
			v = 0;
			break;
		}
	}
	return v;
}

static uae_u32 REGPARAM2 cdtvcr_lget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v = (cdtvcr_bget2 (addr) << 24) | (cdtvcr_bget2 (addr + 1) << 16) |
		(cdtvcr_bget2 (addr + 2) << 8) | (cdtvcr_bget2 (addr + 3));
#if CDTVCR_DEBUG
	if (cdtvcr_debug(addr))
		write_log (_T("cdtvcr_lget %08X=%08X PC=%08X\n"), addr, v, M68K_GETPC);
#endif
	return v;
}

static uae_u32 REGPARAM2 cdtvcr_wgeti (uaecptr addr)
{
	uae_u32 v = 0xffff;
#ifdef JIT
	special_mem |= S_READ;
#endif
	return v;
}
static uae_u32 REGPARAM2 cdtvcr_lgeti (uaecptr addr)
{
	uae_u32 v = 0xffff;
#ifdef JIT
	special_mem |= S_READ;
#endif
	return v;
}

static uae_u32 REGPARAM2 cdtvcr_wget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v = (cdtvcr_bget2 (addr) << 8) | cdtvcr_bget2 (addr + 1);
#if CDTVCR_DEBUG
	if (cdtvcr_debug(addr))
		write_log (_T("cdtvcr_wget %08X=%04X PC=%08X\n"), addr, v, M68K_GETPC);
#endif
	return v;
}

static uae_u32 REGPARAM2 cdtvcr_bget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v = cdtvcr_bget2 (addr);
#if CDTVCR_DEBUG
	if (cdtvcr_debug(addr))
		write_log (_T("cdtvcr_bget %08X=%02X PC=%08X\n"), addr, v, M68K_GETPC);
#endif
	return v;
}

static void REGPARAM2 cdtvcr_lput (uaecptr addr, uae_u32 l)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
#if CDTVCR_DEBUG
	if (cdtvcr_debug(addr))
		write_log (_T("cdtvcr_lput %08X=%08X PC=%08X\n"), addr, l, M68K_GETPC);
#endif
	cdtvcr_bput2 (addr, l >> 24);
	cdtvcr_bput2 (addr + 1, l >> 16);
	cdtvcr_bput2 (addr + 2, l >> 8);
	cdtvcr_bput2 (addr + 3, l);
}

static void REGPARAM2 cdtvcr_wput (uaecptr addr, uae_u32 w)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
#if CDTVCR_DEBUG
	if (cdtvcr_debug(addr))
		write_log (_T("cdtvcr_wput %08X=%04X PC=%08X\n"), addr, w & 65535, M68K_GETPC);
#endif
	cdtvcr_bput2 (addr, w >> 8);
	cdtvcr_bput2 (addr + 1, w);
}

static void REGPARAM2 cdtvcr_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
#if CDTVCR_DEBUG
	if (cdtvcr_debug(addr))
		write_log (_T("cdtvcr_bput %08X=%02X PC=%08X\n"), addr, b & 255, M68K_GETPC);
#endif
	cdtvcr_bput2 (addr, b);
}


addrbank cdtvcr_bank = {
	cdtvcr_lget, cdtvcr_wget, cdtvcr_bget,
	cdtvcr_lput, cdtvcr_wput, cdtvcr_bput,
	default_xlate, default_check, NULL, NULL, _T("CDTV-CR"),
	cdtvcr_lgeti, cdtvcr_wgeti, ABFLAG_IO | ABFLAG_SAFE
};

static void *dev_thread (void *p)
{
	write_log (_T("CDTV-CR: CD thread started\n"));
	thread_alive = 1;
	for (;;) {

		uae_u32 b = read_comm_pipe_u32_blocking (&requests);
		if (b == 0xffff) {
			thread_alive = -1;
			return NULL;
		}
		if (unitnum < 0)
			continue;
		switch (b)
		{
			case 0x0100:
				cdtvcr_do_cmd();
			break;
			case 0x0101:
			{
				int m = ismedia ();
				if (m < 0) {
					write_log (_T("CDTV: device %d lost\n"), unitnum);
					cdtvcr_4510_ram[CDTVCR_SYS_STATE] &= ~(4 | 8);
				} else if (m != cdtvcr_media) {
					cdtvcr_media = m;
					get_toc ();
				}
				if (cdtvcr_media)
					get_qcode ();
			}
			break;
		}
	}
}

void CDTVCR_hsync_handler (void)
{
	static int subqcnt;

	if (!currprefs.cs_cdtvcr)
		return;

	subqcnt--;
	if (subqcnt < 0) {
		write_comm_pipe_u32 (&requests, 0x0101, 1);
		subqcnt = 75;
		// want subcodes but not playing?
		if (cdtvcr_4510_ram[CDTVCR_CD_SUBCODES] && !cdtvcr_4510_ram[CDTVCR_CD_PLAYING]) {
			// just return fake stuff, for some reason cdtv-cr driver requires something
			// that looks validg, even when not playing or it gets in infinite loop
			uae_u8 dst[SUB_CHANNEL_SIZE];
			// regenerate Q-subchannel
			uae_u8 *s = dst + 12;
			struct cd_toc *cdtoc = &toc.toc[toc.first_track];
			int sector = 150;
			memset (dst, 0, SUB_CHANNEL_SIZE);
			s[0] = (cdtoc->control << 4) | (cdtoc->adr << 0);
			s[1] = tobcd (cdtoc->track);
			s[2] = tobcd (1);
			int msf = lsn2msf (sector);
			tolongbcd (s + 7, msf);
			msf = lsn2msf (sector - cdtoc->address - 150);
			tolongbcd (s + 3, msf);
			setsubchannel(dst);
		}
	}
}

static void open_unit (void)
{
	struct device_info di;
	unitnum = get_standard_cd_unit (CD_STANDARD_UNIT_CDTV);
	sys_command_info (unitnum, &di, 0);
	write_log (_T("using drive %s (unit %d, media %d)\n"), di.label, unitnum, di.media_inserted);
}

static void close_unit (void)
{
	if (unitnum >= 0)
		sys_command_close (unitnum);
	unitnum = -1;
}

void cdtvcr_reset(void)
{
	if (!currprefs.cs_cdtvcr)
		return;
	close_unit ();
	if (!thread_alive) {
		init_comm_pipe (&requests, 100, 1);
		uae_start_thread (_T("cdtv-cr"), dev_thread, NULL, NULL);
		while (!thread_alive)
			sleep_millis (10);
		uae_sem_init (&sub_sem, 0, 1);
	}
	open_unit ();
	gui_flicker_led (LED_CD, 0, -1);

	cdtvcr_4510_reset(0);
	cdtvcr_battram_reset();
	cdtvcr_clock[0] = 0xe3;
	cdtvcr_clock[1] = 0x1b;
}

void cdtvcr_free(void)
{
	if (thread_alive > 0) {
		write_comm_pipe_u32 (&requests, 0xffff, 1);
		while (thread_alive > 0)
			sleep_millis (10);
		uae_sem_destroy (&sub_sem);
	}
	thread_alive = 0;
	close_unit ();
}
