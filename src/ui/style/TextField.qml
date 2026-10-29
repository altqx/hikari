// Fusion's own outline (window colour darkened 140%) is about 1.1:1 on dark panels.
import QtQuick
import QtQuick.Controls.Fusion as F
import QtQuick.Controls.Fusion.impl as FI

F.TextField {
    id: control
    background: FI.TextFieldBackground {
        control: control
        border.color: control.activeFocus ? F.Fusion.highlightedOutline(control.palette)
            : (control.enabled && StyleColours.themed ? StyleColours.control : control.palette.mid)
        FocusFrame {
            control: control
            shown: control.activeFocus && (control.focusReason === Qt.TabFocusReason
                || control.focusReason === Qt.BacktabFocusReason || control.focusReason === Qt.ShortcutFocusReason)
        }
    }
}
