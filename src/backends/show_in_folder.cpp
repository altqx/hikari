#include "hikari/backends/show_in_folder.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#else
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#endif

namespace hikari::backends {

QString containingFolder(const QString &path)
{
    // wxFileName(filename).GetPath(): everything before the last separator
    // (on Linux '/'; on Windows '\\' or '/'), or the path when there is none.
    const QString native = QDir::fromNativeSeparators(path);
    const qsizetype at = native.lastIndexOf(u'/');
    QString folder = at > 0 ? native.left(at) : at == 0 ? QStringLiteral("/") : QString();
    if (folder.isEmpty())
        folder = path;
    return QDir::toNativeSeparators(folder);
}

#ifdef _WIN32

void selectInFolder(const QString &path)
{
    // Legacy SelectInFolder, as written (config.cpp:1050-1058).
    CoInitialize(nullptr);
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    if (PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(native.c_str())) {
        SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
        ILFree(pidl);
    }
    CoUninitialize();
}

#else

void selectInFolder(const QString &path, const QDBusConnection &bus, std::function<void(const QString &folder)> fallback)
{
    const QString folder = containingFolder(path);
    if (!bus.isConnected()) {
        fallback(folder);
        return;
    }
    // ShowItems(as uris, s startupId): the freedesktop file manager interface.
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.FileManager1"),
                                                       QStringLiteral("/org/freedesktop/FileManager1"),
                                                       QStringLiteral("org.freedesktop.FileManager1"),
                                                       QStringLiteral("ShowItems"));
    call << QStringList{QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()).toString(QUrl::FullyEncoded)}
         << QString();
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call, 10000));
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher,
                     [fallback = std::move(fallback), folder](QDBusPendingCallWatcher *self) {
                         const QDBusPendingReply<> reply = *self;
                         if (reply.isError())
                             fallback(folder);
                         self->deleteLater();
                     });
}

void selectInFolder(const QString &path)
{
    selectInFolder(path, QDBusConnection::sessionBus(),
                   [](const QString &folder) { QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); });
}

#endif

} // namespace hikari::backends
