import QtQuick
import "qrc:/kddockwidgets/qtquick/views/qml/" as KDDW

// D1: a panel's title bar (docked group or floating window), the engine's
// classic look with its buttons named for assistive technology
// (docs/qt/docking.md: panels expose Float, Dock and Close). The engine's
// default title bar gives its image buttons no role or name. Loaded through
// the adapter's view factory (ui/docking.cpp).
KDDW.TitleBarBase {
    id: root
    objectName: "dockTitleBar"

    // The application palette's colours (as the panels'), so the chrome
    // follows the light or dark appearance.
    color: root.palette.window
    heightWhenVisible: 30

    Accessible.role: Accessible.TitleBar
    Accessible.name: root.title

    // A docked group of tabs: its buttons act on the whole group (each tab
    // has its own Float and Close in DockTabBar.qml). The engine's Loader
    // sits in its Group.qml, which holds the group.
    readonly property var group: parent && parent.parent ? parent.parent.groupCpp : null
    readonly property bool tabbed: group ? group.tabBar.dockWidgetModel.count > 1 : false
    function buttonName(action: string, tabbedAction: string): string {
        return root.tabbed ? tabbedAction : action.arg(root.title)
    }

    // The float button docks a floating window back. The engine retitles its
    // tooltip when that changes, so the binding reads it to re-evaluate.
    readonly property bool floating: titleBarCpp !== null && titleBarCpp.floatButtonToolTip !== undefined
                                     && titleBarCpp.isFloating()

    function imagePath(id: string): string {
        // Qt's @Nx does not cover fractional ratios: the engine ships its own.
        const ratio = Screen.devicePixelRatio
        const suffix = ratio === 1.5 ? "-1.5x" : ratio === 2 ? "-2x" : ""
        return "qrc:/img/" + id + suffix + ".png"
    }

    component Button: Rectangle {
        id: button
        signal clicked()
        property alias imageSource: image.source
        property string name
        color: "transparent"
        height: image.implicitHeight + 5
        width: image.implicitWidth + 5
        radius: 3
        border.color: root.palette.mid
        border.width: mouseArea.containsMouse ? 1 : 0
        Accessible.role: Accessible.Button
        Accessible.name: button.name
        Accessible.focusable: false
        Accessible.ignored: !button.visible
        // A disabled button (a panel that cannot be closed) does nothing.
        Accessible.onPressAction: if (button.enabled) button.clicked()
        Image {
            id: image
            anchors.centerIn: parent
            anchors.verticalCenterOffset: 1
            anchors.horizontalCenterOffset: 1
        }
        MouseArea {
            id: mouseArea
            hoverEnabled: true
            anchors.fill: parent
            onClicked: button.clicked()
        }
    }

    Text {
        text: root.title
        color: root.palette.windowText
        anchors.left: parent ? parent.left : undefined
        anchors.leftMargin: 5
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        Accessible.ignored: true // the title bar carries the name
    }

    Row {
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        anchors.right: parent ? parent.right : undefined
        anchors.rightMargin: 2

        Button {
            objectName: "dockMinimizeButton"
            visible: root.minimizeButtonVisible
            imageSource: root.imagePath("min")
            name: root.buttonName(qsTr("Minimize %1"), qsTr("Minimize tab group"))
            onClicked: root.minimizeButtonClicked()
        }
        Button {
            objectName: "dockFloatButton"
            visible: root.floatButtonVisible
            imageSource: root.imagePath("dock-float")
            name: root.floating ? root.buttonName(qsTr("Dock %1"), qsTr("Dock tab group"))
                                : root.buttonName(qsTr("Float %1"), qsTr("Float tab group"))
            onClicked: root.floatButtonClicked()
        }
        Button {
            objectName: "dockMaximizeButton"
            visible: root.maximizeButtonVisible
            imageSource: root.maximizeUsesRestoreIcon ? root.imagePath("dock-float") : root.imagePath("max")
            name: root.maximizeUsesRestoreIcon ? root.buttonName(qsTr("Restore %1"), qsTr("Restore tab group"))
                                               : root.buttonName(qsTr("Maximize %1"), qsTr("Maximize tab group"))
            onClicked: root.maximizeButtonClicked()
        }
        Button {
            objectName: "dockCloseButton"
            // A panel that cannot be closed (D2: the Video panel in the
            // player layout) shows no Close: the engine's image does not dim.
            visible: root.closeButtonEnabled
            enabled: root.closeButtonEnabled
            imageSource: root.imagePath("close")
            name: root.buttonName(qsTr("Close %1"), qsTr("Close tab group"))
            onClicked: root.closeButtonClicked()
        }
    }
}
