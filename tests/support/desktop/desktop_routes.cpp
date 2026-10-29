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
    const qsizetype before = opened().size();
    const bool result = QDesktopServices::openUrl(url);
    if (!recordingHandlerTook(url, before))
        refuse(url);
    return result;
}

void replaceTheDesktopsRoutes()
{
    backends::desktop::replaceRoutes({.openUrl = openUrl,
                                      .selectInFolder = recordRevealed,
                                      .startDetached = recordStarted});
}

}

}

namespace {

void replaceTheDesktopRoutes()
{
    hikari::test::desktop::replaceTheDesktopsRoutes();
}

}

Q_COREAPP_STARTUP_FUNCTION(replaceTheDesktopRoutes)
