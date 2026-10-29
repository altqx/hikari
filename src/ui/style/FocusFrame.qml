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
