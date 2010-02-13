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


static int ahi_debug = 1;

#define UAE_MAXCHANNELS 24
#define UAE_MAXSOUNDS 256

#define ub_Flags 34
#define ub_Pad1 (ub_Flags + 1)
#define ub_Pad2 (ub_Pad1 + 1)
#define ub_SysLib (ub_Pad2 + 2)
#define ub_SegList (ub_SysLib + 4)
#define ub_DOSBase (ub_SegList + 4)
#define ub_AHIFunc (ub_DOSBase + 4)

#define pub_SizeOf 0
#define pub_Index (pub_SizeOf + 4)
#define pub_Base (pub_Index + 4)
#define pub_audioctrl (pub_Base + 4)
#define pub_FuncTask (pub_audioctrl + 4)
#define pub_WaitMask (pub_FuncTask + 4)
#define pub_WaitSigBit (pub_WaitMask + 4)
#define pub_FuncMode (pub_WaitSigBit +4)
#define pub_TaskMode (pub_FuncMode + 2)
#define pub_ChannelSignal (pub_TaskMode + 2)

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

struct dssample {
    int num;
    int ch, chout;
    int bitspersample;
    int bitspersampleout;
    int bytespersample;
    int bytespersampleout;
    uae_u32 addr;
    uae_u32 len;
    uae_u32 type;
    int streaming;
    int channel;
};

struct dschannel {
    int frequency;
    int volume;
    int panning;
    struct dssample *ds;
    LPDIRECTSOUNDBUFFER8 dsb;
    LPDIRECTSOUNDBUFFER8 dsbback;
};

struct DSAHI {
    uae_u32 audioctrl;
    uae_u32 audioid;
    int output;
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
    struct dschannel *channel;
    int playing, recording;
    evt evttime;
    volatile int thread;
    HANDLE threadevent;
    CRITICAL_SECTION cs;
    uae_u32 signalchannelmask;
};

static struct DSAHI dsahi[1];

#define GETAHI (&dsahi[get_long(get_long(audioctrl + ahiac_DriverData) + pub_Index)])
#define GETSAMPLE (dsahip && sound >= 0 && sound < UAE_MAXSOUNDS ? &dsahip->sample[sound] : NULL)
#define GETCHANNEL (dsahip && channel >= 0 && channel < UAE_MAXCHANNELS ? &dsahip->channel[channel] : NULL)

static int default_freq = 44100;
static int cansurround;
static uae_u32 xahi_author, xahi_copyright, xahi_version, xahi_output[MAX_SOUND_DEVICES], xahi_output_num;
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
    uae_u32 task, signalmask;
    uae_s16 taskmode = get_word (puaebase + pub_TaskMode);
    task = get_long (puaebase + pub_FuncTask);
    signalmask = get_long (puaebase + pub_WaitMask);

    if (!dsahip->playing || ahi_paused)
	return 0;
    if (taskmode <= 0)
	return 0;
    if (dsahip->enabledisable)
	return 0;
    uae_Signal (task, signalmask);
    return 1;
}

static void setchannelevent (struct DSAHI *dsahip, struct dschannel *dc)
{
    uae_u32 audioctrl = dsahip->audioctrl;
    uae_u32 puaebase = get_long (audioctrl + ahiac_DriverData);
    int ch = dc - &dsahip->channel[0];

    if (!dsahip->playing || ahi_paused || dc->ds == NULL || dc->ds->ready <= 0)
	return;
    //write_log ("AHI: channel signal %d\n", ch);
    put_long (puaebase + pub_ChannelSignal, get_long (puaebase + pub_ChannelSignal) | (1 << ch));
    sendsignal (dsahip);
}

static void evtfunc (uae_u32 v)
{
    if (ahi_active) {
	struct DSAHI *dsahip = &dsahi[v];
	uae_u32 audioctrl = dsahip->audioctrl;
	uae_u32 puaebase = get_long (audioctrl + ahiac_DriverData);
       
	put_word (puaebase + pub_FuncMode, get_word (puaebase + pub_FuncMode) | 1);
	if (sendsignal (dsahip))
	    event2_newevent2 (dsahip->evttime, v, evtfunc);
	else
	    dsahip->evttime = 0;
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
    write_log ("AHI: playerfunc freq = %.2fHz\n", f);
    if (f < 1)
	return;
    cycles = maxhpos * maxvpos * vblank_hz;
    t = (evt)(cycles / f);
    if (dsahip->evttime == t)
	return;
    dsahip->evttime = t;
    if (t < 10)
	return;
    event2_newevent2 (t, dsahip - &dsahi[0], evtfunc);
}



static LPDIRECTSOUND8 lpDS;
const static GUID KSDATAFORMAT_SUBTYPE_PCM = {0x00000001,0x0000,0x0010,
    {0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};
#define KSAUDIO_SPEAKER_QUAD_SURROUND   (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | \
					 SPEAKER_SIDE_LEFT  | SPEAKER_SIDE_RIGHT)

static void ds_freebuffer (struct DSAHI *ahidsp, struct dssample *ds)
{
    if (!ds)
	return;
    if (ds->dsb)
	IDirectSoundBuffer8_Release (ds->dsb);
    if (ds->dsbback)
	IDirectSoundBuffer8_Release (ds->dsbback);
    if (ds->dsnotify)
	IDirectSoundNotify_Release (ds->dsnotify);
    if (ds->notifyevent)
	CloseHandle (ds->notifyevent);
    memset (ds, 0, sizeof (struct dssample));
    ds->channel = -1;
}

static void ds_free (struct DSAHI *dsahip)
{
    int i;

    for (i = 0; i < dsahip->sounds; i++) {
	struct dssample *ds = &dsahip->sample[i];
	ds_freebuffer (dsahip, ds);
    }
    if (lpDS)
	IDirectSound_Release (lpDS);
    lpDS = NULL;
    if (dsahip->thread) {
	dsahip->thread = -1;
	SetEvent (dsahip->threadevent);
	while (dsahip->thread)
	    Sleep (2);
    }
    DeleteCriticalSection (&dsahip->cs);
    if (ahi_debug)
	write_log ("AHI: DSOUND freed\n");
}

static unsigned __stdcall waitthread (void *f)
{
    struct DSAHI *dsahip = f;

    dsahip->thread = 1;
    if (ahi_debug)
	write_log ("AHI: waitthread() started\n");
    while (dsahip->thread > 0) {
	HANDLE handles[UAE_MAXCHANNELS + 1];
	struct dssample *dss[UAE_MAXCHANNELS + 1];
	DWORD ob;
	int maxnum = UAE_MAXCHANNELS + 1;
	int num = 0;
	int i;
    
	EnterCriticalSection (&dsahip->cs);
	handles[num++] = dsahip->threadevent;
	if (dsahip->playing) {
	    for (i = 0; i < UAE_MAXSOUNDS && num < maxnum; i++) {
		struct dssample *ds = &dsahip->sample[i];
		if (ds->channel >= 0 && ds->notifyevent) {
		    handles[num] = ds->notifyevent;
		    dss[num] = ds;
		    num++;
		}
	    }
	}
	LeaveCriticalSection (&dsahip->cs);
	//write_log ("AHI: WFMO %d\n", num);
	ob = WaitForMultipleObjects (num, handles, FALSE, INFINITE);
	EnterCriticalSection (&dsahip->cs);
	if (ob >= WAIT_OBJECT_0 + 1 && ob < WAIT_OBJECT_0 + num) {
	    int ch;
	    struct dssample *ds;
	    ob -= WAIT_OBJECT_0;
	    ds = dss[ob];
	    if (ds->notloopingyet) {
		ds->notloopingyet = 0;
	    } else {
		ch = ds->channel;
		dsahip->signalchannelmask |= 1 << ch;
	    }
	}
	LeaveCriticalSection (&dsahip->cs);
    }
    if (ahi_debug)
	write_log ("AHI: waitthread() killed\n");
    dsahip->thread = 0;
    return 0;
}

DWORD fillsupportedmodes (LPDIRECTSOUND8 lpDS, int freq, struct dsaudiomodes *dsam);
static struct dsaudiomodes supportedmodes[16];

static int ds_init (struct DSAHI *dsahip)
{
    unsigned int ta;
    int freq = 48000;
    DSCAPS DSCaps;
    HRESULT hr;
    DWORD speakerconfig;

    InitializeCriticalSection (&dsahip->cs);

    hr = DirectSoundCreate8 (&sound_devices[dsahip->output].guid, &lpDS, NULL);
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
	    cansurround = 1;
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

    dsahip->thread = -1;
    dsahip->threadevent = CreateEvent (NULL, FALSE, FALSE, NULL);
    _beginthreadex (NULL, 0, waitthread, dsahip, 0, &ta);

    if (ahi_debug)
	write_log ("AHI: DSOUND initialized\n");

    return 1;
error:
    if (ahi_debug)
	write_log ("AHI: DSOUND initialization failed\n");
    ds_free (dsahip);
    return 0;
}

static void ds_play (struct DSAHI *dsahip, struct dssample *ds)
{
    HRESULT hr;
    DWORD status;

    if (ahi_debug)
	write_log ("AHI: ds_play(%d)\n", ds->num);
    hr = IDirectSoundBuffer8_GetStatus (ds->dsb, &status);
    if (FAILED (hr))
	write_log ("AHI: IDirectSoundBuffer8_GetStatus() failed, %s\n", DXError (hr));
    hr = IDirectSoundBuffer8_Play (ds->dsb, 0, 0, DSBPLAY_LOOPING);
    if (FAILED (hr))
	write_log ("AHI: IDirectSoundBuffer8_Play() failed, %s\n", DXError (hr));
    if (ds->dsbback) {
	hr = IDirectSoundBuffer8_Play (ds->dsbback, 0, 0, DSBPLAY_LOOPING);
	if (FAILED (hr))
	    write_log ("AHI: IDirectSoundBuffer8_PlayBack() failed, %s\n", DXError (hr));
    }
    ds->notloopingyet = 1;
    SetEvent (dsahip->threadevent);
    ds->ready = -1;
}

static void ds_initsound (struct dssample *ds)
{
    HRESULT hr;

    hr = IDirectSoundBuffer_SetPan (ds->dsb, DSBPAN_CENTER);
    hr = IDirectSoundBuffer_SetVolume (ds->dsb, DSBVOLUME_MIN);
    if (ds->dsbback) {
	hr = IDirectSoundBuffer_SetPan (ds->dsbback, DSBPAN_CENTER);
        hr = IDirectSoundBuffer_SetVolume (ds->dsbback, DSBVOLUME_MIN);
    }
}

static void ds_setvolume (struct dssample *ds, int volume, int panning)
{
    HRESULT hr;
    LONG vol, pan;

    // weird AHI features:
    // negative pan = output from surround speakers!
    // negative volume = invert sample data!! (not yet emulated)
    vol = (LONG)((DSBVOLUME_MIN / 2) + (-DSBVOLUME_MIN / 2) * log (1 + (2.718281828 - 1) * (abs (volume) / 65536.0)));
    pan = (abs (panning) - 0x8000) * DSBPAN_RIGHT / 32768;
    write_log ("%d %d\n", vol, pan);
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
    ds->volume = volume;
    ds->panning = panning;
}

static void ds_setfreq (struct DSAHI *dsahip, struct dssample *ds, int frequency)
{
    HRESULT hr;

    if (frequency == 0) {
	hr = IDirectSoundBuffer8_Stop (ds->dsb);
	if (ds->dsbback)
	    hr = IDirectSoundBuffer8_Stop (ds->dsbback);
    } else if (ds->frequency != frequency) {
	hr = IDirectSoundBuffer8_SetFrequency (ds->dsb, frequency);
	if (FAILED (hr))
	    write_log ("AHI: SetFrequency(%d,%d) failed: %s\n", ds->num, frequency, DXError (hr));
	if (ds->dsbback) {
	    hr = IDirectSoundBuffer8_SetFrequency (ds->dsbback, frequency);
	    if (FAILED (hr))
		write_log ("AHI: SetFrequencyBack(%d,%d) failed: %s\n", ds->num, frequency, DXError (hr));
	}
	if (ds->frequency == 0)
	    ds_play (dsahip, ds);
    }
    ds->frequency = frequency;
}

#define US(x) ((x))

static void copysampledata (struct dssample *ds, void *srcp, void *dstp, int dstsize, int offset, int srcsize)
{
    int i, j;
    uae_u8 *src = (uae_u8*)srcp;
    uae_u8 *dst = (uae_u8*)dstp;
    int och = ds->chout;
    int ich = ds->ch;

    src += offset * ds->ch * ds->bytespersample;
    if (dstsize < srcsize)
	srcsize = dstsize;

    switch (ds->type)
    {
	case AHIST_M8S:
	    for (i = 0; i < srcsize; i++) {
		dst[i * 4 + 0] = US (src[i]);
		dst[i * 4 + 1] = US (src[i]);
		dst[i * 4 + 2] = US (src[i]);
		dst[i * 4 + 3] = US (src[i]);
	    }
	break;
	case AHIST_S8S:
	    for (i = 0; i < srcsize; i++) {
		dst[i * 4 + 0] = src[i * 2 + 0];
		dst[i * 4 + 1] = src[i * 2 + 0];
		dst[i * 4 + 2] = src[i * 2 + 1];
		dst[i * 4 + 3] = src[i * 2 + 1];
	    }
	break;
	case AHIST_M16S:
	    for (i = 0; i < srcsize; i++) {
		dst[i * 4 + 0] = src[i * 2 + 1];
		dst[i * 4 + 1] = src[i * 2 + 0];
		dst[i * 4 + 2] = src[i * 2 + 1];
		dst[i * 4 + 3] = src[i * 2 + 0];
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
		dst[i * 4 + 0] = src[i * 4 + 3];
		dst[i * 4 + 1] = src[i * 4 + 2];
		dst[i * 4 + 2] = src[i * 4 + 3];
		dst[i * 4 + 3] = src[i * 4 + 2];
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
	    if (ds->chout == 8) {
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

static void clearsample (struct dssample *ds, LPDIRECTSOUNDBUFFER8 dsb, int dstlength)
{
    HRESULT hr;
    void *buffer1;
    DWORD size1, outlen;

    if (!dsb)
	return;
    outlen = dstlength * ds->bytespersampleout * ds->chout;
    hr = IDirectSoundBuffer8_Lock (dsb, 0, outlen, &buffer1, &size1, NULL, NULL, 0);
    if (hr == DSERR_BUFFERLOST) {
	IDirectSoundBuffer_Restore (dsb);
	hr = IDirectSoundBuffer8_Lock (dsb, 0, outlen, &buffer1, &size1, NULL, NULL, 0);
    }
    if (FAILED (hr))
	return;
    memset (buffer1, 0, size1);
    IDirectSoundBuffer8_Unlock (dsb, buffer1, size1, NULL, 0);
}

static void copysample (struct dssample *ds, LPDIRECTSOUNDBUFFER8 dsb, int dstoffset, int dstlength, uae_u32 srcoffset, uae_u32 srclength)
{
    HRESULT hr;
    void *buffer1, *buffer2;
    DWORD size1, size2, outoffset, outlen;
    uae_u32 addr;

    if (!dsb)
	return;
    outlen = dstlength * ds->bytespersampleout * ds->chout;
    outoffset = dstoffset * ds->bytespersampleout * ds->chout;
    hr = IDirectSoundBuffer8_Lock (dsb, outoffset, outlen, &buffer1, &size1, &buffer2, &size2, 0);
    if (hr == DSERR_BUFFERLOST) {
	IDirectSoundBuffer_Restore (dsb);
	hr = IDirectSoundBuffer8_Lock (dsb, outoffset, outlen, &buffer1, &size1, &buffer2, &size2, 0);
    }
    if (FAILED (hr))
	return;
    if (ds->addr == 0 && ds->len == 0xffffffff)
	addr = srcoffset;
    else
	addr = ds->addr;
    if (valid_address (addr + srcoffset * ds->ch * ds->bytespersample, srclength * ds->ch * ds->bytespersample)) {
	uae_u8 *naddr = get_real_address (addr);
	int part1 = size1 / (ds->bytespersampleout * ds->chout);
	int part2 = size2 / (ds->bytespersampleout * ds->chout);
	copysampledata (ds, naddr, buffer1, part1, srcoffset, srclength);
	srcoffset += part1;
	srclength -= part1;
	if (srclength != 0)
	    copysampledata (ds, naddr, buffer2, part2, srcoffset, srclength);
    }
    IDirectSoundBuffer8_Unlock (dsb, buffer1, size1, buffer2, size2);
}

void ahi_hsync (void)
{
    struct DSAHI *dsahip = &dsahi[0];
    uae_u32 audioctrl = dsahip->audioctrl;
    uae_u32 puaebase = get_long (audioctrl + ahiac_DriverData);

    if (dsahip->signalchannelmask) {
	EnterCriticalSection (&dsahip->cs);
	if (dsahip->playing && !ahi_paused) {
	    write_log ("AHI: channel signal %08x\n", dsahip->signalchannelmask);
	    put_long (puaebase + pub_ChannelSignal, get_long (puaebase + pub_ChannelSignal) | dsahip->signalchannelmask);
	    sendsignal (dsahip);
	}
	dsahip->signalchannelmask = 0;
	LeaveCriticalSection (&dsahip->cs);
    }
}

void ahi_vsync (void)
{
    struct DSAHI *dsahip = &dsahi[0];
    int i;

    if (!dsahip->playing || ahi_paused)
	return;
    return;
    for (i = 0; i < UAE_MAXCHANNELS; i++) {
	HRESULT hr;
	DWORD playc, writec;
	struct dssample *ds = dsahip->channel[i].ds;

	if (ds == NULL)
	    continue;
	if (ds->type != AHIST_DYNAMICSAMPLE)
	    continue;
	hr = IDirectSoundBuffer8_GetCurrentPosition (ds->dsb, &playc, &writec);
	if (FAILED (hr)) {
	    write_log ("AHI: GetCurrentPosition(%d) failed, %s\n", ds->channel, DXError (hr));
	    continue;
	}
	write_log ("AHI: ch=%d writec=%d\n", ds->channel, writec / (ds->bytespersampleout * ds->chout));
	copysample (ds, ds->dsb,
	    writec / (ds->bytespersampleout * ds->chout),
	    ds->len - 1000,
	    writec / (ds->bytespersampleout * ds->chout),
	    ds->len - 1000);
    }
}

static void ds_stop (struct dssample *ds)
{
    HRESULT hr;

    if (!ds->ready)
	return;
    if (ahi_debug)
	write_log ("AHI: ds_stop(%d)\n", ds->num);
    hr = IDirectSoundBuffer8_Stop (ds->dsb);
    if (FAILED (hr))
	write_log ("AHI: IDirectSoundBuffer8_Stop() failed, %s\n", DXError (hr));
    if (ds->dsbback) {
	hr = IDirectSoundBuffer8_Stop (ds->dsbback);
	if (FAILED (hr))
	    write_log ("AHI: IDirectSoundBuffer8_StopBack() failed, %s\n", DXError (hr));
    }
    ds->ready = 1;
}

void ahi2_pause_sound (int paused)
{
    int i;
    struct DSAHI *dsahip = &dsahi[0];

    ahi_paused = paused;
    if (!dsahip->playing && !dsahip->recording)
	return;
    for (i = 0; i < UAE_MAXCHANNELS; i++) {
	struct dssample *ds = dsahip->channel[i].ds;
	if (ds == NULL)
	    continue;
	if (paused) {
	    ds_stop (ds);
	} else {
	    ds_play (dsahip, ds);
	}
    }
}

static void ds_setsound (struct DSAHI *dsahip, struct dssample *ds, struct dschannel *dc, int offset, int length)
{
    HRESULT hr;

    ds_setvolume (ds, dc->volume, dc->panning);
    ds_setfreq (dsahip, ds, dc->frequency);
    clearsample (ds, ds->dsb, ds->len);
    clearsample (ds, ds->dsbback, ds->len);
    copysample (ds, ds->dsb, 0, ds->len, offset, length);
    copysample (ds, ds->dsbback, 0, ds->len, offset, length);
    hr = IDirectSoundBuffer8_SetCurrentPosition (ds->dsb, 0);
    if (FAILED (hr))
	write_log ("AHI: IDirectSoundBuffer8_SetCurrentPosition() failed, %s\n", DXError (hr));
    if (ds->dsbback) {
	hr = IDirectSoundBuffer8_SetCurrentPosition (ds->dsbback, 0);
	if (FAILED (hr))
	    write_log ("AHI: IDirectSoundBuffer8_SetCurrentPositionBack() failed, %s\n", DXError (hr));
    }
}

static int ds_allocbuffer (struct DSAHI *ahidsp, struct dssample *ds, int type, uae_u32 len)
{
    HRESULT hr;
    DSBUFFERDESC dd;
    WAVEFORMATEXTENSIBLE wavfmt;
    LPDIRECTSOUNDBUFFER pdsb;
    LPDIRECTSOUNDBUFFER8 pdsb8;
    int round, chround;
    int channels[] = { 2, 4, 6, 8 };
    int ch, chout, bps, bpsout;
    DSBPOSITIONNOTIFY pn[1];

    if (!ds)
	return 0;
    switch (type)
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
	channels[0] = 6;
	channels[1] = 8;
	channels[2] = 0;
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
    if (ahi_debug)
	write_log ("AHI: AllocBuffer ch=%d,bps=%d\n", ch, bps);

    pdsb = NULL;
    bpsout = 16;
    for (chround = 0; channels[chround]; chround++) {
	chout = channels[chround];
	for (round = 0; supportedmodes[round].ch; round++) {
	    DWORD ksmode = 0;

	    pdsb = NULL;
	    if (supportedmodes[round].ch != chout)
		continue;

 	    memset (&wavfmt, 0, sizeof (WAVEFORMATEXTENSIBLE));
	    wavfmt.Format.nChannels = chout;
	    wavfmt.Format.nSamplesPerSec = default_freq;
	    wavfmt.Format.wBitsPerSample = bpsout;
	    if (chout <= 2) {
		wavfmt.Format.wFormatTag = WAVE_FORMAT_PCM;
	    } else {
		DWORD ksmode = 0;
		wavfmt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
		wavfmt.Format.cbSize = sizeof (WAVEFORMATEXTENSIBLE) - sizeof (WAVEFORMATEX);
		wavfmt.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
		wavfmt.Samples.wValidBitsPerSample = bpsout;
		wavfmt.dwChannelMask = supportedmodes[round].ksmode;
	    }
	    wavfmt.Format.nBlockAlign = bpsout / 8 * wavfmt.Format.nChannels;
	    wavfmt.Format.nAvgBytesPerSec = wavfmt.Format.nBlockAlign * wavfmt.Format.nSamplesPerSec;

	    memset (&dd, 0, sizeof dd);
  	    dd.dwSize = sizeof dd;
	    dd.dwBufferBytes = len * bpsout / 8 * chout;
	    dd.lpwfxFormat = &wavfmt.Format;
	    dd.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS | DSBCAPS_CTRLPOSITIONNOTIFY;
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
		chout, bpsout, DXError (hr));
	}
	if (pdsb)
	    break;
    }
    if (pdsb == NULL)
	goto error;

    hr = IDirectSound_QueryInterface (pdsb, &IID_IDirectSoundBuffer8, (LPVOID*)&pdsb8);
    if (FAILED (hr))  {
	write_log ("AHI: Secondary QueryInterface(IID_IDirectSoundBuffer8) failure: %s\n", DXError (hr));
	goto error;
    }

    IDirectSound_Release (pdsb);
    ds->dsb = pdsb8;

    hr = IDirectSoundBuffer8_QueryInterface (pdsb, &IID_IDirectSoundNotify8, (void**)&ds->dsnotify);
    if (FAILED (hr)) {
	write_log ("AHI: Secondary QueryInterface(IID_IDirectSoundNotify8) failure: %s\n", DXError (hr));
	goto error;
    }
    ds->notifyevent = CreateEvent (NULL, FALSE, FALSE, NULL);
    pn[0].dwOffset = 0;
    pn[0].hEventNotify = ds->notifyevent;
    hr = IDirectSoundNotify_SetNotificationPositions (ds->dsnotify, 1, pn);
    if (FAILED (hr)) {
	write_log ("AHI: Secondary SetNotificationPositions() failure: %s\n", DXError (hr));
	goto error;
    }


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
     
    ds_initsound (ds);

    ds->bitspersample = bps;
    ds->bitspersampleout = bpsout;
    ds->ch = ch;
    ds->chout = chout;
    ds->bytespersample = bps / 8;
    ds->bytespersampleout = bpsout / 8;

    SetEvent (ahidsp->threadevent);

    return 1;

error:
    return 0;
}

static uae_u32 init (TrapContext *ctx)
{
    int num, i;
    
    num = enumerate_sound_devices ();
    xahi_author = ds ("Toni Wilen");
    xahi_copyright = ds ("GPL");
    xahi_version = ds ("uae2 0.1 (xx.xx.2008)\r\n");
    for (i = 0; i < num; i++)
	xahi_output[i] = ds (sound_devices[i].name);
    xahi_output_num = num;
    return 1;
}

static uae_u32 AHIsub_AllocAudio (TrapContext *ctx)
{
    int i;
    uae_u32 tags = m68k_areg (&ctx->regs, 1);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    uae_u32 pbase = get_long (audioctrl + ahiac_DriverData);
    uae_u32 tag, data, v;
    uae_u32 ret = AHISF_KNOWSTEREO | AHISF_KNOWHIFI;
    struct DSAHI *dsahip = &dsahi[0];

    if (ahi_debug)
	write_log ("AHI: AllocAudio(%08x,%08x)\n", tags, audioctrl);

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

    if (cansurround)
	ret |= AHISF_KNOWMULTICHANNEL;

    while ((tag = gettag (&tags, &data))) {
	if (ahi_debug)
	    write_log ("- TAG %08x=%d: %08x=%u\n", tag, tag & 0x7fff, data, data);
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
    dsahip->sample = xcalloc (sizeof (struct dssample), dsahip->sounds);
    dsahip->channel = xcalloc (sizeof (struct dschannel), dsahip->channels);
    for (i = 0; i < dsahip->channels; i++) {
	struct dschannel *dc = &dsahip->channel[i];
    }
    for (i = 0; i < dsahip->sounds; i++) {
	struct dssample *ds = &dsahip->sample[i];
	ds->channel = -1;
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
    put_long (pbase + pub_Index, -1);
    if (dsahip) {
	ahi_active = 0;
	ds_free (dsahip);
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
	return TRUE;
	case AHIDB_Realtime:
	return TRUE;
	case AHIDB_Outputs:
	return xahi_output_num;
	case AHIDB_MinOutputVolume:
	return 0x00000;
	case AHIDB_MaxOutputVolume:
	return 0x10000;
	case AHIDB_Output:
	if (argument >= 0 && argument < xahi_output_num)
	    return xahi_output[argument];
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
	write_log ("AHI: GetAttr(%08x=%d,%08x,%08x)=%08x\n", attribute, (attribute & 0x7fff), argument, def, v);

    return v;
}

static uae_u32 AHIsub_HardwareControl (TrapContext *ctx)
{
    uae_u32 attribute = m68k_dreg (&ctx->regs, 0);
    uae_u32 argument = m68k_dreg (&ctx->regs, 1);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    struct DSAHI *dsahip = GETAHI;
    if (ahi_debug)
	write_log ("AHI: HardwareControl(%08x,%08x,%08x)\n", attribute, argument, audioctrl);
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
    if (flags & AHISF_PLAY)
	dsahip->playing = 1;
    if (flags & AHISF_RECORD)
	dsahip->recording = 1;
    if (dsahip->playing) {
	setevent (dsahip);
	for (i = 0; i < dsahip->channels; i++) {
	    struct dschannel *dc = &dsahip->channel[i];
	    if (dc->
	    if (ds->ready > 0 && ds->channel >= 0) {
		struct dschannel *dc = &dsahip->channel[ds->channel];
		ds_setvolume (ds, dc->volume, dc->panning);
		ds->frequency = 0;
		ds_setfreq (dsahip, ds, dc->frequency); // this also starts playback
	    }
	}
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
    if (flags & AHISF_PLAY)
	dsahip->playing = 0;
    if (flags & AHISF_RECORD)
	dsahip->recording = 0;
    if (!dsahip->playing) {
	for (i = 0; i < dsahip->sounds; i++) {
	    struct dssample *ds = &dsahip->sample[i];
	    if (ds->ready)
		ds_stop (ds);
	}
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
    int i;
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
	dc->volume = volume;
	dc->panning = pan;
	for (i = 0; i < dsahip->sounds; i++) {
	    struct dssample *ds = &dsahip->sample[i];
	    if (ds->channel == channel)
		ds_setvolume (ds, volume, pan); 
	}
    }
    return 0;
}

static uae_u32 AHIsub_SetFreq (TrapContext *ctx)
{
    int i;
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
	dc->frequency = frequency;
	for (i = 0; i < dsahip->sounds; i++) {
	    struct dssample *ds = &dsahip->sample[i];
	    if (ds->channel == channel)
		ds_setfreq (dsahip, ds, frequency);
	}
    }
   return 0;
}

static uae_u32 AHIsub_SetSound (TrapContext *ctx)
{
    int i;
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
    dc->ds = NULL;
    if (sound == 0xffff) {
	for (i = 0; i < dsahip->sounds; i++) {
	    struct dssample *ds2 = &dsahip->sample[i];
	    if (ds2->channel == channel) {
	        ds_stop (ds2);
	        ds2->channel = -1;
		
	    }
	}
    } else if (ds) {
	length = abs (length);
	if (length == 0)
	    length = ds->len;
	ds->ready = 1;
	ds->channel = channel;
	dc->ds = ds;
	for (i = 0; i < dsahip->sounds; i++) {
	    struct dssample *ds2 = &dsahip->sample[i];
	    if (ds2 != ds && ds2->channel == channel) {
	        ds_stop (ds2);
	        ds2->channel = -1;
	    }
	}
	ds_setsound (dsahip, ds, dc, offset, length);
	if (dsahip->playing)
	    ds_play (dsahip, ds);
    }
    return 0;
}

static uae_u32 AHIsub_SetEffect (TrapContext *ctx)
{
    uae_u32 effect = m68k_areg (&ctx->regs, 0);
    uae_u32 audioctrl = m68k_areg (&ctx->regs, 2);
    struct DSAHI *dsahip = GETAHI;

    if (ahi_debug)
	write_log ("AHI: SetEffect(%08x,%08x)\n", effect, audioctrl);
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
    struct dssample *ds = GETSAMPLE;

    if (ahi_debug)
	write_log ("AHI: LoadSound(%d,%d,%08x,%08x,SMP=%d,ADDR=%08x,LEN=%d)\n",
	    sound, type, info, audioctrl, sampletype, addr, len);

    if (ds) {
	ds->num = sound;
	if (!cansurround && type == AHIST_L7_1)
	    return AHIE_BADSOUNDTYPE;
	ds->addr = addr;
	ds->type = type;
	ds->len = len;
	ds->frequency = 0;
	if (ds_allocbuffer (dsahip, ds, type, len))
	    ret = AHIE_OK;
    }
    return ret;
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
    if (ds)
	ds_freebuffer (dsahip, ds);
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




#endif
