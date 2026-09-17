#pragma once

struct uae_prefs;

void unix_romscan_mark_dirty(void);
void unix_romscan_set_recursive(bool recursive);
void unix_romscan_refresh(struct uae_prefs *prefs, bool force);

/* Resolve a ROM file name saved relative to the ROM search path (a bare name
 * with no directory, as produced by cfgfile_put_multipath) to a full path by
 * looking it up in the configured scan paths. Absolute paths and ":NAME"
 * config-name references are left untouched. Returns true if resolved. */
bool unix_resolve_rom_path(struct uae_prefs *prefs, TCHAR *path, int size);
