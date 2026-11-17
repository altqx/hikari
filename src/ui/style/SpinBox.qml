import QtQuick
import QtQuick.Controls.Fusion as F

F.SpinBox {
    id: control
    textFromValue: (value, locale) => String(value)
    valueFromText: (text, locale) => {
        const n = parseInt(text.replace(/[^0-9-]/g, ""), 10)
        return isNaN(n) ? control.value : n
    }
    // Qt's SpinBox takes no focus from a click on a step arrow, so typing went to the field focused before (#801).
    focusPolicy: editable ? Qt.ClickFocus : Qt.NoFocus
    leftPadding: control.mirrored ? 20 : 8
    rightPadding: control.mirrored ? 8 : 20
    // Fusion's TextInput without the text-editing ContextMenu Qt 6.12 gives it.
    contentItem: TextInput {
        z: 2
        text: control.displayText
        font: control.font
        color: StyleColours.themed ? (control.enabled ? StyleColours.text : StyleColours.disabled) : control.palette.text
        selectionColor: control.palette.highlight
        selectedTextColor: control.palette.highlightedText
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: control.inputMethodHints
        clip: width < implicitWidth
    }
    up.indicator: Rectangle {
        objectName: "spinUp"
        x: control.mirrored ? 4 : control.width - width - 4
        y: control.height / 2 - height
        implicitWidth: 12
        implicitHeight: 10
        radius: 2
        visible: control.enabled
        color: StyleColours.themed && control.up.pressed ? StyleColours.pressed
            : StyleColours.themed && control.up.hovered ? StyleColours.hover : "transparent"
        Chevron {
            anchors.centerIn: parent
            width: 8
            height: 4
            up: true
            color: !StyleColours.themed ? control.palette.buttonText
                : control.up.hovered || control.up.pressed ? StyleColours.text : StyleColours.muted
        }
    }
    down.indicator: Rectangle {
        objectName: "spinDown"
        x: control.mirrored ? 4 : control.width - width - 4
        y: control.height / 2
        implicitWidth: 12
        implicitHeight: 10
        radius: 2
        visible: control.enabled
        color: StyleColours.themed && control.down.pressed ? StyleColours.pressed
            : StyleColours.themed && control.down.hovered ? StyleColours.hover : "transparent"
        Chevron {
            anchors.centerIn: parent
            width: 8
            height: 4
            color: !StyleColours.themed ? control.palette.buttonText
                : control.down.hovered || control.down.pressed ? StyleColours.text : StyleColours.muted
        }
    }
    background: FieldBackground {
        control: control
        keyboardFocus: control.visualFocus
    }
}
