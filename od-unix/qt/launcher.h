#pragma once

#include "config.h"

#include <QVector>

class QApplication;

enum class WinUaeQtLauncherStatus {
    Canceled,
    StartRequested,
    QuitRequested,
    RestartRequested,
    Error
};

struct WinUaeQtLauncherResult {
    WinUaeQtLauncherStatus status = WinUaeQtLauncherStatus::Canceled;
    bool hardReset = false;
    int exitCode = 0;
    QString error;
    QString configPath;
    WinUaeQtConfig config;
};

struct WinUaeQtRuntimeFileDialogResult {
    bool accepted = false;
    QString path;
};

struct WinUaeQtHardwareBoard {
    int index = -1;
    QString type;
    QString name;
    QString start;
    QString end;
    QString size;
    QString id;
    bool movable = false;
};

enum class WinUaeQtBoardSettingType {
    CheckBox,
    Multi,
    String
};

enum WinUaeQtExpansionCategoryFlag {
    WinUaeQtExpansionCategoryInternal = 1 << 0,
    WinUaeQtExpansionCategoryScsi = 1 << 1,
    WinUaeQtExpansionCategoryIde = 1 << 2,
    WinUaeQtExpansionCategorySasi = 1 << 3,
    WinUaeQtExpansionCategoryCustom = 1 << 4,
    WinUaeQtExpansionCategoryPciBridge = 1 << 5,
    WinUaeQtExpansionCategoryX86Bridge = 1 << 6,
    WinUaeQtExpansionCategoryRtg = 1 << 7,
    WinUaeQtExpansionCategorySound = 1 << 8,
    WinUaeQtExpansionCategoryNet = 1 << 9,
    WinUaeQtExpansionCategoryFloppy = 1 << 10,
    WinUaeQtExpansionCategoryX86Expansion = 1 << 11
};

struct WinUaeQtBoardSetting {
    QString display;
    QString configValue;
    WinUaeQtBoardSettingType type = WinUaeQtBoardSettingType::CheckBox;
    QStringList multiDisplays;
    QStringList multiValues;
};

struct WinUaeQtBoardSubtype {
    QString display;
    QString configValue;
    int deviceFlags = 0;
    bool hasRomTypeOverride = false;
    bool noRomFile = false;
};

struct WinUaeQtExpansionBoardCatalogItem {
    QString key;
    QString display;
    int deviceFlags = 0;
    int categoryMask = 0;
    int zorro = 0;
    bool singleOnly = false;
    bool dma24Bit = false;
    bool pcmcia = false;
    bool autobootJumper = false;
    bool idJumper = false;
    bool noRomFile = false;
    bool clockPort = false;
    int extraHdPorts = 0;
    QVector<WinUaeQtBoardSubtype> subtypes;
    QVector<WinUaeQtBoardSetting> settings;
};

struct WinUaeQtCpuBoardCatalogItem {
    QString type;
    QString display;
    QString configValue;
    int maxMemoryMb = 0;
    bool ppc = false;
    QVector<WinUaeQtBoardSetting> settings;
};

struct WinUaeQtRtgBoardCatalogItem {
    QString display;
    QString configValue;
    int configType = 0;
};

struct WinUaeQtBoardCatalog {
    QVector<WinUaeQtExpansionBoardCatalogItem> expansionBoards;
    QVector<WinUaeQtCpuBoardCatalogItem> cpuBoards;
    QVector<WinUaeQtRtgBoardCatalogItem> rtgBoards;
};

struct WinUaeQtHardwareInfoProvider {
    void *context = nullptr;
    bool (*hostSettingGet)(void *context, const char *key, char *out, int outLen) = nullptr;
    void (*hostSettingSet)(void *context, const char *key, const char *value) = nullptr;
    void (*hostSettingsFlush)(void *context) = nullptr;
    WinUaeQtBoardCatalog (*boardCatalog)(void *context) = nullptr;
    bool (*applyConfig)(void *context, const WinUaeQtConfig &config) = nullptr;
    QVector<WinUaeQtHardwareBoard> (*boards)(void *context) = nullptr;
    bool (*customOrder)(void *context) = nullptr;
    void (*setCustomOrder)(void *context, bool enabled) = nullptr;
    bool (*canMove)(void *context, int index, int direction) = nullptr;
    int (*move)(void *context, int index, int direction) = nullptr;
    WinUaeQtConfig::Settings (*orderSettings)(void *context) = nullptr;
    void (*pollHostWindowEvents)(void *context) = nullptr;
    void (*saveScreenshot)(void *context) = nullptr;
    bool (*sampleRipperEnabled)(void *context) = nullptr;
    void (*setSampleRipperEnabled)(void *context, bool enabled) = nullptr;
    bool (*statePlaybackEnabled)(void *context) = nullptr;
    bool (*stateRecordingEnabled)(void *context) = nullptr;
    bool (*canSaveStateRecording)(void *context) = nullptr;
    bool (*setStatePlayback)(void *context, bool enabled, const char *path) = nullptr;
    void (*toggleStateRecording)(void *context) = nullptr;
    bool (*saveStateRecording)(void *context, const char *path) = nullptr;
    void (*runProWizard)(void *context) = nullptr;
};

bool winUaeQtArgumentsSpecifyConfig(const QStringList &arguments);
QString winUaeQtInitialConfigPathFromArguments(const QStringList &arguments);
WinUaeQtLauncherResult runWinUaeQtLauncherForConfig(QApplication &app);
WinUaeQtLauncherResult runWinUaeQtLauncherForConfig(QApplication &app, const QString &initialConfigPath);
WinUaeQtLauncherResult runWinUaeQtLauncherForConfig(QApplication &app, const QString &initialConfigPath, const WinUaeQtHardwareInfoProvider &hardwareProvider);
WinUaeQtLauncherResult runWinUaeQtLauncherForConfig(QApplication &app, const QString &initialConfigPath, const QString &displayConfigPath, const WinUaeQtHardwareInfoProvider &hardwareProvider);
WinUaeQtLauncherResult runWinUaeQtLauncherForConfig(QApplication &app, const WinUaeQtConfig &initialConfig, const QString &displayConfigPath, const WinUaeQtHardwareInfoProvider &hardwareProvider);
WinUaeQtLauncherResult runWinUaeQtLauncherForConfig(int argc, char **argv);
WinUaeQtLauncherResult runWinUaeQtLauncherForConfig(int argc, char **argv, const QString &initialConfigPath);
WinUaeQtLauncherResult runWinUaeQtLauncherForConfig(int argc, char **argv, const QString &initialConfigPath, const WinUaeQtHardwareInfoProvider &hardwareProvider);
WinUaeQtLauncherResult runWinUaeQtLauncherForConfig(int argc, char **argv, const QString &initialConfigPath, const QString &displayConfigPath, const WinUaeQtHardwareInfoProvider &hardwareProvider);
WinUaeQtLauncherResult runWinUaeQtLauncherForConfig(int argc, char **argv, const WinUaeQtConfig &initialConfig, const QString &displayConfigPath, const WinUaeQtHardwareInfoProvider &hardwareProvider);
WinUaeQtRuntimeFileDialogResult runWinUaeQtRuntimeFileDialog(QApplication &app, int shortcut, const QString &initialPath);
WinUaeQtRuntimeFileDialogResult runWinUaeQtRuntimeFileDialog(int argc, char **argv, int shortcut, const QString &initialPath);
int runWinUaeQtMessageBox(QApplication &app, int flags, const QString &message);
int runWinUaeQtMessageBox(int argc, char **argv, int flags, const QString &message);
