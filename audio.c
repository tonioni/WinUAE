 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Paula audio emulation
  *
  * Copyright 1995, 1996, 1997 Bernd Schmidt
  * Copyright 1996 Marcus Sundberg
  * Copyright 1996 Manfred Thole
  * Copyright 2006 Toni Wilen
  *
  * new filter algorithm and anti&sinc interpolators by Antti S. Lankila
  *
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "autoconf.h"
#include "gensound.h"
#include "audio.h"
#include "sounddep/sound.h"
#include "events.h"
#include "savestate.h"
#include "driveclick.h"
#include "zfile.h"
#include "uae.h"
#include "gui.h"
#ifdef AVIOUTPUT
#include "avioutput.h"
#endif
#ifdef AHI
#include "traps.h"
#include "ahidsound.h"
#include "ahidsound_new.h"
#endif
#include "threaddep/thread.h"

#include <math.h>

#define MAX_EV ~0ul
//#define DEBUG_AUDIO
#define DEBUG_CHANNEL_MASK 15

int audio_channel_mask = 15;

STATIC_INLINE int isaudio(void)
{
    if (!currprefs.produce_sound)
	return 0;
    return 1;
}

static int debugchannel (int ch)
{
    if ((1 << ch) & DEBUG_CHANNEL_MASK)
	return 1;
    return 0;
}

#define SINC_QUEUE_MAX_AGE 2048
/* Queue length 128 implies minimum emulated period of 16. I add a few extra
 * entries so that CPU updates during minimum period can be played back. */
#define SINC_QUEUE_LENGTH (SINC_QUEUE_MAX_AGE / 16 + 2)

#include "sinctable.c"

typedef struct {
    int age, output;
} sinc_queue_t;

struct audio_channel_data {
    unsigned long adk_mask;
    unsigned long evtime;
    uae_u8 dmaen, intreq2;
    uaecptr lc, pt;
    int current_sample, last_sample;
    int *voltbl;
    int state;
    int per;
    int vol;
    int len, wlen;
    uae_u16 dat, dat2;
    int request_word, request_word_skip;
    int vpos;
    int sample_accum, sample_accum_time;
    int sinc_output_state;
    sinc_queue_t sinc_queue[SINC_QUEUE_LENGTH];
    int sinc_queue_length;
};

int sampleripper_enabled;
struct ripped_sample
{
    struct ripped_sample *next;
    uae_u8 *sample;
    int len, per, changed;
};

static struct ripped_sample *ripped_samples;

void write_wavheader (struct zfile *wavfile, uae_u32 size, uae_u32 freq)
{
    uae_u16 tw;
    uae_u32 tl;
    int bits = 8, channels = 1;

    zfile_fseek (wavfile, 0, SEEK_SET);
    zfile_fwrite ("RIFF", 1, 4, wavfile);
    tl = 0;
    if (size)
	tl = size - 8;
    zfile_fwrite (&tl, 1, 4, wavfile);
    zfile_fwrite ("WAVEfmt ", 1, 8, wavfile);
    tl = 16;
    zfile_fwrite (&tl, 1, 4, wavfile);
    tw = 1;
    zfile_fwrite (&tw, 1, 2, wavfile);
    tw = channels;
    zfile_fwrite (&tw, 1, 2, wavfile);
    tl = freq;
    zfile_fwrite (&tl, 1, 4, wavfile);
    tl = freq * channels * bits / 8;
    zfile_fwrite (&tl, 1, 4, wavfile);
    tw = channels * bits / 8;
    zfile_fwrite (&tw, 1, 2, wavfile);
    tw = bits;
    zfile_fwrite (&tw, 1, 2, wavfile);
    zfile_fwrite ("data", 1, 4, wavfile);
    tl = 0;
    if (size)
	tl = size - 44;
    zfile_fwrite (&tl, 1, 4, wavfile);
}

static void convertsample(uae_u8 *sample, int len)
{
    int i;
    for (i = 0; i < len; i++)
	sample[i] += 0x80;
}

static void namesplit (TCHAR *s)
{
    int l;

    l = _tcslen (s) - 1;
    while (l >= 0) {
	if (s[l] == '.')
	    s[l] = 0;
	if (s[l] == '\\' || s[l] == '/' || s[l] == ':' || s[l] == '?') {
	    l++;
	    break;
	}
	l--;
    }
    if (l > 0)
	memmove (s, s + l, (_tcslen (s + l) + 1) * sizeof (TCHAR));
}

void audio_sampleripper (int mode)
{
    struct ripped_sample *rs = ripped_samples;
    int cnt = 1;
    TCHAR path[MAX_DPATH], name[MAX_DPATH], filename[MAX_DPATH];
    TCHAR underline[] = L"_";
    TCHAR extension[4];
    struct zfile *wavfile;

    if (mode < 0) {
	while (rs) {
	    struct ripped_sample *next = rs->next;
	    xfree(rs);
	    rs = next;
	}
	ripped_samples = NULL;
	return;
    }

    while (rs) {
	if (rs->changed) {
	    rs->changed = 0;
	    fetch_ripperpath (path, sizeof (path));
	    name[0] = 0;
	    if (currprefs.dfxtype[0] >= 0)
		_tcscpy (name, currprefs.df[0]);
	    if (!name[0])
		underline[0] = 0;
	    namesplit (name);
	    _tcscpy (extension, L"wav");
	    _stprintf (filename, L"%s%s%s%03.3d.%s", path, name, underline, cnt, extension);
	    wavfile = zfile_fopen (filename, L"wb", 0);
	    if (wavfile) {
		int freq = rs->per > 0 ? (currprefs.ntscmode ? 3579545 : 3546895 / rs->per) : 8000;
		write_wavheader (wavfile, 0, 0);
		convertsample (rs->sample, rs->len);
		zfile_fwrite (rs->sample, rs->len, 1, wavfile);
		convertsample (rs->sample, rs->len);
		write_wavheader (wavfile, zfile_ftell(wavfile), freq);
		zfile_fclose (wavfile);
		write_log (L"SAMPLERIPPER: %d: %dHz %d bytes\n", cnt, freq, rs->len);
	    } else {
		write_log (L"SAMPLERIPPER: failed to open '%s'\n", filename);
	    }
	}
	cnt++;
	rs = rs->next;
    }
}

static void do_samplerip (struct audio_channel_data *adp)
{
    struct ripped_sample *rs = ripped_samples, *prev;
    int len = adp->len * 2;
    uae_u8 *smp = chipmem_bank.xlateaddr (adp->pt);
    int cnt = 0, i;

    if (!smp || !chipmem_bank.check (adp->pt, len))
	return;
    for (i = 0; i < len; i++) {
	if (smp[i] != 0)
	    break;
    }
    if (i == len || len <= 2)
	return;
    prev = NULL;
    while(rs) {
	if (rs->sample) {
	    if (len == rs->len && !memcmp (rs->sample, smp, len))
		break;
	    /* replace old identical but shorter sample */
	    if (len > rs->len && !memcmp (rs->sample, smp, rs->len)) {
		xfree (rs->sample);
		rs->sample = xmalloc (len);
		memcpy (rs->sample, smp, len);
		write_log (L"SAMPLERIPPER: replaced sample %d (%d -> %d)\n", cnt, rs->len, len);
		rs->len = len;
		rs->per = adp->per / CYCLE_UNIT;
		rs->changed = 1;
		audio_sampleripper (0);
		return;
	    }
	}
	prev = rs;
	rs = rs->next;
	cnt++;
    }
    if (rs || cnt > 100)
	return;
    rs = xmalloc(sizeof(struct ripped_sample));
    if (prev)
	prev->next = rs;
    else
	ripped_samples = rs;
    rs->len = len;
    rs->per = adp->per / CYCLE_UNIT;
    rs->sample = xmalloc (len);
    memcpy(rs->sample, smp, len);
    rs->next = NULL;
    rs->changed = 1;
    write_log (L"SAMPLERIPPER: sample added (%06X, %d bytes), total %d samples\n", adp->pt, len, ++cnt);
    audio_sampleripper (0);
}

static struct audio_channel_data audio_channel[4];
int sound_available = 0;
static int sound_table[64][256];
void (*sample_handler) (void);
static void (*sample_prehandler) (unsigned long best_evtime);

unsigned long sample_evtime, scaled_sample_evtime;

static unsigned long last_cycles, next_sample_evtime;

void init_sound_table16 (void)
{
    int i,j;

    for (i = 0; i < 256; i++)
	for (j = 0; j < 64; j++)
	    sound_table[j][i] = j * (uae_s8)i * get_audio_ismono() ? 1 : 2;
}

void init_sound_table8 (void)
{
    int i,j;

    for (i = 0; i < 256; i++)
	for (j = 0; j < 64; j++)
	    sound_table[j][i] = (j * (uae_s8)i * get_audio_ismono() ? 1 : 2) / 256;
}

#define	MULTIPLICATION_PROFITABLE

#ifdef MULTIPLICATION_PROFITABLE
typedef uae_s8 sample8_t;
#define DO_CHANNEL_1(v, c) do { (v) *= audio_channel[c].vol; } while (0)
#define SBASEVAL16(logn) ((logn) == 1 ? SOUND16_BASE_VAL >> 1 : SOUND16_BASE_VAL)
#define FINISH_DATA(data, b, logn) do { if (14 - (b) + (logn) > 0) (data) >>= 14 - (b) + (logn); else (data) <<= (b) - 14 - (logn); } while (0);
#else
typedef uae_u8 sample8_t;
#define DO_CHANNEL_1(v, c) do { (v) = audio_channel[c].voltbl[(v)]; } while (0)
#define SBASEVAL16(logn) SOUND16_BASE_VAL
#define FINISH_DATA(data, b, logn)
#endif

static uae_u32 right_word_saved[SOUND_MAX_DELAY_BUFFER];
static uae_u32 left_word_saved[SOUND_MAX_DELAY_BUFFER];
static uae_u32 right2_word_saved[SOUND_MAX_DELAY_BUFFER];
static uae_u32 left2_word_saved[SOUND_MAX_DELAY_BUFFER];
static int saved_ptr, saved_ptr2;

static int mixed_on, mixed_stereo_size, mixed_mul1, mixed_mul2;
static int led_filter_forced, sound_use_filter, sound_use_filter_sinc, led_filter_on;

/* denormals are very small floating point numbers that force FPUs into slow
   mode. All lowpass filters using floats are suspectible to denormals unless
   a small offset is added to avoid very small floating point numbers. */
#define DENORMAL_OFFSET (1E-10)

static struct filter_state {
    float rc1, rc2, rc3, rc4, rc5;
} sound_filter_state[4];

static float a500e_filter1_a0;
static float a500e_filter2_a0;
static float filter_a0; /* a500 and a1200 use the same */

enum {
  FILTER_NONE = 0,
  FILTER_MODEL_A500,
  FILTER_MODEL_A1200
};

/* Amiga has two separate filtering circuits per channel, a static RC filter
 * on A500 and the LED filter. This code emulates both.
 *
 * The Amiga filtering circuitry depends on Amiga model. Older Amigas seem
 * to have a 6 dB/oct RC filter with cutoff frequency such that the -6 dB
 * point for filter is reached at 6 kHz, while newer Amigas have no filtering.
 *
 * The LED filter is complicated, and we are modelling it with a pair of
 * RC filters, the other providing a highboost. The LED starts to cut
 * into signal somewhere around 5-6 kHz, and there's some kind of highboost
 * in effect above 12 kHz. Better measurements are required.
 *
 * The current filtering should be accurate to 2 dB with the filter on,
 * and to 1 dB with the filter off.
*/

static int filter(int input, struct filter_state *fs)
{
    int o;
    float normal_output, led_output;

    input = (uae_s16)input;
    switch (sound_use_filter) {

    case FILTER_NONE:
	return input;
    case FILTER_MODEL_A500:
	fs->rc1 = a500e_filter1_a0 * input + (1 - a500e_filter1_a0) * fs->rc1 + DENORMAL_OFFSET;
	fs->rc2 = a500e_filter2_a0 * fs->rc1 + (1-a500e_filter2_a0) * fs->rc2;
	normal_output = fs->rc2;

	fs->rc3 = filter_a0 * normal_output + (1 - filter_a0) * fs->rc3;
	fs->rc4 = filter_a0 * fs->rc3       + (1 - filter_a0) * fs->rc4;
	fs->rc5 = filter_a0 * fs->rc4       + (1 - filter_a0) * fs->rc5;

	led_output = fs->rc5;
	break;

    case FILTER_MODEL_A1200:
	normal_output = input;

	fs->rc2 = filter_a0 * normal_output + (1 - filter_a0) * fs->rc2 + DENORMAL_OFFSET;
	fs->rc3 = filter_a0 * fs->rc2       + (1 - filter_a0) * fs->rc3;
	fs->rc4 = filter_a0 * fs->rc3       + (1 - filter_a0) * fs->rc4;

	led_output = fs->rc4;
	break;

    }

    if (led_filter_on)
	o = led_output;
    else
	o = normal_output;

    if (o > 32767)
	o = 32767;
    else if (o < -32768)
	o = -32768;

    return o;
}

/* Always put the right word before the left word.  */

STATIC_INLINE void put_sound_word_right (uae_u32 w)
{
    if (mixed_on) {
	right_word_saved[saved_ptr] = w;
	return;
    }
    PUT_SOUND_WORD_RIGHT (w);
}

STATIC_INLINE void put_sound_word_left (uae_u32 w)
{
    if (mixed_on) {
	uae_u32 rold, lold, rnew, lnew, tmp;

	left_word_saved[saved_ptr] = w;
	lnew = w - SOUND16_BASE_VAL;
	rnew = right_word_saved[saved_ptr] - SOUND16_BASE_VAL;

	saved_ptr = (saved_ptr + 1) & mixed_stereo_size;

	lold = left_word_saved[saved_ptr] - SOUND16_BASE_VAL;
	tmp = (rnew * mixed_mul2 + lold * mixed_mul1) / MIXED_STEREO_SCALE;
	tmp += SOUND16_BASE_VAL;
	PUT_SOUND_WORD_RIGHT (tmp);

	rold = right_word_saved[saved_ptr] - SOUND16_BASE_VAL;
	w = (lnew * mixed_mul2 + rold * mixed_mul1) / MIXED_STEREO_SCALE;
    }
    PUT_SOUND_WORD_LEFT (w);
}

STATIC_INLINE void put_sound_word_right2 (uae_u32 w)
{
    if (mixed_on) {
	right2_word_saved[saved_ptr2] = w;
	return;
    }
    PUT_SOUND_WORD_RIGHT2 (w);
}

STATIC_INLINE void put_sound_word_left2 (uae_u32 w)
{
    if (mixed_on) {
	uae_u32 rold, lold, rnew, lnew, tmp;

	left2_word_saved[saved_ptr2] = w;
	lnew = w - SOUND16_BASE_VAL;
	rnew = right2_word_saved[saved_ptr2] - SOUND16_BASE_VAL;

	saved_ptr2 = (saved_ptr2 + 1) & mixed_stereo_size;

	lold = left2_word_saved[saved_ptr2] - SOUND16_BASE_VAL;
	tmp = (rnew * mixed_mul2 + lold * mixed_mul1) / MIXED_STEREO_SCALE;
	tmp += SOUND16_BASE_VAL;
	PUT_SOUND_WORD_RIGHT2 (tmp);

	rold = right2_word_saved[saved_ptr2] - SOUND16_BASE_VAL;
	w = (lnew * mixed_mul2 + rold * mixed_mul1) / MIXED_STEREO_SCALE;
    }
    PUT_SOUND_WORD_LEFT2 (w);
}


#define	DO_CHANNEL(v, c) do { (v) &= audio_channel[c].adk_mask; data += v; } while (0);

static void anti_prehandler (unsigned long best_evtime)
{
    int i, output;
    struct audio_channel_data *acd;

    /* Handle accumulator antialiasiation */
    for (i = 0; i < 4; i++) {
	acd = &audio_channel[i];
	output = (acd->current_sample * acd->vol) & acd->adk_mask;
	acd->sample_accum += output * best_evtime;
	acd->sample_accum_time += best_evtime;
    }
}

STATIC_INLINE void samplexx_anti_handler (int *datasp)
{
    int i;
    for (i = 0; i < 4; i++) {
	datasp[i] = audio_channel[i].sample_accum_time ? (audio_channel[i].sample_accum / audio_channel[i].sample_accum_time) : 0;
	audio_channel[i].sample_accum = 0;
	audio_channel[i].sample_accum_time = 0;

    }
}

static void sinc_prehandler (unsigned long best_evtime)
{
    int i, j, output;
    struct audio_channel_data *acd;

    for (i = 0; i < 4; i++) {
	acd = &audio_channel[i];
	output = (acd->current_sample * acd->vol) & acd->adk_mask;

	/* age the sinc queue and truncate it when necessary */
	for (j = 0; j < acd->sinc_queue_length; j += 1) {
	    acd->sinc_queue[j].age += best_evtime;
	    if (acd->sinc_queue[j].age >= SINC_QUEUE_MAX_AGE) {
		acd->sinc_queue_length = j;
		break;
	    }
	}

	/* if output state changes, record the state change and also
	 * write data into sinc queue for mixing in the BLEP */
	if (acd->sinc_output_state != output) {
	    if (acd->sinc_queue_length > SINC_QUEUE_LENGTH - 1) {
		//write_log (L"warning: sinc queue truncated. Last age: %d.\n", acd->sinc_queue[SINC_QUEUE_LENGTH-1].age);
		acd->sinc_queue_length = SINC_QUEUE_LENGTH - 1;
	    }
	    /* make room for new and add the new value */
	    memmove (&acd->sinc_queue[1], &acd->sinc_queue[0],
		     sizeof(acd->sinc_queue[0]) * acd->sinc_queue_length);
	    acd->sinc_queue_length += 1;
	    acd->sinc_queue[0].age = best_evtime;
	    acd->sinc_queue[0].output = output - acd->sinc_output_state;
	    acd->sinc_output_state = output;
	}
    }
}


/* this interpolator performs BLEP mixing (bleps are shaped like integrated sinc
 * functions) with a type of BLEP that matches the filtering configuration. */
STATIC_INLINE void samplexx_sinc_handler (int *datasp)
{
    int i, n;
    int const *winsinc;

    if (sound_use_filter_sinc) {
	n = (sound_use_filter_sinc == FILTER_MODEL_A500) ? 0 : 2;
	if (led_filter_on)
	    n += 1;
    } else {
	n = 4;
    }
    winsinc = winsinc_integral[n];

    for (i = 0; i < 4; i += 1) {
	int j, v;
	struct audio_channel_data *acd = &audio_channel[i];
	/* The sum rings with harmonic components up to infinity... */
	int sum = acd->sinc_output_state << 17;
	/* ...but we cancel them through mixing in BLEPs instead */
	for (j = 0; j < acd->sinc_queue_length; j += 1)
	    sum -= winsinc[acd->sinc_queue[j].age] * acd->sinc_queue[j].output;
	v = sum >> 17;
	if (v > 32767)
	    v = 32767;
	else if (v < -32768)
	    v = -32768;
	datasp[i] = v;
    }
}

static void sample16i_sinc_handler (void)
{
    int datas[4], data1;

    samplexx_sinc_handler (datas);
    data1 = datas[0] + datas[3] + datas[1] + datas[2];
    FINISH_DATA (data1, 16, 2);
    PUT_SOUND_WORD_MONO (data1);
    check_sound_buffers ();
}

void sample16_handler (void)
{
    uae_u32 data0 = audio_channel[0].current_sample;
    uae_u32 data1 = audio_channel[1].current_sample;
    uae_u32 data2 = audio_channel[2].current_sample;
    uae_u32 data3 = audio_channel[3].current_sample;
    DO_CHANNEL_1 (data0, 0);
    DO_CHANNEL_1 (data1, 1);
    DO_CHANNEL_1 (data2, 2);
    DO_CHANNEL_1 (data3, 3);
    data0 &= audio_channel[0].adk_mask;
    data1 &= audio_channel[1].adk_mask;
    data2 &= audio_channel[2].adk_mask;
    data3 &= audio_channel[3].adk_mask;
    data0 += data1;
    data0 += data2;
    data0 += data3;
    {
	uae_u32 data = SBASEVAL16(2) + data0;
	FINISH_DATA (data, 16, 2);
	PUT_SOUND_WORD_MONO (data);
    }
    check_sound_buffers ();
}

/* This interpolator examines sample points when Paula switches the output
 * voltage and computes the average of Paula's output */
static void sample16i_anti_handler (void)
{
    int datas[4], data1;

    samplexx_anti_handler (datas);
    data1 = datas[0] + datas[3] + datas[1] + datas[2];
    FINISH_DATA (data1, 16, 2);
    PUT_SOUND_WORD_MONO (data1);
    check_sound_buffers ();
}

static void sample16i_rh_handler (void)
{
    unsigned long delta, ratio;

    uae_u32 data0 = audio_channel[0].current_sample;
    uae_u32 data1 = audio_channel[1].current_sample;
    uae_u32 data2 = audio_channel[2].current_sample;
    uae_u32 data3 = audio_channel[3].current_sample;
    uae_u32 data0p = audio_channel[0].last_sample;
    uae_u32 data1p = audio_channel[1].last_sample;
    uae_u32 data2p = audio_channel[2].last_sample;
    uae_u32 data3p = audio_channel[3].last_sample;
    DO_CHANNEL_1 (data0, 0);
    DO_CHANNEL_1 (data1, 1);
    DO_CHANNEL_1 (data2, 2);
    DO_CHANNEL_1 (data3, 3);
    DO_CHANNEL_1 (data0p, 0);
    DO_CHANNEL_1 (data1p, 1);
    DO_CHANNEL_1 (data2p, 2);
    DO_CHANNEL_1 (data3p, 3);

    data0 &= audio_channel[0].adk_mask;
    data0p &= audio_channel[0].adk_mask;
    data1 &= audio_channel[1].adk_mask;
    data1p &= audio_channel[1].adk_mask;
    data2 &= audio_channel[2].adk_mask;
    data2p &= audio_channel[2].adk_mask;
    data3 &= audio_channel[3].adk_mask;
    data3p &= audio_channel[3].adk_mask;

    /* linear interpolation and summing up... */
    delta = audio_channel[0].per;
    ratio = ((audio_channel[0].evtime % delta) << 8) / delta;
    data0 = (data0 * (256 - ratio) + data0p * ratio) >> 8;
    delta = audio_channel[1].per;
    ratio = ((audio_channel[1].evtime % delta) << 8) / delta;
    data0 += (data1 * (256 - ratio) + data1p * ratio) >> 8;
    delta = audio_channel[2].per;
    ratio = ((audio_channel[2].evtime % delta) << 8) / delta;
    data0 += (data2 * (256 - ratio) + data2p * ratio) >> 8;
    delta = audio_channel[3].per;
    ratio = ((audio_channel[3].evtime % delta) << 8) / delta;
    data0 += (data3 * (256 - ratio) + data3p * ratio) >> 8;
    {
	uae_u32 data = SBASEVAL16(2) + data0;
	FINISH_DATA (data, 16, 2);
	PUT_SOUND_WORD_MONO (data);
    }
    check_sound_buffers ();
}

static void sample16i_crux_handler (void)
{
    uae_u32 data0 = audio_channel[0].current_sample;
    uae_u32 data1 = audio_channel[1].current_sample;
    uae_u32 data2 = audio_channel[2].current_sample;
    uae_u32 data3 = audio_channel[3].current_sample;
    uae_u32 data0p = audio_channel[0].last_sample;
    uae_u32 data1p = audio_channel[1].last_sample;
    uae_u32 data2p = audio_channel[2].last_sample;
    uae_u32 data3p = audio_channel[3].last_sample;
    DO_CHANNEL_1 (data0, 0);
    DO_CHANNEL_1 (data1, 1);
    DO_CHANNEL_1 (data2, 2);
    DO_CHANNEL_1 (data3, 3);
    DO_CHANNEL_1 (data0p, 0);
    DO_CHANNEL_1 (data1p, 1);
    DO_CHANNEL_1 (data2p, 2);
    DO_CHANNEL_1 (data3p, 3);

    data0 &= audio_channel[0].adk_mask;
    data0p &= audio_channel[0].adk_mask;
    data1 &= audio_channel[1].adk_mask;
    data1p &= audio_channel[1].adk_mask;
    data2 &= audio_channel[2].adk_mask;
    data2p &= audio_channel[2].adk_mask;
    data3 &= audio_channel[3].adk_mask;
    data3p &= audio_channel[3].adk_mask;

    {
	struct audio_channel_data *cdp;
	unsigned long ratio, ratio1;
#define INTERVAL (scaled_sample_evtime * 3)
	cdp = audio_channel + 0;
	ratio1 = cdp->per - cdp->evtime;
	ratio = (ratio1 << 12) / INTERVAL;
	if (cdp->evtime < scaled_sample_evtime || ratio1 >= INTERVAL)
	    ratio = 4096;
	data0 = (data0 * ratio + data0p * (4096 - ratio)) >> 12;

	cdp = audio_channel + 1;
	ratio1 = cdp->per - cdp->evtime;
	ratio = (ratio1 << 12) / INTERVAL;
	if (cdp->evtime < scaled_sample_evtime || ratio1 >= INTERVAL)
	    ratio = 4096;
	data1 = (data1 * ratio + data1p * (4096 - ratio)) >> 12;

	cdp = audio_channel + 2;
	ratio1 = cdp->per - cdp->evtime;
	ratio = (ratio1 << 12) / INTERVAL;
	if (cdp->evtime < scaled_sample_evtime || ratio1 >= INTERVAL)
	    ratio = 4096;
	data2 = (data2 * ratio + data2p * (4096 - ratio)) >> 12;

	cdp = audio_channel + 3;
	ratio1 = cdp->per - cdp->evtime;
	ratio = (ratio1 << 12) / INTERVAL;
	if (cdp->evtime < scaled_sample_evtime || ratio1 >= INTERVAL)
	    ratio = 4096;
	data3 = (data3 * ratio + data3p * (4096 - ratio)) >> 12;
    }
    data1 += data2;
    data0 += data3;
    data0 += data1;
    {
	uae_u32 data = SBASEVAL16(2) + data0;
	FINISH_DATA (data, 16, 2);
	PUT_SOUND_WORD_MONO (data);
    }
    check_sound_buffers ();
}

#ifdef HAVE_STEREO_SUPPORT

static void make6ch (uae_s32 d0, uae_s32 d1, uae_s32 d2, uae_s32 d3)
{
    uae_s32 sum = d0 + d1 + d2 + d3;
    PUT_SOUND_WORD (sum >> 1);
    PUT_SOUND_WORD (sum >> 1);
}

void sample16ss_handler (void)
{
    uae_u32 data0 = audio_channel[0].current_sample;
    uae_u32 data1 = audio_channel[1].current_sample;
    uae_u32 data2 = audio_channel[2].current_sample;
    uae_u32 data3 = audio_channel[3].current_sample;
    DO_CHANNEL_1 (data0, 0);
    DO_CHANNEL_1 (data1, 1);
    DO_CHANNEL_1 (data2, 2);
    DO_CHANNEL_1 (data3, 3);

    data0 &= audio_channel[0].adk_mask;
    data1 &= audio_channel[1].adk_mask;
    data2 &= audio_channel[2].adk_mask;
    data3 &= audio_channel[3].adk_mask;

    put_sound_word_left (data0 << 2);
    put_sound_word_right (data1 << 2);
    if (currprefs.sound_stereo == SND_6CH)
	make6ch (data0, data1, data2, data3);
    put_sound_word_left2 (data3 << 2);
    put_sound_word_right2 (data2 << 2);

    check_sound_buffers ();
}

/* This interpolator examines sample points when Paula switches the output
 * voltage and computes the average of Paula's output */

void sample16ss_anti_handler (void)
{
    int datas[4];

    samplexx_anti_handler (datas);
    put_sound_word_left (datas[0] << 2);
    put_sound_word_right (datas[1] << 2);
    if (currprefs.sound_stereo == SND_6CH)
	make6ch (datas[0], datas[1], datas[2], datas[3]);
    put_sound_word_left2 (datas[3] << 2);
    put_sound_word_right2 (datas[2] << 2);
    check_sound_buffers ();
}

static void sample16si_anti_handler (void)
{
    int datas[4], data1, data2;

    samplexx_anti_handler (datas);
    data1 = datas[0] + datas[3];
    data2 = datas[1] + datas[2];
    FINISH_DATA (data1, 16, 1);
    put_sound_word_left (data1);
    FINISH_DATA (data2, 16, 1);
    put_sound_word_right (data2);
    check_sound_buffers ();
}

void sample16ss_sinc_handler (void)
{
    int datas[4];

    samplexx_sinc_handler (datas);
    put_sound_word_left (datas[0] << 2);
    put_sound_word_right (datas[1] << 2);
    if (currprefs.sound_stereo == SND_6CH)
	make6ch (datas[0], datas[1], datas[2], datas[3]);
    put_sound_word_left2 (datas[3] << 2);
    put_sound_word_right2 (datas[2] << 2);
    check_sound_buffers ();
}

static void sample16si_sinc_handler (void)
{
    int datas[4], data1, data2;

    samplexx_sinc_handler (datas);
    data1 = datas[0] + datas[3];
    data2 = datas[1] + datas[2];
    FINISH_DATA (data1, 16, 1);
    put_sound_word_left (data1);
    FINISH_DATA (data2, 16, 1);
    put_sound_word_right (data2);
    check_sound_buffers ();
}

void sample16s_handler (void)
{
    uae_u32 data0 = audio_channel[0].current_sample;
    uae_u32 data1 = audio_channel[1].current_sample;
    uae_u32 data2 = audio_channel[2].current_sample;
    uae_u32 data3 = audio_channel[3].current_sample;
    DO_CHANNEL_1 (data0, 0);
    DO_CHANNEL_1 (data1, 1);
    DO_CHANNEL_1 (data2, 2);
    DO_CHANNEL_1 (data3, 3);

    data0 &= audio_channel[0].adk_mask;
    data1 &= audio_channel[1].adk_mask;
    data2 &= audio_channel[2].adk_mask;
    data3 &= audio_channel[3].adk_mask;

    data0 += data3;
    {
	uae_u32 data = SBASEVAL16(1) + data0;
	FINISH_DATA (data, 16, 1);
	put_sound_word_left (data);
    }

    data1 += data2;
    {
	uae_u32 data = SBASEVAL16(1) + data1;
	FINISH_DATA (data, 16, 1);
	put_sound_word_right (data);
    }

    check_sound_buffers ();
}

static void sample16si_crux_handler (void)
{
    uae_u32 data0 = audio_channel[0].current_sample;
    uae_u32 data1 = audio_channel[1].current_sample;
    uae_u32 data2 = audio_channel[2].current_sample;
    uae_u32 data3 = audio_channel[3].current_sample;
    uae_u32 data0p = audio_channel[0].last_sample;
    uae_u32 data1p = audio_channel[1].last_sample;
    uae_u32 data2p = audio_channel[2].last_sample;
    uae_u32 data3p = audio_channel[3].last_sample;

    DO_CHANNEL_1 (data0, 0);
    DO_CHANNEL_1 (data1, 1);
    DO_CHANNEL_1 (data2, 2);
    DO_CHANNEL_1 (data3, 3);
    DO_CHANNEL_1 (data0p, 0);
    DO_CHANNEL_1 (data1p, 1);
    DO_CHANNEL_1 (data2p, 2);
    DO_CHANNEL_1 (data3p, 3);

    data0 &= audio_channel[0].adk_mask;
    data0p &= audio_channel[0].adk_mask;
    data1 &= audio_channel[1].adk_mask;
    data1p &= audio_channel[1].adk_mask;
    data2 &= audio_channel[2].adk_mask;
    data2p &= audio_channel[2].adk_mask;
    data3 &= audio_channel[3].adk_mask;
    data3p &= audio_channel[3].adk_mask;

    {
	struct audio_channel_data *cdp;
	unsigned long ratio, ratio1;
#define INTERVAL (scaled_sample_evtime * 3)
	cdp = audio_channel + 0;
	ratio1 = cdp->per - cdp->evtime;
	ratio = (ratio1 << 12) / INTERVAL;
	if (cdp->evtime < scaled_sample_evtime || ratio1 >= INTERVAL)
	    ratio = 4096;
	data0 = (data0 * ratio + data0p * (4096 - ratio)) >> 12;

	cdp = audio_channel + 1;
	ratio1 = cdp->per - cdp->evtime;
	ratio = (ratio1 << 12) / INTERVAL;
	if (cdp->evtime < scaled_sample_evtime || ratio1 >= INTERVAL)
	    ratio = 4096;
	data1 = (data1 * ratio + data1p * (4096 - ratio)) >> 12;

	cdp = audio_channel + 2;
	ratio1 = cdp->per - cdp->evtime;
	ratio = (ratio1 << 12) / INTERVAL;
	if (cdp->evtime < scaled_sample_evtime || ratio1 >= INTERVAL)
	    ratio = 4096;
	data2 = (data2 * ratio + data2p * (4096 - ratio)) >> 12;

	cdp = audio_channel + 3;
	ratio1 = cdp->per - cdp->evtime;
	ratio = (ratio1 << 12) / INTERVAL;
	if (cdp->evtime < scaled_sample_evtime || ratio1 >= INTERVAL)
	    ratio = 4096;
	data3 = (data3 * ratio + data3p * (4096 - ratio)) >> 12;
    }
    data1 += data2;
    data0 += data3;
    {
	uae_u32 data = SBASEVAL16(1) + data0;
	FINISH_DATA (data, 16, 1);
	put_sound_word_left (data);
    }

    {
	uae_u32 data = SBASEVAL16(1) + data1;
	FINISH_DATA (data, 16, 1);
	put_sound_word_right (data);
    }
    check_sound_buffers ();
}

static void sample16si_rh_handler (void)
{
    unsigned long delta, ratio;

    uae_u32 data0 = audio_channel[0].current_sample;
    uae_u32 data1 = audio_channel[1].current_sample;
    uae_u32 data2 = audio_channel[2].current_sample;
    uae_u32 data3 = audio_channel[3].current_sample;
    uae_u32 data0p = audio_channel[0].last_sample;
    uae_u32 data1p = audio_channel[1].last_sample;
    uae_u32 data2p = audio_channel[2].last_sample;
    uae_u32 data3p = audio_channel[3].last_sample;

    DO_CHANNEL_1 (data0, 0);
    DO_CHANNEL_1 (data1, 1);
    DO_CHANNEL_1 (data2, 2);
    DO_CHANNEL_1 (data3, 3);
    DO_CHANNEL_1 (data0p, 0);
    DO_CHANNEL_1 (data1p, 1);
    DO_CHANNEL_1 (data2p, 2);
    DO_CHANNEL_1 (data3p, 3);

    data0 &= audio_channel[0].adk_mask;
    data0p &= audio_channel[0].adk_mask;
    data1 &= audio_channel[1].adk_mask;
    data1p &= audio_channel[1].adk_mask;
    data2 &= audio_channel[2].adk_mask;
    data2p &= audio_channel[2].adk_mask;
    data3 &= audio_channel[3].adk_mask;
    data3p &= audio_channel[3].adk_mask;

    /* linear interpolation and summing up... */
    delta = audio_channel[0].per;
    ratio = ((audio_channel[0].evtime % delta) << 8) / delta;
    data0 = (data0 * (256 - ratio) + data0p * ratio) >> 8;
    delta = audio_channel[1].per;
    ratio = ((audio_channel[1].evtime % delta) << 8) / delta;
    data1 = (data1 * (256 - ratio) + data1p * ratio) >> 8;
    delta = audio_channel[2].per;
    ratio = ((audio_channel[2].evtime % delta) << 8) / delta;
    data1 += (data2 * (256 - ratio) + data2p * ratio) >> 8;
    delta = audio_channel[3].per;
    ratio = ((audio_channel[3].evtime % delta) << 8) / delta;
    data0 += (data3 * (256 - ratio) + data3p * ratio) >> 8;
    {
	uae_u32 data = SBASEVAL16(1) + data0;
	FINISH_DATA (data, 16, 1);
	put_sound_word_left (data);
    }

    {
	uae_u32 data = SBASEVAL16(1) + data1;
	FINISH_DATA (data, 16, 1);
	put_sound_word_right (data);
    }
    check_sound_buffers ();
}

#else
void sample16s_handler (void)
{
    sample16_handler ();
}
static void sample16si_crux_handler (void)
{
    sample16i_crux_handler ();
}
static void sample16si_rh_handler (void)
{
    sample16i_rh_handler ();
}
#endif

static int audio_work_to_do;

static void zerostate (struct audio_channel_data *cdp)
{
    cdp->state = 0;
    cdp->evtime = MAX_EV;
    cdp->request_word = 0;
}

static void audio_event_reset (void)
{
    int i;

    last_cycles = get_cycles () - 1;
    next_sample_evtime = scaled_sample_evtime;
    for (i = 0; i < 4; i++) {
	struct audio_channel_data *cdp = audio_channel + i;
	zerostate (cdp);
    }
    schedule_audio ();
    events_schedule ();
}

static void audio_deactivate (void)
{
    if (!currprefs.sound_auto)
	return;
    gui_data.sndbuf_status = 3;
    gui_data.sndbuf = 0;
    reset_sound ();
    clear_sound_buffers ();
    audio_event_reset ();
}

int audio_activate (void)
{
    int ret = 0;

    if (!audio_work_to_do) {
	restart_sound_buffer ();
	ret = 1;
	audio_event_reset();
    }
    audio_work_to_do = 4 * maxvpos * 50;
    return ret;
}

STATIC_INLINE int is_audio_active (void)
{
    return audio_work_to_do;
}

void schedule_audio (void)
{
    unsigned long best = MAX_EV;
    int i;

    eventtab[ev_audio].active = 0;
    eventtab[ev_audio].oldcycles = get_cycles ();
    for (i = 0; i < 4; i++) {
	struct audio_channel_data *cdp = audio_channel + i;
	if (cdp->evtime != MAX_EV) {
	    if (best > cdp->evtime) {
		best = cdp->evtime;
		eventtab[ev_audio].active = 1;
	    }
	}
    }
    eventtab[ev_audio].evtime = get_cycles () + best;
}

static int isirq (int nr)
{
    return INTREQR () & (0x80 << nr);
}

static void setirq (int nr)
{
#ifdef DEBUG_AUDIO
    if (debugchannel (nr))
	write_log (L"SETIRQ %d %08X\n", nr, M68K_GETPC);
#endif
    INTREQ (0x8000 | (0x80 << nr));
}

static void newsample (int nr, sample8_t sample)
{
    struct audio_channel_data *cdp = audio_channel + nr;
#ifdef DEBUG_AUDIO
    if (!debugchannel (nr)) sample = 0;
#endif
    if (!(audio_channel_mask & (1 << nr)))
	sample = 0;
    cdp->last_sample = cdp->current_sample;
    cdp->current_sample = sample;
}

static void state23 (struct audio_channel_data *cdp)
{
    if (!cdp->dmaen)
	return;
    if (cdp->request_word >= 0)
	return;
    cdp->request_word = 0;
    if (cdp->wlen == 1) {
	cdp->wlen = cdp->len;
	cdp->pt = cdp->lc;
	cdp->intreq2 = 1;
	if (sampleripper_enabled)
	    do_samplerip (cdp);
#ifdef DEBUG_AUDIO
	if (debugchannel (cdp - audio_channel))
	    write_log (L"Channel %d looped, LC=%08X LEN=%d\n", cdp - audio_channel, cdp->pt, cdp->wlen);
#endif
    } else {
	cdp->wlen = (cdp->wlen - 1) & 0xFFFF;
    }
}

static void audio_handler (int nr)
{
    struct audio_channel_data *cdp = audio_channel + nr;

    int audav = adkcon & (0x01 << nr);
    int audap = adkcon & (0x10 << nr);
    int napnav = (!audav && !audap) || audav;
    int evtime = cdp->evtime;

    audio_activate ();
    cdp->evtime = MAX_EV;
    switch (cdp->state)
    {
	case 0:
	    cdp->intreq2 = 0;
	    cdp->vpos = vpos;
	    if (cdp->dmaen) {
		cdp->state = 1;
		cdp->wlen = cdp->len;
		/* there are too many stupid sound routines that fail on "too" fast cpus.. */
		if (currprefs.cpu_model >= 68020)
		    cdp->pt = cdp->lc;
#ifdef DEBUG_AUDIO
		if (debugchannel (nr))
		    write_log (L"%d:0>1: LEN=%d\n", nr, cdp->wlen);
#endif
		cdp->request_word = 0;
		cdp->request_word_skip = 0;
		audio_handler (nr);
		return;
	    } else if (!cdp->dmaen && cdp->request_word < 0 && !isirq (nr)) {
		cdp->evtime = 0;
		cdp->state = 2;
		setirq (nr);
		audio_handler (nr);
		return;
	    }
	    cdp->request_word = 0;
	    cdp->request_word_skip = 0;
	return;

	case 1:
	    if (!cdp->dmaen) {
		cdp->state = 0;
		return;
	    }
	    cdp->state = 5;
	    if (cdp->wlen != 1)
		cdp->wlen = (cdp->wlen - 1) & 0xFFFF;
	    cdp->request_word = 2;
	    /* "mysterious" delay for Mission Elevator */
	    //if (cdp->vpos == vpos)
	    if (current_hpos () > maxhpos - 20)
		cdp->request_word_skip = 1;
	return;

	case 5:
	    if (!cdp->request_word) {
		cdp->request_word = 2;
		return;
	    }
	    setirq (nr);
	    if (!cdp->dmaen) {
		cdp->state = 0;
		cdp->request_word = 0;
		return;
	    }
	    cdp->state = 2;
	    cdp->request_word = 3;
	    if (napnav)
		cdp->request_word = 2;
	    cdp->dat = cdp->dat2;
	return;

	case 2:
	    if (currprefs.produce_sound == 0)
		cdp->per = PERIOD_MAX;

	    if (!cdp->dmaen && isirq (nr) && (evtime == 0 || evtime == MAX_EV || evtime == cdp->per)) {
		zerostate (cdp);
		return;
	    }

	    state23 (cdp);
	    cdp->state = 3;
	    cdp->evtime = cdp->per;
	    newsample (nr, (cdp->dat >> 8) & 0xff);
	    cdp->dat <<= 8;
	    /* Period attachment? */
	    if (audap) {
		if (cdp->intreq2 && cdp->dmaen)
		    setirq (nr);
		cdp->intreq2 = 0;
		cdp->request_word = 1;
		cdp->dat = cdp->dat2;
		if (nr < 3) {
		    if (cdp->dat == 0)
			(cdp+1)->per = PERIOD_MAX;
		    else if (cdp->dat < maxhpos * CYCLE_UNIT / 2 && currprefs.produce_sound < 3)
			(cdp+1)->per = maxhpos * CYCLE_UNIT / 2;
		    else
			(cdp+1)->per = cdp->dat * CYCLE_UNIT;
		}
	    }
	return;

	case 3:
	    if (currprefs.produce_sound == 0)
		cdp->per = PERIOD_MAX;
	    state23 (cdp);
	    cdp->state = 2;
	    cdp->evtime = cdp->per;
	    newsample (nr, (cdp->dat >> 8) & 0xff);
	    cdp->dat <<= 8;
	    cdp->dat = cdp->dat2;
	    if (cdp->dmaen) {
		if (napnav)
		    cdp->request_word = 1;
		if (cdp->intreq2 && napnav)
		    setirq (nr);
	    } else {
		if (napnav)
		    setirq (nr);
	    }
	    cdp->intreq2 = 0;

	    /* Volume attachment? */
	    if (audav) {
		if (nr < 3) {
		    (cdp+1)->vol = cdp->dat;
		    #ifndef MULTIPLICATION_PROFITABLE
		    (cdp+1)->voltbl = sound_table[cdp->dat];
		    #endif
		}
	    }
	return;
    }
}

void audio_reset (void)
{
    int i;
    struct audio_channel_data *cdp;

#ifdef AHI
    ahi_close_sound ();
    free_ahi_v2 ();
#endif
    reset_sound ();
    memset(sound_filter_state, 0, sizeof sound_filter_state);
    if (savestate_state != STATE_RESTORE) {
	for (i = 0; i < 4; i++) {
	    cdp = &audio_channel[i];
	    memset (cdp, 0, sizeof *audio_channel);
	    cdp->per = PERIOD_MAX - 1;
	    cdp->voltbl = sound_table[0];
	    cdp->vol = 0;
	    cdp->evtime = MAX_EV;
	}
    } else {
	for (i = 0; i < 4; i++) {
	    cdp = &audio_channel[i];
	    cdp->dmaen = (dmacon & DMA_MASTER) && (dmacon & (1 << i));
	}
    }

#ifndef	MULTIPLICATION_PROFITABLE
    for (i = 0; i < 4; i++)
	audio_channel[i].voltbl = sound_table[audio_channel[i].vol];
#endif

    last_cycles = get_cycles ();
    next_sample_evtime = scaled_sample_evtime;
    schedule_audio ();
    events_schedule ();
}

STATIC_INLINE int sound_prefs_changed (void)
{
    if (changed_prefs.produce_sound != currprefs.produce_sound
	    || changed_prefs.win32_soundcard != currprefs.win32_soundcard
	    || changed_prefs.sound_stereo != currprefs.sound_stereo
	    || changed_prefs.sound_maxbsiz != currprefs.sound_maxbsiz
	    || changed_prefs.sound_freq != currprefs.sound_freq
	    || changed_prefs.sound_auto != currprefs.sound_auto)
	    return 1;

    if (changed_prefs.sound_stereo_separation != currprefs.sound_stereo_separation
	    || changed_prefs.sound_mixed_stereo_delay != currprefs.sound_mixed_stereo_delay
	    || changed_prefs.sound_interpol != currprefs.sound_interpol
	    || changed_prefs.sound_volume != currprefs.sound_volume
	    || changed_prefs.sound_stereo_swap_paula != currprefs.sound_stereo_swap_paula
	    || changed_prefs.sound_stereo_swap_ahi != currprefs.sound_stereo_swap_ahi
	    || changed_prefs.sound_filter != currprefs.sound_filter
	    || changed_prefs.sound_filter_type != currprefs.sound_filter_type)
		return -1;
    return 0;
}

/* This computes the 1st order low-pass filter term b0.
 * The a1 term is 1.0 - b0. The center frequency marks the -3 dB point. */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static float rc_calculate_a0(int sample_rate, int cutoff_freq)
{
    float omega;
    /* The BLT correction formula below blows up if the cutoff is above nyquist. */
    if (cutoff_freq >= sample_rate / 2)
	return 1.0;

    omega = 2 * M_PI * cutoff_freq / sample_rate;
    /* Compensate for the bilinear transformation. This allows us to specify the
     * stop frequency more exactly, but the filter becomes less steep further
     * from stopband. */
    omega = tan(omega / 2) * 2;
    return 1 / (1 + 1 / omega);
}

void check_prefs_changed_audio (void)
{
    int ch;
#ifdef DRIVESOUND
    driveclick_check_prefs ();
#endif
    if (!sound_available)
	return;
    ch = sound_prefs_changed ();
    if (!ch)
	return;
    if (ch > 0) {
#ifdef AVIOUTPUT
	AVIOutput_Restart ();
#endif
	clear_sound_buffers ();
    }
    set_audio ();
    audio_activate();
}

void set_audio (void)
{
    int old_mixed_on = mixed_on;
    int old_mixed_size = mixed_stereo_size;
    int sep, delay;
    int ch;


    ch = sound_prefs_changed ();
    if (ch >= 0)
	close_sound ();

    currprefs.produce_sound = changed_prefs.produce_sound;
    currprefs.win32_soundcard = changed_prefs.win32_soundcard;
    currprefs.sound_stereo = changed_prefs.sound_stereo;
    currprefs.sound_auto = changed_prefs.sound_auto;
    currprefs.sound_freq = changed_prefs.sound_freq;
    currprefs.sound_maxbsiz = changed_prefs.sound_maxbsiz;

    currprefs.sound_stereo_separation = changed_prefs.sound_stereo_separation;
    currprefs.sound_mixed_stereo_delay = changed_prefs.sound_mixed_stereo_delay;
    currprefs.sound_interpol = changed_prefs.sound_interpol;
    currprefs.sound_filter = changed_prefs.sound_filter;
    currprefs.sound_filter_type = changed_prefs.sound_filter_type;
    currprefs.sound_volume = changed_prefs.sound_volume;
    currprefs.sound_stereo_swap_paula = changed_prefs.sound_stereo_swap_paula;
    currprefs.sound_stereo_swap_ahi = changed_prefs.sound_stereo_swap_ahi;

    if (ch >= 0) {
	if (currprefs.produce_sound >= 2) {
	    if (!init_audio ()) {
		if (! sound_available) {
		    write_log (L"Sound is not supported.\n");
		} else {
		    write_log (L"Sorry, can't initialize sound.\n");
		    currprefs.produce_sound = 0;
		    /* So we don't do this every frame */
		    changed_prefs.produce_sound = 0;
		}
	    }
	}
	next_sample_evtime = scaled_sample_evtime;
	last_cycles = get_cycles () - 1;
	compute_vsynctime ();
    } else {
        sound_volume (0);
    }

    sep = (currprefs.sound_stereo_separation = changed_prefs.sound_stereo_separation) * 3 / 2;
    delay = currprefs.sound_mixed_stereo_delay = changed_prefs.sound_mixed_stereo_delay;
    mixed_mul1 = MIXED_STEREO_SCALE / 2 - sep;
    mixed_mul2 = MIXED_STEREO_SCALE / 2 + sep;
    mixed_stereo_size = delay > 0 ? (1 << (delay - 1)) - 1 : 0;
    mixed_on = (sep > 0 && sep < MIXED_STEREO_MAX) || mixed_stereo_size > 0;
    if (mixed_on && old_mixed_size != mixed_stereo_size) {
	saved_ptr = 0;
	memset (right_word_saved, 0, sizeof right_word_saved);
    }

    led_filter_forced = -1; // always off
    sound_use_filter = sound_use_filter_sinc = 0;
    if (currprefs.sound_filter) {
	if (currprefs.sound_filter == FILTER_SOUND_ON)
	    led_filter_forced = 1;
	if (currprefs.sound_filter == FILTER_SOUND_EMUL)
	    led_filter_forced = 0;
	if (currprefs.sound_filter_type == FILTER_SOUND_TYPE_A500)
	    sound_use_filter = FILTER_MODEL_A500;
	else if (currprefs.sound_filter_type == FILTER_SOUND_TYPE_A1200)
	    sound_use_filter = FILTER_MODEL_A1200;
    }
    a500e_filter1_a0 = rc_calculate_a0 (currprefs.sound_freq, 6200);
    a500e_filter2_a0 = rc_calculate_a0 (currprefs.sound_freq, 20000);
    filter_a0 = rc_calculate_a0 (currprefs.sound_freq, 7000);
    led_filter_audio ();

    /* Select the right interpolation method.  */
    if (sample_handler == sample16_handler
	|| sample_handler == sample16i_crux_handler
	|| sample_handler == sample16i_rh_handler
	|| sample_handler == sample16i_sinc_handler
	|| sample_handler == sample16i_anti_handler)
    {
	sample_handler = (currprefs.sound_interpol == 0 ? sample16_handler
			  : currprefs.sound_interpol == 3 ? sample16i_rh_handler
			  : currprefs.sound_interpol == 4 ? sample16i_crux_handler
			  : currprefs.sound_interpol == 2 ? sample16i_sinc_handler
			  : sample16i_anti_handler);
    } else if (sample_handler == sample16s_handler
	     || sample_handler == sample16si_crux_handler
	     || sample_handler == sample16si_rh_handler
	     || sample_handler == sample16si_sinc_handler
	     || sample_handler == sample16si_anti_handler)
    {
	sample_handler = (currprefs.sound_interpol == 0 ? sample16s_handler
			  : currprefs.sound_interpol == 3 ? sample16si_rh_handler
			  : currprefs.sound_interpol == 4 ? sample16si_crux_handler
			  : currprefs.sound_interpol == 2 ? sample16si_sinc_handler
			  : sample16si_anti_handler);
    } else if (sample_handler == sample16ss_handler
	|| sample_handler == sample16ss_sinc_handler
	|| sample_handler == sample16ss_anti_handler)
    {
	sample_handler = (currprefs.sound_interpol == 0 ? sample16ss_handler
			  : currprefs.sound_interpol == 3 ? sample16ss_handler
			  : currprefs.sound_interpol == 4 ? sample16ss_handler
			  : currprefs.sound_interpol == 2 ? sample16ss_sinc_handler
			  : sample16ss_anti_handler);
    }
    sample_prehandler = NULL;
    if (sample_handler == sample16si_sinc_handler || sample_handler == sample16i_sinc_handler || sample_handler == sample16ss_sinc_handler) {
	sample_prehandler = sinc_prehandler;
	sound_use_filter_sinc = sound_use_filter;
	sound_use_filter = 0;
    } else if (sample_handler == sample16si_anti_handler || sample_handler == sample16i_anti_handler || sample_handler == sample16ss_anti_handler) {
	sample_prehandler = anti_prehandler;
    }

    audio_activate ();

    if (currprefs.produce_sound == 0) {
	eventtab[ev_audio].active = 0;
	events_schedule ();
    }
}

void update_audio (void)
{
    unsigned long int n_cycles = 0;

    if (!isaudio())
	goto end;
    if (savestate_state == STATE_RESTORE)
	goto end;
    if (!is_audio_active())
	goto end;

    n_cycles = get_cycles () - last_cycles;
    for (;;) {
	unsigned long int best_evtime = n_cycles + 1;

	if (audio_channel[0].evtime != MAX_EV && best_evtime > audio_channel[0].evtime)
	    best_evtime = audio_channel[0].evtime;
	if (audio_channel[1].evtime != MAX_EV && best_evtime > audio_channel[1].evtime)
	    best_evtime = audio_channel[1].evtime;
	if (audio_channel[2].evtime != MAX_EV && best_evtime > audio_channel[2].evtime)
	    best_evtime = audio_channel[2].evtime;
	if (audio_channel[3].evtime != MAX_EV && best_evtime > audio_channel[3].evtime)
	    best_evtime = audio_channel[3].evtime;

	if (currprefs.produce_sound > 1 && best_evtime > next_sample_evtime)
	    best_evtime = next_sample_evtime;

	if (best_evtime > n_cycles)
	    break;

	if (audio_channel[0].evtime != MAX_EV)
	    audio_channel[0].evtime -= best_evtime;
	if (audio_channel[1].evtime != MAX_EV)
	    audio_channel[1].evtime -= best_evtime;
	if (audio_channel[2].evtime != MAX_EV)
	    audio_channel[2].evtime -= best_evtime;
	if (audio_channel[3].evtime != MAX_EV)
	    audio_channel[3].evtime -= best_evtime;

	n_cycles -= best_evtime;
	if (currprefs.produce_sound > 1) {
	    next_sample_evtime -= best_evtime;
	    if (sample_prehandler)
		sample_prehandler(best_evtime / CYCLE_UNIT);
	    if (next_sample_evtime == 0) {
		next_sample_evtime = scaled_sample_evtime;
		(*sample_handler) ();
	    }
	}

	if (audio_channel[0].evtime == 0)
	    audio_handler (0);
	if (audio_channel[1].evtime == 0)
	    audio_handler (1);
	if (audio_channel[2].evtime == 0)
	    audio_handler (2);
	if (audio_channel[3].evtime == 0)
	    audio_handler (3);
    }
end:
    last_cycles = get_cycles () - n_cycles;
}

void audio_evhandler (void)
{
    update_audio ();
    schedule_audio ();
}

uae_u16	dmacon;

void audio_hsync (int dmaaction)
{
    int nr, handle;
    static int old_dma;

    if (!isaudio ())
	return;

    if (old_dma != (dmacon & (DMA_MASTER | 15))) {
	old_dma = dmacon & (DMA_MASTER | 15);
	audio_activate ();
    }

    if (audio_work_to_do > 0 && currprefs.sound_auto) {
	audio_work_to_do--;
	if (audio_work_to_do == 0)
	    audio_deactivate ();
    }

    if (!is_audio_active ())
	return;

    update_audio ();
    handle = 0;
    /* Sound data is fetched at the beginning of each line */
    for (nr = 0; nr < 4; nr++) {
	struct audio_channel_data *cdp = audio_channel + nr;
	int chan_ena = (dmacon & DMA_MASTER) && (dmacon & (1 << nr));
	int handle2 = 0;

	if (dmaaction && cdp->request_word > 0) {

	    if (cdp->request_word_skip) {
		cdp->request_word_skip = 0;
		continue;
	    }

	    if (cdp->state == 5) {
		cdp->pt = cdp->lc;
		if (sampleripper_enabled)
		    do_samplerip (cdp);
#ifdef DEBUG_AUDIO
		if (debugchannel (nr))
		    write_log (L"%d:>5: LEN=%d PT=%08X\n", nr, cdp->wlen, cdp->pt);
#endif
	    }
	    cdp->dat2 = chipmem_agnus_wget (cdp->pt);
	    if (cdp->request_word >= 2)
		handle2 = 1;
	    if (chan_ena) {
		alloc_cycle_ext (13 + nr * 2, CYCLE_MISC);
		if (cdp->request_word == 1 || cdp->request_word == 2)
		    cdp->pt += 2;
	    }
	    cdp->request_word = -1;

	}

	if (cdp->dmaen != chan_ena) {
#ifdef DEBUG_AUDIO
	    if (debugchannel (nr))
		write_log (L"AUD%dDMA %d->%d (%d) LEN=%d/%d %08X\n", nr, cdp->dmaen, chan_ena,
		    cdp->state, cdp->wlen, cdp->len, M68K_GETPC);
#endif
	    cdp->dmaen = chan_ena;
	    if (cdp->dmaen)
		handle2 = 1;
	}
	if (handle2)
	    audio_handler (nr);
	handle |= handle2;
    }
    if (handle) {
	schedule_audio ();
	events_schedule ();
    }
}

void AUDxDAT (int nr, uae_u16 v)
{
    struct audio_channel_data *cdp = audio_channel + nr;

#ifdef DEBUG_AUDIO
    if (debugchannel (nr))
	write_log (L"AUD%dDAT: %04X STATE=%d IRQ=%d %08X\n", nr,
	    v, cdp->state, isirq(nr) ? 1 : 0, M68K_GETPC);
#endif
    audio_activate();
    update_audio ();
    cdp->dat2 = v;
    if (cdp->request_word >= 2 && cdp->request_word_skip == 0)
	audio_handler (nr);
    cdp->request_word = -1;
    cdp->request_word_skip = 0;
    /* cpu >= 68020: another "too fast" memory/CPU hack */
    if (cdp->state == 0 || currprefs.cpu_model >= 68020) {
	cdp->state = 2;
	cdp->wlen = cdp->len;
	cdp->pt = cdp->lc;
	if (currprefs.cpu_model >= 68020)
	    INTREQ (0x80 << nr);
	audio_handler (nr);
	schedule_audio ();
	events_schedule ();
    }
 }

void AUDxLCH (int nr, uae_u16 v)
{
    audio_activate();
    update_audio ();
    audio_channel[nr].lc = (audio_channel[nr].lc & 0xffff) | ((uae_u32)v << 16);
#ifdef DEBUG_AUDIO
    if (debugchannel (nr))
	write_log (L"AUD%dLCH: %04X %08X\n", nr, v, M68K_GETPC);
#endif
}

void AUDxLCL (int nr, uae_u16 v)
{
    audio_activate();
    update_audio ();
    audio_channel[nr].lc = (audio_channel[nr].lc & ~0xffff) | (v & 0xFFFE);
#ifdef DEBUG_AUDIO
    if (debugchannel (nr))
	write_log (L"AUD%dLCL: %04X %08X\n", nr, v, M68K_GETPC);
#endif
}

void AUDxPER (int nr, uae_u16 v)
{
    unsigned long per = v * CYCLE_UNIT;

    audio_activate();
    update_audio ();
    if (per == 0)
	per = PERIOD_MAX - 1;

    if (per < maxhpos * CYCLE_UNIT / 2 && currprefs.produce_sound < 3)
	per = maxhpos * CYCLE_UNIT / 2;
    else if (per < 4 * CYCLE_UNIT)
	 /* smaller values would cause extremely high cpu usage */
	per = 4 * CYCLE_UNIT;

   if (audio_channel[nr].per == PERIOD_MAX - 1 && per != PERIOD_MAX - 1) {
	audio_channel[nr].evtime = CYCLE_UNIT;
	if (isaudio()) {
	    schedule_audio ();
	    events_schedule ();
	}
    }

    audio_channel[nr].per = per;
#ifdef DEBUG_AUDIO
    if (debugchannel (nr))
	write_log (L"AUD%dPER: %d %08X\n", nr, v, M68K_GETPC);
#endif
}

void AUDxLEN (int nr, uae_u16 v)
{
    audio_activate();
    update_audio ();
    audio_channel[nr].len = v;
#ifdef DEBUG_AUDIO
    if (debugchannel (nr))
	write_log (L"AUD%dLEN: %d %08X\n", nr, v, M68K_GETPC);
#endif
}

void AUDxVOL (int nr, uae_u16 v)
{
    int v2 = v & 64 ? 63 : v & 63;

    audio_activate();
    update_audio ();
    audio_channel[nr].vol = v2;
#ifndef	MULTIPLICATION_PROFITABLE
    audio_channel[nr].voltbl = sound_table[v2];
#endif
#ifdef DEBUG_AUDIO
    if (debugchannel (nr))
	write_log (L"AUD%dVOL: %d %08X\n", nr, v2, M68K_GETPC);
#endif
}

void audio_update_irq (uae_u16 v)
{
#ifdef DEBUG_AUDIO
    uae_u16 v2 = intreq, v3 = intreq;
    int i;
    if (v & 0x8000)
	v2 |= v & 0x7FFF;
    else
	v2 &= ~v;
    v2 &= (0x80 | 0x100 | 0x200 | 0x400);
    v3 &= (0x80 | 0x100 | 0x200 | 0x400);
    for (i = 0; i < 4; i++) {
	if ((1 << i) & DEBUG_CHANNEL_MASK) {
	    uae_u16 mask = 0x80 << i;
	    if ((v2 & mask) != (v3 & mask))
		write_log (L"AUD%dINTREQ %d->%d %08X\n", i, !!(v3 & mask), !!(v2 & mask), M68K_GETPC);
	}
    }
#endif
}

void audio_update_adkmasks (void)
{
    static int prevcon = -1;
    unsigned long t = adkcon | (adkcon >> 4);

    audio_channel[0].adk_mask = (((t >> 0) & 1) - 1);
    audio_channel[1].adk_mask = (((t >> 1) & 1) - 1);
    audio_channel[2].adk_mask = (((t >> 2) & 1) - 1);
    audio_channel[3].adk_mask = (((t >> 3) & 1) - 1);
    if ((prevcon & 0xff) != (adkcon & 0xff)) {
	audio_activate();
#ifdef DEBUG_AUDIO
	write_log (L"ADKCON=%02x %08X\n", adkcon & 0xff, M68K_GETPC);
#endif
	prevcon = adkcon;
    }
}

int init_audio (void)
{
    return init_sound ();
}


void led_filter_audio (void)
{
    led_filter_on = 0;
    if (led_filter_forced > 0 || (gui_data.powerled && led_filter_forced >= 0))
	led_filter_on = 1;
}

uae_u8 *restore_audio (int i, uae_u8 *src)
{
    struct audio_channel_data *acd;
    uae_u16 p;

    acd = audio_channel + i;
    acd->state = restore_u8 ();
    acd->vol = restore_u8 ();
    acd->intreq2 = restore_u8 ();
    acd->request_word = restore_u8 ();
    acd->len = restore_u16 ();
    acd->wlen = restore_u16 ();
    p = restore_u16 ();
    acd->per = p ? p * CYCLE_UNIT : PERIOD_MAX;
    p = restore_u16 ();
    acd->lc = restore_u32 ();
    acd->pt = restore_u32 ();
    acd->evtime = restore_u32 ();
    return src;
}


uae_u8 *save_audio (int i, int *len, uae_u8 *dstptr)
{
    struct audio_channel_data *acd;
    uae_u8 *dst, *dstbak;
    uae_u16 p;

    if (dstptr)
	dstbak = dst = dstptr;
    else
	dstbak = dst = (uae_u8*)malloc (100);
    acd = audio_channel + i;
    save_u8 ((uae_u8)acd->state);
    save_u8 (acd->vol);
    save_u8 (acd->intreq2);
    save_u8 (acd->request_word);
    save_u16 (acd->len);
    save_u16 (acd->wlen);
    p = acd->per == PERIOD_MAX ? 0 : acd->per / CYCLE_UNIT;
    save_u16 (p);
    save_u16 (acd->dat2);
    save_u32 (acd->lc);
    save_u32 (acd->pt);
    save_u32 (acd->evtime);
    *len = dst - dstbak;
    return dstbak;
}
