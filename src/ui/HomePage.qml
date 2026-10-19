pragma Translator: "HikariSub.Home"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// SH2 (#221): the Home page, in the main area while no Document is open (at
// start without subtitles, after the last tab closed). The design the owner
// approved on 2026-10-19 (docs/research/musescore-shell.md §7, SH2): New,
// Open… and Open video…; the last Session with Restore when it has a
// Document; the recent subtitles with their last-opened time, a missing file
// kept, marked and removable; and a column for SH3's What's new. There is no
// Home tab: the page goes when a Document opens.
FocusScope {
    id: home
    required property var app
    // SH3 (#223): What's new, shown in the right-hand column when set.
    property Component whatsNew: null
    // The height of the empty space right above the page (the Document tab
    // bar, kept while Home shows).
    property real spaceAbove: 0
    // The clock "Today" and "Yesterday" count from; tests set it.
    property date now: new Date()

    signal newRequested()
    signal openRequested()
    signal openVideoRequested()
    signal restoreRequested()
    signal recentChosen(string path)

    property var rows: []
    property var sessionNames: []
    readonly property var shownRows: {
        const query = filterField.text.trim().toLocaleLowerCase()
        if (query.length === 0)
            return rows
        return rows.filter(r => r.name.toLocaleLowerCase().includes(query) || r.path.toLocaleLowerCase().includes(query))
    }
    readonly property bool anyMissing: rows.some(r => r.missing)

    function reload() {
        rows = app.homeRecent()
        sessionNames = app.lastSessionSummary().names
        now = new Date()
    }
    // The list first when it has a file to open, else New.
    function takeFocus() {
        if (recentList.count > 0)
            recentList.forceActiveFocus(Qt.OtherFocusReason)
        else
            newButton.forceActiveFocus(Qt.OtherFocusReason)
    }
    function openedText(opened) {
        if (!opened || isNaN(opened.getTime()))
            return ""
        const day = d => new Date(d.getFullYear(), d.getMonth(), d.getDate()).getTime()
        const days = Math.round((day(home.now) - day(opened)) / 86400000)
        const time = opened.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
        if (days === 0)
            return qsTr("Today %1").arg(time)
        if (days === 1)
            return qsTr("Yesterday %1").arg(time)
        return opened.toLocaleDateString(Qt.locale(), Locale.ShortFormat)
    }
    function open(row) {
        if (row && !row.missing)
            home.recentChosen(row.path)
    }
    function remove(row) {
        if (!row)
            return
        const index = recentList.currentIndex
        home.app.removeRecent(row.path)
        recentList.currentIndex = Math.min(index, recentList.count - 1)
    }

    // The day turns while Home shows: "Today" and "Yesterday" move on at
    // the next local midnight (a timer that fires early is set again).
    Timer {
        objectName: "homeMidnight"
        running: home.visible
        repeat: true
        interval: {
            const n = home.now
            const next = new Date(n.getFullYear(), n.getMonth(), n.getDate() + 1)
            return Math.max(1000, next.getTime() - n.getTime() + 1000)
        }
        onTriggered: home.now = new Date()
    }

    // Later: a close writes the session after the tabs change.
    onVisibleChanged: if (visible) Qt.callLater(reload)
    Component.onCompleted: reload()
    Connections {
        target: home.app
        function onRecentChanged() { home.rows = home.app.homeRecent() }
    }

    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Home")

    Rectangle {
        anchors.fill: parent
        color: Theme.background
    }

    // The two columns stack below this width; What's new goes under the list.
    readonly property bool narrow: width < 1000

    ColumnLayout {
        id: page
        anchors.fill: parent
        anchors.margins: 24
        // The empty tab bar above counts toward the top margin.
        anchors.topMargin: Math.max(0, 24 - home.spaceAbove)
        anchors.leftMargin: Math.max(24, (home.width - 1120) / 2)
        anchors.rightMargin: anchors.leftMargin
        spacing: 16

        RowLayout {
            spacing: 8
            Button {
                id: newButton
                objectName: "homeNew"
                text: qsTr("New")
                // The page's primary action, filled with the accent (Fusion's
                // highlighted button only tints its outline).
                palette.button: Theme.accent
                palette.buttonText: Theme.onAccent
                icon.source: IconTheme.image("tab-new", Theme.onAccent, Theme.onAccent, false)
                icon.color: "transparent"
                ToolTip.visible: hovered
                ToolTip.text: qsTr("New subtitles in a new tab")
                onClicked: home.newRequested()
            }
            IconTextButton {
                objectName: "homeOpen"
                iconRole: "open-subtitles"
                text: qsTr("Open…")
                onClicked: home.openRequested()
            }
            IconTextButton {
                objectName: "homeOpenVideo"
                iconRole: "open-video"
                text: qsTr("Open video…")
                onClicked: home.openVideoRequested()
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: home.narrow ? 1 : 2
            columnSpacing: 16
            rowSpacing: 16

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 16

                // The last Session, when it has a Document to restore.
                Rectangle {
                    objectName: "homeSession"
                    visible: home.sessionNames.length > 0
                    Layout.fillWidth: true
                    implicitHeight: sessionRow.implicitHeight + 20
                    color: Theme.panel
                    border.color: Theme.line
                    Accessible.role: Accessible.Grouping
                    Accessible.name: sessionTitle.text
                    RowLayout {
                        id: sessionRow
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 12
                        Icon { iconRole: "last-session"; size: 20 }
                        ColumnLayout {
                            spacing: 0
                            Layout.fillWidth: true
                            Label {
                                id: sessionTitle
                                text: qsTr("Last session")
                                font.bold: true
                            }
                            Label {
                                objectName: "homeSessionDocuments"
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                color: Theme.muted
                                // English has no plural catalog: one says "1 document".
                                text: (home.sessionNames.length === 1 ? qsTr("1 document")
                                                                      : qsTr("%n documents", "", home.sessionNames.length))
                                      + " · " + home.sessionNames.join(", ")
                            }
                        }
                        Button {
                            objectName: "homeRestore"
                            text: qsTr("Restore")
                            Accessible.description: sessionTitle.text
                            onClicked: home.restoreRequested()
                        }
                    }
                }

                Rectangle {
                    objectName: "homeRecent"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 160
                    color: Theme.panel
                    border.color: Theme.line

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 1
                        spacing: 0
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.minimumHeight: 40
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            spacing: 12
                            Label {
                                text: qsTr("Recent files")
                                font.bold: true
                                Layout.fillWidth: true
                            }
                            TextField {
                                id: filterField
                                objectName: "homeFilter"
                                visible: home.rows.length > 0
                                Layout.preferredWidth: 220
                                placeholderText: qsTr("Filter")
                                Accessible.name: qsTr("Filter recent files")
                                Keys.onDownPressed: recentList.forceActiveFocus(Qt.TabFocusReason)
                            }
                        }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line }
                        RowLayout {
                            visible: home.rows.length > 0
                            Layout.fillWidth: true
                            Layout.leftMargin: 15
                            Layout.rightMargin: 12
                            Layout.topMargin: 3
                            Layout.bottomMargin: 3
                            Label { text: qsTr("Name"); color: Theme.muted; font: home.smallFont; Layout.fillWidth: true }
                            Label { text: qsTr("Last opened"); color: Theme.muted; font: home.smallFont }
                        }
                        Rectangle { visible: home.rows.length > 0; Layout.fillWidth: true; implicitHeight: 1; color: Theme.line }
                        Label {
                            objectName: "homeNoRecent"
                            visible: home.shownRows.length === 0
                            Layout.margins: 12
                            color: Theme.muted
                            text: home.rows.length === 0 ? qsTr("No recent files") : qsTr("No matching files")
                        }
                        ListView {
                            id: recentList
                            objectName: "homeRecentList"
                            visible: count > 0
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            focus: true
                            activeFocusOnTab: true
                            boundsBehavior: Flickable.StopAtBounds
                            keyNavigationEnabled: true
                            highlightMoveDuration: 0
                            model: home.shownRows
                            Accessible.role: Accessible.List
                            Accessible.name: qsTr("Recent files")
                            ScrollBar.vertical: ScrollBar { policy: recentList.contentHeight > recentList.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
                            // The ring shows while the keyboard has the list (the Grid's row rule).
                            property bool keyboardFocus: false
                            onActiveFocusChanged: {
                                if (!activeFocus)
                                    keyboardFocus = false
                                else if (currentIndex < 0 && count > 0)
                                    currentIndex = 0
                            }
                            Keys.onPressed: event => {
                                const row = home.shownRows[currentIndex]
                                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                    home.open(row)
                                    event.accepted = true
                                } else if (event.key === Qt.Key_Delete) {
                                    home.remove(row)
                                    event.accepted = true
                                } else if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && event.modifiers & Qt.ShiftModifier)) {
                                    const item = currentItem
                                    if (item)
                                        rowMenu.show(row, item, 12, item.height)
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
                                objectName: "homeRecentRow" + index
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
                                Rectangle { // the selected row's leading marker
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
                                Rectangle { // keyboard focus: 1 wide, inside the row, in the text colour
                                    objectName: "homeRowFocus"
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
                                    onDoubleTapped: home.open(row.modelData)
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
                                                objectName: "homeMissing" + row.index
                                                visible: row.modelData.missing
                                                text: qsTr("Missing")
                                                color: Theme.warning
                                                font: home.smallFont
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
                                            font: home.smallFont
                                            elide: Text.ElideMiddle
                                            Layout.fillWidth: true
                                        }
                                    }
                                    Button {
                                        objectName: "homeRemove" + row.index
                                        visible: row.modelData.missing
                                        text: qsTr("Remove")
                                        focusPolicy: Qt.NoFocus // Delete removes from the keyboard
                                        Accessible.description: row.modelData.name
                                        onClicked: home.remove(row.modelData)
                                    }
                                    IconButton {
                                        objectName: "homeShowInFolder" + row.index
                                        visible: !row.modelData.missing && hover.hovered
                                        iconRole: "show-in-folder"
                                        text: qsTr("Show in folder")
                                        flat: true
                                        focusPolicy: Qt.NoFocus // the context menu has it for the keyboard
                                        onClicked: home.app.showInFolder(row.modelData.path)
                                    }
                                    Label {
                                        id: whenLabel
                                        objectName: "homeOpened" + row.index
                                        text: home.openedText(row.modelData.opened)
                                        color: Theme.muted
                                        font.features: { "tnum": 1 }
                                        horizontalAlignment: Text.AlignRight
                                        Layout.preferredWidth: Math.max(implicitWidth, 120)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // SH3's What's new; nothing shows until it fills the slot.
            Loader {
                objectName: "homeWhatsNew"
                active: home.whatsNew !== null
                visible: active
                sourceComponent: home.whatsNew
                Layout.preferredWidth: home.narrow ? -1 : 320
                Layout.fillWidth: home.narrow
                Layout.fillHeight: !home.narrow
                Layout.preferredHeight: home.narrow ? 200 : -1
            }
        }
    }

    // The muted secondary text: 11 against the 13 body (visual-language.md).
    readonly property font smallFont: {
        const base = fontSource.font
        const f = Qt.font({family: base.family})
        if (base.pointSize > 0)
            f.pointSize = Math.max(base.pointSize * 11 / 13, 8)
        else
            f.pixelSize = Math.max(Math.round(base.pixelSize * 11 / 13), 11)
        return f
    }
    Label {
        id: fontSource
        visible: false
    }

    ShellMenu {
        id: rowMenu
        objectName: "homeRowMenu"
        property var row: null
        function show(row, item, x, y) {
            rowMenu.row = row
            rowMenu.popup(item, x, y)
        }
        ShellMenuItem {
            objectName: "homeMenuOpen"
            iconRole: "open-subtitles"
            text: qsTr("Open") + "\tEnter"
            enabled: rowMenu.row !== null && !rowMenu.row.missing
            onTriggered: home.open(rowMenu.row)
        }
        ShellMenuItem {
            objectName: "homeMenuShowInFolder"
            iconRole: "show-in-folder"
            text: qsTr("Show in folder")
            onTriggered: home.app.showInFolder(rowMenu.row.path)
        }
        ShellMenuItem {
            objectName: "homeMenuCopyPath"
            iconRole: "edit-copy"
            text: qsTr("Copy path")
            onTriggered: home.app.copyText(rowMenu.row.path)
        }
        MenuSeparator {}
        ShellMenuItem {
            objectName: "homeMenuRemove"
            iconRole: "remove"
            text: qsTr("Remove from list") + "\tDel"
            onTriggered: home.remove(rowMenu.row)
        }
        ShellMenuItem {
            objectName: "homeMenuRemoveMissing"
            iconRole: "clear"
            text: qsTr("Remove missing files")
            enabled: home.anyMissing
            onTriggered: home.app.removeMissingRecent()
        }
    }
}
