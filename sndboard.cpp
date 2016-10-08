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
#include "rommgr.h"

static uae_u8 *sndboard_get_buffer(int *frames);
static void sndboard_release_buffer(uae_u8 *buffer, int frames);
static void sndboard_free_capture(void);
static bool sndboard_init_capture(int freq);

static double base_event_clock;

extern addrbank uaesndboard_bank;

#define MAX_DUPLICATE_SOUND_BOARDS 1

#define MAX_UAE_CHANNELS 8
#define MAX_UAE_STREAMS 8
struct uaesndboard_stream
{
	int streamid;
	int play;
	int ch;
	int bits;
	int volume;
	int freq;
	int event_time;
	uaecptr baseaddr;
	uaecptr setaddr;
	uaecptr address;
	uaecptr next;
	uaecptr repeat;
	int len;
	int replen;
	int repcnt;
	bool first;
	bool repeating;
	uae_u8 chmode;
	uae_u8 intenamask;
	uae_u8 intreqmask;
	int framesize;
	uae_u8 io[256];
	uae_u16 wordlatch;
	int sample[MAX_UAE_CHANNELS];
};
struct uaesndboard_data
{
	bool enabled;
	bool z3;
	int configured;
	uae_u32 streammask;
	uae_u32 streamallocmask;
	uae_u8 acmemory[128];
	int streamcnt;
	int volume[MAX_UAE_CHANNELS];
	struct uaesndboard_stream stream[MAX_UAE_STREAMS];
	uae_u8 info[256];
};
static struct uaesndboard_data uaesndboard[MAX_DUPLICATE_SOUND_BOARDS];

/*
	autoconfig data:

	manufacturer 6502
	product 2
	Z2 64k board or Z3 16M board, no boot rom.

	uaesnd sample set structure, located in Amiga address space.
	It is never modified by UAE. Must be long aligned.

	 0.L sample address pointer
	 4.L sample length (in frames, negative = play backwards, set address after last sample.). 0 = next set/repeat after single sample period.
	 8.L playback frequency (Hz)
	12.L repeat sample address pointer (ignored if repeat count=0)
	16.L repeat sample length (ignored if repeat count=0, negative = play backwards)
	20.W repeat count (0=no repeat, -1=forever)
	22.W volume (0 to 32768)
	24.L next sample set address (0=end, this was last set)
	28.B number of channels. (interleaved samples if 2 or more channels)
	29.B bits per sample (8, 16, 24 or 32)
	30.B bit 0: interrupt when set starts, bit 1: interrupt when set ends (last sample played), bit 2: interrupt when repeat starts, bit 3: after each sample.
	31.B if mono stream, bit mask that selects output channels. (0=default, redirect to left and right channels)
	(Can be used for example when playing tracker modules by using 4 single channel streams)
	stereo or higher: channel mode.

	Channel mode = 0

	2: left, right
	4: left, right, left back, right back
	6: left, right, center, lfe, left back, right back, left surround
	8: left, right, center, lfe, left back, right back, right surround

	Channel mode = 1 (alternate channel order)

	2: left, right (no change)
	4: left, right, center, back
	6: left, right, left back, right back, center, lfe
	8: left, right, left back, right back, left surround, right surround, center, lfe

	Hardware addresses relative to uaesnd autoconfig board start:

	Read-only hardware information:

	$0080.L uae version (ver.rev.subrev)
	$0084.W uae sndboard "hardware" version
	$0086.W uae sndboard "hardware" revision
	$0088.L preferred frequency (=configured mixing/resampling rate)
	$008C.B max number of channels/stream (>=6: 5.1 fully supported, 7.1 also supported but last 2 surround channels are currently muted)
	$008D.B active number of channels (mirrors selected UAE sound mode. inactive channels are muted but can be still be used normally.)
	$008E.B max number of simultaneous audio streams (currently 8)

	$00E0.L allocated streams, bit mask. Hardware level stream allocation feature, use single test and set/clear instruction or
		disable interrupts before allocating/freeing streams. If stream bit is not set, stream's address range is inactive.
		byte access to $00E3 is also allowed.

	$00F0.L stream enable bit mask. RW. Can be used to start or stop multiple streams simultaneously.
	Changing stream mask always clears stream's interrupt status register.

	$0100 stream 1

	$0100-$011f: Current sample set structure. RW.
	$0140-$015f: Latched sample set structure.
		Reading from $0140.W/.L makes copy of current contents of $0100-$011f.
		Writing to $0142.W/$0140.L copies back to $0100-$011f
	$0180.L Current sample set pointer. RW.
	$0184.B Reserved
	$0185.B Reserved
	$0186.B Reserved
	$0187.B Interrupt status. 7: set when interrupt active. 0,1,2,3: same as 30.B bit, always set when condition matches,
		even if 30.B bit is not set. If also 30.B bit is set: bit 7 set and interrupt is activated.
		Reading clears interrupt. RO.
	$0188.B Reserved
	$0189.B Reserved
	$018A.B Reserved
	$018B.B Alternate interrupt status. Same as $187.B but reading does not clear interrupt. RO.

	$0200 stream 2

	...

	$0800 stream 8

	Writing non-zero to sample set pointer ($180) starts audio if not already started and stream enable ($F0) bit is set.
	Writing zero stops the stream immediately. Also clears automatically when stream ends.

	Long wide registers have special feature when read using word wide reads (68000/10 CPU), when high word is read,
	low word is copied to internal register. Following low word read comes from register copy.
	This prevents situation where sound emulation can modify the register between high and low word reads.
	68020+ long accesses are always guaranteed safe.

	Set structure is copied to emulator side internal address space when set starts.
	This data can be always read and written in real time by accessing $0100-$011f space.
	Writes are nearly immediate, values are updated during next sample period.
	(It probably is not a good idea to do on the fly change number of channels or bits per sample values..)

	Use hardware current sample set structure to detect current sample address, length and repeat count. 

	Repeat address pointer and length values are (re-)fetched from set structure each time when new repeat starts.

	Next sample set pointer is fetched when current set finishes.

	Reading interrupt status register will also clear interrupts.
	Interrupt is level 6 (EXTER).

	Sample set parameters are validated, any error causes audio to stop immediately and log message will be printed.

	During non-repeat playback sample address increases and sample length decreases. When length becomes zero,
	repeat count is checked, if it is non-zero, repeat address and repeat length are loaded from memory and start
	counting (address increases, length decreases). Note that sample address and length won't change anymore.
	when repeat counter becomes zero (or it was already zero), next sample set address is loaded and started.

	Hardware word and long accesses must be aligned (unaligned write is ignored, unaligned read will return 0xffff or 0xffffffff)

*/

static bool audio_state_sndboard_uae(int streamid);

static bool uaesnd_rethink(void)
{
	bool irq = false;
	for (int j = 0; j < MAX_DUPLICATE_SOUND_BOARDS; j++) {
		struct uaesndboard_data *data = &uaesndboard[j];
		if (data->enabled) {
			for (int i = 0; i < MAX_UAE_STREAMS; i++) {
				struct uaesndboard_stream *s = &uaesndboard[j].stream[i];
				if (s->intreqmask & 0x80) {
					irq = true;
					break;
				}
			}
		}
	}
	return irq;
}

static void uaesnd_setfreq(struct uaesndboard_stream *s)
{
	if (s->freq < 1)
		s->freq = 1;
	if (s->freq > 96000)
		s->freq = 96000;
	s->event_time = base_event_clock * CYCLE_UNIT / s->freq;
}

static struct uaesndboard_stream *uaesnd_addr(uaecptr addr)
{
	if (addr < 0x100)
		return NULL;
	int stream = (addr - 0x100) / 0x100;
	if (stream >= MAX_UAE_STREAMS)
		return NULL;
	if (!(uaesndboard[0].streamallocmask & (1 << stream)))
		return NULL;
	return &uaesndboard[0].stream[stream];
}

static struct uaesndboard_stream *uaesnd_get(uaecptr addr)
{
	struct uaesndboard_stream *s = uaesnd_addr(addr);
	if (!s)
		return NULL;

	put_long_host(s->io + 0, s->address);
	put_long_host(s->io + 4, s->len);
	put_long_host(s->io + 8, s->freq);
	put_long_host(s->io + 12, s->repeat);
	put_long_host(s->io + 16, s->replen);
	put_word_host(s->io + 20, s->repcnt);
	put_word_host(s->io + 22, s->volume);
	put_long_host(s->io + 24, s->next);
	put_byte_host(s->io + 28, s->ch);
	put_byte_host(s->io + 29, s->bits);
	put_byte_host(s->io + 30, s->intenamask);
	put_byte_host(s->io + 31, s->chmode);
	put_long_host(s->io + 0x80, s->setaddr);
	put_long_host(s->io + 0x84, s->intreqmask);
	put_long_host(s->io + 0x88, s->intreqmask);

	return s;
}

static void uaesndboard_stop(struct uaesndboard_stream *s)
{
	struct uaesndboard_data *data = &uaesndboard[0];
	if (!s->play)
		return;
	s->play = 0;
	data->streammask &= ~(1 << (s - data->stream));
	audio_enable_stream(false, s->streamid, 0, NULL);
	s->streamid = 0;
	data->streamcnt--;
}

static void uaesndboard_start(struct uaesndboard_stream *s)
{
	struct uaesndboard_data *data = &uaesndboard[0];

	if (s->play)
		return;

	if (!(data->streammask & (1 << (s - data->stream))))
		return;

	data->streamcnt++;
	s->play = 1;
	for (int i = 0; i < MAX_UAE_CHANNELS; i++) {
		s->sample[i] = 0;
	}
	uaesnd_setfreq(s);
	s->streamid = audio_enable_stream(true, -1, MAX_UAE_CHANNELS, audio_state_sndboard_uae);
	if (!s->streamid) {
		uaesndboard_stop(s);
	}
}

static bool uaesnd_validate(struct uaesndboard_stream *s)
{

	s->framesize = s->bits * s->ch / 8;

	if (s->ch < 1 || s->ch > MAX_UAE_CHANNELS || s->ch == 3 || s->ch == 5 || s->ch == 7) {
		write_log(_T("UAESND: unsupported number of channels %d\n"), s->ch);
		return false;
	}
	if (s->bits != 8 && s->bits != 16 && s->bits != 24 && s->bits != 32) {
		write_log(_T("UAESND: unsupported sample bits %d\n"), s->bits);
		return false;
	}
	if (s->freq < 1 || s->freq > 96000) {
		write_log(_T("UAESND: unsupported frequency %d\n"), s->freq);
		return false;
	}
	if (s->volume < 0 || s->volume > 32768) {
		write_log(_T("UAESND: unsupported volume %d\n"), s->volume);
		return false;
	}
	if (s->next && ((s->next & 1) || !valid_address(s->next, 32))) {
		write_log(_T("UAESND: invalid next sample set pointer %08x\n"), s->next);
		return false;
	}
	uaecptr saddr = s->address;
	if (s->len < 0)
		saddr -= abs(s->len) * s->framesize;
	if (!valid_address(saddr, abs(s->len) * s->framesize)) {
		write_log(_T("UAESND: invalid sample pointer range %08x - %08x\n"), saddr, saddr + s->len * s->framesize);
		return false;
	}
	if (s->repcnt) {
		if (s->replen == 0) {
			write_log(_T("UAESND: invalid repeat len %d\n"), s->replen);
			return false;
		}
		uaecptr repeat = s->repeat;
		if (s->replen < 0)
			repeat -= abs(s->replen) * s->framesize;
		if (s->repeat && !valid_address(repeat, abs(s->replen) * s->framesize)) {
			write_log(_T("UAESND: invalid sample repeat pointer range %08x - %08x\n"), repeat, repeat + s->replen * s->framesize);
			return false;
		}
	}
	uaesnd_setfreq(s);
	for (int i = s->ch; i < MAX_UAE_CHANNELS; i++) {
		s->sample[i] = 0;
	}
	return true;
}

static void uaesnd_load(struct uaesndboard_stream *s, uaecptr addr)
{
	s->address = get_long(addr);
	s->len = get_long(addr + 4);
	s->freq = get_long(addr + 8);
	s->repeat = get_long(addr + 12);
	s->replen = get_long(addr + 16);
	s->repcnt = get_word(addr + 20);
	s->volume = get_word(addr + 22);
	s->next = get_long(addr + 24);
	s->ch = get_byte(addr + 28);
	s->bits = get_byte(addr + 29);
	s->intenamask = get_byte(addr + 30);
	s->chmode = get_byte(addr + 31);
}

static bool uaesnd_directload(struct uaesndboard_stream *s, int reg)
{
	if (reg < 0 || reg == 0)
		s->address = get_long_host(s->io + 0);
	if (reg < 0 || reg == 4)
		s->len = get_long_host(s->io + 4);
	if (reg < 0 || reg == 8)
		s->freq = get_long_host(s->io + 8);
	return uaesnd_validate(s);
}

static bool uaesnd_next(struct uaesndboard_stream *s, uaecptr addr)
{
	if ((addr & 3) || !valid_address(addr, 32)) {
		write_log(_T("UAESND: invalid sample set pointer %08x\n"), addr);
		return false;
	}

	s->setaddr = addr;
	uaesnd_load(s, addr);
	s->first = true;
	s->repeating = false;

	return uaesnd_validate(s);
}

static void uaesnd_stream_start(struct uaesndboard_stream *s)
{
	struct uaesndboard_data *data = &uaesndboard[0];
	if (!s->play && s->next) {
		if (data->streammask & (1 << (s - data->stream))) {
			if (uaesnd_next(s, s->next)) {
				uaesndboard_start(s);
			}
		}
	} else if (s->play && !s->next) {
		uaesndboard_stop(s);
	}
}

static void uaesnd_irq(struct uaesndboard_stream *s, uae_u8 mask)
{
	s->intreqmask |= mask;
	if ((s->intenamask & mask)) {
		s->intreqmask |= 0x80;
		sndboard_rethink();
	}
}

static void uaesnd_streammask(uae_u32 m)
{
	struct uaesndboard_data *data = &uaesndboard[0];
	uae_u32 old = data->streammask;
	data->streammask = m;
	data->streammask &= (1 << MAX_UAE_STREAMS) - 1;
	for (int i = 0; i < MAX_UAE_STREAMS; i++) {
		if ((old ^ data->streammask) & (1 << i)) {
			struct uaesndboard_stream *s = &data->stream[i];
			s->intreqmask = 0;
			if (data->streammask & (1 << i)) {
				uaesnd_stream_start(s);
			} else {
				uaesndboard_stop(s);
			}
		}
	}
}

static bool audio_state_sndboard_uae(int streamid)
{
	struct uaesndboard_data *data = &uaesndboard[0];
	struct uaesndboard_stream *s = NULL;

	for (int i = 0; i < MAX_UAE_STREAMS; i++) {
		if (data->stream[i].streamid == streamid) {
			s = &data->stream[i];
			break;
		}
	}
	if (!s)
		return false;
	int highestch = s->ch;
	int streamnum = s - data->stream;
	if (s->play && (data->streammask & (1 << streamnum))) {
		int len = s->repeating ? s->replen : s->len;
		uaecptr addr = s->repeating ? s->repeat : s->address;
		if (len != 0) {
			if (len < 0)
				addr -= s->framesize;
			for (int i = 0; i < s->ch; i++) {
				uae_s16 sample = 0;
				if (s->bits == 16) {
					sample = get_word(addr);
					addr += 2;
				} else if (s->bits == 8) {
					sample = get_byte(addr);
					sample = (sample << 8) | sample;
					addr += 1;
				} else if (s->bits == 24) {
					sample = get_word(addr); // just drop lowest 8 bits for now
					addr += 3;
				} else if (s->bits == 32) {
					sample = get_word(addr); // just drop lowest 16 bits for now
					addr += 4;
				}
				s->sample[i] = sample * ((s->volume + 1) / 2 + (data->volume[i] + 1) / 2) / 32768;
			}
			if (len < 0)
				addr -= s->framesize;
			if (s->repeating) {
				s->repeat = addr;
				if (s->replen > 0)
					s->replen--;
				else
					s->replen++;
				len = s->replen;
			} else {
				s->address = addr;
				if (s->len > 0)
					s->len--;
				else
					s->len++;
				len = s->len;
			}
			uaesnd_irq(s, 8);
		}
		if (s->first) {
			uaesnd_irq(s, 1);
			s->first = false;
		}
		if (len == 0) {
			// sample ended
			if (s->repcnt) {
				if (s->repcnt != 0xffff)
					s->repcnt--;
				s->repeat = get_long(s->setaddr + 12);
				s->replen = get_long(s->setaddr + 16);
				s->repeating = true;
				uaesnd_irq(s, 4);
			} else {
				// set ended
				uaesnd_irq(s, 2);
				s->next = get_long(s->setaddr + 24);
				if (s->next) {
					if (!uaesnd_next(s, s->next)) {
						uaesndboard_stop(s);
					}
				} else {
					uaesndboard_stop(s);
				}
			}
		}
	}
	if (s->ch == 1 && s->chmode) {
		int smp = s->sample[0];
		for (int i = 0; i < MAX_UAE_CHANNELS; i++) {
			if ((1 << i) & s->chmode) {
				s->sample[i] = smp;
				if (i > highestch)
					highestch = i;
			}
		}
	} else if (s->ch == 4 && s->chmode == 1) {
		s->sample[2] = s->sample[4];
		s->sample[3] = s->sample[5];
	} else if (s->ch == 6 && s->chmode == 1) {
		int c = s->sample[2];
		int lfe = s->sample[3];
		s->sample[2] = s->sample[4];
		s->sample[3] = s->sample[5];
		s->sample[4] = c;
		s->sample[5] = lfe;
	} else if (s->ch == 8 && s->chmode == 1) {
		int c = s->sample[2];
		int lfe = s->sample[3];
		s->sample[2] = s->sample[4];
		s->sample[3] = s->sample[5];
		s->sample[4] = s->sample[6];
		s->sample[5] = s->sample[7];
		s->sample[6] = c;
		s->sample[7] = lfe;
	}
	audio_state_stream_state(s->streamid, s->sample, highestch, s->event_time);
	return true;
}

static void uaesnd_latch(struct uaesndboard_stream *s)
{
	memcpy(s->io + 0x40, s->io + 0x00, 0x40);
}
static void uaesnd_latch_back(struct uaesndboard_stream *s)
{
	memcpy(s->io + 0x00, s->io + 0x40, 0x40);
	if (!uaesnd_directload(s, -1)) {
		uaesndboard_stop(s);
	} else if (!s->play) {
		uaesndboard_start(s);
	}
}

static void uaesnd_put(struct uaesndboard_stream *s, int reg)
{
	if (reg == 0x80) { // set pointer write?
		uaecptr setaddr = get_long_host(s->io + 0x80);
		s->next = setaddr;
		uaesnd_stream_start(s);
	} else {
		if (!uaesnd_directload(s, reg)) {
			uaesndboard_stop(s);
		}
	}
}

static void uaesnd_configure(void)
{
	struct uaesndboard_data *data = &uaesndboard[0];
	data->configured = 1;
	for (int i = 0; i < MAX_UAE_STREAMS; i++) {
		data->stream[i].baseaddr = expamem_board_pointer + 0x100 + 0x100 * i;
	}
}

static uae_u32 REGPARAM2 uaesndboard_bget(uaecptr addr)
{
	struct uaesndboard_data *data = &uaesndboard[0];
	addr &= 65535;
	if (addr < 0x80) {
		return data->acmemory[addr];
	} else if (data->configured) {
		struct uaesndboard_stream *s = uaesnd_get(addr);
		if (s) {
			int reg = addr & 255;
			if (reg == 0x87)
				s->intreqmask = 0;
			return get_byte_host(s->io + reg);
		} else if (addr >= 0x80 && addr < 0xe0) {
			return get_byte_host(data->info + (addr & 0x7f));
		} else if (addr >= 0xe0 && addr <= 0xe3) {
			return data->streamallocmask >> (8 * (3 - (addr - 0xe0)));
		} else if (addr >= 0xf0 && addr <= 0xf3) {
			return data->streammask >> ((3 - (addr - 0xf0)) * 8);
		}
	}
	return 0;
}
static uae_u32 REGPARAM2 uaesndboard_wget(uaecptr addr)
{
	struct uaesndboard_data *data = &uaesndboard[0];
	addr &= 65535;
	if (addr < 0x80) {
		return (uaesndboard_bget(addr) << 8) | uaesndboard_bget(addr + 1);
	} else if (data->configured) {
		if (addr & 1)
			return 0xffff;
		struct uaesndboard_stream *s = uaesnd_get(addr);
		if (s) {
			int reg = addr & 255;
			int reg4 = reg / 4;
			if (reg4 <= 4 || reg4 == 6) {
				if (!(reg & 2)) {
					s->wordlatch = get_word_host(s->io + ((reg + 2) & 255));
				} else {
					return s->wordlatch;
				}
			}
			if (reg == 0x40) {
				uaesnd_latch(s);
			} else if (reg == 0x86) {
				s->intreqmask = 0;
			}
			return get_word_host(s->io + reg);
		} else if (addr >= 0x80 && addr < 0xe0 - 1) {
			return get_word_host(data->info + (addr & 0x7f));
		} else if (addr == 0xe0) {
			return data->streamallocmask >> 16;
		} else if (addr == 0xe2) {
			return data->streamallocmask >> 0;
		} else if (addr == 0xf0) {
			return data->streammask >> 16;
		} else if (addr == 0xf2) {
			return data->streammask;
		}
	}
	return 0;
}
static uae_u32 REGPARAM2 uaesndboard_lget(uaecptr addr)
{
	struct uaesndboard_data *data = &uaesndboard[0];
	addr &= 65535;
	if (addr < 0x80) {
		return (uaesndboard_wget(addr) << 16) | uaesndboard_wget(addr + 2);
	} else if (data->configured) {
		if (addr & 3)
			return 0xffffffff;
		struct uaesndboard_stream *s = uaesnd_get(addr);
		if (s) {
			int reg = addr & 255;
			if (reg == 0x40)
				uaesnd_latch(s);
			if (reg == 0x84)
				s->intreqmask = 0;
			return get_long_host(s->io + reg);
		} else if (addr >= 0x80 && addr < 0xe0 - 3) {
			return get_long_host(data->info + (addr & 0x7f));
		} else if (addr == 0xe0) {
			return data->streamallocmask;
		} else if (addr == 0xf0) {
			return data->streammask;
		}
	}
	return 0;
}
static void REGPARAM2 uaesndboard_bput(uaecptr addr, uae_u32 b)
{
	struct uaesndboard_data *data = &uaesndboard[0];
	addr &= 65535;
	if (!data->configured) {
		switch (addr) {
			case 0x48:
			if (!data->z3) {
				map_banks_z2(&uaesndboard_bank, expamem_board_pointer >> 16, 65536 >> 16);
				uaesnd_configure();
				expamem_next(&uaesndboard_bank, NULL);
			}
			break;
			case 0x4c:
			data->configured = -1;
			expamem_shutup(&uaesndboard_bank);
			break;
		}
		return;
	} else {
		struct uaesndboard_stream *s = uaesnd_addr(addr);
		if (s) {
			int reg = addr & 255;
			put_byte_host(s->io + reg, b);
			uaesnd_put(s, reg);
		} else if (addr >= 0xe0 && addr <= 0xe3) {
			uae_u32 v = data->streamallocmask;
			int shift = 8 * (3 - (addr - 0xe0));
			uae_u32 mask = 0xff;
			v &= ~(mask << shift);
			v |= b << shift;
			uaesnd_streammask(data->streammask & v);
			data->streamallocmask &= ~(mask << shift);
			data->streamallocmask |= v << shift;
		} else if (addr >= 0xf0 && addr <= 0xf3) {
			b <<= 8 * (3 - (addr - 0xf0));
			uaesnd_streammask(b);
		}
	}
}
static void REGPARAM2 uaesndboard_wput(uaecptr addr, uae_u32 b)
{
	struct uaesndboard_data *data = &uaesndboard[0];
	addr &= 65535;
	if (!data->configured) {
		switch (addr) {
			case 0x44:
			if (data->z3) {
				map_banks_z3(&uaesndboard_bank, expamem_board_pointer >> 16, (16 * 1024 * 1024) >> 16);
				uaesnd_configure();
				expamem_next(&uaesndboard_bank, NULL);
			}
			break;
		}
		return;
	} else {
		if (addr & 1)
			return;
		struct uaesndboard_stream *s = uaesnd_addr(addr);
		if (s) {
			int reg = addr & 255;
			put_word_host(s->io + reg, b);
			uaesnd_put(s, reg);
			if (reg == 0x42) {
				uaesnd_latch_back(s);
			}
		} else if (addr == 0xf0) {
			uaesnd_streammask(b << 16);
		} else if (addr == 0xf2) {
			uaesnd_streammask(b);
		}
	}
}
static void REGPARAM2 uaesndboard_lput(uaecptr addr, uae_u32 b)
{
	struct uaesndboard_data *data = &uaesndboard[0];
	addr &= 65535;
	if (data->configured) {
		if (addr & 3)
			return;
		struct uaesndboard_stream *s = uaesnd_addr(addr);
		if (s) {
			int reg = addr & 255;
			put_long_host(s->io + reg, b);
			uaesnd_put(s, reg);
			if (reg == 0x40) {
				uaesnd_latch_back(s);
			}
		} else if (addr == 0xe0) {
			uaesnd_streammask(data->streammask & b);
			data->streamallocmask = b;
		} else if (addr == 0xf0) {
			uaesnd_streammask(b);
		}
	}
}

static addrbank uaesndboard_bank = {
	uaesndboard_lget, uaesndboard_wget, uaesndboard_bget,
	uaesndboard_lput, uaesndboard_wput, uaesndboard_bput,
	default_xlate, default_check, NULL, NULL, _T("uaesnd"),
	dummy_lgeti, dummy_wgeti,
	ABFLAG_IO, S_READ, S_WRITE
};

static void ew(uae_u8 *acmemory, int addr, uae_u32 value)
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

bool uaesndboard_init (struct autoconfig_info *aci, int z)
{
	struct uaesndboard_data *data = &uaesndboard[0];

	data->configured = 0;
	data->enabled = true;
	data->z3 = z == 3;
	memset(data->acmemory, 0xff, sizeof data->acmemory);
	const struct expansionromtype *ert = get_device_expansion_rom(z == 3 ? ROMTYPE_UAESNDZ3 : ROMTYPE_UAESNDZ2);
	if (!ert)
		return false;
	data->streammask = 0;
	data->volume[0] = 32768;
	data->volume[1] = 32768;
	put_long_host(data->info + 0, (UAEMAJOR << 16) | (UAEMINOR << 8) | (UAESUBREV));
	put_long_host(data->info + 4, (1 << 16) | (0));
	put_long_host(data->info + 8, currprefs.sound_freq);
	put_byte_host(data->info + 12, MAX_UAE_CHANNELS);
	put_byte_host(data->info + 13, get_audio_nativechannels(currprefs.sound_stereo));
	put_byte_host(data->info + 14, MAX_UAE_STREAMS);
	for (int i = 0; i < 16; i++) {
		uae_u8 b = ert->autoconfig[i];
		ew(data->acmemory, i * 4, b);
	}

	memcpy(aci->autoconfig_raw, data->acmemory, sizeof data->acmemory);
	aci->addrbank = &uaesndboard_bank;
	return true;
}

bool uaesndboard_init_z2(struct autoconfig_info *aci)
{
	return uaesndboard_init(aci, 2);
}
bool uaesndboard_init_z3(struct autoconfig_info *aci)
{
	return uaesndboard_init(aci, 3);
}

void uaesndboard_free(void)
{
	for (int j = 0; j < MAX_DUPLICATE_SOUND_BOARDS; j++) {
		struct uaesndboard_data *data = &uaesndboard[j];
		data->enabled = false;
	}
	sndboard_rethink();
}

void uaesndboard_reset(void)
{
	for (int j = 0; j < MAX_DUPLICATE_SOUND_BOARDS; j++) {
		struct uaesndboard_data *data = &uaesndboard[j];
		if (data->enabled) {
			for (int i = 0; i < MAX_UAE_STREAMS; i++) {
				if (data->stream[i].streamid) {
					audio_enable_stream(false, data->stream[i].streamid, 0, NULL);
					memset(&data->stream[i], 0, sizeof(struct uaesndboard_stream));
				}
			}
		}
		data->streammask = 0;
	}
}


// TOCCATA

#define DEBUG_TOCCATA 0

#define BOARD_MASK 65535
#define BOARD_SIZE 65536

#define FIFO_SIZE 1024
#define FIFO_SIZE_HALF (FIFO_SIZE / 2)

struct toccata_data {
	bool enabled;
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

	int streamid;
	int ch_sample[2];

	int fifo_half;
	int toccata_active;
	int left_volume, right_volume;

	int freq, freq_adjusted, channels, samplebits;
	int event_time, record_event_time;
	int record_event_counter;
	int bytespersample;

	struct romconfig *rc;
};

static struct toccata_data toccata[MAX_DUPLICATE_SOUND_BOARDS];

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
	struct toccata_data *data = &toccata[0];
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

static bool audio_state_sndboard_toccata(int streamid)
{
	struct toccata_data *data = &toccata[0];
	if (!toccata[0].toccata_active)
		return false;
	if (data->streamid != streamid)
		return false;
	if ((data->toccata_active & STATUS_FIFO_PLAY)) {
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
	audio_state_stream_state(data->streamid, data->ch_sample, 2, data->event_time);
	return true;
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
	struct toccata_data *data = &toccata[0];
	data->left_volume = (100 - currprefs.sound_volume_board) * 32768 / 100;
	data->right_volume = (100 - currprefs.sound_volume_board) * 32768 / 100;

	data->left_volume = get_volume(data->ad1848_regs[6]) * data->left_volume / 32768;
	data->right_volume = get_volume(data->ad1848_regs[7]) * data->right_volume / 32768;

	if (data->rc->device_settings & 1) {
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
	struct toccata_data *data = &toccata[0];
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
	struct toccata_data *data = &toccata[0];
	data->toccata_active  = (data->ad1848_regs[9] & 1) ? STATUS_FIFO_PLAY : 0;
	data->toccata_active |= (data->ad1848_regs[9] & 2) ? STATUS_FIFO_RECORD : 0;

	codec_setup();

	data->event_time = base_event_clock * CYCLE_UNIT / data->freq;
	data->record_event_time = base_event_clock * CYCLE_UNIT / (data->freq_adjusted * data->bytespersample);
	data->record_event_counter = 0;

	if (data->toccata_active & STATUS_FIFO_PLAY) {
		data->streamid = audio_enable_stream(true, -1, 2, audio_state_sndboard_toccata);
	}
	if (data->toccata_active & STATUS_FIFO_RECORD) {
		capture_buffer = xcalloc(uae_u8, capture_buffer_size);
		sndboard_init_capture(data->freq_adjusted);
	}
}

static void codec_stop(void)
{
	struct toccata_data *data = &toccata[0];
	write_log(_T("TOCCATA stop\n"));
	data->toccata_active = 0;
	sndboard_free_capture();
	audio_enable_stream(false, data->streamid, 0, NULL);
	data->streamid = 0;
	xfree(capture_buffer);
	capture_buffer = NULL;
}

void sndboard_rethink(void)
{
	bool irq = false;
	if (toccata[0].enabled) {
		struct toccata_data *data = &toccata[0];
		irq = data->toccata_irq != 0;
	}
	if (uaesndboard[0].enabled) {
		irq |= uaesnd_rethink();
	}
	if (irq) {
		atomic_or(&uae_int_requested, 0x200);
		set_special_exter(SPCFLAG_UAEINT);
	} else {
		atomic_and(&uae_int_requested, ~0x200);
	}
}

static void sndboard_process_capture(void)
{
	struct toccata_data *data = &toccata[0];
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
	struct toccata_data *data = &toccata[0];
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
		
		//write_log(_T("%d %d %d %d\n"), capture_read_index, capture_write_index, size, bytes);

		if (data->data_in_record_fifo > FIFO_SIZE_HALF && oldfifo <= FIFO_SIZE_HALF) {
			data->fifo_half |= STATUS_FIFO_RECORD;
			//audio_state_sndboard(-1, -1);
		}
		data->record_event_counter -= oldbytes * data->record_event_time;
	}
}

static void sndboard_vsync_toccata(void)
{
	struct toccata_data *data = &toccata[0];
	if (data->toccata_active) {
		calculate_volume_toccata();
		audio_activate();
	}
}

static void toccata_put(uaecptr addr, uae_u8 v)
{
	struct toccata_data *data = &toccata[0];
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
	struct toccata_data *data = &toccata[0];
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
	struct toccata_data *data = &toccata[0];
	b &= 0xff;
	addr &= BOARD_MASK;
	if (!data->configured) {
		switch (addr)
		{
			case 0x48:
			map_banks_z2(&toccata_bank, expamem_board_pointer >> 16, BOARD_SIZE >> 16);
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
	struct toccata_data *data = &toccata[0];
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
	default_xlate, default_check, NULL, _T("*"), _T("Toccata"),
	dummy_lgeti, dummy_wgeti,
	ABFLAG_IO, S_READ, S_WRITE
};

bool sndboard_init(struct autoconfig_info *aci)
{
	const struct expansionromtype *ert = get_device_expansion_rom(ROMTYPE_TOCCATA);
	if (!ert)
		return false;

	aci->addrbank = &toccata_bank;

	if (!aci->doinit) {
		aci->autoconfigp = ert->autoconfig;
		return true;
	}

	struct toccata_data *data = &toccata[0];
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

	data->configured = 0;
	data->streamid = 0;
	memset(data->acmemory, 0xff, sizeof data->acmemory);
	data->rc = aci->rc;
	data->enabled = true;
	for (int i = 0; i < 16; i++) {
		uae_u8 b = ert->autoconfig[i];
		ew(data->acmemory, i * 4, b);
	}
	mapped_malloc(&toccata_bank);
	calculate_volume_toccata();
	return true;
}

void sndboard_free(void)
{
	struct toccata_data *data = &toccata[0];
	data->enabled = false;
	data->toccata_irq = 0;
	data->rc = NULL;
	sndboard_rethink();
	mapped_free(&toccata_bank);
}

void sndboard_reset(void)
{
	struct toccata_data *data = &toccata[0];
	data->ch_sample[0] = 0;
	data->ch_sample[1] = 0;
	audio_enable_stream(false, data->streamid, 0, NULL);
	data->streamid = 0;
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
	int streamid;
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
	audio_enable_stream(false, data->streamid, 0, NULL);
	data->streamid = 0;
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
		data->pcibs->irq_callback(data->pcibs, true);
	} else {
		data->pcibs->irq_callback(data->pcibs, false);
	}
}

static bool audio_state_sndboard_fm801(int streamid)
{
	struct fm801_data *data = &fm801;

	if (!fm801_active)
		return false;
	if (data->streamid != streamid)
		return false;
	if (data->play_on) {
		uae_u8 sample[2 * 6] = { 0 };
		pci_read_dma(data->pcibs, data->play_dma2[data->dmach], sample, data->bytesperframe);
		for (int i = 0; i < data->ch; i++) {
			int smp, vol;
			if (data->bits == 8)
				smp = (sample[i] << 8) | (sample[i]);
			else
				smp = (sample[i * 2 + 1] << 8) | sample[i *2 + 0];
			if (i == 0 || i == 4)
				vol = data->left_volume;
			else if (i == 1 || i == 5)
				vol = data->right_volume;
			else
				vol = (data->left_volume + data->right_volume) / 2;
			data->ch_sample[i] = smp * vol / 32768;
		}
		data->play_len2 -= data->bytesperframe;
		data->play_dma2[data->dmach] += data->bytesperframe;
		if (data->play_len2 == 0xffff) {
			fm801_swap_buffers(data);
			data->interrupt_status |= 0x100;
			fm801_interrupt(data);
		}
	}
	audio_state_stream_state(data->streamid, data->ch_sample, data->ch, data->event_time);
	return true;
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

	data->streamid = audio_enable_stream(true, -1, data->ch, audio_state_sndboard_fm801);
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
		break;
		case 0x10:
		v = data->play_dma2[data->dmach];
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
	&fm801_pci_config, fm801_init, fm801_free, fm801_reset, fm801_hsync_handler,
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
	&fm801_pci_config_func1, NULL, NULL, NULL, NULL,
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
	&solo1_pci_config, solo1_init, solo1_free, solo1_reset, NULL,
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

static bool audio_state_sndboard_qemu(int streamid)
{
	SWVoiceOut *out = qemu_voice_out;

	if (!out || !out->active)
		return false;
	if (streamid != out->streamid)
		return false;
	if (out->active) {
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
	audio_state_stream_state(out->streamid, out->ch_sample, out->ch, out->event_time);
	return true;
}

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
	audio_enable_stream(false, sw->streamid, 2, NULL);
	sw->streamid = 0;
	if (on) {
		sw->streamid = audio_enable_stream(true, -1, 2, audio_state_sndboard_qemu);
	}
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
	if (sw) {
		audio_enable_stream(false, sw->streamid, 0, NULL);
		sw->streamid = 0;
		xfree(sw);
	}
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

	TCHAR *name2 = au(name);
	write_log(_T("QEMU AUDIO: freq=%d ch=%d bits=%d (fmt=%d) '%s'\n"), out->freq, out->ch, bits, settings->fmt, name2);
	xfree(name2);

	qemu_voice_out = out;

	return out;
}

static void sndboard_vsync_qemu(void)
{
	audio_activate();
}

void sndboard_vsync(void)
{
	if (toccata[0].toccata_active)
		sndboard_vsync_toccata();
	if (fm801_active)
		sndboard_vsync_fm801();
	if (qemu_voice_out && qemu_voice_out->active)
		sndboard_vsync_qemu();
}

void sndboard_ext_volume(void)
{
	if (toccata[0].toccata_active)
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
