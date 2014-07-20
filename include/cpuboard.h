
extern addrbank *cpuboard_autoconfig_init(void);
extern bool cpuboard_maprom(void);
extern void cpuboard_map(void);
extern void cpuboard_reset(bool);
extern void cpuboard_cleanup(void);
extern void cpuboard_init(void);
extern void cpuboard_clear(void);
extern void cpuboard_vsync(void);

extern addrbank blizzardram_bank;

#define BOARD_BLIZZARD_1230_IV 1
#define BOARD_BLIZZARD_1260 2
#define BOARD_BLIZZARD_2060 3
#define BOARD_WARPENGINE_A4000 4
