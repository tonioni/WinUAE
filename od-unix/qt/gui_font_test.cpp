#include "launcher.h"

#include <QtWidgets>
#include <QFileOpenEvent>

static bool requireFont(const char *label, const QFont &expected)
{
    if (qApp->font() == expected) {
        return true;
    }
    qWarning() << label << "expected" << expected << "got" << qApp->font();
    return false;
}

int main(int argc, char **argv)
{
    QTemporaryDir temp;
    if (!temp.isValid()) {
        return 1;
    }
    qunsetenv("WINUAE_QT_DESKTOP_STYLE");
    qunsetenv("WINUAE_MACOS_APP_SMOKE");
    setWinUaeQtDesktopStyle(true);

    WinUaeQtConfig defaults;
    WinUaeQtConfig custom;
    const QFont customFont(QStringLiteral("Courier"), 26);
    custom.setValue(QStringLiteral("unix.ui.gui_font"), customFont.toString());
    const QString defaultPath = temp.filePath(QStringLiteral("default.uae"));
    if (!defaults.save(defaultPath)) {
        return 1;
    }

    qputenv("WINUAE_QT_CONFIG_ROUNDTRIP_OUT",
        temp.filePath(QStringLiteral("export.uae")).toLocal8Bit());
    const auto open = [&](const WinUaeQtConfig &config) {
        return runWinUaeQtLauncherForConfig(argc, argv, config, QString(), {});
    };
    open(defaults);
    const QFont desktopFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    bool ok = requireFont("initial desktop font", desktopFont);
    QApplication *app = qApp;
    open(custom);
    ok = requireFont("config font override", customFont) && ok;
    open(defaults);
    ok = requireFont("reopened settings without font override", desktopFont) && ok;
    if (qApp != app) {
        qWarning() << "QApplication was recreated";
        ok = false;
    }

    /* Load a second config through the normal FileOpen handler while the
     * same settings window is open. */
    qunsetenv("WINUAE_QT_CONFIG_ROUNDTRIP_OUT");
    QTimer::singleShot(0, [&]() {
        QDialog *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) {
            qWarning() << "settings dialog was not opened";
            app->exit(1);
            ok = false;
            return;
        }
        ok = requireFont("open settings font override", customFont) && ok;
        QFileOpenEvent event(defaultPath);
        QApplication::sendEvent(app, &event);
        ok = requireFont("loaded config without font override", desktopFont) && ok;
        dialog->reject();
    });
    open(custom);
    shutdownWinUaeQtApplication();
    return ok ? 0 : 1;
}
