pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Rectangle {
    id: chip
    property string tier: "mine"
    property bool onAccent: false
    readonly property bool project: tier === "project"
    readonly property string text: project ? qsTr("Project") : qsTr("Mine")
    implicitWidth: row.implicitWidth + 12
    implicitHeight: 20
    radius: 10
    color: "transparent"
    border.color: !enabled ? Theme.disabled : onAccent ? Theme.onAccent : project ? Theme.accent : Theme.line
    Accessible.role: Accessible.StaticText
    Accessible.name: text
    RowLayout {
        id: row
        anchors.fill: parent
        anchors.leftMargin: 5
        anchors.rightMargin: 7
        spacing: 4
        Icon {
            iconRole: chip.project ? "project" : "mine"
            size: 14
            enabled: chip.enabled
            color: chip.onAccent && enabled ? Theme.onAccent : enabled ? IconTheme.normal : IconTheme.disabled
            accentColor: chip.onAccent && enabled ? Theme.onAccent : enabled ? IconTheme.accent : IconTheme.disabled
        }
        Label {
            text: chip.text
            font.pixelSize: 11
            color: !chip.enabled ? Theme.disabled : chip.onAccent ? Theme.onAccent : chip.project ? Theme.accent : Theme.text
        }
    }
}
