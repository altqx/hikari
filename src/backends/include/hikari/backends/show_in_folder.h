#pragma once

// P9: legacy SelectInFolder (HikariSub/config.cpp:1048-1062 at 20d647c4),
// used by the tab menu's "Open ... containing folder" items and a Ctrl+click
// on a recent file.
//  - Windows: as legacy, SHOpenFolderAndSelectItems on the file, so Explorer
//    shows its folder with the file selected.
//  - Linux: the desktop's file manager is asked to show the file selected
//    (org.freedesktop.FileManager1.ShowItems on the session bus); when no file
//    manager answers, the containing folder opens as legacy's Linux build
//    opened it (OpenInBrowser on wxFileName(path).GetPath(), or the path
//    itself when it has no folder part).

#include <QString>

#include <functional>

#ifndef _WIN32
class QDBusConnection;
#endif

namespace hikari::backends {

void selectInFolder(const QString &path);

// Legacy's Linux target: the path's folder, or the path itself without one.
QString containingFolder(const QString &path);

#ifndef _WIN32
// The Linux route on `bus`; `fallback` gets the folder to open when the call
// fails (no session bus, no FileManager1 service, an error reply).
void selectInFolder(const QString &path, const QDBusConnection &bus, std::function<void(const QString &folder)> fallback);
#endif

} // namespace hikari::backends
