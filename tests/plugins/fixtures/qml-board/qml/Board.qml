import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Plugin
import "parts"

ColumnLayout {
    id: board
    required property var api
    objectName: "board"
    property string version: "1"
    property string lines
    property string echo
    property string failed
    property string refused
    readonly property color accent: api.theme.accent
    readonly property string language: api.locale
    readonly property int count: api.settings.count ?? -1
    Layout.minimumWidth: 220
    Layout.minimumHeight: 100
    spacing: 8

    function countLines() {
        board.api.call("lines.list", {}).then(result => board.lines = String(result.lines.length))
    }
    function callNothing() {
        board.api.call("no.such.operation").catch(error => board.failed = error.code)
    }
    function say(text) {
        board.api.send({ text: text })
    }
    function keep(value) {
        try {
            board.api.setSetting("count", value)
        } catch (error) {
            board.refused = error.message
        }
    }

    Connections {
        target: board.api
        function onReceived(data) {
            board.echo = data.echo
        }
    }

    Badge {
        objectName: "badge"
        text: qsTr("Lines: %1").arg(board.lines || "?")
        Layout.fillWidth: true
    }
    RowLayout {
        Icon {
            name: "plugins"
            size: 16
        }
        IconButton {
            name: "restart"
            text: "Count lines"
            onClicked: board.countLines()
        }
        Label {
            text: board.echo
        }
    }
    Item {
        Layout.fillHeight: true
    }
}
