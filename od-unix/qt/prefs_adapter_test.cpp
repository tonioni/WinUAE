#include "config.h"
#include "prefs_adapter.h"

#include <QDebug>
#include <QStringList>

#include <cstdlib>
#include <cstring>
#include <memory>

#include "sysconfig.h"
#include "sysdeps.h"
#include "custom.h"
#include "options.h"

static QStringList parsedLines;
static QStringList resolvedRoms;
static int resets;

void default_prefs(struct uae_prefs *prefs, bool, int)
{
    memset(prefs, 0, sizeof(*prefs));
    resets++;
}

bool unix_resolve_rom_path(struct uae_prefs *, TCHAR *path, int)
{
    resolvedRoms.append(QString::fromLocal8Bit(path));
    return false;
}

// This test checks the adapter boundary, not a second implementation of the
// core parser. Runtime config tests exercise the resulting core preferences.
int cfgfile_parse_option(struct uae_prefs *, const TCHAR *key, TCHAR *value, int)
{
    parsedLines.append(QStringLiteral("%1=%2")
        .arg(QString::fromLocal8Bit(key), QString::fromLocal8Bit(value)));
    // The core may modify its value buffer. It must not modify the Qt config.
    value[0] = 0;
    return 1;
}

static bool require(bool condition, const char *message)
{
    if (!condition) {
        qWarning().noquote() << message;
    }
    return condition;
}

using Prefs = std::unique_ptr<uae_prefs>;

static bool testCoreDelegation()
{
    const QString text = QStringLiteral(
        "fpu_model=68882\n"
        "chipmem_size=4\n"
        "fastmem_size=8\n"
        "cpuboardmem1_size=16\n"
        "floppy2type=-1\n"
        "floppy3wp=t\n"
        "sound_output=normal\n"
        "sound_channels=mixed\n"
        "sound_stereo_separation=8\n"
        "sound_stereo_mixing_delay=2\n"
        "sound_filter=emulated\n"
        "floppy0soundvolume_disk=22\n"
        "gfxcard_type=PicassoIV_Z3\n"
        "gfxcard_size=16\n"
        "gfx_linemode=scanlines2p\n"
        "gfx_vsync=autoswitch\n"
        "gfx_keep_aspect=true\n"
        "bsdsocket_emu=yes\n"
        "serial_translate=crlf_cr\n"
        "parallel_matrix_emulation=ascii\n"
        "comp_trustbyte=indirectKS\n"
        "rtg_modes=0x10\n"
        "unix.screenshot_path=/screenshots\n"
        "unix.ui.recursive_roms=true\n"
        "future_core_setting=value\n");
    WinUaeQtConfig config;
    bool ok = require(config.loadFromText(text), "load delegated settings");
    Prefs prefs(new uae_prefs{});
    parsedLines.clear();
    ok = require(applyWinUaeQtConfigToPrefs(config, prefs.get()), "apply delegated settings") && ok;
    ok = require(parsedLines == text.trimmed().split(QLatin1Char('\n')),
        "core receives every setting in document order") && ok;
    ok = require(config.value(QStringLiteral("sound_output")) == QStringLiteral("normal"),
        "core value mutation changed the Qt config") && ok;
    return ok;
}

static bool testLauncherPathsAndDevices()
{
    WinUaeQtConfig config;
    config.loadFromText(QStringLiteral(
        "unix.ui.config_path=/configs\n"
        "unix.ui.gui_font=Test\n"
        "kickstart_rom_file=kick.rom\n"
        "kickstart_ext_rom_file=ext.rom\n"
        "kickstart_ext_rom_file2=ext2.rom\n"
        "cart_file=cart.rom\n"
        "floppy0=/disks/install.adf\n"
        "statefile=/states/test.uss\n"
        "statefile_path=/states\n"
        "unix.serial_port=none\n"
        "unix.soundcard=7\n"
        "unix.samplersoundcard=3\n"
        "midiout_device=4\n"
        "midiin_device=2\n"
        "uaescsimode=SPTI\n"
        "unix.rtg_vblank=default\n"));
    Prefs prefs(new uae_prefs{});
    parsedLines.clear();
    resolvedRoms.clear();
    bool ok = require(applyWinUaeQtConfigToPrefs(config, prefs.get()), "apply launcher settings");
    ok = require(parsedLines.isEmpty(), "launcher-only settings reached the core") && ok;
    ok = require(resolvedRoms == QStringList({QStringLiteral("kick.rom"), QStringLiteral("ext.rom"),
        QStringLiteral("ext2.rom"), QStringLiteral("cart.rom")}), "ROM paths were not resolved") && ok;
    ok = require(!strcmp(prefs->floppyslots[0].df, "/disks/install.adf"), "floppy path") && ok;
    ok = require(!strcmp(prefs->statefile, "/states/test.uss"), "preview statefile") && ok;
    ok = require(!strcmp(prefs->statefile_path, "/states"), "preview statefile path") && ok;
    ok = require(!prefs->use_serial && !prefs->sername[0], "serial none") && ok;
    ok = require(prefs->win32_soundcard == 7 && prefs->win32_samplersoundcard == 3,
        "preview must preserve device selection before enumeration") && ok;
    ok = require(prefs->win32_midioutdev == 4 && prefs->win32_midiindev == 2, "MIDI selection") && ok;
    ok = require(prefs->win32_uaescsimode == UAESCSI_SPTI && prefs->win32_rtgvblankrate == -1,
        "launcher host aliases") && ok;
    return ok;
}

static bool testPairedSettings()
{
    WinUaeQtConfig config;
    config.loadFromText(QStringLiteral(
        "gfx_width_windowed=native\n"
        "gfx_height_windowed=900\n"
        "floppy0soundext=ExternalSet\n"
        "floppy0sound=-1\n"
        "floppy1soundext=StaleSet\n"
        "floppy1sound=1\n"));
    Prefs prefs(new uae_prefs{});
    prefs->gfx_monitor[0].gfx_size_win.width = 123;
    prefs->gfx_monitor[0].gfx_size_win.height = 456;
    strcpy(prefs->floppyslots[1].dfxclickexternal, "OldSet");
    bool ok = require(applyWinUaeQtConfigToPrefs(config, prefs.get()), "apply paired settings");
    ok = require(prefs->gfx_monitor[0].gfx_size_win.width == 0
        && prefs->gfx_monitor[0].gfx_size_win.height == 0, "native size applies to both dimensions") && ok;
    ok = require(prefs->floppyslots[0].dfxclick == -1
        && !strcmp(prefs->floppyslots[0].dfxclickexternal, "ExternalSet"), "external sound selection") && ok;
    ok = require(prefs->floppyslots[1].dfxclick == 1
        && !prefs->floppyslots[1].dfxclickexternal[0], "internal sound clears stale sample name") && ok;
    return ok;
}

static bool testPreviewAndReset()
{
    Prefs prefs(new uae_prefs{});
    strcpy(prefs->floppyslots[0].df, "old.adf");
    strcpy(prefs->romextfile, "old.rom");
    prefs->mountitems = 2;
    resets = 0;
    bool ok = require(applyWinUaeQtConfigToPrefs(WinUaeQtConfig(), prefs.get(), false), "preview");
    ok = require(!resets && prefs->floppyslots[0].df[0] && prefs->romextfile[0]
        && prefs->mountitems == 2, "preview should preserve omitted fields") && ok;
    ok = require(applyWinUaeQtConfigToPrefs(WinUaeQtConfig(), prefs.get(), true), "reset") && ok;
    ok = require(resets == 1 && !prefs->floppyslots[0].df[0] && !prefs->romextfile[0]
        && !prefs->mountitems, "reset should clear removed media") && ok;
    return ok;
}

static bool testCpuConstraints()
{
    WinUaeQtConfig config;
    config.loadFromText(QStringLiteral(
        "cpu_cycle_exact=true\n"
        "cpu_model=68020\n"
        "cachesize=8192\n"
        "cpu_memory_cycle_exact=true\n"
        "blitter_cycle_exact=true\n"));
    Prefs prefs(new uae_prefs{});
    bool ok = require(applyWinUaeQtConfigToPrefs(config, prefs.get()), "apply CPU settings");
    ok = require(prefs->cpu_model == 68020 && prefs->cachesize == 8192,
        "CPU and JIT selection") && ok;
    ok = require(!prefs->cpu_cycle_exact && !prefs->cpu_memory_cycle_exact && !prefs->blitter_cycle_exact,
        "JIT must retain the launcher cycle-exact constraint") && ok;
    return ok;
}

static bool testRepeatedMountDelegation()
{
    WinUaeQtConfig config;
    config.applyRepeatedSettings({
        { QStringLiteral("filesystem2"), QStringLiteral("rw,DH0:System:/tmp/System,0") },
        { QStringLiteral("filesystem2"), QStringLiteral("ro,DH1:Work:/tmp/Work,5") }
    }, { QStringLiteral("filesystem2") });
    Prefs prefs(new uae_prefs{});
    parsedLines.clear();
    bool ok = require(applyWinUaeQtConfigToPrefs(config, prefs.get()), "apply repeated mounts");
    ok = require(parsedLines == QStringList({
        QStringLiteral("filesystem2=rw,DH0:System:/tmp/System,0"),
        QStringLiteral("filesystem2=ro,DH1:Work:/tmp/Work,5") }), "preserve repeated mount order") && ok;
    return ok;
}

int main()
{
    bool ok = require(!applyWinUaeQtConfigToPrefs(WinUaeQtConfig(), nullptr), "reject null prefs");
    ok = testCoreDelegation() && ok;
    ok = testLauncherPathsAndDevices() && ok;
    ok = testPairedSettings() && ok;
    ok = testPreviewAndReset() && ok;
    ok = testCpuConstraints() && ok;
    ok = testRepeatedMountDelegation() && ok;
    return ok ? 0 : 1;
}
