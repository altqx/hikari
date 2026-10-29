pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Pane {
    id: bar
    property string severity: "info"
    property string iconRole: ""
    property string text: ""
    property bool closable: true
    default property alias actions: actionRow.data
    readonly property alias label: textLabel
    readonly property alias closeButton: close
    signal closed()
    padding: 6
    leftPadding: 10
    background: Rectangle {
        color: Theme.raised
        Rectangle { width: parent.width; height: 1; color: Theme.line }
        Rectangle { width: parent.width; height: 1; y: parent.height - 1; color: Theme.line }
    }
    Accessible.role: Accessible.AlertMessage
    Accessible.name: text
    RowLayout {
        anchors.fill: parent
        spacing: 8
        Icon {
            objectName: "inlineBarIcon"
            iconRole: bar.iconRole.length > 0 ? bar.iconRole : ({ error: "notice-error", warning: "notice-warning", info: "about" })[bar.severity] || "about"
            color: bar.iconRole.length > 0 || bar.severity === "info" ? IconTheme.normal
                 : bar.severity === "error" ? Theme.danger : Theme.warning
            accentColor: bar.iconRole.length > 0 || bar.severity === "info" ? IconTheme.accent : color
            size: 16
            Layout.alignment: Qt.AlignVCenter
        }
        Label {
            id: textLabel
            objectName: "inlineBarText"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: bar.text
        }
        RowLayout {
            id: actionRow
            spacing: 8
        }
        IconToolButton {
            id: close
            objectName: "inlineBarClose"
            visible: bar.closable
            iconRole: "tab-close"
            text: qsTr("Close")
            onClicked: bar.closed()
        }
    }
}
