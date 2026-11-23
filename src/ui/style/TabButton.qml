import QtQuick
import QtQuick.Controls.impl
import QtQuick.Controls.Fusion as F

F.TabButton {
    id: control
    leftPadding: 10
    rightPadding: 10
    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        icon: control.icon
        text: control.text
        font: control.checked ? FontWeights.semibold(control.font) : control.font
        color: !StyleColours.themed ? control.palette.buttonText
            : !control.enabled ? StyleColours.disabled
            : control.checked ? StyleColours.text : StyleColours.muted
    }
    background: Rectangle {
        implicitHeight: 30
        border.width: 0
        border.color: StyleColours.themed ? StyleColours.line : control.palette.mid
        color: StyleColours.themed ? "transparent" : (control.checked ? control.palette.window : control.palette.button)
        StateOverlay { control: control; pressed: control.down }
        Rectangle {
            objectName: "tabUnderline"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 2
            visible: control.checked
            color: StyleColours.themed ? StyleColours.accent : control.palette.highlight
        }
        FocusFrame { control: control; shown: control.visualFocus; offset: -2 }
    }
}
