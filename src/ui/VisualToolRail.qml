pragma Translator: "HikariSub.VisualTools"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// T1: the visual tool rail (accepted layout A on #55): legacy VideoToolbar's
// eleven families beside the Video canvas, in its order, with its help texts,
// and VT3's Perspective after Text X / Y rotation (railOrder).
// One family is on; choosing it again goes back to the crosshair
// (VideoToolbar.cpp:205-233). For a Document that is not ASS the rail is
// disabled (VideoToolbar::DisableVisuals). A family whose tool has not
// landed yet (T2-T6) is selectable and draws nothing; its tooltip says so.
// The rail is an icon-only tool strip (visual-language.md, "Tool strips"):
// each family's button shows its icon of the K1 set (legacy VideoToolbar's
// bitmaps, Cross.png to AllTags.png), the family's name is its tooltip and
// accessible name, and the on family is the style's checked button. The
// buttons take keyboard focus by Tab (not by a click, which leaves it with
// the video), Up and Down move between them, Space chooses.
Frame {
    id: rail
    objectName: "visualToolRail"
    required property VisualToolsController tools
    // VT2: (command) => its keys in the Video window, for the tooltips.
    property var keyOf: symbol => ""
    // The buttons sit 1 inside the frame's padding (2 inside the frame, as
    // before the 2026-10-13 focus ring), so a focused button's 1-wide ring
    // is not clipped by it.
    padding: 1
    implicitWidth: 30 + leftPadding + rightPadding
    // Every family's height, whatever is folded: a rail placed by its
    // implicit height (fullscreen's) gets them back when there is room.
    implicitHeight: tools.railOrder.length * 29 - 1 + 2 + topPadding + bottomPadding

    // VB1 (#481): a Video panel too short for the twelve families shows the
    // ones that fit and ends in More tools, which lists the others; the on
    // family keeps the last slot, so it stays in sight. No family is ever
    // cut off.
    readonly property int slots: Math.max(1, Math.floor((availableHeight - 2 + familyColumn.spacing) / (28 + familyColumn.spacing)))
    readonly property bool overflows: tools.railOrder.length > slots
    // The families shown, in the rail's order.
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

    ColumnLayout {
        id: familyColumn
        x: 1
        y: 1
        width: 28
        spacing: 1
        Repeater {
            id: families
            // VT3: in the rail's order; each button names its family's id.
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
                    // In application::visual::Family's order (on one line: icon_tests reads the roles from it).
                    iconRole: ["tool-crosshair", "tool-position", "tool-move", "tool-scale", "tool-rotate-z", "tool-rotate-xy", "tool-clip-rect", "tool-clip-vector", "tool-drawing", "tool-move-all", "tool-all-tags", "tool-perspective"][familyButton.family] ?? ""
                }
                ToolTip.visible: hovered || visualFocus
                // VT2: the tool's key beside its name, as the shell's tooltips show theirs.
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
            // Beside the rail, its top at the button's.
            onClicked: moreMenu.popup(more, more.width + 2, 0)
        }
    }

    // The families that do not fit, in the rail's order.
    ShellMenu {
        id: moreMenu
        objectName: "visualToolsMoreMenu"
        Instantiator {
            model: rail.folded
            delegate: ShellMenuItem {
                required property int modelData
                readonly property int family: modelData
                objectName: "visualToolsMore" + family
                // In application::visual::Family's order (on one line: icon_tests reads the roles from it).
                iconRole: ["tool-crosshair", "tool-position", "tool-move", "tool-scale", "tool-rotate-z", "tool-rotate-xy", "tool-clip-rect", "tool-clip-vector", "tool-drawing", "tool-move-all", "tool-all-tags", "tool-perspective"][family] ?? ""
                text: rail.tools.families[family]?.name ?? ""
                help: rail.tipOf(family)
                enabled: rail.tools.railEnabled
                keepsMenuOpen: false
                // The menu first: choosing a family changes its entries.
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
    // Up and Down walk the buttons shown, More tools included, round.
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
    // The menu goes with the room it was opened for.
    onOverflowsChanged: if (!overflows) moreMenu.close()
}
