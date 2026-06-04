#pragma once

class WinUaeQtConfig;
struct uae_prefs;

bool applyWinUaeQtConfigToPrefs(const WinUaeQtConfig &config, struct uae_prefs *prefs);
