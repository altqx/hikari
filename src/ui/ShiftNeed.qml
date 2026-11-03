pragma Translator: "HikariSub.TimeShift"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

RowLayout {
    id: need
    property alias text: reason.text
    property alias command: commandLink.text
    property string iconRole: "open-video"
    signal run()
    spacing: 8
    Label {
        id: reason
        color: Theme.warning
        wrapMode: Text.WordWrap
        textFormat: Text.PlainText
        Layout.fillWidth: true
    }
    LinkButton {
        id: commandLink
        objectName: need.objectName + "Command"
        iconRole: need.iconRole
        onClicked: need.run()
    }
}
