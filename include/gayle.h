#ifndef UAE_GAYLE_H
#define UAE_GAYLE_H

#include "uae/types.h"

extern void gayle_reset (int);
extern void gayle_hsync (void);
extern void gayle_free (void);
extern int gayle_add_ide_unit (int ch, struct uaedev_config_info *ci);
extern int gayle_modify_pcmcia_sram_unit (struct uaedev_config_info*, int insert);
extern int gayle_modify_pcmcia_ide_unit (struct uaedev_config_info*, int insert);
extern int gayle_add_pcmcia_sram_unit (struct uaedev_config_info*);
extern int gayle_add_pcmcia_ide_unit(struct uaedev_config_info*);
extern int gayle_ne2000_unit(void);
extern void gayle_free_units (void);
extern void rethink_gayle (void);
extern void gayle_map_pcmcia (void);
extern void check_prefs_changed_gayle(void);

extern int gary_toenb; // non-existing memory access = bus error.
extern int gary_timeout; // non-existing memory access = delay

#define PCMCIA_COMMON_START 0x600000
#define PCMCIA_COMMON_SIZE 0x400000

extern void gayle_dataflyer_enable(bool);

#endif /* UAE_GAYLE_H */
