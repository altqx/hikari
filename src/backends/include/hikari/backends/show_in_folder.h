#pragma once

#include <QString>

#include <functional>

#ifndef _WIN32
class QDBusConnection;
#endif

namespace hikari::backends {

// Windows: SHOpenFolderAndSelectItems. Elsewhere: org.freedesktop.FileManager1.ShowItems, else
// the containing folder opens.
void selectInFolder(const QString &path);

// The path's folder, or the path itself when it has none.
QString containingFolder(const QString &path);

#ifndef _WIN32
// The Linux route on `bus`; `fallback` gets the folder to open when the call
// fails (no session bus, no FileManager1 service, an error reply).
void selectInFolder(const QString &path, const QDBusConnection &bus, std::function<void(const QString &folder)> fallback);
#endif

}
