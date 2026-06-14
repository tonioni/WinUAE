#ifndef UAE_UNIX_AVIOUTPUT_H
#define UAE_UNIX_AVIOUTPUT_H

extern int avioutput_video, avioutput_audio, avioutput_enabled, avioutput_requested;

extern int avioutput_width, avioutput_height, avioutput_bits;
extern int avioutput_framelimiter, avioutput_nosoundoutput;
extern int avioutput_nosoundsync, avioutput_originalsize;
extern int screenshot_originalsize;
extern int screenshot_paletteindexed;
extern int screenshot_clipmode;
extern int screenshot_multi;
extern int screenshotmode;

extern TCHAR avioutput_filename_gui[MAX_DPATH];
extern TCHAR avioutput_filename_auto[MAX_DPATH];
extern TCHAR avioutput_filename_inuse[MAX_DPATH];

extern void AVIOutput_Toggle(int mode, bool immediate);
extern bool AVIOutput_WriteAudio(uae_u8 *sndbuffer, int sndbufsize);
extern int AVIOutput_ChooseAudioCodec(void *hwnd, TCHAR*, int);
extern int AVIOutput_GetAudioCodec(TCHAR*, int);
extern int AVIOutput_ChooseVideoCodec(void *hwnd, TCHAR*, int);
extern int AVIOutput_GetVideoCodec(TCHAR*, int);
extern void AVIOutput_Restart(bool);
extern void AVIOutput_End(void);
extern void AVIOutput_Begin(bool);
extern void AVIOutput_Release(void);
extern void AVIOutput_Initialize(void);
extern void AVIOutput_RGBinfo(int, int, int, int, int, int, int, int);
extern void AVIOutput_GetSettings(void);
extern void AVIOutput_SetSettings(void);

extern void Screenshot_RGBinfo(int, int, int, int, int, int, int, int);

#define AVIAUDIO_AVI 1
#define AVIAUDIO_WAV 2

#endif /* UAE_UNIX_AVIOUTPUT_H */
