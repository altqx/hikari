import QtQuick
import QtQuick.Controls
import Hikari.Ui

ShellMenuItem {
    id: item
    enum Kind { Undo, Redo, Cut, Copy, Paste, Delete, SelectAll }
    required property int kind
    required property Item editor
    readonly property bool editable: !!editor && !editor.readOnly
    readonly property bool selected: !!editor && editor.selectedText.length > 0

    objectName: ["textUndo", "textRedo", "textCut", "textCopy", "textPaste", "textDelete", "textSelectAll"][kind]
    text: [qsTranslate("HikariSub.TextMenu", "Undo"), qsTranslate("HikariSub.TextMenu", "Redo"),
           qsTranslate("HikariSub.TextMenu", "Cut"), qsTranslate("HikariSub.TextMenu", "Copy"),
           qsTranslate("HikariSub.TextMenu", "Paste"), qsTranslate("HikariSub.TextMenu", "Delete"),
           qsTranslate("HikariSub.TextMenu", "Select all")][kind]
    help: [qsTranslate("HikariSub.TextMenu", "Undoes the last change to the text"),
           qsTranslate("HikariSub.TextMenu", "Redoes the last change you undid"),
           qsTranslate("HikariSub.TextMenu", "Moves the selected text to the clipboard"),
           qsTranslate("HikariSub.TextMenu", "Copies the selected text to the clipboard"),
           qsTranslate("HikariSub.TextMenu", "Inserts the text from the clipboard"),
           qsTranslate("HikariSub.TextMenu", "Deletes the selected text"),
           qsTranslate("HikariSub.TextMenu", "Selects all the text in the field")][kind]
    iconRole: ["undo", "redo", "edit-cut", "edit-copy", "edit-paste", "clear", "select-all"][kind]
    standardKey: [StandardKey.Undo, StandardKey.Redo, StandardKey.Cut, StandardKey.Copy, StandardKey.Paste,
                  StandardKey.Delete, StandardKey.SelectAll][kind]
    enabled: !!editor && [editable && editor.canUndo, editable && editor.canRedo, editable && selected, selected,
                          editable && editor.canPaste, editable && selected, editor.length > 0][kind]
    onTriggered: ShellMenus.pressKeys(editor, standardKey)
}
