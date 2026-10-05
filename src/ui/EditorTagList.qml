// E6: a Line editor text field's tag list: the keys it takes, the characters
// typed into the field (TextEditor::OnCharPress) and the popup. Legacy
// TextEditor at 20d647c4: its own accelerators close an open list
// (OnAccelerator, DialogueTextEditor.cpp:563-567) except Up and Down, which
// move the selection (722-750), and Return, which puts the selected tag
// (802-808); Escape, Home and End close it too (OnKeyPress, 528-533);
// Page Up, Page Down and Insert leave it (525-527). Typed characters open or
// narrow it; an input method's committed text counts as typed, one
// character at a time, as WM_CHAR delivers it, and nothing is intercepted
// while a composition is open. A mouse press in the field or the field
// losing focus closes it (OnMouseEvent, OnKillFocus).
import QtQuick
import QtQuick.Controls
import Hikari.Ui

Item {
    id: tags
    required property TextArea field
    required property TagListController controller
    // The list works on the raw text only.
    enabled: true

    property string typedKey: ""
    property string composedFrom: ""
    property int composedAt: 0

    function put() {
        const result = controller.put(field.text, field.cursorPosition)
        if (result.text === undefined)
            return false
        field.text = result.text
        field.cursorPosition = result.caret
        return true
    }

    function takesKey(event) {
        if (!controller.open || field.inputMethodComposing)
            return false
        const mods = event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)
        return mods === 0 && (event.key === Qt.Key_Up || event.key === Qt.Key_Down || event.key === Qt.Key_Return
                              || event.key === Qt.Key_Escape)
    }

    // The field's key press, before its own handling: true when the list took it.
    function key(event) {
        if (field.inputMethodComposing)
            return false
        const mods = event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)
        const k = event.key
        if (controller.open) {
            if ((k === Qt.Key_Up || k === Qt.Key_Down) && mods === 0) {
                controller.move(k === Qt.Key_Up ? -1 : 1)
                return true
            }
            if (k === Qt.Key_Return && mods === 0) {
                tags.put()
                return true // the key is the list's even when nothing is put
            }
            if (k === Qt.Key_Escape) {
                controller.close()
                return true // OnKeyPress keeps Escape (no Skip)
            }
            const ctrl = Qt.ControlModifier, shift = Qt.ShiftModifier
            const closes = k === Qt.Key_Home || k === Qt.Key_End || k === Qt.Key_Menu
                || ((k === Qt.Key_Delete || k === Qt.Key_Backspace) && (mods === 0 || mods === ctrl))
                || ((k === Qt.Key_Left || k === Qt.Key_Right)
                    && (mods === 0 || mods === ctrl || mods === shift || mods === (ctrl | shift)))
                || ((k === Qt.Key_Up || k === Qt.Key_Down) && mods === shift)
                || ((k === Qt.Key_A || k === Qt.Key_V || k === Qt.Key_C || k === Qt.Key_X) && mods === ctrl)
            if (closes)
                controller.close()
        }
        // A character for OnCharPress: no modifier but Shift, or Ctrl+Alt (AltGr).
        const text = event.text
        const plain = (mods & ~Qt.ShiftModifier) === 0 || (mods & (Qt.ControlModifier | Qt.AltModifier))
                      === (Qt.ControlModifier | Qt.AltModifier)
        if (text.length === 1 && text.charCodeAt(0) >= 0x20 && text.charCodeAt(0) !== 0x7f && plain) {
            tags.typedKey = text
            Qt.callLater(tags.afterTyped)
        }
        return false
    }

    function afterTyped() {
        const typed = tags.typedKey
        tags.typedKey = ""
        if (!tags.enabled || typed.length !== 1)
            return
        controller.typed(field.text, field.cursorPosition, typed)
    }

    function afterComposition() {
        const before = tags.composedFrom
        const at = tags.composedAt
        const now = field.text
        if (!tags.enabled || now.length <= before.length)
            return
        const inserted = now.length - before.length
        if (now.substring(0, at) !== before.substring(0, at) || now.substring(at + inserted) !== before.substring(at))
            return
        let text = before
        for (let i = 0; i < inserted; ++i) {
            const ch = now.charAt(at + i)
            text = text.substring(0, at + i) + ch + text.substring(at + i)
            controller.typed(text, at + i + 1, ch)
        }
    }

    onEnabledChanged: if (!enabled) controller.close()

    Connections {
        target: tags.field
        function onActiveFocusChanged() {
            if (!tags.field.activeFocus && !popup.menuOpen)
                tags.controller.close()
        }
        function onPressed() { tags.controller.close() }
        function onInputMethodComposingChanged() {
            if (tags.field.inputMethodComposing) {
                tags.composedFrom = tags.field.text
                tags.composedAt = tags.field.cursorPosition
            } else {
                Qt.callLater(tags.afterComposition)
            }
        }
    }

    TagListPopup {
        id: popup
        controller: tags.controller
        field: tags.field
        onChosen: tags.put()
    }
}
