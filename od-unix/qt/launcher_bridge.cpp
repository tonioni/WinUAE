#include "launcher_bridge.h"

#include "launcher.h"
#include "prefs_adapter.h"

#include <QByteArray>
#include <QVector>
#include <QString>
#include <QStringList>

#include <stdio.h>
#include <string.h>

// Qt defines Qt::HANDLE, and the Unix core compatibility header defines HANDLE.
#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "memory.h"
#include "autoconf.h"
#include "rommgr.h"
#include "xwin.h"
#include "host.h"
#include "uae.h"
#include "video.h"
#include "audio.h"
#include "inputrecord.h"
#include "savestate.h"
#include "newcpu.h"
#include "zfile.h"
#include "gfxboard.h"
#include "ethernet.h"
#include "registry.h"
#ifdef PROWIZARD
#include "moduleripper.h"
#endif

extern struct netdriverdata **target_ethernet_enumerate(void);

static QString bridgeText(const TCHAR *text)
{
    return text && text[0] ? QString::fromLocal8Bit(text) : QString();
}

static QStringList bridgeTextList(const TCHAR *text)
{
    QStringList items;
    if (!text) {
        return items;
    }
    const TCHAR *p = text;
    while (p[0]) {
        items.append(bridgeText(p));
        while (p[0]) {
            p++;
        }
        p++;
    }
    return items;
}

static QString bridgeBoardDisplay(const TCHAR *name, const TCHAR *manufacturer)
{
    QString display = bridgeText(name);
    const QString maker = bridgeText(manufacturer);
    if (!maker.isEmpty()) {
        display += QStringLiteral(" (%1)").arg(maker);
    }
    return display;
}

static WinUaeQtBoardSetting bridgeBoardSetting(const struct expansionboardsettings *setting)
{
    WinUaeQtBoardSetting item;
    if (!setting) {
        return item;
    }

    const QStringList names = bridgeTextList(setting->name);
    const QStringList configs = bridgeTextList(setting->configname);
    item.display = names.value(0);
    item.configValue = configs.value(0);
    if (setting->type == EXPANSIONBOARD_MULTI) {
        item.type = WinUaeQtBoardSettingType::Multi;
        item.multiDisplays = names.mid(1);
        item.multiValues = configs.mid(1);
    } else if (setting->type == EXPANSIONBOARD_STRING) {
        item.type = WinUaeQtBoardSettingType::String;
    } else {
        item.type = WinUaeQtBoardSettingType::CheckBox;
    }
    return item;
}

static QVector<WinUaeQtBoardSetting> bridgeBoardSettings(const struct expansionboardsettings *settings)
{
    QVector<WinUaeQtBoardSetting> items;
    if (!settings) {
        return items;
    }
    for (int i = 0; settings[i].name; i++) {
        WinUaeQtBoardSetting item = bridgeBoardSetting(&settings[i]);
        if (!item.display.isEmpty() && !item.configValue.isEmpty()) {
            items.append(item);
        }
    }
    return items;
}

static int bridgeExpansionCategoryMask(int deviceFlags)
{
    int mask = 0;
    if (deviceFlags & EXPANSIONTYPE_INTERNAL) {
        mask |= WinUaeQtExpansionCategoryInternal;
    }
    if ((deviceFlags & EXPANSIONTYPE_SCSI) && !(deviceFlags & (EXPANSIONTYPE_SASI | EXPANSIONTYPE_CUSTOM))) {
        mask |= WinUaeQtExpansionCategoryScsi;
    }
    if (deviceFlags & EXPANSIONTYPE_IDE) {
        mask |= WinUaeQtExpansionCategoryIde;
    }
    if (deviceFlags & EXPANSIONTYPE_SASI) {
        mask |= WinUaeQtExpansionCategorySasi;
    }
    if (deviceFlags & EXPANSIONTYPE_CUSTOM) {
        mask |= WinUaeQtExpansionCategoryCustom;
    }
    if (deviceFlags & EXPANSIONTYPE_PCI_BRIDGE) {
        mask |= WinUaeQtExpansionCategoryPciBridge;
    }
    if (deviceFlags & EXPANSIONTYPE_X86_BRIDGE) {
        mask |= WinUaeQtExpansionCategoryX86Bridge;
    }
    if (deviceFlags & EXPANSIONTYPE_RTG) {
        mask |= WinUaeQtExpansionCategoryRtg;
    }
    if (deviceFlags & EXPANSIONTYPE_SOUND) {
        mask |= WinUaeQtExpansionCategorySound;
    }
    if (deviceFlags & EXPANSIONTYPE_NET) {
        mask |= WinUaeQtExpansionCategoryNet;
    }
    if (deviceFlags & EXPANSIONTYPE_FLOPPY) {
        mask |= WinUaeQtExpansionCategoryFloppy;
    }
    if (deviceFlags & EXPANSIONTYPE_X86_EXPANSION) {
        mask |= WinUaeQtExpansionCategoryX86Expansion;
    }
    return mask;
}

static WinUaeQtBoardCatalog bridgeBoardCatalog(void *)
{
    WinUaeQtBoardCatalog catalog;

    target_ethernet_enumerate();
    ethernet_updateselection();

    for (int i = 0; expansionroms[i].name; i++) {
        const struct expansionromtype *rom = &expansionroms[i];
        if (rom->romtype & ROMTYPE_CPUBOARD) {
            continue;
        }
        WinUaeQtExpansionBoardCatalogItem item;
        item.key = bridgeText(rom->name);
        item.display = bridgeBoardDisplay(rom->friendlyname, rom->friendlymanufacturer);
        item.deviceFlags = rom->deviceflags;
        item.categoryMask = bridgeExpansionCategoryMask(rom->deviceflags);
        item.zorro = rom->zorro;
        item.singleOnly = rom->singleonly;
        item.dma24Bit = rom->deviceflags & EXPANSIONTYPE_DMA24;
        item.pcmcia = rom->deviceflags & EXPANSIONTYPE_PCMCIA;
        item.autobootJumper = rom->autoboot_jumper;
        item.idJumper = rom->id_jumper;
        item.noRomFile = rom->romtype & ROMTYPE_NOT;
        item.clockPort = rom->deviceflags & EXPANSIONTYPE_CLOCKPORT;
        item.extraHdPorts = rom->extrahdports;
        if (rom->subtypes) {
            for (int j = 0; rom->subtypes[j].name; j++) {
                WinUaeQtBoardSubtype subtype;
                subtype.display = bridgeText(rom->subtypes[j].name);
                subtype.configValue = bridgeText(rom->subtypes[j].configname);
                subtype.deviceFlags = rom->subtypes[j].deviceflags;
                subtype.hasRomTypeOverride = rom->subtypes[j].romtype != 0;
                subtype.noRomFile = rom->subtypes[j].romtype & ROMTYPE_NOT;
                item.subtypes.append(subtype);
            }
        }
        item.settings = bridgeBoardSettings(rom->settings);
        if (!item.key.isEmpty() && !item.display.isEmpty()) {
            catalog.expansionBoards.append(item);
        }
    }

    for (int type = 0; cpuboards[type].name; type++) {
        if (!cpuboards[type].subtypes || cpuboards[type].id < 0) {
            continue;
        }
        const QString typeName = bridgeText(cpuboards[type].name);
        for (int subtype = 0; cpuboards[type].subtypes[subtype].name; subtype++) {
            const struct cpuboardsubtype *st = &cpuboards[type].subtypes[subtype];
            WinUaeQtCpuBoardCatalogItem item;
            item.type = typeName;
            item.display = bridgeText(st->name);
            item.configValue = bridgeText(st->configname);
            item.maxMemoryMb = st->maxmemory > 0 ? st->maxmemory / (1024 * 1024) : 0;
            item.ppc = item.configValue.compare(QStringLiteral("BlizzardPPC"), Qt::CaseInsensitive) == 0
                || item.configValue.compare(QStringLiteral("CyberStormPPC"), Qt::CaseInsensitive) == 0;
            item.settings = bridgeBoardSettings(st->settings);
            if (!item.type.isEmpty() && !item.display.isEmpty() && !item.configValue.isEmpty()) {
                catalog.cpuBoards.append(item);
            }
        }
    }

    for (int index = 0;; index++) {
        const int id = gfxboard_get_id_from_index(index);
        if (id < 0) {
            break;
        }
        WinUaeQtRtgBoardCatalogItem item;
        item.display = bridgeBoardDisplay(gfxboard_get_name(id), gfxboard_get_manufacturername(id));
        item.configValue = bridgeText(gfxboard_get_configname(id));
        struct rtgboardconfig rbc = {};
        rbc.rtgmem_type = id;
        rbc.rtgmem_size = 4 * 1024 * 1024;
        item.configType = gfxboard_get_configtype(&rbc);
        if (!item.display.isEmpty() && !item.configValue.isEmpty()) {
            catalog.rtgBoards.append(item);
        }
    }

    return catalog;
}

static QString bridgeHex(uae_u32 value)
{
    return QStringLiteral("0x%1").arg(value, 8, 16, QLatin1Char('0'));
}

static QVector<WinUaeQtHardwareBoard> bridgeHardwareBoards(void *context)
{
    struct uae_prefs *prefs = static_cast<struct uae_prefs *>(context);
    QVector<WinUaeQtHardwareBoard> boards;
    if (!prefs) {
        return boards;
    }

    expansion_generate_autoconfig_info(prefs);
    for (int i = 0;; i++) {
        struct autoconfig_info *aci = expansion_get_autoconfig_data(prefs, i);
        if (!aci) {
            break;
        }
        WinUaeQtHardwareBoard board;
        board.index = i;
        board.type = aci->zorro >= 1 && aci->zorro <= 3 ? QStringLiteral("Z%1").arg(aci->zorro) : QStringLiteral("-");
        if (aci->parent_of_previous) {
            board.name += QStringLiteral(" - ");
        } else if (aci->parent_address_space || aci->parent_romtype) {
            board.name += QStringLiteral("? ");
        }
        board.name += bridgeText(aci->name);
        if (board.name.isEmpty()) {
            board.name = QStringLiteral("<no name>");
        }
        board.start = aci->start != 0xffffffff ? bridgeHex(aci->start) : QStringLiteral("-");
        board.end = aci->size != 0 ? bridgeHex(aci->start + aci->size - 1) : QStringLiteral("-");
        board.size = aci->size != 0 ? bridgeHex(aci->size) : QStringLiteral("-");
        if (aci->autoconfig_bytes[0] != 0xff) {
            board.id = QStringLiteral("0x%1/0x%2")
                .arg((aci->autoconfig_bytes[4] << 8) | aci->autoconfig_bytes[5], 4, 16, QLatin1Char('0'))
                .arg(aci->autoconfig_bytes[1], 2, 16, QLatin1Char('0'));
        } else {
            board.id = QStringLiteral("-");
        }
        board.movable = expansion_can_move(prefs, i);
        boards.append(board);
    }
    return boards;
}

static bool bridgeApplyHardwareConfig(void *context, const WinUaeQtConfig &config)
{
    struct uae_prefs *prefs = static_cast<struct uae_prefs *>(context);
    return applyWinUaeQtConfigToPrefs(config, prefs);
}

static bool bridgeHardwareCustomOrder(void *context)
{
    struct uae_prefs *prefs = static_cast<struct uae_prefs *>(context);
    return prefs && prefs->autoconfig_custom_sort;
}

static void bridgeSetHardwareCustomOrder(void *context, bool enabled)
{
    struct uae_prefs *prefs = static_cast<struct uae_prefs *>(context);
    if (!prefs) {
        return;
    }
    prefs->autoconfig_custom_sort = enabled;
    expansion_set_autoconfig_sort(prefs);
}

static bool bridgeHardwareCanMove(void *context, int index, int direction)
{
    struct uae_prefs *prefs = static_cast<struct uae_prefs *>(context);
    return prefs
        && expansion_can_move(prefs, index)
        && expansion_autoconfig_move(prefs, index, direction, true) >= 0;
}

static int bridgeMoveHardwareBoard(void *context, int index, int direction)
{
    struct uae_prefs *prefs = static_cast<struct uae_prefs *>(context);
    if (!prefs) {
        return -1;
    }
    return expansion_autoconfig_move(prefs, index, direction, false);
}

static QString bridgeOrderOnlyValue(const QString &value)
{
    for (QString token : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        token = token.trimmed();
        if (token.startsWith(QStringLiteral("order="), Qt::CaseInsensitive)) {
            return token;
        }
    }
    return QString();
}

static WinUaeQtConfig::Settings bridgeHardwareOrderSettings(void *context)
{
    struct uae_prefs *prefs = static_cast<struct uae_prefs *>(context);
    WinUaeQtConfig::Settings settings;
    if (!prefs) {
        return settings;
    }

    struct zfile *file = zfile_fopen_empty(nullptr, _T("unix-qt-prefs.uae"), 0);
    if (!file) {
        return settings;
    }
    cfgfile_save_options(file, prefs, CONFIG_TYPE_ALL);
    size_t len = 0;
    const uae_u8 *data = zfile_get_data_pointer(file, &len);
    const QString text = data && len ? QString::fromLocal8Bit((const char *)data, int(len)) : QString();
    zfile_fclose(file);

    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            continue;
        }
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        if (key == QStringLiteral("board_custom_order")) {
            settings.insert(key, value);
            continue;
        }
        const QString order = bridgeOrderOnlyValue(value);
        if (!order.isEmpty()) {
            settings.insert(key, order);
        }
    }
    return settings;
}

static void bridgeSaveScreenshot(void *)
{
    screenshot(-1, 1, 0);
}

static bool bridgeSampleRipperEnabled(void *)
{
    return sampleripper_enabled != 0;
}

static void bridgeSetSampleRipperEnabled(void *, bool enabled)
{
    if ((sampleripper_enabled != 0) == enabled) {
        return;
    }
    sampleripper_enabled = enabled ? 1 : 0;
    audio_sampleripper(-1);
}

static void bridgeCopyPath(TCHAR *dst, size_t dstSize, const char *path)
{
    if (!dst || !dstSize) {
        return;
    }
    if (!path) {
        path = "";
    }
    _tcsncpy(dst, path, dstSize - 1);
    dst[dstSize - 1] = 0;
}

static bool bridgeStatePlaybackEnabled(void *)
{
    return input_play != 0;
}

static bool bridgeStateRecordingEnabled(void *)
{
    return input_record != 0;
}

static bool bridgeCanSaveStateRecording(void *)
{
    return input_record > INPREC_RECORD_NORMAL;
}

static bool bridgeSetStatePlayback(void *, bool enabled, const char *path)
{
    if (input_record) {
        inprec_close(true);
    }
    if (!enabled) {
        if (input_play) {
            inprec_close(true);
        }
        return true;
    }
    if (!path || !path[0]) {
        return false;
    }
    inprec_close(true);
    input_play = INPREC_PLAY_NORMAL;
    bridgeCopyPath(currprefs.inprecfile, sizeof currprefs.inprecfile / sizeof(TCHAR), path);
    set_special(SPCFLAG_MODE_CHANGE);
    return true;
}

static void bridgeToggleStateRecording(void *)
{
    if (input_play) {
        inprec_playtorecord();
    } else if (input_record) {
        inprec_close(true);
    } else {
        input_record = INPREC_RECORD_START;
        set_special(SPCFLAG_MODE_CHANGE);
    }
}

static bool bridgeSaveStateRecording(void *, const char *path)
{
    if (input_record <= INPREC_RECORD_NORMAL || !path || !path[0]) {
        return false;
    }

    TCHAR inputPath[MAX_DPATH];
    TCHAR statePath[MAX_DPATH];
    bridgeCopyPath(inputPath, sizeof inputPath / sizeof(TCHAR), path);
    _sntprintf(statePath, sizeof statePath / sizeof(TCHAR), _T("%s.uss"), inputPath);
    statePath[(sizeof statePath / sizeof(TCHAR)) - 1] = 0;

    inprec_save(inputPath, statePath);
    statefile_save_recording(statePath);
    return true;
}

#ifdef PROWIZARD
static void bridgeRunProWizard(void *)
{
    moduleripper();
}
#endif

static void bridgePollHostWindowEvents(void *)
{
    unix_host_check_quit();
    bool quitRequested = false;
    unix_video_poll_window_events(&quitRequested);
    if (quitRequested) {
        uae_quit();
    }
    unix_host_check_quit();
}

static bool bridgeHostSettingGet(void *, const char *key, char *out, int outLen)
{
    int size = outLen;
    out[0] = 0;
    return regquerystr(nullptr, key, out, &size) != 0;
}

static void bridgeHostSettingSet(void *, const char *key, const char *value)
{
    regsetstr(nullptr, key, value);
}

static void bridgeHostSettingsFlush(void *)
{
    registry_flush();
}

static WinUaeQtHardwareInfoProvider bridgeHardwareProvider(struct uae_prefs *prefs, bool runtimeActions)
{
    WinUaeQtHardwareInfoProvider provider;
    provider.context = prefs;
    provider.hostSettingGet = bridgeHostSettingGet;
    provider.hostSettingSet = bridgeHostSettingSet;
    provider.hostSettingsFlush = bridgeHostSettingsFlush;
    provider.boardCatalog = bridgeBoardCatalog;
    provider.applyConfig = bridgeApplyHardwareConfig;
    provider.boards = bridgeHardwareBoards;
    provider.customOrder = bridgeHardwareCustomOrder;
    provider.setCustomOrder = bridgeSetHardwareCustomOrder;
    provider.canMove = bridgeHardwareCanMove;
    provider.move = bridgeMoveHardwareBoard;
    provider.orderSettings = bridgeHardwareOrderSettings;
    provider.sampleRipperEnabled = bridgeSampleRipperEnabled;
    provider.setSampleRipperEnabled = bridgeSetSampleRipperEnabled;
    if (runtimeActions) {
        provider.pollHostWindowEvents = bridgePollHostWindowEvents;
        provider.saveScreenshot = bridgeSaveScreenshot;
        provider.statePlaybackEnabled = bridgeStatePlaybackEnabled;
        provider.stateRecordingEnabled = bridgeStateRecordingEnabled;
        provider.canSaveStateRecording = bridgeCanSaveStateRecording;
        provider.setStatePlayback = bridgeSetStatePlayback;
        provider.toggleStateRecording = bridgeToggleStateRecording;
        provider.saveStateRecording = bridgeSaveStateRecording;
#ifdef PROWIZARD
        provider.runProWizard = bridgeRunProWizard;
#endif
    }
    return provider;
}

int winUaeQtLauncherArgumentsSpecifyConfig(int argc, char **argv)
{
    QStringList arguments;
    arguments.reserve(argc);
    for (int i = 0; i < argc; i++) {
        arguments.append(QString::fromLocal8Bit(argv[i]));
    }
    return winUaeQtArgumentsSpecifyConfig(arguments) ? 1 : 0;
}

int runWinUaeQtLauncherForPrefs(int argc, char **argv, struct uae_prefs *prefs, int *exitCode)
{
    return runWinUaeQtLauncherForPrefsWithConfig(argc, argv, prefs, nullptr, 0, exitCode);
}

int runWinUaeQtLauncherForPrefsWithConfig(int argc, char **argv, struct uae_prefs *prefs, const char *initialConfigPath, int runtimeActions, int *exitCode)
{
    const QString initialPath = initialConfigPath && initialConfigPath[0]
        ? QString::fromLocal8Bit(initialConfigPath)
        : QString();
    WinUaeQtLauncherResult result = runWinUaeQtLauncherForConfig(argc, argv, initialPath, bridgeHardwareProvider(prefs, runtimeActions != 0));
    if (result.status == WinUaeQtLauncherStatus::StartRequested
        || result.status == WinUaeQtLauncherStatus::RestartRequested) {
        if (!applyWinUaeQtConfigToPrefs(result.config, prefs)) {
            fprintf(stderr, "Unix Qt UI failed: no preferences target available\n");
            if (exitCode) {
                *exitCode = 1;
            }
            return WINUAE_QT_LAUNCHER_ERROR;
        }
        if (result.status == WinUaeQtLauncherStatus::RestartRequested) {
            if (exitCode) {
                *exitCode = 0;
            }
            return WINUAE_QT_LAUNCHER_RESTART;
        }
        return result.hardReset ? WINUAE_QT_LAUNCHER_RESET : WINUAE_QT_LAUNCHER_START;
    }
    if (result.status == WinUaeQtLauncherStatus::QuitRequested) {
        if (exitCode) {
            *exitCode = 0;
        }
        return WINUAE_QT_LAUNCHER_QUIT;
    }
    if (result.status == WinUaeQtLauncherStatus::Error) {
        QByteArray error = result.error.toLocal8Bit();
        fprintf(stderr, "Unix Qt UI failed: %s\n", error.constData());
        if (exitCode) {
            *exitCode = result.exitCode ? result.exitCode : 1;
        }
        return WINUAE_QT_LAUNCHER_ERROR;
    }
    if (exitCode) {
        *exitCode = 0;
    }
    return WINUAE_QT_LAUNCHER_EXIT;
}

int runWinUaeQtRuntimeFileDialog(int argc, char **argv, int shortcut, const char *initialPath, char *selectedPath, size_t selectedPathLen, int *exitCode)
{
    if (!selectedPath || selectedPathLen == 0) {
        if (exitCode) {
            *exitCode = 1;
        }
        return WINUAE_QT_LAUNCHER_ERROR;
    }
    selectedPath[0] = 0;

    const QString initial = initialPath && initialPath[0]
        ? QString::fromLocal8Bit(initialPath)
        : QString();
    const WinUaeQtRuntimeFileDialogResult result = runWinUaeQtRuntimeFileDialog(argc, argv, shortcut, initial);
    if (!result.accepted) {
        if (exitCode) {
            *exitCode = 0;
        }
        return WINUAE_QT_LAUNCHER_EXIT;
    }

    const QByteArray path = result.path.toLocal8Bit();
    if (size_t(path.size()) >= selectedPathLen) {
        fprintf(stderr, "Unix Qt UI failed: selected path is too long\n");
        if (exitCode) {
            *exitCode = 1;
        }
        return WINUAE_QT_LAUNCHER_ERROR;
    }
    memcpy(selectedPath, path.constData(), size_t(path.size()) + 1);
    if (exitCode) {
        *exitCode = 0;
    }
    return WINUAE_QT_LAUNCHER_START;
}

int runWinUaeQtMessageBox(int argc, char **argv, int flags, const char *message, int *exitCode)
{
    const int result = runWinUaeQtMessageBox(
        argc,
        argv,
        flags,
        message && message[0] ? QString::fromLocal8Bit(message) : QString());
    if (exitCode) {
        *exitCode = 0;
    }
    return result;
}
