import QtQuick
import QtQuick.Controls.impl
import QtQuick.Controls.Fusion as F

F.Button {
    id: control
    readonly property bool themed: StyleColours.themed
    leftPadding: 12
    rightPadding: 12
    topPadding: 3
    bottomPadding: 3
    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        icon: control.icon
        text: control.text
        font: control.highlighted && control.themed ? FontWeights.semibold(control.font) : control.font
        color: !control.themed ? control.palette.buttonText
            : !control.enabled ? StyleColours.disabled
            : control.highlighted ? StyleColours.onAccent : StyleColours.text
    }
    background: Rectangle {
        implicitWidth: 80
        implicitHeight: 24
        radius: 4
        visible: !control.flat || control.down || control.checked || control.highlighted || control.visualFocus
            || (control.enabled && control.hovered)
        color: !control.themed ? (control.down || control.checked ? control.palette.mid
                                  : control.enabled && control.hovered ? control.palette.midlight : control.palette.button)
            : !control.enabled || control.flat ? "transparent"
            : control.highlighted ? StyleColours.accent : StyleColours.raised
        border.width: control.flat && !control.highlighted ? 0 : 1
        border.color: !control.themed ? control.palette.mid
            : !control.enabled ? StyleColours.disabled
            : control.highlighted ? StyleColours.accent : StyleColours.control
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            visible: control.themed && control.enabled && (control.hovered || control.down || control.checked)
            color: control.highlighted ? Qt.alpha(StyleColours.text, control.down ? 0.2 : 0.12)
                : control.down || control.checked ? StyleColours.pressed : StyleColours.hover
        }
        FocusFrame { control: control; shown: control.visualFocus }
    }
}
