pragma Translator: "HikariSub.Workspace"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

Rectangle {
    id: bar
    property var entries: []
    property string shown
    property bool onLeft: true
    property bool panelFocused: false
    property bool locked: false
    property bool dropTarget: false
    property int dropIndex: -1
    property string dragging
    property Item toolbar: null
    readonly property Item slot: slotItem
    readonly property Item strip: stripRow
    readonly property var shownEntry: entries.find(e => e.name === shown) ?? null
    readonly property bool empty: entries.length === 0

    signal iconClicked(string panel)
    signal iconMenu(string panel, Item anchor)
    signal menuRequested(Item anchor)
    signal resized(int width, bool done)
    signal iconDragged(string panel, point scenePosition, bool done)

    objectName: "sideBar"
    color: Theme.panel
    clip: true
    Accessible.role: Accessible.Grouping
    Accessible.name: onLeft ? qsTr("Left side bar") : qsTr("Right side bar")

    Rectangle {
        id: stripBack
        objectName: "sideBarStrip"
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 35
        color: bar.dropTarget ? Color.transparent(Theme.accent, 0.22) : Theme.raised
        border.width: bar.dropTarget ? 1 : 0
        border.color: Theme.accent
        Accessible.role: Accessible.PageTabList
        Accessible.name: bar.Accessible.name

        Row {
            id: stripRow
            anchors { left: parent.left; leftMargin: 4; verticalCenter: parent.verticalCenter }
            spacing: 2
            Repeater {
                model: bar.entries
                delegate: IconToolButton {
                    id: icon
                    required property var modelData
                    required property int index
                    readonly property bool current: modelData.name === bar.shown
                    objectName: "sideBarIcon_" + modelData.name
                    iconRole: modelData.iconRole
                    text: modelData.plugin ? qsTr("%1 (plugin)").arg(modelData.title) : modelData.title
                    implicitWidth: 30
                    implicitHeight: 28
                    checkable: false
                    highlighted: current
                    opacity: bar.dragging === modelData.name ? 0.35 : 1
                    focusPolicy: index === Math.max(0, bar.entries.findIndex(e => e.name === bar.shown)) ? Qt.StrongFocus : Qt.ClickFocus
                    Accessible.role: Accessible.PageTab
                    Accessible.checkable: true
                    Accessible.checked: current
                    background: Rectangle {
                        color: icon.current ? Theme.select : icon.hovered ? Color.transparent(Theme.select, 0.6) : "transparent"
                        border.width: icon.visualFocus ? 1 : 0
                        border.color: Theme.focus
                        Rectangle {
                            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 3; rightMargin: 3; bottomMargin: -3 }
                            height: 2
                            color: Theme.accent
                            visible: icon.current
                        }
                        Rectangle {
                            width: 6; height: 6; radius: 3
                            anchors { right: parent.right; bottom: parent.bottom; margins: 4 }
                            color: Theme.accent
                            visible: icon.modelData.plugin === true
                        }
                    }
                    onClicked: bar.iconClicked(modelData.name)
                    Keys.onLeftPressed: bar.stepIcon(index, LayoutMirroring.enabled ? 1 : -1)
                    Keys.onRightPressed: bar.stepIcon(index, LayoutMirroring.enabled ? -1 : 1)
                    Keys.onMenuPressed: bar.iconMenu(modelData.name, icon)
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: bar.iconMenu(icon.modelData.name, icon)
                    }
                    DragHandler {
                        id: drag
                        enabled: !bar.locked
                        target: null
                        onActiveChanged: {
                            if (!active)
                                bar.iconDragged(icon.modelData.name, icon.mapToItem(null, centroid.position), true)
                        }
                        onCentroidChanged: {
                            if (active)
                                bar.iconDragged(icon.modelData.name, icon.mapToItem(null, centroid.position), false)
                        }
                    }
                }
            }
        }
        Rectangle {
            objectName: "sideBarDropLine"
            visible: bar.dropTarget && bar.dropIndex >= 0
            width: 2
            height: 22
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.accent
            x: stripRow.x + Math.min(bar.dropIndex, bar.entries.length) * 32 - 2
        }
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: Theme.line
        }
    }

    Rectangle {
        id: header
        objectName: "sideBarHeader"
        visible: bar.shownEntry !== null
        anchors { left: parent.left; right: parent.right; top: stripBack.bottom }
        height: visible ? 35 : 0
        color: Theme.field
        Accessible.role: Accessible.TitleBar
        Accessible.name: bar.shownEntry ? bar.shownEntry.title : ""
        Label {
            id: headerTitle
            objectName: "sideBarTitle"
            anchors { left: parent.left; leftMargin: 10; right: toolbarSlot.left; rightMargin: 6; verticalCenter: parent.verticalCenter }
            text: bar.shownEntry ? bar.shownEntry.title : ""
            color: Theme.text
            font.bold: true
            elide: Text.ElideRight
        }
        PanelToolbarSlot {
            id: toolbarSlot
            toolbar: header.visible ? bar.toolbar : null
            anchors { right: menuButton.left; rightMargin: 4; verticalCenter: parent.verticalCenter }
            height: parent.height - 1
            readonly property real room: Math.max(0, header.width - menuButton.width - 32)
            readonly property bool fits: toolbar !== null && room - 40 >= toolbar.implicitWidth
            width: !toolbar ? 0 : fits ? toolbar.implicitWidth : Math.max(0, Math.min(32, room - 40))
        }
        DockMenuButton {
            id: menuButton
            objectName: "sideBarMenuButton"
            anchors { right: parent.right; rightMargin: 6; verticalCenter: parent.verticalCenter }
            panelTitle: bar.shownEntry ? bar.shownEntry.title : ""
            onClicked: bar.menuRequested(menuButton)
        }
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: bar.menuRequested(menuButton)
        }
        PanelFocusMark {
            shown: bar.panelFocused
        }
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: Theme.line
        }
    }

    Item {
        id: slotItem
        objectName: "sideBarSlot"
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom }
    }

    Label {
        objectName: "sideBarPlaceholder"
        visible: bar.empty
        anchors.centerIn: slotItem
        width: Math.min(implicitWidth, slotItem.width - 24)
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        color: Theme.muted
        text: qsTr("Drag a panel here")
    }

    Item {
        objectName: "sideBarGrip"
        width: 11
        z: 3
        anchors { top: parent.top; bottom: parent.bottom }
        x: bar.onLeft ? parent.width - 6 : -5
        HoverHandler { cursorShape: Qt.SizeHorCursor }
        Rectangle {
            x: 5; width: 1; height: parent.height
            color: Theme.accent
            visible: grip.active
        }
        DragHandler {
            id: grip
            target: null
            xAxis.enabled: true
            yAxis.enabled: false
            property real startWidth
            onActiveChanged: {
                if (active)
                    startWidth = bar.width
                else
                    bar.resized(Math.round(bar.dragWidth()), true)
            }
            onTranslationChanged: if (active) bar.resized(Math.round(bar.dragWidth()), false)
        }
    }

    function dragWidth() {
        return grip.startWidth + (bar.onLeft ? grip.translation.x : -grip.translation.x)
    }

    function stepIcon(index, step) {
        const next = index + step
        if (next < 0 || next >= stripRow.children.length - 1)
            return
        const button = stripRow.children[next]
        if (button && button.forceActiveFocus)
            button.forceActiveFocus(Qt.TabFocusReason)
    }

    function iconAt(name) {
        for (const child of stripRow.children)
            if (child.objectName === "sideBarIcon_" + name)
                return child
        return null
    }

    function dropIndexAt(x) {
        return Math.max(0, Math.min(entries.length, Math.round((x - stripRow.x) / 32)))
    }
}
