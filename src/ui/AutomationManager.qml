pragma Translator: "HikariSub.Automation"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Dialogs as Dialogs
import Hikari.Ui

Pane {
    id: root
    required property AutomationManagerController controller
    property var automation: null
    property var macroHotkeys: null
    property var hotkeys: null
    property Action loadAction: null
    property Action refreshAction: null
    property string documentName: ""
    property var settings: null
    property var notices: null
    property int documentTab: -1
    implicitWidth: 340
    implicitHeight: 420
    padding: 0
    readonly property size minimumSize: Qt.size(240, 160)

    property string recording: ""
    property var pressed: null
    property string refusal: ""
    property string blocked: ""
    property var folded: ({ missing: true })
    property string view: "scripts"
    readonly property AutomationRuns runs: automation ? automation.runs : null
    function showRun(id) {
        view = "runs"
        runList.choose(id)
    }
    readonly property bool running: automation !== null && automation.running
    readonly property string filterText: filter.text.trim().toLowerCase()

    property var offeredScripts: []
    function readDocumentScripts() {
        offeredScripts = automation && visible ? automation.documentScriptsToLoad() : []
    }
    function fileName(path) {
        return path.replace(/^.*[\\/]/, "")
    }
    readonly property int offerNamed: 8
    readonly property int offerTipPaths: 30
    function offerText() {
        const extra = offeredScripts.length - offerNamed
        const names = offeredScripts.slice(0, offerNamed).map(fileName).join(", ")
        return extra > 0 ? qsTr("This file names scripts: %1 and %n more", "", extra).arg(names)
                         : qsTr("This file names scripts: %1").arg(names)
    }
    function offerTip() {
        const extra = offeredScripts.length - offerTipPaths
        const paths = offeredScripts.slice(0, offerTipPaths).join("\n")
        return extra > 0 ? paths + "\n" + qsTr("and %n more", "", extra) : paths
    }
    // MUST KILL: the reads wait a callLater because the tab and its name change before the editing target
    onVisibleChanged: Qt.callLater(readDocumentScripts)
    onDocumentNameChanged: Qt.callLater(readDocumentScripts)
    onDocumentTabChanged: Qt.callLater(readDocumentScripts)
    Connections {
        target: root.controller
        function onChanged() { Qt.callLater(root.readDocumentScripts) }
    }

    function runnable(row) {
        return row.script.state === "ready" && !root.controller.busy
    }

    function record(legacyName) {
        const indexOf = () => rows.findIndex(r => r.kind === "macro" && r.legacyName === legacyName)
        let i = indexOf()
        if (i < 0) {
            filter.text = ""
            folded = ({ missing: folded.missing === true })
            i = indexOf()
        }
        if (i < 0)
            return
        recording = legacyName
        pressed = null
        blocked = macroHotkeys ? macroHotkeys.recordingProblem(legacyName) : ""
        refusal = blocked
        list.currentIndex = i
        list.positionViewAtIndex(i, ListView.Contain)
        Qt.callLater(() => recorder.forceActiveFocus(Qt.OtherFocusReason))
    }
    function stopRecording() {
        recording = ""
        pressed = null
        blocked = ""
        refusal = ""
        reassign.close()
        list.forceActiveFocus(Qt.OtherFocusReason)
    }
    function saveRecording() {
        if (!hotkeys || !pressed || blocked.length > 0)
            return
        const target = hotkeys.gestureTarget(recording)
        const conflict = hotkeys.gestureConflict(target.id, pressed.accel, 0)
        if (conflict.message === undefined || hotkeys.holdersOf(pressed.accel, target.id).length === 0) {
            hotkeys.gestureMap(target.id, target.name, pressed.accel, 0, "cancel")
            stopRecording()
        } else {
            reassign.ask(target, conflict)
        }
    }
    function legacyNameOf(script, macro) {
        return macroHotkeys ? macroHotkeys.legacyNameFor(script.path, macro.ordinal)
                            : script.fileName + "-" + macro.ordinal
    }
    function keysOf(legacyName) {
        return macroHotkeys ? macroHotkeys.keys[legacyName] ?? "" : ""
    }
    function scriptTitle(script) {
        return script.name !== "" ? script.name : script.fileName
    }
    // Lua names a string chunk [string "<source>"], cut to "..." when long.
    function shownError(script) {
        return (script.error ?? "").replace(/\[string "([^"]*)"\]/g, (chunk, name) => {
            const head = name.endsWith("...") ? name.slice(0, -3) : name
            return head !== "" && (script.path ?? "").startsWith(head) ? script.fileName : chunk
        })
    }
    function matches(text) {
        return filterText.length === 0 || text.toLowerCase().includes(filterText)
    }

    readonly property var rows: {
        const out = []
        const scripts = controller.scripts
        const sections = [
            { key: "document", title: qsTr("Subtitle scripts"), inDocument: true },
            { key: "autoload", title: qsTr("Autoload"), inDocument: false }
        ]
        for (const section of sections) {
            const members = []
            for (let i = 0; i < scripts.length; ++i) {
                const script = scripts[i]
                if (script.inDocument !== section.inDocument)
                    continue
                const named = matches(scriptTitle(script))
                const macros = script.macros.filter(m => named || matches(m.name))
                if (named || macros.length > 0)
                    members.push({ script: script, index: i, macros: macros })
            }
            if (members.length === 0)
                continue
            out.push({ kind: "section", key: section.key, title: section.title, count: members.length })
            if (folded[section.key])
                continue
            for (const m of members) {
                out.push({ kind: "script", script: m.script, index: m.index })
                for (const macro of m.macros)
                    out.push({ kind: "macro", script: m.script, index: m.index, macro: macro,
                               legacyName: legacyNameOf(m.script, macro) })
            }
        }
        const missing = macroHotkeys ? macroHotkeys.missing.filter(b => matches(b.macro) || matches(b.legacyName)) : []
        if (missing.length > 0) {
            out.push({ kind: "section", key: "missing", title: qsTr("Shortcuts for missing macros"), count: missing.length })
            if (!folded.missing)
                for (const b of missing)
                    out.push({ kind: "missing", binding: b })
        }
        return out
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            Layout.bottomMargin: 0
            spacing: 4
            SegmentedButtons {
                objectName: "automationView"
                choices: [{text: qsTr("Scripts"), name: "automationViewScripts"},
                          {text: root.runs && root.runs.count > 0 ? qsTr("Runs %1").arg(root.runs.count) : qsTr("Runs"),
                           name: "automationViewRuns"}]
                currentIndex: root.view === "runs" ? 1 : 0
                onActivated: index => root.view = index === 1 ? "runs" : "scripts"
            }
            Item { Layout.fillWidth: true }
            IconToolButton {
                objectName: "automationLoad"
                iconRole: "automation"
                text: qsTr("Load script…")
                enabled: root.loadAction !== null && root.loadAction.enabled
                onClicked: root.loadAction.trigger()
            }
            IconToolButton {
                objectName: "automationRefresh"
                iconRole: "refresh"
                text: qsTr("Refresh autoload scripts")
                enabled: root.refreshAction !== null && root.refreshAction.enabled && !root.running
                onClicked: root.refreshAction.trigger()
            }
            IconToolButton {
                objectName: "automationRerun"
                iconRole: "run-script"
                text: qsTr("Rerun last macro")
                enabled: root.automation !== null && root.automation.canRerun
                onClicked: root.automation.rerunLast()
            }
            IconToolButton {
                objectName: "automationRepeat"
                iconRole: "repeat-run"
                text: qsTr("Repeat last run")
                tip: root.automation !== null ? root.automation.repeatText : text
                enabled: root.automation !== null && root.automation.canRepeat
                onClicked: root.automation.repeatLast()
            }
        }
        TextField {
            id: filter
            objectName: "automationFilter"
            visible: root.view === "scripts"
            Layout.fillWidth: true
            Layout.margins: 8
            placeholderText: qsTr("Filter macros")
            Accessible.name: qsTr("Filter macros")
            Keys.onDownPressed: list.forceActiveFocus(Qt.TabFocusReason)
        }

        Rectangle {
            objectName: "documentScriptsOffer"
            visible: root.view === "scripts" && root.offeredScripts.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 6
            implicitHeight: offerRow.implicitHeight + 16
            color: Theme.panel
            border.width: 1
            border.color: Theme.line
            Accessible.role: Accessible.Grouping
            Accessible.name: offerText.text
            RowLayout {
                id: offerRow
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        id: offerText
                        objectName: "documentScriptsOfferText"
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        // Plain: as rich text, an <img> in a Document's path could reach a server.
                        textFormat: Text.PlainText
                        text: root.offerText()
                        HoverHandler { id: offerHover }
                        ToolTip {
                            id: offerTip
                            objectName: "documentScriptsOfferToolTip"
                            visible: offerHover.hovered
                            text: root.offerTip()
                            contentItem: Label {
                                text: offerTip.text
                                textFormat: Text.PlainText
                            }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.muted
                        text: qsTr("Loading a script runs its code.")
                    }
                }
                Button {
                    objectName: "documentScriptsLoad"
                    text: qsTr("Load")
                    Accessible.description: qsTr("Load the scripts this file names")
                    onClicked: {
                        root.automation.addFromDocument()
                        root.readDocumentScripts()
                    }
                }
            }
        }

        ColumnLayout {
            visible: root.view === "scripts" && root.controller.scripts.length === 0 && root.rows.length === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10
            Item { Layout.fillHeight: true }
            Label {
                objectName: "scriptsEmpty"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                color: Theme.muted
                text: qsTr("No scripts loaded")
            }
            IconTextButton {
                objectName: "scriptsEmptyLoad"
                visible: root.loadAction !== null
                Layout.alignment: Qt.AlignHCenter
                iconRole: "automation"
                text: qsTr("Load script…")
                tip: qsTr("Load an Automation script")
                onClicked: root.loadAction.trigger()
            }
            Item { Layout.fillHeight: true }
        }
        Label {
            objectName: "scriptsNoMatch"
            visible: root.view === "scripts" && root.controller.scripts.length > 0 && root.rows.length === 0
            Layout.fillWidth: true
            Layout.margins: 12
            horizontalAlignment: Text.AlignHCenter
            color: Theme.muted
            text: qsTr("No macros match")
        }

        ListView {
            id: list
            objectName: "scripts"
            visible: root.view === "scripts" && root.rows.length > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            activeFocusOnTab: true
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.rows
            currentIndex: -1
            highlightFollowsCurrentItem: false
            ScrollBar.vertical: ScrollBar { policy: list.contentHeight > list.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Macros")
            readonly property var current: currentIndex >= 0 && currentIndex < root.rows.length ? root.rows[currentIndex] : null
            function step(by) {
                for (let i = currentIndex + by; i >= 0 && i < root.rows.length; i += by)
                    if (root.rows[i].kind === "macro" || root.rows[i].kind === "missing") {
                        currentIndex = i
                        positionViewAtIndex(i, ListView.Contain)
                        return
                    }
            }
            Keys.onUpPressed: step(-1)
            Keys.onDownPressed: step(1)
            Keys.onPressed: event => {
                const row = list.current
                if (!row || row.kind !== "macro")
                    return
                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && root.runnable(row)) {
                    root.controller.run(row.script.path, row.macro.ordinal)
                    event.accepted = true
                } else if (event.key === Qt.Key_F2) {
                    root.record(row.legacyName)
                    event.accepted = true
                } else if (event.key === Qt.Key_Menu) {
                    macroMenu.openFor(row, list.currentItem)
                    event.accepted = true
                }
            }

            delegate: Loader {
                id: entry
                required property var modelData
                required property int index
                width: ListView.view.width
                sourceComponent: modelData.kind === "section" ? sectionRow
                                 : modelData.kind === "script" ? scriptRow
                                 : modelData.kind === "macro" ? macroRow : missingRow
            }
        }

        Label {
            objectName: "runsEmpty"
            visible: root.view === "runs" && (!root.runs || root.runs.count === 0)
            Layout.fillWidth: true
            Layout.fillHeight: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: Theme.muted
            text: qsTr("No macro has run yet")
        }
        ColumnLayout {
            id: runsView
            objectName: "automationRuns"
            visible: root.view === "runs" && root.runs !== null && root.runs.count > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 8
            spacing: 8
            Rectangle {
                color: Theme.field
                border.color: Theme.line
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(runList.contentHeight, 5 * 52) + 2
                ListView {
                    id: runList
                    objectName: "runList"
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    model: root.runs
                    activeFocusOnTab: true
                    keyNavigationEnabled: true
                    boundsBehavior: Flickable.StopAtBounds
                    highlightFollowsCurrentItem: false
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Runs")
                    ScrollBar.vertical: ScrollBar { policy: runList.contentHeight > runList.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
                    // MUST KILL: chosen 0 means follow the newest run
                    property int chosen: 0
                    readonly property int shownRun: !root.runs ? 0
                                                    : (root.runs.count, chosen > 0 && root.runs.rowOf(chosen) >= 0)
                                                      ? chosen : root.runs.idAt(0)
                    function choose(id) {
                        chosen = id
                        const row = root.runs ? root.runs.rowOf(id) : -1
                        if (row >= 0) {
                            currentIndex = row
                            positionViewAtIndex(row, ListView.Contain)
                        }
                    }
                    onCurrentIndexChanged: if (currentItem) chosen = currentItem.runId
                    delegate: ItemDelegate {
                        id: runRow
                        required property int index
                        required property int runId
                        required property string macro
                        required property string script
                        required property string document
                        required property string started
                        required property string runState
                        required property string task
                        required property double progress
                        required property int duration
                        required property int lineCount
                        readonly property bool chosen: runId === runList.shownRun
                        objectName: "run_" + runId
                        width: ListView.view.width
                        padding: 0
                        leftPadding: 10
                        rightPadding: 10
                        topPadding: 6
                        bottomPadding: 6
                        focusPolicy: Qt.NoFocus
                        highlighted: chosen
                        Accessible.role: Accessible.ListItem
                        Accessible.name: macro + " (" + script + "), " + runRow.summary
                        readonly property string summary: {
                            if (runState === "running")
                                return progress >= 0 ? task.length > 0 ? task + " · " + Math.round(progress) + " %"
                                                                       : Math.round(progress) + " %"
                                                     : task
                            const word = ({ applied: qsTr("Applied"), problems: qsTr("Applied, problems named"),
                                            cancelled: qsTr("Cancelled"), failed: qsTr("Failed") })[runState] || ""
                            const lines = lineCount > 0 ? (lineCount === 1 ? qsTr("1 line") : qsTr("%n lines", "", lineCount)) : qsTr("nothing logged")
                            return [word + ", " + root.durationText(duration), document, lines]
                                   .filter(t => t.length > 0).join(" · ")
                        }
                        background: Rectangle {
                            color: runRow.chosen ? Theme.select : runRow.hovered ? Theme.raised : "transparent"
                            Rectangle {
                                visible: runRow.chosen
                                width: 2
                                height: parent.height
                                color: Theme.accent
                            }
                            Rectangle {
                                visible: runRow.index > 0
                                width: parent.width
                                height: 1
                                color: Theme.line
                            }
                        }
                        contentItem: RowLayout {
                            spacing: 10
                            Icon {
                                id: runIcon
                                iconRole: ({ applied: "run-applied", problems: "notice-warning", cancelled: "run-cancelled",
                                             failed: "notice-error" })[runRow.runState] || "run-busy"
                                color: runRow.runState === "failed" ? Theme.danger
                                     : runRow.runState === "problems" || runRow.runState === "cancelled" ? Theme.warning
                                     : IconTheme.normal
                                accentColor: runRow.runState === "applied" || runRow.runState === "running" ? IconTheme.accent : color
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: 1
                                RotationAnimator on rotation {
                                    running: runRow.runState === "running" && runIcon.visible
                                    from: 0
                                    to: 360
                                    duration: 1200
                                    loops: Animation.Infinite
                                }
                            }
                            ColumnLayout {
                                spacing: 2
                                Layout.fillWidth: true
                                RowLayout {
                                    id: runTitle
                                    Layout.fillWidth: true
                                    Label {
                                        text: runRow.macro
                                        textFormat: Text.PlainText
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                        Layout.maximumWidth: Math.max(0, runTitle.width - runStarted.implicitWidth
                                                                         - 2 * runTitle.spacing)
                                    }
                                    Label {
                                        objectName: "runScript"
                                        text: runRow.script
                                        textFormat: Text.PlainText
                                        color: Theme.muted
                                        elide: Text.ElideMiddle
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        Layout.preferredWidth: 0
                                    }
                                    Label {
                                        id: runStarted
                                        text: runRow.started
                                        color: Theme.muted
                                        font.features: { "tnum": 1 }
                                    }
                                }
                                Label {
                                    objectName: "runSummary"
                                    text: runRow.summary
                                    textFormat: Text.PlainText
                                    color: Theme.muted
                                    elide: Text.ElideRight
                                    font.pixelSize: Math.max(10, Qt.application.font.pixelSize - 1)
                                    Layout.fillWidth: true
                                }
                                ProgressBar {
                                    visible: runRow.runState === "running"
                                    from: 0
                                    to: 100
                                    indeterminate: runRow.progress < 0
                                    value: Math.max(0, runRow.progress)
                                    implicitHeight: 4
                                    Layout.fillWidth: true
                                    Accessible.name: qsTr("Progress")
                                }
                            }
                        }
                        onClicked: {
                            runList.currentIndex = index
                            runList.chosen = runId
                        }
                    }
                }
            }
            RowLayout {
                spacing: 4
                Layout.fillWidth: true
                ComboBox {
                    id: levelChoice
                    objectName: "runLogLevel"
                    model: [qsTr("Up to fatal"), qsTr("Up to error"), qsTr("Up to warning"), qsTr("Up to hint"),
                            qsTr("Up to debug"), qsTr("All levels")]
                    currentIndex: runLines.levelLimit
                    onActivated: index => runLines.levelLimit = index
                    Accessible.name: qsTr("Levels shown")
                    ToolTip.visible: hovered && !popup.visible
                    ToolTip.text: qsTr("Lines without a level always show")
                    delegate: ItemDelegate {
                        id: levelItem
                        required property int index
                        required property string modelData
                        width: ListView.view ? ListView.view.width : implicitWidth
                        highlighted: levelChoice.highlightedIndex === index
                        contentItem: RowLayout {
                            spacing: 12
                            Label {
                                text: levelItem.modelData
                                Layout.fillWidth: true
                            }
                            Label {
                                text: levelItem.index + " · " + (runLines.total, runLines.countAt(levelItem.index))
                                color: Theme.muted
                                font.features: { "tnum": 1 }
                            }
                        }
                    }
                }
                TextField {
                    id: runFilter
                    objectName: "runLogFilter"
                    placeholderText: qsTr("Filter")
                    text: runLines.filterText
                    selectByMouse: true
                    rightPadding: matchCount.visible ? matchCount.implicitWidth + 16 : leftPadding
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Filter the run's log")
                    onTextEdited: runLines.filterText = text
                    Keys.onEscapePressed: event => {
                        if (text.length === 0) {
                            event.accepted = false
                            return
                        }
                        runLines.filterText = ""
                    }
                    Label {
                        id: matchCount
                        objectName: "runLogMatches"
                        visible: runLines.filterText.trim().length > 0
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.muted
                        text: qsTr("%1 of %2").arg(runLines.count).arg(runLines.total)
                    }
                }
                IconToolButton {
                    objectName: "runLogTimes"
                    iconRole: "history"
                    text: qsTr("Show times")
                    checkable: true
                    checked: runLines.times
                    onToggled: {
                        if (root.settings)
                            root.settings.setValue("automation.runLogTimes", checked)
                        else
                            runLines.times = checked
                    }
                }
                IconToolButton {
                    objectName: "runLogCopy"
                    iconRole: "edit-copy"
                    text: qsTr("Copy")
                    tip: qsTr("Copy the lines shown")
                    enabled: runLines.count > 0
                    onClicked: runLines.copyShown()
                }
                IconToolButton {
                    objectName: "runLogSave"
                    iconRole: "save"
                    text: qsTr("Save…")
                    tip: qsTr("Save the lines shown…")
                    enabled: runLines.count > 0
                    onClicked: {
                        saveDialog.selectedFile = saveDialog.currentFolder + "/" + runLines.suggestedName()
                        saveDialog.open()
                    }
                }
            }
            Rectangle {
                color: Theme.field
                border.color: Theme.line
                Layout.fillWidth: true
                Layout.fillHeight: true
                ListView {
                    id: runLog
                    objectName: "runLog"
                    anchors.fill: parent
                    anchors.margins: 4
                    clip: true
                    activeFocusOnTab: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: AutomationRunLines {
                        id: runLines
                        objectName: "runLines"
                        runs: root.runs
                        run: runList.shownRun
                        times: root.settings ? root.settings.value("automation.runLogTimes") === true : false
                    }
                    property bool follow: true
                    onMovementEnded: follow = atYEnd
                    onCountChanged: if (follow) Qt.callLater(positionViewAtEnd)
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Log")
                    ScrollBar.vertical: ScrollBar { policy: runLog.contentHeight > runLog.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
                    delegate: RunLogLine {
                        width: ListView.view.width
                        times: runLines.times
                        font: logMetrics.font
                    }
                }
                Label {
                    objectName: "runLogEmpty"
                    anchors.centerIn: parent
                    visible: runLog.count === 0
                    color: Theme.muted
                    text: runLines.total > 0 ? qsTr("No lines match") : qsTr("Nothing logged")
                }
                FontMetrics {
                    id: logMetrics
                    font.family: "monospace"
                    font.pixelSize: Math.max(10, Qt.application.font.pixelSize - 1)
                }
            }
        }
    }
    Connections {
        target: root.settings
        ignoreUnknownSignals: true
        function onChanged(id) {
            if (id === "automation.runLogTimes")
                runLines.times = root.settings.value(id) === true
        }
    }
    function durationText(ms) {
        const s = ms / 1000
        if (s < 60)
            return qsTr("%1 s").arg(s < 10 ? s.toFixed(1) : Math.round(s))
        return qsTr("%1 min %2 s").arg(Math.floor(s / 60)).arg(Math.round(s % 60))
    }
    Dialogs.FileDialog {
        id: saveDialog
        objectName: "runLogSaveDialog"
        title: qsTr("Save the run's log")
        fileMode: Dialogs.FileDialog.SaveFile
        nameFilters: [qsTr("Log files (*.log *.txt)"), qsTr("All files (*)")]
        onAccepted: {
            const error = runLines.saveShown(selectedFile)
            if (error.length > 0 && root.notices)
                root.notices.post("error", qsTr("Save the run's log"), error)
        }
    }

    Component {
        id: sectionRow
        AbstractButton {
            id: header
            readonly property var row: parent ? parent.modelData : ({})
            objectName: "section_" + row.key
            implicitHeight: 28
            leftPadding: 12
            rightPadding: 12
            topPadding: 6
            checkable: false
            focusPolicy: Qt.TabFocus
            Accessible.role: Accessible.Button
            Accessible.name: row.title
            Accessible.description: root.folded[row.key] ? qsTr("Folded") : qsTr("Unfolded")
            onClicked: {
                const f = Object.assign({}, root.folded)
                f[row.key] = !f[row.key]
                root.folded = f
            }
            contentItem: RowLayout {
                spacing: 4
                Shape {
                    implicitWidth: 12
                    implicitHeight: 12
                    rotation: root.folded[header.row.key] ? -90 : 0
                    ShapePath {
                        strokeColor: Theme.text
                        strokeWidth: 1.25
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin
                        startX: 3; startY: 4.5
                        PathLine { x: 6; y: 7.5 }
                        PathLine { x: 9; y: 4.5 }
                    }
                }
                Label {
                    text: header.row.title
                    font.bold: true
                }
                Label {
                    text: header.row.count
                    color: Theme.muted
                    font.pixelSize: Math.max(10, Qt.application.font.pixelSize - 2)
                    Accessible.ignored: true
                }
                Item { Layout.fillWidth: true }
            }
            background: Item { FocusRing { control: header } }
        }
    }

    Component {
        id: scriptRow
        ColumnLayout {
            id: scriptItem
            readonly property var row: parent ? parent.modelData : ({ script: {}, index: -1 })
            readonly property var script: row.script
            readonly property string state: script.state ?? ""
            property bool fullError: false
            objectName: "script_" + row.index
            spacing: 0
            Accessible.role: Accessible.Grouping
            Accessible.name: root.scriptTitle(script)
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: scriptItem.script.inDocument ? 28 : 20
                Layout.rightMargin: 8
                Layout.topMargin: 4
                spacing: 4
                CheckBox {
                    objectName: "enabled_" + scriptItem.row.index
                    visible: !scriptItem.script.inDocument
                    checked: scriptItem.script.enabled ?? true
                    enabled: scriptItem.state !== "running" && scriptItem.state !== "loading"
                    padding: 0
                    Accessible.name: qsTr("Load %1 at start").arg(root.scriptTitle(scriptItem.script))
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Load at start")
                    onToggled: root.controller.setEnabled(scriptItem.script.path, checked)
                }
                Label {
                    Layout.fillWidth: true
                    text: root.scriptTitle(scriptItem.script)
                    font.bold: true
                    color: scriptItem.state === "disabled" ? Theme.muted : Theme.text
                    elide: Text.ElideRight
                    HoverHandler { id: nameHover }
                    ToolTip.visible: nameHover.hovered && (truncated || (scriptItem.script.description ?? "") !== "")
                    ToolTip.text: (scriptItem.script.description ?? "") !== "" ? scriptItem.script.description : text
                }
                Label {
                    objectName: "state_" + scriptItem.row.index
                    readonly property string scriptState: scriptItem.state
                    visible: scriptState !== "ready" && scriptState !== "disabled"
                    text: scriptState === "loading" ? qsTr("Loading")
                        : scriptState === "running" ? qsTr("Running")
                        : scriptState === "failed" ? qsTr("Failed to load")
                        : scriptState === "unavailable" ? qsTr("Stopped") : ""
                    color: scriptState === "failed" || scriptState === "unavailable" ? Theme.danger : Theme.muted
                    font.pixelSize: Math.max(10, Qt.application.font.pixelSize - 2)
                }
                IconToolButton {
                    objectName: "reload_" + scriptItem.row.index
                    visible: scriptItem.state !== "disabled"
                    iconRole: "refresh"
                    text: qsTr("Reload")
                    enabled: scriptItem.state !== "running" && scriptItem.state !== "loading"
                    onClicked: root.controller.reload(scriptItem.script.path)
                }
                IconToolButton {
                    id: scriptMenuButton
                    objectName: "scriptMenu_" + scriptItem.row.index
                    iconRole: "panel-menu"
                    text: qsTr("%1 options").arg(root.scriptTitle(scriptItem.script))
                    onClicked: scriptMenu.openFor(scriptItem.script, scriptMenuButton)
                }
            }
            Label {
                objectName: "error_" + scriptItem.row.index
                visible: text !== ""
                Layout.fillWidth: true
                Layout.leftMargin: 28
                Layout.rightMargin: 12
                text: scriptItem.state === "unavailable" && (scriptItem.script.error ?? "") === ""
                      ? qsTr("The script's process ended. Reload to start it again; its macro is not rerun.")
                      : root.shownError(scriptItem.script)
                wrapMode: Text.Wrap
                maximumLineCount: scriptItem.fullError ? 1000 : 2
                elide: Text.ElideRight
                color: Theme.danger
                font.pixelSize: Math.max(10, Qt.application.font.pixelSize - 2)
            }
            Button {
                objectName: "fullError_" + scriptItem.row.index
                visible: (scriptItem.script.error ?? "").length > 0 && (scriptItem.fullError || errorMeasure.lineCount > 2)
                Layout.leftMargin: 24
                flat: true
                text: scriptItem.fullError ? qsTr("Show less") : qsTr("Show full error")
                onClicked: scriptItem.fullError = !scriptItem.fullError
                Text {
                    id: errorMeasure
                    visible: false
                    width: Math.max(1, scriptItem.width - 40)
                    wrapMode: Text.Wrap
                    text: root.shownError(scriptItem.script)
                }
            }
        }
    }

    Component {
        id: macroRow
        ItemDelegate {
            id: macroItem
            readonly property var row: parent ? parent.modelData : ({ script: {}, macro: {}, legacyName: "" })
            readonly property int rowIndex: parent ? parent.index : -1
            readonly property bool current: list.currentIndex === rowIndex
            readonly property bool recordingHere: root.recording.length > 0 && root.recording === row.legacyName
            readonly property string keys: root.keysOf(row.legacyName)
            readonly property string takenBy: keys.length > 0 && root.hotkeys ? root.hotkeys.globalHolder(keys) : ""
            readonly property bool runnable: root.runnable(row)
            objectName: "macro_" + row.index + "_" + row.macro.ordinal
            text: row.macro.name
            highlighted: current
            leftPadding: 40
            rightPadding: 8
            topPadding: 0
            bottomPadding: 0
            focusPolicy: Qt.NoFocus
            Accessible.name: row.macro.name + (keys.length > 0 ? ", " + keys : "")
            Accessible.description: row.macro.description ?? ""
            ToolTip.visible: hovered && (row.macro.description ?? "") !== ""
            ToolTip.text: row.macro.description ?? ""
            onClicked: {
                list.currentIndex = rowIndex
                list.forceActiveFocus(Qt.MouseFocusReason)
            }
            onDoubleClicked: if (runnable) root.controller.run(row.script.path, row.macro.ordinal)
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: {
                    list.currentIndex = macroItem.rowIndex
                    macroMenu.openFor(macroItem.row, macroItem)
                }
            }
            background: Rectangle {
                color: macroItem.current ? Theme.select : macroItem.hovered ? Qt.alpha(Theme.select, 0.45) : "transparent"
                Rectangle {
                    visible: macroItem.current
                    width: 3
                    height: parent.height
                    color: Theme.accent
                }
                Rectangle {
                    anchors.fill: parent
                    visible: macroItem.current && list.activeFocus && !root.recording
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.text
                }
            }
            contentItem: ColumnLayout {
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(28, recorderSlot.implicitHeight + 4)
                    spacing: 6
                    Label {
                        Layout.fillWidth: true
                        text: macroItem.row.macro.name
                        elide: Text.ElideRight
                        color: macroItem.runnable || macroItem.row.script.state === "ready" ? Theme.text : Theme.disabled
                    }
                    Icon {
                        id: busyMark
                        objectName: "macroBusy_" + macroItem.row.index + "_" + macroItem.row.macro.ordinal
                        visible: root.running && root.automation.runPath === macroItem.row.script.path
                                 && root.automation.runOrdinal === macroItem.row.macro.ordinal
                        iconRole: "run-busy"
                        size: 14
                        RotationAnimator on rotation {
                            running: busyMark.visible
                            from: 0
                            to: 360
                            duration: 1200
                            loops: Animation.Infinite
                        }
                    }
                    IconToolButton {
                        objectName: "run_" + macroItem.row.index + "_" + macroItem.row.macro.ordinal
                        visible: macroItem.current || macroItem.hovered
                        iconRole: "media-play"
                        text: qsTr("Run")
                        tip: qsTr("Run (Enter)")
                        enabled: macroItem.runnable
                        focusPolicy: Qt.NoFocus
                        onClicked: root.controller.run(macroItem.row.script.path, macroItem.row.macro.ordinal)
                    }
                    Item {
                        id: recorderSlot
                        implicitWidth: macroItem.recordingHere ? recorderField.implicitWidth : keysButton.implicitWidth
                        implicitHeight: macroItem.recordingHere ? recorderField.implicitHeight : keysButton.implicitHeight
                        Layout.preferredWidth: implicitWidth
                        Button {
                            id: keysButton
                            objectName: "keys_" + macroItem.row.index + "_" + macroItem.row.macro.ordinal
                            visible: !macroItem.recordingHere && (macroItem.keys.length > 0 || macroItem.current)
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            implicitWidth: implicitContentWidth + leftPadding + rightPadding
                            flat: macroItem.keys.length > 0
                            focusPolicy: Qt.NoFocus
                            padding: 2
                            leftPadding: 6
                            rightPadding: 6
                            enabled: root.hotkeys !== null
                            text: macroItem.keys.length > 0 ? macroItem.keys : qsTr("Add shortcut")
                            Accessible.name: macroItem.keys.length > 0 ? qsTr("Shortcut %1").arg(macroItem.keys) : qsTr("Add shortcut")
                            contentItem: Label {
                                text: keysButton.text
                                verticalAlignment: Text.AlignVCenter
                                color: macroItem.takenBy.length > 0 ? Theme.warning : Theme.muted
                                font.pixelSize: Math.max(10, Qt.application.font.pixelSize - 2)
                            }
                            onClicked: {
                                list.currentIndex = macroItem.rowIndex
                                root.record(macroItem.row.legacyName)
                            }
                        }
                        FocusScope {
                            id: recorderField
                            visible: macroItem.recordingHere
                            implicitWidth: Math.max(120, recorderRow.implicitWidth + 8)
                            implicitHeight: recorderRow.implicitHeight + 4
                            Rectangle {
                                anchors.fill: parent
                                color: Theme.field
                                border.width: 2
                                border.color: Theme.accent
                            }
                            RowLayout {
                                id: recorderRow
                                anchors.fill: parent
                                anchors.leftMargin: 6
                                anchors.rightMargin: 2
                                spacing: 2
                                Label {
                                    Layout.fillWidth: true
                                    text: root.pressed ? root.pressed.keys : qsTr("Press a shortcut")
                                    color: root.pressed ? Theme.text : Theme.muted
                                    font.bold: root.pressed !== null
                                }
                                IconToolButton {
                                    objectName: "clearKeys"
                                    iconRole: "clear"
                                    iconSize: 14
                                    text: qsTr("Clear shortcut")
                                    focusPolicy: Qt.NoFocus
                                    onClicked: {
                                        if (root.macroHotkeys)
                                            root.macroHotkeys.clearKeys(root.recording)
                                        root.stopRecording()
                                    }
                                }
                            }
                        }
                    }
                }
                Label {
                    objectName: "keysNote_" + macroItem.row.index + "_" + macroItem.row.macro.ordinal
                    readonly property var holders: macroItem.recordingHere && root.pressed && root.hotkeys
                                                   ? root.hotkeys.holdersOf(root.pressed.accel, root.hotkeys.gestureTarget(macroItem.row.legacyName).id)
                                                   : []
                    visible: text.length > 0
                    Layout.fillWidth: true
                    Layout.bottomMargin: 4
                    wrapMode: Text.Wrap
                    font.pixelSize: Math.max(10, Qt.application.font.pixelSize - 2)
                    color: macroItem.recordingHere && root.refusal.length > 0 ? Theme.danger : Theme.warning
                    text: macroItem.recordingHere
                          ? (root.refusal.length > 0 ? root.refusal
                             : holders.length > 0 ? qsTr("Already assigned to: %1").arg(holders.join(", ")) : "")
                          : macroItem.takenBy.length > 0 ? qsTr("%1 also uses %2 and takes it.").arg(macroItem.takenBy).arg(macroItem.keys) : ""
                }
            }
        }
    }

    Component {
        id: missingRow
        ItemDelegate {
            id: missingItem
            readonly property var row: parent ? parent.modelData : ({ binding: {} })
            readonly property int rowIndex: parent ? parent.index : -1
            objectName: "missing_" + row.binding.legacyName
            highlighted: list.currentIndex === rowIndex
            leftPadding: 28
            rightPadding: 8
            focusPolicy: Qt.NoFocus
            Accessible.name: (row.binding.macro !== "" ? row.binding.macro : row.binding.legacyName) + ", " + row.binding.keys
            Accessible.description: row.binding.problem
            onClicked: list.currentIndex = rowIndex
            contentItem: RowLayout {
                spacing: 6
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        text: missingItem.row.binding.macro !== "" ? missingItem.row.binding.macro : missingItem.row.binding.legacyName
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.warning
                        font.pixelSize: Math.max(10, Qt.application.font.pixelSize - 2)
                        text: missingItem.row.binding.problem
                    }
                }
                Label {
                    text: missingItem.row.binding.keys
                    color: Theme.muted
                    font.pixelSize: Math.max(10, Qt.application.font.pixelSize - 2)
                }
                IconToolButton {
                    objectName: "removeMissing"
                    iconRole: "remove"
                    text: qsTr("Remove shortcut")
                    onClicked: root.macroHotkeys.clearKeys(missingItem.row.binding.legacyName)
                }
            }
        }
    }

    Item {
        id: recorder
        objectName: "automationRecorder"
        focus: root.recording.length > 0
        // MUST KILL: recordsKeys is read by name by key_router.cpp, which then stands aside
        readonly property bool recordsKeys: root.recording.length > 0
        Accessible.role: Accessible.EditableText
        Accessible.name: qsTr("Shortcut")
        Keys.onShortcutOverride: event => event.accepted = root.recording.length > 0
        Keys.onPressed: event => {
            if (root.recording.length === 0)
                return
            const mods = event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)
            event.accepted = true
            if (mods === 0 && event.key === Qt.Key_Escape) {
                root.stopRecording()
                return
            }
            if (mods === 0 && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
                root.saveRecording()
                return
            }
            if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                event.accepted = false
                return
            }
            if (!root.hotkeys)
                return
            const c = root.hotkeys.capture(event.key, event.modifiers, 0)
            if (c.result === "accepted") {
                root.pressed = { accel: c.accel, keys: c.keys }
                root.refusal = root.blocked
            } else if (c.result === "refused") {
                root.pressed = null
                root.refusal = c.message
            }
        }
        onActiveFocusChanged: if (!activeFocus && root.recording.length > 0 && !reassign.visible) root.stopRecording()
    }

    ShellMenu {
        id: scriptMenu
        objectName: "automationScriptMenu"
        property var script: null
        function openFor(script, anchor) {
            scriptMenu.script = script
            popup(anchor, 0, anchor.height)
        }
        ShellMenuItem {
            objectName: "scriptMenuReload"
            iconRole: "refresh"
            text: qsTr("Reload")
            enabled: scriptMenu.script !== null && scriptMenu.script.state !== "running" && scriptMenu.script.state !== "loading"
                     && scriptMenu.script.state !== "disabled"
            onTriggered: root.controller.reload(scriptMenu.script.path)
        }
        ShellMenuItem {
            objectName: "scriptMenuEdit"
            iconRole: "edit"
            text: qsTr("Edit script")
            enabled: root.automation !== null
            onTriggered: root.automation.editScript(scriptMenu.script.path)
        }
        MenuSeparator {}
        ShellMenuItem {
            objectName: "scriptMenuUnload"
            iconRole: "unload"
            text: qsTr("Unload")
            enabled: scriptMenu.script !== null && scriptMenu.script.state !== "running"
                     && scriptMenu.script.state !== "disabled"
            onTriggered: root.controller.unload(scriptMenu.script.path)
        }
    }

    ShellMenu {
        id: macroMenu
        objectName: "automationMacroMenu"
        property var row: null
        function openFor(row, anchor) {
            macroMenu.row = row
            popup(anchor, 40, anchor.height)
        }
        ShellMenuItem {
            iconRole: "media-play"
            text: qsTr("Run")
            enabled: macroMenu.row !== null && macroMenu.row.script.state === "ready" && !root.controller.busy
            onTriggered: root.controller.run(macroMenu.row.script.path, macroMenu.row.macro.ordinal)
        }
        ShellMenuItem {
            iconRole: "hotkeys"
            text: qsTr("Set shortcut…")
            enabled: root.hotkeys !== null
            onTriggered: root.record(macroMenu.row.legacyName)
        }
        ShellMenuItem {
            iconRole: "clear"
            text: qsTr("Clear shortcut")
            enabled: macroMenu.row !== null && root.keysOf(macroMenu.row.legacyName).length > 0
            onTriggered: root.macroHotkeys.clearKeys(macroMenu.row.legacyName)
        }
    }

    Popup {
        id: reassign
        objectName: "automationReassign"
        property var target: null
        property var conflict: null
        modal: true
        focus: true
        padding: 12
        x: Math.round((root.width - width) / 2)
        y: Math.round((root.height - height) / 2)
        width: Math.min(root.width - 16, 320)
        closePolicy: Popup.CloseOnEscape
        function ask(target, conflict) {
            reassign.target = target
            reassign.conflict = conflict
            open()
        }
        function answer(a) {
            if (a !== "cancel")
                root.hotkeys.gestureMap(target.id, target.name, root.pressed.accel, 0, a)
            close()
            root.stopRecording()
        }
        onClosed: if (root.recording.length > 0) root.stopRecording()
        background: Rectangle {
            color: Theme.panel
            border.width: 1
            border.color: Theme.line
            radius: 4
        }
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: qsTr("Reassign shortcut")
                font.bold: true
            }
            Label {
                objectName: "automationReassignText"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                readonly property var holders: root.pressed && root.hotkeys && reassign.target
                                               ? root.hotkeys.holdersOf(root.pressed.accel, reassign.target.id) : []
                text: root.pressed ? qsTr("%1 is assigned to %2. Assign it to %3 instead?")
                                     .arg(root.pressed.keys).arg(holders.join(", "))
                                     .arg(list.current && list.current.macro ? list.current.macro.name : root.recording)
                                   : ""
            }
            DialogButtonBox {
                Layout.fillWidth: true
                Button {
                    objectName: "automationReassignOk"
                    text: qsTr("Reassign")
                    highlighted: true
                    DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                }
                FooterButton {
                    objectName: "automationReassignKeepBoth"
                    text: qsTr("Keep both")
                    visible: reassign.conflict !== null && reassign.conflict.canSetAnyway === true
                    DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                    onClicked: reassign.answer("anyway")
                }
                Button {
                    objectName: "automationReassignCancel"
                    text: qsTr("Cancel")
                    DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                }
                onAccepted: reassign.answer("delete")
                onRejected: reassign.answer("cancel")
            }
        }
    }
}
