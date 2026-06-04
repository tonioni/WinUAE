#include "path_utils.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>

QString winUaeQtEnvString(const char *name)
{
    return QString::fromLocal8Bit(qgetenv(name));
}

QString winUaeQtDefaultDataPath()
{
    return QStringLiteral("$HOME/Documents/WinUAE");
}

QString winUaeQtDefaultDataSubPath(const QString &name)
{
    return QDir(winUaeQtDefaultDataPath()).filePath(name);
}

QString winUaeQtDefaultFileDialogPath()
{
    return winUaeQtExpandedPathText(winUaeQtDefaultDataPath());
}

QString winUaeQtExpandUnixPath(QString path)
{
    if (path.isEmpty()) {
        return path;
    }

    if (path.startsWith(QLatin1Char('~')) && (path.size() == 1 || path[1] == QLatin1Char('/'))) {
        path = QDir::homePath() + path.mid(1);
    }

    for (int i = 0; i < path.size(); i++) {
        if (path[i] != QLatin1Char('$')) {
            continue;
        }

        int start = i + 1;
        int end = start;
        QString name;
        if (start < path.size() && path[start] == QLatin1Char('{')) {
            start++;
            end = path.indexOf(QLatin1Char('}'), start);
            if (end < 0) {
                continue;
            }
            name = path.mid(start, end - start);
            end++;
        } else {
            while (end < path.size()) {
                const QChar c = path[end];
                if (!c.isLetterOrNumber() && c != QLatin1Char('_')) {
                    break;
                }
                end++;
            }
            name = path.mid(start, end - start);
        }
        if (name.isEmpty()) {
            continue;
        }

        const QByteArray value = qgetenv(name.toLocal8Bit().constData());
        if (value.isEmpty()) {
            continue;
        }
        const QString replacement = QString::fromLocal8Bit(value);
        path.replace(i, end - i, replacement);
        i += replacement.size() - 1;
    }

    return path;
}

QString winUaeQtExpandedPathText(const QString &path)
{
    return winUaeQtExpandUnixPath(path.trimmed());
}

QString winUaeQtFileDialogInitialPath(const QString &path)
{
    const QString expandedPath = winUaeQtExpandedPathText(path);
    return expandedPath.isEmpty() ? winUaeQtDefaultFileDialogPath() : expandedPath;
}

QString winUaeQtFileDialogInitialDirectory(const QString &path)
{
    const QString expandedPath = winUaeQtExpandedPathText(path);
    if (expandedPath.isEmpty()) {
        return winUaeQtDefaultFileDialogPath();
    }
    QFileInfo info(expandedPath);
    if (info.isDir()) {
        return info.absoluteFilePath();
    }
    const QString parent = info.absolutePath();
    return parent.isEmpty() ? winUaeQtDefaultFileDialogPath() : parent;
}

QString winUaeQtFileDialogInitialSavePath(const QString &path, const QString &fallbackName)
{
    const QString expandedPath = winUaeQtExpandedPathText(path);
    if (!expandedPath.isEmpty()) {
        return expandedPath;
    }
    return QDir(winUaeQtDefaultFileDialogPath()).filePath(fallbackName);
}
