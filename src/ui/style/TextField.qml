// K2: Fusion's text field with its outline in the theme's boundary colour
// (control.palette.mid, the line role; see CMakeLists.txt). Fusion draws it
// in Fusion.outline(), the window colour darkened 140%, about 1.1:1 against
// the Dark and High contrast black panels. Everything else is Fusion's.
import QtQuick
import QtQuick.Controls.Fusion as F
import QtQuick.Controls.Fusion.impl as FI

F.TextField {
    id: control
    background: FI.TextFieldBackground {
        control: control
        border.color: control.activeFocus ? F.Fusion.highlightedOutline(control.palette) : control.palette.mid
    }
}
