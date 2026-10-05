import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// P6: legacy Notebook's tab bar below the workspace. One tab per Document
// (the protected reference has its own tray). A click shows a tab; the
// active tab's close mark or a middle click closes a tab through the close
// review; "+" and a double click on the empty bar add a tab (both write the
// session, as legacy AddPage(true, true) did). K1: the close mark, the new tab
// button and the modified mark (legacy's "*" between the history step and the
// name) are the set's icons.
Item {
    id: bar
    objectName: "documentTabBar"
    required property var app
    signal closeRequested(int index)

    implicitHeight: row.implicitHeight
    Accessible.role: Accessible.PageTabList
    Accessible.name: qsTr("Open documents")

    TapHandler {
        // A double click where there is no tab (legacy i == -1).
        onDoubleTapped: bar.app.addPage(true)
    }
    TapHandler {
        // A right click where there is no tab: the tab menu for none (-1).
        acceptedButtons: Qt.RightButton
        onTapped: eventPoint => bar.openTabMenu(-1, bar, eventPoint.position.x, eventPoint.position.y)
    }

    // Legacy Notebook::ContextMenu on tab `index` (-1: none), at (x, y) in
    // `item`; its items are in DocumentTabMenu.qml.
    function openTabMenu(index, item, x, y) {
        tabMenu.openOn(index, item, x, y)
    }
    DocumentTabMenu {
        id: tabMenu
        app: bar.app
    }

    Flickable {
        anchors.fill: parent
        contentWidth: row.implicitWidth
        clip: true
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds

        RowLayout {
            id: row
            spacing: 2
            Repeater {
                model: bar.app.tabs
                delegate: AbstractButton {
                    id: tab
                    required property int index
                    required property var modelData
                    objectName: "documentTab" + index
                    checkable: false
                    checked: modelData.current
                    focusPolicy: Qt.NoFocus
                    text: modelData.label
                    Accessible.role: Accessible.PageTab
                    Accessible.name: modelData.title
                    Accessible.checked: modelData.current
                    Accessible.description: modelData.modified ? qsTr("Modified") : ""
                    // "<history step>*<name>" while modified: the step, the
                    // modified mark in place of the "*", the name.
                    readonly property int mark: modelData.modified ? modelData.label.indexOf("*") : -1
                    implicitHeight: label.implicitHeight + 10
                    implicitWidth: label.implicitWidth + 12 + (closeMark.visible ? closeMark.implicitWidth : 0)
                                   + (mark >= 0 ? step.implicitWidth + modifiedMark.width + 4 : 0)
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: modelData.tip
                    background: Rectangle {
                        color: tab.checked ? palette.base : (tab.hovered ? palette.midlight : palette.button)
                        border.color: tab.checked ? palette.highlight : palette.mid
                        radius: 2
                    }
                    contentItem: RowLayout {
                        spacing: 2
                        Label {
                            id: step
                            visible: tab.mark >= 0
                            text: tab.mark >= 0 ? tab.text.slice(0, tab.mark) : ""
                            font.bold: tab.checked
                            leftPadding: 4
                        }
                        Icon {
                            id: modifiedMark
                            objectName: "documentTabModified" + tab.index
                            visible: tab.mark >= 0
                            iconRole: "document-modified"
                        }
                        Label {
                            id: label
                            text: tab.mark >= 0 ? tab.text.slice(tab.mark + 1) : tab.text
                            font.bold: tab.checked
                            leftPadding: tab.mark >= 0 ? 0 : 4
                        }
                        IconToolButton {
                            id: closeMark
                            objectName: "documentTabClose" + tab.index
                            visible: tab.checked
                            iconRole: "tab-close"
                            text: qsTr("Close %1").arg(tab.modelData.title)
                            focusPolicy: Qt.NoFocus
                            padding: 1
                            implicitWidth: 18
                            implicitHeight: 18
                            onClicked: bar.closeRequested(tab.index)
                        }
                    }
                    onClicked: bar.app.selectTab(index)
                    TapHandler {
                        acceptedButtons: Qt.MiddleButton
                        onTapped: bar.closeRequested(tab.index)
                    }
                    TapHandler {
                        // The tab's own: the bar's handler does not see it.
                        acceptedButtons: Qt.RightButton
                        gesturePolicy: TapHandler.WithinBounds
                        onTapped: eventPoint => bar.openTabMenu(tab.index, tab, eventPoint.position.x, eventPoint.position.y)
                    }
                }
            }
            IconToolButton {
                objectName: "newTabButton"
                iconRole: "tab-new"
                text: qsTr("Open new tab")
                focusPolicy: Qt.NoFocus
                onClicked: bar.app.addPage(true)
            }
        }
    }
}
