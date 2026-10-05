// K2: Fusion's radio button with its outline in the theme's boundary colour
// (control.palette.mid, the line role; see CMakeLists.txt). Fusion draws it
// in Fusion.outline(), the window colour darkened 140%, about 1.1:1 against
// the Dark and High contrast black panels. Keyboard focus (visualFocus) is
// the style's ring (FocusFrame: 2 wide, 3 beyond the whole control, in the theme's
// focus role, the text colour) instead of Fusion's accent-derived outline,
// apart from the accent of selection and the default button
// (visual-language.md: focus stays distinguishable from selection).
// Everything else is Fusion's.
import QtQuick
import QtQuick.Controls.Fusion as F
import QtQuick.Controls.Fusion.impl as FI

F.RadioButton {
    id: control
    indicator: FI.RadioIndicator {
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding) : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        control: control
        border.color: control.palette.mid
    }
    background: Item {
        FocusFrame { control: control; shown: control.visualFocus }
    }
}
