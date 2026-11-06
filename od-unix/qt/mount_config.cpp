#include "mount_config.h"
#include "include/cfgfile_mount.h"

#include <QFileInfo>

QString winUaeQtConfigEscapeMin(QString value)
{
    return QString::fromStdU16String(cfgfile_escape_min_string(value.toStdU16String(), true));
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

    const std::u16string source = input->toStdU16String();
    const char16_t *next = source.c_str();
    std::u16string value;
    if (!cfgfile_mount_field(next, char16_t(delimiter.unicode()), value,
            requireDelimiter, true)) {
        return false;
    }
    *input = input->mid(next - source.c_str());
    *field = QString::fromStdU16String(value);
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

static bool parseMountPrefix(QString *value, bool directory, WinUaeQtMountEntry *entry)
{
    const std::u16string source = value->toStdU16String();
    cfgfile_mount_prefix<char16_t> prefix;
    if (!cfgfile_parse_mount_prefix(source.c_str(), directory, prefix, true)) {
        return false;
    }
    entry->readOnly = prefix.readonly;
    entry->device = QString::fromStdU16String(prefix.device);
    entry->volume = QString::fromStdU16String(prefix.volume);
    entry->path = QString::fromStdU16String(prefix.path);
    *value = value->mid(prefix.consumed);
    return true;
}

static bool parseDirectoryMountValue(QString value, WinUaeQtMountEntry *entry)
{
    QString bootPri;
    if (!parseMountPrefix(&value, true, entry)
        || !takeConfigField(&value, QLatin1Char(','), &bootPri, false)) {
        return false;
    }
    entry->kind = QStringLiteral("dir");
    entry->bootPri = bootPri.toInt();
    return true;
}

static bool parseHardfileMountValue(QString value, WinUaeQtMountEntry *entry)
{
    if (!parseMountPrefix(&value, false, entry)) {
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
    return QString::fromStdU16String(cfgfile_format_filesystem<char16_t>(entry.readOnly,
        winUaeQtSanitizedAmigaName(entry.device, QStringLiteral("DH0"), true).toStdU16String(),
        winUaeQtSanitizedAmigaName(entry.volume, QStringLiteral("Hard Disk"), false).toStdU16String(),
        winUaeQtConfigEscapeMin(entry.path).toStdU16String(), entry.bootPri));
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
    return QString::fromStdU16String(cfgfile_format_hardfile<char16_t>(entry.readOnly,
        device.toStdU16String(), winUaeQtConfigEscapeMin(entry.path).toStdU16String(),
        geometry.toStdU16String(), bootPri, tail.toStdU16String()));
}

static QString serializeWinUaeQtHardfileLikeMountValue(
    const WinUaeQtMountEntry &entry,
    const QString &fallbackDevice,
    const QString &defaultGeometry,
    const QString &defaultTail)
{
    const QString geometry = entry.hardfileGeometry.isEmpty() ? defaultGeometry : entry.hardfileGeometry;
    const QString tail = entry.hardfileTail.isEmpty() ? defaultTail : entry.hardfileTail;
    return QString::fromStdU16String(cfgfile_format_hardfile<char16_t>(entry.readOnly,
        winUaeQtSanitizedAmigaName(entry.device, fallbackDevice, true).toStdU16String(),
        winUaeQtConfigEscapeMin(entry.path).toStdU16String(), geometry.toStdU16String(),
        entry.bootPri, tail.toStdU16String()));
}

QString serializeWinUaeQtUaehfDirectoryMountValue(const WinUaeQtMountEntry &entry)
{
    return QString::fromStdU16String(cfgfile_format_uaehf<char16_t>(u"dir",
        serializeWinUaeQtFilesystem2MountValue(entry).toStdU16String()));
}

QString serializeWinUaeQtUaehfHardfileMountValue(const WinUaeQtMountEntry &entry)
{
    return QString::fromStdU16String(cfgfile_format_uaehf<char16_t>(u"hdf",
        serializeWinUaeQtHardfile2MountValue(entry).toStdU16String()));
}

QString serializeWinUaeQtUaehfCdMountValue(const WinUaeQtMountEntry &entry)
{
    WinUaeQtMountEntry cd = entry;
    cd.readOnly = true;
    cd.bootPri = 0;
    return QString::fromStdU16String(cfgfile_format_uaehf<char16_t>(u"cd", qBound(0, cd.emuUnit, 9),
        serializeWinUaeQtHardfileLikeMountValue(cd, QString(), QStringLiteral("0,0,0,2048"), QStringLiteral(",ide0")).toStdU16String()));
}

QString serializeWinUaeQtUaehfTapeMountValue(const WinUaeQtMountEntry &entry)
{
    return QString::fromStdU16String(cfgfile_format_uaehf<char16_t>(u"tape", qBound(0, entry.emuUnit, 9),
        serializeWinUaeQtHardfileLikeMountValue(entry, QString(), QStringLiteral("0,0,0,512"), QStringLiteral(",uae0")).toStdU16String()));
}
