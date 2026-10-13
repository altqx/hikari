// E4: legacy NumCtrl (HikariSub/NumCtrl.cpp) for the Line editor's Layer
// and margins: a text field whose mouse steps its number as
// NumCtrl::OnMouseEvent did (NumCtrl.cpp:243-319, NumberFieldMouse over
// application::wheelNumberField and dragNumberField). The wheel steps the
// value by one a notch, with or without the focus; the right button held and
// dragged takes the focus and steps it by one every 8 pixels up or down (up
// adding) or by ten every 10 pixels right or left. Each step is clamped to
// [from, to] and shows the clamped value (E4-numctrl-clamp; legacy's wheel
// showed nothing past the range and kept counting). A step marks the field
// modified (HikariTextCtrl::SetValue's modif). The right button opens no
// context menu here, as in legacy, where its release ended the drag, on any
// platform's trigger (the Menu key still opens it).
import QtQuick
import QtQuick.Controls
import Hikari.Ui

TextField {
    id: field
    // NumCtrl's range (rfrom, rto).
    property real from: 0
    property real to: 0
    // NumCtrl's own `value`: what the field was given (SetInt, clamped), the
    // last number typed (which may be outside the range), and what the wheel
    // and the drag left.
    property real numValue: 0
    // Stepped by the mouse since the field was last applied or given a value.
    property bool modified: false
    property var drag: ({}) // the right button's drag, {} when none
    // From the right button's press to just after its release.
    property bool rightHeld: false
    // NUMBER_CHANGED from a step.
    signal stepped()
    selectByMouse: true

    // NumCtrl::SetInt: the value given, clamped to the range.
    function given(text) {
        const v = NumberFieldMouse.value(text)
        numValue = isNaN(v) ? 0 : Math.min(Math.max(v, from), to)
        modified = false
    }
    // A stepped value: the caret stays (SetValue's newSel false).
    function step(next) {
        const from = selectionStart, to = selectionEnd
        text = next
        select(Math.min(from, text.length), Math.min(to, text.length))
        modified = true
        stepped()
    }
    // OnNumWrite: a typed number becomes the value, whatever the range.
    onTextEdited: {
        const v = NumberFieldMouse.value(text)
        if (!isNaN(v))
            numValue = v
    }
    MouseArea {
        objectName: field.objectName + "Mouse"
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onPressed: mouse => {
            field.rightHeld = true
            field.forceActiveFocus(Qt.MouseFocusReason)
            field.drag = {value: field.numValue, x: mouse.x, y: mouse.y}
        }
        onPositionChanged: mouse => {
            if (field.drag.value === undefined)
                return
            const moved = NumberFieldMouse.dragMove(field.drag, mouse.x, mouse.y, field.from, field.to)
            field.drag = moved.drag
            if (moved.text.length > 0) {
                field.numValue = moved.drag.value
                field.step(moved.text)
            }
        }
        onReleased: {
            field.drag = ({})
            // After the release's own delivery, in which Qt may make its
            // context menu event.
            Qt.callLater(() => field.rightHeld = false)
        }
        onCanceled: {
            field.drag = ({})
            field.rightHeld = false
        }
        onWheel: wheel => {
            // GetWheelRotation() / GetWheelDelta(): whole notches only.
            const notches = Math.trunc(wheel.angleDelta.y / 120)
            if (notches !== 0) {
                const next = NumberFieldMouse.wheel(field.numValue, notches, field.from, field.to)
                field.numValue = next.value
                if (next.text.length > 0)
                    field.step(next.text)
            }
            wheel.accepted = true
        }
    }
    // The right button's context menu event stops here, as on TimeField.
    Item {
        anchors.fill: parent
        visible: field.rightHeld
        ContextMenu.onRequested: {}
    }
}
