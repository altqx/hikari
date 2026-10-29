import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

RowLayout {
    id: error
    property string text
    visible: text.length > 0
    spacing: 6
    Icon {
        iconRole: "notice-error"
        size: 14
        color: Theme.danger
        accentColor: Theme.danger
        Layout.alignment: Qt.AlignTop
        Layout.topMargin: 1
    }
    Label {
        objectName: error.objectName.length > 0 ? error.objectName + "Text" : ""
        text: error.text
        color: Theme.danger
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        Accessible.role: Accessible.AlertMessage
        Accessible.name: text
    }
}
