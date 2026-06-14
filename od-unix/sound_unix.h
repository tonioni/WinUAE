#ifndef WINUAE_OD_UNIX_SOUND_UNIX_H
#define WINUAE_OD_UNIX_SOUND_UNIX_H

#include "sysdeps.h"

int unix_sound_device_count(void);
const TCHAR *unix_sound_device_name(int index);
const TCHAR *unix_sound_device_config_name(int index);
int unix_sound_device_index_from_config_name(const TCHAR *name);
int unix_sampler_device_count(void);
const TCHAR *unix_sampler_device_name(int index);
const TCHAR *unix_sampler_device_config_name(int index);
int unix_sampler_device_index_from_config_name(const TCHAR *name);

#endif /* WINUAE_OD_UNIX_SOUND_UNIX_H */
