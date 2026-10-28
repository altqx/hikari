// E4: the Line editor's counters (legacy EditBox's BoxSizer5,
// EditBox.cpp:216-237): "Wraps: ..." and "Characters per second: ..."
// (EditBox::UpdateChars, in the warning colour over 43 characters a wrap or
// three wraps, and over 15 characters per second; empty for a comment).
// LE2: legacy's Time and Frames radios (EDITBOX_TIMES_TO_FRAMES_SWITCH) left
// this row for Settings > Editor and the Video panel's "⋯" menu.
pragma Translator: "HikariSub.Editor"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

RowLayout {
    id: counters
    required property LineEditorController editor
    spacing: 6

    Label {
        objectName: "charsCounter"
        text: counters.editor.charsText
        // Legacy WINDOW_WARNING_ELEMENTS: the theme layer's warning role.
        color: counters.editor.charsWarning ? Theme.warning : palette.windowText
        Accessible.role: Accessible.StaticText
        Accessible.name: text
        Accessible.description: qsTr("Number of characters in each line.\nNo more than 43 characters per line (maximum 2 lines).")
        HoverHandler { id: charsHover }
        ToolTip.visible: charsHover.hovered
        ToolTip.text: Accessible.description
        ToolTip.delay: 500
    }
    Label {
        objectName: "cpsCounter"
        text: counters.editor.cpsText
        color: counters.editor.cpsWarning ? Theme.warning : palette.windowText
        Accessible.role: Accessible.StaticText
        Accessible.name: text
        Accessible.description: qsTr("Characters per second.\nShould not exceed 15 characters per second")
        HoverHandler { id: cpsHover }
        ToolTip.visible: cpsHover.hovered
        ToolTip.text: Accessible.description
        ToolTip.delay: 500
    }
}
