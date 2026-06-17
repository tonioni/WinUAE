#pragma once

#include <QString>

QString winUaeQtEnvString(const char *name);
QString winUaeQtDefaultDataPath();
QString winUaeQtDefaultDataSubPath(const QString &name);
QString winUaeQtDefaultFileDialogPath();
QString winUaeQtExpandUnixPath(QString path);
QString winUaeQtExpandedPathText(const QString &path);
QString winUaeQtFileDialogInitialPath(const QString &path);
QString winUaeQtFileDialogInitialDirectory(const QString &path);
QString winUaeQtFileDialogInitialSavePath(const QString &path, const QString &fallbackName);
