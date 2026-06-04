#pragma once

struct uae_prefs;

void unix_romscan_mark_dirty(void);
void unix_romscan_set_recursive(bool recursive);
void unix_romscan_refresh(struct uae_prefs *prefs, bool force);
