#include "drive_sound_sets.h"

#include <QDir>
#include <QFileInfo>

QStringList winUaeQtDiscoverDriveSoundSets(const QString &directory)
{
    static const QString prefix = QStringLiteral("drive_click_");
    static const QString suffix = QStringLiteral(".wav");

    QStringList sets;
    const QFileInfoList entries = QDir(directory).entryInfoList(
        QDir::Files | QDir::Readable,
        QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &entry : entries) {
        const QString name = entry.fileName();
        if (!name.startsWith(prefix, Qt::CaseSensitive)
            || !name.endsWith(suffix, Qt::CaseSensitive)
            || name.size() <= prefix.size() + suffix.size()) {
            continue;
        }
        sets.append(name.mid(prefix.size(), name.size() - prefix.size() - suffix.size()));
    }
    return sets;
}
