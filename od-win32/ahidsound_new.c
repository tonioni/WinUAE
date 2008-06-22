/*
 * UAE - The Un*x Amiga Emulator
 *
 * DirectSound AHI wrapper
 *
 * Copyright 2008 Toni Wilen
 */

#include "sysconfig.h"

#if defined(AHI)

#include <ctype.h>
#include <assert.h>

#include <windows.h>
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
#include "autoconf.h"
#include "traps.h"
#include "od-win32/win32.h"
#include "sounddep/sound.h"
#include "ahidsound_new.h"
#include "dxwrap.h"

#include <mmsystem.h>
#include <mmreg.h>
#include <dsound.h>
#include <ks.h>
#include <ksmedia.h>


#define ahiac_AudioCtrl 0
#define ahiac_Flags ahiac_AudioCtrl + 4
#define ahiac_SoundFunc ahiac_Flags + 4
#define ahiac_PlayerFunc ahiac_SoundFunc + 4
#define ahiac_PlayerFreq ahiac_PlayerFunc + 4
#define ahiac_MinPlayerFreq ahiac_PlayerFreq + 4
#define ahiac_MaxPlayerFreq ahiac_MinPlayerFreq + 4
#define ahiac_MixFreq ahiac_MaxPlayerFreq + 4
#define ahiac_Channels ahiac_MixFreq + 4
#define ahiac_Sounds ahiac_Channels + 2
#define ahiac_DriverData ahiac_Sounds + 2
#define ahiac_MixerFunc ahiac_DriverData + 4
#define ahiac_SamplerFunc ahiac_MixerFunc + 4 
#define ahiac_Obsolete ahiac_SamplerFunc + 4 
#define ahiac_BuffSamples ahiac_Obsolete + 4 
#define ahiac_MinBuffSamples ahiac_BuffSamples + 4 
#define ahiac_MaxBuffSamples ahiac_MinBuffSamples + 4 
#define ahiac_BuffSize ahiac_MaxBuffSamples + 4 
#define ahiac_BuffType ahiac_BuffSize + 4 
#define ahiac_PreTimer ahiac_BuffType + 4 
#define ahiac_PostTimer ahiac_PreTimer + 4 
#define ahiac_AntiClickSamples ahiac_PostTimer + 4 
#define ahiac_PreTimerFunc ahiac_AntiClickSamples + 4 
#define ahiac_PostTimerFunc ahiac_PreTimerFunc + 4 

#if 0
struct AHISampleInfo
{
	ULONG	ahisi_Type;			/* Format of samples */
	APTR	ahisi_Address;			/* Address to array of samples */
	ULONG	ahisi_Length;			/* Number of samples in array */
};
#endif

#define ahisi_Type 0
#define ahisi_Address 4
#define ahisi_Length 8

#if 0
struct AHIAudioCtrlDrv
{
	struct AHIAudioCtrl ahiac_AudioCtrl;
	ULONG	     ahiac_Flags;		/* See below for definition	*/
	struct Hook *ahiac_SoundFunc;		/* AHIA_SoundFunc		*/
	struct Hook *ahiac_PlayerFunc;		/* AHIA_PlayerFunc		*/
	Fixed	     ahiac_PlayerFreq;		/* AHIA_PlayerFreq		*/
	Fixed	     ahiac_MinPlayerFreq;	/* AHIA_MinPlayerFreq		*/
	Fixed	     ahiac_MaxPlayerFreq;	/* AHIA_MaxPlayerFreq		*/
	ULONG	     ahiac_MixFreq;		/* AHIA_MixFreq			*/
	UWORD	     ahiac_Channels;		/* AHIA_Channels		*/
	UWORD	     ahiac_Sounds;		/* AHIA_Sounds			*/

	APTR	     ahiac_DriverData;		/* Unused. Store whatever you want here. */

	struct Hook *ahiac_MixerFunc;		/* Mixing routine Hook		*/
	struct Hook *ahiac_SamplerFunc;		/* Sampler routine Hook		*/
	ULONG	     ahiac_Obsolete;
	ULONG	     ahiac_BuffSamples;		/* Samples to mix this pass.	*/
	ULONG	     ahiac_MinBuffSamples;	/* Min. samples to mix each pass. */
	ULONG	     ahiac_MaxBuffSamples;	/* Max. samples to mix each pass. */
	ULONG	     ahiac_BuffSize;		/* Buffer size ahiac_MixerFunc needs. */
	ULONG	     ahiac_BuffType;		/* Buffer format (V2)		*/
	BOOL	   (*ahiac_PreTimer)(void);	/* Call before mixing (V4)	*/
	void	   (*ahiac_PostTimer)(void);	/* Call after mixing (V4)	*/
	ULONG	     ahiac_AntiClickSamples;	/* AntiClick samples (V6)	*/
	struct Hook *ahiac_PreTimerFunc;        /* A Hook wrapper for ahiac_PreTimer (V6) */
	struct Hook *ahiac_PostTimerFunc;       /* A Hook wrapper for ahiac_PostTimer (V6) */
#endif

/* AHIsub_AllocAudio return flags */
#define AHISF_ERROR		(1<<0)
#define AHISF_MIXING		(1<<1)
#define AHISF_TIMING		(1<<2)
#define AHISF_KNOWSTEREO	(1<<3)
#define AHISF_KNOWHIFI		(1<<4)
#define AHISF_CANRECORD 	(1<<5)
#define AHISF_CANPOSTPROCESS	(1<<6)
#define AHISF_KNOWMULTICHANNEL	(1<<7)

#define AHISB_ERROR		(0)
#define AHISB_MIXING		(1)
#define AHISB_TIMING		(2)
#define AHISB_KNOWSTEREO	(3)
#define AHISB_KNOWHIFI		(4)
#define AHISB_CANRECORD		(5)
#define AHISB_CANPOSTPROCESS	(6)
#define AHISB_KNOWMULTICHANNEL	(7)

 /* AHIsub_Start() and AHIsub_Stop() flags */
#define	AHISF_PLAY		(1<<0)
#define	AHISF_RECORD		(1<<1)

#define	AHISB_PLAY		(0)
#define	AHISB_RECORD		(1)

 /* ahiac_Flags */
#define	AHIACF_VOL		(1<<0)
#define	AHIACF_PAN		(1<<1)
#define	AHIACF_STEREO		(1<<2)
#define	AHIACF_HIFI		(1<<3)
#define	AHIACF_PINGPONG		(1<<4)
#define	AHIACF_RECORD		(1<<5)
#define AHIACF_MULTTAB  	(1<<6)
#define	AHIACF_MULTICHANNEL	(1<<7)

#define	AHIACB_VOL		(0)
#define	AHIACB_PAN		(1)
#define	AHIACB_STEREO		(2)
#define	AHIACB_HIFI		(3)
#define	AHIACB_PINGPONG		(4)
#define	AHIACB_RECORD		(5)
#define AHIACB_MULTTAB  	(6)
#define	AHIACB_MULTICHANNEL	(7)

#define AHI_TagBase		(0x80000000)
#define AHI_TagBaseR		(AHI_TagBase|0x8000)

 /* AHI_AllocAudioA tags */
#define AHIA_AudioID		(AHI_TagBase+1)		/* Desired audio mode */
#define AHIA_MixFreq		(AHI_TagBase+2)		/* Suggested mixing frequency */
#define AHIA_Channels		(AHI_TagBase+3)		/* Suggested number of channels */
#define AHIA_Sounds		(AHI_TagBase+4)		/* Number of sounds to use */
#define AHIA_SoundFunc		(AHI_TagBase+5)		/* End-of-Sound Hook */
#define AHIA_PlayerFunc		(AHI_TagBase+6)		/* Player Hook */
#define AHIA_PlayerFreq		(AHI_TagBase+7)		/* Frequency for player Hook (Fixed)*/
#define AHIA_MinPlayerFreq	(AHI_TagBase+8)		/* Minimum Frequency for player Hook */
#define AHIA_MaxPlayerFreq	(AHI_TagBase+9)		/* Maximum Frequency for player Hook */
#define AHIA_RecordFunc		(AHI_TagBase+10)	/* Sample recording Hook */
#define AHIA_UserData		(AHI_TagBase+11)	/* What to put in ahiac_UserData */
#define AHIA_AntiClickSamples	(AHI_TagBase+13)	/* # of samples to smooth (V6)	*/

  /* AHI_PlayA tags (V4) */
#define AHIP_BeginChannel	(AHI_TagBase+40)	/* All command tags should be... */
#define AHIP_EndChannel		(AHI_TagBase+41)	/* ... enclosed by these tags. */
#define AHIP_Freq		(AHI_TagBase+50)
#define AHIP_Vol		(AHI_TagBase+51)
#define AHIP_Pan		(AHI_TagBase+52)
#define AHIP_Sound		(AHI_TagBase+53)
#define AHIP_Offset		(AHI_TagBase+54)
#define AHIP_Length		(AHI_TagBase+55)
#define AHIP_LoopFreq		(AHI_TagBase+60)
#define AHIP_LoopVol		(AHI_TagBase+61)
#define AHIP_LoopPan		(AHI_TagBase+62)
#define AHIP_LoopSound		(AHI_TagBase+63)
#define AHIP_LoopOffset		(AHI_TagBase+64)
#define AHIP_LoopLength		(AHI_TagBase+65)

 /* AHI_ControlAudioA tags */
#define AHIC_Play		(AHI_TagBase+80)	/* Boolean */
#define AHIC_Record		(AHI_TagBase+81)	/* Boolean */
#define AHIC_MonitorVolume	(AHI_TagBase+82)
#define AHIC_MonitorVolume_Query (AHI_TagBase+83)	/* ti_Data is pointer to Fixed (LONG) */
#define AHIC_MixFreq_Query	(AHI_TagBase+84)	/* ti_Data is pointer to ULONG */
/* --- New for V2, they will be ignored by V1 --- */
#define AHIC_InputGain		(AHI_TagBase+85)
#define AHIC_InputGain_Query	(AHI_TagBase+86)	/* ti_Data is pointer to Fixed (LONG) */
#define AHIC_OutputVolume	(AHI_TagBase+87)
#define AHIC_OutputVolume_Query	(AHI_TagBase+88)	/* ti_Data is pointer to Fixed (LONG) */
#define AHIC_Input		(AHI_TagBase+89)
#define AHIC_Input_Query	(AHI_TagBase+90)	/* ti_Data is pointer to ULONG */
#define AHIC_Output		(AHI_TagBase+91)
#define AHIC_Output_Query	(AHI_TagBase+92)	/* ti_Data is pointer to ULONG */

 /* AHI_GetAudioAttrsA tags */
#define AHIDB_AudioID		(AHI_TagBase+100)
#define AHIDB_Driver		(AHI_TagBaseR+101)	/* Pointer to name of driver */
#define AHIDB_Flags		(AHI_TagBase+102)	/* Private! */
#define AHIDB_Volume		(AHI_TagBase+103)	/* Boolean */
#define AHIDB_Panning		(AHI_TagBase+104)	/* Boolean */
#define AHIDB_Stereo		(AHI_TagBase+105)	/* Boolean */
#define AHIDB_HiFi		(AHI_TagBase+106)	/* Boolean */
#define AHIDB_PingPong		(AHI_TagBase+107)	/* Boolean */
#define AHIDB_MultTable		(AHI_TagBase+108)	/* Private! */
#define AHIDB_Name		(AHI_TagBaseR+109)	/* Pointer to name of this mode */
#define AHIDB_Bits		(AHI_TagBase+110)	/* Output bits */
#define AHIDB_MaxChannels	(AHI_TagBase+111)	/* Max supported channels */
#define AHIDB_MinMixFreq	(AHI_TagBase+112)	/* Min mixing freq. supported */
#define AHIDB_MaxMixFreq	(AHI_TagBase+113)	/* Max mixing freq. supported */
#define AHIDB_Record		(AHI_TagBase+114)	/* Boolean */
#define AHIDB_Frequencies	(AHI_TagBase+115)
#define AHIDB_FrequencyArg	(AHI_TagBase+116)	/* ti_Data is frequency index */
#define AHIDB_Frequency		(AHI_TagBase+117)
#define AHIDB_Author		(AHI_TagBase+118)	/* Pointer to driver author name */
#define AHIDB_Copyright		(AHI_TagBase+119)	/* Pointer to driver copyright notice */
#define AHIDB_Version		(AHI_TagBase+120)	/* Pointer to driver version string */
#define AHIDB_Annotation	(AHI_TagBase+121)	/* Pointer to driver annotation text */
#define AHIDB_BufferLen		(AHI_TagBase+122)	/* Specifies the string buffer size */
#define AHIDB_IndexArg		(AHI_TagBase+123)	/* ti_Data is frequency! */
#define AHIDB_Index		(AHI_TagBase+124)
#define AHIDB_Realtime		(AHI_TagBase+125)	/* Boolean */
#define AHIDB_MaxPlaySamples	(AHI_TagBase+126)	/* It's sample *frames* */
#define AHIDB_MaxRecordSamples	(AHI_TagBase+127)	/* It's sample *frames* */
#define AHIDB_FullDuplex	(AHI_TagBase+129)	/* Boolean */
/* --- New for V2, they will be ignored by V1 --- */
#define AHIDB_MinMonitorVolume	(AHI_TagBase+130)
#define AHIDB_MaxMonitorVolume	(AHI_TagBase+131)
#define AHIDB_MinInputGain	(AHI_TagBase+132)
#define AHIDB_MaxInputGain	(AHI_TagBase+133)
#define AHIDB_MinOutputVolume	(AHI_TagBase+134)
#define AHIDB_MaxOutputVolume	(AHI_TagBase+135)
#define AHIDB_Inputs		(AHI_TagBase+136)
#define AHIDB_InputArg		(AHI_TagBase+137)	/* ti_Data is input index */
#define AHIDB_Input		(AHI_TagBase+138)
#define AHIDB_Outputs		(AHI_TagBase+139)
#define AHIDB_OutputArg		(AHI_TagBase+140)	/* ti_Data is input index */
#define AHIDB_Output		(AHI_TagBase+141)
/* --- New for V4, they will be ignored by V2 and earlier --- */
#define AHIDB_Data		(AHI_TagBaseR+142)	/* Private! */
#define AHIDB_DriverBaseName	(AHI_TagBaseR+143)	/* Private! */
/* --- New for V6, they will be ignored by V4 and earlier --- */
#define AHIDB_MultiChannel	(AHI_TagBase+144)	/* Boolean */

 /* AHI_BestAudioIDA tags */
/* --- New for V4, they will be ignored by V2 and earlier --- */
#define AHIB_Dizzy		(AHI_TagBase+190)

 /* AHI_AudioRequestA tags */
	/* Window control */
#define AHIR_Window		(AHI_TagBase+200)	/* Parent window */
#define AHIR_Screen		(AHI_TagBase+201)	/* Screen to open on if no window */
#define AHIR_PubScreenName	(AHI_TagBase+202)	/* Name of public screen */
#define AHIR_PrivateIDCMP	(AHI_TagBase+203)	/* Allocate private IDCMP? */
#define AHIR_IntuiMsgFunc	(AHI_TagBase+204)	/* Function to handle IntuiMessages */
#define AHIR_SleepWindow	(AHI_TagBase+205)	/* Block input in AHIR_Window? */
#define AHIR_ObsoleteUserData	(AHI_TagBase+206)	/* V4 UserData */
#define AHIR_UserData		(AHI_TagBase+207)	/* What to put in ahiam_UserData (V6) */
	/* Text display */
#define AHIR_TextAttr		(AHI_TagBase+220)	/* Text font to use for gadget text */
#define AHIR_Locale		(AHI_TagBase+221)	/* Locale to use for text */
#define AHIR_TitleText		(AHI_TagBase+222)	/* Title of requester */
#define AHIR_PositiveText	(AHI_TagBase+223)	/* Positive gadget text */
#define AHIR_NegativeText	(AHI_TagBase+224)	/* Negative gadget text */
	/* Initial settings */
#define AHIR_InitialLeftEdge	(AHI_TagBase+240)	/* Initial requester coordinates */
#define AHIR_InitialTopEdge	(AHI_TagBase+241)
#define AHIR_InitialWidth	(AHI_TagBase+242)	/* Initial requester dimensions */
#define AHIR_InitialHeight	(AHI_TagBase+243)
#define AHIR_InitialAudioID	(AHI_TagBase+244)	/* Initial audio mode id */
#define AHIR_InitialMixFreq	(AHI_TagBase+245)	/* Initial mixing/sampling frequency */
#define AHIR_InitialInfoOpened	(AHI_TagBase+246)	/* Info window initially opened? */
#define AHIR_InitialInfoLeftEdge (AHI_TagBase+247)	/* Initial Info window coords. */
#define AHIR_InitialInfoTopEdge (AHI_TagBase+248)
#define AHIR_InitialInfoWidth	(AHI_TagBase+249)	/* Not used! */
#define AHIR_InitialInfoHeight	(AHI_TagBase+250)	/* Not used! */
	/* Options */
#define AHIR_DoMixFreq		(AHI_TagBase+260)	/* Allow selection of mixing frequency? */
#define AHIR_DoDefaultMode	(AHI_TagBase+261)	/* Allow selection of default mode? (V4) */
	/* Filtering */
#define AHIR_FilterTags		(AHI_TagBase+270)	/* Pointer to filter taglist */
#define AHIR_FilterFunc		(AHI_TagBase+271)	/* Function to filter mode id's */

/* Sound Types */
#define AHIST_NOTYPE		(~0UL)			/* Private */
#define AHIST_SAMPLE		(0UL)			/* 8 or 16 bit sample */
#define AHIST_DYNAMICSAMPLE	(1UL)			/* Dynamic sample */
#define AHIST_INPUT		(1UL<<29)		/* The input from your sampler */
#define AHIST_BW		(1UL<<30)		/* Private */

 /* Sample types */
/* Note that only AHIST_M8S, AHIST_S8S, AHIST_M16S and AHIST_S16S
   (plus AHIST_M32S, AHIST_S32S and AHIST_L7_1 in V6)
   are supported by AHI_LoadSound(). */
#define AHIST_M8S		(0UL)			/* Mono, 8 bit signed (BYTE) */
#define AHIST_M16S		(1UL)			/* Mono, 16 bit signed (WORD) */
#define AHIST_S8S		(2UL)			/* Stereo, 8 bit signed (2×BYTE) */
#define AHIST_S16S		(3UL)			/* Stereo, 16 bit signed (2×WORD) */
#define AHIST_M32S		(8UL)			/* Mono, 32 bit signed (LONG) */
#define AHIST_S32S		(10UL)			/* Stereo, 32 bit signed (2×LONG) */

#define AHIST_M8U		(4UL)			/* OBSOLETE! */
#define AHIST_L7_1		(0x00c3000aUL)		/* 7.1, 32 bit signed (8×LONG) */

 /* Error codes */
#define AHIE_OK			(0UL)			/* No error */
#define AHIE_NOMEM		(1UL)			/* Out of memory */
#define AHIE_BADSOUNDTYPE	(2UL)			/* Unknown sound type */
#define AHIE_BADSAMPLETYPE	(3UL)			/* Unknown/unsupported sample type */
#define AHIE_ABORTED		(4UL)			/* User-triggered abortion */
#define AHIE_UNKNOWN		(5UL)			/* Error, but unknown */
#define AHIE_HALFDUPLEX		(6UL)			/* CMD_WRITE/CMD_READ failure */

#define MAX_SAMPLES 64

struct dssample {
    int num;
    LPDIRECTSOUNDBUFFER8 dsb;
    LPDIRECTSOUNDBUFFER8 dsbback;
    int ch, chout;
    int bitspersample;
    int bitspersampleout;
    int freq;
    int volume;
    int bytespersample;
    int bytespersampleout;
    uae_u32 addr;
    uae_u32 len;
    uae_u32 type;
    int streaming;
};

struct DSAHI {
    uae_u32 audioid;
    int mixfreq;
    int channels;
    int sounds;
    uae_u32 soundfunc;
    uae_u32 playerfunc;
    int playerfreq;
    int minplayerfreq;
    int maxplayerfreq;
    uae_u32 recordfunc;
    uae_u32 userdata;
    int anticlicksamples;
    int enabledisable;
    struct dssample *sample;
    int playing, recording;
    int cansurround;
};

static struct DSAHI dsahi[1];
static int default_freq = 44100;
static uae_u32 xahi_author, xahi_copyright, xahi_version, xahi_output;

#define TAG_DONE   (0L)		/* terminates array of TagItems. ti_Data unused */
#define TAG_IGNORE (1L)		/* ignore this item, not end of array */
#define TAG_MORE   (2L)		/* ti_Data is pointer to another array of TagItems */
#define TAG_SKIP   (3L)		/* skip this and the next ti_Data items */
#define TAG_USER   ((uae_u32)(1L << 31))

static uae_u32 gettag (uae_u32 *tagpp, uae_u32 *datap)
{
    uae_u32 tagp = *tagpp;
    for (;;) {
	uae_u32 tag = get_long (tagp);
	uae_u32 data = get_long (tagp + 4);
	switch (tag)
	{
	    case TAG_DONE:
	    return 0;
	    case TAG_IGNORE:
	    tagp += 8;
	    break;
	    case TAG_MORE:
	    tagp = data;
	    break;
	    case TAG_SKIP:
	    tagp += data * 8;
	    break;
	    default:
	    tagp += 8;
	    *tagpp = tagp;
	    *datap = data;
	    return tag;
	}
    }
}

static LPDIRECTSOUND8 lpDS;
extern GUID sound_device_guid[];
const static GUID KSDATAFORMAT_SUBTYPE_PCM = {0x00000001,0x0000,0x0010,
    {0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};
#define KSAUDIO_SPEAKER_QUAD_SURROUND   (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | \
					 SPEAKER_SIDE_LEFT  | SPEAKER_SIDE_RIGHT)

static void ds_free (struct DSAHI *dsahip)
{
    int i;

    for (i = 0; i < dsahip->sounds; i++) {
	struct dssample *ds = &dsahip->sample[i];
	if (ds->dsb)
	    IDirectSoundBuffer8_Release (ds->dsb);
	memset (ds, 0, sizeof (ds));
    }
    if (lpDS)
	IDirectSound_Release (lpDS);
    lpDS = NULL;
}

DWORD fillsupportedmodes (LPDIRECTSOUND8 lpDS, int freq, struct dsaudiomodes *dsam);
static struct dsaudiomodes supportedmodes[16];

static int ds_init (struct DSAHI *dsahip)
{
    int freq = 48000;
    DSCAPS DSCaps;
    HRESULT hr;
    DWORD speakerconfig;

    hr = DirectSoundCreate8 (&sound_device_guid[currprefs.win32_soundcard], &lpDS, NULL);
    if (FAILED (hr))  {
	write_log ("AHI: DirectSoundCreate8() failure: %s\n", DXError (hr));
	return 0;
    }

    hr = IDirectSound_SetCooperativeLevel (lpDS, hMainWnd, DSSCL_PRIORITY);
    if (FAILED (hr)) {
	write_log ("AHI: Can't set cooperativelevel: %s\n", DXError (hr));
	goto error;
    }

    fillsupportedmodes (lpDS, default_freq, supportedmodes);
    if (SUCCEEDED (IDirectSound8_GetSpeakerConfig (lpDS, &speakerconfig))) {
	if (speakerconfig > DSSPEAKER_STEREO)
	    dsahip->cansurround = 1;
    }

    memset (&DSCaps, 0, sizeof (DSCaps));
    DSCaps.dwSize = sizeof (DSCaps);
    hr = IDirectSound_GetCaps (lpDS, &DSCaps);
    if (FAILED(hr)) {
	write_log ("AHI: Error getting DirectSound capabilities: %s\n", DXError (hr));
	goto error;
    }
    if (DSCaps.dwFlags & DSCAPS_EMULDRIVER) {
	write_log ("AHI: Emulated DirectSound driver detected, don't complain if sound quality is crap :)\n");
    }
    if (DSCaps.dwFlags & DSCAPS_CONTINUOUSRATE) {
	int minfreq = DSCaps.dwMinSecondarySampleRate;
	int maxfreq = DSCaps.dwMaxSecondarySampleRate;
	if (minfreq > freq && freq < 22050) {
	    freq = minfreq;
	    write_log ("AHI: minimum supported frequency: %d\n", minfreq);
	}
	if (maxfreq < freq && freq > 44100) {
	    freq = maxfreq;
	    write_log ("AHI: maximum supported frequency: %d\n", maxfreq);
	}
    }
    return 1;
error:
    ds_free (dsahip);
    return 0;
}

static void ds_setvolume (struct dssample *ds, int volume, int panning)
{
    HRESULT hr;
    LONG vol, pan;

    // negative pan = output from surround speakers!
    // negative volume = invert sample data!! (not yet emulated)
    vol = (LONG)((DSBVOLUME_MIN / 2) + (-DSBVOLUME_MIN / 2) * log (1 + (2.718281828 - 1) * (abs (volume) / 65536.0)));
    pan = (abs (panning) - 0x8000) * DSBPAN_RIGHT / 32768;
    if (panning >= 0 || ds->chout <= 2) {
	hr = IDirectSoundBuffer_SetPan (ds->dsb, pan);
	if (FAILED (hr))
	    write_log ("AHI: SetPan(%d,%d) failed: %s\n", ds->num, pan, DXError (hr));
	hr = IDirectSoundBuffer_SetVolume (ds->dsb, vol);
	if (FAILED (hr))
	    write_log ("AHI: SetVolume(%d,%d) failed: %s\n", ds->num, vol, DXError (hr));
	if (ds->dsbback) {
	    hr = IDirectSoundBuffer_SetVolume (ds->dsbback, DSBVOLUME_MIN);
	    if (FAILED (hr))
		write_log ("AHI: muteback %d: %s\n", ds->num, DXError (hr));
	}
    } else {
	hr = IDirectSoundBuffer_SetVolume (ds->dsb, DSBVOLUME_MIN);
	if (FAILED (hr))
	    write_log ("AHI: mutefront %d: %s\n", ds->num, DXError (hr));
	if (ds->dsbback) {
	    hr = IDirectSoundBuffer_SetPan (ds->dsbback, pan);
	    if (FAILED (hr))
		write_log ("AHI: SetPanBack(%d,%d) failed: %s\n", ds->num, pan, DXError (hr));
	    hr = IDirectSoundBuffer_SetVolume (ds->dsbback, vol);
	    if (FAILED (hr))
		write_log ("AHI: SetVolumeBack(%d,%d) failed: %s\n", ds->num, vol, DXError (hr));
	}
    }
}

static void ds_setfreq (struct dssample *ds, int freq)
{
    HRESULT hr;

    if (freq == 0) {
	hr = IDirectSoundBuffer8_Stop (ds->dsb);
	if (ds->dsbback)
	    hr = IDirectSoundBuffer8_Stop (ds->dsbback);
    } else {
	if (ds->freq == 0) {
	    hr = IDirectSoundBuffer8_Play (ds->dsb, 0, 0, 0);
	    if (ds->dsbback)
		hr = IDirectSoundBuffer8_Play (ds->dsbback, 0, 0, 0);
	}
	hr = IDirectSoundBuffer8_SetFrequency (ds->dsb, freq);
	if (FAILED (hr))
	    write_log ("AHI: SetFrequency(%d,%d) failed: %s\n", ds->num, freq, DXError (hr));
	if (ds->dsbback) {
	    hr = IDirectSoundBuffer8_SetFrequency (ds->dsbback, freq);
	    if (FAILED (hr))
		write_log ("AHI: SetFrequencyBack(%d,%d) failed: %s\n", ds->num, freq, DXError (hr));
	}
    }
    ds->freq = freq;
}

static void copysampledata (struct dssample *ds, void *srcp, void *dstp, int dstsize, int offset, int srcsize)
{
    int i, j;
    uae_u8 *src = (uae_u8*)srcp;
    uae_u8 *dst = (uae_u8*)dstp;

    src += offset * ds->ch * ds->bytespersample;
    if (dstsize < srcsize)
	srcsize = dstsize;

    switch (ds->type)
    {
	case AHIST_M8S:
	case AHIST_S8S:
	    for (i = 0; i < srcsize; i++) {
		dst[i * 2 + 0] = src[i];
		dst[i * 2 + 1] = src[i];
	    }
	break;
	case AHIST_M16S:
	    for (i = 0; i < srcsize; i++) {
		dst[i * 2 + 0] = src[i * 2 + 1];
		dst[i * 2 + 1] = src[i * 2 + 0];
	    }
	break;
	case AHIST_S16S:
	    for (i = 0; i < srcsize; i++) {
		dst[i * 4 + 0] = src[i * 4 + 1];
		dst[i * 4 + 1] = src[i * 4 + 0];
		dst[i * 4 + 2] = src[i * 4 + 3];
		dst[i * 4 + 3] = src[i * 4 + 2];
	    }
	break;
	case AHIST_M32S:
	    for (i = 0; i < srcsize; i++) {
		dst[i * 2 + 0] = src[i * 4 + 3];
		dst[i * 2 + 1] = src[i * 4 + 2];
	    }
	break;
	case AHIST_S32S:
	    for (i = 0; i < srcsize; i++) {
		dst[i * 4 + 0] = src[i * 8 + 3];
		dst[i * 4 + 1] = src[i * 8 + 2];
		dst[i * 4 + 2] = src[i * 8 + 7];
		dst[i * 4 + 3] = src[i * 8 + 6];
	    }
	break;
	case AHIST_L7_1:
	    if (ds->ch == ds->chout) {
		for (i = 0; i < srcsize; i++) {
		    for (j = 0; j < 8; j++) {
			dst[j * 4 + 0] = src[j * 4 + 2];
			dst[j * 4 + 1] = src[j * 4 + 1];
			dst[j * 4 + 2] = src[j * 4 + 0];
			dst[j * 4 + 3] = 0;
		    }
		    dst += 4 * 8;
		    src += 4 * 8;
		}
	    } else { /* 7.1 -> 5.1 */
		for (i = 0; i < srcsize; i++) {
		    for (j = 0; j < 6; j++) {
			dst[j * 4 + 0] = src[j * 4 + 2];
			dst[j * 4 + 1] = src[j * 4 + 1];
			dst[j * 4 + 2] = src[j * 4 + 0];
			dst[j * 4 + 3] = 0;
		    }
		    dst += 4 * 8;
		    src += 4 * 8;
		}
	    }
	break;
    }
}

static void copysample (struct dssample *ds, LPDIRECTSOUNDBUFFER8 dsb, uae_u32 offset, uae_u32 length)
{
    HRESULT hr;
    void *buffer;
    DWORD size;
    uae_u32 addr;

    if (!dsb)
	return;
    hr = IDirectSoundBuffer8_Lock (dsb, 0, ds->len, &buffer, &size, NULL, NULL, 0);
    if (hr == DSERR_BUFFERLOST) {
	IDirectSoundBuffer_Restore (dsb);
	hr = IDirectSoundBuffer8_Lock (dsb, 0, ds->len, &buffer, &size, NULL, NULL, 0);
    }
    if (FAILED (hr))
	return;
    memset (buffer, 0, size);
    if (ds->addr == 0 && ds->len == 0xffffffff)
	addr = offset;
    else
	addr = ds->addr;
    if (valid_address (addr + offset * ds->ch * ds->bytespersample, length * ds->ch * ds->bytespersample))
	copysampledata (ds, get_real_address (addr), buffer, size, offset, length);
    IDirectSoundBuffer8_Unlock (dsb, buffer, size, NULL, 0);
}

static void ds_setsound (struct DSAHI *dsahip, struct dssample *ds, int offset, int length)
{
    HRESULT hr;
    if (!dsahip->playing)
	return;
    copysample (ds, ds->dsb, offset, length);
    copysample (ds, ds->dsbback, offset, length);
    hr = IDirectSoundBuffer8_SetCurrentPosition (ds->dsb, 0);
    if (FAILED (hr))
	write_log ("AHI: IDirectSoundBuffer8_SetCurrentPosition() failed, %s\n", DXError (hr));
    hr = IDirectSoundBuffer8_Play (ds->dsb, 0, 0, 0);
    if (FAILED (hr))
	write_log ("AHI: IDirectSoundBuffer8_Play() failed, %s\n", DXError (hr));
    if (ds->dsbback) {
	hr = IDirectSoundBuffer8_SetCurrentPosition (ds->dsbback, 0);
	if (FAILED (hr))
	    write_log ("AHI: IDirectSoundBuffer8_SetCurrentPositionBack() failed, %s\n", DXError (hr));
	hr = IDirectSoundBuffer8_Play (ds->dsbback, 0, 0, 0);
	if (FAILED (hr))
	    write_log ("AHI: IDirectSoundBuffer8_PlayBack() failed, %s\n", DXError (hr));
    }
}


static void ds_freebuffer (struct DSAHI *ahidsp, struct dssample *ds)
{
    if (!ds)
	return;
    if (ds->dsb)
	IDirectSoundBuffer8_Release (ds->dsb);
    if (ds->dsbback)
	IDirectSoundBuffer8_Release (ds->dsbback);
    ds->dsb = NULL;
    ds->dsbback = NULL;
}

static int ds_allocbuffer (struct DSAHI *ahidsp, struct dssample *ds, int type, uae_u32 addr, uae_u32 len)
{
    HRESULT hr;
    DSBUFFERDESC dd;
    WAVEFORMATEXTENSIBLE wavfmt;
    LPDIRECTSOUNDBUFFER pdsb;
    LPDIRECTSOUNDBUFFER8 pdsb8;
    int round, chround;
    int channels[] = { 8, 6, 2, 0 };
    int ch, chout, bps, bpsout;

    if (!ds)
	return 0;
    switch (type)
    {
	case AHIST_M8S:
	case AHIST_S8S:
	ch = 1;
	break;
	case AHIST_M16S:
	case AHIST_S16S:
	case AHIST_M32S:
	case AHIST_S32S:
	ch = 2;
	break;
	case AHIST_L7_1:
	ch = 8;
	break;
	default:
	return 0;
    }
    switch (type)
    {
	case AHIST_M8S:
	case AHIST_S8S:
	bps = 8;
	break;
	case AHIST_M16S:
	case AHIST_S16S:
	bps = 16;
	break;
	case AHIST_M32S:
	case AHIST_S32S:
	case AHIST_L7_1:
	bps = 24;
	break;
	default:
	return 0;
    }

    bpsout = 16;
    for (chround = 0; channels[chround]; chround++) {
	chout = channels[chround];
	for (round = 0; supportedmodes[round].ch; round++) {
	    DWORD ksmode = 0;

	    pdsb = NULL;
 	    memset (&wavfmt, 0, sizeof (WAVEFORMATEXTENSIBLE));
	    wavfmt.Format.nChannels = ch;
	    wavfmt.Format.nSamplesPerSec = default_freq;
	    wavfmt.Format.wBitsPerSample = bps;
	    if (supportedmodes[round].ch != chout)
		continue;

	    if (chout <= 2) {
		wavfmt.Format.wFormatTag = WAVE_FORMAT_PCM;
	    } else {
		DWORD ksmode = 0;
		wavfmt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
		wavfmt.Format.cbSize = sizeof (WAVEFORMATEXTENSIBLE) - sizeof (WAVEFORMATEX);
		wavfmt.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
		wavfmt.Samples.wValidBitsPerSample = bps;
		wavfmt.dwChannelMask = supportedmodes[round].ksmode;
	    }
	    wavfmt.Format.nBlockAlign = bps / 8 * wavfmt.Format.nChannels;
	    wavfmt.Format.nAvgBytesPerSec = wavfmt.Format.nBlockAlign * wavfmt.Format.nSamplesPerSec;

  	    dd.dwSize = sizeof (dd);
	    dd.dwBufferBytes = len * bps / 8 * chout;
	    dd.lpwfxFormat = &wavfmt.Format;
	    dd.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS;
	    dd.dwFlags |= DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN | DSBCAPS_CTRLFREQUENCY;
	    dd.dwFlags |= chout >= 4 ? DSBCAPS_LOCHARDWARE : DSBCAPS_LOCSOFTWARE;
	    dd.guid3DAlgorithm = GUID_NULL;

	    hr = IDirectSound_CreateSoundBuffer (lpDS, &dd, &pdsb, NULL);
	    if (SUCCEEDED (hr))
		break;
	    if (dd.dwFlags & DSBCAPS_LOCHARDWARE) {
		HRESULT hr2 = hr;
		dd.dwFlags &= ~DSBCAPS_LOCHARDWARE;
		dd.dwFlags |=  DSBCAPS_LOCSOFTWARE;
		hr = IDirectSound_CreateSoundBuffer (lpDS, &dd, &pdsb, NULL);
		if (SUCCEEDED (hr)) {
		    write_log ("AHI: Couldn't use hardware buffer (switched to software): %s\n", DXError (hr2));
		    break;
		}
	    }
	    write_log ("AHI: DS sound buffer failed (ch=%d,bps=%d): %s\n",
		ch, bps, DXError (hr));
	}
	if (pdsb)
	    break;
    }
    if (pdsb == NULL)
	goto error;
    hr = IDirectSound_QueryInterface (pdsb, &IID_IDirectSoundBuffer8, (LPVOID*)&pdsb8);
    if (FAILED (hr))  {
	write_log ("AHI: Secondary QueryInterface() failure: %s\n", DXError (hr));
	goto error;
    }
    IDirectSound_Release (pdsb);
    ds->dsb = pdsb8;

    if (chout > 2) {
	// create "surround" sound buffer
	hr = IDirectSound_CreateSoundBuffer (lpDS, &dd, &pdsb, NULL);
	if (SUCCEEDED (hr)) {
	    hr = IDirectSound_QueryInterface (pdsb, &IID_IDirectSoundBuffer8, (LPVOID*)&pdsb8);
	    if (SUCCEEDED (hr))
		ds->dsbback = pdsb8;
	    IDirectSound_Release (pdsb);
	}
    }
      
    ds_setvolume (ds, 0, 0);
    ds->bitspersample = bps;
    ds->bitspersampleout = bpsout;
    ds->ch = ch;
    ds->chout = chout;
    ds->bytespersample = bps / 8;
    ds->bytespersampleout = bpsout / 8;
    ds->freq = default_freq;
    ds->addr = addr;
    ds->len = len;
    ds->type = type;

    return 1;

error:
    return 0;
}


static uae_u32 AHIsub_AllocAudio (TrapContext *ctx)
{
    uae_u32 tags = m68k_areg (&ctx->regs, 1);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    uae_u32 tag, data;
    uae_u32 ret = AHISF_KNOWSTEREO | AHISF_KNOWHIFI;
    struct DSAHI *dsahip = &dsahi[0];

    put_long (audioctrl + ahiac_DriverData, dsahip - dsahi);

    if (!ds_init (dsahip))
	return AHISF_ERROR;
    if (dsahip->cansurround)
	ret |= AHISF_KNOWMULTICHANNEL;

    while ((tag = gettag (&tags, &data))) {
	switch (tag)
	{
	    case AHIA_Sounds:
		dsahip->sounds = data;
	    break;
	    case AHIA_Channels:
		dsahip->channels = data;
	    break;
	    case AHIA_SoundFunc:
		dsahip->soundfunc = data;
	    break;
	    case AHIA_PlayerFunc:
		dsahip->playerfunc = data;
	    break;
	    case AHIA_MinPlayerFreq:
		dsahip->minplayerfreq = data;
	    break;
	    case AHIA_MaxPlayerFreq:
		dsahip->maxplayerfreq = data;
	    break;
	}
    }
    if (dsahip->sounds < 0 || dsahip->sounds > 1000) {
	ds_free (dsahip);
	return AHISF_ERROR;
    }
    dsahip->sample = xmalloc (sizeof (struct dssample) * dsahip->sounds);
    xahi_author = ds ("Toni Wilen");
    xahi_copyright = ds ("GPL");
    xahi_version = ds ("uae2 0.1 (22.06.2008)\r\n");
    xahi_output = ds ("Default Output");
    return ret;
}

#define GETAHI (&dsahi[get_long(audioctrl + ahiac_DriverData)])
#define GETSAMPLE (dsahip ? &dsahip->sample[channel] : NULL)

static void AHIsub_Disable (TrapContext *ctx)
{
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    struct DSAHI *dsahip = GETAHI;
    dsahip->enabledisable++;
}

static void AHIsub_Enable (TrapContext *ctx)
{
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    struct DSAHI *dsahip = GETAHI;
    dsahip->enabledisable--;
}

static void AHIsub_FreeAudio (TrapContext *ctx)
{
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    struct DSAHI *dsahip = GETAHI;
    put_long (audioctrl + ahiac_DriverData, -1);
}

static uae_u32 frequencies[] = { 48000, 44100 };
#define MAX_FREQUENCIES (sizeof (frequencies) / sizeof (uae_u32))

static uae_u32 AHIsub_GetAttr (TrapContext *ctx)
{
    uae_u32 attribute = m68k_dreg (&ctx->regs, 0);
    uae_u32 argument = m68k_dreg (&ctx->regs, 1);
    uae_u32 def = m68k_dreg (&ctx->regs, 2);
    uae_u32 taglist = m68k_areg (&ctx->regs, 1);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    struct DSAHI *dsahip = GETAHI;
    int i;
    
    switch (attribute)
    {
	case AHIDB_Bits:
	return 32;
	case AHIDB_Frequencies:
	return MAX_FREQUENCIES;
	case AHIDB_Frequency:
	if (argument < 0 || argument >= MAX_FREQUENCIES)
	    argument = 0;
	return frequencies[argument];
	case AHIDB_Index:
	if (argument <= frequencies[0])
	    return 0;
	if (argument >= frequencies[MAX_FREQUENCIES - 1])
	    return MAX_FREQUENCIES - 1;
	for (i = 1; i < MAX_FREQUENCIES; i++) {
	    if (frequencies[i] > argument) {
	      if (argument - frequencies[i - 1] < frequencies[i] - argument)
		return i - 1;
	      else
		return i;
	    }
	}
	return 0;
	case AHIDB_Author:
	return xahi_author;
	case AHIDB_Copyright:
	return xahi_copyright;
	case AHIDB_Version:
	return xahi_version;
	case AHIDB_Record:
	return FALSE;
	case AHIDB_Realtime:
	return TRUE;
	case AHIDB_Outputs:
	return 1;
	case AHIDB_MinOutputVolume:
	return 0x00000;
	case AHIDB_MaxOutputVolume:
	return 0x10000;
	case AHIDB_Output:
	return xahi_output;
	case AHIDB_Volume:
	return 1;
	case AHIDB_Panning:
	return 1;
	case AHIDB_HiFi:
	return 1;
	case AHIDB_MultiChannel:
	return dsahip->cansurround;
	case AHIDB_MaxChannels:
	return 32;
	default:
	return def;
    }
}

static uae_u32 AHIsub_HardwareControl (TrapContext *ctx)
{
    uae_u32 attribute = m68k_dreg (&ctx->regs, 0);
    uae_u32 argument = m68k_dreg (&ctx->regs, 1);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    struct DSAHI *dsahip = GETAHI;
    return 0;
}

static uae_u32 AHIsub_Start (TrapContext *ctx)
{
    uae_u32 flags = m68k_dreg (&ctx->regs, 0);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    struct DSAHI *dsahip = GETAHI;
    if (flags & AHISF_PLAY)
	dsahip->playing = 1;
    if (flags & AHISF_RECORD)
	dsahip->recording = 1;
    return 0;
}

static uae_u32 AHIsub_Stop (TrapContext *ctx)
{
    uae_u32 flags = m68k_dreg (&ctx->regs, 0);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    struct DSAHI *dsahip = GETAHI;
    if (flags & AHISF_PLAY)
	dsahip->playing = 0;
    if (flags & AHISF_RECORD)
	dsahip->recording = 0;
    return 0;
}

static uae_u32 AHIsub_Update (TrapContext *ctx)
{
    uae_u32 flags = m68k_dreg (&ctx->regs, 0);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    struct DSAHI *dsahip = GETAHI;
    return 0;
}

static uae_u32 AHIsub_SetVol (TrapContext *ctx)
{
    uae_u32 channel = m68k_dreg (&ctx->regs, 0);
    uae_u32 volume = m68k_dreg (&ctx->regs, 1);
    uae_u32 pan = m68k_dreg (&ctx->regs, 2);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    uae_u32 flags = m68k_dreg (&ctx->regs, 3);
    struct DSAHI *dsahip = GETAHI;
    struct dssample *ds = GETSAMPLE;
    if (ds)
	ds_setvolume (ds, volume, pan); 
    return 0;
}

static uae_u32 AHIsub_SetFreq (TrapContext *ctx)
{
    uae_u32 channel = m68k_dreg (&ctx->regs, 0);
    uae_u32 freq = m68k_dreg (&ctx->regs, 1);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    uae_u32 flags = m68k_dreg (&ctx->regs, 3);
    struct DSAHI *dsahip = GETAHI;
    struct dssample *ds = GETSAMPLE;
    if (ds)
	ds_setfreq (ds, freq); 
   return 0;
}

static uae_u32 AHIsub_SetSound (TrapContext *ctx)
{
    uae_u32 channel = m68k_dreg (&ctx->regs, 0);
    uae_u32 sound = m68k_dreg (&ctx->regs, 1);
    uae_u32 offset = m68k_dreg (&ctx->regs, 2);
    uae_u32 length  = m68k_dreg (&ctx->regs, 3);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    uae_u32 flags = m68k_dreg (&ctx->regs, 4);
    struct DSAHI *dsahip = GETAHI;
    struct dssample *ds = GETSAMPLE;
    if (ds)
	ds_setsound (dsahip, ds, offset, length);
    return 0;
}

static uae_u32 AHIsub_SetEffect (TrapContext *ctx)
{
    uae_u32 effect = m68k_areg (&ctx->regs, 0);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    struct DSAHI *dsahip = GETAHI;
    return 0;
}

static uae_u32 AHIsub_LoadSound (TrapContext *ctx)
{
    uae_u16 sound = m68k_dreg (&ctx->regs, 0);
    uae_u32 type = m68k_dreg (&ctx->regs, 1);
    uae_u32 info = m68k_areg (&ctx->regs, 0);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    struct DSAHI *dsahip = GETAHI;
    uae_u32 ret = AHIE_BADSOUNDTYPE;
    int sampletype = get_long (info + ahisi_Type);
    uae_u32 addr = get_long (info + ahisi_Address);
    uae_u32 len = get_long (info + ahisi_Length);
    if (sound >= 0 && sound < MAX_SAMPLES && sound < dsahip->sounds) {
	if (!dsahip->cansurround && type == AHIST_L7_1)
	    return AHIE_BADSOUNDTYPE;
	if (ds_allocbuffer (dsahip, &dsahip->sample[sound], type, addr, len))
	    ret = AHIE_OK;
    }
    return ret;
}

static uae_u32 AHIsub_UnloadSound (TrapContext *ctx)
{
    uae_u16 sound = m68k_dreg (&ctx->regs, 0);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    struct DSAHI *dsahip = GETAHI;
    if (sound >= 0 && sound < MAX_SAMPLES)
	ds_freebuffer (dsahip, &dsahip->sample[sound]);
    return AHIE_OK;
}

static uae_u32 REGPARAM2 ahi_demux (TrapContext *ctx)
{
    uae_u32 ret = 0;
    uae_u32 sp = m68k_areg (&ctx->regs, 7);
    uae_u32 offset = get_word (sp);
    switch (offset)
    {
	case 0:
	    ret = AHIsub_AllocAudio (ctx);
	break;
	case 1:
	    AHIsub_FreeAudio (ctx);
	break;
	case 2:
	    AHIsub_Disable (ctx);
	break;
	case 3:
	    AHIsub_Enable (ctx);
	break;
	case 4:
	    ret = AHIsub_Start (ctx);
	break;
	case 5:
	    ret = AHIsub_Update (ctx);
	break;
	case 6:
	    ret = AHIsub_Stop (ctx);
	break;
	case 7:
	    ret = AHIsub_SetVol (ctx);
	break;
	case 8:
	    ret = AHIsub_SetFreq (ctx);
	break;
	case 9:
	    ret = AHIsub_SetSound (ctx);
	break;
	case 10:
	    ret = AHIsub_SetEffect (ctx);
	break;
	case 11:
	    ret = AHIsub_LoadSound (ctx);
	break;
	case 12:
	    ret = AHIsub_UnloadSound (ctx);
	break;
	case 13:
	    ret = AHIsub_GetAttr (ctx);
	break;
	case 14:
	    ret = AHIsub_HardwareControl (ctx);
	break;
    }
    return ret;
}

void init_ahi_v2 (void)
{
    uaecptr a = here ();
    org (rtarea_base + 0xFFC8);
    calltrap (deftrapres (ahi_demux, 0, "ahi_winuae_v2"));
    dw (RTS);
    org (a);
}




#endif
