// Other IDE controllers

void idecontroller_free_units(void);
void idecontroller_free(void);
void idecontroller_reset(void);
void idecontroller_rethink(void);
void idecontroller_hsync(void);

int gvp_add_ide_unit(int ch, struct uaedev_config_info *ci);
addrbank *gvp_ide_rom_autoconfig_init(int devnum);
addrbank *gvp_ide_controller_autoconfig_init(int devnum);

int alf_add_ide_unit(int ch, struct uaedev_config_info *ci);
addrbank *alf_init(int devnum);

int apollo_add_ide_unit(int ch, struct uaedev_config_info *ci);
addrbank *apollo_init(int devnum);
addrbank *apollo_init_cpu(int devnum);

int masoboshi_add_ide_unit(int ch, struct uaedev_config_info *ci);
addrbank *masoboshi_init(int devnum);

uae_u32 REGPARAM3 apollo_ide_lget (uaecptr addr) REGPARAM;
uae_u32 REGPARAM3 apollo_ide_wget (uaecptr addr) REGPARAM;
uae_u32 REGPARAM3 apollo_ide_bget (uaecptr addr) REGPARAM;
void REGPARAM3 apollo_ide_lput (uaecptr addr, uae_u32 l) REGPARAM;
void REGPARAM3 apollo_ide_wput (uaecptr addr, uae_u32 w) REGPARAM;
void REGPARAM3 apollo_ide_bput (uaecptr addr, uae_u32 b) REGPARAM;
extern const uae_u8 apollo_autoconfig[16];
extern const uae_u8 apollo_autoconfig_060[16];
