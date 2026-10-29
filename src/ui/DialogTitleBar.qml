pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

Rectangle {
    id: bar
    objectName: "dialogTitleBar"
    required property DialogFrame frame
    readonly property Window window: frame.window
    readonly property bool active: window !== null && window.active
    readonly property bool maximized: window !== null
        && (window.visibility === Window.Maximized || window.visibility === Window.FullScreen)
    property string title: window ? window.title : ""
    property bool closable: true
    property string guideId
    visible: frame.custom
    implicitHeight: visible ? 32 : 0
    implicitWidth: titleLabel.implicitWidth + 12 + 24 + buttons.implicitWidth
    color: Theme.panel
    Accessible.role: Accessible.TitleBar
    Accessible.name: title

    Component.onCompleted: {
        frame.titleBar = bar
        frame.moveArea = moveArea
        frame.maximizeButton = maximizeButton
    }

    TitleBarMoveArea {
        id: moveArea
        objectName: "dialogTitleBarMoveArea"
        anchors.fill: parent
        frame: bar.frame
    }

    Label {
        id: titleLabel
        objectName: "dialogTitleBarTitle"
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.right: buttons.left
        anchors.rightMargin: 12
        height: parent.height
        text: bar.title
        textFormat: Text.PlainText
        elide: Text.ElideRight
        maximumLineCount: 1
        verticalAlignment: Text.AlignVCenter
        color: bar.active ? Theme.text : Theme.disabled
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }

    Row {
        id: buttons
        objectName: "dialogTitleBarButtons"
        anchors.right: parent.right
        anchors.top: parent.top
        TitleBarButton {
            objectName: "dialogTitleBarHelp"
            visible: UserGuide.hasPage("window", bar.guideId)
            active: bar.active
            iconRole: "help"
            text: qsTr("Help")
            onClicked: UserGuide.open("window", bar.guideId)
        }
        // Windows snap layouts need HTMAXBUTTON from WM_NCHITTEST, so the frame reports hover and press.
        TitleBarButton {
            id: maximizeButton
            objectName: "dialogTitleBarMaximize"
            visible: bar.frame.resizable
            active: bar.active
            iconRole: bar.maximized ? "window-restore" : "window-maximize"
            text: bar.maximized ? qsTr("Restore") : qsTr("Maximise")
            shownHover: hovered || bar.frame.maximizeHovered
            shownDown: down || bar.frame.maximizePressed
            onClicked: bar.frame.toggleMaximized()
        }
        TitleBarButton {
            objectName: "dialogTitleBarClose"
            visible: bar.closable
            active: bar.active
            iconRole: "window-close"
            text: qsTr("Close")
            onClicked: bar.window.close()
        }
    }

    Rectangle {
        visible: Theme.highContrast
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.line
    }
}
