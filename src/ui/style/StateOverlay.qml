import QtQuick

Rectangle {
    required property Item control
    property bool pressed: false
    anchors.fill: parent
    radius: parent.radius
    color: pressed ? StyleColours.pressed : StyleColours.hover
    visible: StyleColours.themed && control.enabled && (pressed || control.hovered)
}
