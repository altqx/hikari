// K2: Fusion's combo box with its outline in the theme's boundary colour
// (control.palette.mid, the line role; see CMakeLists.txt). Fusion draws it
// in Fusion.outline(), the window colour darkened 140%, about 1.1:1 against
// the Dark and High contrast black panels. Keyboard focus (visualFocus) is
// outlined in the theme's focus role (StyleColours.focus), apart from the
// accent of selection and the default button (visual-language.md: focus
// stays distinguishable from selection). Everything else is Fusion's.
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
        highlighted: control.visualFocus || control.contentItem.activeFocus
        border.color: !control.enabled ? control.palette.mid
            : control.visualFocus ? (StyleColours.themed ? StyleColours.focus : F.Fusion.highlightedOutline(control.palette))
            : highlighted ? F.Fusion.highlightedOutline(control.palette) : control.palette.mid
    }
    Binding {
        target: control.popup ? control.popup.background : null
        property: "border.color"
        value: control.palette.mid
    }
}
