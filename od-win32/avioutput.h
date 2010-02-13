/*
  UAE - The Ultimate Amiga Emulator

  avioutput.h

  Copyright(c) 2001 - 2002 §ane
*/

extern int avioutput_video, avioutput_audio, avioutput_enabled, avioutput_requested;

extern int avioutput_width, avioutput_height, avioutput_bits;
extern int avioutput_fps;
extern int avioutput_framelimiter, avioutput_nosoundoutput;
extern int avioutput_nosoundsync, avioutput_originalsize;
extern int screenshot_originalsize;

extern TCHAR avioutput_filename[MAX_DPATH];

extern void AVIOutput_WriteAudio (uae_u8 *sndbuffer, int sndbufsize);
extern void AVIOutput_WriteVideo (void);
extern int AVIOutput_ChooseAudioCodec (HWND hwnd,TCHAR*,int);
extern int AVIOutput_GetAudioCodec (TCHAR*,int);
extern int AVIOutput_ChooseVideoCodec (HWND hwnd,TCHAR*,int);
extern int AVIOutput_GetVideoCodec (TCHAR*,int);
extern void AVIOutput_Restart (void);
extern void AVIOutput_End (void);
extern void AVIOutput_Begin (void);
extern void AVIOutput_Release (void);
extern void AVIOutput_Initialize (void);
extern void AVIOutput_RGBinfo (int,int,int,int,int,int);
extern void AVIOutput_GetSettings (void);
extern void AVIOutput_SetSettings (void);

extern void Screenshot_RGBinfo (int,int,int,int,int,int);

#define AVIAUDIO_AVI 1
#define AVIAUDIO_WAV 2
