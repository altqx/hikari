// E4: the Line editor's counters (legacy EditBox's BoxSizer5,
// EditBox.cpp:216-237): each wrap's characters and the characters per second
// (EditBox::UpdateChars, warned over 43 characters a wrap or three wraps,
// and over 15 characters per second; empty for a comment).
// LE2: legacy's Time and Frames radios (EDITBOX_TIMES_TO_FRAMES_SWITCH) left
// this row for Settings > Editor and the Video panel's "⋯" menu.
// LE1 (#233): at the tag strip's end, each as its name (the Grid's column
// names, Wraps and CPS) and the numbers, the limit in the secondary text
// colour. Over a limit the numbers take the warning colour and a warning
// mark, so the colour is not the only sign (LE1-counters).
pragma Translator: "HikariSub.Editor"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

RowLayout {
    id: counters
    required property LineEditorController editor
    spacing: 12

    component Counter: RowLayout {
        id: counter
        // "5/10/43": the counts, then the limit after the last slash.
        property string text
        property string name
        property bool warning
        property string description
        readonly property int split: text.lastIndexOf("/")
        visible: text.length > 0
        spacing: 4
        Accessible.role: Accessible.StaticText
        Accessible.name: name + " " + text
        Accessible.description: description
        Icon {
            objectName: counter.objectName + "Warning"
            visible: counter.warning
            iconRole: "notice-warning"
            size: 12
            color: Theme.warning
            accentColor: Theme.warning
        }
        // The counter reads as one text (its name and numbers).
        Label {
            text: counter.name
            color: Theme.muted
            Accessible.ignored: true
        }
        Label {
            objectName: counter.objectName + "Value"
            text: counter.text.substring(0, counter.split)
            color: counter.warning ? Theme.warning : palette.windowText
            font.bold: counter.warning
            font.features: { "tnum": 1 }
            Accessible.ignored: true
        }
        Label {
            text: counter.text.substring(counter.split)
            color: Theme.muted
            font.features: { "tnum": 1 }
            Layout.leftMargin: -4
            Accessible.ignored: true
        }
        HoverHandler { id: hover }
        ToolTip.visible: hover.hovered
        ToolTip.text: description
        ToolTip.delay: 500
    }

    Counter {
        objectName: "charsCounter"
        text: counters.editor.charsText
        name: qsTranslate("HikariSub.Grid", "Wraps")
        // Legacy WINDOW_WARNING_ELEMENTS: the theme layer's warning role.
        warning: counters.editor.charsWarning
        description: qsTr("Number of characters in each line.\nNo more than 43 characters per line (maximum 2 lines).")
    }
    Counter {
        objectName: "cpsCounter"
        text: counters.editor.cpsText
        name: qsTranslate("HikariSub.Grid", "CPS")
        warning: counters.editor.cpsWarning
        description: qsTr("Characters per second.\nShould not exceed 15 characters per second")
    }
}
