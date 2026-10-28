// K2: Fusion's tab bar with its outline in the theme's boundary colour
// (control.palette.mid, the line role; see CMakeLists.txt). Fusion draws it
// in Fusion.outline(), the window colour darkened 140%, about 1.1:1 against
// the Dark and High contrast black panels. Everything else is Fusion's.
import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls.Fusion as F

F.TabBar {
    id: control
    background: Item {
        implicitHeight: 21

        Rectangle {
            width: parent.width
            height: 1
            y: control.position === T.TabBar.Header ? parent.height - 1 : 0
            color: control.palette.mid
        }
    }
}
