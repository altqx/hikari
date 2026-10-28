#pragma once

// V3: the folder as the system enumerates it, for the shell tests'
// comparison with legacy's NextFile observation (Windows calls kept out of
// shell_tests.cpp).

#include <QString>
#include <QStringList>

namespace hikari::tests {

// Every entry of `folder` but "." and "..", in the order readdir (Linux) or
// FindFirstFileW / FindNextFileW (Windows) return them.
QStringList enumerateFolder(const QString &folder);
// Windows: gives `path` FILE_ATTRIBUTE_HIDDEN; elsewhere does nothing. True
// when done.
bool setHiddenAttribute(const QString &path);
// Windows: gives `path` FILE_ATTRIBUTE_SYSTEM alone; elsewhere does nothing.
// True when done.
bool setSystemAttribute(const QString &path);
// Windows: the file system of the volume holding `folder` ("NTFS", "ReFS");
// elsewhere an empty string.
QString folderFileSystem(const QString &folder);

} // namespace hikari::tests
