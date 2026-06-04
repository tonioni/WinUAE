#include "path_utils.h"

#include <QByteArray>
#include <QDebug>

static bool requireEqual(const char *label, const QString &actual, const QString &expected)
{
    if (actual == expected) {
        return true;
    }
    qWarning().noquote() << label << "expected" << expected << "got" << actual;
    return false;
}

int main()
{
    bool ok = true;
    qputenv("HOME", QByteArray("/tmp/winuae-qt-home"));
    qputenv("WINUAE_QT_TEST_PATH", QByteArray("/tmp/winuae-qt-env"));
    qputenv("WINUAE_QT_TEST_SUFFIX", QByteArray("-suffix"));
    qunsetenv("WINUAE_QT_TEST_MISSING");

    ok = requireEqual("home directory", winUaeQtExpandUnixPath(QStringLiteral("~")), QStringLiteral("/tmp/winuae-qt-home")) && ok;
    ok = requireEqual("home subpath", winUaeQtExpandUnixPath(QStringLiteral("~/roms/kick.rom")), QStringLiteral("/tmp/winuae-qt-home/roms/kick.rom")) && ok;
    ok = requireEqual("plain environment", winUaeQtExpandUnixPath(QStringLiteral("$WINUAE_QT_TEST_PATH/kick.rom")), QStringLiteral("/tmp/winuae-qt-env/kick.rom")) && ok;
    ok = requireEqual("braced environment", winUaeQtExpandUnixPath(QStringLiteral("${WINUAE_QT_TEST_PATH}/kick.rom")), QStringLiteral("/tmp/winuae-qt-env/kick.rom")) && ok;
    ok = requireEqual("adjacent environment", winUaeQtExpandUnixPath(QStringLiteral("$WINUAE_QT_TEST_PATH${WINUAE_QT_TEST_SUFFIX}")), QStringLiteral("/tmp/winuae-qt-env-suffix")) && ok;
    ok = requireEqual("missing environment", winUaeQtExpandUnixPath(QStringLiteral("$WINUAE_QT_TEST_MISSING/kick.rom")), QStringLiteral("$WINUAE_QT_TEST_MISSING/kick.rom")) && ok;
    ok = requireEqual("malformed brace", winUaeQtExpandUnixPath(QStringLiteral("${WINUAE_QT_TEST_PATH/kick.rom")), QStringLiteral("${WINUAE_QT_TEST_PATH/kick.rom")) && ok;
    ok = requireEqual("literal dollar", winUaeQtExpandUnixPath(QStringLiteral("disk$")), QStringLiteral("disk$")) && ok;
    ok = requireEqual("trimmed boundary", winUaeQtExpandedPathText(QStringLiteral("  $WINUAE_QT_TEST_PATH/config.uae  ")), QStringLiteral("/tmp/winuae-qt-env/config.uae")) && ok;
    ok = requireEqual("default data path", winUaeQtDefaultDataPath(), QStringLiteral("$HOME/Documents/WinUAE")) && ok;
    ok = requireEqual("default data subpath", winUaeQtDefaultDataSubPath(QStringLiteral("Configuration")), QStringLiteral("$HOME/Documents/WinUAE/Configuration")) && ok;
    ok = requireEqual("expanded default data path", winUaeQtExpandedPathText(winUaeQtDefaultDataSubPath(QStringLiteral("Configuration"))), QStringLiteral("/tmp/winuae-qt-home/Documents/WinUAE/Configuration")) && ok;
    ok = requireEqual("default file dialog path", winUaeQtDefaultFileDialogPath(), QStringLiteral("/tmp/winuae-qt-home/Documents/WinUAE")) && ok;
    ok = requireEqual("empty file dialog path", winUaeQtFileDialogInitialPath(QString()), QStringLiteral("/tmp/winuae-qt-home/Documents/WinUAE")) && ok;
    ok = requireEqual("blank file dialog path", winUaeQtFileDialogInitialPath(QStringLiteral("  ")), QStringLiteral("/tmp/winuae-qt-home/Documents/WinUAE")) && ok;
    ok = requireEqual("empty file dialog directory", winUaeQtFileDialogInitialDirectory(QString()), QStringLiteral("/tmp/winuae-qt-home/Documents/WinUAE")) && ok;
    ok = requireEqual("configured file dialog path", winUaeQtFileDialogInitialPath(QStringLiteral("$WINUAE_QT_TEST_PATH/kick.rom")), QStringLiteral("/tmp/winuae-qt-env/kick.rom")) && ok;
    ok = requireEqual("configured file dialog directory", winUaeQtFileDialogInitialDirectory(QStringLiteral("$WINUAE_QT_TEST_PATH/kick.rom")), QStringLiteral("/tmp/winuae-qt-env")) && ok;
    ok = requireEqual("file dialog save fallback", winUaeQtFileDialogInitialSavePath(QString(), QStringLiteral("state.uss")), QStringLiteral("/tmp/winuae-qt-home/Documents/WinUAE/state.uss")) && ok;
    ok = requireEqual("named home unsupported", winUaeQtExpandUnixPath(QStringLiteral("~other/roms")), QStringLiteral("~other/roms")) && ok;

    return ok ? 0 : 1;
}
