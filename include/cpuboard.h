#ifndef UAE_CPUBOARD_H
#define UAE_CPUBOARD_H

#include "uae/types.h"

extern addrbank *cpuboard_autoconfig_init(struct romconfig*);
extern bool cpuboard_maprom(void);
extern void cpuboard_map(void);
extern void cpuboard_reset(void);
extern void cpuboard_cleanup(void);
extern void cpuboard_init(void);
extern void cpuboard_clear(void);
extern void cpuboard_vsync(void);
extern void cpuboard_hsync(void);
extern void cpuboard_rethink(void);
extern int cpuboard_memorytype(struct uae_prefs *p);
extern int cpuboard_maxmemory(struct uae_prefs *p);
extern bool cpuboard_32bit(struct uae_prefs *p);
extern bool cpuboard_jitdirectompatible(struct uae_prefs *p);
extern bool is_ppc_cpu(struct uae_prefs *);
extern bool cpuboard_io_special(int addr, uae_u32 *val, int size, bool write);
extern void cpuboard_overlay_override(void);
extern void cpuboard_setboard(struct uae_prefs *p, int type, int subtype);
extern uaecptr cpuboard_get_reset_pc(uaecptr *stack);
extern void cpuboard_set_flash_unlocked(bool unlocked);

extern bool ppc_interrupt(int new_m68k_ipl);

extern void cyberstorm_scsi_ram_put(uaecptr addr, uae_u32);
extern uae_u32 cyberstorm_scsi_ram_get(uaecptr addr);
extern int REGPARAM3 cyberstorm_scsi_ram_check(uaecptr addr, uae_u32 size) REGPARAM;
extern uae_u8 *REGPARAM3 cyberstorm_scsi_ram_xlate(uaecptr addr) REGPARAM;

void cyberstorm_irq(int level);
void cyberstorm_mk3_ppc_irq(int level);
void blizzardppc_irq(int level);

#define BOARD_MEMORY_Z2 1
#define BOARD_MEMORY_Z3 2
#define BOARD_MEMORY_HIGHMEM 3
#define BOARD_MEMORY_BLIZZARD_12xx 4
#define BOARD_MEMORY_BLIZZARD_PPC 5
#define BOARD_MEMORY_25BITMEM 6
#define BOARD_MEMORY_EMATRIX 7

#define ISCPUBOARD(type,subtype) (cpuboards[currprefs.cpuboard_type].id == type && (type < 0 || currprefs.cpuboard_subtype == subtype))

#define BOARD_ACT 1
#define BOARD_ACT_SUB_APOLLO 0

#define BOARD_COMMODORE 2
#define BOARD_COMMODORE_SUB_A26x0 0

#define BOARD_DKB 3
#define BOARD_DKB_SUB_12x0 0
#define BOARD_DKB_SUB_WILDFIRE 1

#define BOARD_GVP 4
#define BOARD_GVP_SUB_A3001SI 0
#define BOARD_GVP_SUB_A3001SII 1
#define BOARD_GVP_SUB_A530 2
#define BOARD_GVP_SUB_GFORCE030 3
#define BOARD_GVP_SUB_TEKMAGIC 4

#define BOARD_KUPKE 5

#define BOARD_MACROSYSTEM 6
#define BOARD_MACROSYSTEM_SUB_WARPENGINE_A4000 0

#define BOARD_MTEC 7
#define BOARD_MTEC_SUB_EMATRIX530 0

#define BOARD_BLIZZARD 8
#define BOARD_BLIZZARD_SUB_1230IV 0
#define BOARD_BLIZZARD_SUB_1260 1
#define BOARD_BLIZZARD_SUB_2060 2
#define BOARD_BLIZZARD_SUB_PPC 3

#define BOARD_CYBERSTORM 9
#define BOARD_CYBERSTORM_SUB_MK1 0
#define BOARD_CYBERSTORM_SUB_MK2 1
#define BOARD_CYBERSTORM_SUB_MK3 2
#define BOARD_CYBERSTORM_SUB_PPC 3

#define BOARD_RCS 10
#define BOARD_RCS_SUB_FUSIONFORTY 0

#define BOARD_IC 11
#define BOARD_IC_ACA500 0

#endif /* UAE_CPUBOARD_H */
