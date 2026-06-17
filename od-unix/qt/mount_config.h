#pragma once

#include <QString>
#include <QStringList>

struct WinUaeQtMountEntry {
    QString kind;
    QString device;
    QString volume;
    QString path;
    QString rawConfig;
    QString hardfileGeometry;
    QString hardfileTail;
    bool readOnly = false;
    int bootPri = 0;
    int emuUnit = 0;
};

QString winUaeQtConfigAccessValue(bool readOnly);
QString winUaeQtConfigEscapeMin(QString value);
QStringList winUaeQtConfigFieldList(QString value);
QString winUaeQtConfigJoinFields(const QStringList &fields);
QString winUaeQtSanitizedAmigaName(QString value, const QString &fallback, bool uppercase);
QString winUaeQtDefaultVolumeName(const QString &path);
bool winUaeQtHardfileIsRdb(const WinUaeQtMountEntry &entry);
bool winUaeQtHardfileUsesNonUaeController(const WinUaeQtMountEntry &entry);
bool parseWinUaeQtUaehfMountValue(const QString &value, WinUaeQtMountEntry *entry);
bool parseWinUaeQtFilesystem2MountValue(const QString &value, WinUaeQtMountEntry *entry);
bool parseWinUaeQtHardfile2MountValue(const QString &value, WinUaeQtMountEntry *entry);
QString serializeWinUaeQtFilesystem2MountValue(const WinUaeQtMountEntry &entry);
QString serializeWinUaeQtHardfile2MountValue(const WinUaeQtMountEntry &entry);
QString serializeWinUaeQtUaehfDirectoryMountValue(const WinUaeQtMountEntry &entry);
QString serializeWinUaeQtUaehfHardfileMountValue(const WinUaeQtMountEntry &entry);
QString serializeWinUaeQtUaehfCdMountValue(const WinUaeQtMountEntry &entry);
QString serializeWinUaeQtUaehfTapeMountValue(const WinUaeQtMountEntry &entry);
