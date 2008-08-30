/*
 * UAE - The Un*x Amiga Emulator
 *
 * DirectSound AHI 7.1 wrapper
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
#include <math.h>
#include <process.h>

#include "sysdeps.h"
#include "options.h"
#include "audio.h"
#include "memory.h"
#include "events.h"
#include "custom.h"
#include "newcpu.h"
#include "autoconf.h"
#include "traps.h"
#include "threaddep/thread.h"
#include "native2amiga.h"
#include "od-win32/win32.h"
#include "sounddep/sound.h"
#include "ahidsound_new.h"
#include "dxwrap.h"

#include <mmsystem.h>
#include <mmreg.h>
#include <dsound.h>
#include <ks.h>
#include <ksmedia.h>

#define AHI_STRUCT_VERSION 1

static int ahi_debug = 1;

#define UAE_MAXCHANNELS 24
#define UAE_MAXSOUNDS 256
#define RECORDSAMPLES 2048

#define ub_Flags 34
#define ub_Pad1 (ub_Flags + 1)
#define ub_Pad2 (ub_Pad1 + 1)
#define ub_SysLib (ub_Pad2 + 2)
#define ub_SegList (ub_SysLib + 4)
#define ub_DOSBase (ub_SegList + 4)
#define ub_AHIFunc (ub_DOSBase + 4)

#define pub_SizeOf 0
#define pub_Version (pub_SizeOf + 4)
#define pub_Index (pub_Version + 4)
#define pub_Base (pub_Index + 4)
#define pub_audioctrl (pub_Base + 4)
#define pub_FuncTask (pub_audioctrl + 4)
#define pub_WaitMask (pub_FuncTask + 4)
#define pub_WaitSigBit (pub_WaitMask + 4)
#define pub_FuncMode (pub_WaitSigBit +4)
#define pub_TaskMode (pub_FuncMode + 2)
#define pub_ChannelSignal (pub_TaskMode + 2)
#define pub_ChannelSignalAck (pub_ChannelSignal + 4)
#define pub_ahism_Channel (pub_ChannelSignalAck + 4)
#define pub_RecordHookDone (pub_ahism_Channel + 2)
#define pub_RecordSampleType (pub_RecordHookDone + 2)
#define pub_RecordBufferSize (pub_RecordSampleType + 4)
#define pub_RecordBufferSizeBytes (pub_RecordBufferSize + 4)
#define pub_RecordBuffer (pub_RecordBufferSizeBytes + 4)
#define pub_ChannelInfo (pub_RecordBuffer + 4 + 4 + 4 + 4)
#define pub_End (pub_ChannelInfo + 4)

#define FUNCMODE_PLAY 1
#define FUNCMODE_RECORD 2
#define FUNCMODE_RECORDALLOC 4

#define ahie_Effect 0
#define ahieci_Func 4
#define ahieci_Channels 8
#define ahieci_Pad 10
#define ahieci_Offset 12

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

#define AHISB_IMM		0
#define AHISB_NODELAY		1
#define AHISF_IMM		(1 << AHISB_IMM)
#define AHISF_NODELAY		(1 << SHISB_NODELAY)

#define AHIET_CANCEL		(1 << 31)
#define AHIET_MASTERVOLUME	1
#define AHIET_OUTPUTBUFFER	2
#define AHIET_DSPMASK		3
#define AHIET_DSPECHO		4
#define AHIET_CHANNELINFO	5

#define AHI_TagBase		(0x80000000)
#define AHI_TagBaseR		(AHI_TagBase|0x8000)

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

struct dssample {
    int num;
    int ch;
    int bitspersample;
    int bytespersample;
    uae_u32 addr;
    uae_u32 len;
    uae_u32 type;
    uae_u32 sampletype;
    uae_u32 offset;
};

struct chsample {
    int frequency;
    int volume;
    int panning;
    int backwards;
    struct dssample *ds;
    int srcplayoffset;
    int srcplaylen;
    int stopit;
};

struct dschannel {
    int num;
    struct chsample cs;
    struct chsample csnext;
    LPDIRECTSOUNDBUFFER8 dsb;

    uae_u8 *buffer;
    int mixlength;
    int buffercursor;
    int hsync;
    int channelsignal;
    int srcoffset;

    int dsplaying;
    int dscursor;
};

struct DSAHI {
    uae_u32 audioctrl;
    int chout;
    int bits24;
    int bitspersampleout;
    int bytespersampleout;
    int channellength;
    int mixlength;
    int input;
    int output;
    int channels;
    int sounds;
    int playerfreq;
    int enabledisable;
    struct dssample *sample;
    struct dschannel *channel;
    int playing, recording;
    evt evttime;
    uae_u32 signalchannelmask;
    LPDIRECTSOUND8 DS;

    LPDIRECTSOUNDCAPTURE DSC;
    LPDIRECTSOUNDCAPTUREBUFFER dscb;
    int dsrecording;
    int recordingcursor;
    int record_ch;
    int record_bytespersample;
    int channellength_record;
    int mixlength_record;
    int record_wait;
};

static struct DSAHI dsahi[1];

#define GETAHI (&dsahi[get_long(get_long(audioctrl + ahiac_DriverData) + pub_Index)])
#define GETSAMPLE (dsahip && sound >= 0 && sound < UAE_MAXSOUNDS ? &dsahip->sample[sound] : NULL)
#define GETCHANNEL (dsahip && channel >= 0 && channel < UAE_MAXCHANNELS ? &dsahip->channel[channel] : NULL)

static int default_freq = 44100;
static int cansurround;
static uae_u32 xahi_author, xahi_copyright, xahi_version;
static uae_u32 xahi_output[MAX_SOUND_DEVICES], xahi_output_num;
static uae_u32 xahi_input[MAX_SOUND_DEVICES], xahi_input_num;
static int ahi_paused;
static int ahi_active;

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


static int sendsignal (struct DSAHI *dsahip)
{
    uae_u32 audioctrl = dsahip->audioctrl;
    uae_u32 puaebase = get_long (audioctrl + ahiac_DriverData);
    uae_u32 channelinfo;
    uae_u32 task, signalmask;
    uae_s16 taskmode = get_word (puaebase + pub_TaskMode);
    uae_s16 funcmode = get_word (puaebase + pub_FuncMode);
    task = get_long (puaebase + pub_FuncTask);
    signalmask = get_long (puaebase + pub_WaitMask);

    if ((!dsahip->playing && !dsahip->recording) || ahi_paused)
	return 0;
    if (taskmode <= 0)
	return 0;
    if (dsahip->enabledisable) {
	// allocate Amiga-side recordingbuffer
	funcmode &= FUNCMODE_RECORDALLOC;
	put_word (puaebase + pub_FuncMode, funcmode);
    }

    channelinfo = get_long (puaebase + pub_ChannelInfo);
    if (channelinfo) {
	int i, ch;
	ch = get_word (channelinfo + ahieci_Channels);
	if (ch > UAE_MAXCHANNELS)
	    ch = UAE_MAXCHANNELS;
	for (i = 0; i < ch; i++)
	    put_long (channelinfo + ahieci_Offset + i * 4, dsahip->channel[i].srcoffset);
    }

    uae_Signal (task, signalmask);
    return 1;
}

static void setchannelevent (struct DSAHI *dsahip, struct dschannel *dc)
{
    uae_u32 audioctrl = dsahip->audioctrl;
    uae_u32 puaebase = get_long (audioctrl + ahiac_DriverData);
    int ch = dc - &dsahip->channel[0];
    uae_u32 mask;

    if (!dsahip->playing || ahi_paused || dc->dsb == NULL)
	return;
    mask = get_long (puaebase + pub_ChannelSignal);
    if (mask & (1 << ch))
	return;
    dc->channelsignal = 1;
    put_long (puaebase + pub_ChannelSignal, mask | (1 << ch));
    sendsignal (dsahip);
}

static void evtfunc (uae_u32 v)
{
    if (ahi_active) {
	struct DSAHI *dsahip = &dsahi[v];
	uae_u32 audioctrl = dsahip->audioctrl;
	uae_u32 puaebase = get_long (audioctrl + ahiac_DriverData);
       
	put_word (puaebase + pub_FuncMode, get_word (puaebase + pub_FuncMode) | FUNCMODE_PLAY);
	if (sendsignal (dsahip)) {
	    event2_newevent2 (dsahip->evttime, v, evtfunc);
	} else {
	    dsahip->evttime = 0;
	}
    }
}

static void setevent (struct DSAHI *dsahip)
{
    uae_u32 audioctrl = dsahip->audioctrl;
    uae_u32 freq = get_long (audioctrl + ahiac_PlayerFreq);
    double f;
    uae_u32 cycles;
    evt t;
    
    f = ((double)(freq >> 16)) + ((double)(freq & 0xffff)) / 65536.0;
    if (f < 1)
	return;
    cycles = maxhpos * maxvpos * vblank_hz;
    t = (evt)(cycles / f);
    if (dsahip->evttime == t)
	return;
    write_log ("AHI: playerfunc freq = %.2fHz\n", f);
    dsahip->evttime = t;
    if (t < 10)
	return;
    event2_newevent2 (t, dsahip - &dsahi[0], evtfunc);
}



const static GUID KSDATAFORMAT_SUBTYPE_PCM = {0x00000001,0x0000,0x0010,
    {0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};

static void ds_freechannel (struct DSAHI *ahidsp, struct dschannel *dc)
{
    if (!dc)
	return;
    if (dc->dsb)
	IDirectSoundBuffer8_Release (dc->dsb);
    xfree (dc->buffer);
    memset (dc, 0, sizeof (struct dschannel));
}

static void ds_free (struct DSAHI *dsahip)
{
    int i;

    for (i = 0; i < dsahip->channels; i++) {
	struct dschannel *dc = &dsahip->channel[i];
	ds_freechannel (dsahip, dc);
    }
    if (dsahip->DS)
	IDirectSound_Release (dsahip->DS);
    dsahip->DS = NULL;
    if (ahi_debug && ahi_active)
	write_log ("AHI: DSOUND freed\n");
    ahi_active = 0;
}

DWORD fillsupportedmodes (LPDIRECTSOUND8 lpDS, int freq, struct dsaudiomodes *dsam);
static struct dsaudiomodes supportedmodes[16];

static void ds_free_record (struct DSAHI *dsahip)
{
    if (dsahip->dscb)
	IDirectSoundCaptureBuffer_Release (dsahip->dscb);
    if (dsahip->DSC)
	IDirectSoundCapture_Release (dsahip->DSC);
    dsahip->dscb = NULL;
    dsahip->DSC = NULL;
}

static int ds_init_record (struct DSAHI *dsahip)
{
    HRESULT hr;
    WAVEFORMATEXTENSIBLE wavfmt;
    DSCBUFFERDESC dbd;
    uae_u32 pbase = get_long (dsahip->audioctrl + ahiac_DriverData);
    int freq = get_long (dsahip->audioctrl + ahiac_MixFreq);

    if (dsahip->DSC)
	return 1;
    if (!freq)
	return 0;
    dsahip->mixlength_record = RECORDSAMPLES; // in sample units, not bytes
    dsahip->record_ch = 2;
    dsahip->record_bytespersample = 2;
    dsahip->channellength_record = freq * dsahip->record_bytespersample * dsahip->record_ch * 10;
    put_long (pbase + pub_RecordBufferSize, dsahip->mixlength_record);
    put_long (pbase + pub_RecordBufferSizeBytes, dsahip->mixlength_record * dsahip->record_ch * dsahip->record_bytespersample);
    put_long (pbase + pub_RecordSampleType, AHIST_S16S);
    put_word (pbase + pub_RecordHookDone, 0);

    hr = DirectSoundCaptureCreate (&record_devices[dsahip->input].guid, &dsahip->DSC, NULL);
    if (FAILED (hr)) {
	write_log ("AHI: DirectSoundCaptureCreate() failure %dHz: %s\n", freq, DXError (hr));
	goto end;
    }
    memset (&dbd, 0, sizeof dbd);
    dbd.dwSize = sizeof dbd;
    dbd.dwBufferBytes = dsahip->channellength_record;
    dbd.lpwfxFormat = &wavfmt.Format;
    dbd.dwFlags = 0 ;
    memset (&wavfmt, 0, sizeof wavfmt);
    wavfmt.Format.nChannels = dsahip->record_ch;
    wavfmt.Format.nSamplesPerSec = freq;
    wavfmt.Format.wBitsPerSample = dsahip->record_bytespersample * 8;
    wavfmt.Format.wFormatTag = WAVE_FORMAT_PCM;
    wavfmt.Format.nBlockAlign = wavfmt.Format.wBitsPerSample / 8 * wavfmt.Format.nChannels;
    wavfmt.Format.nAvgBytesPerSec = wavfmt.Format.nBlockAlign * wavfmt.Format.nSamplesPerSec;
    hr = IDirectSoundCapture_CreateCaptureBuffer (dsahip->DSC, &dbd, &dsahip->dscb, NULL);
    if (FAILED (hr)) {
	write_log ("AHI: CreateCaptureSoundBuffer() failure: %s\n", DXError(hr));
	goto end;
    }
    if (ahi_debug)
	write_log ("AHI: DSOUND Recording initialized. %dHz, %s\n", freq, record_devices[dsahip->input].name);

    put_word (pbase + pub_FuncMode, get_word (pbase + pub_FuncMode) | FUNCMODE_RECORDALLOC);
    sendsignal (dsahip);
    return 1;
end:
    ds_free_record (dsahip);
    return 0;
}

static int ds_init (struct DSAHI *dsahip)
{
    int freq = 44100;
    DSCAPS DSCaps;
    HRESULT hr;
    DWORD speakerconfig;

    hr = DirectSoundCreate8 (&sound_devices[dsahip->output].guid, &dsahip->DS, NULL);
    if (FAILED (hr))  {
	write_log ("AHI: DirectSoundCreate8() failure: %s\n", DXError (hr));
	return 0;
    }

    hr = IDirectSound_SetCooperativeLevel (dsahip->DS, hMainWnd, DSSCL_PRIORITY);
    if (FAILED (hr)) {
	write_log ("AHI: Can't set cooperativelevel: %s\n", DXError (hr));
	goto error;
    }

    fillsupportedmodes (dsahip->DS, default_freq, supportedmodes);
    dsahip->chout = 2;
    if (SUCCEEDED (IDirectSound8_GetSpeakerConfig (dsahip->DS, &speakerconfig))) {
	if (speakerconfig >= DSSPEAKER_CONFIG (DSSPEAKER_5POINT1)) {
	    cansurround = 1;
	    dsahip->chout = 6;
	    if (speakerconfig >= DSSPEAKER_CONFIG (DSSPEAKER_7POINT1))
		dsahip->chout = 8;
	}
    }

    dsahip->bitspersampleout = dsahip->bits24 ? 24 : 16;
    dsahip->bytespersampleout = dsahip->bitspersampleout / 8;
    dsahip->channellength = 65536 * dsahip->chout * dsahip->bytespersampleout;
    dsahip->mixlength = 4000 * dsahip->chout * dsahip->bytespersampleout;
    if (ahi_debug)
	write_log("AHI: CH=%d BLEN=%d MLEN=%d SC=%08x\n",
	    dsahip->chout, dsahip->channellength, dsahip->mixlength, speakerconfig);

    memset (&DSCaps, 0, sizeof (DSCaps));
    DSCaps.dwSize = sizeof (DSCaps);
    hr = IDirectSound_GetCaps (dsahip->DS, &DSCaps);
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

    if (ahi_debug)
	write_log ("AHI: DSOUND initialized: %s\n", sound_devices[dsahip->output].name);

    return 1;
error:
    if (ahi_debug)
	write_log ("AHI: DSOUND initialization failed\n");
    ds_free (dsahip);
    return 0;
}

static int ds_reinit (struct DSAHI *dsahip)
{
    ds_free (dsahip);
    return ds_init (dsahip);
}


static void ds_setvolume (struct DSAHI *dsahip, struct dschannel *dc)
{
    HRESULT hr;
    LONG vol, pan;

    if (dc->dsb) {
	if (abs (dc->cs.volume) != abs (dc->csnext.volume)) {
	    vol = (LONG)((DSBVOLUME_MIN / 2) + (-DSBVOLUME_MIN / 2) * log (1 + (2.718281828 - 1) * (abs (dc->csnext.volume) / 65536.0)));
	    hr = IDirectSoundBuffer_SetVolume (dc->dsb, vol);
	    if (FAILED (hr))
		write_log ("AHI: SetVolume(%d,%d) failed: %s\n", dc->num, vol, DXError (hr));
	}
	if (abs (dc->cs.panning) != abs (dc->csnext.panning)) {
	    pan = (abs (dc->csnext.panning) - 0x8000) * DSBPAN_RIGHT / 32768;
	    hr = IDirectSoundBuffer_SetPan (dc->dsb, pan);
	    if (FAILED (hr))
		write_log ("AHI: SetPan(%d,%d) failed: %s\n", dc->num, pan, DXError (hr));
	}
    }
    dc->cs.volume = dc->csnext.volume;
    dc->cs.panning = dc->csnext.panning;
}

static void ds_setfreq (struct DSAHI *dsahip, struct dschannel *dc)
{
    HRESULT hr;

    if (dc->cs.frequency != dc->csnext.frequency && dc->csnext.frequency > 0 && dc->dsb) {
	hr = IDirectSoundBuffer8_SetFrequency (dc->dsb, dc->csnext.frequency);
	if (FAILED (hr))
	    write_log ("AHI: SetFrequency(%d,%d) failed: %s\n", dc->num, dc->csnext.frequency, DXError (hr));
    }
    dc->cs.frequency = dc->csnext.frequency;
}

static int ds_allocchannel (struct DSAHI *dsahip, struct dschannel *dc)
{
    HRESULT hr;
    DSBUFFERDESC dd;
    WAVEFORMATEXTENSIBLE wavfmt;
    LPDIRECTSOUNDBUFFER pdsb;
    LPDIRECTSOUNDBUFFER8 pdsb8;
    int round;

    if (dc->dsb)
	return 1;
    pdsb = NULL;
    for (round = 0; supportedmodes[round].ch; round++) {
        DWORD ksmode = 0;

        pdsb = NULL;
        if (supportedmodes[round].ch != dsahip->chout)
	    continue;

 	memset (&wavfmt, 0, sizeof (WAVEFORMATEXTENSIBLE));
	wavfmt.Format.nChannels = dsahip->chout;
	wavfmt.Format.nSamplesPerSec = default_freq;
	wavfmt.Format.wBitsPerSample = dsahip->bitspersampleout;
	if (dsahip->chout <= 2) {
	    wavfmt.Format.wFormatTag = WAVE_FORMAT_PCM;
	} else {
	    DWORD ksmode = 0;
	    wavfmt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
	    wavfmt.Format.cbSize = sizeof (WAVEFORMATEXTENSIBLE) - sizeof (WAVEFORMATEX);
	    wavfmt.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
	    wavfmt.Samples.wValidBitsPerSample = dsahip->bitspersampleout;
	    wavfmt.dwChannelMask = supportedmodes[round].ksmode;
	}
	wavfmt.Format.nBlockAlign = dsahip->bytespersampleout * wavfmt.Format.nChannels;
	wavfmt.Format.nAvgBytesPerSec = wavfmt.Format.nBlockAlign * wavfmt.Format.nSamplesPerSec;

	memset (&dd, 0, sizeof dd);
  	dd.dwSize = sizeof dd;
	dd.dwBufferBytes = dsahip->channellength;
	dd.lpwfxFormat = &wavfmt.Format;
	dd.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS | DSBCAPS_CTRLPOSITIONNOTIFY;
	dd.dwFlags |= DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN | DSBCAPS_CTRLFREQUENCY;
	dd.dwFlags |= dsahip->chout >= 4 ? DSBCAPS_LOCHARDWARE : DSBCAPS_LOCSOFTWARE;
	dd.guid3DAlgorithm = GUID_NULL;

	hr = IDirectSound_CreateSoundBuffer (dsahip->DS, &dd, &pdsb, NULL);
	if (SUCCEEDED (hr))
	    break;
	if (dd.dwFlags & DSBCAPS_LOCHARDWARE) {
	    HRESULT hr2 = hr;
	    dd.dwFlags &= ~DSBCAPS_LOCHARDWARE;
	    dd.dwFlags |=  DSBCAPS_LOCSOFTWARE;
	    hr = IDirectSound_CreateSoundBuffer (dsahip->DS, &dd, &pdsb, NULL);
	    if (SUCCEEDED (hr))
	        break;
	}
	write_log ("AHI: DS sound buffer failed (ch=%d,bps=%d): %s\n",
	    dsahip->chout, dsahip->bitspersampleout, DXError (hr));
    }
    if (pdsb == NULL)
	goto error;

    hr = IDirectSound_QueryInterface (pdsb, &IID_IDirectSoundBuffer8, (LPVOID*)&pdsb8);
    if (FAILED (hr))  {
	write_log ("AHI: Secondary QueryInterface(IID_IDirectSoundBuffer8) failure: %s\n", DXError (hr));
	goto error;
    }
    IDirectSound_Release (pdsb);
    dc->dsb = pdsb8;

    dc->cs.frequency = -1;
    dc->cs.volume = -1;
    dc->cs.panning = -1;
    ds_setvolume (dsahip, dc);
    ds_setfreq (dsahip, dc);
    dc->mixlength = dsahip->mixlength;
    dc->buffer = xcalloc (dc->mixlength, 1);
    dc->buffercursor = 0;
    if (ahi_debug)
	write_log ("AHI: allocated directsound buffer for channel %d\n", dc->num);
    return 1;
error:
    ds_freechannel (dsahip, dc);
    return 0;
}

#define MAKEXCH makexch (dsahip, dc, dst, i, och2, l, r)

STATIC_INLINE void makexch (struct DSAHI *dsahip, struct dschannel *dc, uae_u8 *dst, int idx, int och2, uae_s32 l, uae_s32 r)
{
    if (dsahip->bits24) {
    } else {
	uae_s16 *dst2 = (uae_u16*)(&dst[idx * och2]);
	l >>= 8;
	r >>= 8;
	if (dc->cs.volume < 0) {
	    l = -l;
	    r = -r;
	}
	dst2[0] = l;
	dst2[1] = r;
	if (dsahip->chout <= 2)
	    return;
	dst2[4] = dst2[0];
	dst2[5] = dst2[1];
	if (dc->cs.panning < 0) {
	    // surround only
	    dst2[2] = 0; // center
	    dst2[3] = (dst2[0] + dst2[1]) / 4; // lfe
	    dst2[0] = dst2[1] = 0;
	    return;
	}
	dst2[2] = dst2[3] = (dst2[0] + dst2[1]) / 4;
	if (dsahip->chout <= 6)
	    return;
	dst2[6] = dst2[4];
	dst2[7] = dst2[5];
    }
}

/* sample conversion routines */
static void copysampledata (struct DSAHI *dsahip, struct dschannel *dc, struct dssample *ds, uae_u8 **psrcp, uae_u8 *srce, uae_u8 *srcp, void *dstp, int dstlen)
{
    int i;
    uae_u8 *src = *psrcp;
    uae_u8 *dst = (uae_u8*)dstp;
    int och = dsahip->chout;
    int och2 = och * 2;
    int ich = ds->ch;
    int len;

    len = dstlen;
    switch (ds->sampletype)
    {
	case AHIST_M8S:
	    for (i = 0; i < len; i++) {
		uae_u32 l = (src[0] << 16) | (src[0] << 8) | src[0] ;
		uae_u32 r = (src[0] << 16) | (src[0] << 8) | src[0];
		src += 1;
		if (src >= srce)
		    src = srcp;
		MAKEXCH;
	    }
	break;
	case AHIST_S8S:
	    for (i = 0; i < len; i++) {
		uae_u32 l = (src[0] << 16) | (src[0] << 8) | src[0] ;
		uae_u32 r = (src[1] << 16) | (src[1] << 8) | src[1];
		src += 2;
		if (src >= srce)
		    src = srcp;
		MAKEXCH;
	    }
	break;
	case AHIST_M16S:
	    for (i = 0; i < len; i++) {
		uae_u32 l = (src[0] << 16) | (src[1] << 8) | src[1];
		uae_u32 r = (src[0] << 16) | (src[1] << 8) | src[1];
		src += 2;
		if (src >= srce)
		    src = srcp;
		MAKEXCH;
	    }
	break;
	case AHIST_S16S:
	    for (i = 0; i < len; i++) {
		uae_u32 l = (src[0] << 16) | (src[1] << 8) | src[1];
		uae_u32 r = (src[2] << 16) | (src[3] << 8) | src[3];
		src += 4;
	        if (src >= srce)
	    	    src = srcp;
		MAKEXCH;
	    }
	break;
	case AHIST_M32S:
	    for (i = 0; i < len; i++) {
		uae_u32 l = (src[3] << 16) | (src[2] << 8) | src[1];
		uae_u32 r = (src[3] << 16) | (src[2] << 8) | src[1];
		src += 4;
	        if (src >= srce)
	    	    src = srcp;
		MAKEXCH;
	    }
	break;
	case AHIST_S32S:
	    for (i = 0; i < len; i++) {
		uae_u32 l = (src[3] << 16) | (src[2] << 8) | src[1];
		uae_u32 r = (src[7] << 16) | (src[6] << 8) | src[5];
		src += 8;
	        if (src >= srce)
	    	    src = srcp;
		MAKEXCH;
	    }
	break;
	case AHIST_L7_1:
	    if (och == 8) {
		for (i = 0; i < len; i++) {
		    if (dsahip->bits24) {
			uae_u32 fl = (src[0 * 4 + 3] << 16) | (src[0 * 4 + 2] << 8) | src[0 * 4 + 1];
			uae_u32 fr = (src[1 * 4 + 3] << 16) | (src[1 * 4 + 2] << 8) | src[1 * 4 + 1];
			uae_u32 cc = (src[6 * 4 + 3] << 16) | (src[6 * 4 + 2] << 8) | src[6 * 4 + 1];
			uae_u32 lf = (src[7 * 4 + 3] << 16) | (src[7 * 4 + 2] << 8) | src[7 * 4 + 1];
			uae_u32 bl = (src[2 * 4 + 3] << 16) | (src[2 * 4 + 2] << 8) | src[2 * 4 + 1];
			uae_u32 br = (src[3 * 4 + 3] << 16) | (src[3 * 4 + 2] << 8) | src[3 * 4 + 1];
			uae_u32 sl = (src[4 * 4 + 3] << 16) | (src[4 * 4 + 2] << 8) | src[4 * 4 + 1];
			uae_u32 sr = (src[5 * 4 + 3] << 16) | (src[5 * 4 + 2] << 8) | src[5 * 4 + 1];
			uae_s32 *dst2 = (uae_s32*)(&dst[i * och2]);
			dst2[0] = fl;
			dst2[1] = fr;
			dst2[2] = cc;
			dst2[3] = lf;
			dst2[4] = bl;
			dst2[5] = br;
			dst2[6] = sl;
			dst2[7] = sr;
		    } else {
			uae_u16 fl = (src[0 * 4 + 3] << 8) | src[0 * 4 + 2];
			uae_u16 fr = (src[1 * 4 + 3] << 8) | src[1 * 4 + 2];
			uae_u16 cc = (src[6 * 4 + 3] << 8) | src[6 * 4 + 2];
			uae_u16 lf = (src[7 * 4 + 3] << 8) | src[7 * 4 + 2];
			uae_u16 bl = (src[2 * 4 + 3] << 8) | src[2 * 4 + 2];
			uae_u16 br = (src[3 * 4 + 3] << 8) | src[3 * 4 + 2];
			uae_u16 sl = (src[4 * 4 + 3] << 8) | src[4 * 4 + 2];
			uae_u16 sr = (src[5 * 4 + 3] << 8) | src[5 * 4 + 2];
			uae_s16 *dst2 = (uae_s16*)(&dst[i * och2]);
			dst2[0] = fl;
			dst2[1] = fr;
			dst2[2] = cc;
			dst2[3] = lf;
			dst2[4] = bl;
			dst2[5] = br;
			dst2[6] = sl;
			dst2[7] = sr;
		    }
		    dst += och2;
		    src += 8 * 4;
		    if (src >= srce)
	    		src = srcp;
		}
	    } else if (och == 6) { /* 7.1 -> 5.1 */
		for (i = 0; i < len; i++) {
		    if (dsahip->bits24) {
			uae_s32 *dst2 = (uae_s32*)(&dst[i * och2]);
			uae_u32 fl = (src[0 * 4 + 3] << 16) | (src[0 * 4 + 2] << 8) | src[0 * 4 + 1];
			uae_u32 fr = (src[1 * 4 + 3] << 16) | (src[1 * 4 + 2] << 8) | src[1 * 4 + 1];
			uae_u32 cc = (src[6 * 4 + 3] << 16) | (src[6 * 4 + 2] << 8) | src[6 * 4 + 1];
			uae_u32 lf = (src[7 * 4 + 3] << 16) | (src[7 * 4 + 2] << 8) | src[7 * 4 + 1];
			uae_u32 bl = (src[2 * 4 + 3] << 16) | (src[2 * 4 + 2] << 8) | src[2 * 4 + 1];
			uae_u32 br = (src[3 * 4 + 3] << 16) | (src[3 * 4 + 2] << 8) | src[3 * 4 + 1];
			uae_u32 sl = (src[4 * 4 + 3] << 16) | (src[4 * 4 + 2] << 8) | src[4 * 4 + 1];
			uae_u32 sr = (src[5 * 4 + 3] << 16) | (src[5 * 4 + 2] << 8) | src[5 * 4 + 1];
		    } else {
			uae_s16 *dst2 = (uae_s16*)(&dst[i * och2]);
			uae_u16 fl = (src[0 * 4 + 3] << 8) | src[0 * 4 + 2];
			uae_u16 fr = (src[1 * 4 + 3] << 8) | src[1 * 4 + 2];
			uae_u16 cc = (src[6 * 4 + 3] << 8) | src[6 * 4 + 2];
			uae_u16 lf = (src[7 * 4 + 3] << 8) | src[7 * 4 + 2];
			uae_u16 bl = (src[2 * 4 + 3] << 8) | src[2 * 4 + 2];
			uae_u16 br = (src[3 * 4 + 3] << 8) | src[3 * 4 + 2];
			uae_u16 sl = (src[4 * 4 + 3] << 8) | src[4 * 4 + 2];
			uae_u16 sr = (src[5 * 4 + 3] << 8) | src[5 * 4 + 2];
			dst2[0] = fl;
			dst2[1] = fr;
			dst2[2] = cc;
			dst2[3] = lf;
			dst2[4] = (bl + sl) / 2;
			dst2[5] = (br + sr) / 2;
		    }
		    dst += och2;
		    src += 8 * 4;
		    if (src >= srce)
	    		src = srcp;
		}
	    }
	break;
    }
    *psrcp = src;
}

static void dorecord (struct DSAHI *dsahip)
{
    uae_u32 pbase = get_long (dsahip->audioctrl + ahiac_DriverData);
    HRESULT hr;
    DWORD cpos, rpos, diff;
    void *buf1, *buf2;
    DWORD size1, size2;
    uae_u32 recordbuf;
    int mixlength_bytes;

    if (dsahip->dscb == NULL)
	return;
    if (dsahip->record_wait && !get_word (pbase + pub_RecordHookDone))
	return;
    dsahip->record_wait = 0;
    mixlength_bytes = dsahip->mixlength_record * dsahip->record_ch * dsahip->record_bytespersample;
    recordbuf = get_long (pbase + pub_RecordBuffer);
    if (recordbuf == 0 || !valid_address (recordbuf, mixlength_bytes))
	return;
    hr = IDirectSoundCaptureBuffer_GetCurrentPosition (dsahip->dscb, &cpos, &rpos);
    if (FAILED (hr)) {
	write_log ("AHI: IDirectSoundCaptureBuffer_GetCurrentPosition() failed %s\n", DXError (hr));
	return;
    }
    if (rpos < dsahip->recordingcursor)
	rpos += dsahip->channellength_record;
    diff = rpos - dsahip->recordingcursor;
    if (diff < mixlength_bytes)
	return;
    hr = IDirectSoundCaptureBuffer_Lock (dsahip->dscb, dsahip->recordingcursor, mixlength_bytes, &buf1, &size1, &buf2, &size2, 0);
    if (SUCCEEDED (hr)) {
	uae_u8 *addr = get_real_address (recordbuf);
	uae_u8 *b = (uae_u8*)buf1;
	int s;
	b = (uae_u8*)buf1;
	s = size1;
	while (s > 0) {
	    addr[0] = b[1];
	    addr[1] = b[0];
	    addr += 2;
	    b += 2;
	    s -= 2;
	}
	b = (uae_u8*)buf2;
	s = size2;
	while (s > 0) {
	    addr[0] = b[1];
	    addr[1] = b[0];
	    addr += 2;
	    b += 2;
	    s -= 2;
	}
	IDirectSoundCaptureBuffer_Unlock (dsahip->dscb, buf1, size1, buf2, size2);
	put_word (pbase + pub_RecordHookDone, 0);
	dsahip->record_wait = 1;
	put_word (pbase + pub_FuncMode, get_word (pbase + pub_FuncMode) | FUNCMODE_RECORD);
	sendsignal (dsahip);
    }
    dsahip->recordingcursor += mixlength_bytes;
    if (dsahip->recordingcursor >= dsahip->channellength_record)
	dsahip->recordingcursor -= dsahip->channellength_record;
}

static int ds_copysample (struct DSAHI *dsahip, struct dschannel *dc)
{
    HRESULT hr;
    DWORD playc, writec;
    DWORD size1, size2;
    DWORD diff;
    void *buf1, *buf2;

    hr = IDirectSoundBuffer8_GetCurrentPosition (dc->dsb, &playc, &writec);
    if (FAILED (hr)) {
        write_log ("AHI: GetCurrentPosition(%d) failed, %s\n", dc->num, DXError (hr));
        return 0;
    }

    if (dc->dscursor >= writec)
	diff = dc->dscursor - writec;
    else
        diff = dsahip->channellength - writec + dc->dscursor;

    if (diff > dsahip->channellength / 2) {
	dc->dscursor = writec + dc->mixlength;
	write_log ("AHI: Resync\n");
    }
    if (diff > dc->mixlength)
	return 0;

    hr = IDirectSoundBuffer8_Lock (dc->dsb, dc->dscursor, dc->mixlength, &buf1, &size1, &buf2, &size2, 0);
    if (hr == DSERR_BUFFERLOST) {
	IDirectSoundBuffer8_Restore (dc->dsb);
	hr = IDirectSoundBuffer8_Lock (dc->dsb, dc->dscursor, dc->mixlength, &buf1, &size1, &buf2, &size2, 0);
    }
    if (SUCCEEDED (hr)) {
	memcpy (buf1, dc->buffer, size1);
	if (buf2) 
	    memcpy (buf2, dc->buffer + size1, size2);
	IDirectSoundBuffer8_Unlock (dc->dsb, buf1, size1, buf2, size2);
    }

    if (ahi_debug > 1)
	write_log ("%d playc=%08d writec=%08d dscursor=%08d\n",
	    diff / (dsahip->chout * dsahip->bytespersampleout),
	    playc, writec, dc->dscursor);

    dc->dscursor += dc->mixlength;
    if (dc->dscursor >= dsahip->channellength)
	dc->dscursor -= dsahip->channellength;

    return 1;
}

static int calcdelay (struct dschannel *dc, int len)
{
    int rate = vblank_hz * maxvpos;
    int freq = dc->cs.frequency;
    int hsyncs = rate * len / freq;
    hsyncs = hsyncs * 2 / 3;
    if (hsyncs > 0)
	hsyncs--;
    return hsyncs;
}

#if 0
static void checkvolfreq (struct DSAHI *dsahip, struct dschannel *dc)
{
    if (dc->cs.frequency != dc->csnext.frequency)
	ds_setfreq (dsahip, dc);
    if (dc->cs.volume != dc->csnext.volume || dc->cs.panning != dc->csnext.panning)
	ds_setvolume (dsahip, dc);
}

static void getmixbufferlen (struct DSAHI *dsahip, struct dschannel *dc)
{
    int olen = dc->mixlength;
    dc->mixlength = (dc->csnext.frequency / 24) * dsahip->bytespersampleout * dsahip->chout;
    if (dc->mixlength > dsahip->mixlength)
	dc->mixlength = dsahip->mixlength;
    if (dc->mixlength < 100)
	dc->mixlength = 100;
    if (ahi_debug && olen != dc->mixlength)
	write_log ("AHI: channel %d: buffer %d frames\n",
	    dc->num, dc->mixlength / (dsahip->bytespersampleout * dsahip->chout));
}

static void copysample (struct DSAHI *dsahip, struct dschannel *dc)
{
    int dstlen, dstlenbytes;
    struct dssample *ds;
    int srclen, srclendstbytes;
    int chbytesout;
    uae_u8 *dstbuf;
    uae_u32 addr, addre, addrs;
    int chbytesin;
    int waitlen;

    assert (dc->buffercursor < dc->mixlength);

    chbytesout = dsahip->chout * dsahip->bytespersampleout;
    dstbuf = dc->buffer + dc->buffercursor;
    dstlenbytes = dc->mixlength - dc->buffercursor;
    dstlen = dstlenbytes / chbytesout;
    
    ds = dc->cs.ds;
    if (ds == NULL) {
	ds = dc->ds = dc->nextds;
	dc->srcplaylen = dc->nextsrcplaylen;
	dc->srcplayoffset = dc->nextsrcplayoffset;
	dc->nextds = NULL;
	dc->nextsrcplaylen = 0;
	dc->nextsrcplayoffset = 0;
	dc->srcoffset = 0;
	if (ds != NULL) { // sample started
	    int len;
	    setchannelevent (dsahip, dc);
	    len = dc->srcplaylen;
	    if (len > dstlen)
		len = dstlen;
	    dc->hsync = calcdelay (dc, len);
	    return;
	}
    }
    if (ds == NULL)
	goto cempty;

    chbytesin = ds->ch * ds->bytespersample;
    if (ds->addr == 0 && ds->len == 0xffffffff) {
        addrs = addr = ds->offset;
    } else {
        addr = ds->addr;
        addr += dc->srcplayoffset * chbytesin;
        addrs = addr;
    }
    addre = addr + dc->srcplaylen * chbytesin;
    addr += dc->srcoffset * chbytesin;

    srclen = dc->srcplaylen - dc->srcoffset;
    assert (srclen > 0);
    srclendstbytes = srclen * chbytesout;

    if (srclendstbytes > dstlenbytes) {
        srclendstbytes = dstlenbytes;
        srclen = srclendstbytes / chbytesout;
    }
    if (dstlenbytes > srclendstbytes) {
        dstlenbytes = srclendstbytes;
        dstlen = dstlenbytes / chbytesout;
    }

    waitlen = dstlen;

    assert (dstlen > 0);

    if (valid_address (addrs, addre - addrs)) {
        uae_u8 *naddr = get_real_address (addr);
        uae_u8 *naddre = get_real_address (addre);
        uae_u8 *naddrs = get_real_address (addrs);
        copysampledata (dsahip, dc, ds, &naddr, naddre, naddrs, dstbuf, dstlen);
        dc->srcoffset = (naddr - naddrs) / chbytesin;
        if (dc->srcoffset == 0) {
	    // looping or next sample
	    if (dc->nextsrcplaylen < 0) { // stop sample
		int off = dstlenbytes;
		dc->nextsrcplaylen = 0;
		dc->nextds = NULL;
		setchannelevent (dsahip, dc);
		dstlenbytes = dc->mixlength - off - dc->buffercursor;
		dstlen = dstlenbytes / chbytesout;
		memset (dstbuf + off, 0, dstlenbytes);
		goto empty;
	    }
	    if (dc->nextds) {
		dc->ds = NULL;
		dc->buffercursor += dstlenbytes;
		dc->hsync = 0;
		return;
	    }
	    setchannelevent (dsahip, dc);

	}

    } else {
	goto cempty;
    }

    dc->hsync = calcdelay (dc, waitlen);
    dc->buffercursor += dstlenbytes;
    checkvolfreq (dsahip, dc);
    //write_log ("%d ", dstlen / (dsahip->chout * dsahip->bytespersampleout));
    return;

cempty:
    memset (dstbuf, 0, dstlenbytes);
empty:
    dc->ds = NULL;
    dc->hsync = calcdelay (dc, waitlen);
    dc->buffercursor += dstlenbytes;
    checkvolfreq (dsahip, dc);
}
#endif

static void incchannel (struct DSAHI *dsahip, struct dschannel *dc, int samplecnt)
{
    struct chsample *cs = &dc->cs;

    if (cs->ds == NULL)
	return;
    dc->srcoffset += samplecnt;
    while (dc->srcoffset >= cs->srcplaylen) {
	dc->srcoffset -= cs->srcplaylen;
        setchannelevent (dsahip, dc);
	if (dc->csnext.stopit) {
	    memset (cs, 0, sizeof (struct chsample));
	    memset (&dc->csnext, 0, sizeof (struct chsample));
	    dc->srcoffset = 0;
	    return;
	}
	if (dc->csnext.ds) {
	    memcpy (cs, &dc->csnext, sizeof (struct chsample));
	    memset (&dc->csnext, 0, sizeof (struct chsample));
	    continue;
	}
    }
}

void ahi_hsync (void)
{
    struct DSAHI *dsahip = &dsahi[0];
    static int cnt;
    uae_u32 pbase;
    int i, flags;

    if (ahi_paused || !ahi_active)
	return;
    pbase = get_long (dsahip->audioctrl + ahiac_DriverData);
    if (cnt >= 0)
	cnt--;
    if (cnt < 0) {
	if (dsahip->dsrecording && dsahip->enabledisable == 0) {
	    dorecord (dsahip);
	    cnt = 100;
	}
    }
    if (!dsahip->playing)
	return;
    flags = get_long (pbase + pub_ChannelSignalAck);
    for (i = 0; i < UAE_MAXCHANNELS; i++) {
	struct dschannel *dc = &dsahip->channel[i];
	HRESULT hr;
	DWORD playc, writec, diff, samplediff;

	if (dc->dsb == NULL)
	    continue;
	if (dc->dsplaying == 0)
	    continue;
        hr = IDirectSoundBuffer8_GetCurrentPosition (dc->dsb, &playc, &writec);
	if (FAILED (hr)) {
	    write_log ("AHI: GetCurrentPosition(%d) failed, %s\n", dc->num, DXError (hr));
	    continue;
	}
	if (dc->dscursor >= writec)
	    diff = dc->dscursor - writec;
	else
	    diff = dsahip->channellength - writec + dc->dscursor;
	if (diff > dsahip->channellength / 2) {
	    dc->dscursor = writec + dc->mixlength;
	    write_log ("AHI: Resync %d\n", dc->num);
	}

	if (dc->dscursor < 0) {
	    dc->dscursor = writec;
	    memcpy (&dc->cs, &dc->csnext, sizeof (struct chsample));
	}
	samplediff = diff / (dsahip->chout * dsahip->bytespersampleout);
	incchannel (dsahip, dc, samplediff);
//	copysample (dsahip, dc, &dc->cs);




	if (diff > dc->mixlength)
	    continue;


#if 0
	if (dc->buffer == NULL)
	    continue;
	if (dc->hsync > 0) {
	    dc->hsync--;
	    continue;
	}
	if (dc->channelsignal) {
	    if (!(flags & (1 << dc->num)))
		continue;
	    dc->channelsignal = 0;
	    flags &= ~(1 << dc->num);
	}
	if (dsahip->enabledisable)
	    continue;
	if (dc->buffercursor < dc->mixlength)
	    copysample (dsahip, dc);
	assert (dc->buffercursor <= dc->mixlength);
	if (dc->buffercursor == dc->mixlength) {
	    if (ds_copysample (dsahip, dc)) {
		getmixbufferlen (dsahip, dc);
		dc->buffercursor = 0;
	    } else {
		dc->hsync = 100;
	    }
	}
#endif
    }
    put_long (pbase + pub_ChannelSignalAck, flags);
}

static void ds_record (struct DSAHI *dsahip, int start)
{
    HRESULT hr;

    if (dsahip->dscb == NULL)
	return;
    if (dsahip->dsrecording && start)
	return;
    dsahip->dsrecording = 0;
    if (start) {
	hr = IDirectSoundCaptureBuffer_Start (dsahip->dscb, DSCBSTART_LOOPING);
	if (FAILED (hr)) {
	    write_log ("AHI: DirectSoundCaptureBuffer_Start failed: %s\n", DXError (hr));
	    return;
	}
	dsahip->dsrecording = 1;
    } else {
	hr = IDirectSoundCaptureBuffer_Stop (dsahip->dscb);
	if (FAILED (hr)) {
	    write_log ("AHI: DirectSoundCaptureBuffer_Stop failed: %s\n", DXError (hr));
	    return;
	}
    }
}

static void ds_stop (struct DSAHI *dsahip, struct dschannel *dc)
{
    HRESULT hr;

    dc->dsplaying = 0;
    if (dc->dsb == NULL)
	return;
    if (ahi_debug)
	write_log ("AHI: ds_stop(%d)\n", dc->num);
    hr = IDirectSoundBuffer8_Stop (dc->dsb);
    if (FAILED (hr))
	write_log ("AHI: IDirectSoundBuffer8_Stop() failed, %s\n", DXError (hr));
}

static void ds_play (struct DSAHI *dsahip, struct dschannel *dc)
{
    HRESULT hr;
    DWORD status, playc, writec;

    if (dc->dsb == NULL)
	return;
    if (dc->dsplaying)
	return;
    if (dc->cs.frequency == 0)
	return;
    dc->dsplaying = 1;
    if (ahi_debug)
	write_log ("AHI: ds_play(%d)\n", dc->num);
    hr = IDirectSoundBuffer8_GetStatus (dc->dsb, &status);
    if (FAILED (hr))
	write_log ("AHI: ds_play() IDirectSoundBuffer8_GetStatus() failed, %s\n", DXError (hr));
    hr = IDirectSoundBuffer8_Play (dc->dsb, 0, 0, DSBPLAY_LOOPING);
    if (FAILED (hr))
	write_log ("AHI: ds_play() IDirectSoundBuffer8_Play() failed, %s\n", DXError (hr));
    hr = IDirectSoundBuffer8_GetCurrentPosition (dc->dsb, &playc, &writec);
    if (FAILED (hr))
	write_log ("AHI: ds_play() IDirectSoundBuffer8_GetCurrentPosition() failed, %s\n", DXError (hr));
    dc->dscursor = writec + dc->mixlength;
    if (dc->dscursor >= dsahip->channellength)
	dc->dscursor -= dsahip->channellength;
    if (ahi_debug)
	write_log("AHI: ds_play(%d) Start=%d->%d\n", dc->num, writec, dc->dscursor);
}

void ahi2_pause_sound (int paused)
{
    int i;
    struct DSAHI *dsahip = &dsahi[0];

    ahi_paused = paused;
    if (!dsahip->playing && !dsahip->recording)
	return;
    for (i = 0; i < UAE_MAXCHANNELS; i++) {
	struct dschannel *dc = &dsahip->channel[i];
	if (dc->dsb == NULL)
	    continue;
	if (paused) {
	    ds_stop (dsahip, dc);
	} else {
	    ds_play (dsahip, dc);
	}
    }
}

static uae_u32 init (TrapContext *ctx)
{
    int i;
    
    enumerate_sound_devices ();
    xahi_author = ds ("Toni Wilen");
    xahi_copyright = ds ("GPL");
    xahi_version = ds ("uae2 0.1 (xx.xx.2008)\r\n");
    for (i = 0; sound_devices[i].name; i++)
	xahi_output[i] = ds (sound_devices[i].name);
    xahi_output_num = i;
    for (i = 0; record_devices[i].name; i++)
	xahi_input[i] = ds (record_devices[i].name);
    xahi_input_num = i;
    return 1;
}

static uae_u32 AHIsub_AllocAudio (TrapContext *ctx)
{
    int i;
    uae_u32 tags = m68k_areg (&ctx->regs, 1);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    uae_u32 pbase = get_long (audioctrl + ahiac_DriverData);
    uae_u32 tag, data, v, ver, size;
    uae_u32 ret = AHISF_KNOWSTEREO | AHISF_KNOWHIFI;
    struct DSAHI *dsahip = &dsahi[0];

    if (ahi_debug)
	write_log ("AHI: AllocAudio(%08x,%08x)\n", tags, audioctrl);

    ver = get_long (pbase + pub_Version);
    size = get_long (pbase + pub_SizeOf);
    if (ver != AHI_STRUCT_VERSION) {
	gui_message ("AHI: Incompatible DEVS:AHI/uae2.audio\nVersion mismatch %d<>%d.", ver, AHI_STRUCT_VERSION);
	return AHISF_ERROR;
    }
    if (size < pub_End) {
	gui_message ("AHI: Incompatible DEVS:AHI/uae2.audio.\nInternal structure size %d<%d.", size, pub_End);
	return AHISF_ERROR;
    }


    v = get_long (pbase + pub_Index);
    if (v != -1) {
	write_log ("AHI: corrupted memory\n");
	return AHISF_ERROR;
    }
    put_long (pbase + pub_Index, dsahip - dsahi);
    dsahip->audioctrl = audioctrl;

    if (!ds_init (dsahip))
	return AHISF_ERROR;
    dsahip->sounds = UAE_MAXSOUNDS;
    dsahip->channels = UAE_MAXCHANNELS;

    if (xahi_input_num)
	ret |= AHISF_CANRECORD;
    if (cansurround)
	ret |= AHISF_KNOWMULTICHANNEL;

    while ((tag = gettag (&tags, &data))) {
	if (ahi_debug)
	    write_log ("- TAG %08x=%d: %08x=%u\n", tag, tag & 0x7fff, data, data);
    }
    if (dsahip->sounds < 0 || dsahip->sounds > 1000) {
	ds_free (dsahip);
	ds_free_record (dsahip);
	return AHISF_ERROR;
    }
    dsahip->sample = xcalloc (sizeof (struct dssample), dsahip->sounds);
    dsahip->channel = xcalloc (sizeof (struct dschannel), dsahip->channels);
    for (i = 0; i < dsahip->channels; i++) {
	struct dschannel *dc = &dsahip->channel[i];
	dc->num = i;
    }
    for (i = 0; i < dsahip->sounds; i++) {
	struct dssample *ds = &dsahip->sample[i];
	ds->num = -1;
    }
    ahi_active = 1;
    return ret;
}

static void AHIsub_Disable (TrapContext *ctx)
{
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    struct DSAHI *dsahip = GETAHI;
    if (ahi_debug)
	write_log ("AHI: Disable(%08x)\n", audioctrl);
    dsahip->enabledisable++;
}

static void AHIsub_Enable (TrapContext *ctx)
{
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    struct DSAHI *dsahip = GETAHI;
    if (ahi_debug)
	write_log ("AHI: Enable(%08x)\n", audioctrl);
    dsahip->enabledisable--;
    if (dsahip->enabledisable == 0 && dsahip->playing)
	setevent (dsahip);
}

static void AHIsub_FreeAudio (TrapContext *ctx)
{
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    uae_u32 pbase = get_long (audioctrl + ahiac_DriverData);
    struct DSAHI *dsahip = GETAHI;
    if (ahi_debug)
	write_log ("AHI: FreeAudio(%08x)\n", audioctrl);
    if (ahi_active == 0)
	return;
    ahi_active = 0;
    put_long (pbase + pub_Index, -1);
    if (dsahip) {
	ds_free (dsahip);
	ds_free_record (dsahip);
	xfree (dsahip->channel);
	xfree (dsahip->sample);
	memset (dsahip, 0, sizeof (struct DSAHI));
    }
}

static uae_u32 frequencies[] = { 48000, 44100 };
#define MAX_FREQUENCIES (sizeof (frequencies) / sizeof (uae_u32))

static uae_u32 getattr2 (uae_u32 attribute, uae_u32 argument, uae_u32 def)
{
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
	return -1;
	case AHIDB_Realtime:
	return -1;
	case AHIDB_MinOutputVolume:
	return 0x00000;
	case AHIDB_MaxOutputVolume:
	return 0x10000;
	case AHIDB_Outputs:
	return xahi_output_num;
	case AHIDB_Output:
	if (argument >= 0 && argument < xahi_output_num)
	    return xahi_output[argument];
	return 0;
	case AHIDB_Inputs:
	return xahi_input_num;
	case AHIDB_Input:
	if (argument >= 0 && argument < xahi_input_num)
	    return xahi_input[argument];
	return 0;
	case AHIDB_Volume:
	return -1;
	case AHIDB_Panning:
	return -1;
	case AHIDB_HiFi:
	return -1;
	case AHIDB_MultiChannel:
	return cansurround ? -1 : 0;
	case AHIDB_MaxChannels:
	return UAE_MAXCHANNELS;
	case AHIDB_FullDuplex:
	return -1;
	case AHIDB_MaxRecordSamples:
	return RECORDSAMPLES;
	default:
	return def;
    }
}

static uae_u32 AHIsub_GetAttr (TrapContext *ctx)
{
    uae_u32 attribute = m68k_dreg (&ctx->regs, 0);
    uae_u32 argument = m68k_dreg (&ctx->regs, 1);
    uae_u32 def = m68k_dreg (&ctx->regs, 2);
    uae_u32 taglist = m68k_areg (&ctx->regs, 1);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    uae_u32 v;

    v = getattr2 (attribute, argument, def);
    if (ahi_debug)
	write_log ("AHI: GetAttr(%08x=%d,%08x,%08x)=%08x\n", attribute, attribute & 0x7fff, argument, def, v);

    return v;
}

static uae_u32 AHIsub_HardwareControl (TrapContext *ctx)
{
    uae_u32 attribute = m68k_dreg (&ctx->regs, 0);
    uae_u32 argument = m68k_dreg (&ctx->regs, 1);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    struct DSAHI *dsahip = GETAHI;
    if (ahi_debug)
	write_log ("AHI: HardwareControl(%08x=%d,%08x,%08x)\n", attribute, attribute & 0x7fff, argument, audioctrl);
    switch (attribute)
    {
	case AHIC_Input:
	if (dsahip->input != argument) {
	    dsahip->input = argument;
	    if (dsahip->dscb) {
		ds_free_record (dsahip);
		ds_init_record (dsahip);
		if (dsahip->recording)
		    ds_record (dsahip, 1);
	    }
	}
	break;
	case AHIC_Input_Query:
	return dsahip->input;
	case AHIC_Output:
	if (dsahip->output != argument) {
	    dsahip->output = argument;
	    if (dsahip->DS)
		ds_reinit (dsahip);
	}
	break;
	case AHIC_Output_Query:
	return dsahip->output;
    }
    return 0;
}

static uae_u32 AHIsub_Start (TrapContext *ctx)
{
    uae_u32 flags = m68k_dreg (&ctx->regs, 0);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    struct DSAHI *dsahip = GETAHI;
    int i;

    if (ahi_debug)
	write_log ("AHI: Play(%08x,%08x)\n",
	    flags, audioctrl);
    if ((flags & AHISF_PLAY) && !dsahip->playing) {
	dsahip->playing = 1;
	setevent (dsahip);
	for (i = 0; i < dsahip->channels; i++) {
	    struct dschannel *dc = &dsahip->channel[i];
	    ds_play (dsahip, dc);
	}
    }
    if ((flags & AHISF_RECORD) && !dsahip->recording) {
	dsahip->recording = 1;
	ds_init_record (dsahip);
	ds_record (dsahip, 1);
    }
    return 0;
}

static uae_u32 AHIsub_Stop (TrapContext *ctx)
{
    uae_u32 flags = m68k_dreg (&ctx->regs, 0);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    struct DSAHI *dsahip = GETAHI;
    int i;

    if (ahi_debug)
	write_log ("AHI: Stop(%08x,%08x)\n",
	    flags, audioctrl);
    if ((flags & AHISF_PLAY) && dsahip->playing) {
	dsahip->playing = 0;
	for (i = 0; i < dsahip->channels; i++) {
	    struct dschannel *dc = &dsahip->channel[i];
	    ds_stop (dsahip, dc);
	}
    }
    if ((flags & AHISF_RECORD) && dsahip->recording) {
	dsahip->recording = 0;
	ds_record (dsahip, 0);
	ds_free_record (dsahip);
    }
    return 0;
}

static uae_u32 AHIsub_Update (TrapContext *ctx)
{
    uae_u32 flags = m68k_dreg (&ctx->regs, 0);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    struct DSAHI *dsahip = GETAHI;
    if (ahi_debug)
	write_log ("AHI: Update(%08x,%08x)\n", flags, audioctrl);
    setevent (dsahip);
    return 0;
}

static uae_u32 AHIsub_SetVol (TrapContext *ctx)
{
    uae_u16 channel = m68k_dreg (&ctx->regs, 0);
    uae_u32 volume = m68k_dreg (&ctx->regs, 1);
    uae_u32 pan = m68k_dreg (&ctx->regs, 2);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    uae_u32 flags = m68k_dreg (&ctx->regs, 3);
    struct DSAHI *dsahip = GETAHI;
    struct dschannel *dc = GETCHANNEL;

    if (ahi_debug)
	write_log ("AHI: SetVol(%d,%d,%d,%08x,%08x)\n",
	    channel, volume, pan, audioctrl, flags);
    if (dc) {
	dc->csnext.volume = volume;
	dc->csnext.panning = pan;
	if (flags & AHISF_IMM) {
	    ds_setvolume (dsahip, dc);
	}
    }
    return 0;
}

static uae_u32 AHIsub_SetFreq (TrapContext *ctx)
{
    uae_u16 channel = m68k_dreg (&ctx->regs, 0);
    uae_u32 frequency = m68k_dreg (&ctx->regs, 1);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    uae_u32 flags = m68k_dreg (&ctx->regs, 3);
    struct DSAHI *dsahip = GETAHI;
    struct dschannel *dc = GETCHANNEL;

    if (ahi_debug)
	write_log ("AHI: SetFreq(%d,%d,%08x,%08x)\n",
	    channel, frequency, audioctrl, flags);
    if (dc) {
	dc->csnext.frequency = frequency;
	if (flags & AHISF_IMM) {
	    ds_setfreq (dsahip, dc);
	    ds_play (dsahip, dc);
	}
    }
    return 0;
}

static uae_u32 AHIsub_SetSound (TrapContext *ctx)
{
    uae_u16 channel = m68k_dreg (&ctx->regs, 0);
    uae_u16 sound = m68k_dreg (&ctx->regs, 1);
    uae_u32 offset = m68k_dreg (&ctx->regs, 2);
    uae_u32 length  = m68k_dreg (&ctx->regs, 3);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    uae_u32 flags = m68k_dreg (&ctx->regs, 4);
    struct DSAHI *dsahip = GETAHI;
    struct dssample *ds = GETSAMPLE;
    struct dschannel *dc = GETCHANNEL;

    if (ahi_debug)
	write_log ("AHI: SetSound(%d,%d,%08x,%d,%08x,%08x)\n",
	    channel, sound, offset, length, audioctrl, flags);
    if (dc == NULL)
	return AHIE_UNKNOWN;
    if (ds->num < 0)
	return AHIE_UNKNOWN;
    if (sound == 0xffff) {
	if (flags & AHISF_IMM) {
	    dc->cs.ds = NULL;
	    dc->csnext.ds = NULL;
	}
	dc->csnext.srcplaylen = -1;
	return 0;
    }
    ds_allocchannel (dsahip, dc);
    dc->cs.backwards = length < 0;
    length = abs (length);
    if (length == 0)
        length = ds->len;
    if (length > ds->len)
	length = ds->len;
    dc->csnext.ds = ds;
    dc->csnext.srcplaylen = length;
    dc->csnext.srcplayoffset = offset;
    if (flags & AHISF_IMM)
	dc->cs.ds = NULL;
    if (dc->cs.ds == NULL) {
	dc->buffercursor = 0;
	dc->hsync = 0;
    }
    ds_setfreq (dsahip, dc);
    ds_setvolume (dsahip, dc);
    ds_play (dsahip, dc);
    return 0;
}

static uae_u32 AHIsub_SetEffect (TrapContext *ctx)
{
    uae_u32 effect = m68k_areg (&ctx->regs, 0);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    uae_u32 effectype = get_long (effect);
    uae_u32 puaebase = get_long (audioctrl + ahiac_DriverData);
    struct DSAHI *dsahip = GETAHI;

    if (ahi_debug)
	write_log ("AHI: SetEffect(%08x (%08x),%08x)\n", effect, effectype, audioctrl);
    switch (effectype)
    {
	case AHIET_CHANNELINFO:
	put_long (puaebase + pub_ChannelInfo, effect);
	break;
	case AHIET_CHANNELINFO | AHIET_CANCEL:
	put_long (puaebase + pub_ChannelInfo, 0);
	break;
	case AHIET_MASTERVOLUME:
	case AHIET_MASTERVOLUME | AHIET_CANCEL:
	break;
	default:
	return AHIE_UNKNOWN;
    }
    return AHIE_OK;
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
    struct dssample *ds = GETSAMPLE;
    int ch;
    int bps;

    if (ahi_debug)
	write_log ("AHI: LoadSound(%d,%d,%08x,%08x,SMP=%d,ADDR=%08x,LEN=%d)\n",
	    sound, type, info, audioctrl, sampletype, addr, len);

    if (!ds)
	return AHIE_BADSOUNDTYPE;

    ds->num = sound;
    if (!cansurround && sampletype == AHIST_L7_1)
        return AHIE_BADSOUNDTYPE;
    ds->addr = addr;
    ds->sampletype = sampletype;
    ds->type = type;
    ds->len = len;

    switch (sampletype)
    {
	case AHIST_M8S:
	case AHIST_M16S:
	case AHIST_M32S:
	ch = 1;
	break;
	case AHIST_S8S:
	case AHIST_S16S:
	case AHIST_S32S:
	ch = 2;
	break;
	case AHIST_L7_1:
	ch = 8;
	break;
	default:
	return 0;
    }
    switch (sampletype)
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
    ds->bitspersample = bps;
    ds->ch = ch;
    ds->bytespersample = bps / 8;
    return AHIE_OK;
}

static uae_u32 AHIsub_UnloadSound (TrapContext *ctx)
{
    uae_u16 sound = m68k_dreg (&ctx->regs, 0);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    struct DSAHI *dsahip = GETAHI;
    struct dssample *ds = GETSAMPLE;

    if (ahi_debug)
	write_log ("AHI: UnloadSound(%d,%08x)\n",
	    sound, audioctrl);
    ds->num = -1;
    return AHIE_OK;
}

static uae_u32 REGPARAM2 ahi_demux (TrapContext *ctx)
{
    uae_u32 ret = 0;
    uae_u32 sp = m68k_areg (&ctx->regs, 7);
    uae_u32 offset = get_long (sp + 4);

    if (0 && ahi_debug)
	write_log ("AHI: %d\n", offset);

    switch (offset)
    {
	case 0xffffffff:
	    ret = init (ctx);
	break;
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

void free_ahi_v2 (void)
{
    ds_free_record (&dsahi[0]);
    ds_free (&dsahi[0]);
}

#endif
