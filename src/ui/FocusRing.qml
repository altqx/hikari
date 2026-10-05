// K2: the keyboard focus ring of a control the shell draws itself (the
// Appearance page's theme cards and accent swatches): visual-language.md's
// "Keyboard focus ring", 2 wide, 3 beyond the control, in the theme's focus
// role, which stays apart from the accent that marks the chosen card or
// swatch. Shown while the control has keyboard focus (visualFocus). Place it
// in the control's background; set radius to the background's.
import QtQuick
import Hikari.Ui

Rectangle {
    id: ring
    required property Item control
    readonly property int offset: 3
    objectName: "focusRing"
    anchors.fill: parent
    anchors.margins: -(offset + border.width)
    radius: 0
    color: "transparent"
    border.width: 2
    border.color: Theme.focus
    visible: ring.control.visualFocus === true
}
