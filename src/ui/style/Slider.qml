// K2: Fusion's slider with its groove's and handle's outline in the theme's
// boundary colour (control.palette.mid, the line role; see CMakeLists.txt).
// Fusion draws them in Fusion.outline(), the window colour darkened 140%,
// about 1.1:1 against the Dark and High contrast black panels. Keyboard
// focus (visualFocus) is the style's ring around the handle (FocusFrame: 2
// wide, 3 beyond it, in the theme's focus role, the text colour) instead of
// Fusion's accent-tinted handle, apart from the accent of the groove's
// filled part. Everything else is Fusion's (the handle is FI.SliderHandle's
// drawing, its outline on itself).
import QtQuick
import QtQuick.Controls.Fusion as F
import QtQuick.Controls.Fusion.impl as FI

F.Slider {
    id: control
    handle: Rectangle {
        x: control.leftPadding + Math.round(control.horizontal ? control.visualPosition * (control.availableWidth - width) : (control.availableWidth - width) / 2)
        y: control.topPadding + Math.round(control.horizontal ? (control.availableHeight - height) / 2 : control.visualPosition * (control.availableHeight - height))
        implicitWidth: 13
        implicitHeight: 13
        rotation: control.vertical ? -90 : 0
        radius: 2
        border.color: control.palette.mid
        gradient: Gradient {
            GradientStop {
                position: 0
                color: F.Fusion.gradientStart(F.Fusion.buttonColor(control.palette, false, control.pressed,
                    control.enabled && control.hovered))
            }
            GradientStop {
                position: 1
                color: F.Fusion.gradientStop(F.Fusion.buttonColor(control.palette, false, control.pressed,
                    control.enabled && control.hovered))
            }
        }

        Rectangle {
            x: 1; y: 1
            width: parent.width - 2
            height: parent.height - 2
            border.color: F.Fusion.innerContrastLine
            color: "transparent"
            radius: 2
        }

        FocusFrame { control: control; shown: control.visualFocus }
    }
    background: FI.SliderGroove {
        control: control
        progress: control.position
        visualProgress: control.visualPosition
        border.color: control.palette.mid
    }
}
