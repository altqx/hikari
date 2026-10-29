// Fusion's outline (the window colour darkened) all but vanishes on black panels.
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
