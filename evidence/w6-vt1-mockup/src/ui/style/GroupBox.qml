// K2: Fusion's group box with its outline in the theme's boundary colour
// (control.palette.mid, the line role; see CMakeLists.txt). Fusion draws it
// in Fusion.outline(), the window colour darkened 140%, about 1.1:1 against
// the Dark and High contrast black panels. Everything else is Fusion's.
import QtQuick
import QtQuick.Controls.Fusion as F

F.GroupBox {
    id: control
    background: Rectangle {
        y: control.topPadding - control.bottomPadding
        width: parent.width
        height: parent.height - control.topPadding + control.bottomPadding
        radius: 2
        color: Qt.rgba(0, 0, 0, 3 / 255)
        border.color: control.palette.mid
    }
}
