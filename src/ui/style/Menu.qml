import QtQuick
import QtQuick.Controls.Fusion as F

F.Menu {
    id: control
    // Fusion's menu stays at its background's 200 px and elides longer labels.
    readonly property real widestItem: {
        let widest = 0
        for (let i = 0; i < count; ++i) {
            const item = itemAt(i)
            if (item)
                widest = Math.max(widest, item.implicitWidth)
        }
        return widest
    }
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            Math.min(480, widestItem) + leftPadding + rightPadding)
    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 20
        color: control.palette.base
        // Fusion outlines its menu in the window colour darkened 140%, about 1.1:1 on dark panels.
        border.color: control.palette.mid

        Rectangle {
            z: -1
            x: 1; y: 1
            width: parent.width
            height: parent.height
            color: control.palette.shadow
            opacity: 0.2
        }
    }
}
