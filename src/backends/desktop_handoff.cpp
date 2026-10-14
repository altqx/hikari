#include "hikari/backends/desktop_handoff.h"

#include "hikari/backends/show_in_folder.h"

#include <QDesktopServices>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>

#include <utility>

namespace hikari::backends::desktop {

namespace {

QMutex &routesMutex()
{
    static QMutex mutex;
    return mutex;
}

Routes &installed()
{
    static Routes routes;
    return routes;
}

Routes current()
{
    QMutexLocker lock(&routesMutex());
    return installed();
}

} // namespace

bool openUrl(const QUrl &url)
{
    if (const auto route = current().openUrl)
        return route(url);
    return QDesktopServices::openUrl(url);
}

void selectInFolder(const QString &path)
{
    if (const auto route = current().selectInFolder)
        return route(path);
    backends::selectInFolder(path);
}

bool startDetached(const QString &program, const QStringList &arguments)
{
    if (const auto route = current().startDetached)
        return route(program, arguments);
    return QProcess::startDetached(program, arguments);
}

Routes replaceRoutes(Routes routes)
{
    QMutexLocker lock(&routesMutex());
    return std::exchange(installed(), std::move(routes));
}

} // namespace hikari::backends::desktop
