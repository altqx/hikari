import QtQuick
import QtQuick.Controls
import "qrc:/kddockwidgets/qtquick/views/qml/" as KDDW
import Hikari.Ui

// D1: the tabs of a panel group, the engine's own tab bar with each tab's
// Float and Close exposed as named buttons (docs/qt/docking.md: panel tabs
// expose names, selection and close/float actions to assistive technology).
// The tabs are page tabs (name and selection); the engine's drag area lies
// over them, so the buttons sit above it. Loaded through the adapter's view
// factory (ui/docking.cpp).
KDDW.TabBarBase {
    id: root
    objectName: "dockTabBar"

    // Called by the engine: the item of tab `index`.
    function getTabAtIndex(index) {
        return tabBar.itemAt(index)
    }
    function getTabIndexAtPosition(globalPoint) {
        for (let i = 0; i < tabBar.count; ++i) {
            const tab = tabBar.itemAt(i)
            if (tab && tab.contains(tab.mapFromGlobal(globalPoint.x, globalPoint.y)))
                return i
        }
        return tabBar.currentIndex
    }

    component TabAction: Rectangle {
        id: action
        signal clicked()
        property string name
        property string glyph
        width: 18
        height: 18
        radius: 3
        color: area.containsMouse ? Theme.select : "transparent" // K2
        Accessible.role: Accessible.Button
        Accessible.name: action.name
        Accessible.focusable: false
        Accessible.ignored: !root.visible
        Accessible.onPressAction: action.clicked()
        Text {
            anchors.centerIn: parent
            text: action.glyph
            color: Theme.text
            Accessible.ignored: true
        }
        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            onClicked: action.clicked()
        }
    }

    implicitHeight: tabBar.implicitHeight
    readonly property int actionsWidth: 40

    onCurrentTabIndexChanged: tabBar.currentIndex = root.currentTabIndex

    TabBar {
        id: tabBar
        width: parent.width
        position: (root.groupCpp && root.groupCpp.tabsAtBottom) ? TabBar.Footer : TabBar.Header
        Accessible.name: qsTr("Panels")
        onCurrentIndexChanged: root.currentTabIndex = currentIndex
        Connections {
            target: root.groupCpp
            function onCurrentIndexChanged() {
                root.currentTabIndex = root.groupCpp.currentIndex
            }
        }
        Repeater {
            model: root.groupCpp ? root.groupCpp.tabBar.dockWidgetModel : 0
            TabButton {
                required property int index
                required property string title
                readonly property int tabIndex: index
                objectName: "dockTab" + index
                text: title
                rightPadding: root.actionsWidth + 8
                Accessible.name: title
                // A single panel shows no tab bar (the title bar names it).
                Accessible.ignored: !root.visible
            }
        }
    }

    // Each tab's actions, over the engine's drag area (z above mouseAreaZ).
    Repeater {
        model: root.groupCpp ? root.groupCpp.tabBar.dockWidgetModel : 0
        Row {
            id: actions
            required property int index
            required property string title
            readonly property Item tab: tabBar.count > index ? tabBar.itemAt(index) : null
            z: root.mouseAreaZ + 1
            spacing: 2
            visible: tab !== null
            x: tab ? tabBar.x + tab.x + tab.width - width - 4 : 0
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined

            TabAction {
                objectName: "dockTabFloat" + actions.index
                name: qsTr("Float %1").arg(actions.title)
                glyph: "↗"
                onClicked: Docking.floatTab(root.tabBarCpp, actions.index)
            }
            TabAction {
                objectName: "dockTabClose" + actions.index
                name: qsTr("Close %1").arg(actions.title)
                glyph: "×"
                onClicked: root.tabBarCpp.closeAtIndex(actions.index)
            }
        }
    }
}
