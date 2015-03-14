
void ncr710_io_bput_a4000t(uaecptr, uae_u32);
uae_u32 ncr710_io_bget_a4000t(uaecptr);

extern addrbank ncr_bank_cyberstorm;
extern addrbank ncr_bank_generic;

extern void ncr_init(void);
extern void ncr_free(void);
extern void ncr_reset(void);
extern void ncr_rethink(void);

extern addrbank *ncr710_a4091_autoconfig_init(struct romconfig*);
extern addrbank *ncr710_warpengine_autoconfig_init(struct romconfig*);

void cpuboard_ncr710_io_bput(uaecptr addr, uae_u32 v);
uae_u32 cpuboard_ncr710_io_bget(uaecptr addr);

extern void a4000t_add_scsi_unit (int ch, struct uaedev_config_info *ci, struct romconfig *rc);
extern void warpengine_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);
extern void tekmagic_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);
extern void cyberstorm_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);
extern void blizzardppc_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);
extern void a4091_add_scsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

