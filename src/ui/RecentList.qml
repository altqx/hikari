pragma Translator: "HikariSub.Home"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Rectangle {
    id: recent
    required property HomePage page
    required property string prefix
    required property string title
    required property string emptyText
    required property string noMatchText
    required property string filterName
    required property string removeMissingText
    required property string iconRole
    required property string openIconRole
    property var rows: []
    property alias list: recentList
    property alias listFocus: recentList.focus
    readonly property real naturalHeight: header.implicitHeight + recentList.contentHeight + 4
                                          + (recentList.visible ? 0 : noRecent.implicitHeight + 24)

    signal chosen(var row)
    signal removeRequested(string path)
    signal removeMissingRequested()

    readonly property var shownRows: {
        const query = filterField.text.trim().toLocaleLowerCase()
        if (query.length === 0)
            return rows
        return rows.filter(r => r.name.toLocaleLowerCase().includes(query) || r.path.toLocaleLowerCase().includes(query))
    }
    readonly property bool anyMissing: rows.some(r => r.missing)

    function open(row) {
        if (row && !row.missing)
            recent.chosen(row)
    }
    function remove(row) {
        if (!row)
            return
        const index = recentList.currentIndex
        recent.removeRequested(row.path)
        recentList.currentIndex = Math.min(index, recentList.count - 1)
    }

    objectName: prefix + "Recent"
    color: Theme.panel
    border.color: Theme.line

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0
        ColumnLayout {
            id: header
            spacing: 0
            Layout.fillWidth: true
            RowLayout {
                Layout.fillWidth: true
                Layout.minimumHeight: 40
                Layout.leftMargin: 12
                Layout.rightMargin: 12
                spacing: 12
                Icon { iconRole: recent.iconRole; size: 20 }
                Label {
                    text: recent.title
                    font.bold: true
                    Layout.fillWidth: true
                }
                TextField {
                    id: filterField
                    objectName: recent.prefix + "Filter"
                    visible: recent.rows.length > 0
                    Layout.preferredWidth: 220
                    placeholderText: qsTr("Filter")
                    Accessible.name: recent.filterName
                    Keys.onDownPressed: recentList.forceActiveFocus(Qt.TabFocusReason)
                }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line }
            RowLayout {
                visible: recent.rows.length > 0
                Layout.fillWidth: true
                Layout.leftMargin: 15
                Layout.rightMargin: 12
                Layout.topMargin: 3
                Layout.bottomMargin: 3
                Label { text: qsTr("Name"); color: Theme.muted; font: recent.page.smallFont; Layout.fillWidth: true }
                Label { text: qsTr("Last opened"); color: Theme.muted; font: recent.page.smallFont }
            }
            Rectangle { visible: recent.rows.length > 0; Layout.fillWidth: true; implicitHeight: 1; color: Theme.line }
        }
        Label {
            id: noRecent
            objectName: recent.prefix + "NoRecent"
            visible: recent.shownRows.length === 0
            Layout.margins: 12
            color: Theme.muted
            text: recent.rows.length === 0 ? recent.emptyText : recent.noMatchText
        }
        Item { visible: !recentList.visible; Layout.fillHeight: true }
        ListView {
            id: recentList
            objectName: recent.prefix + "RecentList"
            visible: count > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            activeFocusOnTab: true
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: true
            highlightMoveDuration: 0
            model: recent.shownRows
            Accessible.role: Accessible.List
            Accessible.name: recent.title
            ScrollBar.vertical: ScrollBar { policy: recentList.contentHeight > recentList.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
            property bool keyboardFocus: false
            onActiveFocusChanged: {
                if (!activeFocus)
                    keyboardFocus = false
                else if (currentIndex < 0 && count > 0)
                    currentIndex = 0
            }
            MenuKey.onRequested: {
                if (currentItem)
                    rowMenu.show(recent.shownRows[currentIndex], currentItem, 12, currentItem.height)
            }
            Keys.onPressed: event => {
                const row = recent.shownRows[currentIndex]
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    recent.open(row)
                    event.accepted = true
                } else if (event.key === Qt.Key_Delete) {
                    recent.remove(row)
                    event.accepted = true
                } else if (event.key === Qt.Key_Home) {
                    currentIndex = 0
                    event.accepted = true
                } else if (event.key === Qt.Key_End) {
                    currentIndex = count - 1
                    event.accepted = true
                }
                if (event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab)
                    keyboardFocus = true
            }
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool current: ListView.isCurrentItem
                objectName: recent.prefix + "RecentRow" + index
                width: ListView.view.width
                implicitHeight: rowContent.implicitHeight + 10
                Accessible.role: Accessible.ListItem
                Accessible.name: modelData.name + ", " + modelData.path
                                 + (whenLabel.text.length > 0 ? ", " + qsTr("last opened %1").arg(whenLabel.text) : "")
                                 + (modelData.missing ? ", " + qsTr("missing") : "")
                Accessible.selected: current

                Rectangle {
                    anchors.fill: parent
                    color: row.current && recentList.activeFocus ? Theme.select
                         : hover.hovered ? Theme.raised : "transparent"
                }
                Rectangle {
                    visible: row.current && recentList.activeFocus
                    width: 3
                    height: parent.height
                    color: Theme.accent
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Theme.highContrast ? Theme.line : Theme.raised
                }
                Rectangle {
                    objectName: recent.prefix + "RowFocus"
                    anchors.fill: parent
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.focus
                    visible: row.current && recentList.activeFocus && recentList.keyboardFocus
                }
                HoverHandler { id: hover }
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: {
                        recentList.currentIndex = row.index
                        recentList.forceActiveFocus(Qt.MouseFocusReason)
                    }
                    onDoubleTapped: recent.open(row.modelData)
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: eventPoint => {
                        recentList.currentIndex = row.index
                        recentList.forceActiveFocus(Qt.MouseFocusReason)
                        rowMenu.show(row.modelData, row, eventPoint.position.x, eventPoint.position.y)
                    }
                }
                RowLayout {
                    id: rowContent
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 15
                    anchors.rightMargin: 12
                    spacing: 12
                    ColumnLayout {
                        spacing: 0
                        Layout.fillWidth: true
                        RowLayout {
                            spacing: 8
                            Layout.fillWidth: true
                            Label {
                                text: row.modelData.name
                                font.bold: !row.modelData.missing
                                color: row.modelData.missing ? Theme.muted : Theme.text
                                elide: Text.ElideRight
                                Layout.maximumWidth: Math.max(80, rowContent.width - 220)
                            }
                            Label {
                                objectName: recent.prefix + "Missing" + row.index
                                visible: row.modelData.missing
                                text: qsTr("Missing")
                                color: Theme.warning
                                font: recent.page.smallFont
                                leftPadding: 5
                                rightPadding: 5
                                background: Rectangle {
                                    color: "transparent"
                                    border.color: Theme.warning
                                    radius: 3
                                }
                            }
                            Item { Layout.fillWidth: true }
                        }
                        Label {
                            text: row.modelData.path
                            color: Theme.muted
                            font: recent.page.smallFont
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                    }
                    IconTextButton {
                        iconRole: "remove"
                        objectName: recent.prefix + "Remove" + row.index
                        visible: row.modelData.missing
                        text: qsTr("Remove")
                        focusPolicy: Qt.NoFocus
                        Accessible.description: row.modelData.name
                        onClicked: recent.remove(row.modelData)
                    }
                    IconButton {
                        objectName: recent.prefix + "ShowInFolder" + row.index
                        visible: !row.modelData.missing && hover.hovered
                        iconRole: "show-in-folder"
                        text: qsTr("Show in folder")
                        flat: true
                        focusPolicy: Qt.NoFocus
                        onClicked: recent.page.app.showInFolder(row.modelData.path)
                    }
                    Label {
                        id: whenLabel
                        objectName: recent.prefix + "Opened" + row.index
                        text: recent.page.openedText(row.modelData.opened)
                        color: Theme.muted
                        font.features: { "tnum": 1 }
                        horizontalAlignment: Text.AlignRight
                        Layout.preferredWidth: Math.max(implicitWidth, 120)
                    }
                }
            }
        }
    }

    ShellMenu {
        id: rowMenu
        objectName: recent.prefix + "RowMenu"
        property var row: null
        function show(row, item, x, y) {
            rowMenu.row = row
            rowMenu.popup(item, x, y)
        }
        ShellMenuItem {
            objectName: recent.prefix + "MenuOpen"
            iconRole: recent.openIconRole
            text: qsTr("Open")
            shortcutText: ShellMenus.keyText("Enter")
            enabled: rowMenu.row !== null && !rowMenu.row.missing
            onTriggered: recent.open(rowMenu.row)
        }
        ShellMenuItem {
            objectName: recent.prefix + "MenuShowInFolder"
            iconRole: "show-in-folder"
            text: qsTr("Show in folder")
            onTriggered: recent.page.app.showInFolder(rowMenu.row.path)
        }
        ShellMenuItem {
            objectName: recent.prefix + "MenuCopyPath"
            iconRole: "copy-path"
            text: qsTr("Copy path")
            onTriggered: recent.page.app.copyText(rowMenu.row.path)
        }
        MenuSeparator {}
        ShellMenuItem {
            objectName: recent.prefix + "MenuRemove"
            iconRole: "remove"
            text: qsTr("Remove from list")
            shortcutText: ShellMenus.keyText("Del")
            onTriggered: recent.remove(rowMenu.row)
        }
        ShellMenuItem {
            objectName: recent.prefix + "MenuRemoveMissing"
            iconRole: "remove"
            text: recent.removeMissingText
            enabled: recent.anyMissing
            onTriggered: recent.removeMissingRequested()
        }
    }
}
