import QtQuick
import QtQuick.Controls.Fusion as F

F.CheckBox {
    id: control
    indicator: Rectangle {
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding) : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: 14
        implicitHeight: 14
        radius: 3
        readonly property bool on: control.checkState !== Qt.Unchecked
        readonly property color ink: !StyleColours.themed ? control.palette.text : StyleColours.onAccent
        color: !StyleColours.themed ? control.palette.base
            : on && control.enabled ? StyleColours.accent : StyleColours.field
        border.color: !StyleColours.themed ? control.palette.mid
            : !control.enabled ? StyleColours.disabled
            : on ? StyleColours.accent : StyleColours.control
        StateOverlay { control: control; pressed: control.down }
        Item {
            objectName: "checkMark"
            anchors.fill: parent
            visible: control.checkState === Qt.Checked
            Rectangle {
                x: 2.5; y: 7; width: 4.5; height: 1.6; rotation: 45; antialiasing: true
                color: control.enabled ? parent.parent.ink : StyleColours.disabled
            }
            Rectangle {
                x: 4.6; y: 6; width: 8; height: 1.6; rotation: -50; antialiasing: true
                color: control.enabled ? parent.parent.ink : StyleColours.disabled
            }
        }
        Rectangle {
            objectName: "partialMark"
            anchors.centerIn: parent
            width: 8
            height: 2
            visible: control.checkState === Qt.PartiallyChecked
            color: control.enabled ? parent.ink : StyleColours.disabled
        }
        FocusFrame { control: control; shown: control.visualFocus }
    }
    contentItem: F.Label {
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0
        text: control.text
        font: control.font
        color: StyleColours.themed ? (control.enabled ? StyleColours.text : StyleColours.disabled) : control.palette.windowText
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
}
