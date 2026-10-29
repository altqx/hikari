import QtQuick
import QtQuick.Window
import Hikari.Ui

Item {
    id: mark
    property bool shown: false
    readonly property bool mirrored: LayoutMirroring.enabled
    readonly property real dpr: Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1
    readonly property real leg: Math.max(1, Math.round(8 * mark.dpr)) / mark.dpr
    readonly property color colour: Theme.accent
    objectName: "panelFocusMark"
    visible: mark.shown
    width: mark.leg
    height: mark.leg
    x: mark.mirrored ? 0 : (mark.parent ? mark.parent.width - mark.width : 0)
    y: 0
    clip: true
    Accessible.ignored: true

    Rectangle {
        width: mark.leg * Math.SQRT2
        height: width
        x: (mark.mirrored ? 0 : mark.leg) - width / 2
        y: -height / 2
        rotation: 45
        antialiasing: true
        color: mark.colour
    }
}
