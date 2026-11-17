import QtQuick
import QtQuick.Controls.Fusion as F

F.RadioButton {
    id: control
    indicator: Rectangle {
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding) : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: 14
        implicitHeight: 14
        radius: width / 2
        color: StyleColours.themed ? StyleColours.field : control.palette.base
        border.color: !StyleColours.themed ? control.palette.mid
            : !control.enabled ? StyleColours.disabled
            : control.checked ? StyleColours.accent : StyleColours.control
        StateOverlay { control: control; pressed: control.down }
        Rectangle {
            objectName: "radioDot"
            anchors.centerIn: parent
            width: 6
            height: 6
            radius: 3
            visible: control.checked
            color: !StyleColours.themed ? control.palette.text : control.enabled ? StyleColours.accent : StyleColours.disabled
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
