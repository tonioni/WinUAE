#include "mount_config.h"

#include <QFileInfo>

QString winUaeQtConfigAccessValue(bool readOnly)
{
    return readOnly ? QStringLiteral("ro") : QStringLiteral("rw");
}

QString winUaeQtConfigEscapeMin(QString value)
{
    const bool needsQuote = value.contains(QLatin1Char(','))
        || value.contains(QLatin1Char('"'))
        || value.startsWith(QLatin1Char(' '))
        || value.endsWith(QLatin1Char(' '));
    if (!needsQuote) {
        return value;
    }
    value.replace(QStringLiteral("\""), QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(value);
}

QString winUaeQtSanitizedAmigaName(QString value, const QString &fallback, bool uppercase)
{
    value = value.trimmed();
    if (value.isEmpty()) {
        value = fallback;
    }
    value.replace(QLatin1Char(':'), QLatin1Char('_'));
    value.replace(QLatin1Char(','), QLatin1Char('_'));
    if (uppercase) {
        value = value.toUpper();
    }
    return value.left(30);
}

QString winUaeQtDefaultVolumeName(const QString &path)
{
    const QString baseName = QFileInfo(path).completeBaseName();
    return winUaeQtSanitizedAmigaName(baseName, QStringLiteral("Hard Disk"), false);
}

static bool takeConfigField(QString *input, QChar delimiter, QString *field, bool requireDelimiter)
{
    if (!input || !field) {
        return false;
    }

    if (input->startsWith(QLatin1Char('"'))) {
        QString output;
        bool escaped = false;
        bool closed = false;
        int index = 1;
        for (; index < input->size(); index++) {
            const QChar c = input->at(index);
            if (escaped) {
                output.append(c);
                escaped = false;
            } else if (c == QLatin1Char('\\')) {
                escaped = true;
            } else if (c == QLatin1Char('"')) {
                index++;
                closed = true;
                break;
            } else {
                output.append(c);
            }
        }
        if (!closed) {
            return false;
        }
        if (index < input->size()) {
            if (input->at(index) != delimiter) {
                return false;
            }
            *input = input->mid(index + 1);
        } else if (requireDelimiter) {
            return false;
        } else {
            input->clear();
        }
        *field = output;
        return true;
    }

    const int delimiterIndex = input->indexOf(delimiter);
    if (delimiterIndex < 0) {
        if (requireDelimiter) {
            return false;
        }
        *field = *input;
        input->clear();
        return true;
    }

    *field = input->left(delimiterIndex);
    *input = input->mid(delimiterIndex + 1);
    return true;
}

QStringList winUaeQtConfigFieldList(QString value)
{
    QStringList fields;
    while (!value.isEmpty()) {
        QString field;
        if (!takeConfigField(&value, QLatin1Char(','), &field, false)) {
            return fields;
        }
        fields.append(field);
    }
    return fields;
}

QString winUaeQtConfigJoinFields(const QStringList &fields)
{
    QStringList escapedFields;
    escapedFields.reserve(fields.size());
    for (const QString &field : fields) {
        escapedFields.append(winUaeQtConfigEscapeMin(field));
    }
    return escapedFields.join(QLatin1Char(','));
}

static QString hardfileControllerValue(const WinUaeQtMountEntry &entry)
{
    QString tail = entry.hardfileTail;
    if (!tail.startsWith(QLatin1Char(','))) {
        tail.prepend(QLatin1Char(','));
    }
    const QStringList fields = winUaeQtConfigFieldList(tail);
    return fields.value(1, QStringLiteral("uae0")).trimmed();
}

bool winUaeQtHardfileIsRdb(const WinUaeQtMountEntry &entry)
{
    if (entry.hardfileGeometry.trimmed().isEmpty()) {
        return false;
    }
    const QStringList geometry = entry.hardfileGeometry.split(QLatin1Char(','));
    return geometry.value(0).toInt() == 0
        && geometry.value(1).toInt() == 0
        && geometry.value(2).toInt() == 0;
}

bool winUaeQtHardfileUsesNonUaeController(const WinUaeQtMountEntry &entry)
{
    const QString controller = hardfileControllerValue(entry).toLower();
    return controller.startsWith(QStringLiteral("ide"))
        || controller.startsWith(QStringLiteral("scsi"))
        || controller.startsWith(QStringLiteral("custom"));
}

static bool parseAccessValue(const QString &value, bool *readOnly)
{
    if (value.compare(QStringLiteral("ro"), Qt::CaseInsensitive) == 0) {
        *readOnly = true;
        return true;
    }
    if (value.compare(QStringLiteral("rw"), Qt::CaseInsensitive) == 0) {
        *readOnly = false;
        return true;
    }
    return false;
}

static bool parseDirectoryMountValue(QString value, WinUaeQtMountEntry *entry)
{
    QString access;
    QString device;
    QString volume;
    QString path;
    QString bootPri;
    if (!takeConfigField(&value, QLatin1Char(','), &access, true)
        || !takeConfigField(&value, QLatin1Char(':'), &device, true)
        || !takeConfigField(&value, QLatin1Char(':'), &volume, true)
        || !takeConfigField(&value, QLatin1Char(','), &path, true)
        || !takeConfigField(&value, QLatin1Char(','), &bootPri, false)
        || !parseAccessValue(access, &entry->readOnly)) {
        return false;
    }
    entry->kind = QStringLiteral("dir");
    entry->device = device;
    entry->volume = volume;
    entry->path = path;
    entry->bootPri = bootPri.toInt();
    return true;
}

static bool parseHardfileMountValue(QString value, WinUaeQtMountEntry *entry)
{
    QString access;
    QString device;
    QString path;
    if (!takeConfigField(&value, QLatin1Char(','), &access, true)
        || !takeConfigField(&value, QLatin1Char(':'), &device, true)
        || !takeConfigField(&value, QLatin1Char(','), &path, true)
        || !parseAccessValue(access, &entry->readOnly)) {
        return false;
    }
    QString sectors;
    QString surfaces;
    QString reserved;
    QString blocksize;
    QString bootPri;
    if (takeConfigField(&value, QLatin1Char(','), &sectors, true)
        && takeConfigField(&value, QLatin1Char(','), &surfaces, true)
        && takeConfigField(&value, QLatin1Char(','), &reserved, true)
        && takeConfigField(&value, QLatin1Char(','), &blocksize, true)
        && takeConfigField(&value, QLatin1Char(','), &bootPri, false)) {
        entry->hardfileGeometry = QStringLiteral("%1,%2,%3,%4").arg(sectors, surfaces, reserved, blocksize);
        entry->bootPri = bootPri.toInt();
        entry->hardfileTail = value;
    }
    entry->kind = QStringLiteral("hdf");
    entry->device = device;
    entry->path = path;
    return true;
}

static bool parseUnitSuffix(const QString &kind, const QString &prefix, int *unit)
{
    if (!unit || !kind.startsWith(prefix, Qt::CaseInsensitive)) {
        return false;
    }

    const QString suffix = kind.mid(prefix.size());
    if (suffix.isEmpty()) {
        *unit = 0;
        return true;
    }
    if (suffix.size() != 1 || !suffix.at(0).isDigit()) {
        return false;
    }
    *unit = suffix.toInt();
    return true;
}

bool parseWinUaeQtUaehfMountValue(const QString &value, WinUaeQtMountEntry *entry)
{
    QString rest = value;
    QString kind;
    int unit = 0;
    if (!takeConfigField(&rest, QLatin1Char(','), &kind, true)) {
        return false;
    }
    if (kind.compare(QStringLiteral("dir"), Qt::CaseInsensitive) == 0) {
        return parseDirectoryMountValue(rest, entry);
    }
    if (kind.compare(QStringLiteral("hdf"), Qt::CaseInsensitive) == 0) {
        if (!parseHardfileMountValue(rest, entry)) {
            return false;
        }
        entry->rawConfig = rest;
        return true;
    }
    if (parseUnitSuffix(kind, QStringLiteral("cd"), &unit)) {
        if (!parseHardfileMountValue(rest, entry)) {
            return false;
        }
        entry->kind = QStringLiteral("cd");
        entry->emuUnit = unit;
        entry->readOnly = true;
        entry->rawConfig = rest;
        if (entry->hardfileGeometry.isEmpty()) {
            entry->hardfileGeometry = QStringLiteral("0,0,0,2048");
        }
        return true;
    }
    if (parseUnitSuffix(kind, QStringLiteral("tape"), &unit)) {
        if (!parseHardfileMountValue(rest, entry)) {
            return false;
        }
        entry->kind = QStringLiteral("tape");
        entry->emuUnit = unit;
        entry->rawConfig = rest;
        if (entry->hardfileGeometry.isEmpty()) {
            entry->hardfileGeometry = QStringLiteral("0,0,0,512");
        }
        return true;
    }
    return false;
}

bool parseWinUaeQtFilesystem2MountValue(const QString &value, WinUaeQtMountEntry *entry)
{
    return parseDirectoryMountValue(value, entry);
}

bool parseWinUaeQtHardfile2MountValue(const QString &value, WinUaeQtMountEntry *entry)
{
    if (!parseHardfileMountValue(value, entry)) {
        return false;
    }
    entry->rawConfig = value;
    return true;
}

QString serializeWinUaeQtFilesystem2MountValue(const WinUaeQtMountEntry &entry)
{
    return QStringLiteral("%1,%2:%3:%4,%5")
        .arg(winUaeQtConfigAccessValue(entry.readOnly),
             winUaeQtSanitizedAmigaName(entry.device, QStringLiteral("DH0"), true),
             winUaeQtSanitizedAmigaName(entry.volume, QStringLiteral("Hard Disk"), false),
             winUaeQtConfigEscapeMin(entry.path),
             QString::number(entry.bootPri));
}

QString serializeWinUaeQtHardfile2MountValue(const WinUaeQtMountEntry &entry)
{
    const QString geometry = entry.hardfileGeometry.isEmpty() ? QStringLiteral("32,1,2,512") : entry.hardfileGeometry;
    const QString tail = entry.hardfileGeometry.isEmpty() && entry.hardfileTail.isEmpty() ? QStringLiteral(",uae0") : entry.hardfileTail;
    const bool controllerBackedRdb = winUaeQtHardfileIsRdb(entry)
        && winUaeQtHardfileUsesNonUaeController(entry);
    const QString device = controllerBackedRdb
        ? QString()
        : winUaeQtSanitizedAmigaName(entry.device, QStringLiteral("DH0"), true);
    const int bootPri = controllerBackedRdb ? 0 : entry.bootPri;
    QString value = QStringLiteral("%1,%2:%3,%4,%5")
        .arg(winUaeQtConfigAccessValue(entry.readOnly),
             device,
             winUaeQtConfigEscapeMin(entry.path),
             geometry,
             QString::number(bootPri));
    if (!tail.isEmpty()) {
        value += QStringLiteral(",") + tail;
    }
    return value;
}

static QString serializeWinUaeQtHardfileLikeMountValue(
    const WinUaeQtMountEntry &entry,
    const QString &fallbackDevice,
    const QString &defaultGeometry,
    const QString &defaultTail)
{
    const QString geometry = entry.hardfileGeometry.isEmpty() ? defaultGeometry : entry.hardfileGeometry;
    const QString tail = entry.hardfileTail.isEmpty() ? defaultTail : entry.hardfileTail;
    QString value = QStringLiteral("%1,%2:%3,%4,%5")
        .arg(winUaeQtConfigAccessValue(entry.readOnly),
             winUaeQtSanitizedAmigaName(entry.device, fallbackDevice, true),
             winUaeQtConfigEscapeMin(entry.path),
             geometry,
             QString::number(entry.bootPri));
    if (!tail.isEmpty()) {
        value += QStringLiteral(",") + tail;
    }
    return value;
}

QString serializeWinUaeQtUaehfDirectoryMountValue(const WinUaeQtMountEntry &entry)
{
    return QStringLiteral("dir,%1").arg(serializeWinUaeQtFilesystem2MountValue(entry));
}

QString serializeWinUaeQtUaehfHardfileMountValue(const WinUaeQtMountEntry &entry)
{
    return QStringLiteral("hdf,%1").arg(serializeWinUaeQtHardfile2MountValue(entry));
}

QString serializeWinUaeQtUaehfCdMountValue(const WinUaeQtMountEntry &entry)
{
    WinUaeQtMountEntry cd = entry;
    cd.readOnly = true;
    cd.bootPri = 0;
    return QStringLiteral("cd%1,%2")
        .arg(qBound(0, cd.emuUnit, 9))
        .arg(serializeWinUaeQtHardfileLikeMountValue(cd, QString(), QStringLiteral("0,0,0,2048"), QStringLiteral(",ide0")));
}

QString serializeWinUaeQtUaehfTapeMountValue(const WinUaeQtMountEntry &entry)
{
    return QStringLiteral("tape%1,%2")
        .arg(qBound(0, entry.emuUnit, 9))
        .arg(serializeWinUaeQtHardfileLikeMountValue(entry, QString(), QStringLiteral("0,0,0,512"), QStringLiteral(",uae0")));
}
