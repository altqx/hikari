import QtQuick
import Hikari.Ui
import com.kdab.dockwidgets 2.0 as KDDW

// D3: a docked or floating panel group (the engine's Group.qml without its
// MDI resize handles), after MuseScore's DockFrame.qml
// (docs/research/musescore-docking.md §1): one header row (DockTabBar.qml,
// the title bar of a lone panel or the tabs), then the panel. The group is
// drawn in the panel body's colour, with no frame of its own: the 1-pixel
// separators between groups are the boundaries. Below a tab bar the content
// starts 12 lower (the header's contentGap and the Panel's own margin).
// Loaded through the adapter's view factory (ui/docking.cpp).
Rectangle {
    id: root
    objectName: "dockGroup"

    property KDDW.GroupView groupCpp
    readonly property QtObject titleBarCpp: groupCpp ? groupCpp.titleBar : null
    // Read by the engine: the group's height besides its panel.
    readonly property int nonContentsHeight: (titleBar.item && titleBar.item.visible ? titleBar.item.heightWhenVisible : 0)
                                             + (tabbar.item && tabbar.item.visible ? tabbar.item.implicitHeight : 0)
                                             + contentGap
    readonly property int contentGap: tabbar.item && tabbar.item.visible ? tabbar.item.contentGap : 0
    property alias tabBarHeight: tabbar.height
    readonly property bool hasCustomMouseEventRedirector: false
    readonly property bool isMDI: false
    readonly property bool tabsAtTop: true

    anchors.fill: parent
    color: Theme.field // the panel body (Panel draws the palette's base)

    onGroupCppChanged: {
        if (groupCpp)
            groupCpp.setStackLayout(stackLayout)
    }
    onNonContentsHeightChanged: {
        if (groupCpp)
            groupCpp.geometryUpdated()
    }

    // The engine's title bar: hidden while the tabs show, which with the
    // adapter's flags is always (DockTabBar.qml draws a lone panel's).
    Loader {
        id: titleBar
        readonly property QtObject titleBarCpp: root.titleBarCpp
        source: root.groupCpp ? KDDW.Singletons.widgetFactory.titleBarFilename() : ""
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
    }

    Loader {
        id: tabbar
        readonly property KDDW.GroupView groupCpp: root.groupCpp
        readonly property bool hasCustomMouseEventRedirector: root.hasCustomMouseEventRedirector
        source: groupCpp ? KDDW.Singletons.widgetFactory.tabbarFilename() : ""
        anchors {
            left: parent.left
            right: parent.right
            top: titleBar.item && titleBar.item.visible ? titleBar.bottom : parent.top
        }
    }

    Item {
        id: stackLayout
        anchors {
            left: parent.left
            right: parent.right
            top: tabbar.item && tabbar.item.visible ? tabbar.bottom : (titleBar.item && titleBar.item.visible ? titleBar.bottom : parent.top)
            bottom: parent.bottom
            topMargin: root.contentGap
        }
    }
}
