#include "prefs_adapter.h"

// Qt defines Qt::HANDLE, and the Unix core compatibility header defines HANDLE.
#include "config.h"

#include <QByteArray>
#include <QString>

#include "sysconfig.h"
#include "sysdeps.h"
#include "custom.h"
#include "options.h"
#include "romscan.h"

enum class ApplySettingResult {
    Fallback,
    Handled,
};

struct Choice {
    const char *name;
    int value;
};

template <size_t N>
static bool parseChoice(const QString &text, const Choice (&choices)[N], int *out)
{
    for (const Choice &choice : choices) {
        if (text.compare(QString::fromLatin1(choice.name), Qt::CaseInsensitive) == 0) {
            *out = choice.value;
            return true;
        }
    }
    return false;
}

static bool textToInt(const QString &text, int *out)
{
    bool ok = false;
    const int value = text.toInt(&ok, 0);
    if (ok && out) {
        *out = value;
    }
    return ok;
}

static bool settingToInt(const WinUaeQtConfig::Settings &settings, const QString &key, int *out)
{
    if (!settings.contains(key)) {
        return false;
    }
    return textToInt(settings.value(key), out);
}

static bool textToBool(const QString &text, bool *out)
{
    const QString normalized = text.trimmed().toLower();
    if (normalized == QStringLiteral("true") || normalized == QStringLiteral("t") ||
        normalized == QStringLiteral("yes") || normalized == QStringLiteral("y") ||
        normalized == QStringLiteral("1")) {
        if (out) {
            *out = true;
        }
        return true;
    }
    if (normalized == QStringLiteral("false") || normalized == QStringLiteral("f") ||
        normalized == QStringLiteral("no") || normalized == QStringLiteral("n") ||
        normalized == QStringLiteral("0")) {
        if (out) {
            *out = false;
        }
        return true;
    }
    return false;
}

static bool rtgVBlankValueToInt(const QString &value, int *out)
{
    const QString normalized = value.trimmed().toLower();
    int parsed = 0;
    bool ok = false;
    if (normalized == QStringLiteral("real") || normalized == QStringLiteral("default")) {
        parsed = -1;
        ok = true;
    } else if (normalized == QStringLiteral("disabled")) {
        parsed = -2;
        ok = true;
    } else if (normalized == QStringLiteral("chipset")) {
        parsed = 0;
        ok = true;
    } else {
        parsed = normalized.toInt(&ok);
    }
    if (ok && out) {
        *out = parsed;
    }
    return ok;
}

static void copyTextSetting(TCHAR *dst, size_t dstSize, const QString &value)
{
    if (!dst || dstSize == 0) {
        return;
    }
    QByteArray text = value.toLocal8Bit();
    _tcsncpy(dst, text.constData(), dstSize - 1);
    dst[dstSize - 1] = 0;
}

static bool parseChipsetCompatible(const QString &value, int *out)
{
    static const Choice choices[] = {
        { "Generic", CP_GENERIC },
        { "CDTV", CP_CDTV },
        { "CDTV-CR", CP_CDTVCR },
        { "CD32", CP_CD32 },
        { "A500", CP_A500 },
        { "A500+", CP_A500P },
        { "A600", CP_A600 },
        { "A1000", CP_A1000 },
        { "A1200", CP_A1200 },
        { "A2000", CP_A2000 },
        { "A3000", CP_A3000 },
        { "A3000T", CP_A3000T },
        { "A4000", CP_A4000 },
        { "A4000T", CP_A4000T },
        { "Velvet", CP_VELVET },
        { "Casablanca", CP_CASABLANCA },
        { "DraCo", CP_DRACO },
    };
    return parseChoice(value, choices, out);
}

static void parseSettingOption(struct uae_prefs *prefs, const QString &key, const QString &value)
{
    if (value.isEmpty()) {
        return;
    }
    if (key.startsWith(QStringLiteral("unix.ui.")) &&
        key != QStringLiteral("unix.ui.recursive_roms")) {
        return;
    }
    if (key == QStringLiteral("uaescsimode") || key == QStringLiteral("unix.uaescsimode")) {
        return;
    }
    if (key == QStringLiteral("rtg_vblank") || key == QStringLiteral("unix.rtg_vblank")) {
        return;
    }
    QByteArray option = key.toLocal8Bit();
    QByteArray optionValue = value.toLocal8Bit();
    cfgfile_parse_option(prefs, option.constData(), optionValue.data(), 0);
}

static void applyCycleExactConstraint(struct uae_prefs *prefs)
{
    if (prefs->cpu_model >= 68020 && prefs->cachesize > 0) {
        prefs->cpu_cycle_exact = false;
        prefs->cpu_memory_cycle_exact = false;
        prefs->blitter_cycle_exact = false;
    }
}

// Only launcher semantics belong here. Ordinary settings are interpreted by
// cfgfile_parse_option so config loading and GUI edits use the same parser.
static ApplySettingResult applyLauncherSetting(const WinUaeQtConfig::Settings &settings,
    const WinUaeQtConfig::Setting &setting, struct uae_prefs *prefs)
{
    auto applyInt = [&](const char *key, int *field) -> bool {
        if (setting.key != QString::fromLatin1(key)) {
            return false;
        }
        int parsed = 0;
        if (textToInt(setting.value, &parsed)) {
            *field = parsed;
        }
        return true;
    };
    auto applyText = [&](const char *key, TCHAR *field, size_t size) -> bool {
        if (setting.key != QString::fromLatin1(key)) {
            return false;
        }
        copyTextSetting(field, size, setting.value);
        return true;
    };

    if (applyText("config_description", prefs->description, sizeof prefs->description / sizeof(TCHAR))) {
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("kickstart_rom_file")) {
        copyTextSetting(prefs->romfile, sizeof prefs->romfile / sizeof(TCHAR), setting.value);
        // cfgfile abbreviates a scanned ROM to a bare name relative to the ROM
        // search path; resolve it to a full path so the core can load it.
        unix_resolve_rom_path(prefs, prefs->romfile, sizeof prefs->romfile / sizeof(TCHAR));
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("kickstart_ext_rom_file")) {
        copyTextSetting(prefs->romextfile, sizeof prefs->romextfile / sizeof(TCHAR), setting.value);
        unix_resolve_rom_path(prefs, prefs->romextfile, sizeof prefs->romextfile / sizeof(TCHAR));
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("kickstart_ext_rom_file2")) {
        copyTextSetting(prefs->romextfile2, sizeof prefs->romextfile2 / sizeof(TCHAR), setting.value);
        unix_resolve_rom_path(prefs, prefs->romextfile2, sizeof prefs->romextfile2 / sizeof(TCHAR));
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("flash_file")) {
        copyTextSetting(prefs->flashfile, sizeof prefs->flashfile / sizeof(TCHAR), setting.value);
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("cart_file")) {
        copyTextSetting(prefs->cartfile, sizeof prefs->cartfile / sizeof(TCHAR), setting.value);
        unix_resolve_rom_path(prefs, prefs->cartfile, sizeof prefs->cartfile / sizeof(TCHAR));
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("rtc_file")) {
        copyTextSetting(prefs->rtcfile, sizeof prefs->rtcfile / sizeof(TCHAR), setting.value);
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("picassoiv_rom_file")) {
        copyTextSetting(prefs->picassoivromfile, sizeof prefs->picassoivromfile / sizeof(TCHAR), setting.value);
        return ApplySettingResult::Handled;
    }
    // Previewing preferences must not schedule a savestate restore or change
    // global savestate paths through the core parser.
    if (setting.key == QStringLiteral("statefile")) {
        copyTextSetting(prefs->statefile, sizeof prefs->statefile / sizeof(TCHAR), setting.value);
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("statefile_quit")) {
        copyTextSetting(prefs->quitstatefile, sizeof prefs->quitstatefile / sizeof(TCHAR), setting.value);
        return ApplySettingResult::Handled;
    }

    int value = 0;
    bool boolValue = false;
    for (int drive = 0; drive < 4; drive++) {
        const QString key = QStringLiteral("floppy%1").arg(drive);
        if (setting.key == key) {
            copyTextSetting(prefs->floppyslots[drive].df,
                sizeof prefs->floppyslots[drive].df / sizeof(TCHAR), setting.value);
            return ApplySettingResult::Handled;
        }
    }
    for (int drive = 0; drive < 4; drive++) {
        const QString soundKey = QStringLiteral("floppy%1sound").arg(drive);
        const QString externalKey = QStringLiteral("floppy%1soundext").arg(drive);
        if (setting.key == soundKey || setting.key == externalKey) {
            if (settingToInt(settings, soundKey, &value)) {
                prefs->floppyslots[drive].dfxclick = value;
            }
            if (prefs->floppyslots[drive].dfxclick < 0 && settings.contains(externalKey)) {
                copyTextSetting(
                    prefs->floppyslots[drive].dfxclickexternal,
                    sizeof prefs->floppyslots[drive].dfxclickexternal / sizeof(TCHAR),
                    settings.value(externalKey));
            } else {
                prefs->floppyslots[drive].dfxclickexternal[0] = 0;
            }
            return ApplySettingResult::Handled;
        }
    }

    if (applyText("genlock_image", prefs->genlock_image_file, sizeof prefs->genlock_image_file / sizeof(TCHAR))
        || applyText("genlock_video", prefs->genlock_video_file, sizeof prefs->genlock_video_file / sizeof(TCHAR))
        || applyText("ghostscript_parameters", prefs->ghostscript_parameters, sizeof prefs->ghostscript_parameters / sizeof(TCHAR))
        || applyText("statefile_path", prefs->statefile_path, sizeof prefs->statefile_path / sizeof(TCHAR))) {
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("unix.serial_port")) {
        const QString port = setting.value.trimmed();
        if (port.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0) {
            prefs->sername[0] = 0;
        } else {
            copyTextSetting(prefs->sername, sizeof prefs->sername / sizeof(TCHAR), port);
        }
        prefs->use_serial = prefs->sername[0] != 0;
        return ApplySettingResult::Handled;
    }
    // Device indices are launcher selections. The target parser can clamp them
    // against an enumeration that is not yet available during a preview.
    if (applyInt("midiout_device", &prefs->win32_midioutdev)
        || applyInt("midi_device", &prefs->win32_midioutdev)
        || applyInt("midiin_device", &prefs->win32_midiindev)
        || applyInt("unix.soundcard", &prefs->win32_soundcard)
        || applyInt("unix.samplersoundcard", &prefs->win32_samplersoundcard)
        || applyInt("samplersoundcard", &prefs->win32_samplersoundcard)) {
        return ApplySettingResult::Handled;
    }
    // Keep the launcher's order-sensitive CPU/JIT constraints until they
    // share a selection helper with the other frontends.
    if (setting.key == QStringLiteral("cpu_model")) {
        if (textToInt(setting.value, &value)) {
            prefs->cpu_model = value;
            prefs->fpu_model = 0;
            applyCycleExactConstraint(prefs);
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("cpu_cycle_exact")) {
        if (textToBool(setting.value, &boolValue)) {
            prefs->cpu_cycle_exact = boolValue;
            prefs->cpu_memory_cycle_exact = boolValue;
            applyCycleExactConstraint(prefs);
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("cpu_memory_cycle_exact")) {
        if (textToBool(setting.value, &boolValue)) {
            prefs->cpu_memory_cycle_exact = boolValue;
            if (!boolValue) {
                prefs->cpu_cycle_exact = false;
                prefs->blitter_cycle_exact = false;
            }
            applyCycleExactConstraint(prefs);
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("blitter_cycle_exact")) {
        if (textToBool(setting.value, &boolValue)) {
            prefs->blitter_cycle_exact = boolValue;
            applyCycleExactConstraint(prefs);
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("chipset_compatible")) {
        if (parseChipsetCompatible(setting.value, &value)) {
            prefs->cs_compatible = value;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("cachesize")) {
        if (textToInt(setting.value, &value)) {
            prefs->cachesize = value;
            applyCycleExactConstraint(prefs);
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("midirouter")) {
#ifdef WITH_MIDI
        if (textToBool(setting.value, &boolValue)) {
            prefs->win32_midirouter = boolValue;
        }
#else
        prefs->win32_midirouter = false;
#endif
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("gfx_width_windowed") ||
        setting.key == QStringLiteral("gfx_height_windowed")) {
        const QString width = settings.value(QStringLiteral("gfx_width_windowed"));
        const QString height = settings.value(QStringLiteral("gfx_height_windowed"));
        if (width.compare(QStringLiteral("native"), Qt::CaseInsensitive) == 0 ||
            height.compare(QStringLiteral("native"), Qt::CaseInsensitive) == 0) {
            prefs->gfx_monitor[0].gfx_size_win.width = 0;
            prefs->gfx_monitor[0].gfx_size_win.height = 0;
        } else if (setting.key == QStringLiteral("gfx_width_windowed") && textToInt(setting.value, &value)) {
            prefs->gfx_monitor[0].gfx_size_win.width = value;
        } else if (setting.key == QStringLiteral("gfx_height_windowed") && textToInt(setting.value, &value)) {
            prefs->gfx_monitor[0].gfx_size_win.height = value;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("gfx_width_fullscreen") ||
        setting.key == QStringLiteral("gfx_height_fullscreen")) {
        if (setting.value.compare(QStringLiteral("native"), Qt::CaseInsensitive) == 0) {
            prefs->gfx_monitor[0].gfx_size_fs.width = 0;
            prefs->gfx_monitor[0].gfx_size_fs.height = 0;
            prefs->gfx_monitor[0].gfx_size_fs.special = WH_NATIVE;
        } else if (setting.key == QStringLiteral("gfx_width_fullscreen") && textToInt(setting.value, &value)) {
            prefs->gfx_monitor[0].gfx_size_fs.width = value;
            prefs->gfx_monitor[0].gfx_size_fs.special = 0;
        } else if (setting.key == QStringLiteral("gfx_height_fullscreen") && textToInt(setting.value, &value)) {
            prefs->gfx_monitor[0].gfx_size_fs.height = value;
            prefs->gfx_monitor[0].gfx_size_fs.special = 0;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("uaescsimode") || setting.key == QStringLiteral("unix.uaescsimode")) {
        if (setting.value.compare(QStringLiteral("SPTI+SCSISCAN"), Qt::CaseInsensitive) == 0) {
            prefs->win32_uaescsimode = UAESCSI_SPTISCAN;
        } else if (setting.value.compare(QStringLiteral("SPTI"), Qt::CaseInsensitive) == 0) {
            prefs->win32_uaescsimode = UAESCSI_SPTI;
        } else {
            prefs->win32_uaescsimode = UAESCSI_CDEMU;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("rtg_vblank") || setting.key == QStringLiteral("unix.rtg_vblank")) {
        if (rtgVBlankValueToInt(setting.value, &value)) {
            prefs->win32_rtgvblankrate = value;
        }
        return ApplySettingResult::Handled;
    }
    return ApplySettingResult::Fallback;
}

bool applyWinUaeQtConfigToPrefs(const WinUaeQtConfig &config, struct uae_prefs *prefs, bool fullReset)
{
    if (!prefs) {
        return false;
    }

    // On the Start/Reset commit path, reset prefs to defaults before applying so
    // the merged config alone defines the machine. This applies a config *text*
    // onto prefs (unlike win32, which copies a prefs struct), so any key the
    // launcher omits when empty - an ejected CD/floppy, a dropped extended/cart
    // ROM, a reassigned hardfile - would otherwise leave a stale value behind.
    // default_prefs()'s memset clears them all at once, mirroring win32's
    // prefs_to_gui (default_prefs + copy) and cfgfile_load's default baseline.
    // The preview/refresh path (fullReset == false) instead applies onto the live
    // prefs so it can enumerate boards for the current config without a full wipe.
    if (fullReset) {
        default_prefs(prefs, false, 0);
    }

    const WinUaeQtConfig::Settings &settings = config.settings();
    for (const WinUaeQtConfig::Setting &setting : config.orderedSettings()) {
        if (applyLauncherSetting(settings, setting, prefs) == ApplySettingResult::Handled) {
            continue;
        }
        parseSettingOption(prefs, setting.key, setting.value);
    }
    return true;
}
