import QtQuick
import QtQuick.Controls.Fusion as F

F.SpinBox {
    id: control
    textFromValue: (value, locale) => String(value)
    valueFromText: (text, locale) => {
        const n = parseInt(text.replace(/[^0-9-]/g, ""), 10)
        return isNaN(n) ? control.value : n
    }
    // Fusion's TextInput without the text-editing ContextMenu Qt 6.12 gives it.
    contentItem: TextInput {
        z: 2
        text: control.displayText
        font: control.font
        color: control.palette.text
        selectionColor: control.palette.highlight
        selectedTextColor: control.palette.highlightedText
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: control.inputMethodHints
        clip: width < implicitWidth
    }
    background: Rectangle {
        implicitWidth: 120
        implicitHeight: 24

        radius: 2
        color: control.palette.base
        border.color: control.activeFocus ? F.Fusion.highlightedOutline(control.palette)
            : (control.enabled && StyleColours.themed ? StyleColours.control : control.palette.mid)

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
