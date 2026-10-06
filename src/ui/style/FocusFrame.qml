// K2: the style's keyboard focus ring (visual-language.md, "Keyboard
// focus"; MuseScore 4's NavigationFocusBorder): 2 wide in the theme's focus
// role, the text colour (StyleColours.focus; without a profile the palette's
// text colour), `offset` beyond the control it fills (negative: inside it,
// where a tab bar or a list would clip it). Square, so its sides are whole
// pixels. Shown while `shown` (the control's keyboard focus); the controls
// below use it. Hikari.Ui's FocusRing is the same ring for controls the
// shell draws itself, under any controls style.
import QtQuick

Rectangle {
    id: ring
    required property Item control
    property bool shown: false
    property int offset: 3
    objectName: "focusRing"
    anchors.fill: parent
    anchors.margins: -(offset + border.width)
    color: "transparent"
    border.width: 2
    border.color: StyleColours.themed ? StyleColours.focus : ring.control.palette.windowText
    visible: ring.shown
}
