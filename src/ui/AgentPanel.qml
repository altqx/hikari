pragma Translator: "HikariSub.Agent"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

FocusScope {
    id: panel
    required property QtObject agent
    property QtObject aiSettings: null
    signal reviewRequested()
    signal restoreRequested(var turn)
    signal settingsRequested()
    signal logRequested()
    signal exportRequested(string id)
    signal pluginRequested(string id)

    readonly property var thread: agent ? agent.thread : ({})
    readonly property bool hasThread: thread.id !== undefined
    readonly property bool running: hasThread && thread.running
    readonly property string mode: hasThread ? thread.mode : "auto"
    readonly property bool canCompact: hasThread && thread.external !== true
    readonly property real contextUsed: hasThread && thread.compactWindow > 0 ? thread.contextTokens / thread.compactWindow : 0
    readonly property bool isChild: hasThread && (thread.child?.parent ?? "").length > 0
    property bool showingThreads: false
    property string componentResult: ""
    readonly property var download: aiSettings ? aiSettings.download : ({})
    Connections {
        target: panel.aiSettings
        ignoreUnknownSignals: true
        function onComponentReady(ready) { panel.componentResult = ready ? "ready" : "failed" }
    }
    objectName: "agentView"
    Keys.onEscapePressed: event => {
        event.accepted = panel.showingThreads || panel.isChild
        if (panel.showingThreads)
            panel.showingThreads = false
        else if (panel.isChild)
            panel.agent.openParent()
    }

    function tokens(n) {
        return n >= 1000 ? Number(n / 1000).toLocaleString(Qt.locale(), "f", n >= 100000 ? 0 : 1) + "K"
                         : Number(n).toLocaleString(Qt.locale(), "f", 0)
    }
    function money(v) { return "$" + Number(v).toLocaleString(Qt.locale(), "f", 2) }
    function serviceName(id) {
        return id === "brave" ? "Brave Search" : id === "tavily" ? "Tavily" : id === "exa" ? "Exa"
             : id === "firecrawl" ? "Firecrawl" : id
    }
    function taskStateName(k) {
        return k.status === "running" ? qsTr("running") : k.status === "needs_you" ? qsTr("needs you")
             : k.status === "done" ? qsTr("done") : k.status === "failed" ? qsTr("failed")
             : k.status === "interrupted" ? qsTr("interrupted")
             : k.stoppedFor === "cap" || k.stoppedFor === "dailyCap" ? qsTr("stopped · cap") : qsTr("stopped")
    }
    function taskStateTint(status) {
        return status === "needs_you" ? Theme.warning : status === "done" ? Theme.success
             : status === "failed" || status === "interrupted" ? Theme.danger
             : status === "running" ? Theme.accent : Theme.muted
    }
    function scopeName(document, lines) {
        return lines.length > 0 ? qsTr("%1 · %n line(s)", "", lines.length).arg(document) : document
    }
    function modeName(m) {
        return m === "manual" ? qsTr("Manual") : m === "full" ? qsTr("Full access") : qsTr("Auto")
    }
    function chooseMode(m) {
        if (m === "full" && panel.mode !== "full" && !panel.agent.fullAccessWarned) {
            fullAccessQuestion.ask({
                title: qsTr("Turn on Full access?"),
                severity: "warning",
                text: qsTr("The agent changes subtitles, styles and settings without asking. Every change still lands in the review and can be undone."),
                detail: qsTr("These still ask, in every mode: deleting or overwriting files outside the subtitles’ folders, running external programs and installing plugins, sending local files to the network, and changing AI permissions, keys or budgets."),
                dontAsk: true,
                dontAskText: qsTr("Don’t warn me again"),
                choices: [{ id: "full", text: qsTr("Turn on Full access"), role: "accept" },
                          { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }]
            }, (id, dontWarn) => {
                if (id !== "full")
                    return
                if (dontWarn)
                    panel.agent.setFullAccessWarned()
                panel.agent.setMode("full")
            })
            return
        }
        panel.agent.setMode(m)
    }
    function modelItemName(value) { return "agentModel_" + value.replace("\t", "/") }
    function chooseModel(value) {
        if (!hasThread)
            agent.newThread()
        agent.setModel(value)
    }
    function send() {
        const text = composer.text.trim()
        if (text.length === 0)
            return
        if (text === "/compact" || text.startsWith("/compact ")) {
            panel.agent.compact(text.slice("/compact".length).trim())
        } else if (text === "/new") {
            panel.agent.newThread()
        } else if (text === "/mode") {
            modeMenu.openAbove(modeButton)
        } else {
            panel.agent.send(text)
        }
        composer.text = ""
    }

    component PanelMenu: ShellMenu {
        id: panelMenu
        property int maxRows: 12
        property real maxWidth: panel.width - 16
        readonly property real rowHeight: count > 0 && itemAt(0) ? itemAt(0).implicitHeight : 24
        ownWindowWhenFloating: true
        width: Math.max(Math.min(200, maxWidth), Math.min(implicitWidth, maxWidth))
        height: Math.min(implicitHeight, topPadding + bottomPadding + maxRows * rowHeight)
        function openAbove(item) {
            const at = item.mapToItem(panel, 0, 0)
            parent = item
            x = Qt.binding(() => Math.max(8 - at.x, Math.min(0, panel.width - 8 - at.x - width)))
            y = Qt.binding(() => -height)
            open()
        }
    }
    component ChooserButton: Button {
        id: chooser
        flat: true
        contentItem: Row {
            spacing: 4
            Label {
                objectName: chooser.objectName + "Label"
                anchors.verticalCenter: parent.verticalCenter
                text: chooser.text
                color: chooser.palette.buttonText
            }
            Icon {
                objectName: chooser.objectName + "Chevron"
                anchors.verticalCenter: parent.verticalCenter
                size: 12
                iconRole: "chevron-down"
            }
        }
    }
    component Card: Rectangle {
        id: card
        property color edge: Theme.line
        default property alias content: cardColumn.data
        Layout.fillWidth: true
        implicitHeight: cardColumn.implicitHeight + 16
        radius: 4
        color: Theme.raised
        border.color: edge
        border.width: 1
        Rectangle {
            width: 3
            height: parent.height
            radius: 2
            color: card.edge
            visible: card.edge !== Theme.line
        }
        ColumnLayout {
            id: cardColumn
            x: 10
            y: 8
            width: parent.width - 20
            spacing: 6
        }
    }
    component CardTitle: RowLayout {
        property alias iconRole: titleIcon.iconRole
        property alias text: titleLabel.text
        property alias iconColor: titleIcon.color
        spacing: 6
        Layout.fillWidth: true
        Icon { id: titleIcon; size: 14; visible: iconRole.length > 0 }
        Label { id: titleLabel; font.bold: true; wrapMode: Text.Wrap; Layout.fillWidth: true }
    }
    component Chip: Label {
        property color tint: Theme.muted
        leftPadding: 6
        rightPadding: 6
        topPadding: 1
        bottomPadding: 1
        font.pixelSize: Math.round(Qt.application.font.pixelSize * 0.85)
        color: tint
        background: Rectangle { radius: height / 2; color: "transparent"; border.color: parent.tint; border.width: 1 }
    }
    component FileChip: Rectangle {
        id: fileChip
        property string name
        property string note
        property bool removable: false
        property bool active: false
        property real maxWidth: 280
        signal removed()
        implicitHeight: 22
        implicitWidth: Math.min(chipRow.implicitWidth + chipRow.anchors.leftMargin + chipRow.anchors.rightMargin, maxWidth)
        radius: 3
        color: Theme.raised
        border.color: active ? Theme.accent : Theme.line
        Accessible.role: Accessible.StaticText
        Accessible.name: note.length > 0 ? qsTr("%1, %2").arg(name, note) : name
        RowLayout {
            id: chipRow
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: fileChip.removable ? 2 : 6
            spacing: 4
            Icon { iconRole: "file-subtitles"; size: 14 }
            Label {
                objectName: "fileChipName"
                Layout.fillWidth: true
                text: fileChip.name
                textFormat: Text.PlainText
                elide: Text.ElideMiddle
                font.pixelSize: Math.round(Qt.application.font.pixelSize * 0.9)
            }
            Label {
                visible: fileChip.note.length > 0
                text: fileChip.note
                color: Theme.muted
                font.pixelSize: Math.round(Qt.application.font.pixelSize * 0.9)
            }
            IconToolButton {
                objectName: "fileChipRemove"
                visible: fileChip.removable
                implicitWidth: 18
                implicitHeight: 18
                padding: 0
                iconRole: "tab-close"
                iconSize: 12
                text: qsTr("Remove %1").arg(fileChip.name)
                onClicked: fileChip.removed()
                Keys.onDeletePressed: fileChip.removed()
            }
        }
    }
    component CardButtons: Flow {
        Layout.fillWidth: true
        spacing: 6
    }
    component TellButton: Button {
        flat: true
        text: qsTr("Tell the agent instead…")
    }
    component ReplyRow: RowLayout {
        id: replyRow
        property alias fieldName: replyField.objectName
        property string action
        signal sent(string text)
        signal cancelled()
        function begin() { replyField.forceActiveFocus() }
        Layout.fillWidth: true
        spacing: 6
        TextField {
            id: replyField
            Layout.fillWidth: true
            Layout.minimumWidth: 80
            placeholderText: qsTr("What should it do instead?")
            onAccepted: replyRow.sent(text)
            Keys.onEscapePressed: replyRow.cancelled()
        }
        Button { text: replyRow.action; onClicked: replyRow.sent(replyField.text) }
    }
    component DiffRows: ColumnLayout {
        id: diffRows
        property var rows: []
        property var document
        property bool all: false
        property int shown: 5
        property string rowName: "agentProposalRow_"
        Layout.fillWidth: true
        spacing: 2
        Repeater {
            model: diffRows.all ? diffRows.rows : diffRows.rows.slice(0, diffRows.shown)
            delegate: ItemDelegate {
                id: change
                required property var modelData
                required property int index
                objectName: diffRows.rowName + index
                Layout.fillWidth: true
                padding: 4
                enabled: modelData.line !== undefined && diffRows.document !== undefined
                onClicked: panel.agent.selectLine(diffRows.document, modelData.line)
                background: Rectangle {
                    color: Theme.field
                    border.color: change.visualFocus || change.hovered ? Theme.focus : Theme.line
                    border.width: change.visualFocus ? 2 : 1
                }
                contentItem: RowLayout {
                    spacing: 6
                    Label {
                        text: change.modelData.row !== undefined ? "#" + change.modelData.row : ""
                        color: Theme.muted
                        Layout.preferredWidth: 34
                        Layout.alignment: Qt.AlignTop
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Label {
                            visible: (change.modelData.before ?? "").length > 0
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                            font.strikeout: true
                            color: Theme.danger
                            text: change.modelData.before ?? ""
                        }
                        Label {
                            visible: (change.modelData.after ?? "").length > 0
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                            color: Theme.success
                            text: change.modelData.after ?? ""
                        }
                        Label {
                            visible: text.length > 0
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: Theme.muted
                            text: change.modelData.change === "deleted" ? qsTr("Deleted")
                                  : change.modelData.change === "moved" ? qsTr("Moved")
                                  : change.modelData.change === "inserted" ? qsTr("Inserted")
                                  : change.modelData.change === "other" ? (change.modelData.what ?? "")
                                  : change.modelData.fields ? qsTr("Changes %1").arg(change.modelData.fields) : ""
                        }
                    }
                }
            }
        }
    }
    component Mono: Label {
        Layout.fillWidth: true
        font.family: "monospace"
        wrapMode: Text.WrapAnywhere
        maximumLineCount: 6
        elide: Text.ElideRight
        padding: 4
        background: Rectangle { color: Theme.field; radius: 3 }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            id: header
            Layout.fillWidth: true
            Layout.margins: 4
            spacing: 2
            IconToolButton {
                objectName: "agentBackToParent"
                iconRole: "chevron-left"
                text: qsTr("Back to the parent thread")
                visible: panel.agent.ready && panel.isChild && !panel.showingThreads
                onClicked: panel.agent.openParent()
            }
            IconToolButton {
                objectName: "agentThreadsButton"
                iconRole: "history"
                text: qsTr("Threads")
                checkable: true
                checked: panel.showingThreads
                visible: panel.agent.ready && !panel.isChild
                onClicked: panel.showingThreads = !panel.showingThreads
            }
            Label {
                objectName: "agentTitle"
                Layout.fillWidth: true
                elide: Text.ElideRight
                font.bold: true
                text: panel.showingThreads ? qsTr("Threads")
                                           : panel.hasThread && panel.thread.title.length > 0 ? panel.thread.title
                                                                                              : qsTr("New thread")
            }
            Chip {
                objectName: "agentFullAccessBadge"
                visible: !panel.showingThreads && panel.mode === "full" && panel.hasThread
                text: qsTr("Full access")
                tint: Theme.danger
            }
            IconToolButton {
                objectName: "agentThreadMenuButton"
                iconRole: "panel-menu"
                text: qsTranslate("HikariSub.Workspace", "More options")
                visible: panel.agent.ready && !panel.showingThreads && !panel.isChild
                enabled: panel.hasThread
                onClicked: threadMenu.popup(this, 0, height)
            }
            IconToolButton {
                objectName: "agentNewThread"
                iconRole: "add"
                text: qsTr("New thread")
                visible: panel.agent.ready
                onClicked: {
                    panel.agent.newThread()
                    panel.showingThreads = false
                    composer.forceActiveFocus()
                }
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line }

        RowLayout {
            objectName: "agentChildBanner"
            visible: panel.agent.ready && panel.isChild && !panel.showingThreads
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 6
            Icon { iconRole: "notice-info"; size: 14 }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.muted
                text: qsTr("Sub-agent of “%1”. Read-only; talk to it through the parent.")
                      .arg(panel.isChild ? panel.thread.child.parentTitle : "")
            }
        }

        ColumnLayout {
            objectName: "agentNoProvider"
            visible: !panel.agent.ready
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 24
            spacing: 10
            Item { Layout.fillHeight: true }
            Icon { iconRole: "ai-assistant"; size: 32; Layout.alignment: Qt.AlignHCenter }
            Label {
                text: qsTr("Add a provider to use the agent")
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label {
                text: qsTr("HikariSub has no AI server. The agent runs on a provider you add: an API key, a ChatGPT or OpenRouter sign-in, or a model on this computer.")
                color: Theme.muted
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            IconTextButton {
                objectName: "agentOpenSettings"
                iconRole: "settings"
                text: qsTr("Open Settings › AI")
                highlighted: true
                Layout.alignment: Qt.AlignHCenter
                onClicked: panel.settingsRequested()
            }
            Label {
                text: qsTr("Nothing is sent or downloaded until you add one.")
                color: Theme.muted
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Item { Layout.fillHeight: true }
        }

        ColumnLayout {
            objectName: "agentThreadsList"
            visible: panel.agent.ready && panel.showingThreads
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 6
            spacing: 6
            TextField {
                id: threadSearch
                objectName: "agentThreadSearch"
                Layout.fillWidth: true
                placeholderText: qsTr("Search threads")
                Keys.onEscapePressed: event => {
                    if (text.length > 0)
                        clear()
                    else
                        panel.showingThreads = false
                }
            }
            Label {
                objectName: "agentNoThreadsMatch"
                visible: threadList.count === 0 && threadSearch.text.trim().length > 0
                Layout.fillWidth: true
                Layout.topMargin: 12
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                color: Theme.muted
                text: qsTr("No threads match “%1”").arg(threadSearch.text.trim())
            }
            ListView {
                id: threadList
                visible: count > 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: panel.agent.threads.filter(t => {
                    const q = threadSearch.text.trim().toLowerCase()
                    return q.length === 0 || t.title.toLowerCase().includes(q) || t.files.toLowerCase().includes(q)
                })
                section.property: "day"
                section.delegate: Label {
                    required property string section
                    topPadding: 8
                    bottomPadding: 2
                    color: Theme.muted
                    text: section === "project" ? qsTr("This project")
                          : section === "today" ? qsTr("Today") : section === "yesterday" ? qsTr("Yesterday")
                                              : Qt.formatDate(Date.fromLocaleDateString(Qt.locale(), section, "yyyy-MM-dd"),
                                                              Locale.ShortFormat)
                }
                delegate: ItemDelegate {
                    id: threadRow
                    required property var modelData
                    width: ListView.view.width
                    objectName: "agentThreadRow"
                    highlighted: modelData.id === panel.agent.current
                    onClicked: {
                        panel.agent.open(modelData.id)
                        panel.showingThreads = false
                    }
                    function openMenu(x, y) {
                        panel.agent.open(modelData.id)
                        threadMenu.popup(threadRow, x, y)
                    }
                    MenuKey.onRequested: at => openMenu(at.x, at.y)
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: point => threadRow.openMenu(point.position.x, point.position.y)
                    }
                    contentItem: RowLayout {
                        spacing: 6
                        ColumnLayout {
                            spacing: 1
                            Layout.fillWidth: true
                            Label {
                                text: threadRow.modelData.title.length > 0 ? threadRow.modelData.title : qsTr("New thread")
                                font.bold: true
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                color: Theme.muted
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                                text: [threadRow.modelData.files, threadRow.modelData.model,
                                       threadRow.modelData.costUnknown ? qsTr("cost not reported")
                                                                       : panel.money(threadRow.modelData.cost)]
                                      .filter(s => s.length > 0).join(" · ")
                            }
                        }
                        Chip {
                            visible: threadRow.modelData.state.length > 0
                            text: threadRow.modelData.state === "running" ? qsTr("running")
                                  : threadRow.modelData.state === "cap" ? qsTr("at cap") : qsTr("Full access")
                            tint: threadRow.modelData.state === "running" ? Theme.accent
                                  : threadRow.modelData.state === "cap" ? Theme.warning : Theme.danger
                        }
                    }
                }
            }
        }

        RowLayout {
            objectName: "agentProjectInstructions"
            visible: panel.agent.ready && !panel.showingThreads && panel.hasThread
                     && (panel.thread.projectInstructions ?? false)
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.topMargin: 4
            spacing: 6
            Icon { iconRole: "notice-info"; size: 13 }
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: Theme.muted
                text: qsTr("This project’s AGENTS.md instructions are on")
            }
            Button {
                objectName: "agentOpenProjectInstructions"
                flat: true
                text: qsTr("Open")
                onClicked: panel.agent.openProjectInstructions()
            }
        }

        ListView {
            id: timeline
            objectName: "agentTimeline"
            visible: panel.agent.ready && !panel.showingThreads
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 80
            clip: true
            spacing: 8
            topMargin: 8
            bottomMargin: 8
            leftMargin: 8
            rightMargin: 8
            model: panel.agent.items
            onCountChanged: Qt.callLater(() => timeline.positionViewAtEnd())
            ScrollBar.vertical: ScrollBar {}
            delegate: Loader {
                id: row
                required property int index
                required property string kind
                required property var item
                width: ListView.view.width - 16
                sourceComponent: kind === "user" ? userItem : kind === "text" ? textItem : kind === "tool" ? toolItem
                               : kind === "change" ? changeItem : kind === "plan" ? planItem : kind === "ask" ? askItem
                               : kind === "proposal" ? (item.status === "applied" ? null : proposalItem)
                               : kind === "notice" ? noticeItem : kind === "search" ? searchItem
                               : kind === "script" ? scriptItem : kind === "tasks" ? tasksItem
                               : kind === "estimate" ? estimateItem : kind === "install" ? installItem
                               : kind === "compaction" ? compactionItem : null
                readonly property bool last: index === timeline.count - 1
            }
        }

        Card {
            objectName: "agentCompactOffer"
            visible: panel.agent.ready && !panel.showingThreads && !panel.isChild && panel.canCompact && !panel.agent.autoCompact
                     && !panel.running && panel.contextUsed >= 0.97
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 4
            edge: Theme.warning
            CardTitle { iconRole: "notice-warning"; text: qsTr("This conversation is nearly full") }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.muted
                text: qsTr("%1 of %2 tokens. Automatic compaction is off in Settings › AI.")
                      .arg(panel.tokens(panel.thread.contextTokens ?? 0), panel.tokens(panel.thread.compactWindow ?? 0))
            }
            RowLayout {
                spacing: 6
                IconTextButton {
                    objectName: "agentOfferCompact"
                    iconRole: "compact"
                    highlighted: true
                    text: qsTr("Compact thread")
                    onClicked: panel.agent.compact("")
                }
                Button {
                    text: qsTr("New thread from summary")
                    onClicked: panel.agent.newThreadFromSummary()
                }
            }
        }

        Rectangle {
            objectName: "agentQueued"
            visible: panel.agent.ready && !panel.showingThreads && !panel.isChild && panel.hasThread && panel.thread.queued.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            implicitHeight: queuedRow.implicitHeight + 8
            radius: 4
            color: Theme.raised
            border.color: Theme.line
            RowLayout {
                id: queuedRow
                anchors.fill: parent
                anchors.margins: 4
                spacing: 4
                Icon { iconRole: "history"; size: 14 }
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: qsTr("Queued · %1").arg(panel.hasThread ? panel.thread.queued : "")
                }
                Button {
                    objectName: "agentSteerNow"
                    text: qsTr("Steer now")
                    enabled: panel.running
                    onClicked: panel.agent.steer()
                }
                IconToolButton {
                    iconRole: "edit"
                    text: qsTr("Edit")
                    onClicked: {
                        composer.text = panel.thread.queued
                        panel.agent.setQueued("")
                        composer.forceActiveFocus()
                    }
                }
                IconToolButton {
                    iconRole: "clear"
                    text: qsTr("Remove")
                    onClicked: panel.agent.setQueued("")
                }
            }
        }

        RowLayout {
            objectName: "agentMeter"
            visible: panel.agent.ready && !panel.showingThreads && panel.agent.costMeter
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.topMargin: 4
            spacing: 8
            readonly property real used: panel.hasThread && panel.thread.contextWindow > 0
                                         ? Math.min(1, panel.thread.contextTokens / panel.thread.contextWindow) : 0
            readonly property bool hot: panel.canCompact && panel.contextUsed >= 0.9
            ProgressBar {
                visible: panel.hasThread && panel.thread.contextWindow > 0
                from: 0
                to: 1
                value: parent.used
                palette.highlight: parent.hot ? Theme.warning : Theme.accent
                Layout.preferredWidth: 48
            }
            Icon {
                visible: parent.hot
                iconRole: "notice-warning"
                size: 14
                color: Theme.warning
            }
            Label {
                objectName: "agentTokens"
                Layout.fillWidth: true
                color: parent.hot ? Theme.warning : Theme.muted
                font.bold: parent.hot
                elide: Text.ElideRight
                readonly property string note: !panel.hasThread ? ""
                    : panel.thread.compacting ? qsTr("compacting…")
                    : parent.hot && panel.running && panel.agent.autoCompact ? qsTr("compacts at the next tool call") : ""
                text: (note.length > 0 ? "%1 · " + note : "%1").arg(tokenText)
                readonly property string tokenText: !panel.hasThread ? ""
                      : panel.thread.contextWindow > 0
                        ? qsTr("%1 of %2 tokens").arg(panel.tokens(panel.thread.contextTokens), panel.tokens(panel.thread.contextWindow))
                        : qsTr("%1 tokens").arg(panel.tokens(panel.thread.contextTokens))
            }
            Label {
                objectName: "agentCost"
                readonly property bool over: panel.hasThread && panel.thread.cap > 0 && panel.thread.cost >= panel.thread.cap
                color: over ? Theme.danger : Theme.muted
                text: !panel.hasThread ? ""
                      : panel.thread.costUnknown && panel.thread.cost === 0 ? qsTr("cost not reported")
                      : panel.thread.cap > 0 ? qsTr("%1 of %2").arg(panel.money(panel.thread.cost), panel.money(panel.thread.cap))
                      : panel.money(panel.thread.cost)
            }
        }

        RowLayout {
            objectName: "agentChildFooter"
            visible: panel.agent.ready && panel.isChild && !panel.showingThreads
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 6
            Chip {
                objectName: "agentChildMode"
                text: qsTr("%1 · capped by the parent").arg(panel.modeName(panel.mode))
            }
            Chip {
                objectName: "agentChildScope"
                text: panel.isChild ? qsTr("scope: %1").arg(panel.scopeName(panel.thread.child.document, panel.thread.child.lines)) : ""
            }
            Item { Layout.fillWidth: true }
            IconTextButton {
                objectName: "agentStopChild"
                visible: panel.running
                iconRole: "media-stop"
                text: qsTr("Stop")
                onClicked: panel.agent.stop()
            }
        }

        Rectangle {
            id: composerBox
            objectName: "agentComposerBox"
            readonly property var attached: panel.hasThread ? panel.thread.attached : []
            readonly property bool hasActiveTab: panel.agent.activeTitle.length > 0
            readonly property int chipRoom: Math.max(0, 3 - (hasActiveTab ? 1 : 0))
            readonly property int foldedFiles: Math.max(0, attached.length - chipRoom)
            readonly property int chipCount: (hasActiveTab ? 1 : 0) + Math.min(attached.length, chipRoom) + (foldedFiles > 0 ? 1 : 0)
            readonly property real chipRowHeight: 22 + chips.spacing
            readonly property real roomForChips: panel.height - timeline.y - timeline.Layout.minimumHeight - lineHeight - fieldPadding
                                                 - buttonRow.implicitHeight - 64
            readonly property int chipRows: Math.max(1, Math.floor(roomForChips / chipRowHeight))
            readonly property int chipsPerRow: Math.ceil(chipCount / chipRows)
            readonly property bool chipsShareRows: chipsPerRow > 1
            readonly property real moreWidth: foldedFiles > 0 ? moreFiles.implicitWidth + chips.spacing : 0
            readonly property real chipWidth: !chipsShareRows ? chips.width
                : (chips.width - moreWidth - (chipsPerRow - 1) * chips.spacing) / (chipsPerRow - (foldedFiles > 0 ? 1 : 0))
            readonly property real lineHeight: composerMetrics.lineSpacing
            readonly property real fieldPadding: composer.topPadding + composer.bottomPadding
            readonly property real maxFieldHeight: Math.max(lineHeight + fieldPadding,
                Math.min(6 * lineHeight + fieldPadding,
                         panel.height * 0.4 - chips.height - buttonRow.height - 3 * composerColumn.spacing - 12))
            visible: !panel.showingThreads && !panel.isChild
            Layout.fillWidth: true
            Layout.margins: 8
            implicitHeight: composerColumn.implicitHeight + 12
            radius: 2
            color: Theme.field
            border.color: composer.activeFocus ? Theme.accent : Theme.control
            border.width: 1
            enabled: panel.agent.ready
            FontMetrics { id: composerMetrics; font: composer.font }
            Rectangle {
                objectName: "agentComposerFocusRing"
                anchors.fill: parent
                anchors.margins: -2
                radius: 3
                color: "transparent"
                border.color: Theme.focus
                visible: composer.activeFocus && [Qt.TabFocusReason, Qt.BacktabFocusReason, Qt.ShortcutFocusReason]
                         .includes(composer.focusReason)
            }
            ColumnLayout {
                id: composerColumn
                x: 6
                y: 6
                width: parent.width - 12
                spacing: 4
                Flow {
                    id: chips
                    objectName: "agentChips"
                    Layout.fillWidth: true
                    visible: composerBox.hasActiveTab || composerBox.attached.length > 0
                    spacing: 4
                    FileChip {
                        objectName: "agentActiveTab"
                        visible: composerBox.hasActiveTab
                        name: panel.agent.activeTitle
                        note: composerBox.chipsShareRows ? "" : activeTabNote
                        readonly property string activeTabNote: qsTr("active tab")
                        Accessible.name: qsTr("%1, %2").arg(name, activeTabNote)
                        active: true
                        maxWidth: composerBox.chipWidth
                    }
                    Repeater {
                        model: composerBox.attached.slice(0, composerBox.chipRoom)
                        delegate: FileChip {
                            required property var modelData
                            objectName: "agentChip_" + modelData.id
                            name: modelData.title
                            removable: true
                            maxWidth: composerBox.chipWidth
                            onRemoved: {
                                composer.forceActiveFocus()
                                panel.agent.detach(modelData.id)
                            }
                        }
                    }
                    ToolButton {
                        id: moreFiles
                        objectName: "agentMoreFiles"
                        visible: composerBox.foldedFiles > 0
                        implicitHeight: 22
                        topPadding: 0
                        bottomPadding: 0
                        font.pixelSize: Math.round(Qt.application.font.pixelSize * 0.9)
                        text: qsTr("+%n more", "", composerBox.foldedFiles)
                        Accessible.name: qsTr("%n more attached file(s)", "", composerBox.foldedFiles)
                        onClicked: attachMenu.openFor(false)
                        background: Rectangle { radius: 3; color: Theme.raised; border.color: Theme.line }
                    }
                }
                ScrollView {
                    objectName: "agentComposerScroll"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(composer.implicitHeight, composerBox.maxFieldHeight)
                    TextArea {
                        id: composer
                        objectName: "agentComposer"
                        focus: true
                        wrapMode: TextEdit.Wrap
                        placeholderText: panel.agent.ready ? qsTr("Ask, or type @ to attach a file, / for commands")
                                                           : qsTr("Add a provider first")
                        background: null
                        Accessible.name: qsTr("Message")
                        Keys.onShortcutOverride: event => event.accepted = event.key === Qt.Key_Escape && panel.running
                        Keys.onPressed: event => {
                            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                    && !(event.modifiers & Qt.ShiftModifier)) {
                                panel.send()
                                event.accepted = true
                            } else if (event.key === Qt.Key_Escape && panel.running) {
                                panel.agent.stop()
                                event.accepted = true
                            }
                        }
                        onTextChanged: {
                            if (text.endsWith("@") && panel.agent.attachable.length > 0)
                                attachMenu.openFor(true)
                            if (text === "/")
                                slashMenu.openAbove(composerBox)
                        }
                    }
                }
                RowLayout {
                    id: buttonRow
                    Layout.fillWidth: true
                    spacing: 4
                    IconToolButton {
                        objectName: "agentAttach"
                        iconRole: "attach"
                        text: qsTr("Attach a file")
                        enabled: panel.agent.attachable.length > 0
                        onClicked: attachMenu.openFor(false)
                    }
                    ChooserButton {
                        id: modelButton
                        objectName: "agentModelButton"
                        readonly property string modelText: (panel.hasThread && panel.thread.modelText) || panel.agent.defaultModelText
                        text: modelText || qsTr("Choose a model")
                        Accessible.name: qsTr("Model")
                        Accessible.description: modelText
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Model")
                        onClicked: modelMenu.openAbove(this)
                    }
                    ChooserButton {
                        id: modeButton
                        objectName: "agentModeButton"
                        text: panel.modeName(panel.mode)
                        palette.buttonText: panel.mode === "full" ? Theme.danger : Theme.text
                        onClicked: modeMenu.openAbove(this)
                    }
                    Item { Layout.fillWidth: true }
                    IconToolButton {
                        id: sendButton
                        readonly property bool stops: panel.running && composer.text.trim().length === 0
                        readonly property bool filled: !stops && enabled
                        objectName: panel.running ? "agentStop" : "agentSend"
                        iconRole: stops ? "media-stop" : "ai-send"
                        text: stops ? qsTr("Stop") : qsTr("Send")
                        enabled: panel.running || composer.text.trim().length > 0
                        implicitWidth: 28
                        implicitHeight: 28
                        contentItem: Icon {
                            iconRole: sendButton.iconRole
                            color: sendButton.filled ? Theme.onAccent : sendButton.enabled ? IconTheme.normal : IconTheme.disabled
                            accentColor: sendButton.filled ? Theme.onAccent : sendButton.enabled ? IconTheme.accent : IconTheme.disabled
                        }
                        background: Rectangle {
                            radius: 2
                            color: sendButton.filled ? (sendButton.down ? Qt.darker(Theme.accent, 1.15) : Theme.accent)
                                 : sendButton.stops ? Theme.raised : "transparent"
                            border.color: sendButton.visualFocus ? Theme.focus : sendButton.stops ? Theme.control : "transparent"
                        }
                        onClicked: stops ? panel.agent.stop() : panel.send()
                    }
                }
            }
        }
    }

    ShellMenu {
        id: threadMenu
        objectName: "agentThreadMenu"
        ShellMenuItem {
            objectName: "agentRenameThread"
            iconRole: "edit"
            text: qsTr("Rename thread…")
            onTriggered: renameLoader.active = true
        }
        ShellMenuItem {
            objectName: "agentExportThread"
            iconRole: "export"
            text: qsTr("Export as Markdown…")
            onTriggered: panel.exportRequested(panel.thread.id)
        }
        MenuSeparator {}
        ShellMenuItem {
            objectName: "agentCompactThread"
            iconRole: "compact"
            text: qsTr("Compact thread")
            enabled: panel.canCompact && !(panel.thread.compacting ?? false)
            onTriggered: panel.agent.compact("")
        }
        ShellMenuItem {
            objectName: "agentThreadFromSummary"
            iconRole: "add"
            text: qsTr("New thread from summary")
            enabled: panel.canCompact && !panel.running
            onTriggered: panel.agent.newThreadFromSummary()
        }
        MenuSeparator {}
        ShellMenuItem {
            objectName: "agentDeleteThread"
            iconRole: "delete"
            text: qsTr("Delete thread…")
            onTriggered: deleteQuestion.ask({
                title: qsTr("Delete thread"),
                severity: "warning",
                plainText: true,
                text: qsTr("Delete “%1”?").arg(panel.thread.title.length > 0 ? panel.thread.title : qsTr("New thread")),
                detail: qsTr("Its messages are deleted. Changes it made stay in your subtitles and in the review."),
                choices: [{ id: "delete", text: qsTr("Delete"), role: "destructive" },
                          { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }]
            }, id => {
                if (id === "delete")
                    panel.agent.remove(panel.thread.id)
            })
        }
    }

    ShellMenu {
        id: answerMenu
        objectName: "agentAnswerMenu"
        property Item editor: null
        function openFor(answer, at) {
            editor = answer
            popup(answer, at.x, at.y)
        }
        TextMenuItem { kind: TextMenuItem.Copy; editor: answerMenu.editor }
        MenuSeparator {}
        TextMenuItem { kind: TextMenuItem.SelectAll; editor: answerMenu.editor }
    }

    PanelMenu {
        id: attachMenu
        objectName: "agentAttachMenu"
        maxRows: 8
        maxWidth: composerBox.width
        readonly property var attachedIds: panel.hasThread ? panel.thread.attached.map(a => a.id) : []
        function openFor(typedAt) {
            openAbove(composerBox)
            if (typedAt)
                currentIndex = Array.from({ length: count }, (_, i) => itemAt(i)).findIndex(row => row?.enabled && row.visible)
        }
        readonly property real nameRoom: maxWidth - 120
        ShellMenuItem {
            objectName: "agentAttachActiveTab"
            visible: panel.agent.activeTitle.length > 0
            height: visible ? implicitHeight : 0
            enabled: false
            checkable: true
            checked: true
            iconRole: "open-subtitles"
            text: activeName.elidedText
            detailText: qsTr("active tab")
            Accessible.name: panel.agent.activeTitle
            TextMetrics { id: activeName; font: attachMenu.font; elide: Text.ElideMiddle; elideWidth: attachMenu.nameRoom - 70; text: panel.agent.activeTitle }
        }
        Repeater {
            model: panel.agent.attachable
            delegate: ShellMenuItem {
                required property var modelData
                readonly property bool attached: attachMenu.attachedIds.indexOf(modelData.id) >= 0
                objectName: "agentAttach_" + modelData.id
                text: fileName.elidedText
                iconRole: "open-subtitles"
                checkable: true
                keepsMenuOpen: false
                checked: attached
                help: modelData.title
                Accessible.name: modelData.title
                TextMetrics { id: fileName; font: attachMenu.font; elide: Text.ElideMiddle; elideWidth: attachMenu.nameRoom; text: modelData.title }
                onTriggered: {
                    if (attached)
                        panel.agent.detach(modelData.id)
                    else
                        panel.agent.attach(modelData.id)
                    if (composer.text.endsWith("@"))
                        composer.text = composer.text.slice(0, -1)
                    composer.forceActiveFocus()
                }
            }
        }
    }

    PanelMenu {
        id: slashMenu
        objectName: "agentSlashMenu"
        ShellMenuItem {
            iconRole: "compact"
            text: qsTr("Compact thread")
            detailText: "/compact"
            enabled: panel.canCompact
            onTriggered: {
                composer.text = "/compact "
                composer.cursorPosition = composer.length
                composer.forceActiveFocus()
            }
        }
        ShellMenuItem {
            iconRole: "add"
            text: qsTr("New thread")
            detailText: "/new"
            onTriggered: {
                composer.text = ""
                panel.agent.newThread()
            }
        }
        ShellMenuItem {
            iconRole: "ai-assistant"
            text: qsTr("Change mode")
            detailText: "/mode"
            onTriggered: {
                composer.text = ""
                modeMenu.openAbove(modeButton)
            }
        }
    }

    PanelMenu {
        id: modelMenu
        objectName: "agentModelMenu"
        Instantiator {
            model: panel.agent.agents
            delegate: ShellMenu {
                id: agentModelMenu
                required property var modelData
                readonly property var listed: panel.agent.agentModelsRevision >= 0 ? panel.agent.agentModels(modelData.agent) : ({})
                objectName: "agentModelMenu_" + modelData.agent
                iconRole: "ai-assistant"
                title: modelData.text
                onAboutToShow: panel.agent.loadAgentModels(modelData.agent)
                MenuRadioItem {
                    objectName: panel.modelItemName(agentModelMenu.modelData.value)
                    text: qsTr("Agent default")
                    checked: panel.hasThread && panel.thread.model === agentModelMenu.modelData.value
                    onTriggered: panel.chooseModel(agentModelMenu.modelData.value)
                }
                Repeater {
                    model: agentModelMenu.listed.models ?? []
                    delegate: MenuRadioItem {
                        required property var modelData
                        objectName: panel.modelItemName(modelData.value)
                        text: modelData.text
                        checked: panel.hasThread && panel.thread.model === modelData.value
                        onTriggered: panel.chooseModel(modelData.value)
                    }
                }
                ShellMenuItem {
                    objectName: "agentModelsLoading_" + agentModelMenu.modelData.agent
                    text: qsTr("Loading models…")
                    enabled: false
                    visible: agentModelMenu.listed.loading === true
                    height: visible ? implicitHeight : 0
                }
            }
            onObjectAdded: (index, object) => modelMenu.insertMenu(index, object)
            onObjectRemoved: (index, object) => modelMenu.removeMenu(object)
        }
        Repeater {
            model: panel.agent.models.filter(m => !m.agent)
            delegate: MenuRadioItem {
                required property var modelData
                objectName: panel.modelItemName(modelData.value)
                text: modelData.text
                detailText: modelData.provider
                checked: panel.hasThread && panel.thread.model === modelData.value
                onTriggered: panel.chooseModel(modelData.value)
            }
        }
        MenuSeparator {}
        ShellMenuItem {
            iconRole: "settings"
            objectName: "agentModelSettings"
            text: qsTr("Settings › AI")
            onTriggered: panel.settingsRequested()
        }
    }

    PanelMenu {
        id: modeMenu
        objectName: "agentModeMenu"
        width: Math.min(320, panel.width - 16)
        Repeater {
            model: [{id: "manual", name: qsTr("Manual"), detail: qsTr("You approve each subtitle edit before it’s made, and every other change. Reading never asks.")},
                    {id: "auto", name: qsTr("Auto"), detail: qsTr("The judge model approves each action, or asks you. It sees the action and your messages, never your files.")},
                    {id: "full", name: qsTr("Full access"), detail: qsTr("Nothing asks except the always-ask list. Edits apply at once and stay undoable.")}]
            delegate: MenuRadioItem {
                required property var modelData
                objectName: "agentMode_" + modelData.id
                checked: panel.mode === modelData.id
                text: modelData.name
                description: modelData.detail
                danger: modelData.id === "full"
                font.weight: Font.DemiBold
                onTriggered: {
                    if (!panel.hasThread)
                        panel.agent.newThread()
                    panel.chooseMode(modelData.id)
                }
            }
        }
        MenuSeparator {}
        ShellMenuItem {
            iconRole: "settings"
            objectName: "agentModeDefault"
            text: qsTr("Default for new threads")
            onTriggered: panel.settingsRequested()
        }
    }

    LazyMessageBox {
        id: fullAccessQuestion
        name: "agentFullAccessQuestion"
    }

    LazyMessageBox {
        id: deleteQuestion
        name: "agentDeleteQuestion"
    }

    Loader {
        id: renameLoader
        active: false
        sourceComponent: DialogWindow {
            id: renameDialog
            objectName: "agentRenameWindow"
            title: qsTr("Rename thread")
            modal: true
            initialFocus: renameField
            defaultButton: renameOk
            onAccepted: if (renameOk.enabled) panel.agent.rename(panel.thread.id, renameField.text.trim())
            onVisibleChanged: if (!visible) Qt.callLater(() => renameLoader.active = false)
            Component.onCompleted: {
                renameField.text = panel.thread.title
                renameField.selectAll()
                open()
            }
            Label {
                text: qsTr("Name")
            }
            TextField {
                id: renameField
                objectName: "agentRenameField"
                Accessible.name: qsTr("Thread name")
                Layout.preferredWidth: 360
            }
            buttons: [
                PrimaryButton {
                    id: renameOk
                    objectName: "agentRenameOk"
                    text: qsTr("Rename")
                    enabled: renameField.text.trim().length > 0 && renameField.text.trim() !== panel.thread.title
                    DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                    onClicked: renameDialog.accept()
                },
                Button {
                    text: qsTr("Cancel")
                    DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                    onClicked: renameDialog.reject()
                }
            ]
        }
    }

    Component {
        id: userItem
        ColumnLayout {
            readonly property var item: parent ? parent.item : ({})
            readonly property bool fromAgent: item.brief === true || item.results === true
            spacing: 2
            Label {
                visible: parent.fromAgent
                Layout.alignment: parent.item.brief ? Qt.AlignLeft : Qt.AlignRight
                color: Theme.muted
                text: parent.item.brief ? qsTr("Brief, written by the agent") : qsTr("Results from your sub-agents")
            }
            Label {
                Layout.alignment: parent.item.brief ? Qt.AlignLeft : Qt.AlignRight
                Layout.fillWidth: parent.item.brief === true
                Layout.maximumWidth: parent.item.brief ? parent.width : parent.width * 0.85
                maximumLineCount: parent.item.results ? 12 : 100000
                elide: Text.ElideRight
                text: parent.item.results ? (parent.item.text ?? "").replace(/^[^\n]*\n+/, "") : parent.item.text ?? ""
                wrapMode: Text.Wrap
                padding: 8
                background: Rectangle {
                    radius: 6
                    color: parent.parent.fromAgent ? Theme.raised : Theme.select
                    border.color: parent.parent.fromAgent ? Theme.line : "transparent"
                }
            }
            RowLayout {
                visible: !parent.item.brief
                Layout.alignment: Qt.AlignRight
                spacing: 6
                Label {
                    color: Theme.muted
                    text: (parent.parent.item.steered ? qsTr("steered") + " · " : "")
                          + (parent.parent.item.time ? Qt.formatTime(new Date(parent.parent.item.time), Locale.ShortFormat) : "")
                }
                LinkButton {
                    visible: (parent.parent.item.turn ?? 0) > 0 && !panel.isChild
                    iconRole: "checkpoint"
                    text: qsTr("Restore checkpoint")
                    onClicked: panel.restoreRequested(parent.parent.item.turn)
                }
            }
        }
    }

    Component {
        id: textItem
        ColumnLayout {
            id: answer
            readonly property var row: parent
            readonly property var item: row ? row.item : ({})
            readonly property var citations: item.citations ?? []
            readonly property bool cited: citations.some(c => c.end !== undefined)
            property bool allSources: false
            spacing: 6
            TextEdit {
                objectName: "agentAnswer"
                Layout.fillWidth: true
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                color: Theme.text
                selectionColor: Theme.select
                textFormat: answer.cited ? TextEdit.RichText : TextEdit.PlainText
                text: answer.cited ? panel.agent.citedText(answer.item, Theme.select, Theme.text)
                                   : (answer.item.text ?? "") + (answer.row && answer.row.last && panel.running ? " ▍" : "")
                onLinkHovered: link => {
                    if (link.startsWith("cite:")) {
                        const p = answerHover.point.position
                        citationHover.show(answer.citations.find(c => "cite:" + c.index === link), this,
                                           positionToRectangle(positionAt(p.x, p.y)))
                    } else {
                        citationHover.leave()
                    }
                }
                onLinkActivated: link => {
                    const c = answer.citations.find(c => "cite:" + c.index === link)
                    if (c)
                        panel.agent.openLink(c.url)
                }
                HoverHandler {
                    id: answerHover
                    cursorShape: parent.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.IBeamCursor
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: point => answerMenu.openFor(parent, point.position)
                }
                MenuKey.onRequested: position => answerMenu.openFor(this, position)
            }
            SearchSuggestions {
                id: suggestionsStrip
                readonly property var view: (answer.item.searchSuggestions ?? "").length > 0
                                            ? panel.agent.suggestions(answer.item.searchSuggestions) : ({})
                visible: (view.chips ?? []).length > 0
                Layout.fillWidth: true
                chips: view.chips ?? []
                lightLogo: view.lightLogo ?? ""
                darkLogo: view.darkLogo ?? ""
                onChipActivated: url => panel.agent.openLink(url)
            }
            ColumnLayout {
                objectName: "agentSources"
                visible: answer.citations.length > 0
                Layout.fillWidth: true
                spacing: 2
                CardTitle { iconRole: "citations"; text: qsTr("Sources"); iconColor: Theme.muted }
                Repeater {
                    model: answer.allSources ? answer.citations : answer.citations.slice(0, 5)
                    delegate: RowLayout {
                        id: source
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 6
                        Label { text: source.modelData.index; color: Theme.muted; Layout.preferredWidth: 14 }
                        LinkButton {
                            objectName: "agentSource_" + source.modelData.index
                            iconRole: "open-link"
                            text: source.modelData.title
                            Layout.maximumWidth: answer.width * 0.65
                            onClicked: panel.agent.openLink(source.modelData.url)
                            onHoveredChanged: hovered ? citationHover.show(source.modelData, this) : citationHover.leave()
                        }
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            color: Theme.muted
                            text: panel.agent.siteOf(source.modelData.url)
                        }
                    }
                }
                LinkButton {
                    objectName: "agentMoreSources"
                    visible: !answer.allSources && answer.citations.length > 5
                    iconRole: "chevron-down"
                    text: qsTr("Show %n more", "", answer.citations.length - 5)
                    onClicked: answer.allSources = true
                }
            }
        }
    }

    Popup {
        id: citationHover
        objectName: "agentCitationHover"
        property var citation: ({})
        readonly property bool pointerInside: cardHover.hovered
        function show(c, over, at) {
            if (!c)
                return
            leaveTimer.stop()
            citation = c
            parent = over
            x = at ? at.x : 0
            y = at ? at.y + at.height + 2 : over.height + 2
            open()
        }
        function leave() { leaveTimer.restart() }
        width: Math.min(320, panel.width - 16)
        padding: 12
        margins: 8
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: leaveTimer.stop()
        background: PopoverBackground {}
        Shortcut {
            sequence: "Esc"
            enabled: citationHover.visible
            onActivated: citationHover.close()
        }
        Timer {
            id: leaveTimer
            interval: 300
            onTriggered: if (!citationHover.pointerInside) citationHover.close()
        }
        contentItem: ColumnLayout {
            spacing: 4
            HoverHandler {
                id: cardHover
                onHoveredChanged: hovered ? leaveTimer.stop() : citationHover.leave()
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.bold: true
                textFormat: Text.PlainText
                text: citationHover.citation.title ?? ""
            }
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: Theme.muted
                text: panel.agent.siteOf(citationHover.citation.url ?? "")
            }
            Label {
                visible: (citationHover.citation.quote ?? "").length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                maximumLineCount: 6
                elide: Text.ElideRight
                font.italic: true
                textFormat: Text.PlainText
                text: "“" + (citationHover.citation.quote ?? "") + "”"
            }
            LinkButton {
                objectName: "agentCitationOpen"
                iconRole: "open-link"
                text: qsTr("Open in browser")
                onClicked: {
                    panel.agent.openLink(citationHover.citation.url)
                    citationHover.close()
                }
            }
        }
    }

    component SearchRow: ColumnLayout {
        id: searchRow
        property string iconRole
        property string text
        property var urls: []
        property bool open: false
        Layout.fillWidth: true
        spacing: 2
        ItemDelegate {
            Layout.fillWidth: true
            padding: 2
            enabled: searchRow.urls.length > 0
            onClicked: searchRow.open = !searchRow.open
            contentItem: RowLayout {
                spacing: 6
                Icon { size: 14; iconRole: searchRow.iconRole }
                Label { Layout.fillWidth: true; elide: Text.ElideRight; color: Theme.muted; text: searchRow.text }
                Icon {
                    size: 12
                    visible: searchRow.urls.length > 0
                    iconRole: searchRow.open ? "chevron-down" : "chevron-right"
                }
            }
        }
        Repeater {
            model: searchRow.open ? searchRow.urls : []
            delegate: LinkButton {
                required property string modelData
                Layout.leftMargin: 20
                Layout.maximumWidth: searchRow.width - 20
                iconRole: "open-link"
                text: modelData
                onClicked: panel.agent.openLink(modelData)
            }
        }
    }

    Component {
        id: searchItem
        ColumnLayout {
            id: searched
            readonly property var item: parent ? parent.item : ({})
            spacing: 2
            Repeater {
                model: searched.item.searches ?? []
                delegate: SearchRow {
                    required property var modelData
                    readonly property bool pageRead: modelData.kind === "read"
                    objectName: "agentSearchRow"
                    iconRole: pageRead ? "web-page-read" : "web-search"
                    text: modelData.kind === "read"
                          ? qsTr("Read %1").arg(panel.agent.siteOf(modelData.url ?? ""))
                          : (searched.item.by === "google" ? qsTr("Searched with Google") : qsTr("Searched the web"))
                            + (modelData.query ? " · " + modelData.query : "")
                    urls: modelData.urls ?? (modelData.url ? [modelData.url] : [])
                }
            }
        }
    }

    Component {
        id: toolItem
        ColumnLayout {
            id: tool
            readonly property var item: parent ? parent.item : ({})
            readonly property bool failed: item.status === "failed"
            readonly property bool busy: item.status === "running"
            readonly property bool web: (item.tool === "web_search" || item.tool === "web_fetch") && !failed
            readonly property bool pageRead: item.tool === "web_fetch"
            property bool open: false
            spacing: 2
            SearchRow {
                objectName: "agentSearchRow"
                visible: tool.web
                iconRole: tool.busy ? "run-busy" : tool.pageRead ? "web-page-read" : "web-search"
                text: tool.item.tool === "web_fetch"
                      ? qsTr("Read %1").arg(panel.agent.siteOf((tool.item.input ?? {}).url ?? ""))
                      : qsTr("Searched with %1").arg(panel.serviceName(tool.item.service ?? ""))
                        + ((tool.item.input ?? {}).query ? " · " + tool.item.input.query : "")
                urls: tool.item.urls ?? []
            }
            ItemDelegate {
                visible: !tool.web
                Layout.fillWidth: true
                padding: 2
                onClicked: tool.open = !tool.open
                contentItem: RowLayout {
                    spacing: 6
                    Icon {
                        size: 14
                        iconRole: tool.failed ? "notice-error" : tool.busy ? "run-busy"
                                  : tool.item.read ? "search" : "run-applied"
                    }
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        color: tool.item.status === "failed" ? Theme.danger : Theme.muted
                        text: tool.item.tool ?? ""
                    }
                    Icon { size: 12; iconRole: tool.open ? "chevron-down" : "chevron-right" }
                }
            }
            Mono {
                visible: tool.open && !tool.web
                text: JSON.stringify(tool.item.input ?? {})
            }
            Mono {
                visible: tool.open && !tool.web && (tool.item.output ?? "").length > 0
                text: tool.item.output ?? ""
            }
        }
    }

    Component {
        id: changeItem
        Card {
            readonly property var item: parent ? parent.item : ({})
            edge: Theme.accent
            RowLayout {
                spacing: 6
                Layout.fillWidth: true
                Icon { iconRole: "run-applied"; size: 14 }
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    font.bold: true
                    text: (parent.parent.parent.item.name ?? "") + (parent.parent.parent.item.title ? " · " + parent.parent.parent.item.title : "")
                }
                Chip { visible: parent.parent.parent.item.judged === true; text: qsTr("judge approved"); tint: Theme.success }
                LinkButton { iconRole: "review-keep"; text: qsTr("Review"); onClicked: panel.reviewRequested() }
            }
        }
    }

    Component {
        id: planItem
        Card {
            readonly property var entries: parent && parent.item.entries ? parent.item.entries : []
            CardTitle { text: qsTr("Plan") }
            Repeater {
                model: parent.parent.entries
                delegate: Label {
                    required property var modelData
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: (modelData.status === "completed" ? "☑  " : modelData.status === "in_progress" ? "▶  " : "☐  ") + modelData.content
                    font.strikeout: modelData.status === "completed"
                    font.bold: modelData.status === "in_progress"
                    color: modelData.status === "completed" ? Theme.muted : Theme.text
                }
            }
        }
    }

    property Item latestProposal: null
    property bool proposalWantsFocus: false

    function holdsFocus() {
        const w = panel.Window.window
        for (let it = w ? w.activeFocusItem : null; it; it = it.parent)
            if (it === panel)
                return true
        return false
    }

    function focusProposal() {
        if (!proposalWantsFocus || composer.activeFocus || !latestProposal || !latestProposal.open)
            return
        proposalWantsFocus = false
        latestProposal.focusApply()
    }

    Connections {
        target: panel.agent
        function onAsked() {
            panel.proposalWantsFocus = !composer.activeFocus && panel.holdsFocus()
            Qt.callLater(panel.focusProposal)
        }
    }

    Component {
        id: proposalItem
        Card {
            id: proposal
            readonly property var item: parent ? parent.item : ({})
            readonly property var rows: item.rows ?? []
            readonly property bool open: item.status === "open"
            readonly property bool stale: item.status === "stale"
            property bool replying: false
            property bool allRows: false
            function focusApply() { applyButton.forceActiveFocus() }
            objectName: "agentProposal"
            edge: stale ? Theme.warning : open ? Theme.accent : Theme.disabled
            Accessible.role: Accessible.Grouping
            Accessible.name: qsTr("Proposal: %1, %n lines", "", rows.length).arg(item.name ?? "")
            Component.onCompleted: {
                if (!open && !stale)
                    return
                panel.latestProposal = proposal
                panel.focusProposal()
            }
            RowLayout {
                visible: proposal.open || proposal.stale
                Layout.fillWidth: true
                CardTitle {
                    iconRole: proposal.stale ? "notice-warning" : "edit"
                    text: (proposal.item.fromChild !== undefined ? qsTr("%1 proposes: %2").arg(proposal.item.fromChild, proposal.item.name ?? "")
                                                                 : qsTr("Proposed: %1").arg(proposal.item.name ?? ""))
                          + (proposal.item.title ? " · " + proposal.item.title : "")
                }
                Chip {
                    text: proposal.stale ? qsTr("out of date") : qsTr("%n lines", "", proposal.rows.length)
                    tint: proposal.stale ? Theme.warning : Theme.muted
                }
            }
            DiffRows {
                visible: proposal.open || proposal.stale
                rows: proposal.open || proposal.stale ? proposal.rows : []
                document: proposal.item.document
                all: proposal.allRows
            }
            LinkButton {
                objectName: "agentProposalAll"
                visible: (proposal.open || proposal.stale) && !proposal.allRows && proposal.rows.length > 5
                iconRole: "chevron-down"
                text: qsTr("Show all %n lines", "", proposal.rows.length)
                onClicked: proposal.allRows = true
            }
            Label {
                visible: proposal.open || proposal.stale
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: proposal.stale ? Theme.warning : Theme.muted
                text: proposal.stale ? qsTr("The subtitles changed after the agent read them.")
                                     : qsTr("Click a row to select its line. Applying adds one undoable step to the review.")
            }
            CardButtons {
                visible: (proposal.open || proposal.stale) && !proposal.replying
                Button {
                    id: applyButton
                    objectName: "agentProposalApply"
                    text: qsTr("Apply")
                    enabled: proposal.open
                    highlighted: proposal.open
                    onClicked: panel.agent.answer(proposal.item.ask, "apply", "")
                    Keys.onReturnPressed: clicked()
                    Keys.onEnterPressed: clicked()
                }
                Button {
                    objectName: "agentProposalAskAgain"
                    visible: proposal.stale
                    text: qsTr("Ask again")
                    highlighted: true
                    onClicked: panel.agent.answer(proposal.item.ask, "askAgain", "")
                    Keys.onReturnPressed: clicked()
                    Keys.onEnterPressed: clicked()
                }
                Button {
                    objectName: "agentProposalDecline"
                    text: qsTr("Decline")
                    onClicked: panel.agent.answer(proposal.item.ask, "decline", "")
                    Keys.onReturnPressed: clicked()
                    Keys.onEnterPressed: clicked()
                }
                TellButton {
                    objectName: "agentProposalTell"
                    visible: proposal.open
                    onClicked: {
                        proposal.replying = true
                        proposalReply.begin()
                    }
                }
            }
            ReplyRow {
                id: proposalReply
                visible: (proposal.open || proposal.stale) && proposal.replying
                fieldName: "agentProposalReply"
                action: qsTr("Decline")
                onSent: text => panel.agent.answer(proposal.item.ask, "decline", text)
                onCancelled: {
                    proposal.replying = false
                    applyButton.forceActiveFocus()
                }
            }
            RowLayout {
                visible: !proposal.open && !proposal.stale
                spacing: 6
                Icon { iconRole: "run-cancelled"; size: 14; color: Theme.muted }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.muted
                    text: (proposal.item.status === "retried" ? qsTr("Sent back to the agent: %1")
                           : proposal.item.status === "declined" ? qsTr("Declined: %1 · nothing changed")
                           : qsTr("Not applied: %1 · nothing changed")).arg(proposal.item.name ?? "")
                }
            }
        }
    }

    component Details: ColumnLayout {
        id: details
        property string text
        property bool open: false
        Layout.fillWidth: true
        spacing: 2
        LinkButton {
            objectName: "agentDetailsToggle"
            iconRole: details.open ? "chevron-down" : "chevron-right"
            text: qsTr("Details")
            Accessible.role: Accessible.ButtonDropDown
            onClicked: details.open = !details.open
        }
        Label {
            objectName: "agentDetailsText"
            visible: details.open
            Layout.fillWidth: true
            Layout.maximumHeight: 12 * fontMetrics.lineSpacing + 8
            clip: true
            font.family: "monospace"
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            padding: 4
            text: details.text
            background: Rectangle { color: Theme.field; border.color: Theme.line }
            FontMetrics { id: fontMetrics; font.family: "monospace" }
        }
    }

    function askTitle(item) {
        if (item.fromChild !== undefined)
            return qsTr("%1 asks you").arg(item.fromChild)
        const area = item.area ?? ""
        if (area === "setting")
            return settingTitle(item)
        return area === "subtitles" ? qsTr("Change the subtitles?")
             : area === "plugin" ? qsTr("Use a plugin?")
             : area === "mcp" ? qsTr("Use %1?").arg(item.tool ?? "")
             : qsTr("Allow this action?")
    }
    function settingTitle(item) {
        const name = SettingLabels.of(item.setting ?? "")
        if (name.length === 0)
            return qsTr("Change a setting")
        return item.value === "true" ? qsTr("Turn on “%1”").arg(name)
             : item.value === "false" ? qsTr("Turn off “%1”").arg(name)
             : qsTr("Set “%1” to %2").arg(name, item.value ?? "")
    }
    function askSummary(item) {
        const rows = item.rows ?? []
        if (rows.length === 1 && rows[0].change === "edited" && rows[0].after !== undefined)
            return rows[0].row !== undefined ? qsTr("Change line %1 to “%2”").arg(rows[0].row, rows[0].after)
                                             : qsTr("Change a line to “%1”").arg(rows[0].after)
        if (rows.length > 0)
            return qsTr("Change %n line(s)", "", rows.length) + ((item.title ?? "").length > 0 ? " · " + item.title : "")
        if ((item.area ?? "") === "setting")
            return ""
        return item.summary ?? ""
    }

    Component {
        id: askItem
        Card {
            id: ask
            readonly property var item: parent ? parent.item : ({})
            readonly property bool open: item.status === "open"
            readonly property bool always: item.type === "always"
            readonly property var rows: item.rows ?? []
            property bool replying: false
            objectName: "agentAsk"
            edge: always ? Theme.danger : Theme.warning
            RowLayout {
                Layout.fillWidth: true
                CardTitle {
                    objectName: "agentAskTitle"
                    iconRole: ask.always ? "notice-error" : "notice-warning"
                    text: panel.askTitle(ask.item)
                }
                Chip {
                    visible: ask.always || ask.item.fromChild !== undefined
                    text: ask.item.fromChild === undefined ? qsTr("asks in every mode")
                          : ask.always ? qsTr("sub-agent · asks in every mode") : qsTr("sub-agent")
                    tint: ask.always ? Theme.danger : Theme.muted
                }
            }
            Label {
                objectName: "agentAskSummary"
                visible: text.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                text: panel.askSummary(ask.item)
            }
            Label {
                objectName: "agentAskSetting"
                visible: ask.item.area === "setting" && SettingLabels.of(ask.item.setting ?? "").length === 0
                Layout.fillWidth: true
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
                font.family: "monospace"
                font.pixelSize: Math.round(Qt.application.font.pixelSize * 0.9)
                color: Theme.muted
                text: (ask.item.setting ?? "") + " → " + (ask.item.value ?? "")
            }
            DiffRows {
                visible: ask.open && ask.rows.length > 1
                rows: visible ? ask.rows : []
                document: ask.item.document
                rowName: "agentAskRow_"
            }
            Label {
                visible: ask.item.fromChild !== undefined && ask.open
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.muted
                text: qsTr("Only you can answer; the parent agent can’t.")
            }
            Label {
                objectName: "agentAskReason"
                visible: (ask.item.reason ?? "").length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                text: ask.item.type === "judge" ? qsTr("Why: %1").arg(ask.item.reason) : ask.item.reason
            }
            Details {
                visible: ask.open
                text: (ask.item.operation ?? "") + "\n" + (ask.item.params ?? "")
            }
            CardButtons {
                visible: ask.open && !ask.replying
                Button {
                    id: allowButton
                    objectName: "agentAllowOnce"
                    text: ask.item.type === "judge" ? qsTr("Allow") : qsTr("Allow once")
                    highlighted: true
                    onClicked: panel.agent.answer(ask.item.ask, "allowOnce", "")
                }
                Button {
                    objectName: "agentAllowInThread"
                    visible: !ask.always
                    text: qsTr("Allow in this thread")
                    onClicked: panel.agent.answer(ask.item.ask, "allowInThread", "")
                }
                Button {
                    objectName: "agentDeny"
                    text: qsTr("Deny")
                    onClicked: panel.agent.answer(ask.item.ask, "deny", "")
                }
                TellButton {
                    objectName: "agentAskTell"
                    visible: !ask.always
                    onClicked: {
                        ask.replying = true
                        askReply.begin()
                    }
                }
            }
            ReplyRow {
                id: askReply
                visible: ask.open && ask.replying
                fieldName: "agentAskReply"
                action: qsTr("Deny")
                onSent: text => panel.agent.answer(ask.item.ask, "deny", text)
                onCancelled: {
                    ask.replying = false
                    allowButton.forceActiveFocus()
                }
            }
            Label {
                visible: !ask.open
                color: Theme.muted
                text: ask.item.status === "allowed" ? qsTr("Allowed")
                      : ask.item.status === "allowedInThread" ? qsTr("Allowed in this thread")
                      : ask.item.status === "denied" ? qsTr("Denied")
                      : ask.item.withdrawnFor === "agent" ? qsTr("Withdrawn: the agent stopped waiting for your answer.")
                      : ask.item.withdrawnFor === "turnEnded" ? qsTr("Withdrawn: the turn ended before you answered.")
                      : ask.item.withdrawnFor === "stopped" ? qsTr("Withdrawn: you stopped the turn.")
                      : ask.item.withdrawnFor === "signedOut" ? qsTr("Withdrawn: you signed out of Codex.")
                      : ask.item.withdrawnFor === "cap" || ask.item.withdrawnFor === "dailyCap" ? qsTr("Withdrawn: the cost cap stopped the turn.")
                      : qsTr("No longer needed")
            }
        }
    }

    Component {
        id: scriptItem
        Card {
            id: lua
            readonly property var item: parent ? parent.item : ({})
            readonly property bool open: item.status === "open"
            readonly property bool running: item.status === "running"
            readonly property var lines: (item.script ?? "").replace(/\n$/, "").split("\n")
            property bool allLines: false
            property bool replying: false
            objectName: "agentScript"
            edge: open ? Theme.danger : item.status === "ran" ? Theme.success : running ? Theme.accent : Theme.line
            RowLayout {
                visible: lua.open
                Layout.fillWidth: true
                CardTitle {
                    iconRole: "run-script"
                    text: lua.item.fromChild !== undefined ? qsTr("%1 asks to run this Lua script").arg(lua.item.fromChild)
                                                           : qsTr("Run this Lua script?")
                }
                Chip { text: qsTr("asks in every mode"); tint: Theme.danger }
            }
            Label {
                visible: lua.open
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.muted
                textFormat: Text.PlainText
                text: [lua.item.macro ? qsTr("%1 · macro “%2”").arg(lua.item.name, lua.item.macro) : (lua.item.name ?? ""),
                       lua.item.title ? qsTr("on %1").arg(lua.item.title) : ""].filter(t => t.length > 0).join("   ")
            }
            Rectangle {
                visible: lua.open
                Layout.fillWidth: true
                implicitHeight: codeColumn.implicitHeight + 8
                color: Theme.field
                border.color: Theme.line
                radius: 2
                ColumnLayout {
                    id: codeColumn
                    x: 4
                    y: 4
                    width: parent.width - 8
                    spacing: 0
                    Repeater {
                        model: lua.open ? (lua.allLines ? lua.lines : lua.lines.slice(0, 12)) : []
                        delegate: RowLayout {
                            required property int index
                            required property string modelData
                            Layout.fillWidth: true
                            spacing: 8
                            Label {
                                Layout.preferredWidth: 24
                                horizontalAlignment: Text.AlignRight
                                font.family: "monospace"
                                color: Theme.muted
                                text: index + 1
                            }
                            Label {
                                Layout.fillWidth: true
                                font.family: "monospace"
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                text: modelData
                            }
                        }
                    }
                }
            }
            LinkButton {
                objectName: "agentScriptShowAll"
                iconRole: "chevron-down"
                visible: lua.open && !lua.allLines && lua.lines.length > 12
                text: qsTr("Show all %n lines", "", lua.lines.length)
                onClicked: lua.allLines = true
            }
            Repeater {
                model: lua.open ? (lua.item.needs ?? []) : []
                delegate: RowLayout {
                    required property var modelData
                    readonly property bool installed: modelData.installed !== undefined
                    Layout.fillWidth: true
                    spacing: 6
                    Icon { iconRole: parent.installed ? "karaoke-check" : "notice-warning"; size: 13 }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                        color: parent.installed ? Theme.success : Theme.warning
                        text: parent.installed ? [parent.modelData.module, parent.modelData.installed].filter(t => t.length > 0).join(" ")
                                               : qsTr("%1 isn’t installed, so the script will stop when it loads.").arg(parent.modelData.module)
                    }
                }
            }
            Label {
                visible: lua.open
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.muted
                text: qsTr("Runs as an Automation macro: one undo step, and Force stop while it runs. Saved in the AI scripts folder, never added to Autoload.")
            }
            CardButtons {
                visible: lua.open && !lua.replying
                Button {
                    id: runButton
                    objectName: "agentScriptRun"
                    text: qsTr("Run")
                    highlighted: true
                    onClicked: panel.agent.answer(lua.item.ask, "allowOnce", "")
                }
                Button {
                    objectName: "agentScriptDecline"
                    text: qsTr("Don’t run")
                    onClicked: panel.agent.answer(lua.item.ask, "deny", "")
                }
                TellButton {
                    objectName: "agentScriptTell"
                    onClicked: {
                        lua.replying = true
                        scriptReply.begin()
                    }
                }
            }
            ReplyRow {
                id: scriptReply
                visible: lua.open && lua.replying
                fieldName: "agentScriptReply"
                action: qsTr("Don’t run")
                onSent: text => panel.agent.answer(lua.item.ask, "deny", text)
                onCancelled: {
                    lua.replying = false
                    runButton.forceActiveFocus()
                }
            }
            RowLayout {
                visible: !lua.open
                Layout.fillWidth: true
                spacing: 6
                Icon { iconRole: lua.running ? "run-busy" : "run-script"; size: 14 }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    color: lua.item.status === "ran" || lua.item.status === "running" ? Theme.text : Theme.muted
                    text: lua.item.status === "running" ? qsTr("Running %1").arg(lua.item.name ?? "")
                          : lua.item.status === "ran" ? qsTr("Ran %1 · one undo step").arg(lua.item.name ?? "")
                          : lua.item.status === "failed" ? qsTr("%1 didn’t finish: %2").arg(lua.item.name ?? "", lua.item.result ?? "")
                          : lua.item.status === "declined" ? (lua.item.message ? qsTr("Didn’t run %1 · “%2”").arg(lua.item.name ?? "", lua.item.message)
                                                                               : qsTr("Didn’t run %1").arg(lua.item.name ?? ""))
                          : qsTr("No longer needed")
                }
                Button {
                    objectName: "agentScriptForceStop"
                    visible: lua.item.status === "running"
                    text: qsTr("Force stop")
                    onClicked: panel.agent.forceStopScript()
                }
                LinkButton {
                    objectName: "agentScriptOpen"
                    iconRole: "edit"
                    visible: (lua.item.file ?? "").length > 0
                    text: qsTr("Open")
                    onClicked: panel.agent.openScript(lua.item.file)
                }
            }
        }
    }

    Component {
        id: installItem
        Card {
            id: install
            readonly property var item: parent ? parent.item : ({})
            readonly property bool open: item.status === "open"
            readonly property bool installed: item.status === "installed"
            readonly property bool fullTrust: item.tier === "unrestricted"
            readonly property var files: item.files ?? []
            readonly property var mainLines: files.length > 0 ? files[0].text.replace(/\n$/, "").split("\n") : []
            property bool allFiles: false
            property bool replying: false
            objectName: "agentInstall"
            edge: open ? Theme.danger : item.status === "installed" ? Theme.success : item.status === "running" ? Theme.accent : Theme.line
            function trust() {
                if (item.needsRetrust && (item.keyId ?? "").length > 0)
                    return qsTr("The signing key changed. The installed copy was signed by key %1, and this one by key %2. Install it only if you trust that the author changed keys.")
                        .arg(item.pinnedKeyId, item.keyId)
                if (item.needsRetrust)
                    return qsTr("This package isn’t signed, but the installed copy was signed by key %1. Install it only if you trust where it came from.")
                        .arg(item.pinnedKeyId)
                if ((item.keyId ?? "").length > 0)
                    return qsTr("Unverified source, signed by key %1. Updates must be signed by the same key.").arg(item.keyId)
                return qsTr("Unverified: the package isn’t signed, so HikariSub can’t tell who made it.")
            }
            RowLayout {
                visible: install.open
                Layout.fillWidth: true
                CardTitle {
                    iconRole: install.fullTrust ? "plugin-full-access" : "plugin-install"
                    text: install.fullTrust ? qsTr("Install with full access?") : qsTr("Install this plugin?")
                }
                Chip { text: qsTr("asks in every mode"); tint: Theme.danger }
            }
            Label {
                visible: install.open
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.muted
                textFormat: Text.PlainText
                text: qsTr("%1 %2 · %3").arg(install.item.name ?? "", install.item.version ?? "", install.item.plugin ?? "")
            }
            Repeater {
                model: install.open ? [
                    { icon: install.fullTrust ? "plugin-full-access" : "plugin-sandbox", danger: install.fullTrust,
                      text: install.fullTrust ? qsTr("Full trust: it runs outside the sandbox and gets full access to your computer.")
                                              : qsTr("Restricted: it runs in the sandbox.") },
                    { icon: "notice-warning", danger: install.item.needsRetrust === true, text: install.trust() }
                ] : []
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 6
                    Icon { iconRole: parent.modelData.icon; size: 13 }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                        color: parent.modelData.danger ? Theme.danger : Theme.warning
                        text: parent.modelData.text
                    }
                }
            }
            Label {
                visible: install.open
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                text: (install.item.permissions ?? []).length === 0
                      ? qsTr("It asks for no permissions beyond the active subtitle file.")
                      : qsTr("It asks for these permissions")
            }
            Repeater {
                model: install.open ? (install.item.permissions ?? []) : []
                delegate: RowLayout {
                    required property int index
                    required property string modelData
                    objectName: "agentInstallPermission_" + index
                    Layout.fillWidth: true
                    Layout.leftMargin: 4
                    spacing: 6
                    Icon { iconRole: "notice-info"; size: 13; Layout.alignment: Qt.AlignTop }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                        text: parent.modelData
                    }
                }
            }
            Label {
                visible: install.open && (install.item.commands ?? []).length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                text: ((install.item.commands ?? []).length === 1 ? qsTr("Adds the command %1.") : qsTr("Adds the commands %1."))
                      .arg((install.item.commands ?? []).map(c => "“" + c + "”").join(", "))
            }
            Repeater {
                model: install.open ? (install.allFiles ? install.files : install.files.slice(0, 1)) : []
                delegate: ColumnLayout {
                    id: file
                    required property int index
                    required property var modelData
                    readonly property var lines: modelData.text.replace(/\n$/, "").split("\n")
                    Layout.fillWidth: true
                    spacing: 2
                    Label { font.family: "monospace"; color: Theme.muted; textFormat: Text.PlainText; text: file.modelData.path }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: fileLines.implicitHeight + 8
                        color: Theme.field
                        border.color: Theme.line
                        radius: 2
                        ColumnLayout {
                            id: fileLines
                            x: 4
                            y: 4
                            width: parent.width - 8
                            spacing: 0
                            Repeater {
                                model: install.allFiles ? file.lines : file.lines.slice(0, 12)
                                delegate: RowLayout {
                                    required property int index
                                    required property string modelData
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Label {
                                        Layout.preferredWidth: 24
                                        horizontalAlignment: Text.AlignRight
                                        font.family: "monospace"
                                        color: Theme.muted
                                        text: index + 1
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        font.family: "monospace"
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                        text: modelData
                                    }
                                }
                            }
                        }
                    }
                }
            }
            LinkButton {
                objectName: "agentInstallShowAll"
                iconRole: "chevron-down"
                visible: install.open && !install.allFiles && (install.files.length > 1 || install.mainLines.length > 12)
                text: qsTr("Show all %n files", "", install.files.length)
                onClicked: install.allFiles = true
            }
            Label {
                visible: install.open
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.muted
                text: qsTr("Installs as Install from file… would: it turns on, and Plugins can turn it off or uninstall it.")
            }
            CardButtons {
                visible: install.open && !install.replying
                Button {
                    id: installAccept
                    objectName: "agentInstallAccept"
                    visible: !install.fullTrust && !install.item.needsRetrust
                    text: qsTr("Install")
                    highlighted: true
                    onClicked: panel.agent.answer(install.item.ask, "allowOnce", "")
                }
                DangerButton {
                    id: installRisky
                    objectName: "agentInstallRisky"
                    visible: install.fullTrust || install.item.needsRetrust === true
                    text: install.item.needsRetrust ? qsTr("Trust and install") : qsTr("Install with full access")
                    onClicked: panel.agent.answer(install.item.ask, "allowOnce", "")
                }
                Button {
                    id: installDecline
                    objectName: "agentInstallDecline"
                    text: qsTr("Don’t install")
                    onClicked: panel.agent.answer(install.item.ask, "deny", "")
                }
                TellButton {
                    objectName: "agentInstallTell"
                    onClicked: {
                        install.replying = true
                        installReply.begin()
                    }
                }
            }
            ReplyRow {
                id: installReply
                visible: install.open && install.replying
                fieldName: "agentInstallReply"
                action: qsTr("Don’t install")
                onSent: text => panel.agent.answer(install.item.ask, "deny", text)
                onCancelled: {
                    install.replying = false
                    installDecline.forceActiveFocus()
                }
            }
            RowLayout {
                visible: !install.open
                Layout.fillWidth: true
                spacing: 6
                Icon { iconRole: install.installed ? "plugin-installed" : "plugin-install"; size: 14 }
                Label {
                    objectName: "agentInstallStatus"
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    color: install.item.status === "installed" || install.item.status === "running" ? Theme.text : Theme.muted
                    text: install.item.status === "running" ? qsTr("Installing %1").arg(install.item.name ?? "")
                          : install.item.status === "installed"
                            ? (install.fullTrust ? qsTr("Installed %1 %2 · full trust") : qsTr("Installed %1 %2 · restricted"))
                                  .arg(install.item.name ?? "", install.item.version ?? "")
                          : install.item.status === "failed" ? qsTr("%1 didn’t install: %2").arg(install.item.name ?? "", install.item.result ?? "")
                          : install.item.status === "declined" ? (install.item.message ? qsTr("Didn’t install %1 · “%2”").arg(install.item.name ?? "", install.item.message)
                                                                                       : qsTr("Didn’t install %1").arg(install.item.name ?? ""))
                          : qsTr("No longer needed")
                }
                LinkButton {
                    objectName: "agentInstallOpen"
                    iconRole: "plugins"
                    visible: install.item.status === "installed"
                    text: qsTr("Open in Plugins")
                    onClicked: panel.pluginRequested(install.item.plugin)
                }
            }
        }
    }

    Component {
        id: compactionItem
        ColumnLayout {
            id: compaction
            readonly property var item: parent ? parent.item : ({})
            readonly property bool hasSummary: (item.summary ?? "").length > 0
            property bool open: false
            objectName: "agentCompaction"
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line }
                ItemDelegate {
                    objectName: "agentCompactionToggle"
                    padding: 2
                    enabled: compaction.hasSummary
                    onClicked: compaction.open = !compaction.open
                    Accessible.name: divider.text
                    contentItem: RowLayout {
                        spacing: 4
                        Icon {
                            size: 12
                            visible: compaction.hasSummary
                            iconRole: compaction.open ? "chevron-down" : "chevron-right"
                        }
                        Label {
                            id: divider
                            color: Theme.muted
                            text: compaction.item.from !== undefined
                                  ? qsTr("Carried on from the summary of “%1”").arg(compaction.item.from)
                                  : qsTr("Context compacted %1 → %2 tokens")
                                    .arg(panel.tokens(compaction.item.before ?? 0), panel.tokens(compaction.item.after ?? 0))
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line }
            }
            Rectangle {
                objectName: "agentCompactionSummary"
                visible: compaction.open
                Layout.fillWidth: true
                implicitHeight: summaryText.implicitHeight + 16
                radius: 4
                color: Theme.field
                border.color: Theme.line
                Label {
                    id: summaryText
                    x: 8
                    y: 8
                    width: parent.width - 16
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    text: (compaction.item.summary ?? "").replace(/^#+[ \t]*/gm, "")
                }
            }
        }
    }

    Component {
        id: noticeItem
        Card {
            id: notice
            readonly property var item: parent ? parent.item : ({})
            readonly property bool last: parent ? parent.last : false
            readonly property string kind: item.notice ?? ""
            readonly property bool quiet: kind === "stopped" || kind === "parent" || kind === "signedOut" || kind === "maxTokens" || kind === "refusal" || kind === "maxRequests"
                                          || kind === "nothingToCompact"
            objectName: "agentNotice_" + kind
            readonly property bool searchFailed: kind === "searchUsedUp" || kind === "searchKey"
            edge: quiet ? Theme.line : kind === "cap" || kind === "dailyCap" || searchFailed ? Theme.warning : Theme.danger
            CardTitle {
                iconRole: notice.quiet ? "notice-info" : notice.edge === Theme.warning ? "notice-warning" : "notice-error"
                text: notice.kind === "cap" ? qsTr("This thread reached its %1 cap").arg(panel.money(panel.thread.cap ?? 0))
                      : notice.kind === "dailyCap" ? qsTr("Today’s %1 cap is reached").arg(panel.money(panel.agent.dailyCap))
                      : notice.kind === "lost" ? qsTr("AI component stopped")
                      : notice.kind === "failed" ? qsTr("Turn failed")
                      : notice.kind === "stopped" ? qsTr("You stopped the turn.")
                      : notice.kind === "parent" ? qsTr("The parent thread stopped this sub-agent.")
                      : notice.kind === "signedOut" ? qsTr("Signed out of Codex. This session ended.")
                      : notice.kind === "maxTokens" ? qsTr("The answer reached the model’s length limit.")
                      : notice.kind === "refusal" ? qsTr("The model declined to answer.")
                      : notice.kind === "maxRequests" ? qsTr("The turn stopped after its most model requests.")
                      : notice.kind === "noModel" ? qsTr("Choose a chat model in Settings › AI")
                      : notice.kind === "notOffered" ? qsTr("This agent can’t start here")
                      : notice.kind === "compactFailed" ? qsTr("Couldn’t compact the conversation")
                      : notice.kind === "nothingToCompact" ? qsTr("There’s nothing to compact yet.")
                      : notice.kind === "searchUsedUp" ? qsTr("%1 has no searches left").arg(panel.serviceName(notice.item.service ?? ""))
                      : notice.kind === "searchKey" ? qsTr("%1 refused HikariSub’s key").arg(panel.serviceName(notice.item.service ?? ""))
                      : panel.componentResult === "ready" ? qsTr("AI component is ready")
                      : qsTr("AI component isn’t ready")
            }
            Label {
                visible: text.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.muted
                text: notice.kind === "cap" && !panel.isChild ? qsTr("The turn stopped after its last change. Raise the cap for this thread to go on.")
                      : notice.kind === "cap" ? qsTr("The parent’s cost cap stopped this sub-agent. Its changes stay in the review.")
                      : notice.kind === "lost" && panel.isChild ? qsTr("The sub-agent was interrupted and won’t rerun. The parent got its partial result. Changes it made stay in the review.")
                      : notice.kind === "dailyCap" ? qsTr("Every thread stops until tomorrow, or until you raise the cap in Settings › AI.")
                      : notice.kind === "lost" ? qsTr("The turn was interrupted and won’t rerun by itself. Changes it made stay in the review.")
                      : notice.kind === "failed" ? (notice.item.detail ?? "")
                      : notice.kind === "compactFailed" ? qsTr("The summary request failed twice (%1). Nothing was removed.").arg(notice.item.detail ?? "")
                      : notice.kind === "noSidecar" && panel.download.active
                        ? qsTr("Getting the AI component: %1 of %2 MB").arg(Math.round((panel.download.received || 0) / 1000000), Math.round((panel.download.size || 0) / 1000000))
                      : notice.kind === "noSidecar" && panel.componentResult === "ready" ? qsTr("The AI component is ready. Send your message again.")
                      : notice.kind === "noSidecar" && panel.componentResult === "failed" ? qsTr("The AI component didn’t download. Check your connection and try again.")
                      : notice.kind === "noSidecar" ? qsTr("Agents need the AI component, which HikariSub downloads once.")
                      : notice.kind === "notOffered" && notice.item.detail === "project"
                        ? qsTr("Not offered: its command is inside an open project, which it could read.")
                      : notice.kind === "notOffered" && notice.item.detail === "channel"
                        ? qsTr("Not offered: HikariSub can’t open a private tools channel for it on this system.")
                      : notice.kind === "notOffered" ? qsTr("Not offered: this system can’t keep it to its own folders.")
                      : notice.searchFailed ? qsTr("The search service answered %1. The answer goes on without searching the web.").arg(notice.item.status ?? 0)
                      : ""
            }
            RowLayout {
                visible: notice.searchFailed || (notice.last && !panel.running && !notice.quiet)
                spacing: 6
                Button {
                    objectName: "agentRaiseCap"
                    visible: notice.kind === "cap" && !panel.isChild
                    highlighted: true
                    text: qsTr("Raise to %1 and continue").arg(panel.money(Math.max(panel.thread.cap ?? 0, panel.thread.cost ?? 0) + panel.agent.threadCap))
                    onClicked: {
                        panel.agent.raiseCap()
                        panel.agent.send(qsTr("Continue."))
                    }
                }
                Button {
                    objectName: "agentRetry"
                    visible: (notice.kind === "lost" || notice.kind === "failed") && !panel.isChild
                    highlighted: true
                    text: qsTr("Retry the turn")
                    onClicked: panel.agent.retry()
                }
                Button {
                    objectName: "agentRetryCompact"
                    visible: notice.kind === "compactFailed"
                    highlighted: true
                    text: qsTr("Retry")
                    onClicked: panel.agent.compact("")
                }
                Button {
                    objectName: "agentSummaryThread"
                    visible: notice.kind === "compactFailed"
                    text: qsTr("New thread from summary")
                    onClicked: panel.agent.newThreadFromSummary()
                }
                Button {
                    visible: notice.kind === "lost"
                    text: qsTr("Show the log")
                    onClicked: panel.logRequested()
                }
                Button {
                    objectName: "agentGetComponent"
                    visible: notice.kind === "noSidecar" && !!panel.aiSettings && !panel.download.active && panel.componentResult !== "ready"
                    highlighted: true
                    text: qsTr("Get the AI component")
                    onClicked: {
                        panel.componentResult = ""
                        panel.aiSettings.installComponent()
                    }
                }
                Button {
                    visible: notice.kind === "dailyCap" || notice.kind === "noModel" || notice.kind === "noSidecar"
                             || notice.kind === "notOffered" || notice.searchFailed
                    text: qsTr("Open Settings › AI")
                    onClicked: panel.settingsRequested()
                }
            }
        }
    }

    Component {
        id: tasksItem
        Card {
            id: tasks
            readonly property var item: parent ? parent.item : ({})
            readonly property var kids: item.children ?? []
            readonly property int needCount: kids.filter(k => k.status === "needs_you").length
            readonly property int runningCount: kids.filter(k => k.status === "running").length + needCount
            objectName: "agentTasks"
            edge: needCount > 0 ? Theme.warning : Theme.line
            RowLayout {
                Layout.fillWidth: true
                CardTitle {
                    iconRole: "ai-assistant"
                    text: [qsTr("%n sub-agent(s)", "", tasks.kids.length),
                           tasks.runningCount > 0 ? qsTr("%n running", "", tasks.runningCount) : "",
                           tasks.needCount > 0 ? qsTr("%n need(s) you", "", tasks.needCount) : ""]
                          .filter(s => s.length > 0).join(" · ")
                }
                IconTextButton {
                    objectName: "agentStopChildren"
                    visible: tasks.runningCount > 0
                    iconRole: "media-stop"
                    text: qsTr("Stop all")
                    onClicked: panel.agent.stopChildren()
                }
            }
            Repeater {
                model: tasks.kids
                delegate: Rectangle {
                    id: kid
                    required property var modelData
                    readonly property bool live: modelData.status === "running" || modelData.status === "needs_you"
                    objectName: "agentTask_" + modelData.task
                    Layout.fillWidth: true
                    implicitHeight: kidColumn.implicitHeight + 8
                    radius: 3
                    color: "transparent"
                    border.color: activeFocus ? Theme.focus : modelData.status === "needs_you" ? Theme.warning : Theme.line
                    border.width: activeFocus ? 2 : 1
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Grouping
                    Accessible.name: qsTr("Sub-agent %1, %2").arg(modelData.title, panel.taskStateName(modelData))
                    Keys.onReturnPressed: panel.agent.open(modelData.id)
                    Keys.onEnterPressed: panel.agent.open(modelData.id)
                    ColumnLayout {
                        id: kidColumn
                        x: 6
                        y: 4
                        width: parent.width - 12
                        spacing: 1
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                font.bold: true
                                text: kid.modelData.title
                            }
                            Label {
                                color: Theme.muted
                                text: kid.modelData.costUnknown && kid.modelData.cost === 0 ? qsTr("cost not reported")
                                                                                            : panel.money(kid.modelData.cost)
                            }
                            Button {
                                objectName: "agentOpenTask"
                                text: qsTr("Open")
                                activeFocusOnTab: false
                                onClicked: panel.agent.open(kid.modelData.id)
                            }
                            Button {
                                objectName: "agentStopTask"
                                visible: kid.live
                                text: qsTr("Stop")
                                activeFocusOnTab: false
                                onClicked: panel.agent.stopChild(kid.modelData.id)
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label {
                                color: panel.taskStateTint(kid.modelData.status)
                                text: panel.taskStateName(kid.modelData)
                            }
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                color: Theme.muted
                                text: " · " + [kid.modelData.target, panel.scopeName(kid.modelData.document, kid.modelData.lines)]
                                      .filter(s => s.length > 0).join(" · ")
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: estimateItem
        Card {
            id: estimate
            readonly property var item: parent ? parent.item : ({})
            readonly property bool open: item.status === "open"
            objectName: "agentEstimate"
            edge: open ? Theme.warning : Theme.line
            CardTitle {
                iconRole: "notice-info"
                text: qsTr("Estimate for %n sub-agent(s)", "", estimate.item.children ?? 0)
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: estimate.item.cost !== undefined
                      ? qsTr("About %1 tokens, %2.").arg(panel.tokens(estimate.item.tokens ?? 0), panel.money(estimate.item.cost))
                      : qsTr("About %1 tokens. This agent doesn’t report cost.").arg(panel.tokens(estimate.item.tokens ?? 0))
            }
            RowLayout {
                visible: estimate.open
                spacing: 6
                Button {
                    objectName: "agentRunEstimate"
                    text: qsTr("Run")
                    highlighted: true
                    onClicked: panel.agent.answer(estimate.item.ask, "allowOnce", "")
                }
                Button {
                    objectName: "agentCancelEstimate"
                    text: qsTr("Cancel")
                    onClicked: panel.agent.answer(estimate.item.ask, "deny", "")
                }
            }
            Label {
                visible: !estimate.open
                color: Theme.muted
                text: estimate.item.status === "started" ? qsTr("Started")
                      : estimate.item.status === "declined" ? qsTr("Cancelled. No more sub-agents start this turn.")
                      : qsTr("No longer needed")
            }
        }
    }
}
