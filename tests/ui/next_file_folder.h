#pragma once

#include <QString>
#include <QStringList>

namespace hikari::tests {

// Unsorted: the order readdir / FindNextFileW return entries in.
QStringList enumerateFolder(const QString &folder);
bool setHiddenAttribute(const QString &path);
bool setSystemAttribute(const QString &path);
QString folderFileSystem(const QString &folder);

}
