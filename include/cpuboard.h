
extern addrbank *cpuboard_autoconfig_init(void);
extern bool cpuboard_maprom(void);
extern void cpuboard_map(void);
extern void cpuboard_reset(bool);
extern void cpuboard_cleanup(void);
extern void cpuboard_init(void);
extern void cpuboard_clear(void);
extern void cpuboard_vsync(void);
extern void cpuboard_rethink(void);
extern bool cpuboard_08000000(struct uae_prefs *p);

extern void cyberstorm_scsi_ram_put(uaecptr addr, uae_u32);
extern uae_u32 cyberstorm_scsi_ram_get(uaecptr addr);
extern int REGPARAM3 cyberstorm_scsi_ram_check(uaecptr addr, uae_u32 size) REGPARAM;
extern uae_u8 *REGPARAM3 cyberstorm_scsi_ram_xlate(uaecptr addr) REGPARAM;

#define BOARD_BLIZZARD_1230_IV 1
#define BOARD_BLIZZARD_1230_IV_SCSI 2
#define BOARD_BLIZZARD_1260 3
#define BOARD_BLIZZARD_1260_SCSI 4
#define BOARD_BLIZZARD_2060 5
#define BOARD_CSMK1 6
#define BOARD_CSMK2 7
#define BOARD_CSMK3 8
#define BOARD_CSPPC 9
#define BOARD_BLIZZARDPPC 10
#define BOARD_WARPENGINE_A4000 11


