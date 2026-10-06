// K2: Fusion's frame with its outline in the theme's boundary colour
// (control.palette.mid, the line role; see CMakeLists.txt). Fusion draws it
// in Fusion.outline(), the window colour darkened 140%, about 1.1:1 against
// the Dark and High contrast black panels. Everything else is Fusion's.
import QtQuick
import QtQuick.Controls.Fusion as F

F.Frame {
    id: control
    background: Rectangle {
        color: "transparent"
        border.color: control.palette.mid
    }
}
