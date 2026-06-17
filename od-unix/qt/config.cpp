#include "config.h"

#include <QFile>
#include <QTextStream>

#include <utility>

static bool isUiOnlySetting(const QString &key)
{
    return key.startsWith(QStringLiteral("unix.ui."));
}

WinUaeQtConfig::WinUaeQtConfig(Settings settings)
{
    setSettings(std::move(settings));
}

WinUaeQtConfig::DocumentLine WinUaeQtConfig::makeSettingLine(const QString &key, const QString &value)
{
    DocumentLine line;
    line.text = QStringLiteral("%1=%2").arg(key, value);
    line.key = key;
    line.value = value;
    line.setting = true;
    return line;
}

const WinUaeQtConfig::Settings &WinUaeQtConfig::settings() const
{
    return configSettings;
}

const WinUaeQtConfig::OrderedSettings &WinUaeQtConfig::orderedSettings() const
{
    return orderedConfigSettings;
}

QStringList WinUaeQtConfig::values(const QString &key) const
{
    QStringList result;
    for (const Setting &setting : orderedConfigSettings) {
        if (setting.key == key && !setting.value.isEmpty()) {
            result.append(setting.value);
        }
    }
    return result;
}

void WinUaeQtConfig::setSettings(Settings settings)
{
    configSettings = std::move(settings);
    orderedConfigSettings = orderedFromSettings(configSettings);
    documentLines.clear();
    documentLoaded = false;
}

WinUaeQtConfig::OrderedSettings WinUaeQtConfig::orderedFromSettings(const Settings &settings)
{
    OrderedSettings ordered;
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
        if (!it.value().isEmpty()) {
            ordered.append({ it.key(), it.value() });
        }
    }
    return ordered;
}

void WinUaeQtConfig::rebuildSettingsFromOrderedSettings()
{
    configSettings.clear();
    for (const Setting &setting : std::as_const(orderedConfigSettings)) {
        if (!setting.value.isEmpty()) {
            configSettings.insert(setting.key, setting.value);
        }
    }
}

void WinUaeQtConfig::rebuildSettingsFromDocumentLines()
{
    configSettings.clear();
    orderedConfigSettings.clear();
    for (const DocumentLine &line : std::as_const(documentLines)) {
        if (line.setting && !line.value.isEmpty()) {
            orderedConfigSettings.append({ line.key, line.value });
            configSettings.insert(line.key, line.value);
        }
    }
}

void WinUaeQtConfig::applySettings(const Settings &settings, const QStringList &ownedKeys)
{
    for (const QString &key : ownedKeys) {
        const QString value = settings.value(key);
        if (settings.contains(key) && !value.isEmpty()) {
            configSettings.insert(key, value);
        } else {
            configSettings.remove(key);
        }
    }

    OrderedSettings updatedSettings;
    for (const Setting &setting : std::as_const(orderedConfigSettings)) {
        if (!ownedKeys.contains(setting.key)) {
            updatedSettings.append(setting);
        }
    }
    for (const QString &key : ownedKeys) {
        const QString value = settings.value(key);
        if (settings.contains(key) && !value.isEmpty()) {
            updatedSettings.append({ key, value });
        }
    }
    orderedConfigSettings = updatedSettings;

    if (!documentLoaded) {
        return;
    }

    QList<DocumentLine> updated;
    QStringList emitted;
    for (const DocumentLine &line : std::as_const(documentLines)) {
        if (!line.setting || !ownedKeys.contains(line.key)) {
            updated.append(line);
            continue;
        }

        const QString value = settings.value(line.key);
        if (settings.contains(line.key) && !value.isEmpty() && !emitted.contains(line.key)) {
            updated.append(makeSettingLine(line.key, value));
            emitted.append(line.key);
        }
    }

    for (const QString &key : ownedKeys) {
        const QString value = settings.value(key);
        if (settings.contains(key) && !value.isEmpty() && !emitted.contains(key)) {
            updated.append(makeSettingLine(key, value));
            emitted.append(key);
        }
    }

    documentLines = updated;
    rebuildSettingsFromDocumentLines();
}

void WinUaeQtConfig::applyRepeatedSettings(const OrderedSettings &settings, const QStringList &ownedKeys)
{
    OrderedSettings updatedSettings;
    for (const Setting &setting : std::as_const(orderedConfigSettings)) {
        if (!ownedKeys.contains(setting.key)) {
            updatedSettings.append(setting);
        }
    }
    for (const Setting &setting : settings) {
        if (ownedKeys.contains(setting.key) && !setting.value.isEmpty()) {
            updatedSettings.append(setting);
        }
    }
    orderedConfigSettings = updatedSettings;
    rebuildSettingsFromOrderedSettings();

    if (!documentLoaded) {
        return;
    }

    QList<DocumentLine> updated;
    bool inserted = false;
    for (const DocumentLine &line : std::as_const(documentLines)) {
        if (!line.setting || !ownedKeys.contains(line.key)) {
            updated.append(line);
            continue;
        }
        if (!inserted) {
            for (const Setting &setting : settings) {
                if (ownedKeys.contains(setting.key) && !setting.value.isEmpty()) {
                    updated.append(makeSettingLine(setting.key, setting.value));
                }
            }
            inserted = true;
        }
    }

    if (!inserted) {
        for (const Setting &setting : settings) {
            if (ownedKeys.contains(setting.key) && !setting.value.isEmpty()) {
                updated.append(makeSettingLine(setting.key, setting.value));
            }
        }
    }

    documentLines = updated;
    rebuildSettingsFromDocumentLines();
}

void WinUaeQtConfig::moveSettingBefore(const QString &key, const QStringList &beforeKeys)
{
    Setting moved;
    bool found = false;
    OrderedSettings updatedSettings;
    for (const Setting &setting : std::as_const(orderedConfigSettings)) {
        if (setting.key == key) {
            if (!found) {
                moved = setting;
                found = true;
            }
            continue;
        }
        updatedSettings.append(setting);
    }
    if (!found) {
        return;
    }

    int insertIndex = updatedSettings.size();
    for (int i = 0; i < updatedSettings.size(); i++) {
        if (beforeKeys.contains(updatedSettings[i].key)) {
            insertIndex = i;
            break;
        }
    }
    updatedSettings.insert(insertIndex, moved);
    orderedConfigSettings = updatedSettings;
    rebuildSettingsFromOrderedSettings();

    if (!documentLoaded) {
        return;
    }

    DocumentLine movedLine = makeSettingLine(moved.key, moved.value);
    QList<DocumentLine> updatedLines;
    bool lineFound = false;
    for (const DocumentLine &line : std::as_const(documentLines)) {
        if (line.setting && line.key == key) {
            if (!lineFound) {
                movedLine = line;
                lineFound = true;
            }
            continue;
        }
        updatedLines.append(line);
    }

    int lineInsertIndex = updatedLines.size();
    for (int i = 0; i < updatedLines.size(); i++) {
        if (updatedLines[i].setting && beforeKeys.contains(updatedLines[i].key)) {
            lineInsertIndex = i;
            break;
        }
    }
    updatedLines.insert(lineInsertIndex, movedLine);
    documentLines = updatedLines;
    rebuildSettingsFromDocumentLines();
}

QString WinUaeQtConfig::value(const QString &key, const QString &defaultValue) const
{
    return configSettings.value(key, defaultValue);
}

void WinUaeQtConfig::setValue(const QString &key, const QString &value)
{
    Settings singleSetting;
    if (!value.isEmpty()) {
        singleSetting.insert(key, value);
    }
    applySettings(singleSetting, QStringList { key });
}

void WinUaeQtConfig::removeValue(const QString &key)
{
    applySettings(Settings(), QStringList { key });
}

bool WinUaeQtConfig::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    Settings loaded;
    OrderedSettings ordered;
    QList<DocumentLine> lines;
    while (!file.atEnd()) {
        QString text = QString::fromUtf8(file.readLine());
        while (text.endsWith(QLatin1Char('\n')) || text.endsWith(QLatin1Char('\r'))) {
            text.chop(1);
        }

        DocumentLine line;
        line.text = text;

        const QString trimmed = text.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char(';')) || trimmed.startsWith(QLatin1Char('#'))) {
            lines.append(line);
            continue;
        }

        const int separator = text.indexOf(QLatin1Char('='));
        if (separator <= 0) {
            lines.append(line);
            continue;
        }

        const QString key = text.left(separator).trimmed();
        const QString value = text.mid(separator + 1).trimmed();
        if (!key.isEmpty()) {
            line.key = key;
            line.value = value;
            line.setting = true;
            loaded.insert(key, value);
            if (!value.isEmpty()) {
                ordered.append({ key, value });
            }
        }
        lines.append(line);
    }

    configSettings = loaded;
    orderedConfigSettings = ordered;
    documentLines = lines;
    documentLoaded = true;
    return true;
}

bool WinUaeQtConfig::save(const QString &path, QString *error) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    QTextStream out(&file);
    if (documentLoaded) {
        for (const DocumentLine &line : documentLines) {
            out << line.text << "\n";
        }
        return true;
    }

    out << "; WinUAE Unix Qt configuration\n";
    for (const Setting &setting : orderedConfigSettings) {
        if (!setting.value.isEmpty()) {
            out << setting.key << "=" << setting.value << "\n";
        }
    }
    return true;
}

QStringList WinUaeQtConfig::commandArguments() const
{
    QStringList args;
    for (const Setting &setting : orderedConfigSettings) {
        if (!setting.value.isEmpty() && !isUiOnlySetting(setting.key)) {
            args << QStringLiteral("-s") << QStringLiteral("%1=%2").arg(setting.key, setting.value);
        }
    }
    return args;
}

QStringList WinUaeQtConfig::validateForLaunch() const
{
    QStringList errors;
    if (value(QStringLiteral("kickstart_rom_file")).trimmed().isEmpty()) {
        errors << QStringLiteral("Main ROM file is required.");
    }
    return errors;
}
