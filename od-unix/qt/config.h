#pragma once

#include <QMap>
#include <QList>
#include <QString>
#include <QStringList>

class WinUaeQtConfig {
public:
    using Settings = QMap<QString, QString>;
    struct Setting {
        QString key;
        QString value;
    };
    using OrderedSettings = QList<Setting>;

    WinUaeQtConfig() = default;
    explicit WinUaeQtConfig(Settings settings);

    const Settings &settings() const;
    const OrderedSettings &orderedSettings() const;
    QStringList values(const QString &key) const;
    void setSettings(Settings settings);
    void applySettings(const Settings &settings, const QStringList &ownedKeys);
    void applyRepeatedSettings(const OrderedSettings &settings, const QStringList &ownedKeys);
    void moveSettingBefore(const QString &key, const QStringList &beforeKeys);

    QString value(const QString &key, const QString &defaultValue = QString()) const;
    void setValue(const QString &key, const QString &value);
    void removeValue(const QString &key);

    bool load(const QString &path, QString *error = nullptr);
    bool loadFromText(const QString &contents, QString *error = nullptr);
    bool save(const QString &path, QString *error = nullptr) const;

    QStringList commandArguments() const;
    QStringList validateForLaunch() const;

private:
    struct DocumentLine {
        QString text;
        QString key;
        QString value;
        bool setting = false;
    };

    static DocumentLine makeSettingLine(const QString &key, const QString &value);
    static OrderedSettings orderedFromSettings(const Settings &settings);

    void rebuildSettingsFromDocumentLines();
    void rebuildSettingsFromOrderedSettings();

    Settings configSettings;
    OrderedSettings orderedConfigSettings;
    QList<DocumentLine> documentLines;
    bool documentLoaded = false;
};
