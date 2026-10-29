// Fusion's own outline (window colour darkened 140%) is about 1.1:1 on dark panels.
import QtQuick
import QtQuick.Controls.Fusion as F

F.Frame {
    id: control
    background: Rectangle {
        color: "transparent"
        border.color: control.palette.mid
    }
}
