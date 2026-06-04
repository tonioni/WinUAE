#ifndef WINUAE_OD_UNIX_SOUNDDEP_SOUND_H
#define WINUAE_OD_UNIX_SOUNDDEP_SOUND_H

#include "uae/types.h"

#define SOUNDSTUFF 1
#define SOUND_MODE_NG 0
#define DEFAULT_SOUND_MAXB 16384
#define DEFAULT_SOUND_MINB 16384
#define DEFAULT_SOUND_BITS 16
#define DEFAULT_SOUND_FREQ 44100
#define HAVE_STEREO_SUPPORT 1

#define FILTER_SOUND_OFF 0
#define FILTER_SOUND_EMUL 1
#define FILTER_SOUND_ON 2
#define FILTER_SOUND_TYPE_A500 0
#define FILTER_SOUND_TYPE_A1200 1
#define FILTER_SOUND_TYPE_A500_FIXEDONLY 2

extern uae_u16 paula_sndbuffer[];
extern uae_u16 *paula_sndbufpt;
extern int paula_sndbufsize;
extern int active_sound_stereo;

void finish_sound_buffer(void);
void restart_sound_buffer(void);
void pause_sound_buffer(void);
int init_sound(void);
void close_sound(void);
int setup_sound(void);
void resume_sound(void);
void pause_sound(void);
void reset_sound(void);
bool sound_paused(void);
void sound_setadjust(float);
int enumerate_sound_devices(void);
void sound_mute(int);
void sound_volume(int);
void set_volume(int, int);
void master_sound_volume(int);

#define PUT_SOUND_WORD(b) do { *(uae_u16 *)paula_sndbufpt = (b); paula_sndbufpt = (uae_u16 *)(((uae_u8 *)paula_sndbufpt) + 2); } while (0)
#define PUT_SOUND_WORD_MONO(b) PUT_SOUND_WORD(b)
#define SOUND16_BASE_VAL 0
#define SOUND8_BASE_VAL 128

#endif /* WINUAE_OD_UNIX_SOUNDDEP_SOUND_H */
