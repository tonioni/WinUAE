/*
* UAE - The Un*x Amiga Emulator
*
* Toccata Z2 board emulation
*
* Copyright 2014-2015 Toni Wilen
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
#include "autoconf.h"
#include "pci_hw.h"
#include "qemuvga/qemuaudio.h"

static uae_u8 *sndboard_get_buffer(int *frames);
static void sndboard_release_buffer(uae_u8 *buffer, int frames);
static void sndboard_free_capture(void);
static bool sndboard_init_capture(int freq);

static double base_event_clock;

#define DEBUG_TOCCATA 0

#define BOARD_MASK 65535
#define BOARD_SIZE 65536

#define FIFO_SIZE 1024
#define FIFO_SIZE_HALF (FIFO_SIZE / 2)

static const uae_u8 toccata_autoconfig[16] = { 0xc1, 12, 0, 0, 18260 >> 8, 18260 & 255 };

struct toccata_data {
	uae_u8 acmemory[128];
	int configured;
	uae_u8 ad1848_index;
	uae_u8 ad1848_regs[16];
	uae_u8 ad1848_status;
	int autocalibration;
	uae_u8 toccata_status;
	int toccata_irq;
	int fifo_read_index;
	int fifo_write_index;
	int data_in_fifo;
	uae_u8 fifo[FIFO_SIZE];

	int fifo_record_read_index;
	int fifo_record_write_index;
	int data_in_record_fifo;
	uae_u8 record_fifo[FIFO_SIZE];

	int ch_sample[2];

	int fifo_half;
	int toccata_active;
	int left_volume, right_volume;

	int freq, freq_adjusted, channels, samplebits;
	int event_time, record_event_time;
	int record_event_counter;
	int bytespersample;
};

static struct toccata_data toccata;

extern addrbank toccata_bank;

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


void update_sndboard_sound (double clk)
{
	base_event_clock = clk;
}

static void process_fifo(void)
{
	struct toccata_data *data = &toccata;
	int prev_data_in_fifo = data->data_in_fifo;
	if (data->data_in_fifo >= data->bytespersample) {
		uae_s16 v;
		if (data->samplebits == 8) {
			v = data->fifo[data->fifo_read_index] << 8;
			v |= data->fifo[data->fifo_read_index];
			data->ch_sample[0] = v;
			if (data->channels == 2) {
				v = data->fifo[data->fifo_read_index + 1] << 8;
				v |= data->fifo[data->fifo_read_index + 1];
			}
			data->ch_sample[1] = v;
		} else if (data->samplebits == 16) {
			v = data->fifo[data->fifo_read_index + 1] << 8;
			v |= data->fifo[data->fifo_read_index + 0];
			data->ch_sample[0] = v;
			if (data->channels == 2) {
				v = data->fifo[data->fifo_read_index + 3] << 8;
				v |= data->fifo[data->fifo_read_index + 2];
			}
			data->ch_sample[1] = v;
		}
		data->data_in_fifo -= data->bytespersample;
		data->fifo_read_index += data->bytespersample;
		data->fifo_read_index = data->fifo_read_index % FIFO_SIZE;
	}

	data->ch_sample[0] = data->ch_sample[0] * data->left_volume / 32768;
	data->ch_sample[1] = data->ch_sample[1] * data->right_volume / 32768;

	if (data->data_in_fifo < FIFO_SIZE_HALF && prev_data_in_fifo >= FIFO_SIZE_HALF)
		data->fifo_half |= STATUS_FIFO_PLAY;
}

static void audio_state_sndboard_toccata(int ch)
{
	struct toccata_data *data = &toccata;
	if ((data->toccata_active & STATUS_FIFO_PLAY) && ch == 0) {
		// get all bytes at once to prevent fifo going out of sync
		// if fifo has for example 3 bytes remaining but we need 4.
		process_fifo();
	}
	if (data->toccata_active && (data->toccata_status & STATUS_FIFO_CODEC)) {
		int old = data->toccata_irq;
		if ((data->fifo_half & STATUS_FIFO_PLAY) && (data->toccata_status & STATUS_PLAY_INTENA) && (data->toccata_status & STATUS_FIFO_PLAY)) {
			data->toccata_irq |= STATUS_READ_PLAY_HALF;
		}
		if ((data->fifo_half & STATUS_FIFO_RECORD) && (data->toccata_status & STATUS_FIFO_RECORD) && (data->toccata_status & STATUS_FIFO_RECORD)) {
			data->toccata_irq |= STATUS_READ_RECORD_HALF;
		}
		if (old != data->toccata_irq) {
			sndboard_rethink();
#if DEBUG_TOCCATA > 2
			write_log(_T("TOCCATA IRQ\n"));
#endif
		}
	}
	audio_state_sndboard_state(ch, data->ch_sample[ch], data->event_time);
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

static void calculate_volume_toccata(void)
{
	struct toccata_data *data = &toccata;
	data->left_volume = (100 - currprefs.sound_volume_board) * 32768 / 100;
	data->right_volume = (100 - currprefs.sound_volume_board) * 32768 / 100;

	data->left_volume = get_volume(data->ad1848_regs[6]) * data->left_volume / 32768;
	data->right_volume = get_volume(data->ad1848_regs[7]) * data->right_volume / 32768;

	if (currprefs.sound_toccata_mixer) {
		sound_paula_volume[0] = get_volume_in(data->ad1848_regs[4]);
		sound_paula_volume[1] = get_volume_in(data->ad1848_regs[5]);

		sound_cd_volume[0] = get_volume_in(data->ad1848_regs[2]);
		sound_cd_volume[1] = get_volume_in(data->ad1848_regs[3]);
	}
}

static const int freq_crystals[] = {
	// AD1848 documentation says 24.576MHz but photo of board shows 24.582MHz
	// Added later: It seems there are boards that have correct crystal and
	// also boards with wrong crystal..
	// So we can use correct one in emulation.
	24576000,
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
	struct toccata_data *data = &toccata;
	uae_u8 c = data->ad1848_regs[8];

	data->channels = (c & 0x10) ? 2 : 1;
	data->samplebits = (c & 0x40) ? 16 : 8;
	data->freq = freq_crystals[c & 1] / freq_dividers[(c >> 1) & 7];
	data->freq_adjusted = ((data->freq + 49) / 100) * 100;
	data->bytespersample = (data->samplebits / 8) * data->channels;
	write_log(_T("TOCCATA start %s freq=%d bits=%d channels=%d\n"),
		((data->toccata_active & (STATUS_FIFO_PLAY | STATUS_FIFO_RECORD)) == (STATUS_FIFO_PLAY | STATUS_FIFO_RECORD)) ? _T("Play+Record") :
		(data->toccata_active & STATUS_FIFO_PLAY) ? _T("Play") : _T("Record"),
		data->freq, data->samplebits, data->channels);
}

static int capture_buffer_size = 48000 * 2 * 2; // 1s at 48000/stereo/16bit
static int capture_read_index, capture_write_index;
static uae_u8 *capture_buffer;

static void codec_start(void)
{
	struct toccata_data *data = &toccata;
	data->toccata_active  = (data->ad1848_regs[9] & 1) ? STATUS_FIFO_PLAY : 0;
	data->toccata_active |= (data->ad1848_regs[9] & 2) ? STATUS_FIFO_RECORD : 0;

	codec_setup();

	data->event_time = base_event_clock * CYCLE_UNIT / data->freq;
	data->record_event_time = base_event_clock * CYCLE_UNIT / (data->freq_adjusted * data->bytespersample);
	data->record_event_counter = 0;

	if (data->toccata_active & STATUS_FIFO_PLAY) {
		audio_enable_sndboard(true);
	}
	if (data->toccata_active & STATUS_FIFO_RECORD) {
		capture_buffer = xcalloc(uae_u8, capture_buffer_size);
		sndboard_init_capture(data->freq_adjusted);
	}
}

static void codec_stop(void)
{
	struct toccata_data *data = &toccata;
	write_log(_T("TOCCATA stop\n"));
	data->toccata_active = 0;
	sndboard_free_capture();
	audio_enable_sndboard(false);
	xfree(capture_buffer);
	capture_buffer = NULL;
}

void sndboard_rethink(void)
{
	struct toccata_data *data = &toccata;
	uae_int_requested &= ~0x200;
	if (data->toccata_irq)
		uae_int_requested |= 0x200;
}

static void sndboard_process_capture(void)
{
	struct toccata_data *data = &toccata;
	int frames;
	uae_u8 *buffer = sndboard_get_buffer(&frames);
	if (buffer && frames) {
		uae_u8 *p = buffer;
		int bytes = frames * 4;
		if (bytes >= capture_buffer_size - capture_write_index) {
			memcpy(capture_buffer + capture_write_index, p, capture_buffer_size - capture_write_index);
			p += capture_buffer_size - capture_write_index;
			bytes -=  capture_buffer_size - capture_write_index;
			capture_write_index = 0;
		}
		if (bytes > 0 && bytes < capture_buffer_size - capture_write_index) {
			memcpy(capture_buffer + capture_write_index, p, bytes);
			capture_write_index += bytes;
		}
	}
	sndboard_release_buffer(buffer, frames);
}

void sndboard_hsync(void)
{
	struct toccata_data *data = &toccata;
	static int capcnt;

	if (data->autocalibration > 0)
		data->autocalibration--;

	if (data->toccata_active & STATUS_FIFO_RECORD) {

		capcnt--;
		if (capcnt <= 0) {
			sndboard_process_capture();
			capcnt = data->record_event_time * 312 / (maxhpos * CYCLE_UNIT);
		}

		data->record_event_counter += maxhpos * CYCLE_UNIT;
		int bytes = data->record_event_counter / data->record_event_time;
		bytes &= ~3;
		if (bytes < 64 || capture_read_index == capture_write_index)
			return;

		int oldfifo = data->data_in_record_fifo;
		int oldbytes = bytes;
		int size = FIFO_SIZE - data->data_in_record_fifo;
		while (size > 0 && capture_read_index != capture_write_index && bytes > 0) {
			uae_u8 *fifop = &data->fifo[data->fifo_record_write_index];
			uae_u8 *bufp = &capture_buffer[capture_read_index];

			if (data->samplebits == 8) {
				fifop[0] = bufp[1];
				data->fifo_record_write_index++;
				data->data_in_record_fifo++;
				size--;
				bytes--;
				if (data->channels == 2) {
					fifop[1] = bufp[3];
					data->fifo_record_write_index++;
					data->data_in_record_fifo++;
					size--;
					bytes--;
				}
			} else if (data->samplebits == 16) {
				fifop[0] = bufp[1];
				fifop[1] = bufp[0];
				data->fifo_record_write_index += 2;
				data->data_in_record_fifo += 2;
				size -= 2;
				bytes -= 2;
				if (data->channels == 2) {
					fifop[2] = bufp[3];
					fifop[3] = bufp[2];
					data->fifo_record_write_index += 2;
					data->data_in_record_fifo += 2;
					size -= 2;
					bytes -= 2;
				}
			}

			data->fifo_record_write_index %= FIFO_SIZE;
			capture_read_index += 4;
			if (capture_read_index >= capture_buffer_size)
				capture_read_index = 0;
		}
		
		write_log(_T("%d %d %d %d\n"), capture_read_index, capture_write_index, size, bytes);

		if (data->data_in_record_fifo > FIFO_SIZE_HALF && oldfifo <= FIFO_SIZE_HALF) {
			data->fifo_half |= STATUS_FIFO_RECORD;
			audio_state_sndboard(-1);
		}
		data->record_event_counter -= oldbytes * data->record_event_time;
	}
}

static void sndboard_vsync_toccata(void)
{
	struct toccata_data *data = &toccata;
	if (data->toccata_active) {
		calculate_volume_toccata();
		audio_activate();
	}
}

static void toccata_put(uaecptr addr, uae_u8 v)
{
	struct toccata_data *data = &toccata;
	int idx = data->ad1848_index & 15;

#if DEBUG_TOCCATA > 2
	if (addr & 0x4000)
		write_log(_T("TOCCATA PUT %08x %02x %d PC=%08X\n"), addr, v, idx, M68K_GETPC);
#endif

	if ((addr & 0x6801) == 0x6001) {
		// AD1848 register 0
		data->ad1848_index = v;
	} else if ((addr & 0x6801) == 0x6801) {
		// AD1848 register 1
		uae_u8 old = data->ad1848_regs[idx];
		data->ad1848_regs[idx] = v;
#if DEBUG_TOCCATA > 0
		write_log(_T("TOCCATA PUT reg %d = %02x PC=%08x\n"), idx, v, M68K_GETPC);
#endif
		switch(idx)
		{
			case 9:
			if (v & 8) // ACI enabled
				data->autocalibration = 50;
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
				calculate_volume_toccata();
			break;

		}
	} else if ((addr & 0x6800) == 0x2000) {
		// FIFO input
		if (data->toccata_status & STATUS_FIFO_PLAY) {
			// 7202LA datasheet says fifo can't overflow
			if (((data->fifo_write_index + 1) % FIFO_SIZE) != data->fifo_read_index) {
				data->fifo[data->fifo_write_index] = v;
				data->fifo_write_index++;
				data->fifo_write_index %= FIFO_SIZE;
				data->data_in_fifo++;
			}
		}
		data->toccata_irq &= ~STATUS_READ_PLAY_HALF;
		data->fifo_half &= ~STATUS_FIFO_PLAY;
	} else if ((addr & 0x6800) == 0x0000) {
		// Board status
		if (v & STATUS_RESET) {
			codec_stop();
			data->toccata_status = 0;
			data->toccata_irq = 0;
			v = 0;
		}
		if (v == STATUS_ACTIVE) {
			data->fifo_write_index = 0;
			data->fifo_read_index = 0;
			data->data_in_fifo = 0;
			data->toccata_status = 0;
			data->toccata_irq = 0;
			data->fifo_half = 0;
		}
		data->toccata_status = v;
#if DEBUG_TOCCATA > 0
		write_log(_T("TOCCATA PUT STATUS %08x %02x %d PC=%08X\n"), addr, v, idx, M68K_GETPC);
#endif
	} else {
		write_log(_T("TOCCATA PUT UNKNOWN %08x\n"), addr);
	}
}

static uae_u8 toccata_get(uaecptr addr)
{
	struct toccata_data *data = &toccata;
	int idx = data->ad1848_index & 15;
	uae_u8 v = 0;

	if ((addr & 0x6801) == 0x6001) {
		// AD1848 register 0
		v = data->ad1848_index;
	} else if ((addr & 0x6801) == 0x6801) {
		// AD1848 register 1
		v = data->ad1848_regs[idx];
#if DEBUG_TOCCATA > 0
		write_log(_T("TOCCATA GET reg %d = %02x PC=%08x\n"), idx, v, M68K_GETPC);
#endif
		switch (idx)
		{
			case 11:
			if (data->autocalibration > 10 && data->autocalibration < 30)
				data->ad1848_regs[11] |= 0x20;
			else
				data->ad1848_regs[11] &= ~0x20;
			break;
			case 12:
				// revision
				v = 0x0a;
			break;
		}
	} else if ((addr & 0x6800) == 0x2000) {
		// FIFO output
		v = data->fifo[data->fifo_record_read_index];
		if (data->toccata_status & STATUS_FIFO_RECORD) {
			if (data->data_in_record_fifo > 0) {
				data->fifo_record_read_index++;
				data->fifo_record_read_index %= FIFO_SIZE;
				data->data_in_record_fifo--;
			}
		}
		data->toccata_irq &= ~STATUS_READ_RECORD_HALF;
		data->fifo_half &= ~STATUS_FIFO_RECORD;
	} else if ((addr & 0x6800) == 0x0000) {
		// Board status
		v = STATUS_READ_INTREQ; // active low
		if (data->toccata_irq) {
			v &= ~STATUS_READ_INTREQ;
			v |= data->toccata_irq;
			data->toccata_irq = 0;
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
	struct toccata_data *data = &toccata;
	b &= 0xff;
	addr &= BOARD_MASK;
	if (!data->configured) {
		switch (addr)
		{
			case 0x48:
			map_banks_z2(&toccata_bank, expamem_z2_pointer >> 16, BOARD_SIZE >> 16);
			data->configured = 1;
			expamem_next(&toccata_bank, NULL);
			break;
			case 0x4c:
			data->configured = -1;
			expamem_shutup(&toccata_bank);
			break;
		}
		return;
	}
	if (data->configured > 0)
		toccata_put(addr, b);
}

static void REGPARAM2 toccata_wput(uaecptr addr, uae_u32 b)
{
	toccata_bput(addr + 0, b >> 8);
	toccata_bput(addr + 1, b >> 0);
}

static void REGPARAM2 toccata_lput(uaecptr addr, uae_u32 b)
{
	toccata_bput(addr + 0, b >> 24);
	toccata_bput(addr + 1, b >> 16);
	toccata_bput(addr + 2, b >>  8);
	toccata_bput(addr + 3, b >>  0);
}

static uae_u32 REGPARAM2 toccata_bget(uaecptr addr)
{
	struct toccata_data *data = &toccata;
	uae_u8 v = 0;
	addr &= BOARD_MASK;
	if (!data->configured) {
		if (addr >= sizeof data->acmemory)
			return 0;
		return data->acmemory[addr];
	}
	if (data->configured > 0)
		v = toccata_get(addr);
	return v;
}
static uae_u32 REGPARAM2 toccata_wget(uaecptr addr)
{
	uae_u16 v;
	v = toccata_get(addr) << 8;
	v |= toccata_get(addr + 1) << 0;
	return v;
}
static uae_u32 REGPARAM2 toccata_lget(uaecptr addr)
{
	uae_u32 v;
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
	dummy_lgeti, dummy_wgeti,
	ABFLAG_IO, S_READ, S_WRITE
};

static void ew (uae_u8 *acmemory, int addr, uae_u32 value)
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

addrbank *sndboard_init(int devnum)
{
	struct toccata_data *data = &toccata;
	memset(data->ad1848_regs, 0, sizeof data->ad1848_regs);
	data->ad1848_regs[2] = 0x80;
	data->ad1848_regs[3] = 0x80;
	data->ad1848_regs[4] = 0x80;
	data->ad1848_regs[5] = 0x80;
	data->ad1848_regs[6] = 0x80;
	data->ad1848_regs[7] = 0x80;
	data->ad1848_regs[9] = 0x10;
	data->ad1848_status = 0xcc;
	data->ad1848_index = 0x40;
	calculate_volume_toccata();

	data->configured = 0;
	memset(data->acmemory, 0xff, sizeof data->acmemory);
	for (int i = 0; i < 16; i++) {
		uae_u8 b = toccata_autoconfig[i];
		ew(data->acmemory, i * 4, b);
	}
	return &toccata_bank;
}

void sndboard_free(void)
{
	struct toccata_data *data = &toccata;
	data->toccata_irq = 0;
	uae_int_requested &= ~0x200;
}

void sndboard_reset(void)
{
	struct toccata_data *data = &toccata;
	data->ch_sample[0] = 0;
	data->ch_sample[1] = 0;
	audio_enable_sndboard(false);
}

struct fm801_data
{
	struct pci_board_state *pcibs;
	uaecptr play_dma[2], play_dma2[2];
	uae_u16 play_len, play_len2;
	uae_u16 play_control;
	uae_u16 interrupt_control;
	uae_u16 interrupt_status;
	int dmach;
	int freq;
	int bits;
	int ch;
	int bytesperframe;
	bool play_on;
	int left_volume, right_volume;
	int ch_sample[2];
	int event_time;
};
static struct fm801_data fm801;
static bool fm801_active;
static const int fm801_freq[16] = { 5500, 8000, 9600, 11025, 16000, 19200, 22050, 32000, 38400, 44100, 48000 };

static void calculate_volume_fm801(void)
{
	struct fm801_data *data = &fm801;
	data->left_volume = (100 - currprefs.sound_volume_board) * 32768 / 100;
	data->right_volume = (100 - currprefs.sound_volume_board) * 32768 / 100;
}

static void sndboard_vsync_fm801(void)
{
	audio_activate();
	calculate_volume_fm801();
}

static void fm801_stop(struct fm801_data *data)
{
	write_log(_T("FM801 STOP\n"));
	data->play_on = false;
	audio_enable_sndboard(false);
}

static void fm801_swap_buffers(struct fm801_data *data)
{
	data->dmach = data->dmach ? 0 : 1;
	data->play_dma2[data->dmach] = data->play_dma[data->dmach];
	data->play_len2 = data->play_len;
	// stop at the end of buffer
	if (!(data->play_control & 0x20) && !(data->play_control & 0x80))
		fm801_stop(data);
}

static void fm801_interrupt(struct fm801_data *data)
{
	if ((data->interrupt_status & 0x100) && !(data->interrupt_control & 1)) {
		data->pcibs->board->irq(data->pcibs, true);
	} else {
		data->pcibs->board->irq(data->pcibs, false);
	}
}

static void audio_state_sndboard_fm801(int ch)
{
	struct fm801_data *data = &fm801;

	if (data->play_on && ch == 0) {
		uae_u8 sample[2 * 6] = { 0 };
		uae_s16 l, r;
		pci_read_dma(data->pcibs, data->play_dma2[data->dmach], sample, data->bytesperframe);
		if (data->bits == 8) {
			if (data->ch == 1) {
				sample[1] = sample[0];
				sample[2] = sample[0];
				sample[3] = sample[0];
			} else {
				sample[2] = sample[1];
				sample[3] = sample[1];
				sample[1] = sample[0];
			}
		} else {
			if (data->ch == 1) {
				sample[2] = sample[0];
				sample[3] = sample[1];
			}
		}
		l = (sample[1] << 8) | sample[0];
		r = (sample[3] << 8) | sample[2];
		data->ch_sample[0] = l;
		data->ch_sample[1] = r;
		data->ch_sample[0] = data->ch_sample[0] * data->left_volume / 32768;
		data->ch_sample[1] = data->ch_sample[1] * data->right_volume / 32768;

		data->play_len2 -= data->bytesperframe;
		data->play_dma2[data->dmach] += data->bytesperframe;
		if (data->play_len2 == 0xffff) {
			fm801_swap_buffers(data);
			data->interrupt_status |= 0x100;
			fm801_interrupt(data);
		}
	}
	audio_state_sndboard_state(ch, data->ch_sample[ch], data->event_time);
}

static void fm801_hsync_handler(struct pci_board_state *pcibs)
{
}

static void fm801_play(struct fm801_data *data)
{
	uae_u16 control = data->play_control;
	int f = (control >> 8) & 15;
	data->freq = fm801_freq[f];
	if (!data->freq)
		data->freq = 44100;
	data->event_time = base_event_clock * CYCLE_UNIT / data->freq;
	data->bits = (control & 0x4000) ? 16 : 8;
	f = (control >> 12) & 3;
	switch (f)
	{
		case 0:
		data->ch = (control & 0x8000) ? 2 : 1;
		break;
		case 1:
		data->ch = 4;
		break;
		case 2:
		data->ch = 6;
		break;
		case 3:
		data->ch = 6;
		break;
	}
	data->bytesperframe = data->bits * data->ch / 8;
	data->play_on = true;

	data->dmach = 1;
	fm801_swap_buffers(data);

	calculate_volume_fm801();

	write_log(_T("FM801 PLAY: freq=%d ch=%d bits=%d\n"), data->freq, data->ch, data->bits);

	audio_enable_sndboard(true);
}

static void fm801_pause(struct fm801_data *data, bool pause)
{
	write_log(_T("FM801 PAUSED %d\n"), pause);
}
static void fm801_control(struct fm801_data *data, uae_u16 control)
{
	uae_u16 old_control = data->play_control;
	data->play_control = control;
	data->play_control &= ~(8 | 16);
	if ((data->play_control & 0x20) && !(old_control & 0x20)) {
		fm801_play(data);
	} else if (!(data->play_control & 0x20) && (old_control & 0x20)) {
		if (data->play_control & 0x80)
			fm801_stop(data);
	} else if (data->play_control & 0x20) {
		if ((data->play_control & 0x40) && !(old_control & 0x40)) {
			fm801_pause(data, true);
		} else if (!(data->play_control & 0x40) && (old_control & 0x40)) {
			fm801_pause(data, true);
		}
	}

}

static void REGPARAM2 fm801_bput(struct pci_board_state *pcibs, uaecptr addr, uae_u32 b)
{
	struct fm801_data *data = &fm801;
}
static void REGPARAM2 fm801_wput(struct pci_board_state *pcibs, uaecptr addr, uae_u32 b)
{
	struct fm801_data *data = &fm801;
	switch (addr)
	{
		case 0x08:
		fm801_control(data, b);
		break;
		case 0x0a:
		data->play_len = b;
		break;
		case 0x56:
		data->interrupt_control = b;
		fm801_interrupt(data);
		break;
		case 0x5a:
		data->interrupt_status &= ~b;
		fm801_interrupt(data);
		break;
	}
}
static void REGPARAM2 fm801_lput(struct pci_board_state *pcibs, uaecptr addr, uae_u32 b)
{
	struct fm801_data *data = &fm801;
	switch (addr)
	{
		case 0x0c:
		data->play_dma[0] = b;
		break;
		case 0x10:
		data->play_dma[1] = b;
		break;
	}
}
static uae_u32 REGPARAM2 fm801_bget(struct pci_board_state *pcibs, uaecptr addr)
{
	struct fm801_data *data = &fm801;
	uae_u32 v = 0;
	return v;
}
static uae_u32 REGPARAM2 fm801_wget(struct pci_board_state *pcibs, uaecptr addr)
{
	struct fm801_data *data = &fm801;
	uae_u32 v = 0;
	switch (addr) {
		case 0x08:
		v = data->play_control;
		break;
		case 0x0a:
		v = data->play_len2;
		break;
		case 0x56:
		v = data->interrupt_control;
		break;
		case 0x5a:
		v = data->interrupt_status;
		break;

	}
	return v;
}
static uae_u32 REGPARAM2 fm801_lget(struct pci_board_state *pcibs, uaecptr addr)
{
	struct fm801_data *data = &fm801;
	uae_u32 v = 0;
	switch(addr)
	{
		case 0x0c:
		v = data->play_dma2[data->dmach];
		break;
		case 0x10:
		v = data->play_dma2[data->dmach];
		break;
	}
	return v;
}

static void fm801_reset(struct pci_board_state *pcibs)
{
	struct fm801_data *data = &fm801;
	data->play_control = 0xca00;
	data->interrupt_control = 0x00df;
}

static void fm801_free(struct pci_board_state *pcibs)
{
	struct fm801_data *data = &fm801;
	fm801_active = false;
	fm801_stop(data);
}

static bool fm801_init(struct pci_board_state *pcibs)
{
	struct fm801_data *data = &fm801;
	memset(data, 0, sizeof(struct fm801_data));
	data->pcibs = pcibs;
	fm801_active = true;
	return false;
}

static const struct pci_config fm801_pci_config =
{
	0x1319, 0x0801, 0, 0, 0xb2, 0x040100, 0x80, 0x1319, 0x1319, 1, 0x04, 0x28, { 128 | 1, 0, 0, 0, 0, 0, 0 }
};
static const struct pci_config fm801_pci_config_func1 =
{
	0x1319, 0x0802, 0, 0, 0xb2, 0x098000, 0x80, 0x1319, 0x1319, 0, 0x04, 0x28, { 16 | 1, 0, 0, 0, 0, 0, 0 }
};

const struct pci_board fm801_pci_board =
{
	_T("FM801"),
	&fm801_pci_config, fm801_init, fm801_free, fm801_reset, fm801_hsync_handler, pci_irq_callback,
	{
		{ fm801_lget, fm801_wget, fm801_bget, fm801_lput, fm801_wput, fm801_bput },
		{ NULL },
		{ NULL },
		{ NULL },
		{ NULL },
		{ NULL },
		{ NULL },
	}
};

const struct pci_board fm801_pci_board_func1 =
{
	_T("FM801-2"),
	&fm801_pci_config_func1, NULL, NULL, NULL, NULL, NULL,
	{
		{ fm801_lget, fm801_wget, fm801_bget, fm801_lput, fm801_wput, fm801_bput },
		{ NULL },
		{ NULL },
		{ NULL },
		{ NULL },
		{ NULL },
		{ NULL },
	}
};

static void solo1_reset(struct pci_board_state *pcibs)
{
}

static void solo1_free(struct pci_board_state *pcibs)
{
}

static bool solo1_init(struct pci_board_state *pcibs)
{
	return true;
}

static void solo1_sb_put(struct pci_board_state *pcibs, uaecptr addr, uae_u32 b)
{
}
static uae_u32 solo1_sb_get(struct pci_board_state *pcibs, uaecptr addr)
{
	uae_u32 v = 0;
	return v;
}

static void solo1_put(struct pci_board_state *pcibs, int bar, uaecptr addr, uae_u32 b)
{
	if (bar == 1)
		solo1_sb_put(pcibs, addr, b);
}
static uae_u32 solo1_get(struct pci_board_state *pcibs, int bar, uaecptr addr)
{
	uae_u32 v = 0;
	if (bar == 1)
		v = solo1_sb_get(pcibs, addr);
	return v;
}

static void REGPARAM2 solo1_bput(struct pci_board_state *pcibs, uaecptr addr, uae_u32 b)
{
	write_log(_T("SOLO1 BPUT %08x=%08x %d\n"), addr, b, pcibs->selected_bar);
	solo1_put(pcibs, pcibs->selected_bar, addr + 0, b >> 24);
	solo1_put(pcibs, pcibs->selected_bar, addr + 1, b >> 16);
	solo1_put(pcibs, pcibs->selected_bar, addr + 2, b >>  8);
	solo1_put(pcibs, pcibs->selected_bar, addr + 3, b >>  0);
}
static void REGPARAM2 solo1_wput(struct pci_board_state *pcibs, uaecptr addr, uae_u32 b)
{
	write_log(_T("SOLO1 WPUT %08x=%08x %d\n"), addr, b, pcibs->selected_bar);
	solo1_put(pcibs, pcibs->selected_bar, addr + 0, b >> 8);
	solo1_put(pcibs, pcibs->selected_bar, addr + 1, b >> 0);
}
static void REGPARAM2 solo1_lput(struct pci_board_state *pcibs, uaecptr addr, uae_u32 b)
{
	write_log(_T("SOLO1 LPUT %08x=%08x %d\n"), addr, b, pcibs->selected_bar);
	solo1_put(pcibs, pcibs->selected_bar, addr, b);
}
static uae_u32 REGPARAM2 solo1_bget(struct pci_board_state *pcibs, uaecptr addr)
{
	uae_u32 v = 0;
	v = solo1_get(pcibs, pcibs->selected_bar, addr);
	write_log(_T("SOLO1 BGET %08x %d\n"), addr, pcibs->selected_bar);
	return v;
}
static uae_u32 REGPARAM2 solo1_wget(struct pci_board_state *pcibs, uaecptr addr)
{
	uae_u32 v = 0;
	write_log(_T("SOLO1 WGET %08x %d\n"), addr, pcibs->selected_bar);
	return v;
}
static uae_u32 REGPARAM2 solo1_lget(struct pci_board_state *pcibs, uaecptr addr)
{
	uae_u32 v = 0;
	write_log(_T("SOLO1 LGET %08x %d\n"), addr, pcibs->selected_bar);
	return v;
}

static const struct pci_config solo1_pci_config =
{
	0x125d, 0x1969, 0, 0, 0, 0x040100, 0, 0x125d, 0x1818, 1, 2, 0x18, { 16 | 1, 16 | 1, 16 | 1, 4 | 1, 4 | 1, 0, 0 }
};
const struct pci_board solo1_pci_board =
{
	_T("SOLO1"),
	&solo1_pci_config, solo1_init, solo1_free, solo1_reset, NULL, pci_irq_callback,
	{
		{ solo1_lget, solo1_wget, solo1_bget, solo1_lput, solo1_wput, solo1_bput },
		{ solo1_lget, solo1_wget, solo1_bget, solo1_lput, solo1_wput, solo1_bput },
		{ solo1_lget, solo1_wget, solo1_bget, solo1_lput, solo1_wput, solo1_bput },
		{ solo1_lget, solo1_wget, solo1_bget, solo1_lput, solo1_wput, solo1_bput },
		{ solo1_lget, solo1_wget, solo1_bget, solo1_lput, solo1_wput, solo1_bput },
		{ NULL },
		{ NULL },
	}
};

static SWVoiceOut *qemu_voice_out;

static void calculate_volume_qemu(void)
{
	SWVoiceOut *out = qemu_voice_out;
	if (!out)
		return;
	out->left_volume = (100 - currprefs.sound_volume_board) * 32768 / 100;
	out->right_volume = (100 - currprefs.sound_volume_board) * 32768 / 100;
}

void AUD_close_in(QEMUSoundCard *card, SWVoiceIn *sw)
{
}
int AUD_read(SWVoiceIn *sw, void *pcm_buf, int size)
{
	return size;
}
int AUD_write(SWVoiceOut *sw, void *pcm_buf, int size)
{
	memcpy(sw->samplebuf, pcm_buf, size);
	sw->samplebuf_total = size;
	return sw->samplebuf_total;
}
void AUD_set_active_out(SWVoiceOut *sw, int on)
{
	sw->active = on != 0;
	sw->event_time = base_event_clock * CYCLE_UNIT / sw->freq;
	sw->samplebuf_index = 0;
	sw->samplebuf_total = 0;
	calculate_volume_qemu();
	audio_enable_sndboard(sw->active);
}
void AUD_set_active_in(SWVoiceIn *sw, int on)
{
}
int  AUD_is_active_in(SWVoiceIn *sw)
{
	return 0;
}
void AUD_close_out(QEMUSoundCard *card, SWVoiceOut *sw)
{
	qemu_voice_out = NULL;
	audio_enable_sndboard(false);
	xfree(sw);
}
SWVoiceIn *AUD_open_in(
	QEMUSoundCard *card,
	SWVoiceIn *sw,
	const char *name,
	void *callback_opaque,
	audio_callback_fn callback_fn,
struct audsettings *settings)
{
	return NULL;
}
SWVoiceOut *AUD_open_out(
	QEMUSoundCard *card,
	SWVoiceOut *sw,
	const char *name,
	void *callback_opaque,
	audio_callback_fn callback_fn,
	struct audsettings *settings)
{
	SWVoiceOut *out = sw;
	if (!sw)
		out = xcalloc(SWVoiceOut, 1);
	int bits = 8;

	if (settings->fmt >= AUD_FMT_U16)
		bits = 16;
	if (settings->fmt >= AUD_FMT_U32)
		bits = 32;

	out->callback = callback_fn;
	out->opaque = callback_opaque;
	out->bits = bits;
	out->freq = settings->freq;
	out->ch = settings->nchannels;
	out->fmt = settings->fmt;
	out->bytesperframe = out->ch * bits / 8;

	write_log(_T("QEMU AUDIO: freq=%d ch=%d bits=%d (fmt=%d) '%s'\n"), out->freq, out->ch, bits, settings->fmt, name);

	qemu_voice_out = out;

	return out;
}

static void audio_state_sndboard_qemu(int ch)
{
	SWVoiceOut *out = qemu_voice_out;

	if (!out)
		return;
	if (out->active && ch == 0) {
		uae_s16 l, r;
		if (out->samplebuf_index >= out->samplebuf_total) {
			int maxsize = sizeof(out->samplebuf);
			int size = 128 * out->bytesperframe;
			if (size > maxsize)
				size = maxsize;
			out->callback(out->opaque, size);
			out->samplebuf_index = 0;
		}
		uae_u8 *p = out->samplebuf + out->samplebuf_index;
		if (out->bits == 8) {
			if (out->ch == 1) {
				p[1] = p[0];
				p[2] = p[0];
				p[3] = p[0];
			} else {
				p[2] = p[1];
				p[3] = p[1];
				p[1] = p[0];
			}
		} else {
			if (out->ch == 1) {
				p[2] = p[0];
				p[3] = p[1];
			}
		}
		l = (p[1] << 8) | p[0];
		r = (p[3] << 8) | p[2];
		out->ch_sample[0] = l;
		out->ch_sample[1] = r;
		out->ch_sample[0] = out->ch_sample[0] * out->left_volume / 32768;
		out->ch_sample[1] = out->ch_sample[1] * out->right_volume / 32768;
		out->samplebuf_index += out->bytesperframe;
	}
	audio_state_sndboard_state(ch, out->ch_sample[ch], out->event_time);
}


static void sndboard_vsync_qemu(void)
{
	audio_activate();
}


void audio_state_sndboard(int ch)
{
	if (toccata.toccata_active)
		audio_state_sndboard_toccata(ch);
	if (fm801_active)
		audio_state_sndboard_fm801(ch);
	if (qemu_voice_out && qemu_voice_out->active)
		audio_state_sndboard_qemu(ch);
}

void sndboard_vsync(void)
{
	if (toccata.toccata_active)
		sndboard_vsync_toccata();
	if (fm801_active)
		sndboard_vsync_fm801();
	if (qemu_voice_out && qemu_voice_out->active)
		sndboard_vsync_qemu();
}

void sndboard_ext_volume(void)
{
	if (toccata.toccata_active)
		calculate_volume_toccata();
	if (fm801_active)
		calculate_volume_fm801();
	if (qemu_voice_out && qemu_voice_out->active)
		calculate_volume_qemu();
}

#ifdef _WIN32

#include <mmdeviceapi.h>
#include <Audioclient.h>

#define REFTIMES_PER_SEC  10000000

static const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
static const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
static const IID IID_IAudioClient = __uuidof(IAudioClient);
static const IID IID_IAudioCaptureClient = __uuidof(IAudioCaptureClient);

#define EXIT_ON_ERROR(hres) if (FAILED(hres)) { goto Exit; }
#define SAFE_RELEASE(punk)  if ((punk) != NULL) { (punk)->Release(); (punk) = NULL; }

static IMMDeviceEnumerator *pEnumerator = NULL;
static IMMDevice *pDevice = NULL;
static IAudioClient *pAudioClient = NULL;
static IAudioCaptureClient *pCaptureClient = NULL;
static bool capture_started;

static uae_u8 *sndboard_get_buffer(int *frames)
{	
	HRESULT hr;
	UINT32 numFramesAvailable;
	BYTE *pData;
	DWORD flags = 0;

	*frames = -1;
	if (!capture_started)
		return NULL;
	hr = pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, NULL, NULL);
	if (FAILED(hr)) {
		write_log(_T("GetBuffer failed %08x\n"), hr);
		return NULL;
	}
	*frames = numFramesAvailable;
	return pData;
}

static void sndboard_release_buffer(uae_u8 *buffer, int frames)
{
	HRESULT hr;
	if (!capture_started || frames < 0)
		return;
	hr = pCaptureClient->ReleaseBuffer(frames);
	if (FAILED(hr)) {
		write_log(_T("ReleaseBuffer failed %08x\n"), hr);
	}
}

static void sndboard_free_capture(void)
{
	if (capture_started)
		pAudioClient->Stop();
	capture_started = false;
	SAFE_RELEASE(pEnumerator)
	SAFE_RELEASE(pDevice)
	SAFE_RELEASE(pAudioClient)
	SAFE_RELEASE(pCaptureClient)
}

static bool sndboard_init_capture(int freq)
{
	HRESULT hr;
	WAVEFORMATEX wavfmtsrc;
	WAVEFORMATEX *wavfmt2;
	WAVEFORMATEX *wavfmt;

	wavfmt2 = NULL;

	hr = CoCreateInstance(
		CLSID_MMDeviceEnumerator, NULL,
		CLSCTX_ALL, IID_IMMDeviceEnumerator,
		(void**)&pEnumerator);
	EXIT_ON_ERROR(hr)

	hr = pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &pDevice);
	EXIT_ON_ERROR(hr)

	hr = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pAudioClient);
	EXIT_ON_ERROR(hr)

	memset (&wavfmtsrc, 0, sizeof wavfmtsrc);
	wavfmtsrc.nChannels = 2;
	wavfmtsrc.nSamplesPerSec = freq;
	wavfmtsrc.wBitsPerSample = 16;
	wavfmtsrc.wFormatTag = WAVE_FORMAT_PCM;
	wavfmtsrc.cbSize = 0;
	wavfmtsrc.nBlockAlign = wavfmtsrc.wBitsPerSample / 8 * wavfmtsrc.nChannels;
	wavfmtsrc.nAvgBytesPerSec = wavfmtsrc.nBlockAlign * wavfmtsrc.nSamplesPerSec;

	bool init = false;
	AUDCLNT_SHAREMODE exc;
	for (int mode = 0; mode < 2; mode++) {
		exc = mode == 0 ? AUDCLNT_SHAREMODE_EXCLUSIVE : AUDCLNT_SHAREMODE_SHARED;
		int time = mode == 0 ? 0 : REFTIMES_PER_SEC / 50;

		wavfmt = &wavfmtsrc;
		hr = pAudioClient->IsFormatSupported(exc, &wavfmtsrc, &wavfmt2);
		if (SUCCEEDED(hr)) {
			hr = pAudioClient->Initialize(exc, 0, time, 0, wavfmt, NULL);
			if (SUCCEEDED(hr)) {
				init = true;
				break;
			}
		}
	
		if (hr == S_FALSE && wavfmt2) {
			wavfmt = wavfmt2;
			hr = pAudioClient->Initialize(exc, 0, time, 0, wavfmt, NULL);
			if (SUCCEEDED(hr)) {
				init = true;
				break;
			}
		}
	}

	if (!init) {
		write_log(_T("sndboard capture init, freq=%d, failed\n"), freq);
		goto Exit;
	}


	hr = pAudioClient->GetService(IID_IAudioCaptureClient, (void**)&pCaptureClient);
	EXIT_ON_ERROR(hr)
		
	hr = pAudioClient->Start();
	EXIT_ON_ERROR(hr)
	capture_started = true;

	CoTaskMemFree(wavfmt2);

	write_log(_T("sndboard capture started: freq=%d mode=%s\n"), freq, exc == AUDCLNT_SHAREMODE_EXCLUSIVE ? _T("exclusive") : _T("shared"));

	return true;
Exit:;
	CoTaskMemFree(wavfmt2);
	write_log(_T("sndboard capture init failed %08x\n"), hr);
	sndboard_free_capture();
	return false;
}

#endif
