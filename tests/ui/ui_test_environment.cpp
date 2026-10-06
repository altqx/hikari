// The environment every UI test starts in, linked into each executable in
// tests/ui (CMakeLists.txt), so a new test or workflow cannot miss it.
//
// The controls style composition.cpp chooses (theme::chooseControlsStyle),
// set as the test's application starts, before any engine loads QML.
// Without it a test runs Qt's default style for the platform; on Windows
// that is the native style, which the application never shows: its theme
// data does not open offscreen and it divides by zero (0xC0000094)
// polishing a window.

#include "theme.h"

#include <QCoreApplication>

namespace {

void chooseTheApplicationsControlsStyle()
{
    hikari::ui::theme::chooseControlsStyle();
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(chooseTheApplicationsControlsStyle)
