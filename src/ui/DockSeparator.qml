import QtQuick
import Hikari.Ui
import "qrc:/kddockwidgets/qtquick/views/qml/" as KDDW

// K2: the docking engine's separator between panels (its Separator.qml
// paints a fixed "#eff0f1") in the theme's application background, the gutter
// between panels. Loaded through the adapter's view factory (ui/docking.cpp).
KDDW.Separator {
    color: Theme.background
}
