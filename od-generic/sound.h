 /* 
  * UAE - The Un*x Amiga Emulator
  * 
  * Support for the Mute sound system.
  * 
  * Copyright 1997 Bernd Schmidt
  */

static __inline__ void check_sound_buffers (void)
{
}

#define PUT_SOUND_BYTE(b) do { ; } while (0)
#define PUT_SOUND_WORD(b) do { ; } while (0)
#define SOUND16_BASE_VAL 0
#define SOUND8_BASE_VAL 0

#define DEFAULT_SOUND_MINB 8192
#define DEFAULT_SOUND_MAXB 8192
#define DEFAULT_SOUND_BITS 16
#define DEFAULT_SOUND_FREQ 44100

#define UNSUPPORTED_OPTION_B
#define UNSUPPORTED_OPTION_R
#define UNSUPPORTED_OPTION_b
#define UNSUPPORTED_OPTION_S

