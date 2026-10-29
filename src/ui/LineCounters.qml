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
        // MUST KILL: text packs the counts and the limit into one slash string ("5/10/43")
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
