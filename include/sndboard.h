#ifndef UAE_SNDBOARD_H
#define UAE_SNDBOARD_H

#include "uae/types.h"

bool toccata_init(struct autoconfig_info *aci);
bool prelude_init(struct autoconfig_info *aci);
bool prelude1200_init(struct autoconfig_info *aci);
void sndboard_free(void);
void sndboard_hsync(void);
void sndboard_vsync(void);
void sndboard_rethink(void);
void update_sndboard_sound(double);
void sndboard_reset(void);
void sndboard_ext_volume(void);

bool uaesndboard_init_z2(struct autoconfig_info *aci);
bool uaesndboard_init_z3(struct autoconfig_info *aci);
void uaesndboard_free(void);
void uaesndboard_reset(void);

bool pmx_init(struct autoconfig_info *aci);
void pmx_free(void);
void pmx_reset(void);

#endif /* UAE_SNDBOARD_H */
