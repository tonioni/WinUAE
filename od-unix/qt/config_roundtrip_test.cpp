#include "config.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QDebug>

static bool writeText(const QString &path, const QString &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        qWarning().noquote() << file.errorString();
        return false;
    }
    QTextStream out(&file);
    out << text;
    return true;
}

static QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning().noquote() << file.errorString();
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

static bool requireContains(const QString &text, const QString &needle)
{
    if (text.contains(needle)) {
        return true;
    }
    qWarning().noquote() << "missing expected text:" << needle;
    return false;
}

static bool requireNotContains(const QString &text, const QString &needle)
{
    if (!text.contains(needle)) {
        return true;
    }
    qWarning().noquote() << "unexpected text:" << needle;
    return false;
}

static bool requireCount(const QString &text, const QString &needle, int expected)
{
    int count = 0;
    int offset = 0;
    for (;;) {
        offset = text.indexOf(needle, offset);
        if (offset < 0) {
            break;
        }
        count++;
        offset += needle.size();
    }
    if (count == expected) {
        return true;
    }
    qWarning().noquote() << needle << "expected count" << expected << "got" << count;
    return false;
}

static bool requireBefore(const QString &text, const QString &first, const QString &second)
{
    const int firstIndex = text.indexOf(first);
    const int secondIndex = text.indexOf(second);
    if (firstIndex >= 0 && secondIndex >= 0 && firstIndex < secondIndex) {
        return true;
    }
    qWarning().noquote() << first << "was not before" << second;
    return false;
}

static bool requireArgBefore(const QStringList &args, const QString &first, const QString &second)
{
    const int firstIndex = args.indexOf(first);
    const int secondIndex = args.indexOf(second);
    if (firstIndex >= 0 && secondIndex >= 0 && firstIndex < secondIndex) {
        return true;
    }
    qWarning().noquote() << first << "argument was not before" << second;
    return false;
}

int main()
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        qWarning().noquote() << "failed to create temporary directory";
        return 1;
    }

    const QString inputPath = QDir(tempDir.path()).filePath(QStringLiteral("input.uae"));
    const QString outputPath = QDir(tempDir.path()).filePath(QStringLiteral("output.uae"));
    const QString input =
        QStringLiteral("; keep this comment\n")
        + QStringLiteral("unknown_setting=keep-me\n")
        + QStringLiteral("kickstart_rom_file=/old.rom\n")
        + QStringLiteral("kickstart_ext_rom_file=/old-ext.rom\n")
        + QStringLiteral("malformed line without separator\n")
        + QStringLiteral("# keep this too\n")
        + QStringLiteral("chipset=ecs\n")
        + QStringLiteral("serial_port=TCP:127.0.0.1:1234\n")
        + QStringLiteral("midiout_device=-2\n")
        + QStringLiteral("midiout_device_name=none\n")
        + QStringLiteral("midiin_device=-1\n")
        + QStringLiteral("midiin_device_name=none\n")
        + QStringLiteral("midirouter=false\n")
        + QStringLiteral("filesystem2=rw,DH0:Old:/old/System,0\n")
        + QStringLiteral("; keep mount comment\n")
        + QStringLiteral("filesystem2=rw,DH1:Old2:/old/Work,0\n")
        + QStringLiteral("hardfile2=rw,DH2:/old/disk.hdf,32,1,2,512,0,,uae0\n");

    if (!writeText(inputPath, input)) {
        return 1;
    }

    WinUaeQtConfig config;
    QString error;
    if (!config.load(inputPath, &error)) {
        qWarning().noquote() << error;
        return 1;
    }

    WinUaeQtConfig::Settings edited;
    edited.insert(QStringLiteral("kickstart_rom_file"), QStringLiteral("/new.rom"));
    edited.insert(QStringLiteral("chipset"), QStringLiteral("aga"));
    edited.insert(QStringLiteral("cpu_model"), QStringLiteral("68020"));
    edited.insert(QStringLiteral("unix.serial_port"), QStringLiteral("TCP://0.0.0.0:1234"));
    edited.insert(QStringLiteral("midiout_device"), QStringLiteral("-1"));
    edited.insert(QStringLiteral("midiout_device_name"), QStringLiteral("default"));
    edited.insert(QStringLiteral("midiin_device"), QStringLiteral("0"));
    edited.insert(QStringLiteral("midiin_device_name"), QStringLiteral("Loopback MIDI"));
    edited.insert(QStringLiteral("midirouter"), QStringLiteral("true"));
    edited.insert(QStringLiteral("unix.ui.config_path"), QStringLiteral("/configs"));
    edited.insert(QStringLiteral("unix.screenshot_path"), QStringLiteral("/screenshots"));
    config.applySettings(edited, {
        QStringLiteral("kickstart_rom_file"),
        QStringLiteral("kickstart_ext_rom_file"),
        QStringLiteral("chipset"),
        QStringLiteral("cpu_model"),
        QStringLiteral("serial_port"),
        QStringLiteral("unix.serial_port"),
        QStringLiteral("midiout_device"),
        QStringLiteral("midiout_device_name"),
        QStringLiteral("midiin_device"),
        QStringLiteral("midiin_device_name"),
        QStringLiteral("midirouter"),
        QStringLiteral("unix.ui.config_path"),
        QStringLiteral("unix.screenshot_path")
    });
    config.applyRepeatedSettings({
        { QStringLiteral("filesystem2"), QStringLiteral("rw,DH0:System:/new/System,0") },
        { QStringLiteral("filesystem2"), QStringLiteral("ro,DH1:Work:\"/new/Work,Disk\",5") }
    }, {
        QStringLiteral("filesystem2"),
        QStringLiteral("hardfile2"),
        QStringLiteral("uaehf0")
    });

    if (!config.save(outputPath, &error)) {
        qWarning().noquote() << error;
        return 1;
    }

    const QString output = readText(outputPath);
    bool ok = true;
    ok = requireContains(output, QStringLiteral("; keep this comment\n")) && ok;
    ok = requireContains(output, QStringLiteral("unknown_setting=keep-me\n")) && ok;
    ok = requireContains(output, QStringLiteral("malformed line without separator\n")) && ok;
    ok = requireContains(output, QStringLiteral("# keep this too\n")) && ok;
    ok = requireContains(output, QStringLiteral("; keep mount comment\n")) && ok;
    ok = requireContains(output, QStringLiteral("kickstart_rom_file=/new.rom\n")) && ok;
    ok = requireContains(output, QStringLiteral("chipset=aga\n")) && ok;
    ok = requireContains(output, QStringLiteral("cpu_model=68020\n")) && ok;
    ok = requireContains(output, QStringLiteral("unix.serial_port=TCP://0.0.0.0:1234\n")) && ok;
    ok = requireContains(output, QStringLiteral("midiout_device=-1\n")) && ok;
    ok = requireContains(output, QStringLiteral("midiout_device_name=default\n")) && ok;
    ok = requireContains(output, QStringLiteral("midiin_device=0\n")) && ok;
    ok = requireContains(output, QStringLiteral("midiin_device_name=Loopback MIDI\n")) && ok;
    ok = requireContains(output, QStringLiteral("midirouter=true\n")) && ok;
    ok = requireContains(output, QStringLiteral("unix.ui.config_path=/configs\n")) && ok;
    ok = requireContains(output, QStringLiteral("unix.screenshot_path=/screenshots\n")) && ok;
    ok = requireContains(output, QStringLiteral("filesystem2=rw,DH0:System:/new/System,0\n")) && ok;
    ok = requireContains(output, QStringLiteral("filesystem2=ro,DH1:Work:\"/new/Work,Disk\",5\n")) && ok;
    ok = requireCount(output, QStringLiteral("filesystem2="), 2) && ok;
    ok = requireBefore(output, QStringLiteral("filesystem2=rw,DH0:System:/new/System,0\n"), QStringLiteral("filesystem2=ro,DH1:Work:\"/new/Work,Disk\",5\n")) && ok;
    ok = requireNotContains(output, QStringLiteral("kickstart_ext_rom_file=")) && ok;
    ok = requireNotContains(output, QStringLiteral("/old.rom")) && ok;
    ok = requireNotContains(output, QStringLiteral("serial_port=TCP:127.0.0.1:1234\n")) && ok;
    ok = requireNotContains(output, QStringLiteral("midiout_device=-2\n")) && ok;
    ok = requireNotContains(output, QStringLiteral("midiout_device_name=none\n")) && ok;
    ok = requireNotContains(output, QStringLiteral("/old/System")) && ok;
    ok = requireNotContains(output, QStringLiteral("hardfile2=")) && ok;

    const QStringList args = config.commandArguments();
    ok = args.contains(QStringLiteral("unknown_setting=keep-me")) && ok;
    ok = args.contains(QStringLiteral("kickstart_rom_file=/new.rom")) && ok;
    ok = args.contains(QStringLiteral("unix.serial_port=TCP://0.0.0.0:1234")) && ok;
    ok = args.contains(QStringLiteral("midiout_device=-1")) && ok;
    ok = args.contains(QStringLiteral("midiin_device=0")) && ok;
    ok = args.contains(QStringLiteral("midirouter=true")) && ok;
    ok = !args.contains(QStringLiteral("unix.ui.config_path=/configs")) && ok;
    ok = args.contains(QStringLiteral("unix.screenshot_path=/screenshots")) && ok;
    ok = args.contains(QStringLiteral("filesystem2=rw,DH0:System:/new/System,0")) && ok;
    ok = args.contains(QStringLiteral("filesystem2=ro,DH1:Work:\"/new/Work,Disk\",5")) && ok;
    ok = requireArgBefore(args, QStringLiteral("filesystem2=rw,DH0:System:/new/System,0"), QStringLiteral("filesystem2=ro,DH1:Work:\"/new/Work,Disk\",5")) && ok;
    ok = !args.contains(QStringLiteral("kickstart_ext_rom_file=/old-ext.rom")) && ok;
    ok = requireCount(args.join(QLatin1Char('\n')), QStringLiteral("filesystem2="), 2) && ok;

    const QString expansionInputPath = QDir(tempDir.path()).filePath(QStringLiteral("expansion-input.uae"));
    const QString expansionOutputPath = QDir(tempDir.path()).filePath(QStringLiteral("expansion-output.uae"));
    const QString expansionInput =
        QStringLiteral("config_description=A4000\n")
        + QStringLiteral("a4091_rom_file=/old-a4091.rom\n")
        + QStringLiteral("hardfile2=rw,SCSI_0:/old/disk.hdf,0,0,0,512,0,,scsi0\n")
        + QStringLiteral("quickstart=A4000,1\n")
        + QStringLiteral("chipset_compatible=A4000\n");

    if (!writeText(expansionInputPath, expansionInput)) {
        return 1;
    }

    WinUaeQtConfig expansionConfig;
    if (!expansionConfig.load(expansionInputPath, &error)) {
        qWarning().noquote() << error;
        return 1;
    }

    const QStringList expansionKeys {
        QStringLiteral("a4091_rom_file"),
        QStringLiteral("a4091_rom_options")
    };
    const QStringList mountKeys {
        QStringLiteral("hardfile2")
    };

    expansionConfig.applySettings(WinUaeQtConfig::Settings(), expansionKeys);
    expansionConfig.applyRepeatedSettings(WinUaeQtConfig::OrderedSettings(), mountKeys);

    WinUaeQtConfig::Settings expansionEdited;
    expansionEdited.insert(QStringLiteral("chipset_compatible"), QStringLiteral("A4000"));
    expansionConfig.applySettings(expansionEdited, {
        QStringLiteral("quickstart"),
        QStringLiteral("chipset_compatible")
    });

    WinUaeQtConfig::Settings expansionBoardEdited;
    expansionBoardEdited.insert(QStringLiteral("a4091_rom_file"), QStringLiteral("/new-a4091.rom"));
    expansionConfig.applySettings(expansionBoardEdited, expansionKeys);
    expansionConfig.applyRepeatedSettings({
        { QStringLiteral("hardfile2"), QStringLiteral("rw,SCSI_0:/new/disk.hdf,0,0,0,512,0,,scsi0_a4091") }
    }, mountKeys);

    if (!expansionConfig.save(expansionOutputPath, &error)) {
        qWarning().noquote() << error;
        return 1;
    }

    const QString expansionOutput = readText(expansionOutputPath);
    ok = requireContains(expansionOutput, QStringLiteral("a4091_rom_file=/new-a4091.rom\n")) && ok;
    ok = requireContains(expansionOutput, QStringLiteral("hardfile2=rw,SCSI_0:/new/disk.hdf,0,0,0,512,0,,scsi0_a4091\n")) && ok;
    ok = requireBefore(expansionOutput, QStringLiteral("a4091_rom_file=/new-a4091.rom\n"), QStringLiteral("hardfile2=rw,SCSI_0:/new/disk.hdf,0,0,0,512,0,,scsi0_a4091\n")) && ok;
    ok = requireNotContains(expansionOutput, QStringLiteral("quickstart=")) && ok;
    ok = requireNotContains(expansionOutput, QStringLiteral("/old-a4091.rom")) && ok;
    ok = requireNotContains(expansionOutput, QStringLiteral("/old/disk.hdf")) && ok;

    const QString quickstartInputPath = QDir(tempDir.path()).filePath(QStringLiteral("quickstart-input.uae"));
    const QString quickstartOutputPath = QDir(tempDir.path()).filePath(QStringLiteral("quickstart-output.uae"));
    const QString quickstartInput =
        QStringLiteral("cpu_model=68020\n")
        + QStringLiteral("cpu_24bit_addressing=false\n")
        + QStringLiteral("quickstart=A1200,1\n")
        + QStringLiteral("gfxcard_size=4\n")
        + QStringLiteral("gfxcard_type=ZorroIII\n");
    if (!writeText(quickstartInputPath, quickstartInput)) {
        return 1;
    }

    WinUaeQtConfig quickstartConfig;
    if (!quickstartConfig.load(quickstartInputPath, &error)) {
        qWarning().noquote() << error;
        return 1;
    }
    quickstartConfig.moveSettingBefore(QStringLiteral("quickstart"), {
        QStringLiteral("cpu_model"),
        QStringLiteral("cpu_24bit_addressing"),
        QStringLiteral("gfxcard_size"),
        QStringLiteral("gfxcard_type")
    });
    WinUaeQtConfig::Settings quickstartEdited;
    quickstartEdited.insert(QStringLiteral("cpu_model"), QStringLiteral("68020"));
    quickstartEdited.insert(QStringLiteral("cpu_24bit_addressing"), QStringLiteral("false"));
    quickstartEdited.insert(QStringLiteral("gfxcard_size"), QStringLiteral("4"));
    quickstartEdited.insert(QStringLiteral("gfxcard_type"), QStringLiteral("ZorroIII"));
    quickstartConfig.applySettings(quickstartEdited, {
        QStringLiteral("quickstart"),
        QStringLiteral("cpu_model"),
        QStringLiteral("cpu_24bit_addressing"),
        QStringLiteral("gfxcard_size"),
        QStringLiteral("gfxcard_type")
    });
    if (!quickstartConfig.save(quickstartOutputPath, &error)) {
        qWarning().noquote() << error;
        return 1;
    }
    const QString quickstartOutput = readText(quickstartOutputPath);
    ok = requireNotContains(quickstartOutput, QStringLiteral("quickstart=")) && ok;
    ok = requireContains(quickstartOutput, QStringLiteral("cpu_model=68020\n")) && ok;
    ok = requireBefore(quickstartOutput, QStringLiteral("cpu_24bit_addressing=false\n"), QStringLiteral("gfxcard_size=4\n")) && ok;
    const QStringList quickstartArgs = quickstartConfig.commandArguments();
    ok = !quickstartArgs.contains(QStringLiteral("quickstart=A1200,1")) && ok;
    ok = quickstartArgs.contains(QStringLiteral("cpu_model=68020")) && ok;
    ok = quickstartArgs.contains(QStringLiteral("gfxcard_size=4")) && ok;

    return ok ? 0 : 1;
}
