#include "drive_sound_sets.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

static bool createFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("test") == 4;
}

static bool requireEqual(const QStringList &actual, const QStringList &expected)
{
    if (actual == expected) {
        return true;
    }
    qWarning().noquote() << "drive sound sets expected" << expected << "got" << actual;
    return false;
}

int main()
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        qWarning() << "failed to create temporary directory";
        return 1;
    }

    const QDir directory(temporary.path());
    if (!createFile(directory.filePath(QStringLiteral("drive_click_Zeta.wav")))
        || !createFile(directory.filePath(QStringLiteral("drive_click_Alpha.wav")))
        || !createFile(directory.filePath(QStringLiteral("drive_click_Beta.wav")))
        || !createFile(directory.filePath(QStringLiteral("drive_click_Uppercase.WAV")))
        || !createFile(directory.filePath(QStringLiteral("drive_spin_Alpha.wav")))
        || !createFile(directory.filePath(QStringLiteral("DRIVE_CLICK_wrong.wav")))
        || !createFile(directory.filePath(QStringLiteral("drive_click_.wav")))
        || !createFile(directory.filePath(QStringLiteral("drive_click_backup.wav.old")))
        || !QDir().mkdir(directory.filePath(QStringLiteral("drive_click_directory.wav")))) {
        qWarning() << "failed to create drive sound discovery fixtures";
        return 1;
    }

    return requireEqual(
        winUaeQtDiscoverDriveSoundSets(temporary.path()),
        { QStringLiteral("Alpha"), QStringLiteral("Beta"), QStringLiteral("Zeta") })
        ? 0
        : 1;
}
