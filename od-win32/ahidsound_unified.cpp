/*
 * UAE - The Un*x Amiga Emulator
 *
 * Win32 interface
 *
 * Copyright 1997 Mathias Ortmann
 * Copyright 1997-2001 Brian King
 * Copyright 2000-2002 Bernd Roesch
 */

#define NATIVEBUFFNUM 4
#define AMIGASAMPLESIZE 4
#define RECORDBUFFER 50 //survive 9 sec of blocking at 44100

#include "sysconfig.h"

#if defined(AHI)

#include <ctype.h>
#include <assert.h>

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

#include "sysdeps.h"
#include "options.h"
#include "audio.h"
#include "memory.h"
#include "events.h"
#include "custom.h"
#include "newcpu.h"
#include "traps.h"
#include "od-win32/win32.h"
#include "dxwrap.h"
#include "win32.h"
#include "parser.h"
#include "enforcer.h"
#include "ahidsound.h"

#include <windows.h>
#include <dsound.h>

#include <portaudio.h>

#include "sounddep/sound.h"

static struct sound_data sdahi;
static struct sound_data *sdp = &sdahi;

static long samples, playchannel, intcount;
static int record_enabled;
int ahi_on;
static uae_u8 soundneutral;

static unsigned int *sndbufrecpt;
static uae_u8 *ahisndbuffer, *sndrecbuffer;
static int ahisndbufsize, ahitweak;;
static uae_u8 *ahisndbufpt;
static uae_u8 *sndptrmax;
int ahi_pollrate = 40;

int sound_freq_ahi, sound_channels_ahi, sound_bits_ahi;

static int amigablksize;

static DWORD sound_flushes2;

extern HWND hAmigaWnd;

// for record
static LPDIRECTSOUNDCAPTURE lpDS2r = NULL;
static LPDIRECTSOUNDCAPTUREBUFFER lpDSBprimary2r = NULL;
static LPDIRECTSOUNDCAPTUREBUFFER lpDSB2r = NULL;

struct winuae	//this struct is put in a6 if you call
		//execute native function
{
    HWND amigawnd;    //address of amiga Window Windows Handle
    unsigned int changenum;   //number to detect screen close/open
    unsigned int z3offset;    //the offset to add to acsess Z3 mem from Dll side
};
static struct winuae uaevar;
static struct winuae *a6;

#if defined(X86_MSVC_ASSEMBLY)

#define CREATE_NATIVE_FUNC_PTR2 uae_u32 (*native_func)(uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32,\
	uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32,uae_u32,uae_u32)
#define SET_NATIVE_FUNC2(x) native_func = (uae_u32 (*)(uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32, uae_u32,uae_u32,uae_u32))(x)
#define CALL_NATIVE_FUNC2(d1,d2,d3,d4,d5,d6,d7,a1,a2,a3,a4,a5,a6,a7) if(native_func) return native_func(d1,d2,d3,d4,d5,d6,d7,a1,a2,a3,a4,a5,a6,a7,regs_)

static uae_u32 REGPARAM2 emulib_ExecuteNativeCode2 (TrapContext *context)
{
    unsigned int espstore;
    uae_u8* object_UAM = (uae_u8*) m68k_areg (&context->regs, 0);
    uae_u32 d1 = m68k_dreg (&context->regs, 1);
    uae_u32 d2 = m68k_dreg (&context->regs, 2);
    uae_u32 d3 = m68k_dreg (&context->regs, 3);
    uae_u32 d4 = m68k_dreg (&context->regs, 4);
    uae_u32 d5 = m68k_dreg (&context->regs, 5);
    uae_u32 d6 = m68k_dreg (&context->regs, 6);
    uae_u32 d7 = m68k_dreg (&context->regs, 7);
    uae_u32 a1 = m68k_areg (&context->regs, 1);
    uae_u32 a2 = m68k_areg (&context->regs, 2);
    uae_u32 a3 = m68k_areg (&context->regs, 3);
    uae_u32 a4 = m68k_areg (&context->regs, 4);
    uae_u32 a5 = m68k_areg (&context->regs, 5);
    uae_u32 a7 = m68k_areg (&context->regs, 7);
    uae_u32 regs_ = (uae_u32)&context->regs;
    CREATE_NATIVE_FUNC_PTR2;
    uaevar.z3offset = (uae_u32)(get_real_address (0x10000000) - 0x10000000);
    uaevar.amigawnd = hAmigaWnd;
    a6 = &uaevar;
    if (object_UAM)  {
	SET_NATIVE_FUNC2 (object_UAM);
    __asm
	{   mov espstore,esp
	    push regs_
	    push a7
	    push a6
	    push a5
	    push a4
	    push a3
	    push a2
	    push a1
	    push d7
	    push d6
	    push d5
	    push d4
	    push d3
	    push d2
	    push d1
	    call native_func
	    mov esp,espstore
	}
	//CALL_NATIVE_FUNC2( d1, d2,d3, d4, d5, d6, d7, a1, a2, a3, a4 , a5 , a6 , a7);
    } else {
	return 0;
    }
}

#endif

void ahi_close_sound (void)
{
    HRESULT hr = DS_OK;

    if (!ahi_on)
	return;
    ahi_on = 0;
    record_enabled = 0;
    ahisndbufpt = ahisndbuffer;

    close_sound_device (sdp);
#if 0
    if (lpDSB2) {
	hr = IDirectSoundBuffer_Stop (lpDSB2);
	if(FAILED (hr))
	    write_log (_T("AHI: SoundStop() failure: %s\n"), DXError (hr));
    } else {
	write_log (_T("AHI: Sound Stopped...\n"));
    }

    if (lpDSB2)
	IDirectSoundBuffer_Release (lpDSB2);
    lpDSB2 = NULL;
    if (lpDSBprimary2)
	IDirectSoundBuffer_Release (lpDSBprimary2);
    lpDSBprimary2 = NULL;
    if (lpDS2)
	IDirectSound_Release (lpDS2);
    lpDS2 = NULL;
#endif

    if (lpDSB2r)
	IDirectSoundCaptureBuffer_Release (lpDSB2r);
    lpDSB2r = NULL;
    if (lpDS2r)
	IDirectSound_Release (lpDS2r);
    lpDS2r = NULL;
    if (ahisndbuffer)
	xfree (ahisndbuffer);
    ahisndbuffer = NULL;
}

void ahi_updatesound (int force)
{
    static int doint;
    uae_u8 *ptr;

    if (sound_flushes2 == 1) {
	intcount = 1;
	INTREQ_f (0x8000 | 0x2000);
	resume_sound_device (sdp);
    }

    if (blocking_sound_device (sdp))
	return;
    if (force == 1) {
	doint = 1;
    }
    if (doint) {
        INTREQ_f (0x8000 | 0x2000);
        intcount += doint;
        doint = 0;
    }

    ptr = ahisndbuffer;
    while (blocking_sound_device (sdp) == 0 && ptr < ahisndbufpt) {
	int blocksize = amigablksize * AMIGASAMPLESIZE;
	if (currprefs.sound_stereo_swap_ahi) {
	    int i;
	    uae_s16 *p = (uae_s16*)ptr;
	    for (i = 0; i < blocksize / 2; i += 2) {
		uae_s16 tmp;
		tmp = p[i + 0];
		p[i + 0] = p[i + 1];
		p[i + 1] = tmp;
	    }
	}
	send_sound (sdp, (uae_u16*)ptr);
	ptr += blocksize;
	doint++;
    }
    if (ptr < ahisndbufpt) {
	int diff = ahisndbufpt - ptr;
	memmove (ahisndbuffer, ptr, diff);
	ahisndbufpt = ahisndbuffer + diff;
	sndptrmax = ahisndbuffer + ahisndbufsize - diff;
    } else {
	ahisndbufpt = ahisndbuffer;
        sndptrmax = ahisndbuffer + ahisndbufsize;
    }
}


#if 0
void ahi_updatesound (int force)
{
    HRESULT hr;
    int pos;
    unsigned int dwBytes1, dwBytes2;
    LPVOID dwData1, dwData2;
    static int oldpos;

    if (sound_flushes2 == 1) {
	oldpos = 0;
	intcount = 1;
	INTREQ_f (0x8000 | 0x2000);
	hr = IDirectSoundBuffer_Play (lpDSB2, 0, 0, DSBPLAY_LOOPING);
	if(hr == DSERR_BUFFERLOST) {
	    IDirectSoundBuffer_Restore (lpDSB2);
	    hr = IDirectSoundBuffer_Play (lpDSB2, 0, 0, DSBPLAY_LOOPING);
	}
    }

    hr = IDirectSoundBuffer_GetCurrentPosition (lpDSB2, &pos, 0);
    if (hr != DSERR_BUFFERLOST) {
	pos -= ahitweak;
	if (pos < 0)
	    pos += ahisndbufsize;
	if (pos >= ahisndbufsize)
	    pos -= ahisndbufsize;
	pos = (pos / (amigablksize * 4)) * (amigablksize * 4);
	if (force == 1) {
	    if (oldpos != pos) {
		intcount = 1;
		INTREQ_f (0x8000 | 0x2000);
		return; //to generate amiga ints every amigablksize
	    } else {
		return;
	    }
	}
    }

    hr = IDirectSoundBuffer_Lock (lpDSB2, oldpos, amigablksize * 4, &dwData1, &dwBytes1, &dwData2, &dwBytes2, 0);
    if(hr == DSERR_BUFFERLOST) {
	write_log (_T("AHI: lostbuf %d %x\n"), pos, amigablksize);
	IDirectSoundBuffer_Restore (lpDSB2);
	hr = IDirectSoundBuffer_Lock (lpDSB2, oldpos, amigablksize * 4, &dwData1, &dwBytes1, &dwData2, &dwBytes2, 0);
    }
    if(FAILED(hr))
	return;

    if (currprefs.sound_stereo_swap_ahi) {
	int i;
	uae_s16 *p = (uae_s16*)ahisndbuffer;
	for (i = 0; i < (dwBytes1 + dwBytes2) / 2; i += 2) {
	    uae_s16 tmp;
	    tmp = p[i + 0];
	    p[i + 0] = p[i + 1];
	    p[i + 1] = tmp;
	}
    }

    memcpy (dwData1, ahisndbuffer, dwBytes1);
    if (dwData2)
	memcpy (dwData2, (uae_u8*)ahisndbuffer + dwBytes1, dwBytes2);

    sndptrmax = ahisndbuffer + ahisndbufsize;
    ahisndbufpt = (int*)ahisndbuffer;

    IDirectSoundBuffer_Unlock (lpDSB2, dwData1, dwBytes1, dwData2, dwBytes2);

    oldpos += amigablksize * 4;
    if (oldpos >= ahisndbufsize)
	oldpos -= ahisndbufsize;
    if (oldpos != pos) {
	intcount = 1;
	INTREQ_f (0x8000 | 0x2000);
    }
}
#endif

void ahi_finish_sound_buffer (void)
{
    sound_flushes2++;
    ahi_updatesound (2);
}

static WAVEFORMATEX wavfmt;

static int ahi_init_record_win32 (void)
{
    HRESULT hr;
    DSCBUFFERDESC sound_buffer_rec;
    // Record begin
    hr = DirectSoundCaptureCreate (NULL, &lpDS2r, NULL);
    if (FAILED (hr)) {
	write_log (_T("AHI: DirectSoundCaptureCreate() failure: %s\n"), DXError (hr));
	record_enabled = -1;
	return 0;
    }
    memset (&sound_buffer_rec, 0, sizeof (DSCBUFFERDESC));
    sound_buffer_rec.dwSize = sizeof (DSCBUFFERDESC);
    sound_buffer_rec.dwBufferBytes = amigablksize * 4 * RECORDBUFFER;
    sound_buffer_rec.lpwfxFormat = &wavfmt;
    sound_buffer_rec.dwFlags = 0 ;

    hr = IDirectSoundCapture_CreateCaptureBuffer (lpDS2r, &sound_buffer_rec, &lpDSB2r, NULL);
    if (FAILED (hr)) {
	write_log (_T("AHI: CreateCaptureSoundBuffer() failure: %s\n"), DXError(hr));
	record_enabled = -1;
	return 0;
    }

    hr = IDirectSoundCaptureBuffer_Start (lpDSB2r, DSCBSTART_LOOPING);
    if (FAILED (hr)) {
	write_log (_T("AHI: DirectSoundCaptureBuffer_Start failed: %s\n"), DXError (hr));
	record_enabled = -1;
	return 0;
    }
    record_enabled = 1;
    write_log (_T("AHI: Init AHI Audio Recording \n"));
    return 1;
}

void setvolume_ahi (int vol)
{
    set_volume_sound_device (sdp, vol, 0);
#if 0
    HRESULT hr;

    if (!lpDS2)
	return;
    hr = IDirectSoundBuffer_SetVolume (lpDSB2, vol);
    if (FAILED (hr))
	write_log (_T("AHI: SetVolume(%d) failed: %s\n"), vol, DXError (hr));
#endif
}

static int ahi_init_sound_win32 (void)
{
    int num, ret;
#if 0
    HRESULT hr;
    DSBUFFERDESC sound_buffer;
    DSCAPS DSCaps;
#endif

    ret = 0;
    if (!amigablksize)
	return 0;

    soundneutral = 0;
    ahisndbufsize = (amigablksize * AMIGASAMPLESIZE) * NATIVEBUFFNUM;  // use 4 native buffer
    ahisndbuffer = malloc (ahisndbufsize + 32);
    if (!ahisndbuffer)
	return 0;

    num = enumerate_sound_devices ();
    if (currprefs.win32_soundcard >= num)
	currprefs.win32_soundcard = changed_prefs.win32_soundcard = 0;
    ret = open_sound_device (sdp, currprefs.win32_soundcard, amigablksize * AMIGASAMPLESIZE, sound_freq_ahi, sound_channels_ahi);
    if (!ret)
	return 0;

    write_log (_T("AHI: Init AHI Sound Rate %d, Channels %d, Bits %d, Buffsize %d\n"),
	sound_freq_ahi, sound_channels_ahi, sound_bits_ahi, amigablksize);

    wavfmt.wFormatTag = WAVE_FORMAT_PCM;
    wavfmt.nChannels = sound_channels_ahi;
    wavfmt.nSamplesPerSec = sound_freq_ahi;
    wavfmt.wBitsPerSample = sound_bits_ahi;
    wavfmt.nBlockAlign = wavfmt.wBitsPerSample / 8 * wavfmt.nChannels;
    wavfmt.nAvgBytesPerSec = wavfmt.nBlockAlign * sound_freq_ahi;
    wavfmt.cbSize = 0;

#if 0
    if (sound_devices[currprefs.win32_soundcard].type != SOUND_DEVICE_DS)
	hr = DirectSoundCreate (NULL, &lpDS2, NULL);
    else
	hr = DirectSoundCreate (&sound_devices[currprefs.win32_soundcard].guid, &lpDS2, NULL);
    if (FAILED (hr)) {
	write_log (_T("AHI: DirectSoundCreate() failure: %s\n"), DXError (hr));
	return 0;
    }
    memset (&sound_buffer, 0, sizeof (DSBUFFERDESC));
    sound_buffer.dwSize = sizeof (DSBUFFERDESC);
    sound_buffer.dwFlags = DSBCAPS_PRIMARYBUFFER;
    sound_buffer.dwBufferBytes = 0;
    sound_buffer.lpwfxFormat = NULL;

    DSCaps.dwSize = sizeof(DSCAPS);
    hr = IDirectSound_GetCaps (lpDS2, &DSCaps);
    if (SUCCEEDED (hr)) {
	if (DSCaps.dwFlags & DSCAPS_EMULDRIVER)
	    write_log (_T("AHI: Your DirectSound Driver is emulated via WaveOut - yuck!\n"));
    }
    if (FAILED (IDirectSound_SetCooperativeLevel (lpDS2, hMainWnd, DSSCL_PRIORITY)))
	return 0;
    hr = IDirectSound_CreateSoundBuffer (lpDS2, &sound_buffer, &lpDSBprimary2, NULL);
    if (FAILED (hr)) {
	write_log (_T("AHI: CreateSoundBuffer() failure: %s\n"), DXError(hr));
	return 0;
    }
    hr = IDirectSoundBuffer_SetFormat (lpDSBprimary2, &wavfmt);
    if (FAILED (hr)) {
	write_log (_T("AHI: SetFormat() failure: %s\n"), DXError (hr));
	return 0;
    }
    sound_buffer.dwBufferBytes = ahisndbufsize;
    sound_buffer.lpwfxFormat = &wavfmt;
    sound_buffer.dwFlags = DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLVOLUME
	| DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS | DSBCAPS_LOCSOFTWARE;
    sound_buffer.guid3DAlgorithm = GUID_NULL;
    hr = IDirectSound_CreateSoundBuffer (lpDS2, &sound_buffer, &lpDSB2, NULL);
    if (FAILED (hr)) {
	write_log (_T("AHI: CreateSoundBuffer() failure: %s\n"), DXError (hr));
	return 0;
    }

    hr = IDirectSoundBuffer_GetFormat (lpDSBprimary2,&wavfmt,500,0);
    if (FAILED (hr)) {
	write_log (_T("AHI: GetFormat() failure: %s\n"), DXError (hr));
	return 0;
    }
#endif

    setvolume_ahi (0);

    ahisndbufpt = ahisndbuffer;
    sndptrmax = ahisndbuffer + ahisndbufsize;
    memset (ahisndbuffer, soundneutral, amigablksize * 8);
    ahi_on = 1;
    intcount = 0;
    return sound_freq_ahi;
}

int ahi_open_sound (void)
{
    int rate;

    uaevar.changenum++;
    if (!sound_freq_ahi)
	return 0;
    if (ahi_on)
	ahi_close_sound ();
    sound_flushes2 = 1;
    if ((rate = ahi_init_sound_win32 ()))
	return rate;
    return 0;
}


static void *bswap_buffer = NULL;
static uae_u32 bswap_buffer_size = 0;
static double syncdivisor;

uae_u32 REGPARAM2 ahi_demux (TrapContext *context)
{
//use the extern int (6 #13)
// d0 0=opensound      d1=unit d2=samplerate d3=blksize ret: sound frequency
// d0 6=opensound_new  d1=unit d2=samplerate d3=blksize ret d4=channels d5=bits d6=zero: sound frequency
// d0 1=closesound     d1=unit
// d0 2=writesamples   d1=unit a0=addr     write blksize samples to card
// d0 3=readsamples    d1=unit a0=addr     read samples from card ret: d0=samples read
	// make sure you have from amigaside blksize*4 mem alloced
	// d0=-1 no data available d0=-2 no recording open
	// d0 > 0 there are more blksize Data in the que
	// do the loop until d0 get 0
	// if d0 is greater than 200 bring a message
	// that show the user that data is lost
	// maximum blocksbuffered are 250 (8,5 sec)
// d0 4=writeinterrupt d1=unit d0=0 no interrupt happen for this unit
	// d0=-2 no playing open
	//note units for now not support use only unit 0
// d0 5=?
    // d0=10 get clipboard size  d0=size in bytes
// d0=11 get clipboard data  a0=clipboarddata
				  //Note: a get clipboard size must do before
// d0=12 write clipboard data	 a0=clipboarddata
// d0=13 setp96mouserate	 d1=hz value
// d0=100 open dll		 d1=dll name in windows name conventions
// d0=101 get dll function addr	 d1=dllhandle a0 function/var name
// d0=102 exec dllcode		 a0=addr of function (see 101)
// d0=103 close dll
// d0=104 screenlost
// d0=105 mem offset
// d0=106 16Bit byteswap
// d0=107 32Bit byteswap
// d0=108 free swap array
// d0=200 ahitweak		 d1=offset for dsound position pointer

    int opcode = m68k_dreg (&context->regs, 0);

    switch (opcode)
    {
	uae_u32 src, num_vars;
	static int cap_pos, clipsize;
	static TCHAR *clipdat;

	case 0:
	    cap_pos = 0;
	    sound_bits_ahi = 16;
	    sound_channels_ahi = 2;
	    sound_freq_ahi = m68k_dreg (&context->regs, 2);
	    amigablksize = m68k_dreg (&context->regs, 3);
	    sound_freq_ahi = ahi_open_sound();
	    uaevar.changenum--;
	return sound_freq_ahi;
	case 6: /* new open function */
	    cap_pos = 0;
	    sound_freq_ahi = m68k_dreg (&context->regs, 2);
	    amigablksize = m68k_dreg (&context->regs, 3);
	    sound_channels_ahi = m68k_dreg (&context->regs, 4);
	    sound_bits_ahi = m68k_dreg (&context->regs, 5);
	    sound_freq_ahi = ahi_open_sound();
	    uaevar.changenum--;
	return sound_freq_ahi;

	case 1:
	    ahi_close_sound();
	    sound_freq_ahi = 0;
	return 0;

	case 2:
	{
	    int i;
	    uaecptr addr = m68k_areg (&context->regs, 0);
	    if (valid_address (addr, amigablksize * AMIGASAMPLESIZE)) {
		uae_u8 *src = get_real_address (addr);
		for (i = 0; i < amigablksize; i++) {
		    if (ahisndbufpt >= sndptrmax)
			break;
		    *ahisndbufpt++ = src[1];
		    *ahisndbufpt++ = src[0];
		    *ahisndbufpt++ = src[3];
		    *ahisndbufpt++ = src[2];
		    src += 4;
		}
	    }
	    ahi_finish_sound_buffer ();
	}
	return amigablksize;

	case 3:
	{
	    LPTSTR pos1,pos2;
	    uaecptr addr;
	    HRESULT hr;
	    int i, t, todo, byte1, byte2, cur_pos;

	    if (!ahi_on)
		return -2;
	    if (record_enabled == 0)
		ahi_init_record_win32 ();
	    if (record_enabled < 0)
		return -2;
	    hr = IDirectSoundCaptureBuffer_GetCurrentPosition (lpDSB2r, &t, &cur_pos);
	    if (FAILED(hr))
		return -1;

	    t =  amigablksize * 4;
	    if (cap_pos <= cur_pos)
		todo = cur_pos - cap_pos;
	    else
		todo = cur_pos + (RECORDBUFFER * t) - cap_pos;
	    if (todo < t) //if no complete buffer ready exit
		return -1;
	    hr = IDirectSoundCaptureBuffer_Lock (lpDSB2r, cap_pos, t, &pos1, &byte1, &pos2, &byte2, 0);
	    if (FAILED(hr))
		return -1;
	    if ((cap_pos + t) < (t * RECORDBUFFER))
		cap_pos = cap_pos + t;
	    else
		cap_pos = 0;
	    addr = m68k_areg (&context->regs, 0);
	    sndbufrecpt = (unsigned int*)pos1;
	    t /= 4;
	    for (i = 0; i < t; i++) {
		put_long (addr, *sndbufrecpt++);
		addr += 4;
	    }
	    t *= 4;
	    IDirectSoundCaptureBuffer_Unlock (lpDSB2r, pos1, byte1, pos2, byte2);
	    return (todo - t) / t;
	}

	case 4:
	{
	    int i;
	    if (!ahi_on)
		return -2;
	    i = intcount;
	    intcount = 0;
	    return i;
	}

	case 5:
	    if (!ahi_on)
		return 0;
	    ahi_updatesound (1);
	return 1;

	case 10:
#if 1
	    if (OpenClipboard (0)) {
		clipdat = GetClipboardData (CF_TEXT);
		if (clipdat) {
		    clipsize = _tcslen (clipdat);
		    clipsize++;
		    return clipsize;
		}
	    }
#endif
	    return 0;

	case 11:
	{
#if 1
	    int i;
	    for (i = 0; i < clipsize; i++)
		put_byte (m68k_areg (&context->regs, 0) + i, clipdat[i]);
	    CloseClipboard ();
#endif
	}
	return 0;

	case 12:
	{
#if 1
	    TCHAR *s = au (get_real_address (m68k_areg (&regs, 0)));
	    static LPTSTR p;
	    int slen;

	    if (OpenClipboard (0)) {
		EmptyClipboard();
		slen = _tcslen (s);
		if (p)
		    GlobalFree (p);
		p = GlobalAlloc (GMEM_MOVEABLE, (slen + 1) * sizeof (TCHAR));
		if (p) {
		    TCHAR *p2 = GlobalLock (p);
		    if (p2) {
			_tcscpy (p2, s);
			GlobalUnlock (p);
			SetClipboardData (CF_TEXT, p);
		    }
		}
		CloseClipboard ();
	    }
	    xfree (s);
#endif
	}
	return 0;

	case 13: /* HACK */
	{ //for higher P96 mouse draw rate
	    extern int p96hack_vpos2, hack_vpos ,p96refresh_active;
	    extern uae_u16 vtotal;
	    extern unsigned int new_beamcon0;
	    p96hack_vpos2 = 0;
	    if (m68k_dreg (&context->regs, 1) > 0)
		p96hack_vpos2 = 15625 / m68k_dreg (&context->regs, 1);
	    if (!currprefs.cs_ciaatod)
		changed_prefs.cs_ciaatod = currprefs.cs_ciaatod = currprefs.ntscmode ? 2 : 1;
	    p96refresh_active=1;
	    picasso_refresh ();
	} //end for higher P96 mouse draw rate
	return 0;

	case 20:
	return enforcer_enable(m68k_dreg (&context->regs, 1));

	case 21:
	return enforcer_disable();

	case 25:
	    flushprinter ();
	return 0;

#if defined(X86_MSVC_ASSEMBLY)

	case 100: // open dll
	{
	    TCHAR *dlldir = TEXT ("winuae_dll");
	    TCHAR *dllname;
	    uaecptr dllptr;
	    HMODULE h = NULL;
	    TCHAR dpath[MAX_DPATH];
	    TCHAR newdllpath[MAX_DPATH];
	    int ok = 0;
	    TCHAR *filepart;

	    dllptr = m68k_areg (&context->regs, 0);
	    dllname = au ((uae_char*)get_real_address (dllptr));
	    dpath[0] = 0;
	    GetFullPathName (dllname, sizeof dpath / sizeof (TCHAR), dpath, &filepart);
	    if (_tcslen (dpath) > _tcslen (start_path_data) && !_tcsncmp (dpath, start_path_data, _tcslen (start_path_data))) {
		/* path really is relative to winuae directory */
		ok = 1;
		_tcscpy (newdllpath, dpath + _tcslen (start_path_data));
		if (!_tcsncmp (newdllpath, dlldir, _tcslen (dlldir))) /* remove "winuae_dll" */
		    _tcscpy (newdllpath, dpath + _tcslen (start_path_data) + 1 + _tcslen (dlldir));
		_stprintf (dpath, _T("%s%s%s"), start_path_data, WIN32_PLUGINDIR, newdllpath);
		h = LoadLibrary (dpath);
		if (h == NULL)
		    write_log (_T("native open: '%s' = %d\n"), dpath, GetLastError ());
		if (h == NULL) {
		    _stprintf (dpath, _T("%s%s\\%s"), start_path_data, dlldir, newdllpath);
		    h = LoadLibrary (dllname);
		    if (h == NULL)
			write_log (_T("fallback native open: '%s' = %d\n"), dpath, GetLastError ());
		}
	    } else {
		write_log (_T("native open outside of installation dir '%s'!\n"), dpath);
	    }
	    xfree (dllname);
#if 0
	    if (h == NULL) {
		h = LoadLibrary (filepart);
		write_log (_T("native file open: '%s' = %p\n"), filepart, h);
		if (h == NULL) {
		    _stprintf (dpath, "%s%s%s", start_path_data, WIN32_PLUGINDIR, filepart);
		    h = LoadLibrary (dpath);
		    write_log (_T("native path open: '%s' = %p\n"), dpath, h);
		}
	    }
#endif
	    syncdivisor = (3580000.0 * CYCLE_UNIT) / (double)syncbase;
	    return (uae_u32)h;
	}

	case 101:	//get dll label
	{
	    HMODULE m;
	    uaecptr funcaddr;
	    char *funcname;
	    m = (HMODULE) m68k_dreg (&context->regs, 1);
	    funcaddr = m68k_areg (&context->regs, 0);
	    funcname = get_real_address (funcaddr);
	    return (uae_u32) GetProcAddress (m, funcname);
	}

	case 102:	//execute native code
	{
	    uae_u32 ret;
	    unsigned long rate1;
	    double v;
	    rate1 = read_processor_time ();
	    ret = emulib_ExecuteNativeCode2 (context);
	    rate1 = read_processor_time () - rate1;
	    v = syncdivisor * rate1;
	    if (v > 0) {
		if (v > 1000000 * CYCLE_UNIT)
		    v = 1000000 * CYCLE_UNIT;
		do_extra_cycles ((unsigned long)(syncdivisor * rate1)); //compensate the time stay in native func
	    }
	    return ret;
	}

	case 103:	//close dll
	{
	    HMODULE libaddr;
	    libaddr = (HMODULE) m68k_dreg (&context->regs, 1);
	    FreeLibrary (libaddr);
	    return 0;
	}
#endif

	case 104:        //screenlost
	{
	    static int oldnum = 0;
	    if (uaevar.changenum == oldnum)
		return 0;
	    oldnum = uaevar.changenum;
	    return 1;
	}

#if defined(X86_MSVC_ASSEMBLY)

	case 105:   //returns memory offset
	    return (uae_u32) get_real_address (0);

	case 106:   //byteswap 16bit vars
		    //a0 = start address
		    //d1 = number of 16bit vars
		    //returns address of new array
	    src = m68k_areg (&context->regs, 0);
	    num_vars = m68k_dreg (&context->regs, 1);

	    if (bswap_buffer_size < num_vars * 2) {
		bswap_buffer_size = (num_vars + 1024) * 2;
		free(bswap_buffer);
		bswap_buffer = (void*)malloc(bswap_buffer_size);
	    }
	    __asm {
		mov esi, dword ptr [src]
		mov edi, dword ptr [bswap_buffer]
		mov ecx, num_vars

		mov ebx, ecx
		and ecx, 3
		je BSWAP_WORD_4X

		BSWAP_WORD_LOOP:
		mov ax, [esi]
		mov dl, al
		mov al, ah
		mov ah, dl
		mov [edi], ax
		add esi, 2
		add edi, 2
		loopne BSWAP_WORD_LOOP

		BSWAP_WORD_4X:
		mov ecx, ebx
		shr ecx, 2
		je BSWAP_WORD_END
		BSWAP_WORD_4X_LOOP:
		mov ax, [esi]
		mov dl, al
		mov al, ah
		mov ah, dl
		mov [edi], ax
		mov ax, [esi+2]
		mov dl, al
		mov al, ah
		mov ah, dl
		mov [edi+2], ax
		mov ax, [esi+4]
		mov dl, al
		mov al, ah
		mov ah, dl
		mov [edi+4], ax
		mov ax, [esi+6]
		mov dl, al
		mov al, ah
		mov ah, dl
		mov [edi+6], ax
		add esi, 8
		add edi, 8
		loopne BSWAP_WORD_4X_LOOP
		BSWAP_WORD_END:
	    }
	return (uae_u32) bswap_buffer;

	case 107:   //byteswap 32bit vars - see case 106
		    //a0 = start address
		    //d1 = number of 32bit vars
		    //returns address of new array
	    src = m68k_areg (&context->regs, 0);
	    num_vars = m68k_dreg (&context->regs, 1);
	    if (bswap_buffer_size < num_vars * 4) {
		bswap_buffer_size = (num_vars + 16384) * 4;
		free(bswap_buffer);
		bswap_buffer = (void*)malloc(bswap_buffer_size);
	    }
	    __asm {
		mov esi, dword ptr [src]
		mov edi, dword ptr [bswap_buffer]
		mov ecx, num_vars

		mov ebx, ecx
		and ecx, 3
		je BSWAP_DWORD_4X

		BSWAP_DWORD_LOOP:
		mov eax, [esi]
		bswap eax
		mov [edi], eax
		add esi, 4
		add edi, 4
		loopne BSWAP_DWORD_LOOP

		BSWAP_DWORD_4X:
		mov ecx, ebx
		shr ecx, 2
		je BSWAP_DWORD_END
		BSWAP_DWORD_4X_LOOP:
		mov eax, [esi]
		bswap eax
		mov [edi], eax
		mov eax, [esi+4]
		bswap eax
		mov [edi+4], eax
		mov eax, [esi+8]
		bswap eax
		mov [edi+8], eax
		mov eax, [esi+12]
		bswap eax
		mov [edi+12], eax
		add esi, 16
		add edi, 16
		loopne BSWAP_DWORD_4X_LOOP

		BSWAP_DWORD_END:
	    }
	return (uae_u32) bswap_buffer;

	case 108: //frees swap array
	    bswap_buffer_size = 0;
	    free (bswap_buffer);
	    bswap_buffer = NULL;
	return 0;
#endif

	case 200:
	    ahitweak = m68k_dreg (&context->regs, 1);
	    ahi_pollrate = m68k_dreg (&context->regs, 2);
	    if (ahi_pollrate < 10)
		ahi_pollrate = 10;
	    if (ahi_pollrate > 60)
		ahi_pollrate = 60;
	return 1;

	default:
	return 0x12345678;     // Code for not supportet function
    }
}

#endif
