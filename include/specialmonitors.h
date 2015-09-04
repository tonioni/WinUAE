#ifndef UAE_SPECIALMONITORS_H
#define UAE_SPECIALMONITORS_H

#include "memory.h"

bool emulate_specialmonitors(struct vidbuffer *src, struct vidbuffer *dst);
void specialmonitor_store_fmode(int vpos, int hpos, uae_u16 fmode);
void specialmonitor_reset(void);
bool specialmonitor_need_genlock(void);
addrbank *specialmonitor_autoconfig_init(int devnum);
bool emulate_genlock(struct vidbuffer*, struct vidbuffer*);

#endif /* UAE_SPECIALMONITORS_H */
