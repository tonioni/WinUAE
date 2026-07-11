#include "floppybridge_ui.h"

#include <QtWidgets>

#include <memory>
#include <vector>

#include "disk.h"
#include "floppybridge/floppybridge_lib.h"
#include "registry.h"

static bool bridgeInitialized;

static void initializeBridge()
{
    if (bridgeInitialized)
        return;
    bridgeInitialized = true;

    std::vector<TCHAR> stored(65536);
    int size = int(stored.size());
    if (regquerystr(NULL, _T("FloppyBridge"), stored.data(), &size)
        && stored[0]) {
        floppybridge_set_config(stored.data());
    }
    floppybridge_has();
}

static void persistProfiles()
{
    char *profiles = NULL;
    if (FloppyBridgeAPI::exportProfilesToString(&profiles) && profiles) {
        regsetstr(NULL, _T("FloppyBridge"), profiles);
        registry_flush();
    }
    floppybridge_reload_profiles();
    floppybridge_modified(-1);
}

bool winuaeQtFloppyBridgeAvailable()
{
    initializeBridge();
    return floppybridge_has();
}

QVector<WinUaeQtFloppyBridgeProfile> winuaeQtFloppyBridgeProfiles()
{
    initializeBridge();
    std::vector<FloppyBridgeAPI::FloppyBridgeProfileInformation> source;
    QVector<WinUaeQtFloppyBridgeProfile> result;
    if (!FloppyBridgeAPI::getAllProfiles(source))
        return result;
    for (const auto &profile : source) {
        result.append({ profile.profileID,
            QString::fromLocal8Bit(profile.name) });
    }
    return result;
}

QString winuaeQtFloppyBridgeInformation()
{
    initializeBridge();
    FloppyBridgeAPI::BridgeInformation information = {};
    FloppyBridgeAPI::getBridgeDriverInformation(false, information);
    return QStringLiteral("%1\nVersion %2.%3\n%4")
        .arg(QString::fromLocal8Bit(information.about))
        .arg(information.majorVersion)
        .arg(information.minorVersion)
        .arg(QString::fromLocal8Bit(information.url));
}

static bool editProfile(QWidget *parent, unsigned int profileId,
    const QString &initialName)
{
    std::unique_ptr<FloppyBridgeAPI> bridge(
        FloppyBridgeAPI::createDriverFromProfileID(profileId));
    if (!bridge) {
        QMessageBox::critical(parent, QStringLiteral("FloppyBridge"),
            QStringLiteral("The selected profile could not be opened."));
        return false;
    }
    const FloppyDiskBridge::BridgeDriver *driver = bridge->getDriverInfo();
    if (!driver)
        return false;

    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("FloppyBridge Profile"));
    QFormLayout *form = new QFormLayout(&dialog);
    QLineEdit *name = new QLineEdit(initialName);
    QLabel *driverName = new QLabel(QStringLiteral("%1 (%2)")
        .arg(QString::fromLocal8Bit(driver->name),
            QString::fromLocal8Bit(driver->manufacturer)));
    QComboBox *mode = new QComboBox;
    mode->addItems({ QStringLiteral("Fast"), QStringLiteral("Compatible"),
        QStringLiteral("Turbo AmigaDOS"), QStringLiteral("Stalling") });
    QComboBox *density = new QComboBox;
    density->addItems({ QStringLiteral("Auto-detect"),
        QStringLiteral("DD only"), QStringLiteral("HD only") });
    QComboBox *port = new QComboBox;
    port->setEditable(true);
    std::vector<const TCHAR *> ports;
    FloppyBridgeAPI::enumCOMPorts(ports);
    for (const TCHAR *item : ports)
        port->addItem(QString::fromLocal8Bit(item));
    QCheckBox *autoDetect = new QCheckBox(QStringLiteral("Auto-detect port"));
    QCheckBox *autoCache = new QCheckBox(QStringLiteral("Auto-cache tracks"));
    QCheckBox *smartSpeed = new QCheckBox(QStringLiteral("Smart speed"));
    QComboBox *cable = new QComboBox;
    cable->addItems({ QStringLiteral("IBM PC Drive A"),
        QStringLiteral("IBM PC Drive B"), QStringLiteral("Shugart Drive 0"),
        QStringLiteral("Shugart Drive 1"), QStringLiteral("Shugart Drive 2"),
        QStringLiteral("Shugart Drive 3") });

    FloppyBridgeAPI::BridgeMode currentMode =
        FloppyBridgeAPI::BridgeMode::bmFast;
    FloppyBridgeAPI::BridgeDensityMode currentDensity =
        FloppyBridgeAPI::BridgeDensityMode::bdmAuto;
    FloppyBridgeAPI::DriveSelection currentCable =
        FloppyBridgeAPI::DriveSelection::dsDriveA;
    bool checked = false;
    TCharString currentPort = {};
    bridge->getBridgeMode(&currentMode);
    bridge->getBridgeDensityMode(&currentDensity);
    bridge->getDriveSelection(&currentCable);
    bridge->getComPort(&currentPort);
    mode->setCurrentIndex(int(currentMode));
    density->setCurrentIndex(int(currentDensity));
    cable->setCurrentIndex(int(currentCable));
    port->setCurrentText(QString::fromLocal8Bit(currentPort));
    if (bridge->getComPortAutoDetect(&checked))
        autoDetect->setChecked(checked);
    if (bridge->getAutoCacheMode(&checked))
        autoCache->setChecked(checked);
    if (bridge->getSmartSpeedEnabled(&checked))
        smartSpeed->setChecked(checked);

    const unsigned int options = driver->configOptions;
    port->setEnabled(options & FloppyBridgeAPI::ConfigOption_ComPort);
    autoDetect->setVisible(options
        & FloppyBridgeAPI::ConfigOption_AutoDetectComport);
    autoCache->setVisible(options & FloppyBridgeAPI::ConfigOption_AutoCache);
    smartSpeed->setVisible(options & FloppyBridgeAPI::ConfigOption_SmartSpeed);
    cable->setVisible(options & FloppyBridgeAPI::ConfigOption_DriveABCable);
    if (!(options & FloppyBridgeAPI::ConfigOption_SupportsShugartMode)) {
        while (cable->count() > 2)
            cable->removeItem(cable->count() - 1);
    }
    QObject::connect(autoDetect, &QCheckBox::toggled, port,
        [port](bool enabled) { port->setEnabled(!enabled); });
    if (autoDetect->isVisible() && autoDetect->isChecked())
        port->setEnabled(false);

    form->addRow(QStringLiteral("Profile name:"), name);
    form->addRow(QStringLiteral("Driver:"), driverName);
    form->addRow(QStringLiteral("Mode:"), mode);
    form->addRow(QStringLiteral("Disk density:"), density);
    form->addRow(QStringLiteral("Port:"), port);
    form->addRow(QString(), autoDetect);
    form->addRow(QString(), autoCache);
    form->addRow(QString(), smartSpeed);
    form->addRow(QStringLiteral("Drive cable:"), cable);
    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted,
        &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected,
        &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return false;

    QString profileName = name->text();
    profileName.remove(QRegularExpression(QStringLiteral("[\\[\\]|]")));
    if (profileName.trimmed().isEmpty()) {
        QMessageBox::warning(parent, QStringLiteral("FloppyBridge"),
            QStringLiteral("A profile name is required."));
        return false;
    }
    bridge->setBridgeMode(FloppyBridgeAPI::BridgeMode(mode->currentIndex()));
    bridge->setBridgeDensityMode(
        FloppyBridgeAPI::BridgeDensityMode(density->currentIndex()));
    if (options & FloppyBridgeAPI::ConfigOption_ComPort) {
        QByteArray value = port->currentText().toLocal8Bit();
        bridge->setComPort(value.data());
    }
    if (options & FloppyBridgeAPI::ConfigOption_AutoDetectComport)
        bridge->setComPortAutoDetect(autoDetect->isChecked());
    if (options & FloppyBridgeAPI::ConfigOption_AutoCache)
        bridge->setAutoCacheMode(autoCache->isChecked());
    if (options & FloppyBridgeAPI::ConfigOption_SmartSpeed)
        bridge->setSmartSpeedEnabled(smartSpeed->isChecked());
    if (options & FloppyBridgeAPI::ConfigOption_DriveABCable)
        bridge->setDriveSelection(
            FloppyBridgeAPI::DriveSelection(cable->currentIndex()));
    char *config = NULL;
    if (!bridge->getConfigAsString(&config) || !config
        || !FloppyBridgeAPI::setProfileConfigFromString(profileId, config)) {
        QMessageBox::critical(parent, QStringLiteral("FloppyBridge"),
            QStringLiteral("The profile configuration could not be saved."));
        return false;
    }
    QByteArray encodedName = profileName.toLocal8Bit();
    FloppyBridgeAPI::setProfileName(profileId, encodedName.data());
    persistProfiles();
    return true;
}

bool winuaeQtManageFloppyBridgeProfiles(QWidget *parent)
{
    if (!winuaeQtFloppyBridgeAvailable()) {
        QMessageBox::warning(parent, QStringLiteral("FloppyBridge"),
            QStringLiteral("FloppyBridge.so could not be loaded."));
        return false;
    }
    QDialog dialog(parent);
    QDialog *dialogPtr = &dialog;
    dialog.setWindowTitle(QStringLiteral("FloppyBridge Profiles"));
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QListWidget *list = new QListWidget;
    layout->addWidget(list);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    QPushButton *add = buttons->addButton(QStringLiteral("New"),
        QDialogButtonBox::ActionRole);
    QPushButton *edit = buttons->addButton(QStringLiteral("Edit"),
        QDialogButtonBox::ActionRole);
    QPushButton *remove = buttons->addButton(QStringLiteral("Delete"),
        QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    auto refresh = [list]() {
        list->clear();
        for (const auto &profile : winuaeQtFloppyBridgeProfiles()) {
            QListWidgetItem *item = new QListWidgetItem(profile.name, list);
            item->setData(Qt::UserRole, profile.id);
        }
    };
    refresh();
    QObject::connect(buttons, &QDialogButtonBox::rejected,
        &dialog, &QDialog::reject);
    QObject::connect(edit, &QPushButton::clicked, &dialog, [=]() {
        QListWidgetItem *item = list->currentItem();
        if (item && editProfile(dialogPtr, item->data(Qt::UserRole).toUInt(),
                item->text()))
            refresh();
    });
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog,
        [=](QListWidgetItem *item) {
            if (editProfile(dialogPtr, item->data(Qt::UserRole).toUInt(),
                    item->text()))
                refresh();
        });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [=]() {
        QListWidgetItem *item = list->currentItem();
        if (!item)
            return;
        if (QMessageBox::question(dialogPtr, QStringLiteral("FloppyBridge"),
                QStringLiteral("Delete profile '%1'?").arg(item->text()))
            == QMessageBox::Yes) {
            FloppyBridgeAPI::deleteProfile(item->data(Qt::UserRole).toUInt());
            persistProfiles();
            refresh();
        }
    });
    QObject::connect(add, &QPushButton::clicked, &dialog, [=]() {
        std::vector<FloppyBridgeAPI::DriverInformation> drivers;
        FloppyBridgeAPI::getDriverList(drivers);
        QStringList names;
        for (const auto &driver : drivers) {
            names.append(QStringLiteral("%1 (%2)")
                .arg(QString::fromLocal8Bit(driver.name),
                    QString::fromLocal8Bit(driver.manufacturer)));
        }
        bool ok = false;
        QString selected = QInputDialog::getItem(dialogPtr,
            QStringLiteral("New FloppyBridge Profile"),
            QStringLiteral("Driver:"), names, 0, false, &ok);
        int index = names.indexOf(selected);
        if (!ok || index < 0)
            return;
        unsigned int id = 0;
        if (!FloppyBridgeAPI::createNewProfile(drivers[index].driverIndex, &id))
            return;
        QString defaultName = QString::fromLocal8Bit(drivers[index].name);
        QByteArray encodedName = defaultName.toLocal8Bit();
        FloppyBridgeAPI::setProfileName(id, encodedName.data());
        floppybridge_reload_profiles();
        if (!editProfile(dialogPtr, id, defaultName))
            FloppyBridgeAPI::deleteProfile(id);
        persistProfiles();
        refresh();
    });
    dialog.resize(520, 360);
    dialog.exec();
    return true;
}
