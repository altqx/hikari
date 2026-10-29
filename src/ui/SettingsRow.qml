import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ColumnLayout {
    id: row
    property string iconRole
    property alias text: label.text
    property string note
    property int labelWidth: 220
    default property alias controls: controlRow.data

    spacing: 3
    Layout.fillWidth: true

    RowLayout {
        spacing: 8
        Layout.fillWidth: true
        Item {
            implicitWidth: 16
            implicitHeight: 16
            Layout.alignment: Qt.AlignVCenter
            Icon {
                anchors.centerIn: parent
                visible: row.iconRole.length > 0
                iconRole: row.iconRole
            }
        }
        Label {
            id: label
            wrapMode: Text.Wrap
            color: Theme.text
            Layout.preferredWidth: row.labelWidth
            Layout.alignment: Qt.AlignVCenter
        }
        RowLayout {
            id: controlRow
            spacing: 8
            Layout.fillWidth: true
        }
    }
    Label {
        visible: row.note.length > 0
        text: row.note
        color: Theme.muted
        wrapMode: Text.Wrap
        Layout.leftMargin: 24
        Layout.fillWidth: true
    }
}
