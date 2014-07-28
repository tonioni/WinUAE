

extern void ncr9x_init(void);
extern void ncr9x_free(void);
extern void ncr9x_reset(void);
extern void ncr9x_rethink(void);

extern int cpuboard_ncr9x_add_scsi_unit(int ch, struct uaedev_config_info *ci);

extern void cpuboard_ncr9x_scsi_put(uaecptr, uae_u32);
extern uae_u32 cpuboard_ncr9x_scsi_get(uaecptr);

#define BLIZZARD_2060_SCSI_OFFSET 0x1ff00
#define BLIZZARD_2060_DMA_OFFSET 0x1fff0
#define BLIZZARD_2060_LED_OFFSET 0x1ffe0

#define BLIZZARD_SCSI_KIT_SCSI_OFFSET 0x8000
#define BLIZZARD_SCSI_KIT_DMA_OFFSET 0x10000

#define CYBERSTORM_MK2_SCSI_OFFSET 0x1ff03
#define CYBERSTORM_MK2_LED_OFFSET 0x1ff43
#define CYBERSTORM_MK2_DMA_OFFSET 0x1ff83

#define CYBERSTORM_MK1_SCSI_OFFSET 0xf400
#define CYBERSTORM_MK1_LED_OFFSET 0xf4e0
#define CYBERSTORM_MK1_DMA_OFFSET 0xf800
#define CYBERSTORM_MK1_JUMPER_OFFSET 0xfc02

