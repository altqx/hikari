// K2: Fusion's tool button with its outline in the theme's boundary colour
// (control.palette.mid, the line role; see CMakeLists.txt). Fusion draws it
// in Fusion.outline(), the window colour darkened 140%, about 1.1:1 against
// the Dark and High contrast black panels. Keyboard focus (visualFocus) is
// the style's ring (FocusFrame: 2 wide, 3 beyond the control, in the theme's
// focus role, the text colour) instead of Fusion's accent-derived outline,
// apart from the accent of selection and the default button
// (visual-language.md: focus stays distinguishable from selection).
// Everything else is Fusion's.
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
            : highlighted ? F.Fusion.highlightedOutline(control.palette) : control.palette.mid
        FocusFrame { control: control; shown: control.visualFocus }
    }
}
