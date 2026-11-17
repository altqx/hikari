import QtQuick
import QtQuick.Controls.Fusion as F

F.Slider {
    id: control
    handle: Rectangle {
        objectName: "sliderHandle"
        readonly property real size: control.pressed ? 14 : 12
        x: control.leftPadding + Math.round(control.horizontal ? control.visualPosition * (control.availableWidth - width) : (control.availableWidth - width) / 2)
        y: control.topPadding + Math.round(control.horizontal ? (control.availableHeight - height) / 2 : control.visualPosition * (control.availableHeight - height))
        implicitWidth: 14
        implicitHeight: 14
        width: size
        height: size
        radius: size / 2
        color: !StyleColours.themed ? control.palette.button : control.enabled ? StyleColours.text : StyleColours.disabled
        border.width: 1
        border.color: !StyleColours.themed ? control.palette.mid : control.enabled ? StyleColours.control : StyleColours.disabled
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: width / 2
            color: "transparent"
            border.width: 2
            border.color: StyleColours.themed ? StyleColours.panel : "transparent"
        }
        Rectangle {
            anchors.centerIn: parent
            width: parent.width + 6
            height: width
            radius: width / 2
            z: -1
            color: StyleColours.hover
            visible: StyleColours.themed && control.enabled && control.hovered && !control.pressed
        }
        FocusFrame { control: control; shown: control.visualFocus; radius: width / 2 }
    }
    background: Rectangle {
        objectName: "sliderGroove"
        x: control.leftPadding + (control.horizontal ? 0 : (control.availableWidth - width) / 2)
        y: control.topPadding + (control.horizontal ? (control.availableHeight - height) / 2 : 0)
        implicitWidth: control.horizontal ? 160 : 3
        implicitHeight: control.horizontal ? 3 : 160
        width: control.horizontal ? control.availableWidth : 3
        height: control.horizontal ? 3 : control.availableHeight
        radius: 1.5
        color: !StyleColours.themed ? control.palette.mid
            : !control.enabled ? StyleColours.disabled
            : StyleColours.highContrast ? StyleColours.muted : StyleColours.line
        Rectangle {
            objectName: "sliderFill"
            x: control.horizontal && control.mirrored ? parent.width - width : 0
            y: control.horizontal ? 0 : parent.height - height
            width: control.horizontal ? parent.width * control.position : parent.width
            height: control.horizontal ? parent.height : parent.height * control.position
            radius: parent.radius
            visible: control.enabled
            color: StyleColours.themed ? StyleColours.accent : control.palette.highlight
        }
    }
}
