pragma Translator: "HikariSub.Home"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

FocusScope {
    id: home
    required property var app
    property Component whatsNew: null
    property real spaceAbove: 0
    property StartupOffer offer: null
    readonly property bool offerShown: offer !== null && offer.active
    property date now: new Date()

    signal newRequested()
    signal openRequested()
    signal openVideoRequested()
    signal restoreRequested()
    signal autosavesRequested()
    signal recentChosen(string path)
    signal projectChosen(string path)
    signal openFolderRequested()

    property int shellWindow: 0
    property var rows: []
    property var projectRows: []
    property var sessionNames: []
    property string projectName: ""
    property var lastTabs: []

    function reload() {
        rows = app.homeRecent()
        projectRows = app.homeRecentProjects()
        sessionNames = app.lastSessionSummary().names
        reloadProject()
        now = new Date()
    }
    function reloadProject() {
        projectName = app.projectName(shellWindow)
        lastTabs = app.projectLastTabs(shellWindow).names
    }
    function takeFocus() {
        const list = recentProjects.list.count > 0 ? recentProjects.list : recentFiles.list
        if (list.count > 0)
            list.forceActiveFocus(Qt.OtherFocusReason)
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

    // MUST KILL: reload waits a callLater for the closing tab's session write
    onVisibleChanged: if (visible) Qt.callLater(reload)
    Component.onCompleted: reload()
    Connections {
        target: home.app
        function onRecentChanged() { home.rows = home.app.homeRecent() }
        function onRecentProjectsChanged() { home.projectRows = home.app.homeRecentProjects() }
        function onProjectsChanged() { home.reloadProject() }
    }

    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Home")

    Rectangle {
        anchors.fill: parent
        color: Theme.background
    }

    readonly property bool narrow: width < 1000

    ColumnLayout {
        id: page
        anchors.fill: parent
        anchors.margins: 24
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
                // Fusion's highlighted button only tints its outline.
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
                objectName: "homeOpenFolder"
                iconRole: "open-project"
                text: qsTr("Open folder…")
                onClicked: home.openFolderRequested()
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

                Rectangle {
                    id: offerCard
                    objectName: "homeOffer"
                    visible: home.offerShown
                    readonly property bool crash: home.offerShown && home.offer.kind === "crash"
                    Layout.fillWidth: true
                    implicitHeight: offerRow.implicitHeight + 28
                    color: Theme.panel
                    border.color: crash ? Theme.warning : Theme.line
                    Accessible.role: Accessible.Grouping
                    Accessible.name: offerTitle.text
                    RowLayout {
                        id: offerRow
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 8
                        anchors.topMargin: 14
                        anchors.bottomMargin: 14
                        spacing: 14
                        Icon {
                            iconRole: offerCard.crash ? "notice-warning" : "last-session"
                            color: offerCard.crash ? Theme.warning : IconTheme.normal
                            accentColor: offerCard.crash ? Theme.warning : IconTheme.accent
                            size: 32
                            Layout.alignment: Qt.AlignTop
                        }
                        ColumnLayout {
                            spacing: 4
                            Layout.fillWidth: true
                            Label {
                                id: offerTitle
                                objectName: "homeOfferTitle"
                                text: home.offerShown ? home.offer.title : ""
                                font.bold: true
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                            }
                            Label {
                                objectName: "homeOfferDetail"
                                text: home.offerShown ? home.offer.detail : ""
                                color: Theme.muted
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                            RowLayout {
                                spacing: 8
                                Layout.topMargin: 6
                                PrimaryButton {
                                    objectName: "homeOfferRestore"
                                    visible: home.offerShown && home.offer.restorable
                                    text: offerCard.crash ? qsTr("Restore session") : qsTr("Restore")
                                    Accessible.description: offerTitle.text
                                    onClicked: home.offer.restore()
                                }
                                IconTextButton {
                                    iconRole: "autosave"
                                    objectName: "homeOfferAutosaves"
                                    visible: offerCard.crash && home.offer.autosaveCount > 0
                                    text: qsTr("Autosaves…")
                                    onClicked: home.autosavesRequested()
                                }
                                CheckBox {
                                    objectName: "homeOfferAlways"
                                    visible: home.offerShown && home.offer.kind === "ask"
                                    text: qsTr("Restore it at every start")
                                    onToggled: home.offer.setRestoreAlways(checked)
                                }
                            }
                        }
                        IconToolButton {
                            objectName: "homeOfferDismiss"
                            visible: offerCard.crash
                            iconRole: "tab-close"
                            text: qsTr("Dismiss")
                            Layout.alignment: Qt.AlignTop
                            onClicked: home.offer.end()
                        }
                    }
                }

                Rectangle {
                    objectName: "homeSession"
                    visible: home.sessionNames.length > 0 && !home.offerShown
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
                                // English has no plural catalog, so the singular is its own string.
                                text: (home.sessionNames.length === 1 ? qsTr("1 document")
                                                                      : qsTr("%n documents", "", home.sessionNames.length))
                                      + " · " + home.sessionNames.join(", ")
                            }
                        }
                        IconTextButton {
                            iconRole: "last-session"
                            objectName: "homeRestore"
                            text: qsTr("Restore")
                            Accessible.description: sessionTitle.text
                            onClicked: home.restoreRequested()
                        }
                    }
                }

                Rectangle {
                    objectName: "homeProject"
                    visible: home.projectName.length > 0
                    Layout.fillWidth: true
                    implicitHeight: projectRow.implicitHeight + 20
                    color: Theme.panel
                    border.color: Theme.line
                    Accessible.role: Accessible.Grouping
                    Accessible.name: projectTitle.text
                    RowLayout {
                        id: projectRow
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 12
                        Icon { iconRole: "project"; size: 20 }
                        ColumnLayout {
                            spacing: 0
                            Layout.fillWidth: true
                            Label {
                                id: projectTitle
                                objectName: "homeProjectName"
                                text: home.projectName
                                font.bold: true
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            Label {
                                objectName: "homeProjectTabs"
                                visible: home.lastTabs.length > 0
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                color: Theme.muted
                                text: home.lastTabs.join(", ")
                            }
                        }
                        IconTextButton {
                            iconRole: "last-session"
                            objectName: "homeReopenTabs"
                            visible: home.lastTabs.length > 0
                            text: qsTr("Reopen last tabs (%1)").arg(home.lastTabs.length)
                            Accessible.description: projectTitle.text
                            onClicked: home.app.reopenProjectTabs(home.shellWindow)
                        }
                    }
                }

                RecentList {
                    id: recentProjects
                    page: home
                    prefix: "homeProjects"
                    visible: rows.length > 0
                    rows: home.projectRows
                    title: qsTr("Recent projects")
                    emptyText: qsTr("No recent projects")
                    noMatchText: qsTr("No matching projects")
                    filterName: qsTr("Filter recent projects")
                    removeMissingText: qsTr("Remove missing folders")
                    iconRole: "recent-projects"
                    openIconRole: "open-project"
                    Layout.fillWidth: true
                    Layout.minimumHeight: 120
                    Layout.preferredHeight: Math.min(naturalHeight, 260)
                    onChosen: row => home.projectChosen(row.path)
                    onRemoveRequested: path => home.app.removeRecentProject(path)
                    onRemoveMissingRequested: home.app.removeMissingRecentProjects()
                }

                RecentList {
                    id: recentFiles
                    page: home
                    prefix: "home"
                    listFocus: true
                    rows: home.rows
                    title: qsTr("Recent files")
                    emptyText: qsTr("No recent files")
                    noMatchText: qsTr("No matching files")
                    filterName: qsTr("Filter recent files")
                    removeMissingText: qsTr("Remove missing files")
                    iconRole: "recent-subtitles"
                    openIconRole: "open-subtitles"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 160
                    onChosen: row => home.recentChosen(row.path)
                    onRemoveRequested: path => home.app.removeRecent(path)
                    onRemoveMissingRequested: home.app.removeMissingRecent()
                }
            }

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
}
