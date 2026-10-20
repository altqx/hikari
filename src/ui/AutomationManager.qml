// The Automation panel (L1, AM1 #248; the design approved on the card):
// the loaded scripts in two sections, the Document scripts ("Subtitle
// scripts": named by a Document's Script Info or added with Load script, one
// list for the application as legacy's) and autoload's, each script's macros with their shortcuts, the run under way
// and the shortcuts whose macro is missing. A macro runs from its row (Enter,
// a double click or its Run button); its shortcut is recorded in place
// through the shortcut editor (legacy's one hotkey map, the Shift+click
// gesture's path), so Settings → Hotkeys shows the same bindings. Loading
// and reloading run the script's top level visibly; a failed or stopped
// script says why and offers reload. The dock header names the panel; the
// panel has no heading of its own.
pragma Translator: "HikariSub.Automation"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import Hikari.Ui

Pane {
    id: root
    required property AutomationManagerController controller
    // AutomationShell: the run (running, runTitle, task, progress, log,
    // lastMessage, forceStopOffered, canRerun, cancelRun, forceStopRun,
    // rerunLast, editScript, runCompleted).
    property var automation: null
    // AutomationHotkeysController: the macros' bindings (keys, missing, clearKeys).
    property var macroHotkeys: null
    // HotkeysController: capture, gestureTarget/Conflict/Map, holdersOf, globalHolder.
    property var hotkeys: null
    // The Automation menu's actions: Load script…, Refresh autoload scripts.
    property Action loadAction: null
    property Action refreshAction: null
    // The editing target's name, for the run's read-only note.
    property string documentName: ""
    // The current tab: the editing target changes with it.
    property int documentTab: -1
    implicitWidth: 340
    implicitHeight: 420
    padding: 0
    // The smallest size that shows the tool strip and a row whole.
    readonly property size minimumSize: Qt.size(240, 160)

    // The macro whose shortcut is being recorded (its legacy name), and what
    // was pressed so far: {accel, keys} accepted, or a refusal's message.
    property string recording: ""
    property var pressed: null
    property string refusal: ""
    // Section keys folded away; the missing shortcuts start folded.
    property var folded: ({ missing: true })
    // A run that changed nothing (failed, refused, not valid, cancelled,
    // lost or stopped) keeps its box until dismissed; the log shows on demand.
    property bool keepResult: false
    readonly property string outcome: automation !== null && !running ? automation.lastOutcome : ""
    readonly property bool cancelled: outcome === "cancelled"
    property bool showLog: false
    property string runTarget: ""

    readonly property bool running: automation !== null && automation.running
    readonly property string filterText: filter.text.trim().toLowerCase()

    // The Document's scripts (its Script Info) that are not loaded, offered
    // as the panel shows and as the editing target changes while it shows
    // (the panel stays open, restored with a layout or a tab's arrangement).
    // Loading a script runs its code, so the panel waits for Load; the
    // Automation menu opening still loads them, as legacy's did
    // (Automation.cpp:1359).
    property var offeredScripts: []
    function readDocumentScripts() {
        offeredScripts = automation && visible ? automation.documentScriptsToLoad() : []
    }
    function fileName(path) {
        return path.replace(/^.*[\\/]/, "")
    }
    // After the change: the tab and its name change before the target does.
    onVisibleChanged: Qt.callLater(readDocumentScripts)
    onDocumentNameChanged: Qt.callLater(readDocumentScripts)
    onDocumentTabChanged: Qt.callLater(readDocumentScripts)
    Connections {
        target: root.controller
        function onChanged() { Qt.callLater(root.readDocumentScripts) }
    }

    // A macro row runs (Run, a double click, Enter) when its script is ready
    // and no macro is running.
    function runnable(row) {
        return row.script.state === "ready" && !root.controller.busy
    }

    // Starts recording `legacyName`'s shortcut (the Automation menu's
    // Shift+click on a macro opens the panel on it).
    function record(legacyName) {
        const indexOf = () => rows.findIndex(r => r.kind === "macro" && r.legacyName === legacyName)
        let i = indexOf()
        if (i < 0) {
            // Its row is shown first: the filter goes, its section unfolds.
            filter.text = ""
            folded = ({ missing: folded.missing === true })
            i = indexOf()
        }
        if (i < 0)
            return // no loaded macro has it
        recording = legacyName
        pressed = null
        refusal = ""
        list.currentIndex = i
        list.positionViewAtIndex(i, ListView.Contain)
        Qt.callLater(() => recorder.forceActiveFocus(Qt.OtherFocusReason))
    }
    function stopRecording() {
        recording = ""
        pressed = null
        refusal = ""
        reassign.close()
        list.forceActiveFocus(Qt.OtherFocusReason)
    }
    // Enter while recording: the shortcut is set through the shortcut
    // editor's gesture path (OnMapHkey), asking first when it is taken.
    function saveRecording() {
        if (!hotkeys || !pressed)
            return
        const target = hotkeys.gestureTarget(recording)
        const conflict = hotkeys.gestureConflict(target.id, pressed.accel, 0)
        // The keys it has already (legacy's question counts the binding
        // itself): nothing to ask; the binding is kept, made for this macro.
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
    function matches(text) {
        return filterText.length === 0 || text.toLowerCase().includes(filterText)
    }

    // The list, flattened: section headers, scripts, their macros, the
    // missing shortcuts. A script shows while its name or a macro matches
    // the filter; a section while it has a script.
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

    onRunningChanged: {
        if (running) {
            keepResult = false
            runTarget = documentName
        }
    }
    Connections {
        target: root.automation
        ignoreUnknownSignals: true
        function onRunCompleted(ok, message) {
            // Not when the user declined the resolution question: the
            // macro did not start.
            root.keepResult = !ok && root.automation.lastOutcome !== "declined"
            if (root.keepResult)
                root.showLog = false
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // The tool strip: the filter and the panel's commands, icon-only
        // with their names on hover.
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 4
            TextField {
                id: filter
                objectName: "automationFilter"
                Layout.fillWidth: true
                placeholderText: qsTr("Filter macros")
                Accessible.name: qsTr("Filter macros")
                Keys.onDownPressed: list.forceActiveFocus(Qt.TabFocusReason)
            }
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
        }

        // The run: while a macro runs, and after one that changed nothing
        // until dismissed.
        Rectangle {
            id: runBox
            objectName: "automationRun"
            visible: root.running || root.keepResult
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 6
            implicitHeight: runColumn.implicitHeight + 16
            color: Theme.panel
            border.width: 1
            border.color: root.keepResult && !root.running && !root.cancelled ? Theme.danger : Theme.line
            Accessible.role: Accessible.Grouping
            Accessible.name: runTitle.text

            ColumnLayout {
                id: runColumn
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        id: runTitle
                        objectName: "automationRunTitle"
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.bold: true
                        color: root.running || root.cancelled ? Theme.text : Theme.danger
                        text: !root.automation ? ""
                              : root.running ? root.automation.runTitle
                              : root.cancelled ? qsTr("%1 cancelled").arg(root.automation.runTitle)
                              : qsTr("%1 failed").arg(root.automation.runTitle)
                    }
                    Label {
                        visible: root.running
                        color: Theme.muted
                        text: root.automation ? Math.round(root.automation.progress) + "%" : ""
                    }
                }
                Label {
                    objectName: "automationTask"
                    Layout.fillWidth: true
                    visible: text.length > 0
                    wrapMode: Text.Wrap
                    color: root.running || root.cancelled ? Theme.muted : Theme.danger
                    font.pixelSize: Math.max(10, Qt.application.font.pixelSize - 1)
                    // Validation says nothing itself (legacy's menu neither);
                    // its error, if any, is in the log.
                    text: !root.automation ? "" : root.running ? root.automation.task
                          : root.outcome === "invalid" ? qsTr("Its validation did not let it run; nothing was changed.")
                          : root.automation.lastMessage
                }
                ProgressBar {
                    objectName: "automationProgressBar"
                    visible: root.running
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    value: root.automation ? root.automation.progress : 0
                    Accessible.name: qsTr("Progress")
                }
                Label {
                    visible: root.running && root.runTarget.length > 0
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.muted
                    font.pixelSize: Math.max(10, Qt.application.font.pixelSize - 1)
                    text: qsTr("%1 is read-only until the macro ends").arg(root.runTarget)
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Button {
                        objectName: "automationShowLog"
                        flat: true
                        text: root.showLog ? qsTr("Hide log") : qsTr("Show log")
                        onClicked: root.showLog = !root.showLog
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        objectName: "automationForceStop"
                        visible: root.running && root.automation.forceStopOffered
                        text: qsTr("Force stop")
                        onClicked: root.automation.forceStopRun()
                    }
                    Button {
                        objectName: "automationCancel"
                        text: root.running ? qsTr("Cancel") : qsTr("Close")
                        onClicked: {
                            if (root.running)
                                root.automation.cancelRun()
                            else
                                root.keepResult = false
                        }
                    }
                }
                ScrollView {
                    visible: root.showLog
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(logText.implicitHeight, 6 * logMetrics.height) + 4
                    TextArea {
                        id: logText
                        objectName: "automationLog"
                        readOnly: true
                        wrapMode: TextEdit.Wrap
                        font.family: "monospace"
                        text: root.automation ? root.automation.log : ""
                        Accessible.name: qsTr("Log")
                    }
                    FontMetrics { id: logMetrics; font: logText.font }
                }
            }
        }

        // The Document's scripts, offered until loaded.
        Rectangle {
            objectName: "documentScriptsOffer"
            visible: root.offeredScripts.length > 0
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
                        text: qsTr("This file names scripts: %1").arg(root.offeredScripts.map(root.fileName).join(", "))
                        ToolTip.visible: offerHover.hovered
                        ToolTip.text: root.offeredScripts.join("\n")
                        HoverHandler { id: offerHover }
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

        // The empty state: what is missing and the command that adds it.
        ColumnLayout {
            visible: root.controller.scripts.length === 0 && root.rows.length === 0
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
            visible: root.controller.scripts.length > 0 && root.rows.length === 0
            Layout.fillWidth: true
            Layout.margins: 12
            horizontalAlignment: Text.AlignHCenter
            color: Theme.muted
            text: qsTr("No macros match")
        }

        ListView {
            id: list
            objectName: "scripts"
            visible: root.rows.length > 0
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
            // Up and Down step over the headers and scripts to the macros.
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
    }

    // A section header: a fold arrow, the title in bold and the count.
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

    // A script: its name in bold, its state in words when it is not ready,
    // Reload and its menu; an error under it in the danger colour.
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
                Layout.leftMargin: 28
                Layout.rightMargin: 8
                Layout.topMargin: 4
                spacing: 4
                Label {
                    Layout.fillWidth: true
                    text: root.scriptTitle(scriptItem.script)
                    font.bold: true
                    elide: Text.ElideRight
                    HoverHandler { id: nameHover }
                    ToolTip.visible: nameHover.hovered && (truncated || (scriptItem.script.description ?? "") !== "")
                    ToolTip.text: (scriptItem.script.description ?? "") !== "" ? scriptItem.script.description : text
                }
                // The state in words, muted; a failure in the danger colour.
                Label {
                    objectName: "state_" + scriptItem.row.index
                    readonly property string scriptState: scriptItem.state
                    visible: scriptState !== "ready"
                    text: scriptState === "loading" ? qsTr("Loading")
                        : scriptState === "running" ? qsTr("Running")
                        : scriptState === "failed" ? qsTr("Failed to load")
                        : scriptState === "unavailable" ? qsTr("Stopped") : ""
                    color: scriptState === "failed" || scriptState === "unavailable" ? Theme.danger : Theme.muted
                    font.pixelSize: Math.max(10, Qt.application.font.pixelSize - 2)
                }
                IconToolButton {
                    objectName: "reload_" + scriptItem.row.index
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
                      : scriptItem.script.error ?? ""
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
                // The error's whole height, to know whether it was cut.
                Text {
                    id: errorMeasure
                    visible: false
                    width: Math.max(1, scriptItem.width - 40)
                    wrapMode: Text.Wrap
                    text: scriptItem.script.error ?? ""
                }
            }
        }
    }

    // A macro: its name (the description on hover), Run on the current row,
    // and its shortcut at the end, recorded in place.
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
            ToolTip.delay: 600
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
                    // keyboard focus on the current row: a ring in the text colour
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
                        // The shortcut: its keys like a menu's (in the warning
                        // colour when a command takes them), "Add shortcut" on
                        // the current row without one; a click records.
                        Button {
                            id: keysButton
                            objectName: "keys_" + macroItem.row.index + "_" + macroItem.row.macro.ordinal
                            visible: !macroItem.recordingHere && (macroItem.keys.length > 0 || macroItem.current)
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            // As wide as its keys (not the style's minimum
                            // button width), so they end at the row's edge.
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
                        // Recording: the keys as pressed, Esc cancels, Enter saves.
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
                // What the keys are: taken (while recording), refused, or
                // a command's binding that takes them.
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

    // A shortcut whose macro is not loaded or changed: what it was for, why
    // it does not run, and Remove.
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

    // Keys while recording go here; the recorder takes every key but Tab
    // (MuseScore's record field): Esc cancels, Enter saves.
    Item {
        id: recorder
        objectName: "automationRecorder"
        focus: root.recording.length > 0
        // KB1's key router stands aside: no hovered panel's binding or
        // Space's play and pause takes a key being recorded.
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
                root.refusal = ""
            } else if (c.result === "refused") {
                root.pressed = null
                root.refusal = c.message
            }
        }
        onActiveFocusChanged: if (!activeFocus && root.recording.length > 0 && !reassign.visible) root.stopRecording()
    }

    // The script's menu: Reload, Edit script, and Unload (legacy
    // Automation::Remove for a Document script: the Script Info keeps it).
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
            onTriggered: root.controller.unload(scriptMenu.script.path)
        }
    }

    // A macro's menu (right click, the Menu key): Run, Set shortcut…,
    // Clear shortcut.
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

    // "Reassign shortcut" (MuseScore's question): the keys move to this
    // macro, the other bindings losing them; across windows legacy's
    // "Set anyway" keeps both.
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
        // Escape closes it as Cancel does: the recording ends.
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
