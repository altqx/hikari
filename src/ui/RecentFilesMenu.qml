pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

ShellMenu {
    id: recentMenu
    property var load: function() { return [] }
    property string prefix: "recent"
    property var rows: []
    signal chosen(string path)
    onAboutToShow: rows = load()
    Instantiator {
        model: recentMenu.rows
        delegate: ShellMenuItem {
            required property var modelData
            required property int index
            objectName: recentMenu.prefix + index
            text: modelData.label
            ToolTip.text: qsTr("Open") + " " + modelData.path
            ToolTip.visible: hovered
            onTriggered: recentMenu.chosen(modelData.path)
        }
        onObjectAdded: (index, object) => recentMenu.insertItem(index, object)
        onObjectRemoved: (index, object) => recentMenu.removeItem(object)
    }
    ShellMenuItem {
        text: qsTr("None")
        enabled: false
        visible: recentMenu.rows.length === 0
        height: visible ? implicitHeight : 0
    }
}
