pragma Translator: "HikariSub.Workspace"
import QtQuick
import QtQuick.Controls
import "qrc:/kddockwidgets/qtquick/views/qml/" as KDDWViews
import Hikari.Ui

KDDWViews.TabBarBase {
    id: root
    objectName: "dockHeader"

    readonly property int headerHeight: 35
    readonly property int contentGap: titleMode ? 0 : 8
    implicitHeight: headerHeight

    function nameAt(index: int): string {
        return root.tabBarCpp && index >= 0 && index < root.count ? Docking.dockNameAt(root.tabBarCpp, index) : ""
    }
    // Compiled bindings drop unused reads and their dependencies: the revisions are compared, not just read.
    readonly property string currentName: Docking.headerRevision >= 0 ? nameAt(root.currentTabIndex) : ""
    readonly property Item currentTab: root.currentTabIndex >= 0 && root.currentTabIndex < tabRepeater.count
                                       ? tabRepeater.itemAt(root.currentTabIndex) : null
    readonly property string currentTitle: currentTab ? currentTab.text : ""
    readonly property bool titleMode: root.count === 1 && Docking.headerRevision >= 0 && !Docking.isHorizontal(nameAt(0))
    readonly property bool floating: Window.window !== null && Window.window.transientParent !== null
    // Some compositors frame the floating window anyway and show its title themselves.
    readonly property bool systemTitle: floating && Window.window.title === currentTitle
                                        && Docking.windowTitleRevision >= 0 && Docking.windowSystemTitle(Window.window)
    readonly property bool panelFocused: root.groupCpp !== null && root.groupCpp.titleBar !== null
                                         && root.groupCpp.titleBar.isFocused
    readonly property bool menuOpen: Docking.openMenu.length > 0 && Docking.openMenu === root.currentName

    Accessible.role: titleMode ? Accessible.TitleBar : Accessible.PageTabList
    Accessible.name: titleMode ? currentTitle : qsTr("Panels")

    // KDDockWidgets calls these.
    function getTabAtIndex(index) {
        return tabRepeater.itemAt(index)
    }
    function getTabIndexAtPosition(globalPoint) {
        if (root.count === 1)
            return root.contains(root.mapFromGlobal(globalPoint.x, globalPoint.y)) ? 0 : -1
        if (toolbarSlot.visible && toolbarSlot.contains(toolbarSlot.mapFromGlobal(globalPoint.x, globalPoint.y)))
            return root.currentTabIndex
        for (let i = 0; i < tabRepeater.count; ++i) {
            const tab = tabRepeater.itemAt(i)
            if (tab && tab.contains(tab.mapFromGlobal(globalPoint.x, globalPoint.y)))
                return i
        }
        return -1
    }

    // Windows turns Shift+F10 into a context menu request (WM_CONTEXTMENU), never a key press.
    ContextMenu.onRequested: {
        for (let i = 0; i < tabRepeater.count; ++i) {
            const tab = tabRepeater.itemAt(i)
            if (tab && tab.activeFocus) {
                root.openMenu(root.nameAt(i), menuButton)
                return
            }
        }
        if (menuButton.activeFocus)
            root.openMenu(root.currentName, menuButton)
    }

    function headerKey(event): bool {
        const mods = event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)
        if (event.key === Qt.Key_F10)
            return mods === Qt.ShiftModifier
        return mods === Qt.NoModifier && [Qt.Key_Left, Qt.Key_Right, Qt.Key_Space, Qt.Key_Return, Qt.Key_Enter,
                                          Qt.Key_Menu].includes(event.key)
    }
    function openMenu(name: string, anchor: Item) {
        Docking.requestMenu(name, anchor)
    }
    function selectTab(index: int, reason: int) {
        if (index < 0 || index >= root.count)
            return
        root.currentTabIndex = index
        const tab = tabRepeater.itemAt(index)
        if (tab)
            tab.forceActiveFocus(reason)
    }

    Rectangle {
        objectName: "dockHeaderStrip"
        anchors.fill: parent
        color: root.titleMode ? Theme.field : Theme.raised
    }
    Rectangle {
        visible: !root.titleMode
        width: parent.width
        height: 1
        y: parent.height - 1
        color: Theme.line
    }

    Text {
        id: titleText
        objectName: "dockTitleText"
        visible: root.titleMode && !root.systemTitle
        x: 12
        width: Math.max(0, (systemMoveArea.enabled ? systemMoveArea.x : menuButton.x) - x - 4)
        anchors.verticalCenter: parent.verticalCenter
        text: root.currentTitle
        textFormat: Text.PlainText
        color: Theme.text
        font.bold: true
        elide: Text.ElideRight
        Accessible.ignored: true
    }

    readonly property real slotWidth: toolbarSlot.width > 0 ? toolbarSlot.width + 8 : 0
    readonly property real naturalTabsWidth: {
        let sum = 0
        for (let i = 0; i < tabRepeater.count; ++i) {
            const tab = tabRepeater.itemAt(i)
            if (tab)
                sum += tab.naturalWidth
        }
        return sum
    }
    readonly property real selectedNaturalWidth: currentTab ? currentTab.naturalWidth : 0
    Row {
        id: tabRow
        visible: !root.titleMode
        height: parent.height
        Repeater {
            id: tabRepeater
            model: root.groupCpp ? root.groupCpp.tabBar.dockWidgetModel : 0
            delegate: Item {
                id: tab
                required property int index
                required property string title
                readonly property int tabIndex: index // KDDockWidgets reads it
                readonly property string text: title // KDDockWidgets reads it
                readonly property bool selected: index === root.currentTabIndex
                readonly property bool hovered: root.tabBarCpp !== null && root.tabBarCpp.hoveredTabIndex === index
                readonly property real naturalWidth: Math.ceil(10 + boldMetrics.advanceWidth
                                                               + (selected ? 6 + menuButton.width + 6 : 10) + 1)
                readonly property bool cutOff: !selected && root.naturalTabsWidth > root.width - root.slotWidth
                objectName: "dockTab" + index
                height: root.height
                clip: cutOff
                width: {
                    if (!cutOff)
                        return naturalWidth
                    const others = root.naturalTabsWidth - root.selectedNaturalWidth
                    const left = Math.max(0, root.width - root.slotWidth - root.selectedNaturalWidth)
                    return Math.floor(naturalWidth * left / Math.max(1, others))
                }
                activeFocusOnTab: (selected && !root.titleMode) || activeFocus
                Accessible.role: Accessible.PageTab
                Accessible.name: title
                Accessible.checkable: true
                Accessible.checked: selected
                Accessible.focusable: true
                Accessible.ignored: root.titleMode || !root.visible
                Accessible.onPressAction: root.currentTabIndex = index

                TextMetrics {
                    id: boldMetrics
                    font: Qt.font({ family: label.font.family, pointSize: label.font.pointSize, bold: true })
                    text: tab.title
                }
                Rectangle {
                    objectName: "dockTabBackground"
                    anchors.fill: parent
                    color: tab.selected ? Theme.field : Theme.raised
                }
                Rectangle {
                    anchors.fill: parent
                    anchors.bottomMargin: 1
                    visible: tab.hovered && !tab.selected
                    color: Theme.field
                    opacity: 0.5
                }
                Text {
                    id: label
                    x: 10
                    width: tab.cutOff ? undefined
                                      : Math.max(0, tab.width - x - (tab.selected ? 6 + menuButton.width + 6 : 10) - 1)
                    anchors.verticalCenter: parent.verticalCenter
                    text: tab.title
                    textFormat: Text.PlainText
                    color: Theme.text
                    font.bold: tab.selected
                    elide: tab.cutOff ? Text.ElideNone : Text.ElideRight
                    Accessible.ignored: true
                }
                Rectangle {
                    objectName: "dockTabFade"
                    visible: tab.cutOff
                    width: Math.min(20, tab.width - 1)
                    x: tab.width - 1 - width
                    height: tab.height - 1
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: Qt.alpha(Theme.raised, 0) }
                        GradientStop { position: 1.0; color: Qt.alpha(Theme.raised, 0.7) }
                    }
                }
                Rectangle {
                    width: 1
                    height: parent.height - (tab.selected ? 0 : 1)
                    x: parent.width - 1
                    color: Theme.line
                }
                Rectangle {
                    visible: !tab.selected
                    width: parent.width
                    height: 1
                    y: parent.height - 1
                    color: Theme.line
                }
                Rectangle {
                    objectName: "tabFocusRing"
                    anchors.fill: parent
                    anchors.margins: 1
                    anchors.rightMargin: tab.selected ? 6 + menuButton.width + 3 : 1
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.focus
                    visible: tab.activeFocus
                }

                Keys.onShortcutOverride: event => event.accepted = root.headerKey(event)
                Keys.onLeftPressed: root.selectTab(index - 1, Qt.TabFocusReason)
                Keys.onRightPressed: {
                    if (tab.selected && menuButton.visible)
                        menuButton.forceActiveFocus(Qt.TabFocusReason)
                    else
                        root.selectTab(index + 1, Qt.TabFocusReason)
                }
                Keys.onSpacePressed: root.currentTabIndex = index
                Keys.onMenuPressed: root.openMenu(root.nameAt(index), menuButton)
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)) {
                        root.openMenu(root.nameAt(index), menuButton)
                        event.accepted = true
                    }
                }
            }
        }
    }

    DockMenuButton {
        id: menuButton
        z: root.mouseAreaZ + 2
        visible: root.count > 0 && (root.titleMode || root.currentTab !== null)
        panelTitle: root.currentTitle
        menuOpen: root.menuOpen
        x: root.titleMode ? root.width - 12 - width
                          : (root.currentTab ? tabRow.x + root.currentTab.x + root.currentTab.width - 1 - 6 - width : 0)
        anchors.verticalCenter: parent.verticalCenter
        // Qt refuses to take the focused item out of the Tab order.
        activeFocusOnTab: root.titleMode || activeFocus
        focusPolicy: Qt.TabFocus
        onClicked: root.openMenu(root.currentName, menuButton)
        Keys.onShortcutOverride: event => event.accepted = root.headerKey(event)
        Keys.onLeftPressed: {
            if (!root.titleMode)
                root.selectTab(root.currentTabIndex, Qt.TabFocusReason)
        }
        Keys.onRightPressed: {
            if (!root.titleMode)
                root.selectTab(root.currentTabIndex + 1, Qt.TabFocusReason)
        }
    }

    Item {
        id: toolbarSlot
        objectName: "dockToolbarSlot"
        readonly property Item toolbar: !root.titleMode && Docking.headerRevision >= 0 ? Docking.toolbar(root.currentName) : null
        z: root.mouseAreaZ + 2
        visible: toolbar !== null
        anchors.right: parent.right
        anchors.rightMargin: 4
        height: parent.height - 1
        width: toolbar ? Math.min(toolbar.implicitWidth, Math.max(0, root.width - root.selectedNaturalWidth - 12)) : 0
        function release(keep: Item) {
            for (let i = children.length - 1; i >= 0; --i) {
                const item = children[i]
                if (item && item !== keep)
                    item.parent = null
            }
        }
        function host() {
            release(toolbar)
            if (toolbar) {
                toolbar.parent = toolbarSlot
                toolbar.anchors.fill = toolbarSlot
            }
        }
        onToolbarChanged: host()
        Component.onCompleted: host()
        Component.onDestruction: release(null)
    }

    MouseArea {
        id: headerArea
        objectName: "dockHeaderArea"
        z: root.mouseAreaZ + 1
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        cursorShape: Qt.SizeAllCursor
        onPressed: mouse => {
            const at = root.count === 1 ? 0 : root.getTabIndexAtPosition(mapToGlobal(mouse.x, mouse.y))
            const index = at >= 0 ? at : root.currentTabIndex
            if (index < 0)
                return
            root.currentTabIndex = index
            root.openMenu(root.nameAt(index), menuButton)
        }
    }

    DockSystemMoveArea {
        id: systemMoveArea
        z: root.mouseAreaZ + 2
        enabled: root.floating && Docking.systemMove
        visible: enabled
        readonly property real start: root.titleMode
                                      ? Math.max(titleText.x + 48, Math.min(titleText.x + titleText.implicitWidth + 8, menuButton.x - 2 - 48))
                                      : tabRow.width
        readonly property real end: root.titleMode ? menuButton.x - 2 : root.width - root.slotWidth
        x: start
        width: Math.max(0, end - start)
        height: parent.height
        onDoubleClicked: Docking.toggleFloating(root.currentName)
    }

    PanelFocusMark {
        z: root.mouseAreaZ + 3
        shown: root.panelFocused
    }
}
