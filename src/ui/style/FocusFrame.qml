// K2: the style's keyboard focus ring (visual-language.md, "Keyboard
// focus"; the user's 2026-10-13 decision): 1 wide in the theme's focus role,
// the text colour (StyleColours.focus; without a profile the palette's text
// colour), drawn tight: `offset` 0 lays it just outside the control it
// fills, touching its edge; a negative offset lays it inside (-2: 1 inside
// the edge, where a tab bar or a list would clip it). Square, so its sides
// are whole pixels. Shown while `shown` (the control's keyboard focus); the
// controls below use it. Hikari.Ui's FocusRing is the same ring for
// controls the shell draws itself, under any controls style.
import QtQuick

Rectangle {
    id: ring
    required property Item control
    property bool shown: false
    property int offset: 0
    objectName: "focusRing"
    anchors.fill: parent
    anchors.margins: -(offset + border.width)
    color: "transparent"
    border.width: 1
    border.color: StyleColours.themed ? StyleColours.focus : ring.control.palette.windowText
    visible: ring.shown
}
