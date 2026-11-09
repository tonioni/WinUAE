#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "events.h"
#include "sound_common.h"
#include <cmath>
uae_prefs currprefs, changed_prefs;
float scaled_sample_evtime, sampler_evtime;
frame_time_t vsynctimebase, vsynctimebase_orig;
int avioutput_audio, avioutput_enabled, avioutput_nosoundsync;
static int vsync;
int isvsync_chipset() { return vsync; }
void sound_setadjust(float);
#define CHECK(x)                                                                                   \
    do {                                                                                           \
	if (!(x)) {                                                                                \
	    fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
	    return 1;                                                                              \
	}                                                                                          \
    } while (0)
int main()
{
    for (int ch : {1, 2, 4, 6, 8}) {
	uae_s16 samples[34];
	for (int i = 0; i < 34; i++)
	    samples[i] = (uae_s16)(i * 100 - 1600);
	CHECK(host_sound_prepare(samples + 1, ch * 4, ch, true, false) == ch * 4);
	CHECK(samples[0] == -1600 && samples[ch * 2 + 1] == (ch * 2 + 1) * 100 - 1600);
	for (int frame = 0; frame < 2; frame++)
	    for (int c = 0; c < ch; c++) {
		int source = (ch == 1 || (ch >= 6 && c >= 2 && c < 4)) ? c : (c ^ 1);
		CHECK(samples[1 + frame * ch + c] == (1 + frame * ch + source) * 100 - 1600);
	    }
    }
    uae_s16 edge[] = {-32768, -12345, 0, 12345, 32767};
    host_sound_apply_gain(edge, 5, 16384);
    CHECK(edge[0] == -16384 && edge[1] == -6172 && edge[4] == 16383);
    CHECK(host_sound_prepare(edge, 3, 2, true, false) == 0);
    CHECK(host_sound_prepare(edge, 4, 2, true, true) == 0);
    CHECK(host_sound_gain(0, false) == -1 && host_sound_gain(100, false) == 0);
    CHECK(host_sound_gain(0, true) == 0 && host_sound_gain(50, false) == 16384);
    CHECK(host_sound_volume_step(95, -1) == 100 && host_sound_volume_step(5, 1) == 0);
    CHECK(host_sound_sync(1000, true) == 12 && host_sound_sync(-1000, false) == -20);
    float sum = 0, count = 0, adjust = 0;
    unsigned long previous = 0;
    CHECK(host_sound_correct(&sum, &count, &previous, 1, 100, 100, &adjust));
    CHECK(adjust == 2);
    CHECK(!host_sound_correct(&sum, &count, &previous, 1, 0, 100, &adjust));
    CHECK(host_sound_correct(&sum, &count, &previous, 2, -100, 100, &adjust));
    CHECK(adjust == -1);
    vsynctimebase_orig = 1000000;
    host_sound_update_clock(3546895, 44100, 1);
    float original = scaled_sample_evtime_orig;
    vsync = 1;
    sound_setadjust(100);
    CHECK(vsynctimebase == vsynctimebase_orig &&
	  fabs(scaled_sample_evtime / original - 1.006f) < 0.00001);
    vsync = 0;
    sound_setadjust(-100);
    CHECK(vsynctimebase == 994000 && scaled_sample_evtime == original);
    avioutput_audio = avioutput_enabled = avioutput_nosoundsync = 1;
    sound_setadjust(6);
    CHECK(vsynctimebase == vsynctimebase_orig && scaled_sample_evtime == original);
    // Closed-loop drift stays bounded with host clocks 0.3 percent apart.
    for (double drift : {-0.003, -0.001, 0.001, 0.003}) {
	double queued = 4096;
	float sum = 0, count = 0, adjustment = 0;
	unsigned long previous = 0;
	for (unsigned long frame = 1; frame <= 180000; frame++) {
	    queued += 882 / (1 + adjustment / 1000.0) - 882 * (1 + drift);
	    host_sound_correct(&sum, &count, &previous, frame, (float)(queued - 4096), 100,
			       &adjustment);
	    if (adjustment < -6)
		adjustment = -6;
	    if (adjustment > 6)
		adjustment = 6;
	    CHECK(queued > 3500 && queued < 4700);
	}
    }
    return 0;
}
