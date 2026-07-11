#pragma once

#include <QVector>
#include <QString>

class QWidget;

struct WinUaeQtFloppyBridgeProfile {
    unsigned int id;
    QString name;
};

#if defined(UAE_UNIX_WITH_FLOPPYBRIDGE) \
    && UAE_UNIX_WITH_FLOPPYBRIDGE
bool winuaeQtFloppyBridgeAvailable();
QVector<WinUaeQtFloppyBridgeProfile> winuaeQtFloppyBridgeProfiles();
bool winuaeQtManageFloppyBridgeProfiles(QWidget *parent);
QString winuaeQtFloppyBridgeInformation();
#else
inline bool winuaeQtFloppyBridgeAvailable()
{
    return false;
}

inline QVector<WinUaeQtFloppyBridgeProfile> winuaeQtFloppyBridgeProfiles()
{
    return {};
}

inline bool winuaeQtManageFloppyBridgeProfiles(QWidget *)
{
    return false;
}

inline QString winuaeQtFloppyBridgeInformation()
{
    return {};
}
#endif
