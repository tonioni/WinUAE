#pragma once

#include <stddef.h>

struct uae_prefs;

enum {
    WINUAE_QT_LAUNCHER_EXIT = 1,
    WINUAE_QT_LAUNCHER_START = 2,
    WINUAE_QT_LAUNCHER_ERROR = 3,
    /* Runtime (F12) GUI results, matching the Windows GUI buttons. */
    WINUAE_QT_LAUNCHER_QUIT = 4,
    WINUAE_QT_LAUNCHER_RESTART = 5,
    /* Apply the edited config like START, then hard-reset the Amiga. */
    WINUAE_QT_LAUNCHER_RESET = 6
};

int winUaeQtLauncherArgumentsSpecifyConfig(int argc, char **argv);
int winUaeQtLauncherInitialConfigPath(int argc, char **argv, char *path, size_t path_len);
int runWinUaeQtLauncherForPrefs(int argc, char **argv, struct uae_prefs *prefs, int *exit_code);
int runWinUaeQtLauncherForPrefsWithConfig(int argc, char **argv, struct uae_prefs *prefs, const char *initial_config_path, int runtime_actions, int *exit_code);
int runWinUaeQtLauncherForPrefsWithConfigPaths(int argc, char **argv, struct uae_prefs *prefs, const char *initial_config_path, const char *display_config_path, char *selected_config_path, size_t selected_config_path_len, int runtime_actions, int *exit_code);
int runWinUaeQtLauncherForPrefsWithConfigSnapshot(int argc, char **argv, struct uae_prefs *prefs, const char *display_config_path, char *selected_config_path, size_t selected_config_path_len, int runtime_actions, int *exit_code);
int runWinUaeQtRuntimeFileDialog(int argc, char **argv, int shortcut, const char *initial_path, char *selected_path, size_t selected_path_len, int *exit_code);
int runWinUaeQtMessageBox(int argc, char **argv, int flags, const char *message, int *exit_code);
