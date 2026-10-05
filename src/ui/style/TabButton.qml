// K2: Fusion's tab button with its outline in the theme's boundary colour
// (control.palette.mid, the line role; see CMakeLists.txt). Fusion draws it
// in Fusion.outline(), the window colour darkened 140%, about 1.1:1 against
// the Dark and High contrast black panels. Keyboard focus (visualFocus) is
// the style's ring (FocusFrame: 2 wide in the theme's focus role, the text
// colour) just inside the tab, where the tab bar does not clip it; the
// current tab is told by its raised face. Everything else is Fusion's.
import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls.Fusion as F

F.TabButton {
    id: control
    background: Rectangle {
        y: control.checked || control.TabBar.position !== T.TabBar.Header ? 0 : 2
        implicitHeight: 21
        height: control.height - (control.checked ? 0 : 2)
        border.color: control.palette.mid
        FocusFrame { control: control; shown: control.visualFocus; offset: -3 }
        gradient: Gradient {
            GradientStop {
                position: 0
                color: control.checked ? Qt.lighter(F.Fusion.tabFrameColor(control.palette), 1.04)
                                       : Qt.darker(F.Fusion.tabFrameColor(control.palette), 1.08)
            }
            GradientStop {
                position: control.checked ? 0 : 0.85
                color: control.checked ? Qt.lighter(F.Fusion.tabFrameColor(control.palette), 1.04)
                                       : Qt.darker(F.Fusion.tabFrameColor(control.palette), 1.08)
            }
            GradientStop {
                position: 1
                color: control.checked ? F.Fusion.tabFrameColor(control.palette)
                                       : Qt.darker(F.Fusion.tabFrameColor(control.palette), 1.16)
            }
        }
    }
}
