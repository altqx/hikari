// K2: the keyboard focus ring of a control the shell draws itself (the
// Appearance page's theme cards and accent swatches): visual-language.md's
// "Keyboard focus" (the user's 2026-10-13 decision), 1 wide, just outside
// the control and touching its edge, in the theme's focus role, the text
// colour (MuseScore 4's convention), which stays apart from the accent that
// marks the chosen card or swatch. Shown while the control has keyboard
// focus (visualFocus). Place it in the control's background; set radius to
// the background's plus 1. The HikariStyle controls draw the same ring
// (style/FocusFrame.qml).
import QtQuick
import QtQuick.Templates as T
import Hikari.Ui

Rectangle {
    id: ring
    required property T.Control control
    readonly property int offset: 0
    objectName: "focusRing"
    anchors.fill: parent
    anchors.margins: -(offset + border.width)
    radius: 0
    color: "transparent"
    border.width: 1
    border.color: Theme.focus
    visible: ring.control.visualFocus
}
