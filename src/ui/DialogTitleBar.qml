pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

// DG1 (#222): a dialog's title bar (DialogWindow.qml) in SH1's style: one
// 32-pixel row on `panel`, the title at the leading edge (the owner's
// answer of 2026-10-21) and the window buttons at the trailing one: Close,
// and Maximise (or Restore) only while the dialog can be resized; no
// Minimise and no application icon. DX1 (#367): before them, "?" (Help)
// opens the window's page of the user guide, on windows that have one;
// User guide's key (F1) opens it from the keyboard. Its free parts move the
// window, a double-click maximises a resizable one, a right click opens the window
// system's window menu (TitleBarMoveArea). While the window is not active
// the title and the glyphs take the disabled role. In the high-contrast
// themes a 1-pixel `line` parts the row from the body. Drawn only while the
// dialog has HikariSub's frame (DialogFrame.custom); with "Use system title
// bar" the window system's frame stays and there is no row.
Rectangle {
    id: bar
    objectName: "dialogTitleBar"
    required property DialogFrame frame
    readonly property Window window: frame.window
    readonly property bool active: window !== null && window.active
    readonly property bool maximized: window !== null
        && (window.visibility === Window.Maximized || window.visibility === Window.FullScreen)
    property string title: window ? window.title : ""
    // PU7 (#309): false for a window the user cannot close (Install update).
    property bool closable: true
    // DX1 (#367): the window's page of the user guide (DialogWindow.guideId);
    // with one, a "?" button before the window buttons opens it.
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
        textFormat: Text.PlainText // as a system title bar shows it
        elide: Text.ElideRight
        maximumLineCount: 1
        verticalAlignment: Text.AlignVCenter
        color: bar.active ? Theme.text : Theme.disabled
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }

    // On Windows the maximise button is the frame's (WM_NCHITTEST's
    // HTMAXBUTTON, the snap layouts), as on the main window's bar.
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
