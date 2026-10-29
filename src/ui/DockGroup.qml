import QtQuick
import Hikari.Ui
import com.kdab.dockwidgets 2.0 as KDDW

Rectangle {
    id: root
    objectName: "dockGroup"

    property KDDW.GroupView groupCpp
    readonly property QtObject titleBarCpp: groupCpp ? groupCpp.titleBar : null
    // Read by KDDW: the group's height besides its panel.
    readonly property int nonContentsHeight: (titleBar.item && titleBar.item.visible ? titleBar.item.heightWhenVisible : 0)
                                             + (tabbar.item && tabbar.item.visible ? tabbar.item.implicitHeight : 0)
                                             + contentGap
    readonly property int contentGap: tabbar.item && tabbar.item.visible ? tabbar.item.contentGap : 0
    property alias tabBarHeight: tabbar.height
    readonly property bool hasCustomMouseEventRedirector: false
    readonly property bool isMDI: false
    readonly property bool tabsAtTop: true

    anchors.fill: parent
    color: Theme.field

    onGroupCppChanged: {
        if (groupCpp)
            groupCpp.setStackLayout(stackLayout)
    }
    onNonContentsHeightChanged: {
        if (groupCpp)
            groupCpp.geometryUpdated()
    }

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
