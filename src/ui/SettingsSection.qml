import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ColumnLayout {
    id: section
    // The Settings window; with it and a key ("<page>.<section>"), the section hides while a search leaves it out.
    property var host: null
    property string key
    property bool project: false
    property string title: key.length > 0 ? SettingsCatalogue.sectionTitle(key) : ""
    property bool ruled: true
    default property alias content: rows.data
    property alias trailing: trailingSlot.data

    spacing: 10
    visible: host === null || key.length === 0 || host.sectionShown(key)
    Layout.fillWidth: true
    Component.onCompleted: if (host !== null && key.length > 0) host.registerSection(key, section, project)
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
    RowLayout {
        spacing: 8
        Layout.fillWidth: true
        Label {
            text: section.title
            font.bold: true
            color: Theme.text
            Layout.fillWidth: true
        }
        Item {
            id: trailingSlot
            visible: children.length > 0
            implicitWidth: childrenRect.width
            implicitHeight: childrenRect.height
        }
    }
    ColumnLayout {
        id: rows
        spacing: 8
        Layout.fillWidth: true
    }
}
