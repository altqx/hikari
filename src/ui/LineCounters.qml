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
        property bool error
        property string description
        readonly property int split: text.lastIndexOf("/")
        readonly property color tone: error ? Theme.danger : Theme.warning
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
            color: counter.tone
            accentColor: counter.tone
        }
        Label {
            text: counter.name
            color: Theme.muted
            Accessible.ignored: true
        }
        Label {
            objectName: counter.objectName + "Value"
            text: counter.text.substring(0, counter.split)
            color: counter.warning ? counter.tone : palette.windowText
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
    }

    Counter {
        objectName: "charsCounter"
        text: counters.editor.charsText
        name: qsTranslate("HikariSub.Grid", "Wraps")
        warning: counters.editor.charsWarning
        description: qsTr("Number of characters in each line. Keep it at %1 or under, in at most %n line(s).", "", counters.editor.maxLines).arg(counters.editor.lineLength)
    }
    Counter {
        objectName: "cpsCounter"
        text: counters.editor.cpsText
        name: qsTranslate("HikariSub.Grid", "CPS")
        warning: counters.editor.cpsWarning
        error: counters.editor.cpsError
        description: qsTr("Characters per second. Keep it at %1 or under.").arg(counters.editor.cpsLimit)
    }
}
