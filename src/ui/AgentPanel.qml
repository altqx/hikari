pragma Translator: "HikariSub.Agent"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

FocusScope {
    id: panel
    required property QtObject agent
    signal reviewRequested()
    signal restoreRequested(var turn)
    signal settingsRequested()
    signal logRequested()
    signal exportRequested(string id)

    readonly property var thread: agent ? agent.thread : ({})
    readonly property bool hasThread: thread.id !== undefined
    readonly property bool running: hasThread && thread.running
    readonly property string mode: hasThread ? thread.mode : "auto"
    property bool showingThreads: false
    objectName: "agentView"

    function tokens(n) {
        return n >= 1000 ? Number(n / 1000).toLocaleString(Qt.locale(), "f", n >= 100000 ? 0 : 1) + "K"
                         : Number(n).toLocaleString(Qt.locale(), "f", 0)
    }
    function money(v) { return "$" + Number(v).toLocaleString(Qt.locale(), "f", 2) }
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
    function send() {
        const text = composer.text.trim()
        if (text.length === 0)
            return
        if (text === "/new") {
            panel.agent.newThread()
        } else if (text === "/mode") {
            modeMenu.popup(modeButton, 0, -modeMenu.implicitHeight)
        } else {
            panel.agent.send(text)
        }
        composer.text = ""
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
                objectName: "agentThreadsButton"
                iconRole: "history"
                text: qsTr("Threads")
                checkable: true
                checked: panel.showingThreads
                visible: panel.agent.ready
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
                iconRole: "menu-more"
                text: qsTr("Thread menu")
                visible: panel.agent.ready && !panel.showingThreads
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
            }
            ListView {
                id: threadList
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
                    text: section === "today" ? qsTr("Today") : section === "yesterday" ? qsTr("Yesterday")
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

        ListView {
            id: timeline
            objectName: "agentTimeline"
            visible: panel.agent.ready && !panel.showingThreads
            Layout.fillWidth: true
            Layout.fillHeight: true
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
                               : kind === "notice" ? noticeItem : null
                readonly property bool last: index === timeline.count - 1
            }
        }

        Rectangle {
            objectName: "agentQueued"
            visible: panel.agent.ready && !panel.showingThreads && panel.hasThread && panel.thread.queued.length > 0
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
            ProgressBar {
                visible: panel.hasThread && panel.thread.contextWindow > 0
                from: 0
                to: 1
                value: parent.used
                Layout.preferredWidth: 48
            }
            Label {
                Layout.fillWidth: true
                color: Theme.muted
                elide: Text.ElideRight
                text: !panel.hasThread ? ""
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

        Rectangle {
            id: composerBox
            visible: !panel.showingThreads
            Layout.fillWidth: true
            Layout.margins: 8
            implicitHeight: composerColumn.implicitHeight + 12
            radius: 4
            color: Theme.field
            border.color: composer.activeFocus ? Theme.focus : Theme.line
            border.width: composer.activeFocus ? 2 : 1
            enabled: panel.agent.ready
            ColumnLayout {
                id: composerColumn
                x: 6
                y: 6
                width: parent.width - 12
                spacing: 4
                Flow {
                    Layout.fillWidth: true
                    spacing: 4
                    Chip {
                        objectName: "agentActiveTab"
                        visible: panel.agent.activeTitle.length > 0
                        text: qsTr("%1 · active tab").arg(panel.agent.activeTitle)
                        tint: Theme.accent
                    }
                    Repeater {
                        model: panel.hasThread ? panel.thread.attached : []
                        delegate: Chip {
                            required property var modelData
                            text: modelData.title + "  ×"
                            tint: Theme.text
                            TapHandler { onTapped: panel.agent.detach(parent.modelData.id) }
                        }
                    }
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(composer.implicitHeight, 140)
                    TextArea {
                        id: composer
                        objectName: "agentComposer"
                        focus: true
                        wrapMode: TextEdit.Wrap
                        placeholderText: panel.agent.ready ? qsTr("Ask, or type @ to attach, / for commands")
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
                            if (text.endsWith("@") && attachMenu.count > 0)
                                attachMenu.popup(composer, 0, composer.height)
                            if (text === "/")
                                slashMenu.popup(composer, 0, composer.height)
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    IconToolButton {
                        objectName: "agentAttach"
                        iconRole: "add"
                        text: qsTr("Attach")
                        enabled: attachMenu.count > 0
                        onClicked: attachMenu.popup(this, 0, -attachMenu.implicitHeight)
                    }
                    Button {
                        id: modelButton
                        objectName: "agentModelButton"
                        flat: true
                        text: (panel.hasThread ? panel.thread.modelText : "") + "  ⌄"
                        onClicked: modelMenu.popup(this, 0, -modelMenu.implicitHeight)
                    }
                    Button {
                        id: modeButton
                        objectName: "agentModeButton"
                        flat: true
                        text: panel.modeName(panel.mode) + "  ⌄"
                        palette.buttonText: panel.mode === "full" ? Theme.danger : Theme.text
                        onClicked: modeMenu.popup(this, 0, -modeMenu.implicitHeight)
                    }
                    Item { Layout.fillWidth: true }
                    IconToolButton {
                        objectName: panel.running ? "agentStop" : "agentSend"
                        iconRole: panel.running && composer.text.trim().length === 0 ? "media-stop" : "ai-send"
                        text: panel.running && composer.text.trim().length === 0 ? qsTr("Stop") : qsTr("Send")
                        enabled: panel.running || composer.text.trim().length > 0
                        onClicked: panel.running && composer.text.trim().length === 0 ? panel.agent.stop() : panel.send()
                    }
                }
            }
        }
    }

    ShellMenu {
        id: threadMenu
        ShellMenuItem {
            objectName: "agentRenameThread"
            iconRole: "edit"
            text: qsTr("Rename thread…")
            onTriggered: renameLoader.active = true
        }
        ShellMenuItem {
            iconRole: "export"
            text: qsTr("Export as Markdown…")
            onTriggered: panel.exportRequested(panel.thread.id)
        }
        MenuSeparator {}
        ShellMenuItem {
            iconRole: "delete"
            text: qsTr("Delete thread")
            onTriggered: deleteQuestion.ask({
                title: qsTr("Delete this thread?"),
                severity: "warning",
                text: qsTr("The conversation goes. Changes it made stay in your subtitles and in the review."),
                choices: [{ id: "delete", text: qsTr("Delete"), role: "destructive" },
                          { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }]
            }, id => {
                if (id === "delete")
                    panel.agent.remove(panel.thread.id)
            })
        }
    }

    ShellMenu {
        id: attachMenu
        Repeater {
            model: panel.agent.attachable
            delegate: ShellMenuItem {
                required property var modelData
                text: modelData.title
                iconRole: "open-subtitles"
                onTriggered: {
                    panel.agent.attach(modelData.id)
                    if (composer.text.endsWith("@"))
                        composer.text = composer.text.slice(0, -1)
                    composer.forceActiveFocus()
                }
            }
        }
    }

    ShellMenu {
        id: slashMenu
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
                modeMenu.popup(modeButton, 0, -modeMenu.implicitHeight)
            }
        }
    }

    ShellMenu {
        id: modelMenu
        Repeater {
            model: panel.agent.models
            delegate: ShellMenuItem {
                required property var modelData
                text: modelData.text + "   " + modelData.provider
                checkable: true
                checked: panel.hasThread && panel.thread.model === modelData.value
                onTriggered: {
                    if (!panel.hasThread)
                        panel.agent.newThread()
                    panel.agent.setModel(modelData.value)
                }
            }
        }
        MenuSeparator {}
        ShellMenuItem {
            iconRole: "settings"
            text: qsTr("Settings › AI…")
            onTriggered: panel.settingsRequested()
        }
    }

    ShellMenu {
        id: modeMenu
        objectName: "agentModeMenu"
        width: 300
        Repeater {
            model: [{id: "manual", name: qsTr("Manual"), detail: qsTr("Subtitle edits go into the review, where you keep or undo them. Other changes ask.")},
                    {id: "auto", name: qsTr("Auto"), detail: qsTr("The judge model approves each action, or asks you. It sees the action and your messages, never your files.")},
                    {id: "full", name: qsTr("Full access"), detail: qsTr("Nothing asks except the always-ask list. Edits apply at once and stay undoable.")}]
            delegate: ShellMenuItem {
                id: modeItem
                required property var modelData
                objectName: "agentMode_" + modelData.id
                checkable: true
                checked: panel.mode === modelData.id
                text: modelData.name
                contentItem: ColumnLayout {
                    spacing: 1
                    Label {
                        text: modeItem.modelData.name
                        font.bold: true
                        color: modeItem.modelData.id === "full" ? Theme.danger : Theme.text
                    }
                    Label {
                        text: modeItem.modelData.detail
                        color: Theme.muted
                        wrapMode: Text.Wrap
                        Layout.preferredWidth: 250
                    }
                }
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
            text: qsTr("Default for new threads…")
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
            onAccepted: panel.agent.rename(panel.thread.id, renameField.text)
            onVisibleChanged: if (!visible) Qt.callLater(() => renameLoader.active = false)
            Component.onCompleted: {
                renameField.text = panel.thread.title
                open()
            }
            TextField {
                id: renameField
                objectName: "agentRenameField"
                Accessible.name: qsTr("Thread name")
                Layout.preferredWidth: 280
            }
            buttons: [
                PrimaryButton {
                    id: renameOk
                    objectName: "agentRenameOk"
                    text: qsTr("Rename")
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
            spacing: 2
            Label {
                Layout.alignment: Qt.AlignRight
                Layout.maximumWidth: parent.width * 0.85
                text: parent.item.text ?? ""
                wrapMode: Text.Wrap
                padding: 8
                background: Rectangle { radius: 6; color: Theme.select }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 6
                Label {
                    color: Theme.muted
                    text: (parent.parent.item.steered ? qsTr("steered") + " · " : "")
                          + (parent.parent.item.time ? Qt.formatTime(new Date(parent.parent.item.time), Locale.ShortFormat) : "")
                }
                LinkButton {
                    visible: (parent.parent.item.turn ?? 0) > 0
                    iconRole: "checkpoint"
                    text: qsTr("Restore checkpoint")
                    onClicked: panel.restoreRequested(parent.parent.item.turn)
                }
            }
        }
    }

    Component {
        id: textItem
        TextEdit {
            readonly property var row: parent
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            color: Theme.text
            selectionColor: Theme.select
            text: (row ? row.item.text : "") + (row && row.last && panel.running ? " ▍" : "")
        }
    }

    Component {
        id: toolItem
        ColumnLayout {
            id: tool
            readonly property var item: parent ? parent.item : ({})
            readonly property bool failed: item.status === "failed"
            readonly property bool busy: item.status === "running"
            property bool open: false
            spacing: 2
            ItemDelegate {
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
                visible: tool.open
                text: JSON.stringify(tool.item.input ?? {})
            }
            Mono {
                visible: tool.open && (tool.item.output ?? "").length > 0
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

    Component {
        id: askItem
        Card {
            id: ask
            readonly property var item: parent ? parent.item : ({})
            readonly property bool open: item.status === "open"
            readonly property bool always: item.type === "always"
            property bool replying: false
            objectName: "agentAsk"
            edge: item.type === "always" ? Theme.danger : Theme.warning
            RowLayout {
                Layout.fillWidth: true
                CardTitle {
                    iconRole: ask.always ? "notice-error" : "notice-warning"
                    text: ask.item.type === "judge" ? qsTr("Judge asks you")
                          : ask.item.type === "always" ? qsTr("Allow %1?").arg(ask.item.tool ?? "")
                          : qsTr("Run %1?").arg(ask.item.tool ?? "")
                }
                Chip {
                    visible: ask.item.type === "always"
                    text: qsTr("asks in every mode")
                    tint: Theme.danger
                }
            }
            Mono { text: ask.item.params ?? "" }
            Label {
                visible: (ask.item.reason ?? "").length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: ask.item.type === "judge" ? qsTr("Why: %1").arg(ask.item.reason) : ask.item.reason
            }
            RowLayout {
                visible: ask.open && !ask.replying
                spacing: 6
                Button {
                    objectName: "agentAllowOnce"
                    text: ask.item.type === "judge" ? qsTr("Allow") : qsTr("Allow once")
                    highlighted: true
                    onClicked: panel.agent.answer(ask.item.ask, "allowOnce", "")
                }
                Button {
                    objectName: "agentAllowInThread"
                    visible: ask.item.type !== "always"
                    text: qsTr("Allow in this thread")
                    onClicked: panel.agent.answer(ask.item.ask, "allowInThread", "")
                }
                Button {
                    objectName: "agentDeny"
                    text: qsTr("Deny")
                    onClicked: panel.agent.answer(ask.item.ask, "deny", "")
                }
                Button {
                    visible: ask.item.type !== "always"
                    text: qsTr("Deny and tell the agent…")
                    onClicked: ask.replying = true
                }
            }
            RowLayout {
                visible: ask.open && ask.replying
                Layout.fillWidth: true
                TextField {
                    id: reply
                    Layout.fillWidth: true
                    placeholderText: qsTr("What should it do instead?")
                    onAccepted: panel.agent.answer(ask.item.ask, "deny", text)
                }
                Button { text: qsTr("Deny"); onClicked: panel.agent.answer(ask.item.ask, "deny", reply.text) }
            }
            Label {
                visible: !ask.open
                color: Theme.muted
                text: ask.item.status === "allowed" ? qsTr("Allowed")
                      : ask.item.status === "allowedInThread" ? qsTr("Allowed in this thread")
                      : ask.item.status === "denied" ? qsTr("Denied") : qsTr("No longer needed")
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
            readonly property bool quiet: kind === "stopped" || kind === "maxTokens" || kind === "refusal" || kind === "maxRequests"
            objectName: "agentNotice_" + kind
            edge: quiet ? Theme.line : kind === "cap" || kind === "dailyCap" ? Theme.warning : Theme.danger
            CardTitle {
                iconRole: notice.quiet ? "notice-info" : notice.edge === Theme.warning ? "notice-warning" : "notice-error"
                text: notice.kind === "cap" ? qsTr("This thread reached its %1 cap").arg(panel.money(panel.thread.cap ?? 0))
                      : notice.kind === "dailyCap" ? qsTr("Today’s %1 cap is reached").arg(panel.money(panel.agent.dailyCap))
                      : notice.kind === "lost" ? qsTr("AI component stopped")
                      : notice.kind === "failed" ? qsTr("Turn failed")
                      : notice.kind === "stopped" ? qsTr("You stopped the turn.")
                      : notice.kind === "maxTokens" ? qsTr("The answer reached the model’s length limit.")
                      : notice.kind === "refusal" ? qsTr("The model declined to answer.")
                      : notice.kind === "maxRequests" ? qsTr("The turn stopped after its most model requests.")
                      : notice.kind === "noModel" ? qsTr("Choose a chat model in Settings › AI")
                      : qsTr("AI component isn’t ready")
            }
            Label {
                visible: text.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.muted
                text: notice.kind === "cap" ? qsTr("The turn stopped after its last change. Raise the cap for this thread to go on.")
                      : notice.kind === "dailyCap" ? qsTr("Every thread stops until tomorrow, or until you raise the cap in Settings › AI.")
                      : notice.kind === "lost" ? qsTr("The turn was interrupted and won’t rerun by itself. Changes it made stay in the review.")
                      : notice.kind === "failed" ? (notice.item.detail ?? "")
                      : notice.kind === "noSidecar" ? qsTr("HikariSub is still getting the AI component, or it couldn’t start.")
                      : ""
            }
            RowLayout {
                visible: notice.last && !panel.running && !notice.quiet
                spacing: 6
                Button {
                    objectName: "agentRaiseCap"
                    visible: notice.kind === "cap"
                    highlighted: true
                    text: qsTr("Raise to %1 and continue").arg(panel.money(Math.max(panel.thread.cap ?? 0, panel.thread.cost ?? 0) + panel.agent.threadCap))
                    onClicked: {
                        panel.agent.raiseCap()
                        panel.agent.send(qsTr("Continue."))
                    }
                }
                Button {
                    objectName: "agentRetry"
                    visible: notice.kind === "lost" || notice.kind === "failed"
                    highlighted: true
                    text: qsTr("Retry the turn")
                    onClicked: panel.agent.retry()
                }
                Button {
                    visible: notice.kind === "lost"
                    text: qsTr("Show the log")
                    onClicked: panel.logRequested()
                }
                Button {
                    visible: notice.kind === "dailyCap" || notice.kind === "noModel" || notice.kind === "noSidecar"
                    text: qsTr("Open Settings › AI")
                    onClicked: panel.settingsRequested()
                }
            }
        }
    }
}
