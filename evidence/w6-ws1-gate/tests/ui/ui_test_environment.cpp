// The environment every UI test starts in, linked into each executable in
// tests/ui (CMakeLists.txt), so a new test or workflow cannot miss it.
//
// The controls style composition.cpp chooses (theme::chooseControlsStyle),
// set as the test's application starts, before any engine loads QML.
// Without it a test runs Qt's default style for the platform; on Windows
// that is the native style, which the application never shows: its theme
// data does not open offscreen and it divides by zero (0xC0000094)
// polishing a window.
//
// On Windows, offscreen, the system's fonts. That platform reads font files
// from QT_QPA_FONTDIR, else from Qt's lib/fonts, which Qt no longer ships
// ("QFontDatabase: Cannot find font directory"). Without fonts every glyph is
// a box as wide as the font's size, so text measures about twice what the
// application shows with Windows' fonts, and the panels' minimum sizes, menus
// and control rows outgrow the window. The windows platform ignores the
// variable (it asks DirectWrite); one set beforehand is kept.

#include "theme.h"

#include <QCoreApplication>
#include <QDir>

namespace {

#ifdef _WIN32
[[maybe_unused]] const bool kSystemFonts = [] {
    const QString windows = qEnvironmentVariable("WINDIR");
    if (!qEnvironmentVariableIsSet("QT_QPA_FONTDIR") && !windows.isEmpty())
        qputenv("QT_QPA_FONTDIR", QDir(windows).filePath(QStringLiteral("Fonts")).toLocal8Bit());
    return true;
}();
#endif

void chooseTheApplicationsControlsStyle()
{
    hikari::ui::theme::chooseControlsStyle();
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(chooseTheApplicationsControlsStyle)
