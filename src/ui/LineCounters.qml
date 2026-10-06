// E4: the Line editor's counters and its Times/Frames switch (legacy
// EditBox's BoxSizer5, EditBox.cpp:216-237): "Wraps: ..." and "Characters per
// second: ..." (EditBox::UpdateChars, in the warning colour over 43
// characters a wrap or three wraps, and over 15 characters per second; empty
// for a comment), then Time and Frames (EDITBOX_TIMES_TO_FRAMES_SWITCH),
// enabled once a video was opened (Notebook::LoadVideo).
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
    Item { Layout.fillWidth: true }
    ButtonGroup { id: timeDisplay }
    RadioButton {
        objectName: "showTimes"
        text: qsTr("Time")
        ButtonGroup.group: timeDisplay
        checked: !counters.editor.showFrames
        enabled: counters.editor.framesAvailable
        focusPolicy: Qt.TabFocus
        Accessible.name: qsTr("Show times")
        onToggled: if (checked) counters.editor.showFrames = false
    }
    RadioButton {
        objectName: "showFrames"
        text: qsTr("Frames")
        ButtonGroup.group: timeDisplay
        checked: counters.editor.showFrames
        enabled: counters.editor.framesAvailable
        focusPolicy: Qt.TabFocus
        Accessible.name: qsTr("Show frames")
        onToggled: if (checked) counters.editor.showFrames = true
    }
}
