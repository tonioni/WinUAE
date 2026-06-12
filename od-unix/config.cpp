#include "sysconfig.h"
#include "sysdeps.h"

#include <ctype.h>
#include <cstdlib>
#include <string>
#include <unistd.h>

#include "options.h"
#ifdef AVIOUTPUT
#include "avioutput.h"
extern int video_recording_active;
static int unix_avi_audio_codec = AVIAUDIO_AVI;
#endif
#include "path_expand.h"
#include "registry.h"
#include "romscan.h"
#include "savestate.h"
#include "sound_unix.h"
#include "uaeserial_unix.h"
#include "winuae_builddate.h"
#ifdef WITH_MIDI
#include "midi.h"
#endif
#include "uae/string.h"
#include "uae.h"
#include "zfile.h"

TCHAR start_path_data[MAX_DPATH];
TCHAR start_path_data_exe[MAX_DPATH];
TCHAR start_path_plugins[MAX_DPATH];
int saveimageoriginalpath;

static TCHAR path_configuration[MAX_DPATH];
static TCHAR path_nvram[MAX_DPATH];
static TCHAR path_screenshot[MAX_DPATH];
static TCHAR path_video[MAX_DPATH];
static TCHAR path_saveimage[MAX_DPATH];
static TCHAR path_ripper[MAX_DPATH];
static TCHAR path_data[MAX_DPATH];
static TCHAR path_rom[MAX_DPATH];
static TCHAR uaeserial_ports[UNIX_UAESERIAL_MAX_UNITS][256];

static std::string trim_copy(const std::string &s)
{
    size_t first = 0;
    while (first < s.size() && isspace((unsigned char)s[first])) {
        first++;
    }
    size_t last = s.size();
    while (last > first && isspace((unsigned char)s[last - 1])) {
        last--;
    }
    return s.substr(first, last - first);
}

static bool parse_path_option(const TCHAR *option, const TCHAR *value, const TCHAR *name, TCHAR *out, int out_size)
{
    if (_tcsicmp(option, name)) {
        return false;
    }
    if (!value || !value[0]) {
        out[0] = 0;
        return true;
    }
    target_expand_environment(value, out, out_size);
    fixtrailing(out);
    return true;
}

static bool parse_rom_path_option(struct uae_prefs *p, const TCHAR *option, const TCHAR *value)
{
    if (!parse_path_option(option, value, _T("rom_path"), path_rom, sizeof path_rom / sizeof(TCHAR))) {
        return false;
    }
    if (!p) {
        return true;
    }
    if (!path_rom[0]) {
        p->path_rom.path[0][0] = 0;
        return true;
    }
    for (int i = 0; i < MAX_PATHS; i++) {
        if (p->path_rom.path[i][0] == 0 || (i == 0 && (!_tcscmp(p->path_rom.path[i], _T(".\\")) || !_tcscmp(p->path_rom.path[i], _T("./"))))) {
            uae_tcslcpy(p->path_rom.path[i], path_rom, sizeof p->path_rom.path[i] / sizeof(TCHAR));
            target_multipath_modified(p);
            return true;
        }
    }
    uae_tcslcpy(p->path_rom.path[MAX_PATHS - 1], path_rom, sizeof p->path_rom.path[MAX_PATHS - 1] / sizeof(TCHAR));
    target_multipath_modified(p);
    return true;
}

static std::string lowercase_copy(std::string s)
{
    for (char &c : s) {
        c = (char)tolower((unsigned char)c);
    }
    return s;
}

static bool file_exists(const std::string &path)
{
    return !path.empty() && access(path.c_str(), F_OK) == 0;
}

static int parse_bool_value(const std::string &value)
{
    std::string v = lowercase_copy(trim_copy(value));
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

static bool parse_int_value(const TCHAR *value, int *out)
{
    if (!value || !out) {
        return false;
    }
    std::string text = trim_copy(value);
    if (text.empty()) {
        return false;
    }
    char *end = NULL;
    long parsed = strtol(text.c_str(), &end, 0);
    if (!end || *end != 0) {
        return false;
    }
    *out = (int)parsed;
    return true;
}

const TCHAR *unix_uaeserial_get_port(int unit)
{
    if (unit < 0 || unit >= UNIX_UAESERIAL_MAX_UNITS) {
        return _T("");
    }
    return uaeserial_ports[unit];
}

void unix_uaeserial_set_port(int unit, const TCHAR *port)
{
    if (unit < 0 || unit >= UNIX_UAESERIAL_MAX_UNITS) {
        return;
    }
    uae_tcslcpy(uaeserial_ports[unit], port ? port : _T(""), sizeof uaeserial_ports[unit] / sizeof(TCHAR));
}

static bool parse_uaeserial_port_option(const TCHAR *option, const TCHAR *value)
{
    const TCHAR *prefix = _T("uaeserial_port");
    int unit = 0;

    if (_tcsicmp(option, prefix)) {
        size_t prefix_len = _tcslen(prefix);
        if (_tcsnicmp(option, prefix, prefix_len)) {
            return false;
        }
        const TCHAR *unit_text = option + prefix_len;
        if (*unit_text == '_') {
            unit_text++;
        }
        if (!*unit_text) {
            unit = 0;
        } else if (!parse_int_value(unit_text, &unit)) {
            return false;
        }
    }
    if (unit < 0 || unit >= UNIX_UAESERIAL_MAX_UNITS) {
        write_log(_T("UAESER: ignoring unsupported Unix uaeserial unit %d\n"), unit);
        return true;
    }

    std::string port = trim_copy(value ? value : "");
    if (port.size() >= 2 &&
        ((port[0] == '"' && port[port.size() - 1] == '"') || (port[0] == '\'' && port[port.size() - 1] == '\''))) {
        port = port.substr(1, port.size() - 2);
    }
    if (lowercase_copy(port) == "none") {
        port.clear();
    }
    unix_uaeserial_set_port(unit, port.c_str());
    return true;
}

static int activity_priority_index_from_value(int value, int defpri)
{
    switch (value) {
    case 1:
        return 0;
    case 0:
        return 1;
    case -1:
        return 2;
    case -2:
        return 3;
    default:
        return defpri;
    }
}

static const TCHAR *configmult[] = { _T("1x"), _T("2x"), _T("3x"), _T("4x"), _T("5x"), _T("6x"), _T("7x"), _T("8x"), NULL };
static const TCHAR *uaescsimode[] = { _T("SCSIEMU"), _T("SPTI"), _T("SPTI+SCSISCAN"), NULL };

int target_cfgfile_load(struct uae_prefs *p, const TCHAR *filename, int type, int isdefault)
{
    if (isdefault && type == CONFIG_TYPE_DEFAULT && !file_exists(filename)) {
        return 1;
    }
    int loaded_type = type;
    return cfgfile_load(p, filename, &loaded_type, 0, !isdefault);
}

int target_parse_option(struct uae_prefs *p, const TCHAR *option, const TCHAR *value, int)
{
    if (!_tcsicmp(option, _T("ui.recursive_roms"))) {
        unix_romscan_set_recursive(parse_bool_value(value ? value : ""));
        return 1;
    }
    if (!_tcsicmp(option, _T("middle_mouse"))) {
        if (parse_bool_value(value ? value : "")) {
            p->input_mouse_untrap |= MOUSEUNTRAP_MIDDLEBUTTON;
        } else {
            p->input_mouse_untrap &= ~MOUSEUNTRAP_MIDDLEBUTTON;
        }
        return 1;
    }
    if (!_tcsicmp(option, _T("active_not_captured_pause"))) {
        p->win32_active_nocapture_pause = parse_bool_value(value ? value : "");
        return 1;
    }
    if (!_tcsicmp(option, _T("active_not_captured_nosound"))) {
        p->win32_active_nocapture_nosound = parse_bool_value(value ? value : "");
        return 1;
    }
    if (!_tcsicmp(option, _T("inactive_pause"))) {
        p->win32_inactive_pause = parse_bool_value(value ? value : "");
        return 1;
    }
    if (!_tcsicmp(option, _T("inactive_nosound"))) {
        p->win32_inactive_nosound = parse_bool_value(value ? value : "");
        return 1;
    }
    if (!_tcsicmp(option, _T("iconified_pause"))) {
        p->win32_iconified_pause = parse_bool_value(value ? value : "");
        return 1;
    }
    if (!_tcsicmp(option, _T("iconified_nosound"))) {
        p->win32_iconified_nosound = parse_bool_value(value ? value : "");
        return 1;
    }
    if (!_tcsicmp(option, _T("active_input"))
        || !_tcsicmp(option, _T("inactive_input"))
        || !_tcsicmp(option, _T("iconified_input"))) {
        int parsed = 0;
        if (!parse_int_value(value, &parsed)) {
            return 0;
        }
        if (!_tcsicmp(option, _T("active_input"))) {
            p->win32_active_input = parsed;
        } else if (!_tcsicmp(option, _T("inactive_input"))) {
            p->win32_inactive_input = parsed;
        } else {
            p->win32_iconified_input = parsed;
        }
        return 1;
    }
    if (!_tcsicmp(option, _T("active_priority"))
        || !_tcsicmp(option, _T("activepriority"))
        || !_tcsicmp(option, _T("inactive_priority"))
        || !_tcsicmp(option, _T("iconified_priority"))) {
        int parsed = 0;
        if (!parse_int_value(value, &parsed)) {
            return 0;
        }
        if (!_tcsicmp(option, _T("active_priority")) || !_tcsicmp(option, _T("activepriority"))) {
            p->win32_active_capture_priority = activity_priority_index_from_value(parsed, 1);
        } else if (!_tcsicmp(option, _T("inactive_priority"))) {
            p->win32_inactive_priority = activity_priority_index_from_value(parsed, 1);
        } else {
            p->win32_iconified_priority = activity_priority_index_from_value(parsed, 2);
        }
        return 1;
    }
    if (cfgfile_intval(option, value, _T("recording_width"), &p->aviout_width, 1)
        || cfgfile_intval(option, value, _T("recording_height"), &p->aviout_height, 1)
        || cfgfile_intval(option, value, _T("recording_x"), &p->aviout_xoffset, 1)
        || cfgfile_intval(option, value, _T("recording_y"), &p->aviout_yoffset, 1)
        || cfgfile_intval(option, value, _T("screenshot_width"), &p->screenshot_width, 1)
        || cfgfile_intval(option, value, _T("screenshot_height"), &p->screenshot_height, 1)
        || cfgfile_intval(option, value, _T("screenshot_x"), &p->screenshot_xoffset, 1)
        || cfgfile_intval(option, value, _T("screenshot_y"), &p->screenshot_yoffset, 1)
        || cfgfile_intval(option, value, _T("screenshot_min_width"), &p->screenshot_min_width, 1)
        || cfgfile_intval(option, value, _T("screenshot_min_height"), &p->screenshot_min_height, 1)
        || cfgfile_intval(option, value, _T("screenshot_max_width"), &p->screenshot_max_width, 1)
        || cfgfile_intval(option, value, _T("screenshot_max_height"), &p->screenshot_max_height, 1)
        || cfgfile_intval(option, value, _T("screenshot_output_width_limit"), &p->screenshot_output_width, 1)
        || cfgfile_intval(option, value, _T("screenshot_output_height_limit"), &p->screenshot_output_height, 1)) {
        return 1;
    }
    if (cfgfile_strval(option, value, _T("uaescsimode"), &p->win32_uaescsimode, uaescsimode, 0)) {
        return 1;
    }
    {
        TCHAR tmpbuf[CONFIG_BLEN];
        if (cfgfile_string(option, value, _T("rtg_vblank"), tmpbuf, sizeof tmpbuf / sizeof(TCHAR))) {
            if (!_tcsicmp(tmpbuf, _T("real"))) {
                p->win32_rtgvblankrate = -1;
            } else if (!_tcsicmp(tmpbuf, _T("disabled"))) {
                p->win32_rtgvblankrate = -2;
            } else if (!_tcsicmp(tmpbuf, _T("chipset"))) {
                p->win32_rtgvblankrate = 0;
            } else {
                p->win32_rtgvblankrate = _tstol(tmpbuf);
            }
            return 1;
        }
    }
    if (cfgfile_strval(option, value, _T("screenshot_mult_width"), &p->screenshot_xmult, configmult, 0)
        || cfgfile_strval(option, value, _T("screenshot_mult_height"), &p->screenshot_ymult, configmult, 0)) {
        return 1;
    }
#ifdef AVIOUTPUT
    if (!_tcsicmp(option, _T("screenshot_original_size")) || !_tcsicmp(option, _T("ui.screenshot_original_size"))) {
        screenshot_originalsize = parse_bool_value(value ? value : "");
        return 1;
    }
    if (!_tcsicmp(option, _T("screenshot_paletted")) || !_tcsicmp(option, _T("ui.screenshot_paletted"))) {
        screenshot_paletteindexed = parse_bool_value(value ? value : "");
        return 1;
    }
    if (!_tcsicmp(option, _T("screenshot_clip")) || !_tcsicmp(option, _T("ui.screenshot_clip"))) {
        screenshot_clipmode = parse_bool_value(value ? value : "");
        return 1;
    }
    if (!_tcsicmp(option, _T("screenshot_auto")) || !_tcsicmp(option, _T("ui.screenshot_auto"))) {
        screenshot_multi = parse_bool_value(value ? value : "") ? -1 : 0;
        if (screenshot_multi) {
            video_recording_active |= 2;
        } else {
            video_recording_active &= ~2;
        }
        return 1;
    }
    {
        TCHAR tmpbuf[MAX_DPATH];
        if (cfgfile_string_escape(option, value, _T("output_file"), tmpbuf, sizeof tmpbuf / sizeof(TCHAR)) ||
            cfgfile_string_escape(option, value, _T("ui.output_file"), tmpbuf, sizeof tmpbuf / sizeof(TCHAR))) {
            _tcsncpy(avioutput_filename_gui, tmpbuf, sizeof avioutput_filename_gui / sizeof(TCHAR) - 1);
            avioutput_filename_gui[sizeof avioutput_filename_gui / sizeof(TCHAR) - 1] = 0;
            return 1;
        }
    }
    if (!_tcsicmp(option, _T("output_frame_limiter_disabled")) || !_tcsicmp(option, _T("ui.output_frame_limiter_disabled"))) {
        avioutput_framelimiter = parse_bool_value(value ? value : "") ? 0 : 1;
        if (!avioutput_framelimiter) {
            avioutput_nosoundoutput = 1;
        }
        return 1;
    }
    if (!_tcsicmp(option, _T("output_original_size")) || !_tcsicmp(option, _T("ui.output_original_size"))) {
        avioutput_originalsize = parse_bool_value(value ? value : "");
        return 1;
    }
    if (!_tcsicmp(option, _T("output_no_sound")) || !_tcsicmp(option, _T("ui.output_no_sound"))) {
        avioutput_nosoundoutput = parse_bool_value(value ? value : "");
        return 1;
    }
    if (!_tcsicmp(option, _T("output_no_sound_sync")) || !_tcsicmp(option, _T("ui.output_no_sound_sync"))) {
        avioutput_nosoundsync = parse_bool_value(value ? value : "");
        return 1;
    }
    if (!_tcsicmp(option, _T("output_audio_codec")) || !_tcsicmp(option, _T("ui.output_audio_codec"))) {
        if (value && (!_tcsicmp(value, _T("wav")) || !_tcsicmp(value, _T("wave")))) {
            unix_avi_audio_codec = AVIAUDIO_WAV;
        } else {
            unix_avi_audio_codec = AVIAUDIO_AVI;
        }
        if (avioutput_audio) {
            avioutput_audio = unix_avi_audio_codec;
        }
        return 1;
    }
    if (!_tcsicmp(option, _T("output_video_codec")) || !_tcsicmp(option, _T("ui.output_video_codec"))) {
        return 1;
    }
    if (!_tcsicmp(option, _T("output_audio")) || !_tcsicmp(option, _T("ui.output_audio"))) {
        avioutput_audio = parse_bool_value(value ? value : "") ? unix_avi_audio_codec : 0;
        return 1;
    }
    if (!_tcsicmp(option, _T("output_video")) || !_tcsicmp(option, _T("ui.output_video"))) {
        avioutput_video = parse_bool_value(value ? value : "") ? 1 : 0;
        return 1;
    }
    if (!_tcsicmp(option, _T("output_enabled")) || !_tcsicmp(option, _T("ui.output_enabled"))) {
        if (parse_bool_value(value ? value : "")) {
            AVIOutput_Begin(false);
        } else {
            AVIOutput_End();
        }
        return 1;
    }
#endif
    if (!_tcsicmp(option, _T("serial_port"))) {
        std::string port = trim_copy(value ? value : "");
        if (port.size() >= 2 &&
            ((port[0] == '"' && port[port.size() - 1] == '"') || (port[0] == '\'' && port[port.size() - 1] == '\''))) {
            port = port.substr(1, port.size() - 2);
        }
        if (lowercase_copy(port) == "none") {
            port.clear();
        }
        uae_tcslcpy(p->sername, port.c_str(), sizeof p->sername / sizeof(TCHAR));
        p->use_serial = p->sername[0] != 0;
        return 1;
    }
    if (!_tcsicmp(option, _T("parallel_port"))) {
        std::string port = trim_copy(value ? value : "");
        if (port.size() >= 2 &&
            ((port[0] == '"' && port[port.size() - 1] == '"') || (port[0] == '\'' && port[port.size() - 1] == '\''))) {
            port = port.substr(1, port.size() - 2);
        }
        if (lowercase_copy(port) == "none") {
            port.clear();
        } else if (lowercase_copy(port) == "default") {
            port = DEFPRTNAME;
        }
        uae_tcslcpy(p->prtname, port.c_str(), sizeof p->prtname / sizeof(TCHAR));
        return 1;
    }
    if (parse_uaeserial_port_option(option, value)) {
        return 1;
    }
    if (!_tcsicmp(option, _T("soundcard"))) {
        int parsed = 0;
        if (!parse_int_value(value, &parsed)) {
            return 0;
        }
        if (parsed < 0 || parsed >= unix_sound_device_count()) {
            parsed = 0;
        }
        p->win32_soundcard = parsed;
        return 1;
    }
    if (!_tcsicmp(option, _T("soundcardname"))) {
        int index = unix_sound_device_index_from_config_name(value);
        if (index >= 0) {
            p->win32_soundcard = index;
        }
        return 1;
    }
    if (!_tcsicmp(option, _T("samplersoundcard")) || !_tcsicmp(option, _T("sampler_soundcard"))) {
        int parsed = 0;
        if (!parse_int_value(value, &parsed)) {
            return 0;
        }
        if (parsed < 0 || parsed >= unix_sampler_device_count()) {
            parsed = -1;
        }
        p->win32_samplersoundcard = parsed;
        return 1;
    }
    if (!_tcsicmp(option, _T("samplersoundcardname")) || !_tcsicmp(option, _T("sampler_soundcardname"))) {
        int index = unix_sampler_device_index_from_config_name(value);
        if (index >= 0) {
            p->win32_samplersoundcard = index;
        }
        return 1;
    }
    if (cfgfile_intval(option, value, _T("midi_device"), &p->win32_midioutdev, 1)
        || cfgfile_intval(option, value, _T("midiout_device"), &p->win32_midioutdev, 1)) {
        return 1;
    }
    if (cfgfile_intval(option, value, _T("midiin_device"), &p->win32_midiindev, 1)) {
#ifndef WITH_MIDI
        p->win32_midiindev = -1;
#endif
        return 1;
    }
    if (cfgfile_yesno(option, value, _T("midirouter"), &p->win32_midirouter)) {
#ifndef WITH_MIDI
        p->win32_midirouter = false;
#endif
        return 1;
    }
    TCHAR tmpbuf[256];
    if (cfgfile_string_escape(option, value, _T("midiout_device_name"), tmpbuf, sizeof tmpbuf / sizeof(TCHAR))) {
#ifdef WITH_MIDI
        p->win32_midioutdev = unix_midi_output_device_id_from_config_name(tmpbuf);
#else
        p->win32_midioutdev = !_tcsicmp(tmpbuf, _T("default")) ? -1 : -2;
#endif
        return 1;
    }
    if (cfgfile_string_escape(option, value, _T("midiin_device_name"), tmpbuf, sizeof tmpbuf / sizeof(TCHAR))) {
#ifdef WITH_MIDI
        p->win32_midiindev = unix_midi_input_device_id_from_config_name(tmpbuf);
#else
        p->win32_midiindev = -1;
#endif
        return 1;
    }
    if (parse_path_option(option, value, _T("config_path"), path_configuration, sizeof path_configuration / sizeof(TCHAR))
        || parse_path_option(option, value, _T("ui.config_path"), path_configuration, sizeof path_configuration / sizeof(TCHAR))
        || parse_path_option(option, value, _T("nvram_path"), path_nvram, sizeof path_nvram / sizeof(TCHAR))
        || parse_path_option(option, value, _T("ui.nvram_path"), path_nvram, sizeof path_nvram / sizeof(TCHAR))
        || parse_path_option(option, value, _T("screenshot_path"), path_screenshot, sizeof path_screenshot / sizeof(TCHAR))
        || parse_path_option(option, value, _T("ui.screenshot_path"), path_screenshot, sizeof path_screenshot / sizeof(TCHAR))
        || parse_path_option(option, value, _T("video_path"), path_video, sizeof path_video / sizeof(TCHAR))
        || parse_path_option(option, value, _T("ui.video_path"), path_video, sizeof path_video / sizeof(TCHAR))
        || parse_path_option(option, value, _T("saveimage_path"), path_saveimage, sizeof path_saveimage / sizeof(TCHAR))
        || parse_path_option(option, value, _T("ui.saveimage_path"), path_saveimage, sizeof path_saveimage / sizeof(TCHAR))
        || parse_path_option(option, value, _T("rip_path"), path_ripper, sizeof path_ripper / sizeof(TCHAR))
        || parse_path_option(option, value, _T("ripper_path"), path_ripper, sizeof path_ripper / sizeof(TCHAR))
        || parse_path_option(option, value, _T("ui.rip_path"), path_ripper, sizeof path_ripper / sizeof(TCHAR))
        || parse_path_option(option, value, _T("data_path"), path_data, sizeof path_data / sizeof(TCHAR))
        || parse_path_option(option, value, _T("ui.data_path"), path_data, sizeof path_data / sizeof(TCHAR))
        || parse_rom_path_option(p, option, value)) {
        return 1;
    }
    return 0;
}

void target_save_options(struct zfile *f, struct uae_prefs *p)
{
    cfgfile_target_dwrite_str(f, _T("serial_port"), p->sername[0] ? p->sername : _T("none"));
    cfgfile_target_dwrite_str_escape(f, _T("parallel_port"), p->prtname[0] ? p->prtname : _T("none"));
    for (int i = 0; i < UNIX_UAESERIAL_MAX_UNITS; i++) {
        if (uaeserial_ports[i][0]) {
            TCHAR option[64];
            _stprintf(option, _T("uaeserial_port%d"), i);
            cfgfile_target_write_str(f, option, uaeserial_ports[i]);
        }
    }

    int index = p->win32_soundcard;
    if (index < 0 || index >= unix_sound_device_count()) {
        index = 0;
    }
    cfgfile_target_write(f, _T("soundcard"), _T("%d"), index);
    const TCHAR *name = unix_sound_device_config_name(index);
    if (name && name[0]) {
        cfgfile_target_write_str(f, _T("soundcardname"), name);
    }
    if (p->win32_samplersoundcard >= 0 && p->win32_samplersoundcard < unix_sampler_device_count()) {
        cfgfile_target_write(f, _T("samplersoundcard"), _T("%d"), p->win32_samplersoundcard);
        const TCHAR *sampler_name = unix_sampler_device_config_name(p->win32_samplersoundcard);
        if (sampler_name && sampler_name[0]) {
            cfgfile_target_write_str(f, _T("samplersoundcardname"), sampler_name);
        }
    }
    cfgfile_target_dwrite(f, _T("midiout_device"), _T("%d"), p->win32_midioutdev);
    cfgfile_target_dwrite(f, _T("midiin_device"), _T("%d"), p->win32_midiindev);
#ifdef WITH_MIDI
    cfgfile_target_dwrite_str_escape(f, _T("midiout_device_name"), unix_midi_output_device_config_name_for_id(p->win32_midioutdev));
    cfgfile_target_dwrite_str_escape(f, _T("midiin_device_name"), unix_midi_input_device_config_name_for_id(p->win32_midiindev));
#else
    cfgfile_target_dwrite_str_escape(f, _T("midiout_device_name"), p->win32_midioutdev == -1 ? _T("default") : _T("none"));
    cfgfile_target_dwrite_str_escape(f, _T("midiin_device_name"), _T("none"));
#endif
    cfgfile_target_dwrite_bool(f, _T("midirouter"), p->win32_midirouter);
    int scsimode = p->win32_uaescsimode;
    if (scsimode < 0 || scsimode > UAESCSI_LAST) {
        scsimode = UAESCSI_SPTI;
    }
    cfgfile_target_dwrite_str(f, _T("uaescsimode"), uaescsimode[scsimode]);
    if (p->win32_rtgvblankrate <= 0) {
        cfgfile_target_dwrite_str(f, _T("rtg_vblank"),
            p->win32_rtgvblankrate == -1 ? _T("real") : (p->win32_rtgvblankrate == -2 ? _T("disabled") : _T("chipset")));
    } else {
        cfgfile_target_dwrite(f, _T("rtg_vblank"), _T("%d"), p->win32_rtgvblankrate);
    }
    cfgfile_target_dwrite(f, _T("recording_width"), _T("%d"), p->aviout_width);
    cfgfile_target_dwrite(f, _T("recording_height"), _T("%d"), p->aviout_height);
    cfgfile_target_dwrite(f, _T("recording_x"), _T("%d"), p->aviout_xoffset);
    cfgfile_target_dwrite(f, _T("recording_y"), _T("%d"), p->aviout_yoffset);
    cfgfile_target_dwrite(f, _T("screenshot_width"), _T("%d"), p->screenshot_width);
    cfgfile_target_dwrite(f, _T("screenshot_height"), _T("%d"), p->screenshot_height);
    cfgfile_target_dwrite(f, _T("screenshot_x"), _T("%d"), p->screenshot_xoffset);
    cfgfile_target_dwrite(f, _T("screenshot_y"), _T("%d"), p->screenshot_yoffset);
    cfgfile_target_dwrite(f, _T("screenshot_min_width"), _T("%d"), p->screenshot_min_width);
    cfgfile_target_dwrite(f, _T("screenshot_min_height"), _T("%d"), p->screenshot_min_height);
    cfgfile_target_dwrite(f, _T("screenshot_max_width"), _T("%d"), p->screenshot_max_width);
    cfgfile_target_dwrite(f, _T("screenshot_max_height"), _T("%d"), p->screenshot_max_height);
    cfgfile_target_dwrite(f, _T("screenshot_output_width_limit"), _T("%d"), p->screenshot_output_width);
    cfgfile_target_dwrite(f, _T("screenshot_output_height_limit"), _T("%d"), p->screenshot_output_height);
    cfgfile_target_dwrite_str(f, _T("screenshot_mult_width"), configmult[p->screenshot_xmult]);
    cfgfile_target_dwrite_str(f, _T("screenshot_mult_height"), configmult[p->screenshot_ymult]);
#ifdef AVIOUTPUT
    cfgfile_target_dwrite_bool(f, _T("screenshot_original_size"), screenshot_originalsize != 0);
    cfgfile_target_dwrite_bool(f, _T("screenshot_paletted"), screenshot_paletteindexed != 0);
    cfgfile_target_dwrite_bool(f, _T("screenshot_clip"), screenshot_clipmode != 0);
    cfgfile_target_dwrite_bool(f, _T("screenshot_auto"), screenshot_multi != 0);
    if (avioutput_filename_gui[0]) {
        cfgfile_target_dwrite_str_escape(f, _T("output_file"), avioutput_filename_gui);
    }
    cfgfile_target_dwrite_bool(f, _T("output_frame_limiter_disabled"), avioutput_framelimiter == 0);
    cfgfile_target_dwrite_bool(f, _T("output_original_size"), avioutput_originalsize != 0);
    cfgfile_target_dwrite_bool(f, _T("output_no_sound"), avioutput_nosoundoutput != 0);
    cfgfile_target_dwrite_bool(f, _T("output_no_sound_sync"), avioutput_nosoundsync != 0);
    cfgfile_target_write_str(f, _T("output_audio_codec"),
        avioutput_audio == AVIAUDIO_WAV ? _T("wav") : _T("pcm"));
    cfgfile_target_write_str(f, _T("output_video_codec"), _T("dib"));
    cfgfile_target_dwrite_bool(f, _T("output_audio"), avioutput_audio != 0);
    cfgfile_target_dwrite_bool(f, _T("output_video"), avioutput_video != 0);
    cfgfile_target_dwrite_bool(f, _T("output_enabled"), avioutput_requested != 0);
#endif
    if (path_configuration[0]) {
        cfgfile_target_write_str(f, _T("config_path"), path_configuration);
    }
    if (path_nvram[0]) {
        cfgfile_target_write_str(f, _T("nvram_path"), path_nvram);
    }
    if (path_screenshot[0]) {
        cfgfile_target_write_str(f, _T("screenshot_path"), path_screenshot);
    }
    if (path_video[0]) {
        cfgfile_target_write_str(f, _T("video_path"), path_video);
    }
    if (path_saveimage[0]) {
        cfgfile_target_write_str(f, _T("saveimage_path"), path_saveimage);
    }
    if (path_ripper[0]) {
        cfgfile_target_write_str(f, _T("rip_path"), path_ripper);
    }
    if (path_data[0]) {
        cfgfile_target_write_str(f, _T("data_path"), path_data);
    }
    if (path_rom[0]) {
        cfgfile_target_write_str(f, _T("rom_path"), path_rom);
    }
}

void target_default_options(struct uae_prefs *p, int)
{
    path_configuration[0] = 0;
    path_nvram[0] = 0;
    path_screenshot[0] = 0;
    path_video[0] = 0;
    path_saveimage[0] = 0;
    path_ripper[0] = 0;
    path_data[0] = 0;
    path_rom[0] = 0;
    for (int i = 0; i < UNIX_UAESERIAL_MAX_UNITS; i++) {
        uaeserial_ports[i][0] = 0;
    }
    unix_romscan_mark_dirty();
    p->rtg_dacswitch = true;
    p->rtg_hardwaresprite = true;
    p->win32_rtgvblankrate = 0;
    p->win32_samplersoundcard = -1;
    p->win32_midioutdev = -2;
    p->win32_midiindev = -1;
    p->win32_midirouter = false;
}

void target_fixup_options(struct uae_prefs *p)
{
#ifndef WITH_MIDI
    p->win32_midiindev = -1;
    p->win32_midirouter = false;
#endif
    if (p->win32_uaescsimode > UAESCSI_LAST) {
        p->win32_uaescsimode = UAESCSI_SPTI;
    }
    unix_romscan_refresh(p, false);
}

void target_multipath_modified(struct uae_prefs*)
{
    unix_romscan_mark_dirty();
}

bool target_isrelativemode(void)
{
    return false;
}

TCHAR *target_expand_environment(const TCHAR *path, TCHAR *out, int maxlen)
{
    if (!path) {
        return NULL;
    }

    std::string expanded = unix_expand_path(path);
    if (out) {
        uae_tcslcpy(out, expanded.c_str(), maxlen);
        return out;
    }
    return my_strdup(expanded.c_str());
}

bool get_plugin_path(TCHAR *out, int size, const TCHAR *path)
{
    uae_tcslcpy(out, path, size);
    return true;
}

void stripslashes(TCHAR *p)
{
    while (*p) {
        if (*p == '\\') {
            *p = '/';
        }
        p++;
    }
}

void fixtrailing(TCHAR *p)
{
    int len = _tcslen(p);
    if (len > 0 && p[len - 1] != '/') {
        _tcscat(p, "/");
    }
}

void fullpath(TCHAR *path, int size)
{
    fullpath(path, size, false);
}

void fullpath(TCHAR *path, int size, bool)
{
    if (!path || !path[0]) {
        return;
    }
    const std::string absolute = unix_absolute_path(path);
    uae_tcslcpy(path, absolute.c_str(), size);
}

void getpathpart(TCHAR *outpath, int size, const TCHAR *inpath)
{
    uae_tcslcpy(outpath, inpath, size);
    TCHAR *slash = _tcsrchr(outpath, '/');
    if (slash) {
        slash[1] = 0;
    } else {
        outpath[0] = 0;
    }
}

void getfilepart(TCHAR *out, int size, const TCHAR *path)
{
    const TCHAR *slash = _tcsrchr(path, '/');
    uae_tcslcpy(out, slash ? slash + 1 : path, size);
}

bool samepath(const TCHAR *p1, const TCHAR *p2)
{
    return _tcscmp(p1, p2) == 0;
}

static void fetch_home_path(TCHAR *out, int size)
{
    const char *home = getenv("HOME");
    uae_tcslcpy(out, home ? home : ".", size);
    fixtrailing(out);
}

static void fetch_user_data_path(TCHAR *out, int size, const char *subdir)
{
    if (!out || size <= 0) {
        return;
    }
    const char *home = getenv("HOME");
    const char *base = home ? home : ".";
    if (subdir && subdir[0]) {
        snprintf(out, (size_t)size, "%s/Documents/WinUAE/%s", base, subdir);
    } else {
        snprintf(out, (size_t)size, "%s/Documents/WinUAE", base);
    }
    fixtrailing(out);
}

static void fetch_user_data_path_override(TCHAR *out, int size, const TCHAR *override_path, const char *regkey, const char *subdir)
{
    if (override_path && override_path[0]) {
        uae_tcslcpy(out, override_path, size);
        fixtrailing(out);
        return;
    }
    /* Per-config overrides win; otherwise consult the persistent host
     * settings (winuae.ini) before falling back to built-in defaults. */
    if (regkey) {
        TCHAR stored[MAX_DPATH];
        int stored_size = MAX_DPATH;
        if (regquerystr(NULL, regkey, stored, &stored_size) && stored[0]) {
            target_expand_environment(stored, out, size);
            fixtrailing(out);
            return;
        }
    }
    fetch_user_data_path(out, size, subdir);
}

void fetch_saveimagepath(TCHAR *out, int size, int) { fetch_user_data_path_override(out, size, path_saveimage, "SaveimagePath", "SaveImages"); }
void fetch_configurationpath(TCHAR *out, int size) { fetch_user_data_path_override(out, size, path_configuration, "ConfigurationPath", "Configuration"); }
void fetch_nvrampath(TCHAR *out, int size) { fetch_user_data_path_override(out, size, path_nvram, "NVRAMPath", "NVRAMs"); }
void fetch_luapath(TCHAR *out, int size) { fetch_home_path(out, size); }
void fetch_screenshotpath(TCHAR *out, int size) { fetch_user_data_path_override(out, size, path_screenshot, "ScreenshotPath", "Screenshots"); }
void fetch_ripperpath(TCHAR *out, int size) { fetch_user_data_path_override(out, size, path_ripper, "RipperPath", "Rips"); }
void fetch_statefilepath(TCHAR *out, int size) { fetch_user_data_path_override(out, size, path_statefile, "StatefilePath", "Save States"); }
void fetch_inputfilepath(TCHAR *out, int size) { fetch_home_path(out, size); }
void fetch_datapath(TCHAR *out, int size) { fetch_user_data_path_override(out, size, path_data, "DataPath", NULL); }
void fetch_rompath(TCHAR *out, int size) { fetch_user_data_path_override(out, size, path_rom, "KickstartPath", "Kickstarts"); }
void fetch_videopath(TCHAR *out, int size) { fetch_user_data_path_override(out, size, path_video, "VideoPath", "Videos"); }

void target_getdate(int *y, int *m, int *d)
{
    *y = GETBDY(WINUAEDATE);
    *m = GETBDM(WINUAEDATE);
    *d = GETBDD(WINUAEDATE);
}
