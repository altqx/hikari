// 38e: backends::desktop's routes in the tests (desktop_guard.h).

#include "desktop_guard.h"

#include "hikari/backends/desktop_handoff.h"

#include <QCoreApplication>
#include <QDesktopServices>

namespace hikari::test::desktop {

namespace {

bool openUrl(const QUrl &url)
{
    if (!recordedSchemes().contains(url.scheme())) {
        refuse(url);
        return false;
    }
    // QDesktopServices::openUrl, so the recording handler takes it as it
    // takes a QML Qt.openUrlExternally.
    const qsizetype before = opened().size();
    const bool result = QDesktopServices::openUrl(url);
    if (!recordingHandlerTook(url, before))
        refuse(url); // a test unset the handler: the desktop may have had it
    return result;
}

void replaceTheDesktopsRoutes()
{
    backends::desktop::replaceRoutes({.openUrl = openUrl,
                                      .selectInFolder = recordRevealed,
                                      .startDetached = recordStarted});
}

} // namespace

} // namespace hikari::test::desktop

namespace {

void replaceTheDesktopRoutes()
{
    hikari::test::desktop::replaceTheDesktopsRoutes();
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(replaceTheDesktopRoutes)
