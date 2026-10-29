pragma Translator: "HikariSub.Fonts"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

RowLayout {
    id: note
    property var resolution: ({})
    readonly property string kind: resolution && resolution.kind ? resolution.kind : ""
    readonly property bool warning: kind === "substituted" || kind === "fallback" || kind === "missing"
    readonly property string text: kind === "requested" ? qsTr("Drawn by the subtitle renderer with %1 (%2).").arg(resolution.family).arg(resolution.file)
        : kind === "substituted" ? qsTr("Not installed under this name: the subtitle renderer substitutes %1 (%2).").arg(resolution.family).arg(resolution.file)
        : kind === "fallback" ? qsTr("Not installed: the subtitle renderer falls back to %1 (%2).").arg(resolution.family).arg(resolution.file)
        : kind === "missing" ? qsTr("No font answers this name.")
        : ""
    spacing: 6
    visible: text.length > 0
    Icon {
        visible: note.warning
        iconRole: "notice-warning"
        size: 14
        color: Theme.warning
    }
    Label {
        textFormat: Text.PlainText
        objectName: "fontResolutionText"
        text: note.text
        color: note.warning ? Theme.warning : Theme.muted
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
}
