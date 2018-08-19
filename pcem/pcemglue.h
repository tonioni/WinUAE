
extern void pcem_close(void);

uint8_t keyboard_at_read(uint16_t port, void *priv);
uint8_t mem_read_romext(uint32_t addr, void *priv);
uint16_t mem_read_romextw(uint32_t addr, void *priv);
uint32_t mem_read_romextl(uint32_t addr, void *priv);

void sound_speed_changed(void);
extern int SOUNDBUFLEN;
extern int32_t *x86_sndbuffer[2];
extern bool x86_sndbuffer_filled[2];

