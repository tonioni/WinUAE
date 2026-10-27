// No core dependencies, so the Qt launcher and its tests can link this too.

#include "tchar.h"
#include "target.h"

// win32.* settings that mean the same on Unix. Paths and host device
// names or indexes (sound cards, MIDI, serial and parallel ports) are
// left out because a Windows value would select the wrong device here.
static const TCHAR *compat_options[] = {
    _T("active_input"), _T("active_not_captured_nosound"), _T("active_not_captured_pause"), _T("active_priority"),
    _T("inactive_input"), _T("inactive_nosound"), _T("inactive_pause"), _T("inactive_priority"),
    _T("iconified_input"), _T("iconified_nosound"), _T("iconified_pause"), _T("iconified_priority"),
    _T("middle_mouse"),
    _T("recording_width"), _T("recording_height"), _T("recording_x"), _T("recording_y"),
    _T("screenshot_width"), _T("screenshot_height"), _T("screenshot_x"), _T("screenshot_y"),
    _T("screenshot_min_width"), _T("screenshot_min_height"), _T("screenshot_max_width"), _T("screenshot_max_height"),
    _T("screenshot_mult_width"), _T("screenshot_mult_height"),
    _T("screenshot_output_width_limit"), _T("screenshot_output_height_limit"),
    _T("rtg_vblank"),
    _T("uaescsimode"),
    NULL
};

bool unix_config_is_compat_option(const TCHAR *option)
{
    for (int i = 0; compat_options[i]; i++) {
        if (!strcasecmp(option, compat_options[i])) {
            return true;
        }
    }
    return false;
}
