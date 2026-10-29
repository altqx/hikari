// Fusion's outline (the window colour darkened) all but vanishes on black panels.
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
        FocusFrame { control: control; shown: control.visualFocus; offset: -2 }
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
