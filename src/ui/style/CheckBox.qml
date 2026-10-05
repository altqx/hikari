// K2: Fusion's check box with its outline in the theme's boundary colour
// (control.palette.mid, the line role; see CMakeLists.txt). Fusion draws it
// in Fusion.outline(), the window colour darkened 140%, about 1.1:1 against
// the Dark and High contrast black panels. Keyboard focus (visualFocus) is
// outlined in the theme's focus role (StyleColours.focus), apart from the
// accent of selection and the default button (visual-language.md: focus
// stays distinguishable from selection). Everything else is Fusion's.
import QtQuick
import QtQuick.Controls.Fusion as F
import QtQuick.Controls.Fusion.impl as FI

F.CheckBox {
    id: control
    indicator: FI.CheckIndicator {
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding) : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        baseLightness: control.enabled ? 1.25 : 1.0
        control: control
        border.color: control.visualFocus ? (StyleColours.themed ? StyleColours.focus : F.Fusion.highlightedOutline(control.palette)) : control.palette.mid
    }
}
