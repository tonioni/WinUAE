#ifndef UAE_IDECONTROLLERS_H
#define UAE_IDECONTROLLERS_H

#include "uae/types.h"

// Other IDE controllers

void idecontroller_free(void);
void idecontroller_reset(void);
void idecontroller_rethink(void);
void idecontroller_hsync(void);

void gvp_add_ide_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);
addrbank *gvp_ide_rom_autoconfig_init(struct romconfig*);
addrbank *gvp_ide_controller_autoconfig_init(struct romconfig*);

void alf_add_ide_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);
addrbank *alf_init(struct romconfig*);

void apollo_add_ide_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);
addrbank *apollo_init_hd(struct romconfig*);
addrbank *apollo_init_cpu(struct romconfig*);

void masoboshi_add_idescsi_unit (int ch, struct uaedev_config_info *ci, struct romconfig *rc);
addrbank *masoboshi_init(struct romconfig*);

void adide_add_ide_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);
addrbank *adide_init(struct romconfig *rc);

void mtec_add_ide_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);
addrbank *mtec_init(struct romconfig *rc);

addrbank *rochard_init(struct romconfig *rc);
void rochard_add_idescsi_unit(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

uae_u32 REGPARAM3 apollo_ide_lget (uaecptr addr) REGPARAM;
uae_u32 REGPARAM3 apollo_ide_wget (uaecptr addr) REGPARAM;
uae_u32 REGPARAM3 apollo_ide_bget (uaecptr addr) REGPARAM;
void REGPARAM3 apollo_ide_lput (uaecptr addr, uae_u32 l) REGPARAM;
void REGPARAM3 apollo_ide_wput (uaecptr addr, uae_u32 w) REGPARAM;
void REGPARAM3 apollo_ide_bput (uaecptr addr, uae_u32 b) REGPARAM;
extern const uae_u8 apollo_autoconfig[16];
extern const uae_u8 apollo_autoconfig_060[16];

void x86_ide_hd_put(int portnum, uae_u16 v, int);
uae_u16 x86_ide_hd_get(int portnum, int);
addrbank *x86_at_hd_init_1(struct romconfig *rc);
void x86_add_at_hd_unit_1(int ch, struct uaedev_config_info *ci, struct romconfig *rc);
addrbank *x86_at_hd_init_2(struct romconfig *rc);
void x86_add_at_hd_unit_2(int ch, struct uaedev_config_info *ci, struct romconfig *rc);
addrbank *x86_at_hd_init_xt(struct romconfig *rc);
void x86_add_at_hd_unit_xt(int ch, struct uaedev_config_info *ci, struct romconfig *rc);

#endif /* UAE_IDECONTROLLERS_H */
