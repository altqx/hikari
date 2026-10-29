#include "theme.h"

#include <QCoreApplication>
#include <QDir>

namespace {

// Offscreen on Windows reads fonts from QT_QPA_FONTDIR, else Qt's lib/fonts, which Qt no longer ships.
#ifdef _WIN32
[[maybe_unused]] const bool kSystemFonts = [] {
    const QString windows = qEnvironmentVariable("WINDIR");
    if (!qEnvironmentVariableIsSet("QT_QPA_FONTDIR") && !windows.isEmpty())
        qputenv("QT_QPA_FONTDIR", QDir(windows).filePath(QStringLiteral("Fonts")).toLocal8Bit());
    return true;
}();
#endif

// Before any QML loads: Qt's default native Windows style divides by zero (0xC0000094) polishing an offscreen window.
void chooseTheApplicationsControlsStyle()
{
    hikari::ui::theme::chooseControlsStyle();
}

}

Q_COREAPP_STARTUP_FUNCTION(chooseTheApplicationsControlsStyle)
