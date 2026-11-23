import QtQuick
import QtQuick.Controls.Fusion as F

F.GroupBox {
    id: control
    background: Rectangle {
        y: control.topPadding - control.bottomPadding
        width: parent.width
        height: parent.height - control.topPadding + control.bottomPadding
        radius: 4
        color: "transparent"
        border.color: StyleColours.themed ? StyleColours.line : control.palette.mid
    }
}
