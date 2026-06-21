#include "sysconfig.h"
#include "sysdeps.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "options.h"
#include "traps.h"
#include "custom.h"
#include "inputdevice.h"
#include "gui.h"
#include "registry.h"
#include "target.h"
#include "target_main.h"
#include "savestate.h"
#include "sounddep/sound.h"
#include "uae.h"

#ifdef WINUAE_UNIX_WITH_INTEGRATED_QT_UI
#include "qt/launcher_bridge.h"
#endif

unsigned int gui_ledstate;

static int unix_gui_argc;
static TCHAR **unix_gui_argv;
#ifdef WINUAE_UNIX_WITH_INTEGRATED_QT_UI
static TCHAR unix_gui_display_config_path[MAX_DPATH];
#endif

static bool is_runtime_config_snapshot(const TCHAR *path)
{
    const TCHAR *name = _tcsrchr(path, '/');
    name = name ? name + 1 : path;

    const TCHAR prefix[] = _T("winuae-runtime-");
    const TCHAR suffix[] = _T(".uae");
    const size_t len = _tcslen(name);
    const size_t prefix_len = _tcslen(prefix);
    const size_t suffix_len = _tcslen(suffix);

    return len > prefix_len + suffix_len
        && !_tcsncmp(name, prefix, prefix_len)
        && !_tcscmp(name + len - suffix_len, suffix);
}

#ifdef WINUAE_UNIX_WITH_INTEGRATED_QT_UI
static void copy_gui_config_path(TCHAR *out, size_t out_len, const TCHAR *path)
{
    if (!out || out_len == 0) {
        return;
    }
    out[0] = 0;
    if (!path || !path[0]) {
        return;
    }
    _tcsncpy(out, path, out_len);
    out[out_len - 1] = 0;
}

static void remember_gui_display_config_path(const TCHAR *path)
{
    if (!path || !path[0] || is_runtime_config_snapshot(path)) {
        return;
    }
    copy_gui_config_path(
        unix_gui_display_config_path,
        sizeof unix_gui_display_config_path / sizeof unix_gui_display_config_path[0],
        path);
}

static void get_gui_display_config_path(TCHAR *out, size_t out_len, const TCHAR *initial_config)
{
    if (!out || out_len == 0) {
        return;
    }
    out[0] = 0;
    if (unix_gui_display_config_path[0]) {
        copy_gui_config_path(out, out_len, unix_gui_display_config_path);
        return;
    }
    if (initial_config && initial_config[0] && !is_runtime_config_snapshot(initial_config)) {
        copy_gui_config_path(out, out_len, initial_config);
        return;
    }
    winUaeQtLauncherInitialConfigPath(unix_gui_argc, unix_gui_argv, out, out_len);
}
#endif

void target_main_set_args(int argc, TCHAR **argv)
{
    unix_gui_argc = argc;
    unix_gui_argv = argv;
}

int target_main_handle_early(int argc, TCHAR **argv)
{
    return -1;
}

int gui_init(void)
{
#ifdef WINUAE_UNIX_WITH_INTEGRATED_QT_UI
    TCHAR initial_config[MAX_DPATH];
    initial_config[0] = 0;

    /* Command-line configs are resolved by the launcher itself and win
     * over default.uae, like on Windows. Otherwise seed the launcher from the
     * config the core just loaded. */
    if (!winUaeQtLauncherArgumentsSpecifyConfig(unix_gui_argc, unix_gui_argv)) {
        if (optionsfile[0] && access(optionsfile, R_OK) == 0) {
            _tcscpy(initial_config, optionsfile);
        } else {
            fetch_configurationpath(initial_config, sizeof initial_config / sizeof initial_config[0]);
            _tcscat(initial_config, OPTIONSFILENAME);
            if (access(initial_config, R_OK) != 0) {
                initial_config[0] = 0;
            }
        }
    }

    TCHAR display_config[MAX_DPATH];
    TCHAR selected_config[MAX_DPATH];
    get_gui_display_config_path(
        display_config,
        sizeof display_config / sizeof display_config[0],
        initial_config);
    selected_config[0] = 0;

    const int action = runWinUaeQtLauncherForPrefsWithConfigPaths(
        unix_gui_argc,
        unix_gui_argv,
        &changed_prefs,
        initial_config[0] ? initial_config : nullptr,
        display_config[0] ? display_config : nullptr,
        selected_config,
        sizeof selected_config / sizeof selected_config[0],
        0,
        nullptr);
    remember_gui_display_config_path(selected_config[0] ? selected_config : display_config);
    if (initial_config[0] && is_runtime_config_snapshot(initial_config)) {
        unlink(initial_config);
    }
    if (action == WINUAE_QT_LAUNCHER_START) {
        return 1;
    }
    if (action == WINUAE_QT_LAUNCHER_ERROR) {
        return -1;
    }
    return -2;
#else
    return 0;
#endif
}
int gui_update(void) { return 1; }
void gui_exit(void)
{
    registry_flush();
}
void gui_led(int led, int on, int brightness)
{
    if (on >= 0 && led >= 0 && led < int(sizeof(gui_ledstate) * 8)) {
        if (on) {
            gui_ledstate |= 1u << led;
        } else {
            gui_ledstate &= ~(1u << led);
        }
    }
    if (led == LED_POWER && brightness >= 0) {
        gui_data.powerled_brightness = brightness;
    }
}
void gui_filename(int, const TCHAR*) {}
void gui_fps(int fps, int lines, bool lace, int idle, int color)
{
    gui_data.fps = fps;
    gui_data.lines = lines;
    gui_data.lace = lace;
    gui_data.idle = idle;
    gui_data.fps_color = color;
    gui_led(LED_FPS, 1, -1);
    gui_led(LED_LINES, 1, -1);
    gui_led(LED_CPU, 1, -1);
    gui_led(LED_SND, (gui_data.sndbuf_status > 1 || gui_data.sndbuf_status < 0) ? 0 : 1, -1);
}
void gui_lock(void) {}
void gui_unlock(void) {}

static void gui_flicker_led_single(int led, int status)
{
    static int resetcounter[LED_MAX];
    uae_s8 *target = nullptr;

    if (led == LED_HD) {
        target = &gui_data.hd;
    } else if (led == LED_CD) {
        target = &gui_data.cd;
    } else if (led == LED_MD) {
        target = &gui_data.md;
    } else if (led == LED_NET) {
        target = &gui_data.net;
    }
    if (!target) {
        return;
    }

    const uae_s8 old = *target;
    if (status < 0) {
        gui_led(led, old < 0 ? -1 : 0, -1);
        return;
    }
    if (status == 0 && old < 0) {
        *target = 0;
        resetcounter[led] = 0;
        gui_led(led, 0, -1);
        return;
    }
    if (status == 0) {
        resetcounter[led]--;
        if (resetcounter[led] > 0) {
            return;
        }
    }

    *target = status;
    resetcounter[led] = 15;
    if (old != *target) {
        gui_led(led, *target, -1);
    }
}

void gui_flicker_led(int led, int, int status)
{
    if (led < 0) {
        gui_flicker_led_single(LED_HD, 0);
        gui_flicker_led_single(LED_CD, 0);
        if (gui_data.net >= 0) {
            gui_flicker_led_single(LED_NET, 0);
        }
        if (gui_data.md >= 0) {
            gui_flicker_led_single(LED_MD, 0);
        }
    } else {
        gui_flicker_led_single(led, status);
    }
}
void gui_disk_image_change(int, const TCHAR*, bool) {}

#ifdef WINUAE_UNIX_WITH_INTEGRATED_QT_UI
static const TCHAR *runtime_shortcut_initial_path(int shortcut)
{
    if (shortcut >= 0 && shortcut < 4) {
        return changed_prefs.floppyslots[shortcut].df[0]
            ? changed_prefs.floppyslots[shortcut].df
            : currprefs.floppyslots[shortcut].df;
    }
    if (shortcut == 4 || shortcut == 5) {
        if (savestate_fname[0]) {
            return savestate_fname;
        }
        if (changed_prefs.statefile[0]) {
            return changed_prefs.statefile;
        }
        return currprefs.statefile;
    }
    if (shortcut == 6) {
        return changed_prefs.cdslots[0].name[0]
            ? changed_prefs.cdslots[0].name
            : currprefs.cdslots[0].name;
    }
    return "";
}

static bool apply_runtime_shortcut_selection(int shortcut, const TCHAR *path)
{
    if (!path || !path[0]) {
        return false;
    }

    if (shortcut >= 0 && shortcut < 4) {
        _tcsncpy(changed_prefs.floppyslots[shortcut].df, path, MAX_DPATH);
        changed_prefs.floppyslots[shortcut].df[MAX_DPATH - 1] = 0;
        set_config_changed();
        return true;
    }
    if (shortcut == 4) {
        savestate_initsave(path, 1, true, false);
        savestate_state = STATE_DORESTORE;
        return true;
    }
    if (shortcut == 5) {
        savestate_initsave(path, 1, true, true);
        save_state(savestate_fname, STATE_SAVE_DESCRIPTION);
        return true;
    }
    if (shortcut == 6) {
        _tcsncpy(changed_prefs.cdslots[0].name, path, MAX_DPATH);
        changed_prefs.cdslots[0].name[MAX_DPATH - 1] = 0;
        changed_prefs.cdslots[0].inuse = true;
        set_config_changed();
        return true;
    }
    return false;
}
#endif

void gui_display(int shortcut)
{
#ifdef WINUAE_UNIX_WITH_INTEGRATED_QT_UI
    static bool active;
    if (active) {
        return;
    }
    if (shortcut != -1 && (shortcut < 0 || shortcut > 6)) {
        write_log("Unix Qt runtime UI: shortcut %d is not implemented yet\n", shortcut);
        return;
    }

    active = true;

    const int old_pause = pause_emulation;
    pause_emulation = 1;
    setsystime();
    inputdevice_unacquire();
    pause_sound();

    if (shortcut == -1) {
        TCHAR display_config[MAX_DPATH];
        TCHAR selected_config[MAX_DPATH];
        get_gui_display_config_path(
            display_config,
            sizeof display_config / sizeof display_config[0],
            nullptr);
        selected_config[0] = 0;

        int exit_code = 0;
        const int action = runWinUaeQtLauncherForPrefsWithConfigSnapshot(
            unix_gui_argc,
            unix_gui_argv,
            &changed_prefs,
            display_config[0] ? display_config : nullptr,
            selected_config,
            sizeof selected_config / sizeof selected_config[0],
            1,
            &exit_code);
        if (action == WINUAE_QT_LAUNCHER_START
            || action == WINUAE_QT_LAUNCHER_RESET
            || action == WINUAE_QT_LAUNCHER_RESTART) {
            remember_gui_display_config_path(selected_config[0] ? selected_config : display_config);
        }

        if (action == WINUAE_QT_LAUNCHER_START || action == WINUAE_QT_LAUNCHER_RESET) {
            fixup_prefs(&changed_prefs, true);
            reset_sound();
            inputdevice_copyconfig(&changed_prefs, &currprefs);
            inputdevice_config_change_test();
            set_config_changed();
            if (action == WINUAE_QT_LAUNCHER_RESET) {
                uae_reset(1, 1);
            }
        } else if (action == WINUAE_QT_LAUNCHER_QUIT) {
            uae_quit();
        } else if (action == WINUAE_QT_LAUNCHER_RESTART) {
            fixup_prefs(&changed_prefs, true);
            copy_prefs(&changed_prefs, &currprefs);
            uae_restart(&changed_prefs, -1, nullptr);
        } else if (action == WINUAE_QT_LAUNCHER_ERROR) {
            write_log("Unix Qt runtime UI exited with error code %d\n", exit_code);
        }
    } else {
        TCHAR selected_path[MAX_DPATH];
        selected_path[0] = 0;
        int exit_code = 0;
        const int action = runWinUaeQtRuntimeFileDialog(
            unix_gui_argc,
            unix_gui_argv,
            shortcut,
            runtime_shortcut_initial_path(shortcut),
            selected_path,
            sizeof selected_path / sizeof selected_path[0],
            &exit_code);
        if (action == WINUAE_QT_LAUNCHER_START) {
            apply_runtime_shortcut_selection(shortcut, selected_path);
        } else if (action == WINUAE_QT_LAUNCHER_ERROR) {
            write_log("Unix Qt runtime file dialog exited with error code %d\n", exit_code);
        }
    }

    pause_emulation = old_pause;
    setsystime();
    resume_sound();
    inputdevice_acquire(TRUE);
    fpscounter_reset();

    active = false;
#else
    write_log("Unix Qt runtime UI is not enabled in this build\n");
#endif
}
void gui_gameport_button_change(int, int, int) {}
void gui_gameport_axis_change(int, int, int, int) {}
void notify_user(int msg)
{
    switch (msg) {
        case NUMSG_MODRIP_NOTFOUND:
            write_log("No music modules or packed data found.\n");
            break;
        case NUMSG_MODRIP_FINISHED:
            write_log("Module ripper scan finished.\n");
            break;
        default:
            write_log("notify_user: %d\n", msg);
            break;
    }
}
void notify_user_parms(int msg, const TCHAR*, ...) { write_log("notify_user: %d\n", msg); }
int translate_message(int msg, TCHAR *out)
{
    if (!out) {
        return 0;
    }
    switch (msg) {
        case NUMSG_MODRIP_SAVE:
            _tcscpy(out, _T("Module/packed data found\n%s\nStart address %08.8X, Size %d bytes\n'%s'\nWould you like to save it?"));
            return 1;
        default:
            out[0] = 0;
            return 0;
    }
}
void gui_message(const TCHAR *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    fputc('\n', stderr);
    va_end(ap);
}

int gui_message_multibutton(int flags, const TCHAR *format, ...)
{
    TCHAR msg[2048];
    va_list ap;
    va_start(ap, format);
    _vsntprintf(msg, sizeof msg / sizeof(TCHAR), format, ap);
    msg[(sizeof msg / sizeof(TCHAR)) - 1] = 0;
    va_end(ap);

    const size_t msg_len = _tcslen(msg);
    write_log("%s", msg);
    if (msg_len == 0 || msg[msg_len - 1] != '\n') {
        write_log("\n");
    }

#ifdef WINUAE_UNIX_WITH_INTEGRATED_QT_UI
    int exit_code = 0;
    const int ret = runWinUaeQtMessageBox(unix_gui_argc, unix_gui_argv, flags, msg, &exit_code);
    if (exit_code == 0) {
        return ret;
    }
#endif

    return flags == 0 ? 0 : 1;
}
