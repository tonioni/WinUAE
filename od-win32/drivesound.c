#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "zfile.h"
#include "sounddep/sound.h"
#include "dxwrap.h"

#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>

static LPDIRECTSOUND lpDS;
static LPDIRECTSOUNDBUFFER lpDSBprimary;

#define DRIVESOUND_CLICK 0
#define DRIVESOUND_STARTUP 1
#define DRIVESOUND_SPIN 2
#define DRIVESOUND_END 3

static LPDIRECTSOUNDBUFFER samples[4][DRIVESOUND_END];
static int drivesound_enabled, drive_enabled[4];
static int click_frequency[4];
static int minfreq, maxfreq;

extern GUID sound_device_guid[];

static void close_audio_ds (void)
{
    int i, j;

    drivesound_enabled = 0;
    for (i = 0; i < DRIVESOUND_END; i++) {
	for (j = 0; j < 4; j++) {
	    if (samples[j][i])
		IDirectSound_Release (samples[j][i]);
	    samples[j][i] = 0;
	    drive_enabled[j] = 0;
	}
    }
    if (lpDSBprimary)
	IDirectSound_Release (lpDSBprimary);
    lpDSBprimary = 0;
    if (lpDS) {
	IDirectSound_Release (lpDS);
	write_log ("DRIVESOUND: DirectSound driver freed\n");
    }
    lpDS = 0;
}

extern HWND hMainWnd;

static int createsample (int drivenum, int samplenum, char *path)
{
    struct zfile *f;
    uae_u8 *buf = 0;
    int size, freq = 44100;
    WAVEFORMATEX wavfmt;
    DSBUFFERDESC sound_buffer;
    HRESULT hr;
    void *b1, *b2;
    DWORD s1, s2;

    f = zfile_fopen (path, "rb");
    if (!f) {
	write_log ("DRIVESOUND: can't open '%s'\n", path);
	goto error;
    }
    zfile_fseek (f, 0, SEEK_END);
    size = zfile_ftell (f);
    buf = malloc (size);
    zfile_fseek (f, 0, SEEK_SET);
    zfile_fread (buf, size, 1, f);
    zfile_fclose (f);

    if (samplenum == DRIVESOUND_CLICK)
	click_frequency[drivenum] = freq;
    wavfmt.wFormatTag = WAVE_FORMAT_PCM;
    wavfmt.nChannels = 2;
    wavfmt.nSamplesPerSec = freq;
    wavfmt.wBitsPerSample = 16;
    wavfmt.nBlockAlign = 16 / 8 * wavfmt.nChannels;
    wavfmt.nAvgBytesPerSec = wavfmt.nBlockAlign * freq;
    memset (&sound_buffer, 0, sizeof (sound_buffer));
    sound_buffer.dwSize = sizeof (sound_buffer);
    sound_buffer.dwBufferBytes = size;
    sound_buffer.lpwfxFormat = &wavfmt;
    sound_buffer.dwFlags = DSBCAPS_LOCSOFTWARE  | DSBCAPS_GLOBALFOCUS;

    hr = IDirectSound_CreateSoundBuffer( lpDS, &sound_buffer, &samples[drivenum][samplenum], NULL );
    if (hr != DS_OK) {
        write_log ("DRIVESOUND: Secondary CreateSoundBuffer(%s) failure: %s\n",
	    path, DXError (hr));
        goto error;
    }
    hr = IDirectSoundBuffer_Lock (samples[drivenum][samplenum], 0, sndbufsize, &b1, &s1, &b2, &s2, DSBLOCK_ENTIREBUFFER);
    if (hr != DS_OK) {
        write_log ("DRIVESOUND: lock failed: %s\n", DXError (hr));
        goto error;
    }
    memcpy (b1, buf, size >= s1 ? s1 : size);
    if (b2)
        memcpy (b2, buf + s1, size - s1);
    IDirectSoundBuffer_Unlock (samples[drivenum][samplenum], b1, s1, b2, s2);
    write_log ("DRIVESOUND: drive %d, sample %d (%s) loaded\n", drivenum, samplenum, path);

    return 1;
    error:
    free (buf);
    if (samples[drivenum][samplenum]) {
	IDirectSound_Release (samples[drivenum][samplenum]);
	samples[drivenum][samplenum] = 0;
    }
    return 0;
}


static int open_audio_ds (void)
{
    HRESULT hr;
    int freq = currprefs.sound_freq;
    
    enumerate_sound_devices (0);
    hr = DirectSoundCreate (&sound_device_guid[currprefs.win32_soundcard], &lpDS, NULL);
    if (hr != DS_OK)  {
        write_log ("SOUND: DirectSoundCreate() failure: %s\n", DXError (hr));
        return 0;
    }
    hr = IDirectSound_SetCooperativeLevel (lpDS, hMainWnd, DSSCL_NORMAL);
    if (hr != DS_OK) {
        write_log ("SOUND: Can't set cooperativelevel: %s\n", DXError (hr));
        goto error;
    }

    createsample(0, DRIVESOUND_CLICK, "drive_click.raw");
    createsample(0, DRIVESOUND_STARTUP, "drive_startup.raw");
    createsample(0, DRIVESOUND_SPIN, "drive_spin.raw");
    drive_enabled[0] = 1;    

    drivesound_enabled = 1;

    return 1;

error:
    close_audio_ds ();
    return 0;
}

int drivesound_init (void)
{
    return open_audio_ds ();
}

void drivesound_free (void)
{
    close_audio_ds ();
}

void drivesound_click (int num, int delay)
{
    HRESULT hr;
    LPDIRECTSOUNDBUFFER dsb;

    if (!drivesound_enabled || !drive_enabled[num])
	return;
    dsb = samples[num][DRIVESOUND_CLICK];
    if (!dsb)
	return;
    hr = IDirectSoundBuffer_SetCurrentPosition (dsb, 0);
    hr = IDirectSoundBuffer_Play (dsb, 0, 0, 0);
}

void drivesound_motor (int num, int status)
{
    HRESULT hr;

    if (!drivesound_enabled || !drive_enabled[num])
	return;
    if (!status) {
	if (samples[num][DRIVESOUND_STARTUP])
	    hr = IDirectSoundBuffer_Stop (samples[num][DRIVESOUND_STARTUP]);
	if (samples[num][DRIVESOUND_SPIN])
	    hr = IDirectSoundBuffer_Stop (samples[num][DRIVESOUND_SPIN]);
    } else {
	if (status == 2 && samples[num][DRIVESOUND_STARTUP])
	    hr = IDirectSoundBuffer_Play (samples[num][DRIVESOUND_STARTUP], 0, 0, 0);
	if (samples[num][DRIVESOUND_SPIN])
	    hr = IDirectSoundBuffer_Play (samples[num][DRIVESOUND_SPIN], 0, 0, DSBPLAY_LOOPING);
    }
}
