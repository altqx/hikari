import QtQuick
import QtQuick.Controls
import "qrc:/kddockwidgets/qtquick/views/qml/" as KDDWViews
import Hikari.Ui

// D3: a panel group's one header row, after MuseScore 4's DockFrame.qml,
// DockTitleBar.qml, DockTabBar.qml and DockPanelTab.qml
// (docs/research/musescore-docking.md §1, §5, §7). The adapter has the
// engine always show a group's tabs and hide its title bar, so this row is
// the group's only header, docked or floating:
//
// - a lone panel: a title bar, its bold title and the "⋯" button, in the
//   panel body's colour;
// - two or more panels, or a horizontal panel (the Grid, the Reference tray,
//   Search) even alone: tabs only, on the `raised` strip. The selected tab is
//   in the body's colour and bold, with no bottom line, and carries the "⋯"
//   button; 1-pixel lines separate the others. The current panel's toolbar
//   (Docking.setPanelHeader) sits right of the tabs.
//
// The header is the drag handle (the engine's mouse area lies under the
// tabs): drag to move or dock, double-click to float or dock. The "⋯"
// button, a right click and the keyboard (Right from the selected tab, then
// Space or Enter; the menu key or Shift+F10 on a tab) open the panel's menu,
// which Main.qml owns (Docking.requestMenu). On Wayland a floating panel's
// header moves its window through the compositor (startSystemMove), as the
// engine cannot place windows there.
//
// The header's focus ring (K2, visual-language.md "Keyboard focus") marks
// the group holding the keyboard focus. Loaded through the adapter's view
// factory (ui/docking.cpp).
KDDWViews.TabBarBase {
    id: root
    objectName: "dockHeader"

    readonly property int headerHeight: 35
    // The content starts 12 below a tab bar: this, then the Panel's margin.
    readonly property int contentGap: titleMode ? 0 : 8
    implicitHeight: headerHeight

    // Panel names per tab, read again when the tabs or the header
    // descriptions change.
    function nameAt(index: int): string {
        return root.tabBarCpp && index >= 0 && index < root.count ? Docking.dockNameAt(root.tabBarCpp, index) : ""
    }
    readonly property string currentName: (Docking.headerRevision, root.count, nameAt(root.currentTabIndex))
    readonly property Item currentTab: root.currentTabIndex >= 0 && root.currentTabIndex < tabRepeater.count
                                       ? tabRepeater.itemAt(root.currentTabIndex) : null
    readonly property string currentTitle: currentTab ? currentTab.text : ""
    readonly property bool titleMode: root.count === 1 && !(Docking.headerRevision, Docking.isHorizontal(nameAt(0)))
    readonly property bool floating: Window.window !== null && Window.window.transientParent !== null
    readonly property bool panelFocused: root.groupCpp !== null && root.groupCpp.titleBar !== null
                                         && root.groupCpp.titleBar.isFocused
    readonly property bool menuOpen: Docking.openMenu.length > 0 && Docking.openMenu === root.currentName

    Accessible.role: titleMode ? Accessible.TitleBar : Accessible.PageTabList
    Accessible.name: titleMode ? currentTitle : qsTr("Panels")

    // Called by the engine: the item of tab `index`, and the tab under a
    // point. A one-panel header is all its panel's, so a double-click or a
    // drag anywhere on it acts on the panel; between tabs it is the group's.
    function getTabAtIndex(index) {
        return tabRepeater.itemAt(index)
    }
    function getTabIndexAtPosition(globalPoint) {
        if (root.count === 1)
            return root.contains(root.mapFromGlobal(globalPoint.x, globalPoint.y)) ? 0 : -1
        for (let i = 0; i < tabRepeater.count; ++i) {
            const tab = tabRepeater.itemAt(i)
            if (tab && tab.contains(tab.mapFromGlobal(globalPoint.x, globalPoint.y)))
                return i
        }
        return -1
    }

    // The keys a header control takes itself: Left, Right, Space, Enter,
    // the menu key and Shift+F10.
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

    // The strip: the body's colour behind a title bar; `raised` behind tabs,
    // with the 1-pixel line under the tabs that are not selected.
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

    // A lone panel's title: bold, 12 from the left, up to the "⋯" button.
    Text {
        objectName: "dockTitleText"
        visible: root.titleMode
        x: 12
        width: Math.max(0, menuButton.x - x - 4)
        anchors.verticalCenter: parent.verticalCenter
        text: root.currentTitle
        color: Theme.text
        font.bold: true
        elide: Text.ElideRight
        Accessible.ignored: true // the header carries the name
    }

    // Tabs: 10 | label | 10, or 10 | label | 6 | ⋯ | 6 on the selected one,
    // then a 1-pixel line. When they do not fit, the selected tab keeps its
    // width and the others share what is left.
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
                readonly property int tabIndex: index // the engine looks for it
                readonly property string text: title // the engine reads it
                readonly property bool selected: index === root.currentTabIndex
                readonly property bool hovered: root.tabBarCpp !== null && root.tabBarCpp.hoveredTabIndex === index
                readonly property real naturalWidth: Math.ceil(10 + boldMetrics.advanceWidth
                                                               + (selected ? 6 + menuButton.width + 6 : 10) + 1)
                objectName: "dockTab" + index
                height: root.height
                width: {
                    const available = root.width - root.slotWidth
                    if (root.naturalTabsWidth <= available || selected)
                        return naturalWidth
                    const others = root.naturalTabsWidth - root.selectedNaturalWidth
                    const left = Math.max(0, available - root.selectedNaturalWidth)
                    return Math.max(24, Math.floor(naturalWidth * left / Math.max(1, others)))
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
                    // hovered: the body's colour at half strength
                    anchors.fill: parent
                    anchors.bottomMargin: 1
                    visible: tab.hovered && !tab.selected
                    color: Theme.field
                    opacity: 0.5
                }
                Text {
                    id: label
                    x: 10
                    width: Math.max(0, tab.width - x - (tab.selected ? 6 + menuButton.width + 6 : 10) - 1)
                    anchors.verticalCenter: parent.verticalCenter
                    text: tab.title
                    color: Theme.text
                    font.bold: tab.selected
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }
                Rectangle {
                    // the line after the tab
                    width: 1
                    height: parent.height - (tab.selected ? 0 : 1)
                    x: parent.width - 1
                    color: Theme.line
                }
                Rectangle {
                    // the line under a tab that is not selected
                    visible: !tab.selected
                    width: parent.width
                    height: 1
                    y: parent.height - 1
                    color: Theme.line
                }
                // K2: keyboard focus just inside the tab (visual-language.md;
                // the tab bar would clip a ring outside it).
                Rectangle {
                    objectName: "tabFocusRing"
                    anchors.fill: parent
                    anchors.margins: 2
                    anchors.rightMargin: tab.selected ? 6 + menuButton.width + 3 : 3
                    color: "transparent"
                    border.width: 2
                    border.color: Theme.focus
                    visible: tab.activeFocus
                }

                // The header's keys come before the shell's shortcuts (Left
                // and Right step frames, Space plays).
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

    // Above the engine's drag area (mouseAreaZ): the "⋯" button, on the
    // selected tab or at the title bar's end.
    DockMenuButton {
        id: menuButton
        z: root.mouseAreaZ + 1
        visible: root.count > 0 && (root.titleMode || root.currentTab !== null)
        panelTitle: root.currentTitle
        menuOpen: root.menuOpen
        x: root.titleMode ? root.width - 12 - width
                          : (root.currentTab ? tabRow.x + root.currentTab.x + root.currentTab.width - 1 - 6 - width : 0)
        anchors.verticalCenter: parent.verticalCenter
        // The header's one entry in the Tab order is its selected tab; a
        // title bar's is this button (kept while it has the focus: Qt
        // refuses to take the focused item out of the order).
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

    // The current panel's toolbar, right of the tabs (MuseScore's
    // toolbarComponent): the toolbar item moves here while its panel is the
    // current tab of this group, and leaves when it is not.
    Item {
        id: toolbarSlot
        objectName: "dockToolbarSlot"
        readonly property Item toolbar: !root.titleMode ? (Docking.headerRevision, Docking.toolbar(root.currentName)) : null
        property Item hosted: null
        z: root.mouseAreaZ + 1
        visible: toolbar !== null
        anchors.right: parent.right
        anchors.rightMargin: 4
        height: parent.height - 1
        width: toolbar ? Math.min(toolbar.implicitWidth, Math.max(0, root.width - root.selectedNaturalWidth - 12)) : 0
        function host() {
            if (hosted && hosted !== toolbar && hosted.parent === toolbarSlot)
                hosted.parent = null
            hosted = toolbar
            if (hosted) {
                hosted.parent = toolbarSlot
                hosted.anchors.fill = toolbarSlot
            }
        }
        onToolbarChanged: host()
        Component.onCompleted: host()
        Component.onDestruction: {
            if (hosted && hosted.parent === toolbarSlot)
                hosted.parent = null
        }
    }

    // A right click: the menu of the panel under the pointer.
    MouseArea {
        z: root.mouseAreaZ + 1
        anchors.fill: parent
        anchors.rightMargin: root.slotWidth
        acceptedButtons: Qt.RightButton
        onPressed: mouse => {
            const index = root.count === 1 ? 0 : root.getTabIndexAtPosition(mapToGlobal(mouse.x, mouse.y))
            if (index < 0)
                return
            root.currentTabIndex = index
            root.openMenu(root.nameAt(index), menuButton)
        }
    }

    // Wayland: a floating panel's header moves its window through the
    // compositor, anywhere on a lone panel's header, else between the tabs
    // (a tab still drags out of the group, as the engine does it).
    MouseArea {
        objectName: "dockSystemMoveArea"
        z: root.mouseAreaZ + 1
        enabled: root.floating && Docking.systemMove
        visible: enabled
        x: root.count === 1 ? 0 : tabRow.width
        width: Math.max(0, root.count === 1 ? menuButton.x - 2 : root.width - tabRow.width - root.slotWidth)
        height: parent.height
        onPressed: Window.window.startSystemMove()
        onDoubleClicked: Docking.toggleFloating(root.count === 1 ? root.nameAt(0) : root.currentName)
    }

    // K2: the group holding the keyboard focus rings its header, 2 wide just
    // inside it, in the focus role; a header control with the focus shows
    // its own ring instead.
    Rectangle {
        objectName: "focusRing"
        anchors.fill: parent
        anchors.margins: 1
        z: root.mouseAreaZ + 2
        color: "transparent"
        border.width: 2
        border.color: Theme.focus
        visible: root.panelFocused && !menuButton.activeFocus && !(root.currentTab && root.currentTab.activeFocus)
    }
}
