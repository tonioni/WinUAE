// Other IDE controllers

void idecontroller_free_units(void);
void idecontroller_free(void);
void idecontroller_reset(void);
void idecontroller_rethink(void);
void idecontroller_hsync(void);

int gvp_add_ide_unit(int ch, struct uaedev_config_info *ci);
addrbank *gvp_ide_rom_autoconfig_init(void);
addrbank *gvp_ide_controller_autoconfig_init(void);
