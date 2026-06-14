#include "mount_config.h"

#include <QDebug>

static bool require(bool condition, const char *message)
{
    if (!condition) {
        qWarning().noquote() << message;
    }
    return condition;
}

static bool requireText(const QString &actual, const QString &expected, const char *field)
{
    if (actual == expected) {
        return true;
    }
    qWarning().noquote() << field << "expected" << expected << "got" << actual;
    return false;
}

static bool requireInt(int actual, int expected, const char *field)
{
    if (actual == expected) {
        return true;
    }
    qWarning().noquote() << field << "expected" << expected << "got" << actual;
    return false;
}

static bool testDirectoryMount()
{
    WinUaeQtMountEntry entry;
    bool ok = parseWinUaeQtUaehfMountValue(QStringLiteral("dir,ro,DH1:Work:\"/tmp/Work,Disk\",5"), &entry);
    ok = require(ok, "directory mount did not parse") && ok;
    ok = requireText(entry.kind, QStringLiteral("dir"), "directory kind") && ok;
    ok = requireText(entry.device, QStringLiteral("DH1"), "directory device") && ok;
    ok = requireText(entry.volume, QStringLiteral("Work"), "directory volume") && ok;
    ok = requireText(entry.path, QStringLiteral("/tmp/Work,Disk"), "directory path") && ok;
    ok = require(entry.readOnly, "directory read-only") && ok;
    ok = requireInt(entry.bootPri, 5, "directory boot priority") && ok;
    ok = requireText(serializeWinUaeQtFilesystem2MountValue(entry), QStringLiteral("ro,DH1:Work:\"/tmp/Work,Disk\",5"), "filesystem2 serialized value") && ok;
    ok = requireText(serializeWinUaeQtUaehfDirectoryMountValue(entry), QStringLiteral("dir,ro,DH1:Work:\"/tmp/Work,Disk\",5"), "uaehf directory serialized value") && ok;
    WinUaeQtMountEntry filesystemEntry;
    ok = require(parseWinUaeQtFilesystem2MountValue(QStringLiteral("ro,DH1:Work:\"/tmp/Work,Disk\",5"), &filesystemEntry), "filesystem2 mount did not parse") && ok;
    ok = requireText(filesystemEntry.path, QStringLiteral("/tmp/Work,Disk"), "filesystem2 path") && ok;
    return ok;
}

static bool testHardfileMount()
{
    const QString config = QStringLiteral("rw,DH2:/tmp/disk.hdf,32,1,2,512,0,,uae0");
    WinUaeQtMountEntry entry;
    bool ok = parseWinUaeQtHardfile2MountValue(config, &entry);
    ok = require(ok, "hardfile mount did not parse") && ok;
    ok = requireText(entry.kind, QStringLiteral("hdf"), "hardfile kind") && ok;
    ok = requireText(entry.device, QStringLiteral("DH2"), "hardfile device") && ok;
    ok = requireText(entry.path, QStringLiteral("/tmp/disk.hdf"), "hardfile path") && ok;
    ok = require(!entry.readOnly, "hardfile read/write") && ok;
    ok = requireInt(entry.bootPri, 0, "hardfile boot priority") && ok;
    ok = requireText(serializeWinUaeQtHardfile2MountValue(entry), config, "hardfile2 serialized value") && ok;
    ok = requireText(serializeWinUaeQtUaehfHardfileMountValue(entry), QStringLiteral("hdf,") + config, "uaehf hardfile serialized value") && ok;
    entry.readOnly = true;
    entry.device = QStringLiteral("DH3");
    entry.bootPri = 7;
    ok = requireText(serializeWinUaeQtHardfile2MountValue(entry), QStringLiteral("ro,DH3:/tmp/disk.hdf,32,1,2,512,7,,uae0"), "edited hardfile2 serialized value") && ok;
    WinUaeQtMountEntry uaehfEntry;
    ok = require(parseWinUaeQtUaehfMountValue(QStringLiteral("hdf,") + config, &uaehfEntry), "uaehf hardfile did not parse") && ok;
    ok = requireText(serializeWinUaeQtHardfile2MountValue(uaehfEntry), config, "uaehf hardfile direct serialized value") && ok;
    WinUaeQtMountEntry minimalEntry;
    ok = require(parseWinUaeQtHardfile2MountValue(QStringLiteral("rw,DH4:/tmp/min.hdf,16,2,1,512,3"), &minimalEntry), "minimal hardfile did not parse") && ok;
    minimalEntry.readOnly = true;
    minimalEntry.bootPri = 4;
    ok = requireText(serializeWinUaeQtHardfile2MountValue(minimalEntry), QStringLiteral("ro,DH4:/tmp/min.hdf,16,2,1,512,4"), "minimal hardfile serialized value") && ok;
    WinUaeQtMountEntry expansionEntry;
    const QString expansionConfig = QStringLiteral("rw,DH5:/tmp/ripple.hdf,32,1,2,512,0,,ide0_ripple,ATA2+");
    ok = require(parseWinUaeQtHardfile2MountValue(expansionConfig, &expansionEntry), "expansion-controller hardfile did not parse") && ok;
    ok = requireText(expansionEntry.hardfileTail, QStringLiteral(",ide0_ripple,ATA2+"), "expansion-controller hardfile tail") && ok;
    ok = requireText(serializeWinUaeQtHardfile2MountValue(expansionEntry), expansionConfig, "expansion-controller hardfile serialized value") && ok;
    return ok;
}

static bool testCdMount()
{
    WinUaeQtMountEntry entry;
    bool ok = parseWinUaeQtUaehfMountValue(QStringLiteral("cd1,ro,:,0,0,0,2048,0,,ide1"), &entry);
    ok = require(ok, "uaehf cd did not parse") && ok;
    ok = requireText(entry.kind, QStringLiteral("cd"), "cd kind") && ok;
    ok = requireInt(entry.emuUnit, 1, "cd emulation unit") && ok;
    ok = require(entry.readOnly, "cd should be read-only") && ok;
    ok = requireText(entry.hardfileGeometry, QStringLiteral("0,0,0,2048"), "cd geometry") && ok;
    ok = requireText(entry.hardfileTail, QStringLiteral(",ide1"), "cd tail") && ok;
    ok = requireText(serializeWinUaeQtUaehfCdMountValue(entry), QStringLiteral("cd1,ro,:,0,0,0,2048,0,,ide1"), "uaehf cd serialized value") && ok;
    WinUaeQtMountEntry expansionEntry;
    ok = require(parseWinUaeQtUaehfMountValue(QStringLiteral("cd0,ro,:,0,0,0,2048,0,,scsi0_a4091"), &expansionEntry), "expansion-controller cd did not parse") && ok;
    ok = requireText(expansionEntry.hardfileTail, QStringLiteral(",scsi0_a4091"), "expansion-controller cd tail") && ok;
    ok = requireText(serializeWinUaeQtUaehfCdMountValue(expansionEntry), QStringLiteral("cd0,ro,:,0,0,0,2048,0,,scsi0_a4091"), "expansion-controller cd serialized value") && ok;
    return ok;
}

static bool testTapeMount()
{
    WinUaeQtMountEntry entry;
    bool ok = parseWinUaeQtUaehfMountValue(QStringLiteral("tape0,rw,:\"/tmp/Tape,One\",0,0,0,512,0,,uae0"), &entry);
    ok = require(ok, "uaehf tape did not parse") && ok;
    ok = requireText(entry.kind, QStringLiteral("tape"), "tape kind") && ok;
    ok = requireInt(entry.emuUnit, 0, "tape emulation unit") && ok;
    ok = requireText(entry.path, QStringLiteral("/tmp/Tape,One"), "tape path") && ok;
    ok = requireText(entry.hardfileGeometry, QStringLiteral("0,0,0,512"), "tape geometry") && ok;
    ok = requireText(entry.hardfileTail, QStringLiteral(",uae0"), "tape tail") && ok;
    ok = requireText(serializeWinUaeQtUaehfTapeMountValue(entry), QStringLiteral("tape0,rw,:\"/tmp/Tape,One\",0,0,0,512,0,,uae0"), "uaehf tape serialized value") && ok;
    WinUaeQtMountEntry expansionEntry;
    ok = require(parseWinUaeQtUaehfMountValue(QStringLiteral("tape1,rw,:,0,0,0,512,0,,ide1_alfapower"), &expansionEntry), "expansion-controller tape did not parse") && ok;
    ok = requireText(expansionEntry.hardfileTail, QStringLiteral(",ide1_alfapower"), "expansion-controller tape tail") && ok;
    ok = requireText(serializeWinUaeQtUaehfTapeMountValue(expansionEntry), QStringLiteral("tape1,rw,:,0,0,0,512,0,,ide1_alfapower"), "expansion-controller tape serialized value") && ok;
    return ok;
}

static bool testConfigFieldList()
{
    const QStringList fields = winUaeQtConfigFieldList(QStringLiteral(",ide0,\"/tmp/Geo,One\",CF,ATA2+S,flags=0x1"));
    bool ok = requireInt(fields.size(), 6, "field list size");
    ok = requireText(fields.value(0), QString(), "empty filesys field") && ok;
    ok = requireText(fields.value(2), QStringLiteral("/tmp/Geo,One"), "quoted field") && ok;
    ok = requireText(winUaeQtConfigJoinFields(fields), QStringLiteral(",ide0,\"/tmp/Geo,One\",CF,ATA2+S,flags=0x1"), "joined fields") && ok;
    return ok;
}

int main()
{
    bool ok = true;
    ok = testDirectoryMount() && ok;
    ok = testHardfileMount() && ok;
    ok = testCdMount() && ok;
    ok = testTapeMount() && ok;
    ok = testConfigFieldList() && ok;
    ok = requireText(winUaeQtSanitizedAmigaName(QStringLiteral("dh:0, "), QStringLiteral("DH0"), true), QStringLiteral("DH_0_"), "sanitized name") && ok;
    return ok ? 0 : 1;
}
