import QtQuick
import QtQuick.Controls.Fusion as F
import QtQuick.Controls.Fusion.impl as FI

F.ToolButton {
    id: control
    background: FI.ButtonPanel {
        implicitWidth: 20
        implicitHeight: 20
        control: control
        visible: control.down || control.checked || control.highlighted || control.visualFocus
            || (enabled && control.hovered)
        border.color: !control.enabled ? control.palette.mid
            : highlighted ? F.Fusion.highlightedOutline(control.palette) : (StyleColours.themed ? StyleColours.control : control.palette.mid)
        FocusFrame { control: control; shown: control.visualFocus }
    }
}
