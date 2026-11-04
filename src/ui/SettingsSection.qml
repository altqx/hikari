import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ColumnLayout {
    id: section
    property string title
    property bool ruled: true
    default property alias content: rows.data

    spacing: 10
    Layout.fillWidth: true
    Accessible.role: Accessible.Grouping
    Accessible.name: title

    Rectangle {
        visible: section.ruled
        color: Theme.line
        implicitHeight: 1
        Layout.fillWidth: true
        Layout.topMargin: 6
        Layout.bottomMargin: 6
    }
    Label {
        text: section.title
        font.bold: true
        color: Theme.text
        Layout.fillWidth: true
    }
    ColumnLayout {
        id: rows
        spacing: 8
        Layout.fillWidth: true
    }
}
