// K2: Fusion's spin box with its outline and the separator before its
// buttons in the theme's boundary colour (control.palette.mid, the line role;
// see CMakeLists.txt). Fusion draws them in Fusion.outline(), the window
// colour darkened 140%, about 1.1:1 against the Dark and High contrast black
// panels. Focused, its border is the accent's outline, as a field's; with
// keyboard focus (visualFocus) the style's ring (FocusFrame: 2 wide, 3
// beyond it, in the theme's focus role, the text colour) is drawn beside it,
// and its buttons keep their colour (Fusion tints them with the accent).
// Everything else is Fusion's.
import QtQuick
import QtQuick.Controls.Fusion as F

F.SpinBox {
    id: control
    background: Rectangle {
        implicitWidth: 120
        implicitHeight: 24

        radius: 2
        color: control.palette.base
        border.color: control.activeFocus ? F.Fusion.highlightedOutline(control.palette) : control.palette.mid

        Rectangle {
            x: 2
            y: 1
            width: parent.width - 4
            height: 1
            color: F.Fusion.topShadow
        }

        Rectangle {
            x: control.mirrored ? 1 : parent.width - width - 1
            y: 1
            width: Math.max(control.up.indicator ? control.up.indicator.width : 0,
                            control.down.indicator ? control.down.indicator.width : 0) + 1
            height: parent.height - 2

            radius: 2
            gradient: Gradient {
                GradientStop {
                    position: 0
                    color: F.Fusion.gradientStart(F.Fusion.buttonColor(control.palette, false, false, control.up.hovered || control.down.hovered))
                }
                GradientStop {
                    position: 1
                    color: F.Fusion.gradientStop(F.Fusion.buttonColor(control.palette, false, false, control.up.hovered || control.down.hovered))
                }
            }

            Rectangle {
                x: control.mirrored ? parent.width - 1 : 0
                height: parent.height
                width: 1
                color: control.palette.mid
            }
        }

        Rectangle {
            x: 1; y: 1
            width: parent.width - 2
            height: parent.height - 2
            color: "transparent"
            border.color: Qt.alpha(F.Fusion.highlightedOutline(control.palette), 40 / 255)
            visible: control.activeFocus
            radius: 1.7
        }

        FocusFrame { control: control; shown: control.visualFocus }
    }
}
