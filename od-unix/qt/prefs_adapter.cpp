#include "prefs_adapter.h"

// Qt defines Qt::HANDLE, and the Unix core compatibility header defines HANDLE.
#include "config.h"

#include <QByteArray>
#include <QString>

#include "sysconfig.h"
#include "sysdeps.h"
#include "audio.h"
#include "custom.h"
#include "gfxboard.h"
#include "options.h"

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

static bool settingToBool(const WinUaeQtConfig::Settings &settings, const QString &key, bool *out)
{
    if (!settings.contains(key)) {
        return false;
    }
    return textToBool(settings.value(key), out);
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

static bool parseSoundOutput(const QString &value, int *out)
{
    static const Choice choices[] = {
        { "none", 0 },
        { "interrupts", 1 },
        { "normal", 2 },
        { "good", 2 },
        { "exact", 3 },
        { "best", 3 },
    };
    return parseChoice(value, choices, out);
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

static bool parseGfxCardType(const QString &value, int *out)
{
    for (int index = 0;; index++) {
        const int id = gfxboard_get_id_from_index(index);
        if (id < 0) {
            break;
        }
        const TCHAR *name = gfxboard_get_configname(id);
        if (name && value.compare(QString::fromLocal8Bit(name), Qt::CaseInsensitive) == 0) {
            *out = id;
            return true;
        }
    }
    return false;
}

static bool parseSoundChannels(const QString &value, int *out, struct uae_prefs *prefs)
{
    static const Choice choices[] = {
        { "mono", SND_MONO },
        { "stereo", SND_STEREO },
        { "clonedstereo", SND_4CH_CLONEDSTEREO },
        { "4ch", SND_4CH },
        { "clonedstereo6ch", SND_6CH_CLONEDSTEREO },
        { "6ch", SND_6CH },
        { "clonedstereo8ch", SND_8CH_CLONEDSTEREO },
        { "8ch", SND_8CH },
        { "mixed", SND_NONE },
    };
    if (!parseChoice(value, choices, out)) {
        return false;
    }
    if (*out == SND_NONE) {
        *out = SND_STEREO;
        prefs->sound_mixed_stereo_delay = 5;
        prefs->sound_stereo_separation = 7;
    }
    return true;
}

static bool parseLinemode(const QString &value, int *out)
{
    static const Choice choices[] = {
        { "none", 0 },
        { "double", 1 },
        { "scanlines", 2 },
        { "scanlines2p", 3 },
        { "scanlines3p", 4 },
        { "double2", 5 },
        { "scanlines2", 6 },
        { "scanlines2p2", 7 },
        { "scanlines2p3", 8 },
        { "double3", 9 },
        { "scanlines3", 10 },
        { "scanlines3p2", 11 },
        { "scanlines3p3", 12 },
    };
    return parseChoice(value, choices, out);
}

static bool parseFullscreenMode(const QString &value, int *out)
{
    static const Choice choices[] = {
        { "false", GFX_WINDOW },
        { "true", GFX_FULLSCREEN },
        { "fullwindow", GFX_FULLWINDOW },
    };
    return parseChoice(value, choices, out);
}

static bool parseVsyncMode(const QString &value, int *out)
{
    static const Choice choices[] = {
        { "false", 0 },
        { "true", 1 },
        { "autoswitch", 2 },
    };
    if (parseChoice(value, choices, out)) {
        return true;
    }
    bool enabled = false;
    if (textToBool(value, &enabled)) {
        *out = enabled ? 1 : 0;
        return true;
    }
    return false;
}

static void parseSettingOption(struct uae_prefs *prefs, const QString &key, const QString &value)
{
    if (value.isEmpty() || key.startsWith(QStringLiteral("unix.ui."))) {
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

static ApplySettingResult applyTypedSetting(const WinUaeQtConfig::Settings &settings,
    const WinUaeQtConfig::Setting &setting, struct uae_prefs *prefs)
{
    auto applyInt = [&](const char *key, int *field, int multiplier = 1) -> bool {
        if (setting.key != QString::fromLatin1(key)) {
            return false;
        }
        int parsed = 0;
        if (textToInt(setting.value, &parsed)) {
            *field = parsed * multiplier;
        }
        return true;
    };
    auto applyBool = [&](const char *key, bool *field) -> bool {
        if (setting.key != QString::fromLatin1(key)) {
            return false;
        }
        bool parsed = false;
        if (textToBool(setting.value, &parsed)) {
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
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("kickstart_ext_rom_file")) {
        copyTextSetting(prefs->romextfile, sizeof prefs->romextfile / sizeof(TCHAR), setting.value);
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("kickstart_ext_rom_file2")) {
        copyTextSetting(prefs->romextfile2, sizeof prefs->romextfile2 / sizeof(TCHAR), setting.value);
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("flash_file")) {
        copyTextSetting(prefs->flashfile, sizeof prefs->flashfile / sizeof(TCHAR), setting.value);
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("cart_file")) {
        copyTextSetting(prefs->cartfile, sizeof prefs->cartfile / sizeof(TCHAR), setting.value);
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
        const QString key = QStringLiteral("floppy%1type").arg(drive);
        if (setting.key == key) {
            int value = 0;
            if (textToInt(setting.value, &value)) {
                prefs->floppyslots[drive].dfxtype = value;
            }
            return ApplySettingResult::Handled;
        }
    }
    for (int drive = 0; drive < 4; drive++) {
        const QString key = QStringLiteral("floppy%1wp").arg(drive);
        if (setting.key != key) {
            continue;
        }
        bool value = false;
        if (textToBool(setting.value, &value)) {
            prefs->floppyslots[drive].forcedwriteprotect = value;
        }
        return ApplySettingResult::Handled;
    }

    for (int drive = 0; drive < 4; drive++) {
        const QString key = QStringLiteral("floppy%1sound").arg(drive);
        if (setting.key == key) {
            if (textToInt(setting.value, &value)) {
                prefs->floppyslots[drive].dfxclick = value;
            }
            return ApplySettingResult::Handled;
        }
    }
    for (int drive = 0; drive < 4; drive++) {
        const QString key = QStringLiteral("floppy%1soundvolume_empty").arg(drive);
        if (setting.key == key) {
            if (textToInt(setting.value, &value)) {
                prefs->dfxclickvolume_empty[drive] = value;
            }
            return ApplySettingResult::Handled;
        }
    }
    for (int drive = 0; drive < 4; drive++) {
        const QString key = QStringLiteral("floppy%1soundvolume_disk").arg(drive);
        if (setting.key == key) {
            if (textToInt(setting.value, &value)) {
                prefs->dfxclickvolume_disk[drive] = value;
            }
            return ApplySettingResult::Handled;
        }
    }

    if (setting.key == QStringLiteral("nr_floppies")) {
        if (textToInt(setting.value, &value)) {
            prefs->nr_floppies = value;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("floppy_speed")) {
        if (textToInt(setting.value, &value)) {
            prefs->floppy_speed = value;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("maprom")) {
        bool ok = false;
        const uint parsed = setting.value.toUInt(&ok, 0);
        if (ok) {
            prefs->maprom = parsed;
        }
        return ApplySettingResult::Handled;
    }
    if (applyBool("kickshifter", &prefs->kickshifter)
        || applyBool("ntsc", &prefs->ntscmode)
        || applyBool("immediate_blits", &prefs->immediate_blits)
        || applyBool("genlock", &prefs->genlock)
        || applyBool("genlock_alpha", &prefs->genlock_alpha)
        || applyBool("keyboard_nkro", &prefs->keyboard_nkro)
        || applyBool("df0idhw", &prefs->cs_df0idhw)
        || applyBool("sana2", &prefs->sana2)
        || applyBool("bsdsocket_emu", &prefs->socket_emu)
        || applyBool("uaeserial", &prefs->uaeserial)
        || applyBool("sampler_stereo", &prefs->sampler_stereo)
        || applyBool("sound_auto", &prefs->sound_auto)
        || applyBool("sound_stereo_swap_paula", &prefs->sound_stereo_swap_paula)
        || applyBool("sound_stereo_swap_ahi", &prefs->sound_stereo_swap_ahi)
        || applyBool("serial_on_demand", &prefs->serial_demand)
        || applyBool("serial_hardware_ctsrts", &prefs->serial_hwctsrts)
        || applyBool("serial_status", &prefs->serial_rtsctsdtrdtecd)
        || applyBool("serial_ri", &prefs->serial_ri)
        || applyBool("serial_direct", &prefs->serial_direct)
        || applyBool("parallel_postscript_detection", &prefs->parallel_postscript_detection)
        || applyBool("parallel_postscript_emulation", &prefs->parallel_postscript_emulation)
        || applyBool("state_replay_autoplay", &prefs->inprec_autoplay)
        || applyBool("gfx_resize_windowed", &prefs->gfx_windowed_resize)
        || applyBool("gfx_flickerfixer", &prefs->gfx_scandoubler)
        || applyBool("gfx_blacker_than_black", &prefs->gfx_blackerthanblack)
        || applyBool("gfx_monochrome", &prefs->gfx_grayscale)
        || applyBool("gfx_autoresolution_vga", &prefs->gfx_autoresolution_vga)
        || applyBool("gfx_keep_aspect", &prefs->gfx_keep_aspect)
        || applyBool("gfx_ntscpixels", &prefs->gfx_ntscpixels)
        || applyBool("tablet_library", &prefs->tablet_library)) {
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("genlock_aspect")) {
        if (textToBool(setting.value, &boolValue)) {
            prefs->genlock_aspect = boolValue ? 1 : 0;
        }
        return ApplySettingResult::Handled;
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
    if (applyInt("genlock_mix", &prefs->genlock_mix)
        || applyInt("chipset_rtc_adjust", &prefs->cs_rtc_adjust)
        || applyInt("fatgary", &prefs->cs_fatgaryrev)
        || applyInt("ramsey", &prefs->cs_ramseyrev)
        || applyInt("sound_max_buff", &prefs->sound_maxbsiz)
        || applyInt("sound_frequency", &prefs->sound_freq)
        || applyInt("sound_stereo_separation", &prefs->sound_stereo_separation)
        || applyInt("sound_stereo_mixing_delay", &prefs->sound_mixed_stereo_delay)
        || applyInt("parallel_autoflush", &prefs->parallel_autoflush_time)
        || applyInt("midiout_device", &prefs->win32_midioutdev)
        || applyInt("midi_device", &prefs->win32_midioutdev)
        || applyInt("midiin_device", &prefs->win32_midiindev)
        || applyInt("unix.soundcard", &prefs->win32_soundcard)
        || applyInt("unix.samplersoundcard", &prefs->win32_samplersoundcard)
        || applyInt("samplersoundcard", &prefs->win32_samplersoundcard)
        || applyInt("state_replay_rate", &prefs->statecapturerate)
        || applyInt("state_replay_buffers", &prefs->statecapturebuffersize)
        || applyInt("gfx_display", &prefs->gfx_apmode[APMODE_NATIVE].gfx_display)
        || applyInt("gfx_display_rtg", &prefs->gfx_apmode[APMODE_RTG].gfx_display)
        || applyInt("gfx_refreshrate", &prefs->gfx_apmode[APMODE_NATIVE].gfx_refreshrate)
        || applyInt("gfx_refreshrate_rtg", &prefs->gfx_apmode[APMODE_RTG].gfx_refreshrate)
        || applyInt("gfx_backbuffers", &prefs->gfx_apmode[APMODE_NATIVE].gfx_backbuffers)
        || applyInt("gfx_backbuffers_rtg", &prefs->gfx_apmode[APMODE_RTG].gfx_backbuffers)
        || applyInt("gfx_frame_slices", &prefs->gfx_display_sections)
        || applyInt("gfx_framerate", &prefs->gfx_framerate)
        || applyInt("gfx_autoresolution", &prefs->gfx_autoresolution)
        || applyInt("gfx_monitorblankdelay", &prefs->gfx_monitorblankdelay)
        || applyInt("rtg_modes", &prefs->picasso96_modeflags)
        || applyInt("cd_speed", &prefs->cd_speed)
        || applyInt("filesys_max_size", &prefs->filesys_limit)) {
        if (setting.key == QStringLiteral("gfx_display")) {
            prefs->gfx_apmode[APMODE_RTG].gfx_display = prefs->gfx_apmode[APMODE_NATIVE].gfx_display;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("cpuboardmem1_size") ||
        setting.key == QStringLiteral("cpuboardmem2_size")) {
        if (textToInt(setting.value, &value)) {
            if (setting.key == QStringLiteral("cpuboardmem1_size")) {
                prefs->cpuboardmem1.size = value * 0x100000;
            } else {
                prefs->cpuboardmem2.size = value * 0x100000;
            }
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("cpu_model")) {
        if (textToInt(setting.value, &value)) {
            prefs->cpu_model = value;
            prefs->fpu_model = 0;
            applyCycleExactConstraint(prefs);
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("fpu_model")) {
        if (textToInt(setting.value, &value)) {
            prefs->fpu_model = value;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("cpu_compatible")) {
        if (textToBool(setting.value, &boolValue)) {
            prefs->cpu_compatible = boolValue;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("cpu_24bit_addressing")) {
        if (textToBool(setting.value, &boolValue)) {
            prefs->address_space_24 = boolValue;
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
    if (setting.key == QStringLiteral("chipmem_size")) {
        if (textToInt(setting.value, &value)) {
            if (value < 0) {
                prefs->chipmem.size = 0x20000;
            } else if (value == 0) {
                prefs->chipmem.size = 0x40000;
            } else {
                prefs->chipmem.size = value * 0x80000;
            }
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("fastmem_size")) {
        if (textToInt(setting.value, &value)) {
            prefs->fastmem[0].size = value * 0x100000;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("bogomem_size")) {
        if (textToInt(setting.value, &value)) {
            prefs->bogomem.size = value * 0x40000;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("z3mem_size")) {
        if (textToInt(setting.value, &value)) {
            prefs->z3fastmem[0].size = value * 0x100000;
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
    if (setting.key == QStringLiteral("compfpu")) {
        if (textToBool(setting.value, &boolValue)) {
            prefs->compfpu = boolValue;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("comp_constjump")) {
        if (textToBool(setting.value, &boolValue)) {
            prefs->comp_constjump = boolValue;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("comp_nf")) {
        if (textToBool(setting.value, &boolValue)) {
            prefs->compnf = boolValue;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("comp_catchfault")) {
        if (textToBool(setting.value, &boolValue)) {
            prefs->comp_catchfault = boolValue;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("comp_flushmode")) {
        if (setting.value.compare(QStringLiteral("hard"), Qt::CaseInsensitive) == 0) {
            prefs->comp_hardflush = true;
        } else if (setting.value.compare(QStringLiteral("soft"), Qt::CaseInsensitive) == 0) {
            prefs->comp_hardflush = false;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("comp_trustbyte")
        || setting.key == QStringLiteral("comp_trustword")
        || setting.key == QStringLiteral("comp_trustlong")
        || setting.key == QStringLiteral("comp_trustnaddr")) {
        static const Choice choices[] = {
            { "direct", 0 },
            { "indirect", 1 },
            { "indirectKS", 2 },
            { "afterPic", 3 },
        };
        if (parseChoice(setting.value, choices, &value)) {
            if (setting.key == QStringLiteral("comp_trustbyte")) {
                prefs->comptrustbyte = value;
            } else if (setting.key == QStringLiteral("comp_trustword")) {
                prefs->comptrustword = value;
            } else if (setting.key == QStringLiteral("comp_trustlong")) {
                prefs->comptrustlong = value;
            } else {
                prefs->comptrustnaddr = value;
            }
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("sound_output")) {
        if (parseSoundOutput(setting.value, &value)) {
            prefs->produce_sound = value;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("sound_channels")) {
        if (parseSoundChannels(setting.value, &value, prefs)) {
            prefs->sound_stereo = value;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("sound_interpol")) {
        static const Choice choices[] = {
            { "none", 0 },
            { "anti", 1 },
            { "sinc", 2 },
            { "rh", 3 },
            { "crux", 4 },
        };
        if (parseChoice(setting.value, choices, &value)) {
            prefs->sound_interpol = value;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("sound_filter")) {
        static const Choice choices[] = {
            { "off", 0 },
            { "emulated", 1 },
            { "on", 2 },
            { "fixedonly", 3 },
        };
        if (parseChoice(setting.value, choices, &value)) {
            prefs->sound_filter = value;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("sound_filter_type")) {
        static const Choice choices[] = {
            { "standard", 0 },
            { "enhanced", 1 },
        };
        if (parseChoice(setting.value, choices, &value)) {
            prefs->sound_filter_type = value;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("sound_volume")) {
        if (textToInt(setting.value, &value)) {
            prefs->sound_volume_master = value;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("sound_volume_paula")) {
        if (textToInt(setting.value, &value)) {
            prefs->sound_volume_paula = value;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("sound_volume_cd")) {
        if (textToInt(setting.value, &value)) {
            prefs->sound_volume_cd = value;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("sound_volume_ahi")) {
        if (textToInt(setting.value, &value)) {
            prefs->sound_volume_board = value;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("sound_volume_midi")) {
        if (textToInt(setting.value, &value)) {
            prefs->sound_volume_midi = value;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("sound_volume_genlock")) {
        if (textToInt(setting.value, &value)) {
            prefs->sound_volume_genlock = value;
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
    if (setting.key == QStringLiteral("serial_translate")) {
        static const Choice choices[] = {
            { "disabled", 0 },
            { "crlf_cr", 1 },
        };
        if (parseChoice(setting.value, choices, &value)) {
            prefs->serial_crlf = value;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("parallel_matrix_emulation")) {
        static const Choice choices[] = {
            { "none", 0 },
            { "ascii", 1 },
            { "epson_matrix_9pin", 2 },
            { "epson_matrix_24pin", 3 },
            { "epson_matrix_48pin", 4 },
        };
        if (parseChoice(setting.value, choices, &value)) {
            prefs->parallel_matrix_emulation = value;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("gfxcard_size")) {
        if (textToInt(setting.value, &value)) {
            prefs->rtgboards[0].rtgmem_size = value * 0x100000;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("gfxcard_type")) {
        if (parseGfxCardType(setting.value, &value)) {
            prefs->rtgboards[0].rtgmem_type = value;
        }
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
    if (setting.key == QStringLiteral("gfx_fullscreen_amiga")) {
        if (parseFullscreenMode(setting.value, &value)) {
            prefs->gfx_apmode[APMODE_NATIVE].gfx_fullscreen = value;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("gfx_fullscreen_picasso")) {
        if (parseFullscreenMode(setting.value, &value)) {
            prefs->gfx_apmode[APMODE_RTG].gfx_fullscreen = value;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("gfx_vsync")) {
        if (parseVsyncMode(setting.value, &value)) {
            prefs->gfx_apmode[APMODE_NATIVE].gfx_vsync = value;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("gfx_vsync_picasso")) {
        if (parseVsyncMode(setting.value, &value)) {
            prefs->gfx_apmode[APMODE_RTG].gfx_vsync = value;
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("gfx_vsyncmode") ||
        setting.key == QStringLiteral("gfx_vsyncmode_picasso")) {
        static const Choice choices[] = {
            { "normal", 0 },
            { "busywait", 1 },
        };
        if (parseChoice(setting.value, choices, &value)) {
            if (setting.key == QStringLiteral("gfx_vsyncmode")) {
                prefs->gfx_apmode[APMODE_NATIVE].gfx_vsyncmode = value;
            } else {
                prefs->gfx_apmode[APMODE_RTG].gfx_vsyncmode = value;
            }
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("gfx_linemode")) {
        if (parseLinemode(setting.value, &value)) {
            prefs->gfx_vresolution = VRES_NONDOUBLE;
            prefs->gfx_pscanlines = 0;
            prefs->gfx_iscanlines = 0;
            if (value > 0) {
                prefs->gfx_iscanlines = (value - 1) / 4;
                prefs->gfx_pscanlines = (value - 1) % 4;
                prefs->gfx_vresolution = VRES_DOUBLE;
            }
        }
        return ApplySettingResult::Handled;
    }
    if (setting.key == QStringLiteral("gfx_center_horizontal") ||
        setting.key == QStringLiteral("gfx_center_vertical")) {
        static const Choice choices[] = {
            { "none", 0 },
            { "simple", 1 },
            { "smart", 2 },
            { "false", 0 },
            { "true", 1 },
        };
        if (parseChoice(setting.value, choices, &value)) {
            if (setting.key == QStringLiteral("gfx_center_horizontal")) {
                prefs->gfx_xcenter = value;
            } else {
                prefs->gfx_ycenter = value;
            }
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

bool applyWinUaeQtConfigToPrefs(const WinUaeQtConfig &config, struct uae_prefs *prefs)
{
    if (!prefs) {
        return false;
    }

    const WinUaeQtConfig::Settings &settings = config.settings();
    for (const WinUaeQtConfig::Setting &setting : config.orderedSettings()) {
        if (applyTypedSetting(settings, setting, prefs) == ApplySettingResult::Handled) {
            continue;
        }
        parseSettingOption(prefs, setting.key, setting.value);
    }
    return true;
}
