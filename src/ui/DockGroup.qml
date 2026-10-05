import QtQuick
import Hikari.Ui
import "qrc:/kddockwidgets/qtquick/views/qml/" as KDDW

// K2: a docked group's frame (the engine's Group.qml draws a fixed "#b8b8b8"
// border) in the theme's boundary colour. Loaded through the adapter's view
// factory (ui/docking.cpp).
KDDW.Group {
    border.color: Theme.line
}
