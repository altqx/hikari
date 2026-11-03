import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Rectangle {
    id: list
    property var rows: []
    property bool nested: false
    property string selected: ""
    property int fixedHeight: 0
    property string accessibleName
    property var opened: ({})
    signal select(string id)
    signal activate(string id)

    readonly property var flat: flatten(rows || [], 0)
    function listOf(value) { return value && typeof value === "object" && typeof value.length === "number" ? value : [] }
    function isOpen(row) { return list.opened[row.id] ?? (row.expanded === true) }
    function flatten(items, depth) {
        const out = []
        for (const row of items) {
            const parent = list.nested && list.listOf(row.children).length > 0
            out.push({ row: row, depth: depth, parent: parent })
            if (parent && list.isOpen(row))
                out.push(...list.flatten(row.children, depth + 1))
        }
        return out
    }
    function toggle(row, open) {
        const next = Object.assign({}, list.opened)
        next[row.id] = open
        list.opened = next
    }
    function choose(i) {
        if (i < 0 || i >= list.flat.length)
            return
        view.currentIndex = i
        list.select(list.flat[i].row.id)
    }

    implicitWidth: 200
    implicitHeight: fixedHeight > 0 ? fixedHeight : Math.max(2 * 28, Math.min(view.contentHeight, 240)) + 2
    color: Theme.field
    border.color: Theme.line

    ListView {
        id: view
        objectName: "pluginListView"
        anchors.fill: parent
        anchors.margins: 1
        clip: true
        model: list.flat
        currentIndex: list.flat.findIndex(e => e.row.id === list.selected)
        activeFocusOnTab: true
        keyNavigationEnabled: false
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        Accessible.role: list.nested ? Accessible.Tree : Accessible.List
        Accessible.name: list.accessibleName
        Keys.onUpPressed: list.choose(Math.max(0, currentIndex - 1))
        Keys.onDownPressed: list.choose(Math.min(list.flat.length - 1, currentIndex + 1))
        Keys.onReturnPressed: if (currentIndex >= 0) list.activate(list.flat[currentIndex].row.id)
        Keys.onEnterPressed: if (currentIndex >= 0) list.activate(list.flat[currentIndex].row.id)
        Keys.onRightPressed: if (currentIndex >= 0 && list.flat[currentIndex].parent) list.toggle(list.flat[currentIndex].row, true)
        Keys.onLeftPressed: if (currentIndex >= 0 && list.flat[currentIndex].parent) list.toggle(list.flat[currentIndex].row, false)

        delegate: ItemDelegate {
            id: item
            required property var modelData
            required property int index
            width: ListView.view.width
            height: 28
            highlighted: modelData.row.id === list.selected
            focusPolicy: Qt.NoFocus
            leftPadding: 6 + modelData.depth * 16
            Accessible.role: list.nested ? Accessible.TreeItem : Accessible.ListItem
            Accessible.name: modelData.row.text + (modelData.row.detail ? ", " + modelData.row.detail : "")
            Accessible.selected: highlighted
            background: Rectangle {
                color: item.highlighted ? Theme.select : item.hovered ? Color.transparent(Theme.raised, 0.6) : "transparent"
                Rectangle { visible: item.highlighted; width: 3; height: parent.height; color: Theme.accent }
            }
            contentItem: RowLayout {
                spacing: 6
                Item {
                    visible: list.nested
                    implicitWidth: 16
                    implicitHeight: 16
                    Icon {
                        visible: item.modelData.parent
                        anchors.centerIn: parent
                        iconRole: list.isOpen(item.modelData.row) ? "chevron-down" : "chevron-right"
                        size: 12
                    }
                    TapHandler {
                        enabled: item.modelData.parent
                        onTapped: list.toggle(item.modelData.row, !list.isOpen(item.modelData.row))
                    }
                }
                Icon {
                    visible: !!item.modelData.row.icon
                    iconRole: item.modelData.row.icon || ""
                }
                Label {
                    text: item.modelData.row.text
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: item.modelData.row.muted === true ? Theme.muted : Theme.text
                    Layout.fillWidth: true
                }
                Label {
                    visible: !!item.modelData.row.detail
                    text: item.modelData.row.detail ?? ""
                    textFormat: Text.PlainText
                    color: Theme.muted
                }
            }
            onClicked: {
                view.forceActiveFocus(Qt.MouseFocusReason)
                list.choose(index)
            }
            onDoubleClicked: list.activate(modelData.row.id)
        }
    }
}
