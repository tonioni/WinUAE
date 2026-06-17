#pragma once

#include <stddef.h>

struct uae_prefs;

enum {
    WINUAE_QT_LAUNCHER_EXIT = 1,
    WINUAE_QT_LAUNCHER_START = 2,
    WINUAE_QT_LAUNCHER_ERROR = 3
};

int runWinUaeQtLauncherForPrefs(int argc, char **argv, struct uae_prefs *prefs, int *exit_code);
int runWinUaeQtLauncherForPrefsWithConfig(int argc, char **argv, struct uae_prefs *prefs, const char *initial_config_path, int *exit_code);
int runWinUaeQtRuntimeFileDialog(int argc, char **argv, int shortcut, const char *initial_path, char *selected_path, size_t selected_path_len, int *exit_code);
int runWinUaeQtMessageBox(int argc, char **argv, int flags, const char *message, int *exit_code);
