pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

ShellMenu {
    id: menu
    property var load: function() { return [] }
    property string prefix: "recent"
    property var rows: []
    signal chosen(string path)
    onAboutToShow: rows = load()
    Instantiator {
        model: menu.rows
        delegate: ShellMenuItem {
            required property var modelData
            required property int index
            objectName: menu.prefix + index
            text: modelData.label
            ToolTip.text: qsTr("Open") + " " + modelData.path
            ToolTip.visible: hovered
            onTriggered: menu.chosen(modelData.path)
        }
        onObjectAdded: (index, object) => menu.insertItem(index, object)
        onObjectRemoved: (index, object) => menu.removeItem(object)
    }
    ShellMenuItem {
        text: qsTr("None")
        enabled: false
        visible: menu.rows.length === 0
        height: visible ? implicitHeight : 0
    }
}
