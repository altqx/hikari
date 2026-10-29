import QtQuick
import QtQuick.Controls
import Hikari.Ui

TextField {
    id: field
    // MUST KILL: form is a magic int (0 ASS, 1 SRT, 2 TMPlayer, 3 a number)
    property int form: 0
    readonly property bool clock: form !== 3
    property var otherKey: event => false
    signal typed()
    signal bell()

    // MUST KILL: mouseForm is a magic int (0 ASS, 1 SRT, 2 TMPlayer, 3 MicroDVD, 4 MPL2)
    property int mouseForm: 0
    property bool frames: false
    property bool modified: false
    property var drag: ({})
    property bool rightHeld: false

    property string held
    selectByMouse: true
    maximumLength: clock ? 32767 : 9
    validator: RegularExpressionValidator { regularExpression: field.clock ? /.*/ : /[0-9]*/ }

    function take(edit) {
        if (edit.bell)
            bell()
        if (text !== edit.text)
            text = edit.text
        select(edit.selectionStart, edit.selectionEnd)
        return edit.changed
    }
    function step(next) {
        if (next.length === 0)
            return
        const from = selectionStart, to = selectionEnd
        text = next
        select(Math.min(from, text.length), Math.min(to, text.length))
        modified = true
        typed()
    }
    MouseArea {
        objectName: field.objectName + "Mouse"
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onPressed: mouse => {
            field.rightHeld = true
            const column = field.positionAt(mouse.x, mouse.y, TextInput.CursorOnCharacter)
            field.forceActiveFocus(Qt.MouseFocusReason)
            field.select(column, column)
            const at = mapToItem(null, mouse.x, mouse.y)
            field.drag = TimeFieldMask.dragBegin(field.mouseForm, field.frames, field.text, column, at.x, at.y)
        }
        onPositionChanged: mouse => {
            if (!field.drag.step)
                return
            const at = mapToItem(null, mouse.x, mouse.y)
            const moved = TimeFieldMask.dragMove(field.mouseForm, field.frames, field.drag, at.x, at.y)
            field.drag = moved.drag
            field.step(moved.text)
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
            // angleDelta is in eighths of a degree: 120 per notch.
            const notches = Math.trunc(wheel.angleDelta.y / 120)
            field.step(TimeFieldMask.wheel(field.mouseForm, field.frames, field.text,
                                           field.positionAt(wheel.x, wheel.y, TextInput.CursorOnCharacter), notches))
            wheel.accepted = true
        }
    }
    // Qt makes a context menu event from the right press (on Windows the release); an empty ContextMenu swallows it.
    Item {
        anchors.fill: parent
        visible: field.rightHeld
        ContextMenu.onRequested: {}
    }
    onTextChanged: if (TimeFieldMask.holds(form, text)) held = text
    onTextEdited: {
        if (TimeFieldMask.holds(form, text)) {
            typed()
            return
        }
        const caret = cursorPosition
        text = held
        cursorPosition = Math.min(caret, text.length)
        bell()
    }
    Keys.onPressed: event => {
        if (event.matches(StandardKey.Paste)) {
            if (take(TimeFieldMask.paste(form, text, selectionStart, selectionEnd)))
                typed()
            event.accepted = true
            return
        }
        if (clock && (event.matches(StandardKey.Copy) || event.matches(StandardKey.Cut))) {
            selectAll()
            TimeFieldMask.copy(text)
            event.accepted = true
            return
        }
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        if (clock && event.key === Qt.Key_Backspace && !ctrl) {
            take(TimeFieldMask.backspace(text, selectionStart, selectionEnd))
            event.accepted = true
            return
        }
        if (clock && event.key === Qt.Key_Delete && event.modifiers === Qt.NoModifier) {
            event.accepted = true
            return
        }
        // AltGr arrives as Ctrl+Alt.
        const alt = (event.modifiers & Qt.AltModifier) !== 0
        if (event.text.length === 1 && event.text >= " " && event.text !== "\u007f" && ctrl === alt) {
            if (take(TimeFieldMask.type(form, text, selectionStart, selectionEnd, event.text)))
                typed()
            event.accepted = true
            return
        }
        event.accepted = otherKey(event)
    }
}
