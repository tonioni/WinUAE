#pragma once

class WinUaeQtConfig;
struct uae_prefs;

// Applies a launcher config onto prefs. When fullReset is true (the Start/Reset
// commit path) prefs is first reset to defaults so the config fully defines the
// machine, mirroring win32's prefs_to_gui (default_prefs + copy). When false (the
// hardware-info preview/refresh) the config is applied onto the live prefs as-is.
bool applyWinUaeQtConfigToPrefs(const WinUaeQtConfig &config, struct uae_prefs *prefs, bool fullReset = false);
