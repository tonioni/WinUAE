#include "sysconfig.h"
#include "sysdeps.h"

#define SND_MAX_BUFFER 65536

uae_u16 paula_sndbuffer[SND_MAX_BUFFER];
uae_u16 *paula_sndbufpt;

int paula_sndbufsize = 0;
int active_sound_stereo;

void audio_finish_pull(void) {
    //UNIMPLEMENTED();
}

void sound_volume(int) {
    UNIMPLEMENTED();
}

void set_volume(int, int) {
    UNIMPLEMENTED();
}
      
void master_sound_volume(int) {
    UNIMPLEMENTED();
}
