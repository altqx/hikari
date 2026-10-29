import QtQuick
import QtQuick.Controls.Fusion as F
import QtQuick.Controls.Fusion.impl as FI

F.Button {
    id: control
    background: FI.ButtonPanel {
        implicitWidth: 80
        implicitHeight: 24
        control: control
        visible: !control.flat || control.down || control.checked || control.highlighted || control.visualFocus
            || (enabled && control.hovered)
        // Fusion's outline (the window colour darkened 140%) is about 1.1:1 on dark panels.
        border.color: !control.enabled ? control.palette.mid
            : highlighted ? F.Fusion.highlightedOutline(control.palette) : (StyleColours.themed ? StyleColours.control : control.palette.mid)
        FocusFrame { control: control; shown: control.visualFocus }
    }
}
