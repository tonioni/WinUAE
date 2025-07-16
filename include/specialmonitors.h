#ifndef UAE_SPECIALMONITORS_H
#define UAE_SPECIALMONITORS_H

#include "memory.h"

bool emulate_specialmonitors(struct vidbuffer *src, struct vidbuffer *dst);
void specialmonitor_store_fmode(int vpos, int hpos, uae_u16 fmode);
void specialmonitor_reset(void);
bool specialmonitor_need_genlock(void);
bool specialmonitor_uses_control_lines(void);
bool specialmonitor_autoconfig_init(struct autoconfig_info*);
bool emulate_genlock(struct vidbuffer*, struct vidbuffer*, bool);
bool emulate_grayscale(struct vidbuffer*, struct vidbuffer*);
bool specialmonitor_linebased(void);
void genlock_infotext(uae_u8*, struct vidbuffer*);

extern const TCHAR *specialmonitorfriendlynames[];
extern const TCHAR *specialmonitormanufacturernames[];
extern const TCHAR *specialmonitorconfignames[];

#endif /* UAE_SPECIALMONITORS_H */
