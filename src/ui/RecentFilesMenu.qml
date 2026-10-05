import QtQuick
import QtQuick.Controls
import Hikari.Ui

// V3: a recent-files submenu (legacy HikariSubFrame::AppendRecent,
// HikariSubFrame.cpp:1549-1591): rebuilt each time it opens from `load`,
// which prunes missing local files first; "<n> <name>" rows, the full path
// in the tooltip (legacy's help text "Open <path>"), "None" disabled when
// empty. `chosen` gives the row's path. `prefix` names the rows' objectName.
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
