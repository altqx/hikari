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
    padding: 1
    implicitWidth: 30 + leftPadding + rightPadding
    implicitHeight: tools.railOrder.length * 29 - 1 + 2 + topPadding + bottomPadding

    readonly property int slots: Math.max(1, Math.floor((availableHeight - 2 + familyColumn.spacing) / (28 + familyColumn.spacing)))
    readonly property bool overflows: tools.railOrder.length > slots
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

    ColumnLayout {
        id: familyColumn
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
                    iconRole: ["tool-crosshair", "tool-position", "tool-move", "tool-scale", "tool-rotate-z", "tool-rotate-xy", "tool-clip-rect", "tool-clip-vector", "tool-drawing", "tool-move-all", "tool-all-tags", "tool-perspective"][familyButton.family] ?? ""
                }
                ToolTip.visible: hovered || visualFocus
                ToolTip.text: rail.tipOf(family)
                Accessible.name: text
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
                iconRole: ["tool-crosshair", "tool-position", "tool-move", "tool-scale", "tool-rotate-z", "tool-rotate-xy", "tool-clip-rect", "tool-clip-vector", "tool-drawing", "tool-move-all", "tool-all-tags", "tool-perspective"][family] ?? ""
                text: rail.tools.families[family]?.name ?? ""
                help: rail.tipOf(family)
                enabled: rail.tools.railEnabled
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
        return !info.available ? qsTr("%1 (not available yet)").arg(name) : key.length ? name + " (" + key + ")" : name
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
