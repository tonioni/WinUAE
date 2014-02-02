
void ncr_io_bput(uaecptr, uae_u32);
uae_u32 ncr_io_bget(uaecptr);

extern void ncr_init(void);
extern void ncr_autoconfig_init(void);
extern void ncr_free(void);
extern void ncr_reset(void);

extern int a4000t_add_scsi_unit (int ch, struct uaedev_config_info *ci);
extern int a4091_add_scsi_unit (int ch, struct uaedev_config_info *ci);
