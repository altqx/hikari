import QtQuick
import Hikari.Ui
import "qrc:/kddockwidgets/qtquick/views/qml/" as KDDW

// D1, K2: the docking engine's separator between panels (its Separator.qml
// paints a fixed "#eff0f1") in the theme's application background, the gutter
// between panels, so the seams follow the appearance. Loaded through the
// adapter's view factory (ui/docking.cpp).
KDDW.Separator {
    objectName: "dockSeparator"
    color: Theme.background
}
