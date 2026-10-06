import QtQuick
import Hikari.Ui
import "qrc:/kddockwidgets/qtquick/views/qml/" as KDDW

// K2: a floating panel window's frame (the engine's FloatingWindow.qml draws a
// fixed "#666666" border) in the theme's boundary colour, over the theme's
// application background. Loaded through the adapter's view factory
// (ui/docking.cpp).
KDDW.FloatingWindow {
    color: Theme.background
    border.color: Theme.line
}
