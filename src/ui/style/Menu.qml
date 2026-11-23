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
    // A ListView doesn't lay out again when a row becomes visible while the menu is closed.
    onAboutToShow: (contentItem as ListView)?.forceLayout()
    Binding { target: control.palette; property: "text"; value: StyleColours.text; when: StyleColours.themed }
    Binding { target: control.palette; property: "windowText"; value: StyleColours.text; when: StyleColours.themed }
    Binding { target: control.palette; property: "highlight"; value: StyleColours.accent; when: StyleColours.themed }
    Binding { target: control.palette; property: "highlightedText"; value: StyleColours.onAccent; when: StyleColours.themed }
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            Math.min(480, widestItem) + leftPadding + rightPadding)
    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 20
        radius: 6
        color: StyleColours.themed ? StyleColours.panel : control.palette.base
        border.color: StyleColours.themed ? StyleColours.line : control.palette.mid

        Rectangle {
            z: -1
            x: 1; y: 1
            width: parent.width
            height: parent.height
            radius: parent.radius
            color: control.palette.shadow
            opacity: 0.2
            visible: !StyleColours.highContrast
        }
    }
}
