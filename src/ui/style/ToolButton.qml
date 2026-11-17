import QtQuick
import QtQuick.Controls.Fusion as F

F.ToolButton {
    id: control
    background: Rectangle {
        implicitWidth: 24
        implicitHeight: 24
        radius: 4
        color: !StyleColours.themed ? (control.down || control.checked ? control.palette.mid
                                       : control.hovered ? control.palette.midlight : "transparent")
            : control.checked && control.enabled ? StyleColours.select : "transparent"
        border.width: StyleColours.highContrast && control.enabled && (control.checked || control.hovered) ? 1 : 0
        border.color: control.checked ? StyleColours.accent : StyleColours.line
        StateOverlay { control: control; pressed: control.down }
        FocusFrame { control: control; shown: control.visualFocus }
    }
}
