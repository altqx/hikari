import QtQuick

Rectangle {
    required property Item control
    property bool open: false
    property bool editing: control.activeFocus
    property bool keyboardFocus: false
    implicitWidth: 120
    implicitHeight: 24
    radius: 4
    color: StyleColours.themed ? StyleColours.field : control.palette.base
    border.color: !StyleColours.themed ? (control.activeFocus ? control.palette.highlight : control.palette.mid)
        : !control.enabled ? StyleColours.disabled
        : editing || open ? StyleColours.accent
        : control.hovered ? StyleColours.text : StyleColours.control
    FocusFrame { control: parent.control; shown: parent.keyboardFocus }
}
