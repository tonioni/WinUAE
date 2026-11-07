// Optional host hooks. Targets define TARGET_HAS_* in target.h when they
// supply an implementation; otherwise all builds use these defaults.
#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "traps.h"
#include "memory.h"
#include "audio.h"
#include "autoconf.h"
#include "debug.h"
#include "filesys.h"
#include "fsdb.h"
#include "inputdevice.h"
#include "rommgr.h"
#include "statusline.h"
#include "uae.h"
#include "xwin.h"

#ifndef TARGET_HAS_MACHDEP_FREE
void machdep_free(void) {}
#endif

#ifndef TARGET_HAS_AUDIO_PULL
int audio_is_pull(void) { return 0; }
bool audio_is_pull_event(void) { return false; }
int audio_pull_buffer(void) { return 0; }
bool audio_finish_pull(void) { return false; }
#endif

#ifndef TARGET_HAS_STATUSLINE_UPDATED
void statusline_updated(int) {}
#endif

#ifndef TARGET_HAS_CUSTOM_FSDB
a_inode *custom_fsdb_lookup_aino_aname(a_inode *, const TCHAR *) { return NULL; }
a_inode *custom_fsdb_lookup_aino_nname(a_inode *, const TCHAR *) { return NULL; }
int custom_fsdb_used_as_nname(a_inode *, const TCHAR *) { return 0; }
#endif

#ifndef TARGET_HAS_EXTERNAL_FILESYSTEM
void filesys_addexternals(void) {}
int target_get_volume_name(struct uaedev_mount_info *, struct uaedev_config_info *, bool, bool, int) { return 0; }
#endif

#ifndef TARGET_HAS_KEYFILE
uae_u8 *target_load_keyfile(struct uae_prefs *, const TCHAR *, int *size, TCHAR *)
{
	if (size) {
		*size = 0;
	}
	return NULL;
}
#endif

#ifndef TARGET_HAS_CPU_RATE
uae_u32 emulib_target_getcpurate(uae_u32, uae_u32 *low)
{
	if (low) {
		*low = 0;
	}
	return 0;
}
#endif

#ifndef TARGET_HAS_TOUCH_LIGHTPEN
int is_touch_lightpen(void) { return 0; }
#endif

#ifndef TARGET_HAS_DEBUG_INFO
void update_debug_info(void) {}
#endif
