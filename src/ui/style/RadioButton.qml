import QtQuick
import QtQuick.Controls.Fusion as F
import QtQuick.Controls.Fusion.impl as FI

F.RadioButton {
    id: control
    indicator: FI.RadioIndicator {
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding) : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        control: control
        // Fusion's own outline (window colour darkened 140%) is about 1.1:1 on dark panels.
        border.color: (control.enabled && StyleColours.themed ? StyleColours.control : control.palette.mid)
    }
    background: Item {
        FocusFrame { control: control; shown: control.visualFocus }
    }
}
