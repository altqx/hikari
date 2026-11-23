import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls.Fusion as F

F.TabBar {
    id: control
    background: Item {
        implicitHeight: 30

        Rectangle {
            width: parent.width
            height: 1
            y: control.position === T.TabBar.Header ? parent.height - 1 : 0
            color: StyleColours.themed ? StyleColours.line : control.palette.mid
        }
    }
}
