/*
* UAE - The Un*x Amiga Emulator
*
* Toccata Z2 board emulation
*
* Copyright 2014 Toni Wilen
*
*/

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "uae.h"
#include "memory.h"
#include "newcpu.h"
#include "debug.h"
#include "custom.h"
#include "sndboard.h"
#include "audio.h"


#define DEBUG_TOCCATA 0

#define BOARD_MASK 65535
#define BOARD_SIZE 65536

static const uae_u8 toccata_autoconfig[16] = { 0xc1, 12, 0, 0, 18260 >> 8, 18260 & 255 };
static uae_u8 acmemory[128];
static int configured;
static uae_u8 ad1848_index;
static uae_u8 ad1848_regs[16];
static uae_u8 ad1848_status;
static int autocalibration;
extern addrbank toccata_bank;
static uae_u8 toccata_status;
static int toccata_irq;

#define FIFO_SIZE 1024
#define FIFO_SIZE_HALF (FIFO_SIZE / 2)

static int fifo_read_index;
static int fifo_write_index;
static int data_in_fifo;
static uae_u8 fifo[FIFO_SIZE];

static int fifo_record_read_index;
static int fifo_record_write_index;
static int data_in_record_fifo;
static uae_u8 record_fifo[FIFO_SIZE];

static int fifo_half;
static int toccata_active;
static int left_volume, right_volume;


#define STATUS_ACTIVE 1
#define STATUS_RESET 2
#define STATUS_FIFO_CODEC 4
#define STATUS_FIFO_RECORD 8
#define STATUS_FIFO_PLAY 0x10
#define STATUS_RECORD_INTENA 0x40
#define STATUS_PLAY_INTENA 0x80

#define STATUS_READ_INTREQ 128
#define STATUS_READ_PLAY_HALF 8
#define STATUS_READ_RECORD_HALF 4

static int freq, channels, samplebits;
static int event_time, record_event_time;
static int record_event_counter;
static double base_event_clock;
static int bytespersample;

void update_sndboard_sound (double clk)
{
	base_event_clock = clk;
}

static int ch_sample[2];

static void process_fifo(void)
{
	int prev_data_in_fifo = data_in_fifo;
	if (data_in_fifo >= bytespersample) {
		uae_s16 v;
		if (samplebits == 8) {
			v = fifo[fifo_read_index] << 8;
			v |= fifo[fifo_read_index];
			ch_sample[0] = v;
			if (channels == 2) {
				v = fifo[fifo_read_index + 1] << 8;
				v |= fifo[fifo_read_index + 1];
			}
			ch_sample[1] = v;
		} else if (samplebits == 16) {
			v = fifo[fifo_read_index + 1] << 8;
			v |= fifo[fifo_read_index + 0];
			ch_sample[0] = v;
			if (channels == 2) {
				v = fifo[fifo_read_index + 3] << 8;
				v |= fifo[fifo_read_index + 2];
			}
			ch_sample[1] = v;
		}
		data_in_fifo -= bytespersample;
		fifo_read_index += bytespersample;
		fifo_read_index = fifo_read_index % FIFO_SIZE;
	}

	ch_sample[0] = ch_sample[0] * left_volume / 32768;
	ch_sample[1] = ch_sample[1] * right_volume / 32768;

	if (data_in_fifo < FIFO_SIZE_HALF && prev_data_in_fifo >= FIFO_SIZE_HALF)
		fifo_half |= STATUS_FIFO_PLAY;
}

void audio_state_sndboard(int ch)
{
	if ((toccata_active & STATUS_FIFO_PLAY) && ch == 0) {
		// get all bytes at once to prevent fifo going out of sync
		// if fifo has for example 3 bytes remaining but we need 4.
		process_fifo();
	}
	if (toccata_active && (toccata_status & STATUS_FIFO_CODEC)) {
		int old = toccata_irq;
		if ((fifo_half & STATUS_FIFO_PLAY) && (toccata_status & STATUS_PLAY_INTENA) && (toccata_status & STATUS_FIFO_PLAY)) {
			toccata_irq |= STATUS_READ_PLAY_HALF;
		}
		if ((fifo_half & STATUS_FIFO_RECORD) && (toccata_status & STATUS_FIFO_RECORD) && (toccata_status & STATUS_FIFO_RECORD)) {
			toccata_irq |= STATUS_READ_RECORD_HALF;
		}
		if (old != toccata_irq) {
			sndboard_rethink();
#if DEBUG_TOCCATA > 2
			write_log(_T("TOCCATA IRQ\n"));
#endif
		}
	}
	audio_state_sndboard_state(ch, ch_sample[ch], event_time);
}

static int get_volume(uae_u8 v)
{
	int out;
	if (v & 0x80) // Mute bit
		return 0;
	out = v & 63;
	out = 64 - out;
	out *= 32768 / 64;
	return out;
}

static int get_volume_in(uae_u8 v)
{
	int out;
	if (v & 0x80) // Mute bit
		return 0;
	out = v & 31;
	out = 32 - out;
	out *= 32768 / 32;
	return out;
}

static void calculate_volume(void)
{
	left_volume = get_volume(ad1848_regs[6]);
	right_volume = get_volume(ad1848_regs[7]);
	left_volume = (100 - currprefs.sound_volume_board) * 32768 / 100;
	right_volume = (100 - currprefs.sound_volume_board) * 32768 / 100;

	if (currprefs.sound_toccata_mixer) {
		sound_paula_volume[0] = get_volume_in(ad1848_regs[4]);
		sound_paula_volume[1] = get_volume_in(ad1848_regs[5]);

		sound_cd_volume[0] = get_volume_in(ad1848_regs[2]);
		sound_cd_volume[1] = get_volume_in(ad1848_regs[3]);
	}
}

void sndboard_ext_volume(void)
{
	calculate_volume();
}

static const int freq_crystals[] = {
	// AD1848 documentation says 24.576MHz but photo of board shows 24.582MHz
	24582000,
	16934400
};
static const int freq_dividers[] = {
	3072,
	1536,
	896,
	768,
	448,
	384,
	512,
	2560
};

static void codec_setup(void)
{
	uae_u8 c = ad1848_regs[8];

	channels = (c & 0x10) ? 2 : 1;
	samplebits = (c & 0x40) ? 16 : 8;
	freq = freq_crystals[c & 1] / freq_dividers[(c >> 1) & 7];
	bytespersample = (samplebits / 8) * channels;
	write_log(_T("TOCCATA start %s freq=%d bits=%d channels=%d\n"),
		((toccata_active & (STATUS_FIFO_PLAY | STATUS_FIFO_RECORD)) == (STATUS_FIFO_PLAY | STATUS_FIFO_RECORD)) ? _T("Play+Record") :
		(toccata_active & STATUS_FIFO_PLAY) ? _T("Play") : _T("Record"),
		freq, samplebits, channels);
}

static void codec_start(void)
{
	toccata_active  = (ad1848_regs[9] & 1) ? STATUS_FIFO_PLAY : 0;
	toccata_active |= (ad1848_regs[9] & 2) ? STATUS_FIFO_RECORD : 0;

	codec_setup();

	event_time = base_event_clock * CYCLE_UNIT / freq;
	record_event_time = base_event_clock * FIFO_SIZE_HALF / (freq * bytespersample);
	record_event_counter = 0;

	if (toccata_active & STATUS_FIFO_PLAY)
		audio_enable_sndboard(true);
}

static void codec_stop(void)
{
	write_log(_T("TOCCATA stop\n"));
	toccata_active = 0;
	audio_enable_sndboard(false);
}

void sndboard_rethink(void)
{
	if (toccata_irq)
		INTREQ_0(0x8000 | 0x2000);
}

void sndboard_hsync(void)
{
	if (autocalibration > 0)
		autocalibration--;
	if (toccata_active & STATUS_FIFO_RECORD) {
		record_event_counter -= maxhpos;
		if (record_event_counter <= 0) {
			record_event_counter += record_event_time;
			int size = data_in_record_fifo < FIFO_SIZE_HALF ? 512 : FIFO_SIZE - data_in_record_fifo;
			data_in_record_fifo += size;
			fifo_record_write_index += size;
			fifo_record_write_index %= FIFO_SIZE;
			fifo_half |= STATUS_FIFO_RECORD;
			audio_state_sndboard(-1);
		}
	}
}

void sndboard_vsync(void)
{
	if (toccata_active)
		calculate_volume();
}

static void toccata_put(uaecptr addr, uae_u8 v)
{
	int idx = ad1848_index & 15;

#if DEBUG_TOCCATA > 2
	if (addr & 0x4000)
		write_log(_T("TOCCATA PUT %08x %02x %d PC=%08X\n"), addr, v, idx, M68K_GETPC);
#endif

	if ((addr & 0x6801) == 0x6001) {
		// AD1848 register 0
		ad1848_index = v;
	} else if ((addr & 0x6801) == 0x6801) {
		// AD1848 register 1
		uae_u8 old = ad1848_regs[idx];
		ad1848_regs[idx] = v;
#if DEBUG_TOCCATA > 0
		write_log(_T("TOCCATA PUT reg %d = %02x PC=%08x\n"), idx, v, M68K_GETPC);
#endif
		switch(idx)
		{
			case 9:
			if (v & 8) // ACI enabled
				autocalibration = 50;
			if (!(old & 3) && (v & 3))
				codec_start();
			else if ((old & 3) && !(v & 3))
				codec_stop();
			break;

			case 2:
			case 3:
			case 4:
			case 5:
			case 6:
			case 7:
			case 8:
				calculate_volume();
			break;

		}
	} else if ((addr & 0x6800) == 0x2000) {
		// FIFO input
		if (toccata_status & STATUS_FIFO_PLAY) {
			// 7202LA datasheet says fifo can't overflow
			if (((fifo_write_index + 1) % FIFO_SIZE) != fifo_read_index) {
				fifo[fifo_write_index] = v;
				fifo_write_index++;
				fifo_write_index %= FIFO_SIZE;
				data_in_fifo++;
			}
		}
		toccata_irq &= ~STATUS_READ_PLAY_HALF;
		fifo_half &= ~STATUS_FIFO_PLAY;
	} else if ((addr & 0x6800) == 0x0000) {
		// Board status
		if (v & STATUS_RESET) {
			codec_stop();
			toccata_status = 0;
			toccata_irq = 0;
			v = 0;
		}
		if (v == STATUS_ACTIVE) {
			fifo_write_index = 0;
			fifo_read_index = 0;
			data_in_fifo = 0;
			toccata_status = 0;
			toccata_irq = 0;
			fifo_half = 0;
		}
		toccata_status = v;
#if DEBUG_TOCCATA > 0
		write_log(_T("TOCCATA PUT STATUS %08x %02x %d PC=%08X\n"), addr, v, idx, M68K_GETPC);
#endif
	} else {
		write_log(_T("TOCCATA PUT UNKNOWN %08x\n"), addr);
	}
}

static uae_u8 toccata_get(uaecptr addr)
{
	int idx = ad1848_index & 15;
	uae_u8 v = 0;

	if ((addr & 0x6801) == 0x6001) {
		// AD1848 register 0
		v = ad1848_index;
	} else if ((addr & 0x6801) == 0x6801) {
		// AD1848 register 1
		v = ad1848_regs[idx];
#if DEBUG_TOCCATA > 0
		write_log(_T("TOCCATA GET reg %d = %02x PC=%08x\n"), idx, v, M68K_GETPC);
#endif
		switch (idx)
		{
			case 11:
			if (autocalibration > 10 && autocalibration < 30)
				ad1848_regs[11] |= 0x20;
			else
				ad1848_regs[11] &= ~0x20;
			break;
			case 12:
				// revision
				v = 0x0a;
			break;
		}
	} else if ((addr & 0x6800) == 0x2000) {
		// FIFO output
		v = fifo[fifo_record_read_index];
		if (toccata_status & STATUS_FIFO_RECORD) {
			if (data_in_record_fifo > 0) {
				fifo_record_read_index++;
				fifo_record_read_index %= FIFO_SIZE;
				data_in_record_fifo--;
			}
		}
		toccata_irq &= ~STATUS_READ_RECORD_HALF;
		fifo_half &= ~STATUS_FIFO_RECORD;
	} else if ((addr & 0x6800) == 0x0000) {
		// Board status
		v = STATUS_READ_INTREQ; // active low
		if (toccata_irq) {
			v &= ~STATUS_READ_INTREQ;
			v |= toccata_irq;
			toccata_irq = 0;
		}
#if DEBUG_TOCCATA > 0
		write_log(_T("TOCCATA GET STATUS %08x %02x %d PC=%08X\n"), addr, v, idx, M68K_GETPC);
#endif
	} else {
		write_log(_T("TOCCATA GET UNKNOWN %08x\n"), addr);
	}

#if DEBUG_TOCCATA > 2
	write_log(_T("TOCCATA GET %08x %02x %d PC=%08X\n"), addr, v, idx, M68K_GETPC);
#endif
	return v;
}

static void REGPARAM2 toccata_bput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	b &= 0xff;
	addr &= BOARD_MASK;
	if (!configured) {
		switch (addr)
		{
			case 0x48:
			map_banks(&toccata_bank, expamem_z2_pointer >> 16, BOARD_SIZE >> 16, 0);
			configured = 1;
			expamem_next(&toccata_bank, NULL);
			break;
			case 0x4c:
			configured = -1;
			expamem_shutup(&toccata_bank);
			break;
		}
		return;
	}
	if (configured > 0)
		toccata_put(addr, b);
}

static void REGPARAM2 toccata_wput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	toccata_bput(addr, b >> 8);
	toccata_bput(addr, b >> 0);
}

static void REGPARAM2 toccata_lput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	toccata_bput(addr + 0, b >> 24);
	toccata_bput(addr + 1, b >> 16);
	toccata_bput(addr + 2, b >>  8);
	toccata_bput(addr + 3, b >>  0);
}

static uae_u32 REGPARAM2 toccata_bget(uaecptr addr)
{
	uae_u8 v = 0;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= BOARD_MASK;
	if (!configured) {
		if (addr >= sizeof acmemory)
			return 0;
		return acmemory[addr];
	}
	if (configured > 0)
		v = toccata_get(addr);
	return v;
}
static uae_u32 REGPARAM2 toccata_wget(uaecptr addr)
{
	uae_u16 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v = toccata_get(addr) << 8;
	v |= toccata_get(addr + 1) << 0;
	return v;
}
static uae_u32 REGPARAM2 toccata_lget(uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	v = toccata_get(addr) << 24;
	v |= toccata_get(addr + 1) << 16;
	v |= toccata_get(addr + 2) << 8;
	v |= toccata_get(addr + 3) << 0;
	return v;
}

addrbank toccata_bank = {
	toccata_lget, toccata_wget, toccata_bget,
	toccata_lput, toccata_wput, toccata_bput,
	default_xlate, default_check, NULL, NULL, _T("Toccata"),
	dummy_lgeti, dummy_wgeti, ABFLAG_IO
};

static void ew (int addr, uae_u32 value)
{
	addr &= 0xffff;
	if (addr == 00 || addr == 02 || addr == 0x40 || addr == 0x42) {
		acmemory[addr] = (value & 0xf0);
		acmemory[addr + 2] = (value & 0x0f) << 4;
	} else {
		acmemory[addr] = ~(value & 0xf0);
		acmemory[addr + 2] = ~((value & 0x0f) << 4);
	}
}

addrbank *sndboard_init(void)
{
	memset(ad1848_regs, 0, sizeof ad1848_regs);
	ad1848_regs[2] = 0x80;
	ad1848_regs[3] = 0x80;
	ad1848_regs[4] = 0x80;
	ad1848_regs[5] = 0x80;
	ad1848_regs[6] = 0x80;
	ad1848_regs[7] = 0x80;
	ad1848_regs[9] = 0x10;
	ad1848_status = 0xcc;
	ad1848_index = 0x40;
	calculate_volume();

	configured = 0;
	memset(acmemory, 0xff, sizeof acmemory);
	for (int i = 0; i < 16; i++) {
		uae_u8 b = toccata_autoconfig[i];
		ew(i * 4, b);
	}
	return &toccata_bank;
}

void sndboard_free(void)
{
}

void sndboard_reset(void)
{
	ch_sample[0] = 0;
	ch_sample[1] = 0;
	audio_enable_sndboard(false);
}
