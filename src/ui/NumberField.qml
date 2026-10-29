import QtQuick
import QtQuick.Controls
import Hikari.Ui

TextField {
    id: field
    property real from: 0
    property real to: 0
    property real numValue: 0
    property bool modified: false
    property var drag: ({})
    property bool rightHeld: false
    signal stepped()
    selectByMouse: true

    function given(text) {
        const v = NumberFieldMouse.value(text)
        numValue = isNaN(v) ? 0 : Math.min(Math.max(v, from), to)
        modified = false
    }
    function step(next) {
        const from = selectionStart, to = selectionEnd
        text = next
        select(Math.min(from, text.length), Math.min(to, text.length))
        modified = true
        stepped()
    }
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
            const at = mapToItem(null, mouse.x, mouse.y)
            field.drag = {value: field.numValue, x: at.x, y: at.y}
        }
        onPositionChanged: mouse => {
            if (field.drag.value === undefined)
                return
            const at = mapToItem(null, mouse.x, mouse.y)
            const moved = NumberFieldMouse.dragMove(field.drag, at.x, at.y, field.from, field.to)
            field.drag = moved.drag
            if (moved.text.length > 0) {
                field.numValue = moved.drag.value
                field.step(moved.text)
            }
        }
        onReleased: {
            field.drag = ({})
            // Qt may make its context menu event during the release's delivery.
            Qt.callLater(() => field.rightHeld = false)
        }
        onCanceled: {
            field.drag = ({})
            field.rightHeld = false
        }
        onWheel: wheel => {
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
    Item {
        anchors.fill: parent
        visible: field.rightHeld
        ContextMenu.onRequested: {}
    }
}
