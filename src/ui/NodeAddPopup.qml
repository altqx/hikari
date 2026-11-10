pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.NodeEffects"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Popup {
    id: popup
    objectName: "nodeAddPopup"
    required property NodeEffectsController controller
    property var at: null
    property var from: null
    property var openCategories: ({})
    signal chosen(string type)
    width: 320
    height: Math.min(400, column.implicitHeight + topPadding + bottomPadding)
    padding: 6
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    readonly property var typeTitles: ({
        Number: qsTr("Number"), Text: qsTr("Text"), Colour: qsTr("Colour"), Alpha: qsTr("Alpha"), Time: qsTr("Time"),
        Point: qsTr("Point"), Tags: qsTr("Tags"), Drawing: qsTr("Drawing"), Bool: qsTr("Yes or no"), Units: qsTr("Units")
    })

    function openAt(point, wire, text) {
        at = point
        from = wire
        openCategories = ({})
        const host = parent
        x = point ? Math.max(0, Math.min(host.width - width, point.x)) : (host.width - width) / 2
        y = point ? Math.max(0, Math.min(host.height - height, point.y)) : Math.max(0, (host.height - height) / 3)
        search.text = text
        open()
        search.forceActiveFocus()
        search.cursorPosition = search.length
        list.currentIndex = firstChoosable()
    }

    function entries() {
        const insideGroup = controller.path.length > 0
        const nodes = controller.catalogue.filter(e => insideGroup || !e.insideGroups)
        return nodes.filter(e => !e.lua).concat(nodes.filter(e => e.lua))
    }
    readonly property var rows: {
        const all = entries()
        const allowed = from ? controller.nodeTypesTaking(from.type) : null
        const query = search.text.trim().toLowerCase()
        const out = []
        if (query !== "" || allowed) {
            for (const e of all)
                if ((!allowed || allowed.indexOf(e.type) >= 0)
                        && (query === "" || e.title.toLowerCase().indexOf(query) >= 0 || e.summary.toLowerCase().indexOf(query) >= 0))
                    out.push({ kind: "node", entry: e, inCategory: false })
            return out
        }
        const categories = []
        for (const e of all) {
            let c = categories.find(x => x.category === e.category)
            if (!c) {
                c = { category: e.category, title: e.categoryTitle, entries: [] }
                categories.push(c)
            }
            c.entries.push(e)
        }
        for (const c of categories) {
            out.push({ kind: "category", category: c.category, title: c.title, count: c.entries.length })
            if (openCategories[c.category])
                for (const e of c.entries)
                    out.push({ kind: "node", entry: e, inCategory: true })
        }
        return out
    }
    function firstChoosable() {
        return rows.length > 0 ? 0 : -1
    }
    function activate(index) {
        const row = rows[index]
        if (!row)
            return
        if (row.kind === "category") {
            const next = Object.assign({}, openCategories)
            next[row.category] = !next[row.category]
            openCategories = next
            list.currentIndex = index
            return
        }
        choose(row.entry.type)
    }
    function choose(type) {
        close()
        chosen(type)
    }

    background: Rectangle {
        color: Theme.panel
        border.width: 1
        border.color: Theme.line
        radius: 3
    }

    contentItem: ColumnLayout {
        id: column
        spacing: 6
        TextField {
            id: search
            objectName: "nodeAddSearch"
            Layout.fillWidth: true
            placeholderText: qsTr("Search nodes")
            Accessible.name: qsTr("Search nodes")
            onTextChanged: list.currentIndex = popup.firstChoosable()
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Down) {
                    list.currentIndex = Math.min(popup.rows.length - 1, list.currentIndex + 1)
                } else if (event.key === Qt.Key_Up) {
                    list.currentIndex = Math.max(0, list.currentIndex - 1)
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    popup.activate(list.currentIndex)
                } else if ((event.key === Qt.Key_Right || event.key === Qt.Key_Left) && search.text === ""
                           && popup.rows[list.currentIndex]?.kind === "category") {
                    const row = popup.rows[list.currentIndex]
                    if (!!popup.openCategories[row.category] !== (event.key === Qt.Key_Right))
                        popup.activate(list.currentIndex)
                } else {
                    return
                }
                event.accepted = true
            }
        }
        Label {
            objectName: "nodeAddFilter"
            visible: popup.from !== null
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.muted
            textFormat: Text.PlainText
            //: Over the Add node list after a wire is dropped in empty space: "Nodes with a Tags input, for fade › tags"
            text: popup.from ? qsTr("Nodes with a %1 input, for %2 › %3")
                               .arg(popup.typeTitles[popup.from.type] ?? popup.from.type, popup.from.fromNode, popup.from.fromPort) : ""
        }
        Flow {
            objectName: "nodeAddRecent"
            visible: search.text === "" && popup.from === null && popup.controller.recent.length > 0
            Layout.fillWidth: true
            spacing: 4
            Label {
                text: qsTr("Recently added")
                color: Theme.muted
                height: 24
                verticalAlignment: Text.AlignVCenter
            }
            Repeater {
                model: popup.controller.recent
                delegate: ToolButton {
                    required property var modelData
                    required property int index
                    objectName: "nodeAddRecent_" + index
                    height: 24
                    text: modelData.title
                    onClicked: popup.choose(modelData.type)
                }
            }
        }
        ListView {
            id: list
            objectName: "nodeAddList"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 300)
            clip: true
            model: popup.rows
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            keyNavigationEnabled: false
            ScrollBar.vertical: ScrollBar {}
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Nodes")
            delegate: ItemDelegate {
                id: row
                required property var modelData
                required property int index
                readonly property bool category: modelData.kind === "category"
                objectName: "nodeAddRow_" + index
                width: list.width
                highlighted: ListView.isCurrentItem
                leftPadding: !category && modelData.inCategory ? 22 : 8
                Accessible.role: category ? Accessible.Button : Accessible.ListItem
                Accessible.name: category ? modelData.title : modelData.entry.title
                Accessible.description: category ? "" : modelData.entry.summary
                contentItem: ColumnLayout {
                    spacing: 1
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Label {
                            Layout.fillWidth: true
                            text: row.category ? row.modelData.title : row.modelData.entry.title
                            font.bold: row.category
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                        }
                        Label {
                            visible: !row.category && !row.modelData.inCategory
                            text: row.category ? "" : row.modelData.entry.categoryTitle
                            color: Theme.muted
                            font.pixelSize: 11
                        }
                        Label {
                            visible: row.category
                            text: row.category ? row.modelData.count + (popup.openCategories[row.modelData.category] ? " ⌄" : " ›") : ""
                            color: Theme.muted
                        }
                    }
                    Label {
                        visible: !row.category && (!row.modelData.inCategory || row.highlighted)
                        Layout.fillWidth: true
                        text: row.category ? "" : row.modelData.entry.summary
                        color: Theme.muted
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }
                }
                onClicked: popup.activate(index)
            }
        }
        Label {
            objectName: "nodeAddNone"
            visible: popup.rows.length === 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.muted
            text: qsTr("No node matches.")
        }
        Label {
            objectName: "nodeAddHint"
            visible: popup.from !== null && list.currentIndex >= 0 && popup.rows[list.currentIndex]?.kind === "node"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.muted
            font.pixelSize: 11
            textFormat: Text.PlainText
            //: Under the filtered Add node list: "Enter adds Transform here, wired from fade › tags"
            text: visible ? qsTr("Enter adds %1 here, wired from %2 › %3")
                            .arg(popup.rows[list.currentIndex].entry.title, popup.from.fromNode, popup.from.fromPort) : ""
        }
    }
}
