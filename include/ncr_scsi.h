
void ncr710_io_bput_a4000t(uaecptr, uae_u32);
uae_u32 ncr710_io_bget_a4000t(uaecptr);

extern addrbank ncr_bank_cyberstorm;
extern addrbank ncr_bank_blizzardppc;

extern void ncr_init(void);
extern void ncr_free(void);
extern void ncr_reset(void);

extern void ncr710_init(void);
extern addrbank *ncr710_a4091_autoconfig_init(int devnum);
extern addrbank *ncr710_warpengine_autoconfig_init(void);
extern void ncr710_free(void);
extern void ncr710_reset(void);
extern void ncr_rethink(void);

extern int a4000t_add_scsi_unit (int ch, struct uaedev_config_info *ci);
extern int warpengine_add_scsi_unit(int ch, struct uaedev_config_info *ci);
extern int cyberstorm_add_scsi_unit(int ch, struct uaedev_config_info *ci);
extern int blizzardppc_add_scsi_unit(int ch, struct uaedev_config_info *ci);
extern int a4091_add_scsi_unit(int ch, struct uaedev_config_info *ci, int devnum);
