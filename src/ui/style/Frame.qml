import QtQuick
import QtQuick.Controls.Fusion as F

F.Frame {
    id: control
    background: Rectangle {
        color: "transparent"
        border.color: StyleColours.themed ? StyleColours.line : control.palette.mid
    }
}
