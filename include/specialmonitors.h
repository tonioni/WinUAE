
bool emulate_specialmonitors (struct vidbuffer*, struct vidbuffer*);
void specialmonitor_store_fmode(int vpos, int hpos, uae_u16 fmode);
void specialmonitor_reset(void);
bool specialmonitor_need_genlock(void);
addrbank *specialmonitor_autoconfig_init(int devnum);
bool emulate_genlock(struct vidbuffer*, struct vidbuffer*);
