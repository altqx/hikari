// K2: Fusion's text area with its outline in the theme's boundary colour
// (control.palette.mid, the line role; see CMakeLists.txt). Fusion draws it
// in Fusion.outline(), the window colour darkened 140%, about 1.1:1 against
// the Dark and High contrast black panels. Focused, its border is the
// accent's outline (the K2 card: the accent drives the focused field
// border); with keyboard focus (Tab, Backtab, a shortcut) the style's ring
// (FocusFrame: 1 wide, just outside it, in the theme's focus role, the
// text colour) is drawn beside it. Everything else is Fusion's.
// K2-control-boundary (2026-10-15): enabled, the outline is the theme's
// control boundary (StyleColours.control, at least 3:1 on every surface);
// disabled, the palette's Mid, the line role.
import QtQuick
import QtQuick.Controls.Fusion as F
import QtQuick.Controls.Fusion.impl as FI

F.TextArea {
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
