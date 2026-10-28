// K2: the mark of the panel holding the keyboard focus (visual-language.md,
// "Keyboard focus"; the user's 2026-10-13 decision, in place of the ring
// round the focused panel's header): a small filled right triangle in the
// accent, its legs flush with the panel's top and trailing edges (the top
// right corner; the top left in a right-to-left layout). The legs are 8
// logical pixels, rounded to whole device pixels. It draws above the
// header's content and takes no input. The accent role is the high-contrast
// accent pick in those themes, at least 3:1 on the header's surfaces
// (hikari_ui_theme_tests panelFocusMarkContrast).
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

    // A square turned 45 degrees about the corner: the clip leaves the
    // triangle between the two edges and its diagonal.
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
