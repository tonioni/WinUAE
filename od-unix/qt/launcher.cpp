#include <QtWidgets>

#include <QDesktopServices>
#include <QFileOpenEvent>
#include <QSet>
#include <QStandardItemModel>
#include <QUrl>

#include <algorithm>
#include <cstdio>
#include <functional>
#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif
#if defined(__APPLE__)
#include <sys/disk.h>
#if defined(UAE_UNIX_WITH_COREMIDI)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>
#endif
#elif defined(__linux__)
#include <linux/fs.h>
#if defined(UAE_UNIX_WITH_ALSA_MIDI)
#include <alsa/asoundlib.h>
#endif
#endif

#ifdef UAE_UNIX_WITH_SDL3
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#endif

#include "config.h"
#include "launcher.h"
#include "mount_config.h"
#include "path_utils.h"

#ifndef WINUAE_UNIX_SOURCE_DIR
#define WINUAE_UNIX_SOURCE_DIR "."
#endif

#ifndef WINUAE_UNIX_INSTALL_DATA_DIR
#define WINUAE_UNIX_INSTALL_DATA_DIR WINUAE_UNIX_SOURCE_DIR
#endif

#ifndef WINUAE_UNIX_INSTALL_DATADIR_RELATIVE
#define WINUAE_UNIX_INSTALL_DATADIR_RELATIVE "share/winuae"
#endif

#ifndef WINUAE_UNIX_VERSION_MAJOR
#define WINUAE_UNIX_VERSION_MAJOR 0
#endif

#ifndef WINUAE_UNIX_VERSION_MINOR
#define WINUAE_UNIX_VERSION_MINOR 0
#endif

#ifndef WINUAE_UNIX_VERSION_REVISION
#define WINUAE_UNIX_VERSION_REVISION 0
#endif

#ifndef UAE_UNIX_WITH_BSDSOCKET
#define UAE_UNIX_WITH_BSDSOCKET 0
#endif

#ifndef UAE_UNIX_WITH_UAESCSI
#define UAE_UNIX_WITH_UAESCSI 0
#endif

#ifndef UAE_UNIX_WITH_SANA2
#define UAE_UNIX_WITH_SANA2 0
#endif

#ifndef UAE_UNIX_WITH_JIT
#define UAE_UNIX_WITH_JIT 0
#endif

#ifndef UAE_UNIX_WITH_SAMPLER
#define UAE_UNIX_WITH_SAMPLER 0
#endif

#ifndef UAE_UNIX_WITH_MIDI
#define UAE_UNIX_WITH_MIDI 0
#endif

#ifndef UAE_UNIX_WITH_SNDBOARD
#define UAE_UNIX_WITH_SNDBOARD 0
#endif

#ifndef UAE_UNIX_WITH_SHADER_PIPELINE
#define UAE_UNIX_WITH_SHADER_PIPELINE 0
#endif

#ifndef UAE_UNIX_WITH_NATIVE_HARDDRIVES
#define UAE_UNIX_WITH_NATIVE_HARDDRIVES 0
#endif

#ifndef UAE_UNIX_WITH_TABLET
#define UAE_UNIX_WITH_TABLET 0
#endif

static bool systemPrefersDarkMode();
static void applyApplicationColors(QApplication &app, bool dark);

static constexpr int FpuInternal = -1;
static constexpr int MaxMountEntries = 8;
static constexpr int MaxControllerUnits = 8;
static constexpr int MaxCdSlots = 8;
static constexpr int MaxRomBoards = 4;
static constexpr int MaxDiskSwapperSlots = 20;
static constexpr int MaxQuickstartConfigs = 6;
static constexpr int MaxJoyportCustomSlots = 6;
static constexpr int CustomInputConfigSlots = 3;
static constexpr int GamePortsInputConfigLine = 4;
static constexpr int MaxInputSubEventSlots = 8;
static constexpr int HardwareBoardIndexRole = Qt::UserRole;
static constexpr int InputMappingKeyRole = Qt::UserRole;
static constexpr int InputMappingEditableRole = Qt::UserRole + 1;
static constexpr int InputFlagAutofire = 1;
static constexpr int InputFlagToggle = 2;
static constexpr int InputFlagInvertToggle = 16;
static constexpr int InputFlagInvert = 32;
static constexpr int InputFlagSetOnOff = 128;
static constexpr int InputFlagSetOnOffVal1 = 256;
static constexpr int InputFlagSetOnOffVal2 = 512;

static bool unixJitBackendAvailable()
{
#if UAE_UNIX_WITH_JIT
    return true;
#else
    return false;
#endif
}

static bool unixShaderPipelineAvailable()
{
#if UAE_UNIX_WITH_SHADER_PIPELINE
    return true;
#else
    return false;
#endif
}

static bool unixTabletBackendAvailable()
{
#if UAE_UNIX_WITH_TABLET
    return true;
#else
    return false;
#endif
}

struct WinUaeQtInputEventChoice {
    const char *display;
    const char *config;
};

static const WinUaeQtInputEventChoice inputEventChoices[] = {
    { "Joy1 Horizontal", "JOY1_HORIZ" },
    { "Joy1 Vertical", "JOY1_VERT" },
    { "Joy1 Horizontal (Analog)", "JOY1_HORIZ_POT" },
    { "Joy1 Vertical (Analog)", "JOY1_VERT_POT" },
    { "Joy1 Left", "JOY1_LEFT" },
    { "Joy1 Right", "JOY1_RIGHT" },
    { "Joy1 Up", "JOY1_UP" },
    { "Joy1 Down", "JOY1_DOWN" },
    { "Joy1 Left+Up", "JOY1_LEFT_UP" },
    { "Joy1 Left+Down", "JOY1_LEFT_DOWN" },
    { "Joy1 Right+Up", "JOY1_RIGHT_UP" },
    { "Joy1 Right+Down", "JOY1_RIGHT_DOWN" },
    { "Joy1 Fire/Mouse1 Left Button", "JOY1_FIRE_BUTTON" },
    { "Joy1 2nd Button/Mouse1 Right Button", "JOY1_2ND_BUTTON" },
    { "Joy1 3rd Button/Mouse1 Middle Button", "JOY1_3RD_BUTTON" },
    { "Joy1 CD32 Play", "JOY1_CD32_PLAY" },
    { "Joy1 CD32 RWD", "JOY1_CD32_RWD" },
    { "Joy1 CD32 FFW", "JOY1_CD32_FFW" },
    { "Joy1 CD32 Green", "JOY1_CD32_GREEN" },
    { "Joy1 CD32 Yellow", "JOY1_CD32_YELLOW" },
    { "Joy1 CD32 Red", "JOY1_CD32_RED" },
    { "Joy1 CD32 Blue", "JOY1_CD32_BLUE" },
    { "Mouse1 Horizontal", "MOUSE1_HORIZ" },
    { "Mouse1 Vertical", "MOUSE1_VERT" },
    { "Mouse1 Horizontal (inverted)", "MOUSE1_HORIZ_INV" },
    { "Mouse1 Vertical (inverted)", "MOUSE1_VERT_INV" },
    { "Mouse1 Up", "MOUSE1_UP" },
    { "Mouse1 Down", "MOUSE1_DOWN" },
    { "Mouse1 Left", "MOUSE1_LEFT" },
    { "Mouse1 Right", "MOUSE1_RIGHT" },
    { "Mouse1 Wheel", "MOUSE1_WHEEL" },
    { "Joy2 Horizontal", "JOY2_HORIZ" },
    { "Joy2 Vertical", "JOY2_VERT" },
    { "Joy2 Horizontal (Analog)", "JOY2_HORIZ_POT" },
    { "Joy2 Vertical (Analog)", "JOY2_VERT_POT" },
    { "Joy2 Left", "JOY2_LEFT" },
    { "Joy2 Right", "JOY2_RIGHT" },
    { "Joy2 Up", "JOY2_UP" },
    { "Joy2 Down", "JOY2_DOWN" },
    { "Joy2 Left+Up", "JOY2_LEFT_UP" },
    { "Joy2 Left+Down", "JOY2_LEFT_DOWN" },
    { "Joy2 Right+Up", "JOY2_RIGHT_UP" },
    { "Joy2 Right+Down", "JOY2_RIGHT_DOWN" },
    { "Joy2 Fire/Mouse2 Left Button", "JOY2_FIRE_BUTTON" },
    { "Joy2 2nd Button/Mouse2 Right Button", "JOY2_2ND_BUTTON" },
    { "Joy2 3rd Button/Mouse2 Middle Button", "JOY2_3RD_BUTTON" },
    { "Joy2 CD32 Play", "JOY2_CD32_PLAY" },
    { "Joy2 CD32 RWD", "JOY2_CD32_RWD" },
    { "Joy2 CD32 FFW", "JOY2_CD32_FFW" },
    { "Joy2 CD32 Green", "JOY2_CD32_GREEN" },
    { "Joy2 CD32 Yellow", "JOY2_CD32_YELLOW" },
    { "Joy2 CD32 Red", "JOY2_CD32_RED" },
    { "Joy2 CD32 Blue", "JOY2_CD32_BLUE" },
    { "Mouse2 Horizontal", "MOUSE2_HORIZ" },
    { "Mouse2 Vertical", "MOUSE2_VERT" },
    { "Mouse2 Horizontal (inverted)", "MOUSE2_HORIZ_INV" },
    { "Mouse2 Vertical (inverted)", "MOUSE2_VERT_INV" },
    { "Mouse2 Up", "MOUSE2_UP" },
    { "Mouse2 Down", "MOUSE2_DOWN" },
    { "Mouse2 Left", "MOUSE2_LEFT" },
    { "Mouse2 Right", "MOUSE2_RIGHT" },
    { "Parallel Joy1 Horizontal", "PAR_JOY1_HORIZ" },
    { "Parallel Joy1 Vertical", "PAR_JOY1_VERT" },
    { "Parallel Joy1 Left", "PAR_JOY1_LEFT" },
    { "Parallel Joy1 Right", "PAR_JOY1_RIGHT" },
    { "Parallel Joy1 Up", "PAR_JOY1_UP" },
    { "Parallel Joy1 Down", "PAR_JOY1_DOWN" },
    { "Parallel Joy1 Fire Button", "PAR_JOY1_FIRE_BUTTON" },
    { "Parallel Joy1 Spare/2nd Button", "PAR_JOY1_2ND_BUTTON" },
    { "Parallel Joy2 Horizontal", "PAR_JOY2_HORIZ" },
    { "Parallel Joy2 Vertical", "PAR_JOY2_VERT" },
    { "Parallel Joy2 Left", "PAR_JOY2_LEFT" },
    { "Parallel Joy2 Right", "PAR_JOY2_RIGHT" },
    { "Parallel Joy2 Up", "PAR_JOY2_UP" },
    { "Parallel Joy2 Down", "PAR_JOY2_DOWN" },
    { "Parallel Joy2 Fire Button", "PAR_JOY2_FIRE_BUTTON" },
    { "Parallel Joy2 Spare/2nd Button", "PAR_JOY2_2ND_BUTTON" },
    { "Enter GUI", "SPC_ENTERGUI" },
    { "Screenshot (file)", "SPC_SCREENSHOT" },
    { "Pause emulation", "SPC_PAUSE" },
    { "Warp mode", "SPC_WARP" },
    { "Quit emulator", "SPC_QUIT" },
    { "Reset emulation", "SPC_SOFTRESET" },
    { "Hard reset emulation", "SPC_HARDRESET" },
    { "Quick save state", "SPC_STATESAVE" },
    { "Quick restore state", "SPC_STATERESTORE" },
    { "Toggle windowed/fullscreen", "SPC_TOGGLEFULLSCREEN" },
    { "Toggle mouse grab", "SPC_TOGGLEMOUSEGRAB" },
    { "Swap joystick ports", "SPC_SWAPJOYPORTS" },
    { "Paste from host clipboard", "SPC_PASTE" }
};

struct WinUaeQtInputSlot {
    QString event;
    int flags = 0;
    QString qualifiers;
    QString suffix;
    bool custom = false;
};

struct WinUaeQtInputRow {
    QString label;
    QString key;
    bool editable = false;
};

struct WinUaeQtKeyboardChoice {
    const char *display;
    int scancode;
    const char *defaultEvent;
};

static const WinUaeQtKeyboardChoice keyboardChoices[] = {
    { "Escape", 41, "KEY_ESC" },
    { "F1", 58, "KEY_F1" },
    { "F2", 59, "KEY_F2" },
    { "F3", 60, "KEY_F3" },
    { "F4", 61, "KEY_F4" },
    { "F5", 62, "KEY_F5" },
    { "F6", 63, "KEY_F6" },
    { "F7", 64, "KEY_F7" },
    { "F8", 65, "KEY_F8" },
    { "F9", 66, "KEY_F9" },
    { "F10", 67, "KEY_F10" },
    { "F11", 68, "" },
    { "F12", 69, "" },
    { "1", 30, "KEY_1" },
    { "2", 31, "KEY_2" },
    { "3", 32, "KEY_3" },
    { "4", 33, "KEY_4" },
    { "5", 34, "KEY_5" },
    { "6", 35, "KEY_6" },
    { "7", 36, "KEY_7" },
    { "8", 37, "KEY_8" },
    { "9", 38, "KEY_9" },
    { "0", 39, "KEY_0" },
    { "Tab", 43, "KEY_TAB" },
    { "A", 4, "KEY_A" },
    { "B", 5, "KEY_B" },
    { "C", 6, "KEY_C" },
    { "D", 7, "KEY_D" },
    { "E", 8, "KEY_E" },
    { "F", 9, "KEY_F" },
    { "G", 10, "KEY_G" },
    { "H", 11, "KEY_H" },
    { "I", 12, "KEY_I" },
    { "J", 13, "KEY_J" },
    { "K", 14, "KEY_K" },
    { "L", 15, "KEY_L" },
    { "M", 16, "KEY_M" },
    { "N", 17, "KEY_N" },
    { "O", 18, "KEY_O" },
    { "P", 19, "KEY_P" },
    { "Q", 20, "KEY_Q" },
    { "R", 21, "KEY_R" },
    { "S", 22, "KEY_S" },
    { "T", 23, "KEY_T" },
    { "U", 24, "KEY_U" },
    { "V", 25, "KEY_V" },
    { "W", 26, "KEY_W" },
    { "X", 27, "KEY_X" },
    { "Y", 28, "KEY_Y" },
    { "Z", 29, "KEY_Z" },
    { "Caps Lock", 57, "KEY_CAPS_LOCK" },
    { "Numpad 1", 89, "KEY_NP_1" },
    { "Numpad 2", 90, "KEY_NP_2" },
    { "Numpad 3", 91, "KEY_NP_3" },
    { "Numpad 4", 92, "KEY_NP_4" },
    { "Numpad 5", 93, "KEY_NP_5" },
    { "Numpad 6", 94, "KEY_NP_6" },
    { "Numpad 7", 95, "KEY_NP_7" },
    { "Numpad 8", 96, "KEY_NP_8" },
    { "Numpad 9", 97, "KEY_NP_9" },
    { "Numpad 0", 98, "KEY_NP_0" },
    { "Numpad Period", 99, "KEY_NP_PERIOD" },
    { "Numpad Plus", 87, "KEY_NP_ADD" },
    { "Numpad Minus", 86, "KEY_NP_SUB" },
    { "Numpad Multiply", 85, "KEY_NP_MUL" },
    { "Numpad Divide", 84, "KEY_NP_DIV" },
    { "Numpad Enter", 88, "KEY_ENTER" },
    { "Minus", 45, "KEY_SUB" },
    { "Equals", 46, "KEY_EQUALS" },
    { "Backspace", 42, "KEY_BACKSPACE" },
    { "Return", 40, "KEY_RETURN" },
    { "Space", 44, "KEY_SPACE" },
    { "Left Shift", 225, "KEY_SHIFT_LEFT" },
    { "Left Ctrl", 224, "KEY_CTRL" },
    { "Left Amiga", 227, "KEY_AMIGA_LEFT" },
    { "Left Alt", 226, "KEY_ALT_LEFT" },
    { "Right Alt", 230, "KEY_ALT_RIGHT" },
    { "Right Amiga", 231, "KEY_AMIGA_RIGHT" },
    { "Right Ctrl", 228, "KEY_CTRL" },
    { "Right Shift", 229, "KEY_SHIFT_RIGHT" },
    { "Cursor Up", 82, "KEY_CURSOR_UP" },
    { "Cursor Down", 81, "KEY_CURSOR_DOWN" },
    { "Cursor Left", 80, "KEY_CURSOR_LEFT" },
    { "Cursor Right", 79, "KEY_CURSOR_RIGHT" },
    { "Insert", 73, "" },
    { "Delete", 76, "KEY_DEL" },
    { "Home", 74, "" },
    { "End", 77, "" },
    { "Page Up", 75, "" },
    { "Page Down", 78, "" },
    { "Back Quote", 53, "KEY_BACKQUOTE" },
    { "Left Bracket", 47, "KEY_LEFTBRACKET" },
    { "Right Bracket", 48, "KEY_RIGHTBRACKET" },
    { "Semicolon", 51, "KEY_SEMICOLON" },
    { "Single Quote", 52, "KEY_SINGLEQUOTE" },
    { "Backslash", 49, "KEY_BACKSLASH" },
    { "Numbersign", 50, "KEY_NUMBERSIGN" },
    { "Non-US Backslash", 100, "KEY_BACKSLASH" },
    { "Comma", 54, "KEY_COMMA" },
    { "Period", 55, "KEY_PERIOD" },
    { "Slash", 56, "KEY_DIV" },
    { "F13", 104, "" },
    { "F14", 105, "" },
    { "F15", 106, "" }
};

struct QuickstartModelChoice {
    const char *display;
    const char *configValue;
    const char *compatible;
    int compatibilityLevels;
    int configCount;
    const char *configs[MaxQuickstartConfigs];
};

static const QuickstartModelChoice quickstartModelChoices[] = {
    { "A500", "A500", "A500", 4, 6, {
        "1.3 ROM, OCS, 512 KB Chip + 512 KB Slow RAM (most common)",
        "1.3 ROM, ECS Agnus, 512 KB Chip RAM + 512 KB Slow RAM",
        "1.3 ROM, ECS Agnus, 1 MB Chip RAM",
        "1.3 ROM, OCS Agnus, 512 KB Chip RAM",
        "1.2 ROM, OCS Agnus, 512 KB Chip RAM",
        "1.2 ROM, OCS Agnus, 512 KB Chip RAM + 512 KB Slow RAM"
    } },
    { "A500+", "A500+", "A500+", 4, 3, {
        "Basic non-expanded configuration",
        "2 MB Chip RAM expanded configuration",
        "4 MB Fast RAM expanded configuration",
        nullptr,
        nullptr,
        nullptr
    } },
    { "A600", "A600", "A600", 4, 3, {
        "Basic non-expanded configuration",
        "2 MB Chip RAM expanded configuration",
        "4 MB Fast RAM expanded configuration",
        nullptr,
        nullptr,
        nullptr
    } },
    { "A1000", "A1000", "A1000", 4, 4, {
        "512 KB Chip RAM",
        "ICS Denise without EHB support",
        "256 KB Chip RAM",
        "A1000 Velvet Prototype",
        nullptr,
        nullptr
    } },
    { "A1200", "A1200", "A1200", 5, 6, {
        "Basic non-expanded configuration",
        "4 MB Fast RAM expanded configuration",
        "Blizzard 1230 IV",
        "Blizzard 1240",
        "Blizzard 1260",
        "Blizzard PPC"
    } },
    { "A3000", "A3000", "A3000", 2, 3, {
        "1.4 ROM, 2MB Chip + 8MB Fast",
        "2.04 ROM, 2MB Chip + 8MB Fast",
        "3.1 ROM, 2MB Chip + 8MB Fast",
        nullptr,
        nullptr,
        nullptr
    } },
    { "A4000", "A4000", "A4000", 1, 3, {
        "68030, 3.1 ROM, 2MB Chip + 8MB Fast",
        "68040, 3.1 ROM, 2MB Chip + 8MB Fast",
        "CyberStorm PPC",
        nullptr,
        nullptr,
        nullptr
    } },
    { "CD32", "CD32", "CD32", 4, 3, {
        "CD32",
        "CD32 with Full Motion Video cartridge",
        "Cubo CD32",
        nullptr,
        nullptr,
        nullptr
    } },
    { "CDTV", "CDTV", "CDTV", 4, 3, {
        "CDTV",
        "Floppy drive and 64KB SRAM card expanded CDTV",
        "CDTV-CR",
        nullptr,
        nullptr,
        nullptr
    } }
};

struct WinUaeQtCdSlot {
    QString path;
    QString type;
    bool inUse = false;
};

struct WinUaeQtRomBoard {
    QString start;
    QString end;
    QString path;
};

struct WinUaeQtExpansionBoardState {
    bool present = false;
    QString romFile;
    QString rawOptions;
    QString subtype;
    QMap<QString, bool> optionBools;
    QMap<QString, QString> optionValues;
    bool autobootDisabled = false;
    bool dma24Bit = false;
    bool inserted = false;
    int id = 7;
};

struct WinUaeQtFilterState {
    QString filter = QStringLiteral("none");
    QString modeH = QStringLiteral("1x");
    QString modeV = QStringLiteral("-");
    QString autoscale = QStringLiteral("auto");
    QString integerLimit = QStringLiteral("1/1");
    QString keepAspect = QStringLiteral("none");
    bool enable = true;
    bool keepAutoscaleAspect = false;
    bool bilinear = false;
    double horizZoom = 0.0;
    double vertZoom = 0.0;
    double horizZoomMult = 1.0;
    double vertZoomMult = 1.0;
    double horizOffset = 0.0;
    double vertOffset = 0.0;
    int scanlines = 0;
    int scanlineLevel = 0;
    int scanlineOffset = 1;
    int luminance = 0;
    int contrast = 0;
    int saturation = 0;
    int gamma = 0;
    int blur = 0;
    int noise = 0;
};

enum MountDataRole {
    MountKindRole = Qt::UserRole,
    MountDeviceRole,
    MountVolumeRole,
    MountPathRole,
    MountReadOnlyRole,
    MountBootPriRole,
    MountEmuUnitRole,
    MountRawConfigRole,
    MountHardfileGeometryRole,
    MountHardfileTailRole
};

enum WinUaeQtMountControllerBus {
    MountControllerBusUnknown,
    MountControllerBusUae,
    MountControllerBusIde,
    MountControllerBusScsi
};

struct WinUaeQtMountControllerChoice {
    QString display;
    QString boardKey;
    WinUaeQtMountControllerBus bus = MountControllerBusUnknown;
    int maxUnit = 0;
    bool valid = false;
};

static QStringList mountControllerParts(QString tail)
{
    if (!tail.startsWith(QLatin1Char(','))) {
        tail.prepend(QLatin1Char(','));
    }
    QStringList parts = winUaeQtConfigFieldList(tail);
    while (parts.size() < 2) {
        parts.append(QString());
    }
    return parts;
}

static QString mountControllerValue(const WinUaeQtMountEntry &entry, const QString &fallback)
{
    const QStringList parts = mountControllerParts(entry.hardfileTail);
    return parts.value(1).isEmpty() ? fallback : parts.value(1);
}

static QString mountTailWithController(const WinUaeQtMountEntry &entry, const QString &controller)
{
    QStringList parts = mountControllerParts(entry.hardfileTail);
    parts[1] = controller;
    return winUaeQtConfigJoinFields(parts);
}

static QString mountControllerBaseDisplay(const QString &display)
{
    const int start = display.lastIndexOf(QStringLiteral(" ["));
    if (start < 0 || !display.endsWith(QLatin1Char(']'))) {
        return display;
    }
    bool ok = false;
    display.mid(start + 2, display.size() - start - 3).toInt(&ok);
    return ok ? display.left(start) : display;
}

static int mountControllerDuplicateFromDisplay(const QString &display)
{
    const int start = display.lastIndexOf(QStringLiteral(" ["));
    if (start < 0 || !display.endsWith(QLatin1Char(']'))) {
        return 0;
    }
    bool ok = false;
    const int oneBased = display.mid(start + 2, display.size() - start - 3).toInt(&ok);
    return ok && oneBased >= 2 ? qBound(1, oneBased - 1, 8) : 0;
}

static QString mountControllerDuplicateDisplaySuffix(int duplicate)
{
    return duplicate > 0 ? QStringLiteral(" [%1]").arg(duplicate + 1) : QString();
}

static QString mountControllerDuplicateConfigSuffix(int duplicate)
{
    return duplicate > 0 ? QStringLiteral("-%1").arg(duplicate + 1) : QString();
}

static WinUaeQtMountControllerBus mountControllerBusFromConfigValue(const QString &value)
{
    const QString lower = value.trimmed().toLower();
    if (lower.startsWith(QStringLiteral("uae"))) {
        return MountControllerBusUae;
    }
    if (lower.startsWith(QStringLiteral("ide"))) {
        return MountControllerBusIde;
    }
    if (lower.startsWith(QStringLiteral("scsi"))) {
        return MountControllerBusScsi;
    }
    return MountControllerBusUnknown;
}

static QString mountControllerExpansionKeyFromConfigValue(const QString &value)
{
    const int underscore = value.indexOf(QLatin1Char('_'));
    if (underscore < 0) {
        return QString();
    }
    QString key = value.mid(underscore + 1).toLower();
    const int dash = key.lastIndexOf(QLatin1Char('-'));
    if (dash > 0) {
        bool ok = false;
        const int duplicate = key.mid(dash + 1).toInt(&ok);
        if (ok && duplicate >= 2) {
            key = key.left(dash);
        }
    }
    return key;
}

static int mountControllerDuplicateFromConfigValue(const QString &value)
{
    const int underscore = value.indexOf(QLatin1Char('_'));
    const int dash = value.lastIndexOf(QLatin1Char('-'));
    if (underscore < 0 || dash <= underscore) {
        return 0;
    }
    bool ok = false;
    const int oneBased = value.mid(dash + 1).toInt(&ok);
    return ok && oneBased >= 2 ? qBound(1, oneBased - 1, 8) : 0;
}

static QString mountControllerBusName(WinUaeQtMountControllerBus bus)
{
    if (bus == MountControllerBusIde) {
        return QStringLiteral("IDE");
    }
    if (bus == MountControllerBusScsi) {
        return QStringLiteral("SCSI");
    }
    return QString();
}

static QString mountControllerBoardBaseName(const WinUaeQtExpansionBoardCatalogItem &board)
{
    const int manufacturer = board.display.lastIndexOf(QStringLiteral(" ("));
    if (manufacturer > 0 && board.display.endsWith(QLatin1Char(')'))) {
        return board.display.left(manufacturer);
    }
    return board.display;
}

static bool mountControllerBoardSupportsBus(
    const WinUaeQtExpansionBoardCatalogItem &board,
    WinUaeQtMountControllerBus bus)
{
    if (bus == MountControllerBusIde) {
        return board.categoryMask & WinUaeQtExpansionCategoryIde;
    }
    if (bus == MountControllerBusScsi) {
        return board.categoryMask & WinUaeQtExpansionCategoryScsi;
    }
    return false;
}

static int mountControllerBoardMaxUnit(
    const WinUaeQtExpansionBoardCatalogItem &board,
    WinUaeQtMountControllerBus bus)
{
    if (bus == MountControllerBusIde) {
        return qMax(1, 1 + board.extraHdPorts);
    }
    if (bus == MountControllerBusScsi
        && (board.categoryMask & (WinUaeQtExpansionCategorySasi | WinUaeQtExpansionCategoryCustom))) {
        return 1;
    }
    return MaxControllerUnits - 1;
}

static WinUaeQtMountControllerChoice mountControllerGenericChoice(WinUaeQtMountControllerBus bus)
{
    WinUaeQtMountControllerChoice choice;
    choice.bus = bus;
    choice.valid = true;
    if (bus == MountControllerBusUae) {
        choice.display = QStringLiteral("UAE (uaehf.device)");
        choice.maxUnit = MaxControllerUnits - 1;
    } else if (bus == MountControllerBusIde) {
        choice.display = QStringLiteral("IDE (Auto)");
        choice.maxUnit = 3;
    } else if (bus == MountControllerBusScsi) {
        choice.display = QStringLiteral("SCSI (Auto)");
        choice.maxUnit = MaxControllerUnits - 1;
    } else {
        choice.valid = false;
        choice.maxUnit = 0;
    }
    return choice;
}

static WinUaeQtMountControllerChoice mountControllerChoiceForBoard(
    const WinUaeQtExpansionBoardCatalogItem &board,
    WinUaeQtMountControllerBus bus)
{
    WinUaeQtMountControllerChoice choice;
    if (!mountControllerBoardSupportsBus(board, bus)) {
        return choice;
    }
    choice.display = QStringLiteral("%1 (%2)")
        .arg(mountControllerBoardBaseName(board), mountControllerBusName(bus));
    choice.boardKey = board.key;
    choice.bus = bus;
    choice.maxUnit = mountControllerBoardMaxUnit(board, bus);
    choice.valid = true;
    return choice;
}

static WinUaeQtMountControllerChoice mountControllerChoiceByDisplay(
    const WinUaeQtBoardCatalog &catalog,
    const QString &display)
{
    const QString baseDisplay = mountControllerBaseDisplay(display);
    for (WinUaeQtMountControllerBus bus : { MountControllerBusUae, MountControllerBusIde, MountControllerBusScsi }) {
        WinUaeQtMountControllerChoice choice = mountControllerGenericChoice(bus);
        if (choice.valid && baseDisplay == choice.display) {
            return choice;
        }
    }
    for (const WinUaeQtExpansionBoardCatalogItem &board : catalog.expansionBoards) {
        for (WinUaeQtMountControllerBus bus : { MountControllerBusIde, MountControllerBusScsi }) {
            WinUaeQtMountControllerChoice choice = mountControllerChoiceForBoard(board, bus);
            if (choice.valid && baseDisplay == choice.display) {
                return choice;
            }
        }
    }
    return {};
}

static WinUaeQtMountControllerChoice mountControllerChoiceByKeyAndBus(
    const WinUaeQtBoardCatalog &catalog,
    const QString &key,
    WinUaeQtMountControllerBus bus)
{
    for (const WinUaeQtExpansionBoardCatalogItem &board : catalog.expansionBoards) {
        if (key.compare(board.key, Qt::CaseInsensitive) == 0) {
            return mountControllerChoiceForBoard(board, bus);
        }
    }
    return {};
}

static QString mountControllerGenericDisplay(WinUaeQtMountControllerBus bus)
{
    if (bus == MountControllerBusIde) {
        return QStringLiteral("IDE (Auto)");
    }
    if (bus == MountControllerBusScsi) {
        return QStringLiteral("SCSI (Auto)");
    }
    return QStringLiteral("UAE (uaehf.device)");
}

static QString mountControllerDisplayForConfigValue(const WinUaeQtBoardCatalog &catalog, const QString &value)
{
    const QString trimmed = value.trimmed();
    const WinUaeQtMountControllerBus bus = mountControllerBusFromConfigValue(trimmed);
    const QString key = mountControllerExpansionKeyFromConfigValue(trimmed);
    if (!key.isEmpty()) {
        const WinUaeQtMountControllerChoice choice = mountControllerChoiceByKeyAndBus(catalog, key, bus);
        if (choice.valid) {
            return choice.display + mountControllerDuplicateDisplaySuffix(mountControllerDuplicateFromConfigValue(trimmed));
        }
        return trimmed;
    }
    return mountControllerGenericDisplay(bus);
}

static WinUaeQtMountControllerBus mountControllerBusFromDisplay(const WinUaeQtBoardCatalog &catalog, const QString &display)
{
    const WinUaeQtMountControllerChoice choice = mountControllerChoiceByDisplay(catalog, display);
    if (choice.valid) {
        return choice.bus;
    }
    return mountControllerBusFromConfigValue(display);
}

static QString mountControllerFamily(
    const WinUaeQtBoardCatalog &catalog,
    const WinUaeQtMountEntry &entry,
    const QString &fallback)
{
    return mountControllerDisplayForConfigValue(catalog, mountControllerValue(entry, fallback));
}

static int mountControllerUnit(const WinUaeQtMountEntry &entry, const QString &fallback)
{
    const QString value = mountControllerValue(entry, fallback).toLower();
    int index = 0;
    while (index < value.size() && !value.at(index).isDigit()) {
        index++;
    }
    if (index >= value.size()) {
        return 0;
    }
    int end = index;
    while (end < value.size() && value.at(end).isDigit()) {
        end++;
    }
    return qBound(0, value.mid(index, end - index).toInt(), MaxControllerUnits - 1);
}

static QString mountControllerConfigPrefix(WinUaeQtMountControllerBus bus)
{
    if (bus == MountControllerBusIde) {
        return QStringLiteral("ide");
    }
    if (bus == MountControllerBusScsi) {
        return QStringLiteral("scsi");
    }
    return QStringLiteral("uae");
}

static int mountControllerMaxUnit(const WinUaeQtBoardCatalog &catalog, const QString &display)
{
    const WinUaeQtMountControllerChoice choice = mountControllerChoiceByDisplay(catalog, display);
    if (choice.valid) {
        return choice.maxUnit;
    }
    return mountControllerBusFromDisplay(catalog, display) == MountControllerBusIde ? 3 : MaxControllerUnits - 1;
}

static QString mountControllerConfigValue(const WinUaeQtBoardCatalog &catalog, const QString &display, int unit)
{
    const int duplicate = mountControllerDuplicateFromDisplay(display);
    const WinUaeQtMountControllerChoice choice = mountControllerChoiceByDisplay(catalog, display);
    if (choice.valid) {
        QString value = mountControllerConfigPrefix(choice.bus) + QString::number(qBound(0, unit, choice.maxUnit));
        if (!choice.boardKey.isEmpty()) {
            value += QStringLiteral("_") + choice.boardKey + mountControllerDuplicateConfigSuffix(duplicate);
        }
        return value;
    }

    const WinUaeQtMountControllerBus bus = mountControllerBusFromConfigValue(display);
    const QString key = mountControllerExpansionKeyFromConfigValue(display);
    QString value = mountControllerConfigPrefix(bus) + QString::number(qBound(0, unit, mountControllerMaxUnit(catalog, display)));
    if (!key.isEmpty()) {
        value += QStringLiteral("_") + key + mountControllerDuplicateConfigSuffix(mountControllerDuplicateFromConfigValue(display));
    }
    return value;
}

static QString mountControllerDisplay(const WinUaeQtBoardCatalog &catalog, const WinUaeQtMountEntry &entry)
{
    const QString fallback = entry.kind == QStringLiteral("cd") ? QStringLiteral("ide0") : QStringLiteral("uae0");
    const QString value = mountControllerValue(entry, fallback);
    const QString display = mountControllerDisplayForConfigValue(catalog, value);
    const WinUaeQtMountControllerChoice choice = mountControllerChoiceByDisplay(catalog, display);
    if (choice.valid && !choice.boardKey.isEmpty()) {
        return QStringLiteral("%1:%2").arg(mountControllerBaseDisplay(display)).arg(mountControllerUnit(entry, fallback));
    }
    const QString upper = value.toUpper();
    if (upper.startsWith(QStringLiteral("IDE"))) {
        return QStringLiteral("IDE:%1").arg(mountControllerUnit(entry, fallback));
    }
    if (upper.startsWith(QStringLiteral("SCSI"))) {
        return QStringLiteral("SCSI:%1").arg(mountControllerUnit(entry, fallback));
    }
    if (upper.startsWith(QStringLiteral("UAE"))) {
        return QStringLiteral("UAE:%1").arg(mountControllerUnit(entry, fallback));
    }
    return value;
}

static bool isIntegerText(const QString &value)
{
    bool ok = false;
    value.toInt(&ok);
    return ok;
}

static QStringList hardfileGeometryParts(const WinUaeQtMountEntry &entry)
{
    QStringList parts = entry.hardfileGeometry.split(QLatin1Char(','));
    while (parts.size() < 4) {
        parts.append(QStringLiteral("0"));
    }
    return parts;
}

static bool hardfileIsRdb(const WinUaeQtMountEntry &entry)
{
    const QStringList geometry = hardfileGeometryParts(entry);
    return geometry.value(0).toInt() == 0
        && geometry.value(1).toInt() == 0
        && geometry.value(2).toInt() == 0;
}

static bool hardfileHasPhysicalGeometry(const WinUaeQtMountEntry &entry)
{
    const QStringList parts = mountControllerParts(entry.hardfileTail);
    return parts.size() > 3
        && isIntegerText(parts.value(2))
        && parts.value(3).contains(QLatin1Char('/'));
}

static QStringList hardfilePhysicalGeometryParts(const WinUaeQtMountEntry &entry)
{
    QStringList geometry;
    if (hardfileHasPhysicalGeometry(entry)) {
        geometry = mountControllerParts(entry.hardfileTail).value(3).split(QLatin1Char('/'));
    }
    while (geometry.size() < 3) {
        geometry.append(QStringLiteral("0"));
    }
    return geometry;
}

static QString hardfileTailGeometryFile(const WinUaeQtMountEntry &entry)
{
    const QStringList parts = mountControllerParts(entry.hardfileTail);
    return hardfileHasPhysicalGeometry(entry) ? parts.value(4) : QString();
}

static int hardfileTailExtraStart(const WinUaeQtMountEntry &entry)
{
    return hardfileHasPhysicalGeometry(entry) ? 5 : 2;
}

static bool isManagedHardfileTailToken(const QString &token)
{
    static const QStringList managed = {
        QStringLiteral("HD"),
        QStringLiteral("CF"),
        QStringLiteral("SCSI1"),
        QStringLiteral("SCSI2"),
        QStringLiteral("SASIE"),
        QStringLiteral("SASI"),
        QStringLiteral("SASI_CHS"),
        QStringLiteral("ATA1"),
        QStringLiteral("ATA2+"),
        QStringLiteral("ATA2+S")
    };
    for (const QString &item : managed) {
        if (token.compare(item, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

static QStringList hardfilePreservedTailExtras(const WinUaeQtMountEntry &entry)
{
    const QStringList parts = mountControllerParts(entry.hardfileTail);
    QStringList extras;
    for (int i = hardfileTailExtraStart(entry); i < parts.size(); i++) {
        if (!parts.value(i).isEmpty() && !isManagedHardfileTailToken(parts.value(i))) {
            extras.append(parts.value(i));
        }
    }
    return extras;
}

static bool hardfileTailHasToken(const WinUaeQtMountEntry &entry, const QString &token)
{
    const QStringList parts = mountControllerParts(entry.hardfileTail);
    for (int i = hardfileTailExtraStart(entry); i < parts.size(); i++) {
        if (parts.value(i).compare(token, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

static QString hardfileFeatureText(
    const WinUaeQtBoardCatalog &catalog,
    const WinUaeQtMountEntry &entry,
    const QString &controllerFamily)
{
    const WinUaeQtMountControllerBus bus = mountControllerBusFromDisplay(catalog, controllerFamily);
    if (bus == MountControllerBusIde) {
        if (hardfileTailHasToken(entry, QStringLiteral("ATA2+S"))) {
            return QStringLiteral("ATA-2+ Strict");
        }
        if (hardfileTailHasToken(entry, QStringLiteral("ATA2+"))) {
            return QStringLiteral("ATA-2+");
        }
        if (hardfileTailHasToken(entry, QStringLiteral("ATA1"))) {
            return QStringLiteral("ATA-1");
        }
    } else if (bus == MountControllerBusScsi) {
        if (hardfileTailHasToken(entry, QStringLiteral("SASI_CHS"))) {
            return QStringLiteral("SASI CHS");
        }
        if (hardfileTailHasToken(entry, QStringLiteral("SASI"))) {
            return QStringLiteral("SASI");
        }
        if (hardfileTailHasToken(entry, QStringLiteral("SCSI1"))) {
            return QStringLiteral("SCSI-1");
        }
        if (hardfileTailHasToken(entry, QStringLiteral("SCSI2"))) {
            return QStringLiteral("SCSI-2");
        }
    }
    return QStringLiteral("Default");
}

static QString hardfileFeatureToken(const QString &featureText)
{
    if (featureText == QStringLiteral("ATA-1")) {
        return QStringLiteral("ATA1");
    }
    if (featureText == QStringLiteral("ATA-2+ Strict")) {
        return QStringLiteral("ATA2+S");
    }
    if (featureText == QStringLiteral("SCSI-1")) {
        return QStringLiteral("SCSI1");
    }
    if (featureText == QStringLiteral("SASI")) {
        return QStringLiteral("SASI");
    }
    if (featureText == QStringLiteral("SASI CHS")) {
        return QStringLiteral("SASI_CHS");
    }
    return QString();
}

static WinUaeQtCdSlot cdSlotFromConfigValue(const QString &value)
{
    WinUaeQtCdSlot slot;
    if (value.compare(QStringLiteral("autodetect"), Qt::CaseInsensitive) == 0) {
        slot.type = QStringLiteral("Autodetect");
        slot.inUse = true;
        return slot;
    }
    if (value.isEmpty()
        || value.compare(QStringLiteral("empty"), Qt::CaseInsensitive) == 0
        || value == QStringLiteral(".")) {
        slot.type = QStringLiteral("Image file");
        return slot;
    }

    const QStringList fields = winUaeQtConfigFieldList(value);
    slot.path = fields.value(0);
    slot.type = fields.value(1).compare(QStringLiteral("image"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("Image file")
        : QStringLiteral("Image file");
    slot.inUse = !slot.path.isEmpty();
    return slot;
}

static QString cdSlotConfigValue(const WinUaeQtCdSlot &slot)
{
    if (!slot.inUse && slot.path.isEmpty()) {
        return QString();
    }
    if (slot.type == QStringLiteral("Autodetect") && slot.path.isEmpty()) {
        return QStringLiteral("autodetect");
    }
    if (slot.path.isEmpty()) {
        return QString();
    }
    return winUaeQtConfigEscapeMin(slot.path);
}

static QString normalizedRomAddress(QString text, bool endAddress)
{
    text = text.trimmed();
    if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        text = text.mid(2);
    }
    bool ok = false;
    quint32 value = text.toUInt(&ok, 16);
    if (!ok) {
        return QString();
    }
    if (endAddress && value == 0) {
        return QString();
    }
    if (endAddress) {
        value = ((value - 1) & ~quint32(0xffff)) | quint32(0xffff);
    } else {
        value &= ~quint32(0xffff);
    }
    return QStringLiteral("%1").arg(value, 8, 16, QLatin1Char('0'));
}

static WinUaeQtRomBoard romBoardFromConfigValue(const QString &value)
{
    WinUaeQtRomBoard board;
    for (const QString &field : winUaeQtConfigFieldList(value)) {
        const int equals = field.indexOf(QLatin1Char('='));
        if (equals <= 0) {
            continue;
        }
        const QString key = field.left(equals).trimmed().toLower();
        const QString fieldValue = field.mid(equals + 1).trimmed();
        if (key == QStringLiteral("start")) {
            board.start = normalizedRomAddress(fieldValue, false);
        } else if (key == QStringLiteral("end")) {
            board.end = normalizedRomAddress(fieldValue, true);
        } else if (key == QStringLiteral("file")) {
            board.path = fieldValue;
        }
    }
    return board;
}

static QString romBoardConfigValue(const WinUaeQtRomBoard &board)
{
    const QString start = normalizedRomAddress(board.start, false);
    const QString end = normalizedRomAddress(board.end, true);
    if (start.isEmpty() || end.isEmpty()) {
        return QString();
    }
    QString value = QStringLiteral("start=%1,end=%2").arg(start, end);
    if (!board.path.trimmed().isEmpty()) {
        value += QStringLiteral(",file=%1").arg(winUaeQtConfigEscapeMin(board.path.trimmed()));
    }
    return value;
}

static int romBoardIndexFromKey(const QString &key)
{
    if (key == QStringLiteral("romboard_options")) {
        return 0;
    }
    if (!key.startsWith(QStringLiteral("romboard")) || !key.endsWith(QStringLiteral("_options"))) {
        return -1;
    }
    bool ok = false;
    const int board = key.mid(8, key.size() - 16).toInt(&ok);
    if (!ok) {
        return -1;
    }
    return board - 1;
}

static QString romBoardKey(int index)
{
    return index == 0
        ? QStringLiteral("romboard_options")
        : QStringLiteral("romboard%1_options").arg(index + 1);
}

static int floppyKeyDrive(const QString &key, const QString &suffix = QString())
{
    if (!key.startsWith(QStringLiteral("floppy"))) {
        return -1;
    }
    if (key.size() != 7 + suffix.size() || !key.endsWith(suffix)) {
        return -1;
    }
    const int drive = key.at(6).digitValue();
    return drive >= 0 && drive < 4 ? drive : -1;
}

static QString uaeBoardConfigValue(const QString &text)
{
    if (text == QStringLiteral("New UAE (64k + F0 ROM)")) {
        return QStringLiteral("min");
    }
    if (text == QStringLiteral("New UAE (128k, ROM, Direct)")) {
        return QStringLiteral("full");
    }
    if (text == QStringLiteral("New UAE (128k, ROM, Indirect)")) {
        return QStringLiteral("full+indirect");
    }
    return QStringLiteral("disabled");
}

static QString uaeBoardText(const QString &value)
{
    const QString lower = value.toLower();
    if (lower == QStringLiteral("min") || lower == QStringLiteral("min_off")) {
        return QStringLiteral("New UAE (64k + F0 ROM)");
    }
    if (lower == QStringLiteral("full") || lower == QStringLiteral("full_off")) {
        return QStringLiteral("New UAE (128k, ROM, Direct)");
    }
    if (lower == QStringLiteral("full+indirect") || lower == QStringLiteral("full+indirect_off")) {
        return QStringLiteral("New UAE (128k, ROM, Indirect)");
    }
    return QStringLiteral("Original UAE (FS + F0 ROM)");
}

static QString fullscreenModeConfigValue(const QString &text)
{
    if (text == QStringLiteral("Fullscreen")) {
        return QStringLiteral("true");
    }
    if (text == QStringLiteral("Full-window")) {
        return QStringLiteral("fullwindow");
    }
    return QStringLiteral("false");
}

static QString fullscreenModeText(const QString &value)
{
    if (value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Fullscreen");
    }
    if (value.compare(QStringLiteral("fullwindow"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Full-window");
    }
    return QStringLiteral("Windowed");
}

static QString lineModeConfigValue(int normalId, int interlacedId)
{
    static const char *lineModes[] = {
        "none",
        "double", "scanlines", "scanlines2p", "scanlines3p",
        "double2", "scanlines2", "scanlines2p2", "scanlines2p3",
        "double3", "scanlines3", "scanlines3p2", "scanlines3p3"
    };
    if (normalId <= 0) {
        return QStringLiteral("none");
    }
    const int progressiveScanlines = normalId == 2 ? 1 : (normalId == 3 ? 2 : (normalId == 4 ? 3 : 0));
    const int interlacedScanlines = interlacedId == 2 ? 1 : (interlacedId == 3 ? 2 : 0);
    const int index = qBound(0, interlacedScanlines * 4 + progressiveScanlines + 1, int(sizeof(lineModes) / sizeof(lineModes[0])) - 1);
    return QString::fromLatin1(lineModes[index]);
}

static int progressiveLineModeId(const QString &value)
{
    const QString lower = value.toLower();
    if (lower == QStringLiteral("none")) {
        return 0;
    }
    if (lower.contains(QStringLiteral("scanlines3p"))) {
        return 4;
    }
    if (lower.contains(QStringLiteral("scanlines2p"))) {
        return 3;
    }
    if (lower.contains(QStringLiteral("scanlines"))) {
        return 2;
    }
    return 1;
}

static int interlacedLineModeId(const QString &value)
{
    const QString lower = value.toLower();
    if (lower == QStringLiteral("none")) {
        return 0;
    }
    if (lower.endsWith(QLatin1Char('3'))) {
        return 3;
    }
    if (lower.endsWith(QLatin1Char('2'))) {
        return 2;
    }
    return 1;
}

static QString nativeVsyncConfigValue(const QString &text)
{
    if (text.contains(QStringLiteral("Autoswitch"), Qt::CaseInsensitive)) {
        return QStringLiteral("autoswitch");
    }
    if (text.startsWith(QStringLiteral("VSync"))) {
        return QStringLiteral("true");
    }
    return QStringLiteral("false");
}

static QString nativeVsyncModeConfigValue(const QString &text)
{
    return text.contains(QStringLiteral("Busy"), Qt::CaseInsensitive) ? QStringLiteral("busywait") : QStringLiteral("normal");
}

static QString nativeVsyncText(const QString &vsync, const QString &mode)
{
    const bool busy = mode.compare(QStringLiteral("busywait"), Qt::CaseInsensitive) == 0;
    if (vsync.compare(QStringLiteral("autoswitch"), Qt::CaseInsensitive) == 0) {
        return busy ? QStringLiteral("VSync autoswitch (Busy wait)") : QStringLiteral("VSync autoswitch");
    }
    if (vsync.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0 || vsync == QStringLiteral("1")) {
        return busy ? QStringLiteral("VSync (Busy wait)") : QStringLiteral("VSync");
    }
    return QStringLiteral("No vsync");
}

static QString rtgVsyncText(const QString &value)
{
    return value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0 || value == QStringLiteral("1")
        ? QStringLiteral("VSync (Busy wait)")
        : QStringLiteral("-");
}

static QString overscanConfigValue(const QString &text)
{
    if (text == QStringLiteral("TV (narrow)")) {
        return QStringLiteral("tv_narrow");
    }
    if (text == QStringLiteral("TV (standard)")) {
        return QStringLiteral("tv_standard");
    }
    if (text == QStringLiteral("TV (wide)")) {
        return QStringLiteral("tv_wide");
    }
    if (text == QStringLiteral("Overscan+")) {
        return QStringLiteral("broadcast");
    }
    if (text == QStringLiteral("Extreme")) {
        return QStringLiteral("extreme");
    }
    if (text == QStringLiteral("Ultra extreme debug")) {
        return QStringLiteral("ultra");
    }
    if (text == QStringLiteral("Ultra extreme debug (HV)")) {
        return QStringLiteral("ultra_hv");
    }
    if (text == QStringLiteral("Ultra extreme debug (C)")) {
        return QStringLiteral("ultra_csync");
    }
    return QStringLiteral("overscan");
}

static QString overscanText(const QString &value)
{
    const QString lower = value.toLower();
    if (lower == QStringLiteral("tv_narrow")) {
        return QStringLiteral("TV (narrow)");
    }
    if (lower == QStringLiteral("tv_standard")) {
        return QStringLiteral("TV (standard)");
    }
    if (lower == QStringLiteral("tv_wide")) {
        return QStringLiteral("TV (wide)");
    }
    if (lower == QStringLiteral("broadcast")) {
        return QStringLiteral("Overscan+");
    }
    if (lower == QStringLiteral("extreme")) {
        return QStringLiteral("Extreme");
    }
    if (lower == QStringLiteral("ultra")) {
        return QStringLiteral("Ultra extreme debug");
    }
    if (lower == QStringLiteral("ultra_hv")) {
        return QStringLiteral("Ultra extreme debug (HV)");
    }
    if (lower == QStringLiteral("ultra_csync")) {
        return QStringLiteral("Ultra extreme debug (C)");
    }
    return QStringLiteral("Overscan");
}

static int autoResolutionValue(const QString &text)
{
    if (text == QStringLiteral("Always on")) {
        return 1;
    }
    if (text == QStringLiteral("10%")) {
        return 10;
    }
    if (text == QStringLiteral("33%")) {
        return 33;
    }
    if (text == QStringLiteral("66%")) {
        return 66;
    }
    if (text == QStringLiteral("100%")) {
        return 100;
    }
    return 0;
}

static QString autoResolutionText(int value)
{
    if (value == 1) {
        return QStringLiteral("Always on");
    }
    if (value <= 0) {
        return QStringLiteral("Disabled");
    }
    if (value <= 10) {
        return QStringLiteral("10%");
    }
    if (value <= 33) {
        return QStringLiteral("33%");
    }
    if (value <= 66) {
        return QStringLiteral("66%");
    }
    return QStringLiteral("100%");
}

static QStringList primaryPortDeviceItems()
{
    QStringList items = {
        QStringLiteral("Mouse"),
        QStringLiteral("Keyboard Layout A"),
        QStringLiteral("Keyboard Layout B"),
        QStringLiteral("Keyboard Layout C"),
        QStringLiteral("Joystick 1"),
        QStringLiteral("Joystick 2"),
        QStringLiteral("Joystick 3"),
        QStringLiteral("Joystick 4")
    };
    for (int i = 0; i < MaxJoyportCustomSlots; i++) {
        items.append(QStringLiteral("Custom %1").arg(i + 1));
    }
    items.append(QStringLiteral("<None>"));
    return items;
}

static QStringList parallelPortDeviceItems()
{
    QStringList items = {
        QStringLiteral("<None>"),
        QStringLiteral("Keyboard Layout A"),
        QStringLiteral("Keyboard Layout B"),
        QStringLiteral("Keyboard Layout C"),
        QStringLiteral("Joystick 1"),
        QStringLiteral("Joystick 2"),
        QStringLiteral("Joystick 3"),
        QStringLiteral("Joystick 4")
    };
    for (int i = 0; i < MaxJoyportCustomSlots; i++) {
        items.append(QStringLiteral("Custom %1").arg(i + 1));
    }
    return items;
}

static QString joyportDeviceConfigValue(const QString &text)
{
    if (text == QStringLiteral("Mouse")) {
        return QStringLiteral("mouse");
    }
    if (text == QStringLiteral("Keyboard Layout A")) {
        return QStringLiteral("kbd1");
    }
    if (text == QStringLiteral("Keyboard Layout B")) {
        return QStringLiteral("kbd2");
    }
    if (text == QStringLiteral("Keyboard Layout C")) {
        return QStringLiteral("kbd3");
    }
    if (text.startsWith(QStringLiteral("Joystick "))) {
        bool ok = false;
        const int index = text.mid(9).toInt(&ok);
        if (ok && index > 0) {
            return QStringLiteral("joy%1").arg(index - 1);
        }
    }
    if (text.startsWith(QStringLiteral("Custom "))) {
        bool ok = false;
        const int index = text.mid(7).toInt(&ok);
        if (ok && index > 0 && index <= MaxJoyportCustomSlots) {
            return QStringLiteral("custom%1").arg(index - 1);
        }
    }
    return QStringLiteral("none");
}

static QString joyportDeviceText(const QString &value, bool allowMouse)
{
    const QString lower = value.toLower();
    if (allowMouse && (lower == QStringLiteral("mouse") || lower == QStringLiteral("mouse0"))) {
        return QStringLiteral("Mouse");
    }
    if (lower == QStringLiteral("kbd1")) {
        return QStringLiteral("Keyboard Layout A");
    }
    if (lower == QStringLiteral("kbd2")) {
        return QStringLiteral("Keyboard Layout B");
    }
    if (lower == QStringLiteral("kbd3")) {
        return QStringLiteral("Keyboard Layout C");
    }
    if (lower.startsWith(QStringLiteral("joy"))) {
        bool ok = false;
        const int index = lower.mid(3).toInt(&ok);
        if (ok && index >= 0 && index < 4) {
            return QStringLiteral("Joystick %1").arg(index + 1);
        }
    }
    if (lower.startsWith(QStringLiteral("custom"))) {
        bool ok = false;
        const int index = lower.mid(6).toInt(&ok);
        if (ok && index >= 0 && index < MaxJoyportCustomSlots) {
            return QStringLiteral("Custom %1").arg(index + 1);
        }
    }
    return QStringLiteral("<None>");
}

static QString autofireConfigValue(const QString &text)
{
    if (text == QStringLiteral("Autofire")) {
        return QStringLiteral("normal");
    }
    if (text == QStringLiteral("Autofire (toggle)")) {
        return QStringLiteral("toggle");
    }
    if (text == QStringLiteral("Autofire (always)")) {
        return QStringLiteral("always");
    }
    if (text == QStringLiteral("No autofire (toggle)")) {
        return QStringLiteral("togglebutton");
    }
    return QStringLiteral("none");
}

static QString autofireText(const QString &value)
{
    if (value.compare(QStringLiteral("normal"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Autofire");
    }
    if (value.compare(QStringLiteral("toggle"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Autofire (toggle)");
    }
    if (value.compare(QStringLiteral("always"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Autofire (always)");
    }
    if (value.compare(QStringLiteral("togglebutton"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("No autofire (toggle)");
    }
    return QStringLiteral("No autofire (normal)");
}

static QString joyportModeConfigValue(const QString &text)
{
    if (text == QStringLiteral("Wheel Mouse")) {
        return QStringLiteral("mouse");
    }
    if (text == QStringLiteral("Mouse")) {
        return QStringLiteral("mousenowheel");
    }
    if (text == QStringLiteral("Joystick")) {
        return QStringLiteral("djoy");
    }
    if (text == QStringLiteral("Gamepad")) {
        return QStringLiteral("gamepad");
    }
    if (text == QStringLiteral("Analog joystick")) {
        return QStringLiteral("ajoy");
    }
    if (text == QStringLiteral("CDTV remote mouse")) {
        return QStringLiteral("cdtvjoy");
    }
    if (text == QStringLiteral("CD32 pad")) {
        return QStringLiteral("cd32joy");
    }
    if (text == QStringLiteral("Generic light pen/gun")) {
        return QStringLiteral("lightpen");
    }
    return QString();
}

static QString joyportModeText(const QString &value)
{
    if (value.compare(QStringLiteral("mouse"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Wheel Mouse");
    }
    if (value.compare(QStringLiteral("mousenowheel"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Mouse");
    }
    if (value.compare(QStringLiteral("djoy"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Joystick");
    }
    if (value.compare(QStringLiteral("gamepad"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Gamepad");
    }
    if (value.compare(QStringLiteral("ajoy"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Analog joystick");
    }
    if (value.compare(QStringLiteral("cdtvjoy"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("CDTV remote mouse");
    }
    if (value.compare(QStringLiteral("cd32joy"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("CD32 pad");
    }
    if (value.compare(QStringLiteral("lightpen"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Generic light pen/gun");
    }
    return QStringLiteral("Default");
}

static QString magicMouseCursorConfigValue(const QString &text)
{
    if (text == QStringLiteral("Show native cursor only")) {
        return QStringLiteral("native");
    }
    if (text == QStringLiteral("Show host cursor only")) {
        return QStringLiteral("host");
    }
    return QStringLiteral("both");
}

static QString magicMouseCursorText(const QString &value)
{
    if (value.compare(QStringLiteral("native"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Show native cursor only");
    }
    if (value.compare(QStringLiteral("host"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Show host cursor only");
    }
    return QStringLiteral("Show both cursors");
}

static constexpr int SoundVolumeCount = 5;
static constexpr int FloppySoundDriveCount = 4;

static QVector<QPair<QString, QString>> displayDeviceChoices()
{
    QVector<QPair<QString, QString>> choices;
    choices.append(qMakePair(QStringLiteral("Default display"), QString()));

#ifdef UAE_UNIX_WITH_SDL3
    SDL_SetMainReady();
    const bool wasVideoInitialized = SDL_WasInit(SDL_INIT_VIDEO) != 0;
    if (wasVideoInitialized || SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        int count = 0;
        SDL_DisplayID *displays = SDL_GetDisplays(&count);
        if (displays) {
            for (int i = 0; i < count; i++) {
                const char *name = SDL_GetDisplayName(displays[i]);
                const QString friendly = QString::fromUtf8(name && name[0] ? name : "Unknown display");
                choices.append(qMakePair(
                    QStringLiteral("Display %1: %2").arg(i + 1).arg(friendly),
                    QStringLiteral("SDL:%1").arg(uint(displays[i]))));
            }
            SDL_free(displays);
        }
        if (!wasVideoInitialized) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
    }
#endif

    return choices;
}

static QString displayFriendlyName(const QString &displayText)
{
    if (displayText == QStringLiteral("Default display")) {
        return QString();
    }
    const QString prefix = QStringLiteral(": ");
    const int separator = displayText.indexOf(prefix);
    return separator >= 0 ? displayText.mid(separator + prefix.size()) : displayText;
}

static QVector<QPair<QString, QString>> soundDeviceChoices()
{
    QVector<QPair<QString, QString>> choices;
    choices.append(qMakePair(QStringLiteral("SDL: Default audio device"), QStringLiteral("SDL:Default Audio Device")));

#ifdef UAE_UNIX_WITH_SDL3
    SDL_SetMainReady();
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        int count = 0;
        SDL_AudioDeviceID *devices = SDL_GetAudioPlaybackDevices(&count);
        if (devices) {
            for (int i = 0; i < count; i++) {
                const char *name = SDL_GetAudioDeviceName(devices[i]);
                const QString display = QString::fromUtf8(name && name[0] ? name : "Unknown audio device");
                choices.append(qMakePair(QStringLiteral("SDL: %1").arg(display), QStringLiteral("SDL:%1").arg(display)));
            }
            SDL_free(devices);
        }
    }
#endif

    return choices;
}

struct WinUaeQtMidiChoice {
    QString display;
    QString configName;
    int deviceId = -2;
};

#if defined(__APPLE__) && defined(UAE_UNIX_WITH_COREMIDI)
static QString coreMidiObjectString(MIDIObjectRef object, CFStringRef property)
{
    CFStringRef str = nullptr;
    if (MIDIObjectGetStringProperty(object, property, &str) != noErr || !str) {
        return QString();
    }
    char buffer[512];
    const bool ok = CFStringGetCString(str, buffer, sizeof buffer, kCFStringEncodingUTF8);
    CFRelease(str);
    return ok ? QString::fromUtf8(buffer) : QString();
}
#endif

static QVector<WinUaeQtMidiChoice> midiOutputChoices()
{
    QVector<WinUaeQtMidiChoice> choices;
#if UAE_UNIX_WITH_MIDI
#if defined(__APPLE__) && defined(UAE_UNIX_WITH_COREMIDI)
    const ItemCount count = MIDIGetNumberOfDestinations();
    if (count > 0) {
        WinUaeQtMidiChoice def;
        def.display = QStringLiteral("Default MIDI-Out Device");
        def.configName = QStringLiteral("default");
        def.deviceId = -1;
        choices.append(def);
    }
    for (ItemCount i = 0; i < count; i++) {
        MIDIEndpointRef endpoint = MIDIGetDestination(i);
        if (!endpoint) {
            continue;
        }
        QString name = coreMidiObjectString(endpoint, kMIDIPropertyDisplayName);
        if (name.isEmpty()) {
            name = coreMidiObjectString(endpoint, kMIDIPropertyName);
        }
        if (name.isEmpty()) {
            name = QStringLiteral("CoreMIDI destination %1").arg(i + 1);
        }
        WinUaeQtMidiChoice choice;
        choice.display = name;
        choice.configName = name;
        choice.deviceId = int(i);
        choices.append(choice);
    }
#elif defined(__linux__) && defined(UAE_UNIX_WITH_ALSA_MIDI)
    snd_seq_t *seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_OUTPUT, 0) >= 0) {
        QVector<WinUaeQtMidiChoice> alsaChoices;
        snd_seq_client_info_t *clientInfo;
        snd_seq_port_info_t *portInfo;
        snd_seq_client_info_alloca(&clientInfo);
        snd_seq_port_info_alloca(&portInfo);
        snd_seq_client_info_set_client(clientInfo, -1);
        int deviceId = 0;
        while (snd_seq_query_next_client(seq, clientInfo) >= 0) {
            const int client = snd_seq_client_info_get_client(clientInfo);
            snd_seq_port_info_set_client(portInfo, client);
            snd_seq_port_info_set_port(portInfo, -1);
            while (snd_seq_query_next_port(seq, portInfo) >= 0) {
                const unsigned int caps = snd_seq_port_info_get_capability(portInfo);
                if ((caps & SND_SEQ_PORT_CAP_WRITE) == 0 || (caps & SND_SEQ_PORT_CAP_SUBS_WRITE) == 0) {
                    continue;
                }
                const int port = snd_seq_port_info_get_port(portInfo);
                const char *clientName = snd_seq_client_info_get_name(clientInfo);
                const char *portName = snd_seq_port_info_get_name(portInfo);
                const QString name = QStringLiteral("%1:%2 %3%4%5")
                    .arg(client)
                    .arg(port)
                    .arg(QString::fromUtf8(clientName ? clientName : "ALSA"))
                    .arg(portName && portName[0] ? QStringLiteral(" ") : QString())
                    .arg(QString::fromUtf8(portName ? portName : ""));
                WinUaeQtMidiChoice choice;
                choice.display = name;
                choice.configName = name;
                choice.deviceId = deviceId++;
                alsaChoices.append(choice);
            }
        }
        snd_seq_close(seq);
        if (!alsaChoices.isEmpty()) {
            WinUaeQtMidiChoice def;
            def.display = QStringLiteral("Default MIDI-Out Device");
            def.configName = QStringLiteral("default");
            def.deviceId = -1;
            choices.append(def);
            choices += alsaChoices;
        }
    }
#endif
#endif
    return choices;
}

static QVector<WinUaeQtMidiChoice> midiInputChoices()
{
    QVector<WinUaeQtMidiChoice> choices;
#if UAE_UNIX_WITH_MIDI
#if defined(__APPLE__) && defined(UAE_UNIX_WITH_COREMIDI)
    const ItemCount count = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < count; i++) {
        MIDIEndpointRef endpoint = MIDIGetSource(i);
        if (!endpoint) {
            continue;
        }
        QString name = coreMidiObjectString(endpoint, kMIDIPropertyDisplayName);
        if (name.isEmpty()) {
            name = coreMidiObjectString(endpoint, kMIDIPropertyName);
        }
        if (name.isEmpty()) {
            name = QStringLiteral("CoreMIDI source %1").arg(i + 1);
        }
        WinUaeQtMidiChoice choice;
        choice.display = name;
        choice.configName = name;
        choice.deviceId = int(i);
        choices.append(choice);
    }
#elif defined(__linux__) && defined(UAE_UNIX_WITH_ALSA_MIDI)
    snd_seq_t *seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_INPUT, 0) >= 0) {
        snd_seq_client_info_t *clientInfo;
        snd_seq_port_info_t *portInfo;
        snd_seq_client_info_alloca(&clientInfo);
        snd_seq_port_info_alloca(&portInfo);
        snd_seq_client_info_set_client(clientInfo, -1);
        int deviceId = 0;
        while (snd_seq_query_next_client(seq, clientInfo) >= 0) {
            const int client = snd_seq_client_info_get_client(clientInfo);
            snd_seq_port_info_set_client(portInfo, client);
            snd_seq_port_info_set_port(portInfo, -1);
            while (snd_seq_query_next_port(seq, portInfo) >= 0) {
                const unsigned int caps = snd_seq_port_info_get_capability(portInfo);
                if ((caps & SND_SEQ_PORT_CAP_READ) == 0 || (caps & SND_SEQ_PORT_CAP_SUBS_READ) == 0) {
                    continue;
                }
                const int port = snd_seq_port_info_get_port(portInfo);
                const char *clientName = snd_seq_client_info_get_name(clientInfo);
                const char *portName = snd_seq_port_info_get_name(portInfo);
                const QString name = QStringLiteral("%1:%2 %3%4%5")
                    .arg(client)
                    .arg(port)
                    .arg(QString::fromUtf8(clientName ? clientName : "ALSA"))
                    .arg(portName && portName[0] ? QStringLiteral(" ") : QString())
                    .arg(QString::fromUtf8(portName ? portName : ""));
                WinUaeQtMidiChoice choice;
                choice.display = name;
                choice.configName = name;
                choice.deviceId = deviceId++;
                choices.append(choice);
            }
        }
        snd_seq_close(seq);
    }
#endif
#endif
    return choices;
}

static QVector<QPair<QString, QString>> samplerDeviceChoices()
{
    QVector<QPair<QString, QString>> choices;
    choices.append(qMakePair(QStringLiteral("<None>"), QString()));

#if UAE_UNIX_WITH_SAMPLER && defined(UAE_UNIX_WITH_SDL3)
    SDL_SetMainReady();
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        choices.append(qMakePair(QStringLiteral("SDL: Default recording device"), QStringLiteral("SDL:Default Recording Device")));
        int count = 0;
        SDL_AudioDeviceID *devices = SDL_GetAudioRecordingDevices(&count);
        if (devices) {
            for (int i = 0; i < count; i++) {
                const char *name = SDL_GetAudioDeviceName(devices[i]);
                const QString display = QString::fromUtf8(name && name[0] ? name : "Unknown recording device");
                choices.append(qMakePair(QStringLiteral("SDL: %1").arg(display), QStringLiteral("SDL:%1").arg(display)));
            }
            SDL_free(devices);
        }
    }
#endif

    return choices;
}

static QString soundOutputConfigValue(int id)
{
    switch (id) {
    case 0:
        return QStringLiteral("none");
    case 1:
        return QStringLiteral("interrupts");
    case 2:
    default:
        return QStringLiteral("exact");
    }
}

static int soundOutputId(const QString &value)
{
    if (value.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0) {
        return 0;
    }
    if (value.compare(QStringLiteral("interrupts"), Qt::CaseInsensitive) == 0) {
        return 1;
    }
    return 2;
}

static QStringList soundChannelItems()
{
    return {
        QStringLiteral("Mono"),
        QStringLiteral("Stereo"),
        QStringLiteral("Cloned Stereo (4 Channels)"),
        QStringLiteral("4 Channels"),
        QStringLiteral("Cloned Stereo (5.1)"),
        QStringLiteral("5.1"),
        QStringLiteral("Cloned stereo (7.1)"),
        QStringLiteral("7.1")
    };
}

static QString soundChannelConfigValue(const QString &text)
{
    static const QStringList values = {
        QStringLiteral("mono"),
        QStringLiteral("stereo"),
        QStringLiteral("clonedstereo"),
        QStringLiteral("4ch"),
        QStringLiteral("clonedstereo6ch"),
        QStringLiteral("6ch"),
        QStringLiteral("clonedstereo8ch"),
        QStringLiteral("8ch")
    };
    const int index = soundChannelItems().indexOf(text);
    return values.value(index, QStringLiteral("stereo"));
}

static QString soundChannelText(const QString &value)
{
    static const QStringList values = {
        QStringLiteral("mono"),
        QStringLiteral("stereo"),
        QStringLiteral("clonedstereo"),
        QStringLiteral("4ch"),
        QStringLiteral("clonedstereo6ch"),
        QStringLiteral("6ch"),
        QStringLiteral("clonedstereo8ch"),
        QStringLiteral("8ch")
    };
    const int index = values.indexOf(value.toLower());
    return soundChannelItems().value(index, QStringLiteral("Stereo"));
}

static QString soundInterpolationConfigValue(const QString &text)
{
    if (text == QStringLiteral("Anti")) {
        return QStringLiteral("anti");
    }
    if (text == QStringLiteral("Sinc")) {
        return QStringLiteral("sinc");
    }
    if (text == QStringLiteral("RH")) {
        return QStringLiteral("rh");
    }
    if (text == QStringLiteral("Crux")) {
        return QStringLiteral("crux");
    }
    return QStringLiteral("none");
}

static QString soundInterpolationText(const QString &value)
{
    if (value.compare(QStringLiteral("anti"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Anti");
    }
    if (value.compare(QStringLiteral("sinc"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Sinc");
    }
    if (value.compare(QStringLiteral("rh"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("RH");
    }
    if (value.compare(QStringLiteral("crux"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Crux");
    }
    return QStringLiteral("Disabled");
}

static QString soundFilterConfigValue(const QString &text)
{
    if (text == QStringLiteral("Always off")) {
        return QStringLiteral("off");
    }
    return text.startsWith(QStringLiteral("Emulated")) ? QStringLiteral("emulated") : QStringLiteral("on");
}

static QString soundFilterTypeConfigValue(const QString &text)
{
    return text.contains(QStringLiteral("A1200")) ? QStringLiteral("enhanced") : QStringLiteral("standard");
}

static QString soundFilterText(const QString &filter, const QString &type)
{
    const bool enhanced = type.compare(QStringLiteral("enhanced"), Qt::CaseInsensitive) == 0;
    if (filter.compare(QStringLiteral("off"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Always off");
    }
    if (filter.compare(QStringLiteral("emulated"), Qt::CaseInsensitive) == 0) {
        return enhanced ? QStringLiteral("Emulated (A1200)") : QStringLiteral("Emulated (A500)");
    }
    return enhanced ? QStringLiteral("Always on (A1200)") : QStringLiteral("Always on (A500)");
}

static QString soundSwapText(bool paula, bool ahi)
{
    const int index = (paula ? 1 : 0) + (ahi ? 2 : 0);
    return QStringList({ QStringLiteral("-"), QStringLiteral("Paula only"), QStringLiteral("AHI only"), QStringLiteral("Both") }).value(index);
}

static int soundBufferSizeFromIndex(int index)
{
    static const int sizes[] = { 1024, 2048, 3072, 4096, 6144, 8192, 12288, 16384, 32768, 65536 };
    if (index <= 0) {
        return 0;
    }
    return sizes[qBound(1, index, 10) - 1];
}

static int soundBufferIndexFromSize(int size)
{
    static const int sizes[] = { 1024, 2048, 3072, 4096, 6144, 8192, 12288, 16384, 32768, 65536 };
    if (size < sizes[0]) {
        return 0;
    }
    int index = 0;
    while (index < 9 && sizes[index] < size) {
        index++;
    }
    return index + 1;
}

static constexpr int RtgRgbClut = 1 << 1;
static constexpr int RtgRgbR8G8B8 = 1 << 2;
static constexpr int RtgRgbB8G8R8 = 1 << 3;
static constexpr int RtgRgbR5G6B5Pc = 1 << 4;
static constexpr int RtgRgbR5G5B5Pc = 1 << 5;
static constexpr int RtgRgbA8R8G8B8 = 1 << 6;
static constexpr int RtgRgbA8B8G8R8 = 1 << 7;
static constexpr int RtgRgbR8G8B8A8 = 1 << 8;
static constexpr int RtgRgbB8G8R8A8 = 1 << 9;
static constexpr int RtgRgbR5G6B5 = 1 << 10;
static constexpr int RtgRgbR5G5B5 = 1 << 11;
static constexpr int RtgRgbB5G6R5Pc = 1 << 12;
static constexpr int RtgRgbB5G5R5Pc = 1 << 13;
static constexpr int RtgDefaultModeMask = RtgRgbClut | RtgRgbR5G6B5Pc | RtgRgbB8G8R8A8;

static QString rtgScaleConfigValue(bool scale, bool center, bool integer)
{
    if (integer) {
        return QStringLiteral("integer");
    }
    if (center) {
        return QStringLiteral("center");
    }
    if (scale) {
        return QStringLiteral("scale");
    }
    return QStringLiteral("resize");
}

static QString rtgBufferConfigValue(const QString &text)
{
    return text == QStringLiteral("Triple") ? QStringLiteral("2") : QStringLiteral("1");
}

static QString rtgBufferText(const QString &value)
{
    return value == QStringLiteral("2") ? QStringLiteral("Triple") : QStringLiteral("Double");
}

static QString rtgVBlankConfigValue(const QString &text)
{
    const QString value = text.trimmed();
    if (value == QStringLiteral("Chipset")) {
        return QStringLiteral("chipset");
    }
    if (value == QStringLiteral("Default")) {
        return QStringLiteral("real");
    }
    return value.isEmpty() ? QStringLiteral("chipset") : value;
}

static QString rtgVBlankText(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("real") || normalized == QStringLiteral("-1") || normalized == QStringLiteral("default")) {
        return QStringLiteral("Default");
    }
    if (normalized == QStringLiteral("chipset") || normalized == QStringLiteral("disabled") ||
        normalized == QStringLiteral("0") || normalized == QStringLiteral("-2")) {
        return QStringLiteral("Chipset");
    }
    return value.trimmed().isEmpty() ? QStringLiteral("Chipset") : value.trimmed();
}

static int rtgColorDepthMask(const QString &text)
{
    if (text == QStringLiteral("8-bit (*)")) {
        return RtgRgbClut;
    }
    if (text == QStringLiteral("All 15/16-bit")) {
        return RtgRgbR5G6B5Pc | RtgRgbR5G5B5Pc | RtgRgbR5G6B5 | RtgRgbR5G5B5 | RtgRgbB5G6R5Pc | RtgRgbB5G5R5Pc;
    }
    if (text == QStringLiteral("R5G6B5PC (*)")) {
        return RtgRgbR5G6B5Pc;
    }
    if (text == QStringLiteral("R5G5B5PC")) {
        return RtgRgbR5G5B5Pc;
    }
    if (text == QStringLiteral("R5G6B5")) {
        return RtgRgbR5G6B5;
    }
    if (text == QStringLiteral("R5G5B5")) {
        return RtgRgbR5G5B5;
    }
    if (text == QStringLiteral("B5G6R5PC")) {
        return RtgRgbB5G6R5Pc;
    }
    if (text == QStringLiteral("B5G5R5PC")) {
        return RtgRgbB5G5R5Pc;
    }
    if (text == QStringLiteral("All 24-bit")) {
        return RtgRgbR8G8B8 | RtgRgbB8G8R8;
    }
    if (text == QStringLiteral("R8G8B8")) {
        return RtgRgbR8G8B8;
    }
    if (text == QStringLiteral("B8G8R8")) {
        return RtgRgbB8G8R8;
    }
    if (text == QStringLiteral("All 32-bit")) {
        return RtgRgbA8R8G8B8 | RtgRgbA8B8G8R8 | RtgRgbR8G8B8A8 | RtgRgbB8G8R8A8;
    }
    if (text == QStringLiteral("A8R8G8B8")) {
        return RtgRgbA8R8G8B8;
    }
    if (text == QStringLiteral("A8B8G8R8")) {
        return RtgRgbA8B8G8R8;
    }
    if (text == QStringLiteral("R8G8B8A8")) {
        return RtgRgbR8G8B8A8;
    }
    if (text == QStringLiteral("B8G8R8A8 (*)")) {
        return RtgRgbB8G8R8A8;
    }
    return 0;
}

static QString rtg8BitText(int mask)
{
    return (mask & RtgRgbClut) ? QStringLiteral("8-bit (*)") : QStringLiteral("(8bit)");
}

static QString rtg16BitText(int mask)
{
    const int all = RtgRgbR5G6B5Pc | RtgRgbR5G5B5Pc | RtgRgbR5G6B5 | RtgRgbR5G5B5 | RtgRgbB5G6R5Pc | RtgRgbB5G5R5Pc;
    if ((mask & all) == all) {
        return QStringLiteral("All 15/16-bit");
    }
    if (mask & RtgRgbR5G6B5Pc) {
        return QStringLiteral("R5G6B5PC (*)");
    }
    if (mask & RtgRgbR5G5B5Pc) {
        return QStringLiteral("R5G5B5PC");
    }
    if (mask & RtgRgbR5G6B5) {
        return QStringLiteral("R5G6B5");
    }
    if (mask & RtgRgbR5G5B5) {
        return QStringLiteral("R5G5B5");
    }
    if (mask & RtgRgbB5G6R5Pc) {
        return QStringLiteral("B5G6R5PC");
    }
    if (mask & RtgRgbB5G5R5Pc) {
        return QStringLiteral("B5G5R5PC");
    }
    return QStringLiteral("(15/16bit)");
}

static QString rtg24BitText(int mask)
{
    const int all = RtgRgbR8G8B8 | RtgRgbB8G8R8;
    if ((mask & all) == all) {
        return QStringLiteral("All 24-bit");
    }
    if (mask & RtgRgbR8G8B8) {
        return QStringLiteral("R8G8B8");
    }
    if (mask & RtgRgbB8G8R8) {
        return QStringLiteral("B8G8R8");
    }
    return QStringLiteral("(24bit)");
}

static QString rtg32BitText(int mask)
{
    const int all = RtgRgbA8R8G8B8 | RtgRgbA8B8G8R8 | RtgRgbR8G8B8A8 | RtgRgbB8G8R8A8;
    if ((mask & all) == all) {
        return QStringLiteral("All 32-bit");
    }
    if (mask & RtgRgbA8R8G8B8) {
        return QStringLiteral("A8R8G8B8");
    }
    if (mask & RtgRgbA8B8G8R8) {
        return QStringLiteral("A8B8G8R8");
    }
    if (mask & RtgRgbR8G8B8A8) {
        return QStringLiteral("R8G8B8A8");
    }
    if (mask & RtgRgbB8G8R8A8) {
        return QStringLiteral("B8G8R8A8 (*)");
    }
    return QStringLiteral("(32bit)");
}

static QString sourceFile(const QString &relative)
{
    return QDir(QString::fromUtf8(WINUAE_UNIX_SOURCE_DIR)).filePath(relative);
}

static QString resourceFile(const QString &relative)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
#ifdef Q_OS_MACOS
    const QString bundlePath = appDir.filePath(QStringLiteral("../Resources/") + relative);
    if (QFileInfo::exists(bundlePath)) {
        return bundlePath;
    }
#endif
    const QString localPath = appDir.filePath(relative);
    if (QFileInfo::exists(localPath)) {
        return localPath;
    }
    const QString relativeInstallPath = appDir.filePath(QStringLiteral("../") + QString::fromUtf8(WINUAE_UNIX_INSTALL_DATADIR_RELATIVE) + QStringLiteral("/") + relative);
    if (QFileInfo::exists(relativeInstallPath)) {
        return relativeInstallPath;
    }
    const QString installPath = QDir(QString::fromUtf8(WINUAE_UNIX_INSTALL_DATA_DIR)).filePath(relative);
    if (QFileInfo::exists(installPath)) {
        return installPath;
    }
    return sourceFile(relative);
}

static QString versionString()
{
    return QStringLiteral("WinUAE %1.%2.%3")
        .arg(WINUAE_UNIX_VERSION_MAJOR)
        .arg(WINUAE_UNIX_VERSION_MINOR)
        .arg(WINUAE_UNIX_VERSION_REVISION);
}

static QStringList contributorLines()
{
    return {
        QStringLiteral("Bernd Schmidt - The Grand-Master"),
        QStringLiteral("Sam Jordan - Custom-chip, floppy-DMA, etc."),
        QStringLiteral("Mathias Ortmann - Original WinUAE Main Guy, BSD Socket support"),
        QStringLiteral("Brian King - Picasso96 Support, Integrated GUI for WinUAE, previous WinUAE Main Guy"),
        QStringLiteral("Toni Wilen - Core updates, WinUAE Main Guy"),
        QStringLiteral("Gustavo Goedert / Peter Remmers / Michael Sontheimer / Tomi Hakala / Tim Gunn / Nemo Pohle - DOS Port Stuff"),
        QStringLiteral("Samuel Devulder / Olaf Barthel / Sam Jordan - Amiga Ports"),
        QStringLiteral("Krister Bergman - XFree86 and OS/2 Port"),
        QStringLiteral("A. Blanchard / Ernesto Corvi - MacOS Port"),
        QStringLiteral("Christian Bauer - BeOS Port"),
        QStringLiteral("Ian Stephenson - NextStep Port"),
        QStringLiteral("Peter Teichmann - Acorn/RiscOS Port"),
        QStringLiteral("Stefan Reinauer - ZorroII/III AutoConfig, Serial Support"),
        QStringLiteral("Christian Schmitt / Chris Hames - Serial Support"),
        QStringLiteral("Herman ten Brugge - 68020/68881 Emulation Code"),
        QStringLiteral("Tauno Taipaleenmaki - Various UAE-Control/UAE-Library Support"),
        QStringLiteral("Brett Eden / Tim Gunn / Paolo Besser / Nemo Pohle - Various Docs and Web-Sites"),
        QStringLiteral("Georg Veichtlbauer - Help File coordinator, German GUI"),
        QStringLiteral("Fulvio Leonardi - Italian translator for WinUAE"),
        QStringLiteral("Arnljot Arntsen, Bill Panagouleas, Cloanto, Zak Jennings - Hardware support"),
        QStringLiteral("Special thanks to Alexander Kneer and Tobias Abt (The Picasso96 Team)"),
        QStringLiteral("Steven Weiser - Postscript printing emulation idea and testing"),
        QStringLiteral("Peter Toth / Balazs Ratkai / Ivan Herczeg / Andras Arato - Hungarian translation"),
        QStringLiteral("Karsten Bock, Gavin Fance, Dirk Trowe and Christian Schindler - Freezer cartridge hardware support"),
        QStringLiteral("Mikko Nieminen - Demo compatibility testing"),
        QStringLiteral("Arabuusimiehet - [This information is on a need-to-know basis]"),
        QStringLiteral("Ross - Chipset torture test programs")
    };
}

struct AboutLink {
    const char *display;
    const char *url;
};

static const AboutLink aboutLinks[] = {
    { "Cloanto's Amiga Forever", "https://www.amigaforever.com/" },
    { "Amiga Corporation", "https://amiga.com/" },
    { "WinUAE Home Page", "http://www.winuae.net/" },
    { "abime.net", "http://www.abime.net/" },
    { "SPS", "http://www.softpres.org/" },
    { "AmiKit", "http://amikit.amiga.sk/" }
};

struct ConfigChoice {
    const char *display;
    const char *value;
};

static const ConfigChoice printerTypeChoices[] = {
    { "Passthrough", "none" },
    { "ASCII-Only", "ascii" },
    { "Epson Matrix Printer Emulation, 9pin", "epson_matrix_9pin" },
    { "Epson Matrix Printer Emulation, 48pin", "epson_matrix_48pin" },
    { "PostScript (Passthrough)", "postscript_passthrough" },
    { "PostScript (Emulation)", "postscript_emulation" }
};

static const ConfigChoice dongleChoices[] = {
    { "None", "none" },
    { "RoboCop 3", "robocop 3" },
    { "Leader Board", "leaderboard" },
    { "B.A.T. II", "b.a.t. ii" },
    { "Italy '90 Soccer", "italy'90 soccer" },
    { "Dames Grand-Maitre", "dames grand maitre" },
    { "Rugby Coach", "rugby coach" },
    { "Cricket Captain", "cricket captain" },
    { "Leviathan", "leviathan" },
    { "Music Master", "musicmaster" },
    { "Logistics/SuperBase", "logistics" },
    { "Scala MM (Red)", "scala red" },
    { "Scala MM (Green)", "scala green" },
    { "Striker Manager", "strikermanager" },
    { "Multi-Player Soccer Manager", "multi-player soccer manager" },
    { "Football Director 2", "football director 2" }
};

static const ConfigChoice keyboardLedChoices[] = {
    { "None", "none" },
    { "Power", "POWER" },
    { "DF0", "DF0" },
    { "DF1", "DF1" },
    { "DF2", "DF2" },
    { "DF3", "DF3" },
    { "HD", "HD" },
    { "CD", "CD" },
    { "DF*", "DFx" }
};

struct MiscCheckChoice {
    const char *display;
    const char *key;
    bool defaultChecked;
};

static const MiscCheckChoice miscCheckChoices[] = {
    { "Show GUI on startup", "use_gui", true },
    { "Synchronize clock", "synchronize_clock", false },
    { "One second reboot pause", "cpu_reset_pause", false },
    { "Faster RTG", "rtg_nocustom", true },
    { "Clipboard sharing", "clipboard_sharing", false },
    { "Allow native code", "native_code", false },
    { "Native on-screen display", "show_leds", false },
    { "RTG on-screen display", "show_leds_rtg", false },
    { "Log illegal memory accesses", "log_illegal_mem", false },
    { "Master floppy write protection", "floppy_write_protect", false },
    { "Master harddrive write protection", "harddrive_write_protect", false },
    { "Hide all UAE autoconfig boards", "uae_hide_autoconfig", false },
    { "Power led dims when audio filter is disabled", "power_led_dim", false },
    { "Debug memory space", "debug_mem", false },
    { "Force hard reset if CPU halted", "cpu_halt_auto_reset", false },
    { "A600/A1200/A4000 IDE scsi.device disable", "scsidevice_disable", false },
    { "Warp mode reset", "warpboot", false }
};

static bool miscCheckChoiceEnabled(const QString &key)
{
    Q_UNUSED(key);
    return true;
}

static QString miscCheckChoiceDisabledReason(const QString &key)
{
    Q_UNUSED(key);
    return QString();
}

struct ActivityPriorityChoice {
    const char *display;
    int value;
};

static const ActivityPriorityChoice activityPriorityChoices[] = {
    { "Above normal", 1 },
    { "Normal", 0 },
    { "Below normal", -1 },
    { "Low", -2 }
};

struct AssociationChoice {
    const char *extension;
};

static const AssociationChoice associationChoices[] = {
    { ".uae" },
    { ".adf" },
    { ".adz" },
    { ".dms" },
    { ".fdi" },
    { ".ipf" },
    { ".uss" }
};

struct AdvancedCheckChoice {
    const char *display;
    const char *key;
    bool defaultChecked;
};

static const ConfigChoice rtcChoices[] = {
    { "None", "none" },
    { "MSM6242B", "MSM6242B" },
    { "RF5C01A", "RP5C01A" },
    { "A2000 MSM6242B", "MSM6242B_A2000" }
};

static const ConfigChoice ciaTodChoices[] = {
    { "Vertical Sync", "vblank" },
    { "Power Supply 50Hz", "50hz" },
    { "Power Supply 60Hz", "60hz" }
};

static const ConfigChoice unmappedAddressChoices[] = {
    { "Floating", "floating" },
    { "All zeros", "zero" },
    { "All ones", "one" }
};

static const ConfigChoice ciaSyncChoices[] = {
    { "Autoselect", "default" },
    { "68000", "68000" },
    { "Gayle", "Gayle" },
    { "68000 Alternate", "68000_opt" }
};

static const ConfigChoice agnusModelChoices[] = {
    { "Auto", "default" },
    { "Velvet", "velvet" },
    { "A1000", "a1000" }
};

static const ConfigChoice agnusSizeChoices[] = {
    { "Auto", "default" },
    { "512k", "512k" },
    { "1M", "1m" },
    { "2M", "2m" }
};

static const ConfigChoice deniseModelChoices[] = {
    { "Auto", "default" },
    { "Velvet", "velvet" },
    { "A1000 No-EHB", "a1000_noehb" },
    { "A1000", "a1000" }
};

static const ConfigChoice hvSyncChoices[] = {
    { "Combined + Blanking", "hvcsync" },
    { "Composite Sync + Blanking", "csync" },
    { "H/V Sync + Blanking", "hvsync" },
    { "Combined + Sync", "hvcsync_s" },
    { "Composite Sync + Sync", "csync_s" },
    { "H/V Sync + Sync", "hvsync_s" }
};

static const ConfigChoice genlockModeChoices[] = {
    { "-", "none" },
    { "Noise (built-in)", "noise" },
    { "Test card (built-in)", "testcard" },
    { "Image file (png)", "image" },
    { "Video file", "video" },
    { "Capture device", "stream" },
    { "American Laser Games/Picmatic LaserDisc Player", "ld" },
    { "Sony LaserDisc Player", "sony_ld" },
    { "Pioneer LaserDisc Player", "pioneer_ld" }
};

static const ConfigChoice keyboardModeChoices[] = {
    { "Keyboard disconnected", "disconnected" },
    { "UAE High level emulation", "UAE" },
    { "A500 / A500+ (6570-036 MCU)", "a500_6570-036" },
    { "A600 (6570-036 MCU)", "a600_6570-036" },
    { "A1000 (6500-1 MCU, ROM not yet dumped)", "a1000_6500-1" },
    { "A1000 (6570-036 MCU)", "a1000_6570-036" },
    { "A1200 (68HC05C MCU)", "a1200_6805" },
    { "A2000 (Cherry, 8039 MCU)", "a2000_8039" },
    { "A2000/A3000/A4000 (6570-036 MCU)", "ax000_6570-036" }
};

static const ConfigChoice z3MappingChoices[] = {
    { "Automatic", "auto" },
    { "UAE", "uae" },
    { "Real", "real" }
};

static const ConfigChoice scsiModeChoices[] = {
    { "SCSI emulation", "SCSIEMU" },
    { "SPTI", "SPTI" },
    { "SPTI + SCSI SCAN", "SPTI+SCSISCAN" }
};

static const AdvancedCheckChoice advancedCheckChoices[] = {
    { "CIA ROM Overlay", "cia_overlay", true },
    { "CD32 CD", "cd32cd", false },
    { "CDTV CD", "cdtvcd", false },
    { "ROM Mirror (E0)", "ksmirror_e0", true },
    { "DF0: ID Hardware", "df0idhw", true },
    { "KB Reset Warning", "resetwarning", true },
    { "CIA TOD bug", "cia_todbug", false },
    { "1M Chip / 0.5M+0.5M", "1mchipjumper", false },
    { "Toshiba Gary", "toshiba_gary", false },
    { "A1000 Boot RAM/ROM", "a1000ram", false },
    { "CD32 C2P", "cd32c2p", false },
    { "CDTV SRAM", "cdtvram", false },
    { "ROM Mirror (A8)", "ksmirror_a8", false },
    { "Z3 Autoconfig", "z3_autoconfig", false },
    { "KS ROM has Chip RAM speed", "rom_is_slow", false },
    { "CD32 NVRAM", "cd32nvram", false },
    { "CDTV-CR", "cdtv-cr", false },
    { "PCMCIA", "pcmcia", false },
    { "Composite color burst", "color_burst", false },
    { "Power up memory pattern", "memory_pattern", false }
};

static const ConfigChoice filterTargetChoices[] = {
    { "Native", "" },
    { "RTG", "_rtg" },
    { "Interlaced", "_lace" }
};

static const ConfigChoice filterModeChoices[] = {
    { "None", "none" }
};

static const ConfigChoice filterModeHChoices[] = {
    { "1x", "1x" },
    { "2x", "2x" },
    { "3x", "3x" },
    { "4x", "4x" }
};

static const ConfigChoice filterModeVChoices[] = {
    { "-", "-" },
    { "1x", "1x" },
    { "2x", "2x" },
    { "3x", "3x" },
    { "4x", "4x" }
};

static const ConfigChoice filterAutoscaleChoices[] = {
    { "Disabled", "none" },
    { "Automatic", "auto" },
    { "TV", "standard" },
    { "Maximum", "max" },
    { "Scale", "scale" },
    { "Resize", "resize" },
    { "Center", "center" },
    { "Manual", "manual" },
    { "Integer", "integer" },
    { "Integer autoscale", "integer_auto" },
    { "Overscan blanking", "overscan_blanking" }
};

static const ConfigChoice filterAutoscaleRtgChoices[] = {
    { "Resize", "resize" },
    { "Scale", "scale" },
    { "Center", "center" },
    { "Integer", "integer" }
};

static const ConfigChoice filterIntegerLimitChoices[] = {
    { "1/1", "1/1" },
    { "1/2", "1/2" },
    { "1/4", "1/4" },
    { "1/8", "1/8" }
};

static const ConfigChoice filterAspectChoices[] = {
    { "VGA", "vga" },
    { "TV", "tv" }
};

static const ConfigChoice rtgAspectRatioChoices[] = {
    { "Disabled", "0:0" },
    { "Automatic", "-1:-1" },
    { "4:3", "4:3" },
    { "5:4", "5:4" },
    { "16:9", "16:9" },
    { "16:10", "16:10" }
};

static QIcon resourceIcon(const QString &name)
{
    const QString path = resourceFile(QStringLiteral("od-win32/resources/") + name);
    return QFileInfo::exists(path) ? QIcon(path) : QIcon();
}

static QLabel *label(const QString &text)
{
    QLabel *w = new QLabel(text);
    w->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return w;
}

static QComboBox *combo(const QStringList &items, const QString &current = QString())
{
    QComboBox *w = new QComboBox;
    w->setEditable(false);
    w->addItems(items);
    if (!current.isEmpty()) {
        const int index = w->findText(current);
        if (index >= 0) {
            w->setCurrentIndex(index);
        }
    }
    return w;
}

static QStringList quickstartModelItems()
{
    QStringList items;
    for (const QuickstartModelChoice &choice : quickstartModelChoices) {
        items.append(QString::fromLatin1(choice.display));
    }
    return items;
}

static const QuickstartModelChoice *quickstartModelChoiceByDisplay(const QString &display)
{
    for (const QuickstartModelChoice &choice : quickstartModelChoices) {
        if (display == QString::fromLatin1(choice.display)) {
            return &choice;
        }
    }
    return nullptr;
}

static const QuickstartModelChoice *quickstartModelChoiceByConfigValue(const QString &value)
{
    for (const QuickstartModelChoice &choice : quickstartModelChoices) {
        if (value.compare(QString::fromLatin1(choice.configValue), Qt::CaseInsensitive) == 0) {
            return &choice;
        }
    }
    return nullptr;
}

static QStringList quickstartConfigItems(const QuickstartModelChoice &choice)
{
    QStringList items;
    for (int i = 0; i < choice.configCount && i < MaxQuickstartConfigs; i++) {
        if (choice.configs[i]) {
            items.append(QString::fromLatin1(choice.configs[i]));
        }
    }
    return items;
}

static QStringList activityPriorityDisplays()
{
    QStringList items;
    for (int i = 0; i < int(sizeof(activityPriorityChoices) / sizeof(activityPriorityChoices[0])); i++) {
        items.append(QString::fromLatin1(activityPriorityChoices[i].display));
    }
    return items;
}

static QString activityPriorityText(int value)
{
    for (int i = 0; i < int(sizeof(activityPriorityChoices) / sizeof(activityPriorityChoices[0])); i++) {
        if (activityPriorityChoices[i].value == value) {
            return QString::fromLatin1(activityPriorityChoices[i].display);
        }
    }
    return QStringLiteral("Normal");
}

static int activityPriorityValue(const QString &text)
{
    for (int i = 0; i < int(sizeof(activityPriorityChoices) / sizeof(activityPriorityChoices[0])); i++) {
        if (text == QString::fromLatin1(activityPriorityChoices[i].display)) {
            return activityPriorityChoices[i].value;
        }
    }
    return 0;
}

static QButtonGroup *radioGroup(const ConfigChoice *choices, int count, int current, QWidget *parent, QGridLayout *layout)
{
    QButtonGroup *group = new QButtonGroup(parent);
    for (int i = 0; i < count; i++) {
        QRadioButton *button = new QRadioButton(QString::fromLatin1(choices[i].display));
        group->addButton(button, i);
        layout->addWidget(button, i / 4, i % 4);
        if (i == current) {
            button->setChecked(true);
        }
    }
    return group;
}

static QComboBox *pathCombo()
{
    QComboBox *w = new QComboBox;
    w->setEditable(true);
    w->setInsertPolicy(QComboBox::NoInsert);
    w->lineEdit()->setClearButtonEnabled(true);
    return w;
}

static QStringList configChoiceDisplays(const ConfigChoice *choices, int count)
{
    QStringList items;
    for (int i = 0; i < count; i++) {
        items.append(QString::fromLatin1(choices[i].display));
    }
    return items;
}

static QString configChoiceValue(const ConfigChoice *choices, int count, const QString &display)
{
    for (int i = 0; i < count; i++) {
        if (display == QString::fromLatin1(choices[i].display)) {
            return QString::fromLatin1(choices[i].value);
        }
    }
    return QString();
}

static QString configChoiceDisplay(const ConfigChoice *choices, int count, const QString &value)
{
    for (int i = 0; i < count; i++) {
        if (value.compare(QString::fromLatin1(choices[i].value), Qt::CaseInsensitive) == 0) {
            return QString::fromLatin1(choices[i].display);
        }
    }
    return QString::fromLatin1(choices[0].display);
}

static QString configChoiceValueAt(const ConfigChoice *choices, int count, int index)
{
    return QString::fromLatin1(choices[qBound(0, index, count - 1)].value);
}

static int configChoiceIndex(const ConfigChoice *choices, int count, const QString &value)
{
    for (int i = 0; i < count; i++) {
        if (value.compare(QString::fromLatin1(choices[i].value), Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return 0;
}

static int configIntegerValue(const QString &value, bool *ok)
{
    bool localOk = false;
    int result = value.trimmed().toInt(&localOk, 0);
    if (!localOk) {
        result = value.trimmed().toInt(&localOk, 16);
    }
    if (ok) {
        *ok = localOk;
    }
    return result;
}

struct WinUaeQtZipEntry {
    QByteArray name;
    QByteArray data;
};

struct WinUaeQtZipCentralEntry {
    QByteArray name;
    quint32 crc = 0;
    quint32 size = 0;
    quint32 offset = 0;
    quint16 time = 0;
    quint16 date = 0;
};

static void appendLe16(QByteArray *out, quint16 value)
{
    out->append(char(value & 0xff));
    out->append(char((value >> 8) & 0xff));
}

static void appendLe32(QByteArray *out, quint32 value)
{
    appendLe16(out, quint16(value & 0xffff));
    appendLe16(out, quint16((value >> 16) & 0xffff));
}

static quint32 crc32ForBytes(const QByteArray &bytes)
{
    quint32 crc = 0xffffffffu;
    for (char ch : bytes) {
        crc ^= quint8(ch);
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xffffffffu;
}

static bool crc32ForFile(const QString &path, quint32 *out, QString *error = nullptr)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    quint32 crc = 0xffffffffu;
    for (;;) {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty()) {
            break;
        }
        for (char ch : chunk) {
            crc ^= quint8(ch);
            for (int bit = 0; bit < 8; bit++) {
                crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
            }
        }
    }
    if (file.error() != QFileDevice::NoError) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    if (out) {
        *out = crc ^ 0xffffffffu;
    }
    return true;
}

static quint32 readBe32(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 4 > data.size()) {
        return 0;
    }
    return (quint32(quint8(data[offset])) << 24)
        | (quint32(quint8(data[offset + 1])) << 16)
        | (quint32(quint8(data[offset + 2])) << 8)
        | quint32(quint8(data[offset + 3]));
}

static QString hex32(quint32 value)
{
    return QStringLiteral("%1").arg(value, 8, 16, QLatin1Char('0')).toUpper();
}

static QString formatByteSize(qint64 bytes)
{
    const double mib = double(bytes) / (1024.0 * 1024.0);
    if (mib >= 1.0) {
        return QStringLiteral("%1 bytes (%2 MiB)").arg(bytes).arg(mib, 0, 'f', 2);
    }
    return QStringLiteral("%1 bytes").arg(bytes);
}

static bool isPlainSectorFloppyImage(const QFileInfo &info)
{
    const QString suffix = info.suffix().toLower();
    return suffix == QStringLiteral("adf")
        || info.size() == 901120
        || info.size() == 1802240;
}

static bool amigaBootBlockChecksumValid(const QByteArray &bootBlock)
{
    if (bootBlock.size() < 1024) {
        return false;
    }

    quint32 sum = 0;
    for (int i = 0; i < 1024; i += 4) {
        quint32 value = readBe32(bootBlock, i);
        if (i == 4) {
            value = 0;
        }
        const quint32 old = sum;
        sum += value;
        if (sum < old) {
            sum++;
        }
    }
    return (sum ^ 0xffffffffu) == readBe32(bootBlock, 4);
}

static QString amigaBootBlockType(const QByteArray &bootBlock)
{
    if (bootBlock.size() < 1024) {
        return QStringLiteral("Unavailable");
    }

    QByteArray zeroed = bootBlock;
    zeroed[4] = 0;
    zeroed[5] = 0;
    zeroed[6] = 0;
    zeroed[7] = 0;

    const quint32 dos = readBe32(bootBlock, 0);
    if (crc32ForBytes(zeroed.left(0x31)) == 0xae5e282cu) {
        return QStringLiteral("Standard 1.x");
    }
    if (dos >= 0x444f5300u && dos <= 0x444f5307u
        && crc32ForBytes(zeroed.mid(8, 0x5c - 8)) == 0xe158ca4bu) {
        return QStringLiteral("Standard 2.x+");
    }
    if (dos == 0x4b49434bu) {
        return QStringLiteral("Kickstart");
    }
    return QStringLiteral("Custom");
}

static QString amigaRootBlockLabel(const QByteArray &rootBlock)
{
    if (rootBlock.size() < 512 || readBe32(rootBlock, 0) != 2 || readBe32(rootBlock, 508) != 1) {
        return QString();
    }

    const int nameOffset = 512 - 20 * 4;
    const int nameLength = quint8(rootBlock[nameOffset]);
    if (nameLength <= 0 || nameOffset + 1 + nameLength > rootBlock.size()) {
        return QString();
    }
    return QString::fromLatin1(rootBlock.constData() + nameOffset + 1, nameLength);
}

static void currentDosDateTime(quint16 *dosDate, quint16 *dosTime)
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDate date = now.date();
    const QTime time = now.time();
    const int year = qBound(1980, date.year(), 2107);
    *dosDate = quint16(((year - 1980) << 9) | (date.month() << 5) | date.day());
    *dosTime = quint16((time.hour() << 11) | (time.minute() << 5) | (time.second() / 2));
}

static bool writeStoredZip(const QString &path, const QVector<WinUaeQtZipEntry> &entries, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    QByteArray out;
    QVector<WinUaeQtZipCentralEntry> centralEntries;
    for (const WinUaeQtZipEntry &entry : entries) {
        if (entry.name.isEmpty() || entry.data.size() > INT_MAX) {
            continue;
        }

        WinUaeQtZipCentralEntry central;
        central.name = entry.name;
        central.crc = crc32ForBytes(entry.data);
        central.size = quint32(entry.data.size());
        central.offset = quint32(out.size());
        currentDosDateTime(&central.date, &central.time);

        appendLe32(&out, 0x04034b50u);
        appendLe16(&out, 20);
        appendLe16(&out, 0);
        appendLe16(&out, 0);
        appendLe16(&out, central.time);
        appendLe16(&out, central.date);
        appendLe32(&out, central.crc);
        appendLe32(&out, central.size);
        appendLe32(&out, central.size);
        appendLe16(&out, quint16(central.name.size()));
        appendLe16(&out, 0);
        out.append(central.name);
        out.append(entry.data);
        centralEntries.append(central);
    }

    const quint32 centralOffset = quint32(out.size());
    for (const WinUaeQtZipCentralEntry &central : std::as_const(centralEntries)) {
        appendLe32(&out, 0x02014b50u);
        appendLe16(&out, 20);
        appendLe16(&out, 20);
        appendLe16(&out, 0);
        appendLe16(&out, 0);
        appendLe16(&out, central.time);
        appendLe16(&out, central.date);
        appendLe32(&out, central.crc);
        appendLe32(&out, central.size);
        appendLe32(&out, central.size);
        appendLe16(&out, quint16(central.name.size()));
        appendLe16(&out, 0);
        appendLe16(&out, 0);
        appendLe16(&out, 0);
        appendLe16(&out, 0);
        appendLe32(&out, 0);
        appendLe32(&out, central.offset);
        out.append(central.name);
    }
    const quint32 centralSize = quint32(out.size()) - centralOffset;

    appendLe32(&out, 0x06054b50u);
    appendLe16(&out, 0);
    appendLe16(&out, 0);
    appendLe16(&out, quint16(centralEntries.size()));
    appendLe16(&out, quint16(centralEntries.size()));
    appendLe32(&out, centralSize);
    appendLe32(&out, centralOffset);
    appendLe16(&out, 0);

    if (file.write(out) != out.size()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

static QString chipsetRevisionText(int value)
{
    if (value < 0) {
        return QString();
    }
    return QStringLiteral("%1").arg(qBound(0, value, 255), 2, 16, QLatin1Char('0')).toUpper();
}

static QString chipsetRevisionConfigValue(QCheckBox *enabled, QLineEdit *field, int defaultValue)
{
    if (!enabled || !enabled->isChecked()) {
        return QStringLiteral("-1");
    }
    bool ok = false;
    int value = field ? field->text().trimmed().toInt(&ok, 16) : defaultValue;
    if (!ok) {
        value = defaultValue;
    }
    return QString::number(qBound(0, value, 255));
}

static QString filterSuffix(int target)
{
    return QString::fromLatin1(filterTargetChoices[qBound(0, target, 2)].value);
}

static QString filterKey(const QString &base, int target)
{
    return base + filterSuffix(target);
}

static bool isRtgAutoscaleValue(const QString &value)
{
    for (const ConfigChoice &choice : filterAutoscaleRtgChoices) {
        if (value.compare(QString::fromLatin1(choice.value), Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

static QString filterAutoscaleConfigValue(const QString &display, int target)
{
    QString value = configChoiceValue(
        target == 1 ? filterAutoscaleRtgChoices : filterAutoscaleChoices,
        target == 1 ? int(sizeof(filterAutoscaleRtgChoices) / sizeof(filterAutoscaleRtgChoices[0])) : int(sizeof(filterAutoscaleChoices) / sizeof(filterAutoscaleChoices[0])),
        display);
    if (target == 1 && !isRtgAutoscaleValue(value)) {
        value = QStringLiteral("resize");
    }
    return value;
}

static QString filterAutoscaleDisplay(const QString &value, int target)
{
    return configChoiceDisplay(
        target == 1 ? filterAutoscaleRtgChoices : filterAutoscaleChoices,
        target == 1 ? int(sizeof(filterAutoscaleRtgChoices) / sizeof(filterAutoscaleRtgChoices[0])) : int(sizeof(filterAutoscaleChoices) / sizeof(filterAutoscaleChoices[0])),
        target == 1 && !isRtgAutoscaleValue(value) ? QStringLiteral("resize") : value);
}

static QStringList unixSerialPortItems()
{
    QStringList items { QStringLiteral("<None>") };
    items.append(QStringLiteral("TCP://0.0.0.0:1234"));
    items.append(QStringLiteral("TCP://0.0.0.0:1234/wait"));
    const QStringList filters {
        QStringLiteral("cu.*"),
        QStringLiteral("tty.*"),
        QStringLiteral("ttyUSB*"),
        QStringLiteral("ttyACM*"),
        QStringLiteral("ttyS*")
    };
    const QStringList entries = QDir(QStringLiteral("/dev")).entryList(filters, QDir::System | QDir::Files | QDir::Readable, QDir::Name);
    for (const QString &entry : entries) {
        const QString path = QDir(QStringLiteral("/dev")).filePath(entry);
        if (!items.contains(path)) {
            items.append(path);
        }
    }
    return items;
}

struct WinUaeQtExpansionCategoryChoice {
    const char *display;
    int mask;
};

static const WinUaeQtExpansionCategoryChoice expansionBoardCategoryChoices[] = {
    { "Built-in expansions", WinUaeQtExpansionCategoryInternal },
    { "SCSI controllers", WinUaeQtExpansionCategoryScsi },
    { "IDE controllers", WinUaeQtExpansionCategoryIde },
    { "SASI controllers", WinUaeQtExpansionCategorySasi },
    { "Custom disk controllers", WinUaeQtExpansionCategoryCustom },
    { "PCI bridgeboards", WinUaeQtExpansionCategoryPciBridge },
    { "PC bridgeboards", WinUaeQtExpansionCategoryX86Bridge },
    { "RTG board ROMs", WinUaeQtExpansionCategoryRtg },
    { "Sound boards", WinUaeQtExpansionCategorySound },
    { "Network adapters", WinUaeQtExpansionCategoryNet },
    { "Floppy controllers", WinUaeQtExpansionCategoryFloppy },
    { "PC expansions", WinUaeQtExpansionCategoryX86Expansion },
    { nullptr, 0 }
};

static bool expansionBoardMatchesCategory(const WinUaeQtExpansionBoardCatalogItem &board, const WinUaeQtExpansionCategoryChoice &category)
{
    if (!(board.categoryMask & category.mask)) {
        return false;
    }
    if (category.mask == WinUaeQtExpansionCategoryInternal
        && (board.categoryMask & (WinUaeQtExpansionCategorySasi | WinUaeQtExpansionCategoryCustom))) {
        return false;
    }
    if ((board.categoryMask & WinUaeQtExpansionCategoryX86Expansion)
        && category.mask != WinUaeQtExpansionCategoryX86Expansion) {
        return false;
    }
    return true;
}

static QString expansionBoardSortText(const QString &display)
{
    const int manufacturer = display.lastIndexOf(QStringLiteral(" ("));
    if (manufacturer > 0 && display.endsWith(QLatin1Char(')'))) {
        return display.left(manufacturer);
    }
    return display;
}

static const WinUaeQtExpansionCategoryChoice *expansionBoardCategoryByDisplay(const QString &display)
{
    for (const WinUaeQtExpansionCategoryChoice *category = expansionBoardCategoryChoices; category->display; category++) {
        if (display == QString::fromLatin1(category->display)) {
            return category;
        }
    }
    return nullptr;
}

static QStringList expansionBoardCategoryItems(const WinUaeQtBoardCatalog &catalog)
{
    QStringList items;
    for (const WinUaeQtExpansionCategoryChoice *category = expansionBoardCategoryChoices; category->display; category++) {
        const bool hasBoard = std::any_of(catalog.expansionBoards.constBegin(), catalog.expansionBoards.constEnd(), [category](const WinUaeQtExpansionBoardCatalogItem &board) {
            return expansionBoardMatchesCategory(board, *category);
        });
        if (hasBoard) {
            items.append(QString::fromLatin1(category->display));
        }
    }
    return items;
}

static const WinUaeQtExpansionBoardCatalogItem *expansionBoardChoiceByKey(const WinUaeQtBoardCatalog &catalog, const QString &key)
{
    for (const WinUaeQtExpansionBoardCatalogItem &choice : catalog.expansionBoards) {
        if (key.compare(choice.key, Qt::CaseInsensitive) == 0) {
            return &choice;
        }
    }
    return nullptr;
}

static const WinUaeQtExpansionBoardCatalogItem *expansionBoardChoiceByDisplay(const WinUaeQtBoardCatalog &catalog, const QString &display)
{
    for (const WinUaeQtExpansionBoardCatalogItem &choice : catalog.expansionBoards) {
        if (display == choice.display) {
            return &choice;
        }
    }
    return nullptr;
}

static bool expansionBoardNoRomFile(const WinUaeQtExpansionBoardCatalogItem *board, const QString &subtype)
{
    if (!board) {
        return false;
    }
    for (const WinUaeQtBoardSubtype &choice : board->subtypes) {
        if (subtype.compare(choice.configValue, Qt::CaseInsensitive) == 0 && choice.hasRomTypeOverride) {
            return choice.noRomFile;
        }
    }
    return board->noRomFile;
}

static const WinUaeQtCpuBoardCatalogItem *cpuBoardSubtypeChoiceByConfig(const WinUaeQtBoardCatalog &catalog, const QString &configValue)
{
    for (const WinUaeQtCpuBoardCatalogItem &choice : catalog.cpuBoards) {
        if (configValue.compare(choice.configValue, Qt::CaseInsensitive) == 0) {
            return &choice;
        }
    }
    return nullptr;
}

static bool cpuBoardTypeHasSubtypes(const WinUaeQtBoardCatalog &catalog, const QString &type)
{
    return std::any_of(catalog.cpuBoards.constBegin(), catalog.cpuBoards.constEnd(), [&type](const WinUaeQtCpuBoardCatalogItem &choice) {
        return type == choice.type;
    });
}

static const WinUaeQtBoardSetting *boardSettingChoiceByConfig(const QVector<WinUaeQtBoardSetting> &settings, const QString &configValue)
{
    for (const WinUaeQtBoardSetting &choice : settings) {
        if (configValue.compare(choice.configValue, Qt::CaseInsensitive) == 0) {
            return &choice;
        }
    }
    return nullptr;
}

static QString expansionBoardConfigName(const QString &key, int slot)
{
    if (key.isEmpty()) {
        return QString();
    }
    return slot > 0 ? QStringLiteral("%1-%2").arg(key).arg(slot + 1) : key;
}

static QString expansionBoardBaseKey(const QString &configName, int *slot)
{
    if (slot) {
        *slot = 0;
    }
    const int dash = configName.lastIndexOf(QLatin1Char('-'));
    if (dash <= 0) {
        return configName;
    }
    bool ok = false;
    const int oneBasedSlot = configName.mid(dash + 1).toInt(&ok);
    if (!ok || oneBasedSlot < 2) {
        return configName;
    }
    if (slot) {
        *slot = oneBasedSlot - 1;
    }
    return configName.left(dash);
}

static QStringList expansionOptionTokens(const QString &options)
{
    QStringList tokens;
    for (QString token : options.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        token = token.trimmed();
        if (!token.isEmpty()) {
            tokens.append(token);
        }
    }
    return tokens;
}

static bool expansionOptionContains(const QString &options, const QString &name)
{
    for (const QString &token : expansionOptionTokens(options)) {
        if (token.compare(name, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

static bool expansionOptionBool(const QString &options, const QString &name)
{
    const QString prefix = name + QLatin1Char('=');
    for (const QString &token : expansionOptionTokens(options)) {
        if (token.compare(name, Qt::CaseInsensitive) == 0) {
            return true;
        }
        if (token.startsWith(prefix, Qt::CaseInsensitive)) {
            const QString value = token.mid(prefix.size()).trimmed().toLower();
            return value == QStringLiteral("true")
                || value == QStringLiteral("yes")
                || value == QStringLiteral("on")
                || value == QStringLiteral("1");
        }
    }
    return false;
}

static QString expansionOptionValue(const QString &options, const QString &name)
{
    const QString prefix = name + QLatin1Char('=');
    for (const QString &token : expansionOptionTokens(options)) {
        if (token.startsWith(prefix, Qt::CaseInsensitive)) {
            return token.mid(prefix.size());
        }
    }
    return QString();
}

static QString expansionOptionsWithValue(const QString &options, const QString &name, const QString &value)
{
    QStringList tokens;
    const QString prefix = name + QLatin1Char('=');
    bool inserted = false;
    for (const QString &token : expansionOptionTokens(options)) {
        if (token.compare(name, Qt::CaseInsensitive) == 0 || token.startsWith(prefix, Qt::CaseInsensitive)) {
            if (!value.isEmpty() && !inserted) {
                tokens.append(prefix + value);
                inserted = true;
            }
        } else {
            tokens.append(token);
        }
    }
    if (!value.isEmpty() && !inserted) {
        tokens.append(prefix + value);
    }
    return tokens.join(QLatin1Char(','));
}

static QString expansionBoardOptionsValue(
    const WinUaeQtExpansionBoardState &state,
    const WinUaeQtExpansionBoardCatalogItem *board)
{
    QStringList tokens;
    const QStringList replaced {
        QStringLiteral("autoboot_disabled"),
        QStringLiteral("dma24bit"),
        QStringLiteral("inserted"),
        QStringLiteral("id"),
        QStringLiteral("subtype")
    };
    for (const QString &token : expansionOptionTokens(state.rawOptions)) {
        const QString name = token.section(QLatin1Char('='), 0, 0).trimmed();
        bool known = false;
        for (const QString &replacement : replaced) {
            if (name.compare(replacement, Qt::CaseInsensitive) == 0) {
                known = true;
                break;
            }
        }
        if (board) {
            for (const WinUaeQtBoardSetting &choice : board->settings) {
                if (choice.type == WinUaeQtBoardSettingType::CheckBox) {
                    known = name.compare(choice.configValue, Qt::CaseInsensitive) == 0;
                } else if (choice.type == WinUaeQtBoardSettingType::String) {
                    known = name.compare(choice.configValue, Qt::CaseInsensitive) == 0;
                } else if (choice.type == WinUaeQtBoardSettingType::Multi) {
                    for (const QString &value : choice.multiValues) {
                        if (name.compare(value, Qt::CaseInsensitive) == 0) {
                            known = true;
                            break;
                        }
                    }
                }
                if (known) {
                    known = true;
                    break;
                }
            }
        }
        if (!known) {
            tokens.append(token);
        }
    }
    if (!state.subtype.isEmpty()) {
        tokens.append(QStringLiteral("subtype=%1").arg(state.subtype));
    }
    if (state.autobootDisabled) {
        tokens.append(QStringLiteral("autoboot_disabled=true"));
    }
    if (state.dma24Bit) {
        tokens.append(QStringLiteral("dma24bit=true"));
    }
    if (state.inserted) {
        tokens.append(QStringLiteral("inserted=true"));
    }
    if (state.id != 7 || !expansionOptionValue(state.rawOptions, QStringLiteral("id")).isEmpty()) {
        tokens.append(QStringLiteral("id=%1").arg(qBound(0, state.id, 7)));
    }
    if (board) {
        for (const WinUaeQtBoardSetting &choice : board->settings) {
            if (choice.type == WinUaeQtBoardSettingType::CheckBox) {
                if (state.optionBools.value(choice.configValue, false)) {
                    tokens.append(choice.configValue);
                }
            } else if (choice.type == WinUaeQtBoardSettingType::String) {
                const QString value = state.optionValues.value(choice.configValue).trimmed();
                if (!value.isEmpty()) {
                    tokens.append(QStringLiteral("%1=%2").arg(choice.configValue, value));
                }
            } else if (choice.type == WinUaeQtBoardSettingType::Multi) {
                const QString value = state.optionValues.value(choice.configValue);
                if (!value.isEmpty()) {
                    tokens.append(value);
                }
            }
        }
    }
    return tokens.join(QLatin1Char(','));
}

static QStringList floppyTypeItems(int drive, bool quickstart)
{
    QStringList items = {
        QStringLiteral("3.5 DD"),
        QStringLiteral("3.5 HD"),
        QStringLiteral("Disabled")
    };
    if (quickstart) {
        return items;
    }
    items.insert(2, QStringLiteral("5.25 SD"));
    items.insert(3, QStringLiteral("5.25 (80)"));
    items.insert(4, QStringLiteral("3.5 DD (Escom)"));
    if (drive >= 2) {
        items.insert(5, QStringLiteral("Bridgeboard 5.25 40"));
        items.insert(6, QStringLiteral("Bridgeboard 5.25 80"));
        items.insert(7, QStringLiteral("Bridgeboard 3.5 80"));
    }
    return items;
}

static int floppyTypeConfigValue(const QString &text)
{
    if (text == QStringLiteral("Disabled")) {
        return -1;
    }
    if (text == QStringLiteral("3.5 HD")) {
        return 1;
    }
    if (text == QStringLiteral("5.25 SD")) {
        return 2;
    }
    if (text == QStringLiteral("3.5 DD (Escom)")) {
        return 3;
    }
    if (text == QStringLiteral("Bridgeboard 5.25 40")) {
        return 4;
    }
    if (text == QStringLiteral("Bridgeboard 3.5 80")) {
        return 5;
    }
    if (text == QStringLiteral("Bridgeboard 5.25 80")) {
        return 6;
    }
    if (text == QStringLiteral("5.25 (80)")) {
        return 7;
    }
    return 0;
}

static QString floppyTypeText(int value)
{
    if (value < 0) {
        return QStringLiteral("Disabled");
    }
    if (value == 1) {
        return QStringLiteral("3.5 HD");
    }
    if (value == 2) {
        return QStringLiteral("5.25 SD");
    }
    if (value == 3) {
        return QStringLiteral("3.5 DD (Escom)");
    }
    if (value == 4) {
        return QStringLiteral("Bridgeboard 5.25 40");
    }
    if (value == 5) {
        return QStringLiteral("Bridgeboard 3.5 80");
    }
    if (value == 6) {
        return QStringLiteral("Bridgeboard 5.25 80");
    }
    if (value == 7) {
        return QStringLiteral("5.25 (80)");
    }
    return QStringLiteral("3.5 DD");
}

static int floppySpeedConfigValue(int sliderPosition)
{
    if (sliderPosition <= 0) {
        return 0;
    }
    return 100 << qBound(0, sliderPosition - 1, 3);
}

static int floppySpeedSliderPosition(int value)
{
    if (value <= 0) {
        return 0;
    }
    if (value <= 100) {
        return 1;
    }
    if (value <= 200) {
        return 2;
    }
    if (value <= 400) {
        return 3;
    }
    return 4;
}

static QString floppySpeedText(int value)
{
    if (value <= 0) {
        return QStringLiteral("Turbo");
    }
    if (value == 100) {
        return QStringLiteral("100% (Compatible)");
    }
    return QStringLiteral("%1%").arg(value);
}

static int jitCacheSizeFromPosition(int position)
{
    if (position <= 0) {
        return 0;
    }
    return 1024 << qBound(0, position - 1, 7);
}

static int jitCachePositionFromSize(int size)
{
    if (size <= 0) {
        return 0;
    }
    int position = 1;
    int cacheSize = 1024;
    while (position < 8 && size > cacheSize) {
        cacheSize <<= 1;
        position++;
    }
    return position;
}

static QString jitCacheText(int size)
{
    return QStringLiteral("%1 MB").arg(size > 0 ? size / 1024 : 0);
}

static int defaultJitCacheSize()
{
    return 8192;
}

static int cpuMultiplierValue(const QString &text)
{
    if (text.startsWith(QStringLiteral("2x"))) {
        return 2;
    }
    if (text.startsWith(QStringLiteral("4x"))) {
        return 4;
    }
    if (text.startsWith(QStringLiteral("8x"))) {
        return 8;
    }
    if (text.startsWith(QStringLiteral("16x"))) {
        return 16;
    }
    return 1;
}

static QString cpuMultiplierText(int value)
{
    switch (value) {
    case 2:
        return QStringLiteral("2x (A500)");
    case 4:
        return QStringLiteral("4x (A1200)");
    case 8:
        return QStringLiteral("8x");
    case 16:
        return QStringLiteral("16x");
    default:
        return QStringLiteral("1x");
    }
}

static QString chipsetConfigValue(const QString &text)
{
    if (text == QStringLiteral("A1000 (No EHB)")) {
        return QStringLiteral("a1000_noehb");
    }
    if (text == QStringLiteral("A1000")) {
        return QStringLiteral("a1000");
    }
    if (text == QStringLiteral("ECS Agnus")) {
        return QStringLiteral("ecs_agnus");
    }
    if (text == QStringLiteral("ECS Denise")) {
        return QStringLiteral("ecs_denise");
    }
    if (text == QStringLiteral("ECS")) {
        return QStringLiteral("ecs");
    }
    if (text == QStringLiteral("AGA")) {
        return QStringLiteral("aga");
    }
    return QStringLiteral("ocs");
}

static QString chipsetText(const QString &value)
{
    const QString lower = value.toLower();
    if (lower == QStringLiteral("a1000_noehb")) {
        return QStringLiteral("A1000 (No EHB)");
    }
    if (lower == QStringLiteral("a1000")) {
        return QStringLiteral("A1000");
    }
    if (lower == QStringLiteral("ecs_agnus")) {
        return QStringLiteral("ECS Agnus");
    }
    if (lower == QStringLiteral("ecs_denise")) {
        return QStringLiteral("ECS Denise");
    }
    if (lower == QStringLiteral("ecs")) {
        return QStringLiteral("ECS");
    }
    if (lower == QStringLiteral("aga")) {
        return QStringLiteral("AGA");
    }
    return QStringLiteral("OCS");
}

static QString displayOptimizationConfigValue(const QString &text)
{
    if (text == QStringLiteral("Partial")) {
        return QStringLiteral("partial");
    }
    if (text == QStringLiteral("None")) {
        return QStringLiteral("none");
    }
    return QStringLiteral("full");
}

static QString displayOptimizationText(const QString &value)
{
    const QString lower = value.toLower();
    if (lower == QStringLiteral("partial")) {
        return QStringLiteral("Partial");
    }
    if (lower == QStringLiteral("none")) {
        return QStringLiteral("None");
    }
    return QStringLiteral("Full");
}

static bool configBoolValue(const QString &value)
{
    return value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
        || value.compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0
        || value == QStringLiteral("1");
}

static QString configBoolText(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

static QString amigaDiskImageFilter()
{
    return QStringLiteral("Amiga disk images (*.adf *.adz *.dms *.wrp *.ipf *.fdi *.scp *.hdf *.chd *.zip *.7z *.rar *.lha *.lzh *.lzx);;All files (*)");
}

static QString floppyDiskImageFilter()
{
    return QStringLiteral("Amiga disk images (*.adf *.adz *.dms *.wrp *.ipf *.zip *.7z *.rar *.lha *.lzh *.lzx);;All files (*)");
}

static QString cdImageFilter()
{
    return QStringLiteral("CD images (*.cue *.ccd *.mds *.iso *.chd *.nrg *.zip *.7z *.rar *.lha *.lzh *.lzx);;All files (*)");
}

static QString hardfileImageFilter()
{
    return QStringLiteral("Hardfiles (*.hdf *.hda *.vhd *.rdf *.hdz *.rdz *.chd);;All files (*)");
}

static QString directoryArchiveFilter()
{
    return QStringLiteral("Directory archives (*.zip *.7z *.rar *.lha *.lzh *.lzx);;All files (*)");
}

struct WinUaeQtNativeDriveChoice {
    QString display;
    QString configPath;
    quint64 size = 0;
    int blockSize = 512;
};

static QString formatNativeDriveSize(quint64 bytes)
{
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        return QStringLiteral("%1G").arg(double(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
    }
    if (bytes >= 1024ULL * 1024ULL) {
        return QStringLiteral("%1M").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    }
    return QStringLiteral("%1K").arg(bytes / 1024ULL);
}

static bool queryNativeDriveGeometry(const QString &path, quint64 *size, int *blockSize)
{
    if (size) {
        *size = 0;
    }
    if (blockSize) {
        *blockSize = 512;
    }
#if defined(__APPLE__) || defined(__linux__)
    const QByteArray localPath = QFile::encodeName(path);
    const int fd = open(localPath.constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
#if defined(__APPLE__)
    uint32_t nativeBlockSize = 512;
    uint64_t nativeBlockCount = 0;
    if (ioctl(fd, DKIOCGETBLOCKSIZE, &nativeBlockSize) == 0 && nativeBlockSize > 0 && blockSize) {
        *blockSize = int(nativeBlockSize);
    }
    if (ioctl(fd, DKIOCGETBLOCKCOUNT, &nativeBlockCount) == 0 && size) {
        *size = nativeBlockCount * quint64(blockSize ? *blockSize : int(nativeBlockSize));
    }
#elif defined(__linux__)
    unsigned long long nativeSize = 0;
    int nativeBlockSize = 512;
    if (ioctl(fd, BLKSSZGET, &nativeBlockSize) == 0 && nativeBlockSize > 0 && blockSize) {
        *blockSize = nativeBlockSize;
    }
    if (ioctl(fd, BLKGETSIZE64, &nativeSize) == 0 && size) {
        *size = quint64(nativeSize);
    }
#endif
    close(fd);
    return size && *size > 0;
#else
    Q_UNUSED(path);
    return false;
#endif
}

static void appendNativeDriveChoice(QVector<WinUaeQtNativeDriveChoice> *choices, QSet<QString> *seen, const QString &path, const QString &name)
{
    if (!choices || !seen || seen->contains(path)) {
        return;
    }
    quint64 size = 0;
    int blockSize = 512;
    if (!queryNativeDriveGeometry(path, &size, &blockSize)) {
        return;
    }
    seen->insert(path);
    WinUaeQtNativeDriveChoice choice;
    choice.size = size;
    choice.blockSize = blockSize;
    choice.configPath = QStringLiteral(":%1").arg(path);
    choice.display = QStringLiteral("[OS] [%1,RO] %2").arg(formatNativeDriveSize(size), path);
    if (!name.isEmpty() && name != path) {
        choice.display += QStringLiteral(" (%1)").arg(name);
    }
    choices->append(choice);
}

static QVector<WinUaeQtNativeDriveChoice> nativeHardDriveChoices()
{
    QVector<WinUaeQtNativeDriveChoice> choices;
    QSet<QString> seen;
#if UAE_UNIX_WITH_NATIVE_HARDDRIVES
#if defined(__APPLE__)
    const QDir dev(QStringLiteral("/dev"));
    for (const QString &name : dev.entryList({ QStringLiteral("rdisk*") }, QDir::System | QDir::Files, QDir::Name)) {
        if (name.mid(5).contains(QLatin1Char('s'))) {
            continue;
        }
        appendNativeDriveChoice(&choices, &seen, dev.absoluteFilePath(name), name);
    }
#elif defined(__linux__)
    const QDir sysBlock(QStringLiteral("/sys/block"));
    for (const QString &name : sysBlock.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (name.startsWith(QStringLiteral("loop"))
            || name.startsWith(QStringLiteral("ram"))
            || name.startsWith(QStringLiteral("zram"))
            || name.startsWith(QStringLiteral("dm-"))) {
            continue;
        }
        appendNativeDriveChoice(&choices, &seen, QStringLiteral("/dev/%1").arg(name), name);
    }
#endif
#endif
    return choices;
}

static bool genlockModeUsesImageFile(const QString &mode)
{
    return mode == QStringLiteral("image");
}

static bool genlockModeUsesVideoFile(const QString &mode)
{
    return mode == QStringLiteral("video")
        || mode == QStringLiteral("ld")
        || mode == QStringLiteral("sony_ld")
        || mode == QStringLiteral("pioneer_ld");
}

static void insertLineEditSetting(WinUaeQtConfig::Settings &settings, const QString &key, const QLineEdit *field)
{
    if (!field) {
        return;
    }
    const QString value = field->text().trimmed();
    if (!value.isEmpty()) {
        settings.insert(key, value);
    }
}

static void insertCheckBoxSetting(WinUaeQtConfig::Settings &settings, const QString &key, const QCheckBox *field)
{
    if (field) {
        settings.insert(key, configBoolText(field->isChecked()));
    }
}

static void setComboTextIfChanged(QComboBox *combo, const QString &text)
{
    if (combo && combo->currentText() != text) {
        combo->setCurrentText(text);
    }
}

static void setCheckBoxIfChanged(QCheckBox *box, bool checked)
{
    if (box && box->isChecked() != checked) {
        box->setChecked(checked);
    }
}

static QPushButton *smallButton(const QString &text)
{
    QPushButton *w = new QPushButton(text);
    w->setFixedWidth(text == QStringLiteral("...") ? 24 : 34);
    return w;
}

static void disableUnavailable(QWidget *widget, const QString &reason)
{
    widget->setEnabled(false);
    widget->setToolTip(reason);
}

static void setComboItemEnabled(QComboBox *combo, int index, bool enabled, const QString &reason = QString())
{
    if (!combo || index < 0 || index >= combo->count()) {
        return;
    }
    if (QStandardItemModel *model = qobject_cast<QStandardItemModel *>(combo->model())) {
        if (QStandardItem *item = model->item(index)) {
            item->setEnabled(enabled);
            item->setToolTip(enabled ? QString() : reason);
        }
    }
}

static QString winUaeQtInputEventDisplayName(const QString &configName)
{
    if (configName.isEmpty()) {
        return QStringLiteral("<None>");
    }
    for (const WinUaeQtInputEventChoice &choice : inputEventChoices) {
        if (configName.compare(QString::fromLatin1(choice.config), Qt::CaseInsensitive) == 0) {
            return QString::fromLatin1(choice.display);
        }
    }
    for (const WinUaeQtKeyboardChoice &choice : keyboardChoices) {
        if (choice.defaultEvent[0] && configName.compare(QString::fromLatin1(choice.defaultEvent), Qt::CaseInsensitive) == 0) {
            return QString::fromLatin1(choice.display);
        }
    }
    return configName;
}

static QStringList winUaeQtInputSlotFields(const QString &value)
{
    QStringList fields;
    QString field;
    bool quoted = false;
    for (QChar ch : value) {
        if (ch == QLatin1Char('\'')) {
            quoted = !quoted;
        }
        if (ch == QLatin1Char(',') && !quoted) {
            fields.append(field.trimmed());
            field.clear();
            continue;
        }
        field.append(ch);
    }
    if (!field.isEmpty() || value.endsWith(QLatin1Char(','))) {
        fields.append(field.trimmed());
    }
    return fields;
}

static WinUaeQtInputSlot winUaeQtParseInputSlot(QString raw)
{
    WinUaeQtInputSlot slot;
    raw = raw.trimmed();
    if (raw.isEmpty() || raw.compare(QStringLiteral("NULL"), Qt::CaseInsensitive) == 0) {
        return slot;
    }

    QString rest;
    if (raw.startsWith(QLatin1Char('\''))) {
        const int endQuote = raw.indexOf(QLatin1Char('\''), 1);
        if (endQuote > 0) {
            slot.custom = true;
            slot.event = raw.mid(1, endQuote - 1);
            rest = raw.mid(endQuote + 1);
        } else {
            slot.event = raw;
        }
    } else {
        const int dot = raw.indexOf(QLatin1Char('.'));
        if (dot >= 0) {
            slot.event = raw.left(dot);
            rest = raw.mid(dot);
        } else {
            slot.event = raw;
        }
    }

    if (rest.startsWith(QLatin1Char('.'))) {
        rest.remove(0, 1);
        int pos = 0;
        while (pos < rest.size() && rest[pos].isDigit()) {
            pos++;
        }
        if (pos > 0) {
            slot.flags = rest.left(pos).toInt();
            rest.remove(0, pos);
        }
        if (rest.startsWith(QLatin1Char('.'))) {
            int qpos = 1;
            while (qpos < rest.size() && rest[qpos].isLetter()) {
                qpos++;
            }
            if (qpos > 1) {
                slot.qualifiers = rest.mid(1, qpos - 1);
                rest.remove(0, qpos);
            }
        }
        slot.suffix = rest;
    }
    return slot;
}

static QString winUaeQtFormatInputSlot(const WinUaeQtInputSlot &slot)
{
    if (slot.event.trimmed().isEmpty()) {
        return QStringLiteral("NULL");
    }

    QString text = slot.custom
        ? QStringLiteral("'%1'").arg(slot.event)
        : slot.event;
    text += QStringLiteral(".%1").arg(slot.flags);
    if (!slot.qualifiers.isEmpty()) {
        text += QLatin1Char('.');
        text += slot.qualifiers;
    }
    text += slot.suffix;
    return text;
}

static bool winUaeQtIsInputDeviceConfigKey(const QString &key)
{
    const QStringList parts = key.split(QLatin1Char('.'));
    if (parts.size() < 5 || parts.value(0) != QStringLiteral("input")) {
        return false;
    }
    bool ok = false;
    const int config = parts.value(1).toInt(&ok);
    if (!ok || config < 1 || config > GamePortsInputConfigLine) {
        return false;
    }
    const QString type = parts.value(2);
    if (type != QStringLiteral("joystick") && type != QStringLiteral("mouse")
        && type != QStringLiteral("keyboard") && type != QStringLiteral("internal")) {
        return false;
    }
    parts.value(3).toInt(&ok);
    if (!ok) {
        return false;
    }
    const QString field = parts.value(4);
    return field == QStringLiteral("axis")
        || field == QStringLiteral("button")
        || field == QStringLiteral("friendlyname")
        || field == QStringLiteral("name")
        || field == QStringLiteral("empty")
        || field == QStringLiteral("disabled")
        || field == QStringLiteral("custom");
}

static bool winUaeQtIsInputWidgetMappingKey(const QString &key)
{
    const QStringList parts = key.split(QLatin1Char('.'));
    return winUaeQtIsInputDeviceConfigKey(key)
        && (parts.value(4) == QStringLiteral("axis") || parts.value(4) == QStringLiteral("button"));
}

static QString winUaeQtSwapInputEventPorts(QString event)
{
    static const QPair<QString, QString> pairs[] = {
        { QStringLiteral("JOY1_"), QStringLiteral("JOY2_") },
        { QStringLiteral("MOUSE1_"), QStringLiteral("MOUSE2_") },
        { QStringLiteral("PAR_JOY1_"), QStringLiteral("PAR_JOY2_") }
    };
    for (const auto &pair : pairs) {
        if (event.startsWith(pair.first)) {
            return pair.second + event.mid(pair.first.size());
        }
        if (event.startsWith(pair.second)) {
            return pair.first + event.mid(pair.second.size());
        }
    }
    return event;
}

#ifdef UAE_UNIX_WITH_SDL3
enum class UnixQtInputWidgetKind {
    Axis,
    Button,
    GamepadDpadX,
    GamepadDpadY,
    JoystickHatX,
    JoystickHatY
};

struct UnixQtInputTestWidget {
    QString name;
    UnixQtInputWidgetKind kind = UnixQtInputWidgetKind::Axis;
    int code = 0;
    int mappingIndex = 0;
    QChar mappingType = QLatin1Char('a');
    int state = 0;
    QTreeWidgetItem *item = nullptr;
};

struct UnixQtInputTestDevice {
    QString name;
    QString uniqueName;
    SDL_JoystickID instanceId = 0;
    SDL_Gamepad *gamepad = nullptr;
    SDL_Joystick *joystick = nullptr;
    QVector<UnixQtInputTestWidget> widgets;
};

static QString sdlGamepadAxisName(SDL_GamepadAxis axis)
{
    switch (axis) {
    case SDL_GAMEPAD_AXIS_LEFTX:
        return QStringLiteral("Left X Axis");
    case SDL_GAMEPAD_AXIS_LEFTY:
        return QStringLiteral("Left Y Axis");
    case SDL_GAMEPAD_AXIS_RIGHTX:
        return QStringLiteral("Right X Axis");
    case SDL_GAMEPAD_AXIS_RIGHTY:
        return QStringLiteral("Right Y Axis");
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
        return QStringLiteral("Left Trigger");
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
        return QStringLiteral("Right Trigger");
    default:
        return QStringLiteral("Axis %1").arg(int(axis) + 1);
    }
}

static QString sdlGamepadButtonName(SDL_GamepadButton button)
{
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return QStringLiteral("South Button");
    case SDL_GAMEPAD_BUTTON_EAST:
        return QStringLiteral("East Button");
    case SDL_GAMEPAD_BUTTON_WEST:
        return QStringLiteral("West Button");
    case SDL_GAMEPAD_BUTTON_NORTH:
        return QStringLiteral("North Button");
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return QStringLiteral("Left Shoulder");
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        return QStringLiteral("Right Shoulder");
    case SDL_GAMEPAD_BUTTON_START:
        return QStringLiteral("Start Button");
    case SDL_GAMEPAD_BUTTON_BACK:
        return QStringLiteral("Back Button");
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        return QStringLiteral("Left Stick Button");
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        return QStringLiteral("Right Stick Button");
    case SDL_GAMEPAD_BUTTON_GUIDE:
        return QStringLiteral("Guide Button");
    case SDL_GAMEPAD_BUTTON_MISC1:
        return QStringLiteral("Misc Button 1");
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1:
        return QStringLiteral("Right Paddle 1");
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1:
        return QStringLiteral("Left Paddle 1");
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2:
        return QStringLiteral("Right Paddle 2");
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2:
        return QStringLiteral("Left Paddle 2");
    case SDL_GAMEPAD_BUTTON_TOUCHPAD:
        return QStringLiteral("Touchpad Button");
    case SDL_GAMEPAD_BUTTON_MISC2:
        return QStringLiteral("Misc Button 2");
    case SDL_GAMEPAD_BUTTON_MISC3:
        return QStringLiteral("Misc Button 3");
    case SDL_GAMEPAD_BUTTON_MISC4:
        return QStringLiteral("Misc Button 4");
    case SDL_GAMEPAD_BUTTON_MISC5:
        return QStringLiteral("Misc Button 5");
    case SDL_GAMEPAD_BUTTON_MISC6:
        return QStringLiteral("Misc Button 6");
    default:
        return QStringLiteral("Button %1").arg(int(button) + 1);
    }
}

static bool unixQtInputWidgetActive(UnixQtInputWidgetKind kind, int state)
{
    if (kind == UnixQtInputWidgetKind::Button) {
        return state != 0;
    }
    if (kind == UnixQtInputWidgetKind::GamepadDpadX || kind == UnixQtInputWidgetKind::GamepadDpadY ||
        kind == UnixQtInputWidgetKind::JoystickHatX || kind == UnixQtInputWidgetKind::JoystickHatY) {
        return state != 0;
    }
    return qAbs(state) > 8000;
}
#endif

class UnixQtInputMapDialog final : public QDialog {
public:
    explicit UnixQtInputMapDialog(int port, const QString &context, const QString &customConfig, QWidget *parent = nullptr, bool captureSingle = false)
        : QDialog(parent),
          port(port),
          contextText(context),
          singleCapture(captureSingle),
          customMappings(customConfig.split(QLatin1Char(' '), Qt::SkipEmptyParts))
    {
        setWindowTitle(QStringLiteral("Input Remap"));
        resize(640, 440);
        setMinimumSize(560, 360);

        list = new QTreeWidget;
        list->setRootIsDecorated(false);
        list->setAlternatingRowColors(true);
        list->setSelectionMode(QAbstractItemView::SingleSelection);
        list->setHeaderLabels({
            QStringLiteral("Host widget"),
            QStringLiteral("Device"),
            QStringLiteral("Type"),
            QStringLiteral("State")
        });
        list->header()->setStretchLastSection(false);
        list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        list->header()->setSectionResizeMode(1, QHeaderView::Stretch);
        list->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        list->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

        inputLine = new QLineEdit;
        inputLine->setReadOnly(true);
        inputLine->setEnabled(false);
        mappingLine = new QPlainTextEdit;
        mappingLine->setReadOnly(true);
        mappingLine->setEnabled(false);
        mappingLine->setFixedHeight(48);

        addEvent = new QComboBox;
        populateEventChoices();
        addEventButton = new QPushButton(QStringLiteral("Add Event"));
        autofireButton = new QPushButton(QStringLiteral("Autofire"));
        testButton = new QPushButton(QStringLiteral("Test"));
        remapButton = new QPushButton(QStringLiteral("Remap"));
        deleteButton = new QPushButton(QStringLiteral("Delete"));
        deleteAllButton = new QPushButton(QStringLiteral("Delete all"));
        QPushButton *exitButton = new QPushButton(QStringLiteral("Exit"));

        if (port < 0) {
            disableUnavailable(addEvent, QStringLiteral("Custom game-port mappings are only available from the Game Ports page."));
            disableUnavailable(addEventButton, QStringLiteral("Custom game-port mappings are only available from the Game Ports page."));
            disableUnavailable(autofireButton, QStringLiteral("Custom game-port mappings are only available from the Game Ports page."));
            disableUnavailable(remapButton, QStringLiteral("Custom game-port mappings are only available from the Game Ports page."));
            disableUnavailable(deleteButton, QStringLiteral("Custom game-port mappings are only available from the Game Ports page."));
            disableUnavailable(deleteAllButton, QStringLiteral("Custom game-port mappings are only available from the Game Ports page."));
        }

        QHBoxLayout *addRow = new QHBoxLayout;
        addRow->setContentsMargins(0, 0, 0, 0);
        addRow->addWidget(addEvent, 1);
        addRow->addWidget(addEventButton);
        addRow->addWidget(autofireButton);

        QHBoxLayout *buttonRow = new QHBoxLayout;
        buttonRow->setContentsMargins(0, 0, 0, 0);
        buttonRow->addWidget(testButton);
        buttonRow->addWidget(remapButton);
        buttonRow->addWidget(deleteButton);
        buttonRow->addWidget(deleteAllButton);
        buttonRow->addWidget(exitButton);

        QVBoxLayout *root = new QVBoxLayout(this);
        root->setContentsMargins(6, 6, 6, 6);
        root->setSpacing(5);
        root->addWidget(list, 1);
        root->addWidget(inputLine);
        root->addWidget(mappingLine);
        root->addLayout(addRow);
        root->addLayout(buttonRow);

        timer = new QTimer(this);
        timer->setInterval(30);
        connect(timer, &QTimer::timeout, this, [this]() { pollInput(); });
        connect(testButton, &QPushButton::clicked, this, [this]() { setTesting(!testing); });
        connect(addEventButton, &QPushButton::clicked, this, [this]() { addSelectedMapping(); });
        connect(autofireButton, &QPushButton::clicked, this, [this]() { toggleSelectedMappingAutofire(); });
        connect(remapButton, &QPushButton::clicked, this, [this]() { toggleSequentialRemap(); });
        connect(deleteButton, &QPushButton::clicked, this, [this]() { deleteSelectedMapping(); });
        connect(deleteAllButton, &QPushButton::clicked, this, [this]() {
            remapping = false;
            customMappings.clear();
            changed = true;
            updateMappingLine();
            updateActionState();
        });
        connect(exitButton, &QPushButton::clicked, this, &QDialog::accept);
        connect(list, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *, QTreeWidgetItem *) {
            updateActionState();
        });

        loadDevices();
        populateList();
        updateMappingLine();
        updateActionState();
        if (list->topLevelItemCount() == 0) {
            QTreeWidgetItem *item = new QTreeWidgetItem(list, {
                QStringLiteral("<None>"),
                QStringLiteral("SDL3"),
                QStringLiteral("Input"),
                QString()
            });
            item->setDisabled(true);
            inputLine->setText(QStringLiteral("No SDL3 joystick/gamepad devices detected."));
        } else if (singleCapture) {
            setTesting(true);
            inputLine->setText(contextText);
        }
    }

    ~UnixQtInputMapDialog() override
    {
        closeDevices();
    }

    bool hasChanges() const
    {
        return changed;
    }

    QString customConfig() const
    {
        return customMappings.join(QLatin1Char(' '));
    }

    bool hasCapturedInput() const
    {
        return capturedDeviceIndex >= 0 && capturedWidgetIndex >= 0;
    }

    int capturedDevice() const
    {
        return capturedDeviceIndex;
    }

    int capturedWidget() const
    {
        return capturedWidgetIndex;
    }

    QChar capturedWidgetType() const
    {
        return capturedMappingType;
    }

    int capturedWidgetMappingIndex() const
    {
        return capturedMappingIndex;
    }

    QString capturedDeviceDisplayName() const
    {
        return capturedDeviceName;
    }

    QString capturedDeviceConfigName() const
    {
        return capturedDeviceUniqueName;
    }

    QString capturedWidgetDisplayName() const
    {
        return capturedWidgetName;
    }

private:
    int port = -1;
    QString contextText;
    QTreeWidget *list = nullptr;
    QLineEdit *inputLine = nullptr;
    QPlainTextEdit *mappingLine = nullptr;
    QComboBox *addEvent = nullptr;
    QPushButton *addEventButton = nullptr;
    QPushButton *autofireButton = nullptr;
    QPushButton *remapButton = nullptr;
    QPushButton *deleteButton = nullptr;
    QPushButton *deleteAllButton = nullptr;
    QPushButton *testButton = nullptr;
    QTimer *timer = nullptr;
    bool testing = false;
    bool changed = false;
    bool remapping = false;
    bool remapWaitingForRelease = false;
    bool singleCapture = false;
    int capturedDeviceIndex = -1;
    int capturedWidgetIndex = -1;
    QChar capturedMappingType = QLatin1Char('a');
    int capturedMappingIndex = 0;
    QString capturedDeviceName;
    QString capturedDeviceUniqueName;
    QString capturedWidgetName;
    int remapEventIndex = -1;
    QStringList customMappings;

#ifdef UAE_UNIX_WITH_SDL3
    QVector<UnixQtInputTestDevice> devices;

    void addGamepad(SDL_JoystickID instanceId)
    {
        SDL_Gamepad *gamepad = SDL_OpenGamepad(instanceId);
        if (!gamepad) {
            return;
        }

        UnixQtInputTestDevice device;
        device.instanceId = instanceId;
        device.gamepad = gamepad;
        const char *name = SDL_GetGamepadName(gamepad);
        device.name = QString::fromUtf8(name && name[0] ? name : "SDL Gamepad");
        char guid[64];
        SDL_GUIDToString(SDL_GetJoystickGUIDForID(instanceId), guid, sizeof guid);
        device.uniqueName = QStringLiteral("unix.gamepad.%1.%2").arg(QString::fromLatin1(guid)).arg(devices.size());

        const SDL_GamepadAxis axes[] = {
            SDL_GAMEPAD_AXIS_LEFTX,
            SDL_GAMEPAD_AXIS_LEFTY,
            SDL_GAMEPAD_AXIS_RIGHTX,
            SDL_GAMEPAD_AXIS_RIGHTY,
            SDL_GAMEPAD_AXIS_LEFT_TRIGGER,
            SDL_GAMEPAD_AXIS_RIGHT_TRIGGER
        };
        int axisSlot = 0;
        for (SDL_GamepadAxis axis : axes) {
            if (SDL_GamepadHasAxis(gamepad, axis)) {
                UnixQtInputTestWidget widget;
                widget.name = sdlGamepadAxisName(axis);
                widget.kind = UnixQtInputWidgetKind::Axis;
                widget.code = int(axis);
                widget.mappingIndex = axisSlot++;
                widget.mappingType = QLatin1Char('a');
                device.widgets.append(widget);
            }
        }

        UnixQtInputTestWidget dpadX;
        dpadX.name = QStringLiteral("DPad X Axis");
        dpadX.kind = UnixQtInputWidgetKind::GamepadDpadX;
        dpadX.mappingIndex = axisSlot++;
        dpadX.mappingType = QLatin1Char('a');
        device.widgets.append(dpadX);
        UnixQtInputTestWidget dpadY;
        dpadY.name = QStringLiteral("DPad Y Axis");
        dpadY.kind = UnixQtInputWidgetKind::GamepadDpadY;
        dpadY.mappingIndex = axisSlot++;
        dpadY.mappingType = QLatin1Char('a');
        device.widgets.append(dpadY);

        const SDL_GamepadButton buttons[] = {
            SDL_GAMEPAD_BUTTON_SOUTH,
            SDL_GAMEPAD_BUTTON_EAST,
            SDL_GAMEPAD_BUTTON_WEST,
            SDL_GAMEPAD_BUTTON_NORTH,
            SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
            SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
            SDL_GAMEPAD_BUTTON_START,
            SDL_GAMEPAD_BUTTON_BACK,
            SDL_GAMEPAD_BUTTON_LEFT_STICK,
            SDL_GAMEPAD_BUTTON_RIGHT_STICK,
            SDL_GAMEPAD_BUTTON_GUIDE,
            SDL_GAMEPAD_BUTTON_MISC1,
            SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1,
            SDL_GAMEPAD_BUTTON_LEFT_PADDLE1,
            SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2,
            SDL_GAMEPAD_BUTTON_LEFT_PADDLE2,
            SDL_GAMEPAD_BUTTON_TOUCHPAD,
            SDL_GAMEPAD_BUTTON_MISC2,
            SDL_GAMEPAD_BUTTON_MISC3,
            SDL_GAMEPAD_BUTTON_MISC4,
            SDL_GAMEPAD_BUTTON_MISC5,
            SDL_GAMEPAD_BUTTON_MISC6
        };
        int buttonSlot = 0;
        for (SDL_GamepadButton button : buttons) {
            if (SDL_GamepadHasButton(gamepad, button)) {
                UnixQtInputTestWidget widget;
                widget.name = sdlGamepadButtonName(button);
                widget.kind = UnixQtInputWidgetKind::Button;
                widget.code = int(button);
                widget.mappingIndex = buttonSlot++;
                widget.mappingType = QLatin1Char('b');
                device.widgets.append(widget);
            }
        }

        devices.append(device);
    }

    void addJoystick(SDL_JoystickID instanceId)
    {
        if (SDL_IsGamepad(instanceId)) {
            return;
        }
        SDL_Joystick *joystick = SDL_OpenJoystick(instanceId);
        if (!joystick) {
            return;
        }

        UnixQtInputTestDevice device;
        device.instanceId = instanceId;
        device.joystick = joystick;
        const char *name = SDL_GetJoystickName(joystick);
        device.name = QString::fromUtf8(name && name[0] ? name : "SDL Joystick");
        char guid[64];
        SDL_GUIDToString(SDL_GetJoystickGUIDForID(instanceId), guid, sizeof guid);
        device.uniqueName = QStringLiteral("unix.joystick.%1.%2").arg(QString::fromLatin1(guid)).arg(devices.size());

        const int axisCount = qMax(0, SDL_GetNumJoystickAxes(joystick));
        for (int i = 0; i < axisCount; i++) {
            UnixQtInputTestWidget widget;
            widget.name = QStringLiteral("Axis %1").arg(i + 1);
            widget.kind = UnixQtInputWidgetKind::Axis;
            widget.code = i;
            widget.mappingIndex = i;
            widget.mappingType = QLatin1Char('a');
            device.widgets.append(widget);
        }

        const int hatCount = qMax(0, SDL_GetNumJoystickHats(joystick));
        int axisSlot = axisCount;
        for (int i = 0; i < hatCount; i++) {
            UnixQtInputTestWidget hatX;
            hatX.name = QStringLiteral("Hat %1 X Axis").arg(i + 1);
            hatX.kind = UnixQtInputWidgetKind::JoystickHatX;
            hatX.code = i;
            hatX.mappingIndex = axisSlot++;
            hatX.mappingType = QLatin1Char('a');
            device.widgets.append(hatX);
            UnixQtInputTestWidget hatY;
            hatY.name = QStringLiteral("Hat %1 Y Axis").arg(i + 1);
            hatY.kind = UnixQtInputWidgetKind::JoystickHatY;
            hatY.code = i;
            hatY.mappingIndex = axisSlot++;
            hatY.mappingType = QLatin1Char('a');
            device.widgets.append(hatY);
        }

        const int buttonCount = qMax(0, SDL_GetNumJoystickButtons(joystick));
        for (int i = 0; i < buttonCount; i++) {
            UnixQtInputTestWidget widget;
            widget.name = QStringLiteral("Button %1").arg(i + 1);
            widget.kind = UnixQtInputWidgetKind::Button;
            widget.code = i;
            widget.mappingIndex = i;
            widget.mappingType = QLatin1Char('b');
            device.widgets.append(widget);
        }

        devices.append(device);
    }

    void loadDevices()
    {
        SDL_SetMainReady();
        if (!SDL_InitSubSystem(SDL_INIT_EVENTS | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD)) {
            inputLine->setText(QStringLiteral("SDL3 input backend is unavailable: %1").arg(QString::fromUtf8(SDL_GetError())));
            return;
        }

        int count = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&count);
        if (ids) {
            for (int i = 0; i < count; i++) {
                addGamepad(ids[i]);
            }
            SDL_free(ids);
        }

        count = 0;
        ids = SDL_GetJoysticks(&count);
        if (ids) {
            for (int i = 0; i < count; i++) {
                addJoystick(ids[i]);
            }
            SDL_free(ids);
        }
    }

    void closeDevices()
    {
        for (UnixQtInputTestDevice &device : devices) {
            if (device.gamepad) {
                SDL_CloseGamepad(device.gamepad);
                device.gamepad = nullptr;
            }
            if (device.joystick) {
                SDL_CloseJoystick(device.joystick);
                device.joystick = nullptr;
            }
        }
    }

    int readWidgetState(const UnixQtInputTestDevice &device, const UnixQtInputTestWidget &widget) const
    {
        if (device.gamepad) {
            switch (widget.kind) {
            case UnixQtInputWidgetKind::Axis:
                return SDL_GetGamepadAxis(device.gamepad, SDL_GamepadAxis(widget.code));
            case UnixQtInputWidgetKind::Button:
                return SDL_GetGamepadButton(device.gamepad, SDL_GamepadButton(widget.code)) ? 1 : 0;
            case UnixQtInputWidgetKind::GamepadDpadX:
                return (SDL_GetGamepadButton(device.gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ? 1 : 0) -
                    (SDL_GetGamepadButton(device.gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) ? 1 : 0);
            case UnixQtInputWidgetKind::GamepadDpadY:
                return (SDL_GetGamepadButton(device.gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) ? 1 : 0) -
                    (SDL_GetGamepadButton(device.gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP) ? 1 : 0);
            case UnixQtInputWidgetKind::JoystickHatX:
            case UnixQtInputWidgetKind::JoystickHatY:
                return 0;
            }
        }
        if (device.joystick) {
            switch (widget.kind) {
            case UnixQtInputWidgetKind::Axis:
                return SDL_GetJoystickAxis(device.joystick, widget.code);
            case UnixQtInputWidgetKind::Button:
                return SDL_GetJoystickButton(device.joystick, widget.code) ? 1 : 0;
            case UnixQtInputWidgetKind::JoystickHatX:
            {
                const Uint8 hat = SDL_GetJoystickHat(device.joystick, widget.code);
                return ((hat & SDL_HAT_RIGHT) ? 1 : 0) - ((hat & SDL_HAT_LEFT) ? 1 : 0);
            }
            case UnixQtInputWidgetKind::JoystickHatY:
            {
                const Uint8 hat = SDL_GetJoystickHat(device.joystick, widget.code);
                return ((hat & SDL_HAT_DOWN) ? 1 : 0) - ((hat & SDL_HAT_UP) ? 1 : 0);
            }
            case UnixQtInputWidgetKind::GamepadDpadX:
            case UnixQtInputWidgetKind::GamepadDpadY:
                return 0;
            }
        }
        return 0;
    }
#else
    void loadDevices()
    {
        inputLine->setText(QStringLiteral("SDL3 input backend is not compiled into this build."));
    }

    void closeDevices()
    {
    }
#endif

    void populateList()
    {
#ifdef UAE_UNIX_WITH_SDL3
        for (int deviceIndex = 0; deviceIndex < devices.size(); deviceIndex++) {
            UnixQtInputTestDevice &device = devices[deviceIndex];
            for (int widgetIndex = 0; widgetIndex < device.widgets.size(); widgetIndex++) {
                UnixQtInputTestWidget &widget = device.widgets[widgetIndex];
                const QString type = widget.kind == UnixQtInputWidgetKind::Button ? QStringLiteral("Button") : QStringLiteral("Axis");
                widget.item = new QTreeWidgetItem(list, {
                    widget.name,
                    device.name,
                    type,
                    QStringLiteral("0")
                });
                widget.item->setData(0, Qt::UserRole, deviceIndex);
                widget.item->setData(0, Qt::UserRole + 1, widgetIndex);
            }
        }
#endif
    }

    QString portEventPrefix() const
    {
        switch (port) {
        case 0:
            return QStringLiteral("JOY1");
        case 1:
            return QStringLiteral("JOY2");
        case 2:
            return QStringLiteral("PAR_JOY1");
        case 3:
            return QStringLiteral("PAR_JOY2");
        default:
            return QString();
        }
    }

    void populateEventChoices()
    {
        const QString prefix = portEventPrefix();
        if (prefix.isEmpty()) {
            addEvent->addItem(QStringLiteral("<None>"));
            return;
        }
        addEvent->addItem(QStringLiteral("Horizontal"), prefix + QStringLiteral("_HORIZ"));
        addEvent->addItem(QStringLiteral("Vertical"), prefix + QStringLiteral("_VERT"));
        addEvent->addItem(QStringLiteral("Up"), prefix + QStringLiteral("_UP"));
        addEvent->addItem(QStringLiteral("Down"), prefix + QStringLiteral("_DOWN"));
        addEvent->addItem(QStringLiteral("Left"), prefix + QStringLiteral("_LEFT"));
        addEvent->addItem(QStringLiteral("Right"), prefix + QStringLiteral("_RIGHT"));
        addEvent->addItem(QStringLiteral("Fire"), prefix + QStringLiteral("_FIRE_BUTTON"));
        if (port < 2) {
            addEvent->addItem(QStringLiteral("2nd Button"), prefix + QStringLiteral("_2ND_BUTTON"));
            addEvent->addItem(QStringLiteral("3rd Button"), prefix + QStringLiteral("_3RD_BUTTON"));
        }
    }

    void updateMappingLine()
    {
        QStringList lines;
        if (!contextText.isEmpty()) {
            lines.append(contextText.split(QLatin1Char('\n')));
        }
        if (!customMappings.isEmpty()) {
            lines.append(customMappings);
        }
        mappingLine->setPlainText(lines.join(QLatin1Char('\n')));
    }

    void updateActionState()
    {
        if (port < 0) {
            return;
        }
        const bool hasSelection = list->currentItem() && !list->currentItem()->isDisabled();
        const bool canEdit = hasSelection && addEvent->count() > 0 && !remapping;
        addEvent->setEnabled(canEdit);
        addEventButton->setEnabled(canEdit);
        if (autofireButton) {
            autofireButton->setEnabled(canEdit);
#ifdef UAE_UNIX_WITH_SDL3
            const int flags = selectedMappingFlags();
            if ((flags & (InputFlagInvertToggle | InputFlagAutofire)) == (InputFlagInvertToggle | InputFlagAutofire)) {
                autofireButton->setText(QStringLiteral("Autofire: Toggle"));
            } else if (flags & InputFlagAutofire) {
                autofireButton->setText(QStringLiteral("Autofire: On"));
            } else {
                autofireButton->setText(QStringLiteral("Autofire"));
            }
#else
            autofireButton->setText(QStringLiteral("Autofire"));
#endif
        }
        if (deleteButton) {
            deleteButton->setEnabled(canEdit && !customMappings.isEmpty());
        }
        if (remapButton) {
            remapButton->setEnabled(hasSelection && addEvent->count() > 0);
            remapButton->setText(remapping ? QStringLiteral("Stop") : QStringLiteral("Remap"));
        }
        deleteAllButton->setEnabled(!remapping && !customMappings.isEmpty());
    }

#ifdef UAE_UNIX_WITH_SDL3
    int selectedDeviceIndex() const
    {
        const int deviceIndex = list->currentItem()->data(0, Qt::UserRole).toInt();
        return deviceIndex >= 0 && deviceIndex < devices.size() ? deviceIndex : -1;
    }

    int selectedWidgetIndex(int deviceIndex) const
    {
        const int widgetIndex = list->currentItem()->data(0, Qt::UserRole + 1).toInt();
        return deviceIndex >= 0 && deviceIndex < devices.size() && widgetIndex >= 0 && widgetIndex < devices[deviceIndex].widgets.size()
            ? widgetIndex
            : -1;
    }

    QString mappingPrefix(int deviceIndex, const UnixQtInputTestWidget &widget) const
    {
        return QStringLiteral("j.%1.%2.%3")
            .arg(deviceIndex)
            .arg(widget.mappingType)
            .arg(widget.mappingIndex);
    }

    QString mappingFor(int deviceIndex, const UnixQtInputTestWidget &widget, const QString &event, int flags = 0) const
    {
        return QStringLiteral("%1.%2=%3").arg(mappingPrefix(deviceIndex, widget)).arg(flags).arg(event);
    }

    static QString mappingLeft(const QString &mapping)
    {
        const int equals = mapping.indexOf(QLatin1Char('='));
        return equals >= 0 ? mapping.left(equals) : mapping;
    }

    static QString mappingRight(const QString &mapping)
    {
        const int equals = mapping.indexOf(QLatin1Char('='));
        return equals >= 0 ? mapping.mid(equals + 1) : QString();
    }

    static int mappingFlags(const QString &left, const QString &prefix)
    {
        if (!left.startsWith(prefix + QLatin1Char('.'))) {
            return 0;
        }
        QString suffix = left.mid(prefix.size() + 1);
        const int dot = suffix.indexOf(QLatin1Char('.'));
        if (dot >= 0) {
            suffix = suffix.left(dot);
        }
        bool ok = false;
        const int flags = suffix.toInt(&ok);
        return ok ? flags : 0;
    }

    static int toggledAutofireFlags(int flags)
    {
        if ((flags & (InputFlagInvertToggle | InputFlagAutofire)) == (InputFlagInvertToggle | InputFlagAutofire)) {
            flags &= ~(InputFlagInvertToggle | InputFlagAutofire);
        } else if (flags & InputFlagAutofire) {
            flags |= InputFlagInvertToggle;
            flags &= ~InputFlagToggle;
        } else if (!(flags & (InputFlagInvertToggle | InputFlagAutofire))) {
            flags |= InputFlagAutofire;
        } else {
            flags &= ~(InputFlagInvertToggle | InputFlagAutofire);
        }
        return flags;
    }

    int removeMappings(const QString &prefix, const QString &event)
    {
        int removed = 0;
        QStringList kept;
        for (const QString &mapping : customMappings) {
            const QString left = mappingLeft(mapping);
            const QString right = mappingRight(mapping);
            if (left.startsWith(prefix + QLatin1Char('.')) && (event.isEmpty() || right == event)) {
                removed++;
            } else {
                kept.append(mapping);
            }
        }
        customMappings = kept;
        return removed;
    }

    int selectedMappingFlags() const
    {
        if (port < 0 || !list->currentItem()) {
            return 0;
        }
        const QString event = addEvent->currentData().toString();
        if (event.isEmpty()) {
            return 0;
        }
        const int deviceIndex = selectedDeviceIndex();
        const int widgetIndex = selectedWidgetIndex(deviceIndex);
        if (deviceIndex < 0 || widgetIndex < 0) {
            return 0;
        }
        const QString prefix = mappingPrefix(deviceIndex, devices[deviceIndex].widgets[widgetIndex]);
        for (const QString &mapping : customMappings) {
            if (mappingRight(mapping) == event) {
                const QString left = mappingLeft(mapping);
                if (left.startsWith(prefix + QLatin1Char('.'))) {
                    return mappingFlags(left, prefix);
                }
            }
        }
        return 0;
    }

    void toggleSelectedMappingAutofireSdl()
    {
        if (port < 0 || !list->currentItem() || remapping) {
            return;
        }
        const QString event = addEvent->currentData().toString();
        if (event.isEmpty()) {
            return;
        }
        const int deviceIndex = selectedDeviceIndex();
        const int widgetIndex = selectedWidgetIndex(deviceIndex);
        if (deviceIndex < 0 || widgetIndex < 0) {
            return;
        }
        const UnixQtInputTestWidget &widget = devices[deviceIndex].widgets[widgetIndex];
        const QString prefix = mappingPrefix(deviceIndex, widget);
        const int flags = toggledAutofireFlags(selectedMappingFlags());
        removeMappings(prefix, event);
        customMappings.append(mappingFor(deviceIndex, widget, event, flags));
        changed = true;
        updateMappingLine();
        updateActionState();
    }

    int nextRemapEventIndex(int start) const
    {
        for (int i = qMax(0, start); i < addEvent->count(); i++) {
            if (!addEvent->itemData(i).toString().isEmpty()) {
                return i;
            }
        }
        return -1;
    }

    void setRemapPrompt()
    {
        if (remapping && remapEventIndex >= 0) {
            inputLine->setText(QStringLiteral("Press input for %1.").arg(addEvent->itemText(remapEventIndex)));
        }
    }

    void finishSequentialRemap(const QString &message)
    {
        remapping = false;
        remapWaitingForRelease = false;
        remapEventIndex = -1;
        if (testing) {
            setTesting(false);
        }
        inputLine->setText(message);
        updateActionState();
    }

    bool appendMappingFor(int deviceIndex, int widgetIndex, const QString &event)
    {
        if (deviceIndex < 0 || deviceIndex >= devices.size()) {
            return false;
        }
        UnixQtInputTestDevice &device = devices[deviceIndex];
        if (widgetIndex < 0 || widgetIndex >= device.widgets.size()) {
            return false;
        }
        const UnixQtInputTestWidget &widget = device.widgets[widgetIndex];
        removeMappings(mappingPrefix(deviceIndex, widget), event);
        customMappings.append(mappingFor(deviceIndex, widget, event));
        changed = true;
        updateMappingLine();
        updateActionState();
        return true;
    }
#endif

    void toggleSelectedMappingAutofire()
    {
#ifdef UAE_UNIX_WITH_SDL3
        toggleSelectedMappingAutofireSdl();
#endif
    }

    void addSelectedMapping()
    {
#ifdef UAE_UNIX_WITH_SDL3
        if (port < 0 || !list->currentItem() || remapping) {
            return;
        }
        const QString event = addEvent->currentData().toString();
        if (event.isEmpty()) {
            return;
        }
        const int deviceIndex = selectedDeviceIndex();
        appendMappingFor(deviceIndex, selectedWidgetIndex(deviceIndex), event);
#endif
    }

    void deleteSelectedMapping()
    {
#ifdef UAE_UNIX_WITH_SDL3
        if (port < 0 || !list->currentItem() || remapping) {
            return;
        }
        const int deviceIndex = selectedDeviceIndex();
        const int widgetIndex = selectedWidgetIndex(deviceIndex);
        if (deviceIndex < 0 || widgetIndex < 0) {
            return;
        }
        const QString event = addEvent->currentData().toString();
        const QString prefix = mappingPrefix(deviceIndex, devices[deviceIndex].widgets[widgetIndex]);
        int removed = event.isEmpty() ? 0 : removeMappings(prefix, event);
        if (!removed) {
            removed = removeMappings(prefix, QString());
        }
        if (removed) {
            changed = true;
            updateMappingLine();
            updateActionState();
        }
#endif
    }

    void toggleSequentialRemap()
    {
#ifdef UAE_UNIX_WITH_SDL3
        if (port < 0 || !list->currentItem()) {
            return;
        }
        if (remapping) {
            finishSequentialRemap(QStringLiteral("Remap canceled."));
            return;
        }
        remapEventIndex = nextRemapEventIndex(0);
        if (remapEventIndex < 0) {
            return;
        }
        customMappings.clear();
        changed = true;
        remapping = true;
        remapWaitingForRelease = false;
        addEvent->setCurrentIndex(remapEventIndex);
        updateMappingLine();
        updateActionState();
        if (!testing) {
            setTesting(true);
        }
        setRemapPrompt();
#endif
    }

    void advanceSequentialRemap()
    {
#ifdef UAE_UNIX_WITH_SDL3
        remapEventIndex = nextRemapEventIndex(remapEventIndex + 1);
        if (remapEventIndex < 0) {
            finishSequentialRemap(QStringLiteral("Remap complete."));
            return;
        }
        addEvent->setCurrentIndex(remapEventIndex);
        setRemapPrompt();
        updateActionState();
#endif
    }

    void setTesting(bool enabled)
    {
        testing = enabled;
        if (!testing && remapping) {
            remapping = false;
            remapWaitingForRelease = false;
            remapEventIndex = -1;
        }
        testButton->setText(testing ? QStringLiteral("Stop") : QStringLiteral("Test"));
        if (testing) {
            timer->start();
        } else {
            timer->stop();
            clearHighlights();
        }
        updateActionState();
    }

    void clearHighlights()
    {
        for (int i = 0; i < list->topLevelItemCount(); i++) {
            QTreeWidgetItem *item = list->topLevelItem(i);
            for (int column = 0; column < list->columnCount(); column++) {
                item->setBackground(column, QBrush());
            }
        }
    }

    void pollInput()
    {
#ifdef UAE_UNIX_WITH_SDL3
        SDL_UpdateGamepads();
        SDL_UpdateJoysticks();
        bool foundActive = false;
        QTreeWidgetItem *activeItem = nullptr;
        const QColor activeColor = palette().color(QPalette::Highlight).lighter(170);

        for (UnixQtInputTestDevice &device : devices) {
            for (UnixQtInputTestWidget &widget : device.widgets) {
                const int state = readWidgetState(device, widget);
                if (widget.item) {
                    widget.item->setText(3, QString::number(state));
                    const bool active = unixQtInputWidgetActive(widget.kind, state);
                    for (int column = 0; column < list->columnCount(); column++) {
                        widget.item->setBackground(column, active ? QBrush(activeColor) : QBrush());
                    }
                    if (active && !foundActive) {
                        foundActive = true;
                        activeItem = widget.item;
                        inputLine->setText(QStringLiteral("%1, %2").arg(widget.name, device.name));
                    }
                }
                widget.state = state;
            }
        }
        if (activeItem) {
            list->setCurrentItem(activeItem);
            list->scrollToItem(activeItem, QAbstractItemView::PositionAtCenter);
            const int deviceIndex = activeItem->data(0, Qt::UserRole).toInt();
            const int widgetIndex = activeItem->data(0, Qt::UserRole + 1).toInt();
            if (singleCapture && deviceIndex >= 0 && deviceIndex < devices.size()
                && widgetIndex >= 0 && widgetIndex < devices[deviceIndex].widgets.size()) {
                const UnixQtInputTestWidget &widget = devices[deviceIndex].widgets[widgetIndex];
                capturedDeviceIndex = deviceIndex;
                capturedWidgetIndex = widgetIndex;
                capturedMappingType = widget.mappingType;
                capturedMappingIndex = widget.mappingIndex;
                capturedDeviceName = devices[deviceIndex].name;
                capturedDeviceUniqueName = devices[deviceIndex].uniqueName;
                capturedWidgetName = widget.name;
                changed = true;
                accept();
                return;
            }
            if (remapping && !remapWaitingForRelease && remapEventIndex >= 0) {
                const QString event = addEvent->itemData(remapEventIndex).toString();
                if (!event.isEmpty() && appendMappingFor(deviceIndex, widgetIndex, event)) {
                    remapWaitingForRelease = true;
                    advanceSequentialRemap();
                }
            }
        } else if (remapping) {
            remapWaitingForRelease = false;
            setRemapPrompt();
        }
#endif
    }
};

static QGroupBox *groupBox(const QString &title, QLayout *layout)
{
    QGroupBox *box = new QGroupBox(title);
    QFont titleFont = box->font();
    titleFont.setPixelSize(13);
    box->setFont(titleFont);
    layout->setContentsMargins(6, 8, 6, 6);
    if (QGridLayout *grid = dynamic_cast<QGridLayout*>(layout)) {
        grid->setHorizontalSpacing(qMax(grid->horizontalSpacing(), 8));
        grid->setVerticalSpacing(qMax(grid->verticalSpacing(), 5));
    } else {
        layout->setSpacing(qMax(layout->spacing(), 5));
    }
    box->setLayout(layout);
    return box;
}

static void setPathComboText(QComboBox *field, const QString &path)
{
    if (!field) {
        return;
    }
    if (!path.isEmpty() && field->findText(path) < 0) {
        field->insertItem(0, path);
    }
    field->setCurrentText(path);
}

static QString envString(const char *name)
{
    return winUaeQtEnvString(name);
}

static QString unixDefaultDataPath()
{
    return winUaeQtDefaultDataPath();
}

static QString unixDefaultDataSubPath(const QString &name)
{
    return winUaeQtDefaultDataSubPath(name);
}

static QString expandUnixPath(QString path)
{
    return winUaeQtExpandUnixPath(path);
}

static QString expandedPathText(const QString &path)
{
    return winUaeQtExpandedPathText(path);
}

static QString fileDialogInitialPath(const QString &path)
{
    return winUaeQtFileDialogInitialPath(path);
}

static QString fileDialogInitialDirectory(const QString &path)
{
    return winUaeQtFileDialogInitialDirectory(path);
}

static QString fileDialogInitialSavePath(const QString &path, const QString &fallbackName)
{
    return winUaeQtFileDialogInitialSavePath(path, fallbackName);
}

static void addBrowse(QComboBox *field, QWidget *parent, const QString &caption, const QString &filter)
{
    const QString path = QFileDialog::getOpenFileName(parent, caption, fileDialogInitialPath(field->currentText()), filter);
    if (!path.isEmpty()) {
        setPathComboText(field, path);
    }
}

static bool isConfigPath(const QString &path)
{
    return path.endsWith(QStringLiteral(".uae"), Qt::CaseInsensitive);
}

static QString initialConfigPathFromArguments(const QStringList &arguments)
{
    for (int i = 1; i < arguments.size(); i++) {
        const QString arg = arguments[i];
        if ((arg == QStringLiteral("-f")
             || arg == QStringLiteral("-config")
             || arg == QStringLiteral("--config")) && i + 1 < arguments.size()) {
            const QString path = expandedPathText(arguments[i + 1]);
            if (isConfigPath(path)) {
                return path;
            }
            i++;
            continue;
        }
        const QString configPrefix = QStringLiteral("-config=");
        const QString longConfigPrefix = QStringLiteral("--config=");
        if (arg.startsWith(configPrefix) || arg.startsWith(longConfigPrefix)) {
            const int offset = arg.startsWith(configPrefix) ? configPrefix.size() : longConfigPrefix.size();
            const QString path = expandedPathText(arg.mid(offset));
            if (isConfigPath(path)) {
                return path;
            }
            continue;
        }
        const QString path = expandedPathText(arg);
        if (!arg.startsWith(QLatin1Char('-')) && isConfigPath(path)) {
            return path;
        }
    }
    return QString();
}

static int &prepareQtApplicationArguments(int &argc)
{
#if defined(__linux__)
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#endif
    return argc;
}

class WinUaeQtApplication final : public QApplication {
public:
    using ConfigOpenHandler = std::function<void(const QString &)>;

    WinUaeQtApplication(int &argc, char **argv)
        : QApplication(prepareQtApplicationArguments(argc), argv)
    {
    }

    void setConfigOpenHandler(ConfigOpenHandler handler)
    {
        configOpenHandler = std::move(handler);
        if (!pendingConfigPath.isEmpty() && configOpenHandler) {
            const QString path = pendingConfigPath;
            pendingConfigPath.clear();
            configOpenHandler(path);
        }
    }

protected:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::FileOpen) {
            const QFileOpenEvent *openEvent = static_cast<QFileOpenEvent *>(event);
            QString path = openEvent->file();
            if (path.isEmpty() && openEvent->url().isLocalFile()) {
                path = openEvent->url().toLocalFile();
            }
            if (isConfigPath(path)) {
                if (configOpenHandler) {
                    configOpenHandler(path);
                } else {
                    pendingConfigPath = path;
                }
                return true;
            }
        }
        return QApplication::event(event);
    }

private:
    QString pendingConfigPath;
    ConfigOpenHandler configOpenHandler;
};

class WinUaeQtDialog final : public QDialog {
public:
    explicit WinUaeQtDialog(
        QWidget *parent = nullptr,
        const QString &initialConfigPath = QString(),
        const WinUaeQtHardwareInfoProvider &hardwareInfoProvider = WinUaeQtHardwareInfoProvider())
        : QDialog(parent),
          hardwareProvider(hardwareInfoProvider),
          boardCatalog(hardwareInfoProvider.boardCatalog
              ? hardwareInfoProvider.boardCatalog(hardwareInfoProvider.context)
              : WinUaeQtBoardCatalog())
    {
        setWindowTitle(QStringLiteral("WinUAE Properties"));
        setWindowIcon(resourceIcon(QStringLiteral("winuae.ico")));
        resize(880, 640);
        setMinimumSize(820, 600);

        navigation = new QTreeWidget;
        navigation->setHeaderHidden(true);
        navigation->setRootIsDecorated(true);
        navigation->setIndentation(12);
        navigation->setIconSize(QSize(16, 16));
        navigation->setFixedWidth(166);

        pageStack = new QStackedWidget;
        pageStack->setObjectName(QStringLiteral("pageStack"));

        QTreeWidgetItem *settingsFolder = addFolder(QStringLiteral("Settings"));
        addPage(QStringLiteral("About"), QStringLiteral("amigainfo.ico"), makeAboutPage(), settingsFolder);
        addPage(QStringLiteral("Paths"), QStringLiteral("paths.ico"), makePathsPage(), settingsFolder);
        QTreeWidgetItem *quickstartPage = addPage(QStringLiteral("Quickstart"), QStringLiteral("quickstart.ico"), makeQuickstartPage(), settingsFolder);
        addPage(QStringLiteral("Configurations"), QStringLiteral("configfile.ico"), makeConfigurationsPage(), settingsFolder);
        addPage(QStringLiteral("Frontend"), QStringLiteral("quickstart.ico"), makeFrontendPage(), settingsFolder);

        QTreeWidgetItem *hardwareFolder = addFolder(QStringLiteral("Hardware"), settingsFolder);
        addPage(QStringLiteral("CPU and FPU"), QStringLiteral("cpu.ico"), makeCpuPage(), hardwareFolder);
        addPage(QStringLiteral("Chipset"), QStringLiteral("chip.ico"), makeChipsetPage(), hardwareFolder);
        addPage(QStringLiteral("Adv. Chipset"), QStringLiteral("chip.ico"), makeAdvancedChipsetPage(), hardwareFolder);
        addPage(QStringLiteral("ROM"), QStringLiteral("chip.ico"), makeRomPage(), hardwareFolder);
        addPage(QStringLiteral("RAM"), QStringLiteral("chip.ico"), makeMemoryPage(), hardwareFolder);
        addPage(QStringLiteral("Floppy drives"), QStringLiteral("35floppy.ico"), makeFloppyPage(), hardwareFolder);
        addPage(QStringLiteral("CD & Hard drives"), QStringLiteral("drive.ico"), makeHardDrivesPage(), hardwareFolder);
        addPage(QStringLiteral("Expansions"), QStringLiteral("expansion.ico"), makeExpansionsPage(), hardwareFolder);
        addPage(QStringLiteral("RTG board"), QStringLiteral("expansion.ico"), makeExpansionPage(), hardwareFolder);
        addPage(QStringLiteral("Hardware info"), QStringLiteral("expansion.ico"), makeHardwareInfoPage(), hardwareFolder);

        QTreeWidgetItem *hostFolder = addFolder(QStringLiteral("Host"), settingsFolder);
        addPage(QStringLiteral("Display"), QStringLiteral("screen.ico"), makeDisplayPage(), hostFolder);
        addPage(QStringLiteral("Sound"), QStringLiteral("sound.ico"), makeSoundPage(), hostFolder);
        addPage(QStringLiteral("Game ports"), QStringLiteral("joystick.ico"), makeGamePortsPage(), hostFolder);
        addPage(QStringLiteral("IO ports"), QStringLiteral("port.ico"), makeIoPortsPage(), hostFolder);
        addPage(QStringLiteral("Input"), QStringLiteral("port.ico"), makeInputPage(), hostFolder);
        addPage(QStringLiteral("Output"), QStringLiteral("avioutput.ico"), makeOutputPage(), hostFolder);
        addPage(QStringLiteral("Filter"), QStringLiteral("screen.ico"), makeFilterPage(), hostFolder);
        addPage(QStringLiteral("Disk Swapper"), QStringLiteral("diskimage.ico"), makeDiskSwapperPage(), hostFolder);
        addPage(QStringLiteral("Miscellaneous"), QStringLiteral("misc.ico"), makeMiscPage(), hostFolder);
        addPage(QStringLiteral("Pri. & Extensions"), QStringLiteral("misc.ico"), makeExtensionsPage(), hostFolder);
        navigation->expandAll();

        connect(navigation, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
            if (!item) {
                return;
            }
            const QVariant pageIndex = item->data(0, Qt::UserRole);
            if (!pageIndex.isValid()) {
                return;
            }
            pageStack->setCurrentIndex(pageIndex.toInt());
            if (item->text(0) == QStringLiteral("Hardware info")) {
                refreshHardwareInfoPage();
            } else if (item->text(0) == QStringLiteral("Frontend")) {
                refreshFrontendPage();
            }
        });

        QFrame *outerFrame = new QFrame;
        outerFrame->setFrameShape(QFrame::Box);
        outerFrame->setObjectName(QStringLiteral("outerFrame"));
        QVBoxLayout *frameLayout = new QVBoxLayout(outerFrame);
        frameLayout->setContentsMargins(4, 4, 4, 4);
        frameLayout->addWidget(pageStack);

        QHBoxLayout *content = new QHBoxLayout;
        content->setContentsMargins(0, 0, 0, 0);
        content->setSpacing(5);
        content->addWidget(navigation);
        content->addWidget(outerFrame, 1);

        runtimeMode = hardwareProvider.pollHostWindowEvents != nullptr;
        QPushButton *reset = new QPushButton(QStringLiteral("Reset"));
        QPushButton *quit = new QPushButton(QStringLiteral("Quit"));
        QPushButton *restart = new QPushButton(QStringLiteral("Restart"));
        QPushButton *errorLog = new QPushButton(QStringLiteral("Error log"));
        QPushButton *start = new QPushButton(runtimeMode ? QStringLiteral("OK") : QStringLiteral("Start"));
        QPushButton *cancel = new QPushButton(QStringLiteral("Cancel"));
        QPushButton *help = new QPushButton(QStringLiteral("Help"));
        restart->setVisible(runtimeMode);
        errorLog->setVisible(false);
        start->setDefault(true);

        connect(reset, &QPushButton::clicked, this, [this]() {
            if (runtimeMode) {
                /* Windows: hard-reset the Amiga with the edited config and
                 * resume (IDC_RESETAMIGA sends IDOK after uae_reset). */
                requestStart(true);
            } else {
                resetDefaults();
            }
        });
        connect(quit, &QPushButton::clicked, this, [this]() {
            result.status = WinUaeQtLauncherStatus::QuitRequested;
            accept();
        });
        connect(restart, &QPushButton::clicked, this, [this]() {
            result.status = WinUaeQtLauncherStatus::RestartRequested;
            accept();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(start, &QPushButton::clicked, this, [this]() { startEmulator(); });
        connect(help, &QPushButton::clicked, this, [this]() { openHelp(); });

        QHBoxLayout *buttons = new QHBoxLayout;
        buttons->setContentsMargins(0, 0, 0, 0);
        buttons->addWidget(reset);
        buttons->addWidget(quit);
        buttons->addWidget(restart);
        buttons->addStretch();
        buttons->addWidget(errorLog);
        buttons->addWidget(start);
        buttons->addWidget(cancel);
        buttons->addWidget(help);

        status = new QLabel;
        status->setObjectName(QStringLiteral("statusLine"));
        status->setFrameShape(QFrame::StyledPanel);
        status->setMinimumHeight(22);

        QVBoxLayout *root = new QVBoxLayout(this);
        root->setContentsMargins(6, 6, 6, 6);
        root->setSpacing(5);
        root->addLayout(content, 1);
        root->addWidget(status);
        root->addLayout(buttons);

        resetDefaults();
        navigation->setCurrentItem(quickstartPage);
        if (!initialConfigPath.isEmpty()) {
            loadConfig(initialConfigPath);
        }
        if (hardwareProvider.pollHostWindowEvents) {
            QTimer *timer = new QTimer(this);
            connect(timer, &QTimer::timeout, this, [this]() {
                hardwareProvider.pollHostWindowEvents(hardwareProvider.context);
            });
            timer->start(100);
        }
    }

    const WinUaeQtLauncherResult &launcherResult() const
    {
        return result;
    }

    void openConfigFile(const QString &path)
    {
        if (path.isEmpty()) {
            return;
        }
        loadConfig(path);
        raise();
        activateWindow();
    }

private:
    WinUaeQtHardwareInfoProvider hardwareProvider;
    WinUaeQtBoardCatalog boardCatalog;
    mutable QStringList hardwareOrderOwnedKeys;
    QTreeWidget *navigation = nullptr;
    QStackedWidget *pageStack = nullptr;
    QLabel *status = nullptr;
    bool runtimeMode = false;

    QComboBox *configName = nullptr;
    QLineEdit *configPath = nullptr;
    QLineEdit *configDescription = nullptr;
    QTreeWidget *configTree = nullptr;
    QLineEdit *configSearch = nullptr;
    QComboBox *configFilter = nullptr;

    QComboBox *quickModel = nullptr;
    QComboBox *quickConfiguration = nullptr;
    QComboBox *quickHostConfiguration = nullptr;
    QCheckBox *quickstartMode = nullptr;
    QPushButton *quickstartSetConfig = nullptr;
    QCheckBox *ntsc = nullptr;
    QSlider *compatibility = nullptr;
    QCheckBox *quickDfEnable[2] = {};
    QComboBox *quickDfType[2] = {};
    QComboBox *quickDfPath[2] = {};
    QCheckBox *quickDfWriteProtect[2] = {};
    bool quickstartUpdating = false;

    QComboBox *romFile = nullptr;
    QComboBox *extendedRomFile = nullptr;
    QComboBox *cartFile = nullptr;
    QLineEdit *flashFile = nullptr;
    QLineEdit *rtcFile = nullptr;
    QCheckBox *mapRom = nullptr;
    QCheckBox *kickShifter = nullptr;
    QComboBox *customRomSelect = nullptr;
    QLineEdit *customRomStart = nullptr;
    QLineEdit *customRomEnd = nullptr;
    QLineEdit *customRomFile = nullptr;
    QComboBox *uaeBoardType = nullptr;
    QVector<WinUaeQtRomBoard> customRomBoards;
    int currentCustomRomBoard = 0;
    bool customRomUpdating = false;

    QComboBox *cpuModel = nullptr;
    QButtonGroup *cpuButtons = nullptr;
    QButtonGroup *fpuButtons = nullptr;
    QCheckBox *cpu24Bit = nullptr;
    QCheckBox *moreCompatible = nullptr;
    QCheckBox *cpuDataCache = nullptr;
    QCheckBox *jit = nullptr;
    QCheckBox *cpuUnimplemented = nullptr;
    QButtonGroup *mmuButtons = nullptr;
    QComboBox *chipset = nullptr;
    QComboBox *chipsetCompatible = nullptr;
    QCheckBox *fpuStrict = nullptr;
    QCheckBox *fpuUnimplemented = nullptr;
    QComboBox *fpuMode = nullptr;
    QButtonGroup *cpuSpeedButtons = nullptr;
    QSlider *cpuSpeed = nullptr;
    QLineEdit *cpuSpeedLabel = nullptr;
    QComboBox *cpuFrequency = nullptr;
    QLineEdit *cpuFrequencyCustom = nullptr;
    QSlider *jitCache = nullptr;
    QLineEdit *jitCacheLabel = nullptr;
    QCheckBox *jitFpu = nullptr;
    QCheckBox *jitConstJump = nullptr;
    QCheckBox *jitHardFlush = nullptr;
    QButtonGroup *jitTrust = nullptr;
    QCheckBox *jitNoFlags = nullptr;
    QCheckBox *jitCatchFault = nullptr;
    QCheckBox *chipsetNtsc = nullptr;
    QCheckBox *chipsetCycleExact = nullptr;
    QCheckBox *chipsetCycleExactMemory = nullptr;
    QCheckBox *immediateBlits = nullptr;
    QCheckBox *waitingBlits = nullptr;
    QComboBox *displayOptimization = nullptr;
    QComboBox *chipsetSyncMode = nullptr;
    QButtonGroup *collisionButtons = nullptr;
    QCheckBox *genlockConnected = nullptr;
    QComboBox *genlockMode = nullptr;
    QComboBox *genlockMix = nullptr;
    QCheckBox *genlockAlpha = nullptr;
    QCheckBox *genlockKeepAspect = nullptr;
    QComboBox *genlockFile = nullptr;
    QPushButton *genlockFileBrowse = nullptr;
    QComboBox *keyboardMode = nullptr;
    QCheckBox *keyboardNkro = nullptr;
    QString genlockImagePath;
    QString genlockVideoPath;
    bool genlockUpdating = false;
    QCheckBox *advancedCompatible = nullptr;
    QButtonGroup *advancedRtcButtons = nullptr;
    QLineEdit *advancedRtcAdjust = nullptr;
    QButtonGroup *advancedCiaTodButtons = nullptr;
    QMap<QString, QCheckBox*> advancedCheckBoxes;
    QCheckBox *advancedIdeA600A1200 = nullptr;
    QCheckBox *advancedIdeA4000 = nullptr;
    QCheckBox *advancedScsiA3000 = nullptr;
    QCheckBox *advancedScsiA4000T = nullptr;
    QCheckBox *advancedCia391078 = nullptr;
    QComboBox *advancedUnmappedAddress = nullptr;
    QComboBox *advancedCiaSync = nullptr;
    QCheckBox *advancedRamsey = nullptr;
    QLineEdit *advancedRamseyRevision = nullptr;
    QCheckBox *advancedFatGary = nullptr;
    QLineEdit *advancedFatGaryRevision = nullptr;
    QComboBox *advancedAgnusModel = nullptr;
    QComboBox *advancedAgnusSize = nullptr;
    QComboBox *advancedDeniseModel = nullptr;

    QComboBox *chipMem = nullptr;
    QComboBox *z2Fast = nullptr;
    QComboBox *slowMem = nullptr;
    QComboBox *z3Fast = nullptr;
    QComboBox *z3ChipMem = nullptr;
    QComboBox *processorSlotMem = nullptr;
    QComboBox *z3Mapping = nullptr;
    QComboBox *rtgMem = nullptr;
    QComboBox *rtgType = nullptr;
    QComboBox *rtgMonitor = nullptr;
    QCheckBox *rtgScale = nullptr;
    QCheckBox *rtgScaleAllow = nullptr;
    QCheckBox *rtgCenter = nullptr;
    QCheckBox *rtgIntegerScale = nullptr;
    QCheckBox *rtgMultithread = nullptr;
    QCheckBox *rtgHardwareSprite = nullptr;
    QCheckBox *rtgHardwareVBlank = nullptr;
    QCheckBox *rtgAutoswitch = nullptr;
    QCheckBox *rtgInitialMonitor = nullptr;
    QComboBox *rtg8Bit = nullptr;
    QComboBox *rtg16Bit = nullptr;
    QComboBox *rtg24Bit = nullptr;
    QComboBox *rtg32Bit = nullptr;
    QComboBox *rtgDisplay = nullptr;
    QComboBox *rtgRefreshRate = nullptr;
    QComboBox *rtgBuffers = nullptr;
    QComboBox *rtgAspectRatio = nullptr;
    QComboBox *expansionRomCategory = nullptr;
    QComboBox *expansionRomBoard = nullptr;
    QComboBox *expansionRomSubtype = nullptr;
    QComboBox *expansionRomSlot = nullptr;
    QComboBox *expansionRomId = nullptr;
    QComboBox *expansionRomFile = nullptr;
    QComboBox *expansionBoardOption = nullptr;
    QComboBox *expansionBoardSelector = nullptr;
    QLineEdit *expansionBoardString = nullptr;
    QCheckBox *expansionRom24BitDma = nullptr;
    QCheckBox *expansionRomEnabled = nullptr;
    QCheckBox *expansionRomAutobootDisabled = nullptr;
    QCheckBox *expansionRomPcmciaInserted = nullptr;
    QCheckBox *expansionBoardOptionCheck = nullptr;
    QPushButton *expansionRomBrowse = nullptr;
    QMap<QString, WinUaeQtExpansionBoardState> expansionBoardStates;
    QString currentExpansionBoardConfigName;
    bool expansionBoardUpdating = false;
    QCheckBox *expansionBsdsocket = nullptr;
    QCheckBox *expansionScsiDevice = nullptr;
    QCheckBox *expansionSana2 = nullptr;
    QComboBox *cpuBoardType = nullptr;
    QComboBox *cpuBoardSubtype = nullptr;
    QComboBox *cpuBoardRom = nullptr;
    QComboBox *cpuBoardMem = nullptr;
    QComboBox *acceleratorOption = nullptr;
    QComboBox *acceleratorSelector = nullptr;
    QCheckBox *acceleratorOptionCheck = nullptr;
    QPushButton *cpuBoardBrowse = nullptr;
    QString cpuBoardSettingsRaw;
    bool cpuBoardUpdating = false;
    QTreeWidget *hardwareBoardList = nullptr;
    QCheckBox *hardwareCustomBoardOrder = nullptr;
    QPushButton *hardwareMoveUp = nullptr;
    QPushButton *hardwareMoveDown = nullptr;

    QCheckBox *dfEnable[4] = {};
    QComboBox *dfType[4] = {};
    QComboBox *dfPath[4] = {};
    QCheckBox *dfWriteProtect[4] = {};
    QSlider *floppySpeed = nullptr;
    QLineEdit *floppySpeedLabel = nullptr;
    QTableWidget *diskSwapperList = nullptr;
    QComboBox *diskSwapperPath = nullptr;
    QPushButton *diskSwapperInsertButton = nullptr;
    QPushButton *diskSwapperRemoveButton = nullptr;
    QPushButton *diskSwapperRemoveAllButton = nullptr;
    QTreeWidget *mountedDrives = nullptr;
    QPushButton *addDirectoryMountButton = nullptr;
    QPushButton *addHardfileMountButton = nullptr;
    QPushButton *addHardDriveMountButton = nullptr;
    QPushButton *addCdMountButton = nullptr;
    QPushButton *addTapeMountButton = nullptr;
    QPushButton *propertiesMountButton = nullptr;
    QPushButton *removeMountButton = nullptr;
    QComboBox *cdSlotNumber = nullptr;
    QComboBox *cdSlotType = nullptr;
    QComboBox *cdSlotPath = nullptr;
    QCheckBox *cdSpeedTurbo = nullptr;
    QCheckBox *hostDriveAutomount = nullptr;
    QCheckBox *hostRemovableDrives = nullptr;
    QCheckBox *hostNetworkDrives = nullptr;
    QCheckBox *hostCdDrives = nullptr;
    QCheckBox *filesysNoFsdb = nullptr;
    QCheckBox *hostNoRecycleBin = nullptr;
    QCheckBox *hostAutomountRemovable = nullptr;
    QCheckBox *filesysLimitSize = nullptr;
    QVector<WinUaeQtCdSlot> cdSlots;
    int currentCdSlot = 0;
    bool cdSlotUpdating = false;

    QLineEdit *windowWidth = nullptr;
    QLineEdit *windowHeight = nullptr;
    QCheckBox *windowResize = nullptr;
    QComboBox *hostDisplay = nullptr;
    QComboBox *fullscreenResolution = nullptr;
    QComboBox *displayRefreshRate = nullptr;
    QComboBox *displayBufferCount = nullptr;
    QComboBox *nativeMode = nullptr;
    QComboBox *nativeVsync = nullptr;
    QComboBox *nativeFrameSlices = nullptr;
    QComboBox *rtgMode = nullptr;
    QComboBox *rtgVsync = nullptr;
    QComboBox *displayResolution = nullptr;
    QComboBox *displayOverscan = nullptr;
    QComboBox *displayAutoResolution = nullptr;
    QSpinBox *displayFrameRate = nullptr;
    QCheckBox *displayCenterHorizontal = nullptr;
    QCheckBox *displayCenterVertical = nullptr;
    QCheckBox *displayFlickerFixer = nullptr;
    QCheckBox *displayLoresSmoothed = nullptr;
    QCheckBox *displayBlackerThanBlack = nullptr;
    QCheckBox *displayMonochrome = nullptr;
    QCheckBox *displayAutoResolutionVga = nullptr;
    QCheckBox *displayResyncBlank = nullptr;
    QCheckBox *displayKeepAspect = nullptr;
    QButtonGroup *displayLineModeButtons = nullptr;
    QButtonGroup *displayInterlacedLineModeButtons = nullptr;
    QComboBox *filterTarget = nullptr;
    QCheckBox *filterEnable = nullptr;
    QComboBox *filterMode = nullptr;
    QComboBox *filterModeH = nullptr;
    QComboBox *filterModeV = nullptr;
    QComboBox *filterAutoscale = nullptr;
    QComboBox *filterIntegerLimit = nullptr;
    QDoubleSpinBox *filterHorizZoomMult = nullptr;
    QDoubleSpinBox *filterVertZoomMult = nullptr;
    QDoubleSpinBox *filterHorizZoom = nullptr;
    QDoubleSpinBox *filterVertZoom = nullptr;
    QDoubleSpinBox *filterHorizOffset = nullptr;
    QDoubleSpinBox *filterVertOffset = nullptr;
    QCheckBox *filterKeepAutoscaleAspect = nullptr;
    QCheckBox *filterKeepAspect = nullptr;
    QComboBox *filterAspect = nullptr;
    QComboBox *filterPresetName = nullptr;
    QCheckBox *filterNtscPixels = nullptr;
    QCheckBox *filterBilinear = nullptr;
    QSpinBox *filterScanlines = nullptr;
    QSpinBox *filterScanlineLevel = nullptr;
    QSpinBox *filterScanlineOffset = nullptr;
    QSpinBox *filterLuminance = nullptr;
    QSpinBox *filterContrast = nullptr;
    QSpinBox *filterSaturation = nullptr;
    QSpinBox *filterGamma = nullptr;
    QSpinBox *filterBlur = nullptr;
    QSpinBox *filterNoise = nullptr;
    WinUaeQtFilterState filterStates[3];
    int currentFilterTarget = 0;
    bool filterUpdating = false;
    QLineEdit *outputFile = nullptr;
    QCheckBox *outputAudio = nullptr;
    QCheckBox *outputVideo = nullptr;
    QCheckBox *outputEnabled = nullptr;
    QComboBox *outputAudioCodec = nullptr;
    QComboBox *outputVideoCodec = nullptr;
    QCheckBox *outputFrameLimiter = nullptr;
    QCheckBox *outputOriginalSize = nullptr;
    QCheckBox *outputNoSound = nullptr;
    QCheckBox *outputNoSoundSync = nullptr;
    QCheckBox *screenshotOriginalSize = nullptr;
    QCheckBox *screenshotPaletted = nullptr;
    QCheckBox *screenshotClip = nullptr;
    QCheckBox *screenshotAuto = nullptr;
    QCheckBox *stateReplayAutoplay = nullptr;
    QComboBox *stateReplayRate = nullptr;
    QComboBox *stateReplayBuffers = nullptr;
    QButtonGroup *soundOutputButtons = nullptr;
    QComboBox *soundDevice = nullptr;
    QCheckBox *soundAutomatic = nullptr;
    QSlider *soundMasterVolume = nullptr;
    QLabel *soundMasterVolumeValue = nullptr;
    QComboBox *soundVolumeSelect = nullptr;
    QSlider *soundSelectedVolume = nullptr;
    QLabel *soundSelectedVolumeValue = nullptr;
    QSlider *soundBufferSize = nullptr;
    QLabel *soundBufferSizeValue = nullptr;
    QComboBox *soundChannels = nullptr;
    QComboBox *soundStereoSeparation = nullptr;
    QComboBox *soundInterpolation = nullptr;
    QComboBox *soundFrequency = nullptr;
    QComboBox *soundSwap = nullptr;
    QComboBox *soundStereoDelay = nullptr;
    QComboBox *soundFilter = nullptr;
    QComboBox *floppySoundDrive = nullptr;
    QComboBox *floppySoundType = nullptr;
    QSlider *floppySoundEmptyVolume = nullptr;
    QLabel *floppySoundEmptyVolumeValue = nullptr;
    QSlider *floppySoundDiskVolume = nullptr;
    QLabel *floppySoundDiskVolumeValue = nullptr;
    int soundVolumeAttenuation[SoundVolumeCount] = {};
    int floppySoundTypeValue[FloppySoundDriveCount] = {};
    int floppySoundEmptyAttenuation[FloppySoundDriveCount] = {};
    int floppySoundDiskAttenuation[FloppySoundDriveCount] = {};
    int currentSoundVolume = 0;
    int currentFloppySoundDrive = 0;
    bool soundVolumeUpdating = false;
    bool floppySoundUpdating = false;
    QComboBox *portDevice[4] = {};
    QComboBox *portAutofire[2] = {};
    QComboBox *portMode[2] = {};
    QString joyportCustom[MaxJoyportCustomSlots];
    QCheckBox *portAutoswitch = nullptr;
    QSpinBox *mouseSpeed = nullptr;
    QCheckBox *virtualMouseDriver = nullptr;
    QComboBox *mouseUntrapMode = nullptr;
    QComboBox *magicMouseCursor = nullptr;
    QCheckBox *tabletLibrary = nullptr;
    QComboBox *tabletMode = nullptr;
    QComboBox *inputType = nullptr;
    QComboBox *inputDevice = nullptr;
    QCheckBox *inputDeviceEnabled = nullptr;
    QTreeWidget *inputMappingList = nullptr;
    QComboBox *inputSubEvent = nullptr;
    QComboBox *inputAmigaEvent = nullptr;
    QPushButton *inputRemapButton = nullptr;
    QPushButton *inputCopyButton = nullptr;
    QPushButton *inputSwapButton = nullptr;
    QSpinBox *inputDeadzone = nullptr;
    QSpinBox *inputAutofireRate = nullptr;
    QSpinBox *inputJoyMouseDigital = nullptr;
    QSpinBox *inputJoyMouseAnalog = nullptr;
    QComboBox *inputCopyFrom = nullptr;
    QCheckBox *inputPageUpEnd = nullptr;
    QCheckBox *inputSwapBackslashF11 = nullptr;
    QMap<QString, QString> inputMappingSettings;
    QStringList inputOwnedMappingKeys;
    bool inputMappingUpdating = false;
    QComboBox *printerPort = nullptr;
    QComboBox *printerType = nullptr;
    QSpinBox *printerAutoFlush = nullptr;
    QLineEdit *ghostscriptParams = nullptr;
    QComboBox *samplerDevice = nullptr;
    QCheckBox *samplerStereo = nullptr;
    QComboBox *serialPort = nullptr;
    QCheckBox *serialShared = nullptr;
    QCheckBox *serialCtsRts = nullptr;
    QCheckBox *serialDirect = nullptr;
    QCheckBox *serialCrlf = nullptr;
    QCheckBox *uaeSerial = nullptr;
    QCheckBox *serialStatus = nullptr;
    QCheckBox *serialRingIndicator = nullptr;
    QComboBox *midiOut = nullptr;
    QComboBox *midiIn = nullptr;
    QCheckBox *midiRouter = nullptr;
    QComboBox *protectionDongle = nullptr;
    QLineEdit *romsPath = nullptr;
    QLineEdit *configsPath = nullptr;
    QLineEdit *nvramPath = nullptr;
    QLineEdit *screenshotsPath = nullptr;
    QLineEdit *stateFilesPath = nullptr;
    QLineEdit *videosPath = nullptr;
    QLineEdit *saveImagesPath = nullptr;
    QLineEdit *ripsPath = nullptr;
    QLineEdit *dataPath = nullptr;
    QLineEdit *logPath = nullptr;
    QCheckBox *recursiveRoms = nullptr;
    QCheckBox *cacheConfigurations = nullptr;
    QCheckBox *cacheBoxArt = nullptr;
    QCheckBox *saveImageOriginalPath = nullptr;
    QCheckBox *relativePaths = nullptr;
    QCheckBox *portableMode = nullptr;
    QCheckBox *fullLogging = nullptr;
    QCheckBox *logWindow = nullptr;
    QComboBox *pathDefaultType = nullptr;
    QComboBox *logSelect = nullptr;
    QListWidget *miscOptionList = nullptr;
    QMap<QString, QListWidgetItem*> miscOptionItems;
    QComboBox *miscScsiMode = nullptr;
    QComboBox *miscWindowedStyle = nullptr;
    QComboBox *miscVideoApi = nullptr;
    QComboBox *miscVideoApiOptions = nullptr;
    QComboBox *miscLanguage = nullptr;
    QComboBox *miscGuiSize = nullptr;
    QCheckBox *miscGuiResize = nullptr;
    QCheckBox *miscGuiFullscreen = nullptr;
    QCheckBox *miscGuiDarkMode = nullptr;
    QString miscGuiFontConfig;
    QComboBox *stateFileName = nullptr;
    QCheckBox *stateFileClear = nullptr;
    QComboBox *keyboardLed[3] = {};
    QCheckBox *keyboardLedUsb = nullptr;
    QComboBox *extensionActivePriority = nullptr;
    QCheckBox *extensionActivePause = nullptr;
    QCheckBox *extensionActiveNoSound = nullptr;
    QCheckBox *extensionActiveNoJoy = nullptr;
    QCheckBox *extensionActiveNoKeyboard = nullptr;
    QComboBox *extensionInactivePriority = nullptr;
    QCheckBox *extensionInactivePause = nullptr;
    QCheckBox *extensionInactiveNoSound = nullptr;
    QCheckBox *extensionInactiveNoJoy = nullptr;
    QComboBox *extensionMinimizedPriority = nullptr;
    QCheckBox *extensionMinimizedPause = nullptr;
    QCheckBox *extensionMinimizedNoSound = nullptr;
    QCheckBox *extensionMinimizedNoJoy = nullptr;
    QTreeWidget *extensionAssociationList = nullptr;
    QTreeWidget *frontendConfigList = nullptr;
    QLabel *frontendScreenshot = nullptr;
    QLabel *frontendInfo = nullptr;
    WinUaeQtConfig loadedConfig;
    WinUaeQtLauncherResult result;

    QTreeWidgetItem *addFolder(const QString &title, QTreeWidgetItem *parent = nullptr)
    {
        QTreeWidgetItem *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(navigation);
        item->setText(0, title);
        item->setIcon(0, resourceIcon(QStringLiteral("folder.ico")));
        QFont font = item->font(0);
        font.setBold(true);
        item->setFont(0, font);
        item->setExpanded(true);
        return item;
    }

    QTreeWidgetItem *addPage(const QString &title, const QString &icon, QWidget *page, QTreeWidgetItem *parent = nullptr)
    {
        QWidget *stackPage = page;
        if (!page->findChild<QScrollArea*>()) {
            if (QLayout *layout = page->layout()) {
                layout->setSizeConstraint(QLayout::SetMinimumSize);
            }
            page->setMinimumSize(page->minimumSizeHint());
            QScrollArea *scroll = new QScrollArea;
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setWidgetResizable(true);
            scroll->setWidget(page);
            stackPage = scroll;
        }
        const int index = pageStack->addWidget(stackPage);
        QTreeWidgetItem *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(navigation);
        item->setText(0, title);
        item->setIcon(0, resourceIcon(icon));
        item->setData(0, Qt::UserRole, index);
        return item;
    }

    QWidget *makePage()
    {
        QWidget *page = new QWidget;
        page->setObjectName(QStringLiteral("page"));
        return page;
    }

    void openHelp()
    {
        if (!QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.winuae.net/help/")))) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Could not open WinUAE help."));
        }
    }

    QWidget *makeConfigurationsPage()
    {
        QWidget *page = makePage();
        QVBoxLayout *root = new QVBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);
        root->setSpacing(4);

        configTree = new QTreeWidget;
        configTree->setHeaderHidden(true);
        configTree->setRootIsDecorated(true);
        root->addWidget(configTree, 1);

        QGridLayout *search = new QGridLayout;
        search->setColumnStretch(1, 1);
        search->setColumnStretch(4, 1);
        configSearch = new QLineEdit;
        configFilter = combo({ QStringLiteral("All configurations"), QStringLiteral("Host"), QStringLiteral("Hardware") });
        search->addWidget(label(QStringLiteral("Search:")), 0, 0);
        search->addWidget(configSearch, 0, 1);
        QPushButton *clearSearch = smallButton(QStringLiteral("X"));
        search->addWidget(clearSearch, 0, 2);
        search->addWidget(label(QStringLiteral("Filter:")), 0, 3);
        search->addWidget(configFilter, 0, 4);
        root->addLayout(search);

        configName = pathCombo();
        configDescription = new QLineEdit;
        configPath = new QLineEdit;
        configPath->setReadOnly(true);

        QGridLayout *details = new QGridLayout;
        details->setColumnStretch(1, 1);
        details->addWidget(label(QStringLiteral("Name:")), 0, 0);
        details->addWidget(configName, 0, 1);
        details->addWidget(configPath, 0, 2);
        details->addWidget(label(QStringLiteral("Description:")), 1, 0);
        details->addWidget(configDescription, 1, 1, 1, 2);
        root->addLayout(details);

        QHBoxLayout *buttons = new QHBoxLayout;
        QPushButton *quickLoad = new QPushButton(QStringLiteral("Load"));
        QPushButton *quickSave = new QPushButton(QStringLiteral("Save"));
        QPushButton *load = new QPushButton(QStringLiteral("Load From..."));
        QPushButton *save = new QPushButton(QStringLiteral("Save As..."));
        QPushButton *deleteConfig = new QPushButton(QStringLiteral("Delete"));
        buttons->addWidget(quickLoad);
        buttons->addWidget(quickSave);
        buttons->addStretch();
        buttons->addWidget(load);
        buttons->addWidget(save);
        buttons->addWidget(deleteConfig);
        root->addLayout(buttons);

        connect(configTree, &QTreeWidget::itemSelectionChanged, this, [this]() { selectConfigFromTree(); });
        connect(configTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *, int) { loadSelectedConfig(); });
        connect(configSearch, &QLineEdit::textChanged, this, [this]() { refreshConfigList(); });
        connect(configFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { refreshConfigList(); });
        connect(clearSearch, &QPushButton::clicked, configSearch, &QLineEdit::clear);
        connect(quickLoad, &QPushButton::clicked, this, [this]() { loadSelectedConfig(); });
        connect(quickSave, &QPushButton::clicked, this, [this]() { saveNamedConfig(); });
        connect(load, &QPushButton::clicked, this, [this]() { loadConfigDialog(); });
        connect(save, &QPushButton::clicked, this, [this]() { saveConfigDialog(); });
        connect(deleteConfig, &QPushButton::clicked, this, [this]() { deleteSelectedConfig(); });
        return page;
    }

    QWidget *makeQuickstartPage()
    {
        QWidget *page = makePage();
        QVBoxLayout *root = new QVBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);
        root->setSpacing(6);

        quickModel = combo(quickstartModelItems(), QStringLiteral("A1200"));
        quickConfiguration = combo({});
        ntsc = new QCheckBox(QStringLiteral("NTSC"));

        QGridLayout *hardware = new QGridLayout;
        hardware->setColumnStretch(1, 1);
        hardware->addWidget(label(QStringLiteral("Model:")), 0, 0);
        hardware->addWidget(quickModel, 0, 1);
        hardware->addWidget(ntsc, 0, 2);
        hardware->addWidget(label(QStringLiteral("Configuration:")), 1, 0);
        hardware->addWidget(quickConfiguration, 1, 1, 1, 2);
        root->addWidget(groupBox(QStringLiteral("Emulated Hardware"), hardware));

        compatibility = new QSlider(Qt::Horizontal);
        compatibility->setRange(0, 4);
        compatibility->setTickInterval(1);
        compatibility->setTickPosition(QSlider::TicksAbove);
        compatibility->setValue(1);

        QHBoxLayout *compat = new QHBoxLayout;
        compat->addWidget(new QLabel(QStringLiteral("Best compatibility")));
        compat->addWidget(compatibility, 1);
        compat->addWidget(new QLabel(QStringLiteral("Low compatibility")));
        root->addWidget(groupBox(QStringLiteral("Compatibility vs Required CPU Power"), compat));

        quickHostConfiguration = combo({
            QStringLiteral("Default"),
            QStringLiteral("Windowed"),
            QStringLiteral("Fullscreen")
        }, QStringLiteral("Default"));
        QGridLayout *host = new QGridLayout;
        host->setColumnStretch(1, 1);
        host->addWidget(label(QStringLiteral("Configuration:")), 0, 0);
        host->addWidget(quickHostConfiguration, 0, 1);
        root->addWidget(groupBox(QStringLiteral("Host Configuration"), host));

        quickstartSetConfig = new QPushButton(QStringLiteral("Set configuration"));
        quickstartMode = new QCheckBox(QStringLiteral("Start in Quickstart mode"));
        QHBoxLayout *mode = new QHBoxLayout;
        mode->addWidget(quickstartSetConfig);
        mode->addStretch();
        mode->addWidget(quickstartMode);
        root->addWidget(groupBox(QStringLiteral("Mode"), mode));

        QVBoxLayout *drives = new QVBoxLayout;
        drives->setSpacing(10);
        addQuickDriveRow(drives, 0);
        addQuickDriveRow(drives, 1);
        drives->addStretch();
        root->addWidget(groupBox(QStringLiteral("Emulated Drives"), drives), 1);

        connect(quickModel, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
            refreshQuickstartConfigurationChoices();
            applyQuickstartSelectionToUi();
        });
        connect(quickConfiguration, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { applyQuickstartSelectionToUi(); });
        connect(compatibility, &QSlider::valueChanged, this, [this]() { applyQuickstartCompatibilityToUi(); });
        connect(quickstartSetConfig, &QPushButton::clicked, this, [this]() { applyQuickstartSelectionToUi(); });
        connect(quickstartMode, &QCheckBox::toggled, this, [this](bool checked) {
            quickstartSetConfig->setVisible(!checked);
            applyQuickstartSelectionToUi();
        });
        refreshQuickstartConfigurationChoices();
        return page;
    }

    void refreshQuickstartConfigurationChoices(int requestedIndex = -1)
    {
        if (!quickConfiguration || !quickModel) {
            return;
        }
        const QSignalBlocker blocker(quickConfiguration);
        const int oldIndex = quickConfiguration->currentIndex();
        quickConfiguration->clear();
        const QuickstartModelChoice *choice = quickstartModelChoiceByDisplay(quickModel->currentText());
        if (!choice) {
            return;
        }
        quickConfiguration->addItems(quickstartConfigItems(*choice));
        const int selected = requestedIndex >= 0 ? requestedIndex : oldIndex;
        quickConfiguration->setCurrentIndex(qBound(0, selected, qMax(0, quickConfiguration->count() - 1)));
        compatibility->setEnabled(choice->compatibilityLevels > 1);
        compatibility->setRange(0, qMax(1, choice->compatibilityLevels - 1));
        compatibility->setValue(qBound(0, compatibility->value(), compatibility->maximum()));
    }

    QString quickstartConfigValue() const
    {
        const QuickstartModelChoice *choice = quickstartModelChoiceByDisplay(quickModel->currentText());
        if (!choice) {
            return QString();
        }
        return QStringLiteral("%1,%2").arg(QString::fromLatin1(choice->configValue)).arg(qMax(0, quickConfiguration->currentIndex()));
    }

    void clearQuickstartCd32ExpansionBoards()
    {
        expansionBoardStates.remove(expansionBoardConfigName(QStringLiteral("cd32fmv"), 0));
        expansionBoardStates.remove(expansionBoardConfigName(QStringLiteral("cubo"), 0));
    }

    void setQuickstartExpansionBoard(const QString &boardKey, const QString &romFile)
    {
        const QString configName = expansionBoardConfigName(boardKey, 0);
        if (configName.isEmpty()) {
            return;
        }
        WinUaeQtExpansionBoardState state = expansionBoardStates.value(configName);
        state.present = true;
        state.romFile = romFile;
        expansionBoardStates.insert(configName, state);
    }

    QString cuboNvramPath() const
    {
        const QString configured = nvramPath ? expandedPathText(nvramPath->text()) : QString();
        const QString root = configured.isEmpty()
            ? expandedPathText(unixDefaultDataSubPath(QStringLiteral("NVRAMs")))
            : configured;
        return QDir(root).filePath(QStringLiteral("cd32cubo.nvr"));
    }

    void applyQuickstartExpansionPreset(const QString &model, int config)
    {
        clearQuickstartCd32ExpansionBoards();
        const bool explicitPreset = !quickstartMode || !quickstartMode->isChecked();
        const QString generatedCuboNvram = cuboNvramPath();
        if (flashFile && flashFile->text().trimmed() == generatedCuboNvram
            && (model != QStringLiteral("CD32") || config <= 1)) {
            flashFile->clear();
        }

        if (model != QStringLiteral("CD32")) {
            refreshExpansionBoardChoiceLabels();
            refreshHardwareInfoPage();
            return;
        }

        if (config == 1 && quickstartMode && !quickstartMode->isChecked()) {
            const QString fmvRom = cartFile ? cartFile->currentText().trimmed() : QString();
            if (!fmvRom.isEmpty()) {
                setQuickstartExpansionBoard(QStringLiteral("cd32fmv"), fmvRom);
            }
        } else if (config > 1) {
            setQuickstartExpansionBoard(QStringLiteral("cubo"), QStringLiteral(":ENABLED"));
            if (explicitPreset && flashFile && flashFile->text().trimmed().isEmpty()) {
                flashFile->setText(generatedCuboNvram);
            }
        }

        refreshExpansionBoardChoiceLabels();
        refreshHardwareInfoPage();
    }

    void applyQuickstartSelectionToUi()
    {
        if (quickstartUpdating) {
            return;
        }
        const QuickstartModelChoice *choice = quickstartModelChoiceByDisplay(quickModel->currentText());
        if (!choice) {
            return;
        }

        quickstartUpdating = true;
        const int config = qMax(0, quickConfiguration->currentIndex());
        QString compatible = QString::fromLatin1(choice->compatible);
        if (quickModel->currentText() == QStringLiteral("CDTV") && config >= 2) {
            compatible = QStringLiteral("CDTV-CR");
        }
        if (quickModel->currentText() == QStringLiteral("A1000") && config >= 3) {
            compatible = QStringLiteral("Velvet");
        }

        applyModelPreset(compatible);
        applyAdvancedChipsetPreset(compatible);
        applyQuickstartConfigurationMemory(quickModel->currentText(), config);
        applyQuickstartExpansionPreset(quickModel->currentText(), config);
        applyQuickstartCompatibilityToUi();
        quickstartUpdating = false;
    }

    void applyQuickstartConfigurationMemory(const QString &model, int config)
    {
        z2Fast->setCurrentText(QStringLiteral("None"));
        z3Fast->setCurrentText(QStringLiteral("None"));
        z3ChipMem->setCurrentText(QStringLiteral("None"));
        processorSlotMem->setCurrentText(QStringLiteral("None"));
        z3Mapping->setCurrentText(QStringLiteral("Automatic"));
        rtgMem->setCurrentText(QStringLiteral("None"));

        if (model == QStringLiteral("A500")) {
            setCpuButton(68000);
            setFpuButton(0);
            const bool oneMegChip = config == 2;
            const bool noSlow = config == 2 || config == 3 || config == 4;
            chipMem->setCurrentText(oneMegChip ? QStringLiteral("1 MB") : QStringLiteral("512 KB"));
            slowMem->setCurrentText(noSlow ? QStringLiteral("None") : QStringLiteral("512 KB"));
            chipset->setCurrentText((config == 1 || config == 2) ? QStringLiteral("ECS") : QStringLiteral("OCS"));
        } else if (model == QStringLiteral("A500+") || model == QStringLiteral("A600")) {
            setCpuButton(68000);
            setFpuButton(0);
            chipMem->setCurrentText(config == 1 ? QStringLiteral("2 MB") : QStringLiteral("1 MB"));
            slowMem->setCurrentText(QStringLiteral("None"));
            if (config == 2) {
                z2Fast->setCurrentText(QStringLiteral("4 MB"));
            }
        } else if (model == QStringLiteral("A1200")) {
            if (config == 2) {
                setCpuButton(68030);
                setFpuButton(68882);
            } else if (config == 3) {
                setCpuButton(68040);
                setFpuButton(68040);
            } else if (config >= 4) {
                setCpuButton(68060);
                setFpuButton(68060);
            } else {
                setCpuButton(68020);
                setFpuButton(0);
            }
            chipMem->setCurrentText(QStringLiteral("2 MB"));
            slowMem->setCurrentText(QStringLiteral("None"));
            if (config == 1) {
                z2Fast->setCurrentText(QStringLiteral("4 MB"));
            } else if (config >= 2) {
                processorSlotMem->setCurrentText(config >= 5 ? QStringLiteral("128 MB") : QStringLiteral("32 MB"));
            }
        } else if (model == QStringLiteral("A3000")) {
            setCpuButton(68030);
            setFpuButton(68882);
            chipMem->setCurrentText(QStringLiteral("2 MB"));
            slowMem->setCurrentText(QStringLiteral("None"));
            processorSlotMem->setCurrentText(QStringLiteral("8 MB"));
        } else if (model == QStringLiteral("A4000")) {
            if (config == 1) {
                setCpuButton(68040);
                setFpuButton(68040);
            } else if (config >= 2) {
                setCpuButton(68060);
                setFpuButton(68060);
            } else {
                setCpuButton(68030);
                setFpuButton(68882);
            }
            chipMem->setCurrentText(QStringLiteral("2 MB"));
            slowMem->setCurrentText(QStringLiteral("None"));
            processorSlotMem->setCurrentText(QStringLiteral("8 MB"));
        } else if (model == QStringLiteral("CD32")) {
            setCpuButton(68020);
            setFpuButton(0);
            chipMem->setCurrentText(QStringLiteral("2 MB"));
            slowMem->setCurrentText(QStringLiteral("None"));
        } else if (model == QStringLiteral("CDTV")) {
            setCpuButton(68000);
            setFpuButton(0);
            chipMem->setCurrentText(QStringLiteral("1 MB"));
            slowMem->setCurrentText(QStringLiteral("None"));
        }

        if (model == QStringLiteral("CD32") || (model == QStringLiteral("CDTV") && config == 0)) {
            for (int i = 0; i < 2; i++) {
                dfEnable[i]->setChecked(false);
                quickDfEnable[i]->setChecked(false);
            }
        } else {
            dfEnable[0]->setChecked(true);
            quickDfEnable[0]->setChecked(true);
            dfEnable[1]->setChecked(false);
            quickDfEnable[1]->setChecked(false);
        }
    }

    void applyQuickstartCompatibilityToUi()
    {
        if (!compatibility) {
            return;
        }
        const int level = compatibility->value();
        const int cpu = selectedCpuModel();
        const bool exact = level == 0 && cpu <= 68030;
        const bool memoryExact = level <= 1 && cpu <= 68030;
        if (QAbstractButton *button = cpuSpeedButtons->button(level >= 3 ? 1 : 0)) {
            button->setChecked(true);
        }
        chipsetCycleExact->setChecked(exact);
        chipsetCycleExactMemory->setChecked(memoryExact);
        moreCompatible->setChecked(level <= 2);
        if (cpu >= 68030) {
            cpu24Bit->setChecked(false);
        }
        updateCpuControlState();
    }

    void addQuickDriveRow(QVBoxLayout *layout, int drive)
    {
        quickDfEnable[drive] = new QCheckBox(QStringLiteral("Floppy drive DF%1:").arg(drive));
        QPushButton *select = new QPushButton(QStringLiteral("Select image file"));
        quickDfType[drive] = combo(floppyTypeItems(drive, true));
        quickDfWriteProtect[drive] = new QCheckBox;
        QPushButton *info = smallButton(QStringLiteral("?"));
        QPushButton *eject = new QPushButton(QStringLiteral("Eject"));
        quickDfPath[drive] = pathCombo();
        quickDfPath[drive]->setMinimumWidth(300);
        QWidget *driveWidget = new QWidget;
        QVBoxLayout *driveLayout = new QVBoxLayout(driveWidget);
        driveLayout->setContentsMargins(0, 0, 0, 0);
        driveLayout->setSpacing(4);

        QHBoxLayout *controls = new QHBoxLayout;
        controls->setContentsMargins(0, 0, 0, 0);
        controls->setSpacing(6);
        controls->addWidget(quickDfEnable[drive]);
        controls->addWidget(select);
        controls->addWidget(quickDfType[drive]);
        controls->addWidget(new QLabel(QStringLiteral("Write-protected")));
        controls->addWidget(quickDfWriteProtect[drive]);
        controls->addWidget(info);
        controls->addWidget(eject);
        controls->addStretch();
        driveLayout->addLayout(controls);
        driveLayout->addWidget(quickDfPath[drive]);
        layout->addWidget(driveWidget);
        quickDfEnable[drive]->setChecked(drive == 0);
        connect(info, &QPushButton::clicked, this, [this, drive]() {
            showFloppyInfo(quickDfPath[drive] ? quickDfPath[drive]->currentText() : QString(), drive);
        });
        connect(select, &QPushButton::clicked, this, [this, drive]() {
            addBrowse(quickDfPath[drive], this, QStringLiteral("Select floppy image"), floppyDiskImageFilter());
        });
        connect(eject, &QPushButton::clicked, this, [this, drive]() {
            quickDfPath[drive]->setCurrentText(QString());
        });
    }

    QWidget *makeCpuPage()
    {
        QWidget *page = makePage();
        QHBoxLayout *root = new QHBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);
        root->setSpacing(6);

        QVBoxLayout *left = new QVBoxLayout;
        left->setSpacing(6);
        cpuButtons = new QButtonGroup(this);
        QGridLayout *cpu = new QGridLayout;
        cpu->setHorizontalSpacing(12);
        cpu->setVerticalSpacing(5);
        cpu->setColumnStretch(0, 1);
        cpu->setColumnStretch(1, 1);
        const QStringList cpus = { QStringLiteral("68000"), QStringLiteral("68010"), QStringLiteral("68020"), QStringLiteral("68030"), QStringLiteral("68040"), QStringLiteral("68060") };
        for (int i = 0; i < cpus.size(); i++) {
            const QString &name = cpus[i];
            QRadioButton *button = new QRadioButton(name);
            cpu->addWidget(button, i / 2, i % 2);
            cpuButtons->addButton(button, name.mid(2).toInt() + 68000);
            connect(button, &QRadioButton::clicked, this, [this]() {
                updateFpuControls();
                updateCpuControlState();
            });
            if (name == QStringLiteral("68020")) {
                button->setChecked(true);
            }
        }
        cpu24Bit = new QCheckBox(QStringLiteral("24-bit addressing"));
        moreCompatible = new QCheckBox(QStringLiteral("More compatible"));
        cpuDataCache = new QCheckBox(QStringLiteral("Data cache emulation"));
        jit = new QCheckBox(QStringLiteral("JIT"));
        if (!unixJitBackendAvailable()) {
            jit->setToolTip(QStringLiteral("JIT is not enabled in this Unix build yet."));
        }
        cpuUnimplemented = new QCheckBox(QStringLiteral("Unimplemented CPU emu"));
        cpu->addWidget(cpu24Bit, 3, 0, 1, 2);
        cpu->addWidget(moreCompatible, 4, 0, 1, 2);
        cpu->addWidget(cpuDataCache, 5, 0, 1, 2);
        cpu->addWidget(jit, 6, 0, 1, 2);
        cpu->addWidget(cpuUnimplemented, 7, 0, 1, 2);
        left->addWidget(groupBox(QStringLiteral("CPU"), cpu));

        mmuButtons = new QButtonGroup(this);
        QHBoxLayout *mmu = new QHBoxLayout;
        const QStringList mmus = { QStringLiteral("None"), QStringLiteral("MMU"), QStringLiteral("EC") };
        for (int i = 0; i < mmus.size(); i++) {
            QRadioButton *button = new QRadioButton(mmus[i]);
            mmu->addWidget(button);
            mmuButtons->addButton(button, i);
            if (i == 0) {
                button->setChecked(true);
            }
        }
        left->addWidget(groupBox(QStringLiteral("MMU"), mmu));

        fpuButtons = new QButtonGroup(this);
        QVBoxLayout *fpu = new QVBoxLayout;
        fpu->setSpacing(5);
        const QStringList fpus = { QStringLiteral("None"), QStringLiteral("68881"), QStringLiteral("68882"), QStringLiteral("CPU internal") };
        for (int i = 0; i < fpus.size(); i++) {
            QRadioButton *button = new QRadioButton(fpus[i]);
            fpu->addWidget(button);
            const int id = i == 1 ? 68881 : (i == 2 ? 68882 : (i == 3 ? FpuInternal : 0));
            fpuButtons->addButton(button, id);
            if (i == 0) {
                button->setChecked(true);
            }
        }
        fpuStrict = new QCheckBox(QStringLiteral("More compatible"));
        fpuUnimplemented = new QCheckBox(QStringLiteral("Unimplemented FPU emu"));
        fpuMode = combo({
            QStringLiteral("Host (64-bit)"),
            QStringLiteral("Host (80-bit)"),
            QStringLiteral("Softfloat (80-bit)")
        }, QStringLiteral("Host (64-bit)"));
        fpu->addWidget(fpuStrict);
        fpu->addWidget(fpuUnimplemented);
        fpu->addWidget(fpuMode);
        left->addWidget(groupBox(QStringLiteral("FPU"), fpu));
        left->addStretch();

        QVBoxLayout *right = new QVBoxLayout;
        QGridLayout *speed = new QGridLayout;
        cpuSpeedButtons = new QButtonGroup(this);
        QRadioButton *fastest = new QRadioButton(QStringLiteral("Fastest possible"));
        QRadioButton *approx = new QRadioButton(QStringLiteral("Approximate A500/A1200 or cycle-exact"));
        approx->setChecked(true);
        cpuSpeedButtons->addButton(fastest, 1);
        cpuSpeedButtons->addButton(approx, 0);
        speed->addWidget(fastest, 0, 0, 1, 2);
        speed->addWidget(approx, 1, 0, 1, 2);
        cpuSpeed = new QSlider(Qt::Horizontal);
        cpuSpeed->setRange(-9, 50);
        cpuSpeed->setTickInterval(1);
        cpuSpeed->setTickPosition(QSlider::TicksAbove);
        cpuSpeedLabel = new QLineEdit;
        cpuSpeedLabel->setReadOnly(true);
        cpuSpeedLabel->setAlignment(Qt::AlignCenter);
        cpuSpeedLabel->setMinimumWidth(60);
        speed->addWidget(label(QStringLiteral("CPU Speed")), 2, 0);
        speed->addWidget(cpuSpeed, 2, 1);
        speed->addWidget(cpuSpeedLabel, 2, 2);
        right->addWidget(groupBox(QStringLiteral("CPU Emulation Speed"), speed));

        QGridLayout *cycle = new QGridLayout;
        cpuFrequency = combo({
            QStringLiteral("1x"),
            QStringLiteral("2x (A500)"),
            QStringLiteral("4x (A1200)"),
            QStringLiteral("8x"),
            QStringLiteral("16x"),
            QStringLiteral("Custom")
        }, QStringLiteral("4x (A1200)"));
        cpuFrequencyCustom = new QLineEdit;
        cpuFrequencyCustom->setPlaceholderText(QStringLiteral("MHz"));
        cycle->addWidget(label(QStringLiteral("CPU Frequency")), 0, 0);
        cycle->addWidget(cpuFrequency, 0, 1);
        cycle->addWidget(cpuFrequencyCustom, 0, 2);
        right->addWidget(groupBox(QStringLiteral("Cycle-exact CPU Emulation Speed"), cycle));

        QGridLayout *jitBox = new QGridLayout;
        jitCache = new QSlider(Qt::Horizontal);
        jitCache->setRange(0, 8);
        jitCache->setTickInterval(1);
        jitCache->setTickPosition(QSlider::TicksAbove);
        jitCacheLabel = new QLineEdit;
        jitCacheLabel->setReadOnly(true);
        jitCacheLabel->setAlignment(Qt::AlignCenter);
        jitCacheLabel->setMinimumWidth(60);
        jitFpu = new QCheckBox(QStringLiteral("FPU support"));
        jitConstJump = new QCheckBox(QStringLiteral("Constant jump"));
        jitHardFlush = new QCheckBox(QStringLiteral("Hard flush"));
        jitTrust = new QButtonGroup(this);
        QRadioButton *jitDirect = new QRadioButton(QStringLiteral("Direct"));
        QRadioButton *jitIndirect = new QRadioButton(QStringLiteral("Indirect"));
        jitTrust->addButton(jitDirect, 0);
        jitTrust->addButton(jitIndirect, 1);
        jitDirect->setChecked(true);
        jitNoFlags = new QCheckBox(QStringLiteral("No flags"));
        jitCatchFault = new QCheckBox(QStringLiteral("Catch unexpected exceptions"));
        jitBox->addWidget(label(QStringLiteral("Cache size:")), 0, 0);
        jitBox->addWidget(jitCache, 0, 1);
        jitBox->addWidget(jitCacheLabel, 0, 2);
        jitBox->addWidget(jitFpu, 1, 0);
        jitBox->addWidget(jitConstJump, 1, 1);
        jitBox->addWidget(jitHardFlush, 1, 2);
        jitBox->addWidget(jitDirect, 2, 0);
        jitBox->addWidget(jitIndirect, 2, 1);
        jitBox->addWidget(jitNoFlags, 2, 2);
        jitBox->addWidget(jitCatchFault, 3, 0, 1, 3);
        right->addWidget(groupBox(QStringLiteral("Advanced JIT Settings"), jitBox));
        right->addStretch();

        connect(cpu24Bit, &QCheckBox::toggled, this, [this]() { updateCpuControlState(); });
        connect(moreCompatible, &QCheckBox::toggled, this, [this]() { updateCpuControlState(); });
        connect(jit, &QCheckBox::toggled, this, [this](bool checked) {
            if (checked) {
                if (jitCache && jitCacheSizeFromPosition(jitCache->value()) <= 0) {
                    jitCache->setValue(jitCachePositionFromSize(defaultJitCacheSize()));
                }
                setCheckBoxIfChanged(chipsetCycleExact, false);
                setCheckBoxIfChanged(chipsetCycleExactMemory, false);
            }
            updateCpuControlState();
        });
        connect(fpuButtons, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked), this, [this]() { updateCpuControlState(); });
        connect(cpuSpeed, &QSlider::valueChanged, this, [this]() { updateCpuSpeedLabel(); });
        connect(jitCache, &QSlider::valueChanged, this, [this]() {
            if (jit && jit->isChecked() && jitCacheSizeFromPosition(jitCache->value()) <= 0) {
                jit->setChecked(false);
            }
            updateJitCacheLabel();
            updateCpuControlState();
        });
        updateFpuControls();
        updateCpuControlState();

        root->addLayout(left, 2);
        root->addLayout(right, 3);
        return page;
    }

    QWidget *makeChipsetPage()
    {
        QWidget *page = makePage();
        QGridLayout *root = new QGridLayout(page);
        root->setContentsMargins(4, 4, 4, 4);
        root->setColumnStretch(0, 1);
        root->setColumnStretch(1, 1);

        chipset = combo({
            QStringLiteral("OCS"),
            QStringLiteral("ECS Agnus"),
            QStringLiteral("ECS Denise"),
            QStringLiteral("ECS"),
            QStringLiteral("AGA"),
            QStringLiteral("A1000"),
            QStringLiteral("A1000 (No EHB)")
        }, QStringLiteral("AGA"));
        chipsetNtsc = new QCheckBox(QStringLiteral("NTSC"));
        chipsetCycleExact = new QCheckBox(QStringLiteral("Cycle-exact (Full)"));
        chipsetCycleExactMemory = new QCheckBox(QStringLiteral("Cycle-exact (DMA/Memory accesses)"));
        chipsetCompatible = combo({
            QStringLiteral("-"),
            QStringLiteral("Generic"),
            QStringLiteral("CDTV"),
            QStringLiteral("CDTV-CR"),
            QStringLiteral("CD32"),
            QStringLiteral("A500"),
            QStringLiteral("A500+"),
            QStringLiteral("A600"),
            QStringLiteral("A1000"),
            QStringLiteral("A1200"),
            QStringLiteral("A2000"),
            QStringLiteral("A3000"),
            QStringLiteral("A3000T"),
            QStringLiteral("A4000"),
            QStringLiteral("A4000T"),
            QStringLiteral("Velvet"),
            QStringLiteral("Casablanca"),
            QStringLiteral("DraCo")
        }, QStringLiteral("A1200"));
        QVBoxLayout *basic = new QVBoxLayout;
        basic->setSpacing(6);
        QGridLayout *chipsetSelection = new QGridLayout;
        chipsetSelection->setHorizontalSpacing(8);
        chipsetSelection->setVerticalSpacing(5);
        chipsetSelection->setColumnStretch(1, 1);
        chipsetSelection->addWidget(label(QStringLiteral("Chipset:")), 0, 0);
        chipsetSelection->addWidget(chipset, 0, 1);
        chipsetSelection->addWidget(chipsetNtsc, 0, 2);
        chipsetSelection->addWidget(label(QStringLiteral("Chipset Extra:")), 1, 0);
        chipsetSelection->addWidget(chipsetCompatible, 1, 1, 1, 2);
        basic->addLayout(chipsetSelection);
        basic->addWidget(chipsetCycleExact);
        basic->addWidget(chipsetCycleExactMemory);
        root->addWidget(groupBox(QStringLiteral("Chipset"), basic), 0, 0);

        immediateBlits = new QCheckBox(QStringLiteral("Immediate Blitter"));
        waitingBlits = new QCheckBox(QStringLiteral("Wait for Blitter"));
        displayOptimization = combo({
            QStringLiteral("Full"),
            QStringLiteral("Partial"),
            QStringLiteral("None")
        }, QStringLiteral("Full"));
        chipsetSyncMode = combo(configChoiceDisplays(hvSyncChoices, int(sizeof(hvSyncChoices) / sizeof(hvSyncChoices[0]))));
        QGridLayout *options = new QGridLayout;
        options->setColumnStretch(1, 1);
        options->addWidget(immediateBlits, 0, 0, 1, 2);
        options->addWidget(waitingBlits, 1, 0, 1, 2);
        options->addWidget(label(QStringLiteral("Optimizations:")), 2, 0);
        options->addWidget(displayOptimization, 2, 1);
        options->addWidget(label(QStringLiteral("H/V sync mode:")), 3, 0);
        options->addWidget(chipsetSyncMode, 3, 1);
        root->addWidget(groupBox(QStringLiteral("Options"), options), 0, 1);

        collisionButtons = new QButtonGroup(this);
        QGridLayout *collision = new QGridLayout;
        const QStringList collisions = {
            QStringLiteral("None"),
            QStringLiteral("Sprites only"),
            QStringLiteral("Sprites and Sprites vs. Playfield"),
            QStringLiteral("Full")
        };
        for (int i = 0; i < collisions.size(); i++) {
            QRadioButton *button = new QRadioButton(collisions[i]);
            collisionButtons->addButton(button, i);
            collision->addWidget(button, i / 2, i % 2);
            if (i == 3) {
                button->setChecked(true);
            }
        }
        root->addWidget(groupBox(QStringLiteral("Collision Level"), collision), 1, 0, 1, 2);

        keyboardMode = combo(configChoiceDisplays(keyboardModeChoices, int(sizeof(keyboardModeChoices) / sizeof(keyboardModeChoices[0]))), QStringLiteral("UAE High level emulation"));
        keyboardNkro = new QCheckBox(QStringLiteral("Keyboard N-key rollover"));
        QGridLayout *keyboard = new QGridLayout;
        keyboard->setColumnStretch(0, 1);
        keyboard->addWidget(keyboardMode, 0, 0);
        keyboard->addWidget(keyboardNkro, 1, 0);
        root->addWidget(groupBox(QStringLiteral("Keyboard"), keyboard), 2, 0, 1, 2);

        genlockConnected = new QCheckBox(QStringLiteral("Genlock connected"));
        genlockMode = combo(configChoiceDisplays(genlockModeChoices, int(sizeof(genlockModeChoices) / sizeof(genlockModeChoices[0]))));
        genlockMix = combo({
            QStringLiteral("100%"),
            QStringLiteral("90%"),
            QStringLiteral("80%"),
            QStringLiteral("70%"),
            QStringLiteral("60%"),
            QStringLiteral("50%"),
            QStringLiteral("40%"),
            QStringLiteral("30%"),
            QStringLiteral("20%"),
            QStringLiteral("10%"),
            QStringLiteral("0%")
        });
        genlockAlpha = new QCheckBox(QStringLiteral("Include alpha channel in screenshots and video captures"));
        genlockKeepAspect = new QCheckBox(QStringLiteral("Keep aspect ratio"));
        genlockFile = pathCombo();
        genlockFileBrowse = smallButton(QStringLiteral("..."));
        QGridLayout *genlock = new QGridLayout;
        genlock->setColumnStretch(1, 1);
        genlock->addWidget(genlockConnected, 0, 0);
        genlock->addWidget(genlockMode, 0, 1);
        genlock->addWidget(genlockMix, 0, 2);
        genlock->addWidget(genlockAlpha, 1, 0, 1, 3);
        genlock->addWidget(genlockKeepAspect, 2, 0, 1, 3);
        genlock->addWidget(genlockFile, 3, 0, 1, 2);
        genlock->addWidget(genlockFileBrowse, 3, 2);
        root->addWidget(groupBox(QStringLiteral("Genlock"), genlock), 3, 0, 1, 2);
        root->setRowStretch(4, 1);

        connect(chipsetNtsc, &QCheckBox::toggled, this, [this](bool checked) {
            setCheckBoxIfChanged(ntsc, checked);
        });
        connect(ntsc, &QCheckBox::toggled, this, [this](bool checked) {
            setCheckBoxIfChanged(chipsetNtsc, checked);
        });
        connect(chipsetCycleExact, &QCheckBox::toggled, this, [this](bool checked) {
            if (checked && jit && jit->isChecked()) {
                setCheckBoxIfChanged(chipsetCycleExact, false);
                return;
            }
            if (checked) {
                chipsetCycleExactMemory->setChecked(true);
            }
            updateCpuControlState();
        });
        connect(chipsetCycleExactMemory, &QCheckBox::toggled, this, [this](bool checked) {
            if (checked && jit && jit->isChecked()) {
                setCheckBoxIfChanged(chipsetCycleExactMemory, false);
                return;
            }
            if (!checked) {
                chipsetCycleExact->setChecked(false);
            }
            updateCpuControlState();
        });
        connect(immediateBlits, &QCheckBox::toggled, this, [this](bool checked) {
            if (checked) {
                waitingBlits->setChecked(false);
            }
        });
        connect(waitingBlits, &QCheckBox::toggled, this, [this](bool checked) {
            if (checked) {
                immediateBlits->setChecked(false);
            }
        });
        connect(genlockConnected, &QCheckBox::toggled, this, [this]() { updateGenlockControlState(); });
        connect(genlockMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { updateGenlockControlState(); });
        connect(genlockFile, &QComboBox::currentTextChanged, this, [this]() { storeGenlockFilePath(); });
        connect(genlockFileBrowse, &QPushButton::clicked, this, [this]() {
            const QString mode = configChoiceValue(genlockModeChoices, int(sizeof(genlockModeChoices) / sizeof(genlockModeChoices[0])), genlockMode->currentText());
            if (genlockModeUsesImageFile(mode)) {
                addBrowse(genlockFile, this, QStringLiteral("Select genlock image"), QStringLiteral("Images (*.png *.bmp *.jpg *.jpeg);;All files (*)"));
            } else if (genlockModeUsesVideoFile(mode)) {
                addBrowse(genlockFile, this, QStringLiteral("Select genlock video"), QStringLiteral("Video files (*.avi *.mp4 *.mov *.mkv);;All files (*)"));
            }
            storeGenlockFilePath();
        });
        updateGenlockControlState();
        return page;
    }

    int genlockMixConfigValue() const
    {
        if (!genlockMix) {
            return 0;
        }
        const int index = genlockMix->currentIndex();
        return index >= 10 ? 255 : qBound(0, index, 10) * 25;
    }

    void setGenlockMixConfigValue(int value)
    {
        if (!genlockMix) {
            return;
        }
        const int index = value >= 250 ? 10 : qBound(0, value / 25, 10);
        genlockMix->setCurrentIndex(index);
    }

    void storeGenlockFilePath()
    {
        if (genlockUpdating || !genlockMode || !genlockFile) {
            return;
        }
        const QString mode = configChoiceValue(genlockModeChoices, int(sizeof(genlockModeChoices) / sizeof(genlockModeChoices[0])), genlockMode->currentText());
        if (genlockModeUsesImageFile(mode)) {
            genlockImagePath = genlockFile->currentText().trimmed();
        } else if (genlockModeUsesVideoFile(mode)) {
            genlockVideoPath = genlockFile->currentText().trimmed();
        }
    }

    void updateGenlockControlState()
    {
        if (!genlockConnected || !genlockMode || !genlockMix || !genlockAlpha || !genlockKeepAspect || !genlockFile || !genlockFileBrowse) {
            return;
        }

        const bool connected = genlockConnected->isChecked();
        const QString mode = configChoiceValue(genlockModeChoices, int(sizeof(genlockModeChoices) / sizeof(genlockModeChoices[0])), genlockMode->currentText());
        const bool usesImage = genlockModeUsesImageFile(mode);
        const bool usesVideo = genlockModeUsesVideoFile(mode);
        const bool usesFile = connected && (usesImage || usesVideo);

        genlockMode->setEnabled(connected);
        genlockMix->setEnabled(connected);
        genlockAlpha->setEnabled(connected);
        genlockKeepAspect->setEnabled(connected);
        genlockFile->setEnabled(usesFile);
        genlockFileBrowse->setEnabled(usesFile);

        genlockUpdating = true;
        setPathComboText(genlockFile, usesImage ? genlockImagePath : (usesVideo ? genlockVideoPath : QString()));
        genlockUpdating = false;
    }

    QWidget *makeAdvancedChipsetPage()
    {
        QWidget *page = makePage();
        QVBoxLayout *outer = new QVBoxLayout(page);
        outer->setContentsMargins(0, 0, 0, 0);

        QWidget *content = new QWidget;
        QVBoxLayout *root = new QVBoxLayout(content);
        root->setContentsMargins(4, 4, 4, 4);
        root->setSpacing(6);

        advancedCompatible = new QCheckBox(QStringLiteral("Compatible Settings"));
        root->addWidget(advancedCompatible);

        advancedRtcAdjust = new QLineEdit;
        advancedRtcAdjust->setFixedWidth(64);
        advancedRtcAdjust->setValidator(new QIntValidator(-100000, 100000, this));
        QGridLayout *rtc = new QGridLayout;
        advancedRtcButtons = radioGroup(rtcChoices, int(sizeof(rtcChoices) / sizeof(rtcChoices[0])), 0, this, rtc);
        rtc->addWidget(label(QStringLiteral("Adjust:")), 0, 4);
        rtc->addWidget(advancedRtcAdjust, 0, 5);
        rtc->setColumnStretch(6, 1);
        root->addWidget(groupBox(QStringLiteral("RTC"), rtc));

        QGridLayout *tod = new QGridLayout;
        advancedCiaTodButtons = radioGroup(ciaTodChoices, int(sizeof(ciaTodChoices) / sizeof(ciaTodChoices[0])), 0, this, tod);
        tod->setColumnStretch(3, 1);
        root->addWidget(groupBox(QStringLiteral("CIA-A TOD"), tod));

        advancedCheckBoxes.clear();
        auto featureCheck = [this](const QString &text, const QString &key) {
            QCheckBox *box = new QCheckBox(text);
            advancedCheckBoxes.insert(key, box);
            return box;
        };
        auto disabledFeature = [](const QString &text) {
            QCheckBox *box = new QCheckBox(text);
            disableUnavailable(box, QStringLiteral("This hardware option is not implemented by the Unix chipset backend yet."));
            return box;
        };

        advancedIdeA600A1200 = new QCheckBox(QStringLiteral("A600/A1200 IDE"));
        advancedIdeA4000 = new QCheckBox(QStringLiteral("A4000/A4000T IDE"));
        advancedCia391078 = new QCheckBox(QStringLiteral("CIA 391078-01"));
        QGridLayout *features = new QGridLayout;
        features->setColumnStretch(0, 1);
        features->setColumnStretch(1, 1);
        features->setColumnStretch(2, 1);
        features->addWidget(featureCheck(QStringLiteral("CIA ROM Overlay"), QStringLiteral("cia_overlay")), 0, 0);
        features->addWidget(featureCheck(QStringLiteral("CD32 CD"), QStringLiteral("cd32cd")), 1, 0);
        features->addWidget(featureCheck(QStringLiteral("CDTV CD"), QStringLiteral("cdtvcd")), 2, 0);
        features->addWidget(advancedIdeA600A1200, 3, 0);
        features->addWidget(featureCheck(QStringLiteral("ROM Mirror (E0)"), QStringLiteral("ksmirror_e0")), 4, 0);
        features->addWidget(featureCheck(QStringLiteral("KB Reset Warning"), QStringLiteral("resetwarning")), 5, 0);
        features->addWidget(featureCheck(QStringLiteral("CIA TOD bug"), QStringLiteral("cia_todbug")), 6, 0);
        features->addWidget(featureCheck(QStringLiteral("1M Chip / 0.5M+0.5M"), QStringLiteral("1mchipjumper")), 7, 0);
        features->addWidget(featureCheck(QStringLiteral("Toshiba Gary"), QStringLiteral("toshiba_gary")), 8, 0);
        features->addWidget(featureCheck(QStringLiteral("A1000 Boot RAM/ROM"), QStringLiteral("a1000ram")), 0, 1);
        features->addWidget(featureCheck(QStringLiteral("CD32 C2P"), QStringLiteral("cd32c2p")), 1, 1);
        features->addWidget(featureCheck(QStringLiteral("CDTV SRAM"), QStringLiteral("cdtvram")), 2, 1);
        features->addWidget(advancedIdeA4000, 3, 1);
        features->addWidget(featureCheck(QStringLiteral("ROM Mirror (A8)"), QStringLiteral("ksmirror_a8")), 4, 1);
        features->addWidget(featureCheck(QStringLiteral("Z3 Autoconfig"), QStringLiteral("z3_autoconfig")), 5, 1);
        features->addWidget(disabledFeature(QStringLiteral("Custom register byte write bug")), 6, 1);
        features->addWidget(featureCheck(QStringLiteral("KS ROM has Chip RAM speed"), QStringLiteral("rom_is_slow")), 7, 1);
        features->addWidget(featureCheck(QStringLiteral("DF0: ID Hardware"), QStringLiteral("df0idhw")), 0, 2);
        features->addWidget(featureCheck(QStringLiteral("CD32 NVRAM"), QStringLiteral("cd32nvram")), 1, 2);
        features->addWidget(featureCheck(QStringLiteral("CDTV-CR"), QStringLiteral("cdtv-cr")), 2, 2);
        features->addWidget(featureCheck(QStringLiteral("PCMCIA"), QStringLiteral("pcmcia")), 3, 2);
        features->addWidget(featureCheck(QStringLiteral("Composite color burst"), QStringLiteral("color_burst")), 4, 2);
        features->addWidget(advancedCia391078, 5, 2);
        features->addWidget(featureCheck(QStringLiteral("Power up memory pattern"), QStringLiteral("memory_pattern")), 6, 2);
        root->addWidget(groupBox(QStringLiteral("Chipset Features"), features));

        advancedUnmappedAddress = combo(configChoiceDisplays(unmappedAddressChoices, int(sizeof(unmappedAddressChoices) / sizeof(unmappedAddressChoices[0]))), QStringLiteral("Floating"));
        advancedCiaSync = combo(configChoiceDisplays(ciaSyncChoices, int(sizeof(ciaSyncChoices) / sizeof(ciaSyncChoices[0]))), QStringLiteral("Autoselect"));
        advancedScsiA3000 = new QCheckBox(QStringLiteral("A3000 WD33C93 SCSI"));
        advancedScsiA4000T = new QCheckBox(QStringLiteral("A4000T NCR53C710 SCSI"));
        QGridLayout *lowLevel = new QGridLayout;
        lowLevel->setColumnStretch(1, 1);
        lowLevel->setColumnStretch(3, 1);
        lowLevel->addWidget(label(QStringLiteral("Unmapped address space:")), 0, 0);
        lowLevel->addWidget(advancedUnmappedAddress, 0, 1);
        lowLevel->addWidget(label(QStringLiteral("CIA E-Clock Sync:")), 0, 2);
        lowLevel->addWidget(advancedCiaSync, 0, 3);
        lowLevel->addWidget(label(QStringLiteral("Internal SCSI Hardware:")), 1, 0);
        lowLevel->addWidget(advancedScsiA3000, 1, 1);
        lowLevel->addWidget(advancedScsiA4000T, 1, 2, 1, 2);
        root->addWidget(groupBox(QStringLiteral("Low Level"), lowLevel));

        advancedRamsey = new QCheckBox(QStringLiteral("Ramsey revision:"));
        advancedRamseyRevision = new QLineEdit;
        advancedRamseyRevision->setFixedWidth(45);
        advancedFatGary = new QCheckBox(QStringLiteral("Fat Gary revision:"));
        advancedFatGaryRevision = new QLineEdit;
        advancedFatGaryRevision->setFixedWidth(45);
        QRegularExpressionValidator *hexByte = new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9A-Fa-f]{0,2}")), this);
        advancedRamseyRevision->setValidator(hexByte);
        advancedFatGaryRevision->setValidator(hexByte);
        advancedAgnusModel = combo(configChoiceDisplays(agnusModelChoices, int(sizeof(agnusModelChoices) / sizeof(agnusModelChoices[0]))), QStringLiteral("Auto"));
        advancedAgnusSize = combo(configChoiceDisplays(agnusSizeChoices, int(sizeof(agnusSizeChoices) / sizeof(agnusSizeChoices[0]))), QStringLiteral("Auto"));
        advancedDeniseModel = combo(configChoiceDisplays(deniseModelChoices, int(sizeof(deniseModelChoices) / sizeof(deniseModelChoices[0]))), QStringLiteral("Auto"));
        QGridLayout *revision = new QGridLayout;
        revision->setColumnStretch(5, 1);
        revision->addWidget(advancedRamsey, 0, 0);
        revision->addWidget(advancedRamseyRevision, 0, 1);
        revision->addWidget(label(QStringLiteral("Agnus/Alice model:")), 0, 2);
        revision->addWidget(advancedAgnusModel, 0, 3);
        revision->addWidget(advancedAgnusSize, 0, 4);
        revision->addWidget(advancedFatGary, 1, 0);
        revision->addWidget(advancedFatGaryRevision, 1, 1);
        revision->addWidget(label(QStringLiteral("Denise/Lisa model:")), 1, 2);
        revision->addWidget(advancedDeniseModel, 1, 3, 1, 2);
        root->addWidget(groupBox(QStringLiteral("Chipset Revision"), revision));
        root->addStretch(1);

        QScrollArea *scroll = new QScrollArea;
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setWidget(content);
        outer->addWidget(scroll);

        connect(advancedCompatible, &QCheckBox::toggled, this, [this](bool checked) {
            if (checked && chipsetCompatible->currentText() == QStringLiteral("-")) {
                chipsetCompatible->setCurrentText(QStringLiteral("Generic"));
            } else if (!checked && chipsetCompatible->currentText() != QStringLiteral("-")) {
                chipsetCompatible->setCurrentText(QStringLiteral("-"));
            }
        });
        connect(chipsetCompatible, &QComboBox::currentTextChanged, this, [this](const QString &text) {
            setCheckBoxIfChanged(advancedCompatible, text != QStringLiteral("-"));
            applyAdvancedChipsetPreset(text);
        });
        connect(advancedIdeA600A1200, &QCheckBox::toggled, this, [this](bool checked) {
            if (checked) {
                advancedIdeA4000->setChecked(false);
            }
        });
        connect(advancedIdeA4000, &QCheckBox::toggled, this, [this](bool checked) {
            if (checked) {
                advancedIdeA600A1200->setChecked(false);
            }
        });
        auto updateRevisionState = [this]() {
            advancedRamseyRevision->setEnabled(advancedRamsey->isChecked());
            advancedFatGaryRevision->setEnabled(advancedFatGary->isChecked());
        };
        connect(advancedRamsey, &QCheckBox::toggled, this, updateRevisionState);
        connect(advancedFatGary, &QCheckBox::toggled, this, updateRevisionState);

        applyAdvancedChipsetPreset(chipsetCompatible->currentText());
        updateRevisionState();
        return page;
    }

    QWidget *makeRomPage()
    {
        QWidget *page = makePage();
        QVBoxLayout *root = new QVBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);
        root->setSpacing(6);

        romFile = pathCombo();
        extendedRomFile = pathCombo();
        mapRom = new QCheckBox(QStringLiteral("MapROM emulation"));
        kickShifter = new QCheckBox(QStringLiteral("ShapeShifter support"));
        QVBoxLayout *system = new QVBoxLayout;
        system->setSpacing(5);

        QPushButton *romBrowse = smallButton(QStringLiteral("..."));
        QHBoxLayout *romRow = new QHBoxLayout;
        romRow->setContentsMargins(0, 0, 0, 0);
        romRow->addWidget(romFile, 1);
        romRow->addWidget(romBrowse);
        system->addWidget(new QLabel(QStringLiteral("Main ROM file:")));
        system->addLayout(romRow);
        connect(romBrowse, &QPushButton::clicked, this, [this]() {
            addBrowse(romFile, this, QStringLiteral("Select main ROM file"), QStringLiteral("ROM files (*.rom *.bin);;All files (*)"));
        });

        QPushButton *extendedRomBrowse = smallButton(QStringLiteral("..."));
        QHBoxLayout *extendedRomRow = new QHBoxLayout;
        extendedRomRow->setContentsMargins(0, 0, 0, 0);
        extendedRomRow->addWidget(extendedRomFile, 1);
        extendedRomRow->addWidget(extendedRomBrowse);
        system->addWidget(new QLabel(QStringLiteral("Extended ROM file:")));
        system->addLayout(extendedRomRow);
        connect(extendedRomBrowse, &QPushButton::clicked, this, [this]() {
            addBrowse(extendedRomFile, this, QStringLiteral("Select extended ROM file"), QStringLiteral("ROM files (*.rom *.bin);;All files (*)"));
        });

        QHBoxLayout *romOptions = new QHBoxLayout;
        romOptions->setContentsMargins(0, 2, 0, 0);
        romOptions->addStretch();
        romOptions->addWidget(mapRom);
        romOptions->addWidget(kickShifter);
        romOptions->addStretch();
        system->addLayout(romOptions);
        root->addWidget(groupBox(QStringLiteral("System ROM Settings"), system));

        QGridLayout *advanced = new QGridLayout;
        customRomSelect = combo({});
        for (int i = 0; i < MaxRomBoards; i++) {
            customRomSelect->addItem(QStringLiteral("ROM #%1").arg(i + 1));
        }
        customRomStart = new QLineEdit;
        customRomEnd = new QLineEdit;
        customRomFile = new QLineEdit;
        QPushButton *customRomBrowse = smallButton(QStringLiteral("..."));
        advanced->setColumnStretch(3, 1);
        advanced->addWidget(customRomSelect, 0, 0);
        advanced->addWidget(label(QStringLiteral("Address range")), 0, 1);
        advanced->addWidget(customRomStart, 0, 2);
        advanced->addWidget(customRomEnd, 0, 3);
        advanced->addWidget(customRomFile, 1, 0, 1, 4);
        advanced->addWidget(customRomBrowse, 1, 4);
        root->addWidget(groupBox(QStringLiteral("Advanced Custom ROM Settings"), advanced));

        cartFile = pathCombo();
        flashFile = new QLineEdit;
        rtcFile = new QLineEdit;
        QGridLayout *misc = new QGridLayout;
        misc->setColumnStretch(1, 1);
        addPathRow(misc, 0, QStringLiteral("Cartridge ROM file:"), cartFile, QStringLiteral("Select cartridge ROM file"), QStringLiteral("ROM files (*.rom *.bin);;All files (*)"));
        addLineBrowseRow(misc, 2, QStringLiteral("Flash RAM or A2286/A2386SX BIOS CMOS RAM file:"), flashFile);
        addLineBrowseRow(misc, 3, QStringLiteral("Real Time Clock file"), rtcFile);
        root->addWidget(groupBox(QStringLiteral("Miscellaneous"), misc), 1);

        uaeBoardType = combo({
            QStringLiteral("ROM disabled"),
            QStringLiteral("Original UAE (FS + F0 ROM)"),
            QStringLiteral("New UAE (64k + F0 ROM)"),
            QStringLiteral("New UAE (128k, ROM, Direct)"),
            QStringLiteral("New UAE (128k, ROM, Indirect)")
        }, QStringLiteral("Original UAE (FS + F0 ROM)"));
        QGridLayout *uaeBoard = new QGridLayout;
        uaeBoard->setColumnStretch(1, 1);
        uaeBoard->addWidget(label(QStringLiteral("Board type:")), 0, 0);
        uaeBoard->addWidget(uaeBoardType, 0, 1);
        root->addWidget(groupBox(QStringLiteral("Advanced UAE expansion board/Boot ROM Settings"), uaeBoard));

        connect(customRomSelect, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
            if (customRomUpdating) {
                return;
            }
            storeCurrentCustomRomBoard();
            currentCustomRomBoard = qBound(0, index, MaxRomBoards - 1);
            loadCurrentCustomRomBoard();
        });
        connect(customRomStart, &QLineEdit::textChanged, this, [this](const QString &) { storeCurrentCustomRomBoard(); });
        connect(customRomEnd, &QLineEdit::textChanged, this, [this](const QString &) { storeCurrentCustomRomBoard(); });
        connect(customRomFile, &QLineEdit::textChanged, this, [this](const QString &) { storeCurrentCustomRomBoard(); });
        connect(customRomBrowse, &QPushButton::clicked, this, [this]() {
            const QString selected = QFileDialog::getOpenFileName(this, QStringLiteral("Select custom ROM file"), fileDialogInitialPath(customRomFile->text()), QStringLiteral("ROM files (*.rom *.bin);;All files (*)"));
            if (selected.isEmpty()) {
                return;
            }
            customRomFile->setText(selected);
            bool ok = false;
            QString startText = customRomStart->text().trimmed();
            if (startText.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
                startText = startText.mid(2);
            }
            const quint32 start = startText.toUInt(&ok, 16);
            if (ok && customRomEnd->text().trimmed().isEmpty()) {
                const qint64 size = QFileInfo(selected).size();
                if (size > 0) {
                    const quint32 end = ((start + quint32(size) - 1) & ~quint32(0xffff)) | quint32(0xffff);
                    customRomEnd->setText(QStringLiteral("%1").arg(end, 8, 16, QLatin1Char('0')));
                }
            }
            storeCurrentCustomRomBoard();
        });
        clearCustomRomBoards();
        return page;
    }

    QWidget *makeMemoryPage()
    {
        QWidget *page = makePage();
        QVBoxLayout *root = new QVBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);

        chipMem = combo({ QStringLiteral("512 KB"), QStringLiteral("1 MB"), QStringLiteral("2 MB"), QStringLiteral("4 MB"), QStringLiteral("8 MB") }, QStringLiteral("2 MB"));
        z2Fast = combo({ QStringLiteral("None"), QStringLiteral("1 MB"), QStringLiteral("2 MB"), QStringLiteral("4 MB"), QStringLiteral("8 MB"), QStringLiteral("16 MB") });
        slowMem = combo({ QStringLiteral("None"), QStringLiteral("512 KB"), QStringLiteral("1 MB"), QStringLiteral("1.5 MB"), QStringLiteral("1.8 MB") });
        z3Fast = combo({ QStringLiteral("None"), QStringLiteral("1 MB"), QStringLiteral("2 MB"), QStringLiteral("4 MB"), QStringLiteral("8 MB"), QStringLiteral("16 MB"), QStringLiteral("32 MB"), QStringLiteral("64 MB"), QStringLiteral("128 MB"), QStringLiteral("256 MB"), QStringLiteral("512 MB"), QStringLiteral("1024 MB") });
        z3ChipMem = combo({ QStringLiteral("None"), QStringLiteral("16 MB"), QStringLiteral("32 MB"), QStringLiteral("64 MB"), QStringLiteral("128 MB"), QStringLiteral("256 MB"), QStringLiteral("384 MB"), QStringLiteral("512 MB"), QStringLiteral("768 MB"), QStringLiteral("1024 MB") });
        processorSlotMem = combo({ QStringLiteral("None"), QStringLiteral("1 MB"), QStringLiteral("2 MB"), QStringLiteral("4 MB"), QStringLiteral("8 MB"), QStringLiteral("16 MB"), QStringLiteral("32 MB"), QStringLiteral("64 MB"), QStringLiteral("128 MB") });

        QGridLayout *settings = new QGridLayout;
        settings->setColumnStretch(1, 1);
        settings->setColumnStretch(3, 1);
        settings->addWidget(label(QStringLiteral("Chip:")), 0, 0);
        settings->addWidget(chipMem, 0, 1);
        settings->addWidget(label(QStringLiteral("Slow:")), 0, 2);
        settings->addWidget(slowMem, 0, 3);
        settings->addWidget(label(QStringLiteral("Z2 Fast:")), 1, 0);
        settings->addWidget(z2Fast, 1, 1);
        settings->addWidget(label(QStringLiteral("Z3 Fast:")), 1, 2);
        settings->addWidget(z3Fast, 1, 3);
        settings->addWidget(label(QStringLiteral("Processor slot:")), 2, 0);
        settings->addWidget(processorSlotMem, 2, 1);
        settings->addWidget(label(QStringLiteral("32-bit Chip:")), 2, 2);
        settings->addWidget(z3ChipMem, 2, 3);
        root->addWidget(groupBox(QStringLiteral("Memory Settings"), settings));

        QGridLayout *advanced = new QGridLayout;
        z3Mapping = combo(configChoiceDisplays(z3MappingChoices, int(sizeof(z3MappingChoices) / sizeof(z3MappingChoices[0]))));
        advanced->setColumnStretch(1, 1);
        advanced->setColumnStretch(3, 1);
        advanced->addWidget(combo({ QStringLiteral("None"), QStringLiteral("Custom memory") }), 0, 0, 1, 2);
        advanced->addWidget(label(QStringLiteral("Z3 mapping mode:")), 0, 2);
        advanced->addWidget(z3Mapping, 0, 3);
        advanced->addWidget(label(QStringLiteral("Manufacturer")), 1, 0);
        advanced->addWidget(new QLineEdit, 1, 1);
        advanced->addWidget(label(QStringLiteral("Product")), 1, 2);
        advanced->addWidget(new QLineEdit, 1, 3);
        advanced->addWidget(new QCheckBox(QStringLiteral("Manual configuration")), 2, 1);
        advanced->addWidget(new QCheckBox(QStringLiteral("DMA Capable")), 2, 2);
        root->addWidget(groupBox(QStringLiteral("Advanced Memory Settings"), advanced), 1);
        return page;
    }

    QWidget *makeFloppyPage()
    {
        QWidget *page = makePage();
        QVBoxLayout *root = new QVBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);
        QVBoxLayout *drives = new QVBoxLayout;
        drives->setSpacing(10);
        for (int i = 0; i < 4; i++) {
            addFloppyRow(drives, i);
        }
        drives->addStretch();
        root->addWidget(groupBox(QStringLiteral("Floppy Drives"), drives), 1);
        floppySpeed = new QSlider(Qt::Horizontal);
        floppySpeed->setRange(0, 4);
        floppySpeed->setTickInterval(1);
        floppySpeed->setTickPosition(QSlider::TicksAbove);
        floppySpeedLabel = new QLineEdit;
        floppySpeedLabel->setReadOnly(true);
        floppySpeedLabel->setAlignment(Qt::AlignCenter);
        floppySpeedLabel->setMinimumWidth(130);
        QGridLayout *speed = new QGridLayout;
        speed->addWidget(floppySpeed, 0, 0);
        speed->addWidget(floppySpeedLabel, 0, 1);
        root->addWidget(groupBox(QStringLiteral("Floppy Drive Emulation Speed"), speed));
        connect(floppySpeed, &QSlider::valueChanged, this, [this]() {
            updateFloppySpeedLabel();
        });
        return page;
    }

    void addFloppyRow(QVBoxLayout *layout, int drive)
    {
        dfEnable[drive] = new QCheckBox(QStringLiteral("DF%1:").arg(drive));
        dfType[drive] = combo(floppyTypeItems(drive, false));
        dfPath[drive] = pathCombo();
        dfWriteProtect[drive] = new QCheckBox;
        QPushButton *info = smallButton(QStringLiteral("?"));
        QPushButton *eject = new QPushButton(QStringLiteral("Eject"));
        QPushButton *browse = smallButton(QStringLiteral("..."));

        QWidget *driveWidget = new QWidget;
        QVBoxLayout *driveLayout = new QVBoxLayout(driveWidget);
        driveLayout->setContentsMargins(0, 0, 0, 0);
        driveLayout->setSpacing(4);

        QHBoxLayout *controls = new QHBoxLayout;
        controls->setContentsMargins(0, 0, 0, 0);
        controls->setSpacing(6);
        controls->addWidget(dfEnable[drive]);
        controls->addWidget(dfType[drive]);
        controls->addWidget(new QLabel(QStringLiteral("Write-protected")));
        controls->addWidget(dfWriteProtect[drive]);
        controls->addWidget(info);
        controls->addWidget(eject);
        controls->addWidget(browse);
        controls->addStretch();
        driveLayout->addLayout(controls);
        driveLayout->addWidget(dfPath[drive]);
        layout->addWidget(driveWidget);
        dfEnable[drive]->setChecked(drive == 0);
        connect(info, &QPushButton::clicked, this, [this, drive]() {
            showFloppyInfo(dfPath[drive] ? dfPath[drive]->currentText() : QString(), drive);
        });
        connect(browse, &QPushButton::clicked, this, [this, drive]() {
            addBrowse(dfPath[drive], this, QStringLiteral("Select floppy image"), floppyDiskImageFilter());
        });
        connect(eject, &QPushButton::clicked, this, [this, drive]() {
            dfPath[drive]->setCurrentText(QString());
        });
        connect(dfPath[drive], &QComboBox::currentTextChanged, this, [this](const QString &) {
            updateDiskSwapperDriveColumn();
        });
        if (drive < 2 && quickDfPath[drive]) {
            connect(dfPath[drive], &QComboBox::currentTextChanged, this, [this, drive](const QString &text) {
                if (quickDfPath[drive]->currentText() != text) {
                    setPathComboText(quickDfPath[drive], text);
                }
            });
            connect(quickDfPath[drive], &QComboBox::currentTextChanged, this, [this, drive](const QString &text) {
                if (dfPath[drive]->currentText() != text) {
                    setPathComboText(dfPath[drive], text);
                }
            });
            connect(dfEnable[drive], &QCheckBox::toggled, quickDfEnable[drive], &QCheckBox::setChecked);
            connect(quickDfEnable[drive], &QCheckBox::toggled, dfEnable[drive], &QCheckBox::setChecked);
            connect(dfType[drive], QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, drive]() {
                syncFloppyDriveToQuick(drive);
            });
            connect(quickDfType[drive], QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, drive]() {
                syncQuickDriveToFloppy(drive);
            });
            connect(dfWriteProtect[drive], &QCheckBox::toggled, this, [this, drive]() {
                syncFloppyDriveToQuick(drive);
            });
            connect(quickDfWriteProtect[drive], &QCheckBox::toggled, this, [this, drive]() {
                syncQuickDriveToFloppy(drive);
            });
        }
    }

    QString floppyInfoText(const QString &path, int drive) const
    {
        QStringList lines;
        lines << QStringLiteral("'%1'").arg(path);

        QFileInfo info(path);
        if (!info.exists()) {
            lines << QStringLiteral("Disk readable: No");
            lines << QStringLiteral("Error: File does not exist");
            return lines.join(QLatin1Char('\n'));
        }

        lines << QStringLiteral("Drive: DF%1").arg(drive);
        lines << QStringLiteral("Size: %1").arg(formatByteSize(info.size()));
        lines << QStringLiteral("Modified: %1").arg(QLocale().toString(info.lastModified(), QLocale::ShortFormat));

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            lines << QStringLiteral("Disk readable: No");
            lines << QStringLiteral("Error: %1").arg(file.errorString());
            return lines.join(QLatin1Char('\n'));
        }

        lines << QStringLiteral("Disk readable: Yes");

        quint32 imageCrc = 0;
        QString imageCrcError;
        if (crc32ForFile(path, &imageCrc, &imageCrcError)) {
            lines << QStringLiteral("Disk CRC32: %1").arg(hex32(imageCrc));
        } else {
            lines << QStringLiteral("Disk CRC32: unavailable (%1)").arg(imageCrcError);
        }

        if (!isPlainSectorFloppyImage(info)) {
            lines << QStringLiteral("Boot block: unsupported image container");
            return lines.join(QLatin1Char('\n'));
        }

        const QByteArray bootBlock = file.read(1024);
        if (bootBlock.size() < 1024) {
            lines << QStringLiteral("Boot block: unavailable");
            return lines.join(QLatin1Char('\n'));
        }

        lines << QStringLiteral("Boot block CRC32: %1").arg(hex32(crc32ForBytes(bootBlock)));
        lines << QStringLiteral("Boot block checksum valid: %1").arg(amigaBootBlockChecksumValid(bootBlock) ? QStringLiteral("Yes") : QStringLiteral("No"));
        lines << QStringLiteral("Boot block type: %1").arg(amigaBootBlockType(bootBlock));

        const quint32 rootBlock = readBe32(bootBlock, 8);
        if (rootBlock > 0 && file.seek(qint64(rootBlock) * 512)) {
            const QString label = amigaRootBlockLabel(file.read(512));
            if (!label.isEmpty()) {
                lines << QStringLiteral("Label: '%1'").arg(label);
            }
        }

        lines << QString();
        for (int i = 0; i < bootBlock.size(); i += 32) {
            QString hex;
            QString ascii;
            for (int j = 0; j < 32; j++) {
                const int index = i + j;
                if (index >= bootBlock.size()) {
                    hex += QStringLiteral("  ");
                    ascii += QLatin1Char(' ');
                    continue;
                }
                const quint8 value = quint8(bootBlock[index]);
                hex += QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
                ascii += (value >= 32 && value < 127) ? QChar(ushort(value)) : QLatin1Char('.');
            }
            lines << QStringLiteral("%1 %2").arg(hex, -64).arg(ascii);
        }

        return lines.join(QLatin1Char('\n'));
    }

    void showFloppyInfo(const QString &rawPath, int drive)
    {
        const QString path = rawPath.trimmed();
        if (path.isEmpty()) {
            QMessageBox::information(this, windowTitle(), QStringLiteral("No disk image is selected for DF%1.").arg(drive));
            return;
        }

        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Disk information"));
        dialog.resize(760, 540);

        QVBoxLayout *layout = new QVBoxLayout(&dialog);
        QPlainTextEdit *text = new QPlainTextEdit(&dialog);
        text->setReadOnly(true);
        text->setLineWrapMode(QPlainTextEdit::NoWrap);
        text->setPlainText(floppyInfoText(path, drive));
        QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        if (fixed.pointSize() > 0) {
            fixed.setPointSize(fixed.pointSize() + 1);
        }
        text->setFont(fixed);
        layout->addWidget(text, 1);

        QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);

        dialog.exec();
    }

    QWidget *makeDiskSwapperPage()
    {
        QWidget *page = makePage();
        QVBoxLayout *root = new QVBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);
        diskSwapperList = new QTableWidget(MaxDiskSwapperSlots, 3);
        diskSwapperList->setSelectionBehavior(QAbstractItemView::SelectRows);
        diskSwapperList->setSelectionMode(QAbstractItemView::SingleSelection);
        diskSwapperList->setEditTriggers(QAbstractItemView::NoEditTriggers);
        diskSwapperList->verticalHeader()->hide();
        diskSwapperList->setHorizontalHeaderLabels({ QStringLiteral("#"), QStringLiteral("Disk image"), QStringLiteral("Drive") });
        diskSwapperList->horizontalHeader()->setStretchLastSection(false);
        diskSwapperList->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        diskSwapperList->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        diskSwapperList->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        for (int row = 0; row < MaxDiskSwapperSlots; row++) {
            QTableWidgetItem *slot = new QTableWidgetItem(QString::number(row + 1));
            slot->setFlags(slot->flags() & ~Qt::ItemIsEditable);
            diskSwapperList->setItem(row, 0, slot);
            setDiskSwapperPath(row, QString());
        }
        root->addWidget(diskSwapperList, 1);

        QHBoxLayout *pathRow = new QHBoxLayout;
        diskSwapperPath = pathCombo();
        QPushButton *browse = smallButton(QStringLiteral("..."));
        pathRow->addWidget(diskSwapperPath, 1);
        pathRow->addWidget(browse);
        root->addLayout(pathRow);

        QHBoxLayout *buttons = new QHBoxLayout;
        diskSwapperInsertButton = new QPushButton(QStringLiteral("Insert floppy disk image"));
        diskSwapperRemoveButton = new QPushButton(QStringLiteral("Remove floppy disk image"));
        diskSwapperRemoveAllButton = new QPushButton(QStringLiteral("Remove all"));
        buttons->addWidget(diskSwapperInsertButton);
        buttons->addWidget(diskSwapperRemoveButton);
        buttons->addWidget(diskSwapperRemoveAllButton);
        root->addLayout(buttons);

        connect(diskSwapperList->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this](const QModelIndex &, const QModelIndex &) {
            updateDiskSwapperSelection();
        });
        connect(diskSwapperList, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
            if (column == 1) {
                browseDiskSwapperImage(row);
            }
        });
        connect(browse, &QPushButton::clicked, this, [this]() {
            browseDiskSwapperImage(selectedDiskSwapperSlot());
        });
        connect(diskSwapperInsertButton, &QPushButton::clicked, this, [this]() {
            setDiskSwapperPath(selectedDiskSwapperSlot(), diskSwapperPath->currentText().trimmed());
        });
        connect(diskSwapperRemoveButton, &QPushButton::clicked, this, [this]() {
            setDiskSwapperPath(selectedDiskSwapperSlot(), QString());
            diskSwapperPath->clearEditText();
        });
        connect(diskSwapperRemoveAllButton, &QPushButton::clicked, this, [this]() {
            for (int row = 0; row < MaxDiskSwapperSlots; row++) {
                setDiskSwapperPath(row, QString());
            }
            diskSwapperPath->clearEditText();
        });
        diskSwapperList->selectRow(0);
        updateDiskSwapperSelection();
        return page;
    }

    int selectedDiskSwapperSlot() const
    {
        if (!diskSwapperList || diskSwapperList->currentRow() < 0) {
            return 0;
        }
        return qBound(0, diskSwapperList->currentRow(), MaxDiskSwapperSlots - 1);
    }

    QString diskSwapperPathAt(int slot) const
    {
        if (!diskSwapperList || slot < 0 || slot >= MaxDiskSwapperSlots) {
            return QString();
        }
        QTableWidgetItem *item = diskSwapperList->item(slot, 1);
        return item ? item->data(Qt::UserRole).toString() : QString();
    }

    void setDiskSwapperPath(int slot, const QString &path)
    {
        if (!diskSwapperList || slot < 0 || slot >= MaxDiskSwapperSlots) {
            return;
        }
        const QString trimmed = path.trimmed();
        QTableWidgetItem *image = diskSwapperList->item(slot, 1);
        if (!image) {
            image = new QTableWidgetItem;
            image->setFlags(image->flags() & ~Qt::ItemIsEditable);
            diskSwapperList->setItem(slot, 1, image);
        }
        image->setData(Qt::UserRole, trimmed);
        image->setText(trimmed.isEmpty() ? QString() : QFileInfo(trimmed).fileName());
        image->setToolTip(trimmed);
        QTableWidgetItem *drive = diskSwapperList->item(slot, 2);
        if (!drive) {
            drive = new QTableWidgetItem;
            drive->setFlags(drive->flags() & ~Qt::ItemIsEditable);
            diskSwapperList->setItem(slot, 2, drive);
        }
        updateDiskSwapperDriveColumn();
    }

    void updateDiskSwapperSelection()
    {
        if (!diskSwapperPath) {
            return;
        }
        diskSwapperPath->setCurrentText(diskSwapperPathAt(selectedDiskSwapperSlot()));
    }

    void updateDiskSwapperDriveColumn()
    {
        if (!diskSwapperList) {
            return;
        }
        for (int slot = 0; slot < MaxDiskSwapperSlots; slot++) {
            QTableWidgetItem *drive = diskSwapperList->item(slot, 2);
            if (!drive) {
                continue;
            }
            QString driveText;
            const QString image = diskSwapperPathAt(slot);
            if (!image.isEmpty()) {
                for (int i = 0; i < 4; i++) {
                    if (dfPath[i] && dfPath[i]->currentText() == image) {
                        driveText = QStringLiteral("DF%1:").arg(i);
                        break;
                    }
                }
            }
            drive->setText(driveText);
        }
    }

    void browseDiskSwapperImage(int slot)
    {
        const QString selected = QFileDialog::getOpenFileName(this, QStringLiteral("Select floppy image"), fileDialogInitialPath(diskSwapperPathAt(slot)), floppyDiskImageFilter());
        if (!selected.isEmpty()) {
            setPathComboText(diskSwapperPath, selected);
            setDiskSwapperPath(slot, selected);
        }
    }

    QWidget *makeHardDrivesPage()
    {
        QWidget *page = makePage();
        QVBoxLayout *root = new QVBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);
        mountedDrives = new QTreeWidget;
        mountedDrives->setRootIsDecorated(false);
        mountedDrives->setSelectionMode(QAbstractItemView::SingleSelection);
        mountedDrives->setDragDropMode(QAbstractItemView::InternalMove);
        mountedDrives->setDragEnabled(true);
        mountedDrives->setAcceptDrops(true);
        mountedDrives->viewport()->setAcceptDrops(true);
        mountedDrives->setDefaultDropAction(Qt::MoveAction);
        mountedDrives->setDropIndicatorShown(true);
        mountedDrives->setHeaderLabels({
            QStringLiteral("*"),
            QStringLiteral("Device"),
            QStringLiteral("Volume"),
            QStringLiteral("Path"),
            QStringLiteral("RW"),
            QStringLiteral("Block size"),
            QStringLiteral("Size"),
            QStringLiteral("BootPri")
        });
        QVBoxLayout *volumeLayout = new QVBoxLayout;
        volumeLayout->addWidget(mountedDrives);
        root->addWidget(groupBox(QStringLiteral("Mounted drives"), volumeLayout), 1);
        QGridLayout *buttons = new QGridLayout;
        addDirectoryMountButton = new QPushButton(QStringLiteral("Add Directory or Archive..."));
        addHardfileMountButton = new QPushButton(QStringLiteral("Add Hardfile..."));
        addHardDriveMountButton = new QPushButton(QStringLiteral("Add Hard Drive..."));
        addCdMountButton = new QPushButton(QStringLiteral("Add SCSI/IDE CD Drive"));
        addTapeMountButton = new QPushButton(QStringLiteral("Add SCSI/IDE Tape Drive"));
        propertiesMountButton = new QPushButton(QStringLiteral("Properties"));
        removeMountButton = new QPushButton(QStringLiteral("Remove"));
        buttons->setColumnStretch(4, 1);
        buttons->addWidget(addDirectoryMountButton, 0, 0);
        buttons->addWidget(addHardfileMountButton, 0, 1);
        buttons->addWidget(addHardDriveMountButton, 0, 2);
        buttons->addWidget(addCdMountButton, 1, 0);
        buttons->addWidget(addTapeMountButton, 1, 1);
        buttons->addWidget(propertiesMountButton, 1, 2);
        buttons->addWidget(removeMountButton, 1, 3);
        root->addLayout(buttons);

        hostDriveAutomount = new QCheckBox(QStringLiteral("Add PC drives at startup"));
        hostRemovableDrives = new QCheckBox(QStringLiteral("Include removable drives.."));
        hostNetworkDrives = new QCheckBox(QStringLiteral("Include network drives.."));
        hostCdDrives = new QCheckBox(QStringLiteral("CDFS automount CD/DVD drives"));
        filesysNoFsdb = new QCheckBox(QStringLiteral("Disable UAEFSDB-support"));
        hostNoRecycleBin = new QCheckBox(QStringLiteral("Don't use Windows Recycle Bin"));
        hostAutomountRemovable = new QCheckBox(QStringLiteral("Automount removable drives"));
        filesysLimitSize = new QCheckBox(QStringLiteral("Limit size of directory drives to 1G"));
        for (QCheckBox *check : { hostDriveAutomount, hostRemovableDrives, hostNetworkDrives, hostCdDrives, hostNoRecycleBin, hostAutomountRemovable }) {
            check->setEnabled(false);
            check->setToolTip(QStringLiteral("Windows host-drive automount option; Unix backend not implemented yet."));
        }
        filesysLimitSize->setToolTip(QStringLiteral("Workaround for old installers that calculate free space incorrectly if a directory drive is large."));
        QGridLayout *options = new QGridLayout;
        options->setColumnStretch(0, 1);
        options->setColumnStretch(1, 1);
        options->addWidget(hostDriveAutomount, 0, 0);
        options->addWidget(hostRemovableDrives, 1, 0);
        options->addWidget(hostNetworkDrives, 2, 0);
        options->addWidget(hostCdDrives, 3, 0);
        options->addWidget(filesysNoFsdb, 0, 1);
        options->addWidget(hostNoRecycleBin, 1, 1);
        options->addWidget(hostAutomountRemovable, 2, 1);
        options->addWidget(filesysLimitSize, 3, 1);
        root->addWidget(groupBox(QStringLiteral("Options"), options));

        cdSlotNumber = combo({});
        for (int i = 0; i < MaxCdSlots; i++) {
            cdSlotNumber->addItem(QString::number(i + 1));
        }
        cdSlotPath = pathCombo();
        cdSlotType = combo({ QStringLiteral("Autodetect"), QStringLiteral("Image file") }, QStringLiteral("Image file"));
        QPushButton *selectCdImage = new QPushButton(QStringLiteral("Select image file"));
        QPushButton *ejectCd = new QPushButton(QStringLiteral("Eject"));
        cdSpeedTurbo = new QCheckBox(QStringLiteral("CDTV/CDTV-CR/CD32 turbo CD read speed"));

        QGridLayout *optical = new QGridLayout;
        optical->setColumnStretch(1, 1);
        optical->addWidget(new QLabel(QStringLiteral("CD drive/image")), 0, 0);
        optical->addWidget(selectCdImage, 0, 2);
        optical->addWidget(cdSlotType, 0, 3);
        optical->addWidget(ejectCd, 0, 4);
        optical->addWidget(cdSlotNumber, 1, 0);
        optical->addWidget(cdSlotPath, 1, 1, 1, 4);
        optical->addWidget(cdSpeedTurbo, 2, 1, 1, 4);
        root->addWidget(groupBox(QStringLiteral("Optical media options"), optical));

        connect(addDirectoryMountButton, &QPushButton::clicked, this, [this]() { addDirectoryMountDialog(); });
        connect(addHardfileMountButton, &QPushButton::clicked, this, [this]() { addHardfileMountDialog(); });
        connect(addHardDriveMountButton, &QPushButton::clicked, this, [this]() { addHardDriveMountDialog(); });
        connect(addCdMountButton, &QPushButton::clicked, this, [this]() { addCdDriveMountDialog(); });
        connect(addTapeMountButton, &QPushButton::clicked, this, [this]() { addTapeDriveMountDialog(); });
        connect(cdSlotNumber, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
            if (cdSlotUpdating || index < 0 || index >= MaxCdSlots) {
                return;
            }
            storeCurrentCdSlotFromUi();
            currentCdSlot = index;
            loadCdSlotToUi(index);
        });
        connect(cdSlotType, &QComboBox::currentTextChanged, this, [this](const QString &text) {
            if (cdSlotUpdating) {
                return;
            }
            if (text == QStringLiteral("Autodetect") && cdSlotPath->currentText().isEmpty()) {
                setCurrentCdSlotInUse(true);
            }
        });
        connect(cdSlotPath, &QComboBox::currentTextChanged, this, [this](const QString &text) {
            if (cdSlotUpdating) {
                return;
            }
            if (!text.isEmpty()) {
                setComboTextIfChanged(cdSlotType, QStringLiteral("Image file"));
                setCurrentCdSlotInUse(true);
            }
        });
        connect(selectCdImage, &QPushButton::clicked, this, [this]() {
            const QString initialPath = fileDialogInitialPath(cdSlotPath->currentText());
            const QString selected = QFileDialog::getOpenFileName(this, QStringLiteral("Select CD image"), initialPath, cdImageFilter());
            if (!selected.isEmpty()) {
                setPathComboText(cdSlotPath, selected);
                setComboTextIfChanged(cdSlotType, QStringLiteral("Image file"));
                setCurrentCdSlotInUse(true);
            }
        });
        connect(ejectCd, &QPushButton::clicked, this, [this]() {
            cdSlotPath->setCurrentText(QString());
            setComboTextIfChanged(cdSlotType, QStringLiteral("Image file"));
            setCurrentCdSlotInUse(false);
        });
        connect(propertiesMountButton, &QPushButton::clicked, this, [this]() { openSelectedMountProperties(); });
        connect(removeMountButton, &QPushButton::clicked, this, [this]() { removeSelectedMount(); });
        connect(mountedDrives, &QTreeWidget::itemSelectionChanged, this, [this]() { updateMountButtons(); });
        connect(mountedDrives, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *, int) { openSelectedMountProperties(); });
        QShortcut *propertiesShortcut = new QShortcut(QKeySequence(Qt::Key_Return), mountedDrives);
        connect(propertiesShortcut, &QShortcut::activated, this, [this]() { openSelectedMountProperties(); });
        QShortcut *propertiesEnterShortcut = new QShortcut(QKeySequence(Qt::Key_Enter), mountedDrives);
        connect(propertiesEnterShortcut, &QShortcut::activated, this, [this]() { openSelectedMountProperties(); });
        QShortcut *removeShortcut = new QShortcut(QKeySequence::Delete, mountedDrives);
        connect(removeShortcut, &QShortcut::activated, this, [this]() { removeSelectedMount(); });
        QShortcut *moveUpShortcut = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Up), mountedDrives);
        connect(moveUpShortcut, &QShortcut::activated, this, [this]() { moveSelectedMount(-1); });
        QShortcut *moveDownShortcut = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Down), mountedDrives);
        connect(moveDownShortcut, &QShortcut::activated, this, [this]() { moveSelectedMount(1); });
        updateMountButtons();
        return page;
    }

    QWidget *makeExpansionPage()
    {
        QWidget *page = makePage();
        QVBoxLayout *root = new QVBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);

        rtgMem = combo({ QStringLiteral("None"), QStringLiteral("1 MB"), QStringLiteral("2 MB"), QStringLiteral("4 MB"), QStringLiteral("8 MB"), QStringLiteral("16 MB"), QStringLiteral("32 MB"), QStringLiteral("64 MB"), QStringLiteral("128 MB"), QStringLiteral("256 MB") }, QStringLiteral("None"));
        QStringList rtgTypes;
        for (const WinUaeQtRtgBoardCatalogItem &board : boardCatalog.rtgBoards) {
            rtgTypes.append(board.configValue);
        }
        if (rtgTypes.isEmpty()) {
            rtgTypes = { QStringLiteral("ZorroII"), QStringLiteral("ZorroIII") };
        }
        rtgType = combo(rtgTypes, QStringLiteral("ZorroIII"));
        rtgMonitor = combo({ QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4") }, QStringLiteral("1"));
        disableUnavailable(rtgMonitor, QStringLiteral("The Unix uaegfx.card backend currently exposes a single RTG monitor."));
        rtgScale = new QCheckBox(QStringLiteral("Scale if smaller than display size setting"));
        rtgScaleAllow = new QCheckBox(QStringLiteral("Always scale in windowed mode"));
        rtgScaleAllow->setEnabled(false);
        rtgScaleAllow->setToolTip(QStringLiteral("Windows display-backend option; Unix scaling is controlled by the RTG filter settings."));
        rtgCenter = new QCheckBox(QStringLiteral("Always center"));
        rtgIntegerScale = new QCheckBox(QStringLiteral("Integer scaling"));
        rtgMultithread = new QCheckBox(QStringLiteral("Multithreaded"));
        rtgMultithread->setToolTip(QStringLiteral("Render RTG frame conversion on a Unix worker thread."));
        rtgHardwareSprite = new QCheckBox(QStringLiteral("Hardware sprite emulation"));
        rtgHardwareVBlank = new QCheckBox(QStringLiteral("Hardware vertical blank interrupt"));
        rtgAutoswitch = new QCheckBox(QStringLiteral("Native/RTG autoswitch"));
        rtgInitialMonitor = new QCheckBox(QStringLiteral("Override initial native chipset display"));
        disableUnavailable(rtgInitialMonitor, QStringLiteral("Initial RTG monitor override needs multi-monitor RTG support in the Unix backend."));
        rtg8Bit = combo({ QStringLiteral("(8bit)"), QStringLiteral("8-bit (*)") }, QStringLiteral("8-bit (*)"));
        rtg16Bit = combo({ QStringLiteral("(15/16bit)"), QStringLiteral("All 15/16-bit"), QStringLiteral("R5G6B5PC (*)"), QStringLiteral("R5G5B5PC"), QStringLiteral("R5G6B5"), QStringLiteral("R5G5B5"), QStringLiteral("B5G6R5PC"), QStringLiteral("B5G5R5PC") }, QStringLiteral("R5G6B5PC (*)"));
        rtg24Bit = combo({ QStringLiteral("(24bit)"), QStringLiteral("All 24-bit"), QStringLiteral("R8G8B8"), QStringLiteral("B8G8R8") }, QStringLiteral("(24bit)"));
        rtg32Bit = combo({ QStringLiteral("(32bit)"), QStringLiteral("All 32-bit"), QStringLiteral("A8R8G8B8"), QStringLiteral("A8B8G8R8"), QStringLiteral("R8G8B8A8"), QStringLiteral("B8G8R8A8 (*)") }, QStringLiteral("B8G8R8A8 (*)"));
        rtgDisplay = combo({ QStringLiteral("Default display") }, QStringLiteral("Default display"));
        rtgDisplay->setEnabled(false);
        rtgDisplay->setToolTip(QStringLiteral("Use the Display page for the shared host display setting; separate RTG monitor placement is not connected yet."));
        rtgRefreshRate = combo({ QStringLiteral("Chipset"), QStringLiteral("Default"), QStringLiteral("50"), QStringLiteral("60"), QStringLiteral("70"), QStringLiteral("75") }, QStringLiteral("Chipset"));
        rtgRefreshRate->setEditable(true);
        rtgBuffers = combo({ QStringLiteral("Double"), QStringLiteral("Triple") }, QStringLiteral("Double"));
        rtgBuffers->setToolTip(QStringLiteral("Controls the Unix SDL RTG presentation texture queue."));
        rtgAspectRatio = combo(configChoiceDisplays(rtgAspectRatioChoices, int(sizeof(rtgAspectRatioChoices) / sizeof(rtgAspectRatioChoices[0]))), QStringLiteral("Automatic"));

        QGridLayout *rtg = new QGridLayout;
        rtg->setColumnStretch(1, 2);
        rtg->setColumnStretch(3, 1);
        rtg->addWidget(label(QStringLiteral("Board:")), 0, 0);
        rtg->addWidget(rtgType, 0, 1);
        rtg->addWidget(label(QStringLiteral("Monitor:")), 0, 2);
        rtg->addWidget(rtgMonitor, 0, 3);
        rtg->addWidget(label(QStringLiteral("VRAM size:")), 1, 0);
        rtg->addWidget(rtgMem, 1, 1);
        rtg->addWidget(rtgAutoswitch, 2, 0, 1, 2);
        rtg->addWidget(rtgScale, 3, 0, 1, 2);
        rtg->addWidget(rtgScaleAllow, 4, 0, 1, 2);
        rtg->addWidget(rtgCenter, 5, 0, 1, 2);
        rtg->addWidget(rtgIntegerScale, 6, 0, 1, 2);
        rtg->addWidget(rtgMultithread, 3, 2, 1, 2);
        rtg->addWidget(rtgHardwareSprite, 4, 2, 1, 2);
        rtg->addWidget(rtgHardwareVBlank, 5, 2, 1, 2);
        rtg->addWidget(label(QStringLiteral("8-bit:")), 7, 0);
        rtg->addWidget(rtg8Bit, 7, 1);
        rtg->addWidget(label(QStringLiteral("16-bit:")), 7, 2);
        rtg->addWidget(rtg16Bit, 7, 3);
        rtg->addWidget(label(QStringLiteral("24-bit:")), 8, 0);
        rtg->addWidget(rtg24Bit, 8, 1);
        rtg->addWidget(label(QStringLiteral("32-bit:")), 8, 2);
        rtg->addWidget(rtg32Bit, 8, 3);
        rtg->addWidget(rtgDisplay, 9, 0, 1, 4);
        rtg->addWidget(label(QStringLiteral("Refresh rate:")), 10, 0);
        rtg->addWidget(rtgRefreshRate, 10, 1);
        rtg->addWidget(label(QStringLiteral("Buffer mode:")), 10, 2);
        rtg->addWidget(rtgBuffers, 10, 3);
        rtg->addWidget(label(QStringLiteral("Aspect ratio:")), 11, 0);
        rtg->addWidget(rtgAspectRatio, 11, 1);
        rtg->addWidget(rtgInitialMonitor, 11, 2, 1, 2);
        root->addWidget(groupBox(QStringLiteral("RTG Graphics Card"), rtg));

        connect(rtgScale, &QCheckBox::toggled, this, [this](bool checked) {
            if (checked) {
                rtgCenter->setChecked(false);
                rtgIntegerScale->setChecked(false);
            }
        });
        connect(rtgCenter, &QCheckBox::toggled, this, [this](bool checked) {
            if (checked) {
                rtgScale->setChecked(false);
                rtgIntegerScale->setChecked(false);
            }
        });
        connect(rtgIntegerScale, &QCheckBox::toggled, this, [this](bool checked) {
            if (checked) {
                rtgScale->setChecked(false);
                rtgCenter->setChecked(false);
            }
        });
        connect(rtgMem, &QComboBox::currentTextChanged, this, [this]() { updateCpuControlState(); });
        connect(rtgType, &QComboBox::currentTextChanged, this, [this]() { updateCpuControlState(); });
        return page;
    }

    QWidget *makeExpansionsPage()
    {
        QWidget *page = makePage();
        QVBoxLayout *root = new QVBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);

        QGridLayout *board = new QGridLayout;
        expansionRomCategory = combo(expansionBoardCategoryItems(boardCatalog));
        expansionRomBoard = combo({});
        expansionRomBoard->setEditable(false);
        expansionRomBoard->setInsertPolicy(QComboBox::NoInsert);
        expansionRomSubtype = combo({ QStringLiteral("Default") });
        expansionRomSubtype->setEnabled(false);
        expansionRomSlot = combo({ QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4") });
        expansionRomId = combo({ QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7") }, QStringLiteral("7"));
        expansionRomFile = pathCombo();
        expansionBoardOption = combo({ QStringLiteral("None") });
        expansionBoardOption->setEnabled(false);
        expansionBoardSelector = combo({ QStringLiteral("None") });
        expansionBoardSelector->setEnabled(false);
        expansionBoardSelector->setVisible(false);
        expansionBoardString = new QLineEdit;
        expansionBoardString->setEnabled(false);
        expansionBoardString->setVisible(false);
        expansionRom24BitDma = new QCheckBox(QStringLiteral("24-bit DMA"));
        expansionRomEnabled = new QCheckBox(QStringLiteral("Enable board"));
        expansionRomEnabled->setToolTip(QStringLiteral("Adds the selected expansion board to the configuration."));
        expansionRomAutobootDisabled = new QCheckBox(QStringLiteral("Autoboot disabled"));
        expansionRomPcmciaInserted = new QCheckBox(QStringLiteral("PCMCIA inserted"));
        expansionBoardOptionCheck = new QCheckBox(QStringLiteral("Enable option"));
        expansionBoardOptionCheck->setToolTip(QStringLiteral("Applies the selected board option."));
        expansionBoardOptionCheck->setEnabled(false);
        expansionRomBrowse = smallButton(QStringLiteral("..."));
        board->setColumnStretch(2, 1);
        board->addWidget(expansionRomCategory, 0, 0, 1, 2);
        board->addWidget(expansionRom24BitDma, 0, 2);
        board->addWidget(label(QStringLiteral("Controller ID:")), 0, 3);
        board->addWidget(expansionRomId, 0, 4);
        board->addWidget(expansionRomBoard, 1, 0);
        board->addWidget(expansionRomSlot, 1, 1);
        board->addWidget(expansionRomFile, 1, 2, 1, 2);
        board->addWidget(expansionRomBrowse, 1, 4);
        board->addWidget(expansionRomSubtype, 2, 0, 1, 2);
        board->addWidget(expansionRomAutobootDisabled, 2, 2);
        board->addWidget(expansionRomPcmciaInserted, 2, 3, 1, 2);
        board->addWidget(expansionBoardOption, 3, 0, 1, 2);
        board->addWidget(expansionBoardSelector, 3, 2, 1, 2);
        board->addWidget(expansionBoardString, 3, 2, 1, 2);
        board->addWidget(expansionBoardOptionCheck, 3, 2, 1, 2);
        board->addWidget(expansionRomEnabled, 3, 4);
        root->addWidget(groupBox(QStringLiteral("Expansion Board Settings"), board));
        populateExpansionBoardChoices();
        loadExpansionBoardUiState(QString(), 0);

        connect(expansionRomCategory, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
            storeCurrentExpansionBoardUiState();
            populateExpansionBoardChoices();
            loadExpansionBoardUiState(selectedExpansionBoardKey(), selectedExpansionBoardSlot());
        });
        connect(expansionRomBoard, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
            storeCurrentExpansionBoardUiState();
            loadExpansionBoardUiState(selectedExpansionBoardKey(), selectedExpansionBoardSlot());
        });
        connect(expansionRomBoard, &QComboBox::currentTextChanged, this, [this]() {
            if (!expansionBoardUpdating) {
                storeCurrentExpansionBoardUiState();
                loadExpansionBoardUiState(selectedExpansionBoardKey(), selectedExpansionBoardSlot());
            }
        });
        connect(expansionRomSlot, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
            storeCurrentExpansionBoardUiState();
            loadExpansionBoardUiState(selectedExpansionBoardKey(), selectedExpansionBoardSlot());
        });
        connect(expansionRomFile, &QComboBox::currentTextChanged, this, [this]() { storeCurrentExpansionBoardUiState(); });
        connect(expansionRomEnabled, &QCheckBox::toggled, this, [this]() { storeCurrentExpansionBoardUiState(); });
        connect(expansionRomAutobootDisabled, &QCheckBox::toggled, this, [this]() { storeCurrentExpansionBoardUiState(); });
        connect(expansionRom24BitDma, &QCheckBox::toggled, this, [this]() { storeCurrentExpansionBoardUiState(); });
        connect(expansionRomPcmciaInserted, &QCheckBox::toggled, this, [this]() { storeCurrentExpansionBoardUiState(); });
        connect(expansionRomId, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { storeCurrentExpansionBoardUiState(); });
        connect(expansionRomSubtype, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { storeCurrentExpansionBoardUiState(); });
        connect(expansionBoardOption, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
            updateExpansionBoardOptionCheck();
        });
        connect(expansionBoardSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { storeCurrentExpansionBoardUiState(); });
        connect(expansionBoardString, &QLineEdit::textChanged, this, [this]() { storeCurrentExpansionBoardUiState(); });
        connect(expansionBoardOptionCheck, &QCheckBox::toggled, this, [this]() { storeCurrentExpansionBoardUiState(); });
        connect(expansionRomBrowse, &QPushButton::clicked, this, [this]() {
            addBrowse(expansionRomFile, this, QStringLiteral("Expansion board ROM file"), QStringLiteral("ROM images (*.rom *.bin);;All files (*)"));
            if (!expansionRomFile->currentText().trimmed().isEmpty()) {
                expansionRomEnabled->setChecked(true);
            }
            storeCurrentExpansionBoardUiState();
        });

        QGridLayout *accelerator = new QGridLayout;
        cpuBoardType = combo({});
        cpuBoardSubtype = combo({ QStringLiteral("Default") });
        cpuBoardRom = pathCombo();
        acceleratorOption = combo({ QStringLiteral("Settings preserved") });
        acceleratorSelector = combo({ QStringLiteral("None") });
        cpuBoardMem = combo({ QStringLiteral("None") });
        acceleratorOptionCheck = new QCheckBox(QStringLiteral("Enabled"));
        cpuBoardBrowse = new QPushButton(QStringLiteral("..."));
        acceleratorOption->setEnabled(false);
        acceleratorSelector->setEnabled(false);
        acceleratorOptionCheck->setEnabled(false);
        accelerator->setColumnStretch(2, 1);
        accelerator->addWidget(cpuBoardType, 0, 0);
        accelerator->addWidget(cpuBoardSubtype, 1, 0);
        accelerator->addWidget(new QLabel(QStringLiteral("Accelerator board ROM file:")), 0, 2, 1, 2);
        accelerator->addWidget(cpuBoardRom, 1, 2);
        accelerator->addWidget(cpuBoardBrowse, 1, 3);
        accelerator->addWidget(label(QStringLiteral("Accelerator board memory:")), 2, 1);
        accelerator->addWidget(cpuBoardMem, 2, 2, 1, 2);
        accelerator->addWidget(acceleratorOption, 3, 0);
        accelerator->addWidget(acceleratorSelector, 3, 2);
        accelerator->addWidget(acceleratorOptionCheck, 3, 3);
        root->addWidget(groupBox(QStringLiteral("Accelerator Board Settings"), accelerator));
        populateCpuBoardTypeChoices();
        populateCpuBoardSubtypeChoices();
        updateCpuBoardControls();

        connect(cpuBoardType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
            if (!cpuBoardUpdating) {
                cpuBoardSettingsRaw.clear();
            }
            populateCpuBoardSubtypeChoices();
            updateCpuBoardControls();
        });
        connect(cpuBoardSubtype, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
            if (!cpuBoardUpdating) {
                cpuBoardSettingsRaw.clear();
            }
            updateCpuBoardControls();
        });
        connect(cpuBoardBrowse, &QPushButton::clicked, this, [this]() {
            addBrowse(cpuBoardRom, this, QStringLiteral("Accelerator board ROM file"), QStringLiteral("ROM images (*.rom *.bin);;All files (*)"));
        });
        connect(acceleratorOption, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
            updateCpuBoardOptionControls();
        });
        connect(acceleratorSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
            storeCpuBoardOptionFromUi();
        });
        connect(acceleratorOptionCheck, &QCheckBox::toggled, this, [this]() {
            storeCpuBoardOptionFromUi();
        });

        QGridLayout *misc = new QGridLayout;
        expansionBsdsocket = new QCheckBox(QStringLiteral("bsdsocket.library"));
        expansionScsiDevice = new QCheckBox(QStringLiteral("uaescsi.device"));
        expansionSana2 = new QCheckBox(QStringLiteral("uaenet.device"));
#if !UAE_UNIX_WITH_BSDSOCKET
        expansionBsdsocket->setEnabled(false);
        expansionBsdsocket->setToolTip(QStringLiteral("Unix bsdsocket.library backend is not enabled in this build yet."));
#endif
#if !UAE_UNIX_WITH_UAESCSI
        expansionScsiDevice->setEnabled(false);
        expansionScsiDevice->setToolTip(QStringLiteral("Unix uaescsi.device backend is not enabled in this build yet."));
#endif
#if !UAE_UNIX_WITH_SANA2
        expansionSana2->setEnabled(false);
        expansionSana2->setToolTip(QStringLiteral("Unix SANA-II backend is not enabled in this build yet."));
#endif
        misc->addWidget(expansionBsdsocket, 0, 0);
        misc->addWidget(expansionScsiDevice, 1, 0);
        misc->addWidget(expansionSana2, 0, 1);
        misc->setColumnStretch(0, 1);
        misc->setColumnStretch(1, 1);
        root->addWidget(groupBox(QStringLiteral("Miscellaneous Expansions"), misc));
        root->addStretch();
        return page;
    }

    QString selectedExpansionBoardKey() const
    {
        if (!expansionRomBoard) {
            return QString();
        }
        const QString data = expansionRomBoard->currentData().toString();
        if (!data.isEmpty()) {
            return data;
        }
        const QString text = expansionRomBoard->currentText().trimmed();
        if (text.isEmpty() || text == QStringLiteral("None")) {
            return QString();
        }
        if (const WinUaeQtExpansionBoardCatalogItem *choice = expansionBoardChoiceByDisplay(boardCatalog, text)) {
            return choice->key;
        }
        return text;
    }

    int selectedExpansionBoardSlot() const
    {
        if (!expansionRomSlot) {
            return 0;
        }
        bool ok = false;
        const int slot = expansionRomSlot->currentText().toInt(&ok);
        return ok ? qBound(0, slot - 1, 3) : 0;
    }

    int expansionBoardEnabledCount(const QString &boardKey) const
    {
        int count = 0;
        for (auto it = expansionBoardStates.constBegin(); it != expansionBoardStates.constEnd(); ++it) {
            int slot = 0;
            const QString key = expansionBoardBaseKey(it.key(), &slot);
            if (it.value().present && key.compare(boardKey, Qt::CaseInsensitive) == 0) {
                count++;
            }
        }
        return count;
    }

    QString expansionBoardComboText(const WinUaeQtExpansionBoardCatalogItem &choice) const
    {
        const int count = expansionBoardEnabledCount(choice.key);
        if (count == 1) {
            return QStringLiteral("* %1").arg(choice.display);
        }
        if (count > 1) {
            return QStringLiteral("[%1] %2").arg(count).arg(choice.display);
        }
        return choice.display;
    }

    void refreshExpansionBoardChoiceLabels()
    {
        if (!expansionRomBoard) {
            return;
        }

        const QSignalBlocker blocker(expansionRomBoard);
        for (int i = 0; i < expansionRomBoard->count(); i++) {
            const QString boardKey = expansionRomBoard->itemData(i).toString();
            if (const WinUaeQtExpansionBoardCatalogItem *choice = expansionBoardChoiceByKey(boardCatalog, boardKey)) {
                expansionRomBoard->setItemText(i, expansionBoardComboText(*choice));
            }
        }
    }

    void populateExpansionBoardChoices(const QString &requestedKey = QString())
    {
        if (!expansionRomBoard || !expansionRomCategory) {
            return;
        }

        const QString oldKey = requestedKey.isEmpty() ? selectedExpansionBoardKey() : requestedKey;
        const WinUaeQtExpansionCategoryChoice *category = expansionBoardCategoryByDisplay(expansionRomCategory->currentText());
        const QSignalBlocker blocker(expansionRomBoard);
        expansionRomBoard->clear();
        expansionRomBoard->addItem(QStringLiteral("None"), QString());
        if (category) {
            QVector<const WinUaeQtExpansionBoardCatalogItem*> choices;
            for (const WinUaeQtExpansionBoardCatalogItem &choice : boardCatalog.expansionBoards) {
                if (expansionBoardMatchesCategory(choice, *category)) {
                    choices.append(&choice);
                }
            }
            std::sort(choices.begin(), choices.end(), [](const WinUaeQtExpansionBoardCatalogItem *a, const WinUaeQtExpansionBoardCatalogItem *b) {
                const QString aSort = expansionBoardSortText(a->display);
                const QString bSort = expansionBoardSortText(b->display);
                const int byName = QString::compare(aSort, bSort, Qt::CaseInsensitive);
                if (byName != 0) {
                    return byName < 0;
                }
                return QString::compare(a->display, b->display, Qt::CaseInsensitive) < 0;
            });
            for (const WinUaeQtExpansionBoardCatalogItem *choice : choices) {
                expansionRomBoard->addItem(expansionBoardComboText(*choice), choice->key);
            }
        }

        int selectedIndex = 0;
        for (int i = 0; i < expansionRomBoard->count(); i++) {
            if (expansionRomBoard->itemData(i).toString().compare(oldKey, Qt::CaseInsensitive) == 0) {
                selectedIndex = i;
                break;
            }
        }
        expansionRomBoard->setCurrentIndex(selectedIndex);
    }

    void populateExpansionSubtypeChoices(const QString &boardKey, const QString &requestedValue)
    {
        if (!expansionRomSubtype) {
            return;
        }

        const QSignalBlocker blocker(expansionRomSubtype);
        expansionRomSubtype->clear();
        int selectedIndex = -1;
        if (const WinUaeQtExpansionBoardCatalogItem *board = expansionBoardChoiceByKey(boardCatalog, boardKey)) {
            for (const WinUaeQtBoardSubtype &choice : board->subtypes) {
                expansionRomSubtype->addItem(choice.display, choice.configValue);
                if (requestedValue.compare(choice.configValue, Qt::CaseInsensitive) == 0) {
                    selectedIndex = expansionRomSubtype->count() - 1;
                }
            }
        }

        if (expansionRomSubtype->count() == 0) {
            expansionRomSubtype->addItem(QStringLiteral("Default"), QString());
            expansionRomSubtype->setCurrentIndex(0);
            expansionRomSubtype->setEnabled(false);
            return;
        }

        if (selectedIndex < 0 && !requestedValue.isEmpty()) {
            expansionRomSubtype->addItem(requestedValue, requestedValue);
            selectedIndex = expansionRomSubtype->count() - 1;
        }
        expansionRomSubtype->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);
        expansionRomSubtype->setEnabled(true);
    }

    bool expansionBoardOptionIsMulti(const WinUaeQtBoardSetting *choice) const
    {
        return choice
            && choice->type == WinUaeQtBoardSettingType::Multi
            && !choice->multiValues.isEmpty();
    }

    bool expansionBoardMultiValueConfigured(const WinUaeQtExpansionBoardState &state, const WinUaeQtBoardSetting &choice) const
    {
        if (state.optionValues.contains(choice.configValue)) {
            return true;
        }
        for (const QString &value : choice.multiValues) {
            if (expansionOptionContains(state.rawOptions, value)) {
                return true;
            }
        }
        return false;
    }

    QString expansionBoardSelectedMultiValue(const WinUaeQtExpansionBoardState &state, const WinUaeQtBoardSetting *choice) const
    {
        if (!choice) {
            return QString();
        }
        const QString stored = state.optionValues.value(choice->configValue);
        if (!stored.isEmpty()) {
            return stored;
        }
        for (const QString &value : choice->multiValues) {
            if (expansionOptionContains(state.rawOptions, value)) {
                return value;
            }
        }
        return choice->multiValues.value(0);
    }

    QString expansionBoardStringValue(const WinUaeQtExpansionBoardState &state, const WinUaeQtBoardSetting *choice) const
    {
        if (!choice) {
            return QString();
        }
        if (state.optionValues.contains(choice->configValue)) {
            return state.optionValues.value(choice->configValue);
        }
        return expansionOptionValue(state.rawOptions, choice->configValue);
    }

    bool expansionBoardSettingHasStoredValue(const WinUaeQtExpansionBoardState &state, const WinUaeQtBoardSetting &choice) const
    {
        if (choice.type == WinUaeQtBoardSettingType::CheckBox) {
            return state.optionBools.value(choice.configValue, false);
        }
        if (choice.type == WinUaeQtBoardSettingType::String) {
            return !expansionBoardStringValue(state, &choice).isEmpty();
        }
        return expansionBoardMultiValueConfigured(state, choice);
    }

    void populateExpansionOptionChoices(const QString &boardKey)
    {
        if (!expansionBoardOption) {
            return;
        }

        const QString requested = expansionBoardOption->currentData().toString();
        const WinUaeQtExpansionBoardState state = expansionBoardStates.value(currentExpansionBoardConfigName);
        const QSignalBlocker blocker(expansionBoardOption);
        expansionBoardOption->clear();
        expansionBoardOption->addItem(QStringLiteral("None"), QString());
        int selectedIndex = 0;
        if (const WinUaeQtExpansionBoardCatalogItem *board = expansionBoardChoiceByKey(boardCatalog, boardKey)) {
            for (const WinUaeQtBoardSetting &choice : board->settings) {
                expansionBoardOption->addItem(choice.display, choice.configValue);
                if (requested.compare(choice.configValue, Qt::CaseInsensitive) == 0) {
                    selectedIndex = expansionBoardOption->count() - 1;
                } else if (selectedIndex == 0 && requested.isEmpty() && expansionBoardSettingHasStoredValue(state, choice)) {
                    selectedIndex = expansionBoardOption->count() - 1;
                }
            }
        }
        expansionBoardOption->setCurrentIndex(selectedIndex);
        expansionBoardOption->setEnabled(expansionBoardOption->count() > 1);
        updateExpansionBoardOptionCheck();
    }

    void updateExpansionBoardOptionCheck()
    {
        if (!expansionBoardOption || !expansionBoardOptionCheck || !expansionBoardSelector || !expansionBoardString) {
            return;
        }
        const QString option = expansionBoardOption->currentData().toString();
        int slot = 0;
        const QString boardKey = expansionBoardBaseKey(currentExpansionBoardConfigName, &slot);
        const WinUaeQtExpansionBoardCatalogItem *board = expansionBoardChoiceByKey(boardCatalog, boardKey);
        const WinUaeQtBoardSetting *choice = board ? boardSettingChoiceByConfig(board->settings, option) : nullptr;
        const bool enabled = !expansionBoardUpdating
            && !currentExpansionBoardConfigName.isEmpty()
            && board
            && choice;
        const WinUaeQtExpansionBoardState state = expansionBoardStates.value(currentExpansionBoardConfigName);
        const QSignalBlocker checkBlocker(expansionBoardOptionCheck);
        const QSignalBlocker selectorBlocker(expansionBoardSelector);
        const QSignalBlocker stringBlocker(expansionBoardString);

        expansionBoardOptionCheck->setVisible(choice && choice->type == WinUaeQtBoardSettingType::CheckBox);
        expansionBoardSelector->setVisible(expansionBoardOptionIsMulti(choice));
        expansionBoardString->setVisible(choice && choice->type == WinUaeQtBoardSettingType::String);

        expansionBoardOptionCheck->setEnabled(enabled && choice->type == WinUaeQtBoardSettingType::CheckBox);
        expansionBoardOptionCheck->setChecked(enabled && choice->type == WinUaeQtBoardSettingType::CheckBox && state.optionBools.value(option, false));

        expansionBoardSelector->clear();
        if (enabled && expansionBoardOptionIsMulti(choice)) {
            const QStringList displays = choice->multiDisplays;
            const QStringList values = choice->multiValues;
            for (int i = 0; i < values.size(); i++) {
                expansionBoardSelector->addItem(displays.value(i, values[i]), values[i]);
            }
            const QString selected = expansionBoardSelectedMultiValue(state, choice);
            const int index = expansionBoardSelector->findData(selected);
            expansionBoardSelector->setCurrentIndex(index >= 0 ? index : 0);
        } else {
            expansionBoardSelector->addItem(QStringLiteral("None"), QString());
            expansionBoardSelector->setCurrentIndex(0);
        }
        expansionBoardSelector->setEnabled(enabled && expansionBoardOptionIsMulti(choice));

        expansionBoardString->setText(enabled && choice->type == WinUaeQtBoardSettingType::String
            ? expansionBoardStringValue(state, choice)
            : QString());
        expansionBoardString->setEnabled(enabled && choice->type == WinUaeQtBoardSettingType::String);
    }

    void loadExpansionBoardUiState(const QString &boardKey, int slot)
    {
        if (!expansionRomEnabled) {
            return;
        }

        expansionBoardUpdating = true;
        const QString configName = expansionBoardConfigName(boardKey, slot);
        currentExpansionBoardConfigName = configName;
        const WinUaeQtExpansionBoardState state = expansionBoardStates.value(configName);
        const bool hasBoard = !boardKey.isEmpty();
        const WinUaeQtExpansionBoardCatalogItem *choice = expansionBoardChoiceByKey(boardCatalog, boardKey);
        const bool supported = hasBoard && choice;
        const bool noRomFile = expansionBoardNoRomFile(choice, state.subtype);

        const QList<QWidget*> controls {
            expansionRomSlot,
            expansionRomId,
            expansionRomFile,
            expansionRomBrowse,
            expansionRom24BitDma,
            expansionRomEnabled,
            expansionRomAutobootDisabled,
            expansionRomPcmciaInserted
        };
        for (QWidget *control : controls) {
            if (control) {
                control->setEnabled(supported);
                control->setToolTip(QString());
            }
        }
        if (expansionRomSlot) {
            expansionRomSlot->setEnabled(supported && ((choice->zorro >= 2 && !choice->singleOnly) || choice->clockPort));
        }
        if (expansionRom24BitDma) {
            expansionRom24BitDma->setEnabled(supported && choice->dma24Bit);
        }
        if (expansionRomPcmciaInserted) {
            expansionRomPcmciaInserted->setEnabled(supported && choice->pcmcia);
        }
        if (expansionRomAutobootDisabled) {
            expansionRomAutobootDisabled->setEnabled(supported && choice->autobootJumper);
        }
        if (expansionRomId) {
            expansionRomId->setEnabled(supported && choice->idJumper);
        }
        populateExpansionSubtypeChoices(hasBoard ? boardKey : QString(), state.subtype);
        expansionRomSubtype->setEnabled(supported && expansionRomSubtype->count() > 1);
        populateExpansionOptionChoices(hasBoard ? boardKey : QString());
        expansionBoardOption->setEnabled(supported && expansionBoardOption->count() > 1);

        if (expansionRomFile) {
            expansionRomFile->setVisible(!noRomFile);
            expansionRomFile->setEnabled(supported && !noRomFile);
        }
        if (expansionRomBrowse) {
            expansionRomBrowse->setVisible(!noRomFile);
            expansionRomBrowse->setEnabled(supported && !noRomFile);
        }
        if (expansionRomEnabled) {
            expansionRomEnabled->setText(noRomFile ? QStringLiteral("Enabled") : QStringLiteral("Enable board"));
        }
        setPathComboText(expansionRomFile, noRomFile || state.romFile == QStringLiteral(":ENABLED") ? QString() : state.romFile);
        expansionRomEnabled->setChecked(hasBoard && state.present);
        expansionRomAutobootDisabled->setChecked(hasBoard && state.autobootDisabled);
        expansionRom24BitDma->setChecked(hasBoard && state.dma24Bit);
        expansionRomPcmciaInserted->setChecked(hasBoard && state.inserted);
        expansionRomId->setCurrentText(QString::number(qBound(0, state.id, 7)));
        expansionBoardUpdating = false;
        updateExpansionBoardOptionCheck();
    }

    void storeCurrentExpansionBoardUiState()
    {
        if (expansionBoardUpdating || currentExpansionBoardConfigName.isEmpty() || !expansionRomEnabled) {
            return;
        }

        WinUaeQtExpansionBoardState state = expansionBoardStates.value(currentExpansionBoardConfigName);
        state.autobootDisabled = expansionRomAutobootDisabled->isChecked();
        state.dma24Bit = expansionRom24BitDma->isChecked();
        state.inserted = expansionRomPcmciaInserted->isChecked();
        state.id = expansionRomId->currentText().toInt();
        state.subtype = expansionRomSubtype && expansionRomSubtype->isEnabled()
            ? expansionRomSubtype->currentData().toString()
            : QString();
        int currentSlot = 0;
        const QString currentBoardKey = expansionBoardBaseKey(currentExpansionBoardConfigName, &currentSlot);
        const WinUaeQtExpansionBoardCatalogItem *currentBoard = expansionBoardChoiceByKey(boardCatalog, currentBoardKey);
        const bool noRomFile = expansionBoardNoRomFile(currentBoard, state.subtype);
        if (noRomFile) {
            state.present = expansionRomEnabled->isChecked();
            state.romFile = state.present ? QStringLiteral(":ENABLED") : QString();
        } else {
            state.present = expansionRomEnabled->isChecked() || !expansionRomFile->currentText().trimmed().isEmpty();
            state.romFile = expansionRomFile->currentText().trimmed();
        }
        if (expansionBoardOption && expansionBoardOptionCheck && expansionBoardSelector && expansionBoardString) {
            const QString option = expansionBoardOption->currentData().toString();
            int slot = 0;
            const QString boardKey = expansionBoardBaseKey(currentExpansionBoardConfigName, &slot);
            const WinUaeQtExpansionBoardCatalogItem *board = expansionBoardChoiceByKey(boardCatalog, boardKey);
            const WinUaeQtBoardSetting *choice = board ? boardSettingChoiceByConfig(board->settings, option) : nullptr;
            if (choice && choice->type == WinUaeQtBoardSettingType::CheckBox) {
                state.optionBools.insert(option, expansionBoardOptionCheck->isChecked());
            } else if (choice && choice->type == WinUaeQtBoardSettingType::Multi) {
                state.optionValues.insert(option, expansionBoardSelector->currentData().toString());
            } else if (choice && choice->type == WinUaeQtBoardSettingType::String) {
                state.optionValues.insert(option, expansionBoardString->text().trimmed());
            }
        }
        if (state.present) {
            expansionBoardStates.insert(currentExpansionBoardConfigName, state);
        } else {
            expansionBoardStates.remove(currentExpansionBoardConfigName);
        }
        refreshExpansionBoardChoiceLabels();
        refreshHardwareInfoPage();
    }

    void clearExpansionBoardStates()
    {
        expansionBoardStates.clear();
        currentExpansionBoardConfigName.clear();
        if (expansionRomCategory) {
            expansionRomCategory->setCurrentIndex(0);
        }
        if (expansionRomBoard) {
            populateExpansionBoardChoices();
            loadExpansionBoardUiState(QString(), 0);
        }
    }

    bool setExpansionBoardFromConfigName(const QString &configName)
    {
        int slot = 0;
        const QString boardKey = expansionBoardBaseKey(configName, &slot);
        const WinUaeQtExpansionBoardCatalogItem *choice = expansionBoardChoiceByKey(boardCatalog, boardKey);
        if (!choice || !expansionRomCategory || !expansionRomBoard || !expansionRomSlot) {
            return false;
        }

        QString categoryDisplay;
        for (const WinUaeQtExpansionCategoryChoice *category = expansionBoardCategoryChoices; category->display; category++) {
            if (expansionBoardMatchesCategory(*choice, *category)) {
                categoryDisplay = QString::fromLatin1(category->display);
                break;
            }
        }
        if (!categoryDisplay.isEmpty() && expansionRomCategory->currentText() != categoryDisplay) {
            expansionRomCategory->setCurrentText(categoryDisplay);
        }
        populateExpansionBoardChoices(boardKey);
        for (int i = 0; i < expansionRomBoard->count(); i++) {
            if (expansionRomBoard->itemData(i).toString().compare(boardKey, Qt::CaseInsensitive) == 0) {
                expansionRomBoard->setCurrentIndex(i);
                break;
            }
        }
        expansionRomSlot->setCurrentText(QString::number(qBound(0, slot, 3) + 1));
        loadExpansionBoardUiState(boardKey, slot);
        return true;
    }

    bool applyExpansionBoardSetting(const QString &key, const QString &value)
    {
        QString configName;
        bool isOptions = false;
        if (key.endsWith(QStringLiteral("_rom_file"))) {
            configName = key.left(key.size() - int(QStringLiteral("_rom_file").size()));
        } else if (key.endsWith(QStringLiteral("_rom_options"))) {
            configName = key.left(key.size() - int(QStringLiteral("_rom_options").size()));
            isOptions = true;
        } else {
            return false;
        }

        int slot = 0;
        const QString boardKey = expansionBoardBaseKey(configName, &slot);
        const WinUaeQtExpansionBoardCatalogItem *board = expansionBoardChoiceByKey(boardCatalog, boardKey);
        if (!board) {
            return false;
        }

        WinUaeQtExpansionBoardState state = expansionBoardStates.value(configName);
        if (isOptions) {
            state.rawOptions = value;
            state.subtype = expansionOptionValue(value, QStringLiteral("subtype"));
            state.autobootDisabled = expansionOptionBool(value, QStringLiteral("autoboot_disabled"));
            state.dma24Bit = expansionOptionBool(value, QStringLiteral("dma24bit"));
            state.inserted = expansionOptionBool(value, QStringLiteral("inserted"));
            const QString id = expansionOptionValue(value, QStringLiteral("id"));
            if (!id.isEmpty()) {
                state.id = qBound(0, id.toInt(), 7);
            }
            for (const WinUaeQtBoardSetting &choice : board->settings) {
                if (choice.type == WinUaeQtBoardSettingType::CheckBox) {
                    const QString option = choice.configValue;
                    state.optionBools.insert(option, expansionOptionBool(value, option));
                } else if (choice.type == WinUaeQtBoardSettingType::String) {
                    state.optionValues.insert(choice.configValue, expansionOptionValue(value, choice.configValue));
                } else if (choice.type == WinUaeQtBoardSettingType::Multi) {
                    for (const QString &option : choice.multiValues) {
                        if (expansionOptionContains(value, option)) {
                            state.optionValues.insert(choice.configValue, option);
                            break;
                        }
                    }
                }
            }
        } else {
            state.present = !value.trimmed().isEmpty();
            state.romFile = value.trimmed();
        }
        expansionBoardStates.insert(configName, state);
        return setExpansionBoardFromConfigName(configName);
    }

    void insertExpansionBoardSettings(WinUaeQtConfig::Settings &settings) const
    {
        for (auto it = expansionBoardStates.constBegin(); it != expansionBoardStates.constEnd(); ++it) {
            const WinUaeQtExpansionBoardState &state = it.value();
            if (!state.present) {
                continue;
            }
            int slot = 0;
            const WinUaeQtExpansionBoardCatalogItem *board = expansionBoardChoiceByKey(boardCatalog, expansionBoardBaseKey(it.key(), &slot));
            if (!board) {
                continue;
            }
            const QString romFile = expansionBoardNoRomFile(board, state.subtype)
                ? QStringLiteral(":ENABLED")
                : (state.romFile.trimmed().isEmpty() ? QStringLiteral(":ENABLED") : state.romFile.trimmed());
            settings.insert(it.key() + QStringLiteral("_rom_file"), romFile);
            const QString options = expansionBoardOptionsValue(state, board);
            if (!options.isEmpty()) {
                settings.insert(it.key() + QStringLiteral("_rom_options"), options);
            }
        }
    }

    WinUaeQtConfig::Settings currentExpansionBoardSettings() const
    {
        WinUaeQtConfig::Settings settings;
        insertExpansionBoardSettings(settings);
        return settings;
    }

    QStringList expansionBoardOwnedKeys() const
    {
        QStringList keys;
        for (const WinUaeQtExpansionBoardCatalogItem &choice : boardCatalog.expansionBoards) {
            for (int slot = 0; slot < 4; slot++) {
                const QString name = expansionBoardConfigName(choice.key, slot);
                keys.append(name + QStringLiteral("_rom_file"));
                keys.append(name + QStringLiteral("_rom_options"));
            }
        }
        return keys;
    }

    QString selectedCpuBoardType() const
    {
        if (!cpuBoardType) {
            return QString();
        }
        const QString data = cpuBoardType->currentData().toString();
        if (!data.isEmpty()) {
            return data;
        }
        const QString text = cpuBoardType->currentText().trimmed();
        return text == QStringLiteral("None") ? QString() : text;
    }

    QString selectedCpuBoardConfigValue() const
    {
        if (!cpuBoardSubtype || !cpuBoardSubtype->isEnabled()) {
            return QString();
        }
        return cpuBoardSubtype->currentData().toString();
    }

    const WinUaeQtCpuBoardCatalogItem *selectedCpuBoardChoice() const
    {
        return cpuBoardSubtypeChoiceByConfig(boardCatalog, selectedCpuBoardConfigValue());
    }

    QStringList cpuBoardMemoryItems(int maxMemoryMb) const
    {
        QStringList items { QStringLiteral("None") };
        for (int mb : { 1, 2, 4, 8, 16, 32, 64, 128, 256 }) {
            if (maxMemoryMb <= 0 || mb <= maxMemoryMb) {
                items.append(QStringLiteral("%1 MB").arg(mb));
            }
        }
        return items;
    }

    bool cpuBoardOptionIsMulti(const WinUaeQtBoardSetting *choice) const
    {
        return choice
            && choice->type == WinUaeQtBoardSettingType::Multi
            && !choice->multiValues.isEmpty();
    }

    QStringList cpuBoardOptionTokens(const WinUaeQtBoardSetting *choice) const
    {
        if (!choice) {
            return {};
        }
        if (cpuBoardOptionIsMulti(choice)) {
            return choice->multiValues;
        }
        return { choice->configValue };
    }

    bool cpuBoardSettingsContainToken(const QString &token) const
    {
        for (const QString &field : expansionOptionTokens(cpuBoardSettingsRaw)) {
            if (field.compare(token, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        return false;
    }

    QString cpuBoardSelectedMultiValue(const WinUaeQtBoardSetting *choice) const
    {
        const QStringList values = cpuBoardOptionTokens(choice);
        for (const QString &value : values) {
            if (cpuBoardSettingsContainToken(value)) {
                return value;
            }
        }
        return values.value(0);
    }

    void setCpuBoardSettingsToken(const WinUaeQtBoardSetting *choice, const QString &selectedValue, bool enabled)
    {
        if (!choice) {
            return;
        }
        const QStringList removeTokens = cpuBoardOptionTokens(choice);
        QStringList tokens;
        for (const QString &token : expansionOptionTokens(cpuBoardSettingsRaw)) {
            bool remove = false;
            for (const QString &known : removeTokens) {
                if (token.compare(known, Qt::CaseInsensitive) == 0) {
                    remove = true;
                    break;
                }
            }
            if (!remove) {
                tokens.append(token);
            }
        }
        if (cpuBoardOptionIsMulti(choice)) {
            if (!selectedValue.isEmpty()) {
                tokens.append(selectedValue);
            }
        } else if (enabled) {
            tokens.append(choice->configValue);
        }
        cpuBoardSettingsRaw = tokens.join(QLatin1Char(','));
    }

    void populateCpuBoardTypeChoices()
    {
        if (!cpuBoardType) {
            return;
        }
        const QSignalBlocker blocker(cpuBoardType);
        const QString selected = selectedCpuBoardType();
        cpuBoardType->clear();
        cpuBoardType->addItem(QStringLiteral("None"), QString());
        int selectedIndex = 0;
        QStringList seenTypes;
        for (const WinUaeQtCpuBoardCatalogItem &choice : boardCatalog.cpuBoards) {
            const QString typeText = choice.type;
            if (seenTypes.contains(typeText) || !cpuBoardTypeHasSubtypes(boardCatalog, typeText)) {
                continue;
            }
            seenTypes.append(typeText);
            cpuBoardType->addItem(typeText, typeText);
            if (selected == typeText) {
                selectedIndex = cpuBoardType->count() - 1;
            }
        }
        cpuBoardType->setCurrentIndex(selectedIndex);
    }

    void populateCpuBoardSubtypeChoices(const QString &requestedConfig = QString())
    {
        if (!cpuBoardSubtype) {
            return;
        }
        const QString selectedType = selectedCpuBoardType();
        const QString currentConfig = requestedConfig.isEmpty() ? selectedCpuBoardConfigValue() : requestedConfig;
        const QSignalBlocker blocker(cpuBoardSubtype);
        cpuBoardSubtype->clear();
        int selectedIndex = -1;
        for (const WinUaeQtCpuBoardCatalogItem &choice : boardCatalog.cpuBoards) {
            if (selectedType != choice.type) {
                continue;
            }
            cpuBoardSubtype->addItem(choice.display, choice.configValue);
            if (currentConfig.compare(choice.configValue, Qt::CaseInsensitive) == 0) {
                selectedIndex = cpuBoardSubtype->count() - 1;
            }
        }
        if (cpuBoardSubtype->count() == 0) {
            cpuBoardSubtype->addItem(QStringLiteral("Default"), QString());
            cpuBoardSubtype->setCurrentIndex(0);
            cpuBoardSubtype->setEnabled(false);
            return;
        }
        cpuBoardSubtype->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);
        cpuBoardSubtype->setEnabled(true);
    }

    void setCpuBoardMemoryMb(int memoryMb)
    {
        if (!cpuBoardMem) {
            return;
        }
        const QString text = memoryMb > 0 ? QStringLiteral("%1 MB").arg(memoryMb) : QStringLiteral("None");
        if (cpuBoardMem->findText(text) < 0) {
            cpuBoardMem->addItem(text);
        }
        cpuBoardMem->setCurrentText(text);
    }

    void updateCpuBoardControls()
    {
        if (!cpuBoardSubtype || !cpuBoardRom || !cpuBoardMem || !cpuBoardBrowse) {
            return;
        }
        const WinUaeQtCpuBoardCatalogItem *choice = selectedCpuBoardChoice();
        const bool hasBoard = choice != nullptr;
        cpuBoardSubtype->setEnabled(!selectedCpuBoardType().isEmpty() && cpuBoardSubtype->count() > 0);
        cpuBoardRom->setEnabled(hasBoard);
        cpuBoardBrowse->setEnabled(hasBoard);

        const int currentMemory = megabytesFromText(cpuBoardMem->currentText());
        const QSignalBlocker blocker(cpuBoardMem);
        cpuBoardMem->clear();
        cpuBoardMem->addItems(cpuBoardMemoryItems(choice ? choice->maxMemoryMb : 0));
        setCpuBoardMemoryMb(hasBoard ? qMin(currentMemory, choice->maxMemoryMb) : 0);
        cpuBoardMem->setEnabled(hasBoard);
        updateCpuBoardOptionChoices();
    }

    void selectCpuBoardConfigValue(const QString &configValue)
    {
        const WinUaeQtCpuBoardCatalogItem *choice = cpuBoardSubtypeChoiceByConfig(boardCatalog, configValue);
        cpuBoardUpdating = true;
        if (!choice) {
            if (cpuBoardType) {
                cpuBoardType->setCurrentIndex(0);
            }
            populateCpuBoardSubtypeChoices();
            updateCpuBoardControls();
            cpuBoardUpdating = false;
            return;
        }
        if (cpuBoardType) {
            cpuBoardType->setCurrentText(choice->type);
        }
        populateCpuBoardSubtypeChoices(choice->configValue);
        updateCpuBoardControls();
        cpuBoardUpdating = false;
    }

    void updateCpuBoardOptionChoices()
    {
        if (!acceleratorOption) {
            return;
        }
        const QString requested = acceleratorOption->currentData().toString();
        const WinUaeQtCpuBoardCatalogItem *board = selectedCpuBoardChoice();
        const QSignalBlocker blocker(acceleratorOption);
        acceleratorOption->clear();
        acceleratorOption->addItem(QStringLiteral("None"), QString());
        int selectedIndex = 0;
        if (board) {
            for (const WinUaeQtBoardSetting &choice : board->settings) {
                if (choice.type == WinUaeQtBoardSettingType::String) {
                    continue;
                }
                acceleratorOption->addItem(choice.display, choice.configValue);
                if (requested.compare(choice.configValue, Qt::CaseInsensitive) == 0) {
                    selectedIndex = acceleratorOption->count() - 1;
                } else if (selectedIndex == 0 && requested.isEmpty()) {
                    for (const QString &token : cpuBoardOptionTokens(&choice)) {
                        if (cpuBoardSettingsContainToken(token)) {
                            selectedIndex = acceleratorOption->count() - 1;
                            break;
                        }
                    }
                }
            }
        }
        acceleratorOption->setCurrentIndex(selectedIndex);
        acceleratorOption->setEnabled(acceleratorOption->count() > 1);
        updateCpuBoardOptionControls();
    }

    void updateCpuBoardOptionControls()
    {
        if (!acceleratorOption || !acceleratorSelector || !acceleratorOptionCheck) {
            return;
        }
        const QString option = acceleratorOption->currentData().toString();
        const WinUaeQtCpuBoardCatalogItem *board = selectedCpuBoardChoice();
        const WinUaeQtBoardSetting *choice = board ? boardSettingChoiceByConfig(board->settings, option) : nullptr;
        const bool isMulti = cpuBoardOptionIsMulti(choice);

        const QSignalBlocker selectorBlocker(acceleratorSelector);
        const QSignalBlocker checkBlocker(acceleratorOptionCheck);
        acceleratorSelector->clear();
        if (isMulti) {
            const QStringList displays = choice->multiDisplays;
            const QStringList values = choice->multiValues;
            for (int i = 0; i < values.size(); i++) {
                acceleratorSelector->addItem(displays.value(i, values[i]), values[i]);
            }
            const QString selected = cpuBoardSelectedMultiValue(choice);
            const int index = acceleratorSelector->findData(selected);
            acceleratorSelector->setCurrentIndex(index >= 0 ? index : 0);
        } else {
            acceleratorSelector->addItem(QStringLiteral("None"), QString());
            acceleratorSelector->setCurrentIndex(0);
        }
        acceleratorSelector->setEnabled(isMulti);
        acceleratorOptionCheck->setEnabled(choice && !isMulti);
        acceleratorOptionCheck->setChecked(choice && !isMulti && cpuBoardSettingsContainToken(choice->configValue));
    }

    void storeCpuBoardOptionFromUi()
    {
        if (cpuBoardUpdating || !acceleratorOption || !acceleratorSelector || !acceleratorOptionCheck) {
            return;
        }
        const QString option = acceleratorOption->currentData().toString();
        const WinUaeQtCpuBoardCatalogItem *board = selectedCpuBoardChoice();
        const WinUaeQtBoardSetting *choice = board ? boardSettingChoiceByConfig(board->settings, option) : nullptr;
        if (!choice) {
            return;
        }
        if (cpuBoardOptionIsMulti(choice)) {
            setCpuBoardSettingsToken(choice, acceleratorSelector->currentData().toString(), true);
        } else {
            setCpuBoardSettingsToken(choice, QString(), acceleratorOptionCheck->isChecked());
        }
    }

    QWidget *makeHardwareInfoPage()
    {
        QWidget *page = makePage();
        QVBoxLayout *root = new QVBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);

        hardwareBoardList = new QTreeWidget;
        hardwareBoardList->setRootIsDecorated(false);
        hardwareBoardList->setAlternatingRowColors(true);
        hardwareBoardList->setSelectionMode(QAbstractItemView::SingleSelection);
        hardwareBoardList->setEditTriggers(QAbstractItemView::NoEditTriggers);
        hardwareBoardList->setHeaderLabels({
            QStringLiteral("Type"),
            QStringLiteral("Name"),
            QStringLiteral("Start"),
            QStringLiteral("End"),
            QStringLiteral("Size"),
            QStringLiteral("ID")
        });
        root->addWidget(hardwareBoardList, 1);

        QHBoxLayout *actions = new QHBoxLayout;
        hardwareCustomBoardOrder = new QCheckBox(QStringLiteral("Custom board order"));
        hardwareMoveUp = new QPushButton(QStringLiteral("Move up"));
        hardwareMoveDown = new QPushButton(QStringLiteral("Move down"));
        if (!hasHardwareInfoProvider()) {
            disableUnavailable(hardwareMoveUp, QStringLiteral("Hardware board reordering needs the integrated Unix preferences model."));
            disableUnavailable(hardwareMoveDown, QStringLiteral("Hardware board reordering needs the integrated Unix preferences model."));
        }
        actions->addWidget(hardwareCustomBoardOrder);
        actions->addStretch();
        actions->addWidget(hardwareMoveUp);
        actions->addWidget(hardwareMoveDown);
        root->addLayout(actions);

        connect(hardwareCustomBoardOrder, &QCheckBox::toggled, this, [this](bool enabled) {
            if (hardwareProvider.setCustomOrder) {
                hardwareProvider.setCustomOrder(hardwareProvider.context, enabled);
            }
            refreshHardwareInfoPage();
        });
        connect(hardwareBoardList, &QTreeWidget::itemSelectionChanged, this, [this]() { updateHardwareMoveButtons(); });
        connect(hardwareMoveUp, &QPushButton::clicked, this, [this]() { moveSelectedHardwareBoard(-1); });
        connect(hardwareMoveDown, &QPushButton::clicked, this, [this]() { moveSelectedHardwareBoard(1); });
        return page;
    }

    quint64 bytesFromMemoryText(QString text) const
    {
        text = text.trimmed();
        if (text == QStringLiteral("None") || text.isEmpty()) {
            return 0;
        }
        if (text.endsWith(QStringLiteral("KB"))) {
            text.chop(2);
            return quint64(text.trimmed().toDouble() * 1024.0);
        }
        if (text.endsWith(QStringLiteral("MB"))) {
            text.chop(2);
            return quint64(text.trimmed().toDouble() * 1024.0 * 1024.0);
        }
        return 0;
    }

    QString hardwareSizeText(quint64 bytes) const
    {
        if (!bytes) {
            return QStringLiteral("-");
        }
        return QStringLiteral("0x%1").arg(bytes, 8, 16, QLatin1Char('0'));
    }

    void addHardwareBoardRow(
        const QString &type,
        const QString &name,
        const QString &start,
        const QString &end,
        const QString &size,
        const QString &id,
        int boardIndex = -1,
        bool movable = false)
    {
        if (!hardwareBoardList) {
            return;
        }
        QTreeWidgetItem *item = new QTreeWidgetItem(hardwareBoardList);
        item->setText(0, type);
        item->setText(1, name);
        item->setText(2, start);
        item->setText(3, end);
        item->setText(4, size);
        item->setText(5, id);
        item->setData(0, HardwareBoardIndexRole, boardIndex);
        item->setData(0, Qt::UserRole + 1, movable);
    }

    bool hasHardwareInfoProvider() const
    {
        return hardwareProvider.context && hardwareProvider.boards;
    }

    bool hardwareCustomOrderEnabled() const
    {
        if (hardwareProvider.customOrder && hardwareProvider.context) {
            return hardwareProvider.customOrder(hardwareProvider.context);
        }
        return hardwareCustomBoardOrder && hardwareCustomBoardOrder->isChecked();
    }

    void updateHardwareMoveButtons()
    {
        bool moveUp = false;
        bool moveDown = false;
        if (hasHardwareInfoProvider() && hardwareCustomOrderEnabled() && hardwareBoardList && hardwareBoardList->currentItem()) {
            const int index = hardwareBoardList->currentItem()->data(0, HardwareBoardIndexRole).toInt();
            if (hardwareProvider.canMove) {
                moveUp = hardwareProvider.canMove(hardwareProvider.context, index, -1);
                moveDown = hardwareProvider.canMove(hardwareProvider.context, index, 1);
            }
        }
        if (hardwareMoveUp) {
            hardwareMoveUp->setEnabled(moveUp);
        }
        if (hardwareMoveDown) {
            hardwareMoveDown->setEnabled(moveDown);
        }
    }

    void moveSelectedHardwareBoard(int direction)
    {
        if (!hasHardwareInfoProvider() || !hardwareProvider.move || !hardwareBoardList || !hardwareBoardList->currentItem()) {
            return;
        }
        const int index = hardwareBoardList->currentItem()->data(0, HardwareBoardIndexRole).toInt();
        const int newIndex = hardwareProvider.move(hardwareProvider.context, index, direction);
        refreshHardwareInfoPage();
        if (newIndex >= 0) {
            for (int row = 0; row < hardwareBoardList->topLevelItemCount(); row++) {
                QTreeWidgetItem *item = hardwareBoardList->topLevelItem(row);
                if (item && item->data(0, HardwareBoardIndexRole).toInt() == newIndex) {
                    hardwareBoardList->setCurrentItem(item);
                    break;
                }
            }
        }
        updateHardwareMoveButtons();
    }

    void mergeHardwareOrderSettings(WinUaeQtConfig::Settings &settings) const
    {
        if (!hardwareProvider.orderSettings || !hardwareProvider.context) {
            return;
        }
        const WinUaeQtConfig::Settings orders = hardwareProvider.orderSettings(hardwareProvider.context);
        for (auto it = orders.constBegin(); it != orders.constEnd(); ++it) {
            if (!hardwareOrderOwnedKeys.contains(it.key())) {
                hardwareOrderOwnedKeys.append(it.key());
            }
            if (it.key() == QStringLiteral("board_custom_order")) {
                continue;
            }
            const QString order = expansionOptionValue(it.value(), QStringLiteral("order"));
            if (order.isEmpty()) {
                continue;
            }
            settings.insert(it.key(), expansionOptionsWithValue(settings.value(it.key()), QStringLiteral("order"), order));
        }
    }

    void trackHardwareOrderSetting(const QString &key, const QString &value)
    {
        if (!hardwareOrderOwnedKeys.contains(key) && !expansionOptionValue(value, QStringLiteral("order")).isEmpty()) {
            hardwareOrderOwnedKeys.append(key);
        }
    }

    void refreshHardwareInfoPage()
    {
        if (!hardwareBoardList) {
            return;
        }
        hardwareBoardList->clear();

        if (hasHardwareInfoProvider()) {
            if (hardwareProvider.applyConfig) {
                hardwareProvider.applyConfig(hardwareProvider.context, mergedConfig());
            }
            if (hardwareCustomBoardOrder) {
                QSignalBlocker blocker(hardwareCustomBoardOrder);
                hardwareCustomBoardOrder->setChecked(hardwareCustomOrderEnabled());
                hardwareCustomBoardOrder->setEnabled(true);
            }
            const QVector<WinUaeQtHardwareBoard> boards = hardwareProvider.boards(hardwareProvider.context);
            for (const WinUaeQtHardwareBoard &board : boards) {
                addHardwareBoardRow(board.type, board.name, board.start, board.end, board.size, board.id, board.index, board.movable);
            }
            for (int i = 0; i < hardwareBoardList->columnCount(); i++) {
                hardwareBoardList->resizeColumnToContents(i);
            }
            updateHardwareMoveButtons();
            return;
        }

        const quint64 chipBytes = bytesFromMemoryText(chipMem ? chipMem->currentText() : QString());
        if (chipBytes) {
            addHardwareBoardRow(
                QStringLiteral("-"),
                QStringLiteral("Chip memory"),
                QStringLiteral("0x00000000"),
                QStringLiteral("0x%1").arg(chipBytes - 1, 8, 16, QLatin1Char('0')),
                hardwareSizeText(chipBytes),
                QStringLiteral("-"));
        }

        const quint64 slowBytes = bytesFromMemoryText(slowMem ? slowMem->currentText() : QString());
        if (slowBytes) {
            addHardwareBoardRow(QStringLiteral("-"), QStringLiteral("Slow memory"), QStringLiteral("-"), QStringLiteral("-"), hardwareSizeText(slowBytes), QStringLiteral("-"));
        }

        const quint64 z2Bytes = bytesFromMemoryText(z2Fast ? z2Fast->currentText() : QString());
        if (z2Bytes) {
            addHardwareBoardRow(QStringLiteral("Z2"), QStringLiteral("Fast memory"), QStringLiteral("-"), QStringLiteral("-"), hardwareSizeText(z2Bytes), QStringLiteral("-"));
        }

        const quint64 z3Bytes = bytesFromMemoryText(z3Fast ? z3Fast->currentText() : QString());
        if (z3Bytes) {
            addHardwareBoardRow(QStringLiteral("Z3"), QStringLiteral("Fast memory"), QStringLiteral("-"), QStringLiteral("-"), hardwareSizeText(z3Bytes), QStringLiteral("-"));
        }

        const quint64 z3ChipBytes = bytesFromMemoryText(z3ChipMem ? z3ChipMem->currentText() : QString());
        if (z3ChipBytes) {
            addHardwareBoardRow(QStringLiteral("Z3"), QStringLiteral("Z3 chip memory"), QStringLiteral("-"), QStringLiteral("-"), hardwareSizeText(z3ChipBytes), QStringLiteral("-"));
        }

        const quint64 processorBytes = bytesFromMemoryText(processorSlotMem ? processorSlotMem->currentText() : QString());
        if (processorBytes) {
            addHardwareBoardRow(QStringLiteral("-"), QStringLiteral("Processor slot memory"), QStringLiteral("-"), QStringLiteral("-"), hardwareSizeText(processorBytes), QStringLiteral("-"));
        }

        const quint64 rtgBytes = bytesFromMemoryText(rtgMem ? rtgMem->currentText() : QString());
        if (rtgBytes) {
            addHardwareBoardRow(
                rtgBusName(rtgType ? rtgType->currentText() : QString()),
                QStringLiteral("RTG graphics card"),
                QStringLiteral("-"),
                QStringLiteral("-"),
                hardwareSizeText(rtgBytes),
                QStringLiteral("gfxcard"));
        }

        storeCurrentCustomRomBoard();
        for (int i = 0; i < customRomBoards.size(); i++) {
            const WinUaeQtRomBoard &board = customRomBoards[i];
            if (board.start.isEmpty() || board.end.isEmpty()) {
                continue;
            }
            bool startOk = false;
            bool endOk = false;
            const quint64 start = board.start.toULongLong(&startOk, 16);
            const quint64 end = board.end.toULongLong(&endOk, 16);
            const quint64 size = startOk && endOk && end >= start ? end - start + 1 : 0;
            addHardwareBoardRow(
                QStringLiteral("-"),
                QStringLiteral("Custom ROM board #%1").arg(i + 1),
                QStringLiteral("0x%1").arg(start, 8, 16, QLatin1Char('0')),
                QStringLiteral("0x%1").arg(end, 8, 16, QLatin1Char('0')),
                hardwareSizeText(size),
                board.path.isEmpty() ? QStringLiteral("-") : QFileInfo(board.path).fileName());
        }

        for (int i = 0; i < hardwareBoardList->columnCount(); i++) {
            hardwareBoardList->resizeColumnToContents(i);
        }
        if (hardwareMoveUp) {
            hardwareMoveUp->setEnabled(false);
        }
        if (hardwareMoveDown) {
            hardwareMoveDown->setEnabled(false);
        }
    }

    QWidget *makeDisplayPage()
    {
        QWidget *page = makePage();
        QHBoxLayout *root = new QHBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);

        QVBoxLayout *left = new QVBoxLayout;
        windowWidth = new QLineEdit(QStringLiteral("720"));
        windowHeight = new QLineEdit(QStringLiteral("568"));
        windowResize = new QCheckBox(QStringLiteral("Window resize"));
        fullscreenResolution = combo({ QStringLiteral("Native"), QStringLiteral("640x480"), QStringLiteral("800x600"), QStringLiteral("1024x768"), QStringLiteral("1280x720"), QStringLiteral("1920x1080") });
        fullscreenResolution->setEditable(true);
        displayRefreshRate = combo({ QStringLiteral("Default"), QStringLiteral("50"), QStringLiteral("60"), QStringLiteral("70"), QStringLiteral("75"), QStringLiteral("120"), QStringLiteral("144") }, QStringLiteral("Default"));
        displayBufferCount = combo({ QStringLiteral("Double"), QStringLiteral("Triple") }, QStringLiteral("Double"));
        hostDisplay = new QComboBox;
        const QVector<QPair<QString, QString>> displays = displayDeviceChoices();
        for (const QPair<QString, QString> &display : displays) {
            hostDisplay->addItem(display.first, display.second);
            hostDisplay->setItemData(hostDisplay->count() - 1, displayFriendlyName(display.first), Qt::UserRole + 1);
        }
        QGridLayout *screen = new QGridLayout;
        screen->setColumnStretch(1, 1);
        screen->addWidget(hostDisplay, 0, 0, 1, 4);
        screen->addWidget(label(QStringLiteral("Fullscreen:")), 1, 0);
        screen->addWidget(fullscreenResolution, 1, 1, 1, 2);
        screen->addWidget(displayRefreshRate, 1, 3);
        screen->addWidget(label(QStringLiteral("Windowed:")), 2, 0);
        screen->addWidget(windowWidth, 2, 1);
        screen->addWidget(windowHeight, 2, 2);
        screen->addWidget(displayBufferCount, 2, 3);
        screen->addWidget(windowResize, 3, 1, 1, 3);
        left->addWidget(groupBox(QStringLiteral("Screen"), screen));

        nativeMode = combo({ QStringLiteral("Windowed"), QStringLiteral("Fullscreen"), QStringLiteral("Full-window") }, QStringLiteral("Windowed"));
        nativeVsync = combo({
            QStringLiteral("No vsync"),
            QStringLiteral("VSync (Busy wait)"),
            QStringLiteral("VSync autoswitch (Busy wait)"),
            QStringLiteral("VSync"),
            QStringLiteral("VSync autoswitch")
        }, QStringLiteral("No vsync"));
        nativeFrameSlices = combo({ QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4") }, QStringLiteral("4"));
        rtgMode = combo({ QStringLiteral("Windowed"), QStringLiteral("Fullscreen"), QStringLiteral("Full-window") }, QStringLiteral("Windowed"));
        rtgVsync = combo({ QStringLiteral("-"), QStringLiteral("VSync (Busy wait)") }, QStringLiteral("-"));
        displayResolution = combo({ QStringLiteral("lores"), QStringLiteral("hires"), QStringLiteral("superhires") }, QStringLiteral("hires"));
        displayOverscan = combo({
            QStringLiteral("TV (narrow)"),
            QStringLiteral("TV (standard)"),
            QStringLiteral("TV (wide)"),
            QStringLiteral("Overscan"),
            QStringLiteral("Overscan+"),
            QStringLiteral("Extreme"),
            QStringLiteral("Ultra extreme debug"),
            QStringLiteral("Ultra extreme debug (HV)"),
            QStringLiteral("Ultra extreme debug (C)")
        }, QStringLiteral("Overscan"));
        displayAutoResolution = combo({ QStringLiteral("Disabled"), QStringLiteral("Always on"), QStringLiteral("10%"), QStringLiteral("33%"), QStringLiteral("66%"), QStringLiteral("100%") }, QStringLiteral("Disabled"));
        displayFrameRate = new QSpinBox;
        displayFrameRate->setRange(1, 20);
        displayFlickerFixer = new QCheckBox(QStringLiteral("Remove interlace artifacts"));
        displayLoresSmoothed = new QCheckBox(QStringLiteral("Filtered low resolution"));
        displayBlackerThanBlack = new QCheckBox(QStringLiteral("Blacker than black"));
        displayMonochrome = new QCheckBox(QStringLiteral("Monochrome video out"));
        displayAutoResolutionVga = new QCheckBox(QStringLiteral("VGA mode resolution autoswitch"));
        displayResyncBlank = new QCheckBox(QStringLiteral("Display resync blanking"));
        QGridLayout *settings = new QGridLayout;
        settings->setColumnStretch(1, 1);
        settings->addWidget(label(QStringLiteral("Native:")), 0, 0);
        settings->addWidget(nativeMode, 0, 1);
        settings->addWidget(nativeVsync, 0, 2);
        settings->addWidget(nativeFrameSlices, 0, 3);
        settings->addWidget(label(QStringLiteral("RTG:")), 1, 0);
        settings->addWidget(rtgMode, 1, 1);
        settings->addWidget(rtgVsync, 1, 2, 1, 2);
        settings->addWidget(label(QStringLiteral("Resolution:")), 2, 0);
        settings->addWidget(displayResolution, 2, 1);
        settings->addWidget(label(QStringLiteral("Overscan:")), 2, 2);
        settings->addWidget(displayOverscan, 2, 3);
        settings->addWidget(label(QStringLiteral("Resolution autoswitch:")), 3, 0);
        settings->addWidget(displayAutoResolution, 3, 1);
        settings->addWidget(label(QStringLiteral("Refresh:")), 3, 2);
        settings->addWidget(displayFrameRate, 3, 3);
        settings->addWidget(displayBlackerThanBlack, 4, 1);
        settings->addWidget(displayLoresSmoothed, 4, 2, 1, 2);
        settings->addWidget(displayFlickerFixer, 5, 1);
        settings->addWidget(displayAutoResolutionVga, 5, 2, 1, 2);
        settings->addWidget(displayMonochrome, 6, 1);
        settings->addWidget(displayResyncBlank, 6, 2, 1, 2);
        left->addWidget(groupBox(QStringLiteral("Settings"), settings), 1);

        QVBoxLayout *right = new QVBoxLayout;
        QVBoxLayout *center = new QVBoxLayout;
        displayCenterHorizontal = new QCheckBox(QStringLiteral("Horizontal"));
        displayCenterVertical = new QCheckBox(QStringLiteral("Vertical"));
        center->addWidget(displayCenterHorizontal);
        center->addWidget(displayCenterVertical);
        right->addWidget(groupBox(QStringLiteral("Centering"), center));
        QVBoxLayout *aspect = new QVBoxLayout;
        displayKeepAspect = new QCheckBox(QStringLiteral("Automatic integer scaling"));
        aspect->addWidget(displayKeepAspect);
        right->addWidget(groupBox(QStringLiteral("Aspect ratio"), aspect));
        QVBoxLayout *lineMode = new QVBoxLayout;
        displayLineModeButtons = new QButtonGroup(this);
        QRadioButton *singleLine = new QRadioButton(QStringLiteral("Single"));
        QRadioButton *doubleLine = new QRadioButton(QStringLiteral("Double"));
        QRadioButton *scanlines = new QRadioButton(QStringLiteral("Scanlines"));
        QRadioButton *doubleFields = new QRadioButton(QStringLiteral("Double, fields"));
        QRadioButton *doubleFieldsPlus = new QRadioButton(QStringLiteral("Double, fields+"));
        displayLineModeButtons->addButton(singleLine, 0);
        displayLineModeButtons->addButton(doubleLine, 1);
        displayLineModeButtons->addButton(scanlines, 2);
        displayLineModeButtons->addButton(doubleFields, 3);
        displayLineModeButtons->addButton(doubleFieldsPlus, 4);
        doubleLine->setChecked(true);
        lineMode->addWidget(singleLine);
        lineMode->addWidget(doubleLine);
        lineMode->addWidget(scanlines);
        lineMode->addWidget(doubleFields);
        lineMode->addWidget(doubleFieldsPlus);
        right->addWidget(groupBox(QStringLiteral("Line mode"), lineMode));
        QVBoxLayout *interlacedLineMode = new QVBoxLayout;
        displayInterlacedLineModeButtons = new QButtonGroup(this);
        QRadioButton *interlacedSingle = new QRadioButton(QStringLiteral("Single"));
        QRadioButton *interlacedFrames = new QRadioButton(QStringLiteral("Double, frames"));
        QRadioButton *interlacedFields = new QRadioButton(QStringLiteral("Double, fields"));
        QRadioButton *interlacedFieldsPlus = new QRadioButton(QStringLiteral("Double, fields+"));
        displayInterlacedLineModeButtons->addButton(interlacedSingle, 0);
        displayInterlacedLineModeButtons->addButton(interlacedFrames, 1);
        displayInterlacedLineModeButtons->addButton(interlacedFields, 2);
        displayInterlacedLineModeButtons->addButton(interlacedFieldsPlus, 3);
        interlacedFrames->setChecked(true);
        interlacedLineMode->addWidget(interlacedSingle);
        interlacedLineMode->addWidget(interlacedFrames);
        interlacedLineMode->addWidget(interlacedFields);
        interlacedLineMode->addWidget(interlacedFieldsPlus);
        right->addWidget(groupBox(QStringLiteral("Interlaced line mode"), interlacedLineMode));
        right->addStretch();

        root->addLayout(left, 3);
        root->addLayout(right, 1);
        return page;
    }

    QWidget *makeFilterPage()
    {
        QWidget *page = makePage();
        QVBoxLayout *outer = new QVBoxLayout(page);
        outer->setContentsMargins(0, 0, 0, 0);

        QWidget *content = new QWidget;
        QVBoxLayout *root = new QVBoxLayout(content);
        root->setContentsMargins(4, 4, 4, 4);
        root->setSpacing(6);

        auto doubleSpin = [](double minimum, double maximum, double step, int decimals) {
            QDoubleSpinBox *spin = new QDoubleSpinBox;
            spin->setRange(minimum, maximum);
            spin->setSingleStep(step);
            spin->setDecimals(decimals);
            spin->setAlignment(Qt::AlignRight);
            return spin;
        };
        auto intSpin = [](int minimum, int maximum) {
            QSpinBox *spin = new QSpinBox;
            spin->setRange(minimum, maximum);
            spin->setAlignment(Qt::AlignRight);
            return spin;
        };

        filterTarget = combo(configChoiceDisplays(filterTargetChoices, int(sizeof(filterTargetChoices) / sizeof(filterTargetChoices[0]))), QStringLiteral("Native"));
        filterEnable = new QCheckBox(QStringLiteral("Enabled"));
        filterMode = combo(configChoiceDisplays(filterModeChoices, int(sizeof(filterModeChoices) / sizeof(filterModeChoices[0]))), QStringLiteral("None"));
        filterModeH = combo(configChoiceDisplays(filterModeHChoices, int(sizeof(filterModeHChoices) / sizeof(filterModeHChoices[0]))), QStringLiteral("1x"));
        filterModeV = combo(configChoiceDisplays(filterModeVChoices, int(sizeof(filterModeVChoices) / sizeof(filterModeVChoices[0]))), QStringLiteral("-"));
        if (!unixShaderPipelineAvailable()) {
            const QString reason = QStringLiteral("Host shader/filter mode selection is not implemented by the Unix graphics backend yet.");
            disableUnavailable(filterMode, reason);
            disableUnavailable(filterModeH, reason);
            disableUnavailable(filterModeV, reason);
        }
        filterAutoscale = combo({});
        filterIntegerLimit = combo(configChoiceDisplays(filterIntegerLimitChoices, int(sizeof(filterIntegerLimitChoices) / sizeof(filterIntegerLimitChoices[0]))), QStringLiteral("1/1"));
        QPushButton *reset = new QPushButton(QStringLiteral("Reset to defaults"));

        QGridLayout *settings = new QGridLayout;
        settings->setColumnStretch(1, 1);
        settings->setColumnStretch(3, 1);
        settings->addWidget(label(QStringLiteral("Target:")), 0, 0);
        settings->addWidget(filterTarget, 0, 1);
        settings->addWidget(filterEnable, 0, 2);
        settings->addWidget(reset, 0, 3);
        settings->addWidget(label(QStringLiteral("Filter:")), 1, 0);
        settings->addWidget(filterMode, 1, 1);
        settings->addWidget(label(QStringLiteral("H/V mode:")), 1, 2);
        QHBoxLayout *modes = new QHBoxLayout;
        modes->setContentsMargins(0, 0, 0, 0);
        modes->addWidget(filterModeH);
        modes->addWidget(filterModeV);
        settings->addLayout(modes, 1, 3);
        settings->addWidget(label(QStringLiteral("Autoscale:")), 2, 0);
        settings->addWidget(filterAutoscale, 2, 1);
        settings->addWidget(label(QStringLiteral("Integer limit:")), 2, 2);
        settings->addWidget(filterIntegerLimit, 2, 3);
        root->addWidget(groupBox(QStringLiteral("Filter Settings"), settings));

        filterHorizZoomMult = doubleSpin(0.25, 8.0, 0.25, 2);
        filterVertZoomMult = doubleSpin(0.25, 8.0, 0.25, 2);
        filterHorizZoom = doubleSpin(-9999.0, 9999.0, 1.0, 0);
        filterVertZoom = doubleSpin(-9999.0, 9999.0, 1.0, 0);
        filterHorizOffset = doubleSpin(-9999.0, 9999.0, 1.0, 0);
        filterVertOffset = doubleSpin(-9999.0, 9999.0, 1.0, 0);
        QGridLayout *geometry = new QGridLayout;
        geometry->setColumnStretch(1, 1);
        geometry->setColumnStretch(3, 1);
        geometry->addWidget(label(QStringLiteral("Horiz. size:")), 0, 0);
        geometry->addWidget(filterHorizZoomMult, 0, 1);
        geometry->addWidget(filterHorizZoom, 0, 2);
        geometry->addWidget(label(QStringLiteral("Vert. size:")), 1, 0);
        geometry->addWidget(filterVertZoomMult, 1, 1);
        geometry->addWidget(filterVertZoom, 1, 2);
        geometry->addWidget(label(QStringLiteral("Horiz. position:")), 0, 3);
        geometry->addWidget(filterHorizOffset, 0, 4);
        geometry->addWidget(label(QStringLiteral("Vert. position:")), 1, 3);
        geometry->addWidget(filterVertOffset, 1, 4);
        root->addWidget(groupBox(QStringLiteral("Size and Position"), geometry));

        filterKeepAutoscaleAspect = new QCheckBox(QStringLiteral("Keep autoscale aspect"));
        filterKeepAspect = new QCheckBox(QStringLiteral("Keep aspect ratio"));
        filterAspect = combo(configChoiceDisplays(filterAspectChoices, int(sizeof(filterAspectChoices) / sizeof(filterAspectChoices[0]))), QStringLiteral("VGA"));
        filterNtscPixels = new QCheckBox(QStringLiteral("Always stretch NTSC mode"));
        QGridLayout *aspect = new QGridLayout;
        aspect->setColumnStretch(2, 1);
        aspect->addWidget(filterKeepAutoscaleAspect, 0, 0, 1, 2);
        aspect->addWidget(filterKeepAspect, 1, 0);
        aspect->addWidget(filterAspect, 1, 1);
        aspect->addWidget(filterNtscPixels, 2, 0, 1, 2);
        root->addWidget(groupBox(QStringLiteral("Aspect Ratio Correction"), aspect));

        filterBilinear = new QCheckBox(QStringLiteral("Point/Bilinear"));
        filterScanlines = intSpin(0, 1000);
        filterScanlineLevel = intSpin(0, 1000);
        filterScanlineOffset = intSpin(-1000, 1000);
        filterLuminance = intSpin(-1000, 1000);
        filterContrast = intSpin(-1000, 1000);
        filterSaturation = intSpin(-1000, 1000);
        filterGamma = intSpin(-1000, 1000);
        filterBlur = intSpin(0, 1000);
        filterNoise = intSpin(0, 1000);
        if (!unixShaderPipelineAvailable()) {
            const QString reason = QStringLiteral("This color/noise filter control is not implemented by the Unix graphics backend yet.");
            for (QWidget *widget : { static_cast<QWidget *>(filterLuminance), static_cast<QWidget *>(filterContrast), static_cast<QWidget *>(filterSaturation), static_cast<QWidget *>(filterGamma), static_cast<QWidget *>(filterBlur), static_cast<QWidget *>(filterNoise) }) {
                disableUnavailable(widget, reason);
            }
        }
        QGridLayout *extra = new QGridLayout;
        extra->setColumnStretch(1, 1);
        extra->setColumnStretch(3, 1);
        extra->addWidget(filterBilinear, 0, 0, 1, 2);
        extra->addWidget(label(QStringLiteral("Scanline opacity:")), 1, 0);
        extra->addWidget(filterScanlines, 1, 1);
        extra->addWidget(label(QStringLiteral("Scanline level:")), 1, 2);
        extra->addWidget(filterScanlineLevel, 1, 3);
        extra->addWidget(label(QStringLiteral("Scanline offset:")), 2, 0);
        extra->addWidget(filterScanlineOffset, 2, 1);
        extra->addWidget(label(QStringLiteral("Brightness:")), 3, 0);
        extra->addWidget(filterLuminance, 3, 1);
        extra->addWidget(label(QStringLiteral("Contrast:")), 3, 2);
        extra->addWidget(filterContrast, 3, 3);
        extra->addWidget(label(QStringLiteral("Saturation:")), 4, 0);
        extra->addWidget(filterSaturation, 4, 1);
        extra->addWidget(label(QStringLiteral("Gamma:")), 4, 2);
        extra->addWidget(filterGamma, 4, 3);
        extra->addWidget(label(QStringLiteral("Blurriness:")), 5, 0);
        extra->addWidget(filterBlur, 5, 1);
        extra->addWidget(label(QStringLiteral("Noise:")), 5, 2);
        extra->addWidget(filterNoise, 5, 3);
        root->addWidget(groupBox(QStringLiteral("Extra Settings"), extra));

        QGridLayout *presets = new QGridLayout;
        filterPresetName = combo({ QStringLiteral("") });
        filterPresetName->setEditable(true);
        filterPresetName->setInsertPolicy(QComboBox::NoInsert);
        filterPresetName->lineEdit()->setClearButtonEnabled(true);
        QPushButton *load = new QPushButton(QStringLiteral("Load"));
        QPushButton *save = new QPushButton(QStringLiteral("Save"));
        QPushButton *deletePreset = new QPushButton(QStringLiteral("Delete"));
        presets->setColumnStretch(0, 1);
        presets->addWidget(filterPresetName, 0, 0);
        presets->addWidget(load, 0, 1);
        presets->addWidget(save, 0, 2);
        presets->addWidget(deletePreset, 0, 3);
        root->addWidget(groupBox(QStringLiteral("Presets"), presets));
        root->addStretch(1);

        QScrollArea *scroll = new QScrollArea;
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setWidget(content);
        outer->addWidget(scroll);

        connect(filterTarget, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
            if (filterUpdating || index < 0 || index > 2) {
                return;
            }
            storeFilterUiToState();
            currentFilterTarget = index;
            loadFilterStateToUi(index);
        });
        connect(filterKeepAspect, &QCheckBox::toggled, this, [this]() { updateFilterControlState(); });
        connect(reset, &QPushButton::clicked, this, [this]() {
            filterStates[currentFilterTarget] = defaultFilterState(currentFilterTarget);
            loadFilterStateToUi(currentFilterTarget);
        });
        connect(load, &QPushButton::clicked, this, [this]() { loadFilterPreset(); });
        connect(save, &QPushButton::clicked, this, [this]() { saveFilterPreset(); });
        connect(deletePreset, &QPushButton::clicked, this, [this]() { deleteFilterPreset(); });

        resetFilterStates();
        refreshFilterPresetList();
        return page;
    }

    QWidget *makeOutputPage()
    {
        QWidget *page = makePage();
        QVBoxLayout *root = new QVBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);
        root->setSpacing(6);

        outputFile = new QLineEdit;
        outputFile->setReadOnly(true);
        QPushButton *browse = smallButton(QStringLiteral("..."));
        outputAudio = new QCheckBox(QStringLiteral("Audio"));
        outputVideo = new QCheckBox(QStringLiteral("Video"));
        outputAudioCodec = combo(QStringList{
            QStringLiteral("PCM in AVI (internal)"),
            QStringLiteral("Wave file (internal)")
        });
        outputVideoCodec = combo(QStringList{ QStringLiteral("DIB RGB (internal)") });
        outputFrameLimiter = new QCheckBox(QStringLiteral("Disable frame rate limit"));
        outputOriginalSize = new QCheckBox(QStringLiteral("Capture before filtering"));
        outputNoSound = new QCheckBox(QStringLiteral("Disable sound output"));
        outputNoSoundSync = new QCheckBox(QStringLiteral("Disable sound sync"));
        outputEnabled = new QCheckBox(QStringLiteral("AVI output enabled"));

        QGridLayout *properties = new QGridLayout;
        properties->setColumnStretch(1, 1);
        properties->addWidget(outputFile, 0, 0, 1, 3);
        properties->addWidget(browse, 0, 3);
        properties->addWidget(outputAudio, 1, 0);
        properties->addWidget(outputAudioCodec, 1, 1, 1, 3);
        properties->addWidget(outputVideo, 2, 0);
        properties->addWidget(outputVideoCodec, 2, 1, 1, 3);
        properties->addWidget(outputFrameLimiter, 3, 0, 1, 2);
        properties->addWidget(outputOriginalSize, 3, 2, 1, 2);
        properties->addWidget(outputNoSound, 4, 0, 1, 2);
        properties->addWidget(outputNoSoundSync, 4, 2, 1, 2);
        properties->addWidget(outputEnabled, 5, 0, 1, 2);
        root->addWidget(groupBox(QStringLiteral("Output Properties"), properties));

        QPushButton *saveScreenshot = new QPushButton(QStringLiteral("Save screenshot"));
        QPushButton *proWizard = new QPushButton(QStringLiteral("Pro Wizard 1.62"));
        QCheckBox *sampleRipper = new QCheckBox(QStringLiteral("Sample ripper"));
        if (hardwareProvider.saveScreenshot) {
            connect(saveScreenshot, &QPushButton::clicked, this, [this]() {
                hardwareProvider.saveScreenshot(hardwareProvider.context);
                if (status) {
                    status->setText(QStringLiteral("Screenshot requested."));
                }
            });
        } else {
            disableUnavailable(saveScreenshot, QStringLiteral("Screenshot capture is only available from the integrated runtime UI."));
        }
        if (hardwareProvider.runProWizard) {
            connect(proWizard, &QPushButton::clicked, this, [this]() {
                hardwareProvider.runProWizard(hardwareProvider.context);
                if (status) {
                    status->setText(QStringLiteral("Pro Wizard scan finished."));
                }
            });
        } else {
            disableUnavailable(proWizard, QStringLiteral("Pro Wizard is only available from the integrated runtime UI when Unix is built with Pro-Wizard support."));
        }
        if (hardwareProvider.sampleRipperEnabled && hardwareProvider.setSampleRipperEnabled) {
            sampleRipper->setChecked(hardwareProvider.sampleRipperEnabled(hardwareProvider.context));
            connect(sampleRipper, &QCheckBox::toggled, this, [this](bool enabled) {
                hardwareProvider.setSampleRipperEnabled(hardwareProvider.context, enabled);
                if (status) {
                    status->setText(enabled ? QStringLiteral("Sample ripper enabled.") : QStringLiteral("Sample ripper disabled."));
                }
            });
        } else {
            disableUnavailable(sampleRipper, QStringLiteral("Sample ripper integration is only available from the integrated emulator UI."));
        }
        screenshotOriginalSize = new QCheckBox(QStringLiteral("Take screenshot before filtering"));
        screenshotPaletted = new QCheckBox(QStringLiteral("Create 256 color palette indexed screenshot if possible"));
        screenshotClip = new QCheckBox(QStringLiteral("Autoclip screenshot"));
        screenshotAuto = new QCheckBox(QStringLiteral("Continuous screenshots"));
        QGridLayout *ripper = new QGridLayout;
        ripper->setColumnStretch(3, 1);
        ripper->addWidget(saveScreenshot, 0, 0);
        ripper->addWidget(proWizard, 0, 1);
        ripper->addWidget(sampleRipper, 0, 2);
        ripper->addWidget(screenshotOriginalSize, 1, 0, 1, 2);
        ripper->addWidget(screenshotClip, 1, 2, 1, 2);
        ripper->addWidget(screenshotPaletted, 2, 0, 1, 2);
        ripper->addWidget(screenshotAuto, 2, 2, 1, 2);
        root->addWidget(groupBox(QStringLiteral("Ripper"), ripper));

        QCheckBox *statePlay = new QCheckBox(QStringLiteral("Play recording"));
        QCheckBox *stateRecord = new QCheckBox(QStringLiteral("Re-recording enabled"));
        stateReplayAutoplay = new QCheckBox(QStringLiteral("Automatic replay"));
        QPushButton *stateSave = new QPushButton(QStringLiteral("Save recording"));
        stateReplayRate = combo({ QStringLiteral("-"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("5"), QStringLiteral("10"), QStringLiteral("20"), QStringLiteral("30"), QStringLiteral("60") }, QStringLiteral("5"));
        stateReplayRate->setEditable(true);
        stateReplayBuffers = combo({ QStringLiteral("50"), QStringLiteral("100"), QStringLiteral("500"), QStringLiteral("1000"), QStringLiteral("10000") }, QStringLiteral("100"));
        stateReplayBuffers->setEditable(true);
        const bool hasStateRecorder = hardwareProvider.statePlaybackEnabled
            && hardwareProvider.stateRecordingEnabled
            && hardwareProvider.canSaveStateRecording
            && hardwareProvider.setStatePlayback
            && hardwareProvider.toggleStateRecording
            && hardwareProvider.saveStateRecording;
        auto updateStateRecorderControls = [this, statePlay, stateRecord, stateSave, hasStateRecorder]() {
            if (!hasStateRecorder) {
                return;
            }
            const QSignalBlocker playBlocker(statePlay);
            const QSignalBlocker recordBlocker(stateRecord);
            statePlay->setChecked(hardwareProvider.statePlaybackEnabled(hardwareProvider.context));
            stateRecord->setChecked(hardwareProvider.stateRecordingEnabled(hardwareProvider.context));
            stateSave->setEnabled(hardwareProvider.canSaveStateRecording(hardwareProvider.context));
        };
        if (hasStateRecorder) {
            connect(statePlay, &QCheckBox::clicked, this, [this, statePlay, updateStateRecorderControls](bool checked) {
                if (checked) {
                    const QString selected = QFileDialog::getOpenFileName(
                        this,
                        QStringLiteral("Select input recording"),
                        expandedPathText(unixDefaultDataSubPath(QStringLiteral("Save States"))),
                        QStringLiteral("Input recordings (*.inp);;All files (*)"));
                    if (!selected.isEmpty()) {
                        const QByteArray path = selected.toLocal8Bit();
                        if (hardwareProvider.setStatePlayback(hardwareProvider.context, true, path.constData())) {
                            if (status) {
                                status->setText(QStringLiteral("State recording playback requested."));
                            }
                        } else if (status) {
                            status->setText(QStringLiteral("State recording playback failed."));
                        }
                    }
                } else {
                    hardwareProvider.setStatePlayback(hardwareProvider.context, false, nullptr);
                    if (status) {
                        status->setText(QStringLiteral("State recording playback stopped."));
                    }
                }
                updateStateRecorderControls();
            });
            connect(stateRecord, &QCheckBox::clicked, this, [this, updateStateRecorderControls]() {
                hardwareProvider.toggleStateRecording(hardwareProvider.context);
                updateStateRecorderControls();
                if (status) {
                    status->setText(hardwareProvider.stateRecordingEnabled(hardwareProvider.context)
                        ? QStringLiteral("State recording enabled.")
                        : QStringLiteral("State recording disabled."));
                }
            });
            connect(stateSave, &QPushButton::clicked, this, [this, updateStateRecorderControls]() {
                if (!hardwareProvider.canSaveStateRecording(hardwareProvider.context)) {
                    if (status) {
                        status->setText(QStringLiteral("No re-recording is available to save."));
                    }
                    updateStateRecorderControls();
                    return;
                }
                const QString selected = QFileDialog::getSaveFileName(
                    this,
                    QStringLiteral("Save input recording"),
                    QDir(expandedPathText(unixDefaultDataSubPath(QStringLiteral("Save States")))).filePath(QStringLiteral("recording.inp")),
                    QStringLiteral("Input recordings (*.inp);;All files (*)"));
                if (!selected.isEmpty()) {
                    QDir().mkpath(QFileInfo(selected).absolutePath());
                    const QByteArray path = selected.toLocal8Bit();
                    if (hardwareProvider.saveStateRecording(hardwareProvider.context, path.constData())) {
                        if (status) {
                            status->setText(QStringLiteral("State recording saved."));
                        }
                    } else if (status) {
                        status->setText(QStringLiteral("State recording save failed."));
                    }
                }
                updateStateRecorderControls();
            });
            updateStateRecorderControls();
        } else {
            disableUnavailable(statePlay, QStringLiteral("State recording playback is only available from the integrated runtime UI."));
            disableUnavailable(stateRecord, QStringLiteral("State recording is only available from the integrated runtime UI."));
            disableUnavailable(stateSave, QStringLiteral("State recording export is only available from the integrated runtime UI."));
        }
        QGridLayout *recorder = new QGridLayout;
        recorder->setColumnStretch(4, 1);
        recorder->addWidget(statePlay, 0, 0);
        recorder->addWidget(stateRecord, 0, 2);
        recorder->addWidget(stateReplayAutoplay, 1, 0);
        recorder->addWidget(stateSave, 1, 2);
        recorder->addWidget(label(QStringLiteral("Recording rate (seconds):")), 2, 0);
        recorder->addWidget(stateReplayRate, 2, 1);
        recorder->addWidget(label(QStringLiteral("Recording buffers:")), 2, 2);
        recorder->addWidget(stateReplayBuffers, 2, 3);
        root->addWidget(groupBox(QStringLiteral("Re-recorder"), recorder));
        root->addStretch(1);

        connect(browse, &QPushButton::clicked, this, [this]() {
            const QString selected = QFileDialog::getSaveFileName(this, QStringLiteral("Select output file"), fileDialogInitialPath(outputFile->text()), QStringLiteral("Video Clip (*.avi);;Wave Sound (*.wav);;All files (*)"));
            if (!selected.isEmpty()) {
                outputFile->setText(selected);
            }
        });
        connect(outputAudio, &QCheckBox::toggled, this, [this](bool) { updateOutputControlState(); });
        connect(outputVideo, &QCheckBox::toggled, this, [this](bool) { updateOutputControlState(); });
        connect(outputAudioCodec, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { updateOutputControlState(); });
        connect(outputFrameLimiter, &QCheckBox::toggled, this, [this](bool) { updateOutputControlState(); });
        connect(screenshotOriginalSize, &QCheckBox::toggled, this, [this](bool) { updateOutputControlState(); });
        updateOutputControlState();
        return page;
    }

    void updateOutputControlState()
    {
        if (!screenshotOriginalSize || !screenshotClip || !outputFrameLimiter || !outputNoSound ||
            !outputAudio || !outputVideo || !outputEnabled || !outputAudioCodec || !outputVideoCodec) {
            return;
        }
        const bool waveOutput = outputAudio->isChecked() && outputAudioCodec->currentIndex() == 1;
        outputAudioCodec->setEnabled(outputAudio->isChecked());
        outputVideoCodec->setEnabled(outputVideo->isChecked() && !waveOutput);
        outputVideo->setEnabled(!waveOutput);
        if (waveOutput && outputVideo->isChecked()) {
            outputVideo->setChecked(false);
        }
        const bool hasOutputStream = outputAudio->isChecked() || outputVideo->isChecked();
        outputEnabled->setEnabled(hasOutputStream);
        if (!hasOutputStream) {
            outputEnabled->setChecked(false);
        }
        outputNoSound->setEnabled(!outputFrameLimiter->isChecked());
        if (outputFrameLimiter->isChecked()) {
            outputNoSound->setChecked(true);
        }
        screenshotClip->setEnabled(screenshotOriginalSize->isChecked());
        if (!screenshotOriginalSize->isChecked()) {
            screenshotClip->setChecked(false);
        }
    }

    QWidget *makeSoundPage()
    {
        QWidget *page = makePage();
        QVBoxLayout *root = new QVBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);
        soundDevice = new QComboBox;
        for (const auto &choice : soundDeviceChoices()) {
            soundDevice->addItem(choice.first, choice.second);
        }
        root->addWidget(soundDevice);

        QHBoxLayout *top = new QHBoxLayout;
        QVBoxLayout *emulation = new QVBoxLayout;
        soundOutputButtons = new QButtonGroup(this);
        QRadioButton *soundDisabled = new QRadioButton(QStringLiteral("Disabled"));
        QRadioButton *soundEmulated = new QRadioButton(QStringLiteral("Disabled, but emulated"));
        QRadioButton *soundEnabled = new QRadioButton(QStringLiteral("Enabled"));
        soundOutputButtons->addButton(soundDisabled, 0);
        soundOutputButtons->addButton(soundEmulated, 1);
        soundOutputButtons->addButton(soundEnabled, 2);
        soundEnabled->setChecked(true);
        soundAutomatic = new QCheckBox(QStringLiteral("Automatic switching"));
        emulation->addWidget(soundDisabled);
        emulation->addWidget(soundEmulated);
        emulation->addWidget(soundEnabled);
        emulation->addSpacing(8);
        emulation->addWidget(soundAutomatic);
        top->addWidget(groupBox(QStringLiteral("Sound Emulation"), emulation), 1);

        QGridLayout *volume = new QGridLayout;
        volume->setColumnStretch(1, 1);
        soundMasterVolume = new QSlider(Qt::Horizontal);
        soundMasterVolume->setRange(0, 100);
        soundMasterVolumeValue = new QLabel;
        soundMasterVolumeValue->setMinimumWidth(44);
        soundVolumeSelect = combo({ QStringLiteral("Paula"), QStringLiteral("CD"), QStringLiteral("AHI"), QStringLiteral("MIDI"), QStringLiteral("Genlock") }, QStringLiteral("Paula"));
        soundSelectedVolume = new QSlider(Qt::Horizontal);
        soundSelectedVolume->setRange(0, 100);
        soundSelectedVolumeValue = new QLabel;
        soundSelectedVolumeValue->setMinimumWidth(44);
        volume->addWidget(label(QStringLiteral("Master")), 0, 0);
        volume->addWidget(soundMasterVolume, 0, 1);
        volume->addWidget(soundMasterVolumeValue, 0, 2);
        volume->addWidget(soundVolumeSelect, 1, 0);
        volume->addWidget(soundSelectedVolume, 1, 1);
        volume->addWidget(soundSelectedVolumeValue, 1, 2);
        top->addWidget(groupBox(QStringLiteral("Volume"), volume), 2);

        QGridLayout *buffer = new QGridLayout;
        buffer->setColumnStretch(0, 1);
        soundBufferSize = new QSlider(Qt::Horizontal);
        soundBufferSize->setRange(0, 10);
        soundBufferSizeValue = new QLabel;
        soundBufferSizeValue->setMinimumWidth(44);
        buffer->addWidget(soundBufferSize, 0, 0);
        buffer->addWidget(soundBufferSizeValue, 0, 1);
        top->addWidget(groupBox(QStringLiteral("Sound Buffer Size"), buffer), 1);
        root->addLayout(top);

        soundChannels = combo(soundChannelItems(), QStringLiteral("Stereo"));
        soundStereoSeparation = combo({});
        for (int i = 10; i >= 0; i--) {
            soundStereoSeparation->addItem(QStringLiteral("%1%").arg(i * 10));
        }
        soundInterpolation = combo({ QStringLiteral("Disabled"), QStringLiteral("Anti"), QStringLiteral("Sinc"), QStringLiteral("RH"), QStringLiteral("Crux") }, QStringLiteral("Anti"));
        soundFrequency = combo({ QStringLiteral("11025"), QStringLiteral("15000"), QStringLiteral("22050"), QStringLiteral("32000"), QStringLiteral("44100"), QStringLiteral("48000") }, QStringLiteral("44100"));
        soundFrequency->setEditable(true);
        soundSwap = combo({ QStringLiteral("-"), QStringLiteral("Paula only"), QStringLiteral("AHI only"), QStringLiteral("Both") }, QStringLiteral("-"));
        soundStereoDelay = combo({ QStringLiteral("-"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7"), QStringLiteral("8"), QStringLiteral("9"), QStringLiteral("10") }, QStringLiteral("-"));
        soundFilter = combo({ QStringLiteral("Always off"), QStringLiteral("Emulated (A500)"), QStringLiteral("Emulated (A1200)"), QStringLiteral("Always on (A500)"), QStringLiteral("Always on (A1200)") }, QStringLiteral("Emulated (A500)"));

        QGridLayout *settings = new QGridLayout;
        settings->setColumnStretch(1, 1);
        settings->setColumnStretch(3, 1);
        settings->setColumnStretch(5, 1);
        settings->addWidget(label(QStringLiteral("Channel mode:")), 0, 0);
        settings->addWidget(soundChannels, 0, 1);
        settings->addWidget(label(QStringLiteral("Stereo separation:")), 0, 2);
        settings->addWidget(soundStereoSeparation, 0, 3);
        settings->addWidget(label(QStringLiteral("Interpolation:")), 0, 4);
        settings->addWidget(soundInterpolation, 0, 5);
        settings->addWidget(label(QStringLiteral("Frequency:")), 1, 0);
        settings->addWidget(soundFrequency, 1, 1);
        settings->addWidget(label(QStringLiteral("Swap channels:")), 1, 2);
        settings->addWidget(soundSwap, 1, 3);
        settings->addWidget(label(QStringLiteral("Stereo delay:")), 1, 4);
        settings->addWidget(soundStereoDelay, 1, 5);
        settings->addWidget(label(QStringLiteral("Audio filter:")), 2, 4);
        settings->addWidget(soundFilter, 2, 5);
        root->addWidget(groupBox(QStringLiteral("Settings"), settings));

        QHBoxLayout *bottom = new QHBoxLayout;
        QGridLayout *floppy = new QGridLayout;
        floppy->setColumnStretch(1, 1);
        floppySoundEmptyVolume = new QSlider(Qt::Horizontal);
        floppySoundEmptyVolume->setRange(0, 100);
        floppySoundEmptyVolumeValue = new QLabel;
        floppySoundEmptyVolumeValue->setMinimumWidth(44);
        floppySoundDiskVolume = new QSlider(Qt::Horizontal);
        floppySoundDiskVolume->setRange(0, 100);
        floppySoundDiskVolumeValue = new QLabel;
        floppySoundDiskVolumeValue->setMinimumWidth(44);
        floppySoundType = combo({ QStringLiteral("No sound"), QStringLiteral("Built-in A500") }, QStringLiteral("No sound"));
        floppySoundDrive = combo({ QStringLiteral("DF0:"), QStringLiteral("DF1:"), QStringLiteral("DF2:"), QStringLiteral("DF3:") }, QStringLiteral("DF0:"));
        floppy->addWidget(label(QStringLiteral("Empty drive")), 0, 0);
        floppy->addWidget(floppySoundEmptyVolume, 0, 1);
        floppy->addWidget(floppySoundEmptyVolumeValue, 0, 2);
        floppy->addWidget(label(QStringLiteral("Disk in drive")), 1, 0);
        floppy->addWidget(floppySoundDiskVolume, 1, 1);
        floppy->addWidget(floppySoundDiskVolumeValue, 1, 2);
        floppy->addWidget(floppySoundType, 2, 0, 1, 2);
        floppy->addWidget(floppySoundDrive, 2, 2);
        bottom->addWidget(groupBox(QStringLiteral("Floppy Drive Sound Emulation"), floppy), 3);

        QVBoxLayout *drivers = new QVBoxLayout;
        QCheckBox *sdlDriver = new QCheckBox(QStringLiteral("SDL"));
        sdlDriver->setChecked(true);
        sdlDriver->setEnabled(false);
        drivers->addWidget(sdlDriver);
        drivers->addStretch();
        bottom->addWidget(groupBox(QStringLiteral("Drivers"), drivers), 1);
        root->addLayout(bottom);

        connect(soundOutputButtons, &QButtonGroup::idClicked, this, [this](int) { updateSoundControlState(); });
        connect(soundMasterVolume, &QSlider::valueChanged, this, [this](int) { updateSoundVolumeLabels(); });
        connect(soundVolumeSelect, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
            if (soundVolumeUpdating) {
                return;
            }
            storeSelectedSoundVolume();
            currentSoundVolume = qBound(0, index, SoundVolumeCount - 1);
            loadSelectedSoundVolume();
        });
        connect(soundSelectedVolume, &QSlider::valueChanged, this, [this](int) { updateSoundVolumeLabels(); });
        connect(soundBufferSize, &QSlider::valueChanged, this, [this](int) { updateSoundBufferLabel(); });
        connect(soundChannels, &QComboBox::currentTextChanged, this, [this](const QString &) { updateSoundControlState(); });
        connect(floppySoundDrive, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
            if (floppySoundUpdating) {
                return;
            }
            storeSelectedFloppySound();
            currentFloppySoundDrive = qBound(0, index, FloppySoundDriveCount - 1);
            loadSelectedFloppySound();
        });
        connect(floppySoundType, &QComboBox::currentTextChanged, this, [this](const QString &) { storeSelectedFloppySound(); });
        connect(floppySoundEmptyVolume, &QSlider::valueChanged, this, [this](int) {
            updateFloppySoundVolumeLabels();
            storeSelectedFloppySound();
        });
        connect(floppySoundDiskVolume, &QSlider::valueChanged, this, [this](int) {
            updateFloppySoundVolumeLabels();
            storeSelectedFloppySound();
        });
        updateSoundControlState();
        return page;
    }

    int soundVolumeAttenuationValue(int index) const
    {
        if (index == currentSoundVolume && soundSelectedVolume) {
            return 100 - soundSelectedVolume->value();
        }
        return soundVolumeAttenuation[qBound(0, index, SoundVolumeCount - 1)];
    }

    int floppySoundTypeConfigValue(int drive) const
    {
        if (drive == currentFloppySoundDrive && floppySoundType) {
            return floppySoundType->currentIndex();
        }
        return floppySoundTypeValue[qBound(0, drive, FloppySoundDriveCount - 1)];
    }

    int floppySoundEmptyAttenuationValue(int drive) const
    {
        if (drive == currentFloppySoundDrive && floppySoundEmptyVolume) {
            return 100 - floppySoundEmptyVolume->value();
        }
        return floppySoundEmptyAttenuation[qBound(0, drive, FloppySoundDriveCount - 1)];
    }

    int floppySoundDiskAttenuationValue(int drive) const
    {
        if (drive == currentFloppySoundDrive && floppySoundDiskVolume) {
            return 100 - floppySoundDiskVolume->value();
        }
        return floppySoundDiskAttenuation[qBound(0, drive, FloppySoundDriveCount - 1)];
    }

    void updateSoundControlState()
    {
        const bool enabled = soundOutputButtons && soundOutputButtons->checkedId() != 0;
        const bool stereo = enabled && soundChannels && soundChannels->currentText() != QStringLiteral("Mono");
        const QList<QWidget *> widgets = {
            soundMasterVolume,
            soundVolumeSelect,
            soundSelectedVolume,
            soundBufferSize,
            soundDevice,
            soundChannels,
            soundInterpolation,
            soundFrequency,
            soundSwap,
            soundFilter,
            floppySoundDrive,
            floppySoundType,
            floppySoundEmptyVolume,
            floppySoundDiskVolume
        };
        for (QWidget *widget : widgets) {
            if (widget) {
                widget->setEnabled(enabled);
            }
        }
        if (soundStereoSeparation) {
            soundStereoSeparation->setEnabled(stereo);
        }
        if (soundStereoDelay) {
            soundStereoDelay->setEnabled(stereo);
        }
        updateSoundVolumeLabels();
        updateSoundBufferLabel();
        updateFloppySoundVolumeLabels();
    }

    void updateSoundVolumeLabels()
    {
        if (soundMasterVolumeValue && soundMasterVolume) {
            soundMasterVolumeValue->setText(QStringLiteral("%1%").arg(soundMasterVolume->value()));
        }
        if (soundSelectedVolumeValue && soundSelectedVolume) {
            soundSelectedVolumeValue->setText(QStringLiteral("%1%").arg(soundSelectedVolume->value()));
        }
    }

    void updateSoundBufferLabel()
    {
        if (!soundBufferSizeValue || !soundBufferSize) {
            return;
        }
        const int index = soundBufferSize->value();
        soundBufferSizeValue->setText(index <= 0 ? QStringLiteral("Min") : QString::number(index));
    }

    void storeSelectedSoundVolume()
    {
        if (!soundSelectedVolume || soundVolumeUpdating) {
            return;
        }
        soundVolumeAttenuation[qBound(0, currentSoundVolume, SoundVolumeCount - 1)] = 100 - soundSelectedVolume->value();
    }

    void loadSelectedSoundVolume()
    {
        if (!soundSelectedVolume) {
            return;
        }
        QSignalBlocker blocker(soundSelectedVolume);
        soundVolumeUpdating = true;
        soundSelectedVolume->setValue(100 - soundVolumeAttenuation[qBound(0, currentSoundVolume, SoundVolumeCount - 1)]);
        soundVolumeUpdating = false;
        updateSoundVolumeLabels();
    }

    void updateFloppySoundVolumeLabels()
    {
        if (floppySoundEmptyVolumeValue && floppySoundEmptyVolume) {
            floppySoundEmptyVolumeValue->setText(QStringLiteral("%1%").arg(floppySoundEmptyVolume->value()));
        }
        if (floppySoundDiskVolumeValue && floppySoundDiskVolume) {
            floppySoundDiskVolumeValue->setText(QStringLiteral("%1%").arg(floppySoundDiskVolume->value()));
        }
    }

    void storeSelectedFloppySound()
    {
        if (!floppySoundType || !floppySoundEmptyVolume || !floppySoundDiskVolume || floppySoundUpdating) {
            return;
        }
        const int drive = qBound(0, currentFloppySoundDrive, FloppySoundDriveCount - 1);
        floppySoundTypeValue[drive] = floppySoundType->currentIndex();
        floppySoundEmptyAttenuation[drive] = 100 - floppySoundEmptyVolume->value();
        floppySoundDiskAttenuation[drive] = 100 - floppySoundDiskVolume->value();
    }

    void loadSelectedFloppySound()
    {
        if (!floppySoundType || !floppySoundEmptyVolume || !floppySoundDiskVolume) {
            return;
        }
        const int drive = qBound(0, currentFloppySoundDrive, FloppySoundDriveCount - 1);
        floppySoundUpdating = true;
        floppySoundType->setCurrentIndex(qBound(0, floppySoundTypeValue[drive], 1));
        floppySoundEmptyVolume->setValue(100 - floppySoundEmptyAttenuation[drive]);
        floppySoundDiskVolume->setValue(100 - floppySoundDiskAttenuation[drive]);
        floppySoundUpdating = false;
        updateFloppySoundVolumeLabels();
    }

    void selectHostDisplayByIndex(int index)
    {
        if (hostDisplay && index >= 0 && index < hostDisplay->count()) {
            hostDisplay->setCurrentIndex(index);
        }
    }

    void selectHostDisplayByName(const QString &value)
    {
        if (!hostDisplay || value.trimmed().isEmpty()) {
            return;
        }
        const QString wanted = value.trimmed();
        for (int i = 0; i < hostDisplay->count(); i++) {
            const QString configName = hostDisplay->itemData(i).toString();
            const QString friendlyName = hostDisplay->itemData(i, Qt::UserRole + 1).toString();
            const QString displayName = hostDisplay->itemText(i);
            if (configName.compare(wanted, Qt::CaseInsensitive) == 0
                || friendlyName.compare(wanted, Qt::CaseInsensitive) == 0
                || displayName.compare(wanted, Qt::CaseInsensitive) == 0) {
                hostDisplay->setCurrentIndex(i);
                return;
            }
        }
    }

    void selectSoundDeviceByConfigName(const QString &value)
    {
        if (!soundDevice || value.trimmed().isEmpty()) {
            return;
        }
        const QString wanted = value.trimmed();
        for (int i = 0; i < soundDevice->count(); i++) {
            const QString configName = soundDevice->itemData(i).toString();
            const QString displayName = soundDevice->itemText(i);
            if (configName.compare(wanted, Qt::CaseInsensitive) == 0
                || displayName.compare(wanted, Qt::CaseInsensitive) == 0
                || configName.mid(4).compare(wanted, Qt::CaseInsensitive) == 0) {
                soundDevice->setCurrentIndex(i);
                return;
            }
        }
    }

    void selectSamplerDeviceByConfigName(const QString &value)
    {
        if (!samplerDevice || value.trimmed().isEmpty()) {
            return;
        }
        const QString wanted = value.trimmed();
        for (int i = 0; i < samplerDevice->count(); i++) {
            const QString configName = samplerDevice->itemData(i).toString();
            const QString displayName = samplerDevice->itemText(i);
            if (configName.compare(wanted, Qt::CaseInsensitive) == 0
                || displayName.compare(wanted, Qt::CaseInsensitive) == 0
                || configName.mid(4).compare(wanted, Qt::CaseInsensitive) == 0) {
                samplerDevice->setCurrentIndex(i);
                updateIoPortsState();
                return;
            }
        }
    }

    void selectMidiOutByDeviceId(int deviceId)
    {
        if (!midiOut) {
            return;
        }
        for (int i = 0; i < midiOut->count(); i++) {
            if (midiOut->itemData(i).toInt() == deviceId) {
                midiOut->setCurrentIndex(i);
                updateIoPortsState();
                return;
            }
        }
        if (deviceId < -1) {
            midiOut->setCurrentIndex(0);
            updateIoPortsState();
        }
    }

    void selectMidiOutByConfigName(const QString &value)
    {
        if (!midiOut || value.trimmed().isEmpty()) {
            return;
        }
        const QString wanted = value.trimmed();
        if (wanted.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0) {
            selectMidiOutByDeviceId(-2);
            return;
        }
        for (int i = 0; i < midiOut->count(); i++) {
            const QString configName = midiOut->itemData(i, Qt::UserRole + 1).toString();
            const QString displayName = midiOut->itemText(i);
            if (configName.compare(wanted, Qt::CaseInsensitive) == 0
                || displayName.compare(wanted, Qt::CaseInsensitive) == 0) {
                midiOut->setCurrentIndex(i);
                updateIoPortsState();
                return;
            }
        }
    }

    void selectMidiInByDeviceId(int deviceId)
    {
        if (!midiIn) {
            return;
        }
        for (int i = 0; i < midiIn->count(); i++) {
            if (midiIn->itemData(i).toInt() == deviceId) {
                midiIn->setCurrentIndex(i);
                updateIoPortsState();
                return;
            }
        }
        midiIn->setCurrentIndex(0);
        updateIoPortsState();
    }

    void selectMidiInByConfigName(const QString &value)
    {
        if (!midiIn || value.trimmed().isEmpty()) {
            return;
        }
        const QString wanted = value.trimmed();
        if (wanted.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0) {
            selectMidiInByDeviceId(-1);
            return;
        }
        for (int i = 0; i < midiIn->count(); i++) {
            const QString configName = midiIn->itemData(i, Qt::UserRole + 1).toString();
            const QString displayName = midiIn->itemText(i);
            if (configName.compare(wanted, Qt::CaseInsensitive) == 0
                || displayName.compare(wanted, Qt::CaseInsensitive) == 0) {
                midiIn->setCurrentIndex(i);
                updateIoPortsState();
                return;
            }
        }
    }

    void setSoundSwapBit(bool paula, bool enabled)
    {
        if (!soundSwap) {
            return;
        }
        const int index = soundSwap->currentIndex();
        bool paulaEnabled = (index & 1) != 0;
        bool ahiEnabled = (index & 2) != 0;
        if (paula) {
            paulaEnabled = enabled;
        } else {
            ahiEnabled = enabled;
        }
        soundSwap->setCurrentText(soundSwapText(paulaEnabled, ahiEnabled));
    }

    QWidget *makeGamePortsPage()
    {
        QWidget *page = makePage();
        QVBoxLayout *root = new QVBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);
        portDevice[0] = combo(primaryPortDeviceItems(), QStringLiteral("Mouse"));
        portDevice[1] = combo(primaryPortDeviceItems(), QStringLiteral("Keyboard Layout A"));
        portDevice[2] = combo(parallelPortDeviceItems(), QStringLiteral("<None>"));
        portDevice[3] = combo(parallelPortDeviceItems(), QStringLiteral("<None>"));
        for (int i = 0; i < 2; i++) {
            portAutofire[i] = combo({
                QStringLiteral("No autofire (normal)"),
                QStringLiteral("Autofire"),
                QStringLiteral("Autofire (toggle)"),
                QStringLiteral("Autofire (always)"),
                QStringLiteral("No autofire (toggle)")
            }, QStringLiteral("No autofire (normal)"));
            portMode[i] = combo({
                QStringLiteral("Default"),
                QStringLiteral("Wheel Mouse"),
                QStringLiteral("Mouse"),
                QStringLiteral("Joystick"),
                QStringLiteral("Gamepad"),
                QStringLiteral("Analog joystick"),
                QStringLiteral("CDTV remote mouse"),
                QStringLiteral("CD32 pad"),
                QStringLiteral("Generic light pen/gun")
            }, QStringLiteral("Default"));
        }
        portAutoswitch = new QCheckBox(QStringLiteral("Mouse/Joystick autoswitching"));
        QPushButton *port0Remap = new QPushButton(QStringLiteral("Remap / Test"));
        QPushButton *port1Remap = new QPushButton(QStringLiteral("Remap / Test"));
        QPushButton *port2Remap = new QPushButton(QStringLiteral("Remap / Test"));
        QPushButton *port3Remap = new QPushButton(QStringLiteral("Remap / Test"));
        const auto wireRemapButton = [this](QPushButton *button, int port) {
            connect(button, &QPushButton::clicked, this, [this, port]() {
                openInputMapDialog(port);
            });
        };
        wireRemapButton(port0Remap, 0);
        wireRemapButton(port1Remap, 1);
        wireRemapButton(port2Remap, 2);
        wireRemapButton(port3Remap, 3);

        QGridLayout *ports = new QGridLayout;
        ports->setColumnStretch(1, 1);
        ports->addWidget(label(QStringLiteral("Port 1:")), 0, 0);
        ports->addWidget(portDevice[0], 0, 1, 1, 3);
        ports->addWidget(portAutofire[0], 1, 1);
        ports->addWidget(portMode[0], 1, 2);
        ports->addWidget(port0Remap, 1, 3);
        ports->addWidget(label(QStringLiteral("Port 2:")), 2, 0);
        ports->addWidget(portDevice[1], 2, 1, 1, 3);
        ports->addWidget(portAutofire[1], 3, 1);
        ports->addWidget(portMode[1], 3, 2);
        ports->addWidget(port1Remap, 3, 3);

        QPushButton *swapPorts = new QPushButton(QStringLiteral("Swap ports"));
        ports->addWidget(swapPorts, 4, 1);
        ports->addWidget(portAutoswitch, 4, 2, 1, 2);
        ports->addWidget(label(QStringLiteral("Emulated parallel port joystick adapter")), 5, 0, 1, 4, Qt::AlignLeft | Qt::AlignVCenter);
        ports->addWidget(label(QStringLiteral("Port 1:")), 6, 0);
        ports->addWidget(portDevice[2], 6, 1, 1, 3);
        ports->addWidget(port2Remap, 7, 3);
        ports->addWidget(label(QStringLiteral("Port 2:")), 8, 0);
        ports->addWidget(portDevice[3], 8, 1, 1, 3);
        ports->addWidget(port3Remap, 9, 3);
        root->addWidget(groupBox(QStringLiteral("Mouse and Joystick settings"), ports));

        connect(swapPorts, &QPushButton::clicked, this, [this]() {
            const QString port0Text = portDevice[0]->currentText();
            const QString port0Autofire = portAutofire[0]->currentText();
            const QString port0Mode = portMode[0]->currentText();
            portDevice[0]->setCurrentText(portDevice[1]->currentText());
            portAutofire[0]->setCurrentText(portAutofire[1]->currentText());
            portMode[0]->setCurrentText(portMode[1]->currentText());
            portDevice[1]->setCurrentText(port0Text);
            portAutofire[1]->setCurrentText(port0Autofire);
            portMode[1]->setCurrentText(port0Mode);
        });
        const auto updateRemapButtons = [this, port0Remap, port1Remap, port2Remap, port3Remap]() {
            QPushButton *buttons[] = { port0Remap, port1Remap, port2Remap, port3Remap };
            for (int i = 0; i < 4; i++) {
                const QString device = portDevice[i] ? portDevice[i]->currentText().trimmed() : QString();
                const bool enabled = !device.isEmpty() && device != QStringLiteral("<None>");
                buttons[i]->setEnabled(enabled);
                buttons[i]->setToolTip(enabled
                    ? QStringLiteral("Open input remap/test dialog.")
                    : QStringLiteral("Select a host input device first."));
            }
        };
        for (QComboBox *device : portDevice) {
            connect(device, &QComboBox::currentTextChanged, this, [updateRemapButtons](const QString &) {
                updateRemapButtons();
            });
        }
        updateRemapButtons();

        QGridLayout *mouse = new QGridLayout;
        mouse->setColumnStretch(1, 1);
        mouseSpeed = new QSpinBox;
        mouseSpeed->setRange(1, 1000);
        mouseSpeed->setValue(100);
        virtualMouseDriver = new QCheckBox(QStringLiteral("Install virtual mouse driver"));
        mouseUntrapMode = combo({
            QStringLiteral("None (Alt-Tab)"),
            QStringLiteral("Middle button"),
            QStringLiteral("Magic mouse"),
            QStringLiteral("Both")
        }, QStringLiteral("Middle button"));
        magicMouseCursor = combo({
            QStringLiteral("Show both cursors"),
            QStringLiteral("Show native cursor only"),
            QStringLiteral("Show host cursor only")
        }, QStringLiteral("Show both cursors"));
        tabletLibrary = new QCheckBox(QStringLiteral("Tablet.library emulation"));
        tabletMode = combo({ QStringLiteral("-"), QStringLiteral("Tablet emulation") }, QStringLiteral("-"));
        if (!unixTabletBackendAvailable()) {
            const QString reason = QStringLiteral("Native tablet input is not implemented by the Unix input backend yet.");
            disableUnavailable(tabletLibrary, reason);
            disableUnavailable(tabletMode, reason);
        }

        mouse->addWidget(label(QStringLiteral("Mouse speed:")), 0, 0);
        mouse->addWidget(mouseSpeed, 0, 1);
        mouse->addWidget(label(QStringLiteral("Mouse untrap mode:")), 0, 2);
        mouse->addWidget(mouseUntrapMode, 0, 3);
        mouse->addWidget(virtualMouseDriver, 1, 0, 1, 2);
        mouse->addWidget(label(QStringLiteral("Magic Mouse cursor mode:")), 1, 2);
        mouse->addWidget(magicMouseCursor, 1, 3);
        mouse->addWidget(tabletLibrary, 2, 0, 1, 2);
        mouse->addWidget(label(QStringLiteral("Tablet mode:")), 2, 2);
        mouse->addWidget(tabletMode, 2, 3);
        root->addWidget(groupBox(QStringLiteral("Mouse extra settings"), mouse), 1);

        connect(virtualMouseDriver, &QCheckBox::toggled, this, [this](bool) { updateMouseExtraState(); });
        connect(tabletMode, &QComboBox::currentTextChanged, this, [this](const QString &) { updateMouseExtraState(); });
        updateMouseExtraState();
        return page;
    }

    void openInputMapDialog(int port)
    {
        QString context;
        int customSlot = -1;
        if (port >= 0 && port < 4) {
            context = QStringLiteral("Port %1: %2").arg(port + 1).arg(portDevice[port] ? portDevice[port]->currentText() : QStringLiteral("<None>"));
            if (port < 2) {
                context += QStringLiteral("\nMode: %1\nAutofire: %2")
                    .arg(portMode[port] ? portMode[port]->currentText() : QStringLiteral("Default"))
                    .arg(portAutofire[port] ? portAutofire[port]->currentText() : QStringLiteral("No autofire (normal)"));
            }
            customSlot = qBound(0, port, MaxJoyportCustomSlots - 1);
            const QString device = portDevice[port] ? portDevice[port]->currentText() : QString();
            if (device.startsWith(QStringLiteral("Custom "))) {
                bool ok = false;
                const int slot = device.mid(7).toInt(&ok) - 1;
                if (ok && slot >= 0 && slot < MaxJoyportCustomSlots) {
                    customSlot = slot;
                }
            }
        } else {
            context = QStringLiteral("Input: %1").arg(inputDevice ? inputDevice->currentText() : QStringLiteral("Device"));
        }
        UnixQtInputMapDialog dialog(port, context, customSlot >= 0 ? joyportCustom[customSlot] : QString(), this);
        if (dialog.exec() == QDialog::Accepted && dialog.hasChanges() && customSlot >= 0) {
            joyportCustom[customSlot] = dialog.customConfig();
            if (portDevice[port]) {
                portDevice[port]->setCurrentText(QStringLiteral("Custom %1").arg(customSlot + 1));
            }
        }
    }

    void updateMouseExtraState()
    {
        const bool tabletBackend = unixTabletBackendAvailable();
        const bool virtualMouseEnabled = virtualMouseDriver && virtualMouseDriver->isChecked();
        const bool tabletModeSelected = tabletMode && tabletMode->currentText() == QStringLiteral("Tablet emulation");
        const bool absoluteMouseEnabled = virtualMouseEnabled || (tabletBackend && tabletModeSelected);
        const bool tabletEnabled = tabletBackend && absoluteMouseEnabled;
        if (magicMouseCursor) {
            magicMouseCursor->setEnabled(absoluteMouseEnabled);
        }
        if (tabletLibrary) {
            tabletLibrary->setEnabled(tabletEnabled);
            if (!tabletBackend || !tabletEnabled) {
                tabletLibrary->setChecked(false);
            }
        }
        if (tabletMode) {
            tabletMode->setEnabled(tabletBackend && absoluteMouseEnabled);
            if (!tabletBackend) {
                tabletMode->setCurrentText(QStringLiteral("-"));
            }
        }
    }

    void setMouseUntrapBit(bool middle, bool enabled)
    {
        if (!mouseUntrapMode) {
            return;
        }
        const int index = mouseUntrapMode->currentIndex();
        bool middleEnabled = index == 1 || index == 3;
        bool magicEnabled = index == 2 || index == 3;
        if (middle) {
            middleEnabled = enabled;
        } else {
            magicEnabled = enabled;
        }
        const int nextIndex = (middleEnabled ? 1 : 0) + (magicEnabled ? 2 : 0);
        mouseUntrapMode->setCurrentIndex(qBound(0, nextIndex, 3));
    }

    QWidget *makeIoPortsPage()
    {
        QWidget *page = makePage();
        QVBoxLayout *root = new QVBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);
        root->setSpacing(6);

        QGridLayout *parallel = new QGridLayout;
        parallel->setHorizontalSpacing(8);
        parallel->setVerticalSpacing(4);
        parallel->setColumnStretch(1, 1);
        printerPort = combo({ QStringLiteral("<None>") });
        printerType = combo(configChoiceDisplays(printerTypeChoices, int(sizeof(printerTypeChoices) / sizeof(printerTypeChoices[0]))));
        QPushButton *flushPrinter = new QPushButton(QStringLiteral("Flush print job"));
        printerAutoFlush = new QSpinBox;
        printerAutoFlush->setRange(0, 3600);
        ghostscriptParams = new QLineEdit;
        samplerDevice = new QComboBox;
        for (const auto &choice : samplerDeviceChoices()) {
            samplerDevice->addItem(choice.first, choice.second);
        }
        samplerStereo = new QCheckBox(QStringLiteral("Stereo sampler"));
        parallel->addWidget(label(QStringLiteral("Printer:")), 0, 0);
        parallel->addWidget(printerPort, 0, 1, 1, 3);
        parallel->addWidget(label(QStringLiteral("Type:")), 1, 0);
        parallel->addWidget(printerType, 1, 1, 1, 3);
        parallel->addWidget(flushPrinter, 2, 1);
        parallel->addWidget(label(QStringLiteral("Autoflush:")), 2, 2);
        parallel->addWidget(printerAutoFlush, 2, 3);
        parallel->addWidget(label(QStringLiteral("Ghostscript extra parameters:")), 3, 0);
        parallel->addWidget(ghostscriptParams, 3, 1, 1, 3);
        parallel->addWidget(label(QStringLiteral("Sampler:")), 4, 0);
        parallel->addWidget(samplerDevice, 4, 1, 1, 3);
        parallel->addWidget(samplerStereo, 5, 1, 1, 3);
        root->addWidget(groupBox(QStringLiteral("Parallel Port"), parallel));

        QGridLayout *serial = new QGridLayout;
        serial->setHorizontalSpacing(8);
        serial->setVerticalSpacing(4);
        serial->setColumnStretch(0, 1);
        serial->setColumnStretch(1, 1);
        serial->setColumnStretch(2, 1);
        serial->setColumnStretch(3, 1);
        serialPort = combo(unixSerialPortItems());
        serialPort->setEditable(true);
        serialPort->setInsertPolicy(QComboBox::NoInsert);
        serialPort->lineEdit()->setClearButtonEnabled(true);
        serialShared = new QCheckBox(QStringLiteral("Shared"));
        serialCtsRts = new QCheckBox(QStringLiteral("Host RTS/CTS"));
        serialDirect = new QCheckBox(QStringLiteral("Direct"));
        serialCrlf = new QCheckBox(QStringLiteral("CR/LF conversion"));
        uaeSerial = new QCheckBox(QStringLiteral("uaeserial.device"));
        serialStatus = new QCheckBox(QStringLiteral("Serial status (RTS/CTS/DTR/DTE/CD)"));
        serialRingIndicator = new QCheckBox(QStringLiteral("Serial status: Ring Indicator"));
        serialDirect->setToolTip(QStringLiteral("Unix serial uses direct host device/TCP I/O; this keeps the Windows-compatible config flag enabled for that path."));
        serial->addWidget(serialPort, 0, 0, 1, 4);
        serial->addWidget(serialShared, 1, 0);
        serial->addWidget(serialCtsRts, 1, 1);
        serial->addWidget(serialDirect, 1, 2);
        serial->addWidget(uaeSerial, 1, 3);
        serial->addWidget(serialStatus, 2, 0, 1, 2);
        serial->addWidget(serialRingIndicator, 2, 2);
        serial->addWidget(serialCrlf, 2, 3);
        root->addWidget(groupBox(QStringLiteral("Serial Port"), serial));

        QGridLayout *midi = new QGridLayout;
        midi->setHorizontalSpacing(8);
        midi->setVerticalSpacing(4);
        midi->setColumnStretch(1, 1);
        midi->setColumnStretch(3, 1);
        midiOut = new QComboBox;
        midiOut->addItem(QStringLiteral("<None>"), -2);
        for (const WinUaeQtMidiChoice &choice : midiOutputChoices()) {
            midiOut->addItem(choice.display, choice.deviceId);
            midiOut->setItemData(midiOut->count() - 1, choice.configName, Qt::UserRole + 1);
        }
        midiIn = new QComboBox;
        midiIn->addItem(QStringLiteral("<None>"), -1);
        for (const WinUaeQtMidiChoice &choice : midiInputChoices()) {
            midiIn->addItem(choice.display, choice.deviceId);
            midiIn->setItemData(midiIn->count() - 1, choice.configName, Qt::UserRole + 1);
        }
        midiRouter = new QCheckBox(QStringLiteral("Route MIDI In to MIDI Out"));
        midi->addWidget(label(QStringLiteral("Out:")), 0, 0);
        midi->addWidget(midiOut, 0, 1);
        midi->addWidget(label(QStringLiteral("In:")), 0, 2);
        midi->addWidget(midiIn, 0, 3);
        midi->addWidget(midiRouter, 1, 1, 1, 3);
        root->addWidget(groupBox(QStringLiteral("MIDI"), midi));

        QGridLayout *dongle = new QGridLayout;
        dongle->setHorizontalSpacing(8);
        dongle->setVerticalSpacing(4);
        dongle->setColumnStretch(1, 1);
        protectionDongle = combo(configChoiceDisplays(dongleChoices, int(sizeof(dongleChoices) / sizeof(dongleChoices[0]))));
        dongle->addWidget(label(QStringLiteral("Dongle:")), 0, 0);
        dongle->addWidget(protectionDongle, 0, 1);
        root->addWidget(groupBox(QStringLiteral("Protection Dongle"), dongle));
        root->addStretch(1);

        connect(serialPort, &QComboBox::currentTextChanged, this, [this](const QString &) { updateIoPortsState(); });
        connect(samplerDevice, &QComboBox::currentTextChanged, this, [this](const QString &) { updateIoPortsState(); });
        connect(midiOut, &QComboBox::currentTextChanged, this, [this](const QString &) { updateIoPortsState(); });
        connect(midiIn, &QComboBox::currentTextChanged, this, [this](const QString &) { updateIoPortsState(); });
        updateIoPortsState();
        return page;
    }

    void updateIoPortsState()
    {
        const QString serial = serialPort ? serialPort->currentText().trimmed() : QString();
        const bool serialEnabled = !serial.isEmpty()
            && serial != QStringLiteral("<None>")
            && serial.compare(QStringLiteral("none"), Qt::CaseInsensitive) != 0;
        if (serialShared) {
            serialShared->setEnabled(serialEnabled);
        }
        if (serialCtsRts) {
            serialCtsRts->setEnabled(serialEnabled);
        }
        if (serialDirect) {
            serialDirect->setEnabled(serialEnabled);
        }
        if (serialStatus) {
            serialStatus->setEnabled(serialEnabled);
        }
        if (serialRingIndicator) {
            serialRingIndicator->setEnabled(serialEnabled);
        }
        if (samplerStereo) {
            samplerStereo->setEnabled(samplerDevice && samplerDevice->currentText() != QStringLiteral("<None>"));
        }
        const bool midiActive = midiOut && midiIn
            && midiOut->currentText() != QStringLiteral("<None>")
            && midiIn->currentText() != QStringLiteral("<None>");
        if (midiRouter) {
            midiRouter->setEnabled(midiActive);
        }
    }

    bool inputGamePortsMode() const
    {
        return !inputType || inputType->currentText() == QStringLiteral("Game Ports");
    }

    int selectedInputConfigLine() const
    {
        if (inputGamePortsMode()) {
            return GamePortsInputConfigLine;
        }
        return qBound(1, inputType->currentIndex() + 1, CustomInputConfigSlots);
    }

    QString selectedInputDeviceType() const
    {
        const QString device = inputDevice ? inputDevice->currentText() : QStringLiteral("Joystick");
        if (device == QStringLiteral("Mouse")) {
            return QStringLiteral("mouse");
        }
        if (device == QStringLiteral("Keyboard")) {
            return QStringLiteral("keyboard");
        }
        return QStringLiteral("joystick");
    }

    void markInputOwnedKey(const QString &key)
    {
        if (!inputOwnedMappingKeys.contains(key)) {
            inputOwnedMappingKeys.append(key);
        }
    }

    QString inputDeviceMetadataKey(const QString &suffix, int device = 0) const
    {
        return QStringLiteral("input.%1.%2.%3.%4")
            .arg(selectedInputConfigLine())
            .arg(selectedInputDeviceType())
            .arg(device)
            .arg(suffix);
    }

    QString selectedInputMappingKey() const
    {
        QTreeWidgetItem *item = inputMappingList ? inputMappingList->currentItem() : nullptr;
        return item ? item->data(0, InputMappingKeyRole).toString() : QString();
    }

    QList<int> inputDeviceIndices(const QString &type, int configLine) const
    {
        QList<int> indices;
        if (type != QStringLiteral("keyboard")) {
            indices.append(0);
        }
        const QString prefix = QStringLiteral("input.%1.%2.").arg(configLine).arg(type);
        for (auto it = inputMappingSettings.constBegin(); it != inputMappingSettings.constEnd(); ++it) {
            if (!it.key().startsWith(prefix)) {
                continue;
            }
            const QStringList parts = it.key().split(QLatin1Char('.'));
            bool ok = false;
            const int index = parts.value(3).toInt(&ok);
            if (ok && !indices.contains(index)) {
                indices.append(index);
            }
        }
        std::sort(indices.begin(), indices.end());
        return indices;
    }

    QVector<WinUaeQtInputRow> currentInputRows() const
    {
        QVector<WinUaeQtInputRow> rows;
        const QString type = selectedInputDeviceType();
        const int configLine = selectedInputConfigLine();
        const QList<int> deviceIndices = inputDeviceIndices(type, configLine);
        const bool showDeviceNumber = deviceIndices.size() > 1 || deviceIndices.value(0, 0) != 0;
        const auto rowLabel = [showDeviceNumber](const QString &base, int device) {
            return showDeviceNumber ? QStringLiteral("Device %1 %2").arg(device + 1).arg(base) : base;
        };

        if (type == QStringLiteral("joystick")) {
            for (int device : deviceIndices) {
                for (int axis = 0; axis < 6; axis++) {
                    rows.append({
                        rowLabel(axis == 0 ? QStringLiteral("X Axis")
                            : axis == 1 ? QStringLiteral("Y Axis")
                            : QStringLiteral("Axis %1").arg(axis + 1), device),
                        QStringLiteral("input.%1.joystick.%2.axis.%3").arg(configLine).arg(device).arg(axis),
                        true
                    });
                }
                for (int button = 0; button < 12; button++) {
                    rows.append({
                        rowLabel(QStringLiteral("Button %1").arg(button + 1), device),
                        QStringLiteral("input.%1.joystick.%2.button.%3").arg(configLine).arg(device).arg(button),
                        true
                    });
                }
            }
        } else if (type == QStringLiteral("mouse")) {
            for (int device : deviceIndices) {
                rows.append({ rowLabel(QStringLiteral("X Axis"), device), QStringLiteral("input.%1.mouse.%2.axis.0").arg(configLine).arg(device), true });
                rows.append({ rowLabel(QStringLiteral("Y Axis"), device), QStringLiteral("input.%1.mouse.%2.axis.1").arg(configLine).arg(device), true });
                rows.append({ rowLabel(QStringLiteral("Wheel"), device), QStringLiteral("input.%1.mouse.%2.axis.2").arg(configLine).arg(device), true });
                for (int button = 0; button < 5; button++) {
                    rows.append({
                        rowLabel(QStringLiteral("Button %1").arg(button + 1), device),
                        QStringLiteral("input.%1.mouse.%2.button.%3").arg(configLine).arg(device).arg(button),
                        true
                    });
                }
            }
        } else {
            QStringList keyboardKeys;
            const QString prefix = QStringLiteral("input.%1.keyboard.").arg(configLine);
            for (auto it = inputMappingSettings.constBegin(); it != inputMappingSettings.constEnd(); ++it) {
                if (winUaeQtIsInputWidgetMappingKey(it.key()) && it.key().startsWith(prefix)) {
                    keyboardKeys.append(it.key());
                }
            }
            std::sort(keyboardKeys.begin(), keyboardKeys.end());
            QStringList generatedKeys;
            for (const WinUaeQtKeyboardChoice &choice : keyboardChoices) {
                const QString key = QStringLiteral("input.%1.keyboard.0.button.%2.KEY_%2")
                    .arg(configLine)
                    .arg(choice.scancode);
                generatedKeys.append(key);
                rows.append({
                    QString::fromLatin1(choice.display),
                    key,
                    true
                });
            }
            for (const QString &key : keyboardKeys) {
                if (generatedKeys.contains(key)) {
                    continue;
                }
                const QStringList parts = key.split(QLatin1Char('.'));
                rows.append({
                    QStringLiteral("Keyboard %1 %2").arg(parts.value(3), parts.mid(4).join(QLatin1Char(' '))),
                    key,
                    true
                });
            }
        }
        return rows;
    }

    QVector<WinUaeQtInputSlot> inputSlotsForKey(const QString &key) const
    {
        QVector<WinUaeQtInputSlot> slotList;
        const QString value = inputMappingSettings.value(key).trimmed();
        if (value.isEmpty()) {
            return slotList;
        }
        for (const QString &field : winUaeQtInputSlotFields(value)) {
            slotList.append(winUaeQtParseInputSlot(field));
        }
        return slotList;
    }

    WinUaeQtInputSlot selectedInputSlotForKey(const QString &key) const
    {
        const QVector<WinUaeQtInputSlot> slotList = inputSlotsForKey(key);
        const int sub = inputSubEvent ? inputSubEvent->currentIndex() : 0;
        return sub >= 0 && sub < slotList.size() ? slotList[sub] : WinUaeQtInputSlot();
    }

    void insertInputMetadataIfMissing(const QString &key, const QString &value)
    {
        if (!inputMappingSettings.contains(key)) {
            inputMappingSettings.insert(key, value);
            markInputOwnedKey(key);
        }
    }

    void ensureInputDeviceMetadataForMappingKey(const QString &key)
    {
        const QStringList parts = key.split(QLatin1Char('.'));
        if (parts.size() < 5) {
            return;
        }
        const QString prefix = QStringLiteral("input.%1.%2.%3.")
            .arg(parts.value(1), parts.value(2), parts.value(3));
        if (parts.value(2) == QStringLiteral("keyboard")) {
            insertInputMetadataIfMissing(prefix + QStringLiteral("friendlyname"), QStringLiteral("Unix Keyboard"));
            insertInputMetadataIfMissing(prefix + QStringLiteral("name"), QStringLiteral("unix.keyboard"));
        } else if (parts.value(2) == QStringLiteral("mouse")) {
            insertInputMetadataIfMissing(prefix + QStringLiteral("friendlyname"), QStringLiteral("Unix Mouse"));
            insertInputMetadataIfMissing(prefix + QStringLiteral("name"), QStringLiteral("unix.mouse"));
        }
        insertInputMetadataIfMissing(prefix + QStringLiteral("empty"), QStringLiteral("false"));
        insertInputMetadataIfMissing(prefix + QStringLiteral("disabled"), QStringLiteral("false"));
    }

    void setInputSlotForKey(const QString &key, int sub, WinUaeQtInputSlot slot)
    {
        if (key.isEmpty() || sub < 0 || sub >= MaxInputSubEventSlots) {
            return;
        }
        QVector<WinUaeQtInputSlot> slotList = inputSlotsForKey(key);
        while (slotList.size() <= sub) {
            slotList.append(WinUaeQtInputSlot());
        }
        slotList[sub] = slot;
        while (!slotList.isEmpty()) {
            const WinUaeQtInputSlot &last = slotList.last();
            if (!last.event.trimmed().isEmpty() || last.flags != 0 || !last.qualifiers.isEmpty() || !last.suffix.isEmpty()) {
                break;
            }
            slotList.removeLast();
        }

        markInputOwnedKey(key);
        if (slotList.isEmpty()) {
            inputMappingSettings.remove(key);
            return;
        }
        QStringList formatted;
        for (const WinUaeQtInputSlot &entry : slotList) {
            formatted.append(winUaeQtFormatInputSlot(entry));
        }
        ensureInputDeviceMetadataForMappingKey(key);
        inputMappingSettings.insert(key, formatted.join(QLatin1Char(',')));
    }

    int inputMappingSubEventCount(const QString &key) const
    {
        int count = 0;
        for (const WinUaeQtInputSlot &slot : inputSlotsForKey(key)) {
            if (!slot.event.trimmed().isEmpty()) {
                count++;
            }
        }
        return count;
    }

    QString inputFlagText(const WinUaeQtInputSlot &slot, int flag) const
    {
        if (slot.event.trimmed().isEmpty()) {
            return QStringLiteral("-");
        }
        return (slot.flags & flag) ? QStringLiteral("YES") : QStringLiteral("NO");
    }

    QString inputAutofireText(const WinUaeQtInputSlot &slot) const
    {
        if (slot.event.trimmed().isEmpty()) {
            return QStringLiteral("-");
        }
        if (slot.flags & InputFlagInvertToggle) {
            return QStringLiteral("ON");
        }
        return (slot.flags & InputFlagAutofire) ? QStringLiteral("YES") : QStringLiteral("NO");
    }

    void populateInputEventChoices(const WinUaeQtInputSlot &slot, bool editable)
    {
        if (!inputAmigaEvent) {
            return;
        }
        const QSignalBlocker blocker(inputAmigaEvent);
        inputMappingUpdating = true;
        inputAmigaEvent->clear();
        inputAmigaEvent->addItem(QStringLiteral("<None>"), QString());
        inputAmigaEvent->addItem(QStringLiteral("<Custom Event>"), QStringLiteral("__custom__"));
        for (const WinUaeQtInputEventChoice &choice : inputEventChoices) {
            inputAmigaEvent->addItem(QString::fromLatin1(choice.display), QString::fromLatin1(choice.config));
        }
        for (const WinUaeQtKeyboardChoice &choice : keyboardChoices) {
            if (choice.defaultEvent[0]) {
                inputAmigaEvent->addItem(QString::fromLatin1(choice.display), QString::fromLatin1(choice.defaultEvent));
            }
        }
        if (!slot.event.isEmpty()) {
            const QString data = slot.custom ? QStringLiteral("__current_custom__") : slot.event;
            const QString text = slot.custom
                ? QStringLiteral("<Custom Event>: %1").arg(slot.event)
                : winUaeQtInputEventDisplayName(slot.event);
            if (inputAmigaEvent->findData(data) < 0) {
                inputAmigaEvent->insertItem(1, text, data);
            }
            inputAmigaEvent->setCurrentIndex(qMax(0, inputAmigaEvent->findData(data)));
        } else {
            inputAmigaEvent->setCurrentIndex(0);
        }
        inputAmigaEvent->setEnabled(editable);
        inputMappingUpdating = false;
    }

    void updateInputDeviceEnabledCheck()
    {
        if (!inputDeviceEnabled) {
            return;
        }
        const QSignalBlocker blocker(inputDeviceEnabled);
        const QString disabled = inputMappingSettings.value(inputDeviceMetadataKey(QStringLiteral("disabled")));
        inputDeviceEnabled->setChecked(!configBoolValue(disabled));
    }

    void updateInputControlState()
    {
        const QString key = selectedInputMappingKey();
        QTreeWidgetItem *item = inputMappingList ? inputMappingList->currentItem() : nullptr;
        const bool editable = item && item->data(0, InputMappingEditableRole).toBool() && !inputGamePortsMode();
        const WinUaeQtInputSlot slot = editable ? selectedInputSlotForKey(key) : WinUaeQtInputSlot();
        populateInputEventChoices(slot, editable);
        if (inputDeviceEnabled) {
            inputDeviceEnabled->setEnabled(!inputGamePortsMode());
            updateInputDeviceEnabledCheck();
        }
        if (inputRemapButton) {
            const bool canCapture = editable && selectedInputDeviceType() == QStringLiteral("joystick");
            inputRemapButton->setEnabled(canCapture);
            inputRemapButton->setToolTip(canCapture
                ? QStringLiteral("Capture an SDL joystick/gamepad control for the selected mapping.")
                : QStringLiteral("Capture is currently available for joystick/gamepad mappings."));
        }
        if (inputCopyButton) {
            inputCopyButton->setEnabled(!inputGamePortsMode());
        }
        if (inputCopyFrom) {
            inputCopyFrom->setEnabled(!inputGamePortsMode());
        }
        if (inputSwapButton) {
            inputSwapButton->setEnabled(!inputGamePortsMode());
        }
    }

    void setSelectedInputEvent(const QString &eventData)
    {
        if (inputMappingUpdating || inputGamePortsMode()) {
            return;
        }
        const QString key = selectedInputMappingKey();
        QTreeWidgetItem *item = inputMappingList ? inputMappingList->currentItem() : nullptr;
        if (key.isEmpty() || !item || !item->data(0, InputMappingEditableRole).toBool()) {
            return;
        }
        WinUaeQtInputSlot slot = selectedInputSlotForKey(key);
        QString event = eventData;
        bool custom = false;
        if (eventData == QStringLiteral("__current_custom__")) {
            return;
        }
        if (eventData == QStringLiteral("__custom__")) {
            bool ok = false;
            event = QInputDialog::getText(this, QStringLiteral("Custom Event"), QStringLiteral("Event:"), QLineEdit::Normal, slot.custom ? slot.event : QString(), &ok).trimmed();
            if (!ok) {
                updateInputControlState();
                return;
            }
            custom = true;
        }
        slot.event = event;
        slot.custom = custom;
        if (slot.event.isEmpty()) {
            slot.flags = 0;
            slot.qualifiers.clear();
            slot.suffix.clear();
            slot.custom = false;
        }
        setInputSlotForKey(key, inputSubEvent ? inputSubEvent->currentIndex() : 0, slot);
        refreshInputMappingList(key);
    }

    void toggleSelectedInputFlag(int flag)
    {
        if (inputGamePortsMode()) {
            return;
        }
        const QString key = selectedInputMappingKey();
        if (key.isEmpty()) {
            return;
        }
        WinUaeQtInputSlot slot = selectedInputSlotForKey(key);
        if (slot.event.trimmed().isEmpty()) {
            return;
        }
        slot.flags ^= flag;
        setInputSlotForKey(key, inputSubEvent ? inputSubEvent->currentIndex() : 0, slot);
        refreshInputMappingList(key);
    }

    void toggleSelectedInputAutofire()
    {
        if (inputGamePortsMode()) {
            return;
        }
        const QString key = selectedInputMappingKey();
        if (key.isEmpty()) {
            return;
        }
        WinUaeQtInputSlot slot = selectedInputSlotForKey(key);
        if (slot.event.trimmed().isEmpty()) {
            return;
        }
        if ((slot.flags & InputFlagAutofire) && (slot.flags & InputFlagInvertToggle)) {
            slot.flags &= ~(InputFlagAutofire | InputFlagInvertToggle);
        } else if (slot.flags & InputFlagAutofire) {
            slot.flags |= InputFlagInvertToggle;
            slot.flags &= ~InputFlagToggle;
        } else {
            slot.flags |= InputFlagAutofire;
        }
        setInputSlotForKey(key, inputSubEvent ? inputSubEvent->currentIndex() : 0, slot);
        refreshInputMappingList(key);
    }

    void toggleSelectedInputSetMode()
    {
        if (inputGamePortsMode()) {
            return;
        }
        const QString key = selectedInputMappingKey();
        if (key.isEmpty()) {
            return;
        }
        WinUaeQtInputSlot slot = selectedInputSlotForKey(key);
        if (slot.event.trimmed().isEmpty()) {
            return;
        }
        if (slot.flags & InputFlagSetOnOffVal2) {
            slot.flags &= ~(InputFlagSetOnOff | InputFlagSetOnOffVal1 | InputFlagSetOnOffVal2);
        } else if (slot.flags & InputFlagSetOnOffVal1) {
            slot.flags &= ~InputFlagSetOnOffVal1;
            slot.flags |= InputFlagSetOnOffVal2;
        } else if (slot.flags & InputFlagSetOnOff) {
            slot.flags |= InputFlagSetOnOffVal1;
        } else {
            slot.flags |= InputFlagSetOnOff;
        }
        setInputSlotForKey(key, inputSubEvent ? inputSubEvent->currentIndex() : 0, slot);
        refreshInputMappingList(key);
    }

    bool inputKeyMatchesCurrentDevice(const QString &key, int configLine) const
    {
        const QString prefix = QStringLiteral("input.%1.%2.").arg(configLine).arg(selectedInputDeviceType());
        return key.startsWith(prefix);
    }

    void copyInputMappings()
    {
        if (inputGamePortsMode()) {
            return;
        }
        const int dest = selectedInputConfigLine();
        const int sourceIndex = inputCopyFrom ? inputCopyFrom->currentIndex() : -1;
        if (sourceIndex < 0) {
            return;
        }
        if (sourceIndex < CustomInputConfigSlots && sourceIndex + 1 == dest) {
            return;
        }
        const QString type = selectedInputDeviceType();
        const QString destPrefix = QStringLiteral("input.%1.%2.").arg(dest).arg(type);
        const QString sourcePrefix = QStringLiteral("input.%1.%2.").arg(sourceIndex + 1).arg(type);
        const QStringList keys = inputMappingSettings.keys();
        for (const QString &key : keys) {
            if (key.startsWith(destPrefix)) {
                inputMappingSettings.remove(key);
                markInputOwnedKey(key);
            }
        }
        if (sourceIndex < CustomInputConfigSlots && sourceIndex + 1 != dest) {
            for (const QString &key : keys) {
                if (!key.startsWith(sourcePrefix)) {
                    continue;
                }
                const QString destKey = destPrefix + key.mid(sourcePrefix.size());
                inputMappingSettings.insert(destKey, inputMappingSettings.value(key));
                markInputOwnedKey(destKey);
            }
        }
        refreshInputMappingList();
    }

    void swapInputMappings()
    {
        if (inputGamePortsMode()) {
            return;
        }
        const QStringList keys = inputMappingSettings.keys();
        const int configLine = selectedInputConfigLine();
        for (const QString &key : keys) {
            if (!inputKeyMatchesCurrentDevice(key, configLine) || !winUaeQtIsInputWidgetMappingKey(key)) {
                continue;
            }
            QVector<WinUaeQtInputSlot> slotList = inputSlotsForKey(key);
            bool changed = false;
            for (WinUaeQtInputSlot &slot : slotList) {
                const QString swapped = winUaeQtSwapInputEventPorts(slot.event);
                if (swapped != slot.event) {
                    slot.event = swapped;
                    changed = true;
                }
            }
            if (!changed) {
                continue;
            }
            QStringList formatted;
            for (const WinUaeQtInputSlot &slot : slotList) {
                formatted.append(winUaeQtFormatInputSlot(slot));
            }
            inputMappingSettings.insert(key, formatted.join(QLatin1Char(',')));
            markInputOwnedKey(key);
        }
        refreshInputMappingList();
    }

    void setInputDeviceEnabled(bool enabled)
    {
        if (inputMappingUpdating || inputGamePortsMode()) {
            return;
        }
        const QString key = inputDeviceMetadataKey(QStringLiteral("disabled"));
        inputMappingSettings.insert(key, enabled ? QStringLiteral("false") : QStringLiteral("true"));
        markInputOwnedKey(key);
    }

    void openInputRemapCapture()
    {
        const QString key = selectedInputMappingKey();
        if (key.isEmpty() || selectedInputDeviceType() != QStringLiteral("joystick")) {
            return;
        }
        WinUaeQtInputSlot slot = selectedInputSlotForKey(key);
        const QString event = inputAmigaEvent ? inputAmigaEvent->currentData().toString() : slot.event;
        if (!event.isEmpty() && !event.startsWith(QStringLiteral("__"))) {
            slot.event = event;
            slot.custom = false;
        }
        if (slot.event.trimmed().isEmpty()) {
            QMessageBox::information(this, windowTitle(), QStringLiteral("Select an Amiga event before capturing a host control."));
            return;
        }

        const QString prompt = QStringLiteral("Press input for %1.").arg(winUaeQtInputEventDisplayName(slot.event));
        UnixQtInputMapDialog dialog(-1, prompt, QString(), this, true);
        if (dialog.exec() != QDialog::Accepted || !dialog.hasCapturedInput()) {
            return;
        }
        const QString capturedKey = QStringLiteral("input.%1.joystick.%2.%3.%4")
            .arg(selectedInputConfigLine())
            .arg(dialog.capturedDevice())
            .arg(dialog.capturedWidgetType() == QLatin1Char('b') ? QStringLiteral("button") : QStringLiteral("axis"))
            .arg(dialog.capturedWidgetMappingIndex());
        inputMappingSettings.insert(QStringLiteral("input.%1.joystick.%2.friendlyname")
                .arg(selectedInputConfigLine()).arg(dialog.capturedDevice()),
            dialog.capturedDeviceDisplayName());
        inputMappingSettings.insert(QStringLiteral("input.%1.joystick.%2.name")
                .arg(selectedInputConfigLine()).arg(dialog.capturedDevice()),
            dialog.capturedDeviceConfigName().isEmpty() ? dialog.capturedDeviceDisplayName() : dialog.capturedDeviceConfigName());
        inputMappingSettings.insert(QStringLiteral("input.%1.joystick.%2.empty")
                .arg(selectedInputConfigLine()).arg(dialog.capturedDevice()),
            QStringLiteral("false"));
        inputMappingSettings.insert(QStringLiteral("input.%1.joystick.%2.disabled")
                .arg(selectedInputConfigLine()).arg(dialog.capturedDevice()),
            QStringLiteral("false"));
        markInputOwnedKey(QStringLiteral("input.%1.joystick.%2.friendlyname").arg(selectedInputConfigLine()).arg(dialog.capturedDevice()));
        markInputOwnedKey(QStringLiteral("input.%1.joystick.%2.name").arg(selectedInputConfigLine()).arg(dialog.capturedDevice()));
        markInputOwnedKey(QStringLiteral("input.%1.joystick.%2.empty").arg(selectedInputConfigLine()).arg(dialog.capturedDevice()));
        markInputOwnedKey(QStringLiteral("input.%1.joystick.%2.disabled").arg(selectedInputConfigLine()).arg(dialog.capturedDevice()));
        setInputSlotForKey(capturedKey, inputSubEvent ? inputSubEvent->currentIndex() : 0, slot);
        refreshInputMappingList(capturedKey);
    }

    QWidget *makeInputPage()
    {
        QWidget *page = makePage();
        QVBoxLayout *root = new QVBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);

        QGridLayout *top = new QGridLayout;
        top->setColumnStretch(1, 1);
        inputType = combo({
            QStringLiteral("Configuration #1"),
            QStringLiteral("Configuration #2"),
            QStringLiteral("Configuration #3"),
            QStringLiteral("Game Ports")
        }, QStringLiteral("Game Ports"));
        inputDevice = combo({
            QStringLiteral("Keyboard"),
            QStringLiteral("Mouse"),
            QStringLiteral("Joystick")
        });
        inputDeviceEnabled = new QCheckBox(QStringLiteral("Device enabled"));
        top->addWidget(inputType, 0, 0);
        top->addWidget(inputDevice, 0, 1);
        top->addWidget(inputDeviceEnabled, 0, 2, Qt::AlignRight | Qt::AlignVCenter);
        root->addLayout(top);

        inputMappingList = new QTreeWidget;
        inputMappingList->setRootIsDecorated(false);
        inputMappingList->setAlternatingRowColors(true);
        inputMappingList->setSelectionMode(QAbstractItemView::SingleSelection);
        inputMappingList->setHeaderLabels({
            QStringLiteral("Host widget"),
            QStringLiteral("Amiga event"),
            QStringLiteral("Autofire"),
            QStringLiteral("Toggle"),
            QStringLiteral("Invert"),
            QStringLiteral("Qualifier"),
            QStringLiteral("#")
        });
        inputMappingList->header()->setStretchLastSection(false);
        inputMappingList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        for (int i = 1; i < inputMappingList->columnCount(); i++) {
            inputMappingList->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
        }
        root->addWidget(inputMappingList, 1);

        QHBoxLayout *eventRow = new QHBoxLayout;
        inputSubEvent = combo({ QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7"), QStringLiteral("8") });
        inputAmigaEvent = new QComboBox;
        inputAmigaEvent->setEditable(false);
        QPushButton *inputTest = new QPushButton(QStringLiteral("Test"));
        inputRemapButton = new QPushButton(QStringLiteral("Remap"));
        connect(inputTest, &QPushButton::clicked, this, [this]() { openInputMapDialog(-1); });
        connect(inputRemapButton, &QPushButton::clicked, this, [this]() { openInputRemapCapture(); });
        eventRow->addWidget(inputSubEvent);
        eventRow->addWidget(inputAmigaEvent, 1);
        eventRow->addWidget(inputTest);
        eventRow->addWidget(inputRemapButton);
        root->addLayout(eventRow);

        QGridLayout *bottom = new QGridLayout;
        bottom->setColumnStretch(1, 1);
        bottom->setColumnStretch(3, 1);
        inputDeadzone = new QSpinBox;
        inputDeadzone->setRange(0, 100);
        inputDeadzone->setSuffix(QStringLiteral("%"));
        inputAutofireRate = new QSpinBox;
        inputAutofireRate->setRange(1, 10000);
        inputJoyMouseDigital = new QSpinBox;
        inputJoyMouseDigital->setRange(1, 1000);
        inputJoyMouseAnalog = new QSpinBox;
        inputJoyMouseAnalog->setRange(1, 1000);
        inputCopyFrom = combo({
            QStringLiteral("Custom #1"),
            QStringLiteral("Custom #2"),
            QStringLiteral("Custom #3"),
            QStringLiteral("Default"),
            QStringLiteral("Default (PC KB)")
        });
        inputCopyButton = new QPushButton(QStringLiteral("Copy from:"));
        inputSwapButton = new QPushButton(QStringLiteral("Swap 1<>2"));
        inputPageUpEnd = new QCheckBox(QStringLiteral("Page Up = End"));
        inputSwapBackslashF11 = new QCheckBox(QStringLiteral("Swap Backslash/F11"));
        inputSwapBackslashF11->setTristate(true);

        bottom->addWidget(label(QStringLiteral("Joystick dead zone (%):")), 0, 0);
        bottom->addWidget(inputDeadzone, 0, 1);
        bottom->addWidget(label(QStringLiteral("Digital joy-mouse speed:")), 0, 2);
        bottom->addWidget(inputJoyMouseDigital, 0, 3);
        bottom->addWidget(inputCopyButton, 0, 4);
        bottom->addWidget(label(QStringLiteral("Autofire rate (lines):")), 1, 0);
        bottom->addWidget(inputAutofireRate, 1, 1);
        bottom->addWidget(label(QStringLiteral("Analog joy-mouse speed:")), 1, 2);
        bottom->addWidget(inputJoyMouseAnalog, 1, 3);
        bottom->addWidget(inputCopyFrom, 1, 4);
        bottom->addWidget(inputPageUpEnd, 2, 1);
        bottom->addWidget(inputSwapBackslashF11, 2, 2, 1, 2);
        bottom->addWidget(inputSwapButton, 2, 4);
        root->addLayout(bottom);

        connect(inputType, &QComboBox::currentTextChanged, this, [this](const QString &) { refreshInputMappingList(); });
        connect(inputDevice, &QComboBox::currentTextChanged, this, [this](const QString &) { refreshInputMappingList(); });
        connect(inputSubEvent, &QComboBox::currentIndexChanged, this, [this](int) { refreshInputMappingList(selectedInputMappingKey()); });
        connect(inputAmigaEvent, &QComboBox::currentIndexChanged, this, [this](int) {
            setSelectedInputEvent(inputAmigaEvent->currentData().toString());
        });
        connect(inputMappingList, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *, QTreeWidgetItem *) {
            updateInputControlState();
        });
        connect(inputMappingList, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *, int column) {
            if (column == 1) {
                toggleSelectedInputSetMode();
            } else if (column == 2) {
                toggleSelectedInputAutofire();
            } else if (column == 3) {
                toggleSelectedInputFlag(InputFlagToggle);
            } else if (column == 4) {
                toggleSelectedInputFlag(InputFlagInvert);
            } else if (column == 6 && inputSubEvent) {
                inputSubEvent->setCurrentIndex((inputSubEvent->currentIndex() + 1) % MaxInputSubEventSlots);
            }
        });
        connect(inputCopyButton, &QPushButton::clicked, this, [this]() { copyInputMappings(); });
        connect(inputSwapButton, &QPushButton::clicked, this, [this]() { swapInputMappings(); });
        connect(inputDeviceEnabled, &QCheckBox::toggled, this, [this](bool enabled) { setInputDeviceEnabled(enabled); });

        refreshInputMappingList();
        return page;
    }

    void refreshInputMappingList(const QString &preferredKey = QString())
    {
        if (!inputMappingList || !inputDevice) {
            return;
        }
        const QString selectedKey = preferredKey.isEmpty() ? selectedInputMappingKey() : preferredKey;
        const QSignalBlocker blocker(inputMappingList);
        inputMappingList->clear();

        for (const WinUaeQtInputRow &row : currentInputRows()) {
            const WinUaeQtInputSlot slot = selectedInputSlotForKey(row.key);
            QTreeWidgetItem *item = new QTreeWidgetItem(inputMappingList);
            item->setText(0, row.label);
            item->setText(1, slot.custom ? QStringLiteral("<Custom Event>: %1").arg(slot.event) : winUaeQtInputEventDisplayName(slot.event));
            item->setText(2, inputAutofireText(slot));
            item->setText(3, inputFlagText(slot, InputFlagToggle));
            item->setText(4, inputFlagText(slot, InputFlagInvert));
            item->setText(5, slot.qualifiers.isEmpty() ? QStringLiteral("-") : slot.qualifiers);
            item->setText(6, row.key.isEmpty() ? QStringLiteral("-") : QString::number(qMax(1, inputMappingSubEventCount(row.key))));
            item->setData(0, InputMappingKeyRole, row.key);
            item->setData(0, InputMappingEditableRole, row.editable);
            if (!row.editable || inputGamePortsMode()) {
                item->setDisabled(true);
            }
            if (!selectedKey.isEmpty() && row.key == selectedKey) {
                inputMappingList->setCurrentItem(item);
            }
        }
        if (!inputMappingList->currentItem() && inputMappingList->topLevelItemCount() > 0) {
            inputMappingList->setCurrentItem(inputMappingList->topLevelItem(0));
        }
        updateInputControlState();
    }

    QWidget *makePathsPage()
    {
        QWidget *page = makePage();
        QVBoxLayout *root = new QVBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);
        QGridLayout *paths = new QGridLayout;
        paths->setColumnStretch(1, 1);
        romsPath = new QLineEdit;
        configsPath = new QLineEdit;
        nvramPath = new QLineEdit;
        screenshotsPath = new QLineEdit;
        stateFilesPath = new QLineEdit;
        videosPath = new QLineEdit;
        saveImagesPath = new QLineEdit;
        ripsPath = new QLineEdit;
        recursiveRoms = new QCheckBox(QStringLiteral("Scan subfolders"));
        cacheConfigurations = new QCheckBox(QStringLiteral("Cache Configuration files"));
        cacheBoxArt = new QCheckBox(QStringLiteral("Cache Boxart files"));
        saveImageOriginalPath = new QCheckBox(QStringLiteral("Use original image's path"));
        paths->addWidget(recursiveRoms, 0, 1);
        addLineBrowseRow(paths, 1, QStringLiteral("System ROMs:"), romsPath, true);
        QHBoxLayout *cacheOptions = new QHBoxLayout;
        cacheOptions->setContentsMargins(0, 0, 0, 0);
        cacheOptions->addWidget(cacheConfigurations);
        cacheOptions->addWidget(cacheBoxArt);
        cacheOptions->addStretch();
        paths->addLayout(cacheOptions, 2, 1, 1, 2);
        addLineBrowseRow(paths, 3, QStringLiteral("Configuration files:"), configsPath, true);
        addLineBrowseRow(paths, 4, QStringLiteral("NVRAM files:"), nvramPath, true);
        addLineBrowseRow(paths, 5, QStringLiteral("Screenshots:"), screenshotsPath, true);
        addLineBrowseRow(paths, 6, QStringLiteral("State files:"), stateFilesPath, true);
        addLineBrowseRow(paths, 7, QStringLiteral("Videos:"), videosPath, true);
        paths->addWidget(saveImageOriginalPath, 8, 1);
        addLineBrowseRow(paths, 9, QStringLiteral("Saveimages:"), saveImagesPath, true);
        addLineBrowseRow(paths, 10, QStringLiteral("Rips:"), ripsPath, true);
        root->addLayout(paths);

        QHBoxLayout *actions = new QHBoxLayout;
        QPushButton *setPath = new QPushButton(QStringLiteral("Set Path"));
        pathDefaultType = combo({ QStringLiteral("Application directory"), QStringLiteral("User data directory"), QStringLiteral("Custom data directory") }, QStringLiteral("User data directory"));
        actions->addWidget(setPath);
        actions->addWidget(pathDefaultType);
        actions->addStretch();
        relativePaths = new QCheckBox(QStringLiteral("Use relative paths"));
        disableUnavailable(relativePaths, QStringLiteral("Windows-style relative path saving is not implemented in the Unix configuration backend yet."));
        portableMode = new QCheckBox(QStringLiteral("Portable mode"));
        actions->addWidget(relativePaths);
        actions->addWidget(portableMode);
        root->addLayout(actions);

        QGridLayout *data = new QGridLayout;
        data->setColumnStretch(1, 1);
        dataPath = new QLineEdit;
        addLineBrowseRow(data, 0, QStringLiteral("Data path:"), dataPath, true);
        QPushButton *rescanRoms = new QPushButton(QStringLiteral("Rescan ROMs"));
        QPushButton *clearDiskHistory = new QPushButton(QStringLiteral("Clear disk history"));
        QPushButton *clearRegistry = new QPushButton(QStringLiteral("Clear registry"));
        disableUnavailable(clearRegistry, QStringLiteral("Windows registry reset does not apply to the Unix configuration backend."));
        data->addWidget(rescanRoms, 1, 0);
        data->addWidget(clearDiskHistory, 1, 1);
        data->addWidget(clearRegistry, 1, 2);
        root->addLayout(data);

        QGridLayout *logging = new QGridLayout;
        logging->setColumnStretch(0, 1);
        logSelect = combo({ QStringLiteral("winuaebootlog.txt"), QStringLiteral("winuaelog.txt"), QStringLiteral("Current configuration") });
        fullLogging = new QCheckBox(QStringLiteral("Enable full logging"));
        logWindow = new QCheckBox(QStringLiteral("Log window"));
        QPushButton *saveAllLogs = new QPushButton(QStringLiteral("Save All"));
        QPushButton *openLog = new QPushButton(QStringLiteral("Open"));
        logPath = new QLineEdit;
        logPath->setReadOnly(true);
        logging->addWidget(logSelect, 0, 0);
        logging->addWidget(fullLogging, 0, 1);
        logging->addWidget(logWindow, 0, 2);
        logging->addWidget(saveAllLogs, 0, 3);
        logging->addWidget(openLog, 1, 3);
        logging->addWidget(logPath, 1, 0, 1, 3);
        root->addWidget(groupBox(QStringLiteral("Debug logging"), logging));
        root->addStretch(1);
        connect(configsPath, &QLineEdit::textChanged, this, [this]() { refreshConfigList(); });
        connect(configsPath, &QLineEdit::textChanged, this, [this]() { refreshFrontendPage(); });
        connect(configsPath, &QLineEdit::textChanged, this, [this]() { updateLogPathText(); });
        connect(setPath, &QPushButton::clicked, this, [this]() { applySelectedPathDefaults(); });
        connect(logSelect, &QComboBox::currentTextChanged, this, [this](const QString &) { updateLogPathText(); });
        connect(rescanRoms, &QPushButton::clicked, this, [this]() { rescanRomPathCandidates(true); });
        connect(clearDiskHistory, &QPushButton::clicked, this, [this]() { clearDiskHistoryCombos(); });
        connect(saveAllLogs, &QPushButton::clicked, this, [this]() { saveAllDebugLogs(); });
        connect(openLog, &QPushButton::clicked, this, [this]() { openSelectedLog(); });
        if (configName) {
            connect(configName, &QComboBox::currentTextChanged, this, [this](const QString &) { updateLogPathText(); });
        }
        return page;
    }

    bool isRomCandidateFile(const QFileInfo &info) const
    {
        if (!info.isFile() || info.size() <= 0 || info.size() >= 10 * 1024 * 1024) {
            return false;
        }
        const QString suffix = info.suffix().toLower();
        return suffix == QStringLiteral("rom")
            || suffix == QStringLiteral("bin")
            || suffix == QStringLiteral("kick")
            || suffix == QStringLiteral("a500")
            || suffix == QStringLiteral("a600")
            || suffix == QStringLiteral("a1200")
            || suffix == QStringLiteral("a4000")
            || suffix == QStringLiteral("cd32")
            || suffix == QStringLiteral("cdtv");
    }

    void collectRomCandidates(const QDir &dir, int depth, QStringList *paths) const
    {
        if (!dir.exists() || !paths) {
            return;
        }
        const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &info : files) {
            if (isRomCandidateFile(info)) {
                paths->append(info.absoluteFilePath());
            }
        }

        const bool recursive = recursiveRoms && recursiveRoms->isChecked();
        if (!recursive || depth >= 2) {
            return;
        }
        const QFileInfoList dirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &info : dirs) {
            collectRomCandidates(QDir(info.absoluteFilePath()), depth + 1, paths);
        }
    }

    void replacePathComboItems(QComboBox *field, const QStringList &paths)
    {
        if (!field) {
            return;
        }
        const QString current = field->currentText();
        QSignalBlocker blocker(field);
        field->clear();
        field->addItems(paths);
        if (!current.isEmpty() && field->findText(current) < 0) {
            field->insertItem(0, current);
        }
        field->setCurrentText(current);
    }

    void clearPathComboHistory(QComboBox *field)
    {
        if (!field) {
            return;
        }
        const QString current = field->currentText();
        QSignalBlocker blocker(field);
        field->clear();
        if (!current.isEmpty()) {
            field->addItem(current);
        }
        field->setCurrentText(current);
    }

    void clearDiskHistoryCombos()
    {
        for (int i = 0; i < 4; i++) {
            clearPathComboHistory(dfPath[i]);
        }
        for (int i = 0; i < 2; i++) {
            clearPathComboHistory(quickDfPath[i]);
        }
        clearPathComboHistory(diskSwapperPath);
        clearPathComboHistory(cdSlotPath);
        clearPathComboHistory(stateFileName);
        clearPathComboHistory(genlockFile);
        status->setText(QStringLiteral("Disk history cleared"));
    }

    void rescanRomPathCandidates(bool showResult)
    {
        const QString root = romsPath ? expandedPathText(romsPath->text()) : QString();
        QStringList candidates;
        if (!root.isEmpty()) {
            collectRomCandidates(QDir(root), 0, &candidates);
        }
        candidates.removeDuplicates();
        candidates.sort(Qt::CaseInsensitive);

        replacePathComboItems(romFile, candidates);
        replacePathComboItems(extendedRomFile, candidates);
        replacePathComboItems(cartFile, candidates);

        if (showResult) {
            if (candidates.isEmpty()) {
                QMessageBox::information(this, windowTitle(), QStringLiteral("No ROM files were found in the configured System ROMs path."));
            } else {
                status->setText(QStringLiteral("Found %1 ROM file%2").arg(candidates.size()).arg(candidates.size() == 1 ? QString() : QStringLiteral("s")));
            }
        }
    }

    void applySelectedPathDefaults()
    {
        QString base;
        if (pathDefaultType && pathDefaultType->currentText() == QStringLiteral("Application directory")) {
            base = QCoreApplication::applicationDirPath();
        } else if (pathDefaultType && pathDefaultType->currentText() == QStringLiteral("Custom data directory")) {
            base = dataPath ? dataPath->text().trimmed() : QString();
        }
        if (base.isEmpty()) {
            base = unixDefaultDataPath();
        }
        if (dataPath) {
            dataPath->setText(base);
        }
        const QDir dir(base);
        if (romsPath) {
            romsPath->setText(dir.filePath(QStringLiteral("Kickstarts")));
        }
        if (configsPath) {
            configsPath->setText(dir.filePath(QStringLiteral("Configuration")));
        }
        if (nvramPath) {
            nvramPath->setText(dir.filePath(QStringLiteral("NVRAMs")));
        }
        if (screenshotsPath) {
            screenshotsPath->setText(dir.filePath(QStringLiteral("Screenshots")));
        }
        if (stateFilesPath) {
            stateFilesPath->setText(dir.filePath(QStringLiteral("Save States")));
        }
        if (videosPath) {
            videosPath->setText(dir.filePath(QStringLiteral("Videos")));
        }
        if (saveImagesPath) {
            saveImagesPath->setText(dir.filePath(QStringLiteral("SaveImages")));
        }
        if (ripsPath) {
            ripsPath->setText(dir.filePath(QStringLiteral("Rips")));
        }
        refreshConfigList();
    }

    QByteArray currentConfigText() const
    {
        QString text = QStringLiteral("; WinUAE Unix Qt current configuration\n");
        const WinUaeQtConfig config = mergedConfig();
        for (const WinUaeQtConfig::Setting &setting : config.orderedSettings()) {
            if (!setting.value.isEmpty()) {
                text += QStringLiteral("%1=%2\n").arg(setting.key, setting.value);
            }
        }
        return text.toUtf8();
    }

    void updateLogPathText()
    {
        if (!logSelect || !logPath) {
            return;
        }
        const QString selected = logSelect->currentText();
        if (selected == QStringLiteral("Current configuration")) {
            logPath->setText(currentConfigReportPath());
        } else {
            logPath->setText(logFilePath(selected));
        }
    }

    QString logFilePath(const QString &name) const
    {
        return QDir::temp().filePath(name);
    }

    QString currentConfigReportPath() const
    {
        return QDir::temp().filePath(QStringLiteral("winuae_config_%1.%2.%3.txt")
            .arg(WINUAE_UNIX_VERSION_MAJOR)
            .arg(WINUAE_UNIX_VERSION_MINOR)
            .arg(WINUAE_UNIX_VERSION_REVISION));
    }

    QString selectedLogPath() const
    {
        if (!logSelect) {
            return QString();
        }
        if (logSelect->currentText() == QStringLiteral("Current configuration")) {
            return currentConfigReportPath();
        }
        return logPath ? logPath->text().trimmed() : QString();
    }

    bool writeCurrentConfigReport(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Could not write %1:\n%2").arg(path, file.errorString()));
            return false;
        }
        const QByteArray text = currentConfigText();
        if (file.write(text) != text.size()) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Could not write %1:\n%2").arg(path, file.errorString()));
            return false;
        }
        return true;
    }

    void openSelectedLog()
    {
        const bool currentConfigSelected = logSelect && logSelect->currentText() == QStringLiteral("Current configuration");
        const QString path = selectedLogPath();
        if (path.isEmpty()) {
            return;
        }
        if (currentConfigSelected) {
            if (!writeCurrentConfigReport(path)) {
                return;
            }
        } else if (!QFileInfo::exists(path)) {
            QMessageBox::information(this, windowTitle(), QStringLiteral("The selected log file does not exist yet:\n%1").arg(path));
            return;
        }

        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Could not open %1.").arg(path));
        }
    }

    void addLogZipEntry(QVector<WinUaeQtZipEntry> *entries, const QString &entryName, const QString &path) const
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return;
        }
        entries->append({ entryName.toUtf8(), file.readAll() });
    }

    void saveAllDebugLogs()
    {
        const QString defaultName = QStringLiteral("winuae_debug_%1.%2.%3.zip")
            .arg(WINUAE_UNIX_VERSION_MAJOR)
            .arg(WINUAE_UNIX_VERSION_MINOR)
            .arg(WINUAE_UNIX_VERSION_REVISION);
        const QString path = QFileDialog::getSaveFileName(
            this,
            QStringLiteral("Save debug information"),
            fileDialogInitialSavePath(QString(), defaultName),
            QStringLiteral("Zip files (*.zip);;All files (*)"));
        if (path.isEmpty()) {
            return;
        }

        QVector<WinUaeQtZipEntry> entries;
        addLogZipEntry(&entries, QStringLiteral("winuaebootlog.txt"), logFilePath(QStringLiteral("winuaebootlog.txt")));
        addLogZipEntry(&entries, QStringLiteral("winuaelog.txt"), logFilePath(QStringLiteral("winuaelog.txt")));
        entries.append({ QByteArray("config.uae"), currentConfigText() });

        QString error;
        if (!writeStoredZip(path, entries, &error)) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Could not save %1:\n%2").arg(path, error));
        }
    }

    QWidget *makeMiscPage()
    {
        QWidget *page = makePage();
        QHBoxLayout *root = new QHBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);
        miscOptionList = new QListWidget;
        miscOptionList->setAlternatingRowColors(true);
        miscOptionItems.clear();
        for (const MiscCheckChoice &choice : miscCheckChoices) {
            const QString key = QString::fromLatin1(choice.key);
            QListWidgetItem *item = new QListWidgetItem(QString::fromLatin1(choice.display), miscOptionList);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(choice.defaultChecked ? Qt::Checked : Qt::Unchecked);
            if (!miscCheckChoiceEnabled(key)) {
                item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
                item->setCheckState(Qt::Unchecked);
                item->setToolTip(miscCheckChoiceDisabledReason(key));
            }
            miscOptionItems.insert(key, item);
        }
        root->addWidget(miscOptionList, 2);

        QVBoxLayout *right = new QVBoxLayout;
        right->setSpacing(6);
        QGridLayout *miscOptions = new QGridLayout;
        miscOptions->setHorizontalSpacing(8);
        miscOptions->setVerticalSpacing(5);
        miscOptions->setColumnStretch(1, 1);
        miscScsiMode = combo(configChoiceDisplays(scsiModeChoices, int(sizeof(scsiModeChoices) / sizeof(scsiModeChoices[0]))));
        miscWindowedStyle = combo({ QStringLiteral("Borderless"), QStringLiteral("Minimal"), QStringLiteral("Standard"), QStringLiteral("Extended") });
        miscVideoApi = combo({ QStringLiteral("Unix video backend") });
        miscVideoApiOptions = combo({ QStringLiteral("Default") });
        disableUnavailable(miscWindowedStyle, QStringLiteral("Windows window-style selector does not apply to the Unix SDL/Qt window backend."));
        disableUnavailable(miscVideoApi, QStringLiteral("Unix video backend selection is not configurable from the Qt frontend yet."));
        disableUnavailable(miscVideoApiOptions, QStringLiteral("Unix video backend options are not configurable from the Qt frontend yet."));
        miscOptions->addWidget(label(QStringLiteral("SCSI and CD/DVD access:")), 0, 0);
        miscOptions->addWidget(miscScsiMode, 0, 1);
        miscOptions->addWidget(label(QStringLiteral("Windowed style:")), 1, 0);
        miscOptions->addWidget(miscWindowedStyle, 1, 1);
        miscOptions->addWidget(miscVideoApi, 2, 1);
        miscOptions->addWidget(miscVideoApiOptions, 3, 1);
        right->addWidget(groupBox(QStringLiteral("Miscellaneous Options"), miscOptions));

        QVBoxLayout *gui = new QVBoxLayout;
        gui->setSpacing(5);
        miscLanguage = combo({ QStringLiteral("Autodetect"), QStringLiteral("English (built-in)") });
        QPushButton *guiFont = new QPushButton(QStringLiteral("GUI Font..."));
        QPushButton *osdFont = new QPushButton(QStringLiteral("OSD Font..."));
        QPushButton *setDefault = new QPushButton(QStringLiteral("Set default"));
        miscGuiSize = combo({
            QStringLiteral("Select..."),
            QStringLiteral("200%"),
            QStringLiteral("190%"),
            QStringLiteral("180%"),
            QStringLiteral("170%"),
            QStringLiteral("160%"),
            QStringLiteral("150%"),
            QStringLiteral("140%"),
            QStringLiteral("130%"),
            QStringLiteral("120%"),
            QStringLiteral("110%"),
            QStringLiteral("100%"),
            QStringLiteral("90%"),
            QStringLiteral("80%"),
            QStringLiteral("70%"),
            QStringLiteral("60%")
        });
        QPushButton *resetLists = new QPushButton(QStringLiteral("Reset list customizations"));
        miscGuiResize = new QCheckBox(QStringLiteral("Resizeable GUI"));
        miscGuiFullscreen = new QCheckBox(QStringLiteral("Fullscreen GUI"));
        miscGuiDarkMode = new QCheckBox(QStringLiteral("Dark mode"));
        miscGuiDarkMode->setTristate(true);
        disableUnavailable(osdFont, QStringLiteral("OSD font selection is not implemented yet."));
        disableUnavailable(resetLists, QStringLiteral("List customization storage is not implemented in the Unix Qt frontend yet."));
        miscGuiDarkMode->setToolTip(QStringLiteral("Matches Windows: unchecked is light, checked is dark, mixed follows the system appearance."));

        QHBoxLayout *fontRow = new QHBoxLayout;
        fontRow->setContentsMargins(0, 0, 0, 0);
        fontRow->setSpacing(6);
        fontRow->addWidget(guiFont);
        fontRow->addWidget(osdFont);

        QHBoxLayout *scaleRow = new QHBoxLayout;
        scaleRow->setContentsMargins(0, 0, 0, 0);
        scaleRow->setSpacing(6);
        scaleRow->addWidget(setDefault);
        scaleRow->addWidget(miscGuiSize, 1);

        gui->addWidget(miscLanguage);
        gui->addLayout(fontRow);
        gui->addLayout(scaleRow);
        gui->addWidget(resetLists);
        gui->addWidget(miscGuiResize);
        gui->addWidget(miscGuiFullscreen);
        gui->addWidget(miscGuiDarkMode);
        right->addWidget(groupBox(QStringLiteral("GUI"), gui));

        QGridLayout *stateFiles = new QGridLayout;
        stateFiles->setColumnStretch(0, 1);
        stateFileName = pathCombo();
        stateFileClear = new QCheckBox;
        QPushButton *loadState = new QPushButton(QStringLiteral("Load state..."));
        QPushButton *saveState = new QPushButton(QStringLiteral("Save state..."));
        QPushButton *browseState = smallButton(QStringLiteral("..."));
        saveState->setEnabled(false);
        saveState->setToolTip(QStringLiteral("Saving emulator state requires runtime save-state support in the Unix GUI."));
        stateFiles->addWidget(stateFileName, 0, 0);
        stateFiles->addWidget(stateFileClear, 0, 1);
        stateFiles->addWidget(browseState, 0, 2);
        stateFiles->addWidget(loadState, 1, 0);
        stateFiles->addWidget(saveState, 1, 1, 1, 2);
        right->addWidget(groupBox(QStringLiteral("State Files"), stateFiles));

        QGridLayout *keyboard = new QGridLayout;
        for (int i = 0; i < 3; i++) {
            keyboardLed[i] = combo(configChoiceDisplays(keyboardLedChoices, int(sizeof(keyboardLedChoices) / sizeof(keyboardLedChoices[0]))));
            keyboard->addWidget(keyboardLed[i], 0, i);
        }
        keyboardLedUsb = new QCheckBox(QStringLiteral("USB mode"));
        disableUnavailable(keyboardLedUsb, QStringLiteral("Native host keyboard LED output is not implemented on Unix yet."));
        keyboard->addWidget(keyboardLedUsb, 0, 3);
        right->addWidget(groupBox(QStringLiteral("Keyboard LEDs"), keyboard));
        root->addLayout(right, 1);

        const auto selectStateFile = [this]() {
            const QString selected = QFileDialog::getOpenFileName(this, QStringLiteral("Select state file"), fileDialogInitialPath(stateFileName->currentText()), QStringLiteral("WinUAE state files (*.uss);;All files (*)"));
            if (!selected.isEmpty()) {
                setPathComboText(stateFileName, selected);
                stateFileClear->setChecked(false);
                status->setText(QStringLiteral("State file %1 selected for restore").arg(selected));
            }
        };
        connect(loadState, &QPushButton::clicked, this, selectStateFile);
        connect(browseState, &QPushButton::clicked, this, selectStateFile);
        connect(stateFileName, &QComboBox::currentTextChanged, this, [this](const QString &text) {
            stateFileClear->setChecked(text.trimmed().isEmpty());
        });
        connect(guiFont, &QPushButton::clicked, this, [this]() { chooseGuiFont(); });
        connect(setDefault, &QPushButton::clicked, this, [this]() { applyGuiScaleSelection(); });
        connect(miscGuiSize, &QComboBox::currentTextChanged, this, [this](const QString &text) {
            if (text != QStringLiteral("Select...")) {
                applyGuiScaleSelection();
            }
        });
        connect(miscGuiResize, &QCheckBox::toggled, this, [this](bool) { applyGuiResizeMode(); });
        connect(miscGuiFullscreen, &QCheckBox::toggled, this, [this](bool checked) { applyGuiFullscreenMode(checked); });
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        connect(miscGuiDarkMode, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState) { applyGuiDarkModeSelection(); });
#else
        connect(miscGuiDarkMode, QOverload<int>::of(&QCheckBox::stateChanged), this, [this](int) { applyGuiDarkModeSelection(); });
#endif
        return page;
    }

    int guiScalePercent(const QString &text) const
    {
        QString value = text.trimmed();
        if (value.endsWith(QLatin1Char('%'))) {
            value.chop(1);
        }
        bool ok = false;
        const int percent = value.trimmed().toInt(&ok);
        return ok && percent >= 60 && percent <= 200 ? percent : 100;
    }

    void applyGuiResizeMode()
    {
        if (!miscGuiResize || !miscGuiFullscreen) {
            return;
        }
        const bool resizable = miscGuiResize->isChecked() || miscGuiFullscreen->isChecked();
        setMinimumSize(820, 600);
        setMaximumSize(resizable ? QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX) : size());
        if (!resizable) {
            setFixedSize(size());
        }
    }

    void applyGuiScaleSelection(bool exitFullscreen = true)
    {
        if (!miscGuiSize) {
            return;
        }
        const int percent = guiScalePercent(miscGuiSize->currentText());
        const QSize baseSize(880, 640);
        const QSize minSize(820, 600);
        const QSize scaled(qMax(minSize.width(), baseSize.width() * percent / 100),
            qMax(minSize.height(), baseSize.height() * percent / 100));
        if (miscGuiFullscreen && miscGuiFullscreen->isChecked()) {
            if (!exitFullscreen) {
                return;
            }
            showNormal();
            miscGuiFullscreen->setChecked(false);
        }
        setMinimumSize(820, 600);
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        resize(scaled);
        applyGuiResizeMode();
    }

    void applyGuiFullscreenMode(bool fullscreen)
    {
        if (fullscreen) {
            setMinimumSize(820, 600);
            setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            showFullScreen();
        } else {
            showNormal();
            applyGuiResizeMode();
        }
    }

    QString guiDarkModeConfigValue() const
    {
        if (!miscGuiDarkMode) {
            return QStringLiteral("system");
        }
        switch (miscGuiDarkMode->checkState()) {
        case Qt::Checked:
            return QStringLiteral("dark");
        case Qt::Unchecked:
            return QStringLiteral("light");
        case Qt::PartiallyChecked:
            return QStringLiteral("system");
        }
        return QStringLiteral("system");
    }

    void setGuiDarkModeFromConfig(const QString &value)
    {
        if (!miscGuiDarkMode) {
            return;
        }
        const QString normalized = value.trimmed().toLower();
        QSignalBlocker blocker(miscGuiDarkMode);
        if (normalized == QStringLiteral("dark") || normalized == QStringLiteral("true") || normalized == QStringLiteral("1")) {
            miscGuiDarkMode->setCheckState(Qt::Checked);
        } else if (normalized == QStringLiteral("light") || normalized == QStringLiteral("false") || normalized == QStringLiteral("0")) {
            miscGuiDarkMode->setCheckState(Qt::Unchecked);
        } else {
            miscGuiDarkMode->setCheckState(Qt::PartiallyChecked);
        }
        applyGuiDarkModeSelection();
    }

    void applyGuiDarkModeSelection()
    {
        if (!miscGuiDarkMode || !qApp) {
            return;
        }
        bool dark = false;
        if (miscGuiDarkMode->checkState() == Qt::Checked) {
            dark = true;
        } else if (miscGuiDarkMode->checkState() == Qt::PartiallyChecked) {
            dark = systemPrefersDarkMode();
        }
        applyApplicationColors(*qApp, dark);
    }

    void applyGuiFont(const QFont &font, const QString &config)
    {
        miscGuiFontConfig = config;
        if (QApplication *app = qobject_cast<QApplication *>(QApplication::instance())) {
            app->setFont(font);
        }
        setFont(font);
    }

    void applyGuiFontConfig(const QString &config)
    {
        QFont font;
        if (font.fromString(config)) {
            applyGuiFont(font, config);
        }
    }

    void chooseGuiFont()
    {
        bool ok = false;
        const QFont selected = QFontDialog::getFont(&ok, font(), this, QStringLiteral("Select GUI Font"));
        if (ok) {
            applyGuiFont(selected, selected.toString());
        }
    }

    bool miscOptionChecked(const QString &key) const
    {
        QListWidgetItem *item = miscOptionItems.value(key, nullptr);
        return item && item->checkState() == Qt::Checked;
    }

    void setMiscOptionChecked(const QString &key, bool checked)
    {
        if (QListWidgetItem *item = miscOptionItems.value(key, nullptr)) {
            if (!miscCheckChoiceEnabled(key)) {
                item->setCheckState(Qt::Unchecked);
                return;
            }
            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        }
    }

    QGroupBox *makeActivityGroup(
        const QString &title,
        QComboBox **priority,
        QCheckBox **pause,
        QCheckBox **noSound,
        QCheckBox **noJoy,
        QCheckBox **noKeyboard = nullptr)
    {
        QGridLayout *layout = new QGridLayout;
        layout->setColumnStretch(0, 1);
        *priority = combo(activityPriorityDisplays(), QStringLiteral("Normal"));
        *pause = new QCheckBox(QStringLiteral("Pause emulation"));
        *noSound = new QCheckBox(QStringLiteral("Disable sound"));
        *noJoy = new QCheckBox(QStringLiteral("Disable game controllers"));
        layout->addWidget(label(QStringLiteral("Run at priority:")), 0, 0);
        layout->addWidget(*priority, 1, 0);
        layout->addWidget(*pause, 3, 0);
        layout->addWidget(*noSound, 4, 0);
        layout->addWidget(*noJoy, 5, 0);
        if (noKeyboard) {
            *noKeyboard = new QCheckBox(QStringLiteral("Disable keyboard"));
            layout->addWidget(label(QStringLiteral("Mouse uncaptured:")), 2, 0);
            layout->addWidget(*noKeyboard, 6, 0);
        }
        return groupBox(title, layout);
    }

    QWidget *makeExtensionsPage()
    {
        QWidget *page = makePage();
        QVBoxLayout *root = new QVBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);
        root->setSpacing(6);

        QHBoxLayout *activity = new QHBoxLayout;
        activity->setSpacing(6);
        activity->addWidget(makeActivityGroup(
            QStringLiteral("When Active"),
            &extensionActivePriority,
            &extensionActivePause,
            &extensionActiveNoSound,
            &extensionActiveNoJoy,
            &extensionActiveNoKeyboard));
        activity->addWidget(makeActivityGroup(
            QStringLiteral("When Inactive"),
            &extensionInactivePriority,
            &extensionInactivePause,
            &extensionInactiveNoSound,
            &extensionInactiveNoJoy));
        activity->addWidget(makeActivityGroup(
            QStringLiteral("When Minimized"),
            &extensionMinimizedPriority,
            &extensionMinimizedPause,
            &extensionMinimizedNoSound,
            &extensionMinimizedNoJoy));
        root->addLayout(activity);

        QVBoxLayout *associations = new QVBoxLayout;
        extensionAssociationList = new QTreeWidget;
        extensionAssociationList->setRootIsDecorated(false);
        extensionAssociationList->setAlternatingRowColors(true);
        extensionAssociationList->setSelectionMode(QAbstractItemView::SingleSelection);
        extensionAssociationList->setEditTriggers(QAbstractItemView::NoEditTriggers);
        extensionAssociationList->setHeaderLabels({ QStringLiteral("Extension"), QStringLiteral("Associated") });
        for (int i = 0; i < int(sizeof(associationChoices) / sizeof(associationChoices[0])); i++) {
            QTreeWidgetItem *item = new QTreeWidgetItem(extensionAssociationList);
            const QString extension = QString::fromLatin1(associationChoices[i].extension);
            item->setText(0, extension);
            if (extension == QStringLiteral(".uae")) {
                item->setText(1, QStringLiteral("Declared by app bundle"));
                item->setToolTip(1, QStringLiteral("The macOS app bundle declares .uae files and the Qt frontend handles document-open events."));
            } else {
                item->setText(1, QStringLiteral("Deferred"));
                item->setToolTip(1, QStringLiteral("Windows starts these files through extension-specific command lines; Unix needs matching startup mapping before declaring them."));
            }
        }
        extensionAssociationList->resizeColumnToContents(0);
        associations->addWidget(extensionAssociationList, 1);

        QHBoxLayout *associationActions = new QHBoxLayout;
        associationActions->addStretch();
        QPushButton *associateAll = new QPushButton(QStringLiteral("Associate all"));
        QPushButton *deassociateAll = new QPushButton(QStringLiteral("Deassociate all"));
        disableUnavailable(associateAll, QStringLiteral("Unix file-association install/uninstall is not implemented; macOS uses bundle document declarations."));
        disableUnavailable(deassociateAll, QStringLiteral("Unix file-association install/uninstall is not implemented; macOS uses bundle document declarations."));
        associationActions->addWidget(associateAll);
        associationActions->addWidget(deassociateAll);
        associationActions->addStretch();
        associations->addLayout(associationActions);
        root->addWidget(groupBox(QStringLiteral("File Extension Associations"), associations), 1);

        const QList<QCheckBox*> boxes = {
            extensionActivePause,
            extensionActiveNoSound,
            extensionActiveNoJoy,
            extensionActiveNoKeyboard,
            extensionInactivePause,
            extensionInactiveNoSound,
            extensionInactiveNoJoy,
            extensionMinimizedPause,
            extensionMinimizedNoSound,
            extensionMinimizedNoJoy
        };
        for (QCheckBox *box : boxes) {
            connect(box, &QCheckBox::toggled, this, [this]() { updateExtensionActivityState(); });
        }
        return page;
    }

    void updateExtensionActivityState()
    {
        if (!extensionActivePause || !extensionInactivePause || !extensionMinimizedPause) {
            return;
        }

        bool paused = extensionActivePause->isChecked();
        bool noSound = extensionActiveNoSound->isChecked();
        bool noJoy = extensionActiveNoJoy->isChecked();

        if (paused) {
            setCheckBoxIfChanged(extensionActiveNoSound, true);
            setCheckBoxIfChanged(extensionActiveNoJoy, true);
            setCheckBoxIfChanged(extensionActiveNoKeyboard, true);
            noSound = true;
            noJoy = true;
        }
        extensionActiveNoSound->setEnabled(!paused);
        extensionActiveNoJoy->setEnabled(!paused);
        extensionActiveNoKeyboard->setEnabled(!paused);

        if (paused) {
            setCheckBoxIfChanged(extensionInactivePause, true);
        }
        if (paused || noSound) {
            setCheckBoxIfChanged(extensionInactiveNoSound, true);
        }
        if (paused || noJoy) {
            setCheckBoxIfChanged(extensionInactiveNoJoy, true);
        }
        extensionInactivePause->setEnabled(!paused);
        extensionInactiveNoSound->setEnabled(!paused && !noSound);
        extensionInactiveNoJoy->setEnabled(!paused && !noJoy);

        paused = paused || extensionInactivePause->isChecked();
        noSound = noSound || extensionInactiveNoSound->isChecked();
        noJoy = noJoy || extensionInactiveNoJoy->isChecked();

        if (paused) {
            setCheckBoxIfChanged(extensionMinimizedPause, true);
        }
        if (paused || noSound) {
            setCheckBoxIfChanged(extensionMinimizedNoSound, true);
        }
        if (paused || noJoy) {
            setCheckBoxIfChanged(extensionMinimizedNoJoy, true);
        }
        extensionMinimizedPause->setEnabled(!paused);
        extensionMinimizedNoSound->setEnabled(!paused && !noSound);
        extensionMinimizedNoJoy->setEnabled(!paused && !noJoy);
    }

    void setExtensionPriorityValue(QComboBox *box, int value)
    {
        if (box) {
            box->setCurrentText(activityPriorityText(value));
        }
    }

    int extensionPriorityValue(QComboBox *box) const
    {
        return box ? activityPriorityValue(box->currentText()) : 0;
    }

    QWidget *makeFrontendPage()
    {
        QWidget *page = makePage();
        QHBoxLayout *root = new QHBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);
        root->setSpacing(8);

        frontendConfigList = new QTreeWidget;
        frontendConfigList->setRootIsDecorated(false);
        frontendConfigList->setAlternatingRowColors(true);
        frontendConfigList->setSelectionMode(QAbstractItemView::SingleSelection);
        frontendConfigList->setEditTriggers(QAbstractItemView::NoEditTriggers);
        frontendConfigList->setHeaderLabels({ QStringLiteral("Configuration"), QStringLiteral("Description") });
        root->addWidget(frontendConfigList, 3);

        QVBoxLayout *right = new QVBoxLayout;
        QVBoxLayout *screenshotLayout = new QVBoxLayout;
        frontendScreenshot = new QLabel;
        frontendScreenshot->setFrameShape(QFrame::Box);
        frontendScreenshot->setAlignment(Qt::AlignCenter);
        frontendScreenshot->setMinimumSize(160, 128);
        screenshotLayout->addWidget(frontendScreenshot, 1);
        right->addWidget(groupBox(QString(), screenshotLayout), 3);

        QVBoxLayout *infoLayout = new QVBoxLayout;
        frontendInfo = new QLabel;
        frontendInfo->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        frontendInfo->setWordWrap(true);
        frontendInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
        infoLayout->addWidget(frontendInfo, 1);
        right->addWidget(groupBox(QString(), infoLayout), 2);
        root->addLayout(right, 2);

        connect(frontendConfigList, &QTreeWidget::itemSelectionChanged, this, [this]() { updateFrontendSelection(); });
        connect(frontendConfigList, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
            const QString path = item ? item->data(0, Qt::UserRole).toString() : QString();
            if (!path.isEmpty()) {
                loadConfig(path);
            }
        });
        return page;
    }

    QString frontendMediaArtDirectory(const QString &mediaPath) const
    {
        if (mediaPath.trimmed().isEmpty()) {
            return QString();
        }
        QDir dir(QFileInfo(mediaPath).absolutePath());
        if (QFileInfo::exists(dir.filePath(QStringLiteral("___Title.png")))) {
            return dir.absolutePath();
        }
        return QString();
    }

    QString frontendArtDirectory(const WinUaeQtConfig &config) const
    {
        const QString floppy = config.value(QStringLiteral("floppy0"));
        QString path = frontendMediaArtDirectory(floppy);
        if (!path.isEmpty()) {
            return path;
        }

        const QString cd = config.value(QStringLiteral("cdimage0"));
        if (!cd.isEmpty()
            && cd.compare(QStringLiteral("autodetect"), Qt::CaseInsensitive) != 0
            && cd.compare(QStringLiteral("empty"), Qt::CaseInsensitive) != 0) {
            const QStringList fields = winUaeQtConfigFieldList(cd);
            path = frontendMediaArtDirectory(fields.value(0));
            if (!path.isEmpty()) {
                return path;
            }
        }
        return QString();
    }

    QString frontendArtImage(const QString &artDirectory) const
    {
        if (artDirectory.isEmpty()) {
            return QString();
        }
        const QStringList names = {
            QStringLiteral("___Title.png"),
            QStringLiteral("___SShot.png"),
            QStringLiteral("___Boxart.png")
        };
        const QDir dir(artDirectory);
        for (const QString &name : names) {
            const QString path = dir.filePath(name);
            if (QFileInfo::exists(path)) {
                return path;
            }
        }
        return QString();
    }

    void refreshFrontendPage()
    {
        if (!frontendConfigList) {
            return;
        }
        const QString selectedPath = frontendConfigList->currentItem()
            ? frontendConfigList->currentItem()->data(0, Qt::UserRole).toString()
            : (configPath ? configPath->text() : QString());
        QSignalBlocker blocker(frontendConfigList);
        frontendConfigList->clear();

        QDir dir(configurationDirectory());
        const QFileInfoList files = dir.entryInfoList({ QStringLiteral("*.uae") }, QDir::Files, QDir::Name | QDir::IgnoreCase);
        QTreeWidgetItem *selected = nullptr;
        for (const QFileInfo &info : files) {
            WinUaeQtConfig config;
            config.load(info.absoluteFilePath());
            const QString description = config.value(QStringLiteral("config_description"));
            const QString artDirectory = frontendArtDirectory(config);
            QTreeWidgetItem *item = new QTreeWidgetItem(frontendConfigList);
            item->setText(0, info.completeBaseName());
            item->setText(1, description);
            item->setData(0, Qt::UserRole, info.absoluteFilePath());
            item->setData(0, Qt::UserRole + 1, description);
            item->setData(0, Qt::UserRole + 2, artDirectory);
            if (info.absoluteFilePath() == selectedPath) {
                selected = item;
            }
        }
        for (int i = 0; i < frontendConfigList->columnCount(); i++) {
            frontendConfigList->resizeColumnToContents(i);
        }
        if (selected) {
            frontendConfigList->setCurrentItem(selected);
        } else if (frontendConfigList->topLevelItemCount() > 0) {
            frontendConfigList->setCurrentItem(frontendConfigList->topLevelItem(0));
        }
        updateFrontendSelection();
    }

    void updateFrontendSelection()
    {
        if (!frontendConfigList || !frontendScreenshot || !frontendInfo) {
            return;
        }
        QTreeWidgetItem *item = frontendConfigList->currentItem();
        if (!item) {
            frontendScreenshot->clear();
            frontendInfo->clear();
            return;
        }

        const QString path = item->data(0, Qt::UserRole).toString();
        const QString description = item->data(0, Qt::UserRole + 1).toString();
        const QString artImage = frontendArtImage(item->data(0, Qt::UserRole + 2).toString());
        if (!artImage.isEmpty()) {
            QPixmap image(artImage);
            frontendScreenshot->setPixmap(image.scaled(frontendScreenshot->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            frontendScreenshot->clear();
        }

        QStringList lines;
        lines.append(QFileInfo(path).completeBaseName());
        if (!description.isEmpty()) {
            lines.append(description);
        }
        lines.append(path);
        frontendInfo->setText(lines.join(QLatin1Char('\n')));
    }

    QWidget *makeAboutPage()
    {
        QWidget *page = makePage();
        QVBoxLayout *root = new QVBoxLayout(page);
        root->setContentsMargins(4, 4, 4, 4);
        root->addStretch();
        QLabel *title = new QLabel(QStringLiteral("WinUAE"));
        QFont font = title->font();
        font.setPointSize(22);
        font.setBold(true);
        title->setFont(font);
        title->setAlignment(Qt::AlignCenter);
        root->addWidget(title);
        QLabel *version = new QLabel(versionString());
        QFont versionFont = version->font();
        versionFont.setPointSize(13);
        version->setFont(versionFont);
        version->setAlignment(Qt::AlignCenter);
        root->addWidget(version);
        QLabel *subtitle = new QLabel(QStringLiteral("Unix Qt configuration frontend"));
        subtitle->setAlignment(Qt::AlignCenter);
        root->addWidget(subtitle);
        QPushButton *contributors = new QPushButton(QStringLiteral("Contributors"));
        contributors->setFixedWidth(120);
        connect(contributors, &QPushButton::clicked, this, [this]() { showContributors(); });
        QHBoxLayout *buttonRow = new QHBoxLayout;
        buttonRow->addStretch();
        buttonRow->addWidget(contributors);
        buttonRow->addStretch();
        root->addLayout(buttonRow);
        QGridLayout *links = new QGridLayout;
        links->setHorizontalSpacing(24);
        links->setVerticalSpacing(16);
        for (int i = 0; i < int(sizeof(aboutLinks) / sizeof(aboutLinks[0])); i++) {
            QLabel *link = new QLabel(QStringLiteral("<a href=\"%1\">%2</a>")
                .arg(QString::fromLatin1(aboutLinks[i].url), QString::fromLatin1(aboutLinks[i].display)));
            link->setAlignment(Qt::AlignCenter);
            link->setOpenExternalLinks(true);
            link->setTextInteractionFlags(Qt::TextBrowserInteraction);
            links->addWidget(link, i / 3, i % 3);
        }
        root->addSpacing(20);
        root->addLayout(links);
        root->addStretch();
        return page;
    }

    void showContributors()
    {
        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("UAE Authors and Contributors..."));
        dialog.resize(620, 420);

        QVBoxLayout *root = new QVBoxLayout(&dialog);
        QListWidget *list = new QListWidget;
        list->addItems(contributorLines());
        root->addWidget(list, 1);

        QPushButton *ok = new QPushButton(QStringLiteral("Ok"));
        ok->setDefault(true);
        connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);

        QHBoxLayout *buttons = new QHBoxLayout;
        buttons->addStretch();
        buttons->addWidget(ok);
        buttons->addStretch();
        root->addLayout(buttons);

        dialog.exec();
    }

    void addPathRow(QGridLayout *layout, int row, const QString &caption, QComboBox *field, const QString &dialogTitle, const QString &filter)
    {
        QPushButton *browse = smallButton(QStringLiteral("..."));
        layout->addWidget(new QLabel(caption), row * 2, 0, 1, 2);
        layout->addWidget(field, row * 2 + 1, 0, 1, 2);
        layout->addWidget(browse, row * 2 + 1, 2);
        connect(browse, &QPushButton::clicked, this, [this, field, dialogTitle, filter]() {
            addBrowse(field, this, dialogTitle, filter);
        });
    }

    void addLineBrowseRow(QGridLayout *layout, int row, const QString &caption, QLineEdit *field, bool directory = false)
    {
        QPushButton *browse = smallButton(QStringLiteral("..."));
        layout->addWidget(label(caption), row, 0);
        layout->addWidget(field, row, 1);
        layout->addWidget(browse, row, 2);
        connect(browse, &QPushButton::clicked, this, [this, field, directory, caption]() {
            const QString initialPath = directory
                ? fileDialogInitialDirectory(field->text())
                : fileDialogInitialPath(field->text());
            QString path;
            if (directory) {
                path = QFileDialog::getExistingDirectory(this, caption, initialPath);
            } else {
                path = QFileDialog::getOpenFileName(this, caption, initialPath, QStringLiteral("All files (*)"));
            }
            if (!path.isEmpty()) {
                field->setText(path);
            }
        });
    }

    void ensureCdSlots()
    {
        if (cdSlots.size() == MaxCdSlots) {
            return;
        }
        cdSlots = QVector<WinUaeQtCdSlot>(MaxCdSlots);
        for (WinUaeQtCdSlot &slot : cdSlots) {
            slot.type = QStringLiteral("Image file");
        }
    }

    void clearCdSlots()
    {
        ensureCdSlots();
        for (WinUaeQtCdSlot &slot : cdSlots) {
            slot = WinUaeQtCdSlot();
            slot.type = QStringLiteral("Image file");
        }
        currentCdSlot = 0;
        loadCdSlotToUi(currentCdSlot);
    }

    void setCurrentCdSlotInUse(bool inUse)
    {
        ensureCdSlots();
        if (currentCdSlot >= 0 && currentCdSlot < cdSlots.size()) {
            cdSlots[currentCdSlot].inUse = inUse;
        }
    }

    void storeCurrentCdSlotFromUi()
    {
        ensureCdSlots();
        if (!cdSlotPath || !cdSlotType || currentCdSlot < 0 || currentCdSlot >= cdSlots.size()) {
            return;
        }
        WinUaeQtCdSlot &slot = cdSlots[currentCdSlot];
        slot.path = cdSlotPath->currentText().trimmed();
        slot.type = cdSlotType->currentText();
        if (!slot.path.isEmpty()) {
            slot.inUse = true;
        } else if (slot.type == QStringLiteral("Image file")) {
            slot.inUse = false;
        }
    }

    WinUaeQtCdSlot cdSlotState(int index) const
    {
        WinUaeQtCdSlot slot = index >= 0 && index < cdSlots.size() ? cdSlots.value(index) : WinUaeQtCdSlot();
        if (index == currentCdSlot && cdSlotPath && cdSlotType) {
            slot.path = cdSlotPath->currentText().trimmed();
            slot.type = cdSlotType->currentText();
            if (!slot.path.isEmpty()) {
                slot.inUse = true;
            } else if (slot.type == QStringLiteral("Image file")) {
                slot.inUse = false;
            }
        }
        return slot;
    }

    void loadCdSlotToUi(int index)
    {
        ensureCdSlots();
        if (!cdSlotNumber || !cdSlotPath || !cdSlotType || index < 0 || index >= cdSlots.size()) {
            return;
        }
        cdSlotUpdating = true;
        QSignalBlocker numberBlocker(cdSlotNumber);
        QSignalBlocker pathBlocker(cdSlotPath);
        QSignalBlocker typeBlocker(cdSlotType);
        cdSlotNumber->setCurrentIndex(index);
        cdSlotPath->setCurrentText(cdSlots[index].path);
        cdSlotType->setCurrentText(cdSlots[index].type.isEmpty() ? QStringLiteral("Image file") : cdSlots[index].type);
        cdSlotUpdating = false;
    }

    void ensureCustomRomBoards()
    {
        if (customRomBoards.size() == MaxRomBoards) {
            return;
        }
        customRomBoards.clear();
        customRomBoards.resize(MaxRomBoards);
        currentCustomRomBoard = 0;
    }

    void clearCustomRomBoards()
    {
        ensureCustomRomBoards();
        for (WinUaeQtRomBoard &board : customRomBoards) {
            board = WinUaeQtRomBoard();
        }
        currentCustomRomBoard = 0;
        if (customRomSelect) {
            QSignalBlocker blocker(customRomSelect);
            customRomSelect->setCurrentIndex(0);
        }
        loadCurrentCustomRomBoard();
    }

    void storeCurrentCustomRomBoard()
    {
        if (customRomUpdating || !customRomStart || !customRomEnd || !customRomFile) {
            return;
        }
        ensureCustomRomBoards();
        if (currentCustomRomBoard < 0 || currentCustomRomBoard >= customRomBoards.size()) {
            return;
        }
        WinUaeQtRomBoard &board = customRomBoards[currentCustomRomBoard];
        board.start = normalizedRomAddress(customRomStart->text(), false);
        board.end = normalizedRomAddress(customRomEnd->text(), true);
        board.path = customRomFile->text().trimmed();
    }

    void loadCurrentCustomRomBoard()
    {
        if (!customRomStart || !customRomEnd || !customRomFile) {
            return;
        }
        ensureCustomRomBoards();
        currentCustomRomBoard = qBound(0, currentCustomRomBoard, MaxRomBoards - 1);
        customRomUpdating = true;
        QSignalBlocker startBlocker(customRomStart);
        QSignalBlocker endBlocker(customRomEnd);
        QSignalBlocker fileBlocker(customRomFile);
        if (customRomSelect) {
            QSignalBlocker selectBlocker(customRomSelect);
            customRomSelect->setCurrentIndex(currentCustomRomBoard);
        }
        const WinUaeQtRomBoard &board = customRomBoards[currentCustomRomBoard];
        customRomStart->setText(board.start);
        customRomEnd->setText(board.end);
        customRomFile->setText(board.path);
        customRomUpdating = false;
    }

    void applyCustomRomBoard(int index, const QString &value)
    {
        if (index < 0 || index >= MaxRomBoards) {
            return;
        }
        ensureCustomRomBoards();
        customRomBoards[index] = romBoardFromConfigValue(value);
        if (index == currentCustomRomBoard) {
            loadCurrentCustomRomBoard();
        }
    }

    void updateFloppySpeedLabel()
    {
        if (!floppySpeed || !floppySpeedLabel) {
            return;
        }
        floppySpeedLabel->setText(floppySpeedText(floppySpeedConfigValue(floppySpeed->value())));
    }

    void updateCpuSpeedLabel()
    {
        if (!cpuSpeed || !cpuSpeedLabel) {
            return;
        }
        const int value = cpuSpeed->value() * 10;
        cpuSpeedLabel->setText(QStringLiteral("%1%2%").arg(value >= 0 ? QStringLiteral("+") : QString()).arg(value));
    }

    void updateJitCacheLabel()
    {
        if (!jitCache || !jitCacheLabel) {
            return;
        }
        jitCacheLabel->setText(jitCacheText(jit->isChecked() ? jitCacheSizeFromPosition(jitCache->value()) : 0));
    }

    void updateCpuControlState()
    {
        const int cpu = selectedCpuModel();
        const int fpu = fpuModelConfigValue(cpu);
        const bool z3Rtg = rtgNeeds32BitAddressSpace();
        bool jitEnabled = jit && jit->isChecked();
        if (cpu24Bit) {
            cpu24Bit->setEnabled(cpu <= 68030 && !z3Rtg);
            cpu24Bit->setToolTip(z3Rtg ? QStringLiteral("Zorro III RTG requires a 32-bit address space.") : QString());
            if (z3Rtg && cpu24Bit->isChecked()) {
                cpu24Bit->setChecked(false);
            }
        }
        if (jit) {
            jit->setEnabled(unixJitBackendAvailable() && cpu >= 68020 && !cpu24Bit->isChecked());
            if (!jit->isEnabled() && jit->isChecked()) {
                jit->setChecked(false);
            }
            jitEnabled = jit->isChecked();
        }
        const bool cycleExactEnabled = !jitEnabled;
        if (chipsetCycleExact) {
            chipsetCycleExact->setEnabled(cycleExactEnabled);
            chipsetCycleExact->setToolTip(jitEnabled ? QStringLiteral("Cycle-exact emulation is not compatible with JIT.") : QString());
        }
        if (chipsetCycleExactMemory) {
            chipsetCycleExactMemory->setEnabled(cycleExactEnabled);
            chipsetCycleExactMemory->setToolTip(jitEnabled ? QStringLiteral("Cycle-exact emulation is not compatible with JIT.") : QString());
        }
        if (cpuDataCache) {
            cpuDataCache->setEnabled(cpu >= 68030 && moreCompatible->isChecked() && !jitEnabled);
        }
        if (cpuUnimplemented) {
            cpuUnimplemented->setEnabled(cpu == 68060 && !jitEnabled);
        }
        if (mmuButtons) {
            const bool mmuEnabled = cpu >= 68030 && !jitEnabled;
            for (QAbstractButton *button : mmuButtons->buttons()) {
                button->setEnabled(button == mmuButtons->button(0) || mmuEnabled);
            }
            if (!mmuEnabled && mmuButtons->checkedId() != 0) {
                mmuButtons->button(0)->setChecked(true);
            }
        }
        const bool hasFpu = fpu != 0;
        if (fpuStrict) {
            fpuStrict->setEnabled(hasFpu);
        }
        if (fpuUnimplemented) {
            fpuUnimplemented->setEnabled(hasFpu && !jitEnabled);
        }
        if (fpuMode) {
            fpuMode->setEnabled(hasFpu);
        }
        const bool jitOptions = jitEnabled && cpu >= 68020 && !cpu24Bit->isChecked();
        for (QWidget *widget : { static_cast<QWidget *>(jitCache), static_cast<QWidget *>(jitCacheLabel), static_cast<QWidget *>(jitConstJump), static_cast<QWidget *>(jitHardFlush), static_cast<QWidget *>(jitNoFlags), static_cast<QWidget *>(jitCatchFault) }) {
            if (widget) {
                widget->setEnabled(jitOptions);
            }
        }
        if (jitFpu) {
            jitFpu->setEnabled(jitOptions && hasFpu);
        }
        if (jitTrust) {
            for (QAbstractButton *button : jitTrust->buttons()) {
                button->setEnabled(jitOptions);
            }
        }
        updateJitCacheLabel();
    }

    bool rtgNeeds32BitAddressSpace() const
    {
        const int configType = rtgConfigType(rtgType ? rtgType->currentText() : QString());
        return rtgMem && rtgType
            && rtgMem->currentText() != QStringLiteral("None")
            && (configType == 3 || configType == 7);
    }

    int rtgConfigType(const QString &type) const
    {
        for (const WinUaeQtRtgBoardCatalogItem &board : boardCatalog.rtgBoards) {
            if (type.compare(board.configValue, Qt::CaseInsensitive) == 0) {
                return board.configType;
            }
        }
        if (type.compare(QStringLiteral("ZorroII"), Qt::CaseInsensitive) == 0
            || type.endsWith(QStringLiteral("_Z2"), Qt::CaseInsensitive)) {
            return 2;
        }
        if (type.compare(QStringLiteral("ZorroIII"), Qt::CaseInsensitive) == 0
            || type.endsWith(QStringLiteral("_Z3"), Qt::CaseInsensitive)) {
            return 3;
        }
        if (type.endsWith(QStringLiteral("_PCI"), Qt::CaseInsensitive)) {
            return 7;
        }
        return 0;
    }

    QString rtgBusName(const QString &type) const
    {
        switch (rtgConfigType(type)) {
            case 2:
                return QStringLiteral("Z2");
            case 3:
                return QStringLiteral("Z3");
            case 7:
                return QStringLiteral("PCI");
            default:
                break;
        }
        return QStringLiteral("-");
    }

    WinUaeQtFilterState defaultFilterState(int target) const
    {
        WinUaeQtFilterState state;
        if (target == 1) {
            state.autoscale = QStringLiteral("resize");
        } else if (target == 2) {
            state.enable = false;
        }
        return state;
    }

    void resetFilterStates()
    {
        for (int i = 0; i < 3; i++) {
            filterStates[i] = defaultFilterState(i);
        }
        currentFilterTarget = 0;
        if (filterTarget) {
            filterTarget->setCurrentIndex(0);
            loadFilterStateToUi(0);
        }
        if (filterNtscPixels) {
            filterNtscPixels->setChecked(true);
        }
    }

    void updateFilterAutoscaleItems(int target)
    {
        if (!filterAutoscale) {
            return;
        }
        const QString current = filterAutoscale->currentText();
        filterAutoscale->clear();
        if (target == 1) {
            filterAutoscale->addItems(configChoiceDisplays(filterAutoscaleRtgChoices, int(sizeof(filterAutoscaleRtgChoices) / sizeof(filterAutoscaleRtgChoices[0]))));
        } else {
            filterAutoscale->addItems(configChoiceDisplays(filterAutoscaleChoices, int(sizeof(filterAutoscaleChoices) / sizeof(filterAutoscaleChoices[0]))));
        }
        if (!current.isEmpty()) {
            setComboTextIfChanged(filterAutoscale, current);
        }
    }

    WinUaeQtFilterState filterStateFromUi(int target) const
    {
        WinUaeQtFilterState state = filterStates[qBound(0, target, 2)];
        if (!filterTarget || target != currentFilterTarget) {
            return state;
        }
        state.enable = filterEnable->isChecked();
        state.filter = configChoiceValue(filterModeChoices, int(sizeof(filterModeChoices) / sizeof(filterModeChoices[0])), filterMode->currentText());
        state.modeH = configChoiceValue(filterModeHChoices, int(sizeof(filterModeHChoices) / sizeof(filterModeHChoices[0])), filterModeH->currentText());
        state.modeV = configChoiceValue(filterModeVChoices, int(sizeof(filterModeVChoices) / sizeof(filterModeVChoices[0])), filterModeV->currentText());
        state.autoscale = filterAutoscaleConfigValue(filterAutoscale->currentText(), target);
        state.integerLimit = configChoiceValue(filterIntegerLimitChoices, int(sizeof(filterIntegerLimitChoices) / sizeof(filterIntegerLimitChoices[0])), filterIntegerLimit->currentText());
        state.keepAspect = filterKeepAspect->isChecked()
            ? configChoiceValue(filterAspectChoices, int(sizeof(filterAspectChoices) / sizeof(filterAspectChoices[0])), filterAspect->currentText())
            : QStringLiteral("none");
        state.keepAutoscaleAspect = filterKeepAutoscaleAspect->isChecked();
        state.bilinear = filterBilinear->isChecked();
        state.horizZoom = filterHorizZoom->value();
        state.vertZoom = filterVertZoom->value();
        state.horizZoomMult = filterHorizZoomMult->value();
        state.vertZoomMult = filterVertZoomMult->value();
        state.horizOffset = filterHorizOffset->value();
        state.vertOffset = filterVertOffset->value();
        state.scanlines = filterScanlines->value();
        state.scanlineLevel = filterScanlineLevel->value();
        state.scanlineOffset = filterScanlineOffset->value();
        state.luminance = filterLuminance->value();
        state.contrast = filterContrast->value();
        state.saturation = filterSaturation->value();
        state.gamma = filterGamma->value();
        state.blur = filterBlur->value();
        state.noise = filterNoise->value();
        return state;
    }

    void storeFilterUiToState()
    {
        if (!filterTarget || filterUpdating) {
            return;
        }
        filterStates[currentFilterTarget] = filterStateFromUi(currentFilterTarget);
    }

    void updateFilterControlState()
    {
        if (!filterTarget) {
            return;
        }
        const int target = filterTarget->currentIndex();
        filterEnable->setEnabled(target == 2);
        filterAspect->setEnabled(filterKeepAspect->isChecked());
    }

    void loadFilterStateToUi(int target)
    {
        if (!filterTarget) {
            return;
        }
        filterUpdating = true;
        const WinUaeQtFilterState state = filterStates[qBound(0, target, 2)];
        updateFilterAutoscaleItems(target);
        filterEnable->setChecked(state.enable);
        filterMode->setCurrentText(configChoiceDisplay(filterModeChoices, int(sizeof(filterModeChoices) / sizeof(filterModeChoices[0])), state.filter));
        filterModeH->setCurrentText(configChoiceDisplay(filterModeHChoices, int(sizeof(filterModeHChoices) / sizeof(filterModeHChoices[0])), state.modeH));
        filterModeV->setCurrentText(configChoiceDisplay(filterModeVChoices, int(sizeof(filterModeVChoices) / sizeof(filterModeVChoices[0])), state.modeV));
        filterAutoscale->setCurrentText(filterAutoscaleDisplay(state.autoscale, target));
        filterIntegerLimit->setCurrentText(configChoiceDisplay(filterIntegerLimitChoices, int(sizeof(filterIntegerLimitChoices) / sizeof(filterIntegerLimitChoices[0])), state.integerLimit));
        filterKeepAspect->setChecked(state.keepAspect != QStringLiteral("none"));
        filterAspect->setCurrentText(configChoiceDisplay(filterAspectChoices, int(sizeof(filterAspectChoices) / sizeof(filterAspectChoices[0])), state.keepAspect));
        filterKeepAutoscaleAspect->setChecked(state.keepAutoscaleAspect);
        filterBilinear->setChecked(state.bilinear);
        filterHorizZoom->setValue(state.horizZoom);
        filterVertZoom->setValue(state.vertZoom);
        filterHorizZoomMult->setValue(state.horizZoomMult);
        filterVertZoomMult->setValue(state.vertZoomMult);
        filterHorizOffset->setValue(state.horizOffset);
        filterVertOffset->setValue(state.vertOffset);
        filterScanlines->setValue(state.scanlines);
        filterScanlineLevel->setValue(state.scanlineLevel);
        filterScanlineOffset->setValue(state.scanlineOffset);
        filterLuminance->setValue(state.luminance);
        filterContrast->setValue(state.contrast);
        filterSaturation->setValue(state.saturation);
        filterGamma->setValue(state.gamma);
        filterBlur->setValue(state.blur);
        filterNoise->setValue(state.noise);
        filterUpdating = false;
        updateFilterControlState();
    }

    QMap<QString, QString> filterPresetSettings(const WinUaeQtFilterState &state, int target) const
    {
        QMap<QString, QString> settings;
        settings.insert(QStringLiteral("gfx_filter_mode"), state.modeH);
        settings.insert(QStringLiteral("gfx_filter_mode2"), state.modeV);
        settings.insert(QStringLiteral("gfx_filter_horiz_zoomf"), QString::number(state.horizZoom, 'f', 1));
        settings.insert(QStringLiteral("gfx_filter_vert_zoomf"), QString::number(state.vertZoom, 'f', 1));
        settings.insert(QStringLiteral("gfx_filter_horiz_zoom_multf"), QString::number(state.horizZoomMult, 'f', 2));
        settings.insert(QStringLiteral("gfx_filter_vert_zoom_multf"), QString::number(state.vertZoomMult, 'f', 2));
        settings.insert(QStringLiteral("gfx_filter_horiz_offsetf"), QString::number(state.horizOffset, 'f', 1));
        settings.insert(QStringLiteral("gfx_filter_vert_offsetf"), QString::number(state.vertOffset, 'f', 1));
        settings.insert(QStringLiteral("gfx_filter_keep_aspect"), state.keepAspect);
        settings.insert(QStringLiteral("gfx_filter_keep_autoscale_aspect"), state.keepAutoscaleAspect ? QStringLiteral("1") : QStringLiteral("0"));
        settings.insert(QStringLiteral("gfx_filter_autoscale"), target == 1 && !isRtgAutoscaleValue(state.autoscale) ? QStringLiteral("resize") : state.autoscale);
        settings.insert(QStringLiteral("gfx_filter_autoscale_limit"), state.integerLimit);
        settings.insert(QStringLiteral("gfx_filter_luminance"), QString::number(state.luminance));
        settings.insert(QStringLiteral("gfx_filter_contrast"), QString::number(state.contrast));
        settings.insert(QStringLiteral("gfx_filter_saturation"), QString::number(state.saturation));
        settings.insert(QStringLiteral("gfx_filter_gamma"), QString::number(state.gamma));
        settings.insert(QStringLiteral("gfx_filter_blur"), QString::number(state.blur));
        settings.insert(QStringLiteral("gfx_filter_noise"), QString::number(state.noise));
        settings.insert(QStringLiteral("gfx_filter_bilinear"), state.bilinear ? QStringLiteral("1") : QStringLiteral("0"));
        settings.insert(QStringLiteral("gfx_filter_scanlines"), QString::number(state.scanlines));
        settings.insert(QStringLiteral("gfx_filter_scanlinelevel"), QString::number(state.scanlineLevel));
        settings.insert(QStringLiteral("gfx_filter_scanlineoffset"), QString::number(state.scanlineOffset));
        settings.insert(QStringLiteral("gfx_filter_enable"), state.enable ? QStringLiteral("1") : QStringLiteral("0"));
        return settings;
    }

    QString filterPresetDirectoryPath() const
    {
        QString base = dataPath ? dataPath->text().trimmed() : QString();
        if (base.isEmpty()) {
            base = unixDefaultDataPath();
        }
        return QDir(expandedPathText(base)).filePath(QStringLiteral("FilterPresets"));
    }

    QString normalizedFilterPresetName(QString name) const
    {
        name = name.trimmed();
        QString result;
        for (const QChar ch : name) {
            result.append(ch.isLetterOrNumber() || ch == QLatin1Char(' ') || ch == QLatin1Char('_') || ch == QLatin1Char('-')
                ? ch
                : QLatin1Char('_'));
        }
        result = result.simplified();
        return result.isEmpty() ? QStringLiteral("Preset") : result.left(80);
    }

    QString filterPresetPath(const QString &name) const
    {
        return QDir(filterPresetDirectoryPath()).filePath(normalizedFilterPresetName(name) + QStringLiteral(".filter"));
    }

    void refreshFilterPresetList(const QString &preferred = QString())
    {
        if (!filterPresetName) {
            return;
        }
        const QString current = preferred.isEmpty() ? filterPresetName->currentText().trimmed() : preferred;
        QSignalBlocker blocker(filterPresetName);
        filterPresetName->clear();
        filterPresetName->addItem(QString());
        QDir dir(filterPresetDirectoryPath());
        const QStringList files = dir.entryList({ QStringLiteral("*.filter") }, QDir::Files, QDir::Name | QDir::IgnoreCase);
        for (const QString &file : files) {
            filterPresetName->addItem(QFileInfo(file).completeBaseName());
        }
        filterPresetName->setCurrentText(current);
    }

    bool writeFilterPresetFile(const QString &path, const QMap<QString, QString> &settings)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Could not write %1:\n%2").arg(path, file.errorString()));
            return false;
        }
        QTextStream out(&file);
        out << "# WinUAE Qt filter preset\n";
        for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
            out << it.key() << "=" << it.value() << "\n";
        }
        return true;
    }

    void saveFilterPreset()
    {
        if (!filterPresetName) {
            return;
        }
        QString name = normalizedFilterPresetName(filterPresetName->currentText());
        if (name.isEmpty()) {
            name = QStringLiteral("Preset");
        }
        QDir dir(filterPresetDirectoryPath());
        if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Could not create %1.").arg(dir.absolutePath()));
            return;
        }
        storeFilterUiToState();
        const QString path = filterPresetPath(name);
        if (writeFilterPresetFile(path, filterPresetSettings(filterStates[currentFilterTarget], currentFilterTarget))) {
            refreshFilterPresetList(name);
            status->setText(QStringLiteral("Filter preset saved: %1").arg(name));
        }
    }

    void loadFilterPreset()
    {
        if (!filterPresetName) {
            return;
        }
        const QString name = normalizedFilterPresetName(filterPresetName->currentText());
        if (name.isEmpty()) {
            return;
        }
        QFile file(filterPresetPath(name));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Could not read %1:\n%2").arg(file.fileName(), file.errorString()));
            return;
        }
        filterStates[currentFilterTarget] = defaultFilterState(currentFilterTarget);
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
                continue;
            }
            const int equals = line.indexOf(QLatin1Char('='));
            if (equals <= 0) {
                continue;
            }
            const QString key = line.left(equals).trimmed();
            const QString value = line.mid(equals + 1).trimmed();
            applyFilterSetting(filterKey(key, currentFilterTarget), value);
        }
        loadFilterStateToUi(currentFilterTarget);
        status->setText(QStringLiteral("Filter preset loaded: %1").arg(name));
    }

    void deleteFilterPreset()
    {
        if (!filterPresetName) {
            return;
        }
        const QString name = normalizedFilterPresetName(filterPresetName->currentText());
        if (name.isEmpty()) {
            return;
        }
        const QString path = filterPresetPath(name);
        if (QFile::exists(path) && !QFile::remove(path)) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Could not delete %1.").arg(path));
            return;
        }
        refreshFilterPresetList();
        status->setText(QStringLiteral("Filter preset deleted: %1").arg(name));
    }

    bool applyFilterSetting(const QString &key, const QString &value)
    {
        const QStringList bases = {
            QStringLiteral("gfx_filter_mode"),
            QStringLiteral("gfx_filter_mode2"),
            QStringLiteral("gfx_filter_horiz_zoomf"),
            QStringLiteral("gfx_filter_vert_zoomf"),
            QStringLiteral("gfx_filter_horiz_zoom_multf"),
            QStringLiteral("gfx_filter_vert_zoom_multf"),
            QStringLiteral("gfx_filter_horiz_offsetf"),
            QStringLiteral("gfx_filter_vert_offsetf"),
            QStringLiteral("gfx_filter_keep_aspect"),
            QStringLiteral("gfx_filter_keep_autoscale_aspect"),
            QStringLiteral("gfx_filter_autoscale"),
            QStringLiteral("gfx_filter_autoscale_limit"),
            QStringLiteral("gfx_filter_luminance"),
            QStringLiteral("gfx_filter_contrast"),
            QStringLiteral("gfx_filter_saturation"),
            QStringLiteral("gfx_filter_gamma"),
            QStringLiteral("gfx_filter_blur"),
            QStringLiteral("gfx_filter_noise"),
            QStringLiteral("gfx_filter_bilinear"),
            QStringLiteral("gfx_filter_scanlines"),
            QStringLiteral("gfx_filter_scanlinelevel"),
            QStringLiteral("gfx_filter_scanlineoffset"),
            QStringLiteral("gfx_filter_enable")
        };
        for (int target = 0; target < 3; target++) {
            for (const QString &base : bases) {
                if (key != filterKey(base, target)) {
                    continue;
                }
                WinUaeQtFilterState &state = filterStates[target];
                if (base == QStringLiteral("gfx_filter_mode")) {
                    state.modeH = value;
                } else if (base == QStringLiteral("gfx_filter_mode2")) {
                    state.modeV = value;
                } else if (base == QStringLiteral("gfx_filter_horiz_zoomf")) {
                    state.horizZoom = value.toDouble();
                } else if (base == QStringLiteral("gfx_filter_vert_zoomf")) {
                    state.vertZoom = value.toDouble();
                } else if (base == QStringLiteral("gfx_filter_horiz_zoom_multf")) {
                    state.horizZoomMult = value.toDouble();
                } else if (base == QStringLiteral("gfx_filter_vert_zoom_multf")) {
                    state.vertZoomMult = value.toDouble();
                } else if (base == QStringLiteral("gfx_filter_horiz_offsetf")) {
                    state.horizOffset = value.toDouble();
                } else if (base == QStringLiteral("gfx_filter_vert_offsetf")) {
                    state.vertOffset = value.toDouble();
                } else if (base == QStringLiteral("gfx_filter_keep_aspect")) {
                    state.keepAspect = value;
                } else if (base == QStringLiteral("gfx_filter_keep_autoscale_aspect")) {
                    state.keepAutoscaleAspect = value.toInt() != 0 || configBoolValue(value);
                } else if (base == QStringLiteral("gfx_filter_autoscale")) {
                    state.autoscale = target == 1 && !isRtgAutoscaleValue(value) ? QStringLiteral("resize") : value;
                    if (target == 1) {
                        applyRtgScaleValue(state.autoscale);
                    }
                } else if (base == QStringLiteral("gfx_filter_autoscale_limit")) {
                    state.integerLimit = value;
                } else if (base == QStringLiteral("gfx_filter_luminance")) {
                    state.luminance = value.toInt();
                } else if (base == QStringLiteral("gfx_filter_contrast")) {
                    state.contrast = value.toInt();
                } else if (base == QStringLiteral("gfx_filter_saturation")) {
                    state.saturation = value.toInt();
                } else if (base == QStringLiteral("gfx_filter_gamma")) {
                    state.gamma = value.toInt();
                } else if (base == QStringLiteral("gfx_filter_blur")) {
                    state.blur = value.toInt();
                } else if (base == QStringLiteral("gfx_filter_noise")) {
                    state.noise = value.toInt();
                } else if (base == QStringLiteral("gfx_filter_bilinear")) {
                    state.bilinear = value.toInt() != 0 || configBoolValue(value);
                } else if (base == QStringLiteral("gfx_filter_scanlines")) {
                    state.scanlines = value.toInt();
                } else if (base == QStringLiteral("gfx_filter_scanlinelevel")) {
                    state.scanlineLevel = value.toInt();
                } else if (base == QStringLiteral("gfx_filter_scanlineoffset")) {
                    state.scanlineOffset = value.toInt();
                } else if (base == QStringLiteral("gfx_filter_enable")) {
                    state.enable = value.toInt() != 0 || configBoolValue(value);
                }
                if (target == currentFilterTarget) {
                    loadFilterStateToUi(target);
                }
                return true;
            }
        }
        return false;
    }

    void setAdvancedCheck(const QString &key, bool checked)
    {
        if (QCheckBox *box = advancedCheckBoxes.value(key, nullptr)) {
            setCheckBoxIfChanged(box, checked);
        }
    }

    bool advancedCheck(const QString &key) const
    {
        if (QCheckBox *box = advancedCheckBoxes.value(key, nullptr)) {
            return box->isChecked();
        }
        return false;
    }

    void setAdvancedRadioValue(QButtonGroup *group, const ConfigChoice *choices, int count, const QString &value)
    {
        if (!group) {
            return;
        }
        if (QAbstractButton *button = group->button(configChoiceIndex(choices, count, value))) {
            button->setChecked(true);
        }
    }

    QString advancedRadioValue(QButtonGroup *group, const ConfigChoice *choices, int count) const
    {
        return configChoiceValueAt(choices, count, group ? group->checkedId() : 0);
    }

    void setAdvancedRevision(QCheckBox *enabled, QLineEdit *field, int value, int defaultValue)
    {
        if (!enabled || !field) {
            return;
        }
        enabled->setChecked(value >= 0);
        field->setText(chipsetRevisionText(value >= 0 ? value : defaultValue));
        field->setEnabled(value >= 0);
    }

    void resetAdvancedChipsetBase()
    {
        if (!advancedRtcButtons) {
            return;
        }
        for (const AdvancedCheckChoice &choice : advancedCheckChoices) {
            setAdvancedCheck(QString::fromLatin1(choice.key), choice.defaultChecked);
        }
        setAdvancedRadioValue(advancedRtcButtons, rtcChoices, int(sizeof(rtcChoices) / sizeof(rtcChoices[0])), QStringLiteral("none"));
        setAdvancedRadioValue(advancedCiaTodButtons, ciaTodChoices, int(sizeof(ciaTodChoices) / sizeof(ciaTodChoices[0])), QStringLiteral("vblank"));
        advancedRtcAdjust->setText(QStringLiteral("0"));
        advancedIdeA600A1200->setChecked(false);
        advancedIdeA4000->setChecked(false);
        advancedScsiA3000->setChecked(false);
        advancedScsiA4000T->setChecked(false);
        advancedCia391078->setChecked(false);
        advancedUnmappedAddress->setCurrentText(QStringLiteral("Floating"));
        advancedCiaSync->setCurrentText(QStringLiteral("Autoselect"));
        setAdvancedRevision(advancedRamsey, advancedRamseyRevision, -1, 0x0f);
        setAdvancedRevision(advancedFatGary, advancedFatGaryRevision, -1, 0x00);
        advancedAgnusModel->setCurrentText(QStringLiteral("Auto"));
        advancedAgnusSize->setCurrentText(QStringLiteral("Auto"));
        advancedDeniseModel->setCurrentText(QStringLiteral("Auto"));
    }

    void setAdvancedCiaTodForPowerSupply()
    {
        setAdvancedRadioValue(advancedCiaTodButtons, ciaTodChoices, int(sizeof(ciaTodChoices) / sizeof(ciaTodChoices[0])),
            (ntsc && ntsc->isChecked()) ? QStringLiteral("60hz") : QStringLiteral("50hz"));
    }

    void applyAdvancedChipsetPreset(const QString &compatible)
    {
        if (!advancedRtcButtons) {
            return;
        }

        resetAdvancedChipsetBase();
        const QString value = compatible.trimmed();
        setCheckBoxIfChanged(advancedCompatible, value != QStringLiteral("-"));
        if (value == QStringLiteral("-")) {
            return;
        }
        if (value == QStringLiteral("CDTV")) {
            setAdvancedRadioValue(advancedRtcButtons, rtcChoices, int(sizeof(rtcChoices) / sizeof(rtcChoices[0])), QStringLiteral("MSM6242B"));
            setAdvancedCheck(QStringLiteral("cdtvcd"), true);
            setAdvancedCheck(QStringLiteral("cdtvram"), true);
            setAdvancedCheck(QStringLiteral("ksmirror_e0"), false);
        } else if (value == QStringLiteral("CDTV-CR")) {
            setAdvancedRadioValue(advancedRtcButtons, rtcChoices, int(sizeof(rtcChoices) / sizeof(rtcChoices[0])), QStringLiteral("MSM6242B"));
            setAdvancedCheck(QStringLiteral("cdtvcd"), true);
            setAdvancedCheck(QStringLiteral("cdtvram"), true);
            setAdvancedCheck(QStringLiteral("cdtv-cr"), true);
            setAdvancedCheck(QStringLiteral("ksmirror_e0"), false);
            setAdvancedCheck(QStringLiteral("ksmirror_a8"), true);
            setAdvancedCheck(QStringLiteral("cia_overlay"), false);
            setAdvancedCheck(QStringLiteral("resetwarning"), false);
            setAdvancedCheck(QStringLiteral("cia_todbug"), true);
            setAdvancedCheck(QStringLiteral("pcmcia"), true);
            advancedIdeA600A1200->setChecked(true);
        } else if (value == QStringLiteral("CD32")) {
            setAdvancedCheck(QStringLiteral("cd32cd"), true);
            setAdvancedCheck(QStringLiteral("cd32c2p"), true);
            setAdvancedCheck(QStringLiteral("cd32nvram"), true);
            setAdvancedCheck(QStringLiteral("ksmirror_e0"), false);
            setAdvancedCheck(QStringLiteral("ksmirror_a8"), true);
            setAdvancedCheck(QStringLiteral("cia_overlay"), false);
            setAdvancedCheck(QStringLiteral("resetwarning"), false);
            advancedUnmappedAddress->setCurrentText(QStringLiteral("All zeros"));
            advancedCiaSync->setCurrentText(QStringLiteral("Gayle"));
        } else if (value == QStringLiteral("A500")) {
            setAdvancedCheck(QStringLiteral("df0idhw"), false);
            setAdvancedCheck(QStringLiteral("resetwarning"), false);
            setAdvancedCheck(QStringLiteral("cia_todbug"), true);
        } else if (value == QStringLiteral("A500+")) {
            setAdvancedRadioValue(advancedRtcButtons, rtcChoices, int(sizeof(rtcChoices) / sizeof(rtcChoices[0])), QStringLiteral("MSM6242B"));
            setAdvancedCheck(QStringLiteral("resetwarning"), false);
            setAdvancedCheck(QStringLiteral("cia_todbug"), true);
        } else if (value == QStringLiteral("A600")) {
            advancedIdeA600A1200->setChecked(true);
            setAdvancedCheck(QStringLiteral("pcmcia"), true);
            setAdvancedCheck(QStringLiteral("ksmirror_a8"), true);
            setAdvancedCheck(QStringLiteral("cia_overlay"), false);
            setAdvancedCheck(QStringLiteral("resetwarning"), false);
            setAdvancedCheck(QStringLiteral("cia_todbug"), true);
            advancedCia391078->setChecked(true);
            advancedCiaSync->setCurrentText(QStringLiteral("Gayle"));
        } else if (value == QStringLiteral("A1000")) {
            setAdvancedCheck(QStringLiteral("a1000ram"), true);
            setAdvancedCiaTodForPowerSupply();
            setAdvancedCheck(QStringLiteral("ksmirror_e0"), false);
            setAdvancedCheck(QStringLiteral("cia_todbug"), true);
            advancedAgnusModel->setCurrentText(QStringLiteral("A1000"));
            advancedDeniseModel->setCurrentText(QStringLiteral("A1000"));
        } else if (value == QStringLiteral("A1200")) {
            advancedIdeA600A1200->setChecked(true);
            setAdvancedCheck(QStringLiteral("pcmcia"), true);
            setAdvancedCheck(QStringLiteral("ksmirror_a8"), true);
            setAdvancedCheck(QStringLiteral("cia_overlay"), false);
            advancedCiaSync->setCurrentText(QStringLiteral("Gayle"));
        } else if (value == QStringLiteral("A2000")) {
            setAdvancedRadioValue(advancedRtcButtons, rtcChoices, int(sizeof(rtcChoices) / sizeof(rtcChoices[0])), QStringLiteral("MSM6242B"));
            setAdvancedCiaTodForPowerSupply();
            setAdvancedCheck(QStringLiteral("cia_todbug"), true);
            advancedUnmappedAddress->setCurrentText(QStringLiteral("All zeros"));
        } else if (value == QStringLiteral("A3000") || value == QStringLiteral("A3000T")) {
            setAdvancedRadioValue(advancedRtcButtons, rtcChoices, int(sizeof(rtcChoices) / sizeof(rtcChoices[0])), QStringLiteral("RP5C01A"));
            setAdvancedRevision(advancedFatGary, advancedFatGaryRevision, 0x00, 0x00);
            setAdvancedRevision(advancedRamsey, advancedRamseyRevision, 0x0d, 0x0f);
            advancedScsiA3000->setChecked(true);
            setAdvancedCheck(QStringLiteral("ksmirror_e0"), false);
            setAdvancedCiaTodForPowerSupply();
            setAdvancedCheck(QStringLiteral("z3_autoconfig"), true);
            advancedUnmappedAddress->setCurrentText(QStringLiteral("All zeros"));
        } else if (value == QStringLiteral("A4000") || value == QStringLiteral("A4000T")) {
            setAdvancedRadioValue(advancedRtcButtons, rtcChoices, int(sizeof(rtcChoices) / sizeof(rtcChoices[0])), QStringLiteral("RP5C01A"));
            setAdvancedRevision(advancedFatGary, advancedFatGaryRevision, 0x00, 0x00);
            setAdvancedRevision(advancedRamsey, advancedRamseyRevision, 0x0f, 0x0f);
            advancedIdeA4000->setChecked(true);
            setAdvancedCheck(QStringLiteral("ksmirror_e0"), false);
            setAdvancedCheck(QStringLiteral("ksmirror_a8"), false);
            setAdvancedCheck(QStringLiteral("z3_autoconfig"), true);
            advancedUnmappedAddress->setCurrentText(QStringLiteral("All zeros"));
            advancedCiaSync->setCurrentText(QStringLiteral("Gayle"));
            if (value == QStringLiteral("A4000T")) {
                advancedScsiA4000T->setChecked(true);
            }
        } else if (value == QStringLiteral("Velvet")) {
            setAdvancedCiaTodForPowerSupply();
            setAdvancedCheck(QStringLiteral("ksmirror_e0"), false);
            advancedAgnusModel->setCurrentText(QStringLiteral("A1000"));
            advancedDeniseModel->setCurrentText(QStringLiteral("A1000 No-EHB"));
        }
    }

    void resetDefaults()
    {
        loadedConfig = WinUaeQtConfig();
        hardwareOrderOwnedKeys.clear();

        if (configName) {
            configName->setCurrentText(QStringLiteral("A1200 Install"));
        }
        if (configDescription) {
            configDescription->setText(QStringLiteral("A1200, 68020, AGA, 2 MB Chip RAM"));
        }
        if (configPath) {
            configPath->clear();
        }

        quickModel->setCurrentText(QStringLiteral("A1200"));
        refreshQuickstartConfigurationChoices(1);
        quickHostConfiguration->setCurrentText(QStringLiteral("Default"));
        compatibility->setValue(1);
        quickstartMode->setChecked(true);
        ntsc->setChecked(false);
        chipsetNtsc->setChecked(false);
        chipsetCycleExact->setChecked(false);
        chipsetCycleExactMemory->setChecked(false);
        immediateBlits->setChecked(false);
        waitingBlits->setChecked(false);
        displayOptimization->setCurrentText(QStringLiteral("Full"));
        chipsetSyncMode->setCurrentText(QStringLiteral("Combined + Blanking"));
        genlockConnected->setChecked(false);
        genlockMode->setCurrentIndex(0);
        genlockMix->setCurrentIndex(0);
        genlockAlpha->setChecked(false);
        genlockKeepAspect->setChecked(false);
        genlockImagePath.clear();
        genlockVideoPath.clear();
        updateGenlockControlState();
        keyboardMode->setCurrentText(QStringLiteral("UAE High level emulation"));
        keyboardNkro->setChecked(true);
        if (QAbstractButton *button = collisionButtons->button(3)) {
            button->setChecked(true);
        }

        applyQuickstartSelectionToUi();
        moreCompatible->setChecked(false);
        cpuDataCache->setChecked(false);
        cpuUnimplemented->setChecked(true);
        if (QAbstractButton *button = mmuButtons->button(0)) {
            button->setChecked(true);
        }
        fpuStrict->setChecked(false);
        fpuUnimplemented->setChecked(true);
        fpuMode->setCurrentText(QStringLiteral("Host (64-bit)"));
        if (QAbstractButton *button = cpuSpeedButtons->button(0)) {
            button->setChecked(true);
        }
        cpuSpeed->setValue(0);
        updateCpuSpeedLabel();
        cpuFrequency->setCurrentText(QStringLiteral("4x (A1200)"));
        cpuFrequencyCustom->clear();
        jit->setChecked(false);
        jitCache->setValue(jitCachePositionFromSize(defaultJitCacheSize()));
        jitFpu->setChecked(false);
        jitConstJump->setChecked(true);
        jitHardFlush->setChecked(false);
        if (QAbstractButton *button = jitTrust->button(0)) {
            button->setChecked(true);
        }
        jitNoFlags->setChecked(false);
        jitCatchFault->setChecked(true);
        updateCpuControlState();
        chipMem->setCurrentText(QStringLiteral("2 MB"));
        z2Fast->setCurrentText(QStringLiteral("None"));
        slowMem->setCurrentText(QStringLiteral("None"));
        z3Fast->setCurrentText(QStringLiteral("None"));
        z3ChipMem->setCurrentText(QStringLiteral("None"));
        processorSlotMem->setCurrentText(QStringLiteral("None"));
        rtgMem->setCurrentText(QStringLiteral("None"));
        rtgType->setCurrentText(QStringLiteral("ZorroIII"));
        rtgMonitor->setCurrentText(QStringLiteral("1"));
        rtgScale->setChecked(true);
        rtgCenter->setChecked(false);
        rtgIntegerScale->setChecked(false);
        rtgMultithread->setChecked(false);
        rtgHardwareSprite->setChecked(true);
        rtgHardwareVBlank->setChecked(false);
        rtgAutoswitch->setChecked(true);
        rtgInitialMonitor->setChecked(false);
        rtg8Bit->setCurrentText(QStringLiteral("8-bit (*)"));
        rtg16Bit->setCurrentText(QStringLiteral("R5G6B5PC (*)"));
        rtg24Bit->setCurrentText(QStringLiteral("(24bit)"));
        rtg32Bit->setCurrentText(QStringLiteral("B8G8R8A8 (*)"));
        rtgRefreshRate->setCurrentText(QStringLiteral("Chipset"));
        rtgBuffers->setCurrentText(QStringLiteral("Double"));
        hardwareCustomBoardOrder->setChecked(false);
        refreshHardwareInfoPage();
        rtgScaleAllow->setChecked(false);
        rtgDisplay->setCurrentText(QStringLiteral("Default display"));
        rtgAspectRatio->setCurrentText(QStringLiteral("Automatic"));
        expansionBsdsocket->setChecked(false);
        expansionScsiDevice->setChecked(false);
        expansionSana2->setChecked(false);
        clearExpansionBoardStates();

        romFile->setCurrentText(envString("WINUAE_KICKSTART_ROM"));
        extendedRomFile->setCurrentText(QString());
        cartFile->setCurrentText(QString());
        flashFile->clear();
        rtcFile->clear();
        mapRom->setChecked(false);
        kickShifter->setChecked(false);
        uaeBoardType->setCurrentText(QStringLiteral("Original UAE (FS + F0 ROM)"));
        clearCustomRomBoards();

        for (int i = 0; i < 4; i++) {
            dfEnable[i]->setChecked(i == 0);
            dfType[i]->setCurrentText(QStringLiteral("3.5 DD"));
            dfWriteProtect[i]->setChecked(false);
            dfPath[i]->setCurrentText(i == 0 ? envString("WINUAE_FLOPPY0") : QString());
        }
        floppySpeed->setValue(floppySpeedSliderPosition(100));
        updateFloppySpeedLabel();
        for (int i = 0; i < 2; i++) {
            quickDfEnable[i]->setChecked(i == 0);
            quickDfType[i]->setCurrentText(QStringLiteral("3.5 DD"));
            quickDfWriteProtect[i]->setChecked(false);
            quickDfPath[i]->setCurrentText(i == 0 ? envString("WINUAE_FLOPPY0") : QString());
        }
        for (int i = 0; i < MaxDiskSwapperSlots; i++) {
            setDiskSwapperPath(i, QString());
        }
        if (diskSwapperPath) {
            diskSwapperPath->clearEditText();
        }
        if (mountedDrives) {
            mountedDrives->clear();
            updateMountButtons();
        }
        clearCdSlots();
        hostDriveAutomount->setChecked(false);
        hostRemovableDrives->setChecked(false);
        hostNetworkDrives->setChecked(false);
        hostCdDrives->setChecked(false);
        filesysNoFsdb->setChecked(false);
        hostNoRecycleBin->setChecked(false);
        hostAutomountRemovable->setChecked(false);
        filesysLimitSize->setChecked(false);

        windowWidth->setText(QStringLiteral("720"));
        windowHeight->setText(QStringLiteral("568"));
        windowResize->setChecked(true);
        hostDisplay->setCurrentIndex(0);
        fullscreenResolution->setProperty("winuae_width", QString());
        fullscreenResolution->setProperty("winuae_height", QString());
        fullscreenResolution->setCurrentText(QStringLiteral("Native"));
        displayRefreshRate->setCurrentText(QStringLiteral("Default"));
        displayBufferCount->setCurrentText(QStringLiteral("Double"));
        nativeMode->setCurrentText(QStringLiteral("Windowed"));
        nativeVsync->setCurrentText(QStringLiteral("No vsync"));
        nativeFrameSlices->setCurrentText(QStringLiteral("4"));
        rtgMode->setCurrentText(QStringLiteral("Windowed"));
        rtgVsync->setCurrentText(QStringLiteral("-"));
        displayResolution->setCurrentText(QStringLiteral("hires"));
        displayOverscan->setCurrentText(QStringLiteral("Overscan"));
        displayAutoResolution->setCurrentText(QStringLiteral("Disabled"));
        displayFrameRate->setValue(1);
        displayCenterHorizontal->setChecked(false);
        displayCenterVertical->setChecked(false);
        displayFlickerFixer->setChecked(false);
        displayLoresSmoothed->setChecked(false);
        displayBlackerThanBlack->setChecked(false);
        displayMonochrome->setChecked(false);
        displayAutoResolutionVga->setChecked(true);
        displayResyncBlank->setChecked(false);
        displayKeepAspect->setChecked(false);
        if (QAbstractButton *button = displayLineModeButtons->button(1)) {
            button->setChecked(true);
        }
        if (QAbstractButton *button = displayInterlacedLineModeButtons->button(1)) {
            button->setChecked(true);
        }
        resetFilterStates();
        outputFile->setText(unixDefaultDataSubPath(QStringLiteral("Videos/output.avi")));
        outputAudio->setChecked(false);
        outputVideo->setChecked(true);
        outputEnabled->setChecked(false);
        outputAudioCodec->setCurrentIndex(0);
        outputVideoCodec->setCurrentIndex(0);
        outputFrameLimiter->setChecked(false);
        outputOriginalSize->setChecked(false);
        outputNoSound->setChecked(false);
        outputNoSoundSync->setChecked(false);
        screenshotOriginalSize->setChecked(false);
        screenshotPaletted->setChecked(false);
        screenshotClip->setChecked(false);
        screenshotClip->setEnabled(false);
        screenshotAuto->setChecked(false);
        stateReplayAutoplay->setChecked(true);
        stateReplayRate->setCurrentText(QStringLiteral("5"));
        stateReplayBuffers->setCurrentText(QStringLiteral("100"));
        if (QAbstractButton *button = soundOutputButtons->button(2)) {
            button->setChecked(true);
        }
        soundDevice->setCurrentIndex(0);
        soundAutomatic->setChecked(false);
        soundMasterVolume->setValue(100);
        for (int i = 0; i < SoundVolumeCount; i++) {
            soundVolumeAttenuation[i] = 0;
        }
        currentSoundVolume = 0;
        soundVolumeSelect->setCurrentIndex(0);
        loadSelectedSoundVolume();
        soundBufferSize->setValue(soundBufferIndexFromSize(16384));
        soundChannels->setCurrentText(QStringLiteral("Stereo"));
        soundStereoSeparation->setCurrentText(QStringLiteral("70%"));
        soundInterpolation->setCurrentText(QStringLiteral("Anti"));
        soundFrequency->setCurrentText(QStringLiteral("44100"));
        soundSwap->setCurrentText(QStringLiteral("-"));
        soundStereoDelay->setCurrentText(QStringLiteral("-"));
        soundFilter->setCurrentText(QStringLiteral("Emulated (A500)"));
        for (int i = 0; i < FloppySoundDriveCount; i++) {
            floppySoundTypeValue[i] = 0;
            floppySoundEmptyAttenuation[i] = 33;
            floppySoundDiskAttenuation[i] = 33;
        }
        currentFloppySoundDrive = 0;
        floppySoundDrive->setCurrentIndex(0);
        loadSelectedFloppySound();
        updateSoundControlState();
        cdSpeedTurbo->setChecked(false);
        portDevice[0]->setCurrentText(QStringLiteral("Mouse"));
        portDevice[1]->setCurrentText(QStringLiteral("Keyboard Layout A"));
        portDevice[2]->setCurrentText(QStringLiteral("<None>"));
        portDevice[3]->setCurrentText(QStringLiteral("<None>"));
        for (QString &custom : joyportCustom) {
            custom.clear();
        }
        for (int i = 0; i < 2; i++) {
            portAutofire[i]->setCurrentText(QStringLiteral("No autofire (normal)"));
            portMode[i]->setCurrentText(QStringLiteral("Default"));
        }
        portAutoswitch->setChecked(true);
        mouseSpeed->setValue(100);
        inputType->setCurrentText(QStringLiteral("Game Ports"));
        inputDevice->setCurrentText(QStringLiteral("Keyboard"));
        inputMappingSettings.clear();
        inputOwnedMappingKeys.clear();
        inputDeviceEnabled->setChecked(true);
        inputSubEvent->setCurrentText(QStringLiteral("1"));
        inputAmigaEvent->setCurrentText(QStringLiteral("<None>"));
        inputDeadzone->setValue(33);
        inputAutofireRate->setValue(600);
        inputJoyMouseDigital->setValue(10);
        inputJoyMouseAnalog->setValue(100);
        inputCopyFrom->setCurrentText(QStringLiteral("Custom #1"));
        inputPageUpEnd->setChecked(false);
        inputSwapBackslashF11->setCheckState(Qt::Unchecked);
        refreshInputMappingList();
        mouseUntrapMode->setCurrentText(QStringLiteral("Middle button"));
        magicMouseCursor->setCurrentText(QStringLiteral("Show both cursors"));
        virtualMouseDriver->setChecked(false);
        tabletMode->setCurrentText(QStringLiteral("-"));
        tabletLibrary->setChecked(false);
        updateMouseExtraState();
        printerPort->setCurrentText(QStringLiteral("<None>"));
        printerType->setCurrentText(QStringLiteral("Passthrough"));
        printerAutoFlush->setValue(5);
        ghostscriptParams->clear();
        samplerDevice->setCurrentText(QStringLiteral("<None>"));
        samplerStereo->setChecked(false);
        serialPort->setCurrentText(QStringLiteral("<None>"));
        serialShared->setChecked(false);
        serialCtsRts->setChecked(true);
        serialDirect->setChecked(false);
        serialCrlf->setChecked(false);
        uaeSerial->setChecked(false);
        serialStatus->setChecked(true);
        serialRingIndicator->setChecked(false);
        midiOut->setCurrentText(QStringLiteral("<None>"));
        midiIn->setCurrentText(QStringLiteral("<None>"));
        midiRouter->setChecked(false);
        protectionDongle->setCurrentText(QStringLiteral("None"));
        updateIoPortsState();
        for (const MiscCheckChoice &choice : miscCheckChoices) {
            setMiscOptionChecked(QString::fromLatin1(choice.key), choice.defaultChecked);
        }
        stateFileName->setCurrentText(QString());
        stateFileClear->setChecked(false);
        for (int i = 0; i < 3; i++) {
            keyboardLed[i]->setCurrentText(QStringLiteral("None"));
        }
        setExtensionPriorityValue(extensionActivePriority, 0);
        extensionActivePause->setChecked(false);
        extensionActiveNoSound->setChecked(false);
        extensionActiveNoJoy->setChecked(false);
        extensionActiveNoKeyboard->setChecked(false);
        setExtensionPriorityValue(extensionInactivePriority, -1);
        extensionInactivePause->setChecked(false);
        extensionInactiveNoSound->setChecked(false);
        extensionInactiveNoJoy->setChecked(true);
        setExtensionPriorityValue(extensionMinimizedPriority, -2);
        extensionMinimizedPause->setChecked(true);
        extensionMinimizedNoSound->setChecked(true);
        extensionMinimizedNoJoy->setChecked(true);
        updateExtensionActivityState();
        romsPath->setText(unixDefaultDataSubPath(QStringLiteral("Kickstarts")));
        configsPath->setText(unixDefaultDataSubPath(QStringLiteral("Configuration")));
        nvramPath->setText(unixDefaultDataSubPath(QStringLiteral("NVRAMs")));
        screenshotsPath->setText(unixDefaultDataSubPath(QStringLiteral("Screenshots")));
        stateFilesPath->setText(unixDefaultDataSubPath(QStringLiteral("Save States")));
        videosPath->setText(unixDefaultDataSubPath(QStringLiteral("Videos")));
        saveImagesPath->setText(unixDefaultDataSubPath(QStringLiteral("SaveImages")));
        ripsPath->setText(unixDefaultDataSubPath(QStringLiteral("Rips")));
        dataPath->setText(unixDefaultDataPath());
        recursiveRoms->setChecked(false);
        cacheConfigurations->setChecked(true);
        cacheBoxArt->setChecked(false);
        saveImageOriginalPath->setChecked(false);
        relativePaths->setChecked(false);
        portableMode->setChecked(false);
        pathDefaultType->setCurrentText(QStringLiteral("User data directory"));
        /* Paths and path flags are host settings: restore the persisted
         * values instead of the factory defaults. */
        loadHostSettings();
        logSelect->setCurrentText(QStringLiteral("winuaebootlog.txt"));
        fullLogging->setChecked(false);
        logWindow->setChecked(false);
        miscGuiSize->setCurrentText(QStringLiteral("Select..."));
        miscGuiResize->setChecked(true);
        miscGuiFullscreen->setChecked(false);
        miscGuiDarkMode->setCheckState(Qt::PartiallyChecked);
        applyGuiDarkModeSelection();
        miscGuiFontConfig.clear();
        applyGuiResizeMode();
        updateLogPathText();
        updateOutputControlState();
        refreshConfigList();
        status->setText(QStringLiteral("Ready"));
    }

    struct HostPathBinding {
        const char *key;
        QLineEdit *edit;
    };

    QVector<HostPathBinding> hostPathBindings() const
    {
        return {
            { "KickstartPath", romsPath },
            { "ConfigurationPath", configsPath },
            { "NVRAMPath", nvramPath },
            { "ScreenshotPath", screenshotsPath },
            { "StatefilePath", stateFilesPath },
            { "VideoPath", videosPath },
            { "SaveimagePath", saveImagesPath },
            { "RipperPath", ripsPath },
            { "DataPath", dataPath },
        };
    }

    struct HostFlagBinding {
        const char *key;
        QCheckBox *box;
    };

    QVector<HostFlagBinding> hostFlagBindings() const
    {
        return {
            { "RecursiveROMScan", recursiveRoms },
            { "ConfigurationCache", cacheConfigurations },
            { "ArtCache", cacheBoxArt },
            { "SaveImageOriginalPath", saveImageOriginalPath },
        };
    }

    void loadHostSettings()
    {
        if (!hardwareProvider.hostSettingGet) {
            return;
        }
        char value[4096];
        for (const HostPathBinding &binding : hostPathBindings()) {
            if (binding.edit && hardwareProvider.hostSettingGet(hardwareProvider.context, binding.key, value, sizeof value)) {
                binding.edit->setText(QString::fromLocal8Bit(value));
            }
        }
        for (const HostFlagBinding &binding : hostFlagBindings()) {
            if (binding.box && hardwareProvider.hostSettingGet(hardwareProvider.context, binding.key, value, sizeof value)) {
                binding.box->setChecked(QString::fromLocal8Bit(value).toInt() != 0);
            }
        }
    }

    void saveHostSettings()
    {
        if (!hardwareProvider.hostSettingSet) {
            return;
        }
        for (const HostPathBinding &binding : hostPathBindings()) {
            if (binding.edit) {
                hardwareProvider.hostSettingSet(hardwareProvider.context, binding.key, binding.edit->text().toLocal8Bit().constData());
            }
        }
        for (const HostFlagBinding &binding : hostFlagBindings()) {
            if (binding.box) {
                hardwareProvider.hostSettingSet(hardwareProvider.context, binding.key, binding.box->isChecked() ? "1" : "0");
            }
        }
        if (hardwareProvider.hostSettingsFlush) {
            hardwareProvider.hostSettingsFlush(hardwareProvider.context);
        }
    }

    void done(int result) override
    {
        saveHostSettings();
        QDialog::done(result);
    }

    void applyModelPreset(const QString &model)
    {
        if (model == QStringLiteral("A1200") || model == QStringLiteral("CD32")) {
            chipset->setCurrentText(QStringLiteral("AGA"));
            chipsetCompatible->setCurrentText(model);
            setCpuButton(68020);
            setFpuButton(0);
            cpu24Bit->setChecked(false);
            chipMem->setCurrentText(QStringLiteral("2 MB"));
        } else if (model == QStringLiteral("A4000")) {
            chipset->setCurrentText(QStringLiteral("AGA"));
            chipsetCompatible->setCurrentText(QStringLiteral("A4000"));
            setCpuButton(68030);
            setFpuButton(68882);
            cpu24Bit->setChecked(false);
            chipMem->setCurrentText(QStringLiteral("2 MB"));
        } else if (model == QStringLiteral("A3000")) {
            chipset->setCurrentText(QStringLiteral("ECS"));
            chipsetCompatible->setCurrentText(QStringLiteral("A3000"));
            setCpuButton(68030);
            setFpuButton(68882);
            cpu24Bit->setChecked(false);
            chipMem->setCurrentText(QStringLiteral("2 MB"));
        } else if (model == QStringLiteral("A600")) {
            chipset->setCurrentText(QStringLiteral("ECS"));
            chipsetCompatible->setCurrentText(QStringLiteral("A600"));
            setCpuButton(68000);
            setFpuButton(0);
            cpu24Bit->setChecked(true);
            chipMem->setCurrentText(QStringLiteral("1 MB"));
        } else if (model == QStringLiteral("A500+")) {
            chipset->setCurrentText(QStringLiteral("ECS"));
            chipsetCompatible->setCurrentText(QStringLiteral("A500+"));
            setCpuButton(68000);
            setFpuButton(0);
            cpu24Bit->setChecked(true);
            chipMem->setCurrentText(QStringLiteral("1 MB"));
        } else if (model == QStringLiteral("A1000") || model == QStringLiteral("Velvet")) {
            chipset->setCurrentText(QStringLiteral("OCS"));
            chipsetCompatible->setCurrentText(model);
            setCpuButton(68000);
            setFpuButton(0);
            cpu24Bit->setChecked(true);
            chipMem->setCurrentText(QStringLiteral("512 KB"));
        } else if (model == QStringLiteral("CDTV") || model == QStringLiteral("CDTV-CR")) {
            chipset->setCurrentText(QStringLiteral("ECS"));
            chipsetCompatible->setCurrentText(model);
            setCpuButton(68000);
            setFpuButton(0);
            cpu24Bit->setChecked(true);
            chipMem->setCurrentText(QStringLiteral("1 MB"));
        } else {
            chipset->setCurrentText(QStringLiteral("OCS"));
            chipsetCompatible->setCurrentText(QStringLiteral("A500"));
            setCpuButton(68000);
            setFpuButton(0);
            cpu24Bit->setChecked(true);
            chipMem->setCurrentText(QStringLiteral("512 KB"));
        }
    }

    void setCpuButton(int model)
    {
        if (QAbstractButton *button = cpuButtons->button(model)) {
            button->setChecked(true);
        }
        updateFpuControls();
        updateCpuControlState();
    }

    void setFpuButton(int model)
    {
        const int id = (model == 68040 || model == 68060) ? FpuInternal : model;
        if (QAbstractButton *button = fpuButtons->button(id)) {
            button->setChecked(true);
        }
        updateFpuControls();
        updateCpuControlState();
    }

    int selectedCpuModel() const
    {
        const int cpu = cpuButtons->checkedId();
        return cpu > 0 ? cpu : 68020;
    }

    int fpuModelConfigValue(int cpu) const
    {
        const int fpu = fpuButtons->checkedId();
        if (fpu == FpuInternal) {
            return cpu >= 68060 ? 68060 : (cpu >= 68040 ? 68040 : 68882);
        }
        return fpu > 0 ? fpu : 0;
    }

    void updateFpuControls()
    {
        const int cpu = selectedCpuModel();
        if (QAbstractButton *internal = fpuButtons->button(FpuInternal)) {
            internal->setEnabled(cpu >= 68040);
            if (!internal->isEnabled() && internal->isChecked()) {
                if (QAbstractButton *none = fpuButtons->button(0)) {
                    none->setChecked(true);
                }
            }
        }
    }

    int chipMemConfigValue() const
    {
        const QString text = chipMem->currentText();
        if (text == QStringLiteral("512 KB")) {
            return 1;
        }
        if (text == QStringLiteral("1 MB")) {
            return 2;
        }
        if (text == QStringLiteral("2 MB")) {
            return 4;
        }
        if (text == QStringLiteral("4 MB")) {
            return 8;
        }
        if (text == QStringLiteral("8 MB")) {
            return 16;
        }
        return 4;
    }

    int megabytesFromText(const QString &text) const
    {
        QString value = text;
        value.remove(QStringLiteral(" MB"));
        return value.toInt();
    }

    int slowMemConfigValue() const
    {
        const QString text = slowMem->currentText();
        if (text == QStringLiteral("512 KB")) {
            return 2;
        }
        if (text == QStringLiteral("1 MB")) {
            return 4;
        }
        if (text == QStringLiteral("1.5 MB")) {
            return 6;
        }
        if (text == QStringLiteral("1.8 MB")) {
            return 7;
        }
        return 0;
    }

    QString nextMountDeviceName() const
    {
        QStringList used;
        if (mountedDrives) {
            for (int i = 0; i < mountedDrives->topLevelItemCount(); i++) {
                const QTreeWidgetItem *item = mountedDrives->topLevelItem(i);
                const QString kind = item->data(0, MountKindRole).toString();
                if (kind == QStringLiteral("dir") || kind == QStringLiteral("hdf")) {
                    used.append(item->data(0, MountDeviceRole).toString().toUpper());
                }
            }
        }
        for (int i = 0; i < MaxMountEntries; i++) {
            const QString device = QStringLiteral("DH%1").arg(i);
            if (!used.contains(device)) {
                return device;
            }
        }
        return QStringLiteral("DH0");
    }

    void addDirectoryMountDialog()
    {
        WinUaeQtMountEntry entry;
        entry.kind = QStringLiteral("dir");
        entry.device = nextMountDeviceName();
        if (showDirectoryMountDialog(&entry, QStringLiteral("Add Directory or Archive"))) {
            addMountEntry(entry);
        }
    }

    void addHardfileMountDialog()
    {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Add hardfile"), fileDialogInitialPath(QString()), hardfileImageFilter());
        if (path.isEmpty()) {
            return;
        }

        WinUaeQtMountEntry entry;
        entry.kind = QStringLiteral("hdf");
        entry.device = nextMountDeviceName();
        entry.path = path;
        entry.hardfileGeometry = QStringLiteral("32,1,2,512");
        entry.hardfileTail = QStringLiteral(",uae0");
        entry.readOnly = false;
        entry.bootPri = 0;
        if (showHardfileMountDialog(&entry, QStringLiteral("Hardfile Properties"))) {
            addMountEntry(entry);
        }
    }

    int nextMountEmuUnit(const QString &kind) const
    {
        QList<int> used;
        if (mountedDrives) {
            for (int i = 0; i < mountedDrives->topLevelItemCount(); i++) {
                const QTreeWidgetItem *item = mountedDrives->topLevelItem(i);
                if (item->data(0, MountKindRole).toString() == kind) {
                    used.append(item->data(0, MountEmuUnitRole).toInt());
                }
            }
        }
        for (int i = 0; i < MaxControllerUnits; i++) {
            if (!used.contains(i)) {
                return i;
            }
        }
        return 0;
    }

    void addHardDriveMountDialog()
    {
        const QVector<WinUaeQtNativeDriveChoice> choices = nativeHardDriveChoices();
        if (choices.isEmpty()) {
            QMessageBox::information(
                this,
                QStringLiteral("Add Hard Drive"),
                QStringLiteral("No readable Unix block devices were found. On macOS and Linux, raw device access may require adjusted permissions."));
            return;
        }

        QDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Add Hard Drive"));
        QComboBox *drive = new QComboBox;
        for (const WinUaeQtNativeDriveChoice &choice : choices) {
            drive->addItem(choice.display, choice.configPath);
        }
        QLabel *notice = new QLabel(QStringLiteral("Native drives are opened read-only by the Unix backend."));
        notice->setWordWrap(true);
        QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        QVBoxLayout *root = new QVBoxLayout(&dialog);
        QGridLayout *fields = new QGridLayout;
        fields->setColumnStretch(1, 1);
        fields->addWidget(label(QStringLiteral("Drive:")), 0, 0);
        fields->addWidget(drive, 0, 1);
        root->addLayout(fields);
        root->addWidget(notice);
        root->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }

        const int index = drive->currentIndex();
        if (index < 0 || index >= choices.size()) {
            return;
        }
        const WinUaeQtNativeDriveChoice &choice = choices[index];
        WinUaeQtMountEntry entry;
        entry.kind = QStringLiteral("hdf");
        entry.path = choice.configPath;
        entry.device.clear();
        entry.bootPri = 0;
        entry.readOnly = true;
        entry.hardfileGeometry = QStringLiteral("0,0,0,%1").arg(qMax(1, choice.blockSize));
        entry.hardfileTail = QStringLiteral(",uae0");
        if (showHardfileMountDialog(&entry, QStringLiteral("Hard Drive Properties"))) {
            addMountEntry(entry);
        }
    }

    void addCdDriveMountDialog()
    {
        WinUaeQtMountEntry entry;
        entry.kind = QStringLiteral("cd");
        entry.emuUnit = nextMountEmuUnit(QStringLiteral("cd"));
        entry.readOnly = true;
        entry.bootPri = 0;
        entry.hardfileGeometry = QStringLiteral("0,0,0,2048");
        entry.hardfileTail = QStringLiteral(",ide0");
        if (showCdDriveMountDialog(&entry, QStringLiteral("Add CD Drive"))) {
            addMountEntry(entry);
        }
    }

    void addTapeDriveMountDialog()
    {
        WinUaeQtMountEntry entry;
        entry.kind = QStringLiteral("tape");
        entry.emuUnit = nextMountEmuUnit(QStringLiteral("tape"));
        entry.readOnly = false;
        entry.bootPri = 0;
        entry.hardfileGeometry = QStringLiteral("0,0,0,512");
        entry.hardfileTail = QStringLiteral(",uae0");
        if (showTapeDriveMountDialog(&entry, QStringLiteral("Add Tape Drive"))) {
            addMountEntry(entry);
        }
    }

    void addMountEntry(const WinUaeQtMountEntry &entry)
    {
        if (!mountedDrives || mountedDrives->topLevelItemCount() >= MaxMountEntries) {
            return;
        }
        QTreeWidgetItem *item = new QTreeWidgetItem(mountedDrives);
        updateMountItem(item, entry);
        updateMountButtons();
    }

    QString mountEntryConfigKey(const WinUaeQtMountEntry &entry, int index = 0) const
    {
        if (entry.kind == QStringLiteral("dir")) {
            return QStringLiteral("filesystem2");
        }
        if (entry.kind == QStringLiteral("hdf")) {
            return QStringLiteral("hardfile2");
        }
        if (entry.kind == QStringLiteral("cd") || entry.kind == QStringLiteral("tape")) {
            return QStringLiteral("uaehf%1").arg(index);
        }
        return QString();
    }

    QString mountEntryConfigValue(const WinUaeQtMountEntry &entry) const
    {
        if (entry.kind == QStringLiteral("dir")) {
            return serializeWinUaeQtFilesystem2MountValue(entry);
        }
        if (entry.kind == QStringLiteral("hdf")) {
            return serializeWinUaeQtHardfile2MountValue(entry);
        }
        if (entry.kind == QStringLiteral("cd")) {
            return serializeWinUaeQtUaehfCdMountValue(entry);
        }
        if (entry.kind == QStringLiteral("tape")) {
            return serializeWinUaeQtUaehfTapeMountValue(entry);
        }
        return QString();
    }

    bool mountEntryExists(const WinUaeQtMountEntry &entry) const
    {
        if (!mountedDrives) {
            return false;
        }
        const QString key = mountEntryConfigKey(entry);
        const QString value = mountEntryConfigValue(entry);
        if (key.isEmpty() || value.isEmpty()) {
            return false;
        }
        for (int i = 0; i < mountedDrives->topLevelItemCount(); i++) {
            const WinUaeQtMountEntry existing = mountEntryFromItem(mountedDrives->topLevelItem(i));
            if (mountEntryConfigKey(existing) == key && mountEntryConfigValue(existing) == value) {
                return true;
            }
        }
        return false;
    }

    void addMountEntryIfUnique(const WinUaeQtMountEntry &entry)
    {
        if (!mountEntryExists(entry)) {
            addMountEntry(entry);
        }
    }

    void updateMountItem(QTreeWidgetItem *item, const WinUaeQtMountEntry &entry)
    {
        if (!mountedDrives || !item) {
            return;
        }
        item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
        WinUaeQtMountEntry normalized = entry;
        if (normalized.kind == QStringLiteral("dir")) {
            normalized.device = winUaeQtSanitizedAmigaName(normalized.device, nextMountDeviceName(), true);
            normalized.volume = winUaeQtSanitizedAmigaName(normalized.volume, winUaeQtDefaultVolumeName(normalized.path), false);
        } else if (normalized.kind == QStringLiteral("hdf")) {
            normalized.device = winUaeQtSanitizedAmigaName(normalized.device, nextMountDeviceName(), true);
        } else if (normalized.kind == QStringLiteral("cd")) {
            normalized.device.clear();
            normalized.volume = QStringLiteral("CD");
            normalized.readOnly = true;
            normalized.bootPri = 0;
            if (normalized.hardfileGeometry.isEmpty()) {
                normalized.hardfileGeometry = QStringLiteral("0,0,0,2048");
            }
        } else if (normalized.kind == QStringLiteral("tape")) {
            normalized.device.clear();
            normalized.volume = QStringLiteral("TAPE");
            normalized.bootPri = 0;
            if (normalized.hardfileGeometry.isEmpty()) {
                normalized.hardfileGeometry = QStringLiteral("0,0,0,512");
            }
        }

        QString deviceText = normalized.device;
        QString volumeText = normalized.kind == QStringLiteral("dir") ? normalized.volume : QStringLiteral("n/a");
        QString blockSizeText = QStringLiteral("n/a");
        QString bootPriText = QString::number(normalized.bootPri);
        if (normalized.kind == QStringLiteral("cd")) {
            deviceText = mountControllerDisplay(boardCatalog, normalized);
            volumeText = QStringLiteral("CD");
            blockSizeText = QStringLiteral("2048");
            bootPriText = QStringLiteral("n/a");
        } else if (normalized.kind == QStringLiteral("tape")) {
            deviceText = mountControllerDisplay(boardCatalog, normalized);
            volumeText = QStringLiteral("TAPE");
            blockSizeText = QStringLiteral("512");
            bootPriText = QStringLiteral("n/a");
        } else if (normalized.kind == QStringLiteral("hdf")) {
            const QStringList geometry = normalized.hardfileGeometry.split(QLatin1Char(','));
            blockSizeText = geometry.value(3, QStringLiteral("512"));
        }

        item->setText(0, QString());
        item->setText(1, deviceText);
        item->setText(2, volumeText);
        item->setText(3, normalized.path.isEmpty() ? QStringLiteral("-") : normalized.path);
        item->setText(4, normalized.readOnly ? QStringLiteral("No") : QStringLiteral("Yes"));
        item->setText(5, blockSizeText);
        item->setText(6, QStringLiteral("n/a"));
        item->setText(7, bootPriText);
        item->setData(0, MountKindRole, entry.kind);
        item->setData(0, MountDeviceRole, normalized.device);
        item->setData(0, MountVolumeRole, normalized.volume);
        item->setData(0, MountPathRole, normalized.path);
        item->setData(0, MountReadOnlyRole, normalized.readOnly);
        item->setData(0, MountBootPriRole, normalized.bootPri);
        item->setData(0, MountEmuUnitRole, normalized.emuUnit);
        item->setData(0, MountRawConfigRole, normalized.rawConfig);
        item->setData(0, MountHardfileGeometryRole, normalized.hardfileGeometry);
        item->setData(0, MountHardfileTailRole, normalized.hardfileTail);
        for (int i = 0; i < mountedDrives->columnCount(); i++) {
            mountedDrives->resizeColumnToContents(i);
        }
    }

    void removeSelectedMount()
    {
        if (!mountedDrives) {
            return;
        }
        qDeleteAll(mountedDrives->selectedItems());
        updateMountButtons();
    }

    void moveSelectedMount(int delta)
    {
        if (!mountedDrives || delta == 0) {
            return;
        }
        QTreeWidgetItem *item = mountedDrives->currentItem();
        if (!item) {
            return;
        }
        const int from = mountedDrives->indexOfTopLevelItem(item);
        if (from < 0) {
            return;
        }
        const int to = qBound(0, from + delta, mountedDrives->topLevelItemCount() - 1);
        if (from == to) {
            return;
        }
        item = mountedDrives->takeTopLevelItem(from);
        mountedDrives->insertTopLevelItem(to, item);
        mountedDrives->setCurrentItem(item);
        item->setSelected(true);
        updateMountButtons();
    }

    void updateMountButtons()
    {
        if (!mountedDrives) {
            return;
        }
        const bool canAdd = mountedDrives->topLevelItemCount() < MaxMountEntries;
        if (addDirectoryMountButton) {
            addDirectoryMountButton->setEnabled(canAdd);
        }
        if (addHardfileMountButton) {
            addHardfileMountButton->setEnabled(canAdd);
        }
        if (addHardDriveMountButton) {
            addHardDriveMountButton->setEnabled(canAdd);
        }
        if (addCdMountButton) {
            addCdMountButton->setEnabled(canAdd);
        }
        if (addTapeMountButton) {
            addTapeMountButton->setEnabled(canAdd);
        }
        if (propertiesMountButton) {
            propertiesMountButton->setEnabled(!mountedDrives->selectedItems().isEmpty());
        }
        if (removeMountButton) {
            removeMountButton->setEnabled(!mountedDrives->selectedItems().isEmpty());
        }
    }

    WinUaeQtMountEntry mountEntryFromItem(const QTreeWidgetItem *item) const
    {
        WinUaeQtMountEntry entry;
        entry.kind = item->data(0, MountKindRole).toString();
        entry.device = item->data(0, MountDeviceRole).toString();
        entry.volume = item->data(0, MountVolumeRole).toString();
        entry.path = item->data(0, MountPathRole).toString();
        entry.readOnly = item->data(0, MountReadOnlyRole).toBool();
        entry.bootPri = item->data(0, MountBootPriRole).toInt();
        entry.emuUnit = item->data(0, MountEmuUnitRole).toInt();
        entry.rawConfig = item->data(0, MountRawConfigRole).toString();
        entry.hardfileGeometry = item->data(0, MountHardfileGeometryRole).toString();
        entry.hardfileTail = item->data(0, MountHardfileTailRole).toString();
        return entry;
    }

    void openSelectedMountProperties()
    {
        if (!mountedDrives) {
            return;
        }
        QTreeWidgetItem *item = mountedDrives->currentItem();
        if (!item) {
            return;
        }

        WinUaeQtMountEntry entry = mountEntryFromItem(item);
        bool accepted = false;
        if (entry.kind == QStringLiteral("dir")) {
            accepted = showDirectoryMountDialog(&entry, QStringLiteral("Directory Properties"));
        } else if (entry.kind == QStringLiteral("hdf")) {
            accepted = showHardfileMountDialog(&entry, QStringLiteral("Hardfile Properties"));
        } else if (entry.kind == QStringLiteral("cd")) {
            accepted = showCdDriveMountDialog(&entry, QStringLiteral("CD Drive Properties"));
        } else if (entry.kind == QStringLiteral("tape")) {
            accepted = showTapeDriveMountDialog(&entry, QStringLiteral("Tape Drive Properties"));
        }
        if (accepted) {
            updateMountItem(item, entry);
        }
    }

    bool showDirectoryMountDialog(WinUaeQtMountEntry *entry, const QString &title)
    {
        if (!entry) {
            return false;
        }

        QDialog dialog(this);
        dialog.setWindowTitle(title);

        QLineEdit *path = new QLineEdit(entry->path);
        QLineEdit *device = new QLineEdit(entry->device.isEmpty() ? nextMountDeviceName() : entry->device);
        QLineEdit *volume = new QLineEdit(entry->volume);
        QSpinBox *bootPri = new QSpinBox;
        bootPri->setRange(-128, 127);
        bootPri->setValue(entry->bootPri);
        QCheckBox *readOnly = new QCheckBox(QStringLiteral("Read-only"));
        readOnly->setChecked(entry->readOnly);
        QPushButton *selectDirectory = new QPushButton(QStringLiteral("Directory..."));
        QPushButton *selectArchive = new QPushButton(QStringLiteral("Archive..."));
        QPushButton *eject = new QPushButton(QStringLiteral("Eject"));

        QGridLayout *fields = new QGridLayout;
        fields->setColumnStretch(1, 1);
        fields->addWidget(label(QStringLiteral("Path:")), 0, 0);
        fields->addWidget(path, 0, 1, 1, 3);
        fields->addWidget(selectDirectory, 1, 1);
        if (selectArchive) {
            fields->addWidget(selectArchive, 1, 2);
        }
        if (eject) {
            fields->addWidget(eject, 1, 3);
        }
        fields->addWidget(label(QStringLiteral("Device:")), 2, 0);
        fields->addWidget(device, 2, 1, 1, 3);
        fields->addWidget(label(QStringLiteral("Volume:")), 3, 0);
        fields->addWidget(volume, 3, 1, 1, 3);
        fields->addWidget(label(QStringLiteral("Boot priority:")), 4, 0);
        fields->addWidget(bootPri, 4, 1);
        fields->addWidget(readOnly, 5, 1, 1, 3);

        QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        QVBoxLayout *root = new QVBoxLayout(&dialog);
        root->addLayout(fields);
        root->addWidget(buttons);

        auto updateReadOnlyForPath = [this, path, readOnly]() {
            const QString text = path->text().trimmed();
            const bool archiveFile = !text.isEmpty() && QFileInfo(expandedPathText(text)).isFile();
            if (archiveFile) {
                readOnly->setChecked(true);
            }
            readOnly->setEnabled(!archiveFile);
        };

        connect(selectDirectory, &QPushButton::clicked, this, [this, path, volume, readOnly, updateReadOnlyForPath]() {
            const QString initialPath = fileDialogInitialDirectory(path->text());
            const QString selected = QFileDialog::getExistingDirectory(this, QStringLiteral("Select directory"), initialPath);
            if (!selected.isEmpty()) {
                path->setText(selected);
                if (volume->text().isEmpty()) {
                    volume->setText(winUaeQtDefaultVolumeName(selected));
                }
                readOnly->setChecked(false);
                updateReadOnlyForPath();
            }
        });
        if (selectArchive) {
            connect(selectArchive, &QPushButton::clicked, this, [this, path, volume, updateReadOnlyForPath]() {
                const QString initialPath = fileDialogInitialPath(path->text());
                const QString selected = QFileDialog::getOpenFileName(this, QStringLiteral("Select directory archive"), initialPath, directoryArchiveFilter());
                if (!selected.isEmpty()) {
                    path->setText(selected);
                    if (volume->text().isEmpty()) {
                        volume->setText(winUaeQtDefaultVolumeName(selected));
                    }
                    updateReadOnlyForPath();
                }
            });
        }
        if (eject) {
            connect(eject, &QPushButton::clicked, this, [path, volume, readOnly]() {
                path->clear();
                volume->clear();
                readOnly->setEnabled(true);
            });
        }
        connect(path, &QLineEdit::editingFinished, this, updateReadOnlyForPath);
        updateReadOnlyForPath();
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() != QDialog::Accepted || path->text().trimmed().isEmpty()) {
            return false;
        }

        entry->kind = QStringLiteral("dir");
        entry->device = winUaeQtSanitizedAmigaName(device->text(), nextMountDeviceName(), true);
        entry->volume = winUaeQtSanitizedAmigaName(volume->text(), winUaeQtDefaultVolumeName(path->text()), false);
        entry->path = path->text().trimmed();
        entry->bootPri = bootPri->value();
        entry->readOnly = readOnly->isChecked() || QFileInfo(expandedPathText(entry->path)).isFile();
        return true;
    }

    void appendActiveMountControllerChoices(QStringList *items, WinUaeQtMountControllerBus bus) const
    {
        if (!items) {
            return;
        }
        for (auto it = expansionBoardStates.constBegin(); it != expansionBoardStates.constEnd(); ++it) {
            if (!it.value().present) {
                continue;
            }
            int slot = 0;
            const QString boardKey = expansionBoardBaseKey(it.key(), &slot);
            const WinUaeQtExpansionBoardCatalogItem *board = expansionBoardChoiceByKey(boardCatalog, boardKey);
            if (!board) {
                continue;
            }
            const WinUaeQtMountControllerChoice choice = mountControllerChoiceForBoard(*board, bus);
            if (!choice.valid) {
                continue;
            }
            const QString display = choice.display + mountControllerDuplicateDisplaySuffix(slot);
            if (!items->contains(display)) {
                items->append(display);
            }
        }
    }

    QStringList mountControllerItemsForDialog(bool includeUae, bool includeIde, bool includeScsi, const WinUaeQtMountEntry &entry, const QString &fallback) const
    {
        QStringList items;
        if (includeUae) {
            items.append(QStringLiteral("UAE (uaehf.device)"));
        }
        if (includeIde) {
            items.append(QStringLiteral("IDE (Auto)"));
            appendActiveMountControllerChoices(&items, MountControllerBusIde);
        }
        if (includeScsi) {
            items.append(QStringLiteral("SCSI (Auto)"));
            appendActiveMountControllerChoices(&items, MountControllerBusScsi);
        }

        const QString current = mountControllerFamily(boardCatalog, entry, fallback);
        if (!current.isEmpty() && !items.contains(current)) {
            items.append(current);
        }
        return items;
    }

    bool showHardfileMountDialog(WinUaeQtMountEntry *entry, const QString &title)
    {
        if (!entry) {
            return false;
        }

        QDialog dialog(this);
        dialog.setWindowTitle(title);
        dialog.resize(600, 460);

        QLineEdit *path = new QLineEdit(entry->path);
        QLineEdit *geometryFile = new QLineEdit(hardfileTailGeometryFile(*entry));
        QLineEdit *filesys = new QLineEdit(mountControllerParts(entry->hardfileTail).value(0));
        QLineEdit *device = new QLineEdit(entry->device.isEmpty() ? nextMountDeviceName() : entry->device);
        QSpinBox *bootPri = new QSpinBox;
        bootPri->setRange(-129, 127);
        bootPri->setValue(entry->bootPri);
        QCheckBox *readWrite = new QCheckBox(QStringLiteral("Read/write"));
        readWrite->setChecked(!entry->readOnly);
        QCheckBox *autoboot = new QCheckBox(QStringLiteral("Bootable"));
        QCheckBox *doNotMount = new QCheckBox(QStringLiteral("Do not mount"));
        QCheckBox *rdbMode = new QCheckBox(QStringLiteral("Full drive/RDB mode"));
        QCheckBox *manualGeometry = new QCheckBox(QStringLiteral("Manual geometry"));
        rdbMode->setChecked(hardfileIsRdb(*entry));
        manualGeometry->setChecked(hardfileHasPhysicalGeometry(*entry));

        const QStringList geometry = hardfileGeometryParts(*entry);
        QSpinBox *surfaces = new QSpinBox;
        QSpinBox *sectors = new QSpinBox;
        QSpinBox *reserved = new QSpinBox;
        QSpinBox *blockSize = new QSpinBox;
        for (QSpinBox *spin : { surfaces, sectors, reserved }) {
            spin->setRange(0, 1000000000);
        }
        blockSize->setRange(1, 65536);
        sectors->setValue(geometry.value(0).toInt());
        surfaces->setValue(geometry.value(1).toInt());
        reserved->setValue(geometry.value(2).toInt());
        blockSize->setValue(qMax(1, geometry.value(3, QStringLiteral("512")).toInt()));

        const QStringList physicalGeometry = hardfilePhysicalGeometryParts(*entry);
        QSpinBox *physicalCylinders = new QSpinBox;
        QSpinBox *physicalHeads = new QSpinBox;
        QSpinBox *physicalSectors = new QSpinBox;
        for (QSpinBox *spin : { physicalCylinders, physicalHeads, physicalSectors }) {
            spin->setRange(0, 1000000000);
        }
        physicalCylinders->setValue(physicalGeometry.value(0).toInt());
        physicalHeads->setValue(physicalGeometry.value(1).toInt());
        physicalSectors->setValue(physicalGeometry.value(2).toInt());

        QComboBox *controller = combo(mountControllerItemsForDialog(true, true, true, *entry, QStringLiteral("uae0")), mountControllerFamily(boardCatalog, *entry, QStringLiteral("uae0")));
        QSpinBox *controllerUnit = new QSpinBox;
        controllerUnit->setRange(0, MaxControllerUnits - 1);
        controllerUnit->setValue(mountControllerUnit(*entry, QStringLiteral("uae0")));
        QComboBox *mediaType = combo({ QStringLiteral("HD"), QStringLiteral("CF") }, hardfileTailHasToken(*entry, QStringLiteral("CF")) ? QStringLiteral("CF") : QStringLiteral("HD"));
        QComboBox *featureLevel = new QComboBox;

        QPushButton *browse = smallButton(QStringLiteral("..."));
        QPushButton *geometryBrowse = smallButton(QStringLiteral("..."));
        QPushButton *filesysBrowse = smallButton(QStringLiteral("..."));

        QGridLayout *fields = new QGridLayout;
        fields->setColumnStretch(1, 1);
        fields->addWidget(label(QStringLiteral("Path:")), 0, 0);
        fields->addWidget(path, 0, 1, 1, 2);
        fields->addWidget(browse, 0, 3);
        fields->addWidget(label(QStringLiteral("Geometry:")), 1, 0);
        fields->addWidget(geometryFile, 1, 1, 1, 2);
        fields->addWidget(geometryBrowse, 1, 3);
        fields->addWidget(label(QStringLiteral("FileSys:")), 2, 0);
        fields->addWidget(filesys, 2, 1, 1, 2);
        fields->addWidget(filesysBrowse, 2, 3);
        fields->addWidget(label(QStringLiteral("Device:")), 3, 0);
        fields->addWidget(device, 3, 1);
        fields->addWidget(label(QStringLiteral("Boot priority:")), 3, 2);
        fields->addWidget(bootPri, 3, 3);
        fields->addWidget(readWrite, 4, 1);
        fields->addWidget(autoboot, 4, 2);
        fields->addWidget(doNotMount, 5, 1);
        fields->addWidget(rdbMode, 6, 1);
        fields->addWidget(manualGeometry, 6, 2, 1, 2);

        QGridLayout *geometryLayout = new QGridLayout;
        geometryLayout->addWidget(label(QStringLiteral("Surfaces:")), 0, 0);
        geometryLayout->addWidget(surfaces, 0, 1);
        geometryLayout->addWidget(label(QStringLiteral("Sectors:")), 1, 0);
        geometryLayout->addWidget(sectors, 1, 1);
        geometryLayout->addWidget(label(QStringLiteral("Reserved:")), 2, 0);
        geometryLayout->addWidget(reserved, 2, 1);
        geometryLayout->addWidget(label(QStringLiteral("Block size:")), 3, 0);
        geometryLayout->addWidget(blockSize, 3, 1);
        geometryLayout->addWidget(label(QStringLiteral("Physical cyls:")), 0, 2);
        geometryLayout->addWidget(physicalCylinders, 0, 3);
        geometryLayout->addWidget(label(QStringLiteral("Physical heads:")), 1, 2);
        geometryLayout->addWidget(physicalHeads, 1, 3);
        geometryLayout->addWidget(label(QStringLiteral("Physical sectors:")), 2, 2);
        geometryLayout->addWidget(physicalSectors, 2, 3);

        QGridLayout *controllerLayout = new QGridLayout;
        controllerLayout->setColumnStretch(1, 1);
        controllerLayout->addWidget(label(QStringLiteral("HD Controller:")), 0, 0);
        controllerLayout->addWidget(controller, 0, 1);
        controllerLayout->addWidget(label(QStringLiteral("Unit:")), 0, 2);
        controllerLayout->addWidget(controllerUnit, 0, 3);
        controllerLayout->addWidget(label(QStringLiteral("Type:")), 1, 0);
        controllerLayout->addWidget(mediaType, 1, 1);
        controllerLayout->addWidget(label(QStringLiteral("Feature level:")), 1, 2);
        controllerLayout->addWidget(featureLevel, 1, 3);

        QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        QVBoxLayout *root = new QVBoxLayout(&dialog);
        root->addWidget(groupBox(QStringLiteral("Settings"), fields));
        root->addWidget(groupBox(QStringLiteral("Geometry"), geometryLayout));
        root->addWidget(groupBox(QStringLiteral("HD Controller"), controllerLayout));
        root->addWidget(buttons);

        auto updateBootChecks = [bootPri, autoboot, doNotMount]() {
            QSignalBlocker blockAutoboot(autoboot);
            QSignalBlocker blockDoNotMount(doNotMount);
            autoboot->setChecked(bootPri->value() > -128);
            doNotMount->setChecked(bootPri->value() <= -129);
        };
        auto updatePhysicalControls = [manualGeometry, physicalCylinders, physicalHeads, physicalSectors]() {
            const bool enabled = manualGeometry->isChecked();
            physicalCylinders->setEnabled(enabled);
            physicalHeads->setEnabled(enabled);
            physicalSectors->setEnabled(enabled);
        };
        auto setFeatureItems = [this, controller, featureLevel, mediaType](const QString &preferred) {
            const WinUaeQtMountControllerBus bus = mountControllerBusFromDisplay(boardCatalog, controller->currentText());
            QSignalBlocker blocker(featureLevel);
            featureLevel->clear();
            featureLevel->addItem(QStringLiteral("Default"));
            if (bus == MountControllerBusIde) {
                featureLevel->addItems({ QStringLiteral("ATA-1"), QStringLiteral("ATA-2+"), QStringLiteral("ATA-2+ Strict") });
            } else if (bus == MountControllerBusScsi) {
                featureLevel->addItems({ QStringLiteral("SCSI-1"), QStringLiteral("SCSI-2"), QStringLiteral("SASI"), QStringLiteral("SASI CHS") });
            }
            const int index = featureLevel->findText(preferred);
            featureLevel->setCurrentIndex(index >= 0 ? index : 0);
            featureLevel->setEnabled(bus == MountControllerBusIde || bus == MountControllerBusScsi);
            mediaType->setEnabled(bus == MountControllerBusIde);
        };
        auto updateNativeDriveControls = [path, readWrite, browse]() {
            const bool nativeDrive = path->text().trimmed().startsWith(QLatin1Char(':'));
            if (nativeDrive) {
                readWrite->setChecked(false);
            }
            readWrite->setEnabled(!nativeDrive);
            browse->setEnabled(!nativeDrive);
        };

        updateBootChecks();
        updatePhysicalControls();
        updateControllerUnitRange(controllerUnit, controller->currentText());
        setFeatureItems(hardfileFeatureText(boardCatalog, *entry, controller->currentText()));
        updateNativeDriveControls();

        connect(browse, &QPushButton::clicked, this, [this, path]() {
            const QString initialPath = fileDialogInitialPath(path->text());
            const QString selected = QFileDialog::getOpenFileName(this, QStringLiteral("Select hardfile"), initialPath, hardfileImageFilter());
            if (!selected.isEmpty()) {
                path->setText(selected);
            }
        });
        connect(geometryBrowse, &QPushButton::clicked, this, [this, geometryFile]() {
            const QString initialPath = fileDialogInitialPath(geometryFile->text());
            const QString selected = QFileDialog::getOpenFileName(this, QStringLiteral("Select geometry file"), initialPath, QStringLiteral("Geometry files (*.geo);;All files (*)"));
            if (!selected.isEmpty()) {
                geometryFile->setText(selected);
            }
        });
        connect(filesysBrowse, &QPushButton::clicked, this, [this, filesys]() {
            const QString initialPath = fileDialogInitialPath(filesys->text());
            const QString selected = QFileDialog::getOpenFileName(this, QStringLiteral("Select filesystem"), initialPath, QStringLiteral("Filesystem files (*.fs *.filesystem);;All files (*)"));
            if (!selected.isEmpty()) {
                filesys->setText(selected);
            }
        });
        connect(bootPri, QOverload<int>::of(&QSpinBox::valueChanged), this, updateBootChecks);
        connect(autoboot, &QCheckBox::toggled, this, [bootPri, doNotMount](bool checked) {
            QSignalBlocker blocker(doNotMount);
            if (checked) {
                bootPri->setValue(0);
                doNotMount->setChecked(false);
            } else if (bootPri->value() > -128) {
                bootPri->setValue(-128);
            }
        });
        connect(doNotMount, &QCheckBox::toggled, this, [bootPri, autoboot](bool checked) {
            QSignalBlocker blocker(autoboot);
            if (checked) {
                bootPri->setValue(-129);
                autoboot->setChecked(false);
            } else if (bootPri->value() <= -129) {
                bootPri->setValue(-128);
            }
        });
        connect(rdbMode, &QCheckBox::toggled, this, [sectors, surfaces, reserved, blockSize, device, filesys, bootPri](bool checked) {
            if (checked) {
                sectors->setValue(0);
                surfaces->setValue(0);
                reserved->setValue(0);
                blockSize->setValue(512);
                device->clear();
                filesys->clear();
                bootPri->setValue(0);
            } else if (sectors->value() == 0 && surfaces->value() == 0 && reserved->value() == 0) {
                sectors->setValue(32);
                surfaces->setValue(1);
                reserved->setValue(2);
                blockSize->setValue(512);
            }
        });
        connect(manualGeometry, &QCheckBox::toggled, this, updatePhysicalControls);
        connect(controller, &QComboBox::currentTextChanged, this, [this, controllerUnit, setFeatureItems](const QString &text) {
            updateControllerUnitRange(controllerUnit, text);
            setFeatureItems(QStringLiteral("Default"));
        });
        connect(path, &QLineEdit::editingFinished, this, updateNativeDriveControls);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() != QDialog::Accepted || path->text().trimmed().isEmpty()) {
            return false;
        }

        entry->kind = QStringLiteral("hdf");
        entry->device = winUaeQtSanitizedAmigaName(device->text(), nextMountDeviceName(), true);
        entry->path = path->text().trimmed();
        entry->bootPri = bootPri->value();
        entry->readOnly = !readWrite->isChecked() || entry->path.startsWith(QLatin1Char(':'));
        entry->hardfileGeometry = QStringLiteral("%1,%2,%3,%4")
            .arg(sectors->value())
            .arg(surfaces->value())
            .arg(reserved->value())
            .arg(blockSize->value());

        QStringList tailFields;
        tailFields.append(filesys->text().trimmed());
        tailFields.append(mountControllerConfigValue(boardCatalog, controller->currentText(), controllerUnit->value()));
        if (manualGeometry->isChecked() || !geometryFile->text().trimmed().isEmpty()) {
            tailFields.append(QString::number(physicalCylinders->value()));
            tailFields.append(QStringLiteral("%1/%2/%3")
                .arg(physicalCylinders->value())
                .arg(physicalHeads->value())
                .arg(physicalSectors->value()));
            if (!geometryFile->text().trimmed().isEmpty()) {
                tailFields.append(geometryFile->text().trimmed());
            }
        }
        if (mountControllerBusFromDisplay(boardCatalog, controller->currentText()) == MountControllerBusIde && mediaType->currentText() == QStringLiteral("CF")) {
            tailFields.append(QStringLiteral("CF"));
        }
        const QString featureToken = hardfileFeatureToken(featureLevel->currentText());
        if (!featureToken.isEmpty()) {
            tailFields.append(featureToken);
        }
        tailFields.append(hardfilePreservedTailExtras(*entry));
        entry->hardfileTail = winUaeQtConfigJoinFields(tailFields);
        return true;
    }

    void updateControllerUnitRange(QSpinBox *unit, const QString &family)
    {
        if (!unit) {
            return;
        }
        unit->setMaximum(mountControllerMaxUnit(boardCatalog, family));
    }

    bool showCdDriveMountDialog(WinUaeQtMountEntry *entry, const QString &title)
    {
        if (!entry) {
            return false;
        }

        QDialog dialog(this);
        dialog.setWindowTitle(title);

        QSpinBox *cdUnit = new QSpinBox;
        cdUnit->setRange(0, MaxControllerUnits - 1);
        cdUnit->setValue(qBound(0, entry->emuUnit, MaxControllerUnits - 1));
        QComboBox *controller = combo(mountControllerItemsForDialog(false, true, true, *entry, QStringLiteral("ide0")), mountControllerFamily(boardCatalog, *entry, QStringLiteral("ide0")));
        QSpinBox *controllerUnit = new QSpinBox;
        controllerUnit->setRange(0, MaxControllerUnits - 1);
        controllerUnit->setValue(mountControllerUnit(*entry, QStringLiteral("ide0")));
        updateControllerUnitRange(controllerUnit, controller->currentText());

        QGridLayout *fields = new QGridLayout;
        fields->setColumnStretch(1, 1);
        fields->addWidget(label(QStringLiteral("CD unit:")), 0, 0);
        fields->addWidget(cdUnit, 0, 1);
        fields->addWidget(label(QStringLiteral("Controller:")), 1, 0);
        fields->addWidget(controller, 1, 1);
        fields->addWidget(label(QStringLiteral("Controller unit:")), 2, 0);
        fields->addWidget(controllerUnit, 2, 1);

        QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        QVBoxLayout *root = new QVBoxLayout(&dialog);
        root->addLayout(fields);
        root->addWidget(buttons);

        connect(controller, &QComboBox::currentTextChanged, this, [this, controllerUnit](const QString &text) {
            updateControllerUnitRange(controllerUnit, text);
        });
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() != QDialog::Accepted) {
            return false;
        }

        entry->kind = QStringLiteral("cd");
        entry->device.clear();
        entry->volume = QStringLiteral("CD");
        entry->emuUnit = cdUnit->value();
        entry->readOnly = true;
        entry->bootPri = 0;
        entry->hardfileGeometry = QStringLiteral("0,0,0,2048");
        entry->hardfileTail = mountTailWithController(*entry, mountControllerConfigValue(boardCatalog, controller->currentText(), controllerUnit->value()));
        return true;
    }

    bool showTapeDriveMountDialog(WinUaeQtMountEntry *entry, const QString &title)
    {
        if (!entry) {
            return false;
        }

        QDialog dialog(this);
        dialog.setWindowTitle(title);

        QLineEdit *path = new QLineEdit(entry->path);
        QSpinBox *tapeUnit = new QSpinBox;
        tapeUnit->setRange(0, MaxControllerUnits - 1);
        tapeUnit->setValue(qBound(0, entry->emuUnit, MaxControllerUnits - 1));
        QComboBox *controller = combo(mountControllerItemsForDialog(true, true, true, *entry, QStringLiteral("uae0")), mountControllerFamily(boardCatalog, *entry, QStringLiteral("uae0")));
        QSpinBox *controllerUnit = new QSpinBox;
        controllerUnit->setRange(0, MaxControllerUnits - 1);
        controllerUnit->setValue(mountControllerUnit(*entry, QStringLiteral("uae0")));
        updateControllerUnitRange(controllerUnit, controller->currentText());
        QCheckBox *readWrite = new QCheckBox(QStringLiteral("Read/write"));
        readWrite->setChecked(!entry->readOnly);
        QPushButton *selectFile = new QPushButton(QStringLiteral("File..."));
        QPushButton *selectDirectory = new QPushButton(QStringLiteral("Directory..."));
        QPushButton *eject = new QPushButton(QStringLiteral("Eject"));

        QGridLayout *fields = new QGridLayout;
        fields->setColumnStretch(1, 1);
        fields->addWidget(label(QStringLiteral("Path:")), 0, 0);
        fields->addWidget(path, 0, 1, 1, 3);
        fields->addWidget(selectFile, 1, 1);
        fields->addWidget(selectDirectory, 1, 2);
        fields->addWidget(eject, 1, 3);
        fields->addWidget(label(QStringLiteral("Tape unit:")), 2, 0);
        fields->addWidget(tapeUnit, 2, 1);
        fields->addWidget(label(QStringLiteral("Controller:")), 3, 0);
        fields->addWidget(controller, 3, 1, 1, 3);
        fields->addWidget(label(QStringLiteral("Controller unit:")), 4, 0);
        fields->addWidget(controllerUnit, 4, 1);
        fields->addWidget(readWrite, 5, 1, 1, 3);

        QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        QVBoxLayout *root = new QVBoxLayout(&dialog);
        root->addLayout(fields);
        root->addWidget(buttons);

        connect(selectFile, &QPushButton::clicked, this, [this, path]() {
            const QString initialPath = fileDialogInitialPath(path->text());
            const QString selected = QFileDialog::getOpenFileName(this, QStringLiteral("Select tape image"), initialPath, QStringLiteral("Tape images (*.tap *.raw);;All files (*)"));
            if (!selected.isEmpty()) {
                path->setText(selected);
            }
        });
        connect(selectDirectory, &QPushButton::clicked, this, [this, path]() {
            const QString initialPath = fileDialogInitialDirectory(path->text());
            const QString selected = QFileDialog::getExistingDirectory(this, QStringLiteral("Select tape directory"), initialPath);
            if (!selected.isEmpty()) {
                path->setText(selected);
            }
        });
        connect(eject, &QPushButton::clicked, path, &QLineEdit::clear);
        connect(controller, &QComboBox::currentTextChanged, this, [this, controllerUnit](const QString &text) {
            updateControllerUnitRange(controllerUnit, text);
        });
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() != QDialog::Accepted) {
            return false;
        }

        entry->kind = QStringLiteral("tape");
        entry->device.clear();
        entry->volume = QStringLiteral("TAPE");
        entry->path = path->text().trimmed();
        entry->emuUnit = tapeUnit->value();
        entry->readOnly = !readWrite->isChecked();
        entry->bootPri = 0;
        entry->hardfileGeometry = QStringLiteral("0,0,0,512");
        entry->hardfileTail = mountTailWithController(*entry, mountControllerConfigValue(boardCatalog, controller->currentText(), controllerUnit->value()));
        return true;
    }

    WinUaeQtConfig::OrderedSettings currentMountSettings() const
    {
        WinUaeQtConfig::OrderedSettings settings;
        if (!mountedDrives) {
            return settings;
        }
        QSet<QString> emitted;
        for (int i = 0; i < mountedDrives->topLevelItemCount(); i++) {
            const WinUaeQtMountEntry entry = mountEntryFromItem(mountedDrives->topLevelItem(i));
            const QString key = mountEntryConfigKey(entry, i);
            const QString value = mountEntryConfigValue(entry);
            if (key.isEmpty() || value.isEmpty()) {
                continue;
            }
            const QString duplicateKey = mountEntryConfigKey(entry) + QLatin1Char('\n') + value;
            if (emitted.contains(duplicateKey)) {
                continue;
            }
            emitted.insert(duplicateKey);
            settings.append({ key, value });
        }
        return settings;
    }

    void syncQuickDriveToFloppy(int drive)
    {
        if (drive < 0 || drive >= 2 || !dfPath[drive] || !quickDfPath[drive]) {
            return;
        }
        setCheckBoxIfChanged(dfEnable[drive], quickDfEnable[drive]->isChecked());
        setComboTextIfChanged(dfType[drive], quickDfType[drive]->currentText());
        if (dfPath[drive]->currentText() != quickDfPath[drive]->currentText()) {
            setPathComboText(dfPath[drive], quickDfPath[drive]->currentText());
        }
        setCheckBoxIfChanged(dfWriteProtect[drive], quickDfWriteProtect[drive]->isChecked());
    }

    void syncFloppyDriveToQuick(int drive)
    {
        if (drive < 0 || drive >= 2 || !dfPath[drive] || !quickDfPath[drive]) {
            return;
        }
        setCheckBoxIfChanged(quickDfEnable[drive], dfEnable[drive]->isChecked());
        setComboTextIfChanged(quickDfType[drive], dfType[drive]->currentText());
        if (quickDfPath[drive]->currentText() != dfPath[drive]->currentText()) {
            setPathComboText(quickDfPath[drive], dfPath[drive]->currentText());
        }
        setCheckBoxIfChanged(quickDfWriteProtect[drive], dfWriteProtect[drive]->isChecked());
    }

    int rtgModeMask() const
    {
        int mask = 0;
        mask |= rtgColorDepthMask(rtg8Bit->currentText());
        mask |= rtgColorDepthMask(rtg16Bit->currentText());
        mask |= rtgColorDepthMask(rtg24Bit->currentText());
        mask |= rtgColorDepthMask(rtg32Bit->currentText());
        return mask ? mask : RtgDefaultModeMask;
    }

    QString rtgOptionsValue() const
    {
        QStringList parts;
        if (!rtgAutoswitch->isChecked()) {
            parts.append(QStringLiteral("noautoswitch"));
        }
        return parts.join(QLatin1Char(','));
    }

    void applyRtgScaleValue(const QString &value)
    {
        const QString lower = value.toLower();
        rtgScale->setChecked(lower == QStringLiteral("scale"));
        rtgCenter->setChecked(lower == QStringLiteral("center"));
        rtgIntegerScale->setChecked(lower == QStringLiteral("integer"));
    }

    void applyRtgOptionsValue(const QString &value)
    {
        bool autoswitch = true;
        for (const QString &field : winUaeQtConfigFieldList(value)) {
            const QString trimmed = field.trimmed();
            if (trimmed.compare(QStringLiteral("noautoswitch"), Qt::CaseInsensitive) == 0) {
                autoswitch = false;
            } else if (trimmed.compare(QStringLiteral("autoswitch"), Qt::CaseInsensitive) == 0) {
                autoswitch = true;
            }
        }
        rtgAutoswitch->setChecked(autoswitch);
        rtgInitialMonitor->setChecked(false);
        rtgMonitor->setCurrentText(QStringLiteral("1"));
    }

    WinUaeQtConfig::Settings currentSettings() const
    {
        WinUaeQtConfig::Settings settings;
        const int cpu = selectedCpuModel();
        const int fpu = fpuModelConfigValue(cpu);
        const bool z3Rtg = rtgNeeds32BitAddressSpace();
        const bool cpu24BitAddressing = !z3Rtg && cpu24Bit && cpu24Bit->isChecked();
        const int requestedJitCacheSize = jitCache ? jitCacheSizeFromPosition(jitCache->value()) : 0;
        const bool jitActive = jit && jit->isChecked() && unixJitBackendAvailable()
            && cpu >= 68020 && !cpu24BitAddressing && requestedJitCacheSize > 0;
        if (configDescription && !configDescription->text().trimmed().isEmpty()) {
            settings.insert(QStringLiteral("config_description"), configDescription->text().trimmed());
        }
        if (quickstartMode && quickstartMode->isChecked()) {
            const QString quickstart = quickstartConfigValue();
            if (!quickstart.isEmpty()) {
                settings.insert(QStringLiteral("quickstart"), quickstart);
            }
        }
        if (romsPath && !romsPath->text().trimmed().isEmpty()) {
            settings.insert(QStringLiteral("unix.rom_path"), romsPath->text().trimmed());
        }
        insertLineEditSetting(settings, QStringLiteral("unix.config_path"), configsPath);
        insertLineEditSetting(settings, QStringLiteral("unix.nvram_path"), nvramPath);
        insertLineEditSetting(settings, QStringLiteral("unix.screenshot_path"), screenshotsPath);
        insertLineEditSetting(settings, QStringLiteral("statefile_path"), stateFilesPath);
        insertLineEditSetting(settings, QStringLiteral("unix.video_path"), videosPath);
        insertLineEditSetting(settings, QStringLiteral("unix.saveimage_path"), saveImagesPath);
        insertLineEditSetting(settings, QStringLiteral("unix.rip_path"), ripsPath);
        insertLineEditSetting(settings, QStringLiteral("unix.data_path"), dataPath);
        if (pathDefaultType) {
            settings.insert(QStringLiteral("unix.ui.path_mode"), pathDefaultType->currentText());
        }
        insertCheckBoxSetting(settings, QStringLiteral("unix.ui.recursive_roms"), recursiveRoms);
        insertCheckBoxSetting(settings, QStringLiteral("unix.ui.cache_configurations"), cacheConfigurations);
        insertCheckBoxSetting(settings, QStringLiteral("unix.ui.cache_boxart"), cacheBoxArt);
        insertCheckBoxSetting(settings, QStringLiteral("unix.ui.saveimage_original_path"), saveImageOriginalPath);
        insertCheckBoxSetting(settings, QStringLiteral("unix.ui.relative_paths"), relativePaths);
        insertCheckBoxSetting(settings, QStringLiteral("unix.ui.portable_mode"), portableMode);
        insertCheckBoxSetting(settings, QStringLiteral("unix.ui.full_logging"), fullLogging);
        insertCheckBoxSetting(settings, QStringLiteral("unix.ui.log_window"), logWindow);
        if (miscGuiSize) {
            settings.insert(QStringLiteral("unix.ui.gui_scale"), miscGuiSize->currentText());
        }
        insertCheckBoxSetting(settings, QStringLiteral("unix.ui.gui_resize"), miscGuiResize);
        insertCheckBoxSetting(settings, QStringLiteral("unix.ui.gui_fullscreen"), miscGuiFullscreen);
        settings.insert(QStringLiteral("unix.ui.gui_dark_mode"), guiDarkModeConfigValue());
        if (!miscGuiFontConfig.isEmpty()) {
            settings.insert(QStringLiteral("unix.ui.gui_font"), miscGuiFontConfig);
        }
        insertLineEditSetting(settings, QStringLiteral("unix.output_file"), outputFile);
        settings.insert(QStringLiteral("unix.output_audio_codec"),
            outputAudioCodec && outputAudioCodec->currentIndex() == 1 ? QStringLiteral("wav") : QStringLiteral("pcm"));
        settings.insert(QStringLiteral("unix.output_video_codec"), QStringLiteral("dib"));
        insertCheckBoxSetting(settings, QStringLiteral("unix.output_audio"), outputAudio);
        insertCheckBoxSetting(settings, QStringLiteral("unix.output_video"), outputVideo);
        insertCheckBoxSetting(settings, QStringLiteral("unix.output_enabled"), outputEnabled);
        insertCheckBoxSetting(settings, QStringLiteral("unix.output_frame_limiter_disabled"), outputFrameLimiter);
        insertCheckBoxSetting(settings, QStringLiteral("unix.output_original_size"), outputOriginalSize);
        insertCheckBoxSetting(settings, QStringLiteral("unix.output_no_sound"), outputNoSound);
        insertCheckBoxSetting(settings, QStringLiteral("unix.output_no_sound_sync"), outputNoSoundSync);
        insertCheckBoxSetting(settings, QStringLiteral("unix.screenshot_original_size"), screenshotOriginalSize);
        insertCheckBoxSetting(settings, QStringLiteral("unix.screenshot_paletted"), screenshotPaletted);
        insertCheckBoxSetting(settings, QStringLiteral("unix.screenshot_clip"), screenshotClip);
        insertCheckBoxSetting(settings, QStringLiteral("unix.screenshot_auto"), screenshotAuto);
        settings.insert(QStringLiteral("kickstart_rom_file"), romFile->currentText());
        if (!extendedRomFile->currentText().isEmpty()) {
            settings.insert(QStringLiteral("kickstart_ext_rom_file"), extendedRomFile->currentText());
        }
        if (!cartFile->currentText().isEmpty()) {
            settings.insert(QStringLiteral("cart_file"), cartFile->currentText());
        }
        if (!flashFile->text().isEmpty()) {
            settings.insert(QStringLiteral("flash_file"), flashFile->text());
        }
        if (!rtcFile->text().isEmpty()) {
            settings.insert(QStringLiteral("rtc_file"), rtcFile->text());
        }
        settings.insert(QStringLiteral("maprom"), mapRom->isChecked() ? QStringLiteral("0x0f000000") : QStringLiteral("0x0"));
        settings.insert(QStringLiteral("kickshifter"), kickShifter->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        if (uaeBoardType->currentText() == QStringLiteral("ROM disabled")) {
            settings.insert(QStringLiteral("boot_rom_uae"), QStringLiteral("disabled"));
            settings.insert(QStringLiteral("uaeboard"), QStringLiteral("disabled"));
        } else {
            settings.insert(QStringLiteral("boot_rom_uae"), QStringLiteral("automatic"));
            settings.insert(QStringLiteral("uaeboard"), uaeBoardConfigValue(uaeBoardType->currentText()));
        }
        for (int i = 0; i < MaxRomBoards; i++) {
            WinUaeQtRomBoard board = customRomBoards.value(i);
            if (i == currentCustomRomBoard && customRomStart && customRomEnd && customRomFile) {
                board.start = customRomStart->text();
                board.end = customRomEnd->text();
                board.path = customRomFile->text();
            }
            const QString value = romBoardConfigValue(board);
            if (!value.isEmpty()) {
                settings.insert(romBoardKey(i), value);
            }
        }
        settings.insert(QStringLiteral("board_custom_order"), hardwareCustomBoardOrder->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        for (int i = 0; i < 4; i++) {
            const int driveType = dfEnable[i]->isChecked() ? floppyTypeConfigValue(dfType[i]->currentText()) : -1;
            settings.insert(QStringLiteral("floppy%1type").arg(i), QString::number(driveType));
            settings.insert(QStringLiteral("floppy%1wp").arg(i), dfWriteProtect[i]->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
            /* Always write the path, empty included, so ejecting a disk in
             * the runtime GUI reaches the core's disk-change detection. */
            settings.insert(QStringLiteral("floppy%1").arg(i), driveType >= 0 ? dfPath[i]->currentText() : QString());
        }
        settings.insert(QStringLiteral("nr_floppies"), QString::number(enabledFloppyCount()));
        settings.insert(QStringLiteral("floppy_speed"), QString::number(floppySpeedConfigValue(floppySpeed->value())));
        for (int i = 0; i < MaxDiskSwapperSlots; i++) {
            const QString path = diskSwapperPathAt(i);
            if (!path.isEmpty()) {
                settings.insert(QStringLiteral("diskimage%1").arg(i), path);
            }
        }
        settings.insert(QStringLiteral("chipset"), chipsetConfigValue(chipset->currentText()));
        settings.insert(QStringLiteral("chipset_compatible"), chipsetCompatible->currentText());
        settings.insert(QStringLiteral("ntsc"), chipsetNtsc->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("immediate_blits"), immediateBlits->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("waiting_blits"), waitingBlits->isChecked() ? QStringLiteral("automatic") : QStringLiteral("disabled"));
        settings.insert(QStringLiteral("collision_level"), QStringList({ QStringLiteral("none"), QStringLiteral("sprites"), QStringLiteral("playfields"), QStringLiteral("full") }).value(collisionButtons->checkedId(), QStringLiteral("full")));
        settings.insert(QStringLiteral("display_optimizations"), displayOptimizationConfigValue(displayOptimization->currentText()));
        settings.insert(QStringLiteral("hvcsync"),
            configChoiceValue(hvSyncChoices, int(sizeof(hvSyncChoices) / sizeof(hvSyncChoices[0])), chipsetSyncMode->currentText()));
        settings.insert(QStringLiteral("genlock"), genlockConnected->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("genlockmode"),
            configChoiceValue(genlockModeChoices, int(sizeof(genlockModeChoices) / sizeof(genlockModeChoices[0])), genlockMode->currentText()));
        settings.insert(QStringLiteral("genlock_mix"), QString::number(genlockMixConfigValue()));
        settings.insert(QStringLiteral("genlock_alpha"), genlockAlpha->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("genlock_aspect"), genlockKeepAspect->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        QString imagePath = genlockImagePath;
        QString videoPath = genlockVideoPath;
        const QString selectedGenlockMode = configChoiceValue(genlockModeChoices, int(sizeof(genlockModeChoices) / sizeof(genlockModeChoices[0])), genlockMode->currentText());
        if (genlockModeUsesImageFile(selectedGenlockMode)) {
            imagePath = genlockFile->currentText().trimmed();
        } else if (genlockModeUsesVideoFile(selectedGenlockMode)) {
            videoPath = genlockFile->currentText().trimmed();
        }
        if (!imagePath.isEmpty()) {
            settings.insert(QStringLiteral("genlock_image"), imagePath);
        }
        if (!videoPath.isEmpty()) {
            settings.insert(QStringLiteral("genlock_video"), videoPath);
        }
        const QString keyboardType = configChoiceValue(keyboardModeChoices, int(sizeof(keyboardModeChoices) / sizeof(keyboardModeChoices[0])), keyboardMode->currentText());
        settings.insert(QStringLiteral("keyboard_connected"), keyboardType == QStringLiteral("disconnected") ? QStringLiteral("false") : QStringLiteral("true"));
        settings.insert(QStringLiteral("keyboard_type"), keyboardType);
        settings.insert(QStringLiteral("keyboard_nkro"), keyboardNkro->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("ciaatod"), advancedRadioValue(advancedCiaTodButtons, ciaTodChoices, int(sizeof(ciaTodChoices) / sizeof(ciaTodChoices[0]))));
        settings.insert(QStringLiteral("rtc"), advancedRadioValue(advancedRtcButtons, rtcChoices, int(sizeof(rtcChoices) / sizeof(rtcChoices[0]))));
        settings.insert(QStringLiteral("chipset_rtc_adjust"), advancedRtcAdjust->text().trimmed().isEmpty() ? QStringLiteral("0") : advancedRtcAdjust->text().trimmed());
        for (const AdvancedCheckChoice &choice : advancedCheckChoices) {
            const QString key = QString::fromLatin1(choice.key);
            settings.insert(key, advancedCheck(key) ? QStringLiteral("true") : QStringLiteral("false"));
        }
        settings.insert(QStringLiteral("ide"), advancedIdeA600A1200->isChecked()
            ? QStringLiteral("a600/a1200")
            : (advancedIdeA4000->isChecked() ? QStringLiteral("a4000") : QStringLiteral("none")));
        settings.insert(QStringLiteral("scsi_a3000"), advancedScsiA3000->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("scsi_a4000t"), advancedScsiA4000T->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("ciaa_type"), advancedCia391078->isChecked() ? QStringLiteral("391078-01") : QStringLiteral("default"));
        settings.insert(QStringLiteral("ciab_type"), advancedCia391078->isChecked() ? QStringLiteral("391078-01") : QStringLiteral("default"));
        settings.insert(QStringLiteral("unmapped_address_space"),
            configChoiceValue(unmappedAddressChoices, int(sizeof(unmappedAddressChoices) / sizeof(unmappedAddressChoices[0])), advancedUnmappedAddress->currentText()));
        settings.insert(QStringLiteral("eclocksync"),
            configChoiceValue(ciaSyncChoices, int(sizeof(ciaSyncChoices) / sizeof(ciaSyncChoices[0])), advancedCiaSync->currentText()));
        settings.insert(QStringLiteral("fatgary"), chipsetRevisionConfigValue(advancedFatGary, advancedFatGaryRevision, 0x00));
        settings.insert(QStringLiteral("ramsey"), chipsetRevisionConfigValue(advancedRamsey, advancedRamseyRevision, 0x0f));
        settings.insert(QStringLiteral("agnusmodel"),
            configChoiceValue(agnusModelChoices, int(sizeof(agnusModelChoices) / sizeof(agnusModelChoices[0])), advancedAgnusModel->currentText()));
        settings.insert(QStringLiteral("agnussize"),
            configChoiceValue(agnusSizeChoices, int(sizeof(agnusSizeChoices) / sizeof(agnusSizeChoices[0])), advancedAgnusSize->currentText()));
        settings.insert(QStringLiteral("denisemodel"),
            configChoiceValue(deniseModelChoices, int(sizeof(deniseModelChoices) / sizeof(deniseModelChoices[0])), advancedDeniseModel->currentText()));
        if (jitActive) {
            settings.insert(QStringLiteral("cycle_exact"), QStringLiteral("false"));
            settings.insert(QStringLiteral("cpu_cycle_exact"), QStringLiteral("false"));
            settings.insert(QStringLiteral("cpu_memory_cycle_exact"), QStringLiteral("false"));
            settings.insert(QStringLiteral("blitter_cycle_exact"), QStringLiteral("false"));
        } else if (chipsetCycleExact->isChecked()) {
            settings.insert(QStringLiteral("cycle_exact"), QStringLiteral("true"));
            settings.insert(QStringLiteral("cpu_cycle_exact"), QStringLiteral("true"));
            settings.insert(QStringLiteral("cpu_memory_cycle_exact"), QStringLiteral("true"));
            settings.insert(QStringLiteral("blitter_cycle_exact"), QStringLiteral("true"));
        } else if (chipsetCycleExactMemory->isChecked()) {
            settings.insert(QStringLiteral("cycle_exact"), QStringLiteral("memory"));
            settings.insert(QStringLiteral("cpu_cycle_exact"), QStringLiteral("false"));
            settings.insert(QStringLiteral("cpu_memory_cycle_exact"), QStringLiteral("true"));
            settings.insert(QStringLiteral("blitter_cycle_exact"), QStringLiteral("true"));
        } else {
            settings.insert(QStringLiteral("cycle_exact"), QStringLiteral("false"));
            settings.insert(QStringLiteral("cpu_cycle_exact"), QStringLiteral("false"));
            settings.insert(QStringLiteral("cpu_memory_cycle_exact"), QStringLiteral("false"));
            settings.insert(QStringLiteral("blitter_cycle_exact"), QStringLiteral("false"));
        }
        settings.insert(QStringLiteral("cpu_model"), QString::number(cpu));
        settings.insert(QStringLiteral("cpu_speed"), cpuSpeedButtons->checkedId() == 1 ? QStringLiteral("max") : QStringLiteral("real"));
        if (cpuSpeed->value() != 0) {
            settings.insert(QStringLiteral("cpu_throttle"), QString::number(cpuSpeed->value() * 100.0, 'f', 1));
        }
        settings.insert(QStringLiteral("cpu_compatible"), moreCompatible->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        if (fpu) {
            settings.insert(QStringLiteral("fpu_model"), QString::number(fpu));
        }
        settings.insert(QStringLiteral("cpu_24bit_addressing"), cpu24BitAddressing ? QStringLiteral("true") : QStringLiteral("false"));
        if (mmuButtons->checkedId() == 1 && cpu >= 68030) {
            settings.insert(QStringLiteral("mmu_model"), QString::number(cpu));
        } else if (mmuButtons->checkedId() == 2 && cpu >= 68030) {
            settings.insert(QStringLiteral("mmu_model"), QStringLiteral("68ec0%1").arg(cpu % 100, 2, 10, QLatin1Char('0')));
        }
        settings.insert(QStringLiteral("cpu_no_unimplemented"), cpuUnimplemented->isChecked() ? QStringLiteral("false") : QStringLiteral("true"));
        settings.insert(QStringLiteral("fpu_no_unimplemented"), fpuUnimplemented->isChecked() ? QStringLiteral("false") : QStringLiteral("true"));
        settings.insert(QStringLiteral("fpu_strict"), fpuStrict->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("fpu_softfloat"), fpuMode->currentText() == QStringLiteral("Softfloat (80-bit)") ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("fpu_msvc_long_double"), fpuMode->currentText() == QStringLiteral("Host (80-bit)") ? QStringLiteral("true") : QStringLiteral("false"));
        if (cpuFrequency->currentText() == QStringLiteral("Custom")) {
            bool ok = false;
            const double mhz = cpuFrequencyCustom->text().toDouble(&ok);
            if (ok && mhz >= 1.0 && mhz < 99.0) {
                settings.insert(QStringLiteral("cpu_frequency"), QString::number(qRound64(mhz * 1000000.0)));
            }
        } else {
            settings.insert(QStringLiteral("cpu_multiplier"), QString::number(cpuMultiplierValue(cpuFrequency->currentText())));
        }
        settings.insert(QStringLiteral("chipmem_size"), QString::number(chipMemConfigValue()));
        if (z2Fast->currentText() != QStringLiteral("None")) {
            settings.insert(QStringLiteral("fastmem_size"), QString::number(megabytesFromText(z2Fast->currentText())));
        }
        const int slow = slowMemConfigValue();
        if (slow) {
            settings.insert(QStringLiteral("bogomem_size"), QString::number(slow));
        }
        if (z3Fast->currentText() != QStringLiteral("None")) {
            settings.insert(QStringLiteral("z3mem_size"), QString::number(megabytesFromText(z3Fast->currentText())));
        }
        if (z3ChipMem->currentText() != QStringLiteral("None")) {
            settings.insert(QStringLiteral("megachipmem_size"), QString::number(megabytesFromText(z3ChipMem->currentText())));
        }
        if (processorSlotMem->currentText() != QStringLiteral("None")) {
            settings.insert(QStringLiteral("mbresmem_size"), QString::number(megabytesFromText(processorSlotMem->currentText())));
        }
        settings.insert(QStringLiteral("z3mapping"),
            configChoiceValue(z3MappingChoices, int(sizeof(z3MappingChoices) / sizeof(z3MappingChoices[0])), z3Mapping->currentText()));
        settings.insert(QStringLiteral("cachesize"), QString::number(jitActive ? requestedJitCacheSize : 0));
        settings.insert(QStringLiteral("compfpu"), jitActive && jitFpu->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("comp_constjump"), jitConstJump->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("comp_flushmode"), jitHardFlush->isChecked() ? QStringLiteral("hard") : QStringLiteral("soft"));
        settings.insert(QStringLiteral("comp_nf"), jitNoFlags->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("comp_catchfault"), jitCatchFault->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        const QString trust = jitTrust->checkedId() == 1 ? QStringLiteral("indirect") : QStringLiteral("direct");
        settings.insert(QStringLiteral("comp_trustbyte"), trust);
        settings.insert(QStringLiteral("comp_trustword"), trust);
        settings.insert(QStringLiteral("comp_trustlong"), trust);
        settings.insert(QStringLiteral("comp_trustnaddr"), trust);
        settings.insert(QStringLiteral("unix.soundcard"), QString::number(qMax(0, soundDevice->currentIndex())));
        const QString soundDeviceConfigName = soundDevice->currentData().toString();
        if (!soundDeviceConfigName.isEmpty()) {
            settings.insert(QStringLiteral("unix.soundcardname"), soundDeviceConfigName);
        }
        settings.insert(QStringLiteral("sound_output"), soundOutputConfigValue(soundOutputButtons->checkedId()));
        settings.insert(QStringLiteral("sound_auto"), soundAutomatic->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("sound_volume"), QString::number(100 - soundMasterVolume->value()));
        settings.insert(QStringLiteral("sound_volume_paula"), QString::number(soundVolumeAttenuationValue(0)));
        settings.insert(QStringLiteral("sound_volume_cd"), QString::number(soundVolumeAttenuationValue(1)));
        settings.insert(QStringLiteral("sound_volume_ahi"), QString::number(soundVolumeAttenuationValue(2)));
        settings.insert(QStringLiteral("sound_volume_midi"), QString::number(soundVolumeAttenuationValue(3)));
        settings.insert(QStringLiteral("sound_volume_genlock"), QString::number(soundVolumeAttenuationValue(4)));
        settings.insert(QStringLiteral("sound_max_buff"), QString::number(soundBufferSizeFromIndex(soundBufferSize->value())));
        settings.insert(QStringLiteral("sound_channels"), soundChannelConfigValue(soundChannels->currentText()));
        if (soundChannels->currentText() == QStringLiteral("Mono")) {
            settings.insert(QStringLiteral("sound_stereo_separation"), QStringLiteral("0"));
            settings.insert(QStringLiteral("sound_stereo_mixing_delay"), QStringLiteral("0"));
        } else {
            settings.insert(QStringLiteral("sound_stereo_separation"), QString::number(qBound(0, 10 - soundStereoSeparation->currentIndex(), 10)));
            settings.insert(QStringLiteral("sound_stereo_mixing_delay"), soundStereoDelay->currentText() == QStringLiteral("-") ? QStringLiteral("0") : soundStereoDelay->currentText());
        }
        settings.insert(QStringLiteral("sound_frequency"), QString::number(qBound(8000, soundFrequency->currentText().toInt(), 768000)));
        settings.insert(QStringLiteral("sound_interpol"), soundInterpolationConfigValue(soundInterpolation->currentText()));
        settings.insert(QStringLiteral("sound_filter"), soundFilterConfigValue(soundFilter->currentText()));
        settings.insert(QStringLiteral("sound_filter_type"), soundFilterTypeConfigValue(soundFilter->currentText()));
        const int swapIndex = soundSwap->currentIndex();
        settings.insert(QStringLiteral("sound_stereo_swap_paula"), (swapIndex & 1) ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("sound_stereo_swap_ahi"), (swapIndex & 2) ? QStringLiteral("true") : QStringLiteral("false"));
        for (int i = 0; i < FloppySoundDriveCount; i++) {
            settings.insert(QStringLiteral("floppy%1sound").arg(i), QString::number(floppySoundTypeConfigValue(i)));
            settings.insert(QStringLiteral("floppy%1soundvolume_empty").arg(i), QString::number(floppySoundEmptyAttenuationValue(i)));
            settings.insert(QStringLiteral("floppy%1soundvolume_disk").arg(i), QString::number(floppySoundDiskAttenuationValue(i)));
        }
        const QString printerTypeValue = configChoiceValue(printerTypeChoices, int(sizeof(printerTypeChoices) / sizeof(printerTypeChoices[0])), printerType->currentText());
        settings.insert(QStringLiteral("parallel_matrix_emulation"),
            printerTypeValue.startsWith(QStringLiteral("postscript_")) ? QStringLiteral("none") : printerTypeValue);
        settings.insert(QStringLiteral("parallel_postscript_detection"),
            printerTypeValue.startsWith(QStringLiteral("postscript_")) ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("parallel_postscript_emulation"),
            printerTypeValue == QStringLiteral("postscript_emulation") ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("parallel_autoflush"), QString::number(printerAutoFlush->value()));
        if (!ghostscriptParams->text().trimmed().isEmpty()) {
            settings.insert(QStringLiteral("ghostscript_parameters"), ghostscriptParams->text().trimmed());
        }
        const int samplerIndex = samplerDevice ? samplerDevice->currentIndex() - 1 : -1;
        settings.insert(QStringLiteral("unix.samplersoundcard"), QString::number(samplerIndex));
        const QString samplerDeviceConfigName = samplerDevice ? samplerDevice->currentData().toString() : QString();
        if (samplerIndex >= 0 && !samplerDeviceConfigName.isEmpty()) {
            settings.insert(QStringLiteral("unix.samplersoundcardname"), samplerDeviceConfigName);
        }
        settings.insert(QStringLiteral("sampler_stereo"), samplerIndex >= 0 && samplerStereo->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        const int midiOutDevice = midiOut ? midiOut->currentData().toInt() : -2;
        settings.insert(QStringLiteral("midiout_device"), QString::number(midiOutDevice));
        const int midiInDevice = midiIn ? midiIn->currentData().toInt() : -1;
        settings.insert(QStringLiteral("midiin_device"), QString::number(midiInDevice));
        QString midiOutName = QStringLiteral("none");
        if (midiOutDevice == -1) {
            midiOutName = QStringLiteral("default");
        } else if (midiOutDevice >= 0 && midiOut) {
            midiOutName = midiOut->currentData(Qt::UserRole + 1).toString();
            if (midiOutName.isEmpty()) {
                midiOutName = midiOut->currentText();
            }
        }
        settings.insert(QStringLiteral("midiout_device_name"), midiOutName);
        QString midiInName = QStringLiteral("none");
        if (midiInDevice >= 0 && midiIn) {
            midiInName = midiIn->currentData(Qt::UserRole + 1).toString();
            if (midiInName.isEmpty()) {
                midiInName = midiIn->currentText();
            }
        }
        settings.insert(QStringLiteral("midiin_device_name"), midiInName);
        const bool midiCanRoute = midiRouter && midiOutDevice >= -1 && midiInDevice >= 0;
        settings.insert(QStringLiteral("midirouter"), midiCanRoute && midiRouter->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        const QString serial = serialPort->currentText().trimmed();
        if (!serial.isEmpty()
            && serial != QStringLiteral("<None>")
            && serial.compare(QStringLiteral("none"), Qt::CaseInsensitive) != 0) {
            settings.insert(QStringLiteral("unix.serial_port"), serial);
        }
        settings.insert(QStringLiteral("serial_on_demand"), serialShared->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("serial_hardware_ctsrts"), serialCtsRts->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("serial_status"), serialStatus->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("serial_ri"), serialRingIndicator->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("serial_direct"), serialDirect->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("serial_translate"), serialCrlf->isChecked() ? QStringLiteral("crlf_cr") : QStringLiteral("disabled"));
        settings.insert(QStringLiteral("uaeserial"), uaeSerial->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        const QString dongle = configChoiceValue(dongleChoices, int(sizeof(dongleChoices) / sizeof(dongleChoices[0])), protectionDongle->currentText());
        if (!dongle.isEmpty() && dongle != QStringLiteral("none")) {
            settings.insert(QStringLiteral("dongle"), dongle);
        }
        for (const MiscCheckChoice &choice : miscCheckChoices) {
            const QString key = QString::fromLatin1(choice.key);
            if (key == QStringLiteral("power_led_dim")) {
                settings.insert(key, miscOptionChecked(key) ? QStringLiteral("128") : QStringLiteral("0"));
            } else {
                settings.insert(key, miscOptionChecked(key) ? QStringLiteral("true") : QStringLiteral("false"));
            }
        }
        if (stateFileName && !stateFileClear->isChecked() && !stateFileName->currentText().trimmed().isEmpty()) {
            settings.insert(QStringLiteral("statefile"), stateFileName->currentText().trimmed());
        }
        QStringList ledValues;
        bool anyKeyboardLed = false;
        const QStringList ledNames { QStringLiteral("numlock"), QStringLiteral("capslock"), QStringLiteral("scrolllock") };
        for (int i = 0; i < 3; i++) {
            const QString value = configChoiceValue(keyboardLedChoices, int(sizeof(keyboardLedChoices) / sizeof(keyboardLedChoices[0])), keyboardLed[i]->currentText());
            ledValues.append(QStringLiteral("%1:%2").arg(ledNames[i], value.isEmpty() ? QStringLiteral("none") : value));
            anyKeyboardLed = anyKeyboardLed || (!value.isEmpty() && value.compare(QStringLiteral("none"), Qt::CaseInsensitive) != 0);
        }
        if (anyKeyboardLed) {
            settings.insert(QStringLiteral("keyboard_leds"), ledValues.join(QLatin1Char(',')));
        }
        settings.insert(QStringLiteral("active_priority"), QString::number(extensionPriorityValue(extensionActivePriority)));
        settings.insert(QStringLiteral("active_not_captured_pause"), extensionActivePause->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("active_not_captured_nosound"), extensionActiveNoSound->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("active_input"), QString::number((extensionActiveNoJoy->isChecked() ? 0 : 4) | (extensionActiveNoKeyboard->isChecked() ? 0 : 1)));
        settings.insert(QStringLiteral("inactive_priority"), QString::number(extensionPriorityValue(extensionInactivePriority)));
        settings.insert(QStringLiteral("inactive_pause"), extensionInactivePause->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("inactive_nosound"), extensionInactiveNoSound->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("inactive_input"), QString::number(extensionInactiveNoJoy->isChecked() ? 0 : 4));
        settings.insert(QStringLiteral("iconified_priority"), QString::number(extensionPriorityValue(extensionMinimizedPriority)));
        settings.insert(QStringLiteral("iconified_pause"), extensionMinimizedPause->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("iconified_nosound"), extensionMinimizedNoSound->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("iconified_input"), QString::number(extensionMinimizedNoJoy->isChecked() ? 0 : 4));
        if (rtgMem->currentText() != QStringLiteral("None")) {
            settings.insert(QStringLiteral("gfxcard_size"), QString::number(megabytesFromText(rtgMem->currentText())));
            settings.insert(QStringLiteral("gfxcard_type"), rtgType->currentText());
            const QString options = rtgOptionsValue();
            if (!options.isEmpty()) {
                settings.insert(QStringLiteral("gfxcard_options"), options);
            }
        }
        settings.insert(QStringLiteral("gfx_filter_autoscale_rtg"), rtgScaleConfigValue(rtgScale->isChecked(), rtgCenter->isChecked(), rtgIntegerScale->isChecked()));
        settings.insert(QStringLiteral("gfx_filter_aspect_ratio_rtg"),
            configChoiceValue(rtgAspectRatioChoices, int(sizeof(rtgAspectRatioChoices) / sizeof(rtgAspectRatioChoices[0])), rtgAspectRatio->currentText()));
        settings.insert(QStringLiteral("unix.rtg_vblank"), rtgVBlankConfigValue(rtgRefreshRate->currentText()));
        settings.insert(QStringLiteral("gfx_backbuffers_rtg"),
            rtgBuffers->currentText() == QStringLiteral("Triple") ? QStringLiteral("3") : QStringLiteral("2"));
        settings.insert(QStringLiteral("rtg_modes"), QStringLiteral("0x%1").arg(rtgModeMask(), 0, 16));
        const QString cpuBoardConfig = selectedCpuBoardConfigValue();
        if (cpuBoardConfig.isEmpty()) {
            settings.insert(QStringLiteral("cpuboard_type"), QStringLiteral("none"));
            settings.insert(QStringLiteral("cpuboardmem1_size"), QStringLiteral("0"));
            settings.insert(QStringLiteral("cpuboardmem2_size"), QStringLiteral("0"));
        } else {
            settings.insert(QStringLiteral("cpuboard_type"), cpuBoardConfig);
            settings.insert(QStringLiteral("cpuboardmem1_size"), QString::number(megabytesFromText(cpuBoardMem->currentText())));
            settings.insert(QStringLiteral("cpuboardmem2_size"), QStringLiteral("0"));
            const WinUaeQtCpuBoardCatalogItem *cpuBoardChoice = cpuBoardSubtypeChoiceByConfig(boardCatalog, cpuBoardConfig);
            if (cpuBoardChoice && cpuBoardChoice->ppc) {
                settings.insert(QStringLiteral("ppc_model"), QStringLiteral("manual"));
            }
            const QString cpuBoardRomPath = cpuBoardRom->currentText().trimmed();
            if (!cpuBoardRomPath.isEmpty()) {
                settings.insert(QStringLiteral("cpuboard_rom_file"), cpuBoardRomPath);
            }
            if (!cpuBoardSettingsRaw.trimmed().isEmpty()) {
                settings.insert(QStringLiteral("cpuboard_settings"), cpuBoardSettingsRaw.trimmed());
            }
        }
        settings.insert(QStringLiteral("bsdsocket_emu"), expansionBsdsocket->isEnabled() && expansionBsdsocket->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("scsi"), expansionScsiDevice->isEnabled() && expansionScsiDevice->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("uaescsimode"), configChoiceValue(scsiModeChoices, int(sizeof(scsiModeChoices) / sizeof(scsiModeChoices[0])), miscScsiMode->currentText()));
        settings.insert(QStringLiteral("sana2"), expansionSana2->isEnabled() && expansionSana2->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        const int displayIndex = qMax(0, hostDisplay->currentIndex());
        settings.insert(QStringLiteral("gfx_display"), QString::number(displayIndex));
        settings.insert(QStringLiteral("gfx_display_rtg"), QString::number(displayIndex));
        const QString displayName = hostDisplay->currentData().toString();
        const QString displayFriendly = hostDisplay->currentData(Qt::UserRole + 1).toString();
        if (!displayName.isEmpty()) {
            settings.insert(QStringLiteral("gfx_display_name"), displayName);
            settings.insert(QStringLiteral("gfx_display_name_rtg"), displayName);
        }
        if (!displayFriendly.isEmpty()) {
            settings.insert(QStringLiteral("gfx_display_friendlyname"), displayFriendly);
            settings.insert(QStringLiteral("gfx_display_friendlyname_rtg"), displayFriendly);
        }
        if (!windowWidth->text().isEmpty()) {
            settings.insert(QStringLiteral("gfx_width_windowed"), windowWidth->text());
        }
        if (!windowHeight->text().isEmpty()) {
            settings.insert(QStringLiteral("gfx_height_windowed"), windowHeight->text());
        }
        const QString fullscreenText = fullscreenResolution->currentText();
        if (fullscreenText == QStringLiteral("Native")) {
            settings.insert(QStringLiteral("gfx_width_fullscreen"), QStringLiteral("native"));
            settings.insert(QStringLiteral("gfx_height_fullscreen"), QStringLiteral("native"));
        } else {
            const QStringList parts = fullscreenText.split(QLatin1Char('x'));
            if (parts.size() == 2) {
                settings.insert(QStringLiteral("gfx_width_fullscreen"), parts.value(0));
                settings.insert(QStringLiteral("gfx_height_fullscreen"), parts.value(1));
            }
        }
        if (displayRefreshRate->currentText() == QStringLiteral("Default")) {
            settings.insert(QStringLiteral("gfx_refreshrate"), QStringLiteral("0"));
        } else {
            settings.insert(QStringLiteral("gfx_refreshrate"), displayRefreshRate->currentText());
        }
        settings.insert(QStringLiteral("gfx_backbuffers"), displayBufferCount->currentText() == QStringLiteral("Triple") ? QStringLiteral("3") : QStringLiteral("2"));
        settings.insert(QStringLiteral("gfx_resize_windowed"), windowResize->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("gfx_fullscreen_amiga"), fullscreenModeConfigValue(nativeMode->currentText()));
        settings.insert(QStringLiteral("gfx_fullscreen_picasso"), fullscreenModeConfigValue(rtgMode->currentText()));
        settings.insert(QStringLiteral("gfx_vsync"), nativeVsyncConfigValue(nativeVsync->currentText()));
        settings.insert(QStringLiteral("gfx_vsyncmode"), nativeVsyncModeConfigValue(nativeVsync->currentText()));
        settings.insert(QStringLiteral("gfx_vsync_picasso"), rtgVsync->currentText() == QStringLiteral("-") ? QStringLiteral("false") : QStringLiteral("true"));
        settings.insert(QStringLiteral("gfx_vsyncmode_picasso"), rtgVsync->currentText() == QStringLiteral("-") ? QStringLiteral("normal") : QStringLiteral("busywait"));
        settings.insert(QStringLiteral("gfx_frame_slices"), nativeFrameSlices->currentText());
        settings.insert(QStringLiteral("gfx_resolution"), displayResolution->currentText());
        settings.insert(QStringLiteral("gfx_overscanmode"), overscanConfigValue(displayOverscan->currentText()));
        settings.insert(QStringLiteral("gfx_autoresolution"), QString::number(autoResolutionValue(displayAutoResolution->currentText())));
        settings.insert(QStringLiteral("gfx_framerate"), QString::number(displayFrameRate->value()));
        settings.insert(QStringLiteral("gfx_linemode"), lineModeConfigValue(displayLineModeButtons->checkedId(), displayInterlacedLineModeButtons->checkedId()));
        settings.insert(QStringLiteral("gfx_center_horizontal"), displayCenterHorizontal->isChecked() ? QStringLiteral("simple") : QStringLiteral("none"));
        settings.insert(QStringLiteral("gfx_center_vertical"), displayCenterVertical->isChecked() ? QStringLiteral("simple") : QStringLiteral("none"));
        settings.insert(QStringLiteral("gfx_flickerfixer"), displayFlickerFixer->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("gfx_lores_mode"), displayLoresSmoothed->isChecked() ? QStringLiteral("filtered") : QStringLiteral("normal"));
        settings.insert(QStringLiteral("gfx_blacker_than_black"), displayBlackerThanBlack->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("gfx_monochrome"), displayMonochrome->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("gfx_autoresolution_vga"), displayAutoResolutionVga->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("gfx_monitorblankdelay"), displayResyncBlank->isChecked() ? QStringLiteral("1000") : QStringLiteral("0"));
        settings.insert(QStringLiteral("gfx_keep_aspect"), displayKeepAspect->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("gfx_ntscpixels"), filterNtscPixels->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        const bool shaderPipeline = unixShaderPipelineAvailable();
        for (int i = 0; i < 3; i++) {
            const WinUaeQtFilterState state = filterStateFromUi(i);
            settings.insert(filterKey(QStringLiteral("gfx_filter_mode"), i), shaderPipeline ? state.modeH : QStringLiteral("1x"));
            settings.insert(filterKey(QStringLiteral("gfx_filter_mode2"), i), shaderPipeline ? state.modeV : QStringLiteral("-"));
            settings.insert(filterKey(QStringLiteral("gfx_filter_horiz_zoomf"), i), QString::number(state.horizZoom, 'f', 1));
            settings.insert(filterKey(QStringLiteral("gfx_filter_vert_zoomf"), i), QString::number(state.vertZoom, 'f', 1));
            settings.insert(filterKey(QStringLiteral("gfx_filter_horiz_zoom_multf"), i), QString::number(state.horizZoomMult, 'f', 2));
            settings.insert(filterKey(QStringLiteral("gfx_filter_vert_zoom_multf"), i), QString::number(state.vertZoomMult, 'f', 2));
            settings.insert(filterKey(QStringLiteral("gfx_filter_horiz_offsetf"), i), QString::number(state.horizOffset, 'f', 1));
            settings.insert(filterKey(QStringLiteral("gfx_filter_vert_offsetf"), i), QString::number(state.vertOffset, 'f', 1));
            settings.insert(filterKey(QStringLiteral("gfx_filter_keep_aspect"), i), state.keepAspect);
            settings.insert(filterKey(QStringLiteral("gfx_filter_keep_autoscale_aspect"), i), state.keepAutoscaleAspect ? QStringLiteral("1") : QStringLiteral("0"));
            settings.insert(filterKey(QStringLiteral("gfx_filter_autoscale"), i), i == 1 && !isRtgAutoscaleValue(state.autoscale) ? QStringLiteral("resize") : state.autoscale);
            settings.insert(filterKey(QStringLiteral("gfx_filter_autoscale_limit"), i), state.integerLimit);
            settings.insert(filterKey(QStringLiteral("gfx_filter_luminance"), i), QString::number(shaderPipeline ? state.luminance : 0));
            settings.insert(filterKey(QStringLiteral("gfx_filter_contrast"), i), QString::number(shaderPipeline ? state.contrast : 0));
            settings.insert(filterKey(QStringLiteral("gfx_filter_saturation"), i), QString::number(shaderPipeline ? state.saturation : 0));
            settings.insert(filterKey(QStringLiteral("gfx_filter_gamma"), i), QString::number(shaderPipeline ? state.gamma : 0));
            settings.insert(filterKey(QStringLiteral("gfx_filter_blur"), i), QString::number(shaderPipeline ? state.blur : 0));
            settings.insert(filterKey(QStringLiteral("gfx_filter_noise"), i), QString::number(shaderPipeline ? state.noise : 0));
            settings.insert(filterKey(QStringLiteral("gfx_filter_bilinear"), i), state.bilinear ? QStringLiteral("1") : QStringLiteral("0"));
            settings.insert(filterKey(QStringLiteral("gfx_filter_scanlines"), i), QString::number(state.scanlines));
            settings.insert(filterKey(QStringLiteral("gfx_filter_scanlinelevel"), i), QString::number(state.scanlineLevel));
            settings.insert(filterKey(QStringLiteral("gfx_filter_scanlineoffset"), i), QString::number(state.scanlineOffset));
        }
        settings.insert(QStringLiteral("gfx_filter_enable_lace"), filterStateFromUi(2).enable ? QStringLiteral("1") : QStringLiteral("0"));
        for (int i = 0; i < MaxCdSlots; i++) {
            const QString value = cdSlotConfigValue(cdSlotState(i));
            /* Slot 0 is always written, empty included, so ejecting the CD
             * in the runtime GUI reaches the core. */
            if (!value.isEmpty() || i == 0) {
                settings.insert(QStringLiteral("cdimage%1").arg(i), value);
            }
        }
        settings.insert(QStringLiteral("cd_speed"), cdSpeedTurbo->isChecked() ? QStringLiteral("0") : QStringLiteral("100"));
        settings.insert(QStringLiteral("filesys_no_fsdb"), filesysNoFsdb->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("filesys_max_size"), filesysLimitSize->isChecked() ? QString::number(950 * 1024) : QStringLiteral("0"));
        const int replaySeconds = stateReplayRate->currentText().trimmed() == QStringLiteral("-") ? -1 : stateReplayRate->currentText().toInt();
        settings.insert(QStringLiteral("state_replay_rate"), QString::number(replaySeconds > 0 ? replaySeconds * 50 : -1));
        settings.insert(QStringLiteral("state_replay_buffers"), QString::number(qMax(1, stateReplayBuffers->currentText().toInt())));
        settings.insert(QStringLiteral("state_replay_autoplay"), stateReplayAutoplay->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        for (int i = 0; i < 4; i++) {
            settings.insert(QStringLiteral("joyport%1").arg(i), joyportDeviceConfigValue(portDevice[i]->currentText()));
        }
        for (int i = 0; i < MaxJoyportCustomSlots; i++) {
            if (!joyportCustom[i].trimmed().isEmpty()) {
                settings.insert(QStringLiteral("joyportcustom%1").arg(i), joyportCustom[i].trimmed());
            }
        }
        for (int i = 0; i < 2; i++) {
            settings.insert(QStringLiteral("joyport%1autofire").arg(i), autofireConfigValue(portAutofire[i]->currentText()));
            const QString mode = joyportModeConfigValue(portMode[i]->currentText());
            if (!mode.isEmpty()) {
                settings.insert(QStringLiteral("joyport%1mode").arg(i), mode);
            }
        }
        const int inputConfig = inputType->currentText() == QStringLiteral("Game Ports")
            ? 0
            : qBound(1, inputType->currentIndex() + 1, CustomInputConfigSlots);
        settings.insert(QStringLiteral("input.config"), QString::number(inputConfig));
        settings.insert(QStringLiteral("input.joystick_deadzone"), QString::number(inputDeadzone->value()));
        settings.insert(QStringLiteral("input.joymouse_deadzone"), QString::number(inputDeadzone->value()));
        settings.insert(QStringLiteral("input.autofire_speed"), QString::number(inputAutofireRate->value()));
        settings.insert(QStringLiteral("input.joymouse_speed_digital"), QString::number(inputJoyMouseDigital->value()));
        settings.insert(QStringLiteral("input.joymouse_speed_analog"), QString::number(inputJoyMouseAnalog->value()));
        settings.insert(QStringLiteral("input.mouse_speed"), QString::number(mouseSpeed->value()));
        settings.insert(QStringLiteral("input.autoswitch"), portAutoswitch->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        const int untrapMode = mouseUntrapMode->currentIndex();
        settings.insert(QStringLiteral("middle_mouse"), (untrapMode == 1 || untrapMode == 3) ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("magic_mouse"), (untrapMode == 2 || untrapMode == 3) ? QStringLiteral("true") : QStringLiteral("false"));
        settings.insert(QStringLiteral("magic_mousecursor"), magicMouseCursorConfigValue(magicMouseCursor->currentText()));
        QString absoluteMouse = QStringLiteral("none");
        if (unixTabletBackendAvailable() && tabletMode->currentText() == QStringLiteral("Tablet emulation")) {
            absoluteMouse = QStringLiteral("tablet");
        } else if (virtualMouseDriver->isChecked()) {
            absoluteMouse = QStringLiteral("mousehack");
        }
        settings.insert(QStringLiteral("absolute_mouse"), absoluteMouse);
        settings.insert(QStringLiteral("tablet_library"), tabletLibrary->isChecked() ? QStringLiteral("true") : QStringLiteral("false"));
        for (const QString &key : inputOwnedMappingKeys) {
            const QString value = inputMappingSettings.value(key).trimmed();
            if (!value.isEmpty()) {
                settings.insert(key, value);
            }
        }
        mergeHardwareOrderSettings(settings);
        return settings;
    }

    QStringList uiOwnedKeys() const
    {
        QStringList keys = {
            QStringLiteral("config_description"),
            QStringLiteral("quickstart"),
            QStringLiteral("unix.rom_path"),
            QStringLiteral("unix.config_path"),
            QStringLiteral("unix.ui.config_path"),
            QStringLiteral("unix.nvram_path"),
            QStringLiteral("unix.ui.nvram_path"),
            QStringLiteral("unix.screenshot_path"),
            QStringLiteral("unix.ui.screenshot_path"),
            QStringLiteral("statefile_path"),
            QStringLiteral("unix.video_path"),
            QStringLiteral("unix.ui.video_path"),
            QStringLiteral("unix.saveimage_path"),
            QStringLiteral("unix.ui.saveimage_path"),
            QStringLiteral("unix.rip_path"),
            QStringLiteral("unix.ripper_path"),
            QStringLiteral("unix.ui.rip_path"),
            QStringLiteral("unix.data_path"),
            QStringLiteral("unix.ui.data_path"),
            QStringLiteral("unix.ui.path_mode"),
            QStringLiteral("unix.ui.recursive_roms"),
            QStringLiteral("unix.ui.cache_configurations"),
            QStringLiteral("unix.ui.cache_boxart"),
            QStringLiteral("unix.ui.saveimage_original_path"),
            QStringLiteral("unix.ui.relative_paths"),
            QStringLiteral("unix.ui.portable_mode"),
            QStringLiteral("unix.ui.full_logging"),
            QStringLiteral("unix.ui.log_window"),
            QStringLiteral("unix.ui.gui_scale"),
            QStringLiteral("unix.ui.gui_resize"),
            QStringLiteral("unix.ui.gui_fullscreen"),
            QStringLiteral("unix.ui.gui_dark_mode"),
            QStringLiteral("unix.ui.gui_font"),
            QStringLiteral("unix.output_file"),
            QStringLiteral("unix.output_audio_codec"),
            QStringLiteral("unix.output_video_codec"),
            QStringLiteral("unix.output_audio"),
            QStringLiteral("unix.output_video"),
            QStringLiteral("unix.output_enabled"),
            QStringLiteral("unix.output_frame_limiter_disabled"),
            QStringLiteral("unix.output_original_size"),
            QStringLiteral("unix.output_no_sound"),
            QStringLiteral("unix.output_no_sound_sync"),
            QStringLiteral("unix.screenshot_original_size"),
            QStringLiteral("unix.screenshot_paletted"),
            QStringLiteral("unix.screenshot_clip"),
            QStringLiteral("unix.screenshot_auto"),
            QStringLiteral("unix.ui.output_file"),
            QStringLiteral("unix.ui.output_audio_codec"),
            QStringLiteral("unix.ui.output_video_codec"),
            QStringLiteral("unix.ui.output_audio"),
            QStringLiteral("unix.ui.output_video"),
            QStringLiteral("unix.ui.output_enabled"),
            QStringLiteral("unix.ui.output_frame_limiter_disabled"),
            QStringLiteral("unix.ui.output_original_size"),
            QStringLiteral("unix.ui.output_no_sound"),
            QStringLiteral("unix.ui.output_no_sound_sync"),
            QStringLiteral("unix.ui.screenshot_original_size"),
            QStringLiteral("unix.ui.screenshot_paletted"),
            QStringLiteral("unix.ui.screenshot_clip"),
            QStringLiteral("unix.ui.screenshot_auto"),
            QStringLiteral("kickstart_rom_file"),
            QStringLiteral("kickstart_ext_rom_file"),
            QStringLiteral("cart_file"),
            QStringLiteral("flash_file"),
            QStringLiteral("rtc_file"),
            QStringLiteral("maprom"),
            QStringLiteral("kickshifter"),
            QStringLiteral("boot_rom_uae"),
            QStringLiteral("uaeboard"),
            QStringLiteral("romboard_options"),
            QStringLiteral("romboard2_options"),
            QStringLiteral("romboard3_options"),
            QStringLiteral("romboard4_options"),
            QStringLiteral("board_custom_order"),
            QStringLiteral("floppy0"),
            QStringLiteral("floppy1"),
            QStringLiteral("floppy2"),
            QStringLiteral("floppy3"),
            QStringLiteral("floppy0type"),
            QStringLiteral("floppy1type"),
            QStringLiteral("floppy2type"),
            QStringLiteral("floppy3type"),
            QStringLiteral("floppy0wp"),
            QStringLiteral("floppy1wp"),
            QStringLiteral("floppy2wp"),
            QStringLiteral("floppy3wp"),
            QStringLiteral("nr_floppies"),
            QStringLiteral("floppy_speed"),
            QStringLiteral("diskimage0"),
            QStringLiteral("diskimage1"),
            QStringLiteral("diskimage2"),
            QStringLiteral("diskimage3"),
            QStringLiteral("diskimage4"),
            QStringLiteral("diskimage5"),
            QStringLiteral("diskimage6"),
            QStringLiteral("diskimage7"),
            QStringLiteral("diskimage8"),
            QStringLiteral("diskimage9"),
            QStringLiteral("diskimage10"),
            QStringLiteral("diskimage11"),
            QStringLiteral("diskimage12"),
            QStringLiteral("diskimage13"),
            QStringLiteral("diskimage14"),
            QStringLiteral("diskimage15"),
            QStringLiteral("diskimage16"),
            QStringLiteral("diskimage17"),
            QStringLiteral("diskimage18"),
            QStringLiteral("diskimage19"),
            QStringLiteral("chipset"),
            QStringLiteral("chipset_compatible"),
            QStringLiteral("ntsc"),
            QStringLiteral("immediate_blits"),
            QStringLiteral("waiting_blits"),
            QStringLiteral("collision_level"),
            QStringLiteral("display_optimizations"),
            QStringLiteral("hvcsync"),
            QStringLiteral("genlock"),
            QStringLiteral("genlockmode"),
            QStringLiteral("genlock_mix"),
            QStringLiteral("genlock_alpha"),
            QStringLiteral("genlock_aspect"),
            QStringLiteral("genlock_image"),
            QStringLiteral("genlock_video"),
            QStringLiteral("keyboard_connected"),
            QStringLiteral("keyboard_type"),
            QStringLiteral("keyboard_nkro"),
            QStringLiteral("ciaatod"),
            QStringLiteral("rtc"),
            QStringLiteral("chipset_rtc_adjust"),
            QStringLiteral("cia_overlay"),
            QStringLiteral("cd32cd"),
            QStringLiteral("cdtvcd"),
            QStringLiteral("ksmirror_e0"),
            QStringLiteral("df0idhw"),
            QStringLiteral("resetwarning"),
            QStringLiteral("cia_todbug"),
            QStringLiteral("1mchipjumper"),
            QStringLiteral("toshiba_gary"),
            QStringLiteral("a1000ram"),
            QStringLiteral("cd32c2p"),
            QStringLiteral("cdtvram"),
            QStringLiteral("ksmirror_a8"),
            QStringLiteral("z3_autoconfig"),
            QStringLiteral("rom_is_slow"),
            QStringLiteral("cd32nvram"),
            QStringLiteral("cdtv-cr"),
            QStringLiteral("pcmcia"),
            QStringLiteral("color_burst"),
            QStringLiteral("memory_pattern"),
            QStringLiteral("ide"),
            QStringLiteral("scsi_a3000"),
            QStringLiteral("scsi_a4000t"),
            QStringLiteral("ciaa_type"),
            QStringLiteral("ciab_type"),
            QStringLiteral("unmapped_address_space"),
            QStringLiteral("eclocksync"),
            QStringLiteral("fatgary"),
            QStringLiteral("ramsey"),
            QStringLiteral("agnusmodel"),
            QStringLiteral("agnussize"),
            QStringLiteral("denisemodel"),
            QStringLiteral("cycle_exact"),
            QStringLiteral("cpu_cycle_exact"),
            QStringLiteral("cpu_memory_cycle_exact"),
            QStringLiteral("blitter_cycle_exact"),
            QStringLiteral("cpu_model"),
            QStringLiteral("cpu_speed"),
            QStringLiteral("cpu_throttle"),
            QStringLiteral("cpu_compatible"),
            QStringLiteral("fpu_model"),
            QStringLiteral("cpu_24bit_addressing"),
            QStringLiteral("mmu_model"),
            QStringLiteral("cpu_no_unimplemented"),
            QStringLiteral("fpu_no_unimplemented"),
            QStringLiteral("fpu_strict"),
            QStringLiteral("fpu_softfloat"),
            QStringLiteral("fpu_msvc_long_double"),
            QStringLiteral("cpu_multiplier"),
            QStringLiteral("cpu_frequency"),
            QStringLiteral("chipmem_size"),
            QStringLiteral("fastmem_size"),
            QStringLiteral("bogomem_size"),
            QStringLiteral("z3mem_size"),
            QStringLiteral("megachipmem_size"),
            QStringLiteral("mbresmem_size"),
            QStringLiteral("z3mapping"),
            QStringLiteral("cachesize"),
            QStringLiteral("compfpu"),
            QStringLiteral("comp_constjump"),
            QStringLiteral("comp_flushmode"),
            QStringLiteral("comp_nf"),
            QStringLiteral("comp_catchfault"),
            QStringLiteral("comp_trustbyte"),
            QStringLiteral("comp_trustword"),
            QStringLiteral("comp_trustlong"),
            QStringLiteral("comp_trustnaddr"),
            QStringLiteral("soundcard"),
            QStringLiteral("soundcardname"),
            QStringLiteral("unix.soundcard"),
            QStringLiteral("unix.soundcardname"),
            QStringLiteral("sound_output"),
            QStringLiteral("sound_auto"),
            QStringLiteral("sound_volume"),
            QStringLiteral("sound_volume_paula"),
            QStringLiteral("sound_volume_cd"),
            QStringLiteral("sound_volume_ahi"),
            QStringLiteral("sound_volume_midi"),
            QStringLiteral("sound_volume_genlock"),
            QStringLiteral("sound_max_buff"),
            QStringLiteral("sound_channels"),
            QStringLiteral("sound_stereo_separation"),
            QStringLiteral("sound_stereo_mixing_delay"),
            QStringLiteral("sound_frequency"),
            QStringLiteral("sound_interpol"),
            QStringLiteral("sound_filter"),
            QStringLiteral("sound_filter_type"),
            QStringLiteral("sound_stereo_swap_paula"),
            QStringLiteral("sound_stereo_swap_ahi"),
            QStringLiteral("floppy0sound"),
            QStringLiteral("floppy1sound"),
            QStringLiteral("floppy2sound"),
            QStringLiteral("floppy3sound"),
            QStringLiteral("floppy0soundvolume_empty"),
            QStringLiteral("floppy1soundvolume_empty"),
            QStringLiteral("floppy2soundvolume_empty"),
            QStringLiteral("floppy3soundvolume_empty"),
            QStringLiteral("floppy0soundvolume_disk"),
            QStringLiteral("floppy1soundvolume_disk"),
            QStringLiteral("floppy2soundvolume_disk"),
            QStringLiteral("floppy3soundvolume_disk"),
            QStringLiteral("parallel_matrix_emulation"),
            QStringLiteral("parallel_postscript_detection"),
            QStringLiteral("parallel_postscript_emulation"),
            QStringLiteral("parallel_autoflush"),
            QStringLiteral("ghostscript_parameters"),
            QStringLiteral("samplersoundcard"),
            QStringLiteral("samplersoundcardname"),
            QStringLiteral("sampler_soundcard"),
            QStringLiteral("sampler_soundcardname"),
            QStringLiteral("unix.samplersoundcard"),
            QStringLiteral("unix.samplersoundcardname"),
            QStringLiteral("unix.sampler_soundcard"),
            QStringLiteral("unix.sampler_soundcardname"),
            QStringLiteral("sampler_stereo"),
            QStringLiteral("midi_device"),
            QStringLiteral("midiout_device"),
            QStringLiteral("midiin_device"),
            QStringLiteral("midiout_device_name"),
            QStringLiteral("midiin_device_name"),
            QStringLiteral("midirouter"),
            QStringLiteral("serial_port"),
            QStringLiteral("unix.serial_port"),
            QStringLiteral("serial_on_demand"),
            QStringLiteral("serial_hardware_ctsrts"),
            QStringLiteral("serial_status"),
            QStringLiteral("serial_ri"),
            QStringLiteral("serial_direct"),
            QStringLiteral("serial_translate"),
            QStringLiteral("uaeserial"),
            QStringLiteral("dongle"),
            QStringLiteral("use_gui"),
            QStringLiteral("synchronize_clock"),
            QStringLiteral("cpu_reset_pause"),
            QStringLiteral("rtg_nocustom"),
            QStringLiteral("clipboard_sharing"),
            QStringLiteral("native_code"),
            QStringLiteral("show_leds"),
            QStringLiteral("show_leds_rtg"),
            QStringLiteral("log_illegal_mem"),
            QStringLiteral("floppy_write_protect"),
            QStringLiteral("harddrive_write_protect"),
            QStringLiteral("uae_hide_autoconfig"),
            QStringLiteral("power_led_dim"),
            QStringLiteral("debug_mem"),
            QStringLiteral("cpu_halt_auto_reset"),
            QStringLiteral("scsidevice_disable"),
            QStringLiteral("warpboot"),
            QStringLiteral("statefile"),
            QStringLiteral("keyboard_leds"),
            QStringLiteral("active_priority"),
            QStringLiteral("activepriority"),
            QStringLiteral("active_not_captured_pause"),
            QStringLiteral("active_not_captured_nosound"),
            QStringLiteral("active_input"),
            QStringLiteral("inactive_priority"),
            QStringLiteral("inactive_pause"),
            QStringLiteral("inactive_nosound"),
            QStringLiteral("inactive_input"),
            QStringLiteral("iconified_priority"),
            QStringLiteral("iconified_pause"),
            QStringLiteral("iconified_nosound"),
            QStringLiteral("iconified_input"),
            QStringLiteral("gfxcard_size"),
            QStringLiteral("gfxcard_type"),
            QStringLiteral("gfxcard_options"),
            QStringLiteral("gfx_filter_autoscale_rtg"),
            QStringLiteral("gfx_backbuffers_rtg"),
            QStringLiteral("gfx_refreshrate_rtg"),
            QStringLiteral("unix.rtg_vblank"),
            QStringLiteral("rtg_vblank"),
            QStringLiteral("gfxcard_hardware_vblank"),
            QStringLiteral("gfxcard_hardware_sprite"),
            QStringLiteral("gfxcard_multithread"),
            QStringLiteral("rtg_modes"),
            QStringLiteral("gfx_filter_aspect_ratio_rtg"),
            QStringLiteral("cpuboard_type"),
            QStringLiteral("cpuboardmem1_size"),
            QStringLiteral("cpuboardmem2_size"),
            QStringLiteral("cpuboard_rom_file"),
            QStringLiteral("cpuboard_settings"),
            QStringLiteral("ppc_model"),
            QStringLiteral("bsdsocket_emu"),
            QStringLiteral("scsi"),
            QStringLiteral("uaescsimode"),
            QStringLiteral("unix.uaescsimode"),
            QStringLiteral("sana2"),
            QStringLiteral("gfx_display"),
            QStringLiteral("gfx_display_rtg"),
            QStringLiteral("gfx_display_name"),
            QStringLiteral("gfx_display_name_rtg"),
            QStringLiteral("gfx_display_friendlyname"),
            QStringLiteral("gfx_display_friendlyname_rtg"),
            QStringLiteral("gfx_width_windowed"),
            QStringLiteral("gfx_height_windowed"),
            QStringLiteral("gfx_width_fullscreen"),
            QStringLiteral("gfx_height_fullscreen"),
            QStringLiteral("gfx_refreshrate"),
            QStringLiteral("gfx_backbuffers"),
            QStringLiteral("gfx_resize_windowed"),
            QStringLiteral("gfx_fullscreen_amiga"),
            QStringLiteral("gfx_fullscreen_picasso"),
            QStringLiteral("gfx_vsync"),
            QStringLiteral("gfx_vsyncmode"),
            QStringLiteral("gfx_vsync_picasso"),
            QStringLiteral("gfx_vsyncmode_picasso"),
            QStringLiteral("gfx_frame_slices"),
            QStringLiteral("gfx_resolution"),
            QStringLiteral("gfx_overscanmode"),
            QStringLiteral("gfx_autoresolution"),
            QStringLiteral("gfx_framerate"),
            QStringLiteral("gfx_linemode"),
            QStringLiteral("gfx_center_horizontal"),
            QStringLiteral("gfx_center_vertical"),
            QStringLiteral("gfx_flickerfixer"),
            QStringLiteral("gfx_lores_mode"),
            QStringLiteral("gfx_blacker_than_black"),
            QStringLiteral("gfx_monochrome"),
            QStringLiteral("gfx_autoresolution_vga"),
            QStringLiteral("gfx_monitorblankdelay"),
            QStringLiteral("gfx_keep_aspect"),
            QStringLiteral("gfx_ntscpixels"),
            QStringLiteral("gfx_filter_mode"),
            QStringLiteral("gfx_filter_mode2"),
            QStringLiteral("gfx_filter_horiz_zoomf"),
            QStringLiteral("gfx_filter_vert_zoomf"),
            QStringLiteral("gfx_filter_horiz_zoom_multf"),
            QStringLiteral("gfx_filter_vert_zoom_multf"),
            QStringLiteral("gfx_filter_horiz_offsetf"),
            QStringLiteral("gfx_filter_vert_offsetf"),
            QStringLiteral("gfx_filter_keep_aspect"),
            QStringLiteral("gfx_filter_keep_autoscale_aspect"),
            QStringLiteral("gfx_filter_autoscale"),
            QStringLiteral("gfx_filter_autoscale_limit"),
            QStringLiteral("gfx_filter_luminance"),
            QStringLiteral("gfx_filter_contrast"),
            QStringLiteral("gfx_filter_saturation"),
            QStringLiteral("gfx_filter_gamma"),
            QStringLiteral("gfx_filter_blur"),
            QStringLiteral("gfx_filter_noise"),
            QStringLiteral("gfx_filter_bilinear"),
            QStringLiteral("gfx_filter_scanlines"),
            QStringLiteral("gfx_filter_scanlinelevel"),
            QStringLiteral("gfx_filter_scanlineoffset"),
            QStringLiteral("gfx_filter_mode_rtg"),
            QStringLiteral("gfx_filter_mode2_rtg"),
            QStringLiteral("gfx_filter_horiz_zoomf_rtg"),
            QStringLiteral("gfx_filter_vert_zoomf_rtg"),
            QStringLiteral("gfx_filter_horiz_zoom_multf_rtg"),
            QStringLiteral("gfx_filter_vert_zoom_multf_rtg"),
            QStringLiteral("gfx_filter_horiz_offsetf_rtg"),
            QStringLiteral("gfx_filter_vert_offsetf_rtg"),
            QStringLiteral("gfx_filter_keep_aspect_rtg"),
            QStringLiteral("gfx_filter_keep_autoscale_aspect_rtg"),
            QStringLiteral("gfx_filter_autoscale_limit_rtg"),
            QStringLiteral("gfx_filter_luminance_rtg"),
            QStringLiteral("gfx_filter_contrast_rtg"),
            QStringLiteral("gfx_filter_saturation_rtg"),
            QStringLiteral("gfx_filter_gamma_rtg"),
            QStringLiteral("gfx_filter_blur_rtg"),
            QStringLiteral("gfx_filter_noise_rtg"),
            QStringLiteral("gfx_filter_bilinear_rtg"),
            QStringLiteral("gfx_filter_scanlines_rtg"),
            QStringLiteral("gfx_filter_scanlinelevel_rtg"),
            QStringLiteral("gfx_filter_scanlineoffset_rtg"),
            QStringLiteral("gfx_filter_mode_lace"),
            QStringLiteral("gfx_filter_mode2_lace"),
            QStringLiteral("gfx_filter_horiz_zoomf_lace"),
            QStringLiteral("gfx_filter_vert_zoomf_lace"),
            QStringLiteral("gfx_filter_horiz_zoom_multf_lace"),
            QStringLiteral("gfx_filter_vert_zoom_multf_lace"),
            QStringLiteral("gfx_filter_horiz_offsetf_lace"),
            QStringLiteral("gfx_filter_vert_offsetf_lace"),
            QStringLiteral("gfx_filter_keep_aspect_lace"),
            QStringLiteral("gfx_filter_keep_autoscale_aspect_lace"),
            QStringLiteral("gfx_filter_autoscale_lace"),
            QStringLiteral("gfx_filter_autoscale_limit_lace"),
            QStringLiteral("gfx_filter_luminance_lace"),
            QStringLiteral("gfx_filter_contrast_lace"),
            QStringLiteral("gfx_filter_saturation_lace"),
            QStringLiteral("gfx_filter_gamma_lace"),
            QStringLiteral("gfx_filter_blur_lace"),
            QStringLiteral("gfx_filter_noise_lace"),
            QStringLiteral("gfx_filter_bilinear_lace"),
            QStringLiteral("gfx_filter_scanlines_lace"),
            QStringLiteral("gfx_filter_scanlinelevel_lace"),
            QStringLiteral("gfx_filter_scanlineoffset_lace"),
            QStringLiteral("gfx_filter_enable_lace"),
            QStringLiteral("cdimage0"),
            QStringLiteral("cdimage1"),
            QStringLiteral("cdimage2"),
            QStringLiteral("cdimage3"),
            QStringLiteral("cdimage4"),
            QStringLiteral("cdimage5"),
            QStringLiteral("cdimage6"),
            QStringLiteral("cdimage7"),
            QStringLiteral("cd_speed"),
            QStringLiteral("filesys_no_fsdb"),
            QStringLiteral("filesys_max_size"),
            QStringLiteral("state_replay_rate"),
            QStringLiteral("state_replay_buffers"),
            QStringLiteral("state_replay_autoplay"),
            QStringLiteral("joyport0"),
            QStringLiteral("joyport1"),
            QStringLiteral("joyport2"),
            QStringLiteral("joyport3"),
            QStringLiteral("joyportcustom0"),
            QStringLiteral("joyportcustom1"),
            QStringLiteral("joyportcustom2"),
            QStringLiteral("joyportcustom3"),
            QStringLiteral("joyportcustom4"),
            QStringLiteral("joyportcustom5"),
            QStringLiteral("joyport0autofire"),
            QStringLiteral("joyport1autofire"),
            QStringLiteral("joyport0mode"),
            QStringLiteral("joyport1mode"),
            QStringLiteral("input.config"),
            QStringLiteral("input.joystick_deadzone"),
            QStringLiteral("input.joymouse_deadzone"),
            QStringLiteral("input.autofire_speed"),
            QStringLiteral("input.joymouse_speed_digital"),
            QStringLiteral("input.joymouse_speed_analog"),
            QStringLiteral("input.mouse_speed"),
            QStringLiteral("input.autoswitch"),
            QStringLiteral("middle_mouse"),
            QStringLiteral("magic_mouse"),
            QStringLiteral("magic_mousecursor"),
            QStringLiteral("absolute_mouse"),
            QStringLiteral("tablet_library")
        };
        keys.append(hardwareOrderOwnedKeys);
        keys.append(inputOwnedMappingKeys);
        return keys;
    }

    QStringList uiOwnedMountKeys() const
    {
        return {
            QStringLiteral("filesystem2"),
            QStringLiteral("hardfile2"),
            QStringLiteral("uaehf0"),
            QStringLiteral("uaehf1"),
            QStringLiteral("uaehf2"),
            QStringLiteral("uaehf3"),
            QStringLiteral("uaehf4"),
            QStringLiteral("uaehf5"),
            QStringLiteral("uaehf6"),
            QStringLiteral("uaehf7")
        };
    }

    WinUaeQtConfig mergedConfig() const
    {
        WinUaeQtConfig config = loadedConfig;
        const QStringList expansionKeys = expansionBoardOwnedKeys();
        const QStringList mountKeys = uiOwnedMountKeys();
        // Quickstart presets can clear board ROMs while parsing, so rewrite
        // expansion boards after normal settings and before controller mounts.
        config.applySettings(WinUaeQtConfig::Settings(), expansionKeys);
        config.applyRepeatedSettings(WinUaeQtConfig::OrderedSettings(), mountKeys);
        config.applySettings(currentSettings(), uiOwnedKeys());
        config.applySettings(currentExpansionBoardSettings(), expansionKeys);
        config.applyRepeatedSettings(currentMountSettings(), mountKeys);
        moveQuickstartBeforeOverrides(config, expansionKeys, mountKeys);
        return config;
    }

    void moveQuickstartBeforeOverrides(WinUaeQtConfig &config) const
    {
        moveQuickstartBeforeOverrides(config, expansionBoardOwnedKeys(), uiOwnedMountKeys());
    }

    void moveQuickstartBeforeOverrides(WinUaeQtConfig &config, const QStringList &expansionKeys, const QStringList &mountKeys) const
    {
        QStringList quickstartOverrideKeys = uiOwnedKeys();
        quickstartOverrideKeys.append(expansionKeys);
        quickstartOverrideKeys.append(mountKeys);
        quickstartOverrideKeys.removeAll(QStringLiteral("quickstart"));
        config.moveSettingBefore(QStringLiteral("quickstart"), quickstartOverrideKeys);
    }

    int enabledFloppyCount() const
    {
        int count = 0;
        for (int i = 0; i < 4; i++) {
            if (dfEnable[i]->isChecked() && floppyTypeConfigValue(dfType[i]->currentText()) >= 0) {
                count = i + 1;
            }
        }
        return qMax(1, count);
    }

    QString configurationDirectory() const
    {
        if (configsPath && !configsPath->text().trimmed().isEmpty()) {
            return expandedPathText(configsPath->text());
        }
        return fileDialogInitialPath(QString());
    }

    QString normalizedConfigName() const
    {
        QString name = configName ? configName->currentText().trimmed() : QString();
        if (name.endsWith(QStringLiteral(".uae"), Qt::CaseInsensitive)) {
            name.chop(4);
        }
        return name;
    }

    QString namedConfigPath() const
    {
        const QString name = normalizedConfigName();
        if (name.isEmpty()) {
            return QString();
        }
        return QDir(configurationDirectory()).filePath(name + QStringLiteral(".uae"));
    }

    QString selectedConfigPath() const
    {
        if (!configTree || !configTree->currentItem()) {
            return QString();
        }
        return configTree->currentItem()->data(0, Qt::UserRole).toString();
    }

    void refreshConfigList()
    {
        if (!configTree) {
            return;
        }
        const QString selectedPath = configPath ? expandedPathText(configPath->text()) : QString();
        const QString search = configSearch ? configSearch->text().trimmed() : QString();
        const QString filter = configFilter ? configFilter->currentText() : QStringLiteral("All configurations");
        QSignalBlocker blocker(configTree);
        configTree->clear();
        QTreeWidgetItem *root = new QTreeWidgetItem(configTree, QStringList(QStringLiteral("Configurations")));
        root->setIcon(0, resourceIcon(QStringLiteral("configfile.ico")));

        QDir dir(configurationDirectory());
        const QFileInfoList files = dir.entryInfoList({ QStringLiteral("*.uae") }, QDir::Files, QDir::Name | QDir::IgnoreCase);
        QTreeWidgetItem *selected = nullptr;
        for (const QFileInfo &info : files) {
            WinUaeQtConfig config;
            config.load(info.absoluteFilePath());
            const QString description = config.value(QStringLiteral("config_description"));
            const bool isHardware = configBoolValue(config.value(QStringLiteral("config_hardware")));
            const bool isHost = configBoolValue(config.value(QStringLiteral("config_host")));
            if (filter == QStringLiteral("Host") && !isHost) {
                continue;
            }
            if (filter == QStringLiteral("Hardware") && !isHardware) {
                continue;
            }
            if (!search.isEmpty()
                && !info.completeBaseName().contains(search, Qt::CaseInsensitive)
                && !description.contains(search, Qt::CaseInsensitive)) {
                continue;
            }
            QString displayName = info.completeBaseName();
            if (!description.isEmpty()) {
                displayName += QStringLiteral(" (%1)").arg(description);
            }
            QTreeWidgetItem *item = new QTreeWidgetItem(root, QStringList(displayName));
            item->setIcon(0, resourceIcon(QStringLiteral("configfile.ico")));
            item->setData(0, Qt::UserRole, info.absoluteFilePath());
            item->setData(0, Qt::UserRole + 1, description);
            item->setToolTip(0, description.isEmpty() ? info.absoluteFilePath() : description);
            if (info.absoluteFilePath() == selectedPath) {
                selected = item;
            }
        }
        root->setExpanded(true);
        if (selected) {
            configTree->setCurrentItem(selected);
        }
    }

    void selectConfigFromTree()
    {
        const QString path = selectedConfigPath();
        if (path.isEmpty()) {
            return;
        }
        configPath->setText(path);
        configName->setCurrentText(QFileInfo(path).completeBaseName());
        configDescription->setText(configTree->currentItem()->data(0, Qt::UserRole + 1).toString());
    }

    void loadSelectedConfig()
    {
        QString path = selectedConfigPath();
        if (path.isEmpty()) {
            path = !configPath->text().trimmed().isEmpty() ? configPath->text().trimmed() : namedConfigPath();
        }
        path = expandedPathText(path);
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Select a configuration to load."));
            return;
        }
        loadConfig(path);
    }

    void saveNamedConfig()
    {
        QString path = !configPath->text().trimmed().isEmpty() ? configPath->text().trimmed() : namedConfigPath();
        path = expandedPathText(path);
        if (path.isEmpty()) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Enter a configuration name before saving."));
            return;
        }
        QDir().mkpath(QFileInfo(path).absolutePath());
        saveConfig(path);
        refreshConfigList();
    }

    void deleteSelectedConfig()
    {
        QString path = selectedConfigPath();
        if (path.isEmpty()) {
            path = configPath->text().trimmed();
        }
        path = expandedPathText(path);
        if (path.isEmpty() || !QFileInfo::exists(path)) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Select a configuration to delete."));
            return;
        }
        const QString name = QFileInfo(path).fileName();
        if (QMessageBox::question(this, windowTitle(), QStringLiteral("Delete configuration %1?").arg(name)) != QMessageBox::Yes) {
            return;
        }
        if (!QFile::remove(path)) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("Could not delete %1").arg(path));
            return;
        }
        if (expandedPathText(configPath->text()) == path) {
            configPath->clear();
        }
        refreshConfigList();
        status->setText(QStringLiteral("Deleted %1").arg(path));
    }

    void loadConfigDialog()
    {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Load configuration"), configurationDirectory(), QStringLiteral("WinUAE configuration (*.uae);;All files (*)"));
        if (!path.isEmpty()) {
            loadConfig(path);
        }
    }

    void saveConfigDialog()
    {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save configuration"), configurationDirectory(), QStringLiteral("WinUAE configuration (*.uae);;All files (*)"));
        if (!path.isEmpty()) {
            saveConfig(path);
        }
    }

    void loadConfig(const QString &path)
    {
        const QString expandedPath = expandedPathText(path);
        WinUaeQtConfig config;
        QString error;
        if (!config.load(expandedPath, &error)) {
            QMessageBox::warning(this, windowTitle(), error);
            return;
        }
        if (mountedDrives) {
            mountedDrives->clear();
        }
        if (configDescription) {
            configDescription->clear();
        }
        for (int i = 0; i < MaxDiskSwapperSlots; i++) {
            setDiskSwapperPath(i, QString());
        }
        clearCdSlots();
        clearExpansionBoardStates();
        inputMappingSettings.clear();
        inputOwnedMappingKeys.clear();
        hardwareOrderOwnedKeys.clear();
        moveQuickstartBeforeOverrides(config);
        for (const WinUaeQtConfig::Setting &setting : config.orderedSettings()) {
            applySetting(setting.key, setting.value);
        }
        updateOutputControlState();
        updateMountButtons();
        refreshInputMappingList();
        loadedConfig = config;
        refreshHardwareInfoPage();
        configPath->setText(expandedPath);
        configName->setCurrentText(QFileInfo(expandedPath).completeBaseName());
        refreshConfigList();
        status->setText(QStringLiteral("Loaded %1").arg(expandedPath));
    }

    void applySetting(const QString &key, const QString &value)
    {
        trackHardwareOrderSetting(key, value);
        if (winUaeQtIsInputDeviceConfigKey(key)) {
            inputMappingSettings.insert(key, value);
        } else if (key == QStringLiteral("config_description")) {
            configDescription->setText(value);
        } else if (key == QStringLiteral("quickstart")) {
            const QStringList parts = value.split(QLatin1Char(','));
            const QuickstartModelChoice *choice = quickstartModelChoiceByConfigValue(parts.value(0).trimmed());
            if (choice) {
                bool ok = false;
                const int config = parts.value(1).toInt(&ok);
                {
                    const QSignalBlocker modelBlocker(quickModel);
                    quickModel->setCurrentText(QString::fromLatin1(choice->display));
                    refreshQuickstartConfigurationChoices(ok ? config : 0);
                }
                quickstartMode->setChecked(true);
                quickstartSetConfig->setVisible(false);
                applyQuickstartSelectionToUi();
            }
        } else if (key == QStringLiteral("unix.rom_path") || key == QStringLiteral("rom_path")) {
            romsPath->setText(value);
        } else if (key == QStringLiteral("unix.config_path") || key == QStringLiteral("unix.ui.config_path")) {
            configsPath->setText(value);
        } else if (key == QStringLiteral("unix.nvram_path") || key == QStringLiteral("unix.ui.nvram_path")) {
            nvramPath->setText(value);
        } else if (key == QStringLiteral("unix.screenshot_path") || key == QStringLiteral("unix.ui.screenshot_path")) {
            screenshotsPath->setText(value);
        } else if (key == QStringLiteral("statefile_path")) {
            stateFilesPath->setText(value);
        } else if (key == QStringLiteral("unix.video_path") || key == QStringLiteral("unix.ui.video_path")) {
            videosPath->setText(value);
        } else if (key == QStringLiteral("unix.saveimage_path") || key == QStringLiteral("unix.ui.saveimage_path")) {
            saveImagesPath->setText(value);
        } else if (key == QStringLiteral("unix.rip_path") || key == QStringLiteral("unix.ripper_path") || key == QStringLiteral("unix.ui.rip_path")) {
            ripsPath->setText(value);
        } else if (key == QStringLiteral("unix.data_path") || key == QStringLiteral("unix.ui.data_path")) {
            dataPath->setText(value);
        } else if (key == QStringLiteral("unix.ui.path_mode")) {
            pathDefaultType->setCurrentText(value);
        } else if (key == QStringLiteral("unix.ui.recursive_roms")) {
            recursiveRoms->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("unix.ui.cache_configurations")) {
            cacheConfigurations->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("unix.ui.cache_boxart")) {
            cacheBoxArt->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("unix.ui.saveimage_original_path")) {
            saveImageOriginalPath->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("unix.ui.relative_paths")) {
            relativePaths->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("unix.ui.portable_mode")) {
            portableMode->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("unix.ui.full_logging")) {
            fullLogging->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("unix.ui.log_window")) {
            logWindow->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("unix.ui.gui_scale")) {
            miscGuiSize->setCurrentText(value);
            applyGuiScaleSelection(false);
        } else if (key == QStringLiteral("unix.ui.gui_resize")) {
            miscGuiResize->setChecked(configBoolValue(value));
            applyGuiResizeMode();
        } else if (key == QStringLiteral("unix.ui.gui_fullscreen")) {
            miscGuiFullscreen->setChecked(configBoolValue(value));
            applyGuiFullscreenMode(miscGuiFullscreen->isChecked());
        } else if (key == QStringLiteral("unix.ui.gui_dark_mode")) {
            setGuiDarkModeFromConfig(value);
        } else if (key == QStringLiteral("unix.ui.gui_font")) {
            applyGuiFontConfig(value);
        } else if (key == QStringLiteral("unix.output_file") || key == QStringLiteral("unix.ui.output_file")) {
            outputFile->setText(value);
        } else if (key == QStringLiteral("unix.output_audio_codec") || key == QStringLiteral("unix.ui.output_audio_codec")) {
            outputAudioCodec->setCurrentIndex(value.compare(QStringLiteral("wav"), Qt::CaseInsensitive) == 0 ? 1 : 0);
            updateOutputControlState();
        } else if (key == QStringLiteral("unix.output_video_codec") || key == QStringLiteral("unix.ui.output_video_codec")) {
            outputVideoCodec->setCurrentIndex(0);
        } else if (key == QStringLiteral("unix.output_audio") || key == QStringLiteral("unix.ui.output_audio")) {
            outputAudio->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("unix.output_video") || key == QStringLiteral("unix.ui.output_video")) {
            outputVideo->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("unix.output_enabled") || key == QStringLiteral("unix.ui.output_enabled")) {
            outputEnabled->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("unix.output_frame_limiter_disabled") || key == QStringLiteral("unix.ui.output_frame_limiter_disabled")) {
            outputFrameLimiter->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("unix.output_original_size") || key == QStringLiteral("unix.ui.output_original_size")) {
            outputOriginalSize->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("unix.output_no_sound") || key == QStringLiteral("unix.ui.output_no_sound")) {
            outputNoSound->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("unix.output_no_sound_sync") || key == QStringLiteral("unix.ui.output_no_sound_sync")) {
            outputNoSoundSync->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("unix.screenshot_original_size") || key == QStringLiteral("unix.ui.screenshot_original_size")) {
            screenshotOriginalSize->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("unix.screenshot_paletted") || key == QStringLiteral("unix.ui.screenshot_paletted")) {
            screenshotPaletted->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("unix.screenshot_clip") || key == QStringLiteral("unix.ui.screenshot_clip")) {
            screenshotClip->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("unix.screenshot_auto") || key == QStringLiteral("unix.ui.screenshot_auto")) {
            screenshotAuto->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("kickstart_rom_file")) {
            setPathComboText(romFile, value);
        } else if (key == QStringLiteral("kickstart_ext_rom_file")) {
            setPathComboText(extendedRomFile, value);
        } else if (key == QStringLiteral("cart_file")) {
            setPathComboText(cartFile, value);
        } else if (key == QStringLiteral("flash_file")) {
            flashFile->setText(value);
        } else if (key == QStringLiteral("rtc_file")) {
            rtcFile->setText(value);
        } else if (key == QStringLiteral("maprom")) {
            bool ok = false;
            const uint mapValue = value.toUInt(&ok, 0);
            mapRom->setChecked(ok && mapValue != 0);
        } else if (key == QStringLiteral("kickshifter")) {
            kickShifter->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("boot_rom_uae")) {
            if (value.compare(QStringLiteral("disabled"), Qt::CaseInsensitive) == 0) {
                uaeBoardType->setCurrentText(QStringLiteral("ROM disabled"));
            } else if (uaeBoardType->currentText() == QStringLiteral("ROM disabled")) {
                uaeBoardType->setCurrentText(QStringLiteral("Original UAE (FS + F0 ROM)"));
            }
        } else if (key == QStringLiteral("uaeboard")) {
            if (value.compare(QStringLiteral("disabled"), Qt::CaseInsensitive) == 0
                || value.compare(QStringLiteral("disabled_off"), Qt::CaseInsensitive) == 0) {
                if (uaeBoardType->currentText() != QStringLiteral("ROM disabled")) {
                    uaeBoardType->setCurrentText(QStringLiteral("Original UAE (FS + F0 ROM)"));
                }
            } else {
                uaeBoardType->setCurrentText(uaeBoardText(value));
            }
        } else if (romBoardIndexFromKey(key) >= 0) {
            applyCustomRomBoard(romBoardIndexFromKey(key), value);
        } else if (key == QStringLiteral("board_custom_order")) {
            hardwareCustomBoardOrder->setChecked(configBoolValue(value));
            refreshHardwareInfoPage();
        } else if (key == QStringLiteral("floppy_speed")) {
            floppySpeed->setValue(floppySpeedSliderPosition(value.toInt()));
            updateFloppySpeedLabel();
        } else if (const int drive = floppyKeyDrive(key, QStringLiteral("type")); drive >= 0) {
            const int driveType = value.toInt();
            dfEnable[drive]->setChecked(driveType >= 0);
            dfType[drive]->setCurrentText(floppyTypeText(driveType));
            syncFloppyDriveToQuick(drive);
        } else if (const int drive = floppyKeyDrive(key, QStringLiteral("wp")); drive >= 0) {
            dfWriteProtect[drive]->setChecked(configBoolValue(value));
            syncFloppyDriveToQuick(drive);
        } else if (const int drive = floppyKeyDrive(key); drive >= 0) {
            dfEnable[drive]->setChecked(true);
            setPathComboText(dfPath[drive], value);
            syncFloppyDriveToQuick(drive);
        } else if (key.startsWith(QStringLiteral("diskimage"))) {
            bool ok = false;
            const int slot = key.mid(9).toInt(&ok);
            if (ok && slot >= 0 && slot < MaxDiskSwapperSlots) {
                setDiskSwapperPath(slot, value);
                if (slot == selectedDiskSwapperSlot()) {
                    setPathComboText(diskSwapperPath, value);
                }
            }
        } else if (key.startsWith(QStringLiteral("uaehf"))) {
            WinUaeQtMountEntry entry;
            if (parseWinUaeQtUaehfMountValue(value, &entry)) {
                addMountEntryIfUnique(entry);
            }
        } else if (key == QStringLiteral("filesystem2")) {
            WinUaeQtMountEntry entry;
            if (parseWinUaeQtFilesystem2MountValue(value, &entry)) {
                addMountEntryIfUnique(entry);
            }
        } else if (key == QStringLiteral("hardfile2")) {
            WinUaeQtMountEntry entry;
            if (parseWinUaeQtHardfile2MountValue(value, &entry)) {
                addMountEntryIfUnique(entry);
            }
        } else if (key.startsWith(QStringLiteral("cdimage"))) {
            bool ok = false;
            const int slot = key.mid(7).toInt(&ok);
            if (ok && slot >= 0 && slot < MaxCdSlots) {
                ensureCdSlots();
                cdSlots[slot] = cdSlotFromConfigValue(value);
                if (slot == currentCdSlot) {
                    loadCdSlotToUi(slot);
                }
            }
        } else if (key == QStringLiteral("cd_speed")) {
            cdSpeedTurbo->setChecked(value == QStringLiteral("0"));
        } else if (key == QStringLiteral("filesys_no_fsdb")) {
            filesysNoFsdb->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("filesys_max_size")) {
            bool ok = false;
            filesysLimitSize->setChecked(configIntegerValue(value, &ok) != 0 && ok);
        } else if (key == QStringLiteral("state_replay_rate")) {
            const int seconds = value.toInt() > 0 ? value.toInt() / 50 : -1;
            stateReplayRate->setCurrentText(seconds > 0 ? QString::number(seconds) : QStringLiteral("-"));
        } else if (key == QStringLiteral("state_replay_buffers")) {
            stateReplayBuffers->setCurrentText(value.toInt() > 0 ? value : QStringLiteral("100"));
        } else if (key == QStringLiteral("state_replay_autoplay")) {
            stateReplayAutoplay->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("chipset")) {
            chipset->setCurrentText(chipsetText(value));
        } else if (key == QStringLiteral("chipset_compatible")) {
            chipsetCompatible->setCurrentText(value);
            quickModel->setCurrentText(value);
        } else if (key == QStringLiteral("ntsc")) {
            chipsetNtsc->setChecked(configBoolValue(value));
            ntsc->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("immediate_blits")) {
            immediateBlits->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("waiting_blits")) {
            waitingBlits->setChecked(value.compare(QStringLiteral("disabled"), Qt::CaseInsensitive) != 0 && value != QStringLiteral("0"));
        } else if (key == QStringLiteral("collision_level")) {
            const QString lower = value.toLower();
            int id = lower == QStringLiteral("none") ? 0
                : lower == QStringLiteral("sprites") ? 1
                : lower == QStringLiteral("playfields") ? 2
                : 3;
            if (QAbstractButton *button = collisionButtons->button(id)) {
                button->setChecked(true);
            }
        } else if (key == QStringLiteral("display_optimizations")) {
            displayOptimization->setCurrentText(displayOptimizationText(value));
        } else if (key == QStringLiteral("hvcsync")) {
            chipsetSyncMode->setCurrentText(configChoiceDisplay(hvSyncChoices, int(sizeof(hvSyncChoices) / sizeof(hvSyncChoices[0])), value));
        } else if (key == QStringLiteral("genlock")) {
            genlockConnected->setChecked(configBoolValue(value));
            updateGenlockControlState();
        } else if (key == QStringLiteral("genlockmode")) {
            genlockMode->setCurrentText(configChoiceDisplay(genlockModeChoices, int(sizeof(genlockModeChoices) / sizeof(genlockModeChoices[0])), value));
            updateGenlockControlState();
        } else if (key == QStringLiteral("genlock_mix")) {
            setGenlockMixConfigValue(value.toInt());
        } else if (key == QStringLiteral("genlock_alpha")) {
            genlockAlpha->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("genlock_aspect")) {
            genlockKeepAspect->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("genlock_image")) {
            genlockImagePath = value;
            const QString mode = configChoiceValue(genlockModeChoices, int(sizeof(genlockModeChoices) / sizeof(genlockModeChoices[0])), genlockMode->currentText());
            if (genlockModeUsesImageFile(mode)) {
                setPathComboText(genlockFile, value);
            }
        } else if (key == QStringLiteral("genlock_video")) {
            genlockVideoPath = value;
            const QString mode = configChoiceValue(genlockModeChoices, int(sizeof(genlockModeChoices) / sizeof(genlockModeChoices[0])), genlockMode->currentText());
            if (genlockModeUsesVideoFile(mode)) {
                setPathComboText(genlockFile, value);
            }
        } else if (key == QStringLiteral("keyboard_connected")) {
            if (!configBoolValue(value)) {
                keyboardMode->setCurrentText(QStringLiteral("Keyboard disconnected"));
            } else if (keyboardMode->currentText() == QStringLiteral("Keyboard disconnected")) {
                keyboardMode->setCurrentText(QStringLiteral("UAE High level emulation"));
            }
        } else if (key == QStringLiteral("keyboard_type")) {
            keyboardMode->setCurrentText(configChoiceDisplay(keyboardModeChoices, int(sizeof(keyboardModeChoices) / sizeof(keyboardModeChoices[0])), value));
        } else if (key == QStringLiteral("keyboard_nkro")) {
            keyboardNkro->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("ciaatod")) {
            setAdvancedRadioValue(advancedCiaTodButtons, ciaTodChoices, int(sizeof(ciaTodChoices) / sizeof(ciaTodChoices[0])), value);
        } else if (key == QStringLiteral("rtc")) {
            setAdvancedRadioValue(advancedRtcButtons, rtcChoices, int(sizeof(rtcChoices) / sizeof(rtcChoices[0])), value);
        } else if (key == QStringLiteral("chipset_rtc_adjust")) {
            advancedRtcAdjust->setText(value);
        } else if (advancedCheckBoxes.contains(key)) {
            setAdvancedCheck(key, configBoolValue(value));
        } else if (key == QStringLiteral("ide")) {
            const QString lower = value.toLower();
            advancedIdeA600A1200->setChecked(lower == QStringLiteral("a600/a1200"));
            advancedIdeA4000->setChecked(lower == QStringLiteral("a4000"));
        } else if (key == QStringLiteral("scsi_a3000")) {
            advancedScsiA3000->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("scsi_a4000t")) {
            advancedScsiA4000T->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("ciaa_type")) {
            advancedCia391078->setChecked(value.compare(QStringLiteral("391078-01"), Qt::CaseInsensitive) == 0);
        } else if (key == QStringLiteral("ciab_type")) {
            if (value.compare(QStringLiteral("391078-01"), Qt::CaseInsensitive) == 0) {
                advancedCia391078->setChecked(true);
            }
        } else if (key == QStringLiteral("unmapped_address_space")) {
            advancedUnmappedAddress->setCurrentText(configChoiceDisplay(unmappedAddressChoices, int(sizeof(unmappedAddressChoices) / sizeof(unmappedAddressChoices[0])), value));
        } else if (key == QStringLiteral("eclocksync")) {
            advancedCiaSync->setCurrentText(configChoiceDisplay(ciaSyncChoices, int(sizeof(ciaSyncChoices) / sizeof(ciaSyncChoices[0])), value));
        } else if (key == QStringLiteral("fatgary") || key == QStringLiteral("ramsey")) {
            bool ok = false;
            const int revision = configIntegerValue(value, &ok);
            if (key == QStringLiteral("fatgary")) {
                setAdvancedRevision(advancedFatGary, advancedFatGaryRevision, ok ? revision : -1, 0x00);
            } else {
                setAdvancedRevision(advancedRamsey, advancedRamseyRevision, ok ? revision : -1, 0x0f);
            }
        } else if (key == QStringLiteral("agnusmodel")) {
            advancedAgnusModel->setCurrentText(configChoiceDisplay(agnusModelChoices, int(sizeof(agnusModelChoices) / sizeof(agnusModelChoices[0])), value));
        } else if (key == QStringLiteral("agnussize")) {
            advancedAgnusSize->setCurrentText(configChoiceDisplay(agnusSizeChoices, int(sizeof(agnusSizeChoices) / sizeof(agnusSizeChoices[0])), value));
        } else if (key == QStringLiteral("denisemodel")) {
            advancedDeniseModel->setCurrentText(configChoiceDisplay(deniseModelChoices, int(sizeof(deniseModelChoices) / sizeof(deniseModelChoices[0])), value));
        } else if (key == QStringLiteral("cycle_exact")) {
            const QString lower = value.toLower();
            chipsetCycleExact->setChecked(lower == QStringLiteral("true"));
            chipsetCycleExactMemory->setChecked(lower == QStringLiteral("true") || lower == QStringLiteral("memory"));
        } else if (key == QStringLiteral("cpu_cycle_exact")) {
            chipsetCycleExact->setChecked(configBoolValue(value));
            if (configBoolValue(value)) {
                chipsetCycleExactMemory->setChecked(true);
            }
        } else if (key == QStringLiteral("cpu_memory_cycle_exact")) {
            chipsetCycleExactMemory->setChecked(configBoolValue(value));
            if (!configBoolValue(value)) {
                chipsetCycleExact->setChecked(false);
            }
        } else if (key == QStringLiteral("blitter_cycle_exact")) {
            if (!configBoolValue(value)) {
                chipsetCycleExactMemory->setChecked(false);
            }
        } else if (key == QStringLiteral("cpu_model")) {
            setCpuButton(value.toInt());
        } else if (key == QStringLiteral("cpu_speed")) {
            const bool fastest = value.compare(QStringLiteral("max"), Qt::CaseInsensitive) == 0;
            if (QAbstractButton *button = cpuSpeedButtons->button(fastest ? 1 : 0)) {
                button->setChecked(true);
            }
        } else if (key == QStringLiteral("cpu_throttle")) {
            cpuSpeed->setValue(qBound(cpuSpeed->minimum(), qRound(value.toDouble() / 100.0), cpuSpeed->maximum()));
            updateCpuSpeedLabel();
        } else if (key == QStringLiteral("cpu_compatible")) {
            moreCompatible->setChecked(configBoolValue(value));
            updateCpuControlState();
        } else if (key == QStringLiteral("fpu_model")) {
            setFpuButton(value.toInt());
        } else if (key == QStringLiteral("cpu_24bit_addressing")) {
            cpu24Bit->setChecked(value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
            updateCpuControlState();
        } else if (key == QStringLiteral("mmu_model")) {
            const QString lower = value.toLower();
            const int id = lower.startsWith(QStringLiteral("68ec")) ? 2 : (value.toInt() > 0 ? 1 : 0);
            if (QAbstractButton *button = mmuButtons->button(id)) {
                button->setChecked(true);
            }
        } else if (key == QStringLiteral("cpu_no_unimplemented")) {
            cpuUnimplemented->setChecked(!configBoolValue(value));
        } else if (key == QStringLiteral("fpu_no_unimplemented")) {
            fpuUnimplemented->setChecked(!configBoolValue(value));
        } else if (key == QStringLiteral("fpu_strict")) {
            fpuStrict->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("fpu_softfloat")) {
            if (configBoolValue(value)) {
                fpuMode->setCurrentText(QStringLiteral("Softfloat (80-bit)"));
            } else if (fpuMode->currentText() == QStringLiteral("Softfloat (80-bit)")) {
                fpuMode->setCurrentText(QStringLiteral("Host (64-bit)"));
            }
        } else if (key == QStringLiteral("fpu_msvc_long_double")) {
            if (configBoolValue(value)) {
                fpuMode->setCurrentText(QStringLiteral("Host (80-bit)"));
            } else if (fpuMode->currentText() == QStringLiteral("Host (80-bit)")) {
                fpuMode->setCurrentText(QStringLiteral("Host (64-bit)"));
            }
        } else if (key == QStringLiteral("cpu_multiplier")) {
            cpuFrequency->setCurrentText(cpuMultiplierText(value.toInt()));
        } else if (key == QStringLiteral("cpu_frequency")) {
            cpuFrequency->setCurrentText(QStringLiteral("Custom"));
            cpuFrequencyCustom->setText(QString::number(value.toDouble() / 1000000.0, 'f', 6));
        } else if (key == QStringLiteral("cachesize")) {
            const int size = value.toInt();
            jit->setChecked(unixJitBackendAvailable() && size > 0);
            jitCache->setValue(jitCachePositionFromSize(size));
            updateCpuControlState();
        } else if (key == QStringLiteral("compfpu")) {
            jitFpu->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("comp_constjump")) {
            jitConstJump->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("comp_flushmode")) {
            jitHardFlush->setChecked(value.compare(QStringLiteral("hard"), Qt::CaseInsensitive) == 0 || configBoolValue(value));
        } else if (key == QStringLiteral("comp_nf")) {
            jitNoFlags->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("comp_catchfault")) {
            jitCatchFault->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("comp_trustbyte")
            || key == QStringLiteral("comp_trustword")
            || key == QStringLiteral("comp_trustlong")
            || key == QStringLiteral("comp_trustnaddr")) {
            if (QAbstractButton *button = jitTrust->button(value.compare(QStringLiteral("indirect"), Qt::CaseInsensitive) == 0 ? 1 : 0)) {
                button->setChecked(true);
            }
        } else if (key == QStringLiteral("chipmem_size")) {
            const QMap<int, QString> map = { { 1, QStringLiteral("512 KB") }, { 2, QStringLiteral("1 MB") }, { 4, QStringLiteral("2 MB") }, { 8, QStringLiteral("4 MB") }, { 16, QStringLiteral("8 MB") } };
            chipMem->setCurrentText(map.value(value.toInt(), QStringLiteral("2 MB")));
        } else if (key == QStringLiteral("fastmem_size")) {
            z2Fast->setCurrentText(value == QStringLiteral("0") ? QStringLiteral("None") : value + QStringLiteral(" MB"));
        } else if (key == QStringLiteral("bogomem_size")) {
            const QMap<int, QString> map = {
                { 0, QStringLiteral("None") },
                { 2, QStringLiteral("512 KB") },
                { 4, QStringLiteral("1 MB") },
                { 6, QStringLiteral("1.5 MB") },
                { 7, QStringLiteral("1.8 MB") }
            };
            slowMem->setCurrentText(map.value(value.toInt(), QStringLiteral("None")));
        } else if (key == QStringLiteral("z3mem_size")) {
            z3Fast->setCurrentText(value == QStringLiteral("0") ? QStringLiteral("None") : value + QStringLiteral(" MB"));
        } else if (key == QStringLiteral("megachipmem_size")) {
            z3ChipMem->setCurrentText(value == QStringLiteral("0") ? QStringLiteral("None") : value + QStringLiteral(" MB"));
        } else if (key == QStringLiteral("mbresmem_size")) {
            processorSlotMem->setCurrentText(value == QStringLiteral("0") ? QStringLiteral("None") : value + QStringLiteral(" MB"));
        } else if (key == QStringLiteral("z3mapping")) {
            z3Mapping->setCurrentText(configChoiceDisplay(z3MappingChoices, int(sizeof(z3MappingChoices) / sizeof(z3MappingChoices[0])), value));
        } else if (key == QStringLiteral("gfxcard_size")) {
            rtgMem->setCurrentText(value == QStringLiteral("0") ? QStringLiteral("None") : value + QStringLiteral(" MB"));
        } else if (key == QStringLiteral("gfxcard_type")) {
            rtgType->setCurrentText(value);
        } else if (key == QStringLiteral("gfxcard_options")) {
            applyRtgOptionsValue(value);
        } else if (key == QStringLiteral("gfx_filter_autoscale_rtg")) {
            applyRtgScaleValue(value);
            filterStates[1].autoscale = isRtgAutoscaleValue(value) ? value : QStringLiteral("resize");
            if (currentFilterTarget == 1) {
                loadFilterStateToUi(1);
            }
        } else if (key == QStringLiteral("gfx_filter_aspect_ratio_rtg")) {
            rtgAspectRatio->setCurrentText(configChoiceDisplay(rtgAspectRatioChoices, int(sizeof(rtgAspectRatioChoices) / sizeof(rtgAspectRatioChoices[0])), value));
        } else if (key == QStringLiteral("gfx_backbuffers_rtg")) {
            rtgBuffers->setCurrentText(rtgBufferText(value));
        } else if (key == QStringLiteral("gfx_refreshrate_rtg")) {
            if (value == QStringLiteral("0")) {
                rtgRefreshRate->setCurrentText(QStringLiteral("Chipset"));
            } else {
                rtgRefreshRate->setCurrentText(value);
            }
        } else if (key == QStringLiteral("unix.rtg_vblank") || key == QStringLiteral("rtg_vblank")) {
            rtgRefreshRate->setCurrentText(rtgVBlankText(value));
        } else if (key == QStringLiteral("gfxcard_hardware_vblank")) {
            rtgHardwareVBlank->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("gfxcard_hardware_sprite")) {
            rtgHardwareSprite->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("gfxcard_multithread")) {
            rtgMultithread->setChecked(rtgMultithread->isEnabled() && configBoolValue(value));
        } else if (key == QStringLiteral("rtg_modes")) {
            bool ok = false;
            int mask = value.toInt(&ok, 0);
            if (!ok) {
                mask = RtgDefaultModeMask;
            }
            rtg8Bit->setCurrentText(rtg8BitText(mask));
            rtg16Bit->setCurrentText(rtg16BitText(mask));
            rtg24Bit->setCurrentText(rtg24BitText(mask));
            rtg32Bit->setCurrentText(rtg32BitText(mask));
        } else if (key == QStringLiteral("cpuboard_type")) {
            if (value.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0 || value == QStringLiteral("-")) {
                selectCpuBoardConfigValue(QString());
            } else {
                selectCpuBoardConfigValue(value);
            }
        } else if (key == QStringLiteral("cpuboardmem1_size")) {
            setCpuBoardMemoryMb(value.toInt());
        } else if (key == QStringLiteral("cpuboard_rom_file")) {
            setPathComboText(cpuBoardRom, value);
        } else if (key == QStringLiteral("cpuboard_settings")) {
            cpuBoardSettingsRaw = value;
            if (acceleratorOption) {
                acceleratorOption->setCurrentText(QStringLiteral("Settings preserved"));
            }
        } else if (applyExpansionBoardSetting(key, value)) {
        } else if (key == QStringLiteral("bsdsocket_emu")) {
            expansionBsdsocket->setChecked(expansionBsdsocket->isEnabled() && configBoolValue(value));
        } else if (key == QStringLiteral("scsi")) {
            const QString lower = value.toLower();
            expansionScsiDevice->setChecked(expansionScsiDevice->isEnabled() && lower != QStringLiteral("false") && lower != QStringLiteral("0") && !lower.isEmpty());
        } else if (key == QStringLiteral("uaescsimode") || key == QStringLiteral("unix.uaescsimode")) {
            miscScsiMode->setCurrentText(configChoiceDisplay(scsiModeChoices, int(sizeof(scsiModeChoices) / sizeof(scsiModeChoices[0])), value));
        } else if (key == QStringLiteral("sana2")) {
            expansionSana2->setChecked(expansionSana2->isEnabled() && configBoolValue(value));
        } else if (key == QStringLiteral("unix.soundcard") || key == QStringLiteral("soundcard")) {
            bool ok = false;
            const int index = value.toInt(&ok);
            if (ok && soundDevice && index >= 0 && index < soundDevice->count()) {
                soundDevice->setCurrentIndex(index);
            }
        } else if (key == QStringLiteral("unix.soundcardname") || key == QStringLiteral("soundcardname")) {
            selectSoundDeviceByConfigName(value);
        } else if (key == QStringLiteral("sound_output")) {
            if (QAbstractButton *button = soundOutputButtons->button(soundOutputId(value))) {
                button->setChecked(true);
            }
            updateSoundControlState();
        } else if (key == QStringLiteral("sound_auto")) {
            soundAutomatic->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("sound_volume")) {
            soundMasterVolume->setValue(100 - qBound(0, value.toInt(), 100));
        } else if (key == QStringLiteral("sound_volume_paula")
            || key == QStringLiteral("sound_volume_cd")
            || key == QStringLiteral("sound_volume_ahi")
            || key == QStringLiteral("sound_volume_midi")
            || key == QStringLiteral("sound_volume_genlock")) {
            const QMap<QString, int> volumeIndex = {
                { QStringLiteral("sound_volume_paula"), 0 },
                { QStringLiteral("sound_volume_cd"), 1 },
                { QStringLiteral("sound_volume_ahi"), 2 },
                { QStringLiteral("sound_volume_midi"), 3 },
                { QStringLiteral("sound_volume_genlock"), 4 }
            };
            const int index = volumeIndex.value(key, 0);
            soundVolumeAttenuation[index] = qBound(0, value.toInt(), 100);
            if (index == currentSoundVolume) {
                loadSelectedSoundVolume();
            }
        } else if (key == QStringLiteral("sound_max_buff")) {
            soundBufferSize->setValue(soundBufferIndexFromSize(value.toInt()));
        } else if (key == QStringLiteral("sound_channels")) {
            soundChannels->setCurrentText(soundChannelText(value));
            updateSoundControlState();
        } else if (key == QStringLiteral("sound_stereo_separation")) {
            soundStereoSeparation->setCurrentText(QStringLiteral("%1%").arg(qBound(0, value.toInt(), 10) * 10));
        } else if (key == QStringLiteral("sound_stereo_mixing_delay")) {
            const int delay = qBound(0, value.toInt(), 10);
            soundStereoDelay->setCurrentText(delay > 0 ? QString::number(delay) : QStringLiteral("-"));
        } else if (key == QStringLiteral("sound_frequency")) {
            soundFrequency->setCurrentText(QString::number(qBound(8000, value.toInt(), 768000)));
        } else if (key == QStringLiteral("sound_interpol")) {
            soundInterpolation->setCurrentText(soundInterpolationText(value));
        } else if (key == QStringLiteral("sound_filter")) {
            soundFilter->setCurrentText(soundFilterText(value, soundFilterTypeConfigValue(soundFilter->currentText())));
        } else if (key == QStringLiteral("sound_filter_type")) {
            soundFilter->setCurrentText(soundFilterText(soundFilterConfigValue(soundFilter->currentText()), value));
        } else if (key == QStringLiteral("sound_stereo_swap_paula")) {
            setSoundSwapBit(true, configBoolValue(value));
        } else if (key == QStringLiteral("sound_stereo_swap_ahi")) {
            setSoundSwapBit(false, configBoolValue(value));
        } else if (key == QStringLiteral("floppy_volume")) {
            const int attenuation = qBound(0, value.toInt(), 100);
            for (int i = 0; i < FloppySoundDriveCount; i++) {
                floppySoundEmptyAttenuation[i] = attenuation;
                floppySoundDiskAttenuation[i] = attenuation;
            }
            loadSelectedFloppySound();
        } else if (const int drive = floppyKeyDrive(key, QStringLiteral("sound")); drive >= 0) {
            floppySoundTypeValue[drive] = qBound(0, value.toInt(), 1);
            if (drive == currentFloppySoundDrive) {
                loadSelectedFloppySound();
            }
        } else if (const int drive = floppyKeyDrive(key, QStringLiteral("soundvolume_empty")); drive >= 0) {
            floppySoundEmptyAttenuation[drive] = qBound(0, value.toInt(), 100);
            if (drive == currentFloppySoundDrive) {
                loadSelectedFloppySound();
            }
        } else if (const int drive = floppyKeyDrive(key, QStringLiteral("soundvolume_disk")); drive >= 0) {
            floppySoundDiskAttenuation[drive] = qBound(0, value.toInt(), 100);
            if (drive == currentFloppySoundDrive) {
                loadSelectedFloppySound();
            }
        } else if (key == QStringLiteral("parallel_matrix_emulation")) {
            printerType->setCurrentText(configChoiceDisplay(printerTypeChoices, int(sizeof(printerTypeChoices) / sizeof(printerTypeChoices[0])), value));
        } else if (key == QStringLiteral("parallel_postscript_detection")) {
            if (configBoolValue(value)) {
                printerType->setCurrentText(QStringLiteral("PostScript (Passthrough)"));
            } else if (printerType->currentText().startsWith(QStringLiteral("PostScript"))) {
                printerType->setCurrentText(QStringLiteral("Passthrough"));
            }
        } else if (key == QStringLiteral("parallel_postscript_emulation")) {
            if (configBoolValue(value)) {
                printerType->setCurrentText(QStringLiteral("PostScript (Emulation)"));
            } else if (printerType->currentText() == QStringLiteral("PostScript (Emulation)")) {
                printerType->setCurrentText(QStringLiteral("PostScript (Passthrough)"));
            }
        } else if (key == QStringLiteral("parallel_autoflush")) {
            printerAutoFlush->setValue(qBound(0, value.toInt(), 3600));
        } else if (key == QStringLiteral("ghostscript_parameters")) {
            ghostscriptParams->setText(value);
        } else if (key == QStringLiteral("unix.samplersoundcard")
            || key == QStringLiteral("samplersoundcard")
            || key == QStringLiteral("unix.sampler_soundcard")
            || key == QStringLiteral("sampler_soundcard")) {
            bool ok = false;
            const int index = value.toInt(&ok);
            if (ok && samplerDevice && index >= -1 && index + 1 < samplerDevice->count()) {
                samplerDevice->setCurrentIndex(index + 1);
            }
            updateIoPortsState();
        } else if (key == QStringLiteral("unix.samplersoundcardname")
            || key == QStringLiteral("samplersoundcardname")
            || key == QStringLiteral("unix.sampler_soundcardname")
            || key == QStringLiteral("sampler_soundcardname")) {
            selectSamplerDeviceByConfigName(value);
            updateIoPortsState();
        } else if (key == QStringLiteral("sampler_stereo")) {
            samplerStereo->setChecked(samplerDevice
                && samplerDevice->currentText() != QStringLiteral("<None>")
                && configBoolValue(value));
            updateIoPortsState();
        } else if (key == QStringLiteral("midi_device") || key == QStringLiteral("midiout_device")) {
            bool ok = false;
            const int deviceId = value.toInt(&ok);
            if (ok) {
                selectMidiOutByDeviceId(deviceId);
            }
        } else if (key == QStringLiteral("midiout_device_name")) {
            selectMidiOutByConfigName(value);
        } else if (key == QStringLiteral("midiin_device")) {
            bool ok = false;
            const int deviceId = value.toInt(&ok);
            if (ok) {
                selectMidiInByDeviceId(deviceId);
            }
        } else if (key == QStringLiteral("midiin_device_name")) {
            selectMidiInByConfigName(value);
        } else if (key == QStringLiteral("midirouter")) {
            midiRouter->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("unix.serial_port") || key == QStringLiteral("serial_port")) {
            serialPort->setCurrentText(value.isEmpty() ? QStringLiteral("<None>") : value);
            updateIoPortsState();
        } else if (key == QStringLiteral("serial_on_demand")) {
            serialShared->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("serial_hardware_ctsrts")) {
            serialCtsRts->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("serial_status")) {
            serialStatus->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("serial_ri")) {
            serialRingIndicator->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("serial_direct")) {
            serialDirect->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("serial_translate")) {
            serialCrlf->setChecked(value.compare(QStringLiteral("crlf_cr"), Qt::CaseInsensitive) == 0 || configBoolValue(value));
        } else if (key == QStringLiteral("uaeserial")) {
            uaeSerial->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("dongle")) {
            bool ok = false;
            const int index = value.toInt(&ok);
            if (ok && index >= 0 && index < int(sizeof(dongleChoices) / sizeof(dongleChoices[0]))) {
                protectionDongle->setCurrentText(QString::fromLatin1(dongleChoices[index].display));
            } else {
                protectionDongle->setCurrentText(configChoiceDisplay(dongleChoices, int(sizeof(dongleChoices) / sizeof(dongleChoices[0])), value));
            }
        } else if (miscOptionItems.contains(key)) {
            if (key == QStringLiteral("power_led_dim")) {
                setMiscOptionChecked(key, value.toInt() != 0);
            } else {
                setMiscOptionChecked(key, configBoolValue(value));
            }
        } else if (key == QStringLiteral("statefile")) {
            setPathComboText(stateFileName, value);
            stateFileClear->setChecked(value.trimmed().isEmpty());
        } else if (key == QStringLiteral("keyboard_leds")) {
            for (const QString &field : value.split(QLatin1Char(','))) {
                const QStringList parts = field.split(QLatin1Char(':'));
                if (parts.size() != 2) {
                    continue;
                }
                const QString name = parts[0].trimmed().toLower();
                const QString led = configChoiceDisplay(keyboardLedChoices, int(sizeof(keyboardLedChoices) / sizeof(keyboardLedChoices[0])), parts[1].trimmed());
                if (name == QStringLiteral("numlock")) {
                    keyboardLed[0]->setCurrentText(led);
                } else if (name == QStringLiteral("capslock")) {
                    keyboardLed[1]->setCurrentText(led);
                } else if (name == QStringLiteral("scrolllock")) {
                    keyboardLed[2]->setCurrentText(led);
                }
            }
        } else if (key == QStringLiteral("active_priority") || key == QStringLiteral("activepriority")) {
            setExtensionPriorityValue(extensionActivePriority, value.toInt());
        } else if (key == QStringLiteral("active_not_captured_pause")) {
            extensionActivePause->setChecked(configBoolValue(value));
            updateExtensionActivityState();
        } else if (key == QStringLiteral("active_not_captured_nosound")) {
            extensionActiveNoSound->setChecked(configBoolValue(value));
            updateExtensionActivityState();
        } else if (key == QStringLiteral("active_input")) {
            const int mask = value.toInt();
            extensionActiveNoJoy->setChecked((mask & 4) == 0);
            extensionActiveNoKeyboard->setChecked((mask & 1) == 0);
            updateExtensionActivityState();
        } else if (key == QStringLiteral("inactive_priority")) {
            setExtensionPriorityValue(extensionInactivePriority, value.toInt());
        } else if (key == QStringLiteral("inactive_pause")) {
            extensionInactivePause->setChecked(configBoolValue(value));
            updateExtensionActivityState();
        } else if (key == QStringLiteral("inactive_nosound")) {
            extensionInactiveNoSound->setChecked(configBoolValue(value));
            updateExtensionActivityState();
        } else if (key == QStringLiteral("inactive_input")) {
            extensionInactiveNoJoy->setChecked((value.toInt() & 4) == 0);
            updateExtensionActivityState();
        } else if (key == QStringLiteral("iconified_priority")) {
            setExtensionPriorityValue(extensionMinimizedPriority, value.toInt());
        } else if (key == QStringLiteral("iconified_pause")) {
            extensionMinimizedPause->setChecked(configBoolValue(value));
            updateExtensionActivityState();
        } else if (key == QStringLiteral("iconified_nosound")) {
            extensionMinimizedNoSound->setChecked(configBoolValue(value));
            updateExtensionActivityState();
        } else if (key == QStringLiteral("iconified_input")) {
            extensionMinimizedNoJoy->setChecked((value.toInt() & 4) == 0);
            updateExtensionActivityState();
        } else if (key.startsWith(QStringLiteral("joyport")) && key.size() == 8) {
            bool ok = false;
            const int port = key.mid(7, 1).toInt(&ok);
            if (ok && port >= 0 && port < 4) {
                portDevice[port]->setCurrentText(joyportDeviceText(value, port < 2));
            }
        } else if (key.startsWith(QStringLiteral("joyportcustom"))) {
            bool ok = false;
            const int slot = key.mid(13).toInt(&ok);
            if (ok && slot >= 0 && slot < MaxJoyportCustomSlots) {
                joyportCustom[slot] = value;
            }
        } else if (key.startsWith(QStringLiteral("joyport")) && key.endsWith(QStringLiteral("autofire"))) {
            bool ok = false;
            const int port = key.mid(7, 1).toInt(&ok);
            if (ok && port >= 0 && port < 2) {
                portAutofire[port]->setCurrentText(autofireText(value));
            }
        } else if (key.startsWith(QStringLiteral("joyport")) && key.endsWith(QStringLiteral("mode"))) {
            bool ok = false;
            const int port = key.mid(7, 1).toInt(&ok);
            if (ok && port >= 0 && port < 2) {
                portMode[port]->setCurrentText(joyportModeText(value));
            }
        } else if (key == QStringLiteral("input.config")) {
            const int config = value.toInt();
            if (config <= 0) {
                inputType->setCurrentText(QStringLiteral("Game Ports"));
            } else {
                inputType->setCurrentIndex(qBound(0, config - 1, CustomInputConfigSlots));
            }
        } else if (key == QStringLiteral("input.joystick_deadzone") || key == QStringLiteral("input.joymouse_deadzone")) {
            inputDeadzone->setValue(qBound(inputDeadzone->minimum(), value.toInt(), inputDeadzone->maximum()));
        } else if (key == QStringLiteral("input.autofire_speed")) {
            inputAutofireRate->setValue(qBound(inputAutofireRate->minimum(), value.toInt(), inputAutofireRate->maximum()));
        } else if (key == QStringLiteral("input.joymouse_speed_digital")) {
            inputJoyMouseDigital->setValue(qBound(inputJoyMouseDigital->minimum(), value.toInt(), inputJoyMouseDigital->maximum()));
        } else if (key == QStringLiteral("input.joymouse_speed_analog")) {
            inputJoyMouseAnalog->setValue(qBound(inputJoyMouseAnalog->minimum(), value.toInt(), inputJoyMouseAnalog->maximum()));
        } else if (key == QStringLiteral("input.mouse_speed")) {
            mouseSpeed->setValue(qBound(mouseSpeed->minimum(), value.toInt(), mouseSpeed->maximum()));
        } else if (key == QStringLiteral("input.autoswitch")) {
            portAutoswitch->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("middle_mouse")) {
            setMouseUntrapBit(true, configBoolValue(value));
        } else if (key == QStringLiteral("magic_mouse")) {
            setMouseUntrapBit(false, configBoolValue(value));
        } else if (key == QStringLiteral("magic_mousecursor")) {
            magicMouseCursor->setCurrentText(magicMouseCursorText(value));
        } else if (key == QStringLiteral("absolute_mouse")) {
            if (value.compare(QStringLiteral("tablet"), Qt::CaseInsensitive) == 0) {
                virtualMouseDriver->setChecked(true);
                tabletMode->setCurrentText(QStringLiteral("Tablet emulation"));
            } else if (value.compare(QStringLiteral("mousehack"), Qt::CaseInsensitive) == 0) {
                virtualMouseDriver->setChecked(true);
                tabletMode->setCurrentText(QStringLiteral("-"));
            } else {
                virtualMouseDriver->setChecked(false);
                tabletMode->setCurrentText(QStringLiteral("-"));
            }
            updateMouseExtraState();
        } else if (key == QStringLiteral("tablet_library")) {
            tabletLibrary->setChecked(configBoolValue(value));
            updateMouseExtraState();
        } else if (key == QStringLiteral("gfx_display") || key == QStringLiteral("gfx_display_rtg")) {
            bool ok = false;
            const int index = value.toInt(&ok);
            if (ok) {
                selectHostDisplayByIndex(index);
            }
        } else if (key == QStringLiteral("gfx_display_name")
            || key == QStringLiteral("gfx_display_name_rtg")
            || key == QStringLiteral("gfx_display_friendlyname")
            || key == QStringLiteral("gfx_display_friendlyname_rtg")) {
            selectHostDisplayByName(value);
        } else if (key == QStringLiteral("gfx_width_windowed")) {
            windowWidth->setText(value);
        } else if (key == QStringLiteral("gfx_height_windowed")) {
            windowHeight->setText(value);
        } else if (key == QStringLiteral("gfx_width_fullscreen")) {
            fullscreenResolution->setProperty("winuae_width", value);
            if (value == QStringLiteral("native")) {
                fullscreenResolution->setCurrentText(QStringLiteral("Native"));
            } else {
                const QString currentHeight = fullscreenResolution->property("winuae_height").toString();
                if (!currentHeight.isEmpty() && currentHeight != QStringLiteral("native")) {
                    fullscreenResolution->setCurrentText(value + QStringLiteral("x") + currentHeight);
                }
            }
        } else if (key == QStringLiteral("gfx_height_fullscreen")) {
            fullscreenResolution->setProperty("winuae_height", value);
            const QString currentWidth = fullscreenResolution->property("winuae_width").toString();
            if (value == QStringLiteral("native") || currentWidth == QStringLiteral("native")) {
                fullscreenResolution->setCurrentText(QStringLiteral("Native"));
            } else if (!currentWidth.isEmpty()) {
                fullscreenResolution->setCurrentText(currentWidth + QStringLiteral("x") + value);
            }
        } else if (key == QStringLiteral("gfx_refreshrate")) {
            displayRefreshRate->setCurrentText(value == QStringLiteral("0") ? QStringLiteral("Default") : value);
        } else if (key == QStringLiteral("gfx_backbuffers")) {
            displayBufferCount->setCurrentText(value.toInt() >= 3 ? QStringLiteral("Triple") : QStringLiteral("Double"));
        } else if (key == QStringLiteral("gfx_resize_windowed")) {
            windowResize->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("gfx_fullscreen_amiga")) {
            nativeMode->setCurrentText(fullscreenModeText(value));
        } else if (key == QStringLiteral("gfx_fullscreen_picasso")) {
            rtgMode->setCurrentText(fullscreenModeText(value));
        } else if (key == QStringLiteral("gfx_vsync")) {
            nativeVsync->setProperty("winuae_vsync", value);
            nativeVsync->setCurrentText(nativeVsyncText(value, nativeVsync->property("winuae_vsyncmode").toString()));
        } else if (key == QStringLiteral("gfx_vsyncmode")) {
            nativeVsync->setProperty("winuae_vsyncmode", value);
            nativeVsync->setCurrentText(nativeVsyncText(nativeVsync->property("winuae_vsync").toString(), value));
        } else if (key == QStringLiteral("gfx_vsync_picasso")) {
            rtgVsync->setProperty("winuae_vsync", value);
            rtgVsync->setCurrentText(rtgVsyncText(value));
        } else if (key == QStringLiteral("gfx_vsyncmode_picasso")) {
            rtgVsync->setProperty("winuae_vsyncmode", value);
            if (rtgVsync->property("winuae_vsync").toString().compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0) {
                rtgVsync->setCurrentText(QStringLiteral("VSync (Busy wait)"));
            }
        } else if (key == QStringLiteral("gfx_frame_slices")) {
            nativeFrameSlices->setCurrentText(QString::number(qBound(1, value.toInt(), 4)));
        } else if (key == QStringLiteral("gfx_resolution")) {
            displayResolution->setCurrentText(value);
        } else if (key == QStringLiteral("gfx_overscanmode")) {
            displayOverscan->setCurrentText(overscanText(value));
        } else if (key == QStringLiteral("gfx_autoresolution")) {
            displayAutoResolution->setCurrentText(autoResolutionText(value.toInt()));
        } else if (key == QStringLiteral("gfx_framerate")) {
            displayFrameRate->setValue(qBound(displayFrameRate->minimum(), value.toInt(), displayFrameRate->maximum()));
        } else if (key == QStringLiteral("gfx_linemode")) {
            if (QAbstractButton *button = displayLineModeButtons->button(progressiveLineModeId(value))) {
                button->setChecked(true);
            }
            if (QAbstractButton *button = displayInterlacedLineModeButtons->button(interlacedLineModeId(value))) {
                button->setChecked(true);
            }
        } else if (key == QStringLiteral("gfx_center_horizontal")) {
            displayCenterHorizontal->setChecked(value.compare(QStringLiteral("none"), Qt::CaseInsensitive) != 0 && !value.isEmpty());
        } else if (key == QStringLiteral("gfx_center_vertical")) {
            displayCenterVertical->setChecked(value.compare(QStringLiteral("none"), Qt::CaseInsensitive) != 0 && !value.isEmpty());
        } else if (key == QStringLiteral("gfx_flickerfixer")) {
            displayFlickerFixer->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("gfx_lores_mode")) {
            displayLoresSmoothed->setChecked(value.compare(QStringLiteral("filtered"), Qt::CaseInsensitive) == 0);
        } else if (key == QStringLiteral("gfx_blacker_than_black")) {
            displayBlackerThanBlack->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("gfx_monochrome")) {
            displayMonochrome->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("gfx_autoresolution_vga")) {
            displayAutoResolutionVga->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("gfx_monitorblankdelay")) {
            displayResyncBlank->setChecked(value.toInt() > 0);
        } else if (key == QStringLiteral("gfx_keep_aspect")) {
            displayKeepAspect->setChecked(configBoolValue(value));
        } else if (key == QStringLiteral("gfx_ntscpixels")) {
            filterNtscPixels->setChecked(configBoolValue(value));
        } else if (applyFilterSetting(key, value)) {
        }
    }

    void saveConfig(const QString &path)
    {
        const QString expandedPath = expandedPathText(path);
        WinUaeQtConfig config = mergedConfig();
        QString error;
        if (!config.save(expandedPath, &error)) {
            QMessageBox::warning(this, windowTitle(), error);
            return;
        }
        loadedConfig = config;
        configPath->setText(expandedPath);
        configName->setCurrentText(QFileInfo(expandedPath).completeBaseName());
        refreshConfigList();
        status->setText(QStringLiteral("Saved %1").arg(expandedPath));
    }

    void startEmulator()
    {
        requestStart(false);
    }

    void requestStart(bool hardReset)
    {
        const WinUaeQtConfig config = mergedConfig();
        const QStringList validationErrors = config.validateForLaunch();
        if (!validationErrors.isEmpty()) {
            QMessageBox::warning(this, windowTitle(), validationErrors.join(QLatin1Char('\n')));
            navigation->setCurrentItem(navigation->topLevelItem(4));
            return;
        }

        result.status = WinUaeQtLauncherStatus::StartRequested;
        result.hardReset = hardReset;
        result.config = config;
        accept();
    }
};

static bool systemPrefersDarkMode()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
    return false;
#endif
}

static void applyApplicationColors(QApplication &app, bool dark)
{
    QPalette palette;
    if (dark) {
        palette.setColor(QPalette::Window, QColor(0x20, 0x20, 0x20));
        palette.setColor(QPalette::WindowText, QColor(0xf0, 0xf0, 0xf0));
        palette.setColor(QPalette::Base, QColor(0x12, 0x12, 0x12));
        palette.setColor(QPalette::AlternateBase, QColor(0x1a, 0x1a, 0x1a));
        palette.setColor(QPalette::ToolTipBase, QColor(0x2b, 0x2b, 0x2b));
        palette.setColor(QPalette::ToolTipText, QColor(0xf0, 0xf0, 0xf0));
        palette.setColor(QPalette::Text, QColor(0xf0, 0xf0, 0xf0));
        palette.setColor(QPalette::Button, QColor(0x2b, 0x2b, 0x2b));
        palette.setColor(QPalette::ButtonText, QColor(0xf0, 0xf0, 0xf0));
        palette.setColor(QPalette::BrightText, Qt::white);
        palette.setColor(QPalette::Highlight, QColor(0x33, 0x78, 0xd4));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x88, 0x88, 0x88));
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor(0x88, 0x88, 0x88));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x88, 0x88, 0x88));
        app.setPalette(palette);
        app.setStyleSheet(QStringLiteral(
            "QDialog, QWidget#page, QStackedWidget#pageStack { background: #202020; color: #f0f0f0; }"
            "QFrame#outerFrame { border: 1px solid #5a5a5a; background: #202020; }"
            "QTreeWidget, QListWidget, QTableWidget, QPlainTextEdit { background: #121212; color: #f0f0f0; border: 1px solid #5a5a5a; alternate-background-color: #1a1a1a; }"
            "QGroupBox { margin-top: 14px; padding: 9px 6px 6px 6px; color: #f0f0f0; }"
            "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 3px; font-size: 13px; }"
            "QPushButton { min-height: 20px; padding: 1px 10px; background: #2b2b2b; color: #f0f0f0; border: 1px solid #6a6a6a; }"
            "QPushButton:disabled { color: #888888; background: #242424; border-color: #4a4a4a; }"
            "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { min-height: 22px; background: #121212; color: #f0f0f0; border: 1px solid #5a5a5a; }"
            "QLabel#statusLine { padding-left: 6px; background: #202020; color: #f0f0f0; }"
            "QWidget:disabled { color: #888888; }"
        ));
        return;
    }

    palette.setColor(QPalette::Window, QColor(0xf0, 0xf0, 0xf0));
    palette.setColor(QPalette::WindowText, Qt::black);
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::AlternateBase, QColor(0xf7, 0xf7, 0xf7));
    palette.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xdc));
    palette.setColor(QPalette::ToolTipText, Qt::black);
    palette.setColor(QPalette::Text, Qt::black);
    palette.setColor(QPalette::Button, QColor(0xf0, 0xf0, 0xf0));
    palette.setColor(QPalette::ButtonText, Qt::black);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Highlight, QColor(0x33, 0x99, 0xff));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x80, 0x80, 0x80));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(0x80, 0x80, 0x80));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x80, 0x80, 0x80));
    app.setPalette(palette);

    app.setStyleSheet(QStringLiteral(
        "QDialog, QWidget#page, QStackedWidget#pageStack { background: #f0f0f0; color: #000000; }"
        "QFrame#outerFrame { border: 1px solid #808080; background: #f0f0f0; }"
        "QTreeWidget, QListWidget, QTableWidget, QPlainTextEdit { background: #ffffff; color: #000000; border: 1px solid #7f9db9; alternate-background-color: #f7f7f7; }"
        "QGroupBox { margin-top: 14px; padding: 9px 6px 6px 6px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 3px; font-size: 13px; }"
        "QPushButton { min-height: 20px; padding: 1px 10px; }"
        "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox { min-height: 22px; background: #ffffff; color: #000000; }"
        "QLabel#statusLine { padding-left: 6px; background: #f0f0f0; color: #000000; }"
        "QWidget:disabled { color: #808080; }"
    ));
}

static void setupApplicationStyle(QApplication &app)
{
    if (QStyle *style = QStyleFactory::create(QStringLiteral("Windows"))) {
        app.setStyle(style);
    } else if (QStyle *style = QStyleFactory::create(QStringLiteral("Fusion"))) {
        app.setStyle(style);
    }
    QString family = QStringLiteral("MS Sans Serif");
    const QStringList families = QFontDatabase::families();
    if (!families.contains(family)) {
        const QStringList fallbacks = {
            QStringLiteral("Microsoft Sans Serif"),
            QStringLiteral("Tahoma"),
            QStringLiteral("Arial")
        };
        for (const QString &candidate : fallbacks) {
            if (families.contains(candidate)) {
                family = candidate;
                break;
            }
        }
        if (!families.contains(family)) {
            family = app.font().family();
        }
    }
    QFont font(family);
    font.setPixelSize(12);
    app.setFont(font);
    applyApplicationColors(app, systemPrefersDarkMode());
}

static void armQtSmokeExit(QDialog &dialog, QApplication *app = nullptr)
{
    if (!qEnvironmentVariableIsSet("WINUAE_MACOS_APP_SMOKE")) {
        return;
    }
    QTimer::singleShot(750, &dialog, [&dialog, app]() {
        std::fputs(dialog.isVisible() ? "WINUAE_QT_SMOKE_WINDOW_VISIBLE\n" : "WINUAE_QT_SMOKE_WINDOW_NOT_VISIBLE\n", stdout);
        std::fflush(stdout);
        dialog.reject();
        if (app) {
            app->quit();
        }
    });
}

bool winUaeQtArgumentsSpecifyConfig(const QStringList &arguments)
{
    return !initialConfigPathFromArguments(arguments).isEmpty();
}

WinUaeQtLauncherResult runWinUaeQtLauncherForConfig(QApplication &app)
{
    return runWinUaeQtLauncherForConfig(app, QString());
}

WinUaeQtLauncherResult runWinUaeQtLauncherForConfig(QApplication &app, const QString &initialConfigPath)
{
    return runWinUaeQtLauncherForConfig(app, initialConfigPath, WinUaeQtHardwareInfoProvider());
}

WinUaeQtLauncherResult runWinUaeQtLauncherForConfig(QApplication &app, const QString &initialConfigPath, const WinUaeQtHardwareInfoProvider &hardwareProvider)
{
    setupApplicationStyle(app);
    WinUaeQtDialog dialog(
        nullptr,
        initialConfigPath.isEmpty() ? initialConfigPathFromArguments(app.arguments()) : initialConfigPath,
        hardwareProvider);
    if (WinUaeQtApplication *qtApp = dynamic_cast<WinUaeQtApplication *>(&app)) {
        qtApp->setConfigOpenHandler([&dialog](const QString &path) {
            dialog.openConfigFile(path);
        });
    }
    armQtSmokeExit(dialog);
    if (dialog.exec() == QDialog::Accepted) {
        return dialog.launcherResult();
    }

    WinUaeQtLauncherResult result;
    result.status = WinUaeQtLauncherStatus::Canceled;
    return result;
}

WinUaeQtLauncherResult runWinUaeQtLauncherForConfig(int argc, char **argv)
{
    return runWinUaeQtLauncherForConfig(argc, argv, QString());
}

WinUaeQtLauncherResult runWinUaeQtLauncherForConfig(int argc, char **argv, const QString &initialConfigPath)
{
    return runWinUaeQtLauncherForConfig(argc, argv, initialConfigPath, WinUaeQtHardwareInfoProvider());
}

WinUaeQtLauncherResult runWinUaeQtLauncherForConfig(int argc, char **argv, const QString &initialConfigPath, const WinUaeQtHardwareInfoProvider &hardwareProvider)
{
    WinUaeQtApplication app(argc, argv);
    return runWinUaeQtLauncherForConfig(app, initialConfigPath, hardwareProvider);
}

static QString runtimeDialogDirectory(const QString &initialPath)
{
    return fileDialogInitialDirectory(initialPath);
}

static QString runtimeDialogSavePath(const QString &initialPath, const QString &fallbackName)
{
    return fileDialogInitialSavePath(initialPath, fallbackName);
}

WinUaeQtRuntimeFileDialogResult runWinUaeQtRuntimeFileDialog(QApplication &app, int shortcut, const QString &initialPath)
{
    setupApplicationStyle(app);

    QString selected;
    if (shortcut >= 0 && shortcut < 4) {
        selected = QFileDialog::getOpenFileName(
            nullptr,
            QStringLiteral("Select disk image for DF%1:").arg(shortcut),
            runtimeDialogDirectory(initialPath),
            amigaDiskImageFilter());
    } else if (shortcut == 4) {
        selected = QFileDialog::getOpenFileName(
            nullptr,
            QStringLiteral("Restore state"),
            runtimeDialogDirectory(initialPath),
            QStringLiteral("WinUAE state files (*.uss);;All files (*)"));
    } else if (shortcut == 5) {
        selected = QFileDialog::getSaveFileName(
            nullptr,
            QStringLiteral("Save state"),
            runtimeDialogSavePath(initialPath, QStringLiteral("state.uss")),
            QStringLiteral("WinUAE state files (*.uss);;All files (*)"));
        if (!selected.isEmpty() && QFileInfo(selected).suffix().isEmpty()) {
            selected += QStringLiteral(".uss");
        }
    } else if (shortcut == 6) {
        selected = QFileDialog::getOpenFileName(
            nullptr,
            QStringLiteral("Select CD image"),
            runtimeDialogDirectory(initialPath),
            cdImageFilter());
    }

    WinUaeQtRuntimeFileDialogResult result;
    result.accepted = !selected.isEmpty();
    result.path = selected;
    return result;
}

WinUaeQtRuntimeFileDialogResult runWinUaeQtRuntimeFileDialog(int argc, char **argv, int shortcut, const QString &initialPath)
{
    WinUaeQtApplication app(argc, argv);
    return runWinUaeQtRuntimeFileDialog(app, shortcut, initialPath);
}

static int showMessageBox(int flags, const QString &message)
{
    QWidget *parent = QApplication::activeModalWidget();
    if (!parent) {
        parent = QApplication::activeWindow();
    }

    QMessageBox box(QMessageBox::Warning, QStringLiteral("WinUAE"), message, QMessageBox::NoButton, parent);
    box.setWindowModality(Qt::ApplicationModal);

    QPushButton *okButton = nullptr;
    QPushButton *yesButton = nullptr;
    QPushButton *noButton = nullptr;
    QPushButton *cancelButton = nullptr;
    if (flags == 0) {
        okButton = box.addButton(QMessageBox::Ok);
        box.setDefaultButton(okButton);
    } else {
        yesButton = box.addButton(QMessageBox::Yes);
        noButton = box.addButton(QMessageBox::No);
        box.setDefaultButton(yesButton);
        if (flags == 2) {
            cancelButton = box.addButton(QMessageBox::Cancel);
            box.setEscapeButton(cancelButton);
        } else {
            box.setEscapeButton(noButton);
        }
    }

    box.exec();
    QAbstractButton *clicked = box.clickedButton();
    if (clicked == okButton) {
        return 0;
    }
    if (clicked == yesButton) {
        return 1;
    }
    if (clicked == noButton) {
        return 2;
    }
    if (clicked == cancelButton) {
        return -1;
    }
    return 0;
}

int runWinUaeQtMessageBox(QApplication &app, int flags, const QString &message)
{
    setupApplicationStyle(app);
    return showMessageBox(flags, message);
}

int runWinUaeQtMessageBox(int argc, char **argv, int flags, const QString &message)
{
    if (QApplication *app = qobject_cast<QApplication *>(QApplication::instance())) {
        return runWinUaeQtMessageBox(*app, flags, message);
    }

    WinUaeQtApplication app(argc, argv);
    return runWinUaeQtMessageBox(app, flags, message);
}
