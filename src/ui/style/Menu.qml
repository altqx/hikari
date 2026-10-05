// K2: Fusion's menu with its outline in the theme's boundary colour
// (control.palette.mid, the line role; see CMakeLists.txt). Fusion draws it
// in Fusion.outline(), the window colour darkened 140%, about 1.1:1 against
// the Dark and High contrast black panels. Its width follows its items
// (below). Everything else is Fusion's.
import QtQuick
import QtQuick.Controls.Fusion as F

F.Menu {
    id: control
    // UI polish: as wide as its widest item (Fusion's menu stays at its
    // background's 200 and elides longer labels), up to 480, beyond which
    // the items elide.
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
