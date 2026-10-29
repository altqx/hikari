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
        // Fusion's own outline (window colour darkened 140%) is about 1.1:1 on dark panels.
        border.color: control.palette.mid
    }
}
