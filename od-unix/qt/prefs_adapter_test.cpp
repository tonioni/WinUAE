#include "config.h"
#include "prefs_adapter.h"

#include <QDebug>
#include <QStringList>

#include <cstdlib>
#include <cstring>

#include "sysconfig.h"
#include "sysdeps.h"
#include "audio.h"
#include "custom.h"
#include "disk.h"
#include "gfxboard.h"
#include "options.h"

static QStringList parsedLines;

int gfxboard_get_id_from_index(int index)
{
    static const int ids[] = {
        GFXBOARD_UAE_Z2,
        GFXBOARD_UAE_Z3,
        GFXBOARD_ID_PICASSO4_Z3,
        -1
    };
    return index >= 0 && index < int(sizeof(ids) / sizeof(ids[0])) ? ids[index] : -1;
}

const TCHAR *gfxboard_get_configname(int id)
{
    switch (id) {
        case GFXBOARD_UAE_Z2:
            return _T("ZorroII");
        case GFXBOARD_UAE_Z3:
            return _T("ZorroIII");
        case GFXBOARD_ID_PICASSO4_Z3:
            return _T("PicassoIV_Z3");
        default:
            return nullptr;
    }
}

static bool configBoolValue(const char *value)
{
    return !strcmp(value, "true") || !strcmp(value, "yes") || !strcmp(value, "1");
}

static void copyText(TCHAR *dst, size_t dstSize, const char *src)
{
    if (!dst || !dstSize) {
        return;
    }
    snprintf(dst, dstSize, "%s", src ? src : "");
}

int cfgfile_parse_option(struct uae_prefs *prefs, const TCHAR *key, TCHAR *value, int)
{
    parsedLines.append(QStringLiteral("%1=%2")
        .arg(QString::fromLocal8Bit(key), QString::fromLocal8Bit(value)));

    if (!strcmp(key, "kickstart_rom_file")) {
        copyText(prefs->romfile, sizeof prefs->romfile, value);
    } else if (!strcmp(key, "kickstart_ext_rom_file")) {
        copyText(prefs->romextfile, sizeof prefs->romextfile, value);
    } else if (!strcmp(key, "floppy0")) {
        copyText(prefs->floppyslots[0].df, sizeof prefs->floppyslots[0].df, value);
    } else if (!strcmp(key, "floppy1")) {
        copyText(prefs->floppyslots[1].df, sizeof prefs->floppyslots[1].df, value);
    } else if (!strcmp(key, "floppy0type")) {
        prefs->floppyslots[0].dfxtype = atoi(value);
    } else if (!strcmp(key, "floppy1type")) {
        prefs->floppyslots[1].dfxtype = atoi(value);
    } else if (!strcmp(key, "floppy2type")) {
        prefs->floppyslots[2].dfxtype = atoi(value);
    } else if (!strcmp(key, "floppy0wp")) {
        prefs->floppyslots[0].forcedwriteprotect = configBoolValue(value);
    } else if (!strcmp(key, "floppy1wp")) {
        prefs->floppyslots[1].forcedwriteprotect = configBoolValue(value);
    } else if (!strcmp(key, "floppy2wp")) {
        prefs->floppyslots[2].forcedwriteprotect = configBoolValue(value);
    } else if (!strcmp(key, "chipset")) {
        prefs->chipset_mask = !strcmp(value, "aga") ? 0x0707 : 0x0101;
    } else if (!strcmp(key, "chipset_compatible")) {
        prefs->cs_compatible = !strcmp(value, "A1200") ? CP_A1200 : CP_GENERIC;
    } else if (!strcmp(key, "cpu_model")) {
        prefs->cpu_model = atoi(value);
        prefs->fpu_model = 0;
    } else if (!strcmp(key, "fpu_model")) {
        prefs->fpu_model = atoi(value);
    } else if (!strcmp(key, "sound_output")) {
        prefs->produce_sound = !strcmp(value, "normal") ? 2 : 0;
    } else if (!strcmp(key, "gfxcard_type")) {
        prefs->rtgboards[0].rtg_index = 0;
        prefs->rtgboards[0].rtgmem_type = !strcmp(value, "ZorroIII") ? 1 : 0;
    }
    return 1;
}

static bool require(bool condition, const char *message)
{
    if (!condition) {
        qWarning().noquote() << message;
    }
    return condition;
}

static bool requireInt(int actual, int expected, const char *field)
{
    if (actual == expected) {
        return true;
    }
    qWarning().noquote() << field << "expected" << expected << "got" << actual;
    return false;
}

static bool requireUnsigned(uae_u32 actual, uae_u32 expected, const char *field)
{
    if (actual == expected) {
        return true;
    }
    qWarning().noquote() << field << "expected" << (qulonglong)expected << "got" << (qulonglong)actual;
    return false;
}

static bool requireText(const TCHAR *actual, const char *expected, const char *field)
{
    if (!strcmp(actual, expected)) {
        return true;
    }
    qWarning().noquote() << field << "expected" << expected << "got" << actual;
    return false;
}

static struct uae_prefs *allocPrefs()
{
    return static_cast<struct uae_prefs *>(calloc(1, sizeof(struct uae_prefs)));
}

static bool testRepresentativeConfig()
{
    WinUaeQtConfig::Settings settings;
    settings.insert(QStringLiteral("kickstart_rom_file"), QStringLiteral("/roms/A1200.rom"));
    settings.insert(QStringLiteral("kickstart_ext_rom_file"), QStringLiteral("/roms/ext.rom"));
    settings.insert(QStringLiteral("floppy0"), QStringLiteral("/disks/install.adf"));
    settings.insert(QStringLiteral("floppy1"), QStringLiteral("/disks/extras.adf"));
    settings.insert(QStringLiteral("floppy0type"), QStringLiteral("0"));
    settings.insert(QStringLiteral("floppy1type"), QStringLiteral("1"));
    settings.insert(QStringLiteral("floppy2type"), QStringLiteral("-1"));
    settings.insert(QStringLiteral("floppy0wp"), QStringLiteral("true"));
    settings.insert(QStringLiteral("floppy1wp"), QStringLiteral("false"));
    settings.insert(QStringLiteral("floppy2wp"), QStringLiteral("yes"));
    settings.insert(QStringLiteral("floppy3wp"), QStringLiteral("t"));
    settings.insert(QStringLiteral("uaehf0"), QStringLiteral("dir,rw,DH0:System:/tmp/System,0"));
    settings.insert(QStringLiteral("nr_floppies"), QStringLiteral("2"));
    settings.insert(QStringLiteral("chipset"), QStringLiteral("aga"));
    settings.insert(QStringLiteral("chipset_compatible"), QStringLiteral("A1200"));
    settings.insert(QStringLiteral("cpu_model"), QStringLiteral("68020"));
    settings.insert(QStringLiteral("fpu_model"), QStringLiteral("68882"));
    settings.insert(QStringLiteral("cpu_24bit_addressing"), QStringLiteral("false"));
    settings.insert(QStringLiteral("chipmem_size"), QStringLiteral("4"));
    settings.insert(QStringLiteral("fastmem_size"), QStringLiteral("8"));
    settings.insert(QStringLiteral("bogomem_size"), QStringLiteral("2"));
    settings.insert(QStringLiteral("z3mem_size"), QStringLiteral("64"));
    settings.insert(QStringLiteral("cachesize"), QStringLiteral("8"));
    settings.insert(QStringLiteral("sound_output"), QStringLiteral("normal"));
    settings.insert(QStringLiteral("sound_auto"), QStringLiteral("true"));
    settings.insert(QStringLiteral("sound_channels"), QStringLiteral("mixed"));
    settings.insert(QStringLiteral("sound_frequency"), QStringLiteral("44100"));
    settings.insert(QStringLiteral("sound_max_buff"), QStringLiteral("4096"));
    settings.insert(QStringLiteral("sound_interpol"), QStringLiteral("sinc"));
    settings.insert(QStringLiteral("sound_filter"), QStringLiteral("emulated"));
    settings.insert(QStringLiteral("sound_filter_type"), QStringLiteral("enhanced"));
    settings.insert(QStringLiteral("sound_stereo_separation"), QStringLiteral("8"));
    settings.insert(QStringLiteral("sound_stereo_mixing_delay"), QStringLiteral("2"));
    settings.insert(QStringLiteral("sound_stereo_swap_paula"), QStringLiteral("true"));
    settings.insert(QStringLiteral("floppy0sound"), QStringLiteral("1"));
    settings.insert(QStringLiteral("floppy0soundvolume_empty"), QStringLiteral("34"));
    settings.insert(QStringLiteral("floppy0soundvolume_disk"), QStringLiteral("22"));
    settings.insert(QStringLiteral("df0idhw"), QStringLiteral("false"));
    settings.insert(QStringLiteral("gfxcard_size"), QStringLiteral("16"));
    settings.insert(QStringLiteral("gfxcard_type"), QStringLiteral("PicassoIV_Z3"));
    settings.insert(QStringLiteral("gfx_width_windowed"), QStringLiteral("800"));
    settings.insert(QStringLiteral("gfx_height_windowed"), QStringLiteral("600"));
    settings.insert(QStringLiteral("gfx_width_fullscreen"), QStringLiteral("native"));
    settings.insert(QStringLiteral("gfx_height_fullscreen"), QStringLiteral("native"));
    settings.insert(QStringLiteral("gfx_fullscreen_amiga"), QStringLiteral("fullwindow"));
    settings.insert(QStringLiteral("gfx_fullscreen_picasso"), QStringLiteral("true"));
    settings.insert(QStringLiteral("gfx_vsync"), QStringLiteral("autoswitch"));
    settings.insert(QStringLiteral("gfx_vsyncmode"), QStringLiteral("busywait"));
    settings.insert(QStringLiteral("gfx_vsync_picasso"), QStringLiteral("true"));
    settings.insert(QStringLiteral("gfx_vsyncmode_picasso"), QStringLiteral("normal"));
    settings.insert(QStringLiteral("gfx_backbuffers"), QStringLiteral("3"));
    settings.insert(QStringLiteral("gfx_resize_windowed"), QStringLiteral("true"));
    settings.insert(QStringLiteral("gfx_framerate"), QStringLiteral("2"));
    settings.insert(QStringLiteral("gfx_linemode"), QStringLiteral("scanlines2p"));
    settings.insert(QStringLiteral("gfx_center_horizontal"), QStringLiteral("simple"));
    settings.insert(QStringLiteral("gfx_center_vertical"), QStringLiteral("smart"));
    settings.insert(QStringLiteral("gfx_keep_aspect"), QStringLiteral("true"));
    settings.insert(QStringLiteral("rtg_modes"), QStringLiteral("0x10"));
    settings.insert(QStringLiteral("bsdsocket_emu"), QStringLiteral("true"));
    settings.insert(QStringLiteral("sana2"), QStringLiteral("true"));
    settings.insert(QStringLiteral("uaescsimode"), QStringLiteral("SPTI"));
    settings.insert(QStringLiteral("unix.rtg_vblank"), QStringLiteral("real"));
    settings.insert(QStringLiteral("unix.serial_port"), QStringLiteral("TCP://0.0.0.0:1234"));
    settings.insert(QStringLiteral("serial_on_demand"), QStringLiteral("true"));
    settings.insert(QStringLiteral("serial_hardware_ctsrts"), QStringLiteral("true"));
    settings.insert(QStringLiteral("serial_status"), QStringLiteral("true"));
    settings.insert(QStringLiteral("serial_ri"), QStringLiteral("true"));
    settings.insert(QStringLiteral("serial_direct"), QStringLiteral("true"));
    settings.insert(QStringLiteral("serial_translate"), QStringLiteral("crlf_cr"));
    settings.insert(QStringLiteral("uaeserial"), QStringLiteral("true"));
    settings.insert(QStringLiteral("unix.samplersoundcard"), QStringLiteral("3"));
    settings.insert(QStringLiteral("sampler_stereo"), QStringLiteral("true"));
    settings.insert(QStringLiteral("midiout_device"), QStringLiteral("-1"));
    settings.insert(QStringLiteral("midiin_device"), QStringLiteral("2"));
    settings.insert(QStringLiteral("midirouter"), QStringLiteral("false"));
    settings.insert(QStringLiteral("parallel_matrix_emulation"), QStringLiteral("ascii"));
    settings.insert(QStringLiteral("parallel_postscript_detection"), QStringLiteral("true"));
    settings.insert(QStringLiteral("parallel_postscript_emulation"), QStringLiteral("false"));
    settings.insert(QStringLiteral("parallel_autoflush"), QStringLiteral("9"));
    settings.insert(QStringLiteral("ghostscript_parameters"), QStringLiteral("-dNOPAUSE"));
    settings.insert(QStringLiteral("unix.ui.config_path"), QStringLiteral("/configs"));
    settings.insert(QStringLiteral("unix.screenshot_path"), QStringLiteral("/screenshots"));

    struct uae_prefs *prefs = allocPrefs();
    if (!prefs) {
        qWarning().noquote() << "failed to allocate preferences";
        return false;
    }
    prefs->fpu_model = 68881;
    prefs->address_space_24 = true;

    parsedLines.clear();
    bool ok = applyWinUaeQtConfigToPrefs(WinUaeQtConfig(settings), prefs);
    ok = require(ok, "adapter rejected representative config") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("unix.ui.config_path=/configs")), "UI-only path was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("kickstart_rom_file=/roms/A1200.rom")), "kickstart_rom_file was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("kickstart_ext_rom_file=/roms/ext.rom")), "kickstart_ext_rom_file was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("floppy0=/disks/install.adf")), "floppy0 was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("floppy1=/disks/extras.adf")), "floppy1 was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("floppy0type=0")), "floppy0type was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("floppy1type=1")), "floppy1type was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("floppy2type=-1")), "floppy2type was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("floppy0wp=true")), "floppy0wp was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("floppy1wp=false")), "floppy1wp was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("floppy2wp=yes")), "floppy2wp was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("floppy3wp=t")), "floppy3wp was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("chipset_compatible=A1200")), "chipset compatibility was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("cpu_model=68020")), "cpu model was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("fpu_model=68882")), "fpu model was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("cpu_24bit_addressing=false")), "cpu_24bit_addressing was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("nr_floppies=2")), "nr_floppies was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("chipmem_size=4")), "chipmem_size was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("fastmem_size=8")), "fastmem_size was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("bogomem_size=2")), "bogomem_size was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("z3mem_size=64")), "z3mem_size was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("cachesize=8")), "cachesize was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("sound_output=normal")), "sound output was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("sound_auto=true")), "sound_auto was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("sound_channels=mixed")), "sound_channels was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("floppy0sound=1")), "floppy0sound was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("df0idhw=false")), "df0idhw was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("gfxcard_size=16")), "gfxcard_size was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("gfxcard_type=PicassoIV_Z3")), "gfxcard_type was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("gfx_width_windowed=800")), "gfx_width_windowed was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("gfx_height_windowed=600")), "gfx_height_windowed was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("gfx_fullscreen_amiga=fullwindow")), "gfx_fullscreen_amiga was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("gfx_vsync=autoswitch")), "gfx_vsync was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("rtg_modes=0x10")), "rtg_modes was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("bsdsocket_emu=true")), "bsdsocket_emu was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("sana2=true")), "sana2 was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("uaescsimode=SPTI")), "uaescsimode was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("unix.rtg_vblank=real")), "unix.rtg_vblank was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("unix.serial_port=TCP://0.0.0.0:1234")), "unix.serial_port was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("serial_translate=crlf_cr")), "serial_translate was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("uaeserial=true")), "uaeserial was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("unix.samplersoundcard=3")), "unix.samplersoundcard was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("midiout_device=-1")), "midiout_device was delegated") && ok;
    ok = require(!parsedLines.contains(QStringLiteral("parallel_matrix_emulation=ascii")), "parallel_matrix_emulation was delegated") && ok;
    ok = require(parsedLines.contains(QStringLiteral("unix.screenshot_path=/screenshots")), "runtime path was not delegated") && ok;
    ok = require(parsedLines.contains(QStringLiteral("chipset=aga")), "chipset was not delegated") && ok;
    ok = require(parsedLines.contains(QStringLiteral("uaehf0=dir,rw,DH0:System:/tmp/System,0")), "hard drive mount was not delegated") && ok;

    ok = requireText(prefs->romfile, "/roms/A1200.rom", "romfile") && ok;
    ok = requireText(prefs->romextfile, "/roms/ext.rom", "romextfile") && ok;
    ok = requireText(prefs->floppyslots[0].df, "/disks/install.adf", "floppy0") && ok;
    ok = requireText(prefs->floppyslots[1].df, "/disks/extras.adf", "floppy1") && ok;
    ok = requireInt(prefs->floppyslots[0].dfxtype, DRV_35_DD, "floppy0type") && ok;
    ok = requireInt(prefs->floppyslots[1].dfxtype, DRV_35_HD, "floppy1type") && ok;
    ok = requireInt(prefs->floppyslots[2].dfxtype, DRV_NONE, "floppy2type") && ok;
    ok = require(prefs->floppyslots[0].forcedwriteprotect, "floppy0wp") && ok;
    ok = require(!prefs->floppyslots[1].forcedwriteprotect, "floppy1wp") && ok;
    ok = require(prefs->floppyslots[2].forcedwriteprotect, "floppy2wp") && ok;
    ok = require(prefs->floppyslots[3].forcedwriteprotect, "floppy3wp") && ok;
    ok = requireInt(prefs->nr_floppies, 2, "nr_floppies") && ok;
    ok = require(!prefs->cs_df0idhw, "df0idhw") && ok;
    ok = requireInt(prefs->cs_compatible, CP_A1200, "cs_compatible") && ok;
    ok = requireInt(prefs->cpu_model, 68020, "cpu_model") && ok;
    ok = requireInt(prefs->fpu_model, 68882, "fpu_model") && ok;
    ok = require(!prefs->address_space_24, "cpu_24bit_addressing") && ok;
    ok = requireUnsigned(prefs->chipmem.size, 4 * 0x80000, "chipmem.size") && ok;
    ok = requireUnsigned(prefs->fastmem[0].size, 8 * 0x100000, "fastmem[0].size") && ok;
    ok = requireUnsigned(prefs->bogomem.size, 2 * 0x40000, "bogomem.size") && ok;
    ok = requireUnsigned(prefs->z3fastmem[0].size, 64 * 0x100000, "z3fastmem[0].size") && ok;
    ok = requireInt(prefs->cachesize, 8, "cachesize") && ok;
    ok = requireInt(prefs->produce_sound, 2, "produce_sound") && ok;
    ok = require(prefs->sound_auto, "sound_auto") && ok;
    ok = requireInt(prefs->sound_stereo, SND_STEREO, "sound_stereo") && ok;
    ok = requireInt(prefs->sound_freq, 44100, "sound_freq") && ok;
    ok = requireInt(prefs->sound_maxbsiz, 4096, "sound_maxbsiz") && ok;
    ok = requireInt(prefs->sound_interpol, 2, "sound_interpol") && ok;
    ok = requireInt(prefs->sound_filter, 1, "sound_filter") && ok;
    ok = requireInt(prefs->sound_filter_type, 1, "sound_filter_type") && ok;
    ok = requireInt(prefs->sound_stereo_separation, 8, "sound_stereo_separation") && ok;
    ok = requireInt(prefs->sound_mixed_stereo_delay, 2, "sound_mixed_stereo_delay") && ok;
    ok = require(prefs->sound_stereo_swap_paula, "sound_stereo_swap_paula") && ok;
    ok = requireInt(prefs->floppyslots[0].dfxclick, 1, "floppy0sound") && ok;
    ok = requireInt(prefs->dfxclickvolume_empty[0], 34, "floppy0soundvolume_empty") && ok;
    ok = requireInt(prefs->dfxclickvolume_disk[0], 22, "floppy0soundvolume_disk") && ok;
    ok = requireInt(prefs->win32_uaescsimode, UAESCSI_SPTI, "win32_uaescsimode") && ok;
    ok = requireInt(prefs->win32_rtgvblankrate, -1, "win32_rtgvblankrate") && ok;
    ok = requireUnsigned(prefs->rtgboards[0].rtgmem_size, 16 * 0x100000, "rtgmem_size") && ok;
    ok = requireInt(prefs->rtgboards[0].rtgmem_type, GFXBOARD_ID_PICASSO4_Z3, "rtgmem_type") && ok;
    ok = requireInt(prefs->gfx_monitor[0].gfx_size_win.width, 800, "gfx width") && ok;
    ok = requireInt(prefs->gfx_monitor[0].gfx_size_win.height, 600, "gfx height") && ok;
    ok = requireInt(prefs->gfx_monitor[0].gfx_size_fs.special, WH_NATIVE, "gfx fullscreen native") && ok;
    ok = requireInt(prefs->gfx_apmode[APMODE_NATIVE].gfx_fullscreen, GFX_FULLWINDOW, "gfx_fullscreen_amiga") && ok;
    ok = requireInt(prefs->gfx_apmode[APMODE_RTG].gfx_fullscreen, GFX_FULLSCREEN, "gfx_fullscreen_picasso") && ok;
    ok = requireInt(prefs->gfx_apmode[APMODE_NATIVE].gfx_vsync, 2, "gfx_vsync") && ok;
    ok = requireInt(prefs->gfx_apmode[APMODE_NATIVE].gfx_vsyncmode, 1, "gfx_vsyncmode") && ok;
    ok = requireInt(prefs->gfx_apmode[APMODE_NATIVE].gfx_backbuffers, 3, "gfx_backbuffers") && ok;
    ok = require(prefs->gfx_windowed_resize, "gfx_resize_windowed") && ok;
    ok = requireInt(prefs->gfx_framerate, 2, "gfx_framerate") && ok;
    ok = requireInt(prefs->gfx_vresolution, VRES_DOUBLE, "gfx_vresolution") && ok;
    ok = requireInt(prefs->gfx_pscanlines, 2, "gfx_pscanlines") && ok;
    ok = requireInt(prefs->gfx_xcenter, 1, "gfx_center_horizontal") && ok;
    ok = requireInt(prefs->gfx_ycenter, 2, "gfx_center_vertical") && ok;
    ok = require(prefs->gfx_keep_aspect, "gfx_keep_aspect") && ok;
    ok = requireInt(prefs->picasso96_modeflags, 0x10, "rtg_modes") && ok;
    ok = require(prefs->socket_emu, "bsdsocket_emu") && ok;
    ok = require(prefs->sana2, "sana2") && ok;
    ok = requireText(prefs->sername, "TCP://0.0.0.0:1234", "unix.serial_port") && ok;
    ok = require(prefs->use_serial, "use_serial") && ok;
    ok = require(prefs->serial_demand, "serial_on_demand") && ok;
    ok = require(prefs->serial_hwctsrts, "serial_hardware_ctsrts") && ok;
    ok = require(prefs->serial_rtsctsdtrdtecd, "serial_status") && ok;
    ok = require(prefs->serial_ri, "serial_ri") && ok;
    ok = require(prefs->serial_direct, "serial_direct") && ok;
    ok = requireInt(prefs->serial_crlf, 1, "serial_translate") && ok;
    ok = require(prefs->uaeserial, "uaeserial") && ok;
    ok = requireInt(prefs->win32_samplersoundcard, 3, "win32_samplersoundcard") && ok;
    ok = require(prefs->sampler_stereo, "sampler_stereo") && ok;
    ok = requireInt(prefs->win32_midioutdev, -1, "win32_midioutdev") && ok;
    ok = requireInt(prefs->win32_midiindev, 2, "win32_midiindev") && ok;
    ok = require(!prefs->win32_midirouter, "win32_midirouter") && ok;
    ok = requireInt(prefs->parallel_matrix_emulation, 1, "parallel_matrix_emulation") && ok;
    ok = require(prefs->parallel_postscript_detection, "parallel_postscript_detection") && ok;
    ok = requireInt(prefs->parallel_autoflush_time, 9, "parallel_autoflush") && ok;
    ok = requireText(prefs->ghostscript_parameters, "-dNOPAUSE", "ghostscript_parameters") && ok;
    free(prefs);
    return ok;
}

static bool testNativeWindowSize()
{
    WinUaeQtConfig::Settings settings;
    settings.insert(QStringLiteral("gfx_width_windowed"), QStringLiteral("native"));
    settings.insert(QStringLiteral("gfx_height_windowed"), QStringLiteral("900"));

    struct uae_prefs *prefs = allocPrefs();
    if (!prefs) {
        qWarning().noquote() << "failed to allocate preferences";
        return false;
    }
    prefs->gfx_monitor[0].gfx_size_win.width = 123;
    prefs->gfx_monitor[0].gfx_size_win.height = 456;

    bool ok = applyWinUaeQtConfigToPrefs(WinUaeQtConfig(settings), prefs);
    ok = require(ok, "adapter rejected native window size config") && ok;
    ok = requireInt(prefs->gfx_monitor[0].gfx_size_win.width, 0, "native gfx width") && ok;
    ok = requireInt(prefs->gfx_monitor[0].gfx_size_win.height, 0, "native gfx height") && ok;
    free(prefs);
    return ok;
}

static bool testRepeatedMountDelegation()
{
    WinUaeQtConfig config;
    config.applyRepeatedSettings({
        { QStringLiteral("filesystem2"), QStringLiteral("rw,DH0:System:/tmp/System,0") },
        { QStringLiteral("filesystem2"), QStringLiteral("ro,DH1:Work:/tmp/Work,5") }
    }, {
        QStringLiteral("filesystem2")
    });

    struct uae_prefs *prefs = allocPrefs();
    if (!prefs) {
        qWarning().noquote() << "failed to allocate preferences";
        return false;
    }

    parsedLines.clear();
    bool ok = applyWinUaeQtConfigToPrefs(config, prefs);
    ok = require(ok, "adapter rejected repeated mount config") && ok;
    ok = requireInt(parsedLines.size(), 2, "repeated mount parsed line count") && ok;
    ok = require(parsedLines.contains(QStringLiteral("filesystem2=rw,DH0:System:/tmp/System,0")), "first repeated mount was not delegated") && ok;
    ok = require(parsedLines.contains(QStringLiteral("filesystem2=ro,DH1:Work:/tmp/Work,5")), "second repeated mount was not delegated") && ok;
    free(prefs);
    return ok;
}

int main()
{
    bool ok = true;
    ok = require(!applyWinUaeQtConfigToPrefs(WinUaeQtConfig(), nullptr), "null prefs should fail") && ok;
    ok = testRepresentativeConfig() && ok;
    ok = testNativeWindowSize() && ok;
    ok = testRepeatedMountDelegation() && ok;
    return ok ? 0 : 1;
}
