pragma Translator: "HikariSub.VisualTools"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Frame {
    id: rail
    objectName: "visualToolRail"
    required property VisualToolsController tools
    property var keyOf: symbol => ""
    property bool named: false
    property string activeHint: ""
    readonly property alias list: namedList
    padding: named ? 0 : 1
    implicitWidth: named ? 188 : 30 + leftPadding + rightPadding
    implicitHeight: named ? namedList.contentHeight : tools.railOrder.length * 29 - 1 + 2 + topPadding + bottomPadding
    Binding {
        target: rail.background
        property: "visible"
        value: !rail.named
    }

    readonly property int slots: Math.max(1, Math.floor((availableHeight - 2 + familyColumn.spacing) / (28 + familyColumn.spacing)))
    readonly property bool overflows: !named && tools.railOrder.length > slots
    readonly property var shown: {
        const order = tools.railOrder
        if (!overflows)
            return order
        const out = order.slice(0, Math.max(0, slots - 1))
        if (out.length > 0 && !out.includes(tools.activeFamily) && order.includes(tools.activeFamily))
            out[out.length - 1] = tools.activeFamily
        return out
    }
    readonly property var folded: tools.railOrder.filter(f => !shown.includes(f))
    readonly property bool menuOpen: moreMenu.opened

    VisualToolTexts {
        id: texts
    }
    ListView {
        id: namedList
        objectName: "visualToolList"
        anchors.fill: parent
        visible: rail.named
        enabled: rail.named
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: false
        activeFocusOnTab: false
        currentIndex: Math.max(0, model.findIndex(r => r.family === rail.tools.activeFamily))
        model: {
            const rows = []
            texts.groups.forEach((g, i) => g.forEach((f, j) => rows.push({ family: f, split: i > 0 && j === 0 })))
            return rows
        }
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Visual tools")
        Keys.onUpPressed: currentIndex = Math.max(0, currentIndex - 1)
        Keys.onDownPressed: currentIndex = Math.min(count - 1, currentIndex + 1)
        Keys.onReturnPressed: rail.tools.selectFamily(model[currentIndex].family)
        Keys.onEnterPressed: rail.tools.selectFamily(model[currentIndex].family)
        Keys.onSpacePressed: rail.tools.selectFamily(model[currentIndex].family)
        delegate: ItemDelegate {
            id: row
            required property var modelData
            required property int index
            readonly property int family: modelData.family
            readonly property bool chosen: rail.tools.activeFamily === family
            objectName: "visualToolRow" + family
            width: ListView.view.width
            height: 28 + topInset
            topInset: modelData.split ? 1 : 0
            topPadding: topInset
            leftPadding: 10
            rightPadding: 8
            enabled: rail.tools.railEnabled && (rail.tools.families[family]?.available ?? false)
            focusPolicy: Qt.NoFocus
            text: rail.tools.families[family]?.name ?? ""
            Accessible.role: Accessible.ListItem
            Accessible.name: text
            Accessible.description: chosen ? rail.activeHint : ""
            Accessible.selectable: true
            Accessible.selected: chosen
            ToolTip.visible: hovered
            ToolTip.text: rail.tipOf(family)
            ToolTip.delay: 600
            onClicked: rail.tools.selectFamily(family)
            background: Item {
                Rectangle {
                    visible: row.modelData.split
                    width: parent.width
                    height: 1
                    color: Theme.line
                }
                Rectangle {
                    y: row.topInset
                    width: parent.width
                    height: parent.height - row.topInset
                    color: row.chosen ? Theme.select : row.down ? Theme.pressed : row.hovered ? Theme.hover : "transparent"
                    border.width: row.chosen && Theme.highContrast ? 1 : 0
                    border.color: Theme.accent
                }
                Rectangle {
                    y: row.topInset
                    width: parent.width
                    height: parent.height - row.topInset
                    visible: namedList.activeFocus && namedList.currentIndex === row.index
                    color: "transparent"
                    border.color: Theme.focus
                }
            }
            contentItem: RowLayout {
                spacing: 8
                Icon {
                    objectName: "visualToolRowIcon" + row.family
                    // MUST KILL: iconRole list repeats application::visual::Family order, and icon_tests parses this one line
                    iconRole: ["tool-crosshair", "tool-position", "tool-move", "tool-scale", "tool-rotate-z", "tool-rotate-xy", "tool-clip-rect", "tool-clip-vector", "tool-drawing", "tool-move-all", "tool-all-tags", "tool-perspective", "tool-gradient", "tool-text-on-path"][row.family] ?? ""
                    enabled: row.enabled
                }
                Label {
                    objectName: "visualToolRowName" + row.family
                    Layout.fillWidth: true
                    text: texts.shortName(row.family)
                    font.weight: row.chosen ? Font.DemiBold : Font.Normal
                    color: row.enabled ? Theme.text : Theme.disabled
                    elide: Text.ElideRight
                }
            }
        }
    }

    ColumnLayout {
        id: familyColumn
        visible: !rail.named
        x: 1
        y: 1
        width: 28
        spacing: 1
        Repeater {
            id: families
            model: rail.tools.railOrder
            delegate: ToolButton {
                id: familyButton
                required property int modelData
                readonly property int family: modelData
                readonly property var info: rail.tools.families[family] ?? ({})
                objectName: "visualTool" + family
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                Layout.alignment: Qt.AlignHCenter
                visible: rail.shown.includes(family)
                text: info.name ?? ""
                display: AbstractButton.IconOnly
                checked: rail.tools.activeFamily === family
                enabled: rail.tools.railEnabled
                focusPolicy: Qt.TabFocus
                contentItem: Icon {
                    objectName: "visualToolIcon" + familyButton.family
                    // MUST KILL: iconRole list repeats application::visual::Family order, and icon_tests parses this one line
                    iconRole: ["tool-crosshair", "tool-position", "tool-move", "tool-scale", "tool-rotate-z", "tool-rotate-xy", "tool-clip-rect", "tool-clip-vector", "tool-drawing", "tool-move-all", "tool-all-tags", "tool-perspective", "tool-gradient", "tool-text-on-path"][familyButton.family] ?? ""
                }
                ToolTip.visible: hovered || visualFocus
                ToolTip.text: rail.tipOf(family)
                Accessible.name: text
                Accessible.description: checked ? rail.activeHint : ""
                Accessible.checkable: true
                Keys.onUpPressed: rail.focusNext(this, -1)
                Keys.onDownPressed: rail.focusNext(this, 1)
                onClicked: rail.tools.selectFamily(family)
            }
        }
        ToolButton {
            id: more
            objectName: "visualToolsMore"
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
            Layout.alignment: Qt.AlignHCenter
            visible: rail.overflows
            text: qsTr("More tools")
            display: AbstractButton.IconOnly
            enabled: rail.tools.railEnabled
            focusPolicy: Qt.TabFocus
            contentItem: Icon { iconRole: "menu-more" }
            ToolTip.visible: (hovered || visualFocus) && !moreMenu.visible
            ToolTip.text: text
            Accessible.name: text
            Keys.onUpPressed: rail.focusNext(this, -1)
            Keys.onDownPressed: rail.focusNext(this, 1)
            onClicked: moreMenu.popup(more, more.width + 2, 0)
        }
    }

    ShellMenu {
        id: moreMenu
        objectName: "visualToolsMoreMenu"
        Instantiator {
            model: rail.folded
            delegate: ShellMenuItem {
                required property int modelData
                readonly property int family: modelData
                objectName: "visualToolsMore" + family
                // MUST KILL: iconRole list repeats application::visual::Family order, and icon_tests parses this one line
                iconRole: ["tool-crosshair", "tool-position", "tool-move", "tool-scale", "tool-rotate-z", "tool-rotate-xy", "tool-clip-rect", "tool-clip-vector", "tool-drawing", "tool-move-all", "tool-all-tags", "tool-perspective", "tool-gradient", "tool-text-on-path"][family] ?? ""
                text: rail.tools.families[family]?.name ?? ""
                help: rail.tipOf(family)
                enabled: rail.tools.railEnabled
                checkable: true
                checked: rail.tools.activeFamily === family
                keepsMenuOpen: false
                // MUST KILL: the menu must close before selectFamily rebuilds its entries
                onTriggered: {
                    moreMenu.close()
                    rail.tools.selectFamily(family)
                }
            }
            onObjectAdded: (index, object) => moreMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => moreMenu.removeItem(object)
        }
    }

    function tipOf(family) {
        const info = tools.families[family] ?? ({})
        const name = info.name ?? ""
        const key = rail.keyOf(info.hotkey ?? "")
        const tip = !info.available ? qsTr("%1 (not available yet)").arg(name) : key.length ? name + " (" + key + ")" : name
        return family === tools.activeFamily && activeHint.length > 0 ? tip + "\n" + activeHint : tip
    }
    function focusNext(from, step) {
        const buttons = []
        for (let i = 0; i < families.count; ++i)
            if (families.itemAt(i).visible)
                buttons.push(families.itemAt(i))
        if (more.visible)
            buttons.push(more)
        const at = buttons.indexOf(from)
        if (buttons.length > 0)
            buttons[(at + step + buttons.length) % buttons.length].forceActiveFocus(Qt.TabFocusReason)
    }
    onOverflowsChanged: if (!overflows) moreMenu.close()
}
