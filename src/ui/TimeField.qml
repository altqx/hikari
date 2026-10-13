// E4: legacy TimeCtrl (HikariSub/TimeCtrl.cpp), a time field that never holds
// text outside its form (application::typeIntoTimeField and its siblings,
// through TimeFieldMask). In a clock form (ASS, SRT, TMPlayer) a digit
// replaces the next one and skips the separators, a digit after ':' is at
// most 5, a digit over a selection zeroes it first, typing past the end
// rings the bell, Backspace zeroes digits and Delete does nothing; Ctrl+C
// copies the whole time. MPL2, MicroDVD and frames are numbers, edited as
// text but digits only. Paste takes only a whole value of the form, and any
// other key is refused with the bell. An edit the mask did not make (the
// platform's other editing keys, the context menu, a middle-click paste) is
// kept only when the text is still of the form; otherwise the field goes
// back with the bell.
//
// The mouse steps the time as TimeCtrl::OnMouseEvent did (TimeCtrl.cpp:286-387,
// application::wheelTimeField and dragTimeField): the wheel over a column
// steps that column's unit per notch, and the right button held and dragged
// steps it every 8 pixels up or down (up adding) or ten units every 10
// pixels right or left. The right button opens no context menu here, as in
// legacy, where its release ended the drag (the Menu key still opens it).
import QtQuick
import QtQuick.Controls
import Hikari.Ui

TextField {
    id: field
    // application::TimeFieldForm (LineEditorController.timeForm): 0 ASS,
    // 1 SRT, 2 TMPlayer, 3 a number.
    property int form: 0
    readonly property bool clock: form !== 3
    // Keys the mask leaves alone go here first (the Line editor's commit keys).
    property var otherKey: event => false
    // TimeCtrl's NUMBER_CHANGED: a value typed or pasted.
    signal typed()
    // Legacy wxBell; the shell has no audible bell to give.
    signal bell()

    // The number is a MicroDVD frame, which the wheel steps by one.
    property bool frameNumber: false
    property var drag: ({}) // the right button's drag (TimeFieldMask.dragBegin), {} when none

    property string held // the last text of the form, for an edit to go back to
    selectByMouse: true
    maximumLength: clock ? 32767 : 9 // application::kTimeFieldDigits
    validator: RegularExpressionValidator { regularExpression: field.clock ? /.*/ : /[0-9]*/ }

    function take(edit) {
        if (edit.bell)
            bell()
        if (text !== edit.text)
            text = edit.text
        select(edit.selectionStart, edit.selectionEnd)
        return edit.changed
    }
    // A stepped time: the caret stays (HikariTextCtrl::SetValue's newSel
    // false) and it applies as typing does (NUMBER_CHANGED).
    function step(next) {
        if (next.length === 0)
            return
        const from = selectionStart, to = selectionEnd
        text = next
        select(Math.min(from, text.length), Math.min(to, text.length))
        typed()
    }
    MouseArea {
        objectName: field.objectName + "Mouse"
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onPressed: mouse => {
            const column = field.positionAt(mouse.x, mouse.y, TextInput.CursorOnCharacter)
            field.forceActiveFocus(Qt.MouseFocusReason)
            field.select(column, column)
            field.drag = TimeFieldMask.dragBegin(field.form, field.text, column, mouse.x, mouse.y)
        }
        onPositionChanged: mouse => {
            if (!field.drag.step)
                return
            const moved = TimeFieldMask.dragMove(field.form, field.drag, mouse.x, mouse.y)
            field.drag = moved.drag
            field.step(moved.text)
        }
        onReleased: field.drag = ({})
        onCanceled: field.drag = ({})
        onWheel: wheel => {
            // GetWheelRotation() / GetWheelDelta(): whole notches only.
            const notches = Math.trunc(wheel.angleDelta.y / 120)
            field.step(TimeFieldMask.wheel(field.form, field.frameNumber, field.text,
                                           field.positionAt(wheel.x, wheel.y, TextInput.CursorOnCharacter), notches))
            wheel.accepted = true
        }
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
            // TimeCtrl's Ctrl+C (:108-112). Its Cut fell through to the text
            // control's and removed digits; here it copies as Ctrl+C does.
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
            event.accepted = true // :138-143: nothing in a clock form
            return
        }
        // A character, as HikariTextCtrl::OnCharPress takes one: no Ctrl or
        // Alt, or both (AltGr).
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
