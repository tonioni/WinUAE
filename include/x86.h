#ifndef UAE_X86_H
#define UAE_X86_H

bool a1060_init(struct autoconfig_info *aci);
bool a2088xt_init(struct autoconfig_info *aci);
bool a2088t_init(struct autoconfig_info *aci);
bool a2286_init(struct autoconfig_info *aci);
bool a2386_init(struct autoconfig_info *aci);
bool isa_expansion_init(struct autoconfig_info *aci);
void x86_bridge_hsync(void);
void x86_bridge_vsync(void);
void x86_bridge_reset(void);
void x86_bridge_free(void);
void x86_bridge_rethink(void);
void x86_bridge_sync_change(void);

#define X86_STATE_INACTIVE 0
#define X86_STATE_STOP 1
#define X86_STATE_ACTIVE 2

int is_x86_cpu(struct uae_prefs*);
void x86_bridge_execute_until(int until);
extern bool x86_turbo_on;

#endif /* UAE_X86_H */
