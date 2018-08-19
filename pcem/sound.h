
#define MAXSOUNDBUFLEN (8192)
extern int sound_pos_global;
void sound_add_handler(void(*get_buffer)(int32_t *buffer, int len, void *p), void *p);
void sound_reset();
void sound_set_cd_volume(unsigned int vol_l, unsigned int vol_r);