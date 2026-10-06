// K2: Fusion's combo box with its outline in the theme's boundary colour
// (control.palette.mid, the line role; see CMakeLists.txt). Fusion draws it
// in Fusion.outline(), the window colour darkened 140%, about 1.1:1 against
// the Dark and High contrast black panels. Keyboard focus (visualFocus) is
// the style's ring (FocusFrame: 2 wide, 3 beyond the control, in the theme's
// focus role, the text colour) instead of Fusion's accent-derived outline,
// apart from the accent of selection and the default button
// (visual-language.md: focus stays distinguishable from selection).
// Everything else is Fusion's.
// The popup's frame takes the boundary colour too.
import QtQuick
import QtQuick.Controls.Fusion as F
import QtQuick.Controls.Fusion.impl as FI

F.ComboBox {
    id: control
    background: FI.ButtonPanel {
        implicitWidth: 120
        implicitHeight: 24
        control: control
        visible: !control.flat || control.down
        // An editable box's field takes the accent's border when focused,
        // as a field does; Fusion's accent tint for keyboard focus is gone.
        highlighted: control.contentItem.activeFocus
        border.color: !control.enabled ? control.palette.mid
            : highlighted ? F.Fusion.highlightedOutline(control.palette) : control.palette.mid
        FocusFrame { control: control; shown: control.visualFocus }
    }
    Binding {
        target: control.popup ? control.popup.background : null
        property: "border.color"
        value: control.palette.mid
    }
}
